// HdTextures.cpp - High-resolution texture replacement by content hash. See
// HdTextures.h.

#include "HdTextures.h"

#include "AssetPath.h"
#include "HdTextureHash.h"
#include "PngImage.h"
#include "WebpImage.h"
#include "../platform/types.h"
#include "../platform/platform.h"
#include "../DebugPrint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <condition_variable>
#include <deque>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

const char* const kFolder = "HD";

bool s_replace = true;
bool s_dump = false;
bool s_active = false;  // s_dump || (s_replace && HD folder present), set by HdTextures_Configure

// Whether the HD folder is there, checked on the first upload rather than at
// configure time: the asset base is only final once config.ini has been read.
// 0 = not checked yet, 1 = present, -1 = absent.
int s_folderState = 0;
bool s_dumpDirMade = false;

// Every replacement in the folder: texture name -> path relative to HD/. Read
// once, with the folder check. A pack may keep its files in the folder itself
// or one level down, grouped however it likes (bgd/, mask/, skin/ ...); only
// the file name matters. dump/ is the game's own output and is not a source.
std::unordered_map<std::string, std::string> s_index;

// What is known about a hash: whether a replacement exists, and whether the
// image has been dumped. Keeps the per-upload cost at one hash for an image
// seen before - no file probe, no rewrite of the dump. Game thread only.
enum { kSeenMissing = 1, kSeenPresent = 2, kSeenDumped = 4 };
std::unordered_map<std::string, unsigned char> s_seen;

// A texture that changes every frame gets a new hash every frame. The table
// stays small for real content (a session sees a few thousand images); this is
// only the guard against one that never repeats.
const size_t kSeenLimit = 32768;

// ---------------------------------------------------------------------------
// Decoded replacements, and the worker that fills them ahead of use.
//
// Decoding a 1280x960 background and a 1024x1024 mask takes far longer than
// the frame a camera cut has, and the game uploads both again on EVERY cut. So
// a decoded replacement is kept, and a room's replacements are decoded on a
// worker thread as soon as the room is loaded (HdTextures_Prefetch), which is
// normally well before the player reaches the first cut.
//
// Everything the two threads share is in Shared, behind its mutex. The decoder
// has no global state, so a decode on the game thread and one on the worker
// can overlap.
// ---------------------------------------------------------------------------
struct Job {
    std::string name;
    std::string path;   // resolved on the game thread: AssetPath is not shared
};

struct Entry {
    HdPixels pixels;
    int width;
    int height;
    std::list<std::string>::iterator lru;
};

struct Shared {
    std::mutex mutex;
    std::condition_variable wake;      // work for the worker
    std::condition_variable done;      // the worker finished a job
    std::deque<Job> queue;
    // Names the workers are decoding now. Several workers: a room asks for
    // a dozen 4x pictures at once (its backgrounds, masks, models, effect
    // pages, doors), and one worker decoding them one after the other kept
    // the room load waiting.
    std::unordered_set<std::string> running;
    // Names whose file did not decode (a deferred upload stops waiting), and
    // the file each queued name was resolved to (to queue it again).
    std::unordered_set<std::string> failed;
    std::unordered_map<std::string, std::string> paths;
    std::unordered_map<std::string, Entry> cache;
    std::list<std::string> order;      // most recently used first
    size_t bytes = 0;
    // What a prefetch asked for: the room backgrounds and masks, which every
    // camera cut uploads again. Only these are kept once decoded. A model
    // skin, a room model or an effect page is uploaded once per room, so
    // keeping its pixels (a 4x skin is 8 MB) only pushed the backgrounds the
    // next cut needs out of the cache, and the cut decoded them again.
    std::unordered_set<std::string> wanted;
    bool started = false;
};

// A room with eight cameras holds about 73 MB of 4x backgrounds and masks.
// Twice that keeps the room just left as well, inside what a 32-bit process
// can spare.
const size_t kCacheBytes = 160u * 1024u * 1024u;

// Never destroyed: the worker is detached and may still be waiting on it when
// the process exits.
Shared& S(void)
{
    static Shared* shared = new Shared();
    return *shared;
}

bool DecodeFile(const char* path, std::vector<unsigned char>* out, int* w, int* h)
{
    size_t size = 0;
    void* file = plat_file_read_all(path, &size);
    if (file == NULL) return false;
    const bool ok = WebpDecodeRGBA((const unsigned char*)file, size, out, w, h);
    free(file);
    return ok;
}

// Caller holds the mutex.
void CacheInsert(Shared& sh, const std::string& name, HdPixels pixels, int w, int h)
{
    if (sh.cache.find(name) != sh.cache.end()) return;
    sh.order.push_front(name);
    Entry e;
    e.pixels = pixels;
    e.width = w;
    e.height = h;
    e.lru = sh.order.begin();
    sh.bytes += pixels->size();
    sh.cache[name] = e;
    // The entry just added always stays, whatever its size.
    while (sh.bytes > kCacheBytes && sh.order.size() > 1) {
        const std::string victim = sh.order.back();
        sh.order.pop_back();
        std::unordered_map<std::string, Entry>::iterator it = sh.cache.find(victim);
        sh.bytes -= it->second.pixels->size();
        sh.cache.erase(it);
    }
}

// Caller holds the mutex.
bool CacheGet(Shared& sh, const std::string& name, HdPixels* out, int* w, int* h)
{
    std::unordered_map<std::string, Entry>::iterator it = sh.cache.find(name);
    if (it == sh.cache.end()) return false;
    sh.order.splice(sh.order.begin(), sh.order, it->second.lru);
    *out = it->second.pixels;
    if (w != NULL) *w = it->second.width;
    if (h != NULL) *h = it->second.height;
    return true;
}

void WorkerMain(void)
{
    Shared& sh = S();
    std::unique_lock<std::mutex> lock(sh.mutex);
    for (;;) {
        while (sh.queue.empty()) sh.wake.wait(lock);
        const Job job = sh.queue.front();
        sh.queue.pop_front();
        sh.running.insert(job.name);
        lock.unlock();

        // No logging from here: the debug sink belongs to the game thread.
        std::shared_ptr<std::vector<unsigned char> > pixels(new std::vector<unsigned char>());
        int w = 0, h = 0;
        const bool ok = DecodeFile(job.path.c_str(), pixels.get(), &w, &h);

        lock.lock();
        if (ok) CacheInsert(sh, job.name, pixels, w, h);
        else sh.failed.insert(job.name);
        sh.running.erase(job.name);
        sh.done.notify_all();
    }
}

// Starts the decode workers the first time there is work. Three: the game
// thread keeps a core, and a 32-bit process has to keep the pictures in
// flight (a 4x background is 5 MB) within reason. Caller holds the mutex.
const int kWorkers = 3;

void StartWorkers(Shared& sh)
{
    if (sh.started) return;
    sh.started = true;
    for (int i = 0; i < kWorkers; ++i) std::thread(WorkerMain).detach();
}

// "<16 hex digits>.webp", any case -> the lower-cased name. False otherwise.
bool ReplacementName(const char* file, std::string* name)
{
    if (strlen(file) != 21) return false;
    const char* ext = file + 16;
    if (ext[0] != '.' || (ext[1] | 0x20) != 'w' || (ext[2] | 0x20) != 'e' ||
        (ext[3] | 0x20) != 'b' || (ext[4] | 0x20) != 'p') {
        return false;
    }
    name->assign(file, 16);
    for (size_t i = 0; i < 16; ++i) {
        char& c = (*name)[i];
        if (c >= 'A' && c <= 'F') c = (char)(c + 32);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

struct IndexWalk {
    std::string prefix;                    // "" for HD/ itself, "skin/" below it
    std::vector<std::string> folders;      // sub-folders seen at the top level
};

void IndexVisit(const char* entry, BOOL isDirectory, void* user)
{
    IndexWalk* walk = (IndexWalk*)user;
    if (isDirectory) {
        if (walk->prefix.empty() && _stricmp(entry, "dump") != 0) {
            walk->folders.push_back(entry);
        }
        return;
    }
    std::string name;
    // The folder itself is listed first, so a file there wins over a copy of
    // the same name in a sub-folder.
    if (ReplacementName(entry, &name) && s_index.find(name) == s_index.end()) {
        s_index[name] = walk->prefix + entry;
    }
}

bool FolderPresent(void)
{
    if (s_folderState == 0) {
        char dir[300];
        char norm[300];
        // No trailing separator: this names the directory itself.
        const bool there = AssetSiblingPath(kFolder, "", dir, sizeof(dir)) != NULL;
        size_t n = there ? strlen(dir) : 0;
        while (n > 1 && (dir[n - 1] == '/' || dir[n - 1] == '\\')) dir[--n] = '\0';

        IndexWalk top;
        s_folderState = (there && plat_dir_list(plat_normalize_path(dir, norm, sizeof(norm)),
                                                IndexVisit, &top)) ? 1 : -1;
        for (size_t i = 0; s_folderState > 0 && i < top.folders.size(); ++i) {
            char sub[300];
            if (AssetSiblingPath(kFolder, top.folders[i].c_str(), sub, sizeof(sub)) == NULL) continue;
            IndexWalk walk;
            walk.prefix = top.folders[i] + "/";
            plat_dir_list(plat_normalize_path(sub, norm, sizeof(norm)), IndexVisit, &walk);
        }
        if (s_folderState > 0) {
            dbg_printf("[hd] %s: %u replacement(s)\n", dir, (unsigned)s_index.size());
        } else {
            dbg_printf("[hd] %s: no folder\n", dir);
        }
    }
    return s_folderState > 0;
}

// The replacement's path for `name`, normalized, into `out`. False when the
// folder holds none.
bool ReplacementPath(const char* name, char* out, size_t outSize)
{
    std::unordered_map<std::string, std::string>::const_iterator it = s_index.find(name);
    if (it == s_index.end()) return false;
    char path[300];
    if (AssetSiblingPath(kFolder, it->second.c_str(), path, sizeof(path)) == NULL) return false;
    plat_normalize_path(path, out, outSize);
    return true;
}

// Writes `rgba` to "HD/dump/<name>.png". `name` may hold '/' (a named
// texture's key); every folder on the way is created.
void Dump(const char* name, int width, int height, const void* rgba)
{
    char rel[260];
    char path[300];
    char norm[300];
    if (!s_dumpDirMade) {
        // Both levels: the dump is how a pack is started, so the HD folder
        // usually does not exist yet.
        if (AssetSiblingPath(kFolder, "", path, sizeof(path)) != NULL) {
            plat_mkdir(plat_normalize_path(path, norm, sizeof(norm)));
        }
        if (AssetSiblingPath(kFolder, "dump", path, sizeof(path)) != NULL) {
            plat_mkdir(plat_normalize_path(path, norm, sizeof(norm)));
        }
        s_dumpDirMade = true;
    }
    if (snprintf(rel, sizeof(rel), "dump/%s.png", name) >= (int)sizeof(rel)) return;
    for (char* sep = strchr(rel + 5, '/'); sep != NULL; sep = strchr(sep + 1, '/')) {
        *sep = '\0';
        if (AssetSiblingPath(kFolder, rel, path, sizeof(path)) != NULL) {
            plat_mkdir(plat_normalize_path(path, norm, sizeof(norm)));
        }
        *sep = '/';
    }
    if (AssetSiblingPath(kFolder, rel, path, sizeof(path)) == NULL) return;

    std::vector<unsigned char> png;
    PngEncodeRGBA((const unsigned char*)rgba, width, height, &png);
    if (!png.empty()) {
        plat_file_write(plat_normalize_path(path, norm, sizeof(norm)), png.data(), png.size());
    }
}

bool Usable(int width, int height, const void* rgba)
{
    // The 1x1 / 2x2 flat fills the renderer makes for itself are not art.
    return rgba != NULL && width > 0 && height > 0 && width * height >= 16;
}

}  // namespace

void HdTextures_Configure(bool replace, bool dump)
{
    s_replace = replace;
    s_dump = dump;
    // Decided once, here: the asset root, version and mode are already set
    // when the config is read, and the HD folder is indexed by this call
    // instead of on the first texture upload.
    s_active = s_dump || (s_replace && FolderPresent());
}

bool HdTextures_ReplaceEnabled(void)
{
    return s_replace;
}

bool HdTextures_Active(void)
{
    return s_active;
}

void HdTextures_HashName(int width, int height, const void* rgba, char* out)
{
    hdtex::HashName(width, height, rgba, out);
}

bool HdTextures_Find(int width, int height, const void* rgba,
                     HdPixels* out, int* outWidth, int* outHeight)
{
    if (out == NULL || !Usable(width, height, rgba)) return false;
    const bool replace = s_replace && FolderPresent();
    if (!replace && !s_dump) return false;

    char name[17];
    HdTextures_HashName(width, height, rgba, name);

    if (s_seen.size() >= kSeenLimit) s_seen.clear();
    unsigned char& seen = s_seen[name];

    if (s_dump && (seen & kSeenDumped) == 0) {
        Dump(name, width, height, rgba);
        seen |= kSeenDumped;
    }
    if (!replace || (seen & kSeenMissing) != 0) return false;

    Shared& sh = S();
    {
        std::unique_lock<std::mutex> lock(sh.mutex);
        if (CacheGet(sh, name, out, outWidth, outHeight)) return true;
        // The worker is on this very image: let it finish instead of decoding
        // the same file twice.
        while (sh.running.count(name)) sh.done.wait(lock);
        if (CacheGet(sh, name, out, outWidth, outHeight)) return true;
        // Still queued: it is needed now, so it is decoded here.
        for (std::deque<Job>::iterator it = sh.queue.begin(); it != sh.queue.end(); ++it) {
            if (it->name == name) {
                sh.queue.erase(it);
                break;
            }
        }
    }

    char path[300];
    std::shared_ptr<std::vector<unsigned char> > pixels(new std::vector<unsigned char>());
    int w = 0, h = 0;
    if (!ReplacementPath(name, path, sizeof(path))) {
        seen |= kSeenMissing;
        return false;
    }
    if (!DecodeFile(path, pixels.get(), &w, &h)) {
        // Reported once: the entry is then remembered as missing.
        dbg_printf("[hd] %s is not a WebP this build can decode - ignored\n", path);
        seen |= kSeenMissing;
        return false;
    }
    if ((seen & kSeenPresent) == 0) {
        dbg_printf("[hd] %s.webp %dx%d replaces a %dx%d texture\n", name, w, h, width, height);
        seen |= kSeenPresent;
    }
    {
        std::lock_guard<std::mutex> lock(sh.mutex);
        if (sh.wanted.count(name)) CacheInsert(sh, name, pixels, w, h);
    }
    *out = pixels;
    if (outWidth != NULL)  *outWidth  = w;
    if (outHeight != NULL) *outHeight = h;
    return true;
}

void HdTextures_Prefetch(int width, int height, const void* rgba)
{
    if (!Usable(width, height, rgba) || !s_replace || !FolderPresent()) return;

    char name[17];
    HdTextures_HashName(width, height, rgba, name);

    if (s_seen.size() >= kSeenLimit) s_seen.clear();
    unsigned char& seen = s_seen[name];
    if ((seen & kSeenMissing) != 0) return;

    char path[300];
    if (!ReplacementPath(name, path, sizeof(path))) {
        seen |= kSeenMissing;
        return;
    }

    Shared& sh = S();
    std::lock_guard<std::mutex> lock(sh.mutex);
    sh.wanted.insert(name);
    if (sh.cache.find(name) != sh.cache.end() || sh.running.count(name)) return;
    for (std::deque<Job>::iterator it = sh.queue.begin(); it != sh.queue.end(); ++it) {
        if (it->name == name) return;
    }
    Job job;
    job.name = name;
    job.path = path;
    sh.paths[name] = path;
    sh.queue.push_back(job);
    StartWorkers(sh);
    sh.wake.notify_all();
}

void HdTextures_PrefetchCancel(void)
{
    Shared& sh = S();
    std::lock_guard<std::mutex> lock(sh.mutex);
    sh.queue.clear();
}

// ---------------------------------------------------------------------------
// Replacement by source file
// ---------------------------------------------------------------------------
namespace {

struct OwnerEntry {
    const void* tag;
    std::string key;
    bool perPalette;
};

// Owner (a PSXTexture bank) -> the file it holds. Game thread only.
std::unordered_map<const void*, OwnerEntry> s_owners;

// Per key: kSeenMissing / kSeenPresent / kSeenDumped, as s_seen is per hash.
// Kept apart from s_seen so a key can never collide with a hash name.
std::unordered_map<std::string, unsigned char> s_namedSeen;

// Per key: whether a mode's key may borrow the base release's HD picture of
// the same name (HdTextures_SetBaseFallback). A key not in here may.
std::unordered_map<std::string, bool> s_baseFallback;

}  // namespace

void HdTextures_SetBaseFallback(const char* key, bool allowed)
{
    if (key == NULL || key[0] == '\0') return;
    s_baseFallback[key] = allowed;
}

bool HdTextures_BaseFallbackKnown(const char* key)
{
    return key != NULL && s_baseFallback.find(key) != s_baseFallback.end();
}

void HdTextures_SetOwnerKey(const void* owner, const void* tag, const char* key)
{
    if (owner == NULL) return;
    if (key == NULL || key[0] == '\0') {
        s_owners.erase(owner);
        return;
    }
    OwnerEntry& e = s_owners[owner];
    e.tag = tag;
    e.key = key;
    e.perPalette = false;
}

void HdTextures_SetOwnerKeyPerPalette(const void* owner, const void* tag, const char* key)
{
    HdTextures_SetOwnerKey(owner, tag, key);
    std::unordered_map<const void*, OwnerEntry>::iterator it = s_owners.find(owner);
    if (it != s_owners.end()) it->second.perPalette = true;
}

bool HdTextures_OwnerPerPalette(const void* owner)
{
    std::unordered_map<const void*, OwnerEntry>::const_iterator it = s_owners.find(owner);
    return it != s_owners.end() && it->second.perPalette;
}

const char* HdTextures_OwnerKey(const void* owner, const void** tag)
{
    std::unordered_map<const void*, OwnerEntry>::const_iterator it = s_owners.find(owner);
    if (it == s_owners.end()) return NULL;
    if (tag != NULL) *tag = it->second.tag;
    return it->second.key.c_str();
}

bool HdTextures_NamedDumpWanted(const char* key)
{
    if (!s_dump || key == NULL || key[0] == '\0') return false;
    std::unordered_map<std::string, unsigned char>::const_iterator it = s_namedSeen.find(key);
    return it == s_namedSeen.end() || (it->second & kSeenDumped) == 0;
}

void HdTextures_DumpNamed(const char* key, int width, int height, const void* rgba)
{
    if (!HdTextures_NamedDumpWanted(key) || !Usable(width, height, rgba)) return;
    Dump(key, width, height, rgba);
    s_namedSeen[key] |= kSeenDumped;
}

namespace {

// '@' cannot start a hash name, so named and hashed replacements share the
// cache (and the worker's job names) safely.
std::string NamedCacheName(const char* key)
{
    return std::string("@") + key;
}

// "HD/<key><ext>" (".webp" by default), normalized, into `out`. False when it
// does not fit.
bool FileThere(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (f == NULL) return false;
    fclose(f);
    return true;
}

// A mode's key ("DC/Data/...", "SATURN/...") names its own HD tree. When that
// tree has no picture for it, the base release's HD ("USA/Data/..." or
// "JPN/...") stands in: a mode only ships the HD art that differs, and the
// rest is the base game's, so the screen would otherwise drop to the low-res
// original while the base HD pack sits unused.
bool NamedPath(const char* key, char* out, size_t outSize, const char* ext = ".webp")
{
    char rel[260];
    char path[300];
    if (snprintf(rel, sizeof(rel), "%s%s", key, ext) >= (int)sizeof(rel) ||
        AssetSiblingPath(kFolder, rel, path, sizeof(path)) == NULL) {
        return false;
    }
    plat_normalize_path(path, out, outSize);

    const char* slash = strchr(key, '/');
    if (slash == NULL || FileThere(out)) return true;
    const size_t treeLen = (size_t)(slash - key);
    if ((treeLen == 3 && _strnicmp(key, "USA", 3) == 0) ||
        (treeLen == 3 && _strnicmp(key, "JPN", 3) == 0)) {
        return true;
    }
    // A mode's file of the same name that is NOT the base file's picture (the
    // DC's EM1032 is its own outfit, the PC's is the PC outfit): the base HD
    // art was drawn for another layout.
    std::unordered_map<std::string, bool>::const_iterator fb = s_baseFallback.find(key);
    if (fb != s_baseFallback.end() && !fb->second) return true;
    char baseRel[260];
    char basePath[300];
    char baseNorm[300];
    if (snprintf(baseRel, sizeof(baseRel), "%s%s%s", GetAssetVersion() ? "JPN" : "USA",
                 slash, ext) >= (int)sizeof(baseRel) ||
        AssetSiblingPath(kFolder, baseRel, basePath, sizeof(basePath)) == NULL) {
        return true;
    }
    plat_normalize_path(basePath, baseNorm, sizeof(baseNorm));
    if (FileThere(baseNorm) && strlen(baseNorm) < outSize) {
        memcpy(out, baseNorm, strlen(baseNorm) + 1);
    }
    return true;
}

// The upload sources (HdTextures_BeginSource), innermost last.
struct Source {
    std::string key;
    int width;
    int height;
    // A region source (HdTextures_BeginRegionSource): the upload is this
    // rectangle of a pageW x pageH page.
    bool region = false;
    int rx = 0, ry = 0, rw = 0, rh = 0, pageW = 0, pageH = 0;
    // A page source (HdTextures_BeginPageSource): the named picture stands for
    // `reference`, and the upload differs from it only where this is set.
    const void* reference = NULL;
};
std::vector<Source> s_sources;

// What LoadFile read where (HdTextures_NoteLoad). A few dozen at most: a new
// load drops every record it overlaps.
struct LoadRecord {
    const unsigned char* start;
    size_t size;
    unsigned int sum;
    std::string key;
};
std::vector<LoadRecord> s_loads;
const size_t kLoadRecords = 64;

// FNV-1a: cheap, and only has to notice that a buffer was written over.
unsigned int Checksum(const unsigned char* p, size_t n)
{
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

}  // namespace

bool HdTextures_NamedFile(const char* key, const char* ext, char* out, size_t outSize)
{
    if (key == NULL || key[0] == '\0' || ext == NULL || out == NULL || outSize == 0 ||
        !s_replace || !FolderPresent()) {
        return false;
    }
    char norm[300];
    if (!NamedPath(key, norm, sizeof(norm), ext) || !FileThere(norm) ||
        strlen(norm) >= outSize) {
        return false;
    }
    memcpy(out, norm, strlen(norm) + 1);
    return true;
}

bool HdTextures_FindNamed(const char* key, HdPixels* out, int* outWidth, int* outHeight)
{
    if (out == NULL || key == NULL || key[0] == '\0') return false;
    if (!s_replace || !FolderPresent()) return false;

    unsigned char& seen = s_namedSeen[key];
    if ((seen & kSeenMissing) != 0) return false;

    const std::string cacheName = NamedCacheName(key);
    Shared& sh = S();
    {
        std::unique_lock<std::mutex> lock(sh.mutex);
        if (CacheGet(sh, cacheName, out, outWidth, outHeight)) return true;
        // The worker is on this very file: let it finish (see HdTextures_Find).
        while (sh.running.count(cacheName)) sh.done.wait(lock);
        if (CacheGet(sh, cacheName, out, outWidth, outHeight)) return true;
        for (std::deque<Job>::iterator it = sh.queue.begin(); it != sh.queue.end(); ++it) {
            if (it->name == cacheName) {
                sh.queue.erase(it);
                break;
            }
        }
    }

    char norm[300];
    if (!NamedPath(key, norm, sizeof(norm))) {
        seen |= kSeenMissing;
        return false;
    }
    std::shared_ptr<std::vector<unsigned char> > pixels(new std::vector<unsigned char>());
    int w = 0, h = 0;
    size_t size = 0;
    void* file = plat_file_read_all(norm, &size);
    if (file == NULL) {
        seen |= kSeenMissing;           // the usual case: no file for this key
        return false;
    }
    const bool ok = WebpDecodeRGBA((const unsigned char*)file, size, pixels.get(), &w, &h);
    free(file);
    if (!ok) {
        dbg_printf("[hd] %s is not a WebP this build can decode - ignored\n", norm);
        seen |= kSeenMissing;
        return false;
    }
    if ((seen & kSeenPresent) == 0) {
        dbg_printf("[hd] %s.webp %dx%d replaces its texture\n", key, w, h);
        seen |= kSeenPresent;
    }
    {
        std::lock_guard<std::mutex> lock(sh.mutex);
        if (sh.wanted.count(cacheName)) CacheInsert(sh, cacheName, pixels, w, h);
    }
    *out = pixels;
    if (outWidth != NULL)  *outWidth  = w;
    if (outHeight != NULL) *outHeight = h;
    return true;
}

bool HdTextures_PrefetchNamed(const char* key)
{
    if (key == NULL || key[0] == '\0' || !s_replace || !FolderPresent()) return false;
    unsigned char& seen = s_namedSeen[key];
    if ((seen & kSeenMissing) != 0) return false;

    char norm[300];
    if (!NamedPath(key, norm, sizeof(norm)) || !FileThere(norm)) {
        seen |= kSeenMissing;
        return false;
    }
    const std::string cacheName = NamedCacheName(key);
    Shared& sh = S();
    std::lock_guard<std::mutex> lock(sh.mutex);
    sh.wanted.insert(cacheName);
    if (sh.cache.find(cacheName) != sh.cache.end() || sh.running.count(cacheName)) return true;
    for (std::deque<Job>::iterator it = sh.queue.begin(); it != sh.queue.end(); ++it) {
        if (it->name == cacheName) return true;
    }
    Job job;
    job.name = cacheName;
    job.path = norm;
    sh.paths[cacheName] = norm;
    sh.queue.push_back(job);
    StartWorkers(sh);
    sh.wake.notify_all();
    return true;
}

void HdTextures_BeginSource(const char* key, int width, int height)
{
    Source s;
    s.key = (key != NULL) ? key : "";
    s.width = width;
    s.height = height;
    s_sources.push_back(s);
}

void HdTextures_BeginRegionSource(const char* key, int x, int y, int w, int h,
                                  int pageW, int pageH)
{
    Source s;
    s.key = (key != NULL) ? key : "";
    s.width = w;              // only an upload of the region's size is named
    s.height = h;
    s.region = true;
    s.rx = x;
    s.ry = y;
    s.rw = w;
    s.rh = h;
    s.pageW = pageW;
    s.pageH = pageH;
    s_sources.push_back(s);
}

void HdTextures_BeginPageSource(const char* key, int width, int height,
                                const void* reference)
{
    Source s;
    s.key = (key != NULL && reference != NULL) ? key : "";
    s.width = width;
    s.height = height;
    s.reference = reference;
    s_sources.push_back(s);
}

const void* HdTextures_SourceReference(void)
{
    if (s_sources.empty()) return NULL;
    return s_sources.back().reference;
}

bool HdTextures_SourceRegion(int* x, int* y, int* w, int* h, int* pageW, int* pageH)
{
    if (s_sources.empty() || !s_sources.back().region) return false;
    const Source& s = s_sources.back();
    *x = s.rx;
    *y = s.ry;
    *w = s.rw;
    *h = s.rh;
    *pageW = s.pageW;
    *pageH = s.pageH;
    return true;
}

const char* HdTextures_KeyForPath(const char* path, char* out, size_t outSize)
{
    if (out == NULL || outSize == 0) return "";
    if (!HdTextures_Active() || path == NULL || AssetSourceKey(path, out, outSize) == NULL) {
        out[0] = '\0';
    }
    return out;
}

void HdTextures_EndSource(void)
{
    if (!s_sources.empty()) s_sources.pop_back();
}

// The picture HdTextures_BeginPicture opened, matched by the upload's pixels.
namespace {
struct Picture {
    int width = 0, height = 0;
    const void* rgba = NULL;
    int hdWidth = 0, hdHeight = 0;
    const void* hdRgba = NULL;
};
Picture s_picture;
}  // namespace

void HdTextures_BeginPicture(int width, int height, const void* rgba,
                             int hdWidth, int hdHeight, const void* hdRgba)
{
    s_picture = Picture();
    if (rgba == NULL || hdRgba == NULL || width <= 0 || height <= 0 ||
        hdWidth <= 0 || hdHeight <= 0) {
        return;
    }
    s_picture.width = width;
    s_picture.height = height;
    s_picture.rgba = rgba;
    s_picture.hdWidth = hdWidth;
    s_picture.hdHeight = hdHeight;
    s_picture.hdRgba = hdRgba;
}

void HdTextures_EndPicture(void)
{
    s_picture = Picture();
}

bool HdTextures_PictureFor(int width, int height, const void* rgba,
                           const void** hdRgba, int* hdWidth, int* hdHeight)
{
    const Picture& p = s_picture;
    if (p.hdRgba == NULL || rgba == NULL || width != p.width || height != p.height) {
        return false;
    }
    if (memcmp(rgba, p.rgba, (size_t)width * (size_t)height * 4) != 0) return false;
    dbg_printf("[hd] drawn %dx%d picture replaces a %dx%d texture\n",
               p.hdWidth, p.hdHeight, width, height);
    *hdRgba = p.hdRgba;
    *hdWidth = p.hdWidth;
    *hdHeight = p.hdHeight;
    return true;
}

const char* HdTextures_SourceFor(int width, int height)
{
    if (s_sources.empty()) return NULL;
    const Source& s = s_sources.back();
    if (s.key.empty()) return NULL;
    if (s.width != 0 && (width != s.width || height != s.height)) return NULL;
    return s.key.c_str();
}

void HdTextures_NoteLoad(const void* buffer, size_t size, const char* assetPath)
{
    if (buffer == NULL || size == 0) return;
    const unsigned char* start = (const unsigned char*)buffer;
    // Whatever this load overwrote is gone.
    for (size_t i = 0; i < s_loads.size();) {
        const LoadRecord& r = s_loads[i];
        if (r.start < start + size && start < r.start + r.size) {
            s_loads.erase(s_loads.begin() + i);
        } else {
            ++i;
        }
    }
    if (!HdTextures_Active()) return;
    char key[160];
    if (assetPath == NULL || AssetSourceKey(assetPath, key, sizeof(key)) == NULL) return;
    if (s_loads.size() >= kLoadRecords) s_loads.erase(s_loads.begin());
    LoadRecord r;
    r.start = start;
    r.size = size;
    r.sum = Checksum(start, size);
    r.key = key;
    s_loads.push_back(r);
}

const char* HdTextures_KeyForBuffer(const void* ptr, char* out, size_t outSize)
{
    if (out == NULL || outSize == 0) return "";
    out[0] = '\0';
    const unsigned char* p = (const unsigned char*)ptr;
    for (size_t i = s_loads.size(); i-- > 0;) {
        LoadRecord& r = s_loads[i];
        if (p < r.start || p >= r.start + r.size) continue;
        if (Checksum(r.start, r.size) != r.sum) {
            s_loads.erase(s_loads.begin() + i);   // written over since
            return out;
        }
        // A TIM's pixels start 20 bytes in (g_TimImageBuffer__bitmap): that
        // is still the whole file.
        const size_t offset = (size_t)(p - r.start);
        if (offset == 0 || offset == 20) {
            snprintf(out, outSize, "%s", r.key.c_str());
        } else {
            snprintf(out, outSize, "%s@%X", r.key.c_str(), (unsigned)offset);
        }
        return out;
    }
    return out;
}

HdBufferScope::HdBufferScope(const void* ptr)
{
    // An enclosing source that names something wins: keep it.
    if (!s_sources.empty() && !s_sources.back().key.empty()) {
        s_sources.push_back(s_sources.back());
        return;
    }
    char key[180];
    HdTextures_BeginSource(HdTextures_KeyForBuffer(ptr, key, sizeof(key)), 0, 0);
}

// ---------------------------------------------------------------------------
// Deferred uploads (see HdTextures.h)
// ---------------------------------------------------------------------------
namespace {
int s_deferDepth = 0;
}

void HdTextures_BeginDefer(void) { ++s_deferDepth; }
void HdTextures_EndDefer(void) { if (s_deferDepth > 0) --s_deferDepth; }
bool HdTextures_Deferring(void) { return s_deferDepth > 0; }

int HdTextures_TryNamed(const char* key, HdPixels* out, int* outWidth, int* outHeight,
                        std::string* token)
{
    if (!HdTextures_Deferring()) return HdTextures_FindNamed(key, out, outWidth, outHeight) ? 1 : 0;
    if (out == NULL || key == NULL || key[0] == '\0' || !s_replace || !FolderPresent()) return 0;
    const std::string cacheName = NamedCacheName(key);
    {
        Shared& sh = S();
        std::lock_guard<std::mutex> lock(sh.mutex);
        if (CacheGet(sh, cacheName, out, outWidth, outHeight)) return 1;
    }
    // Queues it (or finds it queued or running); false: there is no such file.
    if (!HdTextures_PrefetchNamed(key)) return 0;
    if (token != NULL) *token = cacheName;
    return 2;
}

int HdTextures_TryHash(int width, int height, const void* rgba,
                       HdPixels* out, int* outWidth, int* outHeight, std::string* token)
{
    if (!HdTextures_Deferring())
        return HdTextures_Find(width, height, rgba, out, outWidth, outHeight) ? 1 : 0;
    if (out == NULL || !Usable(width, height, rgba)) return 0;
    const bool replace = s_replace && FolderPresent();
    if (!replace && !s_dump) return 0;

    char name[17];
    HdTextures_HashName(width, height, rgba, name);
    if (s_seen.size() >= kSeenLimit) s_seen.clear();
    unsigned char& seen = s_seen[name];
    if (s_dump && (seen & kSeenDumped) == 0) {
        Dump(name, width, height, rgba);
        seen |= kSeenDumped;
    }
    if (!replace || (seen & kSeenMissing) != 0) return 0;
    {
        Shared& sh = S();
        std::lock_guard<std::mutex> lock(sh.mutex);
        if (CacheGet(sh, name, out, outWidth, outHeight)) return 1;
    }
    char path[300];
    if (!ReplacementPath(name, path, sizeof(path))) {
        seen |= kSeenMissing;
        return 0;
    }
    HdTextures_Prefetch(width, height, rgba);
    if (token != NULL) *token = name;
    return 2;
}

int HdTextures_Poll(const std::string& token, HdPixels* out, int* outWidth, int* outHeight)
{
    Shared& sh = S();
    std::lock_guard<std::mutex> lock(sh.mutex);
    if (CacheGet(sh, token, out, outWidth, outHeight)) return 1;
    if (sh.failed.count(token)) return -1;
    if (sh.running.count(token)) return 0;
    for (std::deque<Job>::iterator it = sh.queue.begin(); it != sh.queue.end(); ++it) {
        if (it->name == token) return 0;
    }
    // Neither decoded nor on its way: the queue was cancelled (a new room)
    // or the cache let it go. Queue it again.
    std::unordered_map<std::string, std::string>::iterator p = sh.paths.find(token);
    if (p == sh.paths.end()) return -1;
    Job job;
    job.name = token;
    job.path = p->second;
    sh.wanted.insert(token);
    sh.queue.push_front(job);
    StartWorkers(sh);
    sh.wake.notify_all();
    return 0;
}

// dc_iso.cpp - minimal ISO9660/Joliet reader for /cd (Dreamcast).
//
// Why: every fopen() on KOS's iso9660 looks the name up by scanning the
// directory's sectors through a 16-block LRU cache. The game's Sound/ and
// Voice/ directories are ~17 sectors each, so every lookup there misses the
// cache completely and re-reads the whole directory - ~17 GD-ROM commands,
// ~300 ms per sound bank under Flycast, seconds per room entry. Rebuilding KOS
// with a bigger cache works but is not something a port should require.
//
// So /cd reads of whole files (LoadFile, plat_file_read_all) and the movie
// player resolve names here instead: each directory is read once (one DMA
// command for all its sectors) into an index of name -> (LBA, size), and file
// data is read through one CD stream per read (cdrom_stream_*), like KOS's
// fast path, but only ever in whole sectors. No iso9660 lookup, no iso9660
// file state. Same sector addressing as KOS (LBA + 150).
#include "../platform.h"
#include "dc_platform.h"

#include <kos/nmmgr.h>
#include <kos/fs.h>
#include <kos/mutex.h>
#include <kos/thread.h>
#include <dc/cdrom.h>
#include <dc/fs_iso9660.h>
#include <dc/g2bus.h>
#include <dc/spu.h>
#include <dc/sound/sound.h>
#include <arch/cache.h>
#include <kos/sem.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <malloc.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <new>

// crash.cpp: shown in the stall report. Declared at file scope - inside the
// anonymous namespace below it would become a different, internal variable.
extern "C" volatile int g_dcInCd;
// Bumped after every completed disc request: the main-loop watchdog counts it
// as a sign of life, so a long load (one main-loop pass reading a whole room
// from a slow CD-R) is not reported as a hang - a drive that stops is.
extern "C" { volatile uint32_t g_dcIoProgress = 0; }

namespace {

struct Entry { uint32_t lba; uint32_t size; bool dir; std::string name; };
typedef std::unordered_map<std::string, Entry> Dir;   // lower-case name -> entry

bool     s_tried = false;
bool     s_ok = false;
uint32_t s_rootLba = 0, s_rootSize = 0;
bool     s_joliet = false;
bool     s_gd = false;
mutex_t  s_isoLock = RECURSIVE_MUTEX_INITIALIZER;
std::unordered_map<uint32_t, Dir*> s_dirs;            // by directory LBA

const uint32_t kBounceSectors = 32;                   // 64 KB
uint8_t* s_bounce = nullptr;                          // 32-byte aligned

inline uint32_t LE32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool ReadSectors(uint32_t lba, uint32_t count, void* dst)
{
    // KOS addresses CD sectors with the 150-sector pregap added (fs_iso9660).
    g_dcInCd = 1;
    const bool ok = cdrom_read_sectors_ex(dst, lba + 150, count, true) == ERR_OK;
    g_dcIoProgress = g_dcIoProgress + 1;
    g_dcInCd = 0;
    return ok;
}

bool Init()
{
    if (s_tried) return s_ok;
    s_tried = true;
    s_bounce = (uint8_t*)memalign(32, kBounceSectors * 2048);
    if (s_bounce == nullptr) return false;

    // GD-ROM: the game lives in the high-density area (track 3 at LBA
    // 45000). The low-density TOC only lists tracks 1-2, so asking it for the
    // data track - what KOS's own fs_iso9660 does - finds the placeholder
    // track 1. CD-R/CDI: one area, the low-density TOC is the whole disc.
    int status = 0, type = 0;
    cdrom_get_status(&status, &type);
    s_gd = (type == 0x80);                              // CD_GDROM
    cd_toc_t toc;
    if (cdrom_read_toc(&toc, s_gd) != 0) { printf("[ISO] no TOC\n"); return false; }
    const uint32_t base = cdrom_locate_data_track(&toc);
    if (base == 0) { printf("[ISO] no data track\n"); return false; }
    const uint32_t vd = base - 150 + 16;               // first volume descriptor

    // Prefer Joliet (what KOS uses, and what keeps the long names).
    for (uint32_t i = 1; i <= 3 && !s_ok; ++i) {
        if (!ReadSectors(vd + i, 1, s_bounce)) break;
        if (memcmp(s_bounce, "\x02" "CD001", 6) == 0 && s_bounce[88] == '%' && s_bounce[89] == '/' &&
            (s_bounce[90] == '@' || s_bounce[90] == 'C' || s_bounce[90] == 'E')) {
            s_rootLba = LE32(s_bounce + 156 + 2);
            s_rootSize = LE32(s_bounce + 156 + 10);
            s_joliet = true;
            s_ok = true;
        }
    }
    if (!s_ok) {
        if (!ReadSectors(vd, 1, s_bounce) || memcmp(s_bounce, "\x01" "CD001", 6) != 0) {
            printf("[ISO] not an ISO9660 disc\n");
            return false;
        }
        s_rootLba = LE32(s_bounce + 156 + 2);
        s_rootSize = LE32(s_bounce + 156 + 10);
        s_ok = true;
    }
    printf("[ISO] direct reader: %s %s, root at LBA %u\n", s_gd ? "GD-ROM" : "CD",
           s_joliet ? "Joliet" : "ISO9660", (unsigned)s_rootLba);
    return true;
}

std::string Lower(std::string s)
{
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

// Directory record name -> plain name (Joliet UCS-2 BE, ";1" and a trailing
// '.' stripped, the same normalisation KOS's fncompare applies).
std::string RecordName(const uint8_t* name, int len)
{
    std::string out;
    if (s_joliet) {
        for (int i = 0; i + 1 < len; i += 2) {
            const unsigned ch = (name[i] << 8) | name[i + 1];
            out += (ch < 128) ? (char)ch : '?';
        }
    } else {
        out.assign((const char*)name, (size_t)len);
    }
    const size_t semi = out.find(';');
    if (semi != std::string::npos) out.erase(semi);
    if (!out.empty() && out.back() == '.') out.pop_back();
    return out;
}

const Dir* LoadDir(uint32_t lba, uint32_t size)
{
    auto it = s_dirs.find(lba);
    if (it != s_dirs.end()) return it->second;

    const uint32_t sectors = (size + 2047) / 2048;
    uint8_t* raw = (uint8_t*)memalign(32, (size_t)sectors * 2048);
    if (raw == nullptr) return nullptr;
    if (!ReadSectors(lba, sectors, raw)) { free(raw); return nullptr; }

    Dir* d = new Dir();
    for (uint32_t s = 0; s < sectors; ++s) {
        const uint8_t* sec = raw + (size_t)s * 2048;
        uint32_t off = 0;
        while (off < 2048) {
            const uint8_t* r = sec + off;
            const uint32_t len = r[0];
            if (len == 0) break;                         // rest of the sector is padding
            if (off + len > 2048 || len < 34) break;
            const int nlen = r[32];
            if (!(nlen == 1 && (r[33] == 0 || r[33] == 1))) {   // skip "." and ".."
                Entry e;
                e.lba = LE32(r + 2);
                e.size = LE32(r + 10);
                e.dir = (r[25] & 0x02) != 0;
                e.name = RecordName(r + 33, nlen);
                d->emplace(Lower(e.name), e);
            }
            off += len;
        }
    }
    free(raw);
    s_dirs.emplace(lba, d);
    return d;
}

// Path relative to the disc root ("USA/Sound/x.wav", "/USA", "" = root),
// either slash, any case -> entry (files and directories).
bool LookupRel(const std::string& rel, Entry* out)
{
    if (!Init()) return false;
    std::string p(rel);
    for (char& c : p) if (c == '\\') c = '/';
    Entry cur = { s_rootLba, s_rootSize, true, "" };
    size_t i = 0;
    while (i < p.size()) {
        size_t end = p.find('/', i);
        if (end == std::string::npos) end = p.size();
        const std::string comp = p.substr(i, end - i);
        i = end + 1;
        if (comp.empty() || comp == ".") continue;
        if (!cur.dir) return false;
        const Dir* d = LoadDir(cur.lba, cur.size);
        if (d == nullptr) return false;
        auto it = d->find(Lower(comp));
        if (it == d->end()) return false;
        cur = it->second;
    }
    *out = cur;
    return true;
}

// "/cd/USA/Sound/x.wav" -> file entry (directories are not files).
bool Lookup(const char* path, Entry* out)
{
    if (path == nullptr) return false;
    std::string p(path);
    for (char& c : p) if (c == '\\') c = '/';
    if (p.compare(0, 4, "/cd/") != 0) return false;
    if (!LookupRel(p.substr(4), out)) return false;
    return !out->dir;
}

// Read [offset, offset + len) by plain sector commands (fallback path).
bool ReadRangeSectors(uint32_t lba, uint32_t offset, uint32_t len, uint8_t* dst)
{
    while (len > 0) {
        const uint32_t sec = offset / 2048;
        const uint32_t in = offset % 2048;
        if (in == 0 && len >= 2048 && (((uintptr_t)dst) & 31) == 0) {
            uint32_t n = len / 2048;
            if (n > 256) n = 256;
            if (!ReadSectors(lba + sec, n, dst)) return false;
            dst += n * 2048; offset += n * 2048; len -= n * 2048;
            continue;
        }
        uint32_t n = (in + len + 2047) / 2048;
        if (n > kBounceSectors) n = kBounceSectors;
        if (!ReadSectors(lba + sec, n, s_bounce)) return false;
        uint32_t take = n * 2048 - in;
        if (take > len) take = len;
        memcpy(dst, s_bounce + in, take);
        dst += take; offset += take; len -= take;
    }
    return true;
}

// Read [offset, offset + len) through ONE CD stream (what KOS's iso9660 does
// for big reads): a single seek, then the drive streams sectors and we pull
// them by DMA. Under Flycast a plain sector command costs ~25 ms of seek
// emulation plus 1x-drive throughput; the stream does not. Every request is
// whole sectors into a 32-byte-aligned buffer - never the < 32-byte stream
// request that hangs KOS's iso9660 under Flycast.
bool ReadRangeStream(uint32_t lba, uint32_t offset, uint32_t len, uint8_t* dst)
{
    const uint32_t first = offset / 2048;
    const uint32_t last = (offset + len - 1) / 2048;
    const uint32_t nsec = last - first + 1;
    if (cdrom_stream_start((int)(lba + first + 150), (int)nsec, true) != ERR_OK) return false;

    bool ok = true;
    uint32_t sec = first;                    // next sector the stream delivers
    while (ok && len > 0) {
        const uint32_t in = offset % 2048;
        if (in == 0 && len >= 2048 && (((uintptr_t)dst) & 31) == 0) {
            // Whole sectors straight into the destination.
            uint32_t n = len / 2048;
            if (n > 512) n = 512;            // 1 MB per request
            ok = cdrom_stream_request(dst, n * 2048, true) == ERR_OK;
            g_dcIoProgress = g_dcIoProgress + 1;
            dst += n * 2048; offset += n * 2048; len -= n * 2048; sec += n;
            continue;
        }
        // Partial first/last sector, or an unaligned destination: bounce.
        uint32_t n = (in + len + 2047) / 2048;
        if (n > kBounceSectors) n = kBounceSectors;
        if (n > last - sec + 1) n = last - sec + 1;
        ok = cdrom_stream_request(s_bounce, n * 2048, true) == ERR_OK;
        g_dcIoProgress = g_dcIoProgress + 1;
        uint32_t take = n * 2048 - in;
        if (take > len) take = len;
        if (ok) memcpy(dst, s_bounce + in, take);
        dst += take; offset += take; len -= take; sec += n;
    }
    cdrom_stream_stop(false);
    return ok;
}

bool ReadRangeInner(uint32_t lba, uint32_t offset, uint32_t len, uint8_t* dst);

bool ReadRange(uint32_t lba, uint32_t offset, uint32_t len, uint8_t* dst)
{
    g_dcInCd = 1;
    DC_TRACE('C');
    const bool ok = ReadRangeInner(lba, offset, len, dst);
    DC_TRACE('c');
    g_dcInCd = 0;
    return ok;
}

bool ReadRangeInner(uint32_t lba, uint32_t offset, uint32_t len, uint8_t* dst)
{
    if (len == 0) return true;
    static bool s_streamBroken = false;
    if (!s_streamBroken) {
        if (ReadRangeStream(lba, offset, len, dst)) return true;
        printf("[ISO] CD stream read failed - falling back to sector reads\n");
        s_streamBroken = true;
    }
    return ReadRangeSectors(lba, offset, len, dst);
}

// ---------------------------------------------------------------------------
// /cd VFS on top of the index - replaces KOS's fs_iso9660 (which mounts the
// wrong track on a GD-ROM and rescans directories on every open).
// ---------------------------------------------------------------------------

struct CdFile {
    Entry    e;
    uint32_t pos = 0;
    // files: small-read cache (newlib reads through a 1 KB stdio buffer)
    uint8_t* cache = nullptr;           // 32-byte aligned, kCacheBytes
    uint32_t cacheStart = 0, cacheLen = 0;
    // directories
    std::vector<Entry> list;
    size_t   next = 0;
    dirent_t de;
};

const uint32_t kCacheBytes = 16 * 1024;   // 8 sectors of read-ahead per file

void* VfsOpen(vfs_handler_t*, const char* fn, int mode)
{
    mutex_lock_scoped(&s_isoLock);
    if ((mode & O_MODE_MASK) != O_RDONLY) { errno = EROFS; return nullptr; }
    Entry e;
    if (!LookupRel(fn ? fn : "", &e)) { errno = ENOENT; return nullptr; }
    const bool wantDir = (mode & O_DIR) != 0;
    if (wantDir && !e.dir) { errno = ENOTDIR; return nullptr; }
    if (!wantDir && e.dir) { errno = EISDIR; return nullptr; }
    CdFile* f = new CdFile();
    f->e = e;
    if (e.dir) {
        const Dir* d = LoadDir(e.lba, e.size);
        if (d == nullptr) { delete f; errno = EIO; return nullptr; }
        for (const auto& kv : *d) f->list.push_back(kv.second);
    }
    return f;
}

int VfsClose(void* h)
{
    CdFile* f = (CdFile*)h;
    if (f == nullptr) return -1;
    free(f->cache);
    delete f;
    return 0;
}

ssize_t VfsRead(void* h, void* buf, size_t cnt)
{
    mutex_lock_scoped(&s_isoLock);
    CdFile* f = (CdFile*)h;
    if (f == nullptr || f->e.dir) { errno = EBADF; return -1; }
    if (f->pos >= f->e.size || cnt == 0) return 0;
    uint32_t n = f->e.size - f->pos;
    if (cnt < n) n = (uint32_t)cnt;
    uint8_t* out = (uint8_t*)buf;

    if (n >= kCacheBytes) {                           // big read: straight through
        if (!ReadRange(f->e.lba, f->pos, n, out)) { errno = EIO; return -1; }
        f->pos += n;
        return (ssize_t)n;
    }
    uint32_t done = 0;
    while (done < n) {
        if (f->pos < f->cacheStart || f->pos >= f->cacheStart + f->cacheLen) {
            if (f->cache == nullptr) {
                f->cache = (uint8_t*)memalign(32, kCacheBytes);
                if (f->cache == nullptr) { errno = ENOMEM; return -1; }
            }
            const uint32_t start = f->pos & ~2047u;
            uint32_t len = f->e.size - start;
            if (len > kCacheBytes) len = kCacheBytes;
            if (!ReadRange(f->e.lba, start, len, f->cache)) { errno = EIO; return -1; }
            f->cacheStart = start;
            f->cacheLen = len;
        }
        uint32_t take = f->cacheStart + f->cacheLen - f->pos;
        if (take > n - done) take = n - done;
        memcpy(out + done, f->cache + (f->pos - f->cacheStart), take);
        f->pos += take;
        done += take;
    }
    return (ssize_t)n;
}

off_t VfsSeek(void* h, off_t off, int whence)
{
    CdFile* f = (CdFile*)h;
    if (f == nullptr || f->e.dir) { errno = EBADF; return -1; }
    off_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (off_t)f->pos :
                 whence == SEEK_END ? (off_t)f->e.size : -1;
    if (base < 0 || base + off < 0) { errno = EINVAL; return -1; }
    off_t p = base + off;
    if (p > (off_t)f->e.size) p = (off_t)f->e.size;
    f->pos = (uint32_t)p;
    return p;
}

off_t VfsTell(void* h)
{
    CdFile* f = (CdFile*)h;
    if (f == nullptr) { errno = EBADF; return -1; }
    return (off_t)f->pos;
}

size_t VfsTotal(void* h)
{
    CdFile* f = (CdFile*)h;
    if (f == nullptr) { errno = EBADF; return (size_t)-1; }
    return f->e.size;
}

const dirent_t* VfsReaddir(void* h)
{
    CdFile* f = (CdFile*)h;
    if (f == nullptr || !f->e.dir) { errno = EBADF; return nullptr; }
    if (f->next >= f->list.size()) return nullptr;
    const Entry& e = f->list[f->next++];
    memset(&f->de, 0, sizeof(f->de));
    strncpy(f->de.name, e.name.c_str(), sizeof(f->de.name) - 1);
    f->de.size = e.dir ? -1 : (int)e.size;
    f->de.attr = e.dir ? O_DIR : 0;
    return &f->de;
}

int VfsRewinddir(void* h)
{
    CdFile* f = (CdFile*)h;
    if (f == nullptr || !f->e.dir) { errno = EBADF; return -1; }
    f->next = 0;
    return 0;
}

void FillStat(const Entry& e, struct stat* st)
{
    memset(st, 0, sizeof(*st));
    st->st_dev = (dev_t)('c' | ('d' << 8));
    st->st_mode = S_IRUSR | S_IRGRP | S_IROTH | S_IXUSR | S_IXGRP | S_IXOTH;
    st->st_mode |= e.dir ? S_IFDIR : S_IFREG;
    st->st_size = e.dir ? -1 : (off_t)e.size;
    st->st_nlink = e.dir ? 2 : 1;
    st->st_blksize = 2048;
}

int VfsStat(vfs_handler_t*, const char* path, struct stat* st, int)
{
    mutex_lock_scoped(&s_isoLock);
    Entry e;
    if (!LookupRel(path ? path : "", &e)) { errno = ENOENT; return -1; }
    FillStat(e, st);
    return 0;
}

int VfsFstat(void* h, struct stat* st)
{
    CdFile* f = (CdFile*)h;
    if (f == nullptr) { errno = EBADF; return -1; }
    FillStat(f->e, st);
    return 0;
}

int VfsFcntl(void* h, int cmd, va_list)
{
    CdFile* f = (CdFile*)h;
    if (f == nullptr) { errno = EBADF; return -1; }
    switch (cmd) {
    case F_GETFL: return O_RDONLY | (f->e.dir ? O_DIR : 0);
    case F_SETFL: case F_GETFD: case F_SETFD: return 0;
    default: errno = EINVAL; return -1;
    }
}

vfs_handler_t s_vh = {
    { "/cd", 0, 0x00010000, 0, NMMGR_TYPE_VFS, NMMGR_LIST_INIT },
    0, nullptr,
    VfsOpen, VfsClose, VfsRead, nullptr, VfsSeek, VfsTell, VfsTotal, VfsReaddir,
    nullptr, nullptr, nullptr, nullptr, nullptr, VfsStat, nullptr, nullptr, VfsFcntl,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, VfsRewinddir, VfsFstat
};

}  // namespace

// ---------------------------------------------------------------------------
// API (Dreamcast platform layer only)
// ---------------------------------------------------------------------------

// Size of a /cd file, -1 if not on /cd or not found.
extern "C" long dc_iso_size(const char* path)
{
    mutex_lock_scoped(&s_isoLock);
    Entry e;
    return Lookup(path, &e) ? (long)e.size : -1;
}

// LBA + size of a /cd file. Returns 0 on success.
extern "C" int dc_iso_locate(const char* path, uint32_t* lba, uint32_t* size)
{
    mutex_lock_scoped(&s_isoLock);
    Entry e;
    if (!Lookup(path, &e)) return -1;
    *lba = e.lba;
    *size = e.size;
    return 0;
}

// Read part of a located file. Returns 0 on success.
extern "C" int dc_iso_read(uint32_t lba, uint32_t offset, uint32_t len, void* dst)
{
    mutex_lock_scoped(&s_isoLock);
    return ReadRange(lba, offset, len, (uint8_t*)dst) ? 0 : -1;
}

// ---------------------------------------------------------------------------
// Whole-file cache in sound RAM.
//
// The game reads the same files again and again: the effect-sprite TIM four
// times per room, the door model at every door, a room's RDT/PAK/EMD and sound
// banks each time it is re-entered - and every read costs seek time on the
// console. Copies of whole files are kept in the AICA's 2 MB sound RAM, which
// the audio stream barely uses (its firmware and two small buffers). Main RAM
// is not touched at all: a cache there fragmented the game's heap until a
// 1 MB allocation failed.
//
// Transfers use G2 DMA when the buffer allows it (32-byte aligned); the tail
// and unaligned buffers go through the CPU. A DMA that does not finish within
// a second (an emulator without AICA read DMA) switches to the CPU for good.
// ---------------------------------------------------------------------------
namespace {
// Up to 256 KB: backgrounds (.PAK), enemy models, sound effects, doors,
// effect sprites. Room files (.RDT, ~0.5 MB) and music banks would each push
// out several rooms' worth of these - two neighbouring rooms' small files fit
// together, so walking back and forth only reads the RDT from the disc.
const uint32_t kCacheMaxFile = 256 * 1024;
// Files used recently (read from the disc or hit within the last
// kProtectWindow lookups, i.e. a few room visits) are never overwritten: a new file goes around them,
// or is not cached if there is no room. Plain oldest-first replacement keeps
// almost nothing when the player walks between two rooms whose files together
// just exceed the ring (each visit overwrites the files of the room visited
// next - ~10% in the two-room walk test); with this the part that fits stays.
// Once the player moves on, the old hits age out and the space is reused.
const uint32_t kProtectWindow = 32;
const uint32_t kAicaKeep     = 64 * 1024;          // left for the sound side

// One sound-RAM block used as a ring: files are written one after another and
// the oldest are overwritten when it wraps. No allocator, so no fragmentation.
struct CacheEntry {
    CacheEntry* prev;
    CacheEntry* next;                              // newest at the head
    uint32_t lba, size;
    uint32_t off, span;                            // position in the ring
    uint32_t lastHit;                              // lookup number of the last use
};
uint32_t s_lookupSeq = 0;
CacheEntry* s_cacheHead = nullptr;
CacheEntry* s_cacheTail = nullptr;
uint32_t s_ringBase = 0, s_ringSize = 0, s_ringPos = 0;
uint32_t s_cacheBytes = 0, s_cacheFiles = 0, s_cacheBudget = 0;
uint32_t s_cacheHits = 0, s_cacheMisses = 0, s_cacheHitBytes = 0, s_cacheDropped = 0, s_cacheSkipped = 0;
bool s_cacheReady = false, s_cacheDma = true;
mutex_t s_cacheLock = MUTEX_INITIALIZER;
semaphore_t s_dmaDone;

void Unlink(CacheEntry* c)
{
    if (c->prev) c->prev->next = c->next; else s_cacheHead = c->next;
    if (c->next) c->next->prev = c->prev; else s_cacheTail = c->prev;
    c->prev = c->next = nullptr;
}

void PushFront(CacheEntry* c)
{
    c->prev = nullptr;
    c->next = s_cacheHead;
    if (s_cacheHead) s_cacheHead->prev = c; else s_cacheTail = c;
    s_cacheHead = c;
}

void Drop(CacheEntry* c)
{
    Unlink(c);
    s_cacheBytes -= c->size;
    s_cacheFiles--;
    s_cacheDropped++;
    delete c;
}

void DmaCallback(void*) { sem_signal(&s_dmaDone); }

// One G2 DMA between main RAM and sound RAM; both sides 32-byte aligned,
// length a multiple of 32. Main-RAM cache lines handled here.
bool Dma(void* ram, uint32_t aica, uint32_t len, bool toSound)
{
    if (!s_cacheDma) return false;
    if (toSound) dcache_flush_range((uintptr_t)ram, len);
    else         dcache_inval_range((uintptr_t)ram, len);
    if (g2_dma_transfer(ram, (void*)(uintptr_t)(aica | SPU_RAM_BASE), len, 0, DmaCallback, nullptr,
                        toSound ? G2_DMA_TO_G2 : G2_DMA_TO_SH4, 0, G2_DMA_CHAN_SPU, 0) != 0) {
        printf("[ISO] cache: G2 DMA refused - copying with the CPU from now on\n");
        s_cacheDma = false;
        return false;
    }
    if (sem_wait_timed(&s_dmaDone, 1000) != 0) {
        printf("[ISO] cache: G2 DMA did not finish - copying with the CPU from now on\n");
        s_cacheDma = false;                        // the channel stays busy; never touch it again
        return false;
    }
    if (!toSound) dcache_inval_range((uintptr_t)ram, len);
    return true;
}

// Sound RAM <-> main RAM, any alignment, never touching bytes past `len`.
void CopyFromSound(uint8_t* dst, uint32_t aica, uint32_t len)
{
    uint32_t done = 0;
    if ((((uintptr_t)dst) & 31) == 0 && len >= 32) {
        const uint32_t bulk = len & ~31u;
        if (Dma(dst, aica, bulk, false)) done = bulk;
    }
    if ((((uintptr_t)(dst + done)) & 3) == 0) {    // word copies up to the last whole word
        const uint32_t words = (len - done) & ~3u;
        if (words) spu_memread(dst + done, aica + done, words);
        done += words;
    }
    while (done < len) {                           // tail (or an unaligned buffer)
        uint32_t tmp[8];
        uint32_t n = len - done;
        if (n > sizeof(tmp)) n = sizeof(tmp);
        spu_memread(tmp, aica + done, (n + 3) & ~3u);
        memcpy(dst + done, tmp, n);
        done += n;
    }
}

void CopyToSound(uint32_t aica, const uint8_t* src, uint32_t len)
{
    uint32_t done = 0;
    if ((((uintptr_t)src) & 31) == 0 && len >= 32) {
        const uint32_t bulk = len & ~31u;
        if (Dma((void*)src, aica, bulk, true)) done = bulk;
    }
    if ((((uintptr_t)(src + done)) & 3) == 0) {
        const uint32_t words = (len - done) & ~3u;
        if (words) spu_memload(aica + done, src + done, words);
        done += words;
    }
    while (done < len) {
        uint32_t tmp[8] = {};
        uint32_t n = len - done;
        if (n > sizeof(tmp)) n = sizeof(tmp);
        memcpy(tmp, src + done, n);
        spu_memload(aica + done, tmp, (n + 3) & ~3u);
        done += n;
    }
}

// Copies a cached file into dst. Returns false on a miss.
bool CacheGet(uint32_t lba, uint32_t size, void* dst)
{
    if (!s_cacheReady) return false;
    mutex_lock_scoped(&s_cacheLock);
    ++s_lookupSeq;
    for (CacheEntry* c = s_cacheHead; c != nullptr; c = c->next) {
        if (c->lba != lba || c->size != size) continue;
        CopyFromSound((uint8_t*)dst, s_ringBase + c->off, size);
        c->lastHit = s_lookupSeq;
        s_cacheHits++;
        s_cacheHitBytes += size;
        return true;
    }
    s_cacheMisses++;
    return false;
}

// Keeps a copy of a file just read at the ring's write position, overwriting
// whatever (oldest) files occupied that stretch.
void CachePut(uint32_t lba, uint32_t size, const void* src)
{
    if (!s_cacheReady || size == 0 || size > kCacheMaxFile) return;
    const uint32_t span = (size + 31) & ~31u;      // keeps every file 32-byte aligned
    if (span > s_ringSize) return;
    mutex_lock_scoped(&s_cacheLock);
    for (CacheEntry* c = s_cacheHead; c != nullptr; c = c->next)
        if (c->lba == lba && c->size == size) return;
    // Find a stretch of `span` bytes holding no recently useful file, starting
    // at the write position and stepping past protected files; one lap at most.
    auto isProtected = [](const CacheEntry* e) {
        return e->lastHit != 0 && s_lookupSeq - e->lastHit < kProtectWindow;
    };
    uint32_t lo = s_ringPos, walked = 0;
    for (;;) {
        if (lo + span > s_ringSize) { walked += s_ringSize - lo; lo = 0; }
        const CacheEntry* blocker = nullptr;
        for (const CacheEntry* e = s_cacheHead; e != nullptr; e = e->next)
            if (e->off < lo + span && lo < e->off + e->span && isProtected(e)) {
                if (blocker == nullptr || e->off + e->span > blocker->off + blocker->span) blocker = e;
            }
        if (blocker == nullptr) break;
        const uint32_t next = blocker->off + blocker->span;
        walked += next - lo;
        lo = next;
        if (walked >= s_ringSize) { s_cacheSkipped++; return; }   // all of it is in use
    }
    const uint32_t hi = lo + span;
    for (CacheEntry* e = s_cacheHead; e != nullptr; ) {
        CacheEntry* next = e->next;
        if (e->off < hi && lo < e->off + e->span) Drop(e);     // overwritten
        e = next;
    }
    CacheEntry* c = new (std::nothrow) CacheEntry;
    if (c == nullptr) return;
    CopyToSound(s_ringBase + lo, (const uint8_t*)src, size);
    c->lba = lba; c->size = size; c->off = lo; c->span = span;
    c->lastHit = s_lookupSeq ? s_lookupSeq : 1;    // a file just read counts as recently used
    PushFront(c);
    s_ringPos = hi;
    s_cacheBytes += size;
    s_cacheFiles++;
}
}  // namespace

// Called once the audio stream holds its sound-RAM buffers (audio.cpp): the
// rest of the 2 MB, minus a margin, becomes the file cache.
#ifndef RE1_DC_FILECACHE
#define RE1_DC_FILECACHE 1
#endif

extern "C" void dc_iso_cache_init(void)
{
    if (s_cacheReady) return;
    if (!RE1_DC_FILECACHE) {
        printf("[ISO] cache: disabled (RE1_DC_FILECACHE=0)\n");
        return;
    }
    const uint32_t avail = snd_mem_available();
    if (avail <= kAicaKeep + 128 * 1024) {
        printf("[ISO] cache: only %u KB of sound RAM free - disabled\n", (unsigned)(avail / 1024));
        return;
    }
    const uint32_t want = (avail - kAicaKeep) & ~31u;
    const uint32_t base = snd_mem_malloc(want);
    if (base == 0) {
        printf("[ISO] cache: could not reserve %u KB of sound RAM - disabled\n", (unsigned)(want / 1024));
        return;
    }
    sem_init(&s_dmaDone, 0);
    s_ringBase = (base + 31) & ~31u;               // G2 DMA needs 32-byte alignment
    s_ringSize = (want - (s_ringBase - base)) & ~31u;
    s_cacheBudget = want;
    s_cacheReady = true;
    printf("[ISO] cache: %u KB of sound RAM for files up to %u KB\n",
           (unsigned)(want / 1024), (unsigned)(kCacheMaxFile / 1024));
}

// Debug report line (main loop, every few seconds in RE1_DC_VERBOSE builds).
extern "C" void dc_iso_cache_report(void)
{
    if (!s_cacheReady) return;
    mutex_lock_scoped(&s_cacheLock);
    printf("[ISO] cache: %u files / %u KB, %u hits (%u KB not read from disc), %u misses, %u dropped, %u not kept (in use)%s\n",
           (unsigned)s_cacheFiles, (unsigned)(s_cacheBytes / 1024), (unsigned)s_cacheHits,
           (unsigned)(s_cacheHitBytes / 1024), (unsigned)s_cacheMisses, (unsigned)s_cacheDropped,
           (unsigned)s_cacheSkipped,
           s_cacheDma ? "" : " (CPU copies)");
}

// Caller of the game's create_texture_page, for the PVR texture census: the
// texture itself is created later on the scheduler task. File scope, not in an
// unnamed namespace, so the game code can link to it.
extern "C" { void* volatile g_dcPageCaller = nullptr; }

// Name of the last file read whole (texture census: what a texture came from).
static char s_lastLoaded[24] = "";
extern "C" const char* dc_last_loaded_file(void) { return s_lastLoaded; }

// Whole /cd file into dst. Returns its size, or -1 (caller falls back to fopen).
extern "C" long dc_iso_load(const char* path, void* dst)
{
    {
        const char* slash = strrchr(path, '/');
        const char* bs = strrchr(path, '\\');
        const char* base = slash > bs ? slash + 1 : (bs ? bs + 1 : path);
        strncpy(s_lastLoaded, base, sizeof(s_lastLoaded) - 1);
        s_lastLoaded[sizeof(s_lastLoaded) - 1] = '\0';
    }
    Entry e;
    {
        mutex_lock_scoped(&s_isoLock);
        if (!Lookup(path, &e)) return -1;
    }
    if (CacheGet(e.lba, e.size, dst)) return (long)e.size;
    {
        mutex_lock_scoped(&s_isoLock);
        if (!ReadRange(e.lba, 0, e.size, (uint8_t*)dst)) return -1;
    }
    CachePut(e.lba, e.size, dst);
    return (long)e.size;
}

// Replace KOS's fs_iso9660 on /cd with the VFS above. Call once at boot,
// before anything opens a /cd file. Returns 0 on success; on failure KOS's
// own /cd is left in place.
extern "C" int dc_iso_mount(void)
{
    mutex_lock_scoped(&s_isoLock);
    if (!Init()) return -1;
    fs_iso9660_shutdown();
    if (nmmgr_handler_add(&s_vh.nmmgr) != 0) {
        printf("[ISO] could not mount /cd - falling back to KOS fs_iso9660\n");
        fs_iso9660_init();
        return -1;
    }
    return 0;
}

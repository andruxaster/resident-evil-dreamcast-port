// platform.cpp - Dreamcast (KallistiOS) implementation of platform.h.
//
// Counterpart of src/platform/linux/platform.cpp. Differences that matter:
//   - no MMU guard pages: task stacks get canary guards checked every frame
//   - no /proc: the executable "directory" is RE1_DC_ROOT, and readable-range
//     queries answer from the fixed 16 MB main-RAM map
//   - the filesystem is KOS VFS (/cd iso9660, /pc dcload, /ram, /vmu)
#include "../platform.h"
#include "dc_platform.h"
#include "ctxswitch.h"

#include <kos.h>
#include <malloc.h>
#include <unistd.h>
#include <kos/thread.h>
#include <kos/irq.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Keyboard (VK-indexed; fed by main.cpp / input.cpp)
// ---------------------------------------------------------------------------

static unsigned char s_keyDown[256];
static unsigned char s_keyPrev[256];

void plat_key_event(int vk, BOOL down)
{
    if (vk < 0 || vk > 255) return;
    s_keyDown[vk] = down ? 1 : 0;
}

int plat_key_state(int vk)
{
    if (vk < 0 || vk > 255) return 0;
    int down = s_keyDown[vk];
    int edge = (down && !s_keyPrev[vk]) ? 1 : 0;
    s_keyPrev[vk] = (unsigned char)down;
    return (down ? 0x8000 : 0) | edge;
}

void plat_key_flush(void)
{
    for (int vk = 0; vk < 256; vk++) plat_key_state(vk);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

DWORD plat_time_ms(void)
{
    return (DWORD)timer_ms_gettime64();
}

// ---------------------------------------------------------------------------
// Filesystem
// ---------------------------------------------------------------------------

static bool PathExists(const std::string& p)
{
    file_t f = fs_open(p.c_str(), O_RDONLY);
    if (f != FILEHND_INVALID) { fs_close(f); return true; }
    f = fs_open(p.c_str(), O_RDONLY | O_DIR);
    if (f != FILEHND_INVALID) { fs_close(f); return true; }
    return false;
}

// Same algorithm as the Linux layer: '\\' -> '/', then match each existing
// component case-insensitively. iso9660 already compares case-insensitively,
// but /pc (dcload host fs) and /ram do not.
static std::string ResolveCaseInsensitive(const std::string& input)
{
    std::string path = input;
    for (char& c : path) if (c == '\\') c = '/';

    std::string out;
    size_t i = 0;
    if (path.rfind("./", 0) == 0) { out = "./"; i = 2; }
    else if (!path.empty() && path[0] == '/') { out = "/"; i = 1; }

    while (i <= path.size()) {
        size_t slash = path.find('/', i);
        size_t end = (slash == std::string::npos) ? path.size() : slash;
        std::string comp = path.substr(i, end - i);

        if (!comp.empty() && comp != "..") {
            std::string candidate = out + comp;
            // Never probe the mount points themselves ("/cd", "/pc", ...).
            if (out != "/" && !PathExists(candidate)) {
                DIR* d = opendir(out.empty() ? "." : out.c_str());
                if (d != NULL) {
                    struct dirent* e;
                    while ((e = readdir(d)) != NULL) {
                        if (strcasecmp(e->d_name, comp.c_str()) == 0) {
                            candidate = out + e->d_name;
                            break;
                        }
                    }
                    closedir(d);
                }
            }
            out = candidate;
        } else if (!comp.empty()) {
            out += comp;
        }

        if (slash == std::string::npos) break;
        out += '/';
        i = slash + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------
// /cd directory cache.
//
// Every lookup on the GD-ROM is a scan of the directory's sectors, and KOS
// reads directory sectors one command at a time (~17 ms each under Flycast).
// The voice loader probes .ogg then .wav before every line, each probe was a
// normalize + fopen - several full scans of a large directory per line.
// /cd is read-only, so each directory is listed once and kept: case-
// insensitive resolution and existence checks then never touch the disc.
// ---------------------------------------------------------------------------

namespace {

struct CdDir {
    std::unordered_map<std::string, std::string> names;   // lower -> actual
};

std::unordered_map<std::string, CdDir*> s_cdDirs;          // actual dir path -> listing

std::string Lower(const std::string& in)
{
    std::string out(in);
    for (char& c : out) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return out;
}

const CdDir* ListCdDir(const std::string& dir)
{
    auto it = s_cdDirs.find(dir);
    if (it != s_cdDirs.end()) return it->second;

    CdDir* d = nullptr;
    const uint64_t t0 = timer_ms_gettime64();
    DIR* h = opendir(dir.c_str());
    if (h != nullptr) {
        d = new CdDir();
        struct dirent* e;
        while ((e = readdir(h)) != nullptr) {
            std::string n(e->d_name);
            if (n == "." || n == "..") continue;
            d->names.emplace(Lower(n), n);
        }
        closedir(h);
        printf("[FS] cached %s: %u entries in %u ms\n", dir.c_str(),
               (unsigned)d->names.size(), (unsigned)(timer_ms_gettime64() - t0));
    }
    s_cdDirs.emplace(dir, d);    // a failed listing is cached too (as NULL)
    return d;
}

// Resolve a /cd path component by component through the listings. Returns
// the path with the on-disc spelling; *exists says whether every component
// was found. Components after the first missing one are kept as given.
std::string ResolveCd(const std::string& path, bool* exists)
{
    std::string out = "/cd";
    bool found = true;
    size_t i = 3;                                  // past "/cd"
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') ++i;
        if (i >= path.size()) break;
        size_t end = path.find('/', i);
        if (end == std::string::npos) end = path.size();
        std::string comp = path.substr(i, end - i);
        i = end;
        if (comp == ".") continue;
        if (comp == "..") {
            size_t cut = out.rfind('/');
            if (cut > 2) out.erase(cut);           // never above /cd
            continue;
        }
        if (found) {
            const CdDir* d = ListCdDir(out);
            if (d != nullptr) {
                auto it = d->names.find(Lower(comp));
                if (it != d->names.end()) {
                    out += "/" + it->second;
                    continue;
                }
            }
            found = false;
        }
        out += "/" + comp;
    }
    if (exists) *exists = found;
    return out;
}

bool IsCdPath(const std::string& p)
{
    return p.size() >= 3 && p.compare(0, 3, "/cd") == 0 && (p.size() == 3 || p[3] == '/');
}

std::string Slashes(const char* path)
{
    std::string p(path);
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

}  // namespace

// Existence check without touching the disc for /cd paths (used by the
// shared AudioFile probes via dc_fast_probes.py).
extern "C" int dc_path_exists(const char* path)
{
    if (path == nullptr) return 0;
    std::string p = Slashes(path);
    if (IsCdPath(p)) {
        bool exists = false;
        ResolveCd(p, &exists);
        return exists ? 1 : 0;
    }
    return PathExists(p) ? 1 : 0;
}

const char* plat_normalize_path(const char* path, char* buffer, size_t size)
{
    if (path == NULL || buffer == NULL || size == 0) return path;

    static std::unordered_map<std::string, std::string> s_cache;
    auto it = s_cache.find(path);
    if (it == s_cache.end()) {
        std::string p = Slashes(path);
        std::string r = IsCdPath(p) ? ResolveCd(p, nullptr) : ResolveCaseInsensitive(p);
        it = s_cache.emplace(path, r).first;
    }
    strncpy(buffer, it->second.c_str(), size - 1);
    buffer[size - 1] = '\0';
    return buffer;
}

BOOL plat_exe_dir(char* out, size_t size)
{
    if (out == NULL || size == 0) return FALSE;
    if (strlen(RE1_DC_ROOT) >= size) { out[0] = '\0'; return FALSE; }
    strcpy(out, RE1_DC_ROOT);
    return TRUE;
}

BOOL plat_path_is_absolute(const char* path)
{
    return (path != NULL && path[0] == '/') ? TRUE : FALSE;
}

BOOL plat_mkdir(const char* path)
{
    char norm[1024];
    const char* p = plat_normalize_path(path, norm, sizeof(norm));
    if (fs_mkdir(p) == 0) return TRUE;
    file_t f = fs_open(p, O_RDONLY | O_DIR);
    if (f != FILEHND_INVALID) { fs_close(f); return TRUE; }
    return FALSE;
}

BOOL plat_file_write(const char* path, const void* data, size_t size)
{
    char norm[1024];
    FILE* f = fopen(plat_normalize_path(path, norm, sizeof(norm)), "wb");
    if (f == NULL) return FALSE;
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    return (written == size) ? TRUE : FALSE;
}

// Whole-file read that only ever drives KOS's iso9660 driver down its two
// healthy paths. `fp` must be unbuffered and positioned at 0.
//
// iso_read streams by DMA when the destination is 32-byte aligned, but once a
// file is streaming, a final read of < 32 bytes takes the "stream tail"
// branch (cdrom_stream_request of 32 bytes + thd_poll) - which never returns
// under Flycast; that was the room-entry hang (WAV sizes are arbitrary, TIM/
// EMD/RDT sizes happen to be multiples of 32). So: read the < 32-byte tail
// FIRST, while the file is not streaming (plain cached sector read), then
// the 32-byte-multiple body in one aligned streamed read. The stream is left
// with the tail unread and iso_close aborts it.
extern "C" size_t dc_fread_whole(FILE* fp, void* dst, size_t size)
{
    const size_t tail = size & 31;
    const size_t body = size - tail;
    unsigned char tailBuf[32];

    if (tail != 0) {
        if (fseek(fp, (long)body, SEEK_SET) != 0) return 0;
        if (fread(tailBuf, 1, tail, fp) != tail) return 0;
        if (fseek(fp, 0, SEEK_SET) != 0) return 0;
    }
    if (body != 0) {
        void* target = dst;
        void* bounce = nullptr;
        if ((((uintptr_t)dst) & 31) != 0) {
            bounce = memalign(32, body);
            if (bounce != nullptr) target = bounce;
        }
        const size_t got = fread(target, 1, body, fp);
        if (bounce != nullptr) {
            memcpy(dst, bounce, got);
            free(bounce);
        }
        if (got != body) return got;
    }
    if (tail != 0) memcpy((unsigned char*)dst + body, tailBuf, tail);
    return size;
}

extern "C" long dc_iso_size(const char* path);
extern "C" long dc_iso_load(const char* path, void* dst);

void* plat_file_read_all(const char* path, size_t* outSize)
{
    // /cd files come from the direct ISO reader (dc_iso.cpp): no iso9660
    // directory scan per open - that was ~300 ms per sound bank.
    {
        char cdNorm[1024];
        const char* cp = plat_normalize_path(path, cdNorm, sizeof(cdNorm));
        const long sz = dc_iso_size(cp);
        if (sz >= 0) {
            void* buf = memalign(32, (size_t)((sz & ~3L) + 0x10));
            if (buf == NULL) return NULL;
            if (dc_iso_load(cp, buf) == sz) {
                if (outSize != NULL) *outSize = (size_t)sz;
                return buf;
            }
            free(buf);
            return NULL;
        }
    }
    char norm[1024];
    FILE* f = fopen(plat_normalize_path(path, norm, sizeof(norm)), "rb");
    if (f == NULL) return NULL;
    // Unbuffered, so the fread below lands straight in our 32-byte-aligned
    // buffer: KOS's iso9660 then streams the file by DMA. Through newlib's own
    // (unaligned) stdio buffer it was read one sector per command - ~8 ms/KB
    // under Flycast, several seconds per sound bank.
    setvbuf(f, NULL, _IONBF, 0);

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long fileSize = ftell(f);
    if (fileSize < 0) { fclose(f); return NULL; }
    rewind(f);

    // Same rounding as the original loader. 32-byte alignment keeps the
    // buffer friendly to the SH-4 cache and to any 32-bit field reads.
    size_t alignedSize = (size_t)((fileSize & ~3L) + 0x10);
    void* buffer = memalign(32, alignedSize);
    if (buffer == NULL) { fclose(f); return NULL; }

    size_t got = dc_fread_whole(f, buffer, (size_t)fileSize);
    fclose(f);
    if (got != (size_t)fileSize) { free(buffer); return NULL; }

    if (outSize != NULL) *outSize = (size_t)fileSize;
    return buffer;
}

size_t plat_readable_bytes(const void* p)
{
    // Main RAM: 16 MB at 0x0C000000, seen through P1 (cached, 0x8C...) and
    // P2 (uncached, 0xAC...). Everything the game allocates lives there.
    uintptr_t a = (uintptr_t)p;
    const uintptr_t kRamSize = 16u * 1024u * 1024u;
    const uintptr_t bases[] = { 0x8C000000u, 0xAC000000u, 0x0C000000u };
    for (uintptr_t b : bases) {
        if (a >= b && a < b + kRamSize) return (size_t)(b + kRamSize - a);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void plat_debug_output(const char* s)
{
    if (s != NULL) fputs(s, stdout);   // dcload / serial console
}

BOOL plat_is_debugger_present(void)
{
    // Gates dbg_printf. Printing is always safe here (no fail-fast like
    // OutputDebugString), but serial output is slow: build with
    // RE1_DC_VERBOSE=0 for timing runs.
#if defined(RE1_DC_VERBOSE) && RE1_DC_VERBOSE == 0
    return FALSE;
#else
    return TRUE;
#endif
}

DWORD plat_env_get(const char* name, char* buffer, DWORD size)
{
    (void)name;
    if (buffer != NULL && size > 0) buffer[0] = '\0';
    return 0;
}

// ---------------------------------------------------------------------------
// Audio device volume - no system mixer to save/restore.
// ---------------------------------------------------------------------------

void plat_audio_probe_and_cache_volume(void) {}
void plat_audio_restore_volume(void) {}

// ---------------------------------------------------------------------------
// Task stacks
//
// Layout [G][S][G][S]...[G] as on the other platforms, but the guards are
// canary-filled RAM instead of unmapped pages: dc_check_stack_guards() runs
// once per frame from main.cpp. Stacks are pre-painted so the high-water mark
// can be measured - that number decides how far TASK_STACK_SIZE can shrink.
// ---------------------------------------------------------------------------

static const uint32_t kGuardMagic = 0xDEADBEEFu;
static const uint32_t kPaintMagic = 0xA5A5A5A5u;

static unsigned char* s_stackMem   = NULL;
static int            s_stackCount = 0;
static size_t         s_stackSize  = 0;
static size_t         s_guardSize  = 0;

static unsigned char* GuardPtr(int i)   // guard i, 0..count
{
    return s_stackMem + (size_t)i * (s_stackSize + s_guardSize);
}

void* plat_alloc_guarded_stacks(int count, size_t stackSize, size_t guardSize)
{
    size_t total = (size_t)count * (stackSize + guardSize) + guardSize;
    unsigned char* mem = (unsigned char*)memalign(32, total);
    if (mem == NULL) return NULL;

    s_stackMem = mem; s_stackCount = count;
    s_stackSize = stackSize; s_guardSize = guardSize;

    uint32_t* w = (uint32_t*)mem;
    for (size_t k = 0; k < total / 4; ++k) w[k] = kPaintMagic;
    for (int i = 0; i <= count; ++i) {
        uint32_t* g = (uint32_t*)GuardPtr(i);
        for (size_t k = 0; k < guardSize / 4; ++k) g[k] = kGuardMagic;
    }
    printf("[DC] task stacks: %d x %u KB at %p\n", count,
           (unsigned)(stackSize / 1024), (void*)mem);
    return mem + guardSize;
}

// ---------------------------------------------------------------------------
// Stack bounds for the KOS scheduler (see ctxswitch.h)
// ---------------------------------------------------------------------------

static void*  s_kernelStack     = NULL;
static size_t s_kernelStackSize = 0;
static bool   s_kernelStackSaved = false;

extern "C" uint32_t re1_ctx_begin_switch(void* stackBase, uint32_t stackSize)
{
    irq_mask_t irq = irq_disable();
    kthread_t* t = thd_get_current();
    if (!s_kernelStackSaved) {
        s_kernelStack      = t->stack;
        s_kernelStackSize  = t->stack_size;
        s_kernelStackSaved = true;
    }
    if (stackBase != NULL) {
        t->stack      = stackBase;
        t->stack_size = stackSize;
    } else {
        t->stack      = s_kernelStack;
        t->stack_size = s_kernelStackSize;
    }
    return (uint32_t)irq;
}

extern "C" void re1_ctx_end_switch(uint32_t irqState)
{
    irq_restore((irq_mask_t)irqState);
}

BOOL dc_check_stack_guards(void)
{
    if (s_stackMem == NULL) return TRUE;
    for (int i = 0; i <= s_stackCount; ++i) {
        const uint32_t* g = (const uint32_t*)GuardPtr(i);
        for (size_t k = 0; k < s_guardSize / 4; ++k) {
            if (g[k] != kGuardMagic) {
                // Stacks grow down, and guard i borders the BOTTOM of stack
                // i, so a smashed guard i means task i overflowed. The last
                // guard sits above the top stack and only catches a write
                // past the top of task count-1.
                printf("[DC] STACK GUARD %d SMASHED (task %d) at +0x%x\n",
                       i, i < s_stackCount ? i : s_stackCount - 1, (unsigned)(k * 4));
                return FALSE;
            }
        }
    }
    return TRUE;
}

void dc_report_stack_usage(void)
{
    if (s_stackMem == NULL) return;
    for (int i = 0; i < s_stackCount; ++i) {
        const uint32_t* s = (const uint32_t*)(GuardPtr(i) + s_guardSize);
        size_t untouched = 0;
        while (untouched < s_stackSize / 4 && s[untouched] == kPaintMagic) ++untouched;
        printf("[DC] task %d stack high-water: %u / %u bytes\n", i,
               (unsigned)(s_stackSize - untouched * 4), (unsigned)s_stackSize);
    }
}

void dc_report_memory(const char* tag)
{
    // mallinfo only describes the arena claimed so far; the real headroom is
    // the gap between the heap top (sbrk) and the end of the 16 MB, minus the
    // kernel thread's stack that sits at the very top.
    struct mallinfo mi = mallinfo();
    uintptr_t heapTop = (uintptr_t)sbrk(0);
    uintptr_t ramEnd  = 0x8C000000u + 16u * 1024u * 1024u;
    unsigned unclaimed = heapTop < ramEnd ? (unsigned)((ramEnd - heapTop) / 1024) : 0;
    printf("[DC] mem %s: heap used %u KB, free in arena %u KB, unclaimed above heap %u KB "
           "(incl. kernel stack)\n",
           tag ? tag : "", (unsigned)(mi.uordblks / 1024), (unsigned)(mi.fordblks / 1024),
           unclaimed);
}

void plat_fatal(const char* message)
{
    printf("RESIDENT EVIL: FATAL: %s\n", message ? message : "(fatal)");
    fflush(stdout);
    exit(1);
}

// ---------------------------------------------------------------------------
// Window / cursor - there is no window.
// ---------------------------------------------------------------------------

void plat_window_destroy(HWND window) { (void)window; }
void plat_cursor_show(BOOL show)      { (void)show; }

// BG_SMOOTH_FIX: set by TexturePage_SetupFull around a room-mask load; the
// PVR backend draws textures created meanwhile filtered (both renderers link).
extern "C" { volatile int g_dcSmoothNext = 0; }

// ---------------------------------------------------------------------------
// MERGE140: platform API added in upstream 1.4.0
// ---------------------------------------------------------------------------

// Only used to index the HD texture folder (system/HdTextures.cpp). HD
// textures do not fit the Dreamcast's 16 MB / 8 MB VRAM, so there is never a
// folder: the HD layer stays inactive and costs nothing.
BOOL plat_dir_list(const char* path, PlatDirVisit visit, void* user)
{
    (void)path; (void)visit; (void)user;
    return FALSE;
}

// Saves written since 1.4.0 carry their own timestamp; this only dates older
// ones. /ram files are recreated at every boot, so their time says nothing.
unsigned int plat_file_mtime(const char* path)
{
    (void)path;
    return 0;
}

// The DISPLAY page of the title OPTIONS: the Dreamcast has one mode, 640x480.
void plat_display_request_apply(void) {}

BOOL plat_display_desktop_size(int* width, int* height)
{
    if (width)  *width  = 640;
    if (height) *height = 480;
    return TRUE;
}

// OPTIONS > EXIT GAME: back to whatever started the game (DreamShell, the
// BIOS menu for a disc).
void plat_request_quit(void)
{
    printf("[PLAT] exit requested from the options menu\n");
    arch_exit();
}

// OPTIONS > GAME > APPLY AND RESTART. There is no process to relaunch: the
// console is reset, which boots the disc again. The menu waits for this
// forever once it has asked, so it must not be a no-op.
void plat_request_restart(void)
{
    printf("[PLAT] restart requested from the options menu\n");
    arch_reboot();
}

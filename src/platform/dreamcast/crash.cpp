// crash.cpp - crash and stall diagnostics that work on a real console.
//
// A release build has no log (dbgio is off), and most people have no serial
// cable, so a crash, a failed KOS assert or a hang just freezes the picture.
// Here every one of them ends on a full-screen report drawn with the BIOS font
// straight into video RAM - photograph it, then resolve the addresses:
//
//   sh-elf-addr2line -f -C -e build/dc/residentevil.elf <PC> <PR> <stack...>
//
// (with the ELF of exactly that build). Covered:
//   - CPU exceptions: address errors (unaligned access - an emulator usually
//     lets these through, the SH-4 does not), illegal instructions, FPU
//   - KOS assert() failures
//   - stalls: audio.cpp's watchdog calls dc_crash_stall() when the main loop
//     has not advanced for 8 s; the report says whether the game was waiting
//     for the PowerVR or reading the disc at the time
#include "../platform.h"
#include "dc_platform.h"

#include <kos.h>
#include <kos/irq.h>
#include <kos/thread.h>
#include <arch/irq.h>
#include <dc/minifont.h>
#include <dc/video.h>
#include <assert.h>

#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern "C" {
volatile int g_dcWaitPvr = 0;    // set around pvr_wait_ready (MarniDX_PVR.cpp)
volatile int g_dcInCd = 0;       // set around GD-ROM reads (dc_iso.cpp)
extern volatile uint32_t g_dcHeartbeat;
}

namespace {

char s_lastMark[64] = "(none)";
volatile bool s_reported = false;

// Lines of 12x24 BIOS font at 640x480.
int s_row = 0;
void Line(const char* fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // minifont (8x16, 80 columns, 30 rows) writes straight into the frame
    // buffer with no locking: the BIOS font takes a mutex, which an exception
    // handler cannot - that left an earlier crash report a blank blue screen.
    // With the function ring (FTRACE) the report is longer: skip blank lines.
    if (RE1_DC_FTRACE && buf[0] == '\0') return;
    const int y = (RE1_DC_FTRACE ? 8 : 104) + s_row * 16;
    if (y + 16 > 480) return;
    minifont_draw_str(vram_s + y * 640 + 16, 640, buf);
    s_row++;
}

void BeginScreen(const char* title)
{
    // Show framebuffer 0 and clear it; the PVR is dead or will be ignored.
    vid_set_start(0);
    vid_clear(0, 0, 96);
    s_row = 0;
    Line("RESIDENT EVIL DC - %s", title);
    Line("");
}

void ContextLines(const irq_context_t* c)
{
    Line("PC %08lx  PR %08lx", (unsigned long)c->pc, (unsigned long)c->pr);
    Line("SP %08lx  SR %08lx", (unsigned long)c->r[15], (unsigned long)c->sr);
    // Return-address-looking words near the stack top (code below 0x8c200000).
    const uint32_t* sp = (const uint32_t*)c->r[15];
    char buf[96]; int n = 0, shown = 0;
    if (((uintptr_t)sp & 3) == 0 && (uintptr_t)sp >= 0x8c000000u && (uintptr_t)sp < 0x8d000000u) {
        for (int i = 0; i < 64 && shown < 10; ++i) {
            const uint32_t v = sp[i];
            if (v >= 0x8c010000u && v < 0x8c200000u) {
                n += snprintf(buf + n, sizeof(buf) - n, "%08lx ", (unsigned long)v);
                if (++shown % 5 == 0) { Line("stack %s", buf); n = 0; }
            }
        }
        if (n > 0) Line("stack %s", buf);
    }
}

void RingLines();     // function-entry ring, below

void StateLines()
{
    Line("");
    Line("last step: %s", s_lastMark);
    Line("waiting for PVR: %s   reading disc: %s",
         g_dcWaitPvr ? "YES" : "no", g_dcInCd ? "YES" : "no");
    Line("main loop passes: %lu", (unsigned long)g_dcHeartbeat);
    struct mallinfo mi = mallinfo();
    Line("heap used %u KB", (unsigned)(mi.uordblks / 1024));
    Line("");
    if (!RE1_DC_FTRACE) Line("addr2line -f -C -e residentevil.elf <PC> <PR> <stack>");
    RingLines();
}

// ---------------------------------------------------------------------------
// Unaligned-access emulation.
//
// The game code (a decompiled x86 executable) reads and writes 16/32-bit
// values at odd addresses - event scripts are byte streams of variable-length
// commands, flag banks are addressed as dwords at any offset. x86 does not
// care and neither does Flycast; the SH-4 raises an address error. Like the
// Linux SH kernel, decode the faulting mov.w/mov.l/fmov.s, do the access a
// byte at a time, update the registers and resume - including the case where
// the access sits in a branch delay slot. Only RAM addresses are emulated: a
// wild address is reported instead (touching it could lock up the bus).
// ---------------------------------------------------------------------------

struct UaSite { uint32_t pc; uint32_t count; };
UaSite s_uaSites[48];
int s_uaSiteCount = 0;
volatile uint32_t s_uaTotal = 0;
uint32_t s_uaBadAddr = 0;

#ifndef DC_UA_TEST_INRAM
inline bool InRam(uint32_t a, uint32_t len)
{
    const uint32_t phys = a & 0x1FFFFFFFu;
    return phys >= 0x0C000000u && phys + len <= 0x0D000000u;   // 16 MB main RAM
}
#else
inline bool InRam(uint32_t a, uint32_t len) { return DC_UA_TEST_INRAM(a, len); }
#endif

inline uint32_t LoadBytes(uint32_t a, int len)
{
    const volatile uint8_t* p = (const volatile uint8_t*)a;
    uint32_t v = 0;
    for (int i = 0; i < len; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

inline void StoreBytes(uint32_t a, uint32_t v, int len)
{
    volatile uint8_t* p = (volatile uint8_t*)a;
    for (int i = 0; i < len; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

// 0 = emulated, -1 = not a load/store we know, -2 = address outside RAM.
int EmulateAccess(irq_context_t* c, uint16_t op)
{
    uint32_t* R = c->r;
    const int n = (op >> 8) & 15, m = (op >> 4) & 15;
    uint32_t addr = 0;
    int len = 0;
    enum { LOAD, STORE, FLOAD, FSTORE } kind = LOAD;
    int reg = 0;              // destination (load) / source (store) register
    int incReg = -1, inc = 0; // post-increment / pre-decrement
    const bool fpuSingle = !(c->fpscr & 0x00100000u) && !(c->fpscr & 0x00200000u);  // SZ=0, FR=0

    switch (op & 0xF000) {
    case 0x2000:                                       // mov.x Rm,@Rn / Rm,@-Rn
        switch (op & 0xF) {
        case 0x1: len = 2; addr = R[n]; break;
        case 0x2: len = 4; addr = R[n]; break;
        case 0x5: len = 2; addr = R[n] - 2; incReg = n; inc = -2; break;
        case 0x6: len = 4; addr = R[n] - 4; incReg = n; inc = -4; break;
        default: return -1;
        }
        kind = STORE; reg = m;
        break;
    case 0x6000:                                       // mov.x @Rm,Rn / @Rm+,Rn
        switch (op & 0xF) {
        case 0x1: len = 2; break;
        case 0x2: len = 4; break;
        case 0x5: len = 2; if (n != m) { incReg = m; inc = 2; } break;
        case 0x6: len = 4; if (n != m) { incReg = m; inc = 4; } break;
        default: return -1;
        }
        addr = R[m]; kind = LOAD; reg = n;
        break;
    case 0x0000:                                       // R0-indexed forms
        switch (op & 0xF) {
        case 0x5: len = 2; addr = R[0] + R[n]; kind = STORE; reg = m; break;
        case 0x6: len = 4; addr = R[0] + R[n]; kind = STORE; reg = m; break;
        case 0xD: len = 2; addr = R[0] + R[m]; kind = LOAD; reg = n; break;
        case 0xE: len = 4; addr = R[0] + R[m]; kind = LOAD; reg = n; break;
        default: return -1;
        }
        break;
    case 0x1000:                                       // mov.l Rm,@(disp,Rn)
        len = 4; addr = R[n] + (op & 15) * 4; kind = STORE; reg = m;
        break;
    case 0x5000:                                       // mov.l @(disp,Rm),Rn
        len = 4; addr = R[m] + (op & 15) * 4; kind = LOAD; reg = n;
        break;
    case 0x8000:
        if ((op & 0x0F00) == 0x0100) {                 // mov.w R0,@(disp,Rn)
            len = 2; addr = R[(op >> 4) & 15] + (op & 15) * 2; kind = STORE; reg = 0;
        } else if ((op & 0x0F00) == 0x0500) {          // mov.w @(disp,Rm),R0
            len = 2; addr = R[(op >> 4) & 15] + (op & 15) * 2; kind = LOAD; reg = 0;
        } else {
            return -1;
        }
        break;
    case 0xF000:                                       // fmov.s (single, bank 0)
        if (!fpuSingle) return -1;
        len = 4;
        switch (op & 0xF) {
        case 0x8: addr = R[m]; kind = FLOAD; reg = n; break;
        case 0x9: addr = R[m]; kind = FLOAD; reg = n; incReg = m; inc = 4; break;
        case 0x6: addr = R[0] + R[m]; kind = FLOAD; reg = n; break;
        case 0xA: addr = R[n]; kind = FSTORE; reg = m; break;
        case 0xB: addr = R[n] - 4; kind = FSTORE; reg = m; incReg = n; inc = -4; break;
        case 0x7: addr = R[0] + R[n]; kind = FSTORE; reg = m; break;
        default: return -1;
        }
        break;
    default:
        return -1;
    }

    if (!InRam(addr, (uint32_t)len)) { s_uaBadAddr = addr; return -2; }

    switch (kind) {
    case LOAD: {
        uint32_t v = LoadBytes(addr, len);
        if (len == 2) v = (uint32_t)(int32_t)(int16_t)v;   // mov.w sign-extends
        R[reg] = v;
        break;
    }
    case STORE:  StoreBytes(addr, R[reg], len); break;
    case FLOAD:  c->fr[reg] = LoadBytes(addr, 4); break;
    case FSTORE: StoreBytes(addr, c->fr[reg], 4); break;
    }
    if (incReg >= 0) R[incReg] += (uint32_t)inc;
    return 0;
}

// Delayed branch at `pc`? Fills the target/PR the branch would produce.
bool DelayedBranch(irq_context_t* c, uint32_t pc, uint16_t op, uint32_t* target, bool* setPr)
{
    const uint32_t* R = c->r;
    const bool T = (c->sr & 1) != 0;
    *setPr = false;
    if ((op & 0xF000) == 0xA000 || (op & 0xF000) == 0xB000) {          // bra / bsr
        int32_t d = op & 0x0FFF; if (d & 0x800) d -= 0x1000;
        *target = pc + 4 + (uint32_t)(d * 2);
        *setPr = (op & 0xF000) == 0xB000;
        return true;
    }
    if ((op & 0xFF00) == 0x8D00 || (op & 0xFF00) == 0x8F00) {          // bt/s, bf/s
        int32_t d = (int8_t)(op & 0xFF);
        const bool taken = ((op & 0xFF00) == 0x8D00) ? T : !T;
        *target = taken ? pc + 4 + (uint32_t)(d * 2) : pc + 4;
        return true;
    }
    const int rm = (op >> 8) & 15;
    switch (op & 0xF0FF) {
    case 0x0023: *target = pc + 4 + R[rm]; return true;                 // braf
    case 0x0003: *target = pc + 4 + R[rm]; *setPr = true; return true;  // bsrf
    case 0x402B: *target = R[rm]; return true;                          // jmp
    case 0x400B: *target = R[rm]; *setPr = true; return true;           // jsr
    }
    if (op == 0x000B) { *target = c->pr; return true; }                 // rts
    return false;
}

// true = handled, resume at the updated context.
bool EmulateUnaligned(irq_context_t* c)
{
    const uint32_t pc = c->pc;
    if (pc & 1) return false;                          // misaligned instruction fetch
    // Inside a soft-gUSA atomic sequence KOS has already rewound pc to the
    // sequence start (r15 holds the negated length): not the faulting insn.
    if ((int32_t)c->r[15] < 0 && (int32_t)c->r[15] >= -128) return false;
    const uint16_t op = *(const uint16_t*)pc;
    uint32_t target = 0;
    bool setPr = false;
    int r;
    if (DelayedBranch(c, pc, op, &target, &setPr)) {
        // SPC points at the branch; the faulting access is its delay slot.
        // The branch reads its registers before the slot executes.
        r = EmulateAccess(c, *(const uint16_t*)(pc + 2));
        if (r != 0) return false;
        if (setPr) c->pr = pc + 4;
        c->pc = target;
    } else {
        r = EmulateAccess(c, op);
        if (r != 0) return false;
        c->pc = pc + 2;
    }
    s_uaTotal = s_uaTotal + 1;
    int i = 0;
    for (; i < s_uaSiteCount; ++i) if (s_uaSites[i].pc == pc) { s_uaSites[i].count++; break; }
    if (i == s_uaSiteCount && s_uaSiteCount < 48) { s_uaSites[i].pc = pc; s_uaSites[i].count = 1; s_uaSiteCount++; }
    return true;
}

void ExceptionHandler(irq_t code, irq_context_t* ctx, void*)
{
    if ((code == EXC_DATA_ADDRESS_READ || code == EXC_DATA_ADDRESS_WRITE) && !s_reported &&
        EmulateUnaligned(ctx))
        return;
    if (s_reported) for (;;) {}
    s_reported = true;
    const char* what =
        code == EXC_DATA_ADDRESS_READ  ? "ADDRESS ERROR (read / unaligned?)" :
        code == EXC_DATA_ADDRESS_WRITE ? "ADDRESS ERROR (write / unaligned?)" :
        code == EXC_ILLEGAL_INSTR      ? "ILLEGAL INSTRUCTION" :
        code == EXC_SLOT_ILLEGAL_INSTR ? "ILLEGAL SLOT INSTRUCTION" :
        code == EXC_GENERAL_FPU        ? "FPU EXCEPTION" :
        code == EXC_SLOT_FPU           ? "FPU SLOT EXCEPTION" : "EXCEPTION";
    BeginScreen("CRASH");
    Line("%s  (code %03x)", what, (unsigned)code);
    if (s_uaBadAddr != 0) Line("bad data address %08lx (outside RAM)", (unsigned long)s_uaBadAddr);
    ContextLines(ctx);
    StateLines();
    for (;;) {}
}

void AssertHandler(const char* file, int line, const char* expr, const char* msg, const char* func)
{
    if (s_reported) for (;;) {}
    s_reported = true;
    irq_disable();
    BeginScreen("KOS ASSERT");
    const char* f = strrchr(file ? file : "", '/');
    Line("%s:%d", f ? f + 1 : (file ? file : "?"), line);
    Line("%.60s", func ? func : "");
    Line("%.60s", expr ? expr : "");
    if (msg) Line("%.60s", msg);
    StateLines();
    for (;;) {}
}

}  // namespace

// ---------------------------------------------------------------------------
// Function-entry trace (RE1_DC_FTRACE=1 builds the game code with
// -finstrument-functions). Every game function entered on the main thread
// stores its address in a RAM ring - one store, no serial I/O (printing each
// entry choked the 115200-baud port and stalled the game by itself). The ring
// is shown when something goes wrong: the stall report (screen + serial),
// the crash screen. Resolve with sh-elf-addr2line -f -C -e <elf> <addrs>.
// ---------------------------------------------------------------------------
extern "C" {
volatile int g_dcFtraceArmed = 1;          // L+R still toggles it (main.cpp)
void __cyg_profile_func_enter(void* fn, void* site) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void* fn, void* site) __attribute__((no_instrument_function));
}
static kthread_t* s_ftraceThread = nullptr;
static const unsigned kRing = 256;          // power of two
static uint32_t s_ring[kRing];
static volatile unsigned s_ringPos = 0;

void __cyg_profile_func_enter(void* fn, void*)
{
    if (!g_dcFtraceArmed || thd_get_current() != s_ftraceThread) return;
    s_ring[s_ringPos & (kRing - 1)] = (uint32_t)(uintptr_t)fn;
    s_ringPos = s_ringPos + 1;
}
void __cyg_profile_func_exit(void*, void*) {}

namespace {
// Last n ring entries, newest last. Distinct consecutive values only, so a
// tight loop does not push everything else out of the report.
int RingTail(uint32_t* out, int n)
{
    const unsigned end = s_ringPos;
    int k = 0;
    uint32_t prev = 0;
    for (unsigned i = (end > kRing ? end - kRing : 0); i < end; ++i) {
        const uint32_t v = s_ring[i & (kRing - 1)];
        if (v == prev) continue;
        prev = v;
        out[k % n] = v;
        ++k;
    }
    // rotate so the oldest of the last n comes first
    if (k <= n) return k;
    uint32_t tmp[64];
    for (int i = 0; i < n; ++i) tmp[i] = out[(k + i) % n];
    memcpy(out, tmp, (size_t)n * 4);
    return n;
}

void RingLines()
{
    if (!RE1_DC_FTRACE) return;
    uint32_t a[25];
    const int n = RingTail(a, 25);
    Line("last game functions (oldest first):");
    char buf[96]; int len = 0;
    for (int i = 0; i < n; ++i) {
        len += snprintf(buf + len, sizeof(buf) - len, "%08lx ", (unsigned long)a[i]);
        if ((i + 1) % 5 == 0 || i == n - 1) { Line("%s", buf); len = 0; }
    }
    // Full list on the serial port too (debug builds).
    printf("[FTRACE] last game functions, oldest first:\n");
    for (int i = 0; i < n; ++i) printf("%08lx%c", (unsigned long)a[i], (i % 8 == 7) ? '\n' : ' ');
    printf("\n");
}
}  // namespace

// Straight to dbgio (no stdio buffering), so nothing is lost when the
// machine stops right after.
extern "C" void dc_trace_char(char c)
{
    const char s[2] = { c, 0 };
    dbgio_write_str(s);
}

void crashlog_install(void)
{
    s_ftraceThread = thd_get_current();     // the game runs on this kthread
    const irq_t codes[] = { EXC_DATA_ADDRESS_READ, EXC_DATA_ADDRESS_WRITE, EXC_ILLEGAL_INSTR,
                            EXC_SLOT_ILLEGAL_INSTR, EXC_GENERAL_FPU, EXC_SLOT_FPU };
    for (irq_t c : codes) irq_set_handler(c, ExceptionHandler, nullptr);
    assert_set_handler(AssertHandler);
}

void crashlog_mark(const char* step)
{
    if (step == nullptr) return;
    strncpy(s_lastMark, step, sizeof(s_lastMark) - 1);
    s_lastMark[sizeof(s_lastMark) - 1] = '\0';
    if (RE1_DC_VERBOSE) printf("[mark] %s\n", step);
}

// Called by the audio thread's watchdog when the main loop is stuck. Not
// fatal: if the game recovers, the PVR's next frame replaces the report.
extern "C" void dc_crash_stall(const irq_context_t* mainCtx, unsigned stalledMs)
{
    if (s_reported) return;
    BeginScreen("STALL");
    if (mainCtx != nullptr) {
        Line("main loop stuck for %u s (screen returns if it recovers)", stalledMs / 1000);
        ContextLines(mainCtx);
    } else {
        Line("test screen (L+R+Y+Start) - the game continues");
    }
    StateLines();
}

// ---------------------------------------------------------------------------
// RE1_DC_UACOUNT=1: the game code is built with -fsanitize=alignment, and this
// is the whole "runtime": GCC calls it before every misaligned access with the
// source location of the access. Counted per site and listed by
// dc_report_unaligned. Works in an emulator (Flycast does not fault on
// misaligned accesses, so the exception counter above stays at 0 there).
// ---------------------------------------------------------------------------
namespace {
struct UbsanLoc { const char* file; uint32_t line; uint32_t column; };
struct UbsanTypeMismatch { UbsanLoc loc; const void* type; unsigned char logAlign; unsigned char kind; };
struct UbSite { const UbsanTypeMismatch* d; uint32_t count; };
const int kUbSites = 512;                 // power of two
UbSite s_ubSites[kUbSites];
volatile uint32_t s_ubTotal = 0;
}

extern "C" void __ubsan_handle_type_mismatch_v1(UbsanTypeMismatch* d, uintptr_t)
    __attribute__((no_instrument_function));
extern "C" void __ubsan_handle_type_mismatch_v1(UbsanTypeMismatch* d, uintptr_t)
{
    s_ubTotal = s_ubTotal + 1;
    unsigned h = ((uintptr_t)d >> 3) & (kUbSites - 1);
    for (int probe = 0; probe < kUbSites; ++probe, h = (h + 1) & (kUbSites - 1)) {
        if (s_ubSites[h].d == d) { s_ubSites[h].count++; return; }
        if (s_ubSites[h].d == nullptr) { s_ubSites[h].d = d; s_ubSites[h].count = 1; return; }
    }
}

static void ReportUbsanSites()
{
    if (!RE1_DC_UACOUNT) return;
    printf("[DC] unaligned accesses by source line this period: %lu\n", (unsigned long)s_ubTotal);
    for (int k = 0; k < 20; ++k) {            // top 20, selection by count
        int best = -1;
        for (int i = 0; i < kUbSites; ++i)
            if (s_ubSites[i].d && s_ubSites[i].count && (best < 0 || s_ubSites[i].count > s_ubSites[best].count)) best = i;
        if (best < 0) break;
        const char* f = s_ubSites[best].d->loc.file;
        const char* slash = f ? strrchr(f, '/') : nullptr;
        printf("    %7lu  %s:%lu\n", (unsigned long)s_ubSites[best].count, slash ? slash + 1 : (f ? f : "?"),
               (unsigned long)s_ubSites[best].d->loc.line);
        s_ubSites[best].count = 0;            // also resets it for the next period
    }
    for (int i = 0; i < kUbSites; ++i) s_ubSites[i].count = 0;
    s_ubTotal = 0;
}

// Debug builds: where the game did unaligned accesses (emulated above), so the
// hot ones can be fixed in the source. Called from the periodic report.
extern "C" void dc_report_unaligned(void)
{
    ReportUbsanSites();
    if (s_uaTotal == 0) return;
    printf("[DC] unaligned accesses emulated: %lu at %d site(s):", (unsigned long)s_uaTotal, s_uaSiteCount);
    for (int i = 0; i < s_uaSiteCount; ++i)
        printf("%s%08lx x%lu", (i % 4) ? "  " : "\n    ", (unsigned long)s_uaSites[i].pc, (unsigned long)s_uaSites[i].count);
    printf("\n");
}

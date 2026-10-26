// main.cpp - Dreamcast (KallistiOS) entry point.
//
// Counterpart of src/platform/linux/main.cpp: read config.ini, bring up the
// Marni system, drive main_loop() at the engine's 33 ms tick with the same
// frame limiter. No window and no event pump - maple input is polled by
// CMarniDirectInput::UpdateAllInputStates from inside the game loop.
#include <kos.h>
#include <kos/dbgio.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Globals.h"
#include "platform/platform.h"
#include "platform/dreamcast/dc_platform.h"
#include "system/AssetPath.h"
#include "system/ConfigFile.h"
#include "marni/MarniDX.h"
#include "marni/MarniSystem.h"

KOS_INIT_FLAGS(INIT_DEFAULT);

// Bumped once per main-loop pass; the audio thread's watchdog reads it.
extern "C" { volatile uint32_t g_dcHeartbeat = 0; }

// 0x00441f26: 33 ms == 30 ticks/s.
static const int kFrameIntervalMs = 33;

// Tick timing for the periodic report.
static uint64_t s_tickUsSum = 0;
static unsigned s_tickUsMax = 0, s_tickOver = 0, s_tickCount = 0;
static uint64_t s_periodT0 = 0;
static uint64_t s_nextTickUs = 0;     // tick deadline (see the main loop)
// PROF_STATES_FIX: set while the loop only waits for the next tick, so the
// profiler (audio.cpp) can count it as idle instead of as work.
extern "C" { volatile int g_dcPacingWait = 0; }

// L+R+Y+Start: draw the crash/stall report once, to check it shows on this TV.
extern "C" void dc_crash_stall(const irq_context_t* mainCtx, unsigned stalledMs);
extern "C" volatile int g_dcFtraceArmed;   // crash.cpp (RE1_DC_FTRACE builds)
extern "C" void dc_report_unaligned(void);   // crash.cpp
extern "C" void dc_prof_report(void);        // audio.cpp (RE1_DC_PROF builds)
extern "C" void dc_iso_cache_report(void);   // dc_iso.cpp
static bool ReportComboHeld(void)
{
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (dev == NULL) return false;
    cont_state_t* st = (cont_state_t*)maple_dev_status(dev);
    if (st == NULL) return false;
    return (st->buttons & (CONT_Y | CONT_START)) == (CONT_Y | CONT_START) &&
           st->ltrig > 200 && st->rtrig > 200;
}

// A+B+X+Y+Start held: leave to dcload / the BIOS menu (the usual KOS combo).
static bool ExitComboHeld(void)
{
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (dev == NULL) return false;
    cont_state_t* st = (cont_state_t*)maple_dev_status(dev);
    if (st == NULL) return false;
    const uint32_t all = CONT_A | CONT_B | CONT_X | CONT_Y | CONT_START;
    return (st->buttons & all) == all;
}

#ifndef RE1_DC_POISON
#define RE1_DC_POISON 0
#endif

// RE1_DC_POISON=1: fill every free byte of the heap with garbage before the
// game starts. Flycast boots with zeroed RAM, so malloc'd memory reads as 0
// there; a real console's RAM is full of leftovers from the BIOS/loader. Code
// that uses memory it never initialised then behaves differently - harmless
// zeros in the emulator, wild pointers or runaway counters on hardware. With
// this, the emulator sees the hardware's kind of memory.
static void PoisonFreeHeap(void)
{
    // Grab everything malloc can give, fill it, give it back. dlmalloc hands
    // the same (now dirty) memory out again later.
    void* blocks[512];
    int n = 0;
    size_t total = 0;
    uint32_t x = 0x2545F491u;                    // xorshift: varied, pointer-ish garbage
    for (size_t sz = 1024 * 1024; sz >= 4096 && n < 512; ) {
        void* b = malloc(sz);
        if (b == NULL) { sz /= 2; continue; }
        uint32_t* w = (uint32_t*)b;
        for (size_t i = 0; i < sz / 4; ++i) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            w[i] = x;
        }
        blocks[n++] = b;
        total += sz;
    }
    for (int i = n - 1; i >= 0; --i) free(blocks[i]);
    printf("[DC] RE1_DC_POISON: filled %u KB of free heap with garbage\n", (unsigned)(total / 1024));
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
    if (RE1_DC_POISON) PoisonFreeHeap();

    printf("\n[RE1] Resident Evil PC decomp - Dreamcast build, data root %s\n",
           RE1_DC_ROOT);
#if !RE1_DC_VERBOSE
    // Release build: no serial/dcload log at all. The game's own trace
    // (raw printf in shared code, dbg_printf, OutputDebugString) and the port's
    // diagnostics all end here. Debug build: make ... RE1_DC_VERBOSE=1
    printf("[RE1] release build - log disabled (rebuild with RE1_DC_VERBOSE=1)\n");
    dbgio_disable();
#endif
    dc_report_memory("boot");

    crashlog_install();

    // /cd through the port's own ISO reader (dc_iso.cpp): it finds the game in
    // the high-density area of a GD-ROM (GDI) as well as on a CD-R/CDI, and it
    // does not rescan directories on every open. Must precede the first /cd
    // access (config.ini).
    if (dc_iso_mount() != 0) printf("[RE1] /cd stays on KOS fs_iso9660\n");

    crashlog_mark("config");
    ConfigFile_EnsureExists();   // fails harmlessly on read-only /cd
    ConfigFile_Load();
#if !RE1_DC_SATURN
    // Built without the Saturn content (RE1_DC_SATURN=0): a config.ini asking
    // for Mode=SATURN gets the PC release instead of a half-stubbed mode.
    if (g_GameMode == GAME_MODE_SATURN) {
        printf("[DC] Mode=SATURN needs a RE1_DC_SATURN=1 build - playing ORIGINAL\n");
        g_GameMode = GAME_MODE_OG;
        SetAssetMode("");          // the overlay follows the mode (ConfigFile.cpp)
    }
#endif

    // Saves: the game reads/writes the RAM disk; vmu_save.cpp mirrors it to
    // the VMU (loaded here, written back after every save).
    SetSaveRoot(dc_save_root());
    dc_save_init();

    // 640x480 is the only mode the DC renderer will offer; the game's
    // logical 320x240 is scaled by the backend exactly as on PC.
    g_dwScreenWidth  = 640;
    g_dwScreenHeight = 480;
    g_bFullScreen    = TRUE;

    // Game code only ever checks HWND for NULL; there is no window object.
    g_hWnd = (HWND)1;

    crashlog_mark("sound");
    StartSoundSystemAsync(g_hWnd);

    crashlog_mark("tasks");
    TaskScheduler_Init();

    crashlog_mark("marni");
    InitializeMarniSystem();
    if (!IsGraphicsSystemReadyForOperation()) {
        plat_fatal("graphics system failed to initialize");
    }
    dc_report_memory("after init");

    g_bWindowFocused = TRUE;
    g_bWindowActive  = TRUE;
    g_bQuitFlag      = FALSE;

    g_dwGameTimer1 = plat_time_ms();
    unsigned frames = 0;

    crashlog_mark("main loop");
    s_periodT0 = timer_us_gettime64();
    for (;;) {
        g_dcHeartbeat++;
        if (g_bQuitFlag || ExitComboHeld()) break;
        {
            static bool s_comboWas = false;
            const bool combo = ReportComboHeld();
            if (combo && !s_comboWas) dc_crash_stall(nullptr, 0);
            s_comboWas = combo;
        }
#if RE1_DC_FTRACE
        {
            // L+R (no Y/Start): pause/resume the function-entry ring (crash.cpp).
            static bool s_lrWas = false;
            maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
            cont_state_t* st = dev ? (cont_state_t*)maple_dev_status(dev) : NULL;
            const bool lr = st && st->ltrig > 200 && st->rtrig > 200 &&
                            !(st->buttons & (CONT_Y | CONT_START));
            if (lr && !s_lrWas) {
                g_dcFtraceArmed = !g_dcFtraceArmed;
                dbgio_write_str(g_dcFtraceArmed ? "\n[FTRACE ON]\n" : "\n[FTRACE OFF]\n");
            }
            s_lrWas = lr;
        }
#endif

        // Tick pacing. The PC pump starts a tick once 33 ms (16 ms with frame
        // skip off: menus, door animation) have passed since the previous one
        // STARTED, with no catch-up. On the SH-4 that lost every sleep
        // overshoot for good (measured ~41 ms per tick, the game at ~80%), so
        // ticks follow a deadline series: next = previous deadline + interval,
        // which absorbs small lateness and keeps the average exact. But only
        // SMALL lateness (kCatchUpUs): after a long tick - a camera cut that
        // rebuilds the background, a voice-line load - the schedule restarts
        // from now instead of replaying the missed ticks back to back, which
        // made the game visibly speed up right after every cut. Ticks are thus
        // never closer than interval - kCatchUpUs apart.
        const uint64_t intervalUs = (g_bUseFrameSkip == 1) ? 33333u : 16667u;
        const uint64_t kCatchUpUs = 8000u;
        uint64_t nowUs = timer_us_gettime64();
        if (s_nextTickUs == 0) s_nextTickUs = nowUs;
        if (nowUs < s_nextTickUs) {
            const uint64_t waitUs = s_nextTickUs - nowUs;
            g_dcPacingWait = 1;
            // TICK_OVERSLEEP_FIX: thd_sleep() wakes late (well past a 2 ms
            // margin, often past kCatchUpUs, which restarts the schedule and
            // loses the overshoot) - sleep only long waits, spin the last 12 ms.
            if (waitUs > 16000) thd_sleep((unsigned)((waitUs - 12000) / 1000));
            else thd_pass();    // spin out the last ms; lets the audio thread run
            g_dcPacingWait = 0;
            continue;
        }
        if (nowUs - s_nextTickUs > kCatchUpUs) s_nextTickUs = nowUs;
        s_nextTickUs += intervalUs;
        DC_TRACE('.');

        DWORD now = (DWORD)(nowUs / 1000);
        g_dwGameTimer1 = now;

        // 0x00441dfe: FMV gate (movies are skipped until the DC decoder lands).
        if (g_bMCINotifyEnabled) {
            if (g_bMCINotifyFlag) {
                UpdateVideoPlayback();
                continue;
            }
            g_bMCINotifyFlag = TRUE;
        }

        const uint64_t tickT0 = timer_us_gettime64();
        const int keepGoing = main_loop();
        {
            // Tick cost. The engine has no catch-up: every tick over 33 ms
            // slows the game against real time (audio does not slow down).
            const unsigned us = (unsigned)(timer_us_gettime64() - tickT0);
            s_tickUsSum += us;
            if (us > s_tickUsMax) s_tickUsMax = us;
            if (us > 33333) s_tickOver++;
            s_tickCount++;
        }
        if (keepGoing == 0) break;

        // No guard pages on the DC: check the stack canaries every frame.
        if (!dc_check_stack_guards()) {
            dc_report_stack_usage();
            plat_fatal("task stack overflow");
        }

        if (RE1_DC_VERBOSE && (++frames % 300) == 0) {
            dc_report_unaligned();
            dc_prof_report();
            dc_iso_cache_report();   // ~10 s at full speed
            const uint64_t nowUs = timer_us_gettime64();
            const unsigned wallMs = (unsigned)((nowUs - s_periodT0) / 1000);
            // 300 ticks should take 10000 ms; speed = nominal / actual.
            const unsigned speed = wallMs ? (unsigned)((uint64_t)s_tickCount * 33333 / 10 / wallMs) : 0;
            printf("[RE1] frame %u: %u ticks in %u ms -> game speed %u%%, "
                   "tick avg %u.%u ms max %u.%u ms, %u ticks > 33 ms\n",
                   frames, s_tickCount, wallMs, speed,
                   (unsigned)(s_tickUsSum / s_tickCount / 1000),
                   (unsigned)((s_tickUsSum / s_tickCount / 100) % 10),
                   s_tickUsMax / 1000, (s_tickUsMax / 100) % 10, s_tickOver);
            s_periodT0 = nowUs;
            s_tickUsSum = 0; s_tickUsMax = 0; s_tickOver = 0; s_tickCount = 0;
            dc_report_memory("periodic");
            dc_report_stack_usage();
        }

        if ((int)(g_dwSystemTimer1 + 1000) < (int)now) {
            g_dwSystemTimer1 = now;
        }
    }

    CleanupVideoConfigAndSaveAllSettings();
    dc_report_stack_usage();
    MarniDX_DestroyGlobal();
    return 0;
}

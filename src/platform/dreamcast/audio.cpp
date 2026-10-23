// audio.cpp - Dreamcast audio backend: software mixer -> AICA via snd_stream.
//
// Same DirectSound contract as src/platform/linux/audio.cpp (SDL2), whose
// mixer this follows:
//   - bank indices 1..80, one per loaded WAV (0 == none)
//   - PlaySound(bank, slot): slot != 0 loops forever, slot == 0 plays once
//   - volume in DirectSound millibels (0 full, -10000 silence), amplitude
//     10^(mB/2000); pan -10000..10000
//   - GetStatus returns 1 while the bank is playing, 0 otherwise
//
// Dreamcast specifics
//   - Output: one stereo KOS sound stream (snd_stream) at 22050 Hz, or 44100
//     Hz for the asset migrator's PS1 audio tree. KOS streams are pull-based
//     but only progress when snd_stream_poll() runs, so a small KOS thread
//     polls every few ms; a mutex guards the banks against the game thread.
//   - Banks stay in main RAM as decoded PCM (AudioFile_Load), exactly like the
//     SDL backend. Every load of 64 KB or more is logged with its size:
//     whole-file music banks are the RAM risk to watch.
//   - The per-sample work is fixed point (16.16 position, Q15 gains): double
//     and float per sample would be slow on the SH-4.
#include "Globals.h"
#include "platform/platform.h"
#include "marni/MarniSound.h"
#include "system/AssetPath.h"
#include "system/AudioFile.h"
#include "platform/dreamcast/dc_platform.h"

#include <kos.h>
extern "C" volatile int g_dcPacingWait;   // main.cpp (PROF_STATES_FIX)
#include <arch/timer.h>
#include <kos/thread.h>
#include <kos/mutex.h>
#include <dc/sound/sound.h>
#include <dc/sound/stream.h>
#include <dc/g2bus.h>

#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BANKS 81            // 1..80

// Bisecting real-hardware hangs: RE1_DC_SILENT=1 never starts the AICA stream
// (no G2 traffic from the mixer) and reports every sound as finished at once,
// so scripts that wait for a voice line do not stall.
#ifndef RE1_DC_SILENT
#define RE1_DC_SILENT 0
#endif

extern "C" volatile uint32_t g_dcHeartbeat;   // main.cpp
extern "C" volatile uint32_t g_dcIoProgress;  // dc_iso.cpp
extern "C" void dc_iso_cache_init(void);       // dc_iso.cpp
extern "C" void dc_crash_stall(const irq_context_t* mainCtx, unsigned stalledMs);   // crash.cpp

namespace {

// Per channel, in bytes: 8 KB = 4096 frames = ~186 ms at 22050 Hz. KOS refills
// in halves, so a sound effect starts at most ~90 ms after PlaySound.
const int kStreamBufBytes = 8192;
const int kPollMs         = 5;

int s_outRate = 22050;

struct Bank {
    bool     active;
    bool     playing;
    bool     loop;
    BYTE*    wavData;           // owned buffer (AudioFile_Load)
    BYTE*    pcm;               // points into wavData
    uint32_t frames;            // total frames
    int      sampleRate;
    int      channels;
    int      bits;              // 8 or 16
    int      vol;               // millibels
    int      pan;               // -10000..10000
    int      status;            // 1 playing / 0 stopped
    uint32_t pos;               // integer frame
    uint32_t frac;              // 16-bit fraction
    uint32_t step;              // 16.16 source frames per output frame
    uint32_t loopBegin, loopEnd;// frames; loopEnd 0 = whole buffer
    bool     ps1;               // linear interpolation (PS1 audio only)
    char     name[64];
};

Bank s_banks[MAX_BANKS];
mutex_t s_lock = MUTEX_INITIALIZER;

snd_stream_hnd_t s_stream = SND_STREAM_INVALID;
kthread_t*       s_mainThread = nullptr;   // for the stall watchdog
kthread_t*       s_thread = nullptr;
volatile bool    s_run = false;
int              s_suspended = 0;

size_t s_pcmBytes = 0;          // RAM held by loaded banks

// Mix scratch: 32-bit accumulator and the S16 block handed to KOS.
const int kMaxFrames = kStreamBufBytes;   // generous upper bound
int32_t  s_acc[kMaxFrames * 2];
int16_t* s_out = nullptr;                  // 32-byte aligned, kMaxFrames*2

// Movie audio ring (video.cpp feeds it while demuxing; see dc_movie_audio_*).
// S16 stereo at the movie's own rate, resampled by the mixer like a bank.
int16_t* s_movRing = nullptr;
uint32_t s_movCap = 0;          // frames
uint32_t s_movWrite = 0;        // frames ever written
uint32_t s_movRead = 0;         // frames ever consumed (the movie clock)
uint32_t s_movFrac = 0;
uint32_t s_movStep = 0;
bool     s_movOn = false;

// FMV audio stream (same contract as the SDL backend, see platform.h).
const short* s_streamPcm = nullptr;
long s_streamEnd = 0;
long s_streamPos = 0;
int  s_streamOn  = 0;

inline int32_t ReadSample(const Bank& b, uint32_t frame, int channel)
{
    const uint32_t idx = frame * (uint32_t)b.channels + (uint32_t)channel;
    if (b.bits == 8) return ((int32_t)b.pcm[idx] - 128) << 8;   // unsigned 8-bit
    return (int32_t)(int16_t)((unsigned)b.pcm[idx * 2] | ((unsigned)b.pcm[idx * 2 + 1] << 8));
}

float MillibelsToAmplitude(int mB)
{
    if (mB <= -10000) return 0.0f;
    if (mB >= 0) return 1.0f;
    return powf(10.0f, (float)mB / 2000.0f);
}

// MERGE140: IDirectSoundBuffer::Stop leaves the play cursor where it is, and
// the original's PlaySound (0x0041ee60) only rewinds a buffer that is still
// playing - so a stopped bank resumes. PauseSounds / ResumePausedSounds are
// built on exactly that (same as the Linux backend since 1.4.0).
void StopBank(Bank& b)
{
    b.playing = false;
    b.status = 0;
}

void FreeBank(Bank& b)
{
    StopBank(b);
    b.pos = b.frac = 0;
    if (b.wavData != nullptr) {
        s_pcmBytes -= (size_t)b.frames * b.channels * (b.bits / 8);
        free(b.wavData);
    }
    b.wavData = nullptr;
    b.pcm = nullptr;
    b.frames = 0;
    b.active = false;
    b.name[0] = '\0';
}

// Mix one bank into acc[0..frames). Called with s_lock held.
void MixBank(Bank& b, int32_t* acc, int frames)
{
    const uint32_t end = (b.loop && b.loopEnd != 0) ? b.loopEnd : b.frames;
    const uint32_t begin = (b.loopEnd != 0) ? b.loopBegin : 0;

    const float amp = MillibelsToAmplitude(b.vol);
    const int32_t gl = (int32_t)(amp * (float)(10000 - b.pan) / 20000.0f * 32768.0f);
    const int32_t gr = (int32_t)(amp * (float)(10000 + b.pan) / 20000.0f * 32768.0f);
    const int ch1 = (b.channels == 1) ? 0 : 1;

    for (int f = 0; f < frames; ++f) {
        if (b.pos >= end) {
            if (b.loop) {
                b.pos = begin + (b.pos - end);
                if (b.pos >= end) b.pos = begin;
            } else {
                b.playing = false;
                b.status = 0;
                b.pos = b.frac = 0;       // played out: the next PlaySound starts over
                return;
            }
        }
        int32_t l, r;
        if (b.ps1) {
            uint32_t j = b.pos + 1;
            if (j >= end) j = b.loop ? begin : b.pos;
            const int32_t t = (int32_t)(b.frac >> 1);          // Q15
            const int32_t l0 = ReadSample(b, b.pos, 0),   l1 = ReadSample(b, j, 0);
            const int32_t r0 = ReadSample(b, b.pos, ch1), r1 = ReadSample(b, j, ch1);
            l = l0 + (int32_t)(((int64_t)(l1 - l0) * t) >> 15);   // diff*t can exceed 2^31
            r = r0 + (int32_t)(((int64_t)(r1 - r0) * t) >> 15);
        } else {
            l = ReadSample(b, b.pos, 0);
            r = (ch1 == 0) ? l : ReadSample(b, b.pos, 1);
        }
        acc[f * 2 + 0] += (l * gl) >> 15;
        acc[f * 2 + 1] += (r * gr) >> 15;

        b.frac += b.step;
        b.pos  += b.frac >> 16;
        b.frac &= 0xFFFF;
    }
}

// Mix `frames` stereo frames into s_acc (32-bit accumulator).
void MixInto(int frames)
{
    memset(s_acc, 0, (size_t)frames * 2 * sizeof(int32_t));

    mutex_lock(&s_lock);
    if (!s_suspended) {
        if (s_streamOn && s_streamPcm != nullptr) {
            long pos = s_streamPos;
            for (int f = 0; f < frames; ++f) {
                if (pos >= s_streamEnd) { s_streamOn = 0; break; }
                s_acc[f * 2 + 0] += s_streamPcm[pos * 2 + 0];
                s_acc[f * 2 + 1] += s_streamPcm[pos * 2 + 1];
                ++pos;
            }
            s_streamPos = pos;
        }
        if (s_movOn && s_movRing != nullptr) {
            for (int f = 0; f < frames; ++f) {
                if (s_movRead >= s_movWrite) break;     // underrun: the clock waits
                const uint32_t idx = (s_movRead % s_movCap) * 2;
                s_acc[f * 2 + 0] += s_movRing[idx + 0];
                s_acc[f * 2 + 1] += s_movRing[idx + 1];
                s_movFrac += s_movStep;
                s_movRead += s_movFrac >> 16;
                s_movFrac &= 0xFFFF;
                if (s_movRead > s_movWrite) s_movRead = s_movWrite;
            }
        }
        for (int i = 1; i < MAX_BANKS; ++i) {
            Bank& b = s_banks[i];
            if (b.active && b.playing && b.pcm != nullptr && b.frames > 0) MixBank(b, s_acc, frames);
        }
    }
    mutex_unlock(&s_lock);
}

inline int16_t Clamp16(int32_t v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

// Direct stream callback: KOS hands us the left/right buffers in AICA RAM and
// we write them over the G2 bus ourselves. The default callback path makes
// KOS copy into sound RAM with the store queues - whose lock the PVR backend
// holds for the whole open polygon list (pvr_list_begin..finish), i.e. most of
// every frame, and for 200+ ms on camera cuts. The refill then missed the
// buffer and the AICA replayed stale audio: the garbled, "lisping" voices.
//
// size_req is the total for both channels (snd_stream_fill passes size *
// channels, whatever the header comment says), S16, so frames = size_req / 4.
// AUDIO_G2_FIFO_FIX: copy samples into sound RAM without overrunning the G2
// write FIFO. g2_write_block_16 issued one 16-bit write per sample back to
// back; real hardware drops writes once the FIFO is full - right after the
// left block, so the right channel lost samples and ticked. Bursts of 8
// 32-bit writes, each after g2_fifo_wait(), IRQs/G2 DMA held only per burst.
void G2WriteSamples(const int16_t* src, uintptr_t dst, int n)
{
    int i = 0;
    if ((dst & 3) != 0 && n > 0) {               // align the destination
        g2_ctx_t ctx = g2_lock();
        g2_fifo_wait();
        *(volatile uint16_t*)dst = (uint16_t)src[0];
        g2_unlock(ctx);
        i = 1;
    }
    while (n - i >= 2) {
        const int pairs = ((n - i) / 2 < 8) ? (n - i) / 2 : 8;
        g2_ctx_t ctx = g2_lock();
        g2_fifo_wait();
        volatile uint32_t* d = (volatile uint32_t*)(dst + (uintptr_t)i * 2);
        for (int k = 0; k < pairs; ++k) {
            const uint32_t lo = (uint16_t)src[i + 2 * k];
            const uint32_t hi = (uint16_t)src[i + 2 * k + 1];
            d[k] = lo | (hi << 16);
        }
        g2_unlock(ctx);
        i += pairs * 2;
    }
    if (i < n) {                                  // odd sample left over
        g2_ctx_t ctx = g2_lock();
        g2_fifo_wait();
        *(volatile uint16_t*)(dst + (uintptr_t)i * 2) = (uint16_t)src[i];
        g2_unlock(ctx);
    }
}

size_t DirectCallback(snd_stream_hnd_t, uintptr_t left, uintptr_t right, size_t size_req)
{
    int frames = (int)(size_req / 4);
    if (frames > kMaxFrames) frames = kMaxFrames;
    MixInto(frames);

    int16_t* l = s_out;                  // de-interleave: left then right halves
    int16_t* r = s_out + kMaxFrames;
    for (int i = 0; i < frames; ++i) {
        l[i] = Clamp16(s_acc[i * 2 + 0]);
        r[i] = Clamp16(s_acc[i * 2 + 1]);
    }
    // In chunks: g2_write_block_16 keeps IRQs off for the whole block.
    DC_TRACE('A');
    // AUDIO_G2_FIFO_FIX: FIFO-paced 32-bit writes (see G2WriteSamples).
    for (int done = 0; done < frames; done += 512) {
        const int n = (frames - done < 512) ? frames - done : 512;
        G2WriteSamples(l + done, left + (uintptr_t)done * 2, n);
        G2WriteSamples(r + done, right + (uintptr_t)done * 2, n);
    }
    DC_TRACE('a');
    return (size_t)frames * 4;
}

// Stall watchdog. main.cpp bumps g_dcHeartbeat once per main-loop pass; if
// it stops for 3 s, print where the main thread is parked. While this thread
// runs the main thread is not running, so its saved context is current.
void Watchdog()
{
    static uint32_t lastBeat = 0;
    static uint64_t lastChange = 0;
    static bool reported = false;
    const uint64_t now = timer_ms_gettime64();
    // Alive = the main loop advanced OR a disc request completed (a load can
    // keep one main-loop pass busy for many seconds on a slow CD-R).
    const uint32_t beat = g_dcHeartbeat + g_dcIoProgress;
    static bool shown = false;
    if (beat != lastBeat || lastChange == 0) {
        lastBeat = beat;
        lastChange = now;
        reported = false;
        shown = false;
        return;
    }
    // On a console without a serial cable this is the only way to see a
    // hang: after 8 s draw the report over the frozen picture (crash.cpp).
    if (!shown && now - lastChange >= 8000 && s_mainThread != nullptr) {
        shown = true;
        dc_crash_stall(&s_mainThread->context, (unsigned)(now - lastChange));
    }
    if (reported || now - lastChange < 3000 || s_mainThread == nullptr) return;
    reported = true;
    const irq_context_t& c = s_mainThread->context;
    printf("[WATCHDOG] main loop stalled for %u ms: pc=%08lx pr=%08lx sp=%08lx\n",
           (unsigned)(now - lastChange), (unsigned long)c.pc, (unsigned long)c.pr,
           (unsigned long)c.r[15]);
    // Return-address-looking words near the top of its stack (code is below
    // the bss start; good enough for addr2line).
    const uint32_t* sp = (const uint32_t*)c.r[15];
    printf("[WATCHDOG] stack words:");
    for (int i = 0; i < 24; ++i) {
        uint32_t v = sp[i];
        if (v >= 0x8c010000u && v < 0x8c200000u) printf(" %08lx", (unsigned long)v);
    }
    printf("\n[WATCHDOG] resolve: sh-elf-addr2line -f -C -e build/dc/residentevil.elf <addr>\n");
    thd_pslist(printf);
}

// ---------------------------------------------------------------------------
// Sampling profiler (RE1_DC_PROF=1). This thread preempts the game thread every
// 5 ms; at that moment the game thread's saved context says where it was. The
// PCs go out with the periodic report (main.cpp -> dc_prof_report) and
// tools/dreamcast/prof_resolve.sh turns them into a function ranking.
// ---------------------------------------------------------------------------
#ifndef RE1_DC_PROF
#define RE1_DC_PROF 0
#endif
const int kProfMax = 4096;
uint32_t s_profPc[kProfMax];
uint32_t s_profCall[kProfMax][4];      // PROF_CALLERS_DIAG: pr + 3 stack words
volatile int s_profCount = 0;
volatile uint32_t s_profAudioUs = 0;     // time in snd_stream_poll since the last report
uint64_t s_profLastReportUs = 0;

uint32_t s_profTicks = 0, s_profPacing = 0, s_profBlocked = 0;

void ProfSample()
{
    if (!RE1_DC_PROF || s_mainThread == nullptr) return;
    // PROF_STATES_FIX: classify every tick. Only a game thread that was
    // preempted while doing work has a meaningful pc for the ranking.
    ++s_profTicks;
    if (g_dcPacingWait) { ++s_profPacing; return; }
    if (s_mainThread->state != STATE_READY) { ++s_profBlocked; return; }
    const int i = s_profCount;
    if (i < kProfMax) {
        const irq_context_t& c = s_mainThread->context;
        s_profPc[i] = c.pc;
        // PROF_CALLERS_DIAG: return address + the first code-looking words on
        // the stack, to see which game code led into kernel functions.
        s_profCall[i][0] = c.pr;
        const uint32_t* sp = (const uint32_t*)c.r[15];
        int k = 1;
        for (int w = 0; w < 48 && k < 4; ++w) {
            const uint32_t v = sp[w];
            if (v >= 0x8c010000u && v < 0x8c300000u && (v & 1) == 0) s_profCall[i][k++] = v;
        }
        while (k < 4) s_profCall[i][k++] = 0;
        s_profCount = i + 1;
    }
}

void* PollThread(void*)
{
    while (s_run) {
        // PROF_SAMPLING_FIX: sample first, at the timer wake-up. Sampling after
        // snd_stream_poll() caught the game thread exactly where it released
        // the mixer lock, not where it spends its time.
        ProfSample();
        if (s_stream != SND_STREAM_INVALID) {
            DC_TRACE('S');
            const uint64_t t0 = RE1_DC_PROF ? timer_us_gettime64() : 0;
            snd_stream_poll(s_stream);
            if (RE1_DC_PROF) s_profAudioUs += (uint32_t)(timer_us_gettime64() - t0);
            DC_TRACE('s');
        }
        Watchdog();
        thd_sleep(kPollMs);
    }
    return nullptr;
}

// Stream refill + the main-loop watchdog. Started even without a stream
// (silent build, AICA init failure): the stall report must not depend on audio.
void StartPollThread()
{
    if (s_thread != nullptr) return;
    s_mainThread = thd_get_current();     // InitializeSoundSystem runs on the main thread
    s_run = true;
    s_thread = thd_create(0, PollThread, nullptr);
    // Above the game thread (10): a long tick (camera cut, room load) must not
    // delay the refill past the 186 ms buffer.
    if (s_thread != nullptr) thd_set_prio(s_thread, 5);
}

}  // namespace

// ---------------------------------------------------------------------------
// DirectSound
// ---------------------------------------------------------------------------

DirectSound::DirectSound(HWND hwnd)
{
    (void)hwnd;
    memset(this, 0, sizeof(*this));
    *(int*)((BYTE*)this + 0x10) = 1;    // the shared wrappers gate on this
    m_bInitialized = 1;
}

void DirectSound::compact(void) {}

int DirectSound::GetStatus(int bank)
{
    if (bank <= 0 || bank >= MAX_BANKS) return 0;
    return s_banks[bank].status;
}

void DirectSound::Release(void) { mutex_lock(&s_lock); s_suspended = 1; mutex_unlock(&s_lock); }
void DirectSound::Reload(void)  { mutex_lock(&s_lock); s_suspended = 0; mutex_unlock(&s_lock); }

void DirectSound::DestroySound(int bank)
{
    if (bank <= 0 || bank >= MAX_BANKS) return;
    mutex_lock(&s_lock);
    FreeBank(s_banks[bank]);
    mutex_unlock(&s_lock);
}

int DirectSound::NewDirectSoundBuffer(DWORD*) { return 1; }

void DirectSound::StopSound(int bank)
{
    if (bank <= 0 || bank >= MAX_BANKS) return;
    mutex_lock(&s_lock);
    StopBank(s_banks[bank]);
    mutex_unlock(&s_lock);
}

void DirectSound::PlaySound(int bank, unsigned int slot)
{
    if (bank <= 0 || bank >= MAX_BANKS) return;
    mutex_lock(&s_lock);
    Bank& b = s_banks[bank];
    if (b.active && b.pcm != nullptr && !RE1_DC_SILENT) {
        // 0x0041ee60: SetCurrentPosition(0) only while the buffer is playing.
        if (b.playing) b.pos = b.frac = 0;
        b.loop = (slot != 0);
        b.playing = true;
        b.status = 1;
    }
    mutex_unlock(&s_lock);
}

void DirectSound::SetVol(int bank, int vol)
{
    if (bank <= 0 || bank >= MAX_BANKS) return;
    if (vol > -1) vol = -1;
    if (vol < -9999) vol = -9999;
    mutex_lock(&s_lock);
    s_banks[bank].vol = vol;
    mutex_unlock(&s_lock);
}

void DirectSound::SetPan(int bank, int pan)
{
    if (bank <= 0 || bank >= MAX_BANKS) return;
    if (pan > 10000) pan = 10000;
    if (pan < -10000) pan = -10000;
    mutex_lock(&s_lock);
    s_banks[bank].pan = pan;
    mutex_unlock(&s_lock);
}

int DirectSound::GetVol(int bank)
{
    if (bank <= 0 || bank >= MAX_BANKS) return 0;
    return s_banks[bank].vol;
}

int DirectSound::CreateSound(const char* wavName)
{
    if (wavName == nullptr) return 0;

    char resolved[260];
    const char* actualPath = ResolveAssetRoot(wavName, resolved, sizeof(resolved));

    // Decode outside the lock - it reads the disc.
    const uint64_t t0 = timer_ms_gettime64();
    char found[260];
    AudioFileData af;
    if (!AudioFile_Find(actualPath, found, sizeof(found)) || !AudioFile_Load(found, &af)) {
        static int s_failLogged = 0;
        if (s_failLogged < 8) {
            printf("[AUDIO] could not load bank: %s\n", actualPath);
            if (++s_failLogged == 8) printf("[AUDIO] (further bank load failures suppressed)\n");
        }
        return 0;
    }
    const int bytesPerFrame = (af.bitsPerSample / 8) * af.channels;
    if (bytesPerFrame <= 0 || af.sampleRate <= 0) { free(af.buffer); return 0; }

    mutex_lock(&s_lock);
    int bank = 0;
    for (int i = 1; i < MAX_BANKS; ++i) {
        if (!s_banks[i].active) { bank = i; break; }
    }
    if (bank == 0) {
        mutex_unlock(&s_lock);
        free(af.buffer);
        return 0;
    }

    Bank& b = s_banks[bank];
    FreeBank(b);
    b.wavData    = af.buffer;
    b.pcm        = af.pcm;
    b.frames     = af.pcmSize / (uint32_t)bytesPerFrame;
    b.sampleRate = af.sampleRate;
    b.channels   = af.channels;
    b.bits       = af.bitsPerSample;
    b.vol        = 0;
    b.pan        = 400;            // Windows backend default
    b.status     = 0;
    b.playing    = false;
    b.loop       = false;
    b.pos = b.frac = 0;
    b.step       = (uint32_t)(((uint64_t)af.sampleRate << 16) / (uint64_t)s_outRate);
    b.ps1        = af.ps1 != 0;
    b.loopBegin  = af.loopBegin;
    b.loopEnd    = af.loopEnd > af.loopBegin ? af.loopEnd : 0;
    b.active     = true;
    s_pcmBytes  += (size_t)b.frames * bytesPerFrame;

    const char* slash = strrchr(wavName, '/');
    const char* bs = strrchr(wavName, '\\');
    if (bs != nullptr && (slash == nullptr || bs > slash)) slash = bs;
    strncpy(b.name, slash ? slash + 1 : wavName, sizeof(b.name) - 1);
    b.name[sizeof(b.name) - 1] = '\0';
    const size_t total = s_pcmBytes;
    const unsigned kb = (unsigned)(((size_t)b.frames * bytesPerFrame) / 1024);
    char name[64];
    memcpy(name, b.name, sizeof(name));
    mutex_unlock(&s_lock);

    if (kb >= 64) {
        printf("[AUDIO] bank %d %s: %u KB (%d Hz %d-bit %s), %u ms; banks hold %u KB\n",
               bank, name, kb, af.sampleRate, af.bitsPerSample,
               af.channels == 1 ? "mono" : "stereo",
               (unsigned)(timer_ms_gettime64() - t0), (unsigned)(total / 1024));
    }
    return bank;
}

void DirectSound::ErrorRoutine(int) {}

// ---------------------------------------------------------------------------
// Engine lifecycle
// ---------------------------------------------------------------------------

void InitializeSoundSystem(void)
{
    DirectSound* mgr = new DirectSound(NULL);
    g_pDirectSound = mgr;
    g_SoundManager = (void*)mgr;

    s_outRate = AudioFile_TreeHasCDAudio() ? 44100 : 22050;   // MERGE140: renamed upstream
    if (RE1_DC_SILENT) {
        printf("[AUDIO] RE1_DC_SILENT build - no AICA stream\n");
        StartPollThread();          // the watchdog lives there
        return;
    }

    if (snd_stream_init() < 0) {
        printf("[AUDIO] snd_stream_init failed - running silent\n");
        StartPollThread();
        return;
    }
    s_out = (int16_t*)memalign(32, (size_t)kMaxFrames * 2 * sizeof(int16_t));
    s_stream = snd_stream_alloc(nullptr, kStreamBufBytes);
    if (s_stream != SND_STREAM_INVALID) snd_stream_set_callback_direct(s_stream, DirectCallback);
    if (s_out == nullptr || s_stream == SND_STREAM_INVALID) {
        printf("[AUDIO] snd_stream_alloc failed - running silent\n");
        s_stream = SND_STREAM_INVALID;
        StartPollThread();
        return;
    }
    snd_stream_volume(s_stream, 255);
    snd_stream_start(s_stream, (uint32_t)s_outRate, 1);   // stereo

    StartPollThread();
    dc_iso_cache_init();      // the rest of sound RAM becomes the file cache
    printf("[AUDIO] AICA stream %d Hz stereo, %d KB/channel buffer, direct G2 writes\n",
           s_outRate, kStreamBufBytes / 1024);
}

void CleanupSoundManagerResources(void)
{
    if (s_thread != nullptr) {
        s_run = false;
        thd_join(s_thread, nullptr);
        s_thread = nullptr;
    }
    if (s_stream != SND_STREAM_INVALID) {
        snd_stream_stop(s_stream);
        snd_stream_destroy(s_stream);
        s_stream = SND_STREAM_INVALID;
    }
    mutex_lock(&s_lock);
    for (int i = 1; i < MAX_BANKS; ++i) FreeBank(s_banks[i]);
    mutex_unlock(&s_lock);
}

// ---------------------------------------------------------------------------
// FMV audio stream (used once the Cinepak player lands)
// ---------------------------------------------------------------------------

int plat_audio_stream_rate(void) { return s_outRate; }

void plat_audio_stream_play(const short* pcm, long totalFrames, long startFrame, long endFrame)
{
    mutex_lock(&s_lock);
    s_streamPcm = pcm;
    s_streamEnd = (endFrame <= 0 || endFrame > totalFrames) ? totalFrames : endFrame;
    s_streamPos = (startFrame < 0) ? 0 : startFrame;
    s_streamOn  = (pcm != nullptr && s_streamPos < s_streamEnd) ? 1 : 0;
    mutex_unlock(&s_lock);
}

void plat_audio_stream_stop(void)
{
    mutex_lock(&s_lock);
    s_streamOn  = 0;
    s_streamPcm = nullptr;
    mutex_unlock(&s_lock);
}

long plat_audio_stream_pos(void)
{
    if (!s_streamOn) return -1;
    return s_streamPos;
}

// ---------------------------------------------------------------------------
// Movie audio ring (Dreamcast-only API used by video.cpp)
//
// The PC/Linux path decodes a movie's whole soundtrack up front; on the DC a
// few minutes of S16 stereo would not fit, so the player streams it through
// this ring instead. The consumed-frame count is the movie clock: on an
// underrun the mixer stops consuming and the picture waits for the sound.
// ---------------------------------------------------------------------------

extern "C" int dc_movie_audio_begin(int sampleRate, int capacityFrames)
{
    if (RE1_DC_SILENT || s_stream == SND_STREAM_INVALID) return 0;   // movie falls back to the timer
    mutex_lock(&s_lock);
    free(s_movRing);
    s_movRing = (int16_t*)malloc((size_t)capacityFrames * 2 * sizeof(int16_t));
    s_movCap = s_movRing ? (uint32_t)capacityFrames : 0;
    s_movWrite = s_movRead = s_movFrac = 0;
    s_movStep = (uint32_t)(((uint64_t)sampleRate << 16) / (uint64_t)s_outRate);
    s_movOn = (s_movRing != nullptr);
    mutex_unlock(&s_lock);
    return s_movOn ? 1 : 0;
}

// Free space in frames.
extern "C" int dc_movie_audio_space(void)
{
    mutex_lock(&s_lock);
    const int space = s_movOn ? (int)(s_movCap - (s_movWrite - s_movRead)) : 0;
    mutex_unlock(&s_lock);
    return space;
}

// Buffered (not yet played) frames.
extern "C" int dc_movie_audio_buffered(void)
{
    mutex_lock(&s_lock);
    const int n = s_movOn ? (int)(s_movWrite - s_movRead) : 0;
    mutex_unlock(&s_lock);
    return n;
}

// Append interleaved S16 stereo frames; returns how many fit.
extern "C" int dc_movie_audio_push(const int16_t* frames, int count)
{
    mutex_lock(&s_lock);
    int n = 0;
    if (s_movOn) {
        const int space = (int)(s_movCap - (s_movWrite - s_movRead));
        n = count < space ? count : space;
        for (int i = 0; i < n; ++i) {
            const uint32_t idx = ((s_movWrite + (uint32_t)i) % s_movCap) * 2;
            s_movRing[idx + 0] = frames[i * 2 + 0];
            s_movRing[idx + 1] = frames[i * 2 + 1];
        }
        s_movWrite += (uint32_t)n;
    }
    mutex_unlock(&s_lock);
    return n;
}

// Frames played since dc_movie_audio_begin.
extern "C" uint32_t dc_movie_audio_played(void)
{
    mutex_lock(&s_lock);
    const uint32_t n = s_movRead;
    mutex_unlock(&s_lock);
    return n;
}

extern "C" void dc_movie_audio_end(void)
{
    mutex_lock(&s_lock);
    s_movOn = false;
    free(s_movRing);
    s_movRing = nullptr;
    s_movCap = s_movWrite = s_movRead = 0;
    mutex_unlock(&s_lock);
}

// LOCKU_DIAG (RE1_DC_PROF builds): every mutex_unlock in the program comes
// here (-Wl,--wrap=mutex_unlock); calls and time are tallied per call site.
#if RE1_DC_PROF
extern "C" int __real_mutex_unlock(mutex_t* m);
struct LockSite { uint32_t caller, mutex, calls, us; };
static LockSite s_lockSites[128];
static uint32_t s_lockDropped = 0;
extern "C" int __wrap_mutex_unlock(mutex_t* m)
{
    const uint32_t ra = (uint32_t)(uintptr_t)__builtin_return_address(0);
    const uint64_t t0 = timer_us_gettime64();
    const int r = __real_mutex_unlock(m);
    const uint32_t dt = (uint32_t)(timer_us_gettime64() - t0);
    const int irq = irq_disable();
    uint32_t h = (ra >> 1) * 2654435761u >> 25;          // 0..127
    for (int probe = 0; probe < 128; ++probe, h = (h + 1) & 127) {
        LockSite& e = s_lockSites[h];
        if (e.caller == 0) { e.caller = ra; e.mutex = (uint32_t)(uintptr_t)m; }
        if (e.caller == ra) { ++e.calls; e.us += dt; goto done; }
    }
    ++s_lockDropped;
done:
    irq_restore(irq);
    return r;
}
static void ReportLockSites()
{
    LockSite top[12] = {};
    for (int i = 0; i < 128; ++i) {
        const LockSite& e = s_lockSites[i];
        if (e.caller == 0) continue;
        for (int k = 0; k < 12; ++k) {
            if (e.us > top[k].us || top[k].caller == 0) {
                for (int j = 11; j > k; --j) top[j] = top[j - 1];
                top[k] = e;
                break;
            }
        }
    }
    for (int k = 0; k < 12 && top[k].caller != 0; ++k)
        printf("[LOCKU] caller=%08lx mutex=%08lx calls=%lu us=%lu\n",
               (unsigned long)top[k].caller, (unsigned long)top[k].mutex,
               (unsigned long)top[k].calls, (unsigned long)top[k].us);
    if (s_lockDropped) printf("[LOCKU] %lu calls from sites past the table\n", (unsigned long)s_lockDropped);
    memset(s_lockSites, 0, sizeof(s_lockSites));
    s_lockDropped = 0;
}
#else
static void ReportLockSites() {}
#endif

// Debug builds with RE1_DC_PROF=1: print and reset the PC samples gathered
// since the last report. Called from the main loop's periodic report.
extern "C" void dc_prof_report(void)
{
    if (!RE1_DC_PROF) return;
    const int n = s_profCount;
    printf("[PROF] %d samples:", n);
    for (int i = 0; i < n; ++i) printf("%s%08lx", (i % 10) ? " " : "\n[PROF] ", (unsigned long)s_profPc[i]);
    printf("\n[PROF] end\n");
    for (int i = 0; i < n; ++i)
        printf("[PROFC] %08lx %08lx %08lx %08lx %08lx\n", (unsigned long)s_profPc[i],
               (unsigned long)s_profCall[i][0], (unsigned long)s_profCall[i][1],
               (unsigned long)s_profCall[i][2], (unsigned long)s_profCall[i][3]);
    const uint64_t now = timer_us_gettime64();
    if (s_profLastReportUs != 0 && now > s_profLastReportUs) {
        const double secs = (double)(now - s_profLastReportUs) / 1e6;
        printf("[PROFA] audio thread: %.1f ms per second in snd_stream_poll (%.1f%% of the CPU)\n",
               s_profAudioUs / 1000.0 / secs, s_profAudioUs / 10000.0 / secs);
    }
    if (s_profTicks != 0) {
        const uint32_t work = s_profTicks - s_profPacing - s_profBlocked;
        printf("[PROFS] %lu ticks: work %.1f%%, pacing wait %.1f%%, blocked %.1f%%\n",
               (unsigned long)s_profTicks, 100.0 * work / s_profTicks,
               100.0 * s_profPacing / s_profTicks, 100.0 * s_profBlocked / s_profTicks);
    }
    ReportLockSites();
    s_profTicks = s_profPacing = s_profBlocked = 0;
    s_profLastReportUs = now;
    s_profAudioUs = 0;
    s_profCount = 0;
}

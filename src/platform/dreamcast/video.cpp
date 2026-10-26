// video.cpp - Dreamcast FMV backend: AVI + Cinepak, streamed from the disc.
//
// Same contract as src/platform/linux/video.cpp (ffmpeg): decode the movie
// into an RGBA frame, push it through a MarniDX texture, draw it full screen
// (or hand it to the overlay callback), and report the end of a segment.
//
// Dreamcast specifics
//   - No ffmpeg: a sequential AVI demuxer over 'movi' (no index needed) and a
//     Cinepak ('cvid') decoder written from the bitstream, following the
//     layout ffmpeg's cinepak.c documents (per-strip V1/V4 codebooks, intra
//     0x30 / inter 0x31 / V1-only 0x32 vector chunks).
//   - Audio: PCM 8/16, IMA ADPCM (0x11), MS ADPCM (0x02). It is STREAMED into
//     the mixer's movie ring (audio.cpp dc_movie_audio_*) as the file is read:
//     the PC/Linux path pre-decodes the whole soundtrack, which would not fit.
//   - Clock: frames the mixer has actually played. If the ring runs dry the
//     clock stops, so the picture can never get ahead of the sound. Without
//     audio the wall clock stands in.
//   - Reading: the direct ISO reader (dc_iso.cpp) - whole sectors by DMA in
//     64 KB blocks, bypassing KOS's iso9660 lookups and stream mode.
#include "../platform.h"

#include "../../Globals.h"
#include "../../marni/MarniDX.h"
#include "../../marni/MarniSystem.h"

#include <kos.h>

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
int      dc_movie_audio_begin(int sampleRate, int capacityFrames);
int      dc_movie_audio_space(void);
int      dc_movie_audio_buffered(void);
int      dc_movie_audio_push(const int16_t* frames, int count);
uint32_t dc_movie_audio_played(void);
void     dc_movie_audio_end(void);
}

// dc_iso.cpp (file scope: these are C symbols)
extern "C" {
int dc_iso_locate(const char* path, uint32_t* lba, uint32_t* size);
int dc_iso_read(uint32_t lba, uint32_t offset, uint32_t len, void* dst);
}

namespace {

inline uint32_t RL32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
inline uint16_t RL16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t RB32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
inline uint32_t RB24(const uint8_t* p) { return ((uint32_t)p[0] << 16) | (p[1] << 8) | p[2]; }
inline uint16_t RB16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t FCC(const char* s) { return (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24); }

// ===========================================================================
// Disc reader
// ===========================================================================

const uint32_t kReadBlock = 64 * 1024;     // whole sectors

// Sequential reader over the direct ISO reader (dc_iso.cpp): sector-aligned
// 64 KB blocks by DMA, no iso9660 lookups and no iso9660 stream mode.
struct Reader {
    uint32_t lba = 0;
    uint32_t size = 0;
    uint8_t* buf = nullptr;      // 32-byte aligned, kReadBlock
    uint32_t bufStart = 0;       // file offset of buf[0] (sector aligned)
    uint32_t bufLen = 0;
    uint32_t pos = 0;            // logical read position
    bool     open = false;

    bool Open(const char* path)
    {
        Close();
        if (dc_iso_locate(path, &lba, &size) != 0) return false;
        buf = (uint8_t*)memalign(32, kReadBlock);
        if (buf == nullptr) return false;
        bufStart = bufLen = pos = 0;
        open = true;
        return true;
    }

    void Close()
    {
        free(buf);
        buf = nullptr;
        lba = size = bufStart = bufLen = pos = 0;
        open = false;
    }

    bool Fill()
    {
        const uint32_t start = pos & ~2047u;
        uint32_t n = size - start;
        if (n > kReadBlock) n = kReadBlock;
        // Read whole sectors; the last one may run past the file end.
        const uint32_t whole = (n + 2047) & ~2047u;
        if (dc_iso_read(lba, start, whole <= kReadBlock ? whole : kReadBlock, buf) != 0) return false;
        bufStart = start;
        bufLen = n;
        return true;
    }

    bool Read(void* dst, uint32_t len)
    {
        uint8_t* out = (uint8_t*)dst;
        while (len > 0) {
            if (pos >= size) return false;
            if (pos < bufStart || pos >= bufStart + bufLen) {
                if (!Fill()) return false;
            }
            uint32_t n = bufStart + bufLen - pos;
            if (n > len) n = len;
            if (out) { memcpy(out, buf + (pos - bufStart), n); out += n; }
            pos += n; len -= n;
        }
        return true;
    }

    void Seek(uint32_t p) { pos = p; }
};

// ===========================================================================
// Cinepak
// ===========================================================================

struct CvidStrip {
    uint32_t v4[256][4];         // RGBA pixels: TL, TR, BL, BR
    uint32_t v1[256][4];
};

struct Cinepak {
    static const int kMaxStrips = 8;   // the RE1 movies use 1-3
    int        w = 0, h = 0;      // picture size
    int        h4 = 0;            // height padded to 4 (buffer rows)
    uint32_t*  frame = nullptr;   // RGBA, w * h4
    CvidStrip* strips = nullptr;  // kMaxStrips, per-strip codebooks persist across frames

    bool Init(int width, int height)
    {
        w = width; h = height; h4 = (height + 3) & ~3;
        frame = (uint32_t*)malloc((size_t)w * h4 * 4);
        strips = (CvidStrip*)calloc(kMaxStrips, sizeof(CvidStrip));
        if (frame == nullptr || strips == nullptr) { Free(); return false; }
        for (int i = 0; i < w * h4; ++i) frame[i] = 0xFF000000u;
        return true;
    }
    void Reset()
    {
        if (strips) memset(strips, 0, kMaxStrips * sizeof(CvidStrip));
        if (frame) for (int i = 0; i < w * h4; ++i) frame[i] = 0xFF000000u;
    }
    void Free() { free(frame); frame = nullptr; free(strips); strips = nullptr; }

    static inline uint8_t Clip(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
    static inline uint32_t Px(int r, int g, int b) { return 0xFF000000u | ((uint32_t)Clip(b) << 16) | ((uint32_t)Clip(g) << 8) | Clip(r); }

    static void Codebook(uint32_t (*cb)[4], int id, const uint8_t* d, int size)
    {
        const uint8_t* end = d + size;
        const int n = (id & 0x04) ? 4 : 6;          // 4 = greyscale entries
        uint32_t flag = 0, mask = 0;
        for (int i = 0; i < 256; ++i) {
            if ((id & 0x01) && !(mask >>= 1)) {      // partial update: 32 flags
                if (d + 4 > end) break;
                flag = RB32(d); d += 4; mask = 0x80000000u;
            }
            if (!(id & 0x01) || (flag & mask)) {
                if (d + n > end) break;
                const int y0 = d[0], y1 = d[1], y2 = d[2], y3 = d[3];
                if (n == 6) {
                    const int u = (int8_t)d[4], v = (int8_t)d[5];
                    const int dr = v * 2, dg = -(u / 2) - v, db = u * 2;
                    cb[i][0] = Px(y0 + dr, y0 + dg, y0 + db);
                    cb[i][1] = Px(y1 + dr, y1 + dg, y1 + db);
                    cb[i][2] = Px(y2 + dr, y2 + dg, y2 + db);
                    cb[i][3] = Px(y3 + dr, y3 + dg, y3 + db);
                } else {
                    cb[i][0] = Px(y0, y0, y0); cb[i][1] = Px(y1, y1, y1);
                    cb[i][2] = Px(y2, y2, y2); cb[i][3] = Px(y3, y3, y3);
                }
                d += n;
            }
        }
    }

    void Vectors(CvidStrip& s, int id, const uint8_t* d, int size, int y1, int y2)
    {
        const uint8_t* end = d + size;
        uint32_t flag = 0, mask = 0;
        for (int y = y1; y < y2 && y + 4 <= h4; y += 4) {
            uint32_t* r0 = frame + (size_t)y * w;
            uint32_t* r1 = r0 + w;
            uint32_t* r2 = r1 + w;
            uint32_t* r3 = r2 + w;
            for (int x = 0; x + 4 <= w; x += 4) {
                if ((id & 0x01) && !(mask >>= 1)) {          // inter: coded?
                    if (d + 4 > end) return;
                    flag = RB32(d); d += 4; mask = 0x80000000u;
                }
                if (!(id & 0x01) || (flag & mask)) {
                    if (!(id & 0x02) && !(mask >>= 1)) {     // V1 or V4?
                        if (d + 4 > end) return;
                        flag = RB32(d); d += 4; mask = 0x80000000u;
                    }
                    if ((id & 0x02) || (~flag & mask)) {     // V1: one entry, 2x2 each
                        if (d >= end) return;
                        const uint32_t* p = s.v1[*d++];
                        r0[x] = r0[x + 1] = r1[x] = r1[x + 1] = p[0];
                        r0[x + 2] = r0[x + 3] = r1[x + 2] = r1[x + 3] = p[1];
                        r2[x] = r2[x + 1] = r3[x] = r3[x + 1] = p[2];
                        r2[x + 2] = r2[x + 3] = r3[x + 2] = r3[x + 3] = p[3];
                    } else {                                 // V4: four entries
                        if (d + 4 > end) return;
                        const uint32_t* c0 = s.v4[d[0]];
                        const uint32_t* c1 = s.v4[d[1]];
                        const uint32_t* c2 = s.v4[d[2]];
                        const uint32_t* c3 = s.v4[d[3]];
                        d += 4;
                        r0[x] = c0[0]; r0[x + 1] = c0[1]; r1[x] = c0[2]; r1[x + 1] = c0[3];
                        r0[x + 2] = c1[0]; r0[x + 3] = c1[1]; r1[x + 2] = c1[2]; r1[x + 3] = c1[3];
                        r2[x] = c2[0]; r2[x + 1] = c2[1]; r3[x] = c2[2]; r3[x + 1] = c2[3];
                        r2[x + 2] = c3[0]; r2[x + 3] = c3[1]; r3[x + 2] = c3[2]; r3[x + 3] = c3[3];
                    }
                }
            }
        }
    }

    bool Decode(const uint8_t* d, uint32_t size)
    {
        if (size < 10 || frame == nullptr) return false;
        const uint8_t* end = d + size;
        const int flags = d[0];
        int strips = RB16(d + 8);
        if (strips > kMaxStrips) strips = kMaxStrips;
        d += 10;
        int y0 = 0;
        for (int i = 0; i < strips; ++i) {
            if (d + 12 > end) break;
            int sy1 = RB16(d + 4), sy2 = RB16(d + 8);
            if (sy1 == 0) { sy1 = y0; sy2 = y0 + sy2; }   // 0 = relative to the previous strip
            int stripSize = (int)RB24(d + 1) - 12;
            d += 12;
            if (stripSize < 0) break;
            if (d + stripSize > end) stripSize = (int)(end - d);
            if (i > 0 && !(flags & 0x01)) strips_copy(i);
            if (sy2 > h4) sy2 = h4;
            Strip(strips_ref(i), d, stripSize, sy1, sy2);
            d += stripSize;
            y0 = sy2;
        }
        return true;
    }

    CvidStrip& strips_ref(int i) { return strips[i]; }
    void strips_copy(int i)
    {
        memcpy(strips[i].v4, strips[i - 1].v4, sizeof(strips[i].v4));
        memcpy(strips[i].v1, strips[i - 1].v1, sizeof(strips[i].v1));
    }

    void Strip(CvidStrip& s, const uint8_t* d, int size, int y1, int y2)
    {
        const uint8_t* end = d + size;
        while (d + 4 <= end) {
            const int id = d[0];
            int csize = (int)RB24(d + 1) - 4;
            d += 4;
            if (csize < 0) return;
            if (d + csize > end) csize = (int)(end - d);
            switch (id) {
            case 0x20: case 0x21: case 0x24: case 0x25: Codebook(s.v4, id, d, csize); break;
            case 0x22: case 0x23: case 0x26: case 0x27: Codebook(s.v1, id, d, csize); break;
            case 0x30: case 0x31: case 0x32: Vectors(s, id, d, csize, y1, y2); return;
            default: break;
            }
            d += csize;
        }
    }
};

// ===========================================================================
// Audio decoders -> S16 stereo
// ===========================================================================

const int kImaStep[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
const int kImaIndex[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

const int kMsAdapt[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };
const int kMsCoef1[7] = { 256, 512, 0, 192, 240, 460, 392 };
const int kMsCoef2[7] = { 0, -256, 0, 64, 0, -208, -232 };

inline int16_t Sat16(int v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

struct ImaState { int pred, index; };
inline int16_t ImaNibble(ImaState& st, int nib)
{
    const int step = kImaStep[st.index];
    int diff = step >> 3;
    if (nib & 1) diff += step >> 2;
    if (nib & 2) diff += step >> 1;
    if (nib & 4) diff += step;
    if (nib & 8) st.pred -= diff; else st.pred += diff;
    if (st.pred > 32767) st.pred = 32767;
    if (st.pred < -32768) st.pred = -32768;
    st.index += kImaIndex[nib];
    if (st.index < 0) st.index = 0;
    if (st.index > 88) st.index = 88;
    return (int16_t)st.pred;
}

// ===========================================================================
// Player state
// ===========================================================================

PlatVideoOverlayCallback s_overlayCallback = nullptr;

Reader  s_rd;
Cinepak s_cv;

bool     s_open = false;
double   s_fps = 10.0;
int      s_vIndex = -1, s_aIndex = -1;    // AVI stream numbers
uint32_t s_moviStart = 0, s_moviEnd = 0;

// audio format
int s_aFmt = 0, s_aCh = 0, s_aRate = 0, s_aBits = 0, s_aBlock = 0, s_aSpb = 0;
int s_msCoef1[16], s_msCoef2[16], s_msNumCoef = 7;
bool s_useAudio = false;

// demux position (absolute file offsets)
uint32_t s_chunkPos = 0;
bool     s_demuxEof = false;
int      s_videoChunks = 0;      // video chunks demuxed = next frame number
uint64_t s_audioSamples = 0;     // source frames decoded so far (whole movie)
uint64_t s_audioSkipTo = 0;      // drop decoded audio before this sample

// packet queue (compressed video)
struct VPkt { uint8_t* data; uint32_t size; int frame; };
const int kQueue = 24;
VPkt s_q[kQueue];
int  s_qHead = 0, s_qCount = 0;

// decoded audio waiting for ring space
int16_t* s_pend = nullptr;
int      s_pendFrames = 0, s_pendCap = 0;

// playback
MarniHandle s_tex = MARNI_NULL_HANDLE;
int  s_curFrame = -1;            // frame now in s_cv.frame
int  s_baseFrame = 0;            // segment start
int  s_playToFrame = 0;
bool s_active = false;
bool s_endEvent = false;
DWORD s_startMs = 0;

void QueueClear()
{
    for (int i = 0; i < s_qCount; ++i) free(s_q[(s_qHead + i) % kQueue].data);
    s_qHead = s_qCount = 0;
}

void FreeAll()
{
    if (s_useAudio) dc_movie_audio_end();
    s_useAudio = false;
    QueueClear();
    free(s_pend); s_pend = nullptr; s_pendFrames = s_pendCap = 0;
    if (s_tex != MARNI_NULL_HANDLE) {
        MarniDX* dx = Marni_DX();
        if (dx != nullptr) dx->DestroyTexture(s_tex);
        s_tex = MARNI_NULL_HANDLE;
    }
    s_cv.Free();
    s_rd.Close();
    s_open = false;
    s_active = false;
    s_endEvent = false;
    s_curFrame = -1;
}

bool PendReserve(int frames)
{
    if (s_pendFrames + frames <= s_pendCap) return true;
    int cap = s_pendCap ? s_pendCap : 4096;
    while (cap < s_pendFrames + frames) cap *= 2;
    int16_t* n = (int16_t*)realloc(s_pend, (size_t)cap * 4);
    if (n == nullptr) return false;
    s_pend = n; s_pendCap = cap;
    return true;
}

inline void PendPut(int16_t l, int16_t r)
{
    // Called once per decoded source frame; drops what precedes a seek target.
    if (s_audioSamples++ < s_audioSkipTo) return;
    s_pend[s_pendFrames * 2 + 0] = l;
    s_pend[s_pendFrames * 2 + 1] = r;
    s_pendFrames++;
}

void FlushPending()
{
    if (s_pendFrames == 0) return;
    const int n = dc_movie_audio_push(s_pend, s_pendFrames);
    if (n > 0) {
        memmove(s_pend, s_pend + n * 2, (size_t)(s_pendFrames - n) * 4);
        s_pendFrames -= n;
    }
}

void DecodeAudioChunk(const uint8_t* d, uint32_t size)
{
    if (!s_useAudio) return;
    if (s_aFmt == 1) {                                   // PCM
        const int bps = (s_aBits / 8) * s_aCh;
        const int frames = bps ? (int)(size / (uint32_t)bps) : 0;
        if (!PendReserve(frames)) return;
        for (int i = 0; i < frames; ++i) {
            int16_t l, r;
            if (s_aBits == 8) {
                l = (int16_t)(((int)d[0] - 128) << 8);
                r = (s_aCh == 2) ? (int16_t)(((int)d[1] - 128) << 8) : l;
            } else {
                l = (int16_t)RL16(d);
                r = (s_aCh == 2) ? (int16_t)RL16(d + 2) : l;
            }
            d += bps;
            PendPut(l, r);
        }
        return;
    }
    if (s_aBlock <= 0) return;
    const uint8_t* end = d + size;
    for (; d + s_aBlock <= end; d += s_aBlock) {
        const uint8_t* b = d;
        if (s_aFmt == 0x11) {                            // IMA ADPCM
            if (!PendReserve(s_aSpb)) return;
            ImaState st[2];
            for (int c = 0; c < s_aCh; ++c) {
                st[c].pred = (int16_t)RL16(b + c * 4);
                st[c].index = b[c * 4 + 2] > 88 ? 88 : b[c * 4 + 2];
            }
            PendPut((int16_t)st[0].pred, (int16_t)st[s_aCh - 1].pred);
            const uint8_t* p = b + 4 * s_aCh;
            if (s_aCh == 1) {
                for (int i = 1; i < s_aSpb && p < d + s_aBlock; i += 2, ++p) {
                    const int16_t a = ImaNibble(st[0], *p & 15);
                    PendPut(a, a);
                    if (i + 1 < s_aSpb) { const int16_t bb = ImaNibble(st[0], *p >> 4); PendPut(bb, bb); }
                }
            } else {
                // 8 samples per channel per 4-byte group, channels alternate.
                int16_t L[8], R[8];
                for (int i = 1; i < s_aSpb && p + 8 <= d + s_aBlock; i += 8, p += 8) {
                    for (int k = 0; k < 4; ++k) {
                        L[k * 2] = ImaNibble(st[0], p[k] & 15);
                        L[k * 2 + 1] = ImaNibble(st[0], p[k] >> 4);
                        R[k * 2] = ImaNibble(st[1], p[4 + k] & 15);
                        R[k * 2 + 1] = ImaNibble(st[1], p[4 + k] >> 4);
                    }
                    for (int k = 0; k < 8 && i + k < s_aSpb; ++k) PendPut(L[k], R[k]);
                }
            }
        } else if (s_aFmt == 0x02) {                     // MS ADPCM
            if (!PendReserve(s_aSpb)) return;
            const int ch = s_aCh;
            int pi[2], delta[2], s1[2], s2[2];
            const uint8_t* p = b;
            for (int c = 0; c < ch; ++c) { pi[c] = *p++; if (pi[c] >= s_msNumCoef) pi[c] = 0; }
            for (int c = 0; c < ch; ++c) { delta[c] = (int16_t)RL16(p); p += 2; }
            for (int c = 0; c < ch; ++c) { s1[c] = (int16_t)RL16(p); p += 2; }
            for (int c = 0; c < ch; ++c) { s2[c] = (int16_t)RL16(p); p += 2; }
            PendPut((int16_t)s2[0], (int16_t)s2[ch - 1]);
            PendPut((int16_t)s1[0], (int16_t)s1[ch - 1]);
            int produced = 2;
            int out[2] = { 0, 0 };
            int c = 0;
            const uint8_t* bend = d + s_aBlock;
            while (produced < s_aSpb && p < bend) {
                for (int half = 0; half < 2; ++half) {
                    int nib = half == 0 ? (*p >> 4) : (*p & 15);
                    const int snib = nib >= 8 ? nib - 16 : nib;
                    int pred = (s1[c] * s_msCoef1[pi[c]] + s2[c] * s_msCoef2[pi[c]]) >> 8;
                    pred += snib * delta[c];
                    const int16_t v = Sat16(pred);
                    s2[c] = s1[c]; s1[c] = v;
                    delta[c] = (kMsAdapt[nib] * delta[c]) >> 8;
                    if (delta[c] < 16) delta[c] = 16;
                    out[c] = v;
                    if (++c == ch) {
                        c = 0;
                        if (produced < s_aSpb) PendPut((int16_t)out[0], (int16_t)out[ch - 1]);
                        produced++;
                    }
                }
                ++p;
            }
        } else {
            return;
        }
    }
}

// Read chunks until the audio ring has ~0.6 s and the video queue covers
// `needFrame`, or a per-tick byte budget runs out (keeps ticks short).
void Pump(int needFrame, bool preroll = false)
{
    uint32_t budget = 192 * 1024;
    while (!s_demuxEof && budget > 0) {
        FlushPending();
        const bool audioLow = s_useAudio &&
            (dc_movie_audio_buffered() + s_pendFrames) < (s_aRate * 6) / 10;
        const int lastQueued = s_qCount ? s_q[(s_qHead + s_qCount - 1) % kQueue].frame : s_curFrame;
        const bool videoLow = lastQueued < needFrame + 1;
        if (!audioLow && !videoLow) break;
        if (s_qCount >= kQueue) break;
        if (!preroll && s_pendFrames > s_aRate) break;   // ring is not draining

        if (s_chunkPos + 8 > s_moviEnd) { s_demuxEof = true; break; }
        uint8_t hdr[8];
        s_rd.Seek(s_chunkPos);
        if (!s_rd.Read(hdr, 8)) { s_demuxEof = true; break; }
        const uint32_t id = RL32(hdr), sz = RL32(hdr + 4);
        if (id == FCC("LIST")) { s_chunkPos += 12; continue; }   // 'rec ' groups: descend
        const uint32_t next = s_chunkPos + 8 + sz + (sz & 1);
        const int stream = (hdr[0] - '0') * 10 + (hdr[1] - '0');
        const bool isVideo = stream == s_vIndex && (hdr[2] == 'd');
        const bool isAudio = stream == s_aIndex && hdr[2] == 'w' && hdr[3] == 'b';
        if (isVideo) {
            VPkt& v = s_q[(s_qHead + s_qCount) % kQueue];
            v.size = sz;
            v.frame = s_videoChunks++;
            v.data = sz ? (uint8_t*)malloc(sz) : nullptr;
            if (sz && (v.data == nullptr || !s_rd.Read(v.data, sz))) { free(v.data); s_demuxEof = true; break; }
            s_qCount++;
            budget = budget > sz ? budget - sz : 0;
        } else if (isAudio && s_useAudio && sz > 0) {
            uint8_t* tmp = (uint8_t*)malloc(sz);
            if (tmp == nullptr || !s_rd.Read(tmp, sz)) { free(tmp); s_demuxEof = true; break; }
            DecodeAudioChunk(tmp, sz);
            free(tmp);
            budget = budget > sz ? budget - sz : 0;
        }
        s_chunkPos = next;
    }
    FlushPending();
}

bool ParseHeaders()
{
    uint8_t h[12];
    s_rd.Seek(0);
    if (!s_rd.Read(h, 12) || RL32(h) != FCC("RIFF") || RL32(h + 8) != FCC("AVI ")) return false;
    uint32_t pos = 12;
    const uint32_t riffEnd = 8 + RL32(h + 4) < s_rd.size ? 8 + RL32(h + 4) : s_rd.size;
    int streamNo = -1;
    uint32_t vScale = 0, vRate = 0;
    int curType = 0;                       // 1 video, 2 audio
    while (pos + 8 <= riffEnd) {
        uint8_t c[8];
        s_rd.Seek(pos);
        if (!s_rd.Read(c, 8)) return false;
        const uint32_t id = RL32(c), sz = RL32(c + 4);
        if (id == FCC("LIST")) {
            uint8_t t[4];
            if (!s_rd.Read(t, 4)) return false;
            if (RL32(t) == FCC("movi")) {
                s_moviStart = pos + 12;
                s_moviEnd = pos + 8 + sz < s_rd.size ? pos + 8 + sz : s_rd.size;
                break;
            }
            if (RL32(t) == FCC("strl")) { streamNo++; curType = 0; }
            pos += 12;                     // descend into hdrl / strl
            continue;
        }
        uint8_t body[64];
        const uint32_t take = sz < sizeof(body) ? sz : sizeof(body);
        if (!s_rd.Read(body, take)) return false;
        if (id == FCC("strh") && take >= 32) {
            if (RL32(body) == FCC("vids")) { curType = 1; vScale = RL32(body + 20); vRate = RL32(body + 24); if (s_vIndex < 0) s_vIndex = streamNo; }
            else if (RL32(body) == FCC("auds")) { curType = 2; if (s_aIndex < 0) s_aIndex = streamNo; }
            else curType = 0;
        } else if (id == FCC("strf")) {
            if (curType == 1 && streamNo == s_vIndex && take >= 20) {
                const int w = (int)RL32(body + 4);
                int hh = (int)RL32(body + 8);
                if (hh < 0) hh = -hh;
                const uint32_t comp = RL32(body + 16);
                if (comp != FCC("cvid") && comp != FCC("CVID")) {
                    printf("[VIDEO] unsupported video codec %.4s\n", (const char*)(body + 16));
                    return false;
                }
                if (!s_cv.Init(w, hh)) return false;
            } else if (curType == 2 && streamNo == s_aIndex && take >= 16) {
                s_aFmt = RL16(body); s_aCh = RL16(body + 2); s_aRate = (int)RL32(body + 4);
                s_aBlock = RL16(body + 12); s_aBits = RL16(body + 14);
                if (take >= 20) s_aSpb = RL16(body + 18);
                if (s_aFmt == 0x02 && take >= 22) {
                    s_msNumCoef = RL16(body + 20);
                    if (s_msNumCoef > 16) s_msNumCoef = 16;
                    for (int i = 0; i < s_msNumCoef && 22 + i * 4 + 4 <= (int)take; ++i) {
                        s_msCoef1[i] = (int16_t)RL16(body + 22 + i * 4);
                        s_msCoef2[i] = (int16_t)RL16(body + 24 + i * 4);
                    }
                } else if (s_aFmt == 0x02) {
                    for (int i = 0; i < 7; ++i) { s_msCoef1[i] = kMsCoef1[i]; s_msCoef2[i] = kMsCoef2[i]; }
                }
            }
        }
        pos += 8 + sz + (sz & 1);
    }
    if (s_vIndex < 0 || s_cv.frame == nullptr || s_moviStart == 0) return false;
    if (vScale > 0 && vRate > 0) s_fps = (double)vRate / (double)vScale;
    if (s_fps <= 0.0) s_fps = 10.0;

    const bool pcm = s_aFmt == 1 && (s_aBits == 8 || s_aBits == 16);
    const bool adpcm = (s_aFmt == 0x11 || s_aFmt == 0x02) && s_aBits == 4 && s_aBlock > 0;
    if (s_aIndex >= 0 && s_aCh >= 1 && s_aCh <= 2 && s_aRate > 0 && (pcm || adpcm)) {
        if (s_aFmt == 0x11 && s_aSpb <= 0) s_aSpb = (s_aBlock - 4 * s_aCh) * 8 / (4 * s_aCh) + 1;
        if (s_aFmt == 0x02 && s_aSpb <= 0) s_aSpb = (s_aBlock - 7 * s_aCh) * 2 / s_aCh + 2;
        s_useAudio = true;
    } else if (s_aIndex >= 0) {
        printf("[VIDEO] audio format 0x%x (%d-bit, %d ch) not supported - silent\n", s_aFmt, s_aBits, s_aCh);
    }
    return true;
}

// Restart demuxing at the beginning (decoder state reset with it).
void Rewind()
{
    QueueClear();
    s_chunkPos = s_moviStart;
    s_demuxEof = false;
    s_videoChunks = 0;
    s_audioSamples = 0;
    s_pendFrames = 0;
    s_curFrame = -1;
    s_cv.Reset();
}

void StartSegment(int fromFrame)
{
    if (fromFrame < 0) fromFrame = 0;
    // Always restart from the top: Cinepak inter frames need every earlier
    // frame, and audio demuxed ahead of an earlier cut was thrown away with
    // the ring. Reading is cheap (DMA stream); decoding up to the resume point
    // costs a few ms per frame, once.
    if (s_useAudio) dc_movie_audio_end();
    Rewind();
    s_audioSkipTo = s_useAudio ? (uint64_t)((double)fromFrame / s_fps * s_aRate) : 0;

    // Pre-roll: decode up to the frame before the segment. The ring is off,
    // so no sound starts ahead of the picture; audio from fromFrame on waits
    // in the pending buffer.
    while (s_curFrame < fromFrame - 1) {
        Pump(fromFrame, true);
        if (s_qCount == 0) break;
        VPkt& v = s_q[s_qHead];
        if (v.size) s_cv.Decode(v.data, v.size);
        s_curFrame = v.frame;
        free(v.data);
        s_qHead = (s_qHead + 1) % kQueue; s_qCount--;
    }

    // No audio stream (silent build, AICA init failed): clock on the timer.
    if (s_useAudio && !dc_movie_audio_begin(s_aRate, s_aRate + s_aRate / 2)) s_useAudio = false;
    FlushPending();
    s_baseFrame = fromFrame;
    s_startMs = plat_time_ms();
}

void Present()
{
    MarniDX* dx = Marni_DX();
    if (dx == nullptr || s_cv.frame == nullptr || s_curFrame < 0) return;
    if (s_tex == MARNI_NULL_HANDLE) {
        s_tex = dx->CreateTexture(s_cv.w, s_cv.h, 32, s_cv.frame, nullptr, nullptr);
        if (s_tex == MARNI_NULL_HANDLE) return;
    }
    DWORD bw = 0, bh = 0;
    dx->GetBackBufferSize(&bw, &bh);
    dx->Clear(0.0f, 0.0f, 0.0f, 1.0f);
    const int frameMs = s_curFrame > 0 ? (int)((double)s_curFrame * 1000.0 / s_fps + 0.5) : 0;
    if (s_overlayCallback == nullptr || !s_overlayCallback(s_tex, frameMs)) {
        dx->DrawSprite(0.0f, 0.0f, (float)bw, (float)bh, 0.0f, 0.0f, 1.0f, 1.0f,
                       0xFFFFFFFFu, s_tex, MARNI_SAMPLER_LINEAR, MARNI_BLEND_DISABLE);
    }
    dx->Present();
}

}  // namespace

// ===========================================================================
// Backend entry points (see platform.h)
// ===========================================================================

void plat_video_set_overlay_callback(PlatVideoOverlayCallback callback) { s_overlayCallback = callback; }

BOOL plat_video_init(void) { return TRUE; }

BOOL plat_video_open_and_play(const char* path, int playToMs)
{
    FreeAll();
    s_vIndex = s_aIndex = -1;
    s_aFmt = s_aCh = s_aRate = s_aBits = s_aBlock = s_aSpb = 0;
    s_moviStart = s_moviEnd = 0;
    s_fps = 10.0;

    char norm[512];
    const char* p = plat_normalize_path(path, norm, sizeof(norm));
    if (!s_rd.Open(p)) {      // /cd only (dc_iso)
        printf("[VIDEO] cannot open %s\n", p);
        FreeAll();
        return FALSE;
    }
    if (!ParseHeaders()) {
        printf("[VIDEO] not a Cinepak AVI: %s\n", p);
        FreeAll();
        return FALSE;
    }
    s_open = true;
    s_playToFrame = playToMs > 0 ? (int)((double)playToMs / 1000.0 * s_fps + 0.5) : 0;
    s_active = true;
    s_endEvent = false;
    StartSegment(0);
    printf("[VIDEO] playing %s (%dx%d, %.2f fps, audio %s%s)\n", p, s_cv.w, s_cv.h, s_fps,
           s_useAudio ? "fmt " : "off", s_useAudio ? (s_aFmt == 1 ? "PCM" : s_aFmt == 0x11 ? "IMA" : "MS-ADPCM") : "");
    return TRUE;
}

// MERGE140: length of a movie without playing it. VideoPlayback.cpp picks the
// prologue's Jill cut from it (the DC / censored prologue is ~16 s shorter
// than the PC one); 0 would mean "PC cut". Read from the AVI main header:
// dwMicroSecPerFrame * dwTotalFrames, in the first sectors of the file.
int plat_video_probe_duration_ms(const char* path)
{
    char norm[512];
    const char* p = plat_normalize_path(path, norm, sizeof(norm));
    uint32_t lba = 0, size = 0;
    if (dc_iso_locate(p, &lba, &size) != 0 || size < 64) return 0;
    const uint32_t len = 4096;
    uint8_t* buf = (uint8_t*)memalign(32, len);
    if (buf == nullptr) return 0;
    int ms = 0;
    if (dc_iso_read(lba, 0, len, buf) == 0 && RL32(buf) == FCC("RIFF") && RL32(buf + 8) == FCC("AVI ")) {
        const uint32_t end = (size < len ? size : len) - 8 - 20;
        for (uint32_t i = 12; i < end; i += 2) {
            if (RL32(buf + i) != FCC("avih")) continue;
            const uint32_t usPerFrame = RL32(buf + i + 8);
            const uint32_t frames     = RL32(buf + i + 8 + 16);
            ms = (int)(((uint64_t)usPerFrame * frames) / 1000u);
            break;
        }
    }
    free(buf);
    printf("[VIDEO] %s: %d ms\n", p, ms);
    return ms;
}

void plat_video_play_from(int fromMs)
{
    if (!s_open) return;
    s_playToFrame = 0;
    s_endEvent = false;
    s_active = true;
    StartSegment((int)((double)fromMs / 1000.0 * s_fps + 0.5));
    Present();
}

void plat_video_stop(void) { s_active = false; if (s_useAudio) dc_movie_audio_end(); }
void plat_video_close(void) { FreeAll(); }
BOOL plat_video_is_active(void) { return s_active ? TRUE : FALSE; }

BOOL plat_video_take_end_event(void)
{
    if (!s_endEvent) return FALSE;
    s_endEvent = false;
    return TRUE;
}

void plat_video_tick(void)
{
    if (!s_active || !s_open) return;

    double seconds = (double)(plat_time_ms() - s_startMs) / 1000.0;
    if (s_useAudio) seconds = (double)dc_movie_audio_played() / (double)s_aRate;
    const int target = s_baseFrame + (int)(seconds * s_fps);

    Pump(target);

    bool decoded = false;
    while (s_qCount > 0 && s_q[s_qHead].frame <= target) {
        VPkt& v = s_q[s_qHead];
        if (v.size) s_cv.Decode(v.data, v.size);
        s_curFrame = v.frame;
        decoded = true;
        free(v.data);
        s_qHead = (s_qHead + 1) % kQueue; s_qCount--;
        if (s_playToFrame > 0 && s_curFrame >= s_playToFrame) break;
    }
    if (decoded && s_tex != MARNI_NULL_HANDLE) {
        MarniDX* dx = Marni_DX();
        if (dx != nullptr) dx->UpdateTexturePixels(s_tex, s_cv.frame, s_cv.w, s_cv.h, 32);
    }
    Present();

    if (s_playToFrame > 0 && s_curFrame >= s_playToFrame) {
        s_active = false;
        s_endEvent = true;
        if (s_useAudio) dc_movie_audio_end();
        return;
    }
    const bool audioDone = !s_useAudio || (dc_movie_audio_buffered() == 0 && s_pendFrames == 0);
    if (s_demuxEof && s_qCount == 0 && audioDone) {
        s_active = false;
        s_endEvent = true;
        if (s_useAudio) dc_movie_audio_end();
    }
}

void plat_video_set_window_mode(BOOL forVideo) { (void)forVideo; }

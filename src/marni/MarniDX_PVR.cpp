// MarniDX_PVR.cpp - PowerVR2 (Dreamcast / KallistiOS) MarniDX backend.
//
// Same contract as MarniDX_GL.cpp (see MarniDX.h); differences forced by the
// hardware are noted where they happen.
//
// Submission model
//   The game relies on draw ORDER (backgrounds, sprites, masks, UI overlays)
//   and enables depth only for its 3D paths. The PVR renders its opaque list
//   before its translucent list regardless of submission order, so everything
//   goes into ONE list: translucent, presort (autosort disabled). Polygons are
//   then rasterised in exactly the order MarniDX received them - the GL
//   behaviour. "No blending" is expressed as ONE/ZERO inside that list.
//   Vertices stream straight to the TA (direct mode, no DMA).
//
// Depth
//   The PVR compares 1/w-style values, larger = nearer. 3D vertices carry the
//   view-space w, so z_pvr = kZScale / w gives both the depth test (GEQUAL)
//   and perspective-correct UVs. 2D paths use ALWAYS and a constant z at the
//   very back (kZ2D): the presort TR list writes depth even for them.
//   The background plane sits at the KOS zclip (0.0001): z_pvr is clamped
//   above it so far geometry stays visible.
//
// Textures
//   RGBA8/BGR24 input is converted to 16 bpp: RGB565 when fully opaque,
//   ARGB1555 when alpha is 0/255 only, ARGB4444 otherwise. Power-of-two
//   sizes are twiddled. Non-power-of-two textures whose width is a multiple
//   of 32 use the PVR's single global stride when they match it (the first
//   such width seen claims it, typically 320 for backgrounds); everything
//   else is padded up to a power of two with edge texels replicated, and UVs
//   are scaled by w/pw, h/ph at draw time.
#include "MarniDX.h"

#include <kos.h>
#include <dc/pvr.h>

#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../game/Types.h"
#include "../platform/dreamcast/dc_platform.h"

#define MARNI_MAX_TEXTURES 2048

// Bring-up switches (Makefile: RE1_PVR_DEBUG=<bits>):
//   1 - 3D paths ignore depth (ALWAYS, no write)
//   2 - 3D paths draw untextured opaque magenta (ignores texture, colour, alpha)
#ifndef RE1_DC_VERBOSE
#define RE1_DC_VERBOSE 0
#endif
#ifndef RE1_PVR_DEBUG
#define RE1_PVR_DEBUG 0
#endif

// Set by create_texture_page (game code) right before its texture is created
// on the scheduler task, where the real caller is no longer on the stack.
// Defined in dc_iso.cpp (always linked, also with the null renderer).
extern "C" void* volatile g_dcPageCaller;
// BG_SMOOTH_FIX: bilinear backgrounds/movies/room masks (0 = PS1-sharp).
#ifndef RE1_DC_SMOOTH
#define RE1_DC_SMOOTH 1
#endif
extern "C" volatile int g_dcSmoothNext;   // platform.cpp; set around room-mask loads
// The game's two texture tables (Globals.cpp). The census lists which of their
// entries still hold each texture, so a refcount with no matching owners shows
// up as a leak and names the table entry that was dropped.
extern DWORD       g_TexturePageTable_DAT[256];
extern MarniHandle g_TexturePageSRV[256];
extern "C" void dc_marni_texture_freed(unsigned int handle);   // MarniSystem.cpp

namespace {

const float kZScale  = 16.0f;      // z_pvr = kZScale / w
const float kZMin    = 0.00012f;   // just above the background plane (0.0001)
// Constant depth for the 2D paths (compare ALWAYS, no depth write). Kept at
// the very back - just above the background plane, below every 3D z - so a
// 2D polygon can never occlude a model even if its depth were written. (It
// once was: the depth-write flag was inverted, see SendHeader.)
const float kZ2D     = 0.00011f;
// Measured on the console: the busiest frames send ~125 KB to the TA. 384 KB
// keeps the per-frame vertex budget (kVertexBudget) at ~2x that peak and gives
// 256 KB of the two TA buffers back to textures.
const int   kVertexBufBytes = 384 * 1024;
// Conservative per-frame vertex budget: the TA stores a little more than the
// 32 bytes we send per vertex, plus strip headers.
const int   kVertexBudget = kVertexBufBytes / 48;

struct TexSlot {
    bool      used = false;
    int       w = 0, h = 0;        // logical size (what the game sees)
    int       pw = 0, ph = 0;      // PVR declared size (power of two)
    int       fmt = 0;             // PVR_TXRFMT_* incl. twiddle/stride bits
    bool      strided = false;
    bool      smooth = false;          // drawn bilinear (BG_SMOOTH_FIX)
    pvr_ptr_t mem = NULL;
    size_t    bytes = 0;
    float     us = 1.0f, vs = 1.0f; // UV scale w/pw, h/ph
    int       shift = 0;           // 1 = stored at half size (VRAM was short)
    uint32_t  born = 0;            // frame it was created (census)
    int       refs = 1;            // handles given out for this content (dedup)
    uint64_t  hash = 0;            // content hash, 0 = not shareable (strided)
    uint32_t  site[4] = {};        // return-address-looking words from the creator's stack
    char      from[16] = {};       // last file read when it was created
};

// Bytes sent to the TA this frame / peak (headers and vertices are 32 bytes
// each). KOS only fills pvr_stats' vertex-buffer fields in DMA mode.
static uint32_t g_frameTaBytes = 0, g_peakTaBytes = 0;

// MERGE140: MarniDX::TextureSerial - bumped every time a slot is released.
static unsigned int s_texSerial[MARNI_MAX_TEXTURES];

extern "C" char _etext[] __attribute__((weak));
extern "C" const char* dc_last_loaded_file(void);   // dc_iso.cpp

// Census: up to four code addresses found near the top of the creator's stack
// (no frame pointers on SH-4, so this is a heuristic - good enough to feed
// sh-elf-addr2line and see which game code made the texture).
static void __attribute__((noinline)) GrabSites(uint32_t* out)
{
    const uintptr_t lo = 0x8c010000u;
    const uintptr_t hi = _etext ? (uintptr_t)_etext : 0x8c300000u;
    const uint32_t* sp = (const uint32_t*)__builtin_frame_address(0);
    int n = 0;
    for (int i = 0; i < 160 && n < 4; ++i) {
        const uint32_t w = sp[i];
        if (w >= lo && w < hi && (w & 1) == 0) out[n++] = w;
    }
}

struct DrawKey {
    MarniHandle  tex;
    MarniSampler sampler;
    MarniBlend   blend;
    bool         depthTest;
    bool         depthWrite;
    bool operator==(const DrawKey& o) const {
        return tex == o.tex && sampler == o.sampler && blend == o.blend &&
               depthTest == o.depthTest && depthWrite == o.depthWrite;
    }
};

int NextPow2(int v)
{
    int p = 8;
    while (p < v) p <<= 1;
    return p;
}

bool IsPow2(int v) { return v >= 8 && (v & (v - 1)) == 0; }

inline uint32_t PackColor(float r, float g, float b, float a)
{
    auto c = [](float f) -> uint32_t {
        if (f <= 0.0f) return 0;
        if (f >= 1.0f) return 255;
        return (uint32_t)(f * 255.0f + 0.5f);
    };
    return (c(a) << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
}

inline float DepthFromW(float w)
{
    float z = kZScale / (w > 1e-4f ? w : 1e-4f);
    return z < kZMin ? kZMin : z;
}

}  // namespace

struct MarniDX::Impl {
    int  width  = 640;
    int  height = 480;
    bool ready  = false;
    bool pvrUp  = false;

    MarniHandle whiteHandle = MARNI_NULL_HANDLE;
    MarniHandle fontHandle  = MARNI_NULL_HANDLE;
    int fontW = 0, fontH = 0;

    TexSlot slots[MARNI_MAX_TEXTURES];
    int     strideWidth = 0;        // claimed global stride (0 = free)

    // VRAM of destroyed textures. The PVR renders a scene after
    // pvr_scene_finish(), so memory a submitted polygon may still reference is
    // released only after the next pvr_wait_ready().
    enum { kMaxPending = 256 };
    pvr_ptr_t pendingFree[kMaxPending];
    int       pendingCount = 0;

    // Per-frame state.
    bool    inScene = false;
    bool    drewThisScene = false;
    bool    haveHeader = false;
    DrawKey lastKey = {};
    int     vertsThisFrame = 0;
    bool    budgetWarned = false;

    // Stats.
    unsigned frames = 0;
    unsigned periodDraws = 0, periodTris = 0, periodDropped = 0;
    int      liveTextures = 0;
    size_t   liveBytes = 0;
    int      censusMax = 0;          // live-texture count of the last census
    unsigned periodShared = 0;       // CreateTexture calls answered by an existing texture
    int      createdLogged = 0;
    unsigned periodMidClears = 0;   // Clear() after draws in the same scene
    unsigned periodTexUs = 0, periodTexCount = 0;
    uint64_t periodWaitUs = 0;      // blocked in pvr_wait_ready
    uint64_t periodSubmitUs = 0;    // first draw -> Present (CPU side of a frame)
    uint64_t sceneT0 = 0;
    unsigned period3DTris = 0;
    bool     haveSample = false;     // one 3D vertex per period, for the log
    float    sx = 0, sy = 0, sz = 0, sw = 0, spz = 0, su = 0, sv = 0;
    uint32_t sargb = 0;
    MarniHandle stex = 0;
    MarniBlend  sblend = MARNI_BLEND_ALPHA;
};

static MarniDX* g_pDX = nullptr;
extern "C" volatile int g_dcWaitPvr;   // crash.cpp: shown in the stall report

static inline void WaitPvr()
{
    g_dcWaitPvr = 1;
    DC_TRACE('P');
    pvr_wait_ready();
    DC_TRACE('p');
    g_dcWaitPvr = 0;
}
MarniDX* Marni_DX() { return g_pDX; }

// ---------------------------------------------------------------------------
// Scene management
// ---------------------------------------------------------------------------

// TA_BATCH_FIX: headers and vertices are staged here and go to the TA with
// one pvr_prim() per 8 KB instead of one per 32-byte record - each pvr_prim()
// takes and releases the store-queue mutex, which cost a third of the frame.
// Flushed whenever it fills and always before pvr_list_finish(), so the TA
// receives exactly the same bytes in the same order.
alignas(32) static uint8_t s_taBuf[8192];
static unsigned s_taUsed = 0;

static inline void TaFlush()
{
    if (s_taUsed != 0) {
        pvr_prim(s_taBuf, s_taUsed);
        s_taUsed = 0;
    }
}

static inline void* TaAlloc32()
{
    if (s_taUsed + 32 > sizeof(s_taBuf)) TaFlush();
    void* r = s_taBuf + s_taUsed;
    s_taUsed += 32;
    return r;
}

static void ReleasePending(MarniDX::Impl* p)
{
    for (int i = 0; i < p->pendingCount; ++i) pvr_mem_free(p->pendingFree[i]);
    p->pendingCount = 0;
}

// End the open scene (if any) and wait until the PVR is idle, so pending VRAM
// can be released. Used when VRAM runs out mid-load or the pending list fills.
static void DrainPvr(MarniDX::Impl* p)
{
    if (p->inScene) {
        TaFlush();
        pvr_list_finish();
        pvr_scene_finish();
        p->inScene = false;
    }
    WaitPvr();
    ReleasePending(p);
}

static void BeginSceneIfNeeded(MarniDX::Impl* p)
{
    if (p->inScene) return;
    const uint64_t w0 = timer_us_gettime64();
    WaitPvr();
    p->sceneT0 = timer_us_gettime64();
    p->periodWaitUs += p->sceneT0 - w0;
    ReleasePending(p);
    pvr_scene_begin();
    pvr_list_begin(PVR_LIST_TR_POLY);
    p->inScene = true;
    p->drewThisScene = false;
    p->haveHeader = false;
    p->vertsThisFrame = 0;
}

static void SendHeader(MarniDX::Impl* p, const DrawKey& k)
{
    if (p->haveHeader && p->lastKey == k) return;

    const TexSlot& s = p->slots[k.tex];
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, s.fmt, s.pw, s.ph, s.mem,
                     (k.sampler == MARNI_SAMPLER_POINT && !s.smooth) ? PVR_FILTER_NONE
                                                      : PVR_FILTER_BILINEAR);
    cxt.gen.culling   = PVR_CULLING_NONE;
    cxt.gen.alpha     = true;
    cxt.txr.env       = PVR_TXRENV_MODULATEALPHA;   // tex * vertex colour (GL FS)
    cxt.txr.uv_clamp  = PVR_UVCLAMP_UV;             // GL_CLAMP_TO_EDGE
    cxt.txr.alpha     = false;                      // use texture alpha

    switch (k.blend) {
    case MARNI_BLEND_ADD:
        cxt.blend.src = PVR_BLEND_ONE;
        cxt.blend.dst = PVR_BLEND_ONE;
        break;
    case MARNI_BLEND_DISABLE:
        cxt.blend.src = PVR_BLEND_ONE;
        cxt.blend.dst = PVR_BLEND_ZERO;
        break;
    case MARNI_BLEND_ADD_ALPHA:            // MERGE140: PS1 ABR 1/3 (glass, smoke)
        cxt.blend.src = PVR_BLEND_SRCALPHA;
        cxt.blend.dst = PVR_BLEND_ONE;
        break;
    case MARNI_BLEND_ALPHA:
    default:
        cxt.blend.src = PVR_BLEND_SRCALPHA;
        cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
        break;
    }

    cxt.depth.comparison = k.depthTest ? PVR_DEPTHCMP_GEQUAL : PVR_DEPTHCMP_ALWAYS;
    // KallistiOS 2.3 documents depth.write as "enable depth writes", but
    // pvr_poly_compile copies it straight into ISP bit 26, which the hardware
    // defines as Z-WRITE DISABLE (pvr_header.h: depth_write_dis; KOS's own
    // defaults use PVR_DEPTHWRITE_ENABLE = 0). Taken at its word, every
    // "test only" polygon WROTE depth - shadows clipped by the blood decal
    // under them (the square), soles cut by the shadow - and the models did
    // not write at all. Use the hardware-meaning constants.
    cxt.depth.write      = k.depthWrite ? PVR_DEPTHWRITE_ENABLE : PVR_DEPTHWRITE_DISABLE;

    pvr_poly_hdr_t hdr;
    pvr_poly_compile(&hdr, &cxt);
    memcpy(TaAlloc32(), &hdr, sizeof(hdr));
    g_frameTaBytes += sizeof(hdr);

    p->lastKey = k;
    p->haveHeader = true;
}

static inline void SendVertex(float x, float y, float z, float u, float v,
                              uint32_t argb, bool eol)
{
    pvr_vertex_t* vert = (pvr_vertex_t*)TaAlloc32();
    vert->flags = eol ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
    vert->x = x;
    vert->y = y;
    vert->z = z;
    vert->u = u;
    vert->v = v;
    vert->argb  = argb;
    vert->oargb = 0;
    g_frameTaBytes += sizeof(vert);
}

// Resolve the texture to draw with (NULL/invalid -> white) and check the
// frame's vertex budget. Returns false when the draw must be dropped.
static bool PrepareDraw(MarniDX::Impl* p, MarniHandle& tex, int vertexCount)
{
    if (p == nullptr || !p->ready) return false;
    if (tex == MARNI_NULL_HANDLE || tex >= MARNI_MAX_TEXTURES || !p->slots[tex].used) {
        tex = p->whiteHandle;
    }
    if (tex == MARNI_NULL_HANDLE) return false;

    BeginSceneIfNeeded(p);
    if (p->vertsThisFrame + vertexCount > kVertexBudget) {
        p->periodDropped++;
        if (!p->budgetWarned) {
            printf("[MARNI] vertex budget (%d) exceeded - dropping draws this frame\n",
                   kVertexBudget);
            p->budgetWarned = true;
        }
        return false;
    }
    p->vertsThisFrame += vertexCount;
    p->drewThisScene = true;
    p->periodDraws++;
    return true;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

MarniDX::MarniDX() : m_pImpl(new Impl()) {}
MarniDX::~MarniDX() { Destroy(); delete m_pImpl; m_pImpl = nullptr; }

BOOL MarniDX::Create(HWND, int width, int height, BOOL, int* outWidth, int* outHeight)
{
    Impl* p = m_pImpl;
    if (p == nullptr) return FALSE;

    // The KOS video mode is 640x480 (set at boot); the game's logical
    // 320x240 is scaled by the callers exactly as on PC.
    (void)width; (void)height;
    p->width  = 640;
    p->height = 480;

    if (!p->pvrUp) {
        pvr_init_params_t params;
        memset(&params, 0, sizeof(params));
        params.opb_sizes[PVR_LIST_OP_POLY] = PVR_BINSIZE_0;
        params.opb_sizes[PVR_LIST_OP_MOD]  = PVR_BINSIZE_0;
        params.opb_sizes[PVR_LIST_TR_POLY] = PVR_BINSIZE_32;
        params.opb_sizes[PVR_LIST_TR_MOD]  = PVR_BINSIZE_0;
        params.opb_sizes[PVR_LIST_PT_POLY] = PVR_BINSIZE_0;
        params.vertex_buf_size   = kVertexBufBytes;
        params.dma_enabled       = 0;
        params.fsaa_enabled      = 0;
        params.autosort_disabled = 1;    // presort: submission order
        // Extra tile-bin space. Every Marni triangle is its own strip, so a
        // small, far-away model piles hundreds of polygons into a few 32x32
        // tiles; the real PowerVR (not Flycast) chokes when the bins run out.
        // 8, not KOS's 3: with 3 the inventory's 3D item model - drawn last,
        // hundreds of triangles over tiles already holding the menu's sprites -
        // lost its polygons to bin overflow on the console. Each set costs
        // ~75 KB of VRAM across the two TA buffers.
        params.opb_overflow_count = 8;
        if (pvr_init(&params) < 0) {
            printf("[MARNI] pvr_init failed\n");
            return FALSE;
        }
        pvr_set_bg_color(0.0f, 0.0f, 0.0f);
        p->pvrUp = true;
    }

    const DWORD white = 0xFFFFFFFFu;
    p->whiteHandle = CreateTexture(1, 1, 32, &white, nullptr, nullptr);
    p->ready = (p->whiteHandle != MARNI_NULL_HANDLE);

    if (outWidth)  *outWidth  = p->width;
    if (outHeight) *outHeight = p->height;
    printf("[MARNI] PVR backend %dx%d, VRAM free %u KB\n", p->width, p->height,
           (unsigned)(pvr_mem_available() / 1024));
    return p->ready ? TRUE : FALSE;
}

void MarniDX::Destroy()
{
    Impl* p = m_pImpl;
    if (p == nullptr) return;
    if (p->inScene) {
        TaFlush();
        pvr_list_finish();
        pvr_scene_finish();
        p->inScene = false;
    }
    if (p->pvrUp) WaitPvr();
    ReleasePending(p);
    for (int i = 0; i < MARNI_MAX_TEXTURES; ++i) {
        if (p->slots[i].mem != NULL) pvr_mem_free(p->slots[i].mem);
        p->slots[i] = TexSlot();
    }
    p->liveTextures = 0;
    p->liveBytes = 0;
    p->strideWidth = 0;
    p->ready = false;
}

BOOL MarniDX::IsReady() const { return (m_pImpl && m_pImpl->ready) ? TRUE : FALSE; }

void MarniDX::GetBackBufferSize(DWORD* outWidth, DWORD* outHeight) const
{
    if (outWidth)  *outWidth  = m_pImpl ? (DWORD)m_pImpl->width  : 0;
    if (outHeight) *outHeight = m_pImpl ? (DWORD)m_pImpl->height : 0;
}

int MarniDX::ChangeDisplayMode(DWORD, DWORD, BOOL) { return 1; }   // fixed 640x480
int MarniDX::HandleWindowMessage(HWND, UINT, WPARAM, LPARAM) { return 1; }

unsigned int MarniDX::QueryVideoMemory(BOOL) const
{
    return (unsigned int)pvr_mem_available();
}

void MarniDX::EnumerateDisplayModes(DisplayModeInfo* outModes, int maxModes, int* outCount)
{
    int count = 0;
    if (outModes != nullptr && maxModes > 0) {
        outModes[0].dwWidth = 640; outModes[0].dwHeight = 480;
        outModes[0].dwBPP = 16;    outModes[0].dwRefreshRate = 60;
        outModes[0].dwFlags = 0;
        count = 1;
    }
    if (outCount) *outCount = count;
}

void MarniDX::EnumerateAdapters(D3DRendererInfo* outRenderers, int max, int* outCount)
{
    int count = 0;
    if (outRenderers != nullptr && max > 0) {
        strncpy(outRenderers[0].name, "PowerVR2", sizeof(outRenderers[0].name) - 1);
        outRenderers[0].name[sizeof(outRenderers[0].name) - 1] = '\0';
        outRenderers[0].flags = 0;
        count = 1;
    }
    if (outCount) *outCount = count;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void MarniDX::Clear(float r, float g, float b, float)
{
    Impl* p = m_pImpl;
    if (p == nullptr || !p->ready) return;

    pvr_set_bg_color(r, g, b);   // the PVR "clear" is its background plane
    BeginSceneIfNeeded(p);

    // A clear after draws in the same scene cannot reset the tile buffers, so
    // paint over: full-screen quad in the clear colour that also writes the
    // background depth (ALWAYS + write at the zclip depth).
    if (p->drewThisScene) {
        p->periodMidClears++;
        MarniHandle tex = p->whiteHandle;
        if (!PrepareDraw(p, tex, 4)) return;
        DrawKey k = { tex, MARNI_SAMPLER_POINT, MARNI_BLEND_DISABLE, false, true };
        SendHeader(p, k);
        uint32_t c = PackColor(r, g, b, 1.0f);
        const float z = 0.0001f;
        const float W = (float)p->width, H = (float)p->height;
        SendVertex(0, 0, z, 0, 0, c, false);
        SendVertex(W, 0, z, 0, 0, c, false);
        SendVertex(0, H, z, 0, 0, c, false);
        SendVertex(W, H, z, 0, 0, c, true);
        p->periodTris += 2;
    }
}

// Debug builds: whenever the number of live textures reaches a new high
// (4 or more above the last census), list them all - size, format, the frame
// they were made in and code addresses from their creator's stack - to find
// textures that are never destroyed. Resolve with sh-elf-addr2line -f -C -e.
static void ReportTextureCensus(MarniDX::Impl* p)
{
    if (p->liveTextures < p->censusMax + 4) return;
    p->censusMax = p->liveTextures;
    printf("[MARNI] texture census: %d live, %u KB (frame %u):\n", p->liveTextures,
           (unsigned)(p->liveBytes / 1024), p->frames);
    for (int h = 1; h < MARNI_MAX_TEXTURES; ++h) {
        const TexSlot& t = p->slots[h];
        if (!t.used) continue;
        printf("    #%d %dx%d %s%s x%d %uKB born %u after %s by %08lx %08lx %08lx %08lx\n", h, t.w, t.h,
               t.strided ? "strided" : "twiddled", t.shift ? " half" : "", t.refs,
               (unsigned)(t.bytes / 1024), t.born, t.from[0] ? t.from : "-",
               (unsigned long)t.site[0], (unsigned long)t.site[1],
               (unsigned long)t.site[2], (unsigned long)t.site[3]);
        // Owners in the game's tables: S<n> = g_TexturePageSRV[n] (sprite
        // slots), P<n> = g_TexturePageTable_DAT[n] (legacy pages). Fewer owners
        // than refs means a holder dropped the handle without releasing it
        // (other holders - model pages, item pages, fonts - are not listed).
        char own[160];
        int len = 0, owners = 0;
        own[0] = 0;
        for (int i = 0; i < 256 && len < (int)sizeof(own) - 8; ++i) {
            if (g_TexturePageSRV[i] == (MarniHandle)h)
                { len += snprintf(own + len, sizeof(own) - len, " S%d", i); ++owners; }
            if (g_TexturePageTable_DAT[i] == (DWORD)h)
                { len += snprintf(own + len, sizeof(own) - len, " P%d", i); ++owners; }
        }
        printf("        owners:%s%s (%d of %d)\n", owners ? own : " none", "", owners, t.refs);
    }
}

void MarniDX::Present()
{
    Impl* p = m_pImpl;
    if (p == nullptr || !p->ready) return;

    BeginSceneIfNeeded(p);       // an empty frame still needs a scene
    TaFlush();
        pvr_list_finish();
    pvr_scene_finish();
    p->inScene = false;
    p->periodSubmitUs += timer_us_gettime64() - p->sceneT0;
    if (g_frameTaBytes > g_peakTaBytes) g_peakTaBytes = g_frameTaBytes;
    g_frameTaBytes = 0;

    if (RE1_DC_VERBOSE && (++p->frames % 300) == 0) {
        printf("[MARNI] %u frames, last-period %u draws / %u tris (%u 3D), %u dropped, "
               "%u mid-scene clears, %d live textures (%u KB VRAM), VRAM free %u KB\n",
               p->frames, p->periodDraws, p->periodTris, p->period3DTris, p->periodDropped,
               p->periodMidClears, p->liveTextures, (unsigned)(p->liveBytes / 1024),
               (unsigned)(pvr_mem_available() / 1024));
        if (p->haveSample) {
            printf("[MARNI] 3D sample: x=%.1f y=%.1f zndc=%.4f w=%.1f -> zpvr=%.6f "
                   "uv=(%.3f,%.3f) argb=%08lx tex=%u blend=%d\n",
                   p->sx, p->sy, p->sz, p->sw, p->spz, p->su, p->sv,
                   (unsigned long)p->sargb, (unsigned)p->stex, (int)p->sblend);
        }
        p->periodDraws = p->periodTris = p->periodDropped = 0;
        // TA data high-water mark: decides whether the 2 x 512 KB vertex
        // buffers can shrink to give VRAM back to textures.
        printf("[MARNI] vertex buffer: peak %u KB of %u KB per frame so far\n",
               (unsigned)(g_peakTaBytes / 1024), (unsigned)(kVertexBufBytes / 1024));
        ReportTextureCensus(p);
        printf("[MARNI] per frame: %u.%u ms waiting for the PVR, %u.%u ms from first draw to Present\n",
               (unsigned)(p->periodWaitUs / 300 / 1000), (unsigned)((p->periodWaitUs / 300 / 100) % 10),
               (unsigned)(p->periodSubmitUs / 300 / 1000), (unsigned)((p->periodSubmitUs / 300 / 100) % 10));
        p->periodWaitUs = p->periodSubmitUs = 0;
        if (p->periodTexCount > 0) {
            printf("[MARNI] textures this period: %u created, %u ms total\n",
                   p->periodTexCount, p->periodTexUs / 1000);
        }
        if (p->periodShared > 0)
            printf("[MARNI] textures this period: %u requests answered by an identical texture in VRAM\n",
                   p->periodShared);
        p->periodShared = 0;
        p->periodMidClears = p->period3DTris = 0;
        p->periodTexUs = p->periodTexCount = 0;
        p->haveSample = false;
        p->budgetWarned = false;
    }
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

// RGBA8 (r in the lowest byte) -> 16 bpp, one instantiation per format so the
// per-pixel loop carries no format switch. Works on the whole 32-bit word
// (a few shifts and masks) instead of four byte loads per texel.
static inline uint32_t LoadRgba(const unsigned char* p)
{
    uint32_t v;
    memcpy(&v, p, 4);                    // inlined: callers pass aligned or not
    return v;
}
struct Cvt565  { static inline uint32_t px(uint32_t v) {
    return ((v & 0xF8u) << 8) | ((v >> 5) & 0x07E0u) | ((v >> 19) & 0x001Fu); } };
struct Cvt1555 { static inline uint32_t px(uint32_t v) {
    return ((v >> 16) & 0x8000u) | ((v & 0xF8u) << 7) | ((v >> 6) & 0x03E0u) | ((v >> 19) & 0x001Fu); } };
struct Cvt4444 { static inline uint32_t px(uint32_t v) {
    return ((v >> 16) & 0xF000u) | ((v & 0xF0u) << 4) | ((v >> 8) & 0x00F0u) | ((v >> 20) & 0x000Fu); } };

// One pass over the image: the 16 bpp format it needs (opaque -> 565,
// cut-out alpha -> 1555, anything else -> 4444) and, if asked, the content
// hash used to share identical textures. Never returns hash 0.
static int ScanRgba(const unsigned char* rgba, int w, int h, uint64_t* hashOut)
{
    const size_t n = (size_t)w * h;
    uint32_t allA = 0xFFFFFFFFu;         // AND of all words: alpha 255 everywhere?
    uint32_t odd = 0;                    // any alpha other than 0 / 255?
    uint32_t a = 2166136261u, b = 0x9E3779B9u;
    const bool aligned = (((uintptr_t)rgba) & 3) == 0;
    const uint32_t* p32 = (const uint32_t*)rgba;
    if (hashOut != nullptr) {
        for (size_t i = 0; i < n; ++i) {
            const uint32_t v = aligned ? p32[i] : LoadRgba(rgba + i * 4);
            allA &= v;
            odd |= ((v >> 24) + 1u) & 0xFEu;
            a = (a ^ v) * 16777619u;
            b = ((b << 5) | (b >> 27)) + v;
        }
        const uint64_t h64 = ((uint64_t)a << 32) ^ b ^ ((uint64_t)w << 16) ^ (uint64_t)h;
        *hashOut = h64 ? h64 : 1;
    } else {
        for (size_t i = 0; i < n; ++i) {
            const uint32_t v = aligned ? p32[i] : LoadRgba(rgba + i * 4);
            allA &= v;
            odd |= ((v >> 24) + 1u) & 0xFEu;
        }
    }
    if ((allA >> 24) == 0xFFu) return PVR_TXRFMT_RGB565;
    if (odd == 0)              return PVR_TXRFMT_ARGB1555;
    return PVR_TXRFMT_ARGB4444;
}

static int ChooseColorFormat(const unsigned char* rgba, int count)
{
    return ScanRgba(rgba, count, 1, nullptr);
}

// Twiddle bit-spread table: spread(i) puts bit k of i at bit 2k. The PVR
// twiddled address of (x, y) in a square block is spread(y) | spread(x) << 1
// (KOS TWIDOUT), and a non-square texture is a row/column of such blocks.
static uint32_t s_twid[1024];
static void InitTwiddleTable()
{
    if (s_twid[1] != 0) return;
    for (uint32_t i = 0; i < 1024; ++i) {
        uint32_t v = 0;
        for (int b = 0; b < 10; ++b) v |= ((i >> b) & 1u) << (2 * b);
        s_twid[i] = v;
    }
}

// Linear rows of exactly w texels plus one repeated bottom row (bilinear at
// the edge reads defined data). w is a multiple of 32 here, so rows are
// written two texels per 32-bit store.
template <class C>
static void BuildStrided(uint16_t* out, const unsigned char* rgba, int w, int h)
{
    const bool aligned = (((uintptr_t)rgba) & 3) == 0;
    for (int y = 0; y <= h; ++y) {
        const unsigned char* row = rgba + (size_t)((y < h) ? y : h - 1) * w * 4;
        const uint32_t* r32 = (const uint32_t*)row;
        uint32_t* o = (uint32_t*)(out + (size_t)y * w);
        for (int x = 0; x + 1 < w; x += 2) {
            const uint32_t v0 = aligned ? r32[x] : LoadRgba(row + x * 4);
            const uint32_t v1 = aligned ? r32[x + 1] : LoadRgba(row + x * 4 + 4);
            o[x >> 1] = C::px(v0) | (C::px(v1) << 16);
        }
        if (w & 1) out[(size_t)y * w + w - 1] = (uint16_t)C::px(LoadRgba(row + (w - 1) * 4));
    }
}

// Twiddled pw x ph image straight from the w x h source. Padding repeats the
// last column/row - all bilinear needs; the rest of the padding is never
// sampled because UVs are scaled to w/pw, h/ph.
//
// The four texels of an aligned 2x2 square are consecutive in twiddled order
// ((x,y) (x,y+1) (x+1,y) (x+1,y+1)), so one address computation and two
// 32-bit stores cover four texels.
template <class C>
static void BuildTwiddled(uint16_t* out, const unsigned char* rgba, int w, int h, int pw, int ph)
{
    const int m = pw < ph ? pw : ph;          // square block size
    const int mask = m - 1;
    int shift = 0;
    while ((1 << shift) < m) ++shift;
    const int blockShift = 2 * shift;          // log2(m*m)
    const bool aligned = (((uintptr_t)rgba) & 3) == 0;
    const size_t stride = (size_t)w * 4;

    if (m >= 2) {
        uint32_t* out32 = (uint32_t*)out;
        for (int y = 0; y < ph; y += 2) {
            const int sy0 = (y < h) ? y : h - 1;
            const int sy1 = (y + 1 < h) ? y + 1 : h - 1;
            const unsigned char* r0 = rgba + (size_t)sy0 * stride;
            const unsigned char* r1 = rgba + (size_t)sy1 * stride;
            const uint32_t ty = s_twid[y & mask];
            const uint32_t by = (uint32_t)(y >> shift);
            if (aligned && pw == w) {
                const uint32_t* a0 = (const uint32_t*)r0;
                const uint32_t* a1 = (const uint32_t*)r1;
                for (int x = 0; x < pw; x += 2) {
                    const uint32_t idx = (ty | (s_twid[x & mask] << 1)) +
                                         (((uint32_t)(x >> shift) + by) << blockShift);
                    out32[idx >> 1]       = C::px(a0[x])     | (C::px(a1[x]) << 16);
                    out32[(idx >> 1) + 1] = C::px(a0[x + 1]) | (C::px(a1[x + 1]) << 16);
                }
            } else {
                for (int x = 0; x < pw; x += 2) {
                    const int sx0 = (x < w) ? x : w - 1;
                    const int sx1 = (x + 1 < w) ? x + 1 : w - 1;
                    const uint32_t idx = (ty | (s_twid[x & mask] << 1)) +
                                         (((uint32_t)(x >> shift) + by) << blockShift);
                    out32[idx >> 1]       = C::px(LoadRgba(r0 + sx0 * 4)) | (C::px(LoadRgba(r1 + sx0 * 4)) << 16);
                    out32[(idx >> 1) + 1] = C::px(LoadRgba(r0 + sx1 * 4)) | (C::px(LoadRgba(r1 + sx1 * 4)) << 16);
                }
            }
        }
        return;
    }
    // 1 x N / N x 1: blocks of a single texel.
    for (int y = 0; y < ph; ++y) {
        const unsigned char* row = rgba + (size_t)((y < h) ? y : h - 1) * stride;
        for (int x = 0; x < pw; ++x) {
            const int sx = (x < w) ? x : w - 1;
            const uint32_t idx = (uint32_t)(x >> shift) + (uint32_t)(y >> shift);
            out[idx] = (uint16_t)C::px(LoadRgba(row + sx * 4));
        }
    }
}

template <class C>
static void BuildImage(const TexSlot& s, uint16_t* buf, const unsigned char* rgba)
{
    if (s.strided) BuildStrided<C>(buf, rgba, s.w, s.h);
    else           BuildTwiddled<C>(buf, rgba, s.w >> s.shift, s.h >> s.shift, s.pw, s.ph);
}

// Convert in RAM, then one store-queue copy into VRAM (pvr_txr_load). The old
// path twiddled with one 16-bit write per texel straight into VRAM - the
// main cost of a room load.
// 2x2 box filter, colour weighted by alpha so cut-out edges do not pick up
// the transparent pixels' (usually black) colour.
static unsigned char* HalveRgba(const unsigned char* src, int w, int h)
{
    const int hw = w >> 1, hh = h >> 1;
    unsigned char* out = (unsigned char*)malloc((size_t)hw * hh * 4);
    if (out == nullptr) return nullptr;
    for (int y = 0; y < hh; ++y) {
        for (int x = 0; x < hw; ++x) {
            const unsigned char* q[4] = {
                src + ((2 * y) * w + 2 * x) * 4,     src + ((2 * y) * w + 2 * x + 1) * 4,
                src + ((2 * y + 1) * w + 2 * x) * 4, src + ((2 * y + 1) * w + 2 * x + 1) * 4 };
            unsigned a = 0, r = 0, g = 0, b = 0;
            for (int i = 0; i < 4; ++i) {
                const unsigned qa = q[i][3];
                a += qa; r += q[i][0] * qa; g += q[i][1] * qa; b += q[i][2] * qa;
            }
            unsigned char* o = out + (y * hw + x) * 4;
            if (a == 0) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            o[0] = (unsigned char)(r / a); o[1] = (unsigned char)(g / a); o[2] = (unsigned char)(b / a);
            o[3] = (unsigned char)((a + 2) / 4);
        }
    }
    return out;
}

static bool UploadSlot(TexSlot& s, const unsigned char* rgba)
{
    InitTwiddleTable();
    unsigned char* halved = nullptr;
    if (s.shift) {
        halved = HalveRgba(rgba, s.w, s.h);
        if (halved == nullptr) return false;
        rgba = halved;
    }
    uint16_t* buf = (uint16_t*)memalign(32, s.bytes);
    if (buf == nullptr) { free(halved); return false; }
    switch (s.fmt & (7 << 27)) {
    case PVR_TXRFMT_RGB565:   BuildImage<Cvt565>(s, buf, rgba);  break;
    case PVR_TXRFMT_ARGB1555: BuildImage<Cvt1555>(s, buf, rgba); break;
    default:                  BuildImage<Cvt4444>(s, buf, rgba); break;
    }
    DC_TRACE('T');
    pvr_txr_load(buf, s.mem, s.bytes);
    DC_TRACE('t');
    free(buf);
    free(halved);
    return true;
}

// BG_SMOOTH_FIX: give every fully transparent texel the average colour of its
// opaque neighbours (alpha stays 0), so bilinear filtering at a cut-out edge
// blends toward the edge colour instead of toward black.
static void EdgeSpreadRgba(unsigned char* px, int w, int h)
{
    unsigned char* a = (unsigned char*)malloc((size_t)w * h);
    if (a == nullptr) return;
    for (int i = 0; i < w * h; ++i) a[i] = px[i * 4 + 3];
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (a[y * w + x] != 0) continue;
            unsigned r = 0, g = 0, b = 0, n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = x + dx;
                    if (xx < 0 || xx >= w || a[yy * w + xx] == 0) continue;
                    const unsigned char* q = px + ((size_t)yy * w + xx) * 4;
                    r += q[0]; g += q[1]; b += q[2]; ++n;
                }
            }
            if (n == 0) continue;
            unsigned char* o = px + ((size_t)y * w + x) * 4;
            o[0] = (unsigned char)(r / n); o[1] = (unsigned char)(g / n); o[2] = (unsigned char)(b / n);
        }
    }
    free(a);
}

MarniHandle MarniDX::CreateTexture(int width, int height, int bpp, const void* pixelData,
                                   int* outWidth, int* outHeight)
{
    Impl* p = m_pImpl;
    if (p == nullptr || width <= 0 || height <= 0 || pixelData == nullptr) return MARNI_NULL_HANDLE;
    const uint64_t t0 = timer_us_gettime64();
    if (width > 1024 || height > 1024) {
        printf("[MARNI] texture %dx%d too large for the PVR\n", width, height);
        return MARNI_NULL_HANDLE;
    }

    // Normalise to RGBA8 (same input contract as the GL backend).
    const unsigned char* src = (const unsigned char*)pixelData;
    unsigned char* converted = nullptr;
    if (bpp == 24) {
        converted = (unsigned char*)malloc((size_t)width * height * 4);
        if (converted == nullptr) return MARNI_NULL_HANDLE;
        for (int i = 0; i < width * height; ++i) {
            converted[i * 4 + 0] = src[i * 3 + 2];
            converted[i * 4 + 1] = src[i * 3 + 1];
            converted[i * 4 + 2] = src[i * 3 + 0];
            converted[i * 4 + 3] = 0xFF;
        }
        src = converted;
    } else if (bpp != 32) {
        return MARNI_NULL_HANDLE;
    }
    // BG_SMOOTH_FIX: room masks are drawn filtered - spread colour into their
    // cut-out texels first (on a private copy; the caller's pixels are const).
    const bool smoothReq = RE1_DC_SMOOTH && g_dcSmoothNext != 0;
    if (smoothReq) {
        if (converted == nullptr) {
            converted = (unsigned char*)malloc((size_t)width * height * 4);
            if (converted == nullptr) return MARNI_NULL_HANDLE;
            memcpy(converted, src, (size_t)width * height * 4);
            src = converted;
        }
        EdgeSpreadRgba(converted, width, height);
    }

    MarniHandle handle = MARNI_NULL_HANDLE;
    for (MarniHandle h = 1; h < MARNI_MAX_TEXTURES; ++h) {
        if (!p->slots[h].used) { handle = h; break; }
    }
    if (handle == MARNI_NULL_HANDLE) { free(converted); return MARNI_NULL_HANDLE; }

    TexSlot s;
    s.used = true;
    s.w = width;
    s.h = height;
    s.pw = NextPow2(width);
    s.ph = NextPow2(height);

    const bool pow2 = IsPow2(width) && IsPow2(height);
    if (!pow2 && (width % 32) == 0 && width <= 992 && height >= 8 &&
        (p->strideWidth == 0 || p->strideWidth == width)) {
        if (p->strideWidth == 0) {
            p->strideWidth = width;
            pvr_txr_set_stride((size_t)width);
            printf("[MARNI] global texture stride claimed by width %d\n", width);
        }
        s.strided = true;
    }

    s.smooth = RE1_DC_SMOOTH && (s.strided || smoothReq);   // BG_SMOOTH_FIX

    // Format and (for shareable textures) content hash in one pass.
    uint64_t contentHash = 0;
    const int colorFmt = ScanRgba(src, width, height, s.strided ? nullptr : &contentHash);
    s.fmt = colorFmt | (s.strided ? (PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_X32_STRIDE)
                                  : PVR_TXRFMT_TWIDDLED);
    s.bytes = s.strided ? (size_t)width * (height + 1) * 2 : (size_t)s.pw * s.ph * 2;
    s.us = (float)width / (float)s.pw;
    s.vs = (float)height / (float)s.ph;

    // Same pixels already in VRAM? Hand out that texture again (refcounted).
    // The game keeps several copies of one PS1 VRAM page - LoadImage makes a
    // texture per destination slot of the page (item pictures: 7 identical
    // 256x256 pages = 896 KB in one census). Strided textures (movie frames,
    // backgrounds) are updated in place, so they are never shared.
    if (!s.strided) {
        s.hash = contentHash;
        if (s.smooth) s.hash ^= 0x9E3779B97F4A7C15ull;   // never shared with a sharp copy
        for (MarniHandle h = 1; h < MARNI_MAX_TEXTURES; ++h) {
            TexSlot& o = p->slots[h];
            if (!o.used || o.hash != s.hash || o.w != width || o.h != height) continue;
            o.refs++;
            p->periodShared++;
            free(converted);
            if (outWidth)  *outWidth  = width;
            if (outHeight) *outHeight = height;
            return h;
        }
    }

    // During a room change no frame is rendered, so the previous room's
    // textures still sit in the pending-free list while the new ones load.
    // Release them up front when the free VRAM is short, instead of letting
    // pvr_mem_malloc fail (KOS logs that as "texture heap probably
    // corrupted"). pvr_mem_available() is the total free, not the largest
    // block - with the heap fragmented a 128 KB request failed with over
    // 1 MB free, so any pending frees are released before the first try.
    // Textures are created almost only while a room or a menu loads, where
    // waiting for the last frame to finish costs nothing visible.
    if (p->pendingCount > 0) {
        DrainPvr(p);
    }
    s.mem = pvr_mem_malloc(s.bytes);
    if (s.mem == NULL && p->pendingCount > 0) {
        DrainPvr(p);                       // typical during a room change
        s.mem = pvr_mem_malloc(s.bytes);
    }
    if (s.mem == NULL && !s.strided && width >= 16 && height >= 16) {
        // VRAM short (or too fragmented for this block): keep the texture at
        // half size - a quarter of the memory - rather than not at all. UVs are
        // normalised, so it draws in the same place, just softer.
        TexSlot half = s;
        half.shift = 1;
        half.pw = NextPow2(width >> 1);
        half.ph = NextPow2(height >> 1);
        half.bytes = (size_t)half.pw * half.ph * 2;
        half.us = (float)(width >> 1) / (float)half.pw;
        half.vs = (float)(height >> 1) / (float)half.ph;
        half.mem = pvr_mem_malloc(half.bytes);
        if (half.mem != NULL) {
            printf("[MARNI] VRAM short: %dx%d kept at half size (%u KB instead of %u KB)\n",
                   width, height, (unsigned)(half.bytes / 1024), (unsigned)(s.bytes / 1024));
            s = half;
        }
    }
    if (s.mem == NULL) {
        printf("[MARNI] out of VRAM for %dx%d (%u KB), free %u KB\n", width, height,
               (unsigned)(s.bytes / 1024), (unsigned)(pvr_mem_available() / 1024));
        free(converted);
        return MARNI_NULL_HANDLE;
    }
    if (!UploadSlot(s, src)) {
        pvr_mem_free(s.mem);
        free(converted);
        return MARNI_NULL_HANDLE;
    }
    free(converted);

    s.born = p->frames;
    GrabSites(s.site);
    if (g_dcPageCaller != nullptr) {            // legacy page: name its real creator
        s.site[3] = s.site[2]; s.site[2] = s.site[1]; s.site[1] = s.site[0];
        s.site[0] = (uint32_t)(uintptr_t)g_dcPageCaller;
        g_dcPageCaller = nullptr;
    }
    strncpy(s.from, dc_last_loaded_file(), sizeof(s.from) - 1);
    p->slots[handle] = s;
    p->liveTextures++;
    p->liveBytes += s.bytes;

    const unsigned us = (unsigned)(timer_us_gettime64() - t0);
    p->periodTexUs += us;
    p->periodTexCount++;
    if (RE1_DC_VERBOSE && p->createdLogged < 64) {
        p->createdLogged++;
        printf("[MARNI] tex %u: %dx%d -> %s %dx%d %s, %u KB, %u.%u ms\n", (unsigned)handle,
               width, height, s.strided ? "strided" : "twiddled", s.pw, s.ph,
               colorFmt == PVR_TXRFMT_RGB565 ? "565" :
               colorFmt == PVR_TXRFMT_ARGB1555 ? "1555" : "4444",
               (unsigned)(s.bytes / 1024), us / 1000, (us / 100) % 10);
    }

    if (outWidth)  *outWidth  = width;
    if (outHeight) *outHeight = height;
    return handle;
}

MarniHandle MarniDX::CreateTextureFromBits(CMarniBits* bits)
{
    if (bits == nullptr || bits->m_pPixelData == nullptr) return MARNI_NULL_HANDLE;
    return CreateTexture((int)bits->m_width, (int)bits->m_height,
                         (int)bits->m_bitDepth, bits->m_pPixelData, nullptr, nullptr);
}

BOOL MarniDX::UpdateTexturePixels(MarniHandle tex, const void* pixelData, int width, int height, int bpp)
{
    Impl* p = m_pImpl;
    if (p == nullptr || tex == MARNI_NULL_HANDLE || tex >= MARNI_MAX_TEXTURES) return FALSE;
    TexSlot& s = p->slots[tex];
    if (!s.used || pixelData == nullptr) return FALSE;
    if (width != s.w || height != s.h || bpp != 32) return FALSE;
    if (s.refs > 1) printf("[MARNI] warning: updating shared texture %u (%d owners)\n", (unsigned)tex, s.refs);
    s.hash = 0;                                    // content changed: no longer a dedup target

    // Same size, so the VRAM block fits any 16 bpp format: re-pick it.
    const unsigned char* src = (const unsigned char*)pixelData;
    const int colorFmt = ChooseColorFormat(src, width * height);
    s.fmt = colorFmt | (s.fmt & ~(7 << 27));
    if (p->haveHeader && p->lastKey.tex == tex) p->haveHeader = false;
    return UploadSlot(s, src) ? TRUE : FALSE;
}

void MarniDX::GetTextureSize(MarniHandle tex, int* outWidth, int* outHeight) const
{
    Impl* p = m_pImpl;
    if (outWidth)  *outWidth  = 0;
    if (outHeight) *outHeight = 0;
    if (p == nullptr || tex == MARNI_NULL_HANDLE || tex >= MARNI_MAX_TEXTURES) return;
    if (outWidth)  *outWidth  = p->slots[tex].w;
    if (outHeight) *outHeight = p->slots[tex].h;
}

void MarniDX::DestroyTexture(MarniHandle tex)
{
    Impl* p = m_pImpl;
    if (p == nullptr || tex == MARNI_NULL_HANDLE || tex >= MARNI_MAX_TEXTURES) return;
    TexSlot& s = p->slots[tex];
    if (!s.used) return;
    if (s.refs > 1) { s.refs--; return; }          // still in use under another owner
    if (s.mem != NULL) {
        if (p->pendingCount >= Impl::kMaxPending) DrainPvr(p);
        p->pendingFree[p->pendingCount++] = s.mem;
    }
    p->liveTextures--;
    p->liveBytes -= s.bytes;
    if (s.strided && p->strideWidth == s.w) {
        bool stillUsed = false;
        for (int i = 1; i < MARNI_MAX_TEXTURES && !stillUsed; ++i) {
            if (i != (int)tex && p->slots[i].used && p->slots[i].strided) stillUsed = true;
        }
        if (!stillUsed) p->strideWidth = 0;
    }
    if (p->haveHeader && p->lastKey.tex == tex) p->haveHeader = false;
    s = TexSlot();
    s_texSerial[tex]++;
    // Last owner gone: MarniSystem.cpp drops the STP knock-out twin it keyed
    // on this handle (the handle can be reused by an unrelated texture next).
    dc_marni_texture_freed((unsigned int)tex);
}

// MERGE140: HD textures only (inactive on Dreamcast, see plat_dir_list).
BOOL MarniDX::ReplaceTexture(MarniHandle, int, int, const void*) { return FALSE; }

unsigned int MarniDX::TextureSerial(MarniHandle tex) const
{
    return (tex < MARNI_MAX_TEXTURES) ? s_texSerial[tex] : 0;
}

// The PVR output is always 640x480, i.e. already 4:3.
void MarniDX::SetAspectLock(BOOL) {}

MarniHandle MarniDX::WhiteTexture() const
{
    return m_pImpl ? m_pImpl->whiteHandle : MARNI_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void MarniDX::DrawSprite(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         DWORD color, MarniHandle tex,
                         MarniSampler sampler, MarniBlend blend)
{
    Impl* p = m_pImpl;
    if (!PrepareDraw(p, tex, 4)) return;
    const TexSlot& s = p->slots[tex];
    DrawKey k = { tex, sampler, blend, false, false };
    SendHeader(p, k);

    const uint32_t c = (uint32_t)color;   // already ARGB
    const float x1 = x + w, y1 = y + h;
    if (s.smooth) {
        // BG_SMOOTH_FIX: half-texel inset, so filtering stays inside this
        // sprite's piece of the page (and inside a strided background).
        const float du = 0.5f / (float)s.w, dv = 0.5f / (float)s.h;
        if (u1 - u0 > 2 * du) { u0 += du; u1 -= du; } else if (u0 - u1 > 2 * du) { u0 -= du; u1 += du; }
        if (v1 - v0 > 2 * dv) { v0 += dv; v1 -= dv; } else if (v0 - v1 > 2 * dv) { v0 -= dv; v1 += dv; }
    }
    u0 *= s.us; u1 *= s.us; v0 *= s.vs; v1 *= s.vs;
    SendVertex(x,  y,  kZ2D, u0, v0, c, false);
    SendVertex(x1, y,  kZ2D, u1, v0, c, false);
    SendVertex(x,  y1, kZ2D, u0, v1, c, false);
    SendVertex(x1, y1, kZ2D, u1, v1, c, true);
    p->periodTris += 2;
}

void MarniDX::DrawRect(int x, int y, int w, int h, DWORD color)
{
    DrawSprite((float)x, (float)y, (float)w, (float)h, 0.0f, 0.0f, 1.0f, 1.0f, color,
               m_pImpl ? m_pImpl->whiteHandle : MARNI_NULL_HANDLE,
               MARNI_SAMPLER_POINT, MARNI_BLEND_ALPHA);
}

void MarniDX::DrawLine(float x0, float y0, float x1, float y1, float thickness, DWORD color)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len <= 0.0f) return;
    float nx = -dy / len * (thickness * 0.5f);
    float ny =  dx / len * (thickness * 0.5f);

    Impl* p = m_pImpl;
    MarniHandle tex = p ? p->whiteHandle : MARNI_NULL_HANDLE;
    if (!PrepareDraw(p, tex, 4)) return;
    DrawKey k = { tex, MARNI_SAMPLER_POINT, MARNI_BLEND_ALPHA, false, false };
    SendHeader(p, k);
    const uint32_t c = (uint32_t)color;
    // Strip a, b, e, c == the GL triangles (a,b,e) (e,b,c).
    SendVertex(x0 + nx, y0 + ny, kZ2D, 0, 0, c, false);
    SendVertex(x1 + nx, y1 + ny, kZ2D, 0, 0, c, false);
    SendVertex(x0 - nx, y0 - ny, kZ2D, 0, 0, c, false);
    SendVertex(x1 - nx, y1 - ny, kZ2D, 0, 0, c, true);
    p->periodTris += 2;
}

// 8-float vertices { x, y, u, v, r, g, b, a }, no depth.
void MarniDX::DrawTriangles(const float* verts, int triCount, MarniHandle tex,
                            MarniSampler sampler, MarniBlend blend)
{
    Impl* p = m_pImpl;
    if (verts == nullptr || triCount <= 0) return;
    if (!PrepareDraw(p, tex, triCount * 3)) return;
    const TexSlot& s = p->slots[tex];
    DrawKey k = { tex, sampler, blend, false, false };
    SendHeader(p, k);
    for (int t = 0; t < triCount; ++t) {
        for (int i = 0; i < 3; ++i) {
            const float* v = verts + (t * 3 + i) * 8;
            SendVertex(v[0], v[1], kZ2D, v[2] * s.us, v[3] * s.vs,
                       PackColor(v[4], v[5], v[6], v[7]), i == 2);
        }
    }
    p->periodTris += (unsigned)triCount;
}

// 10-float vertices { x, y, z, w, u, v, r, g, b, a }.
// OVERLAY_ZBIAS_FIX: depth scale for 3D overlays (depth test, no depth write).
// PVR depth is 1/w-like, larger = nearer, compared GEQUAL; 1 + 1/512 pulls
// the overlay ~0.2% of its distance toward the camera, so coplanar overlays
// (room 20A0's tank wash and knock-out copy over its shell) stop z-fighting
// on hardware, whose per-pixel depth is coarser than Flycast's.
static const float kOverlayZScale = 1.0f + 1.0f / 512.0f;

static void DrawTrianglesZW(MarniDX::Impl* p, const float* verts, int triCount,
                            MarniHandle tex, MarniSampler sampler, MarniBlend blend,
                            bool depthTest, bool depthWrite, float zScale = 1.0f)
{
    if (verts == nullptr || triCount <= 0) return;
    if (p != nullptr && !p->haveSample) {
        p->haveSample = true;
        p->sx = verts[0]; p->sy = verts[1]; p->sz = verts[2]; p->sw = verts[3];
        p->spz = DepthFromW(verts[3]); p->su = verts[4]; p->sv = verts[5];
        p->sargb = PackColor(verts[6], verts[7], verts[8], verts[9]);
        p->stex = tex; p->sblend = blend;
    }
#if RE1_PVR_DEBUG & 1
    depthTest = false; depthWrite = false;
#endif
#if RE1_PVR_DEBUG & 2
    tex = p ? p->whiteHandle : MARNI_NULL_HANDLE;
    blend = MARNI_BLEND_DISABLE;
#endif
    if (!PrepareDraw(p, tex, triCount * 3)) return;
    const TexSlot& s = p->slots[tex];
    DrawKey k = { tex, sampler, blend, depthTest, depthWrite };
    SendHeader(p, k);
    for (int t = 0; t < triCount; ++t) {
        for (int i = 0; i < 3; ++i) {
            const float* v = verts + (t * 3 + i) * 10;
#if RE1_PVR_DEBUG & 2
            const uint32_t c = 0xFFFF00FFu;
#else
            const uint32_t c = PackColor(v[6], v[7], v[8], v[9]);
#endif
            SendVertex(v[0], v[1], DepthFromW(v[3]) * zScale, v[4] * s.us, v[5] * s.vs, c, i == 2);
        }
    }
    p->periodTris += (unsigned)triCount;
    p->period3DTris += (unsigned)triCount;
}

void MarniDX::DrawTrianglesPersp(const float* verts, int triCount, MarniHandle tex,
                                 MarniSampler sampler, MarniBlend blend, bool depthTest)
{
    // Test only, never write (same as GL).
    DrawTrianglesZW(m_pImpl, verts, triCount, tex, sampler, blend, depthTest, false);
}

void MarniDX::DrawTriangles3D(const float* verts, int triCount, MarniHandle tex,
                              MarniSampler sampler, MarniBlend blend, bool depthWrite)
{
    DrawTrianglesZW(m_pImpl, verts, triCount, tex, sampler, blend, true, depthWrite,
                    depthWrite ? 1.0f : kOverlayZScale);   // OVERLAY_ZBIAS_FIX
}

// ---------------------------------------------------------------------------
// Readback - not available from PVR VRAM in this backend (GL returns FALSE
// for textures too; the only caller falls back).
// ---------------------------------------------------------------------------

BOOL MarniDX::CaptureBackbufferToRGBA(void**, DWORD*, DWORD*) { return FALSE; }
BOOL MarniDX::ReadTextureRGBA(MarniHandle, void**, int*, int*) { return FALSE; }

// ---------------------------------------------------------------------------
// Font texture
// ---------------------------------------------------------------------------

void MarniDX::SetFontTexture(MarniHandle tex, int width, int height)
{
    if (m_pImpl == nullptr) return;
    m_pImpl->fontHandle = tex; m_pImpl->fontW = width; m_pImpl->fontH = height;
}
MarniHandle MarniDX::FontTexture() const { return m_pImpl ? m_pImpl->fontHandle : MARNI_NULL_HANDLE; }
int MarniDX::FontTextureWidth()  const { return m_pImpl ? m_pImpl->fontW : 0; }
int MarniDX::FontTextureHeight() const { return m_pImpl ? m_pImpl->fontH : 0; }

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

MarniDX* MarniDX_Create()
{
    if (g_pDX == nullptr) g_pDX = new MarniDX();
    return g_pDX;
}

void MarniDX_DestroyGlobal()
{
    if (g_pDX != nullptr) { g_pDX->Destroy(); delete g_pDX; g_pDX = nullptr; }
}

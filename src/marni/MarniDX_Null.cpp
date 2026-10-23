// MarniDX_Null.cpp - headless MarniDX backend (Dreamcast bring-up, phase 1).
//
// Implements the full MarniDX contract (MarniDX.h) without rasterizing, so
// the shared game code can boot, load rooms and run its scripts while the
// PowerVR backend (MarniDX_PVR.cpp, phase 2) is written. Texture handles are
// real and report their sizes; no pixel data is kept, so the run also gives
// an honest main-RAM picture without VRAM-shadow copies.
//
// The one visible sign of life: Clear() fills the KOS framebuffer with the
// clear colour, so fades and screen transitions show on the TV.
#include "MarniDX.h"

#include <stdio.h>
#include <string.h>

#include "../game/Types.h"

#if defined(__DREAMCAST__)
#include <kos.h>
#endif

#define MARNI_MAX_TEXTURES 2048

struct MarniDX::Impl {
    int  width  = 640;
    int  height = 480;
    bool ready  = false;

    MarniHandle whiteHandle = MARNI_NULL_HANDLE;
    MarniHandle fontHandle  = MARNI_NULL_HANDLE;
    int fontW = 0, fontH = 0;

    struct TexSlot { bool used = false; int w = 0, h = 0; } slots[MARNI_MAX_TEXTURES];
    int liveTextures = 0;
    unsigned long liveTexels = 0;

    // Stats for the console: what the PVR backend will have to hold.
    unsigned frames = 0;
    unsigned drawCalls = 0, tris = 0;
};

static MarniDX* g_pDX = nullptr;
MarniDX* Marni_DX() { return g_pDX; }

MarniDX::MarniDX() : m_pImpl(new Impl()) {}
MarniDX::~MarniDX() { Destroy(); delete m_pImpl; m_pImpl = nullptr; }

BOOL MarniDX::Create(HWND, int width, int height, BOOL, int* outWidth, int* outHeight)
{
    Impl* p = m_pImpl;
    if (p == nullptr) return FALSE;
    p->width  = width  > 0 ? width  : 640;
    p->height = height > 0 ? height : 480;

    const DWORD white = 0xFFFFFFFFu;
    p->whiteHandle = CreateTexture(1, 1, 32, &white, nullptr, nullptr);
    p->ready = (p->whiteHandle != MARNI_NULL_HANDLE);

    if (outWidth)  *outWidth  = p->width;
    if (outHeight) *outHeight = p->height;
    printf("[MARNI] null backend %dx%d\n", p->width, p->height);
    return p->ready ? TRUE : FALSE;
}

void MarniDX::Destroy()
{
    Impl* p = m_pImpl;
    if (p == nullptr) return;
    for (int i = 0; i < MARNI_MAX_TEXTURES; ++i) p->slots[i] = Impl::TexSlot();
    p->liveTextures = 0; p->liveTexels = 0;
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

unsigned int MarniDX::QueryVideoMemory(BOOL) const { return 8u * 1024u * 1024u; }

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
        strncpy(outRenderers[0].name, "PowerVR2 (null)", sizeof(outRenderers[0].name) - 1);
        outRenderers[0].name[sizeof(outRenderers[0].name) - 1] = '\0';
        outRenderers[0].flags = 0;
        count = 1;
    }
    if (outCount) *outCount = count;
}

void MarniDX::Clear(float r, float g, float b, float)
{
#if defined(__DREAMCAST__)
    // RGB565 fill of the 640x480 KOS framebuffer.
    unsigned R = (unsigned)(r * 31.0f + 0.5f) & 31;
    unsigned G = (unsigned)(g * 63.0f + 0.5f) & 63;
    unsigned B = (unsigned)(b * 31.0f + 0.5f) & 31;
    uint32_t px = (R << 11) | (G << 5) | B;
    uint32_t two = (px << 16) | px;
    uint32_t* fb = (uint32_t*)vram_l;
    for (int i = 0; i < 640 * 480 / 2; ++i) fb[i] = two;
#else
    (void)r; (void)g; (void)b;
#endif
}

void MarniDX::Present()
{
    Impl* p = m_pImpl;
    if (p == nullptr) return;
    if ((++p->frames % 300) == 0) {
        printf("[MARNI] %u frames, last-period %u draws / %u tris, "
               "%d live textures (%lu KB at 16 bpp)\n",
               p->frames, p->drawCalls, p->tris, p->liveTextures,
               p->liveTexels * 2 / 1024);
        p->drawCalls = p->tris = 0;
    }
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

MarniHandle MarniDX::CreateTexture(int width, int height, int bpp, const void* pixelData,
                                   int* outWidth, int* outHeight)
{
    Impl* p = m_pImpl;
    if (p == nullptr || width <= 0 || height <= 0 || pixelData == nullptr) return MARNI_NULL_HANDLE;
    if (bpp != 24 && bpp != 32) return MARNI_NULL_HANDLE;   // same contract as GL

    for (MarniHandle h = 1; h < MARNI_MAX_TEXTURES; ++h) {
        if (!p->slots[h].used) {
            p->slots[h].used = true;
            p->slots[h].w = width;
            p->slots[h].h = height;
            p->liveTextures++;
            p->liveTexels += (unsigned long)width * height;
            if (outWidth)  *outWidth  = width;
            if (outHeight) *outHeight = height;
            return h;
        }
    }
    return MARNI_NULL_HANDLE;
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
    if (!p->slots[tex].used || pixelData == nullptr) return FALSE;
    if (width != p->slots[tex].w || height != p->slots[tex].h || bpp != 32) return FALSE;
    return TRUE;
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
    if (!p->slots[tex].used) return;
    p->liveTextures--;
    p->liveTexels -= (unsigned long)p->slots[tex].w * p->slots[tex].h;
    p->slots[tex] = Impl::TexSlot();
}

MarniHandle MarniDX::WhiteTexture() const
{
    return m_pImpl ? m_pImpl->whiteHandle : MARNI_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Drawing - counted, not rasterized.
// ---------------------------------------------------------------------------

void MarniDX::DrawSprite(float, float, float, float, float, float, float, float,
                         DWORD, MarniHandle, MarniSampler, MarniBlend)
{ if (m_pImpl) { m_pImpl->drawCalls++; m_pImpl->tris += 2; } }

void MarniDX::DrawRect(int, int, int, int, DWORD)
{ if (m_pImpl) { m_pImpl->drawCalls++; m_pImpl->tris += 2; } }

void MarniDX::DrawLine(float, float, float, float, float, DWORD)
{ if (m_pImpl) { m_pImpl->drawCalls++; m_pImpl->tris += 2; } }

void MarniDX::DrawTriangles(const float*, int triCount, MarniHandle, MarniSampler, MarniBlend)
{ if (m_pImpl) { m_pImpl->drawCalls++; m_pImpl->tris += triCount; } }

void MarniDX::DrawTrianglesPersp(const float*, int triCount, MarniHandle, MarniSampler, MarniBlend, bool)
{ if (m_pImpl) { m_pImpl->drawCalls++; m_pImpl->tris += triCount; } }

void MarniDX::DrawTriangles3D(const float*, int triCount, MarniHandle, MarniSampler, MarniBlend, bool)
{ if (m_pImpl) { m_pImpl->drawCalls++; m_pImpl->tris += triCount; } }

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

// MERGE140: methods added in upstream 1.4.0
BOOL MarniDX::ReplaceTexture(MarniHandle, int, int, const void*) { return FALSE; }
unsigned int MarniDX::TextureSerial(MarniHandle) const { return 0; }
void MarniDX::SetAspectLock(BOOL) {}

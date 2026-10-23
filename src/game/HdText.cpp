// HdText.cpp - HD text drawn in real TrueType fonts (port-only). See HdText.h.
#include "HdText.h"

#include "../Globals.h"
#include "../DebugPrint.h"
#include "SpriteRenderer.h"
#include "../system/AssetPath.h"
#include "../system/HdTextures.h"
#include "../marni/MarniDX.h"
#include "../marni/MarniSystem.h"
#include "../platform/platform.h"
#include "JpnFontTable.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

// The rasteriser (public domain / MIT; THIRD_PARTY_NOTICES.txt). This file is
// its only user, so it is compiled in here.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb_truetype.h"

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------
#define HD_FONT_FILE_MAX  (4 * 1024 * 1024)  // Archivo Black 89 KB, Narrow 90 KB
#define HD_FONT_MAX       8

struct HdFont {
    char key[160];
    unsigned char* file;     // stb_truetype reads the file in place
    stbtt_fontinfo info;
    int capHeight;           // font units, from the H's box
    int usable;
};

static HdFont s_fonts[HD_FONT_MAX];
static int    s_fontCount = 0;

static void hd8_release(void);
static void hd14_release(void);
static void hd14_note_page(const unsigned int* rgba, int width, int height);

// The key the HD layer names the BASE tree's copy of `assetPath` by, whatever
// overlay the current mode reads from: one font file serves every mode.
static void base_key(const char* assetPath, char* key, size_t keySize)
{
    char savedOverlay[16];
    strncpy(savedOverlay, GetAssetModeName(), sizeof(savedOverlay) - 1);
    savedOverlay[sizeof(savedOverlay) - 1] = '\0';
    SetAssetMode("");
    HdTextures_KeyForPath(assetPath, key, keySize);
    SetAssetMode(savedOverlay);
}

// stb_truetype does not bounds-check a font, so a file is trusted the way the
// HD layer trusts its pictures; only the obvious is refused.
static int font_open(HdFont* f, const char* path)
{
    size_t n = 0;
    unsigned char* file = (unsigned char*)plat_file_read_all(path, &n);
    if (file == NULL) return 0;
    const int offset = (n >= 12 && n <= HD_FONT_FILE_MAX) ? stbtt_GetFontOffsetForIndex(file, 0) : -1;
    int x0, y0, x1, y1;
    if (offset < 0 || !stbtt_InitFont(&f->info, file, offset) ||
        stbtt_FindGlyphIndex(&f->info, 'H') == 0 ||
        !stbtt_GetCodepointBox(&f->info, 'H', &x0, &y0, &x1, &y1) || y1 <= 0) {
        dbg_printf("[HDTEXT] %s is not a usable TrueType font - the pixel font stays\n", path);
        free(file);
        return 0;
    }
    f->file = file;
    f->capHeight = y1;
    dbg_printf("[HDTEXT] HD font: %s\n", path);
    return 1;
}

// `ext` follows the file's name: ".ttf", or "_14.ttf" for the 8x14 font, which
// shares fontus.tim / FONT.TIM with the 8x8 one.
static const HdFont* font_for(const char* assetPath, const char* ext)
{
    if (assetPath == NULL || !HdTextures_Active()) return NULL;
    char key[160];
    base_key(assetPath, key, sizeof(key));
    if (key[0] == '\0') return NULL;
    char name[sizeof(((HdFont*)0)->key)];
    snprintf(name, sizeof(name), "%s%s", key, ext);
    for (int i = 0; i < s_fontCount; i++) {
        if (strcmp(s_fonts[i].key, name) == 0) return s_fonts[i].usable ? &s_fonts[i] : NULL;
    }
    if (s_fontCount >= HD_FONT_MAX) return NULL;

    // Remembered either way, so a missing file is looked for once.
    HdFont* f = &s_fonts[s_fontCount++];
    memset(f, 0, sizeof(*f));
    strncpy(f->key, name, sizeof(f->key) - 1);
    char path[300];
    if (HdTextures_NamedFile(key, ext, path, sizeof(path))) f->usable = font_open(f, path);
    return f->usable ? f : NULL;
}

const HdFont* HdFont_ForAsset(const char* assetPath)
{
    return font_for(assetPath, ".ttf");
}

// The game's font sheet: data\fontus.tim, data\FONT.TIM on the Japanese tree.
static const char* font_sheet_path(void)
{
    return GetAssetVersion() != 0 ? GAME_DATA_ROOT "data\\FONT.TIM"
                                  : GAME_DATA_ROOT "data\\fontus.tim";
}

const HdFont* HdFont_Text(void)
{
    return font_for(font_sheet_path(), ".ttf");
}

const HdFont* HdFont_Text14(void)
{
    return font_for(font_sheet_path(), "_14.ttf");
}

void HdFont_ForgetAll(void)
{
    hd8_release();
    hd14_release();
    for (int i = 0; i < s_fontCount; i++) free(s_fonts[i].file);
    memset(s_fonts, 0, sizeof(s_fonts));
    s_fontCount = 0;
}

int HdFont_Glyph(const HdFont* font, int codepoint)
{
    return font != NULL ? stbtt_FindGlyphIndex(&font->info, codepoint) : 0;
}

int HdFont_CapHeight(const HdFont* font)
{
    return font != NULL ? font->capHeight : 0;
}

bool HdFont_GlyphBox(const HdFont* font, int glyph, int* x0, int* y0, int* x1, int* y1)
{
    return font != NULL && glyph != 0 && stbtt_GetGlyphBox(&font->info, glyph, x0, y0, x1, y1) != 0;
}

int HdFont_Advance(const HdFont* font, int glyph)
{
    int advance = 0, lsb = 0;
    if (font != NULL && glyph != 0) stbtt_GetGlyphHMetrics(&font->info, glyph, &advance, &lsb);
    return advance;
}

int HdFont_Kern(const HdFont* font, int glyph, int next)
{
    if (font == NULL || glyph == 0 || next == 0) return 0;
    return stbtt_GetGlyphKernAdvance(&font->info, glyph, next);
}

int HdFont_AddGlyph(const HdFont* font, int glyph, float fs, float x, float baseline,
                    unsigned char* cov, int w, int h)
{
    if (font == NULL || glyph == 0) return 1;
    const int ix = (int)floorf(x);
    const int iy = (int)floorf(baseline);
    const float fx = x - (float)ix;
    const float fy = baseline - (float)iy;
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBoxSubpixel(&font->info, glyph, fs, fs, fx, fy, &x0, &y0, &x1, &y1);
    const int bw = x1 - x0;
    const int bh = y1 - y0;
    if (bw <= 0 || bh <= 0) return 1;
    unsigned char* bmp = (unsigned char*)malloc((size_t)bw * (size_t)bh);
    if (bmp == NULL) return 0;
    stbtt_MakeGlyphBitmapSubpixel(&font->info, bmp, bw, bh, bw, fs, fs, fx, fy, glyph);
    // Neighbours can share a pixel: their coverage adds up.
    for (int by = 0; by < bh; by++) {
        const int r = iy + y0 + by;
        if (r < 0 || r >= h) continue;
        unsigned char* line = cov + (size_t)r * (size_t)w;
        for (int bx = 0; bx < bw; bx++) {
            const int c = ix + x0 + bx;
            if (c < 0 || c >= w) continue;
            const int sum = line[c] + bmp[by * bw + bx];
            line[c] = (unsigned char)(sum > 255 ? 255 : sum);
        }
    }
    free(bmp);
    return 1;
}

// ---------------------------------------------------------------------------
// Pictures
// ---------------------------------------------------------------------------
#define HD_TEXT_SCALE_MAX 8

int HdText_Scale(void)
{
    int w = 0, h = 0;
    int scale = 4;
    if (plat_display_desktop_size(&w, &h) && h > 0) scale = (h + 239) / 240;
    if (scale < 2) scale = 2;
    if (scale > HD_TEXT_SCALE_MAX) scale = HD_TEXT_SCALE_MAX;
    return scale;
}

bool HdText_Tim4ToRgba(const void* tim, unsigned int* out, int maxTexels,
                       int* width, int* height)
{
    const unsigned char* t = (const unsigned char*)tim;
    if (t == NULL || out == NULL) return false;
    unsigned int magic, flags;
    memcpy(&magic, t, 4);
    memcpy(&flags, t + 4, 4);
    if (magic != 0x10 || flags != 0x08) return false;      // 4bpp with a CLUT
    unsigned int clutLen;
    memcpy(&clutLen, t + 8, 4);
    if (clutLen < 12 + 32) return false;
    unsigned short clut[16];
    memcpy(clut, t + 20, sizeof(clut));                     // the first row
    const unsigned char* img = t + 8 + clutLen;
    unsigned short w16, h;
    memcpy(&w16, img + 8, 2);
    memcpy(&h, img + 10, 2);
    const int w = w16 * 4;
    if (w <= 0 || h == 0 || w * (int)h > maxTexels) return false;

    // As LoadTexturePage: index 0 is the key when it is black (with or without
    // STP); every other entry is opaque, its STP bit ignored.
    const bool indexZeroIsKey = (clut[0] == 0x0000 || clut[0] == 0x8000);
    unsigned int rgba[16];
    for (int i = 0; i < 16; i++) {
        const unsigned short c = clut[i];
        const unsigned int r = ((c >> 0)  & 0x1F) * 255 / 31;
        const unsigned int g = ((c >> 5)  & 0x1F) * 255 / 31;
        const unsigned int b = ((c >> 10) & 0x1F) * 255 / 31;
        const unsigned int a = indexZeroIsKey ? (i == 0 ? 0x00 : 0xFF)
                                              : ((c & 0x7FFF) == 0 ? 0x00 : 0xFF);
        rgba[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }
    const unsigned char* pix = img + 12;
    for (int y = 0; y < (int)h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned char p = pix[y * (w / 2) + (x >> 1)];
            out[y * w + x] = rgba[(x & 1) ? (p >> 4) : (p & 0x0F)];
        }
    }
    *width = w;
    *height = (int)h;
    return true;
}

void HdText_Paint(const unsigned char* cov, int w, int h, unsigned int colour,
                  unsigned int* dst, int dstW, int dstH, int x, int y)
{
    for (int r = 0; r < h; r++) {
        const int dy = y + r;
        if (dy < 0 || dy >= dstH) continue;
        const unsigned char* a = cov + (size_t)r * (size_t)w;
        unsigned int* out = dst + (size_t)dy * (size_t)dstW;
        for (int c = 0; c < w; c++) {
            const int dx = x + c;
            if (a[c] == 0 || dx < 0 || dx >= dstW) continue;
            out[dx] = (colour & 0x00FFFFFFu) | ((unsigned int)a[c] << 24);
        }
    }
}

// ---------------------------------------------------------------------------
// The HD 8x8 font
//
// The glyphs are rasterised once into an atlas texture at HdText_Scale(), in
// the 8x8 sheet's own glyph colour (AddTintSprite tints it the same way), and
// every character is a sprite of its own from it.
//
// Size: HD8_CAP rows of the 8-row cell for the capitals, centred on the
// sheet's 7-row capitals; descenders hang below the cell, which the sheet's
// glyphs could not.
//
// Layout: callers lay text out in 8-pixel cells - right-aligned values
// (`288 - strlen * 8`), one character per call in a colour of its own, the lab
// terminal's caret on a cell - so a word keeps its cells' span: its ink starts
// HD8_MARGIN in from the first cell and ends HD8_MARGIN short of the last, the
// gaps between letters even (at least HD8_MIN_GAP). A lone character sits in
// the middle of its cell, and spaces stay a cell wide. Archivo Narrow is far
// narrower than the 8x8 font, so the letters come out spaced.
//
// The sheet's cells: ASCII from 0x20, with the sheet's two own glyphs at 0x40
// (a copyright sign) and 0x5C (a yen sign) and 0x60 blank.
// ---------------------------------------------------------------------------
#define HD8_CAP          6.5f   // capital height, in game pixels
#define HD8_SHEET_CAP    7.0f   // the 8x8 sheet's own capitals: rows 0-6
#define HD8_MARGIN       1.0f
#define HD8_MIN_GAP      0.6f
#define HD8_FIRST        0x21
#define HD8_LAST         0x7E
#define HD8_ATLAS_W      1024
#define HD8_PAD          2
#define HD8_ATLAS_MAX    4      // one atlas per size in use (HdText_SetPrint8x8Size)

struct Hd8Glyph {
    int present;
    float u0, v0, u1, v1;    // atlas texture coordinates
    float x, y, w, h;        // the bitmap in game pixels, from the origin/baseline
    float inkL, inkR;        // the ink's extent, game pixels from the origin
};

// The glyphs rasterised at one size, each atlas at the size it is drawn at so
// a smaller text is not a point-sampled shrink of the normal one.
struct Hd8Atlas {
    float size;              // of the normal HD size (1 = HD8_CAP capitals)
    int state;               // 0 not built, 1 ready, -1 unusable
    int scale;               // HdText_Scale() it was built at
    unsigned int colour;     // glyph colour it was built in
    MarniHandle tex;
    Hd8Glyph glyphs[HD8_LAST + 1];
};

static Hd8Atlas     s_hd8[HD8_ATLAS_MAX];
static int          s_hd8Count = 0;
static unsigned int s_hd8Colour = 0x00CDDEDEu;  // fontus.tim's 8x8 glyphs (222,222,205)
static float        s_hd8Size = 1.0f;           // HdText_SetPrint8x8Size
static int          s_hd8Anchor = HD_TEXT_ANCHOR_LEFT;

static void hd8_release(void)
{
    for (int i = 0; i < s_hd8Count; i++) {
        if (s_hd8[i].tex != MARNI_NULL_HANDLE && Marni_DX() != NULL) {
            Marni_DX()->DestroyTexture(s_hd8[i].tex);
        }
    }
    memset(s_hd8, 0, sizeof(s_hd8));
    s_hd8Count = 0;
}

void HdText_NoteFontPage(const unsigned int* rgba, int width, int height)
{
    if (rgba == NULL || width < 256 || height < 24) return;
    // The most common opaque colour of the 8x8 region (u 0-255, v 0-23).
    unsigned int colours[8];
    int counts[8];
    int used = 0;
    for (int y = 0; y < 24; y++) {
        for (int x = 0; x < 256; x++) {
            const unsigned int p = rgba[y * width + x];
            if ((p >> 24) == 0) continue;
            int k = 0;
            while (k < used && colours[k] != p) k++;
            if (k == used) {
                if (used == 8) continue;
                colours[used] = p;
                counts[used++] = 0;
            }
            counts[k]++;
        }
    }
    int best = -1;
    for (int k = 0; k < used; k++) {
        if (best < 0 || counts[k] > counts[best]) best = k;
    }
    if (best >= 0) s_hd8Colour = colours[best] & 0x00FFFFFFu;
    hd14_note_page(rgba, width, height);
}

static int hd8_codepoint(int ch)
{
    if (ch == 0x40) return 0xA9;   // the sheet's (c)
    if (ch == 0x5C) return 0xA5;   // and its yen sign
    if (ch == 0x60) return 0;      // blank in the sheet
    return ch;
}

static int hd8_build(Hd8Atlas* a, int scale)
{
    const HdFont* font = HdFont_Text();
    if (font == NULL) return 0;
    const float fs = HD8_CAP * a->size * (float)scale / (float)font->capHeight;

    // Shelf-pack the bitmaps.
    struct Box { int g, x, y, w, h, x0, y0; };
    Box boxes[HD8_LAST + 1];
    memset(boxes, 0, sizeof(boxes));
    int penX = HD8_PAD, penY = HD8_PAD, shelf = 0;
    for (int ch = HD8_FIRST; ch <= HD8_LAST; ch++) {
        const int cp = hd8_codepoint(ch);
        const int g = cp != 0 ? stbtt_FindGlyphIndex(&font->info, cp) : 0;
        int x0, y0, x1, y1;
        if (g == 0 || !stbtt_GetGlyphBox(&font->info, g, &x0, &y0, &x1, &y1)) continue;
        stbtt_GetGlyphBitmapBox(&font->info, g, fs, fs, &x0, &y0, &x1, &y1);
        const int w = x1 - x0, h = y1 - y0;
        if (w <= 0 || h <= 0 || w + 2 * HD8_PAD > HD8_ATLAS_W) continue;
        if (penX + w + HD8_PAD > HD8_ATLAS_W) {
            penX = HD8_PAD;
            penY += shelf + HD8_PAD;
            shelf = 0;
        }
        Box& b = boxes[ch];
        b.g = g; b.x = penX; b.y = penY; b.w = w; b.h = h; b.x0 = x0; b.y0 = y0;
        penX += w + HD8_PAD;
        if (h > shelf) shelf = h;
    }
    const int atlasH = penY + shelf + HD8_PAD;
    unsigned int* rgba = (unsigned int*)malloc(sizeof(unsigned int) * HD8_ATLAS_W * (size_t)atlasH);
    unsigned char* cov = (unsigned char*)calloc((size_t)HD8_ATLAS_W * (size_t)atlasH, 1);
    if (rgba == NULL || cov == NULL) {
        free(rgba);
        free(cov);
        return 0;
    }
    memset(a->glyphs, 0, sizeof(a->glyphs));
    for (int ch = HD8_FIRST; ch <= HD8_LAST; ch++) {
        const Box& b = boxes[ch];
        if (b.g == 0) continue;
        stbtt_MakeGlyphBitmap(&font->info, cov + (size_t)b.y * HD8_ATLAS_W + b.x,
                              b.w, b.h, HD8_ATLAS_W, fs, fs, b.g);
        int gx0, gy0, gx1, gy1;
        stbtt_GetGlyphBox(&font->info, b.g, &gx0, &gy0, &gx1, &gy1);
        Hd8Glyph& o = a->glyphs[ch];
        o.present = 1;
        o.u0 = (float)b.x / (float)HD8_ATLAS_W;
        o.v0 = (float)b.y / (float)atlasH;
        o.u1 = (float)(b.x + b.w) / (float)HD8_ATLAS_W;
        o.v1 = (float)(b.y + b.h) / (float)atlasH;
        o.x = (float)b.x0 / (float)scale;
        o.y = (float)b.y0 / (float)scale;
        o.w = (float)b.w / (float)scale;
        o.h = (float)b.h / (float)scale;
        o.inkL = (float)gx0 * fs / (float)scale;
        o.inkR = (float)gx1 * fs / (float)scale;
    }
    // White space keeps the glyph colour, so nothing darkens at the edges.
    for (size_t i = 0; i < (size_t)HD8_ATLAS_W * (size_t)atlasH; i++) {
        rgba[i] = s_hd8Colour | ((unsigned int)cov[i] << 24);
    }
    MarniHandle tex = MARNI_NULL_HANDLE;
    const BOOL ok = MarniCreateTextureRaw(HD8_ATLAS_W, atlasH, rgba, &tex);
    free(cov);
    free(rgba);
    if (!ok) return 0;
    a->tex = tex;
    a->scale = scale;
    a->colour = s_hd8Colour;
    dbg_printf("[HDTEXT] 8x8 font atlas %dx%d at %dx, size %.2f\n", HD8_ATLAS_W, atlasH,
               scale, (double)a->size);
    return 1;
}

// The atlas for the current size, built on first use; NULL without an HD font.
static Hd8Atlas* hd8_ready(void)
{
    if (!HdTextures_Active()) return NULL;
    // A frame prints many lines; the desktop is asked about once per frame.
    static int checkedFrame = -1;
    static int scale = 0;
    if (checkedFrame != g_numFramesPresented || scale == 0) {
        checkedFrame = g_numFramesPresented;
        scale = HdText_Scale();
        int stale = 0;
        for (int i = 0; i < s_hd8Count; i++) {
            if (s_hd8[i].state == 1 && (s_hd8[i].scale != scale || s_hd8[i].colour != s_hd8Colour)) {
                stale = 1;
            }
        }
        if (stale) hd8_release();
    }
    Hd8Atlas* a = NULL;
    for (int i = 0; i < s_hd8Count && a == NULL; i++) {
        if (s_hd8[i].size == s_hd8Size) a = &s_hd8[i];
    }
    if (a == NULL) {
        if (s_hd8Count >= HD8_ATLAS_MAX) return NULL;
        a = &s_hd8[s_hd8Count++];
        memset(a, 0, sizeof(*a));
        a->size = s_hd8Size;
    }
    if (a->state == 0) a->state = hd8_build(a, scale) ? 1 : -1;
    return a->state == 1 ? a : NULL;
}

void HdText_SetPrint8x8Size(float size, int anchor)
{
    s_hd8Size = (size > 0.0f) ? size : 1.0f;
    s_hd8Anchor = anchor;
}

// One character from the 8x8 sheet in its cell, as PrintText8x8 draws it.
static void hd8_sheet_cell(unsigned char ch, short screenX, unsigned short brightness)
{
    g_TextureDesc.screenX = screenX;
    g_TextureDesc.texU = (unsigned char)(ch * 8);
    g_TextureDesc.texV = (unsigned char)(((ch - 0x20) & 0xE3) >> 2);
    AddTintSprite(&g_TextureDesc, brightness);
}

int HdText_Print8x8(short x, short y, unsigned short brightness)
{
    Hd8Atlas* a = hd8_ready();
    if (a == NULL) return 0;
    const Hd8Glyph* glyphs = a->glyphs;

    const char* text = PRINT_TEXT_BUFFER;
    const int len = (int)strlen(text);
    // The cell layout is worked out at full size, then drawn together towards
    // the anchor by the size (1 draws it as laid out).
    const float size = a->size;
    float anchor = (float)x;
    if (s_hd8Anchor == HD_TEXT_ANCHOR_CENTRE) anchor = (float)x + (float)len * 4.0f;
    if (s_hd8Anchor == HD_TEXT_ANCHOR_RIGHT) anchor = (float)x + (float)len * 8.0f;
    const float cap = HD8_CAP * size;
    const float baseline = (float)y + (HD8_SHEET_CAP - cap) * 0.5f + cap;
    const short cellX0 = g_TextureDesc.screenX;   // x - g_ScreenOffsetX
    int i = 0;
    while (text[i] != '\0') {
        if (text[i] == ' ') {
            i++;
            continue;
        }
        int j = i;
        int sheetOnly = 0;
        float ink = 0.0f;
        while (text[j] != '\0' && text[j] != ' ') {
            const unsigned char ch = (unsigned char)text[j];
            if (ch < HD8_FIRST || ch > HD8_LAST || !glyphs[ch].present) sheetOnly = 1;
            else ink += (glyphs[ch].inkR - glyphs[ch].inkL) / size;
            j++;
        }
        const int count = j - i;
        const float span0 = (float)x + (float)(i * 8);
        const float span1 = (float)x + (float)(j * 8);

        if (sheetOnly) {
            // A character the font lacks: the whole word in cells, the HD
            // glyphs centred in theirs.
            for (int k = i; k < j; k++) {
                const unsigned char ch = (unsigned char)text[k];
                if (ch < HD8_FIRST || ch > HD8_LAST || !glyphs[ch].present) {
                    hd8_sheet_cell(ch, (short)(cellX0 + k * 8), brightness);
                    continue;
                }
                const Hd8Glyph& g = glyphs[ch];
                const float centre = anchor + ((float)x + (float)(k * 8) + 4.0f - anchor) * size;
                const float ox = centre - (g.inkL + g.inkR) * 0.5f;
                AddTintSpriteRect(&g_TextureDesc, brightness, ox + g.x, baseline + g.y, g.w, g.h,
                                  g.u0, g.v0, g.u1, g.v1, a->tex);
            }
            i = j;
            continue;
        }

        // Full-size layout: ink widths at size 1, the gaps that fill the span.
        float gap = 0.0f;
        if (count > 1) {
            gap = (span1 - span0 - 2.0f * HD8_MARGIN - ink) / (float)(count - 1);
            if (gap < HD8_MIN_GAP) gap = HD8_MIN_GAP;
        }
        float pen = (span0 + span1) * 0.5f - (ink + gap * (float)(count - 1)) * 0.5f;
        for (int k = i; k < j; k++) {
            const Hd8Glyph& g = glyphs[(unsigned char)text[k]];
            const float inkLeft = anchor + (pen - anchor) * size;
            const float ox = inkLeft - g.inkL;
            AddTintSpriteRect(&g_TextureDesc, brightness, ox + g.x, baseline + g.y, g.w, g.h,
                              g.u0, g.v0, g.u1, g.v1, a->tex);
            pen += (g.inkR - g.inkL) / size + gap;
        }
        i = j;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// The HD 8x14 font
//
// The game text - messages, item names, files, the save screen - is drawn
// from the font sheet's 14-row cells: 8 wide on the USA fontus.tim (rows from
// v 28, PrintText.cpp's table), 14 wide on the Japanese FONT.TIM (left page
// rows from v 28, the kanji page from v 0; JpnFontTable.h). Every renderer
// ends in AddTintSprite, which asks HdText_Glyph14 whether the cell it draws is
// a character the HD font has; the queue then lays those sprites out
// (HdText_Layout14) before it sorts. Cells the font has no character for -
// the controller buttons, the ‽ - stay the sheet's.
//
// Each glyph is drawn the way the sheet draws its letters: a fill with a dark
// outline around it, both in the sheet's own colours (HdText_NoteFontPage
// finds them: the outline is the colour that borders the clear texels, the
// fill the one inside), so a tint multiplies them as it multiplies the sheet.
// The outline is the glyph's coverage grown by HD14_OUTLINE game pixels.
//
// Latin (USA): capitals HD14_CAP tall on the sheet's 10-row ones. A run of
// cells that follow each other, or are one blank cell apart, is set at the
// font's own advances, kerning and space from where its first cell starts; a
// gap of two or more cells (YES  NO, columns) starts the next run at its own
// cell. A blank cell some other glyph fills does not join (hd14_joins). A run
// with no letter in it (dashes, numbers, '/') keeps one glyph centred in each
// cell, so the save screen's columns line up. A letter's place depends only on
// the ones before it, so the message box's typing does not move what is
// already typed.
// Japanese: every glyph centred in its own 14-pixel cell, at HD14_JPN_EM.
// ---------------------------------------------------------------------------
#define HD14_CAP         7.2f    // Latin capitals, game pixels (the sheet's: 10)
#define HD14_SHEET_TOP   1.0f    // the sheet's capitals: rows 1-10 of the cell
#define HD14_SHEET_CAP   10.0f
#define HD14_RUN_INSET   0.5f    // a run's first letter starts this far into its cell
#define HD14_JPN_EM      10.0f   // Japanese em, game pixels (the cell: 14)
#define HD14_JPN_ASCENT  0.88f   // of the em above the baseline
#define HD14_OUTLINE     0.65f    // outline width, game pixels
#define HD14_ATLAS_W     2048
#define HD14_CELLS       2048    // page * 1024 + row * 18 + column

// The USA sheet's characters, index = row * 18 + column from v 28. 0 = none
// (blank, or a controller button the sheet keeps). Two characters share a
// cell in the S. T. A. R. ligatures and the !? one.
static const unsigned short kUsa14[144][2] = {
    // row 0: blank blank, buttons x10, 0-5
    {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
    {'0',0},{'1',0},{'2',0},{'3',0},{'4',0},{'5',0},
    // row 1: 6-9 : ; , " ! ? !? A-G
    {'6',0},{'7',0},{'8',0},{'9',0},{':',0},{';',0},{',',0},{0x201D,0},{'!',0},{'?',0},
    {'!','?'},{'A',0},{'B',0},{'C',0},{'D',0},{'E',0},{'F',0},{'G',0},
    // row 2: H-Y
    {'H',0},{'I',0},{'J',0},{'K',0},{'L',0},{'M',0},{'N',0},{'O',0},{'P',0},
    {'Q',0},{'R',0},{'S',0},{'T',0},{'U',0},{'V',0},{'W',0},{'X',0},{'Y',0},
    // row 3: Z ( / ) ' - middle dot, a-k
    {'Z',0},{'(',0},{'/',0},{')',0},{0x2019,0},{'-',0},{0xB7,0},{'a',0},{'b',0},
    {'c',0},{'d',0},{'e',0},{'f',0},{'g',0},{'h',0},{'i',0},{'j',0},{'k',0},
    // row 4: l-z, Ä ä Ö
    {'l',0},{'m',0},{'n',0},{'o',0},{'p',0},{'q',0},{'r',0},{'s',0},{'t',0},
    {'u',0},{'v',0},{'w',0},{'x',0},{'y',0},{'z',0},{0xC4,0},{0xE4,0},{0xD6,0},
    // row 5
    {0xF6,0},{0xDC,0},{0xFC,0},{0xDF,0},{0xC0,0},{0xE0,0},{0xC2,0},{0xE2,0},{0xC8,0},
    {0xE8,0},{0xC9,0},{0xE9,0},{0xCA,0},{0xEA,0},{0xCF,0},{0xEF,0},{0xCE,0},{0xEE,0},
    // row 6: Ô ô Ù ù Û û Ç ç S. T. A. R. " . ... and the two halves of a long dash, +
    {0xD4,0},{0xF4,0},{0xD9,0},{0xF9,0},{0xDB,0},{0xFB,0},{0xC7,0},{0xE7,0},
    {'S','.'},{'T','.'},{'A','.'},{'R','.'},{0x201C,0},{'.',0},{0x2026,0},
    {0x2014,0},{0x2014,0},{'+',0},
    // row 7: =
    {'=',0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
    {0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},
};

struct Hd14Glyph {
    int first, last;          // font glyphs, for kerning against the neighbours
    float u0, v0, u1, v1;     // atlas texture coordinates
    float x, y, w, h;         // the bitmap in game pixels, from the origin/baseline
    float advance;            // game pixels
    int letter;               // a letter (a run without one keeps the cells)
};

static int hd14_is_letter(unsigned short cp)
{
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return 1;
    return cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7;
}

static Hd14Glyph*   s_hd14 = NULL;
static int          s_hd14Count = 0;
static short        s_hd14Cell[HD14_CELLS];
static unsigned char s_hd14Blank[HD14_CELLS];   // the sheet's empty (space) cells
static MarniHandle  s_hd14Tex = MARNI_NULL_HANDLE;
static int          s_hd14State = 0;     // 0 not built, 1 ready, -1 unusable
static int          s_hd14Scale = 0;
static int          s_hd14Jpn = 0;       // built for the Japanese sheet
static float        s_hd14Space = 3.0f;  // the font's space, game pixels
static float        s_hd14Kern = 0.0f;   // font units -> game pixels
static unsigned int s_hd14Fill = 0x00CDDEDEu;     // fontus.tim (222,222,205)
static unsigned int s_hd14Outline = 0x004A3131u;  // fontus.tim (49,49,74)
static unsigned int s_hd14BuiltFill = 0, s_hd14BuiltOutline = 0;
static const HdFont* s_hd14Font = NULL;

static void hd14_release(void)
{
    if (s_hd14Tex != MARNI_NULL_HANDLE && Marni_DX() != NULL) Marni_DX()->DestroyTexture(s_hd14Tex);
    s_hd14Tex = MARNI_NULL_HANDLE;
    free(s_hd14);
    s_hd14 = NULL;
    s_hd14Count = 0;
    s_hd14State = 0;
    s_hd14Font = NULL;
}

// The sheet's 14-row region: its outline colour is the commonest one next to a
// clear texel, its fill the commonest one that is not.
static void hd14_note_page(const unsigned int* rgba, int width, int height)
{
    unsigned int colours[2][16];
    int counts[2][16];
    int used[2] = { 0, 0 };
    const int x1 = width < 256 ? width : 256;
    for (int y = 29; y + 1 < height; y++) {
        for (int x = 1; x + 1 < x1; x++) {
            const unsigned int p = rgba[y * width + x];
            if ((p >> 24) == 0) continue;
            const int edge = (rgba[y * width + x - 1] >> 24) == 0 || (rgba[y * width + x + 1] >> 24) == 0 ||
                             (rgba[(y - 1) * width + x] >> 24) == 0 || (rgba[(y + 1) * width + x] >> 24) == 0;
            int k = 0;
            while (k < used[edge] && colours[edge][k] != p) k++;
            if (k == used[edge]) {
                if (used[edge] == 16) continue;
                colours[edge][k] = p;
                counts[edge][used[edge]++] = 0;
            }
            counts[edge][k]++;
        }
    }
    for (int e = 0; e < 2; e++) {
        int best = -1;
        for (int k = 0; k < used[e]; k++) {
            if (best < 0 || counts[e][k] > counts[e][best]) best = k;
        }
        if (best < 0) continue;
        if (e) s_hd14Outline = colours[e][best] & 0x00FFFFFFu;
        else   s_hd14Fill = colours[e][best] & 0x00FFFFFFu;
    }
}

// Grows `src` (w x h coverage) by a disc of radius r into `dst`: the widest
// horizontal maximum each row of the disc needs, built up one pixel at a time,
// then the maximum over the disc's rows.
static int hd14_dilate(const unsigned char* src, unsigned char* dst, int w, int h, int r)
{
    const size_t n = (size_t)w * (size_t)h;
    unsigned char* rows = (unsigned char*)malloc(n * (size_t)(r + 1));
    if (rows == NULL) return 0;
    memcpy(rows, src, n);
    for (int k = 1; k <= r; k++) {
        const unsigned char* prev = rows + n * (size_t)(k - 1);
        unsigned char* cur = rows + n * (size_t)k;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                unsigned char m = prev[y * w + x];
                if (x > 0 && prev[y * w + x - 1] > m) m = prev[y * w + x - 1];
                if (x + 1 < w && prev[y * w + x + 1] > m) m = prev[y * w + x + 1];
                cur[y * w + x] = m;
            }
        }
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned char m = 0;
            for (int dy = -r; dy <= r; dy++) {
                const int yy = y + dy;
                if (yy < 0 || yy >= h) continue;
                const int half = (int)floorf(sqrtf((float)(r * r - dy * dy)) + 0.5f);
                const unsigned char v = rows[n * (size_t)half + (size_t)yy * (size_t)w + (size_t)x];
                if (v > m) m = v;
            }
            dst[y * w + x] = m;
        }
    }
    free(rows);
    return 1;
}

// One cell's characters (one, or two in a ligature cell).
struct Hd14Cell {
    int key;
    unsigned short cp[2];
};

static int hd14_cells(int jpn, Hd14Cell* out, int max)
{
    int n = 0;
    if (!jpn) {
        for (int i = 0; i < 144 && n < max; i++) {
            if (kUsa14[i][0] == 0) continue;
            out[n].key = i;
            out[n].cp[0] = kUsa14[i][0];
            out[n].cp[1] = kUsa14[i][1];
            n++;
        }
        return n;
    }
    // The Japanese table: one byte = left page row b / 18; F8 nn = left page
    // rows 13-15; F9 nn = the kanji page rows 0-13, FA nn rows 14-17.
    for (size_t i = 0; i < sizeof(pft_detail::kJpnGlyphs) / sizeof(pft_detail::kJpnGlyphs[0]) && n < max; i++) {
        const pft_detail::JpnGlyph& g = pft_detail::kJpnGlyphs[i];
        if (g.cp == 0x20) continue;
        int page = 0, row, col;
        if (g.n == 1)            { row = g.b0 / 18;      col = g.b0 % 18; }
        else if (g.b0 == 0xF8)   { row = g.b1 / 18 + 13; col = g.b1 % 18; }
        else if (g.b0 == 0xF9)   { page = 1; row = g.b1 / 18;      col = g.b1 % 18; }
        else if (g.b0 == 0xFA)   { page = 1; row = g.b1 / 18 + 14; col = g.b1 % 18; }
        else continue;
        out[n].key = page * 1024 + row * 18 + col;
        out[n].cp[0] = g.cp;
        out[n].cp[1] = 0;
        n++;
    }
    return n;
}

static int hd14_build(void)
{
    const HdFont* font = HdFont_Text14();
    if (font == NULL) return 0;
    const int jpn = GetAssetVersion() != 0;
    const int scale = HdText_Scale();
    const float fs = jpn ? stbtt_ScaleForMappingEmToPixels(&font->info, HD14_JPN_EM * (float)scale)
                         : HD14_CAP * (float)scale / (float)font->capHeight;
    int r = (int)floorf(HD14_OUTLINE * (float)scale + 0.5f);
    if (r < 1) r = 1;
    const int pad = r + 1;

    Hd14Cell* cells = (Hd14Cell*)malloc(sizeof(Hd14Cell) * 1024);
    Hd14Glyph* glyphs = (Hd14Glyph*)calloc(1024, sizeof(Hd14Glyph));
    struct Box { int x, y, w, h, x0, y0; int g[2]; float pen[2]; };
    Box* boxes = (Box*)calloc(1024, sizeof(Box));
    if (cells == NULL || glyphs == NULL || boxes == NULL) {
        free(cells); free(glyphs); free(boxes);
        return 0;
    }
    const int count = hd14_cells(jpn, cells, 1024);

    // Lay each cell's characters out, measure their bitmaps, shelf-pack them.
    int penX = 0, penY = 0, shelf = 0, kept = 0;
    for (int i = 0; i < count; i++) {
        Box& b = boxes[kept];
        Hd14Glyph& o = glyphs[kept];
        float pen = 0.0f;
        int ok = 1, bx0 = 0, by0 = 0, bx1 = 0, by1 = 0, any = 0;
        for (int k = 0; k < 2; k++) {
            b.g[k] = 0;
            if (cells[i].cp[k] == 0) continue;
            const int g = stbtt_FindGlyphIndex(&font->info, cells[i].cp[k]);
            if (g == 0) { ok = 0; break; }
            if (k == 1) pen += (float)stbtt_GetGlyphKernAdvance(&font->info, b.g[0], g) * fs;
            b.g[k] = g;
            b.pen[k] = pen;
            int x0, y0, x1, y1;
            stbtt_GetGlyphBitmapBoxSubpixel(&font->info, g, fs, fs, pen - floorf(pen), 0.0f,
                                            &x0, &y0, &x1, &y1);
            x0 += (int)floorf(pen);
            x1 += (int)floorf(pen);
            if (x1 > x0 && y1 > y0) {
                if (!any || x0 < bx0) bx0 = x0;
                if (!any || y0 < by0) by0 = y0;
                if (!any || x1 > bx1) bx1 = x1;
                if (!any || y1 > by1) by1 = y1;
                any = 1;
            }
            int adv = 0, lsb = 0;
            stbtt_GetGlyphHMetrics(&font->info, g, &adv, &lsb);
            pen += (float)adv * fs;
        }
        if (!ok || !any) continue;
        b.x0 = bx0 - pad;
        b.y0 = by0 - pad;
        b.w = bx1 - bx0 + 2 * pad;
        b.h = by1 - by0 + 2 * pad;
        if (b.w > HD14_ATLAS_W) continue;
        if (penX + b.w > HD14_ATLAS_W) {
            penX = 0;
            penY += shelf;
            shelf = 0;
        }
        b.x = penX;
        b.y = penY;
        penX += b.w;
        if (b.h > shelf) shelf = b.h;
        o.first = b.g[0];
        o.last = b.g[1] != 0 ? b.g[1] : b.g[0];
        o.advance = pen / (float)scale;
        o.letter = hd14_is_letter(cells[i].cp[0]) || hd14_is_letter(cells[i].cp[1]);
        cells[kept] = cells[i];
        kept++;
    }
    const int atlasH = penY + shelf;
    unsigned int* rgba = (unsigned int*)calloc((size_t)HD14_ATLAS_W * (size_t)(atlasH > 0 ? atlasH : 1), 4);
    int ok = rgba != NULL && kept > 0;

    // Rasterise each: fill coverage, the outline grown from it, composited.
    const float fr = (float)((s_hd14Fill >> 0) & 0xFF), fg = (float)((s_hd14Fill >> 8) & 0xFF),
                fb = (float)((s_hd14Fill >> 16) & 0xFF);
    const float orr = (float)((s_hd14Outline >> 0) & 0xFF), og = (float)((s_hd14Outline >> 8) & 0xFF),
                ob = (float)((s_hd14Outline >> 16) & 0xFF);
    for (int i = 0; i < kept && ok; i++) {
        const Box& b = boxes[i];
        unsigned char* fill = (unsigned char*)calloc((size_t)b.w * (size_t)b.h, 1);
        unsigned char* line = (unsigned char*)malloc((size_t)b.w * (size_t)b.h);
        if (fill == NULL || line == NULL) {
            free(fill); free(line);
            ok = 0;
            break;
        }
        for (int k = 0; k < 2; k++) {
            if (b.g[k] == 0) continue;
            // As HdFont_AddGlyph, into the box: origin at -x0, baseline at -y0.
            const float gx = b.pen[k] - (float)b.x0;
            const float gy = (float)-b.y0;
            HdFont_AddGlyph(font, b.g[k], fs, gx, gy, fill, b.w, b.h);
        }
        if (!hd14_dilate(fill, line, b.w, b.h, r)) {
            free(fill); free(line);
            ok = 0;
            break;
        }
        for (int y = 0; y < b.h; y++) {
            unsigned int* out = rgba + (size_t)(b.y + y) * HD14_ATLAS_W + b.x;
            for (int x = 0; x < b.w; x++) {
                const float a = (float)fill[y * b.w + x] / 255.0f;
                const float d = (float)line[y * b.w + x] / 255.0f;
                const float alpha = a + d * (1.0f - a);
                if (alpha <= 0.0f) {
                    out[x] = s_hd14Outline;
                    continue;
                }
                const float wo = d * (1.0f - a);
                const unsigned int cr = (unsigned int)((fr * a + orr * wo) / alpha + 0.5f);
                const unsigned int cg = (unsigned int)((fg * a + og * wo) / alpha + 0.5f);
                const unsigned int cb = (unsigned int)((fb * a + ob * wo) / alpha + 0.5f);
                const unsigned int ca = (unsigned int)(alpha * 255.0f + 0.5f);
                out[x] = (ca << 24) | (cb << 16) | (cg << 8) | cr;
            }
        }
        free(fill);
        free(line);
        Hd14Glyph& o = glyphs[i];
        o.u0 = (float)b.x / (float)HD14_ATLAS_W;
        o.v0 = (float)b.y / (float)atlasH;
        o.u1 = (float)(b.x + b.w) / (float)HD14_ATLAS_W;
        o.v1 = (float)(b.y + b.h) / (float)atlasH;
        o.x = (float)b.x0 / (float)scale;
        o.y = (float)b.y0 / (float)scale;
        o.w = (float)b.w / (float)scale;
        o.h = (float)b.h / (float)scale;
    }
    MarniHandle tex = MARNI_NULL_HANDLE;
    if (ok) ok = MarniCreateTextureRaw(HD14_ATLAS_W, atlasH, rgba, &tex) ? 1 : 0;
    free(rgba);
    free(boxes);
    if (!ok) {
        free(cells);
        free(glyphs);
        return 0;
    }
    for (int i = 0; i < HD14_CELLS; i++) s_hd14Cell[i] = -1;
    // The space: row 0's two blank cells (USA), the table's 0x20 (Japanese).
    memset(s_hd14Blank, 0, sizeof(s_hd14Blank));
    if (!jpn) {
        s_hd14Blank[0] = s_hd14Blank[1] = 1;
    } else {
        for (size_t i = 0; i < sizeof(pft_detail::kJpnGlyphs) / sizeof(pft_detail::kJpnGlyphs[0]); i++) {
            const pft_detail::JpnGlyph& g = pft_detail::kJpnGlyphs[i];
            if (g.cp == 0x20 && g.n == 1) s_hd14Blank[(g.b0 / 18) * 18 + g.b0 % 18] = 1;
        }
    }
    for (int i = 0; i < kept; i++) {
        if (cells[i].key >= 0 && cells[i].key < HD14_CELLS) s_hd14Cell[cells[i].key] = (short)i;
    }
    free(cells);
    s_hd14 = glyphs;
    s_hd14Count = kept;
    s_hd14Tex = tex;
    s_hd14Scale = scale;
    s_hd14Jpn = jpn;
    s_hd14Font = font;
    s_hd14Kern = fs / (float)scale;
    const int space = stbtt_FindGlyphIndex(&font->info, ' ');
    if (space != 0) {
        int adv = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&font->info, space, &adv, &lsb);
        s_hd14Space = (float)adv * fs / (float)scale;
    }
    s_hd14BuiltFill = s_hd14Fill;
    s_hd14BuiltOutline = s_hd14Outline;
    dbg_printf("[HDTEXT] 8x14 font atlas %dx%d at %dx: %d of %d cells\n", HD14_ATLAS_W, atlasH,
               scale, kept, count);
    return 1;
}

static int hd14_ready(void)
{
    if (!HdTextures_Active()) return 0;
    static int checkedFrame = -1;
    if (s_hd14State == 1 && checkedFrame != g_numFramesPresented) {
        checkedFrame = g_numFramesPresented;
        if (s_hd14Scale != HdText_Scale() || s_hd14Jpn != (GetAssetVersion() != 0) ||
            s_hd14BuiltFill != s_hd14Fill || s_hd14BuiltOutline != s_hd14Outline) {
            hd14_release();
        }
    }
    if (s_hd14State == 0) s_hd14State = hd14_build() ? 1 : -1;
    return s_hd14State == 1;
}

int HdText_Glyph14(int texU, int texV, int width, int height)
{
    if (height != 14 || !hd14_ready()) return -1;
    int key;
    if (!s_hd14Jpn) {
        if (width != 8 || (texU & 7) != 0 || texU >= 144 || texV < 28 || (texV - 28) % 14 != 0) return -1;
        const int row = (texV - 28) / 14;
        if (row >= 8) return -1;
        key = row * 18 + texU / 8;
    } else {
        if (width != 14 || texU % 14 >= 14) return -1;
        if (texU >= 256) {
            // The kanji page (texturePage 0x1F): rows from v 0.
            const int u = texU - 256;
            if (u % 14 != 0 || u / 14 >= 18 || texV % 14 != 0 || texV / 14 >= 18) return -1;
            key = 1024 + (texV / 14) * 18 + u / 14;
        } else {
            if (texU % 14 != 0 || texU / 14 >= 18 || texV < 28 || (texV - 28) % 14 != 0) return -1;
            const int row = (texV - 28) / 14;
            if (row >= 16) return -1;
            key = row * 18 + texU / 14;
        }
    }
    if (key < 0 || key >= HD14_CELLS) return -1;
    if (s_hd14Blank[key]) return HD_TEXT_BLANK14;
    return s_hd14Cell[key];
}

unsigned int HdText_Glyph14Texture(void)
{
    return s_hd14Tex;
}

// Does glyph i carry on the run of glyph i - 1? Queued straight after it, on
// the same line, in the next cell or one blank cell further - and that cell
// really blank: the save screen draws "CHRIS" and the count over a template
// whose '\' fills the cell between them, and "CHRIS" + "01" is no word.
static int hd14_joins(const HdTextGlyphSprite* s, int n, int i, float cellW)
{
    const HdTextGlyphSprite& a = s[i - 1];
    const HdTextGlyphSprite& b = s[i];
    if (!b.joins || a.id < 0 || a.id >= s_hd14Count || b.id < 0 || b.id >= s_hd14Count) return 0;
    if (fabsf(b.cellY - a.cellY) >= 0.5f) return 0;
    const float gap = (b.cellX - a.cellX) / cellW - 1.0f;
    if (gap < -0.01f || gap > 1.01f) return 0;
    if (gap > 0.5f) {
        const float skipped = a.cellX + cellW;
        for (int k = 0; k < n; k++) {
            if (s[k].id >= 0 && fabsf(s[k].cellY - a.cellY) < 0.5f && fabsf(s[k].cellX - skipped) < 0.5f)
                return 0;
        }
    }
    return 1;
}

void HdText_Layout14(HdTextGlyphSprite* s, int n)
{
    if (s_hd14State != 1) return;
    const float cellW = s_hd14Jpn ? 14.0f : 8.0f;
    const float base = s_hd14Jpn
        ? (14.0f - HD14_JPN_EM) * 0.5f + HD14_JPN_ASCENT * HD14_JPN_EM
        : HD14_SHEET_TOP + (HD14_SHEET_CAP + HD14_CAP) * 0.5f;
    int i = 0;
    while (i < n) {
        if (s[i].id < 0 || s[i].id >= s_hd14Count) { i++; continue; }
        // The run from i: Latin glyphs that join on (Japanese: one per cell).
        int end = i + 1, letters = s_hd14[s[i].id].letter;
        if (!s_hd14Jpn) {
            while (end < n && hd14_joins(s, n, end, cellW)) {
                letters |= s_hd14[s[end].id].letter;
                end++;
            }
        }
        float pen = s[i].cellX + HD14_RUN_INSET;
        for (int k = i; k < end; k++) {
            const Hd14Glyph& g = s_hd14[s[k].id];
            float origin;
            if (s_hd14Jpn || !letters) {
                // One glyph centred in each cell: Japanese, and the runs with
                // no letter in them (the save screen's dashes, numbers, '/').
                origin = s[k].cellX + (cellW - g.advance) * 0.5f;
            } else {
                if (k > i) {
                    const Hd14Glyph& p = s_hd14[s[k - 1].id];
                    const float gap = (s[k].cellX - s[k - 1].cellX) / cellW - 1.0f;
                    pen += p.advance + (float)HdFont_Kern(s_hd14Font, p.last, g.first) * s_hd14Kern +
                           (gap > 0.5f ? s_hd14Space : 0.0f);
                }
                origin = pen;
            }
            s[k].x = origin + g.x;
            s[k].y = s[k].cellY + base + g.y;
            s[k].w = g.w;
            s[k].h = g.h;
            s[k].u0 = g.u0;
            s[k].v0 = g.v0;
            s[k].u1 = g.u1;
            s[k].v1 = g.v1;
        }
        i = end;
    }
}

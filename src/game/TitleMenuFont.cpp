// TitleMenuFont.cpp - the title menu's text in the original option typeface.
// See TitleMenuFont.h. The font file layout is documented in
// tools/asset_migrator/src/core/TitleFont.h.
#include "TitleMenuFont.h"

#include "../Globals.h"
#include "../DebugPrint.h"
#include "FileLoader.h"
#include "../system/AssetPath.h"
#include "../system/HdTextures.h"
#include "../platform/platform.h"
#include "HdText.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// The font
// ---------------------------------------------------------------------------
#define FONT_FILE_MAX     16384        // TITLEFNT.BIN is ~3 KB
#define FONT_MAX_GLYPH    128

struct TitleGlyph {
    const unsigned char* levels;       // width * height, 0 clear / 1 half / 2 full
    int width;
};

static unsigned char s_fontFile[FONT_FILE_MAX];
static TitleGlyph    s_glyphs[FONT_MAX_GLYPH];
static int           s_fontHeight = 0;
static int           s_fontSpacing = 0;
static int           s_fontSpaceAdvance = 0;
static int           s_fontState = 0;     // 0 = not tried, 1 = loaded, -1 = unusable

// LoadFile reports a missing file as (size_t)-1; anything outside the buffer is
// refused the same way.
static int font_read(void)
{
    // Probe first: LoadFile on a missing file is an error path the rest of the
    // engine does not expect to take quietly.
    if (!AssetBaseHasFile(-1, "Data/TITLEFNT.BIN")) return 0;
    const size_t n = LoadFile(GAME_DATA_ROOT "data\\titlefnt.bin", s_fontFile, 0x20);
    return (n != (size_t)-1 && n >= 8 && n <= FONT_FILE_MAX) ? (int)n : 0;
}

void title_font_forget(void)
{
    s_fontState = 0;
    // The HD fonts are named after the tree's files too.
    HdFont_ForgetAll();
}

int title_font_load(void)
{
    if (s_fontState != 0) return s_fontState > 0;
    s_fontState = -1;

    // The base tree's copy, read with no overlay in the way so every mode gets
    // the same file.
    char savedOverlay[16];
    strncpy(savedOverlay, GetAssetModeName(), sizeof(savedOverlay) - 1);
    savedOverlay[sizeof(savedOverlay) - 1] = '\0';
    SetAssetMode("");
    int n = font_read();
    // A tree migrated before the font moved has it in the SATURN overlay only.
    if (n == 0 && AssetOverlayHasFile("SATURN", "Data/TITLEFNT.BIN")) {
        SetAssetMode("SATURN");
        const size_t got = LoadFile(GAME_DATA_ROOT "data\\titlefnt.bin", s_fontFile, 0x20);
        n = (got != (size_t)-1 && got >= 8 && got <= FONT_FILE_MAX) ? (int)got : 0;
    }
    SetAssetMode(savedOverlay);

    if (n == 0) {
        dbg_printf("[TITLE] titlefnt.bin not available - the title keeps its own rows "
                   "and has no OPTIONS item (re-run the asset migrator's PC tab)\n");
        return 0;
    }
    if (memcmp(s_fontFile, "TFNT", 4) != 0) return 0;

    const int height = s_fontFile[4];
    const int count = s_fontFile[7];
    if (height <= 0 || height > TITLE_FONT_STRIP_H || count <= 0) return 0;

    memset(s_glyphs, 0, sizeof(s_glyphs));
    size_t pos = 8;
    for (int i = 0; i < count; i++) {
        if (pos + 2 > (size_t)n) return 0;
        const unsigned char ch = s_fontFile[pos];
        const int w = s_fontFile[pos + 1];
        if (w == 0 || pos + 2 + (size_t)w * height > (size_t)n) return 0;
        if (ch < FONT_MAX_GLYPH) {
            s_glyphs[ch].levels = s_fontFile + pos + 2;
            s_glyphs[ch].width = w;
        }
        pos += 2 + (size_t)w * height;
    }
    if (pos != (size_t)n) return 0;

    s_fontHeight = height;
    s_fontSpacing = s_fontFile[5];
    s_fontSpaceAdvance = s_fontFile[6];
    s_fontState = 1;
    return 1;
}

static int text_width(const char* text)
{
    int w = 0;
    for (int i = 0; text[i] != '\0'; i++) {
        const unsigned char c = (unsigned char)text[i];
        if (c == ' ' || c >= FONT_MAX_GLYPH || s_glyphs[c].levels == NULL) {
            w += s_fontSpaceAdvance;
        } else {
            w += s_glyphs[c].width + s_fontSpacing;
        }
    }
    return w > 0 ? w - s_fontSpacing : 0;
}

// ---------------------------------------------------------------------------
// Drawing into a 256-wide 4bpp sheet (128 bytes per row, left pixel in the low
// nibble). `bright` text is palette index 1 with index 2 on its half-tone
// corners; dim text is index 2 alone.
// ---------------------------------------------------------------------------
#define SHEET_W       256
#define SHEET_STRIDE  128
#define SHEET_H       256

// The items the last sheet was built from (title_font_build_strips), for its
// HD picture. The callers' tables are static.
static const char* const* s_stripItems = NULL;
static int                s_stripCount = 0;

static void put_pixel(unsigned char* pix, int x, int y, int index)
{
    unsigned char* b = pix + y * SHEET_STRIDE + (x >> 1);
    if (x & 1) *b = (unsigned char)((*b & 0x0F) | (index << 4));
    else       *b = (unsigned char)((*b & 0xF0) | index);
}

static void draw_text(unsigned char* pix, int y, const char* text, int bright)
{
    int x = (SHEET_W - text_width(text)) / 2;
    if (y < 0 || y + s_fontHeight > SHEET_H) return;
    for (int i = 0; text[i] != '\0'; i++) {
        const unsigned char c = (unsigned char)text[i];
        if (c == ' ' || c >= FONT_MAX_GLYPH || s_glyphs[c].levels == NULL) {
            x += s_fontSpaceAdvance;
            continue;
        }
        const TitleGlyph& g = s_glyphs[c];
        for (int gy = 0; gy < s_fontHeight; gy++) {
            for (int gx = 0; gx < g.width; gx++) {
                const int level = g.levels[gy * g.width + gx];
                if (level == 0) continue;
                int index;
                if (bright) index = (level == 2) ? 1 : 2;
                else        index = (level == 2) ? 2 : 0;
                if (index != 0 && x + gx >= 0 && x + gx < SHEET_W) {
                    put_pixel(pix, x + gx, y + gy, index);
                }
            }
        }
        x += g.width + s_fontSpacing;
    }
}

unsigned int title_font_build_strips(unsigned char* out,
                                       const char* const* items, int count)
{
    if (out == NULL || items == NULL || s_fontState <= 0) return 0;
    if (count <= 0 || count > TITLE_FONT_STRIP_MAX) return 0;

    const unsigned int pixBytes = SHEET_STRIDE * SHEET_H;
    const unsigned int headerBytes = 0x40;
    memset(out, 0, headerBytes + pixBytes);

    unsigned char* p = out;
    *(unsigned int*)p = 0x10;                 p += 4;   // TIM magic
    *(unsigned int*)p = 0x08;                 p += 4;   // 4bpp + CLUT
    *(unsigned int*)p = 12 + 32;              p += 4;   // CLUT block length
    *(unsigned short*)p = 0;                  p += 2;   // clutX
    *(unsigned short*)p = 0x1E0;              p += 2;   // clutY, the row the title selects
    *(unsigned short*)p = 16;                 p += 2;
    *(unsigned short*)p = 1;                  p += 2;
    // The USA sheet's greys: 0 clear, 1 = (115,115,115), 2 = (57,57,57).
    for (int i = 0; i < 16; i++) {
        unsigned short c = 0;
        if (i == 1) c = 0x39CE;
        if (i == 2) c = 0x1CE7;
        *(unsigned short*)p = c;              p += 2;
    }
    *(unsigned int*)p = 12 + pixBytes;        p += 4;
    *(unsigned short*)p = 0;                  p += 2;
    *(unsigned short*)p = 0;                  p += 2;
    *(unsigned short*)p = SHEET_W / 4;        p += 2;
    *(unsigned short*)p = SHEET_H;            p += 2;

    for (int i = 0; i < count; i++) {
        const int y = i * TITLE_FONT_STRIP_PITCH;
        draw_text(p, y, items[i], 0);
        draw_text(p, y + TITLE_FONT_STRIP_H, items[i], 1);
    }
    s_stripItems = items;
    s_stripCount = count;
    return headerBytes + pixBytes;
}

// ---------------------------------------------------------------------------
// HD mode (port-only): the same sheet drawn in the HD font at a multiple of its
// size, handed to the HD texture layer as the picture for the 4bpp sheet's
// upload (HdTextures_BeginPicture). The strips, their texV and the logical
// 256x256 the title draws with are unchanged. Every letter sits in the bitmap
// letter's cell (hd_layout), so the HD line has the bitmap's spacing: Archivo
// Black's own is far tighter than the original option text's 3-pixel letter
// gaps and 15-pixel word gaps. The whole line is then drawn at
// TITLE_HD_TEXT_SIZE of the bitmap's size - letters, gaps and width alike -
// about its centre: at the bitmap's full 11-row cap height a smooth font reads
// larger than the pixel font does.
//
// The glyphs are anti-aliased at their sub-pixel positions (HdText.h). The
// colours are the 4bpp sheet's own (index 2 for the dim strips, index 1 for
// the bright ones) with the coverage as alpha; a clear texel keeps its strip's
// colour so a filtered edge does not darken.
// ---------------------------------------------------------------------------
#define TITLE_HD_MAX_CHARS 64
#define TITLE_HD_TEXT_SIZE 0.8f    // HD text size, a fraction of the bitmap's

// The HD title font, TITLEFNT.TTF, beside the base tree's TITLEFNT.BIN. The
// layout needs the bitmap's letter widths, so it needs the bitmap too.
static const HdFont* title_hd_font(void)
{
    if (s_fontState <= 0) return NULL;
    return HdFont_ForAsset(GAME_DATA_ROOT "data\\titlefnt.bin");
}

struct HdPlaced {
    int glyph;
    float x;                 // the glyph's origin, HD pixels from the sheet's left
};

// Places `text` as draw_text places it, centred on sheet column `centreX`, at
// `fs` HD pixels per font unit: each letter's ink centred on the bitmap
// letter's cell, so the line keeps the bitmap's letter and word spacing, the
// cells drawn together towards the line's centre by `size`
// (TITLE_HD_TEXT_SIZE). Returns the number of glyphs placed, -1 when the font
// lacks a letter the bitmap has or the line is too long. `top` / `bottom`
// receive the ink's extent in font units above the baseline (bottom < 0 for a
// descender).
static int hd_layout(const HdFont* font, const char* text, float centreX, float fs,
                     float size, int scale, HdPlaced* out, int* top, int* bottom)
{
    int n = 0;
    float x = centreX - (float)text_width(text) * 0.5f;
    const float centre = centreX * (float)scale;
    *top = 0;
    *bottom = 0;
    for (int i = 0; text[i] != '\0'; i++) {
        const unsigned char c = (unsigned char)text[i];
        if (c == ' ' || c >= FONT_MAX_GLYPH || s_glyphs[c].levels == NULL) {
            x += (float)s_fontSpaceAdvance;
            continue;
        }
        const int g = HdFont_Glyph(font, c);
        if (g == 0 || n >= TITLE_HD_MAX_CHARS) return -1;
        int x0, y0, x1, y1;
        if (HdFont_GlyphBox(font, g, &x0, &y0, &x1, &y1)) {
            const float cell = centre +
                ((x + (float)s_glyphs[c].width * 0.5f) * (float)scale - centre) * size;
            out[n].glyph = g;
            out[n].x = cell - (float)(x0 + x1) * 0.5f * fs;
            if (n == 0 || y1 > *top) *top = y1;
            if (n == 0 || y0 < *bottom) *bottom = y0;
            n++;
        }
        x += (float)(s_glyphs[c].width + s_fontSpacing);
    }
    return n;
}

// Draws `text` in the title font into the HD sheet as alpha over `colour`: the
// line whose bitmap capitals stand on sheet rows y .. y + 10, in a band of
// `rows` sheet rows from y, centred on column `centreX`. `cov` is scratch for
// the band (sheetW * rows * scale bytes). The band is first cleared to `colour`
// (alpha 0); 0 leaves the sheet untouched.
static int hd_draw_text(const HdFont* font, unsigned int* sheet, int sheetW, int sheetH,
                        int y, int rows, float centreX, const char* text,
                        unsigned int colour, int scale, unsigned char* cov)
{
    const int capPx = s_fontHeight * scale;
    const int rowsPx = rows * scale;
    const float capHd = (float)capPx * TITLE_HD_TEXT_SIZE;
    const float fs = capHd / (float)HdFont_CapHeight(font);
    HdPlaced placed[TITLE_HD_MAX_CHARS];
    int top = 0, bottom = 0;
    const int n = hd_layout(font, text, centreX, fs, TITLE_HD_TEXT_SIZE, scale, placed,
                            &top, &bottom);
    if (n < 0) return 0;

    // The capitals sit centred on the bitmap's 11 rows. Round letters overshoot
    // the cap line (Archivo Black's O by a fifth of a row): a line whose ink
    // would leave the band drops down, as far as its descenders allow.
    const float centred = ((float)capPx + capHd) * 0.5f;
    float base = centred;
    const float ascent = (float)top * fs;
    const float descent = -(float)bottom * fs;
    if (ascent > base) {
        base = ascent;
        if (base + descent > (float)rowsPx) base = (float)rowsPx - descent;
        if (base < centred) base = centred;
    }

    memset(cov, 0, (size_t)sheetW * (size_t)rowsPx);
    for (int i = 0; i < n; i++) {
        if (!HdFont_AddGlyph(font, placed[i].glyph, fs, placed[i].x, base, cov, sheetW, rowsPx)) {
            return 0;
        }
    }
    for (int r = y * scale; r < y * scale + rowsPx && r < sheetH; r++) {
        unsigned int* dst = sheet + (size_t)r * (size_t)sheetW;
        for (int x = 0; x < sheetW; x++) dst[x] = colour & 0x00FFFFFFu;
    }
    HdText_Paint(cov, sheetW, rowsPx, colour, sheet, sheetW, sheetH, 0, y * scale);
    return 1;
}

void title_font_load_strips(void* tim, short texId, int slot)
{
    unsigned int* low = NULL;
    unsigned int* hd = NULL;
    unsigned char* cov = NULL;
    const HdFont* font = NULL;
    int scale = 0;
    int hdSide = 0;
    int w = 0, h = 0;

    if (tim != NULL && s_stripItems != NULL) font = title_hd_font();
    if (font != NULL) {
        scale = HdText_Scale();
        hdSide = SHEET_W * scale;
        low = (unsigned int*)malloc(sizeof(unsigned int) * SHEET_W * SHEET_H);
        hd = (unsigned int*)malloc(sizeof(unsigned int) * (size_t)hdSide * (size_t)hdSide);
        cov = (unsigned char*)malloc((size_t)hdSide * (size_t)(TITLE_FONT_STRIP_H * scale));
    }
    // The 4bpp sheet as the upload will see it, to match the upload by.
    int ok = (low != NULL && hd != NULL && cov != NULL) &&
             HdText_Tim4ToRgba(tim, low, SHEET_W * SHEET_H, &w, &h) &&
             w == SHEET_W && h == SHEET_H;
    if (ok) {
        // Clear; each strip's rows take its own colour as it is drawn.
        memset(hd, 0, sizeof(unsigned int) * (size_t)hdSide * (size_t)hdSide);
        const unsigned char* t = (const unsigned char*)tim;
        for (int i = 0; i < s_stripCount && ok; i++) {
            const int width = text_width(s_stripItems[i]);
            const float centreX = (float)((SHEET_W - width) / 2) + (float)width * 0.5f;
            for (int bright = 0; bright < 2 && ok; bright++) {
                const int y = i * TITLE_FONT_STRIP_PITCH + (bright ? TITLE_FONT_STRIP_H : 0);
                const int index = bright ? 1 : 2;
                const unsigned short clr = (unsigned short)(t[20 + index * 2] | (t[21 + index * 2] << 8));
                const unsigned int colour = (((clr >> 10) & 0x1F) * 255 / 31) << 16 |
                                            (((clr >> 5) & 0x1F) * 255 / 31) << 8 |
                                            ((clr & 0x1F) * 255 / 31);
                ok = hd_draw_text(font, hd, hdSide, hdSide, y, TITLE_FONT_STRIP_H, centreX,
                                  s_stripItems[i], colour, scale, cov);
            }
        }
    }
    if (ok) {
        dbg_printf("[TITLE] menu sheet drawn in the HD font at %dx%d\n", hdSide, hdSide);
        HdPictureScope picture(SHEET_W, SHEET_H, low, hdSide, hdSide, hd);
        LoadTexturePage(tim, texId, 0, slot, 0, 0, 0, 0);
    } else {
        LoadTexturePage(tim, texId, 0, slot, 0, 0, 0, 0);
    }
    free(cov);
    free(hd);
    free(low);
}

// ---------------------------------------------------------------------------
// The release's title sheets in HD (port-only)
//
// PRESS START BUTTON, the USA sheet's NEW GAME / LOAD GAME, the Director's
// Cut's STANDARD / TRAINING / ADVANCED and the copyright lines are pictures in
// the release's sheets, not text. With HD textures on, each sheet gets an HD
// picture (HdTextures_BeginPicture) in which every text line of the table
// below is drawn again: the option lines in the title font exactly as the menu
// draws its own (centred where the art centres the line, in its colour), the
// copyright lines in the HD 8x8 font (FONTUS.TTF), smaller than the art's
// (TITLE_SHEET_COPY_SIZE): the line spread to the art's width, then drawn
// together about its centre. Everything else is the sheet's own texels,
// enlarged.
//
// A sheet only gets the picture when its ink is where the table says - every
// inked row inside a listed line, every listed line inked - so a release whose
// art is laid out differently keeps its own. A line whose font is missing
// keeps its art.
// ---------------------------------------------------------------------------
#define TITLE_SHEET_COPY_ROWS  7.0f   // the copyright art's capitals: the line's first 7 rows
#define TITLE_SHEET_COPY_SIZE  0.7f   // its HD size: capitals, gaps and width alike

struct TitleSheetLine {
    unsigned char top;       // first sheet row of the line's art
    unsigned char rows;      // its rows, descenders included
    unsigned char narrow;    // 1 = the 8x8 font (copyright), 0 = the title font
    const char* text;        // Latin-1: 0xA9 is the copyright sign
};

static const char kCopyrightUsa[] = "\xA9" "CAPCOM CO.,LTD.1996,1997 ALL RIGHTS RESERVED.";
static const char kCopyrightDc1[] = "\xA9" "CAPCOM CO.,LTD.1996,1997";
static const char kCopyrightDc2[] = "\xA9" "CAPCOM U.S.A.,INC.1996,1997 ALL RIGHTS RESERVED.";

// data\t_start.tim / data\t_press.tim: PRESS ..., then the NEW GAME and LOAD
// GAME frames, each with its copyright line.
static const TitleSheetLine kSheetUsaStart[] = {
    {   5, 11, 0, "PRESS START BUTTON" }, {  32, 9, 1, kCopyrightUsa },
    {  84, 11, 0, "NEW GAME" },  { 102, 11, 0, "LOAD GAME" }, { 131, 9, 1, kCopyrightUsa },
    { 176, 11, 0, "NEW GAME" },  { 194, 11, 0, "LOAD GAME" }, { 223, 9, 1, kCopyrightUsa },
};
static const TitleSheetLine kSheetUsaPress[] = {
    {   5, 11, 0, "PRESS ANY BUTTON" }, {  32, 9, 1, kCopyrightUsa },
    {  84, 11, 0, "NEW GAME" },  { 102, 11, 0, "LOAD GAME" }, { 131, 9, 1, kCopyrightUsa },
    { 176, 11, 0, "NEW GAME" },  { 194, 11, 0, "LOAD GAME" }, { 223, 9, 1, kCopyrightUsa },
};
// The Director's Cut's page A (TitleScreen.cpp dc_title_build_page): cells 0-2
// of data\bt367oab.tim, 80 rows each.
static const TitleSheetLine kSheetDcMain[] = {
    {  23, 11, 0, "PRESS ANY BUTTON" }, {  52, 9, 1, kCopyrightDc1 }, { 62, 9, 1, kCopyrightDc2 },
    {  91, 11, 0, "NEW GAME" },  { 109, 11, 0, "LOAD GAME" },
    { 171, 11, 0, "NEW GAME" },  { 189, 11, 0, "LOAD GAME" },
};
// Its page B: cells 3-6, cropped to 64 rows - the difficulty submenu, one cell
// per highlighted entry (the last is ADVANCED held, in green).
static const TitleSheetLine kSheetDcDifficulty[] = {
    {   8, 11, 0, "STANDARD" }, {  24, 11, 0, "TRAINING" }, {  40, 11, 0, "ADVANCED" },
    {  72, 11, 0, "STANDARD" }, {  88, 11, 0, "TRAINING" }, { 104, 11, 0, "ADVANCED" },
    { 136, 11, 0, "STANDARD" }, { 152, 11, 0, "TRAINING" }, { 168, 11, 0, "ADVANCED" },
    { 200, 11, 0, "STANDARD" }, { 216, 11, 0, "TRAINING" }, { 232, 11, 0, "ADVANCED" },
};

struct TitleSheet {
    const TitleSheetLine* lines;
    int count;
};

static const TitleSheet kTitleSheets[TITLE_FONT_SHEET_COUNT] = {
    { kSheetUsaStart,     (int)(sizeof(kSheetUsaStart) / sizeof(kSheetUsaStart[0])) },
    { kSheetUsaPress,     (int)(sizeof(kSheetUsaPress) / sizeof(kSheetUsaPress[0])) },
    { kSheetDcMain,       (int)(sizeof(kSheetDcMain) / sizeof(kSheetDcMain[0])) },
    { kSheetDcDifficulty, (int)(sizeof(kSheetDcDifficulty) / sizeof(kSheetDcDifficulty[0])) },
};

// The copyright line: `text` in the 8x8 HD font, laid out as the art's line -
// the letters at the font's own advances and kerning plus one even tracking
// that makes the ink span sheet columns inkL .. inkR - then the whole of it
// drawn at TITLE_SHEET_COPY_SIZE about its centre, its capitals centred on
// the art's 7 rows. Draws into `cov` (sheetW * rows * scale); 0 if a glyph is
// missing.
static int hd_draw_copyright(const HdFont* font, int sheetW, int rows, float inkL, float inkR,
                             const char* text, int scale, unsigned char* cov)
{
    const float cap = TITLE_SHEET_COPY_ROWS * TITLE_SHEET_COPY_SIZE;
    const float fs = cap * (float)scale / (float)HdFont_CapHeight(font);
    const int rowsPx = rows * scale;
    float pos[TITLE_HD_MAX_CHARS];
    int glyphs[TITLE_HD_MAX_CHARS];
    int n = 0;
    float pen = 0.0f, first = 0.0f, last = 0.0f;
    int prev = 0, inked = 0, chars = 0;
    const int space = HdFont_Glyph(font, ' ');
    for (int i = 0; text[i] != '\0'; i++) {
        const int c = (unsigned char)text[i];
        const int g = (c == ' ') ? space : HdFont_Glyph(font, c);
        if (g == 0) return 0;
        if (n >= TITLE_HD_MAX_CHARS) return 0;
        pen += (float)HdFont_Kern(font, prev, g) * fs;
        int x0, y0, x1, y1;
        if (HdFont_GlyphBox(font, g, &x0, &y0, &x1, &y1)) {
            if (!inked) first = pen + (float)x0 * fs;
            last = pen + (float)x1 * fs;
            inked = 1;
        }
        glyphs[n] = g;
        pos[n++] = pen;
        pen += (float)HdFont_Advance(font, g) * fs;
        prev = g;
        chars++;
    }
    if (!inked || chars < 2) return 0;
    const float target = (inkR - inkL) * (float)scale * TITLE_SHEET_COPY_SIZE;
    const float track = (target - (last - first)) / (float)(chars - 1);
    const float shift = (inkL + inkR) * 0.5f * (float)scale - target * 0.5f - first;
    const float base = ((TITLE_SHEET_COPY_ROWS - cap) * 0.5f + cap) * (float)scale;
    memset(cov, 0, (size_t)sheetW * (size_t)rowsPx);
    for (int i = 0; i < n; i++) {
        if (!HdFont_AddGlyph(font, glyphs[i], fs, shift + pos[i] + track * (float)i, base,
                             cov, sheetW, rowsPx)) {
            return 0;
        }
    }
    return 1;
}

void title_font_load_sheet(int sheet, void* tim, short texId, short pageOffset,
                             int slot, int unused, short posX, short posY,
                             unsigned int flags)
{
    unsigned int* low = NULL;
    unsigned int* hd = NULL;
    unsigned char* cov = NULL;
    int w = 0, h = 0, scale = 0, drawn = 0;

    // Loaded here as well: the release's sheet goes up before the menu's.
    title_font_load();
    const HdFont* black = title_hd_font();
    const HdFont* narrow = HdFont_Text();
    const TitleSheet* def = (sheet >= 0 && sheet < TITLE_FONT_SHEET_COUNT) ? &kTitleSheets[sheet] : NULL;
    if (tim != NULL && def != NULL && (black != NULL || narrow != NULL)) {
        low = (unsigned int*)malloc(sizeof(unsigned int) * SHEET_W * SHEET_H);
    }
    if (low != NULL && HdText_Tim4ToRgba(tim, low, SHEET_W * SHEET_H, &w, &h) && w == SHEET_W) {
        // The ink must be where the table has it.
        int ok = 1;
        for (int y = 0; y < h && ok; y++) {
            int inked = 0;
            for (int x = 0; x < w && !inked; x++) inked = (low[y * w + x] >> 24) != 0;
            if (!inked) continue;
            int listed = 0;
            for (int k = 0; k < def->count && !listed; k++) {
                listed = y >= def->lines[k].top && y < def->lines[k].top + def->lines[k].rows;
            }
            ok = listed;
        }
        if (!ok) dbg_printf("[TITLE] sheet %d is not laid out as expected - it keeps its art\n", sheet);
        if (ok) {
            scale = HdText_Scale();
            hd = (unsigned int*)malloc(sizeof(unsigned int) * (size_t)(w * scale) * (size_t)(h * scale));
            cov = (unsigned char*)malloc((size_t)(w * scale) * (size_t)(12 * scale));
        }
        if (hd != NULL && cov != NULL) {
            const int hdW = w * scale, hdH = h * scale;
            // The sheet's own texels, enlarged.
            for (int y = 0; y < hdH; y++) {
                const unsigned int* src = low + (y / scale) * w;
                unsigned int* dst = hd + (size_t)y * (size_t)hdW;
                for (int x = 0; x < hdW; x++) dst[x] = src[x / scale];
            }
            for (int k = 0; k < def->count; k++) {
                const TitleSheetLine& line = def->lines[k];
                if (line.top + line.rows > h || line.rows > 12) continue;
                // Where the art inks the line, and in what colour (its most
                // common one; the rest are its half-tone edges).
                int inkL = w, inkR = -1;
                unsigned int colours[8];
                int counts[8], used = 0;
                for (int y = line.top; y < line.top + line.rows; y++) {
                    for (int x = 0; x < w; x++) {
                        const unsigned int p = low[y * w + x];
                        if ((p >> 24) == 0) continue;
                        if (x < inkL) inkL = x;
                        if (x > inkR) inkR = x;
                        int c = 0;
                        while (c < used && colours[c] != p) c++;
                        if (c == used) {
                            if (used == 8) continue;
                            colours[used] = p;
                            counts[used++] = 0;
                        }
                        counts[c]++;
                    }
                }
                if (inkR < 0) continue;
                int best = 0;
                for (int c = 1; c < used; c++) {
                    if (counts[c] > counts[best]) best = c;
                }
                const unsigned int colour = colours[best] & 0x00FFFFFFu;
                const HdFont* font = line.narrow ? narrow : black;
                if (font == NULL) continue;
                if (!line.narrow) {
                    if (hd_draw_text(font, hd, hdW, hdH, line.top, line.rows,
                                     (float)(inkL + inkR + 1) * 0.5f, line.text, colour, scale, cov)) {
                        drawn++;
                    }
                    continue;
                }
                if (!hd_draw_copyright(font, hdW, line.rows, (float)inkL, (float)(inkR + 1),
                                       line.text, scale, cov)) {
                    continue;
                }
                // The line's band: clear in its colour, then its text.
                for (int y = line.top * scale; y < (line.top + line.rows) * scale; y++) {
                    unsigned int* dst = hd + (size_t)y * (size_t)hdW;
                    for (int x = 0; x < hdW; x++) dst[x] = colour;
                }
                HdText_Paint(cov, hdW, line.rows * scale, colour, hd, hdW, hdH, 0, line.top * scale);
                drawn++;
            }
        }
    }
    if (drawn > 0) {
        dbg_printf("[TITLE] title sheet %d: %d lines drawn in the HD fonts at %dx%d\n",
                   sheet, drawn, w * scale, h * scale);
        HdPictureScope picture(w, h, low, w * scale, h * scale, hd);
        LoadTexturePage(tim, texId, pageOffset, slot, unused, posX, posY, flags);
    } else {
        LoadTexturePage(tim, texId, pageOffset, slot, unused, posX, posY, flags);
    }
    free(cov);
    free(hd);
    free(low);
}

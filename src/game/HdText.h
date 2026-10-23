// HdText.h - HD text drawn in real TrueType fonts (port-only).
//
// With HD textures on, the HD folder can carry a font for a font file the game
// reads, named after it the way the HD layer names every file it replaces:
//
//   HD/<USA|JPN>/Data/TITLEFNT.TTF   the title menu's typeface (Archivo Black):
//                                    the menu lines and the title sheets'
//                                    PRESS START / NEW GAME / STANDARD ... art
//   HD/USA/Data/FONTUS.TTF           the 8x8 font of data\fontus.tim (Archivo
//   HD/JPN/Data/FONT.TTF             Narrow SemiBold; data\FONT.TIM for JPN):
//                                    PrintText8x8 and the title's copyright line
//
// The asset migrator writes them (tools/asset_migrator, core/HdFonts.h). The
// base tree's name is used in every mode, so one font covers the Director's
// Cut and Saturn overlays too. Without HD textures, or without the file,
// nothing here draws and the game keeps its own pixel fonts.
//
// The rasteriser is stb_truetype (src/third_party/stb_truetype.h), compiled
// into HdText.cpp. docs/HD_TEXTURES.md "HD fonts" has the layout rules.
#pragma once

struct HdFont;

// The HD font named after the base tree's `assetPath` (an asset-rooted path
// such as GAME_DATA_ROOT "data\\titlefnt.bin"), read once and kept. NULL when
// HD textures are off, there is no such .ttf, or it is not a usable TrueType
// font (one without an H to size letters by counts as unusable).
const HdFont* HdFont_ForAsset(const char* assetPath);

// The HD 8x8 font (FONTUS.TTF, FONT.TTF on the Japanese tree), or NULL.
const HdFont* HdFont_Text(void);

// Drops every loaded font, so the next HdFont_ForAsset reads the files again
// (the options menu can change the asset region under them).
void HdFont_ForgetAll(void);

// Glyph index of `codepoint`, 0 when the font has none.
int HdFont_Glyph(const HdFont* font, int codepoint);
// The H's height in font units: what a caller scales to a cap height.
int HdFont_CapHeight(const HdFont* font);
// The glyph's ink box in font units, y up from the baseline. False for a glyph
// with no ink (a space).
bool HdFont_GlyphBox(const HdFont* font, int glyph, int* x0, int* y0, int* x1, int* y1);
// Advance width and kerning against the next glyph, in font units.
int HdFont_Advance(const HdFont* font, int glyph);
int HdFont_Kern(const HdFont* font, int glyph, int next);

// Adds the glyph's anti-aliased coverage to `cov` (w x h bytes, saturating),
// drawn at `fs` pixels per font unit with its origin at (x, baseline) - y down.
// Returns 0 when out of memory.
int HdFont_AddGlyph(const HdFont* font, int glyph, float fs, float x, float baseline,
                    unsigned char* cov, int w, int h);

// The HD pixels per game pixel for text drawn into textures: enough for the
// desktop's height (the game is 240 rows tall), 2 to 8.
int HdText_Scale(void);

// A 4bpp TIM (one CLUT row) converted the way LoadTexturePage converts it:
// R8G8B8A8, R in the low byte, index 0 clear when it is the key, every other
// index opaque. `out` holds width * height; false for any other TIM.
bool HdText_Tim4ToRgba(const void* tim, unsigned int* out, int maxTexels,
                       int* width, int* height);

// Writes `colour` (0x00BBGGRR) with `cov` as its alpha into the RGBA image
// `dst` (dstW wide) at (x, y), for every non-zero coverage byte.
void HdText_Paint(const unsigned char* cov, int w, int h, unsigned int colour,
                  unsigned int* dst, int dstW, int dstH, int x, int y);

// The game's 8x8 font page (fontus.tim / FONT.TIM) is being uploaded: remember
// its glyph colour, which the HD glyphs are drawn in. TextureLoader.cpp.
void HdText_NoteFontPage(const unsigned int* rgba, int width, int height);

// PrintText8x8 in the HD font: PRINT_TEXT_BUFFER at (x, y), with g_TextureDesc
// already set up by PrintText8x8 (tint, shadow, flags) and its `brightness`.
// Each word keeps the span its characters have in the 8x8 grid - callers
// place, right-align and caret text by character cells - with its letters
// spread evenly across it; a character the font has no glyph for is drawn
// from the 8x8 sheet in its own cell. Returns 0 without drawing when there is
// no HD font, and PrintText8x8 then draws as before.
int HdText_Print8x8(short x, short y, unsigned short brightness);

// The size HdText_Print8x8 draws at from now on, a fraction of the normal HD
// size, and what it is drawn towards: the text is laid out in its cells as
// above, then the whole of it - letters, gaps, width - is scaled about the
// string's left end, its centre or its right end (x + strlen * 8), so text a
// caller left-aligns, centres or right-aligns stays aligned. Each size has its
// own glyph atlas. (1, HD_TEXT_ANCHOR_LEFT) is the default; a caller that
// changes it puts it back. The pixel font is not affected.
#define HD_TEXT_ANCHOR_LEFT    0
#define HD_TEXT_ANCHOR_CENTRE  1
#define HD_TEXT_ANCHOR_RIGHT   2
void HdText_SetPrint8x8Size(float size, int anchor);

// The HD 8x14 game-text font (FONTUS_14.TTF / FONT_14.TTF beside the sheet's
// name: Libertinus Serif / M PLUS 2), or NULL.
const HdFont* HdFont_Text14(void);

// AddTintSprite's question: is the 14-row cell at (texU, texV) of the font
// sheet - texU with the kanji page's +256 added - a character the HD 8x14
// font draws? Its glyph id, HD_TEXT_BLANK14 for the sheet's space (draw
// nothing: the item-name loop draws its spaces as sprites, and one queued
// between two words splits them into two runs), or -1 (no HD font, a
// controller button, anything else). Builds the glyph atlas on first use.
#define HD_TEXT_BLANK14 (-2)
int HdText_Glyph14(int texU, int texV, int width, int height);
// The atlas those glyphs are drawn from (a MarniHandle).
unsigned int HdText_Glyph14Texture(void);

// One queued HD 8x14 glyph, in queue order. The queue fills id, joins (0 when
// any other sprite was queued between this one and the previous glyph) and the
// game-space position the sheet's cell would have been drawn at; the layout
// fills the rectangle and texture coordinates, in game space.
struct HdTextGlyphSprite {
    int id;
    int joins;
    float cellX, cellY;
    float x, y, w, h;
    float u0, v0, u1, v1;
};
// Lays the queued glyphs out (HdText.cpp, "The HD 8x14 font"): Latin runs at
// the font's own spacing from their first cell, Japanese one glyph per cell.
void HdText_Layout14(HdTextGlyphSprite* sprites, int count);

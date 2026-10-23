// TitleMenuFont.h - the title menu's text, drawn in the original Resident Evil
// option typeface.
//
// This started as the Sega Saturn mode's title menu (saturn/SaturnTitle.*) and
// is now the title menu of every mode. The second title state - the one after
// PRESS START BUTTON - is
//
//     NEW GAME
//     LOAD GAME
//     EXTRA          only while the Battle Game is offered
//     OPTIONS
//
// EXTRA opens a submenu that lists the extra game modes (BATTLE GAME, BACK) and
// OPTIONS chains the options state (GameOptions.h). The item lists and what
// each one does live in TitleScreen.cpp; this file only turns text into art.
//
// The text comes from the bitmap font the asset migrator writes into the BASE
// tree as Data/TITLEFNT.BIN (tools/asset_migrator/src/core/TitleFont.h). The
// PC tab writes it; the Saturn tab used to write it into the SATURN overlay
// instead, and a tree migrated that way is still read. When neither file is
// there nothing here is used: the title keeps the art the release shipped with
// (NEW GAME / LOAD GAME, plus a BATTLE GAME row built from the 8x14 font) and
// has no OPTIONS item.
#pragma once

// g_titleSelectionId values outside the Director's Cut. 0 = PRESS START, 1/2 =
// NEW/LOAD GAME (as in the USA build). 3 is EXTRA, which never leaves the
// title: it opens the submenu. The submenu's entries follow, and the first is
// the Battle Game - the value title_state() acts on. (The Director's Cut has
// its difficulty cells at 3-6 and numbers these from 8; see TitleScreen.cpp.)
#define TITLE_EXTRA_ITEM     3
#define TITLE_EXTRAS_FIRST   4

// Loads data\titlefnt.bin once. Returns 1 when the font is usable.
int title_font_load(void);

// The menu sheet: a 256x256 4bpp TIM in the USA title sheet's grey palette and
// CLUT row, holding two strips per item - the text centred on the sheet's 256
// columns, once dim and once bright:
//
//     item i, dim     rows i * 24      .. i * 24 + 11
//     item i, bright  rows i * 24 + 12 .. i * 24 + 23
//
// The title draws one strip per menu line, so a menu of any length fits on the
// screen without a frame per cursor position. Returns the TIM's size, 0 when the
// font is not loaded or there are more than TITLE_FONT_STRIP_MAX items.
#define TITLE_FONT_STRIP_H      12
#define TITLE_FONT_STRIP_PITCH  24
#define TITLE_FONT_STRIP_MAX    10
unsigned int title_font_build_strips(unsigned char* out,
                                       const char* const* items, int count);

// Loads the sheet title_font_build_strips just built as texture `texId` in
// `slot` (LoadTexturePage). With HD textures on and a TrueType font in the HD
// folder (HD/<USA|JPN>/Data/TITLEFNT.TTF; the migrator writes Archivo Black),
// the upload gets an HD copy of the sheet drawn in it, at a scale fitted to the
// desktop's height (the 4bpp sheet stays the logical texture); otherwise this
// is LoadTexturePage alone and the menu is the bitmap font of TITLEFNT.BIN.
void title_font_load_strips(void* tim, short texId, int slot);

// The release's own title sheets, loaded through title_font_load_sheet:
#define TITLE_FONT_SHEET_USA_START      0   // data\t_start.tim (a pad is connected)
#define TITLE_FONT_SHEET_USA_PRESS      1   // data\t_press.tim
#define TITLE_FONT_SHEET_DC_MAIN        2   // the Director's Cut's page A (cells 0-2)
#define TITLE_FONT_SHEET_DC_DIFFICULTY  3   // its page B (cells 3-6, the difficulty submenu)
#define TITLE_FONT_SHEET_COUNT          4

// LoadTexturePage(tim, texId, pageOffset, slot, unused, posX, posY, flags) for
// one of those sheets. With HD textures on, the upload gets an HD picture with
// the sheet's text lines drawn again in the HD fonts: PRESS START / NEW GAME /
// STANDARD ... in the title font (HD/<tree>/Data/TITLEFNT.TTF), the copyright
// lines in the 8x8 HD font (FONTUS.TTF / FONT.TTF; HdText.h). A sheet whose
// art is not laid out as this file expects keeps its own.
void title_font_load_sheet(int sheet, void* tim, short texId, short pageOffset,
                             int slot, int unused, short posX, short posY,
                             unsigned int flags);

// Makes the next title_font_load() look for the file again. The answer
// is cached, and the options menu can change the asset region under it.
void title_font_forget(void);

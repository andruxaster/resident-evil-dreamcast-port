// GameOptions.cpp - the title screen's OPTIONS menu. See GameOptions.h.
//
// Port-added; nothing here has an original address. The one piece of original
// machinery it leans on is the options-screen character renderer
// (options_render_entity, 0x004775b0) and the setup around it in options_menu
// (0x004761b0): camera, three lights, ambient, Joint_move, joint matrices. The
// BONUS page uses the same calls to show the outfit being chosen.
#include "GameOptions.h"

#include "../Globals.h"
#include "../DebugPrint.h"
#include "../marni/MarniSound.h"
#include "../platform/platform.h"
#include "../system/AssetPath.h"
#include "../system/ConfigFile.h"
#include "../system/HdTextures.h"
#include "Bonus.h"
#include "HdText.h"
#include "OptionsMenu.h"            // options_render_entity
#include "SFXIds.h"
#include "saturn/SaturnBattle.h"
#include "TitleMenuFont.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern void SetLightMatrix(MATRIX* m);

// ---------------------------------------------------------------------------
// Screen layout. 320x240, 8x8 font: 40 columns by 30 rows.
// ---------------------------------------------------------------------------
#define OPT_TITLE_X      16
#define OPT_TITLE_Y      12
#define OPT_RULE_Y       24
#define OPT_ROW_X        16
#define OPT_ROW_Y        40
#define OPT_ROW_PITCH    13
#define OPT_HELP_Y       212
#define OPT_HINT_Y       226
#define OPT_MAX_ROWS     14
// With HD textures and the HD 8x8 font (HdText.h), the menu's text is drawn at
// this fraction of the HD font's normal size; the pixel font is unchanged.
#define OPT_HD_TEXT_SIZE 0.8f

// PrintText8x8's colour byte: high nibble = brightness in 30ths (0 and 2 both
// mean full), low nibble = tint (0 white, 1 green, 2 red, 3 grey, else yellow).
#define COL_SELECTED     0x00    // white, full
#define COL_NORMAL       0xF0    // white at 15/30
#define COL_DISABLED     0x70    // white at 7/30
#define COL_DISABLED_SEL 0xC0    // a dim row under the cursor: 12/30, clear of the bar
#define COL_TITLE        0x04    // yellow
#define COL_GOOD         0x01    // green
#define COL_HELP         0xF0

// Raw pad word bits (function-named; see docs/GAMEPAD_INPUT.md).
#define OPT_PAD_UP           0x1000
#define OPT_PAD_DOWN         0x4000
#define OPT_PAD_LEFT         0x8000
#define OPT_PAD_RIGHT        0x2000
#define OPT_PAD_CONFIRM      0x0080
#define OPT_PAD_CANCEL       0x0040

// g_BgmNameTable's row 15 (0x004d0518): { "Bgm_31" }, channel 0, looping.
#define OPT_BGM_GROUP    15

#define NAV_UP           0x01
#define NAV_DOWN         0x02
#define NAV_LEFT         0x04
#define NAV_RIGHT        0x08
#define NAV_CONFIRM      0x10
#define NAV_CANCEL       0x20
#define NAV_DIRECTIONS   (NAV_UP | NAV_DOWN | NAV_LEFT | NAV_RIGHT)

enum {
    PAGE_ROOT = 0,
    PAGE_DISPLAY,
    PAGE_INPUT,
    PAGE_GAME,
    PAGE_BONUS,
    PAGE_COUNT
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static int s_page = PAGE_ROOT;
static int s_cursor[PAGE_COUNT];
static int s_done = 0;
static int s_confirmQuit = 0;         // EXIT GAME prompt: 0 off, 1 NO, 2 YES
static int s_quitting = 0;          // EXIT GAME was confirmed
static int s_wantTitle = 0;         // in game: BACK TO TITLE was confirmed

// Navigation: what was held last frame, and the auto-repeat counter.
static unsigned int s_navPrev = 0;
static int          s_navRepeat = 0;

// One row of the page being shown, rebuilt every frame.
struct OptRow {
    const char* label;
    char        value[28];
    int         enabled;      // 0 = drawn dim, the cursor still lands on it
    int         arrows;       // 1 = left/right change it
    const char* help;
};
static OptRow s_rows[OPT_MAX_ROWS];
static int    s_rowCount = 0;

// --- DISPLAY ---------------------------------------------------------------
struct OptRes { int w, h; };
static const OptRes kResolutions[] = {
    {  640,  480 }, {  800,  600 }, {  960,  720 }, { 1024,  768 },
    { 1280,  720 }, { 1280,  960 }, { 1366,  768 }, { 1440, 1080 },
    { 1600,  900 }, { 1600, 1200 }, { 1920, 1080 }, { 1920, 1440 },
    { 2560, 1440 }, { 2880, 2160 }, { 3840, 2160 },
};
#define RES_TABLE_COUNT ((int)(sizeof(kResolutions) / sizeof(kResolutions[0])))
static OptRes s_res[RES_TABLE_COUNT + 1];
static int    s_resCount = 0;
static int    s_dispFull = 0;
static int    s_dispRes = 0;
static int    s_dispVsync = 0;

// --- INPUT -----------------------------------------------------------------
struct OptFunction {
    const char*  label;
    unsigned int mask;        // the raw pad word value the binding produces
};
static const OptFunction kFunctions[] = {
    { "UP",        0x1000 },
    { "DOWN",      0x4000 },
    { "LEFT",      0x8000 },
    { "RIGHT",     0x2000 },
    { "ACTION",    0x0080 },
    { "RUN/CANCEL", 0x0040 },
    { "AIM",       0x0008 },
    { "INVENTORY", 0x0800 },
    { "OPTIONS",   0x0900 },
};
#define FUNCTION_COUNT ((int)(sizeof(kFunctions) / sizeof(kFunctions[0])))
#define INPUT_ROW_RESET_KEYS  (FUNCTION_COUNT)
#define INPUT_ROW_RESET_PAD   (FUNCTION_COUNT + 1)
#define INPUT_ROW_BACK        (FUNCTION_COUNT + 2)

// The shipped keyboard layout (g_keyBindingData's initialiser, 0x004d4730).
static const unsigned char kDefaultKeys[32] = {
    0x26, 0x28, 0x25, 0x27, 0,    'V',  0,    0,
    0,    0,    'X',  'C',  0,    'Z',  0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    'A',  0x11, 0x0D, 0x20, 0x1B,
};

// The keys a binding can be given: the original options screen's own watch
// list (g_abOptKeyScanTable, 0x004d46e8) plus Shift.
static const unsigned char kBindableKeys[] = {
    0x0d, 0x20, 0x11, 0x10,
    0x25, 0x26, 0x27, 0x28,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
    0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54,
    0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6e,
    0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0,
    0xdb, 0xdc, 0xdd, 0xde,
    0xe2,
};
#define BINDABLE_KEY_COUNT ((int)sizeof(kBindableKeys))

static int s_inputColumn = 0;         // 0 = keyboard, 1 = gamepad
static int s_capture = 0;             // 0 = off, 1 = waiting for release, 2 = waiting for a press
static int s_captureTimer = 0;
static int s_swallowInput = 0;        // ignore navigation until everything is released
#define CAPTURE_TIMEOUT_FRAMES 360

// --- GAME ------------------------------------------------------------------
// Mode and region are what config.ini will say, not what is running: both are
// read once at start-up (the global textures, the font and the item sheet are
// loaded from whichever tree was active then), so a change takes effect on the
// next start. Initialised from the running game the first time the menu opens
// and kept for the rest of the session, so re-opening the menu shows the choice.
static int s_cfgInit = 0;
static int s_cfgMode = GAME_MODE_OG;
static int s_cfgRegion = 0;
static int s_cfgHd = 1;               // [Textures] HdTextures, also read at start-up
static int s_restarting = 0;          // APPLY AND RESTART was confirmed

// --- BONUS -----------------------------------------------------------------
static int s_bonusWeapons[BONUS_WEAPON_COUNT];
static int s_bonusWeaponCount = 0;
static int s_previewChar = -1;        // what the model on screen is
static int s_previewOutfit = -1;
static int s_previewWeaponItem = 0;   // item id held aimed, 0 = none
static int s_previewReady = 0;
static int s_previewDirty = 0;        // bonus.dat needs writing

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
// The text is placed in 8-pixel cells; `anchor` says how the caller aligned it
// (HD_TEXT_ANCHOR_*), which is what the smaller HD text is drawn towards.
static void opt_vtext(int anchor, int x, int y, unsigned char colour, const char* fmt,
                      va_list args)
{
    vsnprintf(PRINT_TEXT_BUFFER, sizeof(PRINT_TEXT_BUFFER), fmt, args);
    HdText_SetPrint8x8Size(OPT_HD_TEXT_SIZE, anchor);
    PrintText8x8((short)x, (short)y, colour, 0);
    HdText_SetPrint8x8Size(1.0f, HD_TEXT_ANCHOR_LEFT);
}

static void opt_text(int x, int y, unsigned char colour, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    opt_vtext(HD_TEXT_ANCHOR_LEFT, x, y, colour, fmt, args);
    va_end(args);
}

// The same for text the caller right-aligns (it ends at x + strlen * 8) ...
static void opt_text_right(int x, int y, unsigned char colour, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    opt_vtext(HD_TEXT_ANCHOR_RIGHT, x, y, colour, fmt, args);
    va_end(args);
}

// ... and centres (on x + strlen * 4).
static void opt_text_centre(int x, int y, unsigned char colour, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    opt_vtext(HD_TEXT_ANCHOR_CENTRE, x, y, colour, fmt, args);
    va_end(args);
}

// A filled rectangle in screen coordinates (top-left origin). `blend` is
// draw_rect's ordering key with flags = 1: depth = blend * 16 + 500, and
// anything at 0x400 or more is drawn before the 3D pass - i.e. behind the model
// and behind every glyph.
static void opt_rect(int x, int y, int w, int h, unsigned char level, int blend)
{
    RectDrawDesc d;
    d.textureId = 0;                       // variant 0: opaque, literal colour
    d.x = (short)(x - g_ScreenOffsetX);
    d.y = (short)(y - g_ScreenOffsetY);
    d.w = (short)w;
    d.h = (short)h;
    d.r = level;
    d.g = level;
    d.b = level;
    draw_rect(&d, blend, 1);
}

// The menu's own sounds: sound\cursor / decide / cancel (.wav, or the .ogg the
// migrator writes beside it), loaded when the state starts and freed when it
// ends. The call sites keep the title bank's ids (TYPE01 = move / change a
// value, TYPE02 = confirm, CANCEL = back); this maps them to the three files.
// A file that is missing leaves that sound on the title bank's own.
enum { OPT_SND_CURSOR, OPT_SND_DECIDE, OPT_SND_CANCEL, OPT_SND_COUNT };
static const char* const kOptSndName[OPT_SND_COUNT] = { "cursor", "decide", "cancel" };
static int s_optSnd[OPT_SND_COUNT];

static void opt_sfx_load(void)
{
    for (int i = 0; i < OPT_SND_COUNT; i++) {
        char path[260];
        sprintf(path, GAME_DATA_ROOT "sound\\%s.wav", kOptSndName[i]);
        s_optSnd[i] = loadSndBankFromWav(path);
        if (s_optSnd[i] != 0) {
            pan_set(s_optSnd[i], 0);
            set_volume(s_optSnd[i], g_SfxVolume);
        }
    }
}

static void opt_sfx_free(void)
{
    for (int i = 0; i < OPT_SND_COUNT; i++) {
        if (s_optSnd[i] != 0) destroySndBank(s_optSnd[i]);
        s_optSnd[i] = 0;
    }
}

static void opt_sfx(int id)
{
    const int which = id == SFX_TITLE_CANCEL ? OPT_SND_CANCEL
                    : id == SFX_TITLE_TYPE02 ? OPT_SND_DECIDE
                                             : OPT_SND_CURSOR;
    if (s_optSnd[which] != 0) SetSndSlot(s_optSnd[which], 0);
    else play_sfx(SFX_BANKS, id);
}

// ---------------------------------------------------------------------------
// Navigation input.
//
// Two sources, ORed: the game's own bound controls (g_RawPadHeld, keyboard and
// gamepad both, function-level) and four fixed keys - the arrows, Enter and
// Esc. The fixed keys are what make it safe to rebind anything on the INPUT
// page: no layout can lock the player out of this menu.
// ---------------------------------------------------------------------------
static unsigned int opt_nav_held(void)
{
    unsigned int held = 0;
    const unsigned int pad = g_RawPadHeld;
    if (pad & OPT_PAD_UP)      held |= NAV_UP;
    if (pad & OPT_PAD_DOWN)    held |= NAV_DOWN;
    if (pad & OPT_PAD_LEFT)    held |= NAV_LEFT;
    if (pad & OPT_PAD_RIGHT)   held |= NAV_RIGHT;
    if (pad & OPT_PAD_CONFIRM) held |= NAV_CONFIRM;
    if (pad & OPT_PAD_CANCEL)  held |= NAV_CANCEL;

    if (plat_key_state(VK_UP)     & 0x8000) held |= NAV_UP;
    if (plat_key_state(VK_DOWN)   & 0x8000) held |= NAV_DOWN;
    if (plat_key_state(VK_LEFT)   & 0x8000) held |= NAV_LEFT;
    if (plat_key_state(VK_RIGHT)  & 0x8000) held |= NAV_RIGHT;
    if (plat_key_state(VK_RETURN) & 0x8000) held |= NAV_CONFIRM;
    if (plat_key_state(VK_ESCAPE) & 0x8000) held |= NAV_CANCEL;
    return held;
}

// Presses this frame: a rising edge on anything, plus auto-repeat on a held
// direction (after 18 frames, then every 4).
static unsigned int opt_nav_read(void)
{
    const unsigned int held = opt_nav_held();
    unsigned int pressed = held & ~s_navPrev;

    if ((held & NAV_DIRECTIONS) != 0 && (held & NAV_DIRECTIONS) == (s_navPrev & NAV_DIRECTIONS)) {
        s_navRepeat++;
        if (s_navRepeat >= 18 && ((s_navRepeat - 18) % 4) == 0) {
            pressed |= held & NAV_DIRECTIONS;
        }
    } else {
        s_navRepeat = 0;
    }
    s_navPrev = held;

    if (s_swallowInput) {
        if (held == 0) s_swallowInput = 0;
        return 0;
    }
    return pressed;
}

// ---------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------
static OptRow* opt_row(const char* label, const char* help)
{
    if (s_rowCount >= OPT_MAX_ROWS) return &s_rows[OPT_MAX_ROWS - 1];
    OptRow* r = &s_rows[s_rowCount++];
    r->label = label;
    r->value[0] = '\0';
    r->enabled = 1;
    r->arrows = 0;
    r->help = help;
    return r;
}

static void opt_row_value(OptRow* r, const char* value, int arrows)
{
    snprintf(r->value, sizeof(r->value), "%s", value);
    r->arrows = arrows;
}

static void opt_draw_frame(const char* title)
{
    opt_text(OPT_TITLE_X, OPT_TITLE_Y, COL_TITLE, "%s", title);
    opt_rect(OPT_TITLE_X - 4, OPT_RULE_Y, 320 - 2 * (OPT_TITLE_X - 4), 1, 0x60, 60);
    opt_text(OPT_ROW_X, OPT_HINT_Y, COL_DISABLED, "ENTER/ACTION:SELECT   ESC/CANCEL:BACK");
}

// The generic page: label on the left, value right-aligned to `valueRight`,
// a bar behind the row the cursor is on.
static void opt_draw_rows(int cursor, int width)
{
    for (int i = 0; i < s_rowCount; i++) {
        const OptRow* r = &s_rows[i];
        const int y = OPT_ROW_Y + i * OPT_ROW_PITCH;
        unsigned char colour = COL_NORMAL;
        if (!r->enabled) colour = (i == cursor) ? COL_DISABLED_SEL : COL_DISABLED;
        else if (i == cursor) colour = COL_SELECTED;

        if (i == cursor) {
            opt_rect(OPT_ROW_X - 4, y - 2, width + 8, 12, 0x30, 50);
        }
        opt_text(OPT_ROW_X, y, colour, "%s", r->label);
        if (r->value[0] != '\0') {
            const int len = (int)strlen(r->value);
            const int right = OPT_ROW_X + width;
            if (r->arrows && r->enabled && i == cursor) {
                opt_text_right(right - (len + 4) * 8, y, colour, "< %s >", r->value);
            } else {
                opt_text_right(right - (len + 2) * 8, y, colour, "%s", r->value);
            }
        }
    }
    if (cursor >= 0 && cursor < s_rowCount && s_rows[cursor].help != NULL) {
        opt_text(OPT_ROW_X, OPT_HELP_Y, COL_HELP, "%s", s_rows[cursor].help);
    }
}

static void opt_move_cursor(int* cursor, unsigned int nav)
{
    if (s_rowCount <= 0) return;
    if (nav & NAV_UP) {
        *cursor = (*cursor + s_rowCount - 1) % s_rowCount;
        opt_sfx(SFX_TITLE_TYPE01);
    }
    if (nav & NAV_DOWN) {
        *cursor = (*cursor + 1) % s_rowCount;
        opt_sfx(SFX_TITLE_TYPE01);
    }
    if (*cursor >= s_rowCount) *cursor = s_rowCount - 1;
}

static void opt_goto(int page)
{
    s_page = page;
    opt_sfx(page == PAGE_ROOT ? SFX_TITLE_CANCEL : SFX_TITLE_TYPE02);
}

// ---------------------------------------------------------------------------
// ROOT
// ---------------------------------------------------------------------------
static void page_root(unsigned int nav)
{
    s_rowCount = 0;
    opt_row("DISPLAY", "WINDOW MODE, RESOLUTION, VSYNC");
    opt_row("INPUT",   "KEYBOARD AND GAMEPAD CONTROLS");
    opt_row("GAME",    "GAME MODE, REGION AND RULES");
    opt_row("BONUS",   "OUTFITS AND BONUS WEAPONS");
    opt_row("EXIT GAME", "QUIT TO THE DESKTOP");
    opt_row("BACK",    "RETURN TO THE TITLE SCREEN");

    int* cursor = &s_cursor[PAGE_ROOT];
    if (!s_confirmQuit && !s_quitting) opt_move_cursor(cursor, nav);
    if (s_quitting) {
        // The window is closing: nothing may change now.
    } else if (s_confirmQuit) {
        // YES / NO prompt; the cursor starts on NO.
        if (nav & (NAV_LEFT | NAV_RIGHT | NAV_UP | NAV_DOWN)) {
            s_confirmQuit = (s_confirmQuit == 1) ? 2 : 1;
            opt_sfx(SFX_TITLE_TYPE01);
        }
        if (nav & NAV_CANCEL) {
            s_confirmQuit = 0;
            opt_sfx(SFX_TITLE_CANCEL);
        } else if (nav & NAV_CONFIRM) {
            if (s_confirmQuit == 2) {
                // Same as leaving the menu: what the pages changed goes to
                // disk first, since the exit-time save does not write mode
                // or region.
                ConfigFile_SaveGameSettings(s_cfgMode, s_cfgRegion);
                if (s_previewDirty) bonus_save();
                s_quitting = 1;
                opt_sfx(SFX_TITLE_TYPE02);
                plat_request_quit();
            } else {
                s_confirmQuit = 0;
                opt_sfx(SFX_TITLE_CANCEL);
            }
        }
    } else if (nav & NAV_CANCEL) {
        s_done = 1;
        opt_sfx(SFX_TITLE_CANCEL);
    } else if (nav & NAV_CONFIRM) {
        if (*cursor == 4) {
            s_confirmQuit = 1;          // 1 = NO selected, 2 = YES selected
            opt_sfx(SFX_TITLE_TYPE02);
        } else if (*cursor == 5) {
            s_done = 1;
            opt_sfx(SFX_TITLE_CANCEL);
        } else {
            opt_goto(PAGE_DISPLAY + *cursor);
        }
    }

    opt_draw_frame("OPTIONS");
    opt_draw_rows(*cursor, 288);
    if (s_quitting) {
        opt_text(OPT_ROW_X, OPT_HELP_Y - 14, COL_GOOD, "CLOSING...");
    } else if (s_confirmQuit) {
        const int y = OPT_ROW_Y + 6 * OPT_ROW_PITCH + 8;
        opt_rect(OPT_ROW_X - 4, y - 4, 288 + 8, 40, 0x30, 40);
        opt_text(OPT_ROW_X + 8, y, COL_SELECTED, "EXIT TO THE DESKTOP?");
        opt_text(OPT_ROW_X + 8,   y + 16, s_confirmQuit == 1 ? COL_SELECTED : COL_NORMAL,
                 s_confirmQuit == 1 ? "> NO" : "  NO");
        opt_text(OPT_ROW_X + 80,  y + 16, s_confirmQuit == 2 ? COL_SELECTED : COL_NORMAL,
                 s_confirmQuit == 2 ? "> YES" : "  YES");
    }
}

// ROOT, as the in-game menu shows it: DISPLAY, INPUT and the way back to the
// title. The cancel key closes the menu and the game goes on.
static void page_root_ingame(unsigned int nav)
{
    s_rowCount = 0;
    opt_row("DISPLAY", "WINDOW MODE, RESOLUTION, VSYNC");
    opt_row("INPUT",   "KEYBOARD AND GAMEPAD CONTROLS");
    opt_row("BACK TO TITLE", "UNSAVED PROGRESS IS LOST");
    opt_row("BACK", "RETURN TO THE GAME");

    int* cursor = &s_cursor[PAGE_ROOT];
    if (!s_confirmQuit) opt_move_cursor(cursor, nav);
    if (s_confirmQuit) {
        // YES / NO prompt; the cursor starts on NO.
        if (nav & (NAV_LEFT | NAV_RIGHT | NAV_UP | NAV_DOWN)) {
            s_confirmQuit = (s_confirmQuit == 1) ? 2 : 1;
            opt_sfx(SFX_TITLE_TYPE01);
        }
        if (nav & NAV_CANCEL) {
            s_confirmQuit = 0;
            opt_sfx(SFX_TITLE_CANCEL);
        } else if (nav & NAV_CONFIRM) {
            if (s_confirmQuit == 2) {
                s_wantTitle = 1;
                s_done = 1;
                opt_sfx(SFX_TITLE_TYPE02);
            } else {
                s_confirmQuit = 0;
                opt_sfx(SFX_TITLE_CANCEL);
            }
        }
    } else if (nav & NAV_CANCEL) {
        s_done = 1;
        opt_sfx(SFX_TITLE_CANCEL);
    } else if (nav & NAV_CONFIRM) {
        if (*cursor == 2) {
            s_confirmQuit = 1;
            opt_sfx(SFX_TITLE_TYPE02);
        } else if (*cursor == 3) {
            s_done = 1;
            opt_sfx(SFX_TITLE_CANCEL);
        } else {
            opt_goto(PAGE_DISPLAY + *cursor);       // DISPLAY, INPUT
        }
    }

    opt_draw_frame("OPTIONS");
    opt_draw_rows(*cursor, 288);
    if (s_confirmQuit) {
        const int y = OPT_ROW_Y + 4 * OPT_ROW_PITCH + 8;
        opt_rect(OPT_ROW_X - 4, y - 4, 288 + 8, 40, 0x30, 40);
        opt_text(OPT_ROW_X + 8, y, COL_SELECTED, "RETURN TO THE TITLE SCREEN?");
        opt_text(OPT_ROW_X + 8,   y + 16, s_confirmQuit == 1 ? COL_SELECTED : COL_NORMAL,
                 s_confirmQuit == 1 ? "> NO" : "  NO");
        opt_text(OPT_ROW_X + 80,  y + 16, s_confirmQuit == 2 ? COL_SELECTED : COL_NORMAL,
                 s_confirmQuit == 2 ? "> YES" : "  YES");
    }
}

// ---------------------------------------------------------------------------
// DISPLAY
// ---------------------------------------------------------------------------
static void display_build_resolutions(void)
{
    int deskW = 0, deskH = 0;
    plat_display_desktop_size(&deskW, &deskH);

    s_resCount = 0;
    int current = -1;
    for (int i = 0; i < RES_TABLE_COUNT; i++) {
        const int isCurrent = kResolutions[i].w == (int)g_dwScreenWidth &&
                              kResolutions[i].h == (int)g_dwScreenHeight;
        // A window larger than the desktop cannot be shown whole. The size
        // config.ini already holds is always listed.
        if (!isCurrent && deskW > 0 && deskH > 0 &&
            (kResolutions[i].w > deskW || kResolutions[i].h > deskH)) {
            continue;
        }
        if (isCurrent) current = s_resCount;
        s_res[s_resCount++] = kResolutions[i];
    }
    if (current < 0) {
        current = s_resCount;
        s_res[s_resCount].w = (int)g_dwScreenWidth;
        s_res[s_resCount].h = (int)g_dwScreenHeight;
        s_resCount++;
    }
    s_dispRes = current;
    s_dispFull = g_bFullScreen ? 1 : 0;
    s_dispVsync = g_bVSync ? 1 : 0;
}

static int display_is_dirty(void)
{
    return s_dispFull != (g_bFullScreen ? 1 : 0) ||
           s_dispVsync != (g_bVSync ? 1 : 0) ||
           s_res[s_dispRes].w != (int)g_dwScreenWidth ||
           s_res[s_dispRes].h != (int)g_dwScreenHeight;
}

static void display_apply(void)
{
    if (!display_is_dirty()) return;
    g_bFullScreen = s_dispFull ? TRUE : FALSE;
    g_bVSync = s_dispVsync ? TRUE : FALSE;
    g_dwScreenWidth = (DWORD)s_res[s_dispRes].w;
    g_dwScreenHeight = (DWORD)s_res[s_dispRes].h;
    plat_display_request_apply();
}

static void page_display(unsigned int nav)
{
    char buf[28];
    s_rowCount = 0;

    OptRow* r = opt_row("WINDOW MODE", "BORDERLESS FULLSCREEN, 4:3 PICTURE");
    opt_row_value(r, s_dispFull ? "FULLSCREEN" : "WINDOWED", 1);

    r = opt_row("RESOLUTION", s_dispFull ? "FULLSCREEN USES THE DESKTOP SIZE"
                                         : "WIDE SIZES KEEP A 4:3 PICTURE");
    snprintf(buf, sizeof(buf), "%dx%d", s_res[s_dispRes].w, s_res[s_dispRes].h);
    opt_row_value(r, buf, 1);

    r = opt_row("VSYNC", "WAIT FOR THE DISPLAY ON EVERY FRAME");
    opt_row_value(r, s_dispVsync ? "ON" : "OFF", 1);

    r = opt_row("APPLY", display_is_dirty() ? "APPLY THE CHANGES NOW" : "NOTHING TO APPLY");
    r->enabled = display_is_dirty();
    opt_row("BACK", "CHANGES ARE APPLIED WHEN YOU LEAVE");

    int* cursor = &s_cursor[PAGE_DISPLAY];
    opt_move_cursor(cursor, nav);

    const int step = (nav & NAV_RIGHT) ? 1 : (nav & NAV_LEFT) ? -1 : 0;
    const int toggle = step != 0 || (nav & NAV_CONFIRM) != 0;
    switch (*cursor) {
    case 0:
        if (toggle) { s_dispFull = !s_dispFull; opt_sfx(SFX_TITLE_TYPE01); }
        break;
    case 1:
        if (toggle) {
            const int d = (step != 0) ? step : 1;
            s_dispRes = (s_dispRes + s_resCount + d) % s_resCount;
            opt_sfx(SFX_TITLE_TYPE01);
        }
        break;
    case 2:
        if (toggle) { s_dispVsync = !s_dispVsync; opt_sfx(SFX_TITLE_TYPE01); }
        break;
    case 3:
        if ((nav & NAV_CONFIRM) && display_is_dirty()) {
            display_apply();
            opt_sfx(SFX_TITLE_TYPE02);
        }
        break;
    case 4:
        if (nav & NAV_CONFIRM) { display_apply(); opt_goto(PAGE_ROOT); return; }
        break;
    }
    if (nav & NAV_CANCEL) { display_apply(); opt_goto(PAGE_ROOT); return; }

    opt_draw_frame("OPTIONS - DISPLAY");
    opt_draw_rows(*cursor, 288);
}

// ---------------------------------------------------------------------------
// INPUT
// ---------------------------------------------------------------------------
static const char* key_name(unsigned char vk, char* buf, size_t size)
{
    switch (vk) {
    case 0x0D: return "ENTER";
    case 0x20: return "SPACE";
    case 0x11: return "CTRL";
    case 0x10: return "SHIFT";
    case 0x1B: return "ESC";
    case 0x25: return "LEFT";
    case 0x26: return "UP";
    case 0x27: return "RIGHT";
    case 0x28: return "DOWN";
    case 0x6E: return "NUM.";
    case 0xBA: return ";";
    case 0xBB: return "=";
    case 0xBC: return ",";
    case 0xBD: return "-";
    case 0xBE: return ".";
    case 0xBF: return "/";
    case 0xC0: return "`";
    case 0xDB: return "[";
    case 0xDC: return "BSLASH";
    case 0xDD: return "]";
    case 0xDE: return "'";
    default: break;
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        snprintf(buf, size, "%c", vk);
    } else if (vk >= 0x60 && vk <= 0x69) {
        snprintf(buf, size, "NUM%d", vk - 0x60);
    } else {
        snprintf(buf, size, "K%02X", vk);
    }
    return buf;
}

// Pad mask bit -> name. Bits 0-3 are the stick, 4-7 the POV hat / D-pad, 8 and
// up the buttons (MarniInput.h's mask contract).
static const char* pad_name(int bit, char* buf, size_t size)
{
    static const char* const kDirs[8] = {
        "LS-U", "LS-D", "LS-L", "LS-R", "DP-U", "DP-D", "DP-L", "DP-R",
    };
    if (bit >= 0 && bit < 8) return kDirs[bit];
    snprintf(buf, size, "B%d", bit - 7);
    return buf;
}

// Every binding of one function in one column, space separated, cut to fit.
static void binding_list(int function, int column, char* out, size_t size)
{
    const unsigned int mask = kFunctions[function].mask;
    char name[12];
    out[0] = '\0';
    for (int i = 0; i < 32; i++) {
        const char* n = NULL;
        if (column == 0) {
            if (g_JoyRemapTbl[0][i] == mask && g_keyBindingData[i] != 0) {
                n = key_name(g_keyBindingData[i], name, sizeof(name));
            }
        } else if (g_JoyRemapTbl[1][i] == mask) {
            n = pad_name(i, name, sizeof(name));
        }
        if (n == NULL) continue;
        const size_t used = strlen(out);
        if (used + strlen(n) + 2 > size) break;
        if (used != 0) strcat(out, " ");
        strcat(out, n);
    }
    if (out[0] == '\0') snprintf(out, size, "-");
}

// Give `function` the key `vk`. The function's first slot takes it; if another
// slot already had that key it gets the first slot's old key, so no function
// loses its only binding and no key ends up on two functions.
static void bind_key(int function, unsigned char vk)
{
    const unsigned int mask = kFunctions[function].mask;
    int primary = -1;
    for (int i = 0; i < 32; i++) {
        if (g_JoyRemapTbl[0][i] == mask) { primary = i; break; }
    }
    if (primary < 0) return;
    for (int i = 0; i < 32; i++) {
        if (i == primary || g_keyBindingData[i] != vk) continue;
        if (g_JoyRemapTbl[0][i] == mask) return;       // already bound to it
        g_keyBindingData[i] = g_keyBindingData[primary];
    }
    g_keyBindingData[primary] = vk;
    InitInputKeyBindings();
}

// The same for a pad button: the button takes the function, and the function's
// first button takes what the button did before.
static void bind_pad(int function, int bit)
{
    const DWORD mask = kFunctions[function].mask;
    if (bit < 0 || bit >= 32 || g_JoyRemapTbl[1][bit] == mask) return;
    int primary = -1;
    for (int i = 0; i < 32; i++) {
        if (g_JoyRemapTbl[1][i] == mask) { primary = i; break; }
    }
    const DWORD previous = g_JoyRemapTbl[1][bit];
    g_JoyRemapTbl[1][bit] = mask;
    if (primary >= 0) g_JoyRemapTbl[1][primary] = previous;
}

static int capture_any_key_down(void)
{
    for (int i = 0; i < BINDABLE_KEY_COUNT; i++) {
        if (plat_key_state(kBindableKeys[i]) & 0x8000) return kBindableKeys[i];
    }
    return 0;
}

// Runs while a binding is being captured. Returns 1 while it owns the frame.
static int input_capture(int function)
{
    if (s_capture == 0) return 0;

    const int escape = (plat_key_state(VK_ESCAPE) & 0x8000) != 0;
    const int key = capture_any_key_down();
    const unsigned int pad = g_bPadConnected ? (unsigned int)read_sidewinder_pad() : 0;

    if (s_capture == 1) {
        // The confirm press that started the capture may still be down. Only
        // the device being captured is waited on: a pad resting off-centre
        // must not hold up a keyboard binding, nor the other way round.
        const int busy = (s_inputColumn == 0) ? (key != 0) : (pad != 0);
        if (!busy && !escape) {
            s_capture = 2;
            s_captureTimer = CAPTURE_TIMEOUT_FRAMES;
        }
        return 1;
    }

    int finished = 0;
    if (escape || --s_captureTimer <= 0) {
        finished = 1;
        opt_sfx(SFX_TITLE_CANCEL);
    } else if (s_inputColumn == 0 && key != 0) {
        bind_key(function, (unsigned char)key);
        finished = 1;
        opt_sfx(SFX_TITLE_TYPE02);
    } else if (s_inputColumn == 1 && pad != 0) {
        int bit = 0;
        while (bit < 32 && (pad & (1u << bit)) == 0) bit++;
        bind_pad(function, bit);
        finished = 1;
        opt_sfx(SFX_TITLE_TYPE02);
    }
    if (finished) {
        s_capture = 0;
        s_swallowInput = 1;     // the captured key must not also move the cursor
    }
    return 1;
}

static void page_input(unsigned int nav)
{
    int* cursor = &s_cursor[PAGE_INPUT];
    const int capturing = (*cursor < FUNCTION_COUNT) ? input_capture(*cursor) : 0;
    if (capturing) nav = 0;

    s_rowCount = FUNCTION_COUNT + 3;
    if (!capturing) {
        opt_move_cursor(cursor, nav);
        if (*cursor < FUNCTION_COUNT && (nav & (NAV_LEFT | NAV_RIGHT))) {
            s_inputColumn = !s_inputColumn;
            opt_sfx(SFX_TITLE_TYPE01);
        }
        if (nav & NAV_CANCEL) { opt_goto(PAGE_ROOT); return; }
        if (nav & NAV_CONFIRM) {
            if (*cursor < FUNCTION_COUNT) {
                if (s_inputColumn == 0 || g_bPadConnected) {
                    s_capture = 1;
                    opt_sfx(SFX_TITLE_TYPE02);
                } else {
                    opt_sfx(SFX_TITLE_CANCEL);
                }
            } else if (*cursor == INPUT_ROW_RESET_KEYS) {
                memcpy(g_keyBindingData, kDefaultKeys, sizeof(kDefaultKeys));
                InitInputKeyBindings();
                opt_sfx(SFX_TITLE_TYPE02);
            } else if (*cursor == INPUT_ROW_RESET_PAD) {
                // An empty table is what InstallPadDefaultBindings fills in,
                // with the layout that matches the connected pad's backend.
                memset(g_JoyRemapTbl[1], 0, sizeof(g_JoyRemapTbl[1]));
                InstallPadDefaultBindings();
                if (g_JoyRemapTbl[1][0] == 0) {
                    memcpy(g_JoyRemapTbl[1], g_JoyRemapTblLegacyJoyDefault,
                           sizeof(g_JoyRemapTbl[1]));
                    InstallPadDefaultBindings();
                }
                opt_sfx(SFX_TITLE_TYPE02);
            } else {
                opt_goto(PAGE_ROOT);
                return;
            }
        }
    }

    opt_draw_frame("OPTIONS - INPUT");

    const int keyX = 112;
    const int padX = 216;
    const int pitch = 12;
    const int top = 44;
    opt_text(keyX, 30, s_inputColumn == 0 ? COL_SELECTED : COL_NORMAL, "KEYBOARD");
    opt_text(padX, 30, s_inputColumn == 1 ? COL_SELECTED : COL_NORMAL, "GAMEPAD");

    char list[14];
    for (int i = 0; i < FUNCTION_COUNT; i++) {
        const int y = top + i * pitch;
        const int here = (i == *cursor);
        opt_text(OPT_ROW_X, y, here ? COL_SELECTED : COL_NORMAL, "%s", kFunctions[i].label);
        for (int col = 0; col < 2; col++) {
            const int x = col == 0 ? keyX : padX;
            const int active = here && col == s_inputColumn;
            if (active) opt_rect(x - 4, y - 2, 13 * 8, 12, 0x30, 50);
            if (active && s_capture != 0) {
                opt_text(x, y, COL_GOOD, col == 0 ? "PRESS A KEY" : "PRESS BUTTON");
            } else if (col == 1 && !g_bPadConnected) {
                opt_text(x, y, COL_DISABLED, "NO GAMEPAD");
            } else {
                binding_list(i, col, list, sizeof(list));
                opt_text(x, y, active ? COL_SELECTED : COL_NORMAL, "%s", list);
            }
        }
    }

    static const char* const kTail[3] = { "RESET KEYBOARD", "RESET GAMEPAD", "BACK" };
    for (int i = 0; i < 3; i++) {
        const int row = FUNCTION_COUNT + i;
        const int y = top + row * pitch + 6;
        if (row == *cursor) opt_rect(OPT_ROW_X - 4, y - 2, 15 * 8 + 8, 12, 0x30, 50);
        opt_text(OPT_ROW_X, y, row == *cursor ? COL_SELECTED : COL_NORMAL, "%s", kTail[i]);
    }

    if (s_capture != 0) {
        opt_text(OPT_ROW_X, OPT_HELP_Y, COL_HELP, "ESC CANCELS");
    } else if (*cursor < FUNCTION_COUNT) {
        opt_text(OPT_ROW_X, OPT_HELP_Y, COL_HELP, "LEFT/RIGHT: COLUMN   SELECT: REBIND");
    } else if (*cursor == INPUT_ROW_RESET_KEYS) {
        opt_text(OPT_ROW_X, OPT_HELP_Y, COL_HELP, "RESTORE THE DEFAULT KEYS");
    } else if (*cursor == INPUT_ROW_RESET_PAD) {
        opt_text(OPT_ROW_X, OPT_HELP_Y, COL_HELP, "RESTORE THE DEFAULT GAMEPAD LAYOUT");
    }
}

// ---------------------------------------------------------------------------
// GAME
// ---------------------------------------------------------------------------
// A probe against the tree of region `jpn`, whatever is running: the overlay
// folder depends on the region too (SATURN vs SATURN_JPN, AssetPath.cpp).
static int probe_in_region(int jpn, const char* overlay, const char* rel)
{
    const int running = GetAssetVersion();
    if (running != jpn) SetAssetVersion(jpn ? "JPN" : "USA");
    const int present = (overlay != NULL) ? AssetOverlayHasFile(overlay, rel)
                                          : AssetBaseHasFile(jpn, rel);
    if (running != jpn) SetAssetVersion(running ? "JPN" : "USA");
    return present;
}

static int region_available(int jpn)
{
    return AssetBaseHasFile(jpn, jpn ? "Data/FONT.TIM" : "Data/fontus.tim");
}

static int mode_available(int mode, int jpn)
{
    switch (mode) {
    case GAME_MODE_OG:     return 1;
    case GAME_MODE_DC:     return probe_in_region(jpn, "DC", "Data/BT367OAB.TIM");
    // The Battle Game's arenas are the one thing only the Saturn overlay has
    // (saturn_battle_assets_present checks all fifteen; one is enough here).
    case GAME_MODE_SATURN: return probe_in_region(jpn, "SATURN", "Stage8/ROOM8010.RDT");
    default:               return 0;
    }
}

static const char* mode_label(int mode)
{
    switch (mode) {
    case GAME_MODE_DC:     return "DIRECTOR'S CUT";
    case GAME_MODE_SATURN: return "SATURN";
    default:               return "ORIGINAL";
    }
}

// The PS1 staff credits need their text and one of the two credit movies.
static int ps1_credits_available(void)
{
    return AssetBaseHasFile(-1, "Data/STAFF.STF") &&
           (AssetBaseHasFile(-1, "Movie/STFC.mp4") || AssetBaseHasFile(-1, "Movie/STFJ.mp4"));
}

static int ps1_subtitles_available(void)
{
    return AssetBaseHasFile(1, "Data/jimaku00.png");
}

static void page_game(unsigned int nav)
{
    // The settings are on disk and the window is closing: nothing may change now.
    if (s_restarting) nav = 0;

    s_rowCount = 0;
    const int restart = (s_cfgMode != g_GameMode) || (s_cfgRegion != GetAssetVersion()) ||
                        (s_cfgHd != (HdTextures_ReplaceEnabled() ? 1 : 0));

    OptRow* r = opt_row("GAME MODE", "WHICH RELEASE TO PLAY. NEEDS RESTART");
    opt_row_value(r, mode_label(s_cfgMode), 1);

    r = opt_row("REGION", "USA OR JAPAN ASSETS. NEEDS A RESTART");
    opt_row_value(r, s_cfgRegion ? "JAPAN" : "USA", 1);

    const int creditsOk = ps1_credits_available();
    r = opt_row("PS1 ENDING CREDITS", g_bDcMode ? "ALWAYS ON IN THE DIRECTOR'S CUT"
                                      : creditsOk ? "PS1 STAFF CREDITS OVER THE ENDING"
                                                  : "PS1 CREDIT ASSETS ARE NOT INSTALLED");
    opt_row_value(r, (g_bDcMode || g_bPs1EndingCredits) ? "ON" : "OFF", 1);
    r->enabled = !g_bDcMode && creditsOk;

    const int subsOk = ps1_subtitles_available();
    r = opt_row("PS1 FMV SUBTITLES", subsOk ? "JAPANESE SUBTITLES OVER THE PROLOGUE"
                                            : "SUBTITLE ASSETS ARE NOT INSTALLED");
    opt_row_value(r, g_bPs1FmvSubtitles ? "ON" : "OFF", 1);
    r->enabled = subsOk;

    r = opt_row("SKIP ANY FMV", "LET EVERY MOVIE BE SKIPPED");
    opt_row_value(r, g_bSkipUnskippableFmv ? "ON" : "OFF", 1);

    const int battleOk = saturn_battle_assets_present();
    r = opt_row("BATTLE GAME", battleOk ? "OFFER THE SATURN BATTLE GAME (EXTRA)"
                                        : "SATURN ASSETS ARE NOT INSTALLED");
    opt_row_value(r, g_bSaturnBattleGame ? "ON" : "OFF", 1);
    r->enabled = battleOk;

    r = opt_row("WESTERN DIFFICULTY", "PS1/SATURN USA RULES IN ORIGINAL MODE");
    opt_row_value(r, g_bWesternDifficultyOption ? "ON" : "OFF", 1);

    r = opt_row("HD MODE", "HD BACKGROUNDS, TEXTURES AND FONTS. NEEDS RESTART");
    opt_row_value(r, s_cfgHd ? "ON" : "OFF", 1);

    r = opt_row("APPLY AND RESTART", restart ? "RESTART THE GAME NOW WITH THE NEW SETTINGS"
                                             : "MODE, REGION AND HD MODE ARE UNCHANGED");
    r->enabled = restart;
    opt_row("BACK", NULL);

    int* cursor = &s_cursor[PAGE_GAME];
    opt_move_cursor(cursor, nav);

    const int step = (nav & NAV_RIGHT) ? 1 : (nav & NAV_LEFT) ? -1 : 0;
    const int toggle = step != 0 || (nav & NAV_CONFIRM) != 0;
    const int enabled = s_rows[*cursor].enabled;
    if (toggle && !enabled) {
        opt_sfx(SFX_TITLE_CANCEL);
    } else if (toggle) {
        switch (*cursor) {
        case 0: {
            // Step to the next mode whose folder is installed.
            const int d = (step != 0) ? step : 1;
            int mode = s_cfgMode;
            for (int i = 0; i < 3; i++) {
                mode = (mode + 3 + d) % 3;
                if (mode_available(mode, s_cfgRegion)) break;
            }
            if (mode != s_cfgMode) { s_cfgMode = mode; opt_sfx(SFX_TITLE_TYPE01); }
            else opt_sfx(SFX_TITLE_CANCEL);
            break;
        }
        case 1:
            if (region_available(!s_cfgRegion)) {
                s_cfgRegion = !s_cfgRegion;
                // The mode's overlay may not exist for the other region.
                if (!mode_available(s_cfgMode, s_cfgRegion)) s_cfgMode = GAME_MODE_OG;
                opt_sfx(SFX_TITLE_TYPE01);
            } else {
                opt_sfx(SFX_TITLE_CANCEL);
            }
            break;
        case 2: g_bPs1EndingCredits = !g_bPs1EndingCredits; opt_sfx(SFX_TITLE_TYPE01); break;
        case 3: g_bPs1FmvSubtitles = !g_bPs1FmvSubtitles; opt_sfx(SFX_TITLE_TYPE01); break;
        case 4: g_bSkipUnskippableFmv = !g_bSkipUnskippableFmv; opt_sfx(SFX_TITLE_TYPE01); break;
        case 5: g_bSaturnBattleGame = !g_bSaturnBattleGame; opt_sfx(SFX_TITLE_TYPE01); break;
        case 6: g_bWesternDifficultyOption = !g_bWesternDifficultyOption; opt_sfx(SFX_TITLE_TYPE01); break;
        case 7:
            s_cfgHd = !s_cfgHd;
            ConfigFile_SetHdTextures(s_cfgHd);
            opt_sfx(SFX_TITLE_TYPE01);
            break;
        case 8:
            // Everything the new process reads has to be on disk before the
            // restart is asked for: the exit-time save does not write the
            // mode or the region (ConfigFile.h).
            if ((nav & NAV_CONFIRM) && !s_restarting) {
                ConfigFile_SaveGameSettings(s_cfgMode, s_cfgRegion);
                if (s_previewDirty) bonus_save();
                s_restarting = 1;
                opt_sfx(SFX_TITLE_TYPE02);
                plat_request_restart();
            }
            break;
        case 9:
            if (nav & NAV_CONFIRM) { opt_goto(PAGE_ROOT); return; }
            break;
        }
    }
    if ((nav & NAV_CANCEL) && !s_restarting) { opt_goto(PAGE_ROOT); return; }

    opt_draw_frame("OPTIONS - GAME");
    opt_draw_rows(*cursor, 288);
    if (s_restarting) {
        opt_text(OPT_ROW_X, OPT_HELP_Y - 14, COL_GOOD, "RESTARTING...");
    } else if (restart) {
        opt_text(OPT_ROW_X, OPT_HELP_Y - 14, COL_GOOD, "THIS CHANGE NEEDS A RESTART");
    }
}

// ---------------------------------------------------------------------------
// BONUS
// ---------------------------------------------------------------------------
static const unsigned char kCharModelId[BONUS_CHAR_COUNT] = { CHAR_CHRIS, CHAR_JILL, CHAR_REBECCA };

// Who is shown aiming each bonus weapon, and the item id they hold (the same
// items bonus_grant_start_weapons gives).
static const int kWeaponOwner[BONUS_WEAPON_COUNT] = {
    BONUS_CHAR_CHRIS,   // rocket launcher
    BONUS_CHAR_JILL,    // Colt Python
    BONUS_CHAR_JILL,    // Ingram
    BONUS_CHAR_CHRIS,   // Minimi
};
static const int kWeaponItem[BONUS_WEAPON_COUNT] = {
    ITEM_ROCKET_LAUNCHER, ITEM_COLT_PYTHON_MAG, ITEM_INGRAM, ITEM_MINIMI,
};

// Camera for the outfit preview: on the +Z axis looking at the origin, which is
// the original options screen's own arrangement (its camera record is from
// (0, 0, 5000) to the origin, with the model at x 0x960, y 0x834), moved back
// with a longer lens so the figure is nearly flat.
//
// One world unit is PREVIEW_LENS / PREVIEW_CAMERA_Z = 0.0483 pixels there. A
// player model stands about 3020 units tall (measured on screen: 129 pixels at
// 0.0427), so it comes out about 146 pixels - feet at screen y 195, head near
// 49 - which fills the space between the name above it and the outfit below.
//
// The model is placed in WORLD space, with the projection centre left at the
// screen's (160, 120). Moving the centre instead (SetSubpixelOffset) shifts a
// model twice - the 3D pass uses it both as the projection centre and to lean
// the view - which is what put the first version half off the right edge.
// The camera's right axis is world -X, so a negative x is to the right.
#define PREVIEW_CAMERA_Z     10600
#define PREVIEW_LENS         0x200
#define PREVIEW_FLOOR_Y      1553    // (195 - 120) pixels below the centre
#define PREVIEW_MODEL_X      (-2130) // (263 - 160) pixels right of the centre
#define PREVIEW_CENTER_X     263     // the middle of the panel right of the rule (206..320)
#define PREVIEW_TURN_STEP    12      // of 4096 per frame

static void preview_scene_setup(void)
{
    // The options screen's three lights (0x004761b0): all a little to the
    // front of the model, which with this camera is the side being looked at.
    static int light0[4], light1[4], light2[4];
    light0[0] = 100;  light0[1] = 0x50; light0[2] = -780;
    light1[0] = -100; light1[1] = 0x50; light1[2] = -780;
    light2[0] = -100; light2[1] = 0;    light2[2] = -780;
    int* const lights[3] = { light0, light1, light2 };
    for (int i = 0; i < 3; i++) {
        unsigned char* rgb = (unsigned char*)&lights[i][3];
        rgb[0] = 0x80; rgb[1] = 0x80; rgb[2] = 0x80;
        FUN_0040ac80(i, lights[i]);
    }
    setBackColor(1024, 1024, 1024);

    // MatrixToCamera reads eight ints - from x/y/z, to x/y/z, roll - out of
    // whatever it is handed.
    static int camera[8];
    memset(camera, 0, sizeof(camera));
    camera[2] = PREVIEW_CAMERA_Z;
    set_scene_render_param(PREVIEW_LENS);
    MatrixToCamera((MATRIX*)camera);
    SetSubpixelOffset(160, 120);
}

// Load `character` wearing `outfit` into the player entity. The same call the
// game makes when it starts (SetupCharacterData, 0x00494fc0), with the outfit
// forced through Bonus.cpp's preview override instead of the saved choice.
// `weaponItem` is an item id to hold aimed (0 = empty-handed, standing).
static void preview_load(int character, int outfit, int weaponItem)
{
    if (s_previewReady && character == s_previewChar && outfit == s_previewOutfit
        && weaponItem == s_previewWeaponItem) return;

    // SetupCharacterData takes the weapon from the equipped item-box slot
    // (g_itemboxSlots[g_EquippedItemId + 47]); lend it slot 1 for the load.
    const unsigned char savedEquipped = g_EquippedItemId;
    const unsigned char savedSlotId = g_itemboxSlots[48].Id;
    g_EquippedItemId = weaponItem != 0 ? 1 : 0;
    if (weaponItem != 0) g_itemboxSlots[48].Id = (unsigned char)weaponItem;
    g_playerEntity.id = kCharModelId[character];
    bonus_preview_outfit(outfit);
    SetupCharacterData();
    bonus_preview_outfit(-1);
    g_itemboxSlots[48].Id = savedSlotId;
    g_EquippedItemId = savedEquipped;

    ENTITY = (Entity*)&g_playerEntity;
    g_playerEntity.unk_8c = 0;
    g_playerEntity.attackAnim = 0;              // the model's own animation 0: standing
    g_playerEntity.animation_frame_id = 0;
    g_playerEntity.unk_bf = 0;
    g_playerEntity.scaMatrixData.localMatrix.t[0] = PREVIEW_MODEL_X;
    g_playerEntity.scaMatrixData.localMatrix.t[1] = PREVIEW_FLOOR_Y;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = 0;
    g_playerEntity.position.pad = 0;
    g_playerEntity.speed.x = 0;
    if (!s_previewReady) g_playerEntity.directionAngle = 0xce4;

    preview_scene_setup();
    s_previewChar = character;
    s_previewOutfit = outfit;
    s_previewWeaponItem = weaponItem;
    s_previewReady = 1;
}

static void preview_draw(void)
{
    if (!s_previewReady) return;
    // zoneFlags: the renderer only takes its unconditional path while the low
    // seven bits are set (see options_menu_render).
    g_playerEntity.zoneFlags = 1;
    ENTITY = (Entity*)&g_playerEntity;
    g_playerEntity.directionAngle =
        (short)((g_playerEntity.directionAngle + PREVIEW_TURN_STEP) & 0xFFF);
    if (s_previewWeaponItem == 0) {
        g_playerEntity.attackAnim = 0;
        Joint_move(0, g_playerEntity.animHeader, g_playerEntity.animBase, 0x400);
    } else {
        // The aimed pose is the last frame of the weapon file's raise motion
        // (player_behavior_13_gun_raise, PlayerAnimations.cpp): motion 7 for the
        // normal guns, motion 5 for the Ingram / Minimi (id > 0x6e) held at the
        // end of their frame window. The motion table is the last dword of the
        // file (see AdjustWeaponAnimationPositions); entry = {frame count, ...}.
        const int special = g_playerEntity.equippedWeaponId > 0x6e;
        const int motion = special ? 5 : 7;
        const unsigned int table = g_playerEntity.jointMoveData1;
        unsigned int frame = *(unsigned short*)(table + motion * 4) - 1;
        if (special) {
            const unsigned int end = g_weaponSpecialFrameWindows[
                ((g_playerEntity.id & 1) * 3 + ((motion - 1) & 3)) * 4 + 1];
            if (frame > end - 1) frame = end - 1;
        }
        g_playerEntity.attackAnim = (unsigned char)motion;
        g_playerEntity.animation_frame_id = (unsigned char)frame;
        g_playerEntity.unk_bf = 0;              // no frame wait
        g_playerEntity.unk_8c = 0;              // no blend: the pose, not the way there
        Joint_move(0, g_playerEntity.jointMoveData0, g_playerEntity.jointMoveData1, 0x400);
    }
    EntityComputeJointWorldMatrices(g_playerEntity.unk_ca);
    EntityApplyLookAtRotation();
    options_render_entity((int)&g_playerEntity);
}

static int outfit_step(int character, int outfit, int d)
{
    for (int i = 0; i < BONUS_OUTFIT_COUNT; i++) {
        outfit = (outfit + BONUS_OUTFIT_COUNT + d) % BONUS_OUTFIT_COUNT;
        if (bonus_outfit_available(character, outfit)) return outfit;
    }
    return BONUS_OUTFIT_DEFAULT;
}

static int outfit_choices(int character)
{
    int n = 0;
    for (int o = 0; o < BONUS_OUTFIT_COUNT; o++) {
        if (bonus_outfit_available(character, o)) n++;
    }
    return n;
}

static void page_bonus(unsigned int nav)
{
    s_rowCount = 0;
    for (int c = 0; c < BONUS_CHAR_COUNT; c++) {
        const int choices = outfit_choices(c);
        OptRow* r = opt_row(bonus_character_name(c), choices > 1 ? NULL
                                                        : "NO OTHER OUTFIT UNLOCKED YET");
        opt_row_value(r, bonus_outfit_name(bonus_outfit_get(c)), choices > 1);
    }
    s_bonusWeaponCount = 0;
    for (int w = 0; w < BONUS_WEAPON_COUNT; w++) {
        if (!bonus_weapon_unlocked(w)) continue;
        s_bonusWeapons[s_bonusWeaponCount++] = w;
        OptRow* r = opt_row(bonus_weapon_name(w), "PUT IN THE ITEM BOX ON A NEW GAME");
        opt_row_value(r, bonus_weapon_enabled(w) ? "ON" : "OFF", 1);
    }
    const int backRow = s_rowCount;
    opt_row("BACK", NULL);

    int* cursor = &s_cursor[PAGE_BONUS];
    opt_move_cursor(cursor, nav);

    const int step = (nav & NAV_RIGHT) ? 1 : (nav & NAV_LEFT) ? -1 : 0;
    const int toggle = step != 0 || (nav & NAV_CONFIRM) != 0;
    if (*cursor < BONUS_CHAR_COUNT) {
        if (toggle && s_rows[*cursor].arrows) {
            const int now = bonus_outfit_get(*cursor);
            const int next = outfit_step(*cursor, now, step != 0 ? step : 1);
            if (next != now) {
                bonus_outfit_set(*cursor, next);
                s_previewDirty = 1;
                opt_sfx(SFX_TITLE_TYPE01);
            }
        } else if (toggle) {
            opt_sfx(SFX_TITLE_CANCEL);
        }
    } else if (*cursor < backRow) {
        if (toggle) {
            const int w = s_bonusWeapons[*cursor - BONUS_CHAR_COUNT];
            bonus_weapon_set_enabled(w, !bonus_weapon_enabled(w));
            s_previewDirty = 1;
            opt_sfx(SFX_TITLE_TYPE01);
        }
    } else if (nav & NAV_CONFIRM) {
        opt_goto(PAGE_ROOT);
        return;
    }
    if (nav & NAV_CANCEL) { opt_goto(PAGE_ROOT); return; }

    // The model beside the list: the character the cursor is on, in the outfit
    // chosen for them. On a weapon row the last one shown stays.
    if (*cursor < BONUS_CHAR_COUNT) {
        preview_load(*cursor, bonus_outfit_get(*cursor), 0);
    } else if (*cursor < backRow) {
        // A weapon row: its owner, in their chosen outfit, aiming it.
        const int w = s_bonusWeapons[*cursor - BONUS_CHAR_COUNT];
        const int who = kWeaponOwner[w];
        preview_load(who, bonus_outfit_get(who), kWeaponItem[w]);
    } else if (!s_previewReady) {
        preview_load(BONUS_CHAR_CHRIS, bonus_outfit_get(BONUS_CHAR_CHRIS), 0);
    }

    opt_draw_frame("OPTIONS - BONUS");
    opt_text(OPT_ROW_X, OPT_ROW_Y - 12, COL_DISABLED, "OUTFITS / WEAPONS");
    opt_rect(206, OPT_ROW_Y - 12, 1, 176, 0x60, 60);
    opt_draw_rows(*cursor, 184);
    if (s_bonusWeaponCount == 0) {
        opt_text(OPT_ROW_X, OPT_ROW_Y + (backRow + 1) * OPT_ROW_PITCH + 4, COL_DISABLED,
                 "NO WEAPON UNLOCKED YET");
    }
    if (s_previewChar >= 0) {
        const char* who = bonus_character_name(s_previewChar);
        const char* what = bonus_outfit_name(s_previewOutfit);
        if (s_previewWeaponItem != 0 && *cursor >= BONUS_CHAR_COUNT && *cursor < backRow) {
            what = bonus_weapon_name(s_bonusWeapons[*cursor - BONUS_CHAR_COUNT]);
        }
        opt_text_centre(PREVIEW_CENTER_X - (int)strlen(who) * 4, 30, COL_NORMAL, "%s", who);
        opt_text_centre(PREVIEW_CENTER_X - (int)strlen(what) * 4, 202, COL_DISABLED, "%s", what);
    }
    preview_draw();
}

// ---------------------------------------------------------------------------
// The state
// ---------------------------------------------------------------------------
void game_options_state(void)
{
    // The title has already faded to black and released its sheets.
    g_bGameActive = 0;
    g_main_state_flags &= ~MSF_INTENSITY_RAMP;
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
    g_fading_state = -1;
    g_fading_counter = 0;
    g_SpecialRoomLightState = (short)0xFFFF;
    g_menu_choice_id = 0;
    CenterScreenOrigin();

    bonus_load();
    bonus_refresh();
    if (!s_cfgInit) {
        s_cfgInit = 1;
        s_cfgMode = g_GameMode;
        s_cfgRegion = GetAssetVersion();
        s_cfgHd = HdTextures_ReplaceEnabled() ? 1 : 0;
    }
    display_build_resolutions();

    s_page = PAGE_ROOT;
    s_done = 0;
    s_confirmQuit = 0;
    s_capture = 0;
    s_previewReady = 0;
    s_previewChar = -1;
    s_previewOutfit = -1;
    s_previewWeaponItem = 0;
    s_previewDirty = 0;
    // Whatever was held to get here must be released before it counts.
    s_navPrev = ~0u;
    s_navRepeat = 0;
    s_swallowInput = 1;

    opt_sfx_load();

    // The menu's music: Bgm_31, looping, until the menu is left.
    bgm_play_group_channel(OPT_BGM_GROUP, 0);

    Task_sleep(1);

    while (!s_done) {
        const unsigned int nav = opt_nav_read();
        switch (s_page) {
        case PAGE_DISPLAY: page_display(nav); break;
        case PAGE_INPUT:   page_input(nav);   break;
        case PAGE_GAME:    page_game(nav);    break;
        case PAGE_BONUS:   page_bonus(nav);   break;
        default:           page_root(nav);    break;
        }
        Task_sleep(1);
    }

    // Leaving: everything the pages changed is written now.
    bgm_stop_group();
    opt_sfx_free();
    display_apply();
    ConfigFile_SaveGameSettings(s_cfgMode, s_cfgRegion);
    if (s_previewDirty) bonus_save();
    bonus_preview_outfit(-1);

    // The title rebuilds its menu on entry; its EXTRA row depends on the
    // Battle Game toggle just edited.
    CenterScreenOrigin();
    set_scene_render_param(0xc0);
    title_resume_on_menu();
    Task_chain((void*)title_state);
}

// ---------------------------------------------------------------------------
// The in-game menu
// ---------------------------------------------------------------------------
// Opened by check_menus_state (GameLoop.cpp) where the original opens its
// options screen (options_menu, 0x004761b0: the OPTIONS chord). The entry and
// exit around the pages follow that function - the game task is suspended, the
// screen goes to the flat colour and fades in, and on the way out the room's
// lights, the camera cut and the menu flags are put back before the game task
// resumes. What it does NOT do is touch the sound: the room's BGM keeps playing
// under it, as it does under the original's screen.
void ingame_options_menu(void)
{
    Task_suspend(0);
    s_wantTitle = 0;

    // As the original: the screen border flag is lifted while the menu is up.
    g_controllerConfig = g_controllerConfig & 0xef;
    if ((g_main_state_flags2 & MSF2_SCREEN_BORDER) != 0) {
        g_controllerConfig = g_controllerConfig | 0x10;
        g_main_state_flags2 = g_main_state_flags2 & ~MSF2_SCREEN_BORDER;
    }

    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
    g_fade_type_id = 2;
    g_fading_counter = 0xf800;
    fade_update();

    if (!s_cfgInit) {
        s_cfgInit = 1;
        s_cfgMode = g_GameMode;
        s_cfgRegion = GetAssetVersion();
        s_cfgHd = HdTextures_ReplaceEnabled() ? 1 : 0;
    }
    display_build_resolutions();

    s_page = PAGE_ROOT;
    s_cursor[PAGE_ROOT] = 0;
    s_done = 0;
    s_confirmQuit = 0;
    s_capture = 0;
    // The chord that opened the menu must be released before anything counts.
    s_navPrev = ~0u;
    s_navRepeat = 0;
    s_swallowInput = 1;

    opt_sfx_load();
    Task_sleep(1);

    // Wait for the fade-in (the original's state 0).
    while ((short)g_fading_state >= 0 && g_resetGameFlag == 0) {
        Task_sleep(1);
    }

    while (!s_done && g_resetGameFlag == 0) {
        const unsigned int nav = opt_nav_read();
        switch (s_page) {
        case PAGE_DISPLAY: page_display(nav);       break;
        case PAGE_INPUT:   page_input(nav);         break;
        default:           page_root_ingame(nav);   break;
        }
        Task_sleep(1);
    }

    // Leaving: the changes are applied, the screen fades out (the original's
    // options_menu_exit), then the game is put back.
    opt_sfx_free();
    display_apply();
    StMask(0, 3);
    set_fading(2, 0x800);
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
    while ((g_main_state_flags & MSF_FADE_ACTIVE) != 0) {
        Task_sleep(1);
    }

    g_main_state_flags = g_main_state_flags & ~MSF_MENU_BYTE;
    if ((g_controllerConfig & 0x10) != 0) {
        g_main_state_flags2 = g_main_state_flags2 | MSF2_SCREEN_BORDER;
        g_controllerConfig = g_controllerConfig & 0xef;
    }

    // The room's own lighting (the menu uses none, but the original puts it back).
    unsigned char lightIdx = 3;
    do {
        lightIdx--;
        FUN_0040ac80((int)lightIdx, &g_RdtPointer->lights[lightIdx]);
    } while (lightIdx != 0);
    setBackColor((unsigned short)g_RdtPointer->ambient_light_r,
                 (unsigned short)g_RdtPointer->ambient_light_g,
                 (unsigned short)g_RdtPointer->ambient_light_b);
    cut_set();
    g_loadSaveStateFlag = 0;

    // BACK TO TITLE is the soft reset (F9 twice): game_loop sees the flag on its
    // next frame and chains the title.
    if (s_wantTitle) g_resetGameFlag = 1;

    Task_Resume(0);
    Task_exit();    // does not return
}

// Bonus.cpp - persistent bonuses (outfits, bonus weapons). See Bonus.h.
#include "Bonus.h"

#include "../Globals.h"
#include "../DebugPrint.h"
#include "../platform/platform.h"
#include "../system/AssetPath.h"
#include "saturn/SaturnBattle.h" // saturn_mode_is_borrowed

#include <cstdio>
#include <cstring>

extern void Flg_on(int baseAddr, unsigned int bitIndex);   // 0x00473ef0, CmdFunctions.cpp

// ---------------------------------------------------------------------------
// bonus.dat, 32 bytes, little-endian:
//   +0   'R','E','1','B'
//   +4   u8  version (1)
//   +5   u8  outfit unlock bits   (1 << BONUS_OUTFIT_*)
//   +6   u8  weapon unlock bits   (1 << BONUS_WEAPON_*)
//   +7   u8  weapon enable bits
//   +8   u8  selected outfit, one per BONUS_CHAR_*
//   +11  zero to the end
// A file that is missing, short or has another magic reads as "nothing
// unlocked" and is rewritten on the next unlock or change.
// ---------------------------------------------------------------------------
#define BONUS_FILE_NAME   "bonus.dat"
#define BONUS_FILE_SIZE   32
#define BONUS_VERSION     1

static int           s_loaded = 0;
static unsigned char s_outfitUnlocked = 0;
static unsigned char s_weaponUnlocked = 0;
static unsigned char s_weaponEnabled = 0;
static unsigned char s_outfitSelected[BONUS_CHAR_COUNT] = { 0, 0, 0 };
static int           s_previewOutfit = -1;

static void bonus_path(char* out, size_t size)
{
    snprintf(out, size, "%s%s", GetSaveRoot(), BONUS_FILE_NAME);
}

void bonus_load(void)
{
    if (s_loaded) return;
    s_loaded = 1;

    char path[300];
    bonus_path(path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (f == NULL) return;
    unsigned char buf[BONUS_FILE_SIZE];
    const size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n < 11 || memcmp(buf, "RE1B", 4) != 0 || buf[4] != BONUS_VERSION) {
        dbg_printf("[bonus] %s is not a version-%d bonus file - ignored\n", path, BONUS_VERSION);
        return;
    }
    s_outfitUnlocked = buf[5];
    s_weaponUnlocked = buf[6];
    s_weaponEnabled  = buf[7];
    for (int c = 0; c < BONUS_CHAR_COUNT; c++) {
        s_outfitSelected[c] = (buf[8 + c] < BONUS_OUTFIT_COUNT) ? buf[8 + c] : BONUS_OUTFIT_DEFAULT;
    }
}

void bonus_save(void)
{
    bonus_load();

    char path[300];
    bonus_path(path, sizeof(path));

    // The save folder may not exist yet (no game saved so far).
    char dir[300];
    snprintf(dir, sizeof(dir), "%s", GetSaveRoot());
    size_t len = strlen(dir);
    while (len > 1 && (dir[len - 1] == '/' || dir[len - 1] == '\\')) dir[--len] = '\0';
    plat_mkdir(dir);

    unsigned char buf[BONUS_FILE_SIZE];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, "RE1B", 4);
    buf[4] = BONUS_VERSION;
    buf[5] = s_outfitUnlocked;
    buf[6] = s_weaponUnlocked;
    buf[7] = s_weaponEnabled;
    for (int c = 0; c < BONUS_CHAR_COUNT; c++) buf[8 + c] = s_outfitSelected[c];
    if (!plat_file_write(path, buf, sizeof(buf))) {
        dbg_printf("[bonus] could not write %s\n", path);
    }
}

// ---------------------------------------------------------------------------
// Outfits
// ---------------------------------------------------------------------------
static const char* const kOutfitNames[BONUS_OUTFIT_COUNT] = {
    "DEFAULT", "ALTERNATE", "PC", "DIR. CUT", "SATURN",
};
static const char* const kCharacterNames[BONUS_CHAR_COUNT] = {
    "CHRIS", "JILL", "REBECCA",
};

const char* bonus_outfit_name(int outfit)
{
    if (outfit < 0 || outfit >= BONUS_OUTFIT_COUNT) return "";
    return kOutfitNames[outfit];
}

const char* bonus_character_name(int character)
{
    if (character < 0 || character >= BONUS_CHAR_COUNT) return "";
    return kCharacterNames[character];
}

// One candidate location of an outfit's model: the row, the tree and the file
// to probe there. Two per outfit at most: the migrated base-tree copy first,
// then the release's own overlay for a tree migrated before the copy existed.
struct OutfitSource {
    int         row;        // g_emdPathTable row, -1 ends the list
    const char* overlay;    // "" = base tree
    const char* probe;      // path relative to that tree
};
struct OutfitDef {
    OutfitSource src[2];
};

#define NO_SOURCE   { -1, "", "" }
#define NO_OUTFIT   { { NO_SOURCE, NO_SOURCE } }

static const OutfitDef kOutfits[BONUS_CHAR_COUNT][BONUS_OUTFIT_COUNT] = {
    {   // Chris
        NO_OUTFIT,
        { { { EMD_ROW_CHRIS_ALT1,   "",       "Enemy/EM1030.EMD" }, NO_SOURCE } },
        { { { EMD_ROW_CHRIS_PC,     "",       "Enemy/EM1032.EMD" }, NO_SOURCE } },
        { { { EMD_ROW_DC_CHRIS,     "",       "Enemy/EM1052.EMD" },
            { EMD_ROW_CHRIS_PC,     "DC",     "Enemy/EM1032.EMD" } } },
        { { { EMD_ROW_SATURN_CHRIS, "",       "Enemy/EM1061.EMD" },
            { EMD_ROW_SATURN_CHRIS, "SATURN", "Enemy/EM1061.EMD" } } },
    },
    {   // Jill
        NO_OUTFIT,
        { { { EMD_ROW_JILL_ALT1,    "",       "Enemy/EM1031.EMD" }, NO_SOURCE } },
        { { { EMD_ROW_JILL_PC,      "",       "Enemy/EM1033.EMD" }, NO_SOURCE } },
        { { { EMD_ROW_DC_JILL,      "",       "Enemy/EM1053.EMD" },
            { EMD_ROW_JILL_PC,      "DC",     "Enemy/EM1033.EMD" } } },
        { { { EMD_ROW_SATURN_JILL,  "",       "Enemy/EM1161.EMD" },
            { EMD_ROW_SATURN_JILL,  "SATURN", "Enemy/EM1161.EMD" } } },
    },
    {   // Rebecca: only the Director's Cut drew her a second outfit
        NO_OUTFIT,
        NO_OUTFIT,
        NO_OUTFIT,
        { { { EMD_ROW_DC_REBECCA,         "",   "Enemy/EM1055.EMD" },
            { EMD_ROW_DC_REBECCA_OVERLAY, "DC", "Enemy/EM1035.EMD" } } },
        NO_OUTFIT,
    },
};

// Which source of each outfit is on disk: -1 = not probed, 0/1 = that source,
// 2 = neither. Probing is a couple of fopen calls, cached because the options
// page asks every frame.
static signed char s_outfitSource[BONUS_CHAR_COUNT][BONUS_OUTFIT_COUNT];
static int s_probed = 0;

void bonus_refresh(void)
{
    s_probed = 0;
}

static int outfit_source(int character, int outfit)
{
    if (!s_probed) {
        memset(s_outfitSource, -1, sizeof(s_outfitSource));
        s_probed = 1;
    }
    signed char* slot = &s_outfitSource[character][outfit];
    if (*slot < 0) {
        *slot = 2;
        for (int i = 0; i < 2; i++) {
            const OutfitSource& s = kOutfits[character][outfit].src[i];
            if (s.row < 0) break;
            const int present = s.overlay[0] != '\0' ? AssetOverlayHasFile(s.overlay, s.probe)
                                                     : AssetBaseHasFile(-1, s.probe);
            if (present) {
                *slot = (signed char)i;
                break;
            }
        }
    }
    return *slot;
}

int bonus_outfit_exists(int character, int outfit)
{
    if (character < 0 || character >= BONUS_CHAR_COUNT) return 0;
    if (outfit < 0 || outfit >= BONUS_OUTFIT_COUNT) return 0;
    if (outfit == BONUS_OUTFIT_DEFAULT) return 1;
    return kOutfits[character][outfit].src[0].row >= 0;
}

int bonus_outfit_model(int character, int outfit, BonusModel* out)
{
    if (!bonus_outfit_exists(character, outfit) || outfit == BONUS_OUTFIT_DEFAULT) return 0;
    const int src = outfit_source(character, outfit);
    if (src > 1) return 0;
    if (out != NULL) {
        out->row = kOutfits[character][outfit].src[src].row;
        out->overlay = kOutfits[character][outfit].src[src].overlay;
    }
    return 1;
}

int bonus_outfit_available(int character, int outfit)
{
    bonus_load();
    if (!bonus_outfit_exists(character, outfit)) return 0;
    if (outfit == BONUS_OUTFIT_DEFAULT) return 1;
    if ((s_outfitUnlocked & (1 << outfit)) == 0) return 0;
    return bonus_outfit_model(character, outfit, NULL);
}

int bonus_outfit_get(int character)
{
    bonus_load();
    if (character < 0 || character >= BONUS_CHAR_COUNT) return BONUS_OUTFIT_DEFAULT;
    const int outfit = s_outfitSelected[character];
    // A choice whose model has since gone (an overlay folder removed, another
    // region selected) falls back to the default without being forgotten.
    return bonus_outfit_available(character, outfit) ? outfit : BONUS_OUTFIT_DEFAULT;
}

void bonus_outfit_set(int character, int outfit)
{
    bonus_load();
    if (character < 0 || character >= BONUS_CHAR_COUNT) return;
    if (!bonus_outfit_available(character, outfit)) return;
    s_outfitSelected[character] = (unsigned char)outfit;
}

void bonus_preview_outfit(int outfit)
{
    s_previewOutfit = outfit;
}

int bonus_preview_active(void)
{
    return s_previewOutfit >= 0;
}

int bonus_player_model(unsigned char playerModelId, BonusModel* out)
{
    int character;
    switch (playerModelId) {
    case CHAR_CHRIS:   character = BONUS_CHAR_CHRIS;   break;
    case CHAR_JILL:    character = BONUS_CHAR_JILL;    break;
    case CHAR_REBECCA: character = BONUS_CHAR_REBECCA; break;
    default:           return 0;
    }
    if (s_previewOutfit >= 0) {
        return bonus_outfit_model(character, s_previewOutfit, out);
    }
    // The attract demos replay recorded input against the stock models.
    if ((g_main_state_flags2 & MSF2_ATTRACT_DEMO) != 0) return 0;
    return bonus_outfit_model(character, bonus_outfit_get(character), out);
}

int bonus_npc_model(unsigned char modelIndex, BonusModel* out)
{
    int character;
    switch (modelIndex) {
    case 0x24: character = BONUS_CHAR_CHRIS;   break;   // em1020
    case 0x25: character = BONUS_CHAR_JILL;    break;   // em1021
    case 0x27:                                          // em1023
    case 0x30: character = BONUS_CHAR_REBECCA; break;   // em102c, her cutscene variant
    default:   return 0;
    }
    // The options page previews player models only.
    if (s_previewOutfit >= 0) return 0;
    if ((g_main_state_flags2 & MSF2_ATTRACT_DEMO) != 0) return 0;
    return bonus_outfit_model(character, bonus_outfit_get(character), out);
}

// ---------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------
static const char* const kWeaponNames[BONUS_WEAPON_COUNT] = {
    "ROCKET LAUNCHER", "COLT PYTHON", "INGRAM", "MINIMI",
};

const char* bonus_weapon_name(int weapon)
{
    if (weapon < 0 || weapon >= BONUS_WEAPON_COUNT) return "";
    return kWeaponNames[weapon];
}

int bonus_weapon_unlocked(int weapon)
{
    bonus_load();
    if (weapon < 0 || weapon >= BONUS_WEAPON_COUNT) return 0;
    return (s_weaponUnlocked & (1 << weapon)) != 0;
}

int bonus_weapon_enabled(int weapon)
{
    return bonus_weapon_unlocked(weapon) && (s_weaponEnabled & (1 << weapon)) != 0;
}

// The bonus weapons belong to the campaign. A Battle Game run (also when it
// borrows Saturn mode from OG/DC) has its own loadout and rules.
int bonus_weapon_active(int weapon)
{
    return !g_saturnBattleActive && bonus_weapon_enabled(weapon);
}

void bonus_weapon_set_enabled(int weapon, int on)
{
    if (!bonus_weapon_unlocked(weapon)) return;
    if (on) s_weaponEnabled |= (unsigned char)(1 << weapon);
    else    s_weaponEnabled &= (unsigned char)~(1 << weapon);
}

// Put one item in the first free item-box slot. The start inventory is left
// as the game built it: a bonus weapon waits in the box until the player takes
// it out. Nothing happens when the item is already carried or already boxed.
static void give_item(unsigned char itemId, unsigned char qty)
{
    // 6 slots for Chris, 8 for Jill - the same bound SetInitialItems uses
    // (Jill's last two run into the Rebecca block, as in the original).
    const int capacity = ((g_playerEntity.id & 3) == CHAR_JILL) ? 8 : 6;
    for (int i = 0; i < capacity; i++) {
        if (g_ItemsSlots[i].Id == itemId) return;
    }
    for (int i = 0; i < 48; i++) {
        if (g_itemboxSlots[i].Id == itemId) return;
    }
    for (int i = 0; i < 48; i++) {
        if (g_itemboxSlots[i].Id == 0) {
            g_itemboxSlots[i].Id = itemId;
            g_itemboxSlots[i].qty = qty;
            return;
        }
    }
}

void bonus_grant_start_weapons(void)
{
    bonus_load();
    const int character = g_playerEntity.id & 3;

    if (bonus_weapon_active(BONUS_WEAPON_ROCKET)) {
        // The flag is what makes it the infinite one (MainMenu / the fire path
        // both test it), exactly as the ending's own grant raises it.
        Flg_on((int)g_ScenarioFlags, SCENARIO_FLAG_INF_R_LAUNCHER);
        give_item(ITEM_ROCKET_LAUNCHER, 1);
    }
    if (bonus_weapon_active(BONUS_WEAPON_PYTHON)) {
        // Same shape as the DC ending's grant; dc_is_infinite_colt_python()
        // honours the flag outside DC mode while this bonus is switched on.
        Flg_on((int)g_ScenarioFlags, DC_SCENARIO_FLAG_INF_COLT_PYTHON);
        give_item(ITEM_COLT_PYTHON_MAG, 6);
    }
    // The two machine guns are each one character's weapon, as the ending
    // hands them out (EndingScreen.cpp).
    if (bonus_weapon_active(BONUS_WEAPON_INGRAM) && character == CHAR_JILL) {
        give_item(ITEM_INGRAM, 1);
    }
    if (bonus_weapon_active(BONUS_WEAPON_MINIMI) && character == CHAR_CHRIS) {
        give_item(ITEM_MINIMI, 1);
    }
}

// ---------------------------------------------------------------------------
// Unlock events
// ---------------------------------------------------------------------------
void bonus_on_ending(int gotSpecialKey, int gotRocketLauncher,
                     int gotTimeWeapon, int gotColtPython)
{
    bonus_load();
    const unsigned char outfitsBefore = s_outfitUnlocked;
    const unsigned char weaponsBefore = s_weaponUnlocked;

    if (gotSpecialKey) {
        s_outfitUnlocked |= 1 << BONUS_OUTFIT_ALT1;
        if (g_GameMode == GAME_MODE_OG) s_outfitUnlocked |= 1 << BONUS_OUTFIT_PC;
        if (g_bSaturnMode && !saturn_mode_is_borrowed()) s_outfitUnlocked |= 1 << BONUS_OUTFIT_SATURN;
    }
    // The DC's arrange game: ADVANCED and ADVANCED* both, any ending.
    if (g_bDcMode && g_DcDifficulty >= DC_DIFFICULTY_ADVANCED) {
        s_outfitUnlocked |= 1 << BONUS_OUTFIT_DC;
    }

    if (gotRocketLauncher) s_weaponUnlocked |= 1 << BONUS_WEAPON_ROCKET;
    if (gotColtPython)     s_weaponUnlocked |= 1 << BONUS_WEAPON_PYTHON;
    if (gotTimeWeapon && g_GameMode == GAME_MODE_OG) {
        s_weaponUnlocked |= ((g_playerEntity.id & 3) != CHAR_CHRIS) ? (1 << BONUS_WEAPON_INGRAM)
                                                                    : (1 << BONUS_WEAPON_MINIMI);
    }

    // A newly unlocked weapon starts switched on: the run just earned it.
    s_weaponEnabled |= (unsigned char)(s_weaponUnlocked & ~weaponsBefore);

    if (s_outfitUnlocked != outfitsBefore || s_weaponUnlocked != weaponsBefore) {
        dbg_printf("[bonus] unlocked: outfits %02X -> %02X, weapons %02X -> %02X\n",
                   outfitsBefore, s_outfitUnlocked, weaponsBefore, s_weaponUnlocked);
        bonus_save();
    }
}

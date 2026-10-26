// Bonus.h - the bonuses a player has earned, kept across every save and mode.
//
// Port-added (no original address). The original keeps its rewards inside the
// next-cycle save: the special key, the infinite rocket launcher flag, the
// sub-4-hour machine gun. That ties each reward to one save file and one
// release. This module records them once, in <save folder>/bonus.dat, so that
// an outfit unlocked in one mode can be worn in another and a bonus weapon can
// be switched on or off from the title screen's OPTIONS > BONUS page.
//
// What unlocks what (the events are raised by ending_state, EndingScreen.cpp):
//
//   outfit ALT1    getting the special key, in any mode   EM1030 / EM1031
//   outfit PC      getting the special key in OG mode     EM1032 / EM1033
//   outfit DC      clearing the DC's ADVANCED (arrange)   the DC's EM1032 /
//                                                         EM1033 / EM1035
//   outfit SATURN  getting the special key in Saturn mode EM1061 / EM1161
//
//   rocket launcher  the ending grants it, any mode
//   Colt Python      the DC's ADVANCED best ending grants it
//   Ingram / Minimi  the ending grants it in OG mode (Jill / Chris)
//
// The DC and Saturn models are not in the base tree as shipped. The asset
// migrator copies them there under names no release uses (EM1052/1053/1055 for
// the DC's, EM1061/EM1161 for the Saturn's) so they cannot collide with the
// PC's own EM1032/EM1033, and g_emdPathTable carries a row for each
// (EntityModelLoader.cpp). A tree migrated before that copy existed still
// works: the model is then read out of its own overlay folder.
#pragma once

// Playable characters, in the order the options page lists them.
#define BONUS_CHAR_CHRIS      0
#define BONUS_CHAR_JILL       1
#define BONUS_CHAR_REBECCA    2
#define BONUS_CHAR_COUNT      3

#define BONUS_OUTFIT_DEFAULT  0
#define BONUS_OUTFIT_ALT1     1
#define BONUS_OUTFIT_PC       2
#define BONUS_OUTFIT_DC       3
#define BONUS_OUTFIT_SATURN   4
#define BONUS_OUTFIT_COUNT    5

#define BONUS_WEAPON_ROCKET   0
#define BONUS_WEAPON_PYTHON   1
#define BONUS_WEAPON_INGRAM   2
#define BONUS_WEAPON_MINIMI   3
#define BONUS_WEAPON_COUNT    4

// g_emdPathTable rows (EntityModelLoader.cpp). 51/52 are the PC's own costume
// rows in each 53-entry character block; EMD_ROW_BONUS_FIRST.. is the block the
// port appends after the two character blocks for the migrated copies.
#define EMD_ROW_CHRIS_ALT1         51
#define EMD_ROW_CHRIS_PC           52
#define EMD_ROW_JILL_ALT1          (53 + 51)
#define EMD_ROW_JILL_PC            (53 + 52)
#define EMD_ROW_BONUS_FIRST        106
#define EMD_ROW_DC_CHRIS           106   // enemy/em1052.emd  (DC EM1032)
#define EMD_ROW_DC_JILL            107   // enemy/em1053.emd  (DC EM1033)
#define EMD_ROW_DC_REBECCA         108   // enemy/em1055.emd  (DC EM1035)
#define EMD_ROW_SATURN_CHRIS       109   // enemy/em1061.emd
#define EMD_ROW_SATURN_JILL        110   // enemy/em1161.emd
#define EMD_ROW_DC_REBECCA_OVERLAY 111   // enemy/em1035.emd, the DC overlay's own name
#define EMD_ROW_COUNT              112

// Where an outfit's model is: a g_emdPathTable row, and the tree it must be
// read from. `overlay` is "" for the base tree and never NULL - the load has to
// name its tree, because in DC mode "enemy/em1032.emd" would otherwise resolve
// to the DC's model when the PC's was asked for.
struct BonusModel {
    int         row;
    const char* overlay;
};

// Reads bonus.dat. Idempotent; every other call below does it on first use.
void bonus_load(void);
void bonus_save(void);

// Forget which model files were found (the answer is cached). Call after the
// asset region changes.
void bonus_refresh(void);

// --- outfits ---------------------------------------------------------------
const char* bonus_outfit_name(int outfit);
const char* bonus_character_name(int character);
// 1 when the outfit is unlocked, exists for this character and its model file
// is present. BONUS_OUTFIT_DEFAULT is always available.
int  bonus_outfit_available(int character, int outfit);
// 1 when this character has such an outfit at all (unlocked or not).
int  bonus_outfit_exists(int character, int outfit);
int  bonus_outfit_get(int character);
void bonus_outfit_set(int character, int outfit);
// The model of one outfit, for the options page's preview. 0 when the outfit
// is the default (no override) or its file is missing.
int  bonus_outfit_model(int character, int outfit, BonusModel* out);
// What LoadEntityEMD asks: the override for player model id 0 (Chris), 1 (Jill)
// or 3 (Rebecca). 0 = no override, the game's own costume logic applies.
int  bonus_player_model(unsigned char playerModelId, BonusModel* out);
// The same choice for the cutscene NPC that is one of those characters, by
// model-table index (entity id + 4): 0x24 Chris, 0x25 Jill, 0x27 and 0x30
// Rebecca - the four the DC's ADVANCED remap dresses (dc/EntityModels.cpp).
// `out` is the PLAYER's outfit model; LoadEntityEMD puts its mesh and texture
// on the NPC's own skeleton and animations. 0 = no override.
int  bonus_npc_model(unsigned char modelIndex, BonusModel* out);

// A one-shot override for the options page: while set, bonus_player_model()
// answers with this outfit for every character instead of the saved choice,
// BONUS_OUTFIT_DEFAULT included. -1 clears it.
void bonus_preview_outfit(int outfit);
// 1 while that override is set. LoadEntityEMD then loads exactly the model
// asked for: with BONUS_OUTFIT_DEFAULT that is the character's stock model,
// whatever costume flags an earlier game left behind.
int  bonus_preview_active(void);

// --- weapons ---------------------------------------------------------------
const char* bonus_weapon_name(int weapon);
int  bonus_weapon_unlocked(int weapon);
int  bonus_weapon_enabled(int weapon);      // unlocked AND switched on
// enabled AND not inside a Battle Game run: what the game itself should obey.
int  bonus_weapon_active(int weapon);
void bonus_weapon_set_enabled(int weapon, int on);
// InitializeGame, new game only: put every enabled weapon the character can
// use into the item box. The start inventory is not touched.
void bonus_grant_start_weapons(void);

// --- unlock events ---------------------------------------------------------
// ending_state reports what the completed run earned. Each argument is what
// the original itself decided; the mode rules above are applied here.
void bonus_on_ending(int gotSpecialKey, int gotRocketLauncher,
                     int gotTimeWeapon, int gotColtPython);

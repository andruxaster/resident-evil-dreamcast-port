// SaturnBattle.cpp - the Sega Saturn's Battle Game, phases C.1-C.5.
//
// See SaturnBattle.h for what this file is and what is still missing, and
// docs/SATURN_VERSION_ANALYSIS.md 4d-4f for the original. Every Saturn address below
// is quoted with the module it was read from; the eleven .PRG overlays are
// separate compilations, so an address is only meaningful in its own module.
#include "SaturnBattle.h"

#include "../../Globals.h"
#include "../Types.h"
#include "../BioCard.h"
#include "../PrintText.h"
#include "../entities/EntityCommon.h"
#include "../../system/AssetPath.h"
#include "../../DebugPrint.h"
#include "../FileLoader.h"
#include "../SFXIds.h"
#include "../SpriteRenderer.h"
#include "../../marni/MarniSystem.h"
#include "../../platform/platform.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

unsigned char g_saturnBattleActive = 0;

// The runtime mirror of g_SaturnBattleUnlocked. The card byte is the save; this
// is what the title screen and the run read, because a NEW GAME resets the card
// from data\bio_card.dat (GameStart.cpp's 1052-byte memcpy) and the Saturn has
// no such reset. saturn_battle_preserve() bridges the two.
static unsigned char s_battleUnlocked = 0;

// ---------------------------------------------------------------------------
// The unlock (TITLE.PRG g_BattleGameUnlocked, byte 0x0604479A)
// ---------------------------------------------------------------------------

int saturn_battle_is_unlocked(void)
{
    // [Game] BattleGame=1 is a read-time override: it makes the item available
    // without an ending and is deliberately NOT written into the card, so a
    // save made with it on still records only what the game itself earned.
    // It works in every mode; OG and DC have no ending-based unlock (the
    // unlock byte is Saturn-only), so the key is their only way in.
    if (g_bSaturnBattleGame) return 1;
    return s_battleUnlocked != 0;
}

// The Battle Game's arenas are Saturn-only files, so a run needs the SATURN/
// overlay to be imported, whatever [Game] Mode says. The 15 arena RDTs are the
// check: they are the one thing no other tree supplies (the campaign rooms fall
// back to the base tree), and a run without them stops on arena 1. Probed once -
// the answer cannot change while the game is running.
int saturn_battle_assets_present(void)
{
    static int s_checked = 0;
    static int s_present = 0;
    if (s_checked) return s_present;
    s_checked = 1;

    int missing = 0;
    for (int arena = SATURN_BATTLE_FIRST_ROOM; arena <= SATURN_BATTLE_LAST_ROOM; arena++) {
        char rel[40];
        sprintf(rel, "Stage8/ROOM80%X0.RDT", arena);
        if (!AssetOverlayHasFile("SATURN", rel)) missing++;
    }
    s_present = (missing == 0);
    dbg_printf("[battle] SATURN arena assets %s (%d of %d arena RDTs missing)\n",
               s_present ? "present" : "NOT imported", missing, SATURN_BATTLE_ARENA_COUNT);
    return s_present;
}

int saturn_battle_offered(void)
{
    return saturn_battle_is_unlocked() && saturn_battle_assets_present();
}

// A run from OG or DC borrows Saturn mode: the Battle Game's code, its asset
// overlay and its rules (Western difficulty, the Saturn enemy tables) are all
// keyed on g_GameMode, which is the one switch (Globals.h). The mode config.ini
// chose is put back when the player is back on the title screen.
static int  s_savedGameMode = -1;
static char s_savedOverlay[16] = "";

// A run started from a Director's Cut ADVANCED save keeps that save's outfit
// logic. The arrange-exclusive outfit is not a costume flag but a DC mode
// (MSF2_DC_ADVANCED remaps the player model to EM1032/1033 in the DC overlay),
// and Saturn mode drops both. Decided in saturn_battle_begin_run(), after the
// save's mode byte has arrived with the card.
static int s_battleDcOutfit = 0;

int saturn_battle_dc_outfit(void)
{
    return g_bSaturnMode && g_saturnBattleActive && s_battleDcOutfit;
}

int saturn_mode_is_borrowed(void)
{
    return s_savedGameMode >= 0;
}

static void battle_enter_saturn_mode(void)
{
    if (g_GameMode == GAME_MODE_SATURN || s_savedGameMode >= 0) return;
    s_savedGameMode = g_GameMode;
    strncpy(s_savedOverlay, GetAssetModeName(), sizeof(s_savedOverlay) - 1);
    s_savedOverlay[sizeof(s_savedOverlay) - 1] = '\0';
    g_GameMode = GAME_MODE_SATURN;
    SetAssetMode("SATURN");
}

void saturn_battle_restore_mode(void)
{
    s_battleDcOutfit = 0;
    if (s_savedGameMode < 0) return;
    g_GameMode = s_savedGameMode;
    s_savedGameMode = -1;
    SetAssetMode(s_savedOverlay);
}

void saturn_battle_unlock(void)
{
    if (!g_bSaturnMode) return;
    s_battleUnlocked = 1;
    g_SaturnBattleUnlocked = 1;
}

void saturn_battle_sync_from_card(void)
{
    if (!g_bSaturnMode) return;
    s_battleUnlocked = g_SaturnBattleUnlocked;
}

void saturn_battle_preserve(void)
{
    if (!g_bSaturnMode) return;
    if (s_battleUnlocked) g_SaturnBattleUnlocked = 1;
}

// The Saturn's cheat is a pad-2 combo - X + Y + Z + Start during the title's
// "press start" wait (TITLE.PRG TitleMain 0x0602DD70, while g_TitleState == 4
// and the flag is still 0). The PC has no second controller and no reason to
// make a player hold four keys to reach the mode, so it is NOT ported. The
// replacement is [Game] BattleGame in config.ini, read by
// saturn_battle_is_unlocked() above: same effect, and a player who never clears
// a game can still see it.

// ---------------------------------------------------------------------------
// The arena -> campaign room table (GAME1 0x06092B5C, g_BattleArenaRoomTable)
// ---------------------------------------------------------------------------
// 15 big-endian u16 of (0-based stage << 8) | room, indexed by the arena number,
// entry 0 unused. Read back out of the module with tools/saturn_dump_table.py:
//
//   python tools/saturn_dump_table.py GAME1.PRG 0x06092B5E --count 30 --u16
//
// It matches the table in SATURN_VERSION_ANALYSIS.md 4.0 entry for entry, and it is
// the table the camera matching independently produced: 8010->100, 8020->103,
// 8030->302, 8040->117, 8050->210, 8060->100, 8070->404, 8080->40C, 8090->309,
// 80A0->30C, 80B0->30E, 80C0->509, 80D0->50F, 80E0->510, 80F0->513.
static const unsigned short kArenaCampaignRoom[1 + SATURN_BATTLE_ARENA_COUNT] = {
    0x0000,                                                     // unused
    0x0000, 0x0003, 0x0202, 0x0017, 0x0110,                     // arenas 1-5
    0x0000, 0x0304, 0x030C, 0x0209, 0x020C, 0x020E,             // arenas 6-11
    0x0409, 0x040F, 0x0410, 0x0413                              // arenas 12-15
};

// The arena spawn points (GAME5 0x060958A8, the stage-8 block of the 6-byte-per-
// room table at 0x060953E0). Each record is three big-endian u16 -
// directionAngle, x, z - in the order the transition routine writes them
// (+0 -> player +0x74, +2 -> +0x34/+0x6C, +4 -> +0x3C/+0x70).
//
// ADDRESS NOTE. SATURN_VERSION_ANALYSIS.md 4e gives the index as
// `0x060953E0 + 0xAE * stage + 6 * room`, which for the stage byte 7 resolves to
// 0x06095886 - a byte that is NOT on the table's 6-byte record grid (the base
// 0x060953E0 leaves remainder 2 mod 6, 0x06095886 leaves 4). The block was
// therefore located by content instead: exactly 15 consecutive records whose
// directions are 0 / 0x400 / 0xC00 (0x3E0 once) and whose coordinates are
// multiples of 4, sitting between the campaign records and the five-byte virtual
// door table at 0x06095950. That is 0x060958A8. Two records corroborate it:
// arena 15 (campaign room 513, the Laboratory) is (0x400, 4700, 25500), and the
// second Tyrant's capsule in that room is at (0x1C00, 0x6600) = (7168, 26112).
static const unsigned short kArenaSpawn[SATURN_BATTLE_ARENA_COUNT][3] = {
    {  992,  3800,  7400 },   // 1  8010  Mansion west storeroom (itembox)
    {    0,  3000,  3000 },   // 2  8020  F-shaped corridor
    { 1024, 23200, 29200 },   // 3  8030  Falls
    { 1024,  2900, 10600 },   // 4  8040  Large Gallery
    {    0,  6100,  3600 },   // 5  8050  Attic (Yawn1)
    { 3072,  3800,  2700 },   // 6  8060  Mansion west storeroom (itembox)
    {    0, 12000, 13000 },   // 7  8070  Guardhouse Bar
    {    0,  2500, 11000 },   // 8  8080  Plant 42 Room
    {    0,  4100, 26800 },   // 9  8090  Underground Generator Room
    { 1024, 10800, 13000 },   // 10 80A0  Black tiger room
    { 3072,  4600,  2500 },   // 11 80B0  Underground Break room (itembox)
    { 1024,  3600,  2700 },   // 12 80C0  Lab Researcher's private room
    { 1024, 12600, 20560 },   // 13 80D0  Power room passage A
    { 1024, 22000, 16500 },   // 14 80E0  Power room passage B
    { 1024,  4700, 25500 }    // 15 80F0  Main Lab (Golden Tyrant)
};

// The arena being fought, 1..15, or 0 when no run is in progress. See the header
// for why this is derived from g_roomId rather than stored.
int saturn_battle_arena_index(void)
{
    if (!g_bSaturnMode || !g_saturnBattleActive) return 0;
    if (g_roomId < 1 || g_roomId > SATURN_BATTLE_ARENA_COUNT) return 0;
    return (int)g_roomId;
}

int saturn_battle_asset_room(unsigned char* stage, unsigned char* room)
{
    const int arena = saturn_battle_arena_index();
    if (arena == 0) return 0;
    const unsigned short pair = kArenaCampaignRoom[arena];
    if (stage != NULL) *stage = (unsigned char)(pair >> 8);
    if (room != NULL) *room = (unsigned char)(pair & 0xFF);
    return 1;
}

// ---------------------------------------------------------------------------
// Music. GAME5's BGM loader (0x0604C9F0) takes STAGE8's group from this table,
// indexed by the room byte, instead of the per-stage rows the campaign uses:
//   0x0F  the rest rooms (arenas 1, 6, 11) - the campaign's save-room music
//   0x40  /SND/BATTLE.CDP, Saturn-only (0x0604CA7E) - nine of the fights
//   0x06, 0x27, 0x23  campaign music (arenas 5, 8, 10)
//   0x34  arena 15, the Tyrant: STAGE8 loads BGM34B.CDP for it (0x0604CA9A),
//         Battle mode replaces channel 2 with BGM34B song 2; other Saturn-mode
//         callers still fall through to the PC's { Se_55, Bgm_20, Bgm_24 } row.
// Rooms 16..31 are not arenas; 0xFF is "no BGM" the way the PC tables use it.
// ---------------------------------------------------------------------------
static const unsigned char kArenaBgmGroup[32] = {
    0xFF, 0x0F, 0x40, 0x40, 0x40, 0x06, 0x0F, 0x40,   // 0x0609ACCC
    0x27, 0x40, 0x23, 0x0F, 0x40, 0x40, 0x40, 0x34,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

// The room BGM states that follow it (0x0609ACDC): channel 0 everywhere, and
// channel 2 - Bgm_24, the Tyrant battle - in arena 15. Writable because
// init_room silences a room in attract mode through g_RoomBgmStatePtr.
static const unsigned char kArenaBgmStateInit[32] = {
    0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,   // 0x0609ACDC
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x20,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
static unsigned char s_arenaBgmState[32];

int saturn_battle_bgm_group(unsigned char room, unsigned char* group)
{
    if (!g_bSaturnMode || !g_saturnBattleActive) return 0;
    *group = room < 32 ? kArenaBgmGroup[room] : 0xFF;
    return 1;
}

unsigned char* saturn_battle_bgm_states(void)
{
    if (!g_bSaturnMode || !g_saturnBattleActive) return NULL;
    memcpy(s_arenaBgmState, kArenaBgmStateInit, sizeof(s_arenaBgmState));
    return s_arenaBgmState;
}

// Group 0x40 is BATTLE.CDP's two songs - song k is BGM channel k, as in every
// pack (BGM34B's song 2 is the PC's Bgm_24: 43.5 s, the loop from 33.2 s). Song
// 0 loops (its 0x82 loop point); song 1 has none, and the Saturn plays it once
// at the end of the run. The asset migrator writes them as SATURN/Sound/
// Bgm_58 (battle game bgm) and Bgm_59 (battle game finished) (tools/asset_migrator core/SaturnMusic.h).
const char* saturn_battle_bgm_name(unsigned char group, int ch, signed char* loop)
{
    if (!g_bSaturnMode || ch < 0 || ch >= 3) return NULL;
    // RANKING.PRG's /SND/NAME.CDP, song 0 (looping), for the results screens.
    // Not gated on the run: saturn_ranking_state plays it after
    // saturn_battle_close_run() has cleared battle mode, as RANKING runs after
    // GAME5 is gone. The group id is port-only, so nothing else asks for it.
    if (group == SATURN_BGM_GROUP_RANKING) {
        if (ch != 0) return NULL;
        if (loop != NULL) *loop = 1;
        return "Bgm_5a";
    }
    if (!g_saturnBattleActive) return NULL;
    if (group == SATURN_BGM_GROUP_BATTLE) {
        static const char* const kNames[3] = { "Bgm_58", "Bgm_59", NULL };
        static const signed char kLoops[3] = { 1, 0, 0 };
        if (loop != NULL) *loop = kLoops[ch];
        return kNames[ch];
    }
    // Arena 15's group 0x34 is the campaign's own: channel 2 is the PC's Bgm_24,
    // the Tyrant battle, as it is in the lab. (An override to BGM34B.CDP's song
    // 2, once exported under a Saturn-only name, was tried; the Saturn's Tyrant theme is the stock
    // one, and the override only made the arena silent when that file had not
    // been migrated.)
    return NULL;
}

// The Battle Game's loadout (GAME5 battle_run_init 0x06088698). It goes into
// the ITEM BOX, not the inventory: battle_run_init clears the 48 two-byte slots
// at 0x002F8724 (the box; the inventory is the block after it, 0x002F8784) and
// copies a zero-terminated (id, qty) list into them. The run then starts with an
// EMPTY inventory - GAME5's SetInitialItems (0x06045188) skips its default
// knife/spray list when battle bit 31 is set and zeroes every slot instead - so
// the player arms themself from arena 1's item box.
//
// The lists are GAME5 0x06088660 (Chris, 23 bytes) and 0x06088677 (Jill, 33
// bytes), copied to the stack with the 0.BIN memcpy at 0x0600C544. The weapons
// carry their LOADED rounds and the ammo follows as separate items, which is why
// the recorded starting totals are 15+75 = 90 handgun, 7+49 = 56 shotgun and
// 6+48 = 54 magnum for Chris (0x002FFFA8-0x002FFFAD). The sprays are separate
// one-spray entries, four for Chris and five for Jill, as 0x002FFFAE counts them.
// tools/verify_saturn_battle.py reads both lists back out of GAME5.PRG.
struct BattleItem { unsigned char id; unsigned char qty; };

static const BattleItem kChrisItemBox[] = {           // GAME5 0x06088660
    { ITEM_KNIFE,             0 },
    { ITEM_BERETTA,          15 },
    { ITEM_SHOTGUN,           7 },
    { ITEM_COLT_PYTHON_MAG,   6 },
    { ITEM_CLIP,             75 },
    { ITEM_SHELLS,           49 },
    { ITEM_MAGNUM_ROUNDS,    48 },
    { ITEM_FIRST_AID_SPRAY,   1 },
    { ITEM_FIRST_AID_SPRAY,   1 },
    { ITEM_FIRST_AID_SPRAY,   1 },
    { ITEM_FIRST_AID_SPRAY,   1 },
};

static const BattleItem kJillItemBox[] = {            // GAME5 0x06088677
    { ITEM_KNIFE,             0 },
    { ITEM_BERETTA,          15 },
    { ITEM_SHOTGUN,           7 },
    { ITEM_COLT_PYTHON_MAG,   6 },
    { ITEM_BAZOOKA_EXPLOSIVE, 0 },     // the launcher itself, loaded with nothing
    { ITEM_CLIP,             75 },
    { ITEM_SHELLS,           35 },
    { ITEM_MAGNUM_ROUNDS,    36 },
    { ITEM_EXPLOSIVE_ROUNDS,  6 },
    { ITEM_ACID_ROUNDS,       6 },
    { ITEM_FLAME_ROUNDS,      6 },
    { ITEM_FIRST_AID_SPRAY,   1 },
    { ITEM_FIRST_AID_SPRAY,   1 },
    { ITEM_FIRST_AID_SPRAY,   1 },
    { ITEM_FIRST_AID_SPRAY,   1 },
    { ITEM_FIRST_AID_SPRAY,   1 },
};

// The arena warp's virtual door records (GAME5 0x06095950, 16 x 5 bytes,
// indexed by the arena being LEFT - the transition routine reads block +6
// before it increments it, 0x0602DDF4-0x0602DE48). SATURN_VERSION_ANALYSIS.md 4e.
// The battle branch of room_transition_load (0x0602DBBC) writes them into the
// same globals a door record's fields go to, so the fight rooms change room
// through the ordinary door path, door animation and door sound included:
//
//   [0] -> 0x060AD450  record +0x08  direction   (door script var 0)
//   [1] -> 0x060AD464  record +0x09  door sound  (load_room_sfx index)
//   [2] -> 0x060AD460  record +0x0A  door type   (the .dor file)
//   [3] -> 0x060AD45C  record +0x0B  & 0x3F, entry camera (door script var 3)
//   [4] -> 0x060A72C3  record +0x0B  & 0xC0, the flags: 0x40 = no door sound
//
// [4] is the FLAGS byte, not the destination: 0x060A72C3 is 0x060A67D0 + 0xAF3,
// the byte the normal path fills from `record[0x0B] & 0xC0` (0x0602DF9C). The
// destination is not in the record at all - the room is simply incremented.
// The all-zero rows are the rest rooms (a real door leaves them), the Tyrant
// room (the run ends there) and the unused entry 0.
//
// The sound indices order the same as the PC's: GAME5's own door-sound names
// (0x0604C530: DOORWOD, DOORMTL, DOORBRK, DOORREB, ..., DOORELV, ..., DOORAIR)
// line up with g_roomSoundEffectsTable entry for entry, and each row pairs a
// sound with a matching door (row 2: sound 6, the small elevator, with type
// 0x15, ele04).
static const unsigned char kArenaVirtualDoor[1 + SATURN_BATTLE_ARENA_COUNT][5] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   // unused
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   // 1  8010 rest room - real door
    { 0x01, 0x06, 0x15, 0x00, 0x40 },   // 2  8020
    { 0x02, 0x00, 0x1E, 0x05, 0x00 },   // 3  8030
    { 0x02, 0x00, 0x07, 0x01, 0x00 },   // 4  8040
    { 0x00, 0x00, 0x00, 0x04, 0x00 },   // 5  8050
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   // 6  8060 rest room - real door
    { 0x03, 0x00, 0x20, 0x05, 0x00 },   // 7  8070
    { 0x00, 0x01, 0x08, 0x00, 0x00 },   // 8  8080
    { 0x0B, 0x01, 0x08, 0x00, 0x00 },   // 9  8090
    { 0x00, 0x01, 0x08, 0x00, 0x00 },   // 10 80A0
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   // 11 80B0 rest room - real door
    { 0x0B, 0x01, 0x0B, 0x03, 0x00 },   // 12 80C0
    { 0x00, 0x01, 0x0B, 0x03, 0x00 },   // 13 80D0
    { 0x0A, 0x0C, 0x0D, 0x00, 0x40 },   // 14 80E0
    { 0x00, 0x00, 0x00, 0x00, 0x00 },   // 15 80F0 Golden Tyrant - the run ends
};

// ---------------------------------------------------------------------------
// The arena sequence (GAME5 0x0608883C, the per-frame battle state machine)
// ---------------------------------------------------------------------------
// The per-room "has enemies" flags (GAME5 0x060959A0): ONE BYTE per room,
// indexed by the room byte, and bit 0 set means a fight room. Seventeen entries,
// for rooms 0x00..0x10.
//
//   python -X utf8 -c "d=open('assets/SATURN_ISO_CONTENT/GAME5.PRG','rb').read();\
//     o=0x060959A0-0x0602B000; print(' '.join('%02x'%b for b in d[o:o+17]))"
//   -> 00 00 01 01 01 01 00 01 01 01 01 00 01 01 01 01 00
//
// Bit 0 is CLEAR at arenas 1, 6 and 11 - the three rest rooms, and the only
// arenas with a door record (8010 -> 8020, 8060 -> 8070, 80B0 -> 80C0), each
// entered from the boss room before it. Rooms 0 and 16 are not arenas. That is
// the shape of the whole run: rest, four fights, rest, four fights, rest, four
// fights, end. (Read as u16 pairs - which the analysis doc's "17 bytes" first
// invites - it is an irregular table with two hits that matches nothing.)
static const unsigned char kArenaHasEnemies[17] = {
    0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0
};

// The Battle Game's run block (GAME5 0x060A67A0). The field order is the
// original's, so the comments read against the disassembly.
struct BattleBlock {
    unsigned int flags;         // +0
    int          state;         // +4   0 fighting, 1 cleared, 2 next room (the door
                                //      transition owns it until the room changes), 3 end
    int          arena;         // +6   == the room byte
    int          enemiesLeft;   // +8
    int          timer;         // +10
    int          noAmmoFrames;  // +12
};

enum {
    BB_CLOCK      = 0x00000001u,  // +0 bit 0: the run clock counts (see below)
    BB_FIGHT_ROOM = 0x00000002u,  // +0 bit 1, from kArenaHasEnemies
    BB_ADVANCE    = 0x00000004u,  // +0 bit 2
    BB_KNIFE      = 0x00000008u,  // +0 bit 3
    BB_CAN_FIGHT  = 0x00000010u,  // +0 bit 4
    BB_HANDGUN    = 0x00000020u,  // +0 bit 5
    BB_SHOTGUN    = 0x00000040u,  // +0 bit 6
    BB_PYTHON     = 0x00000080u,  // +0 bit 7
    BB_LAUNCHER   = 0x00000100u,  // +0 bit 8
};

static BattleBlock s_battle = { 0, 0, 0, 0, 0, 0 };
static int s_battleSavedHp   = 0;
static int s_battleRunFrames = 0;
// plat_time_ms() of the last saturn_battle_tick_run(), and the milliseconds
// (x60) not yet turned into a whole 60th.
static DWORD s_battleClockLastMs = 0;
static DWORD s_battleClockRemainder = 0;
// Set once the results screens have been shown; game_loop then leaves the run.
static unsigned char s_battleFinished = 0;

// The result counters (GAME5 0x002FFFA8-0x002FFFAE): handgun, shotgun, magnum,
// explosive, acid, flame rounds and sprays. battle_run_init sets the starting
// totals (loaded + spare); each shot takes one off (0x060337B8) and each heal
// one spray (0x060444FA). Bytes on the Saturn; clamped at 0 here.
enum { BR_HANDGUN, BR_SHOTGUN, BR_MAGNUM, BR_EXPLOSIVE, BR_ACID, BR_FLAME, BR_SPRAYS, BR_COUNT };
static int s_battleLeft[BR_COUNT];

// GAME5 0x060888B2 calls the HUD text renderer at 0x0605B8D8 with x/y = 112,
// color 0, and the Saturn-font bytes at 0x060A10B0:
//
//   2f 30 1d 23 21  00  1f 28 21 1d 2e 1a  01
//    S  T  A  G  E     C  L  E  A  R  !  terminator
//
// Twelve glyphs and a terminator - FIVE, a space, then SIX - so the sentence is
// "STAGE CLEAR!", an exclamation mark and not a D. It was read as "STAGE
// CLEARED" here before, which does not fit: "CLEARED" is seven glyphs and there
// is no room for it. The repeat glyphs confirm the reading independently - E
// (0x21) is both the fifth glyph of STAGE and the third of CLEAR!, and A
// (0x1d) is the third of STAGE and the fourth of CLEAR!. Print the same words
// through the port font during the clear pause; center them for either font's
// glyph width.
// Printed by PrintText8x14, which takes the game font's own codes: its '!' is
// cell 62 (ASCII '>'), not 33, in both fontus.tim and the JPN FONT.TIM.
static constexpr char s_stageClearedHudText[] = "STAGE CLEAR>";

// The cleared pause: the Saturn's state 1 runs while `timer > 0x77`, i.e. 0x78 =
// 120 frames.
#define BATTLE_CLEAR_PAUSE_FRAMES 0x78
// The out-of-ammo grace: 0xB3 frames of "can still fight" with nothing to shoot.
#define BATTLE_NO_AMMO_GRACE      0xB3
// GAME5's run-frame counter saturates at 99:59 (0x57E04 frames at 60 fps).
#define BATTLE_MAX_RUN_FRAMES    0x57E04
// The run counter is in 60ths of a second: one per Saturn vertical blank.
#define BATTLE_RUN_FRAMES_PER_SEC 60
// A gap between two main_loop() calls longer than this is the game being held
// (focus loss, the F9 prompt, a debugger), not game time, and is not counted.
// A frame is 16 or 33 ms.
#define BATTLE_CLOCK_MAX_GAP_MS   200

// Re-arm the per-arena half of the block, the one and only writer of
// BB_FIGHT_ROOM. It runs every frame because the bit is a property of the ROOM,
// not of the transition that reached it, and two of the run's fifteen
// transitions are DOORS, not the warp in battle_advance_room(): arenas 1, 6
// and 11 are the rest rooms and the only arenas with a door record
// (8010->8020, 8060->8070, 80B0->80C0, dump_init_doors.py), and
// room_transition_load knows nothing about the Battle block.
//
// Arming the bit only at run start and in the warp is why a run softlocked in
// arena 2: arena 1 is a rest room, so it never clears and battle_advance_room()
// never runs; the player walks out through the door instead, the arena changes,
// and BB_FIGHT_ROOM stays clear. The state-0 clear test requires it, so
// clearing all three of arena 2's zombies produced nothing at all.
//
// GAME5 reads the per-room table the same way - the room's byte decides the
// bit (battle_run_init 0x060886F6 for room 1, and the room-load path for the
// rooms a door reaches).
static void battle_sync_arena(void)
{
    const int arena = saturn_battle_arena_index();
    if (arena < 1 || arena > SATURN_BATTLE_ARENA_COUNT) return;
    if (arena == s_battle.arena) return;

    s_battle.arena         = arena;
    s_battle.state         = 0;
    s_battle.timer         = 0;
    s_battle.noAmmoFrames  = 0;
    // BB_CLOCK is not this function's: the room load that brought the arena in
    // has already raised it (saturn_battle_clock_run).
    s_battle.flags        &= ~BB_ADVANCE;
    if (kArenaHasEnemies[arena]) s_battle.flags |= BB_FIGHT_ROOM;
    else                        s_battle.flags &= ~BB_FIGHT_ROOM;
}

// Ammo usable with a held weapon (state 0): the loaded rounds in the handgun /
// shotgun / Python / launcher, plus the matching spare item, and each spare only
// counts when its weapon is held.

extern void InitPlayerData(void);       // 0x00481880, GameStart.cpp

// The run's player and item set-up: GAME5's battle start-up, 0x0602CA12-0x0602CA42.
// Called from game_start after the card has been restored and before
// LoadHeldItemsImages(), the same window dc_apply_item_tables() uses.
//
// The run enters through the load screen (TITLE -> BU_LOAD -> GAME5), so the
// card is the chosen save's - its inventory, its item box, its HP. The Saturn
// replaces all three:
//   1. 0x0602F150, the new-game player init: SetInitialItems, which in battle
//      mode ZEROES the inventory instead of giving the knife and spray
//      (0x06045188, `bf/s` over the list copy when bit 31 is set), plus the
//      new-game position and the "player initialised" bit. The port's
//      InitPlayerData() is that same function; the inventory is then cleared
//      here, since the port's SetInitialItems has no battle branch. The Saturn
//      clears the character's slot count (6 / 8), leaving Rebecca's slot alone.
//   2. battle_run_init (0x06088698): the 48 item-box slots cleared, then the
//      character's list above copied into them; health-status byte
//      `+0xDC &= 0x9D` (poison and the Yawn's venom off).
//   3. HP set to the character's maximum, 140 / 96 (0x0602CA32-0x0602CA42,
//      `0x8C - 44 * jill` into +0x88 and its card copy).
// Nothing is equipped: SetupCharacterData() derives the weapon from
// g_EquippedItemId, and an empty inventory has nothing to point at.
static void battle_apply_loadout(void)
{
    const int jill = ((g_playerEntity.id & 3) == CHAR_JILL);
    const BattleItem* list = jill ? kJillItemBox : kChrisItemBox;
    const int count = jill ? (int)(sizeof(kJillItemBox) / sizeof(BattleItem))
                           : (int)(sizeof(kChrisItemBox) / sizeof(BattleItem));
    const int slots = jill ? 8 : 6;

    InitPlayerData();

    for (int i = 0; i < slots; i++) {
        g_ItemsSlots[i].Id = ITEM_NONE;
        g_ItemsSlots[i].qty = 0;
    }
    g_ItemSlotsBitmask = 0;
    g_TotalHeldItems = 0;
    g_EquippedItemId = 0;

    for (int i = 0; i < 48; i++) {
        g_itemboxSlots[i].Id = ITEM_NONE;
        g_itemboxSlots[i].qty = 0;
    }
    for (int i = 0; i < count; i++) {
        g_itemboxSlots[i].Id = list[i].id;
        g_itemboxSlots[i].qty = list[i].qty;
    }

    // 0x0608872C-0x0608876E: the starting totals.
    static const int kStart[2][BR_COUNT] = {
        { 90, 56, 54, 0, 0, 0, 4 },         // Chris
        { 90, 42, 42, 6, 6, 6, 5 },         // Jill
    };
    for (int i = 0; i < BR_COUNT; i++) s_battleLeft[i] = kStart[jill][i];

    g_playerEntity.healthStatusFlags &= 0x9D;
    g_playerEntity.health = (short)(jill * -44 + 140);
    g_PlayerHealthCopy = g_playerEntity.health;
}

// Battle mode never continues a save: the run always starts at arena 1 with the
// fresh card, the way TITLE -> BU_LOAD -> GAME5 does on the Saturn (mode 2 has
// no saved stage to restore).
//
// ONLY the flags are set here. The stage and the room are NOT: game_start
// rewrites the whole 1052-byte card from data\bio_card.dat before it does
// anything else, and g_stageId (card +0x200) and g_roomId (+0x201) live in it -
// so anything written here is thrown away, and the run would load the new
// game's room 106 with arena 1's background. saturn_battle_begin_run() sets
// them, after that memcpy.
void saturn_battle_start_run(void)
{
    battle_enter_saturn_mode();
    g_saturnBattleActive = 1;
    g_roomId = SATURN_BATTLE_FIRST_ROOM;
}

// "Enemies remaining", the number the state-0 clear test reads.
//
// It is NOT g_enemy_count. In this engine g_enemy_count is the number of slots
// cmd_enemy_set allocated for the room and NOTHING decrements it: a killed
// zombie keeps its slot with status_flags bit 0 up (zombie_dead_animation only
// ORs 0x0E into it) and the corpse lies there for the rest of the room, so a
// room whose last enemy has fallen over still reads 1 - and the Battle Game's
// clear condition can never come true. The whole engine depends on that
// invariant, so the counter itself cannot be touched here; the number the test
// needs has to be derived from it.
//
// The Saturn derives it, every frame, in its entity pass (GAME5 0x06055480 sets
// the field from the engine's enemy-count byte, 0x060555C8-0x060555D6 then
// subtracts one per slot): the room's enemy-slot count minus one for every slot
// whose Entity+0x88 health is negative - `cmp/pl R1` / `bt`, so the skip is
// taken while health is still non-negative. health < 0 is this engine's own
// "dead" convention everywhere else as well: apply_weapon_damage demotes to
// state 2 only while `health >= 0`, and both aim-target walks
// (player_find_aim_target and the enemy-in-view scan in PlayerAnimations.cpp)
// test `-1 < health`.
//
// The slots are walked the way update_entities and RoomInit walk them: the
// pointer advances over every slot but the counter only advances on an ACTIVE
// one, and 30 is the array's own size.
static int battle_enemies_left(void)
{
    const int total = g_enemy_count;
    if (total <= 0) return 0;

    const Entity* e   = g_EnemiesList;
    int           seen = 0;
    int           dead = 0;
    for (int slot = 0; slot < 30 && seen < total; slot++, e++) {
        if ((e->status_flags & ENTITY_STATUS_ACTIVE) != 0) {
            seen++;
            if (e->health < 0) dead++;
        }
    }

    const int left = total - dead;
    return left < 0 ? 0 : left;
}

void saturn_battle_begin_run(void)
{
    if (!g_bSaturnMode || !g_saturnBattleActive) return;

    // The stage and the room, after the card reset that wiped them - see
    // saturn_battle_start_run(). STAGE8 is g_stageId 7, and the room byte IS the
    // arena number, so arena 1 is room 1 and loads ROOM8010.
    g_stageId = SATURN_BATTLE_STAGE_ID;
    g_roomId = SATURN_BATTLE_FIRST_ROOM;
    g_roomCameraId = 0;
    g_main_state_flags &= ~MSF_CHAR_VARIANT;

    s_battleDcOutfit = (s_savedGameMode == GAME_MODE_DC &&
                        (g_DcGameMode == DC_DIFFICULTY_ADVANCED ||
                         g_DcGameMode == DC_DIFFICULTY_ADVANCED_HOLD));

    // Arm the run block. arena is left at 0, out of the table's range, so the
    // first battle_sync_arena() is what reads arena 1's fight-room bit - arena 1
    // is a rest room, so the bit starts clear and the run only starts clearing
    // once it reaches a fight.
    s_battle.flags = 0;
    s_battle.state = 0;
    s_battle.arena = 0;
    s_battle.timer = 0;
    s_battle.noAmmoFrames = 0;
    s_battleRunFrames = 0;
    s_battleClockRemainder = 0;
    s_battleFinished = 0;
    s_battle.enemiesLeft = battle_enemies_left();

    battle_apply_loadout();

    // The spawn point is code-side on the Saturn too: the transition routine
    // reads the arena's record and writes x, z and the facing into the player,
    // and leaves y to the caller (SATURN_VERSION_ANALYSIS.md 4e).
    const unsigned short* spawn = kArenaSpawn[saturn_battle_arena_index() - 1];
    g_playerEntity.position.x = (short)spawn[1];
    g_playerEntity.position.z = (short)spawn[2];
    g_playerEntity.directionAngle = (short)spawn[0];
}

static int battle_usable_ammo(void)
{
    const int hasGun      = (s_battle.flags & BB_HANDGUN)  != 0;
    const int hasShotgun  = (s_battle.flags & BB_SHOTGUN)  != 0;
    const int hasPython   = (s_battle.flags & BB_PYTHON)   != 0;
    const int hasLauncher = (s_battle.flags & BB_LAUNCHER) != 0;
    int total = 0;

    for (int i = 0; i < (int)g_TotalHeldItems && i < 8; i++) {
        const int id  = g_ItemsSlots[i].Id;
        const int qty = (int)g_ItemsSlots[i].qty;
        switch (id) {
            // loaded rounds
            case ITEM_BERETTA:         total += hasGun      ? qty : 0; break;
            case ITEM_SHOTGUN:         total += hasShotgun  ? qty : 0; break;
            case ITEM_COLT_PYTHON_MAG: total += hasPython   ? qty : 0; break;
            case ITEM_BAZOOKA_EXPLOSIVE:
            case ITEM_BAZOOKA_ACID:
            case ITEM_BAZOOKA_FLAME:
                total += hasLauncher ? qty : 0; break;
            // spare ammo, each tied to its weapon
            case ITEM_CLIP:          total += hasGun      ? qty : 0; break;
            case ITEM_SHELLS:        total += hasShotgun  ? qty : 0; break;
            case ITEM_MAGNUM_ROUNDS: total += hasPython   ? qty : 0; break;
            case ITEM_EXPLOSIVE_ROUNDS:
            case ITEM_ACID_ROUNDS:
            case ITEM_FLAME_ROUNDS:  total += hasLauncher ? qty : 0; break;
            default: break;
        }
    }
    return total;
}

// The weapon-held bits, from the inventory: the same walk the Saturn does every
// frame in state 0.
static void battle_scan_weapons(void)
{
    s_battle.flags &= ~(BB_KNIFE | BB_HANDGUN | BB_SHOTGUN | BB_PYTHON | BB_LAUNCHER);
    for (int i = 0; i < (int)g_TotalHeldItems && i < 8; i++) {
        switch (g_ItemsSlots[i].Id) {
            case ITEM_KNIFE:                 s_battle.flags |= BB_KNIFE;   break;
            case ITEM_BERETTA:               s_battle.flags |= BB_HANDGUN; break;
            case ITEM_SHOTGUN:               s_battle.flags |= BB_SHOTGUN; break;
            case ITEM_COLT_PYTHON_MAG:       s_battle.flags |= BB_PYTHON;  break;
            case ITEM_BAZOOKA_EXPLOSIVE:
            case ITEM_BAZOOKA_ACID:
            case ITEM_BAZOOKA_FLAME:  s_battle.flags |= BB_LAUNCHER; break;
            default: break;
        }
    }
}

// The door record the arena warp hands to room_transition_load. A static, so it
// outlives this frame: the loader reads it after game_loop has left its frame
// loop, exactly as it reads a room's own door record out of the RDT.
static unsigned char s_battleDoorRecord[0x1A];

// On to the next arena: GAME5 state 1 sets state 2 and bit 2 ("advance") and the
// room-transition routine (0x0602DBBC) takes over - through the SAME door path a
// real door uses. Its battle branch substitutes kArenaVirtualDoor[arena] for
// the door record's five fields, increments the room, and places the player
// from the spawn table (0x0602DE3E-0x0602DE9C): x and z from the record, y = 0
// (r12 has run down to 0 in the three-frame wait loop above it), the facing
// from the record, and the animation byte +0x84 = 0.
//
// The port does the same by building a door record and handing it to
// door_begin_transition(), so room_transition_load does the rest: the door
// sound bank, the door animation, the player placement (y = 0 and
// animationId = 0 included), the room load, the camera and the BGM - every one
// of which the old direct room_set() skipped. That warp was also run from the
// middle of a gameplay frame, with the player's Y, animation and the previous
// room's camera id all left as they were.
//
// Destination: the next room in the same stage (< 0x20), which is arena + 1.
// battle_sync_arena() sees the room change on the first frame after it and
// re-arms state 0, the timer and the fight-room bit. HP is not refilled: the
// Saturn carries it between arenas and the score pays for what is left.
//
// `virtualDoor` is the five-byte row that supplies the door animation, the
// door sound and the entry camera; the destination and the arrival point come
// from the arena's own spawn record, as GAME5's transition routine does it.
static void battle_warp(unsigned char arena, const unsigned char* virtualDoor)
{
    const unsigned short* spawn = kArenaSpawn[arena - 1];
    unsigned char* rec = s_battleDoorRecord;

    memset(rec, 0, sizeof(s_battleDoorRecord));
    rec[0x08] = virtualDoor[0];
    rec[0x09] = virtualDoor[1];
    rec[0x0A] = virtualDoor[2];
    rec[0x0B] = (unsigned char)((virtualDoor[4] & 0xC0) | (virtualDoor[3] & 0x3F));
    rec[0x0C] = 0;                                  // not locked
    rec[0x0D] = arena;                              // same stage
    *(unsigned short*)(rec + 0x0E) = spawn[1];      // x
    *(short*)(rec + 0x10)          = 0;             // y
    *(unsigned short*)(rec + 0x12) = spawn[2];      // z
    *(short*)(rec + 0x14)          = (short)spawn[0];

    s_battle.state = 2;
    door_begin_transition(rec);
}

static void battle_advance_room(void)
{
    const int arena = saturn_battle_arena_index();
    const int next = arena + 1;
    if (arena < 1 || next > SATURN_BATTLE_ARENA_COUNT) return;

    battle_warp((unsigned char)next, kArenaVirtualDoor[arena]);
}

// The debug menu's arena jump (DebugMenu.cpp's ROOM CHANGE, listed as STAGE8
// while a run is in progress). NOT the Saturn's: the original only ever moves
// on to arena + 1, through the three rest rooms' real doors and then the
// virtual warp. This goes to any arena 1..15, through the SAME machinery, so
// the door animation, the door sound, the arena's own music table and - above
// all - the spawn record stay the originals' rather than the debug menu's
// generic door-record placement, which would stand the player at whatever door
// the arena's RDT (a copy of a campaign room) happens to carry.
//
// The room byte is the arena number, so the destination stays inside STAGE8
// (dest < 0x20) and room_transition_load loads ROOM80<arena>0 through the
// normal path. Returns 1 when the warp was armed, 0 when it was refused.
int saturn_battle_debug_warp(int arena)
{
    if (!g_bSaturnMode || !g_saturnBattleActive) return 0;
    if (arena < SATURN_BATTLE_FIRST_ROOM || arena > SATURN_BATTLE_LAST_ROOM) return 0;
    // States 3 and 4 are the end sequence: the background has faded out, the
    // room sprites are off and the camera is locked onto the orbit. There is no
    // screen left to warp from, and once the run is finished game_loop is
    // leaving for saturn_ranking_state.
    if (s_battle.state >= 3 || s_battleFinished) return 0;

    dbg_printf("[saturn] battle debug warp: arena %d (ROOM80%X0)\n", arena, arena);

    // BB_ADVANCE is the bit state 1 set when a room clears; leaving it set with
    // state 2 would let the next frame's battle_advance_room() fire the real
    // warp as well. battle_sync_arena() re-arms everything for the new arena
    // (state 0, the timer, the per-room fight bit) on the first frame in it.
    s_battle.flags &= ~BB_ADVANCE;
    battle_warp((unsigned char)arena, kArenaVirtualDoor[arena]);
    return 1;
}

// ---------------------------------------------------------------------------
// The arena extras: Zombie Wesker (arena 12) and the golden Tyrant (arena 15)
// ---------------------------------------------------------------------------
// Both are the model loader's, not the room data's (SATURN_VERSION_ANALYSIS.md 4g).
#define SATURN_BATTLE_WESKER_ARENA  12
#define SATURN_BATTLE_GOLD_ARENA    15
#define SATURN_ZOMBIE_WESKER_TYPE   0x17
#define SATURN_ZOMBIE_WESKER_BEHAV  9

// GAME5's enemy model loop (0x0603C9A6-0x0603CB4E), on its "not the model the
// previous slot loaded" branch:
//
//   0603C9D8  battle bit set?  (tst ; bt skip)
//   0603C9E4  slot == 0?       (tst r10 ; bf skip)
//   0603C9E8  arena == 12?     (cmp/eq #12)
//   0603C9F4  entity +1 = 0x17, entity +2 = 9
//   0603CA06  last-model byte = 0x17 ; load model 0x17 + 4 = em1017
//   0603CA98  same three tests -> entity +1 = the original id again
//
// So the model is em1017, but the entity keeps its zombie type and runs the
// stock zombie AI with behaviour 9 - chase, with double HP (zombie_init,
// 0x0605E968) and a 12-point bite (the Saturn's damage row, 0x0609F3F8).
// The last-model byte is left at 0x17, so the next zombie loads its own model.
int saturn_battle_model_override(Entity* e, int slot)
{
    if (saturn_battle_arena_index() != SATURN_BATTLE_WESKER_ARENA || slot != 0) return 0;
    e->id = SATURN_ZOMBIE_WESKER_TYPE;
    e->behavior_flags = SATURN_ZOMBIE_WESKER_BEHAV;
    return 1;
}

// 0x0603A680(clut, count, +4, +3, -5), called from the EMD texture loader's
// CLUT copy (0x0603BD0A) for every model loaded in arena 15. On the PC the
// EMD's texture is a TIM at the file's tail; its CLUT block is patched before
// ProcessTmdAsync uploads it.
int saturn_battle_tint_model_tim(unsigned char* tim)
{
    if (saturn_battle_arena_index() != SATURN_BATTLE_GOLD_ARENA || tim == NULL) return 0;
    const unsigned int* hdr = (const unsigned int*)tim;
    if (hdr[0] != 0x10 || (hdr[1] & 8) == 0) return 0;     // TIM with a CLUT
    const unsigned int clutBytes = hdr[2];                  // block incl. its 12-byte header
    if (clutBytes <= 12) return 0;
    unsigned short* c = (unsigned short*)(tim + 8 + 12);
    const unsigned int n = (clutBytes - 12) / 2;
    for (unsigned int i = 0; i < n; i++) {
        const unsigned short v = c[i];
        int r = (v & 31) + 4;
        int g = ((v >> 5) & 31) + 3;
        int b = ((v >> 10) & 31) - 5;
        r = r < 0 ? 0 : (r > 31 ? 31 : r);
        g = g < 0 ? 0 : (g > 31 ? 31 : g);
        b = b < 0 ? 0 : (b > 31 ? 31 : b);
        c[i] = (unsigned short)((v & 0x8000) | (b << 10) | (g << 5) | r);
    }
    return 1;
}

// ---------------------------------------------------------------------------
// The end of the run: GAME5 states 3 and 4 (0x06088CEC / 0x06088D86)
// ---------------------------------------------------------------------------
// When the Tyrant's 120-frame clear pause ends, state 1 (0x06088B4C-0x06088C54)
// takes the room camera's eye and target (the camera block at 0x060A4EA0,
// +20..+28 eye, +32..+40 target, fixed point << 14), stores the horizontal
// eye-target distance and the heading as the orbit's radius and angle, stops
// the Tyrant theme, loads /SND/BATTLE.CDP and starts its song 1 (the fanfare),
// and goes to state 3.
//
// State 3 drives the background colour offsets (block + 0xA86/0xA8A/0xA8E) down
// by 32 a frame to -256 - the room goes black behind the models - and then hides
// the room sprites and goes to state 4.
//
// State 4 is the orbit, every frame:
//   target += (player - target) >> 4, player taken at (x, y - 0xA00, z)
//   eye     = target + R_y(angle) * (0, 0, radius)      (same height)
//   angle  -= timer - 0x1C2                  (16-bit angle: a spin that slows)
//   radius += (0x400 - radius) >> 6          while radius > 0x400
// and after timer 0x18F (400 frames) it raises bits 0-1 of g_SystemFlags, which
// makes GAME5's main return and hand over to RANKING.PRG.
//
// Port: the camera is the RDT camera layout MatrixToCamera takes (from, to,
// roll), the black background is the standalone screen mode the death screen
// uses (flat clear, no background quad, no room sprites - the models still
// draw), and the Saturn's 16-bit angle is converted to this engine's 12-bit one.
#define BATTLE_FINALE_FADE_FRAMES 8        // -256 / -32
#define BATTLE_FINALE_ORBIT_END   0x18F
#define BATTLE_FINALE_MIN_RADIUS  0x400

struct BattleOrbitCamera {                 // the ints MatrixToCamera reads
    int from[3];
    int to[3];
    int roll;
    int zero;
};

static BattleOrbitCamera s_orbitCam;
static int  s_orbitTarget[3];
static int  s_orbitRadius;
static int  s_orbitAngle;                  // Saturn 16-bit units

static void battle_begin_finale(void)
{
    const RDT_Camera* cams = (const RDT_Camera*)((const char*)g_RdtPointer + sizeof(RDT));
    const RDT_Camera& cam = cams[g_roomCameraId];

    s_orbitTarget[0] = cam.cam_to_x;
    s_orbitTarget[1] = cam.cam_to_y;
    s_orbitTarget[2] = cam.cam_to_z;
    const double dx = (double)(cam.cam_from_x - cam.cam_to_x);
    const double dz = (double)(cam.cam_from_z - cam.cam_to_z);
    s_orbitRadius = (int)sqrt(dx * dx + dz * dz);
    s_orbitAngle  = (int)(atan2(dx, dz) * 32768.0 / 3.14159265358979);

    s_battle.state = 3;
    s_battle.timer = 0;

    // Nothing else may re-cut the camera while it orbits.
    g_main_state_flags |= MSF_CAMERA_LOCK;

    // GAME5 0x06088BDE..0x06088C18: the Tyrant theme stops, BATTLE.CDP is loaded
    // and its song 1 - the fanfare - plays once.
    bgm_play_group_channel(SATURN_BGM_GROUP_BATTLE, 1);
}

static void battle_finale_fade(void)
{
    if (++s_battle.timer < BATTLE_FINALE_FADE_FRAMES) return;
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
    // 0x06088D20-0x06088D52: the room sprites (36-byte records, +32 = active)
    // are switched off - the background masks would otherwise hang in the black.
    for (int i = 0; g_RdtPointer != NULL && i < (int)g_RdtPointer->sprites_count && i < 128; i++) {
        g_RoomSprEntries[i].active = 0;
    }
    s_battle.state = 4;
    s_battle.timer = 0;
}

// One orbit frame. Returns 1 when the orbit is over.
static int battle_finale_step(void)
{
    const int p[3] = {
        g_playerEntity.scaMatrixData.localMatrix.t[0],
        g_playerEntity.scaMatrixData.localMatrix.t[1] - 0xA00,
        g_playerEntity.scaMatrixData.localMatrix.t[2],
    };
    for (int i = 0; i < 3; i++) {
        s_orbitTarget[i] += (p[i] - s_orbitTarget[i]) >> 4;
    }

    const double a = (double)s_orbitAngle * 3.14159265358979 / 32768.0;
    s_orbitCam.to[0] = s_orbitTarget[0];
    s_orbitCam.to[1] = s_orbitTarget[1];
    s_orbitCam.to[2] = s_orbitTarget[2];
    s_orbitCam.from[0] = s_orbitTarget[0] + (int)(sin(a) * s_orbitRadius);
    s_orbitCam.from[1] = s_orbitTarget[1];
    s_orbitCam.from[2] = s_orbitTarget[2] + (int)(cos(a) * s_orbitRadius);
    s_orbitCam.roll = 0;
    s_orbitCam.zero = 0;
    MatrixToCamera((MATRIX*)&s_orbitCam);

    s_orbitAngle -= s_battle.timer - 0x1C2;
    if (s_orbitRadius > BATTLE_FINALE_MIN_RADIUS) {
        s_orbitRadius += (BATTLE_FINALE_MIN_RADIUS - s_orbitRadius) >> 6;
    }

    return s_battle.timer++ > BATTLE_FINALE_ORBIT_END;
}

int saturn_battle_run_finished(void)
{
    return g_bSaturnMode && g_saturnBattleActive && s_battleFinished;
}


// ---------------------------------------------------------------------------
// Results: RANKING.PRG (main 0x0602E530) - SATURN_VERSION_ANALYSIS.md 4f
// ---------------------------------------------------------------------------
// What GAME5 hands over is the remaining HP, the run's frame count and what is
// left of each ammo type and of the sprays. The Saturn keeps the ammo as
// starting totals decremented once per shot (0x002FFFA8-0x002FFFAD); nothing in
// the run can gain or drop ammo, so the port counts what is left in the
// inventory and the item box instead, which is the same number.
struct BattleResult {
    int hp, maxHp;
    int handgun, shotgun, magnum, explosive, acid, flame, sprays;
    int frames;
    int seconds;
    int score;
};

// 0x060337B8: `weapon id - 2` indexes an 8-entry jump table - Beretta, shotgun,
// (dum-dum Python: none), Python, (flamethrower: none), and the launcher loaded
// with explosive / acid / flame rounds.
void saturn_battle_count_shot(unsigned char weaponId)
{
    if (!g_bSaturnMode || !g_saturnBattleActive) return;
    static const signed char kCounter[8] = {
        BR_HANDGUN, BR_SHOTGUN, -1, BR_MAGNUM, -1, BR_EXPLOSIVE, BR_ACID, BR_FLAME
    };
    const int idx = (int)weaponId - 2;
    if (idx < 0 || idx > 7 || kCounter[idx] < 0) return;
    if (s_battleLeft[kCounter[idx]] > 0) s_battleLeft[kCounter[idx]]--;
}

// Called when the player uses a healing item (the item-use path in MainMenu.cpp,
// only if it actually restores health). Counts one use against the run's
// remaining-sprays counter, as the Saturn does at GAME5 0x060444FA. Floors at 0;
// no-op outside Saturn mode or when no Battle Game run is active.
void saturn_battle_count_heal(void)
{
    if (!g_bSaturnMode || !g_saturnBattleActive) return;
    if (s_battleLeft[BR_SPRAYS] > 0) s_battleLeft[BR_SPRAYS]--;
}

// RANKING 0x0602C3AC, weights 0x06037AC0 = 1, 0, 2, 6, 10, 15, 15, 15, 150.
static void battle_compute_result(BattleResult* r)
{
    memset(r, 0, sizeof(*r));
    const int jill = ((g_playerEntity.id & 3) == CHAR_JILL);
    r->handgun   = s_battleLeft[BR_HANDGUN];
    r->shotgun   = s_battleLeft[BR_SHOTGUN];
    r->magnum    = s_battleLeft[BR_MAGNUM];
    r->explosive = s_battleLeft[BR_EXPLOSIVE];
    r->acid      = s_battleLeft[BR_ACID];
    r->flame     = s_battleLeft[BR_FLAME];
    r->sprays    = s_battleLeft[BR_SPRAYS];

    r->hp = g_playerEntity.health < 0 ? 0 : g_playerEntity.health;
    r->maxHp = 140 - 44 * jill;
    r->frames = s_battleRunFrames;
    r->seconds = s_battleRunFrames / 60;

    const int ammo = 2 * r->handgun + 6 * r->shotgun + 10 * r->magnum
                   + (jill ? 15 * (r->explosive + r->acid + r->flame) : 0)
                   + 150 * r->sprays;
    const double life = 100.0 / (double)r->maxHp * (double)r->hp * 1.0;
    const unsigned int base = (unsigned int)((double)ammo + life);
    int left = (jill ? 420 : 720) - r->seconds;
    if (left < 0) left = 0;
    r->score = (unsigned short)(base + left + 50 * (left / 60));
}

// The rank letter: table[score / 100] over 0x06037B13 = J..A, then S.
static char battle_rank_letter(int score)
{
    static const char kLetters[] = "JIHGFEDCBA";
    const int idx = score / 100;
    return idx < 10 ? kLetters[idx] : 'S';
}

// The two ten-record tables (RAM 0x06043AD8, one per character, 120 bytes
// apart). The Saturn keeps them in save bytes 0x00-0x9F; the PC card has no
// room for 240 bytes, so the port keeps them in their own file beside the saves.
struct BattleRecord {
    char           letter;
    unsigned short score;
    char           name[4];            // three letters and a 0, as +4..+7
    unsigned int   frames;             // +8: the clear time in FRAMES - the
                                       // default 0x34B84 is 59:59 at 60 per second
};
static BattleRecord s_ranking[2][10];

#define BATTLE_RANKING_FILE   "batrank.dat"
#define BATTLE_RANKING_MAGIC  0x324E5242u   // "BRN2" (records hold frames)

static void battle_ranking_path(char* out, size_t n)
{
    snprintf(out, n, "%s%s", GetSaveRoot(), BATTLE_RANKING_FILE);
}

// No records: the RANKING.PRG fill at 0x0602D58E - for both characters, score
// 250 - 20 * i, its letter, name "CAP" and time 0x34B84.
static void battle_ranking_defaults(void)
{
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 10; i++) {
            BattleRecord& rec = s_ranking[c][i];
            rec.score = (unsigned short)(250 - 20 * i);
            rec.letter = battle_rank_letter(rec.score);
            memcpy(rec.name, "CAP", 4);
            rec.frames = 0x34B84;
        }
    }
}

static void battle_ranking_load(void)
{
    battle_ranking_defaults();
    char path[300];
    battle_ranking_path(path, sizeof(path));
    size_t size = 0;
    unsigned char* data = (unsigned char*)plat_file_read_all(path, &size);
    if (data == NULL) return;
    if (size >= 4 + sizeof(s_ranking) && *(unsigned int*)data == BATTLE_RANKING_MAGIC) {
        memcpy(s_ranking, data + 4, sizeof(s_ranking));
    }
    free(data);
}

static void battle_ranking_save(void)
{
    unsigned char buf[4 + sizeof(s_ranking)];
    *(unsigned int*)buf = BATTLE_RANKING_MAGIC;
    memcpy(buf + 4, s_ranking, sizeof(s_ranking));
    char path[300];
    battle_ranking_path(path, sizeof(path));
    EnsureDirectoryExists(GetSaveRoot());
    plat_file_write(path, buf, sizeof(buf));
}

// The placement walk (0x0602C8C0): `cmp/hs`, so a tie keeps the older entry.
// Returns the place 0..9, or -1 (the Saturn's 99) when the score does not rank.
static int battle_ranking_place(int character, int score)
{
    for (int i = 0; i < 10; i++) {
        if (score > s_ranking[character][i].score) {
            for (int j = 9; j > i; j--) s_ranking[character][j] = s_ranking[character][j - 1];
            return i;
        }
    }
    return -1;
}

// --- the Saturn's own art -----------------------------------------------------
// RANKING.PRG's asset load (0x0602B1E4) puts /BATTLE.BIN at 0x00200000 and
// uploads it to the VDPs; the asset migrator copies the file, and /WAPON3.BIN,
// into the SATURN overlay's Data folder unchanged. What the screens use:
//
//   BATTLE.BIN +0x0000  X68 SSCONV section: +0x20 font A cells (0x30-0x1430,
//                       16x16 glyphs, 4 cells each), +0x24 the palette block
//                       (0x1430, sixteen 16-colour rows, big-endian RGB555),
//                       +0x28 font B cells (0x1630-0x2030, 8x16 glyphs, 2 cells)
//   BATTLE.BIN +0x10000 a 320x240 8bpp Windows BMP - the backdrop. 0x0602B428
//                       converts its BGRA palette to CRAM and copies its rows
//                       into VDP2 VRAM B.
//   WAPON3.BIN          X68 SSCONV: seven 112x16 item labels (14 columns of
//                       two cells each) + two 16-colour rows (normal, dim)
//
// Both fonts cover A-Z, 0-9, space and ':'; font B also has '.' and '-', font A
// a "pts" glyph. Glyphs are 4bpp with the LEFT pixel in the HIGH nibble (the
// PS1 / TIM order is the other way round). The text routines (0x0602B880 font
// A, 0x0602B910 font B) write VDP2 map cells: position = (cellX * 8, cellY * 8).
//
// The port builds two 4bpp texture pages from those bytes - fonts in slot 13,
// labels in slot 12, the title's two slots, which the title reloads when the
// run returns to it - and draws the BMP as the display image. The colour of a
// text run is one of the palette block's rows; every row is the white ramp
// scaled into one hue, so the port draws the white ramp and tints it.
//
// The Japanese release ("Bio Hazard", the SATURN_JPN overlay, read with the JPN
// tree) loads no WAPON3.BIN / SAVE2.BIN. Its RANKING.PRG (loader 0x0602B1CC)
// uploads what BATTLE.BIN itself carries, which the USA module overwrote:
//   BATTLE.BIN +0x35000 X68 SSCONV, WAPON3.BIN's very layout: the seven item
//                       labels in Japanese (ベレッタの弾 ... グレネード火炎弾)
//   BATTLE.BIN +0x31EB0 128x64 4bpp yes/no prompts, four 128x16 rows -
//                       セーブしますか? / 続けて遊びますか? / YES / NO (VDP1
//                       characters 0x2400 / 0x2480 / 0x2500 / 0x2580), palette
//                       +0x32EB0 (a grey ramp; SAVE2.BIN's USA counterpart)
// and RANKING.PRG draws the "BIO HAZARD" logo behind both screens (below).
#define SAT_BATTLE_BIN_SIZE   225280
#define SAT_WAPON3_BIN_SIZE   6824
#define SAT_FONT_A_OFF        0x0030
#define SAT_FONT_PAL_OFF      0x1430
#define SAT_FONT_B_OFF        0x1630
#define SAT_BMP_OFF           0x10000
#define SAT_LABELS_OFF        0x0028
#define SAT_LABELS_PAL_OFF    0x18A8
#define SAT_JPN_LABELS_OFF    0x35000    // BATTLE.BIN, WAPON3.BIN's layout
#define SAT_JPN_PROMPT_OFF    0x31EB0    // BATTLE.BIN, 128x64 4bpp
#define SAT_JPN_PROMPT_PAL_OFF 0x32EB0
#define SAT_PROMPT_V          80         // the prompts' rows in the fonts page
#define SAT_PAGE_FONTS_SLOT   13
#define SAT_PAGE_FONTS_ID     9
#define SAT_PAGE_LABELS_SLOT  12
#define SAT_PAGE_LABELS_ID    8

static int s_resultsArt = 0;             // 1 = the Saturn's art is loaded
static int s_resultsJpn = 0;             // 1 = the Japanese release's art

// Palette rows of the font block (CRAM 0x200 + 0x20 * row), as the tint that
// turns the white ramp into that row. Read from the rows' brightest entries.
enum { SAT_PAL_WHITE, SAT_PAL_GREY, SAT_PAL_YELLOW, SAT_PAL_DIM_YELLOW,
       SAT_PAL_GREEN, SAT_PAL_CYAN, SAT_PAL_ORANGE, SAT_PAL_COUNT };
static const unsigned char kSatPalTint[SAT_PAL_COUNT][3] = {
    { 128, 128, 128 },   // 0 0x200: 0x8000 .. 0xFFFF
    {  66,  66,  66 },   // 1 0x220: .. 0xC210 (16/31)
    { 128, 128,   0 },   // 2 0x240: .. 0x83FF (R31 G31)
    {  66,  66,   0 },   // 3 0x260: .. 0x8210 (R16 G16)
    {   0, 107,   0 },   // 4 0x280: .. 0x8340 (G26)
    {   0, 116, 116 },   // 5 0x2A0: .. 0xF380 (G28 B28)
    { 128,  66,   0 },   // 6 0x2C0: .. 0x821F (R31 G16)
};

static unsigned short sat_to_ps1(unsigned short satBE)
{
    // Saturn RGB555 and the PS1's share the bit layout (red in bits 0-4); the
    // Saturn's is stored big-endian. Black is the port's transparency key.
    return (unsigned short)(satBE & 0x7FFF);
}

static unsigned short read_be16(const unsigned char* p)
{
    return (unsigned short)((p[0] << 8) | p[1]);
}

// A 4bpp TIM of w x h with one 16-colour CLUT at (0, 0x1E0). Returns its size.
static unsigned int sat_tim_begin(unsigned char* out, int w, int h,
                                  const unsigned short clut[16])
{
    unsigned char* p = out;
    const unsigned int pixBytes = (unsigned int)(w * h / 2);
    *(unsigned int*)p = 0x10;                 p += 4;
    *(unsigned int*)p = 0x08;                 p += 4;
    *(unsigned int*)p = 12 + 32;              p += 4;
    *(unsigned short*)p = 0;                  p += 2;
    *(unsigned short*)p = 0x1E0;              p += 2;
    *(unsigned short*)p = 16;                 p += 2;
    *(unsigned short*)p = 1;                  p += 2;
    for (int i = 0; i < 16; i++) { *(unsigned short*)p = clut[i]; p += 2; }
    *(unsigned int*)p = 12 + pixBytes;        p += 4;
    *(unsigned short*)p = 0;                  p += 2;
    *(unsigned short*)p = 0;                  p += 2;
    *(unsigned short*)p = (unsigned short)(w / 4); p += 2;
    *(unsigned short*)p = (unsigned short)h;  p += 2;
    memset(p, 0, pixBytes);
    return (unsigned int)(p - out) + pixBytes;
}

// Copy one 8x8 Saturn 4bpp cell into a TIM image at (x, y), swapping nibbles.
static void sat_blit_cell(unsigned char* timPixels, int timW,
                          const unsigned char* cell, int x, int y)
{
    for (int row = 0; row < 8; row++) {
        unsigned char* dst = timPixels + ((y + row) * timW + x) / 2;
        for (int b = 0; b < 4; b++) {
            const unsigned char v = cell[row * 4 + b];
            dst[b] = (unsigned char)((v >> 4) | (v << 4));
        }
    }
}

// Glyph index in either font; -1 for a character the font does not have.
static int sat_glyph_index(char c, int fontA)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '0' && c <= '9') return 26 + (c - '0');
    if (c == ' ') return 36;
    if (c == ':') return 37;
    if (!fontA && c == '.') return 38;
    if (!fontA && c == '-') return 39;
    return -1;
}
#define SAT_GLYPH_PTS 39                  // font A

// Saturn RGB555 brightness (0..93) for picking the nearest ramp entry.
static int sat_luma(unsigned short c)
{
    return (c & 31) + ((c >> 5) & 31) + ((c >> 10) & 31);
}

static int battle_results_load_art(void)
{
    s_resultsArt = 0;
    // The JPN tree is what selects the SATURN_JPN overlay (AssetPath.cpp), so
    // it is also what says which release's art that overlay holds.
    s_resultsJpn = (GetAssetVersion() != 0);
    unsigned char* bat = (unsigned char*)malloc(SAT_BATTLE_BIN_SIZE + 0x40);
    unsigned char* wap = s_resultsJpn ? NULL : (unsigned char*)malloc(SAT_WAPON3_BIN_SIZE + 0x40);
    unsigned char* tim = (unsigned char*)malloc(0x10000);
    unsigned short* bg = (unsigned short*)malloc(320 * 240 * 2);
    int ok = bat && (wap || s_resultsJpn) && tim && bg;
    if (ok) {
        const int nb = LoadFile(GAME_DATA_ROOT "data\\battle.bin", bat, 0x20);
        ok = nb == SAT_BATTLE_BIN_SIZE &&
             bat[SAT_BMP_OFF] == 'B' && bat[SAT_BMP_OFF + 1] == 'M';
        if (ok && !s_resultsJpn) {
            const int nw = LoadFile(GAME_DATA_ROOT "data\\wapon3.bin", wap, 0x20);
            ok = nw == SAT_WAPON3_BIN_SIZE;
        }
    }
    // The item labels: WAPON3.BIN on the USA disc, BATTLE.BIN's own on the JPN.
    const unsigned char* labels = s_resultsJpn ? bat + SAT_JPN_LABELS_OFF : wap;
    if (ok) {
        // Page A: font B glyph k at ((k % 32) * 8, (k / 32) * 16), font A glyph
        // k at ((k % 16) * 16, 32 + (k / 16) * 16), both from palette row 0.
        // JPN: the prompt art below them, at (0, SAT_PROMPT_V).
        unsigned short clut[16];
        for (int i = 0; i < 16; i++) clut[i] = sat_to_ps1(read_be16(bat + SAT_FONT_PAL_OFF + 2 * i));
        clut[0] = 0;
        sat_tim_begin(tim, 256, s_resultsJpn ? SAT_PROMPT_V + 64 : 80, clut);
        unsigned char* pix = tim + 20 + 32 + 12;
        for (int k = 0; k < 40; k++) {
            const unsigned char* g = bat + SAT_FONT_B_OFF + k * 64;
            const int x = (k % 32) * 8, y = (k / 32) * 16;
            sat_blit_cell(pix, 256, g,      x, y);
            sat_blit_cell(pix, 256, g + 32, x, y + 8);
        }
        for (int k = 0; k < 40; k++) {
            const unsigned char* g = bat + SAT_FONT_A_OFF + k * 128;
            const int x = (k % 16) * 16, y = 32 + (k / 16) * 16;
            sat_blit_cell(pix, 256, g,      x,     y);
            sat_blit_cell(pix, 256, g + 32, x + 8, y);
            sat_blit_cell(pix, 256, g + 64, x,     y + 8);
            sat_blit_cell(pix, 256, g + 96, x + 8, y + 8);
        }
        if (s_resultsJpn) {
            // The prompts are a plain 128-pixel-wide bitmap (VDP1 sprite data,
            // left pixel in the high nibble like the cells), drawn on the Saturn
            // with their own grey ramp. The port's page has one CLUT - the font
            // ramp, white like it - so each prompt index is moved onto the font
            // ramp's nearest entry; index 0 stays the transparent one.
            unsigned char remap[16];
            remap[0] = 0;
            for (int i = 1; i < 16; i++) {
                const int want = sat_luma(read_be16(bat + SAT_JPN_PROMPT_PAL_OFF + 2 * i));
                int best = 1;
                for (int k = 2; k < 16; k++) {
                    const int dk = abs(sat_luma(read_be16(bat + SAT_FONT_PAL_OFF + 2 * k)) - want);
                    const int db = abs(sat_luma(read_be16(bat + SAT_FONT_PAL_OFF + 2 * best)) - want);
                    if (dk < db) best = k;
                }
                remap[i] = (unsigned char)best;
            }
            const unsigned char* src = bat + SAT_JPN_PROMPT_OFF;
            for (int y = 0; y < 64; y++) {
                unsigned char* dst = pix + (SAT_PROMPT_V + y) * 256 / 2;
                for (int b = 0; b < 64; b++) {
                    const unsigned char v = src[y * 64 + b];
                    dst[b] = (unsigned char)(remap[v >> 4] | (remap[v & 15] << 4));
                }
            }
        }
        LoadTexturePage(tim, SAT_PAGE_FONTS_ID, 0, SAT_PAGE_FONTS_SLOT, 0, 0, 0, 0);

        // Page B: label L at ((L % 2) * 112, (L / 2) * 16), its own palette.
        for (int i = 0; i < 16; i++) clut[i] = sat_to_ps1(read_be16(labels + SAT_LABELS_PAL_OFF + 2 * i));
        clut[0] = 0;
        sat_tim_begin(tim, 256, 64, clut);
        pix = tim + 20 + 32 + 12;
        for (int L = 0; L < 7; L++) {
            for (int c = 0; c < 14; c++) {
                const unsigned char* g = labels + SAT_LABELS_OFF + (L * 14 + c) * 64;
                const int x = (L % 2) * 112 + c * 8, y = (L / 2) * 16;
                sat_blit_cell(pix, 256, g,      x, y);
                sat_blit_cell(pix, 256, g + 32, x, y + 8);
            }
        }
        LoadTexturePage(tim, SAT_PAGE_LABELS_ID, 0, SAT_PAGE_LABELS_SLOT, 0, 0, 0, 0);

        // The backdrop: the BMP's rows are bottom-up; its palette is BGRA.
        const unsigned char* bmp = bat + SAT_BMP_OFF;
        const unsigned int bits = *(const unsigned int*)(bmp + 10);
        const int w = *(const int*)(bmp + 18), h = *(const int*)(bmp + 22);
        const unsigned char* pal = bmp + 14 + *(const unsigned int*)(bmp + 14);
        const int stride = (w + 3) & ~3;
        for (int y = 0; y < 240; y++) {
            for (int x = 0; x < 320; x++) {
                unsigned short c = 0;
                if (x < w && y < h) {
                    const unsigned char i = bmp[bits + (h - 1 - y) * stride + x];
                    const unsigned char* e = pal + i * 4;
                    c = (unsigned short)((e[2] >> 3) | ((e[1] >> 3) << 5) | ((e[0] >> 3) << 10));
                }
                bg[y * 320 + x] = c;
            }
        }
        display_image(0, bg, 320, 240);
        s_resultsArt = 1;
    }
    free(bat); free(wap); free(tim); free(bg);
    return s_resultsArt;
}

// --- the Japanese release's "BIO HAZARD" logo -------------------------------
// Bio Hazard RANKING.PRG draws a 3D "BIO HAZARD" behind the score screen, the
// name entry, the ranking and both prompts (every state of 0x0602D398 but the
// first calls 0x0602D2E4). The model is in the USA module too (0x0603C2EC), but
// nothing there calls its copy of the draw (0x0602D33C) - the logo is a Japanese
// release feature, gated here on the JPN art. tools/saturn_ranking_logo.py is
// the reference renderer.
//
//   0x0602D2E4  slPerspective(0x2000 = 45 degrees); push; slTranslate(pos
//               0x06042610); slRotX/Y/Z(0x06042608/A/C); the tree walk
//               0x0602D230 on root 0x0603C18C; pop; rot Y -= 91, rot Z += 182
//   0x0602D230  push; slTranslate / slRotZ,Y,X / slScale with the record's own
//               values; slPutPolygonX(part, light 0x06042620); the child under
//               the same matrix; pop; then the sibling
//   state 0     angles 0; pos (0, 0, 64.0); CRAM 0x25F00020 (colour 16..31) =
//               4 x black, then 0x8000 | (i - 4): a red ramp R = 0..11 of 31;
//               light (0x93CD, 0x93CD, 0x93CD) = normalised (1, 1, 1)
//
// An object record is 0x2C bytes: {XPDATA*, FIXED pos[3], ANGLE ang[3] + pad,
// FIXED scl[3], child*, sibling*}; an XPDATA 24: {POINT* (FIXED x,y,z), count,
// POLYGON* (FIXED normal[3], u16 vertices[4]), count, ATTR*, normal work}. All
// nine records hold an identity transform and every ATTR is the same (00 2B
// 0000 00E0 0010 0000 0004: single-sided, centre Z sort, light, palette, colour
// 16, no texture), so the logo is all nine letters under the one matrix, each
// face flat-shaded on the ramp. The loader walks the tree and refuses a record
// that is not an identity, rather than assume.
//
// PORT APPROXIMATION: SGL's light-to-palette offset is not read out of the
// library. The port uses offset = clamp(-(n . l) * 31, 0, 15), calibrated on a
// capture of the real screen (the face toward the camera is the brightest
// entry, ~90/255 red; faces turned from the light fall on the black entries -
// the dark edges the logo shows there). Faces are depth-sorted far to near by
// their centre, as the attribute's Z sort asks; the screen is 320 wide with its
// centre at (160, 120).
#define SAT_PRG_BASE          0x0602B000u
#define SAT_PRG_SIZE_JPN      70080
#define SAT_LOGO_ROOT_JPN     0x0603C18Cu
#define SAT_LOGO_MAX_PARTS    16
#define SAT_LOGO_MAX_POINTS   512
#define SAT_LOGO_MAX_POLYS    512
#define SAT_LOGO_DIST         64.0f
#define SAT_LOGO_ROT_Y_STEP   (-91)
#define SAT_LOGO_ROT_Z_STEP   182
#define SAT_LOGO_LIGHT        (float)(0x93CD / 65536.0)
#define SAT_LOGO_DEPTH        0xF000u    // behind every results text sprite

struct SatLogoPoly {
    float normal[3];
    unsigned short v[4];                 // into s_logoPts; v[2] == v[3]: triangle
};
static float       s_logoPts[SAT_LOGO_MAX_POINTS][3];
static SatLogoPoly s_logoPolys[SAT_LOGO_MAX_POLYS];
static int         s_logoPointCount = 0;
static int         s_logoPolyCount = 0;
static int         s_logoReady = 0;
static unsigned int s_logoFrame = 0;

static unsigned int read_be32(const unsigned char* p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | p[3];
}

static float sat_fixed(const unsigned char* p)
{
    return (float)(int)read_be32(p) * (1.0f / 65536.0f);
}

// One object record and what hangs off it, depth first. Returns 0 on anything
// the port does not expect.
static int sat_logo_walk(const unsigned char* prg, unsigned int addr, int depth)
{
    if (addr == 0) return 1;
    if (depth > SAT_LOGO_MAX_PARTS || addr < SAT_PRG_BASE ||
        addr + 0x2C > SAT_PRG_BASE + SAT_PRG_SIZE_JPN) return 0;
    const unsigned char* rec = prg + (addr - SAT_PRG_BASE);
    for (int i = 0; i < 3; i++) {
        if (read_be32(rec + 4 + 4 * i) != 0) return 0;          // pos
        if (read_be32(rec + 24 + 4 * i) != 0x10000u) return 0;  // scale 1.0
    }
    if (read_be32(rec + 16) != 0 || (read_be32(rec + 20) >> 16) != 0) return 0;  // angles

    const unsigned int part = read_be32(rec);
    if (part != 0) {
        if (part < SAT_PRG_BASE || part + 24 > SAT_PRG_BASE + SAT_PRG_SIZE_JPN) return 0;
        const unsigned char* xp = prg + (part - SAT_PRG_BASE);
        const unsigned int pts = read_be32(xp), npts = read_be32(xp + 4);
        const unsigned int pol = read_be32(xp + 8), npol = read_be32(xp + 12);
        if ((int)npts > SAT_LOGO_MAX_POINTS - s_logoPointCount ||
            (int)npol > SAT_LOGO_MAX_POLYS - s_logoPolyCount ||
            pts < SAT_PRG_BASE || pts + npts * 12 > SAT_PRG_BASE + SAT_PRG_SIZE_JPN ||
            pol < SAT_PRG_BASE || pol + npol * 20 > SAT_PRG_BASE + SAT_PRG_SIZE_JPN)
            return 0;
        const int first = s_logoPointCount;
        for (unsigned int k = 0; k < npts; k++) {
            const unsigned char* p = prg + (pts - SAT_PRG_BASE) + 12 * k;
            for (int c = 0; c < 3; c++) s_logoPts[s_logoPointCount][c] = sat_fixed(p + 4 * c);
            s_logoPointCount++;
        }
        for (unsigned int k = 0; k < npol; k++) {
            const unsigned char* p = prg + (pol - SAT_PRG_BASE) + 20 * k;
            SatLogoPoly* q = &s_logoPolys[s_logoPolyCount++];
            for (int c = 0; c < 3; c++) q->normal[c] = sat_fixed(p + 4 * c);
            for (int c = 0; c < 4; c++) {
                const unsigned int v = read_be16(p + 12 + 2 * c);
                if (v >= npts) return 0;
                q->v[c] = (unsigned short)(first + v);
            }
        }
    }
    return sat_logo_walk(prg, read_be32(rec + 36), depth + 1) &&   // child
           sat_logo_walk(prg, read_be32(rec + 40), depth + 1);     // sibling
}

static void battle_results_load_logo(void)
{
    s_logoReady = 0;
    s_logoPointCount = 0;
    s_logoPolyCount = 0;
    if (!s_resultsJpn) return;
    unsigned char* prg = (unsigned char*)malloc(SAT_PRG_SIZE_JPN + 0x40);
    if (prg == NULL) return;
    if (LoadFile(GAME_DATA_ROOT "data\\ranking.prg", prg, 0x20) == SAT_PRG_SIZE_JPN &&
        sat_logo_walk(prg, SAT_LOGO_ROOT_JPN, 0) && s_logoPolyCount > 0) {
        s_logoReady = 1;
    } else {
        dbg_printf("[battle] no Bio Hazard RANKING.PRG logo - re-run the asset migrator's "
                   "Saturn tab on the Japanese disc\n");
    }
    free(prg);
}

// The type-13 sprite command's callback: the logo for s_logoFrame, drawn in
// the queue's place - over the backdrop, under every text sprite.
static void sat_logo_draw(float scaleX, float scaleY)
{
    // slRotX(0) slRotY(ay) slRotZ(az) on the pushed matrix: a point goes
    // through Rz first, then Ry (Rx is the identity).
    const float ay = (float)(short)(SAT_LOGO_ROT_Y_STEP * (int)s_logoFrame) * (6.2831853f / 65536.0f);
    const float az = (float)(short)(SAT_LOGO_ROT_Z_STEP * (int)s_logoFrame) * (6.2831853f / 65536.0f);
    const float cy = cosf(ay), sy = sinf(ay), cz = cosf(az), sz = sinf(az);
    const float m[3][3] = {
        {  cy * cz, -cy * sz, sy },
        {       sz,       cz, 0.0f },
        { -sy * cz,  sy * sz, cy },
    };
    // slPerspective(45 degrees) over a 320-wide screen.
    const float scr = 160.0f / tanf(22.5f * 3.14159265f / 180.0f);

    static float view[SAT_LOGO_MAX_POINTS][3];
    for (int i = 0; i < s_logoPointCount; i++) {
        const float* p = s_logoPts[i];
        for (int r = 0; r < 3; r++) view[i][r] = m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2];
        view[i][2] += SAT_LOGO_DIST;
    }

    struct Face { float z; int poly; unsigned char shade; };
    static Face faces[SAT_LOGO_MAX_POLYS];
    int n = 0;
    for (int i = 0; i < s_logoPolyCount; i++) {
        const SatLogoPoly* q = &s_logoPolys[i];
        const int corners = (q->v[2] == q->v[3]) ? 3 : 4;
        float c[3] = { 0.0f, 0.0f, 0.0f };
        for (int k = 0; k < corners; k++)
            for (int r = 0; r < 3; r++) c[r] += view[q->v[k]][r];
        for (int r = 0; r < 3; r++) c[r] /= (float)corners;
        float nv[3];
        for (int r = 0; r < 3; r++)
            nv[r] = m[r][0] * q->normal[0] + m[r][1] * q->normal[1] + m[r][2] * q->normal[2];
        // Single plane: only a face whose normal points at the camera.
        if (nv[0] * c[0] + nv[1] * c[1] + nv[2] * c[2] >= 0.0f) continue;
        float d = -(nv[0] + nv[1] + nv[2]) * SAT_LOGO_LIGHT;
        int offset = (int)(d * 31.0f);
        if (offset < 0) offset = 0;
        if (offset > 15) offset = 15;
        faces[n].z = c[2];
        faces[n].poly = i;
        faces[n].shade = (unsigned char)offset;
        n++;
    }
    // Far to near; a plain insertion sort keeps equal depths in model order.
    for (int i = 1; i < n; i++) {
        const Face f = faces[i];
        int j = i - 1;
        while (j >= 0 && faces[j].z < f.z) { faces[j + 1] = faces[j]; j--; }
        faces[j + 1] = f;
    }

    // MERGE140: 120 KB, Battle Game only - allocated on first use.
    static float (*verts)[10] = NULL;
    if (verts == NULL) verts = (float (*)[10])malloc(sizeof(float) * 1024 * 3 * 10);
    if (verts == NULL) return;
    int tris = 0;
    for (int i = 0; i < n; i++) {
        const SatLogoPoly* q = &s_logoPolys[faces[i].poly];
        // The ramp entry: four blacks, then R = 0..11 of 31.
        const int r5 = faces[i].shade < 4 ? 0 : faces[i].shade - 4;
        const float red = (float)((r5 << 3) | (r5 >> 2)) * (1.0f / 255.0f);
        const int fan = (q->v[2] == q->v[3]) ? 1 : 2;
        for (int t = 0; t < fan; t++) {
            const int idx[3] = { q->v[0], q->v[t + 1], q->v[t + 2] };
            for (int k = 0; k < 3; k++) {
                const float* p = view[idx[k]];
                float* o = verts[tris * 3 + k];
                o[0] = (160.0f + p[0] * scr / p[2]) * scaleX;
                o[1] = (120.0f + p[1] * scr / p[2]) * scaleY;
                o[2] = 0.5f;
                o[3] = p[2];
                o[4] = 0.5f;
                o[5] = 0.5f;
                o[6] = red;
                o[7] = 0.0f;
                o[8] = 0.0f;
                o[9] = 1.0f;
            }
            if (++tris == 1024) {
                MarniDrawTrianglesPersp(&verts[0][0], tris, MARNI_NULL_HANDLE);
                tris = 0;
            }
        }
    }
    if (tris > 0) MarniDrawTrianglesPersp(&verts[0][0], tris, MARNI_NULL_HANDLE);
}

// One glyph or label from the art pages, at screen (x, y), tinted.
static void sat_draw(int labels, int u, int v, int w, int h, int x, int y,
                     const unsigned char tint[3])
{
    TextureDesc td;
    memset(&td, 0, sizeof(td));
    td.flags = 0x10000000;
    td.texturePage = labels ? SAT_PAGE_LABELS_ID : SAT_PAGE_FONTS_ID;
    td.texU = (unsigned char)u;
    td.texV = (unsigned char)v;
    td.width = (short)w;
    td.height = (short)h;
    td.screenX = (short)(x - g_ScreenOffsetX);
    td.screenY = (short)(y - g_ScreenOffsetY);
    td.colorMulR = tint[0];
    td.colorMulG = tint[1];
    td.colorMulB = tint[2];
    td.clutY = 0x1E0;
    display_texture(&td, 2, labels ? SAT_PAGE_LABELS_SLOT : SAT_PAGE_FONTS_SLOT, 1);
}

// 0x0602B910: font B at map cell (cx, cy).
static void sat_text_b(const char* s, int cx, int cy, int pal)
{
    for (int i = 0; s[i] != '\0'; i++) {
        const int k = sat_glyph_index(s[i], 0);
        if (k >= 0 && k != 36) {
            sat_draw(0, (k % 32) * 8, (k / 32) * 16, 8, 16, (cx + i) * 8, cy * 8, kSatPalTint[pal]);
        }
    }
}

// 0x0602B880 / 0x0602BB68: font A at map cell (cx, cy), two cells per glyph.
static void sat_glyph_a(int k, int cx, int cy, int pal)
{
    if (k < 0 || k == 36) return;
    sat_draw(0, (k % 16) * 16, 32 + (k / 16) * 16, 16, 16, cx * 8, cy * 8, kSatPalTint[pal]);
}

static void sat_text_a(const char* s, int cx, int cy, int pal)
{
    for (int i = 0; s[i] != '\0'; i++) sat_glyph_a(sat_glyph_index(s[i], 1), cx + 2 * i, cy, pal);
}

// 0x0602B988: a two-digit count, the tens only when non-zero.
static void sat_count(int v, int cx, int cy, int pal)
{
    if (v < 0) v = 0;
    if (v > 99) v = 99;
    char buf[3] = { 0, 0, 0 };
    if (v / 10) { buf[0] = (char)('0' + v / 10); sat_text_b(buf, cx, cy, pal); }
    buf[0] = (char)('0' + v % 10);
    sat_text_b(buf, cx + 1, cy, pal);
}

// 0x0602BA38: mm:ss from a frame count, five cells.
static void sat_time(unsigned int frames, int cx, int cy, int pal)
{
    unsigned int mm = frames / 3600, ss = (frames % 3600) / 60;
    if (mm > 99) mm = 99;
    char buf[8];
    sprintf(buf, "%02u:%02u", mm, ss);
    sat_text_b(buf, cx, cy, pal);
}

// 0x0602BC4C: a four-digit score, leading zeros suppressed, then "pts".
static void sat_score(int v, int cx, int cy, int pal)
{
    if (v < 0) v = 0;
    if (v > 9999) v = 9999;
    const int d[4] = { v / 1000, (v / 100) % 10, (v / 10) % 10, v % 10 };
    int lead = 1;
    for (int i = 0; i < 4; i++) {
        if (d[i] != 0 || i == 3) lead = 0;
        if (!lead) {
            char buf[2] = { (char)('0' + d[i]), 0 };
            sat_text_b(buf, cx + i, cy, pal);
        }
    }
    sat_glyph_a(SAT_GLYPH_PTS, cx + 4, cy, pal);
}

static unsigned int s_resultsFrame = 0;

// The flicker: RANKING passes `(frame counter >> 2) & 2` as the palette of the
// character's name and the rank letter - white and yellow, eight frames each
// (0x0602C6BA / 0x0602CA70).
static int sat_flicker(void)
{
    return (int)((s_resultsFrame >> 2) & 2);
}

// One results frame: present what was queued and read the edge-pressed pad.
// The Japanese release's logo is queued first, and turns once per frame -
// 0x0602D2E4 steps its angles after every draw.
static unsigned int battle_results_frame(void)
{
    if (s_logoReady && AddSpriteCallback(sat_logo_draw, SAT_LOGO_DEPTH)) s_logoFrame++;
    Task_sleep(1);
    s_resultsFrame++;
    return g_PlayerPadHeld;
}

#define BATTLE_PAD_CONFIRM 0x0080u
#define BATTLE_PAD_CANCEL  0x0040u
#define BATTLE_PAD_UP      0x1000u
#define BATTLE_PAD_RIGHT   0x2000u
#define BATTLE_PAD_DOWN    0x4000u
#define BATTLE_PAD_LEFT    0x8000u

static void battle_solid(int x, int y, int w, int h,
                         unsigned char r, unsigned char g, unsigned char b, int depth)
{
    RectDrawDesc rect;
    memset(&rect, 0, sizeof(rect));
    rect.textureId = 0;                   // variant 0: the opaque fill
    rect.x = (short)(x - 160);
    rect.y = (short)(y - 120);
    rect.w = (short)w;
    rect.h = (short)h;
    rect.r = r;
    rect.g = g;
    rect.b = b;
    draw_rect(&rect, depth, 0);
}

// The score screen (RANKING 0x0602C5C0).
static void battle_draw_score(const BattleResult& r, int jill)
{
    sat_text_a("SCORE", 15, 2, SAT_PAL_WHITE);
    sat_text_b(jill ? "JILL" : "CHRIS", 18, 5, sat_flicker());

    // The label table (0x0602C5D4): (2, 8/10/12/14), (21, 8/10/12); the
    // grenade rows are palette 9, the dim row, for Chris; each count sits at
    // label + 15 cells.
    static const unsigned char kLabelXY[7][2] = {
        { 2, 8 }, { 2, 10 }, { 2, 12 }, { 2, 14 }, { 21, 8 }, { 21, 10 }, { 21, 12 }
    };
    const int values[7] = { r.handgun, r.shotgun, r.magnum, r.sprays,
                            r.explosive, r.acid, r.flame };
    static const unsigned char kNormal[3] = { 128, 128, 128 };
    static const unsigned char kDim[3]    = {  90,  90,  90 };
    for (int L = 0; L < 7; L++) {
        const int dim = !jill && L > 3;
        sat_draw(1, (L % 2) * 112, (L / 2) * 16, 112, 16,
                 kLabelXY[L][0] * 8, kLabelXY[L][1] * 8, dim ? kDim : kNormal);
        sat_count(values[L], kLabelXY[L][0] + 15, kLabelXY[L][1], SAT_PAL_WHITE);
    }

    sat_text_b("LIFE", 7, 17, SAT_PAL_CYAN);
    // The bar (RANKING 0x0602C204): two scaled VDP1 sprites, both anchored by
    // their lower-right corner (CTRL 0x0F01) at constant coordinates - it does
    // not move. The art is BATTLE.BIN +0x34000 (palette: yellow, red, black):
    //   the bar   256x16 at VDP1 0x25C14000, drawn 264x16 with its corner at
    //             (200, 64), Z 35.0: a 248x14 yellow block and its black
    //             shadow, 4 right and 2 down
    //   the loss  8x16 at 0x25C14800, 8x14 of red, drawn `256 - 256 / max * hp`
    //             wide (a double, truncated; skipped at 0) with its corner at
    //             (192, 64), Z 34.0 - the yellow's right end, so the red grows
    //             leftward over it as HP is lost
    // VDP1 coordinates are from the screen centre and run at twice the text
    // grid's scale, so everything halves: the yellow is 128x7 at (128, 136).
    // PORT APPROXIMATION: flat fills in the art's three colours. Both sprites
    // set SPD, so the Saturn also draws their colour-0 margins (CRAM 0xB0);
    // those slivers are not reproduced.
    double lost = r.maxHp > 0 ? 256.0 - 256.0 / r.maxHp * r.hp : 0.0;
    if (lost < 0.0) lost = 0.0;
    if (lost > 256.0) lost = 256.0;
    const int red = (int)lost / 2;
    battle_solid(128, 136, 128, 7, 248, 232, 0, 151);
    battle_solid(256, 137, 2, 7, 0, 0, 0, 151);
    battle_solid(130, 143, 126, 1, 0, 0, 0, 151);
    if (red > 0) battle_solid(256 - red, 136, red, 7, 248, 0, 0, 150);

    sat_text_b("CLEAR TIME", 7, 20, SAT_PAL_CYAN);
    sat_time((unsigned int)r.frames, 28, 20, SAT_PAL_WHITE);

    sat_text_b("RANK", 7, 24, SAT_PAL_CYAN);
    sat_glyph_a(sat_glyph_index(battle_rank_letter(r.score), 1), 19, 24, sat_flicker());
    sat_score(r.score, 27, 24, SAT_PAL_WHITE);
}

// The ranking (RANKING 0x0602CA08). `place` is the new row (-1 for none);
// `cursor` the name letter being entered (-1 once it is done).
static void battle_draw_ranking(int character, int place, int cursor)
{
    static const char* const kOrd[10] = {
        " 1ST", " 2ND", " 3RD", " 4TH", " 5TH", " 6TH", " 7TH", " 8TH", " 9TH", "10TH"
    };
    sat_text_a("RANKING", 13, 1, SAT_PAL_WHITE);
    sat_text_b(character ? "JILL" : "CHRIS", 18, 3, sat_flicker());
    for (int i = 0; i < 10; i++) {
        const BattleRecord& rec = s_ranking[character][i];
        const int cy = 6 + 2 * i;
        const int letterK = sat_glyph_index(rec.letter, 1);
        if (i == place) {
            // The new record: yellow; its name is entered in place, the letter
            // being chosen flickering and the ones still to come cyan.
            sat_text_b(kOrd[i], 6, cy, SAT_PAL_YELLOW);
            sat_glyph_a(letterK, 13, cy, rec.letter == 'S' ? SAT_PAL_ORANGE : SAT_PAL_YELLOW);
            sat_score(rec.score, 17, cy, SAT_PAL_YELLOW);
            for (int k = 0; k < 3; k++) {
                char buf[2] = { rec.name[k], 0 };
                const int pal = cursor < 0 ? SAT_PAL_YELLOW
                              : k < cursor ? SAT_PAL_YELLOW
                              : k == cursor ? sat_flicker() : SAT_PAL_CYAN;
                sat_text_b(buf, 24 + k, cy, pal);
            }
            sat_time(rec.frames, 29, cy, SAT_PAL_YELLOW);
        } else if (place >= 0) {
            // Another row while a new one is placed (0x0602CB7E): grey, the
            // letter dim yellow, the name green.
            sat_text_b(kOrd[i], 6, cy, SAT_PAL_GREY);
            sat_glyph_a(letterK, 13, cy, rec.letter == 'S' ? SAT_PAL_ORANGE : SAT_PAL_DIM_YELLOW);
            sat_score(rec.score, 17, cy, SAT_PAL_GREY);
            sat_text_b(rec.name, 24, cy, SAT_PAL_GREEN);
            sat_time(rec.frames, 29, cy, SAT_PAL_GREY);
        } else {
            // Just the table (0x0602CADA): white, the letter yellow, names cyan.
            sat_text_b(kOrd[i], 6, cy, SAT_PAL_WHITE);
            sat_glyph_a(letterK, 13, cy, rec.letter == 'S' ? SAT_PAL_ORANGE : SAT_PAL_YELLOW);
            sat_score(rec.score, 17, cy, SAT_PAL_WHITE);
            sat_text_b(rec.name, 24, cy, SAT_PAL_CYAN);
            sat_time(rec.frames, 29, cy, SAT_PAL_WHITE);
        }
    }
}

// The name-entry alphabet. PORT DECISION: font B's own set, letters then the
// space, '.' and '-'.
static const char kNameAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ .-";

// RANKING.PRG's entry, shared by the finished run and the death: the backdrop,
// the art and NAME.CDP's music.
static void battle_results_begin(void)
{
    // The backdrop replaces the orbit's black: the screen mode goes back to the
    // one that draws the display image.
    g_main_state_flags &= ~MSF_SCREEN_MODE_MASK;
    if (!battle_results_load_art()) {
        // Without the migrated BATTLE.BIN / WAPON3.BIN there is nothing to draw
        // the Saturn's screens with; the run still records its result.
        dbg_printf("[battle] results art missing - re-run the asset migrator's Saturn tab\n");
    }
    // State 0 of 0x0602D398: the logo starts square to the camera.
    battle_results_load_logo();
    s_logoFrame = 0;

    // RANKING's music, /SND/NAME.CDP, from the score screen on.
    bgm_play_group_channel(SATURN_BGM_GROUP_RANKING, 0);
    s_resultsFrame = 0;
}

// RANKING's tail, after the table (RANKING.PRG states 4-11, read from the
// module): two two-choice prompts in a row, each a cursor over YES / NO where
// confirm takes state + cursor + 1 (YES = 0, NO = 1) and the A button is "no".
//   state 5  "Do you want to save?"               - YES runs state 6, which copies
//            the ranking tables to the save mirror (0x002FFF00); NO skips to 7.
//            It is shown when the backup-RAM probe (0x0602FEA0) says the device
//            can be written; the port always has one.
//   state 9  "Do you want to continue this game?" - YES goes to state 10, which
//            leaves for BU_LOAD and so back into GAME5 (module 10 -> 8 in battle
//            mode); NO goes to state 11, which leaves for the title. The exit
//            code is stored minus one as the next module (0x0602E764).
// A finished run and a death both go through here. The answer to the second
// prompt is the state's exit: saturn_ranking_state chains on it directly.

// The question box over the ranking table. Returns 1 for YES. The Saturn's
// prompt art is SAVE2.BIN (128x80, "Do you want to save?" / "Do you want to
// continue this game?" with YES and NO); the port spells it in font B, which
// has no question mark.
//
// The Japanese release has no English to spell, so it draws its own art
// (BATTLE.BIN +0x31EB0, loaded under the fonts at SAT_PROMPT_V): row `which`
// is the question - 0 セーブしますか?, 1 続けて遊びますか? - and rows 2 / 3
// YES / NO, whose letters span u 38..81 and 46..74 of their 128-wide rows.
// PORT LAYOUT: they go in the same box and the same colours as the English
// prompt; the Saturn's own VDP1 placement (0x0602DA80, inside a 152x96 window
// sprite) is not reproduced.
static int battle_yes_no(int character, int place, const char* question, int which)
{
    int yes = 1;
    const int qx = (40 - (int)strlen(question)) / 2;
    for (;;) {
        if (s_resultsArt) {
            battle_draw_ranking(character, place, -1);
            battle_solid(48, 200, 224, 40, 0, 0, 60, 120);
            const int yesPal = yes ? sat_flicker() : SAT_PAL_GREY;
            const int noPal = yes ? SAT_PAL_GREY : sat_flicker();
            if (s_resultsJpn) {
                sat_draw(0, 0, SAT_PROMPT_V + 16 * which, 128, 16, 96, 203,
                         kSatPalTint[SAT_PAL_WHITE]);
                sat_draw(0, 38, SAT_PROMPT_V + 32, 44, 16, 110, 221, kSatPalTint[yesPal]);
                sat_draw(0, 46, SAT_PROMPT_V + 48, 29, 16, 170, 221, kSatPalTint[noPal]);
            } else {
                sat_text_b(question, qx, 26, SAT_PAL_WHITE);
                sat_text_b("YES", 15, 28, yesPal);
                sat_text_b("NO", 22, 28, noPal);
            }
        }
        const unsigned int pad = battle_results_frame();
        if (pad & (BATTLE_PAD_LEFT | BATTLE_PAD_RIGHT | BATTLE_PAD_UP | BATTLE_PAD_DOWN)) yes = !yes;
        if (pad & BATTLE_PAD_CANCEL) return 0;
        if (pad & BATTLE_PAD_CONFIRM) return yes;
    }
}

// `place` is the row a new record took, or -1 when the table is shown as it is.
// Returns 1 when the player chose to continue.
static int battle_results_epilogue(int character, int place)
{
    if (place < 0) {
        // State 3: the table, until a button is pressed.
        for (;;) {
            if (s_resultsArt) battle_draw_ranking(character, -1, -1);
            if (battle_results_frame() & (BATTLE_PAD_CONFIRM | BATTLE_PAD_CANCEL)) break;
        }
    }
    if (battle_yes_no(character, place, "DO YOU WANT TO SAVE", 0)) battle_ranking_save();
    return battle_yes_no(character, place, "DO YOU WANT TO CONTINUE", 1);
}

// Dying in a run. Read from GAME5.PRG and RANKING.PRG:
//   * GAME5's main loop tests bit 24 of 0x002F3000 (the player-dead flag) at
//     0x0602BB68 and branches to 0x0602B764, which calls the Saturn's own death
//     screen (0x0602F5A0 - it loads c:/horr/psx/data/died.pix/.pal) and rejoins
//     the exit path at 0x0602BBD0. The death screen is therefore shown as in the
//     campaign, and the port keeps display_die_screen() for it.
//   * The exit path then sees the battle bit (bit 31 of g_SystemFlags) at
//     0x0602BD28 and goes to RANKING (module 13, 0x0602B480) rather than to the
//     title, handing it the player's HP as the pending result
//     (`0x002FFFA4 = word at 0x002F5124 + 0x88`, 0x0602B49E).
//   * RANKING tests that value with `cmp/pz` at 0x0602D700: non-negative goes on
//     to the score screen and name entry, NEGATIVE - a dead player - goes straight
//     to state 3, the ranking table, with no score or entry.
// So a death shows the table alone, then the two prompts every run ends with
// (battle_results_epilogue). The table is the dead character's.
static int battle_results_dead(int jill)
{
    battle_ranking_load();
    return battle_results_epilogue(jill, -1);
}

// A finished run: the score screen, the name entry when the score places, then
// the prompts.
//
// PORT DECISION: with the port's debug features on ([Debug] EnableDebug, the
// F1 menu's arena warp and cheats among them) the score is shown but never
// enters the ranking - no placement, no name entry, and the table is shown as
// it stands. battle_ranking_place() shifts the rows as it places, so it is not
// called at all.
static int battle_results_finished(const BattleResult& r, int jill)
{
    const int unranked = (g_debugFeaturesEnabled != 0);

    // The score screen, until a button is pressed.
    for (;;) {
        if (s_resultsArt) {
            battle_draw_score(r, jill);
            if (unranked) sat_text_b("DEBUG MODE - NOT RANKED", 8, 27, SAT_PAL_ORANGE);
        }
        if (battle_results_frame() & (BATTLE_PAD_CONFIRM | BATTLE_PAD_CANCEL)) break;
    }

    battle_ranking_load();
    const int place = unranked ? -1 : battle_ranking_place(jill, r.score);
    if (place < 0) return battle_results_epilogue(jill, -1);

    BattleRecord& rec = s_ranking[jill][place];
    rec.score = (unsigned short)r.score;
    rec.letter = battle_rank_letter(r.score);
    memcpy(rec.name, "AAA", 4);          // name entry starts "AAA"
    rec.frames = (unsigned int)r.frames;

    int cursor = 0;
    int letter[3] = { 0, 0, 0 };
    while (cursor < 3) {
        if (s_resultsArt) battle_draw_ranking(jill, place, cursor);
        const unsigned int pad = battle_results_frame();
        const int n = (int)sizeof(kNameAlphabet) - 1;
        if (pad & BATTLE_PAD_UP)   letter[cursor] = (letter[cursor] + 1) % n;
        if (pad & BATTLE_PAD_DOWN) letter[cursor] = (letter[cursor] + n - 1) % n;
        rec.name[cursor] = kNameAlphabet[letter[cursor]];
        if (pad & (BATTLE_PAD_CONFIRM | BATTLE_PAD_RIGHT)) cursor++;
        if ((pad & (BATTLE_PAD_CANCEL | BATTLE_PAD_LEFT)) && cursor > 0) cursor--;
    }

    return battle_results_epilogue(jill, place);
}

// What GAME5 hands RANKING, taken by saturn_battle_close_run() while the run's
// state is still there: the character, whether the run was finished, and the
// result the score screen shows. The Saturn passes it in 0x002FFFA4-0x002FFFAE,
// which survive the module swap; here it survives game_start's cleanup.
enum { BATTLE_OUTCOME_NONE, BATTLE_OUTCOME_FINISHED, BATTLE_OUTCOME_DEAD };
static int          s_pendingOutcome = BATTLE_OUTCOME_NONE;
static int          s_pendingJill = 0;
static BattleResult s_pendingResult;

int saturn_battle_close_run(void)
{
    s_pendingOutcome = BATTLE_OUTCOME_NONE;
    saturn_battle_clock_run(0);           // GAME5 0x0602D9F0
    if (g_bSaturnMode && g_saturnBattleActive) {
        s_pendingJill = ((g_playerEntity.id & 3) == CHAR_JILL);
        if (s_battleFinished) {
            battle_compute_result(&s_pendingResult);
            s_pendingOutcome = BATTLE_OUTCOME_FINISHED;
        } else {
            // game_loop only leaves a run on its end (s_battleFinished) or on a
            // death; GAME5's exit path likewise goes to RANKING whenever the
            // battle bit is up, with the negative HP as the pending result.
            s_pendingOutcome = BATTLE_OUTCOME_DEAD;
        }
    }
    saturn_battle_end_run();
    return s_pendingOutcome != BATTLE_OUTCOME_NONE;
}

// RANKING.PRG (main 0x0602E530), as a game state. game_start chains it when a
// Battle Game run ends; it chains the title (NO) or the Battle Game's load
// screen (YES), the two exits RANKING stores at 0x0602E764.
void saturn_ranking_state(void)
{
    const int outcome = s_pendingOutcome;
    s_pendingOutcome = BATTLE_OUTCOME_NONE;

    // No fade rectangle: g_fading_state >= 0 makes main_loop draw one (0x7FFF is
    // fully faded out - the title screen uses it to end on black), and -1 is "no
    // fade", which is what the death fade finished on to reach die_state(). Set
    // again here so the ranking cannot inherit a stray counter.
    g_fading_state = (short)-1;
    g_fading_counter = 0;

    battle_results_begin();
    const int again = (outcome == BATTLE_OUTCOME_FINISHED)
                    ? battle_results_finished(s_pendingResult, s_pendingJill)
                    : battle_results_dead(s_pendingJill);

    if (!again) {
        Task_chain((void*)title_state);
        return;
    }

    // YES: RANKING -> BU_LOAD. The load screen has always been entered from
    // title_state, and expects what that state's entry (0x00430470) leaves
    // behind: a 320x240 menu screen, no textures but its own, the scene render
    // parameter, and the title's sound bank in g_SfxBanks - the screen plays
    // its cursor and cancel sounds from bank 1, slots 13-15 (see
    // LoadSaveGameState's sfxBank). Without the title in between, set it up
    // here. The results pages in slots 12/13 go with clear_textures().
    g_main_state_flags &= ~MSF_INTENSITY_RAMP;
    g_PlayerPadHeldPrev = 0;
    g_PlayerPadHeld = 0;
    g_RawPadHeld = 0;
    g_PlayerPadPressed = 0;
    g_playingGameFlag = 0;
    g_menu_choice_id = 0;

    setMenuScreenOffset(320, 240, 0, 0, 0);
    CenterScreenOrigin();
    clear_textures();
    set_scene_render_param(0xc0);
    sounds_reset();

    g_loadDataDestPointer = g_DataBuffer;
    LoadSoundBank(GetAssetVersion() != 0 ? BANK_BIO : BANK_TITLE, g_DataBuffer);

    g_fading_state = (short)-1;
    g_SpecialRoomLightState = 0xFFFF;

    // No title picture: the screen is left black until the load screen draws.
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
    setMenuScreenOffset(320, 240, 0, 0, 1);
    Task_sleep(1);

    Task_chain((void*)saturn_battle_load_state);
}

// TITLE (mode 2) / RANKING (YES) -> BU_LOAD -> GAME5. The middle step is NOT
// optional: BU_LOAD is the load screen, and the save the player picks there is
// what the run starts from. battle_run_init (GAME5 0x06088698) takes no
// arguments and reads the character block of the save that was just loaded, so
// the run plays as whoever that save was - which the Saturn allows, and which is
// also how the load screen sets MSF_CONTINUE_GAME and restores the card this
// port then starts from. This is the title's LOAD GAME path (title_state case 2)
// with battle mode armed.
//
// Battle mode is armed only AFTER the screen returns a save. Its EXIT / cancel
// chains title_state from inside LoadSaveGameState and never comes back here,
// so a run abandoned at the load screen leaves nothing armed for the next LOAD
// GAME to inherit. Nothing in the load screen reads the flag.
void saturn_battle_load_state(void)
{
    g_main_state_flags2 &= ~MSF2_ATTRACT_DEMO;

    // g_bGameActive = 2 is what the title menu's confirm sets
    // (update_title_options); the frame pacing and the load menu's cursor and
    // flicker timing follow it, and at 0 they run at the faster non-game rate.
    // Both ways in already have it at 2; set here so neither has to.
    g_bGameActive = 2;

    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
    LoadSaveGameState(1, 0x80180000, 0, 1, 0);
    g_loadSaveStateFlag = 0;
    Game_timer = g_gameTimerSnapshot;
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;

    saturn_battle_start_run();
    Task_chain((void*)game_start);
}

// The per-frame machine. Called from the game loop.
void saturn_battle_update(void)
{
    if (!g_bSaturnMode || !g_saturnBattleActive) return;

    // The room can change without this machine moving it - a door out of a rest
    // room does - so the per-arena half of the block is re-armed first, every
    // frame. See battle_sync_arena().
    battle_sync_arena();

    // GAME5 0x0602D8C6 / 0x0602D950 / 0x0602D9F0: the game loop's death test
    // (HP negative, a signed halfword) stops the clock on the frame it first
    // passes, not when the death sequence is over.
    if (g_playerEntity.health < 0) s_battle.flags &= ~BB_CLOCK;

    if (s_battleFinished) return;         // results shown; game_loop is leaving
    if (s_battle.state == 2) return;      // the door transition owns the run

    if (s_battle.state == 0) {            // fighting
        battle_scan_weapons();
        if (battle_usable_ammo() != 0) {
            s_battle.flags |= BB_CAN_FIGHT;
            s_battle.noAmmoFrames = 0;
        } else {
            // Nothing to shoot: the Saturn still allows fighting for 0xB3
            // frames, then leaves the bit clear.
            if (s_battle.noAmmoFrames < BATTLE_NO_AMMO_GRACE) {
                s_battle.noAmmoFrames++;
            } else {
                s_battle.flags &= ~BB_CAN_FIGHT;
            }
        }

        s_battle.enemiesLeft = battle_enemies_left();

        // Cleared: a fight room, nothing left standing, player alive.
        if ((s_battle.flags & BB_FIGHT_ROOM) && s_battle.enemiesLeft == 0 &&
            g_playerEntity.health >= 0) {
            s_battle.state = 1;
            s_battle.timer = 0;
            s_battle.flags |= BB_ADVANCE;
            s_battle.flags &= ~BB_CLOCK;  // the clock holds until the next room is in
            // HP is pinned up to at least 1, saved, and shown full for the
            // pause; the real value comes back afterwards.
            if (g_playerEntity.health <= 0) g_playerEntity.health = 1;
            s_battleSavedHp = g_playerEntity.health;
            g_playerEntity.health = 1000;
        }
    } else if (s_battle.state == 1) {     // cleared: hold full, then move on
        // GAME5 0x06088B1E-0x06088B26: HP held at the maximum and bit 8 of
        // 0x002FAB02 - the Saturn's g_message_flags - cleared EVERY frame of the
        // pause. That bit is what game_loop tests before it passes the pad to
        // the player and before it lets START open a menu, so the player
        // stands still and cannot open the inventory until the next room is
        // loaded; the door transition's menu path restores the flags.
        g_message_flags &= ~0x100;
        g_playerEntity.health = 1000;
        s_battle.timer++;
        if (s_battle.timer > BATTLE_CLEAR_PAUSE_FRAMES) {
            g_playerEntity.health = s_battleSavedHp;
            if (saturn_battle_arena_index() >= SATURN_BATTLE_ARENA_COUNT) {
                battle_begin_finale();    // after the Tyrant the run is over
            } else {
                battle_advance_room();
            }
        }
    } else if (s_battle.state == 3) {     // the background fades out
        g_message_flags &= ~0x100;
        battle_finale_fade();
    } else if (s_battle.state == 4) {     // the orbit around the winner
        g_message_flags &= ~0x100;
        // The orbit is over: GAME5 raises bits 0-1 of g_SystemFlags and its main
        // returns, and RANKING.PRG takes over. Here game_loop returns and
        // game_start chains saturn_ranking_state.
        if (battle_finale_step()) {
            s_battleFinished = 1;
        }
    }
}

// The run clock, in frames (60 to the second). §4f: the score's time bonus is
// against 720 s for Chris and 420 s for Jill, so the Saturn runs a clock for the
// whole attempt rather than per arena.
int saturn_battle_run_frames(void)
{
    return s_battleRunFrames;
}

void saturn_battle_draw_hud(void)
{
    if (!g_bSaturnMode || !g_saturnBattleActive) return;
    if (s_battle.state >= 3) return;      // the end sequence draws no HUD

    const int frames = saturn_battle_run_frames();
    // The Saturn shows m:ss; this port shows mm' ss"cc - minutes, seconds and
    // hundredths, the last rounded from the frame's millisecond - derived from
    // the same 60-per-second frame count so the HUD, the score and the results
    // all read one clock.
    //
    // PrintText8x14 takes the game font's own codes, not ASCII: the prime and
    // double prime sit where ASCII has '^' and '=' (cells 94 and 61 of both
    // fontus.tim and the JPN FONT.TIM), and digits are at their ASCII codes.
    //
    // Eight characters, no space after the prime: the glyphs carry their own
    // side bearing, so a space reads as a wide gap. The clock is centred on
    // the Saturn's five-character field, 0x84 + 2.5 glyphs of 8 = x 152 - the
    // same screen in both releases. The JPN font's glyphs are 14 wide, so its
    // start is 152 - 4 * 14 = 96 (the USA one 152 - 4 * 8 = 120). 0x68 is NOT
    // a JPN start for this field: it is Biohazard.exe's start for its own
    // eight-character countdown (GameLoop.cpp), and offsetting it by the USA
    // field's 1.5 glyphs left the JPN clock 13 pixels left of centre.
    const char kPrime = 94, kDoublePrime = 61;
    const int clockGlyphW = (GetAssetVersion() != 0) ? 14 : 8;
    const int clockMidX = 0x84 + 5 * 8 / 2;
    const int centis = ((frames % 60) * 100 + 30) / 60;       // 0..98, rounded
    sprintf(PRINT_TEXT_BUFFER, "%02d%c%02d%c%02d", frames / 3600, kPrime,
            (frames / 60) % 60, kDoublePrime, centis);
    PrintText8x14((short)(clockMidX - 8 * clockGlyphW / 2), 0x20, 1, 0);

    if (s_battle.state == 1) {
        const int glyphWidth = (GetAssetVersion() != 0) ? 14 : 8;
        const int textWidth = ((int)sizeof(s_stageClearedHudText) - 1) * glyphWidth;
        strcpy(PRINT_TEXT_BUFFER, s_stageClearedHudText);
        PrintText8x14((short)((320 - textWidth) / 2), 112, 0, 0);
    }
}

// The run clock. GAME5 keeps it in its per-frame interrupt routine (0x06036508,
// installed as a callback at 0x0602B2B2 / 0x0602B4C2 / 0x0602B858 / 0x0602C868),
// right after the play timer (0x060AD01C, +1 capped at 0x014996C4):
//
//   06036592  battle bit 31 of block 0x060A67D0 + 0xA50 clear -> skip
//   0603659E  0x060A67A0 bit 0 clear                           -> skip
//   060365A8  0x002F3000 & 0xFF00, flag 0x4000, +0xA9A, n % 60 -> only decide
//             whether the m:ss text is redrawn; every one of them falls
//             through to
//   0603662A  0x002FFFA0 += 1, capped at 0x57E04
//
// So it is not a gameplay counter: it runs at the display rate whatever the
// game is doing - inventory and item box included - and the only thing that
// stops it is bit 0 of the battle block, whose writers are
//
//   0602CF3C  set    the end of the game's start-up: the first room loaded
//                    AND the opening message closed (the wait at 0x0602CF0A)
//   0602EAC6  set    the end of the room transition routine (0x0602DBBC)
//   0602DBDC  clear  the start of that routine
//   0602D9F0  clear  the player's death: 0x0602D9C8, which the game loop
//                    calls on the first frame HP is negative (0x0602D8C6 sets
//                    bit 24 of 0x002F3000, 0x0602D950 calls it at step 0)
//   060888C6  clear  the arena is cleared (state 0 -> 1), after the banner
//
// which makes the clock stop at the moment the last enemy falls and stay
// stopped through the 120-frame pause and the load of the next arena.
//
// Port: main_loop() is the one routine every frame goes through, whichever
// task owns the screen, so the tick lives there, and it counts real time
// rather than calls - main_loop() runs at 30 a second in play and faster in
// the menus, the vertical blank at 60 throughout.
void saturn_battle_clock_run(int run)
{
    if (!g_bSaturnMode || !g_saturnBattleActive) return;
    if (run) s_battle.flags |= BB_CLOCK;
    else     s_battle.flags &= ~BB_CLOCK;
}

void saturn_battle_tick_run(void)
{
    const DWORD now = plat_time_ms();
    const DWORD elapsed = now - s_battleClockLastMs;
    s_battleClockLastMs = now;

    if (!g_bSaturnMode || !g_saturnBattleActive) return;
    if ((s_battle.flags & BB_CLOCK) == 0) return;
    if (elapsed > BATTLE_CLOCK_MAX_GAP_MS) return;

    s_battleClockRemainder += elapsed * BATTLE_RUN_FRAMES_PER_SEC;
    s_battleRunFrames += (int)(s_battleClockRemainder / 1000);
    s_battleClockRemainder %= 1000;
    if (s_battleRunFrames > BATTLE_MAX_RUN_FRAMES) s_battleRunFrames = BATTLE_MAX_RUN_FRAMES;
}

void saturn_battle_end_run(void)
{
    g_saturnBattleActive = 0;
    s_battle.flags = 0;
    s_battle.state = 0;
    s_battle.enemiesLeft = 0;
    s_battle.timer = 0;
    s_battle.noAmmoFrames = 0;
    s_battleRunFrames = 0;
    s_battleFinished = 0;
    // When the ranking follows (s_pendingOutcome set by close_run), it still
    // needs the Saturn overlay; the title screen restores the mode afterwards.
    if (s_pendingOutcome == BATTLE_OUTCOME_NONE) saturn_battle_restore_mode();
}

// ---------------------------------------------------------------------------
// Title art: the third menu row
// ---------------------------------------------------------------------------
// data\FONTUS.TIM, measured: a 16-byte header, a CLUT through 0x40, then a
// 256x256 4bpp image at 0x40 with a 128-byte row stride - 32,768 bytes, which is
// exactly the file's 32,832 minus the 64. The glyph grid on it is the one the
// port's own text renderers already use (PrintText8x14, ending_draw_text):
// texU = (c % 18) * 8, texV = (c / 18) * 14, 8 px cells in an 18-column grid.
//
// Rendered as 4bpp at that offset the ASCII block comes out as legible glyphs
// with 0 = background, 1 = the letter and 2 = its shadow, which is all the three
// colour page below needs.
#define SATURN_FONT_PIXELS    0x40
#define SATURN_FONT_ROW_BYTES 128
#define SATURN_FONT_CELL_W    8
#define SATURN_FONT_CELL_H    14
#define SATURN_FONT_COLS      18

// The page: 256 x 64, 4bpp, one CLUT of 16 entries at clutY 0x1E0 - the row the
// title's UpdateTitleTextSprite selects and the row dc_title_build_page() writes,
// so the two modes' pages are interchangeable.
#define SATURN_TEXT_PAGE_W    256
#define SATURN_TEXT_PAGE_H    64
#define SATURN_TEXT_CLUT_Y    0x1E0

unsigned int saturn_title_build_text_page(const unsigned char* fontTim,
                                          const char* text,
                                          int destX, int destY,
                                          unsigned char* out)
{
    if (fontTim == NULL || text == NULL || out == NULL) return 0;
    // The blit works in whole bytes of two pixels, and a glyph is 4 bytes wide.
    if ((destX & 1) != 0 || (destY < 0) ||
        (destY + SATURN_FONT_CELL_H) > SATURN_TEXT_PAGE_H) {
        return 0;
    }
    const int advance = SATURN_FONT_CELL_W;
    const int textW = (int)strlen(text) * advance;
    if (destX + textW > SATURN_TEXT_PAGE_W) return 0;

    const unsigned int pixBytes = (unsigned int)(SATURN_TEXT_PAGE_W * SATURN_TEXT_PAGE_H / 2);
    const unsigned int headerBytes = 4 + 4 + 4 + 4 + 2 + 2 + 2 + 2 + 32
                                   + 4 + 2 + 2 + 2 + 2;
    unsigned char* p = out;
    memset(out, 0, headerBytes + pixBytes);

    *(unsigned int*)p = 0x10;                     p += 4;    // TIM magic
    *(unsigned int*)p = 0x08;                     p += 4;    // 4bpp + CLUT
    *(unsigned int*)p = 12 + 32;                  p += 4;    // CLUT block length
    *(unsigned short*)p = 0;                      p += 2;    // clutX
    *(unsigned short*)p = SATURN_TEXT_CLUT_Y;     p += 2;    // clutY
    *(unsigned short*)p = 16;                     p += 2;    // CLUT length
    *(unsigned short*)p = 1;                      p += 2;    // one CLUT
    // 0 = transparent, 1 = white, 2 = the dark red the title's own rows shadow
    // with. 15-bit BGR, little-endian halfwords, as a PS1 CLUT stores them.
    for (int i = 0; i < 16; i++) {
        unsigned short c = 0x0000;
        if (i == 1) c = 0x7FFF;
        if (i == 2) c = 0x1800;
        *(unsigned short*)p = c;                  p += 2;
    }

    *(unsigned int*)p = 12 + pixBytes;            p += 4;    // pixel block length
    *(unsigned short*)p = 0;                      p += 2;    // x
    *(unsigned short*)p = 0;                      p += 2;    // y
    *(unsigned short*)p = SATURN_TEXT_PAGE_W / 4; p += 2;    // 16-bit words per row
    *(unsigned short*)p = SATURN_TEXT_PAGE_H;     p += 2;
    unsigned char* pixels = p;

    for (int i = 0; text[i] != '\0'; i++) {
        const int cellCol = (int)((unsigned char)text[i]) % SATURN_FONT_COLS;
        const int cellRow = (int)((unsigned char)text[i]) / SATURN_FONT_COLS;
        for (int py = 0; py < SATURN_FONT_CELL_H; py++) {
            // Source: the glyph cell, one 4-byte run (8 px) per row.
            const unsigned char* src = fontTim + SATURN_FONT_PIXELS
                                     + ((cellRow * SATURN_FONT_CELL_H + py)
                                        * SATURN_FONT_ROW_BYTES)
                                     + cellCol * (SATURN_FONT_CELL_W / 2);
            unsigned char* dst = pixels + ((destY + py) * SATURN_FONT_ROW_BYTES)
                               + (destX + i * advance) / 2;
            memcpy(dst, src, SATURN_FONT_CELL_W / 2);
        }
    }
    return headerBytes + pixBytes;
}

// SaturnBattle.h - the Sega Saturn's Battle Game.
//
// The Battle Game is the Saturn's second game mode: a fixed 15-arena run through
// STAGE8 with a preset loadout, scored on the ammo you did not spend. Everything
// about it is decoded in docs/SATURN_VERSION_ANALYSIS.md 4d-4f; this header is the
// port's entry point into it, and the state the title screen, the room loaders
// and the ending all share.
//
// WHAT IS HERE NOW
//   * the unlock flag - save byte 0xBF's place in this port, raised by any
//     ending, plus the [Game] BattleGame config key that forces it on,
//   * the third title item itself and the art for its row (built from the
//     port's own font, see saturn_title_build_text_page),
//   * starting a run: STAGE8 / ROOM8010, an empty inventory and the Saturn's
//     per-character loadout in the ITEM BOX, HP at the maximum,
//   * the arena -> campaign-room table, which is what makes an arena's
//     background and sound banks load,
//   * the 15-arena sequence, room-clear pause and health carry-over; the
//     arena warp is a door transition built from GAME5's virtual door records,
//   * the elapsed-time counter and centered "STAGE CLEARED" HUD banner,
//   * its music: the arenas' BGM groups and states from GAME5's own tables,
//     the Saturn-only group 0x40 (/SND/BATTLE.CDP, rendered into SATURN/Sound
//     by the asset migrator) and the end-of-run fanfare,
//   * arena 12's Zombie Wesker (em1017 on a stock zombie, behaviour 9) and
//     arena 15's gold, 700-HP Tyrant,
//   * an arena jump for the F1 debug menu (saturn_battle_debug_warp below),
//     which lists STAGE8's 15 arenas in its ROOM CHANGE screen mid-run,
//   * the end of the run: the black-background orbit, the fanfare, and the
//     results - score, ranking with name entry, saved records.
//
//
// Nothing here runs unless g_bSaturnMode is on, and the arena redirects are all
// keyed on g_saturnBattleActive, which only the title's Battle item sets. OG and
// DC reach it only with [Game] BattleGame=1 and the SATURN/ assets imported, and
// then by switching to Saturn mode for the run (saturn_battle_restore_mode) - see
// docs/SATURN_PORT.md.
#pragma once

// The Battle Game's stage. 0-indexed, so 7 == STAGE8 == the stage byte the
// Saturn holds at 0x002F8660 while a run is in progress (SATURN_VERSION_ANALYSIS.md
// 4d, "the stage byte is 0-indexed"). This is the same id the Director's Cut's
// arrange stages start at (STAGE_ARRANGE_FIRST), which is why the arrange
// machinery folds it correctly on its own: get_stage_id() maps 7 -> 0, so every
// stage-indexed table (BGM state, room effect sprites, room sprites, camera
// light index, voice offsets) reads a base row instead of running off the end.
#define SATURN_BATTLE_STAGE_ID    7

// The arena rooms. The room byte IS the arena number: arena 1 is room 0x01 and
// arena 15 is room 0x0F, and the sequence is "room + 1" all the way (GAME5
// 0x0602DE3E on 0x002F8661). That is also why the files are named the way they
// are - the port's LoadRoomRdt builds "room<stage><roomHi><roomLo><variant>",
// so arena 1 is ROOM8010 (stage 8, room 0x01, variant 0), arena 2 is ROOM8020,
// and arena 15 is ROOM80F0. The 15 ROOM80?0 files in SATURN/Stage8 are one per
// arena, which is the check that pins this reading; taking the two middle digits
// of the filename as one room byte asks for ROOM8100, which is not on the disc
// in any tree.
#define SATURN_BATTLE_FIRST_ROOM  1
#define SATURN_BATTLE_LAST_ROOM   15
#define SATURN_BATTLE_ARENA_COUNT 15

// g_titleSelectionId for the third title item. The USA build uses 1 = NEW GAME,
// 2 = LOAD GAME, 0 = PRESS ANY BUTTON, and the Director's Cut reuses 3..6 for its
// difficulty submenu - the modes are exclusive, so 3 is free here.
#define SATURN_TITLE_BATTLE_ITEM   3

// Battle mode. The Saturn sets bit 31 of g_SystemFlags (0x060A0940 + 0xA50) from
// the module mode byte and tests it in LoadRoomRdt, LoadRoomBss,
// SetupRoomLighting, LoadRoomSeBank and LoadRoomPcmBank. One flag stands in for
// the whole chain, set only while a Battle run is in progress.
extern unsigned char g_saturnBattleActive;

// The arena being fought, 1..15, or 0 when no run is in progress. NOT a stored
// counter: the room byte IS the arena number (the Saturn holds it at
// g_battle_block->arena_index, GAME5 0x060A67A0 + 6, but every table that
// consumes it - the arena rooms, the five-byte virtual doors, the per-room
// flags, the spawn records - is indexed by that same room byte, and arena 1's
// own door is dest=0x02). Deriving it from g_roomId is both the faithful
// reading and the only one that cannot go stale: a stored copy has to be
// advanced by the room transition, and a transition that missed it leaves every
// later arena on arena 1's background.
int saturn_battle_arena_index(void);

// True once any ending has been seen, or [Game] BattleGame=1 is set in
// config.ini. The Saturn keeps the flag in save byte 0xBF and reads it at the
// title's press-start screen; here it is a plain flag mirrored into the card
// byte g_SaturnBattleUnlocked, so it survives a save/load round trip. See
// saturn_battle_sync_from_card.
int saturn_battle_is_unlocked(void);

// True when the SATURN/ overlay carries the 15 arena RDTs (SATURN/Stage8/
// ROOM80?0.RDT), i.e. the asset migrator's Saturn tab has been run. Probed once.
int saturn_battle_assets_present(void);

// Whether the title screen offers the Battle Game: unlocked (an ending in Saturn
// mode, or [Game] BattleGame=1 in any mode) AND the Saturn assets imported. This
// is what makes the item available in OG and DC; the title uses it instead of
// testing g_bSaturnMode.
int saturn_battle_offered(void);

// OG/DC borrow Saturn mode for the length of a run (g_GameMode and the asset
// overlay are one switch, so there is no other way to reach the Saturn's
// arenas, rules and enemy tables). saturn_battle_start_run() switches;
// this puts the configured mode back. Called when a run ends without a ranking,
// and by title_state, which the ranking and the load screen's EXIT both reach.
// A no-op when nothing was switched.
void saturn_battle_restore_mode(void);

// True during a run started from a DC ADVANCED save: the player keeps the DC's
// outfit logic (arrange outfit, EM1032/1033 from the DC overlay) even though the
// run is in Saturn mode. Used by LoadEntityEMD and room_set.
int saturn_battle_dc_outfit(void);

// True while a Battle Game run started from OG or DC is borrowing Saturn mode
// (battle_enter_saturn_mode), i.e. the Saturn is not the mode config.ini chose.
// Anything the Saturn changes about the CAMPAIGN (not the Battle Game's own
// content) must stay off during such a run - the player is still in the OG/DC
// save, with its outfit.
int saturn_mode_is_borrowed(void);

// Raise the unlock and write it into the card, so the next save carries it. The
// Saturn does this at the top of ENDING.PRG (0x0602DC28): any ending counts.
void saturn_battle_unlock(void);

// Pick the unlock up from the card after a save has been restored. A no-op in
// every mode but Saturn's.
void saturn_battle_sync_from_card(void);

// Re-raise the unlock after a NEW GAME has reset the card from data\bio_card.dat
// (GameStart.cpp's 1052-byte memcpy). The Saturn has no such reset - its flag is
// in the save file and the save is rewritten - so without this a fresh new game
// would take the item away again. Call it right where dc_apply_mode_flags() runs.
void saturn_battle_preserve(void);

// Start a run: battle mode on. saturn_battle_load_state() calls it once the load
// screen has returned a save, and chains game_start.
//
// This sets ONLY the flags. The stage and the room are set by
// saturn_battle_begin_run() below, because game_start rewrites the whole card
// from data\bio_card.dat - g_stageId and g_roomId live in it, so anything set
// here would be discarded.
void saturn_battle_start_run(void);

// The rest of the run start, which has to happen inside game_start once the card
// reset and SetInitialItems() are behind us: STAGE8 / ROOM8010, the Saturn's
// per-character loadout (GAME5 battle_run_init 0x06088698) and the arena's
// spawn point. Call it from game_start in the same place dc_apply_mode_flags()
// runs.
void saturn_battle_begin_run(void);

// The per-frame arena sequence (GAME5 0x0608883C): "enemies remaining" is
// derived from g_enemy_count (slot count minus the slots whose health has gone
// negative - battle_enemies_left(), the Saturn's own pass at 0x06055474, NOT the
// raw counter, which never drops and would softlock the run), a fight room
// clears on it reaching zero, the clear holds for 120 frames at full HP, HP then
// carries over to the next arena, and the run ends after arena 15. Called from
// the game loop beside the other Saturn hooks.
void saturn_battle_update(void);

// The run clock, in frames (60 to the second), and its per-frame tick. The
// score's time bonus is against 720 s for Chris and 420 s for Jill, so this is
// for the whole attempt and not per arena.
//
// The tick is main_loop()'s, not the game loop's: the Saturn counts in its
// per-frame interrupt routine (GAME5 0x06036508), so the clock keeps going in
// the inventory and the item box. What stops it is bit 0 of the battle block,
// which saturn_battle_clock_run() writes for the callers outside the Battle
// code: 1 once a room is loaded (game_loop's entry, after the opening message
// has closed, and the end of room_transition_load), 0 when a room transition
// starts. The arena clear, the player's death and the end of the run lower it
// from inside. No-ops outside a run.
int  saturn_battle_run_frames(void);
void saturn_battle_tick_run(void);
void saturn_battle_clock_run(int run);
// Draw the run clock and, during state 1, the clear banner. Called once per
// gameplay frame after saturn_battle_update() so a new clear appears immediately.
void saturn_battle_draw_hud(void);

// Clear battle mode at the end of a run, whatever ended it. The arena redirect
// in load_room_bg and Room_LoadEnemySoundBanks is keyed on the flag alone, so
// leaving it set would put the next campaign game in arena 1's room.
// saturn_battle_close_run() calls it when game_loop() returns; the debug menu
// calls it when it leaves a run for a campaign room.
void saturn_battle_end_run(void);

// The arena -> campaign room table (GAME1 0x06092B5C, `g_BattleArenaRoomTable`):
// 15 big-endian u16 of (0-based stage << 8) | room, indexed by the arena number,
// entry 0 unused. Every arena's cameras are a copy of one campaign room's - all
// fifteen were matched camera by camera on the +0x08..+0x2C camera fields - and
// the game carries exactly this table, so the background, the lighting and the
// SE/PCM banks an arena uses are the campaign room's.
//
// Returns 1 and writes the pair when battle mode is on and the arena is in range;
// returns 0 otherwise, leaving the outputs untouched so callers can keep using
// g_stageId / g_roomId.
int saturn_battle_asset_room(unsigned char* stage, unsigned char* room);

// ---------------------------------------------------------------------------
// Battle Game music (docs/SATURN_VERSION_ANALYSIS.md 6a)
// ---------------------------------------------------------------------------
// The Saturn's BGM loader (GAME5 0x0604C9F0) does not use the campaign's
// per-stage tables in STAGE8: the group comes from a per-arena byte table
// (0x0609ACCC) and the room's channel bits from the one after it (0x0609ACDC).
// Most fights play group 0x40, /SND/BATTLE.CDP - Saturn-only music, which the
// asset migrator renders into SATURN/Sound. The rest rooms and four fights
// reuse campaign groups the PC ships. The PC's own tables have no row for
// stage 8, and folding it onto stage 1 (get_stage_id) played the MANSION's room
// music in the arenas.
#define SATURN_BGM_GROUP_BATTLE 0x40
// Port-only group id for RANKING.PRG's /SND/NAME.CDP (Bgm_5a - Battle game ranking).
// The Saturn loads that pack by name in its own module; the port needs a group to 
// hand to bgm_play_group_channel().
#define SATURN_BGM_GROUP_RANKING 0x41

// True once the run's end sequence is over; game_loop returns then and
// game_start chains saturn_ranking_state (GAME5 raises bits 0-1 of
// g_SystemFlags and hands over to RANKING.PRG at the same point).
int saturn_battle_run_finished(void);

// The result counters GAME5 keeps for RANKING (0x002FFFA8-0x002FFFAE): the
// rounds of each type and the sprays the run started with, minus one per shot
// (0x060337B8, a jump table on the weapon id - 2) and one per heal
// (0x060444FA). Pickups in the arenas do not add to them, which is why the port
// cannot simply count what is left in the inventory. No-ops outside a run.
void saturn_battle_count_shot(unsigned char weaponId);
void saturn_battle_count_heal(void);

// End the run once game_loop() has returned, whatever ended it: take what
// RANKING needs (the character, finished or dead, the score) while the run's
// state is still there, then saturn_battle_end_run(). Returns 1 when a run was
// in progress and game_start must chain saturn_ranking_state; 0 otherwise.
int saturn_battle_close_run(void);

// RANKING.PRG as a game state (SATURN_VERSION_ANALYSIS.md 4f): after a
// finished run the score screen, the ranking with name entry; after a death the
// ranking table alone. Then "save?" and "continue?": NO chains title_state, YES
// chains saturn_battle_load_state - RANKING's two exits (0x0602E764).
void saturn_ranking_state(void);

// The Battle Game's load path, BU_LOAD -> GAME5: the load screen, then the run
// started from the save picked there, chained into game_start. Chained by the
// title's BATTLE GAME item and by the ranking's "continue? YES". Battle mode is
// armed only once a save has been loaded, so the screen's EXIT (which chains
// title_state itself) leaves nothing armed.
void saturn_battle_load_state(void);

// The debug menu's arena jump (DebugMenu.cpp's ROOM CHANGE lists STAGE8, the 15
// arenas, while a run is in progress). Port-only, not the Saturn's: the original
// only ever moves on to the next arena. `arena` is 1..15 and the warp runs
// through the same virtual door record and the same spawn record
// battle_advance_room() uses, so the arrival point is the arena's own. Returns 1
// when the transition was armed, 0 when it was refused (no run in progress, the
// arena out of range, or the end sequence owns the screen).
int saturn_battle_debug_warp(int arena);

// Zombie Wesker (GAME5 0x0603C9E8): in arena 12 the first enemy slot loads
// em1017 in place of its own model and gets behaviour 9. Call it on the enemy
// model loader's "load a new model" path, before LoadEntityEMD; returns 1 when
// it swapped the id, and the caller restores `originalId` after the load, as
// 0x0603CA98 does - the entity keeps its zombie type and AI.
int saturn_battle_model_override(struct Entity* e, int slot);

// The golden Tyrant (GAME5 0x0603BD0A): every CLUT of a model loaded in arena
// 15 gets red +4, green +3, blue -5 (RGB555 components, clamped 0..31, the STP
// bit kept) - 0x0603A680. `tim` is the model's TIM; a no-op elsewhere.
// Returns 1 when it tinted (port: an HD skin named by file is then not used,
// since it would draw the model untinted).
int saturn_battle_tint_model_tim(unsigned char* tim);

// The arena's BGM group: 1 and *group set when a run is in progress, else 0.
int saturn_battle_bgm_group(unsigned char room, unsigned char* group);

// The Battle Game's room BGM states, 32 rooms, for g_RoomBgmStatePtr (writable:
// init_room silences the room in attract mode). NULL when no run is in progress.
unsigned char* saturn_battle_bgm_states(void);

// Channel `ch`'s file name for a Saturn-only BGM group (Sound\<name>.wav in the
// SATURN overlay) and its loop flag, or NULL.
const char* saturn_battle_bgm_name(unsigned char group, int ch, signed char* loop);

// Build one 4bpp TIM page holding `text` in the port's own 8x14 font, for the
// title's third menu row. `fontTim` is a loaded data\FONTUS.TIM; the glyph grid
// is the one the port's PrintText8x14 and ending_draw_text already use
// (texU = (c % 18) * 8, texV = (c / 18) * 14, 8 px cells). Writes a 256x64 page
// and returns its byte length, or 0 if the text does not fit.
//
// PORT DECISION, not the Saturn's art: the Saturn draws its menu with its own
// sheet inside TITLE.BIN, which is not decoded yet (the file is 512 KB of two
// 320x240 title bitmaps in three palettes plus texture data; there is no font in
// it). Building the row from the shipped USA font keeps the item readable today
// with no new asset, and swapping in the Saturn's own art later is a change to
// this one function.
unsigned int saturn_title_build_text_page(const unsigned char* fontTim,
                                          const char* text,
                                          int destX, int destY,
                                          unsigned char* out);

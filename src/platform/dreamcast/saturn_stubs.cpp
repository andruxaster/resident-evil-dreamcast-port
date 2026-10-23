// saturn_stubs.cpp - the Sega Saturn content compiled out (RE1_DC_SATURN=0).
//
// Upstream 1.4.0 adds the Saturn release as a third game mode (src/game/saturn:
// the Tick, Zombie Wesker, the second lab Tyrant, the Battle Game). It needs
// the SATURN/ asset overlay and ~1.3 MB of extra buffers for the converted
// Saturn models, which a Dreamcast disc built from the PC/DC data does not
// have. The default Dreamcast build leaves src/game/saturn/*.cpp out and links
// these instead (Makefile.dreamcast, RE1_DC_SATURN=0); `make RE1_DC_SATURN=1`
// builds the real thing.
//
// Every call site outside src/game/saturn is either gated on g_bSaturnMode
// (never on in this build - main.cpp turns Mode=SATURN into OG) or asks the
// Battle Game whether it is available / running. So each stub returns what the
// real function returns when Saturn mode is off and no Battle run is active:
// 0 / NULL / nothing, the identity for the model-index remap.
#include "../../Globals.h"
#include "../../game/saturn/SaturnBattle.h"
#include "../../game/saturn/SaturnModels.h"
#include "../../game/saturn/SaturnRoomObjects.h"
#include "../../game/saturn/SaturnTick.h"
#include "../../game/saturn/SaturnTyrant.h"

#include <stddef.h>

#if RE1_DC_SATURN
#error "saturn_stubs.cpp is for RE1_DC_SATURN=0 builds only"
#endif

unsigned char g_saturnBattleActive = 0;

// --- SaturnBattle.h: the Battle Game is never offered, never running --------
int  saturn_battle_assets_present(void) { return 0; }
int  saturn_battle_offered(void) { return 0; }
void saturn_battle_restore_mode(void) {}
int  saturn_battle_dc_outfit(void) { return 0; }
int  saturn_mode_is_borrowed(void) { return 0; }
void saturn_battle_unlock(void) {}
void saturn_battle_sync_from_card(void) {}
void saturn_battle_preserve(void) {}
void saturn_battle_begin_run(void) {}
void saturn_battle_tick_run(void) {}
void saturn_battle_clock_run(int) {}
void saturn_battle_end_run(void) {}
int  saturn_battle_asset_room(unsigned char*, unsigned char*) { return 0; }
void saturn_battle_count_shot(unsigned char) {}
void saturn_battle_count_heal(void) {}
int  saturn_battle_close_run(void) { return 0; }
void saturn_ranking_state(void) {}          // only chained after a run
void saturn_battle_load_state(void) {}      // only chained by the title's BATTLE item
int  saturn_battle_debug_warp(int) { return 0; }
int  saturn_battle_model_override(struct Entity*, int) { return 0; }
int  saturn_battle_tint_model_tim(unsigned char*) { return 0; }
int  saturn_battle_bgm_group(unsigned char, unsigned char*) { return 0; }
unsigned char* saturn_battle_bgm_states(void) { return NULL; }
const char* saturn_battle_bgm_name(unsigned char, int, signed char*) { return NULL; }
unsigned int saturn_title_build_text_page(const unsigned char*, const char*, int, int,
                                          unsigned char*) { return 0; }

// --- SaturnModels.h ------------------------------------------------------------
int saturn_emd_is_outfit(unsigned char) { return 0; }
const char* saturn_emd_path(unsigned char, unsigned char) { return NULL; }
unsigned char saturn_emd_model_index(unsigned char modelIndex) { return modelIndex; }
const char* saturn_tick_snd_name(int, const char*, const char* const*) { return NULL; }

// --- SaturnRoomObjects.h -------------------------------------------------------
void saturn_room_objects_update(void) {}
int  saturn_room_objects_hidden(void) { return 0; }
int  saturn_omodel_has_no_model(int) { return 0; }

// --- SaturnTick.h ----------------------------------------------------------------
int  saturn_tick_active(void) { return 0; }
void saturn_tick_room_reset(void) {}
void saturn_tick_state_init(void) {}
int  saturn_tick_variant_hold(void) { return 0; }
void saturn_tick_variant_0_leap(void) {}
void saturn_tick_variant_2_ai(void) {}
void saturn_tick_behavior_chase(void) {}
void saturn_tick_behavior_pounce(void) {}
void saturn_tick_behavior_grabhold(void) {}
int  saturn_tick_player_bitten(void) { return 0; }
int  saturn_tick_joint_scaled(const Entity*, unsigned char) { return 0; }
void saturn_tick_scale_local(const Entity*, unsigned char, MATRIX*) {}
void saturn_tick_scale_mesh(const Entity*, unsigned char, MATRIX*) {}
const MATRIX* saturn_tick_child_parent(const Entity*, unsigned char, const MATRIX* world)
{
    return world;
}

// --- SaturnTyrant.h --------------------------------------------------------------
void saturn_tyrant_init(void) {}
void saturn_tyrant_forced_select(void) {}
int  saturn_tyrant_forced_run(void) { return 0; }
void saturn_tyrant_update_tail(void) {}
void saturn_capsule_collision(int*) {}
int  saturn_game_loop_update(void) { return 0; }
void saturn_tyrant_battle_init(void) {}
void saturn_tyrant_battle_update(void) {}

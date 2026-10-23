// SaturnTyrant.h - the Sega Saturn's second lab Tyrant (Chris, Laboratory B4).
//
// All of it is the Saturn's own code in the Tyrant's update, GAME4.PRG (the
// Laboratory module, loaded at 0x0602B000; every address below is GAME4). It
// is NOT a script and not a separate enemy: the room is the PC's ROOM5130
// byte for byte, and the Saturn re-raises the same type-0x0C entity after its
// first death. See docs/SATURN_PORT.md section 4.
//
// Everything here is gated on g_bSaturnMode by the callers, so OG and DC never
// reach it.
#pragma once

struct Entity;

// Tyrant.cpp hooks. All three run with ENTITY pointing at the Tyrant.

// End of tyrant_init (0x004212b0). The Saturn's init (0x060744C8) clears the
// release bytes +0x184..+0x186 and sets +0x186 bit 0x08 - "die the PC way, no
// second Tyrant" - when the player is Jill.
void saturn_tyrant_init(void);

// tyrant_state_forced (state 3, the death entry). The Saturn's state 3
// (0x06074C8C) picks behaviour 2, its dissolving death, for the lab Tyrant
// unless +0x186 bit 0x08 is set. Called only when the state word is being
// (re)written, i.e. with ignore_player_flag == 0, and only for id 12.
void saturn_tyrant_forced_select(void);

// State-3 behaviour 2 is the dissolve in the Saturn's table (0x0609A994[2] =
// 0x06077804); on the PC's table it would be `pause`. Returns 1 when it ran
// the dissolve, 0 to let the PC table run.
int saturn_tyrant_forced_run(void);

// End of tyrant_update (0x06074B50: `if (+0x186 & 4) 0x060788A8()`, after the
// update_player_position call). The second Tyrant's release: watch for Chris
// at the lab terminal, then run the release cutscene.
void saturn_tyrant_update_tail(void);

// The second capsule's collision: a cylinder at x 0x1BD0, z 0x7400, radius
// 0x923, in this room only (GAME4 0x0605CB38). The Saturn runs it for the
// player at the top of the player's update (0x0602FF52) and for every enemy
// slot at the top of the enemy loop (0x06063256), before the entity's own
// handler - so the Tyrant cannot walk into the capsule either. `t` is the
// entity's scaMatrixData.localMatrix.t (x, y, z).
void saturn_capsule_collision(int* t);

// Saturn: everything the Saturn adds to the per-frame player update point of
// game_loop - the capsule collision push and, in a Battle Game run, the arena
// sequence, run clock and HUD. Returns nonzero when the run is over and
// game_loop must return to the title.
int saturn_game_loop_update(void);

// Saturn Battle Game, arena 15 (GAME5 0x06079A12-0x06079A50, in the Tyrant's
// init after the look-at set-up): the arena Tyrant opens at 0x02BC = 700 HP
// (the campaign's first Tyrant has 220), stands on the floor (y = 0) instead
// of in the capsule, carries behaviour 0x10, starts in state 1 behaviour 1
// rather than the stasis pod, sets +0x179 |= 3 and +0x186 |= 8 ("die the PC
// way" - no second-Tyrant dissolve). Call after saturn_tyrant_init().
void saturn_tyrant_battle_init(void);

// Saturn Battle Game: the top of the Tyrant's update (GAME5 0x06079B78)
// clears behaviour bit 0x80 every frame, so no room script can park it.
void saturn_tyrant_battle_update(void);

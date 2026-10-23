# Western difficulty

The PS1 USA release, the Saturn USA release and the Director's Cut ship exactly one
set of rules; the PC release and the Japanese releases ship another. The port names
the first set **Western difficulty** (`western_difficulty()` in `src/Globals.h`).

| | Western (PS1 USA / Saturn USA / DC) | PC release (default) |
|---|---|---|
| enemies | PC's *second-playthrough* branch (`SCENARIO_FLAG_SECOND_PLAYTHROUGH` set) | branch picked by the flag, clear on a first run |
| weapon hit table | the second-playthrough table | first-playthrough table until the flag is set |
| auto-aim | none - the player rotates by hand | reticle follow, lock-on fire, knife turn |
| ink ribbon pick-up | 2 | 3 (forced in `room_event_item_pickup`) |
| Jill | needs ribbons, main-hall ribbon present | no ribbon needed on a first run, main-hall ribbon removed |

Jill's ribbons are **not only** a western rule: the ribbon-free first-run Jill is
the PC's alone. Every console release - PS1 (USA and Japanese), Saturn (USA and
Japanese), the Director's Cut - makes her use ribbons, whatever its damage
rules. `jill_needs_ink_ribbons()` (Globals.h) therefore turns the PC's
first-run Jill branches off for Saturn and DC mode on its own: the typewriter's
free save and its "save your progress?" prompt, the ribbon pick-ups hidden from
her, the main-hall ribbon and the guardhouse desk reset. That is what keeps the
Japanese Saturn - Japanese damage rules, flag clear - on ribbons. Its rooms give
3 per pick-up (all 40 ribbon `item_model_set` records; the USA Saturn's are 2).

## Where it is on

`western_difficulty()` is true when any of:

- `g_bSaturnMode` on the USA tree - the Saturn campaign **and** the Battle Game.
  Not on the JPN tree: that is the Japanese Saturn release ("Bio Hazard",
  overlay `SATURN_JPN`), whose five `GAME*.PRG` hold the PC's
  **first**-playthrough weapon table - Japanese rules;
- `g_bDcMode` and `g_DcDifficulty != DC_DIFFICULTY_TRAINING` - STANDARD, ADVANCED
  and ADVANCED* (TRAINING keeps the PC rules, as before);
- `[Game] WesternDifficulty=1` in config.ini - the opt-in for `Mode=OG`.

It is derived, not stored: nothing is added to the save block. What it does:

| site | change |
|---|---|
| `GameStart.cpp` `InitializeGame` | raises `SCENARIO_FLAG_SECOND_PLAYTHROUGH` after both the new-game and continue branches (the DC already did this; attract demos excluded) |
| `RoomEvents.cpp` `room_event_item_pickup` | ink ribbon quantity 2 |
| `PlayerAnimations.cpp` `player_autoaim_follow_enabled` / `player_lockon_enabled` | gun-aim reticle scan + follow turn, raw-pad lock-on fire (behaviour 0x1a), knife auto-aim turn (0x16) |
| `entities/Yawn.cpp` | hit damage on the Yawn: flat 15 (see the exception below) |
| `EndingScreen.cpp` | the flag is not a "cleared once" marker, so the first-run backdrop / congratulations FMV are kept (as the DC already did) |

Everything else follows from the flag: the weapon-hit table, Jill's ribbon rules,
and every enemy branch in `src/game/entities/`.

## Evidence (`python tools/verify_western_difficulty.py`)

**Weapon hit table - no new table is needed.** The PS1 USA table (`SLUS_001.70
0x8008bbe0`, 200 x 12 B) equals the PC's *second-playthrough* table
(`0x004bbffe`) in damage and hit state in 200 of 200 records, and differs from the
first-playthrough table (`0x004bb698`) in 71. All five Saturn `GAME1..5.PRG` carry
the same 2400 bytes (byte-swapped to big-endian) at file offsets `0x69154`,
`0x666f4`, `0x669a0`, `0x69ffc`, `0x6ee20`. Before this change a Saturn game ran
with the flag clear and so with the **first**-playthrough table (shotgun 53 against
the Saturn's 20); raising the flag fixes that. (The DC keeps its own three
difficulty columns - `dc/WeaponDamageTables`.)

**There is no first-playthrough branch to port.** None of the seven USA
`STAGEn.EXE` overlays contains a call to `Flg_ck` (`0x80048ef4`). Spot checks of the
player-damage constants written to `0x800c51ac` (health) in those overlays all
take the PC's flag-*set* value: zombie bite 12/12/9/9 (the only table present - the
10/10/6/6 row does not exist), crow 4 and 16, chimera touch 10, hunter 13 / 20,
plant 42 25 / 15 / 40, neptune 30, yawn bite 28, web spinner 18.

**Auto-aim.** In `SLUS_001.70` the gun-aim state (`0x8003f324`) calls the reticle
scan `FUN_8003d8e0` but stores the result in `0x800c529c`, which nothing ever
reads, so there is no reticle follow turn; the gun hold input (`0x8003f830`) has no
raw-pad lock-on branch, so behaviour `0x1a` (`0x80041094`, the lock-on turn tail)
is never entered. Both exist in the PC code (`player_behavior_12_gun_aim`,
`player_behavior_13_gun_hold_input`) and were kept from the Japanese release.

**Ink ribbons.** `IncludeCurrentItem` (`SLUS_001.70 0x8002d3cc`) takes the room
record's own quantity. Every ribbon `item_model_set` (`0x18`, item `0x2F`) in the
PS1 USA RDTs is 2 (the Saturn RDTs are byte-identical to the PS1's); the PC RDTs
carry 2s and 3s and `room_event_item_pickup` overrides all of them to 3.

### The one exception: Yawn hit damage

The PC splits the damage the **Yawn takes** (serum taken): flag clear 15 per hit,
flag set 5 per hit plus another 15 on joints 0-9. The PS1 USA overlays (STAGE2
`0x8011673c`, STAGE7 `0x80120dd4`), the DC overlays (`0x80115fc0`, `0x8011ffcc`) and
the Saturn (`GAME1 0x06077FF4`, `GAME5 0x0607F8CC`) all do a flat `-0xf` with no
`-5` and no joint test - the PC's *first*-playthrough branch. Western therefore
takes that branch even though the flag is set.

## Not verified

- The Saturn's aim handler and ribbon pick-up were not decoded in the SH-2 modules;
  they follow the PS1 USA engine (same lineage, byte-identical RDTs and hit tables)
  and the report that the Saturn has no auto-aim. **Needs a play-test.**
- The enemy *threshold* sites (Cerberus `0x0d/0x11`, Tyrant knock-back
  `0x0c/0x12`, `0x10/0x14`, Chimera health `0x1d/0x13`, Neptune speed `0x78/100`,
  zombie hit-threshold / behaviour tables) were not matched one by one: their
  constants are too common to find by search without per-function identification.
  The damage constants above and the DC's forced flag point the same way.
- DC TRAINING stays on the PC auto-aim and 3-ribbon rules (not requested).

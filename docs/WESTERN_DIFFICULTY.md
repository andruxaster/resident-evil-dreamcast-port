# Western difficulty

The PS1 USA release and the Saturn USA release ship exactly one set of rules; the
PC release and the Japanese releases ship another. The port names the first set
**Western difficulty** (`western_difficulty()` in `src/Globals.h`). The Director's
Cut is neither - see [The Director's Cut](#the-directors-cut) below.

| | Western (PS1 USA / Saturn USA) | PC release (default) |
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
- `[Game] WesternDifficulty=1` in config.ini - the opt-in for `Mode=OG`, and for
  `Mode=DC` in STANDARD, ADVANCED and ADVANCED*. DC TRAINING never takes it.

The in-game OPTIONS menu shows the mode and difficulty being played.

It is derived, not stored: nothing is added to the save block. What it does:

| site | change |
|---|---|
| `GameStart.cpp` `InitializeGame` | raises `SCENARIO_FLAG_SECOND_PLAYTHROUGH` after both the new-game and continue branches (the DC does this on its own term; attract demos excluded) |
| `RoomEvents.cpp` `room_event_item_pickup` | ink ribbon quantity 2 |
| `WeaponDamage.cpp` (DC only) | STANDARD / ADVANCED* read the second-playthrough damage instead of the DC's base column (= the PC's first-playthrough table); ADVANCED keeps its own column |
| `PlayerAnimations.cpp` `player_autoaim_follow_enabled` / `player_lockon_enabled` | gun-aim reticle scan + follow turn, raw-pad lock-on fire (behaviour 0x1a), knife auto-aim turn (0x16) |
| `entities/Yawn.cpp` | hit damage on the Yawn: flat 15 (see the exception below; the DC too) |
| `entities/Hunter.cpp` `hunter_pounce_health_gate` | a Hunter pounces only on a player under 98 / 67 health (Chris / Jill), not the PC's 105 / 72 |
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
- DC mode was play-tested on the old rules (Western in STANDARD/ADVANCED); the DC
  rules below **need a play-test**.

## The Director's Cut

The DC was first filed under Western. Checked against `SLUS_005.51`, the DC disc's
RDTs and its stage overlays, it is a third set. This is what every DC difficulty
plays by when `[Game] WesternDifficulty` is off (and TRAINING always):

| | DC (SLUS_005.51) | port site |
|---|---|---|
| enemies | second-playthrough branch: no `Flg_ck` (`0x800489a8`) call in any DC `STAGEn.EXE`, only the hard zombie-bite row | `InitializeGame` raises the flag for `g_bDcMode` |
| weapon hit table | its own three damage columns, no flag test (`apply_weapon_damage` `0x800120e8`) | `dc/WeaponDamageTables`, unchanged |
| gun auto-aim | **yes** - the gun-aim state `0x8003ea08` reads the reticle result `0x800c529c == 3` and turns toward it; the gun hold `0x8003f008` enters lock-on behaviour `0x1a` (`0x80040880`) on the raw pad | `player_autoaim_follow_enabled`, `player_lockon_enabled` |
| knife | the raise `0x80040b98` follows a locked target (gated by `0x80090568` = 1); the hold `0x80040d78` has **no** entry into knife turn `0x16` (`0x800414f0`), which is unreachable | `player_knife_lockon_enabled` off in DC |
| ink ribbon pick-up | the record's own quantity (`IncludeCurrentItem` `0x8002ce28`, doubled in TRAINING / ADVANCED*): 3 in every DC room except STANDARD ROOM30C0, 5051, 6001, 7180 (2) | `room_event_item_pickup` skips its override in DC mode |
| Jill | needs ribbons | `jill_needs_ink_ribbons()` |
| Yawn hit damage | flat 15 (`0x80115fc0`, `0x8011ffcc`), no mode test; the joint 0-9 bonus is compiled out (reload + store) | `flatHit` includes `g_bDcMode` |
| Hunter pounce gate | `{98, 67}`; the Hunter init (STAGE3 `0x80112be0`, STAGE6 `0x80119cb0`, STAGE7 `0x801150bc`) writes `{75, 52}` under `0x20000` | `hunter_pounce_health_gate` |

**The DC's weapon damage is not the second-playthrough table.** The STANDARD
column (`0x8008AC3A`) equals the PC's *first*-playthrough damage in 200 of 200
records (shotgun on a zombie 53; the PS1 USA / PC second-playthrough value is
20). ADVANCED (`0x8008BEFA`) is the second-playthrough table except in 4 records
(shotgun 34), and TRAINING (`0x8008B59A`) differs from first-playthrough in 42. All
three share the STANDARD hit states (`0x8008ac3e`), which equal both PC tables.
ADVANCED* reads the STANDARD column. Re-derive with
`python tools/gen_dc_damage_tables.py` (Ghidra bridge).

ROOM5051 is not in the DC overlay and the PC tree's copy says 3 where the DC disc
says 2, so that one pick-up gives 3 in DC mode.

### Every DC mode-bit site

All reads of `g_status_flags` (`0x800c3004`) that test the mode bits (TRAINING
`0x40000`, ADVANCED `0x20000`, ADVANCED* `0x10000`), in `SLUS_005.51` and every
overlay on the disc, against the port:

| site | what it does | port |
|---|---|---|
| `InitializeGame` `0x8002958c` | start health 180/150 (TRAINING), 100/70 (ADVANCED, ADVANCED*); max health the same; TRAINING and ADVANCED* put two stacks of 30 clips in item box slots 0-1 (`0x1e0b1e0b` at card `+0x2C4`) | `GameStart.cpp` (the clips were missing; added) |
| `room_set` `0x80044348`, `0x800443d8` | Rebecca 140 / 64, back to 180 / 100 | `RoomInit.cpp` |
| `apply_weapon_damage` `0x800120e8` | three damage columns; ADVANCED Beretta roll `0x8008C854` | `WeaponDamage.cpp` |
| `0x8001393c` (zombie head-explode) | ADVANCED skips it in stage byte 6 room `0x1C` | `WeaponDamage.cpp` |
| `LoadEntityEMD` `0x80023930`, `0x80023a88` | ADVANCED model remaps | `dc/EntityModels.cpp` |
| `LoadEquippedWeaponAnimation` `0x80023e78`, `0x80053998` | the custom Beretta equips as weapon 2 | `EntityModelLoader.cpp`, `MainMenu.cpp` |
| `SetInitialItems` `0x8002c804` | ADVANCED starts with the custom Beretta (Jill and Rebecca) | `GameStart.cpp` |
| `IncludeCurrentItem` `0x8002ce28`, `0x8002e4e4` | ammo and ribbons doubled under `0x50000` (TRAINING, ADVANCED*) | `dc/Items.cpp` |
| `0x80043fb4` | ADVANCED arrange stage lookup | `dc/ArrangeStages.cpp` |
| STAGE1-7 `0x801055c0`.. | ADVANCED picks the arrange rooms' background checksum table for CD reads | not needed on PC |
| STAGE3/6/7 Hunter init | `{75, 52}` pounce gate | `entities/Hunter.cpp` (was missing; added) |
| TITLE `0x800e1204`.., ENDING `0x800e17ec` | set the bits / the ADVANCED unlock | `TitleScreen.cpp`, `EndingScreen.cpp` |

The Hunter gate is also a western rule the original check missed: PC and Japanese
Saturn `{105, 72}` (`0x004b4860`, GAME1/2/5), PS1 USA STAGE3/6/7 and USA Saturn
GAME1/2/5 `{98, 67}`.

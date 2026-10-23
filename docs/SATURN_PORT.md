# Saturn port

Status (2026-09-29): **Phases A and B are done and the mode has been play-tested:
the Ticks are in the Underground and the mansion Hunters stay stock, which is
exactly what the disc does.** `Mode=SATURN` runs, the two Saturn-only creatures
are selected by the Saturn's own model table and loader swap, and the stock
models the earlier play-test had overwritten are restored.

**The main lab and its second Tyrant (Phase D's campaign part) are complete and
play-tested by the user (2026-09-29)**: the fight is ported from the Saturn's own
Tyrant code (§4.3: the dissolving first death, the terminal trigger, the glass,
the battle theme restart, the re-raise), the room shows the Saturn's drained
centre tanks, and the dark `OB80` cover over the second capsule is migrated and
drawn until the release (§4.4). The Battle Game (Phase C) now has the 15-arena
sequence, clear pause, elapsed-time HUD, room-clear banner and its own
music (rendered from the Saturn's sequences and wired to the Saturn-only mode;
in-game listening test still needed); the
Saturn-only boss variants and the results/ranking screen remain (§3).

Reference: `docs/SATURN_VERSION_ANALYSIS.md` (the disc, the code, every address
quoted here). Source tree: `assets/SATURN_ISO_CONTENT/` (`tools/saturn_iso.py`).

Scope: **the Saturn-exclusive content only.** The Saturn's re-dressed versions of
shared content (its lower-detail meshes of the 65 shared creatures, its 8 bpp
backgrounds, the player models) are deliberately not imported: the PC/PS1
versions stay in use.

---

## 0. Design

- **A sparse overlay of exclusive content.** `[Game] Mode = SATURN` makes
  `ResolveAssetRoot` search `<assets>/SATURN/` ahead of the base tree, exactly as
  `DC` does. The overlay holds only what the Saturn adds; everything else is the
  base tree's. This replaces the earlier "full tree" rule, which rested on
  "541 of 542 shared files differ": the rooms decode byte-identical to the PS1's
  (§1), and the differing models are re-dresses we are not using.
- **Mode-exclusive code lives in `src/game/saturn/`** (like `src/game/dc/`),
  gated on `g_bSaturnMode`. `Mode=OG` and `Mode=DC` stay byte-for-byte what they
  are.
- **The Saturn behaviour is code, not data.** Every Saturn addition found so far
  is selected by the modules, not by the room scripts (`SATURN_VERSION_ANALYSIS.md`
  §4g): the port reproduces those branches, it does not edit RDTs.
- No stubs. Where the Saturn changed data the data is ported; where it changed
  code the branch is ported.

### What the overlay holds, and why the rest falls back

| overlay path | content | status |
|---|---|---|
| `Enemy/EM1016.EMD`, `EM1116.EMD` | the Tick (Chris / Jill model block) | **migrated, selected by Phase B** |
| `Enemy/EM1017.EMD`, `EM1117.EMD` | Zombie Wesker | **migrated, selected by Phase B** |
| `Enemy/EM1060.EMD`, `EM1061.EMD` (Chris), `EM1160.EMD`, `EM1161.EMD` (Jill) | the Saturn's two alternate outfits per character (disc `EM1030`/`EM1032`, `EM1031`/`EM1033`), **renamed** — the disc's names are the OG's and DC's own outfit files | **migrated (always)**; selected at model-table index 28 / 29 in genuine Saturn mode, never in a borrowed Battle run (`saturn/SaturnModels.h`). The room 11C wardrobe that sets them is Saturn **code**, not the RDT event: handler 9 is overridden (`GAME5 0x060490B8`, player yaw picks the mannequin, its own global messages 62-64) and the "yes" is post-action 10/4 (`0x0605CA84`). The port carries it as **data** instead: the migrator always writes `Stage1/ROOM11C0/1.RDT` and `Stage6/ROOM61C0/1.RDT` patched (`core/SaturnWardrobe.cpp`, reference `tools/saturn_wardrobe_rdt.py`, `check` compares the migrated files): area 4 → event 0 (outfit 2, flags 112+113), area 5 → new event 3 (outfit 1, flag 112), the Saturn's three texts as room messages 3-5, DC-style toggle back to the default. The yaw choice becomes an area choice. **Play-tested OK 2026-10-05**: both Saturn outfits and the change back |
| `Stage1..7/ROOM*.RDT`, `RC*.pak` | every other campaign room and background | **migrated, optional** ("All rooms and backgrounds"); 141 of 348 rooms differ from the PC's by its script patches |
| `Sound/<Bgm_xx>` | every campaign BGM, rendered from `BGM<group>.CDP` under the PC's file names | **migrated, optional** ("All room BGM"); no code change |
| `Stage8/ROOM8010..80F0.RDT` | the 15 Battle Game arenas, decoded | **migrated**, and the first one is now loaded (Phase C.1) |
| `Stage5/RC5130..5137.pak` | the Laboratory Tyrant room's backgrounds, re-drawn by the Saturn | **migrated, selected by Phase D** |
| `Enemy/A_TRS1.DAT` (+`T`), `A_TRS2.DAT` (+`T`) | the two Tyrant capsules (lab, Underground) | **format decoded**; no PC asset yet |
| `Enemy/A_PLT2/4.DAT`, `A_KKM1.DAT` (+`T`) | extra plants, kitchen machine (stage 3 room 12, arena 8) | **format decoded**; no PC asset yet |
| `Sound/Bgm_58`, `Bgm_59`, `Bgm_24`, `Bgm_5a` | the Battle Game's music (`SND/BATTLE.CDP`, `BGM34B.CDP` song 2, `SND/NAME.CDP`), rendered from the sequences | **migrated**, routed by arena and at the end of a run |
| other sound | the Saturn's SE banks, `BATTLE.BIN`'s unverified streams | music format decoded (`SATURN_VERSION_ANALYSIS.md` §6a); the SE banks are not migrated |
| everything else | shared with PC/PS1 | falls back by design |

A sparse overlay is a *supported* shape, not an accident: `SATURN` is listed in
`kSparseOverlayModes` in `src/system/AssetPath.cpp`, so a campaign room, data
table or player model missing from `SATURN/` is silent rather than logged as an
incomplete tree. The DC's miss log stays exactly as it was.

---

## 1. Findings that set the shape (all verified)

1. **Rooms are the PS1's, LZ77-packed.** Decoded, all 348 shared `.RDT` are
   byte-identical to the PS1 USA files; the 15 `STAGE8` arenas decode to ordinary
   RDTs and reuse campaign rooms' backgrounds, lighting and sound banks through
   `g_BattleArenaRoomTable` (`GAME1` `0x06092B5E`). No room conversion beyond
   the decode + the index-0 transparency relabel.
   *That comparison is Saturn-vs-PS1.* The **PC's** room files are a different
   size — `ROOM5130.RDT` is 546,948 B on the PC against 478,372 B decoded from
   the Saturn — because the Saturn restructures the header. So "the rooms are the
   PS1's" and "the PC's rooms are a different build" are both true, and nothing
   here means the Saturn's rooms can be dropped into the port as-is.
2. **A Saturn `.EMD` = the complete PS1 EMD + three Saturn sections** (LZ77 SGL
   mesh, LZ77 4 bpp per-polygon textures, 16-colour palettes). Saturn units × 4
   = PS1; the joint ids are the PS1 skeleton's, so the PS1 animation data drives
   the Saturn mesh. The embedded PS1 part of `EM1016` is a verbatim copy of the
   Hunter, of `EM1017` of the naked zombie: the Saturn look is only in the
   Saturn sections.
3. **The Saturn extras are code-side** (`SATURN_VERSION_ANALYSIS.md` §4g): Tick =
   model swap for the Hunter on stage index 2 **plus GAME2's own Hunter code (§4i)**; Zombie Wesker = type `0x17` forced
   on arena `80C0`'s first enemy; Golden Tyrant = a +4/+3/−5 RGB555 palette tint
   in arena `80F0`; extra Tyrant capsules = `A_TRS1`/`A_TRS2` props. **All five
   `A_*` props are now mapped to their rooms**, and there are **two** capsules,
   not one: `a_trs1` in the main lab (stage 4 room `0x13`) *and* `a_trs2` in the
   Underground (stage 2 room 3) — the same stage whose Hunters become Ticks, so
   the Saturn re-dresses the Underground both ways.
4. **The Battle Game is fully read statically** (§4d-§4f): unlock, title item,
   arena sequence, 5-byte virtual doors, spawn table, state machine, score,
   ranking, save records.
5. **The stage byte is 0-indexed**, confirmed at `GAME5 0x0604EA80`: the Tick
   swap is `stage == 2 && model index == 10 → 26`, indexed `0x0609AD5C + index*12`
   with the Jill block at `+0x27C`. This is what puts the Tick in the Underground
   and leaves the mansion Hunters stock — confirmed by play-test. It matters
   beyond the Tick: **Saturn addresses are per-module**, and the same extra-prop
   loader sits at `0x0603DBA4` in GAME4 but `0x0603CBD4` in GAME5.

---

## 2. Done

- **Analysis tooling**: `saturn_iso.py`, `saturn_lz77.py`, `saturn_room.py`,
  `saturn_sh2.py` (rewritten 2026-09-28 from the SH-2 manual; the first version
  was wrong, see §4e of the analysis), `saturn_emd.py`, the emulator-side tools.
- **Model converter**: `tools/saturn_emd.py pc` → PC `.EMD` with the Saturn mesh
  as `0x34000609` triangles (winding reversed, packets carrying the PC's
  `CBA 0x7800` / `TSB 0x0080`) and an 8 bpp TIM of 4-5 256-line pages.
- **Engine support** (all modes, inert for stock assets):
  `PSXObject_SetTexHeight` (`src/marni/Marni3DObject.cpp`, set by
  `CreateTmdObjectInternal`) reads each packet's page from its pad halfword when
  the bound TIM is taller than 256 lines; `g_DataBuffer` has 1 MB of port
  headroom for the larger EMDs.
- **Play-tests (2026-09-28)**: the Tick in ROOM604 and Zombie Wesker in ROOM505
  (several at once), swapped in for `EM1106` / `EM1101`. Trap noted: the model
  table is per character — Chris loads `EM10xx`, Jill `EM11xx`.
- **Asset migrator, Sega Saturn tab** (`tools/asset_migrator`): writes
  `<target>/SATURN/` (the four models + the 15 arenas + `SATURN.TXT`). The C++
  converter is byte-identical to the Python one; confirmed working by the user.
- **Phase A — `Mode=SATURN`**: `g_bSaturnMode` beside `g_bDcMode`
  (`src/Globals.h`), the reserved-name note dropped from `config.ini`, the
  sparse-overlay allowance above, and the stock `EM1006/EM1106/EM1001/EM1101`
  restored to `assets/USA/` (the play-test had overwritten them; the originals
  are in `assets/_saturn_test/`). The overlay root needed no work — `SATURN`
  was already a spelling `ParseGameMode` accepted and `SetAssetMode` composed.
- **Phase B — the model table and the Tick** (`src/game/saturn/SaturnModels.*`):
  the two placeholder slots the Saturn fills (index 26 → `em1016`/`em1116`, index
  27 → `em1017`/`em1117`) and the loader swap that makes stage 3's Hunters Ticks
  (index 10 → 26, entity type 6 kept). Wired into `LoadEntityEMD` in the same
  shape as the DC's, so both modes are one `if` away and neither can run in the
  other. Index 35 is deliberately **not** overridden: `em1018` is not on the disc.
- **Verifier**: `tools/verify_saturn_models.py` — the table entries against the
  port's own `g_emdPathTable` in *both* character blocks, the Tick guard's exact
  width, the `g_bSaturnMode` gating, both build files, and the four overlay
  models. It also fails on any basename collision in `Game.vcxproj`, which is
  not hypothetical (see the file's header).
- **Play-test (2026-09-28, the user)**: `Mode=SATURN` boots; the Ticks are in the
  Underground and the mansion Hunters are stock, which is the Saturn's own
  behaviour. Phase A and B confirmed against the original in `0x0604EA80`.
- **Phase D, part 1 — the second Tyrant and its room's art** (2026-09-28):
  * **Where the fight is**: the *same* room as the first one, `ROOM5130`
    (Laboratory B4, `ROOM_MAIN_LAB`). Not room 515 — see §4.1.
  * `src/game/saturn/SaturnTyrant.*`: the second fight, **ported from the
    Saturn's own Tyrant code** (2026-09-29; it replaced a port-invented stand-in).
    Chris only. The first Tyrant dissolves instead of dying, and the same entity
    is re-raised as the second one by the Saturn's release cutscene, triggered
    at the lab terminal. Hooks in `Tyrant.cpp` (init, state 3, end of update);
    see §4.3.
  * `tools/saturn_bss_to_pak.py` + `core/SaturnBss.cpp`: the Saturn's `.BSS`
    (320×240 8 bpp + a per-camera 256-entry RGB555 palette, LZ77, 8×8 cells)
    converted into PC background paks, and `--verify` to find which rooms the
    Saturn re-drew. `SATURN/Stage5/RC5130..5137.pak` is the result.
  * Migrator: a "Laboratory Tyrant room backgrounds" checkbox; the paks are
    checked by decoding them again through the game's own LZW and matching the
    TIM header, not by re-running the encoder. C++ and Python outputs are
    byte-identical for all eight.
- **Phase D, part 2 — the room's objects** (2026-09-29): the Saturn's per-room
  `OBnn.DAT` table (§4.4). Omodel slots 1-3 have no model on the Saturn and are
  not drawn; slot 0 is `OB80`, converted by the migrator
  (`core/SaturnObModel.cpp`) into a TMD + TIM embedded in the migrated
  `SATURN/Stage5/ROOM5130.RDT` / `ROOM5131.RDT`, shown until flag `0x72`.
- **Play-test (2026-09-29, the user)**: Chris, lab Tyrant room — the first Tyrant
  dissolves, the terminal triggers the release, the glass bursts, the battle theme
  restarts with no Wesker voice, the centre tanks are drained and the second
  capsule wears its dark cover. **Main lab and second Tyrant: done.**

### The room props (`A_*.DAT`) — analysis only, nothing in the game yet

- **`tools/saturn_mbn.py`** decodes the Saturn's room props: `info` (container,
  parts, textures, palette, plus a check that the sibling `A_*T.DAT` is exactly
  as large as the textures need), `obj`, and a textured `png` preview. The format
  turned out to be the `.EMD`'s **SGL mesh uncompressed** with no PS1 section, so
  the field layout is `saturn_emd.py`'s; the 4 bpp pixels live in the sibling
  `A_*T.DAT` and the palette in the mesh file's 32-byte tail. Validated on all
  four props, with `A_TRS1`'s texture size matching its file **exactly**
  (1280 + 1280 = 2560).
- **Asset load logic traced**: the `A_*` name table has exactly two users, one per
  file kind. `0x06059C74` builds the texture path by `strcat`-ing the literal
  `"t.dat"` onto the bare name — which is *why* the pair is `A_TRS1.DAT` +
  `A_TRS1T.DAT`, the original's own convention, and what the tool re-derives.
  `0x06059D40` builds the mesh path with `".dat"` and takes a **40-byte** record
  from the registry at `0x0609FF80`, counted at `0x060A1850+0x0A74`.
- **All five props mapped to their rooms** (§1 item 3), and the two
  `a_trs*` capsules are increments of a counter at `0x060A1850+0x0AE7` — which
  is *not* placement data, as an earlier reading assumed.
- **What the capsule is**: `A_TRS1` is 8 points and 2 polygons — a flat 40×64
  **billboard**, the second quad a near-copy of the first. It is a cheap
  background sprite, not a 3D capsule, so the collision cylinder matters more for
  feel than for a solid mesh.

---

## 3. Remaining phases

Each ends with something checkable; play-testing is the user's step.

### Phase A — `Mode=SATURN` scaffolding — **done**

- `g_bSaturnMode` beside `g_bDcMode`; `ConfigFile.cpp` stops calling `SATURN` a
  reserved name; the overlay root resolves to `<assets>/SATURN/`.
- Restore the stock `EM1006/EM1106/EM1001/EM1101` swapped for the play-tests
  (originals in `assets/_saturn_test/`).
- Check: `Mode=SATURN` boots and plays the campaign exactly like `OG` (the
  overlay has no campaign files yet that it would pick up). **Done — the user
  play-tested it.**

### Phase B — the new enemy types and the campaign extras — **done**

- **Model table**: the PC's placeholder slots carry the Saturn's (type `0x16` →
  `em1016`, `0x17` → `em1017`; index = type + 4, both character blocks) in
  Saturn mode. Done, `src/game/saturn/SaturnModels.cpp`.
- **Update functions**: type `0x16` is not used as a type (the Tick is a model
  swap); **open** — the Saturn's `enemies_update_functions_tbl` entry for `0x17`
  (Wesker) is still unread, so `0x17` currently dispatches through whatever the
  PC's table has at that index. It only shows up in arena `80C0`, so it is
  Phase C work: it needs the battle-mode flag first.
- **Tick**: the loader swap (Saturn `GAME5` `0x0604EA80`): on stage index 2,
  model index 10 (Hunter) loads index 26 (`em1016`). Done and **play-tested**.
  Still open: the Tick's sound bank.
- **Tick behaviour** (2026-10-04, `src/game/saturn/SaturnTick.*`): the swap alone
  is not the Tick. Stage byte 2 loads `GAME2.PRG`, whose Hunter is its own build:
  a stretching-arm stab, a keyframe animator with three code-side animations, a
  retuned variant-2 decision layer, and a hold on player flag `0x55`. Ported as
  hooks in `entities/Hunter.cpp` (state init, variant dispatch / 0 / 2, chase,
  pounce) plus an arm-scale hook in `rotate_entity` (`GteMatrix.cpp`) and the
  slot reset in `room_set`, plus the Tick's own hold/decapitation (behaviour 7)
  and four code-side animations (walk, stab, retract, decapitation) whose keys are
  built with the Saturn's `Rz·Ry·Rx` rotation, not the PC's `RotMatrix`, and the
  Tick stage's own player "bitten" machine (`g_playerAnimFunctions[44]`, hurt then
  decapitated, instead of the PC's instant kill).
  `tools/gen_tick_tables.py` generates
  `SaturnTickAnim.cpp` from `GAME2.PRG`. Full function map and the Saturn → PC
  field map in `SATURN_VERSION_ANALYSIS.md` §4i. **Untested in game** — compiled
  only. Open: `Snd_em(8)`, the writer of the `0x55` player flag.
- Check: stage 3 Hunters are Ticks in Saturn mode, stock elsewhere and in OG.
  **Done — the user confirmed Underground Ticks and stock mansion Hunters**, and
  the guard was read back out of the original at `0x0604EA80` to match.
- The main lab's second Tyrant and its extra capsule are **not** here; see
  Phase D, which is now much better specified.

### Phase C — the Battle Game

Everything below is decoded in `SATURN_VERSION_ANALYSIS.md` §4d-§4f.

**Status 2026-09-29: the playable Battle Game sequence is ported through all 15
arenas.** Choosing the title item starts at `8010` with the Saturn's loadout and
arena art; clearing a fight shows the centered `STAGE CLEARED` banner for the
120-frame pause, carries HP forward, and advances to the next room. The elapsed
`MM:SS` counter is drawn from the normal gameplay path. The sequence and its HUD
have not yet been play-tested by the user. The results/ranking screen remains
open; the Battle Game's music is in (below).

- **Done — the unlock** (`src/game/saturn/SaturnBattle.*`). Save byte `0xBF` in
  the Saturn; here `BioCardLayout::saturnBattleUnlocked` at card offset `0x20F`,
  which was documented padding with no references in the PC binary. Raised
  unconditionally by the ending (`ENDING.PRG` main `0x0602DC28` — any ending
  counts) immediately before the ending's save write, so it reaches disk the way
  it does on the Saturn. A NEW GAME resets the card from `data\bio_card.dat`,
  which the Saturn never does, so `saturn_battle_preserve()` re-raises it in
  `game_start` beside `dc_apply_mode_flags()`.
  **Port decision, deviation from the Saturn:** the Saturn reads its single save
  file at the press-start screen; the PC has eight slots, so the item follows
  the card in memory — set by the ending or the cheat, and picked back up by
  `RestoreSaveBlock` when a save is loaded. Loading a *different*, older slot
  therefore hides the item again, which is the closer of the two behaviours.
- **Done — the cheat → a config key.** The Saturn also unlocks the item with
  **X + Y + Z + Start on pad 2** during the press-start wait (`TITLE.PRG`
  `TitleMain` `0x0602DD70`, while `g_TitleState` == 4 and the flag is still 0).
  **That is not ported.** The PC has no second controller, and making a player
  hold four keys to reach a mode is the wrong shape. The replacement is
  **`[Game] BattleGame=1`** in `config.ini`, read in
  `saturn_battle_is_unlocked()`. It is a **read-time override only** — it never
  writes the card byte, so a save made with it on still records only what the
  game itself earned — and it is gated on `g_bSaturnMode`, so it does nothing
  in the other two modes. It also removes the awkwardness that the flag has to
  be polled *after* the title screen was drawn: config and card are both settled
  before `init_title_screen()` runs, so the item's page is built in the same
  place every time.
- **Done — the third title item.** `g_titleSelectionId` 3, the cursor wraps to
  it, and it becomes the default when there is no save — the Saturn's
  `0x30`-vs-`0x20` default. Its row is a **second texture page in slot 13**,
  the same slot the Director's Cut's submenu uses.
  **Port decision, not the Saturn's art:** the row is rendered from the port's
  own `data\FONTUS.TIM` 8x14 glyph grid into a 256×64 4bpp page
  (`saturn_title_build_text_page`). The Saturn's own menu art lives in
  `TITLE.BIN`, which is **not decoded** — the file is 512 KB: two 320×240 title
  bitmaps in three palettes, then texture data, and there is no font in it
  (`TitleMenuCursorDraw` `0x0602BDCC` is the reader, still unread). Importing
  it later is a change to that one function.
- **Done — starting a run.** `STAGE8` (`g_stageId` 7, which the Director's Cut
  arrange machinery already folds to base row 0, so no stage-indexed table grows)
  and **room byte = the arena number** — arena 1 is room `0x01` and loads
  `ROOM8010`, arena 15 is room `0x0F` and loads `ROOM80F0`. That is not a detail:
  the port's `LoadRoomRdt` builds `room<stage><roomHi><roomLo><variant>`, so
  the room byte is split into two nibbles and reading the two middle digits of
  the filename as one byte asks for `ROOM8100`, which is in no tree. Getting it
  wrong is a **crash**, not a wrong room: the load fails, `LoadRoomRdt` keeps the
  previous room's buffer, and `room_set` then walks it — camera pointers,
  `vab_sound_file`, the omodel and item tables — until it hits unmapped memory.
  Two things guard that now, and both are port-only because the original never
  meets a missing room file: `LoadRoomRdt` records the failure and `room_set`
  abandons the room with a message (`Room_RdtLoadFailed()`).
  The Saturn's per-character loadout (`battle_run_init` `0x06088698`) and the
  arena's spawn point are applied in `game_start`, in the window between
  `SetInitialItems()` and `LoadHeldItemsImages()`.
  **Fixed 2026-09-29: the loadout is the ITEM BOX.** `battle_run_init` fills
  the 48 box slots (`0x002F8724`), not the inventory, and GAME5's
  `SetInitialItems` zeroes the inventory in battle mode; the run also sets HP to
  the maximum and clears `+0xDC` bits with `&= 0x9D`. The first port put the
  list in the inventory and kept the loaded save's box (campaign items) and HP.
  `battle_apply_loadout()` now runs `InitPlayerData()`, empties the inventory,
  unequips, and loads the box from GAME5's own lists (the verifier compares them
  with `0x06088660` / `0x06088677`).
  **The stage and the room are set in `game_start`, not from the title.**
  `InitializeGame` rewrites the whole 1052-byte card from `data\bio_card.dat`
  before it does anything else, and `g_stageId` (card `+0x200`) and `g_roomId`
  (`+0x201`) live in it — so a title-side assignment is discarded and the run
  loads the *new game's* room `106` with arena 1's background, which is exactly
  what the first play-test did. `saturn_battle_start_run()` now sets only the
  flags; `saturn_battle_begin_run()` sets the stage, the room, the camera and the
  variant bit, after that memcpy and before `init_room()`. The verifier asserts
  the ordering both ways.
- **Done — the third title item → the load screen.** On the Saturn this is
  `TITLE` (mode 2) → `BU_LOAD` → `GAME5`, and the middle step is **not
  optional**: `BU_LOAD` is the load screen, and the save picked there is what
  the run starts from. `battle_run_init` (`GAME5 0x06088698`) takes no arguments
  and reads the character block of the save that was just loaded, so the run
  plays as whoever that save was. The port therefore runs **the LOAD GAME path
  with battle mode armed**, as its own state, `saturn_battle_load_state`; the
  title item chains it rather than `game_start`. Chaining `game_start` directly
  takes the NEW GAME path, which resets the card from `data\bio_card.dat`, loses
  the character, and shows the new game's narration. The load screen's chosen
  slot sets `MSF_CONTINUE_GAME` and restores the card, so the run continues from
  it and `saturn_battle_begin_run()` then puts the stage, room, loadout and spawn
  on top. Battle mode is armed only **after** the load screen returns a save:
  its EXIT chains `title_state` from inside `LoadSaveGameState`, so an abandoned
  run leaves nothing set for the next LOAD GAME.
- **Done — RANKING is a state.** Every run, finished or dead, leaves `GAME5` for
  `RANKING.PRG` (`GAME5 0x0602BD28`), which picks the next module itself
  (`0x0602E764`). The port matches that shape: `game_start` takes the outcome
  (`saturn_battle_close_run()`, before the run's state is cleared) and chains
  `saturn_ranking_state` — score and name entry, or the table alone after a
  death, then "save?" and "continue?". NO chains `title_state`; YES sets up what
  `title_state`'s entry normally leaves for the load screen (menu screen, the
  title sound bank its cursor sounds come from) and chains
  `saturn_battle_load_state`. `title_state` itself holds no Battle Game logic.
- **Done — the opening message** (`GAME5 0x0609E979`, in the global message
  table; the same bytes are in GAME1..GAME5, so it is common-source text, and
  arena `8010`'s init script has no message of its own). It **replaces the `0x5B`
  "NEW GAME" line** in `display_game_loading_message()` — on the black screen
  before the arena, where the Saturn's text is, not inside the room. (Shown from
  the game loop it appeared over the first arena, which is not where the Saturn
  puts it.) The port's `STR()` encodes to those exact 75 bytes —
  `verify_saturn_battle.py` re-encodes the C++ source and compares, so a
  reworded sentence or a dropped escape fails the check.
  `global_messages[]`; the PC build's 64 entries are all its own text and index
  63 is the one it leaves NULL.
  Reading trap: the Saturn's bytes contain `... the enemies` `03` `30` `and ...`,
  which looks like a stray capital T. It is not — `0x03` is the pause and the
  `T` belongs to the escape (`\pT`), which is why the port writes `\pT` too. The
  three trailing `0x00` are real spaces, and the `0x79` is the full stop, not a
  `\d` operand.
- **Done — the arena index is the room byte, not a stored counter.** Every table
  the run consumes is indexed by it: the arena rooms, the five-byte virtual
  doors, the per-room flags, the spawn records — and arena 1's own door is
  `dest=0x02` (`tools/dump_init_doors.py` on `ROOM8010.RDT`). So
  `saturn_battle_arena_index()` returns `g_roomId`, clamped to 1..15, and
  nothing is stored. A stored counter has to be advanced by the room transition;
  when it was not, the room changed and the index did not, so **every later
  arena loaded arena 1's background**. The background and the enemy sound banks
  both take their arena from `saturn_battle_asset_room()`, so they cannot
  disagree with it.
- **Done — the arena's assets.** Every arena is a copy of one campaign room, and
  the game carries the table (`GAME1` `0x06092B5C`, 15 u16). Three readers swap
  the arena for it under battle-mode bit 31 — `LoadRoomBss`, `LoadRoomSeBank`,
  `LoadRoomPcmBank` — and the port now swaps it in `load_room_bg` (both
  branches) and `Room_LoadEnemySoundBanks`. `LoadRoomRdt`'s variant digit is
  forced to 0, as the Saturn does. **Lighting needs nothing:** the PC's light
  data is inside the RDT, and the arena RDT is the Saturn's own.
- **Verifier**: `tools/verify_saturn_battle.py` — both tables re-read out of
  `GAME1.PRG` / `GAME5.PRG` and compared to the port's, each arena's campaign
  background pak present, the constants, the card offset, every call site
  gated, and `saturn_battle_end_run()` called before `game_start`'s three exits
  (the redirect is keyed on the flag alone, so a flag left set would put the
  next campaign game in arena 1's room).

- **Done — the arena sequence** (Phase C.3/C.4's playable part), the per-frame
  machine `GAME5 0x0608883C`:
  - **The fight-room table** (`0x060959A0`) is **17 single bytes, one per room,
    bit 0 = "this arena has enemies"** — not u16 pairs, which is what §4e's
    "17 bytes" first invites and which yields an irregular table matching
    nothing. Bit 0 is clear at exactly arenas **1, 6 and 11**, the three rest
    rooms, and those are the only arenas with a door record (`dump_init_doors.py`:
    `8010→8020`, `8060→8070`, `80B0→80C0`). So a run is rest → four fights →
    rest → four fights → rest → four fights → end.
  - **The fight-room bit follows the ROOM, not the transition**
    (`battle_sync_arena()`, `SaturnBattle.cpp`). Arming it only at run start and
    inside the warp is a softlock: **two of the fifteen transitions are doors,
    not warps** — arenas 1, 6 and 11 are the rest rooms and the only arenas with
    a door record (`8010→8020`, `8060→8070`, `80B0→80C0`), so a run leaves arena
    1 on foot and `room_transition_load` knows nothing about the Battle block.
    Arena 1 never clears, so the warp never runs, so arena 2 was entered with
    the bit clear — and the state-0 clear test requires it, so killing all three
    of arena 2's zombies did nothing. The bit is now re-read for whichever arena
    is current, every frame, and `battle_sync_arena()` is its only writer.
  - **"enemies remaining" is NOT the raw `g_enemy_count`** — it is derived from
    it every frame, the way `GAME5 0x06055474` does: the room's enemy-slot count
    minus one per slot whose health has gone negative
    (`battle_enemies_left()`, `SaturnBattle.cpp`). `g_enemy_count` on its own is
    the number of slots `cmd_enemy_set` allocated and **nothing ever decrements
    it** — a corpse keeps its slot with `status_flags` bit 0 up for the rest of
    the room, and the whole engine depends on that invariant — so reading it
    directly meant the clear test could never come true and every run
    softlocked in its first fight arena. See §4e's "How `+8` is maintained".
  - **Clear:** a fight room, "enemies remaining" at 0, player alive. HP is
    pinned up to at least 1 and saved, shown at full for the pause, and **the
    real value is restored after** — HP carries between arenas, and the score
    pays for what is left. **"STAGE CLEARED"** appears (`GAME5 0x060A10B0`:
    thirteen HUD-font bytes, five glyphs, a space, six glyphs — the whole
    sentence, no exclamation mark).
  - **The pause** is `0x78` = 120 frames; the **out-of-ammo grace** is `0xB3`
    frames of "can still fight" with no usable ammo. Usable ammo is loaded
    rounds plus the spare item, and each spare only counts when its weapon is
    held.
  - **The advance** is arena + 1, **through the door path** (fixed
    2026-09-29). GAME5's transition routine has a battle branch that feeds a
    5-byte virtual door record (`0x06095950`, indexed by the arena being left)
    into the same globals a door record fills, so the warp plays a door
    animation and door sound. `battle_advance_room()` builds that door record
    (`kArenaVirtualDoor` + the spawn table, y = 0) and hands it to
    `door_begin_transition()`; the run sits in state 2 until the room changes.
    The first version called `room_set()` straight from the game loop — no
    animation, and the player's Y / animation carried over from the last arena.
  - **The run clock** counts the whole attempt (not per arena — the time bonus
    is against 720 s / 420 s), saturates at 99:59 like the Saturn's frame counter,
    and is drawn once per gameplay frame. The Battle path bypasses the unrelated
    self-destruct countdown branch. The Saturn's own clock layout is not decoded;
    the PC uses the existing USA/JPN `PrintText8x14` timer position.
  - **Port decision:** the Saturn calls its HUD-font renderer directly at
    `(112, 112)` for `STAGE CLEARED` (`GAME5` `0x060888B2` → `0x0605B8D8`), not
    the message system. The port draws the same words centered at y=112 through
    `PrintText8x14` during the clear pause, using the active font's glyph width.

- **Implemented — the Battle Game's music; in-game listening test still
  required.** The Saturn plays sequenced music through Sega's sound driver; the
  format, the driver's sequencer and key-on, and the SCSP model are in
  `SATURN_VERSION_ANALYSIS.md` §6a. The headless renderer produces the three tracks,
  and its key-on registers match the running game's SCSP, but the rendered WAVs
  have not yet been listened to in a game session.
  - **Migrator:** the Saturn tab's "Battle Game music" renders
    `SATURN/Sound/Bgm_58` (`BATTLE.CDP` song 0, the arena loop),
    `Bgm_59` (song 1, the end-of-run fanfare), `Bgm_24`
    (`BGM34B.CDP` song 2, the arena 15 Tyrant loop) and `Bgm_5a`
    (`NAME.CDP`, for the ranking screen once it exists), WAV or Ogg, with the
    loop region and marker the game's loader honours, plus
    `Sound/AUDIO.TXT` so the game mixes at 44.1 kHz in Saturn mode.
  - **Game** (`SaturnBattle.cpp`, `SoundSystem.cpp`, `RoomInit.cpp`, all gated
    on `g_bSaturnMode && g_saturnBattleActive`): the arena's BGM group and
    room state come from GAME5's own tables (`0x0609ACCC`, `0x0609ACDC`) -
    before this the arenas folded onto stage 1 and played the **mansion's**
    room music. Group `0x40` names the two Saturn files; the rest rooms play
    the PC's group `0x0F`, arenas 5/8/10 groups `0x06`/`0x27`/`0x23`, arena 15
    group `0x34` channel 2 (`Bgm_24`, not the PC whole-file `Bgm_24`).
    After the Tyrant, the fanfare plays
    once (`bgm_play_group_channel(0x40, 1)`, GAME5 `0x06088BDE`).
  - **Required in-game play-test:** migrate with the Saturn tab (music checked), run a Battle Game:
    arena 1 is the save-room music, arena 2 starts the Saturn battle theme and
    it carries on across arenas 2-4 without restarting, arena 5 changes, ...,
    arena 15 the Tyrant theme, then the fanfare. Without the migrated files the
    `0x40` arenas are silent and the log says `FAILED TO LOAD`.
  - **First test (2026-09-29): no battle theme.** Two causes, both fixed. The
    overlay probe in `ResolveAssetRoot` tried only the literal `.wav`, so an
    overlay holding just `Bgm_58.ogg` fell through to the base tree, which
    has neither file; it now also accepts the `.ogg` sibling. And the arena
    advance (`battle_advance_room`, no door) never called `update_room_bgm`, so
    the BGM only ever changed at the rest rooms' doors.

- **Fixed 2026-09-29, needs a play-test — first play-test's four reports.**
  1. *No door animation between arenas* → the warp now goes through the door
     path (above).
  2. *Wrong weapons, campaign items in the box* → the loadout is the item box
     and the inventory starts empty (above).
  3. *Arena 5's Yawn takes too many shots, odd AI* → `SATURN_VERSION_ANALYSIS.md`
     §4g had both battle-bit branches **inverted**: the Battle Game Yawn has
     350 HP and cannot flee; the Saturn campaign Yawn is the PC's (3050, flees).
     The Saturn's per-hit damage is a flat 15 and its bite a flat 28 with no
     `SECOND_PLAYTHROUGH` test (all Saturn modes); with the PC rule a run
     started from a cleared save did a third of the damage. No venom in the
     Battle Game. What still looks "not attacking" may just be the flinch: every
     hit that lands outside a bite drops the Yawn into its damaged state.
  4. *Arena 11: player placed badly, invisible, no control* → **not reproduced
     statically.** Its spawn `(4600, 2500)` is inside camera 0's own region and
     next to its door, and the room has no per-frame script. The old warp ran
     `room_set()` mid-frame with the previous arena's Y, animation and pending
     player state; the door path resets those the way the Saturn does. Re-test
     arena 10 → 11; if it still fails, note whether the HUD clock still runs and
     whether the screen is black or shows the room.

- **Second play-test (2026-09-30), all four implemented, needs a play-test.**
  1. *Control during the clear pause* - state 1 now clears `g_message_flags`
     bit `0x100` every frame (GAME5 `0x06088B20`), which locks the pad and the
     menu until the door transition restores the flags.
  2. *Zombie Wesker in arena 12* - a model swap, not a type: slot 0 loads
     `em1017` and keeps its zombie id (`saturn_battle_model_override`,
     RoomInit's enemy model loop). Behaviour 9 is the chase on the Saturn
     (`saturn_apply_zombie_tables`), doubles the HP and bites for 12; in Saturn
     mode every zombie bite uses the Saturn's single damage row.
  3. *Golden Tyrant* - arena 15's models get the +4/+3/-5 CLUT tint as their
     TIM is loaded (`saturn_battle_tint_model_tim`), and the arena Tyrant has
     **700 HP**, starts out of the capsule, and cannot be parked by the room
     script (`saturn_tyrant_battle_init` / `_update`). Its music is the stock
     `Bgm_24`: the `Bgm_24` override is removed (the Saturn plays the
     stock theme; the override only made the arena silent when the file had not
     been migrated - the overlay predated the migrator entry).
  4. *End of run* - black background (standalone screen mode, room sprites
     off), the orbit camera from GAME5 state 4, the fanfare, then the results:
     score screen -> ranking with in-place name entry ("AAA") -> save prompt ->
     title. **Port decisions**: the screens are drawn with the port's font and
     solid rectangles after the Saturn's layout (its art is in the undecoded
     `BATTLE.BIN` / `WAPON3.BIN`); the name alphabet is A-Z, space, full stop;
     the records are kept in `<save dir>/batrank.dat` (the Saturn uses save
     bytes `0x00`-`0x9F`, and the PC card has no room); ammo left is counted
     from the inventory and box, equal to the Saturn's start-minus-shots
     counters because nothing in the run gains or drops ammo; the run clock
     stops at the Tyrant's clear (the Saturn advances it in the HUD draw, which
     the end sequence does not run); `Bgm_5a` starts with the ranking.

- **Third play-test (2026-09-30), implemented, needs a play-test.**
  1. *Wesker survives the magnum* - GAME5's zombie hit reaction exempts id 1 +
     behaviour 9 from the head explode and flashes him instead
     (`WeaponDamage.cpp`).
  2. *Arena 15's tubes* - the Battle Game's row of the OBnn table has no model
     in any fight arena; `saturn_omodel_has_no_model` now answers from it.
  3. *The results screens are the Saturn's own*: backdrop BMP, both fonts, the
     label strips and palettes come from `BATTLE.BIN` / `WAPON3.BIN` (the
     migrator's Saturn tab now copies both into `SATURN/Data`), laid out at the
     cells RANKING.PRG writes, with the 8-frame white/yellow flicker. The ammo
     and spray counts are the Saturn's counters (start totals, -1 per shot and
     per heal), not the inventory. Records keep their time in frames (the file
     is now `batrank.dat` "BRN2"; an older file is ignored).
     **Approximations**: the life bar is flat fills in its art's colours at
     the Saturn's place and size (it does not move - `0x0602C204` uses
     constant coordinates); the save prompt is
     spelled in font B (`SAVE2.BIN` not ported); the name alphabet is font B's.
  4. *Music* - `Bgm_5a` starts with the score screen.

Still to do:

1. SAVE2.BIN's prompt and the Saturn's title-row art (`TITLE.BIN`).
2. The score/ranking screens' exact colours, the name-entry character set and
   the save prompt's wording are not read from `RANKING.PRG` yet.
3. Other Saturn audio: the campaign BGM arrangement, SE banks, room PCM banks,
   `BATTLE.BIN`'s four unverified streams. (The Tick's exclusive sounds: rendered from `SE308.CDP` as `Tk_*` and loaded over the Hunter's `He_*` slots 0-7, and slot 8 for the stab, in stage 2 by `saturn_tick_snd_name`. **The slot → song mapping is the bank's own table, not "song = slot"** (that first guess played a door-like thud for the footstep): `Snd_em(n)` looks up key `0x0800+n` in the 8-byte records at `SE308.CDP +0x800` (loaded to `0x002E8430`), song = id − 768, giving slots 0-8 → songs 9 10 5 8 6 3 4 7 16. Fixed 2026-10-04; re-run the Saturn tab or `re1am_selftest saturn <image> <target>` to regenerate — the creatures, outfits, arenas, Tyrant room and Battle/Tick sound are always written now; untested in game.)
4. The Saturn's own title-menu art (above).

### Phase D — the room props and the lab's second Tyrant

**Status 2026-09-29: the lab's second Tyrant and the lab room are done and
play-tested** (§4: the fight, the backgrounds, the drained tanks, the `OB80`
cover). What remains of Phase D is the `A_*.DAT` props of *other* rooms; their
**format is decoded** and their **asset load path is traced** (§2), so that is a
conversion and a spawn rather than a decode. Open items are listed at the end of
this phase.

**Open 1 — RESOLVED 2026-09-28: the capsule's placement.** It is not in the prop registry
at all. GAME4 `0x06055AA8` (per-frame room-object update) special-cases stage 4, room
`0x13`, omodel slot 0: active byte = `!flag(g_ScenarioFlags, 0x72)`, position
(`0x1CB6`, `-0x1000`, `0x738C`) written to both the word (`+0x6C/6E/70`) and long
(`+0x34/38/3C`) copies, yaw `0x238` at `+0x74`. `0x060A74B0` is the RDT's item-record
array, not a prop list (see below). Omodel slot 0 is the tank's water surface (drains when the Tyrant escapes; user-confirmed), so `0x72` = Tyrant escaped and the capsule body is the `A_TRS1` billboard. Port work: in Saturn mode apply the same override to
`g_omodel_table[0]` in ROOM5130 each frame. Flag `0x72` is set by a Saturn-only
routine at GAME4 `0x06078AF8`. Superseded text follows, kept for history.

**Ported and play-tested (2026-09-29):** `src/game/saturn/SaturnRoomObjects.cpp` applies the slot-0 override
each frame in Saturn mode (called from `GameLoop.cpp` before `update_room_objects`). Flag
`0x72` (`g_ScenarioFlags`) is set when the second Tyrant is re-raised — GAME4 `0x06078AF8`,
step 2 of the Tyrant's own release routine `0x06078954`, now ported (§4.3). **Correction
2026-09-29:** that routine was earlier read as "the handler for entity type `0x2E`, the
Wesker actor", from a pointer in a table assumed to be the type table. It is not: the real
type table is `0x06096848`, type `0x2E` runs the shared NPC driver `0x0606059C`, and
`0x06078954` hangs off `tyrant_update`. `+0x188` is a frame timer, not a script command.
(Later reading, §4.4: slot 0 on the Saturn is `OB80.DAT`, the dark capsule
cover, not the water and not `A_TRS1`; it is migrated and drawn.)

**Open 1 (old) — the prop's room transform.** The capsule's position and rotation are
not in the mesh. The per-room prop list is an array of descriptors at
`0x060A74B0` (GAME4) — **BSS**, so built at runtime — whose `+20` field is the
model index; the append loop reads only that field, and the 40-byte record's
remaining 28 bytes are written elsewhere. All 16 references to the array in
GAME4 are **loads, zero stores**, so the writer uses a different addressing form
or lives in another module. The lab capsule's collision cylinder (x `0x1BD0`,
z `0x7400`, r `0x923`) is the only placement value recovered, and its scale is
unconfirmed.

**Open 2 — RESOLVED 2026-09-29: there is no spawn.** The second Tyrant is the
first one's entity, re-raised by the Saturn's Tyrant code itself (GAME4
`0x060788A8`, called from the end of `tyrant_update` `0x060747C0`); see §4.3.
Nothing carries a type field because nothing is spawned — which is why the
searches below found nothing. They are kept because both negatives are real:

* *Not the room data.* Decoding the Saturn's own `STAGE5/ROOM5130.RDT` gives
  three `enemy_set` records with exactly one of type `0x0C` — the same three the
  PC has. (The Saturn's RDT is 478,372 B against the PC's 546,948 B, so the
  headers do differ; the enemy records do not.)
* *Not an immediate-typed spawn.* All five `GAME` overlays were disassembled in
  full — ~1.15 M instructions — and every `mov #12,rN` near a `cmp/eq #4` or
  `cmp/eq #0x13` is either dead or unrelated UI logic. **This has to be scanned
  on real instruction boundaries**: raw byte matching produced three
  confident-looking false positives, because those opcodes occur constantly
  inside immediates and PC displacements.

(The old "spawn descriptor beside `0x060A74B0`" hypothesis is withdrawn.)

**Then, on the PC side** (this is the part the Saturn does *not* mirror, so it is
a port decision):

* `assets/USA/objspr/OSP*.PAK` are **camera masks, not object models** — do not
  write one. The room's props come from *inside the RDT*: `object_models` at
  RDT+`0x50` is `omodel_slot_count` `{TMD*,TIM*}` pairs, and the 0xA4-byte
  records are carved from the RDT's own buffer. `ROOM5130`'s count is **7** and
  the init script already uses slots 0-6, so there is no spare slot to point at
  the capsule; the port needs either a grown omodel block or a Saturn-owned
  registry beside it. The Saturn's own answer is a **separate** 40-byte
  extra-model registry, which is why its rooms stay byte-identical to the PC's
  while it still adds props.
* `saturn_mbn.py` needs a `pc` command emitting the capsule as a PC TMD+TIM, the
  way `saturn_emd.py pc` does for creatures.

**Still open after the lab (2026-09-29):**

- The other `A_*.DAT` props (`A_TRS2`, the extra plants, the kitchen machine) —
  decoded, not migrated. Go through the per-room `OBnn` table (§4.4) first: it
  is how the Saturn actually places room models, and the lab showed that the
  RDT's omodel TMDs are not what it draws.
- GAME4 lab room `0x06`: its `OBnn` row is all zero (3 slots), i.e. the Saturn
  draws none of its omodels. Read, not live-verified, not applied.
- `OB80`'s own light setup (`0x060387F8`); the PC lights the cover with the
  room's lights. Visually matches, so low priority.
- The Tyrant init's Battle Game branch (`0x060746A2`) — part of Phase C.
- Remaining Saturn audio (`CD_PACK` campaign BGM/SE and room PCM, `X68 SSCONV`
  `.BIN` banks, and the Tick's exclusive sounds). The Battle Game's own tracks
  are migrated by the Saturn tab (§3).
- Optional: the `BM60` title/ending art. (`MOVIE.CPK` is migrated by the
  Saturn tab's FMV option; `SATURN_VERSION_ANALYSIS.md` §6.)

Any Saturn asset goes through the asset migrator, converted to a format the PC
engine reads as it is (uncompressed); the game never decodes Saturn data.

---

## 4. The second Tyrant (added 2026-09-28)

### 4.1 It is not room 515 — and it is not the PC's room either

The second Tyrant is in **the same room as the first**: Saturn
`STAGE5/ROOM5130.RDT` = PC `ROOM5130`, Laboratory B4, `ROOM_MAIN_LAB` = stage
index 4 room `0x13`. Chris' route runs the fight twice; Jill's route once.

`ROOM5150` is a different room, and nothing about it is a boss:

| | |
|---|---|
| Saturn file | `STAGE5/ROOM5150.RDT`, decoded |
| init SCD | `door_set 00/01`, `state_byte_test 03 00 0C`, `enemy_set 23 00 FF …`, `scd_event_create 00 09 00/01` |
| what it is | the room where **Chris meets Rebecca** (`23` = `NPC_REBECCA`); doors in from rooms `0x0C` and `0x14`, out to `0x0C` and `0x14` |

Its SCD on the Saturn is **byte-identical to the PC's**, and that is the general
case: all 348 shared rooms decode byte-identical to the PS1's, so **no Saturn
room script can carry Saturn-only content**. Whatever the Saturn does for the
second fight, it is in the modules. A sweep of every PC room's `enemy_set` list
finds exactly two Tyrant rooms in the whole campaign — stage 5 room `0x13`
(`0x0C`, the lab) and stage 3 room `0x03` (`0x10`, the final Super Tyrant) — and
`ROOM5130`'s **three** `enemy_set` records are the PC's three: the Wesker
cutscene actor `0x2E` in both branches of the room's flag test, and the **first**
Tyrant `0x0C` (`0c 40 70 01 02 00 00 00 0c 00 00 cc 29 00 00 58 1b 01 00 00 00`)
only in the branch guarded by SysFlags `0x01000000`.

**But the Saturn's room is not the PC's room.** The two are different builds, so
every RDT section has to be compared rather than the script alone, and one does
differ. Comparing the two `ROOM5130.RDT` files section by section on the header
layout in `Types.h` (`cam_switch_zones` 0x48 … `vab_header_file` 0x8C), with the
PC's 8-byte header shift accounted for:

| RDT section | Saturn | PC | |
|---|---|---|---|
| `+0x48` `cam_switch_zones` | 114,820 B | 114,820 B | identical |
| `+0x50` `object_models` | 115,132 B | 115,132 B | 35 bytes differ — all relocated pointers |
| `+0x58` `walk_zones` | 160 B | 160 B | identical |
| `+0x5C` `footstep_sound_zones` | 24 B | 24 B | identical |
| `+0x60` `initialization_scd` | **572 B** | **600 B** | **one extra command** |
| `+0x64` `scd_opcodes` | 1,652 B | 1,652 B | identical |
| `+0x68` `scd_opcodes2` (events) | 3,200 B | 3,196 B | relocation only |
| `+0x70` player anim pair | 2,252 B | 2,252 B | identical |
| `+0x74` `messages` | 820 B | 820 B | identical |
| `+0x78` `item_icons` | **1,200 B** | **2,400 B** | **one 40×30 icon fewer** |
| `+0x7C` `effect_anim_index` | 1,952 B | 1,952 B | 3 relocated pointers |
| `+0x88` `sound_attribute_table` | 192 B | 192 B | identical |

The 28 extra bytes of init script are exactly one `item_model_set` record:

```
18 item_model_set  04 70 17 30 75 D0 07 D0 07 0B 0F 01 FF 0C 1A 00 00 92 78 80 0A CF 81 01 82
```

on the PC only, and the icon table agrees: 1,200 bytes is one 40×30 8bpp item
icon, 2,400 is two, and the Saturn's first icon is byte-identical to the PC's.
So **the PC's lab room has one item the Saturn's does not** — the pickable
magazine clip.

**So the port overlays the room, it does not patch it.** The Saturn's
`STAGE5/ROOM5130.RDT` is LZ77-decoded into
`SATURN/Stage5/ROOM5130.RDT` (Chris) **and** `ROOM5131.RDT` (Jill), each from
its own disc file (the disc has both; they differ, and copying Chris's to Jill's
name made Jill's lab trigger Chris's cutscene), and the engine picks it up through the sparse overlay at
`LoadFile`'s existing `ResolveAssetRoot` call. The clip then simply is not
placed, because the Saturn's script has no record for it — no flag clearing, no
engine special case, and as a bonus the record that indexed
`g_item_model_table[0xFF]` on this table of 8 (§4.4) is not executed in Saturn
mode at all.

Nothing else in the room differs in content, so the second Tyrant, the corpse
dissolving and the terminal are still not in the room data: they are in the
Saturn's modules, which is why they are not found yet.

**The mask is in the room, and it is the same on both sides.** The room's mask
art is not really an `objspr` file: `RDT_Camera::tim_mask_pointer` (offset +4 of
each 0x2C camera record) points at a TIM inside the RDT, and the PC's
`objspr/OSP04190.PAK` is that TIM LZW'd — `pak_view` decodes the pak to 16,928
bytes, which is exactly camera 0's block. Resolving those pointers in both
files and comparing the blocks gives **0 differing bytes in all seven** (16,928 /
22,048 / 15,904 / 18,976 / 11,808 / 9,760 / 9,744 B). So the mask is not a
Saturn-vs-PC difference either, and the port's arrange-room path
(`load_room_masks` reading `tim_mask_pointer` directly) would read the same art
for this room.

### 4.2 The Saturn re-drew the room's backgrounds — `saturn_bss_to_pak.py --verify`

The Saturn's `.BSS` is 320×240 **8 bpp** with a per-camera 256-entry RGB555
palette; the PC's background pak is one 16 bpp TIM inside the game's own LZW.
`tools/saturn_bss_to_pak.py --verify` decodes every Saturn camera frame, decodes
the PC pak for the same room **and camera** through `unpack_pakfile_`'s format,
and reports the frames whose luma differs by more than 16 on more than 5 % of
pixels (the Saturn's palette runs a step dimmer, so an unnormalised comparison
calls every room "different"):

```
655 Saturn camera frames, 115 differ from the PC pak, 8 have no PC pak
   Saturn-only: STAGE1 room 05 camera 9
   Saturn-only: STAGE3 room 03 cameras 8, 9, 10
   Saturn-only: STAGE3 room 06 camera 2
   Saturn-only: STAGE4 room 01 cameras 6, 7
   Saturn-only: STAGE4 room 06 camera 7
```

The calm frames come out at 0.0–3 % — pixel-identical art behind a slightly
different palette. The room that matters is the sharpest outlier:

```
DIFFER STAGE5 room 13 camera 0: 55.7 %   camera 3: 61.3 %
DIFFER STAGE5 room 13 camera 4: 64.8 %   camera 6: 86.4 %
DIFFER STAGE5 room 15 camera 0: 70.2 %
```

`ROOM513.BSS` is re-drawn on **all eight cameras**. Side by side
(`bg513/ROOM513_03.png` against `assets/USA/Stage5/RC5133.png`) it is the same
shot of the same room with the **tall tank vats emptied**: the PC's are
translucent green cylinders, the Saturn's are dark, and the small floor tanks are
unlit on both. That is the same reason the rest of the Saturn release looks
"cleaner": it cannot draw the PC's translucent glass. (`ROOM515`'s 70 % is a
different room's art entirely — a grated floor with a body on it — and is not
part of this fight.)

So `assets/SATURN/Stage5/RC5130..5137.pak` carry the Saturn's own art, and
`load_room_bg` picks them up through the sparse overlay with no code change.

### 4.3 How the second fight works — the Saturn's own Tyrant code (2026-09-29)

**It is all in the Tyrant's update, not in the room and not in a separate
enemy.** The room is the PC's `ROOM5130`; its one type-`0x0C` record spawns the
one Tyrant exactly as on the PC, and the Saturn raises that *same entity* a
second time. GAME4 addresses (module loaded at `0x0602B000`; the Ghidra program
`/SATURN_PRG/GAME4.PRG` is rebased there and carries these names):

| GAME4 | what | PC twin |
|---|---|---|
| `0x06096848` | the real enemy-type table (48 entries); `[0x0C]` and `[0x10]` → `0x060747C0` | `enemies_update_functions_tbl` |
| `0x060747C0` | `tyrant_update`; its tail calls `0x060788A8` while `+0x186 & 4` | `tyrant_update` 0x00421990 |
| `0x060744C8` | `tyrant_init`; clears `+0x184..0x186`, sets `+0x186 \|= 8` when the player is Jill (player id bit 0) | `tyrant_init` 0x004212b0 |
| `0x0609A914` | state table; `[5]` is a bare `rts` | `0x004ba280` |
| `0x06074C8C` | state 3 (death entry): `0x03010200` → behaviour **2**; id `0x10` → 1; `+0x186 & 8` → 0 | `tyrant_state_forced` (always 0) |
| `0x0609A994` | behaviour table, **17** entries: `[0]` restrained, `[1]` yield-to-SCD, **`[2]` the dissolve (new)**, then the PC's `[2..15]` shifted by one; state 1 indexes it at `+3` | `0x004ba308`, 16 entries, `+2` bias |
| `0x06077804` | behaviour 2, the dissolve | — (Saturn only) |
| `0x060788A8` | the release: `+0x184` → `[0x060788D0 watch, 0x06078954 cutscene]` (table `0x0609AA34`) | — (Saturn only) |
| `0x0605CB38` | the second capsule's collision cylinder | — (Saturn only) |

**The dissolve** (`0x06077804`, sub = `+0x87`, timer = `+0xC4`). Sub 0: the PC's
collapse animation 8, **BGM ch 2 stopped and ch 0 / 1 restarted** (room group
`0x34` = `Se_55`, `Bgm_20`, `Bgm_24`). Sub 1: animate, `Snd_em(6)` on frames 25 /
50, then `+0x186 |= 4` (the second Tyrant is armed from here). Sub 2 (timer >
`0xBF`): look-at and shadow off, all 15 joints frozen (`flags |= 8`, which
`rotate_entity` turns into `0x50`). Sub 3: the frozen joints shrink
(`ScaleMatrix` by `(0xFB0, 0x1004, 0x1004)`, one more joint each even frame in the
order `0x0609A9EC` = 1,0,2,6,3,12,9,7,4,13,10,8,5,14,11,0), each joint in turn
tinted `0x706050/0x507060/0x506070`, and from timer `0x120` a 16-step palette
fade. Subs 4–5: all joints squash by `(0x400, 0xEE0, 0xEE0)` per frame, then
vanish one by one (`flags &= ~1`), the heart goes out. Sub 6: `status |= 0x0A`,
state 5. Every 8th frame of timer 64–288 a smoke billboard (type 9) rises off a
random joint. **It never raises the room's death flag** — only the second
Tyrant's death (the PC's behaviour 0) does.

**The release, watch** (`0x060788D0`): the point `0x258` in front of the player
(`RotMatrixY(angle)` of `(0x258, 0, 0)`) inside the rectangle at `0x0609AA44` —
x `0x170C..0x24B8`, z `0x0ED8..0x19D4`, the lab terminal — starts the cutscene.

**The release, cutscene** (`0x06078954`, step `+0x185`, timer `+0x188`):

| step / frame | action |
|---|---|
| 0 | camera 5, input off (`g_message_flags &= ~0x100`), `+0x186 \|= 0x10` |
| 1, t=3 | letterbox (`MSF_INTENSITY_RAMP`) |
| 1, t=4, 0x30 | glass hit `Play3DSnd(2, 0x19)` at (0x1B58, −0xFA0, 0x3E80) |
| 1, t=8 | Chris: player SCD behaviour 6, turn toward (0x3000, 0x7000), step 0x80 |
| 1, t=0x40 | BGM ch 0/1 stopped, **ch 2 (`Bgm_24`) restarted**, camera 0 |
| 2 | **re-raise**: `tyrant_init`'s field setup (no heart clone, no joint reset), joints unfrozen and shown, health 220, `+0x186 \|= 8`, `Flg_on(g_ScenarioFlags, 0x72)`, heart on, at (0x1C00, 0, 0x6600) angle 0x400, animation 6 fast-forwarded to frame 0x55, `play_sound_and_voice_effect(1, 0xB3)` + `MSF_VOICE_PLAYING` |
| 3 | the PC's `em_scd_behavior_break_glass` from frame 0x55: 53 shards on frame 0x59 (same variants and lifetimes as the PC's, every base moved to the second capsule, rotation + 0x800), shadow on at 0x91, `Snd_em(0)` at 0x8F / 0x97 |
| 4 | state `0x01000001` — the ordinary AI from here |
| 5, 6 | after 0x40 frames camera 1, after 0x80 more camera 2 |
| 7 | letterbox off, input on, release done |

Voice `0xB3` is the same clip as the PC's (`VB00_10`): the Saturn's stage-4 voice
table (`0x06097818`, 8-byte offset/size records) has `0xB4`, `0x51` and `0xAA`
sharing one clip exactly as the PC's `VB00_11` does, and the sizes match the PC
files in ratio.

**The capsule collision** (`0x0605CB38`): cylinder x `0x1BD0`, z `0x7400`, radius
`0x923` (a true `sqrt` distance), only when the entity's y ≥ 0, stage byte 4 /
room `0x13` (or Battle arena 15). The Saturn runs it on the **current entity**:
at the top of the player's update (`0x0602FF52`) and for every enemy slot at the
top of the enemy loop (`0x06063256`), before the handler.

**What the stand-in got wrong** (the port before 2026-09-29, for the record):
it re-raised the corpse when Chris walked *into the capsule* instead of at the
terminal; it put the smoke on the *new* Tyrant; it had no glass; it handed the
second Tyrant to behaviour 11 — the heliport Super Tyrant's eruption, which plays
voice `0x2D` (`V012_11`, a Wesker line in the lab's table) and the heliport's
room sounds; and it never restarted the battle BGM.

**Port substitutions** (in `SaturnTyrant.cpp`'s header): the Saturn's VDP1
Gouraud tint and CRAM palette fade become the PC's per-joint
`JointApplyColorTint`, scaled for the fade; the Saturn's unfreeze
`(flags & 0xAF) | 1` becomes `| 3`, because the PC's `rotate_entity` recomputes
only joints with bit 2; the dissolve's `rand() % 16` joint pick can land one past
the 15 joints, and that puff is dropped. SGL screen / VRAM calls
(`0x0608AC08`, `0x0603C580`) have no PC counterpart.

**Play-tested (the user, 2026-09-29)**: Chris, `Mode=SATURN`, the lab Tyrant
room. The first Tyrant collapses, the battle music gives way to the room's, and
the corpse shrinks, darkens, smokes and vanishes. At the terminal the cutscene
cuts to camera 5, pounds the glass, turns Chris, restarts the battle theme, and
the second Tyrant shatters out of the second capsule — all as on the Saturn.
Still worth a regression pass: Jill's Tyrant dies the PC way with no second one,
and `Mode=OG` / `Mode=DC` are unchanged.

### 4.4 The room's objects: the drained tanks and the capsule cover — done

**Update 2026-09-29 (read live in Yaba Sanshiro, lab room):** the Saturn does not
build omodels from the room file's TMDs. It loads one `ENEMY/OBnn.DAT` per slot
from a per-room table (GAME4 `0x060980F4` + stage·`0x10E` + room·9; loader
`0x060680F0` / `0x060681E8`), and a slot whose entry is 0 gets a null model
handle (registry `0x0609FF80` + 40·(1+slot), `+28`) and is never drawn. For
`ROOM5130` the row is `50 00 00 00 12 5A 03`:

* **Slots 1–3 (the PC's green water columns in the centre tanks) have no model**
  on the Saturn, which is why its re-drawn backgrounds show those tanks drained.
  Ported: `saturn_omodel_has_no_model()` (SaturnRoomObjects.cpp), checked in
  `render_room_objects`. The room masks were *not* the cause — they hold only the
  rails and tank bases.
* **Slot 0 is `OB80.DAT`, the dark cover over the second capsule** (not
  `A_TRS1`): LZ77-packed MBN (3-offset header `0x0C/0x150/0x158`), an opaque
  near-black (RGB555 `0x8001`, attribute `0xCC` = Gouraud + 16-colour LUT)
  half-cylinder shell with a bottom cap, 18 points / 12 polygons, drawn with its
  own light setup (0x060387F8: registry `+0` = `0xB4` = 100 + 80). It is the
  record the Saturn parks at the capsule and hides with flag `0x72`. **Ported**:
  its transform is hardcoded per model in GAME4 `0x060681E8` - position
  (-4, 1.5, 0) at `0x06068616`, scale (24, 44, 64) at `0x06068878`, rotation
  (0, 0x2580, 0) at `0x06068A00` (also read back live at the draw,
  `0x060386D0`), drawn as T_rec·R_rec·S·T_obj·R_obj, SGL unit = PS1/4. The
  asset migrator (`core/SaturnObModel.cpp`) bakes that into a PS1 TMD of
  mode-0x34 triangles in both windings (SGL Dual_Plane vs. the PC back-face
  cull) plus a flat 8 bpp TIM of palette entry 0 (`0x0001`), appends both to
  the migrated ROOM5130.RDT and points `object_models[0]` at them; projected
  through camera 0 it covers the pod window exactly. The engine shows slot 0
  until flag `0x72` when the room carries it (SaturnRoomObjects.cpp), and keeps
  it hidden with an older migrated room. Not reproduced: its own light setup
  (`0x060387F8`); the PC lights it with the room's lights. **Play-tested by the
  user (2026-09-29).**

Earlier notes, kept for history (some superseded by the above: the dissolve is
now the Saturn's own, §4.3, and the second capsule's visible body is `OB80`):

* **The green tank vats: found and fixed.** They were the **room mask**, and
  there are two copies of it. The mask art is the block at the camera's
  `tim_mask_pointer` inside the RDT, and the Saturn disc has no `objspr/`, so
  that is the only place its masks can be. The PC's `assets/USA/objspr/OSP04190.PAK`
  is a *different* copy of the same 16,928-byte block: 1,841 bytes differ, and
  rendered side by side the pak draws the vats pale green while the room's own
  copy draws them grey. Saturn mode now reads the mask from the room file
  (`load_room_masks`, the same path the DC's arrange rooms use); OG and DC keep
  the pak, which is the shipped and tested one. The vats' model and the room's
  `obj_flag_set` records are identical on both sides — omodel slots 0-3 all point
  at one TMD/TIM pair whose CLUT is the pale green `D3AC C368 …` ramp — so once
  the mask is right, the room reads as the Saturn's.
* **The second capsule's collision is ported** (GAME4 `0x0605CB38`: a cylinder at
  x `0x1BD0`, z `0x7400`, r `0x923`, this room only, pushed out after the
  player's move) — but the **capsule model** is still unmigrated
  (`STAGE5/ENEMY/A_TRS1.DAT`, 412 B of mesh, plus `A_TRS1T.DAT`, 2,560 B of
  texture), so the player walks into an invisible cylinder. Behind that is a
  structural issue: on the Saturn the capsule is drawn from its **own 40-byte
  extra-model registry**, while the port's renderer walks the room's
  `object_models` table, which has seven slots and all seven are used by this
  room's script. So the capsule needs a port-side slot, not just a model.
* The corpse dissolve is the port's three smoke billboards (§4.3), not the
  Saturn's own effect.

Full script and event audit of the imported room: `SATURN_VERSION_ANALYSIS.md` §4h.
The short version — the init script differs by the magazine clip, the frame SCD
is identical, the 16 event scripts differ by four hunks all inside script 8
(three reorderings and one extra deferred command), **neither side's events
contain a single `enemy_set`**, and every other section is byte-identical. The
room is fully ported, which is why the magazine clip was the only visible
change.

**A port bug found on the way** (still present in OG and DC): `cmd_item_model_set`
indexes `g_item_model_table` with operand `0x0C` of its record — the original
does the same — and ROOM5130's magazine clip record has `0xFF` there, against a
table the port declares as 8 entries. That is an out-of-bounds read and a write
through whatever pointer it finds, on every play of this room in every mode.
Overlaying the room removes it in Saturn mode; OG and DC still run it.

## 5. Rules this port must not break

- `Mode=OG` and `Mode=DC` stay byte-for-byte what they were; every Saturn branch
  is gated on `g_bSaturnMode` or a Saturn-owned flag. The engine support already
  added (multi-page textures, buffer headroom) is inert for stock assets.
- The base trees `assets/USA/` and `assets/JPN/` are never written to by the
  migrator; `SATURN/` is the mode's own content.
- `docs/MEMORY_LAYOUT.md` before any new global; add it to
  `ResetGameStateBlock()` if it lands in `0x00be41e0..0x00be9620`.
- Comment the original address of every function rewritten and every global
  declared. Saturn addresses are another program space: name the module, e.g.
  `// 0x0608883C (Saturn GAME5.PRG)`.
- **A Saturn address is only valid in the module it was read from.** The eleven
  overlays are separate compilations of the same source, so the same function is
  at a different address in each: the extra-prop loader is `0x0603DBA4` in GAME4
  and `0x0603CBD4` in GAME5, and the `A_*` name table is at base `0x06096350` in
  GAME4 against `0x0609B174` in GAME5. Quote the module with the address, and
  disassemble the module you mean.
- Keep `CMakeLists.txt` and `Game.vcxproj` in step as `src/game/saturn/` grows.
  New files must also have a **unique basename**: MSVC names objects after the
  basename alone, so `dc/EntityModels.cpp` plus `saturn/EntityModels.cpp` would
  share one `.obj` and the link would silently lose four `dc_*` symbols.
  `verify_saturn_models.py` fails on any such collision.
- Platform boundary: `src/game/saturn/` is OS-agnostic like the rest of
  `src/game/`.

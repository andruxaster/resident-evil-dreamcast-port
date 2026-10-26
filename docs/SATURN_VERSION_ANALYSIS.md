# Sega Saturn disc (Resident Evil, USA) — content analysis

(The Japanese release, "Bio Hazard", is covered in §6b.)

Everything below was measured from the disc, not assumed. Source image:

    assets/Resident Evil - Saturn (USA)/Resident Evil (USA) (Track 1).bin
    assets/Resident Evil - Saturn (USA)/Resident Evil (USA) (Track 2).bin   (CD-DA audio)

Read with `tools/saturn_iso.py`; extracted to `assets/SATURN_RAW/`.

---

## 1. The container is not the problem

The track is plain `MODE1/2352` — a normal CD-ROM track, 248,841 sectors, one
16-byte sync/header skip per sector and nothing Saturn-specific in the way.
Sector 0 is `IP.BIN`; sector 16 is an ordinary ISO9660 Primary Volume
Descriptor. **The filesystem is plain ISO9660 and the asset tree is as
structured as the PS1's** — 12 folders, all the expected names. The difficulty
is *inside* the files, not in finding them.

| | value |
|---|---|
| system id | `SEGA SEGASATURN` |
| volume id | `RESIDENT_EVIL` |
| volume space size | 249,369 blocks (528 more than track 1 holds — a stale field) |
| directory entries | 1336 (1324 files + 12 directories) |
| folders | `DATA` `ENEMY` `ITEM_M1` `SND` `STAGE1`..`STAGE8` |

`IP.BIN` (sector 0): `SEGA SEGASATURN SEGA TP T-12`, `T-1221H  V1.002
19970804CD-1/1`, area symbol `U` at +0x40 with `J` at +0x50, `RESIDENT EVIL` at
+0x60, and the **boot-filename field at +0x80 is blank** (all spaces). So the
BIOS is not told which program to run — which one it picks is still open
(§3).

---

## 2. Saturn disc vs the PS1 OG disc

| Saturn | PS1 OG | note |
|---|---|---|
| `GAME1..GAME5.PRG` (root) | `PSX/PROG2/*.EXE` (12) | Saturn splits by *function*, PS1 by stage overlay |
| `MOVIE.CPK` + `MOVIE.PRG` (250 MB) | `PSX/MOVIE/*.STR` × 25 (291 MB) | FMV repacked into one container + a player |
| `SND/*.CDP` + `*.PCK` (311 files) | `PSX/SOUND/*` (127) + `PSX/VOICE/*.XAS` (5) | `CD_PACK Ver.1.0` + PCM packs; the VAB/VAG bank layout is gone |
| `ENEMY/*.EMD` × 71 + `*.DAT` × 244 | `PSX/ENEMY/*.EMD` × 65 | 6 new meshes, 244 new per-creature data files |
| `ITEM_M1/DD01..DD50.DAT` (50) | `PSX/ITEM_M1/*.DOR` (34) | door animations, renamed and re-laid out |
| `DATA/*.EMW` (30, merged) | `PSX/PLAYERS/*.EMW` + `*.TMD` (37) | player models folded into `DATA` |
| `STAGE1..STAGE8` `.RDT` (363) | `PSX/STAGE1..STAGE7` `.RDT` (348) | 348 shared names, every file byte-different; `STAGE8`'s 15 are new |
| `STAGE1..STAGE5` `.BSS` (116) | `PSX/STAGE1..STAGE5` `.BSS` (116) | same names, re-encoded (both are MDEC bitstreams) |
| root `ROOM1PCM.PCK`..`ROOM5PCM.PCK` | — | per-stage room PCM, 13.7–38.8 MB each |
| root `BATTLE.BIN` `TITLE.BIN` `END.BIN` `0.BIN` `BIO2.BIN` `WAPON3.BIN` `SAVE2.BIN` `KEY3.BIN` `BAZ.BIN` `FONT_000.BIN` `FONT2.CEL` | — | all Saturn-only |
| `DATA/BU_DATA.PCK`, `DATA/*.PRS` | — | Saturn-only |

Nothing that exists on both discs is byte-identical except one file:
`DATA/BIO_CARD.DAT` (the save-card template). Of the **542** files the two discs
share (348 `.RDT` + 116 `.BSS` + 65 `.EMD` + 13 `DATA`), 541 differ. This is
the opposite of the Director's Cut, whose tree was largely a sparse diff — **the
Saturn release is a full re-dress of every asset**, which is exactly what §0 of
`DC_PORT.md` says a mode tree should be, and why this mode wants a tree rather
than a diff.

---

## 3. The programs

### 3a. `0.BIN` is the boot module

`0.BIN` (86,400 B) is the **first file in the root directory in on-disc order**,
and `IP.BIN`'s boot-filename field at +0x80 is blank — so the Saturn BIOS has
nothing else to go on. It is a Saturn module like the `.PRG`s (same 0x22-byte
prologue at +0x40, after a 0x40-byte header), and at **+0x190 it holds the
module table**: eleven 12-byte name fields naming every `.PRG` on the disc.

| slot | name | slot | name |
|---|---|---|---|
| 0 | `TITLE.PRG` | 6 | `MOVIE.PRG` |
| 1 | `GAME1.PRG` | 7 | `BU_LOAD.PRG` |
| 2 | `GAME2.PRG` | 8 | `BU_CK.PRG` |
| 3 | `GAME3.PRG` | 9 | `ENDING.PRG` |
| 4 | `GAME4.PRG` | 10 | `RANKING.PRG` |
| 5 | `GAME5.PRG` | | |

`GAME1..5` line up one-to-one with `STAGE1..5` (the stages that ship
backgrounds) and `ROOM1PCM.PCK`..`ROOM5PCM.PCK`, and `STAGE6/7` are the
background-less revisit stages exactly as on the PS1. No `.PRG` names any other
`.PRG` — `0.BIN` is the only thing that knows they exist, and the strings
`GAME1`..`GAME5` appear **nowhere else on the disc**.

### 3b. The eleven modules

| file | bytes | what it is | evidence |
|---|---|---|---|
| `TITLE.PRG` | 66,084 | title screen | loads `/SND/TITLE.PCK`, `/SND/SELECT.PCK` |
| `GAME1.PRG` | 460,608 | mansion stage 1 | loads `/SND/CHR.CDP`, `/SND/BATTLE.CDP`, `/SND/BGM34B.CDP` |
| `GAME2.PRG` | 468,204 | mansion stage 2 | as above |
| `GAME3.PRG` | 450,196 | mansion stage 3 | as above |
| `GAME4.PRG` | 463,668 | mansion stage 4 | as above |
| `GAME5.PRG` | 484,828 | mansion stage 5 | as above |
| `ENDING.PRG` | 117,352 | ending | loads `END.BIN`, `BAZ.BIN`, `FONT_000.BIN`, `BIO2.BIN` |
| `RANKING.PRG` | 70,432 | score / name entry | loads `/SND/JIL.CDP`, `/SND/NAME.CDP` |
| `BU_LOAD.PRG` | 17,968 | "BU" loader | loads `/SND/SDDRVS.PCK` |
| `BU_CK.PRG` | 5,336 | "BU" checker | loads `/data/bu_data.pck`, calls the SH-2 BIOS (`TBIOUDATA_` symbol) |
| `MOVIE.PRG` | 31,444 | FMV playback | loads `MOVIE.CPK` |

The Battle Game has **no module of its own**: `GAME1..5` load
`/SND/BATTLE.CDP`, so its logic lives in the stage code, and `BU_CK.PRG` /
`BU_LOAD.PRG` / `RANKING.PRG` are its checker, loader and high-score screen.

**The modules are big-endian SH-2, with no standard Saturn executable header.**
There is no 0x20-byte header (`branch` / `bss_size` / `init_pc` / `init_sp` /
`load_address` / `file_size`) at offset 0 — each module starts directly with the
shared prologue `mov.l r14,@-r15 ; sts.l pr,@-r15 ; …`, at file offset **0** for
the eleven `.PRG`s and **0x40** for `0.BIN` (which carries a 0x40-byte header
first). Ten of the eleven share their first 0x22 bytes; `MOVIE.PRG` does not.
Ghidra decodes `BU_CK.PRG` from offset 0 into a coherent C-runtime stub —
`sts.l pr` / `lds.l @r15+,pr` / `rts` — and `0.BIN` from 0x40 into a
conditional-call function, both unambiguous.

### 3d. The `.PRG` files are PARTLY compressed

> **WRONG — the `.PRG` files are NOT compressed.** Everything below came from
> the byte-shifted `GAME1.ram` (§3h). Against the corrected image,
> `GAME1.PRG` equals live RAM at `0x0602B000` in all but **309 of 460,608**
> bytes, in 13 small runs of runtime data (the path templates being rewritten,
> `font2.cel` uppercased, variables). The 477,376 bytes past the end of the
> file are BSS (75.7% zero). So a module is loaded **verbatim**, and
> `FUN_0602b040` is an asset decompressor the module carries (rooms,
> backgrounds), not a self-extractor. To analyse a `GAMEx` statically, import
> the disc `.PRG` at `0x0602B000` with `SuperH:BE:32:SH-2`; the emulator is
> only needed for runtime state and BSS contents.

**Correction to an earlier reading of this section.** The modules are *not* wholly
compressed stubs. Measured properly, with the disc file compared against live RAM
and the disc file's own branch density profiled across its length:

| | `GAME1.PRG` (live) | `TITLE.PRG` (live) |
|---|---|---|
| on disc | 460,608 B | 66,084 B |
| live in RAM | 937,984 B (2.04x) | 409,600 B (6.2x) |
| **verbatim prefix shared with the disc** | **26,384 B** (0x6710) | not re-measured |
| branch density on disc, per 64 KB | 5.9 / 5.6 / 6.6 / 5.8 / 6.0 / 3.9 / 2.2 / **0.3%** | 6.4% |
| printable-ASCII fraction on disc | 38-44%, then 9.6% | 40% |

The profile is the whole story: **the first ~26 KB of the file is ordinary SH-2
code** (5.9-6.6% branch density, ~40% printable — normal for code with inline
strings), and **the tail past ~0x60000 is a compressed payload** (0.3% branch
density, 9.6% printable). The stub at the start expands that tail in place, over
its own text — which is why RAM stops matching the disc exactly at the point the
expansion reaches.

So the disc image *is* the code, for its first ~26 KB, and the live image is the
code plus the expanded tail. **Importing the disc file gives real code**, just
missing whatever lives in the compressed tail — which is where the game's data
tables and strings are. The two are complements, not alternatives:

    GAME1.PRG on disc   : code for +0x0000..+0x6710, then the compressed blob
    live RAM at the slot: the same 26 KB verbatim, then the expanded blob

`tools/saturn_module_dump.py` writes the live image, which is the superset and the
one to import. Its guard rejects a dump whose branch density is at or below the
noise floor — it must *not* compare against the disc file's density, because the
expanded tail legitimately has less branch density than the original code prefix.

`TITLE.PRG`'s 6.2x expansion is the outlier here and is still unexplained: its
disc file is 6.4% branch-dense throughout with no obvious compressed tail, yet
live RAM matched it only 1.3%. The first TITLE dump was taken with a throwaway
reader whose hexdump parser is not the tool's, so that measurement is suspect and
should be redone with the current tool.

Consequences for the port:

1. Import the **live** image; it is the superset. `GAME1.PRG` gives 277
   functions, `TITLE.PRG` 257.
2. The emulator is still required — the compressed tail only exists in RAM after
   the module has run, and that is where the data tables and file paths live.
3. This compiler emits **no stack-frame prologue** — `STS.L PR,@-R15`
   (0x2D12) does not occur once in 409,600 bytes — so functions must be seeded
   from `BSR` call targets, not found by prologue scans.

### 3g. The Saturn build shares the PS1 source tree

The unpacked modules carry their **build paths**, which settles where the port
came from and gives a handle on the room loader. In `GAME1`:

    c:/horr/psx/stage /room    .rdt      at 0x06092B34
    c:/horr/psx/stage /room   .bss      at 0x06092B7C
    /0123456789abcdef                     (hex digit table, for the room number)
    c:/horr/psx/data/status.dat, bio_card.dat, itembox.pix/.pal, font.pal/.dat
    c:/horr/psx/enemy/, c:/horr/psx/data/map0*.dat, CORE00.ESP/.ETM
    ROOM1PCM.PCK .. ROOM5PCM.PCK

`horr` is the project's internal name and the tree is the **PS1** one, so the
Saturn modules are compiled from the same source as `PSX/PROG2/*.EXE`. The room
filename is formatted from a template with the stage digit substituted into
`stage ` and the room number written in **hex** into `room    ` (up to four
digits, via the digit table), which is why the disc's rooms are `ROOM1010`,
`ROOM8010` and so on.

Each template's address occurs exactly once in the unpacked image — `+0x012E05`
for `.rdt` and `+0x013909` for `.bss` — and neither is a PC-relative
`MOV.L @(disp,PC)` nor a 4-byte-aligned pointer, so the reference chain goes
through a byte-packed structure rather than a literal pool.

**The reference was resolved by watching the buffers change.** Diffing three
RAM dumps — no room loaded, room "107" loaded, room "118" loaded — shows the
templates being rewritten *in place* into 31-byte path buffers at
**0x06092B44** (`.rdt`) and **0x06092B84** (`.bss`), uppercased:

| | 0x06092B44 | 0x06092B84 |
|---|---|---|
| no room | `c:/horr/psx/stage /room    .rdt` | `c:/horr/psx/stage /room   .bss` |
| room 107 | `c:/horr/psx/STAGE1/ROOM1070.RDT` | `c:/horr/psx/STAGE1/ROOM107.BSS` |
| room 118 | `c:/horr/psx/STAGE1/ROOM1181.RDT` | `c:/horr/psx/STAGE1/ROOM118.BSS` |

The in-game room number is the **3-digit** field; the `.rdt` template's
fourth digit is a **variant** digit, and the `.bss` template has no variant
field at all. Confirmed against the disc:

| | `STAGE1` |
|---|---|
| `.RDT` names | 58 files, **all** 4 hex digits — `ROOM1000`/`ROOM1001`, … `ROOM11C0`/`ROOM11C1` |
| `.BSS` names | 29 files, **all** 3 hex digits — `ROOM100` … `ROOM11C` |

So each stage holds 29 rooms, each with **two `.RDT` variants** sharing **one
`.BSS` background**. `ROOM1070.RDT` is 289,843 B and `ROOM107.BSS` 262,340 B;
`ROOM1180.RDT` and `ROOM1181.RDT` are both 115,343 B. `STAGE6`/`STAGE7` ship 58
`.RDT` each and **no** `.BSS`, reusing `STAGE1`/`STAGE2`'s — which is why the
shared-room count is 348 `.RDT` (58+58+36+36+44+58+58) and 116 `.BSS`.
A converter therefore has to map `ROOMnnn{v}.RDT` → PC room data, and **the
variant digit is significant** — it is the same 0/1 pairing the PC port already
carries.

Three-way diffing also isolates the **room machinery** from level content.
Three rooms were sampled — `ROOM1070.RDT` (289,843 B), `ROOM1181.RDT`
(115,343 B) and `ROOM10B1.RDT` (169,211 B) — and the window `0x06090000`-
`0x06100000` classifies as:

| | bytes | share |
|---|---|---|
| never written by a room load | 293,971 | 64.1% |
| written every load, **same value in all three** | 9,891 | 2.2% |
| written every load, **room-dependent** | 58,308 | 12.7% |
| written by some rooms only | 96,582 | 21.1% |

Within the invariant-on-load set are **three 255-byte records at exactly 0x400
apart** — `0x060F130D`, `0x060F170D`, `0x060F1B0D`. **These are a dead end, and
worth recording so they are not chased again**: they are byte ramps
(`00 01 02 ... 0F 10`, then zero padding, repeated), i.e. a palette / index
translation table for colour conversion, not room structure. Read as `u16` they
look like `0x0100 0x0302 0x0504 ...`, which is what makes them look structured.

### 3h. The loader DECODES the room; nothing is copied through

The decisive negative for Phase 2. If any section were copied from the `.RDT`
into RAM verbatim, the file→RAM mapping would *be* the section layout and the
converter could be written from data alone. It is not:

| room file | sampled 32-byte windows found verbatim in RAM |
|---|---|
| `ROOM10B1.RDT` | **0 of 21** |
| `ROOM1181.RDT` | **0 of 15** |
| `ROOM1070.RDT` | **0 of 36** |

Longest verbatim span anywhere: **0 bytes**.

**Caveat, and it matters:** the on-disc `.RDT` is *packed* (see 4a), so this test
cannot distinguish "the loader decodes it" from "the loader never copies packed
bytes anywhere" — a packed input is not expected to appear in RAM in the first
place, and the zero-overlap result is fully explained by the packing alone. It
does **not** establish that a second transform follows the decode. What it does
establish, independently, is the one thing Phase 2 needs: **there is no data-side
shortcut**, because the file is not the layout. **Phase 2 remains a code-RE
task** — the layout has to come out of the parser, now that the codec in 4c is
partially understood.

> **The old `GAME1.ram` was corrupt, which is why the static routes failed.**
> `saturn_module_dump.py` and `verify_saturn_ram.py` parsed the emulator's
> hexdump expecting a `|` before the ASCII column. There is none: the ASCII
> follows two spaces, so any hex-looking piece of it (`AB`, `0F`) was appended
> as a byte. Against a strict re-read, whole 64 KB windows of the old image sat
> 1-2 bytes off live RAM (−1 at `0x0604B000` and `0x0607B000`, mixed at
> `0x0602B000`-`0x0603B000`). Ghidra was decoding that code at the wrong
> halfword boundary. Both parsers are fixed, `GAME1.ram` is re-dumped (the bad
> one is kept as `GAME1.ram.shifted-bad`), and the corrected image is imported as
> **`/SATURN_RAM_FIXED/GAME1.ram`**. `0.ram` and `TITLE.ram` came from the same
> parser and should be re-dumped before being trusted. The "+1 module-base
> drift" and the 86,401-byte `0.ram` in §4c are likely this bug, not the
> decompressor.
>
> **The room loader, found through a watchpoint backtrace on the corrected
> image:** `FUN_0603dd30` builds the `.rdt` path in place in the template at
> `0x06092B34`: the stage digit at `[0x11]`/`[0x17]`, the room as two hex digits
> at `[0x18]`/`[0x19]`, and the variant digit at `[0x1A]` (bit 31 of
> `*(0x060A0940+0xA50)`). It then calls `LoadFileStripPrefix` (`0x0603EACC`,
> which loads `path + 0xB`, i.e. without `c:/horr/psx`) to **`0x00270000`**. The
> current stage and room are the bytes **`0x002F8660` / `0x002F8661`** in low
> work RAM. Call chain: `0x06052EBC` → `FUN_0603dd30` → `LoadFileStripPrefix` →
> `0.BIN` `0x0600474C`/`0x060046C2`. The `.rdt` template is reached only
> through literal pools (`0x0603DE04`), which is why the byte searches below
> missed it in the shifted image.

> **Superseded by §4.0.** The zero-overlap result is fully explained by the
> packing. Once decoded, the room *is* the PS1 file, so there is a data-side
> shortcut after all, and Phase 2 is a decode, not a parser RE.

The parser is not where it might be hoped. The path buffers at `0x06092B44` /
`0x06092B84` are written in place from the templates, but **every static route to
them is exhausted**:

* no `MOV.L @(disp,PC),Rn` within ±8 KB of either template resolves to them;
* no 4-byte-aligned pointer to them exists anywhere in `GAME1`'s live image;
* `0.BIN` contains no reference to either buffer address at all;
* the entire template neighbourhood `0x06092A00`-`0x06092D00` holds only **two**
  even-offset big-endian literals (`+0x014F4A` → `0x06092BAA`, `+0x016E3A` →
  `0x06092CEC`), and neither is reached by a PC-relative load.

A memory watchpoint on the room buffer (`0x060AA00F`) does fire on a room
change, but the emulator reports the stop at a neighbouring PC rather than the
accessed address, so it localises the parser only loosely.

So the formatting code reaches the templates through some structure the static
passes have not identified, and the remaining routes are both dynamic: a long
`exec_history` capture across an actual room transition (the 512-instruction
buffer is far too small to characterise anything), or single-stepping a room
load under the interpreter core — slow, but definitive.

### 3e. Getting the unpacked code into Ghidra — it just works

`tools/saturn_module_dump.py` writes the unpacked image; import it at the module's
load address with `SuperH:BE:32:SH-2` and run analysis. That is the whole
procedure. `TITLE.PRG`'s 409,600-byte image yields **257 functions** and
`GAME1.PRG`'s 937,984-byte image **277**, both decompiling into ordinary C, and
the image is then as analysed as it can be:

- **No manual seeding is needed.** Analysis follows `BSR` from the first
  function and covers the image. Hand-placed seeds added only themselves — ten
  of them produced ten functions and *zero* new callees, because the image was
  already covered.
- **The 215 remaining gaps are data, not missing code.** `find_code_gaps`
  reports 215 gaps of 72-300 bytes between functions, and Ghidra refuses to
  start functions there: *"Function entryPoint may not be created on defined
  data."* They are the module's tables and strings.
- The 1,246 distinct `BSR` targets are not 1,246 functions: 1,244 of them have
  exactly one caller, and most are already inside a function body. This
  compiler calls each function from a single place, so call-target counting
  wildly overestimates the function count.

### 3f. Ghidra instance state — scripts unavailable, but not needed

With `GHIDRA_MCP_ALLOW_SCRIPTS=1` set (and after a full restart, to rule out a
stale instance) `run_script_inline` gets past its gate and then fails in the
**Java** provider:

    GhidraScriptLoadException: ... java.lang.NullPointerException: Cannot invoke
    "ghidra.app.plugin.core.osgi.BundleHost.getBundleFiles()" because
    "ghidra.app.script.GhidraScriptUtil.bundleHost" is null

The **Python** provider is not installed either ("Ghidra was not started with
PyGhidra"), and `list_analyzers` returns zero analyzers for *every* program
including the long-established `ResidentEvil.exe` — the same missing OSGi bundle
host. So this instance cannot run scripts, and its analyzer registry is not the
normal one. (Figures quoted as "produced by analysis" before this was noticed
are really just what entry-point creation gave.)

None of that blocks the port: the bridge's own tools cover everything needed
here, as §3e shows. It only matters if a task later needs a real Ghidra script
(a scripted rename pass over hundreds of functions, say).

### 3c. Byte order, and how not to get it wrong

Ghidra 12.1.2's SuperH support is **big-endian only** — `superh.ldefs` defines
`SuperH:BE:32:SH-1`, `:SH-2` and `:SH-2A`, and nothing little-endian. So the
modules import as they are, with `SuperH:BE:32:SH-2`. The tempting mistake is
`SuperH4:LE:32:default`, which *is* little-endian and *is* SuperH, and fails
silently: the SH-4 puts its FPU opcodes in the 1100/1101/1110/1111 groups where
an SH-2 has `MOV.B @(disp,Gn)`, `MOV.L Rm,@(disp,Rn)`, `MOV #imm,Rn` and
`MOV.L @(disp,PC),Rn`, so every data-movement and literal-load instruction
decodes as a floating-point op and auto-analysis finds nothing.

The other trap is concluding the code is little-endian from a *couple* of words.
The little-endian reading of the shared prologue's first word, `2F E6`, comes out
as `MOV.L @(8,PC),R6` — a real-looking instruction — so it does not look wrong.

`tools/verify_saturn_code.py` settles it with control flow rather than data
movement, because MOV/CMP-style words occur by chance in either order while
`NOP` (`0x0009`) and `RTS` (`0x000B`-`0x000F`) do not. `RTS` + `NOP` density
against a random-byte baseline:

| module | little-endian | big-endian |
|---|---|---|
| `0.BIN` | 10 (2.4×) | **1436 (266×)** |
| `BU_CK.PRG` | 1 (3.9×) | **85 (255×)** |
| `GAME1.PRG` | 89 (4.1×) | **5632 (196×)** |
| `GAME5.PRG` | 113 (4.9×) | **5783 (191×)** |
| `RANKING.PRG` | 5 (1.5×) | **680 (155×)** |

All twelve come out big-endian; the little-endian column sits at the noise
floor. **There is no byte swap to apply.**

**Load addresses — read out of a running copy, and verified.**

Yaba Sanshiro's MCP debug server (127.0.0.1:9640) was used to dump the 2 MB SH-2
main RAM window `0x06000000`-`0x061FFFFF` while the disc was booted, then each
module's bytes were matched against the dump:

| module | load address | evidence |
|---|---|---|
| `0.BIN` | **0x06004000** (mirrored at 0x06104000) | file[0:16384] == RAM[0x06004000:] — 16384/16384 byte-identical |
| all eleven `.PRG`s | **0x0602B000** | `TITLE.PRG` file[0:16384] == RAM[0x0602B000:] — 16384/16384; only `TITLE.PRG` was resident, the rest share the slot |

So **the eleven `.PRG`s are overlays at one address**, swapped on demand — the
Saturn counterpart of the PS1's `PSX/PROG2/*.EXE` stage overlays, and the reason
`GAME1..5.PRG` are separate files at all. The gap `0x06019180`-`0x0602B000` after
`0.BIN` is its data/BSS.

The first bytes of each `.PRG` are the decompression stub, **not** the module
entry (§3d), and the loader in `0.BIN` chooses the real target. With the base
set, Ghidra analyses the *disc* image and finds 398 functions in `0.BIN` and
255 in `TITLE.PRG` — but see §3d: for the `.PRG`s those are the stub and its
data, not the game.

---

## 4. Room data (`.RDT`)

### 4.0 RESOLVED — the Saturn `.RDT` *is* the PS1 `.RDT`, LZ77-packed

**This supersedes the "not this codec" negative and everything built on it in
§3h, §4a, §4b and §4c.** Both room files are packed from byte 0, with no header,
by the same `FUN_0602b040` LZ77 as the `.PRG` tails (`tools/saturn_lz77.py`,
`GAME1` masks `0xFFFFFF00`/`0xFFFFE000`). `tools/saturn_room.py` covers both.

**`.RDT`: one stream, and the decode is the PS1 file byte for byte.** You can see
it by eye, because the literals *are* the PS1 bytes. Saturn `ROOM1010` opens
`c7 00 06 00 ff b8 7f 02 a8 02 f8 01 c4 2c f8 f6 10 f5 ff ff d4 …`, which reads as:
flag `c7` → literals `00 06 00`, short ref distance −1 length 3 → `00 00 00`,
literal `b8`, flag `7f` → literals `02 a8 02 f8 01 c4 2c`, flag `f8` → short ref
−10 length 2 → `00 00`, then literals `10 f5 ff ff d4`. That is PS1 `ROOM1010`'s
`00 06 00 00 00 00 b8 02 a8 02 f8 01 c4 2c 00 00 10 f5 ff ff d4`.

`python tools/saturn_room.py verify` decodes every Saturn room and compares it
with the PS1 USA disc (`SLUS-00170`, read in place, MODE2/2352):

| | rooms |
|---|---|
| decode ends on the zero token at **exactly** the last source byte | **363 of 363** |
| decoded file **identical** to the PS1 disc's file of the same name | **348 of 348** |
| Saturn-only (`STAGE8`, Battle Game) | 15 |

So **there is no Saturn RDT format to reverse**. The header, the 19 relative
pointers at `+0x48`, the 0x2C camera array at `0x94`, the SCD and the
per-camera mask TIMs are all the PS1 data, untouched. The existing room tools
(`mine_room_scd.py`, `dump_init_scd.py`, `evt_disasm.py`, `dump_room_masks.py`)
read a decoded file as it is. Compared with the **PC** tree instead, 189 are
identical and 159 differ, but those differences are PC-side edits: the
PS1 comparison is the one that counts.

The "file-size drop" (§7 item 7) is just the compression ratio: `ROOM1010` is
161,572 B packed and 297,300 B decoded.

**Why the earlier scan said "not this codec".** `saturn_rdt_lzscan.py` capped the
output at 128 KB as "suspicious". Decoded rooms are 175-616 KB (`ROOM1060` is
456,096 B), so the true hit at offset 0 always stopped on `limit` and was
discarded. The cap is now 1 MB. The lesson is the same one the section itself
draws about permissive criteria, in the other direction: a *too strict* bound
turns a clean positive into a clean-looking negative.

**`.BSS`: a container of LZ77 streams, and it is a new Saturn format.** It is
not the PS1 MDEC frame set. Layout:

| part | contents |
|---|---|
| offset table | big-endian `u32`s. The first entry is the table's own size, so `count = first / 4`: **20** in 114 files, **22** in `ROOM303` |
| slots `[0, n/2)` | palettes, 512 B each = 256 Saturn RGB555 words (BE, bit 15 set, R bits 0-4, G 5-9, B 10-14) |
| slots `[n/2, n)` | images: one LZ77 stream each → **76,800 B = 320×240, 8 bpp** |
| unused slot | the 4-byte placeholder `00 00 00 01` |

Palette slot *k* belongs to image slot *n/2 + k*; `ROOM105` uses slot 9 with
image 19, which is what pins the pairing. The pixels are **VDP2 character
cells**: 8×8 cells, 40×30, cells in row-major order and each cell row-major
inside. Laid out linearly, the image comes out as horizontal smears with the
right colours, which is the tell. Rendered with the cell order, `ROOM106` slot 0
is the main hall balcony. This is also the "camera-cut asset, 256 KB → 76,800
bytes" of the live capture in §4c.

Across the 115 containers: **647 palettes, 647 images, every image decodes to
exactly 76,800 B.** Exceptions and loose ends:

* `STAGE3/ROOM311.BSS` (22,528 B) is **not** converted. It is a PS1 MDEC frame
  (`20 3b 00 38 02 00 03 00 …`), left over on the disc.
* **Camera → image mapping: image slot *k* is camera *k*.** Every Saturn image
  was matched against the PC's per-camera backgrounds (`RC<room><k>.PAK`) on
  normalised 40×30 luminance thumbnails, then the doubtful ones were checked
  by eye. In 105 of 115 rooms the best match is the identity. The other 10 are
  metric failures (dark rooms, or PC `.PAK`s that `pak_view.py` renders
  skewed), not remappings. Compare on luminance only: the PC TIMs are BGR and
  the Saturn palettes RGB, and an RGB distance fails on that swap alone.
* **The count check needs the right RDT.** Many `ROOM2xxx` rooms decode to the
  4-byte stub `00 00 00 00` (as on the PS1), and their real room is
  `ROOM7xxx`, since STAGE6/7 reuse STAGE1/2's backgrounds. With the non-stub
  room as reference, 90 rooms have images == cameras. Where the images exceed
  the cameras, the PC ships the same extra `.PAK`s too (e.g. `ROOM104`: 6
  cameras, 8 images, 8 PC paks), so extra slots are PS1-era pictures beyond the
  camera count, not Saturn additions.
* **Against the PS1 frame count** (PS1 `.BSS` = 32 KB per MDEC frame), 104 of
  115 are equal. The Saturn **adds** pictures in `ROOM105` (slot 9, a small
  plaque close-up), `ROOM303` (slots 8-10: sky, wall and floor views of the
  heliport) and `ROOM30E`. It has **fewer** in 106, 200, 206, 306, 410, 411,
  500 and 506.
* **Only two cameras in the game have no Saturn image**, checked against the
  real (non-stub) RDT of every room. `ROOM200` is not one: its real room is
  `ROOM7000`, which has 1 camera and 4 images.

  | camera | switch zones to it | SCD cuts to it | what it is (PC `.PAK`) |
  |---|---|---|---|
  | `ROOM106` cam 7 | none, in all 4 variants | none | high view over the main-hall balcony and stairs |
  | `ROOM506` cam 5 | none | none (no cut commands at all) | lab computer monitor close-up |

  Both readings are confirmed from play knowledge: the main-hall camera 7 is
  never used on the PS1, and lab camera 5 is the PC terminal's screen.
  106 cam 7 is **dead**: no zone, no script cut and no code path reaches it,
  so the Saturn simply dropped its picture. 506 cam 5 is **reached from
  code**: the PC's lab-terminal handler switches between cameras 4 and 5
  (`s_pendingCamera = 0x84 / 0x85`, `cl_camera_step` `0x004154D0`, room 5060).
  **Resolved.** Camera 4 is already the terminal close-up, and camera 5 is the
  *same view, darker*: the PC toggles to it with the pending `0x85` and back
  with `0x84`. The Saturn terminal is in **`GAME4.PRG`** (not `GAME5`): the
  `JOHN`/`ADA`/`MOLE` table at `0x06093F18` has the PS1's layout, big-endian
  (PS1 `0x80090A20`, PC `0x004C7AF0`). Its init `FUN_06046078` matches the
  PC's `computer_lab_init` line for line: save the camera, set it to 4, call
  the camera switch with 1. The login check is `FUN_06046c3c`, and the
  terminal's state table is at `0x06094440`. No code in the terminal range
  stores 5 to the camera byte, so the Saturn keeps camera 4 throughout and
  drops the dim variant. Nothing is lost: the PS1 does not use camera 5
  either (confirmed in play), so the toggle to it is **PC-only** behaviour. The Saturn camera byte is
  **`0x002F8662`**, right after stage `0x002F8660` and room `0x002F8661`.
  `ENEMY/COMPIC.DAT`, a candidate on its name, is unrelated: it is a
  "COMING SOON… 1997 SUMMER!" promo screen in the `.BSS` container.
* The PC port's backgrounds are `.pak` (LZW → 16-bit TIM). A Saturn `.BSS`
  image converts to that with only the palette lookup and a cell de-swizzle.
  Note the Saturn backgrounds are **8 bpp** where the PS1's MDEC frames are
  true-colour, so they are a lower-fidelity source, not a better one.

**Battle Game arenas (`STAGE8`) reuse campaign rooms, all 15 of them.** Each
arena camera record was matched individually against every campaign camera,
on bytes `+0x08..+0x2C` (from, to, roll, fov). Bytes `+0x00..+0x07` are file
offsets and legitimately differ, which is why an earlier whole-array
comparison missed three. **Every camera of every arena** matches one campaign
room:

| arena | room | arena | room | arena | room |
|---|---|---|---|---|---|
| `8010` | 100 | `8060` | 100 | `80B0` | 30E |
| `8020` | 103 | `8070` | 404 | `80C0` | 509 |
| `8030` | 302 | `8080` | 40C | `80D0` | 50F |
| `8040` | 117 | `8090` | 309 | `80E0` | 510 |
| `8050` | 210 | `80A0` | 30C | `80F0` | 513 |

**And the game carries that exact table.** `GAME1` at **`0x06092B5E`** holds
the 15 entries as big-endian `u16` = `(0-based stage << 8) | room`:
`0000 0003 0202 0017 0110 0000 0304 030C 0209 020C 020E 0409 040F 0410 0413`.
It sits between the `.rdt` path template (ending `0x06092B53`) and the `.bss`
template (`0x06092B7C`), inside the room-path formatter's data. Indexed by the
arena room id `0x01..0x0F`, the base is `0x06092B5C` with entry 0 unused; it
is labelled `g_BattleArenaRoomTable` there in Ghidra.

**Confirmed in code.** With the corrected `GAME1` image (§3h), the base
`0x06092B5C` appears as a literal in four literal pools, and each reader swaps
the arena for its campaign room (`/SATURN_RAM_FIXED/GAME1.ram`, names applied):

| function | loads | battle-mode test | lookup |
|---|---|---|---|
| `LoadRoomBss` `0x0603E7FC` | the `.BSS` (`…/stage /room   .bss`) | bit 31 of `g_SystemFlags` | `stage, room = table[g_BattleArenaIndex]` |
| `SetupRoomLighting` `0x0603E32C` | per-room light/colour from a `(stage, room)` table (`stage*9 + room*…`) | bit 31 | same |
| `LoadRoomSeBank` `0x06051204` | `/SND/SE<stage><room>.CDP` | stage byte `== 7` (STAGE8) | same |
| `LoadRoomPcmBank` `0x06052AB4` | `/SND/PCM<stage><room>` | stage byte `== 7` | same |

Globals: `g_SystemFlags` = `0x060A1390` (`0x060A0940 + 0xA50`), battle mode =
bit 31; `g_BattleArenaIndex` = `u16` at `0x060A0916`, the table index
(`table[i*2]` = stage, `table[i*2+1]` = room); the current stage/room are the
bytes `0x002F8660`/`0x002F8661`, and the stage byte is 7 in an arena.
`LoadRoomRdt` (`0x0603DD30`) tests the same bit 31: set, the `.rdt` variant
digit is always `0`; clear, it is drawn from `0x002F3000 & 1`.

So an arena takes its **RDT from STAGE8** (its own SCD, enemies and items) but
its **background, lighting, SE bank and PCM bank from the campaign room**. The
code also confirms the STAGE6/7 rule: `LoadRoomBss` subtracts 5 from the path's
stage digit when the 0-based stage is 5 or 6, so those load STAGE1/2's `.BSS`.

A port therefore needs: the 15 decoded STAGE8 RDTs, this table, and the
battle-mode swap at the four load sites.

### 4a. The `.RDT` and `.BSS` files are COMPRESSED — the gate on Phase 2

**This corrects an earlier conclusion in this document.** The room format was
first reported as "restructured, not merely re-offset", on the evidence that the
Saturn header differs from PS1 at offset `0x00`, that the 19 relative pointers
are not at `+0x48`, that no 0x2C camera array is findable, and that the stub
rooms are 5 bytes where the PS1's are 4. **All of that is what compressed data
looks like.** None of it is evidence about the format.

The discriminator is `maxrun`, the longest run of identical bytes. Real code and
real data — padding, zero tables, repeated fields — always contain long runs;
packed data has almost none. Two controls bracket it, taken from the same disc:

| | bytes | branch density | printable | **maxrun** |
|---|---|---|---|---|
| `GAME1.PRG` code prefix (known plain) | 393,216 | 5.65% | 40.1% | **381** |
| `GAME1.PRG` tail (known packed) | 67,392 | 2.12% | 25.7% | 222 |
| `ROOM10B1.RDT` | 169,211 | 1.03% | 29.0% | **2** |
| `ROOM1181.RDT` | 115,343 | 1.25% | 25.0% | **3** |
| `ROOM10B.BSS` | 129,716 | 1.42% | 29.9% | **3** |

Sweeping every file: the **highest `maxrun` in the first 16 KB is 3 across all
335 `.RDT` files and 4 across all 116 `.BSS` files.** There is no plain region
anywhere in the Saturn room or background payload. `tools/saturn_rdt.py packed`
reproduces this.

It is not a trivial obfuscation: **no single-byte XOR and no single-byte ADD key
decodes any of them** — all 256 of each were scored by `maxrun`, and the best
never exceeds 3, where the plain-code control sits at 12. So it is a real
compressor.

Two consequences:

1. **Nothing about the Saturn room format can be read off the disc**, so the
   header/camera/stride detectors in `tools/saturn_rdt.py` have nothing to bite
   on yet. They are kept because each is validated against the PS1 disc and
   because they are how a *decode* gets verified — not because their negatives
   mean anything about the format.
2. **The codec is already identified.** See §4c.

And the reason a converter is still worth aiming for: the Saturn modules are
compiled from the **PS1** source tree (§3g), so once decoded, the room is likely
to be very close to the PS1 layout. That is a hypothesis to test after decoding,
not a fact.

The conversion target, meanwhile, is exact and already in the tree: `RDT` in
`src/game/Types.h` is a 0x94-byte header — five count bytes, a 2-byte pad, three
ambient shorts, three `RDT_Light` (0x14 each), then **19 32-bit *relative*
pointers** — with the camera array immediately after at **0x94**, 0x2C bytes per
record, ending in `fov`. Every one of those fields is read somewhere in
`src/game/`, so a converter has to produce all of them, not a subset.

Also settled by measurement, and worth keeping: the RDT is **little-endian on
both discs** — the port reads `ambient_light_r` as a plain short and gets 0x02b8
from the bytes `b8 02`, so a big-endian reading matches nothing. And the loader
**decodes rather than copies**: sampled windows from three rooms' `.RDT` files
find **0 verbatim matches in RAM**, longest common span **0 bytes**, so once
decoded there is still a transform between the decoded stream and the in-memory
form.

### 4c. The codec: a bit-stream LZ77 with *backwards* references

The stub at the front of every `.PRG` is a decompressor, and Ghidra decompiles
it cleanly — `GAME1.ram` at `0x0602B040` is `FUN_0602b040`, a **bit-stream
LZ77**:

* a 1 bit in the bit buffer means "copy the next source byte literally";
* a 0 bit starts a back-reference;
* one more bit selects the form. **Long form** (1): a 2-byte big-endian token —
  a token of 0 ends the stream, otherwise `offset = (token >> 3) | BIAS_B` and
  `length = (token's high byte & 7) + 2`, except that when that length field is 0
  the *next* source byte plus one is the length. **Short form** (0): two more
  bits give a length of 2-5, and one source byte `| BIAS_A` is the offset.

Two 16-bit literals are loaded by PC-relative `mov.w` at the top of the stub.
For `GAME1` they are **`DAT_0602b04c` = `0xFF00`** and **`DAT_0602b04e` =
`0xE000`**, both confirmed by reading them out of the running emulator with
`GAME1` loaded.

> **Read these constants only with the right module loaded.** They are baked into
> each module's stub, and an address that is a bias literal in `GAME1` is plain
> data in `TITLE`: at `0x602B04C`, with `TITLE` resident, the words read
> `0x6613` / `0xE200` and the code at `0x602B040` is an unrelated table-fill
> loop, not the decompressor. A stop there looks exactly like a hit and is not.

And the offset sign, which is the part that is easy to lose: Ghidra prints
`uVar11 = (uint)DAT_0602b04c`, so the mask looks like a *positive* 0xFF00 and the
following `add r5,r2` looks like a 64 KB forward jump. On SH-2 the register form
`MOV.W Rm,Rn` and the PC-relative `MOV.W @(disp,PC),Rn` **sign-extend** the
halfword (only the *immediate* form zero-extends), so the actual register values
are `0xFFFFFF00` and `0xFFFFE000`, and the OR/add computes a *backward* distance:

| form | 32-bit computation | range |
|---|---|---|
| short | `byte \| 0xFFFFFF00` then `+ dst` | −256 … −1 |
| long | `(token >> 3) \| 0xFFFFE000` then `+ dst` | −8192 … −1 |

**Every reference points backwards**, into the code prefix in front of the stream
and into already-written output. The decompiler's lost sign extension is what
makes an unsigned transcription fail instantly on a forward reference at output
position 0.

Register roles, straight from the listing: `r4` is the source (`mov.b @r4+,r6`
post-increments it), `r5` is the destination, and the literal path is
`mov.b @r4+,r1; mov.b r1,@r5; add #1,r5` — the literal bit comes from `bt`, so
**a set bit means literal**.

**Long-form length, note the sense** (`tst r0,r0` sets T = 1 when the field is
*zero*, and `bf/s` branches when T = 0):

* length field `!= 0` → branch to `0x602b094`, `add #2` → **length = field + 2**
  (field 1..7, so 3..9);
* length field `== 0` → falls through to `mov.b @r4+,r0` + `extu.b`, and the
  branch to `0x602b098` skips the `add #2` but carries **`add #1` in its delay
  slot** (`0x0602B0C8`) → **length = the following byte + 1, 1..256**
  (`saturn_lz77.py`'s `zero_field_bias=1`).

So it is the *non-zero* field that carries the `+2` bias, not the zero one.

The copy loop at `0x602b098` post-increments both pointers one byte at a time, so
overlapping (run-length) copies are safe. That is worth noting because it means a
distance of 1 is just a byte repeat, which is what makes the format usable for
run-heavy data.

#### Verified against live execution

A code breakpoint on the decoder's **entry** `0x602B040`, taken while loading
room 106, gives the two parameters no amount of static reading could supply:

| register | value | meaning |
|---|---|---|
| `R4` | `0x25E01990` | `src` — the compressed stream |
| `R5` | `0x25C0D800` | `dst` — the output buffer |

(`R3` is *not* a usable destination base: `mov r5,r3` at `0x602b04a` is dead
code, since the entry `bra 0x602b056` skips it and nothing else branches there.)

Replaying the first token from `src = 0x25E01990`, whose head is
`71 EE FF DD EE 1F F8 …`:

* bit byte `0x71`, LSB-first — the first bit is **1** → one **literal**,
  `src[1] = 0xEE`, and `r5` is now `dst + 1`;
* next bit **0** → **short** form; two more bits `0,0` → length 2;
* offset byte `0xFF` → `0xFF | 0xFFFFFF00` = **−1**;
* `r2 = r5 − 1 = 0x25C0D800`, the byte just written.

So the room opens with a 1-byte literal and a 2-byte run at distance 1 — three
`0xEE` bytes. `tools/saturn_lz77.py` implements the decoder and now reproduces
that first token exactly.

**The bit gotcha that sank every earlier transcription**, worth stating on its
own: `BT` tests the **C flag produced by `SHLR`**, which is the bit that was bit
0 *before* the shift. Reading `r6 & 1` *after* shifting returns the wrong bit
and derails the stream within one or two tokens, with no error and no obvious
symptom. The other two that cost time: `BF/S` branches when T = **0** (so
`DT`-based exhaustion falls *through* to the refill), and `add r5,r2` is
relative to the **current** output pointer, not the buffer base — using the base
makes a legal distance-1 reference look like it points one byte before the
start.

Also settled by inspection: the long form's `+2` bias applies to the
**non-zero** length field (3..9); the **zero** field reads a following byte and
adds **1**, giving 1..256. That is `add #1,r0` at `0x602b0c8` — which looks dead
because the preceding `bra` appears to target the copy loop at `0x602b0b98`, but
it is not: the branch lands on the `add` and falls through. This is worth
recording as a caution, because an earlier revision of this file asserted the
opposite (a bare `0..255` byte) on the strength of the decompilation's branch
target, and doing so stopped the decoder dead at 31 tokens. Forcing
`zero_field_bias = 1` in `tools/saturn_lz77.py` takes the same stream from
**31 tokens / 215 bytes to 69 tokens / 515 bytes** — the single most decisive
measurement in the whole exercise, and a reminder that a plausible reading of a
decompilation is not the same as a tested one.

> **WRONG — see §4.0.** The negative below was an artifact of a 128 KB output
> cap in the scanner. Every room decodes from offset 0 and is the PS1 file.

**And the room files are NOT this LZ77 — a tested negative.** `saturn_rdt_lzscan.py`
decodes `ROOM1060.RDT` from *every* byte offset and looks for a clean termination
on a zero token (the only reliable signal — see below). Across all 266,320 bytes:

* **0** offsets terminate cleanly;
* **1** offset even reaches 4 KB of output.

So the `.RDT` payload is not `FUN_0602b040`'s LZ77 at any fixed offset, and
`tools/saturn_lz77.py` is not the room converter. The room data uses a different
codec, or is not compressed at all but transformed some other way.

The *method* matters here as much as the result. Scoring offsets by "decoded N
bytes without violating the invariant" is worthless on packed data: almost any
first byte reads as a long literal run, so the decode limps along 1:1 and **258
offsets** clear a 1 KB bar, none of them real. Requiring a zero token cuts the
count to zero. A permissive criterion on packed input manufactures false
positives by the hundred — the same failure mode as the hexdump `|` filter that
silently returned nothing, and the same reason a plausible reading of a
decompilation is not a tested one.

What the LZ77 *is* used for, from live captures: two distinct call sites share
it, distinguished by output buffer — `0x25C0D800` (room-change data, 45,712 bytes
decoded) and `0x25E20040` (a camera-cut asset, 256 KB → exactly 76,800 bytes).
It is reached indirectly via a function-pointer slot at `0x602BCA4`, whose static
value is `0x602BE84` (a viewport/coordinate function, not a loader) and which
held `0x602B040` at the moment of capture. Note that a PC-relative *store* to
that address can only originate within 512 bytes of it, so a Ghidra xref search
on the address cannot find the writer — the search is the wrong tool, not the
theory.

**The token is little-endian — this is the last real trap.** Ghidra's C output
renders the long form as `CONCAT11(pbVar4[1], *pbVar4)`, which reads like
`(second << 8) | first`, and that is right, but it is easy to conclude the
opposite from the instruction names. Read the actual instructions:

```
0602b0a6: mov.b @r4+, r0      ; r0 = FIRST source byte
0602b0a8: mov.b @r4+, r1      ; r1 = SECOND source byte
0602b0aa: extu.b r0, r2       ; r2 = first
0602b0ac: shll8 r1            ; r1 = second << 8
0602b0ae: or   r1, r2         ; r2 = (second << 8) | first
0602b0b8: and  0x7, r0        ; field = FIRST byte & 7
```

So the first byte is the **low** byte of the token and also supplies the length
field. With the order reversed, a token like `FC FF` decodes as `0xFCFF`
(distance −1633) instead of `0xFFFC` (distance −1) — and the first room token is
exactly `FC FF`, which is why the whole stream looked like it derailed
immediately. `tools/saturn_lz77.py` now decodes a real room stream for 31
tokens / 215 bytes with the opening four bytes matching live RAM exactly, before
a single back-reference comes up two bytes short of the history available.

`src`/`dst` for a room-106 load were captured off the entry breakpoint as
`0x25E29C2C` and `0x25E60040`. Memory immediately below `dst` is unmapped, which
is worth knowing: it means `dst` really is the start of the output region, so a
valid distance can never exceed the number of bytes written so far. That gives
the decoder a cheap self-check and is what exposes a token that is one or two
short.

**The `bf/s` polarity is the easy thing to get backwards**, and it decides
whether a transcription refills correctly. `DT Rn` sets T = 1 when the result is
zero, and `BF/S` branches when T = **0**. So at `0x602B056`:

```
0602b056: dt r7
0602b058: bf/s 0x602b062     ; branch when r7 is NOT exhausted
0602b05a: shlr r6            ; delay slot - always runs
0602b05c: mov.b @r4+,r6      ; only reached when r7 WAS exhausted
0602b05e: mov #0x8,r7
0602b060: shlr r6
0602b062: bt 0x602b050
```

Reading it as "`bf` branches when T = 1" inverts the logic and makes the
decoder refill a fresh byte on *every* bit. The correct reading: bits remaining
(`r7 != 0`, T = 0) → branch over the refill, just `shlr`; exhausted (`r7 == 0`,
T = 1) → fall through, load a byte, reset the counter to 8, `shlr`. Either way
exactly one bit is consumed per iteration. The entry state is `r6 = *r4++`
(so `r6` is the first byte and `r4` already points at the second) and
`r7 = 9`, whose first `DT` yields 8 — which is why no refill happens before the
first bit is tested.

#### What the ground truth actually is

The disc-vs-live comparison gives an exact handle on the stream, and it is not
what a naive reading suggests. For `GAME1`:

| live offset | equals | length |
|---|---|---|
| `0x0000` | `disc[0x0000]` | 26,384 (`0x6710`) — plain code, untouched |
| `0x6710` | `0xAA` — **not from the disc** | 1 |
| `0x6711` | `disc[0x6710]` | 39,152 (`0x98F0`), ending exactly at disc `0x10000` |
| `0x10001` | `disc[0x10001]` | 4,400, then the images diverge |

The third row is the important one and it is **not** a copy-through: a literal is
copied *from the input stream*, so a run of N literals reproduces the input
shifted by however many bytes have already been emitted. A one-byte shift over a
39,152-byte window that re-aligns at a round `0x10000` boundary is therefore the
signature of a single long literal run — the loader wrote the `0xAA` byte at
`0x6710` itself, and the decompressed output starts at `0x6711`.

This also explains the **`+1` module-base drift** reported by the RAM tools, and
the `0.ram` size (86,401 vs 86,400): the dumps are each one byte longer than the
disc file because of that inserted byte, not because of a parser bug.

Two more negatives worth recording, because they are easy to re-derive wrongly:

* **The `.PRG` files have no header.** All eleven start with the *identical*
  0x22-byte SH-2 prologue (`2F E6 4F 22 D1 09 D2 0A ...`), code at +0x00. The
  0x40-byte header and the module-name table at +0x190 are properties of
  `0.BIN` alone. So `0x6710` is not a header field.
* **The split point is not an immediate in the boot module** — `0x6710` does not
  occur anywhere in `0.ram`.

#### State of the decoder: transcribed, not yet correct

The bit accounting was tested rather than assumed. Across 40+ combinations of
stream start (`0x670C`..`0x6715`), bit order (LSB-first per the `uVar7 >> 1` in
the decompilation, and MSB-first), literal sense (1 = literal per `uVar7 & 1`,
and inverted), refill sign-extension (the refills are cast `(char)`, the initial
load is not), and output base (pre-seed `0x6710`, the corrected `0x6711`, and
small in-place seeds `0`/`0x20`/`0x40`/`0x80`/`0x200`/`0x1000`):

* every small seed **fails on the very first back-reference** — with a large
  negative offset, so it needs the full code prefix as history;
* every full-prefix seed consumes only ~420-3,200 input bytes for ~1-10 KB of
  output and then stops on a spurious zero token;
* **none** reproduces more than 1 byte of the leading literal window, and the
  measured byte-match against live is ~1.5%, i.e. chance.

So the extend convention, the bit order, the literal sense and the output
geometry are all *ruled out* rather than assumed, and the defect is somewhere the
decompilation has not been read closely enough — most likely in the two `pbVar3`
/`pbVar4` pointer manipulations around the long form, which are easy to lose in
Ghidra's output because the same two variables alias the bit buffer and the data
pointer at different points.

The remaining unknown is the *caller*, and that is the concrete next step:
`FUN_0602b040` has **no callers and no big-endian pointer to it anywhere in
`GAME1.ram`**, so the call is indirect or comes from the boot module. `0.ram` is
now imported as `/SATURN_RAM/0.ram`; the parameters (`src`, `dst`, the stream
start and any length) should be read off a live breakpoint in the emulator
rather than inferred from the callee.

### 4b. Room data (`.RDT`)

> **Superseded by §4.0.** The "rebuild of the RDT format" reading below came
> from comparing *packed* bytes. Decoded, all 348 rooms equal the PS1 files.

348 room files with the **same names as the PS1** (58/58/36/36/44/58/58 across
`STAGE1..7`), plus 15 new ones in `STAGE8`. All 348 differ byte-for-byte. The
header is the same *kind* of structure — for `ROOM1010` the camera count
(byte 1 = 6) and the three ambient-light words at +0x06 (`b8 02  a8 02  f8 01`)
match the PS1 exactly — **but the 19 relative pointers are not at +0x48**, so
the pointer block moved and the file is a different size (`ROOM1010`: 161,572 B
vs the PS1's 297,300 B). The 20-byte camera-switch-zone records are recognisable
in the Saturn file but at a different offset and with different corner values,
so this is a **rebuild of the RDT format, not a copy of it**. None of
`tools/mine_room_scd.py`, `tools/dump_init_scd.py`, `tools/dump_room_masks.py`,
`tools/evt_disasm.py` or `tools/scd_widths.py` can read a Saturn RDT as they
stand; every room-related tool needs a Saturn reader first.

`STAGE8` is new and belongs to the Battle Game:

    ROOM8010 ROOM8020 ROOM8030 ROOM8040 ROOM8050 ROOM8060 ROOM8070
    ROOM8080 ROOM8090 ROOM80A0 ROOM80B0 ROOM80C0 ROOM80D0 ROOM80E0 ROOM80F0

15 arenas, room ids `0x01`–`0x0F`, 87,629–244,096 B each, and **no `.BSS`** —
where their backgrounds come from is one of the open questions (§7).

---

## 4d. Battle Game: unlock, cheat and entry (`TITLE.PRG`)

Read statically from the disc `TITLE.PRG` imported at `0x0602B000`
(`/SATURN_PRG/TITLE.PRG`, names applied). The "Battle Game" item on the title
menu is gated by one flag, **`g_BattleGameUnlocked`** (byte `0x0604479A`),
cleared at start-up and set by either of two paths in `TitleMain`
(`0x0602DD70`):

1. **Finishing the game (the save).** If a `BIOUDATA_SS` backup-memory save
   exists and reads cleanly, and **save byte `0xBF` == 1**, the flag is set.
   (The writer of that byte, presumably `ENDING.PRG`, has not been read.)
2. **The port-2 cheat.** Every frame, while **`g_TitleState` == 4** (the
   "press start" wait, before the menu opens) and the flag is still 0, it reads
   the port-2 pad record: `*0x0601471C + 0x168`, 0x18-byte records from the
   boot module's peripheral table. If the record's type byte is **2** (standard
   digital pad) and **`(word+4 & 0x0800) == 0`** and
   **`(word+2 & 0x0070) == 0`**, the flag is set and a confirmation sound
   plays. Saturn pad bits are active-low, and in SGL terms `0x0800` = Start
   and `0x0070` = X+Y+Z, so the cheat is **hold X + Y + Z + Start on
   controller 2 at the "press start" screen**. The mapping is checked by the
   neighbouring soft-reset test in the same loop: port 1, masks `0x0800` and
   `0x0700`, which decode to Start + A + B + C, the standard Saturn reset.
   (Not yet confirmed in play.)

**Menu effect** (`TitleStateMachine` `0x0602D508`, `TitleMenuCursorDraw`
`0x0602BDCC`). With the flag set, the menu gains an extra item: the cursor
wraps at `0x40` rather than `0x30`, and the default cursor becomes `0x30`
when there is no save. On confirm (state 7, `g_TitleMenuCursor >> 4`), item 2
goes to state 10 when unlocked (state 9 otherwise) and item 3 to state 9.
State 9 writes mode **1** and state 10 mode **2** to **`0x002FFFF2`**, a byte
at the top of low work RAM that survives the module swap. Both then write the
next-module value 10 to `0x0602AFFE`.

**The mode byte drives the Battle Game.** In `GAME1`, `0x0602B9AC` tests
`0x002FFFF2 == 2` and then ORs **`0x80000000` into `g_SystemFlags`
(`0x060A0940 + 0xA50`)**. That is the battle-mode bit which `LoadRoomRdt`,
`LoadRoomBss`, `SetupRoomLighting`, `LoadRoomSeBank` and `LoadRoomPcmBank`
test (§4.0). So the chain is: unlock flag → third menu item → mode byte 2 →
`g_SystemFlags` bit 31 → STAGE8 RDT with campaign-room assets.

### Module flow (`0.BIN` loader, `0x06004214`)

Every module writes its successor to **`g_NextModule`** (`u16` at
`0x0602AFFE`). The boot loader loops on it with a `switch`, so the order of
the name strings at `0.BIN+0x190` is **not** the index:

| index | module | notes |
|---|---|---|
| 0 | `TITLE` | |
| 1, 2, 6, 7 | `GAME1` | index = 0-based stage + 1: stages 1, 2, 6, 7 |
| 3 | `GAME2` | stage 3 |
| 4 | `GAME3` | stage 4 |
| 5 | `GAME4` | stage 5, the lab (hence the terminal in `GAME4`) |
| **8** | **`GAME5`** | **stage 8, the Battle Game** |
| 9 | `MOVIE` | |
| 10 | `BU_LOAD` | save loader and dispatcher |
| 11 | `BU_CK` | **boot entry**: the loop starts at 11; backup-RAM check, then `TITLE` |
| 12 | `ENDING` | |
| 13 | `RANKING` | Battle Game results |

Each `.PRG` starts with the same `crt0` (clear BSS, call `0x06016B1E`, call
`main` from the pointer at `+0x38`). The mains are `TITLE` `0x0602DD70`,
`ENDING` `0x0602DC28`, `RANKING` `0x0602E530`, `BU_LOAD` `0x0602B1E4`,
`BU_CK` `0x0602B4CC` and `GAME1` `0x0602B1F0`.

**Flows:**

* **Battle Game:** `TITLE` (mode 2) → 10 `BU_LOAD` (loads the save to
  `0x00200000`; mode 2 → next = 8) → **8 `GAME5`**. It runs STAGE8, and
  `0x0602B9AC` turns mode 2 into bit 31 of `g_SystemFlags` (§4.0: arena RDT
  with campaign-room assets).
* **Load game:** `TITLE` (mode 1) → `BU_LOAD` → next = saved stage + 1.
* **After a battle:** 13 `RANKING` → `TITLE`.
* **Ending:** 12 `ENDING` → `GAME1` with mode 4 when `0x002FFFFF` == 0,
  else `TITLE`.

### Ending: the unlock is unconditional (`ENDING.PRG` main `0x0602DC28`)

On entry, if a save exists and reads cleanly, `ENDING` sets **save byte
`0xBF` = 1** and writes the save back. Any ending counts. Before that it
updates a reward byte at `0x002FFFF6`:

* **bit 1** when the play time (`0x002F8684`) is `<= 0x9E340` = 648,000
  frames = **3 hours at 60 fps** — the PS1's rocket-launcher threshold.
* **bit 0** when the ending number `0x002FFFF5` (4-7) is above 5.

Both are consumed by the mode-4 new game in `GAME1` (§4f): rocket launcher
and the costume key (`ITEM_SPECIAL_KEY`).

A pad-1 press with mask `0x0F00` leaves the ending.

### `RANKING.PRG`: Battle Game results (main `0x0602E530`, state machine `0x0602D3F0`)

* Two tables of 10 records, `0xC` bytes each in RAM at `0x06043AD8`: rank
  letter, `u16` score, 3-letter name and time. The rank letter comes from a
  table at `0x06037B13`, indexed by score / 100. With no saved records the
  tables are filled with the name "CAP" and a default time.
* The records persist in **save bytes `0x00`-`0x9F`**, mirrored to
  **`0x002FFF00`**. Save byte `0xBF` is the unlock.
* A pending battle result (`0x002FFFA4 >= 0`) leads to name entry (starts
  "AAA") and a save yes/no prompt (state 9: Up/Down, Start/A/C confirm, B
  cancels). Then it returns to `TITLE`.

`BU_CK.PRG` is not Battle Game code: it is the standard backup-RAM check that
runs at boot (error codes −1…−5 mapped to messages from `/data/bu_data.pck`).

### `GAME5.PRG`: the Battle Game run (main `0x0602B1F0`)

**Start-up** (`0x06088698`) sets stage 7 / room 1 (`ROOM8010`) and the
battle block at `0x060A67A0` (`+6` = arena index = 1). It resets the result
area (`0x002FFFA0`, `0x002FFFA4`, `0x002FFFF7` = 0) and gives a loadout by
character (bit 0 of `0x002F5125`: 0 = Chris, 1 = Jill).

> **The loadout goes into the ITEM BOX, and the inventory starts EMPTY.**
> `battle_run_init` clears the 48 two-byte slots at **`0x002F8724`** — the item
> box; the inventory is the block after it, `0x002F8784` — and copies a
> zero-terminated `(id, qty)` list into them (Chris `0x06088660`, 23 bytes;
> Jill `0x06088677`, 33 bytes; the sprays are separate qty-1 entries). Its
> caller (`0x0602CA12`) first runs the new-game player init `0x0602F150`, whose
> `SetInitialItems` (`0x06045188`) skips the knife/spray list when battle bit 31
> is set and zeroes every inventory slot; afterwards HP is set to the maximum
> (`0x8C - 44*jill`, `0x0602CA32`) and `battle_run_init` clears health-status
> bits with `+0xDC &= 0x9D`. So the player arms themself from arena 1's box. The
> table below is the box's contents:

| | Chris | Jill |
|---|---|---|
| weapons | Knife, Beretta 15, Shotgun 7, Colt Python 6 | same **+ grenade launcher** (explosive, empty) |
| ammo | Clip 75, Shells 49, Magnum 48 | Clip 75, Shells 35, Magnum 36, Explosive/Acid/Flame 6 each |
| First Aid Spray | 4 | 5 |

It also records the **starting total per ammo type** (loaded + spare) at
`0x002FFFA8`-`0x002FFFAD`: Chris 90/56/54/0/0/0, Jill 90/42/42/6/6/6. The
spray count goes to `0x002FFFAE`. The game keeps what you started with,
presumably to score resource use.

**Exit** (`main`): with the battle bit (`0x80000000`, `GAME5`'s flag word
`0x060A67D0 + 0xA50`) set, it copies the `u16` at `0x002F5124 + 0x88` to the
pending result **`0x002FFFA4`** and sets next module **13** (`RANKING`). The
story exit sets 12 (`ENDING`) when bit 31 of `0x002F3004` is set; it also
stores the finishing character (`0x002FFFF1`), adds the frames to the play
time (`0x002F8684`), and sets bits of `0x002FFFF6` from story flags `0x73` /
`0x7E`.

**Arena sequence: one run through all 15 rooms.** The room-transition routine
(`GAME5` `0x0602DBBC`) has a battle branch. With the battle bit set and bit 2
of the battle block (`0x060A67A0`) set, it takes the next room as **room + 1**
(`0x002F8661`) and advances the arena index (`+6`) in step. It places the
player from a spawn table at `0x060953E0` (6 bytes per room, `0xAE` per
stage) and reads a 5-byte parameter record per index from `0x06095950`
(meaning not decoded). A per-room flag table at `0x060959A0` sets battle-block
bit 1:

| index = room | 1 | 2-5 | 6 | 7-10 | 11 | 12-15 | 16 |
|---|---|---|---|---|---|---|---|
| flag | 0 | 1 | 0 | 1 | 0 | 1 | 0 |
| room | `8010` | `8020`-`8050` | `8060` | `8070`-`80A0` | `80B0` | `80C0`-`80F0` | end of run |

This is the **normal door-transition code with the destination replaced**:
where a door record would supply the next stage / room / camera, the battle
branch takes them from the table instead. So the 5-byte records are the fight
rooms' virtual doors. **Battle-block bit 2 is a one-shot "advance" request**: the transition
routine consumes it and clears it at `0x0602EA64`.

**The clear condition: the per-frame battle state machine** (`GAME5`
`0x0608883C`, found with a write watchpoint on the block, then read
statically with the corrected `tools/saturn_sh2.py` — see §4e's warning). It
opens with a bounds-checked **5-way `switch` on the state** (`+4`): the
"computed goto" at `0x06088860` is `mova` + a halfword offset table at
`0x0608886C` (`000A 029E 05F0 0480 051A`), giving state 0 → `0x06088876`,
1 → `0x06088B0A`, 2 → the epilogue (nothing to do; the transition routine
owns it), 3 → `0x06088CEC`, 4 → `0x06088D86`. The battle block at
`0x060A67A0`:

| offset | field |
|---|---|
| `+0` | flags: bit 0 = **the run clock counts** (see "The run clock" below); bit 1 = fight room (from the per-room table); bit 2 = advance request; bit 3 knife held; bit 4 = **"can still fight"** (see below); bit 5 handgun, 6 shotgun, 7 Python, 8 grenade launcher held |
| `+4` | state: 0 fighting, 1 cleared, 2 next room, 3-4 end of run |
| `+6` | arena index (= room) |
| `+8` | **enemies remaining** (read live: 3 in the Cerberus room). Not the engine's enemy counter: see "How `+8` is maintained" below. |
| `+10` | state timer |
| `+12` | out-of-ammo frame counter |

* **State 0:** when bit 1 is set, `+8` == 0 and the player's HP (entity
  `0x002F5124 + 0x88`, tested as a signed **halfword** `>= 0`) the room is
  **cleared**: state 1, timer 0, bit 2 set, bit 0 cleared, a call to
  `0x0605B8D8(112, 112, 0, 0x060A10B0)`. HP is pinned to 1 if it was not
  positive, **saved** to `0x060A1680` (a `u32`), and the HP field is set to
  max HP (`u8` at `+0x175`) for the pause.
* **The fight-room bit (bit 1) is a property of the room, and two of the run's
  fifteen transitions are doors, not warps.** `battle_run_init` (0x060886F6)
  reads `0x060959A0[room]` for room 1 only:

  ```
  060886F6  E201  mov #1,r2
  060886F8  D13F  mov.l @(0xFC,pc),r1   ; 0x060959A0
  060886FC  012C  mov.b @(r0,r2),r1
  060886FE  611C  extu.b r1,r1
  06088700  2118  tst r1,r1
  06088702  8900  bt 0x06088706
  06088704  E102  mov #2,r1              ; set -> 2, clear -> 0
  06088706  2712  mov.l r1,@r7           ; block + 0
  ```

  so the bit is `2` or `0` and never the other flag bits. The rooms a DOOR
  reaches (1→2, 6→7, 11→12 — the rest rooms are the only arenas with a door
  record) get it from the room-load path instead, which is why a port that arms
  it only at run start and in the warp cannot clear the arena after every rest
  room: arena 1 never clears, so the warp never runs, and the door hands the
  player to arena 2 with bit 1 clear — the state-0 test then never passes.
* **How `+8` is maintained** — this is the part that is easy to get wrong, and
  getting it wrong is a softlock rather than a wrong number. `+8` is *not* the
  engine's enemy counter (`0x002F3022`); it is derived from it, every frame, by
  the entity pass at `0x06055474`:

  ```
  06055480  D360  mov.l @(0x180,pc),r3      ; r3 = 0x060A67A8 (block + 8)
  06055482  D161  mov.l @(0x184,pc),r1      ; r1 = 0x002F3022 (engine count)
  06055484  6210  mov.b @r1,r2
  06055488  612C  extu.b r2,r1
  0605548A  2311  mov.w r1,@r3             ; +8 = the room's slot count
  ...                                        ; walk the slots, stride 0x18C
  060555C8  901A  mov.w @(0x34,pc),r0       ; r0 = 0x88
  060555CA  011D  mov.w @(r0,r1),r1          ; r1 = slot->health
  060555CC  4111  cmp/pl r1                  ; T = health >= 0
  060555CE  8903  bt 0x060555D8              ; alive -> skip the decrement
  060555D0  D20C  mov.l @(0x30,pc),r2        ; r2 = 0x060A67A8 (block + 8)
  060555D2  6121  mov.w @r2,r1
  060555D4  71FF  add #-1,r1
  060555D6  2211  mov.w r1,@r2               ; one dead slot, one off +8
  ```

  (both PC-relative literal slots live after the loop, at `0x06055604` and
  `0x06055608`.) So `+8` = *slots allocated − slots dead*, and it only reaches
  zero once the last enemy is dead. The engine counter alone cannot stand in:
  nothing in this engine ever decrements it, because a corpse keeps its slot
  (the port's `zombie_dead_animation` only ORs `0x0E` into `status_flags`,
  leaving bit 0 up) and `update_entities` / `RoomInit` / the collision and aim
  walks all rely on `count == number of slots with bit 0 set`. `health < 0` is
  the engine-wide "dead" convention: `apply_weapon_damage` demotes to state 2
  only while `health >= 0`, and both aim-target walks test `-1 < health`.
* While fighting it clears bits 3/4, then walks the inventory
  (`0x002F8784`, 2 bytes per slot, `0x002F8667` slots) setting a bit per
  weapon held and summing into `0x060A1688` the ammo **usable with a held
  weapon**: loaded rounds in the handgun / shotgun / Python / launcher (ids
  2, 3, 5, 7-9) plus clips (11) only with the handgun, shells (12) only with
  the shotgun, magnum rounds (14) only with the Python, and grenade rounds
  (16-18) only with the launcher. If the sum is non-zero bit 4 is set;
  otherwise `+12` counts up and bit 4 is still set for the first 180 frames
  (`0xB3`), then left clear — three seconds' grace before "out of ammo".
* **State 1:** holds HP at max and clears bit 8 of `0x002FAB02` every frame
  for 120 frames (timer > `0x77`), then **restores the saved HP** from
  `0x060A1682` (the low half of `0x060A1680`). So **HP carries over between
  arenas**; the full bar during the pause is cosmetic. Then, for index ≤ 14:
  `0x002F845B` = 1, `0x002FAB02` = 0, bit 25 of `0x002F3000`, state 2,
  `0x002FFFF7` = 0 — the transition routine takes the next room. At index 15
  (Tyrant) it ends the run: `0x002FFFF7` = 1, a camera aimed from the player
  at a point (`0x060A4EA0`; square root `0x06089D58` and `atan2`
  `0x06089D90` into `0x060A169C`/`0x060A169E`), state 3, and a 7-frame
  loop that loads `/SND/BATTLE.CDP` (the string at `0x0608882C`) while
  **`0x060FFC1B` bit 1 holds the display** (§4f).

**The run clock** (`0x002FFFA0`, 60ths, capped at `0x57E04` = 99:59) is not
counted by the game loop. Its only writer besides the run init is the per-frame
interrupt routine `0x06036508` (installed as a callback from `0x0602B2B2`,
`0x0602B4C2`, `0x0602B858`, `0x0602C868`), right after the play timer
(`0x060AD01C`): with the battle bit up and **block bit 0 set** it adds 1 at
`0x0603662A`. The tests in between (`0x002F3000 & 0xFF00`, flag `0x4000`,
`+0xA9A`, `n % 60`) all branch *to* the add; they only decide whether the
`m:ss` text is redrawn. So the clock **keeps running in the inventory and the
item box**, and block bit 0 is the one thing that stops it:

| address | bit 0 | when |
|---|---|---|
| `0x0602CF3C` | set | the end of the game's start-up: first room loaded **and** the opening message closed (it spins on `0x002F8665` bit 7 at `0x0602CF0A` first) |
| `0x0602EAC6` | set | the end of the room transition routine `0x0602DBBC` |
| `0x0602DBDC` | clear | the start of that routine |
| `0x0602D9F0` | clear | the player's death: `0x0602D9C8`, called by the game loop (`0x0602D950`) on the first frame HP is negative (`0x0602D8C6`) |
| `0x060888BE` | clear | the arena is cleared (state 0 → 1) |

The clock therefore stops the moment the last enemy falls and stays stopped
through the 120-frame pause and the load of the next arena, and it also stops
across the three rest rooms' doors. `verify_saturn_battle.py` ("run clock")
checks every one of these words. Port: `saturn_battle_tick_run()` is called
from `main_loop()` and counts real time; `saturn_battle_clock_run()` is the bit.

Settled in §4f: what reaches the results screen is a **score** computed by
`RANKING` from remaining HP, remaining ammo and clear time; HP is not
refilled between arenas.

Flag 0 marks exactly the three rooms without enemies and the end. Those three
are **rest rooms**, the only arenas with a door record (`8010 → 8020`,
`8060 → 8070`, `80B0 → 80C0`), and each boss room leads into one. So a run
is `8010` → four fights → `8060` → four fights → `80B0` → four fights → the
end (`RANKING`).

> **The flag table is 17 single BYTES, one per room, bit 0 = "has enemies"** -
> not u16 pairs, which "17 bytes" invites and which gives an irregular table
> with two hits that matches nothing. Read as bytes:
>
> ```
> python -X utf8 -c "d=open('assets/SATURN_ISO_CONTENT/GAME5.PRG','rb').read();\
>   o=0x060959A0-0x0602B000; print(' '.join('%02x'%b for b in d[o:o+17]))"
> -> 00 00 01 01 01 01 00 01 01 01 01 00 01 01 01 01 00
> ```
>
> Bit 0 is clear at exactly **1, 6 and 11** (and at 0 and 16, which are not
> arenas) - the three rest rooms, which is the same set as the door records.
> That is the check on the reading.

**"STAGE CLEARED"** is at `GAME5 0x060A10B0`: thirteen bytes in the **HUD**
font, drawn at (112, 112) - five glyphs, a space, six glyphs, which is the whole
sentence and **no exclamation mark**. It is not in the message encoding, which is
why searching for it as a `STR()` string finds nothing. The bytes either side are
HUD font data, not text.

**Each fight room is one enemy type, and every fourth one is a boss.** Types
are `enemy_set` byte `[1]`, the index into `enemies_update_functions_tbl`:

| after rest room | fight rooms (type × count) |
|---|---|
| `8010` | `8020` Zombie ×3 → `8030` Cerberus ×3 → `8040` Crow ×6 → **`8050` Yawn** |
| `8060` | `8070` Web Spinner ×2 → `8080` Plant 42 (1 + 5 parts) → `8090` Hunter ×3 → **`80A0` Black Tiger** |
| `80B0` | `80C0` Zombie (naked) ×4 → `80D0` Chimera ×2 → `80E0` Chimera ×2 → **`80F0` Tyrant** |

These are the stock PS1/PC type ids. None of the six Saturn-only models
(§5) appears as a type, so if the Battle Game uses them it is by some other
route. Fight rooms have no door; they advance through the transition routine
above.

> **The `0x78` / `0xF4` "unknown opcodes" were a tool bug, not Saturn
> commands.** The Saturn dispatch table (`GAME5` `0x06099384`, 79 entries
> `0x00`-`0x4E`, script pointer `0x002FF640`, interpreter `0x0604C312`) has
> the PC's first 79 opcodes with the same widths. But
> `mine_room_scd.py` / `dump_init_doors.py` had `omodel_set` (`0x1F`) at 4
> bytes. Both the Saturn handler (`0x0603F2E8`) and the PC's
> `cmd_omodel_set` advance `0x1C` (28). Fixing it, together with the other
> three corrections already in `rdt_event_editor.html` (`0x2D` → 4, `0x34`
> → 8, `0x46` → 44 bytes total), makes every arena decode, and it also fixes
> any campaign room using those commands.
>
> The interpreter's rule matters when reading scripts: a handler that returns
> 0 (a failed test, or `block_end`) **ends the block**, so bytes after a guard
> test may never run.

Still open: the menu item order (item 1 goes to title state `0xC`).
**Resolved since:** the 5-byte per-room records and the spawn table's index
expression (§4e); the state machine's switch, the score formula, the ranking
ladder, what `0x060FFC1B` is (not a reward) and mode 4 (§4f).

### 4e. RESOLVED — the arena transition's 5-byte record and spawn table

The room-transition routine is `GAME5` `0x0602DBBC` (now
`room_transition_load` in Ghidra). Its battle branch and the two tables it
reads are decoded below. Re-checked with the **corrected**
`tools/saturn_sh2.py` (see the warning at the end: the first version of that
tool was itself wrong); the record and table conclusions survived, `rec[1]`'s
destination and the spawn-record field order did not.

**The 5-byte record is a compressed door record.** The normal (non-battle)
path of the same function takes five fields out of a full door record at
offsets `+0x08`, `+0x09`, `+0x0A`, `+0x0B` and `+0x0D` — the same five, in
the same order, as the PC's `door_set` record (`tools/dump_init_doors.py`,
and `src/Globals.h:626-630`). The battle branch substitutes a 5-byte table
for the door record and writes **the same five globals**:

| rec byte | destination | name here | door-record offset |
|---|---|---|---|
| 0 | `*(u32*)0x060AD450` | `g_next_room_door_type` (direction, door var 0) | `+0x08` |
| 1 | `*(u32*)0x060AD464` | `g_next_room_sfx_id` | `+0x09` |
| 2 | `*(u32*)0x060AD460` | `g_next_room_be05b7` (the `.dor` type) | `+0x0A` |
| 3 | `*(u32*)0x060AD45C` | `g_next_room_camera_id` | `+0x0B & 0x3F` |
| 4 | `*(u8*)0x060A72C3` | the transition **flags** | `+0x0B & 0xC0` |

> **CORRECTION 2026-09-29 — `rec[4]` is the flags byte, not the destination.**
> `0x060A72C3` = `0x060A67D0 + 0xAF3`, and the normal path fills that byte with
> `record[0x0B] & 0xC0` (`0x0602DF96`-`0x0602DFA4`), the PC's "camera-only"
> (`0x80`) / "no door sound" (`0x40`) bits. The destination global
> `0x060AD454` gets `r12` in the battle branch, which is 0 there. `rec[4]` is
> `0x40` for arenas 2 and 14, i.e. those two warps are silent. The door-sound
> index `rec[1]` uses the PC's numbering: GAME5's own sound names at
> `0x0604C530` (`DOORWOD, DOORMTL, DOORBRK, DOORREB, …, DOORELV, …, DOORAIR,
> DOORLAB, DOORFTN`) are in `g_roomSoundEffectsTable`'s order.
>
> **This is a door transition, animation and all.** The battle branch runs
> inside the ordinary transition routine, so the port hands a door record built
> from these five bytes plus the spawn record to `door_begin_transition()` and
> lets `room_transition_load` play it. The first port called `room_set()`
> directly: no door animation, no door sound, and the player's Y and animation
> left over from the previous arena.

The first three destinations are reached in this function by *two* literal
slots each — one in the battle branch, one in the normal path — which is what
makes the pairing certain rather than assumed. `rec[4]`'s address is
computed: the halfword `0x0AF3` at `0x0602DEC4` is added to `r9`, which holds
`0x060A67D0`, giving `0x060A72C3`. The routine also stores `0x060A67D0` into
`*(u32*)0x060AD454`, i.e. the arena's own flag word.

**The room is not in the record.** The transition simply increments it:

```
0x0602DE3E  room   (0x002F8661) += 1
0x0602DE48  arena index (g_battle_block + 6) = index + 1
```

So the 5 bytes carry only what `room + 1` cannot supply. That is the whole
point of the table, and it is why the records are so small.

**The table is 16 records of 5 bytes at `0x06095950`**, indexed by
`g_battle_block->arena_index` (values 1-15, entry 0 unused). The count is not
a guess: 16 × 5 = 80 bytes ends at `0x0609599F`, and the per-room flag table
begins on the very next byte at `0x060959A0`.

| idx | room | rec | | idx | room | rec |
|---|---|---|---|---|---|---|
| 0 | — | `00 00 00 00 00` unused | | 8 | `8080` | `00 01 08 00 00` |
| 1 | `8010` | `00 00 00 00 00` | | 9 | `8090` | `0B 01 08 00 00` |
| 2 | `8020` | `01 06 15 00 40` | | 10 | `80A0` | `00 01 08 00 00` |
| 3 | `8030` | `02 00 1E 05 00` | | 11 | `80B0` | `00 00 00 00 00` |
| 4 | `8040` | `02 00 07 01 00` | | 12 | `80C0` | `0B 01 0B 03 00` |
| 5 | `8050` | `00 00 00 04 00` | | 13 | `80D0` | `00 01 0B 03 00` |
| 6 | `8060` | `00 00 00 00 00` | | 14 | `80E0` | `0A 0C 0D 00 40` |
| 7 | `8070` | `03 00 20 05 00` | | 15 | `80F0` | `00 00 00 00 00` |

The all-zero entries are exactly the rooms that need no virtual door: the
three rest rooms (`8010`, `8060`, `80B0`, which have a real `door_set` in
their RDT) and the Tyrant room `80F0`, where state 1 goes to state 3 and ends
the run. `rec[3]` (the camera) is 0, 1, 3 or 5, and `rec[4]` has bit 6 set
(`0x40`) for `8020` and `80E0`.

**The spawn table's index is now exact.** `0x060953E0`, 6 bytes per room,
`0xAE` (174 = 29 × 6) per stage, and the offset arithmetic is literal:

```
0x0602DE4A  r0 = 3 * newRoom
0x0602DE5A  MACL = 0x00AE * stage        ; 0x00AE from the halfword at 0x0602DEC6
0x0602DE62  r0 = 6 * newRoom + 0xAE * stage
0x0602DE66  r3 = 0x060953E0 + that
```

so the record is at `0x060953E0 + 0xAE*stage + 6*room`, with `room`
the room byte (`0x002F8661`) and `stage` the 0-based stage. The record is three
`u16`s: `+2` goes to player `+0x34` and `+0x6C`, `+4` to `+0x3C` and `+0x70`,
and `+0` to `+0x74`; `+0x38` and `+0x6E` get register values (`r12`,
`r14`) that are not from the table. That is x / z / facing, with y left to
the caller. The 6-byte stride has its `+0` first, so the index arithmetic is
unchanged.

> **Re-check 2026-09-29 (later): the arithmetic above is right after all.**
> `0x060953E0 + 0xAE*7 = 0x060958A2`, and `+ 6*room` puts room 1 at
> **`0x060958A8`** — the block found by content below. The "`0x06095886`" in the
> original correction was an addition slip. Index it as `0x060958A2 + 6*room`
> (equivalently `0x060958A8 + 6*(arena-1)`); the room is the NEW room, read after
> the increment at `0x0602DE3E`. The rest of this note is kept for its content
> check.
>
> **CORRECTION 2026-09-29 — the arithmetic above does not land on a record
> boundary.** `0x060953E0` leaves remainder 2 modulo 6, so a record start must
> too; `0xAE * 7` gives `0x06095886`, which leaves remainder **4**. The Battle
> Game's block was therefore located by content instead: exactly **15**
> consecutive records sitting between the campaign records and the five-byte
> table at `0x06095950`, every direction a multiple of `0x400` (one `0x3E0`),
> every coordinate a multiple of 4. That block is **`0x060958A8`**, and it is
> what the port uses (`tools/verify_saturn_battle.py` re-reads it out of
> `GAME5.PRG` on every run). Two records corroborate it: arena 15 is campaign
> room 513, the Laboratory, and its `(0x400, 4700, 25500)` sits beside the
> second Tyrant's capsule at `(0x1C00, 0x6600)` = `(7168, 26112)`. The
> `0xAE`-per-stage model is still the right *shape*; the constant above is not
> the right offset, and the table is not simply `6 * arena` from the block
> start — index it as `6 * (arena - 1)`.
>
> The room byte is the **arena number** 1..15, and that is also what the file
> name says. The 15 arena files are `ROOM8010` .. `ROOM80F0`, and the port's
> `LoadRoomRdt` builds `room<stage><roomHi><roomLo><variant>` — so `ROOM8010`
> is stage 8, room **0x01**, variant 0, and `ROOM80F0` is room **0x0F**. One file
> per arena, no per-character variant. Reading the two middle digits of the
> filename as a single room byte asks for `ROOM8100`, which is in no tree — that
> is the mistake the port made first, and `tools/verify_saturn_battle.py` now
> derives the room byte from each of the 15 filenames and requires arena N's
> file to imply N.


The flag table read at `0x0602DEA0` is `0x060959A0`, 17 bytes, index 1-16
with 16 = end of run. It sets battle-block bit 1, which is what makes the
per-frame state machine clear the room.

#### The warning this section has to carry

> **The first `tools/saturn_sh2.py` was wrong, and so were the claims it
> made about Ghidra.** It had been tuned until the Saturn code "made sense"
> and its self-test vectors were written to agree with it. Measured against
> the Hitachi SH-1/SH-2 programming manual it misdecoded the commonest words
> in a module: `4n0B` (`JSR @Rn`, every call) was printed as a store
> `mov.b rN,@r0`; `C8`/`C9` (`TST`/`AND #imm`) were `cmp/eq`/`tst`; group 3
> was mis-keyed (`CMP/GE` = `3nm3`, `CMP/HI` = `3nm6`, `SUB` = `3nm8`,
> `DT` = `4n10`); `2nmE`/`2nmF` are `MULU.W`/`MULS.W` (there is no
> `CMP/HI` in group 2, so "Ghidra reads `0x251E` as `CMP/HI`" was the old
> table talking); conditional branches used a 7-bit displacement,
> `BRA`/`BSR` an unsigned 8-bit one (they are signed 8 and signed 12); and
> `1nmd`/`5nmd`/`84`/`85` displacements were unscaled or mis-fielded.
> The rewrite decodes every group by the manual's bit pattern and its
> self-test vectors are worked out from those patterns.
>
> Consequences: §4d's state-machine table, and §4f's ladder, record layout,
> HP cap and "reward bits", were misreadings and are replaced. §4e's tables
> survived because they are table *contents*. §4c's LZ77 listing is
> **correct**: re-disassembled with the fixed tool, `GAME1` `0x0602B040`-
> `0x0602B0CA` matches it instruction for instruction (the old note calling it
> "illustrative only" came from the broken tool and is withdrawn).
> Ghidra's SLEIGH has **not** been shown to be wrong about anything; treat
> the old claims as retracted rather than confirmed.

### 4g. The Battle Game's Yawn (arena 5) — there is NO exclusive AI

Asked whether arena 5's Yawn (room `0x05`, one `enemy_set`: id `0x0D`,
behaviour `0x02`, death flag `0x18`, spawned at x `0x2AF8` z `0x2710`) uses a
Battle-Game-only AI. **It does not.** The whole Yawn is one function chain and
it is shared; only two decisions branch on the battle bit.

The battle bit is bit 31 of `g_SystemFlags` = `0x060A67D0 + 0xA50`. `GAME5`'s
module entry sets it from mode byte 2 (`0x0602B9AC`: `cmp/eq #2` on the byte at
`0x002FFFF2`, then `or #0x80000000` into `[0x060A67D0+0xA50]`).

* **Entity update table** `0x0609B4F0`, 48 entries indexed by `Entity+1`:
  `[12]`/`[16]` = `0x06079B70` (Tyrant 1/2), `[13]`/`[18]` = `0x0607F79C`
  (**Yawn 1/2**), `[0]`/`[1]`/`[17]` = `0x0605EB8C` (zombies). So arena 5 runs
  exactly the code the campaign runs.
* **Behaviour selector table** `0x0609FD88`, indexed by `behavior_flags & 7`:
  `[0]`/`[2]` → `0x0607FE38` (the free-roaming tree), `[4]`/`[6]` →
  `0x060800AC` (the cutscene tree), odd → `NULL`. **Byte-identical to the PC's
  `0x004b1a20`.** No fifth entry, so there is nowhere for a battle-only AI to
  live.
* **`yawn_pick_action_normal` (`0x0607FE38`) matches the PC clause for clause**,
  including the constants that look room-specific: bite `< 2000` (`0x07CF`),
  lunge `0x16E * 1000 + 5000` (`0x1388`), rear-up `< 7000` (`0x1B57`), swallow
  `< 4000` (`0x0F9F`), the hard-coded `localMatrix.t[2] > 17000` (`0x4268`)
  shrink clause, the wedged counter `0x16C > 0x96`, and the poison-shrink.
* **What DOES branch, and what the port does about it.** All three of these are
  read off the raw words, not off `saturn_sh2.py`'s operand annotations — which
  are wrong here. The tool prints the PC-relative *displacement* and the *slot*
  inconsistently for the `Dnnn` form (it picked `0x002FAB14`, the current-entity
  pointer slot, where the correct slot `0x0608009C` holds `0x060A67D0`). The real
  rule, calibrated on `0x0602B9BC`'s `D13D` → slot `0x0602BAB4` = `0x80000000`,
  is **displacement = low byte of the instruction, in 4-byte units** for
  `Dnnn`/`5nnn` and in halfwords for `9nnn`. Re-read every address below that
  way; `docs/SATURN_VERSION_ANALYSIS.md`'s older listings in this area are suspect.
  > **CORRECTION 2026-09-29 — items 1 and 2 were read backwards.** `tst`
  > raises T when the tested bit is **clear**, and `bf` branches when T is
  > clear, i.e. when the battle bit is **set**. Both decisions therefore take
  > their branch in the **Battle Game**. Corrected below; the port had given the
  > Saturn's campaign Yawn 350 HP and no flee because of it.

  1. **Opening HP** (`0x0607F294`-`0x0607F2EE`): `and`/`tst` bit 31 of
     `[0x060A67D0 + 0xA50]`; `bf/s 0x0607F2E8` is taken when the bit is
     **set**, and the target loads `0x015E` (350); the fall-through loads
     `0x0BEA` (3050) — **battle 350, campaign 3050**, the PC's value. Yawn 2
     (`id 0x12`) is `0x0190` (400) on both paths.
  2. **The low-health flee** (`0x06080048`-`0x06080068`): threshold `0x0AEF`
     (2800), state word `0x01010700` (= `0x00070101` little-endian), **and**
     bit 31 of `[0x060A67D0 + 0xA50]` — `bf 0x0608006A` skips when it is
     **set**. So the Saturn's campaign Yawn flees exactly like the PC's, and
     only the Battle Game's cannot — which it must not: with 350 HP it is below
     the threshold from the first frame.
  3. **Damage has no difficulty flag on the Saturn** (all modes). A body hit is
     a flat `add #-15` (`0x0607F8CC`; GAME1 `0x06077FF4` is the same code) — the
     PC's `SECOND_PLAYTHROUGH` split (5 per hit, +15 on joints 0-9) is not
     there; the value is computed once and stored twice when the joint index is
     below 10, the shape a folded-away `if` leaves. The bite is a flat
     `add #-28` (`0x060805EE`) where the PC does 10 or 28 on the same flag. The
     Battle Game starts from a loaded save, so the PC rule could make arena 5's
     Yawn take a third of the damage.
  4. **No venom in the Battle Game** (`0x060806DC`-`0x060806F6`): the Yawn-1
     bite still raises the "poisoned" story flag, but the `+0xDC |= 0x20` status
     is skipped when the battle bit is set.
     The spiders' venom is switched off the same way: the projectile splash
     test (`0x06049D30`, the PC's `effect_projectile_hit_check` `0x0043bd90`)
     tests the battle bit at `0x06049D4A`-`0x06049D56` after the id 3/4 test
     and `bf`s over the two `rand()` calls and the `+0xDC |= 2` /
     `+0x174 = 0x96` pair; the 15 / 30 damage still lands. So the Yawn and
     the spiders do not poison in the Battle Game, and `battle_run_init`
     clears both bits (`+0xDC &= 0x9D`). The Adder and the Wasp, the
     other two poisoners, are in no arena, so their writes need no test.
     Play-tested 2026-10-10.
  3. **The selector is unchanged.** The arena-5 `enemy_set` carries behaviour
     byte `0x02`, so it runs the free-roaming tree — `[0]`/`[2]` of the identical
     PC/`0x0609FD88` tables. The arena Yawn is a **Second-Yawn** fight in the one
     sense that matters: it does not flee. That is **one clause removed, not a
     change of tree.** Switching it to the cutscene tree is the tempting reading
     and it is wrong — the cutscene tree also drops the wedged-reposition
     clause, so a snake that jams against the arena's collision has no way out,
     stands still and never reaches bite range. (That was tried and observed:
     killable, but it stopped attacking.) Keep the free-roaming tree, lose only
     the flee.

* **Action table — the base is `0x0609FD64`, not `0x0609FD68`.**
  `yawn_check_actions` (`0x0607FDB4`) loads `0x0609FD64`; reading the table from
  the wrong base shifts every entry by one and makes action 8 look like the
  flee. Correctly:

  | action | addr | | action | addr |
  |---|---|---|---|---|
  | 0 idle | `0x06080254` | | 5 entry | `0x06081328` |
  | 1 move | `0x06080300` | | 6 emerge | `0x06081554` |
  | 2 bite | `0x06080470` | | 7 **flee** | `0x060819CC` |
  | 3 rear | `0x06080924` | | 8 **reposition** | `0x06081CFC` |
  | 4 swallow | `0x06080B1C` | | 9 | `0x0609FD88` = the selector table |

  Action 9 is NULL on the PC and unused on the Saturn; its slot is simply the
  next address, which is where the selector table begins. So there is no gap.

* **The reposition ladder is the PC's.** `0x06081CFC` is action **8**: start
  at index 3, `x > 9000` (`0x2328`) → 2, `z <= 18999` (`0x4A37`) → 1,
  `z <= 14999` (`0x3A97`) → 0, over the waypoint table `0x0609FDE4` =
  `(6500,7500) (11500,19500) (7500,24500) (11500,24500)`. That is exactly the
  port's `yawn_action_reposition`. (An earlier note here compared it against
  a different, stale ladder and called it a difference; it is not.) Action 7, the flee
  (`0x060819CC`), has not been disassembled — and with the flee unreachable in
  Saturn mode there is nothing left that reads its table.

### 4f. The Battle Game's score and ranking

Read with the corrected `tools/saturn_sh2.py`. This replaces an earlier
§4f that had the ladder as `(V-1)/32`, the record's time overwriting the
score, a hard 140 HP cap and `0x060FFC1B` as "reward bits": all four were
decoder errors.

**What `GAME5` hands over.** At exit the (shared, all-`GAME` modules) exit
path stores `(s16)player[+0x88]` — remaining HP — as a `u32` at
**`0x002FFFA4`**, so its low half **`0x002FFFA6`** is the HP. The other inputs
live next to it and survive the module swap:

| address | what | writer |
|---|---|---|
| `0x002FFFA0` | frame counter, +1 per frame, capped at `0x57E04` (99:59 at 60 fps) | `GAME5` `0x0603662A` |
| `0x002FFFA6` | remaining HP | exit path |
| `0x002FFFA8`-`AD` | **remaining** rounds per type: handgun, shotgun, magnum, then (Jill) explosive / acid / flame | start totals from `battle_run_init` `0x06088698`, **−1 per shot** (`0x060337B8`, a jump table on weapon type) |
| `0x002FFFAE` | First Aid Sprays | `battle_run_init` |
| `0x002FFFF1` | character, bit 0: 0 Chris, 1 Jill | exit path |

**The score** (`RANKING` `0x0602C3AC`, stored as `u16` at `0x06043BC8`).
Weights are the `u16` table at `0x06037AC0` = `1, 0, 2, 6, 10, 15, 15, 15, 150`:

```
maxHP  = 140 - 44 * jill                 ; Chris 140, Jill 96 (the PS1 values)
ammo   = 2*handgun + 6*shotgun + 10*magnum
       + (jill ? 15*(explosive + acid + flame) : 0)
       + 150*sprays
life   = 100.0 / maxHP * HP * 1.0        ; doubles; 100.0 at 0x0602C3A0
base   = (unsigned)(ammo + life)         ; added as doubles, then truncated
secs   = frames / 60                     ; 0x88888889 >> 37
left   = max(0, (jill ? 420 : 720) - secs)
score  = (u16)(base + left + 50 * (left / 60))
```

The soft-float calls are `0.BIN` library routines, identified statically.
`0x06012834`, `0x06012678` and `0x06012238` are the same wrapper — unpack
both operands (`0x06011F7C`), call a core, repack (`0x06011E04`) — and the
cores settle which is which: `0x060126E0` **subtracts** the unpacked
exponents (`+8`, at `0x0601276A`) and runs a shift-subtract loop → **divide**;
`0x0601231C` **adds** them (`0x06012544`) → **multiply**; `0x06012064`
aligns by exponent difference → **add**, confirmed by `RANKING`'s own
double→unsigned `0x060303D0`, which calls `0x06012238` with −2147483648.0 to
bias values ≥ 2³¹. `0x06012C18` is int→double. So the life term is
`(100.0 / maxHP) * HP * W[0]`, as above. The screen's labels sit next to the function:
`SCORE`, `CHRIS`, `JILL`, `LIFE`, `CLEAR TIME`, `RANK`.

**The letter** is `table[score / 100]` (`dmulu.l 0x51EB851F`, `MACH >> 5`)
over the halfword-spaced table at `0x06037B13`:
`J I H G F E D C B A`, then `S` for every higher index — so 0-99 is J,
900-999 is A, and **1000+ is S**. All three readers (`0x0602CCBA`,
`0x0602D5C8`, `0x0602D6A0`) use the same expression. With Chris's full start
(1056 ammo points + 600 for sprays), S is within reach without firing
carefully; spending ammo is what costs rank.

**The tables.** Two tables of ten 12-byte records at `0x06043AD8`, **one per
character** (`120 * jill`): `+0` letter, `+2` score (`u16`), `+4..+6`
name, `+7` 0, `+8` clear time in seconds (`u32`: hours×3600 + minutes×60
from the default records). The placement routine `0x0602C8C0` walks the
character's table comparing the new score with each `+2` (`cmp/hs`, so a tie
keeps the older entry) and shifts the tail down; `0x06043BD0` = placement,
99 = not ranked. With no result pending (`0x002FFFA4 < 0`) score is 0,
time `0x34B84` and placement 99.

**`0x060FFC1B` is a display-control byte, not a reward.** It is GBR+`0x1B`
of the `0.BIN` system work area (GBR = `0x060FFC00`). The VBlank handler
(`0.BIN` `0x06016420`) skips the VDP1 frame change while **bit 1** is set, and
`0x060162E4` skips a VDP2 register update while **bit 0** is set. Every
`GAME` module sets bit 1 around synchronous loads; the battle state machine
does it around loading `/SND/BATTLE.CDP`. It has no Battle Game meaning.

**The real rewards are the PS1's, and they go to mode 4.** `ENDING` sets
bits of `0x002FFFF6`: bit 1 when the play time is ≤ 3 hours, bit 0 when the
ending number `0x002FFFF5` (4-7, set per character at the ending by `GAME1`
`0x0602F2A0`) is above 5. It then writes **mode 4** to `0x002FFFF2`
(when `0x002FFFFF` is 0) and next module 1. In `GAME1` `0x0602CB64` mode 4
is a **new game with rewards**: story flag 123; bit 1 → flag 126 and item
**10 (rocket launcher)** ×1; bit 0 → flag 115 and item **`0x38`
(`ITEM_SPECIAL_KEY`, the costume key)** ×1; play time 0; stage 0, room 6.
Nothing in the Battle Game reads or writes `0x002FFFF6`.

Everything in the formula is now read statically; a play-test would only
confirm it end to end.

### 4g. Saturn-only content is added in code, not in room data

Every campaign room's `enemy_set` list is identical to the PC's (all 348
compared, `mine_room_scd.py` over the decoded RDTs), and no room — arenas
included — scripts type `0x16` or `0x17`. The new creatures and props are
added by the modules. Read with the corrected `saturn_sh2.py`; addresses are
`GAME5` unless noted.

> **The same code is in every `GAME` module, but NOT at the same address.** The
> eleven overlays are different compilations of the same source, so every code
> address in this section is module-specific. A concrete case: the extra-model
> loader that spawns the lab Tyrant capsule is at `0x0603DBA4` in **GAME4** and
> `0x0603CBD4` in **GAME5** — 0x30 bytes apart, same instructions. Disassemble
> the module you actually mean, and do not carry an address across from one
> module to another. (The data tables move too: the `A_*` name table is at
> base `0x0609B174` in GAME5 and base `0x06096350` in GAME4 — check it by
> confirming index 28 reads `a_plt2`, because the base is not where the first
> `A_*` name sits.)

**The model table grows by three types.** The per-character EMD name table
(`0x0609AD5C`, 53 × 12-byte entries per character, second block at `+0x27C`
for Jill; model index = type + 4, indices 0-3 are the players) fills PC
placeholder slots:

| type | index | Saturn | PC |
|---|---|---|---|
| `0x16` | 26 | `em1016` / `em1116` | `em100a` |
| `0x17` | 27 | `em1017` / `em1117` | `em100a` |
| `0x1F` | 35 | `em1018` / `em1118` (**not on the disc**) | `em100a` |

The EMDs' first section (shared within a creature family) puts `EM1016` with
`EM1006` (Hunter) and `EM1017` with the zombies `EM1000`/`EM1001`/`EM1011`.

**Tick** — a model swap in the loader `0x0604EA80`: when the stage byte
`0x002F8660` is **2** and the model index is 10 (Hunter, `em1006`), index 26
(`em1016`) is loaded instead. The entity keeps type 6 and the Hunter's tables,
**but the module that holds stage byte 2 (`GAME2.PRG`) carries its own build of
the Hunter's AI** — a stretching-arm stab, its own keyframe animator and a
retuned decision layer. An earlier version of this note said the Tick "runs the
Hunter's AI"; that was only ever checked for the table slot, not the code. See
§4i.

**Zombie Wesker** — a **model** override in the enemy model loop
(`0x0603C9E8`), not a new type: in battle mode, arena index **12** (`80C0`,
scripted as four naked zombies), the **first** slot's id becomes **`0x17`** and
its behaviour byte (`+2`) **9**; the loader records `0x17` as the last model,
loads model `0x17 + 4` = **`em1017`**, and then (`0x0603CA98`, the same three
tests) **writes the original id back**. The update table's `0x17` entry is a bare
`rts` (`0x060841BC`, shared by every unused type), so Wesker never runs as type
`0x17`: it is the stock zombie with behaviour 9. Behaviour 9 is where the Saturn
differs from the PC (read 2026-09-30):

* the zombie table `0x0609F30C` has its behaviour view at `[9]`, and
  **behaviour 9 = `[18]` = `0x0605EEA4`, the chase** (behaviours 0/1) — the
  PC's slot is NULL;
* `zombie_init` **doubles the HP** for behaviour 9 (`0x0605E968`:
  `(+2 & 0xF) == 9` → `hp += hp`), right after the random roll;
* the bite reads the Saturn's **only** damage row, `0x0609F3F8` =
  `12 12 9 9 12 12 12 12 12 12`, with no scenario-flag test — the PC's "hard"
  row, and behaviour 9 bites for 12 where the PC's rows have 0.

**Golden Tyrant** — a palette tint (`0x0603BD0A`): at room load, in battle
mode, arena index **15**, the enemy model's CLUT is copied and then passed to
`0x0603A680(clut, n, +4, +3, -5)`, which adds a signed offset per RGB555
channel with a 0..31 clamp, bit 15 kept: **red +4, green +3, blue −5**. Same
Tyrant (type `0x0C`), same mesh, gold palette. The tint sits in the shared EMD
texture loader (`0x0603BC78`, called from `0x0604ED40`), so it applies to every
model loaded in that room. **It is also a stronger Tyrant**: the Tyrant's init
(`0x06079A12`, battle bit set) gives it **`0x02BC` = 700 HP** (campaign Tyrant 1
= 220, Tyrant 2 = 600), puts it on the floor (`+0x38 = 0`), behaviour `0x10`,
state word `0x01000100` (state 1 / behaviour 1 instead of the stasis pod),
`+0x179 |= 3` and `+0x186 |= 8` (no second-Tyrant dissolve); its update
(`0x06079B78`) clears behaviour bit `0x80` every frame in battle mode. The
arena's music is the stock Tyrant theme (group `0x34` channel 2, the PC's
`Bgm_24`; confirmed in play 2026-09-30).

**The end of the run** (GAME5 states 1→3→4, `0x06088B4C`-`0x06088E5C`): after
the Tyrant's 120-frame pause the room camera's eye/target become an orbit; the
Tyrant theme stops, `/SND/BATTLE.CDP` song 1 (the fanfare) plays; state 3 fades
the background colour offsets to −256 and switches the room sprites off
(36-byte records, `+32` = active); state 4 orbits for `0x18F` frames — target
eases `>>4` toward the player at `y − 0xA00`, eye at target + `R_y(angle)·(0,0,r)`,
`angle −= timer − 0x1C2` (16-bit units), `r += (0x400 − r) >> 6` — then raises
bits 0-1 of `g_SystemFlags` and GAME5 hands over to `RANKING`. The clear pause
(state 1) also clears **bit 8 of `0x002FAB02`** (the Saturn's `g_message_flags`)
every frame, which is what locks the pad and the menu. The helpers are `0.BIN`
shift ladders: `0x0600C24C` = `>> 14` (camera fixed point), `0x0600C25C` = `>> 6`.

**Results** (`RANKING.PRG`): with no stored records both characters' tables are
filled (`0x0602D58E`) with score `250 − 20·i`, its letter, name `"CAP"` and time
`0x34B84` (displayed 59:59). The screen labels (`SCORE`, `CHRIS`, `JILL`, `LIFE`,
`CLEAR TIME`, `RANK`, `RANKING`) are strings at `0x0602C58C`/`0x0602CA00`; the
item-name rows are drawn from art in `BATTLE.BIN` / `WAPON3.BIN` (decoded
below).

**Zombie Wesker keeps his head** (`GAME5 0x0604AE00`, the Saturn's
`enemy_hit_reaction_zombie`). The PC's head-explode rule is unchanged (shotgun
inside 3000 with the aim off level, Python on a level hit) but before it sets
`health = -300` it tests `(entity word & 0x00FF0F00) == 0x00010900` - id 1 with
behaviour 9. That zombie instead gets a colour flash on joints 0, 1, 2, 9 and 12
(`JointApplyColorTint(j, 48, 0x080820, 0x606060)`; the offsets are in the
Saturn's 0xA4-byte joints) and the routine jumps to its hit billboards
(`0x0604AFF0`), skipping the aim-height offset and Jill's handgun chip.

**The arenas draw no omodels.** The OBnn table lookup (`0x0605A348`) folds the
stage before indexing: stages 5 and 6 read rows 0 and 1, and **stage 7 reads row
5**, with the arena number as the room. Row 5 is all zero except slot 0 of the
rest rooms (1, 6, 11), which is OB31. So none of a fight arena's RDT omodels is
drawn - arena 15's four centre tubes included; the broken tanks are in the
background and masks.

**The results screens' art** (`RANKING.PRG 0x0602B1E4` loads `/BATTLE.BIN` at
`0x00200000`, `/WAPON3.BIN` at `0x00235000`, `/SAVE2.BIN` at `0x00237000`):

| file / offset | what | goes to |
|---|---|---|
| `BATTLE.BIN +0x0000` | X68 SSCONV: font A (16x16, 4 cells/glyph) at `+0x30`, 16 palette rows at `+0x1430`, font B (8x16, 2 cells) at `+0x1630` | VDP2 `0x25E00080`, CRAM `0x200`, VDP2 `0x25E02040` |
| `BATTLE.BIN +0x10000` | a 320x240 8bpp **Windows BMP** - the blue backdrop; `0x0602B428` converts its BGRA palette to CRAM `0x400` and copies the rows (bottom-up) into VDP2 VRAM B | NBG bitmap |
| `BATTLE.BIN +0x30000` | X68 SSCONV: the prompt window's gradient art | VDP1 `0x25C10000`, CRAM `0x100` |
| `BATTLE.BIN +0x34000` | X68 SSCONV: the life bar (256x16: 248x14 yellow + a black shadow 4 right / 2 down) at `+0x30`, its palette (grey-green, red, yellow, black) at `+0x830`, the loss fill (8x16: 8x14 red) at `+0xA30` | VDP1 `0x25C14000`, CRAM `0x160`, VDP1 `0x25C14800` |

**The life bar** (`0x0602C204`) is two scaled sprites with the lower-right zoom
point (`CTRL 0x0F01`, `PMOD 0xC0`, `COLR 0xB0`) at **constant** coordinates - it
does not move: the bar, character `0x2800`, corner (200, 64), drawn 264x16,
Z 35.0; the loss, character `0x2900`, corner (192, 64), drawn
`256 - 256.0 / max * hp` wide (skipped at 0), Z 34.0 - red growing leftward
from the yellow's right end. `max` is `0x8C - 44 * (character & 1)`, `hp` the
word at `0x002FFFA6`. VDP1 coordinates here are from the screen centre at twice
the text grid's scale (the bar's 264 units are 132 px, beside `LIFE`). The
`0x00230000` / `0x00220000` in that routine are those Z values, **not**
`BATTLE.BIN` offsets, which is what an earlier reading took them for.
| `WAPON3.BIN` | the seven item labels, 14 columns x 2 cells each, + 2 palette rows (normal, dim) | VDP2 `0x25E03000`, CRAM `0x300` |
| `SAVE2.BIN` | the save prompt art (YES/NO) | VDP1 |

Both fonts: A-Z, 0-9, space, `:`; font B adds `.` `-`, font A a `pts` glyph.
4bpp with the left pixel in the HIGH nibble. The text routines write one 64-cell
VDP2 map at `0x25E78000` (cell = 8x8 px on a 320x224 screen): `0x0602B880` font
A, `0x0602B910` font B, `0x0602B988` a two-digit count (tens only if non-zero),
`0x0602BA38` mm:ss from **frames**, `0x0602BC4C` a four-digit score with `pts`,
`0x0602BB68` one font-A glyph. Palette rows (CRAM `0x200 + 0x20 n`): 0 white,
1 grey, 2 yellow, 3 dim yellow, 4 green, 5 cyan, 6 orange. The character name
and the rank letter use palette `(frame counter >> 2) & 2` - white and yellow,
eight frames each.

Score screen cells: `SCORE` (15, 2); name (18, 5); labels (2, 8/10/12/14) and
(21, 8/10/12), palette 8 or 9 (dim: the grenade rows for Chris), each count at
label + 15; `LIFE` / `CLEAR TIME` / `RANK` at (7, 17/20/24) in cyan; time (28,
20); letter (19, 24); score (27, 24). Ranking: `RANKING` (13, 1), name (18, 3),
row i at y = 6 + 2i: ordinal (6), letter (13, palette 6 for S), score (17), name
(24), time (29); white/yellow/white/cyan/white when just viewing, grey/dim
yellow/grey/green/grey around a newly placed row.

> **CORRECTION:** the record's `+8` is a time in **frames**, not seconds - the
> default `0x34B84` = 215,940 frames = 59:59.

**The extra Tyrant capsule** — a new prop in the main lab (stage index 4,
room `0x13`, `ROOM513`) and arena 15, which reuses that room:

* model: `0x0603DBA4` (GAME4 `0x0603DBA4`) loads index **31** of the
  extra-model table (`0x0609B174`, 8-byte names: 28 `a_plt2`, 29 `a_kkm1`,
  30 `a_plt4`, 31 `a_trs1`, 32 `a_trs2`) through `0x0604EC74`. So
  **`ENEMY/A_TRS1.DAT` + `A_TRS1T.DAT` is the capsule** (mesh + 4 bpp texture).
  The other `A_*` pairs are the same kind of room prop (arena 8 loads index
  28, `a_plt2`, via `0x0603CB82`).
* collision: `0x06051B38` (GAME4 `0x0605CB38`) keeps the current entity out of
  a cylinder at x `0x1BD0`, z `0x7400`, radius `0x923` — pushed back to the
  rim along the angle from the centre. Re-read 2026-09-29 from the function
  entry:

  ```
  0605CB38  sts.l pr,@-r15              ; entry: saves r8/r9/r10
  0605CB58  mov.l @(0xB0,pc),r1  = 0x002F8660   ; stage byte
  0605CB5E  cmp/eq #4,r0                       ; stage 4 == STAGE_LABORATORY
  0605CB62  mov.l @(0xAC,pc),r1  = 0x002F8661   ; room byte
  0605CB68  cmp/eq #19,r0                       ; room 19 == 0x13
  0605CB6E  mov.w @(0x82,pc),r9  = 0xFFFF8C00   ; -0x7400
  0605CB72  mov.w @(0x80,pc),r8  = 0xFFFFE430   ; -0x1BD0
  0605CB74  mov.l @(60,r1),r5                  ; player z   (+0x3C)
  0605CB78  mov.l @(52,r1),r4                  ; player x   (+0x34)
  0605CB7A  add r9,r5 / 0605CB7E add r8,r4      ; z-0x7400, x-0x1BD0
  0605CB7C  jsr 0x0605A1E4                      ; squared distance
  0605CB80  mov.w @(0x74,pc),r1  = 0x923        ; radius
  0605CB82  cmp/gt r1,r0                        ; outside -> leave the player be
  0605CBA2  jsr 0x06085DCC / 0x06085DF0        ; angle, scaled by the radius
  0605CBCA  mov.l r1,@(52,r3)                   ; store player x
  ```

  So the capsule is a **collision cylinder at (x 0x1BD0, z 0x7400) r 0x923**,
  in this room only, in the port's own stage/room numbering, written to the
  entity's SCA matrix (+52/+60 = t[0]/t[2]) *after* the player's movement — which
  is where the room's per-frame code runs it on the Saturn. It is 0xB0 from the
  room-object special case's (0x1CB6, 0x738C), so the two agree on where the
  capsule is, and the cylinder alone answers all three of the port's open
  questions about it: the collision, the trigger and a spawn point.

#### Re-read of the capsule loader (2026-09-28)

The routine is the room's **extra-model loader**: one function that loads
whichever of the `A_*` props the current room wants, and it is at a
**different address in each module** — the addresses quoted above are GAME4's
`0x0603DBA4`, and the same code is at **`0x0603CBD4` in GAME5**. The §4g
preamble's "the same code is in every GAME module" is true of the *logic*, not
of the address; quoting one number for both modules is a trap, and the table
below gives the per-module addresses that were actually read.

The campaign condition is exactly:

```
stage byte 0x002F8660 == 4  &&  room byte 0x002F8661 == 0x13
```

which then loads name-table index **31** (`a_trs1`) through the
`0x0604EC74` / `0x0604ED40` pair, OR'd with the Battle Game test
(`long[0x060A67D0 + 0x0A50] & 0x80000000`) && `word[0x060A67A6] == 15`
(arena 15). **The stage byte is 0-indexed**, independently confirmed by the
Tick swap below — which is what makes it match the port's `g_stageId`.

The `A_*` name table is **base `0x0609B174`, stride 8**, and index 28 lands on
`0x0609B254` = `a_plt2` — the base is *not* where the first `A_*` name sits, so
dumping from `0x0609B174` reads the tail of the neighbouring 12-byte
`em10xx.emd` model-name table and looks like zeros. The two tables interleave in
the same address range; the loader only ever indexes 28-32.

**The Tick swap, verified instruction for instruction** at `GAME5 0x0604EA80`
(the `0x0604EA80` the `enemy_set` path calls with the raw type, before adding 4):

```
r8 = extu.b r5                     ; the model index
cmp/eq #2, byte[0x002F8660]        ; stage == 2
cmp/eq #10, r8
mov   #26, r8                      ; -> the Tick
r2 = 0x0609AD5C + r8 * 12          ; 12-byte entries
```

so index 10 -> 26 on stage byte 2, and the Jill block is the same table
+ `0x27C`. This is byte-for-byte what `src/game/saturn/SaturnModels.cpp`
implements, and the stage byte being 0-indexed is what puts the Tick in the
**underground** (stage 3 in 1-index) with the mansion Hunters left stock.

#### The `A_*.DAT` container — **decoded** (`tools/saturn_mbn.py`)

It is **not** a new mesh format: it is the *same SGL layout* as an `.EMD`'s
section 1 (see `saturn_emd.py`), stored **uncompressed**, with no PS1 section.
All big-endian, all FIXED values 16.16.

```
+0x00 u32  BASE  - file offset of the SGL mesh (always 8)
+0x04 u32  END   - where the mesh ends; the palettes follow
BASE        the SGL mesh; every pointer below is relative to BASE
  +0x00 u32  object count N        (+0x08 u32 -> root OBJECT)
  +0x04 u32  palette count         (+0x0C u32 -> TEXTURE table)
  +0x10 u32[N] -> XPDATA, one per joint
END    32 bytes per palette, 16 x RGB555 BE
```

XPDATA (24 B) = pntbl, nbPoint, pltbl, nbPolygon, attbl, vntbl; POINT and
VECTOR are 3 x FIXED; POLYGON is 20 B (normal 3 x FIXED + 4 x u16 index,
`v3 == v2` meaning a triangle); ATTR is 12 B; OBJECT is 48 B. All identical to
the EMD's, so `saturn_mbn.py` reuses those readings verbatim.

**The mesh sections tile exactly, which is what proves the layout.** For
`A_TRS1` (412 B) they land at BASE+`0x14` points, +`0x74` polygons, +`0x9C`
attrs, +`0xB4` vectors, +`0x114` xpdata, +`0x12C` root OBJECT (48 B), +`0x15C`
the texture table — and the OBJECT's 48 bytes end precisely where the texture
table begins. `END` is the file size minus one 32-byte palette.

**TEXTURE (8 B) = `u16 w, h, offset/8, (w/8)<<8 | h`**, terminated by
`w == 0xFFFF`. The fourth field is the size in **8x8 VDP1 cells** (high byte
`w/8`, low byte `h`) — which is why a 40x64 image is `0x0540` and a 16x27 one is
`0x021B`. The third field is a byte offset in 8-byte units. `A_PLT2`'s 14
entries ascend monotonically and land inside its texture file, which is the
check that pins the meaning.

**The pixels are in the sibling `A_*T.DAT`, not in the mesh file.** `A_TRS1.DAT`
is mesh + palette; the 4 bpp VDP1 character data lives in `A_TRS1T.DAT` and is
reached purely by each texture's `offset`. `A_TRS1`'s two 40x64 images are
1280 B each and `1280 + 1280 == 2560`, exactly `A_TRS1T.DAT`'s size;
`A_PLT2` needs 7868 of its 7872 (4 B of alignment). That size identity is what
confirms the offset field.

**The palette is the same convention as an EMD's**: 16 x RGB555 BE with bit 15
set on every entry, so bit 15 is not STP and the low 15 bits are the colour.
Checked against `EM1016`, whose palette is in the same ramp style and *is*
confirmed rendering in the engine.

**These are cheap background props, not solid meshes.** `A_TRS1` is 8 points
and 2 polygons — a flat 40x64 billboard, and the second quad is a near-copy of
the first (the OBJ shows the two vertex sets within ~1% of each other), i.e. a
cross-billboard with a small offset rather than a 3D capsule. `A_PLT2` is 57
points and 56 polygons, which is a set of leaf cards at varied angles, as the
decoded mesh shows.

#### The asset load logic (GAME4 addresses; GAME5 is a different compilation)

The name table at base `0x06096350` has **exactly two users** in GAME4
(`0x06059D24` and `0x06059EC0`, i.e. one per file kind), and they are the whole
of the load path. Read with the corrected `saturn_sh2.py`; note the eleven
overlays are separate compilations, so every address shifts per module.

1. **The room's extra-model routine** — `0x0603DBA4` in GAME4, `0x0603CBD4` in
   GAME5. Tests `stage byte == 4 && room byte == 0x13` (the campaign) OR'd with
   the Battle Game arena-15 test, and if either holds loads name-table index
   **31** (`a_trs1`). It also loads 28/29/30 (`a_plt2`/`a_kkm1`/`a_plt4`) for
   other rooms through the same two calls.

2. **`0x06059C74` builds the TEXTURE path.** It takes the index in `r4`,
   rejects anything outside 28-34 (`r4 -= 28; cmp/hi #6`), and then at
   `0x06059CC8`:

   ```
   r1 = 0x06096350          ; A_* name table base
   r5 = r5 * 8              ; 8-byte entries
   r5 = r1 + r5             ; -> "a_trs1"
   r4 = 0x0609A6C           ; shared path buffer
   r5 = 0x06059C6C          ; "t.dat"
   jsr 0x0603BD0C           ; strcat
   ```

   So the texture file is literally the mesh name with **`t` inserted before
   `.dat`** — which is why the pair is `A_TRS1.DAT` + `A_TRS1T.DAT`. This is
   the original's own reason for the `T` sibling convention, and it is what
   `tools/saturn_mbn.py` re-derives (it builds the texture path as
   `stem + "T" + ext`; the code builds it as `name + "t.dat"` — same file).
   `0x0603BD0C` is a plain strcat, used 6 times across the module.

3. **`0x06059D40` builds the MESH path and allocates the record.** Same buffer
   (`0x0609A6C`, in its literal pool at `0x06059EC0`), same strcat, with
   `".dat"` at `0x06059D38`. It then takes a 40-byte record from the registry
   array at **`0x0609FF80`**, stride **40**, counted by the word at
   `0x060A1850 + 0x0A74`, and writes the index into it.

4. **Residency**: `0x06059C74`'s other branch reads the words at
   `0x060A1850 + 0xAA8` and `+0xAB4` and skips the reload when the model is
   already up, so a prop re-entered repeatedly is loaded once.

The registry is 40 bytes per extra model, not the 0xA4 the PC uses for
`omodel` records — the Saturn's extra models are a separate concept from the
RDT-carried props, which is consistent with the rooms being byte-identical to
the PC's while the Saturn still adds one.

Record fields, from the append loop at `0x0603D8A8` (GAME4), which writes with
the record's base `0x0609FF80 + word[0x060A1850+0x0A74] * 40` and then bumps
that count:

| off | value |
|---|---|
| `+0x00` | u32, the name-table index (`a_trs1` = 31, ...) |
| `+0x0A` | u16, from `word[0x060A1850 + 0x0A72]` |
| `+0x10` | u32, from `long[0x060A1850 + 0x0AB0]` |
| `+0x1C` | u32, the index again |

#### Every `A_*` prop, and which room wants it

The whole set is decided in one routine (GAME4 `0x0603D840`-`0x0603DC60`), and
each block is the same shape: test the stage/room bytes, then call `0x06059C74`
+ `0x06059D40` with the index. **These are GAME4 addresses; GAME5's copy sits
~0x30 bytes earlier.**

| stage (0-idx) | room (0-idx) | prop | what |
|---|---|---|---|
| 2 | 3 | `a_trs2` | a **second capsule, in the Underground** |
| 3 | 12 (`0x0C`) | `a_plt2`, `a_plt4`, `a_kkm1` | extra plants + kitchen machine |
| 4 | 19 (`0x13`) | `a_trs1` | the main lab Tyrant room, `ROOM5130` |
| arena 8 | - | `a_plt2` / `a_plt4` / `a_kkm1` | Battle Game (`word[0x060A1826] == 8`) |
| arena 15 | - | `a_trs1` | Battle Game, reuses the lab room |

**`a_trs2` in stage 2 room 3 is new** — the earlier notes only had the lab
capsule. Stage 2 is the same stage whose Hunters become Ticks, so the Saturn
re-dresses the Underground in both ways, and the Saturn therefore has a
capsule to look at before the lab. Its trigger is the same two bytes as
`a_trs1`'s (`0x0603DC34`: `stage == 2 && room == 3`).

Two counter fields in the `0x060A1850` struct are reset on entry to this
routine and maintained here:

* `byte[0x060A1850 + 0x0AE7]` - zeroed at `0x0603DB2C`, then **incremented once
  per capsule prop loaded**, by the `add #1 / mov.b r1,@(r0,r2)` pair at
  `0x0603DC30` and `0x0603DC5A`. So it counts loaded capsules, and it is *not*
  placement data - an earlier reading of `0x0AE7` as the capsule's own record
  was wrong.
* `byte[0x060A1850 + 0x0AE6]` - set from `byte[0x060A22C5]` at `0x0603DB20`.

**Still missing: the transform.** The per-room prop list is an array of
descriptors at **`0x060A74B0`** (GAME4), indexed by the loop counter, each entry
being a *pointer*; the record-append loop reads only `descriptor + 20`, which
is the model index. `0x060A74B0` is **past the end of the module image** - it is
BSS, zeroed at load - so the descriptors are built at runtime, and the 40-byte
record's remaining 28 bytes (position/rotation) are not written by the append
loop either. Of the 17 references to `0x060A74B0` in GAME4, three are in this
routine; the rest are the writers, not yet identified.

The loop is **duplicated in the module**: an identical copy sits at
`0x0603D700`-`0x0603D768`, with the same `0x0609FF80` record array, the same
stride 40 and the same `descriptor + 20` read, and both use the same loop bound
`byte[long[0x002FAB10] + 3]`. Which of the two is live for a given room, and
what fills the descriptors, is unresolved.

The collision cylinder for the lab capsule (x `0x1BD0`, z `0x7400`, radius
`0x923`) is the only placement value recovered so far, and its scale is
unconfirmed. `0x002FAB10` itself is useless as a search key - 72 references in
GAME4 alone, so it is a hot shared global, not a per-room table.

#### Getting the capsule onto the PC — and a wrong assumption to avoid

`assets/USA/objspr/OSP*.PAK` (436 files) are **room camera MASKS, not object
models**, and are not the container to write. `g_maskPathTemplate`
(PC `0x004c3bc8`) is `<root>objspr\osp0SRRC.pak` with the stage digit at
`[0x11]`, the room's two decimal digits at `[0x12]`/`[0x13]` and the camera
index at `[0x14]`, and `load_room_masks` is what reads them (Room.cpp). They
are LZW-packed; the port's decoder is `unpack_pakfile_` (`0x00425ab0`).

The room's 3D props come from **inside the RDT instead**: `object_models` at
RDT+`0x50` is `omodel_slot_count` `{TMD*, TIM*}` pairs, made absolute at room
load by adding the RDT base (RoomInit.cpp `0x00477e7e`), and
`cmd_omodel_set` (`0x00461ac0`) hands `modelData[1]` to `ProcessTmdAsync` and
`modelData[0]` to `ProcessTmdTextures`. The 0xA4-byte omodel *records* are
carved out of the RDT's `vab_sound_file` region, `omodel_slot_count` of them,
and `render_room_objects` / `update_room_objects` walk exactly that many.

So the Saturn's "load a prop in code" has no PAK to imitate: the Saturn keeps
its own extra-model table and its own record, whereas the PC has no spare
concept. `ROOM5130`'s `omodel_slot_count` is **7** and the init SCD already uses
slots 0-6, so there is no unused slot to point at the capsule. Anything done
here has to add a slot (the item records that follow are re-carved every load,
so growing the omodel block is survivable) or run a Saturn-owned record
alongside the table.

What is still missing is the capsule's **placement**: the loader writes it into
a record at `0x060A67D0 + 0x0AE7`, a data table not yet located, so the room
position and rotation are unknown. The collision cylinder gives a good
candidate for the position — x `0x1BD0`, z `0x7400` — but its scale has to be
confirmed against the room before it can be used, and the rotation is not
recorded anywhere read so far.

Not found yet: the Tick's sound bank and the update function for type `0x17`.

**RESOLVED 2026-09-29 — Chris's second lab Tyrant is not spawned at all.** The
Saturn re-raises the *first* Tyrant's entity from inside its own update: state 3
(`0x06074C8C`) sends Chris's Tyrant to a new behaviour 2, a dissolving death
(`0x06077804`), which arms `+0x186` bit 4; the tail of `tyrant_update`
(`0x060747C0`) then runs `0x060788A8`, which waits for the player at the lab
terminal (rectangle `0x0609AA44`) and plays a release cutscene that
re-initialises the same entity at the second capsule. Full table in
`docs/SATURN_PORT.md` §4.3. The real enemy-type table is **`0x06096848`**;
`0x0609A980` (read earlier as a type table, putting `0x06078954` at "type
`0x2E`") is a run of small per-function Tyrant tables. The search below is kept
because both of its negatives are true — there is no spawn to find.

**Where the second Tyrant was looked for** (2026-09-28, so the next session does
not repeat it).

**First, ruled out: the room data.** Decoding the Saturn's own
`STAGE5/ROOM5130.RDT` with `saturn_room.py` and running `mine_room_scd.py
--init` over it gives **three** `enemy_set` records, of which exactly **one** is
type `0x0C` — the same three records the PC's `assets/USA/Stage5/ROOM5130.RDT`
has, at the same order. (The Saturn's RDT is 478,372 B against the PC's
546,948 B — the Saturn restructures the header — but the three `enemy_set`
records are identical.) So the second Tyrant is **not** in the room script, and
the earlier "all 348 campaign rooms match" claim holds for this room.

**Second, ruled out: an immediate-typed spawn.** All five `GAME` overlays were
disassembled in full and scanned on **real instruction boundaries** (raw byte
searching is useless here — `88 04` and `E? 0C` occur constantly inside
immediates and PC displacements; the first pass through it produced three
confident-looking false positives). ~1.15 M instructions across GAME1-5:

| module | instructions | `cmp/eq #4` | `cmp/eq #0x13` | `mov #12,rN` |
|---|---|---|---|---|
| GAME1 | 228,115 | 50 | 9 | 74 |
| GAME2 | 231,913 | 47 | 9 | 63 |
| GAME3 | 222,909 | 50 | 9 | 59 |
| GAME4 | 229,645 | 49 | 9 | 62 |
| GAME5 | 240,026 | 57 | 10 | 78 |

Every `mov #12,rN` within 0x60 bytes of a `cmp/eq #4` or `cmp/eq #0x13` is
either **dead** (immediately overwritten, e.g. GAME4 `0x06071850` `mov #12,r1`
followed at `0x06071852` by `mov #1,r1`) or unrelated logic (GAME1
`0x0606DFE4` writes 2 or 3 to a byte at `0x184`; both are UI/flag code). The
same function is duplicated in every overlay, which is why the surviving
candidates repeat at a fixed +30/+32 offset from module to module.

**So the second Tyrant's type is not an immediate, which means it comes from
data.** The most likely shape is the one the Saturn already uses for props: a
per-room array of descriptors built at runtime, exactly like the prop
descriptors at `0x060A74B0` (BSS, index at `+20`, count from
`byte[long[0x002FAB10] + 3]`). The thing to look for is an enemy-spawn
descriptor array beside that one, whose entries carry a type field and whose
count is bumped for stage 4 / room `0x13` — not a branch that loads `#12`.
A secondary possibility is that the spawn is keyed on the capsule count:
`byte[0x060A1850 + 0x0AE7]` is incremented once per `a_trs*` prop loaded, and
the lab room loads exactly one, so it is a candidate trigger.


**Assets the Battle Game (and the campaign extras) need from the disc:**
`EM1016`/`EM1116`, `EM1017`/`EM1117`, `A_TRS1`/`A_TRS1T` (and the other
`A_*` pairs where their rooms are ported), plus the 15 `STAGE8` RDTs. The
golden Tyrant needs no asset. The Saturn EMDs use the Saturn mesh format
(§5, §7 item 8), so they need a converter before the PC engine can draw them.

### 4i. The Tick's own Hunter body (GAME2.PRG)  — read 2026-10-04

Stage byte → module (`g_NextModule` = stage + 1): `0,1,5,6` → `GAME1`, **`2` →
`GAME2`**, `3` → `GAME3`, `4` → `GAME4`; `GAME5` is the Battle Game. The Hunter
update tables are `GAME1 0x060959A0`, `GAME2 0x06092F40`, `GAME5 0x0609B4F0`
(type 6); `GAME3`/`GAME4` have an `rts` stub there.

`GAME1` and `GAME5`'s Hunter code is the PC Hunter's, function for function
(plus Saturn-wide tweaks such as the player-health gate `62 43` = 98/67). All the
Hunter's data tables (SCA records, health roll, pounce rolls, intro tables) are
identical in `GAME2`. **`GAME2` differs in exactly these functions** (every other
Hunter function, the script-driven state-8 handlers included, compares
identical; compared by decompiling both sides with constants folded, not by
byte diff — a byte diff of two relocated modules is mostly literal pools):

| GAME2 | GAME5 / PC | what changes |
|---|---|---|
| `0x0606C824` state init | `0x0606CD10` / `0x00416020` | + animator slot (`FUN_060705C4(e+0x188)`); installs the arm parts' scale flags; GAME5 instead clears two dead globals |
| `0x0606D074` variant roll | `0x0606D4F4` / `0x004167F0` | **player** `+0x8A == 0x55` → state block `0x01010100`, skip the roll. Nothing in the campaign writes 0x55 |
| `0x0606D10C` variant 0 | `0x0606D564` / `0x00416840` | leap tail: turn tolerance `0x20` (not `0x100`), `Add_speedXZ(0)`, behaviour 5, `+0x189 = 0`; no leap target |
| `0x0606D484` variant 2 | `0x0606DA18` / `0x00416CA0` | rewritten (below) |
| `0x0606D998` behaviour 1 | `0x0606E058` / `0x00417290` | the walk is animation `0x80` from the Tick's own tables; keyframe animator instead of `Joint_move`; arm scale reset on the first frame |
| `0x0606DE98` behaviour 5 | `0x0606E51C` / `0x00417760` | the stab (`0x0606DF24`), then a slow turn toward the player; no pounce sub-table |
| `0x0606E78C` behaviour 7 | `0x0606ECB4` / `0x00417EE0` | **the hold is the Tick's own**: it carries the player on the arm and decapitates them (below). An earlier pass marked this function "same" because the jump-table decompile hid its body |
| `0x06070604` / `0x060705C4` / `0x060705A8` | — | the keyframe animator, slot alloc, slot reset (from the room init `0x0603D36C`) |

**Variant 2** (`0x0606D484`): probes 0x800 ahead of the facing (`angle + 0xA0`)
with the room-boundary point query (radius 2); rolls the pounce only while the
player is hurt — `0 < health < maxHealth / 3` — against the Hunter's *low* table
alone (`0x0609B8FC`), cancelled by distance > 4000, a set player flag or a
blocked probe; then, in this order (later writes win): close in (behaviour 2,
partner latched, < 6000) → **stab** (turn ≤ `0x20`, < 4000, latched; behaviour 5)
→ howl (player flag `0x80`, < 3000; behaviour 9) → with the flag clear: swipe
(line of sight, < 2000, turn ≤ `0x80`; behaviour 4) else dodge (the PC's test;
behaviour 6).

**The stab** (`0x0606DF24`, sub-state in `Entity+0x189`):

| sub | does |
|---|---|
| 0 | animation `0x81`, `Snd_em(2)`, arm scale 1.0, `angle += 0xA0` |
| 1 | play `0x81`; hold while frame is 1-7, then fall into 2 |
| 2 | scale `< 0x4000`: wall-probe the tip (joint 6 world); stuck → billboard 8/8 at y 700, `Snd_em(8)`, sub 3. Sphere test (r 800) of the tip against the player → hit: flag 1, player pose anim 7 frame 6, hunter behaviour 7 (hold), billboard, joint tint, `Snd_em(3)`, player faces the Tick. Scale ≥ `0x4000` → clamp, sub 3. Every pass: `scale += (0x4800 − scale) / 4` (doubles, truncated) |
| 3 | `scale −= 3276.8`; at ≤ `0x1000` → scale 1.0, animation `0x82`, sub 4 |
| 4 | play `0x82`; sub 5 when it ends |
| 5 | state block `0x01000100` (back to the AI layer, behaviour 1) |

**The stretching arm** is joints 4 → 5 → 6 (the left arm chain of the
Hunter-shaped skeleton). The Saturn draws parts with per-part scale (draw
function `0x060708B0`, stride `0xA4`): flags `+0x64` `0x10/0x20/0x40` scale the
MESH on X/Y/Z, `0x100` scales the part's own offset by its scale vector
(`+0x80/84/88`); the saved world matrix (`+0x44`, children, hit tests) is
unscaled. Init sets part 4 `|= 0x20`, part 5 `|= 0x120`, part 6 `|= 0x100`, all
three scale vectors to 1.0; the stab writes `scale << 4` to the three Y scales
every frame.

**The animator** (`0x06070604(table, slotPtr, id)`): `table` = 12-byte entries
at `0x0609B878`, index `id & 0xF`: `{u16 frames, u16 0, u32 rootKeys, u32
keyArrays[16]}`; keys are `{u16 time, s16 x, s16 y, s16 z}`, held (not
interpolated), the index advancing at most once per call when the *next*
record's time is reached, each array ending with time `0xFFFE`. Slots are four
`0x20`-byte records at `0x0609D5F4` (key-index bytes per joint, root index at
`+0x1E`, in-use byte at `+0x1F`). Four animations: `0x80` walk (68 frames),
`0x81` wind-up (14), `0x82` retract (14), `0x83` decapitation (49) — the table has **four** entries and ends at the
Hunter's SCA record (`0x0609B8A8`); the units are the PS1 section's (the
Tick's walk root is `(27, −1603, 5)`, the Hunter's chase `(27, −1547, 22)`).
The Tick EMD's PS1 section is byte-identical to the Hunter's, so every other
pose (swipe, dodge, death) plays from the EMD as for a Hunter.

**Rotation convention — the keys are not the PS1 data's.** GAME2 has two
part-draw routines, and the entity loop (`0x0602F2E4`) takes the Tick's
(`0x06070794` → `0x060708B0`) only for type 6 **with `animationId & 0x80`**, i.e.
only while a keyframe animation plays; every EMD animation goes through the
generic one (`0x0603866C` → `0x060381A8`). Both read part `+4/+6/+8` (Joint_move
`0x06039C68` and the animator store x/y/z there unchanged) and both call the same
SGL primitives — `0x06082C94` = Rx, `0x06082C1C` = Ry, `0x06086E6C` = Rz, each
post-multiplying by the standard matrix (3×4 rows of `0x10`, translation in the
fourth column; `slTranslate 0x06086EE4` proves the layout) — but in opposite
order:

| routine | order |
|---|---|
| generic `0x060381A8` (root `0x06038404`, joints `0x060385C0`) | translate, Rx(+4), Ry(+6), Rz(+8) |
| Tick `0x060708B0` (pool `0x06070BC8`) | translate, Rz(+8), Ry(+6), Rx(+4) |

The generic order is the PS1 GTE's, and it is exactly what the PC draws for the
Hunter's PS1 data — but the PC keeps its matrices **Y-flipped**: `RotMatrix`
stores `Rx(−x)·Ry(y)·Rz(−z) = D·Rx(x)Ry(y)Rz(z)·D` with `D = diag(1,−1,1)`, and
`CompMatrix`/`ApplyMatrixLV` negate Y on the way in and out, so what is drawn is
`D·stored·D`. A Tick key must therefore be **stored** as `D·Rz(z)Ry(y)Rx(x)·D`,
which in PC angles is the plain `Rx(a)Ry(b)Rz(c)` decomposition of `Rz·Ry·Rx`
(no sign changes) — `tick_key_to_pc_angles`.

Two wrong tries, both play-tested: storing the keys through `RotMatrix` (drawn
`Rx·Ry·Rz`) put the stab arm overhead; storing `Rz·Ry·Rx` itself (drawn
`D·Rz·Ry·Rx·D`, a mirror of every joint) made the Tick lean back with its legs
splayed and thrust the stab off to the side. Measured in **drawn** space (raw
offsets, effective matrices — what the verifier now does): the walk is the
Hunter's forward hunch (head ahead of the pelvis at hip height, the Hunter's
chase has it ahead and up), and the wind-up ends with the arm level (+5…+15°)
and 13–19° off the heading, which the `0xA0` (14°) the stab turns the body by
cancels. Forward is **+X** (`Add_speedXZ`, `RotMatrixY` = standard Ry; the
Hunter's drawn swipe puts the right hand, joint 9, ~1500 ahead). Measuring the
stored matrices as if they were drawn is what made the second try look right.

**The hold** (`0x0606E78C`, behaviour 7, `Entity+0x87` sub-state):

| sub | does |
|---|---|
| 0 | `0x0603A644(tip world, player local, 0x0609D5D4)` = `inverse(tip) · player` (only the rotation is kept); the block's translation is pinned to `(−1700, −800, −1100)` (`0xF95C 0xFCE0 0xFBB4`); player `Entity+3 \|= 0x80` (matrix externally controlled — `EntityComputeJointWorldMatrices` then skips `RotMatrix`); player look-at flags cleared; `+0xC4 = 8` |
| 1 | count `+0xC4` down |
| 2 | `scale −= 1146.88` a frame; every frame the player's local matrix = `tip · block` (`0x06055D08`), a blood billboard on joint 5 every 4th frame; at ≤ `0x1000`: scale 1.0, animation `0x83`, `+0xC4 = 8`, player `+3 &= 0x7F` |
| 3 | wait `+0xC4`, then play `0x83` (49 frames): billboard at the tip with y −600 on frame 1; on frame 10 the player's `action_state = 4` (their own grabbed machine, `player_anim_grabbed`) |
| 4 | state block `0x01010900` (state 1, ignore 1, behaviour 9, the howl) |

The Hunter's hold does `animationId + 1` from the previous behaviour's id; after
the Tick's stab that id is `0x81`, so on the PC it indexed a non-existent EMD
animation and the Tick froze at the decapitation.

**The player's side — `g_playerAnimFunctions[44]`.** The bite sets the player to
`animationId 7 / animFrameId 6`; the player dispatcher (window 2, `0x26 +
animFrameId`) sends that to entry **44**. GAME2's table setup (`0x06052FB8`; the
same 14 indices the PC's `set_player_animations_functions` fills) installs its own
function there, `0x0606FF78` (the PC's is `player_anim_dispatch_4c2ac8`, the
original's 5-state death that sets `health = -1` in its first state, i.e. an
instant kill). GAME2's has eight states, in the player's `Entity+0x87`:

| state | does |
|---|---|
| 0 | frame 0, `flags \|= 6`, `+0xBC = 0xB4`, message flag `0x40` off, `+0xC4 = 0` |
| 1 | the player's damage animation; frame 8: impact sound + tint on joints 0,1,2,9,12; blood on even frames < 9; billboard after `0x5F` |
| 2 | `attackAnim` 1↔2, restart |
| 3 | animation at half rate |
| 4 | **the decapitation** — set by the Tick's hold on frame 10 of `0x83`: head (joint 1) hidden (`flags &= 0xFE`), flag `0x80`, two blood billboards, `Snd_em(5)` |
| 5 | decapitation animation; blood every 4th frame 9–`0x2A`; tints on 3; sound on `0x2A`; **`health = -1` on frame `0x10`** |
| 6 | corpse throw: push velocity −900 along the facing |
| 7 | fade at `+0xBC == 0xA0`; `animationId = 4` (dead) at `0x20` |

(6 and 7 are the PC's states 2 and 3.) So the player gets hurt and carried first and
is killed only when the hold reaches the decapitation — and it applies to anything
that bites in that stage, as on the Saturn.

**Saturn → PC `Entity` fields** (the Saturn's tail layout is not the PC's):
`+0x16C → +0x170` path latch, `+0x170 → +0x174` partner, `+0x17C → +0x180` room
hit, `+0x17D → +0x182` pounce latch, `+0x17A → +0x17E` step word, `+0x182 →
+0x187` approach counter; `+0x183/+0x184/+0x185` are the same. `+0x186` (arm
scale), `+0x188` (slot) and `+0x189` (stab) have no free PC bytes (the PC
`Entity` is exactly `0x18C` and the Hunter uses to `+0x18A`), so the port keeps
them in a side table keyed by entity (`src/game/saturn/SaturnTick.cpp`).

**Sounds.** `Snd_em(n)` (`0x0605276C`) → `0x06065560(8, n, pan…)` looks up key
`0x0800 + n` in a 26 × 8-byte RAM table at `0x002E8430` ({key, song id, …}).
`LoadRoomSeBank` (`0x06051204`) clears it and loads `/SND/SE<stage+1><room>.CDP`,
whose **first segment (file `+0x800`, `0x120` bytes) is that table**, so the
slot → song map ships inside each bank. Song ids count from 768. In `SE308`:
slots 0-8 → ids 777 778 773 776 774 771 772 775 784 → pack songs
**9 10 5 8 6 3 4 7 16**. Other Hunter rooms hold the same samples under other
song numbers (`SE30B`: 9 10 5 8 6 3 4 7, slot 8 = 19); slot 8 renders identically
in both (2.09 s), so one bank serves every Tick room. (An earlier port took "song
N = slot N" and played the wrong samples: the footstep was a door-like thud.)

Not ported / open: the `hit_state` writer for the `0x55` hold; the walk's
footstep timing was not re-derived from the tables.

### 4h. The second Tyrant's room, and the room art the Saturn re-drew

**The second Tyrant is in the *same* room as the first: stage 4, room `0x13`**
(PC `ROOM5130`, Laboratory B4, `ROOM_MAIN_LAB`). It is not room `0x15`:

* Decoding `STAGE5/ROOM5150.RDT` and running `mine_room_scd.py --init` over it
  gives `door_set 00/01`, `state_byte_test 03 00 0C`, `enemy_set 23 00 FF …`
  (type `0x23` = `NPC_REBECCA`) and `scd_event_create 00 09 00/01` — **no
  Tyrant**, and the same script the PC's `ROOM5150.RDT` has. That room is where
  Chris meets Rebecca; its doors lead to rooms `0x0C` and `0x14` and are entered
  from `0x0C` and `0x14`.
* `ROOM5130`'s own script (`0c 40 70 01 02 00 00 00 0c 00 00 cc 29 00 00
  58 1b 01 00 00 00`, under SysFlags `0x01000000`) is the **first** Tyrant, and
  the two Wesker records (`0x2E`) are the cutscene actor. Three records, same as
  the PC.
* Across the whole PC campaign only two rooms script a Tyrant: stage 5 room
  `0x13` (type `0x0C`) and stage 3 room `0x03` (type `0x10`, the final Super
  Tyrant, in the Underground — the room the extra `A_TRS2` capsule is also in).

**The Saturn re-drew that room's backgrounds.** The Saturn `.BSS` is 320×240
**8 bpp** with a per-camera 256-entry RGB555 palette; the PC's background pak is
one 16 bpp TIM inside the game's own LZW (`docs/PSX_BSS_AND_PC_PAK.md`).
`tools/saturn_bss_to_pak.py --verify` decodes all 655 Saturn camera frames and
diffs each against the PC pak for the **same room and camera**, through
`unpack_pakfile_`'s format:

```
655 frames, 115 differ from the PC pak, 8 have no PC pak at all
   Saturn-only: STAGE1 room 05 cam 9 | STAGE3 room 03 cams 8, 9, 10
   Saturn-only: STAGE3 room 06 cam 2 | STAGE4 room 01 cams 6, 7 | STAGE4 room 06 cam 7
```

Three traps in that measurement, all of which produced wrong answers first:

* **Do not compare brightness.** The Saturn's palettes run about one 8-bit step
  dimmer, so an 8×8-block mean comparison calls *every* room "different"
  (STAGE5 ROOM13 cam0: 23.9 mean absolute difference) and a normalised one calls
  every room "identical" (0.3) — the block means are the same while individual
  pixels differ wildly. Full resolution is required, and a threshold (>16 on
  >5 % of pixels) rather than a distance.
* **`parse_tim` truncates the last six pixels.** A background TIM's image-block
  length counts only the pixel bytes, so the spec-style `len-12` slice yields
  76,794 pixels instead of 76,800.
* **Compare against the same room *and* camera.** Many rooms reuse another
  room's art within a stage, so "closest pak in the stage" alone produces
  confident but meaningless matches.

The outliers are the interesting ones, and the lab Tyrant room is the extreme:

```
DIFFER STAGE5 room 13: cam0 55.7 %  cam1 51.5 %  cam2 45.0 %  cam3 61.3 %
                   cam4 64.8 %  cam5 57.1 %  cam6 86.4 %  cam7 49.7 %
DIFFER STAGE5 room 15 cam 0: 70.2 %      (grated floor, a body on it - a
                                           different room, not the fight)
```

Rendering both (`saturn_room.py bss` against `pak_view.py`) shows the same shot:
the tall tank vats are **empty** on the Saturn and **translucent green** on the
PC, with the small floor tanks unlit on both. That is the Saturn working around
its own lack of translucent drawing, the same reason the rest of the release
looks flatter. So `ROOM5130` needs the Saturn's own art in Saturn mode
(`assets/SATURN/Stage5/RC5130..5137.pak`, written by the asset migrator); the
other 107 differing frames are other rooms' re-dresses and are not ported.

**The Saturn's room is not the PC's room, even though the script is nearly the
same.** Both RDTs are different builds (the Saturn's decodes to 478,372 B, the
PC's to 546,948 B, with the PC's header 8 bytes longer), so the files have to be
compared section by section on the header layout in `Types.h` rather than by the
script alone. For `STAGE5/ROOM5130.RDT` against `assets/USA/Stage5/ROOM5130.RDT`,
with the 8-byte shift accounted for, exactly one section differs in content:

| RDT section | Saturn | PC |
|---|---|---|
| `+0x60` `initialization_scd` | 572 B | 600 B |
| `+0x78` `item_icons` (40×30 8bpp per item) | 1,200 B | 2,400 B |

`cam_switch_zones` (114,820 B), `walk_zones`, `footstep_sound_zones`,
`scd_opcodes`, the player animation pair, `messages`, `effect_anim_index` and
`sound_attribute_table` are identical; `object_models` differs by 35 bytes, all
relocated pointers.

The 28 extra bytes are one `item_model_set` record the Saturn's script does not
have:

```
18 item_model_set  04 70 17 30 75 D0 07 D0 07 0B 0F 01 FF 0C 1A 00 00 92 78 80 0A CF 81 01 82
```

and the icon table agrees: 1,200 B is one item icon, 2,400 B two, and the
Saturn's icon is byte-identical to the PC's first. So the PC's lab room carries
**one pickable item the Saturn's does not** — the magazine clip. That is the
*only* room-data difference, which is why the second Tyrant, the corpse
dissolving and the terminal trigger are still not found: they are in the
modules.

**Resolved 2026-09-29** (see §4g's note and `docs/SATURN_PORT.md` §4.3): both
are in the Tyrant's own code — the dissolve `0x06077804` and the release
`0x060788A8`, whose trigger is the terminal rectangle `0x0609AA44`. The text
below describes the port's earlier stand-in, kept for history: The port substitutes the second
capsule's own position — GAME4 `0x06055AA8` parks the tank's water-surface
record there (`x 0x1CB6`, `z 0x738C`) — and the room's own action zone for the
terminal, since `0x738C` = 29,612 is outside the range the player and the room
action zones live in, so the two coordinate sets are not interchangeable. See
`docs/SATURN_PORT.md` §4.3-4.4.

**The room mask lives in the room file, and the PC's `objspr` pak is a *different*
copy of it.** The mask is the block at the camera's `tim_mask_pointer` (offset
+4 of the 0x2C camera record) inside the RDT — the field the arrange-room path
in `load_room_masks` already reads — and the Saturn disc has no `objspr/` at
all, so its masks can only be there. The two copies are **not the same art**:

```
STAGE5 ROOM5130, camera 0:  OSP04190.PAK decodes to 16,928 B
                            the RDT's own block is 16,928 B
                            1,841 bytes differ (1,837 texels of 16,384)
                            the CLUTs differ in 2 of 256 entries
```

Rendered side by side they are the same shapes in different colours: the pak
draws the tank vats and their highlights **pale green**, the room's own copy
draws them **grey**. That is the "the vats in the middle of the room are broken
on the Saturn" note — and it is a *mask* difference, not a background or a model
one. The background is separately re-drawn (§4h's `saturn_bss_to_pak.py` scan)
and the vats' model is identical to the PC's: omodel slots 0-3 all point at one
TMD/TIM pair, and its CLUT is the pale green `D3AC C368 BB46 B324 …` ramp with
index 0 transparent.

The embedded art is the *same* in the Saturn's and the PC's RDT (0 differing
bytes in all seven cameras), so both copies coexisted in the PC release: the
room ships with one version embedded and the engine draws the other from
`objspr/`.

**Full script and event audit of the imported room** (2026-09-29, after the
overlay was in and the only visible change was the magazine clip). Every part
of `ROOM5130.RDT` was disassembled on both sides and compared:

| what | Saturn | PC | difference |
|---|---|---|---|
| `+0x60` init SCD | 572 B | 600 B | one `item_model_set` (the clip) |
| `+0x64` frame SCD | 258 records | 258 records | **identical** |
| `+0x68` event scripts | 16 scripts, 897 instructions | 16 scripts, 895 | four hunks, all in **script 8** |
| `+0x48` `cam_switch_zones` | 114,820 B | 114,820 B | **identical** |
| `+0x5C` `footstep_sound_zones` | 24 B | 24 B | **identical** |
| `+0x6C`/`+0x70` player anim pair | 27,844 + 1,432 B | same | **identical** |
| `+0x74` `messages` | 820 B | 820 B | **identical** |
| `+0x88` `sound_attribute_table` | 192 B | 192 B | **identical** |
| `+0x8C` `vab_header_file` | 3,616 B | 3,616 B | **identical** |
| the 7 cameras' mask TIMs | 16,928…9,744 B | same | **0 differing bytes, all 7** |
| omodel slots 0-3 (one TMD + TIM) | — | — | **identical** (TMD 992 B, both TIMs 16 KB) |

The event difference is entirely in **script 8**, the lab's 492-line cutscene
(`evt_disasm.py`, offset-normalised diff), and it is small:

* three reorderings — `cut_lock_set [05]` / `event_create slot 0x7` / a
  `run_scd` block moved relative to each other, same commands;
* **one extra deferred-command dispatch**, `FC 06 04 05 0E 00` + `FD` at
  +006FC: a `bit_op`-class SCD command the Saturn runs at one extra point.

Scripts 0-7 and 9-15 are identical instruction for instruction. And the decisive
part: **neither side's events contain a single `enemy_set`** — the cutscene only
*moves* the two existing enemies with `enemy_pos_set` (3 times each) and spawns
five `effect_spawn`s (5 each). So the second Tyrant is not in the events, and
with the init script and the room data identical besides the clip, it is not in
the room file at all.

The vats are the same *data* too: omodel slots 0-3 all point at one TMD/TIM pair
and it is byte-identical, the mask art is byte-identical, and the only Saturn
difference is the background. **So the green vats cannot be fixed by data** —
the Saturn draws that same model and that same mask differently, which is module
code.

`effect_anim_data` is where the PC's extra 67,344 bytes live. The count and the
leading animation headers match (the first 1 KB is identical), so the effect
*set* is the same; the deeper payloads are not, and effects spawned by id in
this room now come from the Saturn's table.

**What the port does with all this**: it overlays the room — the Saturn's
`ROOM5130.RDT` / `ROOM5131.RDT` are each LZ77-decoded into the same name under
`SATURN/Stage5/` (the disc has one per character variant, as the PC does; they
differ, so one must not stand in for the other), which removes the magazine clip with no
engine change; it overlays the background (`SATURN/Stage5/RC5130..5137.pak`);
and in Saturn mode `load_room_masks` reads the mask from the room file instead of
`objspr/`, which is what turns the vats grey. OG and DC keep the pak.

## 5. Models (`.EMD`)

`ENEMY` holds 71 `.EMD` and 244 `.DAT`. Against the PC's `Enemy/`, only four
EMDs are new:

    EM1016 (262,504)   EM1017 (225,928)   EM1116 (262,504)   EM1117 (225,928)

(`EM1032`/`EM1033`, once counted here, are on the PC too — the DC/player
variants.) `EM11xx` is **Jill's** block of the per-character model table
(§4g: second block at `+0x27C`), so these are two creatures, each with a
per-character file: `EM1016` = the **Tick** (Hunter family), `EM1017` =
**Zombie Wesker** (zombie family). §4g has how the code selects them.

**The Saturn `.EMD` is decoded** (`tools/saturn_emd.py`: `info`, `obj`
export with a texture atlas, `png` software preview). The earlier reading here
("PS1 first `0x800` bytes, then a new mesh") was wrong — it compared at the
wrong offset. The real layout, verified on every model tested:

| part | content |
|---|---|
| `+0x00` | BE `u32` `0x10`, then three BE `u32` offsets S1, S2, S3 |
| `+0x10`..S1 | **the complete PS1 `.EMD`**, byte-identical to the PC file (the last texture section differs slightly on a few models). The Saturn does not draw it; for `EM1016` it is a copy of `EM1006`, for `EM1017` one of `EM1001` |
| S1..S2 | LZ77 (the `saturn_lz77.py` codec) → the **SGL mesh** |
| S2..S3 | LZ77 → **4 bpp texture pixels** (VDP1 character data, 8-byte aligned) |
| S3..end | raw **16-colour palettes**, 32 B each, RGB555 BE |

The decoded mesh is Sega SGL's own format: a header (object count, palette
count, root `OBJECT`, `TEXTURE` table, then one `XPDATA` per joint);
`XPDATA` = points (FIXED 16.16) / polygons (normal + 4 vertex indices,
`v3 == v2` for a triangle) / `ATTR` (texno, VDP1 `CMDPMOD`, palette index × 4,
H/V flip) / vertex normals; an `OBJECT` tree (48 B nodes: mesh, joint id,
offset, angle, scale, child, next) that is the skeleton; a `TEXTURE` table of
per-polygon sprites (`w, h, offset/8, size`, `0xFFFF`-terminated). **Saturn
units × 4 = PS1 units**: the joint offsets are the PS1 skeleton's / 4, and each
joint's mesh × 4 has the PS1 mesh's bounding box. Mesh *k* is joint `id`,
which is not the list order (Hunter: meshes 0-3 are joints 3, 2, 1, 0).

The Saturn draws every polygon as its own small texture (the Hunter: 385
textures for 481 polygons, ~195,000 texels) — unlike the PS1's one shared
256 × 256 page. Animations are not in the Saturn sections: the joint ids are
the PS1 skeleton's, so the PS1 animation data in section 0 drives them.

**Conversion to the PC format** (`saturn_emd.py pc`): section 0's animation
data (everything before its TMD) is kept, and the Saturn mesh becomes a PS1
TMD of `0x34000609` textured Gouraud triangles — the only packet the PC EMDs
use. Quads are split, the winding is **reversed** (the Saturn's is opposite:
cross × normal > 0 on all 481 Hunter polygons, < 0 on 788 of 792 PS1 ones),
coordinates × 4, and vertex normals are averaged from the polygon normals
(the `XPDATA` sixth word is not a normal table). The per-polygon textures go
into one 8 bpp TIM, 256 wide, with a single 256-colour CLUT (index 0 = the
transparent key the PC engine uses; Saturn palette *k* colour *c* → 1 + 16*k*
+ *c*). They need **3.6-4.4 pages** (TIM 256 × 912-1120), so each packet
carries its 256-line page in the pad halfword after `(u2, v2)`, and the engine
reads it when the bound TIM is taller than 256 lines
(`PSXObject_SetTexHeight`, `src/marni/Marni3DObject.cpp`); no original TIM
is, so the stock path is untouched. Converted models are 350-410 KB against
~190 KB stock, which is why `g_DataBuffer` got 1 MB of port headroom.

---|---|
| header | `00 00 00 10` + three section offsets, all **big-endian** (16 bytes) |
| then | the PS1 file's **first `0x800` bytes byte-for-byte** |
| then | a new tail — the first differing byte is always at 2048 |

**Verified on all 65 shared models, not a sample** — `tools/dump_emd.py
FILE --vs MATCHED_PS1_FILE` reproduces the comparison per pair. The shared
`0x800` is the texture page, unchanged. The tail is the Saturn's own
mesh, and it is *larger* than the PS1's (e.g. `EM1000` 235,472 vs 185,788 B),
which is the quad-geometry TMD the Saturn hardware wanted. So the Saturn models
are the same creatures with re-authored, lower-detail meshes and re-encoded
timed-driver data — the reason they look worse is the mesh, not the texture.

The 244 new `.DAT` files are per-creature data the PS1 kept inside the RDT or the
sound bank: `IP00`–`IP92` and `IV00`–`IV75`, `OB01`–`OB99`, plus
`A_KKM1`/`A_KKM1T`, `A_PLT2`/`A_PLT2T`, `A_PLT4`/`A_PLT4T`, `A_TRS1`/`A_TRS1T`,
`A_TRS2`/`A_TRS2T` (animation-ish names, each with a `T` partner) and `COMPIC`.

---

## 6. Audio, FMV, UI data

- `SND/*.CDP` — 251 files, signature `CD_PACK Ver.1.0`: packs for Sega's
  68000 sound driver. The music is the 53 `BGM*.CDP`, `M30B0`/`M30F0`,
  `BATTLE.CDP` (the Battle Game's) and `NAME.CDP` (the ranking screen's);
  `SE*`/`DOOR*`/`WEP*`/`CHR*`/`JIL*`/`REB*` are sound-effect banks. The music
  is **sequenced**, not streamed, and is decoded and rendered - see §6a. This
  replaces the PS1's `.HED`/`.HSB`/`.VB` VAB banks and the 5 `.XAS` voice
  files.
- `BATTLE.BIN`, `BIO2.BIN`, `WAPON3.BIN`, `SAVE2.BIN`, `KEY3.BIN`, `BAZ.BIN` —
  signature `X68 SSCONV v0.00`, an X68000 sound-tool container, dword at +0x10 =
  sub-stream count (2 or 4). `BATTLE.BIN` is 225,280 B split 4 ways; the stream
  formats and its role (announcer voice is only a hypothesis) are unverified.
- `TITLE.BIN` (524,288) and `END.BIN` (341,248) — signature `BM60`, Capcom's
  bitmap container. These are the Saturn title and ending art.
- `DATA/BU_DATA.PCK` (62,404) — raw 16-bit Saturn pixels from byte 0, no header.
- `0.BIN` (86,400), `FONT_000.BIN` (16,936), `FONT2.CEL` (551), `DATA/*.PRS`.
- FMV: one `MOVIE.CPK` (250,490,880 B; Japan 257,513,472) played by `MOVIE.PRG`,
  instead of 25 `.STR`. It is **28 Sega FILM 1.06 files end to end**, each
  starting on a 2048-byte sector: Cinepak 320x224 at 15 fps, PCM stereo (movie 0,
  the Capcom logo, 8-bit 44.1 kHz; the rest 16-bit 22.05 kHz). A FILM header's
  length plus the end of its furthest STAB sample is the file's length, and
  walking that chain from byte 0 ends exactly at the CPK's end on both discs.
  Order (identical on both discs, checked by frame difference):
  0 CAPCOM, 1 OU, 2-4 the prologue, 5 DM1, 6 DM2, 7 DM3, 8 DM4, 9 DM6, 10 DM7,
  11 DMB, 12 DMC, 13 DMD, 14 DME, 15 DM8, 16 DMF, 17 ED1, 18 ED2, 19 ED3, 20 EU4,
  21 EU5, 22 ED6, 23 ED7, 24 ED8, 25-27 the Chris / Jill / both staff rolls with
  the credits in the picture (the PC's `stfc_r`/`stfj_r`/`stfz_r`; there is no
  `staf_r`). The prologue is split where the PS1 cuts it for Jill: movie 2 is
  exactly 2430 frames (`SLUS_001.70` PlayFMV's cut start, `0x97e`) and movie 3
  (150 frames; Japan 2666 + 151) is the door / main hall beat, PC `PU.AVI`
  162-172 s. The Japanese prologue has its subtitles drawn into the video; there
  is no `JIMAKU*.RGB` or `STAFF*.STF` on either disc. Which movie `MOVIE.PRG`
  plays for Jill was not read from the code - the split is the evidence.
  Migrated by the Saturn tab's FMV option (`core/SaturnMovie.cpp`).

> **The extracted copy in `assets/SATURN_ISO_CONTENT` is incomplete.** 101 of its files
> are shorter than the ISO directory says - `SND/SDDRVS.PCK` is 4,096 B of
> 28,064, `ROOM1PCM.PCK` 159,744 of 26,595,328, the `CHR`/`JIL` banks, most
> `PCM*.PCK`, `DATA/FONT.*`, `MOVIE.CPK`. The music packs are whole. Extract a
> fresh copy (`python tools/saturn_iso.py extract DIR --filter SND`) before
> trusting any size; the migrator reads the disc image and is not affected.

---

## 6a. Music: the sound driver, its formats, and the renderer

**Status: decoded and rendered.** `tools/asset_migrator/src/core/SaturnSound.cpp`
 renders any song of any music pack to 44.1 kHz stereo; the Saturn tab of the
 migrator writes the Battle Game's four tracks into `SATURN/Sound`
(`core/SaturnMusic.cpp`); `re1am_selftest satsong` / `satkeys` and
`tools/saturn_snd.py` are the instruments. All 97 songs of the 57 music packs
render (`satsong <pack> all`); the notes some drop are keys outside every layer
of their voice, which the driver drops too.

### The driver

`/SND/SDDRVS.PCK` (28,064 B) is **Sega's standard sound driver, "Ver-2.10 -L",
dated Jul 2 1996**: a 68000 image loaded at sound-CPU address 0 (vector table,
`SSP = 0xA000`, reset `PC = 0x1000`). Ghidra: `/AudioResearch/SDDRVS.PCK.0`,
`68000:BE:32:default` (the older `SDDRVS.PCK` entry there is the truncated
4 KB copy, mis-imported as SH-2). The SH-2 side loads it through WRAM-L
`0x00280000` (`BU_LOAD.PRG`), and its DSP program with it: **`smpldsp.EXB`
at file `+0x6860`**, which the game copies to sound RAM `0xB000`
(`BU_LOAD` `0x0602D7DC`, 0x540 bytes from `0x00286860`).

Several function names already in that Ghidra program were guesses and are
wrong (`SetHostPcmVoiceSlots` at `0x1F0E` is the host PCM-stream voice setup,
`DecodeSequenceTrackData` packs host MIDI). What the renderer follows:

| Address | Role |
|---|---|
| `0x04D20` | per-tick main: the 32 gate timers (`0x052AC`) **before** the 8 song slots (`0x0506E`) |
| `0x0506E` | song tick: tempo track, then the step counter; emits every due event |
| `0x0579A` | event reader, opcode table `0x05938` for `0x80..0xFF` |
| `0x056A8` | queues a MIDI-style message (ring `0x8A00`), processed FIFO by `0x02EB0` |
| `0x02F1A` | message dispatch: 0 note-off `0x03CAC`, 1 note-on `0x03574`, 3 CC `0x02FAA` (table `0x02FB8`), 4 program `0x01B14`, 6 bend `0x03480`; 2/5/7 flag-only |
| `0x038B2` | per-slot key-on: the layer's register image, TL, pan, pitch (`0x03AF6`) |
| `0x040A4` / `0x04164` | the pitch tables: OCT<<4\|semitone by `key - base + 0x60`, and FNS by `semitone*128 + fraction/2` (+0x100 at `0x1EF`, `0x383`, `0x4D9`) |

### The pack

A music CD_PACK holds a tone bank (loaded for `0x25A55000`) and a sequence
bank (`0x25A7A000`); six BGM packs also carry the in-game **sound area map**
for `0x25A00500`. In three of those (BGM30, 33, 35) the tone bank does not fit
at `0x55000` and the map puts bank 4 lower (`0x3F840` for BGM33) - the address
does not matter for the sound, layer sample addresses are bank-relative.
`/SND/BIOMAP.BIN` is **not** the game's map (it puts bank 4 at `0x50840`); the
map read back live at `0x500` has `0405 5000` (bank 4 at `0x55000`) and
`3000 C000 .. 8040` (DSP work RAM). **Every song of all 57 music packs selects
bank 4 with CC32** - its own pack's tone bank.

**Tone bank:** `+0` mixer tables, `+2` velocity (VL) tables, `+4` PEG, `+6`
PLFO, `+8..` u16 voice offsets up to the mixer table. A voice is pitch-bend
range (low nibble; the high nibble is a play mode the driver rejects), porta,
layer count - 1, volume bias, then `0x20`-byte layers: key range, then the
SCSP slot register image copied as-is (`+2` SA/LPCTL/PCM8B, `+6` LSA, `+8` LEA,
`+A`/`+C` EG, `+E` STWINH/SDIR, `+F` TL, `+10` MDL, `+14` LFO, `+16` ISEL/IMXL,
`+18` DISDL/DIPAN), base note, fine tune, and VL/PEG/PLFO table indices.
**No music layer uses PEG, PLFO, FM (MDL) or an LFO depth** (861 layers).

**Sequence bank:** `+0` song count, BE32 offsets. **Song k is BGM channel k**
- BGM34B's song 2 is the PC's `Bgm_24` (43.5 s, loop from 33.2 s against
the PC's 43.8 s / 33.5 s build-up). Song header: PPQN (driver range
`0x18..0x3C0`), tempo-event count, event-stream offset, tempo loop offset;
tempo events (BE32 steps, BE32 µs per quarter, driver range 200,000..1,500,000)
from `+8`. One stream carries all 16 channels.

Events, as the **driver** reads them - VGMTrans's `SegSatSeq` (written for
driver 1.33) differs in three places, marked **(!)**:

- `00-7F` note: channel = low **5** bits, key, velocity, gate, step; status
  bit 6 = gate bit 8, bit 5 = step bit 8.
- `A0-BF` poly pressure / CC (num, value, step), `C0-CF` program, `D0-DF`
  pressure, `E0-EF` bend (one byte, the MSB): bit 7 of the **first** data byte
  is channel + 16, which the message processor rejects; the step is 8 bits
  **(!)** (VGMTrans reads the CC value's bit 7 as step bit 8).
- `81` call: u16 offset from the stream, then play that many events there
  (only events that reach `0x057E2` count: not `8x`).
- `82` loop point, with a step byte: set the first time, jumped to the second.
- `83` end: the song ends once its gates run out.
- `88-8B` gate += `0x200/0x800/0x1000/0x2000`; `8C-8F` step +=
  `0x100/0x200/0x800/0x1000`. **`87` is invalid (!)** - it and `80`, `84-86`,
  `90-9F`, `F0-FF` stop the song.
- **An event's step is the wait BEFORE it (!).** The reader adds the new
  event's step to the counter and emits it only once the counter is ≤ 0
  (`0x051F2`); the first event waits its own step (`0x04F8E`). The data
  agrees: pan changes land one tick before their note groups.

Timing: a 500 Hz tick. The increment is `0x7D000 / (µs/quarter / PPQN)`
(two `divu.w`, so the driver's own rounding) in 8.8 steps per tick, i.e. 2 ms.
Gate timers count down with the same increment, from the emitting tick's
remainder; since they run before the songs, a same-tick note-off always lands
before a retrigger.

### Key-on (`0x03574` → `0x038B2`)

- **Every** layer whose key range holds the note sounds, one slot each. Slot
  choice: an idle slot (lowest first), else the last releasing one, else the
  last held one of the song, else the oldest.
- The layer's image goes to the slot; reg 0 gets KYONB|KYONEX.
- **TL**, when the layer is heard at all (`+2` bit 5, IMXL or DISDL set):
  velocity through the VL table (three rate/point/level segments; 8 slope ops,
  bit 3 of the rate = the ×1.5 "boost"; clamp to 1..127), × (255 − TL + voice
  bias) >> 8 = the slot's base level, × channel level >> 6, + song volume − 128,
  clamped, inverted. A bias that takes 255 − TL out of range makes the driver
  drop the note. Channel level = volume × expression >> 7; both default to 127
  and the level to 127 until a CC (read live at `0x7000`). The game starts BGM
  with song volume **100** (`+0x48` = `0x64` of the live song slot at
  `0x9900`; GAME5 `0x0604E124` = a per-song table + 5).
- **Pitch**: `bend (1/256 semitone) + fine tune`, the high byte + key + 0x60 −
  base note through the OCT table, the low nibble and the fraction through the
  FNS table. Bend is scaled by the voice's range and applies at the next
  key-on (no pitch refresh without PEG/PLFO).
- **Pan**: DIPAN, unless the channel's CC10 set one (`0x03264`: 5-bit SCSP pan,
  bit 4 = side). Program change drops that override.
- **Effects**: slots 0..15's EFSDL/EFPAN are the DSP returns. The game issues
  MIXER_CHANGE(bank 0, mixer 0) (GAME5 `0x0604D148` → `0x0605D58C`); bank 0 is
  the player's `CHR`/`JIL`/`REB.CDP`, whose one mixer table is `BF AF 00 ..`:
  EFREG0 → left, EFREG1 → right, EFSDL 5. Read back live on slots 0/1.

CC table (`0x02FB8`): 1 LFO depths on ≥ 64 · 2, 11 expression · 7 volume ·
10 pan · 16 mixer n of the bank · 17, 70 EFPAN · 71 EFSDL · 32 bank (area map)
· 64 sustain · 80-83 3D parameters · 91 DSP program · 120, 123.

HLE boundary: PCM tone layers and the above music controls are rendered. SCSP
`SSCTL=1` noise is currently silent; FM-modulated layers fail rather than being
approximated. CC91, CC80-83 and CC120/123 are not implemented. The rendered
BATTLE and NAME songs use only PCM layers and CC7/10/32, so these omissions do
not affect the migrated tracks; other Saturn sound packs need separate checks.

### The DSP

`smpldsp.EXB`: name (0x20), header (`+0x20` = RBL: EFFECT_CHANGE `0x033A0`
needs `0x4000 << RBL + 0x40` bytes of work RAM, exactly the map's `0x8040`),
COEF ×64 at `+0x40`, MADRS ×32 at `+0xC0`, MPRO ×128×4 at `+0x100`. RBP =
`0xC000 / 0x2000` = 6. It reads MIXS[0] and writes EFREG0/1 - a stereo reverb
about 17 dB under the direct sound; it saturates in 0.04 % of samples, which
is the program's own clamp. (`0x05DDE` rewrites COEFs for the driver's 3D
sound; music does not use it.)

### How the renderer was checked

- **Against the running game.** With BGM34/34B resident in Yaba Sanshiro, the
  SCSP slot registers were read back (`memory_read 0x25B00000`) and compared
  with every key-on image the renderer produces for BGM34
  (`re1am_selftest satkeys`): slots 0-5 match in **all twelve** register
  words - SA, loop, EG, TL, pitch, LFO, sends, pan and the `BF`/`AF` effect
  returns; slots 8-31 match in every word including pitch, and differ only in
  TL, all at the same `0x9C` (the whole song faded, which rewrites every
  slot's TL); one slot differs only in the per-slot mixer byte (a different
  slot number). That pins the layer mapping, both pitch tables, the TL
  formula and song volume 100.
- **Timing** against a static walk of each song (`saturn_snd.py`): BATTLE
  song 0 loops over 25.27 s, NAME's 10.67 s intro and 10.67 s loop, all 97
  songs end or loop.
- The SCSP (slots, linear interpolation, envelope, LFO, DSP, mixer) is MAME's
  `scsp.cpp` / `scspdsp.cpp` (BSD-3-Clause) - reference behaviour, not
  cycle-exact hardware. **Not checked by ear against a real Saturn**; that
  is the user's test.

### The Battle Game's music

GAME5's BGM loader (`0x0604C9F0`) takes STAGE8's group from a per-arena byte
table at `0x0609ACCC` and the room's channel bits from `0x0609ACDC`:

| arena | 1 | 2-4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12-14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| group | 0F | 40 | 06 | 0F | 40 | 27 | 40 | 23 | 0F | 40 | 34 |
| state | 08 | 08 | 08 | 08 | 08 | 08 | 08 | 08 | 08 | 08 | 20 |

Group `0x40` loads `/SND/BATTLE.CDP` (`0x0604CA7E`); in STAGE8 group `0x34`
loads `BGM34B.CDP` and its channel 2 (state `0x20`) is the Tyrant battle. At
the end of the run (`0x06088BDE`) the Tyrant theme stops, `BATTLE.CDP` is
loaded again and **song 1** - a 14.4 s one-shot fanfare - starts. BATTLE song 0
  is the 25.3 s arena loop. `RANKING.PRG` loads `NAME.CDP` (one looping song,
  the ranking / name entry). The migrator writes them as `Bgm_58` (battle mode bgm),
  `Bgm_59` (battle mode finished), `Bgm_24` (Tyrant Battle) and `Bgm_5a` (Battle game ranking bgm); the port's tables and
  names are checked by `tools/verify_saturn_battle.py`.

---

## 6b. The Japanese release ("Bio Hazard") — read 2026-10-08

`assets/Bio Hazard (J)(Saturn)/Bio Hazard (J) T01 Mode1 2048.iso`: a **cooked
2048-byte** track 1 (volume `BIO_HAZARD`); `saturn_iso.py --jpn` reads it (the
sector layout is detected from where the PVD's `CD001` sits) and
`--jpn extract assets/SATURN_JPN_ISO_CONTENT` unpacks it. Ghidra:
`/SATURN_JPN_PRG/RANKING_JPN.PRG` (SH-2 BE, base `0x0602B000`).

**Files.** The two discs use the same names. The JPN disc lacks `BIO2.BIN`,
`KEY3.BIN`, `SAVE2.BIN`, `WAPON3.BIN`, `DATA/FONT.DAT`, `DATA/FONT.TIM` and five
`DATA/FILEM_*.PIX`, and adds `/BU_DATA.PCK`. 980 shared files are identical;
333 differ, of which 267 are room `.RDT`s (the text). `ENEMY/*` (every EMD,
`OB80.DAT`), `BATTLE.BIN`, `STAGE5/ROOM513.BSS`, the arenas and the `SND/` packs
the migrator reads (bar `TITLE.PCK`) are byte-identical.

**Text encoding = the PC Biohazard.exe one.** The JPN rooms' messages decode
cleanly through `tools/jpn_font_table.py` (`data\FONT.TIM` glyph map) — the
wardrobe, the opening cutscene room, everything sampled. So a JPN Saturn room is
correct only against the JPN tree; the migrator writes this disc to
**`SATURN_JPN`**, which the game reads instead of `SATURN` when
`[Assets] Version=JPN` (`src/system/AssetPath.cpp` `ComposeModeRoot`).

**GAME5 texts.** The wardrobe questions sit at `0x0609EAB7` / `0x0609EAE2` /
`0x0609EB0D` (pointers `0x0609EC80..88`, USA `0x0609E8BC..` / `0x0609EAC0..`),
same order and meaning: とてもカッコイイコスチュームがある / みたことのある… (the
"back to the default" one) / サイズのピッタリなものがある, each followed by
コスチュームチェンジしますか? and the tail `02 08 02 0A 04 01 00` (one line
break more than the USA's `08 02 0A 04 01 00`). The migrator tells the discs
apart by these bytes (`saturnRegionFromGame5`) and the wardrobe patch writes the
matching set (`core/SaturnWardrobe.cpp`, reference `saturn_wardrobe_rdt.py`).
The **Battle Game opening message** (`0x0609EB37`, pointer `0x0609EC8C`) is
still the **English** sentence, re-encoded for the JPN font (`.` = `F8 1C`, one
pad space fewer); the port's `global_messages_jpn[63]` (generated by
`gen_jpn_text.py` `PORT_EXTRAS`) spells the USA `s_gm63` text through `STR_JP`.

**Difficulty.** All five JPN `GAME*.PRG` hold the PC's **first**-playthrough
weapon table (0 of 200 differ; 71 differ from the western one), so the JPN
Saturn has Japanese rules: `western_difficulty()` excludes Saturn mode on the
JPN tree. `tools/verify_western_difficulty.py` checks it.

**`RANKING.PRG`.** The two modules differ in a few hundred bytes:

| | USA | JPN |
|---|---|---|
| asset load | `0x0602B1E4` | `0x0602B1CC` |
| files | `BATTLE.BIN`, `WAPON3.BIN` → `0x00235000`, `SAVE2.BIN` → `0x00237000` | `BATTLE.BIN` only |
| VDP1 `0x25C12000` | `SAVE2.BIN` (English prompts) | `BATTLE.BIN +0x31EB0` (JPN prompts) |
| VDP2 `0x25E03000` | `WAPON3.BIN` over `BATTLE.BIN +0x35000` | `BATTLE.BIN +0x35000` (JPN labels) |
| logo draw | `0x0602D33C`, **no callers** | `0x0602D2E4`, called from 7 states |
| state machine | `0x0602D3F0` | `0x0602D398` |
| main | `0x0602E530` | `0x0602E3CC` |

So `BATTLE.BIN` was authored for Japan: `+0x35000` is an SSCONV block with
`WAPON3.BIN`'s exact layout (cells `+0x28`, palette `+0x18A8`) holding
ベレッタの弾 … グレネード火炎弾, and `+0x31EB0` is a 128×64 4bpp bitmap — four
128×16 rows セーブしますか? / 続けて遊びますか? / YES / NO (VDP1 characters
`0x2400` / `0x2480` / `0x2500` / `0x2580`, palette `+0x32EB0`, a grey ramp). The
USA build loaded English files over both. `+0x30030` (7,296 B) is gradient art
(the prompt window, VDP1 `0x25C10000`) and `+0x34030` the yellow LIFE bar;
`+0x20000` is still the backdrop BMP (77,878 B from `+0x10000`), and the
`0x00220000` in the bar code is a Z, not a pointer into it.

**The "BIO HAZARD" logo.** An SGL object tree inside the module, present in
**both** (JPN root `0x0603C18C`, XPDATA `0x0603BF54`; USA `0x0603C2EC` /
`0x0603C0B4`): nine parts, one per letter, 396 points, 406 polygons (378 quads,
28 triangles), extruded 2 units deep, 42.4 × 10.3 units. Records are 0x2C bytes
`{XPDATA*, pos[3], ang[3]+pad, scl[3], child*, sibling*}`, all identity; every
`ATTR` is `00 2B 0000 00E0 0010 0000 0004` (single plane, centre sort + light +
palette, 256-colour bank, colour 16, untextured). `ranking_draw_logo`
(`0x0602D2E4`): `slPerspective(0x2000)`, push, translate `(0, 0, 64.0)`
(`0x06042610`), `slRotX/Y/Z` (`0x06042608..C`), `sgl_draw_object_tree`
(`0x0602D230`, `slPutPolygonX(part, light 0x06042620)`), pop, then
**rot Y −= 91, rot Z += 182** every frame. State 0 writes CRAM `0x25F00020`
(colours 16-31) = four black entries and a red ramp R = 0..11 of 31, and sets
the light to `0x93CD` ×3 (normalised (1,1,1)). The JPN module calls the draw in
states 1-11 (score, name entry, ranking, both prompts). How SGL turns light into
a palette offset is not read; the port uses `clamp(−(n·l)·31, 0, 15)`, calibrated
on a capture of the real screen. `tools/saturn_ranking_logo.py png` renders it;
`src/game/saturn/SaturnBattle.cpp` (`sat_logo_draw`, a port-only type-13
sprite-queue callback) draws it from the migrated `Data/RANKING.PRG`.

---

## 7. Open questions

The boot module, the module roles and the load addresses are all answered
(§3a-§3c). What is left needs the modules analysed at their real addresses.

1. **The module entry points.** Each `.PRG` starts with the same 0x22-byte
   prologue at `0x0602B000`. The old reading of it as a decompression stub came
   from the byte-shifted dump (§3d, §3h). The modules load verbatim, so what
   `0.BIN` jumps to is now a plain static question.
2. ~~**The Saturn RDT format.**~~ **Answered (§4.0):** the PS1 RDT,
   LZ77-packed. `.BSS` slot *k* = camera *k*; the only two cameras without a
   picture (106 cam 7, 506 cam 5) are unused on the PS1 and the Saturn.
3. **The Battle Game's code.** `BU_CK.PRG`, `BU_LOAD.PRG`, `RANKING.PRG`,
   `BATTLE.BIN`, `BATTLE.CDP`, `DATA/BU_DATA.PCK` and `STAGE8` are all clearly
   Battle Game. Entry, unlock, arena sequencing, the 5-byte virtual-door
   records, the spawn placement, the state machine, the score formula, the
   ranking ladder and mode 4 are all known (§4d, §4e, §4f). `0x060FFC1B` is a
   display-control byte, not a reward. The score formula is fully static-read.
   `BU_CK.PRG` is settled as a decompressor for `BU_DATA.PCK`, not scoring.
4. ~~**Which model is the Tick, Zombie Wesker and the Golden Tyrant.**~~
   **Answered (§4g):** Tick = `EM1016` swapped in for the Hunter on stage 3;
   Wesker = type `0x17` (`EM1017`) forced in arena `80C0`; Golden Tyrant =
   the normal Tyrant with a +4/+3/−5 palette tint in arena `80F0`; the extra
   lab capsule = `A_TRS1`. Chris's second Tyrant: **answered 2026-09-29** — the
   first one's entity re-raised by the Tyrant's own code (§4g's note,
   `docs/SATURN_PORT.md` §4.3), in the same room (§4h).
5. ~~**The Battle Game's backgrounds.**~~ **Answered (§4.0):** all 15 arenas
   use a campaign room's background, lighting and sound banks, looked up in
   code by `LoadRoomBss` and three siblings.
6. **The 244 `ENEMY/*.DAT`** — what each `IP`/`IV`/`OB`/`A_*` group is.
   (`COMPIC.DAT` is known: a "coming soon" promo screen.)
7. ~~**The Saturn `.RDT` file-size drop.**~~ **Answered (§4.0):** it is the
   compression; decoded sizes equal the PS1's.
8. ~~**The Saturn mesh format.**~~ **Answered (§5):** SGL `XPDATA` per joint +
   per-polygon 4 bpp textures + 16-colour palettes, LZ77-packed after a full
   PS1 `.EMD`. `tools/saturn_emd.py` decodes and renders it. Open: the
   conversion to the PC's TMD/TIM (one 256 × 256 page, 8-bit UVs).
 9. ~~**Audio.**~~ **Answered for the music (§6a):** the packs are Sega's
    sound driver Ver-2.10 tone + sequence banks; the sequencer, the key-on and
    the tables are read out of `SDDRVS.PCK`, the renderer matches the live
    SCSP registers, and the Battle Game's tracks are migrated. Still open: the
    SE banks' use in game (they are the same format, played per event, not
    sequenced music), the `.PCK` room PCM, and the `X68 SSCONV` `.BIN` banks.
10. **Stale dumps.** `0.ram` and `TITLE.ram` were made with the buggy parser;
    re-dump them, or import the disc files directly.
11. ~~**Ghidra's SH-2 SLEIGH mis-renders code.**~~ **Withdrawn.** The claim
    came from the first `tools/saturn_sh2.py`, which was itself wrong (§4e's
    warning). Re-checked against the SH-2 manual, Ghidra's rendering of the
    shared prologue (`2FE6 4F22` = `mov.l r14,@-r15 ; sts.l pr,@-r15`) and of
    the §4c LZ77 stub is correct. Ghidra and the rewritten `saturn_sh2.py` agree;
    use either.

### 7a. The emulator is a usable instrument

Yaba Sanshiro exposes an MCP debug server on `127.0.0.1:9640` with
`debug_status`, `pause`/`resume`, `registers_get`/`_set`, `disassemble`,
`breakpoint_add`/`_remove`/`_list` (code and memory, with register conditions),
`step`/`step_over`/`step_out`, `backtrace`, `exec_history`, and
`memory_read`/`memory_write`. It is not registered in `.mcp.json`; it answers
streamable-HTTP JSON-RPC on `POST /mcp`. `memory_read` takes up to at least
64 KB per call, so the whole SH-2 RAM window is dumpable, which is how the load
addresses above were obtained. `step`/`breakpoint`/`exec_history` additionally
need `set_debug_option {"cpu_core":"interpreter"}` first.

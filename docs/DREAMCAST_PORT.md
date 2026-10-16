# Dreamcast port (KallistiOS)

The shared game code runs unchanged on the Dreamcast; everything specific to
the console lives in a platform layer (`src/platform/dreamcast/`) and a
PowerVR2 renderer backend (`src/marni/MarniDX_PVR.cpp`). The game data is the
user's own PC copy, put on the disc next to the executable.

Toolchain: KallistiOS 2.3, GCC 15 (`kos-chain` stable).

## Building

    source /opt/toolchains/dc/kos/environ.sh
    make -f Makefile.dreamcast                              # build/dc/residentevil.elf
    make -f Makefile.dreamcast gdi DATA=/srv/re1            # GD-ROM image, build/dc/gdi/
    sh tools/dreamcast/make_cdi.sh build/dc/residentevil.elf /srv/re1 re1.cdi
    make -f Makefile.dreamcast RE1_DC_ROOT=/pc/srv/re1 run DC_IP=192.168.1.50   # dcload

`DATA` is a folder holding `USA/` (and/or `JPN/`, `DC/`) and `config.ini`; its
contents become the disc root, so the game finds `/cd/USA/...`. A sample
`config.ini` is in `tools/dreamcast/`.

The shared translation units are read from the `RE1_SOURCES` block of
`CMakeLists.txt`, so the Dreamcast and Linux source lists never drift apart.
`make -f Makefile.dreamcast list-sources` prints them, `bss-report` lists the
largest static symbols.

### Build switches

| Variable | Default | Effect |
|---|---|---|
| `RE1_DC_ROOT` | `/cd` | data root; `/pc/<dir>` for dcload |
| `RE1_DC_VERBOSE` | `0` | `1` = serial log and periodic reports (frame time, memory, VRAM, textures, disc cache, task stacks) |
| `RE1_DC_RENDERER` | `pvr` | `null` = headless backend (`MarniDX_Null.cpp`) |
| `RE1_DC_FILECACHE` | `1` | `0` = no file cache in sound RAM |
| `RE1_DC_SILENT` | `0` | `1` = never start the AICA stream; every sound reports finished |
| `RE1_PVR_DEBUG` | `0` | bit 0: no depth test, bit 1: untextured magenta 3D |
| `RE1_DC_TRACE` | `0` | one-letter serial trace of risky operations (needs `VERBOSE=1`) |
| `RE1_DC_FTRACE` | `0` | game code built with `-finstrument-functions`; L+R toggles a function-entry log |
| `RE1_DC_PROF` | `0` | sampling profiler, see *Diagnostics* |
| `RE1_DC_UACOUNT` | `0` | game code built with `-fsanitize=alignment`; unaligned accesses counted per source line |
| `RE1_DC_POISON` | `0` | fill the free heap with garbage at boot (hardware-like RAM in an emulator) |

`RE1_DC_SMOOTH` (in `MarniDX_PVR.cpp`, default `1`) switches bilinear
filtering of backgrounds, movies and room masks.

## Source layout

| File | Role |
|---|---|
| `src/platform/dreamcast/main.cpp` | entry point, tick pacing, periodic reports |
| `src/platform/dreamcast/platform.cpp` | `platform.h` on KOS: paths, files, time, task-stack bounds |
| `src/platform/dreamcast/ctxswitch.{S,h}` | SH-4 context switch for the game's task scheduler |
| `src/platform/dreamcast/input.cpp` | maple controller → the XInput-layout pad mask |
| `src/platform/dreamcast/audio.cpp` | DirectSound contract: software mixer → AICA stream |
| `src/platform/dreamcast/video.cpp` | FMV: AVI demuxer, Cinepak decoder, streamed audio |
| `src/platform/dreamcast/dc_iso.cpp` | direct ISO9660/Joliet reader, file cache in sound RAM |
| `src/platform/dreamcast/vmu_save.cpp` | save games on the VMU |
| `src/platform/dreamcast/config.cpp` | settings-save hook, single-instance check |
| `src/platform/dreamcast/crash.cpp` | on-screen crash/stall report, unaligned-access emulation |
| `src/platform/dreamcast/stubs.cpp` | Windows-only entry points the shared code references |
| `src/platform/dreamcast/dc_compat.h` | force-included into every TU (newlib/libstdc++ gaps) |
| `src/platform/dreamcast/dc_platform.h` | Dreamcast-only helpers and build-switch defaults |
| `src/marni/MarniDX_PVR.cpp` | PowerVR2 MarniDX backend |
| `src/marni/MarniDX_Null.cpp` | headless MarniDX (bring-up, `RE1_DC_RENDERER=null`) |
| `tools/dreamcast/` | disc images, asset conversion, log resolvers |

Shared code tests `__DREAMCAST__` (passed by KOS 2.2+).

## Main loop and timing

`main.cpp` reads `config.ini`, brings up Marni and calls `main_loop()` once
per game tick. Ticks follow a deadline series (`next = previous deadline +
interval`, 33.3 ms, or 16.7 ms when the game turns frame skip off), so small
lateness is absorbed and the average rate is exact. A tick more than 8 ms late
restarts the series from "now" instead of replaying missed ticks, so the game
does not speed up after a long camera cut or load. Long waits use
`thd_sleep()` with a 12 ms margin, the rest is spun out with `thd_pass()`
(`thd_sleep` wakes late on the console).

Button combos: A+B+X+Y+Start exits to the loader; L+R+Y+Start draws the
crash/stall report once (to check it is readable on the TV).

## Task scheduler

The game's cooperative tasks run on their own stacks. KOS newlib has no
`ucontext`, so `ctxswitch.S` swaps the SH-4 callee-saved registers, PR and
FPSCR. KOS asserts on every reschedule that the running thread's `sp` lies
within `kthread_t::stack`, so each switch is bracketed by
`re1_ctx_begin_switch` / `re1_ctx_end_switch` (`platform.cpp`), which disable
IRQs and point the thread's stack bounds at the task stack being entered.

Task stacks are 3 × 64 KB (measured peak use ~16 KB). With no MMU guard
pages, each stack has canary words checked every tick; an overwrite stops the
game with the slot number.

## Input

`input.cpp` polls the first maple controller and publishes the same 32-bit
mask as the Linux backend (d-pad/analog in bits 0–3, XInput button slots in
bits 8–19), so the default pad bindings apply unchanged:

| DC pad | Slot | Game |
|---|---|---|
| A | A | action / confirm |
| B | B | cancel / run |
| X | X | aim |
| Y | Y | inventory |
| L trigger | LT | run |
| R trigger | RT | aim |
| Start | Back | options screen |

## Files and the disc

KOS's iso9660 driver looks every name up by rereading the directory through a
16-block cache; the game's `Sound/` and `Voice/` directories are larger than
that, so each lookup costs a dozen disc commands. `dc_iso.cpp` instead reads
each directory once into a name → (LBA, size) index and reads file data in
whole sectors by DMA, 64 KB at a time. Whole-file loads (`LoadFile`,
`plat_file_read_all`) and the movie player use it; other `fopen` calls go
through KOS VFS (`/cd`, `/pc`, `/ram`, `/vmu`).

**File cache.** Copies of recently read files are kept in the AICA's 2 MB
sound RAM that the audio stream does not use (~1.7 MB; 64 KB is left for the
sound side). Main RAM ↔ sound RAM is one G2 DMA per file. A room revisit or
a repeated voice line is then served without touching the disc. Files still
in use are not cached; the oldest entries are dropped first.

**Asset overlay.** With `mode=DC` in `config.ini`, files are looked up under
`DC/` first and fall back to the base tree (`USA/`), logged as
`[assets] DC has no ... - falling back`.

## Saves

The game writes `savedat<N>.dat` with plain `fopen`/`fwrite`. A VMU stores
files in 512-byte blocks, so a 2690-byte save would read back larger than the
game's buffer. The save root is therefore the RAM disk (`/ram/`), and
`vmu_save.cpp` mirrors it:

- at boot, every `SAVEDAT*.DAT` on the first VMU is unpacked into `/ram/`
  (the exact length is stored in a small prefix inside the payload); slots
  the VMU lacks are imported from the disc's `SAVE/` folder, if present;
- after every successful write into the save root, the file is written to
  the VMU with a VMS header (name and icon), so the BIOS memory manager lists
  it as *RESIDENT EVIL*.

Settings (`config.ini`) are read from the disc and not saved back on a disc
build (`config.cpp`); under dcload they are written to the host.

## Audio

`audio.cpp` implements the same DirectSound contract as the SDL backend (bank
indices 1..80, `PlaySound` slot ≠ 0 loops, volumes in millibels, pan
−10000..10000, `GetStatus`).

- **Mixer.** Banks stay in main RAM as decoded PCM and are mixed in software
  into one stereo KOS stream at 22050 Hz (44100 Hz for the PS1 audio tree).
  Per-sample work is fixed point (16.16 position, Q15 gains); PS1 banks are
  linearly interpolated.
- **Output.** The stream callback writes the mixed buffer into sound RAM
  itself, as FIFO-paced 32-bit G2 writes in bursts of 8, each after
  `g2_fifo_wait()` and under `g2_lock()`. KOS's default path copies with the
  store queues, whose lock the renderer holds for most of every frame.
- **Polling.** KOS streams only progress when `snd_stream_poll()` runs: a
  thread at priority 5 (above the game's 10) polls every 5 ms, so a long tick
  cannot starve the 186 ms buffer. A mutex guards the banks.
- **Movies** stream their soundtrack into a ring the mixer reads
  (`dc_movie_audio_*`); the ring's play position is the movie clock.
- **Watchdog.** The same thread checks a heartbeat from the main loop; after
  3 s without progress it logs where the game thread is parked, after 8 s it
  draws the stall report.

## Movies

`video.cpp` plays AVI files: a sequential demuxer over `movi` (no index
needed) and a Cinepak decoder (V1/V4 codebooks per strip, intra/inter/V1-only
chunks). Audio may be PCM 8/16, IMA ADPCM or MS ADPCM and is decoded as the
file is read. The picture follows the audio clock, so it can never run ahead
of the sound; without audio the wall clock is used. Segments restart from
the first frame (Cinepak inter frames depend on all earlier ones) and decode
silently up to the resume point.

The PC movies are 320×240 Cinepak and play as they are. The Director's Cut
movies converted by the asset migrator are H.264 `.mp4`, which the SH-4
cannot decode; `tools/dreamcast/dc_movies_to_cinepak.sh` turns them into
Cinepak AVI, and the disc scripts warn if `.mp4` files are present.

## Renderer (`MarniDX_PVR.cpp`)

Same contract as `MarniDX_GL.cpp`.

**Submission.** The game depends on draw order (background, sprites, masks,
UI), but the PVR renders its opaque list before its translucent list. So
everything goes into one translucent list with autosort disabled, and
polygons rasterise in exactly the order received. "No blending" is
ONE/ZERO inside that list. Vertices go straight to the TA (direct mode, no
DMA); the TA vertex buffer is 384 KB per frame.

**Depth.** The PVR compares 1/w-like values, larger = nearer. 3D vertices
carry view-space w, so `z = kZScale / w` gives both the depth test (GEQUAL)
and perspective-correct UVs. 2D draws use ALWAYS at a constant back depth.
KOS's `pvr_poly_cxt` `depth.write` field is the hardware's *write-disable*
bit; the backend uses the hardware meaning. 3D triangles that test depth but
do not write it (glass, water, translucent overlays) are pulled 1/512 of
their depth toward the camera, so overlays coplanar with already-drawn
geometry (room 20A0's tank) do not z-fight on the console's coarser depth.

**Textures.**
- RGBA input is converted to 16 bpp: RGB565 when opaque, ARGB1555 when alpha
  is only 0/255, ARGB4444 otherwise.
- Power-of-two sizes are twiddled (converted in RAM with 2×2 stores, then one
  store-queue copy into VRAM). Non-power-of-two widths that are a multiple of
  32 use the PVR's single global stride if they match it (the first such
  width claims it, normally 320 for backgrounds and movies); anything else is
  padded to a power of two with edge texels replicated, and UVs are scaled.
- Identical texture contents are shared: the backend hashes each texture and
  hands out the existing one with a reference count (the game keeps several
  copies of one PS1 VRAM page).
- When VRAM is short (or too fragmented), pending frees are released first;
  if allocation still fails a texture is stored at half size rather than not
  at all.
- Textures freed while the PVR may still read them are released after the
  next frame finishes.

**Smoothing** (`RE1_DC_SMOOTH`). Strided textures (backgrounds, movies) and
room masks are drawn bilinear; models, sprites, fonts and UI keep point
sampling. Before a mask is uploaded, its transparent texels take the average
colour of their opaque neighbours (alpha unchanged), so filtering does not
draw a dark halo along mask edges. Sprites from smoothed textures are inset
by half a texel so filtering stays inside their piece of the page.

**STP knock-out twins.** Per-texel semi-transparency is drawn as the game
does on the PC: the blended model plus an opaque copy whose texture has the
STP texels punched out. The twin texture belongs to its source handle and is
destroyed when the last owner of that (shared) handle releases it
(`dc_marni_texture_freed`, `MarniSystem.cpp`).

## Memory

16 MB main RAM: ~3.5 MB of static game buffers, 3 × 64 KB task stacks, the
heap (decoded sound banks are the largest variable user), and the kernel.
VRAM: ~5.3 MB free for textures after the framebuffer and TA buffers; a
typical room uses 3–4.5 MB. Sound RAM: the mixer's stream buffers plus the
file cache.

## Shared-code behaviour the port relies on

These are in the shared sources (most apply to every platform):

- **Alignment.** The SH-4 faults on unaligned loads/stores. Reads of packed
  game data go through `memcpy` (effect system `AH_SHORT` proxy,
  `ApplyMatrix`/`ApplyMatrixSV` copy misaligned operands). Any remaining
  unaligned RAM access is emulated by the exception handler in `crash.cpp`
  (correct, but slow); `RE1_DC_UACOUNT=1` finds them.
- **Room masks** (`SpriteRenderer.cpp`): `TexturePage_SetupFull` releases the
  previous camera's mask textures before loading the next, as the PS1
  overwrote them in VRAM.
- **Texture pages**: `ProcessTextureImage` releases the page handle it
  overwrites; `TexturePage_ClearAll` (menu entry) releases each legacy page
  handle before clearing the table.
- **Sprite queue**: `FlushSpriteCommandsRange` sorts only when the queue is
  not already in depth order.
- **TMD renderer**: per-object scratch arrays are grow-only buffers kept
  between calls (no `malloc`/`free` per object per frame).
- **Menu camera**: the 2 padding bytes of the camera matrix that
  `MatrixToCamera` reads as part of "to Y" are cleared before the call.
- **Effect sprites**: the sheet scratch buffer is 4 × 256 KB instead of one
  1 MB block (fragmentation).

## Diagnostics

With `RE1_DC_VERBOSE=1` (serial cable or dcload) the log carries, every 300
ticks: game speed and tick times, renderer stats (draws, triangles, live
textures, VRAM free, PVR wait and submit time, vertex-buffer peak), heap and
task-stack figures, and the disc-cache hit rate. A texture census is printed
whenever the number of live textures reaches a new high: size, format,
reference count, the file loaded just before, creator addresses, and which
game table slots (`S<n>` = `g_TexturePageSRV`, `P<n>` =
`g_TexturePageTable_DAT`) still own it.

On a release build a crash (CPU exception, KOS assert) or a stall of 8 s
draws a full-screen report with the BIOS font: exception, PC/PR, stack
words, and whether the game was waiting for the PVR or the disc. Resolve with
the ELF of exactly that build:

    sh-elf-addr2line -f -C -e build/dc/residentevil.elf <PC> <PR> <stack...>

Resolvers in `tools/dreamcast/` take a serial log and the matching ELF:

| Script | For |
|---|---|
| `census_resolve.sh` | texture census creator addresses |
| `prof_resolve.sh` | `RE1_DC_PROF=1` samples → function ranking (`LAST=1`: last report only) |
| `ftrace_resolve.sh` | `RE1_DC_FTRACE=1` function-entry log |

The profiler (`RE1_DC_PROF=1`) samples the game thread every 5 ms from the
audio thread and reports the split between work, pacing wait and blocked
time, plus the audio thread's own CPU share. In profiling builds the link
wraps `mutex_unlock` and the report lists the call sites that release
mutexes most, with time spent. The serial output of a profiling build slows
the game, so its "game speed" figure is not meaningful.

## Tools

| Script | |
|---|---|
| `make_gdi.sh` | GD-ROM image (GDEMU, Flycast); used by `make gdi` |
| `make_cdi.sh` | CDI via `mkdcdisc` (CD-R, Flycast) |
| `dc_movies_to_cinepak.sh` | Director's Cut `.mp4` movies → Cinepak AVI |
| `dc_sound_to_22k.py` | resample sound banks to 22050 Hz (RAM and disc space) |
| `config.ini` | default settings for a disc build |

## Known limitations

- Settings changed in the options menu are not kept across power-off on a
  disc build.
- Movies are 320×240; higher-resolution Cinepak would not fit on the disc.
- Camera cuts that load a new background and masks take one long tick
  (~200 ms).

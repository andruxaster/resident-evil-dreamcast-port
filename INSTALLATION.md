# Installation

This page covers getting the game data into place and pointing the executable
at it. For building the executable itself see
[How to build](README.md#how-to-build) in the README; for the optional
features (HD mode, Director's Cut, Saturn, PS1 credits, ...) see the feature
sections there.

## Which game data

The decompilation took as base the GOG USA version, which is the same binary
released in 1997, so this build expects the assets of any of those versions.

Some features of the Japanese PC release (*Biohazard* Mediakite version) are
supported as well.

The PS1 Director's Cut's content also runs, as an overlay on top of either base
tree. Use the asset migrator (below) to lay out `USA/`, `JPN/` and the
Director's Cut `DC/` overlay. The PS1 ending credits can replace the PC's
credit movies in either mode; see
[PS1 ending credits](README.md#ps1-ending-credits).

## Asset migration

No game data is distributed with this repository. The player-facing importer is a
portable Qt GUI in `tools/asset_migrator/` (released as
`asset-migrator-<version>-windows-x64.zip`). It has three tabs:

- **PC Assets** — migrates a base tree from a folder (an installed GOG/retail
  copy) or from a disc image. Choose **USA** or **Japanese** for the destination
  (`USA/` or `JPN/`), and optionally convert the `Movie/*.avi` files to `.mp4`.
  The port prefers a `.mp4` sibling of the FMV table's `.avi` and falls back to
  the AVI, so keeping both is safe. Requires `ffmpeg` on `PATH` for the
  conversion. It also writes the title menu's font (`Data/TITLEFNT.BIN`), which
  the title screen's OPTIONS item depends on.
- **Director's Cut** — builds the `DC/` overlay from a Director's Cut disc image.
  An image is required: a raw 2352-byte `.bin`/`.cue` keeps the `.STR` movies'
  CD-XA audio, which a 2048-byte copy truncates. Every DC room and all
  backgrounds are migrated. The arrange game's outfits are also copied into the
  base tree (`Enemy/EM1052`, `EM1053`, `EM1055`) so they can be worn in any mode
  as bonus outfits.
- **Sega Saturn** — builds the `SATURN/` overlay from a Saturn disc image, and
  copies the Saturn-exclusive outfit into the base tree (`Enemy/EM1061`,
  `EM1161`) for the same reason.

Above the tabs, **Room backgrounds** picks the format every tab leaves its
backgrounds in: the original `.pak`, or WebP (lossless or lossy, encoded with
`ffmpeg`), which the game reads in the `.pak`'s place.

The tool's own `README.md` covers building it, its options and the headless
verifier. The same migrations are scriptable on either platform through the
Python tools (`tools/port_dc_assets.py`, `tools/bss_to_pak.py`,
`tools/str_to_video.py`).

## Folder layout

Every asset path is built against a data root that is resolved at startup:

| What | Comes from | Default |
|---|---|---|
| Assets folder | `[Assets] Path` in `config.ini` | the executable's own directory |
| Region tree | `[Assets] Version` (`USA` / `JPN`) | `USA` |
| Save folder | `[Save] Path` in `config.ini` | `<assets folder>/SAVE` |

Relative paths are resolved from the **executable's directory**, never from the
working directory, so launching through a shortcut or a launcher with an
arbitrary CWD behaves the same. With nothing configured, put the region folder
next to the executable:

```
RE1/
  residentevil
  config.ini
  USA/
  JPN/
  DC/        (Director's Cut overlay; optional)
  SAVE/
```

The development tree keeps the assets under `assets/` one level above the build
directory, so its `config.ini` sets `Path=../../assets`. Under the VS debugger
the executable is in `bin\Debug\`, so that build needs `Path=..\..\assets` too.

On Linux, the portable bundle (see
[Packaging a portable bundle](README.md#packaging-a-portable-bundle)) takes the
same trees next to its launcher.

## config.ini

The game reads `config.ini` from the executable's directory first (falling back
to the working directory) and creates one with commented defaults if none is
present. Release packages ship `config.ini.template` from the repository root as
that initial `config.ini`. The `[Display]`, `[Player]` and `[Input]` keys are
saved on exit. `[Assets] Version`, the `[Game]` keys and `[Input] PadDef` (the
gamepad layout) are written when you leave the title screen's OPTIONS menu
(see [Options menu](README.md#options-menu-title-screen)). Everything else -
`[Assets] Path`, `[Save]`, `[Textures]`, `[Debug]`, your comments - is yours to
edit and is never rewritten.

## Asset version (USA / JPN)

`[Assets] Version` in `config.ini` selects which release the build runs. It is
read once at startup (`src/system/ConfigFile.cpp` → `SetAssetVersion`,
`src/system/AssetPath.cpp`) and swaps the data root every asset reader uses, so
**one binary runs either version** — no rebuild:

```ini
[Assets]
; Folder holding the USA/ and JPN/ trees; relative to the executable's
; directory. Empty = the executable's directory itself.
Path=
; USA = North American (default)
; JPN = Japanese (Biohazard)
Version=USA

[Save]
; Where savedat*.dat lives. Empty = <Assets Path>/SAVE, which with the default
; [Assets] Path is SAVE/ next to the executable.
Path=
```

## Troubleshooting

With no debugger attached, trace output is suppressed; set the environment
variable `RE1_DEBUGLOG=1` to append every trace line to `re1_debug.log` next to
the exe instead. Fatal startup errors also append a symbolized stack trace to
`crash.log`.

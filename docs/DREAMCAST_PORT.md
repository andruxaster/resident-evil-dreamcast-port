# Dreamcast port (KallistiOS)

Status: **phase 1 - build skeleton and platform layer.** The shared game code
builds for SH-4 with a headless renderer and silent audio, so the boot path,
room loading and scripts can be exercised on hardware/emulator before the
PowerVR backend exists.

## Layout

| File | Role |
|---|---|
| `Makefile.dreamcast` | KOS build; shared sources are read from `CMakeLists.txt` |
| `src/platform/dreamcast/platform.cpp` | `platform.h` on KOS: VFS paths, timers, canary-guarded task stacks |
| `src/platform/dreamcast/ctxswitch.{S,h}` | SH-4 register swap for the task scheduler (no ucontext in KOS newlib) |
| `src/platform/dreamcast/input.cpp` | maple controller -> XInput-layout mask in `joysticks[0]` |
| `src/platform/dreamcast/audio.cpp` | DirectSound contract, silent (phase 1) |
| `src/platform/dreamcast/video.cpp` | FMV disabled -> `VideoPlayback` skips movies |
| `src/platform/dreamcast/main.cpp` | entry point, 33 ms frame limiter, A+B+X+Y+Start exits |
| `src/marni/MarniDX_Null.cpp` | headless MarniDX: real handles + stats, no rasterizing |
| `tools/dreamcast/` | DC `config.ini`, CDI script |

Shared-code changes: `TaskScheduler.cpp` gets an `_arch_dreamcast` switch
branch; `Globals.{h,cpp}` route rand/srand through the libc override on
newlib too.

## Build

    source /opt/toolchains/dc/kos/environ.sh
    make -f Makefile.dreamcast                         # data root /cd
    make -f Makefile.dreamcast RE1_DC_ROOT=/pc/path/to/re1 run   # dcload-ip

`RE1_DC_VERBOSE=0` silences dbg_printf (serial output is slow).
`make -f Makefile.dreamcast bss-report` lists the largest symbols.

## Phase 1 exit criteria

- boots to `[RE1] frame N` heartbeats with no address error
- title -> new game -> first room loads (watch `[MARNI]` live-texture stats)
- `[DC] task N stack high-water` figures recorded -> pick TASK_STACK_SIZE
- `[DC] mem` figures recorded -> RAM budget for phase 2

## Known risks

- **Unaligned access** = address error exception on SH-4. KOS prints PC/PR;
  resolve with `sh-elf-addr2line -f -C -e build/dc/residentevil.elf <pc>`.
  Packed-struct *members* are safe (GCC emits byte accesses); casts like
  `*(DWORD*)(buf + off)` into file data are not - fix those with memcpy.
- RAM: ~3.5 MB static buffers + 3x256 KB task stacks + ~200 KB DirectSound
  object + heap, of 16 MB total.

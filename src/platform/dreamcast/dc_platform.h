// dc_platform.h - Dreamcast-only helpers used by the DC entry point.
// Not part of platform.h: game code never calls these.
#pragma once

#include "../types.h"

// Root of the data tree. "/cd" for a burned disc / CDI, "/pc/<dir>" when
// running under dcload with the host filesystem. Override at build time:
//   make -f Makefile.dreamcast RE1_DC_ROOT=/pc/home/me/re1
// 0 = release (default): log off. 1 = debug: serial/dcload log + periodic
// timing, memory, texture and audio reports.
#ifndef RE1_DC_VERBOSE
#define RE1_DC_VERBOSE 0
#endif

// 1 = one-character trace of risky operations straight to the serial port
// (needs RE1_DC_VERBOSE=1). The last letters before a hard hang name the
// operation it hung in: S/s snd_stream_poll, A/a AICA writes, C/c disc reads,
// P/p waiting for the PowerVR, T/t texture upload, '.' one game tick.
#ifndef RE1_DC_UACOUNT
#define RE1_DC_UACOUNT 0
#endif
#ifndef RE1_DC_FTRACE
#define RE1_DC_FTRACE 0
#endif
#ifndef RE1_DC_TRACE
#define RE1_DC_TRACE 0
#endif
extern "C" void dc_trace_char(char c);
#define DC_TRACE(c) do { if (RE1_DC_TRACE) dc_trace_char(c); } while (0)

#ifndef RE1_DC_ROOT
#define RE1_DC_ROOT "/cd"
#endif

// Keyboard: the DC layer feeds Win32 virtual-key codes directly (there is no
// scancode layer in between), so plat_key_event(vk, down) takes a VK.

// Task-stack canaries (there is no MMU guard page on the DC).
// Returns FALSE and prints the slot if a guard was overwritten.
BOOL dc_check_stack_guards(void);
// Print each task stack's high-water mark (bytes ever used) to the console.
void dc_report_stack_usage(void);
// Print free main-RAM/heap figures to the console.
void dc_report_memory(const char* tag);

// VMU save mirror (vmu_save.cpp). The game's save root is the RAM disk.
void        dc_save_init(void);
const char* dc_save_root(void);
extern "C" void dc_save_commit(const char* path, const void* data, int len);

// /cd file system on the port's ISO reader (dc_iso.cpp). 0 = mounted.
extern "C" int dc_iso_mount(void);

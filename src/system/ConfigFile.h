// ConfigFile.h - config.ini as the single settings store (both builds).
//
// Windows used to keep the player's settings in HKCU\Software\CAPCOM\RESIDENT
// EVIL; this module replaces that with the same file the port already reads as
// a dev override, so settings travel between the Windows and Linux builds and
// nothing lands in a tracked file (config.ini is gitignored now).
//
//   - ConfigFile_Load()      reads [Display]/[Player]/[Input] (plus the
//                            [Assets] and [Debug] dev knobs) into the globals.
//   - ConfigFile_Save()      rewrites ONLY the keys the game owns, in place:
//                            comments, unknown keys and section order survive.
//   - ConfigFile_EnsureExists() writes the documented default file when it is
//                            missing, so a fresh install has something to edit.
//
// The Windows registry is still READ (system/Installation.cpp) before this,
// so an existing install keeps its bindings until the first save; it is no
// longer written.
#pragma once
#include "../platform/types.h"

// Locate config.ini: current directory first, then the parent directories the
// Windows build has always searched. Returns the path to use for writing.
const char* ConfigFile_Find(void);

// Write the default config.ini if none exists.
void ConfigFile_EnsureExists(void);

// Read the file into the game globals. FALSE if there is no file.
BOOL ConfigFile_Load(void);

// Write the current settings back, preserving everything else in the file.
void ConfigFile_Save(void);

// Write the settings the title-screen options menu edits as well: the [Game]
// keys, [Input] PadDef (the gamepad layout) and the two that only take effect
// on the next start - [Game] Mode and [Assets] Version - which are passed in
// rather than read from the running game, because the game is still running
// the mode and region it started with. Everything ConfigFile_Save() writes is
// included.
void ConfigFile_SaveGameSettings(int gameMode, int regionJpn);

// The [Textures] HdTextures value ConfigFile_SaveGameSettings() writes. Like
// Mode and Region it only takes effect on the next start, so the running
// setting (HdTextures_ReplaceEnabled) is not touched. Until this is called the
// running value is written.
void ConfigFile_SetHdTextures(int enabled);

// "OG" / "DC" / "SATURN" - the [Game] Mode spelling, which is also the asset
// overlay's folder name.
const char* ConfigFile_GameModeName(int mode);

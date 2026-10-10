// config.cpp - settings-save hook and single-instance check (Dreamcast).
#include "../platform.h"

#include "../../Globals.h"
#include "../../system/ConfigFile.h"

// CleanupVideoConfigAndSaveAllSettings (0x00497ea0). On a disc build the
// config sits on read-only /cd, so the write fails quietly; with dcload (/pc)
// it is written back to the host. VMU-backed settings come with phase 3.
void CleanupVideoConfigAndSaveAllSettings(void)
{
    if (g_bHasFinalizedSettings) return;
    g_bHasFinalizedSettings = TRUE;
    ConfigFile_Save();
}

// One program runs at a time on the console.
BOOL plat_single_instance_check(void) { return TRUE; }

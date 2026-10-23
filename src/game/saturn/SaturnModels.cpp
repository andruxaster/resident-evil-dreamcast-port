// SaturnModels.cpp - Sega Saturn entity model selection.
//
// Everything the Saturn adds to the model table is in saturn/SaturnModels.h.
// The two pieces are the table override (two indices) and the Tick loader swap
// (one index, one stage); the Golden Tyrant's palette tint, the Zombie Wesker's
// arena-80C0 type override and the Battle Game's own rooms come later, with
// Phase C, because each of them needs the battle-mode flag that Phase C ports
// first.
#include "SaturnModels.h"

#include "SaturnBattle.h"
#include "../../Globals.h"
#include "../../system/AssetPath.h"

#include <cstring>

// The OG/DC costume indices the Saturn remaps (LoadEntityEMD: variant + 0x33).
#define SATURN_COSTUME_INDEX_1  0x33
#define SATURN_COSTUME_INDEX_2  0x34

// Whether the migrator wrote the outfit's file. [outfit 0/1][block 0/1], probed
// once: a missing file would make LoadEntityEMD read -1 bytes (LoadFile failure
// corrupts its callers), so the remap is simply not taken.
static int saturn_outfit_file_present(int outfit, int jillBlock)
{
    static const char* const kRel[2][2] = {
        { "Enemy/EM1060.EMD", "Enemy/EM1160.EMD" },
        { "Enemy/EM1061.EMD", "Enemy/EM1161.EMD" },
    };
    static int s_probe[2][2] = { { -1, -1 }, { -1, -1 } };
    if (s_probe[outfit][jillBlock] < 0) {
        s_probe[outfit][jillBlock] = AssetOverlayHasFile("SATURN", kRel[outfit][jillBlock]);
    }
    return s_probe[outfit][jillBlock];
}

int saturn_emd_is_outfit(unsigned char modelIndex)
{
    return modelIndex == SATURN_EMD_INDEX_OUTFIT_1 || modelIndex == SATURN_EMD_INDEX_OUTFIT_2;
}

const char* saturn_emd_path(unsigned char modelIndex, unsigned char jillBlock)
{
    switch (modelIndex) {
    // Indices 28 and 29: the Saturn's alternate outfits (SaturnModels.h).
    case SATURN_EMD_INDEX_OUTFIT_1:
        return jillBlock ? "enemy/em1160.emd" : "enemy/em1060.emd";
    case SATURN_EMD_INDEX_OUTFIT_2:
        return jillBlock ? "enemy/em1161.emd" : "enemy/em1061.emd";
    // Index 26 = entity type 0x16. The PC's table holds the first em100a filler
    // here because no USA room ever spawns id 0x16 - the Saturn's Tick is the
    // only thing in any release that does.
    case SATURN_EMD_INDEX_TICK:
        return jillBlock ? "enemy/em1116.emd" : "enemy/em1016.emd";
    // Index 27 = entity type 0x17, Zombie Wesker. Not a campaign type either:
    // the Saturn forces it on arena 80C0's first enemy set, in battle mode.
    case SATURN_EMD_INDEX_ZOMBIE_WESKER:
        return jillBlock ? "enemy/em1117.emd" : "enemy/em1017.emd";
    default:
        // Index 35 would be em1018/em1118 (type 0x1F), but that model is not on
        // the disc, so the table entry stands and nothing reads it.
        return NULL;
    }
}

unsigned char saturn_emd_model_index(unsigned char modelIndex)
{
    // The costume fixup left the OG/DC alternate-outfit index; the Saturn's own
    // outfits live at their own indices so neither set shadows the other. Only the
    // Saturn proper: a Battle Game run borrowed from OG/DC wears the save's outfit.
    if ((modelIndex == SATURN_COSTUME_INDEX_1 || modelIndex == SATURN_COSTUME_INDEX_2) &&
        !saturn_mode_is_borrowed()) {
        // The Saturn's loader (GAME5 0x0604EA80) maps scenario flags 112/113 to a
        // model: neither = default, 112 only = slot 51, 112+113 = slot 52. The
        // costume bit is only up when 112 is (LoadEntityEMD), so 113 picks which.
        // g_bCostumeVariant (0x33 vs 0x34) plays no part: the Saturn's wardrobe
        // never runs the PC's costume opcode.
        const int outfit = Flg_ck((int)g_ScenarioFlags, SATURN_SCENARIO_FLAG_OUTFIT_2) != 0 ? 1 : 0;
        if (saturn_outfit_file_present(outfit, g_playerEntity.id & 1)) {
            return outfit ? SATURN_EMD_INDEX_OUTFIT_2 : SATURN_EMD_INDEX_OUTFIT_1;
        }
    }
    // The Saturn's loader (GAME5 0x0604EA80) tests the stage byte and the model
    // index together, right where the PC reads g_emdPathTable. g_stageId is the
    // same 0-indexed stage the Saturn's `0x002F8660` holds, and Saturn mode has
    // no arrange stages, so no room_file_stage() fold is wanted here: the test
    // is against the stage the game is actually in.
    if (modelIndex == SATURN_EMD_INDEX_HUNTER && g_stageId == SATURN_TICK_STAGE) {
        return SATURN_EMD_INDEX_TICK;
    }
    return modelIndex;
}

const char* saturn_tick_snd_name(int slot, const char* baseName, const char* const* row)
{
    // Slot n is what Snd_em(n) plays; the samples come from the room's SE bank
    // song its slot table names (not song n), see core/SaturnMusic.h.
    static const char* const kTickSnd[9] = {
        "Tk_walkA", "Tk_walkB", "Tk_jump", "Tk_att",
        "Tk_land",  "Tk_smash", "Tk_dam",  "Tk_Nout",
        "Tk_stuck",
    };
    if (g_stageId != SATURN_TICK_STAGE || slot < 0 || slot >= 9) return NULL;
    // Slot 8 is empty on the PC (nothing to key on), and the stab's floor strike
    // (Snd_em(8)) needs it. Only a room whose row is the Hunter's gets it.
    if (slot == 8) {
        if (row == NULL || row[0] == NULL || strcmp(row[0], "He_walkA") != 0) return NULL;
        return kTickSnd[8];
    }
    // Only the Hunter's own rows: the PC names those He_*, and every other
    // sound in the room (props, footsteps) stays as it is.
    if (baseName == NULL || baseName[0] != 'H' || baseName[1] != 'e' || baseName[2] != '_') {
        return NULL;
    }
    return kTickSnd[slot];
}

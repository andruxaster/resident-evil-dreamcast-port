// SaturnRoomObjects.cpp - Sega Saturn room-object overrides.
#include "SaturnRoomObjects.h"
#include "SaturnBattle.h"

#include "../../Globals.h"
#include "../Types.h"
#include "../BioCard.h"

extern unsigned int Flg_ck(int baseAddr, unsigned int bitIndex);   // 0x00473f10

// GAME4 0x06055AA8 (GAME5 has the same code) - the Saturn's per-frame room
// object update. For stage byte 4, room 0x13, omodel slot 0 it rewrites the
// record every frame:
//
//   byte 0        = (flag(g_ScenarioFlags, 0x72) == 0)   visible until set
//   +0x34/+0x6C   = 0x1CB6       x   (long copy / short copy)
//   +0x38/+0x6E   = -0x1000      y
//   +0x3C/+0x70   = 0x738C       z
//   +0x74         = 0x238        yaw
//
// Slot 0 of ROOM5130 is the tank's water surface, which drains when the Tyrant
// escapes; on the PC it sits at (0x1FF4, -0x208, 0x5730). The Saturn parks it
// at the extra capsule instead. Saturn flag 0x72 (g_ScenarioFlags) is raised
// when the second Tyrant breaks out of that capsule - the Tyrant's own release
// code, GAME4 0x06078AF8, ported in SaturnTyrant.cpp (saturn_release_reraise).
// An older note put it in a "type 0x2E Wesker actor handler": that was wrong,
// 0x06078954 is a sub-routine of the Tyrant's update and type 0x2E runs the
// shared NPC driver (the real type table is 0x06096848).
#define SATURN_CAPSULE_WATER_SLOT   0
#define SATURN_CAPSULE_WATER_X      0x1CB6
#define SATURN_CAPSULE_WATER_Y      (-0x1000)
#define SATURN_CAPSULE_WATER_Z      0x738C
#define SATURN_CAPSULE_WATER_YAW    0x238
#define SATURN_TYRANT_RELEASED_FLAG 0x72       // g_ScenarioFlags

void saturn_room_objects_update(void)
{
    if (g_stageId != STAGE_LABORATORY || g_roomId != ROOM_MAIN_LAB) return;
    if (g_RdtPointer == NULL || (unsigned char)g_RdtPointer->omodel_slot_count == 0) return;

    unsigned char* obj = (unsigned char*)g_omodel_table[SATURN_CAPSULE_WATER_SLOT];
    if (obj == NULL) return;

    // On the Saturn this slot's model is ENEMY/OB80.DAT, the dark cover over the
    // second capsule (GAME4 table 0x060980F4; see saturn_omodel_has_no_model),
    // which the asset migrator bakes into a TMD and points object_models[0] at.
    // With that room file the record shows until the second Tyrant breaks out,
    // exactly as the Saturn writes it. An older migrated room still carries the
    // PC's water plane in slot 0 - the TMD slots 1-3 share - which the Saturn
    // never shows, so it stays hidden.
    const int* models = (const int*)g_RdtPointer->object_models;
    const bool hasSaturnCover =
        (unsigned char)g_RdtPointer->omodel_slot_count > 1 && models[0] != models[2];
    obj[0] = (hasSaturnCover &&
              Flg_ck((int)g_ScenarioFlags, SATURN_TYRANT_RELEASED_FLAG) == 0) ? 1 : 0;

    *(short*)(obj + 0x6c) = (short)SATURN_CAPSULE_WATER_X;
    *(int*)  (obj + 0x34) = SATURN_CAPSULE_WATER_X;
    *(short*)(obj + 0x6e) = (short)SATURN_CAPSULE_WATER_Y;
    *(int*)  (obj + 0x38) = SATURN_CAPSULE_WATER_Y;
    *(short*)(obj + 0x70) = (short)SATURN_CAPSULE_WATER_Z;
    *(int*)  (obj + 0x3c) = SATURN_CAPSULE_WATER_Z;
    *(short*)(obj + 0x74) = SATURN_CAPSULE_WATER_YAW;
}

int saturn_room_objects_hidden(void)
{
    return g_stageId == STAGE_LABORATORY && g_roomId == ROOM_MAIN_LAB && g_roomCameraId == 5;
}

// GAME4 0x060980F4 + stage * 0x10E + room * 9: the OBnn.DAT number per omodel
// slot. Stage 4 (Laboratory) room 0x13 = { 0x50, 0, 0, 0, 0x12, 0x5A, 0x03 }:
// slot 0 is OB80 (the dark capsule cover), slots 1-3 have none, slots 4-6 are
// OB18 / OB90 / OB03. Confirmed in the running game (Yaba Sanshiro, lab room):
// registry entries 2-4 - slots 1-3 - hold a null model handle, so the three
// water columns the PC draws in the centre tanks do not exist on the Saturn,
// whose backgrounds show those tanks drained. Room 0x06's row is all zero too
// (its three slots) but has not been checked live, so it is not listed.
static const unsigned char kLabRoom13Models[9] = {
    0x50, 0x00, 0x00, 0x00, 0x12, 0x5A, 0x03, 0x00, 0x00
};

// The Battle Game's rows of the same table. GAME5's lookup (0x0605A348) folds
// the stage byte before indexing it - stages 5 and 6 read rows 0 and 1, and
// stage 7, the Battle Game, reads ROW 5 - with the arena number as the room.
// GAME5 0x0609CD9C + 5 * 0x10E + arena * 9: slot 0 of the three rest rooms
// (arenas 1, 6, 11) is OB31; every other slot of every arena is 0, no model.
// So the fight arenas draw none of their RDT's omodels - arena 15's four
// centre tubes included, whose broken glass is in the room's background and
// masks instead.
static int saturn_battle_omodel_has_model(int arena, int slot)
{
    return slot == 0 && (arena == 1 || arena == 6 || arena == 11);
}

int saturn_omodel_has_no_model(int slot)
{
    if (g_saturnBattleActive && g_stageId == SATURN_BATTLE_STAGE_ID) {
        return !saturn_battle_omodel_has_model((int)g_roomId, slot);
    }
    if (g_stageId != STAGE_LABORATORY || g_roomId != ROOM_MAIN_LAB) return 0;
    if (slot < 0 || slot >= 9) return 0;
    return kLabRoom13Models[slot] == 0;
}

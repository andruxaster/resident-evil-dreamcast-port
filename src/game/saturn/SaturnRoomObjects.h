// SaturnRoomObjects.h - Sega Saturn room-object overrides.
//
// The Saturn ships the PC's campaign rooms unchanged and moves things in code
// (docs/SATURN_VERSION_ANALYSIS.md 4g). The main lab's extra Tyrant capsule is one
// of them: omodel slot 0 of ROOM5130 (the PC's tank water surface) is
// relocated to the capsule and hidden when the second Tyrant escapes, and on
// the Saturn that slot carries a different model, ENEMY/OB80.DAT.
#pragma once

// Applies the Saturn's per-frame overrides to the room's omodel records. Call
// once per frame before update_room_objects(), Saturn mode only (the caller
// gates on g_bSaturnMode). Does nothing outside the rooms it knows.
void saturn_room_objects_update(void);

// The Saturn draws none of a room's omodels/items while the camera is 5 in the
// main lab (GAME4 0x06055CA0-0x06055CBC: stage 4, room 0x13, camera 5 skips the
// draw call). Returns non-zero when render_room_objects() should be skipped.
int saturn_room_objects_hidden(void);

// The Saturn does not build omodels from the room file's TMDs: it loads one
// ENEMY/OBnn.DAT per slot from a per-room table (GAME4 0x060980F4, 9 bytes per
// room; loader 0x060680F0 / 0x060681E8), and a slot whose entry is 0 gets no
// model and is never drawn. Returns non-zero for such a slot in the current
// room, so render_room_objects() skips it. Only rooms verified against the
// running Saturn are listed.
int saturn_omodel_has_no_model(int slot);

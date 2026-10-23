// SaturnTyrant.cpp - the Sega Saturn's second lab Tyrant (Chris, Laboratory B4).
//
// Ported from GAME4.PRG (the Main Laboratory, loaded at 0x0602B000). Every
// address in this file is a GAME4 address unless it says otherwise; the PC
// names are given beside them.
//
// What the Saturn does
// --------------------
// It is all inside the Tyrant's own update. The room (ROOM5130) is the PC's
// byte for byte and spawns one type-0x0C Tyrant exactly as on the PC; nothing
// is scripted. The Saturn changes three things in the Tyrant's code:
//
//   1. State 3, the death entry (0x06074C8C). The PC always runs behaviour 0
//      (`tyrant_behavior_restrained`, the collapse that raises the room's death
//      flag). The Saturn runs behaviour 2 instead - a new entry it inserted in
//      its behaviour table (0x0609A994[2] = 0x06077804) - unless entity +0x186
//      bit 0x08 is set. tyrant_init sets that bit when the player is Jill, so
//      Jill's Tyrant dies the PC way and only Chris gets the rest.
//
//   2. Behaviour 2, the dissolve (0x06077804). The corpse collapses (the same
//      animation 8 as the PC's death), the battle music stops and the room's
//      own two channels come back, its joints freeze in the death pose and then
//      shrink, one joint at a time turns brown, the whole model darkens, smoke
//      rises off random joints, the joints vanish one by one and the heart goes
//      out. It does NOT raise the room's death flag - the room still thinks the
//      Tyrant is alive - and it arms the second Tyrant (+0x186 |= 4).
//
//   3. The release (0x060788A8, run from the end of tyrant_update while +0x186
//      bit 0x04 is set). It waits for the point 0x258 in front of Chris to
//      enter a rectangle at the lab terminal, then runs its own cutscene: the
//      camera cuts to camera 5, the letterbox comes in, glass is pounded, Chris
//      turns to face the capsule, the battle theme restarts on camera 0, and the
//      SAME entity is re-initialised as a fresh Tyrant at the second capsule,
//      which shatters (the PC's own break-out animation and shard burst,
//      relocated), after which it is an ordinary Tyrant. Its +0x186 bit 0x08 is
//      set on the way, so its death is the PC's - which is what finally raises
//      the room's death flag.
//
// Per-entity bytes the Saturn uses (all free in the PC's Tyrant; the generic
// names in Entities.h at these offsets belong to other enemies):
//   +0x17A  u16  dissolve: joints already tinted (bit per step)
//   +0x184  u8   release routine: 0 = watch for Chris, 1 = cutscene
//   +0x185  u8   release cutscene step
//   +0x186  u8   0x04 release armed, 0x08 die the PC way, 0x10 cutscene running
//   +0x187  u8   dissolve: joints affected so far
//   +0x188  u8   release cutscene timer
//
// What is not ported, and why
// ---------------------------
//   * 0x0608AC08(15) at the letterbox cue and 0x0603C580(prop registry entry)
//     at the re-raise are Saturn display plumbing (an SGL screen call and a
//     VRAM re-upload of a registered model). The PC keeps its textures
//     resident and draws the letterbox from MSF_INTENSITY_RAMP alone.
//   * The Saturn's colour steps go through VDP1 Gouraud tables (0x0605E2A8) and
//     a per-model CRAM palette fade (0x0608309C / reset 0x06083248). The PC has
//     neither, so the port uses its own per-joint vertex tint,
//     JointApplyColorTint - the same call the PS1-derived PC code makes with
//     the same three-colour argument list - and scales it for the fade. That is
//     the one substitution in this file.
//   * The Battle Game branch of the init (0x060746A2) - the port has no Battle
//     Game mode yet.

#include "SaturnTyrant.h"
#include "SaturnBattle.h"

#include "../../Globals.h"
#include "../BioCard.h"
#include "../Types.h"
#include "../entities/EntityCommon.h"

extern void Flg_on(int baseAddr, unsigned int bitIndex);           // 0x00473ef0
extern void play_sound_and_voice_effect(int type, int id);         // 0x00475340
extern void SetSndSlot(int bank, int slot);
extern void setSndStop(int bank);
extern void set_volume(int bank, int vol);
extern int  SquareRoot0(int val);
extern void tyrant_root_motion_saturn(unsigned char set, char apply);   // Tyrant.cpp

namespace {

inline unsigned char&  sb(Entity* e, int off) { return *((unsigned char*)e + off); }
inline unsigned short& sw(Entity* e, int off) { return *(unsigned short*)((unsigned char*)e + off); }

// The state word as the Saturn stores it: state, ignore_player_flag,
// action_behavior, action_state (+0x84..+0x87). Written byte by byte so the
// Saturn's big-endian constants read the same here.
void set_state_bytes(Entity* e, unsigned char state, unsigned char ignore,
                     unsigned char behavior, unsigned char sub)
{
    e->state = state;
    e->ignore_player_flag = ignore;
    e->action_behavior = behavior;
    e->action_state = sub;
}

// +0x170 is the Tyrant's heart, a cloned entity (tyrant_init). Byte 0 is its
// status: 0 hides it, 1 shows it again.
Entity* tyrant_heart(Entity* e)
{
    return (Entity*)(uintptr_t)*(unsigned int*)((unsigned char*)e + 0x170);
}

const int SATURN_TYRANT_JOINTS = 15;           // every loop in the Saturn walks 15
const unsigned char SATURN_TYRANT_BEHAVIOR_DISSOLVE = 2;

// 0x0609A9EC - the order the dissolve takes the joints in (16 entries: the
// tint step runs one past the 15 joints before its done-mask is full).
const unsigned char kDissolveOrder[16] = {
    1, 0, 2, 6, 3, 12, 9, 7, 4, 13, 10, 8, 5, 14, 11, 0
};

// ---------------------------------------------------------------------------
// BGM: 0x060583A8(_, ch) stops a channel and 0x060582CC(_, ch, 0, 0) starts
// one - the same two primitives SCD opcodes 0x16 / 0x15 reach. ROOM5130's
// group 0x34 is { Se_55, Bgm_20, Bgm_24 }: ambience, lab theme, Tyrant battle.
// ---------------------------------------------------------------------------
void saturn_bgm_play(unsigned int ch)                       // cmd_bgm_play
{
    if (g_SndBank[ch].handle != 0) {
        SetSndSlot(g_SndBank[ch].handle, (int)g_SndBank[ch].slot);
    }
    g_BGM_STATE |= 1u << (ch + 3);
}

void saturn_bgm_stop(unsigned int ch)                       // cmd_bgm_stop
{
    const unsigned int bit = 1u << (ch + 3);
    if ((g_BGM_STATE & bit) != 0) {
        if (g_SndBank[ch].handle != 0) setSndStop(g_SndBank[ch].handle);
        g_BGM_STATE &= ~bit;
        set_volume(g_SndBank[ch].handle, -1);
    }
}

// ---------------------------------------------------------------------------
// The colour substitution (see the file header). The Saturn tints a joint with
// three colours, 0x706050 / 0x507060 / 0x506070 (0x0605E2A8); the PC's
// JointApplyColorTint takes the same argument list and applies the first. The
// whole-model fade (0x0608309C) steps each RGB555 component by -1 per call,
// clamped to -31; the port scales the tint by (31 + step) / 31 instead.
// ---------------------------------------------------------------------------
const unsigned int SATURN_TINT_NEUTRAL = 0x808080;   // 0x80 = 1.0 in JointSetColorTint
const unsigned int SATURN_TINT_DECAY   = 0x706050;

int g_saturnTyrantFade = 0;                          // 0 .. -31

unsigned int scale_tint(unsigned int c, int fade)
{
    unsigned int out = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        int v = (int)((c >> shift) & 0xFF) * (31 + fade) / 31;
        out |= (unsigned int)v << shift;
    }
    return out;
}

void apply_tints(Entity* e)
{
    JointStruct* j = e->jointsStructs;
    if (j == NULL) return;
    const unsigned short done = sw(e, 0x17A);
    for (int i = 0; i < SATURN_TYRANT_JOINTS; ++i) {
        unsigned int base = SATURN_TINT_NEUTRAL;
        for (int n = 0; n < 16; ++n) {
            if (((done >> n) & 1) != 0 && kDissolveOrder[n] == i) base = SATURN_TINT_DECAY;
        }
        JointApplyColorTint(&j[i], (int)scale_tint(base, g_saturnTyrantFade),
                            0x507060, (void*)0x506070);
    }
}

// ===========================================================================
// 0x06077804 - state-3 behaviour 2: the dissolve. Sub state = action_state,
// timer = +0xC4 (the Tyrant's action_ticks_counter), counting up to 0x201.
// ===========================================================================
void saturn_tyrant_behavior_dissolve(void)
{
    Entity* e = ENTITY;
    JointStruct* j = e->jointsStructs;

    switch (e->action_state) {
    case 0:
        e->action_state = 1;
        e->animation_frame_id = 0;
        e->timing_control = 0;
        e->blend_counter = 0x0F;
        e->animationId = 8;                     // the PC's collapse
        saturn_bgm_stop(2);                     // Bgm_24 off
        saturn_bgm_play(0);                     // the room's own two channels back
        saturn_bgm_play(1);
        // fall through, as 0x06077888 does
    case 1:
        e->action_state = (unsigned char)(e->action_state +
            (char)Joint_move(0, e->animHeader, e->animBase, 0x100));
        if (e->animation_frame_id == 0x19 && (e->timing_control & 1) != 0) Snd_em(6);
        if (e->animation_frame_id == 0x32 && (e->timing_control & 1) != 0) Snd_em(6);
        tyrant_root_motion_saturn(0, 0);        // 0x06078560
        Add_speedXZ(0);                         // 0x0605EA90
        sw(e, 0xC4) = 0;
        sb(e, 0x187) = 0;
        sw(e, 0x17A) = 0;
        sb(e, 0x186) |= 4;                      // arm the second Tyrant
        g_saturnTyrantFade = 0;
        break;

    case 2:                                     // 0x06077920
        if ((short)sw(e, 0xC4) > 0xBF) {
            sb(e, 0x179) &= 0xFD;               // look-at off
            e->lookAtFlags = 0;
            for (int i = 0; i < SATURN_TYRANT_JOINTS; ++i) j[i].flags |= 8;   // freeze
            sb(e, 0x179) &= 0xFE;               // ground shadow off
            e->action_state = 3;
        }
        break;

    case 3: {                                   // 0x060779B4
        VECTOR s = { 0xFB0, 0x1004, 0x1004, 0 };
        for (int i = 0; i < SATURN_TYRANT_JOINTS; ++i) {
            if (i < sb(e, 0x187)) ScaleMatrixCols(&j[kDissolveOrder[i]].world, &s);
        }
        const unsigned short t = sw(e, 0xC4);
        if ((t & 1) == 0) {
            const unsigned short done = sw(e, 0x17A);
            if (done != 0xFFFF) {
                const unsigned int n = sb(e, 0x187);
                if (((done >> n) & 1) == 0) {
                    sw(e, 0x17A) = (unsigned short)(done | (1u << n));
                    apply_tints(e);
                }
            }
            sb(e, 0x187)++;
        }
        if ((short)t > 0x11F && (t & 1) == 0 && g_saturnTyrantFade > -31) {
            g_saturnTyrantFade--;               // 0x0608309C(-1, -1, -1, 0, id, 0x200)
            apply_tints(e);
        }
        if ((short)t > 0x13F) e->action_state = 4;
        break;
    }

    case 4:                                     // 0x06077ABE
        if ((short)sw(e, 0xC4) > 0x14F) {
            e->action_state = 5;
            sb(e, 0x187) = 15;
        }
        break;

    case 5: {                                   // 0x06077B28
        VECTOR s = { 0x400, 0xEE0, 0xEE0, 0 };
        for (int i = 0; i < SATURN_TYRANT_JOINTS; ++i) ScaleMatrixCols(&j[i].world, &s);
        if ((short)sw(e, 0xC4) > 0x17F) {
            const unsigned char k = sb(e, 0x187);
            sb(e, 0x187) = (unsigned char)(k - 1);
            j[kDissolveOrder[k & 15]].flags &= 0xFE;     // hide it
            sb(e, 0x187) &= 15;
        }
        if ((short)sw(e, 0xC4) > 0x18F) {
            Entity* heart = tyrant_heart(e);
            if (heart != NULL) heart->status_flags = 0;
            e->action_state = 6;
        }
        break;
    }

    case 6:                                     // 0x06077BC6
        e->status_flags |= 0x0A;
        set_state_bytes(e, 5, 0, 0, 0);         // state 5: nothing runs
        break;

    default:
        break;
    }

    // 0x06077BDC - the smoke, every 8th frame of timer 64..288, unless the
    // release cutscene has started (+0x186 bit 0x10).
    const unsigned short t = sw(e, 0xC4);
    if ((unsigned short)(t - 64) <= 0xE0 && (sb(e, 0x186) & 0x10) == 0 && (t & 7) == 0) {
        VECTOR off;
        off.x = -(rand() & 0x1FF);
        off.y = (rand() & 0x1FF) - 0x100;
        off.z = (rand() & 0x1FF) - 0x100;
        off.pad = 0;
        // The Saturn picks rand() % 16 of a 15-joint model; index 15 is past the
        // array and the Saturn reads whatever follows it. The draw is kept so the
        // random stream is consumed the same; the out-of-range puff is dropped.
        const int k = rand() % 16;
        if (k < e->jointCount) {
            Effect_CreateBillboard(9, 1, 0, &j[k].world, &off, 20);
        }
    }
    if (t <= 0x200) sw(e, 0xC4) = (unsigned short)(t + 1);
}

// ===========================================================================
// The release, 0x060788A8: +0x184 selects 0x060788D0 (watch) or 0x06078954
// (the cutscene).
// ===========================================================================

// 0x0609AA44 - the trigger rectangle at the lab terminal: x0, z0, width, depth.
const unsigned short kReleaseZone[4] = { 0x170C, 0x0ED8, 0x0DAC, 0x0AFC };

// 0x060788D0 - a point 0x258 in front of Chris, tested against the rectangle
// (0x06052C80: unsigned (p - corner) <= size on each axis).
void saturn_release_watch(Entity* e)
{
    SVECTOR ahead = { 0x258, 0, 0, 0 };
    MATRIX m = {};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = 0x1000;
    RotMatrixY((int)g_playerEntity.directionAngle, &m);     // 0x06052C10
    ApplyMatrixSV(&m, &ahead, &ahead);

    const int x = g_playerEntity.scaMatrixData.localMatrix.t[0] + ahead.x;
    const int z = g_playerEntity.scaMatrixData.localMatrix.t[2] + ahead.z;
    if ((unsigned int)(x - kReleaseZone[0]) <= kReleaseZone[2] &&
        (unsigned int)(z - kReleaseZone[1]) <= kReleaseZone[3]) {
        sb(e, 0x184) = 1;
        sb(e, 0x185) = 0;
    }
}

// The glass, 0x06078D2E: the PC's 53-shard burst (em_scd_behavior_break_glass,
// s_tyrantGlassShards) with every base moved to the second capsule. Variants
// and lifetimes are the PC's, shard for shard; every rotation base is the PC's
// + 0x800, so the glass flies the other way. The rand() stream is consumed rot,
// y, x, z per shard, like the PC's.
struct SaturnShard {
    short rotBase;
    short xBase, zBase;
    unsigned short xzMask;
    unsigned char variant, life;
};

const SaturnShard kCapsule2Shards[53] = {
    { 0x05ac, 0x2318, 0x6bb4, 0x1ff, 1, 10 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 2, 20 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 3, 10 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 4, 30 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 5, 10 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 6, 20 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 7, 20 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 1, 10 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 5, 10 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 6, 30 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 7, 20 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 1, 10 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 5, 10 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 6, 30 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 7, 20 },
    { 0x05ac, 0x2318, 0x69c0, 0x1ff, 1, 10 },
    { 0x1300, 0x1da0, 0x6a88, 0x1ff, 1, 10 },
    { 0x1300, 0x1da0, 0x6a88, 0x1ff, 2, 20 },
    { 0x1300, 0x1da0, 0x6a88, 0x1ff, 3, 10 },
    { 0x1300, 0x1da0, 0x6a88, 0x1ff, 4, 20 },
    { 0x1300, 0x1da0, 0x6a88, 0x1ff, 5, 10 },
    { 0x1300, 0x1da0, 0x6a88, 0x1ff, 6, 25 },
    { 0x1300, 0x1da0, 0x6a88, 0x1ff, 7, 20 },
    { 0x1300, 0x1da0, 0x6a88, 0x1ff, 1, 10 },
    { 0x1300, 0x1da0, 0x6a88, 0x1ff, 5, 10 },
    { 0x1300, 0x1da0, 0x6a88, 0x1ff, 6, 20 },
    { 0x1300, 0x1da0, 0x6a88, 0x1ff, 7, 15 },
    { 0x04e4, 0x1ff8, 0x6a24, 0x7ff, 7, 15 },
    { 0x04e4, 0x1ff8, 0x6a24, 0x7ff, 1, 25 },
    { 0x04e4, 0x1ff8, 0x6a24, 0x7ff, 5, 15 },
    { 0x04e4, 0x1ff8, 0x6a24, 0x7ff, 6, 10 },
    { 0x04e4, 0x1ff8, 0x6a24, 0x7ff, 7, 10 },
    { 0x04e4, 0x1ff8, 0x6a24, 0x7ff, 1, 10 },
    { 0x04e4, 0x1ff8, 0x6a24, 0x7ff, 6, 20 },
    { 0x04e4, 0x1ff8, 0x6a24, 0x7ff, 7, 10 },
    { 0x04e4, 0x1ff8, 0x6a24, 0x7ff, 1, 10 },
    { 0x14f4, 0x1ff8, 0x6a24, 0x7ff, 1, 15 },
    { 0x14f4, 0x1ff8, 0x6a24, 0x7ff, 2, 25 },
    { 0x14f4, 0x1ff8, 0x6a24, 0x7ff, 3, 15 },
    { 0x14f4, 0x1ff8, 0x6a24, 0x7ff, 4, 10 },
    { 0x14f4, 0x1ff8, 0x6a24, 0x7ff, 5,  5 },
    { 0x14f4, 0x1ff8, 0x6a24, 0x7ff, 3, 15 },
    { 0x14f4, 0x1ff8, 0x6a24, 0x7ff, 4, 15 },
    { 0x14f4, 0x1ff8, 0x6a24, 0x7ff, 5, 10 },
    { 0x04e4, 0x1f30, 0x6a24, 0x7ff, 7, 25 },
    { 0x04e4, 0x1f30, 0x6a24, 0x7ff, 1,  5 },
    { 0x04e4, 0x1f30, 0x6a24, 0x7ff, 6, 15 },
    { 0x04e4, 0x1f30, 0x6a24, 0x7ff, 7, 25 },
    { 0x04e4, 0x1f30, 0x6a24, 0x7ff, 1,  5 },
    { 0x14f4, 0x1f30, 0x6a24, 0x7ff, 1,  5 },
    { 0x14f4, 0x1f30, 0x6a24, 0x7ff, 2,  5 },
    { 0x14f4, 0x1f30, 0x6a24, 0x7ff, 3, 15 },
    { 0x14f4, 0x1f30, 0x6a24, 0x7ff, 4, 15 },
};

// 0x06078AF8 - re-raise the corpse as a fresh Tyrant at the second capsule.
// tyrant_init's field setup without the heart clone and the joint reset, which
// the entity already has. Falls through into step 3, as the Saturn does.
void saturn_release_reraise(Entity* e)
{
    JointStruct* j = e->jointsStructs;

    // Unfreeze and show every joint. The Saturn writes (flags & 0xAF) | 1: on
    // its renderer a joint without 0x40 is recomputed every frame. The PC's
    // rotate_entity recomputes only joints with bit 0x02, which the freeze
    // cleared, so the PC's normal value 3 is restored instead.
    for (int i = 0; i < SATURN_TYRANT_JOINTS; ++i) {
        j[i].flags = (unsigned char)((j[i].flags & 0xAF) | 3);
    }
    // 0x06083248(id, 0, 0x200): the palette fade is reset.
    g_saturnTyrantFade = 0;
    sw(e, 0x17A) = 0;
    apply_tints(e);

    e->scaMatrixData.field_00 = 0;
    e->hit_state = 0;
    e->health = 0xDC;                           // 220, tyrant_init's value
    sb(e, 0x186) |= 8;                          // this one dies the PC way
    Flg_on((int)g_ScenarioFlags, 0x72);         // 0x06056404(0x002F8700, 0x72)

    sb(e, 0x16D) = 0;
    sb(e, 0x16E) = 0;
    sb(e, 0x16F) = 3;
    sb(e, 0x178) = 3;
    sb(e, 0x179) = 0;
    sb(e, 0x17F) = 0;
    sb(e, 0x180) = 0;
    sb(e, 0x181) = 0xD2;
    sb(e, 0x16C) = 0;
    sb(e, 0x17E) = 0;
    sb(e, 0x182) = 0;
    sb(e, 0x183) = 0;
    sb(e, 0x179) |= 2;                          // look-at live
    e->lookAtFlags = 1;
    Entity* heart = tyrant_heart(e);
    if (heart != NULL) heart->status_flags = 1;

    e->scaMatrixData.localMatrix.t[0] = 0x1C00;
    e->scaMatrixData.localMatrix.t[1] = 0;
    e->scaMatrixData.localMatrix.t[2] = 0x6600;
    e->angle = 0x400;

    e->animation_frame_id = 0;
    e->timing_control = 0;
    e->blend_counter = 0;
    e->animationId = 6;                         // the PC's break-out animation
    set_state_bytes(e, 5, 0, 0, 0);             // state 5: the Saturn drives it

    // Skip the pounding: run the animation straight to frame 0x55, the shatter.
    // The bound is port-only; the Saturn loops until the frame gets there.
    for (int guard = 0; e->animation_frame_id < 0x55 && guard < 0x100; ++guard) {
        Joint_move(0, e->animHeader, e->animBase, 0x80);
    }

    sb(e, 0x185) = 3;
    g_playerPosScratch.x = 0x2000;
    g_playerPosScratch.y = -0x1004;
    g_playerPosScratch.z = 0x6000;
    play_sound_and_voice_effect(1, 0xB3);       // the PC's shatter line, VB00_10
    g_main_state_flags |= MSF_VOICE_PLAYING;
}

// 0x06078C86 - step 3: the break-out, the PC's em_scd_behavior_break_glass
// from frame 0x55 on.
void saturn_release_breakout(Entity* e)
{
    sb(e, 0x185) = (unsigned char)(sb(e, 0x185) +
        (char)Joint_move(0, e->animHeader, e->animBase, 0x80));

    if (e->animation_frame_id == 0x59) {
        for (const SaturnShard& s : kCapsule2Shards) {
            g_animFrameIdSave = (unsigned int)((rand() & 0x1ff) + s.rotBase);
            g_playerPosScratch.y = -0xa28 - (rand() & 0xfff);
            g_playerPosScratch.x = (rand() & s.xzMask) + s.xBase;
            g_playerPosScratch.z = (rand() & s.xzMask) + s.zBase;
            Effect_CreateBillboard(0x15, s.variant, (short)g_animFrameIdSave,
                                   (void*)(uintptr_t)g_deadMoveValue,
                                   &g_playerPosScratch, (char)s.life);
        }
    }
    if (e->animation_frame_id == 0x91) sb(e, 0x179) |= 1;              // shadow on
    if (e->animation_frame_id == 0x97 || e->animation_frame_id == 0x8F) Snd_em(0);
}

void set_camera(unsigned char cam)
{
    g_roomCameraId = cam;
    g_main_state_flags |= MSF_CAMERA_REDRAW;
}

// 0x06078954 - the cutscene, stepped by +0x185 with +0x188 as its timer.
void saturn_release_run(Entity* e)
{
    switch (sb(e, 0x185)) {
    case 0:                                     // 0x060789A4
        set_camera(5);
        g_message_flags &= ~0x100;              // player input off
        sb(e, 0x186) |= 0x10;
        sb(e, 0x185) = 1;
        sb(e, 0x188) = 0;
        return;

    case 1: {                                   // 0x060789EC
        const unsigned char t = sb(e, 0x188);
        if (t == 3) {
            g_main_state_flags |= MSF_INTENSITY_RAMP;          // letterbox in
        } else if (t == 4 || t == 0x30) {
            g_playerPosScratch.x = 0x1B58;
            g_playerPosScratch.y = -0xFA0;
            g_playerPosScratch.z = 0x3E80;
            Play3DSnd(2, 0x19, 0, (int)&g_playerPosScratch);   // glass hit
        } else if (t == 8) {
            // Chris turns to face the capsule: player SCD behaviour 6.
            g_playerEntity.unk_c6 = 0x3000;
            g_playerEntity.unk_c8 = 0x7000;
            g_playerEntity.unk_de = 0x80;
            g_playerEntity.animationId = 8;
            g_playerEntity.animFrameId = 0;
            g_playerEntity.action_behavior = 6;
            g_playerEntity.action_state = 0;
        } else if (t == 0x40) {
            sb(e, 0x185) = 2;
            saturn_bgm_stop(0);
            saturn_bgm_stop(1);
            saturn_bgm_play(2);                 // Bgm_24 again
            set_camera(0);
        }
        sb(e, 0x188)++;
        return;
    }

    case 2:
        saturn_release_reraise(e);
        // fall through
    case 3:
        saturn_release_breakout(e);
        return;

    case 4:                                     // 0x06079D36
        e->animation_frame_id = 0;
        e->timing_control = 0;
        e->animationId = 0;
        sw(e, 0xC4) = 0;
        e->blend_counter = 0;
        set_state_bytes(e, 1, 0, 0, 1);         // state 1: the ordinary AI
        sb(e, 0x188) = 0x40;
        sb(e, 0x185) = 5;
        return;

    case 5:                                     // 0x06079D80
        if (--sb(e, 0x188) != 0) return;
        set_camera(1);
        sb(e, 0x188) = 0x80;
        sb(e, 0x185) = 6;
        return;

    case 6:                                     // 0x06079DB4
        if (--sb(e, 0x188) != 0) return;
        set_camera(2);
        sb(e, 0x185) = 7;
        return;

    case 7:                                     // 0x06079E1C
        g_main_state_flags &= ~MSF_INTENSITY_RAMP;             // letterbox out
        g_message_flags |= 0x100;                              // input back
        sb(e, 0x184) = 0;
        sb(e, 0x185) = 0;
        sb(e, 0x186) &= 0xFB;                                  // done
        return;

    default:
        return;
    }
}

} // namespace

// ===========================================================================
// Hooks
// ===========================================================================

void saturn_tyrant_init(void)
{
    Entity* e = ENTITY;
    sb(e, 0x184) = 0;                           // 0x060745E4..0x060745F6
    sb(e, 0x185) = 0;
    sb(e, 0x186) = 0;
    // 0x060746A2: player id bit 0 (Jill) -> the PC's death, no second Tyrant.
    if ((g_playerEntity.id & 1) != 0) sb(e, 0x186) |= 8;
}

void saturn_tyrant_battle_init(void)
{
    if (!g_saturnBattleActive) return;
    Entity* e = ENTITY;
    e->health = 0x02BC;                         // 0x06079A22: 700
    e->behavior_flags = 0x10;                   // 0x06079A2A
    e->scaMatrixData.localMatrix.t[1] = 0;      // 0x06079A32: +0x38 = 0
    e->state = 1;                               // 0x06079A36: state word
    e->ignore_player_flag = 0;                  //   0x01000100 at +0x84
    e->action_behavior = 1;
    e->action_state = 0;
    sb(e, 0x179) |= 3;                          // 0x06079A38-0x06079A42
    sb(e, 0x186) |= 8;                          // 0x06079A44-0x06079A50
}

void saturn_tyrant_battle_update(void)
{
    if (!g_saturnBattleActive) return;
    ENTITY->behavior_flags &= 0x7F;
}

void saturn_tyrant_forced_select(void)
{
    // 0x06074C9C: state word 0x03010200 (behaviour 2), then id 0x10 -> 1 (the
    // caller keeps the PC's), else +0x186 bit 0x08 -> 0.
    Entity* e = ENTITY;
    e->action_behavior = (sb(e, 0x186) & 8) != 0 ? 0 : SATURN_TYRANT_BEHAVIOR_DISSOLVE;
}

int saturn_tyrant_forced_run(void)
{
    if (ENTITY->id != ENEMY_TYRANT_1 ||
        ENTITY->action_behavior != SATURN_TYRANT_BEHAVIOR_DISSOLVE) {
        return 0;
    }
    saturn_tyrant_behavior_dissolve();
    return 1;
}

void saturn_tyrant_update_tail(void)
{
    Entity* e = ENTITY;
    if (e->id != ENEMY_TYRANT_1 || (sb(e, 0x186) & 4) == 0) return;
    if (sb(e, 0x184) == 0) saturn_release_watch(e);
    else                   saturn_release_run(e);
}

// ===========================================================================
// 0x0605CB38 - the second capsule's collision cylinder.
// ===========================================================================
#define SATURN_CAPSULE2_X 0x1BD0
#define SATURN_CAPSULE2_Z 0x7400
#define SATURN_CAPSULE2_R 0x923

void saturn_capsule_collision(int* t)
{
    // Gated on stage byte 4 / room 0x13 (or Battle arena 15, not ported).
    if (g_stageId != STAGE_LABORATORY || g_roomId != ROOM_MAIN_LAB) return;

    const int dx = t[0] - SATURN_CAPSULE2_X;
    const int dz = t[2] - SATURN_CAPSULE2_Z;
    const int dist = SquareRoot0(dx * dx + dz * dz);   // 0x0605A1E4
    if (dist > SATURN_CAPSULE2_R) return;
    if (t[1] < 0) return;                              // only on the floor (y >= 0)

    // Back onto the rim along the centre -> entity direction. The Saturn goes
    // through atan2/sin/cos (0x06083BB0, 0x06085DCC, 0x06085DF0) and a /28 of
    // the 16.16 sine, i.e. a radius of 65536/28 = 2340; the direct form below
    // lands on the same point to within a unit.
    if (dist == 0) {
        t[0] = SATURN_CAPSULE2_X + SATURN_CAPSULE2_R;
        return;
    }
    t[0] = SATURN_CAPSULE2_X + dx * SATURN_CAPSULE2_R / dist;
    t[2] = SATURN_CAPSULE2_Z + dz * SATURN_CAPSULE2_R / dist;
}

int saturn_game_loop_update(void)
{
    // The Saturn's player update calls the capsule push first thing
    // (0x0602FF52), before the player's state machine moves him; the enemies
    // get the same push in update_entities.
    saturn_capsule_collision(g_playerEntity.scaMatrixData.localMatrix.t);

    // The Battle Game's arena sequence. The run clock is main_loop()'s.
    if (g_saturnBattleActive) {
        saturn_battle_update();
        // The end sequence is over: leave the run the way a death does;
        // game_start then chains saturn_ranking_state (GAME5 raises bits 0-1
        // of g_SystemFlags here and main hands over to RANKING.PRG).
        if (saturn_battle_run_finished()) {
            return 1;
        }
        saturn_battle_draw_hud();
    }
    return 0;
}

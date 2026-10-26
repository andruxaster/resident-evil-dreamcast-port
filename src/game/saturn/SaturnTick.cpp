// SaturnTick.cpp - the Sega Saturn's Tick. See SaturnTick.h for the overview.
//
// Ported from GAME2.PRG (the module the Saturn loads for stage byte 2, loaded
// at 0x0602B000). Every address in this file is a GAME2 address unless it says
// otherwise; the PC names are given beside them. The Saturn's own Entity layout
// differs from the PC's after +0x16C, so the offsets the decompile shows are
// NOT the PC's; where a Saturn field is the same thing as a PC Hunter field the
// PC macro is used, and the mapping is written at the use.
//
//   Saturn   PC                       what
//   +0x16C   H_PATH_LATCH   +0x170    partner / path latch (word)
//   +0x170   H_PARTNER      +0x174    the other hunter (pointer)
//   +0x17C   H_ROOM_HIT     +0x180    touched-a-wall byte
//   +0x17D   H_POUNCE_LATCH +0x182    pounce roll
//   +0x17A   H_STEP_WORD    +0x17E    wander-turn step word
//   +0x17C.. wander control +0x180    (entity_update_wander_turn's 2nd argument)
//   +0x184   H_DEATH_CNT_B  +0x184    damage latch B
//   +0x186   -- side table  armScale  the arm's Y scale, 4.12
//   +0x188   -- side table  slot      the animator's slot (0xFF = none)
//   +0x189   -- side table  stab      the stab's sub-state
//
// The three Saturn-only values are in a side table because the PC Entity has no
// free bytes (see SaturnTick.h).
#include "SaturnTick.h"
#include "SaturnModels.h"

#include "../../Globals.h"
#include "../BioCard.h"
#include "../Types.h"
#include "../entities/EntityCommon.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

// Shared entity scratch, defined in EntityCommon.cpp (as Hunter.cpp declares).
extern int player_distance_z;                                      // 0x00be0de4
extern int g_scaled_down_dist;                                     // 0x00be0de8

// ---- Hunter fields, by PC offset (the same macros entities/Hunter.cpp uses) --

#define T_STATE_BLOCK   (*(unsigned int*)  ((char*)ENTITY + 0x84))
#define T_SPEED_W       (*(unsigned short*)((char*)ENTITY + 0xC2))
#define T_TICKS         (*(short*)         ((char*)ENTITY + 0xC4))
#define T_POS_T         ((int*)            ((char*)ENTITY + 0x34))
#define T_PATH_LATCH    (*(short*)         ((char*)ENTITY + 0x170))
#define T_JOINT_SEL     (*(unsigned short*)((char*)ENTITY + 0x17C))
#define T_STEP_WORD     (*(unsigned short*)((char*)ENTITY + 0x17E))
#define T_ROOM_HIT      (*(unsigned char*) ((char*)ENTITY + 0x180))
#define T_POUNCE_LATCH  (*(unsigned char*) ((char*)ENTITY + 0x182))
#define T_DEATH_CNT_B   (*(unsigned char*) ((char*)ENTITY + 0x184))
#define T_JOINTS        ((JointStruct*)    ENTITY->jointsStructs)

#define T_PLAYER_T      ((VECTOR*) g_playerEntity.scaMatrixData.localMatrix.t)
#define T_PLAYER_T_INT  ((int*)    g_playerEntity.scaMatrixData.localMatrix.t)
#define T_DMV           ((const char*)g_deadMoveValue)

// ============================================================================
// The side table: one record per Tick.
// ============================================================================

// 0x0609D5F4: four 0x20-byte slots, key-index bytes 0..0x1D and an in-use flag
// at +0x1F. The port's record carries the same fields plus the arm.
struct TickState {
    const Entity* owner;                       // NULL = free
    unsigned char keyIdx[SATURN_TICK_JOINTS];  // slot +0x00..: current key per joint
    unsigned char rootIdx;                     // slot +0x1E:   current root key
    short         armScale;                    // Entity +0x186, 4.12 (0x1000 = 1.0)
    unsigned char stab;                        // Entity +0x189
    MATRIX        unscaled[2];                 // joints 4 and 5, world before mesh scale
    unsigned char unscaledValid;               // bit 0 = joint 4, bit 1 = joint 5
};

static TickState s_tick[SATURN_TICK_SLOTS];
// Where a Tick lands when the table is full. The Saturn would index past its
// four slots with 0xFF; a shared spare keeps the same "works, badly" outcome
// without leaving the table.
static TickState s_tickSpare;

static TickState* tick_find(const Entity* ent)
{
    for (int i = 0; i < SATURN_TICK_SLOTS; i++) {
        if (s_tick[i].owner == ent) return &s_tick[i];
    }
    return NULL;
}

// 0x060705C4 - first free slot, marked in use. Returns the spare when full.
static TickState* tick_alloc(const Entity* ent)
{
    TickState* st = tick_find(ent);
    if (st == NULL) {
        for (int i = 0; i < SATURN_TICK_SLOTS; i++) {
            if (s_tick[i].owner == NULL) { st = &s_tick[i]; break; }
        }
    }
    if (st == NULL) st = &s_tickSpare;
    memset(st, 0, sizeof(*st));
    st->owner = ent;
    st->armScale = 0x1000;
    return st;
}

// The current Hunter's record, allocating one on first use (the Saturn
// allocates in the state init, which the port reaches first, but a Tick that
// somehow runs a behaviour before it - a loaded save - must still work).
static TickState* tick_state(void)
{
    TickState* st = tick_find(ENTITY);
    if (st == NULL) st = tick_alloc(ENTITY);
    return st;
}

int saturn_tick_active(void)
{
    return g_bSaturnMode && g_stageId == SATURN_TICK_STAGE &&
           ENTITY != NULL && ENTITY->id == SATURN_TICK_ENTITY_ID;
}

// 0x060705A8 - zero the whole slot table.
void saturn_tick_room_reset(void)
{
    memset(s_tick, 0, sizeof(s_tick));
    memset(&s_tickSpare, 0, sizeof(s_tickSpare));
}

// ============================================================================
// The Tick's rotation convention
// ============================================================================

// GAME2 has two part-draw routines, and the entity loop (0x0602F2E4) picks
// the Tick's (0x06070794 -> 0x060708B0) only for type 6 with animationId bit
// 0x80 set, i.e. only while one of these keyframe animations plays:
//   generic 0x060381A8   translate, Rx(+4), Ry(+6), Rz(+8)  - PS1 EMD data
//   Tick    0x060708B0   translate, Rz(+8), Ry(+6), Rx(+4)  - the Tick's keys
// (0x06082C94 = Rx, 0x06082C1C = Ry, 0x06086E6C = Rz; each post-multiplies the
// SGL matrix by the standard right-handed rotation, 4096 = a full turn.) Both
// read the same three part fields: Joint_move (0x06039C68) and the animator
// (0x06070604) store x/y/z into part +4/+6/+8 unchanged.
//
// The generic order is the PS1 GTE's, and it is what the PC draws for the
// Hunter's PS1 data - but the PC keeps its matrices Y-flipped: RotMatrix
// stores Rx(-x) Ry(y) Rz(-z) = D * Rx(x)Ry(y)Rz(z) * D with D = diag(1,-1,1),
// and CompMatrix/ApplyMatrixLV negate Y on the way in and out, so what is drawn
// is D * stored * D. The Tick's matrix therefore has to be STORED as
// D * Rz(z)Ry(y)Rx(x) * D. Storing Rz Ry Rx itself (the first try at this) drew
// every joint mirrored through D: the walk leaned back with the legs splayed
// and the stab thrust off to the side.
//
// The joints keep PC-convention angles in Joint.rotation and get their matrix
// from the ordinary RotMatrix: Joint_move blends from the CURRENT angles when a
// behaviour hands back to an EMD animation, and blending angles of two Euler
// conventions would glitch for those few frames. So the key is turned into the
// PC angles that store the right matrix: RotMatrix(X, Y, Z) = D Rx(X)Ry(Y)Rz(Z) D,
// so with Rz(z)Ry(y)Rx(x) = Rx(a) Ry(b) Rz(c) the PC's angles are (a, b, c).
// Rx(a)Ry(b)Rz(c) has M[0][2] = sin b, M[0][0] = cos b cos c,
// M[0][1] = -cos b sin c, M[1][2] = -sin a cos b, M[2][2] = cos a cos b.
// Round-trip error is the 4096-step quantisation.
static SVECTOR tick_key_to_pc_angles(const SaturnTickKey& k)
{
    const double r = 3.14159265358979323846 * 2.0 / 4096.0;
    const double cx = cos(k.x * r), sx = sin(k.x * r);
    const double cy = cos(k.y * r), sy = sin(k.y * r);
    const double cz = cos(k.z * r), sz = sin(k.z * r);

    // M = Rz * Ry * Rx, written out
    double M[3][3];
    M[0][0] = cz * cy;
    M[0][1] = -sz * cx + cz * sy * sx;
    M[0][2] = sz * sx + cz * sy * cx;
    M[1][0] = sz * cy;
    M[1][1] = cz * cx + sz * sy * sx;
    M[1][2] = sz * sy * cx - cz * sx;
    M[2][0] = -sy;
    M[2][1] = cy * sx;
    M[2][2] = cy * cx;

    SVECTOR out;
    out.pad = 0;
    const double sb = M[0][2] > 1.0 ? 1.0 : (M[0][2] < -1.0 ? -1.0 : M[0][2]);
    const double b = asin(sb);
    if (fabs(cos(b)) < 1e-4) {
        // Gimbal: no unique decomposition. None of the Tick's 2262 keys is near
        // it (smallest cos b is 0.34; tools/verify_saturn_tick.py checks), so
        // this is unreachable with the shipped data; keep the raw angles.
        out.x = k.x;
        out.y = k.y;
        out.z = k.z;
        return out;
    }
    const double c = atan2(-M[0][1], M[0][0]);
    const double a = atan2(-M[1][2], M[2][2]);
    const double q = 4096.0 / (3.14159265358979323846 * 2.0);
    const long ix = lround(a * q), iy = lround(b * q), iz = lround(c * q);
    out.x = (short)(((ix + 2048) & 0xFFF) - 2048);
    out.y = (short)(((iy + 2048) & 0xFFF) - 2048);
    out.z = (short)(((iz + 2048) & 0xFFF) - 2048);
    return out;
}

// ============================================================================
// The animator (0x06070604)
// ============================================================================

// Plays one frame of Tick animation `animId` (0x80 walk, 0x81 wind-up, 0x82
// retract) into the entity's joints: the root translation and each joint's
// rotation, then the frame counter. Returns 1 when the animation wrapped.
//
// The keys are HELD, not interpolated: a joint keeps its current record until
// the next record's time is reached, then takes that one. The record index is
// advanced at most once per call, so a time that was skipped catches up one
// frame at a time, as on the Saturn. Frame 0 resets every index.
//
// Joint_move (PlayerAnimations.cpp) is the same job against the EMD's frames;
// the stores are the same ones, the rotation then RotMatrix into the transform
// and the root's translation straight into transform.t - except that the keys
// are first turned from the Saturn's angle convention into the PC's.
static int tick_anim_step(TickState* st, unsigned char animId)
{
    Entity* e = ENTITY;
    const SaturnTickAnim& anim = g_saturnTickAnims[(animId & 0xF) % SATURN_TICK_ANIMS];
    const unsigned char frame = e->animation_frame_id;

    // 0x06070612: FUN_0603ee20(slot, 0, 0x1f) - a new animation starts clean.
    if (frame == 0) {
        memset(st->keyIdx, 0, sizeof(st->keyIdx));
        st->rootIdx = 0;
    }

    JointStruct* joints = e->jointsStructs;

    {
        const SaturnTickKey* keys = &g_saturnTickKeys[anim.root];
        unsigned int idx = st->rootIdx;
        if (keys[idx].time != 0xFFFE && keys[idx + 1].time <= frame) idx++;
        st->rootIdx = (unsigned char)idx;
        joints[0].transform.t[0] = keys[idx].x;
        joints[0].transform.t[1] = keys[idx].y;
        joints[0].transform.t[2] = keys[idx].z;
    }

    unsigned int count = e->jointCount;
    if (count > SATURN_TICK_JOINTS) count = SATURN_TICK_JOINTS;
    for (unsigned int j = 0; j < count; j++) {
        const SaturnTickKey* keys = &g_saturnTickKeys[anim.joint[j]];
        unsigned int idx = st->keyIdx[j];
        if (keys[idx].time != 0xFFFE && keys[idx + 1].time <= frame) idx++;
        st->keyIdx[j] = (unsigned char)idx;

        if ((joints[j].flags & 0x10) == 0) {
            joints[j].rotation = tick_key_to_pc_angles(keys[idx]);
            RotMatrix(&joints[j].rotation, &joints[j].transform);
        }
    }

    const unsigned char next = (unsigned char)(frame + 1);
    e->animation_frame_id = next;
    if (anim.frames < next) {
        e->animation_frame_id = 0;
        return 1;
    }
    return 0;
}

// ============================================================================
// The arm's scale
// ============================================================================

// The three arm parts' Y scale is armScale << 4 (16.16) on the Saturn
// (0x0606C824 tail, 0x0606DF24 every frame); the 4.12 value in the record is
// what rotate_entity's hook reads, so storing it IS the Saturn's per-frame write.

static int tick_is_arm_joint(unsigned char joint)
{
    return joint >= SATURN_TICK_ARM_FIRST && joint <= SATURN_TICK_ARM_LAST;
}

int saturn_tick_joint_scaled(const Entity* ent, unsigned char joint)
{
    if (!tick_is_arm_joint(joint)) return 0;
    if (ent == NULL || ent->id != SATURN_TICK_ENTITY_ID) return 0;
    if (g_stageId != SATURN_TICK_STAGE) return 0;
    const TickState* st = tick_find(ent);
    return st != NULL && st->armScale != 0x1000;
}

// Flag 0x100 (parts 5 and 6): the part's own offset from its parent is scaled
// by the part's scale vector, which is (1, s, 1).
void saturn_tick_scale_local(const Entity* ent, unsigned char joint, MATRIX* local)
{
    if (joint != 5 && joint != 6) return;
    const TickState* st = tick_find(ent);
    if (st == NULL) return;
    local->t[1] = (int)(((long long)local->t[1] * st->armScale) >> 12);
}

// Flag 0x20 (parts 4 and 5): the MESH is scaled along its own Y. What the
// children and hit tests read - the saved world matrix - stays unscaled, so
// the unscaled one is kept for them and the one the mesh is drawn with is the
// scaled copy.
void saturn_tick_scale_mesh(const Entity* ent, unsigned char joint, MATRIX* world)
{
    if (joint != 4 && joint != 5) return;
    TickState* st = tick_find(ent);
    if (st == NULL) return;
    const int k = joint - 4;
    st->unscaled[k] = *world;
    st->unscaledValid |= (unsigned char)(1u << k);

    VECTOR s;
    s.x = 0x1000;
    s.y = st->armScale;
    s.z = 0x1000;
    s.pad = 0;
    ScaleMatrixCols(world, &s);
}

const MATRIX* saturn_tick_child_parent(const Entity* ent, unsigned char joint,
                                       const MATRIX* world)
{
    if (joint != 4 && joint != 5) return world;
    const TickState* st = tick_find(ent);
    if (st == NULL) return world;
    const int k = joint - 4;
    return (st->unscaledValid & (1u << k)) ? &st->unscaled[k] : world;
}

// ============================================================================
// State init (0x0606C824)
// ============================================================================

// Shared by the port's tail of hunter_state_init: the Saturn allocates the
// animator slot first and installs the arm parts' flags last. The flags are the
// rotate_entity hook; the slot is the record.
void saturn_tick_state_init(void)
{
    tick_alloc(ENTITY);                  // armScale starts at 1.0
}

// ============================================================================
// Variant roll (0x0606D074)
// ============================================================================

// The Saturn tests the PLAYER's being-attacked flag after the coward branch.
// 0x55 = the Tick holds: state 1, ignore 1, behaviour 1, action 0.
int saturn_tick_variant_hold(void)
{
    if (g_playerEntity.isBeingAttackedFlag != 0x55) return 0;
    T_STATE_BLOCK = 0x00010101;   // Saturn 0x01010100, big-endian
    return 1;
}

// ============================================================================
// Variant 0, the leap tail (0x0606D10C)
// ============================================================================

// Where the Hunter rolls a leap target (hunter_roll_leap), the Tick takes one
// movement step and starts the stab: ignore 1, behaviour 5, action 0, stab
// sub-state 0 (Entity +0x189). The target the Hunter stores is not used - the
// Tick's behaviour 5 is the stab, which aims at the player each frame.
void saturn_tick_variant_0_leap(void)
{
    Add_speedXZ(0);                                  // FUN_06059360(0)
    ENTITY->ignore_player_flag = 1;
    ENTITY->action_behavior = 5;
    ENTITY->action_state = 0;
    tick_state()->stab = 0;
}

// ============================================================================
// Variant 2 (0x0606D484)
// ============================================================================

// 0x0609B8FC - the Tick rolls its pounce against the Hunter's "low health"
// table only (the Hunter's mid table, and its health gates, are not used).
static const unsigned char tick_pounce_roll_tbl[16] = {
    0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1
};

void saturn_tick_variant_2_ai(void)
{
    // Manhattan distance to the player.
    {
        int dz = T_PLAYER_T_INT[2] - T_POS_T[2];
        int dx = T_PLAYER_T_INT[0] - T_POS_T[0];
        g_playerDisplacement = (dz < 0 ? -dz : dz) + (dx < 0 ? -dx : dx);
    }
    g_scaled_down_dist = ENTITY->action_behavior;
    T_POUNCE_LATCH = 0;

    // A probe two units wide, 0x800 ahead of where the Tick is facing
    // (FUN_0604D4E0 rotates the vector by angle + 0xA0; FUN_0604CBC8 asks the
    // room's boundaries whether the point is fully blocked).
    g_svecScratch.x = 0x800;
    g_svecScratch.y = 0;
    g_svecScratch.z = 0;
    g_matrixScratch = g_identityMatrixData;
    RotMatrixY((int)(short)ENTITY->angle + 0xA0, &g_matrixScratch);
    ApplyMatrixSV(&g_matrixScratch, &g_svecScratch, &g_svecScratch);
    short blocked = room_collision_check_radius((VECTOR*)T_POS_T, (VECTOR*)&g_svecScratch, 2);

    // The pounce roll runs only while the player is hurt: under a third of
    // their maximum health (the Hunter's gate is a per-character constant).
    T_POUNCE_LATCH = 0;
    if (g_playerEntity.health > 0 &&
        g_playerEntity.health < (short)(unsigned short)(g_playerEntity.maxHealth / 3)) {
        T_POUNCE_LATCH = tick_pounce_roll_tbl[rand() & 0xF];
        if (g_playerDisplacement > 4000 || g_playerEntity.isBeingAttackedFlag != 0 ||
            blocked == 1) {
            T_POUNCE_LATCH = 0;
        }
    }
    if (T_DEATH_CNT_B != 0) {
        T_POUNCE_LATCH = 0;
    }

    // Partner latched and the player within 6000: close in (behaviour 2).
    if (T_PATH_LATCH != 0 && g_playerDisplacement < 6000) {
        if (g_scaled_down_dist != 2) {
            ENTITY->action_state = 0;
            ENTITY->blend_counter = 7;
        }
        ENTITY->action_behavior = 2;
        ENTITY->ignore_player_flag = 0;
    }

    // Aimed, close and rolled: the stab.
    if ((short)turn_toward_target(T_PLAYER_T, 0x20) == 0 &&
        g_playerDisplacement < 4000 && T_POUNCE_LATCH != 0) {
        saturn_tick_variant_0_leap();
    }

    // The player is held (flag bit 0x80) and close: the howl (behaviour 9).
    if ((g_playerEntity.isBeingAttackedFlag & 0x80) != 0 && g_playerDisplacement < 3000) {
        ENTITY->ignore_player_flag = 1;
        ENTITY->action_behavior = 9;
        ENTITY->action_state = 0;
    }

    if (g_playerEntity.isBeingAttackedFlag == 0) {
        // In sight and close and aimed: the swipe (behaviour 4).
        if (check_line_of_sight(T_PLAYER_T) == 0 && g_playerDisplacement < 2000 &&
            (short)turn_toward_target(T_PLAYER_T, 0x80) == 0) {
            ENTITY->ignore_player_flag = 1;
            ENTITY->action_behavior = 4;
            ENTITY->action_state = 0;
        } else if (T_PATH_LATCH != 0 && (ENTITY->behavior_flags & 0xF) != 7 &&
                   g_playerDisplacement > 0x1900 &&
                   (short)turn_toward_target(T_PLAYER_T, 0x20) == 0 &&
                   (g_playerEntity.action_behavior == 0x12 ||
                    (g_playerEntity.action_behavior == 0x13 && (g_RandSeed & 1) != 0)) &&
                   (char)is_facing_toward_entity(&g_playerEntity) == 0) {
            // The player is running at the Tick: the dodge (behaviour 6).
            ENTITY->ignore_player_flag = 1;
            ENTITY->action_behavior = 6;
            ENTITY->action_state = 0;
            ENTITY->status_flags &= 0x1F;
        }
    }
}

// ============================================================================
// Chase, behaviour 1 (0x0606D998)
// ============================================================================

// The Hunter's chase (hunter_behavior_chase) with the Tick's walk: animation
// 0x80 from the keyframe tables instead of the EMD's 0x10, and the arm's scale
// reset on the first frame. The wander steering, the footsteps and the homing
// inside 2000 are the Hunter's.
void saturn_tick_behavior_chase(void)
{
    TickState* st = tick_state();

    ENTITY->animationId = 0x80;
    T_SPEED_W = 0x28;
    g_animFrameIdSave = 6;

    g_playerPosScratch.x = T_PLAYER_T_INT[0];
    g_playerPosScratch.y = T_PLAYER_T_INT[1];
    g_playerPosScratch.z = T_PLAYER_T_INT[2];
    g_playerPosScratch.pad = (int)((short)g_playerEntity.scaMatrixData.worldMatrix.m[0][0] |
                                   ((short)g_playerEntity.scaMatrixData.worldMatrix.m[0][1] << 16));

    entity_update_wander_turn(T_STEP_WORD, (unsigned char*)((char*)ENTITY + 0x180),
                              &ENTITY->angle_turn_delta, 0x18, 0x3C);

    if (ENTITY->action_state == 0) {
        ENTITY->action_state = 1;
        ENTITY->animation_frame_id = 0;
        ENTITY->timing_control = 0;
        ENTITY->blend_counter = 3;
        st->armScale = 0x1000;
        T_JOINT_SEL = 0;
    }
    if (ENTITY->animation_frame_id == 1)  Snd_em(0);
    if (ENTITY->animation_frame_id == 32) Snd_em(0);

    int dz = T_PLAYER_T_INT[2] - T_POS_T[2];
    int dx = T_PLAYER_T_INT[0] - T_POS_T[0];
    g_playerDisplacement = (dz < 0 ? -dz : dz) + (dx < 0 ? -dx : dx);
    if (g_playerDisplacement < 2000) {
        entity_rotate_toward_target(T_PLAYER_T, 0x18);
    }

    tick_anim_step(st, ENTITY->animationId);
    Add_speedXZ(0);
}

// ============================================================================
// Pounce, behaviour 5: the stab (0x0606DE98 / 0x0606DF24)
// ============================================================================

// 0x0609... - the Saturn seeds the effect and hit-test vectors from the same
// dead-move block the Hunter does.
static void tick_seed_from_dead_move(void)
{
    const int* seed = (const int*)(T_DMV + 0x14);
    g_playerPosScratch.x   = seed[0];
    g_playerPosScratch.y   = seed[1];
    g_playerPosScratch.z   = seed[2];
    g_playerPosScratch.pad = seed[3];
}

// The arm's tip is joint 6.
static MATRIX* tick_tip(void)
{
    return &T_JOINTS[SATURN_TICK_ARM_LAST].world;
}

// 0x0606DF24. Sub-state in the side table (Entity +0x189):
//   0  wind-up: animation 0x81, the howl, the arm back to 1.0
//   1  play 0x81; fall into 2 once it ends or passes frame 7
//   2  the arm extends: scale eases toward 4.5, the tip is tested against the
//      room (stuck in the floor) and the player (hit)
//   3  the arm retracts at 3276.8 per frame
//   4  play the retract animation 0x82
//   5  done: back to the AI layer (state 1, ignore 0, behaviour 1)
static void tick_stab(TickState* st)
{
    Entity* e = ENTITY;
    int sub = st->stab;

    if (sub == 0) {
        e->animation_frame_id = 0;
        e->timing_control = 0;
        e->animationId = 0x81;
        T_TICKS = 0;
        Snd_em(2);
        st->armScale = 0x1000;
        st->stab = 1;
        e->angle = (short)(e->angle + 0xA0);
    } else {
        if (sub == 1) {
            st->stab = (unsigned char)(st->stab + tick_anim_step(st, e->animationId));
            // Still winding up: hold in state 1 while the frame is 1..7.
            if (e->animation_frame_id != 0 && e->animation_frame_id < 8) goto done;
            sub = 2;                       // fall through
        }

        if (sub == 2) {
            if (st->armScale < 0x4000) {
                // Is the tip in a wall or the floor?
                g_svecScratch.x = 0;
                g_svecScratch.y = 0;
                g_svecScratch.z = 0;
                if (room_collision_check_radius((VECTOR*)tick_tip()->t,
                                                (VECTOR*)&g_svecScratch, 0) == 1) {
                    g_playerPosScratch.x = 0;
                    g_playerPosScratch.y = 700;
                    g_playerPosScratch.z = 0;
                    g_playerPosScratch.pad = 0;
                    Effect_CreateBillboard(8, 8, 0, tick_tip(), &g_playerPosScratch, 0);
                    Snd_em(8);
                    st->stab = 3;
                }

                // Is the tip on the player?
                tick_seed_from_dead_move();
                player_distance_z = FUN_0048ae00(tick_tip(), &g_playerPosScratch, 800,
                                                 T_PLAYER_T_INT);
                if (player_distance_z != 0 &&
                    (double)st->armScale <= 16384.0 &&
                    g_playerEntity.isBeingAttackedFlag == 0) {
                    g_playerEntity.isBeingAttackedFlag = 1;
                    // Saturn: the player's +0x84 word = 0x07060000, i.e. the
                    // bitten pose (animation 7, frame 6) the Hunter's pounce
                    // uses, and the Tick's behaviour word = 0x0700 (hold).
                    g_playerEntity.animationId = 7;
                    g_playerEntity.animFrameId = 6;
                    g_playerEntity.action_behavior = 0;
                    g_playerEntity.action_state = 0;
                    e->action_behavior = 7;
                    e->action_state = 0;

                    tick_seed_from_dead_move();
                    g_playerPosScratch.x = 200;
                    Effect_CreateBillboard(0, 3, 0, tick_tip(), &g_playerPosScratch, 0);
                    JointApplyColorTint(&T_JOINTS[SATURN_TICK_ARM_LAST], 0x30, 0x808C0,
                                        (void*)0x606060);
                    Snd_em(3);

                    // Face the player so the hold animation lines up.
                    const short ang = e->angle;
                    if ((unsigned short)(((g_playerEntity.directionAngle - ang) & 0xFFF) - 0x400) < 0x801) {
                        g_playerEntity.attackAnim = 1;
                        g_playerEntity.directionAngle = (short)(ang + 0x800);
                    } else {
                        g_playerEntity.attackAnim = 2;
                        g_playerEntity.directionAngle = ang;
                    }
                }
            } else {
                st->armScale = 0x4000;
                st->stab = 3;
            }
            // Ease toward 4.5 (0x4800) a quarter of the way per frame. The
            // Saturn does this in doubles and truncates to a short.
            const double s = (double)st->armScale;
            st->armScale = (short)(s + (18432.0 - s) / 4.0);
        } else if (sub == 3 || sub == 4) {
            if (sub == 3) {
                st->armScale = (short)((double)st->armScale - 3276.8);
                if (st->armScale > 0x1000) goto done;
                st->armScale = 0x1000;
                e->animation_frame_id = 0;
                e->timing_control = 0;
                e->animationId = 0x82;
                st->stab = 4;
            }
            st->stab = (unsigned char)(st->stab + tick_anim_step(st, e->animationId));
        } else if (sub == 5) {
            T_STATE_BLOCK = 0x00010001;    // Saturn 0x01000100: state 1, behaviour 1
        }
    }

done:
    return;     // the arm's scale is read straight from the record
}

// ============================================================================
// Hold and decapitation, behaviour 7 (0x0606E78C)
// ============================================================================

// 0x0609D5D4 - the player's pose relative to the arm tip, taken when the hold
// starts: its rotation is kept, its translation is pinned (see below).
static MATRIX s_holdBlock;

// 0x0603A644: out = inverse(A) * B for the rotation part. The Saturn builds the
// whole matrix, then the hold overwrites the translation, so only A^T * B is
// needed. 4.12 fixed point throughout.
static void tick_relative_rotation(const MATRIX* a, const MATRIX* b, MATRIX* out)
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            int sum = 0;
            for (int k = 0; k < 3; k++) sum += (int)a->m[k][i] * (int)b->m[k][j];
            out->m[i][j] = (short)(sum >> 12);
        }
    }
}

// The Tick does not use the Hunter's hold (hunter_behavior_grabhold). It carries
// the player on the arm and takes their head off:
//   0  take the player's rotation relative to the tip; pin the offset at
//      (-1700, -800, -1100) in the tip's frame; flag the player's matrix as
//      externally controlled (Entity +3 bit 0x80, which makes
//      EntityComputeJointWorldMatrices keep it) and clear their look-at
//   1  eight frames
//   2  the arm retracts (scale -= 1146.88 a frame) and the player rides it:
//      their matrix is tip * block every frame, a blood billboard every 4th
//      frame at joint 5; at 1.0 the player is let go, animation 0x83 is armed
//   3  eight frames, then animation 0x83 (49 frames): a billboard at the tip on
//      its frame 1, and on frame 10 the player's action_state becomes 4 - the
//      grabbed player's own state machine (player_anim_grabbed) takes it from there
//   4  back to the AI layer: state 1, ignore 1, behaviour 9 (the howl)
void saturn_tick_behavior_grabhold(void)
{
    TickState* st = tick_state();
    Entity* e = ENTITY;
    JointStruct* joints = T_JOINTS;
    unsigned char* playerFlags3 = (unsigned char*)&g_playerEntity + 3;

    switch (e->action_state) {
    case 0:
        e->action_state = 1;
        T_TICKS = 8;
        tick_relative_rotation(tick_tip(), &g_playerEntity.scaMatrixData.localMatrix,
                               &s_holdBlock);
        s_holdBlock.t[0] = -1700;     // 0xFFFFF95C
        s_holdBlock.t[1] = -800;      // 0xFFFFFCE0
        s_holdBlock.t[2] = -1100;     // 0xFFFFFBB4
        *playerFlags3 |= 0x80;
        g_playerEntity.lookAtFlags = 0;
        break;

    case 1: {
        const short n = T_TICKS;
        T_TICKS = (short)(n - 1);
        if (n < 1) e->action_state = 2;
        break;
    }

    case 2: {
        st->armScale = (short)((double)st->armScale - 1146.88);
        if (st->armScale < 0x1001) {
            st->armScale = 0x1000;
            e->animation_frame_id = 0;
            e->timing_control = 0;
            e->animationId = 0x83;
            e->action_state = 3;
            T_TICKS = 8;
            *playerFlags3 &= 0x7F;
        } else {
            const unsigned short c = (unsigned short)T_TICKS;
            T_TICKS = (short)(c + 1);
            if ((c & 3) == 0) {
                tick_seed_from_dead_move();
                Effect_CreateBillboard(0, 0, 0, &joints[5].world, &g_playerPosScratch, 0);
            }
            MATRIX placed;
            CompMatrix(tick_tip(), &s_holdBlock, &placed);
            g_playerEntity.scaMatrixData.localMatrix = placed;
        }
        break;
    }

    case 3:
        if (T_TICKS < 1) {
            e->action_state = (unsigned char)(e->action_state +
                                              tick_anim_step(st, e->animationId));
            if (e->animation_frame_id == 1) {
                tick_seed_from_dead_move();
                g_playerPosScratch.y = -600;
                Effect_CreateBillboard(0, 0, 0, tick_tip(), &g_playerPosScratch, 0);
            } else if (e->animation_frame_id == 10) {
                g_playerEntity.action_state = 4;
            }
        } else {
            T_TICKS = (short)(T_TICKS - 1);
        }
        break;

    case 4:
        T_STATE_BLOCK = 0x00090101;   // Saturn 0x01010900: state 1, ignore 1, behaviour 9
        break;

    default:
        break;
    }
}

// ============================================================================
// The player's "bitten" machine (0x0606FF78)
// ============================================================================

// The Saturn's version of g_playerAnimFunctions[44] for the Tick's stage. ENTITY
// is the player while it runs (the original reads the current-entity pointer
// for the frame counter). Sub-states, in Entity +0x87:
//   0  set up: frame 0, flags |= 6, counter +0xBC = 0xB4, message flag 0x40 off
//   1  the bitten animation (the player's own damage animations, not the
//      EMD's): impact sound and joint tints on frame 8, blood every other frame
//      before 9, a billboard after 0x5F; advances when the animation ends
//   2  face the other way (attackAnim 1 <-> 2) and restart the animation
//   3  the animation at half rate
//   4  the decapitation (the Tick's hold sets this on frame 10 of 0x83): the
//      head (joint 1) is hidden, the player is flagged 0x80, two blood
//      billboards, Snd_em(5)
//   5  the decapitation animation; blood every 4th frame from 9 to 0x2A, joint
//      tints on frame 3, a sound on 0x2A, and on frame 0x10 health becomes -1
//   6  throw the corpse: a push velocity of -900 along the facing
//   7  fade out after 0x40 frames, then the player's state becomes 4 (dead)
// States 0-3 are the hurt half the PC's version does not have (it sets
// health = -1 in its first state); 6 and 7 are its states 2 and 3.
int saturn_tick_player_bitten(void)
{
    if (!g_bSaturnMode || g_stageId != SATURN_TICK_STAGE) return 0;

    PlayerEntity& pl = g_playerEntity;
    JointStruct* joints = pl.jointsStructs;
    VECTOR* pos = (VECTOR*)pl.scaMatrixData.localMatrix.t;

    switch (pl.action_state) {
    case 0:
        pl.action_state = 1;
        pl.animation_frame_id = 0;
        pl.unk_bf = 0;
        pl.flags |= 6;
        pl.unk_bc = 0xB4;
        g_message_flags &= 0xFFBF;
        pl.attackDirection = 0;
        break;

    case 1:
        if (pl.animation_frame_id == 8) {
            Play3DSnd(3, 3, 0, (int)pl.scaMatrixData.localMatrix.t);
            JointApplyColorTint(&joints[0], 0x30, 0x80820, (void*)0x606060);
            JointApplyColorTint(&joints[1], 0x30, 0x80820, (void*)0x606060);
            JointApplyColorTint(&joints[2], 0x30, 0x80820, (void*)0x606060);
            JointApplyColorTint(&joints[9], 0x30, 0x80820, (void*)0x606060);
            JointApplyColorTint(&joints[12], 0x30, 0x80820, (void*)0x606060);
        }
        if (pl.animation_frame_id < 9 && (pl.animation_frame_id & 1) == 0) {
            g_playerPosScratch.x = 0;
            g_playerPosScratch.y = -1500;     // 0xFFFFFA24
            g_playerPosScratch.z = 0;
            g_playerPosScratch.pad = 0;
            Effect_CreateBillboard(0, 0, 0, &pl.scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        }
        if (pl.animation_frame_id > 0x5F) {
            g_playerPosScratch.x = 0;
            g_playerPosScratch.y = 0;
            g_playerPosScratch.z = 0;
            g_playerPosScratch.pad = 0;
            Effect_CreateBillboard(0, 0, 0, &pl.scaMatrixData.localMatrix, &g_playerPosScratch, 0);
        }
        pl.action_state = (unsigned char)(pl.action_state +
            (char)Joint_move(0, pl.emdScratchPtr1, pl.emdScratchPtr2, 0x400));
        break;

    case 2: {
        pl.action_state = 3;
        pl.animation_frame_id = 0;
        pl.unk_bf = 0;
        const bool notOne = pl.attackAnim != 1;
        pl.attackAnim = 2;
        if (notOne) pl.attackAnim = 1;
        break;
    }

    case 3:
        if ((pl.attackDirection & 1) == 0) {
            pl.attackDirection++;
        } else {
            pl.attackDirection++;
            Joint_move(0, pl.emdScratchPtr1, pl.emdScratchPtr2, 0x400);
        }
        break;

    case 4:
        pl.action_state = 5;
        pl.animation_frame_id = 0;
        pl.unk_bf = 0;
        pl.attackAnim = 0;
        pl.isBeingAttackedFlag = 0x80;
        joints[1].flags &= 0xFE;                      // hide the head
        g_playerPosScratch.x = 0;
        g_playerPosScratch.y = 0;
        g_playerPosScratch.z = 0;
        g_playerPosScratch.pad = 0;
        Effect_CreateBillboard(0, 3, 0, &joints[1].world, &g_playerPosScratch, 0);
        Effect_CreateBillboard(0, 3, 0, (void*)g_deadMoveValue, &joints[1].world.t, 0);
        Snd_em(5);
        break;

    case 5: {
        pl.action_state = (unsigned char)(pl.action_state +
            (char)Joint_move(0, pl.emdScratchPtr1, pl.emdScratchPtr2, 0x400));
        const unsigned char f = pl.animation_frame_id;
        if ((unsigned char)(f - 9) < 0x22 && (f & 3) == 0) {
            const int* dead = (const int*)g_deadMoveValue;
            g_playerPosScratch.x = dead[5];
            g_playerPosScratch.y = -900;              // 0xFFFFFC7C
            g_playerPosScratch.z = dead[7];
            g_playerPosScratch.pad = dead[8];
            Effect_CreateBillboard(0, 0, 0, &joints[0].world, &g_playerPosScratch, 0);
        }
        if (f == 3) {
            JointApplyColorTint(&joints[0], 0x30, 0x80820, (void*)0x606060);
            JointApplyColorTint(&joints[9], 0x30, 0x80820, (void*)0x606060);
            JointApplyColorTint(&joints[12], 0x30, 0x80820, (void*)0x606060);
        }
        if (f == 0x2A) PlayEntitySnd(2);
        if (f == 0x10) pl.health = -1;                // 0xFFFF
        break;
    }

    case 6: {
        g_svecScratch.x = -900;
        g_svecScratch.y = 0;
        g_svecScratch.z = 0;
        memcpy(&g_matrixScratch, (const void*)g_deadMoveValue, 32);
        RotMatrixY((int)(unsigned short)pl.directionAngle, &g_matrixScratch);
        ApplyMatrixSV(&g_matrixScratch, &g_svecScratch, &g_svecScratch);
        ENTITY->pushVelocity.x = (short)(ENTITY->pushVelocity.x + g_svecScratch.x);
        ENTITY->pushVelocity.y = (short)(ENTITY->pushVelocity.y + g_svecScratch.y);
        ENTITY->pushVelocity.z = (short)(ENTITY->pushVelocity.z + g_svecScratch.z);
        BillboardSetColor(&ENTITY->pushVelocity, 1, 2, 0x00FFFF50);
        BillboardAdjSize(&ENTITY->pushVelocity, (short)0xFF38, (short)0xFF38);
        pl.action_state = 7;
        break;
    }

    case 7:
        BillboardAdjSize(&ENTITY->pushVelocity, 0x10, 0x10);
        if (pl.unk_bc == 0xA0) {
            g_fade_type_id = 1;
            g_fading_counter = 0x100;
            fade_update();
        }
        pl.unk_bc--;
        if (pl.unk_bc == 0x20) {
            pl.animationId = 4;
        }
        break;

    default:
        break;
    }
    (void)pos;
    return 1;
}

// 0x0606DE98. The stab, then a slow turn toward the player: 0x10 a frame while
// the player is more than 4 units off the heading (+0xA0 trims it).
void saturn_tick_behavior_pounce(void)
{
    TickState* st = tick_state();

    T_POUNCE_LATCH = 0;
    tick_stab(st);

    const short toPlayer = (short)getAngleTowardsTarget(T_PLAYER_T_INT[0], T_PLAYER_T_INT[2]);
    const short facing = ENTITY->angle;
    const unsigned short delta = (unsigned short)((toPlayer - facing) + 0xA0) & 0xFFF;
    if ((unsigned short)(delta - 5) < 0xFF7) {
        ENTITY->angle = (short)(facing + (delta < 0x801 ? 0x10 : -0x10));
    }
}

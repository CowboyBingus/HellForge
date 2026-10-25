/* New behaviours (SDK 4, docs/modding/content.md "Behaviours"): behaviour ids of their own (694 and up) that
 * the game's behaviour system runs like its own. An entity picks one by data: its BehaviorComponentData record's id
 * (HfEntity.behaviour). Included by hellforge_modloader.c, after registry.c and damage.c (AllocateNearGame).
 *
 * The game's behaviours (build 25480438, game.dll 2E2C3B7C...):
 *   three dispatchers switch on the id through 693-entry tables, update 0x472F60 (ctx, id, dt), stage 0x48EE50
 *   (ctx, id, stage) and event 0x4966D0 (ctx, id, hash); ctx = {EntityInfo*, P*}, P = the behaviour record + 8 (P+0
 *   the stage). Each begins `dec edx; cmp edx, 0x2B4; ja default`, so an id above 693 takes that `ja`. Three helpers
 *   do the same: the stage count 0x495F20 (0 for an unknown id, and then the entity never enters stage 1) and the
 *   event <-> network slot pair 0x47A240 / 0x484B60.
 *   HellForge points those six `ja`s at stubs in a block near game.dll. A stub hands HellForge's ids to its code (or,
 *   for the helpers, to its base's answer) and sends every other id to the original target, so vanilla behaviours run
 *   exactly as before. The update, stage and event stubs leave through their dispatcher's default epilogue (0x4795AA,
 *   0x4930FD, 0x4A0141), which restores only what the prologue saved before the `ja`.
 * A HellForge behaviour extends a vanilla one (HfBehaviour.base) and runs that behaviour's own functions, which never
 * read the id:
 *   667, the transport Pelican (update 0x45C760, stage 0x45BD00, event 0x45BBE0): flies to the beacon (stages 1-3),
 *        hovers (6, entered with P+0x180 = now), releases its cargo after 6 s, departs and is removed. HellForge's
 *        version flies from its approach straight to a point on its orbit (instead of 6 m over the beacon) and enters
 *        the hover there, then holds it for hold_seconds, circling the beacon with the extraction Pelican's orbit step
 *        (0x4D5E60, at the behaviour's radius and height) and move-to (0x4D1150) and turning its body toward its
 *        mounted turret's target (0x4C1530, the hover controller's desired facing); then 667 releases and departs.
 *   645, the Pelican's chin turret (its update is inline in the dispatcher at 0x478D9D: stage 1 0x44F310 idle,
 *        stage 2 0x44FE20 aim (at least 1.5 s, until within 2 degrees), stage 3 fire, stage 4 inactive; transitions
 *        0x450AD0): it fires 0.5 s (P+0x180 + 0.5 s), then aims again, re-scoring its perceiver's entries without regard
 *        to sight. Its firing stage never looks at the target (0x4B0810 tests that the turret is active, 0x4BB080 that
 *        it has ammunition). HellForge's version chooses the target
 *        itself, as HD2Runtime's Pelican Gatling did: only entries its sensors see now (sense bits against the
 *        perceiver's sensors), hostile, alive, in range and not rejected, the nearest to the target it last let go
 *        (or to the turret); it aims 0.3 s and fires once within 2 degrees of its aim point (no 1.5 s wait), at the
 *        behaviour's aim height above the target's feet but for Bile Titans (players near it or not: 645 spares targets
 *        within 15 m of a player); it lets one go when it
 *        dies, has not been sensed for 1 s, has taken no damage in 3 s of firing, or cannot be aimed at in 3 s (the last
 *        three are not chosen again for 5 s), for the nearest valid one; installed with the game's target setter
 *        (0x4AF4E0), while 645's own re-score waits (P+0x90, its re-score time, held ahead: not replicated). It fires for
 *        burst_seconds, or as long as it keeps a target (0), and refills its magazine from reserve_rounds before it runs
 *        dry (0x76E080, on the machine with authority), as 645 never reloads.
 * Co-op (2026-10-18): a behaviour runs on the machine with authority over
 * its entity. Elsewhere the record is made empty at the spawn (0x842200 skips its set-up without authority) and filled
 * from what arrives (0x842690: the id, the stage and the fields), and the manager does not update a transport there;
 * each arrival ends with resume (0x493BE0), which for 667 sends the Pelican's mover to its flight target, the replicated
 * P+0x1BC (0x495710, stages 1-10). So a new behaviour resumes as its base does (a seventh patched site: resume's first
 * split, `jg` for ids above 0x11A at 0x493C00, the one whose rel32 sits inside an aligned qword; its stub sends a new id
 * into the switch over 0x255..0x2AB with its base's index), and whatever it writes to P+0x1BC it marks replicated
 * (0x8449B0, mask 0xE00), as
 * the orbit step does. Without the two, a friend's Pelican sat where it spawned on the other machines. */

#define BHV_FIRST 694u
#define BHV_MAX 8u
#define BHV_PELICAN_TRANSPORT 667u
#define BHV_PELICAN_TURRET 645u

#define BHV_CLOCK 0x3326348u          /* [game + this] + 0x18: the behaviour clock, microseconds */
#define BHV_MANAGER 0x3326740u        /* behaviour records: map +0x40, records +0x60, stride 0x1F8 */
#define BHV_MOUNTS 0x3326438u         /* mounts: map +0x20, records +0x48 of 6 u32 (children 0..4) */
#define BHV_ANCHORS 0x3326658u        /* drop positions: map +0x30, records +0x60, stride 0x20, +0 the anchor */
#define BHV_INVALID_ENTITY 0x3483C20u /* u32: the invalid entity id */
#define BHV_PERCEPTION 0x3326548u     /* perceivers: map +0x30, records +0x50, stride 0x13F8 */
#define BHV_FACTIONS 0x3326CB8u       /* factions: map +0x12050, masks +0x12078 (u32 per index) */
#define BHV_FLAGS 0x3326430u          /* entity flags: map +0x18, values +0x38 (u64 per index); bit 35: never targeted */
#define BHV_HEALTH 0x3326688u         /* health: map +0x1030, count +0x1020, records +0x1058 stride 0x1B8 */
#define BHV_ROUNDS 0x3326660u         /* the magazines 0x742900 counts */
#define BHV_ENGINE_API 0x3326308u     /* the engine API: [+0x18] the scene graph, its +0x88 a node's world position */
#define BHV_TARGETING 0x3326D30u      /* weapon targeting: map +0x150, records +0x178 stride 0xD0, network +0x180 stride 0x18 */
#define BHV_MAGAZINES 0x3326648u      /* the magazines 0x76E080 refills: map +0x20 */

typedef struct BehaviourCtx { uint8_t* entity; uint8_t* p; } BehaviourCtx;
typedef void (*BhvCtxFn)(BehaviourCtx* ctx);
typedef void (*BhvCtxIntFn)(BehaviourCtx* ctx, int value);
typedef void (*BhvCtxHashFn)(BehaviourCtx* ctx, uint32_t hash);
typedef void (*BhvCtxTimeFn)(BehaviourCtx* ctx, float dt);
typedef char (*BhvCtxTestFn)(BehaviourCtx* ctx);
typedef float* (*BhvPositionFn)(float out[3], BehaviourCtx* ctx);
typedef void (*BhvOrbitFn)(BehaviourCtx* ctx, const float position[3], void* unused, float height, float radius);
typedef void (*BhvVectorFn)(BehaviourCtx* ctx, const float vector[3]);
typedef float* (*BhvTargetFn)(float out[3], BehaviourCtx* ctx, const float self[3]);

typedef struct GameFunction { uint32_t rva; uint8_t pin[32]; } GameFunction;
enum
{
    F_667_UPDATE, F_667_STAGE, F_667_EVENT, F_POSITION, F_ORBIT, F_MOVE, F_FACE, F_TARGET,
    F_645_IDLE, F_645_AIM, F_645_ACTIVE, F_645_AMMO, F_645_STAGE, F_SET_TARGET, F_DEGREES, F_VALID, F_FIND_ENTRY,
    F_NEAR_PLAYER, F_ROUNDS, F_REFILL, F_CAPACITY, F_FIRE_GATE, F_DESCRIPTOR, F_MARK, F_COUNT
};
static const GameFunction kBhvFunctions[F_COUNT] = {
    {0x45C760u, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x60, 0x48, 0x8b, 0x05, 0x9c, 0xf8, 0x1d, 0x02, 0x48, 0x33, 0xc4, 0x48, 0x89, 0x45, 0xf0, 0x48, 0x8b, 0x51, 0x08, 0x48}}, /* 667 update */
    {0x45BD00u, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x7c, 0x24, 0x10, 0x55, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x70, 0x48, 0x8b, 0x41, 0x08, 0x8b, 0xfa, 0x48, 0x8b, 0xd9, 0x39, 0x50, 0x08, 0x0f, 0x84}}, /* 667 stage */
    {0x45BBE0u, {0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x48, 0x8b, 0xd9, 0x81, 0xfa, 0x75, 0xd1, 0x71, 0x33, 0x74, 0x74, 0x81, 0xfa, 0xde, 0x01, 0xc4, 0x54, 0x74, 0x3b, 0x81, 0xfa, 0x1a, 0x39, 0x02, 0xca, 0x74}}, /* 667 event */
    {0x4BC5D0u, {0x40, 0x53, 0x56, 0x48, 0x83, 0xec, 0x38, 0x48, 0x8b, 0x32, 0x48, 0x8b, 0xd9, 0x8b, 0x4e, 0x0c, 0x85, 0xc9, 0x74, 0x2e, 0x48, 0x8b, 0x05, 0x1d, 0x9d, 0xe6, 0x02, 0x48, 0x8b, 0x50, 0x18, 0x48}}, /* own position */
    {0x4D5E60u, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, 0x4c, 0x8b, 0x29, 0x45, 0x33, 0xf6}}, /* next orbit point -> P+0x1BC */
    {0x4D1150u, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec}}, /* fly to */
    {0x4C1530u, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30, 0x4c, 0x8b, 0x21, 0x45}}, /* face */
    {0x4B0C40u, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b}}, /* a behaviour's target position */
    {0x44F310u, {0x40, 0x55, 0x41, 0x55, 0x48, 0x8d, 0xac, 0x24, 0x38, 0xfe, 0xff, 0xff, 0x48, 0x81, 0xec, 0xc8, 0x02, 0x00, 0x00, 0x48, 0x8b, 0x05, 0xe6, 0xcc, 0x1e, 0x02, 0x48, 0x33, 0xc4, 0x48, 0x89, 0x85}}, /* 645 stage 1: idle */
    {0x44FE20u, {0x40, 0x55, 0x41, 0x57, 0x48, 0x8d, 0xac, 0x24, 0x48, 0xfe, 0xff, 0xff, 0x48, 0x81, 0xec, 0xb8, 0x02, 0x00, 0x00, 0x48, 0x8b, 0x05, 0xd6, 0xc1, 0x1e, 0x02, 0x48, 0x33, 0xc4, 0x48, 0x89, 0x85}}, /* 645 stage 2: aim */
    {0x4B0810u, {0x48, 0x83, 0xec, 0x28, 0x4c, 0x8b, 0x19, 0x41, 0x8b, 0x43, 0x08, 0x3b, 0x05, 0xff, 0x33, 0xfd, 0x02, 0x74, 0x68, 0x48, 0x8b, 0x15, 0x46, 0x65, 0xe7, 0x02, 0x33, 0xc9, 0x48, 0x89, 0x74, 0x24}}, /* the turret is active */
    {0x4BB080u, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x30, 0x4c, 0x8b, 0x29, 0x4c}}, /* it has ammunition */
    {0x450AD0u, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x60, 0x48, 0x8b, 0x41, 0x08, 0x8b, 0xda, 0x48, 0x8b, 0xf9, 0x39, 0x50, 0x08, 0x0f, 0x84, 0xcb, 0x02, 0x00, 0x00, 0x89, 0x50, 0x08, 0x48}}, /* 645 stage */
    {0x4AF4E0u, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x4c, 0x8b, 0x41, 0x08, 0x4c, 0x8b}}, /* install a perception entry as the target, or clear (NULL) */
    {0x7582D0u, {0x4c, 0x8b, 0xdc, 0x48, 0x81, 0xec, 0x88, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x05, 0x2f, 0x3d, 0xee, 0x01, 0x48, 0x33, 0xc4, 0x48, 0x89, 0x44, 0x24, 0x30, 0x3b, 0x15, 0x31, 0xb9, 0xd2, 0x02, 0x49}}, /* degrees between a weapon's forward and a point */
    {0x8858A0u, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xec, 0x20, 0x49, 0x8b, 0xd8, 0x4d, 0x85, 0xc0}}, /* a valid target: exists, alive, not flag 25 */
    {0x8891C0u, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56, 0x3b, 0x15, 0x44, 0xaa, 0xbf, 0x02, 0x48, 0x8b, 0xd9, 0x0f}}, /* a perceiver's entry for an entity */
    {0x4B3B80u, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x48, 0x89, 0x4c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x50, 0x48, 0x8b, 0x0d, 0xd2, 0x28, 0xe7, 0x02, 0x33, 0xdb, 0x0f, 0x29, 0x74, 0x24, 0x40, 0x48, 0x8b, 0xfa}}, /* a living player near a point */
    {0x742900u, {0x48, 0x89, 0x5c, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x20, 0x3b, 0x15, 0x0e, 0x13, 0xd4, 0x02, 0x8b, 0xda, 0x4c, 0x8b, 0xc9, 0x74, 0x43, 0x44, 0x8b, 0x41, 0x30, 0x33, 0xf6, 0x44}}, /* rounds in the magazine */
    {0x76E080u, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x3d, 0xab, 0x85, 0xbb, 0x02, 0x45, 0x33, 0xe4}}, /* add rounds to the magazine */
    {0x743410u, {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xec, 0x20, 0x3b, 0x15, 0xfe, 0x07, 0xd4, 0x02, 0x8b, 0xda, 0x4c, 0x8b, 0x05, 0x35, 0x32, 0xbe, 0x02, 0x74, 0x3f, 0x45, 0x8b, 0x48}}, /* magazine capacity (ignores its first argument) */
    {0x4B54D0u, {0x48, 0x89, 0x6c, 0x24, 0x18, 0x56, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0x31, 0x48, 0x8b, 0xea, 0x0f, 0x29, 0x74, 0x24, 0x30, 0x0f, 0x28, 0xf2, 0x8b, 0x46, 0x08, 0x3b, 0x05, 0x2f, 0xe7, 0xfc}}, /* the weapon within maxDeg of a point (645's fire gate) */
    {0xFD9D40u, {0x48, 0x83, 0xec, 0x08, 0x3b, 0x0d, 0x02, 0x9f, 0x4a, 0x02, 0x0f, 0x84, 0x9b, 0x00, 0x00, 0x00, 0x4c, 0x8b, 0x15, 0x41, 0x22, 0x49, 0x02, 0x45, 0x33, 0xc0, 0x48, 0x89, 0x5c, 0x24, 0x10, 0x48}}, /* an entity's descriptor (+0 its type) */
    {0x8449B0u, {0x41, 0x56, 0x3b, 0x15, 0x68, 0xf2, 0xc3, 0x02, 0x45, 0x8b, 0xf0, 0x4c, 0x8b, 0x0d, 0x7e, 0x1d, 0xae, 0x02, 0x0f, 0x84, 0xd3, 0x00, 0x00, 0x00, 0x45, 0x8b, 0x51, 0x48, 0x45, 0x33, 0xc0, 0x48}}, /* mark a record's fields replicated (no-op without authority) */
};

/* The six branches HellForge retargets: {rva of the `ja` (0F 87 rel32), its original target}. */
typedef struct BhvSite { uint32_t ja, target; uint8_t cond; } BhvSite; /* cond: the branch's second opcode byte (0: ja, 0x87) */
enum { S_UPDATE, S_STAGE, S_EVENT, S_STAGES, S_TO_SLOT, S_FROM_SLOT, S_RESUME, S_COUNT };
static const BhvSite kBhvSites[S_COUNT] = {
    {0x472F8Fu, 0x4795AAu}, {0x48EE71u, 0x4930FDu}, {0x4966F0u, 0x4A0141u},
    {0x495F28u, 0x495FB8u}, {0x47A248u, 0x484081u}, {0x484B68u, 0x48A2A1u},
    {0x493C00u, 0x494C76u, 0x8F}, /* resume 0x493BE0: `cmp edx, 0x11A; jg` (edx = the id, rbx = ctx) */
};
#define BHV_RESUME_FIRST 0x255u
#define BHV_RESUME_LAST 0x2ABu
#define BHV_RESUME_CASES 0x4952C2u /* the switch over 0x255..0x2AB, given edx = id - 0x255 (lea r8, [image] first) */
#define BHV_RESUME_NONE 0x495846u  /* resume's end: nothing to resume */
static const uint8_t kBhvResumePin[12] = {0x81, 0xfa, 0x1a, 0x01, 0x00, 0x00, 0x0f, 0x8f, 0x70, 0x10, 0x00, 0x00};
/* the switch the stub enters: `add edx, -0x255; cmp edx, 0x56; ja 0x495846; lea r8, [image]` */
#define BHV_RESUME_SWITCH 0x4952B3u
static const uint8_t kBhvResumeSwitchPin[18] = {0x81, 0xc2, 0xab, 0xfd, 0xff, 0xff, 0x83, 0xfa, 0x56, 0x0f, 0x87, 0x84, 0x05, 0x00, 0x00, 0x4c, 0x8d, 0x05};

/* What the per-id helpers answer for each base (the vanilla answers, read from their tables). */
typedef struct BhvBase { uint32_t id, stages, toSlot, fromSlot; int death; } BhvBase;
static const BhvBase kBhvBases[] = {
    {BHV_PELICAN_TRANSPORT, 12, 0x483D22u, 0x489FC9u, 12}, /* its replicated events; death: its destroy stage */
    {BHV_PELICAN_TURRET, 4, 0x484081u, 0x48A2A1u, 0},      /* no replicated events; death: inactive */
};

typedef struct Behaviour
{
    HfMod* mod;
    uint32_t id;
    const BhvBase* base;
    uint64_t holdMicroseconds, burstMicroseconds;
    float radius, height;
    int face;
    int32_t reserveRounds;
    float aimHeight;
} Behaviour;

static Behaviour g_behaviours[BHV_MAX];

/* Diagnostics: where a HellForge behaviour call is, so the loader's watcher (BehaviourWatch) can report a call that
 * does not return and the fault handler (BehaviourFault) a fault inside one. Written by the game thread, read by them. */
static volatile ULONGLONG g_bhvCallAt;   /* GetTickCount64 when the current call began, 0 outside one */
static volatile DWORD g_bhvThread;
static volatile uint32_t g_bhvStep, g_bhvId;
static volatile int32_t g_bhvStage;
#define STEP(n) (g_bhvStep = (n))
static uint32_t g_behaviourCount;
static int g_behaviourState; /* 0 waiting, 1 installed, -1 refused */
static uintptr_t g_bhvGame;

static uint32_t ApiAddBehaviour(HfMod* mod, const HfBehaviour* def)
{
    if (mod == NULL || def == NULL || def->size < offsetof(HfBehaviour, face_target) + sizeof def->face_target)
    {
        ApiLog(mod, "add_behaviour: no behaviour, or its size is not set (HfBehaviour b = {sizeof b})");
        return 0;
    }
    int live = g_liveSetup != NULL && g_liveSetup == mod; /* a mod set up while the game runs (live_content.c) */
    if (!live && (g_behaviourState != 0 || g_fired[HF_EVENT_GAME_LOADED])) { ApiLog(mod, "add_behaviour: too late; call it from hellforge_mod_init"); return 0; }
    if (!live && g_behaviourCount >= BHV_MAX) { ApiLog(mod, "add_behaviour: mods added %u behaviours already", BHV_MAX); return 0; }
    const BhvBase* base = NULL;
    for (size_t i = 0; i < sizeof kBhvBases / sizeof kBhvBases[0]; i++) if (kBhvBases[i].id == def->base) base = &kBhvBases[i];
    if (base == NULL) { ApiLog(mod, "add_behaviour: base %u is not one HellForge extends (HF_BEHAVIOUR_PELICAN_...)", def->base); return 0; }
    float burst = def->size >= offsetof(HfBehaviour, burst_seconds) + sizeof def->burst_seconds ? def->burst_seconds : 0.0f;
    int32_t reserve = def->size >= offsetof(HfBehaviour, reserve_rounds) + sizeof def->reserve_rounds ? (int32_t)def->reserve_rounds : 0;
    if (reserve < 0 || reserve > 100000) { ApiLog(mod, "add_behaviour: reserve_rounds 0 to 100000"); return 0; }
    float aimHeight = def->size >= offsetof(HfBehaviour, aim_height) + sizeof def->aim_height ? def->aim_height : 0.0f;
    if (!(aimHeight >= 0.0f && aimHeight <= 5.0f)) { ApiLog(mod, "add_behaviour: aim_height 0 to 5 m"); return 0; }
    if (!(def->hold_seconds >= 0.0f && def->hold_seconds <= 600.0f) || !(def->orbit_radius >= 0.0f && def->orbit_radius <= 200.0f) ||
        !(def->orbit_height >= 0.0f && def->orbit_height <= 200.0f) || !(burst >= 0.0f && burst <= 60.0f))
    {
        ApiLog(mod, "add_behaviour: hold 0 to 600 s, orbit radius and height 0 to 200 m, burst 0 to 60 s");
        return 0;
    }
    uint32_t index = g_behaviourCount;
    Behaviour* b = live ? (Behaviour*)LiveEntry(LC_BEHAVIOUR, mod, NULL, &index) : &g_behaviours[index];
    if (b == NULL) return 0;
    memset(b, 0, sizeof *b);
    b->mod = mod;
    b->id = BHV_FIRST + index;
    b->base = base;
    b->holdMicroseconds = (uint64_t)(def->hold_seconds * 1000000.0f);
    b->burstMicroseconds = (uint64_t)(burst * 1000000.0f);
    b->radius = def->orbit_radius;
    b->height = def->orbit_height;
    b->face = def->face_target != 0;
    b->reserveRounds = reserve;
    b->aimHeight = aimHeight;
    MemoryBarrier(); /* the entry is whole before the count shows it */
    if (index == g_behaviourCount) g_behaviourCount++;
    if (base->id == BHV_PELICAN_TRANSPORT)
        ApiLog(mod, "adding behaviour %u: %u holding %.0f s, orbit %.0f m at %.0f m%s", b->id, base->id, (double)def->hold_seconds,
            (double)b->radius, (double)b->height, b->face ? ", facing its turret's target" : "");
    else
        ApiLog(mod, "adding behaviour %u: %u firing %s", b->id, base->id, burst > 0.0f ? "in bursts" : "while it has an aimed target");
    return b->id;
}

/* Whether entities may use a behaviour id: vanilla, or HellForge's and installed. */
static int BehaviourUsable(uint32_t id)
{
    if (id >= 1 && id < BHV_FIRST) return 1;
    return g_behaviourState == 1 && id >= BHV_FIRST && id < BHV_FIRST + g_behaviourCount;
}

static void* BhvFn(int f) { return (void*)(g_bhvGame + kBhvFunctions[f].rva); }
static uint64_t BhvNow(void) { return *(uint64_t*)(*(uint8_t**)(g_bhvGame + BHV_CLOCK) + 0x18); }

/* The value of a game manager's open-addressed map {u32 key, u32 value} (table at +at, then capacity, empty key and
 * multiplier) for an entity id, or -1. */
static int64_t BhvMapFind(uintptr_t manager, uint32_t at, uint32_t entity)
{
    uint8_t* table = *(uint8_t**)(manager + at);
    uint32_t capacity = *(uint32_t*)(manager + at + 8), empty = *(uint32_t*)(manager + at + 12);
    uint32_t multiplier = *(uint32_t*)(manager + at + 16);
    if (table == NULL || capacity == 0) return -1;
    for (uint32_t i = 0; i < capacity; i++)
    {
        uint32_t slot = (i + multiplier * entity) & (capacity - 1);
        uint32_t key = *(uint32_t*)(table + (size_t)slot * 8), value = *(uint32_t*)(table + (size_t)slot * 8 + 4);
        if (key == entity) return value == 0xFFFFFFFFu ? -1 : (int64_t)value;
        if (key == empty) return -1;
    }
    return -1;
}

static uint32_t EntityOf(BehaviourCtx* ctx) { return *(uint32_t*)(ctx->entity + 8); }

/* The beacon the entity was called to (its drop position's anchor). */
static int Anchor(BehaviourCtx* ctx, float out[3])
{
    uintptr_t anchors = *(uintptr_t*)(g_bhvGame + BHV_ANCHORS);
    int64_t record = anchors != 0 ? BhvMapFind(anchors, 0x30, EntityOf(ctx)) : -1;
    if (record < 0) return 0;
    memcpy(out, *(uint8_t**)(anchors + 0x60) + (size_t)record * 0x20, 12);
    return 1;
}

/* The position of what the entity's mounted turret targets, or 0. */
static int TurretTarget(BehaviourCtx* ctx, const float self[3], float out[3])
{
    uint32_t invalid = *(uint32_t*)(g_bhvGame + BHV_INVALID_ENTITY);
    uintptr_t mounts = *(uintptr_t*)(g_bhvGame + BHV_MOUNTS), behaviours = *(uintptr_t*)(g_bhvGame + BHV_MANAGER);
    if (mounts == 0 || behaviours == 0) return 0;
    int64_t mount = BhvMapFind(mounts, 0x20, EntityOf(ctx));
    if (mount < 0) return 0;
    const uint32_t* children = (const uint32_t*)(*(uint8_t**)(mounts + 0x48) + (size_t)mount * 24);
    for (int k = 0; k < 5; k++)
    {
        if (children[k] == invalid || children[k] == 0) continue;
        int64_t record = BhvMapFind(behaviours, 0x40, children[k]);
        if (record < 0) continue;
        uint8_t* p = *(uint8_t**)(behaviours + 0x60) + (size_t)record * 0x1F8 + 8;
        if (p[0x70] == 0 || *(uint32_t*)(p + 0x10) == invalid) continue; /* no target, or 645's rest point */
        BehaviourCtx turret = {NULL, p};
        ((BhvTargetFn)BhvFn(F_TARGET))(out, &turret, self);
        return 1;
    }
    return 0;
}

/* P+0x190 / P+0x198: replicated timestamps 667 never uses. HellForge keeps when its approach began and which hover
 * its orbit started for there. */
#define P_APPROACH 0x190
#define P_ORBIT_OF 0x198

typedef void (*BhvMarkFn)(void* unused, uint32_t entity, uint32_t mask);

/* Marks fields of the entity's record replicated, as the game's behaviour code does (a networked entity only). */
static void MarkReplicated(BehaviourCtx* ctx, uint32_t mask)
{
    if (*(uint32_t*)(ctx->entity + 0x10) != 0x7FFFu) ((BhvMarkFn)BhvFn(F_MARK))(NULL, EntityOf(ctx), mask);
}

/* The transport's approach, stage 3 (667 flies to 6 m over the beacon and hovers within 20 m of it): to a point on
 * the orbit instead, on the side the Pelican comes from, and the hover there. */
static void Approach(BehaviourCtx* ctx, const Behaviour* b)
{
    float self[3], anchor[3];
    ((BhvPositionFn)BhvFn(F_POSITION))(self, ctx);
    uint64_t now = BhvNow(), *started = (uint64_t*)(ctx->p + P_APPROACH);
    if (*started == 0) *started = now;
    if (!Anchor(ctx, anchor)) { ((BhvCtxFn)BhvFn(F_667_UPDATE))(ctx); return; }
    float dx = self[0] - anchor[0], dy = self[1] - anchor[1], d = sqrtf(dx * dx + dy * dy);
    float* point = (float*)(ctx->p + 0x1BC);
    if (d > 1.0f) { point[0] = anchor[0] + dx / d * b->radius; point[1] = anchor[1] + dy / d * b->radius; }
    else { point[0] = anchor[0] + b->radius; point[1] = anchor[1]; }
    point[2] = anchor[2] + b->height;
    MarkReplicated(ctx, 0xE00); /* P+0x1BC: the other machines' copies fly to it (resume) */
    float target[3] = {point[0], point[1], point[2]};
    ((BhvVectorFn)BhvFn(F_MOVE))(ctx, target);
    float ex = self[0] - target[0], ey = self[1] - target[1], ez = self[2] - target[2];
    if (ex * ex + ey * ey + ez * ez <= 15.0f * 15.0f || now >= *started + 30000000ull)
        ((BhvCtxIntFn)BhvFn(F_667_STAGE))(ctx, 6);
}

/* One update of the hold: orbit the beacon and face the turret's target. */
static void HoldTick(BehaviourCtx* ctx, const Behaviour* b)
{
    float self[3];
    ((BhvPositionFn)BhvFn(F_POSITION))(self, ctx);
    if (b->radius > 0.0f)
    {
        float* point = (float*)(ctx->p + 0x1BC);
        uint64_t hover = *(uint64_t*)(ctx->p + 0x180), *orbitOf = (uint64_t*)(ctx->p + P_ORBIT_OF);
        float dx = point[0] - self[0], dy = point[1] - self[1];
        /* At its point (or at the hover's start): the next one, a step around the beacon (as the extraction Pelican). */
        if (*orbitOf != hover || dx * dx + dy * dy <= 25.0f)
        {
            *orbitOf = hover;
            ((BhvOrbitFn)BhvFn(F_ORBIT))(ctx, self, NULL, b->height, b->radius);
            float target[3] = {point[0], point[1], point[2]};
            ((BhvVectorFn)BhvFn(F_MOVE))(ctx, target);
        }
    }
    float aim[3];
    if (b->face && TurretTarget(ctx, self, aim))
    {
        float direction[3] = {aim[0] - self[0], aim[1] - self[1], 0.0f};
        float length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1]);
        if (length > 1.0f)
        {
            direction[0] /= length;
            direction[1] /= length;
            ((BhvVectorFn)BhvFn(F_FACE))(ctx, direction);
        }
    }
}

static void TransportUpdate(BehaviourCtx* ctx, const Behaviour* b)
{
    int32_t stage = *(int32_t*)ctx->p;
    if (stage == 3 && b->radius > 0.0f) { Approach(ctx, b); return; }
    uint64_t hover = *(uint64_t*)(ctx->p + 0x180);
    if (stage == 6 && b->holdMicroseconds != 0 && BhvNow() < hover + b->holdMicroseconds) { HoldTick(ctx, b); return; }
    ((BhvCtxFn)BhvFn(F_667_UPDATE))(ctx); /* 667: approach, hover, release (at once after the hold), depart, removal */
}

/* ---- the turret ------------------------------------------------------------------------------------------------ */

#define T_GRACE 1000000ull          /* not sensed this long (its sensors re-test by rays, so sight flickers): let go */
#define T_RECENT 10000000ull        /* a target let go this recently still guides the next choice (the nearest to it) */
#define T_AIM_MIN 300000ull         /* aiming at least this long before firing (645: 1.5 s) */
#define T_AIM_GATE 2.0f             /* firing within this many degrees of the aim point (645's) */
#define T_RELEASE 3000000ull        /* fired at this long without it taking damage: let go */
#define T_AIM_TIMEOUT 3000000ull    /* aimed at this long without getting within 2 degrees: let go */
#define T_REJECT 5000000ull         /* a target let go for those reasons is not chosen again for this long */
#define T_PICK 250000ull            /* how often an idle turret looks for a target */
#define T_SEARCH 1000000ull         /* how long an aiming turret that lost its target looks before it idles */
#define T_HOLD_RESCORE 3600000000ull
#define T_RANGE 120.0f
#define T_FIRE_TRAVERSE 12.0f       /* a new target this close keeps the trigger pulled */
#define T_REFILL_BELOW 200
#define T_MINDS 16

typedef char (*BhvValidFn)(void* unused, uint32_t entity, void* info);
typedef uint8_t* (*BhvFindEntryFn)(uintptr_t perception, uint32_t perceiver, uint32_t entity);
typedef float (*BhvDegreesFn)(void* unused, uint32_t entity, const float position[3]);
typedef int32_t (*BhvRoundsFn)(void* manager, uint32_t entity);
typedef void (*BhvRefillFn)(void* unused, uint32_t entity, int32_t add);
typedef char (*BhvFireGateFn)(BehaviourCtx* ctx, const float position[3], float maxDegrees);

/* What a turret has decided (per turret, on the machine that runs its behaviour). */
typedef struct TurretMind
{
    uint32_t turret, lock, sighted; /* lock: P's target (the setter may redirect it); sighted: the perceiver entry picked */
    float lockPosition[3];
    int hasLockPosition;
    uint64_t seenAt, progressAt, searchUntil, nextPick, used, lockedAt; /* lockedAt: when the lock was taken (its aim time) */
    int32_t health;
    float sinceDamage;
    struct { uint32_t entity; uint64_t until; } rejected[8];
    int32_t reserve;
    int reserveSet, fullSeen, aiming, releases;
    uint64_t lostAt;
} TurretMind;

/* Targets whose own aim point the gun keeps (big enough that the lowered one would aim under their body): the Bile
 * Titans (content/fac_bugs/cha_strider/cha_strider, cha_strider_gloom), as HD2Runtime's Pelican Gatling did. */
static const uint64_t kOwnAim[] = {0x9E2E17F2CCCCAFDDull, 0xEF04CB84D097A497ull};

static TurretMind g_minds[T_MINDS];

static TurretMind* MindOf(uint32_t turret, uint64_t now)
{
    TurretMind* oldest = &g_minds[0];
    for (int i = 0; i < T_MINDS; i++)
    {
        if (g_minds[i].turret == turret) { g_minds[i].used = now; return &g_minds[i]; }
        if (g_minds[i].used < oldest->used) oldest = &g_minds[i];
    }
    memset(oldest, 0, sizeof *oldest);
    oldest->turret = turret;
    oldest->used = now;
    return oldest;
}

static int Rejected(const TurretMind* m, uint32_t entity, uint64_t now)
{
    for (int i = 0; i < 8; i++) if (m->rejected[i].entity == entity && now < m->rejected[i].until) return 1;
    return 0;
}

static void Reject(TurretMind* m, uint32_t entity, uint64_t now)
{
    int slot = 0;
    for (int i = 0; i < 8; i++) if (m->rejected[i].until < m->rejected[slot].until) slot = i;
    m->rejected[slot].entity = entity;
    m->rejected[slot].until = now + T_REJECT;
}

/* A u32 / u64 value an entity has in a game manager's per-index array, through its map. */
static int ManagerValue(uint32_t global, uint32_t mapAt, uint32_t valuesAt, uint32_t size, uint32_t entity, void* out)
{
    uintptr_t manager = *(uintptr_t*)(g_bhvGame + global);
    int64_t index = manager != 0 ? BhvMapFind(manager, mapAt, entity) : -1;
    uint8_t* values = index >= 0 ? *(uint8_t**)(manager + valuesAt) : NULL;
    if (values == NULL) return 0;
    memcpy(out, values + (size_t)index * size, size);
    return 1;
}

/* An entity's health: its pool, and seconds since it last took damage (ApplyDamage resets it). 0 if none, or dead. */
static int Health(uint32_t entity, int32_t* health, float* sinceDamage)
{
    uintptr_t manager = *(uintptr_t*)(g_bhvGame + BHV_HEALTH);
    int64_t index = manager != 0 ? BhvMapFind(manager, 0x1030, entity) : -1;
    if (index < 0 || (uint32_t)index >= *(uint32_t*)(manager + 0x1020)) return 0;
    uint8_t* record = *(uint8_t**)(manager + 0x1058) + (size_t)index * 0x1B8;
    *health = *(int32_t*)(record + 0x14);
    *sinceDamage = *(float*)(record + 0x1A4);
    return *health > 0 && *(uint32_t*)(record + 0x19C) < 2;
}

/* The turret's perceiver record and its sensors' bits (an entry's sense bits name the sensors that see it now). */
static uint8_t* Perceiver(BehaviourCtx* ctx, uint32_t* senses)
{
    uintptr_t manager = *(uintptr_t*)(g_bhvGame + BHV_PERCEPTION);
    int64_t index = manager != 0 ? BhvMapFind(manager, 0x30, *(uint32_t*)(ctx->p + 0x60)) : -1;
    if (index < 0) return NULL;
    uint8_t* record = *(uint8_t**)(manager + 0x50) + (size_t)index * 0x13F8;
    uint32_t sensors = *(uint32_t*)record, mask = 0;
    for (uint32_t k = 0; k < sensors && k < 32; k++) mask |= 1u << (*(uint32_t*)(record + 8 + 8 * k) & 31);
    *senses = mask;
    return record;
}

/* Whether a perceiver entry is something the turret should shoot: seen now, valid and alive, targetable, hostile, in
 * range, not near a player, not rejected. */
static int Acceptable(const uint8_t* perceiver, uint32_t senses, const uint8_t* entry, const float self[3], const TurretMind* m, uint64_t now)
{
    uint32_t entity = *(const uint32_t*)entry, invalid = *(uint32_t*)(g_bhvGame + BHV_INVALID_ENTITY);
    if (entity == 0 || entity == invalid || Rejected(m, entity, now)) return 0;
    if ((*(const uint32_t*)(entry + 0x4C) & senses) == 0) return 0;
    if (!((BhvValidFn)BhvFn(F_VALID))(NULL, entity, NULL)) return 0;
    uint64_t flags = 0;
    if (ManagerValue(BHV_FLAGS, 0x18, 0x38, 8, entity, &flags) && (flags >> 35 & 1)) return 0;
    uint32_t faction = 0;
    if (!ManagerValue(BHV_FACTIONS, 0x12050, 0x12078, 4, entity, &faction) || faction == 0 || (faction & *(const uint32_t*)(perceiver + 0x13E8))) return 0;
    int32_t health;
    float since;
    if (!Health(entity, &health, &since)) return 0;
    const float* at = (const float*)(entry + 4);
    float dx = at[0] - self[0], dy = at[1] - self[1], dz = at[2] - self[2];
    if (dx * dx + dy * dy + dz * dz >= T_RANGE * T_RANGE) return 0;
    return 1;
}

/* The best entry to shoot: given `around` (where a target was just let go), the nearest to it (within 25 m, then 50 m,
 * then any); otherwise the nearest to the turret. The gun's turn only breaks ties. */
static const uint8_t* Pick(BehaviourCtx* ctx, const uint8_t* perceiver, uint32_t senses, const float self[3], const TurretMind* m,
    const float* around, uint64_t now)
{
    static const uint32_t lists[3] = {0x310, 0x818, 0xD20};
    const uint8_t* best = NULL;
    float bestScore = 0.0f;
    for (int l = 0; l < 4; l++)
    {
        uint32_t count = l < 3 ? *(const uint32_t*)(perceiver + lists[l]) : 3;
        for (uint32_t i = 0; i < count && i < 16; i++)
        {
            const uint8_t* entry = l < 3 ? perceiver + lists[l] + 8 + (size_t)i * 0x50 : perceiver + 0x1228 + (size_t)i * 0x50;
            if (l == 3 && *(const uint32_t*)(entry + 0x48) == 0) continue;
            STEP(80);
            if (!Acceptable(perceiver, senses, entry, self, m, now)) continue;
            STEP(81);
            const float* at = (const float*)(entry + 4);
            const float* from = around != NULL ? around : self;
            float dx = at[0] - from[0], dy = at[1] - from[1], dz = at[2] - from[2], distance = sqrtf(dx * dx + dy * dy + dz * dz);
            float score = distance + 0.05f * ((BhvDegreesFn)BhvFn(F_DEGREES))(NULL, EntityOf(ctx), at);
            if (around != NULL) score += distance <= 25.0f ? 0.0f : distance <= 50.0f ? 1000.0f : 2000.0f;
            if (best == NULL || score < bestScore) { best = entry; bestScore = score; }
        }
    }
    return best;
}

static void StageTurret(BehaviourCtx* ctx, int stage) { ((BhvCtxIntFn)BhvFn(F_645_STAGE))(ctx, stage); }

/* Where the turret's last target was, while that still guides the next choice. */
static const float* Around(const TurretMind* m, uint64_t now)
{
    return m->hasLockPosition && (m->lock != 0 || now < m->lostAt + T_RECENT) ? m->lockPosition : NULL;
}

typedef uint8_t* (*BhvDescriptorFn)(uint32_t entity);

static int OwnAim(uint32_t entity)
{
    const uint8_t* descriptor = ((BhvDescriptorFn)BhvFn(F_DESCRIPTOR))(entity);
    if (descriptor == NULL) return 0;
    uint64_t type = *(const uint64_t*)descriptor;
    for (size_t i = 0; i < sizeof kOwnAim / sizeof kOwnAim[0]; i++) if (type == kOwnAim[i]) return 1;
    return 0;
}

typedef const float* (*BhvNodePositionFn)(uint32_t unit, uint32_t node);

/* An entity's live root (its model's node 0, from the scene graph, as 0x4BC5D0 reads the behaviour's own). */
static int Root(uint32_t entity, float out[3])
{
    const uint8_t* descriptor = ((BhvDescriptorFn)BhvFn(F_DESCRIPTOR))(entity);
    uint32_t unit = descriptor != NULL ? *(const uint32_t*)(descriptor + 0xC) : 0;
    uint8_t* api = *(uint8_t**)(g_bhvGame + BHV_ENGINE_API);
    uint8_t* graph = api != NULL ? *(uint8_t**)(api + 0x18) : NULL;
    if (unit == 0 || graph == NULL) return 0;
    const float* at = ((BhvNodePositionFn)*(void**)(graph + 0x88))(unit, 0);
    if (at == NULL) return 0;
    memcpy(out, at, 12);
    return 1;
}

/* The lowered aim point for P's target (the behaviour's aim height above its live root), or 0 to keep the game's. */
static int Lowered(BehaviourCtx* ctx, const Behaviour* b, float out[3])
{
    uint32_t target = *(uint32_t*)(ctx->p + 0x10);
    if (b->aimHeight <= 0.0f || ctx->p[0x70] == 0 || target == *(uint32_t*)(g_bhvGame + BHV_INVALID_ENTITY) || OwnAim(target) ||
        !Root(target, out))
        return 0;
    out[2] += b->aimHeight;
    return 1;
}

/* The point the gun aims at: the lowered one, or the game's (the target's aim node). */
static void AimPoint(BehaviourCtx* ctx, const Behaviour* b, const float self[3], float out[3])
{
    if (!Lowered(ctx, b, out)) ((BhvTargetFn)BhvFn(F_TARGET))(out, ctx, self);
}

/* Takes the target in P (after the setter, which may redirect it to the entity it belongs to) as the turret's lock;
 * `sighted` is the entity its perceiver lists know (what sight is judged on). */
static void Lock(BehaviourCtx* ctx, TurretMind* m, uint64_t now, uint32_t sighted)
{
    m->lock = *(uint32_t*)(ctx->p + 0x10);
    m->sighted = sighted;
    m->seenAt = m->progressAt = m->lockedAt = now;
    m->health = 0;
    m->sinceDamage = 0.0f;
    Health(m->lock, &m->health, &m->sinceDamage);
    memcpy(m->lockPosition, ctx->p + 0x14, 12);
    m->hasLockPosition = 1;
}

/* Lets the lock go (rejecting it for a while if asked) for the best target near it, or none. Returns 1 with a new one. */
static int Release(BehaviourCtx* ctx, TurretMind* m, const uint8_t* perceiver, uint32_t senses, const float self[3], uint64_t now,
    int reject, const char* why)
{
    if (reject && m->lock != 0) Reject(m, m->lock, now);
    uint32_t old = m->lock;
    if (m->lock != 0) m->lostAt = now;
    m->lock = 0;
    const uint8_t* entry = perceiver != NULL ? Pick(ctx, perceiver, senses, self, m, Around(m, now), now) : NULL;
    if (old == 0 && entry == NULL) return 0; /* nothing held, nothing found: the search already runs */
    if (m->releases++ < 40)
        Note("turret %u: let go of %u (%s); %s %u", EntityOf(ctx), old, why, entry != NULL ? "now" : "no other target",
            entry != NULL ? *(const uint32_t*)entry : 0);
    if (entry != NULL)
    {
        float turn = ((BhvDegreesFn)BhvFn(F_DEGREES))(NULL, EntityOf(ctx), (const float*)(entry + 4));
        ((BhvVectorFn)BhvFn(F_SET_TARGET))(ctx, (const float*)entry);
        Lock(ctx, m, now, *(const uint32_t*)entry);
        if (*(int32_t*)ctx->p == 3 && turn > T_FIRE_TRAVERSE) StageTurret(ctx, 2); /* too far a turn to keep firing */
        return 1;
    }
    ((BhvVectorFn)BhvFn(F_SET_TARGET))(ctx, NULL); /* 645's own clear */
    m->searchUntil = now + T_SEARCH;
    if (*(int32_t*)ctx->p == 3) StageTurret(ctx, 2); /* leaving 3 releases the trigger */
    return 0;
}

/* Keeps the lock while it is worth shooting: returns 1 with a target (the lock, or its replacement), 0 with none. */
static int Track(BehaviourCtx* ctx, TurretMind* m, const uint8_t* perceiver, uint32_t senses, const float self[3], uint64_t now, int firing)
{
    uint32_t target = *(uint32_t*)(ctx->p + 0x10), invalid = *(uint32_t*)(g_bhvGame + BHV_INVALID_ENTITY);
    int32_t health;
    float since;
    if (ctx->p[0x70] == 0 || target == invalid || !((BhvValidFn)BhvFn(F_VALID))(NULL, target, NULL) || !Health(target, &health, &since))
        return Release(ctx, m, perceiver, senses, self, now, 0, "dead or gone");
    if (target != m->lock) Lock(ctx, m, now, target);
    uintptr_t perception = *(uintptr_t*)(g_bhvGame + BHV_PERCEPTION);
    const uint8_t* entry = perception != 0 ? ((BhvFindEntryFn)BhvFn(F_FIND_ENTRY))(perception, *(uint32_t*)(ctx->p + 0x60), m->sighted) : NULL;
    if (entry != NULL)
    {
        uint64_t sensed = (*(const uint32_t*)(entry + 0x4C) & senses) ? now : *(const uint64_t*)(entry + 0x10);
        if (sensed > m->seenAt) m->seenAt = sensed;
        memcpy(m->lockPosition, entry + 4, 12);
    }
    if (now >= m->seenAt + T_GRACE) return Release(ctx, m, perceiver, senses, self, now, 1, "out of sight");
    if (firing)
    {
        if (health < m->health || since < m->sinceDamage) m->progressAt = now;
        m->health = health;
        m->sinceDamage = since;
        if (now >= m->progressAt + T_RELEASE) return Release(ctx, m, perceiver, senses, self, now, 1, "not hurting it");
    }
    else m->progressAt = now; /* only firing time counts */
    return 1;
}

/* The weapon's aim point, set lower than the game's for the enemy (its aim node, about 1.5 m above the feet: from a
 * Pelican's height, rounds aimed there pass over): the targeting record's aim override, as the game's own AI action code
 * sets it (the targeting update 0x6BBDC0 aims at the override's curve while its t >= 0: A +0x20 = B +0x2C = the point,
 * mode +0x60 2 (linear), no advance +0x6C, t +0x50 = 1, last; released with t = -1, as 0x6C0EB0 does). Written only
 * when the point moves 5 cm, never while the record's network bits are set or another user holds the override; every
 * machine computes its own (it is not replicated). HD2Runtime's Pelican Gatling: 0.7 m above the target's root. */
static void AimLow(BehaviourCtx* ctx, const Behaviour* b, TurretMind* m, int stage)
{
    if (b->aimHeight <= 0.0f) return;
    uintptr_t manager = *(uintptr_t*)(g_bhvGame + BHV_TARGETING);
    int64_t index = manager != 0 ? BhvMapFind(manager, 0x150, EntityOf(ctx)) : -1;
    if (index < 0) return;
    uint8_t* record = *(uint8_t**)(manager + 0x178) + (size_t)index * 0xD0;
    static int notes;
    uint32_t bits = *(uint32_t*)(*(uint8_t**)(manager + 0x180) + (size_t)index * 0x18 + 0x10);
    if (bits != 0) { if (notes++ < 3) Note("turret %u: aim override waits (network bits %08x)", EntityOf(ctx), bits); return; }
    float t = *(float*)(record + 0x50);
    uint32_t invalid = *(uint32_t*)(g_bhvGame + BHV_INVALID_ENTITY);
    float point[3];
    int want = (stage == 2 || stage == 3) && Lowered(ctx, b, point);
    (void)invalid;
    if (!want)
    {
        if (m->aiming && t >= 0.0f) *(float*)(record + 0x50) = -1.0f;
        m->aiming = 0;
        return;
    }
    if (!m->aiming && t >= 0.0f) { if (notes++ < 3) Note("turret %u: aim override in use by another (t %.2f)", EntityOf(ctx), (double)t); return; }
    const float* a = (const float*)(record + 0x20);
    float dx = a[0] - point[0], dy = a[1] - point[1], dz = a[2] - point[2];
    if (m->aiming && dx * dx + dy * dy + dz * dz < 0.05f * 0.05f) return;
    memcpy(record + 0x20, point, 12);
    memcpy(record + 0x2C, point, 12);
    if (!m->aiming)
    {
        *(uint32_t*)(record + 0x60) = 2;
        record[0x6C] = 0;
        *(float*)(record + 0x50) = 1.0f; /* the switch, last */
        m->aiming = 1;
        if (notes++ < 6) Note("turret %u: aims %.1f m above the feet of %u at (%.1f, %.1f, %.1f)", EntityOf(ctx), (double)b->aimHeight,
            *(uint32_t*)(ctx->p + 0x10), (double)point[0], (double)point[1], (double)point[2]);
    }
}

/* Refills the magazine from the behaviour's reserve before it runs dry (645 never reloads). */
static void Refill(BehaviourCtx* ctx, const Behaviour* b, TurretMind* m)
{
    if (!m->reserveSet) { m->reserve = b->reserveRounds; m->reserveSet = 1; }
    if (m->reserve <= 0 || !(*(uint32_t*)(ctx->entity + 0x14) & 1)) return; /* the machine with authority only */
    void* magazines = *(void**)(g_bhvGame + BHV_ROUNDS);
    uintptr_t refills = *(uintptr_t*)(g_bhvGame + BHV_MAGAZINES);
    if (magazines == NULL || refills == 0 || BhvMapFind(refills, 0x20, EntityOf(ctx)) < 0) return; /* the refill's own lookup */
    STEP(40);
    int32_t rounds = ((BhvRoundsFn)BhvFn(F_ROUNDS))(magazines, EntityOf(ctx));
    STEP(41);
    int32_t capacity = ((BhvRoundsFn)BhvFn(F_CAPACITY))(NULL, EntityOf(ctx));
    if (rounds > T_REFILL_BELOW) m->fullSeen = 1; /* never at spawn, before its magazine is filled */
    if (!m->fullSeen || rounds > T_REFILL_BELOW || capacity <= rounds) return;
    STEP(42);
    int32_t add = capacity - rounds < m->reserve ? capacity - rounds : m->reserve;
    ((BhvRefillFn)BhvFn(F_REFILL))(NULL, EntityOf(ctx), add);
    m->reserve -= add;
}

/* 645's update (0x478D9D) with HellForge choosing the target. */
static void TurretUpdate(BehaviourCtx* ctx, const Behaviour* b, float dt)
{
    uint8_t* p = ctx->p;
    uint64_t now = BhvNow();
    TurretMind* m = MindOf(EntityOf(ctx), now);
    *(uint64_t*)(p + 0x90) = now + T_HOLD_RESCORE; /* 645 does not re-score: HellForge picks */
    uint32_t senses = 0;
    STEP(30);
    const uint8_t* perceiver = Perceiver(ctx, &senses);
    STEP(31);
    int active = ((BhvCtxTestFn)BhvFn(F_645_ACTIVE))(ctx) != 0;
    STEP(32);
    int ammunition = ((BhvCtxTestFn)BhvFn(F_645_AMMO))(ctx) != 0;
    Refill(ctx, b, m);
    float self[3];
    STEP(33);
    ((BhvPositionFn)BhvFn(F_POSITION))(self, ctx);
    STEP(34);
    switch (*(int32_t*)p)
    {
    case 1: /* idle: 645 rests the gun; HellForge looks for a target */
        STEP(50);
        ((BhvCtxTimeFn)BhvFn(F_645_IDLE))(ctx, dt);
        STEP(51);
        if (*(int32_t*)p == 1 && active && ammunition && perceiver != NULL && now >= m->nextPick)
        {
            m->nextPick = now + T_PICK;
            const uint8_t* entry = Pick(ctx, perceiver, senses, self, m, Around(m, now), now);
            if (entry != NULL)
            {
                ((BhvVectorFn)BhvFn(F_SET_TARGET))(ctx, (const float*)entry);
                Lock(ctx, m, now, *(const uint32_t*)entry);
                StageTurret(ctx, 2);
            }
        }
        break;
    case 2: /* aim: 645's (0x44FE20) without its 1.5 s wait or its re-score: fire once within 2 degrees of the aim point */
        if (!active || !ammunition) { StageTurret(ctx, 4); break; }
        if (!Track(ctx, m, perceiver, senses, self, now, 0))
        {
            const uint8_t* entry = perceiver != NULL && now < m->searchUntil ? Pick(ctx, perceiver, senses, self, m, Around(m, now), now) : NULL;
            if (entry == NULL) { if (now >= m->searchUntil) StageTurret(ctx, 1); break; }
            ((BhvVectorFn)BhvFn(F_SET_TARGET))(ctx, (const float*)entry);
            Lock(ctx, m, now, *(const uint32_t*)entry);
        }
        STEP(60);
        {
            float point[3];
            AimPoint(ctx, b, self, point);
            /* Aim time counts from the stage or the current lock, whichever is later: a replacement gets its own. */
            uint64_t aimed = *(uint64_t*)(p + 0x178) > m->lockedAt ? *(uint64_t*)(p + 0x178) : m->lockedAt;
            if (now >= aimed + T_AIM_MIN && ((BhvFireGateFn)BhvFn(F_FIRE_GATE))(ctx, point, T_AIM_GATE)) StageTurret(ctx, 3);
            else if (now >= aimed + T_AIM_TIMEOUT) Release(ctx, m, perceiver, senses, self, now, 1, "cannot aim at it");
        }
        STEP(61);
        break;
    case 3: /* fire */
        if (!active) StageTurret(ctx, 4);
        else if (!ammunition) StageTurret(ctx, 1);
        else if (b->burstMicroseconds != 0 && now >= *(uint64_t*)(p + 0x180) + b->burstMicroseconds) StageTurret(ctx, 2);
        else { STEP(70); Track(ctx, m, perceiver, senses, self, now, 1); }
        break;
    case 4: /* inactive */
        if (active) StageTurret(ctx, 1);
        break;
    }
    STEP(90);
    AimLow(ctx, b, m, *(int32_t*)p);
}

static const Behaviour* BehaviourOf(int id)
{
    uint32_t i = (uint32_t)id - BHV_FIRST;
    return i < g_behaviourCount ? &g_behaviours[i] : NULL;
}

/* Co-op diagnostics (2026-10-18): a Pelican on a HellForge behaviour (694) flew only on the machine that called it; on
 * the others its behaviour never updated (they saw it on the ground), while its turret's (695) did on one of them. For
 * every entity with a HellForge behaviour that spawns on this machine, the behaviour manager's view of it is logged at
 * the spawn and 1, 5 and 15 s later: whether it has a record, the record's id and stage (and pending stage), and its
 * entity info flags (bit 0 authority, bit 1 skipped by the manager). Read only, from the watch thread, through SafeRead. */
#define BHV_SEEN_MAX 32
typedef struct BhvSeen { uint32_t entity, behaviour, network; uint64_t resource; ULONGLONG spawned; int authority, step; } BhvSeen;
static BhvSeen g_bhvSeen[BHV_SEEN_MAX];
static volatile LONG g_bhvSeenNext;
static int g_bhvSeenLines;

/* The HellForge behaviour an entity resource runs (add_entity's), or 0. */
static uint32_t BehaviourOfResource(uint64_t resource)
{
    for (int i = 0; i < g_weaponCount; i++)
        if (g_weapons[i].plain && g_weapons[i].entity == resource && g_weapons[i].behaviour >= BHV_FIRST) return g_weapons[i].behaviour;
    return 0;
}

/* Whether any entity runs a HellForge behaviour: the spawn hook is then wanted for the diagnostics. */
static int BehaviourSpawnsWatched(void)
{
    for (int i = 0; i < g_weaponCount; i++) if (g_weapons[i].plain && g_weapons[i].behaviour >= BHV_FIRST) return 1;
    return 0;
}

/* The spawn hook (event_sources.c), on the game thread: an entity of ours spawned here. */
static void BehaviourSpawned(uint32_t entity, uint64_t resource, const uint8_t* info)
{
    uint32_t behaviour = BehaviourOfResource(resource);
    if (behaviour == 0) return;
    LONG slot = InterlockedIncrement(&g_bhvSeenNext) - 1;
    BhvSeen* s = &g_bhvSeen[slot % BHV_SEEN_MAX];
    BhvSeen seen = {entity, behaviour, 0, resource, GetTickCount64(), info != NULL && info[0] != 0, 0};
    if (info != NULL) memcpy(&seen.network, info + 4, 4);
    *s = seen;
}

/* An entity's behaviour record as the manager has it, read safely (another thread). */
static void BehaviourSeenLog(const BhvSeen* s, const char* when)
{
    uintptr_t manager = 0, table = 0, infos = 0, records = 0, info = 0;
    uint32_t capacity = 0, empty = 0, multiplier = 0, index = 0xFFFFFFFFu;
    if (g_bhvGame == 0 || SafeRead(g_bhvGame + BHV_MANAGER, &manager, 8) || manager == 0 || SafeRead(manager + 0x40, &table, 8) ||
        SafeRead(manager + 0x48, &capacity, 4) || SafeRead(manager + 0x4C, &empty, 4) || SafeRead(manager + 0x50, &multiplier, 4))
    {
        Note("behaviours: co-op check %s: entity %u (behaviour %u): the behaviour manager cannot be read", when, s->entity, s->behaviour);
        return;
    }
    for (uint32_t i = 0; table != 0 && capacity != 0 && i < capacity; i++)
    {
        uint32_t slot = (i + multiplier * s->entity) & (capacity - 1), pair[2];
        if (SafeRead(table + (uintptr_t)slot * 8, pair, 8)) break;
        if (pair[0] == s->entity) { index = pair[1]; break; }
        if (pair[0] == empty) break;
    }
    if (index == 0xFFFFFFFFu)
    {
        Note("behaviours: co-op check %s: entity %u (behaviour %u, %s, network %u): no behaviour record on this machine", when,
            s->entity, s->behaviour, s->authority ? "authority here" : "not authority", s->network);
        return;
    }
    int32_t record[4] = {0, 0, 0, 0}; /* id, (padding), P+0 stage, P+4 pending stage */
    uint32_t flags = 0;
    SafeRead(manager + 0x60, &records, 8);
    SafeRead(manager + 0x58, &infos, 8);
    if (records != 0) SafeRead(records + (uintptr_t)index * 0x1F8, record, 16);
    int flagsRead = infos != 0 && !SafeRead(infos + (uintptr_t)index * 8, &info, 8) && info != 0 && !SafeRead(info + 0x14, &flags, 4);
    if (!flagsRead)
    {
        Note("behaviours: co-op check %s: entity %u (behaviour %u, network %u): its entity info does not read (record id %d, stage %d)",
            when, s->entity, s->behaviour, s->network, record[0], record[2]);
        CoopDiag("behaviour %u on entity %u %s (network %u): authority unknown, its entity info does not read", s->behaviour, s->entity, when, s->network);
        return;
    }
    if (flags & 1)
    {
        Note("behaviours: co-op check %s: entity %u (behaviour %u, %s, network %u): record id %d, stage %d (pending %d), flags %x%s%s",
            when, s->entity, s->behaviour, s->authority ? "authority at spawn" : "not authority at spawn", s->network, record[0], record[2],
            record[3], flags, (flags & 2) ? " (skipped by the manager)" : "", record[0] == 0 ? " (no behaviour: never updated)" : "");
        CoopDiag("behaviour %u on entity %u %s (authority here, network %u): record id %d, stage %d (pending %d), flags %x%s", s->behaviour,
            s->entity, when, s->network, record[0], record[2], record[3], flags, record[0] == 0 ? ": NEVER UPDATED on this machine" : "");
        return;
    }
    /* Without authority the manager never runs the record (co-op research 2026-10-21): what the
     * authority sends lands in the snapshot ([manager +0x68] + index * 0xA0: +0 stage, +4 behaviour id; 0x845B60), and
     * becomes the record only at a migration. Its stamps (+0x90, +0x94, +0x98) change as updates arrive. */
    uintptr_t snapshots = 0;
    int32_t snapshot[2] = {0, 0};
    uint32_t stamps[3] = {0, 0, 0};
    if (!SafeRead(manager + 0x68, &snapshots, 8) && snapshots != 0)
    {
        SafeRead(snapshots + (uintptr_t)index * 0xA0, snapshot, 8);
        SafeRead(snapshots + (uintptr_t)index * 0xA0 + 0x90, stamps, 12);
    }
    Note("behaviours: co-op check %s: entity %u (behaviour %u, not authority here, network %u): the authority's state received: "
        "behaviour %d, stage %d (stamps %u/%u/%u), flags %x%s", when, s->entity, s->behaviour, s->network, snapshot[1], snapshot[0],
        stamps[0], stamps[1], stamps[2], flags, snapshot[1] == 0 ? " (nothing received yet)" : "");
    CoopDiag("behaviour %u on entity %u %s (NOT authority here, network %u): received behaviour %d, stage %d (stamps %u/%u/%u)%s", s->behaviour,
        s->entity, when, s->network, snapshot[1], snapshot[0], stamps[0], stamps[1], stamps[2],
        snapshot[1] == 0 ? ": NOTHING RECEIVED from the authority" : "");
}

/* The watch loop: each spawned entity of ours, once at the spawn and 1, 5 and 15 s later (at most 400 lines a session). */
static void BehaviourSeenTick(void)
{
    static const ULONGLONG delays[] = {0, 1000, 5000, 15000};
    static const char* names[] = {"at spawn", "after 1 s", "after 5 s", "after 15 s"};
    ULONGLONG now = GetTickCount64();
    for (int i = 0; i < BHV_SEEN_MAX && g_bhvSeenLines < 400; i++)
    {
        BhvSeen* s = &g_bhvSeen[i];
        if (s->behaviour == 0 || s->step >= 4 || now < s->spawned + delays[s->step]) continue;
        BhvSeen copy = *s;
        BehaviourSeenLog(&copy, names[s->step]);
        s->step++;
        g_bhvSeenLines++;
    }
}

static void BehaviourUpdate(BehaviourCtx* ctx, int id, float dt)
{
    const Behaviour* b = BehaviourOf(id);
    if (b == NULL) return;
    static uint32_t announced;
    if (!(announced >> (id - BHV_FIRST) & 1))
    {
        announced |= 1u << (id - BHV_FIRST);
        Note("behaviours: behaviour %d runs (entity %u, stage %d, thread %lu)", id, EntityOf(ctx), *(int32_t*)ctx->p, GetCurrentThreadId());
    }
    g_bhvThread = GetCurrentThreadId();
    g_bhvId = (uint32_t)id;
    g_bhvStage = *(int32_t*)ctx->p;
    g_bhvStep = 1;
    g_bhvCallAt = GetTickCount64();
    if (b->base->id == BHV_PELICAN_TRANSPORT) TransportUpdate(ctx, b);
    else TurretUpdate(ctx, b, dt);
    g_bhvCallAt = 0;
}

/* Called by the loader's watcher: reports a HellForge behaviour call that has not returned in 2 s, with where the game
 * thread is (its instruction pointer, briefly suspended), once per call. */
static void BehaviourWatch(void)
{
    static ULONGLONG reported;
    ULONGLONG at = g_bhvCallAt;
    if (at == 0 || at == reported || GetTickCount64() - at < 2000) return;
    reported = at;
    uintptr_t rip = 0, rsp = 0;
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, g_bhvThread);
    uint64_t stack[8] = {0};
    if (thread != NULL)
    {
        if (SuspendThread(thread) != (DWORD)-1)
        {
            CONTEXT context = {0};
            context.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(thread, &context)) { rip = context.Rip; rsp = context.Rsp; }
            if (rsp != 0) SafeRead(rsp, stack, sizeof stack);
            ResumeThread(thread);
        }
        CloseHandle(thread);
    }
    HMODULE module = NULL;
    char name[MAX_PATH] = "?";
    if (rip != 0 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)rip, &module))
        GetModuleFileNameA(module, name, sizeof name);
    const char* file = strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name;
    Note("behaviours: behaviour %u (stage %d) has not returned in %llu ms, at step %u; thread %lu at %s+0x%llx (stack %016llx %016llx %016llx %016llx %016llx %016llx)",
        g_bhvId, g_bhvStage, (unsigned long long)(GetTickCount64() - at), g_bhvStep, g_bhvThread, file,
        (unsigned long long)(rip - (uintptr_t)module), (unsigned long long)stack[0], (unsigned long long)stack[1],
        (unsigned long long)stack[2], (unsigned long long)stack[3], (unsigned long long)stack[4], (unsigned long long)stack[5]);
}

/* Diagnostics for a hang anywhere: when the game's clock stops for 3 s after it has run, every thread executing in
 * game code is briefly suspended and logged (its position and the game return addresses on its stack), once per stop. */
static int InGame(uintptr_t at, uintptr_t game, size_t gameSize, uintptr_t exe, size_t exeSize, const char** module, uintptr_t* offset)
{
    if (at >= game && at < game + gameSize) { *module = "game.dll"; *offset = at - game; return 1; }
    if (at >= exe && at < exe + exeSize) { *module = "helldivers2.exe"; *offset = at - exe; return 1; }
    return 0;
}

/* The loaded module an address lies in, as "name+0xoffset" (0 when it lies in none): the crash note names the code
 * outside the game a fault came from (2026-10-21: a host's render thread called address 0 from a module at a fixed
 * address in two sessions; the note could not say which). */
static int ModuleAt(uintptr_t at, char* out, size_t size)
{
    HMODULE module = NULL;
    wchar_t path[MAX_PATH];
    if (at < 0x10000 || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)at, &module) ||
        module == NULL || !GetModuleFileNameW(module, path, MAX_PATH)) return 0;
    const wchar_t* name = wcsrchr(path, L'\\');
    snprintf(out, size, "%ls+0x%llx", name != NULL ? name + 1 : path, (unsigned long long)(at - (uintptr_t)module));
    return 1;
}

static void FreezeWatch(void)
{
    static uint64_t lastClock;
    static ULONGLONG lastChange;
    static int ran, reported;
    size_t gameSize = 0;
    uintptr_t game = ApiModuleBase("game.dll", &gameSize), clock = 0;
    uint64_t value = 0;
    if (game == 0 || SafeRead(game + BHV_CLOCK, &clock, 8) || clock == 0 || SafeRead(clock + 0x18, &value, 8)) return;
    ULONGLONG now = GetTickCount64();
    if (value != lastClock) { if (lastClock != 0) ran = 1; lastClock = value; lastChange = now; reported = 0; return; }
    if (!ran || reported || now - lastChange < 3000) return;
    reported = 1;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    size_t exeSize = ((IMAGE_NT_HEADERS64*)(exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew))->OptionalHeader.SizeOfImage;
    Note("freeze: the game's clock has not moved for %llu ms; threads in game code:", (unsigned long long)(now - lastChange));
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry = {sizeof entry};
    DWORD self = GetCurrentThreadId(), process = GetCurrentProcessId();
    int logged = 0;
    for (BOOL more = Thread32First(snapshot, &entry); more && logged < 12; more = Thread32Next(snapshot, &entry))
    {
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
        if (thread == NULL) continue;
        CONTEXT context = {0};
        context.ContextFlags = CONTEXT_CONTROL;
        uint64_t stack[96] = {0};
        int got = 0;
        if (SuspendThread(thread) != (DWORD)-1)
        {
            if (GetThreadContext(thread, &context)) { got = 1; SafeRead(context.Rsp, stack, sizeof stack); }
            ResumeThread(thread);
        }
        CloseHandle(thread);
        const char* module;
        uintptr_t offset;
        if (!got || !InGame(context.Rip, game, gameSize, exe, exeSize, &module, &offset)) continue;
        char trail[512];
        int n = 0;
        for (int k = 0, found = 0; k < 96 && found < 10 && n < (int)sizeof trail - 40; k++)
        {
            const char* m;
            uintptr_t o;
            if (InGame(stack[k], game, gameSize, exe, exeSize, &m, &o)) { n += snprintf(trail + n, sizeof trail - n, " %s+0x%llx", m[0] == 'g' ? "g" : "x", (unsigned long long)o); found++; }
        }
        trail[n] = 0;
        Note("freeze: thread %lu at %s+0x%llx; stack:%s", entry.th32ThreadID, module, (unsigned long long)offset, trail);
        logged++;
    }
    CloseHandle(snapshot);
}

/* A fault inside a HellForge behaviour call: logged with its step, then left to the game's own handling. */
static LONG CALLBACK BehaviourFault(EXCEPTION_POINTERS* info)
{
    static int reported;
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (g_bhvCallAt == 0 || GetCurrentThreadId() != g_bhvThread || reported >= 4 ||
        (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_STACK_OVERFLOW))
        return EXCEPTION_CONTINUE_SEARCH;
    reported++;
    uintptr_t at = (uintptr_t)info->ExceptionRecord->ExceptionAddress;
    Note("behaviours: fault %08lx in behaviour %u (stage %d) at step %u, address 0x%llx (game.dll+0x%llx), reading 0x%llx",
        code, g_bhvId, g_bhvStage, g_bhvStep, (unsigned long long)at, (unsigned long long)(at - g_bhvGame),
        (unsigned long long)(info->ExceptionRecord->NumberParameters > 1 ? info->ExceptionRecord->ExceptionInformation[1] : 0));
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Any access violation or illegal instruction: where it happened (game.dll / exe offset or raw), what it read, the main
 * registers and the game addresses on the stack, then left to the game's own handling. The game's crash reporter does
 * not always get to write its dump; this is the record the loader keeps. The first 4 only. */
/* Diagnostics (the launcher's Crash dump setting: HELLFORGE_CRASH_DUMP names HellForge.Cli.exe): a full memory dump of the
 * game at its first crash, beside the session's logs. It is written from outside (HellForge.Cli crash-dump), while this
 * thread waits: a process dumping itself from its exception handler can deadlock on a lock a suspended thread holds. */
static void WriteCrashDump(EXCEPTION_POINTERS* info)
{
    wchar_t cli[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"HELLFORGE_CRASH_DUMP", cli, MAX_PATH);
    if (length == 0 || length >= MAX_PATH || g_logs[0] == 0) return;
    wchar_t command[3 * MAX_PATH];
    swprintf(command, sizeof command / sizeof command[0], L"\"%ls\" crash-dump --pid %lu --thread %lu --pointers %llx --out \"%ls\\crash-%lu.dmp\"",
        cli, GetCurrentProcessId(), GetCurrentThreadId(), (unsigned long long)(uintptr_t)info, g_logs, GetCurrentProcessId());
    STARTUPINFOW startup = {sizeof startup};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(cli, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process))
    {
        Note("crash dump: could not start %ls (%lu)", cli, GetLastError());
        return;
    }
    DWORD waited = WaitForSingleObject(process.hProcess, 120000), exit = 1;
    GetExitCodeProcess(process.hProcess, &exit);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Note(waited == WAIT_OBJECT_0 && exit == 0 ? "crash dump: written (thread %lu)" : "crash dump: not written (thread %lu, exit %lu)", GetCurrentThreadId(), exit);
}
static LONG CALLBACK CrashNote(EXCEPTION_POINTERS* info)
{
    static volatile LONG reported;
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if ((code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION) || InterlockedIncrement(&reported) > 4)
        return EXCEPTION_CONTINUE_SEARCH;
    if (reported == 1) WriteCrashDump(info);
    size_t gameSize = 0;
    uintptr_t game = ApiModuleBase("game.dll", &gameSize);
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    size_t exeSize = ((IMAGE_NT_HEADERS64*)(exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew))->OptionalHeader.SizeOfImage;
    uintptr_t at = (uintptr_t)info->ExceptionRecord->ExceptionAddress;
    const char* module = "?";
    uintptr_t offset = at;
    InGame(at, game, gameSize, exe, exeSize, &module, &offset);
    CONTEXT* c = info->ContextRecord;
    char trail[1024];
    int n = 0, found = 0;
    uintptr_t stack[256];
    if (!SafeRead(c->Rsp, stack, sizeof stack))
        for (int k = 0; k < 256 && found < 24 && n < (int)sizeof trail - 40; k++)
        {
            const char* m;
            uintptr_t o;
            if (InGame(stack[k], game, gameSize, exe, exeSize, &m, &o)) { n += snprintf(trail + n, sizeof trail - n, " %s+0x%llx", m[0] == 'g' ? "g" : "x", (unsigned long long)o); found++; }
        }
    trail[n] = 0;
    Note("crash: %08lx on thread %lu at %s+0x%llx (0x%llx), %s 0x%llx; rax %llx rbx %llx rcx %llx rdx %llx rsi %llx rdi %llx r8 %llx "
        "r9 %llx r12 %llx r13 %llx r14 %llx r15 %llx; stack:%s", code, GetCurrentThreadId(), module, (unsigned long long)offset,
        (unsigned long long)at, info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
        (unsigned long long)(info->ExceptionRecord->NumberParameters > 1 ? info->ExceptionRecord->ExceptionInformation[1] : 0),
        (unsigned long long)c->Rax, (unsigned long long)c->Rbx, (unsigned long long)c->Rcx, (unsigned long long)c->Rdx,
        (unsigned long long)c->Rsi, (unsigned long long)c->Rdi, (unsigned long long)c->R8, (unsigned long long)c->R9,
        (unsigned long long)c->R12, (unsigned long long)c->R13, (unsigned long long)c->R14, (unsigned long long)c->R15, trail);
    /* the same, named by module: where the fault is, the registers that point into a module, and the stack's values in
     * modules other than the game (a caller outside it: a driver, an overlay, a script loader) */
    {
        char where[160] = "", names[1000];
        int m = snprintf(names, sizeof names, "crash: by module: at %s", ModuleAt(at, where, sizeof where) ? where : "no module");
        const struct { const char* name; DWORD64 value; } regs[] = {{"rax", c->Rax}, {"rbx", c->Rbx}, {"rcx", c->Rcx}, {"rdx", c->Rdx},
            {"rsi", c->Rsi}, {"rdi", c->Rdi}, {"r8", c->R8}, {"r9", c->R9}, {"r12", c->R12}, {"r13", c->R13}, {"r14", c->R14}, {"r15", c->R15}};
        for (size_t k = 0; k < sizeof regs / sizeof regs[0] && m < (int)sizeof names - 200; k++)
            if (ModuleAt((uintptr_t)regs[k].value, where, sizeof where)) m += snprintf(names + m, sizeof names - m, "; %s %s", regs[k].name, where);
        int listed = 0;
        if (!SafeRead(c->Rsp, stack, sizeof stack))
        {
            m += snprintf(names + m, sizeof names - m, "; stack outside the game:");
            for (int k = 0; k < 256 && listed < 16 && m < (int)sizeof names - 200; k++)
            {
                const char* inGame;
                uintptr_t o;
                if (InGame(stack[k], game, gameSize, exe, exeSize, &inGame, &o) || !ModuleAt(stack[k], where, sizeof where)) continue;
                m += snprintf(names + m, sizeof names - m, " %s", where);
                listed++;
            }
        }
        Note("%s%s", names, listed == 0 ? " none" : "");
    }
    /* GenerateObjectiveLocations (game.dll 0xF8F670) reads a mission row from its objective list (r12: +0 row, +4 ids,
     * +0x54 starts, +0x94 counts, +0xD4 count): the list and what surrounds it, 0x40 bytes a line. */
    if (module[0] == 'g' && offset == 0xF8F670)
        for (int k = -4; k < 8; k++)
        {
            uint8_t line[0x40];
            char hex[0x40 * 2 + 1];
            if (SafeRead(c->R12 + (intptr_t)k * 0x40, line, sizeof line)) continue;
            for (int i = 0; i < (int)sizeof line; i++) snprintf(hex + 2 * i, 3, "%02x", line[i]);
            Note("crash: objective list %+04x: %s", k * 0x40, hex);
        }
    /* GenerateResourceHash (game.dll 0xC176B8) reads each location record's (0xE0 bytes, +0x90 at rbx) resource: the
     * record without one, its neighbours, and how many of the rest (rdi) have none. */
    if (module[0] == 'g' && offset == 0xC176B8)
    {
        for (int k = -1; k <= 1; k++)
        {
            uint8_t record[0xE0];
            char hex[0xE0 * 2 + 1];
            if (SafeRead(c->Rbx - 0x90 + (intptr_t)k * 0xE0, record, sizeof record)) continue;
            for (int i = 0; i < (int)sizeof record; i++) snprintf(hex + 2 * i, 3, "%02x", record[i]);
            Note("crash: location record %+d: %s", k, hex);
        }
        int missing = 0;
        for (uint64_t i = 0; i < c->Rdi && i < 4096; i++)
        {
            uint64_t resource = 1;
            if (!SafeRead(c->Rbx + i * 0xE0, &resource, 8) && resource == 0) missing++;
        }
        Note("crash: %d of the %llu remaining location records have no resource", missing, (unsigned long long)c->Rdi);
        /* The generation block (G = [r15 + 0x28]) and every count of its arrays (G+0x11A7070..+0x11A7270). */
        uintptr_t block = 0;
        uint32_t counts[0x80];
        if (!SafeRead(c->R15 + 0x28, &block, 8) && !SafeRead(block + 0x11A7070, counts, sizeof counts))
        {
            char text[2048];
            int len = 0;
            for (int i = 0; i < 0x80 && len < (int)sizeof text - 32; i++)
                if (counts[i] != 0) len += snprintf(text + len, sizeof text - len, " %x=%u", 0x11A7070 + 4 * i, counts[i]);
            text[len] = 0;
            Note("crash: generation block 0x%llx, counts:%s", (unsigned long long)block, text);
            /* Where the locations are: GenerateResourceHash's add rbx, A + 0x90 (0xC176A7; moved for larger maps). */
            uint32_t locations = 0x758E20;
            if (!SafeRead(game + 0xC176A7 + 3, &locations, 4)) locations -= 0x90;
            /* The locations (G+0x758E20, 0xE0 each, 2740 places) by kind (+0xD8/+0xCC) and the 16 commonest type hashes
             * (+0xA0); records past the 2740th sit over the next arrays and may be overwritten. */
            struct { uint32_t d8, cc, count, past; } kinds[64];
            static struct { uint64_t type; uint32_t d8, count; } types[512];
            int nk = 0, nt = 0;
            uint32_t total = counts[(0x11A7108 - 0x11A7070) / 4];
            for (uint32_t i = 0; i < total && i < 8192; i++)
            {
                uint8_t record[0xE0];
                if (SafeRead(block + locations + (uintptr_t)i * 0xE0, record, sizeof record)) break;
                uint32_t d8, cc; uint64_t type;
                memcpy(&d8, record + 0xD8, 4); memcpy(&cc, record + 0xCC, 4); memcpy(&type, record + 0xA0, 8);
                int k = 0;
                while (k < nk && !(kinds[k].d8 == d8 && kinds[k].cc == cc)) k++;
                if (k == nk && nk < 64) { kinds[nk].d8 = d8; kinds[nk].cc = cc; kinds[nk].count = kinds[nk].past = 0; nk++; }
                if (k < nk) { kinds[k].count++; if (i >= 2740) kinds[k].past++; }
                int t = 0;
                while (t < nt && types[t].type != type) t++;
                if (t == nt && nt < 512) { types[nt].type = type; types[nt].d8 = d8; types[nt].count = 0; nt++; }
                if (t < nt) types[t].count++;
            }
            len = 0;
            for (int k = 0; k < nk && len < (int)sizeof text - 48; k++)
                len += snprintf(text + len, sizeof text - len, " %u/%u=%u(%u past)", kinds[k].d8, kinds[k].cc, kinds[k].count, kinds[k].past);
            text[len] = 0;
            Note("crash: %u locations by kind (+0xD8/+0xCC):%s", total, text);
            len = 0;
            for (int shown = 0; shown < 16; shown++)
            {
                int best = -1;
                for (int t = 0; t < nt; t++) if (types[t].count && (best < 0 || types[t].count > types[best].count)) best = t;
                if (best < 0 || len >= (int)sizeof text - 48) break;
                len += snprintf(text + len, sizeof text - len, " %016llx(%u)=%u", (unsigned long long)types[best].type, types[best].d8, types[best].count);
                types[best].count = 0;
            }
            text[len] = 0;
            Note("crash: %d location types, the commonest (type(kind)=count):%s", nt, text);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* A fail-fast exit (0xC0000409: a stack cookie check, abort, an invalid parameter) skips the exception handlers; the
 * runtime asks IsProcessorFeaturePresent(PF_FASTFAIL_AVAILABLE) just before it. That question logs the game addresses
 * on the asking thread's stack, then is answered as usual. */
static BOOL (WINAPI* g_isProcessorFeaturePresent)(DWORD);
static BOOL WINAPI FeatureDetour(DWORD feature)
{
    static volatile LONG reported;
    if (feature == 23 /* PF_FASTFAIL_AVAILABLE */ && InterlockedIncrement(&reported) <= 4)
    {
        size_t gameSize = 0;
        uintptr_t game = ApiModuleBase("game.dll", &gameSize);
        uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
        size_t exeSize = ((IMAGE_NT_HEADERS64*)(exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew))->OptionalHeader.SizeOfImage;
        static uintptr_t stack[512]; /* the callers' frames: above this one */
        char trail[1536];
        int n = 0, found = 0;
        if (!SafeRead((uintptr_t)__builtin_frame_address(0), stack, sizeof stack))
            for (int k = 0; k < 512 && found < 40 && n < (int)sizeof trail - 40; k++)
            {
                const char* m;
                uintptr_t o;
                if (InGame(stack[k], game, gameSize, exe, exeSize, &m, &o)) { n += snprintf(trail + n, sizeof trail - n, " %s+0x%llx", m[0] == 'g' ? "g" : "x", (unsigned long long)o); found++; }
            }
        trail[n] = 0;
        /* Who asked: a game address, else the module and offset (ucrtbase's abort, kernelbase, ...). */
        uintptr_t at = (uintptr_t)__builtin_return_address(0), o = at;
        const char* m = NULL;
        char owner[MAX_PATH] = "?";
        HMODULE module = NULL;
        if (!InGame(at, game, gameSize, exe, exeSize, &m, &o) &&
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &module))
        {
            GetModuleFileNameA(module, owner, sizeof owner);
            o = at - (uintptr_t)module;
        }
        const char* name = m != NULL ? m : (strrchr(owner, '\\') ? strrchr(owner, '\\') + 1 : owner);
        Note("crash: fail-fast about to end the game (thread %lu, asked from %s+0x%llx); stack:%s", GetCurrentThreadId(), name, (unsigned long long)o, trail);
    }
    return g_isProcessorFeaturePresent(feature);
}

static void InstallFailFastNote(void)
{
    void* target = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsProcessorFeaturePresent");
    if (target == NULL) return;
    if (LoaderHook("fail-fast notes", (uintptr_t)target, (void*)FeatureDetour, (void**)&g_isProcessorFeaturePresent))
        Note("crash notes: fail-fast exits will not be noted");
}

/* Diagnostic (the war table's map previews): hardware watchpoints on a level's playable radius (+0x2D70, DR0) and row
 * factor (+0x2D80, DR1) in every thread; each new game address that touches them is logged once. */
static volatile uintptr_t g_fieldWatched;
static volatile uintptr_t g_previewRecord; /* the record 0x1003EE6 copies the radius into (+0x240); watched with DR2 */
static uintptr_t g_fieldHits[48];
static volatile LONG g_fieldHitCount;
static LONG CALLBACK FieldWatchHit(EXCEPTION_POINTERS* info)
{
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(info->ContextRecord->Dr6 & 7)) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t at = info->ContextRecord->Rip;
    int which = (info->ContextRecord->Dr6 & 1) ? 0 : (info->ContextRecord->Dr6 & 2) ? 1 : 2;
    if (which == 0 && at == ApiModuleBase("game.dll", NULL) + 0x1003EE6) g_previewRecord = info->ContextRecord->Rsi;
    info->ContextRecord->Dr6 = 0;
    LONG n = g_fieldHitCount;
    for (LONG i = 0; i < n && i < 48; i++) if (g_fieldHits[i] == at) return EXCEPTION_CONTINUE_EXECUTION;
    if (n < 48)
    {
        g_fieldHits[n] = at;
        InterlockedIncrement(&g_fieldHitCount);
        size_t gameSize = 0;
        uintptr_t game = ApiModuleBase("game.dll", &gameSize);
        uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
        size_t exeSize = ((IMAGE_NT_HEADERS64*)(exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew))->OptionalHeader.SizeOfImage;
        const char* m = "?";
        uintptr_t o = at;
        InGame(at, game, gameSize, exe, exeSize, &m, &o);
        float value = 0;
        SafeRead(which == 2 ? info->ContextRecord->Dr2 : g_fieldWatched + (which ? 0x10 : 0), &value, 4);
        if (which == 2)
            Note("difficulty: preview record +0x240 (now %.3f) touched before %s+0x%llx (thread %lu)", (double)value, m, (unsigned long long)o, GetCurrentThreadId());
        else
            Note("difficulty: level +0x%x (now %.3f) touched before %s+0x%llx (thread %lu)", which ? 0x2D80 : 0x2D70, (double)value, m,
                (unsigned long long)o, GetCurrentThreadId());
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void FieldWatch(uintptr_t field)
{
    static int handler;
    static uintptr_t record;
    if (field == g_fieldWatched && g_previewRecord == record) return;
    record = g_previewRecord;
    if (!handler) { AddVectoredExceptionHandler(1, FieldWatchHit); handler = 1; }
    g_fieldWatched = field;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 entry = {sizeof entry};
    DWORD self = GetCurrentThreadId(), process = GetCurrentProcessId();
    int set = 0;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
    {
        if (entry.th32OwnerProcessID != process || entry.th32ThreadID == self) continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, entry.th32ThreadID);
        if (thread == NULL) continue;
        if (SuspendThread(thread) != (DWORD)-1)
        {
            CONTEXT context = {0};
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread, &context))
            {
                context.Dr0 = field;
                context.Dr1 = field + 0x10;
                context.Dr7 = (context.Dr7 & ~0xFF000Full) | 1 | 4 | (0xFull << 16) | (0xFull << 20); /* L0, L1: read/write, 4 bytes */
                if (record != 0) { context.Dr2 = record + 0x240; context.Dr7 = (context.Dr7 & ~0xF000030ull) | 0x10 | (0xFull << 24); } /* L2 */
                if (SetThreadContext(thread, &context)) set++;
            }
            ResumeThread(thread);
        }
        CloseHandle(thread);
    }
    CloseHandle(snapshot);
    Note("difficulty: watching level fields at 0x%llx%s in %d threads", (unsigned long long)field, record ? " and the preview record" : "", set);
}

static void BehaviourStage(BehaviourCtx* ctx, int stage, int id)
{
    const Behaviour* b = BehaviourOf(id);
    if (b == NULL) return;
    if (stage == -1) stage = b->base->death;                       /* its death (0x495BC0): the base's death stage */
    if (b->base->id == BHV_PELICAN_TRANSPORT)
    {
        if (*(int32_t*)ctx->p == 12 && stage != 0) return;         /* 667 leaves stage 12 only for 0 (0x4962C0) */
        ((BhvCtxIntFn)BhvFn(F_667_STAGE))(ctx, stage);
    }
    else ((BhvCtxIntFn)BhvFn(F_645_STAGE))(ctx, stage);
}

static void BehaviourEvent(BehaviourCtx* ctx, uint32_t hash, int id)
{
    const Behaviour* b = BehaviourOf(id);
    if (b != NULL && b->base->id == BHV_PELICAN_TRANSPORT) ((BhvCtxHashFn)BhvFn(F_667_EVENT))(ctx, hash); /* 645 has none */
}

/* ---- the stubs ------------------------------------------------------------------------------------------------- */

typedef struct Emit { uint8_t* at; } Emit;
static void Put(Emit* e, const void* bytes, size_t n) { memcpy(e->at, bytes, n); e->at += n; }
static void Put8(Emit* e, uint8_t v) { *e->at++ = v; }
static void Put32(Emit* e, uint32_t v) { memcpy(e->at, &v, 4); e->at += 4; }
static void PutRel32(Emit* e, const uint8_t* target) { Put32(e, (uint32_t)(int32_t)(target - (e->at + 4))); }

/* The stubs' block once built, and where each stub's COUNT is (a live change raises them: live_content.c). */
static uint8_t* g_bhvBlock;
static uint8_t* g_bhvCountAt[8];
static int g_bhvCountSites;
static void PutCount(Emit* e, uint32_t count)
{
    if (g_bhvCountSites < (int)(sizeof g_bhvCountAt / sizeof g_bhvCountAt[0])) g_bhvCountAt[g_bhvCountSites++] = e->at;
    Put32(e, count);
}

/* `lea eax, [reg + 1 - FIRST]; cmp eax, COUNT; jae <patched later>` (eax: the index of a HellForge id): returns where
 * the jae's rel8 is. */
static uint8_t* PutIdCheck(Emit* e, uint8_t leaModrm)
{
    Put8(e, 0x8D); Put8(e, leaModrm); Put32(e, (uint32_t)(1 - (int32_t)BHV_FIRST));
    Put8(e, 0x3D); PutCount(e, g_behaviourCount);
    Put8(e, 0x73); Put8(e, 0);
    return e->at - 1;
}

/* `call/jmp qword [rip + slot]` to an absolute address kept in the block's slot area. */
static void PutIndirect(Emit* e, uint8_t op, uint8_t** slots, uintptr_t target)
{
    memcpy(*slots, &target, 8);
    Put8(e, 0xFF); Put8(e, op);
    PutRel32(e, *slots);
    *slots += 8;
}

static void Land(uint8_t* rel8, const Emit* e) { *rel8 = (uint8_t)(e->at - (rel8 + 1)); }

/* Unwind data for a stub that calls out from inside its dispatcher's frame: that frame's prologue, all at offset 0. */
typedef struct BhvUnwind { uint8_t version, prolog, count, frame; uint16_t codes[6]; } BhvUnwind;

/* A behaviour's answers in the stubs' per-id tables: its base's stage count, slot mappings and case in resume's switch
 * (or past it: nothing). */
static void BehaviourAnswers(const Behaviour* b, uintptr_t game, uint32_t* stages, uintptr_t* to, uintptr_t* from, uint32_t* resume)
{
    uint32_t base = b->base->id;
    *stages = b->base->stages;
    *to = game + b->base->toSlot;
    *from = game + b->base->fromSlot;
    *resume = base >= BHV_RESUME_FIRST && base <= BHV_RESUME_LAST ? base - BHV_RESUME_FIRST : 0xFFFFFFFFu;
}

static int RefuseBehaviours(const char* why, uint32_t at)
{
    Note("behaviours: %s (0x%x); %u behaviour(s) not added: entities given one run its base", why, at, g_behaviourCount);
    g_behaviourState = -1;
    return 1;
}

static int InstallBehaviours(uintptr_t game)
{
    if (g_behaviourState != 0) return 1;
    if (g_behaviourCount == 0) { g_behaviourState = 1; return 1; }
    g_bhvGame = game;
    for (int f = 0; f < F_COUNT; f++)
    {
        uint8_t bytes[32];
        if (SafeRead(game + kBhvFunctions[f].rva, bytes, 32) || memcmp(bytes, kBhvFunctions[f].pin, 32) != 0)
            return RefuseBehaviours("a game function is not the reviewed one", kBhvFunctions[f].rva);
    }
    uint8_t resumePin[sizeof kBhvResumePin], switchPin[sizeof kBhvResumeSwitchPin];
    if (SafeRead(game + kBhvSites[S_RESUME].ja - 6, resumePin, sizeof resumePin) || memcmp(resumePin, kBhvResumePin, sizeof resumePin) != 0 ||
        SafeRead(game + BHV_RESUME_SWITCH, switchPin, sizeof switchPin) || memcmp(switchPin, kBhvResumeSwitchPin, sizeof switchPin) != 0)
        return RefuseBehaviours("the resume dispatcher is not the reviewed one", kBhvSites[S_RESUME].ja);
    for (int s = 0; s < S_COUNT; s++)
    {
        uint8_t bytes[6];
        int32_t rel = 0;
        if (!SafeRead(game + kBhvSites[s].ja, bytes, 6)) memcpy(&rel, bytes + 2, 4);
        if (bytes[0] != 0x0F || bytes[1] != (kBhvSites[s].cond ? kBhvSites[s].cond : 0x87) || game + kBhvSites[s].ja + 6 + rel != game + kBhvSites[s].target)
            return RefuseBehaviours("a dispatch is not the reviewed one", kBhvSites[s].ja);
    }
    uint8_t* block = AllocateNearGame(game, 0x1000);
    if (block == NULL) return RefuseBehaviours("no memory near game.dll", 0);
    Emit e = {block};
    uint8_t* slots = block + 0x600;
    g_bhvCountSites = 0;
    /* Per-id answers of the helpers: the stage count (u32) and the two slot mappings (absolute targets). */
    uint32_t* stageCounts = (uint32_t*)(block + 0x700);
    uintptr_t* toSlot = (uintptr_t*)(block + 0x740);
    uintptr_t* fromSlot = (uintptr_t*)(block + 0x780);
    uint32_t* resumeCase = (uint32_t*)(block + 0x7C0); /* the base's index in resume's switch (or past it: nothing) */
    for (uint32_t i = 0; i < g_behaviourCount; i++)
        BehaviourAnswers(&g_behaviours[i], game, &stageCounts[i], &toSlot[i], &fromSlot[i], &resumeCase[i]);
    uint8_t* stubs[S_COUNT];
    uint8_t* jae;

    stubs[S_UPDATE] = e.at;                 /* edx = id - 1, rdi = ctx, xmm2 = dt */
    jae = PutIdCheck(&e, 0x82);
    Put(&e, "\x48\x89\xF9", 3);             /* mov rcx, rdi */
    Put8(&e, 0x8D); Put8(&e, 0x90); Put32(&e, BHV_FIRST); /* lea edx, [rax + FIRST] */
    PutIndirect(&e, 0x15, &slots, (uintptr_t)BehaviourUpdate);
    Land(jae, &e);
    PutIndirect(&e, 0x25, &slots, game + kBhvSites[S_UPDATE].target);
    uint8_t* endUpdate = e.at;

    stubs[S_STAGE] = e.at;                  /* edx = id - 1, rbx = ctx, edi = stage */
    jae = PutIdCheck(&e, 0x82);
    Put(&e, "\x48\x89\xD9\x89\xFA", 5);     /* mov rcx, rbx; mov edx, edi */
    Put(&e, "\x44\x8D\x80", 3); Put32(&e, BHV_FIRST); /* lea r8d, [rax + FIRST] */
    PutIndirect(&e, 0x15, &slots, (uintptr_t)BehaviourStage);
    Land(jae, &e);
    PutIndirect(&e, 0x25, &slots, game + kBhvSites[S_STAGE].target);
    uint8_t* endStage = e.at;

    stubs[S_EVENT] = e.at;                  /* edx = id - 1, rdi = ctx, r8d = hash */
    jae = PutIdCheck(&e, 0x82);
    Put(&e, "\x48\x89\xF9\x44\x89\xC2", 6); /* mov rcx, rdi; mov edx, r8d */
    Put(&e, "\x44\x8D\x80", 3); Put32(&e, BHV_FIRST);
    PutIndirect(&e, 0x15, &slots, (uintptr_t)BehaviourEvent);
    Land(jae, &e);
    PutIndirect(&e, 0x25, &slots, game + kBhvSites[S_EVENT].target);
    uint8_t* endEvent = e.at;

    stubs[S_STAGES] = e.at;                 /* leaf; ecx = id - 1: the base's stage count */
    jae = PutIdCheck(&e, 0x81);
    Put(&e, "\x4C\x8D\x05", 3); PutRel32(&e, (uint8_t*)stageCounts); /* lea r8, [rip + counts] */
    Put(&e, "\x41\x8B\x04\x80\xC3", 5);     /* mov eax, [r8 + rax*4]; ret */
    Land(jae, &e);
    PutIndirect(&e, 0x25, &slots, game + kBhvSites[S_STAGES].target);

    stubs[S_TO_SLOT] = e.at;                /* leaf; ecx = id - 1, edx = hash: the base's case */
    jae = PutIdCheck(&e, 0x81);
    Put(&e, "\x4C\x8D\x05", 3); PutRel32(&e, (uint8_t*)toSlot);       /* lea r8, [rip + table] */
    Put(&e, "\x41\xFF\x24\xC0", 4);         /* jmp [r8 + rax*8] */
    Land(jae, &e);
    PutIndirect(&e, 0x25, &slots, game + kBhvSites[S_TO_SLOT].target);

    stubs[S_FROM_SLOT] = e.at;
    jae = PutIdCheck(&e, 0x81);
    Put(&e, "\x4C\x8D\x05", 3); PutRel32(&e, (uint8_t*)fromSlot);
    Put(&e, "\x41\xFF\x24\xC0", 4);
    Land(jae, &e);
    PutIndirect(&e, 0x25, &slots, game + kBhvSites[S_FROM_SLOT].target);

    stubs[S_RESUME] = e.at;                 /* edx = the id, rbx = ctx: a new id into the switch with its base's index */
    Put8(&e, 0x8D); Put8(&e, 0x82); Put32(&e, (uint32_t)(-(int32_t)BHV_FIRST));                         /* lea eax, [rdx - FIRST] */
    Put8(&e, 0x3D); PutCount(&e, g_behaviourCount);                                                       /* cmp eax, COUNT */
    Put8(&e, 0x73); Put8(&e, 0);                                                                          /* jae not ours */
    jae = e.at - 1;
    Put(&e, "\x4C\x8D\x05", 3); PutRel32(&e, (uint8_t*)resumeCase);                                   /* lea r8, [rip + cases] */
    Put(&e, "\x41\x8B\x14\x80", 4);                                                                   /* mov edx, [r8 + rax*4] */
    Put(&e, "\x83\xFA", 2); Put8(&e, (uint8_t)(BHV_RESUME_LAST - BHV_RESUME_FIRST));                    /* cmp edx, 0x56 */
    Put8(&e, 0x77); Put8(&e, 0);                                                                          /* ja nothing */
    uint8_t* outside = e.at - 1;
    PutIndirect(&e, 0x25, &slots, game + BHV_RESUME_CASES);
    Land(outside, &e);
    PutIndirect(&e, 0x25, &slots, game + BHV_RESUME_NONE);                                               /* a base it has no case for */
    Land(jae, &e);
    PutIndirect(&e, 0x25, &slots, game + kBhvSites[S_RESUME].target);                                   /* every other id, as before */

    /* Unwind data for the three stubs that call out: ALLOC_LARGE, then the pushes/saves. */
    BhvUnwind* unwind = (BhvUnwind*)(block + 0x800);
    unwind[0] = (BhvUnwind){1, 0, 4, 0, {0x0100, 0x218 / 8, 0x7000, 0x5000}};               /* sub 0x218; push rdi; push rbp */
    unwind[1] = (BhvUnwind){1, 0, 5, 0, {0x0100, 0x1D0 / 8, 0x7000, 0x3000, 0x5000}};       /* sub 0x1D0; push rdi, rbx, rbp */
    unwind[2] = (BhvUnwind){1, 0, 5, 0, {0x0100, 0xA40 / 8, 0x5000, 0x7400, 0xA60 / 8}};    /* sub 0xA40; push rbp; rdi at +0xA60 */
    RUNTIME_FUNCTION* functions = (RUNTIME_FUNCTION*)(block + 0x900);
    uint8_t* ranges[3][2] = {{stubs[S_UPDATE], endUpdate}, {stubs[S_STAGE], endStage}, {stubs[S_EVENT], endEvent}};
    for (int k = 0; k < 3; k++)
    {
        functions[k].BeginAddress = (DWORD)(ranges[k][0] - block);
        functions[k].EndAddress = (DWORD)(ranges[k][1] - block);
        functions[k].UnwindData = (DWORD)((uint8_t*)&unwind[k] - block);
    }
    DWORD old;
    if (!VirtualProtect(block, 0x1000, PAGE_EXECUTE_READ, &old)) return RefuseBehaviours("the stubs could not be made executable", 0);
    FlushInstructionCache(GetCurrentProcess(), block, 0x1000);
    if (!RtlAddFunctionTable(functions, 3, (DWORD64)(uintptr_t)block)) Note("behaviours: the stubs' unwind data was not registered");

    /* The branches last, each one atomic (its rel32 sits inside one aligned qword). */
    for (int s = 0; s < S_COUNT; s++)
    {
        uintptr_t at = game + kBhvSites[s].ja + 2;
        int32_t rel = (int32_t)(int64_t)((uintptr_t)stubs[s] - (game + kBhvSites[s].ja + 6));
        uintptr_t qword = at & ~(uintptr_t)7;
        if (at + 4 > qword + 8) return RefuseBehaviours("a branch straddles a qword", kBhvSites[s].ja);
        DWORD protect;
        if (!VirtualProtect((void*)qword, 8, PAGE_EXECUTE_READWRITE, &protect)) return RefuseBehaviours("a branch could not be written", kBhvSites[s].ja);
        LONG64 before = *(volatile LONG64*)qword, after = before;
        memcpy((uint8_t*)&after + (at - qword), &rel, 4);
        InterlockedCompareExchange64((volatile LONG64*)qword, after, before);
        VirtualProtect((void*)qword, 8, protect, &protect);
        FlushInstructionCache(GetCurrentProcess(), (void*)qword, 8);
    }
    AddVectoredExceptionHandler(1, BehaviourFault);
    g_bhvBlock = block;
    g_behaviourState = 1;
    Note("behaviours: %u behaviour(s) added (ids %u..%u), stubs at 0x%llx", g_behaviourCount, BHV_FIRST, BHV_FIRST + g_behaviourCount - 1,
        (unsigned long long)(uintptr_t)block);
    return 1;
}

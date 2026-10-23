/* Per-player projectile swaps in co-op (docs/multiplayer.md "Shots in co-op"). Every machine simulates every player's
 * shots from its own data, so a swap one player made in the editor (a weapon's ProjectileWeapon +0) showed as the stock
 * round on the others' screens, and the swapping player's own swap showed on the others' guns. Each shot's type is
 * chosen in the ProjectileWeapon fire function (game.dll 0x6128B0) for the local player's shots and for this machine's
 * copies of the others' alike: after the native choice ([rbp-0x68], at 0x614081, past both of its branches), before
 * the pellet loop and the pool's constructor, with the shot's source at [rbp-0x80] (+0 the weapon's resource, +8 its
 * entity). Research: HD2Mods\research\projectile-per-player (Astra, from a decoded snapshot of this build; both pins
 * are checked here before anything is written).
 *
 * A stub at 0x614081 calls ShotType with the chosen type and the source, writes back what it returns, and replays the
 * two instructions it displaced. ShotType changes only a weapon's normal round (the type its record holds, or held
 * before this machine's own swap): a mode, charge or ammunition type it leaves as it is. It finds the shooter (the
 * weapon's wielder, Wieldable manager [game+0x3326730]) among the players (event_polls.c EvpPlayers: peer and
 * Helldiver), then:
 *   - a player with a swap for that weapon (the lobby's rules, from the session's backend: live.c "shots"): theirs;
 *   - this machine's player: what the game chose (its own record, swapped or not);
 *   - another player without one: the weapon's type from before this machine's own swap.
 * Shots by no player (turrets, enemies), and any shot while no rules are set (solo, or a host sharing its stats), are
 * left alone. A rule is used once its projectile's row exists and its packages are resident (refs.c RefCheck). Ordinary
 * projectile weapons only: beams, weapons that fire entities and programmable ammunition choose later. */
#define SHOT_FIRE 0x6128B0u
#define SHOT_SITE 0x614081u
#define SHOT_SITE_BACK 0x61408Bu      /* past the two displaced instructions */
#define SHOT_WIELDERS 0x3326730u
#define SHOT_RULES 256
#define SHOT_LOCAL 128
static const uint8_t kShotFirePin[48] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x8D, 0xA8, 0x18, 0xFB, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xB0, 0x05, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xB8, 0x0F, 0x29,
    0x78, 0xA8, 0x44, 0x0F, 0x29, 0x40, 0x98, 0x44, 0x0F, 0x29};
static const uint8_t kShotSitePin[48] = {0x80, 0x7C, 0x24, 0x60, 0x00, 0x48, 0x8B, 0x4C, 0x24, 0x78, 0x4C, 0x8B, 0x75, 0xB0, 0xC6, 0x41,
    0x11, 0x00, 0x0F, 0x84, 0x14, 0x03, 0x00, 0x00, 0x49, 0x8B, 0x4E, 0x68, 0x41, 0x8B, 0xC4, 0x48, 0x8B, 0x0C, 0xC1, 0xE8, 0x57, 0x10,
    0xF0, 0xFF, 0x8B, 0x8D, 0x18, 0x03, 0x00, 0x00, 0x4C, 0x8D};

typedef struct ShotRule { uint64_t peer, resource; uint32_t type, native; int ready; } ShotRule; /* native: the weapon's record here */
typedef struct ShotLocal { uint64_t resource; uint32_t base, now; } ShotLocal;
typedef struct ShotPlayer { uint64_t peer; uint32_t avatar; int local; } ShotPlayer;
static ShotRule g_shotRules[SHOT_RULES];
static int g_shotRuleCount;
static ShotLocal g_shotLocal[SHOT_LOCAL];
static int g_shotLocalCount;
static ShotPlayer g_shotPlayers[4];
static int g_shotPlayerCount;
static volatile LONG g_shotsActive; /* rules are set: co-op with every player keeping their own */
static SRWLOCK g_shotsLock = SRWLOCK_INIT;
static int g_shotState;              /* 0 not installed yet, 1 installed, -1 refused */
static ULONGLONG g_shotFirstTry;
static volatile LONG g_shotChanged;  /* shots whose type a rule changed, for the log */

static volatile uintptr_t g_shotGame; /* game.dll, as the tick last found it */

static uint32_t ShotWielderOf(uint32_t weapon)
{
    uintptr_t game = g_shotGame, wielders, of;
    uint32_t index;
    if (!game || !EvpPtr(game + SHOT_WIELDERS, &wielders) || !EvpHash(wielders + 0x18, weapon, 8, 4, &index) || !EvpPtr(wielders + 0x38, &of)) return 0;
    return EvpU32(of + (uintptr_t)index * 4, 0);
}
/* The weapon's current wielder; the native fixture replaces it. */
static uint32_t (*g_shotWielder)(uint32_t weapon) = ShotWielderOf;

/* This machine's own swap of a weapon's projectile (a Local Stats edit at boot, or the editor's live one): its type
 * before the first and now. Called after every write of ProjectileWeaponComponentData +0 (stats.c, fields.c). */
static void ShotsNoteLocal(uint64_t resource, uint32_t before, uint32_t after)
{
    AcquireSRWLockExclusive(&g_shotsLock);
    int i = 0;
    while (i < g_shotLocalCount && g_shotLocal[i].resource != resource) i++;
    if (i == g_shotLocalCount && g_shotLocalCount < SHOT_LOCAL) g_shotLocal[g_shotLocalCount++] = (ShotLocal){resource, before, after};
    else if (i < g_shotLocalCount) g_shotLocal[i].now = after;
    ReleaseSRWLockExclusive(&g_shotsLock);
}

/* The type a shot fires (see the top): from the stub, on the thread that fires. */
static uint32_t ShotType(uint32_t selected, const uint8_t* source)
{
    if (!g_shotsActive || selected == 0 || source == NULL) return selected;
    uint64_t resource = 0;
    uint32_t weapon = 0;
    if (SafeRead((uintptr_t)source, &resource, 8) || SafeRead((uintptr_t)source + 8, &weapon, 4) || resource == 0) return selected;
    uint32_t wielder = weapon ? g_shotWielder(weapon) : 0;
    uint32_t chosen = selected;
    AcquireSRWLockShared(&g_shotsLock);
    const ShotLocal* local = NULL;
    for (int i = 0; i < g_shotLocalCount; i++) if (g_shotLocal[i].resource == resource) local = &g_shotLocal[i];
    const ShotPlayer* shooter = NULL;
    for (int i = 0; wielder != 0 && i < g_shotPlayerCount; i++) if (g_shotPlayers[i].avatar == wielder) shooter = &g_shotPlayers[i];
    const ShotRule* rule = NULL;
    for (int i = 0; shooter != NULL && i < g_shotRuleCount; i++)
        if (g_shotRules[i].peer == shooter->peer && g_shotRules[i].resource == resource && g_shotRules[i].ready) rule = &g_shotRules[i];
    /* only the weapon's normal round: its record's type here, or its type before and after this machine's own swap; a
     * mode's, a charge's or an ammunition's type stays */
    int normal = (rule != NULL && selected == rule->native) || (local != NULL && (selected == local->base || selected == local->now));
    if (shooter != NULL && normal)
    {
        if (rule != NULL) chosen = rule->type;
        else if (!shooter->local && local != NULL) chosen = local->base; /* another player's gun: not this machine's swap */
    }
    ReleaseSRWLockShared(&g_shotsLock);
    if (chosen != selected) InterlockedIncrement(&g_shotChanged);
    return chosen;
}

/* live.c "shots": the lobby's rules, "<peer hex>:<resource hex>=<type>" comma separated, "-" none (solo, or a host
 * sharing its stats). Replaces the last. 0 with *why when malformed. */
static int ShotsSetRules(const char* list, const char** why)
{
    ShotRule rules[SHOT_RULES];
    int count = 0;
    for (const char* at = list; strcmp(list, "-") && *at;)
    {
        char* end;
        if (count == SHOT_RULES) { *why = "more rules than HellForge keeps"; return 0; }
        uint64_t peer = strtoull(at, &end, 16);
        if (end == at || *end != ':') { *why = "malformed rule"; return 0; }
        const char* r = end + 1;
        uint64_t resource = strtoull(r, &end, 16);
        if (end == r || *end != '=') { *why = "malformed rule"; return 0; }
        const char* t = end + 1;
        unsigned long type = strtoul(t, &end, 10);
        if (end == t || (*end != ',' && *end != 0) || type == 0 || type > 0xFFFF) { *why = "malformed rule"; return 0; }
        rules[count++] = (ShotRule){peer, resource, (uint32_t)type, 0, 0};
        at = *end == ',' ? end + 1 : end;
    }
    AcquireSRWLockExclusive(&g_shotsLock);
    for (int i = 0; i < count; i++) /* a rule that was ready stays so */
        for (int j = 0; j < g_shotRuleCount; j++)
            if (g_shotRules[j].peer == rules[i].peer && g_shotRules[j].resource == rules[i].resource && g_shotRules[j].type == rules[i].type)
                { rules[i].ready = g_shotRules[j].ready; rules[i].native = g_shotRules[j].native; }
    memcpy(g_shotRules, rules, sizeof rules[0] * (size_t)count);
    g_shotRuleCount = count;
    InterlockedExchange(&g_shotsActive, count > 0);
    ReleaseSRWLockExclusive(&g_shotsLock);
    Note("shots: %d player projectile swap(s) from the lobby%s", count, count > 0 && g_shotState != 1 ? " (not applied: the fire site is not hooked)" : "");
    return 1;
}

/* The watch loop, after the game loads: the players (peer and Helldiver), each rule's readiness (its row and
 * packages), and the hook once its bytes read as reviewed (60 s for code still being decoded). */
static void ShotsTick(void)
{
    static ULONGLONG next;
    ULONGLONG now = GetTickCount64();
    if (now < next) return;
    next = now + 250;
    uintptr_t game = ApiModuleBase("game.dll", NULL);
    if (game == 0) return;
    g_shotGame = game;
    EvpPlayer players[EVT_MAX_PLAYERS];
    int count = EvpPlayers(game, players);
    int ready[SHOT_RULES], rules;
    AcquireSRWLockShared(&g_shotsLock);
    rules = g_shotRuleCount;
    ShotRule copy[SHOT_RULES];
    memcpy(copy, g_shotRules, sizeof copy[0] * (size_t)rules);
    ReleaseSRWLockShared(&g_shotsLock);
    uint32_t native[SHOT_RULES];
    for (int i = 0; i < rules; i++)
    {
        char why[200];
        ready[i] = copy[i].ready || RefCheck(REF_PROJECTILE, copy[i].type, 0xFFFFFFFFu, 0, why, sizeof why) == REF_OK;
        /* the weapon's normal round here: its ProjectileWeapon record's type (a shared record: read only) */
        uint32_t stride = 0;
        uintptr_t record = g_fieldRecord("ProjectileWeaponComponentData", copy[i].resource, &stride, 1);
        native[i] = 0;
        if (record == 0 || SafeRead(record, &native[i], 4)) ready[i] = 0;
    }
    AcquireSRWLockExclusive(&g_shotsLock);
    g_shotPlayerCount = 0;
    for (int i = 0; i < count && i < 4; i++)
        if (players[i].peer != 0) g_shotPlayers[g_shotPlayerCount++] = (ShotPlayer){players[i].peer, players[i].avatar, players[i].local};
    for (int i = 0; i < rules && i < g_shotRuleCount; i++)
        if (g_shotRules[i].peer == copy[i].peer && g_shotRules[i].resource == copy[i].resource)
        {
            g_shotRules[i].native = native[i];
            if (!ready[i] || g_shotRules[i].ready) continue;
            g_shotRules[i].ready = 1;
            Note("shots: player %016llx's %016llx fires projectile %u", (unsigned long long)copy[i].peer, (unsigned long long)copy[i].resource, copy[i].type);
        }
    ReleaseSRWLockExclusive(&g_shotsLock);
    if (g_shotState != 0) return;
    if (!g_shotFirstTry) g_shotFirstTry = now;
    if (strcmp(g_build, "25480438")) { g_shotState = -1; Note("shots: not hooked (unsupported build)"); return; }
    if (!PinMatches(game + SHOT_FIRE, kShotFirePin, sizeof kShotFirePin) || !PinMatches(game + SHOT_SITE, kShotSitePin, sizeof kShotSitePin))
    {
        if (now - g_shotFirstTry < 60000) return;
        g_shotState = -1;
        Note("shots: the projectile fire site is not the reviewed one; per-player projectile swaps are off");
        return;
    }
    /* the stub: save what the game holds in volatile registers, ShotType(type, source), the type written back, the two
     * displaced instructions, and back */
    uint8_t* stub = AllocateNearGame(game, 0x1000);
    if (stub == NULL) { g_shotState = -1; Note("shots: no room near game.dll for the stub"); return; }
    static const uint8_t head[] = {
        0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x9C, /* push rax rcx rdx r8 r9 r10 r11, pushfq */
        0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00,                               /* sub rsp, 0x80 (shadow space, xmm0-5) */
        0xF3, 0x0F, 0x7F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x7F, 0x4C, 0x24, 0x30, 0xF3, 0x0F, 0x7F, 0x54, 0x24, 0x40,
        0xF3, 0x0F, 0x7F, 0x5C, 0x24, 0x50, 0xF3, 0x0F, 0x7F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x7F, 0x6C, 0x24, 0x70,
        0x8B, 0x4D, 0x98,                                                       /* mov ecx, [rbp-0x68] (the chosen type) */
        0x48, 0x8B, 0x55, 0x80,                                                 /* mov rdx, [rbp-0x80] (the shot's source) */
        0x48, 0xB8};                                                            /* mov rax, imm64 (ShotType) */
    static const uint8_t tail[] = {
        0xFF, 0xD0,                                                             /* call rax */
        0x89, 0x45, 0x98,                                                       /* mov [rbp-0x68], eax */
        0xF3, 0x0F, 0x6F, 0x44, 0x24, 0x20, 0xF3, 0x0F, 0x6F, 0x4C, 0x24, 0x30, 0xF3, 0x0F, 0x6F, 0x54, 0x24, 0x40,
        0xF3, 0x0F, 0x6F, 0x5C, 0x24, 0x50, 0xF3, 0x0F, 0x6F, 0x64, 0x24, 0x60, 0xF3, 0x0F, 0x6F, 0x6C, 0x24, 0x70,
        0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00,                               /* add rsp, 0x80 */
        0x9D, 0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0x58, /* popfq, pop r11 r10 r9 r8 rdx rcx rax */
        0x80, 0x7C, 0x24, 0x60, 0x00,                                           /* cmp byte [rsp+0x60], 0 (displaced) */
        0x48, 0x8B, 0x4C, 0x24, 0x78,                                           /* mov rcx, [rsp+0x78] (displaced) */
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};                                    /* jmp [rip] -> back */
    uintptr_t fn = (uintptr_t)ShotType, back = game + SHOT_SITE_BACK;
    size_t n = 0;
    memcpy(stub, head, sizeof head); n += sizeof head;
    memcpy(stub + n, &fn, 8); n += 8;
    memcpy(stub + n, tail, sizeof tail); n += sizeof tail;
    memcpy(stub + n, &back, 8);
    DWORD protect;
    if (!VirtualProtect(stub, 0x1000, PAGE_EXECUTE_READ, &protect)) { g_shotState = -1; Note("shots: the stub could not be made executable"); return; }
    uint8_t jump[10] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90};
    int32_t rel = (int32_t)((intptr_t)stub - (intptr_t)(game + SHOT_SITE + 5));
    memcpy(jump + 1, &rel, 4);
    if (ApiWrite(game + SHOT_SITE, jump, sizeof jump)) { g_shotState = -1; Note("shots: the fire site could not be changed"); return; }
    g_shotState = 1;
    Note("shots: hooked the projectile fire site: in co-op each player's projectile swaps fire as theirs on every machine");
}

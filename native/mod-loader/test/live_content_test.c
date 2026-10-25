/* Live content (live_content.c, live_registry.c, ownership.c): numbering as a boot of the change's mods numbers (by mod id,
 * then call order), stand-ins for a new version's entries, tombstones, refusals; the first move of the stratagem,
 * damage and projectile lists and a later add, on a synthetic game image, with ownership in a fake catalogue; a live
 * behaviour in the stubs; live entities in a fake entity state. Included by stats_test.c after live_mods_test.c. */

/* The load events as a fixture found them, put back at its end. */
static LONG g_contentFired[HF_EVENT_MODS_CHANGED + 1];
static void ContentEventsSave(void) { for (int e = 0; e <= HF_EVENT_MODS_CHANGED; e++) g_contentFired[e] = g_fired[e]; }
static void ContentEventsRestore(void) { for (int e = 0; e <= HF_EVENT_MODS_CHANGED; e++) InterlockedExchange(&g_fired[e], g_contentFired[e]); }

/* A mod entry as a live change makes one (TxnModEntry's fresh one). */
static HfMod* ContentMod(const char* id)
{
    HfMod* m = &g_mods[g_modCount++];
    memset(m, 0, sizeof *m);
    strcpy(m->id, id);
    m->loaded = 1;
    m->log = INVALID_HANDLE_VALUE;
    m->order = g_modCount - 1;
    wcscpy(m->dll, L"x.dll");
    return m;
}

/* The change: one mod added (old NULL), replaced (both) or removed (now NULL); content's numbers begin. */
static void ContentChange(HfMod* old, HfMod* now)
{
    TxnReset();
    g_txn.open = 1;
    g_txn.modCount = 1;
    TxnMod* t = &g_txn.mods[0];
    memset(t, 0, sizeof *t);
    strcpy(t->id, (now != NULL ? now : old)->id);
    t->action = old == NULL ? TXN_ADD : now == NULL ? TXN_REMOVE : TXN_REPLACE;
    t->old = old;
    t->mod = now;
    char why[256];
    assert(LiveContentBegin(why, sizeof why) == NULL);
}

static void ContentSecond(HfMod* old, HfMod* now)
{
    TxnMod* t = &g_txn.mods[g_txn.modCount++];
    memset(t, 0, sizeof *t);
    strcpy(t->id, (now != NULL ? now : old)->id);
    t->action = old == NULL ? TXN_ADD : now == NULL ? TXN_REMOVE : TXN_REPLACE;
    t->old = old;
    t->mod = now;
}

static uint32_t ContentStratagem(HfMod* mod, const char* name, float cooldown)
{
    HfStratagem s = {sizeof s, .base = 1, .name = name, .cooldown = cooldown};
    g_liveSetup = mod;
    uint32_t type = ApiAddStratagem(mod, &s);
    g_liveSetup = NULL;
    return type;
}

/* The change checked and made (1), or refused and unstaged (0); a mod it takes out is gone afterwards. */
static int ContentCommit(void)
{
    char why[512];
    int failed = 0;
    const char* refused = LiveNumbersCheck(why, sizeof why);
    if (refused == NULL) refused = LiveContentMake(why, sizeof why, &failed);
    if (refused != NULL) { LiveContentUnstage(); TxnReset(); return 0; }
    assert(!failed);
    for (int i = 0; i < g_txn.modCount; i++) if (g_txn.mods[i].old != NULL) { g_txn.mods[i].old->loaded = 0; g_txn.mods[i].old->removed = 1; }
    TxnReset();
    return 1;
}

static void ContentNumbersFixture(void)
{
    ContentEventsSave();
    int mods = g_modCount;
    uint32_t stratagems = g_stratagemCount, behaviours = g_behaviourCount;
    int stratagemState = g_stratagemState, behaviourState = g_behaviourState;
    g_stratagemCount = g_behaviourCount = 0;
    memset(g_lcTomb, 0, sizeof g_lcTomb);
    /* the boot: b's stratagem 150 */
    g_stratagemState = 0;
    InterlockedExchange(&g_fired[HF_EVENT_GAME_LOADED], 0);
    HfMod* b = ContentMod("b");
    HfStratagem one = {sizeof one, .base = 1, .name = "one"};
    assert(ApiAddStratagem(b, &one) == STR_VANILLA);
    /* the game as it runs: stratagems refused at boot, so only the numbers are under test here */
    g_stratagemState = -1;
    InterlockedExchange(&g_fired[HF_EVENT_GAME_LOADED], 1);

    /* c after b: the next number; a before b: b's number, refused */
    HfMod* c = ContentMod("c");
    ContentChange(NULL, c);
    assert(ContentStratagem(c, "x", 0) == STR_VANILLA + 1);
    LiveContentUnstage();
    assert(g_stratagemCount == 1);
    HfMod* a = ContentMod("a");
    ContentChange(NULL, a);
    assert(ContentStratagem(a, "x", 0) == 0 && !ContentCommit() && g_stratagemCount == 1);

    /* b's new version: the same stratagem stands in for its old one; another name would take its number, refused */
    HfMod* b2 = ContentMod("b");
    ContentChange(b, b2);
    assert(ContentStratagem(b2, "one", 3) == STR_VANILLA && g_lcShadowed[LC_STRATAGEM][0] && g_stratagemCount == 1);
    assert(ContentCommit() && g_stratagems[0].mod == b2 && g_stratagems[0].cooldown == 3);
    HfMod* b3 = ContentMod("b");
    ContentChange(b2, b3);
    assert(ContentStratagem(b3, "two", 0) == 0 && !ContentCommit() && g_stratagems[0].mod == b2);

    /* b out and c in: c's stratagem would take b's number under another mod: a restart */
    ContentChange(b2, NULL);
    ContentSecond(NULL, c);
    assert(ContentStratagem(c, "x", 0) == 0 && !ContentCommit());

    /* d and e in, whatever their load order: by id; d out while e stays would move e's number, refused; e out, back */
    HfMod* d = ContentMod("d");
    HfMod* e = ContentMod("e");
    d->order = 9;
    e->order = 1;
    ContentChange(NULL, d);
    ContentSecond(NULL, e);
    assert(ContentStratagem(d, "y", 0) == STR_VANILLA + 1 && ContentStratagem(e, "z", 0) == STR_VANILLA + 2 && ContentCommit());
    d->removed = e->removed = 0;
    ContentChange(d, NULL);
    assert(!ContentCommit() && !LiveTomb(LC_STRATAGEM, 1));
    d->removed = 0;
    ContentChange(e, NULL);
    assert(ContentCommit() && LiveTomb(LC_STRATAGEM, 2) && ApiStratagemRow(STR_VANILLA + 2) == 0);
    HfMod* e2 = ContentMod("e");
    ContentChange(NULL, e2);
    assert(ContentStratagem(e2, "z", 0) == STR_VANILLA + 2 && ContentCommit() && !LiveTomb(LC_STRATAGEM, 2) && g_stratagems[2].mod == e2);

    /* behaviours (unnamed): numbered by mod id too */
    g_behaviourState = -1;
    HfMod* f = ContentMod("f");
    HfBehaviour turret = {sizeof turret, .base = HF_BEHAVIOUR_PELICAN_TURRET};
    ContentChange(NULL, f);
    g_liveSetup = f;
    assert(ApiAddBehaviour(f, &turret) == BHV_FIRST && ApiAddBehaviour(f, &turret) == BHV_FIRST + 1);
    g_liveSetup = NULL;
    assert(ContentCommit() && g_behaviourCount == 2);
    printf("  live content: numbered by mod id then call order (load order aside), a new version stands in, tombstones, 4 refusals\n");

    memset(g_lcTomb, 0, sizeof g_lcTomb);
    g_stratagemCount = stratagems;
    g_behaviourCount = behaviours;
    g_stratagemState = stratagemState;
    g_behaviourState = behaviourState;
    g_modCount = mods;
    ContentEventsRestore();
}

/* ---- a synthetic game.dll: the three lists as reviewed, one base row each ------------------------------------------ */

static void SiteAs(uint8_t* game, uint32_t rva, uint8_t at, int32_t disp) { memcpy(game + rva + at, &disp, 4); }

static uint8_t* ContentGame(size_t size)
{
    uint8_t* game = (uint8_t*)VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(game != NULL);
    uintptr_t g = (uintptr_t)game;
    memcpy(game + STR_CLEAR, kStratagemClearPin, sizeof kStratagemClearPin);
    for (size_t i = 0; i < sizeof kStratagemSites / sizeof kStratagemSites[0]; i++)
        SiteAs(game, kStratagemSites[i].rva, kStratagemSites[i].dispAt, TableDisplacement(g + STR_TABLE, g, &kStratagemSites[i]));
    for (size_t i = 0; i < sizeof kStratagemNameSites / sizeof kStratagemNameSites[0]; i++)
        SiteAs(game, kStratagemNameSites[i].rva, kStratagemNameSites[i].dispAt, (int32_t)STR_NAMES);
    for (size_t i = 0; i < sizeof kStratagemBounds / sizeof kStratagemBounds[0]; i++)
        memcpy(game + kStratagemBounds[i].rva + kStratagemBounds[i].immAt, &kStratagemBounds[i].value, kStratagemBounds[i].width);
    memcpy(game + STR_PICKER_BOUND, kPickerBoundPin, sizeof kPickerBoundPin);
    memcpy(game + STR_PICKER_PAD, kPickerPadPin, sizeof kPickerPadPin);
    *(uint32_t*)(game + STR_ROWS_LOADED) = STR_VANILLA - 1;
    memcpy(game + DMG_CLEAR, kDamageClearPin, sizeof kDamageClearPin);
    for (size_t i = 0; i < sizeof kDamageSites / sizeof kDamageSites[0]; i++)
        SiteAs(game, kDamageSites[i].rva, kDamageSites[i].dispAt, SiteDisplacement(g + DMG_TABLE, g, &kDamageSites[i]));
    memcpy(game + PRJ_CLEAR_A, kClearPin, sizeof kClearPin);
    memcpy(game + PRJ_CLEAR_B, kClearPin, sizeof kClearPin);
    memcpy(game + PRJ_BOUND_A, kBoundAPin, sizeof kBoundAPin);
    memcpy(game + PRJ_BOUND_B, kBoundBPin, sizeof kBoundBPin);
    for (size_t i = 0; i < sizeof kProjectileSites / sizeof kProjectileSites[0]; i++)
        SiteAs(game, kProjectileSites[i].rva, kProjectileSites[i].dispAt, SiteDisplacement(g + PRJ_TABLE, g, &kProjectileSites[i]));
    static uint8_t stratagem[STR_ROW], projectile[PRJ_ROW], damage[DMG_ROW];
    static const char* count = "Count";
    stratagem[0] = 1; *(uint32_t*)(stratagem + 4) = 0x1111; stratagem[0x80] = 2; stratagem[0xC0] = 1;
    projectile[0] = 5;
    damage[0] = 7; *(uint32_t*)(damage + 4) = 100; *(uint32_t*)(damage + 8) = 200;
    uintptr_t row = (uintptr_t)stratagem;
    memcpy(game + STR_TABLE + 8, &row, 8);
    memcpy(game + STR_NAMES + STR_VANILLA * 8, &count, 8);
    row = (uintptr_t)projectile;
    memcpy(game + PRJ_TABLE + 5 * 8, &row, 8);
    row = (uintptr_t)damage;
    memcpy(game + DMG_TABLE + 7 * 8, &row, 8);
    return game;
}

/* ---- a fake catalogue: the game's append (children linked to the root just before), sort, apply and remove ---------- */

static uint8_t* g_fakeCatalogue;
static int g_fakeSorts, g_fakeTakes;
static uint32_t g_fakeLastRoot, g_fakeOwned[8];
static int g_fakeOwnedCount;
static uintptr_t FakeManager(void) { return (uintptr_t)g_fakeCatalogue; }
static uint64_t FakeOwnAdd(uint64_t unused, uint32_t mixId, uint32_t itemId, uint32_t parent, int32_t field, char flag, const void* buy, uint32_t buyCount,
    const void* sell, uint32_t sellCount, const uint32_t* dwords, uint32_t dwordCount, const uint32_t* required, uint32_t requiredCount, uint32_t category,
    uint32_t maxCap)
{
    uint32_t* count = (uint32_t*)(g_fakeCatalogue + OWN_COUNT);
    uint8_t* record = g_fakeCatalogue + OWN_RECORDS + (size_t)*count * OWN_RECORD;
    uint32_t* entry = (uint32_t*)(g_fakeCatalogue + OWN_INDEX + (size_t)*count * 24);
    if (parent == 0) g_fakeLastRoot = *count;
    *(uint32_t*)record = *count; *(uint32_t*)(record + 4) = mixId; *(uint32_t*)(record + 8) = itemId; *(uint32_t*)(record + 0xC) = category;
    entry[0] = *count; entry[1] = mixId; entry[2] = itemId; entry[4] = parent != 0 ? *count - g_fakeLastRoot : 0;
    (*count)++;
    return 0;
}
static void FakeOwnSort(void) { g_fakeSorts++; }
static void FakeOwnApply(uintptr_t manager, uint32_t mixId, uint32_t amount, uint32_t source, const char* text, int extra)
{
    if (g_fakeOwnedCount < 8) g_fakeOwned[g_fakeOwnedCount++] = mixId;
}
static void FakeOwnRemove(uintptr_t manager, uint32_t mixId, uint32_t amount) { g_fakeTakes++; }
static int g_fakePushes;
static void FakeScrubPush(void) { g_fakePushes++; }

/* The catalogue a login gave: a stratagem item (category 11) and its child (category 10, itemId the base's stable id);
 * the login done (the progression manager's state). */
static void FakeCatalogue(void)
{
    g_fakeCatalogue = (uint8_t*)calloc(1, 0xDDCF8 + 4);
    *(uint32_t*)(g_fakeCatalogue + 0xDDCF8) = 12;
    *(uint32_t*)(g_fakeCatalogue + OWN_CATALOGUE) = 2;
    *(uint32_t*)(g_fakeCatalogue + OWN_INVENTORY) = 2;
    FakeOwnAdd(0, 0x5000, 0x5000, 0, 0, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0, 11, 1);
    FakeOwnAdd(0, WeaponChild(0x1111, 0x5000), 0x1111, 0x5000, 0, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0, 10, 1);
    g_ownAdd = FakeOwnAdd; g_ownSort = FakeOwnSort; g_ownRemove = FakeOwnRemove; g_ownManager = FakeManager;
    g_ownApply = FakeOwnApply;
}

static int StratagemSitesAt(uintptr_t game, uintptr_t table)
{
    for (size_t i = 0; i < sizeof kStratagemSites / sizeof kStratagemSites[0]; i++)
        if (*(int32_t*)(game + kStratagemSites[i].rva + kStratagemSites[i].dispAt) != TableDisplacement(table, game, &kStratagemSites[i])) return 0;
    for (size_t i = 0; i < sizeof kStratagemNameSites / sizeof kStratagemNameSites[0]; i++)
        if (*(int32_t*)(game + kStratagemNameSites[i].rva + kStratagemNameSites[i].dispAt) != (int32_t)(int64_t)(table + STR_NAMES_AT - game)) return 0;
    return 1;
}

static int BoundsAt(uintptr_t game, uint32_t added)
{
    for (size_t i = 0; i < sizeof kStratagemBounds / sizeof kStratagemBounds[0]; i++)
    {
        uint32_t value = 0;
        memcpy(&value, (void*)(game + kStratagemBounds[i].rva + kStratagemBounds[i].immAt), kStratagemBounds[i].width);
        if (value != kStratagemBounds[i].value + added) return 0;
    }
    return *(uint32_t*)(game + STR_ROWS_LOADED) == STR_VANILLA + added && *(uint32_t*)(game + STR_PICKER_BOUND + 1) == STR_VANILLA + added;
}

static int ListsAt(uintptr_t game, const DamageSite* sites, size_t count, uintptr_t table)
{
    for (size_t i = 0; i < count; i++) if (*(int32_t*)(game + sites[i].rva + sites[i].dispAt) != SiteDisplacement(table, game, &sites[i])) return 0;
    return 1;
}

static void ContentInstallFixture(void)
{
    ContentEventsSave();
    int mods = g_modCount;
    size_t size = 0x3800000;
    uint8_t* game = ContentGame(size);
    uintptr_t g = (uintptr_t)game;
    g_lcFixtureGame = g;
    g_lcFixtureGameSize = size;
    uint32_t stratagems = g_stratagemCount, projectiles = g_projectileCount, damages = g_damageCount;
    g_stratagemCount = g_projectileCount = g_damageCount = 0;
    g_stratagemState = g_projectileState = g_damageState = 1; /* the boot had none: nothing moved */
    g_stratagemTable = NULL; g_projectileTable = NULL; g_damageTable = NULL; g_projectileBlock = NULL;
    memset(g_lcTomb, 0, sizeof g_lcTomb);
    FakeCatalogue();
    for (int event = HF_EVENT_GAME_LOADED; event <= HF_EVENT_PROJECTILES_READY; event++) InterlockedExchange(&g_fired[event], 1);

    /* the first move of each list, with a new stratagem, damage and projectile */
    HfMod* m = ContentMod("m");
    ContentChange(NULL, m);
    HfDamage dd = {sizeof dd, .name = "d", .base = 7, .scale = 2};
    g_liveSetup = m;
    uint32_t damage = ApiAddDamage(m, &dd);
    HfProjectile pp = {sizeof pp, .name = "p", .base = 5, .damage = damage};
    uint32_t projectile = ApiAddProjectile(m, &pp);
    g_liveSetup = NULL;
    uint32_t type = ContentStratagem(m, "s", 5);
    assert(damage == DMG_VANILLA && projectile == PRJ_VANILLA && type == STR_VANILLA && ContentCommit());
    uintptr_t table = (uintptr_t)g_stratagemTable;
    Stratagem* s = &g_stratagems[0];
    assert(table && StratagemSitesAt(g, table) && BoundsAt(g, 1) && !memcmp(game + STR_PICKER_PAD, kPickerPad, sizeof kPickerPad));
    assert(*(uint8_t**)(table + type * 8) == s->row && *(uint32_t*)s->row == type && *(uint32_t*)(s->row + 4) == s->stable &&
        *(float*)(s->row + 0x68) == 5.0f && *(const char**)(table + STR_NAMES_AT + type * 8) == s->name &&
        !strcmp(*(const char**)(table + STR_NAMES_AT + (type + 1) * 8), "Count"));
    assert(g_damageTable && ListsAt(g, kDamageSites, sizeof kDamageSites / sizeof kDamageSites[0], (uintptr_t)g_damageTable));
    uint8_t* damageRow = *(uint8_t**)(g_damageTable + damage * 8);
    assert(*(uint32_t*)damageRow == damage && *(uint32_t*)(damageRow + 4) == 200 && *(uint32_t*)(damageRow + 8) == 400);
    assert(g_projectileTable && ListsAt(g, kProjectileSites, sizeof kProjectileSites / sizeof kProjectileSites[0], (uintptr_t)g_projectileTable));
    uint8_t* projectileRow = *(uint8_t**)(g_projectileTable + projectile * 8);
    assert(*(uint32_t*)projectileRow == projectile && *(uint32_t*)(projectileRow + PRJ_DAMAGE_AT) == damage &&
        *(uint32_t*)(game + PRJ_BOUND_A + 2) == PRJ_VANILLA + 1 && *(uint32_t*)(game + PRJ_BOUND_B + 3) == PRJ_VANILLA + 1);
    /* owned: the base's item and child copied under the new ids (the child linked to its item), sorted, the item owned */
    assert(*(uint32_t*)(g_fakeCatalogue + OWN_COUNT) == 4 && g_fakeSorts == 1 && g_fakeOwnedCount == 1 && g_fakeOwned[0] == s->item);
    const uint32_t* child = (const uint32_t*)(g_fakeCatalogue + OWN_INDEX + 3 * 24);
    assert(child[1] == WeaponChild(s->stable, s->item) && child[2] == s->stable && child[4] == 1);

    /* a later stratagem: its row and the raise only; the instructions already read the moved list */
    HfMod* n = ContentMod("n");
    ContentChange(NULL, n);
    assert(ContentStratagem(n, "t", 0) == STR_VANILLA + 1 && ContentCommit());
    assert(StratagemSitesAt(g, table) && BoundsAt(g, 2) && *(uint8_t**)(table + (STR_VANILLA + 1) * 8) == g_stratagems[1].row);
    assert(!strcmp(*(const char**)(table + STR_NAMES_AT + (STR_VANILLA + 2) * 8), "Count") && g_fakeOwnedCount == 2);

    /* n out: a tombstone, its item given back; its type stays in the list for whatever holds it, but not in the loadout:
     * out of the history's record (the later slots moved down) and the profile's pairs */
    uint8_t* history = (uint8_t*)calloc(1, LS_RECORDS + 4);
    uint8_t* profile = (uint8_t*)calloc(1, 0x200);
    uint8_t* loadout = history + LS_LOADOUT;
    *(uint32_t*)(history + LS_RECORDS) = 1;
    *(uint64_t*)history = 1;
    const uint32_t held[3] = {STR_VANILLA, STR_VANILLA + 1, 118};
    for (int k = 0; k < 3; k++) *(uint32_t*)(loadout + LS_SLOTS + k * LS_SLOT) = held[k];
    *(uint32_t*)(loadout + LS_SLOT_COUNT) = 3;
    uint32_t* pairs = (uint32_t*)(profile + LS_PAIRS);
    pairs[0] = g_stratagems[1].stable; pairs[1] = 1; pairs[2] = 0x1234; pairs[3] = 1;
    memcpy(game + LS_HISTORY, &history, 8);
    memcpy(game + LS_PROFILE, &profile, 8);
    memcpy(game + LS_MANAGER, &g_fakeCatalogue, 8);
    for (size_t i = 0; i < sizeof kLoadoutPins / sizeof kLoadoutPins[0]; i++) memcpy(game + kLoadoutPins[i].rva, kLoadoutPins[i].bytes, kLoadoutPins[i].size);
    ContentChange(n, NULL);
    assert(ContentCommit() && LiveTomb(LC_STRATAGEM, 1) && g_fakeTakes == 1 && ApiStratagemRow(STR_VANILLA + 1) == 0 && BoundsAt(g, 2));
    assert(*(uint32_t*)(loadout + LS_SLOT_COUNT) == 2 && *(uint32_t*)(loadout + LS_SLOTS) == STR_VANILLA && *(uint32_t*)(loadout + LS_SLOTS + LS_SLOT) == 118 &&
        *(uint32_t*)(loadout + LS_SLOTS + 2 * LS_SLOT) == 0 && pairs[0] == 0x1234 && pairs[2] == 0);
    /* a weapon going: the profile's back to the default and pushed; one the push did not reach set in the record */
    Scrub weapon = {.weapons = {0x777}, .weaponCount = 1};
    *(uint32_t*)(profile + 0x44) = 0x777;
    *(uint32_t*)(loadout + 0x60) = 0x777;
    uint8_t* peers = (uint8_t*)calloc(1, 0xB3A0);
    *(uint64_t*)(peers + 0xB398) = 1;
    memcpy(game + LS_PEER, &peers, 8);
    g_scrubPush = FakeScrubPush;
    assert(ScrubNeeded(g, &weapon) && ScrubReady(g) == NULL);
    ScrubStep(g, &weapon);
    assert(*(uint32_t*)(profile + 0x44) == kDefaultWeapons[1] && weapon.pushed && g_fakePushes == 1 && *(uint32_t*)(loadout + 0x60) == 0x777);
    ScrubVerify(g, &weapon);
    assert(*(uint32_t*)(loadout + 0x60) == kDefaultWeapons[1] && !ScrubNeeded(g, &weapon));
    *(uint32_t*)(g_fakeCatalogue + 0xDDCF8) = 11;
    assert(ScrubReady(g) != NULL);
    g_scrubPush = NULL;
    free(history); free(profile); free(peers);
    printf("  live content: stratagem, damage and projectile lists moved under one freeze, a later stratagem raised, one taken out (out of the loadout too); items owned and given back\n");

    VirtualFree(game, 0, MEM_RELEASE);
    g_lcFixtureGame = 0;
    g_lcFixtureGameSize = 0;
    memset(g_lcTomb, 0, sizeof g_lcTomb);
    g_stratagemTable = NULL; g_projectileTable = NULL; g_damageTable = NULL; g_projectileBlock = NULL;
    g_stratagemCount = stratagems; g_projectileCount = projectiles; g_damageCount = damages;
    g_stratagemState = g_projectileState = g_damageState = 0;
    g_ownAdd = NULL; g_ownSort = NULL; g_ownRemove = NULL; g_ownManager = NULL; g_ownApply = NULL;
    free(g_fakeCatalogue);
    g_modCount = mods;
    ContentEventsRestore();
}

/* A behaviour added live to the stubs a boot built: its answers in the stubs' tables, every COUNT raised; it resumes as its
 * base does. */
static void ContentBehaviourFixture(void)
{
    ContentEventsSave();
    int mods = g_modCount;
    size_t size = 0x1000000;
    uint8_t* game = BehaviourGame(size);
    HfMod* boot = &g_mods[0];
    g_behaviourCount = 0; g_behaviourState = 0;
    HfBehaviour transport = {sizeof transport, .base = HF_BEHAVIOUR_PELICAN_TRANSPORT}, turret = {sizeof turret, .base = HF_BEHAVIOUR_PELICAN_TURRET};
    assert(ApiAddBehaviour(boot, &transport) == BHV_FIRST && InstallBehaviours((uintptr_t)game) == 1 && g_bhvCountSites == 7);
    HfMod* m = ContentMod("zz.live.behaviour");
    ContentChange(NULL, m);
    g_liveSetup = m;
    assert(ApiAddBehaviour(m, &turret) == BHV_FIRST + 1);
    g_liveSetup = NULL;
    g_lcFixtureGame = (uintptr_t)game;
    g_lcFixtureGameSize = size;
    assert(ContentCommit());
    for (int k = 0; k < g_bhvCountSites; k++) assert(*(uint32_t*)g_bhvCountAt[k] == 2);
    ResumeFn resume = BehaviourResume(game);
    assert(resume(NULL, BHV_FIRST + 1) == (int)(BHV_PELICAN_TURRET - BHV_RESUME_FIRST) && resume(NULL, BHV_FIRST + 2) == -2);
    printf("  live content: a behaviour added to the boot's stubs (7 counts raised), resuming as its base\n");
    g_lcFixtureGame = 0;
    g_lcFixtureGameSize = 0;
    g_behaviourCount = 0; g_behaviourState = 0; g_bhvGame = 0; g_bhvBlock = NULL;
    VirtualFree(game, 0, MEM_RELEASE);
    g_modCount = mods;
    ContentEventsRestore();
}

/* An entity name whose resource id sorts below (or above) another's. */
static const char* ContentEntityName(const char* prefix, uint64_t than, int below, char* buffer)
{
    for (int i = 0; i < 1000; i++)
    {
        sprintf(buffer, "%s:e%d", prefix, i);
        uint64_t h = Murmur64A(buffer, strlen(buffer));
        if (below ? h < than : h > than) return buffer;
    }
    assert(0);
    return NULL;
}

static uint64_t ContentEntity(HfMod* mod, uint64_t base, const char* name)
{
    HfEntity e = {sizeof e, .base = base, .name = name};
    g_liveSetup = mod;
    uint64_t entity = ApiAddEntity(mod, &e);
    g_liveSetup = NULL;
    return entity;
}

/* Entities added live to a fake entity state: short ids after the vanilla ones in resource id order, the copies made
 * at the first; one sorting before a staying one refused; a new version's identical entity kept. */
static void ContentWeaponsFixture(void)
{
    ContentEventsSave();
    int mods = g_modCount;
    uintptr_t was = g_game;
    g_weaponCount = g_ownedCount = 0; g_loadoutTable = NULL; g_regList = NULL;
    int hooked = g_registryHooked, active = g_registryActive;
    g_registryHooked = g_registryActive = 1;
    const uint64_t base = 0x37CDE43876BA26BBull;
    uint8_t* gameState = calloc(1, REG_STATE_GLOBAL + 8);
    uint8_t* state = calloc(1, REG_LIST + 8);
    g_game = (uintptr_t)gameState;
    memcpy(gameState + REG_STATE_GLOBAL, &state, 8);
    uint8_t* behaviours = FakeTable(284, 2008 * 16 + 12 * 4);
    FakeRow(behaviours, 2008, base, 1);
    SetStatePointer(state, REG_TABLES + 8 * 284, behaviours);
    SetStatePointer(state, REG_TABLES + 8 * REG_LOADOUT, FakeTable(REG_LOADOUT, REG_LOADOUT_SLOTS * 16 + REG_LOADOUT_RECORDS * REG_LOADOUT_STRIDE));
    static uint16_t components[] = {REG_LOADOUT, 284};
    uint8_t* map = calloc(REG_MAP_SLOTS, 32);
    uint8_t* row = FreeRow(map, REG_MAP_SLOTS, 32, base);
    uint16_t* list = components;
    uint64_t count = 2;
    uint32_t shortId = 77, mapIndex = (uint32_t)((row - map) / 32), listBytes = 8;
    memcpy(row, &base, 8); memcpy(row + 8, &list, 8); memcpy(row + 16, &count, 8); memcpy(row + 24, &shortId, 4);
    uint8_t* shortIds = calloc(1, 32 + 8);
    memcpy(shortIds + 20, &listBytes, 4); memcpy(shortIds + 32, &shortId, 4); memcpy(shortIds + 36, &mapIndex, 4);
    SetStatePointer(state, REG_MAP, map);
    SetStatePointer(state, REG_LIST, shortIds + 32);
    InterlockedExchange(&g_fired[HF_EVENT_ENTITIES_READY], 1);

    /* the first: short id 78, the copies made and the game pointed at them */
    HfMod* w = ContentMod("w");
    ContentChange(NULL, w);
    uint64_t first = ContentEntity(w, base, "w:copy");
    assert(first != 0 && g_weapons[0].state == 2 && ContentCommit());
    assert(g_weapons[0].state == 1 && g_weapons[0].shortId == 78 && FindRow(map, REG_MAP_SLOTS, 32, first) != NULL && g_loadoutTable != NULL &&
        StatePointer(state, REG_LIST) == g_regList + 32 && g_regListCount == 2 && FindRow(behaviours, 2008, 16, first) != NULL);
    /* one sorting before it: refused; one after it: 79 */
    char name[32];
    HfMod* x = ContentMod("x");
    ContentChange(NULL, x);
    assert(ContentEntity(x, base, ContentEntityName("x", first, 1, name)) != 0 && !ContentCommit() && g_weaponCount == 1);
    ContentChange(NULL, x);
    uint64_t second = ContentEntity(x, base, ContentEntityName("x", first, 0, name));
    char other[32];
    assert(ContentCommit() && g_weapons[1].shortId == 79 && g_regListCount == 3);
    /* x's new version defining the same entity keeps its registration */
    HfMod* x2 = ContentMod("x");
    ContentChange(x, x2);
    assert(ContentEntity(x2, base, name) == second && ContentCommit() && g_weaponCount == 2 && g_weapons[1].mod == x2 &&
        !LiveTomb(LC_WEAPON, 1) && g_regListCount == 3);
    /* w out while x stays: a boot gives x 78, refused; x out: a tombstone at 79, after which a new entity would take 80
     * where a boot gives it 79, refused; x back: its old registration again */
    ContentChange(w, NULL);
    assert(!ContentCommit() && !LiveTomb(LC_WEAPON, 0));
    w->removed = 0;
    ContentChange(x2, NULL);
    assert(ContentCommit() && LiveTomb(LC_WEAPON, 1) && !LiveTomb(LC_WEAPON, 0));
    HfMod* y = ContentMod("y");
    ContentChange(NULL, y);
    assert(ContentEntity(y, base, ContentEntityName("y", second, 0, other)) != 0 && !ContentCommit() && g_weaponCount == 2);
    HfMod* x3 = ContentMod("x");
    ContentChange(NULL, x3);
    assert(ContentEntity(x3, base, name) == second && ContentCommit() && !LiveTomb(LC_WEAPON, 1) && g_weapons[1].mod == x3 && g_weaponCount == 2);
    printf("  live content: entities registered live in resource id order (copies made at the first), a new version's and a returning one's kept, 3 refusals\n");

    memset(g_lcTomb, 0, sizeof g_lcTomb);
    g_weaponCount = 0; g_loadoutTable = NULL; g_regList = NULL; g_regListCount = 0; g_ownedCount = 0;
    g_registryHooked = hooked; g_registryActive = active;
    g_game = was;
    g_modCount = mods;
    ContentEventsRestore();
}

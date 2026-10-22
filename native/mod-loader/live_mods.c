/* Live changes of the session's mods (HellForge-Open docs/mods.md "Changing mods while you play"): the session's
 * backend sends a change of the loader's mods over the live pipe as one transaction, and the loader takes all of it or
 * none. This phase takes stats mods: their stat lines (a mod's componentWrites are stat lines too)
 * added, removed or replaced, each mod keeping its place in load order. A change with anything else is refused
 * ("capability"), and the backend restarts the session as before. Included by hellforge_modloader.c after lua_bridge.c:
 * it adds mods to g_mods under the bridge's lock.
 *
 *   modbegin   <seq> <txn> <solo|sync> <target digest>
 *   modorder   <seq> <txn> <index> <mod id>          the loader's mods after the change, 0 loads first
 *   modadd     <seq> <txn> <mod id> <class> <hash16> <folder> <dll or ->
 *   modreplace <seq> <txn> <mod id> <class> <hash16> <folder> <dll or ->
 *   modstat    <seq> <txn> <mod id> <kind> <component> <resource> <record> <offset> <storage> <value> <expect> <own>
 *              <shared> <unverified> <label>         a plan's stat line after "stat", for a mod added or replaced
 *   modremove  <seq> <txn> <mod id>
 *   modcommit  <seq> <txn>
 *   modabort   <seq> <txn>
 * Each answers <seq> ok now, or <seq> refused <reason> <why>; a refusal ends the change. modcommit answers "ok queued" once
 * the change is made, and the loader then sends on its own "modstate <txn> <mod id> <added|removed|replaced> <applied>
 * <refused>" for each mod and "modresult <txn> ok". Reasons: state, coop, busy, capability, capacity, malformed.
 *
 * Layers. A field holds, bottom up: what it held before any stats mod, the stats
 * mods' values in load order (the boot's and live ones alike), code mods' deferred edits, then live edits by rank
 * (fields.c). A change writes the new top of the stats layers only where nothing above holds the field: under a deferred
 * edit nothing is written (the code mod's value stays, as at boot); under live edits the lowest one's original (what its
 * reset puts back) becomes the new top. A removed mod's stat entries are retired in place (no kind, an unloaded stand-in
 * mod), so every pass and reader skips them, and a later change reuses their slots; nothing in g_stats moves, since
 * other threads read it. The change runs on the pipe's thread under g_fieldsLock, then g_statsLock (the order set_live
 * and set_stat take them); the game's thread is not involved. A line the session cannot place yet (its kind's boot pass
 * has not run) refuses the change: the boot pass would apply it out of load order. */

#define TXN_MODS 64
enum { TXN_ADD, TXN_REMOVE, TXN_REPLACE };
static const char* const kTxnActions[] = {"added", "removed", "replaced"};
typedef struct TxnMod { char id[80]; int action; char folder[MAX_PATH * 3]; HfMod* mod; int applied, refused; } TxnMod;
typedef struct TxnStat { StatEdit edit; int mod; uintptr_t at; } TxnStat;
/* A field the change touched: where, its size, and what it held before any stats mod (known 0: a deferred edit holds
 * it, and keeps no original). */
typedef struct TxnField { uintptr_t at; uint32_t size, bottom; int known; } TxnField;

static struct
{
    int open;
    char id[40];
    char order[MAX_MODS][80];
    int orderCount;
    TxnMod mods[TXN_MODS];
    int modCount;
    TxnStat* stats;
    int statCount, statCapacity;
    TxnField* fields;
    int fieldCount, fieldCapacity;
} g_txn;
/* A retired stat entry's mod: never loaded, so every pass skips the entry. */
static HfMod g_retiredMod = {.id = "retired"};

static int LivePlayers(void)
{
    uintptr_t game = ApiModuleBase("game.dll", NULL), manager = 0;
    uint32_t count = 0;
    if (game == 0 || SafeRead(game + EVT_PLAYERS, &manager, 8) || manager == 0 || SafeRead(manager + 0x84, &count, 4)) return -1;
    return (int)count;
}
/* The players in the game now (-1 unknown); the native fixture replaces it. */
static int (*g_livePlayers)(void) = LivePlayers;
/* The entity load's state (registry.c); the native fixture replaces it. */
static uint8_t* (*g_liveEntityState)(void) = EntityState;

/* Why no change can run now ("<reason>\t<why>"), or NULL. */
static const char* TxnGate(void)
{
    HfSession session;
    ApiSession(NULL, &session);
    if (session.role == HF_SESSION_JOINER) return "coop\tthis game plays in a friend's lobby";
    if (g_livePlayers() > 1) return "coop\tanother player is in the game";
    return NULL;
}

static void TxnReset(void)
{
    g_txn.open = 0;
    g_txn.id[0] = 0;
    g_txn.orderCount = g_txn.modCount = g_txn.statCount = g_txn.fieldCount = 0;
}

static void LiveModsAbort(const char* why)
{
    if (!g_txn.open) return;
    Note("live mod change %s dropped: %s", g_txn.id, why);
    TxnReset();
}

static void TxnRefuse(char* reply, size_t size, const char* seq, const char* reason, const char* why)
{
    snprintf(reply, size, "%s\trefused\t%s\t%s", seq, reason, why);
    if (g_txn.open) Note("live mod change %s refused (%s): %s", g_txn.id, reason, why);
    TxnReset();
}

static TxnMod* TxnFind(const char* id)
{
    for (int i = 0; i < g_txn.modCount; i++) if (!strcmp(g_txn.mods[i].id, id)) return &g_txn.mods[i];
    return NULL;
}

/* The session's mod of that id the loader started or added (not a Lua mod), or NULL. */
static HfMod* TxnSessionMod(const char* id)
{
    for (int i = 0; i < g_modCount; i++) if (!g_mods[i].lua && !strcmp(g_mods[i].id, id)) return &g_mods[i];
    return NULL;
}

/* A mod this phase can change live: a data-only stats mod (no DLL), none of its lines "own" (their record clones are
 * made only as the entity data loads). NULL, or why not. */
static const char* TxnStatsOnly(const HfMod* mod)
{
    if (mod->lua || wcscmp(mod->dll, L"-")) return "it runs code (a DLL); code mods change at the next session";
    for (int i = 0; i < g_statCount; i++)
        if (g_stats[i].mod == mod && g_stats[i].own) return "it gives an entity a record of its own (\"own\"), which needs a new session";
    return NULL;
}

/* Where a stat line applies now: its field, 0 with *why (refused, as the boot pass refuses it), or 0 with *later when
 * its kind's boot pass has not run yet. Reads only; called with g_statsLock held. */
static uintptr_t TxnStatAddress(StatEdit* e, int* later, const char** why)
{
    *later = 0;
    *why = NULL;
    uintptr_t at = 0;
    int phase = !strcmp(e->kind, "ProjectileSettings") ? 0 : !strcmp(e->kind, "DamageInfo") ? 1 : !strcmp(e->kind, "StratagemDefinition") ? 2 : -1;
    int extra = StatsExtraKind(e->kind);
    if (!strcmp(e->kind, "component"))
    {
        if (!g_statsComponentsReady) { *later = 1; return 0; }
        int expect = e->hasExpect;
        e->hasExpect = 0; /* checked against the bottom of the field's layers instead (TxnBottom) */
        at = strcmp(g_build, "25480438") ? 0 : StatsComponent(e, g_liveEntityState());
        e->hasExpect = expect;
        if (!at) *why = "component layout or ownership differs from the reviewed record";
    }
    else if (phase >= 0)
    {
        if (!g_statsTablesDone[phase]) { *later = 1; return 0; }
        at = StatsTableField(phase, ApiModuleBase("game.dll", NULL), e, why);
    }
    else if (!strcmp(e->kind, "BoosterSettings"))
    {
        if (!g_boostersDone) { *later = 1; return 0; }
        at = BoosterField(e, BoosterTable(), why);
    }
    else if (!strcmp(e->kind, "MeleeHitVolume"))
    {
        if (!g_meleeDone) { *later = 1; return 0; }
        at = MeleeField(e, why);
    }
    else if (!strncmp(e->kind, "Passive", 7))
    {
        if (!g_perksDone) { *later = 1; return 0; }
        if (!(at = PerkField(e))) *why = "the armor perk's entry is not as reviewed";
    }
    else if (extra >= 0)
    {
        if (!g_statsExtraDone[extra]) { *later = 1; return 0; }
        at = strcmp(g_build, "25480438") ? 0 : StatsExtraRow(e);
        if (!at) *why = "settings allocation or delta ownership/layout is not uniquely as reviewed";
    }
    else *why = "not a kind of stats HellForge edits";
    return at;
}

/* The edits above the stats layers at a field: whether a code mod's deferred edit holds any of its bytes, and the lowest
 * live edit of exactly this field (NULL none). Called with g_fieldsLock held. */
static FieldEdit* TxnAbove(uintptr_t at, uint32_t size, int* deferred)
{
    *deferred = 0;
    FieldEdit* lowest = NULL;
    for (int i = 0; i < g_editCount; i++)
    {
        FieldEdit* e = &g_edits[i];
        if (e->failed || e->waiting || e->address == 0 || e->kind == EDIT_TAG) continue;
        if (e->address >= at + size || e->address + e->field->size <= at) continue;
        if (!e->live) *deferred = 1;
        else if (lowest == NULL && e->address == at && e->field->size == size) lowest = e;
    }
    return lowest;
}

static TxnField* TxnFieldAt(uintptr_t at, uint32_t size)
{
    for (int i = 0; i < g_txn.fieldCount; i++) if (g_txn.fields[i].at == at && g_txn.fields[i].size == size) return &g_txn.fields[i];
    return NULL;
}

/* What the field held before any stats mod: an applied stat entry's original (each keeps the bottom: StatsWrite chains
 * them), else what is under the edits above the stats (the lowest live edit's original, a live attachment change's
 * before), else the field now. 0 when a deferred edit holds it with no stats entry under it. Both locks held. */
static int TxnBottom(uintptr_t at, uint32_t size, uint32_t* bottom)
{
    *bottom = 0;
    const TxnField* touched = TxnFieldAt(at, size);
    if (touched != NULL) { *bottom = touched->bottom; return touched->known; }
    for (int i = 0; i < g_statCount; i++)
        if (g_stats[i].applied && g_stats[i].address == at && StatSize(&g_stats[i]) == size) { *bottom = g_stats[i].original; return 1; }
    int deferred = 0;
    const FieldEdit* live = TxnAbove(at, size, &deferred);
    if (deferred) return 0;
    if (live != NULL && live->hasOriginal) { memcpy(bottom, live->original, size); return 1; }
    if (LiveExtraBefore(at, bottom)) return 1;
    return !SafeRead(at, bottom, size);
}

/* Notes a field the change touches (before anything changes at it). 0 when the list is full. */
static int TxnTouch(uintptr_t at, uint32_t size)
{
    if (TxnFieldAt(at, size) != NULL) return 1;
    uint32_t bottom = 0;
    int known = TxnBottom(at, size, &bottom);
    if (g_txn.fieldCount == g_txn.fieldCapacity)
    {
        int capacity = g_txn.fieldCapacity ? g_txn.fieldCapacity * 2 : 256;
        TxnField* grown = capacity <= MAX_STATS * 2 ? (TxnField*)realloc(g_txn.fields, (size_t)capacity * sizeof *grown) : NULL;
        if (grown == NULL) return 0;
        g_txn.fields = grown;
        g_txn.fieldCapacity = capacity;
    }
    g_txn.fields[g_txn.fieldCount++] = (TxnField){at, size, bottom, known};
    return 1;
}

/* The field's stats layers changed: their top (the last in load order, a later line of a mod over an earlier one, else
 * the bottom) goes where nothing above holds the field. Both locks held; 0 when the write did not hold. */
static int TxnLayerApply(const TxnField* f)
{
    uint32_t top = f->bottom;
    int best = -1, bestOrder = -1;
    for (int i = 0; i < g_statCount; i++)
    {
        const StatEdit* s = &g_stats[i];
        if (!s->applied || s->address != f->at || StatSize(s) != f->size) continue;
        int order = ModLoadIndex(s->mod);
        if (order >= bestOrder) { best = i; bestOrder = order; }
    }
    if (best >= 0) top = g_stats[best].value;
    else if (!f->known) return 1; /* under a deferred edit with no stats left: nothing to write, nothing known to restore */
    int deferred = 0;
    FieldEdit* live = TxnAbove(f->at, f->size, &deferred);
    if (deferred) return 1;
    if (live != NULL) { if (live->hasOriginal) memcpy(live->original, &top, f->size); return 1; }
    for (int i = 0; i < g_liveExtraCount; i++)
    {
        LiveExtra* x = &g_liveExtra[i];
        if (x->address != f->at) continue;
        x->before = top;
        for (int r = RANK_CODE; r <= RANK_EDITOR; r++) if (x->layer[r].held) return 1;
    }
    uint32_t now = 0, check = 0;
    if (SafeRead(f->at, &now, f->size)) return 0;
    if (!memcmp(&now, &top, f->size)) return 1;
    return !ApiWrite(f->at, &top, f->size) && !SafeRead(f->at, &check, f->size) && !memcmp(&check, &top, f->size);
}

/* A free slot for a stat entry: a retired one, else the next; -1 when g_stats is full. */
static int TxnStatSlot(int* from)
{
    for (; *from < g_statCount; (*from)++)
        if (g_stats[*from].mod == &g_retiredMod) return (*from)++;
    return g_statCount < MAX_STATS ? g_statCount : -1;
}

static int TxnFreeSlots(void)
{
    int free = MAX_STATS - g_statCount;
    for (int i = 0; i < g_statCount; i++) free += g_stats[i].mod == &g_retiredMod;
    return free;
}

/* An added mod's entry: the one it had (a stats mod removed earlier in the session, its slot kept), else a new one in
 * g_mods. NULL when the mods are full. */
static HfMod* TxnModEntry(const TxnMod* t)
{
    HfMod* mod = TxnSessionMod(t->id);
    HANDLE log = INVALID_HANDLE_VALUE;
    if (mod == NULL)
    {
        /* opened before the lock: the game's thread queues Lua events under it */
        wchar_t logName[128];
        swprintf(logName, 128, L"mod-%hs.log", t->id);
        log = OpenLog(logName);
    }
    AcquireSRWLockExclusive(&g_luaLock);
    if (mod == NULL && g_modCount < MAX_MODS)
    {
        HfMod* m = &g_mods[g_modCount];
        memset(m, 0, sizeof *m);
        m->api = &g_api;
        snprintf(m->id, sizeof m->id, "%s", t->id);
        wcscpy(m->dll, L"-");
        m->log = log;
        log = INVALID_HANDLE_VALUE;
        m->order = g_modCount;
        MemoryBarrier(); /* the entry is whole before anything counting the mods sees it */
        g_modCount++;
        mod = m;
    }
    if (mod != NULL)
    {
        snprintf(mod->folder, sizeof mod->folder, "%s", t->folder);
        mod->loaded = 1;
    }
    ReleaseSRWLockExclusive(&g_luaLock);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log); /* the mods were full */
    return mod;
}

static void TxnRetire(StatEdit* e)
{
    e->applied = 0;
    e->refWaiting = 0;
    e->kind[0] = 0;
    MemoryBarrier();
    e->mod = &g_retiredMod;
}

/* The checks before anything changes: the gate, each mod's kind, the order, room, and every line placeable. NULL, or
 * "<reason>\t<why>" (written into why). */
static const char* TxnCheck(char* why, size_t size)
{
    const char* gate = TxnGate();
    if (gate != NULL) return gate;
    for (int i = 0; i < g_txn.modCount; i++)
    {
        const TxnMod* t = &g_txn.mods[i];
        HfMod* mod = TxnSessionMod(t->id);
        int present = mod != NULL && mod->loaded;
        if (t->action == TXN_ADD && present) { snprintf(why, size, "malformed\t%s is already loaded", t->id); return why; }
        if (t->action != TXN_ADD && !present) { snprintf(why, size, "malformed\t%s is not loaded", t->id); return why; }
        const char* not = mod != NULL && (present || wcscmp(mod->dll, L"-")) ? TxnStatsOnly(mod) : NULL;
        if (not != NULL) { snprintf(why, size, "capability\t%s: %s", t->id, not); return why; }
    }
    for (int i = 0; i < g_txn.statCount; i++)
        if (g_txn.stats[i].edit.own)
        {
            snprintf(why, size, "capability\t%s: it gives an entity a record of its own (\"own\"), which needs a new session", g_txn.mods[g_txn.stats[i].mod].id);
            return why;
        }
    /* the order: every loader mod that stays, in the order it had, and the added ones anywhere */
    int last = -1;
    for (int i = 0; i < g_txn.orderCount; i++)
    {
        const TxnMod* t = TxnFind(g_txn.order[i]);
        HfMod* mod = TxnSessionMod(g_txn.order[i]);
        if (t != NULL && t->action == TXN_REMOVE) { snprintf(why, size, "malformed\tthe order lists %s, which is removed", t->id); return why; }
        if (t != NULL && t->action == TXN_ADD) continue;
        if (mod == NULL) { snprintf(why, size, "malformed\tthe order lists %s, which is not a mod of this session", g_txn.order[i]); return why; }
        if (mod->order <= last) { snprintf(why, size, "capability\tthe mods' order changed (%s); a new order needs a new session", mod->id); return why; }
        last = mod->order;
    }
    for (int i = 0; i < g_modCount; i++)
    {
        const HfMod* mod = &g_mods[i];
        const TxnMod* t = TxnFind(mod->id);
        if (mod->lua || !mod->loaded || (t != NULL && t->action == TXN_REMOVE)) continue;
        int listed = 0;
        for (int k = 0; k < g_txn.orderCount && !listed; k++) listed = !strcmp(g_txn.order[k], mod->id);
        if (!listed) { snprintf(why, size, "malformed\tthe order leaves out %s", mod->id); return why; }
    }
    int adds = 0;
    for (int i = 0; i < g_txn.modCount; i++) adds += g_txn.mods[i].action == TXN_ADD && TxnSessionMod(g_txn.mods[i].id) == NULL;
    if (g_modCount + adds > MAX_MODS) { snprintf(why, size, "capacity\tthe session has %d mods, the most it can hold", MAX_MODS); return why; }
    if (g_txn.statCount > TxnFreeSlots()) { snprintf(why, size, "capacity\tthe session holds %d stats edits at most", MAX_STATS); return why; }
    for (int i = 0; i < g_txn.statCount; i++)
    {
        TxnStat* s = &g_txn.stats[i];
        int later = 0;
        const char* refused = NULL;
        s->at = TxnStatAddress(&s->edit, &later, &refused);
        if (later)
        {
            snprintf(why, size, "state\t%s: the game has not loaded its %s values yet; try again in a moment", g_txn.mods[s->mod].id, s->edit.kind);
            return why;
        }
    }
    return NULL;
}

/* The change itself, once checked: removals and replacements retire their entries, the added lines take slots, then
 * each touched field gets its new top. Both locks held. */
static void TxnApply(void)
{
    /* every field the change touches, with its bottom, before anything at it changes */
    for (int i = 0; i < g_statCount; i++)
    {
        StatEdit* e = &g_stats[i];
        const TxnMod* t = e->mod != NULL ? TxnFind(e->mod->id) : NULL;
        if (t != NULL && t->action != TXN_ADD && e->mod == TxnSessionMod(t->id) && e->applied) TxnTouch(e->address, StatSize(e));
    }
    for (int i = 0; i < g_txn.statCount; i++) if (g_txn.stats[i].at) TxnTouch(g_txn.stats[i].at, StatSize(&g_txn.stats[i].edit));
    for (int i = 0; i < g_txn.modCount; i++)
    {
        TxnMod* t = &g_txn.mods[i];
        HfMod* mod = TxnSessionMod(t->id);
        if (t->action == TXN_ADD) mod = TxnModEntry(t);
        t->mod = mod;
        if (t->action == TXN_ADD || mod == NULL) continue;
        for (int k = 0; k < g_statCount; k++) if (g_stats[k].mod == mod) TxnRetire(&g_stats[k]);
        if (t->action == TXN_REMOVE) mod->loaded = 0;
        else snprintf(mod->folder, sizeof mod->folder, "%s", t->folder);
    }
    for (int i = 0; i < g_txn.orderCount; i++)
    {
        HfMod* mod = TxnSessionMod(g_txn.order[i]);
        if (mod != NULL) mod->order = i;
    }
    int from = 0;
    for (int i = 0; i < g_txn.statCount; i++)
    {
        TxnStat* s = &g_txn.stats[i];
        TxnMod* t = &g_txn.mods[s->mod];
        StatEdit e = s->edit;
        e.mod = t->mod;
        int slot = TxnStatSlot(&from);
        if (slot < 0 || e.mod == NULL) { t->refused++; continue; } /* (checked: never) */
        uint32_t bottom = 0;
        int known = s->at ? TxnBottom(s->at, StatSize(&e), &bottom) : 0;
        const char* why = NULL;
        if (!s->at) why = "its field was not found";
        else if (e.hasExpect && !known) why = "a code mod's edit holds the field, so its original value is not known";
        else if (e.hasExpect && bottom != e.expect) why = "original value differs from expect";
        else if (e.floating) { float v; memcpy(&v, &bottom, 4); if (!isfinite(v)) why = "current float is not finite"; }
        int logged = 0; /* RefStat refused it, and said why */
        if (why == NULL)
        {
            int ref = RefStat(&e, s->at, bottom);
            if (ref == REF_WAIT) why = "it names a row whose packages are not loaded yet; change it again once they are";
            else if (ref == REF_REFUSED) { why = "its reference was refused"; logged = 1; }
            e.refWaiting = 0; /* a waiting reference would be written later out of load order (StatsRefsTick) */
        }
        if (why == NULL)
        {
            e.address = s->at;
            e.original = bottom;
            e.hasBaseline = 1;
        }
        e.applied = 0;
        if (slot == g_statCount) { g_stats[slot] = e; MemoryBarrier(); g_statCount++; }
        else { g_stats[slot] = e; MemoryBarrier(); }
        if (why != NULL) { if (!logged) StatsRefuse(&g_stats[slot], why); t->refused++; continue; }
        g_stats[slot].applied = 1;
        if (e.shared) ApiLog(e.mod, "stats %s: shared record; all consumers are affected", e.label);
        if (e.unverified) ApiLog(e.mod, "stats %s: gameplay effect is unverified", e.label);
        t->applied++;
    }
    for (int i = 0; i < g_txn.fieldCount; i++)
        if (!TxnLayerApply(&g_txn.fields[i])) Note("live mod change %s: the write at 0x%llx did not hold", g_txn.id, (unsigned long long)g_txn.fields[i].at);
    for (int i = 0; i < g_statCount; i++)
    {
        const StatEdit* e = &g_stats[i];
        const TxnMod* t = e->applied && e->mod != NULL ? TxnFind(e->mod->id) : NULL;
        if (t == NULL || t->action == TXN_REMOVE) continue;
        uint32_t now = 0;
        SafeRead(e->address, &now, StatSize(e));
        ApiLog(e->mod, "stats %s: applied live (%08x -> %08x)%s", e->label, e->original, e->value, now == e->value ? "" : " under another mod's value");
    }
}

static void TxnCommit(const char* seq, char* reply, size_t size)
{
    char why[512];
    AcquireSRWLockExclusive(&g_fieldsLock);
    AcquireSRWLockExclusive(&g_statsLock);
    const char* refused = TxnCheck(why, sizeof why);
    if (refused == NULL) TxnApply();
    ReleaseSRWLockExclusive(&g_statsLock);
    ReleaseSRWLockExclusive(&g_fieldsLock);
    if (refused != NULL)
    {
        const char* tab = strchr(refused, '\t');
        char reason[32];
        snprintf(reason, sizeof reason, "%.*s", tab != NULL ? (int)(tab - refused) : (int)strlen(refused), refused);
        TxnRefuse(reply, size, seq, reason, tab != NULL ? tab + 1 : "");
        return;
    }
    snprintf(reply, size, "%s\tok\tqueued", seq);
    char line[256];
    for (int i = 0; i < g_txn.modCount; i++)
    {
        const TxnMod* t = &g_txn.mods[i];
        snprintf(line, sizeof line, "modstate\t%s\t%s\t%s\t%d\t%d", g_txn.id, t->id, kTxnActions[t->action], t->applied, t->refused);
        LiveQueue(line);
        Note("live mod change %s: %s %s, %d stats edit(s) applied, %d refused", g_txn.id, t->id, kTxnActions[t->action], t->applied, t->refused);
    }
    snprintf(line, sizeof line, "modresult\t%s\tok", g_txn.id);
    LiveQueue(line);
    TxnReset();
}

/* One of the change's lines (its verb starts with "mod"): 1 when it was one, with the reply written. */
static int LiveModsRequest(char* line, char* reply, size_t size)
{
    char* f[20];
    char copy[8192];
    snprintf(copy, sizeof copy, "%s", line);
    int n = Split(copy, f, 20);
    if (n < 3) return 0;
    const char *verb = f[0], *seq = f[1], *txn = f[2];
    int begin = !strcmp(verb, "modbegin"), order = !strcmp(verb, "modorder"), add = !strcmp(verb, "modadd"), replace = !strcmp(verb, "modreplace"),
        stat = !strcmp(verb, "modstat"), remove = !strcmp(verb, "modremove"), commit = !strcmp(verb, "modcommit"), abort = !strcmp(verb, "modabort");
    if (!(begin || order || add || replace || stat || remove || commit || abort)) return 0;
    if (begin)
    {
        if (g_txn.open) { snprintf(reply, size, "%s\trefused\tbusy\tanother change (%s) is under way", seq, g_txn.id); return 1; }
        if (n != 5 || strlen(txn) >= sizeof g_txn.id || (strcmp(f[3], "solo") && strcmp(f[3], "sync"))) { TxnRefuse(reply, size, seq, "malformed", "modbegin"); return 1; }
        const char* gate = TxnGate();
        if (gate != NULL)
        {
            char reason[16];
            snprintf(reason, sizeof reason, "%.*s", (int)(strchr(gate, '\t') - gate), gate);
            TxnRefuse(reply, size, seq, reason, strchr(gate, '\t') + 1);
            return 1;
        }
        TxnReset();
        g_txn.open = 1;
        snprintf(g_txn.id, sizeof g_txn.id, "%s", txn);
        Note("live mod change %s begins (%s): the mods' digest becomes %s", txn, f[3], f[4]);
        snprintf(reply, size, "%s\tok\tnow", seq);
        return 1;
    }
    if (!g_txn.open || strcmp(txn, g_txn.id)) { snprintf(reply, size, "%s\trefused\tmalformed\tno change %s is under way", seq, txn); return 1; }
    if (abort) { LiveModsAbort("the backend withdrew it"); snprintf(reply, size, "%s\tok\tnow", seq); return 1; }
    if (commit) { TxnCommit(seq, reply, size); return 1; }
    if (order)
    {
        char* end;
        unsigned long index = n == 5 ? strtoul(f[3], &end, 10) : 0;
        if (n != 5 || *end || index != (unsigned long)g_txn.orderCount || index >= MAX_MODS || strlen(f[4]) >= sizeof g_txn.order[0])
        { TxnRefuse(reply, size, seq, "malformed", "modorder"); return 1; }
        snprintf(g_txn.order[g_txn.orderCount++], sizeof g_txn.order[0], "%s", f[4]);
        snprintf(reply, size, "%s\tok\tnow", seq);
        return 1;
    }
    if (add || replace || remove)
    {
        if (remove ? n != 4 : n != 8) { TxnRefuse(reply, size, seq, "malformed", verb); return 1; }
        if (strlen(f[3]) >= sizeof g_txn.mods[0].id || TxnFind(f[3]) != NULL) { TxnRefuse(reply, size, seq, "malformed", "a mod named twice"); return 1; }
        if (g_txn.modCount == TXN_MODS) { TxnRefuse(reply, size, seq, "capacity", "a change holds 64 mods at most"); return 1; }
        if (!remove && (strcmp(f[4], "stats") || strcmp(f[7], "-")))
        {
            char why[160];
            snprintf(why, sizeof why, "%s: a %s mod%s changes at the next session", f[3], f[4], strcmp(f[7], "-") ? " with a DLL" : "");
            TxnRefuse(reply, size, seq, "capability", why);
            return 1;
        }
        TxnMod* t = &g_txn.mods[g_txn.modCount++];
        memset(t, 0, sizeof *t);
        snprintf(t->id, sizeof t->id, "%s", f[3]);
        t->action = add ? TXN_ADD : replace ? TXN_REPLACE : TXN_REMOVE;
        if (!remove) snprintf(t->folder, sizeof t->folder, "%s", f[6]);
        snprintf(reply, size, "%s\tok\tnow", seq);
        return 1;
    }
    /* modstat: f[3] the mod, then the plan's stat fields */
    TxnMod* t = n == 16 ? TxnFind(f[3]) : NULL;
    if (t == NULL || t->action == TXN_REMOVE) { TxnRefuse(reply, size, seq, "malformed", "modstat for no mod added or replaced"); return 1; }
    StatEdit e;
    if (StatsParseInto(f + 2, 14, &e)) { TxnRefuse(reply, size, seq, "malformed", "modstat"); return 1; }
    if (g_txn.statCount == g_txn.statCapacity)
    {
        int capacity = g_txn.statCapacity ? g_txn.statCapacity * 2 : 256;
        TxnStat* grown = capacity <= MAX_STATS * 2 ? (TxnStat*)realloc(g_txn.stats, (size_t)capacity * sizeof *grown) : NULL;
        if (grown == NULL) { TxnRefuse(reply, size, seq, "capacity", "too many stats edits"); return 1; }
        g_txn.stats = grown;
        g_txn.statCapacity = capacity;
    }
    g_txn.stats[g_txn.statCount++] = (TxnStat){e, (int)(t - g_txn.mods), 0};
    snprintf(reply, size, "%s\tok\tnow", seq);
    return 1;
}

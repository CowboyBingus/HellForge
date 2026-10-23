/* The launcher's live stats editor (docs/modding/runtime.md "Live edits"), over the named pipe \\.\pipe\hellforge-live-<session
 * id> that the launcher serves (current user only) while its editor is live in a solo session; in a co-op session the
 * session's backend serves it, to give a joiner the host's stats (docs/multiplayer.md step 5). Included by hellforge_modloader.c after fields.c. The loader connects once the entity data has loaded, and
 * again whenever the launcher stops and starts serving.
 *
 * Lines, UTF-8 and tab-separated:
 *   loader:   hello <protocol> <game build> <stats schema hash>
 *   launcher: set <seq> <kind> <component> <resource hex> <record> <offset> <storage> <value hex> <shared 0|1>
 *             reset <seq> <kind> <component> <resource hex> <record> <offset> <storage>
 *             base <seq> <kind> <component> <resource hex> <record> <offset> <storage>   (without Local Stats' edits)
 *             get <seq> <kind> <component> <resource hex> <record> <offset> <storage>
 *             session <seq> <solo|host|joiner> [<host name>]
 *             notice <seq> <error|info> <title> [<line>...]   (SDK 12: a message the in-game editor shows the player)
 *             syncmod <seq> <version> <index> <have|download|manual|blocked|update|off> <content|code|bundled|raw> <bytes>
 *                     <id> <name> <mod version>
 *             syncstate <seq> <version> <none|offer|blocked|downloading|ready|failed> <host name> <bytes> <done> <mods>
 *                       <message>                                      (syncmod, syncstate: SDK 14, mod sync)
 *   loader:   <seq> ok <now|respawn|mission> [<value hex>]     or     <seq> refused <reason>
 *             syncanswer <version> <accept|decline|restart|block> <mod id or ->   (unasked: the player's answer to an offer)
 * A request names a field as the stats schema does (kind "component" and a component record, or a settings kind and
 * its row id; a stratagem by its stable id), so the editor sends what it would save. Each becomes a set_live edit of the
 * Local Stats mod (zz.local.stats), whose file the editor saves into: same checks, same conflict reports. */

#define LIVE_PROTOCOL 1
#define LIVE_MOD_ID "zz.local.stats"

static HfMod g_liveModStorage;
static HfMod* g_liveMod;
static SRWLOCK g_sessionLock = SRWLOCK_INIT;
static HfSession g_session;

/* SDK 6, session: the co-op role the session's backend last gave (solo until it says otherwise). */
static void ApiSession(HfMod* mod, HfSession* out)
{
    (void)mod;
    if (out == NULL) return;
    AcquireSRWLockShared(&g_sessionLock);
    *out = g_session;
    ReleaseSRWLockShared(&g_sessionLock);
}

static void SetSession(uint32_t role, const char* host)
{
    AcquireSRWLockExclusive(&g_sessionLock);
    if (g_session.role != role || strcmp(g_session.host, host))
    {
        g_session.role = role;
        snprintf(g_session.host, sizeof g_session.host, "%s", host);
        g_session.version++;
        Note("session: %s%s%s%s", role == HF_SESSION_JOINER ? "joined, playing with the host's stats" : role == HF_SESSION_HOST ?
            "hosting: friends play with this machine's stats" : "solo", host[0] ? " (host: " : "", host, host[0] ? ")" : "");
    }
    ReleaseSRWLockExclusive(&g_sessionLock);
}

/* SDK 12, notice: the backend's latest message for the player (why a friend's lobby refused this game's join, say). */
static HfNotice g_notice;

static void ApiNotice(HfMod* mod, HfNotice* out)
{
    (void)mod;
    if (out == NULL) return;
    AcquireSRWLockShared(&g_sessionLock);
    *out = g_notice;
    ReleaseSRWLockShared(&g_sessionLock);
}

/* f: the request's fields after the sequence number: the kind, the title, the lines. */
static void SetNotice(char** f, int n)
{
    AcquireSRWLockExclusive(&g_sessionLock);
    g_notice.kind = !strcmp(f[0], "error") ? HF_NOTICE_ERROR : HF_NOTICE_INFO;
    snprintf(g_notice.title, sizeof g_notice.title, "%s", n > 1 ? f[1] : "");
    size_t used = 0;
    g_notice.text[0] = 0;
    for (int i = 2; i < n && used < sizeof g_notice.text - 1; i++)
    {
        int wrote = snprintf(g_notice.text + used, sizeof g_notice.text - used, "%s%s", used ? "\n" : "", f[i]);
        if (wrote < 0) break;
        used += (size_t)wrote;
    }
    if (used >= sizeof g_notice.text) used = sizeof g_notice.text - 1;
    g_notice.version++;
    Note("notice (%s): %s%s%s", g_notice.kind == HF_NOTICE_ERROR ? "error" : "info", g_notice.title, used ? ": " : "", g_notice.text);
    ReleaseSRWLockExclusive(&g_sessionLock);
}

/* SDK 14, mod sync: the backend's offer to download the host's mods and restart with them (docs/mods.md "Playing with
 * other mods"), for an editor to show (sync_state, sync_mod) and answer (sync_answer). A version's syncmod lines come
 * first and wait in g_syncNext (the pipe's thread alone touches it); its syncstate line publishes them with the state. A
 * syncstate of the version already published is progress (state, done, message): its mods stay. Answers are queued
 * for LiveWatch, the pipe's only writer, so an answer never waits on the pipe's pending read. */
#define SYNC_MAX_MODS 64
static const char* const kSyncStates[] = {"none", "offer", "blocked", "downloading", "ready", "failed"};
static const char* const kSyncActions[] = {"have", "download", "manual", "blocked", "update", "off"};
static const char* const kSyncClasses[] = {"content", "code", "bundled", "raw"};
static SRWLOCK g_syncLock = SRWLOCK_INIT;
static HfSyncState g_sync;
static HfSyncMod g_syncMods[SYNC_MAX_MODS];
static HfSyncMod g_syncNext[SYNC_MAX_MODS];
static uint32_t g_syncNextVersion;
static uint64_t g_syncNextSeen; /* the indices of g_syncNext that arrived */
/* Lines for LiveWatch to write (the answers), and whether a backend is connected to take them. */
static SRWLOCK g_liveOutLock = SRWLOCK_INIT;
static char g_liveOut[1024];
static size_t g_liveOutUsed;
static int g_liveConnected;
static HANDLE g_liveOutEvent;

static int SyncName(const char* text, const char* const* names, int count)
{
    for (int i = 0; i < count; i++) if (!strcmp(text, names[i])) return i;
    return -1;
}

static int SyncNumber(const char* text, uint64_t* out)
{
    char* end;
    if (*text < '0' || *text > '9') return 0;
    *out = strtoull(text, &end, 10);
    return *end == 0;
}

/* Copies text into a field of `size` bytes; cut short, it ends on a whole UTF-8 character. */
static void SyncCopy(char* out, size_t size, const char* text)
{
    size_t length = strlen(text);
    if (length >= size)
    {
        length = size - 1;
        while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80) length--;
    }
    memcpy(out, text, length);
    out[length] = 0;
}

/* Splits at every tab and keeps empty fields (Split skips them): a host name or message may be empty. The last field
 * takes the rest of the line. */
static int SplitAll(char* line, char** fields, int capacity)
{
    int count = 0;
    for (char* at = line; count < capacity;)
    {
        fields[count++] = at;
        char* tab = count < capacity ? strchr(at, '\t') : NULL;
        if (tab == NULL) break;
        *tab = 0;
        at = tab + 1;
    }
    return count;
}

static void SyncModLine(char* line, char* reply, size_t size)
{
    char* f[10];
    int n = SplitAll(line, f, 10);
    uint64_t version = 0, index = 0, bytes = 0;
    int action = n == 10 ? SyncName(f[4], kSyncActions, 6) : -1, kind = n == 10 ? SyncName(f[5], kSyncClasses, 4) : -1;
    if (n != 10 || !SyncNumber(f[2], &version) || version > UINT32_MAX || !SyncNumber(f[3], &index) || !SyncNumber(f[6], &bytes) ||
        action < 0 || kind < 0 || !f[7][0])
    { snprintf(reply, size, "%s\trefused\tmalformed syncmod", n >= 2 ? f[1] : "-"); return; }
    if (index >= SYNC_MAX_MODS) { snprintf(reply, size, "%s\trefused\tan offer lists at most %d mods", f[1], SYNC_MAX_MODS); return; }
    if ((uint32_t)version != g_syncNextVersion)
    {
        memset(g_syncNext, 0, sizeof g_syncNext);
        g_syncNextVersion = (uint32_t)version;
        g_syncNextSeen = 0;
    }
    HfSyncMod* m = &g_syncNext[index];
    memset(m, 0, sizeof *m);
    m->size = sizeof *m;
    m->action = (uint32_t)action;
    m->kind = (uint32_t)kind;
    m->bytes = bytes;
    SyncCopy(m->id, sizeof m->id, f[7]);
    SyncCopy(m->name, sizeof m->name, f[8]);
    SyncCopy(m->version, sizeof m->version, f[9]);
    g_syncNextSeen |= 1ull << index;
    snprintf(reply, size, "%s\tok\tnow", f[1]);
}

static void SyncStateLine(char* line, char* reply, size_t size)
{
    char* f[9];
    int n = SplitAll(line, f, 9);
    uint64_t version = 0, bytes = 0, done = 0, mods = 0;
    int state = n == 9 ? SyncName(f[3], kSyncStates, 6) : -1;
    if (n != 9 || !SyncNumber(f[2], &version) || version > UINT32_MAX || state < 0 || !SyncNumber(f[5], &bytes) || !SyncNumber(f[6], &done) ||
        !SyncNumber(f[7], &mods))
    { snprintf(reply, size, "%s\trefused\tmalformed syncstate", n >= 2 ? f[1] : "-"); return; }
    if (mods > SYNC_MAX_MODS) { snprintf(reply, size, "%s\trefused\tan offer lists at most %d mods", f[1], SYNC_MAX_MODS); return; }
    uint64_t all = mods == 64 ? ~0ull : (1ull << mods) - 1;
    AcquireSRWLockExclusive(&g_syncLock);
    int fresh = (uint32_t)version != g_sync.version;
    const char* why = !fresh && (uint32_t)mods != g_sync.mods ? "its mod count changed without its mods" :
        fresh && mods > 0 && (g_syncNextVersion != (uint32_t)version || (g_syncNextSeen & all) != all) ? "its syncmod lines are missing" : NULL;
    if (why != NULL)
    {
        ReleaseSRWLockExclusive(&g_syncLock);
        snprintf(reply, size, "%s\trefused\t%s", f[1], why);
        return;
    }
    int changed = fresh || g_sync.state != (uint32_t)state;
    if (fresh) memcpy(g_syncMods, g_syncNext, (size_t)mods * sizeof g_syncMods[0]);
    g_sync.size = sizeof g_sync;
    g_sync.version = (uint32_t)version;
    g_sync.state = (uint32_t)state;
    g_sync.mods = (uint32_t)mods;
    g_sync.bytes = bytes;
    g_sync.done = done;
    SyncCopy(g_sync.host, sizeof g_sync.host, f[4]);
    SyncCopy(g_sync.message, sizeof g_sync.message, f[8]);
    HfSyncState noted = g_sync;
    ReleaseSRWLockExclusive(&g_syncLock);
    /* logged after the lock: the editor's render thread reads the state under it */
    if (changed)
        Note("mod sync %u: %s%s%s, %u mod(s), %llu bytes to download%s%s", noted.version, kSyncStates[state], noted.host[0] ? " from " : "",
            noted.host, noted.mods, (unsigned long long)bytes, noted.message[0] ? ": " : "", noted.message);
    snprintf(reply, size, "%s\tok\tnow", f[1]);
}

/* The backend is gone: no offer stands. */
static void SyncClear(void)
{
    AcquireSRWLockExclusive(&g_syncLock);
    if (g_sync.state != HF_SYNC_NONE) Note("mod sync %u: withdrawn (the backend disconnected)", g_sync.version);
    memset(&g_sync, 0, sizeof g_sync);
    ReleaseSRWLockExclusive(&g_syncLock);
}

/* SDK 14, sync_state: the current offer (state HF_SYNC_NONE, and 0, without one). At most out->size bytes are written. */
static int ApiSyncState(HfMod* mod, HfSyncState* out)
{
    (void)mod;
    if (out == NULL || out->size < sizeof(uint32_t)) return 0;
    uint32_t size = out->size;
    AcquireSRWLockShared(&g_syncLock);
    HfSyncState now = g_sync;
    ReleaseSRWLockShared(&g_syncLock);
    memcpy(out, &now, size < sizeof now ? size : sizeof now);
    out->size = size;
    return now.state != HF_SYNC_NONE;
}

/* SDK 14, sync_mod: the index-th mod of the current offer. At most out->size bytes are written. */
static int ApiSyncMod(HfMod* mod, uint32_t index, HfSyncMod* out)
{
    (void)mod;
    if (out == NULL || out->size < sizeof(uint32_t)) return 0;
    uint32_t size = out->size;
    HfSyncMod now;
    AcquireSRWLockShared(&g_syncLock);
    int ok = g_sync.state != HF_SYNC_NONE && index < g_sync.mods;
    if (ok) now = g_syncMods[index];
    ReleaseSRWLockShared(&g_syncLock);
    if (!ok) return 0;
    memcpy(out, &now, size < sizeof now ? size : sizeof now);
    out->size = size;
    return 1;
}

/* SDK 14, sync_answer: the player's answer to the offer of that version, queued for the backend. 0 when another offer
 * came since, none stands, or no backend is connected. */
static int ApiSyncAnswer(HfMod* mod, uint32_t version, uint32_t answer, const char* modId)
{
    (void)mod;
    const char* word = answer == HF_SYNC_ACCEPT ? "accept" : answer == HF_SYNC_DECLINE ? "decline" : answer == HF_SYNC_RESTART ? "restart" :
        answer == HF_SYNC_BLOCK ? "block" : NULL;
    if (word == NULL) return 0;
    char id[65] = "-";
    if (answer == HF_SYNC_BLOCK)
    {
        size_t n = 0;
        for (const char* c = modId != NULL ? modId : ""; *c && n < sizeof id - 1; c++) if ((unsigned char)*c >= 0x20 && *c != 0x7F) id[n++] = *c;
        id[n] = 0;
        if (n == 0) return 0;
    }
    AcquireSRWLockShared(&g_syncLock);
    int current = g_sync.state != HF_SYNC_NONE && g_sync.version == version;
    ReleaseSRWLockShared(&g_syncLock);
    if (!current) { Note("mod sync: answer %s to offer %u dropped: it is not the current offer", word, version); return 0; }
    char line[128];
    int length = snprintf(line, sizeof line, "syncanswer\t%u\t%s\t%s\n", version, word, id);
    AcquireSRWLockExclusive(&g_liveOutLock);
    int queued = g_liveConnected && length > 0 && g_liveOutUsed + (size_t)length <= sizeof g_liveOut;
    if (queued) { memcpy(g_liveOut + g_liveOutUsed, line, (size_t)length); g_liveOutUsed += (size_t)length; }
    ReleaseSRWLockExclusive(&g_liveOutLock);
    if (queued && g_liveOutEvent != NULL) SetEvent(g_liveOutEvent);
    Note("mod sync %u: the player answered %s%s%s%s", version, word, answer == HF_SYNC_BLOCK ? " " : "", answer == HF_SYNC_BLOCK ? id : "",
        queued ? "" : " (not sent: no backend connected)");
    return queued;
}

/* The Local Stats mod when the session loads it, else a stand-in under its id with a log of its own. */
static HfMod* LiveMod(void)
{
    if (g_liveMod != NULL) return g_liveMod;
    for (int i = 0; i < g_modCount; i++) if (!strcmp(g_mods[i].id, LIVE_MOD_ID)) g_liveMod = &g_mods[i];
    if (g_liveMod == NULL)
    {
        g_liveModStorage.api = &g_api;
        snprintf(g_liveModStorage.id, sizeof g_liveModStorage.id, "%s", LIVE_MOD_ID);
        g_liveModStorage.log = OpenLog(L"mod-" LIVE_MOD_ID L".log");
        g_liveModStorage.loaded = 1;
        g_liveMod = &g_liveModStorage;
    }
    return g_liveMod;
}

static const char* AppliesName(int applies)
{
    return applies == HF_APPLIES_NOW ? "now" : applies == HF_APPLIES_RESPAWN ? "respawn" : applies == HF_APPLIES_MISSION ? "mission" : "?";
}

/* A stratagem's type from its stable id (the stats schema names stratagem rows by it). */
static uint64_t StratagemTypeOf(uint32_t stable)
{
    for (uint32_t type = 1; type < STR_VANILLA + STR_MAX_NEW; type++)
    {
        int ready = 1;
        uintptr_t row = g_tableRow("StratagemDefinition", type, &ready);
        uint32_t id = 0;
        if (row != 0 && !SafeRead(row + 4, &id, 4) && id == stable) return type;
    }
    return 0;
}

/* Entity deltas (an attachment's stats, a default magazine's) and status multipliers are bytes of the tables the boot
 * stats pass edits (stats-tables.c StatsExtraRow), not mapped component fields: a live request writes them in the same
 * places, so a co-op joiner gets the host's attachment values too. The game reads an attachment's delta when it builds
 * a weapon, so a change is expected from the next respawn (not proven: HD2Runtime never tested a rebuilt weapon). Each
 * address keeps what it held before its first live write (reset) and its base: what another mod wrote there, else what
 * the boot pass found before the Local Stats mod's edit, else what it held before. Live values stack as live edits do
 * (fields.c): the editors' over a Lua mod's over a code mod's, one value per rank (a later mod of the same rank replaces
 * the earlier's); the address holds the highest, and taking one away shows the one under it. */
#define LIVE_EXTRA_MAX 2048
typedef struct LiveExtraLayer { int held; uint32_t bits; HfMod* mod; } LiveExtraLayer;
typedef struct LiveExtra { uintptr_t address; uint32_t before; LiveExtraLayer layer[RANK_EDITOR + 1]; } LiveExtra;
static LiveExtra g_liveExtra[LIVE_EXTRA_MAX];
static int g_liveExtraCount;

static int LiveExtraKind(const char* kind) { return !strcmp(kind, "EntityDelta") || !strcmp(kind, STATUS_MULTIPLIER); }

/* What a live write found at the address before its first one (get_stat's base where no boot edit is there); 0 none.
 * Called with g_statsLock held. */
static int LiveExtraBefore(uintptr_t address, uint32_t* before)
{
    for (int i = 0; i < g_liveExtraCount; i++) if (g_liveExtra[i].address == address) { *before = g_liveExtra[i].before; return 1; }
    return 0;
}

/* The Lua or code mod whose live value is at the address under the editors' (get_stat's holder), and that value; NULL
 * none. Called with g_statsLock held. */
static const char* LiveExtraHolder(uintptr_t address, uint32_t* value)
{
    for (int i = 0; i < g_liveExtraCount; i++)
    {
        if (g_liveExtra[i].address != address) continue;
        for (int rank = RANK_LUA; rank >= RANK_CODE; rank--)
        {
            const LiveExtraLayer* l = &g_liveExtra[i].layer[rank];
            if (l->held && l->mod != NULL) { *value = l->bits; return l->mod->id; }
        }
    }
    return NULL;
}

/* verb "set" (bits), "reset" (the rank's value taken away: the one under it shows, else what the address held before
 * this session's first live write), "base" (the editors' value taken away, and without the Local Stats mod's edits) or
 * "get" (*now only), by a mod of `rank` (mod: the Lua or code mod; NULL for the editors). HF_APPLIES_RESPAWN, or 0 with
 * *why. */
static int LiveExtraWrite(const char* verb, int rank, HfMod* mod, const char* kind, const char* component, uint64_t resource,
    int64_t record, uint32_t offset, uint32_t bytes, int floating, uint32_t bits, uint32_t* now, const char** why)
{
    StatEdit e;
    memset(&e, 0, sizeof e);
    snprintf(e.kind, sizeof e.kind, "%s", kind);
    snprintf(e.component, sizeof e.component, "%s", component);
    e.resource = resource;
    e.record = record;
    e.offset = offset;
    *now = 0;
    int table = StatsExtraKind(e.kind);
    if (table < 0 || !LiveExtraKind(kind)) { *why = "not an entity delta or status multiplier"; return 0; }
    if (!g_statsExtraDone[table]) { *why = "its table is not loaded yet"; return 0; }
    AcquireSRWLockExclusive(&g_statsLock);
    uintptr_t at = !strcmp(g_build, "25480438") ? StatsExtraRow(&e) : 0;
    if (!at || SafeRead(at, now, bytes))
    {
        ReleaseSRWLockExclusive(&g_statsLock);
        *why = "the delta's table or ownership is not as reviewed";
        return 0;
    }
    if (!strcmp(verb, "get")) { ReleaseSRWLockExclusive(&g_statsLock); return HF_APPLIES_RESPAWN; }
    int slot = -1;
    for (int i = 0; i < g_liveExtraCount; i++) if (g_liveExtra[i].address == at) slot = i;
    int reset = !strcmp(verb, "reset"), set = !strcmp(verb, "set");
    if (reset && slot < 0) { ReleaseSRWLockExclusive(&g_statsLock); return HF_APPLIES_RESPAWN; }
    if (slot < 0)
    {
        if (g_liveExtraCount == LIVE_EXTRA_MAX) { ReleaseSRWLockExclusive(&g_statsLock); *why = "too many live attachment changes"; return 0; }
        slot = g_liveExtraCount++;
        memset(&g_liveExtra[slot], 0, sizeof g_liveExtra[slot]);
        g_liveExtra[slot].address = at;
        g_liveExtra[slot].before = *now;
    }
    LiveExtra* x = &g_liveExtra[slot];
    LiveExtraLayer* own = &x->layer[rank];
    if (set) { own->held = 1; own->bits = bits; own->mod = mod; }
    else if (rank == RANK_EDITOR || own->mod == mod) { own->held = 0; own->mod = NULL; }
    else { ReleaseSRWLockExclusive(&g_statsLock); return HF_APPLIES_RESPAWN; } /* another mod's value: none of this one's here */
    int top = -1, held = 0;
    for (int r = RANK_EDITOR; r >= RANK_CODE; r--) if (x->layer[r].held) { held = 1; if (top < 0) top = r; }
    uint32_t want;
    if (top >= 0) want = x->layer[top].bits;
    else if (reset) want = x->before;
    else
    {
        /* base: another mod's value, else what the Local Stats mod's boot edit replaced, else what it held before */
        int other = 0, local = 0;
        uint32_t otherValue = 0, localOriginal = 0;
        for (int i = 0; i < g_statCount; i++)
        {
            const StatEdit* s = &g_stats[i];
            if (!s->applied || s->address != at) continue;
            if (strcmp(s->mod->id, LIVE_MOD_ID)) { other = 1; otherValue = s->value; }
            else if (!local) { local = 1; localOriginal = s->original; }
        }
        want = other ? otherValue : local ? localOriginal : x->before;
    }
    if (floating) { float v; memcpy(&v, &want, 4); if (!isfinite(v)) { ReleaseSRWLockExclusive(&g_statsLock); *why = "not a finite number"; return 0; } }
    uint32_t check = 0;
    int written = !ApiWrite(at, &want, bytes) && !SafeRead(at, &check, bytes) && (bytes == 1 ? (uint8_t)check == (uint8_t)want : check == want);
    if (written && reset && !held) g_liveExtra[slot] = g_liveExtra[--g_liveExtraCount];
    ReleaseSRWLockExclusive(&g_statsLock);
    if (!written) { *why = "the write did not hold"; return 0; }
    ApiLog(mod != NULL ? mod : LiveMod(), "live %s %s %s +%u (record %lld) of %016llx: %08x -> %08x (expected from the next respawn)", verb, kind,
        component, offset, (long long)record, (unsigned long long)resource, *now, want);
    return HF_APPLIES_RESPAWN;
}

static void LiveExtraRequest(const char* verb, char** f, const char* seq, char* reply, size_t size)
{
    char* end;
    uint64_t resource = strtoull(f[4], &end, 16);
    const char* storage = f[7];
    uint32_t bytes = !strcmp(storage, "u8") ? 1 : 4;
    if (*end || (bytes == 4 && strcmp(storage, "f32") && strcmp(storage, "u32") && strcmp(storage, "i32")))
    { snprintf(reply, size, "%s	refused	malformed request", seq); return; }
    uint32_t now = 0;
    const char* why = "refused";
    int applies = LiveExtraWrite(verb, RANK_EDITOR, NULL, f[2], f[3], resource, strtoll(f[5], NULL, 10), (uint32_t)strtoul(f[6], NULL, 10), bytes,
        !strcmp(storage, "f32"), !strcmp(verb, "set") ? (uint32_t)strtoul(f[8], NULL, 16) : 0, &now, &why);
    if (!applies) snprintf(reply, size, "%s	refused	%s", seq, why);
    else if (!strcmp(verb, "get")) snprintf(reply, size, "%s	ok	respawn	%08x", seq, now);
    else snprintf(reply, size, "%s	ok	respawn", seq);
}

/* SDK 9, set_stat: an entity delta or status multiplier (what set_live cannot reach, get_stat reads) changed live in the
 * place the stats pass writes it. value NULL: back to before this session's live change, or with HF_LIVE_BASE to the
 * field's base. HF_APPLIES_RESPAWN (a weapon takes an attachment's value when it is next built), or 0 (logged why). */
static int ApiSetStat(HfMod* mod, const HfStat* stat, const HfValue* value, uint32_t flags)
{
    if (mod == NULL || stat == NULL || stat->kind == NULL || stat->component == NULL) return 0;
    uint32_t type = stat->type, bytes = type == HF_TYPE_U8 ? 1 : 4, bits = 0;
    if (value != NULL)
    {
        if (type == HF_TYPE_F32) { float v = (float)value->number; memcpy(&bits, &v, 4); }
        else if (type == HF_TYPE_I32) { int32_t v = (int32_t)value->number; memcpy(&bits, &v, 4); }
        else bits = (uint32_t)value->number;
    }
    uint32_t now = 0;
    const char* why = "refused";
    int rank = ModRank(mod);
    int applies = LiveExtraWrite(value != NULL ? "set" : (flags & HF_LIVE_BASE) && rank == RANK_EDITOR ? "base" : "reset", rank,
        rank == RANK_EDITOR ? NULL : mod, stat->kind, stat->component, stat->resource, stat->record, stat->offset, bytes, type == HF_TYPE_F32,
        bits, &now, &why);
    if (!applies) ApiLog(mod, "set_stat %s %s +%u refused: %s", stat->kind, stat->component, stat->offset, why);
    return applies;
}

/* One request; the reply goes to `reply`. Also the native fixture's entry point. */
static void LiveRequest(char* line, char* reply, size_t size)
{
    if (!strncmp(line, "syncmod\t", 8)) { SyncModLine(line, reply, size); return; }
    if (!strncmp(line, "syncstate\t", 10)) { SyncStateLine(line, reply, size); return; }
    char* f[12];
    int n = Split(line, f, 12);
    if (n < 2) { snprintf(reply, size, "-\trefused\tmalformed request"); return; }
    const char* seq = f[1];
    if (!strcmp(f[0], "session") && n >= 3)
    {
        uint32_t role = !strcmp(f[2], "joiner") ? HF_SESSION_JOINER : !strcmp(f[2], "host") ? HF_SESSION_HOST : HF_SESSION_SOLO;
        SetSession(role, n >= 4 ? f[3] : "");
        snprintf(reply, size, "%s\tok\tnow", seq);
        return;
    }
    if (!strcmp(f[0], "notice") && n >= 4)
    {
        SetNotice(f + 2, n - 2);
        snprintf(reply, size, "%s\tok\tnow", seq);
        return;
    }
    int set = !strcmp(f[0], "set"), reset = !strcmp(f[0], "reset"), get = !strcmp(f[0], "get"), base = !strcmp(f[0], "base");
    if (!(set && n == 10) && !((reset || get || base) && n == 8)) { snprintf(reply, size, "%s\trefused\tmalformed request", seq); return; }
    const char *kind = f[2], *storage = f[7];
    if (LiveExtraKind(kind)) { LiveExtraRequest(f[0], f, seq, reply, size); return; }
    const SdkComponent* c = SdkComponentNamed(!strcmp(kind, "component") ? f[3] : kind);
    uint32_t type = !strcmp(storage, "f32") ? HF_TYPE_F32 : !strcmp(storage, "u32") ? HF_TYPE_U32 : !strcmp(storage, "i32") ? HF_TYPE_I32 :
        !strcmp(storage, "u8") ? HF_TYPE_U8 : 0;
    char* end;
    uint64_t resource = strtoull(f[4], &end, 16);
    long long record = strtoll(f[5], NULL, 10);
    unsigned long offset = strtoul(f[6], NULL, 10);
    if (c == NULL || type == 0 || *end || offset > 0xFFFF || (c->table != (strcmp(kind, "component") != 0)))
    { snprintf(reply, size, "%s\trefused\tHellForge has no mappings for %s %s", seq, kind, f[3]); return; }
    uint64_t target = !c->table ? resource : !strcmp(kind, "StratagemDefinition") ? StratagemTypeOf((uint32_t)record) : (uint64_t)record;
    if (target == 0) { snprintf(reply, size, "%s\trefused\tno such %s row", seq, kind); return; }
    HfField field = {c->component, c->layout, (uint16_t)offset, (uint16_t)(type == HF_TYPE_U8 ? 1 : 4), type, 0};
    HfMod* mod = LiveMod();
    g_liveReason[0] = 0;
    g_reasonMod = mod;
    int applies = 0;
    if (get)
    {
        const SdkComponent* resolved = NULL;
        const SdkField* sf = ResolveField(mod, "live get", &field, &resolved);
        FieldEdit probe = {mod, EDIT_SET, target, 0, resolved, sf};
        probe.table = c->table;
        probe.shared = 1;
        int waiting = 0;
        uint8_t* record = sf != NULL ? EditRecord(&probe, target, 0, &waiting) : NULL;
        uint32_t bits = 0;
        g_reasonMod = NULL;
        if (record == NULL || SafeRead((uintptr_t)(record + sf->offset), &bits, sf->size == 1 ? 1 : 4))
        { snprintf(reply, size, "%s\trefused\t%s", seq, sf == NULL ? g_liveReason : waiting ? "its table is not loaded yet" : "no such record"); return; }
        snprintf(reply, size, "%s\tok\t%s\t%08x", seq, AppliesName((int)sf->applies), bits);
        return;
    }
    if (set)
    {
        uint32_t bits = (uint32_t)strtoul(f[8], NULL, 16);
        HfValue value = {type, 0.0, 0};
        if (type == HF_TYPE_F32) { float v; memcpy(&v, &bits, 4); value.number = v; }
        else if (type == HF_TYPE_I32) value.number = (int32_t)bits;
        else value.number = bits;
        applies = ApiSetLive(mod, target, &field, &value, f[9][0] == '1' ? HF_LIVE_SHARED : 0);
    }
    else applies = ApiSetLive(mod, target, &field, NULL, base ? HF_LIVE_BASE : 0);
    g_reasonMod = NULL;
    if (applies == 0) snprintf(reply, size, "%s\trefused\t%s", seq, g_liveReason[0] ? g_liveReason : "refused");
    else snprintf(reply, size, "%s\tok\t%s", seq, AppliesName(applies));
    /* Every live change, with what the record holds afterwards, so a session log shows what the game got. */
    const SdkComponent* resolved = NULL;
    const SdkField* sf = applies ? ResolveField(mod, "live", &field, &resolved) : NULL;
    uint32_t now = 0;
    int waiting = 0;
    FieldEdit probe = {mod, EDIT_SET, target, 0, resolved, sf};
    probe.table = c->table;
    probe.shared = 1;
    uint8_t* at = sf != NULL ? EditRecord(&probe, target, 0, &waiting) : NULL;
    int readable = at != NULL && !SafeRead((uintptr_t)(at + sf->offset), &now, sf->size == 1 ? 1 : 4);
    ApiLog(mod, "live %s %s +%lu (%s %s, record %lld) of %016llx: %s%s%s", set ? "set" : base ? "base" : "reset", c->name, offset, kind, f[3], record,
        (unsigned long long)target, applies ? "applies " : "refused: ", applies ? AppliesName(applies) : (g_liveReason[0] ? g_liveReason : "?"), "");
    if (readable) ApiLog(mod, "  %s at 0x%llx now holds %08x%s", sf->name, (unsigned long long)(uintptr_t)(at + sf->offset), now,
        set ? (strtoul(f[8], NULL, 16) == now ? " (as sent)" : " (NOT as sent)") : "");
}

/* One write on the overlapped pipe, waited for. */
static int LiveWrite(HANDLE pipe, OVERLAPPED* op, const char* data, size_t length)
{
    DWORD written = 0;
    ResetEvent(op->hEvent);
    if (!WriteFile(pipe, data, (DWORD)length, NULL, op) && GetLastError() != ERROR_IO_PENDING) return 0;
    return GetOverlappedResult(pipe, op, &written, TRUE) && written == length;
}

static int LiveSend(HANDLE pipe, OVERLAPPED* op, const char* text)
{
    char line[600];
    int length = snprintf(line, sizeof line, "%s\n", text);
    return length > 0 && (size_t)length < sizeof line && LiveWrite(pipe, op, line, (size_t)length);
}

/* The queued answers (sync_answer), written here: this thread is the pipe's only writer. */
static int LiveFlush(HANDLE pipe, OVERLAPPED* op)
{
    char out[sizeof g_liveOut];
    AcquireSRWLockExclusive(&g_liveOutLock);
    size_t used = g_liveOutUsed;
    memcpy(out, g_liveOut, used);
    g_liveOutUsed = 0;
    ReleaseSRWLockExclusive(&g_liveOutLock);
    return used == 0 || LiveWrite(pipe, op, out, used);
}

/* Connects to the launcher's pipe while the session runs; one connection at a time. The pipe is overlapped: a
 * synchronous handle serializes its I/O, so an answer written from another thread would wait for the next request. This
 * thread waits for either a request or a queued answer, and does all the writing. */
static DWORD WINAPI LiveWatch(void* parameter)
{
    const wchar_t* session = wcsrchr(g_logs, L'\\');
    wchar_t path[MAX_PATH];
    if (session == NULL || swprintf(path, MAX_PATH, L"\\\\.\\pipe\\hellforge-live-%ls", session + 1) < 0) return 0;
    g_liveOutEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    OVERLAPPED readOp = {0}, writeOp = {0};
    readOp.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    writeOp.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (g_liveOutEvent == NULL || readOp.hEvent == NULL || writeOp.hEvent == NULL) return 0;
    for (;;)
    {
        if (!g_fired[HF_EVENT_ENTITIES_READY]) { Sleep(500); continue; }
        HANDLE pipe = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (pipe == INVALID_HANDLE_VALUE) { Sleep(2000); continue; }
        Note("live editor connected");
        AcquireSRWLockExclusive(&g_liveOutLock);
        g_liveConnected = 1;
        g_liveOutUsed = 0;
        ReleaseSRWLockExclusive(&g_liveOutLock);
        char hello[160];
        snprintf(hello, sizeof hello, "hello\t%d\t%s\t%s", LIVE_PROTOCOL, g_build, g_statsSchema[0] ? g_statsSchema : "-");
        char buffer[8192], reply[512];
        size_t used = 0;
        int open = LiveSend(pipe, &writeOp, hello), reading = 0;
        while (open)
        {
            if (!reading)
            {
                ResetEvent(readOp.hEvent);
                if (!ReadFile(pipe, buffer + used, (DWORD)(sizeof buffer - 1 - used), NULL, &readOp) && GetLastError() != ERROR_IO_PENDING) break;
                reading = 1;
            }
            HANDLE waits[2] = {readOp.hEvent, g_liveOutEvent};
            DWORD woke = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (woke == WAIT_OBJECT_0 + 1) { open = LiveFlush(pipe, &writeOp); continue; }
            if (woke != WAIT_OBJECT_0) break;
            DWORD read = 0;
            reading = 0;
            if (!GetOverlappedResult(pipe, &readOp, &read, FALSE) || read == 0) break;
            used += read;
            char* start = buffer;
            for (char* newline; open && (newline = memchr(start, '\n', used - (size_t)(start - buffer))) != NULL; start = newline + 1)
            {
                *newline = 0;
                if (newline > start && newline[-1] == '\r') newline[-1] = 0;
                if (*start == 0) continue;
                LiveRequest(start, reply, sizeof reply);
                open = LiveSend(pipe, &writeOp, reply);
            }
            used -= (size_t)(start - buffer);
            memmove(buffer, start, used);
            if (used >= sizeof buffer - 1) used = 0; /* a line too long to be a request */
        }
        if (reading)
        {
            DWORD read;
            CancelIoEx(pipe, &readOp);
            GetOverlappedResult(pipe, &readOp, &read, TRUE);
        }
        AcquireSRWLockExclusive(&g_liveOutLock);
        g_liveConnected = 0;
        g_liveOutUsed = 0;
        ReleaseSRWLockExclusive(&g_liveOutLock);
        CloseHandle(pipe);
        Note("live editor disconnected");
        SetSession(HF_SESSION_SOLO, ""); /* the backend is gone: nothing tells this game it is joined */
        SyncClear();
    }
}

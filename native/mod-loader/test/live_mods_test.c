/* Live mod changes (live_mods.c): stats mods added, removed and replaced in a running session, each keeping its place in
 * load order (a later mod's value and Local Stats' stay on top; removing them shows what was under them); nothing written
 * under a code mod's deferred edit; a live edit's reset value following the stats under it; an edit's expect checked
 * against the field's bottom; retired slots reused; refusals (a friend's lobby, another player, busy, a code mod, own,
 * a table not loaded yet, the order) leaving everything as it was. Included by stats_test.c. */

static uint8_t* g_txnState;
static int g_txnPlayers = 1;
static uint8_t* TxnFixtureState(void) { return g_txnState; }
static int TxnFixturePlayers(void) { return g_txnPlayers; }

static void TxnSend(const char* text, char* reply, size_t size)
{
    char line[1024];
    snprintf(line, sizeof line, "%s", text);
    LiveRequest(line, reply, size);
}

/* Sends a change's lines, each but the last answered ok; the last one's reply in *reply. */
static void TxnRun(const char* const* lines, int count, char* reply, size_t size)
{
    for (int i = 0; i < count; i++)
    {
        TxnSend(lines[i], reply, size);
        if (i < count - 1 && strstr(reply, "\tok") == NULL) { printf("  live mods: %s -> %s\n", lines[i], reply); assert(0); }
    }
}

/* What the loader queued to send on its own, then forgotten. */
static void TxnSent(char* out, size_t size)
{
    AcquireSRWLockExclusive(&g_liveOutLock);
    size_t n = g_liveOutUsed < size - 1 ? g_liveOutUsed : size - 1;
    memcpy(out, g_liveOut, n);
    out[n] = 0;
    g_liveOutUsed = 0;
    ReleaseSRWLockExclusive(&g_liveOutLock);
}

static int TxnRefused(const char* reply, const char* reason)
{
    char want[64];
    snprintf(want, sizeof want, "\trefused\t%s\t", reason);
    return strstr(reply, want) != NULL;
}

static void LiveModsFixture(void)
{
    /* the session: a stats mod a, a code mod b, Local Stats on top; one component record of entity 1 */
    HfMod saved[8];
    memcpy(saved, g_mods, sizeof saved);
    int savedMods = g_modCount, savedComponents = g_componentCount, savedStats = g_statCount, savedEdits = g_editCount;
    int savedTable = g_statsTablesDone[1];
    Component savedComponent = g_components[0];
    memset(g_mods, 0, sizeof saved);
    const char* ids[] = {"a", "b", "zz.local.stats"};
    for (int i = 0; i < 3; i++)
    {
        strcpy(g_mods[i].id, ids[i]);
        g_mods[i].loaded = 1;
        g_mods[i].log = INVALID_HANDLE_VALUE;
        g_mods[i].order = i;
        wcscpy(g_mods[i].dll, i == 1 ? L"b.dll" : L"-");
    }
    for (int i = 3; i < 8; i++) g_mods[i].order = i;
    g_modCount = 3;
    Component* c = &g_components[0];
    g_componentCount = 1;
    memset(c, 0, sizeof *c);
    strcpy(c->name, "Fixture"); c->index = 999; c->indices = 4; c->records = 1; c->recordOffset = 64; c->stride = 16;
    uint32_t extent = 80;
    memcpy(c->header + 16, &extent, 4);
    g_txnState = calloc(1, REG_TABLES + 8 * 1000);
    uint8_t* block = calloc(1, 112);
    uint8_t* table = block + 32;
    memcpy(block, &c->index, 4);
    memcpy(block + 4, c->header, 28);
    *(uint64_t*)(table + 16) = 1; /* entity 1 owns record 0 */
    assert(!SetStatePointer(g_txnState, REG_TABLES + 8 * c->index, table));
    uint32_t* record = (uint32_t*)(table + 64);
    record[0] = 10; record[1] = 20; record[2] = 30;
    /* the boot pass: a sets +0 to 30, Local Stats +4 to 77 */
    g_stats[0] = Edit(0, 30, 0, 0); g_stats[0].hasExpect = 1; g_stats[0].expect = 10;
    g_stats[1] = Edit(2, 77, 4, 0);
    g_statCount = 2;
    g_editCount = 0;
    FixtureLoad(g_txnState);
    assert(record[0] == 30 && record[1] == 77 && g_statsComponentsReady);
    g_liveEntityState = TxnFixtureState;
    g_livePlayers = TxnFixturePlayers;
    AcquireSRWLockExclusive(&g_liveOutLock);
    g_liveConnected = 1;
    g_liveOutUsed = 0;
    ReleaseSRWLockExclusive(&g_liveOutLock);
    char reply[512], sent[2048];

    /* Added between a and b: over a's value, under Local Stats'; a line whose expect is not the field's bottom refused. */
    const char* add[] = {"modbegin\t1\tt1\tsolo\tdigest1", "modorder\t2\tt1\t0\ta", "modorder\t3\tt1\t1\tc", "modorder\t4\tt1\t2\tb",
        "modorder\t5\tt1\t3\tzz.local.stats", "modadd\t6\tt1\tc\tstats\t0123456789abcdef\tC:\\mods\\c\t-",
        "modstat\t7\tt1\tc\tcomponent\tFixture\t0000000000000001\t0\t0\tu32\t00000032\t0000000A\t0\t1\t0\tc +0",
        "modstat\t8\tt1\tc\tcomponent\tFixture\t0000000000000001\t0\t4\tu32\t0000003C\t-\t0\t1\t0\tc +4",
        "modstat\t9\tt1\tc\tcomponent\tFixture\t0000000000000001\t0\t8\tu32\t00000063\t000003E7\t0\t1\t0\tc +8 expects 999",
        "modcommit\t10\tt1"};
    TxnRun(add, 10, reply, sizeof reply);
    TxnSent(sent, sizeof sent);
    assert(!strcmp(reply, "10\tok\tqueued") && record[0] == 50 && record[1] == 77 && record[2] == 30);
    assert(strstr(sent, "modstate\tt1\tc\tadded\t2\t1\n") && strstr(sent, "modresult\tt1\tok\n") && g_modCount == 4 && g_mods[3].order == 1);

    /* Local Stats removed: c's value under it shows. */
    const char* removeLocal[] = {"modbegin\t11\tt2\tsolo\tdigest2", "modorder\t12\tt2\t0\ta", "modorder\t13\tt2\t1\tc",
        "modorder\t14\tt2\t2\tb", "modremove\t15\tt2\tzz.local.stats", "modcommit\t16\tt2"};
    TxnRun(removeLocal, 6, reply, sizeof reply);
    assert(!strcmp(reply, "16\tok\tqueued") && record[1] == 60 && !g_mods[2].loaded);

    /* c replaced by a version that sets +0 only: +0 takes the new value, +4 goes back to the game's own. */
    const char* replace[] = {"modbegin\t17\tt3\tsolo\tdigest3", "modorder\t18\tt3\t0\ta", "modorder\t19\tt3\t1\tc", "modorder\t20\tt3\t2\tb",
        "modreplace\t21\tt3\tc\tstats\tfedcba9876543210\tC:\\mods\\c\t-",
        "modstat\t22\tt3\tc\tcomponent\tFixture\t0000000000000001\t0\t0\tu32\t00000037\t-\t0\t1\t0\tc +0",
        "modcommit\t23\tt3"};
    TxnRun(replace, 7, reply, sizeof reply);
    TxnSent(sent, sizeof sent);
    assert(!strcmp(reply, "23\tok\tqueued") && record[0] == 55 && record[1] == 20 && strstr(sent, "modstate\tt3\tc\treplaced\t1\t0\n"));

    /* Under a code mod's deferred edit nothing is written; the change reuses a retired slot. */
    static const SdkField field = {"fixture", 0, 4, HF_TYPE_U32, 0, 0};
    assert(EditRoom());
    FieldEdit deferred = {&g_mods[1], EDIT_SET, 1, 0, NULL, &field};
    deferred.address = (uintptr_t)&record[0];
    g_edits[g_editCount++] = deferred;
    record[0] = 1234;
    int stats = g_statCount;
    const char* under[] = {"modbegin\t24\tt4\tsolo\tdigest4", "modorder\t25\tt4\t0\ta", "modorder\t26\tt4\t1\tc", "modorder\t27\tt4\t2\td",
        "modorder\t28\tt4\t3\tb", "modadd\t29\tt4\td\tstats\t1111111111111111\tC:\\mods\\d\t-",
        "modstat\t30\tt4\td\tcomponent\tFixture\t0000000000000001\t0\t0\tu32\t00000063\t-\t0\t1\t0\td +0", "modcommit\t31\tt4"};
    TxnRun(under, 8, reply, sizeof reply);
    assert(!strcmp(reply, "31\tok\tqueued") && record[0] == 1234 && g_statCount == stats);

    /* Under a live edit: its reset value follows the stats (d removed: c's 55), the field keeps the live value. */
    FieldEdit* live = &g_edits[0];
    live->live = 1;
    live->hasOriginal = 1;
    uint32_t top = 99, liveValue = 500;
    memcpy(live->original, &top, 4);
    memcpy(live->value, &liveValue, 4);
    record[0] = 500;
    const char* removeD[] = {"modbegin\t32\tt5\tsolo\tdigest5", "modorder\t33\tt5\t0\ta", "modorder\t34\tt5\t1\tc", "modorder\t35\tt5\t2\tb",
        "modremove\t36\tt5\td", "modcommit\t37\tt5"};
    TxnRun(removeD, 6, reply, sizeof reply);
    uint32_t restored;
    memcpy(&restored, live->original, 4);
    assert(!strcmp(reply, "37\tok\tqueued") && record[0] == 500 && restored == 55);
    g_editCount = 0;
    record[0] = 55;
    TxnSent(sent, sizeof sent);

    /* Refusals: nothing changes. */
    int mods = g_modCount;
    SetSession(HF_SESSION_JOINER, "Host");
    TxnSend("modbegin\t40\tr1\tsolo\tx", reply, sizeof reply);
    assert(TxnRefused(reply, "coop"));
    SetSession(HF_SESSION_SOLO, "");
    g_txnPlayers = 2;
    TxnSend("modbegin\t41\tr2\tsolo\tx", reply, sizeof reply);
    assert(TxnRefused(reply, "coop"));
    g_txnPlayers = 1;
    TxnSend("modbegin\t42\tr3\tsolo\tx", reply, sizeof reply);
    assert(!strcmp(reply, "42\tok\tnow"));
    TxnSend("modbegin\t43\tr4\tsolo\tx", reply, sizeof reply);
    assert(TxnRefused(reply, "busy"));
    TxnSend("modadd\t44\tr3\tx\tcode\t2222222222222222\tC:\\mods\\x\tC:\\mods\\x\\x.dll", reply, sizeof reply);
    assert(TxnRefused(reply, "capability"));
    TxnSend("modstat\t45\tr3\tnobody\tcomponent\tFixture\t0000000000000001\t0\t0\tu32\t00000001\t-\t0\t1\t0\tx", reply, sizeof reply);
    assert(TxnRefused(reply, "malformed")); /* (the change ended at the refusal before) */

    const char* removeCode[] = {"modbegin\t46\tr5\tsolo\tx", "modorder\t47\tr5\t0\ta", "modorder\t48\tr5\t1\tc", "modremove\t49\tr5\tb",
        "modcommit\t50\tr5"};
    TxnRun(removeCode, 5, reply, sizeof reply);
    assert(TxnRefused(reply, "capability") && g_mods[1].loaded);
    const char* own[] = {"modbegin\t51\tr6\tsolo\tx", "modorder\t52\tr6\t0\ta", "modorder\t53\tr6\t1\tc", "modorder\t54\tr6\t2\tb",
        "modorder\t55\tr6\t3\te", "modadd\t56\tr6\te\tstats\t3333333333333333\tC:\\mods\\e\t-",
        "modstat\t57\tr6\te\tcomponent\tFixture\t0000000000000001\t0\t0\tu32\t00000001\t-\t1\t0\t0\te own", "modcommit\t58\tr6"};
    TxnRun(own, 8, reply, sizeof reply);
    assert(TxnRefused(reply, "capability"));
    g_statsTablesDone[1] = 0;
    const char* later[] = {"modbegin\t59\tr7\tsolo\tx", "modorder\t60\tr7\t0\ta", "modorder\t61\tr7\t1\tc", "modorder\t62\tr7\t2\tb",
        "modorder\t63\tr7\t3\tf", "modadd\t64\tr7\tf\tstats\t4444444444444444\tC:\\mods\\f\t-",
        "modstat\t65\tr7\tf\tDamageInfo\t-\t0000000000000000\t54\t4\tu32\t00000096\t-\t0\t0\t0\tf damage", "modcommit\t66\tr7"};
    TxnRun(later, 8, reply, sizeof reply);
    assert(TxnRefused(reply, "state"));
    g_statsTablesDone[1] = savedTable;
    const char* leftOut[] = {"modbegin\t67\tr8\tsolo\tx", "modorder\t68\tr8\t0\ta", "modorder\t69\tr8\t1\tg",
        "modadd\t70\tr8\tg\tstats\t5555555555555555\tC:\\mods\\g\t-", "modcommit\t71\tr8"};
    TxnRun(leftOut, 5, reply, sizeof reply);
    assert(TxnRefused(reply, "malformed") && strstr(reply, "leaves out"));
    const char* reordered[] = {"modbegin\t72\tr9\tsolo\tx", "modorder\t73\tr9\t0\tc", "modorder\t74\tr9\t1\ta", "modorder\t75\tr9\t2\tb",
        "modcommit\t76\tr9"};
    TxnRun(reordered, 5, reply, sizeof reply);
    assert(TxnRefused(reply, "capability") && strstr(reply, "order"));
    TxnSent(sent, sizeof sent);
    assert(g_modCount == mods && record[0] == 55 && record[1] == 20 && record[2] == 30 && sent[0] == 0);
    /* a change the backend withdraws, and one dropped when the backend goes */
    TxnSend("modbegin\t77\tr10\tsolo\tx", reply, sizeof reply);
    TxnSend("modabort\t78\tr10", reply, sizeof reply);
    assert(!strcmp(reply, "78\tok\tnow") && !g_txn.open);
    TxnSend("modbegin\t79\tr11\tsolo\tx", reply, sizeof reply);
    LiveModsAbort("the backend disconnected");
    assert(!g_txn.open);
    printf("  live mods: added over a and under Local Stats, Local Stats removed, replaced, kept under a code mod's edit and a live edit, 9 refusals\n");

    AcquireSRWLockExclusive(&g_liveOutLock);
    g_liveConnected = 0;
    g_liveOutUsed = 0;
    ReleaseSRWLockExclusive(&g_liveOutLock);
    g_liveEntityState = EntityState;
    g_livePlayers = LivePlayers;
    free(g_txnState);
    free(block);
    memcpy(g_mods, saved, sizeof saved);
    g_modCount = savedMods;
    g_components[0] = savedComponent;
    g_componentCount = savedComponents;
    g_statCount = savedStats;
    g_editCount = savedEdits;
    InterlockedExchange(&g_statsComponentsReady, 0);
}

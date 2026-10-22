/* Read-only, per-load observations for the launcher. Values stay in the local session directory.
 * A field a stats file changed reports its value from before the stats pass: the editor's game value. */
static uintptr_t StatsObservedComponent(StatEdit* e)
{
    const Component* c = NULL;
    for (int i = 0; i < g_componentCount; i++) if (!strcmp(g_components[i].name, e->component)) c = &g_components[i];
    if (!c || c->stride < 4 || e->offset > c->stride - StatSize(e) || c->recordOffset != c->indices * 16u || !c->indices) return 0;
    uint8_t* table = StatePointer(EntityState(), REG_TABLES + 8 * c->index);
    uint8_t head[32];
    if (!table || SafeRead((uintptr_t)table - 32, head, 32) || memcmp(head, &c->index, 4) || memcmp(head + 4, c->header, 28)) return 0;
    uint32_t at = (uint32_t)(e->resource % c->indices);
    for (uint32_t i = 0; i < c->indices; i++)
    {
        uint8_t row[16]; uint64_t resource; uint32_t index, flags;
        if (SafeRead((uintptr_t)table + at * 16u, row, sizeof row)) return 0;
        memcpy(&resource, row, 8); memcpy(&index, row + 8, 4); memcpy(&flags, row + 12, 4);
        if (!resource) return 0;
        if (resource == e->resource)
        {
            if (flags) return 0;
            int valid = index < c->records && index == e->record;
            for (int j = 0; j < g_ownedCount; j++)
                if (g_owned[j].component == c->index && g_owned[j].index == table && g_owned[j].stride == c->stride &&
                    index >= g_owned[j].first && index < g_owned[j].next) valid = 1;
            return valid ? (uintptr_t)table + c->recordOffset + (uintptr_t)index * c->stride + e->offset : 0;
        }
        at = (at + 1) % c->indices;
    }
    return 0;
}

static uintptr_t StatsObservedTable(StatEdit* e, uintptr_t game)
{
    if (StatsExtraKind(e->kind) >= 0) return StatsExtraRow(e);
    int phase = !strcmp(e->kind, "ProjectileSettings") ? 0 : !strcmp(e->kind, "DamageInfo") ? 1 : 2;
    uintptr_t table = phase == 0 ? ProjectileTable(game) : phase == 1 ? (g_damageTable ? (uintptr_t)g_damageTable : game + DMG_TABLE) :
        (g_stratagemTable ? (uintptr_t)g_stratagemTable : game + STR_TABLE);
    uint32_t count = phase == 0 ? PRJ_VANILLA : phase == 1 ? DMG_VANILLA : STR_VANILLA;
    uint32_t stride = phase == 0 ? PRJ_ROW : phase == 1 ? DMG_ROW : STR_ROW;
    if (!game || e->offset < 4 || e->offset > stride - 4) return 0;
    uintptr_t found = 0; int matches = 0;
    uint32_t first = phase == 2 ? 1 : (uint32_t)e->record;
    uint32_t last = phase == 2 ? count : first + 1;
    if (first == 0 || first >= count) return 0;
    for (uint32_t slot = first; slot < last; slot++)
    {
        uintptr_t row; uint32_t type, id;
        if (SafeRead(table + slot * 8u, &row, 8) || !row || SafeRead(row, &type, 4) || type != slot) continue;
        if (phase == 2) { if (SafeRead(row + 4, &id, 4)) continue; } else id = type;
        if (id == e->record) { matches++; found = row; }
    }
    return matches == 1 ? found + e->offset : 0;
}

static void StatsSaveSnapshot(void)
{
    wchar_t temporary[MAX_PATH], path[MAX_PATH];
    if (!g_logs[0] || !g_statsSchema[0] || swprintf(path, MAX_PATH, L"%ls\\stats-values.tsv", g_logs) < 0 ||
        swprintf(temporary, MAX_PATH, L"%ls\\stats-values.tsv.partial", g_logs) < 0) return;
    FILE* file = _wfopen(temporary, L"wb");
    if (!file) return;
    fprintf(file, "stats_snapshot\t2\t%s\t%s\n", g_build, g_statsSchema);
    for (int i = 0; i < g_statsProbeCount; i++)
    {
        const StatEdit* e = &g_statsProbes[i];
        fprintf(file, "%s\t%s\t%016llX\t%lld\t%u\t%s\t%s\t%08X\n", e->kind, e->component,
            (unsigned long long)e->resource, (long long)e->record, e->offset, e->floating ? "f32" : e->signedValue ? "i32" : e->size == 1 ? "u8" : "u32",
            e->applied ? "ok" : "unavailable", e->value);
    }
    int failed = ferror(file); if (fclose(file)) failed = 1;
    if (!failed) MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

static void StatsSnapshotReady(uintptr_t game)
{
    static unsigned componentGeneration = UINT32_MAX;
    static int phases[3], extra[STATS_EXTRA_COUNT], boosters, perks, melee;
    if (!g_statsProbeCount || strcmp(g_build, "25480438")) return;
    int components = g_statsComponentsReady && componentGeneration != g_statsGeneration;
    int changed = components;
    for (int i = 0; i < 3; i++) if (phases[i] != g_statsTablesDone[i]) changed = 1;
    for (int i = 0; i < (int)STATS_EXTRA_COUNT; i++) if (extra[i] != g_statsExtraDone[i]) changed = 1;
    if (boosters != g_boostersDone || perks != g_perksDone || melee != g_meleeDone) changed = 1;
    if (!changed) return;
    AcquireSRWLockExclusive(&g_statsLock);
    for (int i = 0; i < g_statsProbeCount; i++)
    {
        StatEdit* e = &g_statsProbes[i]; uintptr_t address = 0;
        if (!strcmp(e->kind, "component"))
        {
            if (!components) continue;
            address = StatsObservedComponent(e);
        }
        else if (!strncmp(e->kind, "Passive", 7))
        {
            if (perks == g_perksDone) continue;
            int ready;
            uintptr_t at = PerkRow(e->kind, (uint64_t)e->record, &ready);
            address = at && e->offset >= 4 && e->offset <= 12 ? at + e->offset : 0;
        }
        else if (!strcmp(e->kind, "MeleeHitVolume"))
        {
            if (melee == g_meleeDone) continue;
            uintptr_t row = MeleeVolume((uint64_t)e->record);
            address = row && e->offset >= 4 && e->offset <= MELEE_VOLUME_SIZE - 4 ? row + e->offset : 0;
        }
        else if (!strcmp(e->kind, "BoosterSettings"))
        {
            if (boosters == g_boostersDone) continue;
            uintptr_t row = BoosterRow((uint64_t)e->record);
            address = row && e->offset >= 4 && e->offset <= BOOSTER_ROW - 4 ? row + e->offset : 0;
        }
        else
        {
            int kind = StatsExtraKind(e->kind);
            if (kind >= 0) { if (extra[kind] == g_statsExtraDone[kind]) continue; }
            else
            {
                int phase = !strcmp(e->kind, "ProjectileSettings") ? 0 : !strcmp(e->kind, "DamageInfo") ? 1 : 2;
                if (phases[phase] == g_statsTablesDone[phase]) continue;
            }
            address = StatsObservedTable(e, game);
        }
        e->value = 0;
        e->applied = address && !SafeRead(address, &e->value, StatSize(e));
        e->address = e->applied ? address : 0; /* get_stat reads it again */
        for (int j = 0; e->applied && j < g_statCount; j++)
            if (g_stats[j].applied && g_stats[j].address == address) { e->value = g_stats[j].original; break; }
        if (e->applied && e->floating) { float value; memcpy(&value, &e->value, 4); if (!isfinite(value)) e->applied = 0; }
    }
    if (components) componentGeneration = g_statsGeneration;
    memcpy(phases, g_statsTablesDone, sizeof phases); memcpy(extra, g_statsExtraDone, sizeof extra); boosters = g_boostersDone; perks = g_perksDone; melee = g_meleeDone;
    ReleaseSRWLockExclusive(&g_statsLock);
    StatsSaveSnapshot();
}

static int StatsParseProbe(char** f, int n)
{
    if (n != 7) return 1;
    StatEdit e = {0}; char* end;
    if (strlen(f[1]) >= sizeof e.kind || strlen(f[2]) >= sizeof e.component ||
        (strcmp(f[1], "component") && !StatsTableKind(f[1]) && StatsExtraKind(f[1]) < 0)) return 1;
    snprintf(e.kind, sizeof e.kind, "%s", f[1]); snprintf(e.component, sizeof e.component, "%s", f[2]);
    e.resource = strtoull(f[3], &end, 16); if (*end) return 1;
    e.record = strtoll(f[4], &end, 10); if (*end || e.record < 0 || e.record > UINT32_MAX) return 1;
    uint64_t offset = strtoull(f[5], &end, 10); if (*end || offset > UINT32_MAX) return 1; e.offset = (uint32_t)offset;
    if (strcmp(f[6], "f32") && strcmp(f[6], "i32") && strcmp(f[6], "u32") && (strcmp(f[6], "u8") || strcmp(e.kind, "component"))) return 1;
    e.floating = !strcmp(f[6], "f32"); e.signedValue = !strcmp(f[6], "i32"); e.size = !strcmp(f[6], "u8") ? 1 : 4;
    StatEdit* slot = StatsProbeSlot();
    if (slot == NULL) return 1;
    *slot = e;
    return 0;
}

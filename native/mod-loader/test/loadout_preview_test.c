/* The crashing native path: ordinary query -> alternate query -> the same alternate query forever. */
static int g_previewTestCalls, g_previewTestCycle;
static void* PreviewTestOriginal(void* out, uint8_t alternate, uint32_t slot, uint32_t mode, void* context)
{
    assert(++g_previewTestCalls < 10);
    if (g_previewTestCycle)
        return LoadoutPreviewDetour(out, 1, slot, mode, context);
    memset(out, 0xA5, LOADOUT_PREVIEW_BYTES);
    return out;
}

static DWORD WINAPI PreviewOtherThread(void* parameter)
{
    uint8_t out[LOADOUT_PREVIEW_BYTES];
    assert(g_loadoutPreviewCall == NULL);
    LoadoutPreviewDetour(out, 1, 6, 0, NULL);
    assert(out[0] == 0xA5 && g_loadoutPreviewCall == NULL);
    return 0;
}

static void LoadoutPreviewFixture(void)
{
    g_loadoutPreviewOriginal = PreviewTestOriginal;
    uint8_t record[LOADOUT_PREVIEW_BYTES + 8]; memset(record, 0xCC, sizeof record);
    g_previewTestCycle = 1;
    assert(LoadoutPreviewDetour(record, 0, 6, 0, NULL) == record);
    assert(g_previewTestCalls == 2 && g_loadoutPreviewCycles == 1 && g_loadoutPreviewCall == NULL);
    for (size_t i = 0; i < sizeof record; i++) assert(record[i] == (i < LOADOUT_PREVIEW_BYTES ? 0 : 0xCC));
    g_previewTestCycle = 0; g_previewTestCalls = 0;
    assert(LoadoutPreviewDetour(record, 0, 6, 0, NULL) == record && g_previewTestCalls == 1);
    for (size_t i = 0; i < LOADOUT_PREVIEW_BYTES; i++) assert(record[i] == 0xA5);
    /* A legitimate nested query for another slot, mode, variant or context is not a cycle. */
    LoadoutPreviewCall active = {NULL, NULL, 6, 0, 1};
    g_loadoutPreviewCall = &active;
    g_previewTestCalls = 0;
    LoadoutPreviewDetour(record, 1, 5, 0, NULL);
    LoadoutPreviewDetour(record, 1, 6, 1, NULL);
    LoadoutPreviewDetour(record, 0, 6, 0, NULL);
    LoadoutPreviewDetour(record, 1, 6, 0, record);
    assert(g_previewTestCalls == 4 && g_loadoutPreviewCall == &active);
    LoadoutPreviewDetour(record, 1, 6, 0, NULL);
    assert(g_previewTestCalls == 4 && g_loadoutPreviewCycles == 2);
    HANDLE thread = CreateThread(NULL, 0, PreviewOtherThread, NULL, 0, NULL);
    assert(thread && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    assert(g_previewTestCalls == 5 && g_loadoutPreviewCall == &active && g_loadoutPreviewCycles == 2);
    g_loadoutPreviewCall = NULL;
    puts("PASS loadout preview: repeated alternate fallback terminates, native empty result, output bounds, ordinary/distinct nested queries preserved, thread isolation");
}

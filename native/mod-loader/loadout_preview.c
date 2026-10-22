/* Build 25480438: the loadout unit-preview query at 0x18E9D50 returns a 0x4C-byte record through its first argument.
 * Its missing-child fallback at 0x18EA753 calls itself with alternate=1, the SAME slot, mode and context, even if
 * alternate is already 1. G-8 Immolation in the hellpod (2026-10-18 dump, slot 6/mode 0) repeats this until the stack
 * overflows, before the stratagem picker builds its candidates. Break only a cycle in that query's current thread.
 * A missing loadout row already returns the zero record initialized at 0x18E9D84..0x18E9DAF; use that same result.
 * Included after packages.c for PinMatches. No entity, saved loadout or stratagem category is changed. */
#define LOADOUT_PREVIEW_QUERY 0x18E9D50u
#define LOADOUT_PREVIEW_BYTES 0x4Cu
typedef void* (*LoadoutPreviewFn)(void* out, uint8_t alternate, uint32_t slot, uint32_t mode, void* context);
typedef struct LoadoutPreviewCall
{
    struct LoadoutPreviewCall* previous;
    void* context;
    uint32_t slot, mode;
    uint8_t alternate;
} LoadoutPreviewCall;
static _Thread_local LoadoutPreviewCall* g_loadoutPreviewCall;
static LoadoutPreviewFn g_loadoutPreviewOriginal;
static volatile LONG g_loadoutPreviewCycles;
static int g_loadoutPreviewState;
static ULONGLONG g_loadoutPreviewFirstTry;
static const uint8_t kLoadoutPreviewEntry[] = {
    0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8d,0xac,0x24,0x60,0xeb,0xff,0xff,0xb8,0xa0,0x15,0,0,
    0xe8,0x57,0xa8,0x81,0,0x48,0x2b,0xe0};
static const uint8_t kLoadoutPreviewEmpty[] = {
    0x0f,0x57,0xc0,0x0f,0x11,0x01,0x48,0x89,0x44,0x24,0x78,0x33,0xc0,0x0f,0x11,0x41,0x10,0x48,0x8b,0xf1,
    0x48,0x89,0x4c,0x24,0x48,0x0f,0x11,0x41,0x20,0x41,0x8b,0xd8,0x41,0x8b,0xf9,0x0f,0x11,0x41,0x30,
    0x48,0x89,0x41,0x40,0x89,0x41,0x48};
static const uint8_t kLoadoutPreviewFallback[] = {
    0x48,0x8b,0x44,0x24,0x78,0x48,0x8d,0x4d,0x90,0x44,0x8b,0x44,0x24,0x60,0x45,0x8b,0xcb,0xb2,0x01,
    0x48,0x89,0x44,0x24,0x20,0xe8,0xf8,0xf5,0xff,0xff};

static void* LoadoutPreviewDetour(void* out, uint8_t alternate, uint32_t slot, uint32_t mode, void* context)
{
    for (LoadoutPreviewCall* call = g_loadoutPreviewCall; call; call = call->previous)
        if (call->alternate == alternate && call->slot == slot && call->mode == mode && call->context == context)
        {
            memset(out, 0, LOADOUT_PREVIEW_BYTES);
            if (InterlockedIncrement(&g_loadoutPreviewCycles) <= 4)
                Note("loadout preview: stopped repeated fallback for slot %u, mode %u, alternate %u (empty preview)", slot, mode, alternate);
            return out;
        }
    LoadoutPreviewCall call = {g_loadoutPreviewCall, context, slot, mode, alternate};
    g_loadoutPreviewCall = &call;
    void* result = g_loadoutPreviewOriginal(out, alternate, slot, mode, context);
    g_loadoutPreviewCall = call.previous;
    return result;
}

static int LoadoutPreviewPinsMatch(uintptr_t game)
{
    return PinMatches(game + LOADOUT_PREVIEW_QUERY, kLoadoutPreviewEntry, sizeof kLoadoutPreviewEntry) &&
        PinMatches(game + 0x18E9D84u, kLoadoutPreviewEmpty, sizeof kLoadoutPreviewEmpty) &&
        PinMatches(game + 0x18EA73Bu, kLoadoutPreviewFallback, sizeof kLoadoutPreviewFallback);
}

static void InstallLoadoutPreview(void)
{
    if (g_loadoutPreviewState) return;
    uintptr_t game = ApiModuleBase("game.dll", NULL);
    ULONGLONG now = GetTickCount64();
    if (!g_loadoutPreviewFirstTry) g_loadoutPreviewFirstTry = now;
    if (strcmp(g_build, "25480438")) { g_loadoutPreviewState = -1; return; }
    if (!game || !LoadoutPreviewPinsMatch(game))
    {
        if (now - g_loadoutPreviewFirstTry < 60000) return;
        g_loadoutPreviewState = -1;
        Note("loadout preview: cycle guard not installed; query does not match the reviewed build");
        return;
    }
    g_loadoutPreviewState = LoaderHook("loadout preview guard", game + LOADOUT_PREVIEW_QUERY, (void*)LoadoutPreviewDetour,
        (void**)&g_loadoutPreviewOriginal) ? -1 : 1;
    Note(g_loadoutPreviewState == 1 ? "loadout preview: recursive fallback guarded" : "loadout preview: could not hook recursive fallback");
}

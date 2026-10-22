/* Managed hooks (SDK 13; docs/modding/hooks.md). Every hook the loader puts in the game's process, a mod's
 * (hook, hook_ex) or HellForge's own (event sources, the registry, the game thread...), goes into one chain per hooked
 * function, so any number of them coexist and none silently replaces another. Included by hellforge_modloader.c.
 *
 * A function is patched once: MinHook (or FarHook, where MinHook finds no room) sends it to a head stub, a jump through a
 * pointer to the first link's detour. Each link's original is the next link's detour; the last link's is a tail stub, a
 * jump through the pointer to the trampoline that runs the function's own code. Adding a link only changes pointers,
 * one 8-byte write each, the new link's own original first, so a call in flight always finds a whole chain. Detours of
 * any signature chain: the stubs only jump.
 *
 * The order, first to run to last (each calls the next through its original):
 *   1. mods, highest priority first (hook is priority 0; hook_ex takes one); equal priorities by load order, the mod
 *      higher on the Mods page (loaded later) first, as it wins other conflicts. A mod's own later hook on the same
 *      function goes before its earlier one;
 *   2. HellForge's own hooks last, nearest the game's code (the newest first): what a mod's detour passes on is what
 *      HellForge's events see.
 * Hooks stay for the session. A mod must call the original through the variable it passed: HellForge rewrites it when a
 * hook goes in after it in the chain. */

#define HOOK_TARGETS 1024
#define HOOK_LINKS 16
#define HOOK_SAVED 32

typedef struct HookLink
{
    HfMod* mod;       /* NULL: HellForge's own */
    char owner[96];   /* the mod's id, or "HellForge (<what>)" */
    int32_t priority;
    int loadIndex;    /* the mod's place in load order */
    void* detour;
    void** original;
} HookLink;

typedef struct HookTarget
{
    uintptr_t target;
    uint8_t* stubs;               /* head stub (+0, its pointer at +8) and tail stub (+16, its pointer at +24) */
    uint8_t saved[HOOK_SAVED];    /* the function's first bytes before it was hooked (HookedCode) */
    size_t savedSize;             /* 0 when the hook changed none of them (an overlay's stub, a jump followed) */
    char how[96];
    int count;
    HookLink links[HOOK_LINKS];   /* first to run (outermost) to last */
} HookTarget;

static HookTarget g_hookTargets[HOOK_TARGETS];
static volatile LONG g_hookTargetCount;
static SRWLOCK g_hookLock = SRWLOCK_INIT;
static uint8_t* g_hookStubPage;
static size_t g_hookStubUsed;
#define HOOK_STUB_PAGE 0x10000

/* game.dll functions hooked (the Lua bridge's hellforge_lua_rewrites): their rvas. */
#define HOOKED_MAX 256
static uint32_t g_hookedRvas[HOOKED_MAX];
static volatile LONG g_hookedCount;

static void** StubPointer(uint8_t* stub) { return (void**)(stub + 8); }

/* jmp [rip+2]; int3; int3; <pointer>: the pointer 8-byte aligned, so it is swapped in one write. */
static void WriteStub(uint8_t* stub, void* to)
{
    static const uint8_t jump[8] = {0xFF, 0x25, 0x02, 0x00, 0x00, 0x00, 0xCC, 0xCC};
    memcpy(stub, jump, 8);
    *StubPointer(stub) = to;
}

static void SetPointer(void** at, void* value)
{
    if (((uintptr_t)at & 7) == 0) InterlockedExchangePointer((void* volatile*)at, value);
    else memcpy(at, &value, sizeof value);
}

/* "game.dll+0x13c0a80", or the bare address outside a module. */
static void HookWhere(uintptr_t at, char* out, size_t size)
{
    HMODULE module = NULL;
    char path[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &module) &&
        GetModuleFileNameA(module, path, sizeof path))
    {
        const char* name = strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path;
        snprintf(out, size, "%s+0x%llx", name, (unsigned long long)(at - (uintptr_t)module));
    }
    else snprintf(out, size, "0x%llx", (unsigned long long)at);
}

/* True when `a` runs before `b` (is outside it) in a chain. */
static int HookRunsBefore(const HookLink* a, const HookLink* b)
{
    if ((a->mod == NULL) != (b->mod == NULL)) return a->mod != NULL; /* mods before HellForge's own */
    if (a->mod == NULL) return 1;                                     /* HellForge's: the newest first */
    if (a->priority != b->priority) return a->priority > b->priority;
    return a->loadIndex >= b->loadIndex;                              /* later in load order first; the same mod's newest first */
}

/* The session's hooks, every chain first to last: logs/sessions/<session>/hooks.tsv. Under g_hookLock. */
static void HookReport(void)
{
    HANDLE file;
    wchar_t path[MAX_PATH];
    if (!g_logs[0] || swprintf(path, MAX_PATH, L"%ls\\hooks.tsv", g_logs) < 0) return;
    file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[512];
    DWORD written;
    int length = snprintf(line, sizeof line, "function\tplace\towner\tpriority\thow\n");
    WriteFile(file, line, (DWORD)length, &written, NULL);
    for (LONG t = 0; t < g_hookTargetCount; t++)
    {
        HookTarget* h = &g_hookTargets[t];
        char where[128];
        HookWhere(h->target, where, sizeof where);
        for (int i = 0; i < h->count; i++)
        {
            length = snprintf(line, sizeof line, "%s\t%d of %d\t%s\t%d\t%s\n", where, i + 1, h->count, h->links[i].owner,
                (int)h->links[i].priority, h->how[0] ? h->how : "MinHook");
            if (length > 0) WriteFile(file, line, (DWORD)(length < (int)sizeof line ? length : (int)sizeof line - 1), &written, NULL);
        }
    }
    CloseHandle(file);
}

/* A chain with two owners or more: one line naming them all, first to last, in modloader.log and each mod's log. */
static void HookShared(HookTarget* h)
{
    int owners = 0;
    for (int i = 0; i < h->count; i++)
    {
        int seen = 0;
        for (int j = 0; j < i; j++) seen |= h->links[j].mod == h->links[i].mod;
        owners += !seen;
    }
    if (owners < 2) return;
    char where[128], list[1024];
    size_t used = 0;
    HookWhere(h->target, where, sizeof where);
    list[0] = 0;
    for (int i = 0; i < h->count && used < sizeof list; i++)
    {
        int n = snprintf(list + used, sizeof list - used, "%s%s (priority %d)", i ? ", then " : "", h->links[i].owner, (int)h->links[i].priority);
        if (n < 0) break;
        used += (size_t)n;
    }
    Note("hooks: shared hook %s, first to last: %s", where, list);
    for (int i = 0; i < h->count; i++)
    {
        int seen = 0;
        for (int j = 0; j < i; j++) seen |= h->links[j].mod == h->links[i].mod;
        if (h->links[i].mod != NULL && !seen) ApiLog(h->links[i].mod, "shared hook %s, first to last: %s", where, list);
    }
}

/* The function's first patch: MinHook, else FarHook, sending it to the head stub; the trampoline goes into the tail
 * stub's pointer. 0 when hooked. Under g_hookLock. */
static int HookInstall(HookTarget* h, char* why, size_t whySize)
{
    if (!g_hooksReady) g_hooksReady = MH_Initialize() == MH_OK ? 1 : -1;
    if (g_hooksReady != 1) { snprintf(why, whySize, "MinHook did not start"); return 1; }
    MakeExecutable(h->target);
    MH_STATUS status = MH_CreateHook((void*)h->target, h->stubs, StubPointer(h->stubs + 16));
    if (status == MH_OK) status = MH_EnableHook((void*)h->target);
    if (status == MH_OK) { h->savedSize = HOOK_SAVED; return 0; }
    if (status == MH_ERROR_MEMORY_ALLOC)
    {
        const char* how = NULL;
        if (FarHook(h->target, h->stubs, StubPointer(h->stubs + 16), &how) == 0)
        {
            snprintf(h->how, sizeof h->how, "no room near it for MinHook: %s", how);
            h->savedSize = strstr(how, "trampoline") ? HOOK_SAVED : 0;
            return 0;
        }
        snprintf(why, whySize, "MinHook %d, then: %s", (int)status, how ? how : "?");
        return 1;
    }
    snprintf(why, whySize, status == MH_ERROR_ALREADY_CREATED ? "MinHook %d: hooked outside HellForge's chain" : "MinHook %d", (int)status);
    return 1;
}

/* Adds `detour` to the chain of `target` for `mod` (NULL: HellForge's own, `what` naming it). 0 when it is in. */
static int HookAdd(HfMod* mod, const char* what, uintptr_t target, void* detour, void** original, int32_t priority)
{
    char where[128];
    HookWhere(target, where, sizeof where);
    if (target == 0 || detour == NULL || original == NULL)
    {
        if (mod) ApiLog(mod, "hook refused: %s", target == 0 ? "no function (0)" : detour == NULL ? "no detour" : "no variable for the original");
        else Note("hooks: %s refused (no function, detour or original)", what ? what : "?");
        return 1;
    }
    HookLink link = {0};
    link.mod = mod;
    link.priority = mod ? priority : 0;
    link.loadIndex = mod ? ModLoadIndex(mod) : -1;
    link.detour = detour;
    link.original = original;
    if (mod) snprintf(link.owner, sizeof link.owner, "%s", mod->id);
    else snprintf(link.owner, sizeof link.owner, "HellForge (%s)", what ? what : "loader");

    AcquireSRWLockExclusive(&g_hookLock);
    HookTarget* h = NULL;
    for (LONG t = 0; t < g_hookTargetCount && h == NULL; t++) if (g_hookTargets[t].target == target) h = &g_hookTargets[t];
    char why[192] = "";
    int fresh = h == NULL;
    if (fresh)
    {
        if (g_hookTargetCount >= HOOK_TARGETS) snprintf(why, sizeof why, "%d hooked functions already (the most)", HOOK_TARGETS);
        else if (g_hookStubPage == NULL && (g_hookStubPage = (uint8_t*)VirtualAlloc(NULL, HOOK_STUB_PAGE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)) == NULL)
            snprintf(why, sizeof why, "no memory for its stubs");
        else if (g_hookStubUsed + 32 > HOOK_STUB_PAGE) snprintf(why, sizeof why, "no room for its stubs");
        else
        {
            h = &g_hookTargets[g_hookTargetCount];
            memset(h, 0, sizeof *h);
            h->target = target;
            h->stubs = g_hookStubPage + g_hookStubUsed;
            if (SafeRead(target, h->saved, HOOK_SAVED)) { snprintf(why, sizeof why, "its code cannot be read"); h = NULL; }
            else
            {
                /* the whole chain before the function is patched: the link, its original the tail stub */
                void* before = *original;
                *original = h->stubs + 16;
                WriteStub(h->stubs, detour);
                WriteStub(h->stubs + 16, NULL);
                FlushInstructionCache(GetCurrentProcess(), h->stubs, 32);
                if (HookInstall(h, why, sizeof why)) { *original = before; h = NULL; }
                else
                {
                    h->links[0] = link;
                    h->count = 1;
                    g_hookStubUsed += 32;
                    InterlockedIncrement(&g_hookTargetCount);
                    size_t gameSize = 0;
                    uintptr_t game = ApiModuleBase("game.dll", &gameSize);
                    if (game != 0 && target >= game && target < game + gameSize && g_hookedCount < HOOKED_MAX)
                    {
                        g_hookedRvas[g_hookedCount] = (uint32_t)(target - game);
                        InterlockedIncrement(&g_hookedCount);
                    }
                }
            }
        }
    }
    else
    {
        for (int i = 0; i < h->count && !why[0]; i++)
            if (h->links[i].detour == detour) snprintf(why, sizeof why, "%s hooks it with this detour already", h->links[i].owner);
        if (!why[0] && h->count >= HOOK_LINKS) snprintf(why, sizeof why, "%d hooks on it already (the most)", HOOK_LINKS);
        if (why[0]) h = NULL;
        else
        {
            int at = 0;
            while (at < h->count && !HookRunsBefore(&link, &h->links[at])) at++;
            /* the new link's original first, then the pointer that leads to it */
            SetPointer(original, at < h->count ? h->links[at].detour : (void*)(h->stubs + 16));
            for (int i = h->count; i > at; i--) h->links[i] = h->links[i - 1];
            h->links[at] = link;
            h->count++;
            SetPointer(at == 0 ? StubPointer(h->stubs) : h->links[at - 1].original, detour);
        }
    }
    int place = 0, count = 0;
    if (h != NULL)
    {
        for (int i = 0; i < h->count; i++) if (h->links[i].detour == detour) place = i + 1;
        count = h->count;
        HookShared(h);
        HookReport();
    }
    ReleaseSRWLockExclusive(&g_hookLock);

    if (h == NULL)
    {
        if (mod)
        {
            uint8_t bytes[16] = {0};
            SafeRead(target, bytes, sizeof bytes);
            ApiLog(mod, "could not hook %s (%s); its first bytes %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                where, why, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9],
                bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
        }
        Note("hooks: %s could not hook %s (%s)", link.owner, where, why);
        return 1;
    }
    if (mod) ApiLog(mod, "hooked %s (priority %d; %d of %d in its chain%s%s)", where, (int)priority, place, count,
        fresh && h->how[0] ? "; " : "", fresh ? h->how : "");
    Note("hooks: %s hooked %s (priority %d; %d of %d in its chain%s%s)", link.owner, where, (int)link.priority, place, count,
        fresh && h->how[0] ? "; " : "", fresh ? h->how : "");
    return 0;
}

/* Code as the game has it: `size` bytes at `at`, with a hooked function's first bytes as they were before its hook (a
 * function behind a hook is still the reviewed one). 0 when read. */
static int HookedCode(uintptr_t at, uint8_t* bytes, size_t size)
{
    if (SafeRead(at, bytes, size)) return 1;
    AcquireSRWLockShared(&g_hookLock);
    for (LONG t = 0; t < g_hookTargetCount; t++)
    {
        const HookTarget* h = &g_hookTargets[t];
        for (size_t i = 0; i < h->savedSize; i++)
            if (h->target + i >= at && h->target + i < at + size) bytes[h->target + i - at] = h->saved[i];
    }
    ReleaseSRWLockShared(&g_hookLock);
    return 0;
}

/* HfApi.read_code and code_matches (SDK 13): a mod's check of a function before it hooks it, which another hook in
 * front (a mod's that hooked first, or HellForge's) does not fail. Only hooks are read through: other writes, a mod's
 * hf->write or HellForge's rewritten instructions, read as they are. */
static int ApiReadCode(uintptr_t address, void* buffer, size_t size)
{
    return address == 0 || buffer == NULL ? 1 : HookedCode(address, (uint8_t*)buffer, size);
}

static int ApiCodeMatches(uintptr_t address, const char* pattern)
{
    uint8_t bytes[256], mask[256], code[256];
    size_t count = ParsePattern(pattern, bytes, mask);
    if (address == 0 || count == 0 || HookedCode(address, code, count)) return 0;
    for (size_t i = 0; i < count; i++) if (mask[i] && code[i] != bytes[i]) return 0;
    return 1;
}

/* HfApi.hook (SDK 1) and hook_ex (SDK 13). */
static int ApiHook(HfMod* mod, uintptr_t target, void* detour, void** original) { return HookAdd(mod, NULL, target, detour, original, 0); }
static int ApiHookEx(HfMod* mod, uintptr_t target, void* detour, void** original, int32_t priority)
{
    return HookAdd(mod, NULL, target, detour, original, priority);
}

/* HellForge's own hooks: the last in every chain. */
static int LoaderHook(const char* what, uintptr_t target, void* detour, void** original) { return HookAdd(NULL, what, target, detour, original, 0); }

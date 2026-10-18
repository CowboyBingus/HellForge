/* The editor against a real D3D12 swap chain, without the game: a window that presents frames like the game does (clear,
 * ExecuteCommandLists, Present), the editor DLL loaded with a small stand-in SDK (log, on, hook through MinHook, set_live,
 * get_live, mod_folder), F8 pressed, the window resized and the editor closed and opened again; then, as a player would,
 * Tab to the objects and to the fields, a field picked with the arrows, "1200" typed and applied with F9. It passes when
 * every Present succeeds, the device is never removed, the editor reports it is drawing, its window shows on screen (with
 * a screenshot), the change reached set_live, and Local Stats holds it.
 * Usage: overlay_test.exe <hellforge_editor.dll> [<screenshot.bmp> [<editor folder: editor-catalog.json, game-icons.bin>]] */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <string.h>
#include "MinHook.h"
#include "hellforge_mod.h"

struct HfMod { const HfApi* api; };
static HfMod g_mod;
static HfApi g_api;
static HfEventCallback g_ready;
static int g_drawing, g_hooked, g_lives, g_presetImports;
static int g_modPage, g_modApplied; /* a mod's stratagem: its page made, its saved value sent to its own row */
static char g_modDir[MAX_PATH] = "";
static HfSession g_sessionNow; /* the co-op role the stand-in loader reports */
static void Session(HfMod*, HfSession* out) { *out = g_sessionNow; }
static double g_lastLive;
static char g_folder[MAX_PATH] = "";
static char g_logsFolder[MAX_PATH] = "";

static void Log(HfMod*, const char* format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof line, format, arguments);
    va_end(arguments);
    printf("  mod: %s\n", line);
    if (strstr(line, "drawing on the game's queue")) g_drawing = 1;
    if (strstr(line, "hooked the swap chain")) g_hooked = 1;
    if (strstr(line, "staged (APPLY writes them)")) g_presetImports++;
    if (strstr(line, "A/MG-101 Heavy MG Sentry gets a page")) g_modPage = 1;
}
static const char* Build() { return "25480438"; }
static const char* ModFolder(HfMod*) { return g_folder; }
static int On(HfMod* mod, HfEvent event, HfEventCallback callback, void* user) { if (event == HF_EVENT_ENTITIES_READY) g_ready = callback; return 0; }
static int Hook(HfMod*, uintptr_t target, void* detour, void** original)
{
    if (MH_CreateHook((void*)target, detour, original) != MH_OK) return 1;
    return MH_EnableHook((void*)target) == MH_OK ? 0 : 1;
}
static int g_resets;
static int g_refuseCapacity; /* the big preset's test: magazine capacities refused (its errors) */
static const char* LastLog(HfMod*) { return "set_live: test: magazine capacities are refused here"; }
static const char* LogsFolder(HfMod*) { return g_logsFolder; }
static double g_modCooldown = -2; /* the mod stratagem's cooldown as the editor last set it (-1: its edit undone) */
static int SetLive(HfMod*, HfRef target, const HfField* field, const HfValue* value, uint32_t flags)
{
    if (g_refuseCapacity && value && field && field->offset == 136) return 0;
    if (target == 150 && value && value->number == 150 && field && !strcmp(field->component, "StratagemDefinition")) g_modApplied++;
    if (target == 150 && field && !strcmp(field->component, "StratagemDefinition")) g_modCooldown = value ? value->number : -1;
    g_lives++;
    if (value) g_lastLive = value->number;
    else if (flags & HF_LIVE_BASE) g_resets++;
    return HF_APPLIES_RESPAWN;
}
/* every field holds 640, its base 600; whole-number fields (references among them) hold their base */
static int GetLive(HfMod*, HfRef, const HfField* field, HfLiveValue* out)
{
    out->now.type = out->base.type = field->type;
    out->now.number = field->type == HF_TYPE_U32 ? 600 : 640;
    out->base.number = 600;
    out->mod = NULL;
    return HF_APPLIES_RESPAWN;
}
/* fields applied at boot (entity deltas, say): every one holds 1.5 (whole ones 7), its base 1 */
static int g_stats;
static int GetStat(HfMod*, const HfStat* stat, HfLiveValue* out)
{
    g_stats++;
    out->now.type = out->base.type = stat->type;
    out->now.number = stat->type == HF_TYPE_F32 ? 1.5 : 7;
    out->base.number = 1;
    out->mod = NULL;
    return 1;
}
/* A mod's content, as the loader lists it: a copy of the A/MG-43 sentry's stratagem (type 150) dropping a copy of the
 * sentry (its own designation) that fires a copy of its round dealing a copy of its damage. */
static const HfEditorEntity kModEntities[] = {{0xABCDull, 0x37CDE43876BA26BBull}, {0, 0}};
static int Content(HfMod*, uint32_t index, HfContent* out)
{
    HfContent c = {};
    c.size = sizeof c;
    c.mod = "test.mod";
    c.folder = g_modDir;
    switch (index)
    {
    case 0: c.kind = HF_KIND_STRATAGEM; c.id = 150; c.base = 121; c.name = "test/mod/hmg"; c.title = "Heavy MG Sentry";
        c.icon = "test/mod/icons/hmg"; c.editor_like = 121; c.editor_entities = kModEntities; break;
    case 1: c.kind = HF_KIND_ENTITY; c.id = 0xABCD; c.base = 0x37CDE43876BA26BBull; c.name = "test/mod/hmg_entity"; c.designation = "A/MG-101"; break;
    case 2: c.kind = HF_KIND_PROJECTILE; c.id = 400; c.base = 148; c.link = 700; c.name = "test/mod/round"; break;
    case 3: c.kind = HF_KIND_DAMAGE; c.id = 700; c.base = 125; c.name = "test/mod/damage"; break;
    default: return 0;
    }
    *out = c;
    return 1;
}

static void Key(HWND window, WPARAM key) { SendMessageW(window, WM_KEYDOWN, key, 0); SendMessageW(window, WM_KEYUP, key, 0xC0000000); }
static void Type(HWND window, const char* text) { for (; *text; text++) SendMessageW(window, WM_CHAR, (WPARAM)*text, 0); }
/* The screen as it is into a 24-bit BMP. */
static void Screenshot(const char* path, UINT width, UINT height)
{
    HDC screen = GetDC(NULL), memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, (int)width, (int)height);
    SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, (int)width, (int)height, screen, 0, 0, SRCCOPY);
    BITMAPINFOHEADER header = {sizeof header, (LONG)width, -(LONG)height, 1, 24, BI_RGB};
    DWORD row = ((width * 3 + 3) & ~3u), size = row * height;
    BYTE* pixels = (BYTE*)malloc(size);
    GetDIBits(memory, bitmap, 0, height, pixels, (BITMAPINFO*)&header, DIB_RGB_COLORS);
    BITMAPFILEHEADER file = {0x4D42, (DWORD)(sizeof file + sizeof header + size), 0, 0, (DWORD)(sizeof file + sizeof header)};
    FILE* out = fopen(path, "wb");
    if (out) { fwrite(&file, sizeof file, 1, out); fwrite(&header, sizeof header, 1, out); fwrite(pixels, size, 1, out); fclose(out); }
    free(pixels); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(NULL, screen);
}

static bool CopyInto(const char* from, const char* folder, const char* name)
{
    char source[MAX_PATH], target[MAX_PATH];
    snprintf(source, sizeof source, "%s\\%s", from, name);
    snprintf(target, sizeof target, "%s\\%s", folder, name);
    return CopyFileA(source, target, FALSE) != 0;
}
static void* Record(HfMod*, HfRef, const char*, uint32_t, uint32_t size) /* a Liberator record holding 640 rpm */
{
    static HfProjectileWeapon weapon;
    weapon.fire_rate[1] = 640.0f;
    return size == sizeof weapon ? &weapon : NULL;
}

int main(int argc, char** argv)
{
    if (argc < 2) { puts("usage: overlay_test.exe <hellforge_editor.dll> [<screenshot.bmp> [<editor folder>]]"); return 2; }
    SetProcessDPIAware(); /* the screenshot in real pixels, not a scaled copy */
    MH_Initialize();
    g_api.version = HF_MOD_SDK_VERSION; g_api.size = sizeof g_api;
    g_api.log = Log; g_api.build_id = Build; g_api.on = On; g_api.hook = Hook; g_api.set_live = SetLive; g_api.record = Record; g_api.mod_folder = ModFolder;
    g_api.get_live = GetLive;
    g_api.last_log = LastLog; g_api.logs_folder = LogsFolder; g_api.get_stat = GetStat; g_api.session = Session;
    g_api.content = Content;
    /* the editor's folder in a mods folder of the test's own: its catalogue and icons, and Local Stats next to it */
    char mods[MAX_PATH], stats[MAX_PATH];
    GetTempPathA(MAX_PATH, mods);
    snprintf(mods + strlen(mods), MAX_PATH - strlen(mods), "hellforge-overlay-test-%lu", GetCurrentProcessId());
    snprintf(g_folder, sizeof g_folder, "%s\\hellforge-editor", mods);
    snprintf(stats, sizeof stats, "%s\\zz.local.stats\\hellforge-stats.json", mods);
    bool catalogued = false;
    if (argc > 3)
    {
        CreateDirectoryA(mods, NULL);
        CreateDirectoryA(g_folder, NULL);
        catalogued = CopyInto(argv[3], g_folder, "editor-catalog.json");
        CopyInto(argv[3], g_folder, "game-icons.bin");
        /* the mod: its data names its icon's image (a 4 x 4 BMP here); a cooldown the editor saved for its stratagem */
        snprintf(g_modDir, sizeof g_modDir, "%s\\test-mod", mods);
        CreateDirectoryA(g_modDir, NULL);
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\hellforge-data.json", g_modDir);
        if (FILE* file = fopen(path, "wb"))
        {
            fputs("{\"format\":1,\"patches\":[{\"archives\":[\"007e093ca718ca1a\"],\"resources\":[{\"icon\":\"icon.bmp\",\"name\":\"test/mod/icons/hmg\"}]}]}", file);
            fclose(file);
        }
        snprintf(path, sizeof path, "%s\\icon.bmp", g_modDir);
        if (FILE* file = fopen(path, "wb"))
        {
            /* the stratagem pack's format: red the category layer, green the white one (BMP rows are blue, green, red) */
            BYTE pixels[4 * 4 * 3];
            for (int i = 0; i < 16; i++)
            {
                bool inner = (i % 4 == 1 || i % 4 == 2) && (i / 4 == 1 || i / 4 == 2);
                pixels[3 * i] = 0; pixels[3 * i + 1] = inner ? 255 : 0; pixels[3 * i + 2] = inner ? 0 : 255;
            }
            BITMAPINFOHEADER header = {sizeof header, 4, 4, 1, 24, BI_RGB};
            BITMAPFILEHEADER bmp = {0x4D42, (DWORD)(sizeof bmp + sizeof header + sizeof pixels), 0, 0, (DWORD)(sizeof bmp + sizeof header)};
            fwrite(&bmp, sizeof bmp, 1, file); fwrite(&header, sizeof header, 1, file); fwrite(pixels, sizeof pixels, 1, file);
            fclose(file);
        }
        snprintf(path, sizeof path, "%s\\mod-stats.json", g_folder);
        if (FILE* file = fopen(path, "wb"))
        {
            fputs("{\"format\":1,\"mod:test/mod/hmg\":{\"test/mod/hmg\":{\"stratagem.cooldown\":150}}}", file);
            fclose(file);
        }
        /* the Patriot's left gun swapped to the Emancipator's (mounted weapon 2), its Mounted Weapons and left gun open */
        char settings[MAX_PATH];
        snprintf(settings, sizeof settings, "%s\\editor-settings.json", g_folder);
        if (FILE* file = fopen(settings, "wb"))
        {
            /* HELLFORGE_EDITOR_TEST_SCALE=1.5: the screenshots at that UI scale (the category list then outgrows its panel) */
            const char* scale = getenv("HELLFORGE_EDITOR_TEST_SCALE");
            fprintf(file, "{%s%s%s\"open\":[\"stratagems/EXO-45 Patriot Exosuit|c|mounts\",\"stratagems/EXO-45 Patriot Exosuit|c|mount:EXO-45 Patriot Exosuit / left_gun\"],"
                  "\"mounts\":{\"EXO-45 Patriot Exosuit|mount.slot_0\":2}}", scale ? "\"ui_scale\":" : "", scale ? scale : "", scale ? "," : "");
            fclose(file);
        }
    }
    g_mod.api = &g_api;

    WNDCLASSEXW type = {sizeof type, 0, DefWindowProcW, 0, 0, GetModuleHandleW(NULL), NULL, LoadCursor(NULL, IDC_ARROW), NULL, NULL, L"OverlayTest", NULL};
    RegisterClassExW(&type);
    HWND window = CreateWindowExW(WS_EX_TOPMOST, type.lpszClassName, L"HellForge overlay test", WS_POPUP | WS_VISIBLE, 0, 0, 1280, 720, NULL, NULL, type.hInstance, NULL);
    ID3D12Device* device; ID3D12CommandQueue* queue; IDXGIFactory2* factory; IDXGISwapChain1* chain1; IDXGISwapChain3* chain;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = 1280; desc.Height = 720; desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM; desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 3; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) || FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))) ||
        FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->CreateSwapChainForHwnd(queue, window, &desc, NULL, NULL, &chain1)) ||
        FAILED(chain1->QueryInterface(IID_PPV_ARGS(&chain))))
    { puts("FAIL no D3D12 swap chain on this machine"); return 1; }

    HMODULE editor = LoadLibraryA(argv[1]);
    typedef int (*Init)(const HfApi*, HfMod*);
    Init init = editor ? (Init)(void*)GetProcAddress(editor, "hellforge_mod_init") : NULL;
    if (!init || init(&g_api, &g_mod) != 0 || !g_ready) { puts("FAIL the editor did not initialize"); return 1; }
    g_ready(&g_mod, HF_EVENT_ENTITIES_READY, NULL);
    if (catalogued)
    {
        printf("  mod stratagem: page %s, its saved cooldown sent to its own row %d time(s)\n", g_modPage ? "made" : "NOT made", g_modApplied);
        if (!g_modPage || g_modApplied != 1) { puts("FAIL a mod's stratagem got no page, or its saved value did not reach its own row"); return 1; }
    }

    ID3D12CommandAllocator* allocator; ID3D12GraphicsCommandList* list; ID3D12Fence* fence; ID3D12DescriptorHeap* rtvHeap;
    D3D12_DESCRIPTOR_HEAP_DESC rtv = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, NULL, IID_PPV_ARGS(&list));
    list->Close();
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&rtvHeap));
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL);
    UINT64 value = 0;
    int failures = 0;
    UINT width = 1280, height = 720;
    for (int frame = 0; frame < 420; frame++)
    {
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        if (frame == 30 || frame == 260 || frame == 300) SendMessageW(window, WM_KEYDOWN, VK_F8, 0);     /* open, close, open */
        /* to the objects, the next one back, to its fields, three rows down (past the category header, the projectile swap
         * and the part header), 1200, F9 */
        if (frame == 320 || frame == 330) Key(window, VK_TAB);
        if (frame == 324) Key(window, VK_DOWN);
        if (frame == 326) Key(window, VK_UP);
        /* headers start closed: Right opens the category, Down past the projectile swap to the Flight header, Right opens
         * it, Down to its first row */
        if (frame == 332 || frame == 338) Key(window, VK_RIGHT);
        if (frame == 334 || frame == 336 || frame == 340) Key(window, VK_DOWN);
        if (frame == 342) Type(window, "1200");
        if (frame == 350) Key(window, VK_RETURN);
        if (frame == 356) Key(window, VK_F9);
        if (frame == 150 || frame == 200) /* the game resizes its swap chain */
        {
            fence->SetEventOnCompletion(value, event); queue->Signal(fence, ++value); WaitForSingleObject(event, 2000);
            width = frame == 150 ? 1600 : 1280; height = frame == 150 ? 900 : 720;
            SetWindowPos(window, NULL, 0, 0, (int)width, (int)height, SWP_NOZORDER);
            if (FAILED(chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) { puts("FAIL ResizeBuffers"); failures++; }
        }
        UINT index = chain->GetCurrentBackBufferIndex();
        ID3D12Resource* buffer;
        chain->GetBuffer(index, IID_PPV_ARGS(&buffer));
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        device->CreateRenderTargetView(buffer, NULL, handle);
        allocator->Reset(); list->Reset(allocator, NULL);
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource = buffer;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1, &barrier);
        const float color[4] = {0.05f, 0.25f, 0.05f, 1.0f}; /* green: what the "game" draws */
        list->ClearRenderTargetView(handle, color, 0, NULL);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1, &barrier);
        list->Close();
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        HRESULT presented = chain->Present(1, 0);
        if (FAILED(presented)) { printf("FAIL Present 0x%08lx at frame %d\n", (unsigned long)presented, frame); failures++; }
        queue->Signal(fence, ++value);
        fence->SetEventOnCompletion(value, event); WaitForSingleObject(event, 2000);
        buffer->Release();
        if (frame == 400 && argc > 2) /* the screen as it is, editor open */
        {
            HDC screen = GetDC(NULL), memory = CreateCompatibleDC(screen);
            HBITMAP bitmap = CreateCompatibleBitmap(screen, (int)width, (int)height);
            SelectObject(memory, bitmap);
            BitBlt(memory, 0, 0, (int)width, (int)height, screen, 0, 0, SRCCOPY);
            BITMAPINFOHEADER header = {sizeof header, (LONG)width, -(LONG)height, 1, 24, BI_RGB};
            DWORD row = ((width * 3 + 3) & ~3u), size = row * height;
            BYTE* pixels = (BYTE*)malloc(size);
            GetDIBits(memory, bitmap, 0, height, pixels, (BITMAPINFO*)&header, DIB_RGB_COLORS);
            BITMAPFILEHEADER file = {0x4D42, (DWORD)(sizeof file + sizeof header + size), 0, 0, (DWORD)(sizeof file + sizeof header)};
            FILE* out = fopen(argv[2], "wb");
            if (out) { fwrite(&file, sizeof file, 1, out); fwrite(&header, sizeof header, 1, out); fwrite(pixels, size, 1, out); fclose(out); }
            /* The editor's window is drawn from (60, 60): its pixels there are not the clear colour. */
            int drawn = 0;
            for (int y = 80; y < 200; y++) for (int x = 80; x < 400; x++)
            {
                BYTE* p = pixels + y * row + x * 3;
                if (!(p[1] > 50 && p[1] < 80 && p[0] < 30 && p[2] < 30)) drawn++;
            }
            printf("  screen: %d of %d pixels in the editor's area are not the clear colour\n", drawn, 120 * 320);
            if (drawn < 1000) { puts("FAIL the editor's window is not on screen"); failures++; }
            free(pixels); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(NULL, screen);
        }
    }
    /* the change: applied through set_live, then saved into Local Stats (a second after it, from a frame) */
    if (catalogued)
    {
        bool saved = false;
        for (int wait = 0; wait < 60 && !saved; wait++)
        {
            chain->Present(1, 0);
            FILE* file = fopen(stats, "rb");
            char text[4096] = {};
            if (file) { fread(text, 1, sizeof text - 1, file); fclose(file); saved = strstr(text, ": 1200") != NULL; }
            if (!saved) Sleep(50);
        }
        printf("  applied: set_live %d time(s), last %.0f; Local Stats %s\n", g_lives, g_lastLive, saved ? "holds 1200" : "does not hold it");
        if (g_lives < 1 || g_lastLive != 1200) { puts("FAIL the change did not reach set_live"); failures++; }
        if (!saved) { puts("FAIL Local Stats does not hold the change"); failures++; }
        /* Del stages the field's reset (nothing written yet), F9 applies it: the base back, and the field out of Local Stats */
        auto frames = [&](int n) {
            for (int i = 0; i < n; i++)
            {
                MSG message;
                while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
                chain->Present(1, 0);
            }
        };
        Key(window, VK_DELETE);
        frames(10);
        int staged = g_resets;
        Key(window, VK_F9);
        frames(10);
        bool gone = false;
        for (int wait = 0; wait < 60 && !gone; wait++)
        {
            frames(1);
            gone = GetFileAttributesA(stats) == INVALID_FILE_ATTRIBUTES;
            if (!gone) Sleep(50);
        }
        printf("  reset: staged without writing %s, applied %d reset(s); Local Stats %s\n", staged == 0 ? "yes" : "NO", g_resets,
            gone ? "no longer holds it (the mod is gone)" : "still holds it");
        if (staged != 0 || g_resets < 1) { puts("FAIL the reset was not staged, then applied"); failures++; }
        if (!gone) { puts("FAIL Local Stats still holds the reset field"); failures++; }
        /* the projectile swap (two rows up): its picker, filtered, a donor staged (the weapon's own rows locked), applied */
        Key(window, VK_UP); frames(4);
        Key(window, VK_UP); frames(4);
        Key(window, VK_RETURN); frames(10);
        char shot[MAX_PATH];
        snprintf(shot, sizeof shot, "%s", argc > 2 ? argv[2] : "");
        char* dot = strrchr(shot, '.');
        if (argc > 2 && dot) { snprintf(dot, (size_t)(shot + sizeof shot - dot), "-picker.bmp"); Screenshot(shot, width, height); }
        Type(window, "eagle");
        frames(6);
        Key(window, VK_DOWN); frames(4);
        Key(window, VK_RETURN); frames(10);
        if (argc > 2 && dot) { snprintf(dot, (size_t)(shot + sizeof shot - dot), "-swapped.bmp"); Screenshot(shot, width, height); }
        int before = g_lives;
        Key(window, VK_F9); frames(10);
        printf("  swap: set_live %d time(s), last %.0f\n", g_lives - before, g_lastLive);
        if (g_lives - before != 1 || g_lastLive == 600 || g_lastLive == 1200 || g_lastLive < 1)
        { puts("FAIL the projectile swap did not reach set_live with a donor"); failures++; }
        Key(window, VK_DELETE); frames(10);
        Key(window, VK_F9); frames(20);
        /* a preset (HD2R Editor's format) imported from the PRESETS view (F6 x3): a number, unlimited uses, no impact
         * explosion and a status staged; an enemy (no HellForge row) skipped, a status no damage applies refused */
        {
            char presets[MAX_PATH], preset[MAX_PATH];
            snprintf(presets, sizeof presets, "%s\\presets", g_folder);
            CreateDirectoryA(presets, NULL);
            snprintf(preset, sizeof preset, "%s\\test.hd2rpreset.json", presets);
            FILE* file = fopen(preset, "wb");
            fputs("{\"format\":\"hd2r-editor-preset\",\"version\":1,\"name\":\"Overlay test\",\"editor\":\"1.0.0\",\"runtime\":\"0.40.0\",\"fields\":["
                "{\"key\":\"pw|AR-11 Arbitrator|damage.durable_damage\",\"value\":41},"
                "{\"key\":\"st|Orbital Precision Strike|stratagem:orbital_precision_strike:orbital_precision_strike_stratagem:stratagem.max_uses\",\"value\":\"unlimited\"},"
                "{\"key\":\"pw|R-36 Eruptor|terminal.primary.impact.explosion\",\"value\":{\"ref\":\"none\"}},"
                "{\"key\":\"pw|R-36 Eruptor|damage.status_1_type\",\"value\":\"fire\"},"
                "{\"key\":\"pw|R-36 Eruptor|damage.status_2_type\",\"value\":\"pure_damage\"},"
                "{\"key\":\"en|Bile Titan|entity::entity.health\",\"value\":7000}]}", file);
            fclose(file);
            for (int i = 0; i < 3; i++) { Key(window, VK_F6); frames(4); }
            Key(window, VK_RETURN); frames(10);
            if (argc > 2 && dot) { snprintf(dot, (size_t)(shot + sizeof shot - dot), "-presets.bmp"); Screenshot(shot, width, height); }
            int lives = g_lives;
            Key(window, VK_F9); frames(20);
            bool saved = false;
            char text[8192] = {};
            for (int wait = 0; wait < 60 && !saved; wait++)
            {
                frames(1);
                FILE* in = fopen(stats, "rb");
                if (in) { fread(text, 1, sizeof text - 1, in); fclose(in); }
                saved = strstr(text, "\"stratagem.max_uses\": -1") && strstr(text, "\"terminal.primary.impact.explosion\": 0") &&
                    strstr(text, "\"damage.status_1_type\": 5") && strstr(text, "\"damage.durable_damage\": 41");
                if (!saved) Sleep(50);
            }
            printf("  preset: set_live %d time(s); Local Stats %s\n", g_lives - lives, saved ? "holds the four values" : "does not hold them");
            if (g_lives - lives != 4 || !saved) { puts("FAIL the preset's four values were not staged and applied"); failures++; }
            Key(window, VK_F6); frames(4); /* LOGS */
            Key(window, VK_F6); frames(4); /* SETTINGS */
            Key(window, VK_F6); frames(4); /* BROWSE */
        }
        /* the stratagems: back to the categories, down to Offensive (past Secondary and Throwables), a screenshot */
        if (argc > 2)
        {
            Key(window, VK_TAB);
            frames(4);
            for (int i = 0; i < 3; i++) { Key(window, VK_DOWN); frames(4); }
            frames(20);
            if (dot) snprintf(dot, (size_t)(shot + sizeof shot - dot), "-stratagems.bmp");
            Screenshot(shot, width, height);
            /* Defensive: the mod's stratagem, last in the list (its own icon), its page with its first category open */
            Key(window, VK_DOWN); frames(4);
            Key(window, VK_TAB); frames(4);
            for (int i = 0; i < 6; i++) { Key(window, VK_NEXT); frames(4); }
            Key(window, VK_TAB); frames(4);
            Key(window, VK_RIGHT); frames(20);
            if (dot) snprintf(dot, (size_t)(shot + sizeof shot - dot), "-mod.bmp");
            Screenshot(shot, width, height);
            Key(window, VK_TAB); frames(4); /* back to the categories, at Defensive */
            /* Vehicles (past Support Weapons, Support Backpacks): the Patriot, its swapped left gun's stats in the left
             * gun's category, below Mounted Weapons */
            for (int i = 0; i < 3; i++) { Key(window, VK_DOWN); frames(4); }
            Key(window, VK_TAB); frames(4);
            Key(window, VK_TAB); frames(20);
            if (dot) snprintf(dot, (size_t)(shot + sizeof shot - dot), "-vehicle.bmp");
            Screenshot(shot, width, height);
            Key(window, VK_TAB); frames(4); /* back to the categories, at Vehicles */
            /* the Helldiver (past Defensive, Support Weapons, Support Backpacks, Vehicles, Mission and Boosters): its armor
             * perks, the armor passive's picker open */
            for (int i = 0; i < 3; i++) { Key(window, VK_DOWN); frames(4); } /* Mission, Boosters, Helldiver */
            Key(window, VK_TAB); frames(4);
            Key(window, VK_DOWN); frames(4);   /* past the Helldiver's page, to its armor perks */
            Key(window, VK_TAB); frames(4);
            Key(window, VK_RIGHT); frames(4);  /* its header open (headers start closed) */
            Key(window, VK_DOWN); frames(4);
            Key(window, VK_RETURN); frames(20);
            if (dot) snprintf(dot, (size_t)(shot + sizeof shot - dot), "-helldiver.bmp");
            Screenshot(shot, width, height);
            Key(window, VK_ESCAPE); frames(4);
        }
        /* a big preset (HF_TEST_BIG_PRESET: HD2R Editor's Black Wolfs balance, ~1900 fields) imported: thousands of pending
         * changes must not slow the editor (frames timed without vsync in PRESETS, CHANGES and BROWSE) */
        const char* big = getenv("HF_TEST_BIG_PRESET");
        if (big != NULL && big[0])
        {
            char target[MAX_PATH];
            snprintf(target, sizeof target, "%s\\presets\\big.hd2rpreset.json", g_folder);
            if (CopyFileA(big, target, FALSE))
            {
                auto timed = [&](const char* where) {
                    LARGE_INTEGER f, a, b;
                    QueryPerformanceFrequency(&f);
                    for (int i = 0; i < 5; i++) { MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } chain->Present(0, 0); }
                    QueryPerformanceCounter(&a);
                    for (int i = 0; i < 60; i++) { MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } chain->Present(0, 0); }
                    QueryPerformanceCounter(&b);
                    double ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart / 60.0;
                    printf("  big preset: %.1f ms a frame in %s\n", ms, where);
                    if (ms > 50) { printf("FAIL the editor is slow with a big preset in %s\n", where); failures++; }
                };
                for (int i = 0; i < 3; i++) { Key(window, VK_F6); frames(4); }   /* PRESETS (big.hd2rpreset.json first) */
                Key(window, VK_HOME); frames(2);
                Key(window, VK_RETURN); frames(4);
                timed("PRESETS");
                for (int i = 0; i < 5; i++) { Key(window, VK_F6); frames(2); }   /* LOGS, SETTINGS, BROWSE, SEARCH, CHANGES */
                timed("CHANGES");
                /* fields applied at boot among them (attachments, boosters): read through get_stat, not left blank */
                printf("  get_stat: %d read(s)\n", g_stats);
                if (g_stats == 0) { puts("FAIL no field applied at boot was read through get_stat"); failures++; }
                for (int i = 0; i < 4; i++) { Key(window, VK_F6); frames(2); }   /* PRESETS, LOGS, SETTINGS, BROWSE */
                timed("BROWSE");
                /* applied, with refusals: the ERRORS chip opens the errors list; then the LOGS view */
                snprintf(g_logsFolder, sizeof g_logsFolder, "%s", mods);
                char logPath[MAX_PATH];
                snprintf(logPath, sizeof logPath, "%s\\modloader.log", mods);
                FILE* log = fopen(logPath, "wb");
                fputs("10:00:00.000 HellForge mod loader, SDK 6: 9 code mod(s)\n10:00:01.000 conflict: a and b both change X\n"
                      "10:00:02.000 stats x: refused: the record is not as reviewed\n10:00:03.000 entity data ready\n", log);
                fclose(log);
                g_refuseCapacity = 1;
                Key(window, VK_F9); frames(20);
                g_refuseCapacity = 0;
                float unit = (float)height / 1080.0f, ox = ((float)width / unit - 1260) / 2, oy = (1080 - 930) / 2.0f;
                LPARAM at = MAKELPARAM((int)((ox + 1260 - 62 - 50) * unit), (int)((oy + 29) * unit));
                SendMessageW(window, WM_MOUSEMOVE, 0, at); frames(3);
                SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, at); frames(2);
                SendMessageW(window, WM_LBUTTONUP, 0, at); frames(10);
                if (dot) { snprintf(dot, (size_t)(shot + sizeof shot - dot), "-errors.bmp"); Screenshot(shot, width, height); }
                Key(window, VK_ESCAPE); frames(4);
                for (int i = 0; i < 4; i++) { Key(window, VK_F6); frames(3); } /* SEARCH, CHANGES, PRESETS, LOGS */
                frames(10);
                if (dot) { snprintf(dot, (size_t)(shot + sizeof shot - dot), "-logs.bmp"); Screenshot(shot, width, height); }
                /* joined to a friend's lobby: the editor is greyed and read-only (a preset import or APPLY changes nothing), and
                 * the mod stratagem's cooldown is the host's (host-mod-stats.json, which the session's backend writes) */
                char hostMods[MAX_PATH];
                snprintf(hostMods, sizeof hostMods, "%s\\host-mod-stats.json", g_folder);
                if (FILE* file = fopen(hostMods, "wb"))
                {
                    fputs("{\"format\":1,\"mod:test/mod/hmg\":{\"test/mod/hmg\":{\"stratagem.cooldown\":90}}}", file);
                    fclose(file);
                }
                g_sessionNow.role = HF_SESSION_JOINER; g_sessionNow.version = 1; snprintf(g_sessionNow.host, sizeof g_sessionNow.host, "Friend Host");
                for (int i = 0; i < 2; i++) { Key(window, VK_F6); frames(3); } /* SETTINGS, BROWSE */
                frames(10);
                if (dot) { snprintf(dot, (size_t)(shot + sizeof shot - dot), "-readonly.bmp"); Screenshot(shot, width, height); }
                for (int i = 0; i < 3; i++) { Key(window, VK_F6); frames(3); } /* SEARCH, CHANGES, PRESETS */
                int imports = g_presetImports, writes = g_lives;
                Key(window, VK_HOME); frames(2); Key(window, VK_RETURN); frames(6); Key(window, VK_F9); frames(6);
                printf("  read-only: %d import(s), %d write(s) while joined\n", g_presetImports - imports, g_lives - writes);
                if (g_presetImports != imports || g_lives != writes) { puts("FAIL a joined editor changed something"); failures++; }
                double joined = g_modCooldown;
                g_sessionNow.role = HF_SESSION_SOLO; g_sessionNow.version = 2; g_sessionNow.host[0] = 0;
                DeleteFileA(hostMods);
                frames(30);
                printf("  co-op mod values: the mod stratagem's cooldown %.0f while joined (the host's 90), %.0f after (its own 150)\n", joined, g_modCooldown);
                if (joined != 90 || g_modCooldown != 150) { puts("FAIL the mod content values did not follow the co-op role"); failures++; }
            }
        }
    }
    HRESULT removed = device->GetDeviceRemovedReason();
    if (removed != S_OK) { printf("FAIL device removed 0x%08lx\n", (unsigned long)removed); failures++; }
    if (!g_hooked || !g_drawing) { puts("FAIL the editor did not hook or start drawing"); failures++; }
    printf("%s overlay: hooked %d, drawing %d, 420 frames, two resizes, F8 open/close/open, device %s\n", failures ? "FAIL" : "PASS",
        g_hooked, g_drawing, removed == S_OK ? "healthy" : "removed");
    return failures ? 1 : 0;
}

/* HellForge's in-game editor (docs/sdk-roadmap.md phase 5a): a code mod on the public SDK that draws the editor's window
 * over the game (F8; app.cpp, the HD2R Editor's layout and look) with Dear ImGui's renderer, and changes fields live
 * through set_live.
 *
 * Drawing: the game renders with D3D12 (borderless). The editor hooks IDXGISwapChain::Present / Present1 and
 * ResizeBuffers (their addresses come from a throwaway device and swap chain) and ID3D12CommandQueue::
 * ExecuteCommandLists, which shows the game's direct queue. Just before the game presents, ImGui is recorded into the
 * back buffer on that queue, with a fence per back buffer, so nothing depends on the game's own GUI, materials or
 * fonts, and the window draws over the game's menus.
 * Input: the game window's procedure is replaced by one that toggles the window (F8) and, while it is open, gives
 * keyboard, mouse and raw input to ImGui instead of the game; the game's cursor clipping and recentring are ignored
 * meanwhile, and ImGui draws its own cursor. ImGui runs under one lock, taken by the window thread (messages) and the
 * render thread (frames), so it is never used from two threads at once.
 * Values: changes go through set_live, the same path as the launcher's live editor (applied at once by identity, with
 * the loader's checks), so nothing here writes game memory itself; get_live reads them. Applied changes are also saved
 * into the Local Stats mod (local_stats.cpp; the launcher's STATS page saves there too), on a thread of their own: the
 * stats pass applies them whenever the game loads, so they come back every boot. A reset takes a field out of that file
 * and puts its base back (set_live with HF_LIVE_BASE).
 * Images: the game's weapon and throwable silhouettes, extracted from the player's own game into the editor's folder
 * (game-icons.bin), and the HD2R Editor's marker icons, in one texture (icons.cpp). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "app.h"
#include "canvas.h"
#include "hellforge_mod.h"
#include "icons.h"
#include "local_stats.h"
#include "sounds.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM w, LPARAM l);

typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain3*, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* Present1Fn)(IDXGISwapChain3*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffersFn)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
typedef void(STDMETHODCALLTYPE* ExecuteFn)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
typedef BOOL(WINAPI* ClipCursorFn)(const RECT*);
typedef BOOL(WINAPI* SetCursorPosFn)(int, int);
typedef UINT(WINAPI* RawInputBufferFn)(PRAWINPUT, PUINT, UINT);

static const HfApi* g_api;
static HfMod* g_mod;
static PresentFn g_present;
static Present1Fn g_present1;
static ResizeBuffersFn g_resize;
static ExecuteFn g_execute;
static ClipCursorFn g_clipCursor;
static SetCursorPosFn g_setCursorPos;
static RawInputBufferFn g_rawInputBuffer;

static CRITICAL_SECTION g_lock;   /* ImGui: the window thread's messages and the render thread's frames */
static volatile LONG g_open;      /* the window is shown */
static ID3D12CommandQueue* g_queue;
static int g_ready, g_failed;
static HWND g_window;
static WNDPROC g_gameProc;

#define MAX_BUFFERS 8
static ID3D12Device* g_device;
static ID3D12DescriptorHeap *g_rtvHeap, *g_srvHeap;
static UINT g_rtvSize, g_buffers;
static DXGI_FORMAT g_format;
static ID3D12CommandAllocator* g_allocators[MAX_BUFFERS];
static ID3D12GraphicsCommandList* g_list;
static ID3D12Fence* g_fence;
static HANDLE g_fenceEvent;
static UINT64 g_fenceValue, g_frameFence[MAX_BUFFERS];

/* ---- the editor's window ---------------------------------------------------------------------------------------- */

/* Saving into Local Stats: when the queued changes are written (0: now), and how the last save went. */
static std::string g_modsFolder;
static volatile LONG g_saving, g_saveFailed;
static ULONGLONG g_saveAt;
static char g_saveStatus[256];

static DWORD WINAPI SaveThread(void*)
{
    std::string error;
    bool saved = localstats::Save(g_modsFolder, &error);
    snprintf(g_saveStatus, sizeof g_saveStatus, saved ? "saved to Local Stats" : "Local Stats not saved: %s", error.c_str());
    g_api->log(g_mod, "editor: %s", g_saveStatus);
    if (!saved) InterlockedExchange(&g_saveFailed, 1);
    InterlockedExchange(&g_saving, 0);
    return 0;
}

static void SaveSoon(int ms) { g_saveAt = GetTickCount64() + (ULONGLONG)ms; }
static void CloseEditor() { InterlockedExchange(&g_open, 0); g_saveAt = 0; }

/* Called every frame, open or not: starts a save once the queued changes have settled. */
static void SaveTick()
{
    if (g_modsFolder.empty() || g_saving || !localstats::Pending() || GetTickCount64() < g_saveAt) return;
    InterlockedExchange(&g_saving, 1);
    HANDLE thread = CreateThread(NULL, 0, SaveThread, NULL, 0, NULL);
    if (thread != NULL) CloseHandle(thread); else InterlockedExchange(&g_saving, 0);
}

/* ---- input ------------------------------------------------------------------------------------------------------ */

static bool InputMessage(UINT message)
{
    return (message >= WM_KEYFIRST && message <= WM_KEYLAST) || (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
        message == WM_INPUT || message == WM_MOUSEHOVER || message == WM_MOUSELEAVE || message == WM_NCMOUSEMOVE;
}

/* The editor's sounds are posted on the window's thread, the game's main thread, as the game's own menus post theirs. */
#define WM_EDITOR_SOUND (WM_APP + 0x46)

static LRESULT CALLBACK EditorProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_EDITOR_SOUND)
    {
        static int seen;
        if (seen++ == 0) g_api->log(g_mod, "editor: sound requests reach the window's thread (%lu)", GetCurrentThreadId());
        sounds::Flush();
        return 0;
    }
    if (message == WM_KEYDOWN && w == VK_F8 && !(l & 0x40000000))
    {
        LONG open = !g_open;
        if (!open) g_saveAt = 0; /* closing saves at once */
        InterlockedExchange(&g_open, open);
        if (open && g_clipCursor) g_clipCursor(NULL);
        return 0;
    }
    if (g_open && g_ready)
    {
        EnterCriticalSection(&g_lock);
        /* a button or the wheel where the mouse is (ImGui learns the position only from moves: the first click after
         * opening, before any move, landed nowhere) */
        if ((message >= WM_LBUTTONDOWN && message <= WM_MBUTTONDBLCLK) || (message >= WM_XBUTTONDOWN && message <= WM_XBUTTONDBLCLK))
        {
            ImGui::GetIO().AddMousePosEvent((float)(short)LOWORD(l), (float)(short)HIWORD(l));
        }
        else if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
        {
            POINT at = {(short)LOWORD(l), (short)HIWORD(l)};
            ScreenToClient(window, &at);
            ImGui::GetIO().AddMousePosEvent((float)at.x, (float)at.y);
        }
        ImGui_ImplWin32_WndProcHandler(window, message, w, l);
        LeaveCriticalSection(&g_lock);
        if (InputMessage(message)) return message == WM_INPUT ? DefWindowProcW(window, message, w, l) : 0;
    }
    return CallWindowProcW(g_gameProc, window, message, w, l);
}

static BOOL WINAPI ClipCursorDetour(const RECT* rect) { return g_open ? TRUE : g_clipCursor(rect); }
static BOOL WINAPI SetCursorPosDetour(int x, int y) { return g_open ? TRUE : g_setCursorPos(x, y); }
static UINT WINAPI RawInputBufferDetour(PRAWINPUT data, PUINT size, UINT header)
{
    if (g_open && data != NULL) { UINT dropped = g_rawInputBuffer(data, size, header); (void)dropped; return 0; }
    return g_rawInputBuffer(data, size, header);
}

/* ---- rendering -------------------------------------------------------------------------------------------------- */

/* The view heap: ImGui's font atlas in the first slot, the editor's images (icons.cpp) in the second. */
static void SrvAlloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
{
    *cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
    *gpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
}

static void ImagesView(D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
{
    UINT step = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    *cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
    *gpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
    cpu->ptr += step;
    gpu->ptr += step;
}
static void SrvFree(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {}

static void WaitFence(UINT64 value)
{
    if (value == 0 || g_fence->GetCompletedValue() >= value) return;
    g_fence->SetEventOnCompletion(value, g_fenceEvent);
    WaitForSingleObject(g_fenceEvent, 2000);
}

static bool InitDx12Backend()
{
    ImGui_ImplDX12_InitInfo info;
    info.Device = g_device;
    info.CommandQueue = g_queue;
    info.NumFramesInFlight = (int)g_buffers;
    info.RTVFormat = g_format;
    info.SrvDescriptorHeap = g_srvHeap;
    info.SrvDescriptorAllocFn = SrvAlloc;
    info.SrvDescriptorFreeFn = SrvFree;
    return ImGui_ImplDX12_Init(&info);
}

static bool Initialize(IDXGISwapChain3* swapChain)
{
    DXGI_SWAP_CHAIN_DESC desc;
    if (FAILED(swapChain->GetDesc(&desc)) || FAILED(swapChain->GetDevice(IID_PPV_ARGS(&g_device)))) return false;
    g_window = desc.OutputWindow;
    g_buffers = desc.BufferCount < MAX_BUFFERS ? desc.BufferCount : MAX_BUFFERS;
    g_format = desc.BufferDesc.Format;
    D3D12_DESCRIPTOR_HEAP_DESC rtv = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, MAX_BUFFERS, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC srv = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    if (FAILED(g_device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&g_rtvHeap))) || FAILED(g_device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&g_srvHeap))))
        return false;
    g_rtvSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (UINT i = 0; i < MAX_BUFFERS; i++)
        if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_allocators[i])))) return false;
    if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_allocators[0], NULL, IID_PPV_ARGS(&g_list)))) return false;
    g_list->Close();
    if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) return false;
    g_fenceEvent = CreateEventW(NULL, FALSE, FALSE, NULL);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.MouseDrawCursor = true;
    RECT client;
    GetClientRect(g_window, &client);
    ImGui::StyleColorsDark();
    if (!ImGui_ImplWin32_Init(g_window) || !InitDx12Backend()) return false;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu;
    ImagesView(&cpu, &gpu);
    if (!icons::Upload(g_device, g_queue, g_execute, cpu, gpu)) g_api->log(g_mod, "editor: the images could not be made; the editor shows none");
    g_gameProc = (WNDPROC)SetWindowLongPtrW(g_window, GWLP_WNDPROC, (LONG_PTR)EditorProc);
    g_api->log(g_mod, "editor: drawing on the game's queue %p, %u back buffers, format %d, %ldx%ld; F8 opens it (frames on thread %lu, "
        "the window's thread %lu)", (void*)g_queue, g_buffers, (int)g_format, client.right - client.left, client.bottom - client.top,
        GetCurrentThreadId(), GetWindowThreadProcessId(g_window, NULL));
    return g_gameProc != NULL;
}

static void Render(IDXGISwapChain3* swapChain)
{
    if (g_failed || g_queue == NULL) return;
    EnterCriticalSection(&g_lock);
    if (!g_ready)
    {
        if (!Initialize(swapChain)) { g_failed = 1; g_api->log(g_mod, "editor: could not set up drawing; the editor stays off"); }
        else g_ready = 1;
    }
    static bool shown;
    if (g_ready && (g_open != 0) != shown)
    {
        shown = g_open != 0;
        if (shown) editorapp::Opened(); else editorapp::Closed();
    }
    if (g_ready && g_saveFailed) { InterlockedExchange(&g_saveFailed, 0); editorapp::SaveStatus(false, g_saveStatus); }
    if (g_ready) editorapp::Tick();
    if (g_ready && g_open)
    {
        DXGI_SWAP_CHAIN_DESC desc;
        if (FAILED(swapChain->GetDesc(&desc))) { LeaveCriticalSection(&g_lock); return; }
        if (desc.BufferDesc.Format != g_format)
        {
            WaitFence(g_fenceValue);
            ImGui_ImplDX12_Shutdown();
            g_format = desc.BufferDesc.Format;
            if (!InitDx12Backend()) { g_failed = 1; LeaveCriticalSection(&g_lock); return; }
        }
        /* the fonts at the window's scale: made again (after our last frame) when the screen or the interface size changes */
        float width = (float)desc.BufferDesc.Width, height = (float)desc.BufferDesc.Height;
        if (ui::BuildFonts(editorapp::PixelScale(width, height)))
        {
            g_api->log(g_mod, "editor: fonts for %.0fx%.0f at %.3f pixels a unit", width, height, editorapp::PixelScale(width, height));
            WaitFence(g_fenceValue);
            ImGui_ImplDX12_InvalidateDeviceObjects();
        }
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        editorapp::Frame(ImGui::GetBackgroundDrawList(), width, height, ImGui::GetIO().DeltaTime);
        ImGui::Render();

        UINT index = swapChain->GetCurrentBackBufferIndex() % MAX_BUFFERS;
        ID3D12Resource* buffer = NULL;
        if (SUCCEEDED(swapChain->GetBuffer(swapChain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&buffer))))
        {
            WaitFence(g_frameFence[index]);
            g_allocators[index]->Reset();
            g_list->Reset(g_allocators[index], NULL);
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            rtv.ptr += (SIZE_T)index * g_rtvSize;
            g_device->CreateRenderTargetView(buffer, NULL, rtv);
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = buffer;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            g_list->ResourceBarrier(1, &barrier);
            g_list->OMSetRenderTargets(1, &rtv, FALSE, NULL);
            g_list->SetDescriptorHeaps(1, &g_srvHeap);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_list);
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            g_list->ResourceBarrier(1, &barrier);
            g_list->Close();
            ID3D12CommandList* lists[] = {g_list};
            g_execute(g_queue, 1, lists);
            g_queue->Signal(g_fence, ++g_fenceValue);
            g_frameFence[index] = g_fenceValue;
            buffer->Release();
        }
    }
    /* sounds asked for this frame (the closing one's too) go to the window's thread */
    if (g_ready && sounds::Waiting() && !sounds::OnGameThread()) PostMessageW(g_window, WM_EDITOR_SOUND, 0, 0);
    LeaveCriticalSection(&g_lock);
}

static HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain3* swapChain, UINT interval, UINT flags)
{
    SaveTick();
    if (!(flags & DXGI_PRESENT_TEST)) Render(swapChain);
    return g_present(swapChain, interval, flags);
}

static HRESULT STDMETHODCALLTYPE Present1Detour(IDXGISwapChain3* swapChain, UINT interval, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters)
{
    SaveTick();
    if (!(flags & DXGI_PRESENT_TEST)) Render(swapChain);
    return g_present1(swapChain, interval, flags, parameters);
}

static HRESULT STDMETHODCALLTYPE ResizeBuffersDetour(IDXGISwapChain3* swapChain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags)
{
    EnterCriticalSection(&g_lock);
    if (g_ready) WaitFence(g_fenceValue); /* our last frame is done with the old buffers */
    LeaveCriticalSection(&g_lock);
    return g_resize(swapChain, count, width, height, format, flags);
}

static void STDMETHODCALLTYPE ExecuteDetour(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
{
    if (g_queue == NULL && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        queue->AddRef();
        g_queue = queue;
    }
    g_execute(queue, count, lists);
}

/* ---- set-up ----------------------------------------------------------------------------------------------------- */

/* The addresses of the swap chain's and the queue's methods: a throwaway device, queue and swap chain on a hidden
 * window show them (every swap chain and queue of the process shares these implementations). */
static bool FindMethods(void** present, void** present1, void** resize, void** execute)
{
    WNDCLASSEXW type = {sizeof type, 0, DefWindowProcW, 0, 0, GetModuleHandleW(NULL), NULL, NULL, NULL, NULL, L"HellForgeEditorProbe", NULL};
    RegisterClassExW(&type);
    HWND window = CreateWindowExW(0, type.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, type.hInstance, NULL);
    ID3D12Device* device = NULL;
    ID3D12CommandQueue* queue = NULL;
    IDXGIFactory2* factory = NULL;
    IDXGISwapChain1* swapChain = NULL;
    bool found = false;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = desc.Height = 64;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (window != NULL && SUCCEEDED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) &&
        SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))) && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateSwapChainForHwnd(queue, window, &desc, NULL, NULL, &swapChain)))
    {
        void** chain = *(void***)swapChain;
        void** commands = *(void***)queue;
        *present = chain[8];    /* IDXGISwapChain::Present */
        *resize = chain[13];    /* IDXGISwapChain::ResizeBuffers */
        *present1 = chain[22];  /* IDXGISwapChain1::Present1 */
        *execute = commands[10]; /* ID3D12CommandQueue::ExecuteCommandLists */
        found = true;
    }
    if (swapChain) swapChain->Release();
    if (factory) factory->Release();
    if (queue) queue->Release();
    if (device) device->Release();
    if (window) DestroyWindow(window);
    UnregisterClassW(type.lpszClassName, type.hInstance);
    return found;
}

static void Start(HfMod* mod, HfEvent event, void* user)
{
    editorapp::AddModContent(); /* every mod has registered its content by now; before the first frame */
    /* game.dll runs by now (its code is unpacked once it starts): the menu sounds can be checked */
    g_api->log(mod, "editor: menu sounds %s", sounds::Init(g_api, mod) ? "on (the game's UI sound function checked)" : "off");
    void *present, *present1, *resize, *execute;
    if (!FindMethods(&present, &present1, &resize, &execute)) { g_api->log(mod, "editor: no D3D12 device for the probe; the editor stays off"); return; }
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    bool hooked =
        !g_api->hook(mod, (uintptr_t)execute, (void*)ExecuteDetour, (void**)&g_execute) &&
        !g_api->hook(mod, (uintptr_t)resize, (void*)ResizeBuffersDetour, (void**)&g_resize) &&
        !g_api->hook(mod, (uintptr_t)present, (void*)PresentDetour, (void**)&g_present) &&
        !g_api->hook(mod, (uintptr_t)present1, (void*)Present1Detour, (void**)&g_present1);
    if (hooked && user32 != NULL)
    {
        g_api->hook(mod, (uintptr_t)GetProcAddress(user32, "ClipCursor"), (void*)ClipCursorDetour, (void**)&g_clipCursor);
        g_api->hook(mod, (uintptr_t)GetProcAddress(user32, "SetCursorPos"), (void*)SetCursorPosDetour, (void**)&g_setCursorPos);
        g_api->hook(mod, (uintptr_t)GetProcAddress(user32, "GetRawInputBuffer"), (void*)RawInputBufferDetour, (void**)&g_rawInputBuffer);
    }
    g_api->log(mod, hooked ? "editor: hooked the swap chain and the queue; drawing starts with the next frame" : "editor: could not hook D3D12; the editor stays off");
}

extern "C" HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 6) return 1;
    g_api = hf;
    g_mod = mod;
    /* Local Stats is the editor's neighbour in the mods folder. */
    std::string folder = hf->mod_folder(mod);
    size_t slash = folder.find_last_of("\\/");
    if (slash != std::string::npos) g_modsFolder = folder.substr(0, slash);
    InitializeCriticalSection(&g_lock);
    editorapp::Host host = {hf, mod, folder, g_modsFolder, CloseEditor, SaveSoon};
    editorapp::Init(host);
    /* Once the game runs and has its device: entity data loads after the renderer is up. */
    return hf->on(mod, HF_EVENT_ENTITIES_READY, Start, NULL);
}

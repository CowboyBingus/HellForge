// HellForge stand-in for the copied game's steam_api64.dll. Every export except the two that hand out Steam's remote storage
// interface is a loader forwarder to the real DLL, which the session places next to it as steam_api64_hellforge.dll
// (HellForge.SdkShimGen forward writes the .def): sign-in, ownership and everything else stay Valve's own code.
//
// The game keeps a cloud save (testament_new.sav: loadout, title, player card) and input_settings.config in Steam
// Cloud, which belongs to the Steam account and is shared with the regular game and uploaded to Valve. The stand-in
// interface keeps those files in HELLFORGE_CLOUD_DIR (the profile's folder inside the installation) instead. The game's
// executable calls only FileWrite, FileRead, FileForget, FileDelete, FileExists, GetFileSize and the two
// IsCloudEnabled checks (docs/steam-cloud.md); every other method fails without side effects. Without HELLFORGE_CLOUD_DIR
// cloud storage reports itself disabled: the real storage is never used. Calls are logged to HELLFORGE_CLOUD_LOG if set.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "net_gate.h"
#include "steam_census.h"
#include "bridge_channel.h"
#include "../common/hf_privacy.h"

#define HELLFORGE_API
#define STORAGE_VERSION "STEAMREMOTESTORAGE_INTERFACE_VERSION016"
#define REAL_MODULE L"steam_api64_hellforge.dll"
#define SLOT_COUNT 59
#define MAX_FILE_BYTES (64 * 1024 * 1024)

static CRITICAL_SECTION g_lock;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static wchar_t g_dir[MAX_PATH];
static bool g_enabled;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static HMODULE g_self;
static GameChannel g_bridge;
static bool g_bridged;

static void Log(const char *format, ...)
{
    if (g_log == INVALID_HANDLE_VALUE) return;
    char line[512];
    SYSTEMTIME now;
    GetSystemTime(&now);
    int length = snprintf(line, sizeof line, "%02u:%02u:%02u.%03u ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    va_list arguments;
    va_start(arguments, format);
    int more = vsnprintf(line + length, sizeof line - length - 1, format, arguments);
    va_end(arguments);
    length += more < 0 ? 0 : (more > (int)sizeof line - length - 2 ? (int)sizeof line - length - 2 : more);
    length = (int)HfPrivacyScrub(line, (size_t)length, sizeof line - 1);
    line[length++] = '\n';
    DWORD written;
    WriteFile(g_log, line, (DWORD)length, &written, NULL);
}

static BOOL CALLBACK Initialize(PINIT_ONCE once, void *parameter, void **context)
{
    wchar_t log[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"HELLFORGE_CLOUD_LOG", log, MAX_PATH);
    if (length > 0 && length < MAX_PATH)
        g_log = CreateFileW(log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    length = GetEnvironmentVariableW(L"HELLFORGE_CLOUD_DIR", g_dir, MAX_PATH - 64);
    DWORD attributes = length > 0 && length < MAX_PATH - 64 ? GetFileAttributesW(g_dir) : INVALID_FILE_ATTRIBUTES;
    g_enabled = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    Log("start: cloud storage %s", g_enabled ? "in HELLFORGE_CLOUD_DIR" : "disabled (HELLFORGE_CLOUD_DIR missing)");
    return TRUE;
}

static void EnsureInitialized(void) { InitOnceExecuteOnce(&g_once, Initialize, NULL, NULL); }

// Cloud file names are plain names ("testament_new.sav"); anything that could leave the folder is refused.
static bool PathFor(const char *name, wchar_t *path)
{
    if (!g_enabled || !name || !name[0] || strlen(name) > 128 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (const char *c = name; *c; c++)
        if (*c < 0x20 || strchr("\\/:*?\"<>|", *c)) return false;
    wchar_t wide[160];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide, 160)) return false;
    return swprintf(path, MAX_PATH, L"%ls\\%ls", g_dir, wide) > 0;
}

static int32_t SizeOf(const wchar_t *path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return -1;
    return data.nFileSizeHigh || data.nFileSizeLow > MAX_FILE_BYTES ? -1 : (int32_t)data.nFileSizeLow;
}

// Slot 0. Written to a temporary file first, so a save is either the old or the new one, never half of each.
static bool StorageFileWrite(void *self, const char *name, const void *data, int32_t size)
{
    wchar_t path[MAX_PATH], temporary[MAX_PATH];
    if (!PathFor(name, path) || size < 0 || size > MAX_FILE_BYTES || (!data && size)) { Log("FileWrite %s refused", name ? name : "(null)"); return false; }
    swprintf(temporary, MAX_PATH, L"%ls.hellforge-tmp", path);
    EnterCriticalSection(&g_lock);
    bool ok = false;
    HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        ok = WriteFile(file, data, (DWORD)size, &written, NULL) && written == (DWORD)size && FlushFileBuffers(file);
        CloseHandle(file);
        ok = ok && MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) DeleteFileW(temporary);
    }
    LeaveCriticalSection(&g_lock);
    Log("FileWrite %s %d bytes %s", name, size, ok ? "ok" : "failed");
    return ok;
}

// Slot 1. Returns the number of bytes read (at most the buffer size), 0 when the file is missing.
static int32_t StorageFileRead(void *self, const char *name, void *buffer, int32_t capacity)
{
    wchar_t path[MAX_PATH];
    int32_t result = 0;
    if (PathFor(name, path) && buffer && capacity > 0)
    {
        EnterCriticalSection(&g_lock);
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD read = 0;
            if (ReadFile(file, buffer, (DWORD)capacity, &read, NULL)) result = (int32_t)read;
            CloseHandle(file);
        }
        LeaveCriticalSection(&g_lock);
    }
    Log("FileRead %s capacity %d -> %d", name ? name : "(null)", capacity, result);
    return result;
}

// Slot 5. Steam would stop syncing the file but keep it locally; there is nothing to sync here.
static bool StorageFileForget(void *self, const char *name)
{
    wchar_t path[MAX_PATH];
    bool exists = PathFor(name, path) && SizeOf(path) >= 0;
    Log("FileForget %s -> %d", name ? name : "(null)", exists);
    return exists;
}

// Slot 6.
static bool StorageFileDelete(void *self, const char *name)
{
    wchar_t path[MAX_PATH];
    bool ok = false;
    if (PathFor(name, path))
    {
        EnterCriticalSection(&g_lock);
        ok = DeleteFileW(path) != 0;
        LeaveCriticalSection(&g_lock);
    }
    Log("FileDelete %s -> %d", name ? name : "(null)", ok);
    return ok;
}

// Slots 13 and 14 (FileExists, FilePersisted).
static bool StorageFileExists(void *self, const char *name)
{
    wchar_t path[MAX_PATH];
    bool exists = PathFor(name, path) && SizeOf(path) >= 0;
    Log("FileExists %s -> %d", name ? name : "(null)", exists);
    return exists;
}

// Slot 15. 0 when the file is missing, like Steam.
static int32_t StorageGetFileSize(void *self, const char *name)
{
    wchar_t path[MAX_PATH];
    int32_t size = PathFor(name, path) ? SizeOf(path) : -1;
    Log("GetFileSize %s -> %d", name ? name : "(null)", size);
    return size < 0 ? 0 : size;
}

// Slots 21 and 22 (IsCloudEnabledForAccount, IsCloudEnabledForApp).
static bool StorageIsCloudEnabled(void *self)
{
    Log("IsCloudEnabled -> %d", g_enabled);
    return g_enabled;
}

// Every other slot: a zero result is failure for each of them (false, 0 bytes or files, k_uAPICallInvalid, an
// invalid handle). Out parameters are left alone; the game calls none of these.
#define UNUSED_SLOT(n) static uint64_t Unused##n(void *self) { Log("unused slot %d", n); return 0; }
UNUSED_SLOT(2) UNUSED_SLOT(3) UNUSED_SLOT(4) UNUSED_SLOT(7) UNUSED_SLOT(8) UNUSED_SLOT(9) UNUSED_SLOT(10) UNUSED_SLOT(11)
UNUSED_SLOT(12) UNUSED_SLOT(16) UNUSED_SLOT(17) UNUSED_SLOT(18) UNUSED_SLOT(19) UNUSED_SLOT(20) UNUSED_SLOT(23) UNUSED_SLOT(24)
UNUSED_SLOT(25) UNUSED_SLOT(26) UNUSED_SLOT(27) UNUSED_SLOT(28) UNUSED_SLOT(29) UNUSED_SLOT(30) UNUSED_SLOT(31) UNUSED_SLOT(32)
UNUSED_SLOT(33) UNUSED_SLOT(34) UNUSED_SLOT(35) UNUSED_SLOT(36) UNUSED_SLOT(37) UNUSED_SLOT(38) UNUSED_SLOT(39) UNUSED_SLOT(40)
UNUSED_SLOT(41) UNUSED_SLOT(42) UNUSED_SLOT(43) UNUSED_SLOT(44) UNUSED_SLOT(45) UNUSED_SLOT(46) UNUSED_SLOT(47) UNUSED_SLOT(48)
UNUSED_SLOT(49) UNUSED_SLOT(50) UNUSED_SLOT(51) UNUSED_SLOT(52) UNUSED_SLOT(53) UNUSED_SLOT(54) UNUSED_SLOT(55) UNUSED_SLOT(56)
UNUSED_SLOT(57) UNUSED_SLOT(58)

// ISteamRemoteStorage v016 in declaration order (isteamremotestorage.h); a C table, so no compiler vtable ABI is involved.
static const void *const g_vtable[SLOT_COUNT] = {
    (void *)StorageFileWrite, (void *)StorageFileRead, (void *)Unused2, (void *)Unused3, (void *)Unused4, (void *)StorageFileForget,
    (void *)StorageFileDelete, (void *)Unused7, (void *)Unused8, (void *)Unused9, (void *)Unused10, (void *)Unused11,
    (void *)Unused12, (void *)StorageFileExists, (void *)StorageFileExists, (void *)StorageGetFileSize, (void *)Unused16, (void *)Unused17,
    (void *)Unused18, (void *)Unused19, (void *)Unused20, (void *)StorageIsCloudEnabled, (void *)StorageIsCloudEnabled, (void *)Unused23,
    (void *)Unused24, (void *)Unused25, (void *)Unused26, (void *)Unused27, (void *)Unused28, (void *)Unused29,
    (void *)Unused30, (void *)Unused31, (void *)Unused32, (void *)Unused33, (void *)Unused34, (void *)Unused35,
    (void *)Unused36, (void *)Unused37, (void *)Unused38, (void *)Unused39, (void *)Unused40, (void *)Unused41,
    (void *)Unused42, (void *)Unused43, (void *)Unused44, (void *)Unused45, (void *)Unused46, (void *)Unused47,
    (void *)Unused48, (void *)Unused49, (void *)Unused50, (void *)Unused51, (void *)Unused52, (void *)Unused53,
    (void *)Unused54, (void *)Unused55, (void *)Unused56, (void *)Unused57, (void *)Unused58,
};
static struct { const void *const *vtable; } g_storage = { g_vtable };

typedef void *(*FindOrCreateUserInterfaceFn)(int32_t user, const char *version);

// The real DLL, which sits next to this one; loaded by full path, so no search order is involved.
static HMODULE Real(void)
{
    static HMODULE real;
    if (!real)
    {
        wchar_t path[MAX_PATH];
        DWORD length = GetModuleFileNameW(g_self, path, MAX_PATH);
        wchar_t *slash = length > 0 && length < MAX_PATH ? wcsrchr(path, L'\\') : NULL;
        if (slash && (size_t)(slash + 1 - path) + wcslen(REAL_MODULE) < MAX_PATH)
        {
            wcscpy(slash + 1, REAL_MODULE);
            real = LoadLibraryW(path);
        }
    }
    return real;
}

HMODULE SteamRealModule(void) { return Real(); }

// The accessor behind the game's SteamRemoteStorage(); every other interface comes from the real DLL (in the census build,
// behind a counting proxy).
HELLFORGE_API void *SteamInternal_FindOrCreateUserInterface(int32_t user, const char *version)
{
    if (version) SteamCensusInterface(version, user);
    if (version && strcmp(version, STORAGE_VERSION) == 0)
    {
        EnsureInitialized();
        Log("FindOrCreateUserInterface %s -> HellForge storage", version);
        return &g_storage;
    }
    HMODULE real = Real();
    FindOrCreateUserInterfaceFn forward = real ? (FindOrCreateUserInterfaceFn)(void *)GetProcAddress(real, "SteamInternal_FindOrCreateUserInterface") : NULL;
    return forward ? SteamCensusWrap(forward(user, version), version) : NULL;
}

// Callback registrations, passed on to the real DLL; the census notes each callback id and its struct's size once (a
// CCallbackBase: its vtable, a flags byte, then the id at +12; Steamworks' steam_api_common.h).
typedef void (*RegisterCallbackFn)(void *callback, int id);
typedef void (*RegisterCallResultFn)(void *callback, uint64_t call);

HELLFORGE_API void SteamAPI_RegisterCallback(void *callback, int id)
{
    SteamCensusCallback("callback", id, callback);
    HMODULE real = Real();
    RegisterCallbackFn forward = real ? (RegisterCallbackFn)(void *)GetProcAddress(real, "SteamAPI_RegisterCallback") : NULL;
    if (forward) forward(callback, id);
}

HELLFORGE_API void SteamAPI_RegisterCallResult(void *callback, uint64_t call)
{
    int id = 0;
    if (callback) memcpy(&id, (const uint8_t *)callback + 12, 4);
    SteamCensusCallback("callresult", id, callback);
    HMODULE real = Real();
    RegisterCallResultFn forward = real ? (RegisterCallResultFn)(void *)GetProcAddress(real, "SteamAPI_RegisterCallResult") : NULL;
    if (forward) forward(callback, call);
}

// The flat-API accessor for the same interface.
HELLFORGE_API void *SteamAPI_SteamRemoteStorage_v016(void)
{
    EnsureInitialized();
    return &g_storage;
}

// --- Flat API: bridge mode handles these; non-bridge forwards to the real DLL. ---
// The .def generator (SdkShimGen forward) sees HELLFORGE_API and emits these as code exports, not forwarders.

enum { BROKER_CALL_IS_STEAM_RUNNING = 0, BROKER_CALL_RUN_FRAME = 1, BROKER_CALL_SHUTDOWN = 2 };

static int BrokerCall(uint32_t callId, const void *args, uint32_t argsSize, void *result, uint32_t resultCap, uint32_t *resultSize)
{
    uint8_t buf[4096 + 4];
    memcpy(buf, &callId, 4);
    if (argsSize > 0 && args) memcpy(buf + 4, args, argsSize);
    if (!BridgePipeSend(g_bridge.pipe, BRIDGE_CALL, buf, 4 + argsSize)) return 0;
    uint8_t reply[4096 + 8];
    uint32_t replySize = 0;
    uint32_t type = BridgePipeRecv(g_bridge.pipe, reply, sizeof reply, &replySize);
    if (type != BRIDGE_RESULT || replySize < 8) return 0;
    uint32_t status;
    memcpy(&status, reply + 4, 4);
    uint32_t bodySize = replySize - 8;
    if (resultSize) *resultSize = bodySize;
    if (bodySize > 0 && result && resultCap >= bodySize) memcpy(result, reply + 8, bodySize);
    return status == 0;
}

HELLFORGE_API int __cdecl SteamAPI_RestartAppIfNecessary(uint32_t appId)
{
    Log("RestartAppIfNecessary %u -> 0 (stand-in)", appId);
    return 0;
}

HELLFORGE_API int __cdecl SteamAPI_Init(void)
{
    if (g_bridged) { Log("SteamAPI_Init -> 1 (broker)"); return 1; }
    HMODULE real = Real();
    typedef int (__cdecl *Fn)(void);
    Fn forward = real ? (Fn)(void *)GetProcAddress(real, "SteamAPI_Init") : NULL;
    int ok = forward ? forward() : 0;
    Log("SteamAPI_Init -> %d (real)", ok);
    return ok;
}

HELLFORGE_API void __cdecl SteamAPI_RunCallbacks(void)
{
    if (g_bridged)
    {
        BrokerCall(BROKER_CALL_RUN_FRAME, NULL, 0, NULL, 0, NULL);
        uint32_t cbId;
        uint8_t cbData[1024];
        uint32_t cbSize;
        while (GameChannelReadCallback(&g_bridge, &cbId, cbData, sizeof cbData, &cbSize)) {}
        return;
    }
    HMODULE real = Real();
    typedef void (__cdecl *Fn)(void);
    Fn forward = real ? (Fn)(void *)GetProcAddress(real, "SteamAPI_RunCallbacks") : NULL;
    if (forward) forward();
}

HELLFORGE_API int __cdecl SteamAPI_IsSteamRunning(void)
{
    if (g_bridged)
    {
        uint8_t running = 0;
        uint32_t size = 0;
        BrokerCall(BROKER_CALL_IS_STEAM_RUNNING, NULL, 0, &running, 1, &size);
        return size >= 1 ? running : 1;
    }
    HMODULE real = Real();
    typedef int (__cdecl *Fn)(void);
    Fn forward = real ? (Fn)(void *)GetProcAddress(real, "SteamAPI_IsSteamRunning") : NULL;
    return forward ? forward() : 1;
}

HELLFORGE_API void __cdecl SteamAPI_Shutdown(void)
{
    if (g_bridged)
    {
        BridgePipeSend(g_bridge.pipe, BRIDGE_SHUTDOWN, NULL, 0);
        GameChannelClose(&g_bridge);
        g_bridged = false;
        Log("SteamAPI_Shutdown (broker)");
        return;
    }
    HMODULE real = Real();
    typedef void (__cdecl *Fn)(void);
    Fn forward = real ? (Fn)(void *)GetProcAddress(real, "SteamAPI_Shutdown") : NULL;
    if (forward) forward();
    Log("SteamAPI_Shutdown (real)");
}

HELLFORGE_API int __cdecl SteamAPI_GetHSteamPipe(void)
{
    if (g_bridged) return 1;
    HMODULE real = Real();
    typedef int (__cdecl *Fn)(void);
    Fn forward = real ? (Fn)(void *)GetProcAddress(real, "SteamAPI_GetHSteamPipe") : NULL;
    return forward ? forward() : 0;
}

HELLFORGE_API int __cdecl SteamAPI_GetHSteamUser(void)
{
    if (g_bridged) return 1;
    HMODULE real = Real();
    typedef int (__cdecl *Fn)(void);
    Fn forward = real ? (Fn)(void *)GetProcAddress(real, "SteamAPI_GetHSteamUser") : NULL;
    return forward ? forward() : 0;
}

HELLFORGE_API void __cdecl SteamAPI_UnregisterCallback(void *callback)
{
    if (g_bridged) return;
    HMODULE real = Real();
    typedef void (__cdecl *Fn)(void *);
    Fn forward = real ? (Fn)(void *)GetProcAddress(real, "SteamAPI_UnregisterCallback") : NULL;
    if (forward) forward(callback);
}

HELLFORGE_API void __cdecl SteamAPI_ManualDispatch_Init(void)
{
    if (g_bridged) return;
    HMODULE real = Real();
    typedef void (__cdecl *Fn)(void);
    Fn forward = real ? (Fn)(void *)GetProcAddress(real, "SteamAPI_ManualDispatch_Init") : NULL;
    if (forward) forward();
}

typedef int (*LoaderStart)(const wchar_t *plan);

// Sessions with code mods name the HellForge mod loader and its plan (docs/internals.md, "The load chain"). This DLL is the first HellForge code in the
// game's process, so it starts the loader, on its own thread once the loader lock is released.
static DWORD WINAPI StartModLoader(void *parameter)
{
    wchar_t loader[MAX_PATH], plan[MAX_PATH];
    DWORD loaderLength = GetEnvironmentVariableW(L"HELLFORGE_MODLOADER", loader, MAX_PATH);
    DWORD planLength = GetEnvironmentVariableW(L"HELLFORGE_MODLOADER_PLAN", plan, MAX_PATH);
    if (loaderLength == 0 || loaderLength >= MAX_PATH || planLength == 0 || planLength >= MAX_PATH) return 0;
    HMODULE module = LoadLibraryExW(loader, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    LoaderStart start = module ? (LoaderStart)(void *)GetProcAddress(module, "hellforge_loader_start") : NULL;
    if (start) start(plan);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_self = instance;
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&g_lock);
        // the network gate first: hooked before the game's own code can reach the network (net_gate.c)
        NetGateStart();
        // the Steam census, when asked for (steam_census.c): what the game asks of Steam
        SteamCensusStart(instance);
        // bridge mode (docs/steam-bridge.md): connect to the broker instead of loading Valve's DLL
        wchar_t suffix[64];
        DWORD suffixLen = GetEnvironmentVariableW(L"HELLFORGE_BRIDGE_PIPE", suffix, 64);
        if (suffixLen > 0 && suffixLen < 64)
        {
            if (GameChannelConnect(&g_bridge, suffix) && GameChannelHello(&g_bridge))
                g_bridged = true;
        }
        if (GetEnvironmentVariableW(L"HELLFORGE_MODLOADER", NULL, 0) > 0)
        {
            HANDLE thread = CreateThread(NULL, 0, StartModLoader, NULL, 0, NULL);
            if (thread) CloseHandle(thread);
        }
    }
    return TRUE;
}

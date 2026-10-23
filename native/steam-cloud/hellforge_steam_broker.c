/* HellForge's Steam broker (docs/steam-bridge.md, phase 2): a process at normal integrity that loads Valve's
 * steam_api64.dll as app 553850, accepts the session's game over a named pipe with a low mandatory label, answers the
 * calls the allow-list permits, and pushes callbacks into a shared-memory ring.
 *
 * Usage: hellforge_steam_broker.exe <pipe-suffix> <game-pid> [valve-dll-path]
 *
 * The session runner starts one broker per session; the game's stand-in finds the pipe by HELLFORGE_BRIDGE_PIPE. The
 * broker serves only the process with <game-pid>, and exits when that process exits, when it says BRIDGE_SHUTDOWN, or
 * when its pipe breaks. Its log is its standard error.
 *
 * Phase 2: the channel, the client check and the flat functions. The generated dispatch table and allow-list are
 * phase 3 (SdkShimGen steam-bridge). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bridge_broker.h"

#define GAME_APP_ID "553850"

/* --- Valve's DLL (loaded at runtime) --- */

/* The pinned build's SDK exports SteamAPI_Init (bool). Newer SDKs have SteamAPI_InitFlat instead, which returns an
 * ESteamAPIInitResult (0: OK) with a reason in a 1024-character SteamErrMsg: used only when SteamAPI_Init is missing. */
typedef bool (__cdecl *PFN_SteamAPI_Init)(void);
typedef int  (__cdecl *PFN_SteamAPI_InitFlat)(char* error);
typedef void (__cdecl *PFN_SteamAPI_Shutdown)(void);
typedef void (__cdecl *PFN_SteamAPI_ManualDispatch_Init)(void);
typedef void (__cdecl *PFN_SteamAPI_ManualDispatch_RunFrame)(int hPipe);
typedef int  (__cdecl *PFN_SteamAPI_ManualDispatch_GetNextCallback)(int hPipe, void* msg);
typedef void (__cdecl *PFN_SteamAPI_ManualDispatch_FreeLastCallback)(int hPipe);
typedef int  (__cdecl *PFN_SteamAPI_GetHSteamPipe)(void);
typedef int  (__cdecl *PFN_SteamAPI_IsSteamRunning)(void);

static struct {
    HMODULE module;
    PFN_SteamAPI_Init                            Init;
    PFN_SteamAPI_InitFlat                        InitFlat;
    PFN_SteamAPI_Shutdown                        Shutdown;
    PFN_SteamAPI_ManualDispatch_Init             ManualDispatch_Init;
    PFN_SteamAPI_ManualDispatch_RunFrame         ManualDispatch_RunFrame;
    PFN_SteamAPI_ManualDispatch_GetNextCallback  ManualDispatch_GetNextCallback;
    PFN_SteamAPI_ManualDispatch_FreeLastCallback ManualDispatch_FreeLastCallback;
    PFN_SteamAPI_GetHSteamPipe                   GetHSteamPipe;
    PFN_SteamAPI_IsSteamRunning                  IsSteamRunning;
    int hPipe;
} g_steam;

static int LoadValveDll(const wchar_t* path)
{
    g_steam.module = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_steam.module) return 0;
#define LOAD(name) g_steam.name = (PFN_SteamAPI_##name)(void*)GetProcAddress(g_steam.module, "SteamAPI_" #name); if (!g_steam.name) return 0
    g_steam.Init = (PFN_SteamAPI_Init)(void*)GetProcAddress(g_steam.module, "SteamAPI_Init");
    g_steam.InitFlat = (PFN_SteamAPI_InitFlat)(void*)GetProcAddress(g_steam.module, "SteamAPI_InitFlat");
    if (!g_steam.Init && !g_steam.InitFlat) return 0;
    LOAD(Shutdown);
    LOAD(ManualDispatch_Init);
    LOAD(ManualDispatch_RunFrame);
    LOAD(ManualDispatch_GetNextCallback);
    LOAD(ManualDispatch_FreeLastCallback);
    LOAD(GetHSteamPipe);
    LOAD(IsSteamRunning);
#undef LOAD
    return 1;
}

/* SteamAPI_RestartAppIfNecessary is never called: it may start the installed game through Steam. The app id comes from
 * the environment instead, which Steam's init reads when there is no steam_appid.txt. */
static int InitSteam(void)
{
    if (!SetEnvironmentVariableA("SteamAppId", GAME_APP_ID) || !SetEnvironmentVariableA("SteamGameId", GAME_APP_ID)) return 0;
    if (g_steam.Init)
    {
        if (!g_steam.Init()) { fprintf(stderr, "broker: SteamAPI_Init -> false\n"); return 0; }
    }
    else
    {
        char error[1024] = "";
        int result = g_steam.InitFlat(error);
        error[sizeof error - 1] = 0;
        if (result != 0) { fprintf(stderr, "broker: SteamAPI_InitFlat -> %d (%s)\n", result, error); return 0; }
    }
    g_steam.ManualDispatch_Init();
    g_steam.hPipe = g_steam.GetHSteamPipe();
    return g_steam.hPipe != 0;
}

/* CallbackMsg_t from steam_api_common.h (what ManualDispatch_GetNextCallback fills). */
#pragma pack(push, 8)
typedef struct CallbackMsg {
    int32_t  steamUser;
    int32_t  callbackId;
    uint8_t* data;
    int32_t  dataSize;
} CallbackMsg;
#pragma pack(pop)

static void Pump(void* context, BrokerChannel* ch)
{
    (void)context;
    static uint32_t reportedDrops;
    g_steam.ManualDispatch_RunFrame(g_steam.hPipe);
    CallbackMsg msg;
    while (g_steam.ManualDispatch_GetNextCallback(g_steam.hPipe, &msg))
    {
        uint32_t size = msg.dataSize > 0 ? (uint32_t)msg.dataSize : 0;
        if (!BrokerChannelPushCallback(ch, (uint32_t)msg.callbackId, msg.data, size) && size > BRIDGE_RING_DATA_SIZE)
            fprintf(stderr, "broker: callback %d of %u bytes does not fit a ring slot; dropped\n", msg.callbackId, size);
        g_steam.ManualDispatch_FreeLastCallback(g_steam.hPipe);
    }
    /* a full ring: the game stopped reading. Said once, then counted at exit. */
    if (ch->dropped > 0 && reportedDrops == 0) { fprintf(stderr, "broker: the ring is full; callbacks are being dropped\n"); reportedDrops = 1; }
}

static int IsSteamRunning(void* context) { (void)context; return g_steam.IsSteamRunning(); }

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 3 || wcslen(argv[1]) == 0 || wcslen(argv[1]) > 64)
    {
        fprintf(stderr, "Usage: hellforge_steam_broker <pipe-suffix> <game-pid> [valve-dll-path]\n");
        return 2;
    }
    wchar_t* end = NULL;
    DWORD gamePid = (DWORD)wcstoul(argv[2], &end, 10);
    if (gamePid == 0 || end == argv[2] || *end != L'\0') { fprintf(stderr, "broker: bad game pid\n"); return 2; }

    /* the broker lives only as long as the game: watched while it waits and while it serves */
    HANDLE game = OpenProcess(SYNCHRONIZE, FALSE, gamePid);
    if (!game) { fprintf(stderr, "broker: game process %lu not found (%lu)\n", gamePid, GetLastError()); return 1; }

    /* the channel before Steam, so the game's wait starts as soon as possible */
    BrokerChannel* ch = (BrokerChannel*)calloc(1, sizeof *ch);
    if (!ch || !BrokerChannelCreate(ch, argv[1]))
    {
        fprintf(stderr, "broker: cannot create the channel (%lu)\n", GetLastError());
        CloseHandle(game);
        return 1;
    }

    if (argc >= 4 && argv[3][0] != L'\0')
    {
        if (!LoadValveDll(argv[3])) { fprintf(stderr, "broker: cannot load Valve's steam_api64.dll (%lu)\n", GetLastError()); goto fail; }
        if (!InitSteam()) { fprintf(stderr, "broker: Steam did not start\n"); goto fail; }
        fprintf(stderr, "broker: Steam ready (pipe %d)\n", g_steam.hPipe);
    }
    BrokerSteam steam = { NULL, g_steam.module ? Pump : NULL, g_steam.module ? IsSteamRunning : NULL };

    fprintf(stderr, "broker: waiting for the game (%lu)\n", gamePid);
    int accepted = BrokerChannelAccept(ch, gamePid, game, 5000);
    if (ch->refused) fprintf(stderr, "broker: refused %u other client(s)\n", ch->refused);
    if (accepted != BROKER_ACCEPTED)
    {
        fprintf(stderr, accepted == BROKER_GAME_GONE ? "broker: the game exited before connecting\n" : "broker: accept failed (%lu)\n", GetLastError());
        goto fail;
    }
    fprintf(stderr, "broker: game connected\n");

    int why = BrokerServe(ch, &steam, game);
    fprintf(stderr, "broker: %s; %u callback(s) dropped\n",
        why == BROKER_END_SHUTDOWN ? "shutdown" : why == BROKER_END_GAME_GONE ? "the game exited" : "the pipe closed", ch->dropped);

    BrokerChannelClose(ch);
    free(ch);
    CloseHandle(game);
    if (g_steam.module) g_steam.Shutdown();
    return 0;
fail:
    BrokerChannelClose(ch);
    free(ch);
    CloseHandle(game);
    if (g_steam.hPipe) g_steam.Shutdown();
    return 1;
}

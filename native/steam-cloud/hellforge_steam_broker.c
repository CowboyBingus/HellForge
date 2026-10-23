/* HellForge's Steam broker (docs/steam-bridge.md, phase 2): a process at normal integrity that loads Valve's
 * steam_api64.dll, accepts one game client over a named pipe with a low mandatory label, dispatches the calls the
 * allow-list permits, and pushes callbacks into a shared-memory ring.
 *
 * Usage: hellforge_steam_broker.exe <pipe-suffix> <expected-pid> <valve-dll-path>
 *
 * The session runner starts one broker per session, passes the suffix to the game's stand-in through an environment
 * variable, and gives the game's PID so the broker can verify it. The broker exits when the pipe closes or when it
 * receives BRIDGE_SHUTDOWN.
 *
 * Phase 2: the channel (pipe, ring, PID check) and a minimal dispatch loop. The full generated dispatch table and
 * allow-list are phase 3 (SdkShimGen steam-bridge). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bridge_channel.h"

/* --- Valve's DLL (loaded at runtime) --- */

typedef int  (__cdecl *PFN_SteamAPI_Init)(void);
typedef void (__cdecl *PFN_SteamAPI_Shutdown)(void);
typedef int  (__cdecl *PFN_SteamAPI_RestartAppIfNecessary)(uint32_t appId);
typedef void (__cdecl *PFN_SteamAPI_ManualDispatch_Init)(void);
typedef void (__cdecl *PFN_SteamAPI_ManualDispatch_RunFrame)(int hPipe);
typedef int  (__cdecl *PFN_SteamAPI_ManualDispatch_GetNextCallback)(int hPipe, void* msg);
typedef void (__cdecl *PFN_SteamAPI_ManualDispatch_FreeLastCallback)(int hPipe);
typedef int  (__cdecl *PFN_SteamAPI_ManualDispatch_GetAPICallResult)(int hPipe, uint64_t call, void* buf, int bufSize, int expected, int* failed);
typedef int  (__cdecl *PFN_SteamAPI_GetHSteamPipe)(void);
typedef int  (__cdecl *PFN_SteamAPI_IsSteamRunning)(void);

static struct {
    HMODULE module;
    PFN_SteamAPI_Init                           Init;
    PFN_SteamAPI_Shutdown                       Shutdown;
    PFN_SteamAPI_RestartAppIfNecessary          RestartAppIfNecessary;
    PFN_SteamAPI_ManualDispatch_Init            ManualDispatch_Init;
    PFN_SteamAPI_ManualDispatch_RunFrame        ManualDispatch_RunFrame;
    PFN_SteamAPI_ManualDispatch_GetNextCallback ManualDispatch_GetNextCallback;
    PFN_SteamAPI_ManualDispatch_FreeLastCallback ManualDispatch_FreeLastCallback;
    PFN_SteamAPI_ManualDispatch_GetAPICallResult ManualDispatch_GetAPICallResult;
    PFN_SteamAPI_GetHSteamPipe                  GetHSteamPipe;
    PFN_SteamAPI_IsSteamRunning                 IsSteamRunning;
    int hPipe;
} g_steam;

static int LoadValveDll(const char* path)
{
    g_steam.module = LoadLibraryA(path);
    if (!g_steam.module) return 0;
#define LOAD(name) g_steam.name = (PFN_SteamAPI_##name)GetProcAddress(g_steam.module, "SteamAPI_" #name); if (!g_steam.name) return 0
    LOAD(Init);
    LOAD(Shutdown);
    LOAD(RestartAppIfNecessary);
    LOAD(ManualDispatch_Init);
    LOAD(ManualDispatch_RunFrame);
    LOAD(ManualDispatch_GetNextCallback);
    LOAD(ManualDispatch_FreeLastCallback);
    LOAD(ManualDispatch_GetAPICallResult);
    LOAD(GetHSteamPipe);
    LOAD(IsSteamRunning);
#undef LOAD
    return 1;
}

static int InitSteam(void)
{
    g_steam.RestartAppIfNecessary(553850);
    if (!g_steam.Init()) return 0;
    g_steam.ManualDispatch_Init();
    g_steam.hPipe = g_steam.GetHSteamPipe();
    return g_steam.hPipe != 0;
}

/* --- Phase 2 dispatch: a handful of calls for testing the channel --- */

/* CallbackMsg_t from isteamclient.h (the struct ManualDispatch_GetNextCallback fills). */
#pragma pack(push, 8)
typedef struct CallbackMsg {
    int32_t  steamUser;
    int32_t  callbackId;
    uint8_t* data;
    int32_t  dataSize;
} CallbackMsg;
#pragma pack(pop)

enum {
    BROKER_CALL_IS_STEAM_RUNNING = 0,
    BROKER_CALL_RUN_FRAME        = 1,
    BROKER_CALL_SHUTDOWN         = 2,
};

/* Pump Valve's callbacks into the ring. */
static void PumpCallbacks(BrokerChannel* ch)
{
    if (!g_steam.module) return;
    g_steam.ManualDispatch_RunFrame(g_steam.hPipe);
    CallbackMsg msg;
    while (g_steam.ManualDispatch_GetNextCallback(g_steam.hPipe, &msg))
    {
        BrokerChannelPushCallback(ch, (uint32_t)msg.callbackId, msg.data, msg.dataSize > 0 ? (uint32_t)msg.dataSize : 0);
        g_steam.ManualDispatch_FreeLastCallback(g_steam.hPipe);
    }
}

static int HandleCall(BrokerChannel* ch, uint32_t callId, const uint8_t* args, uint32_t argsSize,
                      uint8_t* result, uint32_t resultCapacity, uint32_t* resultSize, uint32_t* status)
{
    *resultSize = 0;
    *status = 0;
    switch (callId)
    {
    case BROKER_CALL_IS_STEAM_RUNNING:
    {
        uint8_t running = g_steam.module ? (uint8_t)g_steam.IsSteamRunning() : 1;
        if (resultCapacity >= 1) { result[0] = running; *resultSize = 1; }
        return 1;
    }
    case BROKER_CALL_RUN_FRAME:
        PumpCallbacks(ch);
        return 1;
    case BROKER_CALL_SHUTDOWN:
        return 0;
    default:
        *status = 1;
        return 1;
    }
}

/* --- Main loop --- */

static void Serve(BrokerChannel* ch)
{
    uint8_t body[BRIDGE_MSG_BUF];
    for (;;)
    {
        uint32_t bodySize = 0;
        uint32_t type = BridgePipeRecv(ch->pipe, body, sizeof body, &bodySize);
        if (type == 0 || type == BRIDGE_SHUTDOWN) break;

        if (type == BRIDGE_CALL && bodySize >= 4)
        {
            uint32_t callId;
            memcpy(&callId, body, 4);
            const uint8_t* args = body + 4;
            uint32_t argsSize = bodySize - 4;

            uint8_t resultBuf[4096];
            uint32_t resultSize = 0, status = 0;
            int cont = HandleCall(ch, callId, args, argsSize, resultBuf, sizeof resultBuf, &resultSize, &status);

            uint8_t reply[4096 + 8];
            uint32_t replyCallId = callId;
            memcpy(reply, &replyCallId, 4);
            memcpy(reply + 4, &status, 4);
            if (resultSize > 0) memcpy(reply + 8, resultBuf, resultSize);
            BridgePipeSend(ch->pipe, status == 0 ? BRIDGE_RESULT : (uint32_t)BRIDGE_RESULT, reply, 8 + resultSize);

            if (!cont) break;
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: hellforge_steam_broker <pipe-suffix> <expected-pid> [valve-dll-path]\n");
        return 1;
    }

    wchar_t suffix[64];
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, suffix, 64);
    DWORD expectedPid = (DWORD)strtoul(argv[2], NULL, 10);

    if (argc >= 4 && argv[3][0] != '\0')
    {
        if (!LoadValveDll(argv[3])) { fprintf(stderr, "broker: cannot load %s\n", argv[3]); return 1; }
        if (!InitSteam()) { fprintf(stderr, "broker: SteamAPI_Init failed\n"); return 1; }
        fprintf(stderr, "broker: Valve DLL loaded, pipe %d\n", g_steam.hPipe);
    }

    BrokerChannel ch;
    if (!BrokerChannelCreate(&ch, suffix)) { fprintf(stderr, "broker: cannot create channel\n"); return 1; }
    fprintf(stderr, "broker: waiting for client on %ls\n", ch.pipeName);

    if (!BrokerChannelAccept(&ch, expectedPid)) { fprintf(stderr, "broker: client rejected\n"); BrokerChannelClose(&ch); return 1; }
    fprintf(stderr, "broker: client %lu accepted\n", ch.clientPid);

    Serve(&ch);
    fprintf(stderr, "broker: shutting down\n");

    BrokerChannelClose(&ch);
    if (g_steam.module) { g_steam.Shutdown(); FreeLibrary(g_steam.module); }
    return 0;
}

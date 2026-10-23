/* The broker's Steam side of the bridge (broker_bridge.h). Every Steam call here runs on the broker's one serving
 * thread: BrokerServe calls the pump between the game's calls. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "bridge_gen.h"
#include "broker_bridge.h"

#define STEAM_API_CALL_COMPLETED 703
#define LOBBY_CREATED 513
#define RESULT_OK 1

/* CallbackMsg_t (steam_api_common.h), what ManualDispatch_GetNextCallback fills. */
#pragma pack(push, 8)
typedef struct CallbackMsg { int32_t steamUser; int32_t callbackId; uint8_t* data; int32_t dataSize; } CallbackMsg;
#pragma pack(pop)

typedef void (*RunFrameFn)(int hPipe);
typedef bool (*NextFn)(int hPipe, CallbackMsg* msg);
typedef void (*FreeFn)(int hPipe);
typedef bool (*CallResultFn)(int hPipe, uint64_t call, void* data, int size, int expectedId, bool* failed);
typedef int (*IsRunningFn)(void);

static BrokerBridge* Of(const BrokerApi* api) { return (BrokerBridge*)api->context; }

static void* Resolve(const BrokerApi* api, const char* flat) { return (void*)GetProcAddress(Of(api)->valve, flat); }

/* The accessors of the interfaces the table bridges, each looked up and called once. */
static void* Self(const BrokerApi* api, const char* accessor)
{
    BrokerBridge* b = Of(api);
    for (int i = 0; i < 16 && b->selves[i].name; i++) if (strcmp(b->selves[i].name, accessor) == 0) return b->selves[i].object;
    typedef void* (*AccessorFn)(void);
    AccessorFn fn = (AccessorFn)(void*)GetProcAddress(b->valve, accessor);
    void* object = fn ? fn() : NULL;
    for (int i = 0; i < 16; i++) if (!b->selves[i].name) { b->selves[i].name = accessor; b->selves[i].object = object; break; }
    return object;
}

static int OwnLobby(const BrokerApi* api, uint64_t lobby)
{
    BrokerBridge* b = Of(api);
    for (int i = 0; i < b->lobbyCount; i++) if (b->lobbies[i] == lobby) return 1;
    return 0;
}

static uint32_t Refuse(const BrokerApi* api, const char* why)
{
    Of(api)->refusals++;
    fprintf(stderr, "broker: refused %s\n", why);
    return BRIDGE_STATUS_REFUSED;
}

static void LogValue(const BrokerApi* api, const char* what, const char* value)
{
    (void)api;
    fprintf(stderr, "broker: %s = %.64s\n", what, value ? value : "(null)");
}

static bool Allowed(int id)
{
    for (int i = 0; i < BRIDGE_CALLBACK_COUNT; i++) if (g_bridgeCallbacks[i] == id) return true;
    return false;
}

/* SteamAPICallCompleted_t: the call, the result's callback id and size. Its result goes to the game behind the call-result
 * header; a LobbyCreated_t that succeeded names a lobby the game may then use. */
static void CallCompleted(BrokerBridge* b, BrokerChannel* ch, const uint8_t* data, int32_t size)
{
    if (size < 16) return;
    uint64_t call;
    int32_t id;
    uint32_t resultSize;
    memcpy(&call, data, 8);
    memcpy(&id, data + 8, 4);
    memcpy(&resultSize, data + 12, 4);
    uint8_t slot[BRIDGE_RING_DATA_SIZE];
    if (resultSize > sizeof slot - BRIDGE_CALL_RESULT_HEADER || !Allowed(id))
    {
        fprintf(stderr, "broker: call result %d (%u bytes) not passed on\n", id, resultSize);
        return;
    }
    CallResultFn result = (CallResultFn)(void*)GetProcAddress(b->valve, "SteamAPI_ManualDispatch_GetAPICallResult");
    bool failed = false;
    memset(slot, 0, BRIDGE_CALL_RESULT_HEADER + resultSize);
    if (!result || !result(b->hPipe, call, slot + BRIDGE_CALL_RESULT_HEADER, (int)resultSize, id, &failed)) failed = true;
    memcpy(slot, &call, 8);
    slot[8] = failed ? 1 : 0;
    if (id == LOBBY_CREATED && !failed && resultSize >= 16 && b->lobbyCount < BROKER_MAX_LOBBIES)
    {
        int32_t eResult;
        uint64_t lobby;
        memcpy(&eResult, slot + BRIDGE_CALL_RESULT_HEADER, 4);
        memcpy(&lobby, slot + BRIDGE_CALL_RESULT_HEADER + 8, 8);
        if (eResult == RESULT_OK && lobby) b->lobbies[b->lobbyCount++] = lobby;
    }
    BrokerChannelPushCallback(ch, (uint32_t)id | BRIDGE_CALL_RESULT_FLAG, slot, BRIDGE_CALL_RESULT_HEADER + resultSize);
}

static void Pump(void* context, BrokerChannel* ch)
{
    BrokerBridge* b = (BrokerBridge*)context;
    NextFn next = (NextFn)b->next;
    b->runFrame(b->hPipe);
    CallbackMsg msg;
    static uint32_t reportedDrops;
    while (next(b->hPipe, &msg))
    {
        uint32_t size = msg.dataSize > 0 ? (uint32_t)msg.dataSize : 0;
        if (msg.callbackId == STEAM_API_CALL_COMPLETED) CallCompleted(b, ch, msg.data, msg.dataSize);
        else if (Allowed(msg.callbackId) && !BrokerChannelPushCallback(ch, (uint32_t)msg.callbackId, msg.data, size) && size > BRIDGE_RING_DATA_SIZE)
            fprintf(stderr, "broker: callback %d of %u bytes does not fit a ring slot; dropped\n", msg.callbackId, size);
        b->freeLast(b->hPipe);
    }
    if (ch->dropped > 0 && reportedDrops == 0) { fprintf(stderr, "broker: the ring is full; callbacks are being dropped\n"); reportedDrops = 1; }
    DWORD now = GetTickCount();
    if (!b->snapshotDone || now - b->lastSnapshot >= BROKER_SNAPSHOT_MS) BrokerBridgeSnapshot(b, ch);
}

static int IsRunning(void* context)
{
    IsRunningFn fn = (IsRunningFn)(void*)GetProcAddress(((BrokerBridge*)context)->valve, "SteamAPI_IsSteamRunning");
    return fn ? fn() : 0;
}

#define CALL(type, name, ...) ((type)(void*)GetProcAddress(b->valve, name))(__VA_ARGS__)
#define HAVE(name) (GetProcAddress(b->valve, name) != NULL)

static void Put(char* to, size_t capacity, const char* from)
{
    if (!from) { to[0] = 0; return; }
    size_t n = strnlen(from, capacity - 1);
    memcpy(to, from, n);
    to[n] = 0;
}

void BrokerBridgeSnapshot(BrokerBridge* b, BrokerChannel* ch)
{
    BridgeSnapshot* s = BridgeSnapshotOf(ch->ring);
    BridgeSnapshot next;
    memset(&next, 0, sizeof next);
    void* user = Self(&b->api, "SteamAPI_SteamUser_v021");
    void* friends = Self(&b->api, "SteamAPI_SteamFriends_v017");
    void* utils = Self(&b->api, "SteamAPI_SteamUtils_v010");
    void* apps = Self(&b->api, "SteamAPI_SteamApps_v008");
    if (user && HAVE("SteamAPI_ISteamUser_BLoggedOn")) next.loggedOn = CALL(bool (*)(void*), "SteamAPI_ISteamUser_BLoggedOn", user) ? 1 : 0;
    if (user && HAVE("SteamAPI_ISteamUser_GetSteamID")) next.steamId = CALL(uint64_t (*)(void*), "SteamAPI_ISteamUser_GetSteamID", user);
    if (friends && HAVE("SteamAPI_ISteamFriends_GetPersonaName"))
        Put(next.personaName, sizeof next.personaName, CALL(const char* (*)(void*), "SteamAPI_ISteamFriends_GetPersonaName", friends));
    if (utils && HAVE("SteamAPI_ISteamUtils_GetIPCountry"))
        Put(next.ipCountry, sizeof next.ipCountry, CALL(const char* (*)(void*), "SteamAPI_ISteamUtils_GetIPCountry", utils));
    if (utils && HAVE("SteamAPI_ISteamUtils_IsSteamRunningOnSteamDeck"))
        next.steamDeck = CALL(bool (*)(void*), "SteamAPI_ISteamUtils_IsSteamRunningOnSteamDeck", utils) ? 1 : 0;
    if (apps && HAVE("SteamAPI_ISteamApps_GetCurrentGameLanguage"))
        Put(next.language, sizeof next.language, CALL(const char* (*)(void*), "SteamAPI_ISteamApps_GetCurrentGameLanguage", apps));
    if (apps && HAVE("SteamAPI_ISteamApps_GetCurrentBetaName"))
        next.hasBeta = CALL(bool (*)(void*, char*, int), "SteamAPI_ISteamApps_GetCurrentBetaName", apps, next.betaName, (int)sizeof next.betaName - 1) ? 1 : 0;
    if (apps && HAVE("SteamAPI_ISteamApps_GetDLCCount") && HAVE("SteamAPI_ISteamApps_BGetDLCDataByIndex"))
    {
        int count = CALL(int (*)(void*), "SteamAPI_ISteamApps_GetDLCCount", apps);
        for (int i = 0; i < count && next.dlcCount < BRIDGE_DLC_MAX; i++)
        {
            BridgeSnapshotDlc* d = &next.dlc[next.dlcCount];
            bool available = false;
            if (!CALL(bool (*)(void*, int, uint32_t*, bool*, char*, int), "SteamAPI_ISteamApps_BGetDLCDataByIndex", apps, i, &d->appId, &available,
                d->name, (int)sizeof d->name - 1)) continue;
            d->name[sizeof d->name - 1] = 0;
            d->available = available ? 1 : 0;
            d->subscribed = HAVE("SteamAPI_ISteamApps_BIsSubscribedApp") &&
                CALL(bool (*)(void*, uint32_t), "SteamAPI_ISteamApps_BIsSubscribedApp", apps, d->appId) ? 1 : 0;
            next.dlcCount++;
        }
    }
    next.betaName[sizeof next.betaName - 1] = 0;
    /* the seqlock: odd while written */
    uint32_t version = s->version;
    __atomic_store_n(&s->version, version | 1, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    memcpy((uint8_t*)s + sizeof s->version, (uint8_t*)&next + sizeof next.version, sizeof next - sizeof next.version);
    s->ready = 1;
    __atomic_store_n(&s->version, (version | 1) + 1, __ATOMIC_RELEASE);
    b->lastSnapshot = GetTickCount();
    b->snapshotDone = 1;
}

int BrokerBridgeOpen(BrokerBridge* b, HMODULE valve, int hPipe)
{
    memset(b, 0, sizeof *b);
    b->valve = valve;
    b->hPipe = hPipe;
    if (!valve) return 0;
    b->runFrame = (RunFrameFn)(void*)GetProcAddress(valve, "SteamAPI_ManualDispatch_RunFrame");
    b->next = (void*)GetProcAddress(valve, "SteamAPI_ManualDispatch_GetNextCallback");
    b->freeLast = (FreeFn)(void*)GetProcAddress(valve, "SteamAPI_ManualDispatch_FreeLastCallback");
    if (!b->runFrame || !b->next || !b->freeLast) return 0;
    b->api = (BrokerApi){ b, Resolve, Self, OwnLobby, Refuse, LogValue, BrokerDispatchMethod };
    b->steam = (BrokerSteam){ b, Pump, IsRunning, &b->api };
    return 1;
}

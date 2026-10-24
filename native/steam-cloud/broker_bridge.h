/* The broker's Steam side of the bridge (docs/steam-bridge.md, phase 3): Valve's DLL behind the generated dispatch
 * (BrokerApi), manual dispatch into the ring with only the allow-list's callbacks, call results, the lobbies the game
 * made, and the snapshot. Separate from main so tests drive it with a fake Steam DLL. */
#pragma once
#include "bridge_broker.h"
#include "broker_dispatch.h"

#define BROKER_SNAPSHOT_MS 1000
#define BROKER_MAX_LOBBIES 16
/* how often the settings file's time stamp is looked at, as the backend does (StrangerSetting.Recheck) */
#define BROKER_STRANGERS_MS 2000
#define BROKER_PENDING_LOBBIES 4

typedef struct BrokerBridge {
    HMODULE valve;
    int hPipe;
    BrokerApi api;
    BrokerSteam steam;
    uint64_t lobbies[BROKER_MAX_LOBBIES];     /* the lobbies the game made or entered (RememberEntered) */
    int32_t lobbyAsked[BROKER_MAX_LOBBIES];   /* the lobby type the game asked for each; -1 for one it entered */
    int lobbyCount;
    /* CreateLobby's types asked for, oldest first, until their LobbyCreated_t (the calls run in order) */
    int32_t pendingAsked[BROKER_PENDING_LOBBIES];
    int pendingCount;
    /* Play with strangers (mods/hellforge-mods.json "playWithStrangers", off without the file): the mods folder, the
     * settings file's time stamp when read, when it was last looked at */
    wchar_t modsFolder[520];
    int strangers;
    FILETIME strangersStamp;
    DWORD strangersChecked;
    DWORD lastSnapshot;
    int snapshotDone;
    uint32_t refusals;
    struct { const char* name; void* object; } selves[16];  /* the accessors' objects, each looked up once */
    void (*runFrame)(int hPipe);
    void* next;                 /* SteamAPI_ManualDispatch_GetNextCallback */
    void (*freeLast)(int hPipe);
} BrokerBridge;

/* Fills b for a loaded and initialized Valve DLL (or a fake) and its Steam pipe. Returns 0 when manual dispatch's
 * exports are missing. */
int BrokerBridgeOpen(BrokerBridge* b, HMODULE valve, int hPipe);

/* Reads Play with strangers from that mods folder now, and again whenever its settings file changes (the pump looks
 * every BROKER_STRANGERS_MS). Without it, strangers are off. Call before serving. */
void BrokerBridgeWatchStrangers(BrokerBridge* b, const wchar_t* modsFolder);

/* Play with strangers in a settings file's text: a top-level "playWithStrangers": true. */
int BrokerStrangersIn(const char* json, size_t length);

/* Fills the snapshot in ch's mapping now (the pump also does it every BROKER_SNAPSHOT_MS). */
void BrokerBridgeSnapshot(BrokerBridge* b, BrokerChannel* ch);

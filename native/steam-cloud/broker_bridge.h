/* The broker's Steam side of the bridge (docs/steam-bridge.md, phase 3): Valve's DLL behind the generated dispatch
 * (BrokerApi), manual dispatch into the ring with only the allow-list's callbacks, call results, the lobbies the game
 * made, and the snapshot. Separate from main so tests drive it with a fake Steam DLL. */
#pragma once
#include "bridge_broker.h"
#include "broker_dispatch.h"

#define BROKER_SNAPSHOT_MS 1000
#define BROKER_MAX_LOBBIES 16

typedef struct BrokerBridge {
    HMODULE valve;
    int hPipe;
    BrokerApi api;
    BrokerSteam steam;
    uint64_t lobbies[BROKER_MAX_LOBBIES];
    int lobbyCount;
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

/* Fills the snapshot in ch's mapping now (the pump also does it every BROKER_SNAPSHOT_MS). */
void BrokerBridgeSnapshot(BrokerBridge* b, BrokerChannel* ch);

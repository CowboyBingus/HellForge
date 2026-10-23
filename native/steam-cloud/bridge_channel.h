/* The Steam bridge channel (docs/steam-bridge.md): a named pipe (duplex, message mode) and a callback ring (shared
 * memory), both with a low mandatory label so the game at low integrity can use them. The broker creates them; the game
 * connects to them. */
#pragma once
#include <windows.h>
#include "bridge_protocol.h"

/* --- Broker (server) side --- */

typedef struct BrokerChannel {
    HANDLE pipe;
    HANDLE ringMapping;
    BridgeRingHeader* ring;
    DWORD clientPid;
    wchar_t pipeName[128];
    wchar_t ringName[128];
} BrokerChannel;

int  BrokerChannelCreate(BrokerChannel* ch, const wchar_t* suffix);
int  BrokerChannelAccept(BrokerChannel* ch, DWORD expectedPid);
void BrokerChannelClose(BrokerChannel* ch);

/* Push a callback into the ring. Returns 0 when the ring is full or the data exceeds a slot. */
int BrokerChannelPushCallback(BrokerChannel* ch, uint32_t callbackId, const void* data, uint32_t dataSize);

/* --- Game (client) side --- */

typedef struct GameChannel {
    HANDLE pipe;
    HANDLE ringMapping;
    BridgeRingHeader* ring;
} GameChannel;

int  GameChannelConnect(GameChannel* ch, const wchar_t* suffix);
int  GameChannelHello(GameChannel* ch);
void GameChannelClose(GameChannel* ch);

/* Read the next callback from the ring. Returns 0 when the ring is empty. */
int GameChannelReadCallback(GameChannel* ch, uint32_t* callbackId, void* data, uint32_t capacity, uint32_t* dataSize);

/* --- Pipe I/O (both sides) --- */

#define BRIDGE_MSG_BUF 8192

/* Receive one pipe message. Returns the type (> 0), or 0 on error. body receives the bytes after the type field;
 * *bodySize is the number of body bytes. */
uint32_t BridgePipeRecv(HANDLE pipe, void* body, uint32_t bodyCapacity, uint32_t* bodySize);

/* Send one pipe message: [uint32 type][body]. */
int BridgePipeSend(HANDLE pipe, uint32_t type, const void* body, uint32_t bodySize);

/* The Steam bridge wire protocol (docs/steam-bridge.md): pipe messages and the callback ring, shared between the broker
 * (hellforge_steam_broker.exe, normal integrity) and the game's stand-in (steam_api64.dll, low integrity). */
#pragma once
#include <stdint.h>

/* --- Pipe messages (message-mode named pipe, one WriteFile per message) ---
 *
 * Each message: [uint32 type][body...], where the body's length is the pipe message's size minus 4. The pipe is in
 * message mode, so ReadFile returns one whole message. */

enum {
    BRIDGE_HELLO        = 1,  /* client -> broker: uint32 pid */
    BRIDGE_HELLO_OK     = 2,  /* broker -> client: uint64 ring section handle, already duplicated into the client */
    BRIDGE_HELLO_DENIED = 3,  /* broker -> client: empty, then the broker disconnects and waits for the next client */
    BRIDGE_CALL         = 4,  /* client -> broker: uint32 seq, uint32 function, then argument bytes */
    BRIDGE_RESULT       = 5,  /* broker -> client: uint32 seq (the call's), uint32 status, then result bytes */
    BRIDGE_SHUTDOWN     = 6,  /* client -> broker */
};

/* Functions a BRIDGE_CALL names. Phase 3's generated proxies add the interface methods. */
enum {
    BRIDGE_FN_IS_STEAM_RUNNING = 0,  /* result: uint8 */
};

/* A BRIDGE_RESULT's status. */
enum {
    BRIDGE_STATUS_OK        = 0,
    BRIDGE_STATUS_UNKNOWN   = 1,  /* no such function, or refused by the allow-list */
    BRIDGE_STATUS_MALFORMED = 2,  /* too short, or larger than BRIDGE_MSG_BUF */
};

/* --- Callback ring (shared memory, single-producer single-consumer) ---
 *
 * The broker writes callbacks; the game reads them in RunCallbacks. An idle frame reads writeSeq only (no system call).
 * Fixed-size slots: the largest callback the game registers is 712 bytes (SteamNetConnectionStatusChanged).
 *
 * The game can write the whole mapping, so neither side indexes with a value the other wrote without bounding it: each
 * keeps its own sequence privately and only publishes it here, and a slot index is always seq % BRIDGE_RING_SLOT_COUNT
 * of an unsigned sequence. */

#define BRIDGE_RING_SLOT_SIZE   1024
#define BRIDGE_RING_SLOT_COUNT  60u
#define BRIDGE_RING_DATA_SIZE   (BRIDGE_RING_SLOT_SIZE - 8)

typedef struct BridgeRingSlot {
    uint32_t callbackId;
    uint32_t dataSize;
    uint8_t  data[BRIDGE_RING_DATA_SIZE];
} BridgeRingSlot;

typedef struct BridgeRingHeader {
    volatile uint64_t writeSeq;  /* published by the broker */
    uint8_t _pad1[56];
    volatile uint64_t readSeq;   /* published by the game */
    uint8_t _pad2[56];
} BridgeRingHeader;

#define BRIDGE_RING_MAP_SIZE (sizeof(BridgeRingHeader) + (BRIDGE_RING_SLOT_COUNT) * sizeof(BridgeRingSlot))

static inline BridgeRingSlot* BridgeRingSlotAt(BridgeRingHeader* ring, uint64_t seq)
{
    return (BridgeRingSlot*)((uint8_t*)(ring + 1) + (size_t)(seq % BRIDGE_RING_SLOT_COUNT) * sizeof(BridgeRingSlot));
}

/* The game's published read position as the broker may use it: never past what the broker wrote, never more than a
 * ring behind it. */
static inline uint64_t BridgeRingClampRead(uint64_t published, uint64_t written)
{
    if (published > written) return written;
    if (written - published > BRIDGE_RING_SLOT_COUNT) return written - BRIDGE_RING_SLOT_COUNT;
    return published;
}

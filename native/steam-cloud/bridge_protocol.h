/* The Steam bridge wire protocol (docs/steam-bridge.md): pipe messages and the callback ring, shared between the broker
 * (hellforge_steam_broker.exe, normal integrity) and the game's stand-in (steam_api64.dll, low integrity). */
#pragma once
#include <stdint.h>

/* --- Pipe messages (message-mode named pipe, one WriteFile per message) ---
 *
 * Each message: [uint32 type][body...], where the body's length is the pipe message's size minus 4. The pipe is in
 * message mode, so ReadFile returns one whole message. */

enum {
    BRIDGE_HELLO        = 1,  /* client -> broker: body is uint32_t pid */
    BRIDGE_HELLO_OK     = 2,  /* broker -> client: empty */
    BRIDGE_HELLO_DENIED = 3,  /* broker -> client: empty, then close */
    BRIDGE_CALL         = 4,  /* client -> broker: uint32 callId, then argument bytes */
    BRIDGE_RESULT       = 5,  /* broker -> client: uint32 callId, uint32 status, then result bytes */
    BRIDGE_SHUTDOWN     = 6,  /* either side */
};

/* --- Callback ring (shared memory, single-producer single-consumer) ---
 *
 * The broker writes callbacks; the game reads them in RunCallbacks. An idle frame reads writeSeq only (no system call).
 * Fixed-size slots: the largest callback the game registers is 712 bytes (SteamNetConnectionStatusChanged). */

#define BRIDGE_RING_SLOT_SIZE   1024
#define BRIDGE_RING_SLOT_COUNT  60

typedef struct BridgeRingSlot {
    uint32_t callbackId;
    uint32_t dataSize;
    uint8_t  data[BRIDGE_RING_SLOT_SIZE - 8];
} BridgeRingSlot;

typedef struct BridgeRingHeader {
    volatile int64_t writeSeq;
    uint8_t _pad1[56];
    volatile int64_t readSeq;
    uint8_t _pad2[56];
} BridgeRingHeader;

#define BRIDGE_RING_MAP_SIZE (sizeof(BridgeRingHeader) + (BRIDGE_RING_SLOT_COUNT) * sizeof(BridgeRingSlot))

static inline BridgeRingSlot* BridgeRingSlotAt(BridgeRingHeader* ring, int64_t seq)
{
    return (BridgeRingSlot*)((uint8_t*)(ring + 1) + (seq % BRIDGE_RING_SLOT_COUNT) * sizeof(BridgeRingSlot));
}

static inline int BridgeRingCanWrite(BridgeRingHeader* ring)
{
    return (ring->writeSeq - ring->readSeq) < BRIDGE_RING_SLOT_COUNT;
}

static inline int BridgeRingCanRead(BridgeRingHeader* ring)
{
    return ring->writeSeq > ring->readSeq;
}

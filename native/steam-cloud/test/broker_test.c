/* Tests for the Steam bridge channel (bridge_channel.c): the pipe, the ring, the PID check, and message round trips.
 * Two threads: the broker side creates the channel and serves; the game side connects and makes calls. No Valve DLL
 * is needed: the test drives both ends from one process.
 *
 * Usage: broker_test.exe
 * Exit 0 on success, 1 on failure. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../bridge_channel.h"

/* ---- Ring tests (in-process, no shared memory needed) ---- */

static void TestRingBasic(void)
{
    /* Allocate a ring in local memory. */
    uint8_t* mem = (uint8_t*)VirtualAlloc(NULL, BRIDGE_RING_MAP_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(mem);
    BridgeRingHeader* ring = (BridgeRingHeader*)mem;
    memset(ring, 0, BRIDGE_RING_MAP_SIZE);

    assert(!BridgeRingCanRead(ring));
    assert(BridgeRingCanWrite(ring));

    /* Write one callback. */
    uint8_t payload[] = { 0x11, 0x22, 0x33, 0x44 };
    BridgeRingSlot* slot = BridgeRingSlotAt(ring, ring->writeSeq);
    slot->callbackId = 1101;
    slot->dataSize = sizeof payload;
    memcpy(slot->data, payload, sizeof payload);
    MemoryBarrier();
    ring->writeSeq++;

    assert(BridgeRingCanRead(ring));

    /* Read it. */
    MemoryBarrier();
    BridgeRingSlot* rslot = BridgeRingSlotAt(ring, ring->readSeq);
    if (rslot->callbackId != 1101) { printf("FAIL: readback id %u\n", rslot->callbackId); exit(1); }
    if (rslot->dataSize != 4) { printf("FAIL: readback size %u\n", rslot->dataSize); exit(1); }
    if (memcmp(rslot->data, payload, 4) != 0) { puts("FAIL: readback data"); exit(1); }
    ring->readSeq++;

    assert(!BridgeRingCanRead(ring));

    /* Fill the ring completely. */
    for (int i = 0; i < BRIDGE_RING_SLOT_COUNT; i++)
    {
        assert(BridgeRingCanWrite(ring));
        BridgeRingSlot* s = BridgeRingSlotAt(ring, ring->writeSeq);
        s->callbackId = (uint32_t)(2000 + i);
        s->dataSize = 0;
        MemoryBarrier();
        ring->writeSeq++;
    }
    assert(!BridgeRingCanWrite(ring));

    /* Drain half. */
    for (int i = 0; i < BRIDGE_RING_SLOT_COUNT / 2; i++)
    {
        if (!BridgeRingCanRead(ring)) { puts("FAIL: ring should be readable"); exit(1); }
        BridgeRingSlot* ds = BridgeRingSlotAt(ring, ring->readSeq);
        if (ds->callbackId != (uint32_t)(2000 + i)) { printf("FAIL: expected %d, got %u\n", 2000+i, ds->callbackId); exit(1); }
        ring->readSeq++;
    }
    assert(BridgeRingCanWrite(ring));
    assert(BridgeRingCanRead(ring));

    VirtualFree(mem, 0, MEM_RELEASE);
    puts("PASS ring: write, read, full, drain");
}

/* ---- Pipe channel tests (two threads) ---- */

typedef struct ServerArgs {
    const wchar_t* suffix;
    DWORD expectedPid;
    HANDLE readyEvent;
    int accepted;
    int callsHandled;
    int callbacksPushed;
} ServerArgs;

static DWORD WINAPI ServerThread(void* param)
{
    ServerArgs* a = (ServerArgs*)param;
    BrokerChannel ch;
    if (!BrokerChannelCreate(&ch, a->suffix)) { SetEvent(a->readyEvent); return 1; }
    SetEvent(a->readyEvent);

    a->accepted = BrokerChannelAccept(&ch, a->expectedPid);
    if (!a->accepted) { BrokerChannelClose(&ch); return 0; }

    /* Handle calls until SHUTDOWN. */
    uint8_t body[4096];
    for (;;)
    {
        uint32_t bodySize = 0;
        uint32_t type = BridgePipeRecv(ch.pipe, body, sizeof body, &bodySize);
        if (type == 0 || type == BRIDGE_SHUTDOWN) break;

        if (type == BRIDGE_CALL && bodySize >= 4)
        {
            uint32_t callId;
            memcpy(&callId, body, 4);

            /* Echo the callId and a fixed reply. */
            uint8_t reply[12];
            memcpy(reply, &callId, 4);
            uint32_t status = 0;
            memcpy(reply + 4, &status, 4);
            uint8_t answer = 42;
            memcpy(reply + 8, &answer, 1);
            BridgePipeSend(ch.pipe, BRIDGE_RESULT, reply, 9);
            a->callsHandled++;
        }
    }

    /* Push some callbacks before closing. */
    uint8_t cbData[] = { 0xAA, 0xBB };
    for (int i = 0; i < 3; i++)
    {
        cbData[0] = (uint8_t)(0xA0 + i);
        if (BrokerChannelPushCallback(&ch, (uint32_t)(500 + i), cbData, sizeof cbData))
            a->callbacksPushed++;
    }

    BrokerChannelClose(&ch);
    return 0;
}

static void TestPipeChannelBasic(void)
{
    ServerArgs args;
    memset(&args, 0, sizeof args);
    args.suffix = L"test-basic";
    args.expectedPid = 0; /* accept anyone */
    args.readyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(args.readyEvent);

    HANDLE thread = CreateThread(NULL, 0, ServerThread, &args, 0, NULL);
    assert(thread);

    WaitForSingleObject(args.readyEvent, 5000);
    CloseHandle(args.readyEvent);

    GameChannel game;
    if (!GameChannelConnect(&game, L"test-basic")) { printf("FAIL: connect, err=%lu\n", GetLastError()); exit(1); }
    if (!GameChannelHello(&game)) { printf("FAIL: hello, err=%lu\n", GetLastError()); exit(1); }

    /* Make a call: send callId=7, expect reply with status=0 and one byte 42. */
    {
        uint32_t callId = 7;
        if (!BridgePipeSend(game.pipe, BRIDGE_CALL, &callId, sizeof callId)) { printf("FAIL: send call, pipe=%p err=%lu\n", (void*)game.pipe, GetLastError()); exit(1); }

        uint8_t reply[256];
        uint32_t replySize = 0;
        uint32_t rtype = BridgePipeRecv(game.pipe, reply, sizeof reply, &replySize);
        if (rtype != BRIDGE_RESULT) { printf("FAIL: expected RESULT, got %u\n", rtype); exit(1); }
        if (replySize < 9) { printf("FAIL: reply too short: %u\n", replySize); exit(1); }
        uint32_t replyCallId, replyStatus;
        memcpy(&replyCallId, reply, 4);
        memcpy(&replyStatus, reply + 4, 4);
        if (replyCallId != 7) { printf("FAIL: callId %u\n", replyCallId); exit(1); }
        if (replyStatus != 0) { printf("FAIL: status %u\n", replyStatus); exit(1); }
        if (reply[8] != 42) { printf("FAIL: answer %u\n", reply[8]); exit(1); }
    }

    /* Send SHUTDOWN so the server exits its loop. */
    BridgePipeSend(game.pipe, BRIDGE_SHUTDOWN, NULL, 0);

    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);

    /* Read callbacks from the ring. */
    for (int i = 0; i < 3; i++)
    {
        uint32_t cbId = 0;
        uint8_t cbData[256];
        uint32_t cbSize = 0;
        if (!GameChannelReadCallback(&game, &cbId, cbData, sizeof cbData, &cbSize)) { printf("FAIL: no callback %d\n", i); exit(1); }
        if (cbId != (uint32_t)(500 + i)) { printf("FAIL: cbId %u expected %d\n", cbId, 500+i); exit(1); }
        if (cbSize != 2) { printf("FAIL: cbSize %u\n", cbSize); exit(1); }
        if (cbData[0] != (uint8_t)(0xA0 + i) || cbData[1] != 0xBB) { puts("FAIL: cbData"); exit(1); }
    }

    /* Ring should be empty now. */
    {
        uint32_t emptyId = 0, emptySize = 0;
        if (GameChannelReadCallback(&game, &emptyId, NULL, 0, &emptySize)) { puts("FAIL: ring not empty"); exit(1); }
    }

    GameChannelClose(&game);
    assert(args.accepted);
    assert(args.callsHandled == 1);
    assert(args.callbacksPushed == 3);
    puts("PASS pipe channel: connect, hello, call, callbacks");
}

/* ---- PID mismatch test ---- */

static void TestPidMismatch(void)
{
    ServerArgs args;
    memset(&args, 0, sizeof args);
    args.suffix = L"test-pid";
    args.expectedPid = 0xDEAD; /* our PID is not this */
    args.readyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(args.readyEvent);

    HANDLE thread = CreateThread(NULL, 0, ServerThread, &args, 0, NULL);
    assert(thread);

    WaitForSingleObject(args.readyEvent, 5000);
    CloseHandle(args.readyEvent);

    GameChannel game;
    if (!GameChannelConnect(&game, L"test-pid")) { puts("FAIL: connect"); exit(1); }
    if (GameChannelHello(&game)) { puts("FAIL: hello should be denied"); exit(1); }

    GameChannelClose(&game);
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    assert(!args.accepted);
    puts("PASS pid mismatch: connection denied");
}

/* ---- PID match test ---- */

static void TestPidMatch(void)
{
    DWORD myPid = GetCurrentProcessId();
    ServerArgs args;
    memset(&args, 0, sizeof args);
    args.suffix = L"test-pid-ok";
    args.expectedPid = myPid;
    args.readyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(args.readyEvent);

    HANDLE thread = CreateThread(NULL, 0, ServerThread, &args, 0, NULL);
    assert(thread);

    WaitForSingleObject(args.readyEvent, 5000);
    CloseHandle(args.readyEvent);

    GameChannel game;
    assert(GameChannelConnect(&game, L"test-pid-ok"));
    assert(GameChannelHello(&game));

    BridgePipeSend(game.pipe, BRIDGE_SHUTDOWN, NULL, 0);
    GameChannelClose(&game);
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    assert(args.accepted);
    puts("PASS pid match: connection accepted");
}

/* ---- Ring overflow test ---- */

static void TestRingOverflow(void)
{
    uint8_t* mem = (uint8_t*)VirtualAlloc(NULL, BRIDGE_RING_MAP_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(mem);
    BridgeRingHeader* ring = (BridgeRingHeader*)mem;
    memset(ring, 0, BRIDGE_RING_MAP_SIZE);

    /* Fill the ring. */
    for (int i = 0; i < BRIDGE_RING_SLOT_COUNT; i++)
    {
        BridgeRingSlot* s = BridgeRingSlotAt(ring, ring->writeSeq);
        s->callbackId = (uint32_t)i;
        s->dataSize = 0;
        MemoryBarrier();
        ring->writeSeq++;
    }

    /* The 61st write must fail. */
    assert(!BridgeRingCanWrite(ring));

    /* Read one to make room. */
    ring->readSeq++;
    assert(BridgeRingCanWrite(ring));

    /* Write one more. */
    BridgeRingSlot* s = BridgeRingSlotAt(ring, ring->writeSeq);
    s->callbackId = 9999;
    s->dataSize = 0;
    MemoryBarrier();
    ring->writeSeq++;
    assert(!BridgeRingCanWrite(ring));

    VirtualFree(mem, 0, MEM_RELEASE);
    puts("PASS ring overflow: full ring blocks, read frees a slot");
}

/* ---- Oversized callback test ---- */

static void TestOversizedCallback(void)
{
    uint8_t* mem = (uint8_t*)VirtualAlloc(NULL, BRIDGE_RING_MAP_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(mem);
    BridgeRingHeader* ring = (BridgeRingHeader*)mem;
    memset(ring, 0, BRIDGE_RING_MAP_SIZE);

    /* A callback whose data exceeds a slot. */
    BrokerChannel ch;
    memset(&ch, 0, sizeof ch);
    ch.ring = ring;
    uint8_t big[BRIDGE_RING_SLOT_SIZE];
    memset(big, 0xCC, sizeof big);
    if (BrokerChannelPushCallback(&ch, 1, big, BRIDGE_RING_SLOT_SIZE - 8 + 1)) { puts("FAIL: oversized accepted"); exit(1); }

    /* Exactly at the limit. */
    if (!BrokerChannelPushCallback(&ch, 1, big, BRIDGE_RING_SLOT_SIZE - 8)) { puts("FAIL: max-sized rejected"); exit(1); }

    VirtualFree(mem, 0, MEM_RELEASE);
    puts("PASS oversized callback: rejected when data exceeds slot");
}

int main(void)
{
    TestRingBasic();
    TestRingOverflow();
    TestOversizedCallback();
    TestPipeChannelBasic();
    TestPidMismatch();
    TestPidMatch();
    puts("\nAll broker channel tests passed.");
    return 0;
}

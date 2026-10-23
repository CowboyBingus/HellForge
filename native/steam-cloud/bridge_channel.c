/* The Steam bridge channel: a named pipe and callback ring with low mandatory labels (docs/steam-bridge.md). The low
 * label lets the game process (low integrity) connect to and write the pipe and map the ring; nothing else at low
 * integrity can, since the names are random per session. The broker verifies the client's process id against what the
 * session runner started.
 *
 * The pipe is message-mode and duplex: each WriteFile is one message, each ReadFile returns one whole message. The ring
 * is a page-file-backed file mapping: 60 fixed-size slots (1024 bytes each), single-producer (broker) single-consumer
 * (game), with writeSeq/readSeq on separate cache lines. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "bridge_channel.h"

static PSECURITY_DESCRIPTOR g_lowLabelSd;

static SECURITY_ATTRIBUTES* LowLabelSA(void)
{
    static SECURITY_ATTRIBUTES sa;
    if (g_lowLabelSd) { sa.lpSecurityDescriptor = g_lowLabelSd; return &sa; }
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)", SDDL_REVISION_1, &g_lowLabelSd, NULL))
        return NULL;
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = g_lowLabelSd;
    sa.bInheritHandle = FALSE;
    return &sa;
}

/* --- Pipe I/O --- */

uint32_t BridgePipeRecv(HANDLE pipe, void* body, uint32_t bodyCapacity, uint32_t* bodySize)
{
    uint8_t buf[BRIDGE_MSG_BUF];
    DWORD read = 0;
    if (!ReadFile(pipe, buf, sizeof buf, &read, NULL) || read < 4) return 0;
    uint32_t type;
    memcpy(&type, buf, 4);
    uint32_t n = read - 4;
    if (n > bodyCapacity) return 0;
    *bodySize = n;
    if (n > 0) memcpy(body, buf + 4, n);
    return type;
}

int BridgePipeSend(HANDLE pipe, uint32_t type, const void* body, uint32_t bodySize)
{
    uint8_t buf[BRIDGE_MSG_BUF];
    uint32_t total = 4 + bodySize;
    if (total > sizeof buf) return 0;
    memcpy(buf, &type, 4);
    if (bodySize > 0 && body) memcpy(buf + 4, body, bodySize);
    DWORD written = 0;
    return WriteFile(pipe, buf, total, &written, NULL) && written == total;
}

/* --- Broker (server) --- */

int BrokerChannelCreate(BrokerChannel* ch, const wchar_t* suffix)
{
    memset(ch, 0, sizeof *ch);
    ch->pipe = INVALID_HANDLE_VALUE;
    swprintf(ch->pipeName, 128, L"\\\\.\\pipe\\hellforge-broker-%ls", suffix);
    swprintf(ch->ringName, 128, L"Local\\hellforge-ring-%ls", suffix);

    SECURITY_ATTRIBUTES* sa = LowLabelSA();
    if (!sa) return 0;

    ch->pipe = CreateNamedPipeW(ch->pipeName,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, BRIDGE_MSG_BUF, BRIDGE_MSG_BUF, 0, sa);
    if (ch->pipe == INVALID_HANDLE_VALUE) return 0;

    ch->ringMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, sa, PAGE_READWRITE,
        0, (DWORD)BRIDGE_RING_MAP_SIZE, ch->ringName);
    if (!ch->ringMapping) goto fail;

    ch->ring = (BridgeRingHeader*)MapViewOfFile(ch->ringMapping, FILE_MAP_ALL_ACCESS, 0, 0, BRIDGE_RING_MAP_SIZE);
    if (!ch->ring) goto fail;

    memset(ch->ring, 0, BRIDGE_RING_MAP_SIZE);
    return 1;
fail:
    BrokerChannelClose(ch);
    return 0;
}

int BrokerChannelAccept(BrokerChannel* ch, DWORD expectedPid)
{
    if (!ConnectNamedPipe(ch->pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) return 0;

    uint8_t body[256];
    uint32_t bodySize = 0;
    uint32_t type = BridgePipeRecv(ch->pipe, body, sizeof body, &bodySize);
    if (type != BRIDGE_HELLO || bodySize < sizeof(uint32_t))
    {
        BridgePipeSend(ch->pipe, BRIDGE_HELLO_DENIED, NULL, 0);
        DisconnectNamedPipe(ch->pipe);
        return 0;
    }

    uint32_t declaredPid;
    memcpy(&declaredPid, body, sizeof declaredPid);

    ULONG pipePid = 0;
    GetNamedPipeClientProcessId(ch->pipe, &pipePid);
    if (declaredPid != pipePid)
    {
        BridgePipeSend(ch->pipe, BRIDGE_HELLO_DENIED, NULL, 0);
        DisconnectNamedPipe(ch->pipe);
        return 0;
    }

    if (expectedPid != 0 && declaredPid != expectedPid)
    {
        BridgePipeSend(ch->pipe, BRIDGE_HELLO_DENIED, NULL, 0);
        DisconnectNamedPipe(ch->pipe);
        return 0;
    }

    ch->clientPid = declaredPid;
    BridgePipeSend(ch->pipe, BRIDGE_HELLO_OK, NULL, 0);
    return 1;
}

int BrokerChannelPushCallback(BrokerChannel* ch, uint32_t callbackId, const void* data, uint32_t dataSize)
{
    if (!BridgeRingCanWrite(ch->ring)) return 0;
    if (dataSize > BRIDGE_RING_SLOT_SIZE - 8) return 0;

    BridgeRingSlot* slot = BridgeRingSlotAt(ch->ring, ch->ring->writeSeq);
    slot->callbackId = callbackId;
    slot->dataSize = dataSize;
    if (dataSize > 0) memcpy(slot->data, data, dataSize);

    MemoryBarrier();
    ch->ring->writeSeq++;
    return 1;
}

void BrokerChannelClose(BrokerChannel* ch)
{
    if (ch->ring) { UnmapViewOfFile(ch->ring); ch->ring = NULL; }
    if (ch->ringMapping) { CloseHandle(ch->ringMapping); ch->ringMapping = NULL; }
    if (ch->pipe != INVALID_HANDLE_VALUE) { DisconnectNamedPipe(ch->pipe); CloseHandle(ch->pipe); ch->pipe = INVALID_HANDLE_VALUE; }
}

/* --- Game (client) --- */

int GameChannelConnect(GameChannel* ch, const wchar_t* suffix)
{
    memset(ch, 0, sizeof *ch);
    wchar_t pipeName[128], ringName[128];
    swprintf(pipeName, 128, L"\\\\.\\pipe\\hellforge-broker-%ls", suffix);
    swprintf(ringName, 128, L"Local\\hellforge-ring-%ls", suffix);

    ch->pipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (ch->pipe == INVALID_HANDLE_VALUE) return 0;

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(ch->pipe, &mode, NULL, NULL);

    ch->ringMapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, ringName);
    if (!ch->ringMapping) { CloseHandle(ch->pipe); ch->pipe = INVALID_HANDLE_VALUE; return 0; }

    ch->ring = (BridgeRingHeader*)MapViewOfFile(ch->ringMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, BRIDGE_RING_MAP_SIZE);
    if (!ch->ring) { CloseHandle(ch->ringMapping); CloseHandle(ch->pipe); ch->pipe = INVALID_HANDLE_VALUE; return 0; }

    return 1;
}

int GameChannelHello(GameChannel* ch)
{
    uint32_t pid = GetCurrentProcessId();
    if (!BridgePipeSend(ch->pipe, BRIDGE_HELLO, &pid, sizeof pid)) return 0;

    uint8_t body[256];
    uint32_t bodySize = 0;
    uint32_t type = BridgePipeRecv(ch->pipe, body, sizeof body, &bodySize);
    return type == BRIDGE_HELLO_OK;
}

int GameChannelReadCallback(GameChannel* ch, uint32_t* callbackId, void* data, uint32_t capacity, uint32_t* dataSize)
{
    if (!BridgeRingCanRead(ch->ring)) return 0;

    MemoryBarrier();
    BridgeRingSlot* slot = BridgeRingSlotAt(ch->ring, ch->ring->readSeq);
    *callbackId = slot->callbackId;
    *dataSize = slot->dataSize;
    if (slot->dataSize > 0 && slot->dataSize <= capacity)
        memcpy(data, slot->data, slot->dataSize);

    MemoryBarrier();
    ch->ring->readSeq++;
    return 1;
}

void GameChannelClose(GameChannel* ch)
{
    if (ch->ring) { UnmapViewOfFile(ch->ring); ch->ring = NULL; }
    if (ch->ringMapping) { CloseHandle(ch->ringMapping); ch->ringMapping = NULL; }
    if (ch->pipe != INVALID_HANDLE_VALUE) { CloseHandle(ch->pipe); ch->pipe = INVALID_HANDLE_VALUE; }
}

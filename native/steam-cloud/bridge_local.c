/* The game's side of the bridge, by hand (bridge_local.h; docs/steam-bridge.md, phase 3): the round trip the generated
 * proxies make, the methods answered in the game (from the broker's snapshot, or fixed), and the game's callback
 * registrations with their dispatch from the ring. Only in the bridge build of the stand-in. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "bridge_gen.h"
#include "bridge_local.h"

void BridgeLog(const char* format, ...); /* steam_cloud.c */

#define GAME_APP_ID 553850u

static GameChannel* g_channel;

void BridgeLocalAttach(GameChannel* channel) { g_channel = channel; }

int BridgeInvoke(uint32_t method, BridgeWriter* w, BridgeReader* r)
{
    uint32_t size = 0, status = BRIDGE_STATUS_UNKNOWN;
    if (!g_channel || w->failed) return 0;
    /* the reply is read into the writer's buffer: GameChannelCall copies the arguments out before it reads */
    if (!GameChannelCall(g_channel, method, w->data, w->size, w->data, sizeof w->data, &size, &status) ||
        status != BRIDGE_STATUS_OK || size > sizeof w->data)
        return 0;
    r->data = w->data;
    r->size = size;
    r->at = 0;
    return 1;
}

/* Each refusal is logged once: the names are string literals, so their addresses tell them apart. */
void BridgeRefused(const char* what)
{
    static const char* seen[512];
    static volatile LONG count;
    LONG n = count < 512 ? count : 512;
    for (LONG i = 0; i < n; i++) if (seen[i] == what) return;
    LONG at = InterlockedIncrement(&count) - 1;
    if (at < 512) seen[at] = what;
    BridgeLog("bridge: refused %s", what);
}

/* ---- the snapshot ---- */

static BridgeSnapshot* Snapshot(void)
{
    return g_channel && g_channel->ring ? BridgeSnapshotOf(g_channel->ring) : NULL;
}

/* Copies what `read` takes from the snapshot under its seqlock: a consistent copy, or 0 after a few tries (the broker
 * writes about once a second, so a retry is rare). No system call. */
#define SNAPSHOT_READ(snap, ok, read) do {                                                         \
        (ok) = 0;                                                                                  \
        for (int try_ = 0; (snap) && (snap)->ready && try_ < 64 && !(ok); try_++) {                \
            uint32_t v_ = __atomic_load_n(&(snap)->version, __ATOMIC_ACQUIRE);                      \
            if (v_ & 1) { YieldProcessor(); continue; }                                            \
            read;                                                                                  \
            __atomic_thread_fence(__ATOMIC_ACQUIRE);                                               \
            (ok) = __atomic_load_n(&(snap)->version, __ATOMIC_RELAXED) == v_;                      \
        }                                                                                          \
    } while (0)

static void CopyString(char* to, size_t capacity, const char* from, size_t fromCapacity)
{
    size_t n = strnlen(from, fromCapacity);
    if (n >= capacity) n = capacity - 1;
    memcpy(to, from, n);
    to[n] = 0;
}

/* ---- ISteamClient (from SteamInternal_CreateInterface): its pipes and users are the bridge's one of each ---- */

int32_t Hand_SteamClient020_CreateSteamPipe(void* self) { (void)self; return 1; }
bool Hand_SteamClient020_BReleaseSteamPipe(void* self, int32_t hSteamPipe) { (void)self; return hSteamPipe == 1; }
int32_t Hand_SteamClient020_ConnectToGlobalUser(void* self, int32_t hSteamPipe) { (void)self; return hSteamPipe == 1 ? 1 : 0; }
void Hand_SteamClient020_ReleaseUser(void* self, int32_t hSteamPipe, int32_t hUser) { (void)self; (void)hSteamPipe; (void)hUser; }

/* ---- answered from the snapshot ---- */

int32_t Hand_SteamUser021_GetHSteamUser(void* self) { (void)self; return 1; }

bool Hand_SteamUser021_BLoggedOn(void* self)
{
    (void)self;
    BridgeSnapshot* s = Snapshot();
    int ok;
    bool on = false;
    SNAPSHOT_READ(s, ok, on = s->loggedOn != 0);
    return ok && on;
}

uint64_t* Hand_SteamUser021_GetSteamID(void* self, uint64_t* ret_)
{
    (void)self;
    BridgeSnapshot* s = Snapshot();
    int ok;
    uint64_t id = 0;
    SNAPSHOT_READ(s, ok, id = s->steamId);
    *ret_ = ok ? id : 0;
    return ret_;
}

const char* Hand_SteamFriends017_GetPersonaName(void* self)
{
    (void)self;
    static char name[sizeof(((BridgeSnapshot*)0)->personaName)];
    BridgeSnapshot* s = Snapshot();
    int ok;
    SNAPSHOT_READ(s, ok, CopyString(name, sizeof name, s->personaName, sizeof s->personaName));
    if (!ok) name[0] = 0;
    return name;
}

const char* Hand_SteamUtils010_GetIPCountry(void* self)
{
    (void)self;
    static char country[sizeof(((BridgeSnapshot*)0)->ipCountry)];
    BridgeSnapshot* s = Snapshot();
    int ok;
    SNAPSHOT_READ(s, ok, CopyString(country, sizeof country, s->ipCountry, sizeof s->ipCountry));
    if (!ok) country[0] = 0;
    return country;
}

uint32_t Hand_SteamUtils010_GetAppID(void* self) { (void)self; return GAME_APP_ID; }

/* the overlay lives in the broker's process: nothing to place, and none over the game */
void Hand_SteamUtils010_SetOverlayNotificationPosition(void* self, int32_t eNotificationPosition) { (void)self; (void)eNotificationPosition; }
bool Hand_SteamUtils010_IsOverlayEnabled(void* self) { (void)self; return false; }

bool Hand_SteamUtils010_IsSteamRunningOnSteamDeck(void* self)
{
    (void)self;
    BridgeSnapshot* s = Snapshot();
    int ok;
    bool deck = false;
    SNAPSHOT_READ(s, ok, deck = s->steamDeck != 0);
    return ok && deck;
}

const char* Hand_STEAMAPPS_INTERFACE_VERSION008_GetCurrentGameLanguage(void* self)
{
    (void)self;
    static char language[sizeof(((BridgeSnapshot*)0)->language)];
    BridgeSnapshot* s = Snapshot();
    int ok;
    SNAPSHOT_READ(s, ok, CopyString(language, sizeof language, s->language, sizeof s->language));
    if (!ok || !language[0]) strcpy(language, "english");
    return language;
}

/* The game itself and its DLC, as the broker saw them; any other app is not subscribed. */
bool Hand_STEAMAPPS_INTERFACE_VERSION008_BIsSubscribedApp(void* self, uint32_t appID)
{
    (void)self;
    BridgeSnapshot* s = Snapshot();
    int ok;
    bool subscribed = false;
    SNAPSHOT_READ(s, ok, {
        subscribed = false;
        uint32_t n = s->dlcCount < BRIDGE_DLC_MAX ? s->dlcCount : BRIDGE_DLC_MAX;
        for (uint32_t i = 0; i < n; i++) if (s->dlc[i].appId == appID) subscribed = s->dlc[i].subscribed != 0;
    });
    return appID == GAME_APP_ID ? ok : ok && subscribed;
}

int32_t Hand_STEAMAPPS_INTERFACE_VERSION008_GetDLCCount(void* self)
{
    (void)self;
    BridgeSnapshot* s = Snapshot();
    int ok;
    uint32_t n = 0;
    SNAPSHOT_READ(s, ok, n = s->dlcCount < BRIDGE_DLC_MAX ? s->dlcCount : BRIDGE_DLC_MAX);
    return ok ? (int32_t)n : 0;
}

bool Hand_STEAMAPPS_INTERFACE_VERSION008_BGetDLCDataByIndex(void* self, int32_t iDLC, void* pAppID, void* pbAvailable, void* pchName,
                                                           int32_t cchNameBufferSize)
{
    (void)self;
    BridgeSnapshot* s = Snapshot();
    int ok;
    BridgeSnapshotDlc dlc;
    bool found = false;
    SNAPSHOT_READ(s, ok, {
        found = iDLC >= 0 && (uint32_t)iDLC < s->dlcCount && iDLC < BRIDGE_DLC_MAX;
        if (found) dlc = s->dlc[iDLC];
    });
    if (!ok || !found) return false;
    if (pAppID) memcpy(pAppID, &dlc.appId, 4);
    if (pbAvailable) { bool available = dlc.available != 0; memcpy(pbAvailable, &available, 1); }
    if (pchName && cchNameBufferSize > 0) CopyString((char*)pchName, (size_t)cchNameBufferSize, dlc.name, sizeof dlc.name);
    return true;
}

bool Hand_STEAMAPPS_INTERFACE_VERSION008_GetCurrentBetaName(void* self, void* pchName, int32_t cchNameBufferSize)
{
    (void)self;
    BridgeSnapshot* s = Snapshot();
    int ok;
    bool beta = false;
    char name[sizeof(((BridgeSnapshot*)0)->betaName)] = "";
    SNAPSHOT_READ(s, ok, { beta = s->hasBeta != 0; CopyString(name, sizeof name, s->betaName, sizeof s->betaName); });
    if (!ok || !beta) return false;
    if (pchName && cchNameBufferSize > 0) CopyString((char*)pchName, (size_t)cchNameBufferSize, name, sizeof name);
    return true;
}

/* ---- answered in the game: HellForge's co-op does not use Steam P2P (its party traffic goes through the backend) ---- */

bool Hand_SteamNetworking006_IsP2PPacketAvailable(void* self, void* pcubMsgSize, int32_t nChannel)
{
    (void)self; (void)nChannel;
    if (pcubMsgSize) memset(pcubMsgSize, 0, 4);
    return false;
}

bool Hand_SteamNetworking006_ReadP2PPacket(void* self, void* pubDest, uint32_t cubDest, void* pcubMsgSize, void* psteamIDRemote, int32_t nChannel)
{
    (void)self; (void)pubDest; (void)cubDest; (void)nChannel;
    if (pcubMsgSize) memset(pcubMsgSize, 0, 4);
    if (psteamIDRemote) memset(psteamIDRemote, 0, 8);
    return false;
}

/* ---- callbacks ----
 *
 * A CCallbackBase (steam_api_common.h) is its vtable, a flags byte at +8 (k_ECallbackFlagsRegistered = 1) and the
 * callback id at +12. MSVC orders its two Run overloads in reverse, so slot 0 is Run(pvParam, bIOFailure, hSteamAPICall):
 * a CCallback's forwards to Run(pvParam) and a CCallResult's checks the call, so slot 0 is right for both. Slot 2 is
 * GetCallbackSizeBytes. */

typedef void (*RunFn)(void* self, void* param, bool ioFailure, uint64_t call);
typedef int (*SizeFn)(void* self);

#define MAX_REGISTRATIONS 512

typedef struct Registration { void* object; int id; uint64_t call; } Registration; /* call 0: a callback */

static SRWLOCK g_registrationsLock = SRWLOCK_INIT;
static Registration g_registrations[MAX_REGISTRATIONS];
static int g_registrationCount;

static void Add(void* object, int id, uint64_t call)
{
    AcquireSRWLockExclusive(&g_registrationsLock);
    if (g_registrationCount < MAX_REGISTRATIONS) g_registrations[g_registrationCount++] = (Registration){ object, id, call };
    else BridgeRefused("a callback registration past 512");
    ReleaseSRWLockExclusive(&g_registrationsLock);
}

static void Remove(void* object, int callbacks, uint64_t call)
{
    AcquireSRWLockExclusive(&g_registrationsLock);
    for (int i = 0; i < g_registrationCount;)
    {
        Registration* r = &g_registrations[i];
        if (r->object == object && (callbacks ? r->call == 0 : r->call == call)) g_registrations[i] = g_registrations[--g_registrationCount];
        else i++;
    }
    ReleaseSRWLockExclusive(&g_registrationsLock);
}

void BridgeRegisterCallback(void* callback, int id)
{
    if (!callback) return;
    uint8_t* object = (uint8_t*)callback;
    object[8] |= 1;
    memcpy(object + 12, &id, 4);
    Add(callback, id, 0);
}

void BridgeUnregisterCallback(void* callback)
{
    if (!callback) return;
    ((uint8_t*)callback)[8] &= (uint8_t)~1;
    Remove(callback, 1, 0);
}

void BridgeRegisterCallResult(void* callback, uint64_t call) { if (callback && call) Add(callback, 0, call); }
void BridgeUnregisterCallResult(void* callback, uint64_t call) { if (callback && call) Remove(callback, 0, call); }

static bool StillRegistered(void* object, int id, uint64_t call)
{
    bool found = false;
    AcquireSRWLockShared(&g_registrationsLock);
    for (int i = 0; i < g_registrationCount && !found; i++)
        found = g_registrations[i].object == object && (call ? g_registrations[i].call == call : g_registrations[i].call == 0 && g_registrations[i].id == id);
    ReleaseSRWLockShared(&g_registrationsLock);
    return found;
}

/* The struct the object expects, at least its own size: the payload, zero-padded when the broker's is shorter. */
static void Run(void* object, const uint8_t* payload, uint32_t size, bool ioFailure, uint64_t call)
{
    void* const* vtable = *(void* const* const*)object;
    int expected = ((SizeFn)vtable[2])(object);
    uint8_t param[BRIDGE_RING_DATA_SIZE];
    if (expected < 0 || (uint32_t)expected > sizeof param) { BridgeRefused("a callback larger than a ring slot"); return; }
    memset(param, 0, sizeof param);
    memcpy(param, payload, size < sizeof param ? size : sizeof param);
    if (size < (uint32_t)expected) BridgeRefused("a callback shorter than its struct (padded)");
    ((RunFn)vtable[0])(object, param, ioFailure, call);
}

void BridgeDispatch(uint32_t callbackId, const uint8_t* data, uint32_t size)
{
    if (callbackId & BRIDGE_CALL_RESULT_FLAG)
    {
        if (size < BRIDGE_CALL_RESULT_HEADER) return;
        uint64_t call;
        memcpy(&call, data, 8);
        bool ioFailure = data[8] != 0;
        /* as Steam: the registration ends with its delivery */
        void* object = NULL;
        AcquireSRWLockExclusive(&g_registrationsLock);
        for (int i = 0; i < g_registrationCount; i++)
            if (g_registrations[i].call == call) { object = g_registrations[i].object; g_registrations[i] = g_registrations[--g_registrationCount]; break; }
        ReleaseSRWLockExclusive(&g_registrationsLock);
        if (object) Run(object, data + BRIDGE_CALL_RESULT_HEADER, size - BRIDGE_CALL_RESULT_HEADER, ioFailure, call);
        return;
    }
    /* a callback: every object registered for its id, as it stands when each is called (a handler may unregister others) */
    void* objects[MAX_REGISTRATIONS];
    int n = 0;
    AcquireSRWLockShared(&g_registrationsLock);
    for (int i = 0; i < g_registrationCount; i++)
        if (g_registrations[i].call == 0 && g_registrations[i].id == (int)callbackId) objects[n++] = g_registrations[i].object;
    ReleaseSRWLockShared(&g_registrationsLock);
    for (int i = 0; i < n; i++)
        if (StillRegistered(objects[i], (int)callbackId, 0)) Run(objects[i], data, size, false, 0);
}

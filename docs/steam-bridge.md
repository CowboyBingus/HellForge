# The Steam bridge: the game at low integrity

**Status: in development** on branch `research/steam-broker`. Nothing here ships yet.

## Why

Mods run inside the game's process, and so does anything a crafted file manages to execute through a bug in the
engine's parsers. HellForge already keeps the copied game off the internet with firewall rules. Code inside the game can
still:
- write anywhere the player can (documents, the Startup folder, `HKCU` Run keys, HellForge's own programs);
- write into other programs the player runs (the launcher, the backend, Steam);
- start programs the firewall rules do not list.

A process at **low integrity** cannot do any of that. Windows refuses its writes to the player's files, registry and
processes ("no write up"). That holds even against native code calling the kernel directly.

**Without a bridge the game stops.** Tried on one PC (branch `experiment/low-integrity-game`): the game ran at low
integrity (`0x1000`) and its graphics worked, but it stopped at **CONNECTION ERROR - STEAM NOT DETECTED**. The Steam
client's IPC objects are at the player's normal integrity, so a low-integrity process cannot use them.

## Design

```
 game (low integrity, job: no child processes)          broker (normal integrity, one per session)
 ┌────────────────────────────────────────────┐          ┌──────────────────────────────────────────┐
 │ steam_api64.dll stand-in, bridged mode:    │  pipe    │ hellforge_steam_broker.exe               │
 │  flat API (Init, RunCallbacks, Register..) │ ───────► │  Valve's steam_api64.dll, SteamAPI_Init  │
 │  interface proxies (generated vtables)     │ ◄─────── │  the allow-list, then the flat API       │
 │  callbacks: read from the shared ring      │  ring    │  manual dispatch: callbacks, call results│
 └────────────────────────────────────────────┘          └──────────────────────────────────────────┘
```

- **The game's side** is the `steam_api64.dll` stand-in, in bridged mode (when the session names a broker).
  - It never loads Valve's DLL in the game.
  - It answers the flat API itself: init, shutdown, `RunCallbacks`, callback registration, the user and pipe handles,
    and context init.
  - It hands out generated proxy objects for each interface the game requests. Each vtable slot marshals its call to
    the broker.
  - An export with no bridged implementation logs once and fails, so nothing ever reaches Valve's code in the game.
- **The broker** is native C and runs at the player's normal integrity: one process per session, started by the session
  runner.
  - It loads Valve's `steam_api64.dll` (the game's pinned copy) as app 553850, then calls `SteamAPI_Init` and manual
    dispatch.
  - It serves one client: the game process it was started for. It checks the pipe's client process id against the id
    the runner gives it.
  - It makes allowed calls through Valve's flat C API, so the broker itself needs no C++ ABI.
- **Calls** go over a duplex pipe, one request then its reply, serialized by a lock in the stand-in.
  - The pipe's security descriptor grants the player and carries a **low** mandatory label (no write up), so the game
    can open it and nothing else at low integrity can.
  - The pipe name is random per session.
- **Callbacks** are pushed by the broker.
  - The broker writes every callback and call result (`SteamAPICallCompleted_t`, then `GetAPICallResult`) into a
    shared-memory ring, labelled low as well.
  - The stand-in's `RunCallbacks` reads the ring and calls the game's registered `CCallbackBase` objects.
  - An idle frame is one read of the ring's sequence number: no system call.
- **Steam Input,** which the game polls every frame, is answered from a shared snapshot the broker refreshes. This is
  done once the census shows which calls happen per frame; see "Cost".

### The allow-list (the security boundary)

The bridge must not become the way out of the sandbox. Every method of the 7 bridged interfaces is classed, by review,
as one of:
- **allowed;**
- **allowed with checks** (sizes, ranges, the strings a key may carry);
- **refused** (the broker answers as Steam would on failure).

Interfaces the game never requests are refused whole: `ISteamHTTP`, `ISteamUGC`, `ISteamMatchmaking` and so on.

Methods that carry data to other people or out of the PC, which are refused unless the census shows the game needs them,
and then checked:
- `ISteamFriends`: `SetRichPresence` (strings friends see), `ReplyToFriendMessage`, `InviteUserToGame`,
  `ActivateGameOverlayToWebPage` (it may open the player's browser), `SetPersonaName`;
- `ISteamNetworking`: `SendP2PPacket` to any SteamID. HellForge's co-op does not use Steam P2P, since its party traffic
  goes through the backend.
- `ISteamUserStats`: leaderboard uploads.

The broker logs every refusal.

### Where the definitions come from

The proxies and the broker's dispatch are **generated** by `HellForge.SdkShimGen`, as the PlayFab stand-ins are:
- **Methods and parameter kinds** come from Valve's Steamworks SDK (`steam_api.json`), a version whose
  `redistributable_bin/win64/steam_api64.dll` matches the game's pinned copy byte for byte. It is pinned by SHA-256 in
  the gitignored `tools/`; nothing from it is committed or shipped.
- **Vtable slots** come from the game's `steam_api64.dll` itself. Each flat export is a thunk through its interface's
  vtable, and the generator reads the slot from its code. The table must match the SDK's declaration order, or
  generation fails: MSVC orders overloads in reverse, which the binary settles.
- **The ABI is MSVC x64.** `this` is in `RCX`. A method returning a class type (`CSteamID`, for example) returns it
  through a hidden pointer in `RDX`. The proxies are C functions written with that pointer explicit.

### The rest of a low-integrity session

- **The game process:** the session runner starts it with a lowered copy of its own token (`CreateProcessAsUser`; no
  admin needed). Its job adds an active-process limit of 1, so no child processes. That loses the crash handler, and
  NVIDIA's DLSS updater that Steam's overlay starts.
- **Folders the game writes:**
  - The runner **creates** them with a low label in their security descriptor. Labelling at creation, at or below one's
    own integrity, needs no admin.
  - They cover: the game's app data, temp and shader cache; the session's game logs (the mod loader's, the stand-in's,
    the engine log); and the profile's user data and cloud folder.
  - Data the runner keeps across sessions is copied in at the start and back at the end, and read back as untrusted.
- **Pipes the game opens** carry a low label: the lobby and party stand-ins' pipe (backend) and the live pipe (backend).
- **The in-game editor** writes into `mods/` (Local Stats, its settings). At low integrity it cannot. Its saves go to
  the backend over the live pipe instead, which validates them (`StatsData.Parse`) and writes them. A low-writable
  `mods/` would let a compromised game plant files for the next session.

### What it does not stop

- **Reading the player's files.** Low integrity blocks writes, not reads; data read can only leave through the
  channels below.
- **Traffic to this PC:** the backend's HTTP on loopback, and its pipes. The backend treats everything from the game
  as untrusted, as it does today.
- **Steam's overlay.** It is loaded by `SteamAPI_Init` in the process that calls it, which is now the broker, so the
  game has no overlay: no chat, invite dialog or screenshots. The invite dialog's callbacks are still bridged.

## The census

`native/steam-cloud/steam_census.c`, off unless `HELLFORGE_STEAM_CENSUS_LOG` names a file.

**Step 1** logs each interface version the game requests, each stand-in export it resolves by name, and each callback
id it registers.

Results (2026-10-22, one session from start to the title screen, the game connected to the backend): **8 interfaces**,
each requested once.

| Interface | Version | What the game needs it for, likely |
| --- | --- | --- |
| `ISteamUtils` | `SteamUtils010` | app id, overlay state, the gamepad text box |
| `ISteamRemoteStorage` | `016` | the cloud save; HellForge's stand-in already answers it locally |
| `ISteamUserStats` | `012` | stats and achievements |
| `ISteamApps` | `008` | ownership, DLC, language |
| `ISteamInput` | `SteamInput006` | controllers: called every frame while a controller is used |
| `ISteamUser` | `SteamUser021` | SteamID, auth tickets |
| `ISteamNetworking` | `SteamNetworking006` | the old P2P API |
| `ISteamFriends` | `SteamFriends017` | names, overlay, invites, rich presence |

**17 callback ids,** decoded by Steamworks' numbering; to be confirmed against the headers:

| Area | Callbacks |
| --- | --- |
| user | 143 `ValidateAuthTicketResponse`, 152 `MicroTxnAuthorizationResponse`, 163 `GetAuthSessionTicketResponse` |
| friends | 331 `GameOverlayActivated`, 333 `GameLobbyJoinRequested`, 337 `GameRichPresenceJoinRequested` |
| matchmaking | 505 `LobbyDataUpdate` |
| utils | 714 `GamepadTextInputDismissed` |
| apps | 1005 `DlcInstalled` |
| user stats | 1101 `UserStatsReceived`, 1102 `UserStatsStored`, 1103 `UserAchievementStored` |
| networking | 1202 `P2PSessionRequest`, 1203 `P2PSessionConnectFail`, 1221 `SteamNetConnectionStatusChanged` |

**Flat functions: not seen by name, but the hook could not see them.** Step 1 hooked kernelbase's `GetProcAddress`.
kernel32's `GetProcAddress` jumps to kernelbase's `GetProcAddressForCaller` instead, so lookups through kernel32 never
reached the hook (the census test showed it). The hook is now on `GetProcAddressForCaller`, and step 2 counts every
export the game calls whichever way it found it.

**Step 2** (the census build) needs no definitions:
- **Generic slot proxies:** each interface the game gets is a proxy whose 192 vtable slots count their calls, then jump
  to Valve's object's slot with the real object in `RCX` and every other register untouched.
- **A counting thunk per export:** every function export the stand-in does not write itself counts its calls and jumps
  to Valve's export. Data exports still forward.
- **Callback struct sizes** come from each registered callback's `GetCallbackSizeBytes`, as a check on the generated
  layouts.
- **A report every 15 s** of each count that changed (`HELLFORGE_STEAM_CENSUS_PERIOD_MS` changes the period). The
  count for `SteamAPI_RunCallbacks` is the frame count, so per-frame rates follow.

Build it with `scripts/Build-Engine.ps1 -SteamCensus`: the package is `hellforge-engine-<version>-census.zip`, and its
`engine.json` says `"steamCensus": true`. It is never for release. Every engine build also builds the census stand-in
and runs `census_test.c` against a fake Steam DLL (`fake_steam.c`): register, XMM and stack arguments, a hidden result
pointer, unwind data, and the log's lines and counts.

The census build costs a call and an interlocked increment per Steam call, and one more for proxied methods.
Unmeasured in game; the census session itself is not a cost measurement.

Run it while someone plays: the menus, the social tab, a mission, a controller.

### Step 2 results (2026-10-23, run `e0b8d508`, PC A, 214 s: the ship and the start of a mission)

Slot names below follow each interface's declaration order in the Steamworks headers as remembered; they are likely,
not checked, until phase 3 reads the SDK.

**Flat exports: 11 called**, besides the stand-in's own 4. Even with the hook on `GetProcAddressForCaller`, the game
looked none of them up by name, so its protection does resolve them itself.
- Once: `SteamAPI_RestartAppIfNecessary`, `SteamAPI_Init`, `SteamInternal_CreateInterface`,
  `SteamGameServer_GetHSteamUser`, `SteamInternal_FindOrCreateGameServerInterface`, `SteamAPI_UnregisterCallback`,
  `SteamAPI_Shutdown`.
- Often: `SteamAPI_RunCallbacks` 82,436 (about 400 a second), `SteamInternal_ContextInit` 73,040 (the inline
  accessors), `SteamAPI_IsSteamRunning` 2,014, `SteamAPI_GetHSteamUser` 8.

**9 interfaces.** Step 1's 8, plus **`SteamMatchMaking009`**, requested on reaching the ship: the game makes a Steam
lobby of its own (call result 513, likely `LobbyCreated_t`). `ISteamMatchmaking` cannot be refused whole.

**Calls per `RunCallbacks`** (the methods called more than a few times):

| Slot | Calls | Per `RunCallbacks` | Likely method |
| --- | --- | --- | --- |
| `SteamNetworking006` #2 | 82,438 | 1.00 | `ReadP2PPacket` or `IsP2PPacketAvailable` (P2P polling) |
| `STEAMAPPS_INTERFACE_VERSION008` #11 | 46,894 | 0.57 | `BGetDLCDataByIndex` |
| `STEAMAPPS_INTERFACE_VERSION008` #10 | 11,718 | 0.14 | `GetDLCCount` |
| `STEAMAPPS_INTERFACE_VERSION008` #6 | 11,716 | 0.14 | `BIsSubscribedApp` |
| `SteamUser021` #1 | 2,013 | 0.024 | `BLoggedOn`, beside each `SteamAPI_IsSteamRunning` |
| `SteamMatchMaking009` #17, #18 | 1,534 each | 0.019 | lobby data or members |
| `SteamFriends017` #4 | 228 | | |

Everything else was called 41 times or fewer: `SteamFriends017` #0, #3, #5, #42, #43; `SteamUtils010` #4, #9, #10,
#31, #32, #34; `STEAMAPPS_INTERFACE_VERSION008` #4, #15; `STEAMUSERSTATS_INTERFACE_VERSION012` #0, #6;
`SteamUser021` #2, #13; `SteamMatchMaking009` #13, #15, #20, #34, #35. `SteamInput006` #38 was called 4 times at
start and never again.

**Callbacks: 16 ids, with their sizes** (bytes): 143 (20), 152 (24), 163 (8), 331 (1), 333 (16), 337 (264), 505 (24),
714 (8), 1005 (4), 1101 (24), 1102 (16), 1103 (152), 1202 (8), 1203 (9), 1221 (712), and the call result 513 (16).

**What it means for the bridge:**
- Forwarding every call would cost about 1.9 round trips per `RunCallbacks`. Almost all of them can be answered in the
  game instead:
  - `SteamInternal_ContextInit` is the stand-in's own business.
  - The DLC answers (#6, #10, #11) change only with callback 1005 (`DlcInstalled`), so the broker can publish them in
    shared memory and refresh them then.
  - P2P polling (#2) can read a "packets waiting" flag the broker sets.

  An idle frame then needs no round trip.
- The allow-list needs part of `ISteamMatchmaking` (the game's own lobby).
- **Not covered by this run:**
  - The objects from `SteamInternal_CreateInterface` and `SteamInternal_FindOrCreateGameServerInterface` are not
    proxied, so calls on them are uncounted.
  - Whether a controller was in use, and so whether Steam Input polls per frame.
  - A whole mission, and the social tab.

## Cost

- **Idle frame:** the ring's sequence read in `RunCallbacks`. No system call; no Steam Input traffic without a
  controller.
- **A bridged call:** one pipe round trip, about 10-30 us. That is to be measured in game; most calls happen once or on
  events.
- **Steam Input with a controller:** read from the snapshot, with no system call per call. The snapshot's per-frame
  cost is to be measured.

Every number here is unmeasured in game until phase 4.

## Phases, each tested before the next

1. **Census step 2:** slot proxies, export thunks and callback sizes. Built and tested against a fake Steam DLL
   (2026-10-22); first played run 2026-10-23 (the ship and a mission's start). Still to cover: a controller, a whole
   mission, the social tab, and the game-server and `CreateInterface` objects.
2. **The broker and its channel:**
   - the pipe and ring with low labels and the client check;
   - `SteamAPI_Init` in the broker;
   - manual dispatch into the ring, and the flat API in the stand-in.

   Fixture tests use a fake Steam DLL.
3. **Generated proxies:** `SdkShimGen steam-bridge`, from the pinned SDK and the game's DLL, with the allow-list as a
   reviewed table. Fixture tests per parameter kind (strings, buffers, structs, the `CSteamID` hidden pointer).
4. **A low-integrity session:** the token and job, labelled folders, pipe labels, and the editor's saves through the
   backend. In game, solo then co-op on two PCs: no STEAM NOT DETECTED, login, a mission, a controller, an invite, and
   per-frame cost measured.

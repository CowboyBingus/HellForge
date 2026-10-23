# The Steam bridge: the game at low integrity

**Status: in development** on `port/networking-0.2.0`. Built and tested with fixtures: the census, the broker and its
channel (phase 2), and the session side of phase 4. Phase 3 (the generated proxies) is not started, so a sandboxed game
still loads Valve's DLL for every interface call; the sandbox is not a boundary until it is. A session is sandboxed only
when asked (`HellForge.Session play --sandbox`, or `HELLFORGE_SANDBOX=1`); nothing here ships yet, and none of it has
run in game.

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

- **The game's side** is the `steam_api64.dll` stand-in, in bridged mode (when the session sets
  `HELLFORGE_BRIDGE_PIPE`).
  - Its own functions never load Valve's DLL in that mode. Its `SteamAPI_Init` and `SteamAPI_InitSafe` (the pinned
    build's init exports) connect to the broker on first use, never in `DllMain` (no wait under the loader lock).
    It waits up to 10 s for the broker's pipe. Without a broker it **fails closed**: init returns false,
    `IsSteamRunning` 0, and nothing falls back to Valve's DLL.
  - It answers the flat API itself: init, shutdown, `RunCallbacks`, `IsSteamRunning` (asked of the broker), callback
    registration, the user and pipe handles. `SteamInternal_ContextInit` is still to do.
  - **Until phase 3**, the other exports are still loader forwarders to Valve's DLL, so a forwarded call would load it
    in the game. Phase 3's generated exports and proxies replace them: an export with no bridged implementation logs
    once and fails.
  - It hands out generated proxy objects for each interface the game requests (phase 3). Each vtable slot marshals its
    call to the broker.
- **The broker** (`hellforge_steam_broker.exe`, beside the stand-ins) is native C and runs at the player's normal
  integrity: one process per session, started by the session runner as `<pipe suffix> <game pid> <Valve's DLL>`.
  - It creates the pipe and ring first, then loads Valve's `steam_api64.dll` (the game's pinned copy) as app 553850
    (`SteamAppId` in its environment; it never calls `SteamAPI_RestartAppIfNecessary`, which may start the installed
    game) and calls `SteamAPI_Init` (`SteamAPI_InitFlat` with a newer SDK) and manual dispatch.
  - It serves one client: the game process it was started for. A client whose pipe process id differs from its own
    HELLO or from the game's, or that says nothing for 5 s, is refused and disconnected, and the broker waits on.
  - It watches the game's process, and exits when the game does, even while it waits for the connection.
  - One thread makes every Steam call: it answers calls, and when none comes for 16 ms it runs manual dispatch.
  - It makes allowed calls through Valve's flat C API, so the broker itself needs no C++ ABI.
- **Calls** go over a duplex pipe, one request then its reply, serialized by a lock in the stand-in; each carries a
  sequence number its reply must echo. A call too short or too large is answered as malformed; a broken pipe marks
  the channel dead, and every later call fails at once.
  - The pipe's security descriptor grants the player's own SID read and write only, and carries a **low** mandatory
    label (no write up), so the game can open it. It is the first and only instance of its name and refuses remote
    clients, so nothing that made the name first can stand in for the broker.
  - The pipe name is the session's id. Any low-integrity process of the player could open it; the process id check
    is what keeps them out.
- **Callbacks** are pushed by the broker.
  - The broker writes every callback and call result (`SteamAPICallCompleted_t`, then `GetAPICallResult`) into a
    shared-memory ring: an unnamed section the broker duplicates into the checked game process only.
  - The game can write the ring, so each side keeps its own position and only publishes it there. The broker clamps
    the game's read position before it uses it, and a slot index is always an unsigned position modulo 60. Callbacks
    that find the ring full are counted and logged.
  - The stand-in's `RunCallbacks` reads the ring and calls the game's registered `CCallbackBase` objects (phase 3:
    until then they are read and dropped).
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
- **Folders the game writes** get a low label (`LowIntegrityHelper.LabelFoldersLow`; at or below one's own integrity
  that needs no admin):
  - the game's environment (`userdata/environment`: its AppData, temp and shader cache) and its profile's user data
    (settings, saves, the cloud folder). They keep the label between sessions; HellForge reads nothing in them as
    trusted. (The design copied them in and out per session instead; the shader cache makes that costly.)
  - the session's `game/` folder inside its run folder: the mod loader's, the stand-ins' and the census's logs, the
    stats snapshot, and the editor's saves. The rest of the run folder stays at the player's level: the runner's
    result, the backend's evidence and the `steam_api64.dll` copy the backend loads for the peer link must stay out of
    the game's reach.
- **Pipes the game opens** carry a low label when the backend runs with `--sandbox` (`PipeServer`): the lobby and party
  stand-ins' pipe and the live pipe. Only the player's account may open them, from this PC only.
- **The in-game editor** cannot write into `mods/` (Local Stats, its settings, its presets); a low-writable `mods/`
  would let a compromised game plant files for the next session. Sandboxed, each save writes the session's saved
  changes as a list (`game/sandbox-stats/changes.json`), and its settings and exported presets beside it. Once the game
  is gone the runner applies them (`SandboxStats`): Local Stats changes onto the file as it is then (the launcher may
  have saved meanwhile), under the launcher's lock, each kept only if `StatsData.Parse` accepts it; mod content values,
  settings and presets rebuilt by System.Text.Json within size limits. A save from the live pipe was the first design;
  the loader's message buffer is 1 KiB and the editor has no API to it.
- **The mods hold** (lockdown item 4) still applies: what appeared in `mods/` during the session is reported and the
  mods it appeared in switched off, before the editor's saves are applied.

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

- **Idle frame** (`RunCallbacks`, about 400 a second in the census): one read of the ring's write position, compared
  with the stand-in's own. 0 system calls, 0 pipe messages, no allocation. Unmeasured in game.
- **A frame with callbacks:** per callback, one slot copied (at most 1016 bytes) and two memory fences; no system call.
  Unmeasured in game.
- **The broker's pumping** runs in its own process every 16 ms, not on the game's thread.
- **A bridged call:** one pipe round trip (two system calls in the game). `SteamAPI_IsSteamRunning` is one: 2,014 in
  the census's 214 s, about 9 a second. Unmeasured in game; most other calls happen once or on events.
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

   Fixture tests use a fake Steam DLL: `broker_test.c` (every engine build) drives both ends. It covers the ring's
   bounds against a game that writes its header, the pipe's label and grant, a second process with the wrong pid, a
   lying HELLO, malformed and oversized calls, a late broker, a dead broker, the game exiting, and the stand-in itself
   in bridge mode (Valve's DLL never loads; without a broker, init fails closed).
3. **Generated proxies:** `SdkShimGen steam-bridge`, from the pinned SDK and the game's DLL, with the allow-list as a
   reviewed table. Fixture tests per parameter kind (strings, buffers, structs, the `CSteamID` hidden pointer).
4. **A low-integrity session:** the token and job, labelled folders, pipe labels, and the editor's saves through the
   backend. In game, solo then co-op on two PCs: no STEAM NOT DETECTED, login, a mission, a controller, an invite, and
   per-frame cost measured.

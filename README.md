# HellForge

**Offline HELLDIVERS 2 with a modding SDK.** HellForge runs your own copy of HELLDIVERS 2 against a local backend on
your PC, loads mods into it (new stratagems, weapons, stats, difficulties) and lets you play solo or with friends who
run HellForge too. It comes with an in-game editor (F8) and a C mod SDK with docs and examples.

**Status: beta (0.1.7-beta)**, for modders first. Expect rough edges; the version and game build it targets are in the
launcher's bottom-left corner.

## How it works

- **Your own game.** Setup copies HELLDIVERS 2 from your Steam library into HellForge's folder and checks every file
  against Steam's hashes. No game files are distributed, and your normal Steam game is never changed.
- **Steam ownership.** A genuine Steam ownership check runs at setup and before every launch: Steam must be running and
  signed in to an account that owns the game or has it through Steam Family Sharing.
- **Offline.** The copy talks to a local backend on your PC instead of Arrowhead's online services; a network block
  in the Windows Filtering Platform stops it reaching anything but this PC. It never connects to official servers, so it cannot be used to play online
  with a modified game. Multiplayer is co-op with other HellForge players only (below). The galactic war can follow
  the live war read-only from the community mirror [api.helldivers2.dev](https://api.helldivers2.dev), stay HellForge's
  frozen war, or open every planet to liberation (Settings > Galactic war).
- **Profiles** are local. A new profile starts with every item unlocked; other templates can be picked.
- **Co-op with other HellForge players.** You host or join Steam friends who run HellForge, and public lobbies of other
  HellForge players show on the Galactic Map: HellForge lists them on Steam's lobby service, never on Arrowhead's. A
  public lobby shows its players' names and its host's Steam account id to anyone who looks.
  - Joining a lobby whose host runs other gameplay mods offers to download them and restart with the host's mods. Mods
    that run code (a DLL, sources or Lua) download only when HellForge has verified that exact version.
  - A blocklist hides lobbies running mods you don't want. Lobbies whose host plays with edited stats (which you would
    play with) are hidden unless you show them on the Mods page.
  - There is no play with the normal game's players.
- Everything HellForge writes stays inside its folder, apart from the network block and optional shortcuts.

## Install (players)

**You need:** Windows 10 or 11 (64-bit); Steam running and signed in to an account that owns HELLDIVERS 2 (or has it through Steam Family Sharing); about 25 GB
free on the drive you install to (plus about 2 GB while a patch pack is applied, or about 50 GB in total if Steam has to
download the supported build); permission
to approve one Windows administrator prompt. Windows Firewall does not need to be on: the block lives in the Windows
Filtering Platform and holds even when a security suite has switched the firewall off. Windows SmartScreen may warn that the setup
program is unrecognised: it is not code-signed (**More info > Run anyway**).

**Linux (experimental):** HellForge runs under Proton with its game in a network namespace instead of the Windows block;
setup still runs on Windows. See [docs/linux.md](docs/linux.md).

Run `HellForge-Setup.exe` and pick a folder (default `Documents\HellForge`, or your user folder when Documents is in
OneDrive; Setup refuses OneDrive and linked folders). It unpacks HellForge there and opens the launcher, whose setup
wizard:

1. finds HELLDIVERS 2 in your Steam libraries and checks it is the supported build (Steam build 25480438). When Steam
   has a newer build that HellForge has a patch pack for, setup downloads the pack (about 1.7 GB, checked against its
   SHA-256) and makes the copy by patching your own Steam files. Otherwise (another build, or no HELLDIVERS 2) Steam
   itself downloads the supported build: the wizard shows one `download_depot 553850 <depot> <manifest>` command per
   depot (the manifests pinned in `supported-build.json`) to paste into the Steam console (`steam://open/console`),
   watches `<Steam>/steamapps/content/app_553850/` and continues when every depot is complete, then copies from there
   and can delete the download. The normal game is not touched either way;
2. confirms with Steam that the signed-in account owns the game, or has it through Steam Family Sharing (when a patch
   pack is used, after the copy, with the copy's Steam library);
3. copies or patches the game's 135 depot files (about 24 GB) from your own Steam library, each checked against
   Steam's hashes; mods and other files in the Steam folder are not copied, and an interrupted copy continues where it
   stopped;
4. places the network block for this folder's copy (the only Windows administrator prompt) and checks that it blocks;
   setup does not finish until it is in place;
5. optionally imports game settings from an earlier HellForge setup and creates shortcuts.

Everything HellForge and the copied game write stays inside the folder: `app\` (program), `game\` (the copy),
`userdata\` (game settings, saves, shader cache, crash reports; `settings.ini` is pointed here each session),
`profiles\` (HellForge progress), `logs\` and `temp\`. The game's Steam Cloud files (loadout, title, player card,
controls) stay in each profile's `userdata\profiles\<name>\cloud\` instead of the Steam account's cloud, which the
normal game shares. Outside the folder there are only the network block
and the optional shortcuts. To uninstall: Settings > Installation > **Uninstall HellForge** removes the network block and shortcuts, then deletes the folder (game copy, profiles, mods and all) once the launcher has closed. Start HellForge with `HellForge.exe` at the top of the folder. Running `HellForge-Setup.exe` on an existing
installation updates the app and keeps the game, profiles and settings; Settings > Installation > Check and repair
re-verifies the copy. Three mods are bundled, built: the in-game editor (F8), the Hell Forge difficulty (level 11) and
Vanilla Plus (small fixes: beacons stick, hellpods steer over any ground, reinforcements drop at their beacon). Before
each session they are put into `mods\hellforge-editor`, `mods\hell-forge` and `mods\vanilla-plus` (switch any off on
the Mods page), the editor's icons and fonts and the difficulty's icon made from the copy. An update brings their new
versions; an older copy downloaded separately (another folder, or a zip) is moved to `mods-retired` and the editor's
settings and presets come along. Every player's game copy is made from their own Steam installation: copied as it is when Steam
has the supported build, patched from it with HellForge's patch pack (binary differences between the two builds, plus a
few small files the newer build no longer has) when Steam has a newer one, or copied from Steam's own download of the
supported build on their account. An installation keeps working when Steam later
updates the normal game: sessions check the copy, not the Steam installation. Each HellForge release targets one HD2 build.

Building the setup: `scripts/Package-HellForge.ps1` writes `artifacts/package/HellForge-Setup.exe`. `scripts/Publish-HellForge.ps1 -InstallRoot <folder>` updates an installation's app
directly during development. The supported build's file list (`src/HellForge.Core/supported-build.json`, names and hashes
only) comes from Steam's depot manifests via `HellForge.Cli supported-build`.

## Making mods

Start with **[docs/first-mod.md](docs/first-mod.md)**: copy the template mod (`app/sdk/template-mod`) into the
`mods` folder, rename it, and HellForge builds it, fetching its pinned compiler (Zig) by itself. The Mods page adds
mods from their zips (**Add mods...**), removes them, and packs yours to share (**ZIP**). Then:

- [docs/sdk-reference.md](docs/sdk-reference.md): every API call, component field and constant (generated).
- [docs/mods.md](docs/mods.md): how each part works: definition blocks, new stratagems, entities, abilities,
  selection categories, events, live editing, data specs, stats mods.
- `examples/mods` (in an installation, `app/sdk/examples/mods`): worked examples, from a one-line weapon edit to the
  eleven-stratagem Stratagem Compendium and the Hell Forge difficulty.
- [docs/modding.md](docs/modding.md): building mods in this repository, and IDE setup.

Mods ship their C sources; every player's HellForge builds them with the same pinned compiler, so a mod is the same on
every machine and follows game updates when its field names still exist. A code mod runs inside the game with your
permissions: only enable code mods from people you trust.

## Building from source

Windows 10 or 11, the .NET 10 SDK, and HELLDIVERS 2 installed through Steam.

```powershell
./scripts/Test.ps1                # build and integration checks
./scripts/Package-HellForge.ps1   # artifacts/package/HellForge-Setup.exe
```

This repository is HellForge's open part: the launcher, setup, command-line tool, the mod SDK, its docs and examples, the
in-game editor and the mod tooling. The engine (the session runner, the local backend, the mod loader and the SDK
stand-ins) is closed and comes as a package of built binaries, `engine/hellforge-engine-<version>.zip`, pinned by
SHA-256 in `engine.lock.json` (`LICENSE-ENGINE.txt` inside it); `scripts/Publish-HellForge.ps1` checks it and puts it
into the app. Mods are built
against the SDK headers here and do not need the engine's sources.

`Launch.cmd` builds and opens the launcher from the repository. Downloads are pinned by SHA-256 and kept in the ignored
`tools/` folder (`scripts/Get-BuildTools.ps1`: Zig and MinHook); build outputs and test fixtures go to the ignored
`artifacts/` folder.

## Credits

- [Dear ImGui](https://github.com/ocornut/imgui) (MIT) draws the in-game editor; [MinHook](https://github.com/TsudaKageyu/minhook)
  (BSD-2-Clause) hooks for the mod loader and the editor; [Zig](https://ziglang.org) compiles native code and mods. All are downloaded at
  build time, pinned, and their licences ship with what uses them.
- The live war comes from the community [Helldivers 2 API](https://github.com/helldivers-2/api).
- Linux support was worked out by **DreamyBranch** and **joshua-software-dev** ([issue 1](https://github.com/SkyeShade/HellForge/issues/1)).

HellForge is an independent fan project, not affiliated with or endorsed by Arrowhead Game Studios or Sony Interactive
Entertainment. HELLDIVERS is a trademark of Sony Interactive Entertainment.

## Licence

HellForge is **source-available**, not open source: you may read it, build it, modify it for your own use and send
changes back as pull requests, but not redistribute or reupload it, or use it commercially, without permission
([LICENSE](LICENSE)). The **mod SDK, the mod template and the example mods are MIT No Attribution**
([LICENSE-SDK](LICENSE-SDK)): mods made with them belong to their authors, under any licence they choose, with no
notice required. Share HellForge by linking to this repository or its releases.

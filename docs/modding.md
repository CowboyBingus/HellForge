# Making a mod

A HellForge mod is a folder in `<installation>/mods` (or a zip of one) with a `hellforge-mod.json`. What it can hold is
described in [mods.md](mods.md): game patch files in `data/`, small JSON edits (`networkFieldTypes`,
`componentWrites`), and code, a DLL written in C or C++ against the mod SDK. Only code mods need building; this page
is about them. **New to it? Start with [first-mod.md](first-mod.md)**: a copy of `app/sdk/template-mod` and the launcher's **BUILD** make one
from HellForge's template (`templates/c-mod`; `app/sdk` in an installation) in a few minutes. The generated
[sdk-reference.md](sdk-reference.md) lists every API call, field and constant.

The rest of this page describes working in the HellForge source repository (its `scripts/`, `native/` and `examples/`
folders). In an installation the same things are the launcher's Mods page, `HellForge.Cli` in `app/cli`, the SDK in
`app/sdk` and the examples in `app/sdk/examples/mods`.

## What a code mod looks like

`examples/mods/golden-liberator` is the reference:

```
golden-liberator/
  hellforge-mod.json       id, name, version, "gameplay": true, "code": {"dll": "golden_liberator.dll", "sdk": 2}
  src/golden_liberator.c   the code; its hellforge_mod_init runs when the game starts
  CMakeLists.txt           hellforge_add_mod(golden_liberator src/golden_liberator.c)
  CMakePresets.json        the toolchain (pinned Zig), Ninja, and where to copy the built mod
  data/                    the gold model (game patch files built from your own game; never committed, see below)
  golden_liberator.dll     built here, next to the manifest
```

The SDK is one header to include, `native/mod-sdk/hellforge_mod.h`: the `HfApi` table the game hands the mod (logging,
memory, hooks, events, network field types, component records, `add_weapon`...), with each function documented where
it is declared. It includes two headers generated from `mappings/`: `hellforge_components.h` (component records with
named fields, for `HF_FIELD` and `hf_record`) and `hellforge_content.h` (vanilla content constants); see
[mods.md, Named fields](mods.md#named-fields-and-definition-blocks-sdk-6). The loader (`native/mod-loader`) loads the DLL inside the game, calls `hellforge_mod_init`, and delivers
events as the game starts (`HF_EVENT_GAME_LOADED`, `HF_EVENT_NETWORK_READY`, `HF_EVENT_ENTITIES_READY`).

## Tools

Mods are compiled by Zig (a C/C++ compiler in one folder), the same one HellForge's own native code is built with, so
no Visual Studio is needed. Fetch it, CMake and Ninja once, pinned and checksummed, into `tools/`:

```
scripts/Setup-ModTools.ps1
```

## Working in Rider

1. Run `scripts/Setup-ModTools.ps1` once.
2. In Rider: **Settings > Build, Execution, Deployment > Toolchains** (or **CMake**, depending on the version), set the
   CMake executable to `<repository>/tools/cmake/bin/cmake.exe`. That is the only IDE setting; the compiler, Ninja and
   the SDK come from the mod's `CMakePresets.json`.
3. **File > Open** the mod folder (`examples/mods/golden-liberator`) or its `CMakeLists.txt`. Rider loads it as a CMake
   project with the `default` preset; code completion and navigation into `hellforge_mod.h` work from there.
4. **Build** compiles `golden_liberator.dll` and copies the mod (manifest, DLL, `data/`) into
   `<Documents>/HellForge/mods/golden-liberator`. To copy somewhere else, add a `CMakeUserPresets.json` (not committed)
   that inherits `default` and sets `HELLFORGE_INSTALL`, or set it to empty to only build.
5. Start HellForge from its launcher; the Mods page lists the mod. The game loads the new DLL at the next session (a
   running game keeps the one it loaded).

CLion and VS Code (CMake Tools) open the same folder the same way.

From a terminal instead:

```
cd examples/mods/golden-liberator
../../../tools/cmake/bin/cmake --preset default
../../../tools/cmake/bin/cmake --build --preset default
```

or, without CMake, `scripts/Build-Mod.ps1 examples/mods/golden-liberator` (compiles `src/*.c` into the manifest's DLL;
it does not copy the mod anywhere).

## Seeing what it did

Each session's log folder (`<installation>/logs/sessions/<id>`) has:

- `mod-<mod id>.log`: everything the mod writes with `api->log`, plus what the SDK did for it (for `add_weapon`: the
  records, network type and text it got).
- `modloader.log`: the loader itself, which mods loaded, events and the weapon registry.
- `arsenal.tsv`: every armory item's loadout id, type, entity and name, for choosing `HfWeapon.base`.
- `engine-logs/`: the game's own console log and any crash dump.

## Starting your own

Copy `app/sdk/template-mod` into the mods folder and rename it, or use `HellForge.Cli new-mod --id yourname.my-mod
--name "My Mod" --install <installation>` (`--out <folder>` in the repository): it fills HellForge's template in for you
([first-mod.md](first-mod.md)).
`HellForge.Cli build-mod --mod <folder>` builds any code mod with the SDK's pinned Zig, in an installation or the
repository. To start from an example instead, copy the golden-liberator folder, then change:

- `hellforge-mod.json`: `id` (`yourname.mod-name`), `name`, `version`, `description`, `code.dll`;
- `CMakeLists.txt`: the project name and `hellforge_add_mod(<dll name without .dll> <sources>)`;
- `src/`: your code. A weapon's `unit` name must be unique: prefix it with your name and the mod's.
- `hellforge-data.json`: the names of your copies (the same prefix; the weapon's `unit` is the unit copy's `name`).

## Models and textures

The gold Liberator's `hellforge-data.json` describes its model: the Liberator's unit, body material and colour texture
copied under the mod's own names, recoloured gold. HellForge builds those files on each player's machine from their own
game copy when a session starts (once, then cached), so the mod never contains game files. The weapon's `unit` must exist
in the game's data, so this mod needs its data spec. The format, with effect colours, material colours, sound banks and
stratagem icons: [mods.md, Data specs](mods.md#data-specs). `scripts/Build-Mod.ps1 <mod folder> -Install <installation>`
builds the data right away and reports anything the spec names that the game does not have.

## Sharing a mod

Zip the mod folder (manifest, DLL, `hellforge-data.json` and its images, any hand-made `data/`) or share the folder;
players drop it into `<installation>/mods`. Friends
playing together need the same gameplay mods with identical files: the launcher's modlist code covers each mod's
files, but not editor and build leftovers (dot folders such as `.idea`, `*.user` files, a `build/` folder). Code runs
inside the game with the player's permissions: only use code mods from people you trust.

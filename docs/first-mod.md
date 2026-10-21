# Your first HellForge mod

A code mod in C, from nothing to in game. It takes about ten minutes, most of it the one-time tools download. The SDK
reference ([sdk-reference.md](sdk-reference.md)) lists every field and constant; [mods.md](mods.md) explains how each
part works. In an installation these files are in `app/sdk/docs`.

## 1. Make the mod

Copy the folder `app/sdk/template-mod` of the installation into its `mods` folder and rename the copy after your mod
(`mods/liberator-tweaks`). Then open its `hellforge-mod.json` and change:

- `id`: `owner.mod-name` in lowercase (`yourname.liberator-tweaks`). It names the mod everywhere: its log, its
  content's identities, the modlist friends compare. Two mods with one id load neither, so always change it;
- `name`: what players see (`Liberator Tweaks`), and its `description`.

```
mods/liberator-tweaks/
  hellforge-mod.json   id, name, version, "code": {"dll": "my_mod.dll", "sdk": 11}
  src/my_mod.c         the mod (rename it and "dll" if you like: any name works)
  CMakeLists.txt       for IDEs (Rider, CLion, VS Code)
  CMakePresets.json
```

Press **Refresh** on the launcher's **Mods** page and the mod is there. From a terminal, `HellForge.Cli new-mod --id
yourname.liberator-tweaks --name "Liberator Tweaks" --install <installation>` makes the same folder with your names
filled in.

## 2. Get the tools (once)

Code mods are compiled by Zig, which HellForge fetches into the installation's `tools` folder: pinned and checksummed,
nothing installed system-wide. The Mods page shows **Install mod tools** until it has them; CMake and Ninja come too,
for IDEs. From a terminal: `HellForge.Cli mod-tools --install <installation> --ide yes`. In the repository,
`scripts/Setup-ModTools.ps1` does the same.

## 3. Build it

Nothing to do: HellForge builds every mod that has its sources (`src/`) when a session starts, and keeps the build
until the sources change (see [mods.md](mods.md), "Source mods"). Press **BUILD** on the mod (or `HellForge.Cli build-mod
--mod mods/liberator-tweaks`) to compile it now and see any errors on its card at once. The mod is in the mods folder
already, so it is installed: switch it on, press **PLAY**, and the AR-23 Liberator fires at 1200 rounds per minute with
no recoil. Its log,
`logs/sessions/<session>/mod-yourname.liberator-tweaks.log`, says `AR-23 Liberator: 1200 rounds per minute`.

Builds wait while a session runs (the game has the DLL loaded); a change to the sources loads from the next session.
A mod that does not compile stays off for that session (the session still runs), and its card shows the compiler's
message with the file and line.

## 4. Make it yours

Open `src/liberator_tweaks.c`. The template is one definition block:

```c
static const HfSet liberatorSet[] = {
    {HF_FIELD_AT(ProjectileWeapon, fire_rate, 1), HF_F32(1200)},
    HF_SET_GROUP(WeaponData, recoil, HF_F32(0)),
    {0}
};
static const HfEditDef liberator = {.entity = HF_ENTITY_AR23_LIBERATOR, .set = liberatorSet};
```

- **Fields** have names: `HF_FIELD(Component, field)`. The components are in the reference (`HfWeaponMagazine`'s
  `capacity`, `HfWeaponData`'s `horizontal_spread`...). A misspelt name does not compile, and an IDE completes them.
- **Values** carry their type: `HF_F32(1200)`, `HF_U32(60)`. The loader refuses a value of the wrong type or out of
  range, with your source line, in the mod's log.
- **Vanilla content** has constants: `HF_ENTITY_...`, `HF_ITEM_...`, `HF_STRATAGEM_...`, `HF_TRAIT_...`
  (`hellforge_content.h`; every name is in the reference).
- **When it applies**: the loader applies the block whenever the game loads its data. You never time it with events;
  events are for logic (the template logs a line on `HF_EVENT_ENTITIES_READY`).

A bigger magazine is one more line: `{HF_FIELD(WeaponMagazine, capacity), HF_U32(60)},`. Build again and play.

## 5. Go further

- **New content**: `register_entity` with an `HfEntityDef` (a copy of a vanilla entity with its own records and
  changes), `add_stratagem`, `add_weapon`, `add_behaviour`, `add_stratagem_category`. Worked examples ship in
  `app/sdk/examples/mods` (in the repository, `examples/mods`): `stratagem-compendium` adds eleven stratagems, one source
  file per family, under a selection heading of their own; `stratagem-categories` shows headings and their order;
  `machine-gun-10k-code` edits an existing weapon. To try one, copy its folder into the installation's `mods` folder.
  `hell-forge` (difficulty 11) is already installed, built: to work on its source, replace `mods/hell-forge` with the
  example's folder (a copy with `src/` is left as it is), not a second folder, since two mods with one id load neither.
- **Stats without code**: the in-game editor (F8) edits values live and saves them into the Local Stats mod; EXPORT on
  its PRESETS page shares them. The same fields are in the reference.
- **Calls alongside blocks**: `hf_set`, `hf_copy`, `hf_live` for logic; `hf_record` for a typed pointer to a record.
- **Share it**: press **ZIP** on the mod's card. HellForge builds it first (a mod that does not compile is not packed),
  then writes `exports/<folder>-<version>.zip` in the installation and shows it in Explorer: the manifest, `src/`,
  `images/` and data files, without a DLL or IDE leftovers (`HellForge.Cli package-mod --mod <folder>` does the same).
  Raise `version` in `hellforge-mod.json` for each release. Players need no DLL and no tools setup of their own beyond
  the one-time download HellForge does by itself: their HellForge builds the sources. The zip goes straight into the
  `mods` folder, unpacked or not (but not both: two copies of one id load neither). Friends who play with you need the
  same gameplay mods; the Mods page shows the modlist code to compare.

A code mod runs inside the game with your permissions: only enable code mods from people you trust.

# HellForge modding SDK: roadmap

Status: phases 1 and 2 implemented (SDK 6), phase 3's foundation (live editing) built, 2026-10-17; the rest is design. `docs/mods.md` stays the reference for what
exists today.

## Goal

Modding Helldivers 2 should feel like making a Minecraft Forge mod: create a mod from a template, register content by
name, change game data through named fields, build with one command, and see it in the launcher.

- **C SDK** (the main path): typed, mapped access to the game's data, registration of new content, events and logic.
- **Stats data** (`hellforge-stats.json`, exists): named value edits. It is also the save format of the editors: the
  launcher's stats editor today, and a live editor later (in the launcher or a bundled in-game C mod that edits through
  the public SDK and saves into a mod's stats file).

Decided 2026-10-17: no JSON *content* layer for now (new stratagems, entities and behaviours as data files). It would
duplicate the C layer, which is cleaner for content (types, autocomplete, logic), and cost time the C SDK needs. It
comes back only if an editor ever has to create new content without code.

## What is hard today (examples/mods/pelican-support)

| Problem | Example | Fix (phase) |
|---|---|---|
| Raw offsets into component records | `memcpy(weapon + 237, hmg + 237, 1)` is "the firing sound" | Named fields (1) |
| Two halves of every mod | register at init, edit records at `HF_EVENT_ENTITIES_READY` | Deferred edits applied by the loader (1) |
| Magic numbers | `flags = 0x4800`, `PELICAN_PACKAGE 0x131FE264B7380717`, `NO_CREDIT_TAG` | Named traits, tags, inferred packages (2) |
| Vanilla content by hash | `HF_ENTITY_GATLING_SENTRY`, `0x2152D5147B0AC418ull` | Lookup by catalog name (2) |
| No template or reference | copy an example and read docs/mods.md | MDK and generated reference (4) |

## Principles

1. **Names over offsets.** A mod never writes an offset. Fields come from the *mappings* (below), like Forge's mapped
   names over obfuscated ones.
2. **Deferred registration.** Mods declare at init; the loader applies each edit when its data exists and again when
   the game reloads it. Mods only use events for logic, not for timing.
3. **Stable identities.** Mod content is `modid:name` (`skyeshade.pelican-support:pelican_gatling`); its resource ids
   derive from that, so every player's game agrees (as `add_weapon` and `add_entity` already do).
4. **Compatible.** SDK 1..5 mods keep working: functions are only added at the end of `HfApi`, structs keep their
   `size` field. Version 6 starts the new layer.
5. **Data-level.** New content is real game data and registry entries, never per-shot or per-spawn substitutions
   (the project's rule since the weapon registry).
6. **Refuse loudly.** A name that does not resolve, a field outside the reviewed layout or a guard that fails is
   reported in the launcher and the session log with the mod and line, never silently skipped.

## Mappings

One machine-readable source names every field the SDK can touch. It grows out of `src/HellForge.Core/stats-schema.json`
(already: projectile, damage, stratagem and explosion settings, and parts of 20 component types) and the research under
`artifacts/` (for example the Pelican mod's ProjectileWeapon fields: round, rate, casings, sound events).

- `mappings/components.json`: per component, `name -> {offset, type, count, meaning, reviewed build}`.
- `mappings/catalog.json`: vanilla content by name: items, entities, stratagems, behaviours, projectiles, damage,
  objectives, mission rows, with their ids and resources.
- Generated from them: `native/mod-sdk/hellforge_fields.h` (compile-time constants for C), the loader's runtime table
  (names resolved at load), the JSON schema, and the editor's pickers.
- Every entry carries the game build it was reviewed on; a build change revalidates (as `stats-schema.json` does today).
- Naming grows over time: an `_unknown_XX` range becomes a named field by adding one entry (offset, type, meaning,
  build, verified) and regenerating; the padding around it splits and nothing else moves. Adding a name never breaks a
  mod; renaming keeps the old name as a deprecated alias for a while. Unverified fields are marked in the header.
  Discovery tools (live snapshots, a byte diff between similar entities) and outside contributions (one entry plus
  evidence) feed the same file.

## C SDK version 6 (sketch)

Decided 2026-10-17: two styles over one registry.

- **Definition blocks (main style):** one struct per thing, registered once, easy to read. Lists are named arrays
  ending in `{0}`; `HF_F32` / `HF_U32` tag a value with its type; unset fields mean the base's.
- **Calls (alongside):** one call per change on a handle, for logic and runtime edits.

```c
static const HfCopy gatlingTurretCopies[] = {
    {"A/G-16 Gatling Sentry",    "ProjectileWeapon.fire_rate"},
    {"MG-206 Heavy Machine Gun", "ProjectileWeapon.round"},
    {0}
};
static const HfSet gatlingTurretFields[] = {
    {"WeaponData.recoil",       HF_F32(0)},
    {"WeaponMagazine.capacity", HF_U32(2000)},
    {0}
};
static const HfEntityDef gatlingTurret = {
    .name = "gatling_turret",
    .base = "Pelican Turret",
    .copy = gatlingTurretCopies,
    .set  = gatlingTurretFields,
};
/* in hellforge_mod_init */
HfRef turret = hf->register_entity(mod, &gatlingTurret);
```

### Typed access (generated headers), decided 2026-10-17

C mods do not use strings for fields or vanilla content: the mappings generate typed headers per game build, so typos
fail to compile and the IDE completes every component, field and item (Forge's mapped classes, for HD2's component
records). Strings stay for JSON and runtime lookups; both come from the same mappings.

- `hellforge_components.h`: a struct per component record, named fields at their offsets, unresearched bytes as
  `_unknown_XX` padding, `HF_ASSERT_SIZE` on the stride, and the game build it was generated for (the loader refuses a
  mod built against another layout).
- `hellforge_content.h`: constants for vanilla content (`HF_ENTITY_GATLING_SENTRY`, `HF_ITEM_MG206_HEAVY_MACHINE_GUN`, ...).
- `HF_FIELD(Component, field)`: component, offset and type as a compile-time constant, for definition blocks:
  `{HF_FIELD(WeaponData, recoil_up), HF_F32(0)}`.
- `hf_record(mod, ref, HfWeaponData)`: a typed pointer for runtime code (`data->recoil_up = 0.0f;`).
- Game functions are exposed as wrapped SDK calls (spawn, give, damage...), not raw addresses, since those move between builds.

The same content in the call style (the older sketch below):

```c
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    /* Behaviours, entities and stratagems keep their structs (ABI-stable), now with named references. */
    HfRef orbit = hf->add_behaviour(mod, &(HfBehaviour){sizeof(HfBehaviour), .base = HF_BEHAVIOUR_PELICAN_TRANSPORT,
        .hold_seconds = 90, .orbit_radius = 40, .orbit_height = 60, .face_target = 1});

    HfRef turret = hf->entity(mod, "pelican_gatling_turret", "Pelican Turret");          /* modid:name, from a vanilla name */
    hf->copy_fields(mod, turret, "MG-206 Heavy Machine Gun", "ProjectileWeapon", "round, casings, sound");
    hf->copy_fields(mod, turret, "A/G-16 Gatling Sentry", "ProjectileWeapon", "fire_rate");
    hf->set_f32(mod, turret, "WeaponData.recoil", 0);                                     /* deferred: applied when ready */
    hf->set_u32(mod, turret, "WeaponMagazine.capacity", 2000);
    hf->set_flag(mod, turret, "credits_kills", 1);

    HfRef pelican = hf->entity(mod, "pelican_gatling", "Pelican Transport");
    hf->set_model(mod, pelican, "Pelican Gunship");
    hf->mount(mod, pelican, 0, turret);
    hf->set_behaviour(mod, pelican, orbit);

    HfRef gatling = hf->stratagem(mod, "pelican_gatling_support", "EXO-45 Patriot");
    hf->set_text(mod, gatling, "Pelican Gatling Support", "Calls down a Pelican for close air support...");
    hf->set_code(mod, gatling, "LDLLUU");
    hf->set_drop(mod, gatling, pelican);                  /* packages inferred from the Pelican and its turret */
    hf->set_traits(mod, gatling, HF_TRAIT_MACHINE_GUN | HF_TRAIT_HEAVY_ARMOR_PIERCING);
    return 0;
}
```

- `HfRef`: an opaque 64-bit handle for mod or vanilla content; `hf->find(kind, "name")` resolves vanilla content by
  catalog name. Raw ids stay available for what the catalog does not cover yet.
- Setters queue *deferred edits* with the mod's name for errors; the loader applies them at the right moment
  (entities ready, tables installed) and renews them when the game reloads its data.
- `component_record` stays for code that needs raw access; named fields are the recommended path.
- A small header-only kit (`hellforge_kit.h`) on top of the API for common recipes (air support Pelicans, sentries,
  orbital strikes), the way Forge mods share helpers. Not part of the ABI.

## JSON content (`hellforge-content.json`): dropped for now (see Goal); the sketch is kept for reference

```json
{
  "format": 1,
  "behaviours": { "pelican_orbit": { "base": "Pelican Transport", "hold": 90, "orbit": { "radius": 40, "height": 60 }, "faceTarget": true } },
  "entities": {
    "pelican_gatling_turret": {
      "base": "Pelican Turret",
      "copy": [ { "from": "MG-206 Heavy Machine Gun", "component": "ProjectileWeapon", "fields": ["round", "casings", "sound"] },
                { "from": "A/G-16 Gatling Sentry", "component": "ProjectileWeapon", "fields": ["fire_rate"] } ],
      "set": { "WeaponData.recoil": 0, "WeaponMagazine.capacity": 2000, "credits_kills": true },
      "behaviour": { "base": "Pelican Turret", "reserveRounds": 2000, "aimHeight": 0.15 }
    },
    "pelican_gatling": { "base": "Pelican Transport", "model": "Pelican Gunship", "mount": ["pelican_gatling_turret"], "behaviour": "pelican_orbit" }
  },
  "stratagems": {
    "pelican_gatling_support": {
      "template": "EXO-45 Patriot", "drops": "pelican_gatling",
      "title": "Pelican Gatling Support", "code": "LDLLUU", "cooldown": 300, "uses": 4,
      "category": "offensive", "traits": ["machine gun", "heavy armor piercing"], "icon": "images/pelican_gatling_support.png"
    }
  }
}
```

Every key maps to one C call above. The launcher validates the file against the mappings before a session (names,
types, ranges) and shows errors with file and line; the loader runs it like a code mod.

## Mod conflicts, events and hooks (decided 2026-10-17)

Declarative content makes conflicts visible instead of silent (unlike script mods that overwrite tables at run time):

- **Isolation first:** mod content is `modid:name`, numbered in load order, and new content owns copies of its records
  (`own_components`), so most mods never share data.
- **Deferred edits know every writer:** before applying, the loader has every mod's edits. Two mods setting the same
  shared field are reported in the launcher (both mods, the field, the winner) and resolved by a stated rule: load
  order by default, or a priority set by the mod or the user. Later: merge rules for list-like data (append instead of
  replace). Today's stats layer already reports "last edit wins" conflicts (`Mods.cs`); phase 1's deferred edits use
  the same reporting.
- **Events over hooks:** raw hooks (`api->hook`) are the equivalent of Minecraft Mixins: powerful, but two mods patching
  the same game code can break each other, and their addresses move with every game build. Common needs become SDK
  events that many mods subscribe to in a defined order (the Forge event bus). First candidates: mission start and end,
  level generated, entity spawned, damage dealt or taken (modifiable and cancellable), stratagem called, objective
  completed, player joined or left. Every event added removes a reason to hook.
- **Managed hooks for the rest:** the loader chains detours on the same function (each calls the next, nobody silently
  replaces another), honours a requested priority (earlier or later, like Mixin priorities), and the launcher lists
  which mods hook what and flags functions hooked by more than one mod.

## Phases

1. **Named fields and deferred edits (C).** Mappings for the components the example mods touch (ProjectileWeapon,
   WeaponData, WeaponMagazine, Tag, Mount, Behavior, LoadoutPackage), the runtime field table, `copy_fields`,
   `set_*`, deferred application and renewal. Port pelican-support to it with no offsets. Tests in the native fixture.
   **Done (SDK 6):** `mappings/components.json` (7 components, 91 fields, 4 groups) and `mappings/catalog.json`
   generate `hellforge_components.h`, `hellforge_content.h` and the loader's `sdk_fields.h` (`HellForge.Cli
   sdk-headers`; an integration check keeps them in step). `HfEntityDef` with `HfCopy`, `HfSet`, tags and
   `HfMountDef` lists; `register_entity` infers the records an entity owns, registers what it mounts and its behaviour
   once, and refuses a block with any unknown field, other layout or ill-typed value (logged with the source line).
   Calls alongside: `hf_copy`, `hf_set`, `hf_tag`, `hf_mount`, `hf_record`. Edits apply before
   `HF_EVENT_ENTITIES_READY` reaches mods, on every load. pelican-support has no offsets or field strings left; its
   stratagem `flags` stay numbers until named traits (phase 2). Naming the fields showed two of its old WeaponData
   writes used the live per-entity copy's offsets (+0x38, +0x58) on the component record: the port zeroes both recoil
   blocks and copies both spreads as its comments intended (verified in game 2026-10-17).
   Decided while building it: field groups in the mappings (`HF_COPY_GROUP`, `HF_SET_GROUP`), so "the firing sounds" is
   one line; the mount list type is `HfMountDef` (`HfMount` is the component record); an `HfMod` starts with its API
   pointer so `hf_record(mod, ref, HfWeaponData)` needs no `api` argument; values are checked against their field's
   type rather than typed at compile time, so `{HF_FIELD(...), HF_F32(0)}` reads as the roadmap sketched.
2. **Identity and lookup.** `modid:name` for all `add_*`, `find` by catalog name, `HfRef`, named traits, tags,
   categories and beams, packages inferred from payload entities. Port the remaining examples.
   **Done (SDK 6):** short names become `<mod id>:<name>` in every `add_*` and `register_entity` (full names keep
   their ids: saved loadouts keep a stratagem's stable id, so the examples keep theirs). The catalog grew to ~800
   entries (every armory item and its entity from a session's arsenal.tsv, every vanilla stratagem from the row dump
   named by the stats schema, uniquely named entities and single-owner projectiles from HD2Runtime research) plus
   kinds for traits (the row's StratagemInfoFlag bits, named from the type library's name lengths and the rows carrying
   them; bits 13 and 15 unnamed), categories and beams; `find(kind, name)` resolves catalog names and mod identities.
   Packages are inferred from a stratagem's payload when it names none. Also added: `HfEditDef` / `edit_entity` for
   existing entities, deferred edits of vanilla records no other entity shares, conflict reports between mods and
   with stats mods (the "Mod conflicts" section's first step; the launcher does not show them yet, the logs do), and
   typed structs for the settings tables' rows. Ported: pelican-support (traits, inferred package), machine-gun-10k-code
   (`edit_entity` on the Maxigun backpack, tested end to end by the loader smoke test), hell-swarm (typed difficulty
   row), hell-forge (stratagem constant), talon-liberator (short names). more-voices calls a Wwise export by name,
   which waits for wrapped game functions. `HfRef` stays the entity id; other kinds keep their u32 ids.
3. ~~JSON content layer~~ dropped for now (see Goal). Next instead: **live editing** of stats (an "applies" timing per
   mapped field; edits by identity through the deferred-edit path; the launcher editor's live mode and/or a bundled
   in-game editor C mod; saves into a mod's stats file; solo only at first).
   **Foundation built (SDK 6, docs/mods.md "Live editing"):** every stats-schema field was imported into the mappings
   (138 new fields, 4 settings tables), and every mapped field carries `applies` (now / respawn / mission, with
   `appliesVerified` where code shows the read); `set_live` on the deferred-edit path, with table rows by id, array
   elements, replace-and-reset and `HF_LIVE_SHARED`; the launcher's stats editor goes live over a pipe in solo sessions
   and saves into the Local Stats mod. Next: the in-game editor mod on the same API; then the timings checked in game,
   field by field (most are inferred).
   **Runtime edits in multiplayer: host-authoritative (decided 2026-10-17).** Edits are identity + field + value, never
   addresses, so they travel between machines safely.
   1. Joining still requires matching mods (the modlist gate; with source-first builds, matching source hashes), so
      *saved* edits (stats mods such as Local Stats) are already identical everywhere.
   2. On join, the host sends its *unsaved* runtime edit set (a versioned list of `{target, field, value}`); the joiner
      applies it through `set_live` (resolved by identity, guarded, timed by each field's `applies`). The joiner's own
      unsaved runtime edits are set aside for the session and restored when it leaves.
   3. The joiner's in-game editor turns view-only and shows the host's values ("Host: <name>").
   4. Host edits mid-session are broadcast as they happen, numbered so they apply in order; a late joiner gets the full
      set first, then the stream. Optionally a hash of the host's set lets a joiner confirm it applied the same edits.
   5. When the host leaves or the session ends, everyone returns to their own values.
   - Channel: HellForge's own peer link (the backend connection used for joining, over Steam networking), not the
     game's network code; depends on the multiplayer join work (docs/multiplayer plan).
   - Trust: joiners take the host's edits; fine for friends-only co-op, revisit for anything public.
   - Build for it now: the editor's view-only mode and an export/apply format for the runtime edit set (also useful
     for presets and "export as a mod").
   **Built (2026-10-17):** the backend's `StatsShare` sends the host's Local Stats to
   admitted friends and on every change; joiners apply them through the live-edit pipe and get their own back on leave;
   `session` (SDK 6) tells mods solo, host or joiner, and a joiner's in-game editor is read-only. Not synced: boot-only
   fields, arm swaps and perks. The editor's mod content values (`mod-stats.json`) go with them since 2026-10-18 (the
   third field of `stats.set`); a first two-PC session found them missing (a joiner kept its own silo values).
4. **MDK.** `templates/c-mod`, `hellforge new-mod <id>` (CLI and launcher button), one-command
   build and install, a reference generated from the header and mappings, a "first mod" guide. Next (decided
   2026-10-17); the reference generator is built now and fills in as the mappings grow.
   **Source-first builds (decided 2026-10-17):** a mod ships C source (`hellforge-mod.json` + `src/`); the launcher
   compiles it with the bundled, pinned Zig toolchain (same compiler and flags on every machine) and caches the DLL.
   - Cache key: a hash of the mod's source files, the SDK headers and mappings, the compiler version and flags, and the
     game build. Any change rebuilds, so mods follow a game update automatically when their field names still exist.
   - Every launch verifies both the source hash and the cached DLL's hash against the cache record; a mismatch
     rebuilds (a stale, corrupted or swapped DLL never loads). This guards against accidents and casual swaps, not a
     determined local attacker (who could edit the source or the record too).
   - Compile errors show on the mods page with file and line; that mod stays off and the session still runs.
   - The multiplayer modlist gate hashes the source, so peers build identical mods.
   - Prebuilt DLLs stay allowed (closed source, C++ or extra libraries such as an ImGui runtime editor); they declare
     the SDK version and game build they were built for and are refused with a clear message when outdated.
   Built 2026-10-17: sessions compile `src/` mods with the pinned Zig into `temp/mod-builds`, hash-checked as above.
   **Built (2026-10-17):** `templates/c-mod` (an SDK 6 definition block on the AR-23 Liberator, CMake files for IDEs);
   `HellForge.Cli new-mod` / `build-mod` / `mod-tools` (`ModKit`, `src/HellForge.Core/ModKit.cs`); the launcher's
   Mods page gets **New mod**, **BUILD** on every mod with sources, and **Install mod tools**. Installations now ship
   the SDK in `app/sdk` (headers, CMake helpers, the template and docs) and fetch the tools into `<installation>/tools`;
   the tool pins are one file, `native/mod-sdk/tools.json`, shared with `scripts/Setup-ModTools.ps1`. The reference,
   `docs/sdk-reference.md`, is generated with the headers (the API from `hellforge_mod.h`'s comments, every component,
   table and constant) and checked the same way; `docs/first-mod.md` is the guide. Not done: a template for data-only
   (stats) mods, which the in-game editor covers (its PRESETS export). The launcher's STATS page was removed
   2026-10-18: the in-game editor does all it did.
5. **More registries and events**, split in two (decided 2026-10-17):
   - **5a. The HD2R Editor, native to HellForge.** Port the HD2R Editor's fields (`HD2Mods/HD2RuntimeEditor`, from
     HD2Runtime's catalogues) into the mappings and the stats schema, and its features (Mods, Changes, Errors, Export,
     Logs, presets, export as a mod) into HellForge's stats editor, on the live path. Not its periodic re-verification
     rewrites: HellForge renews edits when the game reloads its data instead. Decided 2026-10-17: it is an **in-game**
     editor, a bundled C mod on the public SDK (`set_live`, saving into a stats file), replacing the HD2R Editor (slow and
     crash-prone on HD2Runtime's Lua path); it needs a drawing and input layer of its own, researched first.
     **Drawing layer prototype (2026-10-17):** `native/editor` (`scripts/Build-Editor.ps1`): Dear ImGui 1.91.9b (MIT,
     pinned) drawn into the game's D3D12 back buffer from hooks on Present / Present1 / ResizeBuffers and
     ExecuteCommandLists (the game's direct queue), with a fence per back buffer; F8 toggles it; while open, the game
     window's input goes to ImGui (window procedure replaced; ClipCursor, SetCursorPos and GetRawInputBuffer held back).
     One live control (the Liberator's fire rate through `set_live`). Passes `overlay_test` (a real D3D12 swap chain:
     resizes, F8, a screenshot check); tried in game (the change applies on respawn). A game started with `--use-d3d11`
     (launch options) is drawn on with ImGui's DX11 backend (2026-10-18); `HELLFORGE_EDITOR_TEST_D3D11=1` runs
     `overlay_test` on a D3D11 swap chain. Changes are saved into the Local
     Stats mod (`mods/zz.local.stats`, the launcher STATS page's save file, merged under its lock) a second after the
     last one, so the stats pass applies them at every boot; Reset removes the field and puts the game's own value back
     (`set_live` with `HF_LIVE_BASE`).
     **The editor's window (2026-10-17):** the HD2R Editor's layout and look, ported to C++ over ImGui's draw lists
     (`native/editor/app.cpp`, `canvas.cpp`): BROWSE (categories, objects with the armory's group chips and search,
     the field page in collapsible categories and parts with DEFAULT, VALUE and SOURCE columns, markers), SEARCH,
     CHANGES and SETTINGS (interface size), the status bar and APPLY / DISCARD / RESET TO DEFAULTS. 1080p units: the
     panel is 1260 units wide on a 1080-unit screen, centred, so 100% is the same share of every screen. HellForge's
     red on lighter panels; the game's FS Sinclair (its TTFs, extracted locally) and menu sounds (game.dll's UI sound
     function, checked byte for byte, posted on the game's main thread); changes and resets stay pending until APPLY.
     First categories: Primary, Secondary and
     Throwables, every stats-schema field of theirs (`EditorCatalog.cs`), with the game's HUD silhouettes
     (`mappings/hud-icons.json` names the sprites; `EditorIcons.cs` extracts them locally). Values come from
     `get_live` (SDK 6). Then the stratagems (six categories in the game's colours, their icons in two layers), boosters
     (the `BoosterSettings` table, new to the SDK and the stats schema) and the armor perks (`set_passives`, a port of
     HD2Runtime's player passives: solo only, pins proven, kept through armor changes; a picker of the passives).
     Why not HD2R's way: its engine-GUI bindings check no
     arguments and are redrawn from Lua every frame, and its writes are guarded ensures one at a time; HellForge draws
     natively and writes through set_live. Field port still to do: the HD2R Editor's non-enemy rows (~18,200 on ~12,700
     locations; HellForge has 6,676 named fields) need melee (MeleeHitVolume), armor and perks, status effects,
     helldiver, boosters, attachments beyond magazines, beam conversion and pod payloads, plus choice fields (fire
     modes, traits); ExportStatsSchema.py now runs on HD2Runtime 0.40.1 (an empty Lua table was read as a list).
     **Not enemy, structure or mission fields for now:** enemies, spawn rosters and compositions, and their damage
     zones and attacks are over half of the HD2R Editor's ~20,700 fields; structures (with demolition templates and
     destruction models) and missions (mission difficulty and the other Spawning & Destruction pages) stay out too. This
     step covers weapons, stratagems, backpacks, vehicles, boosters, and helldiver and armor.
     **Since then (2026-10-17/18), most of the port above is built:** projectile swaps, impact and expiry payloads,
     status effects and slots, donor rows; all 20 boosters; every armor perk's values; vehicle pages (damage zones,
     mounts, arm swaps); attachments (magazines, muzzles, optics, underbarrels) and Explosions; the Helldiver page, pod
     spawn counts, status multipliers; melee hit volumes; HD2R's flags, enums and slot lists (fire modes, traits); mod
     stratagems on pages of their own. Features: CHANGES, an errors list, LOGS, RESET ITEM, and **PRESETS** (HD2R
     Editor's `.hd2rpreset.json`, import stages for APPLY, export writes the saved changes; `native/editor/app.cpp`).
     In co-op the host's edits reach the joiners (step 3 above). **Left in 5a:** beam conversion; "export as a mod"
     (the saved changes as a stats mod to share); the HD2R rows still without a HellForge field (a coverage count).
   - **5b. Events and managed hooks.** Objectives, mission rows and operations (from the level-11 work), outposts and
     stamps (city copies); the event list and managed hooks (chaining, priority, launcher report) from "Mod conflicts,
     events and hooks". Enemies and spawns come back here or later, with the enemy fields 5a leaves out.
     **SDK 7 (2026-10-17/18):** `own_all` copies (each component's records sized by its index), game events through
     `subscribe` with priorities (state changes, mission start and end, player spawn and down, damage (observe-only),
     stratagem called, kill, entity spawned) and `game_state`, `content` (`HfContent`), entity text, mod stratagems in the
     in-game editor (`editor_like` / `editor_entities`, mask icons), the projectile list moved (64 new types) and
     `add_ability` (ids 2868 and up; the Shredder Silo's countdown). `examples/mods/stratagem-compendium` ports the
     HD2Runtime pack's eleven stratagems on it.
     **SDK 8 (2026-10-18): custom selection categories.** `add_stratagem_category` registers a heading in the stratagem
     selection screen, `HfStratagem.selection_category` puts a stratagem under it and `selection_order` sets its place
     there (default: call order; UI grouping only; `category` and `beam` still drive HUD, ping and beacon colours). At
     most 28 custom headings across all mods, after the vanilla sections, by `sort_order` then identity; empty ones
     hidden; refused cleanly (vanilla sections) if the picker's reviewed instructions do not match. Docs: docs/mods.md,
     docs/stratagem-categories.md (shipped in `app/sdk/docs`); example `examples/mods/stratagem-categories`; the
     Compendium now sits under one "Stratagem Compendium" heading, offensive first, then supply (blue), then defensive.
     Also fixed: the loadout unit-preview query's fallback recursed forever on the G-8 Immolation in the hellpod
     (`native/mod-loader/loadout_preview.c` breaks only that cycle). Checked in game 2026-10-18: the Compendium's
     heading shows its eleven stratagems in the set order. **Still owed in game**: controller selection, saved loadouts.
     **Left in 5b:** events for player joined/left, objective completed and level generated; damage modify/cancel
     (undecided, see data-level registration); objectives, mission rows, operations, outposts and stamps as SDK
     registries; managed hooks (chaining, priority) and the launcher showing mod conflicts and who hooks what (the
     logs report conflicts today).
6. **Editor (HellRaiser).** Catalog pickers, icon import and export over the stats data; creating new content
   without code would bring back a content format (or generate a C mod from a template), decided then.

## Research plan (feeds the mappings)

The level-11 work produced research across `artifacts/difficulty11/research-*` and earlier folders. Consolidate it into
the mappings, area by area, each ending in a machine-readable file plus a short doc:

| Area | Have | Missing |
|---|---|---|
| Component fields | stats schema (partial, 20 components), Pelican offsets | names for ProjectileWeapon, Mount, Behavior, Tag, LoadoutPackage; coverage report per component |
| Content registries | weapons, stratagems, entities, behaviours, projectiles, damage (SDK 2..4); mission rows, operations, objective defs, outposts/stamps (level 11) | one catalog with names; objectives/missions as SDK registries |
| Lifecycle | events 1..4, when tables load (registries) | per-table "ready" and "reloaded" timing, mission start/end |
| Resources and packages | what a mission loads (research-stamp-resources) | automatic package inference for new content; missing-asset checks |
| Networking | modlist gate, deterministic numbering | which new content needs network types or replication |
| Gaps | | UI and text, audio (Wwise), progression and unlocks, armory customization |

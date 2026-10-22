# Mods

An installation loads every mod in `<installation>/mods/<folder>/` that has a valid `hellforge-mod.json`, and every
`<installation>/mods/<name>.zip` holding one (at the zip's root or in its single top-level folder). Zips are unpacked
into `mods/.unpacked/<name>-<content hash>` when mods are loaded (the launcher's Mods page or a session start) and
loaded from there; deleting or replacing a zip removes its unpacked copy at the next load, so a mod is managed by its
zip alone. Zips with absolute or `..` paths (files that would land outside the mod's folder) are refused; there is no size or file
count limit. A zip and a folder
with the same files give the same modlist code. A zipped mod counts by what its zip holds: files it writes into its
unpacked folder as it runs (key bindings, a cache) are each player's own and never split friends. A mod kept as a
loose folder counts by every file in it, so share mods as zips. Each session stages
the data mods into the copied game, loads the code mods into it and applies the declarative native edits while the game
runs; nothing is changed in the normal game. Staging copies only what changed since the last session: a patch already
staged from the same file (same size and time) stays, so an unchanged modlist starts without copying anything. The zips'
hashes and the files they ship are kept in `mods/.unpacked` (`.zip-hashes.json`, `.zip-files.json`) for the same reason. The session's `result.json` lists what loaded (`mods`) and network edit
status (`nativeMods`). `native-mods.log` records network edits; `mod-<id>.log` records stats and legacy component edits.

The launcher's **Mods** page lists every mod as a row with a switch, in two groups: **gameplay** mods, then
**cosmetic** ones (`"gameplay": false` mods and cosmetic packs, below). A row's details (click it) say what it
changes; its ⋮ menu builds, zips, opens or removes it. The page also shows the mods that could not be loaded and why,
and the modlist code friends must match.

**Load order.** Rows are dragged from anywhere on them (or moved with ⋮ Move up / Move down) within their group. The
page follows the mods folder: a mod copied in or deleted there shows at once. The
**bottom mod loads first and the top one last**, as in the community's mod managers: where two mods change the same
thing, the higher one wins (its data patches get the higher numbers, its stats and its code's edits apply later). Code
mods are set up in the order of their ids, the same on every machine, so what they add is numbered alike for friends
who load them in another order.
Sessions load and stage mods in this order (`ModCatalog.LoadOrder`). Mods new to the page go to the top of their
group. The editor's Local Stats stays at the top, so what you edit in game applies last. The modlist code compares the
gameplay mods, not their order, and not the cosmetic group. Friends who order the same mods differently can still play
together, but that is theirs to sort out: a conflict goes to a different mod on each machine. What mods add
(stratagems, weapons, entities, projectiles, damage types, abilities, behaviours) is numbered alike whatever the order:
code mods are set up in the order of their ids, and added entities are numbered by their resource ids.

**A refused join.** When a friend's modlist code is not the host's, the host's backend refuses the join and both games
say why, over the game (the in-game editor draws it, open or not, for 20 seconds; the SDK 12 `notice` call): the
joiner sees "Couldn't join <host>" with each mod that differs (one the host has and they do not, one only they have,
another version, or the same version with other files), the host sees "<friend> couldn't join" with the same list from
their side. Another HellForge version, a full or a locked lobby are said the same way. Both sessions'
`multiplayer.log` keep the full list. The game's own "couldn't join" message shows as well. When the host's mods can
be downloaded, the joiner's editor also opens its MOD SYNC dialog (below).

Switches, the order and packs' options are saved in `mods/hellforge-mods.json`
(`{"format":1,"disabled":[ids],"order":[ids, top first],"packs":{id:{"off":[options],"sub":{option:variant}}},"blocked":[ids],"share":true,"showEditedStats":false}`;
mods are on by default) and apply to the next session; they are locked while a session runs. A disabled mod is not
staged, applied, checked for conflicts or counted in the modlist code.

## Playing with other mods (mod sync)

Joining a lobby whose host runs other gameplay mods does not just fail. The host's backend refuses the join (above) and
sends its mod set beside the refusal. The in-game editor then opens on a **MOD SYNC** dialog (SDK 13 `sync_state`,
`sync_mod`, `sync_answer`) listing each of the host's mods and what happens to it:

| Shown as | Meaning |
|---|---|
| you have it | an installed mod (on or off) or one downloaded before has the same id and content |
| downloads | it comes from the host's backend over the Steam peer link |
| install it yourself | the host does not share their mods, or it runs code that HellForge has not verified |
| blocked | it is on your blocklist, so the lobby cannot be joined |
| needs another HellForge | a mod HellForge ships differs: both players need the same HellForge |
| off for this session | one of your own gameplay mods that the host does not run |

**DOWNLOAD AND RESTART** downloads what it can. **RESTART TO JOIN** then ends the session and the launcher starts a new
one with the host's mods, in the host's load order, plus your own cosmetic mods and Local Stats. Join the lobby again
from the social tab or the map. Only that one session plays the host's mods: your Mods page and its switches never
change, and the next session you start yourself runs your own mods again.

**What may download (the trust rule).** Your backend decides each mod's class from the bytes it received, never from
what the host says:
- *Content* mods download from any host: data patches without Lua, data specs, stats and native edits.
- *Code* mods (a DLL, C or C++ sources HellForge compiles, or a patch holding Lua) download only when HellForge's
  verified list names that exact content: `data/verified-mods.json`, shipped with each HellForge release, entries
  `{"id","name","version","hash","reviewed"}`. The hash is the mod's content hash: the SHA-256 of the lines the modlist
  code takes from it (`ModSets.ContentHash`). Any other code mod is one you install yourself.
- *Bundled* mods (the in-game editor) never download.

**Checks.** The manifest comes first: a mod that declares code and is not verified stops there, before its other
files. Every file must have a safe path and match the size and SHA-256 the host listed. The finished mod must parse,
have the expected id and version, rehash to the host's content hash, and be content or verified code. Only then is it
kept, in `mods/.synced/<id>@<hash>/`; a failed download is deleted. Normal sessions never load that folder.

**Blocking.** **BLOCK** on a row of the dialog (or **B**) puts that mod on your blocklist (`"blocked"`): lobbies running
it are not offered again. Unblock it on the launcher's Mods page (MOD SYNC card).

**Hosting.** Players who join you may download your gameplay mods' counted files for 10 minutes after their join is
refused. Your backend serves them at most 1 MB/s per player and 2 MB/s in total, so your own game keeps its link. Turn
it off with "Let players who join me download my gameplay mods" on the Mods page (`"share": false`).

**Downloaded mods.** The Mods page shows how many there are and how much space they take, with a button that deletes
them all.

**Public lobbies on the Galactic Map.** The map lists other HellForge players' public lobbies, except the ones you could
not join:
- lobbies running a blocked mod;
- lobbies running mods that would have to be installed by hand;
- lobbies whose host plays with edited stats (Local Stats, the in-game editor's values or arm swaps). Joiners play with
  the host's stats, so these are hidden unless "Show public lobbies whose host plays with edited stats" is on
  (`"showEditedStats"`).

Lobbies with other mods you can download still show. Joining one opens the MOD SYNC dialog.

## Manifest

```json
{
  "format": 1,
  "id": "skyeshade.machine-gun-10k",
  "name": "Machine Gun 10k",
  "version": "1.0.0",
  "description": "optional",
  "icon": "optional, e.g. images/icon.png",
  "gameplay": true,
  "native": { "networkFieldTypes": [ ... ], "componentWrites": [ ... ] }
}
```

- `id`: 1-64 lowercase letters, digits, `.`, `_`, `-`; unique across the installation.
- `icon`: a PNG or JPEG in the mod folder, shown on the Mods page (square looks best); without one the row shows the
  name's initials.
- `gameplay` (default true): a gameplay mod changes how the game plays. Friends compare a digest of their gameplay
  mods (ids, versions and exact files) before a join and must match, or sync to the host's (mod sync, above);
  cosmetic mods (`false`) are not compared.
- A mod must change something: native edits, code, data files or a data spec (`hellforge-data.json`, below).

## Code mods

How to build one, with Rider or from a terminal: [modding.md](modding.md).

A code mod is a DLL its author writes against the HellForge mod SDK (`native/mod-sdk/hellforge_mod.h`, version 6), in C, C++ or
any language that can export a C function. Mods from before the rename (HD2 Beyond The Stars) keep working: the loader
reads `bts-mod.json` and calls `bts_mod_init`, and `native/mod-sdk/bts_mod.h` maps the old SDK names onto the new ones.
The manifest names it:

```json
{ "format": 1, "id": "you.your-mod", "name": "Your mod", "version": "1.0.0", "code": { "dll": "your_mod.dll", "sdk": 1 } }
```

### Source mods

A code mod that ships its sources (C or C++ under `src/`) is built by HellForge, not by its author: `HellForge.Cli
mods-stage` compiles it before every session that has it enabled, with the SDK's pinned Zig and the same flags on every
machine, into `<installation>/temp/mod-builds/<mod id>/`, and the loader loads that build. A DLL in the mod's own folder
(an IDE's build, say) is not used (`src/HellForge.Core/ModBuilds.cs`):

- **The cache.** A build is kept under its key, a hash of the mod's manifest and every file under `src/`, the SDK's
  headers, the compiler (its pinned archive) and flags, and the game build. Anything that changes builds it again, so a
  mod follows a game update by itself when the names it uses still exist.
- **The check.** `record.json` beside the build holds its key, the sources' hash and the DLL's SHA-256. A session uses
  the build only when all three still match; a stale, corrupted or swapped DLL is built again, never loaded. This guards
  against accidents and casual swaps, not against someone with access to the installation (who could edit the sources or
  the record too).
- **Errors.** A mod that does not compile stays off for that session; the session still runs. The record keeps the
  compiler's message (file and line), which the launcher's Mods page shows on the mod's card, and the same sources are not
  compiled again. **BUILD** on the card (or `HellForge.Cli build-mod --mod <mods>/<mod>`) makes the same build at once.
- **Tools.** The first source mod fetches the pinned Zig into `<installation>/tools` (checksummed) if it is not there.
- **Friends.** The modlist digest takes a source mod's sources, not a DLL, so friends who play together build identical
  mods.

A mod that ships only its DLL (closed source, or built with other tools and libraries) sets `"prebuilt": true` in `code`
when it also ships sources, and names the game build it was built for, `"game": "25480438"`: one built for another
build is refused with a message saying so. Without `"game"` a prebuilt DLL still loads (mods from before this rule).

When a session has enabled code mods, `HellForge.Cli mods-stage` writes the plan `modloader-plan.txt` into the session folder
(build pins, entity profile, mods in load order) and the session sets `HELLFORGE_MODLOADER` and `HELLFORGE_MODLOADER_PLAN` for the game.
The `steam_api64.dll` stand-in, the first HellForge code in the game's process, starts `hellforge_modloader.dll` on its own
thread; the loader loads each mod and calls its exported `hellforge_mod_init(const HfApi*, HfMod*)`, then watches the game
start and delivers events on its thread, in load order:

| Event | When | Typical use |
|---|---|---|
| `HF_EVENT_GAME_LOADED` | game.dll is loaded and its code is ready (before this, its functions cannot be found or hooked) | find functions, hook |
| `HF_EVENT_NETWORK_READY` | the engine's network configuration exists | read or widen network field types |
| `HF_EVENT_ENTITIES_READY` | the entity component region exists, with every added weapon (again after a reload) | edit component records |

The API: per-mod logging (`mod-<id>.log` in the session folder; the loader's own is `modloader.log`), `module_base`,
`find_pattern` (IDA-style, `??` wildcards), `read` / `write` (any protection; flushes the instruction cache),
`write_u32_if` (only from an expected value), `hook` (MinHook detours with a trampoline to the original),
`network_field_type` / `set_network_field_bits` (as `networkFieldTypes` below) and `component_record` (as
`componentWrites` below: the entity's own, unshared record or 0), and from SDK 2 `add_weapon` / `weapon_entity`
(below). Functions are only ever added at the end of `HfApi`; a mod checks `api->version`.

### Adding weapons (SDK 2)

`add_weapon` puts a new weapon in the arsenal: a copy of an existing one, with its own loadout id, model, name and,
if the mod wants, stats. One call from `hellforge_mod_init`:

```c
HfWeapon gold = {sizeof gold};                 /* zero the rest: fields added later read as unset */
gold.base = HF_ITEM_AR23_LIBERATOR;            /* the weapon it copies */
gold.unit = "you/your_mod/assault_rifle_gold";  /* its model, from the mod's data/; also its identity */
gold.designation = "AR-23G";
gold.name = "Liberator Gold";
gold.description = "...";
uint32_t id = api->add_weapon(mod, &gold);      /* its loadout id, or 0 (the mod's log says why) */
```

- **Identity.** The new entity is the hash of `unit` and its loadout id the top 32 bits of that, so every player's
  game agrees on it without coordination. Prefix the name with your name and mod.
- **What it shares.** Everything its base has (handling, magazine, ammunition, customization, sounds) except its
  loadout entry, its unit (the model), its text and the components it asks for.
- **Stats of its own.** `own_components` lists components to give the weapon its own copy of, for example
  `{"WeaponMagazineComponentData", NULL}`. After `HF_EVENT_ENTITIES_READY`,
  `component_record("WeaponMagazineComponentData", api->weapon_entity(id), &stride)` returns that copy, and changing it
  changes the new weapon only. Shared records stay out of reach: `component_record` returns 0 for them. The components
  HellForge knows are those in the entity profile (`src/HellForge.Core/entity-profile.json`).
- **Text.** One language for every game language. The armory shows "<designation> <NAME>"; HellForge makes the upper-case
  copy.
- **Choosing a base.** Every session with an added weapon writes `arsenal.tsv` into its log folder: every armory item's
  loadout id, armory type, entity, designation and name, as the game names it.
- **Where it ends up.** HellForge adds the weapon to the game's entity data when that loads (the file is never changed), to
  the engine's network types (so it can be equipped and synced), to the armory, and to the backend's item catalogue
  and the player's inventory (`<session>/registered-items.json`). The loader's `modloader.log` and the mod's log record
  each step. `native/mod-loader/registry.c` explains how.
- **Multiplayer.** The weapon is part of the mod's code, which the modlist gate already requires friends to share.

### Adding projectiles and damage (SDK 3)

A weapon added with `add_weapon` can fire a projectile of its own, dealing damage of its own, and all three are plain
game data: the game reads them the way it reads its own weapons, with no code running per shot.

```c
HfDamage heavy = {sizeof heavy};
heavy.base = HF_DAMAGE_LAS58_TALON;              /* the DamageInfo it copies: damage, penetration, statuses */
heavy.name = "you/your_mod/heavy_bolt";
heavy.scale = 5.0f;                              /* x its standard and durable damage */
uint32_t damage = api->add_damage(mod, &heavy);  /* 650 and up */

HfProjectile bolt = {sizeof bolt};
bolt.base = HF_PROJECTILE_LAS58_TALON;           /* the projectile it copies: speed, gravity, lifetime, ... */
bolt.name = "you/your_mod/bolt";
bolt.damage = damage;                            /* what its hits deal (any damage id; 0 keeps the base's) */
bolt.visual_from = HF_PROJECTILE_AR23_LIBERATOR; /* optional: another type's look (effects already loaded) */
uint32_t type = api->add_projectile(mod, &bolt); /* 351 and up */

HfWeapon rifle = {sizeof rifle};
/* ... base, unit, name as in "Adding weapons" ... */
rifle.projectile = type;                         /* any projectile type, vanilla or added */
api->add_weapon(mod, &rifle);
```

- **The weapon.** `HfWeapon.projectile` gives the weapon its own `ProjectileWeaponComponentData` record naming the
  type (+0), and removes the default ammunition from its own customization record. Ammunition attachments carry an
  entity delta that sets a weapon's projectile when it is assembled (the Liberator's default Full Metal Jacket sets
  276); without one, the weapon fires what its record names, as vanilla weapons without ammunition do.
- **The projectile.** A copy of the base's 272-byte settings row under a type after the game's list (types 0-350; 64
  new types in all mods). The list has room for only 3 more where it is, so the loader moves it to a larger copy within
  2 GB of game.dll and rewrites the 66 instructions that address it (`native/mod-loader/projectile_sites.h`, generated
  by `artifacts/projectile-registry/projectile_sites.py`), each checked first, then raises the game's two type bounds.
  Its damage is the DamageInfo id at row +0x3C; `projectile_row(type)` returns the row after
  `HF_EVENT_PROJECTILES_READY` (+0 is the type and must stay), and `set_field` / `hf_set` change a mod's own row like an
  entity's record (`hf_set(mod, type, ProjectileSettings, impact_explosion, HF_U32(82))`: its hits burst into the Orbital
  Gas Strike's cloud; HellForge loads an explosion's packages itself).
- **The damage.** The game's DamageInfo list (ids 0-649) has no spare room, so the loader moves it to a larger copy
  within 2 GB of game.dll and rewrites the 489 instructions that address it (`native/mod-loader/damage_sites.h`,
  generated by `artifacts/projectile-registry/damage_sites.py`), each checked first. New rows are copies of their base
  with the damage scaled; `damage_row(id)` returns one after `HF_EVENT_PROJECTILES_READY` for further changes (+4
  standard, +8 durable, +0x0C four armour penetration values).
- **Checks.** Every assumption (the lists, their loaders, the bounds, every rewritten instruction) is checked against
  the reviewed build when the lists have loaded; any difference adds nothing and logs why, and weapons then fire their
  base's projectile. The research is in `artifacts/projectile-registry/FINDINGS.md`; the code in
  `native/mod-loader/projectiles.c` and `damage.c`.
- **Multiplayer.** Every machine fires its own copy of a hand-held weapon's shots from its own data, so friends with the
  same mods (the modlist gate) fire the same projectile; types and ids are numbered in the mods' set-up order (by mod
  id), the same on every machine whatever its load order.
- **Sounds.** `HfWeapon.sound_from` (a vanilla weapon's loadout id, such as `HF_ITEM_LAS16_SICKLE`) gives the weapon
  that weapon's firing sounds: its own `ProjectileWeaponComponentData` record takes the donor's sound events (shot,
  loops, fire modes, secondary, silenced). The donor's sound banks must load with the weapon, so the mod's data puts
  them in the weapon's archive patch, as `examples/mods/talon-liberator` does with the Sickle's two banks.
- **Looks.** No row member is a colour: a projectile's look is its particle effects (`spawn_effect`, row +0x48, draws
  the LAS-58 Talon's bolt; `flight_effect`, row +0x70, its light and trail). An effect's colours are curves over the
  particle's life (10 key times, then 10 RGB keys on a 0..255 scale), not its materials or textures. A recoloured
  effect has to come as data, in an archive the weapon loads (as the gold Liberator's model does):
  `examples/mods/talon-liberator` turns the Talon's yellow curves blue.

### Adding stratagems (SDK 4)

`add_stratagem` adds a new stratagem: a copy of a vanilla stratagem's settings row under a StratagemType of its own
(150 and up), with its own name, description, input code, icon, cooldown and uses. Players own it and pick it in the
loadout like any other; what it does when called (its payload, beacon and packages) is its base's.

```c
HfStratagem gatling = {sizeof gatling};
gatling.base = HF_STRATAGEM_ORBITAL_PRECISION_STRIKE;
gatling.name = "you/your_mod/pelican_gatling";              /* its identity: the stable id is this name's 32-bit id */
gatling.title = "Pelican Gatling Support";
gatling.description = "Calls down a Pelican for close air support.";
gatling.code = "LDLLUU";                                     /* 1 to 9 of U, D, L, R */
gatling.icon = "you/your_mod/icons/pelican_gatling";         /* built by the mod's data spec (icon) */
uint32_t type = api->add_stratagem(mod, &gatling);
```

- **The list.** The game's stratagem list (`game.dll + 0x37CB600`) holds exactly the 150 StratagemTypes its enum
  names, so HellForge moves it to a larger copy once the game has loaded its stratagems, as it does the damage list:
  339 instructions address it and 69 bounds on the type grow by the number of new stratagems. The names the mission
  report reads by type move with it. Every instruction and bound is checked against the reviewed game build first;
  anything else adds no stratagem. The review is in `artifacts/stratagem-registry/BOUNDS.md` (not committed).
- **Limits.** 48 new stratagems in all mods: the loadout picker lists at most 150 available stratagems and up to 96
  vanilla ones can be. A new stratagem does not load its base's ship-upgrade (level) packages (the loadout keeps one
  per vanilla type), never counts in the mission statistics, and is never a mission's random pick.
- **Owning it.** The local backend lists each new stratagem for the session as a copy of its base's catalogue item
  (level, price, traits) and its child under the new stable id, and adds it to the inventory. Nothing is saved to the
  profile; a saved loadout that names a removed stratagem drops it.
- **Code.** Choose a code that is neither another stratagem's code nor a prefix of one (or has one as a prefix):
  `artifacts/stratagem-registry/codes.py <code>` lists the vanilla ones it collides with.
- **Icon.** A data spec `icon` resource in the stratagem icons archive `007e093ca718ca1a` (see Data specs):
  `examples/mods/stratagem-compendium`. A missing icon material crashes the game, which is why the spec always builds
  both. The image follows the stratagem pack's format: red where the icon takes its category's colour, green where it
  is white (the in-game editor draws it that way too).
- **Multiplayer.** Peers exchange stratagem types as numbers, so every player must run the same mods: the modlist gate
  ensures it, and types are numbered in the mods' set-up order (by mod id), the same on every machine.
- **Payload.** `payload` / `payload_count` name what a call spawns, and `package` what it loads for the mission. A
  Pelican drop (a base of delivery kind 3, such as `HF_STRATAGEM_EXO45_PATRIOT`) spawns `payload[1]` carrying
  `payload[0]`; `{0, pelican}` sends an empty Pelican.
- **Its name on the permit.** The game names a stratagem "<designation> <title>", the designation being its payload
  entity's (a stratagem row has none): a copy of the A/MG-43 sentry whose sentry is a copy too reads "A/MG-43 <title>"
  until the sentry entity is given text of its own (`HfEntity` / `HfEntityDef` `designation` and `title`, SDK 7).
  The compendium's HMG sentry names its sentry "A/MG-101" and its stratagem "Heavy MG Sentry".
- **In the in-game editor (SDK 7).** Every custom stratagem gets a page in the editor, under its category, with its
  own icon (the PNG its data spec builds the icon from). The page is a vanilla stratagem's (`editor_like`; 0: its
  base's) pointed at the stratagem's own data: its own settings row, and for each vanilla entity on that page the
  mod's entity standing for it, with that entity's projectile and damage where the mod copied them from the page's
  (one copy of each base; two of the same base are a guess, so their fields are left out). `editor_entities`
  (`{entity, like}` pairs ending in `{0}`) names the entities; by default, each payload entity a mod added stands for
  the vanilla entity it copies. Fields with nothing of the mod's to point at are left out, so the page never edits
  the base, and a shared record is refused as for any live edit (give the entity `own_all` to edit everything). Its
  edits are saved in the editor's own `mod-stats.json`, not in Local Stats (the stats pass knows no mod content), and
  the editor applies them as live edits at every session's start. In co-op the host's go with its Local Stats: a joiner's
  editor takes its own off and applies the host's (`host-mod-stats.json`, which its backend writes), and puts its own
  back when the host goes. Tools read the
  same through `content` (`HfContent`: every stratagem, entity, projectile and damage type mods added, with what it
  copies).

### Custom selection categories (SDK 8)

Mods can register their own headings in the game's stratagem selection screen. Register the category during
`hellforge_mod_init`, then pass its handle in `HfStratagem.selection_category`:

```c
HfStratagemCategory category = {sizeof category,
    .name = "air_support", .title = "Air Support", .sort_order = 10};
uint32_t air = hf->add_stratagem_category(mod, &category);
if (!air) return 1;
HfStratagem stratagem = {sizeof stratagem,
    .name = "support_mg", .base = HF_STRATAGEM_MG43_MACHINE_GUN,
    .title = "Support Machine Gun", .selection_category = air};
if (!hf->add_stratagem(mod, &stratagem)) return 1;
```

The mod manifest must require `"sdk": 8`; code should check the loader's API version before using the new call.
`name` becomes `modid:name`. Cooperating mods can register an identical fully qualified definition to get the same
handle; conflicting titles or orders are refused and logged. Handles last for the session and must not be saved.

- Custom headings follow the vanilla sections, ordered by `sort_order` (lowest first), then namespaced identity.
  Empty headings are hidden. The screen retains native selection, scrolling, disabled cards and saved loadouts.
- Inside a heading, `HfStratagem.selection_order` sets each stratagem's place: lowest first, any `int32_t`. Equal
  orders (0 by default) keep `add_stratagem` call order, then mod load order, so a mod that adds its stratagems in the
  order it wants needs no numbers. The base stratagem's own menu order is not used under a custom heading, and
  `selection_order` is ignored in vanilla sections.
- There are at most **28 custom categories across all mods**, alongside five native categories. The existing limit
  of 48 new stratagems is unchanged.
- `selection_category = 0` uses the vanilla section. The existing `category` and `beam` fields still control native
  HUD/ping and beacon presentation; a custom group can contain stratagems from different native categories.
- The loader checks the reviewed picker instructions before enabling custom grouping. If that fails, it logs the
  reason and the stratagems appear in their vanilla sections. Older mods keep their existing grouping.

Native fixture tests pass and a heading with its order was checked in game (2026-10-18); **controller selection and
saved loadouts still need in-game validation**.
See [the implementation notes and acceptance checks](stratagem-categories.md).
The [category example](../examples/mods/stratagem-categories/src/categories.c) registers Experimental and Fire Support
headings with three custom stratagems. Build it with `scripts/Build-Mod.ps1 examples/mods/stratagem-categories`.

### Adding entities and behaviours (SDK 4)

`add_entity` adds any entity (a Pelican, a turret...) as a copy of a vanilla one under a resource name of its own, the
way `add_weapon` adds weapons but without a loadout, armory or catalogue entry. It shares every component record of its
base except the ones it owns (`own_components`); after `HF_EVENT_ENTITIES_READY`, `component_record` finds its own
records to set once: which turret its mount carries (`MountComponentData` +0, slot 0), its weapon's round and rate
(`ProjectileWeaponComponentData` +0, +8), its magazine (`WeaponMagazineComponentData` +0x88), its tags. `model_from`
gives it another entity's model together with every record that names the model's parts (thrusters, effects,
collision, seats) and the packages that load it; a model under another model's records hangs the game at spawn.

**Sharing nothing (SDK 7).** `own_all = 1` (on `HfEntity`, or on `HfEntityDef` with `size = sizeof(HfEntityDef)`) gives
the copy a record of its own in every component of its base, each a copy of the base's: an edit of the base (a stats
mod, the in-game editor) never changes the copy, and the copy's edits never change the base. HellForge sizes each
record from the profile's component, or from the table's shape among every table of the build's entity data
(`native/mod-loader/component_sizes.h`, from HD2Runtime's component catalogue: its index slots and its size); a table
it cannot size for certain stays shared, and the mod's log names it. Every component of the A/MG-43 sentry sizes;
`examples/mods/stratagem-compendium` builds every stratagem this way.

`add_behaviour` adds a behaviour: a vanilla behaviour that HellForge extends, under an id of its own (694 and up), which
an entity runs through `HfEntity.behaviour` (its own `BehaviorComponentData` record names it). The game's behaviour
system dispatches it like its own: each of its six per-behaviour switches (update, stage change, event, stage count,
and the two network event mappings) sends ids above 693 to HellForge, which runs its behaviours and sends every other
id where the game always did. The one base so far is the transport Pelican's (`HF_BEHAVIOUR_PELICAN_TRANSPORT`, 667):
it flies to the beacon and hovers, and its HellForge version holds there for `hold_seconds` instead of 6, circling the
beacon (`orbit_radius`, `orbit_height`) and turning toward its mounted turret's target (`face_target`), then departs.
Everything else is 667's own code. If HellForge cannot install its behaviours (another game build), entities given
one run their base's. The review is in `artifacts/pelican-registry/behaviour/DISPATCH.md` (not committed).

`examples/mods/stratagem-compendium` (src/pelicans.c) uses all of it, as definition blocks (below): two Pelicans with turrets of their own
(the Gatling's fires the MG-206's armour-piercing rounds at the Gatling Sentry's 1600 RPM) on that behaviour, called in
by two new stratagems.

### Adding abilities (SDK 7)

`add_ability` adds an ability: a vanilla one that HellForge holds `hold_seconds` before it runs, playing sounds of its
own on the entity, under an id of its own (2868 and up). An entity runs it where its data names its vanilla one, as an
explosive's armed ability does (`HF_FIELD(Explosive, armed_ability)`: what it runs once it is set off; the Solo Silo
missile's is `HF_ABILITY_SILO_LAUNCH`, 2088), or an ability weapon's fire modes (`HF_FIELD(AbilityWeapon,
mode0_ability)`: what it runs when it fires). The base then runs as it would have, the hold later. Holding the Solo Silo
missile's armed ability does not delay its launch: 2088 runs in flight and arms the missile, which then never came down.

```c
HfAbility countdown = {sizeof countdown};
countdown.base = HF_ABILITY_SILO_LAUNCH;
countdown.hold_seconds = 10.0f;
countdown.start_event = 0x88ADD4DA;          /* a Wwise event: the Launch ICBM objective's countdown */
countdown.second_event = 0;                  /* one every whole second after the first, or 0 */
countdown.package = 0x20BDEC2A51065E1F;      /* the bank's package (packages/content/gen_icbm), loaded for the session */
uint32_t id = api->add_ability(mod, &countdown);
```

**A silo countdown** (`flags = HF_ABILITY_HOLDS_LINKED_LAUNCH`, no base) is for a copy of a silo's targeting remote,
named in its fire modes. Firing the remote holds the launch of the missile it is linked to for `hold_seconds`; the
countdown runs on the missile, so it goes on once the remote is tossed. Then the missile launches at the point the laser
marked last, and while the laser stays on, it follows it, as the silo's does. The compendium's Shredder Silo
(`src/shredder_silo.c`) is the example. HellForge does what the remote's link does at a launch (engine, explosive set off,
owner, guidance target, the rack letting go), through the game's own functions, each checked against the reviewed build;
if one differs, the remote launches at once, as the silo's does. Experimental (2026-10-18): if the laser launches the
missile before the remote's fire runs, there is no countdown, and `modloader.log` says so.

- **The game's abilities.** The tick dispatcher (game.dll 0x11509E0) runs an ability 60 times a second from tick 0 until
  it reports it is done; its ids end at 2867, and HellForge points the dispatcher's default branch at a stub that hands
  its own ids to the loader (`native/mod-loader/abilities.c`) and every other id on, as it does for behaviours. A
  HellForge ability reports "running" through its hold, then calls its base with the tick less the hold.
- **Sounds** play in the world at the entity, on its unit's sound source, as the game's weapon sounds do (a silo
  countdown's at the silo; with no unit, on the world's default source, heard everywhere). On machines where the
  countdown's start is replicated it plays its sounds too (not yet seen in a two-PC session). A sound whose bank is not loaded plays nothing: `package` loads it (a package of HellForge's catalogue).
  `modloader.log` says when an ability is set off and when its base runs.
- **Multiplayer.** Whether every machine runs an entity's ability, or only the one with authority, is still to be seen in
  a two-PC session.

### Adding a difficulty (SDK 5)

`add_difficulty` adds level 11 after Super Helldive: a copy of level 10's settings (enemy mix, packages, mission rules)
with its own title, description, unlock item and enemy budgets (`encounter_points`, `guardforce_points`,
`spawner_cooldown`; level 10 has 600, 600 and 1.5 s). One mod can add it.

The game keeps its 10 difficulties in a static table of 0x330-byte rows, in 19 identical copies. HellForge moves it once
`game.dll` has filled it: one table with 11 rows replaces every copy for the 57 instructions that read them
(`native/mod-loader/difficulty_sites.h`, generated by `artifacts/difficulty11/difficulty_sites.py`, which is not
committed; research in `FINDINGS.md` there). The difficulty API's limits of 10 become 11: the highest level, row and level
lookups by unlock item, and the unlocked mask and highest unlocked level. Every pinned instruction, limit and copy is
checked first; anything else refuses the change, and the game keeps its 10 levels.

- **Choosing it:** on the war table, level 11 is the carousel's eleventh place, after Super Helldive. The bar's object
  holds exactly 10 widgets (one per level; another member follows them), so it gets an 11th of its own: its build, update,
  destroy and choice loops run to 11, and the five instructions that turn an index into a widget address go through
  stubs that give index 10 the bar's own extra widget (a pool of 32, one per bar). The band sprite behind each icon
  comes from a 10-entry array read by level, which moves too (level 11 takes level 10's).
- **Icon and name:** `HfDifficulty.icon` names an icon the mod's data spec builds (`"icon"` with `"prepare": "image"`,
  archive `007e093ca718ca1a`). The bar's own image setter takes atlas sprites only, so for level 11 its icon call goes
  through a stub: Super Helldive's sprite first (it gives the image element its resource), then the mod's texture with
  the whole texture as its UV rect. Other places that draw a level's icon (the war table's mission markers) take atlas
  sprites only, so level 11's row keeps Super Helldive's sprite for them. The bar draws the icon in 200 x 138, so make
  the art fill the square texture. The title is shown in capitals, as the game's names are.
- **Enemies:** level 11 has its own enemy tech level, 11. Each roster row (0x80) holds a spawn weight per tech level at
  +0x30 with 8 unused bytes before them, so the weights move 4 bytes down and level 11's (level 10's to begin with) goes
  after them; the six readers' displacements follow. Compositions allowing up to tech 10 allow 11 (318 rows) and the
  director's clamp of the tech level becomes 11. It gets its own budgets. A single enemy group still holds at most 32
  enemies (more crashes the game), so bigger budgets mean more groups more often.
- **Unlocking:** each level's row names an inventory item that unlocks it. Level 11's item is a copy of Super Helldive's
  in the backend's catalogue (`registered-items.json`, `RegisteredItems.cs`). A player who holds Super Helldive's item
  holds it too. Nothing is saved to the profile.
- **The live cap:** the game client configuration carries Arrowhead's cap on the level (online override 48726272, "10"),
  and the game uses the lower of it and its own maximum. With a difficulty registered, the backend serves the cap as 11.
- **Rewards:** level 11 pays 1800 XP and 2200 requisition per mission.
- **Patrols:** `patrol_frequency` divides the row's per-faction patrol interval (+0x2A0.., level 10: 65 / 78 / 75 s for
  Terminids / Automatons / Illuminate) and `patrol_cap` scales the most living patrol enemies (+0x2AC.., 41 / 39 / 48; the
  HiveMind scales it further by mission progress and players). The director reads both from the row every frame.
- **Flags** (`flags`): `HF_DIFFICULTY_SECOND_PRIMARY` makes every mission at the level two missions in one: at the
  end of objective selection (0x1756730, every caller: war table, mission, preview) the loader picks a partner mission
  (same faction, a standard mission an operation lists with this one, placed on this map's layout, with main objectives
  this mission does not have; deterministic from the mission's seed, row and faction, so the same on every HellForge
  client), runs the game's own selection for it and appends its main objectives and their steps (class 0 and 1). The
  steps are registered in the base missions' rows as class 1 with weight 0 (never drawn, so levels 1 to 10 are unchanged)
  so the game gates every main objective on all steps of both missions. `HF_DIFFICULTY_CITY_STRONGHOLDS` gives the
  level's city maps the strongholds (Automaton fortresses, Terminid mega nests) its outpost counts ask for: the outposts
  set's two stronghold groups get copies of their items up to 100 m across, appended at level init, with class list {3}
  (the city layout's only class) and minimum difficulty 11, so only cities at the level take them; open maps keep theirs.
  `HF_DIFFICULTY_CITY_ICBM` makes every city operation at the level start with a Launch ICBM mission: three of the game's
  unused mission rows (one per faction) become city rows for levels 11..11 (the faction's city row with the ICBM row's
  name, gen_icbm x 3 and the codes, power and refuelling steps, plus the city's side objectives), listed first in the
  city operations with a weight that always wins the operation's first (kind 1) mission; the ICBM objectives' stamps get
  city copies the same way as the strongholds. These rows are not mixed with partner missions.
  `HF_DIFFICULTY_NO_ERADICATE` keeps Eradicate missions
  (their game mode's delta) at level 10 and below; their kill objectives remain second main objectives. The mission-end
  telemetry keeps 16 objectives (capped).
- **Objectives, outposts and map:** objective definitions allowing up to level 10 allow 11 (148; without this a
  level-11 mission had no objectives at all). `optional_objectives` and `major_objectives` (together at most 18: with more objectives the
  map generator's objective list moves from 16 to 30 places, its mission stamp list from 128 to 1024, both to the end of
  the enlarged generation block; up to 12 of the 30 are the mission's own, its live event's and a partner mission's)
  set the row's counts (+0x28,
  +0x2C; a mission's own list limits them), `outpost_scale` its outposts per size and faction (+0x34..), `mission_minutes` every mission's countdown (an
  entity delta of its own, registered in the game's entity delta table: HiveMind +0x74 = the level's seconds, added to
  a level-11 mission's delta list at launch, 0xAD0E4E, after the mission's own deltas, as High Value Asset missions get
  1200 s; the shared definition is never written, so other levels keep their countdown. The countdown's network
  definition, 207, 0..2500 s and clamped by the host every frame, is widened to fit), `map_scale` its
  playable radius factors (+0xF0.., up to 2). The map generator's route cells (a Poisson disc of 40 m spacing within
  680 m of the world's centre) fill fixed arrays without checking them, so a larger playable area alone overflows them.
  Above 1, each mission at the level gets a disk that covers its border (at most 1022 m: the world is 2048 m) and a
  spacing that keeps level 10's number of cells, in quarter metres (map type 1 lays them as a grid, and an inexact grid splits its corners into extra vertices; 2 x: 1022 m, 65.75 m): level init writes the level's radii
  (game.dll 0xF71CD8, after the map type is known) and the two spacing reads (0xF3614D, 0xF3732C) take the level's.
  GenerateVoronoi (0xF3F8E0) lays a 16 m grid over the same square, keeps the points in the disk and builds their
  Voronoi diagram (jc_voronoi): level 10 nearly fills its unchecked arrays (5672 of 5680 sites, 5839 of 5849 vertices),
  so its spacing grows with the disk too (0xF3FB04), rounded up to a quarter metre so the grid points stay exact in
  float. An inexact spacing makes near-degenerate circles and extra vertices, overruns the vertex array and the pass
  retries forever (the war table's "Receiving Geodetic Data"). 2 x: 24.5 m.
  GenerateRouteCellVoronoi (op 3, 0xF93800) fills each route cell with points at its type's size / 2 or / 4: level 10
  makes 11954 areas of 13224, in proportion to the generated area, so that spacing grows with the disk too (0xF9420F,
  x 1.04, in quarter metres).
  On map type 1, FinalizeVoronoi (0xF68050) rebuilds the route cells from points over the whole square at each cell
  type's size: scaled the same way (0xF68273), else 2.25 x the points overrun the 1024 route cells.
  The generator's stamp index (0xC04B80, for overlap tests of stamps, locations and roads) has 10757 slots; level 10
  uses 4892, and its 17 inserters don't check for a full index. Above 1 it gets 16383 (the same 14 index bits).
  The level's locations (objectives, points of interest, outposts, ...) are two arrays of 2740 in the generation block
  (G+0x758E20, 0xE0 each, and G+0x7EEBA0, 0x40 each). Level 10 makes about 824; 2 x maps with Hell Forge's objectives
  and outposts made 3900 to 5500. Above 1 the block grows (0x11A7268 -> 0x13E7280 bytes) and both arrays move to its end
  with 8192 places: 223 instructions, 236 builder capacities and the block's size at 6 places (location_sites.h).
  The level's location types (G+0x756C20, 96 x 0x10; level 10 uses ~44, 2 x maps 73..81) move after them with 512
  places (99 instructions, 92 capacities). The minimap's arrays (proxies G+0x8198A0, 4096 x 0x30; scatter G+0x8498A0,
  2048 x 0x58; proxy units G+0x118BEFC and G+0x118FEFC, 4096 and 16384 x 4) follow with 4 x their places: a 2 x map's
  proxies ran over the scatter into the terrain merge slots (G+0x8758A8). The block is then 0x15A9280 bytes.
  Blocks are made with a world, after the loader installs this; if one exists already the move is skipped.
  Structured maps (map type 2, another generator) keep level 10's size.
  The mission distance factors (10 levels x 3, read unclamped at 7 sites) move to a table with level 11's.
- **Missions:** the war table offers a level only the operations whose difficulty range includes it (operation
  settings, 25 rows, and missions, 162 rows, each with a lowest and highest level). Every range ending at 10 ends at 11.
  The game's random pick of a difficulty within a mission's range keeps 10, so nothing picks 11 by chance.
- **Mission board:** the war table generates three missions per level into a 110-slot board per planet. Level 11's three
  take slots the board's special missions use for levels 1 to 3, which may then not appear at those levels.

Level 11 is published in the lobby like any other level: friends need the same mod (the modlist gate checks it).
`examples/mods/hell-forge` adds Hell Forge: 1500 encounter and guardforce points, a 1 s spawner cooldown, 8 optional and 2 secondary objectives, twice the outposts and its own icon.

### Named fields and definition blocks (SDK 6)

SDK 6 names the fields of component records, so a mod never writes an offset (the plan: [sdk-roadmap.md](sdk-roadmap.md)).
One source names them: `mappings/components.json` (per component: index, record size, the game build whose layout it
is, and fields with offset, type, count, meaning, source and whether the game confirmed it) and `mappings/catalog.json`
(vanilla content by name). `HellForge.Cli sdk-headers --repo .` generates from them:

| File | What |
|---|---|
| `native/mod-sdk/hellforge_components.h` | a packed struct per component record (`HfProjectileWeapon`, `HfWeaponData`, `HfDeposit`...): named fields at their offsets, the rest as `_unknown_XX` padding, size and offsets asserted; field groups (`HF_GROUP_...`); and structs for the settings tables' rows (`HfDifficultySettings`, `HfStratagemSettings`, `HfProjectileSettings`, `HfDamageInfo`) |
| `native/mod-sdk/hellforge_content.h` | vanilla content constants, each commented with its catalog name: every armory item and its entity (`HF_ITEM_...`, `HF_ENTITY_...`), every stratagem (`HF_STRATAGEM_...`), projectiles, behaviours, damage, packages, tags, stratagem traits (`HF_TRAIT_...`), categories and beams; and the kinds `find` takes (`HF_KIND_...`) |
| `native/mod-loader/sdk_fields.h`, `sdk_content.h` | the loader's tables of the same fields (which a mod's fields must match) and content (which `find` resolves) |

`hellforge_mod.h` includes both SDK headers. Edit the mappings, never the generated files; the integration checks fail
when they differ. Naming a researched range is one entry in the mappings: the padding around it splits, nothing else
moves, and mods built before keep working.

A **definition block** (`HfEntityDef`) declares a new entity: its base, and lists ending in `{0}` of what changes.

```c
static const HfCopy turretCopies[] = {
    {HF_ENTITY_MG206_HEAVY_MACHINE_GUN, HF_FIELD(ProjectileWeapon, projectile)},
    HF_COPY_GROUP(HF_ENTITY_MG206_HEAVY_MACHINE_GUN, ProjectileWeapon, firing_sounds),
    {HF_ENTITY_GATLING_SENTRY, HF_FIELD(ProjectileWeapon, fire_rate)},
    {0}
};
static const HfSet turretSet[] = {HF_SET_GROUP(WeaponData, recoil, HF_F32(0)), {HF_FIELD(WeaponMagazine, capacity), HF_U32(2000)}, {0}};
static const uint64_t chinTags[] = {HF_TAG_NO_KILL_CREDIT, 0};
static const HfEntityDef turret = {.name = "you/your_mod/turret", .base = HF_ENTITY_PELICAN_TURRET,
    .behaviour = &fullAuto, .copy = turretCopies, .set = turretSet, .remove_tags = chinTags};
static const HfMountDef mounts[] = {{.slot = 0, .entity = &turret}, {0}};
static const HfEntityDef pelican = {.name = "you/your_mod/pelican", .base = HF_ENTITY_PELICAN_TRANSPORT, .mount = mounts};
/* in hellforge_mod_init */
HfRef ref = api->register_entity(mod, &pelican); /* registers the turret and its behaviour too */
```

- `HF_FIELD(Component, field)` names a field; a misspelt component or field does not compile. `HF_F32`, `HF_U32`,
  `HF_I32`, `HF_U8`, `HF_U16` and `HF_U64` tag a value with its type. `HF_COPY_GROUP` and `HF_SET_GROUP` expand a group
  of the mappings (the casings, the firing sounds, both recoil blocks, the spread) into one entry per field.
- The entity gets records of its own in every component the block changes, so there is no `own_components` list;
  `own_all = 1` (with `size = sizeof(HfEntityDef)`) gives it its own in every other component too (it shares nothing).
  `behaviour` points at an `HfBehaviour` added once however many entities name it (`behaviour_id` takes an existing
  id); `mount` entries pointing at other blocks register those first.
- The loader checks every field and value when the block is registered: a component without mappings, a field of
  another layout (the mod was built for another game build), an offset, size or type the mappings do not have, a
  value of another type or out of its field's range (`HF_U32(-1)`), or a set on an array. It refuses the whole block
  and logs each problem with the mod's source line; nothing is added.
- **Deferred edits.** The changes are applied by the loader whenever the game loads its entity data, before
  `HF_EVENT_ENTITIES_READY` reaches any mod, and again after a reload. A mod uses events for logic, not to time its
  edits. A source record that is missing at that point is reported once per load in the mod's log and `modloader.log`.
- **Existing entities.** `HfEditDef` and `edit_entity` change an entity that exists already with the same lists:

  ```c
  static const HfSet backpackSet[] = {{HF_FIELD(Deposit, capacity), HF_U32(10000)}, {0}};
  static const HfEditDef backpack = {.entity = HF_ENTITY_MAXIGUN_BACKPACK, .set = backpackSet};
  /* in hellforge_mod_init */
  api->edit_entity(mod, &backpack);
  ```

  A vanilla entity's record is changed only if no other entity shares it; a shared record is refused when the data
  loads (stats mods change shared records, with their guards and conflict rules).
- **Conflicts.** Two mods changing the same field of the same entity, or a code mod and a stats mod, are reported when
  the second edit is queued: in both mods' logs and `modloader.log`, with the other mod's source line. Edits apply in
  load order after the stats, so the later one's value stays. Tag lists compose (each edit adds or removes one tag).
- **Identities.** A short name (no `/` or `:`) is the mod's own: `"pelican_gatling"` in `skyeshade.pelican-support` is
  `skyeshade.pelican-support:pelican_gatling`, and its ids derive from that, the same in every player's game. This holds
  for `register_entity`, `add_entity`, `add_stratagem`, `add_projectile`, `add_damage` and `add_difficulty`. A full name
  (`"skyeshade/pelican_support/pelican_gatling"`) stays as it is, so content named before keeps its ids: a stratagem's
  stable id is what saved loadouts keep. A weapon's `unit` is a resource name and is never changed.
- **Lookup.** `find(kind, name)` resolves a name at run time: vanilla content by its catalog name (`find(HF_KIND_ENTITY,
  "Pelican Transport")`, `find(HF_KIND_TRAIT, "Anti-Tank")`) or content a mod added by its identity. Code that knows
  its content uses the constants; `find` is for names from a config file or another mod.
- **Stratagems:** `HfStratagem.flags` takes traits (`HF_TRAIT_CROWD_CONTROL | HF_TRAIT_ARMOR_PIERCING`; the game's
  StratagemInfoFlag bits, two of them not named yet), `category` and `beam` their constants. A stratagem with a payload
  and no `package` loads its payload entity's package (its LoadoutPackage record, or its model's or base's), set once
  the entity data and the stratagem rows both exist; the mod's log names the package.
- **Table rows.** `difficulty_row`, `stratagem_row`, `projectile_row` and `damage_row` return rows the generated structs
  describe: `HfDifficultySettings* level = (HfDifficultySettings*)api->difficulty_row(11); level->encounter_points =
  6000.0f;`. They are written directly, not as deferred edits (`examples/mods/hell-swarm`).

The same edits as calls, for code (`HF_API(mod)` is the API: an `HfMod` starts with it):
`hf_copy(mod, ref, from, Component, field)`, `hf_set(mod, ref, Component, field, HF_U32(2000))`,
`hf_tag(mod, ref, HF_TAG_..., 0 or 1)` and `hf_mount(mod, ref, slot, carried)` queue the same deferred edits (from
`hellforge_mod_init`, the entity gets a record of its own in the field's component; later it must have one), and
`hf_record(mod, ref, HfWeaponData)` returns a typed pointer to an entity's own record once the data has loaded, or
NULL when the record or the mod's layout does not match.

Build a mod with `scripts/Build-Mod.ps1 <mod folder>`: it compiles `<mod>/src/*.c` (or `*.cpp`) with the project's pinned
Zig into the DLL the manifest names. `examples/mods/machine-gun-10k-code` is Machine Gun 10k written this way (about 40
lines). The DLL is part of the modlist code, so friends need the identical build.

A code mod runs inside the game with your permissions and the loader does not catch its crashes: enable code mods only
from people you trust. The launcher marks them CODE. Every engine build tests the whole chain without the game: a fake
game module, the real stand-in, loader and Machine Gun mod, and a hook-test mod.

## Data mods

`data/` holds the game's own mod format, the files mod managers install: `<bundle>.patch_<n>`, `.patch_<n>.gpu_resources`
and `.patch_<n>.stream`. Each session removes the files it staged last time, then copies every mod's patches into the
copy's `game/data`, in load order, renumbered per bundle after any patches already there (contiguous, as the game
expects): the top mod's patches get the highest numbers, so they win. The Steam files of the copy are never touched, so
verification and repair are unaffected.

### Cosmetic packs

Packs in the community's format load as they are, without a `hellforge-mod.json`: a folder (or `.zip`; **Add mods...**
also takes a `.rar` or `.7z`, unpacked with Windows' own `tar`) with a `manifest.json` and folders of patches, or just patches at
its root:

```json
{
  "Version": 1, "Guid": "0967dcdf-...", "Name": "Kinetic Replacer AIO", "Description": "...", "IconPath": ".jpgs/icon.png",
  "Options": [
    { "Name": "AC-11 Autocannon", "Image": ".jpgs/ac11.jpg", "Include": [],
      "SubOptions": [ { "Name": "AC-11/A", "Include": ["AC-11 Autocannon (Bipod)"] }, ... ] },
    { "Name": "JAR-40 Conqueror", "Include": ["JAR-40 Conqueror"] }
  ]
}
```

A pack's id is `pack.<Guid>` (or `pack.<name>` without one). It is cosmetic: listed in the cosmetic group and not in
the modlist code. Its row's details list the options: each is switched on or off, and an option with sub-options has a
variant to pick (the first by default); hovering an option shows its picture. A session stages the folders the choice
includes, root patches first, then the options in the manifest's order, like any mod's `data/`. Packs are copied into
`mods/` whole (their previews are often in a dot folder) and are never zipped by HellForge: share a pack as its author
made it.

**Lua mods** (HD2Runtime, Bingus Shared Loader and the mods made for them) ship as packs too; their patches hold a
`lua` resource. They load like any pack, with a **LUA** tag, and are client-only: never in the modlist code. HD2Runtime
runs on HellForge's Lua bridge, so its mods' changes are HellForge live edits the F8 editor names (`lua.<mod>`); see
[lua-bridge.md](lua-bridge.md). Only each patch's resource table is read to tell a Lua mod.

### Data specs

Data made from the game's own resources (a recoloured model, an effect copy, a weapon's sound banks) must not be
shipped: it is the game's bytes. A mod describes it instead in `hellforge-data.json` next to its manifest, and HellForge
builds it on each player's machine from their own game copy, then stages it exactly like `data/` (after the mod's own
`data/` files, if it has both). The spec is its own file rather than a manifest section: it is only read when data is
built, it can be long (resource lists), and the manifest stays small for the launcher's Mods page.

```json
{
  "format": 1,
  "patches": [
    {
      "archives": ["dbd9381f9ea9788f", "047c26f2f11d253e"],
      "resources": [
        { "type": "unit", "copy": "0x968211C0033DCE64", "name": "you/your_mod/rifle",
          "replace": [{ "from": "0x7F112ACD09A5E523", "to": "you/your_mod/rifle/m_weapon", "at": ["0x2E44"] }] },
        { "type": "material", "copy": "0x7F112ACD09A5E523", "name": "you/your_mod/rifle/m_weapon",
          "replace": [{ "from": "0xBABD338977FAA2FF", "to": "you/your_mod/rifle/lut" }] },
        { "type": "texture", "copy": "0xBABD338977FAA2FF", "name": "you/your_mod/rifle/lut",
          "lut": { "zonesFrom": 6, "columns": { "0": [0.83, 0.62, 0.18], "2": "#8C6114" } } },
        { "type": "wwise_bank", "include": ["0xE9A5E5E12FD10F52"] }
      ]
    },
    { "archives": ["007e093ca718ca1a"], "resources": [{ "icon": "icons/my_stratagem.png", "name": "you/your_mod/icons/my_stratagem" }] }
  ]
}
```

Each patch becomes one archive, written as `<bundle>.patch_<n>` (+ `.gpu_resources`, + `.stream` when a resource has
one) for every bundle in `archives` (16 hex digits; the bundles that load with what the mod adds, such as the base
weapon's, or `007e093ca718ca1a` for UI icons). Resources and references are `0x<hash>` or a name (hashed with
MurmurHash64A); types are names (`unit`, `material`, `texture`, `particles`, `wwise_bank`, ...) or `0x<hash>`; colours
are `[r, g, b]` as the game stores them or `"#RRGGBB"`. Each resource is one of:

- **`copy`**: the game resource under the mod's own `name` (a path, prefixed with your name and mod; it must not be a
  game resource's name), with all its parts. Optional edits, in this order:
  - `expect`: `{"<offset>": "<u64>"}` the resource must hold before anything is changed;
  - `replace`: references to another resource (`from`) become `to`, usually another copy; every occurrence, or exactly
    the `at` offsets (each must hold `from`);
  - `curves`: a particle effect's colour curves (10 key times, then 10 RGB keys on 0..255). Each warm key (red >= green
    >= blue) is recoloured: `"preset": "warm-to-blue"` (red and blue swapped, green x 0.6, as the Talon research) or
    `"colour": "#3070FF"` (that hue and saturation at the key's brightness). `"keys": n` requires exactly n keys to
    change; without it, at least one must;
  - `colours`: a material's RGB parameters by name (`0x<IdString32>` or the name): `{"0xB4138578": [0, 0.35, 1]}`;
  - `lut`: a material's colour lookup texture (R16G16B16A16_FLOAT, a row per material zone, column 0 the base colour):
    `zonesFrom` (every row first takes this row's values), `columns` (`{"<column>": colour}` in every row), `rows`
    (`{"<row>": {"<column>": colour}}`); alpha is kept and the mips are rebuilt.
  - A `unit` copy also gets its base's physics under its own name, unchanged (the engine finds a unit's physics by the
    unit's name; without it a copied sentry got a huge default collision box and no aim). Its bones and state machine are
    named in the unit itself, so they stay the base's.
  - `mesh` (on a `unit` copy): the mod's own geometry, from a Wavefront OBJ in the mod folder:
    `{"obj": "models/cube.obj", "scale": 1, "yUp": true}`. Every render and shadow mesh of the copy, in every LOD,
    gets it (faces triangulated; `yUp`, the default, turns Blender's Y-up into the game's Z-up; V is flipped); it is
    rigid on the unit's main bone, centred on the mesh joint (a weapon's grip area), and the boxes the engine culls
    with are rebuilt around it. Everything else is the base's: materials (recolour them in the same patch), physics,
    hitboxes and attachment points such as the muzzle. HellForge refuses models it cannot rebuild yet (geometry kept
    in another resource, a body culling the model as a sentry's does, rotated mesh joints) with the reason.
    `examples/mods/cube-gun` makes an arsenal weapon of a purple metal cube this way.
- **`include`**: game resources of `type` unchanged under their own names (what a copy needs loaded with it: the
  materials and textures an effect uses, a donor weapon's sound banks).
- **`icon`**: a 256 x 256 PNG in the mod folder as a stratagem icon named `name`: its BC1 texture and the GUI material
  that draws it (the game crashes on an icon without its material). `"prepare": "auto"` converts a red-and-white picture
  to the game's icon masks (R the category colour, G white); the default `raw` uses the pixels as given. `"image"` keeps
  the picture's colours and transparency in a BC3 texture (as the UI atlas pages), for widgets that draw it as it is (a difficulty
  icon; BC1 has no alpha).

Everything is checked before anything is written: every resource the spec names must be in the game data (all missing
ones are listed), every edit finds what it expects, and new names must be new. When a session stages mods, the data is
built from the installation's game copy into `<installation>/temp/mod-data/<mod id>/<key>/` and reused while the key
holds: the data builder's version, the supported game build, the game's `bundles.nxa`, the spec and its input files.
A mod whose data cannot be built is not loaded (the session's result lists why). The modlist code counts the spec and
the builder version, not the generated files: with the same game build they make the same bytes on every machine.
To build or check one by hand: `HellForge.Cli build-data --mod <folder>` (checks the spec), with `--install <folder>`
(builds into that installation's cache) or `--game-data <game/data> --out <folder>`; `scripts/Build-Mod.ps1` runs it for
mods that have a spec (`-Install` or `HELLFORGE_INSTALL` to build). The code is in `src/HellForge.Core/GameData/`.

## Stats data mods

A mod can include `hellforge-stats.json` beside its `hellforge-mod.json` manifest. No DLL is required. Stats mods
are gameplay mods, including when the manifest says `gameplay: false`; their exact files participate in the
modlist digest. `examples/mods/stats-example` is a working authoring example for the supported schema.
Validate one without launching the game with `HellForge.Cli check-stats --mod <folder>` or
`scripts/Build-Mod.ps1 <folder>`; stats-only mods have nothing to compile.

```json
{
  "format": 1,
  "weapons": { "AR-23 Liberator": { "rpm": 900, "magazine": 60, "spread": 5 } },
  "throwables": { "G-12 High Explosive": { "fuse": 2.0 } },
  "projectiles": { "276": { "speed": 1200, "damage": 54 } },
  "damage": { "54": { "standard": 150, "durable": 40, "armorPenetration": [4, 4, 4, 4] } },
  "stratagems": { "Orbital Precision Strike": { "cooldown": 60, "uses": -1 } }
}
```

Categories are `weapons` (primary, secondary, support and mounted weapons), `throwables`, `backpacks`, `vehicles`,
`stratagems`, `projectiles` and `damage`. Entity names are exact catalog armory names; projectile and damage keys
are decimal type ids. Mounted weapon names include their host and mount, for example `A/AC-8 Autocannon Sentry / weapon`.
The field keys are HD2Runtime's `semanticFieldId`, such as `weapon.fire_rate` or `deposit.capacity`. Short aliases
include `rpm`, `magazine`, `recoil`, `fuse`, `cooldown`, `uses`, projectile `speed`, and damage `standard` and `durable`.
An alias is usable only when its underlying field has a reviewed scalar backing. `spread` sets both
`weapon.horizontal_spread` and `weapon.vertical_spread`; `armorPenetration` takes four integers, in direct, slight,
large and extreme angle order. Stratagem `uses: -1` means unlimited; the reviewed native name `OrbitalShell` also
resolves to Orbital Precision Strike. Direct catalogs cover projectile ids 1–350 and damage ids 1–649.

The embedded `src/HellForge.Core/stats-schema.json` lists supported fields, storage, ranges, sharing and blocked
fields with refusal reasons, and, for the editor, each target's armory slot, weapon class, host item and stratagem
family and the catalogs' field order. The exporter reads literal catalogs without executing Lua:

```powershell
py scripts/ExportEntityProfile.py <HD2Runtime>/schemas/current.lua src/HellForge.Core/entity-profile.json
py scripts/ExportStatsSchema.py <HD2Runtime> src/HellForge.Core/stats-schema.json native/mod-loader/stats-layouts.h
```

The generated stats schema contains metadata, not game bytes or extracted default values. Named edits validate
the reviewed component header, stride, resource ownership and record index, or the settings row identity and layout,
before writing. The schema fingerprint must match the supported game.dll profile (build 25480438). Floats must be
finite; integer fields reject fractions and overflow, and catalog ranges are enforced. Invalid names, blocked fields,
duplicate properties and duplicate physical fields within a stats file reject that mod.

Component edits run on the entity-loading thread after registry additions, before `HF_EVENT_ENTITIES_READY`.
They run once for each entity-data load. Projectile, damage and stratagem edits run once after their tables become
available and registry installation has finished, using the moved table where one exists. Explosion, arc, beam and
status settings use uniquely identified retained allocations, validated against their structural headers, row counts
and relocated pointers. Default weapon magazine overrides use the attachment catalog's reviewed entity-delta chain
(attachment resource, settings index, component, member and data offset). Readiness is polled for up to 20 seconds;
each validated allocation is edited once, and missing or ambiguous allocations refuse the edit. There are no per-frame,
per-shot or periodic field rewrites. A settings reload later in the session does not reapply stats. Every applied edit
and runtime refusal goes to `mod-<id>.log`; missing load-time machinery refuses the edit.

Shared records affect all consumers and produce a warning in that log. Add `"own": true` to an entity's entry to
clone its component record through the registry before editing. All fields in that entry must be component-backed.
Settings and attachment-delta ownership are rejected: isolating these shared definitions requires reviewed relinking
of their consumers. A magazine edit affects weapons using that default attachment; selecting a different attachment
can select different stats.
Unverified gameplay effects are logged even when the structural checks pass.

References take type ids (the editor's pickers name them; `mappings/references.json` lists them):
`attack.primary.projectile` (a weapon's projectile; for the Liberator, Dominator, Redeemer, Peacemaker, Diligence and
Breaker `ammunition.projectile`, their default ammunition's entity delta, which every weapon equipping it shares and
which applies from the next boot), `terminal.<role>.impact.explosion` and `terminal.<role>.expiry.explosion` (0 none),
and `<damage>.status_<k>_type` with `<damage>.status_<k>_strength` for k 1 to 4 on every damage type a target shows
(`damage.`, `explosion.primary.impact.damage.`...; 0 empty, filled from slot 1). Projectile pages take
`terminal.impact.explosion` / `terminal.expiry.explosion`, damage pages `status_<k>_type`. The loader writes a reference
once the packages it needs are loaded (see Live editing, References).

Mods apply in load order (the Mods page's bottom mod first). Conflicting stats and legacy component writes are reported in `ModCatalog` problems
and both mods remain enabled; the later write wins if its guard passes. An expected value is compared with the value
before earlier writes in that pass, so an intentional conflict does not defeat the guard. Network-type conflict
handling retains its existing behavior.

For a reviewed component not exposed by a named field, `raw` requires an explicit expected value:

```json
{ "format": 1, "raw": [
  { "component": "DepositComponentData", "resource": "0x056DE1C5E21E723E",
    "offset": "0x0", "type": "u32", "expect": 1000, "value": 1500 }
] }
```

Raw storage types are `u32`, `i32` and `f32`; offsets must be aligned and within a component in the pinned entity
profile. A mismatched expected value refuses the write. Raw writes also support `own` and log shared-record warnings.

Coverage is limited to reviewed numeric fields and the projectile-to-damage reference. Unsupported structured fields,
unreviewed references and ambiguous attack branches have explicit refusal reasons. The Liberator's default magazine
is supported through the newer attachment catalog, which supersedes the older weapon-catalog refusal.

### Launcher editor

Open **Stats** in the installed launcher. The page follows the HD2R Editor's layout (`src/HellForge.Core/StatsCatalog.cs`):

- **Navigation.** WEAPONS (Primary, Secondary, Throwables), STRATAGEMS (Offensive, Defensive, Support weapons,
  Backpacks, Vehicles, Mission, coloured like the game's stratagem types) and ADVANCED (projectile and damage type ids,
  each named by the items that use it). The list groups weapons by armory class (Assault Rifle, Marksman Rifle…).
- **One item, one page.** A support weapon holds its weapon and call-in, plus its backpack when it has one. A vehicle
  or sentry holds its call-in, body and mounted weapons; a primary holds its underbarrel. Fields are sorted into
  Projectile (flight, direct hit, explosions), Damage, Handling / Fire, Ammunition, Heat / Charge, Beam / Arc and
  Hellpod / Call-in, in the catalogs' order. Hellpod / Call-in comes last on weapon pages and first on stratagem pages.
  Categories can be closed; a closed category stays closed on every page.
- **Columns.** DEFAULT is the value before the Local Stats mod: another enabled mod's (blue, MOD), else the game's.
  VALUE is editable, and SOURCE shows EDITED for your changes, LINKED for a shared record set on another item, and MOD.
- **Editing.** Type a value; ↑/↓ step it (Shift ×10, Ctrl ÷10 on decimals); Enter moves to the next field and Esc
  restores the value it had. Entering the default, clearing the box or pressing ↺ undoes a change. Invalid values are
  marked and block saving. A shared record (SHARED, with the other items in its tooltip) is one value: editing it on
  any item changes it everywhere, and the list marks every item it reaches.
- **Search** finds items by name, and fields by label from three letters ("liberator fire rate"). Every word must
  match, in any order.
- **Changes** lists every changed field with its default and value; select one to open it, ↺ to undo it.

**Save** writes `mods/zz.local.stats/hellforge-stats.json` and its manifest; **Discard changes** goes back to the
saved file. The mod uses normal validation, ordering (it loads last, so it wins), conflict reporting, enable/disable
controls and digest rules. Undoing every change removes the editor's mod. Other mods are not changed. A file changed
outside the editor must be discarded before saving. Editing and saving are disabled during a session, except in live
mode (below). Aliases in the file (`rpm`, `spread`, `armorPenetration`, native stratagem names) are expanded to field
names when the editor loads it.

**Live mode.** In a solo session (Settings: "Lobby and session through Steam friends" off, so no friend can join), the
editor goes live once the game has loaded: every change goes into the running game as well as into the draft, and
**Save** still writes the Local Stats mod so the changes stay for later sessions. A field's tooltip says when its change
applies (`applies now`, `on respawn`, `next mission`; "expected" when not yet confirmed in game), and its SOURCE chip
shows LIVE when the game has it, NOT LIVE with the game's reason when it refused it, and NEXT SESSION for what cannot
change while the game runs (own records, attachment changes). Undoing a change puts the default back in the game.
Live changes that are not saved end with the session. See "Live editing" below for how it works.

Each local session writes `logs/sessions/<session>/stats-values.tsv` (format 2), even without active mods. The loader
observes each field once per load, after its stats pass, and reports a field that a stats file changed with its value
from before that pass. The editor reads the newest snapshot that matches this build and the schema's set of observed
fields (its fingerprint covers addresses only, so editor metadata changes keep snapshots valid). **Refresh values**
reloads it. Without a snapshot DEFAULT shows "—"; you can still edit. The schema contains no observed values.

**Give this item its own records** appears when an item shares component records. It uses `OwnRecord` for the item's
component fields only; projectile, damage and explosion definitions stay shared, and an edit to one of them on such an
item is stored on another view of that definition (the projectile or damage type itself when there is one).

Fixture checks: `dotnet run --project tests/HellForge.Tests`,
`py -m unittest discover -s scripts -p test_export_stats_schema.py`, and the engine's native stats test (every engine
build runs it), with synthetic component and settings tables, including shared and owned records, guard refusals,
conflict ordering, moved damage tables, retained allocations and attachment-delta ownership chains.

Gameplay acceptance remains a separate check in the isolated installation: primary rate of fire and magazine,
secondary, grenade fuse or damage, support charge, backpack, stratagem cooldown and uses, vehicle ammunition,
projectile speed and damage. Fixture results establish resolution, guards and writes; they do not establish that every
unverified field affects gameplay. The current implementation has not yet passed this in-game category checklist.

### Live editing (SDK 6)

The launcher's live mode and code mods (`set_live`) change fields in the running game through the same path as
deferred edits, with the same checks and conflict reports.

- **When a change applies.** Every mapped field has an `applies` timing in `mappings/components.json` (a component
  default, overridden per field), shown as a `[tag]` in `hellforge_components.h`:
  - `now`: the next time the game reads it (projectile, damage, explosion, arc, beam and status settings, the
    difficulty director's budgets and patrols);
  - `respawn`: when the entity is next made (component records: weapons, backpacks, vehicles, sentries; the next
    spawn, re-equip or call-in);
  - `mission`: when the next mission loads (stratagem settings, packages, level generation).

  `appliesVerified` marks timings confirmed from game code (a "?" in the header); the rest are inferred from where the
  game reads the field. Every stats-schema field is a mapped field (the schema's fields were imported into the
  mappings; a test keeps them in step), so the live editor and the SDK check the same layout.
- **`set_live(mod, target, field, value, flags)`** returns `HF_APPLIES_NOW`, `_RESPAWN`, `_MISSION` or 0 (refused,
  with the reason in the mod's log). The target is found by identity every time, never by address: an entity by its
  resource id, or a settings table row by its id (a projectile type, a damage id, a stratagem type, a level, or an
  explosion, arc, beam or status row id). One element of an array field is `HF_FIELD_AT(Component, field, index)`. A
  mod's next live edit of the same field replaces its earlier one; `value` NULL undoes it, putting back the value from
  before it. A record other entities share is changed only with `HF_LIVE_SHARED`, which changes all of them, as a
  stats file does. Live edits stay for the session and are renewed if the game reloads its data; nothing is saved.
  `hf_live(mod, target, Component, field, HF_U32(60), 0)` is the macro form. Deferred `set_field` edits reach table
  rows the same way and wait until the table has loaded.
- **`get_live(mod, target, field, &out)`** reads a field found as `set_live` finds it (a shared record too) into an
  `HfLiveValue`: `now`, what the record holds, and `base`, the field without the Local Stats mod's change and without
  live edits, so another mod's value (`out.mod` names it), else the game's own. It returns the field's timing, or 0
  when it cannot be read yet. `set_live(..., NULL, HF_LIVE_BASE)` puts the base back, past a value Local Stats applied
  at load: an editor removing a change it saved.
- **Boosters** are a settings table, `BoosterSettings` (game.dll's static booster definitions, a row per
  `HF_BOOSTER_...` value): `tuning` (+0x08 f32) is the value its effect reads, which `mappings/boosters.json` explains
  per booster (damage taken for Vitality Enhancement, stamina for Stamina Enhancement, ...), with its range. They are
  also a stats category (`"boosters"` in hellforge-stats.json, keyed `booster.<value>`), so the in-game editor
  edits and saves them. Rows are used once game.dll's image and all 21 row ids read as reviewed.
- **Armor passives** (`set_passives(mod, armor, second)`, `get_passives(mod, &state)`): the local player's armor
  passive and a second passive on top (the helmet's slot), by passive id (`mappings/passives.json`), or
  `HF_PASSIVE_KIT` for what the kits give. Solo only: with other players the kits' values come back and the override
  waits. The loader keeps it through armor changes (it looks every 250 ms) and only replaces the kits' value or its
  own. It acts after proving the 59 code pins of HD2Runtime's research. Passives whose effect needs a package
  (INTEGRATED EXPLOSIVES, ADRENO-DEFIBRILLATOR) are refused unless the armor gives them.
- **References** (HD2R Editor's payload swaps). Fields marked `"ref"` in the mappings name another table's row:
  `ProjectileWeapon.projectile` (the projectile a weapon fires), `ProjectileSettings.impact_explosion` and
  `expiry_explosion` (+0x90, +0x9C; 0 none), `DamageInfo.status_1_type` .. `status_4_type` (+0x2C, 8 apart, each
  followed by its strength; slots fill from 1), and the damage of an explosion and a projectile. A new value must be a
  row the reference catalogue knows (`mappings/references.json`, from HD2Runtime's reviewed catalogues with
  `scripts/ExportReferences.py`): a projectile type another weapon fires, a catalogued explosion, an attachable status
  effect. It is written once the packages its effects need are resident; until then the edit waits (logged), and it
  fails if they do not load within 90 seconds. An explosion whose shrapnel leads back to the projectile it is set on is
  refused; a status after an empty slot is written but logged, since the game stops at the first empty one. Stats files
  take the same references, with the same waits.
- **Packages** (`load_package(mod, package)`): HellForge loads a catalogue package through the game's own
  RefcountedPackageSystem (its request checked byte for byte), on the game thread, once per session, and holds it for
  the session: at most 64, never with the game's reference map 3/4 full. Residency is read from the engine's package
  list. Loading is per client; nothing is advertised to other players.
- **The game thread** (`on_game_thread(mod, callback, user)`): a callback every game update, on the thread the game's
  own engine calls come from (sounds, package requests), from the menu sound manager's update the loader hooks once its
  bytes are checked.
- **The in-game editor** (`native/editor`, F8; `scripts/Build-Editor.ps1`) is a code mod on these calls, with the HD2R
  Editor's layout and look. Its catalogue (`editor-catalog.json`, from `HellForge.Cli editor-data`) holds the
  primaries, secondaries, throwables, stratagems and boosters with their stats fields, and the armor perks (two choice
  rows with a picker; kept in the editor's settings and set again at every boot); its weapon and grenade silhouettes are the game's own
  HUD sprites, and its text is in the game's own FS Sinclair, both extracted from the installation's game copy into the
  editor's folder (`game-icons.bin`, `fs-sinclair*.ttf`; never published). Its buttons play the game's menu sounds
  through the game's own UI sound path (checked byte for byte; silent on another build). Every change and reset is
  staged until APPLY (F9), so DISCARD takes any of them back; applied, it goes through `set_live` and into Local Stats,
  so it comes back at every boot. Reference rows (HD2R Editor's payload swaps) open a picker instead: a weapon's
  projectile (own first, then every donor by what fires it), a projectile's impact and expiry payloads (own, none,
  then the 241 catalogued explosions in the HD2R Editor's groups) and status slots (empty, then the 35 statuses a damage
  type can apply), filtered by typing. While a weapon fires a donor its own round's rows are hidden (shown LOCKED when
  they hold a change); while a slot sets off another explosion its own explosion's rows are, and the chosen
  explosion's rows show under the slot where the editor has them (edits land on their own item). Status slots fill in
  order (IN ORDER until the slot before holds a status; a slot with later ones filled is not emptied) and are applied
  in that order.
  **Presets** (the PRESETS tab, F6 or Shift+Tab): HD2R Editor presets (`.hd2rpreset.json`, its format 1) in the
  editor's `presets` folder. Every catalogue row carries HD2R Editor's row keys on its bytes (`mappings/hd2r-keys.json`,
  rebuilt from HD2Runtime's tables by `scripts/ExportHd2rKeys.py`), so IMPORT stages each value HellForge has a row for
  (swaps first; a weapon's own round is left out while the preset swaps it; values checked as typed ones are), skips
  the rest with a count per family, and EXPORT writes the changes saved in Local Stats (and the armor perks) in the same
  format, references as HD2R Editor stores them (status ids, `{ref:"explosion"}`, `{ref:"output"}`, `"unlimited"`).
  The Armor perks category edits each perk's values (perks.c), the Boosters category every booster's fields, and two
  Advanced categories (Projectiles, Damage Types) reach rows by type id.
- **The launcher's pipe** (`native/mod-loader/live.c`, `src/HellForge.Core/LiveEdit.cs`). The launcher serves
  `\\.\pipe\hellforge-live-<session id>` (current user only) during a solo session, and the loader connects once
  the entity data has loaded. Requests name a field as the stats schema does (kind, component, resource, record,
  offset, storage) with `set`, `reset` and `get`; each becomes a `set_live` edit of the Local Stats mod
  (`zz.local.stats`), so conflicts with other mods are reported as for any edit, and the editor's own saved file is not
  a conflict. The game's hello carries its build and stats schema; the launcher sends nothing to a game running another build or schema.
  Entity deltas and status multipliers go over the pipe too (written where the stats pass writes them, below).
- **Attachments and status multipliers live** (SDK 9, `set_stat(mod, &stat, value, flags)`): the fields `set_live`
  cannot reach and `get_stat` reads (an attachment's or a default magazine's entity delta, a booster's status
  multiplier), changed in the running game in the place a stats file writes them at boot. `value` NULL puts back the
  value from before this session's first live change, or with `HF_LIVE_BASE` the field's base (another mod's value,
  else the game's). It returns `HF_APPLIES_RESPAWN`: the game reads an attachment's delta when it builds the weapon, so
  a change is expected from the next respawn or re-equip (not yet confirmed in game). The in-game editor applies its
  attachment and magazine changes this way (and saves them, so they hold from every boot).
- **Hellpod payloads** (HD2R Editor's pod rows): a support weapon's, backpack's, expendable's or Resupply's call-in
  page has a Hellpod Payload category: what its pod opens with in slots 1 to 4 (`HellpodRack.slot0_item` ..
  `slot3_item`, a `"pickup"` reference: the slot's vanilla item, nothing, or a pickup HD2Runtime reviewed, by kind:
  support weapons, backpacks, ammo, stims, grenades, supplies; `mappings/references.json` `pickups`, `podRacks`) and how
  many of them spawn (`spawn_count`, 1 to 4). The loader writes a pickup once its package is resident, and only over
  the slot's vanilla item or a pickup it put there; a mod's own racks are not checked. Pods called in after the change
  carry it; a rack several call-ins share (the EAT-17's and the Surplus EAT booster's) changes for all of them. The
  editor keeps the choices with the arm swaps, so they come back at every boot and follow the host in co-op. Untested
  in game except the pairs HD2Runtime proved (a Grenade Box in the Resupply and MG-43 pods, an EAT-700 in the
  Stalwart's slot 2), which the picker marks.
- **Whole-row copies** (SDK 9, `copy_row(mod, table, row, from, offset, size)`): a settings row's bytes replaced live by
  another row's of the same table, for the rows `mappings/references.json` `rowCopies` names (HD2R Editor's G-109 Urchin
  pulse explosion: one of three explosions' settings over its own), from the donors listed there, once the donor's
  packages are resident; `from` 0 puts its own bytes back. The in-game editor keeps the choice with the arm swaps.
- **The armory's displayed armor penetration** is a choice row after a weapon's trait slots (HD2R Editor's): None or an
  armor penetration trait, written into the slot that holds one, else the first empty slot. It changes only what the
  armory shows; the weapon's damage decides its real armor penetration.
- **Co-op** (docs/multiplayer.md step 5): a joiner plays with the host's Local Stats, including attachments, the
  in-game editor's mod content values, its arm swaps (Mounted Weapons) and hellpod payloads, and its editor shows the
  host's changes read-only. Armor perks stay solo only.

### Game events (SDK 7)

`subscribe(mod, kind, listener, user, priority)` hands a mod what happens in the game: a listener per kind, called on
the game thread (engine calls are safe there), highest priority first and equal priorities in subscription order. A
listener receives an `HfEventInfo` and casts it to its kind's struct; the data is valid during the call only.

```c
static void Spawned(HfMod* mod, const HfEventInfo* event, void* user)
{
    const HfSpawnEvent* spawn = (const HfSpawnEvent*)event;
    if (spawn->resource == sentry) HF_API(mod)->log(mod, "sentry %u landed", spawn->entity);
}
/* in hellforge_mod_init */
hf->subscribe(mod, HF_ON_SPAWN, Spawned, NULL, 0);
```

| Kind | Struct | When |
|---|---|---|
| `HF_ON_STATE` | `HfStateEvent` | the game's state changes (`HF_STATE_TITLE`, `_SHIP`, `_MISSION`, `_PREPARE_SHIP`...); `game_state` reads it any time |
| `HF_ON_MISSION_START`, `HF_ON_MISSION_END` | `HfStateEvent` | the state enters or leaves `HF_STATE_MISSION` |
| `HF_ON_PLAYER_SPAWN`, `HF_ON_PLAYER_DOWN` | `HfPlayerEvent` | a player lands (dropped in, reinforced) or dies and waits to be reinforced; players are told apart by peer id, `local` marks this machine's |
| `HF_ON_DAMAGE` | `HfDamageEvent` | damage this machine's game applies: target entity, kind, element, dealer and owner, creditor peer, amount (before armour and multipliers) and where it hit |
| `HF_ON_KILL` | `HfKillEvent` | something is killed: victim, dealer, creditor peer, kind, element |
| `HF_ON_STRATAGEM` | `HfStratagemEvent` | a stratagem's beacon activates: its type, the thrower, the beacon's position |
| `HF_ON_SPAWN` | `HfSpawnEvent` | an entity is spawned: its entity id, network id, resource (what it is) and whether this machine simulates it |
| `HF_ON_HEAL` (SDK 11) | `HfHealEvent` | a Helldiver's health went up (any player's): the amount, health now, its maximum, the player's peer |
| `HF_ON_STAT` (SDK 11) | `HfStatEvent` | the local player's mission stats grew (`HF_STAT_SHOTS`, `_HITS`, `_KILLS`, `_DAMAGE`): the growth, the total and up to five sources (the weapon, throwable or payload) |
| `HF_ON_WEAPON` (SDK 11) | `HfWeaponEvent` | what the local Helldiver holds changed: the held entity and its resource, before and now, and the inventory selection |
| `HF_ON_EXPLOSION` (SDK 11) | `HfExplosionEvent` | an explosion was requested in this machine's world: its type, where, what it came from, the creditor |
| `HF_ON_BUGHOLE` (SDK 11) | `HfBugholeEvent` | a bug hole was closed: its entity, resource, the stage it reached and where |
| `HF_ON_DAMAGE_PRE` (SDK 11) | `HfDamagePreEvent` | damage about to apply (as `HF_ON_DAMAGE`): a listener may set `damage.amount` or `cancel` |

- **Observing, not changing** (but `HF_ON_DAMAGE_PRE`). Events report; changing their data changes nothing, and
  nothing is cancelled. A pre-damage listener's `amount` and `cancel` are the exception: the game applies what the
  listeners left. Content stays game data (definition blocks, registered entities and behaviours); events are for
  logic around it.
- **Sources** (`native/mod-loader/event_sources.c`, from HD2Runtime 0.40.1's event research and game.dll's code): the
  state and the players are read every game update; damage, kills, stratagems and spawns come from hooks on the game
  functions that do them (the damage consumer, the kill dispatch, the beacon activation dispatcher, the entity spawner).
  A hook is installed only when some mod listens to its kind, and only if its first bytes read as reviewed for the
  game build; otherwise `modloader.log` says so and that kind stays silent.
- **Each machine raises its own.** Damage events are the hits this machine's game applies (a target another machine
  owns takes its damage there); a stratagem's activation is raised on its thrower's machine. Co-op behaviour of the
  other kinds on a joined client is still to be confirmed in a two-PC session.
- **Entity ids** in events are the session's entity instances (the low 22 bits index them), not resource ids; the
  spawn event pairs the two.
- The compendium's HMG sentry (src/hmg_sentry.c) logs its beacon, each of its sentries landing and the deployments per
  mission.
- **SDK 11 sources** (`native/mod-loader/event_polls.c`): heal, stats, weapon and bug hole events are read from the
  game's own records (health, mission stats, wielders, destructibles) every update or a few times a second; the
  explosion event hooks the game's explosion request, so every one is told.

### Actions (SDK 11)

What a mod makes the game do, through the game's own functions (HD2Runtime 0.40.1's research). Call them on the game
thread: from an event listener, or `on_game_thread`'s callback. Called from another thread an action waits for the next
game update and answers `HF_ACT_QUEUED`. Each answers `HF_ACT_DONE`, `HF_ACT_REQUESTED` (the game does it in this
update or the next), `HF_ACT_WAITING` (its packages are loading: call again), or 0 refused with the reason in
`last_log`.

| Call | Does |
|---|---|
| `heal(mod, amount)` | heals the local Helldiver (each limb by the same share), up to its maximum |
| `heal_limb(mod, limb)`, `injure(mod, limb, damage)`, `limb_state(mod, entity, limb, &out)` | a limb (`HF_LIMB_HEAD` .. `_RIGHT_LEG`): healed in full, damaged as a hit would, or read |
| `apply_status(mod, entity, status, buildup)` | a status (`HF_STATUS_FIRE`, `_STUN_LARGE`, `_GAS`, ...) on an entity; host |
| `add_velocity(mod, delta)` | pushes the local Helldiver (an upward push holds; walking resets sideways speed) |
| `resupply_from_pack(mod)` | the local Helldiver uses its B-1 Supply Pack on itself |
| `spawn_explosion(mod, type, position)`, `spawn_projectile(mod, type, position, direction)` | an explosion or a projectile, credited to this machine's player; host; their packages loaded first |
| `credit_to_player(mod, entity)` | a turret's or vehicle's kills credited to its owner; solo host |
| `freeze_mission_timer(mod, frozen)` | pauses or resumes the mission timer; host |
| `spawn_pelican(mod, &call, &entity)` | calls in a transport Pelican to hover over a point (`HfPelicanCall`); host |
| `modify_shots(mod, &rule)`, `homing(mod, &rule)`, `stop_shots(mod, id)` | rules for the local player's own shots of a projectile type: damage, penetration, speed, gravity and drag multipliers, or steering towards enemies (or friends); solo unless `HF_SHOT_MULTIPLAYER` |

```c
static void Healed(HfMod* mod, const HfEventInfo* event, void* user)
{
    const HfHealEvent* heal = (const HfHealEvent*)event;
    if (heal->local && heal->amount >= 50) HF_API(mod)->heal_limb(mod, HF_LIMB_LEFT_LEG); /* a big heal mends a leg too */
}
```

Actions are logic around the game, like events: they make the game do what it does (a hit, a status, an explosion),
never new content. New weapons, projectiles and stratagems stay game data (definition blocks and registries).

## Native mods

Native manifests support network field types and legacy component writes. Each edit is checked against this exact
build before it is written; anything that is not as reviewed is logged and left alone. `HellForge.Cli mods-apply`
continues to check network field types beside the running game. Component writes now use the stats loader pass above
and are no longer periodically restored.

### `networkFieldTypes`: widen a network field type

```json
{ "name": "deposit_value", "expect": { "kind": 1, "bits": 10, "min": 0 }, "bits": 14 }
```

The engine's loaded network configuration (`[[helldivers2.exe+0x1A10268]]`) lists its field types at `config+0x18`
(`config+0x10` entries of 0x18 bytes: name hash at +0, kind at +12, bits at +13, min at +16). The type is found by its
name hash (IdString32: upper half of MurmurHash64A, seed 0) and must be unique and exactly as expected. Only integer
types (kind 1) can be widened, to at most 31 bits. The engine validates every integer field written to the network
against `min .. min + 2^bits - 1` and clamps it in place (exe 0x34BAB0), so a wider type raises that limit for every
field of that type. All players must run the same edit; the modlist digest makes sure of that.

### `componentWrites`: set fields of an entity's component record

```json
{ "component": "DepositComponentData", "resource": "0x056DE1C5E21E723E",
  "fields": [ { "name": "capacity", "offset": 0, "expect": 1000, "value": 10000 } ] }
```

The game keeps entity component records in one private allocation of a fixed size whose first 28 bytes are the
entity map header. HellForge finds it and each component's index exactly as HD2Runtime's `core/entity_catalog.lua` does,
using the profile `src/HellForge.Core/entity-profile.json` (exported from HD2Runtime's reviewed `schemas/current.lua` by
`scripts/ExportEntityProfile.py`; pinned to the exe and game.dll SHA-256). The resource must own exactly one record of
the component. Shared records now produce a warning, as with stats data mods. Fields are u32 at a 4-byte aligned offset
inside the record, and their original value must equal `expect`. Resource ids and offsets come from HD2Runtime's catalogs
(`domains/entity_authoring.lua`, the `backing` of each field).

## Example: the Stratagem Compendium

`examples/mods/stratagem-compendium` is the HD2Runtime custom stratagem pack 0.4.1 as game data: eleven stratagems, one
source file per family, each a worked example of the SDK.

| File | Stratagems | What it shows |
|---|---|---|
| `src/pelicans.c` | Pelican Gatling, Cannon, EMS and Gas Support | entities with a mount, a registered behaviour, turrets with rounds of their own (a ProjectileSettings edit for the EMS field and gas cloud), sounds from another entity |
| `src/hmg_sentry.c` | A/MG-101 Heavy MG Sentry | `own_all`, entity text (the designation), the magazine pattern, game events |
| `src/orbital_barrages.c` | Orbital Gas and EMS Barrages | a bombardment entity whose shell list (`HfBombardment.shells`) names a shell of its own |
| `src/expendables.c` | EAT-77 Cluster, EAT-40 Gas, EAT-23 EMS | a weapon entity and the hellpod rack delivering two of it (`HfHellpodRack.slot0_item`, `slot1_item`) |
| `src/shredder_silo.c` | MS-N223 Shredder Silo | a missile entity with another explosion (`HfExplosive.explosion`), a targeting remote of its own counting down 10 s with the ICBM's countdown before the missile launches (`add_ability`, `HF_ABILITY_HOLDS_LINKED_LAUNCH`), in a rack of its own |

Every stratagem owns what it changes and as much else as HellForge can give it (its entities with `own_all`, its rounds,
its damage), so tuning it, with code or in the in-game editor, never changes a vanilla stratagem or weapon. The editor
lists each under its category with the page of the stratagem it is built on, pointed at its own records. A package a
stratagem needs besides its payload's and its `loads_item`'s (another stratagem's shell, another weapon's round) is held
for the session with `load_package`. What the pack did per call at run time and has no data form: the silo's fallback
blast (a mod names one) and its one-per-player limit.

## Example: Machine Gun 10k

`examples/mods/machine-gun-10k`: the M-1000 Maxigun's backpack holds 10,000 rounds. HD2Runtime's research
(`local_research/events/maxigun/deposit-limit-notes.md`) found why no Lua mod can: a backpack's live ammo is the
network field `remaining` of type `deposit_value` {int, 10 bits, min 0}, so the engine clamps it to 1,023 at the first
shot after any value above that. The mod widens `deposit_value` to 14 bits (up to 16,383) and sets the backpack's
`DepositComponentData` (record 23 of resource `0x056DE1C5E21E723E`) capacity and start amount to 10,000 and refill to
5,000. The HUD maximum is the start amount, so it shows 10,000.

## Example: AR-23G Liberator Gold

`examples/mods/golden-liberator`: a gold-plated copy of the AR-23 Liberator as its own arsenal weapon, about 20 lines
of C (`add_weapon` above). Its `hellforge-data.json` describes the model: the Liberator's unit under the mod's own name,
pointing to a copy of the body material whose colour lookup texture is gold metal in every zone (every zone takes the
bare-metal zone's values). HellForge builds it from each player's game copy (Data specs above).
`examples/mods/talon-liberator` does the same in blue and adds the blue copies of the Talon's bolt and flight effects,
everything they use, and the Sickle's sound banks. Both build byte for byte what the research scripts
`artifacts/registry/build_gold_liberator.py` and `build_talon_liberator.py` built.

# HellForge roadmap

What HellForge can do today, what is being tested, and what comes after. Updated 2026-10-23 for HellForge 0.3.0-dev.1.
Nothing here has a date, and stability comes first.

## At a glance

| Area | Status | Where it stands |
|---|---|---|
| Local game copy | Done | Shipped in 0.2.0 |
| Co-op with friends | Done | Shipped in 0.2.0 |
| [Mod SDK](sdk-roadmap.md) | Done | SDK 14; some events and registries left |
| Build patches for newer Steam builds | Done | Each new Steam build needs a new patch |
| Open source | Done | GPL-3 |
| In-game editor | Mostly done | Player's kit mostly ported; enemies, structures and missions left |
| Difficulty 11 (Hell Forge) | Mostly done | Needs its own enemy mix |
| Linux | Experimental | Runs under Proton; co-op and Steam Deck untested |
| Public lobbies, privacy, invites, kicking | Testing | Built for 0.3.0; needs two-PC tests |
| Faster mod sync | Testing | Built for 0.3.0 |
| Host-leaves fix | Next | Planned |
| Co-op tools for mods | Next | Planned |
| Super Earth and Cyberstan missions | Later | Research first |
| Subfactions | Later | Research first |
| Custom missions | Later | Planned in steps |
| Progression system | Later | Design not decided |
| Custom models | Later | Mesh importer works; physics, AI and textures left |
| 1.0 release | Later | No date yet |

Not planned: a way onto the live servers, play with people outside HellForge, support for the EAC builds, or getting
around an active anti-cheat. See also [known issues](#known-issues), and the [README](../README.md) for what HellForge
is.

Much of this is mod SDK work underneath: the in-game editor, co-op tools for mods, difficulty 11's enemy mix,
subfactions, custom missions, and mod content in progression. The [SDK roadmap](sdk-roadmap.md) tracks that side.

## The details

The rest of this page goes through each item. Contributors will find more detail in [engine-roadmap.md](engine-roadmap.md)
(session runner, local backend, mod loader, stand-ins) and [sdk-roadmap.md](sdk-roadmap.md) (the mod SDK).

Versions: HellForge 0.3.0-dev.1, engine 0.1.29, mod SDK 14.

## Done in 0.2.0-beta

- The local game copy, the local backend, and the WFP network block, which setup installs for you.
- The first step of the network census: the game logs every connection it tries to make.
- Mod content syncs in co-op. Added stratagems, entities, behaviours and abilities get the same numbers on every PC,
  and new behaviours and abilities sync the way the game's own versions do.
- The mod SDK's foundations: fields by name instead of raw offsets, looking content up by name, and the mod kit (a
  template, the CLI, and mods built from source).
- Live editing of game values, with the host's values used for everyone in co-op.
- The in-game editor, a port of the HD2R Editor that is mostly finished.
- Hook chains, so several mods can hook the same game function, and most of the game events (SDK 13).
- Mod groups, modlist codes, cosmetic packs, and a CLI for mod managers.
- Difficulty 11, "Hell Forge", as a mod: bigger maps, more objectives and outposts, a 72-minute timer, more patrols,
  two missions in one, and fortresses and mega nests in the mega cities.
- Build patches: setup turns a newer Steam build into the build HellForge runs ([downpatch.md](downpatch.md)).
- Linux under Proton, experimental, from the community's work in issue 1 ([linux.md](linux.md)).
- The source code, released under GPL-3.

## 0.3.0-beta (built, now in testing)

This is on the `dev` branch.

Other HellForge players' public lobbies now show up on the Galactic Map. HellForge lists them on Steam's lobby service,
and only HellForge players can see them (CowboyBingus, PR #17). If you'd rather not appear there, a switch in the
launcher keeps your Public lobbies off the map.

When you join someone, mod sync downloads the host's mods for that session. You can trust a Steam friend's code mods
after reading a warning, but a stranger's code mods have to be installed by hand. Downloads run at several MB/s: 38 MB
takes about 10 seconds now, where it took about 3 minutes before commit 5fb79fe. Mods can also list a web address, which MOD
SYNC shows as a GET IT button.

Your lobby's privacy setting decides who can join: Public lets in any HellForge player, Friends only lets in your Steam
friends, and Invite only lets in the people you invite. In-game invites and kicking both work. Multiplayer now requires
Local PlayFab.

Fixed: issue #5 (the F8 editor didn't show with RivaTuner or Afterburner running), plus #6, #13, #18 and #20. Resets in
Local Stats are no longer lost.

Before release we still need two-PC tests: privacy with a friend, a stranger and an invited player; invites; kicking;
mod sync with its restart and rejoin; and a public lobby on the map. The editor's overlay test also needs one run with
RivaTuner closed.

## Next: stability and co-op

When the host goes back to the ship in the middle of a mission, everyone else gets dropped to the main menu. The fix
will keep the lobby and network alive and hand ownership to another player. If the host quits HellForge entirely, the
session will still end, because fixing that means handing the session over to another player's backend.

Mods need a way to tell which machine is in charge of an entity (`is_authority`) and a channel for sending messages
between players (`send`). That covers state the game itself has nowhere to keep, such as the point a laser has marked or
the target a custom turret has picked. The Pelican's facing and aim height will move onto it
([SDK roadmap, phase 6](sdk-roadmap.md#co-op-sync-phase-6)).

More events for mods: a player joining or leaving, an objective being completed, and a level being generated
([SDK roadmap, phase 5b](sdk-roadmap.md#events-hooks-and-registries-phase-5b)).

The network census has two more steps. Step 2 names every destination the game copy tries to reach. Step 3 answers or
refuses each one and makes a test fail whenever a new destination shows up. We re-run it for every game build HellForge
pins.

CowboyBingus is hardening security with the Steamworks SDK. Public lobbies are being reviewed with hostile players in
mind: checking what they send, kicking them, and rate limits.

## War and missions

The local backend can serve the war two ways. It can follow the live war read-only through the community API mirror, or
it can run "All planets open" (`OpenWar.cs`), where every planet except Super Earth gets a liberation campaign. Some of
the game's content only appears when the war sets it up, and "All planets open" doesn't set any of it up yet.

### Super Earth missions

Status: planned. Research comes first.

Super Earth (planet 0) has seven regions in the war data, which are its mega cities. "All planets open" never gives
Super Earth a campaign and clears every planet event, so its missions can't show up. With the live war they show up
only while the live war has them. The plan:

1. Find out, without changing anything, what the game needs before it offers a Super Earth mission: the campaign or
   defense event, which race it's against, the regions, and anything else the mission generator checks.
2. Have "All planets open" give Super Earth and its mega cities missions against the enemies the game allows there.
3. Add a launcher setting to turn them on or off.

### Cyberstan missions

Status: planned. Research comes first.

In the war data, Cyberstan is the Automatons' home world (planet 260). In "All planets open" it gets an ordinary
liberation campaign. The research is the same as for Super Earth: find which campaign type, event, regions or effects
bring up Cyberstan's own missions, then have "All planets open" serve them.

### Subfactions

Status: planned. Research comes first.

When the game picks an enemy to spawn, it only considers roster rows whose subfaction is active for that mission
(HD2Runtime's spawn research; the pick is at game.dll 0x953150). We don't know yet what makes a subfaction active. The
most likely answer is planet effects: the frozen war has 128 of them, with over 60 different ids. "All planets open"
moves planets from one enemy to another but leaves their effects as they were. The plan:

1. Find out what decides a mission's subfaction (planet effects, region settings, or the mission generator), and name
   every subfaction and effect id.
2. Have "All planets open" give each planet subfactions that fit its enemy, the way the live war does.
3. Let players choose subfactions per session or per planet, in the launcher or the editor, and let mods set them
   through the SDK ([SDK roadmap, phase 5b](sdk-roadmap.md#events-hooks-and-registries-phase-5b)). A subfaction is
   set up with the mission and never swapped in per spawn.

### Difficulty 11 and custom missions

Difficulty 11 ("Hell Forge") is done apart from its enemy mix. For now every enemy's chance of spawning at level 11 is
a copy of its chance at level 10, so the mix is Super Helldive's with bigger budgets. It needs a way to set its own,
which is an SDK registry for its enemy weights.

Custom missions would come in steps. First, a Missions category in the editor for timers and per-difficulty settings.
Second, remixed missions through a mission registry like the stratagem one, once we've studied the mission definition
record. New objective logic would be the third step, and it's probably out of reach.

We also want objectives, mission rows, operations, outposts and stamps as SDK registries. Those registries and the enemy
weights are in the [SDK roadmap, phase 5b](sdk-roadmap.md#events-hooks-and-registries-phase-5b).

## Progression

Status: planned. The design isn't decided yet.

A new profile starts from a template: `starter` (a fresh player just out of basic training) or `unlock-all`
(everything, at the top level). For now the local backend pays out with simple placeholder rules (`MissionService.cs`):
XP based on difficulty, success and objectives, requisition based on difficulty, and whatever samples the game reports
you looted. We want playing up from `starter` to be fun, without turning into a grind or giving everything away. The
plan:

1. Make the whole loop work. Everything a profile earns should be spendable on what the game sells for it (stratagems,
   ship modules, warbond pages, the store), and every way of earning should pay out (medals, super credits, operation
   rewards). The first job is listing which of the game's progression requests the backend answers today.
2. Tune the rewards. That means how fast a profile levels up and unlocks things, per difficulty and across a whole
   operation. The numbers live in one data file, so players and mods can change the pace: faster, slower, or hardcore.
3. Bring HellForge's own content in. Mod stratagems and weapons get a level requirement, a price or a warbond page
   through an SDK call ([SDK roadmap, phase 5b](sdk-roadmap.md#events-hooks-and-registries-phase-5b)), and
   difficulty 11 unlocks through progress.
4. Add more start templates between `starter` and `unlock-all`.

In co-op, every player keeps their own profile and earns rewards on their own machine.

## Modding

This section covers what players will notice. The [SDK roadmap](sdk-roadmap.md) lists everything left in the mod SDK,
phase by phase.

HD2Runtime runs on HellForge through the Lua bridge, and its edits rank between the editors' and code mods'
([lua-bridge.md](lua-bridge.md)). Some of the bridge's events and actions still have no engine call behind them.

The in-game editor should eventually edit every field the HD2R Editor does
([SDK roadmap, phase 5a](sdk-roadmap.md#the-in-game-editor-phase-5a)). That happens in three steps:

1. Weapons and the rest of the player's kit. Weapons, stratagems, backpacks, vehicles, boosters, the Helldiver and
   armor are mostly ported. Still to do: beam conversion, the HD2R rows that have no HellForge field yet (a coverage
   count will show which), and checking in game when each field takes effect.
2. Enemies: their stats, damage zones and attacks, spawn rosters and compositions. More than half of the HD2R Editor's
   roughly 20,700 fields are in this step.
3. Structures, with their demolition and destruction settings, and mission settings (the other Spawning & Destruction
   pages).

For custom models, the OBJ mesh importer works in game (the cube gun proved it). Copied units still need working
physics and AI (the blue sentry has neither yet), along with animation events and PNG textures.

The launcher should show which mods hook what and where mods conflict
([SDK roadmap, phase 5b](sdk-roadmap.md#events-hooks-and-registries-phase-5b)).

## Platform and install

Linux support shipped in 0.2.0 as experimental ([linux.md](linux.md)). Testers in issue 1 have run the game under
Proton, but co-op on Linux, the Steam ownership check under Proton, and Steam as a Flatpak or on the Steam Deck are
still untested.

Build patches work: setup picks a patch from the list online and keeps Steam's depot download as the fallback. Each
new Steam build of HD2 needs a new patch.

Setup still needs a full run through its GUI on a fresh PC.

## Known issues

| Issue | Workaround |
|---|---|
| Smart App Control blocks the unsigned session exe (exit code `0xC0000428`) | Turn Smart App Control off. Windows can't turn it back on without a reset ([troubleshooting.md](troubleshooting.md)) |
| HellForge's editor and the HD2R Editor clash | Use one at a time |
| Antivirus programs can quarantine HellForge's files | Add an exclusion for the install folder ([troubleshooting.md](troubleshooting.md#antivirus)) |
| A crash at `helldivers2.exe+0x626296` when the game closes | Nothing to fix: it's the game's own crash on quitting |

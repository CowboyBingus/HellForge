# Using mods

For players: how to add, order and remove mods in HellForge, and what friends need to play together. To make mods, see
[modding/README.md](modding/README.md). Mod managers and launchers of their own use
[external-launchers.md](external-launchers.md).

## What HellForge takes

Mods live in `<installation>/mods`, each as a folder or a `.zip` of one. HellForge takes:

- HellForge mods: a `hellforge-mod.json` with code (sources HellForge builds for you), data, stats, or several of them.
- Cosmetic packs in the community's format (a `manifest.json` with options), as other mod managers install them.
- Lua mods: HD2Runtime, Bingus Shared Loader and the mods made for them ([lua-bridge.md](lua-bridge.md)).

A code mod runs inside the game with your permissions, so only enable code mods from people you trust. The Mods page
marks them CODE.

## Adding mods

1. Open the launcher's Mods page and press Add mods....
2. Pick one or more files: a mod's `.zip` (or `.rar`, `.7z`), a folder mod's `hellforge-mod.json`, or a cosmetic pack's
   `manifest.json`. HellForge copies each mod into the mods folder.
3. When a mod has the same id as one already installed (an update, or going back to an older version), the page lists it
   and waits: press Add to replace the installed one, whose files are deleted, or Cancel.

Copying a mod into the mods folder yourself works too (Open mods folder opens it), and the page shows it at once.

Some things to know:

- `.rar` and `.7z` files are unpacked with Windows' own `tar` (Windows 11). If Windows cannot unpack one, unpack it
  yourself and add its folder.
- Keep a mod as its zip: HellForge unpacks it by itself. Two copies of one mod (a zip and a folder with the same id) load
  neither, and the page says so.
- Files that are not mods are listed under Mod notices with the reason, until the next Add mods.

## The Mods page

Every mod is a row with a switch, in two groups: gameplay mods first, then cosmetic ones. Cosmetic mods are cosmetic
packs, Lua mods and mods that say they are cosmetic; friends need not have the same ones.

- Click a row's name for what the mod changes. Its ⋮ menu (or a right click) has Details, Move up and Move down, Build
  now for a mod with sources, Zip to share for your own folder mods, Open folder, and Remove... for your own mods
  (not the bundled mods or Local Stats). Remove asks before it deletes the mod's files.
- Tags on the right say what a mod is, among them CODE, DATA, STATS, PACK, LUA, ZIP and BUNDLED (shipped with
  HellForge). DID NOT COMPILE marks a source mod whose last build failed; it stays off until fixed, and its details show
  the compiler's message.
- A pack's details let you switch its options on and off (or All on, All off) and pick their variants.
- Mod notices, at the top in red, say why a mod could not be loaded.
- The modlist code friends must match is at the bottom of the page.

Changes apply to the next session. While a session runs, the page is locked and the session keeps the mods it started
with.

The page saves its switches, order and pack options in `mods/hellforge-mods.json`.

## Load order

Drag a row within its group to change the order, or use Move up and Move down. The bottom mod loads first and the top
one wins, as in other mod managers: where two mods change the same thing, the higher one's change stays. Mods new to the
page go to the top of their group. The in-game editor's Local Stats (your saved editor changes, tagged YOUR EDITS) stays
on top, so it applies last.

## Playing with friends

Friends who play together need the same gameplay mods: the same mods, the same versions and the same files. The order
is up to each of you, and cosmetic mods, packs and Lua mods are your own. The Mods page shows the modlist code to
compare. With no gameplay mods there is nothing to compare, and you can play with friends who have none.

When a friend's code is not the host's, the join is refused and both games say why, over the game:

- The joiner sees "Couldn't join <host>" for 20 seconds, with each mod that differs: one the host has and they do not,
  one only they have, another version, or the same version with other files.
- The host sees "<friend> couldn't join" for 12 seconds, with the same list from their side.

A different HellForge version is reported the same way, and the joiner is told when a lobby is full or locked. The
bundled in-game editor draws these notices. With it switched off, only the logs say why: `backend/multiplayer.log` in
each session's folder, the first lines of the list.

In co-op everyone plays with the host's Local Stats (the in-game editor's saved changes), so yours is not part of the
modlist code.

If a mod does not show up in game, see [troubleshooting](troubleshooting.md#sessions-that-fail). For the docs map, see
[README.md](README.md).

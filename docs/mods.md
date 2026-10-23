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

When the host lets joiners download their mods, the joiner also gets the MOD SYNC prompt (below).

In co-op everyone plays with the host's Local Stats (the in-game editor's saved changes), so yours is not part of the
modlist code.

## Playing with other mods

Joining a lobby whose host runs other gameplay mods does not just fail. The host's backend refuses the join (above) and
sends its mod set with the refusal. A **MOD SYNC** prompt then shows over the game by itself, without the editor
window behind it (the in-game editor draws it). It holds the mouse and keyboard until it is answered, and lists each of
the host's mods and what happens to it:

| Shown as | Meaning |
|---|---|
| you have it | an installed mod (on or off) or one downloaded before has the same id and content |
| downloads | it comes from the host's backend over the Steam peer link |
| install it yourself | the host does not share their mods, or it runs code or carries data that HellForge cannot check and has not verified |
| blocked | it is on your blocklist, so the lobby cannot be joined |
| needs another HellForge | a mod HellForge ships differs: both players need the same HellForge |
| off for this session | one of your own gameplay mods that the host does not run |

**DOWNLOAD AND RESTART** downloads what it can; **Esc** hides the prompt while the download goes on, and it comes back
when the mods are ready. **CANCEL** or **Esc** before that answers no. **RESTART TO JOIN** then ends the session and
the launcher starts a new one with the host's mods, in the host's load order, plus your own cosmetic mods and Local
Stats. Join the lobby again from the social tab or the map. Only that one session plays the host's mods: your Mods page
and its switches never change, and the next session you start yourself runs your own mods again.

**What may download (the trust rule).** Only what HellForge checks itself downloads by itself. Your backend decides
each mod's class from the bytes it received, never from what the host says, and the rule is the same for every host,
Steam friends included:
- *Content* downloads from any host: data specs (`hellforge-data.json`, whose patches HellForge builds from your own
  game files), schema stats (`hellforge-stats.json` fields by name, each within its reviewed range) and network field
  types (an integer field widened, never past 31 bits).
- *Raw data* reaches the game without HellForge being able to check it: binary data patches (a `data/` folder, read by
  the game's own file parsers), raw stats lines (the stats file's `"raw"` section) and `componentWrites` (any field of
  a component record). It downloads only as code does, below.
- *Code* (a DLL, C or C++ sources HellForge compiles, or a patch holding Lua) and raw data download only when
  HellForge's verified list names that exact content: `data/verified-mods.json`, shipped with each HellForge release,
  entries `{"id","name","version","hash","reviewed"}`. The hash is the mod's content hash: the SHA-256 of the lines the
  modlist code takes from it (`ModSets.ContentHash`). Any other such mod is one you install yourself; the prompt marks
  it CODE or RAW DATA.
- *Bundled* mods (the in-game editor) never download.

**Checks.** The file list, the manifest and the stats file come first: a mod that shows code or raw data there and is
not verified stops before its other files. Every file must have a safe path and match the size and SHA-256 the host
listed. The finished mod must parse, have the expected id and version, rehash to the host's content hash, and be
content or verified. Only then is it kept, in `mods/.synced/<id>@<hash>/`; a failed download is deleted. Normal
sessions never load that folder.

**Blocking.** **BLOCK** on a row of the prompt (or **B**) puts that mod on your blocklist: lobbies running it are not
offered again. Unblock it on the launcher's Mods page (MOD SYNC card).

**Hosting.** Players who join you may download your gameplay mods' counted files for 10 minutes after their join is
refused. Your backend serves them at most 1 MB/s per player and 2 MB/s in total, so your own game keeps its link. Turn
it off with "Let players who join me download my gameplay mods" on the Mods page.

**Downloaded mods.** The Mods page shows how many there are and how much space they take, with a button that deletes
them all.

**Public lobbies on the Galactic Map.** The map lists other HellForge players' public lobbies, except the ones you could
not join:
- lobbies running a blocked mod;
- lobbies running mods that would have to be installed by hand;
- lobbies whose host plays with edited stats (Local Stats, the in-game editor's values or arm swaps). Joiners play with
  the host's stats, so these are hidden unless "Show public lobbies whose host plays with edited stats" is on.

Lobbies with other mods you can download still show. Joining one shows the MOD SYNC prompt.

For mod authors, the prompt is the SDK 14 calls `sync_state`, `sync_mod` and `sync_answer`
([sdk-reference.md](sdk-reference.md)).

If a mod does not show up in game, see [troubleshooting](troubleshooting.md#sessions-that-fail). For the docs map, see
[README.md](README.md).

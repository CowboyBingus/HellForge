using System.Globalization;
using System.Text.Json.Nodes;

namespace HellForge.Core;

// Changing a running session's mods without a restart (docs/mods.md "Changing mods while you play"). The session's
// backend compares the mods the session runs (SessionMods, written by mods-stage) with the mods it would run now (the
// Mods page's, or a host's after a mod sync), and sends the in-game loader the change (LiveModPlan) over the live pipe
// (HellForge-Engine native/mod-loader/live_mods.c). For now only stats mods change live: their stats and componentWrites.
// Any other difference (code, Lua, data patches, cosmetic packs, network types, the order) waits for the next session.

// A mod as the session runs it: its content hash and pack choice tell whether the mod on disk is still the one that runs.
public sealed record SessionMod(string Id, string Version, string Folder, string Hash, string Choice, bool Loader, bool Live);

// The session's mods in load order, and the mods its load order named that did not load (a source mod that did not
// build, a data spec that could not be made), so they do not count as added. SyncedWith: the host whose mods it plays
// after a mod sync ("" its own).
public sealed record SessionMods(IReadOnlyList<SessionMod> Mods, IReadOnlyList<SessionMod> Skipped, string SyncedWith)
{
    public const string FileName = "session-mods.json";

    public static SessionMod Of(ModManifest mod) =>
        new(mod.Id, mod.Version, mod.Folder, ModSets.ContentHash(mod), ChoiceText(mod), mod.NeedsLoader, LiveModPlan.Why(mod) == null);

    public static SessionMods Of(IReadOnlyList<ModManifest> loadOrder, IReadOnlyList<ModManifest> active, string syncedWith) =>
        new([.. active.Select(Of)], [.. loadOrder.Where(m => active.All(a => a.Id != m.Id)).Select(Of)], syncedWith);

    // A cosmetic pack's choice as text: which options are off and which variants are picked ("" for other mods).
    public static string ChoiceText(ModManifest mod) => mod.Pack == null ? "" :
        string.Join(',', mod.PackChoice.Off.Order(StringComparer.Ordinal)) + ";" +
        string.Join(',', mod.PackChoice.Sub.OrderBy(p => p.Key, StringComparer.Ordinal).Select(p => p.Key + "=" + p.Value));

    public void Write(string path)
    {
        static JsonArray List(IEnumerable<SessionMod> mods) => [.. mods.Select(m => (JsonNode?)new JsonObject
        {
            ["id"] = m.Id, ["version"] = m.Version, ["folder"] = m.Folder, ["hash"] = m.Hash, ["choice"] = m.Choice, ["loader"] = m.Loader, ["live"] = m.Live
        })];
        var root = new JsonObject { ["format"] = 1, ["syncedWith"] = SyncedWith, ["mods"] = List(Mods), ["skipped"] = List(Skipped) };
        File.WriteAllText(path + ".partial", root.ToJsonString());
        File.Move(path + ".partial", path, overwrite: true);
    }

    public static SessionMods Read(string path)
    {
        var root = JsonNode.Parse(File.ReadAllText(path)) as JsonObject ?? throw new InvalidDataException("The session's mods file is not an object.");
        if ((int?)root["format"] != 1) throw new InvalidDataException("The session's mods file has no format 1.");
        static List<SessionMod> List(JsonNode? node)
        {
            var mods = new List<SessionMod>();
            foreach (var m in node as JsonArray ?? [])
            {
                var mod = new SessionMod((string?)m?["id"] ?? "", (string?)m?["version"] ?? "", (string?)m?["folder"] ?? "", (string?)m?["hash"] ?? "",
                    (string?)m?["choice"] ?? "", (bool?)m?["loader"] ?? false, (bool?)m?["live"] ?? false);
                if (!ModCatalog.IsModId(mod.Id) || !ModSets.Hex(mod.Hash, 64)) throw new InvalidDataException("A mod in the session's mods file is malformed.");
                mods.Add(mod);
            }
            return mods;
        }
        return new SessionMods(List(root["mods"]), List(root["skipped"]), (string?)root["syncedWith"] ?? "");
    }
}

public enum LiveModAction { Add, Remove, Replace }

// One mod's part of a change. Mod: the mod as it will run (null for a removal); Was: as it ran (null for an addition).
public sealed record LiveModChange(string Id, string Name, LiveModAction Action, ModManifest? Mod, SessionMod? Was);

// A change of the session's mods: Live when the loader can take all of it now; otherwise Blocking says, mod by mod, what
// waits for the next session. Target: the mods the session runs after it, in load order.
public sealed record LiveModPlan(IReadOnlyList<LiveModChange> Changes, IReadOnlyList<string> Blocking, IReadOnlyList<ModManifest> Target)
{
    public bool Live => Changes.Count > 0 && Blocking.Count == 0;
    public bool Empty => Changes.Count == 0 && Blocking.Count == 0;

    // Why a mod cannot change while the game runs, or null: only stats mods (stats and componentWrites) do for now.
    public static string? Why(ModManifest mod) =>
        mod.HasCode || mod.HasSource ? "it runs code (a DLL)" :
        mod.Lua ? "it holds Lua scripts" :
        mod.HasDataSpec || mod.DataFiles.Count > 0 || mod.Pack != null ? "it patches game data" :
        mod.NetworkFieldTypes.Count > 0 ? "it changes network field types" :
        StatsData.Edits(mod).Any(e => e.Own) ? "it gives an entity a record of its own (\"own\")" : null;

    public static LiveModPlan Make(SessionMods current, IReadOnlyList<ModManifest> target)
    {
        var changes = new List<LiveModChange>();
        var blocking = new List<string>();
        var was = current.Mods.ToDictionary(m => m.Id, StringComparer.Ordinal);
        var skipped = current.Skipped.ToDictionary(m => m.Id, StringComparer.Ordinal);
        var now = target.Select(m => m.Id).ToHashSet(StringComparer.Ordinal);
        void Change(LiveModAction action, string id, string name, ModManifest? mod, SessionMod? old)
        {
            var why = mod != null ? Why(mod) : null;
            why ??= old != null && !old.Live ? "it ran code, Lua, data patches or own records" : null;
            if (why != null) blocking.Add($"{name}: {why}");
            else changes.Add(new LiveModChange(id, name, action, mod, old));
        }
        foreach (var mod in target)
        {
            if (!was.TryGetValue(mod.Id, out var old))
            {
                // a mod that did not load at the session's start, unchanged since, is not new
                if (skipped.TryGetValue(mod.Id, out var notLoaded) && notLoaded.Hash == ModSets.ContentHash(mod)) continue;
                Change(LiveModAction.Add, mod.Id, mod.Name, mod, null);
            }
            // Local Stats is the editors' save file: what they save, they have already changed live
            else if (mod.Id != ModCatalog.LocalStatsId && (old.Hash != ModSets.ContentHash(mod) || old.Choice != SessionMods.ChoiceText(mod)))
                Change(LiveModAction.Replace, mod.Id, mod.Name, mod, old);
        }
        foreach (var old in current.Mods.Where(m => !now.Contains(m.Id))) Change(LiveModAction.Remove, old.Id, old.Id, null, old);
        // the mods that stay keep their order: stats apply in it, and data patches were staged in it
        var stayed = current.Mods.Where(m => now.Contains(m.Id)).Select(m => m.Id).ToList();
        var stays = target.Where(m => was.ContainsKey(m.Id)).Select(m => m.Id).ToList();
        if (!stayed.SequenceEqual(stays, StringComparer.Ordinal)) blocking.Add("The mods' order changed");
        return new LiveModPlan(changes, blocking, target);
    }

    // The loader's lines, verb then fields (the live pipe adds the sequence number): the loader's mods after the change in
    // load order, then each change with its stat lines. txn: the change's id; sync: mod sync applying a host's set.
    public IEnumerable<(string Verb, string[] Fields)> Lines(string txn, bool sync, string digest)
    {
        yield return ("modbegin", [txn, sync ? "sync" : "solo", digest]);
        var index = 0;
        foreach (var mod in Target.Where(m => m.NeedsLoader))
            yield return ("modorder", [txn, (index++).ToString(CultureInfo.InvariantCulture), mod.Id]);
        foreach (var change in Changes)
        {
            var loads = change.Mod?.NeedsLoader == true;
            var loaded = change.Was?.Loader == true;
            if (loaded && (change.Action == LiveModAction.Remove || !loads)) { yield return ("modremove", [txn, change.Id]); continue; }
            if (!loads) continue; /* a mod the loader never sees: only the digest changes */
            var mod = change.Mod!;
            var hash = ModSets.ContentHash(mod)[..16];
            yield return (loaded ? "modreplace" : "modadd", [txn, mod.Id, "stats", hash, mod.Folder, "-"]);
            foreach (var edit in StatsData.Edits(mod)) yield return ("modstat", [txn, .. ModLoaderPlan.StatFields(mod, edit)]);
        }
        yield return ("modcommit", [txn]);
    }
}

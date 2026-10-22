using HellForge.Core;
using HellForge.Core.GameData;

// Changing a running session's mods (LiveModPlan): stats mods added, removed and replaced plan live, with the loader's
// lines; code, data patches, Lua, own records and a new order wait for the next session; a mod that did not load at the
// start is not new; the session's mods file round-trips. Every mod here is synthetic.
static class LiveModsTests
{
    const string Rpm = """{"format":1,"weapons":{"AR-23 Liberator":{"rpm":900}}}""";

    static void Mod(string mods, string id, string extra = "", string? stats = null)
    {
        var folder = Path.Combine(mods, id);
        Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, ModCatalog.ManifestName), $$"""{"format":1,"id":"{{id}}","name":"{{id}} mod","version":"1"{{extra}}}""");
        if (stats != null) File.WriteAllText(Path.Combine(folder, StatsData.FileName), stats);
    }

    static List<ModManifest> Session(string mods) => ModCatalog.LoadOrder(ModCatalog.Active(ModCatalog.Load(mods).Mods));

    public static void Register(Action<string, Action> test, Action<bool, string> check, Action<Action> reject, string root)
    {
        test("Live mod changes: stats mods plan live with the loader's lines; code, data, own records and a new order wait", () =>
        {
            var mods = Path.Combine(root, "live-mods");
            Mod(mods, "s1", stats: Rpm);
            Mod(mods, "s2", stats: Rpm.Replace("900", "800"));
            Mod(mods, "c1", ",\"code\":{\"dll\":\"mod.dll\",\"sdk\":1}");
            File.WriteAllText(Path.Combine(mods, "c1", "mod.dll"), "synthetic dll");
            Mod(mods, "v1", ",\"gameplay\":false");
            var (archive, _, _) = ResourceArchive.Write(new Dictionary<ResourceKey, ResourceParts> { [new(ResourceTypes.Texture, 0x1234)] = new([1, 2, 3, 4], [], []) });
            Directory.CreateDirectory(Path.Combine(mods, "v1", "data"));
            File.WriteAllBytes(Path.Combine(mods, "v1", "data", "9ba626afa44a3aa3.patch_0"), archive);
            var started = Session(mods);
            var current = SessionMods.Of(started, started, "");
            check(LiveModPlan.Make(current, Session(mods)).Empty, "An unchanged session planned a change.");

            // s1's stats changed: replaced live; its modstat fields are the boot plan's stat fields
            File.WriteAllText(Path.Combine(mods, "s1", StatsData.FileName), Rpm.Replace("900", "1000"));
            var plan = LiveModPlan.Make(current, Session(mods));
            check(plan.Live && plan.Changes.Single() is { Id: "s1", Action: LiveModAction.Replace }, "A changed stats mod did not plan a live replace.");
            var lines = plan.Lines("t1", sync: false, "digest").ToList();
            var s1 = plan.Target.Single(m => m.Id == "s1");
            var bootStat = "stat\t" + string.Join('\t', ModLoaderPlan.StatFields(s1, StatsData.Edits(s1).Single()));
            check(ModLoaderPlan.Text(plan.Target, root, EntityProfile.Current, "25480438").Contains(bootStat + "\n"), "The boot plan's stat line differs.");
            check(lines[0].Verb == "modbegin" && lines[0].Fields.SequenceEqual(["t1", "solo", "digest"]) &&
                lines.Where(l => l.Verb == "modorder").Select(l => l.Fields[2]).SequenceEqual(plan.Target.Where(m => m.NeedsLoader).Select(m => m.Id)) &&
                lines.Any(l => l.Verb == "modreplace" && l.Fields[1] == "s1" && l.Fields[2] == "stats" && l.Fields[5] == "-") &&
                lines.Single(l => l.Verb == "modstat").Fields.Skip(1).SequenceEqual(bootStat.Split('\t').Skip(1)) && lines[^1].Verb == "modcommit",
                "The loader's lines are wrong: " + string.Join(" | ", lines.Select(l => l.Verb + " " + string.Join(',', l.Fields))));
            File.WriteAllText(Path.Combine(mods, "s1", StatsData.FileName), Rpm);

            // s2 off and a new stats mod on: removed and added live
            ModSettings.SetEnabled(mods, "s2", false);
            Mod(mods, "s3", stats: Rpm.Replace("900", "700"));
            plan = LiveModPlan.Make(current, Session(mods));
            check(plan.Live && plan.Changes.Any(c => c is { Id: "s2", Action: LiveModAction.Remove }) && plan.Changes.Any(c => c is { Id: "s3", Action: LiveModAction.Add }),
                "Off and new stats mods did not plan live.");
            var verbs = plan.Lines("t2", sync: true, "digest").ToList();
            check(verbs.Any(l => l.Verb == "modremove" && l.Fields[1] == "s2") && verbs.Any(l => l.Verb == "modadd" && l.Fields[1] == "s3") && verbs[0].Fields[1] == "sync",
                "Remove and add lines are wrong.");
            ModSettings.SetEnabled(mods, "s2", true);
            Directory.Delete(Path.Combine(mods, "s3"), true);

            // code, a data patch and own records wait for the next session; so does a new order
            ModSettings.SetEnabled(mods, "c1", false);
            plan = LiveModPlan.Make(current, Session(mods));
            check(!plan.Live && plan.Blocking.Any(b => b.StartsWith("c1", StringComparison.Ordinal) && b.Contains("code")), "A code mod switched off planned live.");
            ModSettings.SetEnabled(mods, "c1", true);
            File.WriteAllBytes(Path.Combine(mods, "v1", "data", "9ba626afa44a3aa3.patch_0"), [.. archive, 0]);
            plan = LiveModPlan.Make(current, Session(mods));
            check(!plan.Live && plan.Blocking.Any(b => b.Contains("patches game data")), "A changed data patch planned live.");
            File.WriteAllBytes(Path.Combine(mods, "v1", "data", "9ba626afa44a3aa3.patch_0"), archive);
            Mod(mods, "o1", stats: """{"format":1,"weapons":{"AR-23 Liberator":{"own":true,"rpm":950}}}""");
            plan = LiveModPlan.Make(current, Session(mods));
            check(!plan.Live && plan.Blocking.Any(b => b.StartsWith("o1", StringComparison.Ordinal) && b.Contains("own")), "Own records planned live: " + string.Join(" | ", plan.Blocking));
            Directory.Delete(Path.Combine(mods, "o1"), true);
            var order = ModCatalog.Load(mods).Mods.Select(m => m.Id).ToList();
            ModSettings.SetOrder(mods, order.AsEnumerable().Reverse());
            plan = LiveModPlan.Make(current, Session(mods));
            check(!plan.Live && plan.Blocking.Contains("The mods' order changed"), "A new order planned live.");
            ModSettings.SetOrder(mods, order);
            check(LiveModPlan.Make(current, Session(mods)).Empty, "Restoring everything left a change.");

            // a mod that did not load at the start (a source mod that did not build) is not new while it is unchanged
            var loaded = started.Where(m => m.Id != "c1").ToList();
            var withoutCode = SessionMods.Of(started, loaded, "");
            check(LiveModPlan.Make(withoutCode, Session(mods)).Empty && withoutCode.Skipped.Single().Id == "c1", "A mod that did not load counted as added.");
            File.WriteAllText(Path.Combine(mods, "c1", "mod.dll"), "another synthetic dll");
            check(!LiveModPlan.Make(withoutCode, Session(mods)).Live, "A changed mod that did not load planned live.");

            // the session's mods file round-trips
            var file = Path.Combine(root, "live-mods-session.json");
            withoutCode.Write(file);
            var read = SessionMods.Read(file);
            check(read.Mods.SequenceEqual(withoutCode.Mods) && read.Skipped.SequenceEqual(withoutCode.Skipped) && read.SyncedWith == "", "The session's mods did not round-trip.");
            File.WriteAllText(file, File.ReadAllText(file).Replace(read.Mods[0].Hash, "nothex"));
            reject(() => SessionMods.Read(file));
        });
    }
}

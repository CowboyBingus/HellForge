using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using HellForge.Core;
using HellForge.Core.GameData;

// Mod sync: the mod set and content hashes (the digest unchanged), the plan, the synced store's checks and the session
// after a sync. Every mod here is synthetic.
static class ModSyncTests
{
    const string Ammo = """
        {"format":1,"id":"test.ammo","name":"Ammo","version":"1.0.0","native":{
          "networkFieldTypes":[{"name":"deposit_value","expect":{"kind":1,"bits":10,"min":0},"bits":14}]}}
        """;

    // The modlist digest as ModCatalog.Digest computed it before mod sync (HellForge 0.1.8), kept to prove it unchanged.
    static string DigestBefore(IEnumerable<ModManifest> mods)
    {
        var lines = new StringBuilder();
        foreach (var mod in mods.Where(m => m.Gameplay && m.Id != ModCatalog.LocalStatsId).OrderBy(m => m.Id, StringComparer.Ordinal))
        {
            lines.Append(mod.Id).Append('|').Append(mod.Version).Append('\n');
            if (mod.HasDataSpec) lines.Append("data-builder|").Append(ModDataBuilder.Version).Append('\n');
            var shipped = BundledMods.ShippedFiles(mod.Folder);
            foreach (var file in Directory.GetFiles(mod.Folder, "*", SearchOption.AllDirectories)
                .Select(f => Path.GetRelativePath(mod.Folder, f).Replace('\\', '/'))
                .Where(f => !f.Split('/').Any(p => p.StartsWith('.')) && f.Split('/')[0] != "build" && !f.EndsWith(".user", StringComparison.OrdinalIgnoreCase))
                .Where(f => shipped == null || shipped.Contains(f))
                .Where(f => !mod.HasSource || !(f == mod.CodeDll || f.EndsWith(".lib", StringComparison.OrdinalIgnoreCase) || f.EndsWith(".pdb", StringComparison.OrdinalIgnoreCase)))
                .Order(StringComparer.Ordinal))
                lines.Append(file).Append('|').Append(Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(Path.Combine(mod.Folder, file))))).Append('\n');
        }
        return lines.Length == 0 ? "none" : Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(lines.ToString())))[..32].ToLowerInvariant();
    }

    static void Write(string folder, string relative, string text)
    {
        var path = Path.Combine(folder, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, text);
    }

    static void Patch(string path, ulong type)
    {
        var (archive, _, _) = ResourceArchive.Write(new Dictionary<ResourceKey, ResourceParts> { [new(type, 0x1234)] = new([1, 2, 3, 4], [], []) });
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, archive);
    }

    static string Manifest(string id, string version, string extra = "") =>
        $$"""{"format":1,"id":"{{id}}","name":"{{id}} mod","version":"{{version}}"{{extra}}}""";

    // A mod folder of each kind: network field types (content), a data patch (raw), a prebuilt DLL (code), a source mod
    // with an IDE's leftovers (code), a cosmetic one, and a data patch holding Lua beside network field types (code).
    static string Mods(string root, string name)
    {
        var mods = Path.Combine(root, name);
        Write(Path.Combine(mods, "ammo"), ModCatalog.ManifestName, Ammo);
        Write(Path.Combine(mods, "texture"), ModCatalog.ManifestName, Manifest("test.texture", "2"));
        Patch(Path.Combine(mods, "texture", "data", "9ba626afa44a3aa3.patch_0"), ResourceTypes.Texture);
        Write(Path.Combine(mods, "prebuilt"), ModCatalog.ManifestName, Manifest("test.prebuilt", "1", ",\"code\":{\"dll\":\"mod.dll\",\"sdk\":1}"));
        Write(Path.Combine(mods, "prebuilt"), "mod.dll", "synthetic dll");
        Write(Path.Combine(mods, "source"), ModCatalog.ManifestName, Manifest("test.source", "1", ",\"code\":{\"dll\":\"mod.dll\",\"sdk\":1}"));
        Write(Path.Combine(mods, "source"), "src/mod.c", "int x;");
        Write(Path.Combine(mods, "source"), "mod.dll", "an IDE's build");
        Write(Path.Combine(mods, "source"), "mod.pdb", "symbols");
        Write(Path.Combine(mods, "source"), ".idea/workspace.xml", "<x/>");
        Write(Path.Combine(mods, "source"), "build/out.o", "object");
        Write(Path.Combine(mods, "visual"), ModCatalog.ManifestName, Manifest("test.visual", "1", ",\"gameplay\":false"));
        Patch(Path.Combine(mods, "visual", "data", "9ba626afa44a3aa3.patch_0"), ResourceTypes.Texture);
        Write(Path.Combine(mods, "scripted"), ModCatalog.ManifestName, Manifest("test.scripted", "1", ",\"gameplay\":true,\"native\":{\"networkFieldTypes\":[{\"name\":\"remaining\",\"expect\":{\"kind\":1,\"bits\":8,\"min\":0},\"bits\":9}]}"));
        Patch(Path.Combine(mods, "scripted", "data", "9ba626afa44a3aa3.patch_0"), ResourceTypes.Lua);
        return mods;
    }

    // Copies a mod's counted files, as a download would write them.
    static void CopyFiles(ModSetEntry mod, string to)
    {
        foreach (var file in mod.Files)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(to, file.Path))!);
            File.Copy(Path.Combine(mod.Folder!, file.Path), Path.Combine(to, file.Path));
        }
    }

    public static void Register(Action<string, Action> test, Action<bool, string> check, Action<Action> reject, string root)
    {
        test("Mod sync: the mod set's digest is the modlist digest as before, and each mod's content hash is its own lines", () =>
        {
            var (loaded, problems) = ModCatalog.Load(Mods(root, "sync-set"));
            check(loaded.Count == 6, "Fixture mods load: " + string.Join(" | ", problems));
            var set = ModSet.Of(ModCatalog.LoadOrder(loaded));
            check(set.Digest == DigestBefore(loaded) && ModCatalog.Digest(loaded) == set.Digest, "The digest changed with mod sync.");
            foreach (var subset in new IEnumerable<ModManifest>[] { loaded.Take(2), loaded.Skip(3), loaded.Where(m => !m.Gameplay), [] })
                check(ModCatalog.Digest(subset) == DigestBefore(subset), "A subset's digest changed.");
            check(set.Mods.Select(m => m.Id).SequenceEqual(["test.ammo", "test.prebuilt", "test.scripted", "test.source", "test.texture"]), "Gameplay mods by id, cosmetic left out.");
            ModSetEntry M(string id) => set.Mods.Single(m => m.Id == id);
            check(M("test.ammo").Class == ModClass.Content && M("test.texture").Class == ModClass.Raw && M("test.prebuilt").Class == ModClass.Code &&
                M("test.source").Class == ModClass.Code && M("test.scripted").Class == ModClass.Code,
                "Classes: network field types content; a texture patch raw; a DLL, sources and Lua code.");
            check(M("test.source").Files.Select(f => f.Path).SequenceEqual([ModCatalog.ManifestName, "src/mod.c"]), "A source mod's files leave out its build and the IDE's.");
            var ammo = loaded.Single(m => m.Id == "test.ammo");
            check(M("test.ammo").Hash == ModSets.ContentHash(ammo) && M("test.ammo").Hash.Length == 64 && M("test.ammo").Bytes == new FileInfo(Path.Combine(ammo.Folder, ModCatalog.ManifestName)).Length,
                "Content hash and bytes are the mod's own.");
            // the load order is kept as each mod's place, whatever the id order
            var reversed = ModSet.Of(Enumerable.Reverse(ModCatalog.LoadOrder(loaded)).ToList());
            check(reversed.Digest == set.Digest && reversed.Mods.Single(m => m.Id == "test.ammo").Order != set.Mods.Single(m => m.Id == "test.ammo").Order, "Order is the load order's.");
        });

        test("Mod sync: a zipped mod counts by what its zip ships, as the refused join's fingerprints do, and the locator finds it", () =>
        {
            var mods = Path.Combine(root, "sync-zipped");
            var source = Path.Combine(root, "sync-zipped-source");
            Write(source, ModCatalog.ManifestName, Manifest("test.zipped", "3"));
            Patch(Path.Combine(source, "data", "9ba626afa44a3aa3.patch_0"), ResourceTypes.Texture);
            Directory.CreateDirectory(mods);
            System.IO.Compression.ZipFile.CreateFromDirectory(source, Path.Combine(mods, "zipped.zip"));
            var loaded = ModCatalog.Load(mods).Mods;
            var zipped = loaded.Single(m => m.Id == "test.zipped");
            check(zipped.Archive == "zipped.zip", "Loaded from its zip.");
            var set = ModSet.Of(loaded);
            var entry = set.Mods.Single();
            var (digest, fingerprints) = ModCatalog.Modlist(loaded);
            check(set.Digest == digest && fingerprints.Single().Hash == entry.Hash[..16] && set.Fingerprints().Single() == fingerprints.Single(),
                "The mod set's digest and hashes are the refused join's.");
            // a file the mod writes beside itself as it runs is not counted
            File.WriteAllText(Path.Combine(zipped.Folder, "keybinds.json"), "{}");
            check(ModSet.Of(ModCatalog.Load(mods).Mods).Mods.Single().Hash == entry.Hash, "A runtime file does not change the hash.");
            check(new ModLocator(mods).Find("test.zipped", entry.Hash) == zipped.Folder, "The locator finds the installed zip's mod by its hash.");
        });

        test("Mod sync: a mod set goes over the wire without folders or files, and a host's malformed set is refused", () =>
        {
            var (loaded, _) = ModCatalog.Load(Mods(root, "sync-json"));
            var set = ModSet.Of(ModCatalog.LoadOrder(loaded));
            var wire = set.ToJson(local: false).ToJsonString();
            check(!wire.Contains("folder") && !wire.Contains("files") && !wire.Contains(root.Replace("\\", "\\\\")), "The wire form leaves this machine's paths out.");
            var back = ModSet.Parse(wire, local: false);
            check(back.Digest == set.Digest && back.Mods.Count == set.Mods.Count && back.Mods.All(m => m.Folder == null && m.Files.Count == 0) &&
                back.Mods.Zip(set.Mods).All(p => p.First.Hash == p.Second.Hash && p.First.Class == p.Second.Class && p.First.Order == p.Second.Order), "Wire round trip.");
            var file = Path.Combine(root, "sync-json-modset.json");
            set.Write(file);
            var local = ModSet.Parse(File.ReadAllText(file), local: true);
            check(local.Mods.All(m => m.Folder != null && m.Files.Count > 0) && local.Mods.SelectMany(m => m.Files).SequenceEqual(set.Mods.SelectMany(m => m.Files)), "Local round trip keeps files.");
            string With(Action<JsonObject> change) { var node = set.ToJson(local: false); change((JsonObject)node["mods"]![0]!); return node.ToJsonString(); }
            foreach (var bad in new[] {
                         "not json", "{}", "{\"format\":2,\"digest\":\"none\",\"mods\":[]}",
                         With(m => m["id"] = "Bad Id"), With(m => m["hash"] = "ABCD"), With(m => m["hash"] = new string('A', 64)),
                         With(m => m["name"] = "tab\there"), With(m => m["version"] = new string('9', 33)), With(m => m["class"] = "kernel"),
                         With(m => m["bytes"] = -1), With(m => m["order"] = 9999), With(m => m.Remove("name")),
                         With(m => m["id"] = set.Mods[1].Id) })
                reject(() => ModSet.Parse(bad, local: false));
        });

        test("Mod sync: the verified list is embedded and checked; the blocklist and share switch are saved with the Mods page's settings", () =>
        {
            check(VerifiedMods.All.Count == 0 && !VerifiedMods.Contains("test.prebuilt", new string('a', 64)), "The shipped list starts empty.");
            var listed = VerifiedMods.Parse($$"""{"format":1,"mods":[{"id":"a.b","name":"B","version":"1","hash":"{{new string('c', 64)}}","reviewed":"2026-10-22, maintainer"}]}""");
            check(listed.Count == 1 && listed[0].Hash == new string('c', 64), "A listed entry parses.");
            foreach (var bad in new[] { "{\"format\":1}", $$"""{"format":1,"mods":[{"id":"a.b","version":"1","hash":"{{new string('C', 64)}}","reviewed":"x"}]}""",
                         $$"""{"format":1,"mods":[{"id":"a.b","version":"1","hash":"{{new string('c', 64)}}"}]}""" })
                reject(() => VerifiedMods.Parse(bad));

            var mods = Path.Combine(root, "sync-settings");
            ModSettings.SetEnabled(mods, "test.off", false);
            ModSettings.SetOrder(mods, ["test.b", "test.a"]);
            ModSettings.SetBlocked(mods, "bad.mod", true);
            ModSettings.SetShare(mods, false);
            check(!ModSettings.Read(mods).ShowEditedStats, "Edited stats lobbies are hidden by default.");
            ModSettings.SetShowEditedStats(mods, true);
            var state = ModSettings.Read(mods);
            check(state.Blocked.SetEquals(["bad.mod"]) && !state.Share && state.ShowEditedStats && state.Disabled.SetEquals(["test.off"]) && state.Order.SequenceEqual(["test.b", "test.a"]),
                "Blocklist, share, edited stats and the existing settings all kept.");
            ModSettings.SetBlocked(mods, "bad.mod", false);
            ModSettings.SetShare(mods, true);
            state = ModSettings.Read(mods);
            check(state.Blocked.Count == 0 && state.Share && state.Disabled.Count == 1, "Unblocked and sharing again.");
        });

        test("Mod sync: the plan finds what the joiner has, downloads content and verified code, and refuses blocked, unverified and other HellForge's mods", () =>
        {
            var (hostMods, _) = ModCatalog.Load(Mods(root, "sync-plan-host"));
            var host = ModSet.Of(ModCatalog.LoadOrder(hostMods));
            // The joiner: test.ammo the same (on), test.texture the same but switched off, test.prebuilt another version, and a
            // mod of its own the host lacks; nothing else.
            var joinerFolder = Path.Combine(root, "sync-plan-joiner");
            Write(Path.Combine(joinerFolder, "ammo"), ModCatalog.ManifestName, Ammo);
            Write(Path.Combine(joinerFolder, "texture"), ModCatalog.ManifestName, Manifest("test.texture", "2"));
            Patch(Path.Combine(joinerFolder, "texture", "data", "9ba626afa44a3aa3.patch_0"), ResourceTypes.Texture);
            Write(Path.Combine(joinerFolder, "prebuilt"), ModCatalog.ManifestName, Manifest("test.prebuilt", "0.9", ",\"code\":{\"dll\":\"mod.dll\",\"sdk\":1}"));
            Write(Path.Combine(joinerFolder, "prebuilt"), "mod.dll", "older dll");
            Write(Path.Combine(joinerFolder, "mine"), ModCatalog.ManifestName, Manifest("test.mine", "1", ",\"native\":{\"networkFieldTypes\":[{\"name\":\"remaining\",\"expect\":{\"kind\":1,\"bits\":8,\"min\":0},\"bits\":9}]}"));
            ModSettings.SetEnabled(joinerFolder, "test.texture", false);
            var (joinerMods, joinerProblems) = ModCatalog.Load(joinerFolder);
            check(joinerProblems.Count == 0, "Joiner mods load: " + string.Join(" | ", joinerProblems));
            var own = ModSet.Of(ModCatalog.LoadOrder(ModCatalog.Active(joinerMods)));
            var locator = new ModLocator(joinerFolder);
            var none = new HashSet<string>();
            var plan = ModSyncPlan.Make(host, hostShares: true, own, locator.Find, none, verified: (_, _) => false);
            SyncAction A(ModSyncPlan p, string id) => p.Items.Single(i => i.Mod.Id == id).Action;
            check(A(plan, "test.ammo") == SyncAction.Have && A(plan, "test.texture") == SyncAction.Have && A(plan, "test.prebuilt") == SyncAction.Manual &&
                A(plan, "test.source") == SyncAction.Manual && A(plan, "test.scripted") == SyncAction.Manual && A(plan, "test.mine") == SyncAction.Off,
                "Actions: " + string.Join(", ", plan.Items.Select(i => $"{i.Mod.Id}={i.Action}")));
            check(plan.Items.Single(i => i.Mod.Id == "test.texture").Folder == Path.Combine(joinerFolder, "texture") && plan.Verdict == SyncVerdict.Unjoinable,
                "A switched-off copy counts, and unverified code makes the lobby unjoinable.");
            var verified = ModSyncPlan.Make(host, true, own, locator.Find, none, verified: (id, hash) => host.Find(id, hash)?.Class == ModClass.Code);
            check(A(verified, "test.prebuilt") == SyncAction.Download && verified.Verdict == SyncVerdict.Download &&
                verified.DownloadBytes == host.Mods.Where(m => m.Class == ModClass.Code).Sum(m => m.Bytes), "Verified code downloads.");
            var notShared = ModSyncPlan.Make(host, false, own, locator.Find, none, verified: (_, _) => true);
            check(A(notShared, "test.prebuilt") == SyncAction.Manual && A(notShared, "test.ammo") == SyncAction.Have, "A host that does not share: nothing downloads.");
            var blocked = ModSyncPlan.Make(host, true, own, locator.Find, new HashSet<string> { "test.ammo" }, verified: (_, _) => true);
            check(A(blocked, "test.ammo") == SyncAction.Blocked && blocked.Verdict == SyncVerdict.Blocked, "Blocked wins, even over a mod the joiner has.");
            var ready = ModSyncPlan.Make(own, true, own, locator.Find, none);
            check(ready.Verdict == SyncVerdict.Ready && ready.Items.All(i => i.Action == SyncAction.Have), "The same set: nothing to do.");
            var bundled = new ModSet("none", [host.Mods[0] with { Class = ModClass.Bundled, Hash = new string('b', 64) }]);
            check(A(ModSyncPlan.Make(bundled, true, own, locator.Find, none), host.Mods[0].Id) == SyncAction.Update, "A different bundled mod is another HellForge.");
        });

        test("Mod sync: only what HellForge checks itself downloads unverified: data specs, schema stats, network field types; raw data needs the verified list, from friends too", () =>
        {
            var folder = Path.Combine(root, "sync-classes");
            Write(Path.Combine(folder, "spec"), ModCatalog.ManifestName, Manifest("test.spec", "1"));
            Write(Path.Combine(folder, "spec"), DataSpecs.FileName,
                """{"format":1,"patches":[{"archives":["1111111111111111"],"resources":[{"type":"unit","copy":"0x3333","name":"test/mod/unit"}]}]}""");
            Write(Path.Combine(folder, "stats"), ModCatalog.ManifestName, Manifest("test.stats", "1"));
            Write(Path.Combine(folder, "stats"), StatsData.FileName, """{"format":1,"weapons":{"AR-23 Liberator":{"rpm":900}}}""");
            Write(Path.Combine(folder, "ammo"), ModCatalog.ManifestName, Ammo);
            Write(Path.Combine(folder, "rawstats"), ModCatalog.ManifestName, Manifest("test.rawstats", "1"));
            Write(Path.Combine(folder, "rawstats"), StatsData.FileName,
                """{"format":1,"raw":[{"component":"DepositComponentData","resource":"0x1","offset":0,"type":"u32","value":10,"expect":5}]}""");
            Write(Path.Combine(folder, "writes"), ModCatalog.ManifestName, Manifest("test.writes", "1",
                ",\"native\":{\"componentWrites\":[{\"component\":\"DepositComponentData\",\"resource\":\"0x056DE1C5E21E723E\",\"fields\":[{\"name\":\"capacity\",\"offset\":0,\"expect\":1000,\"value\":10000}]}]}"));
            Write(Path.Combine(folder, "patch"), ModCatalog.ManifestName, Manifest("test.patch", "1"));
            Patch(Path.Combine(folder, "patch", "data", "9ba626afa44a3aa3.patch_0"), ResourceTypes.Texture);
            Write(Path.Combine(folder, "dll"), ModCatalog.ManifestName, Manifest("test.dll", "1", ",\"code\":{\"dll\":\"mod.dll\",\"sdk\":1}"));
            Write(Path.Combine(folder, "dll"), "mod.dll", "synthetic dll");
            var (loaded, problems) = ModCatalog.Load(folder);
            check(loaded.Count == 7, "Fixture mods load: " + string.Join(" | ", problems));
            var set = ModSet.Of(ModCatalog.LoadOrder(loaded));
            var expected = new Dictionary<string, ModClass>
            {
                ["test.spec"] = ModClass.Content, ["test.stats"] = ModClass.Content, ["test.ammo"] = ModClass.Content,
                ["test.rawstats"] = ModClass.Raw, ["test.writes"] = ModClass.Raw, ["test.patch"] = ModClass.Raw, ["test.dll"] = ModClass.Code
            };
            foreach (var (id, kind) in expected) check(set.Find(id, set.Mods.Single(m => m.Id == id).Hash)!.Class == kind, $"{id} is {kind}.");
            check(ModSet.Parse(set.ToJson(local: false).ToJsonString(), local: false).Mods.Single(m => m.Id == "test.patch").Class == ModClass.Raw, "The raw class goes over the wire.");

            // The plan: unverified, content downloads and the rest is the player's to install; verified, all of it downloads.
            // One rule for every host (no friends' exception: the plan is not told who the host is).
            var none = new ModSet("none", []);
            var plan = ModSyncPlan.Make(set, true, none, (_, _) => null, new HashSet<string>(), verified: (_, _) => false);
            foreach (var (id, kind) in expected)
                check(plan.Items.Single(i => i.Mod.Id == id).Action == (kind == ModClass.Content ? SyncAction.Download : SyncAction.Manual), $"{id}: unverified plan.");
            var verifiedPlan = ModSyncPlan.Make(set, true, none, (_, _) => null, new HashSet<string>(), verified: (id, hash) => set.Find(id, hash) != null);
            check(verifiedPlan.Items.All(i => i.Action == SyncAction.Download), "Verified raw data and code download.");

            // Before the bytes: the file list, the manifest and the stats file show code and raw data.
            foreach (var mod in set.Mods)
            {
                var manifest = File.ReadAllText(Path.Combine(mod.Folder!, ModCatalog.ManifestName));
                var statsFile = Path.Combine(mod.Folder!, StatsData.FileName);
                var declared = ModSets.Declared(mod.Files.Select(f => f.Path), manifest, File.Exists(statsFile) ? File.ReadAllText(statsFile) : null);
                check(declared == (expected[mod.Id] == ModClass.Content ? null : expected[mod.Id]), $"{mod.Id}: declared {declared?.ToString() ?? "nothing"}.");
            }

            // The store: content goes in unverified; raw data only when verified, whatever the host calls it.
            var install = new InstallLayout(Path.Combine(root, "sync-classes-install"));
            install.Create(SupportedBuild.Current.BuildId);
            Directory.CreateDirectory(install.Mods);
            string Fresh(ModSetEntry mod) { var p = SyncedStore.Partial(install.Mods, mod.Id, mod.Hash); CopyFiles(mod, p); return p; }
            foreach (var mod in set.Mods.Where(m => expected[m.Id] == ModClass.Content))
                check(SyncedStore.Install(install.Mods, mod, Fresh(mod), verified: (_, _) => false) == SyncedStore.Find(install.Mods, mod.Id, mod.Hash), mod.Id + " is stored unverified.");
            foreach (var mod in set.Mods.Where(m => expected[m.Id] == ModClass.Raw))
            {
                reject(() => SyncedStore.Install(install.Mods, mod with { Class = ModClass.Content }, Fresh(mod), verified: (_, _) => false));
                check(SyncedStore.Find(install.Mods, mod.Id, mod.Hash) == null, mod.Id + " is refused unverified.");
                check(SyncedStore.Install(install.Mods, mod, Fresh(mod), verified: (id, hash) => id == mod.Id && hash == mod.Hash) == SyncedStore.Find(install.Mods, mod.Id, mod.Hash),
                    mod.Id + " is stored verified.");
            }
        });

        test("Mod sync: a data spec is content only when HellForge checks all it builds; a hostile host's replace, lua, or an edit on another type needs the verified list", () =>
        {
            var folder = Path.Combine(root, "sync-specs");
            var specs = new Dictionary<string, (string Resources, ModClass Class)>
            {
                ["test.icon"] = ("""{"icon":"icons/a.png","name":"test/mod/icon"}""", ModClass.Content),
                ["test.colours"] = ("""{"type":"material","copy":"0x3333","name":"test/mod/m","colours":{"0x11111111":[1,0,0]}}""", ModClass.Content),
                ["test.curves"] = ("""{"type":"particles","copy":"0x3333","name":"test/mod/p","curves":{"preset":"warm-to-blue"}}""", ModClass.Content),
                ["test.sounds"] = ("""{"type":"wwise_bank","include":["0x4444"]}""", ModClass.Content),
                // the review's case: 8 bytes of the host's choosing written into a copied resource
                ["test.replace"] = ("""{"type":"material","copy":"0x3333","name":"test/mod/m","replace":[{"from":"0x2222","to":"0x4141414141414141","at":["0x8C"]}]}""", ModClass.Raw),
                ["test.luacopy"] = ("""{"type":"lua","copy":"0x5555","name":"test/mod/script"}""", ModClass.Raw),
                ["test.luainclude"] = ("""{"type":"lua","include":["0x5555"]}""", ModClass.Raw),
                ["test.luahash"] = ($$"""{"type":"0x{{ResourceTypes.Lua:X16}}","include":["0x5555"]}""", ModClass.Raw),
                ["test.unknown"] = ("""{"type":"0x1234","copy":"0x5555","name":"test/mod/x"}""", ModClass.Raw),
                ["test.misedit"] = ("""{"type":"unit","copy":"0x3333","name":"test/mod/u","colours":{"0x11111111":[1,0,0]}}""", ModClass.Raw),
                ["test.soundcopy"] = ("""{"type":"wwise_bank","copy":"0x4444","name":"test/mod/bank"}""", ModClass.Raw),
            };
            foreach (var (id, (resources, _)) in specs)
            {
                Write(Path.Combine(folder, id), ModCatalog.ManifestName, Manifest(id, "1"));
                Write(Path.Combine(folder, id), DataSpecs.FileName, $$"""{"format":1,"patches":[{"archives":["1111111111111111"],"resources":[{{resources}}]}]}""");
            }
            Write(Path.Combine(folder, "test.icon"), "icons/a.png", "a picture");
            var (loaded, problems) = ModCatalog.Load(folder);
            check(loaded.Count == specs.Count, "Fixture specs load: " + string.Join(" | ", problems));
            var set = ModSet.Of(ModCatalog.LoadOrder(loaded));
            foreach (var mod in set.Mods)
            {
                check(mod.Class == specs[mod.Id].Class, $"{mod.Id} is {mod.Class}, not {specs[mod.Id].Class}.");
                var declared = ModSets.Declared(mod.Files.Select(f => f.Path), File.ReadAllText(Path.Combine(mod.Folder!, ModCatalog.ManifestName)), null,
                    File.ReadAllText(Path.Combine(mod.Folder!, DataSpecs.FileName)));
                check(declared == (mod.Class == ModClass.Raw ? ModClass.Raw : null), $"{mod.Id}: its spec declares {declared?.ToString() ?? "nothing"} before its other files.");
            }
            var plan = ModSyncPlan.Make(set, true, new ModSet("none", []), (_, _) => null, new HashSet<string>(), verified: (_, _) => false);
            check(plan.Items.All(i => i.Action == (specs[i.Mod.Id].Class == ModClass.Content ? SyncAction.Download : SyncAction.Manual)), "Unverified, only checked specs download.");

            // A hostile host calls its specs content: the joiner's store reads them from their own bytes and refuses them.
            var install = new InstallLayout(Path.Combine(root, "sync-specs-install"));
            install.Create(SupportedBuild.Current.BuildId);
            Directory.CreateDirectory(install.Mods);
            foreach (var mod in set.Mods)
            {
                var partial = SyncedStore.Partial(install.Mods, mod.Id, mod.Hash);
                CopyFiles(mod, partial);
                if (mod.Class == ModClass.Content)
                    check(SyncedStore.Install(install.Mods, mod, partial, verified: (_, _) => false) == SyncedStore.Find(install.Mods, mod.Id, mod.Hash), mod.Id + " is stored unverified.");
                else
                {
                    reject(() => SyncedStore.Install(install.Mods, mod with { Class = ModClass.Content }, partial, verified: (_, _) => false));
                    check(!Directory.Exists(partial) && SyncedStore.Find(install.Mods, mod.Id, mod.Hash) == null, mod.Id + ", called content, is refused and deleted.");
                }
            }
        });

        test("Mod sync: a host's manifest of the wrong shape is a bad mod, not a crash, and its download is deleted", () =>
        {
            var folder = Path.Combine(root, "sync-shapes");
            var install = new InstallLayout(Path.Combine(root, "sync-shapes-install"));
            install.Create(SupportedBuild.Current.BuildId);
            Directory.CreateDirectory(install.Mods);
            var shapes = new Dictionary<string, string>
            {
                ["native-number"] = ",\"native\":1",
                ["native-list"] = ",\"native\":[]",
                ["types-object"] = ",\"native\":{\"networkFieldTypes\":{\"a\":1}}",
                ["types-numbers"] = ",\"native\":{\"networkFieldTypes\":[1,2]}",
                ["no-expect"] = ",\"native\":{\"networkFieldTypes\":[{\"name\":\"deposit_value\",\"bits\":14}]}",
                ["expect-number"] = ",\"native\":{\"networkFieldTypes\":[{\"name\":\"deposit_value\",\"expect\":3,\"bits\":14}]}",
                ["writes-no-fields"] = ",\"native\":{\"componentWrites\":[{\"component\":\"DepositComponentData\",\"resource\":\"0x056DE1C5E21E723E\"}]}",
                ["writes-fields-text"] = ",\"native\":{\"componentWrites\":[{\"component\":\"DepositComponentData\",\"resource\":\"0x056DE1C5E21E723E\",\"fields\":\"x\"}]}",
                ["code-text"] = ",\"code\":\"mod.dll\"",
                ["dll-number"] = ",\"code\":{\"dll\":1,\"sdk\":1}",
            };
            foreach (var (name, extra) in shapes)
            {
                var id = "test." + name;
                Write(Path.Combine(folder, name), ModCatalog.ManifestName, Manifest(id, "1", extra));
                reject(() => ModCatalog.Parse(Path.Combine(folder, name), EntityProfile.Current));
                var partial = SyncedStore.Partial(install.Mods, id, new string('a', 64));
                Write(partial, ModCatalog.ManifestName, Manifest(id, "1", extra));
                reject(() => SyncedStore.Install(install.Mods, new ModSetEntry(id, id, "1", new string('a', 64), 10, ModClass.Content, 0), partial, verified: (_, _) => false));
                check(!Directory.Exists(partial), name + ": the download is deleted.");
            }
            Directory.CreateDirectory(Path.Combine(folder, "array"));
            File.WriteAllText(Path.Combine(folder, "array", ModCatalog.ManifestName), "[1,2]");
            reject(() => ModCatalog.Parse(Path.Combine(folder, "array"), EntityProfile.Current));
            var (loaded, problems) = ModCatalog.Load(folder);
            check(loaded.Count == 0 && problems.Count == shapes.Count + 1, "The catalogue lists each as a problem: " + string.Join(" | ", problems));
        });

        test("Mod sync: each download has its own folder, a cancelled one is deleted, and a stored mod changed since is not found", () =>
        {
            var (hostMods, _) = ModCatalog.Load(Mods(root, "sync-attempts-host"));
            var host = ModSet.Of(ModCatalog.LoadOrder(hostMods));
            var install = new InstallLayout(Path.Combine(root, "sync-attempts-install"));
            install.Create(SupportedBuild.Current.BuildId);
            Directory.CreateDirectory(install.Mods);
            var ammo = host.Mods.Single(m => m.Id == "test.ammo");
            var first = SyncedStore.Partial(install.Mods, ammo.Id, ammo.Hash);
            var second = SyncedStore.Partial(install.Mods, ammo.Id, ammo.Hash);
            check(first != second && Directory.Exists(first) && Directory.Exists(second), "Two attempts, two folders.");
            CopyFiles(ammo, first);
            using (var cancelled = new CancellationTokenSource())
            {
                cancelled.Cancel();
                try { SyncedStore.Install(install.Mods, ammo, first, cancel: cancelled.Token); check(false, "A cancelled install went on."); }
                catch (OperationCanceledException) { }
            }
            check(!Directory.Exists(first) && SyncedStore.Find(install.Mods, ammo.Id, ammo.Hash) == null, "A cancelled install is deleted, not stored.");
            CopyFiles(ammo, second);
            var stored = SyncedStore.Install(install.Mods, ammo, second);
            var locator = new ModLocator(install.Mods);
            check(SyncedStore.Find(install.Mods, ammo.Id, ammo.Hash) == stored && locator.Find(ammo.Id, ammo.Hash) == stored, "Stored and found.");
            // the files changed after the store checked them; the record still names the hash
            File.WriteAllText(Path.Combine(stored, ModCatalog.ManifestName), Ammo.Replace("\"bits\":14", "\"bits\":15"));
            check(SyncedStore.Find(install.Mods, ammo.Id, ammo.Hash) == null && new ModLocator(install.Mods).Find(ammo.Id, ammo.Hash) == null,
                "A stored mod whose files changed is not the host's any more.");
            SyncedStore.Clean(install.Mods);
            check(!Directory.Exists(Path.Combine(SyncedStore.Root(install.Mods), SyncedStore.PartialFolder)) && Directory.Exists(stored), "Clean empties the downloads' folder only.");
        });

        test("Mod sync: only safe paths a digest would count can be written by a download", () =>
        {
            foreach (var good in new[] { "hellforge-mod.json", "data/9ba626afa44a3aa3.patch_0", "src/a b/c.cpp", "images/icon.png" })
                check(SyncedStore.SafePath(good), "Refused a normal path: " + good);
            foreach (var bad in new[] { "", "../x", "a/../b", "/abs", "a\\b", "C:x", "a:stream", "con", "CON.txt", "data/nul.json", "lpt1", "a./b", "a /b",
                         " a", ".git/config", "src/.hidden", "build/out.o", "x.user", "a//b", "tab\tname", new string('a', 241),
                         "con .txt", "nul .json", "data/aux  .patch_0", "COM1 .x" })
                check(!SyncedStore.SafePath(bad), "Accepted an unsafe path: " + bad);
            var partial = Path.Combine(root, "sync-target");
            check(SyncedStore.Target(partial, "data/x.patch_0") == Path.Combine(Path.GetFullPath(partial), "data", "x.patch_0"), "A safe target lands inside.");
            reject(() => SyncedStore.Target(partial, "../escape"));
        });

        test("Mod sync: the store takes a download only when its own bytes are the host's mod and a class the joiner may take", () =>
        {
            var (hostMods, _) = ModCatalog.Load(Mods(root, "sync-store-host"));
            var host = ModSet.Of(ModCatalog.LoadOrder(hostMods));
            var install = new InstallLayout(Path.Combine(root, "sync-store-install"));
            install.Create(SupportedBuild.Current.BuildId);
            Directory.CreateDirectory(install.Mods);
            var texture = host.Mods.Single(m => m.Id == "test.texture");
            bool TextureVerified(string id, string hash) => id == texture.Id && hash == texture.Hash;
            var partial = SyncedStore.Partial(install.Mods, texture.Id, texture.Hash);
            CopyFiles(texture, partial);
            reject(() => SyncedStore.Install(install.Mods, texture with { Class = ModClass.Content }, partial));
            check(!Directory.Exists(partial) && SyncedStore.Find(install.Mods, texture.Id, texture.Hash) == null, "A raw data patch is refused unverified, whatever the host calls it.");
            CopyFiles(texture, partial);
            var installed = SyncedStore.Install(install.Mods, texture, partial, TextureVerified);
            check(SyncedStore.Find(install.Mods, texture.Id, texture.Hash) == installed && !Directory.Exists(partial), "Installed and found by hash.");
            check(new ModLocator(install.Mods).Find(texture.Id, texture.Hash) == installed, "The locator finds the store's copy.");
            check(ModCatalog.Load(install.Mods).Mods.Count == 0, "Normal sessions never load the store.");
            check(ModSets.ContentHash(ModCatalog.Parse(installed, EntityProfile.Current)) == texture.Hash, "The stored mod rehashes to the host's.");

            string Fresh(ModSetEntry mod) { var p = SyncedStore.Partial(install.Mods, mod.Id, mod.Hash); CopyFiles(mod, p); return p; }
            var ammo = host.Mods.Single(m => m.Id == "test.ammo");
            var tampered = Fresh(ammo);
            File.WriteAllText(Path.Combine(tampered, ModCatalog.ManifestName), Ammo.Replace("\"bits\":14", "\"bits\":15"));
            reject(() => SyncedStore.Install(install.Mods, ammo, tampered));
            var extra = Fresh(ammo);
            Write(extra, ".hidden/payload.dll", "left out of the hash, so never compared");
            reject(() => SyncedStore.Install(install.Mods, ammo, extra));
            reject(() => SyncedStore.Install(install.Mods, ammo with { Id = "test.other" }, Fresh(ammo)));
            reject(() => SyncedStore.Install(install.Mods, ammo with { Version = "9" }, Fresh(ammo)));
            // code: refused unless verified by its exact hash, even when the host calls it content
            foreach (var id in new[] { "test.prebuilt", "test.scripted", "test.source" })
            {
                var code = host.Mods.Single(m => m.Id == id);
                var refused = Fresh(code);
                reject(() => SyncedStore.Install(install.Mods, code with { Class = ModClass.Content }, refused, verified: (_, _) => false));
                check(!Directory.Exists(refused), "A refused download is deleted.");
                reject(() => SyncedStore.Install(install.Mods, code, Fresh(code), verified: (i, h) => i == id && h != code.Hash));
                check(SyncedStore.Find(install.Mods, id, code.Hash) == null, "A refused download is not in the store.");
                check(SyncedStore.Install(install.Mods, code, Fresh(code), verified: (i, h) => i == id && h == code.Hash) == SyncedStore.Find(install.Mods, id, code.Hash),
                    id + ": verified code is taken.");
            }
        });

        test("Mod sync: the next session plays the host's mods in the host's order with the player's cosmetic mods and Local Stats, or the player's own when one is missing", () =>
        {
            var (hostMods, _) = ModCatalog.Load(Mods(root, "sync-session-host"));
            ModSettings.SetOrder(Path.Combine(root, "sync-session-host"), ["test.texture", "test.ammo"]);
            (hostMods, _) = ModCatalog.Load(Path.Combine(root, "sync-session-host"));
            var hostContent = ModCatalog.LoadOrder(hostMods.Where(m => m.Id is "test.ammo" or "test.texture" or "test.visual").ToList());
            var host = ModSet.Of(hostContent);
            var install = new InstallLayout(Path.Combine(root, "sync-session-install"));
            install.Create(SupportedBuild.Current.BuildId);
            // The joiner has test.ammo installed (switched off), a cosmetic mod and Local Stats of its own, and a gameplay mod
            // the host lacks; test.texture comes from the store.
            Write(Path.Combine(install.Mods, "ammo"), ModCatalog.ManifestName, Ammo);
            Write(Path.Combine(install.Mods, "look"), ModCatalog.ManifestName, Manifest("test.look", "1", ",\"gameplay\":false"));
            Patch(Path.Combine(install.Mods, "look", "data", "9ba626afa44a3aa3.patch_0"), ResourceTypes.Texture);
            Write(Path.Combine(install.Mods, "local-stats"), ModCatalog.ManifestName, Manifest(ModCatalog.LocalStatsId, "1"));
            Patch(Path.Combine(install.Mods, "local-stats", "data", "9ba626afa44a3aa3.patch_0"), ResourceTypes.Texture);
            Write(Path.Combine(install.Mods, "mine"), ModCatalog.ManifestName, Manifest("test.mine", "1", ",\"native\":{\"networkFieldTypes\":[{\"name\":\"remaining\",\"expect\":{\"kind\":1,\"bits\":8,\"min\":0},\"bits\":9}]}"));
            ModSettings.SetEnabled(install.Mods, "test.ammo", false);
            var texture = host.Mods.Single(m => m.Id == "test.texture");
            var partial = SyncedStore.Partial(install.Mods, texture.Id, texture.Hash);
            CopyFiles(texture, partial);
            SyncedStore.Install(install.Mods, texture, partial, verified: (id, hash) => id == texture.Id && hash == texture.Hash);

            var file = ModSyncSession.PathIn(install);
            var session = new ModSyncSession("1100001DEADBEEF", "Host", "hellforge1-lobby;x;1100001DEADBEEF", host.Digest,
                [.. host.Mods.Select(m => new ModSyncSession.Mod(m.Id, m.Hash, m.Order))]);
            session.Write(file);
            var read = ModSyncSession.Read(file);
            check(read.Digest == host.Digest && read.Mods.Count == 2 && read.HostName == "Host", "The session file round-trips.");
            var (catalog, _) = ModCatalog.Load(install.Mods);
            var (order, problem) = read.Resolve(install.Mods, catalog);
            if (order == null) throw new Exception("Not resolved: " + problem);
            check(order.Select(m => m.Id).SequenceEqual(["test.look", "test.ammo", "test.texture", ModCatalog.LocalStatsId]) && order.All(m => m.Enabled),
                "Cosmetic first, then the host's in its order (ammo loads before texture there), the player's own gameplay mod left out: " + string.Join(", ", order.Select(m => m.Id)));
            check(ModSet.Of(order).Digest == host.Digest, "The session's digest is the host's.");

            Directory.Delete(SyncedStore.Find(install.Mods, texture.Id, texture.Hash)!, recursive: true);
            var (missing, why) = read.Resolve(install.Mods, ModCatalog.Load(install.Mods).Mods);
            check(missing == null && why!.Contains("test.texture"), "A missing mod falls back: " + why);
            var wrong = read with { Digest = new string('0', 32) };
            CopyFiles(texture, partial = SyncedStore.Partial(install.Mods, texture.Id, texture.Hash));
            SyncedStore.Install(install.Mods, texture, partial, verified: (id, hash) => id == texture.Id && hash == texture.Hash);
            check(wrong.Resolve(install.Mods, ModCatalog.Load(install.Mods).Mods).Problem != null, "Another digest falls back.");
            File.WriteAllText(file, File.ReadAllText(file).Replace(texture.Hash, "nothex"));
            reject(() => ModSyncSession.Read(file));
        });
    }
}

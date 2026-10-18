using System.IO.Compression;
using HellForge.Core;
using HellForge.Core.GameData;

static class CosmeticPacksTests
{
    // A pack in the community's format: an option with sub-options, a plain option, and its folders of patches.
    static string Pack(string root, string name, string guid)
    {
        var folder = Directory.CreateDirectory(Path.Combine(root, name)).FullName;
        File.WriteAllText(Path.Combine(folder, CosmeticPacks.ManifestName), $$"""
            {
              "Version": 1, "Guid": "{{guid}}", "Name": "{{name}} Pack", "Description": "Skins.", "IconPath": ".jpgs/icon.png",
              "Options": [
                { "Name": "Rifle", "Description": "A rifle skin.", "Image": ".jpgs/rifle.jpg",
                  "SubOptions": [ { "Name": "Rifle/A", "Include": ["Rifle A"] }, { "Name": "Rifle/B", "Include": ["Rifle B"] } ], },
                { "Name": "Pistol", "Include": ["Pistol"] }
              ]
            }
            """);
        foreach (var variant in new[] { "Rifle A", "Rifle B", "Pistol" })
        {
            var at = Directory.CreateDirectory(Path.Combine(folder, variant)).FullName;
            foreach (var suffix in new[] { "", ".gpu_resources", ".stream" })
                File.WriteAllText(Path.Combine(at, "9ba626afa44a3aa3.patch_0" + suffix), variant + suffix);
        }
        Directory.CreateDirectory(Path.Combine(folder, ".jpgs"));
        File.WriteAllText(Path.Combine(folder, ".jpgs", "icon.png"), "png");
        return folder;
    }

    public static void Register(Action<string, Action> test, Action<bool, string> check, Action<Action> reject, string root)
    {
        test("Cosmetic packs: options and sub-options choose the folders staged, renumbered after the game's own patches", () =>
        {
            var install = new InstallLayout(Path.Combine(root, "pack-install"));
            install.Create("25480438");
            Directory.CreateDirectory(install.Mods);
            var data = Directory.CreateDirectory(Path.Combine(install.Game, "data")).FullName;
            File.WriteAllText(Path.Combine(data, "9ba626afa44a3aa3.patch_0"), "the player's own"); // a patch HellForge did not stage
            var folder = Pack(install.Mods, "kinetic", "0967dcdf-f9c8-4e3c-b8ac-184d2ba16f2a");

            var (mods, problems) = ModCatalog.Load(install.Mods);
            var pack = mods.Single();
            check(problems.Count == 0 && pack is { Id: "pack.0967dcdf-f9c8-4e3c-b8ac-184d2ba16f2a", Name: "kinetic Pack", Cosmetic: true } &&
                pack.Pack!.Options.Count == 2 && pack.Pack.Icon == Path.Combine(folder, ".jpgs", "icon.png"), "Read as a cosmetic mod: " + string.Join(" | ", problems));
            check(pack.PackFolders.Select(Path.GetFileName).SequenceEqual(["Rifle A", "Pistol"]), "By default: every option, each with its first sub-option.");

            ModSettings.SetChoice(install.Mods, pack.Id, new(new HashSet<string> { "Pistol" }, new Dictionary<string, string> { ["Rifle"] = "Rifle/B" }));
            ModSettings.SetEnabled(install.Mods, "some.other-mod", false);
            pack = ModCatalog.Load(install.Mods).Mods.Single();
            check(pack.PackFolders.Select(Path.GetFileName).SequenceEqual(["Rifle B"]), "The choice is kept: Pistol off, Rifle/B.");
            check(ModSettings.Read(install.Mods).Choices.ContainsKey(pack.Id), "Switching another mod off keeps the pack's choice.");

            var staged = ModStaging.Stage(install, [pack]);
            check(staged.Order().SequenceEqual(["9ba626afa44a3aa3.patch_1", "9ba626afa44a3aa3.patch_1.gpu_resources", "9ba626afa44a3aa3.patch_1.stream"]) &&
                File.ReadAllText(Path.Combine(data, "9ba626afa44a3aa3.patch_1")) == "Rifle B", "Staged after the game's own patch_0: " + string.Join(", ", staged));
            ModStaging.Unstage(install);
            check(File.Exists(Path.Combine(data, "9ba626afa44a3aa3.patch_0")) && !File.Exists(Path.Combine(data, "9ba626afa44a3aa3.patch_1")), "Unstaged; the player's own patch stays.");

            check(ModCatalog.Digest([pack]) == "none", "Friends do not need the same cosmetic packs.");
        });

        test("The Mods page's order: gameplay then cosmetic, saved, new mods on top; sessions load it bottom first", () =>
        {
            var mods = Directory.CreateDirectory(Path.Combine(root, "order-mods")).FullName;
            foreach (var id in new[] { "a.one", "a.two", "a.three" })
            {
                var folder = Directory.CreateDirectory(Path.Combine(mods, id)).FullName;
                File.WriteAllText(Path.Combine(folder, ModCatalog.ManifestName), $$"""{"format":1,"id":"{{id}}","name":"{{id}}","version":"1","gameplay":true}""");
                Directory.CreateDirectory(Path.Combine(folder, "data"));
                File.WriteAllText(Path.Combine(folder, "data", "9ba626afa44a3aa3.patch_0"), id);
            }
            File.WriteAllText(Path.Combine(mods, "a.one", "icon.png"), "png");
            File.WriteAllText(Path.Combine(mods, "a.one", ModCatalog.ManifestName), File.ReadAllText(Path.Combine(mods, "a.one", ModCatalog.ManifestName)).Replace("\"gameplay\"", "\"icon\":\"icon.png\",\"gameplay\""));
            File.WriteAllText(Path.Combine(mods, "a.two", ModCatalog.ManifestName), File.ReadAllText(Path.Combine(mods, "a.two", ModCatalog.ManifestName)).Replace("\"gameplay\"", "\"icon\":\"../a.one/icon.png\",\"gameplay\""));
            Pack(mods, "skins", "11111111-2222-3333-4444-555555555555");
            ModSettings.SetOrder(mods, ["pack.11111111-2222-3333-4444-555555555555", "a.three", "a.one"]);
            var (listed, problems) = ModCatalog.Load(mods);
            check(problems.Count == 0 && listed.Select(m => m.Id).SequenceEqual(["a.two", "a.three", "a.one", "pack.11111111-2222-3333-4444-555555555555"]),
                "Gameplay first (the new a.two on top), then the cosmetic group: " + string.Join(", ", listed.Select(m => m.Id)) + " " + string.Join(" | ", problems));
            check(ModCatalog.LoadOrder(listed).Select(m => m.Id).First() == "pack.11111111-2222-3333-4444-555555555555", "The bottom mod loads first.");
            check(listed.Single(m => m.Id == "a.one").Icon == Path.Combine(mods, "a.one", "icon.png") && listed.Single(m => m.Id == "a.two").Icon == null,
                "A manifest's icon is a file in its own folder, never outside it.");
            var gameplay = listed.Where(m => m.Gameplay).ToList();
            check(ModCatalog.Digest(gameplay) != ModCatalog.Digest(Enumerable.Reverse(gameplay)), "The modlist code follows the gameplay order.");
            check(ModCatalog.Digest(listed) == ModCatalog.Digest(listed.Where(m => m.Gameplay)), "Cosmetic packs are not in it, wherever they sit.");
        });

        test("Lua script mods are refused: a pack, a zip of one, or a HellForge mod's data with a lua resource", () =>
        {
            static void Archive(string path, ulong type)
            {
                var (archive, gpu, _) = ResourceArchive.Write(new Dictionary<ResourceKey, ResourceParts> { [new(type, 0x1234)] = new([1, 2, 3, 4], [], []) });
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                File.WriteAllBytes(path, archive);
                File.WriteAllBytes(path + ".gpu_resources", gpu);
            }
            var install = new InstallLayout(Path.Combine(root, "lua-install"));
            install.Create("25480438");
            var mods = Directory.CreateDirectory(install.Mods).FullName;
            // as the community ships them: manifest.json with no options, the script patch at the root
            var lua = Path.Combine(mods, "frv-swap");
            Directory.CreateDirectory(lua);
            File.WriteAllText(Path.Combine(lua, CosmeticPacks.ManifestName), """{"Version":1,"Guid":"7a41fd46-5db0-4f09-9a96-22a832e5bb62","Name":"FRV Weapon Swap","Options":[]}""");
            Archive(Path.Combine(lua, "9ba626afa44a3aa3.patch_0"), ResourceTypes.Lua);
            var skin = Path.Combine(mods, "skin");
            Directory.CreateDirectory(skin);
            Archive(Path.Combine(skin, "9ba626afa44a3aa3.patch_0"), ResourceTypes.Texture);
            var coded = Path.Combine(mods, "wrapped");
            File.WriteAllText(Path.Combine(Directory.CreateDirectory(coded).FullName, ModCatalog.ManifestName), """{"format":1,"id":"a.wrapped","name":"Wrapped","version":"1"}""");
            Archive(Path.Combine(coded, "data", "9ba626afa44a3aa3.patch_0"), ResourceTypes.Lua);

            var (loaded, problems) = ModCatalog.Load(mods);
            check(loaded.Select(m => m.Id).SequenceEqual(["pack.skin"]), "Only the skin loads: " + string.Join(", ", loaded.Select(m => m.Id)));
            check(problems.Count == 2 && problems.All(p => p.Contains("Lua script mod")) && problems.Any(p => p.StartsWith("frv-swap")) && problems.Any(p => p.StartsWith("wrapped")),
                "Both Lua mods are named as refused: " + string.Join(" | ", problems));

            var zip = Path.Combine(root, "lua-source", "frv.zip");
            Directory.CreateDirectory(Path.GetDirectoryName(zip)!);
            ZipFile.CreateFromDirectory(lua, zip);
            Directory.Delete(lua, recursive: true);
            var (candidates, refused) = ModInstaller.Inspect(install, [zip]);
            check(candidates.Count == 0 && refused.Single().Contains("Lua script mod"), "Add mods refuses its zip: " + string.Join(" | ", refused));
        });

        test("A cosmetic pack zipped loads like a folder, and Add mods takes it", () =>
        {
            var install = new InstallLayout(Path.Combine(root, "pack-zip-install"));
            install.Create("25480438");
            Directory.CreateDirectory(install.Mods);
            var source = Directory.CreateDirectory(Path.Combine(root, "pack-zip-source")).FullName;
            var folder = Pack(source, "zipped", "99999999-2222-3333-4444-555555555555");
            var zip = Path.Combine(source, "zipped.zip");
            ZipFile.CreateFromDirectory(folder, zip, CompressionLevel.Fastest, includeBaseDirectory: true);
            var (candidates, problems) = ModInstaller.Inspect(install, [zip, Path.Combine(folder, CosmeticPacks.ManifestName)]);
            check(problems.Count == 1 && candidates.Single() is { Id: "pack.99999999-2222-3333-4444-555555555555", IsZip: true },
                "The zip is a pack; the same pack again (its folder) is refused as chosen twice: " + string.Join(" | ", problems));
            ModInstaller.Add(install, candidates.Single());
            var (mods, loadProblems) = ModCatalog.Load(install.Mods);
            check(loadProblems.Count == 0 && mods.Single() is { Cosmetic: true, Archive: "zipped.zip" } p && p.PackFolders.Count == 2,
                "Loaded from the zip: " + string.Join(" | ", loadProblems));
        });
    }
}

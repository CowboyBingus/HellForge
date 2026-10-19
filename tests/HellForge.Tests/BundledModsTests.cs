using HellForge.Core;

static class BundledModsTests
{
    public static void Register(Action<string, Action> test, Action<bool, string> check, Action<Action> reject, string root)
    {
        test("Bundled mods (the in-game editor) are put into mods before a session, updated with the app, and keep what the player saved", () =>
        {
            var install = new InstallLayout(Path.Combine(root, "bundled-install"));
            install.Create(SupportedBuild.Current.BuildId);
            var shipped = Directory.CreateDirectory(Path.Combine(BundledMods.Folder(install), BundledMods.EditorFolder)).FullName;
            File.WriteAllText(Path.Combine(shipped, "hellforge-mod.json"), "{\"format\": 1}");
            File.WriteAllText(Path.Combine(shipped, "hellforge_editor.dll"), "version 1");
            var target = Path.Combine(install.Mods, BundledMods.EditorFolder);

            var lines = new List<string>();
            check(BundledMods.Sync(install, lines.Add).Count == 0, "No problems.");
            check(File.ReadAllText(Path.Combine(target, "hellforge_editor.dll")) == "version 1" && lines.Count == 1, "Installed: " + string.Join(" | ", lines));
            var catalog = Path.Combine(target, EditorCatalog.FileName);
            check(File.Exists(catalog) && new FileInfo(catalog).Length > 1000, "The catalogue is made in the installation.");
            check(!File.Exists(Path.Combine(target, HellForge.Core.GameData.EditorIcons.FileName)), "No game copy: no icons, and no problem.");

            File.WriteAllText(Path.Combine(target, "editor-settings.json"), "{\"mine\": true}");
            Directory.CreateDirectory(Path.Combine(target, "presets"));
            File.WriteAllText(Path.Combine(target, "presets", "a.hd2rpreset.json"), "{}");
            lines.Clear();
            var stamp = File.GetLastWriteTimeUtc(catalog);
            BundledMods.Sync(install, lines.Add);
            check(lines.Count == 0 && File.GetLastWriteTimeUtc(catalog) == stamp, "Unchanged: nothing copied or rewritten.");

            File.WriteAllText(Path.Combine(shipped, "hellforge_editor.dll"), "version 2");
            BundledMods.Sync(install, lines.Add);
            check(File.ReadAllText(Path.Combine(target, "hellforge_editor.dll")) == "version 2" && lines.Count == 1, "An update replaces the shipped files.");
            check(File.ReadAllText(Path.Combine(target, "editor-settings.json")) == "{\"mine\": true}" &&
                File.Exists(Path.Combine(target, "presets", "a.hd2rpreset.json")), "The player's settings and presets stay.");

            // A second bundled mod, and a modder's own copy of it with sources: left alone, the others still installed.
            var difficulty = Directory.CreateDirectory(Path.Combine(BundledMods.Folder(install), "hell-forge")).FullName;
            File.WriteAllText(Path.Combine(difficulty, "hellforge-mod.json"), "{\"shipped\": true}");
            var own = Directory.CreateDirectory(Path.Combine(install.Mods, "hell-forge", "src")).Parent!.FullName;
            File.WriteAllText(Path.Combine(own, "hellforge-mod.json"), "{\"mine\": true}");
            lines.Clear();
            check(BundledMods.Sync(install, lines.Add).Count == 0 && File.ReadAllText(Path.Combine(own, "hellforge-mod.json")) == "{\"mine\": true}" &&
                lines.Any(l => l.Contains("sources of its own")), "A copy with its own sources is not overwritten: " + string.Join(" | ", lines));
            Directory.Delete(Path.Combine(own, "src"));
            BundledMods.Sync(install);
            check(File.ReadAllText(Path.Combine(own, "hellforge-mod.json")) == "{\"shipped\": true}", "Without sources it is HellForge's copy again.");

            // Copies of a bundled mod by its id (downloaded before it was bundled): an older modder's copy, a built copy in a
            // folder of another name and a zip are retired; what the player saved in an editor copy comes along.
            var update = new InstallLayout(Path.Combine(root, "bundled-update"));
            update.Create(SupportedBuild.Current.BuildId);
            static string Manifest(string id, string version) =>
                $"{{\"format\": 1, \"id\": \"{id}\", \"name\": \"M\", \"version\": \"{version}\", \"description\": \"d\", \"gameplay\": true}}";
            var plus = Directory.CreateDirectory(Path.Combine(BundledMods.Folder(update), "vanilla-plus")).FullName;
            File.WriteAllText(Path.Combine(plus, "hellforge-mod.json"), Manifest("skyeshade.vanilla-plus", "0.2.0"));
            File.WriteAllText(Path.Combine(plus, "vanilla_plus.dll"), "bundled");
            File.WriteAllText(Path.Combine(plus, "old.txt"), "shipped once");
            var oldSource = Directory.CreateDirectory(Path.Combine(update.Mods, "vanilla-plus", "src")).Parent!.FullName;
            File.WriteAllText(Path.Combine(oldSource, "hellforge-mod.json"), Manifest("skyeshade.vanilla-plus", "0.1.0"));
            var renamed = Directory.CreateDirectory(Path.Combine(update.Mods, "Vanilla Plus 0.1")).FullName;
            File.WriteAllText(Path.Combine(renamed, "hellforge-mod.json"), Manifest("skyeshade.vanilla-plus", "0.1.0"));
            using (var zip = System.IO.Compression.ZipFile.Open(Path.Combine(update.Mods, "vanilla-plus-0.1.0.zip"), System.IO.Compression.ZipArchiveMode.Create))
            using (var writer = new StreamWriter(zip.CreateEntry("vanilla-plus/hellforge-mod.json").Open()))
                writer.Write(Manifest("skyeshade.vanilla-plus", "0.1.0"));
            var editorApp = Directory.CreateDirectory(Path.Combine(BundledMods.Folder(update), BundledMods.EditorFolder)).FullName;
            File.WriteAllText(Path.Combine(editorApp, "hellforge-mod.json"), Manifest("hellforge.editor", "0.4.0"));
            var oldEditor = Directory.CreateDirectory(Path.Combine(update.Mods, "hellforge-editor-0.3", "presets")).Parent!.FullName;
            File.WriteAllText(Path.Combine(oldEditor, "hellforge-mod.json"), Manifest("hellforge.editor", "0.3.0"));
            File.WriteAllText(Path.Combine(oldEditor, "editor-settings.json"), "{\"old\": true}");
            File.WriteAllText(Path.Combine(oldEditor, "presets", "mine.hd2rpreset.json"), "{}");
            lines.Clear();
            check(BundledMods.Sync(update, lines.Add).Count == 0, "Update: no problems: " + string.Join(" | ", lines));
            var retired = Path.Combine(update.Root, BundledMods.RetiredFolder);
            var installedPlus = Path.Combine(update.Mods, "vanilla-plus");
            check(File.ReadAllText(Path.Combine(installedPlus, "vanilla_plus.dll")) == "bundled" && !Directory.Exists(Path.Combine(installedPlus, "src")) &&
                Directory.Exists(Path.Combine(retired, "vanilla-plus-0.1.0", "src")), "An older modder's copy in its folder is retired, the bundled one installed.");
            check(!Directory.Exists(renamed) && Directory.Exists(Path.Combine(retired, "Vanilla Plus 0.1-0.1.0")) &&
                !File.Exists(Path.Combine(update.Mods, "vanilla-plus-0.1.0.zip")) && File.Exists(Path.Combine(retired, "vanilla-plus-0.1.0-0.1.0.zip")),
                "A copy in another folder and a zip of it are retired: " + string.Join(", ", Directory.GetFileSystemEntries(retired).Select(Path.GetFileName)));
            var installedEditor = Path.Combine(update.Mods, BundledMods.EditorFolder);
            check(!Directory.Exists(oldEditor) && File.ReadAllText(Path.Combine(installedEditor, "editor-settings.json")) == "{\"old\": true}" &&
                File.Exists(Path.Combine(installedEditor, "presets", "mine.hd2rpreset.json")), "The retired editor's settings and presets come along.");
            File.Delete(Path.Combine(plus, "old.txt"));
            BundledMods.Sync(update);
            check(!File.Exists(Path.Combine(installedPlus, "old.txt")) && File.Exists(Path.Combine(installedPlus, "vanilla_plus.dll")),
                "A file the new version no longer ships is removed.");
            var working = Directory.CreateDirectory(Path.Combine(update.Mods, "my-plus", "src")).Parent!.FullName;
            File.WriteAllText(Path.Combine(working, "hellforge-mod.json"), Manifest("skyeshade.vanilla-plus", "0.2.0"));
            lines.Clear();
            BundledMods.Sync(update, lines.Add);
            check(Directory.Exists(working) && !Directory.Exists(installedPlus) && lines.Any(l => l.Contains("my-plus") && l.Contains("sources of its own")),
                "A modder's copy of the same version stays, and the bundled one is not added beside it: " + string.Join(" | ", lines));

            // Friends compare a bundled gameplay mod by what HellForge ships, not what each player saved beside it.
            string Digest(string name, string settings, string dll)
            {
                var player = new InstallLayout(Path.Combine(root, name));
                player.Create(SupportedBuild.Current.BuildId);
                var app = Directory.CreateDirectory(Path.Combine(BundledMods.Folder(player), BundledMods.EditorFolder)).FullName;
                File.WriteAllText(Path.Combine(app, "hellforge-mod.json"),
                    "{\"format\": 1, \"id\": \"hellforge.editor\", \"name\": \"Editor\", \"version\": \"1.0.0\", \"description\": \"d\", \"gameplay\": true, \"code\": {\"dll\": \"hellforge_editor.dll\", \"sdk\": 6}}");
                File.WriteAllText(Path.Combine(app, "hellforge_editor.dll"), dll);
                BundledMods.Sync(player);
                File.WriteAllText(Path.Combine(player.Mods, BundledMods.EditorFolder, "editor-settings.json"), settings);
                var (mods, problems) = ModCatalog.Load(player.Mods);
                check(problems.Count == 0 && mods.Single().Gameplay, "The bundled editor loads as a gameplay mod: " + string.Join(" | ", problems));
                return ModCatalog.Digest(mods);
            }
            var first = Digest("digest-a", "{\"a\": 1}", "build 1");
            check(first != "none" && first == Digest("digest-b", "{\"b\": 2}", "build 1"), "Same editor, different saved settings: friends match.");
            check(first != Digest("digest-c", "{\"a\": 1}", "build 2"), "A different editor build does not match.");

            var empty = new InstallLayout(Path.Combine(root, "bundled-none"));
            empty.Create(SupportedBuild.Current.BuildId);
            check(BundledMods.Sync(empty).Count == 0 && !Directory.Exists(Path.Combine(empty.Mods, BundledMods.EditorFolder)), "An app without bundled mods adds none.");
        });
    }
}

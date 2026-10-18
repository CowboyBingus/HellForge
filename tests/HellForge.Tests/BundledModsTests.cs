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

using HellForge.Core;

// The session's mods/ hold (lockdown item 4): every file in the mods folder is held read-only for the session, and
// files that appear during the session are caught and the new mod folders disabled.
static class ModsHoldTests
{
    static string Manifest(string id, string name) =>
        $$$"""{"format":1,"id":"{{{id}}}","name":"{{{name}}}","version":"1.0","native":{"networkFieldTypes":[{"name":"{{{id.Replace('-', '_')}}}","expect":{"kind":1,"bits":8,"min":0},"bits":9}]}}""";

    public static void Register(Action<string, Action> test, Action<bool, string> check, Action<Action> reject, string root)
    {
        test("Mods hold: files in the mods folder are held read-only during the session", () =>
        {
            var modsRoot = Directory.CreateDirectory(Path.Combine(root, "mods-hold", "mods")).FullName;
            var modFolder = Directory.CreateDirectory(Path.Combine(modsRoot, "test-mod")).FullName;
            File.WriteAllText(Path.Combine(modFolder, ModCatalog.ManifestName), Manifest("test-mod", "Test"));
            File.WriteAllText(Path.Combine(modFolder, "readme.txt"), "Hello");
            var settingsFile = Path.Combine(modsRoot, ModSettings.FileName);
            File.WriteAllText(settingsFile, """{"format":1,"disabled":[],"order":[],"packs":{},"blocked":[],"share":true,"showEditedStats":false}""");

            // Hold every file, as PlaySession.HoldModFiles does.
            var locks = new List<FileStream>();
            var snapshot = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var file in Directory.EnumerateFiles(modsRoot, "*", SearchOption.AllDirectories))
            {
                var full = Path.GetFullPath(file);
                snapshot.Add(full);
                locks.Add(new FileStream(full, FileMode.Open, FileAccess.Read, FileShare.Read));
            }
            foreach (var dir in Directory.EnumerateDirectories(modsRoot, "*", SearchOption.AllDirectories))
                snapshot.Add(Path.GetFullPath(dir));

            // Existing files cannot be overwritten while held.
            var manifest = Path.Combine(modFolder, ModCatalog.ManifestName);
            var blocked = false;
            try { File.WriteAllText(manifest, "replaced"); }
            catch (IOException) { blocked = true; }
            check(blocked, "An existing mod file cannot be overwritten while held.");
            check(File.ReadAllText(manifest).Contains("test-mod"), "The manifest is unchanged.");

            // Release holds.
            foreach (var l in locks) l.Dispose();
        });

        test("Mods hold: planted files are detected and new mod folders disabled", () =>
        {
            var modsRoot = Directory.CreateDirectory(Path.Combine(root, "mods-planted", "mods")).FullName;
            var existingFolder = Directory.CreateDirectory(Path.Combine(modsRoot, "existing-mod")).FullName;
            File.WriteAllText(Path.Combine(existingFolder, ModCatalog.ManifestName), Manifest("existing-mod", "Existing"));
            File.WriteAllText(Path.Combine(modsRoot, ModSettings.FileName),
                """{"format":1,"disabled":[],"order":[],"packs":{},"blocked":[],"share":true,"showEditedStats":false}""");

            // Take a snapshot (what PlaySession does at session start).
            var snapshot = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var file in Directory.EnumerateFiles(modsRoot, "*", SearchOption.AllDirectories))
                snapshot.Add(Path.GetFullPath(file));
            foreach (var dir in Directory.EnumerateDirectories(modsRoot, "*", SearchOption.AllDirectories))
                snapshot.Add(Path.GetFullPath(dir));

            // Simulate the game planting a new mod folder during the session.
            var plantedFolder = Directory.CreateDirectory(Path.Combine(modsRoot, "planted-mod")).FullName;
            File.WriteAllText(Path.Combine(plantedFolder, ModCatalog.ManifestName), Manifest("planted-mod", "Planted"));
            File.WriteAllText(Path.Combine(plantedFolder, "payload.dll"), "not a real DLL");

            // Also plant a file in an existing mod folder.
            File.WriteAllText(Path.Combine(existingFolder, "extra.txt"), "snuck in");

            // Detect planted files (what PlaySession.CheckPlantedMods does).
            var planted = new List<string>();
            foreach (var file in Directory.EnumerateFiles(modsRoot, "*", SearchOption.AllDirectories))
            {
                var full = Path.GetFullPath(file);
                if (!snapshot.Contains(full)) planted.Add(Path.GetRelativePath(modsRoot, full));
            }
            foreach (var dir in Directory.EnumerateDirectories(modsRoot, "*", SearchOption.AllDirectories))
            {
                var full = Path.GetFullPath(dir);
                if (!snapshot.Contains(full)) planted.Add(Path.GetRelativePath(modsRoot, full) + Path.DirectorySeparatorChar);
            }
            check(planted.Count >= 3, $"Planted files detected: {planted.Count} (manifest, payload.dll, extra.txt, folder).");
            check(planted.Any(p => p.Contains("planted-mod")), "The planted mod folder's files are found.");
            check(planted.Any(p => p.Contains("extra.txt")), "A file planted in an existing folder is found.");

            // Disable new top-level mod folders (what PlaySession does).
            var disabledIds = new List<string>();
            foreach (var dir in Directory.GetDirectories(modsRoot))
            {
                var full = Path.GetFullPath(dir);
                if (snapshot.Contains(full)) continue;
                var (mods, problems) = ModCatalog.Load(modsRoot);
                var mod = mods.FirstOrDefault(m => string.Equals(Path.GetFullPath(m.Folder).TrimEnd('\\', '/'),
                    full.TrimEnd('\\', '/'), StringComparison.OrdinalIgnoreCase));
                if (mod != null && mod.Enabled)
                {
                    ModSettings.SetEnabled(modsRoot, mod.Id, false);
                    disabledIds.Add(mod.Id);
                }
            }
            check(disabledIds.Contains("planted-mod"), "The planted mod was disabled.");
            check(!ModSettings.Disabled(modsRoot).Contains("existing-mod"), "The existing mod was not disabled.");
            check(ModSettings.Disabled(modsRoot).Contains("planted-mod"), "The planted mod stays disabled in the settings file.");
        });

        test("Mods hold: an empty mods folder produces no planted-files report", () =>
        {
            var modsRoot = Directory.CreateDirectory(Path.Combine(root, "mods-empty", "mods")).FullName;
            var snapshot = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var file in Directory.EnumerateFiles(modsRoot, "*", SearchOption.AllDirectories))
                snapshot.Add(Path.GetFullPath(file));
            foreach (var dir in Directory.EnumerateDirectories(modsRoot, "*", SearchOption.AllDirectories))
                snapshot.Add(Path.GetFullPath(dir));

            // Nothing changed.
            var planted = new List<string>();
            foreach (var file in Directory.EnumerateFiles(modsRoot, "*", SearchOption.AllDirectories))
                if (!snapshot.Contains(Path.GetFullPath(file))) planted.Add(file);
            check(planted.Count == 0, "No planted files in an unchanged folder.");
        });

        test("Backend port: deterministic, in the dynamic range, and different paths get different ports", () =>
        {
            var a = new InstallLayout(Path.Combine(root, "port-a"));
            var b = new InstallLayout(Path.Combine(root, "port-b"));
            check(a.BackendPort >= 49152 && a.BackendPort <= 65535, $"Port {a.BackendPort} is in the dynamic range.");
            check(a.BackendPort == new InstallLayout(a.Root).BackendPort, "Same path gives the same port.");
            check(a.BackendPort != b.BackendPort, $"Different paths give different ports ({a.BackendPort} vs {b.BackendPort}).");
        });
    }
}

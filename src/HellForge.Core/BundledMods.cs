using HellForge.Core.GameData;

namespace HellForge.Core;

// Mods that ship with HellForge, built (the in-game editor, the Hell Forge difficulty): <installation>/app/bundled-mods/
// <folder> (scripts/Publish-HellForge.ps1), copied into <installation>/mods/<folder> when the launcher starts, when its
// setup has copied the game, and before each session, when one is missing or differs, so a new HellForge brings its
// new version. Only the shipped files are replaced: what a mod keeps in
// its folder (the in-game editor's settings, presets and mod-stats.json) stays. A mods/<folder> with sources of its own
// (src/: a modder working on the example's copy) is left as it is. Switching one off on the Mods page keeps it off.
// The in-game editor's catalogue, icons and fonts are made here, the icons and fonts from the installation's own game
// copy: they are the player's game files and are never part of what HellForge ships.
public static class BundledMods
{
    public const string FolderName = "bundled-mods";
    public const string EditorFolder = "hellforge-editor";

    public static string Folder(InstallLayout install) => Path.Combine(install.App, FolderName);

    // The problems (a bundled mod that could not be put in place); the session still runs without it.
    public static List<string> Sync(InstallLayout install, Action<string>? log = null)
    {
        var problems = new List<string>();
        var source = Folder(install);
        if (!Directory.Exists(source)) return problems;
        foreach (var shipped in Directory.GetDirectories(source).Order(StringComparer.OrdinalIgnoreCase))
        {
            var name = Path.GetFileName(shipped);
            var target = Path.Combine(install.Mods, name);
            if (Directory.Exists(Path.Combine(target, "src")))
            {
                log?.Invoke($"Bundled mod {name}: mods/{name} has sources of its own, so HellForge's built copy is not installed over it.");
                continue;
            }
            try
            {
                var changed = false;
                foreach (var file in Directory.GetFiles(shipped, "*", SearchOption.AllDirectories))
                {
                    var destination = Path.Combine(target, Path.GetRelativePath(shipped, file));
                    if (File.Exists(destination) && Same(file, destination)) continue;
                    Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                    File.Copy(file, destination, overwrite: true);
                    changed = true;
                }
                if (changed) log?.Invoke($"Bundled mod {name}: installed from this HellForge version.");
                if (name.Equals(EditorFolder, StringComparison.OrdinalIgnoreCase)) EditorData(install, target, changed, log);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
            {
                problems.Add($"{name} (bundled with HellForge) could not be installed: {error.Message}");
            }
        }
        return problems;
    }

    // The catalogue follows the app (rewritten whenever the editor changed); icons and fonts come from the game copy and
    // are made once, when missing.
    static void EditorData(InstallLayout install, string folder, bool changed, Action<string>? log)
    {
        var catalog = Path.Combine(folder, EditorCatalog.FileName);
        if (changed || !File.Exists(catalog))
        {
            File.WriteAllText(catalog + ".partial", EditorCatalog.Text());
            File.Move(catalog + ".partial", catalog, overwrite: true);
        }
        // Only from a complete copy: the launcher syncs at start, which can be before or during setup's copy.
        var gameData = Path.Combine(install.Game, "data");
        var icons = Path.Combine(folder, EditorIcons.FileName);
        if ((File.Exists(icons) && !changed) || !SupportedBuild.Current.IsComplete(install.Game)) return;
        var (count, missing) = EditorIcons.Write(gameData, icons);
        var fonts = EditorFonts.Write(gameData, folder);
        log?.Invoke($"In-game editor: {count} icons ({missing.Count} missing) and {fonts.Count} fonts from this installation's game copy.");
    }

    // The files HellForge ships for the bundled mod in modFolder (relative, '/'), or null when it is not one (no
    // app/bundled-mods/<folder> in its installation, or a modder's copy with src/). Friends compare these
    // (ModCatalog.Digest), not what each player's copy made or saved there: the editor's settings, presets, icons, fonts.
    public static HashSet<string>? ShippedFiles(string modFolder)
    {
        modFolder = Path.GetFullPath(modFolder);
        if (Directory.Exists(Path.Combine(modFolder, "src")) || Path.GetDirectoryName(Path.GetDirectoryName(modFolder)) is not { } root) return null;
        var shipped = Path.Combine(root, "app", FolderName, Path.GetFileName(modFolder));
        return Directory.Exists(shipped)
            ? Directory.GetFiles(shipped, "*", SearchOption.AllDirectories).Select(f => Path.GetRelativePath(shipped, f).Replace('\\', '/')).ToHashSet(StringComparer.Ordinal)
            : null;
    }

    static bool Same(string a, string b)
    {
        var x = new FileInfo(a);
        var y = new FileInfo(b);
        if (x.Length != y.Length) return false;
        using var first = x.OpenRead();
        using var second = y.OpenRead();
        return System.Security.Cryptography.SHA256.HashData(first).AsSpan().SequenceEqual(System.Security.Cryptography.SHA256.HashData(second));
    }
}

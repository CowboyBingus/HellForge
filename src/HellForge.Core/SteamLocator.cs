using Microsoft.Win32;

namespace HellForge.Core;

public sealed record SteamGame(string ManifestPath, InstallReport Report, bool SupportedBuild);

// Finds HELLDIVERS 2 in the signed-in user's Steam libraries, wherever Steam is installed.
public static class SteamLocator
{
    // Steam records its folder for the current user; the usual location is the fallback.
    public static string? SteamRoot()
    {
        if (OperatingSystem.IsWindows() &&
            Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) is string path && Directory.Exists(path))
            return LocalInstall.FullPath(path);
        var fallback = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Steam");
        return Directory.Exists(fallback) ? fallback : null;
    }

    // The installed game, or null when Steam or the game is not installed. Throws when the game is present but not
    // usable as a source (an update in progress, missing files).
    public static SteamGame? FindGame(string? steamRoot = null)
    {
        steamRoot ??= SteamRoot();
        if (steamRoot == null) return null;
        var manifest = LocalInstall.FindManifests(steamRoot).FirstOrDefault();
        if (manifest == null) return null;
        var report = LocalInstall.Inspect(manifest);
        return new(manifest, report, report.Pin.BuildId == SupportedBuild.Current.BuildId &&
            report.Pin.ExeSha256 == SupportedBuild.Current.ExeSha256 && report.Pin.GameDllSha256 == SupportedBuild.Current.GameDllSha256);
    }
}

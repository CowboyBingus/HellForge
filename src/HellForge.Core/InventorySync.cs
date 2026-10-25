using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace HellForge.Core;

// The launcher's Sync Inventory: copies the player's own HELLDIVERS 2 inventory and progression into a profile's
// account snapshot, which the local backend applies on every start (HellForge.LocalBackend Program.cs).
//
// The capture proxy (app/capture/HellForge.CaptureProxy.exe) runs elevated: it maps the live API host to loopback in the
// hosts file, trusts a per-run CA, terminates TLS for the normal game's login and, with that login's session, reads
// /api/Progression and /api/Progression/inventory. It removes the hosts line and the CA when it exits. Both files land in
// a private sync folder under logs/, and FinalizeCapture moves them into account-snapshot/<profile>/.
public static class InventorySync
{
    public const string SnapshotFolder = "account-snapshot";
    public const string InventoryFile = "real-inventory.json";
    public const string ProgressionFile = "progression.json";
    public const string MetadataFile = "snapshot.json";
    // Written by the proxy once it listens; the launcher opens the game only then.
    public const string ListeningFile = "capture-state.json";
    // The proxy's own line in the hosts file, above its mapping.
    public const string HostsMarker = "# HellForge-CaptureProxy";
    public const string ProxyExe = "HellForge.CaptureProxy.exe";
    public const int TimeoutMinutes = 10;
    const string GameProcess = "helldivers2";

    public static string? FindCaptureProxy(string? installRoot)
    {
        if (installRoot == null) return null;
        var path = Path.Combine(new InstallLayout(installRoot).App, "capture", ProxyExe);
        return File.Exists(path) ? path : null;
    }

    public static string SnapshotDirectory(string installRoot, string profileName) =>
        Path.Combine(installRoot, SnapshotFolder, profileName);

    // profiles/<name> -> account-snapshot/<name>, both under the installation root.
    public static string? SnapshotDirectoryFromProfileDir(string profileDir)
    {
        var full = Path.GetFullPath(profileDir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
        var profileName = Path.GetFileName(full);
        var installRoot = Path.GetDirectoryName(Path.GetDirectoryName(full));
        if (installRoot == null || string.IsNullOrEmpty(profileName)) return null;
        return Path.Combine(installRoot, SnapshotFolder, profileName);
    }

    // The inventory makes a snapshot; progression is applied when it was captured too.
    public static bool HasSnapshot(string snapshotDir) => File.Exists(Path.Combine(snapshotDir, InventoryFile));

    public static SnapshotInfo? ReadMetadata(string snapshotDir)
    {
        var path = Path.Combine(snapshotDir, MetadataFile);
        if (!File.Exists(path)) return null;
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            var root = doc.RootElement;
            DateTime? captured = root.TryGetProperty("capturedUtc", out var utc) && utc.ValueKind == JsonValueKind.String &&
                DateTime.TryParse(utc.GetString(), CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var when) ? when : null;
            var items = root.TryGetProperty("items", out var count) && count.ValueKind == JsonValueKind.Number && count.TryGetInt32(out var n) ? n : 0;
            long? experience = root.TryGetProperty("experience", out var xp) && xp.ValueKind == JsonValueKind.Number && xp.TryGetInt64(out var value) ? value : null;
            return new SnapshotInfo(captured, items, experience);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException)
        {
            return null;
        }
    }

    public static bool IsGameRunning()
    {
        var running = Process.GetProcessesByName(GameProcess);
        foreach (var process in running) process.Dispose();
        return running.Length > 0;
    }

    // A capture that was killed before its cleanup leaves the live API host pointed at loopback, so the normal game
    // cannot reach Arrowhead's servers until the proxy runs again (it removes leftovers first) or with --cleanup.
    public static bool HasLeftoverRedirect()
    {
        try
        {
            var hosts = Path.Combine(Environment.SystemDirectory, "drivers", "etc", "hosts");
            return File.Exists(hosts) && File.ReadLines(hosts).Any(line => line.Contains(HostsMarker, StringComparison.OrdinalIgnoreCase));
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return false;
        }
    }

    // Starts the proxy elevated (one UAC prompt). Blocks until the prompt is answered, so call it off the UI thread.
    // Returns null when elevation was declined.
    public static Process? StartCaptureProxy(string proxyPath, string syncDir)
    {
        Directory.CreateDirectory(syncDir);
        return StartElevated(proxyPath, [
            "--timeout-min", TimeoutMinutes.ToString(CultureInfo.InvariantCulture),
            "--output", Path.Combine(syncDir, InventoryFile),
            "--progression", Path.Combine(syncDir, ProgressionFile),
            "--evidence", syncDir]);
    }

    // Removes a leftover hosts line and CA (one UAC prompt). Null when elevation was declined.
    public static Process? StartCleanup(string proxyPath) => StartElevated(proxyPath, ["--cleanup"]);

    static Process? StartElevated(string path, string[] arguments)
    {
        var start = new ProcessStartInfo(path) { UseShellExecute = true, Verb = "runas", WindowStyle = ProcessWindowStyle.Hidden };
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        try
        {
            return Process.Start(start);
        }
        catch (Win32Exception)
        {
            return null;
        }
    }

    public static bool IsListening(string syncDir) => File.Exists(Path.Combine(syncDir, ListeningFile));

    public static void OpenVanillaGame() =>
        Process.Start(new ProcessStartInfo("steam://rungameid/553850") { UseShellExecute = true })?.Dispose();

    // After the proxy exits with success: moves its files into the snapshot folder and writes the metadata.
    // Returns null on success, or what went wrong. A previous snapshot is replaced only by a complete new inventory.
    public static string? FinalizeCapture(string syncDir, string snapshotDir)
    {
        var inventory = Path.Combine(syncDir, InventoryFile);
        if (!File.Exists(inventory)) return "The capture finished without an inventory.";
        int items;
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(inventory));
            if (!doc.RootElement.TryGetProperty("items", out var list) || list.ValueKind != JsonValueKind.Array)
                return "The captured inventory has no items list.";
            items = list.GetArrayLength();
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException)
        {
            return "Could not read the captured inventory: " + error.Message;
        }
        var progression = Path.Combine(syncDir, ProgressionFile);
        long? experience = null;
        if (File.Exists(progression))
        {
            try
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(progression));
                if (doc.RootElement.ValueKind != JsonValueKind.Object) return "The captured progression is not an object.";
                if (doc.RootElement.TryGetProperty("experience", out var xp) && xp.ValueKind == JsonValueKind.Number && xp.TryGetInt64(out var value)) experience = value;
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException)
            {
                return "Could not read the captured progression: " + error.Message;
            }
        }
        try
        {
            Directory.CreateDirectory(snapshotDir);
            File.Move(inventory, Path.Combine(snapshotDir, InventoryFile), overwrite: true);
            // Progression from an earlier capture must not stay beside a newer inventory.
            var snapshotProgression = Path.Combine(snapshotDir, ProgressionFile);
            if (File.Exists(progression)) File.Move(progression, snapshotProgression, overwrite: true);
            else File.Delete(snapshotProgression);
            var metadata = new JsonObject
            {
                ["capturedUtc"] = DateTime.UtcNow.ToString("O", CultureInfo.InvariantCulture),
                ["items"] = items,
                ["experience"] = experience
            };
            File.WriteAllText(Path.Combine(snapshotDir, MetadataFile), metadata.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n");
            return null;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return "Could not save the snapshot: " + error.Message;
        }
    }
}

public sealed record SnapshotInfo(DateTime? CapturedUtc, int Items, long? Experience);

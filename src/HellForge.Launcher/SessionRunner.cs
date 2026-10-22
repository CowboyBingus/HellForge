using System.Diagnostics;
using HellForge.Core;

namespace HellForge.Launcher;

// Starts and stops the engine's session runner (HellForge.Session.exe play). It owns every safety step (verified copy,
// Steam ownership, network block, cleanup); this class only starts it and asks it to stop. It runs without a window and
// reports through the run directory's files, so it does not depend on this process staying alive to clean up.
sealed class SessionRunner
{
    Process? controller;

    public static bool IsSupported => OperatingSystem.IsWindows();

    public static string UnsupportedMessage => OperatingSystem.IsLinux()
        ? "Linux sessions are not supported yet."
        : "Sessions are only supported on Windows for now.";

    public bool IsRunning => controller is { HasExited: false };

    // The template only matters when the backend creates the profile on first use.
    // modSync: the next session's mods after a mod sync (ModSyncSession), for this one session.
    public void Start(LauncherPaths paths, LocalProfile profile, bool localPlayFab, bool sdkShims, string war, bool engineLog, bool crashDump = false, IReadOnlyList<string>? gameOptions = null,
        string? modSync = null)
    {
        if (!IsSupported) throw new PlatformNotSupportedException(UnsupportedMessage);
        if (IsRunning) return;
        if (paths.InstallRoot == null) throw new InvalidOperationException("Sessions run in installations made with HellForge-Setup.");
        LinuxNetworkBlock.GuardSession(paths.InstallRoot);
        var start = new ProcessStartInfo(paths.Session)
        {
            WorkingDirectory = paths.Root,
            UseShellExecute = false,
            CreateNoWindow = true
        };
        foreach (var argument in new[] { "play", "--install", Path.TrimEndingDirectorySeparator(paths.InstallRoot),
                     "--profile", Path.TrimEndingDirectorySeparator(Path.GetFullPath(profile.Folder)), "--template", profile.Template })
            start.ArgumentList.Add(argument);
        if (localPlayFab) start.ArgumentList.Add("--local-playfab");
        if (sdkShims) start.ArgumentList.Add("--multiplayer");
        // the galactic war (ProfileStore.LoadWar): the live war through the mirror, or the all-planets war; the frozen war needs nothing
        if (war == ProfileStore.WarLive) { start.ArgumentList.Add("--live-war"); start.ArgumentList.Add(Path.Combine(paths.HostRoot, "live-war.json")); }
        else if (war == ProfileStore.WarOpen) start.ArgumentList.Add("--open-war");
        if (engineLog) start.ArgumentList.Add("--engine-log");
        // the player's launch options, for the session runner to add to the game's command line (GameLaunchOptions)
        if (gameOptions is { Count: > 0 }) { start.ArgumentList.Add("--game-options"); start.ArgumentList.Add(GameLaunchOptions.CommandLine(gameOptions)); }
        if (modSync != null) { start.ArgumentList.Add("--mod-sync"); start.ArgumentList.Add(modSync); }
        // The mod loader has HellForge.Cli write a full dump at the game's first crash (WriteCrashDump); the game inherits
        // the session runner's environment.
        if (crashDump)
            start.Environment["HELLFORGE_CRASH_DUMP"] = Path.Combine(paths.InstallRoot, "app", "cli", "HellForge.Cli.exe");
        // An installation keeps even temporary files inside its folder.
        var temp = Directory.CreateDirectory(Path.Combine(paths.InstallRoot, "temp")).FullName;
        start.Environment["TEMP"] = start.Environment["TMP"] = temp;
        controller = Process.Start(start) ?? throw new InvalidOperationException("The session runner did not start.");
    }

    // The session runner stops the game and the backend, then exits.
    public static void SignalStop(string runDirectory) =>
        File.WriteAllText(Path.Combine(runDirectory, "stop-observation.signal"), "stop");

    // Only before a run directory exists: the session runner has not started the game yet.
    public void Kill()
    {
        if (IsRunning) controller!.Kill();
    }

    // True once, when the controller has exited; releases it.
    public bool CollectExited()
    {
        if (controller is not { HasExited: true }) return false;
        controller.Dispose();
        controller = null;
        return true;
    }

    public async Task WaitForExitAsync(TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (IsRunning && DateTime.UtcNow < deadline) await Task.Delay(250);
    }
}

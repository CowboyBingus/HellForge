using System.Diagnostics;
using System.Globalization;
using Avalonia.Media;
using HellForge.Core;

namespace HellForge.Launcher;

// Profile > Account sync (HellForge.Core InventorySync): runs the elevated capture proxy, opens the normal game through Steam
// once the proxy listens, and saves the player's sealed inventory and progression as the profile's snapshot as soon as the
// proxy has sealed them. The proxy keeps the game connected until it exits. The backend takes premium ownership from the
// snapshot on every Play; the rest is imported as the player chooses (AccountImportFlow). Polled with the rest of the
// window, once a second, only while a sync runs.
public partial class MainWindow
{
    Process? syncProxy;
    LocalProfile? syncProfile;
    string? syncDir, syncSnapshotDir;
    bool syncGameOpened, syncStarting, syncTaken;

    // Busy until the capture is taken; after that the proxy only keeps the game connected, and imports may run.
    bool SyncBusy => syncProxy != null && !syncTaken || syncStarting;

    void InitializeSync()
    {
        SyncCard.IsVisible = InventorySync.FindCaptureProxy(paths.InstallRoot) != null;
        SyncButton.Click += async (_, _) => await StartSync();
        InitializeAccountImport();
        UpdateSyncStatus();
    }

    void UpdateSyncStatus()
    {
        if (paths.InstallRoot == null || !SyncCard.IsVisible) return;
        var info = InventorySync.ReadMetadata(InventorySync.SnapshotDirectory(paths.InstallRoot, Selected.Name));
        SyncDetailText.Text = info == null ? "No snapshot yet. Sync to copy your real inventory into this profile."
            : "Last synced " + (info.CapturedUtc?.ToLocalTime().ToString("g", CultureInfo.CurrentCulture) ?? "at an unknown time") +
              $": {info.Items:N0} items, " + (!info.Progression ? "no progression (a partial sync)."
                  : info.Experience is { } xp ? $"{xp:N0} XP." : "progression captured.");
        UpdateImportControls();
        if (SyncBusy) return;
        var leftover = InventorySync.HasLeftoverRedirect();
        SyncButton.Content = leftover ? "Clean up" : "Sync Inventory";
        if (leftover) SetSyncStatus("An earlier sync did not finish: the normal game cannot reach its servers until you clean up.", Orange);
    }

    void SetSyncStatus(string text, IBrush brush)
    {
        SyncStatusText.Text = text;
        SyncStatusText.Foreground = brush;
    }

    async Task StartSync()
    {
        if (SyncBusy || paths.InstallRoot == null || InventorySync.FindCaptureProxy(paths.InstallRoot) is not { } proxy) return;
        if (syncProxy != null)
        {
            SetSyncStatus("The last sync still keeps HELLDIVERS 2 connected: close the game first.", Orange);
            return;
        }
        if (InventorySync.HasLeftoverRedirect())
        {
            await CleanUpSync(proxy);
            return;
        }
        if (SyncRefusal(out var account) is { } refusal)
        {
            SetSyncStatus(refusal, Orange);
            return;
        }
        var install = new InstallLayout(paths.InstallRoot);
        var profile = Selected;
        var snapshot = InventorySync.SnapshotDirectory(install.Root, profile.Name);
        var folder = Path.Combine(install.Logs, "sync-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture));
        syncStarting = true;
        SyncButton.IsEnabled = false;
        SetSyncStatus("Waiting for the administrator prompt...", Gold);
        Process? started = null;
        string? failure = null;
        try { started = await Task.Run(() => InventorySync.StartCaptureProxy(proxy, folder, snapshot, account)); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { failure = error.Message; }
        syncStarting = false;
        if (started == null)
        {
            SyncButton.IsEnabled = true;
            SetSyncStatus(failure ?? "Sync cancelled: it needs the administrator prompt.", failure != null ? Red : Orange);
            return;
        }
        (syncProxy, syncProfile, syncDir, syncSnapshotDir, syncGameOpened, syncTaken) = (started, profile, folder, snapshot, false, false);
        SetSyncStatus("Starting the capture...", Gold);
        Append("Inventory sync: capture started for profile " + profile.Name + ". HELLDIVERS 2 opens through Steam; take it past the title screen.", LauncherLine);
    }

    // Why a sync cannot start now; null with the signed-in Steam account it will be sealed for.
    string? SyncRefusal(out ulong account)
    {
        account = 0;
        if (runner.IsRunning) return "Stop the HellForge session first.";
        if (InventorySync.IsGameRunning()) return "Close HELLDIVERS 2 first: the sync reads its next login.";
        if (SteamAccount.Current() is not { } signedIn) return "Sign in to Steam first: the sync is sealed for your Steam account.";
        account = signedIn;
        return null;
    }

    void RefreshSync()
    {
        if (syncProxy == null || syncProfile == null || syncDir == null || syncSnapshotDir == null) return;
        if (!syncProxy.HasExited)
        {
            if (!syncGameOpened && InventorySync.IsListening(syncDir)) OpenGameForSync();
            else if (!syncTaken && InventorySync.IsSealed(syncDir)) TakeCapture(syncProfile, syncDir, syncSnapshotDir);
            return;
        }
        var exitCode = syncProxy.ExitCode;
        syncProxy.Dispose();
        syncProxy = null;
        SyncButton.IsEnabled = true;
        var (profile, folder, snapshot) = (syncProfile, syncDir, syncSnapshotDir);
        (syncProfile, syncDir, syncSnapshotDir) = (null, null, null);
        if (syncTaken) ProxyClosed(exitCode, folder);
        else if (exitCode == 0) TakeCapture(profile, folder, snapshot);
        else SyncFailed(syncGameOpened ? $"Nothing was captured within {InventorySync.TimeoutMinutes} minutes. Close the game and sync again."
            : "The capture could not start: see " + Path.Combine(folder, "proxy.log") + ".");
        syncTaken = false;
        UpdateSyncStatus();
    }

    // The sealed capture becomes the profile's snapshot, while the proxy may still keep the game connected.
    void TakeCapture(LocalProfile profile, string folder, string snapshot)
    {
        syncTaken = true;
        if (InventorySync.FinalizeCapture(folder, snapshot) is { } error)
        {
            SyncFailed(error);
            return;
        }
        UpdateSyncStatus();
        var partial = InventorySync.ReadMetadata(snapshot) is not { Progression: true };
        Append("Inventory sync complete for profile " + profile.Name + (partial ? ", without progression (see " + Path.Combine(folder, "proxy.log") + ")." : "."), LauncherLine);
        if (syncProxy is { HasExited: false })
            Append("Inventory sync: the game stays connected through the capture until you close it; then the hosts line and certificate are removed.", LauncherLine);
        _ = AfterCaptureAsync(profile, partial ? Path.Combine(folder, "proxy.log") : null);
    }

    void SyncFailed(string error)
    {
        SetSyncStatus(error, Red);
        Append("Inventory sync failed: " + error, Red);
    }

    static string ProxyClosedLine(int exitCode, string folder) => exitCode == 0
        ? "Inventory sync: the game closed, and the capture removed its hosts line and certificate."
        : "Inventory sync: the capture ended with an error; see " + Path.Combine(folder, "proxy.log") + ".";

    void ProxyClosed(int exitCode, string folder) => Append(ProxyClosedLine(exitCode, folder), exitCode == 0 ? LauncherLine : Orange);

    void OpenGameForSync()
    {
        syncGameOpened = true;
        try
        {
            InventorySync.OpenVanillaGame();
            SetSyncStatus("Log in to HELLDIVERS 2 and pass the title screen. This updates when the capture is done.", Gold);
        }
        catch (System.ComponentModel.Win32Exception)
        {
            SetSyncStatus("Steam did not open the game: start HELLDIVERS 2 from Steam and pass the title screen.", Orange);
        }
    }

    async Task CleanUpSync(string proxy)
    {
        syncStarting = true;
        SyncButton.IsEnabled = false;
        SetSyncStatus("Waiting for the administrator prompt...", Gold);
        using var cleanup = await Task.Run(() => InventorySync.StartCleanup(proxy));
        if (cleanup != null) await cleanup.WaitForExitAsync();
        syncStarting = false;
        SyncButton.IsEnabled = true;
        UpdateSyncStatus();
        if (cleanup == null) SetSyncStatus("Clean up needs the administrator prompt.", Orange);
        else if (InventorySync.HasLeftoverRedirect()) SetSyncStatus("Clean up failed: remove the HellForge-CaptureProxy lines from the hosts file.", Red);
        else SetSyncStatus("Cleaned up: the normal game reaches its servers again.", Green);
    }
}

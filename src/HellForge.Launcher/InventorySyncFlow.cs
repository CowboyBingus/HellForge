using System.Diagnostics;
using System.Globalization;
using Avalonia.Media;
using HellForge.Core;

namespace HellForge.Launcher;

// Profile > Account sync (HellForge.Core InventorySync): runs the elevated capture proxy, opens the normal game through Steam
// once the proxy listens, and saves the player's inventory and progression as the profile's snapshot, which the backend
// applies on every Play. Polled with the rest of the window, once a second, only while a sync runs.
public partial class MainWindow
{
    Process? syncProxy;
    string? syncDir, syncSnapshotDir;
    bool syncGameOpened, syncStarting;

    bool SyncBusy => syncProxy != null || syncStarting;

    void InitializeSync()
    {
        SyncCard.IsVisible = InventorySync.FindCaptureProxy(paths.InstallRoot) != null;
        SyncButton.Click += async (_, _) => await StartSync();
        UpdateSyncStatus();
    }

    void UpdateSyncStatus()
    {
        if (paths.InstallRoot == null || !SyncCard.IsVisible) return;
        var info = InventorySync.ReadMetadata(InventorySync.SnapshotDirectory(paths.InstallRoot, Selected.Name));
        SyncDetailText.Text = info == null ? "No snapshot yet. Sync to copy your real inventory into this profile."
            : "Last synced " + (info.CapturedUtc?.ToLocalTime().ToString("g", CultureInfo.CurrentCulture) ?? "at an unknown time") +
              $": {info.Items:N0} items, " + (info.Experience is { } xp ? $"{xp:N0} XP." : "no progression captured.");
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
        if (InventorySync.HasLeftoverRedirect())
        {
            await CleanUpSync(proxy);
            return;
        }
        if (runner.IsRunning)
        {
            SetSyncStatus("Stop the HellForge session first.", Orange);
            return;
        }
        if (InventorySync.IsGameRunning())
        {
            SetSyncStatus("Close HELLDIVERS 2 first: the sync reads its next login.", Orange);
            return;
        }
        var install = new InstallLayout(paths.InstallRoot);
        var snapshot = InventorySync.SnapshotDirectory(install.Root, Selected.Name);
        var folder = Path.Combine(install.Logs, "sync-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture));
        syncStarting = true;
        SyncButton.IsEnabled = false;
        SetSyncStatus("Waiting for the administrator prompt...", Gold);
        Process? started = null;
        string? failure = null;
        try { started = await Task.Run(() => InventorySync.StartCaptureProxy(proxy, folder)); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { failure = error.Message; }
        syncStarting = false;
        if (started == null)
        {
            SyncButton.IsEnabled = true;
            SetSyncStatus(failure ?? "Sync cancelled: it needs the administrator prompt.", failure != null ? Red : Orange);
            return;
        }
        (syncProxy, syncDir, syncSnapshotDir, syncGameOpened) = (started, folder, snapshot, false);
        SetSyncStatus("Starting the capture...", Gold);
        Append("Inventory sync: capture started for profile " + Selected.Name + ". HELLDIVERS 2 opens through Steam; take it past the title screen.", LauncherLine);
    }

    void RefreshSync()
    {
        if (syncProxy == null || syncDir == null || syncSnapshotDir == null) return;
        if (!syncProxy.HasExited)
        {
            if (!syncGameOpened && InventorySync.IsListening(syncDir)) OpenGameForSync();
            return;
        }
        var exitCode = syncProxy.ExitCode;
        syncProxy.Dispose();
        syncProxy = null;
        SyncButton.IsEnabled = true;
        var error = exitCode == 0 ? InventorySync.FinalizeCapture(syncDir, syncSnapshotDir)
            : syncGameOpened ? $"Nothing was captured within {InventorySync.TimeoutMinutes} minutes. Close the game and sync again."
            : "The capture could not start: see " + Path.Combine(syncDir, "proxy.log") + ".";
        if (error == null)
        {
            SetSyncStatus("Synced. The inventory and progression apply on the next Play.", Green);
            Append("Inventory sync complete: " + syncSnapshotDir, LauncherLine);
        }
        else
        {
            SetSyncStatus(error, Red);
            Append("Inventory sync failed: " + error, Red);
        }
        (syncDir, syncSnapshotDir) = (null, null);
        UpdateSyncStatus();
    }

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

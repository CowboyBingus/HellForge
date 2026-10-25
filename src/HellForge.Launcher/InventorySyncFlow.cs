using System.Diagnostics;
using System.Globalization;
using Avalonia.Media;
using HellForge.Core;

namespace HellForge.Launcher;

// Profile > Account sync (HellForge.Core InventorySync): runs the elevated capture proxy, opens the normal game through Steam
// once the proxy listens, and saves the player's sealed inventory and progression as the profile's snapshot. The backend
// takes premium ownership from it on every Play; the rest is imported as the player chooses (AccountImportFlow). Polled
// with the rest of the window, once a second, only while a sync runs.
public partial class MainWindow
{
    Process? syncProxy;
    LocalProfile? syncProfile;
    string? syncDir, syncSnapshotDir;
    bool syncGameOpened, syncStarting;

    bool SyncBusy => syncProxy != null || syncStarting;

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
              $": {info.Items:N0} items, " + (info.Experience is { } xp ? $"{xp:N0} XP." : "no progression captured.");
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
        (syncProxy, syncProfile, syncDir, syncSnapshotDir, syncGameOpened) = (started, profile, folder, snapshot, false);
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
            return;
        }
        var exitCode = syncProxy.ExitCode;
        syncProxy.Dispose();
        syncProxy = null;
        SyncButton.IsEnabled = true;
        var error = exitCode == 0 ? InventorySync.FinalizeCapture(syncDir, syncSnapshotDir)
            : syncGameOpened ? $"Nothing was captured within {InventorySync.TimeoutMinutes} minutes. Close the game and sync again."
            : "The capture could not start: see " + Path.Combine(syncDir, "proxy.log") + ".";
        var synced = syncProfile;
        (syncProfile, syncDir, syncSnapshotDir) = (null, null, null);
        UpdateSyncStatus();
        if (error == null)
        {
            Append("Inventory sync complete for profile " + synced.Name + ".", LauncherLine);
            _ = AfterCaptureAsync(synced);
        }
        else
        {
            SetSyncStatus(error, Red);
            Append("Inventory sync failed: " + error, Red);
        }
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

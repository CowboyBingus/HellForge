using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Layout;
using HellForge.Core;

namespace HellForge.Launcher;

// Profile > Account sync, after a capture: the review of what to import (one row per AccountImportSettings category, with
// the profile's and the account's side), the import itself, and profile backups with Restore. Previews, imports and
// restores run the local backend's profile commands (HellForge.LocalBackend AccountCommands), which hold the profile's
// lease, verify the snapshot's seal and back the profile up before changing it.
public partial class MainWindow
{
    readonly List<(ImportCategory Category, CheckBox Include, ComboBox Rule)> importRows = [];
    LocalProfile? importProfile;
    bool importBusy;

    static string RuleLabel(ImportRule rule) => rule switch
    {
        ImportRule.Higher => "Keep the higher",
        ImportRule.Merge => "Merge both",
        _ => "Use vanilla"
    };

    void InitializeAccountImport()
    {
        ReviewImportButton.Click += async (_, _) => await ShowReviewAsync(Selected);
        ImportApplyButton.Click += async (_, _) => await ApplyReviewAsync();
        ImportCancelButton.Click += (_, _) =>
        {
            ImportPanel.IsVisible = false;
            SetSyncStatus("Nothing imported. Premium items you own still follow the sync.", Light);
        };
        RestoreBackupButton.Click += async (_, _) => await RestoreBackupAsync();
    }

    // The review button when the selected profile has a snapshot; the backups it has.
    void UpdateImportControls()
    {
        if (paths.InstallRoot == null) return;
        var profile = Selected;
        ReviewImportButton.IsVisible = InventorySync.HasSnapshot(InventorySync.SnapshotDirectory(paths.InstallRoot, profile.Name));
        if (importProfile != null && importProfile.Name != profile.Name) ImportPanel.IsVisible = false;
        var backups = ProfileBackups.List(profile.Folder);
        BackupCombo.ItemsSource = backups.Select(b => b.SavedUtc.ToLocalTime().ToString("g", CultureInfo.CurrentCulture) + " — before " + b.Reason).ToList();
        BackupCombo.SelectedIndex = backups.Count > 0 ? 0 : -1;
        BackupCombo.IsVisible = RestoreBackupButton.IsVisible = backups.Count > 0;
    }

    // partialLog: the capture's log when it got the inventory without the progression, so only part of the review is filled.
    async Task AfterCaptureAsync(LocalProfile profile, string? partialLog)
    {
        var (synced, brush) = partialLog == null ? ("Synced", Green)
            : ($"Partly synced: your inventory, but not your progression (see {partialLog}). Level and XP, warbond unlocks, ship " +
               "modules and weapon levels, ship name and tutorial progress can't be imported from it", Orange);
        if (!AccountImportSettings.Load(AccountImportSettings.PathIn(profile.Folder)).AutoApply)
        {
            SetSyncStatus(synced + ". Choose what to import below.", brush);
            await ShowReviewAsync(profile);
            return;
        }
        if (await RunImportAsync(profile)) SetSyncStatus(synced + ". Imported with your saved choices; a backup was made first.", brush);
    }

    async Task ShowReviewAsync(LocalProfile profile)
    {
        if (importBusy) return;
        if (ImportRefusal() is { } refusal)
        {
            SetSyncStatus(refusal, Orange);
            return;
        }
        importBusy = true;
        var (code, output, error) = await RunBackendAsync("profile", "import-preview", profile.Folder, profile.Template);
        importBusy = false;
        if (code != 0)
        {
            SetSyncStatus(BackendMessage(error, "The snapshot could not be read."), Red);
            return;
        }
        try
        {
            using var preview = JsonDocument.Parse(output);
            BuildReview(profile, preview.RootElement);
        }
        catch (Exception problem) when (problem is JsonException or KeyNotFoundException or InvalidOperationException)
        {
            SetSyncStatus("The backend's review could not be read: " + problem.Message, Red);
        }
    }

    void BuildReview(LocalProfile profile, JsonElement preview)
    {
        importProfile = profile;
        var settings = AccountImportSettings.Load(AccountImportSettings.PathIn(profile.Folder));
        var sides = preview.GetProperty("categories");
        ImportPremiumText.Text = "Always synced: " + preview.GetProperty("premium").GetString() + " you own. Super Credits are never imported.";
        ImportRows.Children.Clear();
        importRows.Clear();
        foreach (var category in AccountImportSettings.Categories)
        {
            var side = sides.GetProperty(category.Id);
            var include = new CheckBox { Content = category.Label, IsChecked = settings.Includes(category.Id), VerticalAlignment = VerticalAlignment.Center };
            var rule = new ComboBox
            {
                ItemsSource = category.Rules.Select(RuleLabel).ToList(), SelectedIndex = category.Rules.ToList().IndexOf(settings.RuleFor(category.Id)),
                IsVisible = category.Rules.Count > 1, MinWidth = 160, VerticalAlignment = VerticalAlignment.Center
            };
            var detail = new TextBlock
            {
                Text = "HellForge: " + side.GetProperty("hellforge").GetString() + "   ·   Vanilla: " + side.GetProperty("vanilla").GetString(),
                Classes = { "muted" }, FontSize = 12, TextWrapping = Avalonia.Media.TextWrapping.Wrap, VerticalAlignment = VerticalAlignment.Center,
                Margin = new Avalonia.Thickness(12, 0)
            };
            var row = new Grid { ColumnDefinitions = new ColumnDefinitions("260,*,Auto") };
            Grid.SetColumn(detail, 1);
            Grid.SetColumn(rule, 2);
            row.Children.Add(include);
            row.Children.Add(detail);
            row.Children.Add(rule);
            ImportRows.Children.Add(row);
            importRows.Add((category, include, rule));
        }
        ImportAutoBox.IsChecked = settings.AutoApply;
        ImportPanel.IsVisible = true;
    }

    async Task ApplyReviewAsync()
    {
        if (importProfile is not { } profile || importBusy) return;
        var settings = new AccountImportSettings { AutoApply = ImportAutoBox.IsChecked == true };
        foreach (var (category, include, rule) in importRows)
            settings.Set(category.Id, include.IsChecked == true, category.Rules[Math.Max(0, rule.SelectedIndex)]);
        try { settings.Save(AccountImportSettings.PathIn(profile.Folder)); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            SetSyncStatus("Could not save the import choices: " + error.Message, Red);
            return;
        }
        if (!await RunImportAsync(profile)) return;
        ImportPanel.IsVisible = false;
        SetSyncStatus("Imported. The profile was backed up first; Restore backup puts it back.", Green);
    }

    async Task<bool> RunImportAsync(LocalProfile profile)
    {
        if (ImportRefusal() is { } refusal)
        {
            SetSyncStatus(refusal, Orange);
            return false;
        }
        importBusy = true;
        SetSyncStatus("Importing...", Gold);
        var (code, _, error) = await RunBackendAsync("profile", "import", profile.Folder, profile.Template);
        importBusy = false;
        if (code != 0)
        {
            SetSyncStatus(BackendMessage(error, "The import failed."), Red);
            return false;
        }
        Append("Account import applied to profile " + profile.Name + ".", LauncherLine);
        ShowProfile();
        UpdateImportControls();
        return true;
    }

    async Task RestoreBackupAsync()
    {
        var profile = Selected;
        var backups = ProfileBackups.List(profile.Folder);
        if (importBusy || BackupCombo.SelectedIndex < 0 || BackupCombo.SelectedIndex >= backups.Count) return;
        if (ImportRefusal() is { } refusal)
        {
            SetSyncStatus(refusal, Orange);
            return;
        }
        importBusy = true;
        var (code, _, error) = await RunBackendAsync("profile", "restore", profile.Folder, backups[BackupCombo.SelectedIndex].Path);
        importBusy = false;
        if (code != 0)
        {
            SetSyncStatus(BackendMessage(error, "The restore failed."), Red);
            return;
        }
        SetSyncStatus("Restored. The profile it replaced is the newest backup.", Green);
        Append("Profile " + profile.Name + " restored from a backup.", LauncherLine);
        ShowProfile();
        UpdateImportControls();
    }

    // The profile is in use while a session runs; the backend would refuse anyway.
    string? ImportRefusal() => runner.IsRunning ? "Stop the HellForge session first: it holds the profile." : SyncBusy ? "Wait for the sync to finish." : null;

    static string BackendMessage(string error, string fallback)
    {
        var line = error.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries).LastOrDefault();
        return line == null ? fallback : line.Replace("HellForge local backend: ", "", StringComparison.Ordinal);
    }

    async Task<(int Code, string Output, string Error)> RunBackendAsync(params string[] arguments)
    {
        var start = new ProcessStartInfo(paths.Backend) { UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true };
        foreach (var argument in arguments) start.ArgumentList.Add(argument);
        try
        {
            using var process = Process.Start(start) ?? throw new IOException("The local backend did not start.");
            var output = process.StandardOutput.ReadToEndAsync();
            var error = process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync();
            return (process.ExitCode, await output, await error);
        }
        catch (Exception problem) when (problem is IOException or System.ComponentModel.Win32Exception)
        {
            return (-1, "", "HellForge local backend: " + problem.Message);
        }
    }
}

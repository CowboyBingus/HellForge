using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Media;
using HellForge.Core;

namespace HellForge.Launcher;

// The MOD SYNC prompt in the launcher, for a session whose game has no in-game editor to show it (the editor switched off
// on the Mods page): the offer the session's backend writes (<run>/backend/mod-sync.json, ModSyncOffer) on the Play page,
// with the prompt's answers, written back for the backend to take (docs/mods.md "Playing with other mods"). With the
// editor on, the game shows the prompt and this card stays hidden.
public partial class MainWindow
{
    bool sessionHasEditor = true; // the editor was on when this session started (a session keeps the mods it started with)
    string? modSyncShown;         // what the card shows now: version, state, progress
    int modSyncAnswered = -1;     // the offer version last answered from here

    // At a session's start: whether its game shows the prompt itself.
    void NoteSessionEditor()
    {
        modSyncShown = null;
        modSyncAnswered = -1;
        sessionHasEditor = true;
        if (paths.ModsFolder == null) return; // not an installation: no session starts
        try
        {
            var (mods, _) = ModCatalog.Load(paths.ModsFolder);
            sessionHasEditor = mods.Any(m => m.Id == "hellforge.editor" && m.Enabled);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            sessionHasEditor = true; // unknown: leave the prompt to the game
            Append("Could not read the mods folder for mod sync: " + error.Message, Red);
        }
    }

    // Each refresh while a session runs: the card follows the offer.
    void ShowModSync(string runDirectory)
    {
        var offer = sessionHasEditor ? null : ModSyncOffer.Read(Path.Combine(runDirectory, "backend"));
        if (offer == null || offer.State is "none" or "" || offer.Version == modSyncAnswered)
        {
            ModSyncCard.IsVisible = false;
            modSyncShown = null;
            return;
        }
        var key = $"{offer.Version}|{offer.State}|{offer.Done}|{offer.Mods.Count}";
        if (key == modSyncShown) return;
        if (modSyncShown == null) Append($"MOD SYNC: {offer.Message}", LauncherLine);
        modSyncShown = key;
        RenderModSync(runDirectory, offer);
    }

    void RenderModSync(string runDirectory, ModSyncOffer offer)
    {
        ModSyncPanel.Children.Clear();
        ModSyncPanel.Children.Add(new TextBlock { Text = $"MOD SYNC  ·  {offer.Host}", Classes = { "h2" }, Margin = new(0) });
        ModSyncPanel.Children.Add(new TextBlock { Text = offer.Message, TextWrapping = TextWrapping.Wrap });
        foreach (var mod in offer.Mods) ModSyncPanel.Children.Add(ModSyncRow(mod));
        if (offer.State == "downloading")
            ModSyncPanel.Children.Add(new ProgressBar { Classes = { "detail" }, Minimum = 0, Maximum = Math.Max(1, offer.Bytes), Value = offer.Done, Margin = new(0, 4, 0, 0) });
        var buttons = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 10, Margin = new(0, 6, 0, 0) };
        void Add(string label, string answer, bool primary)
        {
            var button = new Button { Content = label, Classes = { primary ? "primary" : "ghost" } };
            button.Click += (_, _) => AnswerModSync(runDirectory, offer, answer);
            buttons.Children.Add(button);
        }
        switch (offer.State)
        {
            case "offer" when offer.Bytes == 0: Add("RESTART TO JOIN", "restart", true); Add("CANCEL", "decline", false); break;
            case "offer": Add("DOWNLOAD AND RESTART", "accept", true); Add("CANCEL", "decline", false); break;
            case "downloading": Add("CANCEL", "decline", false); break;
            case "ready": Add("RESTART TO JOIN", "restart", true); Add("CANCEL", "decline", false); break;
            default: Add("OK", "decline", false); break; // blocked or failed: nothing to do but close it
        }
        ModSyncPanel.Children.Add(buttons);
        ModSyncCard.IsVisible = true;
    }

    static Control ModSyncRow(ModSyncOffer.Mod mod)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        if (mod.Class == "code") row.Children.Add(ModTag("CODE", "#E05591"));
        else if (mod.Class == "raw") row.Children.Add(ModTag("RAW DATA", "#F2A541"));
        row.Children.Add(new TextBlock { Text = mod.Name.Length > 0 ? mod.Name : mod.Id, VerticalAlignment = VerticalAlignment.Center });
        if (mod.Version.Length > 0) row.Children.Add(new TextBlock { Text = mod.Version, FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center });
        var what = mod.Action switch
        {
            "have" => "you have it",
            "download" => $"downloads ({mod.Bytes / 1048576.0:0.0} MB)",
            "manual" => "install it yourself",
            "blocked" => "blocked",
            "update" => "needs another HellForge",
            "off" => "off for this session",
            _ => mod.Action
        };
        row.Children.Add(new TextBlock { Text = "·  " + what, FontSize = 12, Classes = { "muted" }, VerticalAlignment = VerticalAlignment.Center });
        return row;
    }

    void AnswerModSync(string runDirectory, ModSyncOffer offer, string answer)
    {
        try { ModSyncOffer.Answer(Path.Combine(runDirectory, "backend"), offer.Version, answer); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            Append("Could not send the mod sync answer: " + error.Message, Red);
            return;
        }
        Append($"MOD SYNC: {answer} sent to the session.", LauncherLine);
        if (answer == "decline")
        {
            modSyncAnswered = offer.Version;
            ModSyncCard.IsVisible = false;
        }
        modSyncShown = null;
    }
}

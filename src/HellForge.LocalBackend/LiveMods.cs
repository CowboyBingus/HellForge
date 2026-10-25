using HellForge.Core;

namespace HellForge.LocalBackend;

// Changes the running session's mods without a restart (HellForge-Open docs/mods.md "Changing mods while you play"): the
// Mods page's changes (the launcher asks: GET and POST /hellforge/live-mods) or, after a mod sync, the host's set. It plans
// the change (LiveModPlan), sends it to the game's loader over the live pipe (native/mod-loader/live_mods.c) and waits
// for the loader's result. Then the session runs the new mods: its session-mods.json and modset.json say so, and Changed
// gives the new set to the lobby, mod sync and the directory, so joins are checked against the new digest.
public sealed class LiveMods
{
    public sealed record Result(bool Live, bool Applied, IReadOnlyList<string> Changes, IReadOnlyList<string> Blocking, string Detail, string Digest);

    public static readonly TimeSpan ResultTimeout = TimeSpan.FromSeconds(30);
    readonly LiveEditServer live;
    readonly string modsFolder, sessionFile, modSetFile;
    readonly Action<string> log;
    readonly SemaphoreSlim one = new(1, 1);
    readonly object gate = new();
    TaskCompletionSource<string[]>? result;
    string? waitingFor;
    int changes;

    public LiveMods(LiveEditServer live, string modsFolder, string sessionFile, string modSetFile, Action<string> log)
    {
        this.live = live;
        this.modsFolder = modsFolder;
        this.sessionFile = sessionFile;
        this.modSetFile = modSetFile;
        this.log = log;
        live.Received += Heard;
    }

    // The mod set the session plays with after a change.
    public event Action<ModSet>? Changed;

    void Heard(string[] parts)
    {
        if (parts.Length >= 6 && parts[0] == "modstate") log($"Live mods {parts[1]}: {parts[2]} {parts[3]}, {parts[4]} stats edit(s) applied, {parts[5]} refused.");
        if (parts.Length < 3 || parts[0] != "modresult") return;
        lock (gate) if (parts[1] == waitingFor) result?.TrySetResult(parts);
    }

    public SessionMods Current() => SessionMods.Read(sessionFile);

    // The installation's mods as its Mods page has them, in load order.
    public List<ModManifest> Installation() => ModCatalog.LoadOrder(ModCatalog.Active(ModCatalog.Load(modsFolder).Mods));

    // What a change to `target` (the Mods page's mods when null) would do, without making it.
    public Result Preview(IReadOnlyList<ModManifest>? target = null)
    {
        var current = Current();
        var plan = LiveModPlan.Make(current, target ?? Installation());
        var mine = current.SyncedWith.Length > 0 && target == null ? SyncedRefusal(current) : null;
        return new(plan.Live && mine == null, false, Describe(plan), mine != null ? [mine] : plan.Blocking,
            plan.Empty ? "Nothing changed." : plan.Live && mine == null ? "Applies now." : "Applies to the next session.", "");
    }

    // Makes the change. target: the Mods page's mods when null; sync: a host's set after a mod sync (syncedWith: the host).
    // expectDigest: the digest the change must give (mod sync: the host's), else it is not made.
    public async Task<Result> ApplyAsync(IReadOnlyList<ModManifest>? target, bool sync, string syncedWith, CancellationToken cancel, string? expectDigest = null)
    {
        await one.WaitAsync(cancel);
        try
        {
            var current = Current();
            if (!sync && current.SyncedWith.Length > 0) return new(false, false, [], [SyncedRefusal(current)], "Applies to the next session.", "");
            var plan = LiveModPlan.Make(current, target ?? Installation());
            var described = Describe(plan);
            if (plan.Empty) return new(true, false, [], [], "Nothing changed.", "");
            if (!plan.Live) return new(false, false, described, plan.Blocking, "Applies to the next session.", "");
            if (!live.Connected) return new(true, false, described, [], "The game is not connected yet; try again once it has loaded.", "");
            // what runs: the target, but for mods that did not load at the start and are unchanged since
            var active = plan.Target.Where(m => !current.Skipped.Any(s => s.Id == m.Id && s.Hash == ModSets.ContentHash(m))).ToList();
            var set = ModSet.Of(active);
            if (expectDigest != null && set.Digest != expectDigest)
                return new(false, false, described, ["The mods here do not make the host's mod list."], "Applies to the next session.", "");
            var txn = "live" + Interlocked.Increment(ref changes).ToString(System.Globalization.CultureInfo.InvariantCulture);
            var waiter = new TaskCompletionSource<string[]>(TaskCreationOptions.RunContinuationsAsynchronously);
            lock (gate) { result = waiter; waitingFor = txn; }
            foreach (var (verb, fields) in plan.Lines(txn, sync, set.Digest))
            {
                var reply = await live.ModAsync(verb, fields);
                if (reply.Ok) continue;
                if (verb != "modbegin") await live.ModAsync("modabort", [txn]);
                log($"Live mods {txn} refused at {verb}: {reply.Detail.Replace('\t', ' ')}");
                return new(false, false, described, [Explain(reply.Detail)], "Applies to the next session.", "");
            }
            string[] done;
            try { done = await waiter.Task.WaitAsync(ResultTimeout, cancel); }
            catch (TimeoutException)
            {
                log($"Live mods {txn}: the game did not report its result within {ResultTimeout.TotalSeconds} s.");
                return new(false, false, described, ["The game did not finish the change."], "Restart the session to be sure of its mods.", "");
            }
            if (done[2] != "ok") return new(false, false, described, [Explain(string.Join('\t', done[3..]))], "Applies to the next session.", "");
            SessionMods.Of(plan.Target, active, sync ? syncedWith : current.SyncedWith).Write(sessionFile);
            set.Write(modSetFile);
            log($"Live mods {txn}: {string.Join("; ", described)}. The mods' digest is now {set.Digest}.");
            Changed?.Invoke(set);
            return new(true, true, described, [], "Applied.", set.Digest);
        }
        finally
        {
            lock (gate) { result = null; waitingFor = null; }
            one.Release();
        }
    }

    // A host's mods after a mod sync, in load order with this player's cosmetic mods and Local Stats (ModSyncSession.Resolve);
    // null when some are not here as the host has them.
    List<ModManifest>? HostOrder(ModSyncSession sync)
    {
        var (catalog, _) = ModCatalog.Load(modsFolder);
        return sync.Resolve(modsFolder, catalog).LoadOrder;
    }

    // Whether the running game can take a host's mods (mod sync's APPLY AND JOIN).
    public bool CanApplySync(ModSyncSession sync)
    {
        if (HostOrder(sync) is not { } order) return false;
        var current = Current();
        var plan = LiveModPlan.Make(current, order);
        if (!plan.Live && !plan.Empty) return false;
        var active = plan.Target.Where(m => !current.Skipped.Any(s => s.Id == m.Id && s.Hash == ModSets.ContentHash(m))).ToList();
        return ModSet.Of(active).Digest == sync.Digest;
    }

    // The running game takes a host's mods: null when done, else why not.
    public async Task<string?> ApplySyncAsync(ModSyncSession sync, CancellationToken cancel)
    {
        if (HostOrder(sync) is not { } order) return "some of the host's mods are not here as the host has them";
        var result = await ApplyAsync(order, sync: true, sync.HostName.Length > 0 ? sync.HostName : sync.Host, cancel, expectDigest: sync.Digest);
        if (result.Applied || result.Live && result.Changes.Count == 0) return null;
        return string.Join(" ", result.Blocking.DefaultIfEmpty(result.Detail));
    }

    static string SyncedRefusal(SessionMods current) =>
        $"This session plays {current.SyncedWith}'s mods (mod sync): your Mods page applies to your next session.";

    static List<string> Describe(LiveModPlan plan) => [.. plan.Changes.Select(c => c.Name + c.Action switch
    {
        LiveModAction.Add => " on",
        LiveModAction.Remove => " off",
        _ => " changed"
    })];

    // A loader refusal ("<reason>\t<why>") said for the player.
    static string Explain(string detail)
    {
        var parts = detail.Split('\t', 2);
        var why = parts.Length > 1 ? parts[1] : parts[0];
        return parts[0] switch
        {
            "coop" => "Friends are in the game: " + why + ".",
            "busy" => "Another change is under way.",
            "state" => "The game is still loading: " + why + ".",
            _ => why.Length > 0 ? char.ToUpperInvariant(why[0]) + why[1..] + "." : "The game refused the change."
        };
    }
}

// The session's LiveMods, once the live pipe exists (it is made after the backend's HTTP server starts).
public sealed class LiveModsHub
{
    public LiveMods? Service { get; set; }
}

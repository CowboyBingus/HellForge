using HellForge.Core;

namespace HellForge.LocalBackend.Multiplayer;

// The host's stats in co-op (docs/multiplayer.md step 5, HellForge.Core.StatsSync): everyone in a session plays with the
// stats of the player whose lobby it is.
//   host:   this machine's Local Stats file (mods/zz.local.stats/hellforge-stats.json, which the in-game editor saves on
//           APPLY) goes to every friend in a lobby hosted here when they join, and again, numbered, whenever it changes:
//           "stats.set" [version, gzip of the file ("" none), gzip of the in-game editor's mod content values
//           (hellforge-editor/mod-stats.json: content mods added, which Local Stats does not hold), gzip of its arm swaps
//           (the "mounts" of hellforge-editor/editor-settings.json: a vehicle's mount slot and the weapon it holds)].
//   joiner: the host's file arrives from the friend whose lobby this machine joined; this backend serves the session's
//           live-edit pipe (\\.\pipe\hellforge-live-<run id>, native/mod-loader/live.c) and turns it into live edits of
//           the Local Stats mod in this game. The host's mod content values go to host-mod-stats.json in the in-game
//           editor's folder, which the editor applies in place of its own while joined, and the host's Local Stats file to
//           host-stats.json beside it, which the editor shows as the saved changes (marks, its Changes list), read-only,
//           and the host's arm swaps to host-mounts.json, which the editor applies in place of its own.
//           When the host goes (left, closed or silent), the files go and its own values come back.
// The game is told its role ("session" solo / host / joiner, with the host's name): the in-game editor turns read-only
// while joined. Fields live editing does not reach (attachments, status multipliers) stay each machine's own.
public sealed class StatsShare : IAsyncDisposable
{
    public const string FileName = "hellforge-stats.json";
    public const string ModValuesName = "mod-stats.json", HostModValuesName = "host-mod-stats.json", HostStatsName = "host-stats.json";
    public const string EditorSettingsName = "editor-settings.json", HostMountsName = "host-mounts.json";
    readonly IPeerTransport transport;
    readonly LobbyService lobby;
    readonly string statsFile, modValuesFile, hostModValuesFile, hostStatsFile, editorSettingsFile, hostMountsFile;
    readonly Func<ulong, string> nameOf;
    readonly Action<string> log;
    readonly LiveEditServer live;
    // The session's live-edit pipe, which mod sync shares (ModSync's offers to the game, its answers back).
    public LiveEditServer Live => live;
    readonly StatsSync sync = new();
    readonly SemaphoreSlim applying = new(1, 1);
    readonly object gate = new();
    readonly Timer timer;
    // host: the file last read, its version, and the friends that have it
    string? mine, mineMods, mineSettings, mineMounts;
    DateTime mineStamp, modsStamp, settingsStamp;
    long length = -1, modsLength = -1, settingsLength = -1;
    long version;
    readonly HashSet<ulong> sentTo = [];
    // joiner: the host this game plays with, its last version and file
    ulong hostPeer;
    long hostVersion = -1;
    string? hostStats, hostMods, hostMounts;
    string role = "solo", roleHost = "";
    bool sessionSent;

    public StatsShare(IPeerTransport transport, LobbyService lobby, string runId, string modsFolder, Func<ulong, string> nameOf, Action<string> log)
    {
        this.transport = transport;
        this.lobby = lobby;
        this.nameOf = nameOf;
        this.log = log;
        statsFile = Path.Combine(modsFolder, "zz.local.stats", FileName);
        var editor = Path.Combine(modsFolder, BundledMods.EditorFolder);
        modValuesFile = Path.Combine(editor, ModValuesName);
        hostModValuesFile = Path.Combine(editor, HostModValuesName);
        hostStatsFile = Path.Combine(editor, HostStatsName);
        editorSettingsFile = Path.Combine(editor, EditorSettingsName);
        hostMountsFile = Path.Combine(editor, HostMountsName);
        WriteHost(null, null, null); /* a session cut short leaves its host's files behind */
        live = new LiveEditServer(runId);
        live.StateChanged += () => { lock (gate) sessionSent = false; _ = ApplyAsync(); };
        live.Start();
        timer = new Timer(_ => Tick(), null, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(1));
    }

    // Every second: the host's file (sent to friends who joined or when it changed), and whether the host is still there.
    void Tick()
    {
        try
        {
            ReadMine();
            var friends = lobby.HostedFriends();
            List<ulong> send;
            byte[] packed, packedMods, packedMounts;
            long sending;
            lock (gate)
            {
                sentTo.IntersectWith(friends);
                send = friends.Where(f => !sentTo.Contains(f)).ToList();
                sentTo.UnionWith(send);
                packed = StatsSync.Pack(mine ?? "");
                packedMods = StatsSync.Pack(mineMods ?? "");
                packedMounts = StatsSync.Pack(mineMounts ?? "");
                sending = version;
            }
            foreach (var friend in send)
            {
                transport.Send(friend, new Frame("stats.set").Add(sending).Add(packed).Add(packedMods).Add(packedMounts).Encode(), reliable: true);
                log($"Stats: sent this host's Local Stats, mod content values and arm swaps (version {sending}, {packed.Length} + {packedMods.Length} + {packedMounts.Length} bytes) to {LobbyService.EntityId(friend)}.");
            }
            bool hostGone;
            lock (gate)
            {
                hostGone = hostPeer != 0 && !lobby.HostsJoinedLobby(hostPeer);
                if (hostGone)
                {
                    log($"Stats: the host {LobbyService.EntityId(hostPeer)} is gone; this game's own values come back.");
                    (hostPeer, hostVersion, hostStats, hostMods, hostMounts) = (0, -1, null, null, null);
                }
                var (wantRole, wantHost) = hostPeer != 0 ? ("joiner", nameOf(hostPeer)) : friends.Count > 0 ? ("host", "") : ("solo", "");
                if (wantRole != role || wantHost != roleHost) (role, roleHost, sessionSent) = (wantRole, wantHost, false);
            }
            if (hostGone) WriteHost(null, null, null);
            if (hostGone || !sessionSent) _ = ApplyAsync();
        }
        catch (Exception error) { log("Stats sharing failed: " + error.Message); }
    }

    // The Local Stats file, the editor's mod content values and its arm swaps, re-read when they changed (a new version
    // for friends; the editor's settings change for its own reasons too, so only a change of its mounts counts).
    void ReadMine()
    {
        var changed = ReadChanged(statsFile, ref mineStamp, ref length, ref mine) | ReadChanged(modValuesFile, ref modsStamp, ref modsLength, ref mineMods);
        if (ReadChanged(editorSettingsFile, ref settingsStamp, ref settingsLength, ref mineSettings))
        {
            var mounts = MountsOf(mineSettings);
            lock (gate) if (mounts != mineMounts) { mineMounts = mounts; changed = true; }
        }
        if (changed)
            lock (gate)
            {
                version++;
                sentTo.Clear();
            }
    }

    // The arm swaps of the editor's settings: its "mounts" object as JSON ("" none or unreadable).
    static string MountsOf(string? settings)
    {
        if (string.IsNullOrWhiteSpace(settings)) return "";
        try { return System.Text.Json.Nodes.JsonNode.Parse(settings) is System.Text.Json.Nodes.JsonObject root && root["mounts"] is System.Text.Json.Nodes.JsonObject mounts ? mounts.ToJsonString() : ""; }
        catch (System.Text.Json.JsonException) { return ""; }
    }

    // A file's text, when its time or size changed and its text with them; false when unchanged or being written.
    bool ReadChanged(string path, ref DateTime seenStamp, ref long seenLength, ref string? seen)
    {
        var info = new FileInfo(path);
        var stamp = info.Exists ? info.LastWriteTimeUtc : DateTime.MinValue;
        var size = info.Exists ? info.Length : -1;
        lock (gate) if (stamp == seenStamp && size == seenLength) return false;
        string? text = null;
        try { if (info.Exists) text = File.ReadAllText(path); }
        catch (IOException) { return false; } /* the editor is writing it: next second */
        lock (gate)
        {
            (seenStamp, seenLength) = (stamp, size);
            if (text == seen) return false;
            seen = text;
            return true;
        }
    }

    // The host's Local Stats file and mod content values for the in-game editor (null: none, the files go), each written
    // whole (the editor reads them each second and skips one caught mid-write).
    void WriteHost(string? stats, string? mods, string? mounts)
    {
        Write(hostStatsFile, stats, "Local Stats");
        Write(hostModValuesFile, mods, "mod content values");
        Write(hostMountsFile, mounts, "arm swaps");
        void Write(string path, string? text, string what)
        {
            try
            {
                if (text == null) { if (File.Exists(path)) File.Delete(path); return; }
                if (!Directory.Exists(Path.GetDirectoryName(path))) return;
                var partial = path + ".partial";
                File.WriteAllText(partial, text);
                File.Move(partial, path, overwrite: true);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { log($"Stats: the host's {what} could not be written: " + error.Message); }
        }
    }

    // A friend's backend: the host's stats, accepted only from the friend whose lobby this machine joined.
    public void FromPeer(ulong peer, Frame frame)
    {
        if (frame.Type != "stats.set" || !lobby.HostsJoinedLobby(peer)) return;
        string text, mods, mounts;
        try
        {
            text = StatsSync.Unpack(frame.Bytes(1));
            mods = frame.Count > 2 ? StatsSync.Unpack(frame.Bytes(2)) : ""; /* a host from before mod content values: none */
            mounts = frame.Count > 3 ? StatsSync.Unpack(frame.Bytes(3)) : ""; /* a host from before arm swaps: none */
        }
        catch (Exception error) when (error is InvalidDataException or IOException) { log("Stats: the host's stats could not be read: " + error.Message); return; }
        if (!long.TryParse(frame.Text(0), System.Globalization.NumberStyles.None, System.Globalization.CultureInfo.InvariantCulture, out var received)) return;
        lock (gate)
        {
            if (peer == hostPeer && received <= hostVersion) return;
            (hostPeer, hostVersion, hostStats, hostMods, hostMounts) = (peer, received, text, mods, mounts);
            var name = nameOf(peer);
            if (role != "joiner" || roleHost != name) (role, roleHost, sessionSent) = ("joiner", name, false);
        }
        log($"Stats: the host's Local Stats, version {received}, from {LobbyService.EntityId(peer)}.");
        WriteHost(text, mods, mounts);
        _ = ApplyAsync();
    }

    // A message for the player, which the in-game editor shows over the game (a refused join: MultiplayerHost); kind
    // "error" or "info". Not kept: a game that is not connected does not get it.
    public void Notice(string kind, string title, IReadOnlyList<string> lines)
    {
        if (!live.Connected || live.Mismatched) { log($"Notice not shown, the game is not connected: {title}"); return; }
        _ = Task.Run(async () =>
        {
            var result = await live.NoticeAsync(kind, title, lines);
            log(result.Ok ? $"Notice shown in the game: {title}" : $"Notice not shown ({result.Detail}): {title}");
        });
    }

    // Takes the game to the host's values (joined) or its own, one request at a time; a request the game refused is
    // logged and tried again with the next change.
    async Task ApplyAsync()
    {
        if (!live.Connected || live.Mismatched) return;
        await applying.WaitAsync();
        try
        {
            string? host, own;
            string sessionRole, sessionHost;
            lock (gate) (host, own, sessionRole, sessionHost) = (hostPeer != 0 ? hostStats ?? "" : null, mine, role, roleHost);
            bool sendSession;
            lock (gate) sendSession = !sessionSent;
            if (sendSession)
            {
                var session = await live.SessionAsync(sessionRole, sessionHost);
                lock (gate) if (session.Ok && sessionRole == role && sessionHost == roleHost) sessionSent = true;
            }
            var requests = sync.Plan(host, own);
            int done = 0, refused = 0;
            foreach (var request in requests)
            {
                var result = request.Verb switch
                {
                    "set" => await live.SetAsync(request.Field, request.Bits, request.Field.Shared),
                    "base" => await live.BaseAsync(request.Field),
                    _ => await live.ResetAsync(request.Field),
                };
                if (result.Ok) { sync.Done(request, host != null); done++; }
                else if (refused++ < 5) log($"Stats: {request.Verb} {request.Field.Name} refused: {result.Detail}");
            }
            if (requests.Count > 0)
                log($"Stats: {(host != null ? "the host's values" : "this game's own values")}: {done} applied, {refused} refused" +
                    (host != null && sync.BootOnly > 0 ? $", {sync.BootOnly} apply only from a session start (attachments, status multipliers)" : "") + ".");
        }
        catch (Exception error) { log("Stats: applying failed: " + error.Message); }
        finally { applying.Release(); }
    }

    public async ValueTask DisposeAsync()
    {
        await timer.DisposeAsync();
        await live.DisposeAsync();
        applying.Dispose();
    }
}

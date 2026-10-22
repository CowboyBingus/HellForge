using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Channels;
using HellForge.Core;

namespace HellForge.LocalBackend.Multiplayer;

// Mod sync (HellForge.Core.ModSync; docs/multiplayer.md "Mod sync"): a joiner who lacks the host's gameplay mods can
// download them and play with the host's set.
//   host:   a joiner refused for different gameplay mods (LobbyService.ModsRefused) is sent this session's mod set
//           (without its folders) just before the refusal, mods.offer [lobby id, set, sharing 0|1], and its machine may
//           download for GrantTime. The host serves only the set's counted files, from one worker, at most PeerRate per
//           joiner and TotalRate in all, so its own game keeps its link:
//             mods.files [req, id, hash]                        -> mods.filelist [req, "ok", count, (path, size, sha256)...]
//             mods.get   [req, id, hash, path, offset, length]  -> mods.chunk [req, "ok", offset, bytes]
//           or [req, "refused", reason]. The player can turn sharing off (ModSettings.Share).
//   joiner: a mod offer for a join this machine asked for (Expected: LobbyService.JoinedRecently) becomes an offer for
//           the game (ModSyncPlan; HfApi sync_state, the in-game editor's MOD
//           SYNC view). On accept it downloads the plan's mods into the synced store, every byte checked (SyncedStore). On
//           restart it writes the next session's mods (ModSyncSession) and stops this session ("mod-sync" in
//           stop-observation.signal), which the launcher starts again with them.
public sealed class ModSync : IAsyncDisposable
{
    public const int ChunkBytes = 128 << 10, Window = 4, QueuedPerPeer = 8;
    public const long PeerRate = 1 << 20, TotalRate = 2 << 20;
    public static readonly TimeSpan GrantTime = TimeSpan.FromMinutes(10), RequestTimeout = TimeSpan.FromSeconds(30);
    public const string StopSignal = "stop-observation.signal", StopReason = "mod-sync", OfferFile = "mod-sync.json";

    readonly IPeerTransport transport;
    readonly string modsFolder;
    // This session's mod set (what joiners may download) and its wire form, swapped whole when a live change of the
    // session's mods replaces it (SetOwn).
    sealed record Owned(ModSet? Set, string? Wire);
    volatile Owned owned;
    ModSet? own => owned.Set;
    readonly string runDirectory, evidenceDirectory, sessionFile;
    readonly Func<ulong, string> nameOf;
    readonly Action<string> log;
    readonly Func<string, string, bool> verified;
    readonly CancellationTokenSource stop = new();
    readonly object gate = new();

    public ModSync(IPeerTransport transport, string modsFolder, ModSet? own, string runDirectory, string evidenceDirectory, string sessionFile,
        Func<ulong, string> nameOf, Action<string> log, Func<string, string, bool>? verified = null)
    {
        this.transport = transport;
        this.modsFolder = modsFolder;
        owned = new Owned(own, own?.ToJson(local: false).ToJsonString());
        this.runDirectory = runDirectory;
        this.evidenceDirectory = evidenceDirectory;
        this.sessionFile = sessionFile;
        this.nameOf = nameOf;
        this.log = log;
        this.verified = verified ?? VerifiedMods.Contains;
        serving = Task.Run(ServeAsync);
    }

    // Where the offer is shown: the game (LiveSyncDisplay) or a test.
    public ISyncDisplay? Display { get; set; }

    public async ValueTask DisposeAsync()
    {
        lock (gate) downloading?.Cancel();
        stop.Cancel();
        requests.Writer.TryComplete();
        try { await serving; }
        catch (OperationCanceledException) { }
        stop.Dispose();
    }

    // A frame from a peer's backend.
    public void FromPeer(ulong peer, Frame frame)
    {
        try
        {
            switch (frame.Type)
            {
                case "mods.files" or "mods.get": Queue(peer, frame); break;
                case "mods.offer": Offered(peer, frame); break;
                case "mods.filelist" or "mods.chunk": Answered(peer, frame); break;
            }
        }
        catch (InvalidDataException error) { log($"Mod sync: bad {frame.Type} from {LobbyService.EntityId(peer)}: {error.Message}"); }
    }

    // ---- Host ---------------------------------------------------------------------------------------------------------

    readonly Dictionary<ulong, DateTime> grants = [];
    readonly Dictionary<ulong, int> queued = [];
    readonly Channel<(ulong Peer, Frame Frame)> requests = Channel.CreateUnbounded<(ulong, Frame)>(new UnboundedChannelOptions { SingleReader = true });
    readonly Dictionary<ulong, long> peerNext = [];
    readonly Task serving;
    long totalNext;

    // A joiner with other gameplay mods is about to be refused (LobbyService.ModsRefused): this session's mod set goes to
    // them first, and they may download it for GrantTime. True when they may (this player shares, and the set is known).
    public bool OfferMods(ulong joiner, string lobbyId)
    {
        var wire = owned.Wire;
        if (wire == null) return false;
        var share = Shares();
        if (share) lock (gate) grants[joiner] = DateTime.UtcNow + GrantTime;
        Send(joiner, new Frame("mods.offer").Add(lobbyId).Add(wire).Add(share ? "1" : "0"));
        log($"Mod sync: {LobbyService.EntityId(joiner)} lacks this session's mods; {(share ? "they may download them for " + GrantTime.TotalMinutes + " minutes" : "sharing is off")}.");
        return share;
    }

    bool Shares() => ModSettings.Read(modsFolder).Share;

    // ---- The lobby directory (DirectoryService) --------------------------------------------------------------------

    // This session's mod set as a lobby listing carries it (the wire form), its digest, and whether this player shares it.
    public string? OwnSetWire => owned.Wire;

    // The session's mods changed while it runs (LiveMods): joiners are offered, and may download, the new set.
    public void SetOwn(ModSet set) => owned = new Owned(set, set.ToJson(local: false).ToJsonString());
    public string OwnDigest => own?.Digest ?? "none";
    public bool Sharing => Shares();

    ModLocator? locator;
    ModLocator Locator { get { lock (gate) return locator ??= new ModLocator(modsFolder); } }

    // Whether a listed lobby could be joined: the same mods, or a plan that is not blocked or unjoinable (mods to install
    // by hand, another HellForge), and edited stats only when the player shows such lobbies. A listing without a mod set
    // is shown: the join's refusal then brings it.
    public bool Joinable(DirectoryListing listing, out string why)
    {
        why = "";
        var settings = ModSettings.Read(modsFolder);
        if (listing.Data.GetValueOrDefault(DirectoryKeys.Stats) == "1" && !settings.ShowEditedStats) { why = "its host plays with edited stats"; return false; }
        if (listing.Data.GetValueOrDefault(DirectoryKeys.Digest) == OwnDigest) return true;
        if (DirectoryData.Unpack(DirectoryData.Get(listing.Data, DirectoryKeys.Mods)) is not { } wire) return true;
        ModSet set;
        try { set = ModSet.Parse(wire, local: false); }
        catch (InvalidDataException) { why = "its mod list is malformed"; return false; }
        var plan = ModSyncPlan.Make(set, listing.Data.GetValueOrDefault(DirectoryKeys.Share) == "1", own ?? new ModSet("none", []), Locator.Find, settings.Blocked, verified);
        why = plan.Verdict switch { SyncVerdict.Blocked => "it runs a blocked mod", SyncVerdict.Unjoinable => "it runs mods that cannot be downloaded", _ => "" };
        return plan.Verdict is SyncVerdict.Ready or SyncVerdict.Download;
    }

    // A machine this one downloads from or lets download (a stranger is let through for that: MultiplayerHost's gate).
    public bool Related(ulong peer)
    {
        lock (gate) return grants.TryGetValue(peer, out var until) && until > DateTime.UtcNow || offer?.Host == peer;
    }

    void Queue(ulong peer, Frame frame)
    {
        lock (gate)
        {
            var count = queued.GetValueOrDefault(peer);
            if (count >= QueuedPerPeer) { Send(peer, Refused(frame, "too many requests at once")); return; }
            queued[peer] = count + 1;
        }
        if (!requests.Writer.TryWrite((peer, frame))) lock (gate) queued[peer]--;
    }

    async Task ServeAsync()
    {
        await foreach (var (peer, frame) in requests.Reader.ReadAllAsync(stop.Token))
        {
            lock (gate) queued[peer] = Math.Max(0, queued.GetValueOrDefault(peer) - 1);
            Frame reply;
            try { reply = Serve(peer, frame); }
            catch (Exception error) when (error is InvalidDataException or IOException or UnauthorizedAccessException)
            {
                reply = Refused(frame, error is InvalidDataException ? error.Message : "the host could not read the file");
                log($"Mod sync: a request from {LobbyService.EntityId(peer)} failed: {error.Message}");
            }
            if (frame.Type == "mods.get" && reply.Text(1) == "ok") await PaceAsync(peer, reply.Bytes(3).Length);
            Send(peer, reply);
        }
    }

    // Waits until both this joiner's and the total budget allow length more bytes.
    async Task PaceAsync(ulong peer, int length)
    {
        var now = Stopwatch.GetTimestamp();
        var start = Math.Max(now, Math.Max(peerNext.GetValueOrDefault(peer), totalNext));
        peerNext[peer] = start + length * Stopwatch.Frequency / PeerRate;
        totalNext = Math.Max(now, totalNext) + length * Stopwatch.Frequency / TotalRate;
        if (start > now) await Task.Delay(TimeSpan.FromSeconds((double)(start - now) / Stopwatch.Frequency), stop.Token);
    }

    // One request, answered from this session's mod set: only a mod in it, only a file it counts, only while granted.
    Frame Serve(ulong peer, Frame frame)
    {
        bool granted;
        lock (gate)
        {
            granted = grants.TryGetValue(peer, out var until) && until > DateTime.UtcNow;
            if (granted) grants[peer] = until > DateTime.UtcNow + TimeSpan.FromMinutes(2) ? until : DateTime.UtcNow + TimeSpan.FromMinutes(2);
        }
        if (!granted) return Refused(frame, "no download was offered to this player");
        if (!Shares()) return Refused(frame, "the host does not share their mods");
        var mod = own?.Find(frame.Text(1), frame.Text(2));
        if (mod == null || mod.Class == ModClass.Bundled || mod.Folder == null) return Refused(frame, "not one of the host's mods");
        if (frame.Type == "mods.files")
        {
            var list = new Frame("mods.filelist").Add(frame.Text(0)).Add("ok").Add(mod.Files.Count);
            foreach (var file in mod.Files) list.Add(file.Path).Add(file.Size).Add(file.Sha256);
            return list;
        }
        var path = frame.Text(3);
        var listed = mod.Files.FirstOrDefault(f => f.Path == path);
        if (listed == null || !long.TryParse(frame.Text(4), NumberStyles.None, CultureInfo.InvariantCulture, out var offset) ||
            !int.TryParse(frame.Text(5), NumberStyles.None, CultureInfo.InvariantCulture, out var length) ||
            length is < 1 or > ChunkBytes || offset > listed.Size - length)
            return Refused(frame, "not a part of the mod's files");
        var full = Path.GetFullPath(Path.Combine(mod.Folder, path));
        if (!full.StartsWith(Path.GetFullPath(mod.Folder) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            return Refused(frame, "not a part of the mod's files");
        using var stream = new FileStream(full, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        if (stream.Length != listed.Size) return Refused(frame, "the host's copy of the mod changed since the session started");
        var bytes = new byte[length];
        stream.Seek(offset, SeekOrigin.Begin);
        stream.ReadExactly(bytes);
        return new Frame("mods.chunk").Add(frame.Text(0)).Add("ok").Add(offset).Add(bytes);
    }

    static Frame Refused(Frame request, string reason) =>
        new Frame(request.Type == "mods.files" ? "mods.filelist" : "mods.chunk").Add(request.Count > 0 ? request.Text(0) : "").Add("refused").Add(reason);

    void Send(ulong peer, Frame frame) => transport.Send(peer, frame.Encode(), reliable: true);

    // ---- Joiner -------------------------------------------------------------------------------------------------------

    sealed class Offer(int version, ulong host, string hostName, string connection, ModSet set, bool shares, ModSyncPlan plan)
    {
        public int Version { get; } = version;
        public ulong Host { get; } = host;
        public string HostName { get; } = hostName;
        public string Connection { get; } = connection;
        public ModSet Set { get; } = set;
        public bool Shares { get; } = shares;
        public ModSyncPlan Plan { get; } = plan;
        public string State = "offer", Message = "";
        public long Done;
        public bool Live; // the running game can take the host's mods: "restart" changes them without a restart
    }

    Offer? offer;
    int versions;
    CancellationTokenSource? downloading;
    readonly Dictionary<string, (ulong Peer, TaskCompletionSource<Frame> Reply)> waiting = new(StringComparer.Ordinal);
    long requestIds;

    // Whether this machine asked that host to join that lobby lately (LobbyService.JoinedRecently): only then is its mod
    // offer taken.
    public Func<ulong, string, bool>? Expected { get; set; }

    // The running game taking the host's mods without a restart (LiveMods, set by the backend): whether it can, and doing it
    // (null when done, else why not; the session then restarts as before).
    public Func<ModSyncSession, bool>? CanApplyLive { get; set; }
    public Func<ModSyncSession, Task<string?>>? ApplyLive { get; set; }

    // The session that plays a host's mods: the next one (a restart), or this one changed live.
    static ModSyncSession SessionOf(ulong host, string hostName, string connection, ModSet set) =>
        new(LobbyService.EntityId(host), hostName, connection, set.Digest, [.. set.Mods.Select(m => new ModSyncSession.Mod(m.Id, m.Hash, m.Order))]);

    bool LiveFor(ulong host, string hostName, string connection, ModSet set)
    {
        try { return CanApplyLive?.Invoke(SessionOf(host, hostName, connection, set)) == true; }
        catch (Exception error) when (error is InvalidDataException or IOException or UnauthorizedAccessException or System.Text.Json.JsonException)
        {
            log("Mod sync: whether the running game can take the host's mods could not be worked out: " + error.Message);
            return false;
        }
    }

    // A host about to refuse this machine's join sent its mod set: an offer (worked out off the transport's thread: it may
    // hash installed mods).
    void Offered(ulong host, Frame frame)
    {
        var lobbyId = frame.Text(0);
        if (Expected?.Invoke(host, lobbyId) != true) { log($"Mod sync: dropped a mod offer from {LobbyService.EntityId(host)} for a join not asked for."); return; }
        var connection = LobbyService.ConnectionPrefix + lobbyId + ";" + LobbyService.EntityId(host);
        var (set, shares) = (frame.Text(1), frame.Text(2) == "1");
        _ = Task.Run(() =>
        {
            try { MakeOffer(host, connection, ModSet.Parse(set, local: false), shares); }
            catch (Exception error) when (error is InvalidDataException or IOException or UnauthorizedAccessException)
            {
                log("Mod sync: the host's mod list could not be used: " + error.Message);
            }
            catch (Exception error) { log("Mod sync: making the offer failed: " + error); }
        });
    }

    void MakeOffer(ulong host, string connection, ModSet set, bool shares)
    {
        var plan = Plan(set, shares);
        var live = plan.Verdict == SyncVerdict.Ready && LiveFor(host, nameOf(host), connection, set);
        Offer made;
        lock (gate)
        {
            downloading?.Cancel();
            downloading = null;
            made = offer = new Offer(++versions, host, nameOf(host), connection, set, shares, plan) { Live = live };
            (made.State, made.Message) = Describe(made);
        }
        log($"Mod sync: {made.HostName}'s lobby: {plan.Verdict}, " + string.Join(", ", plan.Items.Select(i => $"{i.Mod.Id} {ModSyncPlan.ActionName(i.Action)}")) + ".");
        _ = PublishAsync(made, withMods: true);
    }

    ModSyncPlan Plan(ModSet set, bool shares) =>
        ModSyncPlan.Make(set, shares, own ?? new ModSet("none", []), new ModLocator(modsFolder).Find, ModSettings.Read(modsFolder).Blocked, verified);

    static (string State, string Message) Describe(Offer o)
    {
        var plan = o.Plan;
        string Names(params SyncAction[] actions) => string.Join(", ", plan.Items.Where(i => actions.Contains(i.Action)).Select(i => i.Mod.Name));
        return plan.Verdict switch
        {
            SyncVerdict.Blocked => ("blocked", $"This lobby runs mods you blocked: {Names(SyncAction.Blocked)}."),
            SyncVerdict.Unjoinable when plan.Items.Any(i => i.Action == SyncAction.Update) =>
                ("blocked", $"{o.HostName} runs another HellForge version ({Names(SyncAction.Update)} differs): both players need the same HellForge."),
            SyncVerdict.Unjoinable when !o.Shares => ("blocked", $"{o.HostName} does not share their mods. Install them yourself to join: {Names(SyncAction.Manual)}."),
            SyncVerdict.Unjoinable => ("blocked", $"Some of {o.HostName}'s mods run code HellForge has not verified. Install them yourself to join: {Names(SyncAction.Manual)}."),
            SyncVerdict.Download => ("offer", $"{o.HostName} plays with {plan.Items.Count(i => i.Action == SyncAction.Download)} mod(s) you don't have ({Megabytes(plan.DownloadBytes)}). Download them and restart with {o.HostName}'s mods to join."),
            _ => ("offer", o.Live ? $"You have all of {o.HostName}'s mods. Play with them now, without a restart, to join."
                : $"You have all of {o.HostName}'s mods. Restart with them to join.")
        };
    }

    static string Megabytes(long bytes) => (bytes / 1048576.0).ToString(bytes < 10 << 20 ? "0.0" : "0", CultureInfo.InvariantCulture) + " MB";

    // An answer from the game (HfApi sync_answer): "syncanswer" version accept|decline|restart|block [mod id].
    public void Answer(IReadOnlyList<string> parts)
    {
        if (parts.Count < 3 || parts[0] != "syncanswer" || !int.TryParse(parts[1], NumberStyles.None, CultureInfo.InvariantCulture, out var version)) return;
        Offer? current;
        lock (gate) current = offer?.Version == version ? offer : null;
        if (current == null) { log($"Mod sync: an answer ({parts[2]}) to an offer that is gone."); return; }
        switch (parts[2])
        {
            case "decline": Close(current); break;
            case "accept" when current.State == "offer": Accept(current); break;
            case "restart" when current.State is "ready" || current.State == "offer" && current.Plan.Verdict == SyncVerdict.Ready: _ = Task.Run(() => RestartAsync(current)); break;
            case "block" when parts.Count > 3 && current.Set.Mods.Any(m => m.Id == parts[3]):
                ModSettings.SetBlocked(modsFolder, parts[3], true);
                log($"Mod sync: {parts[3]} is blocked; lobbies running it are not offered again.");
                _ = Task.Run(() => MakeOffer(current.Host, current.Connection, current.Set, current.Shares));
                break;
            default: log($"Mod sync: the answer {parts[2]} does not fit the offer's state ({current.State})."); break;
        }
    }

    void Close(Offer current)
    {
        Offer closed;
        lock (gate)
        {
            if (offer != current) return;
            downloading?.Cancel();
            downloading = null;
            offer = null;
            closed = new Offer(++versions, current.Host, current.HostName, current.Connection, new ModSet("none", []), false, new ModSyncPlan([])) { State = "none" };
        }
        log("Mod sync: the offer was closed.");
        _ = PublishAsync(closed, withMods: true);
    }

    void Accept(Offer current)
    {
        if (current.Plan.Verdict == SyncVerdict.Ready) { _ = Task.Run(() => RestartAsync(current)); return; }
        if (current.Plan.Verdict != SyncVerdict.Download) return;
        CancellationTokenSource token;
        lock (gate)
        {
            if (offer != current || downloading != null) return;
            token = downloading = CancellationTokenSource.CreateLinkedTokenSource(stop.Token);
            (current.State, current.Message) = ("downloading", $"Downloading {current.HostName}'s mods...");
        }
        log($"Mod sync: downloading {Megabytes(current.Plan.DownloadBytes)} from {current.HostName}.");
        _ = PublishAsync(current, withMods: false);
        _ = Task.Run(() => DownloadAsync(current, token.Token));
    }

    async Task DownloadAsync(Offer current, CancellationToken token)
    {
        try
        {
            foreach (var item in current.Plan.Items.Where(i => i.Action == SyncAction.Download)) await DownloadModAsync(current, item.Mod, token);
            current.Live = LiveFor(current.Host, current.HostName, current.Connection, current.Set);
            Update(current, "ready", current.Live ? $"Everything is here. Play with {current.HostName}'s mods now, without a restart, to join."
                : $"Everything is here. Restart with {current.HostName}'s mods to join.");
            log($"Mod sync: every mod is here ({Megabytes(current.Done)}).");
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { log("Mod sync: the download was cancelled."); }
        catch (Exception error) when (error is InvalidDataException or IOException or UnauthorizedAccessException or TimeoutException or JsonException)
        {
            log("Mod sync: the download failed: " + error.Message);
            Update(current, "failed", "The download failed: " + error.Message);
        }
    }

    async Task DownloadModAsync(Offer current, ModSetEntry mod, CancellationToken token)
    {
        var files = FileList(await RequestAsync(current.Host, new Frame("mods.files").Add(mod.Id).Add(mod.Hash), token), mod);
        var partial = SyncedStore.Partial(modsFolder, mod.Id, mod.Hash);
        try
        {
            // The manifest first: a mod that declares code and is not verified stops before its bytes.
            var manifest = files.First(f => f.Path is ModCatalog.ManifestName or ModCatalog.LegacyManifestName);
            await DownloadFileAsync(current, mod, manifest, partial, token);
            if (DeclaresCode(SyncedStore.Target(partial, manifest.Path)) && !verified(mod.Id, mod.Hash))
                throw new InvalidDataException($"{mod.Name} runs code and is not on HellForge's verified list.");
            foreach (var file in files.Where(f => f != manifest)) await DownloadFileAsync(current, mod, file, partial, token);
        }
        catch
        {
            try { Directory.Delete(partial, recursive: true); }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
            throw;
        }
        var folder = await Task.Run(() => SyncedStore.Install(modsFolder, mod, partial, verified), token);
        log($"Mod sync: {mod.Id} {mod.Version} checked and stored in {folder}.");
    }

    // The host's file list for a mod: safe paths, each once, sizes adding up to the mod set's bytes, a manifest.
    static List<ModFile> FileList(Frame reply, ModSetEntry mod)
    {
        if (reply.Text(1) != "ok") throw new InvalidDataException($"{mod.Name}: the host refused ({(reply.Count > 2 ? reply.Text(2) : "no reason")}).");
        var count = reply.Int(2);
        if (count is < 1 or > SyncedStore.MaxFiles || reply.Count != 3 + 3 * count) throw new InvalidDataException($"{mod.Name}: the host's file list is malformed.");
        var files = new List<ModFile>();
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < count; i++)
        {
            var file = new ModFile(reply.Text(3 + 3 * i), long.TryParse(reply.Text(4 + 3 * i), NumberStyles.None, CultureInfo.InvariantCulture, out var size) ? size : -1,
                reply.Text(5 + 3 * i));
            if (!SyncedStore.SafePath(file.Path) || !paths.Add(file.Path) || file.Size < 0 || file.Sha256.Length != 64 || !file.Sha256.All(char.IsAsciiHexDigitUpper))
                throw new InvalidDataException($"{mod.Name}: the host listed a file that cannot be taken.");
            files.Add(file);
        }
        if (files.Sum(f => f.Size) != mod.Bytes) throw new InvalidDataException($"{mod.Name}: the host's files are not the size its mod list says.");
        if (!files.Any(f => f.Path is ModCatalog.ManifestName or ModCatalog.LegacyManifestName)) throw new InvalidDataException($"{mod.Name}: the host listed no manifest.");
        return files;
    }

    static bool DeclaresCode(string manifest)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(manifest));
        return document.RootElement.ValueKind == JsonValueKind.Object && document.RootElement.TryGetProperty("code", out _);
    }

    // One file, Window chunks in flight, written and hashed in order.
    async Task DownloadFileAsync(Offer current, ModSetEntry mod, ModFile file, string partial, CancellationToken token)
    {
        var target = SyncedStore.Target(partial, file.Path);
        Directory.CreateDirectory(Path.GetDirectoryName(target)!);
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        await using (var output = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None))
        {
            var chunks = new Queue<(long Offset, int Length, Task<Frame> Reply)>();
            long next = 0;
            while (next < file.Size || chunks.Count > 0)
            {
                while (chunks.Count < Window && next < file.Size)
                {
                    var length = (int)Math.Min(ChunkBytes, file.Size - next);
                    chunks.Enqueue((next, length, RequestAsync(current.Host, new Frame("mods.get").Add(mod.Id).Add(mod.Hash).Add(file.Path).Add(next).Add(length), token)));
                    next += length;
                }
                var (offset, expected, pending) = chunks.Dequeue();
                var reply = await pending;
                if (reply.Text(1) != "ok") throw new InvalidDataException($"{mod.Name}: the host refused ({(reply.Count > 2 ? reply.Text(2) : "no reason")}).");
                var bytes = reply.Bytes(3);
                if (reply.Text(2) != offset.ToString(CultureInfo.InvariantCulture) || bytes.Length != expected)
                    throw new InvalidDataException($"{mod.Name}: the host sent the wrong part of {file.Path}.");
                await output.WriteAsync(bytes, token);
                hash.AppendData(bytes);
                Progress(current, bytes.Length);
            }
        }
        if (Convert.ToHexString(hash.GetHashAndReset()) != file.Sha256) throw new InvalidDataException($"{mod.Name}: {file.Path} is not the file the host listed.");
    }

    async Task<Frame> RequestAsync(ulong host, Frame request, CancellationToken token)
    {
        var id = "s" + Interlocked.Increment(ref requestIds).ToString(CultureInfo.InvariantCulture);
        var reply = new TaskCompletionSource<Frame>(TaskCreationOptions.RunContinuationsAsynchronously);
        var frame = new Frame(request.Type).Add(id);
        frame.Fields.AddRange(request.Fields);
        lock (gate) waiting[id] = (host, reply);
        try
        {
            Send(host, frame);
            return await reply.Task.WaitAsync(RequestTimeout, token);
        }
        catch (TimeoutException) { throw new TimeoutException("the host stopped answering."); }
        finally { lock (gate) waiting.Remove(id); }
    }

    void Answered(ulong peer, Frame frame)
    {
        TaskCompletionSource<Frame>? reply = null;
        lock (gate)
            if (waiting.TryGetValue(frame.Text(0), out var entry) && entry.Peer == peer) reply = entry.Reply;
        reply?.TrySetResult(frame);
    }

    readonly Stopwatch progressClock = Stopwatch.StartNew();
    long progressShown;

    // Downloaded bytes: the game sees them at most four times a second.
    void Progress(Offer current, int bytes)
    {
        bool show;
        lock (gate)
        {
            current.Done += bytes;
            show = progressClock.ElapsedMilliseconds - progressShown >= 250;
            if (show) progressShown = progressClock.ElapsedMilliseconds;
        }
        if (show) _ = PublishAsync(current, withMods: false);
    }

    void Update(Offer current, string state, string message)
    {
        lock (gate)
        {
            if (offer != current) return;
            (current.State, current.Message) = (state, message);
            if (state != "downloading") downloading = null;
        }
        _ = PublishAsync(current, withMods: false);
    }

    // Everything is here: the running game takes the host's mods when it can (LiveMods), else the next session's mods are
    // written and this session stops for the launcher to start it with them. Off the pipe's thread: it hashes the mods it
    // finds again.
    async Task RestartAsync(Offer current)
    {
        if (current.Live && ApplyLive != null)
        {
            Update(current, "ready", $"Changing to {current.HostName}'s mods...");
            var why = await ApplyLive(SessionOf(current.Host, current.HostName, current.Connection, current.Set));
            if (why == null)
            {
                log($"Mod sync: the running game plays {current.HostName}'s mods now; join their lobby again.");
                Close(current);
                return;
            }
            log($"Mod sync: the running game could not take {current.HostName}'s mods ({why}); the session restarts with them instead.");
        }
        try { WriteRestart(current); }
        catch (Exception error) when (error is InvalidDataException or IOException or UnauthorizedAccessException)
        {
            log("Mod sync: could not restart: " + error.Message);
            Update(current, "failed", "Could not restart with the host's mods: " + error.Message);
        }
    }

    void WriteRestart(Offer current)
    {
        var plan = Plan(current.Set, current.Shares);
        if (plan.Verdict != SyncVerdict.Ready)
        {
            Update(current, "failed", "Some of the host's mods are no longer here: " + string.Join(", ", plan.Items.Where(i => i.Action != SyncAction.Have && i.Action != SyncAction.Off).Select(i => i.Mod.Name)) + ".");
            return;
        }
        SessionOf(current.Host, current.HostName, current.Connection, current.Set).Write(sessionFile);
        Update(current, "ready", $"Restarting with {current.HostName}'s mods...");
        File.WriteAllText(Path.Combine(runDirectory, StopSignal), StopReason);
        log($"Mod sync: the next session plays {current.HostName}'s mods ({sessionFile}); this session stops for the launcher to restart it.");
    }

    // The offer as the game and the session's logs see it.
    async Task PublishAsync(Offer current, bool withMods)
    {
        SyncView view;
        lock (gate) view = new SyncView(current.Version, current.State, current.HostName, current.Plan.DownloadBytes, current.Done, current.Message, current.Plan.Items,
            current.Live);
        try
        {
            File.WriteAllText(Path.Combine(evidenceDirectory, OfferFile), new JsonObject
            {
                ["version"] = view.Version, ["state"] = view.State, ["host"] = view.Host, ["connection"] = current.Connection, ["bytes"] = view.Bytes, ["done"] = view.Done,
                ["message"] = view.Message, ["live"] = view.Live,
                ["mods"] = new JsonArray([.. view.Items.Select(i => (JsonNode?)new JsonObject
                {
                    ["id"] = i.Mod.Id, ["name"] = i.Mod.Name, ["version"] = i.Mod.Version, ["class"] = ModSets.ClassName(i.Mod.Class),
                    ["action"] = ModSyncPlan.ActionName(i.Action), ["bytes"] = i.Mod.Bytes
                })])
            }.ToJsonString());
        }
        catch (IOException error) { log("Mod sync: could not write the offer file: " + error.Message); }
        if (Display != null && !await Display.ShowAsync(view, withMods)) log("Mod sync: the game did not take the offer (not connected yet?); it is shown when it connects.");
    }

    // The game connected again (a new pipe): the current offer, whole.
    public void Republish()
    {
        Offer? current;
        lock (gate) current = offer;
        if (current != null) _ = PublishAsync(current, withMods: true);
    }
}

// An offer as the game shows it.
// Live: answering "restart" changes the running game's mods (HF_SYNC_LIVE).
public sealed record SyncView(int Version, string State, string Host, long Bytes, long Done, string Message, IReadOnlyList<SyncItem> Items, bool Live = false);

public interface ISyncDisplay
{
    // withMods: the offer's mods first (a new version); otherwise only its state (progress). False when the game did not take it.
    Task<bool> ShowAsync(SyncView view, bool withMods);
}

// The game, over the session's live-edit pipe (native/mod-loader/live.c: "syncmod" lines, then "syncstate").
public sealed class LiveSyncDisplay(LiveEditServer live) : ISyncDisplay
{
    readonly SemaphoreSlim order = new(1, 1);

    public async Task<bool> ShowAsync(SyncView view, bool withMods)
    {
        await order.WaitAsync();
        try
        {
            if (withMods)
                for (var i = 0; i < view.Items.Count; i++)
                {
                    var item = view.Items[i];
                    if (!(await live.SyncModAsync(view.Version, i, ModSyncPlan.ActionName(item.Action), ModSets.ClassName(item.Mod.Class), item.Mod.Bytes, item.Mod.Id,
                            item.Mod.Name, item.Mod.Version)).Ok) return false;
                }
            return (await live.SyncStateAsync(view.Version, view.State, view.Host, view.Bytes, view.Done, view.Items.Count, view.Live, view.Message)).Ok;
        }
        finally { order.Release(); }
    }
}

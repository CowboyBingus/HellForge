using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Channels;
using HellForge.Core;
using HellForge.Core.GameData;

namespace HellForge.LocalBackend.Multiplayer;

// Mod sync (HellForge.Core.ModSync; docs/multiplayer.md "Mod sync"): a joiner who lacks the host's gameplay mods can
// download them and play with the host's set.
//   host:   a joiner refused for different gameplay mods (LobbyService.ModsRefused) is sent this session's mod set
//           (without its folders) just before the refusal, mods.offer [lobby id, set, sharing 0|1], and its machine may
//           download for GrantTime from the first such offer (a later refusal does not extend it), at most the grant's
//           byte budget (GrantBudget). The host serves only the set's counted files, from one worker, at most PeerRate
//           per joiner and TotalRate in all, so its own game keeps its link; code and raw mods' files only to its Steam
//           friends (a stranger could not take them unverified anyway, and they may be a modder's unreleased sources):
//             mods.files [req, id, hash, start]                 -> mods.filelist [req, "ok", total, start, (path, size, sha256)...]
//                                                                  (at most PageFiles files a page)
//             mods.get   [req, id, hash, path, offset, length]  -> mods.chunk [req, "ok", offset, bytes]
//           or [req, "refused", reason]. The player can turn sharing off (ModSettings.Share).
//   joiner: a mod offer for a join this machine asked for (Expected: LobbyService.JoinedRecently) becomes an offer for
//           the game (ModSyncPlan; HfApi sync_state, the in-game editor's MOD
//           SYNC view). A Steam friend's code or raw mod HellForge has not verified waits for the player's trust (TRUST on
//           the prompt: that exact content goes on ModSettings' Trusted list), and GET IT opens a mod's web address in
//           the player's browser. On accept it downloads the plan's mods into the synced store, every byte checked (SyncedStore). On
//           restart it writes the next session's mods (ModSyncSession) and stops this session ("mod-sync" in
//           stop-observation.signal), which the launcher starts again with them.
public sealed class ModSync : IAsyncDisposable
{
    // PageFiles: a file list page's files (3 fields each, paths of at most 240 characters) stay within a frame's 4096
    // fields and 1 MiB.
    public const int ChunkBytes = 128 << 10, Window = 4, QueuedPerPeer = 8, PageFiles = 1000;
    public const long PeerRate = 1 << 20, TotalRate = 2 << 20;
    // Free space a download leaves on the drive besides its own bytes.
    public const long SpareDisk = 256L << 20;
    public static readonly TimeSpan GrantTime = TimeSpan.FromMinutes(10), RequestTimeout = TimeSpan.FromSeconds(30);
    public const string StopSignal = "stop-observation.signal", StopReason = "mod-sync", OfferFile = "mod-sync.json";

    readonly IPeerTransport transport;
    readonly string modsFolder;
    readonly ModSet? own;
    readonly string? ownWire;
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
        this.own = own;
        ownWire = own?.ToJson(local: false).ToJsonString();
        this.runDirectory = runDirectory;
        this.evidenceDirectory = evidenceDirectory;
        this.sessionFile = sessionFile;
        this.nameOf = nameOf;
        this.log = log;
        this.verified = verified ?? VerifiedMods.Contains;
        // downloads an earlier session left unfinished (it stopped mid-download)
        SyncedStore.Clean(modsFolder);
        serving = Task.Run(ServeAsync);
    }

    // Where the offer is shown: the game (LiveSyncDisplay) or a test.
    public ISyncDisplay? Display { get; set; }

    // How long a grant lasts (GrantTime; a test shortens it).
    public TimeSpan GrantFor { get; set; } = GrantTime;

    // The free bytes on the drive of a folder, or null when unknown (a test sets its own).
    public Func<string, long?> FreeSpace { get; set; } = FreeBytes;

    static long? FreeBytes(string folder)
    {
        try { return new DriveInfo(Path.GetPathRoot(Path.GetFullPath(folder))!).AvailableFreeSpace; }
        catch (Exception error) when (error is IOException or ArgumentException or UnauthorizedAccessException) { return null; }
    }

    // Never throws: MultiplayerHost.DisposeAsync goes on to stop the rest.
    public async ValueTask DisposeAsync()
    {
        lock (gate) downloading?.Cancel();
        stop.Cancel();
        requests.Writer.TryComplete();
        try { await serving; }
        catch (OperationCanceledException) { }
        catch (Exception error) { log("Mod sync: the host's worker stopped with an error: " + error); }
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

    // A machine's download grant: until when, and how many bytes it may still take.
    sealed class Grant(DateTime until, long budget)
    {
        public DateTime Until { get; } = until;
        public long Left = budget;
    }

    readonly Dictionary<ulong, Grant> grants = [];
    readonly Dictionary<ulong, int> queued = [];
    readonly Channel<(ulong Peer, Frame Frame)> requests = Channel.CreateUnbounded<(ulong, Frame)>(new UnboundedChannelOptions { SingleReader = true });
    readonly Dictionary<ulong, long> peerNext = [];
    readonly Task serving;
    long totalNext;

    // What one grant may download: the whole set twice (a retry after a failed check), and a little over.
    long GrantBudget => 2 * (own?.Mods.Sum(m => m.Bytes) ?? 0) + Window * ChunkBytes;

    // A joiner with other gameplay mods is about to be refused (LobbyService.ModsRefused): this session's mod set goes to
    // them first, and they may download it for GrantTime. True when they may (this player shares, and the set is known).
    // A grant still running stays as it is: joining again does not extend it or refill its budget.
    public bool OfferMods(ulong joiner, string lobbyId)
    {
        if (ownWire == null) return false;
        var share = Shares();
        var now = DateTime.UtcNow;
        Grant? grant = null;
        if (share)
            lock (gate)
            {
                foreach (var ended in grants.Where(g => g.Value.Until <= now).Select(g => g.Key).ToList()) grants.Remove(ended);
                if (!grants.TryGetValue(joiner, out grant)) grants[joiner] = grant = new Grant(now + GrantFor, GrantBudget);
            }
        Send(joiner, new Frame("mods.offer").Add(lobbyId).Add(ownWire).Add(share ? "1" : "0"));
        log($"Mod sync: {LobbyService.EntityId(joiner)} lacks this session's mods; " + (grant == null ? "sharing is off."
            : $"they may download them until {grant.Until:HH:mm:ss} UTC ({Megabytes(grant.Left)} left)."));
        return share;
    }

    bool Shares() => ModSettings.Read(modsFolder).Share;

    // ---- The lobby directory (DirectoryService) --------------------------------------------------------------------

    // This session's mod set as a lobby listing carries it (the wire form), its digest, and whether this player shares it.
    public string? OwnSetWire => ownWire;
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
        var plan = ModSyncPlan.Make(set, listing.Data.GetValueOrDefault(DirectoryKeys.Share) == "1", own ?? new ModSet("none", []), Locator.Find, settings.Blocked, verified,
            hostIsFriend: transport.IsFriend(listing.Host), trusted: settings.Trusts);
        why = plan.Verdict switch { SyncVerdict.Blocked => "it runs a blocked mod", SyncVerdict.Unjoinable => "it runs mods that cannot be downloaded", _ => "" };
        // a friend's mods the player may still trust on the prompt: shown
        return plan.Verdict is SyncVerdict.Ready or SyncVerdict.Download or SyncVerdict.Trust;
    }

    // A machine this one downloads from or lets download (a stranger is let through for that: MultiplayerHost's gate).
    public bool Related(ulong peer)
    {
        lock (gate) return grants.TryGetValue(peer, out var grant) && grant.Until > DateTime.UtcNow || offer?.Host == peer;
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

    // The one worker: a request that fails in any way is refused (or, when even that cannot be sent, logged), and the next
    // is served; only stopping ends it.
    async Task ServeAsync()
    {
        try
        {
            await foreach (var (peer, frame) in requests.Reader.ReadAllAsync(stop.Token))
            {
                lock (gate) queued[peer] = Math.Max(0, queued.GetValueOrDefault(peer) - 1);
                try { await ServeOneAsync(peer, frame); }
                catch (OperationCanceledException) when (stop.IsCancellationRequested) { throw; }
                catch (Exception error) { log($"Mod sync: a {frame.Type} from {LobbyService.EntityId(peer)} could not be answered: {error.Message}"); }
            }
        }
        catch (OperationCanceledException) when (stop.IsCancellationRequested) { }
    }

    async Task ServeOneAsync(ulong peer, Frame frame)
    {
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

    // Waits until both this joiner's and the total budget allow length more bytes.
    async Task PaceAsync(ulong peer, int length)
    {
        var now = Stopwatch.GetTimestamp();
        var start = Math.Max(now, Math.Max(peerNext.GetValueOrDefault(peer), totalNext));
        peerNext[peer] = start + length * Stopwatch.Frequency / PeerRate;
        totalNext = Math.Max(now, totalNext) + length * Stopwatch.Frequency / TotalRate;
        if (start > now) await Task.Delay(TimeSpan.FromSeconds((double)(start - now) / Stopwatch.Frequency), stop.Token);
    }

    // One request, answered from this session's mod set: only a mod in it, only a file it counts, only while granted and
    // within the grant's bytes; a code or raw mod only to a Steam friend.
    Frame Serve(ulong peer, Frame frame)
    {
        Grant? grant;
        lock (gate) grant = grants.TryGetValue(peer, out var found) && found.Until > DateTime.UtcNow ? found : null;
        if (grant == null) return Refused(frame, "no download was offered to this player, or its time is up");
        if (!Shares()) return Refused(frame, "the host does not share their mods");
        var mod = own?.Find(frame.Text(1), frame.Text(2));
        if (mod == null || mod.Class == ModClass.Bundled || mod.Folder == null) return Refused(frame, "not one of the host's mods");
        if (ModSets.NeedsVerified(mod.Class) && !transport.IsFriend(peer))
            return Refused(frame, "the host shares mods that run code or carry unchecked data only with Steam friends");
        if (mod.Files.Count > SyncedStore.MaxFiles) return Refused(frame, $"the mod has more than {SyncedStore.MaxFiles} files");
        if (frame.Type == "mods.files")
        {
            if (!int.TryParse(frame.Text(3), NumberStyles.None, CultureInfo.InvariantCulture, out var start) || start >= mod.Files.Count)
                return Refused(frame, "not a page of the mod's file list");
            var page = mod.Files.Skip(start).Take(PageFiles).ToList();
            var list = new Frame("mods.filelist").Add(frame.Text(0)).Add("ok").Add(mod.Files.Count).Add(start);
            foreach (var file in page) list.Add(file.Path).Add(file.Size).Add(file.Sha256);
            return list;
        }
        var path = frame.Text(3);
        var listed = mod.Files.FirstOrDefault(f => f.Path == path);
        if (listed == null || !long.TryParse(frame.Text(4), NumberStyles.None, CultureInfo.InvariantCulture, out var offset) ||
            !int.TryParse(frame.Text(5), NumberStyles.None, CultureInfo.InvariantCulture, out var length) ||
            length is < 1 or > ChunkBytes || offset > listed.Size - length)
            return Refused(frame, "not a part of the mod's files");
        lock (gate)
        {
            if (grant.Left < length) return Refused(frame, "this player has downloaded all this offer allows");
            grant.Left -= length;
        }
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
    }

    Offer? offer;
    int versions;
    CancellationTokenSource? downloading;
    readonly Dictionary<string, (ulong Peer, TaskCompletionSource<Frame> Reply)> waiting = new(StringComparer.Ordinal);
    long requestIds;

    // Whether this machine asked that host to join that lobby lately (LobbyService.JoinedRecently): only then is its mod
    // offer taken.
    public Func<ulong, string, bool>? Expected { get; set; }

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

    // accept: the player trusted a mod and asked for the download (TRUST AND DOWNLOAD): it starts when nothing else waits.
    void MakeOffer(ulong host, string connection, ModSet set, bool shares, bool accept = false)
    {
        var plan = Plan(set, shares, host);
        Offer made;
        lock (gate)
        {
            downloading?.Cancel();
            downloading = null;
            made = offer = new Offer(++versions, host, nameOf(host), connection, set, shares, plan);
            (made.State, made.Message) = Describe(made);
        }
        log($"Mod sync: {made.HostName}'s lobby: {plan.Verdict}, " + string.Join(", ", plan.Items.Select(i => $"{i.Mod.Id} {ModSyncPlan.ActionName(i.Action)}")) + ".");
        _ = PublishAsync(made, withMods: true);
        if (accept && plan.Verdict == SyncVerdict.Download) Accept(made);
    }

    // A stranger host's code and raw mods are the player's to install: hosts serve them only to Steam friends. A friend's
    // download once verified or trusted.
    ModSyncPlan Plan(ModSet set, bool shares, ulong host)
    {
        var settings = ModSettings.Read(modsFolder);
        return ModSyncPlan.Make(set, shares, own ?? new ModSet("none", []), new ModLocator(modsFolder).Find, settings.Blocked, verified,
            hostIsFriend: transport.IsFriend(host), trusted: settings.Trusts);
    }

    // What lets a code or raw mod's exact content through on this machine: HellForge's verified list, or the player's trust
    // (read again each time: the Mods page can take it back).
    bool Allowed(string id, string hash) => verified(id, hash) || ModSettings.Read(modsFolder).Trusts(id, hash);

    (string State, string Message) Describe(Offer o)
    {
        var plan = o.Plan;
        string Names(params SyncAction[] actions) => string.Join(", ", plan.Items.Where(i => actions.Contains(i.Action)).Select(i => i.Mod.Name));
        return plan.Verdict switch
        {
            SyncVerdict.Blocked => ("blocked", $"This lobby runs mods you blocked: {Names(SyncAction.Blocked)}."),
            SyncVerdict.Unjoinable when plan.Items.Any(i => i.Action == SyncAction.Update) =>
                ("blocked", $"{o.HostName} runs another HellForge version ({Names(SyncAction.Update)} differs): both players need the same HellForge."),
            SyncVerdict.Unjoinable when !o.Shares => ("blocked", $"{o.HostName} does not share their mods. Install them yourself to join: {Names(SyncAction.Manual)}."),
            SyncVerdict.Unjoinable when !transport.IsFriend(o.Host) => ("blocked", $"Some of {o.HostName}'s mods run code or carry data that HellForge cannot check: those download only from Steam friends, and only when HellForge has verified them. Install them yourself to join: {Names(SyncAction.Manual)}."),
            SyncVerdict.Unjoinable => ("blocked", $"Install these mods yourself to join: {Names(SyncAction.Manual)}."),
            // (the message is cut at 191 bytes in the game: names last)
            SyncVerdict.Trust => ("blocked", $"HellForge has not checked these mods. TRUST one to download it from {o.HostName}, or install it yourself: {Names(SyncAction.Trust)}."),
            SyncVerdict.Download => ("offer", $"{o.HostName} plays with {plan.Items.Count(i => i.Action == SyncAction.Download)} mod(s) you don't have ({Megabytes(plan.DownloadBytes)}). Download them and restart with {o.HostName}'s mods to join."),
            _ => ("offer", $"You have all of {o.HostName}'s mods. Restart with them to join.")
        };
    }

    static string Megabytes(long bytes) => (bytes / 1048576.0).ToString(bytes < 10 << 20 ? "0.0" : "0", CultureInfo.InvariantCulture) + " MB";

    // Opens a mod's web address in the player's browser (GET IT); a test sets its own.
    public Action<string> Browse { get; set; } = OpenInBrowser;
    DateTime browsed;

    // Only an https:// address that passes ModCatalog.WebAddress again, handed to the shell as the URI it parses to: the
    // default browser opens it, nothing else is started.
    static void OpenInBrowser(string address)
    {
        if (ModCatalog.WebAddress(address) is not { } checkedAddress || !Uri.TryCreate(checkedAddress, UriKind.Absolute, out var uri) || uri.Scheme != Uri.UriSchemeHttps) return;
        using var _ = Process.Start(new ProcessStartInfo(uri.AbsoluteUri) { UseShellExecute = true, Verb = "open" });
    }

    // An answer from the game (HfApi sync_answer): "syncanswer" version accept|decline|restart|block|trust|open [mod id].
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
            case "restart" when current.State is "ready" || current.State == "offer" && current.Plan.Verdict == SyncVerdict.Ready: _ = Task.Run(() => Restart(current)); break;
            case "block" when parts.Count > 3 && current.Set.Mods.Any(m => m.Id == parts[3]):
                ModSettings.SetBlocked(modsFolder, parts[3], true);
                log($"Mod sync: {parts[3]} is blocked; lobbies running it are not offered again.");
                _ = Task.Run(() => MakeOffer(current.Host, current.Connection, current.Set, current.Shares));
                break;
            // TRUST AND DOWNLOAD: that exact content (id and hash), only a mod the plan offers for trust (a Steam friend's,
            // shared, not verified); the offer is made again, and downloads when nothing else waits
            case "trust" when parts.Count > 3 && current.Plan.Items.FirstOrDefault(i => i.Mod.Id == parts[3] && i.Action == SyncAction.Trust) is { } item:
                ModSettings.SetTrusted(modsFolder, new TrustedMod(item.Mod.Id, item.Mod.Hash, item.Mod.Name, item.Mod.Version,
                    $"{current.HostName} ({LobbyService.EntityId(current.Host)})", DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture)));
                log($"Mod sync: the player trusts {item.Mod.Id} {item.Mod.Version} ({item.Mod.Hash[..16]}) from {current.HostName}.");
                _ = Task.Run(() => MakeOffer(current.Host, current.Connection, current.Set, current.Shares, accept: true));
                break;
            // GET IT: the mod's own web address from the host's set (checked when the set was read, and again before it opens)
            case "open" when parts.Count > 3 && current.Set.Mods.FirstOrDefault(m => m.Id == parts[3])?.Url is { } url:
                lock (gate)
                {
                    if (DateTime.UtcNow - browsed < TimeSpan.FromSeconds(1)) return; // a double click opens one page
                    browsed = DateTime.UtcNow;
                }
                log($"Mod sync: opening {parts[3]}'s page in the browser: {url}");
                _ = Task.Run(() =>
                {
                    try { Browse(url); }
                    catch (Exception error) when (error is System.ComponentModel.Win32Exception or InvalidOperationException) { log("Mod sync: the browser did not open: " + error.Message); }
                });
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
        if (current.Plan.Verdict == SyncVerdict.Ready) { _ = Task.Run(() => Restart(current)); return; }
        if (current.Plan.Verdict != SyncVerdict.Download) return;
        // the prompt showed the total; the drive must hold it, with some to spare
        if (FreeSpace(modsFolder) is { } free && free < current.Plan.DownloadBytes + SpareDisk)
        {
            log($"Mod sync: {Megabytes(current.Plan.DownloadBytes)} to download, {Megabytes(free)} free on the mods folder's drive.");
            Update(current, "failed", $"Not enough free disk space: the download needs {Megabytes(current.Plan.DownloadBytes)} and {Megabytes(SpareDisk)} to spare, " +
                $"and the drive of HellForge's mods folder has {Megabytes(free)} free.");
            return;
        }
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

    // Whatever goes wrong (a host's bytes can make anything throw) ends in "failed" with its message; only a cancelled
    // download ends without one.
    async Task DownloadAsync(Offer current, CancellationToken token)
    {
        try
        {
            foreach (var item in current.Plan.Items.Where(i => i.Action == SyncAction.Download)) await DownloadModAsync(current, item.Mod, token);
            Update(current, "ready", $"Everything is here. Restart with {current.HostName}'s mods to join.");
            log($"Mod sync: every mod is here ({Megabytes(current.Done)}).");
        }
        catch (OperationCanceledException) when (token.IsCancellationRequested) { log("Mod sync: the download was cancelled."); }
        catch (Exception error)
        {
            log("Mod sync: the download failed: " + (error is InvalidDataException or IOException or UnauthorizedAccessException or TimeoutException or JsonException
                ? error.Message : error.ToString()));
            Update(current, "failed", "The download failed: " + error.Message);
        }
    }

    async Task DownloadModAsync(Offer current, ModSetEntry mod, CancellationToken token)
    {
        var files = await FileListAsync(current.Host, mod, token);
        var partial = SyncedStore.Partial(modsFolder, mod.Id, mod.Hash);
        try
        {
            // The manifest, the stats file and the data spec first: a mod that shows code or raw data (its file list, its
            // manifest, its stats file, its spec) and is not verified stops before its other bytes. The manifest is the one
            // the catalogue reads (ModCatalog.ManifestIn: hellforge-mod.json before bts-mod.json). SyncedStore.Install
            // decides from all of them.
            var manifest = files.FirstOrDefault(f => f.Path == ModCatalog.ManifestName) ?? files.First(f => f.Path == ModCatalog.LegacyManifestName);
            var stats = files.FirstOrDefault(f => f.Path == StatsData.FileName);
            var spec = files.FirstOrDefault(f => f.Path == DataSpecs.FileName);
            ModFile[] first = [manifest, .. stats == null ? Array.Empty<ModFile>() : [stats], .. spec == null ? Array.Empty<ModFile>() : [spec]];
            foreach (var file in first) await DownloadFileAsync(current, mod, file, partial, token);
            string? Read(ModFile? file) => file == null ? null : File.ReadAllText(SyncedStore.Target(partial, file.Path));
            var declared = ModSets.Declared(files.Select(f => f.Path), Read(manifest)!, Read(stats), Read(spec));
            if (declared is { } kind && !Allowed(mod.Id, mod.Hash))
                throw new InvalidDataException($"{mod.Name} {ModSets.Unchecked(kind)} and is neither on HellForge's verified list nor trusted.");
            foreach (var file in files.Where(f => !first.Contains(f))) await DownloadFileAsync(current, mod, file, partial, token);
        }
        catch
        {
            SyncedStore.Discard(partial);
            throw;
        }
        // Install deletes the download itself when it refuses it or is cancelled
        var folder = await Task.Run(() => SyncedStore.Install(modsFolder, mod, partial, Allowed, cancel: token), CancellationToken.None);
        log($"Mod sync: {mod.Id} {mod.Version} checked and stored in {folder}.");
    }

    // The host's file list for a mod, page by page: safe paths, each once, at most SyncedStore.MaxFiles, sizes adding up to
    // the mod set's bytes (each at most that, so the sum cannot overflow), a manifest.
    async Task<List<ModFile>> FileListAsync(ulong host, ModSetEntry mod, CancellationToken token)
    {
        var files = new List<ModFile>();
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        int? total = null;
        long bytes = 0;
        while (total == null || files.Count < total)
        {
            var reply = await RequestAsync(host, new Frame("mods.files").Add(mod.Id).Add(mod.Hash).Add(files.Count), token);
            if (reply.Text(1) != "ok") throw new InvalidDataException($"{mod.Name}: the host refused ({(reply.Count > 2 ? reply.Text(2) : "no reason")}).");
            var count = (reply.Count - 4) / 3;
            if (reply.Count < 7 || (reply.Count - 4) % 3 != 0 || count > PageFiles || reply.Int(3) != files.Count)
                throw new InvalidDataException($"{mod.Name}: the host's file list is malformed.");
            total ??= reply.Int(2);
            if (total is < 1 or > SyncedStore.MaxFiles || reply.Int(2) != total || files.Count + count > total)
                throw new InvalidDataException($"{mod.Name}: the host's file list is malformed or too long (at most {SyncedStore.MaxFiles} files).");
            for (var i = 0; i < count; i++)
            {
                var at = 4 + 3 * i;
                var file = new ModFile(reply.Text(at), long.TryParse(reply.Text(at + 1), NumberStyles.None, CultureInfo.InvariantCulture, out var size) ? size : -1,
                    reply.Text(at + 2));
                if (!SyncedStore.SafePath(file.Path) || !paths.Add(file.Path) || file.Size < 0 || file.Size > mod.Bytes || file.Sha256.Length != 64 ||
                    !file.Sha256.All(char.IsAsciiHexDigitUpper))
                    throw new InvalidDataException($"{mod.Name}: the host listed a file that cannot be taken.");
                bytes += file.Size;
                files.Add(file);
            }
        }
        if (bytes != mod.Bytes) throw new InvalidDataException($"{mod.Name}: the host's files are not the size its mod list says.");
        if (!files.Any(f => f.Path is ModCatalog.ManifestName or ModCatalog.LegacyManifestName)) throw new InvalidDataException($"{mod.Name}: the host listed no manifest.");
        return files;
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

    // Everything is here: the next session's mods, and this session stops for the launcher to start it with them. Off the
    // pipe's thread: it hashes the mods it finds again.
    void Restart(Offer current)
    {
        try { WriteRestart(current); }
        catch (Exception error)
        {
            log("Mod sync: could not restart: " + error.Message);
            Update(current, "failed", "Could not restart with the host's mods: " + error.Message);
        }
    }

    void WriteRestart(Offer current)
    {
        var plan = Plan(current.Set, current.Shares, current.Host);
        if (plan.Verdict != SyncVerdict.Ready)
        {
            Update(current, "failed", "Some of the host's mods are no longer here: " + string.Join(", ", plan.Items.Where(i => i.Action != SyncAction.Have && i.Action != SyncAction.Off).Select(i => i.Mod.Name)) + ".");
            return;
        }
        new ModSyncSession(LobbyService.EntityId(current.Host), current.HostName, current.Connection, current.Set.Digest,
            [.. current.Set.Mods.Select(m => new ModSyncSession.Mod(m.Id, m.Hash, m.Order))]).Write(sessionFile);
        Update(current, "ready", $"Restarting with {current.HostName}'s mods...");
        File.WriteAllText(Path.Combine(runDirectory, StopSignal), StopReason);
        log($"Mod sync: the next session plays {current.HostName}'s mods ({sessionFile}); this session stops for the launcher to restart it.");
    }

    // The offer as the game and the session's logs see it.
    async Task PublishAsync(Offer current, bool withMods)
    {
        SyncView view;
        lock (gate) view = new SyncView(current.Version, current.State, current.HostName, current.Plan.DownloadBytes, current.Done, current.Message, current.Plan.Items);
        try
        {
            File.WriteAllText(Path.Combine(evidenceDirectory, OfferFile), new JsonObject
            {
                ["version"] = view.Version, ["state"] = view.State, ["host"] = view.Host, ["connection"] = current.Connection, ["bytes"] = view.Bytes, ["done"] = view.Done,
                ["message"] = view.Message,
                ["mods"] = new JsonArray([.. view.Items.Select(i => (JsonNode?)new JsonObject
                {
                    ["id"] = i.Mod.Id, ["name"] = i.Mod.Name, ["version"] = i.Mod.Version, ["class"] = ModSets.ClassName(i.Mod.Class),
                    ["action"] = ModSyncPlan.ActionName(i.Action), ["bytes"] = i.Mod.Bytes, ["url"] = i.Mod.Url
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
public sealed record SyncView(int Version, string State, string Host, long Bytes, long Done, string Message, IReadOnlyList<SyncItem> Items);

public interface ISyncDisplay
{
    // withMods: the offer's mods first (a new version); otherwise only its state (progress). False when the game did not take it.
    Task<bool> ShowAsync(SyncView view, bool withMods);
}

// The game, over the session's live-edit pipe (native/mod-loader/live.c: "syncmod" lines, then "syncstate").
public sealed class LiveSyncDisplay(LiveEditServer live) : ISyncDisplay
{
    // The loader's limit (live.c SYNC_MAX_MODS): a host's whole set (ModSet.MaxMods) and this machine's own mods. Past it,
    // the mods that need nothing (have, off) are left out first, and the message says how many.
    public const int MaxMods = 512;

    readonly SemaphoreSlim order = new(1, 1);

    public static (IReadOnlyList<SyncItem> Items, string Message) Shown(SyncView view)
    {
        if (view.Items.Count <= MaxMods) return (view.Items, view.Message);
        var items = view.Items.OrderBy(i => i.Action is SyncAction.Have or SyncAction.Off).Take(MaxMods).ToList();
        return (items, $"({view.Items.Count - MaxMods} more mods are not listed.) {view.Message}");
    }

    public async Task<bool> ShowAsync(SyncView view, bool withMods)
    {
        var (items, message) = Shown(view);
        await order.WaitAsync();
        try
        {
            if (withMods)
                for (var i = 0; i < items.Count; i++)
                {
                    var item = items[i];
                    if (!(await live.SyncModAsync(view.Version, i, ModSyncPlan.ActionName(item.Action), ModSets.ClassName(item.Mod.Class), item.Mod.Bytes, item.Mod.Id,
                            item.Mod.Name, item.Mod.Version, item.Mod.Url)).Ok) return false;
                }
            return (await live.SyncStateAsync(view.Version, view.State, view.Host, view.Bytes, view.Done, items.Count, message)).Ok;
        }
        finally { order.Release(); }
    }
}

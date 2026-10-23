using System.Globalization;
using HellForge.Core;

namespace HellForge.LocalBackend.Multiplayer;

// The PlayFab Lobby service for HellForge. A lobby lives in the backend of the player who created it (its owner node); that
// backend is its only authority. The lobby stand-in DLL of every member mirrors it from the events below and turns them
// into the SDK's state changes. Requests from this machine's DLL for a friend's lobby go to the friend's backend over
// the peer link, and its answers and events come back the same way; the frames are the same on both hops.
//
// DLL -> backend requests (field 0 is an operation id the answer repeats):
//   lobby.create [op, creatorId, creatorType, maxMembers, access, migration, lock, lobbyProps, searchProps, memberProps]
//   lobby.join   [op, memberId, memberType, connectionString, memberProps]  (+ modDigest, Protocol, modList when forwarded
//                to the owner; modList, ModlistDiff's form, only from a backend given --mod-list)
//   lobby.update [op, lobbyId, memberId, newOwner?, newOwnerId, maxMembers?, maxMembers, access?, access, lock?, lock,
//                 lobbyChanges, searchChanges, memberUpdate?, memberChanges]
//   lobby.leave  [op, lobbyId, memberId ("" = every member of the requesting machine)]
// Backend -> DLL events:
//   lobby.result        [op, kind, hresult, snapshot (create and join successes)]
//   lobby.memberAdded   [lobbyId, memberId, memberType, props]
//   lobby.memberRemoved [lobbyId, memberId, reason]
//   lobby.updated       [lobbyId, ownerId ("" = unchanged), maxMembers ("" = unchanged), access, lock, lobbyChanges,
//                        searchChanges, memberUpdateCount, (memberId, changes)...]
//   lobby.disconnected  [lobbyId, reason]
// snapshot = lobbyId, connectionString, ownerId, maxMembers, access, migration, lock, lobbyProps, searchProps,
//            memberCount, (memberId, memberType, props)...
public sealed class LobbyService
{
    public const ulong LocalNode = 0;
    public const string ConnectionPrefix = "hellforge1-lobby;";
    // HRESULTs from the SDK's error table (docs/multiplayer.md).
    public const uint LobbyNotFound = 0x89236226, AccessDenied = 0x80070005, InvalidArgument = 0x80070057;
    // "the requested lobby wasn't in a joinable state" (SDK error table, PFMultiplayerGetErrorMessage 0x38cb0). Joins this
    // host will not take (full, locked, other gameplay mods or HellForge version) fail with it: a code of HellForge's own
    // (0x8923B701) left the joining game retrying, stuck on "connecting". The reason goes to the joiner's backend in
    // "lobby.refused" [reason, line...] and its multiplayer.log; the lines (what the joiner's game shows: which mods differ,
    // say) go to its game (Refused), and the host's game is told too (RefusedFriend). For other gameplay mods the host's
    // mod set goes to the joiner just before, in its own frame (ModSync: "mods.offer"), which older backends ignore.
    public const uint LobbyNotJoinable = 0x89236227;
    public const uint ModlistMismatch = LobbyNotJoinable;
    // The HellForge multiplayer protocol a joiner's backend speaks; a host refuses other versions, since mixed builds fail in
    // confusing ways (second two-PC test: an outdated friend got no missions and could not rejoin). Raise it whenever
    // backends or stand-ins change behaviour the other side relies on.
    public const string Protocol = "hellforge-mp-7"; // 7: a refusal for different gameplay mods carries the host's mod set, and joiners download it (ModSync); 6: mods set up in id order and added entities numbered by resource id (engine 0.1.4: content numbered alike whatever the load order), the editor's mod content values in stats.set; 5: the host's stats, Local Stats left the mod digest
    const int MemberRemovedRemoteLeft = 2, DisconnectingLobbyDeleted = 1;
    static readonly HashSet<string> Requests = ["lobby.create", "lobby.join", "lobby.update", "lobby.leave"];
    static readonly HashSet<string> Events = ["lobby.result", "lobby.memberAdded", "lobby.memberRemoved", "lobby.updated", "lobby.disconnected"];

    readonly object gate = new();
    readonly IPeerTransport? transport;
    readonly Action<Frame> toLocal;
    readonly Action<string> log;
    readonly string modDigest;
    // The digest's mods one by one (ModlistDiff's form), or null when the session did not give them.
    readonly string? modList;
    readonly WarClock? warClock;
    readonly Dictionary<string, HostedLobby> hosted = new(StringComparer.Ordinal);
    // Lobbies on a friend's machine that this machine's DLL joined or is joining: lobby id -> owner node.
    readonly Dictionary<string, ulong> remote = new(StringComparer.Ordinal);
    // Forwarded requests awaiting the owner's answer: op -> owner node and lobby.
    readonly Dictionary<string, (ulong Owner, string LobbyId)> forwarded = new(StringComparer.Ordinal);
    // Friends' machines this machine's game last asked to join, and when: a refusal ("lobby.refused") may come before or
    // after the failed join's answer, so it is taken from them for a while either way.
    readonly Dictionary<ulong, DateTime> joining = [];
    static readonly TimeSpan RefusalWindow = TimeSpan.FromSeconds(30);

    // Mod sync (ModSync). Host: a joiner with other gameplay mods is refused; ModsRefused (joiner, lobby id) offers them this
    // host's mods first and says whether they may download them (the joiner's game is then told so). Called under this
    // service's lock: it must not call back into it.
    public Func<ulong, string, bool>? ModsRefused { get; set; }
    // Joins this machine asked for in the last minute: lobby id -> owner, kept whatever their answers (a mod offer, a
    // refusal or the failed join's result, in any order).
    readonly Dictionary<string, (ulong Owner, DateTime At)> joins = new(StringComparer.Ordinal);
    static readonly TimeSpan JoinMemory = TimeSpan.FromMinutes(1);

    // The lobby directory (DirectoryService). A lobby hosted here changed (created, joined, updated, left, closed): its
    // listing is looked at again. The game's lobby search from this machine's DLL: lobby.find [op, entity id, entity type,
    // filter, sort, result count], answered with lobby.found. Both are called under this service's lock.
    public event Action? HostedChanged;
    public Action<Frame>? Find { get; set; }
    public sealed record Hosted(string Id, string ConnectionString, uint MaxMembers, int Members, int Access, bool Locked, bool LocalOwner,
        IReadOnlyDictionary<string, string> SearchProperties);

    public LobbyService(IPeerTransport? transport, Action<Frame> toLocal, Action<string> log, string modDigest, WarClock? warClock = null,
        string? modList = null)
    {
        this.warClock = warClock;
        this.transport = transport;
        this.toLocal = toLocal;
        this.log = log;
        this.modDigest = modDigest;
        this.modList = modList;
    }

    // A friend's lobby refused this machine's join (the host's machine, and lines saying why for this player), and this
    // host refused a friend's (their machine, and lines for this host); the game is told (MultiplayerHost). Raised under
    // the service's lock: a handler must not call back into it.
    public event Action<ulong, IReadOnlyList<string>>? Refused;
    public event Action<ulong, IReadOnlyList<string>>? RefusedFriend;
    // Why this machine's game may not join a lobby owned by that machine (lines for the player), or null: a stranger's
    // while Play with strangers is off (MultiplayerHost). The join is answered at once, and Refused says why.
    public Func<ulong, IReadOnlyList<string>?>? JoinRefusal { get; set; }

    ulong LocalId => transport?.LocalId ?? 0;
    public static string EntityId(ulong node) => node.ToString("X", CultureInfo.InvariantCulture);
    public static bool TryNode(string entityId, out ulong node) =>
        ulong.TryParse(entityId, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out node) && node != 0;

    // A frame from this machine's DLL.
    public void FromLocal(Frame frame)
    {
        lock (gate)
        {
            if (frame.Type == "lobby.find" && Find != null) { Find(frame); return; }
            if (!Requests.Contains(frame.Type)) { log("Ignored a lobby frame from the DLL: " + frame.Type); return; }
            try { Route(frame); }
            catch (InvalidDataException error) { log($"Bad {frame.Type} from the DLL: {error.Message}"); }
        }
    }

    // A frame from a friend's backend.
    public void FromPeer(ulong peer, Frame frame)
    {
        lock (gate)
        {
            try
            {
                if (Requests.Contains(frame.Type)) Handle(peer, frame);
                else if (Events.Contains(frame.Type)) RelayEvent(peer, frame);
                else if (frame.Type == "lobby.refused" && joining.Remove(peer, out var asked) && DateTime.UtcNow - asked < RefusalWindow)
                {
                    // the lines for this player after the reason (a host from before them: the reason alone)
                    var lines = Enumerable.Range(1, Math.Max(0, frame.Count - 1)).Take(8).Select(frame.Text).ToList();
                    if (lines.Count == 0) lines.Add("The host refused the join: " + frame.Text(0));
                    log($"{EntityId(peer)} refused this player's join: {frame.Text(0)}" + (frame.Count > 1 ? " | " + string.Join(" | ", lines) : ""));
                    Refused?.Invoke(peer, lines);
                }
            }
            catch (InvalidDataException error) { log($"Bad {frame.Type} from a peer: {error.Message}"); }
        }
    }

    // This machine's DLL went away (the game closed): its members leave every lobby, and lobbies it owned close.
    public void LocalDisconnected()
    {
        lock (gate)
        {
            foreach (var lobby in hosted.Values.ToList())
            {
                var leaving = lobby.Members.Where(m => m.Node == LocalNode).ToList();
                if (leaving.Count > 0) Leave(lobby, leaving);
            }
            foreach (var (id, owner) in remote.ToList())
                Send(owner, new Frame("lobby.leave").Add("hellforge-disconnect-" + id).Add(id).Add(""));
            remote.Clear();
            forwarded.Clear();
            joining.Clear();
        }
    }

    // The lobbies hosted here as the directory lists them.
    public IReadOnlyList<Hosted> HostedLobbies()
    {
        lock (gate)
            return [.. hosted.Values.Select(l => new Hosted(l.Id, l.ConnectionString, l.MaxMembers, l.Members.Count, l.Access, l.Lock == 1,
                l.Members.Any(m => m.Node == LocalNode && SameEntity(m.Id, l.OwnerId)), new Dictionary<string, string>(l.SearchProperties, StringComparer.Ordinal)))];
    }

    // Whether this machine asked the peer for a join in the last minute (its answers may come from a stranger).
    public bool Joining(ulong peer)
    {
        lock (gate) return joins.Values.Any(j => j.Owner == peer && DateTime.UtcNow - j.At <= JoinMemory);
    }

    // Machines this backend shares a lobby with, for the peer heartbeat.
    public IReadOnlyCollection<ulong> Peers()
    {
        lock (gate) return hosted.Values.SelectMany(l => l.Nodes()).Concat(remote.Values).Where(n => n != LocalNode).ToHashSet();
    }

    // The string a friend's game joins a lobby this machine's game is in with (social tab), or null when unknown.
    public string? ConnectionStringFor(string lobbyId)
    {
        lock (gate)
        {
            if (hosted.TryGetValue(lobbyId, out var lobby)) return lobby.ConnectionString;
            if (remote.TryGetValue(lobbyId, out var owner)) return ConnectionPrefix + lobbyId + ";" + EntityId(owner);
            return null;
        }
    }

    // Whether a friend's machine hosts a lobby this machine is in (whose war time it may set).
    public bool HostsJoinedLobby(ulong node)
    {
        lock (gate) return remote.ContainsValue(node);
    }

    // Friends' machines in lobbies hosted here (they play with this machine's stats: StatsShare).
    public IReadOnlyCollection<ulong> HostedFriends()
    {
        lock (gate) return hosted.Values.SelectMany(l => l.Nodes()).Where(n => n != LocalNode).ToHashSet();
    }

    // Whether a friend's machine is a member of a lobby hosted here (the Party network admits only them).
    public bool InHostedLobby(ulong node)
    {
        lock (gate) return hosted.Values.Any(l => l.Members.Any(m => m.Node == node));
    }

    // A friend's machine stopped answering: its members leave lobbies hosted here, and its own lobbies are gone.
    public void PeerLost(ulong peer)
    {
        lock (gate)
        {
            foreach (var lobby in hosted.Values.ToList())
            {
                var leaving = lobby.Members.Where(m => m.Node == peer).ToList();
                if (leaving.Count > 0) Leave(lobby, leaving);
            }
            foreach (var (id, owner) in remote.Where(r => r.Value == peer).ToList())
            {
                remote.Remove(id);
                toLocal(new Frame("lobby.disconnected").Add(id).Add(2)); // ConnectionInterruption
            }
        }
    }

    // Requests from the local DLL go to the lobby's owner: this backend, or a friend's.
    void Route(Frame frame)
    {
        var op = frame.Text(0);
        ulong owner;
        string lobbyId;
        switch (frame.Type)
        {
            case "lobby.create":
                Handle(LocalNode, frame);
                return;
            case "lobby.join":
            {
                var connection = frame.Text(3);
                var parts = connection.StartsWith(ConnectionPrefix, StringComparison.Ordinal) ? connection.Split(';') : [];
                if (parts.Length != 3 || !TryNode(parts[2], out owner) || transport == null && owner != LocalId)
                {
                    Answer(LocalNode, op, "join", LobbyNotFound);
                    return;
                }
                if (owner == LocalId) { Handle(LocalNode, frame); return; }
                if (JoinRefusal?.Invoke(owner) is { } why)
                {
                    log($"Did not join {parts[1]} of {EntityId(owner)}: " + string.Join(" ", why));
                    Answer(LocalNode, op, "join", LobbyNotJoinable, parts[1]);
                    Refused?.Invoke(owner, why);
                    return;
                }
                lobbyId = parts[1];
                remote[lobbyId] = owner;
                joining[owner] = DateTime.UtcNow;
                foreach (var old in joins.Where(j => DateTime.UtcNow - j.Value.At > JoinMemory).Select(j => j.Key).ToList()) joins.Remove(old);
                joins[lobbyId] = (owner, DateTime.UtcNow);
                frame.Add(modDigest);
                frame.Add(Protocol);
                if (modList != null) frame.Add(modList);
                break;
            }
            default:
            {
                lobbyId = frame.Text(1);
                if (hosted.ContainsKey(lobbyId)) { Handle(LocalNode, frame); return; }
                if (!remote.TryGetValue(lobbyId, out owner))
                {
                    Answer(LocalNode, op, frame.Type == "lobby.update" ? "update" : "leave", LobbyNotFound);
                    return;
                }
                if (frame.Type == "lobby.leave") remote.Remove(lobbyId);
                break;
            }
        }
        forwarded[op] = (owner, lobbyId);
        Send(owner, frame);
    }

    // Whether this machine asked owner to join lobbyId in the last minute (a mod offer from them counts only then).
    public bool JoinedRecently(ulong owner, string lobbyId)
    {
        lock (gate) return joins.TryGetValue(lobbyId, out var join) && join.Owner == owner && DateTime.UtcNow - join.At <= JoinMemory;
    }

    // An event from a friend's backend reaches this machine's DLL only if that friend owns the lobby it is about.
    void RelayEvent(ulong peer, Frame frame)
    {
        if (frame.Type == "lobby.result")
        {
            if (!forwarded.Remove(frame.Text(0), out var request) || request.Owner != peer) return;
            if (frame.Text(1) == "join" && frame.Text(2) != "0") remote.Remove(request.LobbyId);
            if (frame.Text(1) == "join" && frame.Text(2) == "0") joining.Remove(peer); /* in: no refusal to wait for */
        }
        else
        {
            if (!remote.TryGetValue(frame.Text(0), out var owner) || owner != peer) return;
            if (frame.Type == "lobby.disconnected") remote.Remove(frame.Text(0));
        }
        toLocal(frame);
    }

    // Requests for a lobby this backend owns (or creates), from the local DLL or a friend's backend.
    void Handle(ulong node, Frame frame)
    {
        var op = frame.Text(0);
        switch (frame.Type)
        {
            case "lobby.create":
            {
                if (node != LocalNode) { Answer(node, op, "create", AccessDenied); return; }
                var index = 7;
                var lobby = new HostedLobby
                {
                    Id = Guid.NewGuid().ToString(),
                    MaxMembers = frame.UInt(3), Access = frame.Int(4), Migration = frame.Int(5), Lock = frame.Int(6)
                };
                lobby.LobbyProperties = frame.Properties(ref index);
                lobby.SearchProperties = frame.Properties(ref index);
                var memberProperties = frame.Properties(ref index);
                lobby.ConnectionString = ConnectionPrefix + lobby.Id + ";" + EntityId(LocalId != 0 ? LocalId : NodeOf(frame.Text(1)));
                var creator = new LobbyMember(frame.Text(1), frame.Text(2), LocalNode) { Properties = memberProperties };
                lobby.Members.Add(creator);
                lobby.OwnerId = creator.Id;
                hosted[lobby.Id] = lobby;
                log($"Lobby {lobby.Id} created, at most {lobby.MaxMembers} members.");
                HostedChanged?.Invoke();
                Send(LocalNode, Snapshot(new Frame("lobby.result").Add(op).Add("create").Add(0), lobby));
                return;
            }
            case "lobby.join":
            {
                var memberId = frame.Text(1);
                var connection = frame.Text(3).Split(';');
                var index = 4;
                var memberProperties = frame.Properties(ref index);
                var digest = node == LocalNode ? modDigest : frame.Count > index ? frame.Text(index) : "";
                var protocol = node == LocalNode ? Protocol : frame.Count > index + 1 ? frame.Text(index + 1) : "an older HellForge";
                if (connection.Length != 3 || !hosted.TryGetValue(connection[1], out var lobby)) { Answer(node, op, "join", LobbyNotFound, connection.ElementAtOrDefault(1)); return; }
                // A friend joins as themselves: the member id must be the requesting machine's own SteamID.
                if (node != LocalNode && (!TryNode(memberId, out var claimed) || claimed != node)) { Answer(node, op, "join", AccessDenied, lobby.Id); return; }
                if (protocol != Protocol)
                {
                    Refuse(node, op, lobby.Id, $"different HellForge versions ({protocol} joining, {Protocol} hosting); both players need the same installer.",
                        ["Your HellForge version is not the host's.", "Both players need the same HellForge installer (its version is in the launcher's corner)."],
                        ["Their HellForge version is not yours: both players need the same HellForge installer."]);
                    return;
                }
                if (digest != modDigest)
                {
                    // the joiner's mods one by one (after its digest and protocol; a joiner from before sends none)
                    var theirs = node != LocalNode && frame.Count > index + 2 ? ModlistDiff.Decode(frame.Text(index + 2)) : null;
                    var mine = modList != null ? ModlistDiff.Decode(modList) : null;
                    // mod sync: the joiner's backend gets this host's mod set first; when it may download it, its game is told so
                    var offered = node != LocalNode && ModsRefused?.Invoke(node, lobby.Id) == true;
                    Refuse(node, op, lobby.Id, "different gameplay mods; enable the same gameplay mods (see the launcher's MODS page) and start a new session.",
                        ["Your gameplay mods don't match the host's.", .. Differences(theirs, mine, asHost: false),
                            offered ? "HellForge's MOD SYNC prompt shows which of the host's mods it can download for you."
                                : "Turn on the same gameplay mods on the launcher's MODS page, then start a new session."],
                        ["Their gameplay mods don't match yours.", .. Differences(mine, theirs, asHost: true)]);
                    return;
                }
                // A friend joining again while still listed (their game crashed, or they came back before this host noticed
                // they were gone): the earlier membership leaves first, so the others see them leave and join again.
                if (node != LocalNode && lobby.Members.Where(m => m.Node == node && SameEntity(m.Id, memberId)).ToList() is { Count: > 0 } stale)
                {
                    log($"A friend joined lobby {lobby.Id} again; their earlier membership was removed first.");
                    Leave(lobby, stale);
                }
                if (lobby.Lock == 1 || lobby.Members.Count >= lobby.MaxMembers)
                {
                    Refuse(node, op, lobby.Id, lobby.Lock == 1 ? "the lobby is locked." : "the lobby is full.", [lobby.Lock == 1 ? "The host's lobby is locked." : "The host's lobby is full."]);
                    return;
                }
                if (lobby.Members.Any(m => SameEntity(m.Id, memberId))) { Answer(node, op, "join", InvalidArgument, lobby.Id); return; }
                var member = new LobbyMember(memberId, frame.Text(2), node) { Properties = memberProperties };
                lobby.Members.Add(member);
                log($"{(node == LocalNode ? "A local player" : "A friend")} joined lobby {lobby.Id} ({lobby.Members.Count} members).");
                Send(node, Snapshot(new Frame("lobby.result").Add(op).Add("join").Add(0), lobby));
                // A friend's backend adopts this war time, so both games see the same war.
                if (node != LocalNode && warClock != null) Send(node, new Frame("war.clock").Add(warClock.Seconds));
                var added = new Frame("lobby.memberAdded").Add(lobby.Id).Add(member.Id).Add(member.Type).Add(member.Properties);
                foreach (var other in lobby.Nodes().Where(n => n != node)) Send(other, added);
                HostedChanged?.Invoke();
                return;
            }
            case "lobby.update":
            {
                if (!hosted.TryGetValue(frame.Text(1), out var lobby)) { Answer(node, op, "update", LobbyNotFound); return; }
                var member = lobby.Members.FirstOrDefault(m => SameEntity(m.Id, frame.Text(2)) && m.Node == node);
                var index = 11;
                var lobbyChanges = frame.Changes(ref index);
                var searchChanges = frame.Changes(ref index);
                var memberUpdate = frame.Text(index++) == "1";
                var memberChanges = frame.Changes(ref index);
                var newOwner = frame.Text(3) == "1" ? lobby.Members.FirstOrDefault(m => SameEntity(m.Id, frame.Text(4))) : null;
                var changesLobby = frame.Text(3) == "1" || frame.Text(5) == "1" || frame.Text(7) == "1" || frame.Text(9) == "1" ||
                    lobbyChanges.Count > 0 || searchChanges.Count > 0;
                uint result = member == null ? LobbyNotFound
                    : changesLobby && !SameEntity(lobby.OwnerId, member.Id) ? AccessDenied
                    : frame.Text(3) == "1" && newOwner == null ? InvalidArgument : 0;
                Answer(node, op, "update", result);
                if (result != 0) return;
                var updated = new Frame("lobby.updated").Add(lobby.Id);
                if (newOwner != null) lobby.OwnerId = newOwner.Id;
                updated.Add(newOwner?.Id ?? "");
                if (frame.Text(5) == "1") lobby.MaxMembers = frame.UInt(6);
                updated.Add(frame.Text(5) == "1" ? lobby.MaxMembers.ToString(CultureInfo.InvariantCulture) : "");
                if (frame.Text(7) == "1") lobby.Access = frame.Int(8);
                updated.Add(frame.Text(7) == "1" ? lobby.Access.ToString(CultureInfo.InvariantCulture) : "");
                if (frame.Text(9) == "1") lobby.Lock = frame.Int(10);
                updated.Add(frame.Text(9) == "1" ? lobby.Lock.ToString(CultureInfo.InvariantCulture) : "");
                Apply(lobby.LobbyProperties, lobbyChanges);
                Apply(lobby.SearchProperties, searchChanges);
                updated.Add(lobbyChanges).Add(searchChanges);
                if (memberUpdate)
                {
                    Apply(member!.Properties, memberChanges);
                    updated.Add(1).Add(member.Id).Add(memberChanges);
                }
                else updated.Add(0);
                foreach (var target in lobby.Nodes()) Send(target, updated);
                HostedChanged?.Invoke();
                return;
            }
            case "lobby.leave":
            {
                if (!hosted.TryGetValue(frame.Text(1), out var lobby)) { Answer(node, op, "leave", LobbyNotFound); return; }
                var memberId = frame.Text(2);
                var leaving = lobby.Members.Where(m => m.Node == node && (memberId.Length == 0 || SameEntity(m.Id, memberId))).ToList();
                Answer(node, op, "leave", 0);
                if (leaving.Count > 0) Leave(lobby, leaving);
                return;
            }
        }
    }

    // Members leave. When the owner leaves, the lobby closes for everyone else (HD2's host ends the session).
    void Leave(HostedLobby lobby, List<LobbyMember> leaving)
    {
        foreach (var member in leaving) lobby.Members.Remove(member);
        HostedChanged?.Invoke();
        var leavingNodes = leaving.Select(m => m.Node).ToHashSet();
        if (leaving.Any(m => SameEntity(m.Id, lobby.OwnerId)) || lobby.Members.Count == 0)
        {
            hosted.Remove(lobby.Id);
            log($"Lobby {lobby.Id} closed.");
            var closed = new Frame("lobby.disconnected").Add(lobby.Id).Add(DisconnectingLobbyDeleted);
            foreach (var other in lobby.Nodes().Where(n => !leavingNodes.Contains(n))) Send(other, closed);
            return;
        }
        foreach (var member in leaving)
        {
            var removed = new Frame("lobby.memberRemoved").Add(lobby.Id).Add(member.Id).Add(MemberRemovedRemoteLeft);
            foreach (var other in lobby.Nodes()) Send(other, removed);
        }
    }

    // A join this host will not take: the joiner's backend is told why (the reason, then the lines its game shows), then the
    // join fails like a non-joinable lobby. hostLines: what this host's game shows, when it should know.
    void Refuse(ulong node, string op, string lobbyId, string reason, IReadOnlyList<string> joinerLines, IReadOnlyList<string>? hostLines = null)
    {
        log($"Refused a join to {lobbyId}: {reason}" + (hostLines is { Count: > 1 } ? " | " + string.Join(" | ", hostLines.Skip(1)) : ""));
        if (node != LocalNode)
        {
            var refused = new Frame("lobby.refused").Add(reason);
            foreach (var line in joinerLines) refused.Add(line);
            Send(node, refused);
            if (hostLines != null) RefusedFriend?.Invoke(node, hostLines);
        }
        Answer(node, op, "join", LobbyNotJoinable, lobbyId);
    }

    // The mods that differ, a line each, as one player reads them (ModlistDiff), at most 6 lines; none when either list is
    // unknown (a backend from before modlists).
    static List<string> Differences(List<ModFingerprint>? mine, List<ModFingerprint>? theirs, bool asHost)
    {
        if (mine == null || theirs == null) return [];
        var lines = ModlistDiff.Describe(mine, theirs, asHost);
        return lines.Count <= 6 ? lines : [.. lines.Take(5), $"... and {lines.Count - 5} more"];
    }

    void Answer(ulong node, string op, string kind, uint result, string? lobbyId = null)
    {
        var frame = new Frame("lobby.result").Add(op).Add(kind).Add(result);
        if (lobbyId != null) frame.Add(lobbyId);
        Send(node, frame);
    }

    static Frame Snapshot(Frame frame, HostedLobby lobby)
    {
        frame.Add(lobby.Id).Add(lobby.ConnectionString).Add(lobby.OwnerId).Add(lobby.MaxMembers).Add(lobby.Access).Add(lobby.Migration)
            .Add(lobby.Lock).Add(lobby.LobbyProperties).Add(lobby.SearchProperties).Add(lobby.Members.Count);
        foreach (var member in lobby.Members) frame.Add(member.Id).Add(member.Type).Add(member.Properties);
        return frame;
    }

    void Send(ulong node, Frame frame)
    {
        if (node == LocalNode) toLocal(frame);
        else transport?.Send(node, frame.Encode(), reliable: true);
    }

    static void Apply(Dictionary<string, string> properties, List<PropertyChange> changes)
    {
        foreach (var change in changes)
            if (change.Value == null) properties.Remove(change.Key);
            else properties[change.Key] = change.Value;
    }

    static bool SameEntity(string a, string b) =>
        TryNode(a, out var x) && TryNode(b, out var y) ? x == y : string.Equals(a, b, StringComparison.Ordinal);

    static ulong NodeOf(string entityId) => TryNode(entityId, out var node) ? node : 0;

    sealed class HostedLobby
    {
        public string Id = "";
        public string ConnectionString = "";
        public string OwnerId = "";
        public uint MaxMembers;
        public int Access, Migration, Lock;
        public Dictionary<string, string> LobbyProperties = new(StringComparer.Ordinal);
        public Dictionary<string, string> SearchProperties = new(StringComparer.Ordinal);
        public List<LobbyMember> Members = [];
        public IEnumerable<ulong> Nodes() => Members.Select(m => m.Node).Distinct();
    }

    sealed class LobbyMember(string id, string type, ulong node)
    {
        public string Id { get; } = id;
        public string Type { get; } = type;
        public ulong Node { get; } = node;
        public Dictionary<string, string> Properties { get; init; } = new(StringComparer.Ordinal);
    }
}

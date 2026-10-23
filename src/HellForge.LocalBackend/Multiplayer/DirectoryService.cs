using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace HellForge.LocalBackend.Multiplayer;

// The Galactic Map's public lobbies through the lobby directory (LobbyDirectory.cs; docs/multiplayer.md step 7).
//   publish: the public lobby this machine's game hosts is listed while it can be joined (privacy Public: lobby
//            property "19" / number_key6 0, LobbyService.PrivacyOf; not locked, not full) and the player lists it (the
//            Mods page's "listPublic", on by default), with its search properties, the game's lobby record (PUT /lobby,
//            rebuilt from LobbyRecords' schema: only the host's account id kept, player names kept), its live record, its
//            mod set and whether its host plays with edited stats. A change is written at most every PublishInterval; a
//            listing appears and goes at once (a lobby turned Friends only or Invite only goes; its members stay).
//   search:  the game's lobby search (lobby.find [op, entity id, entity type, filter, sort, count]) is answered from the
//            directory with lobby.found [op, hresult, count, (lobby id, connection string, owner entity id, max members,
//            members, locked 0|1, search properties)...]: the directory's coarse filters, then the game's whole filter
//            (LobbyFilter), then mod sync's and the player's rules (ModSync.Joinable). What it found stays for
//            FoundFor, so the /lobby/accounts that follows is answered for those hosts (FriendPresence).
//   strangers' listings: nothing of one reaches the game unchecked (Valid): its HellForge keys of the right form (a GUID
//            lobby id, the connection string naming the listing's host, 1 to 4 members of at most 2 to 8), only search
//            keys string_key1..30 / number_key1..30 within PlayFab's limits (LobbyService.SearchValue), at most 30, and its
//            records rebuilt from the schema (LobbyRecords): a listing breaking any of it is left out whole.
public sealed partial class DirectoryService : IAsyncDisposable
{
    public static readonly TimeSpan PublishInterval = TimeSpan.FromSeconds(5), FoundFor = TimeSpan.FromMinutes(2), SameSearch = TimeSpan.FromMilliseconds(500);
    const uint SearchFailed = 0x80004005; // E_FAIL
    // The game's search properties keep their PlayFab names in a listing; nothing else of theirs is copied. Every key that
    // looks like one is checked (LobbyService.SearchValue).
    [GeneratedRegex("^(string|number)_key")]
    private static partial Regex SearchKey();
    // The member counts a listing may claim: HD2's lobbies are made for 8 PlayFab members (max 8 members in every captured
    // PFMultiplayerCreateAndJoinLobby), a squad is at most 4.
    public const int MaxListedMembers = 4, MaxListedLimit = 8;

    readonly ILobbyDirectory directory;
    readonly LobbyService lobby;
    readonly FriendPresence presence;
    readonly ulong localId;
    readonly Action<Frame> toLocal;
    readonly Action<string> log;
    readonly ModSync? sync;
    readonly Func<bool> editedStats;
    readonly Func<bool>? listPublic;
    bool unlistedLogged;
    readonly object gate = new();
    readonly Timer timer;
    readonly CancellationTokenSource stop = new();
    Dictionary<string, string>? published;
    DateTime publishedAt = DateTime.MinValue;
    // Lobbies the last searches found, by host account id (decimal) and by lobby id.
    readonly Dictionary<string, (FriendPresence.DirectoryLobby Lobby, DateTime At)> foundByAccount = new(StringComparer.Ordinal);
    readonly Dictionary<string, (FriendPresence.DirectoryLobby Lobby, DateTime At)> foundByLobby = new(StringComparer.Ordinal);
    (DirectoryQuery Query, DateTime At, Task<IReadOnlyList<DirectoryListing>> Results)? lastSearch;

    // autoPublish: the listing follows the lobby by itself (every second, and soon after a change); tests call Publish.
    public DirectoryService(ILobbyDirectory directory, LobbyService lobby, FriendPresence presence, ulong localId, Action<Frame> toLocal, Action<string> log,
        ModSync? sync, Func<bool> editedStats, bool autoPublish = true, Func<bool>? listPublic = null)
    {
        this.listPublic = listPublic;
        this.directory = directory;
        this.lobby = lobby;
        this.presence = presence;
        this.localId = localId;
        this.toLocal = toLocal;
        this.log = log;
        this.sync = sync;
        this.editedStats = editedStats;
        if (autoPublish) lobby.HostedChanged += () => timer?.Change(TimeSpan.FromMilliseconds(200), TimeSpan.FromSeconds(1));
        lobby.Find = Find;
        presence.DirectoryByAccount = id => Found(foundByAccount, id);
        presence.DirectoryByLobby = id => Found(foundByLobby, id);
        timer = autoPublish ? new Timer(_ => Tick(), null, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(1)) : new Timer(_ => { });
    }

    // Whether this machine lists a lobby now, and the directory holds it (a stranger's join may then come in:
    // SteamPeerTransport.AcceptStranger).
    public bool Listing { get { lock (gate) return published != null && directory.Holds; } }

    // ---- Publish ----------------------------------------------------------------------------------------------------

    void Tick()
    {
        try { Publish(DateTime.UtcNow); }
        catch (Exception error) when (error is InvalidDataException or IOException or JsonException) { log("Directory: publishing failed: " + error.Message); }
    }

    // Lists the lobby when its data changed (not more often than PublishInterval), or removes the listing.
    public void Publish(DateTime now)
    {
        var desired = Desired();
        lock (gate)
        {
            if (desired == null && published == null) return;
            if (desired != null && published != null && (Same(desired, published) || now - publishedAt < PublishInterval)) return;
            var appeared = published == null;
            published = desired;
            publishedAt = now;
            directory.Publish(desired == null ? null : new DirectoryListing(localId, desired));
            if (desired == null) log("Directory: this machine's lobby is no longer listed.");
            else if (appeared) log($"Directory: listed lobby {desired[DirectoryKeys.LobbyId]} ({desired.Count} keys).");
        }
    }

    static bool Same(Dictionary<string, string> a, Dictionary<string, string> b) => a.Count == b.Count && a.All(p => b.TryGetValue(p.Key, out var v) && v == p.Value);

    // The listing of the public lobby this machine's game hosts, or null.
    Dictionary<string, string>? Desired()
    {
        var hosted = lobby.HostedLobbies().FirstOrDefault(l => l.LocalOwner);
        if (hosted == null || hosted.Locked || hosted.Members >= hosted.MaxMembers || hosted.Privacy != LobbyService.Privacy.Public) return null;
        // the player's switch (read as the listing is looked at, so a change applies within a second)
        var allowed = listPublic?.Invoke() != false;
        if (allowed == unlistedLogged)
        {
            unlistedLogged = !allowed;
            if (!allowed) log("Directory: this player's public lobbies are not listed (Mods page: \"List my Public lobbies\" is off).");
        }
        if (!allowed) return null;
        var data = new Dictionary<string, string>(StringComparer.Ordinal)
        {
            [DirectoryKeys.Protocol] = LobbyService.Protocol,
            [DirectoryKeys.Connection] = hosted.ConnectionString,
            [DirectoryKeys.LobbyId] = hosted.Id,
            [DirectoryKeys.Host] = localId.ToString(CultureInfo.InvariantCulture),
            [DirectoryKeys.Members] = hosted.Members.ToString(CultureInfo.InvariantCulture),
            [DirectoryKeys.MaxMembers] = hosted.MaxMembers.ToString(CultureInfo.InvariantCulture),
            [DirectoryKeys.Slots] = (hosted.MaxMembers - hosted.Members).ToString(CultureInfo.InvariantCulture),
            [DirectoryKeys.Lock] = hosted.Locked ? "1" : "0",
            [DirectoryKeys.Stats] = editedStats() ? "1" : "0",
            [DirectoryKeys.Digest] = sync?.OwnDigest ?? "none",
            [DirectoryKeys.Share] = sync?.Sharing == true ? "1" : "0"
        };
        foreach (var (key, value) in hosted.SearchProperties.Take(LobbyService.MaxProperties))
            if (LobbyService.SearchValue(key, value)) data[key] = value;
        var (_, body, _, _) = presence.Local;
        if (body != null) DirectoryData.Put(data, DirectoryKeys.Record, DirectoryData.Pack(Public(body, localId).ToJsonString()));
        if (presence.LocalLive is { } live) DirectoryData.Put(data, DirectoryKeys.Live, DirectoryData.Pack(live.ToJsonString()));
        if (sync?.OwnSetWire is { } mods) DirectoryData.Put(data, DirectoryKeys.Mods, DirectoryData.Pack(mods));
        return data;
    }

    // The game's lobby record as the directory publishes it (decided 2026-10-22): rebuilt from the schema's fields alone
    // (LobbyRecords), the players' names kept (the hover card shows them), every account id but the host's 0, the same
    // kind (text or number) as the game wrote. The lobby id and connection string are the listing's own.
    public static JsonObject Public(JsonObject record, ulong host) => LobbyRecords.Clean(record, strict: false, host)!;

    // ---- Search -----------------------------------------------------------------------------------------------------

    // lobby.find from the DLL, under the lobby service's lock: answered off it.
    void Find(Frame frame)
    {
        string op, filter;
        int count;
        try
        {
            op = frame.Text(0);
            filter = frame.Count > 3 ? frame.Text(3) : "";
            count = frame.Count > 5 && int.TryParse(frame.Text(5), NumberStyles.None, CultureInfo.InvariantCulture, out var asked) && asked is > 0 and <= 50 ? asked : 50;
        }
        catch (InvalidDataException error) { log("Directory: a malformed search: " + error.Message); return; }
        _ = Task.Run(async () =>
        {
            Frame answer;
            try { answer = await SearchAsync(op, filter, count); }
            catch (Exception error) when (error is InvalidDataException or IOException or TimeoutException or OperationCanceledException)
            {
                log("Directory: a search failed: " + error.Message);
                answer = new Frame("lobby.found").Add(op).Add(SearchFailed).Add(0);
            }
            toLocal(answer);
        });
    }

    public async Task<Frame> SearchAsync(string op, string filterText, int count)
    {
        var filter = LobbyFilter.Parse(filterText);
        var query = new DirectoryQuery(LobbyService.Protocol, filter.Equalities());
        var listings = await Listings(query);
        var answer = new Frame("lobby.found").Add(op).Add(0);
        var kept = new List<DirectoryListing>();
        var refused = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (var listing in listings)
        {
            // (Steam's search finds this machine's own listing too: counted, so "1 listed, 0 shown" never hides why)
            if (listing.Host == localId) { Count(refused, "this machine's own lobby"); continue; }
            if (!Valid(listing, out var props, out var members, out var max, out var locked, out var malformed)) { Count(refused, "malformed (" + malformed + ")"); continue; }
            if (!filter.Matches(props, members, max, locked)) { Count(refused, "the game's filter"); continue; }
            if (sync != null && !sync.Joinable(listing, out var why)) { Count(refused, why); continue; }
            kept.Add(listing);
            if (kept.Count == count) break;
        }
        answer.Add(kept.Count);
        foreach (var listing in kept)
        {
            Valid(listing, out var props, out var members, out var max, out var locked, out _);
            answer.Add(listing.Data[DirectoryKeys.LobbyId]).Add(listing.Data[DirectoryKeys.Connection]).Add(LobbyService.EntityId(listing.Host))
                .Add(max).Add(members).Add(locked ? 1 : 0).Add(props);
            Remember(listing);
        }
        log($"Directory: search \"{filterText}\": {listings.Count} listed, {kept.Count} shown" +
            (refused.Count > 0 ? " (" + string.Join(", ", refused.Select(r => $"{r.Value} left out: {r.Key}")) + ")" : "") +
            (filter.Ignored.Count > 0 ? $"; {filter.Ignored.Count} clause(s) not applied (random window, continent)" : "") + ".");
        return answer;
    }

    static void Count(Dictionary<string, int> reasons, string why) => reasons[why] = reasons.GetValueOrDefault(why) + 1;

    // The game sends two searches about 30 ms apart per scan: one directory search serves both (not longer: a listing that
    // appeared or went would be missed; the smoke test saw 3 s of stale results with a 3 s window).
    Task<IReadOnlyList<DirectoryListing>> Listings(DirectoryQuery query)
    {
        lock (gate)
        {
            if (lastSearch is { } last && DateTime.UtcNow - last.At < SameSearch && Same(last.Query, query) && !last.Results.IsFaulted) return last.Results;
            var results = directory.SearchAsync(query, stop.Token);
            lastSearch = (query, DateTime.UtcNow, results);
            return results;
        }
    }

    static bool Same(DirectoryQuery a, DirectoryQuery b) =>
        a.Protocol == b.Protocol && a.Count == b.Count && a.Equal.Count == b.Equal.Count && a.Equal.All(e => b.Equal.TryGetValue(e.Key, out var v) && v == e.Value);

    // A listing a search can show (see the top of this file): its HellForge keys, its search properties and its records,
    // all checked; why: what was wrong, for the log.
    public static bool Valid(DirectoryListing listing, out Dictionary<string, string> props, out int members, out int max, out bool locked, out string why) =>
        Valid(listing, out props, out members, out max, out locked, out why, out _, out _);

    static bool Valid(DirectoryListing listing, out Dictionary<string, string> props, out int members, out int max, out bool locked, out string why,
        out JsonObject? record, out JsonObject? live)
    {
        var data = listing.Data;
        props = data.Where(d => SearchKey().IsMatch(d.Key)).ToDictionary(d => d.Key, d => d.Value, StringComparer.Ordinal);
        locked = data.GetValueOrDefault(DirectoryKeys.Lock) == "1";
        max = members = 0;
        record = live = null;
        why = "";
        var (recordJson, recordOk) = Json(data, DirectoryKeys.Record);
        if (!data.TryGetValue(DirectoryKeys.LobbyId, out var id) || !Guid.TryParseExact(id, "D", out _)) why = "lobby id";
        else if (data.GetValueOrDefault(DirectoryKeys.Host) != listing.Host.ToString(CultureInfo.InvariantCulture) ||
            data.GetValueOrDefault(DirectoryKeys.Connection) != LobbyService.ConnectionPrefix + id + ";" + LobbyService.EntityId(listing.Host)) why = "host";
        else if (!int.TryParse(data.GetValueOrDefault(DirectoryKeys.Members), NumberStyles.None, CultureInfo.InvariantCulture, out members) ||
            !int.TryParse(data.GetValueOrDefault(DirectoryKeys.MaxMembers), NumberStyles.None, CultureInfo.InvariantCulture, out max) ||
            members is < 1 or > MaxListedMembers || max is < 2 or > MaxListedLimit || members >= max) why = "member counts";
        else if (data.GetValueOrDefault(DirectoryKeys.Lock) is not ("0" or "1")) why = "lock";
        else if (props.Count > LobbyService.MaxProperties || props.Any(p => !LobbyService.SearchValue(p.Key, p.Value))) why = "search properties";
        else if (!recordOk || recordJson != null && ((record = LobbyRecords.Clean(recordJson, strict: true)) == null || !HostRecord(record, listing.Host)))
        {
            record = null;
            why = "lobby record";
        }
        else if (Json(data, DirectoryKeys.Live).Value is { } liveJson) live = LobbyRecords.CleanLive(liveJson);
        return why.Length == 0;
    }

    // A packed JSON object, or (null, true) when the listing has none; (null, false) when it is not one.
    static (JsonObject? Value, bool Ok) Json(IReadOnlyDictionary<string, string> data, string key)
    {
        if (!data.ContainsKey(key)) return (null, true);
        try { return DirectoryData.Unpack(DirectoryData.Get(data, key)) is { } text && JsonNode.Parse(text) is JsonObject value ? (value, true) : (null, false); }
        catch (JsonException) { return (null, false); }
    }

    // The record's players include no more than one host, and that one is the listing's.
    static bool HostRecord(JsonObject record, ulong host)
    {
        var hosts = (record["players"] as JsonArray)?.OfType<JsonObject>().Where(p => p["isHost"]?.GetValueKind() == JsonValueKind.True).ToList() ?? [];
        return hosts.Count <= 1 && hosts.All(p => p["memberAccountId"] is not { } id || id.ToString() == host.ToString(CultureInfo.InvariantCulture));
    }

    // What /lobby/accounts and GET /lobby/<id> answer for this listing's host: its records as the schema rebuilt them.
    void Remember(DirectoryListing listing)
    {
        // Without the host's record (its game has not uploaded one yet) the map gets none: no marker for it.
        if (!Valid(listing, out _, out _, out _, out _, out _, out var record, out var live) || record == null) return;
        var found = new FriendPresence.DirectoryLobby(listing.Data[DirectoryKeys.LobbyId], listing.Data[DirectoryKeys.Connection], record, live);
        lock (gate)
        {
            var now = DateTime.UtcNow;
            foundByAccount[listing.Data[DirectoryKeys.Host]] = (found, now);
            foundByLobby[found.LobbyId] = (found, now);
            foreach (var old in foundByAccount.Where(f => now - f.Value.At > FoundFor).Select(f => f.Key).ToList()) foundByAccount.Remove(old);
            foreach (var old in foundByLobby.Where(f => now - f.Value.At > FoundFor).Select(f => f.Key).ToList()) foundByLobby.Remove(old);
        }
    }

    // The host's player name in the record of a lobby a search found (the player marked isHost), or null.
    public string? HostName(ulong host)
    {
        var record = Found(foundByAccount, host.ToString(CultureInfo.InvariantCulture))?.Body;
        return (record?["players"] as JsonArray)?.OfType<JsonObject>().Where(p => p["isHost"]?.GetValueKind() == JsonValueKind.True)
            .Select(p => p["playerName"]?.GetValueKind() == JsonValueKind.String ? (string?)p["playerName"] : null).FirstOrDefault(n => !string.IsNullOrWhiteSpace(n));
    }

    FriendPresence.DirectoryLobby? Found(Dictionary<string, (FriendPresence.DirectoryLobby Lobby, DateTime At)> found, string key)
    {
        lock (gate) return found.TryGetValue(key, out var entry) && DateTime.UtcNow - entry.At <= FoundFor ? entry.Lobby : null;
    }

    public async ValueTask DisposeAsync()
    {
        stop.Cancel();
        await timer.DisposeAsync();
        lock (gate) if (published != null) directory.Publish(null);
        stop.Dispose();
    }
}

using System.Collections.Concurrent;
using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;
using HellForge.Core;
using HellForge.LocalBackend;
using HellForge.LocalBackend.Multiplayer;

// The lobby directory: the game's filter, listing values, what a host publishes and when, and what a searching backend
// shows, remembers for the map's records and leaves out; the gate for strangers. Backends over the in-memory network and
// directory; every mod synthetic.
static class DirectoryTests
{
    const ulong HostId = 76561198000000031, GuestId = 76561198000000032, OtherId = 76561198000000033;
    const string Build = "f8c9037f96047f7a";
    const string MapFilter = "string_key1 eq 'f8c9037f96047f7a' and number_key6 eq 0 and number_key3 eq 261 and number_key7 ge 5 and number_key7 le 900 " +
        "and string_key6 ne 'SA' and string_key6 ne 'AF' and lobby/memberCount le 3";

    sealed class Node
    {
        public readonly BlockingCollection<Frame> Inbox = new();
        public readonly LobbyService Lobby;
        public readonly FriendPresence Presence = new();
        public readonly ModSync Sync;
        public readonly DirectoryService Directory;
        public readonly string Mods;
        public readonly List<string> Log = [];
        public bool Edited;

        public Node(InMemoryNetwork network, MemoryLobbyDirectory listings, ulong id, string folder, ModSet own)
        {
            Mods = System.IO.Directory.CreateDirectory(Path.Combine(folder, "mods")).FullName;
            var run = System.IO.Directory.CreateDirectory(Path.Combine(folder, "run", "backend")).FullName;
            var transport = network.Join(id);
            void Note(string line) { lock (Log) Log.Add(line); }
            Lobby = new LobbyService(transport, Inbox.Add, Note, own.Digest);
            Sync = new ModSync(transport, Mods, own, Path.GetDirectoryName(run)!, run, ModSyncSession.PathIn(new InstallLayout(folder)), _ => "Player", Note, (_, _) => false);
            Directory = new DirectoryService(listings.For(id), Lobby, Presence, id, Inbox.Add, Note, Sync, () => Edited, autoPublish: false);
            transport.Received += (peer, bytes) =>
            {
                var frame = Frame.Decode(bytes);
                if (frame.Type.StartsWith("mods.", StringComparison.Ordinal)) Sync.FromPeer(peer, frame);
                else Lobby.FromPeer(peer, frame);
            };
        }

        public Frame Next(string type, Action<bool, string> check)
        {
            var got = Inbox.TryTake(out var frame, TimeSpan.FromSeconds(5));
            check(got && frame!.Type == type, $"expected {type}, got {(got ? frame!.Type : "nothing")}");
            return frame!;
        }
    }

    static Frame Create(string op, ulong creator, IReadOnlyDictionary<string, string> search) => new Frame("lobby.create").Add(op).Add(LobbyService.EntityId(creator))
        .Add("title_player_account").Add(4).Add(0).Add(0).Add(0).Add(new Dictionary<string, string> { ["1"] = Build }).Add(search)
        .Add(new Dictionary<string, string> { ["crossplay_mode"] = "0" });

    static Frame SearchChange(string op, string lobby, ulong member, string key, string value) =>
        new Frame("lobby.update").Add(op).Add(lobby).Add(LobbyService.EntityId(member)).Add(0).Add("").Add(0).Add(0).Add(0).Add(0).Add(0).Add(0)
            .Add(Array.Empty<PropertyChange>()).Add(new List<PropertyChange> { new(key, value) }).Add(0).Add(Array.Empty<PropertyChange>());

    static readonly Dictionary<string, string> PublicSearch = new()
    {
        ["string_key1"] = Build, ["number_key6"] = "0", ["number_key3"] = "261", ["number_key5"] = "3", ["number_key7"] = "42", ["string_key6"] = "EU"
    };

    static string Record(ulong host) => $$"""
        {"players":[{"playerName":"Host Diver","memberAccountId":"{{host}}","isHost":true},{"playerName":"Second","memberAccountId":"76561198000000099","isHost":false}],
         "memberAccountIds":["{{host}}", 76561198000000099],"difficulty":5,"planetId":261,"connectionString":"from the game"}
        """;

    public static async Task Run(string root, Action<bool, string> check)
    {
        root = Path.Combine(root, "directory");
        Filters(check);
        Values(check);

        // The host runs a content mod the guest lacks.
        var hostMods = Path.Combine(root, "host", "mods");
        Directory.CreateDirectory(Path.Combine(hostMods, "ammo"));
        File.WriteAllText(Path.Combine(hostMods, "ammo", ModCatalog.ManifestName),
            """{"format":1,"id":"test.ammo","name":"Ammo","version":"1","native":{"networkFieldTypes":[{"name":"deposit_value","expect":{"kind":1,"bits":10,"min":0},"bits":14}]}}""");
        var hostSet = ModSet.Of(ModCatalog.LoadOrder(ModCatalog.Load(hostMods).Mods));
        var network = new InMemoryNetwork();
        var listings = new MemoryLobbyDirectory();
        var host = new Node(network, listings, HostId, Path.Combine(root, "host"), hostSet);
        var guest = new Node(network, listings, GuestId, Path.Combine(root, "guest"), new ModSet("none", []));
        var now = DateTime.UtcNow;

        // A public lobby is listed with what a search needs; the game's record comes once it is uploaded.
        host.Lobby.FromLocal(Create("c1", HostId, PublicSearch));
        var created = host.Next("lobby.result", check);
        var (lobbyId, connection) = (created.Text(3), created.Text(4));
        host.Directory.Publish(now);
        var listing = listings.All.Single();
        check(listing.Host == HostId && listing.Data[DirectoryKeys.Protocol] == LobbyService.Protocol && listing.Data[DirectoryKeys.LobbyId] == lobbyId &&
            listing.Data[DirectoryKeys.Connection] == connection && listing.Data[DirectoryKeys.Host] == HostId.ToString(CultureInfo.InvariantCulture) &&
            listing.Data[DirectoryKeys.Members] == "1" && listing.Data[DirectoryKeys.Slots] == "3" && listing.Data["number_key3"] == "261" &&
            listing.Data[DirectoryKeys.Stats] == "0" && listing.Data[DirectoryKeys.Share] == "1" && listing.Data[DirectoryKeys.Digest] == hostSet.Digest &&
            !listing.Data.ContainsKey(DirectoryKeys.Record), "The listing: " + string.Join(", ", listing.Data.Keys));
        check(ModSet.Parse(DirectoryData.Unpack(DirectoryData.Get(listing.Data, DirectoryKeys.Mods))!, local: false).Mods.Single().Id == "test.ammo", "The listing carries the mod set.");
        host.Presence.SetLocalLobby(JsonDocument.Parse(Record(HostId)).RootElement);
        host.Directory.Publish(now + TimeSpan.FromSeconds(1));
        check(!listings.All.Single().Data.ContainsKey(DirectoryKeys.Record), "A change waits for the publish interval.");
        host.Directory.Publish(now + TimeSpan.FromSeconds(6));
        var record = JsonNode.Parse(DirectoryData.Unpack(DirectoryData.Get(listings.All.Single().Data, DirectoryKeys.Record))!)!.AsObject();
        check(record["players"]![0]!["playerName"]!.GetValue<string>() == "Host Diver" && record["players"]![1]!["playerName"]!.GetValue<string>() == "Second" &&
            record["players"]![0]!["memberAccountId"]!.GetValue<string>() == HostId.ToString(CultureInfo.InvariantCulture) &&
            record["players"]![1]!["memberAccountId"]!.GetValue<string>() == "0" && record["memberAccountIds"]![1]!.GetValue<int>() == 0 &&
            record["memberAccountIds"]![0]!.GetValue<string>() == HostId.ToString(CultureInfo.InvariantCulture) && !record.ContainsKey("connectionString"),
            "The published record keeps names and only the host's account id: " + record.ToJsonString());

        // The guest's Galactic Map search finds it through the game's whole filter; the map's records follow for its host.
        var found = await guest.Directory.SearchAsync("s1", MapFilter, 50);
        check(found.Type == "lobby.found" && found.Text(0) == "s1" && found.Text(1) == "0" && found.Int(2) == 1 && found.Text(3) == lobbyId &&
            found.Text(4) == connection && found.Text(5) == LobbyService.EntityId(HostId) && found.Text(6) == "4" && found.Text(7) == "1" && found.Text(8) == "0",
            "The search answer: " + string.Join(" | ", Enumerable.Range(0, Math.Min(found.Count, 10)).Select(found.Text)));
        var index = 9;
        var props = found.Properties(ref index);
        check(props["number_key3"] == "261" && props["string_key1"] == Build && !props.Keys.Any(k => k.StartsWith("hf.", StringComparison.Ordinal)), "Its search properties only.");
        var accounts = guest.Presence.Lookup([HostId.ToString(CultureInfo.InvariantCulture)]);
        check(accounts["lobbies"]!.AsArray().Count == 1 && accounts["lobbies"]![0]!["lobbyId"]!.GetValue<string>() == lobbyId &&
            accounts["lobbies"]![0]!["connectionString"]!.GetValue<string>() == connection && accounts["accountLobbies"]![0]!["lobbyId"]!.GetValue<string>() == lobbyId,
            "/lobby/accounts answers for the host the search found: " + accounts.ToJsonString());
        check(guest.Presence.LobbyRecord(lobbyId)?["players"]?.AsArray().Count == 2 && guest.Directory.HostName(HostId) == "Host Diver", "GET /lobby/<id> and the host's name.");
        check((await host.Directory.SearchAsync("own", MapFilter, 50)).Int(2) == 0, "A host never finds its own lobby.");
        check((await guest.Directory.SearchAsync("s2", MapFilter.Replace("eq 261", "eq 100"), 50)).Int(2) == 0, "Another planet: not shown.");
        check((await guest.Directory.SearchAsync("s3", "lobby/memberCountRemaining gt 3", 50)).Int(2) == 0 &&
            (await guest.Directory.SearchAsync("s4", "lobby/memberCountRemaining ge 3", 50)).Int(2) == 1, "Member counts are the lobby's, not Steam's.");

        // Through the stand-in's pipe: lobby.find becomes lobby.found.
        guest.Lobby.FromLocal(new Frame("lobby.find").Add("p1").Add(LobbyService.EntityId(GuestId)).Add("title_player_account").Add(MapFilter).Add("").Add(10));
        check(guest.Next("lobby.found", check).Text(0) == "p1", "The DLL's search is answered.");

        // Edited stats: hidden unless the player shows them.
        host.Edited = true;
        host.Directory.Publish(now + TimeSpan.FromSeconds(12));
        await Task.Delay(SameSearchPause);
        check((await guest.Directory.SearchAsync("e1", MapFilter, 50)).Int(2) == 0, "A host with edited stats is hidden by default.");
        ModSettings.SetShowEditedStats(guest.Mods, true);
        check((await guest.Directory.SearchAsync("e2", MapFilter, 50)).Int(2) == 1, "Shown when the player wants them.");
        ModSettings.SetShowEditedStats(guest.Mods, false);
        host.Edited = false;
        host.Directory.Publish(now + TimeSpan.FromSeconds(18));
        await Task.Delay(SameSearchPause);

        // A blocked mod hides the lobby.
        ModSettings.SetBlocked(guest.Mods, "test.ammo", true);
        check((await guest.Directory.SearchAsync("b1", MapFilter, 50)).Int(2) == 0, "A lobby running a blocked mod is hidden.");
        ModSettings.SetBlocked(guest.Mods, "test.ammo", false);

        // Listings that cannot be joined, or are malformed, are left out.
        var code = new ModSet("0123456789abcdef0123456789abcdef", [new ModSetEntry("test.code", "Code", "1", new string('c', 64), 10, ModClass.Code, 0)]);
        var other = new Dictionary<string, string>(listings.All.Single().Data)
        {
            [DirectoryKeys.LobbyId] = "other-lobby", [DirectoryKeys.Connection] = LobbyService.ConnectionPrefix + "other-lobby;" + LobbyService.EntityId(OtherId),
            [DirectoryKeys.Host] = OtherId.ToString(CultureInfo.InvariantCulture), [DirectoryKeys.Digest] = code.Digest
        };
        DirectoryData.Put(other, DirectoryKeys.Mods, DirectoryData.Pack(code.ToJson(local: false).ToJsonString()));
        listings.For(OtherId).Publish(new DirectoryListing(OtherId, other));
        await Task.Delay(SameSearchPause);
        var withOther = await guest.Directory.SearchAsync("u1", MapFilter, 50);
        check(withOther.Int(2) == 1 && withOther.Text(3) == lobbyId, "A lobby with unverified code the guest lacks is left out.");
        other[DirectoryKeys.Digest] = guest.Sync.OwnDigest;
        other[DirectoryKeys.Connection] = LobbyService.ConnectionPrefix + "other-lobby;" + LobbyService.EntityId(HostId);
        listings.For(OtherId).Publish(new DirectoryListing(OtherId, other));
        await Task.Delay(SameSearchPause);
        check((await guest.Directory.SearchAsync("u2", MapFilter, 50)).Int(2) == 1, "A listing whose connection string names another host is left out.");
        listings.For(OtherId).Publish(null);

        // A lobby that turns private, locks or fills is no longer listed, at once.
        host.Lobby.FromLocal(SearchChange("u", lobbyId, HostId, "number_key6", "1"));
        host.Next("lobby.result", check);
        host.Next("lobby.updated", check);
        host.Directory.Publish(now + TimeSpan.FromSeconds(19));
        check(listings.All.Count == 0 && host.Directory.Listing == false, "A private lobby is not listed.");

        Gate(check);
        await host.Directory.DisposeAsync();
        await guest.Directory.DisposeAsync();
        await host.Sync.DisposeAsync();
        await guest.Sync.DisposeAsync();
    }

    // Searches within DirectoryService.SameSearch share one directory search; tests that change listings wait it out.
    static readonly TimeSpan SameSearchPause = DirectoryService.SameSearch + TimeSpan.FromMilliseconds(100);

    static void Filters(Action<bool, string> check)
    {
        var filter = LobbyFilter.Parse(MapFilter);
        check(filter.Clauses.Count == 4 && filter.Ignored.Count == 4, $"Clauses kept {filter.Clauses.Count}, left out {filter.Ignored.Count}.");
        var equal = filter.Equalities();
        check(equal.Count == 3 && equal["string_key1"] == Build && equal["number_key6"] == "0" && equal["number_key3"] == "261", "The coarse filters.");
        var props = new Dictionary<string, string>(PublicSearch);
        check(filter.Matches(props, 1, 4, false) && !filter.Matches(props, 4, 4, false), "Member count clause.");
        props["number_key3"] = "260";
        check(!filter.Matches(props, 1, 4, false), "Another planet fails.");
        props.Remove("number_key3");
        check(!filter.Matches(props, 1, 4, false), "A missing key never matches.");
        var quoted = LobbyFilter.Parse("string_key2 eq 'it''s' and number_key5 ge 2.5 and lobby/membershipLock eq 'Unlocked' and lobby/amMember eq true");
        check(quoted.Clauses[0].Value == "it's" && quoted.Matches(new Dictionary<string, string> { ["string_key2"] = "it's", ["number_key5"] = "3" }, 1, 4, false) == false,
            "Quotes, decimals; amMember is never true from a search.");
        check(LobbyFilter.Parse("number_key5 ge 2.5").Matches(new Dictionary<string, string> { ["number_key5"] = "10" }, 1, 4, false), "Numbers compare as numbers (10 > 2.5).");
        check(LobbyFilter.Parse("lobby/membershipLock eq 'Unlocked'").Matches(new Dictionary<string, string>(), 1, 4, true) == false, "A locked lobby fails the lock clause.");
        check(LobbyFilter.Parse("").Clauses.Count == 0 && LobbyFilter.Parse(null).Matches(new Dictionary<string, string>(), 0, 4, false), "No filter matches everything.");
        foreach (var bad in new[] { "string_key1 eq", "string_key1 like 'x'", "string_key1 eq 'open", "a eq 1 or b eq 2", "a eq 1 and", "'k' eq 1", new string('a', 1001) })
        {
            var refused = false;
            try { LobbyFilter.Parse(bad); } catch (InvalidDataException) { refused = true; }
            check(refused, "A malformed filter was taken: " + bad[..Math.Min(40, bad.Length)]);
        }
    }

    static void Values(Action<bool, string> check)
    {
        var data = new Dictionary<string, string>();
        var big = string.Concat(Enumerable.Range(0, 9000).Select(i => (char)('a' + i % 26)));
        DirectoryData.Put(data, "k", big);
        DirectoryData.Put(data, "small", "x");
        DirectoryData.Put(data, "hash", "#not a count");
        check(data["k"] == "#3" && data["k.0"].Length == DirectoryData.PartLength && DirectoryData.Get(data, "k") == big && DirectoryData.Get(data, "small") == "x" &&
            DirectoryData.Get(data, "hash") == "#not a count", "Long values in parts, read back whole.");
        data.Remove("k.1");
        check(DirectoryData.Get(data, "k") == null && DirectoryData.Get(data, "absent") == null, "A missing part reads as nothing.");
        var json = "{\"a\":[" + string.Join(",", Enumerable.Repeat("1", 3000)) + "]}";
        check(DirectoryData.Unpack(DirectoryData.Pack(json)) == json && DirectoryData.Pack(json).Length < json.Length / 4, "JSON packs small and unpacks.");
        check(DirectoryData.Unpack("not base64!") == null && DirectoryData.Unpack(Convert.ToBase64String([1, 2, 3])) == null &&
            DirectoryData.Unpack(DirectoryData.Pack(new string('x', 2 << 20)), limit: 1 << 20) == null, "Bad or oversized packed values read as nothing.");
        var tooBig = false;
        try { DirectoryData.Put(data, "huge", new string('z', DirectoryData.PartLength * DirectoryData.MaxParts + 1)); } catch (InvalidDataException) { tooBig = true; }
        check(tooBig, "A value beyond the parts limit is refused.");
    }

    // Strangers: presence never, a join only with a public lobby listed (and not too often), the rest once related.
    static void Gate(Action<bool, string> check)
    {
        var network = new InMemoryNetwork();
        network.Strangers.Add(OtherId);
        var transport = network.Join(HostId);
        var listing = false;
        var related = new HashSet<ulong>();
        // Play with strangers is on: the existing behavior.
        var gate = new StrangerGate(transport, () => listing, peer => related.Contains(peer), () => true, _ => { });
        var join = new Frame("lobby.join").Add("op");
        check(gate.Admit(GuestId, new Frame("presence")) && gate.Admit(GuestId, new Frame("party.message")), "Friends: everything.");
        check(!gate.Admit(OtherId, new Frame("presence")) && !gate.Admit(OtherId, new Frame("party.message")) && !gate.Admit(OtherId, join) &&
            gate.Admit(OtherId, new Frame("peer.ping")), "A stranger: no join without a listed lobby, only pings.");
        listing = true;
        var admitted = Enumerable.Range(0, 10).Count(_ => gate.Admit(OtherId, join));
        check(admitted == StrangerGate.JoinsPerPeer, $"Joins from one stranger are limited ({admitted}).");
        related.Add(OtherId);
        check(gate.Admit(OtherId, new Frame("party.message")) && gate.Admit(OtherId, new Frame("account.key")) && !gate.Admit(OtherId, new Frame("presence.hello")),
            "A stranger in a lobby here: its game's traffic, never presence.");

        // Play with strangers is off: all strangers refused, friends still pass.
        var closed = new StrangerGate(transport, () => true, _ => true, () => false, _ => { });
        check(closed.Admit(GuestId, new Frame("presence")), "Friends still pass with strangers off.");
        check(!closed.Admit(OtherId, join) && !closed.Admit(OtherId, new Frame("peer.ping")),
            "Strangers refused entirely when Play with strangers is off.");
    }
}

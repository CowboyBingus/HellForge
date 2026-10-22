using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace HellForge.LocalBackend;

// The social tab's data. Arrowhead's backend keeps every player's current lobby: the game uploads it with PUT /lobby
// (players, operation, region; game.dll 0x12cb5a0, reply ignored), keeps it alive with POST /lobby/<id>/member every
// 20 s and drops it with DELETE /lobby/clear. The social tab asks POST /lobby/accounts {"ids":["<account>",...]}
// (builders 0x12cbc40 and 0x12cbdc0) for friends' lobbies and reads {"accountLobbies":[{"account","lobbyId"}],
// "lobbies":[<the uploaded fields> + lobbyId, connectionString]} (parser 0x1055b50, lobby 0x1053f60). Its friends list
// is PlayFab's Client/GetFriendsList ({"ExternalPlatformFriends":"None"}; FriendPlayFabId, TitleDisplayName).
// HellForge answers both from Steam friends who run HellForge (Multiplayer.MultiplayerHost finds them and swaps lobbies with their
// backends, with the PlayFab profile objects each game sets for itself, which the engine needs to list a friend);
// account ids are taken to be SteamID64s, like /Account/Keys.
public sealed class FriendPresence
{
    public sealed record Friend(ulong SteamId, string Name);
    sealed record Lobby(string LobbyId, string ConnectionString, JsonObject Body, DateTime Heard);

    // A friend's lobby is shown while their backend keeps sending it.
    public static readonly TimeSpan Fresh = TimeSpan.FromSeconds(45);

    // Fields the lobby parser (0x1053f60) requires; one missing fails the whole /lobby/accounts answer, which left
    // friends greyed out. The game's own upload lacks some (numSessionMembers, completedObjectives, planetType,
    // transitionTarget, missionId per mission), so absent ones get neutral values.
    static readonly string[] RequiredNumbers = ["completedObjectives", "crossplayMode", "difficulty", "gameModeProgress", "hostability",
        "onboardingState", "operationProgress", "operationSeed", "operationType", "planetId", "planetType", "preferenceGroup", "privacyMode",
        "race", "regionIndex", "sosBeacons", "startTime", "state", "territoryId", "transitionTarget"];
    bool loggedUploadFields;

    readonly object gate = new();
    IReadOnlyList<Friend> friends = [];
    readonly Dictionary<ulong, Lobby> friendLobbies = [];
    readonly Dictionary<ulong, JsonObject> friendObjects = [];
    JsonObject localObjects = [];
    string lastUnknown = "";
    JsonObject? localBody;
    string? localLobbyId;
    long localVersion;

    public Action<string>? Log { get; set; }

    public IReadOnlyList<Friend> Friends { get { lock (gate) return friends; } }

    public void SetFriends(IReadOnlyList<Friend> current)
    {
        lock (gate)
        {
            if (current.Select(f => f.SteamId).SequenceEqual(friends.Select(f => f.SteamId))) { friends = current; return; }
            friends = current;
        }
        Log?.Invoke(current.Count == 0 ? "No Steam friends running HellForge right now." : $"Steam friends running HellForge: {current.Count} ({string.Join(", ", current.Select(f => f.Name))}).");
    }

    // PUT /lobby: the game's current lobby (fields as the game uploads them).
    public void SetLocalLobby(JsonElement body)
    {
        if (body.ValueKind != JsonValueKind.Object) return;
        var copy = JsonNode.Parse(body.GetRawText())!.AsObject();
        bool first;
        lock (gate)
        {
            localBody = copy;
            localVersion++;
            first = !loggedUploadFields;
            loggedUploadFields = true;
        }
        if (first) Log?.Invoke("The game's lobby upload has: " + string.Join(", ", copy.Select(p => p.Key)) + ".");
    }

    // POST /lobby/<id>/member: the lobby the game is in (its PlayFab lobby id).
    public void SetLocalLobbyId(string lobbyId)
    {
        lock (gate)
        {
            if (localLobbyId == lobbyId) return;
            localLobbyId = lobbyId;
            localVersion++;
        }
        Log?.Invoke($"This game is in lobby {lobbyId}.");
    }

    // DELETE /lobby/clear.
    public void ClearLocal()
    {
        lock (gate)
        {
            localBody = null;
            localLobbyId = null;
            localVersion++;
        }
    }

    public (string? LobbyId, JsonObject? Body, JsonObject Objects, long Version) Local
    {
        get { lock (gate) return (localLobbyId, localBody?.DeepClone().AsObject(), localObjects.DeepClone().AsObject(), localVersion); }
    }

    // This player's PlayFab profile objects (Object/SetObjects), shared with friends.
    public void SetLocalObjects(JsonObject objects)
    {
        lock (gate)
        {
            localObjects = objects.DeepClone().AsObject();
            localVersion++;
        }
    }

    public void SetFriendObjects(ulong friend, JsonObject objects)
    {
        bool first;
        lock (gate)
        {
            first = !friendObjects.ContainsKey(friend) && objects.Count > 0;
            if (objects.Count > 0 || !friendObjects.ContainsKey(friend)) friendObjects[friend] = objects;
        }
        if (first) Log?.Invoke($"Profile of {FriendName(friend) ?? friend.ToString("X", CultureInfo.InvariantCulture)} received for the friends list.");
    }

    // The game asks for its friends' profiles once, right after logging in, and leaves out a profile without objects
    // (first try: the friend was counted online but not listed). Profiles wait briefly for friends' objects still on
    // their way.
    public async Task WaitForFriendObjectsAsync(TimeSpan timeout, CancellationToken cancellation)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            lock (gate)
                if (friends.All(f => friendObjects.TryGetValue(f.SteamId, out var objects) && objects.Count > 0)) return;
            await Task.Delay(100, cancellation);
        }
        Log?.Invoke("A friend's profile had not arrived when the game asked for it; that friend may be missing from the list until the next session.");
    }

    public JsonObject? FriendObjects(ulong friend)
    {
        lock (gate) return friendObjects.TryGetValue(friend, out var objects) ? objects.DeepClone().AsObject() : null;
    }

    public string? FriendName(ulong friend)
    {
        lock (gate) return friends.FirstOrDefault(f => f.SteamId == friend)?.Name;
    }

    public void SetFriendLobby(ulong friend, string lobbyId, string connectionString, JsonObject body)
    {
        lock (gate) friendLobbies[friend] = new Lobby(lobbyId, connectionString, body, DateTime.UtcNow);
    }

    public void ClearFriendLobby(ulong friend)
    {
        lock (gate) friendLobbies.Remove(friend);
    }

    static void CompleteRequired(JsonObject body)
    {
        var members = body["players"] is JsonArray players ? Math.Max(1, players.Count) : 1;
        body["numLobbyMembers"] ??= members;
        body["numSessionMembers"] ??= body["numLobbyMembers"]!.DeepClone();
        body["warId"] ??= 801;
        foreach (var name in RequiredNumbers) body[name] ??= 0;
        if (body["operationMissionStates"] is JsonArray missions)
            foreach (var mission in missions.OfType<JsonObject>())
            {
                mission["index"] ??= 0;
                mission["type"] ??= 0;
                mission["state"] ??= 0;
                mission["missionId"] ??= "";
            }
        if (body["players"] is JsonArray list)
            foreach (var player in list.OfType<JsonObject>())
            {
                player["playerRank"] ??= 0;
                player["commendRank"] ??= 0;
            }
    }

    // POST /lobby/accounts: the lobbies of the asked-for friends that are in one.
    public JsonObject Lookup(IEnumerable<string> ids)
    {
        var accountLobbies = new JsonArray();
        var lobbies = new JsonArray();
        var listed = new HashSet<string>(StringComparer.Ordinal);
        var unknown = new List<string>();
        lock (gate)
        {
            foreach (var id in ids)
            {
                if (!ulong.TryParse(id, NumberStyles.None, CultureInfo.InvariantCulture, out var steamId) ||
                    !friendLobbies.TryGetValue(steamId, out var lobby) || DateTime.UtcNow - lobby.Heard > Fresh)
                {
                    unknown.Add(id);
                    continue;
                }
                accountLobbies.Add((JsonNode?)new JsonObject { ["account"] = id, ["lobbyId"] = lobby.LobbyId });
                if (!listed.Add(lobby.LobbyId)) continue;
                var body = lobby.Body.DeepClone().AsObject();
                body["lobbyId"] = lobby.LobbyId;
                body["connectionString"] = lobby.ConnectionString;
                CompleteRequired(body);
                lobbies.Add((JsonNode?)body);
            }
        }
        var missing = string.Join(", ", unknown);
        lock (gate)
        {
            if (missing == lastUnknown) missing = "";
            else lastUnknown = missing;
        }
        if (missing.Length > 0) Log?.Invoke($"Asked for the lobbies of account(s) {missing}; none is a HellForge friend in a lobby.");
        return new JsonObject { ["accountLobbies"] = accountLobbies, ["lobbies"] = lobbies };
    }
}

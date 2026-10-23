using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace HellForge.LocalBackend.Multiplayer;

// Lobby and profile records bounded before they leave this PC or reach the game: the game's own uploads before they are
// shared with friends or published in the lobby directory (so code in the game cannot use them to carry data out), and
// friends' and strangers' records before the game reads them (it was written for records from Arrowhead's servers).
// A lobby record keeps only the fields the game uploads (docs/multiplayer.md step 7), its players only theirs; every
// value has a depth, list length and text length a game record does not need, with control characters out of text.
// Null when the result is still too large.
public static class RecordGuard
{
    public sealed record Limits(int Bytes, int Depth, int Items, int Text);

    public static readonly Limits LobbyLimits = new(16 << 10, 6, 64, 256), ObjectLimits = new(64 << 10, 8, 256, 4096);

    static readonly HashSet<string> LobbyFields = new(StringComparer.Ordinal)
    {
        "startTime", "lobbyId", "players", "connectionString", "operationProgress", "gameModeProgress", "memberAccountIds",
        "inLoadout", "buildGameHash", "warId", "state", "difficulty", "hostability", "privacyMode", "onboardingState",
        "numLobbyMembers", "sosBeacons", "planetId", "territoryId", "race", "crossplayMode", "preferenceGroup", "regionIndex",
        "operationSeed", "operationType", "operationMissionStates"
    };
    static readonly HashSet<string> PlayerFields = new(StringComparer.Ordinal)
    {
        "memberAccountId", "playerName", "playerRank", "commendRank", "experience", "sourcePlatform", "hasInfo", "isHost"
    };
    static readonly HashSet<string> LiveFields = new(StringComparer.Ordinal)
    {
        "missionTime", "playerPositions", "objectiveStatusMask", "outpostStatusMask", "state"
    };

    // PUT /lobby's record (the game's, a friend's, a listing's).
    public static JsonObject? Lobby(JsonObject record) => Clean(record, LobbyFields, LobbyLimits, players: true);

    // PUT /lobby/<id>/live's state.
    public static JsonObject? Live(JsonObject record) => Clean(record, LiveFields, LobbyLimits, players: false);

    // A friend's PlayFab profile objects (presence): no known field list, so only bounded.
    public static JsonObject? Objects(JsonObject objects) => Clean(objects, null, ObjectLimits, players: false);

    static JsonObject? Clean(JsonObject record, IReadOnlySet<string>? fields, Limits limits, bool players)
    {
        var copy = new JsonObject();
        foreach (var (key, value) in record)
        {
            if (fields != null ? !fields.Contains(key) : !Key(key, limits)) continue;
            var cleaned = players && key == "players" ? Players(value, limits) : Value(value, limits, 1);
            if (cleaned.Ok) copy[key] = cleaned.Node;
        }
        return Encoding.UTF8.GetByteCount(copy.ToJsonString()) <= limits.Bytes ? copy : null;
    }

    static (bool Ok, JsonNode? Node) Players(JsonNode? value, Limits limits)
    {
        if (value is not JsonArray list || list.Count > limits.Items) return (false, null);
        var players = new JsonArray();
        foreach (var player in list.OfType<JsonObject>())
        {
            var kept = new JsonObject();
            foreach (var (key, field) in player)
                if (PlayerFields.Contains(key) && Value(field, limits, 3) is { Ok: true } cleaned) kept[key] = cleaned.Node;
            players.Add((JsonNode)kept);
        }
        return (true, players);
    }

    static (bool Ok, JsonNode? Node) Value(JsonNode? value, Limits limits, int depth)
    {
        switch (value)
        {
            case null: return (true, null);
            case JsonValue scalar:
                return scalar.GetValueKind() switch
                {
                    JsonValueKind.String => scalar.GetValue<string>() is var text && text.Length <= limits.Text
                        ? (true, JsonValue.Create(new string([.. text.Where(c => !char.IsControl(c))]))) : (false, null),
                    JsonValueKind.Number => double.TryParse(scalar.ToJsonString(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var number) &&
                        double.IsFinite(number) ? (true, scalar.DeepClone()) : (false, null),
                    JsonValueKind.True or JsonValueKind.False or JsonValueKind.Null => (true, scalar.DeepClone()),
                    _ => (false, null)
                };
            case JsonArray list when depth < limits.Depth && list.Count <= limits.Items:
            {
                var copy = new JsonArray();
                foreach (var item in list)
                    if (Value(item, limits, depth + 1) is { Ok: true } cleaned) copy.Add(cleaned.Node);
                return (true, copy);
            }
            case JsonObject node when depth < limits.Depth && node.Count <= limits.Items:
            {
                var copy = new JsonObject();
                foreach (var (key, item) in node)
                    if (Key(key, limits) && Value(item, limits, depth + 1) is { Ok: true } cleaned) copy[key] = cleaned.Node;
                return (true, copy);
            }
            default: return (false, null);
        }
    }

    static bool Key(string key, Limits limits) => key.Length is > 0 and <= 64 && !key.Any(char.IsControl);
}

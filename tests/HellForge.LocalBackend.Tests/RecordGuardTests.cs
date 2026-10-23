using System.Text.Json.Nodes;
using HellForge.LocalBackend.Multiplayer;

// Lobby and profile records bounded before they are shared or reach the game (RecordGuard): only the known fields, every
// value within its limits, control characters out of text, and nothing over the size limit.
static class RecordGuardTests
{
    public static void Run(Action<bool, string> check)
    {
        var record = new JsonObject
        {
            ["planetId"] = 261, ["difficulty"] = 5, ["buildGameHash"] = new string('a', 214), ["stolen"] = "C:/Users/someone/secret.txt",
            ["players"] = new JsonArray(new JsonObject { ["playerName"] = "Name\u0007\nHere", ["isHost"] = true, ["extra"] = "x" }),
            ["sosBeacons"] = new JsonArray(Enumerable.Range(0, 65).Select(i => (JsonNode?)JsonValue.Create(i)).ToArray()),
            ["operationProgress"] = Deep(10), ["state"] = new string('s', 300), ["startTime"] = 1.5
        };
        var clean = RecordGuard.Lobby(record);
        check(clean != null && !clean.ContainsKey("stolen") && clean["planetId"]!.GetValue<int>() == 261 && clean["buildGameHash"]!.GetValue<string>().Length == 214,
            "A lobby record keeps its known fields only: " + clean?.ToJsonString());
        var player = clean!["players"]![0]!.AsObject();
        check(player["playerName"]!.GetValue<string>() == "NameHere" && player["isHost"]!.GetValue<bool>() && !player.ContainsKey("extra"),
            "A player keeps its known fields, with control characters out of text.");
        check(!clean.ContainsKey("sosBeacons") && !clean.ContainsKey("state") && clean["startTime"]!.GetValue<double>() == 1.5,
            "A list or text past its limit is left out.");
        check(Depth(clean["operationProgress"]) < RecordGuard.LobbyLimits.Depth, "Nesting stops at the depth limit.");
        var huge = new JsonObject { ["players"] = new JsonArray(Enumerable.Range(0, 64).Select(_ => (JsonNode?)new JsonObject { ["playerName"] = new string('n', 256) }).ToArray()) };
        check(RecordGuard.Lobby(huge) == null, "A record over the size limit is refused.");
        var live = RecordGuard.Live(new JsonObject { ["missionTime"] = 30, ["playerPositions"] = new JsonArray(1, 2), ["other"] = 1 });
        check(live != null && live.Count == 2 && !live.ContainsKey("other"), "A live state keeps its known fields.");
        var objects = RecordGuard.Objects(new JsonObject { ["Title"] = new JsonObject { ["DataObject"] = "abc" }, ["bad\u0001key"] = 1 });
        check(objects != null && objects.ContainsKey("Title") && objects.Count == 1, "Profile objects are bounded, keys with control characters left out.");
        Console.WriteLine("PASS record guard: known fields, limits, control characters, size");
    }

    static JsonObject Deep(int levels)
    {
        var top = new JsonObject();
        var at = top;
        for (var i = 0; i < levels; i++) { var next = new JsonObject(); at["n"] = next; at = next; }
        at["leaf"] = 1;
        return top;
    }

    static int Depth(JsonNode? node) => node is JsonObject o && o["n"] is { } next ? 1 + Depth(next) : 0;
}

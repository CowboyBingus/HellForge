using System.Text.Json;
using HellForge.LocalBackend;

// The all-planets war (OpenWar, backend --war open): every planet but Super Earth's home world an enemy's with one
// liberation campaign, Super Earth's planets given to the nearest front, the frozen war's defenses gone; served in place
// of the frozen war and of the live war.
static class OpenWarTests
{
    public static async Task Run(string root, Action<bool, string> check)
    {
        using var embedded = LocalBackend.LoadResource("war-data.json");
        var info = embedded.RootElement.GetProperty("warInfo");
        var frozen = embedded.RootElement.GetProperty("status");
        var open = OpenWar.Status(info, frozen);

        var planets = info.GetProperty("planetInfos").GetArrayLength();
        var owner = open.GetProperty("planetStatus").EnumerateArray().ToDictionary(p => p.GetProperty("index").GetInt32(), p => p.GetProperty("owner").GetInt32());
        var campaigns = open.GetProperty("campaigns").EnumerateArray().ToList();
        check(campaigns.Count == planets - 1, $"Every planet but Super Earth has a campaign: {campaigns.Count} of {planets - 1}.");
        check(owner[0] == 1 && campaigns.All(c => c.GetProperty("planetIndex").GetInt32() != 0), "Super Earth stays Super Earth's, with no campaign.");
        check(campaigns.All(c => c.GetProperty("type").GetInt32() == 0 && c.GetProperty("race").GetInt32() == 1 && owner[c.GetProperty("planetIndex").GetInt32()] is 2 or 3 or 4),
            "Each campaign is a liberation (type 0, race 1) of an enemy-held planet.");
        check(campaigns.Select(c => c.GetProperty("id").GetInt32()).Distinct().Count() == campaigns.Count, "Campaign ids are unique.");
        check(open.GetProperty("planetEvents").GetArrayLength() == 0 && open.GetProperty("planetAttacks").GetArrayLength() == 0 &&
            open.GetProperty("jointOperations").GetArrayLength() == 0, "The frozen war's defenses, attacks and joint operations are gone.");
        // one enemy per sector of the galaxy map; a sector the frozen war's enemy holds stays that enemy's
        var frozenOwner = frozen.GetProperty("planetStatus").EnumerateArray().ToDictionary(p => p.GetProperty("index").GetInt32(), p => p.GetProperty("owner").GetInt32());
        var sectors = OpenWar.Sectors();
        var mixed = owner.Where(o => o.Key != 0).GroupBy(o => sectors[o.Key]).Where(g => g.Select(o => o.Value).Distinct().Count() > 1).Select(g => g.Key).ToList();
        check(mixed.Count == 0, "Each sector is one enemy's: " + string.Join(", ", mixed));
        check(frozenOwner.Where(o => o.Value != 1).GroupBy(o => sectors[o.Key]).Where(g => g.Select(o => o.Value).Distinct().Count() == 1)
            .All(g => owner[g.First().Key] == g.First().Value), "A sector one enemy holds in the frozen war stays its.");
        check(owner[196] == 3 && owner[241] == 3 && owner[153] == 3, $"Malevelon Creek, Ubanea and Draupnir are the Automatons', not {owner[196]}, {owner[241]}, {owner[153]}.");
        check(owner[97] == 4 && owner[34] == 2 && owner[169] == 2, "Calypso the Illuminate's; Hellmire and Estanu the Terminids'.");
        var shares = owner.Where(o => o.Key != 0).GroupBy(o => o.Value).ToDictionary(g => g.Key, g => g.Count());
        check(shares.Count == 3 && shares.Values.All(n => n >= 60), "Each enemy holds a large share of the galaxy: " + string.Join(", ", shares.Select(s => s.Key + "=" + s.Value)));
        check(OpenWar.Status(info, frozen).GetRawText() == open.GetRawText(), "The same on every machine.");

        // served by the backend in place of the frozen war
        await using var app = LocalBackend.Create(0, Path.Combine(root, "open-war-backend"), LoginMode.LocalSession, true, openWar: true);
        await app.StartAsync();
        try
        {
            using var client = new HttpClient(new HttpClientHandler { UseProxy = false }) { BaseAddress = new Uri(app.Urls.Single()) };
            using var response = await client.GetAsync("/WarSeason/801/Status");
            using var served = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            check(served.RootElement.GetProperty("campaigns").GetArrayLength() == planets - 1, "The backend serves the all-planets war.");
        }
        finally { await app.StopAsync(); }
    }
}

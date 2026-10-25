using System.Text.Json;

namespace HellForge.LocalBackend;

// Items that cost real money: premium warbond tokens (premiumTokenMixIds from each seasonPass)
// and Superstore items (mixId in storeFront). Built once from the embedded progression-data.json.
// Warbond page items are earned with medals (free), but require the premium token to access;
// removing the token from a profile locks out the pages.
public sealed class PaidContentSet
{
    public HashSet<uint> MixIds { get; }

    PaidContentSet(HashSet<uint> mixIds) => MixIds = mixIds;

    public static PaidContentSet Load()
    {
        using var data = LocalBackend.LoadResource("progression-data.json");
        var paid = new HashSet<uint>();

        // Premium warbond tokens: each seasonPass entry's premiumTokenMixIds.
        foreach (var pass in data.RootElement.GetProperty("seasonPass").EnumerateArray())
            if (pass.TryGetProperty("premiumTokenMixIds", out var tokens))
                foreach (var token in tokens.EnumerateArray())
                    if (token.TryGetUInt32(out var id))
                        paid.Add(id);

        // Superstore items: every mixId reachable from storeFront.
        CollectMixIds(data.RootElement.GetProperty("storeFront"), paid);

        return new PaidContentSet(paid);
    }

    static void CollectMixIds(JsonElement node, HashSet<uint> found)
    {
        switch (node.ValueKind)
        {
            case JsonValueKind.Object:
                foreach (var property in node.EnumerateObject())
                {
                    if (property.Name == "mixId" && property.Value.TryGetUInt32(out var id))
                        found.Add(id);
                    else
                        CollectMixIds(property.Value, found);
                }
                break;
            case JsonValueKind.Array:
                foreach (var element in node.EnumerateArray())
                    CollectMixIds(element, found);
                break;
        }
    }

    public bool IsPaid(uint mixId) => MixIds.Contains(mixId);
}

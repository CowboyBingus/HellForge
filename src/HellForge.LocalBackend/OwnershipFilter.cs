using System.Text.Json;
using System.Text.Json.Nodes;

namespace HellForge.LocalBackend;

// Filters paid content from profile inventory responses.
//
// Without a real inventory (offline mode): all paid items are removed from served profiles.
// With a real inventory (the player's own, from the launcher's Sync Inventory snapshot): only paid items
// the player actually owns are kept; paid items not in the real inventory are removed.
//
// Items that are not in the paid set pass through unfiltered in both modes.
public sealed class OwnershipFilter
{
    readonly HashSet<uint>? realInventory;

    public OwnershipFilter(PaidContentSet paid, HashSet<uint>? realInventory)
    {
        Paid = paid;
        this.realInventory = realInventory;
    }

    public PaidContentSet Paid { get; }
    public int PaidCount => Paid.MixIds.Count;

    // True when the filter was built with a captured real inventory.
    public bool HasRealInventory => realInventory != null;

    // Whether a given item should be included in a served profile.
    public bool IsAllowed(uint mixId)
    {
        if (!Paid.IsPaid(mixId))
            return true;
        if (realInventory == null)
            return false;
        return realInventory.Contains(mixId);
    }

    // Filter a template's items array: returns a new array with disallowed items removed.
    public JsonArray FilterItems(JsonArray items)
    {
        var filtered = new JsonArray();
        foreach (var item in items)
        {
            if (item is not JsonObject obj) continue;
            if (obj["mixId"] is not JsonNode mixNode) { filtered.Add(item.DeepClone()); continue; }
            var mixId = (uint)long.Parse(mixNode.ToJsonString(), System.Globalization.CultureInfo.InvariantCulture);
            if (IsAllowed(mixId))
                filtered.Add(item.DeepClone());
        }
        return filtered;
    }

    // Filter an inventory section (the "items" array inside the inventory object).
    public JsonElement FilterInventoryResponse(JsonElement inventorySection)
    {
        var node = JsonNode.Parse(inventorySection.GetRawText())!.AsObject();
        if (node["items"] is JsonArray items)
            node["items"] = FilterItems(items);
        return JsonSerializer.SerializeToElement(node, BackendJson.Default.JsonNode);
    }

    // Load a real inventory from a JSON file: an array of objects with "mixId" fields,
    // as the real backend's /Progression/inventory response serves them.
    public static HashSet<uint> LoadRealInventory(string path)
    {
        var text = File.ReadAllText(path);
        using var document = JsonDocument.Parse(text);
        return ParseRealInventory(document.RootElement);
    }

    // Parse a real inventory from a JSON element: accepts either the full inventory response
    // ({"items": [...]}) or a bare items array ([...]).
    public static HashSet<uint> ParseRealInventory(JsonElement element)
    {
        var owned = new HashSet<uint>();
        var items = element.ValueKind == JsonValueKind.Object && element.TryGetProperty("items", out var arr)
            ? arr : element;
        if (items.ValueKind != JsonValueKind.Array) return owned;
        foreach (var item in items.EnumerateArray())
            if (item.TryGetProperty("mixId", out var mix) && mix.TryGetUInt32(out var id))
                owned.Add(id);
        return owned;
    }
}

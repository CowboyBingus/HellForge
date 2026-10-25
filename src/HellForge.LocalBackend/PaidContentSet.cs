using System.Text.Json;

namespace HellForge.LocalBackend;

// What the game sells for real money, and its currencies, from the embedded progression-data.json and item-catalog.json:
// - paid items (MixIds): each premium warbond's tokens, and every item priced in Super Credits (the Superstore's, also
//   those not in the current rotation) unless a warbond page or a progression package also gives it. A player has them
//   only by buying them in the normal game, so the backend serves them only from a sealed account snapshot
//   (OwnershipFilter). The requisition terminal's and the samples store's items are not paid, though they are store fronts.
// - warbond items: bought with medals on a warbond's pages. A premium warbond's items need one of its tokens; Helldivers
//   Mobilize is free, and an item it also offers needs none.
// - Super Credits: a premium currency. The price item and the three wallets that make up its balance are never stored,
//   served, granted or imported (KB hellforge-vanilla-progression).
// - currencies: medals, requisition and samples, which the import treats as numbers.
// Steam DLC gear (for example the Super Citizen Edition's) is not in this data, so it is not covered here.
public sealed class PaidContentSet
{
    static readonly HashSet<uint> FreeWarbonds = [1929468580];
    const uint SuperCreditPrice = 0xCF875032;
    static readonly HashSet<uint> SuperCreditIds = [SuperCreditPrice, 0x6C3C83C1, 0xCBFFC645, 0x865AF2FD];
    // Requisition, common, rare and super samples, and the generic medal reward
    static readonly uint[] FixedCurrencies = [0xD7150EEC, 0xEDF6EAF5, 0xB1ED1C41, 0xDAC0E9DB, 0x3584C850];

    readonly Dictionary<uint, uint[]> warbondItemTokens;

    // Premium warbond tokens and items sold for Super Credits.
    public HashSet<uint> MixIds { get; }
    // The premium warbond tokens alone.
    public HashSet<uint> WarbondTokens { get; }
    // Every item on any warbond's pages, free or premium.
    public HashSet<uint> WarbondItems { get; }
    public HashSet<uint> Currencies { get; }

    PaidContentSet(HashSet<uint> warbondTokens, HashSet<uint> mixIds, Dictionary<uint, uint[]> warbondItemTokens, HashSet<uint> warbondItems,
        HashSet<uint> currencies)
    {
        WarbondTokens = warbondTokens;
        MixIds = mixIds;
        this.warbondItemTokens = warbondItemTokens;
        WarbondItems = warbondItems;
        Currencies = currencies;
    }

    public static PaidContentSet Load()
    {
        using var data = LocalBackend.LoadResource("progression-data.json");
        var paid = new HashSet<uint>();
        var tokensByItem = new Dictionary<uint, HashSet<uint>>();
        var freeItems = new HashSet<uint>();
        var warbondItems = new HashSet<uint>();
        var currencies = new HashSet<uint>(FixedCurrencies);
        foreach (var pass in data.RootElement.GetProperty("seasonPassDetails").EnumerateObject().Select(p => p.Value))
        {
            foreach (var medal in new[] { "medalsTotalMixId", "medalsConsumableMixId", "medalsSpentMixId" })
                currencies.Add(pass.GetProperty(medal).GetUInt32());
            var free = FreeWarbonds.Contains(pass.GetProperty("id32").GetUInt32());
            var tokens = pass.GetProperty("premiumTokenMixIds").EnumerateArray().Select(t => t.GetUInt32()).ToArray();
            if (!free) paid.UnionWith(tokens);
            foreach (var item in WarbondPageItems(pass))
            {
                warbondItems.Add(item);
                if (free) freeItems.Add(item);
                else if (tokensByItem.TryGetValue(item, out var known)) known.UnionWith(tokens);
                else tokensByItem[item] = [.. tokens];
            }
        }
        var warbondTokens = new HashSet<uint>(paid);
        var earned = PackageItems(data.RootElement);
        paid.UnionWith(SoldForSuperCredits().Where(item => !warbondItems.Contains(item) && !earned.Contains(item)));
        var gated = tokensByItem.Where(entry => !freeItems.Contains(entry.Key)).ToDictionary(entry => entry.Key, entry => entry.Value.ToArray());
        return new PaidContentSet(warbondTokens, paid, gated, warbondItems, currencies);
    }

    static IEnumerable<uint> WarbondPageItems(JsonElement pass) =>
        pass.GetProperty("seasonPassPages").EnumerateArray()
            .SelectMany(page => page.GetProperty("seasonPassItems").EnumerateArray())
            .Select(item => item.GetProperty("itemMixId").GetUInt32());

    // Catalogue items whose price names Super Credits.
    static List<uint> SoldForSuperCredits()
    {
        using var catalog = LocalBackend.LoadResource("item-catalog.json");
        return [.. catalog.RootElement.GetProperty("items").EnumerateArray()
            .Where(item => item.TryGetProperty("buyPrice", out var price) && price.ValueKind == JsonValueKind.Array &&
                price.EnumerateArray().Any(cost => cost.TryGetProperty("mixId", out var currency) && currency.TryGetUInt32(out var id) && id == SuperCreditPrice))
            .Select(item => item.GetProperty("mixId").GetUInt32())];
    }

    // Items progression packages (player levels, ship modules, weapon levels) need or give: earned, never bought.
    static HashSet<uint> PackageItems(JsonElement data)
    {
        var items = new HashSet<uint>();
        foreach (var package in data.GetProperty("progressionPackages").EnumerateArray())
        {
            if (package.TryGetProperty("requiredItems", out var required))
                items.UnionWith(required.EnumerateArray().Where(i => i.ValueKind == JsonValueKind.Number).Select(i => i.GetUInt32()));
            if (!package.TryGetProperty("levels", out var levels)) continue;
            foreach (var reward in levels.EnumerateArray().Where(l => l.TryGetProperty("rewards", out _)).SelectMany(l => l.GetProperty("rewards").EnumerateArray()))
                if (reward.TryGetProperty("type", out var type) && type.GetInt32() == 1 && reward.TryGetProperty("id32", out var id)) items.Add(id.GetUInt32());
        }
        return items;
    }

    public bool IsPaid(uint mixId) => MixIds.Contains(mixId);

    public static bool IsSuperCredits(uint mixId) => SuperCreditIds.Contains(mixId);

    // A premium warbond's item: the tokens of the warbonds that offer it, one of which the player must own.
    public bool TryGetWarbondTokens(uint mixId, [System.Diagnostics.CodeAnalysis.MaybeNullWhen(false)] out uint[] tokens) =>
        warbondItemTokens.TryGetValue(mixId, out tokens);
}

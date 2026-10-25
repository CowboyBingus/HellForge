using System.Text.Json;
using System.Text.Json.Nodes;

namespace HellForge.Core;

// How a profile takes an account snapshot (Profile > Account sync): which categories are imported and how each settles a
// difference with what the profile earned in HellForge. Kept in the profile folder as account-import.json. Premium
// ownership (warbond tokens, Superstore items) is not a category: the backend always takes it from the sealed snapshot.
// Super Credits are never imported.
public enum ImportRule
{
    // numbers: the larger of the two; ship modules and weapon levels: the further of the two, per package
    Higher,
    // item lists: everything either side holds, each at the larger amount
    Merge,
    // the vanilla account's values replace the profile's
    Vanilla
}

public sealed record ImportCategory(string Id, string Label, bool IncludedByDefault, IReadOnlyList<ImportRule> Rules);

public sealed class AccountImportSettings
{
    public const string FileName = "account-import.json";

    public static IReadOnlyList<ImportCategory> Categories { get; } =
    [
        new("level", "Level and XP", true, [ImportRule.Higher, ImportRule.Vanilla]),
        new("warbondItems", "Warbond items bought with medals", true, [ImportRule.Merge, ImportRule.Vanilla]),
        new("armory", "Other armory items", true, [ImportRule.Merge, ImportRule.Vanilla]),
        new("packages", "Ship modules and weapon levels", true, [ImportRule.Higher, ImportRule.Vanilla]),
        new("currencies", "Medals, requisition and samples", true, [ImportRule.Higher, ImportRule.Vanilla]),
        new("ship", "Ship name and location", false, [ImportRule.Vanilla]),
        new("onboarding", "Tutorial progress", false, [ImportRule.Vanilla])
    ];

    readonly Dictionary<string, (bool Include, ImportRule Rule)> choices = Categories.ToDictionary(
        category => category.Id, category => (category.IncludedByDefault, category.Rules[0]), StringComparer.Ordinal);

    // Later syncs apply with these choices without asking.
    public bool AutoApply { get; set; }

    public bool Includes(string category) => choices[category].Include;

    public ImportRule RuleFor(string category) => choices[category].Rule;

    // A rule the category does not offer is refused, so a hand-edited file cannot ask for one.
    public void Set(string category, bool include, ImportRule rule)
    {
        var known = Categories.FirstOrDefault(c => c.Id == category) ?? throw new ArgumentException("Unknown import category " + category + ".");
        if (!known.Rules.Contains(rule)) throw new ArgumentException(known.Label + " cannot use " + rule + ".");
        choices[category] = (include, rule);
    }

    public static string PathIn(string profileFolder) => Path.Combine(profileFolder, FileName);

    // A missing file gives the defaults; a category the file leaves out, or names a rule it does not offer, keeps its default.
    public static AccountImportSettings Load(string path)
    {
        var settings = new AccountImportSettings();
        if (!File.Exists(path)) return settings;
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        var root = document.RootElement;
        settings.AutoApply = root.TryGetProperty("autoApply", out var auto) && auto.ValueKind == JsonValueKind.True;
        if (!root.TryGetProperty("categories", out var categories) || categories.ValueKind != JsonValueKind.Object) return settings;
        foreach (var category in Categories)
            if (categories.TryGetProperty(category.Id, out var entry) && entry.ValueKind == JsonValueKind.Object &&
                entry.TryGetProperty("include", out var include) && include.ValueKind is JsonValueKind.True or JsonValueKind.False &&
                entry.TryGetProperty("rule", out var rule) && rule.ValueKind == JsonValueKind.String &&
                Enum.TryParse<ImportRule>(rule.GetString(), out var parsed) && category.Rules.Contains(parsed))
                settings.choices[category.Id] = (include.GetBoolean(), parsed);
        return settings;
    }

    public void Save(string path)
    {
        var categories = new JsonObject();
        foreach (var category in Categories)
            categories[category.Id] = new JsonObject { ["include"] = Includes(category.Id), ["rule"] = RuleFor(category.Id).ToString() };
        var root = new JsonObject { ["autoApply"] = AutoApply, ["categories"] = categories };
        File.WriteAllText(path, root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n");
    }
}

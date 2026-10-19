using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace HellForge.Core;

// The in-game editor's catalogue (native/editor, docs/sdk-roadmap.md phase 5a): the arsenal pages of StatsCatalog for the
// categories the editor shows, laid out as the HD2R Editor lays out a page (ui/sections.lua there), with what the editor
// needs per field to read it (get_live), change it (set_live) and save it (Local Stats): editor-catalog.json, written next
// to the editor's DLL by Build-Editor.ps1.
//
// A page is categories (Projectile, Damage, Handling / Fire, Ammunition...), each with its parts (Flight, Direct hit...).
//   flat    the category shows its one part's rows directly (no part header)
//   plain   the category is a single row: no header at all, the row alone, `prefix` before its label
//   fold    a part of a single row: shown in its category without a header, `prefix` before its label
// Keys are the HD2R Editor's ("c|<category>", "c|<category>|<part>"): a closed header stays closed on every page.
public static class EditorCatalog
{
    public const string FileName = "editor-catalog.json";
    public const int Format = 1;
    // The categories the editor shows, in the HD2R Editor's groups; more come with their fields.
    // (id, label, group, tone: the game's stratagem category colour, or none)
    static readonly (string Id, string Label, string Group, string? Tone)[] Categories =
    [
        ("primary", "Primary", "WEAPONS", null), ("secondary", "Secondary", "WEAPONS", null), ("throwables", "Throwables", "WEAPONS", null),
        ("offensive", "Offensive", "STRATAGEMS", "offensive"), ("defensive", "Defensive", "STRATAGEMS", "defensive"),
        ("support", "Support Weapons", "STRATAGEMS", "support"), ("backpacks", "Support Backpacks", "STRATAGEMS", "support"),
        ("vehicles", "Vehicles", "STRATAGEMS", "support"), ("mission", "Mission", "STRATAGEMS", "support"),
        ("boosters", "Boosters", "EQUIPMENT", null),
        ("helldiver", "Helldiver", "HELLDIVER", null), ("perks", "Armor perks", "HELLDIVER", null),
        ("statuses", "Status Effects", "EFFECTS", null),
        ("explosions", "Explosions", "ADVANCED", null), ("projectiles", "Projectiles", "ADVANCED", null), ("damage", "Damage Types", "ADVANCED", null),
    ];
    // A stratagem's settings row is the stratagem's type (mappings/catalog.json), what set_live names a row by.
    static readonly Lazy<Dictionary<string, ulong>> StratagemTypes = new(() =>
        SdkMappings.Current.Content.Where(c => c.Kind == "stratagems").ToDictionary(c => c.Name, c => c.Id, StringComparer.Ordinal));
    static readonly Lazy<IReadOnlyDictionary<string, IReadOnlyDictionary<string, ulong>>> Sprites = new(GameData.EditorIcons.Sprites);
    // The armory's weapon groups, in its order (the HD2R Editor's filter chips).
    static readonly Dictionary<string, string[]> GroupOrder = new(StringComparer.Ordinal)
    {
        ["primary"] = ["Assault Rifle", "Marksman Rifle", "Submachine Gun", "Shotgun", "Explosive", "Energy-Based", "Special"],
        ["secondary"] = ["Pistol", "Melee", "Special"],
    };
    const string Sep = " · ";

    public static JsonObject Build(StatsSchema? schema = null)
    {
        schema ??= StatsSchema.Current;
        var categories = new JsonArray();
        var objects = new JsonArray();
        foreach (var (id, label, group, tone) in Categories)
        {
            var items = StatsCatalog.Items.Where(i => i.Nav == id).ToList();
            var present = items.Select(i => i.Group ?? "").Where(g => g.Length > 0).Distinct().ToList();
            var order = GroupOrder.GetValueOrDefault(id) ?? [];
            var groups = order.Where(present.Contains).Concat(present.Where(g => !order.Contains(g)).Order(StringComparer.Ordinal)).ToList();
            categories.Add(new JsonObject
            {
                ["id"] = id, ["label"] = label, ["group"] = group, ["tone"] = tone,
                ["filters"] = GroupOrder.ContainsKey(id) ? new JsonArray(groups.Select(g => (JsonNode)g).ToArray()) : null,
            });
            foreach (var item in items) objects.Add(Object(item, id, tone, schema));
            if (id == "helldiver") objects.Add(ArmorPerks());
        }
        return new JsonObject
        {
            ["format"] = Format, ["build"] = schema.Build, ["schema"] = StatsSchema.Fingerprint, ["categories"] = categories, ["objects"] = objects,
            ["references"] = References(),
            ["hd2r"] = Hd2rValues(),
        };
    }

    // HD2R Editor presets (mappings/hd2r-keys.json): each key with the bytes it edits, how its value is stored, and the
    // ids HD2R stores for references (an attack output's id for a projectile, an explosion's catalogue id, a status's).
    sealed record Hd2rKey(string Key, string Kind, string Field)
    {
        public string Object => Key.Split('|')[1];
        // a value kind held as plain fields ("slots" in hd2r-keys.json): this field's slot and the value names
        public int Slot { get; init; } = -1;
        public JsonNode? Names { get; init; }
    }
    static readonly Lazy<JsonNode> Hd2rData = new(() =>
    {
        using var stream = typeof(EditorCatalog).Assembly.GetManifestResourceStream("HellForge.Core.mappings.hd2r-keys.json")!;
        return JsonNode.Parse(stream)!;
    });
    static readonly Lazy<Dictionary<string, List<Hd2rKey>>> Hd2rByAddress = new(() =>
    {
        var byAddress = new Dictionary<string, List<Hd2rKey>>(StringComparer.Ordinal);
        foreach (var (key, v) in Hd2rData.Value["keys"]!.AsObject())
        {
            var a = v!.AsArray();
            var address = a[0]!.GetValue<string>();
            if (!byAddress.TryGetValue(address, out var list)) byAddress[address] = list = [];
            list.Add(new Hd2rKey(key, a[1]!.GetValue<string>(), a[2]!.GetValue<string>()));
        }
        // flags, enums, armor penetration labels and slot lists (fire rates, fire modes, traits): a key on each slot
        foreach (var (key, v) in Hd2rData.Value["slots"]!.AsObject())
        {
            var addresses = v!["addresses"]!.AsArray();
            for (var i = 0; i < addresses.Count; i++)
            {
                var address = addresses[i]!.GetValue<string>();
                if (!byAddress.TryGetValue(address, out var list)) byAddress[address] = list = [];
                list.Add(new Hd2rKey(key, v["kind"]!.GetValue<string>(), v["field"]!.GetValue<string>()) { Slot = i, Names = v["names"] });
            }
        }
        return byAddress;
    });

    static JsonObject Hd2rValues()
    {
        var statuses = new JsonObject();
        foreach (var s in ReferenceData.Value["statuses"]!.AsArray()) statuses[s!["id"]!.GetValue<string>()] = s["type"]!.GetValue<long>();
        return new JsonObject
        {
            ["outputs"] = Hd2rData.Value["outputs"]!.DeepClone(), ["explosions"] = Hd2rData.Value["explosions"]!.DeepClone(), ["statuses"] = statuses,
            ["editor"] = Hd2rData.Value["editor"]!.DeepClone(), ["runtime"] = Hd2rData.Value["runtime"]!.DeepClone(),
        };
    }

    // The HD2R keys on a row's bytes, the row's own first (same item, same field id), and how their value is stored.
    static void Hd2rKeys(JsonObject json, StatsRow row, StatsItem item)
    {
        if (!Hd2rByAddress.Value.TryGetValue(row.Field.Hd2rAddress, out var keys)) return;
        var names = new HashSet<string>(item.Parts.Select(p => p.Target).Append(item.Name), StringComparer.Ordinal);
        var ordered = keys.OrderBy(k => names.Contains(k.Object) ? 0 : 1).ThenBy(k => k.Field == row.Key ? 0 : 1).ThenBy(k => k.Key, StringComparer.Ordinal).ToList();
        // bytes many items share (a melee hit volume, ~100 weapons): each item's row carries its own keys only
        if (ordered.Count > 8 && ordered.Any(k => names.Contains(k.Object))) ordered = ordered.Where(k => names.Contains(k.Object)).ToList();
        json["hd2r"] = new JsonArray(ordered.Select(k => (JsonNode)k.Key).ToArray());
        json["hd2rKind"] = ordered[0].Kind;
        // keys whose values are not the field's number (a flag, a name, a list): how each reads
        var structured = ordered.Where(k => k.Slot >= 0).ToList();
        if (structured.Count > 0)
            json["hd2rAs"] = new JsonObject(structured.Select(k => KeyValuePair.Create(k.Key, (JsonNode?)new JsonObject
            {
                ["kind"] = k.Kind, ["slot"] = k.Slot, ["names"] = k.Names?.DeepClone(),
            })));
    }

    // The references a pick row offers (mappings/references.json; HD2R Editor's pickers), each list once: projectile
    // donors by what fires them, explosions in the HD2R Editor's groups, status effects by family. No game values.
    static JsonObject References()
    {
        using var stream = typeof(EditorCatalog).Assembly.GetManifestResourceStream("HellForge.Core.mappings.references.json")!;
        var root = JsonNode.Parse(stream)!;
        JsonObject Option(long value, string label, string group, string note) => new() { ["value"] = value, ["label"] = label, ["group"] = group, ["note"] = note };
        var payloadGroups = root["payload"]!["groups"]!.AsArray().Select(g => g!).ToDictionary(g => g["id"]!.GetValue<string>(), g => g["label"]!.GetValue<string>());
        var groupOrder = payloadGroups.Values.ToList();
        static string Kind(string owner) => owner switch
        {
            "player_weapon" => "Weapons", "support_weapon" => "Support weapons", "stratagem" => "Stratagems", "vehicle_weapon" => "Vehicles",
            "mounted_weapon" => "Mounted weapons", _ => "Other",
        };
        string[] projectileGroups = ["Weapons", "Support weapons", "Stratagems", "Vehicles", "Mounted weapons", "Other"];
        var projectiles = root["projectiles"]!.AsArray().Select(p => p!).Select(p =>
        {
            var owners = p["owners"]!.AsArray().Select(o => o!.GetValue<string>()).ToList();
            var others = owners.Count > 1 ? $", also {string.Join(", ", owners.Skip(1).Take(2))}{(owners.Count > 3 ? $" +{owners.Count - 3}" : "")}" : "";
            return Option(p["type"]!.GetValue<long>(), owners[0], Kind(p["owner"]!.GetValue<string>()), $"projectile {p["type"]}{others}");
        }).OrderBy(o => Array.IndexOf(projectileGroups, o["group"]!.GetValue<string>())).ThenBy(o => o["label"]!.GetValue<string>(), StringComparer.OrdinalIgnoreCase);
        // The explosions a payload slot offers: HD2R Editor's payload options, in its groups, with its warnings.
        var explosions = root["explosions"]!.AsArray().Select(x => x!).Where(x => x["payload"] != null).Select(x =>
        {
            var notes = new List<string> { $"explosion {x["type"]}" };
            if (x["mission"]!.GetValue<bool>()) notes.Add("a mission's effects (in missions)");
            else if (x["packages"]!.AsArray().Count > 0) notes.Add("loads its package first");
            var option = Option(x["type"]!.GetValue<long>(), x["label"]!.GetValue<string>(), payloadGroups[x["payload"]!["group"]!.GetValue<string>()], string.Join(" · ", notes));
            option["warnings"] = x["payload"]!["warnings"]!.DeepClone();
            return option;
        }).OrderBy(o => groupOrder.IndexOf(o["group"]!.GetValue<string>())).ThenBy(o => o["label"]!.GetValue<string>(), StringComparer.OrdinalIgnoreCase);
        var statuses = root["statuses"]!.AsArray().Select(s => s!).Where(s => s["attachable"]!.GetValue<bool>()).Select(s => Option(s["type"]!.GetValue<long>(),
            s["label"]!.GetValue<string>(), StatsCatalog.Humanize(s["family"]!.GetValue<string>()),
            $"status {s["type"]}" + (s["tier"]?.GetValue<string>() == "player_attack" ? "" : " · unverified on a weapon (HD2R: allow_unverified_effect)")))
            .OrderBy(o => o["group"]!.GetValue<string>(), StringComparer.Ordinal)
            .ThenBy(o => o["label"]!.GetValue<string>(), StringComparer.OrdinalIgnoreCase);
        return new JsonObject
        {
            ["warnings"] = root["payload"]!["warnings"]!.DeepClone(),
            ["projectile"] = new JsonArray(projectiles.Select(o => (JsonNode)o).ToArray()),
            ["explosion"] = new JsonArray(explosions.Select(o => (JsonNode)o).ToArray()),
            ["status"] = new JsonArray(statuses.Select(o => (JsonNode)o).ToArray()),
        };
    }

    static readonly Lazy<JsonNode> ReferenceData = new(() =>
    {
        using var stream = typeof(EditorCatalog).Assembly.GetManifestResourceStream("HellForge.Core.mappings.references.json")!;
        return JsonNode.Parse(stream)!;
    });

    static readonly Lazy<Dictionary<string, JsonObject>> Ammunition = new(() =>
    {
        using var stream = typeof(EditorCatalog).Assembly.GetManifestResourceStream("HellForge.Core.mappings.references.json")!;
        return JsonNode.Parse(stream)!["ammunition"]!.AsArray().Select(a => a!.AsObject()).ToDictionary(a => a["weapon"]!.GetValue<string>(), a => a, StringComparer.Ordinal);
    });

    // What a reference row picks (projectile, explosion, status), from its mapping's "ref"; a weapon's ammunition delta
    // is its projectile.
    static string? Pick(StatsField f) => f.Kind == "EntityDelta" ? (f.Component == "321:0" ? "projectile" : null) :
        SdkMappings.Current.FieldAt(f.Kind == "component" ? f.Component : f.Kind, f.Offset, f.Storage)?.Ref is { } r && r != "damage" ? r : null;

    // The page's attack branch of a key ("" its main round), as StatsCatalog places it.
    static bool MainRound(string key)
    {
        var segments = key.Split('.');
        var middle = segments.Skip(1).Take(segments.Length - 2).Where(s => s != "damage").ToList();
        return middle.Count == 0 || middle[0] is "primary" or "feed_primary" or "impact" or "expiry" or "detonation" ||
            middle[0].StartsWith("primary_", StringComparison.Ordinal);
    }

    public static string Text(StatsSchema? schema = null) => Build(schema).ToJsonString(new JsonSerializerOptions { WriteIndented = false }) + "\n";

    // A vehicle's mount slots (HD2R Editor's "Mounted Weapons", its arm swap): a choice row per slot, kept in the
    // editor's settings and applied live (MountComponentData slot k +24k, a u64: the mounted entity), offering the weapons
    // HD2Runtime allows there (the vanilla one first), each named by its stats page where it has one. The rows of a
    // mount's own weapon say which slot holds it (mountSlot), so a swap can lock them.
    static readonly Lazy<Dictionary<string, string>> WeaponByResource = new(() =>
    {
        var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (name, t) in StatsSchema.Current.Targets["weapons"])
            foreach (var f in t.Fields.Values.Where(f => f.Kind == "component"))
                map.TryAdd(f.Resource, name);
        return map;
    });

    static JsonObject? MountCategory(StatsItem item)
    {
        var vehicle = item.Parts.FirstOrDefault(p => p.Category == "vehicles")?.Target;
        if (vehicle == null) return null;
        var slots = ReferenceData.Value["mountSlots"]!.AsArray().Select(s => s!).Where(s => s["vehicle"]!.GetValue<string>() == vehicle).ToList();
        if (slots.Count == 0) return null;
        var weapons = ReferenceData.Value["mountedWeapons"]!.AsObject();
        var ids = weapons.Select(w => w.Key).ToList();
        using var stream = typeof(EditorCatalog).Assembly.GetManifestResourceStream("HellForge.Core.mappings.vehicles.json")!;
        var mounts = JsonNode.Parse(stream)!["vehicles"]![vehicle]!["mounts"]!.AsArray().Select(m => m!).ToList();
        string Name(string id)
        {
            var w = weapons[id]!;
            var resource = w["resource"]!.GetValue<string>();
            if (!WeaponByResource.Value.TryGetValue(resource, out var page)) return StatsCatalog.Humanize(w["name"]!.GetValue<string>());
            var at = page.LastIndexOf(" / ", StringComparison.Ordinal); /* "EXO-49 Emancipator Exosuit / left_gun": its vehicle · Left gun */
            return at < 0 ? page : page[..at] + Sep + StatsCatalog.Humanize(page[(at + 3)..]);
        }
        var rows = new JsonArray();
        foreach (var s in slots)
        {
            var slot = s["slot"]!.GetValue<int>();
            var label = mounts.FirstOrDefault(m => m["slot"]?.GetValue<int>() == slot)?["label"]?.GetValue<string>() ?? $"Slot {slot}";
            var options = new JsonArray();
            var first = true;
            foreach (var id in s["allowed"]!.AsArray().Select(a => a!.GetValue<string>()))
            {
                var w = weapons[id]!;
                var resource = w["resource"]!.GetValue<string>();
                options.Add(new JsonObject
                {
                    ["value"] = ids.IndexOf(id), ["label"] = Name(id) + (first ? " (vanilla)" : ""), ["resource"] = resource,
                    ["note"] = (WeaponByResource.Value.TryGetValue(resource, out var page) ? "stats editable: " + page : "stats not editable") +
                        (w["package"] != null ? " · loads its package first" : ""),
                    ["target"] = page, // its stats page: shown under the slot while it is swapped in
                });
                first = false;
            }
            AddSlot(slot, s["untested"] != null ? "Shield arm" : label, s["resource"]!.GetValue<string>(), options, s["untested"]?.GetValue<string>());
        }
        void AddSlot(int slot, string label, string resource, JsonArray options, string? warning)
        {
            rows.Add(new JsonObject
            {
                ["label"] = label, ["cat"] = "mounts", ["target"] = vehicle, ["key"] = $"mount.slot_{slot}", ["choice"] = "mount",
                ["component"] = "MountComponentData", ["ref"] = resource, ["offset"] = slot * 24, ["type"] = "u64",
                ["min"] = null, ["max"] = null, ["unit"] = null, ["applies"] = "respawn", ["verified"] = false, ["unverified"] = true,
                ["shared"] = false, ["also"] = null, ["link"] = $"mount|{vehicle}|{slot}",
                ["note"] = (warning != null ? warning + " " : "") +
                    "Swaps the weapon this slot holds (HD2R Editor's arm swap): vehicles called in after it carry it. Firing and look of a swapped mount are not proven.",
                ["options"] = options,
            });
        }
        var part = new JsonObject { ["title"] = "Mounted Weapons", ["key"] = "c|mounts|Mounted Weapons", ["fold"] = false, ["prefix"] = null, ["rows"] = rows };
        return new JsonObject { ["title"] = "Mounted Weapons", ["key"] = "c|mounts", ["flat"] = true, ["plain"] = false, ["prefix"] = null, ["subs"] = new JsonArray(part) };
    }

    // The mount slot holding a vehicle page's weapon part (its own weapon), or null.
    static string? MountSlotOf(StatsItem item, StatsPart part)
    {
        var vehicle = item.Parts.FirstOrDefault(p => p.Category == "vehicles")?.Target;
        if (vehicle == null || part.Category != "weapons") return null;
        var resources = StatsSchema.Current.Targets["weapons"][part.Target].Fields.Values.Where(f => f.Kind == "component").Select(f => f.Resource)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        var weapons = ReferenceData.Value["mountedWeapons"]!.AsObject();
        var slot = ReferenceData.Value["mountSlots"]!.AsArray().Select(s => s!).FirstOrDefault(s => s["vehicle"]!.GetValue<string>() == vehicle &&
            resources.Contains(weapons[s["default"]!.GetValue<string>()]?["resource"]?.GetValue<string>() ?? ""));
        return slot == null ? null : $"mount.slot_{slot["slot"]!.GetValue<int>()}";
    }

    static JsonObject Object(StatsItem item, string category, string? tone, StatsSchema schema)
    {
        var main = item.Parts[0];
        var page = new JsonArray();
        foreach (var section in StatsCatalog.Sections(item, schema))
        {
            var classified = !section.Key.Contains(':');
            var rows = section.Rows.ToList();
            // an attachment category keeps every attachment as its own group, one field or many (HD2R Editor's)
            var attachments = section.Key.StartsWith("att:", StringComparison.Ordinal) || section.Key == "melee" ||
                section.Rows.Any(r => r.Key.StartsWith("attachment.", StringComparison.Ordinal)); /* magazines inside Ammunition */
            var flat = section.Groups.Count == 1 && !attachments; /* a single group: its rows, no sub-header (Hellpod / Call-in) */
            var plain = rows.Count == 1 && !attachments;
            string? prefix = null;
            if (plain)
            {
                var g = section.Groups[0];
                var own = FoldPrefix(g.Title, rows[0].Label);
                prefix = classified ? (own != null && g.Title != section.Title ? own : FoldPrefix(section.Title, rows[0].Label)) : own;
            }
            var key = "c|" + section.Key;
            var subs = new JsonArray();
            foreach (var g in section.Groups)
            {
                var fold = g.Rows.Count == 1 && !flat && !plain && !attachments;
                subs.Add(new JsonObject
                {
                    ["title"] = g.Title, ["key"] = key + "|" + g.Title, ["fold"] = fold,
                    ["prefix"] = fold ? FoldPrefix(g.Title, g.Rows[0].Label) : null,
                    ["rows"] = new JsonArray(g.Rows.Select(r => (JsonNode)WithMount(Row(r, item), item, r)).ToArray()),
                });
            }
            page.Add(new JsonObject { ["title"] = section.Title, ["key"] = key, ["flat"] = flat, ["plain"] = plain, ["prefix"] = prefix, ["subs"] = subs });
        }
        if (MountCategory(item) is { } mountCategory)
            page.Insert(page.Count > 0 && page[0]!["key"]!.GetValue<string>() == "c|call_in" ? 1 : 0, mountCategory);
        // A weapon HD2R Editor offers no projectile swap for: why (shown on its page).
        var refused = item.Parts.Where(p => p.Category == "weapons").Select(p => ReferenceData.Value["refused"]![p.Target]).FirstOrDefault(r => r != null);
        return new JsonObject
        {
            ["id"] = item.Id, ["name"] = item.Name, ["category"] = category, ["subtitle"] = item.Group,
            ["icon"] = Icon(item, main), ["tone"] = tone, ["search"] = item.Search, ["page"] = page,
            ["notOffered"] = refused?.GetValue<string>(),
            // an armor perk: its passive's icon (by the game's name), and what it does (HD2R Editor's "Perk details")
            ["sprite"] = item.Nav == "perks" ? PerkSprite(item.Name) : null,
            ["detail"] = item.Nav == "perks" ? PerkDetail(item, page.Count) : null,
        };
    }

    static string? PerkSprite(string title)
    {
        using var stream = typeof(EditorCatalog).Assembly.GetManifestResourceStream("HellForge.Core.mappings.passives.json")!;
        return JsonNode.Parse(stream)!["passives"]!.AsArray().FirstOrDefault(p => p!["title"]!.GetValue<string>() == title)?["name"]!.GetValue<string>();
    }

    static string PerkDetail(StatsItem item, int categories)
    {
        var target = StatsSchema.Current.Targets["perks"][item.Name];
        return (target.Class ?? "") + (categories == 0 ? "   No numeric gameplay fields mapped for this perk." : "");
    }

    // The armor perks (HD2R Editor's "Armor perks", SDK set_passives): the local player's armor passive and a second passive
    // on top, two choice rows; -1 is the kits' own (HF_PASSIVE_KIT). Passives whose effect needs a package HellForge does
    // not load are not offered (mappings/passives.json), nor STANDARD ISSUE as the second (it gives nothing).
    static JsonObject ArmorPerks()
    {
        using var stream = typeof(EditorCatalog).Assembly.GetManifestResourceStream("HellForge.Core.mappings.passives.json")!;
        var passives = JsonNode.Parse(stream)!["passives"]!.AsArray().Select(p => p!.AsObject())
            .Where(p => p["package"] == null).OrderBy(p => p["title"]!.GetValue<string>(), StringComparer.Ordinal).ToList();
        JsonArray Options(string none, bool second) => new(new JsonNode?[] { new JsonObject { ["value"] = -1, ["label"] = none } }.Concat(
            passives.Where(p => !(second && (p["id"]!.GetValue<int>() == 0 || p["armorOnly"]!.GetValue<bool>()))).Select(p => (JsonNode)new JsonObject
            {
                ["value"] = p["id"]!.GetValue<int>(), ["label"] = p["title"]!.GetValue<string>(), ["icon"] = p["name"]!.GetValue<string>(),
                ["note"] = p["description"]!.GetValue<string>() + (p["follows"]!.GetValue<string>() == "partial" ? " (Some of it applies only at spawn.)" : ""),
            })).ToArray());
        JsonObject Choice(string choice, string label, string none, bool second, string note) => new()
        {
            ["label"] = label, ["cat"] = "", ["target"] = "", ["key"] = "passives." + choice, ["choice"] = choice, ["component"] = null,
            ["ref"] = null, ["offset"] = 0, ["type"] = "i32", ["min"] = null, ["max"] = null, ["unit"] = null, ["applies"] = "now",
            ["verified"] = false, ["unverified"] = true, ["shared"] = false, ["also"] = null, ["link"] = "passives." + choice,
            ["note"] = note, ["options"] = Options(none, second),
            ["hd2r"] = new JsonArray("hd|Armor perks|" + choice), ["hd2rKind"] = "passive",
        };
        var rows = new JsonArray(
            Choice("armor", "Armor passive", "The armor's own", false, "Your own armor, solo only; follows armor changes"),
            Choice("second", "Second passive", "None", true, "Added to the armor passive (no stacking of the same bonus)"));
        var part = new JsonObject { ["title"] = "Armor perks", ["key"] = "c|perks|Armor perks", ["fold"] = false, ["prefix"] = null, ["rows"] = rows };
        var category = new JsonObject
        {
            ["title"] = "Armor perks", ["key"] = "c|perks", ["flat"] = true, ["plain"] = false, ["prefix"] = null, ["subs"] = new JsonArray(part),
        };
        return new JsonObject
        {
            ["id"] = "helldiver/armor-perks", ["name"] = "Armor perks", ["category"] = "helldiver", ["subtitle"] = "Your Helldiver",
            ["icon"] = null, ["tone"] = null, ["search"] = "armor perks passive passives helldiver", ["page"] = new JsonArray(category),
        };
    }

    static JsonObject WithMount(JsonObject row, StatsItem item, StatsRow r)
    {
        if (MountSlotOf(item, r.Part) is { } slot) row["mountSlot"] = slot;
        return row;
    }

    // Which of the game's icons the object shows (by its name in that group of mappings/hud-icons.json), or none.
    static string? Icon(StatsItem item, StatsPart main)
    {
        if (item.Id.StartsWith("stratagems/", StringComparison.Ordinal)) return Sprites.Value["stratagems"].ContainsKey(item.Name) ? "stratagem" : null;
        if (item.Id.StartsWith("boosters/", StringComparison.Ordinal)) return Sprites.Value["boosters"].ContainsKey(item.Name) ? "booster" : null;
        if (item.Id.StartsWith("perks/", StringComparison.Ordinal)) return "passive";
        if (main.Category == "throwables") return "throwable";
        return main.Category == "weapons" && Sprites.Value["weapons"].ContainsKey(item.Name) ? "weapon" : null;
    }

    // The part's title before a folded row's label, unless the label starts with its first word already
    // ("Ammunition · Spare magazines", but "Recoil" alone under "Recoil").
    static string? FoldPrefix(string title, string label)
    {
        var word = title.Split(' ', StringSplitOptions.RemoveEmptyEntries).FirstOrDefault()?.ToLowerInvariant() ?? "";
        return word.Length > 0 && !label.ToLowerInvariant().StartsWith(word, StringComparison.Ordinal) ? title : null;
    }

    static JsonObject Row(StatsRow row, StatsItem item)
    {
        var f = row.Field;
        var applies = LiveApplies.Of(f);
        var type = f.LiveId ?? (f.Kind == "StratagemDefinition" && StratagemTypes.Value.TryGetValue(row.Part.Target, out var t) ? t : (ulong?)null);
        var live = applies != null && (f.Kind != "StratagemDefinition" || type != null);
        var also = StatsCatalog.AlsoChanges(row, item.Nav is "projectiles" or "damage");
        var json = new JsonObject
        {
            ["label"] = row.Label, ["cat"] = row.Part.Category, ["target"] = row.Part.Target, ["key"] = row.Key,
            // set_live / get_live: the component record (or settings table) and the target, an entity's resource id or a row id
            ["component"] = live ? (f.Kind == "component" ? f.Component : f.Kind) : null,
            ["ref"] = live ? (f.Kind == "component" ? f.Resource : (type ?? (ulong)f.Record).ToString(CultureInfo.InvariantCulture)) : null,
            ["offset"] = f.Offset, ["type"] = f.Storage,
            ["min"] = f.Min, ["max"] = f.Max, ["unit"] = StatsCatalog.Unit(f.Unit) is { Length: > 0 } unit ? unit : null,
            ["applies"] = live ? applies!.When : "boot", ["verified"] = live && applies!.Verified, ["unverified"] = f.Unverified,
            ["shared"] = f.Shared, // set_live with HF_LIVE_SHARED, as the launcher sends it
            ["also"] = also.Count > 0 ? new JsonArray(also.Select(a => (JsonNode)a).ToArray()) : null,
            ["link"] = StatsCatalog.LinkKey(f),
        };
        // get_stat: a field get_live cannot read (entity deltas, say), by the address the session's stats snapshot reads
        if (!live) json["stat"] = new JsonArray(f.Kind, f.Component, f.Resource, f.Record);
        // a field with named values (a flag, an enum, a trait or fire mode slot): picked from them
        if (f.Options.ValueKind == JsonValueKind.Array)
            json["values"] = new JsonArray(f.Choices.Select(c => (JsonNode)new JsonObject { ["value"] = c.Value, ["label"] = c.Label }).ToArray());
        // References (HD2R Editor's payload swaps): what the row picks; a status slot's number (its strength's too) and
        // the slot group; the rows a projectile swap locks (the weapon's own round), named by the swap's key.
        var pick = Pick(f);
        if (pick != null) json["pick"] = pick;
        if (StatsCatalog.Unlimited(f)) { json["unlimited"] = true; json["label"] = row.Label + " (next launch)"; } /* UINT32_MAX: unlimited, -1 in the stats file */
        var slot = System.Text.RegularExpressions.Regex.Match(row.Key, @"^(.*?)status_([1-4])_(type|strength)$");
        if (slot.Success && f.Kind == "DamageInfo") { json["slot"] = int.Parse(slot.Groups[2].Value, CultureInfo.InvariantCulture); json["slots"] = slot.Groups[1].Value; }
        if (pick == "explosion" && ReferenceData.Value["payload"]!["excluded"]![$"{row.Part.Category}/{row.Part.Target}/{row.Key}"] is JsonArray excluded)
            json["exclude"] = excluded.DeepClone();
        if (row.Key == "function_ammo.projectile" && ReferenceData.Value["programmable"]![row.Part.Target] is { } programmed)
            json["note"] = "While programmable ammunition is on, this projectile replaces the fired one. " + programmed["note"]!.GetValue<string>();
        if (f.Kind == "EntityDelta" && pick == "projectile" && Ammunition.Value.TryGetValue(row.Part.Target, out var ammunition))
        {
            json["base"] = ammunition["projectile"]!.GetValue<long>();
            json["note"] = $"Swapped on the default ammunition ({ammunition["item"]}), which every weapon that equips it shares";
        }
        if (f.Note != null && json["note"] == null) json["note"] = f.Note;
        Hd2rKeys(json, row, item);
        var swap = SwapKey(row.Part);
        if (swap != null && row.Key != swap && f.Kind is "ProjectileSettings" or "DamageInfo" or "ExplosionSettings" or "StatusEffectSettings" or "ArcSettings" &&
            MainRound(row.Key))
            json["fires"] = swap;
        return json;
    }

    // The key of the part's projectile swap row, if it has one.
    static string? SwapKey(StatsPart part)
    {
        var fields = StatsSchema.Current.Targets[part.Category][part.Target].Fields;
        return new[] { "attack.primary.projectile", "attack.feed_primary.projectile", "ammunition.projectile" }.FirstOrDefault(fields.ContainsKey);
    }
}

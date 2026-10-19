using System.Globalization;
using System.Text;
using System.Text.Json;

namespace HellForge.Core;

// The SDK's mappings (docs/sdk-roadmap.md "Mappings"): mappings/components.json names the fields of component records,
// mappings/catalog.json names vanilla content. Both generate the typed C headers mods include and the loader's table of
// reviewed fields (hellforge sdk-headers), so a mod's field and the loader's check come from one entry.
public sealed record SdkField(string Name, int Offset, string Type, int Count, string Meaning, bool Verified, string Source)
{
    public int Size => Count * SdkMappings.TypeSize(Type);
    // When a change to it takes effect in a running game (live editing): now, respawn or mission.
    public string Applies { get; init; } = "respawn";
    public bool AppliesVerified { get; init; }
    // A reference to another table's row (projectile, explosion, status, damage): the loader takes only a row of that
    // table as its value (or 0 where the field allows none), and loads the package the row needs first.
    public string? Ref { get; init; }
}
// A named set of fields copied or set together (HF_COPY_GROUP, HF_SET_GROUP): "the firing sounds".
public sealed record SdkGroup(string Name, string Meaning, IReadOnlyList<string> Fields);
// An entity component record (Index: its component number), or a settings table row (Table; Index -1), found by its id
// (a projectile type, a damage id...). Row names the HfApi function that returns one, when there is one (difficulty_row).
public sealed record SdkComponent(string Name, string Component, int Index, int Stride, uint Layout, string Meaning,
    IReadOnlyList<SdkField> Fields, IReadOnlyList<SdkGroup> Groups)
{
    public string? Row { get; init; }
    public bool Table { get; init; }
}
public sealed record SdkContent(string Kind, string Name, string Constant, ulong Id, string Note);

public sealed class SdkMappings
{
    public const string ComponentsFile = "components.json";
    public const string CatalogFile = "catalog.json";
    public static readonly string[] HeaderFiles =
        ["native/mod-sdk/hellforge_components.h", "native/mod-sdk/hellforge_content.h", "native/mod-loader/sdk_fields.h",
         "native/mod-loader/sdk_content.h", "docs/sdk-reference.md"];

    // Content kinds, in header order: (JSON key, constant prefix, C type of the ids, what the ids are for).
    static readonly (string Key, string Prefix, bool Wide, string Doc)[] Kinds =
    [
        ("items", "HF_ITEM_", false, "Loadout ids of vanilla items, for HfWeapon.base, sound_from and HfStratagem.loads_item. Every session with a\n * weapon added lists the whole arsenal (loadout id, armory type, entity, designation, name) in its log folder as arsenal.tsv."),
        ("projectiles", "HF_PROJECTILE_", false, "Vanilla projectile types, for HfProjectile.base and visual_from (HD2Runtime research projectile-identities)."),
        ("stratagems", "HF_STRATAGEM_", false, "Vanilla stratagem types, for HfStratagem.base (the game's StratagemType enum)."),
        ("behaviours", "HF_BEHAVIOUR_", false, "Vanilla behaviours, for HfBehaviour.base, HfEntity.behaviour and HfEntityDef.behaviour_id."),
        ("entities", "HF_ENTITY_", true, "Vanilla entities, for HfEntity.base, HfEntityDef.base, model_from and HfCopy.from (entity resource ids)."),
        ("damage", "HF_DAMAGE_", false, "Vanilla damage ids, for HfDamage.base and HfProjectile.damage (the projectile row's +0x3C)."),
        ("packages", "HF_PACKAGE_", true, "Vanilla packages, for HfStratagem.package (the resource ids LoadoutPackage records name)."),
        ("tags", "HF_TAG_", true, "Entity tags, for HfEntityDef.add_tags and remove_tags (TagComponentData)."),
        ("traits", "HF_TRAIT_", false, "Stratagem traits, for HfStratagem.flags (row +0x104, the game's StratagemInfoFlag bits; OR them together).\n * Bits 13 and 15 are unnamed so far; HF_TRAIT_VEHICLE allows one per loadout."),
        ("categories", "HF_CATEGORY_", false, "Loadout sections and ping colours, for HfStratagem.category (row +0xB8 + 1)."),
        ("beams", "HF_BEAM_", false, "Beacon beam colours, for HfStratagem.beam (row +0xD4)."),
        ("boosters", "HF_BOOSTER_", false, "Boosters, for the BoosterSettings table (set_live, get_live): a row is the game's Booster value."),
    ];

    // hellforge_mod.h's own Hf names: a component record struct can't take one.
    static readonly HashSet<string> Reserved = ["Mod", "Event", "EventCallback", "Weapon", "Entity", "Behaviour", "Damage",
        "Stratagem", "Projectile", "FieldType", "Difficulty", "Api", "Ref", "Field", "Value", "Copy", "Set", "MountDef", "EntityDef"];

    public required string Build { get; init; }
    public required IReadOnlyList<SdkComponent> Components { get; init; }
    public required IReadOnlyList<SdkContent> Content { get; init; }

    public static readonly string[] AppliesValues = ["now", "respawn", "mission"];
    public static readonly string[] RefValues = ["projectile", "explosion", "status", "damage", "mount", "pickup"];

    // The field a stats-schema field is (by component or table kind, offset and storage): itself, or one element of an
    // array field of that type. Null if the mappings do not have it.
    public SdkField? FieldAt(string component, int offset, string storage)
    {
        var c = Components.FirstOrDefault(c => c.Component == component);
        return c?.Fields.FirstOrDefault(f => f.Type == storage && offset >= f.Offset && offset < f.Offset + f.Size &&
            (offset - f.Offset) % TypeSize(f.Type) == 0);
    }

    static readonly Lazy<SdkMappings> CurrentMappings = new(() =>
    {
        string Resource(string name)
        {
            using var stream = typeof(SdkMappings).Assembly.GetManifestResourceStream("HellForge.Core.mappings." + name)!;
            return new StreamReader(stream).ReadToEnd();
        }
        return Parse(Resource(ComponentsFile), Resource(CatalogFile));
    });
    // The mappings this build of HellForge was made with (embedded from mappings/).
    public static SdkMappings Current => CurrentMappings.Value;

    public static int TypeSize(string type) => type switch
    {
        "u8" or "bytes" => 1, "u16" => 2, "u32" or "i32" or "f32" => 4, "u64" => 8,
        _ => throw new InvalidDataException($"Unknown field type {type}.")
    };

    public static SdkMappings Load(string folder)
    {
        try { return Parse(File.ReadAllText(Path.Combine(folder, ComponentsFile)), File.ReadAllText(Path.Combine(folder, CatalogFile))); }
        catch (JsonException error) { throw new InvalidDataException($"mappings: {error.Message}"); }
    }

    public static SdkMappings Parse(string componentsJson, string catalogJson)
    {
        using var components = JsonDocument.Parse(componentsJson);
        using var catalog = JsonDocument.Parse(catalogJson);
        var root = components.RootElement;
        Allowed(root, "mappings", "format", "build", "components");
        if (Int(root, "format") != 1) throw new InvalidDataException("mappings/components.json: format must be 1.");
        var build = Text(root, "build");
        var list = new List<SdkComponent>();
        foreach (var entry in Property(root, "components").EnumerateObject())
        {
            var where = $"component {entry.Name}";
            Identifier(entry.Name, where);
            if (Reserved.Contains(entry.Name)) throw new InvalidDataException($"{where}: Hf{entry.Name} is an SDK type already.");
            var c = entry.Value;
            Allowed(c, where, "component", "index", "row", "table", "stride", "layout", "meaning", "applies", "appliesVerified", "appliesSource",
                "fields", "groups");
            var row = c.TryGetProperty("row", out _) ? Text(c, "row") : null;
            if (row != null) Identifier(row, where);
            var table = row != null || (c.TryGetProperty("table", out _) && Bool(c, "table"));
            var applies = Applies(c, where, null);
            var appliesVerified = c.TryGetProperty("appliesVerified", out _) && Bool(c, "appliesVerified");
            var stride = Int(c, "stride");
            var fields = new List<SdkField>();
            foreach (var f in Property(c, "fields").EnumerateObject())
            {
                var at = $"{entry.Name}.{f.Name}";
                Identifier(f.Name, at);
                if (f.Name.StartsWith("_unknown", StringComparison.Ordinal)) throw new InvalidDataException($"{at}: _unknown names are generated.");
                var v = f.Value;
                Allowed(v, at, "offset", "type", "count", "meaning", "verified", "source", "applies", "appliesVerified", "appliesSource", "ref");
                var type = Text(v, "type");
                TypeSize(type);
                var count = v.TryGetProperty("count", out _) ? Int(v, "count") : 1;
                var field = new SdkField(f.Name, Int(v, "offset"), type, count, Text(v, "meaning"), Bool(v, "verified"), Text(v, "source"))
                {
                    Applies = Applies(v, at, applies),
                    AppliesVerified = v.TryGetProperty("applies", out _) ? v.TryGetProperty("appliesVerified", out _) && Bool(v, "appliesVerified") : appliesVerified,
                    Ref = v.TryGetProperty("ref", out _) ? Text(v, "ref") : null
                };
                if (field.Ref != null && (!RefValues.Contains(field.Ref) || type != (field.Ref is "mount" or "pickup" ? "u64" : "u32") || count != 1))
                    throw new InvalidDataException($"{at}: ref must be projectile, explosion, status or damage on a single u32, or mount on a u64.");
                if (count < 1 || field.Offset < 0 || field.Offset + field.Size > stride || field.Offset % Math.Min(TypeSize(type), 4) != 0)
                    throw new InvalidDataException($"{at}: outside the {stride}-byte record or misaligned.");
                fields.Add(field);
            }
            fields.Sort((a, b) => a.Offset.CompareTo(b.Offset));
            for (var i = 1; i < fields.Count; i++)
                if (fields[i].Offset < fields[i - 1].Offset + fields[i - 1].Size)
                    throw new InvalidDataException($"{entry.Name}: {fields[i - 1].Name} and {fields[i].Name} overlap.");
            var groups = new List<SdkGroup>();
            if (c.TryGetProperty("groups", out var groupsElement))
                foreach (var g in groupsElement.EnumerateObject())
                {
                    var at = $"{entry.Name} group {g.Name}";
                    Identifier(g.Name, at);
                    Allowed(g.Value, at, "meaning", "fields");
                    var names = Property(g.Value, "fields").EnumerateArray().Select(n => n.GetString() ?? "").ToList();
                    if (names.Count == 0 || names.Distinct().Count() != names.Count || names.Any(n => fields.All(f => f.Name != n)))
                        throw new InvalidDataException($"{at}: its fields must be distinct fields of {entry.Name}.");
                    groups.Add(new SdkGroup(g.Name, Text(g.Value, "meaning"), names));
                }
            list.Add(new SdkComponent(entry.Name, Text(c, "component"), table ? -1 : Int(c, "index"), stride,
                uint.Parse(Text(c, "layout"), CultureInfo.InvariantCulture), Text(c, "meaning"), fields, groups) { Row = row, Table = table });
        }

        var croot = catalog.RootElement;
        Allowed(croot, "catalog", ["format", "build", .. Kinds.Select(k => k.Key)]);
        if (Int(croot, "format") != 1 || Text(croot, "build") != build) throw new InvalidDataException("mappings/catalog.json: format 1, the components' build.");
        var content = new List<SdkContent>();
        var constants = new HashSet<string>(StringComparer.Ordinal);
        foreach (var kind in Kinds)
        {
            if (!croot.TryGetProperty(kind.Key, out var section)) continue;
            foreach (var item in section.EnumerateObject())
            {
                var at = $"{kind.Key} \"{item.Name}\"";
                Allowed(item.Value, at, "constant", "id", "note");
                var constant = item.Value.TryGetProperty("constant", out _) ? Text(item.Value, "constant") : Constant(item.Name);
                if (constant.Length == 0 || !constant.All(ch => ch is >= 'A' and <= 'Z' or >= '0' and <= '9' or '_') || !constants.Add(kind.Prefix + constant))
                    throw new InvalidDataException($"{at}: constant {constant} is not a unique C name.");
                var idText = Text(item.Value, "id");
                var id = idText.StartsWith("0x", StringComparison.Ordinal)
                    ? ulong.Parse(idText[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture)
                    : ulong.Parse(idText, CultureInfo.InvariantCulture);
                if (!kind.Wide && id > uint.MaxValue) throw new InvalidDataException($"{at}: id is wider than 32 bits.");
                content.Add(new SdkContent(kind.Key, item.Name, constant, id, item.Value.TryGetProperty("note", out _) ? Text(item.Value, "note") : ""));
            }
        }
        return new SdkMappings { Build = build, Components = list, Content = content };
    }

    // "MG-206 Heavy Machine Gun" -> MG206_HEAVY_MACHINE_GUN, as the SDK's constants have always been named.
    public static string Constant(string name)
    {
        var text = new StringBuilder();
        foreach (var ch in name.Replace("-", "").Replace("/", "").ToUpperInvariant())
            if (ch is >= 'A' and <= 'Z' or >= '0' and <= '9') text.Append(ch);
            else if (text.Length > 0 && text[^1] != '_') text.Append('_');
        return text.ToString().Trim('_');
    }

    // The generated files, by repository-relative path.
    // The generated files, by repository-relative path. The reference documents the API from hellforge_mod.h (apiHeader:
    // its text; without it, the reference has the mappings only).
    public IReadOnlyDictionary<string, string> Generate(string? apiHeader = null) => new Dictionary<string, string>
    {
        [HeaderFiles[0]] = ComponentsHeader(),
        [HeaderFiles[1]] = ContentHeader(),
        [HeaderFiles[2]] = LoaderTable(),
        [HeaderFiles[3]] = LoaderContent(),
        [HeaderFiles[4]] = Reference(apiHeader),
    };

    // The generated files the mod loader includes (native/mod-loader/...): they belong to the engine's repository.
    static bool ForLoader(string path) => path.StartsWith("native/mod-loader/", StringComparison.Ordinal);

    // Writes the generated files under the repository root, and the loader's under the engine's (when given; skipped
    // otherwise); with check, only lists the ones that differ.
    public static IReadOnlyList<string> Sync(string repository, bool check, string? engine = null)
    {
        var stale = new List<string>();
        // Line endings as git checks them out (CRLF on Windows with core.autocrlf): compared and parsed as LF.
        var api = File.ReadAllText(Path.Combine(repository, "native", "mod-sdk", "hellforge_mod.h")).ReplaceLineEndings("\n");
        foreach (var (path, text) in Load(Path.Combine(repository, "mappings")).Generate(api))
        {
            if (ForLoader(path) && engine == null) continue;
            var file = Path.Combine(ForLoader(path) ? engine! : repository, path);
            if (File.Exists(file) && File.ReadAllText(file).ReplaceLineEndings("\n") == text.ReplaceLineEndings("\n")) continue;
            stale.Add(path);
            if (!check) File.WriteAllText(file, text);
        }
        return stale;
    }

    const string Generated = "Generated by `hellforge sdk-headers` from mappings/; do not edit (docs/sdk-roadmap.md \"Mappings\").";

    static string CType(string type) => type switch
    {
        "u8" or "bytes" => "uint8_t", "u16" => "uint16_t", "u32" => "uint32_t", "i32" => "int32_t", "f32" => "float", "u64" => "uint64_t",
        _ => throw new InvalidDataException(type)
    };
    static string TypeTag(string type) => "HF_TYPE_" + type.ToUpperInvariant();

    string ComponentsHeader()
    {
        var s = new StringBuilder();
        s.Append($$"""
            /* HellForge mod SDK: component records with named fields, for game build {{Build}}.
             * {{Generated}}
             *
             * One packed struct per component record: named fields at their offsets, unresearched bytes as _unknown_XX
             * padding. Use HF_FIELD(Component, field) in definition blocks and hf_record(mod, ref, HfComponent) for a typed
             * pointer to an entity's own record (hellforge_mod.h). Fields marked unverified are named from research but not
             * yet confirmed in the game. Each component's layout is the game build its offsets were reviewed on: the
             * loader refuses fields of a layout it does not have, so a mod built against another layout is reported, not
             * run. Each field's [tag] says when a change applies in a running game (set_live): [now] the next time the game
             * reads it, [respawn] when the entity is next made (spawn, re-equip, call-in), [mission] when a mission loads;
             * a "?" means it is inferred, not yet confirmed. Included by hellforge_mod.h. */
            #ifndef HELLFORGE_COMPONENTS_H
            #define HELLFORGE_COMPONENTS_H

            #define HF_MAPPINGS_BUILD "{{Build}}"

            #pragma pack(push, 1)

            """);
        foreach (var c in Components)
        {
            if (c.Table)
                s.Append($"\n/* {c.Component} ({c.Stride}-byte table rows, found by id{(c.Row != null ? $"; api->{c.Row} returns one: (Hf{c.Name}*)api->{c.Row}(...)" : "")}): {c.Meaning} */\n");
            else s.Append($"\n/* {c.Component} (component {c.Index}, {c.Stride}-byte records): {c.Meaning} */\n");
            s.Append($"#define Hf{c.Name}_INFO \"{c.Component}\", {c.Layout}u\n");
            s.Append($"typedef struct Hf{c.Name}\n{{\n");
            var at = 0;
            foreach (var f in c.Fields)
            {
                if (f.Offset > at) s.Append($"    uint8_t _unknown_{at:X2}[{f.Offset - at}];\n");
                var declaration = $"    {CType(f.Type)} {f.Name}{(f.Count > 1 || f.Type == "bytes" ? $"[{f.Count}]" : "")};";
                s.Append($"{declaration.PadRight(48)} /* +0x{f.Offset:X2} {f.Meaning}{(f.Verified ? "" : " (unverified)")} [{f.Applies}{(f.AppliesVerified ? "" : "?")}] */\n");
                at = f.Offset + f.Size;
            }
            if (c.Stride > at) s.Append($"    uint8_t _unknown_{at:X2}[{c.Stride - at}];\n");
            s.Append($"}} Hf{c.Name};\n");
            s.Append($"HF_ASSERT_SIZE(Hf{c.Name}, {c.Stride});\n");
            foreach (var f in c.Fields)
            {
                s.Append($"HF_ASSERT_OFFSET(Hf{c.Name}, {f.Name}, {f.Offset});\n");
                s.Append($"#define HF_TYPE_{c.Name}_{f.Name} {TypeTag(f.Type)}\n");
            }
            foreach (var g in c.Groups)
                s.Append($"/* Group {g.Name}: {g.Meaning} */\n#define HF_GROUP_{c.Name}_{g.Name}(X, ...) " +
                    string.Join(", ", g.Fields.Select(f => $"X({c.Name}, {f}, __VA_ARGS__)")) + "\n");
        }
        s.Append("\n#pragma pack(pop)\n\n#endif\n");
        return s.ToString().ReplaceLineEndings("\n");
    }

    string ContentHeader()
    {
        var s = new StringBuilder();
        s.Append($$"""
            /* HellForge mod SDK: vanilla content by name, for game build {{Build}}.
             * {{Generated}}
             * Each constant's comment starts with the content's catalog name (mappings/catalog.json). Included by
             * hellforge_mod.h. */
            #ifndef HELLFORGE_CONTENT_H
            #define HELLFORGE_CONTENT_H

            /* Content kinds, for find (HfApi): find(HF_KIND_ENTITY, "Pelican Transport"). */

            """);
        for (var i = 0; i < Kinds.Length; i++) s.Append($"#define {KindConstant(Kinds[i].Prefix)} {i + 1}u\n");
        foreach (var kind in Kinds)
        {
            var items = Content.Where(c => c.Kind == kind.Key).ToList();
            if (items.Count == 0) continue;
            s.Append($"\n/* {kind.Doc} */\n");
            foreach (var item in items)
            {
                var value = kind.Wide ? $"0x{item.Id:X16}ull" : item.Id > 0xFFFF || kind.Key == "traits" ? $"0x{item.Id:X8}u" : $"{item.Id}u";
                var line = $"#define {kind.Prefix}{item.Constant} {value}";
                s.Append($"{line.PadRight(56)} /* {item.Name}{(item.Note.Length > 0 ? " - " + item.Note : "")} */\n");
            }
        }
        s.Append("\n#endif\n");
        return s.ToString().ReplaceLineEndings("\n");
    }

    // The SDK reference (docs/sdk-reference.md): every API call with its comment, every component and table with its
    // fields, and every vanilla content constant.
    string Reference(string? apiHeader)
    {
        var s = new StringBuilder();
        s.Append($"""
            # HellForge mod SDK reference (game build {Build})

            {Generated.Replace("`hellforge sdk-headers`", "`HellForge.Cli sdk-headers`")} It comes from
            `native/mod-sdk/hellforge_mod.h` (the API, as documented there) and `mappings/` (fields and content). For a first
            mod, see [first-mod.md](first-mod.md); for how each part works, [mods.md](mods.md).

            - [API](#api) · [Components](#components) · [Settings tables](#settings-tables) · [Content](#content)


            """);
        if (apiHeader != null)
        {
            s.Append("## API\n\nThe `HfApi` table a mod's `hellforge_mod_init(const HfApi* hf, HfMod* mod)` receives. Functions are only ever added at the\nend; a mod checks `hf->version`.\n");
            foreach (var line in ApiReference(apiHeader)) s.Append(line).Append('\n');
            s.Append('\n');
        }
        string Applies(SdkField f) => f.Applies + (f.AppliesVerified ? "" : "?");
        void Component(SdkComponent c)
        {
            s.Append($"\n### Hf{c.Name}\n\n`{c.Component}`: {(c.Table ? $"{c.Stride}-byte rows found by id" + (c.Row != null ? $", `api->{c.Row}`" : "") : $"component {c.Index}, {c.Stride}-byte records")}. {c.Meaning}.\n\n");
            if (c.Fields.Count == 0) { s.Append("No fields mapped yet.\n"); return; }
            s.Append("| Field | Offset | Type | Applies | Meaning |\n|---|---|---|---|---|\n");
            foreach (var f in c.Fields)
                s.Append($"| `{f.Name}` | +0x{f.Offset:X2} | {f.Type}{(f.Count > 1 || f.Type == "bytes" ? $"[{f.Count}]" : "")} | {Applies(f)} | {f.Meaning.Replace("|", "\\|")}{(f.Verified ? "" : " (unverified)")}{(f.Ref != null ? $" ({(f.Ref == "explosion" ? "an" : "a")} {f.Ref} reference)" : "")} |\n");
            foreach (var g in c.Groups) s.Append($"\nGroup `{g.Name}` ({g.Meaning}): {string.Join(", ", g.Fields.Select(f => $"`{f}`"))}.\n");
        }
        s.Append("## Components\n\nEntity component records: `HF_FIELD(Component, field)` names a field, `hf_record(mod, entity, HfComponent)` gives a typed\npointer. *Applies* is when a live change takes effect (`now`, `respawn`, `mission`; `?` inferred, not yet confirmed).\n");
        foreach (var c in Components.Where(c => !c.Table)) Component(c);
        s.Append("\n## Settings tables\n\nRows of the game's settings tables, found by their id (`set_live`, `set_field`), or through a row function.\n");
        foreach (var c in Components.Where(c => c.Table)) Component(c);
        s.Append("\n## Content\n\nVanilla content constants (`hellforge_content.h`); `hf->find(HF_KIND_..., \"name\")` resolves the same names at run time.\n");
        foreach (var kind in Kinds)
        {
            var items = Content.Where(c => c.Kind == kind.Key).ToList();
            if (items.Count == 0) continue;
            s.Append($"\n### {kind.Prefix}... ({items.Count}, `{KindConstant(kind.Prefix)}`)\n\n{kind.Doc.Replace("\n * ", " ")}\n\n<details><summary>All {items.Count}</summary>\n\n| Constant | Value | Name |\n|---|---|---|\n");
            foreach (var item in items)
                s.Append($"| `{kind.Prefix}{item.Constant}` | {(kind.Wide ? $"0x{item.Id:X16}" : item.Id > 0xFFFF || kind.Key == "traits" ? $"0x{item.Id:X}" : item.Id.ToString(CultureInfo.InvariantCulture))} | {item.Name.Replace("|", "\\|")}{(item.Note.Length > 0 ? " - " + item.Note.Replace("|", "\\|") : "")} |\n");
            s.Append("\n</details>\n");
        }
        return s.ToString().ReplaceLineEndings("\n");
    }

    // HfApi's members, each with the comment above it, grouped by the SDK version that added them.
    static IEnumerable<string> ApiReference(string header)
    {
        var start = header.IndexOf("typedef struct HfApi\n{", StringComparison.Ordinal);
        var end = header.IndexOf("} HfApi;", Math.Max(start, 0), StringComparison.Ordinal);
        if (start < 0 || end < 0) throw new InvalidDataException("hellforge_mod.h has no HfApi table.");
        var comment = new List<string>();
        var inComment = false;
        yield return "\n### SDK 1\n";
        foreach (var raw in header[start..end].Split('\n').Skip(2))
        {
            var line = raw.Trim();
            if (line.StartsWith("/* ----", StringComparison.Ordinal))
            {
                comment.Clear();
                var version = line.Trim('/', '*', ' ', '-');
                yield return $"\n### {version}\n";
                continue;
            }
            if (inComment || line.StartsWith("/*", StringComparison.Ordinal))
            {
                var text = line.TrimStart('/').TrimStart('*').Trim();
                inComment = !line.EndsWith("*/", StringComparison.Ordinal);
                if (text.EndsWith("*/", StringComparison.Ordinal)) text = text[..^2].Trim();
                if (text.Length > 0) comment.Add(text);
                continue;
            }
            var member = System.Text.RegularExpressions.Regex.Match(line, @"^(.+?)\s*\(\*(\w+)\)\((.*)\);");
            var data = System.Text.RegularExpressions.Regex.Match(line, @"^(\w+)\s+(\w+);\s*(?:/\*\s*(.*?)\s*\*/)?$");
            if (member.Success)
            {
                yield return $"- **`{member.Groups[2].Value}`** `{member.Groups[1].Value} ({member.Groups[3].Value})`" +
                    (comment.Count > 0 ? ": " + string.Join(" ", comment) : "");
                comment.Clear();
            }
            else if (data.Success)
            {
                yield return $"- **`{data.Groups[2].Value}`** `{data.Groups[1].Value}`" + (data.Groups[3].Value.Length > 0 ? ": " + data.Groups[3].Value : "");
                comment.Clear();
            }
            else if (line.Length == 0) comment.Clear();
        }
    }

    // HF_ITEM_ -> HF_KIND_ITEM
    static string KindConstant(string prefix) => "HF_KIND_" + prefix[3..^1];

    // Vanilla content by catalog name, for find.
    string LoaderContent()
    {
        var s = new StringBuilder();
        s.Append($"/* Vanilla content by catalog name, for game build {Build}: what find resolves.\n * {Generated} Included by fields.c. */\n");
        s.Append("static const SdkContent kSdkContent[] = {\n");
        foreach (var item in Content)
        {
            var kind = Kinds.First(k => k.Key == item.Kind);
            s.Append($"    {{{KindConstant(kind.Prefix)}, \"{CString(item.Name)}\", 0x{item.Id:X}ull}},\n");
        }
        s.Append("};\n");
        return s.ToString().ReplaceLineEndings("\n");
    }

    static string CString(string text)
    {
        var s = new StringBuilder();
        foreach (var b in Encoding.UTF8.GetBytes(text))
            s.Append(b is (byte)'"' or (byte)'\\' ? $"\\{(char)b}" : b is >= 0x20 and < 0x7F ? ((char)b).ToString() : $"\\{Convert.ToString(b, 8).PadLeft(3, '0')}");
        return s.ToString();
    }

    string LoaderTable()
    {
        var s = new StringBuilder();
        s.Append($"/* The loader's table of reviewed component fields, for game build {Build}: what a mod's HF_FIELD must match.\n * {Generated} Included by fields.c. */\n");
        foreach (var c in Components)
        {
            s.Append($"static const SdkField kSdkFields{c.Name}[] = {{\n");
            foreach (var f in c.Fields)
                s.Append($"    {{\"{f.Name}\", {f.Offset}, {f.Size}, {TypeTag(f.Type)}, HF_APPLIES_{f.Applies.ToUpperInvariant()}, REF_{(f.Ref ?? "none").ToUpperInvariant()}}},\n");
            s.Append("};\n");
        }
        s.Append("static const SdkComponent kSdkComponents[] = {\n");
        foreach (var c in Components)
            s.Append($"    {{\"{c.Name}\", \"{c.Component}\", {c.Layout}u, {c.Stride}u, kSdkFields{c.Name}, {c.Fields.Count}, {(c.Table ? 1 : 0)}, {c.Index}}},\n");
        s.Append("};\n");
        return s.ToString().ReplaceLineEndings("\n");
    }

    // A component's "applies" (required), or a field's (defaulting to its component's).
    static string Applies(JsonElement element, string where, string? fallback)
    {
        if (!element.TryGetProperty("applies", out _)) return fallback ?? throw new InvalidDataException($"{where}: missing applies (now, respawn or mission).");
        var value = Text(element, "applies");
        return AppliesValues.Contains(value) ? value : throw new InvalidDataException($"{where}: applies must be now, respawn or mission.");
    }

    static void Identifier(string name, string where)
    {
        if (name.Length == 0 || !(char.IsAsciiLetter(name[0]) || name[0] == '_') || !name.All(ch => char.IsAsciiLetterOrDigit(ch) || ch == '_'))
            throw new InvalidDataException($"{where}: not a C identifier.");
    }
    static void Allowed(JsonElement element, string where, params string[] names)
    {
        if (element.ValueKind != JsonValueKind.Object) throw new InvalidDataException($"{where}: must be an object.");
        foreach (var property in element.EnumerateObject())
            if (!names.Contains(property.Name)) throw new InvalidDataException($"{where}: unknown key {property.Name}.");
    }
    static JsonElement Property(JsonElement element, string name) =>
        element.TryGetProperty(name, out var value) ? value : throw new InvalidDataException($"Missing {name}.");
    static string Text(JsonElement element, string name) =>
        Property(element, name) is { ValueKind: JsonValueKind.String } value ? value.GetString()! : throw new InvalidDataException($"{name} must be text.");
    static int Int(JsonElement element, string name) =>
        Property(element, name) is { ValueKind: JsonValueKind.Number } value && value.TryGetInt32(out var number) ? number : throw new InvalidDataException($"{name} must be an integer.");
    static bool Bool(JsonElement element, string name) =>
        Property(element, name) is { ValueKind: JsonValueKind.True or JsonValueKind.False } value ? value.GetBoolean() : throw new InvalidDataException($"{name} must be true or false.");
}

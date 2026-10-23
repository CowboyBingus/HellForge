using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;
using HellForge.Core.GameData;

namespace HellForge.Core;

// Mod sync (docs/mods.md "Playing with other mods"): a join refused for different gameplay mods carries the host's mod
// set (ModSet); the joiner's backend plans the difference (ModSyncPlan), downloads what it may from the host's backend
// into the synced store (SyncedStore) and restarts the session with the host's set (ModSyncSession). What a joiner takes
// from a host depends on the mod's class, which the joiner reads from the bytes it received, never from the host:
//   content  what HellForge checks itself: data specs (built from the joiner's own game files), schema stats (named fields
//            within their reviewed ranges) and network field types (integer widenings): from any host;
//   code     a DLL, sources HellForge compiles, or a patch holding Lua: only when VerifiedMods lists its exact content;
//   raw      bytes HellForge cannot check, which reach the game's own parsers or any field: binary data patches (data/),
//            raw stats lines and componentWrites: only when VerifiedMods lists its exact content, as code;
//   bundled  a mod HellForge ships (the in-game editor): never downloaded; a different one is another HellForge version.
// Every host gets the same rule, a Steam friend too.
public enum ModClass { Content, Code, Bundled, Raw }

// A file of a mod as the digest counts it: its path in the mod folder ('/'), size and SHA-256 (upper-case hex).
public sealed record ModFile(string Path, long Size, string Sha256);

// One gameplay mod of a mod set. Hash is its content hash: the SHA-256 (lower-case hex) of the lines the modlist digest
// takes from it (ModSets.Block). Order is its place in the session's load order (0 loads first). Folder and Files are
// this machine's own and never leave it (ModSet.ToJson(local: false)).
public sealed record ModSetEntry(string Id, string Name, string Version, string Hash, long Bytes, ModClass Class, int Order)
{
    public string? Folder { get; init; }
    public IReadOnlyList<ModFile> Files { get; init; } = [];
}

// The gameplay mods a session runs, with the modlist digest they make. Written per session (mods-stage --modset) for the
// backend, which shares it with joiners whose mods differ.
public sealed record ModSet(string Digest, IReadOnlyList<ModSetEntry> Mods)
{
    public const int MaxMods = 256;

    // The set of mods given in load order (bottom of the Mods page first): the mods the digest counts, from the digest's
    // own lines (ModCatalog.Compared), so the set's digest is the modlist digest.
    public static ModSet Of(IReadOnlyList<ModManifest> loadOrder)
    {
        var entries = new List<ModSetEntry>();
        var lines = new StringBuilder();
        var counted = loadOrder.Where(ModCatalog.Counted).ToList();
        foreach (var (mod, files, block) in ModCatalog.Compared(counted))
        {
            lines.Append(block);
            entries.Add(new ModSetEntry(mod.Id, mod.Name, mod.Version, ModSets.Hash(block), files.Sum(f => f.Size), ModSets.ClassOf(mod), counted.IndexOf(mod))
                { Folder = mod.Folder, Files = files });
        }
        var digest = lines.Length == 0 ? "none" : Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(lines.ToString())))[..32].ToLowerInvariant();
        return new ModSet(digest, entries);
    }

    public ModSetEntry? Find(string id, string hash) => Mods.FirstOrDefault(m => m.Id == id && m.Hash == hash);

    // The mods as a refused join names them (ModCatalog.Modlist's fingerprints: the content hash's first 16 characters).
    public List<ModFingerprint> Fingerprints() => [.. Mods.Select(m => new ModFingerprint(m.Id, m.Name, m.Version, m.Hash[..16]))];

    // local: with each mod's folder and files (the session's own file); otherwise what a joiner is sent.
    public JsonObject ToJson(bool local)
    {
        var mods = new JsonArray();
        foreach (var mod in Mods)
        {
            var item = new JsonObject
            {
                ["id"] = mod.Id, ["name"] = mod.Name, ["version"] = mod.Version, ["hash"] = mod.Hash, ["bytes"] = mod.Bytes,
                ["class"] = ModSets.ClassName(mod.Class), ["order"] = mod.Order
            };
            if (local)
            {
                item["folder"] = mod.Folder;
                item["files"] = new JsonArray([.. mod.Files.Select(f => (JsonNode?)new JsonObject { ["path"] = f.Path, ["size"] = f.Size, ["sha256"] = f.Sha256 })]);
            }
            mods.Add((JsonNode?)item);
        }
        return new JsonObject { ["format"] = 1, ["digest"] = Digest, ["mods"] = mods };
    }

    public void Write(string path)
    {
        File.WriteAllText(path + ".partial", ToJson(local: true).ToJsonString());
        File.Move(path + ".partial", path, overwrite: true);
    }

    // A set from JSON: the session's own file (local: folders and files kept) or a host's (local: false), which comes
    // from another machine and is checked field by field.
    public static ModSet Parse(string json, bool local)
    {
        JsonNode? root;
        try { root = JsonNode.Parse(json); }
        catch (JsonException error) { throw new InvalidDataException("The mod set is not JSON: " + error.Message); }
        if (root is not JsonObject set || (int?)set["format"] != 1) throw new InvalidDataException("The mod set has no format 1.");
        var digest = (string?)set["digest"] ?? "";
        if (digest != "none" && !ModSets.Hex(digest, 32)) throw new InvalidDataException("The mod set's digest is malformed.");
        if (set["mods"] is not JsonArray list || list.Count > MaxMods) throw new InvalidDataException("The mod set's mods are missing or too many.");
        var mods = new List<ModSetEntry>();
        foreach (var node in list)
        {
            if (node is not JsonObject item) throw new InvalidDataException("A mod set entry is not an object.");
            string Text(string name, int max) => item[name] is JsonValue v && v.TryGetValue<string>(out var s) && s.Length > 0 && s.Length <= max && !s.Any(char.IsControl)
                ? s : throw new InvalidDataException($"A mod set entry's {name} is missing or malformed.");
            long Number(string name, long max) => item[name] is JsonValue v && v.TryGetValue<long>(out var n) && n >= 0 && n <= max
                ? n : throw new InvalidDataException($"A mod set entry's {name} is missing or malformed.");
            var id = Text("id", 64);
            if (!ModCatalog.IsModId(id)) throw new InvalidDataException("A mod set entry's id is malformed.");
            var hash = Text("hash", 64);
            if (!ModSets.Hex(hash, 64)) throw new InvalidDataException($"{id}: its content hash is malformed.");
            var entry = new ModSetEntry(id, Text("name", 80), Text("version", 32), hash, Number("bytes", 1L << 40),
                ModSets.ClassNamed(Text("class", 16)), (int)Number("order", MaxMods));
            if (local)
            {
                var files = new List<ModFile>();
                foreach (var file in item["files"] as JsonArray ?? [])
                    files.Add(new ModFile((string?)file?["path"] ?? "", (long?)file?["size"] ?? -1, (string?)file?["sha256"] ?? ""));
                entry = entry with { Folder = (string?)item["folder"], Files = files };
            }
            mods.Add(entry);
        }
        if (mods.Select(m => m.Id).Distinct(StringComparer.Ordinal).Count() != mods.Count) throw new InvalidDataException("The mod set names a mod twice.");
        return new ModSet(digest, mods);
    }
}

public static class ModSets
{
    // The files of a mod as the digest counts them, hashed. A zipped mod's hashes are its zip's (ModArchives.Shipped), its
    // sizes those of its unpacked folder: a file the mod changed there as it ran is sent as it is now and fails the
    // joiner's check, rather than counting differently.
    public static List<ModFile> Files(ModManifest mod) =>
        [.. ModCatalog.DigestFiles(mod).Select(file =>
        {
            var full = Path.Combine(mod.Folder, file.Path);
            if (file.Sha256 != null) return new ModFile(file.Path, File.Exists(full) ? new FileInfo(full).Length : 0, file.Sha256);
            using var stream = ModCatalog.OpenShared(full); // a file another program has open (issue 18)
            return new ModFile(file.Path, stream.Length, Convert.ToHexString(SHA256.HashData(stream)));
        })];

    // The lines the modlist digest takes from one mod: id and version, the data builder's version for a data spec (with
    // the supported game build, which every player has, it determines the generated bytes), then each file and its hash.
    public static string Block(ModManifest mod, IEnumerable<ModFile> files)
    {
        var lines = new StringBuilder();
        lines.Append(mod.Id).Append('|').Append(mod.Version).Append('\n');
        if (mod.HasDataSpec) lines.Append("data-builder|").Append(ModDataBuilder.Version).Append('\n');
        foreach (var file in files) lines.Append(file.Path).Append('|').Append(file.Sha256).Append('\n');
        return lines.ToString();
    }

    public static string Hash(string block) => Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(block)));

    // A mod's content hash, read from its folder now.
    public static string ContentHash(ModManifest mod) => Hash(Block(mod, Files(mod)));

    // Code runs in the game: a DLL (or sources HellForge builds into one), or a patch with Lua scripts. Raw is what reaches
    // the game unchecked: binary data patches, componentWrites, and raw stats lines (StatsData's "raw" section, the only
    // stats edits that carry an expected vanilla value rather than a reviewed range).
    public static ModClass ClassOf(ModManifest mod) =>
        BundledMods.ShippedFiles(mod.Folder) != null ? ModClass.Bundled
        : mod.HasCode || mod.Lua ? ModClass.Code
        : mod.DataFiles.Count > 0 || mod.ComponentWrites.Count > 0 || mod.Stats.Any(e => e.Expect != null) ? ModClass.Raw
        : ModClass.Content;

    // Downloads only when the verified list names its exact content.
    public static bool NeedsVerified(ModClass kind) => kind is ModClass.Code or ModClass.Raw;

    // What a download already shows before the rest of its bytes: the host's file list, its manifest, and its stats file
    // (null when it lists none). Code or Raw when they show it, null otherwise: the whole mod decides then (ClassOf).
    public static ModClass? Declared(IEnumerable<string> paths, string manifestJson, string? statsJson)
    {
        using var manifest = JsonDocument.Parse(manifestJson);
        var root = manifest.RootElement;
        if (root.ValueKind != JsonValueKind.Object) return null;
        if (root.TryGetProperty("code", out _)) return ModClass.Code;
        if (paths.Any(p => p.StartsWith("data/", StringComparison.OrdinalIgnoreCase))) return ModClass.Raw;
        if (root.TryGetProperty("native", out var native) && native.ValueKind == JsonValueKind.Object && native.TryGetProperty("componentWrites", out _))
            return ModClass.Raw;
        if (statsJson == null) return null;
        using var stats = JsonDocument.Parse(statsJson);
        return stats.RootElement.ValueKind == JsonValueKind.Object && stats.RootElement.TryGetProperty("raw", out _) ? ModClass.Raw : null;
    }

    public static string ClassName(ModClass kind) => kind switch
    {
        ModClass.Code => "code", ModClass.Bundled => "bundled", ModClass.Raw => "raw", _ => "content"
    };

    public static ModClass ClassNamed(string name) => name switch
    {
        "content" => ModClass.Content, "code" => ModClass.Code, "bundled" => ModClass.Bundled, "raw" => ModClass.Raw,
        _ => throw new InvalidDataException($"{name} is not a mod class.")
    };

    // Why a mod of this class needs the verified list, for the player.
    public static string Unchecked(ModClass kind) => kind == ModClass.Raw
        ? "carries data HellForge cannot check (data patches, raw stats or component writes)"
        : "runs code (a DLL, sources or Lua)";

    internal static bool Hex(string text, int length) => text.Length == length && text.All(char.IsAsciiHexDigitLower);
}

// HellForge's verified list (data/verified-mods.json, embedded in Core): code mods whose exact content a HellForge
// maintainer reviewed, which mod sync may download from any host. A version of a mod is listed by its content hash
// (ModSets.ContentHash), so any other bytes under the same id and version are not verified.
//   {"format":1,"mods":[{"id":"...","name":"...","version":"...","hash":"<64 hex>","reviewed":"<date>, <by whom>"}]}
public static class VerifiedMods
{
    public sealed record Entry(string Id, string Name, string Version, string Hash, string Reviewed);

    static readonly Lazy<IReadOnlyList<Entry>> embedded = new(() =>
    {
        using var stream = typeof(VerifiedMods).Assembly.GetManifestResourceStream("HellForge.Core.verified-mods.json")
            ?? throw new InvalidDataException("The verified mod list is not embedded.");
        using var reader = new StreamReader(stream);
        return Parse(reader.ReadToEnd());
    });

    public static IReadOnlyList<Entry> All => embedded.Value;

    public static bool Contains(string id, string hash) => All.Any(e => e.Id == id && e.Hash == hash);

    public static IReadOnlyList<Entry> Parse(string json)
    {
        var root = JsonNode.Parse(json) as JsonObject ?? throw new InvalidDataException("The verified mod list is not an object.");
        if ((int?)root["format"] != 1 || root["mods"] is not JsonArray mods) throw new InvalidDataException("The verified mod list has no format 1 or no mods.");
        var entries = new List<Entry>();
        foreach (var node in mods)
        {
            var entry = new Entry((string?)node?["id"] ?? "", (string?)node?["name"] ?? "", (string?)node?["version"] ?? "", (string?)node?["hash"] ?? "",
                (string?)node?["reviewed"] ?? "");
            if (!ModCatalog.IsModId(entry.Id) || !ModSets.Hex(entry.Hash, 64) || entry.Version.Length == 0 || entry.Reviewed.Length == 0)
                throw new InvalidDataException($"A verified mod entry is malformed: {entry.Id}.");
            entries.Add(entry);
        }
        return entries;
    }
}

// Where the joiner finds a mod it may already have, by id and content hash: an installed mod (on or off, a folder or a
// zip) or one the synced store holds. The installed ones are the catalogue's (ModCatalog.Load, as the Mods page reads it
// while a session runs), so a zipped mod is hashed by its zip as the host's is; each is hashed only when asked about, once.
public sealed class ModLocator(string modsFolder, EntityProfile? profile = null)
{
    readonly Dictionary<string, List<(string Folder, string Hash)>> byId = new(StringComparer.Ordinal);
    List<ModManifest>? installed;

    public string? Find(string id, string hash)
    {
        if (SyncedStore.Find(modsFolder, id, hash) is { } synced) return synced;
        if (!byId.TryGetValue(id, out var found))
        {
            found = [];
            installed ??= Directory.Exists(modsFolder) ? ModCatalog.Load(modsFolder, profile).Mods : [];
            foreach (var mod in installed.Where(m => m.Id == id))
                try { found.Add((mod.Folder, ModSets.ContentHash(mod))); }
                catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
            byId[id] = found;
        }
        return found.FirstOrDefault(f => f.Hash == hash).Folder;
    }
}

// What joining a host's lobby takes, mod by mod (each host mod in id order, then this machine's mods the host lacks).
//   have      this machine has it (installed, on or off, or synced before): Folder;
//   download  a content mod, or a verified code mod, the host shares;
//   manual    the host does not share, or a code mod that is not verified: the player installs it themselves;
//   blocked   on the player's blocklist;
//   update    a mod HellForge ships differs: one of the two players runs another HellForge version;
//   off       this machine's own gameplay mod the host does not run: left out of the synced session.
public enum SyncAction { Have, Download, Manual, Blocked, Update, Off }
public enum SyncVerdict { Ready, Download, Unjoinable, Blocked }
public sealed record SyncItem(ModSetEntry Mod, SyncAction Action, string? Folder);

public sealed record ModSyncPlan(IReadOnlyList<SyncItem> Items)
{
    public SyncVerdict Verdict =>
        Items.Any(i => i.Action == SyncAction.Blocked) ? SyncVerdict.Blocked
        : Items.Any(i => i.Action is SyncAction.Manual or SyncAction.Update) ? SyncVerdict.Unjoinable
        : Items.Any(i => i.Action == SyncAction.Download) ? SyncVerdict.Download : SyncVerdict.Ready;

    public long DownloadBytes => Items.Where(i => i.Action == SyncAction.Download).Sum(i => i.Mod.Bytes);

    // own: this session's mod set (the joiner's); find: an installed or synced mod by id and hash (ModLocator.Find).
    public static ModSyncPlan Make(ModSet host, bool hostShares, ModSet own, Func<string, string, string?> find, IReadOnlySet<string> blocked,
        Func<string, string, bool>? verified = null)
    {
        verified ??= VerifiedMods.Contains;
        var items = new List<SyncItem>();
        foreach (var mod in host.Mods.OrderBy(m => m.Id, StringComparer.Ordinal))
        {
            if (blocked.Contains(mod.Id)) { items.Add(new(mod, SyncAction.Blocked, null)); continue; }
            var folder = own.Find(mod.Id, mod.Hash)?.Folder ?? find(mod.Id, mod.Hash);
            var action = folder != null ? SyncAction.Have
                : mod.Class == ModClass.Bundled ? SyncAction.Update
                : !hostShares || ModSets.NeedsVerified(mod.Class) && !verified(mod.Id, mod.Hash) ? SyncAction.Manual
                : SyncAction.Download;
            items.Add(new(mod, action, folder));
        }
        foreach (var mod in own.Mods.Where(m => host.Mods.All(h => h.Id != m.Id)).OrderBy(m => m.Id, StringComparer.Ordinal))
            items.Add(new(mod, SyncAction.Off, mod.Folder));
        return new ModSyncPlan(items);
    }

    public static string ActionName(SyncAction action) => action.ToString().ToLowerInvariant();
}

// Mods downloaded from hosts: <mods>/.synced/<id>@<first 16 of the content hash>/, which normal sessions never load
// (ModCatalog.Load skips dot folders); only a synced session (ModSyncSession) does. A download is written into
// .synced/.partial/<same name>/ and moved in only after SyncedStore.Install has checked all of it.
public static class SyncedStore
{
    public const string FolderName = ".synced";
    // Beside a synced mod's files; a dot file, so the digest and the content hash leave it out.
    public const string RecordName = ".hellforge-synced.json";
    public const int MaxFiles = 10000;

    public static string Root(string modsFolder) => Path.Combine(modsFolder, FolderName);
    static string Name(string id, string hash) => id + "@" + hash[..16];

    // The store's folder of a mod, when it holds that exact one.
    public static string? Find(string modsFolder, string id, string hash)
    {
        if (!ModCatalog.IsModId(id) || !ModSets.Hex(hash, 64)) return null;
        var folder = Path.Combine(Root(modsFolder), Name(id, hash));
        try
        {
            var record = Path.Combine(folder, RecordName);
            return File.Exists(record) && (string?)JsonNode.Parse(File.ReadAllText(record))?["hash"] == hash ? folder : null;
        }
        catch (Exception error) when (error is JsonException or IOException or UnauthorizedAccessException) { return null; }
    }

    // An empty folder to download a mod into.
    public static string Partial(string modsFolder, string id, string hash)
    {
        if (!ModCatalog.IsModId(id) || !ModSets.Hex(hash, 64)) throw new InvalidDataException("A synced mod's id or hash is malformed.");
        var folder = Path.Combine(Root(modsFolder), ".partial", Name(id, hash));
        if (Directory.Exists(folder)) Directory.Delete(folder, recursive: true);
        Directory.CreateDirectory(folder);
        return folder;
    }

    // A file a host listed, inside the download folder; refused unless its path is one the digest would count and that
    // Windows reads as written (no device names, no trailing dots or spaces, no streams).
    public static string Target(string partial, string relative)
    {
        if (!SafePath(relative)) throw new InvalidDataException($"The host listed an unsafe path: {Visible(relative)}.");
        var root = Path.GetFullPath(partial) + Path.DirectorySeparatorChar;
        var full = Path.GetFullPath(Path.Combine(partial, relative));
        return full.StartsWith(root, StringComparison.OrdinalIgnoreCase) ? full : throw new InvalidDataException($"The host listed an unsafe path: {Visible(relative)}.");
    }

    static readonly HashSet<string> Devices = new(["CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$",
        .. Enumerable.Range(0, 10).SelectMany(n => new[] { "COM" + n, "LPT" + n }), "COM¹", "COM²", "COM³", "LPT¹", "LPT²", "LPT³"], StringComparer.OrdinalIgnoreCase);

    public static bool SafePath(string relative)
    {
        if (relative.Length is 0 or > 240 || ModCatalog.IsWorkspaceFile(relative)) return false;
        foreach (var part in relative.Split('/'))
        {
            if (part.Length == 0 || part.EndsWith('.') || part.EndsWith(' ') || part.StartsWith(' ')) return false;
            if (part.Any(c => c < 32 || c is '\\' or ':' or '*' or '?' or '"' or '<' or '>' or '|')) return false;
            if (Devices.Contains(part.Split('.')[0])) return false;
        }
        return true;
    }

    static string Visible(string text) => string.Concat(text.Take(80).Select(c => char.IsControl(c) ? '?' : c));

    // Checks a finished download against what the plan expected and moves it into the store; returns its folder. It
    // must be a mod with that id and version, the same content hash read from its own files, and content, or code or raw
    // data the verified list names (read from its own files, whatever the host said). Throws InvalidDataException,
    // with the download deleted.
    public static string Install(string modsFolder, ModSetEntry expected, string partial, Func<string, string, bool>? verified = null,
        EntityProfile? profile = null)
    {
        try { Verify(expected, partial, verified ?? VerifiedMods.Contains, profile ?? EntityProfile.Current); }
        catch (InvalidDataException)
        {
            try { Directory.Delete(partial, recursive: true); }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
            throw;
        }
        File.WriteAllText(Path.Combine(partial, RecordName), new JsonObject { ["hash"] = expected.Hash, ["id"] = expected.Id, ["version"] = expected.Version,
            ["synced"] = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture) }.ToJsonString());
        var target = Path.Combine(Root(modsFolder), Name(expected.Id, expected.Hash));
        if (Directory.Exists(target)) Directory.Delete(target, recursive: true);
        Directory.Move(partial, target);
        return target;
    }

    static void Verify(ModSetEntry expected, string partial, Func<string, string, bool> verified, EntityProfile profile)
    {
        ModManifest mod;
        try { mod = ModCatalog.Parse(partial, profile); }
        catch (JsonException error) { throw new InvalidDataException($"{expected.Id}: its manifest is not valid JSON ({error.Message})."); }
        if (mod.Id != expected.Id || mod.Version != expected.Version || !mod.Gameplay)
            throw new InvalidDataException($"{expected.Id}: the files are another mod ({mod.Id} {mod.Version}{(mod.Gameplay ? "" : ", cosmetic")}).");
        var files = ModSets.Files(mod);
        if (files.Count > MaxFiles || ModSets.Hash(ModSets.Block(mod, files)) != expected.Hash)
            throw new InvalidDataException($"{expected.Id}: the files do not match the host's mod.");
        // Only the counted files may be there: anything else would load without being compared.
        var counted = files.Select(f => f.Path).ToHashSet(StringComparer.Ordinal);
        if (Directory.GetFiles(partial, "*", SearchOption.AllDirectories).Any(f => !counted.Contains(Path.GetRelativePath(partial, f).Replace('\\', '/'))))
            throw new InvalidDataException($"{expected.Id}: it holds files the host did not list.");
        var kind = ModSets.ClassOf(mod);
        if (kind == ModClass.Bundled) throw new InvalidDataException($"{expected.Id} is a mod HellForge ships; it is never downloaded.");
        if (ModSets.NeedsVerified(kind) && !verified(mod.Id, expected.Hash))
            throw new InvalidDataException($"{expected.Id} {ModSets.Unchecked(kind)} and is not on HellForge's verified list.");
    }
}

// The session after a sync: the host's gameplay mods in the host's load order, found by id and content hash, then this
// player's own cosmetic mods and Local Stats. <installation>/temp/mod-sync/session.json, written by the joiner's backend
// once everything is here; used once, by the session the launcher starts with --mod-sync.
public sealed record ModSyncSession(string Host, string HostName, string Connection, string Digest, IReadOnlyList<ModSyncSession.Mod> Mods)
{
    public sealed record Mod(string Id, string Hash, int Order);
    public const string FolderName = "mod-sync", FileName = "session.json";

    public static string PathIn(InstallLayout install) => System.IO.Path.Combine(install.Temp, FolderName, FileName);

    public void Write(string path)
    {
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(path)!);
        var root = new JsonObject
        {
            ["format"] = 1, ["host"] = Host, ["hostName"] = HostName, ["connection"] = Connection, ["digest"] = Digest,
            ["created"] = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
            ["mods"] = new JsonArray([.. Mods.Select(m => (JsonNode?)new JsonObject { ["id"] = m.Id, ["hash"] = m.Hash, ["order"] = m.Order })])
        };
        File.WriteAllText(path + ".partial", root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        File.Move(path + ".partial", path, overwrite: true);
    }

    public static ModSyncSession Read(string path)
    {
        var root = JsonNode.Parse(File.ReadAllText(path)) as JsonObject ?? throw new InvalidDataException("The mod sync file is not an object.");
        if ((int?)root["format"] != 1) throw new InvalidDataException("The mod sync file has no format 1.");
        var mods = new List<Mod>();
        foreach (var node in root["mods"] as JsonArray ?? [])
        {
            var mod = new Mod((string?)node?["id"] ?? "", (string?)node?["hash"] ?? "", (int?)node?["order"] ?? -1);
            if (!ModCatalog.IsModId(mod.Id) || !ModSets.Hex(mod.Hash, 64) || mod.Order < 0) throw new InvalidDataException("A mod in the mod sync file is malformed.");
            mods.Add(mod);
        }
        return new ModSyncSession((string?)root["host"] ?? "", (string?)root["hostName"] ?? "", (string?)root["connection"] ?? "", (string?)root["digest"] ?? "", mods);
    }

    // The session's mods in load order (bottom of the Mods page first), from the player's catalogue (ModCatalog.Load) and
    // the store; null with the reason when a mod is missing or the result's digest is not the host's.
    public (List<ModManifest>? LoadOrder, string? Problem) Resolve(string modsFolder, IReadOnlyList<ModManifest> catalog, EntityProfile? profile = null)
    {
        var locator = new ModLocator(modsFolder, profile);
        var host = new List<ModManifest>();
        foreach (var mod in Mods.OrderBy(m => m.Order))
        {
            var listed = catalog.FirstOrDefault(m => m.Id == mod.Id);
            var folder = locator.Find(mod.Id, mod.Hash);
            if (folder == null) return (null, $"{mod.Id} is no longer here.");
            ModManifest found;
            try
            {
                found = listed != null && Same(listed.Folder, folder) ? listed with { Enabled = true }
                    : ModCatalog.Parse(folder, profile ?? EntityProfile.Current);
            }
            catch (Exception error) when (error is InvalidDataException or JsonException or IOException) { return (null, $"{mod.Id}: {error.Message}"); }
            host.Add(found);
        }
        // The player's own: cosmetic mods first (the bottom of their Mods page), Local Stats last (it stays on top).
        var own = ModCatalog.LoadOrder(catalog).Where(m => m.Enabled && !ModCatalog.Counted(m) && host.All(h => h.Id != m.Id)).ToList();
        var loadOrder = own.Where(m => m.Id != ModCatalog.LocalStatsId).Concat(host).Concat(own.Where(m => m.Id == ModCatalog.LocalStatsId)).ToList();
        var digest = ModSet.Of(loadOrder).Digest;
        return digest == Digest ? (loadOrder, null) : (null, "the mods found do not make the host's modlist code.");
    }

    static bool Same(string a, string b) => string.Equals(System.IO.Path.GetFullPath(a).TrimEnd('\\', '/'), System.IO.Path.GetFullPath(b).TrimEnd('\\', '/'), StringComparison.OrdinalIgnoreCase);
}

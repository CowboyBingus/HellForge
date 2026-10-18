using System.IO.Compression;
using System.Text;
using System.Text.Json.Nodes;

namespace HellForge.Core;

// Host-authoritative stats in co-op: everyone in a session plays with the host's Local Stats.
// The host's backend sends its Local Stats file to every friend in its lobby, again whenever it changes; a joiner's
// backend turns it into live edits of the Local Stats mod in its game (LiveEditServer, native/mod-loader/live.c) and,
// when the host goes, puts the joiner's own values back. This is the joiner's side of it: what its game holds over its
// session start, and the requests that take it to what it should hold.
//   set   <bits>  the field holds bits (a live edit of the Local Stats mod)
//   base          the field holds its base: without the Local Stats mod's edits (another mod's value, else the game's)
//   reset         the Local Stats mod's live edit is undone: the field holds its session-start value again
// Fields live editing does not reach (attachments' entity deltas, status multipliers) apply from a session start: they
// stay this machine's own and are counted (BootOnly).
public sealed record StatsSyncRequest(string Verb, StatsField Field, uint Bits, string Address);

public sealed class StatsSync
{
    // What the game holds over its session start, by field address: the bits a set put there, or null after a base.
    readonly Dictionary<string, (StatsField Field, uint? Bits)> applied = new(StringComparer.Ordinal);

    public int Tracked => applied.Count;
    // Host values the last plan could not send live (they apply from a session start).
    public int BootOnly { get; private set; }

    // A Local Stats document's live-editable values by field address; null or "" an empty one.
    public static Dictionary<string, (StatsField Field, uint Bits)> Values(string? json, out int bootOnly)
    {
        bootOnly = 0;
        var values = new Dictionary<string, (StatsField Field, uint Bits)>(StringComparer.Ordinal);
        if (string.IsNullOrWhiteSpace(json)) return values;
        try { Read(JsonNode.Parse(json) as JsonObject ?? [], values, ref bootOnly); }
        catch (Exception error) when (error is System.Text.Json.JsonException or ArgumentException) /* not JSON, a key twice */
        {
            values.Clear();
            bootOnly = 0;
        }
        return values;
    }

    static void Read(JsonObject root, Dictionary<string, (StatsField Field, uint Bits)> values, ref int bootOnly)
    {
        foreach (var (category, targets) in root)
        {
            if (targets is not JsonObject byTarget || StatsSchema.Current.Targets.GetValueOrDefault(category) is not { } section) continue;
            foreach (var (name, entry) in byTarget)
            {
                if (entry is not JsonObject fields || section.GetValueOrDefault(name) is not { } target) continue;
                if (fields["own"] is JsonValue own && own.TryGetValue(out bool isOwn) && isOwn) { bootOnly += fields.Count - 1; continue; }
                foreach (var (key, value) in fields)
                {
                    if (value == null || !target.Fields.TryGetValue(target.Aliases.GetValueOrDefault(key, key), out var field)) continue;
                    if (LiveApplies.Of(field) == null) { bootOnly++; continue; }
                    if (StatsCatalog.TryParse(field, value.ToJsonString(), out _, out var bits, out _)) values[field.AddressKey] = (field, bits);
                }
            }
        }
    }

    // The requests that take the game from what it holds now to the host's values (host not null: joined; "" the host has
    // none), or back to its own (host null: not joined, or the host left). own: this machine's Local Stats file now.
    public List<StatsSyncRequest> Plan(string? host, string? own)
    {
        var mine = Values(own, out _);
        var boot = 0;
        var theirs = host != null ? Values(host, out boot) : null;
        BootOnly = boot;
        var requests = new List<StatsSyncRequest>();
        IEnumerable<string> addresses = applied.Keys;
        if (theirs != null) addresses = addresses.Concat(theirs.Keys).Concat(mine.Keys);
        foreach (var address in addresses.Distinct(StringComparer.Ordinal).ToList())
        {
            var field = applied.TryGetValue(address, out var now) ? now.Field : theirs?.GetValueOrDefault(address).Field ?? mine[address].Field;
            var tracked = applied.ContainsKey(address);
            var ownBits = mine.TryGetValue(address, out var o) ? o.Bits : (uint?)null;
            if (theirs != null)
            {
                if (theirs.TryGetValue(address, out var h))
                {
                    if (!(tracked && now.Bits == h.Bits)) requests.Add(new("set", field, h.Bits, address));
                }
                else if (tracked ? now.Bits != null : ownBits != null) requests.Add(new("base", field, 0, address)); // their base
            }
            else if (tracked)
            {
                if (ownBits is { } back) requests.Add(new("set", field, back, address)); // its own value again
                else if (now.Bits != null) requests.Add(new("reset", field, 0, address));
                else applied.Remove(address); // at its base, which is its session-start value
            }
        }
        return requests;
    }

    // A request the game took. Back at its own values (not joined), a field is no longer tracked.
    public void Done(StatsSyncRequest request, bool joined)
    {
        if (!joined || request.Verb == "reset") applied.Remove(request.Address);
        else applied[request.Address] = (request.Field, request.Verb == "set" ? request.Bits : null);
    }

    // The game went away (its session ended): it holds nothing of the host's any more.
    public void Forget() => applied.Clear();

    // A Local Stats document for the peer link: gzip of its UTF-8 text.
    public static byte[] Pack(string json)
    {
        using var output = new MemoryStream();
        using (var zip = new GZipStream(output, CompressionLevel.SmallestSize)) zip.Write(Encoding.UTF8.GetBytes(json));
        return output.ToArray();
    }

    public static string Unpack(byte[] packed, int limit = 16 << 20)
    {
        using var input = new GZipStream(new MemoryStream(packed), CompressionMode.Decompress);
        using var output = new MemoryStream();
        var buffer = new byte[65536];
        for (int read; (read = input.Read(buffer)) > 0;)
        {
            output.Write(buffer, 0, read);
            if (output.Length > limit) throw new InvalidDataException("The host's stats are too large.");
        }
        return Encoding.UTF8.GetString(output.ToArray());
    }
}

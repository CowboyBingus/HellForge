using System.Buffers.Binary;

namespace HellForge.Core.GameData;

// A Stingray archive in the layout of the game's own (HD2Runtime's hd2_archive.make_resource_archive with .stream parts,
// as artifacts/registry/build_talon_liberator.py writes them): a 72-byte header {0xF0000011, type count, resource count,
// -, main sum, GPU sum}, a 32-byte row per type {0, 0, type, count, 0, 16, 64}, then an 80-byte entry per resource
// {name, type, main offset, stream offset, GPU offset, main sum, GPU sum, main size, stream size, GPU size, 16, 64,
// index}; the table padded to 256 bytes. Types and resources are sorted by hash. Main and stream parts are 16-byte, GPU
// parts 64-byte aligned (an entry without a part records the current position); the sums are running totals of the
// 256-byte-rounded main and GPU sizes (streams have none).
public static class ResourceArchive
{
    const uint Magic = 0xF0000011;

    public static (byte[] Archive, byte[] Gpu, byte[] Stream) Write(IReadOnlyDictionary<ResourceKey, ResourceParts> resources)
    {
        if (resources.Count == 0) throw new InvalidDataException("An archive needs at least one resource.");
        var keys = resources.Keys.OrderBy(k => k.Type).ThenBy(k => k.Name).ToList();
        var types = keys.Select(k => k.Type).Distinct().ToList();
        var tableEnd = 72 + 32 * types.Count + 80 * keys.Count;
        using var archive = new MemoryStream();
        using var gpu = new MemoryStream();
        using var stream = new MemoryStream();
        archive.SetLength(Align(tableEnd, 256));
        archive.Position = archive.Length;
        var table = new byte[tableEnd];
        long mainSum = 0, gpuSum = 0;
        for (var index = 0; index < keys.Count; index++)
        {
            var parts = resources[keys[index]];
            Pad(archive, 16);
            Pad(gpu, 64);
            Pad(stream, 16);
            var e = table.AsSpan(72 + 32 * types.Count + 80 * index, 80);
            BinaryPrimitives.WriteUInt64LittleEndian(e, keys[index].Name);
            BinaryPrimitives.WriteUInt64LittleEndian(e[8..], keys[index].Type);
            BinaryPrimitives.WriteUInt64LittleEndian(e[16..], (ulong)archive.Length);
            BinaryPrimitives.WriteUInt64LittleEndian(e[24..], (ulong)stream.Length);
            BinaryPrimitives.WriteUInt64LittleEndian(e[32..], (ulong)gpu.Length);
            BinaryPrimitives.WriteUInt64LittleEndian(e[40..], (ulong)mainSum);
            BinaryPrimitives.WriteUInt64LittleEndian(e[48..], (ulong)gpuSum);
            BinaryPrimitives.WriteUInt32LittleEndian(e[56..], (uint)parts.Main.Length);
            BinaryPrimitives.WriteUInt32LittleEndian(e[60..], (uint)parts.Stream.Length);
            BinaryPrimitives.WriteUInt32LittleEndian(e[64..], (uint)parts.Gpu.Length);
            BinaryPrimitives.WriteUInt32LittleEndian(e[68..], 16);
            BinaryPrimitives.WriteUInt32LittleEndian(e[72..], 64);
            BinaryPrimitives.WriteUInt32LittleEndian(e[76..], (uint)index);
            archive.Write(parts.Main);
            gpu.Write(parts.Gpu);
            stream.Write(parts.Stream);
            mainSum += Align(parts.Main.Length, 256);
            gpuSum += Align(parts.Gpu.Length, 256);
        }
        BinaryPrimitives.WriteUInt32LittleEndian(table, Magic);
        BinaryPrimitives.WriteUInt32LittleEndian(table.AsSpan(4), (uint)types.Count);
        BinaryPrimitives.WriteUInt32LittleEndian(table.AsSpan(8), (uint)keys.Count);
        BinaryPrimitives.WriteUInt64LittleEndian(table.AsSpan(32), (ulong)mainSum);
        BinaryPrimitives.WriteUInt64LittleEndian(table.AsSpan(40), (ulong)gpuSum);
        for (var t = 0; t < types.Count; t++)
        {
            var row = table.AsSpan(72 + 32 * t, 32);
            BinaryPrimitives.WriteUInt64LittleEndian(row[8..], types[t]);
            BinaryPrimitives.WriteUInt32LittleEndian(row[16..], (uint)keys.Count(k => k.Type == types[t]));
            BinaryPrimitives.WriteUInt32LittleEndian(row[24..], 16);
            BinaryPrimitives.WriteUInt32LittleEndian(row[28..], 64);
        }
        var bytes = archive.ToArray();
        table.CopyTo(bytes, 0);
        return (bytes, gpu.ToArray(), stream.ToArray());
    }

    // The resources of an archive written by Write (or the game), by key.
    public static Dictionary<ResourceKey, ResourceParts> Read(byte[] archive, byte[] gpu, byte[] stream)
    {
        if (archive.Length < 72 || BinaryPrimitives.ReadUInt32LittleEndian(archive) != Magic) throw new InvalidDataException("Not a resource archive.");
        var start = 72 + 32 * BinaryPrimitives.ReadInt32LittleEndian(archive.AsSpan(4));
        var found = new Dictionary<ResourceKey, ResourceParts>();
        for (var i = 0; i < BinaryPrimitives.ReadInt32LittleEndian(archive.AsSpan(8)); i++)
        {
            var e = archive.AsSpan(start + 80 * i, 80);
            static byte[] Slice(byte[] data, ulong offset, uint size) => size == 0 ? [] : data.AsSpan(checked((int)offset), (int)size).ToArray();
            found[new(BinaryPrimitives.ReadUInt64LittleEndian(e[8..]), BinaryPrimitives.ReadUInt64LittleEndian(e))] = new(
                Slice(archive, BinaryPrimitives.ReadUInt64LittleEndian(e[16..]), BinaryPrimitives.ReadUInt32LittleEndian(e[56..])),
                Slice(gpu, BinaryPrimitives.ReadUInt64LittleEndian(e[32..]), BinaryPrimitives.ReadUInt32LittleEndian(e[64..])),
                Slice(stream, BinaryPrimitives.ReadUInt64LittleEndian(e[24..]), BinaryPrimitives.ReadUInt32LittleEndian(e[60..])));
        }
        return found;
    }

    static long Align(long value, int alignment) => (value + alignment - 1) / alignment * alignment;

    static void Pad(MemoryStream stream, int alignment)
    {
        var padded = Align(stream.Length, alignment);
        if (padded == stream.Length) return;
        stream.SetLength(padded);
        stream.Position = padded;
    }
}

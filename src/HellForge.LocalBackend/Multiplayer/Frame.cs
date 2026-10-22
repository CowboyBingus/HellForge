using System.Buffers.Binary;
using System.Text;

namespace HellForge.LocalBackend.Multiplayer;

// One message between a stand-in DLL and its backend, or between two backends: a type name and a list of fields, each
// a byte string (UTF-8 text in nearly all of them). Encoded as u32 total length, u16 field count, then per field a u32
// length and the bytes, all little-endian; the type is field 0. Small enough to read and write by hand in C++.
public sealed class Frame
{
    public const int MaxBytes = 1 << 20;
    const int MaxFields = 4096;

    public string Type { get; }
    public List<byte[]> Fields { get; } = [];

    public Frame(string type) => Type = type;

    public int Count => Fields.Count;
    public string Text(int index) => index < Fields.Count ? Encoding.UTF8.GetString(Fields[index]) : throw new InvalidDataException($"{Type} lacks field {index}.");
    public byte[] Bytes(int index) => index < Fields.Count ? Fields[index] : throw new InvalidDataException($"{Type} lacks field {index}.");
    public int Int(int index) => int.TryParse(Text(index), out var value) ? value : throw new InvalidDataException($"{Type} field {index} is not a number.");
    public uint UInt(int index) => uint.TryParse(Text(index), out var value) ? value : throw new InvalidDataException($"{Type} field {index} is not a number.");

    public Frame Add(string text) { Fields.Add(Encoding.UTF8.GetBytes(text)); return this; }
    public Frame Add(long number) => Add(number.ToString(System.Globalization.CultureInfo.InvariantCulture));
    public Frame Add(byte[] bytes) { Fields.Add(bytes); return this; }

    // A property map as a count followed by key/value pairs.
    public Frame Add(IReadOnlyDictionary<string, string> properties)
    {
        Add(properties.Count);
        foreach (var (key, value) in properties.OrderBy(p => p.Key, StringComparer.Ordinal)) Add(key).Add(value);
        return this;
    }

    public Dictionary<string, string> Properties(ref int index)
    {
        var count = Int(index++);
        if (count < 0 || count > MaxFields) throw new InvalidDataException($"{Type} has a bad property count.");
        var properties = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < count; i++) properties[Text(index++)] = Text(index++);
        return properties;
    }

    // Property changes as a count followed by key, "set"/"remove", value triples (the SDK's null value removes a key).
    public Frame Add(IReadOnlyList<PropertyChange> changes)
    {
        Add(changes.Count);
        foreach (var change in changes) Add(change.Key).Add(change.Value == null ? "remove" : "set").Add(change.Value ?? "");
        return this;
    }

    public List<PropertyChange> Changes(ref int index)
    {
        var count = Int(index++);
        if (count < 0 || count > MaxFields) throw new InvalidDataException($"{Type} has a bad change count.");
        var changes = new List<PropertyChange>(count);
        for (var i = 0; i < count; i++)
        {
            var key = Text(index++);
            var operation = Text(index++);
            var value = Text(index++);
            changes.Add(new(key, operation == "remove" ? null : value));
        }
        return changes;
    }

    public byte[] Encode()
    {
        var fields = new List<byte[]>(Fields.Count + 1) { Encoding.UTF8.GetBytes(Type) };
        fields.AddRange(Fields);
        if (fields.Count > MaxFields) throw new InvalidDataException("Too many fields.");
        var length = 2 + fields.Sum(f => 4 + f.Length);
        if (length + 4 > MaxBytes) throw new InvalidDataException("Frame too large.");
        var buffer = new byte[4 + length];
        BinaryPrimitives.WriteUInt32LittleEndian(buffer, (uint)length);
        BinaryPrimitives.WriteUInt16LittleEndian(buffer.AsSpan(4), (ushort)fields.Count);
        var at = 6;
        foreach (var field in fields)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(buffer.AsSpan(at), (uint)field.Length);
            field.CopyTo(buffer, at + 4);
            at += 4 + field.Length;
        }
        return buffer;
    }

    // Decodes one whole frame (including its length prefix).
    public static Frame Decode(ReadOnlySpan<byte> data)
    {
        if (data.Length < 6) throw new InvalidDataException("Frame too short.");
        var length = BinaryPrimitives.ReadUInt32LittleEndian(data);
        if (length != data.Length - 4) throw new InvalidDataException("Frame length mismatch.");
        var count = BinaryPrimitives.ReadUInt16LittleEndian(data[4..]);
        if (count == 0) throw new InvalidDataException("Frame without a type.");
        var at = 6;
        var fields = new List<byte[]>(count);
        for (var i = 0; i < count; i++)
        {
            if (at + 4 > data.Length) throw new InvalidDataException("Truncated field.");
            var size = (int)BinaryPrimitives.ReadUInt32LittleEndian(data[at..]);
            if (size < 0 || at + 4 + size > data.Length) throw new InvalidDataException("Truncated field.");
            fields.Add(data.Slice(at + 4, size).ToArray());
            at += 4 + size;
        }
        if (at != data.Length) throw new InvalidDataException("Trailing bytes after the last field.");
        var frame = new Frame(Encoding.UTF8.GetString(fields[0]));
        frame.Fields.AddRange(fields.Skip(1));
        return frame;
    }

    // Reads one frame from a stream; null at a clean end of stream.
    public static async Task<Frame?> ReadAsync(Stream stream, CancellationToken cancellation)
    {
        var header = new byte[4];
        if (!await ReadExactly(stream, header, cancellation)) return null;
        var length = BinaryPrimitives.ReadUInt32LittleEndian(header);
        if (length < 2 || length + 4 > MaxBytes) throw new InvalidDataException("Bad frame length.");
        var buffer = new byte[4 + length];
        header.CopyTo(buffer, 0);
        if (!await ReadExactly(stream, buffer.AsMemory(4), cancellation)) throw new EndOfStreamException("Stream ended inside a frame.");
        return Decode(buffer);
    }

    static async Task<bool> ReadExactly(Stream stream, Memory<byte> buffer, CancellationToken cancellation)
    {
        var read = 0;
        while (read < buffer.Length)
        {
            var got = await stream.ReadAsync(buffer[read..], cancellation);
            if (got == 0) return read == 0 ? false : throw new EndOfStreamException("Stream ended inside a frame.");
            read += got;
        }
        return true;
    }

    public override string ToString() => $"{Type}[{Fields.Count}]";
}

// A property change: a null value removes the key.
public sealed record PropertyChange(string Key, string? Value);

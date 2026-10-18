using System.Text;

namespace HellForge.Core;

// The quoted KeyValues subset used by Steam's appmanifest and libraryfolders files.
public sealed class ValveData
{
    public Dictionary<string, string> Values { get; } = new(StringComparer.OrdinalIgnoreCase);
    public Dictionary<string, ValveData> Children { get; } = new(StringComparer.OrdinalIgnoreCase);

    public string Required(string key) => Values.TryGetValue(key, out var value) && value.Length > 0
        ? value : throw new InvalidDataException($"Steam metadata is missing '{key}'.");

    public static ValveData Parse(string text)
    {
        var position = 0;
        (string Value, bool Quoted)? Token()
        {
            while (position < text.Length)
            {
                if (char.IsWhiteSpace(text[position]) || text[position] == '\uFEFF') { position++; continue; }
                if (text[position] == '/' && position + 1 < text.Length && text[position + 1] == '/')
                {
                    while (position < text.Length && text[position] != '\n') position++;
                    continue;
                }
                break;
            }
            if (position == text.Length) return null;
            var c = text[position++];
            if (c is '{' or '}') return (c.ToString(), false);
            if (c != '"') throw new InvalidDataException("Expected a quoted Steam metadata token.");
            var value = new StringBuilder();
            while (position < text.Length)
            {
                c = text[position++];
                if (c == '"') return (value.ToString(), true);
                if (c == '\\' && position < text.Length && text[position] is '\\' or '"') c = text[position++];
                value.Append(c);
            }
            throw new InvalidDataException("Unterminated Steam metadata string.");
        }

        ValveData Read(bool nested, int depth)
        {
            if (depth > 32) throw new InvalidDataException("Steam metadata is nested too deeply.");
            var node = new ValveData();
            while (Token() is { } key)
            {
                if (!key.Quoted && key.Value == "}" && nested) return node;
                if (!key.Quoted) throw new InvalidDataException("Unexpected Steam metadata brace.");
                if (node.Values.ContainsKey(key.Value) || node.Children.ContainsKey(key.Value))
                    throw new InvalidDataException($"Duplicate Steam metadata key: {key.Value}");
                var value = Token() ?? throw new InvalidDataException("Missing Steam metadata value.");
                if (!value.Quoted && value.Value == "{") node.Children.Add(key.Value, Read(true, depth + 1));
                else if (value.Quoted) node.Values.Add(key.Value, value.Value);
                else throw new InvalidDataException("Unexpected Steam metadata closing brace.");
            }
            if (nested) throw new InvalidDataException("Unclosed Steam metadata object.");
            return node;
        }
        return Read(false, 0);
    }
}

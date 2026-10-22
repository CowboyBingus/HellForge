using System.Text;

namespace HellForge.Launcher;

// Follows a JSON-lines file. Only complete lines are returned, so a line that is mid-write is read next time.
sealed class LineTail
{
    long offset;

    public void Reset() => offset = 0;

    public List<string> ReadNewLines(string path)
    {
        var lines = new List<string>();
        try
        {
            if (!File.Exists(path)) return lines;
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            if (stream.Length < offset) offset = 0;  // Replaced or truncated.
            stream.Seek(offset, SeekOrigin.Begin);
            var buffer = new byte[stream.Length - offset];
            stream.ReadExactly(buffer);
            var end = Array.LastIndexOf(buffer, (byte)'\n');
            if (end < 0) return lines;
            offset += end + 1;
            foreach (var line in Encoding.UTF8.GetString(buffer, 0, end + 1).Split('\n'))
            {
                var trimmed = line.TrimStart('﻿').TrimEnd('\r');
                if (trimmed.Length > 0) lines.Add(trimmed);
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
        return lines;
    }
}

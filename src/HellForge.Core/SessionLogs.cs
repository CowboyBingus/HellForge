using System.Text.RegularExpressions;

namespace HellForge.Core;

// A session's logs (logs/sessions/<run id>: the session runner's result, the mod loader's and the backend's logs, the
// game's own, a crash dump when asked for) stay for the next sessions to compare, but not for ever: with crash dumps and
// engine logs they reached gigabytes. When the launcher opens and when a session ends, only the newest Keep stay. Only
// folders named as a run id, directly in the sessions folder, are removed; a folder in use (a session still writing) is
// left.
public static partial class SessionLogs
{
    public const int Keep = 10;

    [GeneratedRegex("^[0-9a-f]{32}$")]
    private static partial Regex RunId();

    public sealed record Pruned(int Removed, long Bytes, int Skipped);

    public static Pruned Prune(string sessionsFolder, int keep = Keep)
    {
        if (!Directory.Exists(sessionsFolder)) return new(0, 0, 0);
        LocalInstall.NoLinks(sessionsFolder);
        var runs = new DirectoryInfo(sessionsFolder).GetDirectories()
            .Where(d => RunId().IsMatch(d.Name) && (d.Attributes & FileAttributes.ReparsePoint) == 0)
            .OrderByDescending(d => d.CreationTimeUtc).ThenByDescending(d => d.Name, StringComparer.Ordinal).ToList();
        int removed = 0, skipped = 0;
        long bytes = 0;
        foreach (var run in runs.Skip(keep))
        {
            long size = 0;
            try
            {
                size = run.EnumerateFiles("*", SearchOption.AllDirectories).Sum(f => f.Length);
                run.Delete(recursive: true);
                removed++;
                bytes += size;
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException) { skipped++; }
        }
        return new(removed, bytes, skipped);
    }
}

using System.Diagnostics;

namespace HellForge.LocalBackend;

// The war's time in seconds (GET /WarSeason/801/timeSinceStart, which the client re-polls; parser 0x10817b0). It
// continues from the imported war's time; a friend who joins a host's lobby adopts the host's (Multiplayer.LobbyService
// sends it with the join), so both games see the same war.
public sealed class WarClock
{
    readonly Stopwatch clock = Stopwatch.StartNew();
    long offset;

    public WarClock(long seconds = 0) => Set(seconds);

    public long Seconds => Interlocked.Read(ref offset) + (long)clock.Elapsed.TotalSeconds;

    public void Set(long seconds) => Interlocked.Exchange(ref offset, seconds - (long)clock.Elapsed.TotalSeconds);
}

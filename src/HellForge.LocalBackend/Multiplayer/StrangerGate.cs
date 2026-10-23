namespace HellForge.LocalBackend.Multiplayer;

// What a backend takes from someone who is not a Steam friend (docs/multiplayer.md step 7). Strangers only come through
// the lobby directory, and only when the player has turned on "Play with strangers" (off by default):
//   - a join to the public lobby listed here (lobby.join), at most JoinsPerPeer a minute from one machine and JoinsInAll
//     from everyone;
//   - everything that follows once this machine deals with them (Related: a member of a lobby hosted here, the host of a
//     lobby joined or being joined from here, a machine downloading this one's mods or this one theirs);
//   - pings.
// Presence (the social tab) stays between friends.
public sealed class StrangerGate(IPeerTransport transport, Func<bool> listing, Func<ulong, bool> related, Func<bool> strangersAllowed, Action<string> log)
{
    public const int JoinsPerPeer = 6, JoinsInAll = 60;
    static readonly TimeSpan Window = TimeSpan.FromMinutes(1);

    readonly Dictionary<ulong, Queue<DateTime>> joins = [];
    readonly Queue<DateTime> allJoins = new();
    readonly Dictionary<ulong, DateTime> logged = [];

    public bool Admit(ulong peer, Frame frame)
    {
        if (transport.IsFriend(peer)) return true;
        if (!strangersAllowed()) return Refuse(peer, frame.Type + " (Play with strangers is off)");
        if (frame.Type is "presence" or "presence.hello") return Refuse(peer, frame.Type);
        if (frame.Type == "peer.ping" || related(peer)) return true;
        if (frame.Type != "lobby.join") return Refuse(peer, frame.Type);
        if (!listing()) return Refuse(peer, "a join with no public lobby listed here");
        lock (joins)
        {
            var now = DateTime.UtcNow;
            if (!joins.TryGetValue(peer, out var times)) joins[peer] = times = new Queue<DateTime>();
            while (times.Count > 0 && now - times.Peek() > Window) times.Dequeue();
            while (allJoins.Count > 0 && now - allJoins.Peek() > Window) allJoins.Dequeue();
            if (times.Count >= JoinsPerPeer || allJoins.Count >= JoinsInAll) return Refuse(peer, "too many joins");
            times.Enqueue(now);
            allJoins.Enqueue(now);
            foreach (var idle in joins.Where(j => j.Value.Count == 0).Select(j => j.Key).ToList()) joins.Remove(idle);
            return true;
        }
    }

    // Logged once a minute per machine.
    bool Refuse(ulong peer, string what)
    {
        lock (logged)
        {
            var now = DateTime.UtcNow;
            if (logged.TryGetValue(peer, out var last) && now - last < Window) return false;
            logged[peer] = now;
        }
        log($"Dropped {what} from {LobbyService.EntityId(peer)}, who is not a Steam friend.");
        return false;
    }
}

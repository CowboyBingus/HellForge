namespace HellForge.LocalBackend.Multiplayer;

// Shares this machine's player's public key (AccountKeys) with friends' backends: "account.key" <the game's key text>. The
// transport names the sender (Steam authenticates it), so a received key is stored for that SteamID only. A backend
// sends its key to a friend the first time it hears from them, again when its game logs in with a new key, and back to
// a friend whose key is new (their game or backend restarted).
public sealed class AccountKeyExchange
{
    readonly IPeerTransport transport;
    readonly AccountKeys keys;
    readonly Action<string> log;
    readonly HashSet<ulong> told = [];

    public AccountKeyExchange(IPeerTransport transport, AccountKeys keys, Action<string> log)
    {
        this.transport = transport;
        this.keys = keys;
        this.log = log;
        keys.LocalId = transport.LocalId;
        keys.LocalChanged += key =>
        {
            ulong[] peers;
            lock (told) peers = [.. told];
            foreach (var peer in peers) Send(peer, key);
        };
    }

    // Called for every frame a friend sends, before it is handled.
    public void Heard(ulong peer)
    {
        lock (told) if (!told.Add(peer)) return;
        if (keys.Local is { } key) Send(peer, key);
    }

    public void FromPeer(ulong peer, Frame frame)
    {
        var key = frame.Text(0);
        if (!AccountKeys.IsKey(key)) { log("Dropped a malformed player key from a friend."); return; }
        if (!keys.SetRemote(peer, key)) return;
        log($"Player key received from {LobbyService.EntityId(peer)}.");
        if (keys.Local is { } local) Send(peer, local);
    }

    // A friend who is gone is told again when they come back.
    public void Forget(ulong peer)
    {
        lock (told) told.Remove(peer);
    }

    void Send(ulong peer, string key) => transport.Send(peer, new Frame("account.key").Add(key).Encode(), reliable: true);
}

namespace HellForge.LocalBackend;

// Players' public keys. Each game sends a 32-byte key as text with POST /Account/Login ("publicKey", body built near
// game.dll 0x12b949c; encoded into a 64-byte buffer by the engine, so not 64 hex digits - kept and served verbatim). A game admits a player to its session only when GET /Account/Keys?id=<platform account id>
// (builder 0x12be5a0, response {"accountKeys":[{"platformAccountId","key"}]} parsed at 0x1042120) returns the key that
// player's game shows on the game connection (check at 0x108f92b); with no key the joiner is never admitted and sees
// "lobby timeout". Platform account ids are SteamID64s. Friends' backends exchange their own player's key over the peer
// link (Multiplayer.AccountKeyExchange), so a backend can answer for every player in its session.
public sealed class AccountKeys
{
    readonly object gate = new();
    readonly Dictionary<ulong, string> remote = [];
    string? local;
    ulong? localId;

    // Raised with the new key when this machine's game logs in with a different key.
    public event Action<string>? LocalChanged;

    // Multiplayer's log, for lookups that find no key.
    public Action<string>? Log { get; set; }

    // Whatever text the game sent (first two-PC test: a 64-hex-digit rule dropped every real key).
    public static bool IsKey(string? text) => text is { Length: > 0 and <= 256 } && text.All(c => c is > ' ' and <= '~');

    public string? Local { get { lock (gate) return local; } }

    // This machine's SteamID64, known once the peer link has started.
    public ulong? LocalId
    {
        get { lock (gate) return localId; }
        set { lock (gate) localId = value; }
    }

    public void SetLocal(string key)
    {
        if (!IsKey(key)) throw new ArgumentException("A public key is 1 to 256 printable characters.", nameof(key));
        lock (gate)
        {
            if (local == key) return;
            local = key;
        }
        LocalChanged?.Invoke(key);
    }

    // True when the key is new or changed (the friend's game logged in again).
    public bool SetRemote(ulong id, string key)
    {
        if (!IsKey(key)) throw new ArgumentException("A public key is 1 to 256 printable characters.", nameof(key));
        lock (gate)
        {
            if (remote.TryGetValue(id, out var known) && known == key) return false;
            remote[id] = key;
            return true;
        }
    }

    public string? Find(ulong id)
    {
        lock (gate) return id == localId ? local : remote.GetValueOrDefault(id);
    }

    // A friend's key can still be on its way when their game joins, so a lookup waits briefly for it.
    public async Task<string?> WaitAsync(ulong id, TimeSpan timeout, CancellationToken cancellation)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (true)
        {
            if (Find(id) is { } key) return key;
            if (DateTime.UtcNow >= deadline) return null;
            await Task.Delay(100, cancellation);
        }
    }
}

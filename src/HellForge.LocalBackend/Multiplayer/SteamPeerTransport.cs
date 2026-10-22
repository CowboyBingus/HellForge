using System.Collections.Concurrent;
using System.Runtime.InteropServices;

namespace HellForge.LocalBackend.Multiplayer;

// Backend-to-backend messages through Steam networking (ISteamNetworkingMessages over Valve's relays, peers named by
// SteamID). Runs in the backend process, which the network block does not list; the copied game itself only talks to
// loopback. The backend starts the Steam API as the game's app beside the running game (verified with a
// probe program, docs/multiplayer.md). Sessions are accepted only from the signed-in account's Steam
// friends. Every Steam call happens on one thread.
// Friends who run HellForge are found for the social tab by a rich presence key ("hellforge" = the multiplayer protocol) that the
// backend sets for the game's app beside the game's own keys: only friends who are playing HD2 are looked at, and nothing
// is sent to anyone to find them.
public sealed unsafe class SteamPeerTransport : IPeerTransport, IFriendSource
{
    const int AppId = 553850;
    const int Channel = 0;
    // k_nSteamNetworkingSend_Reliable / _Unreliable, each with _AutoRestartBrokenSession. Unreliable (not NoDelay) is
    // queued while a route is being set up: NoDelay dropped a joining player's first game messages (first two-PC test).
    const int SendReliable = 8 | 32, SendUnreliable = 0 | 32;
    const int SessionRequest = 1251, SessionFailed = 1252; // SteamNetworkingMessagesSessionRequest_t, ..SessionFailed_t
    const int IdentitySize = 136, IdentitySteamId = 16; // SteamNetworkingIdentity, k_ESteamNetworkingIdentityType_SteamID
    const int FriendRelationshipFriend = 3;
    const int FriendFlagImmediate = 4;
    const string PresenceKey = "hellforge";
    static readonly TimeSpan FriendScanInterval = TimeSpan.FromSeconds(5);

    readonly string library;
    readonly Action<string> log;
    readonly ConcurrentQueue<(ulong Peer, byte[] Frame, bool Reliable)> outbox = new();
    readonly ManualResetEventSlim started = new();
    readonly CancellationTokenSource stop = new();
    Thread? thread;
    string? failure;
    public ulong LocalId { get; private set; }
    public event Action<ulong, byte[]>? Received;
    volatile IReadOnlyList<FriendPresence.Friend> hellforgeFriends = [];
    public IReadOnlyList<FriendPresence.Friend> HellForgeFriends => hellforgeFriends;

    public SteamPeerTransport(string steamApiLibrary, Action<string> log)
    {
        library = Path.GetFullPath(steamApiLibrary);
        this.log = log;
    }

    public void Start()
    {
        thread = new Thread(Run) { IsBackground = true, Name = "HellForge Steam peer link" };
        thread.Start();
        started.Wait();
        if (failure != null) throw new IOException(failure);
    }

    public void Send(ulong peer, byte[] frame, bool reliable) => outbox.Enqueue((peer, frame, reliable));

    void Run()
    {
        nint module = 0;
        var initialized = false;
        try
        {
            Environment.SetEnvironmentVariable("SteamAppId", AppId.ToString());
            Environment.SetEnvironmentVariable("SteamGameId", AppId.ToString());
            module = NativeLibrary.Load(library);
            nint E(string name) => NativeLibrary.GetExport(module, name);
            initialized = ((delegate* unmanaged[Cdecl]<byte>)E("SteamAPI_Init"))() != 0;
            if (!initialized) throw new IOException("SteamAPI_Init failed: is Steam running and signed in?");
            var pipe = ((delegate* unmanaged[Cdecl]<int>)E("SteamAPI_GetHSteamPipe"))();
            ((delegate* unmanaged[Cdecl]<void>)E("SteamAPI_ManualDispatch_Init"))();
            var runFrame = (delegate* unmanaged[Cdecl]<int, void>)E("SteamAPI_ManualDispatch_RunFrame");
            var nextCallback = (delegate* unmanaged[Cdecl]<int, byte*, byte>)E("SteamAPI_ManualDispatch_GetNextCallback");
            var freeCallback = (delegate* unmanaged[Cdecl]<int, void>)E("SteamAPI_ManualDispatch_FreeLastCallback");
            var user = ((delegate* unmanaged[Cdecl]<nint>)E("SteamAPI_SteamUser_v021"))();
            var friends = ((delegate* unmanaged[Cdecl]<nint>)E("SteamAPI_SteamFriends_v017"))();
            var utils = ((delegate* unmanaged[Cdecl]<nint>)E("SteamAPI_SteamNetworkingUtils_SteamAPI_v004"))();
            var messages = ((delegate* unmanaged[Cdecl]<nint>)E("SteamAPI_SteamNetworkingMessages_SteamAPI_v002"))();
            if (user == 0 || friends == 0 || utils == 0 || messages == 0) throw new IOException("Steam networking interfaces are unavailable.");
            LocalId = ((delegate* unmanaged[Cdecl]<nint, ulong>)E("SteamAPI_ISteamUser_GetSteamID"))(user);
            var relationship = (delegate* unmanaged[Cdecl]<nint, ulong, int>)E("SteamAPI_ISteamFriends_GetFriendRelationship");
            var setSteamId = (delegate* unmanaged[Cdecl]<byte*, ulong, void>)E("SteamAPI_SteamNetworkingIdentity_SetSteamID64");
            var send = (delegate* unmanaged[Cdecl]<nint, byte*, byte*, uint, int, int, int>)E("SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
            var receive = (delegate* unmanaged[Cdecl]<nint, int, nint*, int, int>)E("SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
            var accept = (delegate* unmanaged[Cdecl]<nint, byte*, byte>)E("SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
            var release = (delegate* unmanaged[Cdecl]<nint, void>)E("SteamAPI_SteamNetworkingMessage_t_Release");
            ((delegate* unmanaged[Cdecl]<nint, void>)E("SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess"))(utils);
            var setPresence = (delegate* unmanaged[Cdecl]<nint, byte*, byte*, byte>)E("SteamAPI_ISteamFriends_SetRichPresence");
            var friendCount = (delegate* unmanaged[Cdecl]<nint, int, int>)E("SteamAPI_ISteamFriends_GetFriendCount");
            var friendByIndex = (delegate* unmanaged[Cdecl]<nint, int, int, ulong>)E("SteamAPI_ISteamFriends_GetFriendByIndex");
            var gamePlayed = (delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte>)E("SteamAPI_ISteamFriends_GetFriendGamePlayed");
            var requestPresence = (delegate* unmanaged[Cdecl]<nint, ulong, void>)E("SteamAPI_ISteamFriends_RequestFriendRichPresence");
            var friendPresence = (delegate* unmanaged[Cdecl]<nint, ulong, byte*, byte*>)E("SteamAPI_ISteamFriends_GetFriendRichPresence");
            var personaName = (delegate* unmanaged[Cdecl]<nint, ulong, byte*>)E("SteamAPI_ISteamFriends_GetFriendPersonaName");
            log("Steam peer link started.");
            started.Set();

            var key = System.Text.Encoding.UTF8.GetBytes(PresenceKey + "\0");
            var tag = System.Text.Encoding.UTF8.GetBytes(LobbyService.Protocol + "\0");
            var gameInfo = stackalloc byte[32]; // FriendGameInfo_t: CGameID, IP, ports, lobby
            var requested = new Dictionary<ulong, DateTime>();
            var lastScan = DateTime.MinValue;
            // Lists Steam friends playing HD2 whose backend set the tag; re-sets this machine's tag, which the game's own
            // rich presence calls may clear.
            void ScanFriends()
            {
                fixed (byte* k = key, v = tag) setPresence(friends, k, v);
                var found = new List<FriendPresence.Friend>();
                var count = friendCount(friends, FriendFlagImmediate);
                for (var i = 0; i < count; i++)
                {
                    var friend = friendByIndex(friends, i, FriendFlagImmediate);
                    new Span<byte>(gameInfo, 32).Clear();
                    if (gamePlayed(friends, friend, gameInfo) == 0 || (*(ulong*)gameInfo & 0xFFFFFF) != AppId) continue;
                    if (!requested.TryGetValue(friend, out var asked) || DateTime.UtcNow - asked > TimeSpan.FromMinutes(1))
                    {
                        requestPresence(friends, friend);
                        requested[friend] = DateTime.UtcNow;
                    }
                    byte* value;
                    fixed (byte* k = key) value = friendPresence(friends, friend, k);
                    if (value == null || *value == 0) continue;
                    found.Add(new FriendPresence.Friend(friend, Marshal.PtrToStringUTF8((nint)personaName(friends, friend)) ?? "Steam friend"));
                }
                hellforgeFriends = found;
            }

            var callback = stackalloc byte[24]; // CallbackMsg_t
            var identity = stackalloc byte[IdentitySize];
            var inbox = stackalloc nint[32];
            while (!stop.IsCancellationRequested)
            {
                runFrame(pipe);
                while (nextCallback(pipe, callback) != 0)
                {
                    var id = *(int*)(callback + 4);
                    var parameter = *(byte**)(callback + 8);
                    if (id is SessionRequest or SessionFailed && *(int*)parameter == IdentitySteamId)
                    {
                        var remote = *(ulong*)(parameter + 8);
                        if (id == SessionFailed) log("A Steam peer session failed.");
                        else if (relationship(friends, remote) == FriendRelationshipFriend) accept(messages, parameter);
                        else log("Refused a Steam peer session from someone who is not a Steam friend.");
                    }
                    freeCallback(pipe);
                }
                while (outbox.TryDequeue(out var outgoing))
                {
                    new Span<byte>(identity, IdentitySize).Clear();
                    setSteamId(identity, outgoing.Peer);
                    fixed (byte* data = outgoing.Frame)
                        send(messages, identity, data, (uint)outgoing.Frame.Length, outgoing.Reliable ? SendReliable : SendUnreliable, Channel);
                }
                int count;
                while ((count = receive(messages, Channel, inbox, 32)) > 0)
                    for (var i = 0; i < count; i++)
                    {
                        var message = inbox[i];
                        var peerIdentity = (byte*)message + 16; // SteamNetworkingMessage_t.m_identityPeer
                        if (*(int*)peerIdentity == IdentitySteamId)
                        {
                            var sender = *(ulong*)(peerIdentity + 8);
                            var frame = new ReadOnlySpan<byte>(*(byte**)message, *(int*)(message + 8)).ToArray();
                            try { Received?.Invoke(sender, frame); }
                            catch (Exception error) { log("Peer message handler failed: " + error.Message); }
                        }
                        release(message);
                    }
                if (DateTime.UtcNow - lastScan > FriendScanInterval)
                {
                    lastScan = DateTime.UtcNow;
                    ScanFriends();
                }
                Thread.Sleep(2);
            }
            // The tag goes when the backend does (an empty value deletes the key, leaving the game's own keys alone).
            fixed (byte* k = key, v = "\0"u8) setPresence(friends, k, v);
        }
        catch (Exception error) when (error is IOException or DllNotFoundException or EntryPointNotFoundException or BadImageFormatException)
        {
            failure = error.Message;
            started.Set();
        }
        finally
        {
            if (initialized) ((delegate* unmanaged[Cdecl]<void>)NativeLibrary.GetExport(module, "SteamAPI_Shutdown"))();
            if (module != 0) NativeLibrary.Free(module);
        }
    }

    public void Dispose()
    {
        stop.Cancel();
        thread?.Join(TimeSpan.FromSeconds(5));
    }
}

using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO.Pipes;
using System.Text;
using HellForge.Core;
using HellForge.LocalBackend.Multiplayer;

// Mod sync end to end: a host's and a joiner's lobby services and mod sync over the in-memory network, the joiner's game
// a display that records each offer. Every mod is synthetic.
static class ModSyncTests
{
    const ulong HostId = 0x110000100000021, GuestId = 0x110000100000022, StrangerId = 0x110000100000023;

    sealed class Display : ISyncDisplay
    {
        public readonly BlockingCollection<SyncView> Views = new();
        public Task<bool> ShowAsync(SyncView view, bool withMods) { Views.Add(view); return Task.FromResult(true); }

        // The next view in one of these states (progress views on the way are skipped).
        public SyncView Next(Action<bool, string> check, params string[] states)
        {
            var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(20);
            while (Views.TryTake(out var view, deadline - DateTime.UtcNow > TimeSpan.Zero ? deadline - DateTime.UtcNow : TimeSpan.Zero))
                if (states.Contains(view.State)) return view;
                else if (view.State is not ("downloading" or "offer")) { check(false, $"expected {string.Join("/", states)}, got {view.State}: {view.Message}"); return view; }
            check(false, $"expected {string.Join("/", states)}, got nothing");
            return null!;
        }
    }

    sealed class Node
    {
        public readonly BlockingCollection<Frame> Inbox = new();
        public readonly LobbyService Lobby;
        public readonly ModSync Sync;
        public readonly Display Shown = new();
        public readonly string Mods, Run, SessionFile;
        public readonly List<string> Log = [];

        public Node(InMemoryNetwork network, ulong id, string folder, ModSet own, Func<string, string, bool> verified)
        {
            Mods = Path.Combine(folder, "mods");
            Run = Directory.CreateDirectory(Path.Combine(folder, "run")).FullName;
            var evidence = Directory.CreateDirectory(Path.Combine(Run, "backend")).FullName;
            SessionFile = ModSyncSession.PathIn(new InstallLayout(folder));
            var transport = network.Join(id);
            void Note(string line) { lock (Log) Log.Add(line); }
            Lobby = new LobbyService(transport, Inbox.Add, Note, own.Digest);
            Sync = new ModSync(transport, Mods, own, Run, evidence, SessionFile, peer => "Player " + LobbyService.EntityId(peer), Note, verified) { Display = Shown };
            Lobby.ModsRefused = Sync.OfferMods;
            Sync.Expected = Lobby.JoinedRecently;
            transport.Received += (peer, bytes) =>
            {
                var frame = Frame.Decode(bytes);
                if (frame.Type.StartsWith("mods.", StringComparison.Ordinal)) Sync.FromPeer(peer, frame);
                else Lobby.FromPeer(peer, frame);
            };
        }

        public Frame Next(string type, Action<bool, string> check)
        {
            var got = Inbox.TryTake(out var frame, TimeSpan.FromSeconds(5));
            check(got && frame!.Type == type, $"expected {type}, got {(got ? frame!.Type : "nothing")}");
            return frame!;
        }
    }

    static void Write(string folder, string relative, string text)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(folder, relative))!);
        File.WriteAllText(Path.Combine(folder, relative), text);
    }

    const string Ammo = """{"format":1,"id":"test.ammo","name":"Ammo","version":"1","native":{"networkFieldTypes":[{"name":"deposit_value","expect":{"kind":1,"bits":10,"min":0},"bits":14}]}}""";

    // A data patch of size bytes: an empty resource table, then a pattern.
    static void Patch(string path, int size, byte seed)
    {
        var bytes = new byte[size];
        for (var i = 72; i < size; i++) bytes[i] = (byte)(i * 31 + seed);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, bytes);
    }

    // The host: native edits, a 600 KB data patch (several chunks) and a prebuilt DLL. The joiner has the native edits only.
    static (ModSet Host, ModSet Guest) Fixture(string root)
    {
        var host = Path.Combine(root, "host", "mods");
        Write(Path.Combine(host, "ammo"), ModCatalog.ManifestName, Ammo);
        Write(Path.Combine(host, "texture"), ModCatalog.ManifestName, """{"format":1,"id":"test.texture","name":"Texture","version":"2"}""");
        Patch(Path.Combine(host, "texture", "data", "9ba626afa44a3aa3.patch_0"), 600_000, 7);
        Write(Path.Combine(host, "code"), ModCatalog.ManifestName, """{"format":1,"id":"test.code","name":"Code","version":"1","code":{"dll":"mod.dll","sdk":1}}""");
        Write(Path.Combine(host, "code"), "mod.dll", "synthetic dll");
        var guest = Path.Combine(root, "guest", "mods");
        Write(Path.Combine(guest, "ammo"), ModCatalog.ManifestName, Ammo);
        return (ModSet.Of(ModCatalog.LoadOrder(ModCatalog.Load(host).Mods)), ModSet.Of(ModCatalog.LoadOrder(ModCatalog.Load(guest).Mods)));
    }

    // The host creates a lobby; the guest joins and is refused for its mods: the offer it is shown.
    static SyncView Refused(Node host, Node guest, Action<bool, string> check, string op, params string[] states)
    {
        host.Lobby.FromLocal(MultiplayerTests.Create(op + "-create", HostId));
        var created = host.Next("lobby.result", check);
        guest.Lobby.FromLocal(MultiplayerTests.Join(op + "-join", GuestId, created.Text(4)));
        var refused = guest.Next("lobby.result", check);
        check(refused.Text(2) == LobbyService.LobbyNotJoinable.ToString(), "The join is refused like a lobby that cannot be joined.");
        return guest.Shown.Next((ok, why) => { lock (guest.Log) lock (host.Log) check(ok, why + " | joiner log: " + string.Join(" / ", guest.Log.TakeLast(6)) +
            " | host log: " + string.Join(" / ", host.Log.TakeLast(6))); }, states);
    }

    public static async Task Run(string root, Action<bool, string> check)
    {
        root = Path.Combine(root, "mod-sync");
        var (hostSet, guestSet) = Fixture(root);
        var allowCode = false;
        bool Verified(string id, string hash) => allowCode && hostSet.Find(id, hash)?.Class == ModClass.Code;
        var network = new InMemoryNetwork();
        var host = new Node(network, HostId, Path.Combine(root, "host"), hostSet, Verified);
        var guest = new Node(network, GuestId, Path.Combine(root, "guest"), guestSet, Verified);
        SyncAction Action(SyncView view, string id) => view.Items.Single(i => i.Mod.Id == id).Action;

        // Unverified code: the lobby cannot be joined, and the code mod is named.
        var blocked = Refused(host, guest, check, "a", "blocked");
        check(Action(blocked, "test.ammo") == SyncAction.Have && Action(blocked, "test.texture") == SyncAction.Download && Action(blocked, "test.code") == SyncAction.Manual &&
            blocked.Message.Contains("Code") && blocked.Message.Contains("not verified"), "Unverified code: " + blocked.Message);
        check(File.Exists(Path.Combine(guest.Run, "backend", ModSync.OfferFile)), "The offer is in the session's logs.");

        // Verified code: an offer; accepted, everything downloads, is checked and stored; restart writes the next session.
        allowCode = true;
        var offer = Refused(host, guest, check, "b", "offer");
        var expected = hostSet.Mods.Where(m => m.Id != "test.ammo").Sum(m => m.Bytes);
        check(offer.Bytes == expected && Action(offer, "test.code") == SyncAction.Download && offer.Message.Contains("2 mod(s)"), "Offer: " + offer.Message);
        guest.Sync.Answer(["syncanswer", offer.Version.ToString(), "accept"]);
        var ready = guest.Shown.Next(check, "ready", "failed");
        check(ready.State == "ready" && ready.Done == expected, $"Downloaded: {ready.State} {ready.Message} ({ready.Done} of {expected})");
        foreach (var mod in hostSet.Mods.Where(m => m.Id != "test.ammo"))
        {
            var stored = SyncedStore.Find(guest.Mods, mod.Id, mod.Hash);
            check(stored != null && ModSets.ContentHash(ModCatalog.Parse(stored, EntityProfile.Current)) == mod.Hash, mod.Id + " is stored as the host has it.");
        }
        guest.Sync.Answer(["syncanswer", ready.Version.ToString(), "restart"]);
        var signal = Path.Combine(guest.Run, ModSync.StopSignal);
        var waited = Stopwatch.StartNew();
        while (!File.Exists(signal) && waited.Elapsed < TimeSpan.FromSeconds(10)) await Task.Delay(50);
        check(File.Exists(signal) && File.ReadAllText(signal) == ModSync.StopReason, "The session is asked to stop for a restart.");
        check(guest.Shown.Next(check, "ready").Message.Contains("Restarting"), "The game is told it restarts.");
        var session = ModSyncSession.Read(guest.SessionFile);
        var (order, problem) = session.Resolve(guest.Mods, ModCatalog.Load(guest.Mods).Mods);
        check(order != null && ModSet.Of(order).Digest == hostSet.Digest && session.Connection.StartsWith(LobbyService.ConnectionPrefix, StringComparison.Ordinal),
            "The next session plays the host's mods: " + problem);
        File.Delete(signal);

        // Joined again with everything here: nothing to download, a restart is enough.
        var again = Refused(host, guest, check, "c", "offer");
        check(again.Bytes == 0 && again.Items.All(i => i.Action == SyncAction.Have) && again.Message.Contains("Restart"), "Ready: " + again.Message);
        guest.Sync.Answer(["syncanswer", again.Version.ToString(), "decline"]);
        check(guest.Shown.Next(check, "none").Items.Count == 0, "Declined: the offer closes.");

        // Blocking a mod from the offer: saved, and the lobby is shown as blocked.
        foreach (var mod in hostSet.Mods.Where(m => m.Id != "test.ammo")) Directory.Delete(SyncedStore.Find(guest.Mods, mod.Id, mod.Hash)!, recursive: true);
        var toBlock = Refused(host, guest, check, "d", "offer");
        guest.Sync.Answer(["syncanswer", toBlock.Version.ToString(), "block", "test.texture"]);
        var nowBlocked = guest.Shown.Next(check, "blocked");
        check(Action(nowBlocked, "test.texture") == SyncAction.Blocked && ModSettings.Read(guest.Mods).Blocked.Contains("test.texture") && nowBlocked.Message.Contains("Texture"),
            "Blocked: " + nowBlocked.Message);
        ModSettings.SetBlocked(guest.Mods, "test.texture", false);

        // A host that does not share: nothing downloads.
        ModSettings.SetShare(host.Mods, false);
        var unshared = Refused(host, guest, check, "e", "blocked");
        check(unshared.Message.Contains("does not share") && Action(unshared, "test.texture") == SyncAction.Manual, "Not shared: " + unshared.Message);
        ModSettings.SetShare(host.Mods, true);

        // The host serves only a granted machine, only the set's files and parts.
        var stranger = network.Join(StrangerId);
        var replies = new BlockingCollection<Frame>();
        stranger.Received += (_, bytes) => replies.Add(Frame.Decode(bytes));
        var texture = hostSet.Mods.Single(m => m.Id == "test.texture");
        Frame Ask(IPeerTransport from, Frame request)
        {
            from.Send(HostId, request.Encode(), reliable: true);
            check(replies.TryTake(out var reply, TimeSpan.FromSeconds(5)), "The host answers " + request.Type);
            return reply!;
        }
        check(Ask(stranger, new Frame("mods.files").Add("1").Add(texture.Id).Add(texture.Hash)).Text(1) == "refused", "No grant, no files.");
        host.Lobby.FromLocal(MultiplayerTests.Create("f-create", HostId));
        var lobby = host.Next("lobby.result", check).Text(4);
        stranger.Send(HostId, MultiplayerTests.Join("f-join", StrangerId, lobby).Add(new string('0', 32)).Add(LobbyService.Protocol).Encode(), reliable: true);
        // The offer, the refusal and the join's result (the test transport may reorder them).
        var answers = new List<Frame>();
        while (answers.Count < 3 && replies.TryTake(out var answer, TimeSpan.FromSeconds(5))) answers.Add(answer);
        check(answers.Any(a => a.Type == "mods.offer" && a.Text(0) == lobby.Split(';')[1] && a.Text(2) == "1" && ModSet.Parse(a.Text(1), local: false).Digest == hostSet.Digest) &&
            answers.Any(a => a.Type == "lobby.refused" && Enumerable.Range(0, a.Count).Any(i => a.Text(i).Contains("MOD SYNC"))) &&
            answers.Any(a => a.Type == "lobby.result"), "A refusal for mods names its lobby and carries the set.");
        foreach (var bad in new[] {
                     new Frame("mods.get").Add("2").Add(texture.Id).Add(texture.Hash).Add("hellforge-mod.json").Add(0).Add(ModSync.ChunkBytes + 1),
                     new Frame("mods.get").Add("3").Add(texture.Id).Add(texture.Hash).Add("../ammo/hellforge-mod.json").Add(0).Add(10),
                     new Frame("mods.get").Add("4").Add(texture.Id).Add(texture.Hash).Add("data/9ba626afa44a3aa3.patch_0").Add(599_990).Add(100),
                     new Frame("mods.files").Add("5").Add("test.ammo").Add(new string('a', 64)) })
            check(Ask(stranger, bad).Text(1) == "refused", "Refused: " + bad.Text(3 < bad.Count ? 3 : 1));
        var listed = Ask(stranger, new Frame("mods.files").Add("6").Add(texture.Id).Add(texture.Hash));
        check(listed.Text(1) == "ok" && listed.Int(2) == texture.Files.Count, "A granted machine gets the file list.");
        // Pacing: about 1 MiB/s to one machine (600 KB in five chunks takes at least half a second).
        var clock = Stopwatch.StartNew();
        for (var offset = 0; offset < 600_000; offset += ModSync.ChunkBytes)
            check(Ask(stranger, new Frame("mods.get").Add("p" + offset).Add(texture.Id).Add(texture.Hash).Add("data/9ba626afa44a3aa3.patch_0")
                .Add(offset).Add(Math.Min(ModSync.ChunkBytes, 600_000 - offset))).Text(1) == "ok", "A chunk is served.");
        check(clock.Elapsed > TimeSpan.FromSeconds(0.4), $"The host paced its upload ({clock.ElapsedMilliseconds} ms).");

        // A host whose copy changed (same size, other bytes): the download fails its check and nothing is stored.
        var patch = Path.Combine(host.Mods, "texture", "data", "9ba626afa44a3aa3.patch_0");
        Patch(patch, 600_000, 8);
        var changed = Refused(host, guest, check, "g", "offer");
        guest.Sync.Answer(["syncanswer", changed.Version.ToString(), "accept"]);
        var failed = guest.Shown.Next(check, "failed");
        check(failed.Message.Contains("not the file the host listed") && SyncedStore.Find(guest.Mods, texture.Id, texture.Hash) == null &&
            !Directory.Exists(Path.Combine(SyncedStore.Root(guest.Mods), ".partial", texture.Id + "@" + texture.Hash[..16])), "Tampered: " + failed.Message);
        Patch(patch, 600_000, 7);

        // A host that calls its code mod content: the joiner reads the manifest and stops before the DLL.
        allowCode = false;
        var code = hostSet.Mods.Single(m => m.Id == "test.code");
        Directory.Delete(SyncedStore.Find(guest.Mods, code.Id, code.Hash)!, recursive: true);
        var liar = new Node(network, 0x110000100000024, Path.Combine(root, "liar"), hostSet with { Mods = [.. hostSet.Mods.Select(m => m.Id == code.Id ? m with { Class = ModClass.Content } : m)] },
            Verified);
        liar.Lobby.FromLocal(MultiplayerTests.Create("h-create", 0x110000100000024));
        guest.Lobby.FromLocal(MultiplayerTests.Join("h-join", GuestId, liar.Next("lobby.result", check).Text(4)));
        guest.Next("lobby.result", check);
        var lied = guest.Shown.Next(check, "offer");
        check(Action(lied, "test.code") == SyncAction.Download, "The joiner first believes the host's class.");
        guest.Sync.Answer(["syncanswer", lied.Version.ToString(), "accept"]);
        var caught = guest.Shown.Next(check, "failed");
        check(caught.Message.Contains("runs code") && SyncedStore.Find(guest.Mods, code.Id, code.Hash) == null, "A lying host is caught: " + caught.Message);

        await LiveDisplay(root, check);
        await liar.Sync.DisposeAsync();
        await guest.Sync.DisposeAsync();
        await host.Sync.DisposeAsync();
    }

    // The game's side over the live-edit pipe: the offer's lines, and its answer back.
    static async Task LiveDisplay(string root, Action<bool, string> check)
    {
        var runId = "modsync-" + Guid.NewGuid().ToString("N")[..8];
        await using var live = new LiveEditServer(runId);
        var answers = new BlockingCollection<string[]>();
        live.Received += parts => answers.Add(parts);
        live.Start();
        using var pipe = new NamedPipeClientStream(".", LiveEditServer.PipeName(runId), PipeDirection.InOut, PipeOptions.Asynchronous);
        await pipe.ConnectAsync(5000);
        using var reader = new StreamReader(pipe, new UTF8Encoding(false));
        using var writer = new StreamWriter(pipe, new UTF8Encoding(false)) { NewLine = "\n", AutoFlush = true };
        await writer.WriteLineAsync($"hello\t{LiveEditServer.Protocol}\t{StatsSchema.Current.Build}\t{StatsSchema.Fingerprint}");
        var waited = Stopwatch.StartNew();
        while (!live.Connected && waited.Elapsed < TimeSpan.FromSeconds(5)) await Task.Delay(20);
        var lines = new List<string>();
        var game = Task.Run(async () =>
        {
            while (await reader.ReadLineAsync() is { } line)
            {
                var parts = line.Split('\t');
                lock (lines) lines.Add(string.Join('\t', parts.Where((_, i) => i != 1)));
                await writer.WriteLineAsync(parts[1] + "\tok\tnow"); /* as the loader answers (live.c) */
                if (parts[0] == "syncstate") return;
            }
        });
        var mod = new ModSetEntry("test.tab", "Name\twith a tab", "1", new string('a', 64), 1234, ModClass.Code, 0);
        var view = new SyncView(7, "offer", "Host\nName", 1234, 0, "Message", [new SyncItem(mod, SyncAction.Download, null)]);
        check(await new LiveSyncDisplay(live).ShowAsync(view, withMods: true), "The game took the offer.");
        await game;
        check(lines.SequenceEqual(["syncmod\t7\t0\tdownload\tcode\t1234\ttest.tab\tNamewith a tab\t1", "syncstate\t7\toffer\tHostName\t1234\t0\t1\tMessage"]),
            "The lines: " + string.Join(" | ", lines));
        await writer.WriteLineAsync("syncanswer\t7\tblock\ttest.tab");
        check(answers.TryTake(out var answer, TimeSpan.FromSeconds(5)) && answer!.SequenceEqual(["syncanswer", "7", "block", "test.tab"]), "The game's answer arrives.");
    }
}

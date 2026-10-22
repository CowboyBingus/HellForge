using System.Collections.Concurrent;
using System.Globalization;
using System.IO.Pipes;
using System.Text;
using HellForge.Core;
using HellForge.LocalBackend.Multiplayer;

// The host's stats in co-op, end to end: two backends' lobbies and stats sharing over the in-memory network, the
// joiner's game a fake mod loader on its live-edit pipe that answers every request and records it.
static class StatsShareTests
{
    const ulong HostId = 0x110000100000011, GuestId = 0x110000100000012;

    sealed class Game : IAsyncDisposable
    {
        public readonly BlockingCollection<string> Requests = new();
        readonly CancellationTokenSource stop = new();
        readonly Task running;

        public Game(string runId) => running = Task.Run(() => Serve(runId));

        async Task Serve(string runId)
        {
            while (!stop.IsCancellationRequested)
            {
                using var pipe = new NamedPipeClientStream(".", LiveEditServer.PipeName(runId), PipeDirection.InOut, PipeOptions.Asynchronous);
                try { await pipe.ConnectAsync(200, stop.Token); }
                catch (TimeoutException) { continue; }
                catch (OperationCanceledException) { return; }
                using var reader = new StreamReader(pipe, new UTF8Encoding(false));
                using var writer = new StreamWriter(pipe, new UTF8Encoding(false)) { NewLine = "\n", AutoFlush = true };
                await writer.WriteLineAsync($"hello\t{LiveEditServer.Protocol}\t{StatsSchema.Current.Build}\t{StatsSchema.Fingerprint}");
                try
                {
                    while (await reader.ReadLineAsync(stop.Token) is { } line)
                    {
                        var parts = line.Split('\t');
                        Requests.Add(string.Join('\t', parts.Where((_, i) => i != 1))); /* without its sequence number */
                        await writer.WriteLineAsync(parts[1] + "\tok\tnow");
                    }
                }
                catch (Exception error) when (error is IOException or OperationCanceledException) { }
            }
        }

        // The requests that arrive within the time: every one, until none comes for a moment.
        public List<string> Take(int atLeast)
        {
            var got = new List<string>();
            var until = DateTime.UtcNow.AddSeconds(8);
            while (DateTime.UtcNow < until)
            {
                if (Requests.TryTake(out var request, TimeSpan.FromMilliseconds(got.Count >= atLeast ? 600 : 200))) got.Add(request);
                else if (got.Count >= atLeast) break;
            }
            return got;
        }

        public async ValueTask DisposeAsync()
        {
            stop.Cancel();
            await running;
        }
    }

    static string Stats(string weapons) => "{\"format\":1,\"weapons\":{" + weapons + "}}";
    static string Bits(float value) => BitConverter.SingleToUInt32Bits(value).ToString("x8", CultureInfo.InvariantCulture);

    public static async Task Run(string root, Action<bool, string> check)
    {
        var hostMods = Directory.CreateDirectory(Path.Combine(root, "stats-host", "mods")).FullName;
        var guestMods = Directory.CreateDirectory(Path.Combine(root, "stats-guest", "mods")).FullName;
        var hostFile = Path.Combine(Directory.CreateDirectory(Path.Combine(hostMods, "zz.local.stats")).FullName, StatsShare.FileName);
        var guestFile = Path.Combine(Directory.CreateDirectory(Path.Combine(guestMods, "zz.local.stats")).FullName, StatsShare.FileName);
        File.WriteAllText(hostFile, Stats("\"AR-23 Liberator\":{\"weapon.fire_rate\":900}"));
        File.WriteAllText(guestFile, Stats("\"AR-23 Liberator\":{\"weapon.fire_rate\":700},\"R-36 Eruptor\":{\"weapon.fire_rate\":400}"));
        // the in-game editor's mod content values: the host's go to the joiner's editor folder (host-mod-stats.json)
        var hostEditor = Directory.CreateDirectory(Path.Combine(hostMods, BundledMods.EditorFolder)).FullName;
        var guestEditor = Directory.CreateDirectory(Path.Combine(guestMods, BundledMods.EditorFolder)).FullName;
        var hostModValues = Path.Combine(hostEditor, StatsShare.ModValuesName);
        var guestHostValues = Path.Combine(guestEditor, StatsShare.HostModValuesName);
        var guestHostStats = Path.Combine(guestEditor, StatsShare.HostStatsName); /* the host's Local Stats, which the editor shows */
        const string silo = "{\"format\":1,\"mod:skyeshade/compendium/shredder_silo\":{\"skyeshade/compendium/shredder_silo\":{\"stratagem.uses\":2}}}";
        File.WriteAllText(hostModValues, silo);
        File.WriteAllText(Path.Combine(guestEditor, StatsShare.ModValuesName), silo.Replace(":2}", ":9}"));
        File.WriteAllText(guestHostValues, "left by a session cut short");
        File.WriteAllText(guestHostStats, "left by a session cut short");
        // the editor's arm swaps (its settings' "mounts"): the host's go to the joiner's editor folder (host-mounts.json)
        var guestHostMounts = Path.Combine(guestEditor, StatsShare.HostMountsName);
        File.WriteAllText(Path.Combine(hostEditor, StatsShare.EditorSettingsName), "{\"ui_scale\": 1, \"mounts\": {\"M-102 Gunner FRV|mount.slot_0\": 4}}");
        File.WriteAllText(guestHostMounts, "left by a session cut short");
        static bool Eventually(Func<bool> test)
        {
            for (var deadline = DateTime.UtcNow.AddSeconds(10); DateTime.UtcNow < deadline; Thread.Sleep(50)) if (test()) return true;
            return test();
        }
        static string? Read(string path) { try { return File.Exists(path) ? File.ReadAllText(path) : null; } catch (IOException) { return null; } }

        var network = new InMemoryNetwork();
        var hostLink = network.Join(HostId);
        var guestLink = network.Join(GuestId);
        var hostInbox = new BlockingCollection<Frame>();
        var guestInbox = new BlockingCollection<Frame>();
        var hostLobby = new LobbyService(hostLink, hostInbox.Add, _ => { }, "mods");
        var guestLobby = new LobbyService(guestLink, guestInbox.Add, _ => { }, "mods");
        var log = new ConcurrentQueue<string>();
        var runId = "stats-test-" + Guid.NewGuid().ToString("N")[..8];
        await using var hostShare = new StatsShare(hostLink, hostLobby, "stats-host-" + runId, hostMods, _ => "Host", log.Enqueue);
        await using var guestShare = new StatsShare(guestLink, guestLobby, runId, guestMods, id => id == HostId ? "Friend Host" : "?", log.Enqueue);
        check(!File.Exists(guestHostValues) && !File.Exists(guestHostStats) && !File.Exists(guestHostMounts), "A host's files left by a session cut short are removed.");
        void Wire(IPeerTransport link, LobbyService lobby, StatsShare share) => link.Received += (peer, bytes) =>
        {
            var frame = Frame.Decode(bytes);
            if (frame.Type.StartsWith("stats.", StringComparison.Ordinal)) share.FromPeer(peer, frame);
            else lobby.FromPeer(peer, frame);
        };
        Wire(hostLink, hostLobby, hostShare);
        Wire(guestLink, guestLobby, guestShare);
        await using var game = new Game(runId);

        // Alone, the game is told it is solo and nothing changes.
        var alone = game.Take(1);
        check(alone.SequenceEqual(["session\tsolo\t"]), "Alone, the game is solo: " + string.Join(" | ", alone));

        // The guest joins the host's lobby: its game plays with the host's values, its own Eruptor change gives way.
        hostLobby.FromLocal(MultiplayerTests.Create("c1", HostId));
        hostInbox.TryTake(out var created, TimeSpan.FromSeconds(5));
        var connection = created!.Text(4);
        guestLobby.FromLocal(MultiplayerTests.Join("j1", GuestId, connection));
        var joined = game.Take(3);
        var liberator = StatsSchema.Current.Targets["weapons"]["AR-23 Liberator"].Fields["weapon.fire_rate"];
        var eruptor = StatsSchema.Current.Targets["weapons"]["R-36 Eruptor"].Fields["weapon.fire_rate"];
        string Id(StatsField f) => string.Join('\t', f.Kind, f.Component, Convert.ToUInt64(f.Resource[2..], 16).ToString("x16", CultureInfo.InvariantCulture),
            f.Record.ToString(CultureInfo.InvariantCulture), f.Offset.ToString(CultureInfo.InvariantCulture), f.Storage);
        check(joined.Contains("session\tjoiner\tFriend Host") && joined.Contains($"set\t{Id(liberator)}\t{Bits(900)}\t{(liberator.Shared ? 1 : 0)}") &&
            joined.Contains($"base\t{Id(eruptor)}") && joined.Count == 3, "Joined: the host's values, the joiner's own at their base: " + string.Join(" | ", joined));
        check(Eventually(() => Read(guestHostValues) == silo), "Joined: the host's mod content values are in the joiner's editor folder.");
        check(Eventually(() => Read(guestHostStats) == Read(hostFile)), "Joined: the host's Local Stats file is beside them, for the editor to show.");
        check(Eventually(() => Read(guestHostMounts) == "{\"M-102 Gunner FRV|mount.slot_0\":4}"), "Joined: the host's arm swaps are beside them: " + Read(guestHostMounts));
        File.WriteAllText(hostModValues, silo.Replace(":2}", ":3}"));
        check(Eventually(() => Read(guestHostValues) == silo.Replace(":2}", ":3}")), "The host's change of a mod content value arrives.");

        // The host applies a change: it reaches the joiner's game.
        File.WriteAllText(hostFile, Stats("\"AR-23 Liberator\":{\"weapon.fire_rate\":1000}"));
        var changed = game.Take(1);
        check(changed.SequenceEqual([$"set\t{Id(liberator)}\t{Bits(1000)}\t{(liberator.Shared ? 1 : 0)}"]), "The host's change arrives: " + string.Join(" | ", changed));

        // The host's lobby closes: the joiner's own values come back and it is solo again.
        hostLobby.FromLocal(new Frame("lobby.leave").Add("l1").Add(created.Text(3)).Add(""));
        var left = game.Take(3);
        check(left.Contains("session\tsolo\t") && left.Contains($"set\t{Id(liberator)}\t{Bits(700)}\t{(liberator.Shared ? 1 : 0)}") &&
            left.Contains($"set\t{Id(eruptor)}\t{Bits(400)}\t{(eruptor.Shared ? 1 : 0)}") && left.Count == 3,
            "Without the host: the joiner's own values: " + string.Join(" | ", left));
        check(Eventually(() => !File.Exists(guestHostValues) && !File.Exists(guestHostStats) && !File.Exists(guestHostMounts)), "Without the host: its files are gone (the editor's own come back).");
        check(log.Any(l => l.Contains("sent this host's Local Stats")), "The host logged what it sent.");
    }
}

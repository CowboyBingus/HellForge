using System.Collections.Concurrent;
using System.IO.Pipes;
using System.Text;
using HellForge.Core;
using HellForge.LocalBackend.Multiplayer;

// Assets in co-op, end to end: a host and two joiners over the in-memory network, each game a fake mod loader on its
// live-edit pipe that says which packages it holds ("packages") and records what it is asked to load ("preload"). Every
// game loads the lobby's packages less its own, again when a player's change; malformed lists are dropped. Each player's
// projectile swaps (from their Local Stats) reach every game, by their peer ("shots").
static class AssetShareTests
{
    const ulong HostId = 0x110000100000021, FirstId = 0x110000100000022, SecondId = 0x110000100000023;
    const string P1 = "00000000000000a1", P2 = "00000000000000b2", P3 = "00000000000000c3";

    sealed class Game : IAsyncDisposable
    {
        public readonly BlockingCollection<string> Preloads = new(), Shots = new();
        readonly BlockingCollection<string> outgoing = new();
        readonly CancellationTokenSource stop = new();
        readonly Task running;
        public volatile string Packages;

        public Game(string runId, string packages)
        {
            Packages = packages;
            running = Task.Run(() => Serve(runId));
        }

        // Says the game's packages changed: the new list goes up the pipe as the loader's own line would.
        public void Hold(string packages)
        {
            Packages = packages;
            outgoing.Add("packages\t" + packages);
        }

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
                var writing = new SemaphoreSlim(1, 1);
                async Task Write(string line) { await writing.WaitAsync(); try { await writer.WriteLineAsync(line); } finally { writing.Release(); } }
                await Write($"hello\t{LiveEditServer.Protocol}\t{StatsSchema.Current.Build}\t{StatsSchema.Fingerprint}");
                await Write("packages\t" + Packages);
                using var connected = CancellationTokenSource.CreateLinkedTokenSource(stop.Token);
                var pushing = Task.Run(async () =>
                {
                    try { while (outgoing.TryTake(out var line, -1, connected.Token)) await Write(line); }
                    catch (Exception error) when (error is OperationCanceledException or IOException) { }
                });
                try
                {
                    while (await reader.ReadLineAsync(stop.Token) is { } line)
                    {
                        var parts = line.Split('\t');
                        if (parts[0] == "preload") Preloads.Add(parts[2]);
                        if (parts[0] == "shots") Shots.Add(parts[2]);
                        await Write(parts[1] + "\tok\tnow");
                    }
                }
                catch (Exception error) when (error is IOException or OperationCanceledException) { }
                connected.Cancel();
                await pushing;
            }
        }

        // The last list it was asked to load within the time, waiting until it equals `want` (or the time runs out).
        public string WaitFor(string want) => Wait(Preloads, want);
        public string WaitForShots(string want) => Wait(Shots, want);
        static string Wait(BlockingCollection<string> lines, string want)
        {
            var last = "";
            for (var until = DateTime.UtcNow.AddSeconds(10); DateTime.UtcNow < until;)
                if (lines.TryTake(out var list, TimeSpan.FromMilliseconds(100)) && (last = list) == want) return last;
            return last;
        }

        public async ValueTask DisposeAsync()
        {
            stop.Cancel();
            await running;
        }
    }

    sealed class Machine : IAsyncDisposable
    {
        public readonly LobbyService Lobby;
        public readonly BlockingCollection<Frame> Inbox = new();
        public readonly StatsShare Stats;
        public readonly AssetShare Assets;
        public readonly Game Game;

        public Machine(InMemoryNetwork network, ulong id, string root, string name, string packages, ConcurrentQueue<string> log, string? localStats = null)
        {
            var link = network.Join(id);
            Lobby = new LobbyService(link, Inbox.Add, _ => { }, "mods");
            var mods = Directory.CreateDirectory(Path.Combine(root, "assets-" + name, "mods")).FullName;
            if (localStats != null)
                File.WriteAllText(Path.Combine(Directory.CreateDirectory(Path.Combine(mods, "zz.local.stats")).FullName, StatsShare.FileName), localStats);
            var runId = "assets-" + name + "-" + Guid.NewGuid().ToString("N")[..8];
            Stats = new StatsShare(link, Lobby, runId, mods, _ => "Host", line => log.Enqueue(name + ": " + line));
            var stats = Stats;
            Assets = new AssetShare(link, Lobby, Stats.Live, line => log.Enqueue(name + ": " + line), () => stats.LocalStats, () => stats.LobbyPlaysHostStats);
            link.Received += (peer, bytes) =>
            {
                var frame = Frame.Decode(bytes);
                if (frame.Type.StartsWith("stats.", StringComparison.Ordinal)) Stats.FromPeer(peer, frame);
                else if (frame.Type.StartsWith("assets.", StringComparison.Ordinal)) Assets.FromPeer(peer, frame);
                else Lobby.FromPeer(peer, frame);
            };
            Game = new Game(runId, packages);
        }

        public async ValueTask DisposeAsync()
        {
            await Game.DisposeAsync();
            await Assets.DisposeAsync();
            await Stats.DisposeAsync();
        }
    }

    public static async Task Run(string root, Action<bool, string> check)
    {
        var network = new InMemoryNetwork();
        var log = new ConcurrentQueue<string>();
        await using var host = new Machine(network, HostId, root, "host", P1, log);
        // the first joiner swapped the One-Two's round to the Scorcher's (projectile 142) in its editor
        const string swap = """{"format":1,"weapons":{"AR/GL-21 One-Two":{"attack.primary.projectile":142}}}""";
        check(AssetShare.Swaps(swap) == "a955c4ea6f6d4203=142" && AssetShare.Swaps(null) == "-",
            "A player's projectile swaps from their Local Stats: " + AssetShare.Swaps(swap));
        await using var first = new Machine(network, FirstId, root, "first", P2, log, swap);
        await using var second = new Machine(network, SecondId, root, "second", "-", log);

        // Alone, nothing is asked of any game.
        await Task.Delay(1500);
        check(host.Game.Preloads.Count == 0 && first.Game.Preloads.Count == 0, "Alone, no game is asked to load anything.");

        // Both join the host's lobby: every game loads the others' packages.
        host.Lobby.FromLocal(MultiplayerTests.Create("c1", HostId));
        host.Inbox.TryTake(out var created, TimeSpan.FromSeconds(5));
        var connection = created!.Text(4);
        first.Lobby.FromLocal(MultiplayerTests.Join("j1", FirstId, connection));
        second.Lobby.FromLocal(MultiplayerTests.Join("j2", SecondId, connection));
        var hostLoads = host.Game.WaitFor(P2);
        var firstLoads = first.Game.WaitFor(P1);
        var secondLoads = second.Game.WaitFor(P1 + "," + P2);
        check(hostLoads == P2, "Joined: the host's game loads the first joiner's package: " + hostLoads);
        check(firstLoads == P1, "Joined: the first joiner's game loads the host's package, not its own: " + firstLoads);
        check(secondLoads == P1 + "," + P2, "Joined: the second joiner's game loads both: " + secondLoads);
        // ... and every game is told the first joiner's swap, by its peer: its copies of their shots fire it
        var rule = FirstId.ToString("x", System.Globalization.CultureInfo.InvariantCulture) + ":a955c4ea6f6d4203=142";
        var hostShots = host.Game.WaitForShots(rule);
        var secondShots = second.Game.WaitForShots(rule);
        check(hostShots == rule && secondShots == rule, "Joined: the first joiner's projectile swap reaches the others' games: " + hostShots + " | " + secondShots);

        // The first joiner applies a change that needs another package: it reaches every other game.
        first.Game.Hold(P2 + "," + P3);
        hostLoads = host.Game.WaitFor(P2 + "," + P3);
        secondLoads = second.Game.WaitFor(P1 + "," + P2 + "," + P3);
        check(hostLoads == P2 + "," + P3, "A joiner's new package reaches the host's game: " + hostLoads);
        check(secondLoads == P1 + "," + P2 + "," + P3, "... and the other joiner's: " + secondLoads);
        check(first.Game.WaitFor("never") == "", "The joiner itself is not asked again: its own are not friends'.");

        // A malformed list from a game is dropped, and what the others load stands.
        first.Game.Hold("not-a-package-list");
        check(host.Game.WaitFor("never") == "" && log.Any(l => l.Contains("malformed list of its packages")), "A malformed list is dropped and logged.");
        check(log.Any(l => l.StartsWith("host: Assets: sent the lobby's", StringComparison.Ordinal)) &&
            log.Any(l => l.StartsWith("first: Assets: sent this game's", StringComparison.Ordinal)), "Both sides logged what they sent.");
    }
}

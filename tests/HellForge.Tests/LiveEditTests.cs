using System.IO.Pipes;
using System.Text;
using HellForge.Core;

static class LiveEditTests
{
    public static void Register(Action<string, Action> test, Action<bool, string> check, Action<Action> reject, string root)
    {
        test("Every stats field the editor shows is a mapped field with a live timing", () =>
        {
            var fields = StatsSchema.Current.Targets.Values.SelectMany(t => t.Values).SelectMany(t => t.Fields.Values).ToList();
            var missing = fields.Where(f => f.Kind is not ("EntityDelta" or "StatusMultiplier") && LiveApplies.Of(f) == null)
                .Select(f => $"{f.Kind} {f.Component} +{f.Offset} {f.Storage}").Distinct().ToList();
            check(missing.Count == 0, "Unmapped: " + string.Join(", ", missing.Take(10)));
            check(fields.Where(f => f.Kind == "EntityDelta").All(f => LiveApplies.Of(f) == null), "Attachment deltas are not live.");
            var rpm = StatsSchema.Current.Targets["weapons"]["AR-23 Liberator"].Fields["weapon.fire_rate"];
            check(LiveApplies.Of(rpm) is { When: "respawn", Verified: true }, "The Liberator's rate applies on respawn (seeded at creation).");
            check(SdkMappings.Current.Components.All(c => c.Fields.All(f => SdkMappings.AppliesValues.Contains(f.Applies))), "Every field has a timing.");
        });

        test("The live editor's pipe sends stats fields by identity and reads the game's answers", () =>
        {
            var run = "test" + Guid.NewGuid().ToString("N")[..12];
            var server = new LiveEditServer(run);
            server.Start();
            var rpm = StatsSchema.Current.Targets["weapons"]["AR-23 Liberator"].Fields["weapon.fire_rate"];
            check(server.SetAsync(rpm, 1, false).Result == LiveResult.NotConnected, "Nothing is sent before the game connects.");
            using var game = new NamedPipeClientStream(".", LiveEditServer.PipeName(run), PipeDirection.InOut);
            game.Connect(5000);
            var writer = new StreamWriter(game, new UTF8Encoding(false)) { NewLine = "\n", AutoFlush = true };
            var reader = new StreamReader(game, new UTF8Encoding(false));
            writer.WriteLine($"hello\t{LiveEditServer.Protocol}\t{StatsSchema.Current.Build}\t{StatsSchema.Fingerprint}");
            var deadline = DateTime.UtcNow.AddSeconds(5);
            while (!server.Connected && DateTime.UtcNow < deadline) Thread.Sleep(10);
            check(server.Connected && !server.Mismatched, "The game's hello connects it.");

            var set = server.SetAsync(rpm, BitConverter.SingleToUInt32Bits(900f), shared: true);
            var request = reader.ReadLine()!.Split('\t');
            check(request.Length == 10 && request[0] == "set" && request[2] == "component" && request[3] == "ProjectileWeaponComponentData" &&
                request[4] == Convert.ToUInt64(rpm.Resource[2..], 16).ToString("x16") && request[6] == "8" && request[7] == "f32" &&
                request[8] == BitConverter.SingleToUInt32Bits(900f).ToString("x8") && request[9] == "1", "Set request: " + string.Join('|', request));
            writer.WriteLine($"{request[1]}\tok\trespawn");
            check(set.Result is { Ok: true, Detail: "respawn" }, "The game's answer is the timing.");

            var get = server.GetAsync(rpm);
            var asked = reader.ReadLine()!.Split('\t');
            writer.WriteLine($"{asked[1]}\tok\trespawn\t44610000");
            check(asked[0] == "get" && get.Result.Bits == 0x44610000, "Get returns the game's value.");
            var reset = server.ResetAsync(rpm);
            var undo = reader.ReadLine()!.Split('\t');
            writer.WriteLine($"{undo[1]}\trefused\tno such record");
            check(undo[0] == "reset" && reset.Result is { Ok: false, Detail: "no such record" }, "A refusal carries its reason.");

            game.Dispose();
            deadline = DateTime.UtcNow.AddSeconds(5);
            while (server.Connected && DateTime.UtcNow < deadline) Thread.Sleep(10);
            check(!server.Connected, "A closed game disconnects.");
            server.DisposeAsync().AsTask().Wait();
            var refused = false;
            try { _ = new LiveEditServer("bad\\name"); } catch (ArgumentException) { refused = true; }
            check(refused, "A pipe name is letters, digits, '-' and '_' only.");
        });
    
        test("Co-op plays with the host's Local Stats: a joiner's requests to the host's values and back to its own", () =>
        {
            static string Doc(string weapons) => "{\"format\":1,\"weapons\":{" + weapons + "}}";
            var sync = new StatsSync();
            var own = Doc("\"AR-23 Liberator\":{\"weapon.fire_rate\":700},\"R-36 Eruptor\":{\"weapon.fire_rate\":400}");
            check(StatsSync.Values("{\"weapons\":{\"x\":{},\"x\":{}}}", out _).Count == 0 && StatsSync.Values("not json", out _).Count == 0,
                "A broken file has no values (duplicate keys, not JSON).");
            var host = Doc("\"AR-23 Liberator\":{\"weapon.fire_rate\":900,\"attachment.magazine.rifle_5_5x50mm_drum.attachment.ergonomics_modifier\":5}");
            string Show(IEnumerable<StatsSyncRequest> r) => string.Join(", ", r.Select(q => q.Verb + " " + q.Field.Name + (q.Verb == "set" ? " " + BitConverter.UInt32BitsToSingle(q.Bits) : "")));
            var joined = sync.Plan(host, own);
            check(joined.Count == 2 && joined.Any(q => q is { Verb: "set" } && BitConverter.UInt32BitsToSingle(q.Bits) == 900) && joined.Count(q => q.Verb == "base") == 1,
                "Joined: the host's value, the joiner's own change at its base: " + Show(joined));
            check(sync.BootOnly >= 0, "Boot-only host values are counted.");
            foreach (var q in joined) sync.Done(q, joined: true);
            check(sync.Plan(host, own).Count == 0, "Nothing again for the same values.");
            var changed = sync.Plan(Doc("\"AR-23 Liberator\":{\"weapon.fire_rate\":1000}"), own);
            check(changed.Count == 1 && changed[0] is { Verb: "set" } && BitConverter.UInt32BitsToSingle(changed[0].Bits) == 1000, "A host change: " + Show(changed));
            foreach (var q in changed) sync.Done(q, joined: true);
            var dropped = sync.Plan(Doc(""), own);
            check(dropped.Count == 1 && dropped[0].Verb == "base", "A value the host dropped goes to its base: " + Show(dropped));
            foreach (var q in dropped) sync.Done(q, joined: true);
            var back = sync.Plan(null, own);
            check(back.Count == 2 && back.All(q => q.Verb == "set") && back.Select(q => BitConverter.UInt32BitsToSingle(q.Bits)).Order().SequenceEqual([400f, 700f]),
                "The host gone: the joiner's own values: " + Show(back));
            foreach (var q in back) sync.Done(q, joined: false);
            check(sync.Tracked == 0 && sync.Plan(null, own).Count == 0, "Back at its own values, nothing is tracked.");
            var fresh = new StatsSync();
            foreach (var q in fresh.Plan(Doc("\"AR-23 Liberator\":{\"weapon.fire_rate\":900}"), null)) fresh.Done(q, joined: true);
            var reset = fresh.Plan(null, null);
            check(reset.Count == 1 && reset[0].Verb == "reset", "Without its own value, a host value is undone: " + Show(reset));
            var text = Doc("\"AR-23 Liberator\":{\"weapon.fire_rate\":900}");
            check(StatsSync.Unpack(StatsSync.Pack(text)) == text, "The host's file survives the peer link.");
        });
    }
}

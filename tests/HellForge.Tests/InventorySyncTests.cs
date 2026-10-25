using HellForge.Core;

// Profile > Account sync (InventorySync): where snapshots live, and how a finished capture becomes one. Synthetic files only.
static class InventorySyncTests
{
    const string Inventory = """{"items":[{"mixId":1,"amount":1,"isDiscovered":true},{"mixId":2,"amount":3,"isDiscovered":false}]}""";
    const string Progression = """{"experience":485592,"onboardingState":128,"onboardingFlags":7}""";

    public static void Register(Action<string, Action> test, Action<bool, string> check, Action<Action> reject, string root)
    {
        test("Sync Inventory keeps each profile's snapshot in account-snapshot beside profiles, and an inventory alone makes one", () =>
        {
            var install = Path.Combine(root, "sync-layout");
            var expected = Path.Combine(install, InventorySync.SnapshotFolder, "player");
            check(InventorySync.SnapshotDirectoryFromProfileDir(Path.Combine(install, "profiles", "player")) == expected, "Profile folder.");
            check(InventorySync.SnapshotDirectoryFromProfileDir(Path.Combine(install, "profiles", "player") + Path.DirectorySeparatorChar) == expected, "Trailing separator.");
            check(InventorySync.SnapshotDirectory(install, "player") == expected, "Launcher and backend disagree on the folder.");
            check(!InventorySync.HasSnapshot(expected), "A missing folder is a snapshot.");
            Directory.CreateDirectory(expected);
            File.WriteAllText(Path.Combine(expected, InventorySync.InventoryFile), Inventory);
            check(InventorySync.HasSnapshot(expected), "An inventory without progression is not a snapshot.");
        });

        test("Sync Inventory finds its capture only in an installation's app/capture", () =>
        {
            var install = Path.Combine(root, "sync-proxy");
            check(InventorySync.FindCaptureProxy(null) == null, "Found a capture without an installation.");
            check(InventorySync.FindCaptureProxy(install) == null, "Found a capture that is not there.");
            var proxy = Path.Combine(install, "app", "capture", InventorySync.ProxyExe);
            Directory.CreateDirectory(Path.GetDirectoryName(proxy)!);
            File.WriteAllText(proxy, "synthetic");
            check(InventorySync.FindCaptureProxy(install) == proxy, "The installed capture was not found.");
        });

        test("A capture becomes the snapshot only with an inventory list; a newer one without progression drops the older progression", () =>
        {
            var sync = Path.Combine(root, "sync-run");
            var snapshot = Path.Combine(root, "sync-snapshot");
            Directory.CreateDirectory(sync);
            check(InventorySync.FinalizeCapture(sync, snapshot) != null, "A capture without an inventory was saved.");
            File.WriteAllText(Path.Combine(sync, InventorySync.InventoryFile), """{"error":"denied"}""");
            check(InventorySync.FinalizeCapture(sync, snapshot) != null, "An inventory without items was saved.");
            check(!Directory.Exists(snapshot), "A failed capture created the snapshot.");

            File.WriteAllText(Path.Combine(sync, InventorySync.InventoryFile), Inventory);
            File.WriteAllText(Path.Combine(sync, InventorySync.ProgressionFile), Progression);
            check(InventorySync.FinalizeCapture(sync, snapshot) == null, "A complete capture was refused.");
            check(!File.Exists(Path.Combine(sync, InventorySync.InventoryFile)) && !File.Exists(Path.Combine(sync, InventorySync.ProgressionFile)),
                "The capture's files stayed in the sync folder.");
            check(File.ReadAllText(Path.Combine(snapshot, InventorySync.InventoryFile)) == Inventory, "Inventory not moved.");
            check(File.ReadAllText(Path.Combine(snapshot, InventorySync.ProgressionFile)) == Progression, "Progression not moved.");
            var info = InventorySync.ReadMetadata(snapshot);
            check(info is { Items: 2, Experience: 485592, CapturedUtc: not null }, "Metadata does not describe the capture.");

            File.WriteAllText(Path.Combine(sync, InventorySync.ProgressionFile), "[1]");
            File.WriteAllText(Path.Combine(sync, InventorySync.InventoryFile), Inventory);
            check(InventorySync.FinalizeCapture(sync, snapshot) != null, "A progression that is not an object was saved.");
            check(File.Exists(Path.Combine(snapshot, InventorySync.ProgressionFile)), "A refused capture changed the snapshot.");

            File.Delete(Path.Combine(sync, InventorySync.ProgressionFile));
            check(InventorySync.FinalizeCapture(sync, snapshot) == null, "An inventory-only capture was refused.");
            check(!File.Exists(Path.Combine(snapshot, InventorySync.ProgressionFile)), "Progression from an older capture stayed beside a newer inventory.");
            check(InventorySync.ReadMetadata(snapshot) is { Items: 2, Experience: null }, "Metadata claims progression that was not captured.");
        });
    }
}

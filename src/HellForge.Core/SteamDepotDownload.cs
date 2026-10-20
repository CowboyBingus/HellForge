namespace HellForge.Core;

// The supported build fetched by Steam itself, for when Steam's own HELLDIVERS 2 has moved on to another build (or is not
// installed). The Steam console's "download_depot <app> <depot> <manifest>" downloads one depot at an exact manifest, for
// an account that owns the game, into <Steam>/steamapps/content/app_<app>/depot_<depot>. HellForge shows the commands for the
// manifests it pins (SupportedBuild.Depots), watches those folders and copies the game from them like from an install:
// every file is hashed against the build's list. HellForge downloads nothing itself and ships no game content.
public static class SteamDepotDownload
{
    public sealed record DepotState(SupportedDepot Depot, long Done, long Total)
    {
        public bool Complete => Done == Total;
    }

    public static string ContentFolder(string steamRoot, SupportedBuild build) =>
        Path.Combine(steamRoot, "steamapps", "content", "app_" + build.AppId);

    public static string DepotFolder(string steamRoot, SupportedBuild build, uint depot) =>
        Path.Combine(ContentFolder(steamRoot, build), "depot_" + depot);

    public static IReadOnlyList<string> Commands(SupportedBuild build) =>
        [.. build.Depots.Select(depot => $"download_depot {build.AppId} {depot.Id} {depot.ManifestId}")];

    public static string Locate(string steamRoot, SupportedBuild build, SupportedFile file) =>
        Path.Combine(DepotFolder(steamRoot, build, file.Depot), file.Path);

    // Bytes of each depot's files that are present with their listed size (a quick check while Steam downloads; the copy
    // hashes every file).
    public static IReadOnlyList<DepotState> Status(string steamRoot, SupportedBuild build) =>
        [.. build.Depots.Select(depot =>
        {
            long done = 0, total = 0;
            foreach (var file in build.Files.Where(f => f.Depot == depot.Id))
            {
                total += file.Size;
                var info = new FileInfo(Locate(steamRoot, build, file));
                if (info.Exists && info.Length == file.Size) done += file.Size;
            }
            return new DepotState(depot, done, total);
        })];

    public static bool IsComplete(string steamRoot, SupportedBuild build) => Status(steamRoot, build).All(depot => depot.Complete);

    // Removes the build's depot folders after the game is copied (and the app folder when nothing else is in it).
    public static void Delete(string steamRoot, SupportedBuild build)
    {
        foreach (var depot in build.Depots)
        {
            var folder = DepotFolder(steamRoot, build, depot.Id);
            if (Directory.Exists(folder)) Directory.Delete(folder, recursive: true);
        }
        var app = ContentFolder(steamRoot, build);
        if (Directory.Exists(app) && !Directory.EnumerateFileSystemEntries(app).Any()) Directory.Delete(app);
    }
}

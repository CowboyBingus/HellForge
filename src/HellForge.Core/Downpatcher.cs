using System.Diagnostics;
using System.IO.Compression;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;

namespace HellForge.Core;

// A patch from one Steam build to the supported one. SourceExeSha256 and SourceGameDllSha256 name the source build by its
// files too, so a copy Steam labels with another build id (a stale manifest) still finds its patch.
public sealed record DownpatchPack(string SourceBuild, string TargetBuild, string Url, string Sha256, long Size,
    string SourceExeSha256 = "", string SourceGameDllSha256 = "");

public static class Downpatcher
{
    public static readonly DownpatchPack[] Packs =
    [
        new("25988413", "25480438",
            "https://files.hf-mod.com/downpatch/downpatch-25988413-to-25480438.zip",
            "4CE81BFB2CB9D47848A5C3518F511D4BB14B6BDF18748007F93B46B1E0BD03AC",
            1_680_909_152,
            "2CE8B3D74C5573088E4211B060CED0963F1FC1E74526E2645399780B8FB13FF7",
            "A457433F0EB766FBE15A6DE5A16F0FBBB1FD3C840C5607A5F0B2EBF53B112949")
    ];

    const string HpatchzUrl = "https://github.com/sisong/HDiffPatch/releases/download/v5.1.3/hdiffpatch_v5.1.3_bin_windows64.zip";

    public static DownpatchPack? FindPack(string sourceBuild) =>
        Packs.FirstOrDefault(p => p.SourceBuild == sourceBuild && p.TargetBuild == SupportedBuild.Current.BuildId);

    // The patch for an installed game: by its build id, else by its executable's and game.dll's hashes.
    public static DownpatchPack? FindPack(BuildPin pin) =>
        FindPack(pin.BuildId) ?? Packs.FirstOrDefault(p => p.TargetBuild == SupportedBuild.Current.BuildId && p.SourceExeSha256.Length > 0 &&
            p.SourceExeSha256.Equals(pin.ExeSha256, StringComparison.OrdinalIgnoreCase) &&
            p.SourceGameDllSha256.Equals(pin.GameDllSha256, StringComparison.OrdinalIgnoreCase));

    public static async Task ApplyAsync(
        string steamGame, string gameDestination, string tempFolder,
        DownpatchPack pack, HttpClient http,
        Action<long, long>? downloadProgress = null,
        Action<long, long, string>? applyProgress = null,
        CancellationToken cancellation = default)
    {
        steamGame = LocalInstall.FullPath(steamGame);
        gameDestination = LocalInstall.FullPath(gameDestination);
        LocalInstall.NoLinks(steamGame);
        LocalInstall.NoLinks(gameDestination);
        Directory.CreateDirectory(gameDestination);
        Directory.CreateDirectory(tempFolder);

        var hpatchz = await EnsureHpatchz(tempFolder, http, cancellation);
        var packPath = await DownloadPack(tempFolder, pack, http, downloadProgress, cancellation);
        ApplyPack(steamGame, gameDestination, packPath, hpatchz, http, applyProgress, cancellation);
    }

    static async Task<string> EnsureHpatchz(string tempFolder, HttpClient http, CancellationToken cancellation)
    {
        var toolsDir = Path.Combine(tempFolder, "tools");
        var hpatchz = Path.Combine(toolsDir, "hpatchz.exe");
        if (File.Exists(hpatchz)) return hpatchz;

        Directory.CreateDirectory(toolsDir);
        var zipPath = Path.Combine(toolsDir, "hdiffpatch.zip");

        using (var response = await http.GetAsync(HpatchzUrl, HttpCompletionOption.ResponseHeadersRead, cancellation))
        {
            response.EnsureSuccessStatusCode();
            await using var stream = await response.Content.ReadAsStreamAsync(cancellation);
            await using var file = new FileStream(zipPath, FileMode.Create, FileAccess.Write, FileShare.None);
            await stream.CopyToAsync(file, cancellation);
        }

        var extractDir = Path.Combine(toolsDir, "hdiffpatch");
        if (Directory.Exists(extractDir)) Directory.Delete(extractDir, recursive: true);
        ZipFile.ExtractToDirectory(zipPath, extractDir);
        File.Copy(Path.Combine(extractDir, "windows64", "hpatchz.exe"), hpatchz, overwrite: true);
        return hpatchz;
    }

    static async Task<string> DownloadPack(string tempFolder, DownpatchPack pack, HttpClient http,
        Action<long, long>? progress, CancellationToken cancellation)
    {
        var packPath = Path.Combine(tempFolder, $"downpatch-{pack.SourceBuild}-to-{pack.TargetBuild}.zip");

        if (File.Exists(packPath) && new FileInfo(packPath).Length == pack.Size)
        {
            progress?.Invoke(pack.Size, pack.Size);
            return packPath;
        }

        var partial = packPath + ".partial";
        if (File.Exists(partial)) File.Delete(partial);

        using var response = await http.GetAsync(pack.Url, HttpCompletionOption.ResponseHeadersRead, cancellation);
        response.EnsureSuccessStatusCode();

        using var sha = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        long downloaded = 0;
        await using (var stream = await response.Content.ReadAsStreamAsync(cancellation))
        await using (var file = new FileStream(partial, FileMode.Create, FileAccess.Write, FileShare.None, 81920))
        {
            var buffer = new byte[81920];
            int read;
            while ((read = await stream.ReadAsync(buffer, cancellation)) > 0)
            {
                cancellation.ThrowIfCancellationRequested();
                sha.AppendData(buffer, 0, read);
                await file.WriteAsync(buffer.AsMemory(0, read), cancellation);
                downloaded += read;
                progress?.Invoke(downloaded, pack.Size);
            }
        }

        var hash = Convert.ToHexString(sha.GetHashAndReset());
        if (!hash.Equals(pack.Sha256, StringComparison.OrdinalIgnoreCase))
        {
            File.Delete(partial);
            throw new InvalidDataException($"Patch pack corrupted during download (expected SHA-256 {pack.Sha256[..16]}...).");
        }

        File.Move(partial, packPath, overwrite: true);
        return packPath;
    }

    static readonly Dictionary<string, string> DownloadOverrides = new(StringComparer.OrdinalIgnoreCase)
    {
        ["data/settings.ini"] = "https://files.hf-mod.com/downpatch/settings-25480438.ini"
    };

    static void ApplyPack(string steamGame, string gameDestination, string packPath, string hpatchz,
        HttpClient http, Action<long, long, string>? progress, CancellationToken cancellation)
    {
        using var zip = ZipFile.OpenRead(packPath);
        var manifestEntry = zip.GetEntry("downpatch.json")
            ?? throw new InvalidDataException("Patch pack has no downpatch.json.");

        DownpatchManifest manifest;
        using (var reader = new StreamReader(manifestEntry.Open()))
            manifest = JsonSerializer.Deserialize<DownpatchManifest>(reader.ReadToEnd(), ManifestJson)
                ?? throw new InvalidDataException("Empty downpatch manifest.");

        long done = 0;
        long total = manifest.Files.Sum(f => f.Op == "patch" ? f.TargetSize : f.Size);

        foreach (var file in manifest.Files)
        {
            cancellation.ThrowIfCancellationRequested();
            var hasOverride = DownloadOverrides.TryGetValue(file.Path, out var overrideUrl);
            var destPath = Path.Combine(gameDestination, file.Path.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(destPath)!);
            var expectedSize = file.Op == "patch" ? file.TargetSize : file.Size;

            if (File.Exists(destPath) && new FileInfo(destPath).Length == expectedSize)
            {
                done += expectedSize;
                progress?.Invoke(done, total, file.Path);
                continue;
            }

            var partial = destPath + ".partial";

            if (hasOverride)
            {
                using var response = http.GetAsync(overrideUrl, cancellation).GetAwaiter().GetResult();
                response.EnsureSuccessStatusCode();
                using var stream = response.Content.ReadAsStreamAsync(cancellation).GetAwaiter().GetResult();
                using var output = new FileStream(partial, FileMode.Create, FileAccess.Write, FileShare.None);
                stream.CopyTo(output);
            }
            else switch (file.Op)
            {
                case "copy":
                {
                    var src = Path.Combine(steamGame, file.Path.Replace('/', Path.DirectorySeparatorChar));
                    if (!File.Exists(src))
                        throw new InvalidDataException($"Steam is missing {file.Path}; validate the game in Steam and start setup again.");
                    File.Copy(src, partial, overwrite: true);
                    break;
                }
                case "patch":
                {
                    var src = Path.Combine(steamGame, file.Path.Replace('/', Path.DirectorySeparatorChar));
                    if (!File.Exists(src))
                        throw new InvalidDataException($"Steam is missing {file.Path}; validate the game in Steam and start setup again.");
                    var patchEntry = zip.GetEntry($"patches/{file.PatchFile}")
                        ?? throw new InvalidDataException($"Patch pack is missing patches/{file.PatchFile}.");
                    var patchTemp = Path.Combine(Path.GetTempPath(), $"hf-patch-{Guid.NewGuid():N}.hdiff");
                    try
                    {
                        patchEntry.ExtractToFile(patchTemp, overwrite: true);
                        var psi = new ProcessStartInfo(hpatchz)
                        {
                            ArgumentList = { "-f", src, patchTemp, partial },
                            UseShellExecute = false,
                            CreateNoWindow = true,
                            RedirectStandardOutput = true,
                            RedirectStandardError = true
                        };
                        using var proc = Process.Start(psi) ?? throw new InvalidOperationException("hpatchz did not start.");
                        proc.WaitForExit();
                        if (proc.ExitCode != 0)
                            throw new InvalidDataException($"Patching {file.Path} failed (hpatchz exit code {proc.ExitCode}).");
                    }
                    finally
                    {
                        if (File.Exists(patchTemp)) File.Delete(patchTemp);
                    }
                    break;
                }
                case "add":
                {
                    var addEntry = zip.GetEntry($"add/{file.Path}")
                        ?? throw new InvalidDataException($"Patch pack is missing add/{file.Path}.");
                    addEntry.ExtractToFile(partial, overwrite: true);
                    break;
                }
                default:
                    throw new InvalidDataException($"Unknown operation in patch manifest: {file.Op}");
            }

            var info = new FileInfo(partial);
            if (!info.Exists)
                throw new InvalidDataException($"Patching produced no output for {file.Path}.");
            if (info.Length != expectedSize)
                throw new InvalidDataException(
                    $"Patching produced a wrong-sized {file.Path} (expected {expectedSize}, got {info.Length}).");
            File.Move(partial, destPath, overwrite: true);

            done += expectedSize;
            progress?.Invoke(done, total, file.Path);
        }
    }

    sealed record DownpatchManifest(string SourceBuild, string TargetBuild, DownpatchFile[] Files);

    sealed record DownpatchFile
    {
        public string Path { get; init; } = "";
        public string Op { get; init; } = "";
        public long Size { get; init; }
        public string? Sha1 { get; init; }
        public string? PatchFile { get; init; }
        public long TargetSize { get; init; }
        public string? TargetSha1 { get; init; }
    }

    static readonly JsonSerializerOptions ManifestJson = new() { PropertyNameCaseInsensitive = true };
}

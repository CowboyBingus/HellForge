namespace HellForge.Core.GameData;

// The in-game editor's fonts: the game's own FS Sinclair and FS Sinclair Medium, the TrueType files its UI draws all of
// its text with (HD2Runtime's research, scripts/hd2_font.py: resources of type "ttf" in package 6728ac296c9eab7b, a
// 16-byte resource header, then the sfnt). Read from the installed game on the player's machine and written into the
// editor's folder; like the icons, they are never published.
public static class EditorFonts
{
    public const ulong TtfType = 0xCBAE3394881E3D1B;
    public static readonly IReadOnlyList<(string File, ulong Name)> Fonts =
        [("fs-sinclair.ttf", 0xA4EA96E59644F60D), ("fs-sinclair-medium.ttf", 0x6DECA49EA82C82EC)];

    // The TrueType file inside a ttf resource's main part, or null when it is not one.
    public static byte[]? TrueType(byte[] main) =>
        main.Length > 20 && (main.AsSpan(16, 4).SequenceEqual(new byte[] { 0, 1, 0, 0 }) || main.AsSpan(16, 4).SequenceEqual("true"u8) ||
            main.AsSpan(16, 4).SequenceEqual("OTTO"u8)) ? main[16..] : null;

    // Writes the fonts the game data has into `folder`. Returns the files written.
    public static IReadOnlyList<string> Write(string gameData, string folder)
    {
        using var data = new GameDataReader(gameData);
        var found = data.Find(Fonts.Select(f => new ResourceKey(TtfType, f.Name)));
        var written = new List<string>();
        foreach (var (file, name) in Fonts)
        {
            if (!found.TryGetValue(new(TtfType, name), out var at) || TrueType(data.Read(at).Main) is not { } ttf) continue;
            var path = Path.Combine(folder, file);
            File.WriteAllBytes(path + ".partial", ttf);
            File.Move(path + ".partial", path, true);
            written.Add(file);
        }
        return written;
    }
}

using System.Text;
using System.Text.RegularExpressions;

namespace HellForge.Core;

public sealed record PreparedSession(string BuildId, IReadOnlyList<string> Problems, string ProfileData, bool ProfileSeeded,
    IReadOnlyList<string> GameFiles);

// The build's data/settings.ini names %APPDATA%/Arrowhead/Helldivers2 for everything the game writes: settings, saves,
// shader cache, logs and crash dumps. A session points them into the installation's userdata folder instead, with the
// user settings and saves per profile. The engine resolves %APPDATA% itself (known-folder API), so only rewriting the
// paths keeps the game out of AppData.
public static partial class GameSettings
{
    const string VanillaRoot = "%APPDATA%/Arrowhead/Helldivers2";
    const string SettingsPath = "data/settings.ini";
    public const string OriginalSuffix = ".hellforge-original";
    static readonly UTF8Encoding Utf8 = new(encoderShouldEmitUTF8Identifier: false, throwOnInvalidBytes: true);

    [GeneratedRegex("^[a-z0-9-]{1,32}$")]
    public static partial Regex ProfileName();

    public static string Rewrite(string original, string userData, string profile)
    {
        if (!ProfileName().IsMatch(profile)) throw new ArgumentException("Profile names use 1-32 lowercase letters, digits or hyphens.");
        var root = LocalInstall.FullPath(userData).Replace('\\', '/');
        if (root.Contains('"') || root.Contains('%')) throw new InvalidDataException("The installation path cannot contain quotes or percent signs.");
        if (!original.Contains(VanillaRoot)) throw new InvalidDataException("settings.ini lacks the expected user data paths.");
        var text = original.Replace(VanillaRoot, root);
        foreach (var (key, format) in new[] { ("user_settings", "{0}/user_settings.config"), ("save_dir", "{0}/saves") })
        {
            var shared = $"{key} = \"{string.Format(format, root)}\"";
            if (!text.Contains(shared)) throw new InvalidDataException($"settings.ini lacks its {key} line.");
            text = text.Replace(shared, $"{key} = \"{string.Format(format, root + "/profiles/" + profile)}\"");
        }
        return text;
    }

    // Verifies the installation's game for a session and points its settings at the profile's user data. A new
    // profile starts from the normal game's settings (graphics, controls, answered first-run prompts), read from
    // vanillaUserData; nothing is written there. Returns the problems instead of preparing when the game is not the
    // supported build.
    public static PreparedSession PrepareSession(InstallLayout install, string profile, string? vanillaUserData)
    {
        if (!ProfileName().IsMatch(profile)) throw new ArgumentException("Profile names use 1-32 lowercase letters, digits or hyphens.");
        var build = SupportedBuild.Current;
        var settings = build.Files.Single(file => file.Path == SettingsPath);
        var settingsFile = Path.Combine(install.Game, SettingsPath);
        if (!File.Exists(install.OriginalSettings))
        {
            if (!File.Exists(settingsFile) || !SupportedBuild.Sha1(settingsFile).Equals(settings.Sha1, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("data/settings.ini was changed and its original is missing; repair the game copy.");
            File.Copy(settingsFile, install.OriginalSettings);
        }
        var problems = new List<string>();
        if (!SupportedBuild.Sha1(install.OriginalSettings).Equals(settings.Sha1, StringComparison.OrdinalIgnoreCase))
            problems.Add("changed: " + SettingsPath + " (kept original)");
        Directory.CreateDirectory(install.Temp);
        var others = build with { Files = build.Files.Where(file => file.Path != SettingsPath).ToList() };
        problems.AddRange(others.Verify(install.Game, install.VerifyCache));
        var profileData = install.GameProfile(profile);
        var files = build.Files.Select(file => file.Path).ToList();
        if (problems.Count > 0) return new(build.BuildId, problems, profileData, false, files);

        var seeded = false;
        if (!Directory.Exists(profileData))
        {
            Directory.CreateDirectory(Path.Combine(profileData, "saves"));
            if (vanillaUserData != null && Directory.Exists(vanillaUserData))
            {
                var userSettings = Path.Combine(vanillaUserData, "user_settings.config");
                if (File.Exists(userSettings)) File.Copy(userSettings, Path.Combine(profileData, "user_settings.config"));
                var saves = Path.Combine(vanillaUserData, "saves");
                if (Directory.Exists(saves))
                    foreach (var file in Directory.EnumerateFiles(saves, "*_user_settings.config"))
                        File.Copy(file, Path.Combine(profileData, "saves", Path.GetFileName(file)));
            }
            seeded = true;
        }
        var original = Utf8.GetString(File.ReadAllBytes(install.OriginalSettings));
        File.WriteAllBytes(settingsFile, Utf8.GetBytes(Rewrite(original, install.UserData, profile)));
        return new(build.BuildId, problems, profileData, seeded, files);
    }
}

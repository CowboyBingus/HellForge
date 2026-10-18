using System.Reflection;

namespace HellForge.Core;

// HellForge's version (Directory.Build.props), with the commit it was built from: "0.1.0-beta.1 (b89d1e7)". Shown in
// the corner of the launcher and of HellForge-Setup, so players know what they run and what they install.
public static class AppVersion
{
    public static string Text { get; } = Read();

    static string Read()
    {
        var version = typeof(AppVersion).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "unknown";
        var plus = version.IndexOf('+');
        return plus < 0 ? version : $"{version[..plus]} ({version[(plus + 1)..][..Math.Min(7, version.Length - plus - 1)]})";
    }
}

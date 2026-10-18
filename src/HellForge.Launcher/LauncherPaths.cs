using HellForge.Core;

namespace HellForge.Launcher;

// Every file the launcher reads or starts. An installation (hellforge-install.json in a folder above the launcher) keeps
// everything inside its own folder. In the repository (a build of the launcher, for working on it) the engine is whatever
// package scripts/Publish-HellForge.ps1 last unpacked into artifacts/engine, and the data comes from data/.
sealed class LauncherPaths
{
    LauncherPaths(string root, string? installRoot, string hostRoot, string session, string scripts, string backend, string templates,
        string progression, string profilesRoot, string settingsFile, string sessionFile, string runsFolder)
    {
        Root = root;
        InstallRoot = installRoot;
        HostRoot = hostRoot;
        Session = session;
        ScriptsFolder = scripts;
        Backend = backend;
        TemplatesFile = templates;
        ProgressionFile = progression;
        ProfilesRoot = profilesRoot;
        SettingsFile = settingsFile;
        SessionFile = sessionFile;
        RunsFolder = runsFolder;
    }

    public string Root { get; }
    public string? InstallRoot { get; }
    public string HostRoot { get; }
    // The engine's session runner (HellForge.Session.exe play ...): it owns every step of a session.
    public string Session { get; }
    // The engine's readable scripts: the network block (Windows Firewall rules) and the Steam ownership check.
    public string ScriptsFolder { get; }
    public string Backend { get; }
    public string TemplatesFile { get; }
    // The progression packages the backend serves (weapon mastery tracks for the profile page's weapon levels).
    public string ProgressionFile { get; }
    public string ProfilesRoot { get; }
    public string SettingsFile { get; }
    public string SessionFile { get; }
    public string RunsFolder { get; }
    // Mods load in installations only (<installation>/mods, docs/mods.md).
    public string? ModsFolder => InstallRoot != null ? Path.Combine(InstallRoot, "mods") : null;

    public static LauncherPaths ForRepository(string root)
    {
        var host = Path.Combine(root, "artifacts", "host-startup");
        var engine = Path.Combine(root, "artifacts", "engine");
        return new(root, null, host, Path.Combine(engine, "session", "HellForge.Session.exe"), Path.Combine(engine, "scripts"),
            Path.Combine(engine, "backend", "HellForge.LocalBackend.exe"),
            Path.Combine(root, "data", "profile-templates.json"),
            Path.Combine(root, "data", "progression-data.json"), Path.Combine(host, "profiles"),
            Path.Combine(host, "launcher-settings.json"), Path.Combine(host, "play-session.json"),
            Path.Combine(host, "local-boots"));
    }

    public static LauncherPaths ForInstall(InstallLayout install)
    {
        var app = install.App;
        return new(install.Root, install.Root, install.Root, Path.Combine(app, "session", "HellForge.Session.exe"), Path.Combine(app, "scripts"),
            Path.Combine(app, "backend", "HellForge.LocalBackend.exe"), Path.Combine(app, "profile-templates.json"),
            Path.Combine(app, "progression-data.json"), install.Profiles,
            Path.Combine(install.Root, "launcher-settings.json"),
            Path.Combine(install.Logs, "play-session.json"), install.Sessions);
    }

    // The installation containing the launcher, else the repository it was built in (the folder with the solution).
    public static LauncherPaths? Find()
    {
        if (InstallLayout.Find(AppContext.BaseDirectory) is { } install) return ForInstall(install);
        var root = AppContext.BaseDirectory;
        while (root != null && !File.Exists(Path.Combine(root, "HellForge.slnx")))
            root = Path.GetDirectoryName(root.TrimEnd(Path.DirectorySeparatorChar));
        return root != null ? ForRepository(root) : null;
    }
}

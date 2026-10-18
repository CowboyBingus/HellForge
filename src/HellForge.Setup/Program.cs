namespace HellForge.Setup;

static class Program
{
    // "HellForge-Setup.exe --unpack <folder>" installs or updates without the window and without starting the launcher
    // (scripted updates and tests); the exit code is 0 on success.
    [STAThread]
    static int Main(string[] args)
    {
        if (args is ["--unpack", var folder]) return SetupForm.UnpackQuietly(folder);
        ApplicationConfiguration.Initialize();
        Application.Run(new SetupForm());
        return 0;
    }
}

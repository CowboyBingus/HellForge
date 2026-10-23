// The session runner's game job (src/HellForge.Session/HostBoundedProcess.cs): with singleProcess the game is alone in it
// and cannot start another program; without it, children start as before (the Crash dump diagnostic needs one).
static class SessionJobTests
{
    public static void Register(Action<string, Action> test, Action<bool, string> check, Action<Action> reject, string root)
    {
        test("Sessions: the game's job holds it alone, so it cannot start another program", () =>
        {
            if (!OperatingSystem.IsWindows()) return;
            var cmd = Path.Combine(Environment.SystemDirectory, "cmd.exe");
            int Run(bool singleProcess)
            {
                // cmd starts a second cmd that exits with 7, and exits with what it got
                using var job = new HostBoundedProcess(cmd, "/d /c \"\"" + cmd + "\" /d /c exit 7\"", Environment.SystemDirectory, "", 30, singleProcess);
                job.Resume();
                check(job.Process.WaitForExit(20000), "The test process ended.");
                return job.Process.ExitCode;
            }
            check(Run(singleProcess: false) == 7, "Without the limit a child starts.");
            var limited = Run(singleProcess: true);
            check(limited != 7, $"With the limit the child cannot start (exit code {limited}).");
        });
    }
}

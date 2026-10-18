using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;

// HellForge.Cli crash-dump: a full memory dump of a crashed game, written from outside it. The mod loader's crash note
// (native/mod-loader/behaviours.c WriteCrashDump) starts this with the game's process, the crashed thread and the
// address of its EXCEPTION_POINTERS, and waits: a process dumping itself from its own exception handler can deadlock on
// a lock one of its suspended threads holds.
static class CrashDump
{
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    struct ExceptionInformation
    {
        public uint ThreadId;
        public IntPtr ExceptionPointers;
        public int ClientPointers;
    }

    [DllImport("dbghelp.dll", SetLastError = true)]
    static extern bool MiniDumpWriteDump(IntPtr process, uint processId, SafeHandle file, uint type, ref ExceptionInformation exception, IntPtr user, IntPtr callback);

    // Full memory, handle data, thread info, unloaded modules.
    const uint DumpType = 0x2 | 0x4 | 0x1000 | 0x20;

    public static object Write(int processId, uint threadId, ulong pointers, string output)
    {
        using var process = Process.GetProcessById(processId);
        using var file = File.Create(output);
        var exception = new ExceptionInformation { ThreadId = threadId, ExceptionPointers = (IntPtr)(long)pointers, ClientPointers = 1 };
        if (!MiniDumpWriteDump(process.Handle, (uint)processId, file.SafeFileHandle, DumpType, ref exception, IntPtr.Zero, IntPtr.Zero))
            throw new IOException("MiniDumpWriteDump failed: " + new Win32Exception(Marshal.GetLastWin32Error()).Message);
        file.Flush();
        return new { dump = output, megabytes = file.Length / (1024 * 1024) };
    }
}

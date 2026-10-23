using System.ComponentModel;
using System.Runtime.InteropServices;

namespace HellForge.Session;

/// <summary>
/// Low-integrity process tokens and folder labels for the sandboxed game session (docs/steam-bridge.md, "The rest of a
/// low-integrity session"). A low-integrity process cannot write to the player's files, registry or other processes
/// ("no write up"), even from native code calling the kernel directly. The folders the game needs to write are labelled
/// low before launch.
/// </summary>
static class LowIntegrityHelper
{
    /// <summary>
    /// Creates a primary token whose mandatory integrity level is Low (S-1-16-4096), duplicated from the current
    /// process's token. The caller must close the handle.
    /// </summary>
    public static nint CreateLowToken()
    {
        nint processToken = 0, lowToken = 0, sid = 0;
        try
        {
            if (!OpenProcessToken((nint)(-1), 0x0002 | 0x0008, out processToken))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "OpenProcessToken");
            if (!DuplicateTokenEx(processToken, 0x10000000, nint.Zero, 2, 1, out lowToken))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "DuplicateTokenEx");

            byte[] authority = [0, 0, 0, 0, 0, 16];
            if (!AllocateAndInitializeSid(authority, 1, 0x1000, 0, 0, 0, 0, 0, 0, 0, out sid))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "AllocateAndInitializeSid");

            var label = new TOKEN_MANDATORY_LABEL { Label = new SID_AND_ATTRIBUTES { Sid = sid, Attributes = 0x20 } };
            int size = Marshal.SizeOf(label);
            nint ptr = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(label, ptr, false);
                if (!SetTokenInformation(lowToken, 25, ptr, size))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "SetTokenInformation(TokenIntegrityLevel)");
            }
            finally { Marshal.FreeHGlobal(ptr); }

            nint result = lowToken;
            lowToken = 0;
            return result;
        }
        finally
        {
            if (sid != 0) FreeSid(sid);
            if (processToken != 0) CloseHandle(processToken);
            if (lowToken != 0) CloseHandle(lowToken);
        }
    }

    /// <summary>
    /// Labels a folder with a Low mandatory integrity level, inherited by its children (OI and CI). A normal-integrity
    /// process can label objects at or below its own level without elevation.
    /// </summary>
    public static void LabelFolderLow(string path)
    {
        nint sd = 0;
        try
        {
            if (!ConvertStringSecurityDescriptorToSecurityDescriptor("S:(ML;OICI;NW;;;LW)", 1, out sd, out _))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "ConvertStringSecurityDescriptorToSecurityDescriptor");

            if (!GetSecurityDescriptorSacl(sd, out bool present, out nint sacl, out _) || !present)
                throw new InvalidOperationException("SACL not present in the low-label descriptor.");

            uint err = SetNamedSecurityInfoW(path, 1, 0x10, nint.Zero, nint.Zero, nint.Zero, sacl);
            if (err != 0) throw new Win32Exception((int)err, $"SetNamedSecurityInfo({path})");
        }
        finally
        {
            if (sd != 0) LocalFree(sd);
        }
    }

    /// <summary>
    /// Labels several folders and creates them if they do not exist. Each is labelled Low with OI+CI inheritance.
    /// </summary>
    public static void LabelFoldersLow(IEnumerable<string> paths)
    {
        foreach (string path in paths)
        {
            Directory.CreateDirectory(path);
            LabelFolderLow(path);
        }
    }

    public static void CloseToken(nint token) { if (token != 0) CloseHandle(token); }

    // --- P/Invoke ---

    [StructLayout(LayoutKind.Sequential)]
    struct SID_AND_ATTRIBUTES { public nint Sid; public uint Attributes; }

    [StructLayout(LayoutKind.Sequential)]
    struct TOKEN_MANDATORY_LABEL { public SID_AND_ATTRIBUTES Label; }

    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool OpenProcessToken(nint process, uint access, out nint token);

    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool DuplicateTokenEx(nint existing, uint access, nint attrs, int level, int type, out nint duplicate);

    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool AllocateAndInitializeSid(byte[] authority, byte subAuthorityCount,
        uint a0, uint a1, uint a2, uint a3, uint a4, uint a5, uint a6, uint a7, out nint sid);

    [DllImport("advapi32.dll", SetLastError = true)]
    static extern nint FreeSid(nint sid);

    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool SetTokenInformation(nint token, int cls, nint info, int size);

    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool ConvertStringSecurityDescriptorToSecurityDescriptor(string sddl, uint revision, out nint sd, out uint size);

    [DllImport("advapi32.dll", SetLastError = true)]
    static extern bool GetSecurityDescriptorSacl(nint sd, out bool present, out nint sacl, out bool defaulted);

    [DllImport("advapi32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern uint SetNamedSecurityInfoW(string name, int objectType, uint info, nint owner, nint group, nint dacl, nint sacl);

    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(nint handle);

    [DllImport("kernel32.dll")]
    static extern nint LocalFree(nint mem);
}

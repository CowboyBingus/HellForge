// "HellForge.exe" at the top of an installation: starts the launcher, app\launcher\HellForge.Launcher.exe, from
// the folder this program sits in, then exits. The installation's top level stays this program and its folders, and
// it keeps working wherever the folder is moved.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define PATH_CAPACITY 32768

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int show)
{
    static wchar_t folder[PATH_CAPACITY], launcher[PATH_CAPACITY], command[PATH_CAPACITY + 2];
    DWORD length = GetModuleFileNameW(NULL, folder, PATH_CAPACITY);
    if (length == 0 || length >= PATH_CAPACITY) return 1;
    wchar_t* last = folder;
    for (wchar_t* c = folder; *c; c++)
        if (*c == L'\\') last = c;
    *last = 0;
    if (lstrlenW(folder) + 40 >= PATH_CAPACITY) return 1;
    lstrcatW(folder, L"\\app\\launcher");
    lstrcpyW(launcher, folder);
    lstrcatW(launcher, L"\\HellForge.Launcher.exe");
    lstrcpyW(command, L"\"");
    lstrcatW(command, launcher);
    lstrcatW(command, L"\"");

    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(launcher, command, NULL, NULL, FALSE, 0, NULL, folder, &startup, &process))
    {
        MessageBoxW(NULL, L"The HellForge launcher (app\\launcher\\HellForge.Launcher.exe) is missing from this folder. Reinstall HellForge.",
            L"HellForge", MB_ICONERROR | MB_OK);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

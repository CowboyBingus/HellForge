// What native logs say about the PC (hf_privacy.h): a set user folder and Linux home, and this PC's own computer name.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../hf_privacy.h"

static int g_failed;

static void Expect(const char *input, const char *expected)
{
    char text[512];
    snprintf(text, sizeof text, "%s", input);
    size_t length = HfPrivacyScrub(text, strlen(text), sizeof text - 1);
    text[length] = 0;
    int ok = strcmp(text, expected) == 0;
    printf("%s %s -> %s\n", ok ? "PASS" : "FAIL", input, text);
    if (!ok) g_failed++;
}

int main(void)
{
    SetEnvironmentVariableW(L"USERPROFILE", L"C:\\Users\\Alex\\");
    SetEnvironmentVariableW(L"HOME", L"/home/alex");
    Expect("mod at C:\\Users\\Alex\\Documents\\HellForge\\mods", "mod at %USERPROFILE%\\Documents\\HellForge\\mods");
    Expect("font c:/users/ALEX/Documents/x.ttf", "font %USERPROFILE%/Documents/x.ttf");
    Expect("{\"caller\":\"C:\\\\Users\\\\Alex\\\\game\\\\bin\\\\helldivers2.exe\"}", "{\"caller\":\"%USERPROFILE%\\\\game\\\\bin\\\\helldivers2.exe\"}");
    Expect("C:\\Users\\Alexa\\x C:\\Users\\Alex-old\\y alexmods.vanilla-plus", "C:\\Users\\Alexa\\x C:\\Users\\Alex-old\\y alexmods.vanilla-plus");
    Expect("Z:\\home\\alex\\games", "Z:~\\games");
    Expect("no names here", "no names here");

    wchar_t wide[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    char name[64], input[256], expected[256];
    if (GetComputerNameW(wide, &size) && WideCharToMultiByte(CP_UTF8, 0, wide, -1, name, sizeof name, NULL, NULL) > 3)
    {
        snprintf(input, sizeof input, "console_2026-10-21-b2db8104-%s.txt on %s, not X%s", name, name, name);
        snprintf(expected, sizeof expected, "console_2026-10-21-b2db8104-PC.txt on PC, not X%s", name);
        Expect(input, expected);
    }

    char last[24] = "C:\\Users\\Alex\\a\\b\\c";
    HfPrivacyScrubString(last, sizeof last);
    int ok = strcmp(last, "%USERPROFILE%\\a\\b\\c") == 0;
    printf("%s a message in its buffer -> %s\n", ok ? "PASS" : "FAIL", last);
    if (!ok) g_failed++;
    return g_failed ? 1 : 0;
}

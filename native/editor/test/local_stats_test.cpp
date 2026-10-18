/* The in-game editor's Local Stats saving (local_stats.cpp) against a mods folder of its own: the mod made when missing,
 * a file the launcher wrote kept as it was apart from the changed fields, removals down to deleting the mod, and a broken
 * file or a held lock refusing the save while keeping the changes for the next one. Usage: local_stats_test.exe */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string>
#include "../local_stats.h"

static int g_failures;
static void Check(bool ok, const char* what) { printf("%s %s\n", ok ? "  ok  " : "  FAIL", what); if (!ok) g_failures++; }

static std::string Read(const std::string& path)
{
    std::string text;
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return "<missing>";
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof buffer, file)) > 0) text.append(buffer, n);
    fclose(file);
    return text;
}

static void Write(const std::string& path, const std::string& text)
{
    FILE* file = fopen(path.c_str(), "wb");
    fwrite(text.data(), 1, text.size(), file);
    fclose(file);
}

static bool Exists(const std::string& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

int main()
{
    char temp[MAX_PATH];
    GetTempPathA(sizeof temp, temp);
    std::string mods = std::string(temp) + "hellforge-local-stats-test-" + std::to_string(GetCurrentProcessId());
    CreateDirectoryA(mods.c_str(), NULL);
    std::string folder = mods + "\\zz.local.stats", stats = folder + "\\hellforge-stats.json", manifest = folder + "\\hellforge-mod.json";
    std::string error;

    /* No Local Stats yet: the mod is made. */
    localstats::Set("weapons", "AR-23 Liberator", "weapon.fire_rate", 1200, false);
    Check(localstats::Pending(), "a change is pending");
    Check(localstats::Save(mods, &error) && !localstats::Pending(), "saved into a new mod");
    Check(Read(stats) == "{\n  \"format\": 1,\n  \"weapons\": {\n    \"AR-23 Liberator\": {\n      \"weapon.fire_rate\": 1200\n    }\n  }\n}\n",
        "the new file holds the change");
    Check(Read(manifest).find("\"editor\": \"hellforge-stats-editor-v1\"") != std::string::npos, "the manifest is the launcher's");

    /* A file the launcher wrote: everything else stays as it was. */
    Write(stats, "{\n  \"format\": 1,\n  \"weapons\": {\n    \"AR-23C Liberator Concussive\": {\n      \"damage.ap_direct\": 10,\n"
        "      \"damage.standard_damage\": 2.50e4\n    },\n    \"AR-23 Liberator\": {\n      \"weapon.fire_rate\": 900,\n"
        "      \"weapon.mag_size\": 45\n    }\n  },\n  \"note\": \"caf\\u00e9 \\\"x\\\"\"\n}\n");
    localstats::Set("weapons", "AR-23 Liberator", "weapon.fire_rate", 640.5, false);
    localstats::Set("weapons", "AR-23 Liberator", "weapon.fire_rate", 750.25, false); /* the latest change of a field wins */
    Check(localstats::Save(mods, &error), "saved over the launcher's file");
    Check(Read(stats) == "{\n  \"format\": 1,\n  \"weapons\": {\n    \"AR-23C Liberator Concussive\": {\n      \"damage.ap_direct\": 10,\n"
        "      \"damage.standard_damage\": 2.50e4\n    },\n    \"AR-23 Liberator\": {\n      \"weapon.fire_rate\": 750.25,\n"
        "      \"weapon.mag_size\": 45\n    }\n  },\n  \"note\": \"caf\xc3\xa9 \\\"x\\\"\"\n}\n", "only the changed field differs");

    /* Removals: the field, then its target, then the mod. */
    localstats::Remove("weapons", "AR-23 Liberator", "weapon.fire_rate");
    localstats::Remove("weapons", "AR-23 Liberator", "weapon.mag_size");
    localstats::Remove("note", "x", "y"); /* not in the file */
    Check(localstats::Save(mods, &error) && Read(stats).find("\"AR-23 Liberator\"") == std::string::npos &&
        Read(stats).find("AR-23C Liberator Concussive") != std::string::npos, "a target with nothing left is removed");
    Write(stats, "{\n  \"format\": 1,\n  \"weapons\": {\n    \"AR-23 Liberator\": {\n      \"weapon.fire_rate\": 900\n    }\n  }\n}\n");
    localstats::Remove("weapons", "AR-23 Liberator", "weapon.fire_rate");
    Check(localstats::Save(mods, &error) && !Exists(folder), "the mod goes when nothing is left");

    /* A broken file is left alone, and the change waits. */
    CreateDirectoryA(folder.c_str(), NULL);
    Write(manifest, "{}");
    Write(stats, "{\n  \"format\": 1,\n  \"weapons\": {\n");
    localstats::Set("weapons", "AR-23 Liberator", "weapon.fire_rate", 1200, false);
    Check(!localstats::Save(mods, &error) && localstats::Pending() && Read(stats) == "{\n  \"format\": 1,\n  \"weapons\": {\n",
        "a broken file is refused and the change kept");
    printf("        (%s)\n", error.c_str());

    /* The launcher saving: the lock is held, the save gives up after a while, and goes through once it is free. */
    Write(stats, "{\n  \"format\": 1\n}\n");
    HANDLE lock = CreateFileA((mods + "\\.hellforge-stats-editor.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0, NULL);
    Check(!localstats::Save(mods, &error) && localstats::Pending(), "a held lock is waited for, then the change kept");
    printf("        (%s)\n", error.c_str());
    CloseHandle(lock);
    Check(localstats::Save(mods, &error) && Read(stats).find("\"weapon.fire_rate\": 1200") != std::string::npos, "saved once the lock is free");

    /* A mod's content ("mod:" categories) goes to the editor's own file, never into Local Stats. */
    std::string modFile = mods + "\\mod-stats.json";
    localstats::SetModFile(std::wstring(modFile.begin(), modFile.end()));
    std::string before = Read(stats);
    localstats::Set("mod:you/mod/sentry", "you/mod/sentry", "stratagem.cooldown", 150, false);
    double got = 0;
    Check(localstats::Get("mod:you/mod/sentry", "you/mod/sentry", "stratagem.cooldown", &got) && got == 150, "a mod entry reads back");
    Check(localstats::Save(mods, &error) && Read(stats) == before && Read(modFile).find("\"stratagem.cooldown\": 150") != std::string::npos,
        "a mod entry is saved in the editor's file, Local Stats unchanged");
    localstats::Set("weapons", "AR-23 Liberator", "weapon.fire_rate", 999, false);
    localstats::Set("mod:you/mod/sentry", "you/mod/sentry", "turret.yaw_speed", 30, false);
    Check(localstats::Save(mods, &error) && Read(stats).find("999") != std::string::npos && Read(stats).find("you/mod") == std::string::npos &&
        Read(modFile).find("turret.yaw_speed") != std::string::npos && Read(modFile).find("stratagem.cooldown") != std::string::npos,
        "both saved at once, each to its own file");
    Check(localstats::Load(mods, &error) && localstats::Get("mod:you/mod/sentry", "you/mod/sentry", "turret.yaw_speed", &got) && got == 30,
        "the editor's file is read back with Local Stats");
    localstats::Remove("mod:you/mod/sentry", "you/mod/sentry", "turret.yaw_speed");
    localstats::Remove("mod:you/mod/sentry", "you/mod/sentry", "stratagem.cooldown");
    Check(localstats::Save(mods, &error) && Read(modFile).find("you/mod") == std::string::npos, "mod entries are removed");
    DeleteFileA(modFile.c_str());

    DeleteFileA(stats.c_str()); DeleteFileA(manifest.c_str()); RemoveDirectoryA(folder.c_str());
    DeleteFileA((mods + "\\.hellforge-stats-editor.lock").c_str()); RemoveDirectoryA(mods.c_str());
    printf("%s local stats: %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}

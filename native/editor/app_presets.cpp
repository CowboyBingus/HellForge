/* The editor's window (app_internal.h): HD2R Editor presets, imported and exported, and the PRESETS view. */
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include "app_internal.h"

namespace editorapp
{
/* ---- presets (HD2R Editor's .hd2rpreset.json, in the editor's presets folder) ----
 * A preset is {format "hd2r-editor-preset", version 1, name, editor, runtime, created, fields [{key, value, label}]}:
 * each key is an HD2R Editor row key, which the catalogue gives to the rows on the same bytes (mappings/hd2r-keys.json).
 * Importing stages every field HellForge has a row for (APPLY writes them, DISCARD drops them): projectile swaps
 * first, then the rest, leaving out a weapon's own round while the preset swaps it (as HD2R Editor does). Keys with no
 * row yet (enemies, vehicle zones...) are skipped and counted. Exporting writes the changes saved in Local Stats (and
 * the armor perks the editor keeps) as a preset HD2R Editor reads. */
std::vector<PresetFile> g_presets;
int g_preset;
static struct { bool shown = false; std::string name, text; ImU32 colour = TEXT; } g_presetResult;

static std::wstring PresetFolder() { return hfjson::Wide(g_host.modFolder) + L"\\presets"; }
static std::wstring PresetWriteFolder()
{
    std::wstring sandbox = localstats::SandboxFolder();
    return sandbox.empty() ? PresetFolder() : sandbox + L"\\presets";
}

std::string Narrow(const std::wstring& text)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], n, nullptr, nullptr);
    return out;
}

static void ScanPresetFolder(const std::wstring& folder)
{
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW((folder + L"\\*.json").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return;
    do
    {
        PresetFile p;
        p.path = folder + L"\\" + found.cFileName;
        p.file = Narrow(found.cFileName);
        std::string text, error;
        hfjson::Value root;
        if (found.nFileSizeHigh || found.nFileSizeLow > (16u << 20)) p.error = "larger than 16 MB";
        else if (!hfjson::ReadFile(p.path, &text)) p.error = "cannot be read";
        else
        {
            if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text.erase(0, 3);
            if (!hfjson::Parse(text, &root, &error)) p.error = "not JSON: " + error;
            else if (root.GetString("format") != "hd2r-editor-preset") p.error = "not an HD2R Editor preset";
            else if (root.GetNumber("version", 0) > 1) p.error = "a newer preset format";
            else
            {
                p.name = root.GetString("name", p.file);
                p.editor = root.GetString("editor");
                p.runtime = root.GetString("runtime");
                const hfjson::Value* fields = root.Find("fields");
                p.fields = fields != nullptr && fields->kind == hfjson::Value::Array ? (int)fields->items.size() : 0;
            }
        }
        /* a preset exported this (sandboxed) session replaces the saved one of its name */
        auto same = std::find_if(g_presets.begin(), g_presets.end(), [&](const PresetFile& q) { return q.file == p.file; });
        if (same != g_presets.end()) *same = p; else g_presets.push_back(p);
    } while (FindNextFileW(find, &found));
    FindClose(find);
}

void ScanPresets()
{
    std::wstring folder = PresetFolder(), written = PresetWriteFolder();
    CreateDirectoryW(written.c_str(), nullptr);
    g_presets.clear();
    ScanPresetFolder(folder);
    if (written != folder) ScanPresetFolder(written);
    std::sort(g_presets.begin(), g_presets.end(), [](const PresetFile& a, const PresetFile& b) { return a.file < b.file; });
    g_preset = std::clamp(g_preset, 0, std::max(0, (int)g_presets.size() - 1));
}

/* Reticle styles (WeaponDataComponentData crosshair_type): none (the damage indicator only) and the assault rifle's. */
constexpr double RETICLE_OFF = 3, RETICLE_ON = 4;

/* A flag, a named value or a slot list's entry as the row's number, or false with *why. */
static bool PresetStructured(const Row* row, const Row::Hd2rAs& as, const hfjson::Value& v, double* out, std::string* why)
{
    /* HD2R's reticle flag over the style number: off is 3 (damage indicator only), on the weapon's own style, or the
     * assault rifle's (4) where the game's own is off */
    if (as.kind == "reticle" && v.kind == hfjson::Value::Bool)
    {
        RowView view = ViewOf(row, false);
        double own = view.hasBase ? view.base : view.value;
        return Valid(row, !v.boolean ? RETICLE_OFF : own != RETICLE_OFF && row->OptionOf(own) ? own : RETICLE_ON, out, why);
    }
    auto named = [&](const hfjson::Value& x, double* n) {
        if (x.kind == hfjson::Value::Bool) { *n = x.boolean ? 1 : 0; return true; }
        if (x.kind == hfjson::Value::Number) { *n = x.AsNumber(); return true; }
        if (x.kind == hfjson::Value::String)
        {
            auto found = as.names.find(x.Str());
            if (found != as.names.end()) { *n = found->second; return true; }
            *why = "'" + x.Str() + "' is not a value HellForge knows here"; return false;
        }
        *why = "not a value of this kind"; return false;
    };
    /* a pod slot's pickup ({ref: pickup, id} or "empty") or a name (a mounted weapon's id, a charge weapon whose
     * overcharge explosion it takes): the option it names */
    if (as.kind == "pickup" || as.kind == "named")
    {
        std::string id = v.kind == hfjson::Value::String ? v.Str() :
            as.kind == "pickup" && v.kind == hfjson::Value::Object && v.GetString("ref") == "pickup" ? v.GetString("id") : "";
        auto found = as.names.find(id);
        if (found == as.names.end()) { *why = id.empty() ? "not a value of this kind" : "'" + id + "' is not one HellForge offers here"; return false; }
        *out = found->second;
        return true;
    }
    double n = 0;
    if (as.kind == "slots")
    {
        if (v.kind != hfjson::Value::Array) { *why = "not a list"; return false; }
        if (as.slot < (int)v.items.size() && !named(v.items[as.slot], &n)) return false; /* past the list: an empty slot */
    }
    else if (!named(v, &n)) return false;
    return Valid(row, n, out, why);
}

/* A preset's stored value as the row's number (Local Stats's and the picker's), or false with *why. */
static bool PresetValue(const Row* row, const std::string& key, const hfjson::Value& v, double* out, std::string* why)
{
    if (auto as = row->hd2rAs.find(key); as != row->hd2rAs.end()) return PresetStructured(row, as->second, v, out, why);
    const std::string& kind = row->hd2rKind;
    auto ref = [&](const char* expect) { return v.kind == hfjson::Value::Object && v.GetString("ref") == expect; };
    if (kind == "passive")
    {
        if (v.kind == hfjson::Value::String && (v.Str() == "kit" || v.Str() == "none")) { *out = -1; return true; }
        if (v.kind == hfjson::Value::Number && row->OptionOf(v.AsNumber())) { *out = v.AsNumber(); return true; }
        *why = "not a passive HellForge can give"; return false;
    }
    if (kind == "status")
    {
        if (v.kind != hfjson::Value::String) { *why = "not a status id"; return false; }
        if (v.Str() == "none") { *out = 0; return true; }
        auto s = g_catalog.hd2rStatuses.find(v.Str());
        if (s == g_catalog.hd2rStatuses.end() || !g_catalog.Reference("status", s->second)) { *why = "status " + v.Str() + " is not one a damage type can apply"; return false; }
        *out = s->second; return true;
    }
    if (kind == "explosion")
    {
        if (ref("none")) { *out = 0; return true; }
        auto e = ref("explosion") ? g_catalog.hd2rExplosions.find(v.GetString("id")) : g_catalog.hd2rExplosions.end();
        if (e == g_catalog.hd2rExplosions.end()) { *why = "not a catalogued explosion"; return false; }
        if (std::find(row->exclude.begin(), row->exclude.end(), e->second) != row->exclude.end()) { *why = "an explosion this slot does not offer"; return false; }
        *out = e->second; return true;
    }
    if (kind == "projectile")
    {
        auto o = ref("output") ? g_catalog.hd2rOutputs.find(v.GetString("id")) : g_catalog.hd2rOutputs.end();
        if (o == g_catalog.hd2rOutputs.end()) { *why = "not a projectile donor HellForge knows"; return false; }
        *out = o->second; return true;
    }
    if (kind == "uses" && v.kind == hfjson::Value::String && v.Str() == "unlimited")
    {
        if (!row->unlimited) { *why = "unlimited uses are not offered here"; return false; }
        *out = 4294967295.0; return true;
    }
    if (v.kind != hfjson::Value::Number) { *why = "not a number"; return false; }
    return Valid(row, v.AsNumber(), out, why);
}

void ImportPreset(const PresetFile& p)
{
    if (RefuseLocked()) return;
    std::string text, error;
    hfjson::Value root;
    if (!p.error.empty() || !hfjson::ReadFile(p.path, &text)) { Toast("The preset cannot be read", ERROR_); return; }
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF) text.erase(0, 3);
    if (!hfjson::Parse(text, &root, &error)) { Toast("The preset is not JSON: " + error, ERROR_); return; }
    const hfjson::Value* fields = root.Find("fields");
    if (fields == nullptr || fields->kind != hfjson::Value::Array) { Toast("The preset has no fields", ERROR_); return; }
    std::map<std::string, const hfjson::Value*> byKey; /* a key twice: the last value, as HD2R Editor does */
    for (const hfjson::Value& f : fields->items)
        if (f.kind == hfjson::Value::Object && !f.GetString("key").empty()) if (const hfjson::Value* v = f.Find("value")) byKey[f.GetString("key")] = v;
    int staged = 0, locked = 0, refused = 0;
    std::map<std::string, int> skipped;
    std::string firstWhy;
    std::set<const Row*> done;
    for (int pass = 0; pass < 2; pass++) /* the projectile swaps first: they decide which round's rows apply */
        for (auto& e : byKey)
        {
            auto range = g_catalog.byHd2r.equal_range(e.first);
            if (range.first == range.second)
            {
                if (pass == 0) skipped[e.first.substr(0, e.first.find('|'))]++;
                continue;
            }
            bool counted = false; /* a slot list stages several rows: one value of the preset */
            bool plain = false;   /* a number's key: its first row only (the page it belongs to, not a shared one) */
            for (auto it = range.first; it != range.second; ++it)
            {
                const Row* row = it->second;
                if (!row->hd2rAs.count(e.first)) { if (plain) continue; plain = true; }
                if ((row->hd2rKind == "projectile" && row->fires.empty()) != (pass == 0) || done.count(row)) continue;
                double value;
                std::string why;
                if (!PresetValue(row, e.first, *e.second, &value, &why))
                {
                    if (!counted) refused++;
                    counted = true;
                    if (firstWhy.empty()) firstWhy = row->object->name + SEP + row->label + ": " + why;
                    continue;
                }
                if (pass == 1 && !LockOf(row).empty()) { if (!counted) locked++; counted = true; continue; }
                done.insert(row);
                Stage(row, value);
                if (!counted) staged++;
                counted = true;
            }
        }
    int skip = 0;
    std::string families;
    static const std::map<std::string, std::string> names = {{"en", "enemies"}, {"ve", "vehicles"}, {"mi", "missions"}, {"mo", "missions"},
        {"mt", "missions"}, {"hd", "Helldiver"}, {"pw", "weapons"}, {"sp", "support weapons"}, {"st", "stratagems"}, {"th", "throwables"},
        {"sb", "backpacks"}, {"bo", "boosters"}, {"ex", "explosions"}, {"me", "melee"}, {"rs", "resupply"}, {"ap", "armor perks"},
        {"se", "status effects"}, {"ar", "armor"}, {"ed", "spawning"}, {"dm", "destruction"}, {"eo", "spawning"}};
    for (auto& s : skipped)
    {
        auto name = names.find(s.first);
        skip += s.second;
        families += (families.empty() ? "" : ", ") + (name != names.end() ? name->second : s.first) + " " + std::to_string(s.second);
    }
    g_presetResult.shown = true;
    g_presetResult.name = root.GetString("name", p.file);
    g_presetResult.text = std::to_string(staged) + " staged (APPLY writes them)" +
        (skip ? ", " + std::to_string(skip) + " with no HellForge row yet (" + families + ")" : "") +
        (locked ? ", " + std::to_string(locked) + " for a weapon's own round while the preset swaps it" : "") +
        (refused ? ", " + std::to_string(refused) + " refused (" + firstWhy + (refused > 1 ? ", ..." : "") + ")" : "");
    g_presetResult.colour = staged ? OK : PENDING;
    g_host.api->log(g_host.mod, "editor: preset %s: %s", g_presetResult.name.c_str(), g_presetResult.text.c_str());
    Sound(staged ? sounds::APPLY : sounds::ERROR_);
    Toast(std::to_string(staged) + " preset value" + (staged == 1 ? "" : "s") + " staged from " + g_presetResult.name, staged ? OK : PENDING);
}

/* A structured key's value as a preset stores it, from its rows' values (a slot list: every slot's, in order). */
static hfjson::Value PresetEncodeStructured(const std::string& key, const Row::Hd2rAs& as, const std::map<const Row*, double>& values)
{
    auto name = [&](double n) {
        for (auto& [k, v] : as.names) if (v == n) return hfjson::Value::MakeString(k);
        return hfjson::Value::MakeNumber(n, n == floor(n));
    };
    hfjson::Value list;
    if (values.empty()) return list;
    if (as.kind == "bool") return hfjson::Value::MakeBool(values.begin()->second != 0);
    if (as.kind == "reticle") return hfjson::Value::MakeBool(values.begin()->second != RETICLE_OFF);
    if (as.kind == "enum") return hfjson::Value::MakeNumber(values.begin()->second, true);
    if (as.kind == "pickup" || as.kind == "named")
    {
        for (auto& [k, v] : as.names)
            if (v == values.begin()->second)
            {
                if (as.kind == "named" || k == "empty") return hfjson::Value::MakeString(k);
                hfjson::Value pickup;
                pickup.kind = hfjson::Value::Object;
                pickup.members.emplace_back("ref", hfjson::Value::MakeString("pickup"));
                pickup.members.emplace_back("id", hfjson::Value::MakeString(k));
                return pickup;
            }
        return list;
    }
    if (as.kind != "slots") return list; /* "ap": the traits carry its bytes */
    list.kind = hfjson::Value::Array;
    std::vector<std::pair<int, double>> slots;
    for (auto& [row, v] : values) slots.push_back({row->hd2rAs.at(key).slot, v});
    std::sort(slots.begin(), slots.end());
    bool rates = as.names.empty(); /* fire rates: all three, 0 an empty slot */
    if (!rates) while (!slots.empty() && slots.back().second == 0) slots.pop_back();
    for (auto& [slot, v] : slots) if (rates || v != 0) list.items.push_back(rates ? hfjson::Value::MakeNumber(v, false) : name(v));
    return list;
}

/* A row's value as a preset stores it, or a null value when it has none HD2R Editor would read. */
static hfjson::Value PresetEncode(const Row* row, double v)
{
    hfjson::Value out;
    auto object = [&](const char* ref, const std::string* id) {
        out.kind = hfjson::Value::Object;
        out.members.emplace_back("ref", hfjson::Value::MakeString(ref));
        if (id) out.members.emplace_back("id", hfjson::Value::MakeString(*id));
    };
    const std::string& kind = row->hd2rKind;
    if (kind == "passive") { if (v < 0) return hfjson::Value::MakeString("kit"); return hfjson::Value::MakeNumber(v, true); }
    if (kind == "status")
    {
        if (v == 0) return hfjson::Value::MakeString("none");
        auto s = g_catalog.statusOf.find(v);
        return s != g_catalog.statusOf.end() ? hfjson::Value::MakeString(s->second) : out;
    }
    if (kind == "explosion")
    {
        if (v == 0) { object("none", nullptr); return out; }
        auto e = g_catalog.explosionOf.find(v);
        if (e != g_catalog.explosionOf.end()) object("explosion", &e->second);
        return out;
    }
    if (kind == "projectile")
    {
        auto o = g_catalog.outputOf.find(v);
        if (o != g_catalog.outputOf.end()) object("output", &o->second);
        return out;
    }
    if (row->unlimited && v == 4294967295.0) return hfjson::Value::MakeString("unlimited");
    return hfjson::Value::MakeNumber(v, row->Whole());
}

static void ExportPreset()
{
    std::map<std::string, std::pair<const Row*, double>> fields;
    std::map<std::string, hfjson::Value> structured; /* flags, named values, slot lists: from every slot's row */
    for (const Row* row : SavedRows())
    {
        double v;
        if (row->hd2r.empty() || !Saved(row, &v)) continue;
        auto as = row->hd2rAs.find(row->hd2r[0]);
        if (as == row->hd2rAs.end()) { fields.emplace(row->hd2r[0], std::make_pair(row, v)); continue; }
        if (structured.count(row->hd2r[0])) continue;
        std::map<const Row*, double> values;
        auto range = g_catalog.byHd2r.equal_range(row->hd2r[0]);
        for (auto it = range.first; it != range.second; ++it)
        {
            double s;
            if (Saved(it->second, &s)) values[it->second] = s;
            else if (RowView view = ViewOf(it->second, false); view.has) values[it->second] = view.value;
        }
        hfjson::Value value = PresetEncodeStructured(row->hd2r[0], as->second, values);
        if (!value.IsNull()) structured[row->hd2r[0]] = value;
        fields.emplace(row->hd2r[0], std::make_pair(row, v));
    }
    if (fields.empty()) { Toast("Nothing to export: no change is saved yet", PENDING); return; }
    SYSTEMTIME now;
    GetSystemTime(&now);
    char stamp[32], created[32];
    snprintf(stamp, sizeof stamp, "%04d-%02d-%02d %02d%02d%02d", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    snprintf(created, sizeof created, "%04d-%02d-%02dT%02d:%02d:%02dZ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    std::string name = std::string("HellForge ") + stamp;
    hfjson::Value root, list;
    root.kind = hfjson::Value::Object;
    list.kind = hfjson::Value::Array;
    int written = 0;
    for (auto& f : fields)
    {
        auto s = structured.find(f.first);
        hfjson::Value value = s != structured.end() ? s->second : PresetEncode(f.second.first, f.second.second);
        if (value.IsNull()) continue;
        hfjson::Value field;
        field.kind = hfjson::Value::Object;
        field.members.emplace_back("key", hfjson::Value::MakeString(f.first));
        field.members.emplace_back("value", value);
        field.members.emplace_back("label", hfjson::Value::MakeString(f.second.first->object->name + SEP + f.second.first->label));
        list.items.push_back(field);
        written++;
    }
    root.members.emplace_back("format", hfjson::Value::MakeString("hd2r-editor-preset"));
    root.members.emplace_back("version", hfjson::Value::MakeNumber(1, true));
    root.members.emplace_back("name", hfjson::Value::MakeString(name.substr(0, 40)));
    root.members.emplace_back("editor", hfjson::Value::MakeString("HellForge"));
    root.members.emplace_back("runtime", hfjson::Value::MakeString(g_catalog.hd2rRuntime));
    root.members.emplace_back("created", hfjson::Value::MakeString(created));
    root.members.emplace_back("fields", list);
    CreateDirectoryW(PresetWriteFolder().c_str(), nullptr);
    std::wstring path = PresetWriteFolder() + L"\\" + hfjson::Wide(name) + L".hd2rpreset.json";
    if (!hfjson::WriteAtomically(path, hfjson::Write(root))) { Toast("The preset could not be written", ERROR_); return; }
    ScanPresets();
    for (int i = 0; i < (int)g_presets.size(); i++) if (g_presets[i].path == path) g_preset = i;
    Sound(sounds::APPLY);
    Toast(std::to_string(written) + " saved change" + (written == 1 ? "" : "s") + " exported as " + name, OK);
}

static void OpenPresetFolder()
{
    std::wstring folder = PresetFolder();
    CreateDirectoryW(folder.c_str(), nullptr);
    std::wstring command = L"explorer.exe \"" + folder + L"\"";
    STARTUPINFOW si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (CreateProcessW(nullptr, &command[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    /* a session's game may not start other programs (its job holds it alone): the folder is said instead */
    else Toast("The presets are in " + Narrow(folder), PENDING);
}

void DrawPresets(float y0, float h)
{
    float W = PANEL_W;
    g_cv.Rect(0, y0, W, h, PANEL, 1);
    g_cv.Text("PRESETS", 20, y0 + 24, T(20, TEXT, ui::TITLE));
    g_cv.Text("HD2R Editor presets (.hd2rpreset.json) in the editor's presets folder. IMPORT stages their values for APPLY.",
        20, y0 + 47, T(SZ_SMALL, FAINT, ui::BODY, ui::LEFT, W - 520));
    float bx = W - 20;
    bx -= 150; if (Button("EXPORT", {bx, y0 + 16, 150, 34}, NORMAL, ui::Id("presetexport"))) ExportPreset();
    bx -= 160; if (Button("OPEN FOLDER", {bx, y0 + 16, 150, 34}, NORMAL, ui::Id("presetfolder"))) OpenPresetFolder();
    bx -= 130; if (Button("REFRESH", {bx, y0 + 16, 120, 34}, NORMAL, ui::Id("presetscan"))) ScanPresets();
    float hy = y0 + 66;
    if (g_presetResult.shown)
    {
        g_cv.Text(g_presetResult.name + ":  " + g_presetResult.text, 20, hy + 12, T(SZ_SMALL, g_presetResult.colour, ui::BODY, ui::LEFT, W - 40));
        hy += 26;
    }
    g_cv.Rect(0, hy, W, 22, HEADER, 2);
    g_cv.Text("PRESET", 20, hy + 11, T(SZ_TINY, FAINT, ui::TITLE));
    g_cv.Text("FILE", 420, hy + 11, T(SZ_TINY, FAINT, ui::TITLE));
    g_cv.Text("FIELDS", W - 330, hy + 11, T(SZ_TINY, FAINT, ui::TITLE, ui::RIGHT));
    g_cv.Text("MADE WITH", W - 140, hy + 11, T(SZ_TINY, FAINT, ui::TITLE, ui::RIGHT));
    if (g_presets.empty())
        g_cv.Text("No presets yet. OPEN FOLDER and put .hd2rpreset.json files there, or EXPORT your saved changes.", 20, hy + 44, T(SZ_SMALL, FAINT));
    ListOpts o;
    o.x = 0; o.y = hy + 24; o.w = W; o.h = y0 + h - hy - 28; o.rowH = 36; o.count = (int)g_presets.size(); o.selected = g_preset; o.key = "presets"; o.focused = true;
    o.click = [&](int i) { if (g_preset == i && g_presets[i].error.empty()) ImportPreset(g_presets[i]); else g_preset = i; };
    o.draw = [&](int i, float x, float y, float lw, float rh, bool hovered) {
        const PresetFile& p = g_presets[i];
        bool selected = i == g_preset;
        if (selected) { g_cv.Rect(x + 8, y + 2, lw - 16, rh - 4, SELECT, 2); g_cv.Rect(x + 8, y + 2, 3, rh - 4, GOLD, 3); }
        else if (hovered) g_cv.Rect(x + 8, y + 2, lw - 16, rh - 4, HOVER, 2);
        float cy = y + rh / 2;
        g_cv.Text(p.error.empty() ? p.name : p.file, x + 20, cy, T(SZ_LABEL, p.error.empty() ? (selected ? TEXT : DIM) : FAINT, ui::BODY, ui::LEFT, 380));
        g_cv.Text(p.error.empty() ? p.file : p.error, x + 420, cy, T(SZ_SMALL, p.error.empty() ? FAINT : ERROR_, ui::BODY, ui::LEFT, lw - 420 - 480));
        if (p.error.empty())
        {
            g_cv.Text(std::to_string(p.fields), x + lw - 330, cy, T(SZ_SMALL, DIM, ui::BODY, ui::RIGHT, 80));
            g_cv.Text(p.editor.empty() ? "?" : p.editor + (p.runtime.empty() ? "" : "  (" + p.runtime + ")"), x + lw - 140, cy, T(SZ_TINY, FAINT, ui::BODY, ui::RIGHT, 170));
        }
    };
    o.after = [&](int i, float x, float y, float lw, float rh) {
        if (g_presets[i].error.empty() && Button("IMPORT", {x + lw - 116, y + 4, 100, rh - 8}, PRIMARY, ui::Id("presetimport", (uint64_t)i)))
        { g_preset = i; ImportPreset(g_presets[i]); }
    };
    List(o);
}
} // namespace editorapp

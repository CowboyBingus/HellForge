/* The editor's window (app_internal.h): staging, applying and editing values, the mods each row's value is held by,
 * and the marks of objects, categories and headers. */
#include <math.h>
#include <stdlib.h>
#include <algorithm>
#include "app_internal.h"

namespace editorapp
{
/* ---- staging and applying ---- */

/* The trait slot an AP pick goes to: the one holding an armor penetration trait, else (a new one) the first empty one. */
static const Row* ApSlot(const Row* row, double value)
{
    auto traits = TraitRows(row);
    for (const Row* t : traits) if (double v = ViewOf(t).value; v != 0 && row->OptionOf(v)) return t;
    if (value == 0) return nullptr;
    for (const Row* t : traits) if (ViewOf(t).value == 0) return t;
    return nullptr;
}

void Stage(const Row* row, double value)
{
    if (RefuseLocked()) return;
    if (IsApLabel(row))
    {
        if (const Row* slot = ApSlot(row, value)) Stage(slot, value);
        else if (value != 0) Toast("Every trait slot holds a trait: empty one first, then pick the armor penetration", ERROR_);
        return;
    }
    for (const Row* s : g_catalog.Siblings(row)) if (s != row) g_pending.erase(s);
    RowView applied = ViewOf(row, false);
    if (applied.has && Same(row, value, applied.value)) g_pending.erase(row);
    else g_pending[row] = {value, false};
    g_markVersion++;
    /* a donor projectile: the weapon's own round is not what it fires any more, so its pending values go */
    if (row->pick == "projectile" && applied.hasBase && !Same(row, value, applied.base))
    {
        int dropped = 0;
        for (const Row* r : row->object->rows)
            if (r->fires == row->key && r->target == row->target && g_pending.erase(r)) dropped++;
        if (dropped) Toast(std::to_string(dropped) + " pending change" + (dropped == 1 ? "" : "s") + " to its own round dropped: it fires " +
            RefLabel(row, value) + " now", PENDING);
    }
}

/* Stages a field's reset: it shows its default now, and goes back to it on APPLY (DISCARD takes it back). Only a field
 * Local Stats changed has anything to reset. */
bool StageReset(const Row* row)
{
    if (RefuseLocked()) return false;
    if (IsApLabel(row)) { const Row* slot = ApSlot(row, 0); return slot != nullptr && StageReset(slot); }
    double saved;
    for (const Row* s : g_catalog.Siblings(row)) g_pending.erase(s);
    g_markVersion++;
    if (!Saved(row, &saved)) return false;
    g_pending[row] = {0, true};
    return true;
}

/* A mount slot's or hellpod row's choice to the game (its option's entity, or a count), or back to its vanilla one. */
static bool HasCopyRow() { return g_host.api->size >= offsetof(HfApi, copy_row) + sizeof(void*) && g_host.api->copy_row != nullptr; }
void SendMount(const Row* row)
{
    if (row->choice == "rowcopy") /* another explosion's settings over the row (SDK 9 copy_row), or its own back */
    {
        auto chosen = Mounts().find(MountKey(row));
        const Row::Option* o = chosen != Mounts().end() ? row->OptionOf(chosen->second) : nullptr;
        uint64_t from = o == nullptr || o == &row->options[0] ? 0 : (uint64_t)o->value;
        if (!HasCopyRow() || g_host.api->copy_row(g_host.mod, row->component.c_str(), row->ref, from, row->offset, row->size) == 0)
            SetError(row, HasCopyRow() ? Refusal("HellForge refused the copy") : "this HellForge has no copy_row");
        else ClearError(row);
        return;
    }
    bool wide = row->type == HF_TYPE_U64;
    HfField field{row->component.c_str(), g_layout, (uint16_t)row->offset, (uint16_t)(wide ? 8 : 4), row->type, 0};
    auto chosen = Mounts().find(MountKey(row));
    const Row::Option* o = chosen != Mounts().end() ? row->OptionOf(chosen->second) : nullptr;
    int applies;
    if (o == nullptr || o == &row->options[0]) applies = g_host.api->set_live(g_host.mod, row->ref, &field, nullptr, row->shared ? HF_LIVE_SHARED : 0);
    else
    {
        HfValue v = wide ? HfValue{HF_TYPE_U64, 0, o->resource} : HfValue{row->type, o->value, 0};
        applies = g_host.api->set_live(g_host.mod, row->ref, &field, &v, row->shared ? HF_LIVE_SHARED : 0);
    }
    if (applies == 0) SetError(row, Refusal("HellForge refused the swap"));
    else ClearError(row);
}

/* Back to the base: out of Local Stats (the row and every row on its bytes), and the base back in the game. */
static void ResetRow(const Row* row)
{
    if (IsMount(row))
    {
        g_mounts.erase(MountKey(row));
        SendMount(row);
        SaveSettings();
        g_pending.erase(row);
        g_markVersion++;
        return;
    }
    if (!row->choice.empty())
    {
        g_choice[ChoiceIndex(row)] = -1;
        SendPassives();
        SaveSettings();
        g_pending.erase(row);
        g_markVersion++;
        return;
    }
    for (const Row* s : g_catalog.Siblings(row))
    {
        double v;
        if (localstats::Get(s->category, s->target, s->key, &v)) localstats::Remove(s->category, s->target, s->key);
        if (s->Live())
        {
            HfField field = FieldOf(s);
            g_host.api->set_live(g_host.mod, s->ref, &field, nullptr, HF_LIVE_BASE);
        }
        else if (LiveStat(s))
        {
            HfStat stat = StatOf(s);
            g_host.api->set_stat(g_host.mod, &stat, nullptr, HF_LIVE_BASE);
        }
        ClearError(s);
        g_pending.erase(s);
        g_live[s].at = 0;
    }
    g_markVersion++;
}

static void ApplyRow(const Row* row, double value)
{
    if (IsMount(row))
    {
        if (!row->options.empty() && value == row->options[0].value) g_mounts.erase(MountKey(row));
        else g_mounts[MountKey(row)] = value;
        SendMount(row);
        SaveSettings();
        return;
    }
    if (!row->choice.empty())
    {
        g_choice[ChoiceIndex(row)] = value;
        SendPassives();
        SaveSettings();
        return;
    }
    LiveState& live = Live(row, true);
    if ((row->Live() || row->Stat()) && live.read && Same(row, value, live.base)) { ResetRow(row); return; }
    for (const Row* s : g_catalog.Siblings(row))
    {
        double v;
        if (s != row && localstats::Get(s->category, s->target, s->key, &v)) localstats::Remove(s->category, s->target, s->key);
    }
    localstats::Set(row->category, row->target, row->key, row->unlimited && value == 4294967295.0 ? -1 : value, row->Whole());
    if (row->Live())
    {
        HfField field = FieldOf(row);
        HfValue v{row->type, value, 0};
        int applies = g_host.api->set_live(g_host.mod, row->ref, &field, &v, row->shared ? HF_LIVE_SHARED : 0);
        if (applies == 0) SetError(row, Refusal("HellForge refused it"));
        else ClearError(row);
    }
    else if (LiveStat(row))
    {
        HfStat stat = StatOf(row);
        HfValue v{row->type, value, 0};
        if (g_host.api->set_stat(g_host.mod, &stat, &v, 0) == 0) SetError(row, Refusal("HellForge refused it (saved for the next boot)"));
        else ClearError(row);
    }
    for (const Row* s : g_catalog.Siblings(row)) g_live[s].at = 0;
}


void Apply()
{
    if (RefuseLocked()) return;
    if (g_pending.empty()) return;
    int n = (int)g_pending.size();
    std::vector<std::pair<const Row*, Pend>> pending(g_pending.begin(), g_pending.end());
    g_pending.clear();
    /* status slots are filled from slot 1 and emptied from the last, so no write leaves a gap */
    auto rank = [](const std::pair<const Row*, Pend>& p) {
        const Row* r = p.first;
        if (r->pick != "status") return 0;
        double to = p.second.reset ? ViewOf(r, false).base : p.second.value;
        return to != 0 ? 1 + r->slot : 10 - r->slot;
    };
    std::stable_sort(pending.begin(), pending.end(), [&](auto& a, auto& b) { return rank(a) < rank(b); });
    for (auto& p : pending) { if (p.second.reset) ResetRow(p.first); else ApplyRow(p.first, p.second.value); }
    g_markVersion++;
    g_host.saveSoon(800);
    Sound(sounds::APPLY);
    Toast(g_errors.empty() ? (std::to_string(n) + (n == 1 ? " change applied" : " changes applied") + " and saved to Local Stats") :
        std::to_string(g_errors.size()) + " refused: see the ERRORS badge", g_errors.empty() ? OK : ERROR_);
}

void Discard() { g_pending.clear(); g_markVersion++; }

std::vector<const Row*> g_choiceRows; /* the rows the editor keeps itself (armor perks, arm swaps): found at Init */

const std::vector<const Row*>& SavedRows()
{
    static unsigned stats, version;
    static std::vector<const Row*> out;
    unsigned now = localstats::Version();
    if (stats == now && version == g_markVersion && !out.empty()) return out;
    stats = now; version = g_markVersion;
    out.clear();
    for (auto& e : localstats::Entries()) /* joined: the host's */
        if (const Row* row = g_catalog.Entry(e.category, e.target, e.key)) out.push_back(row);
    for (const Row* row : g_choiceRows) if (ChoiceSaved(row)) out.push_back(row);
    return out;
}

/* RESET TO DEFAULTS, REVERT ALL: every field Local Stats changed staged back to its default, other pending changes
 * dropped; APPLY writes them, DISCARD takes them back. */
void StageResetAll()
{
    if (RefuseLocked()) return;
    g_pending.clear();
    int n = 0;
    for (const Row* row : SavedRows()) if (!PendingOf(row) && StageReset(row)) n++;
    g_markVersion++;
    Toast(n ? std::to_string(n) + (n == 1 ? " reset" : " resets") + " pending: APPLY writes them, DISCARD takes them back" : "Nothing to reset", TEXT);
}

/* ---- holders: the mod whose value each catalogue row holds (get_live / get_stat name it), read a few rows a frame so
 * the objects' and categories' marks and the MODS view know them without stalling the game: every frame from the
 * session's start, open or not (Tick: 0.5 ms of reads; 1.5 ms more while the editor draws), a pass over the catalogue
 * every 10 s. What a pass finds shows in the marks within half a second. ---- */
std::map<const Row*, std::string> g_holders;
static size_t g_holderNext;
static ULONGLONG g_holderPass, g_holderMarked;
static bool g_holderChanged;
bool g_holdersRead;


void HolderTick(LONGLONG microseconds)
{
    if (!g_catalogOk) return;
    ULONGLONG tick = GetTickCount64();
    if (g_holderChanged && (g_holderNext >= g_catalog.rows.size() || tick - g_holderMarked >= 500))
    { g_markVersion++; g_holderChanged = false; g_holderMarked = tick; }
    if (g_holderNext >= g_catalog.rows.size())
    {
        g_holdersRead = true;
        if (g_holderPass != 0 && tick - g_holderPass < 10000) return;
        g_holderNext = 0;
        g_holderPass = tick;
    }
    LARGE_INTEGER frequency, start, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const LONGLONG budget = frequency.QuadPart * microseconds / 1000000;
    while (g_holderNext < g_catalog.rows.size())
    {
        const Row* row = &g_catalog.rows[g_holderNext++];
        if (!row->choice.empty() || IsApLabel(row) || (!row->Live() && !row->Stat())) continue;
        const LiveState& live = Live(row);
        std::string holder = live.read ? live.mod : "";
        auto found = g_holders.find(row);
        if (holder.empty() && found != g_holders.end()) { g_holders.erase(found); g_holderChanged = true; }
        else if (!holder.empty() && (found == g_holders.end() || found->second != holder)) { g_holders[row] = holder; g_holderChanged = true; }
        QueryPerformanceCounter(&now);
        if (now.QuadPart - start.QuadPart > budget) break;
    }
}

/* ---- markers (what an object, a category or a header holds) ---- */
static MarkCache g_marks;

const MarkCache& Markers()
{
    unsigned stats = localstats::Version();
    if (g_marks.version == g_markVersion && g_marks.stats == stats && g_marks.errors == g_errorsVersion) return g_marks;
    g_marks.objects.clear();
    g_marks.navs.clear();
    auto mark = [](const Row* row, int kind) {
        for (const Row* s : g_catalog.Siblings(row))
        {
            Marks& o = g_marks.objects[s->object];
            Marks& n = g_marks.navs[s->object->category];
            if (kind == 0) o.error = n.error = true;
            if (kind == 1) o.pending = n.pending = true;
            if (kind == 2) o.editor = n.editor = true;
        }
    };
    /* content a mod added (a custom stratagem): the mod's mark on it and its category, as on its fields */
    for (auto& o : g_catalog.objects)
        if (o->id.rfind("mod/", 0) == 0) { g_marks.objects[o.get()].mod = true; g_marks.navs[o->category].mod = true; }
    /* values a mod holds: its mark (a Lua mod's purple) on every object and category showing them */
    for (auto& [row, holder] : g_holders)
        for (const Row* s : g_catalog.Siblings(row))
        {
            bool lua = IsLuaHolder(holder);
            Marks& o = g_marks.objects[s->object];
            Marks& n = g_marks.navs[s->object->category];
            (lua ? o.lua : o.mod) = true;
            (lua ? n.lua : n.mod) = true;
        }
    for (auto& e : g_errors) { g_marks.objects[e.first->object].error = true; g_marks.navs[e.first->object->category].error = true; }
    for (auto& p : g_pending) mark(p.first, 1);
    for (const Row* row : SavedRows()) mark(row, 2);
    g_marks.version = g_markVersion;
    g_marks.stats = stats;
    g_marks.errors = g_errorsVersion;
    return g_marks;
}

Marks RowsMarks(const std::vector<const Row*>& rows)
{
    Marks m;
    for (const Row* row : rows)
    {
        double v;
        if (g_errors.count(row)) m.error = true;
        if (PendingOf(row)) m.pending = true;
        if (Saved(row, &v)) m.editor = true;
        auto live = g_live.find(row);
        std::string holder = live != g_live.end() ? live->second.mod : "";
        if (holder.empty()) { auto h = g_holders.find(row); if (h != g_holders.end()) holder = h->second; }
        if (!holder.empty()) (IsLuaHolder(holder) ? m.lua : m.mod) = true;
    }
    return m;
}

/* ---- editing a value ---- */
void PickerFill()
{
    const Row* row = g_picker.row;
    auto& out = g_picker.entries;
    out.clear();
    std::string filter = Lower(g_picker.filter);
    auto match = [&](const std::string& text) { return filter.empty() || Lower(text).find(filter) != std::string::npos; };
    if (!row->choice.empty())
    {
        std::string group;
        for (auto& o : row->options)
        {
            if (!match(o.label + " " + o.group + " " + o.note)) continue;
            if (!o.group.empty() && o.group != group) { group = o.group; out.push_back({true, 0, group}); } /* a pod's pickups by kind */
            out.push_back({false, o.value, o.label, o.note, o.icon});
        }
        return;
    }
    if (row->Valued())
    {
        RowView v = ViewOf(row, false);
        for (auto& o : row->options)
            if (match(o.label)) out.push_back({false, o.value, o.label, v.hasBase && o.value == v.base ? "the game's own" : ""});
        return;
    }
    RowView v = ViewOf(row, false);
    double own = v.hasBase ? v.base : v.value;
    std::string what = row->pick == "status" ? "applies" : row->pick == "explosion" ? "sets off" : "fires";
    std::string ownLabel = (row->pick == "projectile" ? "Own projectile: " : row->pick == "explosion" ? "Own explosion: " : "Own status: ") + RefLabel(row, own);
    if (match(ownLabel)) out.push_back({false, own, ownLabel, "what it " + what + " in the game's own data"});
    if (row->pick != "projectile" && own != 0 && match(row->pick == "status" ? "Empty" : "None"))
        out.push_back({false, 0, row->pick == "status" ? "Empty" : "None", row->pick == "status" ? "an empty slot" : "no explosion"});
    auto list = g_catalog.references.find(row->pick);
    if (list == g_catalog.references.end()) return;
    std::string group;
    for (auto& o : list->second)
    {
        if (o.value == own || !match(o.label + " " + o.group + " " + o.note)) continue;
        if (std::find(row->exclude.begin(), row->exclude.end(), o.value) != row->exclude.end()) continue;
        if (o.group != group) { group = o.group; out.push_back({true, 0, group}); }
        out.push_back({false, o.value, o.label, o.note, "", o.warnings});
    }
}

/* Whether `i` is an entry of the list (a filter that matches nothing leaves it empty and the index -1). */
bool PickerHas(int i) { return i >= 0 && i < (int)g_picker.entries.size(); }

/* The next selectable entry from `from` going `step` (headers are skipped), or `from`. */
int PickerStep(int from, int step)
{
    int n = (int)g_picker.entries.size();
    for (int i = from + step; i >= 0 && i < n; i += step) if (!g_picker.entries[i].header) return i;
    return from;
}

void PickerSelect(double value)
{
    g_picker.index = PickerStep(-1, 1);
    for (size_t i = 0; i < g_picker.entries.size(); i++)
        if (!g_picker.entries[i].header && g_picker.entries[i].value == value) { g_picker.index = (int)i; break; }
}

static void OpenPicker(const Row* row)
{
    g_picker.on = true;
    g_picker.row = row;
    g_picker.filter.clear();
    PickerFill();
    PickerSelect(ViewOf(row).value);
    g_scroll["picker"] = std::max(0, g_picker.index - 4);
    g_seen.erase("picker");
}

void PickerChoose(int i)
{
    if (!PickerHas(i) || g_picker.entries[i].header) return;
    const Row* row = g_picker.row;
    double value = g_picker.entries[i].value;
    if (row->pick == "status" && value == 0 && LaterSlotFilled(row))
    { Toast("Empty the later status slots first: the slots fill in order, from slot 1", ERROR_); return; }
    Stage(row, value);
    g_picker.on = false;
    Sound(sounds::CLICK);
}

void BeginEdit(const Row* row, const std::string& text, bool fresh)
{
    if (RefuseLocked()) return;
    std::string lock = LockOf(row), block = SlotBlock(row);
    if (!lock.empty() || !block.empty()) { Toast("Locked for now: " + (lock.empty() ? block : lock), ERROR_); return; }
    if (!row->choice.empty() || !row->pick.empty() || row->Valued()) { OpenPicker(row); return; }
    RowView v = ViewOf(row);
    g_edit.on = true;
    g_edit.row = row;
    g_edit.fresh = fresh && text.empty();
    g_edit.buffer = !text.empty() ? text : v.has ? Format(row, v.value) : "";
}


static bool Parse(const Row* row, const std::string& text, double* out, std::string* error)
{
    std::string t = text;
    if (t.find('.') == std::string::npos && std::count(t.begin(), t.end(), ',') == 1) std::replace(t.begin(), t.end(), ',', '.');
    char* end = nullptr;
    double v = strtod(t.c_str(), &end);
    if (t.empty() || end == t.c_str() || *end != 0 || !isfinite(v)) { *error = "Enter a number."; return false; }
    if (row->unlimited && (v == -1 || v == 0)) { *out = 4294967295.0; return true; } /* -1 or 0: unlimited */
    return Valid(row, v, out, error);
}

/* A number as the row takes it (whole, in range, fitting its storage), or false with *error. */
bool Valid(const Row* row, double v, double* out, std::string* error)
{
    if (!isfinite(v)) { *error = "Enter a number."; return false; }
    if (row->Whole() && v != floor(v)) { *error = "Whole numbers only."; return false; }
    if ((row->hasMin && v < row->min) || (row->hasMax && v > row->max))
    { *error = "Allowed range: " + Format(row, row->hasMin ? row->min : -INFINITY) + " to " + Format(row, row->max); return false; }
    if (row->type == HF_TYPE_U32 && (v < 0 || v > 4294967295.0)) { *error = v < 0 ? "Must not be negative." : "Number is too large."; return false; }
    if (row->type == HF_TYPE_I32 && (v < -2147483648.0 || v > 2147483647.0)) { *error = "Number is too large."; return false; }
    if (row->type == HF_TYPE_F32 && fabs(v) > 3.4e38) { *error = "Number is too large."; return false; }
    if (row->type == HF_TYPE_U8 && (v < 0 || v > 255)) { *error = "Allowed range: 0 to 255."; return false; }
    if (row->Valued() && row->OptionOf(v) == nullptr) { *error = "not one of its values"; return false; }
    *out = v;
    return true;
}

void CommitEdit()
{
    if (!g_edit.on) return;
    g_edit.on = false;
    double v;
    std::string error;
    if (g_edit.buffer.empty()) return;
    if (!Parse(g_edit.row, g_edit.buffer, &v, &error)) { Toast(error, ERROR_); return; }
    Stage(g_edit.row, v);
}

void Nudge(const Row* row, int direction, bool shift, bool ctrl)
{
    if (!row->pick.empty() || !LockOf(row).empty() || !SlotBlock(row).empty()) return;
    RowView v = ViewOf(row);
    /* mission uses (HD2R Editor's): left from 1 is unlimited, left from unlimited is the most (100) */
    if (row->unlimited && v.value == 4294967295.0) { if (direction < 0) Stage(row, row->hasMax ? row->max : 100); return; }
    if (row->unlimited && direction < 0 && v.value <= (row->hasMin ? row->min : 1)) { Stage(row, 4294967295.0); return; }
    if (!row->choice.empty() || row->Valued())
    {
        int n = (int)row->options.size(), at = 0;
        for (int i = 0; i < n; i++) if (row->options[i].value == v.value) at = i;
        Stage(row, row->options[(at + direction + n) % n].value);
        return;
    }
    if (!v.has) return;
    double step = row->Whole() ? 1 : v.value == 0 ? 0.1 : pow(10, floor(log10(fabs(v.value))) - 1);
    if (shift) step *= 10;
    if (ctrl) step = row->Whole() ? 1 : step / 10;
    double next = v.value + direction * step;
    if (row->Whole()) next = llround(next);
    if (row->hasMin) next = std::max(next, row->min);
    if (row->hasMax) next = std::min(next, row->max);
    if (row->type == HF_TYPE_U32) next = std::max(next, 0.0);
    Stage(row, next);
}

/* Reverts a field: its pending change (or pending reset) dropped, else a reset staged. */
void Revert(const Row* row)
{
    if (PendingOf(row)) { for (const Row* s : g_catalog.Siblings(row)) g_pending.erase(s); g_markVersion++; return; }
    StageReset(row);
}
} // namespace editorapp

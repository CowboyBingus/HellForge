/* The editor's window (app_internal.h): its keys. */
#include <string.h>
#include <algorithm>
#include "app_internal.h"

namespace editorapp
{
/* ---- keys ---- */

static void NextView(int delta)
{
    g_view = (View)(((int)g_view + delta + VIEW_COUNT) % VIEW_COUNT);
    g_edit.on = false;
    if (g_view == PRESETS) ScanPresets();
    if (g_view == LOGS) LogRead(true);
    if (g_view == MODS) ScanMods(true);
}

static std::vector<std::string> Chars()
{
    std::vector<std::string> out;
    ImGuiIO& io = ImGui::GetIO();
    for (ImWchar c : io.InputQueueCharacters)
    {
        if (c < 0x20 || c == 0x7F) continue;
        char text[5] = {};
        if (c < 0x80) text[0] = (char)c;
        else if (c < 0x800) { text[0] = (char)(0xC0 | (c >> 6)); text[1] = (char)(0x80 | (c & 0x3F)); }
        else { text[0] = (char)(0xE0 | (c >> 12)); text[1] = (char)(0x80 | ((c >> 6) & 0x3F)); text[2] = (char)(0x80 | (c & 0x3F)); }
        out.push_back(text);
    }
    io.InputQueueCharacters.resize(0);
    return out;
}

static void DropLast(std::string& text)
{
    if (text.empty()) return;
    size_t n = text.size() - 1;
    while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
    text.resize(n);
}

void HandleKeys()
{
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl, shift = io.KeyShift;
    auto chars = Chars();
    if (g_sync.on) { SyncKeys(); return; }
    if (g_errorsPopup.on)
    {
        auto list = ErrorRows();
        int n = std::max(1, (int)list.size());
        if (Pressed(ImGuiKey_UpArrow)) g_errorsPopup.index = std::max(0, g_errorsPopup.index - 1);
        if (Pressed(ImGuiKey_DownArrow)) g_errorsPopup.index = std::min(n - 1, g_errorsPopup.index + 1);
        if (Pressed(ImGuiKey_PageUp)) g_errorsPopup.index = std::max(0, g_errorsPopup.index - 8);
        if (Pressed(ImGuiKey_PageDown)) g_errorsPopup.index = std::min(n - 1, g_errorsPopup.index + 8);
        if ((Pressed(ImGuiKey_Enter) || Pressed(ImGuiKey_KeypadEnter)) && g_errorsPopup.index < (int)list.size())
        { g_errorsPopup.on = false; Reveal(list[g_errorsPopup.index]); Sound(sounds::OPEN); }
        if ((Pressed(ImGuiKey_Delete) || Pressed(ImGuiKey_Backspace)) && g_errorsPopup.index < (int)list.size()) { Revert(list[g_errorsPopup.index]); Sound(sounds::CLICK); }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { g_errorsPopup.on = false; Sound(sounds::BACK); }
        return;
    }
    if (g_picker.on)
    {
        bool refilter = false;
        for (auto& c : chars) if (g_picker.filter.size() < 32 && !ctrl) { g_picker.filter += c; refilter = true; }
        if (Pressed(ImGuiKey_Backspace) && !g_picker.filter.empty()) { DropLast(g_picker.filter); refilter = true; }
        if (refilter)
        {
            double was = PickerHas(g_picker.index) ? g_picker.entries[g_picker.index].value : ViewOf(g_picker.row).value;
            PickerFill();
            PickerSelect(was);
            g_scroll["picker"] = 0;
        }
        if (Pressed(ImGuiKey_UpArrow)) g_picker.index = PickerStep(g_picker.index, -1);
        if (Pressed(ImGuiKey_DownArrow)) g_picker.index = PickerStep(g_picker.index, 1);
        for (int k = 0; k < 8; k++)
        {
            if (Pressed(ImGuiKey_PageUp)) g_picker.index = PickerStep(g_picker.index, -1);
            if (Pressed(ImGuiKey_PageDown)) g_picker.index = PickerStep(g_picker.index, 1);
        }
        if (Pressed(ImGuiKey_Enter) || Pressed(ImGuiKey_KeypadEnter)) PickerChoose(g_picker.index);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || Pressed(ImGuiKey_Delete)) { g_picker.on = false; Sound(sounds::BACK); }
        return;
    }
    if (g_edit.on)
    {
        for (auto& c : chars)
            if (strchr("0123456789.,-+eE", c[0]) && g_edit.buffer.size() < 24)
            {
                if (g_edit.fresh) { g_edit.buffer.clear(); g_edit.fresh = false; }
                g_edit.buffer += c;
            }
        if (Pressed(ImGuiKey_Backspace)) { if (g_edit.fresh) g_edit.buffer.clear(); else DropLast(g_edit.buffer); g_edit.fresh = false; }
        if (Pressed(ImGuiKey_Enter) || Pressed(ImGuiKey_KeypadEnter) || Pressed(ImGuiKey_Tab)) CommitEdit();
        else if (Pressed(ImGuiKey_UpArrow)) { CommitEdit(); g_field = std::max(0, g_field - 1); }
        else if (Pressed(ImGuiKey_DownArrow)) { CommitEdit(); g_field++; }
        else if (Pressed(ImGuiKey_Delete) || Pressed(ImGuiKey_Escape)) g_edit.on = false;
        return;
    }
    if (g_view == BROWSE && g_search.active)
    {
        for (auto& c : chars) if (g_search.text.size() < 40) { g_search.text += c; g_obj = 0; g_field = 0; }
        if (Pressed(ImGuiKey_Backspace)) { DropLast(g_search.text); g_obj = 0; }
        if (Pressed(ImGuiKey_Delete)) { g_search.text.clear(); g_obj = 0; }
        if (Pressed(ImGuiKey_Tab) || Pressed(ImGuiKey_Enter)) { g_search.active = false; g_focus = OBJECTS; }
        if (Pressed(ImGuiKey_Escape)) { g_search.active = false; g_search.text.clear(); }
        if (Pressed(ImGuiKey_DownArrow)) { auto n = (int)Objects().size(); if (n) SelectObject(std::min(n - 1, g_obj + 1)); }
        if (Pressed(ImGuiKey_UpArrow)) SelectObject(std::max(0, g_obj - 1));
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        if (g_view == SEARCH && !g_gsearch.text.empty()) { g_gsearch.text.clear(); g_gsearch.index = 0; }
        else g_host.close(); /* Closed() plays the closing sound */
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) { Apply(); return; }
    if ((Pressed(ImGuiKey_Tab) && shift) || ImGui::IsKeyPressed(ImGuiKey_F6, false)) { NextView(1); Sound(sounds::CLICK); return; }
    if (ctrl && Pressed(ImGuiKey_F)) { g_view = BROWSE; g_search.active = true; g_focus = OBJECTS; return; }
    if (ctrl && Pressed(ImGuiKey_E)) { g_view = CHANGES; return; }

    if (g_view == SEARCH)
    {
        for (auto& c : chars) if (g_gsearch.text.size() < 40) { g_gsearch.text += c; g_gsearch.index = 0; }
        if (Pressed(ImGuiKey_Backspace)) { DropLast(g_gsearch.text); g_gsearch.index = 0; }
        if (Pressed(ImGuiKey_Delete)) { g_gsearch.text.clear(); g_gsearch.index = 0; }
        auto results = SearchResults(g_gsearch.text);
        int n = std::max(1, (int)results.size());
        if (Pressed(ImGuiKey_UpArrow)) g_gsearch.index = std::max(0, g_gsearch.index - 1);
        if (Pressed(ImGuiKey_DownArrow)) g_gsearch.index = std::min(n - 1, g_gsearch.index + 1);
        if (Pressed(ImGuiKey_PageUp)) g_gsearch.index = std::max(0, g_gsearch.index - 10);
        if (Pressed(ImGuiKey_PageDown)) g_gsearch.index = std::min(n - 1, g_gsearch.index + 10);
        if ((Pressed(ImGuiKey_Enter) || Pressed(ImGuiKey_KeypadEnter)) && g_gsearch.index < (int)results.size())
        { GotoObject(results[g_gsearch.index].object); g_view = BROWSE; g_focus = OBJECTS; Sound(sounds::OPEN); }
        return;
    }
    if (g_view == CHANGES)
    {
        auto list = Changes();
        int n = std::max(1, (int)list.size());
        if (Pressed(ImGuiKey_UpArrow)) g_change = std::max(0, g_change - 1);
        if (Pressed(ImGuiKey_DownArrow)) g_change = std::min(n - 1, g_change + 1);
        if (g_change < (int)list.size())
        {
            if (Pressed(ImGuiKey_Enter)) { Reveal(list[g_change].row); Sound(sounds::OPEN); }
            else if (Pressed(ImGuiKey_Backspace)) { Revert(list[g_change].row); Sound(sounds::CLICK); }
        }
        return;
    }
    if (g_view == MODS)
    {
        auto entries = ModEntries();
        auto rows = ModRows();
        if (Pressed(ImGuiKey_UpArrow) || Pressed(ImGuiKey_DownArrow))
        {
            int dir = Pressed(ImGuiKey_UpArrow) ? -1 : 1;
            for (int i = g_modEntry + dir; i >= 0 && i < (int)entries.size(); i += dir)
                if (entries[i].mod >= 0) { g_modEntry = i; g_modRow = 0; g_scroll["modrows"] = 0; break; }
        }
        if (Pressed(ImGuiKey_PageUp)) g_modRow = std::max(0, g_modRow - 1);
        if (Pressed(ImGuiKey_PageDown)) g_modRow = std::min(std::max(0, (int)rows.size() - 1), g_modRow + 1);
        if ((Pressed(ImGuiKey_Enter) || Pressed(ImGuiKey_KeypadEnter)) && g_modRow < (int)rows.size()) { Reveal(rows[g_modRow]); Sound(sounds::OPEN); }
        if (Pressed(ImGuiKey_F5)) ScanMods(true);
        return;
    }
    if (g_view == LOGS)
    {
        int n = std::max(1, (int)g_logs.shown.size());
        auto move = [&](int to) { g_logs.index = std::clamp(to, 0, n - 1); g_logs.follow = g_logs.index >= n - 1; };
        if (Pressed(ImGuiKey_UpArrow)) move(g_logs.index - 1);
        if (Pressed(ImGuiKey_DownArrow)) move(g_logs.index + 1);
        if (Pressed(ImGuiKey_PageUp)) move(g_logs.index - 20);
        if (Pressed(ImGuiKey_PageDown)) move(g_logs.index + 20);
        if (Pressed(ImGuiKey_Home)) move(0);
        if (Pressed(ImGuiKey_End)) move(n - 1);
        if (Pressed(ImGuiKey_LeftArrow) || Pressed(ImGuiKey_RightArrow))
        { g_logs.filter = (g_logs.filter + (Pressed(ImGuiKey_LeftArrow) ? 2 : 1)) % 3; g_logs.follow = true; LogFilter(); }
        if (Pressed(ImGuiKey_Enter)) { g_logs.source = g_logSource = 1 - g_logs.source; g_logs.follow = true; LogRead(true); SaveSettings(); }
        return;
    }
    if (g_view == PRESETS)
    {
        int n = std::max(1, (int)g_presets.size());
        if (Pressed(ImGuiKey_UpArrow)) g_preset = std::max(0, g_preset - 1);
        if (Pressed(ImGuiKey_DownArrow)) g_preset = std::min(n - 1, g_preset + 1);
        if ((Pressed(ImGuiKey_Enter) || Pressed(ImGuiKey_KeypadEnter)) && g_preset < (int)g_presets.size() && g_presets[g_preset].error.empty())
            ImportPreset(g_presets[g_preset]);
        return;
    }
    if (g_view == SETTINGS)
    {
        int index = 0;
        for (int i = 0; i < UI_SCALE_COUNT; i++) if (UI_SCALES[i] == g_uiScale) index = i;
        if (Pressed(ImGuiKey_LeftArrow) && index > 0) { g_uiScale = UI_SCALES[index - 1]; SaveSettings(); Sound(sounds::CLICK); }
        if (Pressed(ImGuiKey_RightArrow) && index + 1 < UI_SCALE_COUNT) { g_uiScale = UI_SCALES[index + 1]; SaveSettings(); Sound(sounds::CLICK); }
        return;
    }

    /* BROWSE */
    if (Pressed(ImGuiKey_Tab)) { g_focus = g_focus == CATEGORIES ? OBJECTS : g_focus == OBJECTS ? FIELDS : CATEGORIES; return; }
    if (g_focus == CATEGORIES)
    {
        if (Pressed(ImGuiKey_UpArrow)) MoveCategory(-1);
        if (Pressed(ImGuiKey_DownArrow)) MoveCategory(1);
        if (Pressed(ImGuiKey_RightArrow) || Pressed(ImGuiKey_Enter)) { g_focus = OBJECTS; if (Pressed(ImGuiKey_Enter)) Sound(sounds::CLICK); }
        return;
    }
    if (g_focus == OBJECTS)
    {
        int n = (int)Objects().size();
        if (Pressed(ImGuiKey_UpArrow)) SelectObject(std::max(0, g_obj - 1));
        if (Pressed(ImGuiKey_DownArrow) && n) SelectObject(std::min(n - 1, g_obj + 1));
        if (Pressed(ImGuiKey_PageUp)) SelectObject(std::max(0, g_obj - 10));
        if (Pressed(ImGuiKey_PageDown) && n) SelectObject(std::min(n - 1, g_obj + 10));
        if (Pressed(ImGuiKey_LeftArrow)) g_focus = CATEGORIES;
        if ((Pressed(ImGuiKey_RightArrow) || Pressed(ImGuiKey_Enter)) && n) { g_focus = FIELDS; if (Pressed(ImGuiKey_Enter)) Sound(sounds::CLICK); }
        for (auto& c : chars) if (isalnum((unsigned char)c[0])) { g_search.active = true; g_search.text = c; g_obj = 0; break; }
        return;
    }
    /* fields */
    Object* object = Current();
    auto items = Items(object);
    if (items.empty()) { if (Pressed(ImGuiKey_LeftArrow)) g_focus = OBJECTS; return; }
    g_field = std::clamp(g_field, 0, (int)items.size() - 1);
    if (ctrl && Pressed(ImGuiKey_UpArrow)) { SetPageOpen(object, false); return; }
    if (ctrl && Pressed(ImGuiKey_DownArrow)) { SetPageOpen(object, true); return; }
    if (Pressed(ImGuiKey_UpArrow)) g_field = std::max(0, g_field - 1);
    if (Pressed(ImGuiKey_DownArrow)) g_field = std::min((int)items.size() - 1, g_field + 1);
    if (Pressed(ImGuiKey_PageUp)) g_field = std::max(0, g_field - 10);
    if (Pressed(ImGuiKey_PageDown)) g_field = std::min((int)items.size() - 1, g_field + 10);
    const Item& item = items[std::clamp(g_field, 0, (int)items.size() - 1)];
    if (item.header)
    {
        if (Pressed(ImGuiKey_Enter)) { Toggle(item.key); Sound(sounds::CLICK); }
        if (Pressed(ImGuiKey_RightArrow)) Toggle(item.key, 1);
        if (Pressed(ImGuiKey_LeftArrow)) { if (item.open) Toggle(item.key, 0); else g_focus = OBJECTS; }
        return;
    }
    if (item.row == nullptr) { if (Pressed(ImGuiKey_LeftArrow)) g_focus = OBJECTS; return; } /* a note */
    if (Pressed(ImGuiKey_LeftArrow)) Nudge(item.row, -1, shift, ctrl);
    if (Pressed(ImGuiKey_RightArrow)) Nudge(item.row, 1, shift, ctrl);
    if (Pressed(ImGuiKey_Enter) || Pressed(ImGuiKey_KeypadEnter)) { BeginEdit(item.row, "", true); Sound(sounds::CLICK); }
    if (Pressed(ImGuiKey_Delete)) { Revert(item.row); Sound(sounds::CLICK); }
    /* digits start a value: everything typed this frame */
    std::string typed;
    for (auto& c : chars) if (strchr("0123456789.,-", c[0]) && (typed.size() || c[0] != ',')) typed += c;
    if (!typed.empty() && item.row->choice.empty() && item.row->pick.empty()) BeginEdit(item.row, typed, false);
}
} // namespace editorapp

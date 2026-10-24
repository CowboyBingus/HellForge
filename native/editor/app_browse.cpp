/* The editor's window (app_internal.h): the header, BROWSE's panes, the status bar and the footer. */
#include <algorithm>
#include "app_internal.h"
#include "icons.h"

namespace editorapp
{
/* ---- the header ---- */
void DrawHeader(int errors, int active)
{
    float h = HEADER_H;
    g_cv.Rect(0, 0, PANEL_W, h, HEADER, 1);
    g_cv.Rect(0, h - 1, PANEL_W, 1, LINE_STRONG, 2);
    const icons::Icon* logo = icons::Logo();
    if (logo != nullptr && icons::Texture()) g_cv.Image(icons::Texture(), ImVec2(14, 9), ImVec2(40, 40), logo->uv0, logo->uv1, IM_COL32_WHITE, 4);
    else Logo(18, 15, 28);
    float x = 62;
    x += g_cv.Text("HELLFORGE", x, h / 2, T(SZ_TITLE, TEXT, ui::TITLE));
    x += 8 + g_cv.Text("EDITOR", x + 8, h / 2, T(SZ_TITLE, BRAND, ui::TITLE));
    float tx = x + 24;
    for (int v = 0; v < VIEW_COUNT; v++)
    {
        std::string label = VIEW_LABELS[v];
        if (v == CHANGES) { int n = (int)Changes().size(); if (n > 0) label += "  " + std::to_string(n); }
        float w = g_cv.Measure(label, SZ_TAB, ui::TITLE) + 22;
        uint64_t id = ui::Id("tab", (uint64_t)v);
        bool selected = g_view == v;
        if (selected) { g_cv.Rect(tx, h - 3, w, 3, GOLD, 4); g_cv.Rect(tx, 8, w, h - 11, GOLD_WASH, 2); }
        else if (g_cv.Hovered() == id) g_cv.Rect(tx, 8, w, h - 11, HOVER, 2);
        g_cv.Text(label, tx + 11, h / 2 + 1, T(SZ_TAB, selected ? GOLD : DIM, ui::TITLE));
        if (g_cv.Hit(tx, 6, w, h - 6, id)) { g_view = (View)v; g_edit.on = false; if (v == PRESETS) ScanPresets(); if (v == LOGS) LogRead(true); if (v == MODS) ScanMods(true); }
        tx += w + 2;
    }
    float tabsEnd = tx + 8;
    uint64_t closeId = ui::Id("close");
    bool hovered = g_cv.Hovered() == closeId;
    g_cv.Rect(PANEL_W - 50, 12, 36, 34, hovered ? ERROR_SOFT : PANEL, 3);
    g_cv.Frame(PANEL_W - 50, 12, 36, 34, hovered ? ERROR_ : LINE_STRONG, 4);
    g_cv.Text("\xC3\x97", PANEL_W - 32, 29, T(24, hovered ? ERROR_ : DIM, ui::BODY, ui::CENTER, 0, 6));
    if (g_cv.Hit(PANEL_W - 50, 12, 36, 34, closeId)) g_host.close();
    float rx = PANEL_W - 62;
    auto status = [&](const std::string& text, ImU32 fg, ImU32 bg, uint64_t action) {
        float w = g_cv.Measure(text, SZ_TINY, ui::TITLE) + 14;
        if (rx - w < tabsEnd) return false;
        bool clicked = false;
        if (action)
        {
            if (g_cv.Hovered() == action) g_cv.Frame(rx - w - 2, h / 2 - 12, w + 4, 24, fg, 4);
            clicked = g_cv.Hit(rx - w - 4, h / 2 - 14, w + 8, 28, action);
        }
        rx -= Chip(text, rx, h / 2, fg, bg, true) + 8;
        return clicked;
    };
    if (Locked()) status(std::string("READ-ONLY \xC2\xB7 HOST: ") + (g_session.host[0] ? g_session.host : "A FRIEND"), PENDING, PENDING_SOFT, 0);
    else if (g_session.role == HF_SESSION_HOST) status(g_lobbyStats ? "HOSTING \xC2\xB7 FRIENDS PLAY WITH YOUR STATS" : "HOSTING \xC2\xB7 EVERYONE KEEPS THEIR OWN", MOD, MOD_SOFT, 0);
    else if (g_session.role == HF_SESSION_JOINER) status(std::string("JOINED \xC2\xB7 ") + (g_session.host[0] ? g_session.host : "A FRIEND") + " \xC2\xB7 YOUR OWN STATS", MOD, MOD_SOFT, 0);
    if (errors > 0 && status(std::to_string(errors) + (errors == 1 ? " ERROR" : " ERRORS"), ERROR_, ERROR_SOFT, ui::Id("errors"))) OpenErrors();
    if (active > 0) status(std::to_string(active) + (Locked() ? " ACTIVE (HOST)" : " ACTIVE"), GOLD, GOLD_WASH, 0);
    if (PendingCount() > 0 && !Locked()) status(std::to_string(PendingCount()) + " PENDING", PENDING, PENDING_SOFT, 0);
}

/* ---- BROWSE ---- */
void DrawCategories(float y0, float h)
{
    g_cv.Rect(0, y0, CAT_W, h, PANEL_ALT, 1);
    g_cv.Rect(CAT_W - 1, y0, 1, h, LINE, 2);
    const MarkCache& marks = Markers();
    bool focused = g_focus == CATEGORIES;
    const float rh = 31, headH = 24, gap = 10;
    /* Taller than the panel (a short screen, a large UI scale): it scrolls, by the wheel or its scrollbar, and a category
     * chosen with the keys is kept in view. Only whole rows are drawn, as in a List. */
    float total = gap, selTop = 0;
    for (int i = 0; i < (int)g_cats.size(); i++)
    {
        if (g_cats[i].group) { total += (i > 0 ? gap : 0) + headH; continue; }
        if (i == g_cat) selTop = total;
        total += rh;
    }
    total += gap;
    float maxScroll = std::max(0.0f, total - h);
    static int seen = -1;
    if (seen != g_cat)
    {
        seen = g_cat;
        if (selTop - gap < g_catScroll) g_catScroll = selTop - gap;
        if (selTop + rh + gap > g_catScroll + h) g_catScroll = selTop + rh + gap - h;
    }
    float wheel = g_cv.Scroll(0, y0, CAT_W, h, ui::Id("cats", 7777));
    if (wheel != 0) g_catScroll -= wheel * rh * 3;
    float trackY = y0 + 2, trackH = h - 4, thumb = std::max(24.0f, trackH * std::min(1.0f, h / std::max(1.0f, total)));
    if (g_drag.key == "cats")
    {
        if (!ImGui::IsMouseDown(0)) g_drag.key.clear();
        else
        {
            float ux, uy;
            g_cv.ToUnits(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, &ux, &uy);
            g_catScroll = std::clamp((uy - trackY - thumb / 2) / std::max(1.0f, trackH - thumb), 0.0f, 1.0f) * maxScroll;
        }
    }
    g_catScroll = std::clamp(g_catScroll, 0.0f, maxScroll);
    auto inside = [&](float y, float rowH) { return y >= y0 && y + rowH <= y0 + h; };
    float yy = gap - g_catScroll;
    for (int i = 0; i < (int)g_cats.size(); i++)
    {
        const CatItem& item = g_cats[i];
        if (item.group)
        {
            yy += i > 0 ? gap : 0;
            if (inside(y0 + yy, headH)) g_cv.Text(item.label, 18, y0 + yy + 11, T(SZ_HEADING, FAINT, ui::TITLE));
            yy += headH;
            continue;
        }
        float y = y0 + yy;
        yy += rh;
        if (!inside(y, rh)) continue;
        bool selected = i == g_cat;
        uint64_t id = ui::Id("cat", (uint64_t)i);
        const catalog::Nav& nav = g_catalog.navs[item.nav];
        Tone tone = ToneOf(nav.tone);
        if (selected)
        {
            g_cv.Rect(8, y, CAT_W - 16, rh, tone.colour ? tone.soft : focused ? SELECT : GOLD_WASH, 2);
            g_cv.Rect(8, y, 3, rh, tone.colour ? tone.colour : focused ? GOLD : GOLD_DIM, 3);
        }
        else if (g_cv.Hovered() == id) g_cv.Rect(8, y, CAT_W - 16, rh, HOVER, 2);
        if (tone.colour && !selected) g_cv.Rect(8, y + 9, 3, rh - 18, tone.colour, 3);
        float cw = g_cv.Text(std::to_string(g_catalog.Of(nav.id).size()), CAT_W - 20, y + rh / 2, T(SZ_SMALL, FAINT, ui::BODY, ui::RIGHT));
        auto found = marks.navs.find(nav.id);
        float mx = DrawMarks(found != marks.navs.end() ? found->second : Marks{}, CAT_W - 28 - cw, y + rh / 2, 26);
        g_cv.Text(item.label, 22, y + rh / 2, T(SZ_LABEL, selected ? TEXT : DIM, ui::BODY, ui::LEFT, std::min(CAT_W - 80, mx - 30)));
        if (g_cv.Hit(8, y, CAT_W - 16, rh, id)) { SelectCategory(i); g_focus = CATEGORIES; }
    }
    if (maxScroll > 0)
    {
        bool dragging = g_drag.key == "cats";
        g_cv.Rect(CAT_W - 6, trackY, 3, trackH, LINE, 3);
        g_cv.Rect(CAT_W - (dragging ? 8 : 6), trackY + (trackH - thumb) * (g_catScroll / maxScroll), dragging ? 5.0f : 3.0f, thumb,
            (focused || dragging) ? GOLD : LINE_STRONG, 4);
        if (g_cv.Hit(CAT_W - 16, y0, 14, h, ui::Id("cats", 8888)))
        {
            g_drag.key = "cats"; g_drag.trackY = trackY; g_drag.trackH = trackH; g_drag.thumb = thumb;
        }
    }
}

void DrawObjects(float y0, float h)
{
    float x0 = CAT_W;
    g_cv.Rect(x0, y0, OBJ_W, h, PANEL, 1);
    g_cv.Rect(x0 + OBJ_W - 1, y0, 1, h, LINE, 2);
    float sy = y0 + 10;
    bool searching = g_search.active;
    g_cv.Rect(x0 + 12, sy, OBJ_W - 24, 32, BOX, 3);
    g_cv.Frame(x0 + 12, sy, OBJ_W - 24, 32, searching ? BOX_FOCUS : BOX_EDGE, 4);
    if (g_search.text.empty() && !searching) g_cv.Text("Search   Ctrl+F", x0 + 24, sy + 16, T(SZ_SMALL, FAINT));
    else
    {
        float w = g_cv.Text(g_search.text, x0 + 24, sy + 16, T(SZ_LABEL, TEXT, ui::BODY, ui::LEFT, OBJ_W - 60));
        if (searching && (int)(g_time * 2) % 2 == 0) g_cv.Rect(x0 + 26 + w, sy + 7, 2, 18, GOLD, 6);
    }
    if (g_cv.Hit(x0 + 12, sy, OBJ_W - 24, 32, ui::Id("objsearch"))) { g_search.active = true; g_focus = OBJECTS; }
    /* the weapon-group filter, wrapped onto as many rows as it needs */
    float ly = sy + 44;
    const catalog::Nav* nav = NavOf(g_cat);
    if (nav != nullptr && nav->hasFilters)
    {
        static const std::map<std::string, std::string> shortLabels = {{"Assault Rifle", "Assault"}, {"Marksman Rifle", "Marksman"},
            {"Submachine Gun", "SMG"}, {"Energy-Based", "Energy"}};
        std::string current = g_group.count(nav->id) ? g_group[nav->id] : "All";
        float cx = x0 + 12, cy = sy + 40;
        std::vector<std::string> groups = {"All"};
        groups.insert(groups.end(), nav->filters.begin(), nav->filters.end());
        for (auto& g : groups)
        {
            std::string label = shortLabels.count(g) ? shortLabels.at(g) : g;
            float w = g_cv.Measure(label, SZ_TINY, ui::TITLE) + 18;
            if (cx + w > x0 + OBJ_W - 12) { cx = x0 + 12; cy += 28; }
            uint64_t id = ui::Id("group" + g);
            bool on = g == current;
            g_cv.Rect(cx, cy, w, 22, on ? GOLD : g_cv.Hovered() == id ? HOVER : BOX, 3);
            if (!on) g_cv.Frame(cx, cy, w, 22, LINE_STRONG, 4);
            g_cv.Text(label, cx + w / 2, cy + 11, T(SZ_TINY, on ? INVERSE : DIM, ui::TITLE, ui::CENTER, 0, 5));
            if (g_cv.Hit(cx, cy, w, 22, id)) { if (g != current) { g_group[nav->id] = g; g_obj = 0; g_field = 0; } g_focus = OBJECTS; }
            cx += w + 6;
        }
        ly = cy + 32;
    }
    auto objects = Objects();
    g_obj = std::clamp(g_obj, 0, std::max(0, (int)objects.size() - 1));
    const MarkCache& marks = Markers();
    bool focused = g_focus == OBJECTS;
    if (objects.empty()) g_cv.Text(!g_search.text.empty() ? "No match" : "Nothing here", x0 + 20, ly + 20, T(SZ_SMALL, FAINT, ui::BODY, ui::LEFT, OBJ_W - 40));
    ListOpts o;
    o.x = x0; o.y = ly; o.w = OBJ_W - 1; o.h = y0 + h - ly - 6; o.rowH = LIST_ROW_H + 8; o.count = (int)objects.size(); o.selected = g_obj;
    o.key = "obj"; o.focused = focused;
    o.click = [](int i) { SelectObject(i); g_focus = OBJECTS; };
    o.draw = [&](int i, float x, float y, float w, float rh, bool hovered) {
        Object* object = objects[i];
        bool selected = i == g_obj;
        Tone tone = ToneOf(object->tone);
        if (selected)
        {
            g_cv.Rect(x + 8, y + 2, w - 16, rh - 4, tone.colour ? tone.soft : focused ? SELECT : GOLD_WASH, 2);
            g_cv.Rect(x + 8, y + 2, 3, rh - 4, tone.colour ? tone.colour : focused ? GOLD : GOLD_DIM, 3);
        }
        else if (hovered) g_cv.Rect(x + 8, y + 2, w - 16, rh - 4, HOVER, 2);
        float tx = x + 22;
        if (DrawIcon(object, x + 18, y + 5, rh - 10)) tx = x + 18 + rh;
        g_cv.Text(object->name, tx, y + 15, T(SZ_LABEL, selected ? TEXT : DIM, ui::BODY, ui::LEFT, w - 56 - (tx - x)));
        g_cv.Text(object->subtitle, tx, y + 31, T(SZ_TINY, FAINT, ui::BODY, ui::LEFT, w - 56 - (tx - x)));
        auto found = marks.objects.find(object);
        DrawMarks(found != marks.objects.end() ? found->second : Marks{}, x + w - 10, y + rh / 2, 34);
    };
    List(o);
}

void DrawFields(float y0, float h)
{
    float x0 = CAT_W + OBJ_W, w = PANEL_W - x0;
    g_cv.Rect(x0, y0, w, h, PANEL_ALT, 1);
    Object* object = Current();
    if (object == nullptr) { g_cv.Text("Select a category and an object", x0 + 24, y0 + 40, T(SZ_LABEL, FAINT)); return; }
    float hx = x0 + 20;
    if (DrawIcon(object, x0 + 20, y0 + 8, 48)) hx = x0 + 80;
    bool anyOpen = PageOpen(object);
    float bw = 142;
    if (Button(anyOpen ? "COLLAPSE ALL" : "EXPAND ALL", x0 + w - bw - 14, y0 + 12, bw, 28, NORMAL, ui::Id("pageopen"))) SetPageOpen(object, !anyOpen);
    float rw = 132;
    bool changed = !ItemChanges(object).empty();
    if (Button("RESET ITEM", x0 + w - bw - rw - 24, y0 + 12, rw, 28, changed && !Locked() ? DANGER : DISABLED, ui::Id("resetitem")) && changed) ResetItem(object);
    g_cv.Text(object->name, hx, y0 + 24, T(20, TEXT, ui::TITLE, ui::LEFT, w - 70 - bw - rw - (hx - x0)));
    int edited = 0;
    for (const Row* row : object->rows) { RowView v = ViewOf(row); if (v.source == STAGED || v.source == EDITED) edited++; }
    std::string sub = (object->subtitle.empty() ? "" : object->subtitle + "   ") + std::to_string(object->rows.size()) + " fields" +
        (edited > 0 ? "   " + std::to_string(edited) + " edited" : "");
    g_cv.Text(sub, hx, y0 + 47, T(SZ_SMALL, FAINT, ui::BODY, ui::LEFT, w - 40 - (hx - x0)));
    float hy = y0 + 66;
    g_cv.Rect(x0, hy, w, 22, HEADER, 2);
    g_cv.Text("FIELD", x0 + FX_LABEL, hy + 11, T(SZ_TINY, FAINT, ui::TITLE));
    g_cv.Text("DEFAULT", x0 + FX_DEFAULT_RIGHT, hy + 11, T(SZ_TINY, FAINT, ui::TITLE, ui::RIGHT));
    g_cv.Text("VALUE", x0 + FX_BOX + FX_BOX_W - 8, hy + 11, T(SZ_TINY, FAINT, ui::TITLE, ui::RIGHT));
    g_cv.Text("SOURCE", x0 + w + FX_BADGE_RIGHT, hy + 11, T(SZ_TINY, FAINT, ui::TITLE, ui::RIGHT));
    auto items = Items(object);
    g_field = std::clamp(g_field, 0, std::max(0, (int)items.size() - 1));
    bool focused = g_focus == FIELDS;
    float ly = hy + 24, rh = ROW_H;
    ListOpts o;
    o.x = x0; o.y = ly; o.w = w; o.h = y0 + h - ly - 4; o.rowH = rh; o.count = (int)items.size(); o.selected = g_field; o.key = "fields"; o.focused = focused;
    o.click = [&](int i) {
        g_field = i;
        g_focus = FIELDS;
        if (items[i].header) { Toggle(items[i].key); return; }
        if (items[i].row == nullptr) return;
        float ux, uy;
        g_cv.ToUnits(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, &ux, &uy);
        if (ux >= x0 + FX_BOX && ux <= x0 + FX_BOX + FX_BOX_W) BeginEdit(items[i].row, "", true);
    };
    o.rightClick = [&](int i) { if (!items[i].header && items[i].row) Revert(items[i].row); };
    o.draw = [&](int i, float x, float y, float lw, float rowH, bool hovered) {
        const Item& item = items[i];
        bool selected = i == g_field;
        float cy = y + rowH / 2;
        if (item.header)
        {
            float indent = FX_LABEL + item.depth * INDENT;
            if (item.depth == 0) g_cv.Rect(x + 6, y + 2, lw - 12, rowH - 4, HEADER, 1);
            if (selected) { g_cv.Rect(x + 6, y + 1, lw - 12, rowH - 2, focused ? SELECT_HEADER : WASH_HEADER, 2); g_cv.Rect(x + 6, y + 1, 3, rowH - 2, focused ? GOLD : GOLD_DIM, 3); }
            else if (hovered) g_cv.Rect(x + 6, y + 1, lw - 12, rowH - 2, item.depth == 0 ? HOVER_HEADER : HOVER, 2); /* a dark header: only a hint */
            Arrow(x + indent, cy, item.open, GOLD);
            float mx = DrawMarks(RowsMarks(item.rows), x + lw - 14, cy, 28);
            float right = mx < x + lw - 14 ? mx - 8 : x + lw - 18, reserve = x + lw - right;
            if (item.depth == 0)
            {
                std::string upper = item.title;
                for (char& c : upper) c = (char)toupper((unsigned char)c);
                g_cv.Text(upper, x + indent + 20, cy, T(SZ_SMALL, GOLD, ui::TITLE, ui::LEFT, lw - indent - 170 - reserve));
                g_cv.Text(item.parts > 1 ? std::to_string(item.parts) + " parts" + SEP + std::to_string(item.count) + " fields" : std::to_string(item.count) + " fields",
                    right, cy, T(SZ_TINY, FAINT, ui::BODY, ui::RIGHT));
            }
            else
            {
                g_cv.Text(item.title, x + indent + 20, cy, T(SZ_LABEL, selected ? TEXT : DIM, ui::BODY, ui::LEFT, lw - indent - 150 - reserve));
                g_cv.Text(std::to_string(item.count) + " fields", right, cy, T(SZ_TINY, FAINT, ui::BODY, ui::RIGHT));
            }
            return;
        }
        const Row* row = item.row;
        if (selected) { g_cv.Rect(x + 6, y + 1, lw - 12, rowH - 2, focused ? SELECT : GOLD_WASH, 2); g_cv.Rect(x + 6, y + 1, 3, rowH - 2, focused ? GOLD : GOLD_DIM, 3); }
        else if (hovered) g_cv.Rect(x + 6, y + 1, lw - 12, rowH - 2, HOVER, 2);
        float lx = FX_LABEL + (item.depth > 0 ? (item.depth + 1) * INDENT : 0);
        if (row == nullptr) { g_cv.Text(item.note, x + lx, cy, T(SZ_SMALL, item.detail ? FAINT : PENDING, ui::BODY, ui::LEFT, lw - lx - 24)); return; }
        bool locked = !LockOf(row).empty(), blocked = !SlotBlock(row).empty();
        RowView v = ViewOf(row);
        std::string def = v.hasBase ? Format(row, v.base) : "\xE2\x80\x94";
        float dw = std::min(84.0f, g_cv.Measure(def, SZ_SMALL));
        float labelMax = FX_DEFAULT_RIGHT - std::max(dw, 24.0f) - 12 - lx;
        g_cv.Text(RowLabel(item, labelMax), x + lx, cy, T(SZ_LABEL, selected ? TEXT : DIM, ui::BODY, ui::LEFT, labelMax));
        g_cv.Text(def, x + FX_DEFAULT_RIGHT, cy, T(SZ_SMALL, FAINT, ui::BODY, ui::RIGHT, 84));
        bool editing = g_edit.on && g_edit.row == row;
        bool error = g_errors.count(row) > 0;
        float bx = x + FX_BOX, bw2 = FX_BOX_W;
        ImU32 edge = BOX_EDGE, colour = TEXT;
        if (editing) edge = BOX_FOCUS;
        else if (error && v.source != STAGED) edge = ERROR_;
        else if (v.source == STAGED) { edge = PENDING; colour = PENDING; }
        else if (v.source == EDITED) { edge = GOLD_DIM; colour = GOLD; }
        else if (v.source == MODDED) colour = IsLuaHolder(v.holder) ? LUA : MOD;
        if ((locked || blocked) && v.source != STAGED) colour = FAINT;
        g_cv.Rect(bx, y + 4, bw2, rowH - 8, BOX, 3);
        g_cv.Frame(bx, y + 4, bw2, rowH - 8, edge, 4);
        if (editing)
        {
            float tw = g_cv.Text(g_edit.buffer, bx + bw2 - 10, cy, T(SZ_LABEL, TEXT, ui::BODY, ui::RIGHT, bw2 - 16));
            if (g_edit.fresh && !g_edit.buffer.empty()) g_cv.Rect(bx + bw2 - 12 - tw, y + 7, tw + 4, rowH - 14, GOLD_SOFT, 4);
            if ((int)(g_time * 2.2f) % 2 == 0) g_cv.Rect(bx + bw2 - 8, y + 8, 2, rowH - 16, GOLD, 6);
        }
        else g_cv.Text(v.has ? Format(row, v.value) : "\xE2\x80\x94", bx + bw2 - 10, cy, T(SZ_LABEL, v.has ? colour : FAINT, ui::BODY, ui::RIGHT, bw2 - 16));
        if (!row->unit.empty()) g_cv.Text(row->unit, x + FX_UNIT, cy, T(SZ_TINY, FAINT, ui::BODY, ui::LEFT, 56));
        float bxr = x + lw + FX_BADGE_RIGHT;
        if (error) Chip("ERROR", bxr, cy, ERROR_, ERROR_SOFT, true);
        else if (locked) Chip("LOCKED", bxr, cy, FAINT, LINE, true);
        else if (blocked && v.source != STAGED) Chip("IN ORDER", bxr, cy, FAINT, LINE, true);
        else if (v.source == STAGED) Chip(v.reset ? "PENDING RESET" : "PENDING", bxr, cy, PENDING, PENDING_SOFT, true);
        else if (v.source == EDITED) Chip(row->Live() ? "EDITED" : "NEXT BOOT", bxr, cy, GOLD, GOLD_WASH, true);
        else if (v.source == MODDED && IsLuaHolder(v.holder)) Chip("LUA", bxr, cy, LUA, LUA_SOFT, true);
        else if (v.source == MODDED) Chip("MOD", bxr, cy, MOD, MOD_SOFT, true);
    };
    List(o);
}

/* ---- the status bar and the footer ---- */
void DrawStatus(float y, float h)
{
    g_cv.Rect(0, y, PANEL_W, h, HEADER, 1);
    g_cv.Rect(0, y, PANEL_W, 1, LINE, 2);
    float cy = y + h / 2;
    if (g_toast.on && g_time - g_toast.time > 3.2f) g_toast.on = false;
    if (g_toast.on) g_cv.Text(g_toast.text, PANEL_W - 18, cy, T(SZ_SMALL, g_toast.colour, ui::BODY, ui::RIGHT, 520));
    float maxw = g_toast.on ? PANEL_W - 580 : PANEL_W - 40;
    if (!g_catalogOk) { g_cv.Text("The editor's catalogue could not be read: " + g_catalogError, 18, cy, T(SZ_SMALL, ERROR_, ui::BODY, ui::LEFT, maxw)); return; }
    if (g_view == BROWSE)
    {
        auto items = Items(Current());
        const Row* row = g_focus == FIELDS && g_field < (int)items.size() ? items[g_field].row : nullptr;
        if (row == nullptr && g_focus == FIELDS && g_field < (int)items.size() && !items[g_field].note.empty())
        { g_cv.Text(items[g_field].note, 18, cy, T(SZ_SMALL, PENDING, ui::BODY, ui::LEFT, maxw)); return; }
        if (row != nullptr && (!LockOf(row).empty() || !SlotBlock(row).empty()))
        {
            std::string why = LockOf(row);
            g_cv.Text("Locked for now: " + (why.empty() ? SlotBlock(row) : why), 18, cy, T(SZ_SMALL, FAINT, ui::BODY, ui::LEFT, maxw));
            return;
        }
        if (row != nullptr && !row->pick.empty())
        {
            RowView v = ViewOf(row);
            const Row::Option* o = v.has ? g_catalog.Reference(row->pick, v.value) : nullptr;
            float x = 18;
            x += g_cv.Text(row->key, x, cy, T(SZ_SMALL, DIM, ui::BODY, ui::LEFT, 300)) + 16;
            x += g_cv.Text(AppliesText(row), x, cy, T(SZ_SMALL, row->Live() ? FAINT : PENDING, ui::BODY, ui::LEFT, 220)) + 14;
            if (row->shared || !row->also.empty()) x += Chip("SHARED", x, cy, PENDING, PENDING_SOFT, false) + 6;
            std::string text = !row->note.empty() ? row->note : o != nullptr ? o->group + ": " + o->note :
                "Enter picks " + std::string(row->pick == "status" ? "a status effect" : row->pick == "explosion" ? "an explosion" : "a projectile");
            g_cv.Text(text, x + 6, cy, T(SZ_SMALL, FAINT, ui::BODY, ui::LEFT, std::max(40.0f, maxw - x - 6)));
            return;
        }
        if (row == nullptr) { g_cv.Text("Pick a field: \xE2\x86\x91\xE2\x86\x93 to move, \xE2\x86\x92 to go deeper, Tab to switch panes", 18, cy, T(SZ_SMALL, FAINT, ui::BODY, ui::LEFT, maxw)); return; }
        auto error = g_errors.find(row);
        if (error != g_errors.end()) { g_cv.Text("Error: " + error->second, 18, cy, T(SZ_SMALL, ERROR_, ui::BODY, ui::LEFT, maxw)); return; }
        float x = 18;
        if (!row->choice.empty())
        {
            RowView v = ViewOf(row);
            const Row::Option* o = row->OptionOf(v.value);
            std::string text = o && !o->note.empty() ? o->label + ": " + o->note : row->note;
            if (IsMount(row)) text += row->choice == "rowcopy" ? "   [applies now]" : IsPod(row) ? "   [applies to pods called in after it]" :
                "   [applies to vehicles called in after it]";
            else if (HasPassives())
            {
                HfPassives state;
                g_host.api->get_passives(g_host.mod, &state);
                if (state.held || state.status[0]) text += std::string("   [") + state.status + "]";
            }
            else text += "   [this HellForge has no set_passives]";
            g_cv.Text(text, x, cy, T(SZ_SMALL, FAINT, ui::BODY, ui::LEFT, maxw));
            return;
        }
        std::string range;
        if (row->hasMin || row->hasMax)
            range = "range " + (row->hasMin ? Format(row, row->min) : std::string("-inf")) + " to " + (row->hasMax ? Format(row, row->max) : std::string("inf"));
        else range = "no published range";
        if (row->Whole()) range += ", whole numbers";
        if (row->unlimited) range = "unlimited (-1 or 0), or " + (row->hasMin ? Format(row, row->min) : std::string("1")) + " to " +
            (row->hasMax ? Format(row, row->max) : std::string("100"));
        x += g_cv.Text(row->key, x, cy, T(SZ_SMALL, DIM, ui::BODY, ui::LEFT, 260)) + 16;
        x += g_cv.Text(range, x, cy, T(SZ_SMALL, FAINT, ui::BODY, ui::LEFT, 240)) + 14;
        x += g_cv.Text(AppliesText(row), x, cy, T(SZ_SMALL, row->Live() ? FAINT : PENDING, ui::BODY, ui::LEFT, 220)) + 14;
        if (row->shared || !row->also.empty()) x += Chip("SHARED", x, cy, PENDING, PENDING_SOFT, false) + 6;
        if (row->unverified) x += Chip("UNVERIFIED EFFECT", x, cy, FAINT, LINE, false) + 6;
        RowView v = ViewOf(row);
        if (!v.holder.empty())
            g_cv.Text(std::string(IsLuaHolder(v.holder) ? "Lua mod: " : "Mod: ") + v.holder + " = " + Format(row, v.base), x + 10, cy,
                T(SZ_SMALL, IsLuaHolder(v.holder) ? LUA : MOD, ui::BODY, ui::LEFT, std::max(40.0f, maxw - x - 10)));
        else if (!row->also.empty() || !row->alsoIf.empty())
        {
            auto list = [](const std::vector<std::string>& of) {
                std::string names;
                for (size_t i = 0; i < of.size() && i < 2; i++) names += (i ? ", " : "") + of[i];
                if (of.size() > 2) names += " +" + std::to_string(of.size() - 2);
                return names;
            };
            /* weapons that reach the row only by equipping the same ammunition are named as such (bug-report-0035) */
            std::string text = row->also.empty() ? "" : "also changes " + list(row->also);
            if (!row->alsoIf.empty())
                text += (text.empty() ? "also changes " : "; and ") + list(row->alsoIf) + " when they use " + row->alsoAmmo;
            g_cv.Text(text, x + 10, cy, T(SZ_SMALL, PENDING, ui::BODY, ui::LEFT, std::max(40.0f, maxw - x - 10)));
        }
        else if (!row->note.empty())
            g_cv.Text(row->note, x + 10, cy, T(SZ_SMALL, FAINT, ui::BODY, ui::LEFT, std::max(40.0f, maxw - x - 10)));
        return;
    }
    const char* text = g_view == SEARCH ? "Every word must match, in any order. Enter or a click opens the object in BROWSE." :
        g_view == CHANGES ? "Enter jumps to the field. RESET or Backspace stages a reset, UNDO drops a pending change; APPLY writes them." :
        g_view == MODS ? "\xE2\x86\x91\xE2\x86\x93 mod   PgUp PgDn value   Enter or a click opens the field; its reset goes back to the mod's value   F5 read again" :
        g_view == PRESETS ? "Enter or IMPORT stages the selected preset; EXPORT writes your saved changes as a preset HD2R Editor reads." :
        g_view == LOGS ? "\xE2\x86\x90\xE2\x86\x92 filter   Enter switch HellForge's log / the editor's   End follow the end" :
        "Settings are saved with the editor and apply at once.";
    g_cv.Text(text, 18, cy, T(SZ_SMALL, FAINT, ui::BODY, ui::LEFT, maxw));
}

void DrawFooter(float y, float h)
{
    g_cv.Rect(0, y, PANEL_W, h, FOOTER, 1);
    g_cv.Rect(0, y, PANEL_W, 1, LINE_STRONG, 2);
    std::string hint;
    auto items = g_view == BROWSE ? Items(Current()) : std::vector<Item>{};
    bool header = g_focus == FIELDS && g_field < (int)items.size() && items[g_field].header;
    if (g_edit.on) hint = "Type a number   Backspace erase   Tab / \xE2\x86\x91\xE2\x86\x93 confirm   Del cancel";
    else if (g_search.active) hint = "Type to filter   Backspace erase   Tab done   Del clear";
    else if (g_view == SEARCH) hint = "Type to search   \xE2\x86\x91\xE2\x86\x93 select   Enter open   Backspace erase   Del clear";
    else if (g_view == BROWSE && g_focus == FIELDS && header)
        hint = "F8 close   Enter open / close   \xE2\x86\x90\xE2\x86\x92 open, close   Ctrl+\xE2\x86\x91 collapse all   Ctrl+\xE2\x86\x93 expand all   F9 apply";
    else if (g_view == BROWSE && g_focus == FIELDS)
        hint = "F8 close   \xE2\x86\x90\xE2\x86\x92 adjust (Shift x10, Ctrl \xC3\xB7" "10)   digits type a value   Del revert   F9 apply";
    else hint = "F8 close   \xE2\x86\x91\xE2\x86\x93 select   \xE2\x86\x90\xE2\x86\x92 panes   Tab next pane   Shift+Tab next tab   F9 apply";
    g_cv.Text(hint, 18, y + h / 2, T(SZ_SMALL, FAINT, ui::BODY, ui::LEFT, PANEL_W - 560));
    float bh = 34, by = y + (h - bh) / 2, bx = PANEL_W - 14;
    int pending = PendingCount();
    bool resettable = !SavedRows().empty() || pending > 0;
    float bw = 196;
    bx -= bw;
    if (Button("RESET TO DEFAULTS", bx, by, bw, bh, resettable && !Locked() ? DANGER : DISABLED, ui::Id("reset"))) StageResetAll();
    bw = 110; bx -= bw + 10;
    if (Button("DISCARD", bx, by, bw, bh, pending > 0 && !Locked() ? NORMAL : DISABLED, ui::Id("discard"))) Discard();
    bw = 150; bx -= bw + 10;
    if (Button(pending > 0 ? "APPLY  " + std::to_string(pending) : "APPLY", bx, by, bw, bh, pending > 0 && !Locked() ? PRIMARY : DISABLED, ui::Id("apply"))) Apply();
}
} // namespace editorapp

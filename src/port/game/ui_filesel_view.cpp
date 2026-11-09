// zplayer (ZC_PORT): the file selector's window, drawn with Dear ImGui. It is
// the view of zcui_fsel::State (ui_filesel.h): it runs as a UI layer on the
// platform thread with the UI lock held, shows what the game side put in the
// state and queues requests; it never touches the file system.
//
//   [ title                                              x ]
//   [ ^ ]  C: > Users > me > Games                           breadcrumbs
//   +-----------+--------------------------------------------+
//   | Places    | Name                       Size  Modified  |
//   |  Game ... |  ..                                        |
//   | Drives    |  [] folder                                 |
//   |  C:  ...  |  [] quest.qst            1.2 MB  ...       |
//   +-----------+--------------------------------------------+
//   Path [ c:\...\quest.qst                                  ]
//   Type [ ZC Quests (*.qst) v ]                [Cancel] [OK]
//
// Mouse: click selects, double click opens. Keyboard: arrows, Page Up/Down,
// Home/End move (the selection follows), Enter opens, Backspace goes up,
// typing a name's first letters jumps to it, Esc cancels. Gamepad: d-pad
// moves, A opens, Y goes up, B cancels. Everything else is reachable with
// ImGui's keyboard/gamepad navigation (Tab, arrows).
//
// The window is modal: a full-screen dim window under it takes every click
// outside it, and it takes the focus back whenever it loses it.
//
// Written from scratch for zplayer.

#include "zcport/ui.h"
#include "ui_filesel.h"

#include <imgui.h>
#include <imgui_internal.h>     // BringWindowToDisplayFront, GetCurrentWindow

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace zcui_fsel;

namespace
{

// View-only state (platform thread, under the UI lock like everything here).
struct View
{
    unsigned serial;            // the opening this belongs to
    std::vector<char> path_buf; // the path field's text (UTF-8)
    unsigned path_serial;       // State::path_serial copied into path_buf
    bool path_active;           // the path field was being edited last frame
    bool path_focused;
    bool panel_focused;         // the window had the focus last frame
    unsigned list_serial;       // the listing the next fields belong to
    int last_focused;           // row with the keyboard focus last frame
    int focus_row;              // row to move the keyboard focus to, or -1
    bool nav_alive;             // some item of ours had the keyboard focus last frame
    std::string typeahead;
    double typeahead_time;

    View() : serial(0), path_serial(0), path_active(false), path_focused(false), panel_focused(false),
             list_serial(0), last_focused(-1), focus_row(-1), nav_alive(false), typeahead_time(0.0) {}
};

View g_view;

const ImU32 kFolder = IM_COL32(0xE8, 0xB4, 0x4C, 0xFF);
const ImU32 kFolderTab = IM_COL32(0xC9, 0x95, 0x33, 0xFF);
const ImU32 kFile = IM_COL32(0x9A, 0x9A, 0xA8, 0xFF);
const ImU32 kFileFold = IM_COL32(0x6C, 0x6C, 0x78, 0xFF);
const ImU32 kMatch = IM_COL32(0x6E, 0xA8, 0xFF, 0xFF);
const ImU32 kMatchFold = IM_COL32(0x3B, 0x6F, 0xD0, 0xFF);
const ImU32 kDrive = IM_COL32(0x9A, 0xA4, 0xB8, 0xFF);
const ImU32 kLed = IM_COL32(0x4A, 0xDE, 0x80, 0xFF);

void push(State &st, RequestKind kind, int index, const std::string &text, bool keys)
{
    Request r;
    r.kind = kind;
    r.index = index;
    r.text = text;
    r.dir = st.dir;
    r.keys = keys;
    st.requests.push_back(r);
    zcui::request_redraw();
}

std::string path_text()
{
    return g_view.path_buf.empty() ? std::string() : std::string(&g_view.path_buf[0]);
}

// ---- icons (drawn, so no icon font is needed) -------------------------------

void icon_folder(ImDrawList *dl, ImVec2 p, float sz)
{
    const float h = sz * 0.76f;
    const float y = p.y + (sz - h) * 0.5f;
    const float r = sz * 0.10f;
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + sz * 0.46f, y + h * 0.35f), kFolderTab, r, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(ImVec2(p.x, y + h * 0.20f), ImVec2(p.x + sz, y + h), kFolder, r);
}

void icon_file(ImDrawList *dl, ImVec2 p, float sz, bool match)
{
    const float w = sz * 0.72f;
    const float x = p.x + (sz - w) * 0.5f;
    const float y0 = p.y + sz * 0.04f, y1 = p.y + sz * 0.96f;
    const float f = w * 0.36f;
    ImVec2 pts[5] = { ImVec2(x, y0), ImVec2(x + w - f, y0), ImVec2(x + w, y0 + f), ImVec2(x + w, y1), ImVec2(x, y1) };
    dl->AddConvexPolyFilled(pts, 5, match ? kMatch : kFile);
    dl->AddTriangleFilled(ImVec2(x + w - f, y0), ImVec2(x + w - f, y0 + f), ImVec2(x + w, y0 + f), match ? kMatchFold : kFileFold);
}

void icon_up(ImDrawList *dl, ImVec2 p, float sz, ImU32 col)
{
    const float cx = p.x + sz * 0.5f;
    dl->AddTriangleFilled(ImVec2(cx, p.y + sz * 0.10f), ImVec2(p.x + sz * 0.88f, p.y + sz * 0.52f),
                          ImVec2(p.x + sz * 0.12f, p.y + sz * 0.52f), col);
    dl->AddRectFilled(ImVec2(cx - sz * 0.12f, p.y + sz * 0.50f), ImVec2(cx + sz * 0.12f, p.y + sz * 0.90f), col);
}

void icon_drive(ImDrawList *dl, ImVec2 p, float sz)
{
    const float h = sz * 0.54f;
    const float y = p.y + (sz - h) * 0.5f;
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + sz, y + h), kDrive, sz * 0.14f);
    dl->AddCircleFilled(ImVec2(p.x + sz * 0.78f, y + h * 0.5f), sz * 0.08f, kLed);
}

void chevron(ImDrawList *dl, ImVec2 c, float sz, ImU32 col)
{
    ImVec2 pts[3] = { ImVec2(c.x - sz * 0.25f, c.y - sz * 0.5f), ImVec2(c.x + sz * 0.25f, c.y), ImVec2(c.x - sz * 0.25f, c.y + sz * 0.5f) };
    dl->AddPolyline(pts, 3, col, ImDrawFlags_None, std::max(1.0f, sz * 0.16f));
}

int path_char_filter(ImGuiInputTextCallbackData *d)
{
#ifdef _WIN32
    if(d->EventChar == '/')
        d->EventChar = '\\';            // as the original edit field
#endif
    if(d->EventChar < 32)
        return 1;
    return 0;
}

bool starts_with_ci(const std::string &s, const std::string &prefix)
{
    if(prefix.size() > s.size())
        return false;
    for(size_t i = 0; i < prefix.size(); ++i)
        if(tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i]))
            return false;
    return true;
}

bool same_ci(const std::string &a, const std::string &b)
{
    return a.size() == b.size() && starts_with_ci(a, b);
}

void append_utf8(std::string &s, unsigned c)
{
    if(c < 0x80)
        s += (char)c;
    else if(c < 0x800)
    {
        s += (char)(0xC0 | (c >> 6));
        s += (char)(0x80 | (c & 0x3F));
    }
    else if(c < 0x10000)
    {
        s += (char)(0xE0 | (c >> 12));
        s += (char)(0x80 | ((c >> 6) & 0x3F));
        s += (char)(0x80 | (c & 0x3F));
    }
}

// ---- parts of the window ----------------------------------------------------

void draw_toolbar(State &st, float s)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    const float fh = ImGui::GetFrameHeight();
    ImGuiIO &io = ImGui::GetIO();

    // Up
    ImGui::BeginDisabled(st.at_root || st.loading);
    if(ImGui::Button("##up", ImVec2(fh, fh)))
        push(st, REQ_UP, 0, std::string(), io.NavVisible);
    ImGui::SetItemTooltip("Parent folder (Backspace)");
    {
        ImVec2 a = ImGui::GetItemRectMin();
        float isz = fh * 0.46f;
        icon_up(ImGui::GetWindowDrawList(), ImVec2(a.x + (fh - isz) * 0.5f, a.y + (fh - isz) * 0.5f), isz,
                ImGui::GetColorU32(ImGuiCol_Text));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    // Breadcrumbs: the last ones that fit.
    const float avail = ImGui::GetContentRegionAvail().x;
    const float sep_w = fh * 0.55f;
    std::vector<float> widths;
    float total = 0.0f;
    for(size_t i = 0; i < st.crumbs.size(); ++i)
    {
        widths.push_back(ImGui::CalcTextSize(st.crumbs[i].label.c_str()).x + style.FramePadding.x * 2.0f);
        total += widths.back() + (i ? sep_w : 0.0f);
    }
    const float ell_w = ImGui::CalcTextSize("...").x + sep_w + style.ItemInnerSpacing.x;
    size_t first = 0;
    while(first + 1 < st.crumbs.size() && total + (first ? ell_w : 0.0f) > avail)
    {
        total -= widths[first] + sep_w;
        ++first;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, style.ItemSpacing.y));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    if(first > 0)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("...");
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImVec2 c = ImGui::GetCursorScreenPos();
        chevron(dl, ImVec2(c.x + sep_w * 0.5f, c.y + fh * 0.5f), fh * 0.30f, dim);
        ImGui::Dummy(ImVec2(sep_w, fh));
        ImGui::SameLine();
    }
    for(size_t i = first; i < st.crumbs.size(); ++i)
    {
        const bool last = i + 1 == st.crumbs.size();
        ImGui::PushID((int)i);
        if(!last)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if(ImGui::Button(st.crumbs[i].label.c_str()) && !st.loading)
            push(st, REQ_GOTO, 0, st.crumbs[i].path, io.NavVisible);
        if(!last)
            ImGui::PopStyleColor();
        ImGui::PopID();
        if(!last)
        {
            ImGui::SameLine();
            ImVec2 c = ImGui::GetCursorScreenPos();
            chevron(dl, ImVec2(c.x + sep_w * 0.5f, c.y + fh * 0.5f), fh * 0.30f, dim);
            ImGui::Dummy(ImVec2(sep_w, fh));
            ImGui::SameLine();
        }
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    (void)s;
}

void place_item(State &st, const zcui_fs::Place &p, float s)
{
    ImGuiIO &io = ImGui::GetIO();
    const float line = ImGui::GetTextLineHeight();
    const float h = line + 10.0f * s;
    const float isz = line * 0.95f;
    const bool current = same_ci(st.dir, p.path);

    ImGui::PushID(p.path.c_str());
    ImVec2 a = ImGui::GetCursorScreenPos();
    if(ImGui::Selectable("##place", current, ImGuiSelectableFlags_None, ImVec2(0.0f, h)) && !st.loading)
        push(st, REQ_GOTO, 0, p.path, io.NavVisible);
    if(!p.detail.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", p.detail.c_str());
    const ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float x = a.x + 6.0f * s;
    const ImVec2 ip(x, a.y + (h - isz) * 0.5f);
    if(p.drive)
        icon_drive(dl, ip, isz);
    else
        icon_folder(dl, ip, isz);
    const float tx = x + isz + 8.0f * s;
    const float ty = a.y + (h - line) * 0.5f;
    dl->PushClipRect(ImVec2(tx, a.y), ImVec2(b.x - 4.0f * s, b.y), true);
    dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(ImGuiCol_Text), p.label.c_str());
    if(!p.detail.empty())
    {
        float lw = ImGui::CalcTextSize(p.label.c_str()).x;
        dl->AddText(ImVec2(tx + lw + 8.0f * s, ty), ImGui::GetColorU32(ImGuiCol_TextDisabled), p.detail.c_str());
    }
    dl->PopClipRect();
    ImGui::PopID();
}

void draw_places(State &st, float s)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted("Places");
    ImGui::PopStyleColor();
    for(size_t i = 0; i < st.places.size(); ++i)
        place_item(st, st.places[i], s);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted("Drives");
    ImGui::PopStyleColor();
    for(size_t i = 0; i < st.drives.size(); ++i)
        place_item(st, st.drives[i], s);
}

struct Columns
{
    float size_r;       // right edge of the size column (0 = no column)
    float time_x;       // left edge of the time column (0 = no column)
};

Columns columns_for(float w, float s)
{
    Columns c;
    const float time_w = ImGui::CalcTextSize("0000-00-00 00:00").x + 16.0f * s;
    const float size_w = ImGui::CalcTextSize("000.0 MB").x + 16.0f * s;
    c.time_x = w > 520.0f * s ? w - time_w : 0.0f;
    float right = c.time_x > 0.0f ? c.time_x : w;
    c.size_r = w > 330.0f * s ? right - 12.0f * s : 0.0f;
    (void)size_w;
    return c;
}

void draw_list(State &st, float s, float height, bool act_key, bool keys_ok)
{
    ImGuiIO &io = ImGui::GetIO();
    const ImGuiStyle &style = ImGui::GetStyle();
    const float line = ImGui::GetTextLineHeight();
    const float row_h = line + 10.0f * s;
    const float isz = line * 0.95f;
    const ImVec2 pad(6.0f * s, 4.0f * s);

    if(g_view.list_serial != st.list_serial)
    {
        g_view.list_serial = st.list_serial;
        g_view.last_focused = -1;
        g_view.focus_row = -1;
        g_view.typeahead.clear();
        if(st.focus_sel && !st.rows.empty())
            g_view.focus_row = st.sel >= 0 ? st.sel : 0;
        st.focus_sel = false;
    }

    // Column header, lined up with the rows below it.
    const float list_w = ImGui::GetContentRegionAvail().x;
    const float header_h = line + 8.0f * s;
    const float rows_h = height - header_h;
    const bool will_scroll = (float)st.rows.size() * row_h > rows_h - 2.0f * pad.y;
    const float content_w = list_w - 2.0f * pad.x - (will_scroll ? style.ScrollbarSize : 0.0f);
    const Columns col = columns_for(content_w, s);
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const float ty = a.y + (header_h - line) * 0.5f;
        const float x0 = a.x + pad.x;
        dl->AddRectFilled(a, ImVec2(a.x + list_w, a.y + header_h), ImGui::GetColorU32(ImGuiCol_TableHeaderBg),
                          style.ChildRounding, ImDrawFlags_RoundCornersTop);
        dl->AddText(ImVec2(x0 + 6.0f * s + isz + 8.0f * s, ty), dim, "Name");
        if(col.size_r > 0.0f)
            dl->AddText(ImVec2(x0 + col.size_r - ImGui::CalcTextSize("Size").x, ty), dim, "Size");
        if(col.time_x > 0.0f)
            dl->AddText(ImVec2(x0 + col.time_x, ty), dim, "Modified");
        ImGui::Dummy(ImVec2(list_w, header_h));
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - style.ItemSpacing.y);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    const bool shown = ImGui::BeginChild("##rows", ImVec2(list_w, rows_h),
                                         ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if(shown)
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const float w = ImGui::GetContentRegionAvail().x;
        const Columns c = columns_for(w, s);
        const int default_row = st.sel >= 0 ? st.sel : 0;
        bool opened = false;
        bool any_focused = false;

        if(st.loading || st.rows.empty() || (st.rows.size() == 1 && st.rows[0].kind == ROW_UP))
        {
            const char *msg = st.loading ? "Loading..." : st.unreadable ? "This folder cannot be read"
                              : "No folders or matching files here";
            ImVec2 ts = ImGui::CalcTextSize(msg);
            ImVec2 a = ImGui::GetWindowPos();
            ImVec2 sz = ImGui::GetWindowSize();
            float y = st.rows.empty() || st.loading ? a.y + (sz.y - ts.y) * 0.5f : a.y + pad.y + row_h * 2.0f;
            ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + (sz.x - ts.x) * 0.5f, y), dim, msg);
        }

        ImGui::BeginDisabled(st.loading);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 0.0f));
        ImGui::PushID((int)st.list_serial);
        ImGuiListClipper clipper;
        clipper.Begin((int)st.rows.size(), row_h);
        if(g_view.focus_row >= 0)
            clipper.IncludeItemByIndex(g_view.focus_row);
        if(st.scroll_to_sel && st.sel >= 0 && st.sel < (int)st.rows.size())
            clipper.IncludeItemByIndex(st.sel);
        while(clipper.Step())
        {
            for(int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const Row &r = st.rows[(size_t)i];
                if(g_view.focus_row == i)
                    ImGui::SetKeyboardFocusHere();
                ImGui::PushID(i);
                const ImVec2 a = ImGui::GetCursorScreenPos();
                const bool pressed = ImGui::Selectable("##row", st.sel == i, ImGuiSelectableFlags_AllowDoubleClick,
                                                       ImVec2(w, row_h));
                const bool focused = ImGui::IsItemFocused();
                any_focused = any_focused || focused;
                if(i == default_row)
                    ImGui::SetItemDefaultFocus();
                if(st.scroll_to_sel && i == st.sel)
                {
                    ImGui::SetScrollHereY(0.35f);
                    st.scroll_to_sel = false;
                }

                if(focused)
                {
                    if(g_view.focus_row == i)
                        g_view.focus_row = -1;      // moved there by us: not a selection
                    else if(g_view.last_focused != i && io.NavVisible && !pressed && st.sel != i)
                    {
                        st.sel = i;                 // moved there with the keys
                        push(st, REQ_SELECT, i, std::string(), true);
                    }
                    g_view.last_focused = i;
                }
                if(pressed && !st.loading)
                {
                    const bool dbl = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered();
                    if(st.sel != i)
                    {
                        st.sel = i;
                        push(st, REQ_SELECT, i, std::string(), false);
                    }
                    if(dbl && !opened)
                    {
                        push(st, REQ_OPEN, i, std::string(), false);
                        opened = true;
                    }
                }
                if(focused && act_key && keys_ok && !opened && !st.loading)
                {
                    st.sel = i;
                    push(st, REQ_OPEN, i, std::string(), true);
                    opened = true;
                }

                // contents
                const float x = a.x + 6.0f * s;
                const ImVec2 ip(x, a.y + (row_h - isz) * 0.5f);
                if(r.kind == ROW_UP)
                    icon_up(dl, ImVec2(ip.x + isz * 0.1f, ip.y + isz * 0.1f), isz * 0.8f, dim);
                else if(r.kind == ROW_DIR)
                    icon_folder(dl, ip, isz);
                else
                    icon_file(dl, ip, isz, r.highlight);
                const float tx = x + isz + 8.0f * s;
                const float ty = a.y + (row_h - line) * 0.5f;
                const float name_r = c.size_r > 0.0f ? a.x + c.size_r - ImGui::CalcTextSize("000.0 MB").x - 8.0f * s
                                     : a.x + w - 4.0f * s;
                dl->PushClipRect(ImVec2(tx, a.y), ImVec2(std::max(tx, name_r), a.y + row_h), true);
                dl->AddText(ImVec2(tx, ty), r.kind == ROW_UP ? dim : text, r.label.c_str());
                dl->PopClipRect();
                if(c.size_r > 0.0f && !r.size_text.empty())
                {
                    float sw = ImGui::CalcTextSize(r.size_text.c_str()).x;
                    dl->AddText(ImVec2(a.x + c.size_r - sw, ty), dim, r.size_text.c_str());
                }
                if(c.time_x > 0.0f && !r.time_text.empty())
                    dl->AddText(ImVec2(a.x + c.time_x, ty), dim, r.time_text.c_str());
                ImGui::PopID();
            }
        }
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::EndDisabled();

        // Enter while nothing has the keyboard focus (just opened, or the
        // focused row went with the last listing): open the selected row.
        if(act_key && keys_ok && !opened && !st.loading && st.sel >= 0 && st.sel < (int)st.rows.size() &&
           !any_focused && !g_view.nav_alive)
        {
            push(st, REQ_OPEN, st.sel, std::string(), true);
            opened = true;
        }
    }
    ImGui::EndChild();

    // Type to jump: the next row whose name starts with what was typed.
    if(keys_ok && !st.loading && !io.KeyCtrl && !io.KeyAlt && io.InputQueueCharacters.Size > 0)
    {
        const double now = ImGui::GetTime();
        if(now - g_view.typeahead_time > 1.0)
            g_view.typeahead.clear();
        bool changed = false;
        for(int i = 0; i < io.InputQueueCharacters.Size; ++i)
        {
            unsigned ch = io.InputQueueCharacters[i];
            if(ch < 32 || (ch == ' ' && g_view.typeahead.empty()))
                continue;
            append_utf8(g_view.typeahead, ch);
            changed = true;
        }
        if(changed)
        {
            g_view.typeahead_time = now;
            const int n = (int)st.rows.size();
            // a repeated single letter steps through the matches
            const bool step = g_view.typeahead.size() == 1 ||
                              (g_view.typeahead.size() > 1 &&
                               g_view.typeahead.find_first_not_of(g_view.typeahead[0]) == std::string::npos);
            const std::string key = step ? g_view.typeahead.substr(0, 1) : g_view.typeahead;
            const int start = st.sel < 0 ? 0 : (step ? st.sel + 1 : st.sel);
            for(int k = 0; k < n; ++k)
            {
                int i = (start + k) % n;
                if(st.rows[(size_t)i].kind != ROW_UP && starts_with_ci(st.rows[(size_t)i].label, key))
                {
                    if(st.sel != i)
                    {
                        st.sel = i;
                        push(st, REQ_SELECT, i, std::string(), true);
                    }
                    g_view.focus_row = i;
                    st.scroll_to_sel = true;
                    break;
                }
            }
        }
    }
}

void draw_footer(State &st, float s)
{
    ImGuiIO &io = ImGui::GetIO();
    const ImGuiStyle &style = ImGui::GetStyle();
    const float label_w = std::max(ImGui::CalcTextSize("Path").x, ImGui::CalcTextSize("Type").x) + style.ItemSpacing.x * 1.5f;

    // The path field: the original's edit field.
    if(g_view.path_serial != st.path_serial && !g_view.path_active)
    {
        g_view.path_serial = st.path_serial;
        size_t n = std::min(st.path_utf8.size(), g_view.path_buf.size() - 1);
        memcpy(&g_view.path_buf[0], st.path_utf8.data(), n);
        g_view.path_buf[n] = 0;
    }
    const float x0 = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Path");
    ImGui::SameLine(x0 + label_w);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool enter = ImGui::InputText("##path", &g_view.path_buf[0], g_view.path_buf.size(),
                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_ElideLeft |
                                        ImGuiInputTextFlags_CallbackCharFilter, path_char_filter);
    g_view.path_active = ImGui::IsItemActive();
    g_view.path_focused = ImGui::IsItemFocused();
    if(ImGui::IsItemEdited())
        push(st, REQ_EDIT, 0, path_text(), false);
    if(enter && !st.loading)
        push(st, REQ_ENTER, 0, path_text(), true);

    // Filter, Cancel, OK
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Type");
    ImGui::SameLine(x0 + label_w);
    const float bw = std::max(std::max(ImGui::CalcTextSize(st.ok_label.c_str()).x, ImGui::CalcTextSize(st.cancel_label.c_str()).x)
                              + style.FramePadding.x * 4.0f, 110.0f * s);
    const float buttons_w = bw * 2.0f + style.ItemSpacing.x;
    const float combo_w = std::max(120.0f * s, std::min(360.0f * s, ImGui::GetContentRegionAvail().x - buttons_w - 24.0f * s));
    if(st.mode == MODE_BROWSE && !st.filters.empty())
    {
        ImGui::SetNextItemWidth(combo_w);
        int cur = std::min(std::max(st.filter, 0), (int)st.filters.size() - 1);
        if(ImGui::BeginCombo("##type", st.filters[(size_t)cur].c_str()))
        {
            for(size_t i = 0; i < st.filters.size(); ++i)
            {
                const bool is_cur = (int)i == cur;
                if(ImGui::Selectable(st.filters[i].c_str(), is_cur) && !is_cur && !st.loading)
                {
                    st.filter = (int)i;
                    push(st, REQ_FILTER, (int)i, std::string(), io.NavVisible);
                }
                if(is_cur)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    else
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", st.filter_text.c_str());
    }

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - buttons_w));
    if(ImGui::Button(st.cancel_label.c_str(), ImVec2(bw, 0.0f)))
        push(st, REQ_CANCEL, 0, path_text(), io.NavVisible);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::BeginDisabled(st.loading);
    if(ImGui::Button(st.ok_label.c_str(), ImVec2(bw, 0.0f)))
        push(st, REQ_OK, 0, path_text(), io.NavVisible);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(st.mode == MODE_DIR
                           ? "Enter / A: open folder  \xC2\xB7  Backspace / Y: up  \xC2\xB7  OK: use this path  \xC2\xB7  Esc / B: cancel"
                           : "Enter / A: open  \xC2\xB7  Backspace / Y: up  \xC2\xB7  type to jump  \xC2\xB7  Esc / B: cancel");
    ImGui::PopStyleColor();
}

} // namespace

namespace zcui_fsel
{

void view_layer(void *)
{
    State &st = state();
    if(!st.active)
        return;

    ImGuiIO &io = ImGui::GetIO();
    const ImGuiStyle &style = ImGui::GetStyle();
    const float s = zcui::scale();
    const ImVec2 disp = io.DisplaySize;
    if(disp.x < 64.0f || disp.y < 64.0f)
        return;

    const bool appearing = g_view.serial != st.serial;
    if(appearing)
    {
        g_view = View();
        g_view.serial = st.serial;
        g_view.path_buf.assign(std::max<size_t>(st.path_capacity, 64), 0);
        g_view.path_serial = 0;         // copy st.path_utf8 in
    }

    // The dim: covers the game and any other window, takes their clicks.
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(disp);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##zc_fsel_dim", NULL,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
    if(appearing)
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // The browser.
    const float w = std::min(1040.0f * s, disp.x - 48.0f * s);
    const float h = std::min(700.0f * s, disp.y - 48.0f * s);
    ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::max(w, 320.0f), std::max(h, 240.0f)), ImGuiCond_Always);
    if(appearing || !g_view.panel_focused)
        ImGui::SetNextWindowFocus();
    bool open = true;
    const std::string title = st.title + "###zc_fsel";
    ImGui::Begin(title.c_str(), &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const bool was_focused = g_view.panel_focused;
    g_view.panel_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Keys for the whole window (not while the path field is being edited or
    // a drop-down is open; not on the first frame).
    const bool popup_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const bool keys_ok = was_focused && g_view.panel_focused && !g_view.path_active && !popup_open;
    const ImGuiKey pad_activate = io.ConfigNavSwapGamepadButtons ? ImGuiKey_GamepadFaceRight : ImGuiKey_GamepadFaceDown;
    const ImGuiKey pad_cancel = io.ConfigNavSwapGamepadButtons ? ImGuiKey_GamepadFaceDown : ImGuiKey_GamepadFaceRight;
    const bool act_key = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
                         ImGui::IsKeyPressed(pad_activate, false);
    if(!open)
        push(st, REQ_CANCEL, 0, path_text(), false);
    else if(keys_ok)
    {
        if(ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(pad_cancel, false))
            push(st, REQ_CANCEL, 0, path_text(), true);
        else if(!st.loading && !st.at_root &&
                (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) ||
                 (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceUp, false) && !g_view.path_focused)))
            push(st, REQ_UP, 0, std::string(), true);
    }

    draw_toolbar(st, s);

    const float fh = ImGui::GetFrameHeight();
    const float footer_h = fh * 2.0f + ImGui::GetTextLineHeight() + style.ItemSpacing.y * 3.0f;
    const float main_h = std::max(80.0f * s, ImGui::GetContentRegionAvail().y - footer_h);
    const float side_w = std::min(230.0f * s, ImGui::GetContentRegionAvail().x * 0.3f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * s, 8.0f * s));
    if(ImGui::BeginChild("##places", ImVec2(side_w, main_h), ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened))
        draw_places(st, s);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SameLine();
    ImGui::BeginGroup();
    draw_list(st, s, main_h, act_key, keys_ok);
    ImGui::EndGroup();

    draw_footer(st, s);
    {
        ImGuiContext &g = *GImGui;
        g_view.nav_alive = g.NavId != 0 && g.NavIdIsAlive && g.NavWindow &&
                           g.NavWindow->RootWindow == ImGui::GetCurrentWindow()->RootWindow;
    }
    ImGui::End();

    // Redraw while waiting for the game thread's answer.
    if(!st.requests.empty() || st.loading)
        zcui::request_redraw();
}

} // namespace zcui_fsel

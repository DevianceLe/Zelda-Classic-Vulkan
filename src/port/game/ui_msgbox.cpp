// zplayer (ZC_PORT): the boxes the player drew with jwin outside its dialogs,
// drawn by Dear ImGui over the game picture (see ui_game.h):
//
//  - the progress message box of src/zsys.cpp (box_start ... box_end): a
//    card with a title and the lines written so far, the newest at the
//    bottom; lines wrap at the card's width;
//  - the "Press a key" box of the key and button settings (src/zc_sys.cpp).
//
// Neither takes input: the game keeps reading the keyboard, mouse and
// joystick itself, exactly as before (box_pause() and the key settings wait
// for keys in their own loops). So both are drawn on ImGui's foreground draw
// list instead of being windows: they never take the focus from a dialog
// and always show on top. Each change asks the platform for a new UI frame.
//
// Written from scratch for zplayer.

#include "zcport/ui.h"
#include "ui_game.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

namespace
{

struct MsgBox
{
    bool active;
    bool waiting;                       // "-- press a key --" (box_pause, box_end(true))
    size_t mark;                        // length of the last line when waiting started
    std::string title;                  // UTF-8, may be empty
    std::vector<std::string> lines;     // UTF-8; the last one is being written
    MsgBox() : active(false), waiting(false), mark(0) {}
};

struct Prompt
{
    bool active;
    std::vector<std::string> lines;     // UTF-8
    Prompt() : active(false) {}
};

// Shared with the layer: guarded by the UI lock.
MsgBox g_box;
Prompt g_prompt;

// Lines kept for the box: far more than it can show.
const size_t kMaxLines = 400;

ImU32 with_alpha(ImU32 c, float a)
{
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w *= a;
    return ImGui::ColorConvertFloat4ToU32(v);
}

// A rounded card with a soft shadow, in the window colours of the UI style.
void draw_card(ImDrawList *dl, const ImVec2 &a, const ImVec2 &b, float s)
{
    const ImGuiStyle &st = ImGui::GetStyle();
    const float r = st.WindowRounding;
    for(int i = 0; i < 5; ++i)
    {
        float g = (float)(i + 1) * 3.0f * s;
        dl->AddRectFilled(ImVec2(a.x - g, a.y - g + 6.0f * s), ImVec2(b.x + g, b.y + g + 6.0f * s),
                          IM_COL32(0, 0, 0, 22 - i * 4), r + g);
    }
    dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_WindowBg), r);
    dl->AddRect(a, b, ImGui::GetColorU32(ImGuiCol_Border), r, 0, 1.0f);
}

void draw_msgbox(ImDrawList *dl, float s)
{
    const ImGuiStyle &st = ImGui::GetStyle();
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImFont *font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float line_h = fs + 2.0f * s;

    const float w = std::min(600.0f * s, disp.x - 32.0f * s);
    const float h = std::min(330.0f * s, disp.y - 32.0f * s);
    if(w < 64.0f * s || h < 64.0f * s)
        return;
    const ImVec2 a(std::floor((disp.x - w) * 0.5f), std::floor((disp.y - h) * 0.5f));
    const ImVec2 b(a.x + w, a.y + h);
    draw_card(dl, a, b, s);

    const ImVec2 pad = st.WindowPadding;
    float top = a.y;

    // Title bar
    if(!g_box.title.empty())
    {
        const float tfs = fs * 1.08f;
        const float bar_h = tfs + 2.0f * st.FramePadding.y + 6.0f * s;
        dl->AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, a.y + bar_h),
                          ImGui::GetColorU32(ImGuiCol_TitleBgActive), st.WindowRounding, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(a.x + 1, a.y + bar_h), ImVec2(b.x - 1, a.y + bar_h), ImGui::GetColorU32(ImGuiCol_Border));
        ImVec2 ts = font->CalcTextSizeA(tfs, FLT_MAX, 0.0f, g_box.title.c_str());
        dl->AddText(font, tfs, ImVec2(std::floor(a.x + (w - ts.x) * 0.5f), std::floor(a.y + (bar_h - ts.y) * 0.5f)),
                    ImGui::GetColorU32(ImGuiCol_Text), g_box.title.c_str());
        top = a.y + bar_h;
    }

    // Body: as many of the newest lines as fit, oldest first. While waiting
    // for a key, what was written since (the prompt) is a line of its own
    // that pulses; box_pause() takes it away again afterwards.
    std::vector<std::string> shown(g_box.lines);
    std::string prompt;
    if(g_box.waiting && !shown.empty())
    {
        std::string &last = shown.back();
        size_t m = std::min(g_box.mark, last.size());
        prompt = last.substr(m);
        last.resize(m);
    }
    if(!shown.empty() && shown.back().empty())
        shown.pop_back();               // the empty line being started
    if(!prompt.empty())
        shown.push_back(prompt);

    const float wrap = w - 2.0f * pad.x;
    const ImVec2 body_a(a.x + pad.x, top + pad.y);
    const float body_h = b.y - pad.y - body_a.y;
    const size_t n = shown.size();
    std::vector<float> heights(n, 0.0f);
    size_t first = n;
    float used = 0.0f;
    while(first > 0)
    {
        const std::string &l = shown[first - 1];
        float lh = l.empty() ? line_h
                   : font->CalcTextSizeA(fs, FLT_MAX, wrap, l.c_str()).y + (line_h - fs);
        if(used + lh > body_h && first < n)
            break;
        used += lh;
        heights[first - 1] = lh;
        --first;
    }

    const ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 accent = ImGui::GetColorU32(ImGuiCol_HeaderActive);
    dl->PushClipRect(ImVec2(a.x + 1, top), ImVec2(b.x - 1, b.y - 1), true);
    float y = body_a.y;
    for(size_t i = first; i < n; ++i)
    {
        ImU32 col = text_col;
        if(!prompt.empty() && i == n - 1)
        {
            float t = (float)ImGui::GetTime();
            col = with_alpha(accent, 0.65f + 0.35f * std::sin(t * 4.0f));
        }
        if(!shown[i].empty())
            dl->AddText(font, fs, ImVec2(body_a.x, y), col, shown[i].c_str(), NULL, wrap);
        y += heights[i];
    }
    dl->PopClipRect();

    if(g_box.waiting)
        zcui::request_redraw();         // keep the pulse going
}

void draw_prompt(ImDrawList *dl, float s)
{
    const ImGuiStyle &st = ImGui::GetStyle();
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImFont *font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float big = fs * 1.15f;
    const float gap = 4.0f * s;

    float tw = 0.0f, th = 0.0f;
    std::vector<ImVec2> sizes;
    for(size_t i = 0; i < g_prompt.lines.size(); ++i)
    {
        ImVec2 ts = font->CalcTextSizeA(i == 0 ? big : fs, FLT_MAX, 0.0f, g_prompt.lines[i].c_str());
        sizes.push_back(ts);
        tw = std::max(tw, ts.x);
        th += ts.y + (i ? gap : 0.0f);
    }
    const float w = std::min(std::max(tw + 4.0f * st.WindowPadding.x, 300.0f * s), disp.x - 16.0f * s);
    const float h = th + 3.2f * st.WindowPadding.y;
    const ImVec2 a(std::floor((disp.x - w) * 0.5f), std::floor((disp.y - h) * 0.5f));
    const ImVec2 b(a.x + w, a.y + h);
    draw_card(dl, a, b, s);
    dl->AddRectFilled(ImVec2(a.x + st.WindowRounding, a.y), ImVec2(b.x - st.WindowRounding, a.y + 3.0f * s),
                      ImGui::GetColorU32(ImGuiCol_HeaderActive));

    float y = a.y + (h - th) * 0.5f;
    for(size_t i = 0; i < g_prompt.lines.size(); ++i)
    {
        ImU32 col = ImGui::GetColorU32(i == 0 ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        dl->AddText(font, i == 0 ? big : fs, ImVec2(std::floor(a.x + (w - sizes[i].x) * 0.5f), std::floor(y)),
                    col, g_prompt.lines[i].c_str());
        y += sizes[i].y + gap;
    }
}

void boxes_layer(void *)
{
    if(!g_box.active && !g_prompt.active)
        return;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    const float s = zcui::scale();
    if(g_box.active)
        draw_msgbox(dl, s);
    if(g_prompt.active)
        draw_prompt(dl, s);
}

// Game thread only.
void ensure_layer()
{
    static bool added = false;
    if(!added)
    {
        zcui::add_layer(boxes_layer, NULL, 950);
        added = true;
    }
}

} // namespace

namespace zcui_game
{

void msgbox_start(const char *title)
{
    ensure_layer();
    std::string t = title ? zcui_fs::native_to_utf8(title) : std::string();
    {
        zcui::Lock lock;
        g_box = MsgBox();
        g_box.active = true;
        g_box.title = t;
        g_box.lines.push_back(std::string());
    }
    zcui::request_redraw();
}

void msgbox_text(const char *text)
{
    if(!text || !*text)
        return;
    std::string t = zcui_fs::native_to_utf8(text);
    {
        zcui::Lock lock;
        if(!g_box.active)
            return;
        g_box.lines.back() += t;
    }
    zcui::request_redraw();
}

void msgbox_eol()
{
    {
        zcui::Lock lock;
        if(!g_box.active)
            return;
        g_box.lines.push_back(std::string());
        if(g_box.lines.size() > kMaxLines)
            g_box.lines.erase(g_box.lines.begin(), g_box.lines.begin() + (g_box.lines.size() - kMaxLines));
    }
    zcui::request_redraw();
}

void msgbox_wait(bool waiting)
{
    {
        zcui::Lock lock;
        if(!g_box.active)
            return;
        if(waiting && !g_box.waiting)
            g_box.mark = g_box.lines.back().size();
        else if(!waiting && g_box.waiting)
            g_box.lines.back().resize(std::min(g_box.mark, g_box.lines.back().size()));
        g_box.waiting = waiting;
    }
    zcui::request_redraw();
}

void msgbox_end()
{
    {
        zcui::Lock lock;
        g_box = MsgBox();
    }
    zcui::request_redraw();
}

void prompt_show(const char *const *lines, int count)
{
    ensure_layer();
    std::vector<std::string> l;
    for(int i = 0; i < count && i < 4; ++i)
        l.push_back(zcui_fs::native_to_utf8(lines[i]));
    {
        zcui::Lock lock;
        g_prompt.active = true;
        g_prompt.lines = l;
    }
    zcui::request_redraw();
}

void prompt_hide()
{
    {
        zcui::Lock lock;
        g_prompt = Prompt();
    }
    zcui::request_redraw();
}

} // namespace zcui_game

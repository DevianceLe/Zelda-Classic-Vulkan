// zplayer: Settings > Video Settings... (Dear ImGui) and the frame-rate
// overlay, on top of the platform's display settings (zcport/display.h).
//
//   - The settings window is a layer (platform thread, UI lock held) shown
//     during a modal UI session that zplayer_video_settings() holds open on
//     the game thread. Every change applies at once; a change of display
//     mode, size/resolution or monitor asks "Keep these settings?" and goes
//     back to the previous ones after 10 s without an answer.
//   - The config (zcport/video_settings.h) is read on the game thread when
//     the platform opens the first graphics mode (the loader) and written by
//     zplayer_video_save_config() from save_game_configs().
//
// The game's logical screen and graphics mode are never changed here.
//
// Written from scratch for zplayer.

#include <allegro.h>

#include "zcport/display.h"
#include "zcport/ui.h"
#include "zcport/video_settings.h"
#if defined(__SWITCH__)
extern bool close_button_quit;   // src/zelda.cpp
#endif

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The game's "fullscreen" flag (src/zelda.cpp), saved by save_game_configs().
extern int fullscreen;

#ifdef ZC_ZQEDITOR
extern bool is_large;    // the layout in use
void zq_switch_layout(bool large);
#endif

using zcdisplay::Settings;

namespace
{

typedef std::chrono::steady_clock Clock;

// ZQuest shares this window; it keeps its settings in zquest.cfg.
#ifdef ZC_ZQEDITOR
const char *const kSection = "zqeditor";
const char *const kGameSection = "zquest";
#define VIDEO_CONFIG_FILE "zquest.cfg"
#else
const char *const kSection = "zplayer";
const char *const kGameSection = "zeldadx";
#define VIDEO_CONFIG_FILE "zc.cfg"
#endif
const int kConfirmSeconds = 10;
const float kLabelWidth = 120.0f;   // settings table columns (at UI scale 1)
const float kValueWidth = 320.0f;

#define TIMES "\xC3\x97"   // U+00D7 (UTF-8), in every font the UI uses
#define DOT "\xC2\xB7"     // U+00B7

// ---------------------------------------------------------------------------
// config (game thread)
// ---------------------------------------------------------------------------

const char *const kScalingKeys[] = { "integer", "fit", "smooth", "stretch" };

bool same_text(const char *a, const char *b)
{
    if (!a || !b)
        return false;
    for (; *a && *b; ++a, ++b)
    {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y)
            return false;
    }
    return *a == *b;
}

#ifdef ZC_ZQEDITOR
int g_layout_request = -1;      // 0 Large, 1 Small: for the game thread to apply
#endif

void fps_layer(void *);
void start_test_hook();   // TEMP TEST HOOK

// The platform's settings loader: at the first set_gfx_mode(), game thread.
void load_settings(Settings *s)
{
    s->fullscreen_mode = same_text(get_config_string(kSection, "fullscreen_mode", "borderless"), "exclusive")
                         ? zcdisplay::MODE_EXCLUSIVE : zcdisplay::MODE_BORDERLESS;
    s->window_w = get_config_int(kSection, "window_width", 0);
    s->window_h = get_config_int(kSection, "window_height", 0);
#ifdef ZC_ZQEDITOR
    // A window the size of the small layout's 320 x 240 screen is too little.
    if (!is_large && (s->window_w <= 0 || s->window_h <= 0))
    {
        s->window_w = 960;
        s->window_h = 720;
    }
#endif
    s->exclusive_w = get_config_int(kSection, "fullscreen_width", 0);
    s->exclusive_h = get_config_int(kSection, "fullscreen_height", 0);
    s->exclusive_hz = get_config_float(kSection, "fullscreen_refresh", 0.0f);
    s->display = get_config_int(kGameSection, "window_display", 0);
    const char *sc = get_config_string(kSection, "scaling", "fit");
    s->scaling = zcdisplay::SCALING_FIT;
    for (int i = 0; i < 4; i++)
        if (same_text(sc, kScalingKeys[i]))
            s->scaling = i;
    s->vsync = get_config_int(kSection, "vsync", 0) != 0;
    s->show_fps = get_config_int(kSection, "show_fps", 0) != 0;
    s->ui_scale = get_config_int(kSection, "ui_scale", 100) / 100.0f;
    const char *tc = get_config_string(kSection, "touch_controls", "auto");
    s->touch_mode = same_text(tc, "always") ? zcdisplay::TOUCH_ALWAYS
                  : same_text(tc, "never") ? zcdisplay::TOUCH_NEVER : zcdisplay::TOUCH_AUTO;
    s->touch_size = get_config_int(kSection, "touch_size", 100) / 100.0f;
    s->touch_opacity = get_config_int(kSection, "touch_opacity", 60) / 100.0f;

    static bool overlay = false;
    if (!overlay)
    {
        zcui::add_layer(fps_layer, NULL, 900);
        overlay = true;
    }
    start_test_hook();   // TEMP TEST HOOK
}

struct RegisterLoader
{
    RegisterLoader() { zcdisplay::set_settings_loader(load_settings); }
};
RegisterLoader g_register_loader;

// ---------------------------------------------------------------------------
// the frame-rate overlay (platform thread)
// ---------------------------------------------------------------------------

void fps_layer(void *)
{
    if (!zcdisplay::get().show_fps)
        return;
    const float sc = zcui::scale();
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 8 * sc, vp->WorkPos.y + 8 * sc),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * sc, 4 * sc));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6 * sc);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##zplayer_fps", NULL, flags))
    {
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("%.0f fps", fps);
        ImGui::SameLine();
        ImGui::TextDisabled("%.1f ms", fps > 0.0f ? 1000.0f / fps : 0.0f);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ---------------------------------------------------------------------------
// the settings window (platform thread, UI lock held)
// ---------------------------------------------------------------------------

struct VideoUi
{
    bool open;               // the window is up (zplayer_video_settings runs)
    bool close;              // the player closed it
    bool focus;              // focus it on the next frame
    bool confirming;         // "Keep these settings?" is up
    bool want_popup;
    bool popup_last_frame;   // a popup was open last frame (Esc closed it)
    Clock::time_point deadline;
    Settings revert_to;
    unsigned hotkeys;        // Alt+Enter count when the question came up

    // lists (refreshed when monitors change)
    unsigned monitors_serial;
    std::vector<zcdisplay::Monitor> monitors;
    int modes_for;
    std::vector<zcdisplay::DisplayMode> modes;

    VideoUi()
        : open(false), close(false), focus(false), confirming(false), want_popup(false),
          popup_last_frame(false), hotkeys(0), monitors_serial(0), modes_for(-1)
    {
        revert_to = zcdisplay::defaults();
    }
};

VideoUi g_ui;

// Display mode, size/resolution and monitor: the part that asks for
// confirmation.
bool window_part_differs(const Settings &a, const Settings &b)
{
    if (a.mode != b.mode || a.display != b.display)
        return true;
    if (a.mode == zcdisplay::MODE_WINDOWED && (a.window_w != b.window_w || a.window_h != b.window_h))
        return true;
    if (a.mode == zcdisplay::MODE_EXCLUSIVE &&
        (a.exclusive_w != b.exclusive_w || a.exclusive_h != b.exclusive_h ||
         std::fabs(a.exclusive_hz - b.exclusive_hz) >= 0.01f))
        return true;
    return false;
}

// 'now' with the window part of 'old'.
Settings with_window_part(const Settings &now, const Settings &old)
{
    Settings r = now;
    r.mode = old.mode;
    r.fullscreen_mode = old.fullscreen_mode;
    r.display = old.display;
    r.window_w = old.window_w;
    r.window_h = old.window_h;
    r.exclusive_w = old.exclusive_w;
    r.exclusive_h = old.exclusive_h;
    r.exclusive_hz = old.exclusive_hz;
    return r;
}

void tlog(const char *fmt, ...); // TEMP
void commit(VideoUi &u, const Settings &cur, const Settings &n)
{
    tlog("commit cur.mode=%d n.mode=%d confirming=%d differs=%d", cur.mode, n.mode, u.confirming, window_part_differs(n, cur)); // TEMP
    if (window_part_differs(n, cur))
    {
        if (!u.confirming)
        {
            u.revert_to = cur;
            u.confirming = true;
            u.want_popup = true;
            u.hotkeys = zcdisplay::hotkey_toggles();
        }
        u.deadline = Clock::now() + std::chrono::seconds(kConfirmSeconds);
    }
    zcdisplay::set(n);
}

void revert(VideoUi &u)
{
    tlog("revert to mode=%d win=%dx%d (now mode=%d)", u.revert_to.mode, u.revert_to.window_w, u.revert_to.window_h, zcdisplay::get().mode); // TEMP
    u.confirming = false;
    zcdisplay::set(with_window_part(zcdisplay::get(), u.revert_to));
}

std::string hz_text(float hz)
{
    char buf[32];
    if (std::fabs(hz - std::floor(hz + 0.5f)) < 0.05f)
        snprintf(buf, sizeof(buf), "%.0f Hz", hz);
    else
        snprintf(buf, sizeof(buf), "%.2f Hz", hz);
    return buf;
}

std::string scale_text(double f)
{
    char buf[32];
    if (std::fabs(f - std::floor(f + 0.5)) < 0.01)
        snprintf(buf, sizeof(buf), "%.0f" TIMES, f);
    else
        snprintf(buf, sizeof(buf), "%.2g" TIMES, f);
    return buf;
}

// Where a monitor is among the others ("left", "right", "top" ...).
const char *where(const zcdisplay::Monitor &m, const std::vector<zcdisplay::Monitor> &all)
{
    if (all.size() < 2)
        return "";
    int minx = 0, maxx = 0, miny = 0, maxy = 0;
    for (size_t i = 0; i < all.size(); i++)
    {
        int cx = all[i].x + all[i].w / 2, cy = all[i].y + all[i].h / 2;
        if (i == 0 || cx < minx) minx = cx;
        if (i == 0 || cx > maxx) maxx = cx;
        if (i == 0 || cy < miny) miny = cy;
        if (i == 0 || cy > maxy) maxy = cy;
    }
    int cx = m.x + m.w / 2, cy = m.y + m.h / 2;
    if (maxx - minx >= maxy - miny)
    {
        double t = (double)(cx - minx) / (maxx - minx);
        return t < 0.2 ? "left" : t > 0.8 ? "right" : "middle";
    }
    double t = (double)(cy - miny) / (maxy - miny);
    return t < 0.2 ? "top" : t > 0.8 ? "bottom" : "middle";
}

std::string monitor_text(const zcdisplay::Monitor &m, const std::vector<zcdisplay::Monitor> &all)
{
    char buf[256];
    const char *w = where(m, all);
    snprintf(buf, sizeof(buf), "%d. %s  " DOT "  %d " TIMES " %d%s%s%s",
             m.index, m.name.empty() ? "Display" : m.name.c_str(), m.desktop_w, m.desktop_h,
             *w ? ", " : "", w, m.primary ? ", primary" : "");
    return buf;
}

// A row of the settings table: label on the left, the widget fills the right.
void row(const char *label)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// A combo of 'items'; returns true when the player picked another one.
bool combo(const char *id, int *current, const std::vector<std::string> &items,
           const std::vector<std::string> *tips = NULL)
{
    const char *preview = (*current >= 0 && *current < (int)items.size()) ? items[*current].c_str() : "";
    bool changed = false;
    if (ImGui::BeginCombo(id, preview))
    {
        for (int i = 0; i < (int)items.size(); i++)
        {
            ImGui::PushID(i);
            const bool sel = i == *current;
            if (ImGui::Selectable(items[i].c_str(), sel) && !sel)
            {
                *current = i;
                changed = true;
            }
            if (tips && i < (int)tips->size() && !(*tips)[i].empty())
                ImGui::SetItemTooltip("%s", (*tips)[i].c_str());
            if (sel)
                ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void refresh_lists(VideoUi &u, int monitor)
{
    unsigned serial = zcdisplay::monitors_serial();
    if (u.monitors.empty() || serial != u.monitors_serial)
    {
        u.monitors = zcdisplay::monitors();
        u.monitors_serial = serial;
        u.modes_for = -1;
    }
    if (u.modes_for != monitor)
    {
        u.modes = zcdisplay::fullscreen_modes(monitor);
        u.modes_for = monitor;
    }
}

void display_section(VideoUi &u, const Settings &cur, float sc)
{
    // The monitor the settings are about: the chosen one, else where the
    // window is.
    int win_mon = zcdisplay::window_monitor();
    int mon = cur.display > 0 ? cur.display : win_mon;
    refresh_lists(u, mon);
    const zcdisplay::Monitor *m = NULL;
    for (size_t i = 0; i < u.monitors.size(); i++)
        if (u.monitors[i].index == mon)
            m = &u.monitors[i];
    if (!m && !u.monitors.empty())
    {
        m = &u.monitors[0];
        mon = m->index;
    }

    ImGui::SeparatorText("Display");
    if (!ImGui::BeginTable("##display", 2, ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, kLabelWidth * sc);
    ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthFixed, kValueWidth * sc);

    // display mode
    {
        row("Display mode");
        std::vector<std::string> items;
        items.push_back("Windowed");
        items.push_back("Borderless fullscreen");
        items.push_back("Exclusive fullscreen");
        std::vector<std::string> tips;
        tips.push_back("A window on the desktop; drag its edges for any size.");
        tips.push_back("Covers the monitor at its desktop resolution. Switches instantly.");
        tips.push_back("Sets the monitor to a resolution and refresh rate of its own.");
        int v = cur.mode;
        if (combo("##mode", &v, items, &tips))
        {
            Settings n = cur;
            n.mode = v;
            if (v != zcdisplay::MODE_WINDOWED)
                n.fullscreen_mode = v;
            commit(u, cur, n);
        }
    }

    // monitor
    {
        row("Monitor");
        std::vector<std::string> items;
        int v = -1;
        for (size_t i = 0; i < u.monitors.size(); i++)
        {
            items.push_back(monitor_text(u.monitors[i], u.monitors));
            if (u.monitors[i].index == mon)
                v = (int)i;
        }
        if (combo("##monitor", &v, items) && v >= 0)
        {
            Settings n = cur;
            n.display = u.monitors[v].index;
            commit(u, cur, n);
        }
        if (m)
            ImGui::SetItemTooltip("Counted from the left, as window_display in " VIDEO_CONFIG_FILE ".\n"
                                  "At %d, %d on the desktop.", m->x, m->y);
    }

    int lw = 0, lh = 0;
    zcdisplay::logical_size(&lw, &lh);
    if (lw <= 0 || lh <= 0)
    {
        lw = 640;
        lh = 480;
    }

    if (cur.mode == zcdisplay::MODE_WINDOWED)
    {
        // window size: the sizes offered that fit the monitor, plus the
        // current size when it is none of them. Multiples of 320 x 240,
        // or for ZQuest's 800 x 600 screen the usual sizes of that shape.
        row("Window size");
        std::vector<std::string> items;
        std::vector<int> ws, hs;
        int cw = cur.window_w > 0 ? cur.window_w : lw;
        int ch = cur.window_h > 0 ? cur.window_h : lh;
        int v = -1;
        static const int kLargeSizes[][2] = { { 800, 600 }, { 1024, 768 }, { 1200, 900 }, { 1280, 960 },
                                              { 1440, 1080 }, { 1600, 1200 }, { 1920, 1440 }, { 2400, 1800 },
                                              { 2880, 2160 } };
        const bool large = lw == 800 && lh == 600;
        const int nsizes = large ? (int)(sizeof(kLargeSizes) / sizeof(kLargeSizes[0])) : 32;
        for (int k = 0; k < nsizes; k++)
        {
            int w = large ? kLargeSizes[k][0] : 320 * (k + 1);
            int h = large ? kLargeSizes[k][1] : 240 * (k + 1);
            if (m && (w > m->max_client_w || h > m->max_client_h) && k > 0)
                break;
            char buf[96];
            snprintf(buf, sizeof(buf), "%d " TIMES " %d   (%s)", w, h, scale_text((double)w / lw).c_str());
            if (w == cw && h == ch)
                v = (int)items.size();
            items.push_back(buf);
            ws.push_back(w);
            hs.push_back(h);
        }
        if (v < 0)
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "%d " TIMES " %d   (custom)", cw, ch);
            v = (int)items.size();
            items.push_back(buf);
            ws.push_back(cw);
            hs.push_back(ch);
        }
        if (combo("##winsize", &v, items))
        {
            Settings n = cur;
            n.window_w = ws[v];
            n.window_h = hs[v];
            commit(u, cur, n);
        }
    }
    else if (cur.mode == zcdisplay::MODE_BORDERLESS)
    {
        row("Resolution");
        if (m)
            ImGui::TextDisabled("%d " TIMES " %d at %s (the desktop's)", m->desktop_w, m->desktop_h,
                                hz_text(m->desktop_hz).c_str());
    }
    else
    {
        // exclusive: the monitor's modes, size and refresh rate apart
        int cw = cur.exclusive_w, ch = cur.exclusive_h;
        float chz = cur.exclusive_hz;
        if ((cw <= 0 || ch <= 0) && m)
        {
            cw = m->desktop_w;
            ch = m->desktop_h;
            chz = m->desktop_hz;
        }
        std::vector<std::string> items;
        std::vector<int> ws, hs;
        int v = -1;
        for (size_t i = 0; i < u.modes.size(); i++)
        {
            bool dup = false;
            for (size_t j = 0; j < ws.size() && !dup; j++)
                dup = ws[j] == u.modes[i].w && hs[j] == u.modes[i].h;
            if (dup)
                continue;
            char buf[64];
            snprintf(buf, sizeof(buf), "%d " TIMES " %d", u.modes[i].w, u.modes[i].h);
            if (u.modes[i].w == cw && u.modes[i].h == ch)
                v = (int)items.size();
            items.push_back(buf);
            ws.push_back(u.modes[i].w);
            hs.push_back(u.modes[i].h);
        }
        row("Resolution");
        if (combo("##fsres", &v, items) && v >= 0)
        {
            Settings n = cur;
            n.exclusive_w = ws[v];
            n.exclusive_h = hs[v];
            // the same rate if that size has it, else its highest
            float best = 0.0f;
            bool same = false;
            for (size_t i = 0; i < u.modes.size(); i++)
            {
                if (u.modes[i].w != ws[v] || u.modes[i].h != hs[v])
                    continue;
                if (std::fabs(u.modes[i].hz - chz) < 0.01f)
                    same = true;
                if (u.modes[i].hz > best)
                    best = u.modes[i].hz;
            }
            n.exclusive_hz = same ? chz : best;
            commit(u, cur, n);
        }

        row("Refresh rate");
        std::vector<std::string> rates;
        std::vector<float> hzs;
        int r = -1;
        for (size_t i = 0; i < u.modes.size(); i++)
        {
            if (u.modes[i].w != cw || u.modes[i].h != ch)
                continue;
            if (std::fabs(u.modes[i].hz - chz) < 0.01f)
                r = (int)rates.size();
            rates.push_back(hz_text(u.modes[i].hz));
            hzs.push_back(u.modes[i].hz);
        }
        if (combo("##fshz", &r, rates) && r >= 0)
        {
            Settings n = cur;
            n.exclusive_w = cw;
            n.exclusive_h = ch;
            n.exclusive_hz = hzs[r];
            commit(u, cur, n);
        }
    }
    ImGui::EndTable();
}

void picture_section(VideoUi &u, const Settings &cur, float sc)
{
    ImGui::SeparatorText("Picture");
    if (!ImGui::BeginTable("##picture", 2, ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, kLabelWidth * sc);
    ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthFixed, kValueWidth * sc);

    {
        row("Scaling");
        std::vector<std::string> items, tips;
        items.push_back("Pixel-perfect (integer)");
        tips.push_back("The largest whole multiple of the game's screen that fits; every pixel the same size.");
        items.push_back("Fit (sharp)");
        tips.push_back("As large as fits, aspect ratio kept, crisp pixels.");
        items.push_back("Fit (smooth)");
        tips.push_back("As large as fits, aspect ratio kept, filtered.");
        items.push_back("Stretch");
        tips.push_back("Fills the whole window or screen.");
        int v = cur.scaling;
        if (combo("##scaling", &v, items, &tips))
        {
            Settings n = cur;
            n.scaling = v;
            commit(u, cur, n);
        }
    }
#if !defined(__SWITCH__)   // the display always paces the picture there
    {
        row("VSync");
        bool b = cur.vsync;
        if (ImGui::Checkbox("##vsync", &b))
        {
            Settings n = cur;
            n.vsync = b;
            commit(u, cur, n);
        }
        ImGui::SetItemTooltip("Show each picture in step with the monitor's refresh (no tearing).\n"
                              "The game's own speed does not change.");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", zcdisplay::refresh_hz() > 0 ? hz_text((float)zcdisplay::refresh_hz()).c_str() : "");
    }
#endif
    {
        row("Frame rate");
        bool b = cur.show_fps;
        if (ImGui::Checkbox("Show in the corner##fps", &b))
        {
            Settings n = cur;
            n.show_fps = b;
            commit(u, cur, n);
        }
    }
    ImGui::EndTable();
}

void interface_section(VideoUi &u, const Settings &cur, float sc)
{
    ImGui::SeparatorText("Interface");
    if (!ImGui::BeginTable("##interface", 2, ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, kLabelWidth * sc);
    ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthFixed, kValueWidth * sc);
    row("UI scale");
    static const int pct[] = { 75, 90, 100, 110, 125, 150, 175, 200, 250, 300 };
    const int npct = (int)(sizeof(pct) / sizeof(pct[0]));
    std::vector<std::string> items;
    int v = -1;
    int now = (int)std::lround(cur.ui_scale * 100.0f);
    for (int i = 0; i < npct; i++)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d %%", pct[i]);
        items.push_back(buf);
        if (pct[i] == now)
            v = i;
    }
    if (v < 0)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d %%", now);
        v = (int)items.size();
        items.push_back(buf);
    }
    if (combo("##uiscale", &v, items) && v < npct)
    {
        Settings n = cur;
        n.ui_scale = pct[v] / 100.0f;
        commit(u, cur, n);
    }
    ImGui::SetItemTooltip("Size of menus and dialogs, on top of the monitor's own scaling.");
#ifdef ZC_ZQEDITOR
    {
        row("Editor layout");
        std::vector<std::string> layouts, tips;
        layouts.push_back("Large (800 " TIMES " 600)");
        tips.push_back("The usual layout, with more of the quest on screen.");
        layouts.push_back("Small (320 " TIMES " 240)");
        tips.push_back("The compact layout of the original ZQuest.");
        int layout = g_layout_request >= 0 ? g_layout_request : (is_large ? 0 : 1);
        if (combo("##layout", &layout, layouts, &tips))
            g_layout_request = layout;
    }
#endif
    ImGui::EndTable();
}

// The on-screen gamepad (src/port/platform/platform_touch.cpp).
void touch_section(VideoUi &u, const Settings &cur, float sc)
{
    ImGui::SeparatorText("Touch controls");
    if (!ImGui::BeginTable("##touch", 2, ImGuiTableFlags_SizingFixedFit))
        return;
    ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, kLabelWidth * sc);
    ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthFixed, kValueWidth * sc);

    row("Show");
    {
        std::vector<std::string> items;
        items.push_back("Auto (after a touch)");
        items.push_back("Always");
        items.push_back("Never");
        int v = (cur.touch_mode >= 0 && cur.touch_mode < 3) ? cur.touch_mode : 0;
        if (combo("##touchmode", &v, items))
        {
            Settings n = cur;
            n.touch_mode = v;
            commit(u, cur, n);
        }
        ImGui::SetItemTooltip("Auto: the gamepad appears when you touch the screen and hides when a keyboard or controller is used.");
    }

    struct Pct { const char *id; const char *label; const int *vals; int n; float Settings::*field; };
    static const int sizes[] = { 70, 85, 100, 115, 130, 150 };
    static const int opac[] = { 25, 40, 60, 80, 100 };
    const Pct rows[2] = {
        { "##touchsize", "Size", sizes, (int)(sizeof(sizes) / sizeof(sizes[0])), &Settings::touch_size },
        { "##touchopacity", "Opacity", opac, (int)(sizeof(opac) / sizeof(opac[0])), &Settings::touch_opacity },
    };
    for (int r = 0; r < 2; r++)
    {
        row(rows[r].label);
        std::vector<std::string> items;
        int now = (int)std::lround((cur.*rows[r].field) * 100.0f), v = -1;
        for (int i = 0; i < rows[r].n; i++)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d %%", rows[r].vals[i]);
            items.push_back(buf);
            if (rows[r].vals[i] == now)
                v = i;
        }
        if (v < 0)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d %%", now);
            v = (int)items.size();
            items.push_back(buf);
        }
        if (combo(rows[r].id, &v, items) && v < rows[r].n)
        {
            Settings n = cur;
            n.*rows[r].field = rows[r].vals[v] / 100.0f;
            commit(u, cur, n);
        }
    }
    ImGui::EndTable();
}

void confirm_popup(VideoUi &u, float sc)
{
    const char *title = "Keep these settings?###zplayer_keep";
    if (u.want_popup)
    {
        ImGui::OpenPopup(title);
        u.want_popup = false;
    }
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(title, NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                                              ImGuiWindowFlags_NoSavedSettings))
        return;
    if (u.confirming && zcdisplay::hotkey_toggles() != u.hotkeys)
        { u.confirming = false; tlog("hotkey cancel"); }  // Alt+Enter: the player chose the mode himself
    if (!u.confirming)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    double left = std::chrono::duration<double>(u.deadline - Clock::now()).count();
    if (left <= 0.0)
    {
        revert(u);
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::Text("Reverting in %d s", (int)std::ceil(left));
    ImGui::ProgressBar((float)(left / kConfirmSeconds), ImVec2(320 * sc, 5 * sc), "");
    ImGui::Spacing();
    const float bw = (320 * sc - ImGui::GetStyle().ItemSpacing.x) / 2;
    if (ImGui::Button("Keep", ImVec2(bw, 0)))
    {
        tlog("keep button"); // TEMP
        u.confirming = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Revert", ImVec2(bw, 0)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
    {
        revert(u);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

#if defined(__SWITCH__)
void start_test_hook() {}
void tlog(const char *, ...) {}
void run_script(VideoUi &, const Settings &) {}
#else
// ==== TEMP TEST HOOK BEGIN (not committed) ====
}
#include <SDL3/SDL.h>
#include <thread>
#include <cstdarg>
#include <ctime>
#include <cstdlib>
namespace {
std::vector<std::string> g_script;
size_t g_script_pos = 0;
Clock::time_point g_script_next;
double g_step = 2.5;
FILE *g_tlog = NULL;
Clock::time_point g_t0 = Clock::now();
void tlog(const char *fmt, ...)
{
    if (!g_tlog) g_tlog = fopen("video_test.log", "a");
    if (!g_tlog) return;
    time_t tt = time(NULL); struct tm *lt = localtime(&tt);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() % 1000;
    fprintf(g_tlog, "%02d%02d%02d.%03d ", lt->tm_hour, lt->tm_min, lt->tm_sec, (int)ms);
    va_list ap; va_start(ap, fmt); vfprintf(g_tlog, fmt, ap); va_end(ap);
    fputc('\n', g_tlog); fflush(g_tlog);
}
void log_state(const char *what)
{
    Settings s = zcdisplay::get();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    tlog("%-14s mode=%d fsk=%d win=%dx%d ex=%dx%d@%.2f disp=%d mon=%d scal=%d vs=%d fps=%d ui=%.2f | view=%.0fx%.0f confirming=%d hz=%d scale=%.2f fr=%.1f",
         what, s.mode, s.fullscreen_mode, s.window_w, s.window_h, s.exclusive_w, s.exclusive_h, s.exclusive_hz,
         s.display, zcdisplay::window_monitor(), s.scaling, s.vsync, s.show_fps, s.ui_scale, ds.x, ds.y,
         g_ui.confirming, zcdisplay::refresh_hz(), zcui::scale(), ImGui::GetIO().Framerate);
}
void run_script(VideoUi &u, const Settings &cur)
{
    if (g_script_pos >= g_script.size() || Clock::now() < g_script_next) return;
    std::string a = g_script[g_script_pos++];
    g_script_next = Clock::now() + std::chrono::milliseconds((int)(g_step * 1000));
    Settings n = cur;
    int x = 0, y = 0; float hz = 0;
    if (a.compare(0, 5, "size:") == 0 && sscanf(a.c_str() + 5, "%dx%d", &x, &y) == 2) { n.window_w = x; n.window_h = y; commit(u, cur, n); }
    else if (a.compare(0, 5, "mode:") == 0) { n.mode = atoi(a.c_str() + 5); if (n.mode) n.fullscreen_mode = n.mode; commit(u, cur, n); }
    else if (a.compare(0, 4, "mon:") == 0) { n.display = atoi(a.c_str() + 4); commit(u, cur, n); }
    else if (a.compare(0, 6, "fsres:") == 0 && sscanf(a.c_str() + 6, "%dx%d@%f", &x, &y, &hz) >= 2) { n.exclusive_w = x; n.exclusive_h = y; n.exclusive_hz = hz; commit(u, cur, n); }
    else if (a == "keep") { u.confirming = false; }
    else if (a == "revert") { revert(u); }
    else if (a.compare(0, 6, "scale:") == 0) { n.scaling = atoi(a.c_str() + 6); commit(u, cur, n); }
    else if (a.compare(0, 6, "vsync:") == 0) { n.vsync = atoi(a.c_str() + 6) != 0; commit(u, cur, n); }
    else if (a.compare(0, 4, "fps:") == 0) { n.show_fps = atoi(a.c_str() + 4) != 0; commit(u, cur, n); }
    else if (a.compare(0, 3, "ui:") == 0) { n.ui_scale = atoi(a.c_str() + 3) / 100.0f; commit(u, cur, n); }
    else if (a == "reset") { Settings d = zcdisplay::defaults(); d.display = cur.display; commit(u, cur, d); }
    else if (a == "altenter") {
        SDL_Event e; SDL_zero(e);
        e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = SDL_SCANCODE_RETURN; e.key.key = SDLK_RETURN;
        e.key.mod = SDL_KMOD_LALT; e.key.down = true; SDL_PushEvent(&e);
        e.type = SDL_EVENT_KEY_UP; e.key.down = false; SDL_PushEvent(&e);
    }
    else if (a == "close") { u.close = true; }
    else if (a.compare(0, 6, "delay:") == 0) { g_step = atof(a.c_str() + 6); }
    log_state(a.c_str());
}
void test_thread(std::string script)
{
    std::this_thread::sleep_for(std::chrono::seconds(4));
    {
        zcui::Lock lock;
        size_t p = 0;
        while (p <= script.size()) {
            size_t c = script.find(',', p);
            if (c == std::string::npos) c = script.size();
            if (c > p) g_script.push_back(script.substr(p, c - p));
            p = c + 1;
        }
        g_script_pos = 0;
        g_script_next = Clock::now() + std::chrono::seconds(2);
    }
    {
        std::vector<zcdisplay::Monitor> ms = zcdisplay::monitors();
        for (size_t i = 0; i < ms.size(); i++)
            tlog("monitor %d '%s' at %d,%d %dx%d desktop %dx%d@%.2f maxclient %dx%d%s", ms[i].index, ms[i].name.c_str(), ms[i].x, ms[i].y, ms[i].w, ms[i].h, ms[i].desktop_w, ms[i].desktop_h, ms[i].desktop_hz, ms[i].max_client_w, ms[i].max_client_h, ms[i].primary ? " primary" : "");
        std::vector<zcdisplay::DisplayMode> md = zcdisplay::fullscreen_modes(zcdisplay::window_monitor());
        std::string all;
        for (size_t i = 0; i < md.size(); i++) { char b[48]; snprintf(b, sizeof(b), "%dx%d@%.2f ", md[i].w, md[i].h, md[i].hz); all += b; }
        tlog("modes: %s", all.c_str());
    }
    if (getenv("ZPLAYER_VIDEO_TEST_MENU")) { tlog("waiting for the menu"); return; }
    tlog("open settings, %d steps", (int)g_script.size());
    zplayer_video_settings();
    tlog("closed");
    zplayer_video_save_config();
    flush_config_file();
    tlog("saved");
}
void start_test_hook()
{
    static bool started = false;
    const char *s = getenv("ZPLAYER_VIDEO_TEST");
    if (started || !s || !*s) return;
    started = true;
    std::thread(test_thread, std::string(s)).detach();
}
// ==== TEMP TEST HOOK END ====
#endif // __SWITCH__

void settings_layer(void *)
{
    VideoUi &u = g_ui;
    if (!u.open)
        return;
    const float sc = zcui::scale();
    run_script(u, zcdisplay::get());   // TEMP TEST HOOK
    const Settings cur = zcdisplay::get();
    const ImGuiViewport *vp = ImGui::GetMainViewport();

    // the game dimmed behind
    ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                  IM_COL32(0, 0, 0, 120));

    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    // A small window (640x480) scrolls rather than cutting the window off.
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(vp->Size.x - 12 * sc, vp->Size.y - 12 * sc));
    if (u.focus)
    {
        ImGui::SetNextWindowFocus();
        u.focus = false;
    }
    bool open = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Video Settings###zplayer_video", &open, flags))
    {
#if !defined(__ANDROID__) && !defined(__SWITCH__)
        display_section(u, cur, sc);
#endif // Android, Switch: the window always fills the screen (no modes, sizes, monitors)
        ImGui::Spacing();
        picture_section(u, cur, sc);
        ImGui::Spacing();
        interface_section(u, cur, sc);
#if !defined(ZC_ZQEDITOR) && !defined(__SWITCH__)
        touch_section(u, cur, sc);
#endif

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Reset to defaults"))
        {
            Settings n = zcdisplay::defaults();
            n.display = cur.display;   // the monitor stays
            commit(u, cur, n);
        }
        ImGui::SetItemTooltip("Windowed at the game's size, Fit (sharp), no vsync, 100 %%.\n"
                              "The monitor stays as it is.");
        const float close_w = 110 * sc;
        ImGui::SameLine();
        const float gap = ImGui::GetContentRegionAvail().x - close_w;
        if (gap > 0)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + gap);
        if (ImGui::Button("Close", ImVec2(close_w, 0)))
            u.close = true;
#if !defined(__ANDROID__) && !defined(__SWITCH__)
        ImGui::TextDisabled("Alt+Enter switches between windowed and fullscreen at any time.");
#endif

        confirm_popup(u, sc);

        // Esc (or the gamepad's B) closes the window, unless it just closed
        // a list or the question.
        const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
        if (!popup && !u.popup_last_frame && !u.confirming && !ImGui::IsAnyItemActive() &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
            u.close = true;
        u.popup_last_frame = popup;
    }
    ImGui::End();
    if (!open)
        u.close = true;
}

} // namespace

// ---------------------------------------------------------------------------
// game side (zcport/video_settings.h)
// ---------------------------------------------------------------------------

void zplayer_video_settings()
{
    if (!zcui::available())
    {
        al_trace("Video Settings: the UI is not available (no Vulkan presenter)\n");
        return;
    }
    {
        zcui::Lock lock;
        if (g_ui.open)
            return;
        g_ui.open = true;
        g_ui.close = false;
        g_ui.focus = true;
        g_ui.confirming = false;
        g_ui.want_popup = false;
        g_ui.popup_last_frame = false;
        g_ui.monitors.clear();
        g_ui.modes_for = -1;
    }
    const int layer = zcui::add_layer(settings_layer, NULL, 600);
    zcui::begin_session();
    for (;;)
    {
        zcui::wait_frame(100);
        bool done, revert_now = false;
        Settings r;
#ifdef ZC_ZQEDITOR
        int layout = -1;
        {
            zcui::Lock lock;
            layout = g_layout_request;
        }
        if (layout >= 0)
        {
            zq_switch_layout(layout == 0);
            zcui::Lock lock;
            g_layout_request = -1;
        }
#endif
        {
            zcui::Lock lock;
            done = g_ui.close;
            // No UI frames for a while (the presenter stalled on a mode
            // the monitor cannot show?): the timer still runs out.
            if (g_ui.confirming && Clock::now() > g_ui.deadline + std::chrono::seconds(2))
            {
                g_ui.confirming = false;
                r = with_window_part(zcdisplay::get(), g_ui.revert_to);
                revert_now = true;
            }
        }
        if (revert_now)
            zcdisplay::set(r);
#if defined(__SWITCH__)
        if (close_button_quit)   // HOME is closing the software
            done = true;
#endif
        if (done || !zcui::available())
            break;
    }
    zcui::end_session();
    zcui::remove_layer(layer);
    bool revert_now = false;
    Settings r;
    {
        zcui::Lock lock;
        if (g_ui.confirming)
        {
            g_ui.confirming = false;
            r = with_window_part(zcdisplay::get(), g_ui.revert_to);
            revert_now = true;
        }
        g_ui.open = false;
    }
    if (revert_now)
        zcdisplay::set(r);
}

void zplayer_video_save_config()
{
    if (!zcdisplay::managed())
        return;
    const Settings s = zcdisplay::get();
    int lw = 0, lh = 0;
    zcdisplay::logical_size(&lw, &lh);
    int ww = s.window_w, wh = s.window_h;
    if (ww == lw && wh == lh)
        ww = wh = 0;   // the game's size, whatever it is next time
    char hz[32];
    snprintf(hz, sizeof(hz), "%.2f", s.exclusive_hz);
    set_config_string(kSection, "fullscreen_mode", s.fullscreen_mode == zcdisplay::MODE_EXCLUSIVE ? "exclusive" : "borderless");
    set_config_int(kSection, "window_width", ww);
    set_config_int(kSection, "window_height", wh);
    set_config_int(kSection, "fullscreen_width", s.exclusive_w);
    set_config_int(kSection, "fullscreen_height", s.exclusive_h);
    set_config_string(kSection, "fullscreen_refresh", hz);
    set_config_string(kSection, "scaling", kScalingKeys[s.scaling >= 0 && s.scaling < 4 ? s.scaling : 1]);
    set_config_int(kSection, "vsync", s.vsync ? 1 : 0);
    set_config_int(kSection, "show_fps", s.show_fps ? 1 : 0);
    set_config_int(kSection, "ui_scale", (int)std::lround(s.ui_scale * 100.0f));
    static const char *const kTouchKeys[3] = { "auto", "always", "never" };
    set_config_string(kSection, "touch_controls", kTouchKeys[s.touch_mode >= 0 && s.touch_mode < 3 ? s.touch_mode : 0]);
    set_config_int(kSection, "touch_size", (int)std::lround(s.touch_size * 100.0f));
    set_config_int(kSection, "touch_opacity", (int)std::lround(s.touch_opacity * 100.0f));
    set_config_int(kGameSection, "window_display", s.display);
    // Windowed or fullscreen is the game's own setting; it changes only
    // when the player switched this run (not for -windowed / -fullscreen).
    if (zcdisplay::fullscreen_changed())
        fullscreen = zcdisplay::is_fullscreen() ? 1 : 0;
}

void zplayer_video_pin_window()
{
    if (!zcdisplay::managed())
        return;
    Settings s = zcdisplay::get();
    if (s.window_w > 0 && s.window_h > 0)
        return;
    int lw = 0, lh = 0;
    zcdisplay::logical_size(&lw, &lh);
    s.window_w = lw;
    s.window_h = lh;
    zcdisplay::set(s);
}

int zplayer_window_is_fullscreen()
{
    return zcdisplay::is_fullscreen() ? 1 : 0;
}

void zplayer_toggle_fullscreen()
{
    zcdisplay::toggle_fullscreen();
}

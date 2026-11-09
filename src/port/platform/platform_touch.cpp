// zplayer: on-screen gamepad for touch screens (Android phones and tablets,
// Windows touch screens).
//
// - Multi-touch: every finger drives one control. The D-pad follows its
//   finger (8 directions, diagonals press two keys); a button stays pressed
//   until its finger lifts.
// - Each control presses the game's own key for it (zcport/touch.h: the
//   game side supplies its current key bindings), so the game sees normal
//   key presses and its Controls settings keep working.
// - Placed in the side margins left and right of the game's picture when
//   they are wide enough (landscape phones), else over the picture's edges.
// - Visibility (Video Settings, "Touch controls"): Auto shows them after the
//   first touch and hides them when a keyboard key or gamepad is used,
//   Always / Never. Hidden while a menu or dialog is open (the UI then takes
//   the touches as mouse input).
// - While the controls are enabled, touches do not reach the game as mouse
//   clicks.
//
// Runs on the platform thread (SDL events and the UI layer).
//
// Written from scratch for zcport.

#include "touch_internal.h"

#include "zcport/display.h"
#include "zcport/input.h"
#include "zcport/touch.h"
#include "zcport/ui.h"

#include <allegro.h>
#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>

namespace
{

enum Control
{
    C_NONE = -1,
    C_DPAD = 0,
    C_A, C_B, C_START, C_MAP, C_L, C_R, C_EX1, C_EX2, C_EX3, C_EX4, C_MENU, C_KEYBOARD,
    C_COUNT
};

struct Shape
{
    bool round;          // circle (cx, cy, r) or rounded box (cx, cy, w x h)
    float cx, cy, r, w, h;
    const char *label;
};

std::atomic<ZcportTouchKeysFn> g_key_source(NULL);

SDL_Window *g_window = NULL;
bool g_layer_added = false;

// picture rectangle in window pixels, and the window's pixel size
float g_pic_x0 = 0, g_pic_y0 = 0, g_pic_x1 = 0, g_pic_y1 = 0;
int g_win_px_w = 0, g_win_px_h = 0;

bool g_seen_touch = false;     // Auto: a finger touched the screen since the last key/gamepad use
Shape g_shapes[C_COUNT];
bool g_layout_ok = false;
float g_unit = 0;

// fingers -> control; D-pad direction bits (1 up, 2 down, 4 left, 8 right)
std::map<SDL_FingerID, int> g_fingers;
std::map<SDL_FingerID, int> g_dpad_dirs;
int g_key_refs[ZCPORT_TOUCH_KEYS];
int g_down_key[ZCPORT_TOUCH_KEYS];   // the key code pressed for each logical key (bindings may change meanwhile)

int setting_mode()
{
    return zcdisplay::get().touch_mode;
}

bool enabled()
{
    return setting_mode() != zcdisplay::TOUCH_NEVER;
}

bool visible_now()
{
    if (!enabled() || !g_layout_ok || zcui::session_active())
        return false;
    if (g_pic_x1 <= g_pic_x0 || g_pic_y1 <= g_pic_y0)
        return false;
    return setting_mode() == zcdisplay::TOUCH_ALWAYS || g_seen_touch;
}

int ascii_for(int key)
{
    switch (key) {
        case KEY_ENTER: return 13;
        case KEY_ESC:   return 27;
        case KEY_SPACE: return ' ';
        default:        return 0;
    }
}

void press(int logical)
{
    if (logical < 0 || logical >= ZCPORT_TOUCH_KEYS)
        return;
    if (g_key_refs[logical]++ > 0)
        return;
    int keys[ZCPORT_TOUCH_KEYS] = { 0 };
    keys[ZCPORT_TOUCH_UP] = KEY_UP;
    keys[ZCPORT_TOUCH_DOWN] = KEY_DOWN;
    keys[ZCPORT_TOUCH_LEFT] = KEY_LEFT;
    keys[ZCPORT_TOUCH_RIGHT] = KEY_RIGHT;
    keys[ZCPORT_TOUCH_START] = KEY_ENTER;
    keys[ZCPORT_TOUCH_MENU] = KEY_ESC;
    if (ZcportTouchKeysFn fn = g_key_source.load())
        fn(keys);
    keys[ZCPORT_TOUCH_MENU] = KEY_ESC;   // always the system menu
    int k = keys[logical];
    g_down_key[logical] = k;
    if (k > 0 && k < KEY_MAX)
        zcport_key_down(k, ascii_for(k));
}

void release(int logical)
{
    if (logical < 0 || logical >= ZCPORT_TOUCH_KEYS || g_key_refs[logical] <= 0)
        return;
    if (--g_key_refs[logical] > 0)
        return;
    int k = g_down_key[logical];
    g_down_key[logical] = 0;
    if (k > 0 && k < KEY_MAX)
        zcport_key_up(k);
}

int logical_of(int control)
{
    switch (control) {
        case C_A:     return ZCPORT_TOUCH_A;
        case C_B:     return ZCPORT_TOUCH_B;
        case C_START: return ZCPORT_TOUCH_START;
        case C_MAP:   return ZCPORT_TOUCH_MAP;
        case C_L:     return ZCPORT_TOUCH_L;
        case C_R:     return ZCPORT_TOUCH_R;
        case C_EX1:   return ZCPORT_TOUCH_EX1;
        case C_EX2:   return ZCPORT_TOUCH_EX2;
        case C_EX3:   return ZCPORT_TOUCH_EX3;
        case C_EX4:   return ZCPORT_TOUCH_EX4;
        case C_MENU:  return ZCPORT_TOUCH_MENU;
        default:      return -1;
    }
}

void set_dpad(SDL_FingerID id, int dirs)
{
    int old = g_dpad_dirs[id];
    if (old == dirs)
        return;
    static const int bits[4] = { 1, 2, 4, 8 };
    static const int logical[4] = { ZCPORT_TOUCH_UP, ZCPORT_TOUCH_DOWN, ZCPORT_TOUCH_LEFT, ZCPORT_TOUCH_RIGHT };
    for (int i = 0; i < 4; i++) {
        if ((dirs & bits[i]) && !(old & bits[i]))
            press(logical[i]);
        if (!(dirs & bits[i]) && (old & bits[i]))
            release(logical[i]);
    }
    g_dpad_dirs[id] = dirs;
}

// Directions for a point relative to the D-pad centre (8 sectors).
int dpad_dirs(float dx, float dy, float r)
{
    float d = std::sqrt(dx * dx + dy * dy);
    if (d < r * 0.22f)
        return 0;
    float a = std::atan2(dy, dx);                 // y down
    int sector = (int)std::floor((a + 3.14159265f / 8.0f) / (3.14159265f / 4.0f));
    sector = ((sector % 8) + 8) % 8;              // 0 = right, 2 = down, 4 = left, 6 = up
    static const int dirs[8] = { 8, 8 | 2, 2, 2 | 4, 4, 4 | 1, 1, 1 | 8 };
    return dirs[sector];
}

void window_points(float *w, float *h, float *density)
{
    int pw = 0, ph = 0;
    SDL_GetWindowSize(g_window, &pw, &ph);
    *w = (float)pw;
    *h = (float)ph;
    float dens = SDL_GetWindowPixelDensity(g_window);
    *density = dens > 0 ? dens : 1.0f;
}

void layout()
{
    g_layout_ok = false;
    if (!g_window)
        return;
    float W, H, dens;
    window_points(&W, &H, &dens);
    if (W <= 0 || H <= 0)
        return;
    zcdisplay::Settings s = zcdisplay::get();
    float size = s.touch_size > 0.3f ? s.touch_size : 1.0f;

    float u = std::min(H, W * 0.5f) * 0.085f * size;
    float left = g_pic_x0 / dens, right = W - g_pic_x1 / dens;
    float margin = std::min(left, right);
    float lcx, rcx;
    if (margin >= u * 4.2f) {
        lcx = left * 0.5f;
        rcx = W - right * 0.5f;
    }
    else {
        lcx = u * 2.4f;
        rcx = W - u * 2.4f;
    }

    Shape *sh = g_shapes;
    sh[C_DPAD]     = { true,  lcx,            H * 0.62f, u * 1.9f, 0, 0, NULL };
    sh[C_A]        = { true,  rcx + u * 0.95f, H * 0.56f, u * 0.95f, 0, 0, "A" };
    sh[C_B]        = { true,  rcx - u * 0.95f, H * 0.73f, u * 0.95f, 0, 0, "B" };
    sh[C_EX1]      = { true,  rcx - u * 1.65f, H * 0.31f, u * 0.5f, 0, 0, "1" };
    sh[C_EX2]      = { true,  rcx - u * 0.55f, H * 0.31f, u * 0.5f, 0, 0, "2" };
    sh[C_EX3]      = { true,  rcx + u * 0.55f, H * 0.31f, u * 0.5f, 0, 0, "3" };
    sh[C_EX4]      = { true,  rcx + u * 1.65f, H * 0.31f, u * 0.5f, 0, 0, "4" };
    sh[C_L]        = { false, lcx,            H * 0.12f, 0, u * 2.6f, u * 0.85f, "L" };
    sh[C_R]        = { false, rcx,            H * 0.12f, 0, u * 2.6f, u * 0.85f, "R" };
    sh[C_MENU]     = { false, lcx - u * 0.85f, H * 0.29f, 0, u * 1.5f, u * 0.75f, "MENU" };
    sh[C_MAP]      = { false, lcx + u * 0.85f, H * 0.29f, 0, u * 1.5f, u * 0.75f, "MAP" };
    sh[C_KEYBOARD] = { false, lcx,            H * 0.92f, 0, u * 2.0f, u * 0.75f, "ABC" };
    sh[C_START]    = { false, rcx,            H * 0.92f, 0, u * 2.4f, u * 0.75f, "START" };
    g_unit = u;
    g_layout_ok = true;
}

// The control under a point (window points), with some slack around each.
int hit(float x, float y)
{
    int best = C_NONE;
    float best_d = 1e9f;
    for (int c = 0; c < C_COUNT; c++) {
        const Shape &s = g_shapes[c];
        float dx = x - s.cx, dy = y - s.cy;
        float d;
        if (s.round) {
            float slack = (c == C_DPAD) ? 1.35f : 1.3f;
            d = std::sqrt(dx * dx + dy * dy) / (s.r * slack);
        }
        else {
            float hw = s.w * 0.5f + g_unit * 0.2f, hh = s.h * 0.5f + g_unit * 0.2f;
            d = std::max(std::fabs(dx) / hw, std::fabs(dy) / hh);
        }
        if (d <= 1.0f && d < best_d) {
            best_d = d;
            best = c;
        }
    }
    return best;
}

void toggle_keyboard()
{
    if (!g_window)
        return;
    if (SDL_TextInputActive(g_window))
        SDL_StopTextInput(g_window);
    else
        SDL_StartTextInput(g_window);
}

void finger_down(const SDL_TouchFingerEvent &f)
{
    g_seen_touch = true;
    layout();
    float W, H, dens;
    window_points(&W, &H, &dens);
    float x = f.x * W, y = f.y * H;
    int c = hit(x, y);
    if (c == C_NONE)
        return;
    g_fingers[f.fingerID] = c;
    if (c == C_DPAD) {
        const Shape &s = g_shapes[C_DPAD];
        set_dpad(f.fingerID, dpad_dirs(x - s.cx, y - s.cy, s.r));
    }
    else if (c == C_KEYBOARD)
        toggle_keyboard();
    else
        press(logical_of(c));
}

void finger_motion(const SDL_TouchFingerEvent &f)
{
    std::map<SDL_FingerID, int>::iterator it = g_fingers.find(f.fingerID);
    if (it == g_fingers.end() || it->second != C_DPAD)
        return;
    float W, H, dens;
    window_points(&W, &H, &dens);
    const Shape &s = g_shapes[C_DPAD];
    set_dpad(f.fingerID, dpad_dirs(f.x * W - s.cx, f.y * H - s.cy, s.r));
}

void finger_up(SDL_FingerID id)
{
    std::map<SDL_FingerID, int>::iterator it = g_fingers.find(id);
    if (it == g_fingers.end())
        return;
    if (it->second == C_DPAD) {
        set_dpad(id, 0);
        g_dpad_dirs.erase(id);
    }
    else if (it->second != C_KEYBOARD)
        release(logical_of(it->second));
    g_fingers.erase(it);
}

bool control_down(int c)
{
    for (std::map<SDL_FingerID, int>::const_iterator it = g_fingers.begin(); it != g_fingers.end(); ++it)
        if (it->second == c)
            return true;
    return false;
}

// ---- drawing (UI layer, platform thread) ---------------------------------

ImU32 col(int r, int g, int b, float a)
{
    return IM_COL32(r, g, b, (int)std::lround(std::max(0.0f, std::min(1.0f, a)) * 255.0f));
}

void draw_label(ImDrawList *dl, float cx, float cy, const char *text, float size, ImU32 c)
{
    ImFont *font = ImGui::GetFont();
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
    dl->AddText(font, size, ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f), c, text);
}

void draw_layer(void *)
{
    if (!visible_now())
        return;
    layout();
    if (!g_layout_ok)
        return;
    float op = zcdisplay::get().touch_opacity;
    if (!(op > 0.05f && op <= 1.0f))
        op = 0.6f;
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    const float u = g_unit;
    const ImU32 fill = col(255, 255, 255, 0.16f * op);
    const ImU32 fill_down = col(59, 130, 246, 0.75f * op);
    const ImU32 edge = col(255, 255, 255, 0.45f * op);
    const ImU32 text = col(255, 255, 255, 0.9f * op);

    // D-pad: a disc with a cross; pressed directions light up
    {
        const Shape &s = g_shapes[C_DPAD];
        dl->AddCircleFilled(ImVec2(s.cx, s.cy), s.r, fill, 48);
        dl->AddCircle(ImVec2(s.cx, s.cy), s.r, edge, 48, std::max(1.0f, u * 0.04f));
        int dirs = 0;
        for (std::map<SDL_FingerID, int>::const_iterator it = g_dpad_dirs.begin(); it != g_dpad_dirs.end(); ++it)
            dirs |= it->second;
        float arm = s.r * 0.36f, len = s.r * 0.86f;
        struct { int bit; float dx, dy; } arms[4] = { { 1, 0, -1 }, { 2, 0, 1 }, { 4, -1, 0 }, { 8, 1, 0 } };
        for (int i = 0; i < 4; i++) {
            float x0 = s.cx + (arms[i].dx != 0 ? arms[i].dx * arm * 0.5f : -arm * 0.5f);
            float y0 = s.cy + (arms[i].dy != 0 ? arms[i].dy * arm * 0.5f : -arm * 0.5f);
            float x1 = s.cx + (arms[i].dx != 0 ? arms[i].dx * len : arm * 0.5f);
            float y1 = s.cy + (arms[i].dy != 0 ? arms[i].dy * len : arm * 0.5f);
            ImVec2 a(std::min(x0, x1), std::min(y0, y1)), b(std::max(x0, x1), std::max(y0, y1));
            dl->AddRectFilled(a, b, (dirs & arms[i].bit) ? fill_down : col(255, 255, 255, 0.28f * op), u * 0.12f);
        }
        dl->AddRectFilled(ImVec2(s.cx - arm * 0.5f, s.cy - arm * 0.5f), ImVec2(s.cx + arm * 0.5f, s.cy + arm * 0.5f),
                          col(255, 255, 255, 0.28f * op));
    }

    for (int c = C_A; c < C_COUNT; c++) {
        const Shape &s = g_shapes[c];
        bool down = control_down(c) || (c == C_KEYBOARD && g_window && SDL_TextInputActive(g_window));
        if (s.round) {
            dl->AddCircleFilled(ImVec2(s.cx, s.cy), s.r, down ? fill_down : fill, 40);
            dl->AddCircle(ImVec2(s.cx, s.cy), s.r, edge, 40, std::max(1.0f, u * 0.04f));
            draw_label(dl, s.cx, s.cy, s.label, s.r * (c <= C_B ? 0.95f : 0.8f), text);
        }
        else {
            ImVec2 a(s.cx - s.w * 0.5f, s.cy - s.h * 0.5f), b(s.cx + s.w * 0.5f, s.cy + s.h * 0.5f);
            dl->AddRectFilled(a, b, down ? fill_down : fill, s.h * 0.5f);
            dl->AddRect(a, b, edge, s.h * 0.5f, 0, std::max(1.0f, u * 0.04f));
            draw_label(dl, s.cx, s.cy, s.label, s.h * 0.5f, text);
        }
    }
}

} // namespace

extern "C" void zcport_touch_set_key_source(ZcportTouchKeysFn fn)
{
    g_key_source = fn;
}

namespace zcport_touch
{

void attach(SDL_Window *window)
{
    g_window = window;
    if (!g_layer_added) {
        zcui::add_layer(draw_layer, NULL, -100);   // under menus and dialogs
        g_layer_added = true;
    }
}

void detach()
{
    release_all();
    g_window = NULL;
}

void set_picture(float x0, float y0, float x1, float y1, int win_px_w, int win_px_h)
{
    g_pic_x0 = x0;
    g_pic_y0 = y0;
    g_pic_x1 = x1;
    g_pic_y1 = y1;
    g_win_px_w = win_px_w;
    g_win_px_h = win_px_h;
}

void release_all()
{
    std::map<SDL_FingerID, int> fingers = g_fingers;
    for (std::map<SDL_FingerID, int>::const_iterator it = fingers.begin(); it != fingers.end(); ++it)
        finger_up(it->first);
    g_fingers.clear();
    g_dpad_dirs.clear();
}

void physical_input()
{
    // Keys from the on-screen keyboard are not a hardware keyboard.
    if (g_window && SDL_TextInputActive(g_window))
        return;
    if (setting_mode() == zcdisplay::TOUCH_AUTO && g_seen_touch && g_fingers.empty())
        g_seen_touch = false;
}

bool event(const SDL_Event &ev)
{
    if (!g_window)
        return false;
    bool session = zcui::session_active();
    switch (ev.type) {
        case SDL_EVENT_FINGER_DOWN:
            if (session || !enabled())
                return false;
            finger_down(ev.tfinger);
            return true;
        case SDL_EVENT_FINGER_MOTION:
            if (session || !enabled())
                return false;
            finger_motion(ev.tfinger);
            return true;
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            finger_up(ev.tfinger.fingerID);
            return !session && enabled();
        // Mouse events SDL makes from touches: not for the game while the
        // touch controls are on (the UI already had them during a session).
        case SDL_EVENT_MOUSE_MOTION:
            return enabled() && ev.motion.which == SDL_TOUCH_MOUSEID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return enabled() && ev.button.which == SDL_TOUCH_MOUSEID;
        case SDL_EVENT_MOUSE_WHEEL:
            return enabled() && ev.wheel.which == SDL_TOUCH_MOUSEID;
        default:
            return false;
    }
}

} // namespace zcport_touch

// zplayer UI layer: the ImGui context, style, fonts, layers, sessions and the
// game-thread handshake. See zcport/ui.h (public) and ui_platform.h.
//
// Written from scratch for zcport.

#include "zcport/ui.h"
#include "ui_platform.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace
{

struct Layer
{
    int id;
    int order;
    zcui::LayerFn fn;
    void *user;
};

std::recursive_mutex g_ui_lock;           // layers + their shared state
std::mutex g_layers_mutex;                // the layer list itself
std::vector<Layer> g_layers;
int g_next_layer_id = 1;

std::atomic<int> g_sessions(0);
std::atomic<bool> g_renderer_ready(false);
std::atomic<bool> g_attached(false);
std::atomic<bool> g_redraw(false);
std::atomic<float> g_scale(1.0f);         // display scale x user scale
std::atomic<float> g_user_scale(1.0f);
std::atomic<bool> g_capture_mouse(false);
std::atomic<bool> g_capture_keys(false);

std::mutex g_frame_mutex;
std::condition_variable g_frame_cv;
unsigned long g_frame_serial = 0;

bool g_context_made = false;
SDL_Window *g_window = NULL;

// Colours are 0xRRGGBB.
ImVec4 rgb(unsigned c, float a = 1.0f)
{
    return ImVec4(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, a);
}

// A flat dark look: neutral greys, one blue accent, rounded corners.
void apply_style(float scale)
{
    ImGuiStyle &s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);

    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 8);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 14;
    s.GrabMinSize = 12;
    s.WindowRounding = 10;
    s.ChildRounding = 8;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.ScrollbarRounding = 8;
    s.GrabRounding = 6;
    s.TabRounding = 6;
    s.WindowBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    s.SeparatorTextBorderSize = 1;

    const ImVec4 accent = rgb(0x3B82F6);
    const ImVec4 accent_hover = rgb(0x5B9BFF);
    const ImVec4 accent_active = rgb(0x2563EB);

    ImVec4 *c = s.Colors;
    c[ImGuiCol_Text]                 = rgb(0xECECF1);
    c[ImGuiCol_TextDisabled]         = rgb(0x7C7C88);
    c[ImGuiCol_WindowBg]             = rgb(0x1C1C22, 0.97f);
    c[ImGuiCol_ChildBg]              = rgb(0x000000, 0.0f);
    c[ImGuiCol_PopupBg]              = rgb(0x22222A, 0.98f);
    c[ImGuiCol_Border]               = rgb(0x3A3A46);
    c[ImGuiCol_BorderShadow]         = rgb(0x000000, 0.0f);
    c[ImGuiCol_FrameBg]              = rgb(0x2A2A33);
    c[ImGuiCol_FrameBgHovered]       = rgb(0x34343F);
    c[ImGuiCol_FrameBgActive]        = rgb(0x3D3D4A);
    c[ImGuiCol_TitleBg]              = rgb(0x16161B);
    c[ImGuiCol_TitleBgActive]        = rgb(0x16161B);
    c[ImGuiCol_TitleBgCollapsed]     = rgb(0x16161B);
    c[ImGuiCol_MenuBarBg]            = rgb(0x16161B, 0.97f);
    c[ImGuiCol_ScrollbarBg]          = rgb(0x000000, 0.0f);
    c[ImGuiCol_ScrollbarGrab]        = rgb(0x3A3A46);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(0x4A4A58);
    c[ImGuiCol_ScrollbarGrabActive]  = rgb(0x5A5A6A);
    c[ImGuiCol_CheckMark]            = rgb(0xFFFFFF);
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accent_hover;
    c[ImGuiCol_Button]               = rgb(0x30303A);
    c[ImGuiCol_ButtonHovered]        = rgb(0x3C3C48);
    c[ImGuiCol_ButtonActive]         = accent_active;
    c[ImGuiCol_Header]               = rgb(0x3B82F6, 0.35f);
    c[ImGuiCol_HeaderHovered]        = rgb(0x3B82F6, 0.55f);
    c[ImGuiCol_HeaderActive]         = accent;
    c[ImGuiCol_Separator]            = rgb(0x3A3A46);
    c[ImGuiCol_SeparatorHovered]     = accent_hover;
    c[ImGuiCol_SeparatorActive]      = accent;
    c[ImGuiCol_ResizeGrip]           = rgb(0x3B82F6, 0.20f);
    c[ImGuiCol_ResizeGripHovered]    = rgb(0x3B82F6, 0.60f);
    c[ImGuiCol_ResizeGripActive]     = accent;
    c[ImGuiCol_Tab]                  = rgb(0x26262E);
    c[ImGuiCol_TabHovered]           = rgb(0x3B82F6, 0.55f);
    c[ImGuiCol_TabSelected]          = rgb(0x30303A);
    c[ImGuiCol_TabSelectedOverline]  = accent;
    c[ImGuiCol_TabDimmed]            = rgb(0x26262E);
    c[ImGuiCol_TabDimmedSelected]    = rgb(0x30303A);
    c[ImGuiCol_PlotHistogram]        = accent;
    c[ImGuiCol_TextSelectedBg]       = rgb(0x3B82F6, 0.40f);
    c[ImGuiCol_NavCursor]            = accent_hover;
    c[ImGuiCol_ModalWindowDimBg]     = rgb(0x000000, 0.55f);

    s.ScaleAllSizes(scale);
    s.FontSizeBase = 17.0f;
    s.FontScaleDpi = scale;
}

// A clean UI font: the system's Segoe UI on Windows, ImGui's own otherwise.
void load_fonts()
{
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFont *f = NULL;
#ifdef _WIN32
    const char *windir = getenv("WINDIR");
    std::string path = std::string(windir ? windir : "C:\\Windows") + "\\Fonts\\segoeui.ttf";
    if(FILE *probe = fopen(path.c_str(), "rb"))
    {
        fclose(probe);
        f = io.Fonts->AddFontFromFileTTF(path.c_str());
    }
#elif defined(__ANDROID__)
    // The system UI font (Roboto on stock Android).
    static const char *const candidates[] = {
        "/system/fonts/Roboto-Regular.ttf", "/system/fonts/RobotoStatic-Regular.ttf",
        "/system/fonts/DroidSans.ttf", NULL };
    for(int i = 0; !f && candidates[i]; ++i)
    {
        if(FILE *probe = fopen(candidates[i], "rb"))
        {
            fclose(probe);
            f = io.Fonts->AddFontFromFileTTF(candidates[i]);
        }
    }
#endif
    if(!f)
        f = io.Fonts->AddFontDefault();
    io.FontDefault = f;
}

void make_context()
{
    if(g_context_made)
        return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = NULL;   // nothing written next to the game data
    io.LogFilename = NULL;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    load_fonts();
    g_context_made = true;
}

float window_scale(SDL_Window *w)
{
    float s = w ? SDL_GetWindowDisplayScale(w) : 1.0f;
    if(!(s > 0.25f && s < 8.0f))
        s = 1.0f;
#if defined(__ANDROID__)
    // Phones report a high density (2.5-3.5); at that scale the longer menus
    // are taller than a landscape screen, and ImGui has no drag-to-scroll
    // for fingers. Keep the layout within about 540 points of height (the
    // user's UI scale setting still multiplies this).
    int pw = 0, ph = 0;
    if(w && SDL_GetWindowSizeInPixels(w, &pw, &ph) && ph > 0)
        s = std::min(s, std::max(1.0f, ph / 540.0f));
#endif
    return s;
}

// Restyles when the display scale or the user's factor changed (platform
// thread, outside a UI frame).
void refresh_scale()
{
    float s = window_scale(g_window) * g_user_scale.load();
    if (s != g_scale.load())
    {
        g_scale = s;
        apply_style(s);
    }
}

bool is_mouse_event(Uint32 type)
{
    return type == SDL_EVENT_MOUSE_MOTION || type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
           type == SDL_EVENT_MOUSE_BUTTON_UP || type == SDL_EVENT_MOUSE_WHEEL;
}

bool is_key_event(Uint32 type)
{
    return type == SDL_EVENT_KEY_DOWN || type == SDL_EVENT_KEY_UP ||
           type == SDL_EVENT_TEXT_INPUT || type == SDL_EVENT_TEXT_EDITING;
}

bool is_input_event(Uint32 type)
{
    switch(type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP:
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        return true;
    default:
        return false;
    }
}

// Optional self-test window (environment ZPLAYER_UI_TEST=1).
void test_layer(void *)
{
    ImGui::SetNextWindowPos(ImVec2(40 * zcui::scale(), 40 * zcui::scale()), ImGuiCond_FirstUseEver);
    ImGui::Begin("Zelda Classic - Vulkan Edition");
    ImGui::Text("Dear ImGui %s over the game, %.0f fps", ImGui::GetVersion(), ImGui::GetIO().Framerate);
    static bool check = true;
    static int slider = 50;
    ImGui::Checkbox("A checkbox", &check);
    ImGui::SliderInt("A slider", &slider, 0, 100);
    ImGui::Button("A button");
    ImGui::End();
}

} // namespace

// ---------------------------------------------------------------------------
// public side
// ---------------------------------------------------------------------------

namespace zcui
{

int add_layer(LayerFn fn, void *user, int order)
{
    std::lock_guard<std::mutex> guard(g_layers_mutex);
    Layer l;
    l.id = g_next_layer_id++;
    l.order = order;
    l.fn = fn;
    l.user = user;
    g_layers.push_back(l);
    std::stable_sort(g_layers.begin(), g_layers.end(),
                     [](const Layer &a, const Layer &b) { return a.order < b.order; });
    g_redraw = true;
    return l.id;
}

void remove_layer(int id)
{
    std::lock_guard<std::mutex> guard(g_layers_mutex);
    for(size_t i = 0; i < g_layers.size(); ++i)
    {
        if(g_layers[i].id == id)
        {
            g_layers.erase(g_layers.begin() + i);
            break;
        }
    }
    g_redraw = true;
}

void lock()
{
    g_ui_lock.lock();
}

void unlock()
{
    g_ui_lock.unlock();
}

void begin_session()
{
    ++g_sessions;
    g_redraw = true;
}

void end_session()
{
    if(g_sessions.load() > 0)
        --g_sessions;
    g_redraw = true;
}

bool session_active()
{
    return g_sessions.load() > 0;
}

bool wait_frame(int timeout_ms)
{
    std::unique_lock<std::mutex> lk(g_frame_mutex);
    unsigned long start = g_frame_serial;
    return g_frame_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                               [start] { return g_frame_serial != start; });
}

void request_redraw()
{
    g_redraw = true;
}

bool available()
{
    return g_attached.load() && g_renderer_ready.load();
}

float scale()
{
    return g_scale.load();
}

void set_user_scale(float factor)
{
    if (!(factor >= 0.25f && factor <= 8.0f))
        factor = 1.0f;
    if (factor != g_user_scale.exchange(factor))
        g_redraw = true;
}

float user_scale()
{
    return g_user_scale.load();
}

void set_capture(bool mouse, bool keys)
{
    g_capture_mouse = mouse;
    g_capture_keys = keys;
}

} // namespace zcui

// ---------------------------------------------------------------------------
// platform side
// ---------------------------------------------------------------------------

namespace zcui_platform
{

bool attach(SDL_Window *window)
{
    make_context();
    if(g_attached)
        detach();
    if(!ImGui_ImplSDL3_InitForVulkan(window))
        return false;
    g_window = window;
    float s = window_scale(window) * g_user_scale.load();
    g_scale = s;
    apply_style(s);
    g_attached = true;

    static bool test_added = false;
    const char *t = getenv("ZPLAYER_UI_TEST");
    if(!test_added && t && t[0] == '1')
    {
        zcui::add_layer(test_layer, NULL, 1000);
        test_added = true;
    }
    return true;
}

void detach()
{
    if(!g_attached)
        return;
    ImGui_ImplSDL3_Shutdown();
    g_attached = false;
    g_window = NULL;
}

void set_renderer_ready(bool ready)
{
    g_renderer_ready = ready;
}

bool event(const SDL_Event &ev)
{
    if(!g_attached)
        return false;
    ImGui_ImplSDL3_ProcessEvent(&ev);
    if(ev.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
        refresh_scale();
    if(ev.type == SDL_EVENT_KEY_UP || ev.type == SDL_EVENT_MOUSE_BUTTON_UP ||
       ev.type == SDL_EVENT_GAMEPAD_BUTTON_UP || ev.type == SDL_EVENT_JOYSTICK_BUTTON_UP)
        return false;   // releases always reach the game: nothing stays held
    if(zcui::available() && !zcui::session_active())
    {
        if(g_capture_mouse.load() && is_mouse_event(ev.type))
            return true;
        if(g_capture_keys.load() && is_key_event(ev.type))
            return true;
        return false;
    }
    return zcui::session_active() && zcui::available() && is_input_event(ev.type);
}

bool wants_continuous()
{
    return zcui::available() && (zcui::session_active() || g_redraw.exchange(false));
}

ImDrawData *build_frame()
{
    if(!zcui::available())
        return NULL;

    std::vector<Layer> layers;
    {
        std::lock_guard<std::mutex> guard(g_layers_mutex);
        layers = g_layers;
    }

    refresh_scale();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    {
        std::lock_guard<std::recursive_mutex> guard(g_ui_lock);
        for(size_t i = 0; i < layers.size(); ++i)
            layers[i].fn(layers[i].user);
    }
    ImGui::Render();

    {
        std::lock_guard<std::mutex> lk(g_frame_mutex);
        ++g_frame_serial;
    }
    g_frame_cv.notify_all();

    ImDrawData *dd = ImGui::GetDrawData();
    return (dd && dd->CmdListsCount > 0) ? dd : NULL;
}

void shutdown()
{
    detach();
    if(g_context_made)
    {
        ImGui::DestroyContext();
        g_context_made = false;
    }
}

} // namespace zcui_platform

/*
 * zplayer's display settings: how the window shows the game (windowed,
 * borderless or exclusive fullscreen, its size, the monitor, the scaling,
 * vsync, UI scale). Implemented by the SDL platform
 * (src/port/platform/platform_display.cpp); the Video Settings window
 * (src/port/game/ui_video.cpp) and the game's own fullscreen toggle use it.
 *
 * None of this touches the game's graphics mode: the game keeps drawing its
 * fixed logical screen (set_gfx_mode, e.g. 640x480 8-bit) and the presenter
 * scales that picture into the window. Only the window and the monitor's
 * mode change.
 *
 * Settings source. The game side registers a loader (set_settings_loader);
 * the platform calls it on the game thread at the first set_gfx_mode(),
 * before the window is shown, so the window opens with the saved size,
 * monitor and fullscreen kind. From then on the platform "manages" the
 * window (resizable, those settings, Alt+Enter). Without a loader (the
 * conformance tests) the window behaves as before: a fixed-size window
 * centred on the primary display, fullscreen = borderless desktop.
 * Whether a mode is windowed or fullscreen is still what set_gfx_mode()
 * asked for; the settings say which fullscreen kind that means.
 *
 * Threads: every function may be called from any thread. Functions that
 * change the window run the work on the platform thread: from another
 * thread they wait for it, on the platform thread itself (an ImGui layer)
 * they queue it for right after the current frame and return at once. get()
 * always returns the latest requested settings, so a layer sees its own
 * change immediately. Never call them with the UI lock held from the game
 * thread (they may wait for the platform thread, whose layers take it).
 *
 * C++ only; includes neither SDL nor Allegro.
 */

#ifndef ZCPORT_DISPLAY_H
#define ZCPORT_DISPLAY_H

#include <string>
#include <vector>

namespace zcdisplay
{

enum Mode
{
    MODE_WINDOWED = 0,
    MODE_BORDERLESS = 1,     // fullscreen at the monitor's desktop resolution
    MODE_EXCLUSIVE = 2       // fullscreen in a display mode of its own
};

enum Scaling
{
    SCALING_INTEGER = 0,     // largest whole multiple that fits (pixel-perfect)
    SCALING_FIT = 1,         // as large as fits, aspect kept, sharp pixels
    SCALING_FIT_SMOOTH = 2,  // the same, filtered
    SCALING_STRETCH = 3      // the whole window
};

enum Renderer
{
    RENDERER_AUTO = 0,       // Vulkan, else SDL's renderer, else the window surface
    RENDERER_SDL = 1,        // SDL's renderer (Direct3D, OpenGL, Metal), not Vulkan
    RENDERER_SOFTWARE = 2    // SDL's software renderer (no graphics driver needed)
};

enum TouchMode
{
    TOUCH_AUTO = 0,          // shown after a touch, hidden when a key or gamepad is used
    TOUCH_ALWAYS = 1,
    TOUCH_NEVER = 2
};

struct Settings
{
    int mode;                // Mode now (or for the next window)
    int fullscreen_mode;     // MODE_BORDERLESS or MODE_EXCLUSIVE: what
                             // "fullscreen" means (Alt+Enter, the game's
                             // Fullscreen menu item, set_gfx_mode)
    int window_w, window_h;  // windowed client size in pixels; 0 = the
                             // game's screen size
    int exclusive_w, exclusive_h;  // exclusive mode; 0 = the monitor's
                                   // desktop mode
    float exclusive_hz;      // its refresh rate; 0 = the highest
    int display;             // monitor, counted from the left like zc.cfg's
                             // window_display (1 = leftmost, see
                             // monitors()); 0 = the primary display at
                             // start, then wherever the window is
    int scaling;             // Scaling
    bool vsync;              // present on the display's refresh
    bool show_fps;           // frame-rate overlay
    float ui_scale;          // multiplies the display's own UI scale
    int touch_mode;          // TouchMode: the on-screen gamepad
    float touch_size;        // its size (1.0 = 100 %)
    float touch_opacity;     // its opacity (0.1 .. 1.0)
    int renderer;            // Renderer; takes effect at the next start
};

/* What a new installation gets. */
Settings defaults();

struct Monitor
{
    int index;               // 1 = leftmost (then top to bottom)
    std::string name;
    int x, y, w, h;          // bounds on the desktop
    int desktop_w, desktop_h;
    float desktop_hz;
    int max_client_w, max_client_h;  // largest windowed client area (pixels)
                                     // that fits its usable area
    bool primary;
};

struct DisplayMode
{
    int w, h;
    float hz;
};

/* The connected monitors, ordered as window_display counts them. */
std::vector<Monitor> monitors();

/* Exclusive fullscreen modes of monitor 'display' (1-based), largest
 * first, without duplicates. */
std::vector<DisplayMode> fullscreen_modes(int display);

/* The monitor the window is on now (1-based), 0 without a window. */
int window_monitor();

/* Changes whenever monitors are added, removed or change mode. */
unsigned monitors_serial();

/* The settings (the latest requested; see above). */
Settings get();

/* Request new settings; the window follows (see "Threads"). Fields the
 * platform cannot honour are corrected (e.g. the closest exclusive mode). */
void set(const Settings &s);

/* Windowed <-> the chosen fullscreen kind (Alt+Enter, the game's toggle). */
void toggle_fullscreen();

/* The window is (being made) fullscreen. */
bool is_fullscreen();

/* True when the window's windowed/fullscreen state differs from the one
 * the first graphics mode opened with (the user changed it this run). */
bool fullscreen_changed();

/* Number of Alt+Enter toggles so far. */
unsigned hotkey_toggles();

/* True once the loader ran (the platform manages the window). */
bool managed();

/* The game's screen size (0x0 without a mode) and the display's refresh
 * rate, for the settings window. */
void logical_size(int *w, int *h);
int refresh_hz();

/* What shows the game now ("Vulkan, <GPU>", "Direct3D 11", "Software",
 * ...), and the Renderer setting the window was made with. */
std::string renderer_in_use();
int renderer_at_start();

/* Game side, see "Settings source". Called with *s = defaults(). */
typedef void (*LoadSettingsFn)(Settings *s);
void set_settings_loader(LoadSettingsFn fn);

} // namespace zcdisplay

#endif /* ZCPORT_DISPLAY_H */

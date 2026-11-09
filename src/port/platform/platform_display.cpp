// zcport SDL platform: zplayer's display settings (zcport/display.h).
//
// The window side of the Video Settings: windowed / borderless / exclusive
// fullscreen, the window size, the monitor, the scaling, vsync and the UI
// scale, plus Alt+Enter. The game's graphics mode is never touched: the
// game keeps its logical screen and the presenter scales it.
//
// State. g_want holds the requested settings (any thread, under g_mutex);
// the platform thread applies the difference to what the window has
// (g_applied, platform thread only) and writes back what it could not
// honour (the closest exclusive mode, a window clamped to the monitor).
// Resizing the window by hand updates both, so it is never undone.
//
// Written from scratch for zcport.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <allegro.h>

#ifdef _WIN32
   #ifndef NOMINMAX
      #define NOMINMAX
   #endif
   #include <winalleg.h>
#endif

#include <SDL3/SDL.h>

#include "zcport/display.h"
#include "zcport/ui.h"
#include "platform_sdl_internal.h"

using zcdisplay::Settings;

namespace
{

std::mutex g_mutex;                         // the fields up to g_fs_at_open
Settings g_want = zcdisplay::defaults();
bool g_managed = false;
bool g_loaded = false;
zcdisplay::LoadSettingsFn g_loader = NULL;
bool g_opened = false;                      // a managed mode was opened
bool g_fs_at_open = false;                  // ... fullscreen

std::atomic<unsigned> g_hotkeys(0);
std::atomic<unsigned> g_monitors_serial(1);

// platform thread only
Settings g_applied;
bool g_applied_valid = false;
uint64_t g_ignore_resize_until = 0;         // SDL_GetTicks()
int g_border[4] = { 31, 8, 8, 8 };          // top left bottom right, last seen

uint64_t ticks_ms()
{
   return SDL_GetTicks();
}

zcport::ScaleMode to_scale_mode(int s)
{
   switch (s) {
      case zcdisplay::SCALING_INTEGER:    return zcport::SCALE_INTEGER;
      case zcdisplay::SCALING_FIT_SMOOTH: return zcport::SCALE_ASPECT_SMOOTH;
      case zcdisplay::SCALING_STRETCH:    return zcport::SCALE_STRETCH;
      default:                            return zcport::SCALE_ASPECT;
   }
}

void normalize(Settings &s)
{
   if (s.mode < zcdisplay::MODE_WINDOWED || s.mode > zcdisplay::MODE_EXCLUSIVE)
      s.mode = zcdisplay::MODE_WINDOWED;
   if (s.fullscreen_mode != zcdisplay::MODE_EXCLUSIVE)
      s.fullscreen_mode = zcdisplay::MODE_BORDERLESS;
   if (s.window_w < 160 || s.window_h < 120 || s.window_w > 16384 || s.window_h > 16384)
      s.window_w = s.window_h = 0;
   if (s.exclusive_w < 160 || s.exclusive_h < 120 || s.exclusive_w > 16384 || s.exclusive_h > 16384)
      s.exclusive_w = s.exclusive_h = 0;
   if (!(s.exclusive_hz >= 0.0f && s.exclusive_hz <= 1000.0f))
      s.exclusive_hz = 0.0f;
   if (s.display < 0 || s.display > 64)
      s.display = 0;
   if (s.scaling < zcdisplay::SCALING_INTEGER || s.scaling > zcdisplay::SCALING_STRETCH)
      s.scaling = zcdisplay::SCALING_FIT;
   if (!(s.ui_scale >= 0.5f && s.ui_scale <= 3.0f))
      s.ui_scale = 1.0f;
}

bool same_hz(float a, float b)
{
   return std::fabs(a - b) < 0.01f;
}

// The window-related fields that differ need the window reconfigured.
bool window_differs(const Settings &a, const Settings &b)
{
   if (a.mode != b.mode || a.display != b.display)
      return true;
   if (a.mode == zcdisplay::MODE_WINDOWED && (a.window_w != b.window_w || a.window_h != b.window_h))
      return true;
   if (a.mode == zcdisplay::MODE_EXCLUSIVE &&
       (a.exclusive_w != b.exclusive_w || a.exclusive_h != b.exclusive_h || !same_hz(a.exclusive_hz, b.exclusive_hz)))
      return true;
   return false;
}

// Copies the window fields the platform corrected ('done', from 'asked')
// into g_want, unless a newer request changed them meanwhile.
void write_back(const Settings &asked, const Settings &done)
{
   std::lock_guard<std::mutex> guard(g_mutex);
   if (window_differs(g_want, asked) || g_want.fullscreen_mode != asked.fullscreen_mode)
      return;
   g_want.mode = done.mode;
   g_want.window_w = done.window_w;
   g_want.window_h = done.window_h;
   g_want.exclusive_w = done.exclusive_w;
   g_want.exclusive_h = done.exclusive_h;
   g_want.exclusive_hz = done.exclusive_hz;
}

// ---------------------------------------------------------------------------
// monitors (platform thread)
// ---------------------------------------------------------------------------

struct Screen
{
   SDL_DisplayID id;
   SDL_Rect bounds;
};

// Left to right, then top to bottom: the order of zc.cfg's window_display
// (zc_place_window_on_display() in src/zsys.cpp).
std::vector<Screen> sorted_screens()
{
   std::vector<Screen> out;
   int count = 0;
   SDL_DisplayID *ids = SDL_GetDisplays(&count);
   for (int i = 0; ids && i < count; i++) {
      Screen s;
      s.id = ids[i];
      if (SDL_GetDisplayBounds(ids[i], &s.bounds))
         out.push_back(s);
   }
   SDL_free(ids);
   std::stable_sort(out.begin(), out.end(), [](const Screen &a, const Screen &b) {
      if (a.bounds.x != b.bounds.x)
         return a.bounds.x < b.bounds.x;
      return a.bounds.y < b.bounds.y;
   });
   return out;
}

SDL_DisplayID screen_id(int index)
{
   std::vector<Screen> s = sorted_screens();
   if (index < 1 || index > (int)s.size())
      return 0;
   return s[index - 1].id;
}

int screen_index(SDL_DisplayID id)
{
   std::vector<Screen> s = sorted_screens();
   for (size_t i = 0; i < s.size(); i++)
      if (s[i].id == id)
         return (int)i + 1;
   return 0;
}

float pixel_density(SDL_Window *win)
{
   float d = win ? SDL_GetWindowPixelDensity(win) : 1.0f;
   return d > 0.0f ? d : 1.0f;
}

void remember_borders(SDL_Window *win)
{
   if (SDL_GetWindowFlags(win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS))
      return;
   int t = 0, l = 0, b = 0, r = 0;
   if (SDL_GetWindowBordersSize(win, &t, &l, &b, &r) && (t | l | b | r)) {
      g_border[0] = t;
      g_border[1] = l;
      g_border[2] = b;
      g_border[3] = r;
   }
}

// Largest windowed client area (window coordinates) that fits the usable
// area of 'id'.
void max_client(SDL_DisplayID id, int *w, int *h)
{
   SDL_Rect work;
   if (!SDL_GetDisplayUsableBounds(id, &work) && !SDL_GetDisplayBounds(id, &work)) {
      *w = *h = 0;
      return;
   }
   *w = std::max(0, work.w - g_border[1] - g_border[3]);
   *h = std::max(0, work.h - g_border[0] - g_border[2]);
}

// Centres the windowed window in the usable area of 'id', keeping its title
// bar on screen (zc_place_window_on_display() in src/zsys.cpp does the same
// with Win32). Returns the outer position.
void center_on(SDL_Window *win, SDL_DisplayID id, int *ox, int *oy)
{
   SDL_Rect work;
   if (!SDL_GetDisplayUsableBounds(id, &work))
      SDL_GetDisplayBounds(id, &work);
   int top = 0, left = 0, bottom = 0, right = 0;
   SDL_GetWindowBordersSize(win, &top, &left, &bottom, &right);
   int cw = 0, ch = 0;
   SDL_GetWindowSize(win, &cw, &ch);
   int ow = cw + left + right, oh = ch + top + bottom;
   int x = work.x + (work.w - ow) / 2;
   int y = work.y + (work.h - oh) / 2;
   // A window bigger than the screen still keeps its title bar on screen.
   if (x < work.x) x = work.x;
   if (y < work.y) y = work.y;
   SDL_SetWindowPosition(win, x + left, y + top);
   SDL_SyncWindow(win);
   if (ox) *ox = x;
   if (oy) *oy = y;
}

// The exclusive mode of 'id' closest to w x h at hz (0x0: the desktop mode;
// hz 0: the highest rate for that size).
bool pick_mode(SDL_DisplayID id, int w, int h, float hz, SDL_DisplayMode *out)
{
   if (w <= 0 || h <= 0) {
      const SDL_DisplayMode *d = SDL_GetDesktopDisplayMode(id);
      if (!d)
         return false;
      w = d->w;
      h = d->h;
      if (hz <= 0.0f)
         hz = d->refresh_rate;
   }
   int n = 0;
   SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(id, &n);
   const SDL_DisplayMode *best = NULL;
   for (int i = 0; modes && i < n; i++) {
      const SDL_DisplayMode *m = modes[i];
      if (m->w != w || m->h != h || m->pixel_density > 1.0f)
         continue;
      if (!best)
         best = m;
      else if (hz > 0.0f ? std::fabs(m->refresh_rate - hz) < std::fabs(best->refresh_rate - hz)
                         : m->refresh_rate > best->refresh_rate)
         best = m;
   }
   bool ok = false;
   if (best) {
      *out = *best;
      ok = true;
   }
   else if (SDL_GetClosestFullscreenDisplayMode(id, w, h, hz, false, out)) {
      ok = true;
   }
   SDL_free(modes);
   return ok;
}

// ---------------------------------------------------------------------------
// the window (platform thread)
// ---------------------------------------------------------------------------

// Gives the window the mode, size and monitor of 's' and corrects 's' to
// what it got. 'opening': the window is shown for a new graphics mode of
// lw x lh (the first one opens on the primary display unless a monitor is
// set; later ones stay where the window is).
void configure_window(Settings &s, bool opening, int lw, int lh)
{
   SDL_Window *win = zcport_sdl::window();
   if (!win)
      return;
   static bool shown_once = false;
   const SDL_WindowFlags flags = SDL_GetWindowFlags(win);
   bool fullscreen_now = (flags & SDL_WINDOW_FULLSCREEN) != 0;
   SDL_DisplayID current = SDL_GetDisplayForWindow(win);
   SDL_DisplayID target = s.display > 0 ? screen_id(s.display) : 0;
   if (!target)
      target = (opening && !shown_once) ? SDL_GetPrimaryDisplay() : current;
   if (!target)
      target = SDL_GetPrimaryDisplay();
   const float den = pixel_density(win);

   if (s.mode == zcdisplay::MODE_WINDOWED) {
      if (fullscreen_now) {
         SDL_SetWindowFullscreen(win, false);
         SDL_SyncWindow(win);
      }
      if (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED) {
         SDL_RestoreWindow(win);
         SDL_SyncWindow(win);
      }
      SDL_SetWindowResizable(win, true);
      SDL_SetWindowMinimumSize(win, (int)(320 / den), (int)(240 / den));
      int pw = s.window_w > 0 ? s.window_w : lw;
      int ph = s.window_h > 0 ? s.window_h : lh;
      int mw = 0, mh = 0;
      max_client(target, &mw, &mh);
      mw = (int)(mw * den);
      mh = (int)(mh * den);
      if (mw > 0 && mh > 0 && (pw > mw || ph > mh)) {
         // Too big for that monitor: as large as fits, same shape.
         double f = std::min((double)mw / pw, (double)mh / ph);
         pw = std::max(320, (int)(pw * f));
         ph = std::max(240, (int)(ph * f));
      }
      SDL_SetWindowSize(win, (int)std::lround(pw / den), (int)std::lround(ph / den));
      SDL_SyncWindow(win);
      // Placed before it shows (a new window would flash where Windows
      // puts it), and again once shown in case the frame changed.
      center_on(win, target, NULL, NULL);
      SDL_ShowWindow(win);
      SDL_SyncWindow(win);
      remember_borders(win);
      center_on(win, target, NULL, NULL);
      int apw = 0, aph = 0;
      SDL_GetWindowSizeInPixels(win, &apw, &aph);
      if (apw > 0 && aph > 0) {
         s.window_w = apw;
         s.window_h = aph;
      }
   }
   else {
      bool exclusive = s.mode == zcdisplay::MODE_EXCLUSIVE;
      SDL_DisplayMode mode;
      SDL_zero(mode);
      if (exclusive && !pick_mode(target, s.exclusive_w, s.exclusive_h, s.exclusive_hz, &mode)) {
         al_trace("zplayer display: no exclusive mode on that monitor (%s), using borderless\n", SDL_GetError());
         exclusive = false;
      }
      if (!fullscreen_now)
         remember_borders(win);
      if (fullscreen_now && current != target) {
         // To another monitor: leave fullscreen first.
         SDL_SetWindowFullscreen(win, false);
         SDL_SyncWindow(win);
         fullscreen_now = false;
      }
      if (!fullscreen_now)
         SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(target), SDL_WINDOWPOS_CENTERED_DISPLAY(target));
      if (!SDL_SetWindowFullscreenMode(win, exclusive ? &mode : NULL)) {
         al_trace("zplayer display: fullscreen mode refused (%s)\n", SDL_GetError());
         if (exclusive) {
            exclusive = false;
            SDL_SetWindowFullscreenMode(win, NULL);
         }
      }
      SDL_ShowWindow(win);
      SDL_SetWindowFullscreen(win, true);
      SDL_SyncWindow(win);
      if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) && exclusive) {
         al_trace("zplayer display: exclusive %dx%d failed (%s), using borderless\n", mode.w, mode.h, SDL_GetError());
         exclusive = false;
         SDL_SetWindowFullscreenMode(win, NULL);
         SDL_SetWindowFullscreen(win, true);
         SDL_SyncWindow(win);
      }
      s.mode = exclusive ? zcdisplay::MODE_EXCLUSIVE : zcdisplay::MODE_BORDERLESS;
      if (exclusive) {
         s.exclusive_w = mode.w;
         s.exclusive_h = mode.h;
         s.exclusive_hz = mode.refresh_rate;
      }
   }
   shown_once = true;
   // The resizes this caused arrive as events later: not the user's.
   g_ignore_resize_until = ticks_ms() + 600;
   zcport_sdl::window_reconfigured(s.mode == zcdisplay::MODE_WINDOWED);

   int ww = 0, wh = 0;
   SDL_GetWindowSizeInPixels(win, &ww, &wh);
   al_trace("zplayer display: %s %dx%d (%d Hz) on monitor %d\n",
            s.mode == zcdisplay::MODE_WINDOWED ? "windowed" :
            s.mode == zcdisplay::MODE_BORDERLESS ? "borderless" : "exclusive",
            ww, wh, zcport_sdl::window_refresh_hz(), screen_index(SDL_GetDisplayForWindow(win)));
}

// Scaling, vsync and UI scale ('all': whatever was applied before).
void apply_picture(const Settings &s, bool all)
{
   if (all || s.scaling != g_applied.scaling) {
      zcport::ScaleMode m = to_scale_mode(s.scaling);
      zcport_sdl::set_scale_modes(m, m);
   }
   if (all || s.vsync != g_applied.vsync)
      zcport_sdl::set_vsync(s.vsync);
   if (all || s.ui_scale != g_applied.ui_scale)
      zcui::set_user_scale(s.ui_scale);
}

// Applies what g_want asks for and the window does not have yet.
void apply_now()
{
   Settings s;
   {
      std::lock_guard<std::mutex> guard(g_mutex);
      if (!g_managed)
         return;
      s = g_want;
   }
   if (!g_applied_valid) {
      // No mode was opened yet: display_open() does the window.
      apply_picture(s, true);
      return;
   }
   const Settings asked = s;
   apply_picture(s, false);
   if (!zcport_sdl::window() || !zcport_sdl::mode_active()) {
      // No mode: the window part waits for the next display_open().
      Settings had = g_applied;
      g_applied = s;
      g_applied.mode = had.mode;
      g_applied.display = had.display;
      g_applied.window_w = had.window_w;
      g_applied.window_h = had.window_h;
      g_applied.exclusive_w = had.exclusive_w;
      g_applied.exclusive_h = had.exclusive_h;
      g_applied.exclusive_hz = had.exclusive_hz;
      return;
   }
   if (window_differs(s, g_applied)) {
      int lw = 0, lh = 0;
      zcport_sdl::logical_size(&lw, &lh);
      configure_window(s, false, lw, lh);
      write_back(asked, s);
   }
   g_applied = s;
}

// Applies g_want on the platform thread: right after the current UI frame
// when called there (a layer), at once and waiting otherwise.
void request_apply()
{
   if (!zcport_sdl::running())
      return;
   if (zcport_sdl::on_platform_thread())
      zcport_sdl::post([] { apply_now(); });
   else
      zcport_sdl::run([] { apply_now(); });
}

void toggle_want()
{
   std::lock_guard<std::mutex> guard(g_mutex);
   if (g_want.mode == zcdisplay::MODE_WINDOWED)
      g_want.mode = g_want.fullscreen_mode == zcdisplay::MODE_EXCLUSIVE ? zcdisplay::MODE_EXCLUSIVE
                                                                       : zcdisplay::MODE_BORDERLESS;
   else
      g_want.mode = zcdisplay::MODE_WINDOWED;
}

} // namespace

// ---------------------------------------------------------------------------
// platform side (platform_sdl_internal.h)
// ---------------------------------------------------------------------------

namespace zcport_sdl
{

bool display_managed()
{
   std::lock_guard<std::mutex> guard(g_mutex);
   return g_managed;
}

void display_before_open()
{
   zcdisplay::LoadSettingsFn fn;
   {
      std::lock_guard<std::mutex> guard(g_mutex);
      if (g_loaded || !g_loader)
         return;
      g_loaded = true;
      fn = g_loader;
   }
   Settings s = zcdisplay::defaults();
   fn(&s);
   normalize(s);
   std::lock_guard<std::mutex> guard(g_mutex);
   g_want = s;
   g_managed = true;
}

int display_open(int w, int h, bool windowed, std::string *error)
{
   Settings s;
   {
      std::lock_guard<std::mutex> guard(g_mutex);
      g_want.mode = windowed ? zcdisplay::MODE_WINDOWED : g_want.fullscreen_mode;
      if (!g_opened) {
         g_opened = true;
         g_fs_at_open = !windowed;
      }
      s = g_want;
   }
   const Settings asked = s;
   configure_window(s, true, w, h);

#ifdef _WIN32
   if (windowed) {
      // Windows' minimum window width (SM_CXMINTRACK, frame included): the
      // reference cannot open a narrower windowed mode and fails.
      SDL_Window *win = window();
      int bt = 0, bl = 0, bb = 0, br = 0;
      SDL_GetWindowBordersSize(win, &bt, &bl, &bb, &br);
      if (w + bl + br < GetSystemMetrics(SM_CXMINTRACK)) {
         char buf[128];
         snprintf(buf, sizeof(buf), "Unable to create a %dx%d window", w, h);
         *error = buf;
         return -1;
      }
   }
#else
   (void)error;
#endif

   apply_picture(s, !g_applied_valid);
   write_back(asked, s);
   g_applied = s;
   g_applied_valid = true;
   return 0;
}

bool display_event(const SDL_Event &ev)
{
   switch (ev.type) {
      case SDL_EVENT_KEY_DOWN: {
         // Alt+Enter: windowed <-> fullscreen, at any time. Neither the game
         // nor the UI sees the Enter.
         const SDL_Keymod mod = ev.key.mod;
         if ((ev.key.scancode == SDL_SCANCODE_RETURN || ev.key.scancode == SDL_SCANCODE_KP_ENTER) &&
             (mod & SDL_KMOD_ALT) && !(mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) &&
             display_managed() && window() && mode_active()) {
            if (!ev.key.repeat) {
               toggle_want();
               g_hotkeys++;
               apply_now();
            }
            return true;
         }
         break;
      }

      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
         // The user resized the window: that is the new window size.
         SDL_Window *win = window();
         if (!win || ev.window.windowID != SDL_GetWindowID(win) || !g_applied_valid ||
             g_applied.mode != zcdisplay::MODE_WINDOWED || ticks_ms() < g_ignore_resize_until)
            break;
         if (SDL_GetWindowFlags(win) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED))
            break;
         int pw = 0, ph = 0;
         SDL_GetWindowSizeInPixels(win, &pw, &ph);
         if (pw < 1 || ph < 1)
            break;
         std::lock_guard<std::mutex> guard(g_mutex);
         if (!g_managed)
            break;
         if (g_want.mode == zcdisplay::MODE_WINDOWED &&
             g_want.window_w == g_applied.window_w && g_want.window_h == g_applied.window_h) {
            g_want.window_w = pw;
            g_want.window_h = ph;
         }
         g_applied.window_w = pw;
         g_applied.window_h = ph;
         break;
      }

      case SDL_EVENT_WINDOW_DISPLAY_CHANGED: {
         // The user dragged the window to another monitor: when a monitor
         // is set, it follows (and is saved as window_display).
         SDL_Window *win = window();
         if (!win || ev.window.windowID != SDL_GetWindowID(win) || !g_applied_valid ||
             g_applied.mode != zcdisplay::MODE_WINDOWED || ticks_ms() < g_ignore_resize_until ||
             (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN))
            break;
         int idx = screen_index(SDL_GetDisplayForWindow(win));
         std::lock_guard<std::mutex> guard(g_mutex);
         if (!g_managed || idx < 1 || g_applied.display == 0)
            break;
         if (g_want.display == g_applied.display)
            g_want.display = idx;
         g_applied.display = idx;
         break;
      }

      default:
         if (ev.type >= SDL_EVENT_DISPLAY_FIRST && ev.type <= SDL_EVENT_DISPLAY_LAST)
            g_monitors_serial++;
         break;
   }
   return false;
}

bool place_on_display(int display, int *count)
{
   SDL_Window *win = window();
   std::vector<Screen> screens = sorted_screens();
   if (count)
      *count = (int)screens.size();
   if (!win || display < 1 || display > (int)screens.size())
      return false;
   int x = 0, y = 0;
   center_on(win, screens[display - 1].id, &x, &y);
   al_trace("window_display: moved the window to screen %d (from the left) at %d,%d\n", display, x, y);
   return true;
}

} // namespace zcport_sdl

// ---------------------------------------------------------------------------
// public side (zcport/display.h)
// ---------------------------------------------------------------------------

namespace zcdisplay
{

Settings defaults()
{
   Settings s;
   s.mode = MODE_WINDOWED;
   s.fullscreen_mode = MODE_BORDERLESS;
   s.window_w = s.window_h = 0;
   s.exclusive_w = s.exclusive_h = 0;
   s.exclusive_hz = 0.0f;
   s.display = 0;
   s.scaling = SCALING_FIT;
   s.vsync = false;
   s.show_fps = false;
   s.ui_scale = 1.0f;
   s.touch_mode = TOUCH_AUTO;
   s.touch_size = 1.0f;
   s.touch_opacity = 0.6f;
   return s;
}

std::vector<Monitor> monitors()
{
   std::vector<Monitor> out;
   if (!zcport_sdl::running())
      return out;
   zcport_sdl::run([&out] {
      std::vector<Screen> screens = sorted_screens();
      SDL_DisplayID primary = SDL_GetPrimaryDisplay();
      SDL_Window *win = zcport_sdl::window();
      if (win)
         remember_borders(win);
      const float den = pixel_density(win);
      for (size_t i = 0; i < screens.size(); i++) {
         Monitor m;
         m.index = (int)i + 1;
         const char *name = SDL_GetDisplayName(screens[i].id);
         m.name = name ? name : "";
         m.x = screens[i].bounds.x;
         m.y = screens[i].bounds.y;
         m.w = screens[i].bounds.w;
         m.h = screens[i].bounds.h;
         const SDL_DisplayMode *d = SDL_GetDesktopDisplayMode(screens[i].id);
         m.desktop_w = d ? d->w : m.w;
         m.desktop_h = d ? d->h : m.h;
         m.desktop_hz = d ? d->refresh_rate : 0.0f;
         int cw = 0, ch = 0;
         max_client(screens[i].id, &cw, &ch);
         m.max_client_w = (int)(cw * den);
         m.max_client_h = (int)(ch * den);
         m.primary = screens[i].id == primary;
         out.push_back(m);
      }
   });
   return out;
}

std::vector<DisplayMode> fullscreen_modes(int display)
{
   std::vector<DisplayMode> out;
   if (!zcport_sdl::running())
      return out;
   zcport_sdl::run([&out, display] {
      SDL_DisplayID id = screen_id(display);
      if (!id)
         return;
      int n = 0;
      SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(id, &n);
      for (int i = 0; modes && i < n; i++) {
         const SDL_DisplayMode *m = modes[i];
         if (m->pixel_density > 1.0f)
            continue;
         bool dup = false;
         for (size_t j = 0; j < out.size() && !dup; j++)
            dup = out[j].w == m->w && out[j].h == m->h && same_hz(out[j].hz, m->refresh_rate);
         if (dup)
            continue;
         DisplayMode d;
         d.w = m->w;
         d.h = m->h;
         d.hz = m->refresh_rate;
         out.push_back(d);
      }
      SDL_free(modes);
   });
   return out;
}

int window_monitor()
{
   int r = 0;
   if (!zcport_sdl::running())
      return 0;
   zcport_sdl::run([&r] {
      SDL_Window *win = zcport_sdl::window();
      if (win)
         r = screen_index(SDL_GetDisplayForWindow(win));
   });
   return r;
}

unsigned monitors_serial()
{
   return g_monitors_serial.load();
}

Settings get()
{
   std::lock_guard<std::mutex> guard(g_mutex);
   return g_want;
}

void set(const Settings &s)
{
   Settings n = s;
   normalize(n);
   {
      std::lock_guard<std::mutex> guard(g_mutex);
      g_want = n;
   }
   request_apply();
}

void toggle_fullscreen()
{
   toggle_want();
   request_apply();
}

bool is_fullscreen()
{
   {
      std::lock_guard<std::mutex> guard(g_mutex);
      if (g_managed)
         return g_want.mode != MODE_WINDOWED;
   }
   bool fs = false;
   if (zcport_sdl::running())
      zcport_sdl::run([&fs] {
         SDL_Window *win = zcport_sdl::window();
         fs = win && (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN);
      });
   return fs;
}

bool fullscreen_changed()
{
   std::lock_guard<std::mutex> guard(g_mutex);
   return g_opened && ((g_want.mode != MODE_WINDOWED) != g_fs_at_open);
}

unsigned hotkey_toggles()
{
   return g_hotkeys.load();
}

bool managed()
{
   return zcport_sdl::display_managed();
}

void logical_size(int *w, int *h)
{
   zcport_sdl::logical_size(w, h);
}

int refresh_hz()
{
   return zcport_sdl::window_refresh_hz();
}

void set_settings_loader(LoadSettingsFn fn)
{
   std::lock_guard<std::mutex> guard(g_mutex);
   g_loader = fn;
}

} // namespace zcdisplay

// zcport SDL platform: fills the zcport/platform.h hook table with SDL3
// (window, events, joysticks) and zcport::VulkanPresenter (display).
//
// Threads (see zcport/platform_sdl.h for the contract):
//   - the platform thread initialises SDL (it is SDL's main thread), owns
//     the window and the presenter, runs the event loop and feeds input to
//     the foundation (zcport/input.h) as events arrive, so key[] / mouse_x
//     change while the game busy-waits on them, as with Allegro's own input
//     thread;
//   - the game thread converts the screen to BGRA through _current_palette
//     (with the mouse pointer) at most once per display refresh, from
//     pump_events (keypressed, rest, poll_joystick, vsync ...), and hands the
//     frame to the platform thread, which presents it at once without ever
//     blocking the game (mailbox / immediate present);
//   - if no frame came for 100 ms, the platform thread copies the screen
//     itself (without the pointer) at the refresh rate, so a busy loop or a
//     long load never freezes the window;
//   - sound: one SDL audio stream on the default playback device, opened
//     and closed on the platform thread; SDL's audio thread pulls the
//     zcport mixer's output through the stream's callback, and audio_lock
//     is the stream's lock (held by SDL around that callback).
//
// Written from scratch for zcport (no Allegro code).

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <allegro.h>
#include <allegro/internal/aintern.h>
#include "zcport/core.h"
#include "zcport/input.h"
#include "zcport/midi_backend.h"
#include "zcport/platform.h"
#include "zcport/platform_sdl.h"

#ifdef _WIN32
   #ifndef NOMINMAX
      #define NOMINMAX
   #endif
   #include <winalleg.h>
#endif
#if defined(__APPLE__)
   #include <pthread.h>
   #include <cstdlib>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#define SDL_MAIN_HANDLED 1     /* no main() redirection: only SDL_SetMainReady() */
#include <SDL3/SDL_main.h>

#include "vk_present.h"
#include "sdl_present.h"
#include "ui_platform.h"
#include "touch_internal.h"
#include "zcport/display.h"
#include "zcport/ui.h"
#include "platform_sdl_internal.h"

namespace
{

typedef std::chrono::steady_clock Clock;

uint64_t now_us()
{
   static const Clock::time_point t0 = Clock::now();
   return (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
}

// ---------------------------------------------------------------------------
// pixel conversion (BITMAP -> 0xAARRGGBB)
// ---------------------------------------------------------------------------

// 6-bit palette component -> 8 bits, as the reference build's display
// shows it (bit replication: 1 -> 4, 16 -> 65, 32 -> 130, 63 -> 255).
inline uint32_t expand6(int v)
{
   v &= 63;
   return (uint32_t)((v << 2) | (v >> 4));
}

inline uint32_t expand5(int v)
{
   v &= 31;
   return (uint32_t)((v << 3) | (v >> 2));
}

void build_lut(const RGB *pal, uint32_t lut[256])
{
   for (int i = 0; i < 256; i++)
      lut[i] = 0xFF000000u | (expand6(pal[i].r) << 16) | (expand6(pal[i].g) << 8) | expand6(pal[i].b);
}

inline uint32_t pixel_to_argb(BITMAP *bmp, const unsigned char *row, int x, const uint32_t *lut)
{
   switch (bitmap_color_depth(bmp)) {
      case 8:
         return lut[row[x]];
      case 15: {
         unsigned c = ((const uint16_t *)row)[x];
         return 0xFF000000u | (expand5(c >> 10) << 16) | (expand5(c >> 5) << 8) | expand5(c);
      }
      case 16: {
         unsigned c = ((const uint16_t *)row)[x];
         unsigned g = (c >> 5) & 63;
         return 0xFF000000u | (expand5(c >> 11) << 16) | (((g << 2) | (g >> 4)) << 8) | expand5(c);
      }
      case 24: {
         const unsigned char *p = row + x * 3;
         return 0xFF000000u | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
      }
      case 32:
         return 0xFF000000u | (((const uint32_t *)row)[x] & 0xFFFFFFu);
   }
   return 0xFF000000u;
}

void convert_bitmap(BITMAP *bmp, const uint32_t *lut, uint32_t *dst)
{
   const int w = bmp->w, h = bmp->h;
   const int depth = bitmap_color_depth(bmp);
   for (int y = 0; y < h; y++) {
      const unsigned char *row = bmp->line[y];
      uint32_t *out = dst + (size_t)y * w;
      if (depth == 8) {
         for (int x = 0; x < w; x++)
            out[x] = lut[row[x]];
      }
      else if (depth == 32) {
         const uint32_t *r32 = (const uint32_t *)row;
         for (int x = 0; x < w; x++)
            out[x] = 0xFF000000u | (r32[x] & 0xFFFFFFu);
      }
      else {
         for (int x = 0; x < w; x++)
            out[x] = pixel_to_argb(bmp, row, x, lut);
      }
   }
}

// draw_sprite() of 'spr' at (sx, sy) onto a w x h frame.
void draw_pointer(BITMAP *spr, int sx, int sy, const uint32_t *lut, uint32_t *dst, int w, int h)
{
   const int mask = bitmap_mask_color(spr);
   const int depth = bitmap_color_depth(spr);
   for (int y = 0; y < spr->h; y++) {
      int dy = sy + y;
      if (dy < 0 || dy >= h)
         continue;
      const unsigned char *row = spr->line[y];
      for (int x = 0; x < spr->w; x++) {
         int dx = sx + x;
         if (dx < 0 || dx >= w)
            continue;
         int raw;
         switch (depth) {
            case 8:  raw = row[x]; break;
            case 15:
            case 16: raw = ((const uint16_t *)row)[x]; break;
            case 24: raw = row[x * 3] | (row[x * 3 + 1] << 8) | (row[x * 3 + 2] << 16); break;
            default: raw = (int)(((const uint32_t *)row)[x] & 0xFFFFFFu); break;
         }
         if (raw == (mask & (depth == 32 ? 0xFFFFFF : -1)))
            continue;
         dst[(size_t)dy * w + dx] = pixel_to_argb(spr, row, x, lut);
      }
   }
}

// Same placement as zcport::VulkanPresenter (vk_present.cpp dest_rect).
void dest_rect(int ew, int eh, int w, int h, zcport::ScaleMode mode,
               float *x0, float *y0, float *x1, float *y1)
{
   float dw = (float)ew, dh = (float)eh;
   if (w <= 0 || h <= 0 || ew <= 0 || eh <= 0) {
      *x0 = *y0 = 0;
      *x1 = (float)ew;
      *y1 = (float)eh;
      return;
   }
   if (mode == zcport::SCALE_INTEGER) {
      int s = std::min(ew / w, eh / h);
      if (s < 1)
         mode = zcport::SCALE_ASPECT;
      else {
         dw = (float)(w * s);
         dh = (float)(h * s);
      }
   }
   if (mode == zcport::SCALE_ASPECT || mode == zcport::SCALE_ASPECT_SMOOTH) {
      float s = std::min((float)ew / w, (float)eh / h);
      dw = w * s;
      dh = h * s;
   }
   *x0 = (float)(int)((ew - dw) / 2);
   *y0 = (float)(int)((eh - dh) / 2);
   *x1 = *x0 + dw;
   *y1 = *y0 + dh;
}

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

struct Frame
{
   std::vector<uint32_t> px;
   int w, h;
   Frame() : w(0), h(0) {}
};

struct Joy
{
   SDL_JoystickID id;
   SDL_Joystick *js;
   SDL_Gamepad *gp;
   ZCPORT_JOY_STATE st;
   std::string name;
};

struct State
{
   // thread
   std::thread thread;
   std::thread::id tid;
   bool running;
   bool init_done;
   bool init_ok;
   std::string init_error;
   std::mutex init_mutex;
   std::condition_variable init_cv;

   // work for the platform thread
   std::mutex cmd_mutex;
   std::condition_variable cmd_cv;
   std::deque<std::function<void()> > cmds;
   unsigned long cmds_done;
   unsigned long cmds_queued;
   Uint32 wake_type;
   std::atomic<bool> wake_pending;
   bool quit;

   // window (platform thread)
   SDL_Window *window;
   zcport::VulkanPresenter *vk;
   zcport::SdlPresenter *sr;        // SDL's renderer, when Vulkan isn't used
   bool vulkan_loaded;
   bool use_vulkan;
   bool vsync;
   std::string vk_device;           // the Vulkan presenter's GPU
   std::atomic<void *> hwnd;
   std::atomic<uint32_t> window_id;


   zcport::ScaleMode scale_windowed;
   zcport::ScaleMode scale_fullscreen;

   // mode
   std::atomic<int> disp_w, disp_h;
   std::atomic<bool> windowed;
   std::atomic<bool> mode_active;

   // frames
   // Held while a frame is copied out of the game's display bitmap into 'work', and while
   // the display closes. Event pumps also run on other threads (ZQuest's double-click
   // timer polls the mouse): without it they copied a bitmap that close_mode was freeing
   // (an AddressSanitizer build saw it as ZQuest exited) and resized 'work' alongside
   // the game thread's own present.
   std::mutex handover_mutex;
   std::mutex frame_mutex;          // pending, display
   Frame pending;
   bool pending_new;
   BITMAP *display;                 // what the game shows (for self-snapshots)
   std::mutex shown_mutex;          // shown
   Frame shown;                     // last presented
   Frame work;                      // game thread scratch
   Frame self_work;                 // platform thread scratch
   std::atomic<uint64_t> last_handover_us;
   uint64_t last_present_us;
   bool need_repaint;
   std::atomic<unsigned long> frames_presented;

   // mapping window pixels <-> display (platform thread)
   int win_px_w, win_px_h;
   float dst_x0, dst_y0, dst_x1, dst_y1;
   float rel_acc_x, rel_acc_y;
   float wheel_acc_y, wheel_acc_x;
   std::atomic<int> os_cursor;      // show_os_cursor() request
   std::atomic<int> close_enabled;  // -1 unknown, 0 greyed, 1 enabled
   bool cursor_hidden;
   bool mouse_inside;

   // refresh rates
   std::atomic<int> window_hz;
   std::atomic<int> primary_hz;

   // focus and display switching
   std::atomic<bool> focused;
   std::atomic<bool> switched_out;
   std::mutex switch_mutex;
   std::deque<int> switch_queue;
   bool in_pause;

   // joysticks
   std::mutex joy_mutex;
   std::vector<Joy> joys;

   State()
      : running(false), init_done(false), init_ok(false),
        cmds_done(0), cmds_queued(0), wake_type(0), wake_pending(false), quit(false),
        window(NULL), vk(NULL), sr(NULL), vulkan_loaded(false), use_vulkan(false), vsync(false),
        hwnd(NULL), window_id(0),
        scale_windowed(zcport::SCALE_INTEGER), scale_fullscreen(zcport::SCALE_ASPECT),
        disp_w(0), disp_h(0), windowed(true), mode_active(false),
        pending_new(false), display(NULL), last_handover_us(0), last_present_us(0),
        need_repaint(false), frames_presented(0),
        win_px_w(0), win_px_h(0), dst_x0(0), dst_y0(0), dst_x1(0), dst_y1(0),
        rel_acc_x(0), rel_acc_y(0), wheel_acc_y(0), wheel_acc_x(0), os_cursor(0), close_enabled(-1),
        cursor_hidden(false), mouse_inside(false), window_hz(60), primary_hz(60),
        focused(false), switched_out(false), in_pause(false)
   {
   }
};

State *g = NULL;

bool on_platform_thread()
{
   return g && g->running && std::this_thread::get_id() == g->tid;
}

void wake_platform()
{
   if (!g || !g->wake_type)
      return;
   bool expected = false;
   if (!g->wake_pending.compare_exchange_strong(expected, true))
      return;
   SDL_Event ev;
   SDL_zero(ev);
   ev.type = g->wake_type;
   SDL_PushEvent(&ev);
}

// Runs fn on the platform thread; waits for it if 'wait'.
void run_on_platform(const std::function<void()> &fn, bool wait)
{
   if (!g || !g->running) {
      return;
   }
   if (on_platform_thread()) {
      fn();
      return;
   }
   unsigned long ticket;
   {
      std::lock_guard<std::mutex> guard(g->cmd_mutex);
      g->cmds.push_back(fn);
      ticket = ++g->cmds_queued;
   }
   wake_platform();
   if (wait) {
      std::unique_lock<std::mutex> lock(g->cmd_mutex);
      g->cmd_cv.wait(lock, [ticket] { return g->cmds_done >= ticket || !g->running; });
   }
}

void run_commands()
{
   for (;;) {
      std::function<void()> fn;
      {
         std::lock_guard<std::mutex> guard(g->cmd_mutex);
         if (g->cmds.empty())
            return;
         fn = g->cmds.front();
         g->cmds.pop_front();
      }
      fn();
      {
         std::lock_guard<std::mutex> guard(g->cmd_mutex);
         g->cmds_done++;
      }
      g->cmd_cv.notify_all();
   }
}

int display_hz(SDL_DisplayID id)
{
   const SDL_DisplayMode *m = id ? SDL_GetCurrentDisplayMode(id) : NULL;
   if (m && m->refresh_rate > 1.0f)
      return (int)std::lround(m->refresh_rate);
   return 60;
}

// The window title: set_window_title() may come before the window exists
// (or before the platform thread runs), so it is kept here.
std::mutex g_title_mutex;
std::string g_title;
bool g_title_set = false;

std::string window_title()
{
   {
      std::lock_guard<std::mutex> guard(g_title_mutex);
      if (g_title_set)
         return g_title;
   }
   // Like the reference build: the executable's name without extension.
   char path[1024];
   get_executable_name(path, sizeof(path));
   std::string s = path;
   size_t slash = s.find_last_of("/\\");
   if (slash != std::string::npos)
      s = s.substr(slash + 1);
   size_t dot = s.find_last_of('.');
   if (dot != std::string::npos && dot > 0)
      s = s.substr(0, dot);
   return s;
}

// ---------------------------------------------------------------------------
// platform thread: presenting
// ---------------------------------------------------------------------------

zcport::ScaleMode current_scale()
{
   return g->windowed ? g->scale_windowed : g->scale_fullscreen;
}

void update_mapping()
{
   if (!g->window)
      return;
   SDL_GetWindowSizeInPixels(g->window, &g->win_px_w, &g->win_px_h);
   dest_rect(g->win_px_w, g->win_px_h, g->disp_w, g->disp_h, current_scale(),
             &g->dst_x0, &g->dst_y0, &g->dst_x1, &g->dst_y1);
   zcport_touch::set_picture(g->dst_x0, g->dst_y0, g->dst_x1, g->dst_y1, g->win_px_w, g->win_px_h);
}

void present_surface_fallback(const Frame &f)
{
   SDL_Surface *ws = SDL_GetWindowSurface(g->window);
   if (!ws)
      return;
   SDL_FillSurfaceRect(ws, NULL, SDL_MapSurfaceRGB(ws, 0, 0, 0));
   SDL_Surface *src = SDL_CreateSurfaceFrom(f.w, f.h, SDL_PIXELFORMAT_ARGB8888,
                                            (void *)&f.px[0], f.w * 4);
   if (src) {
      float x0, y0, x1, y1;
      dest_rect(ws->w, ws->h, f.w, f.h, current_scale(), &x0, &y0, &x1, &y1);
      SDL_Rect r;
      r.x = (int)x0;
      r.y = (int)y0;
      r.w = (int)(x1 - x0);
      r.h = (int)(y1 - y0);
      SDL_BlitSurfaceScaled(src, NULL, ws, &r, SDL_SCALEMODE_NEAREST);
      SDL_DestroySurface(src);
   }
   SDL_UpdateWindowSurface(g->window);
}

// Dear ImGui (zplayer's menus and dialogs) and the on-screen gamepad over
// whichever presenter runs.
void attach_ui()
{
   bool ui = zcui_platform::attach(g->window) &&
             (g->vk ? g->vk->init_ui() : g->sr ? g->sr->init_ui() : false);
   zcui_platform::set_renderer_ready(ui);
   if (!ui)
      SDL_Log("zcport: the UI layer could not start");
   zcport_touch::attach(g->window);   // on-screen gamepad (touch screens)
}

void detach_ui()
{
   zcport_touch::detach();
   zcui_platform::set_renderer_ready(false);
   if (g->vk)
      g->vk->shutdown_ui();
   if (g->sr)
      g->sr->shutdown_ui();
   zcui_platform::detach();
}

void stop_sdl_renderer()
{
   if (!g->sr)
      return;
   g->sr->shutdown();
   delete g->sr;
   g->sr = NULL;
}

void stop_vulkan()
{
   if (!g->vk)
      return;
   g->vk->shutdown();
   delete g->vk;
   g->vk = NULL;
   g->use_vulkan = false;
}

// SDL's renderer (Direct3D, OpenGL, Metal or software) when Vulkan is
// missing, broken or not chosen. 'drivers' as SdlPresenter::init. Without it
// the window surface is the last resort (no menus or dialogs).
bool start_sdl_renderer(const char *drivers)
{
   g->sr = new zcport::SdlPresenter;
   std::string err;
   if (g->sr->init(g->window, drivers, g->vsync, &err)) {
      attach_ui();
      return true;
   }
   SDL_Log("zcport: SDL's renderer is unavailable too (%s), using the window surface", err.c_str());
   delete g->sr;
   g->sr = NULL;
   return false;
}

void present(const Frame &f)
{
   if (!g->window || f.w <= 0 || f.h <= 0 || f.px.empty())
      return;
   if (g->use_vulkan && g->vk) {
      ImDrawData *ui = NULL;
      if (g->vk->ui_ready()) {
         g->vk->ui_new_frame();
         ui = zcui_platform::build_frame();
      }
      if (!g->vk->present(&f.px[0], f.w, f.h, f.w, current_scale(), ui)) {
         SDL_Log("zcport: Vulkan present failed, switching to SDL's renderer");
         detach_ui();
         stop_vulkan();
         if (start_sdl_renderer(NULL))
            g->need_repaint = true;
      }
   }
   else if (g->sr) {
      ImDrawData *ui = NULL;
      if (g->sr->ui_ready()) {
         g->sr->ui_new_frame();
         ui = zcui_platform::build_frame();
      }
      if (!g->sr->present(&f.px[0], f.w, f.h, f.w, current_scale(), ui)) {
         SDL_Log("zcport: SDL's renderer stopped (%s), using the window surface", SDL_GetError());
         detach_ui();
         stop_sdl_renderer();
         g->need_repaint = true;
      }
   }
   else {
      present_surface_fallback(f);
   }
   g->frames_presented++;
}

void maybe_present()
{
   if (!g->window)
      return;
   uint64_t now = now_us();
   int hz = g->window_hz.load();
   uint64_t period = 1000000u / (uint64_t)(hz > 0 ? hz : 60);
   bool have = false;
   {
      std::lock_guard<std::mutex> guard(g->frame_mutex);
      if (g->pending_new) {
         std::swap(g->self_work.px, g->pending.px);
         g->self_work.w = g->pending.w;
         g->self_work.h = g->pending.h;
         g->pending_new = false;
         have = true;
      }
      else if (g->display && g->mode_active &&
               now - g->last_handover_us.load() > 100000 &&
               now - g->last_present_us >= period) {
         // The game has not shown a frame for a while: copy the screen
         // ourselves (no pointer: its sprite may change under us).
         BITMAP *d = g->display;
         uint32_t lut[256];
         build_lut(_current_palette, lut);
         g->self_work.px.resize((size_t)d->w * d->h);
         g->self_work.w = d->w;
         g->self_work.h = d->h;
         convert_bitmap(d, lut, &g->self_work.px[0]);
         have = true;
      }
   }
   if (have) {
      std::lock_guard<std::mutex> guard(g->shown_mutex);
      std::swap(g->shown.px, g->self_work.px);
      g->shown.w = g->self_work.w;
      g->shown.h = g->self_work.h;
   }
   if (!have && !g->need_repaint && now - g->last_present_us >= period &&
       zcui_platform::wants_continuous())
      g->need_repaint = true;   // a menu or dialog is up: keep drawing it
   if (have || g->need_repaint) {
      g->need_repaint = false;
      g->last_present_us = now;
      std::lock_guard<std::mutex> guard(g->shown_mutex);
      present(g->shown);
   }
}

void show_black(int w, int h)
{
   std::lock_guard<std::mutex> guard(g->shown_mutex);
   g->shown.w = w;
   g->shown.h = h;
   g->shown.px.assign((size_t)w * h, 0xFF000000u);
   g->need_repaint = true;
}

// ---------------------------------------------------------------------------
// platform thread: window
// ---------------------------------------------------------------------------

void destroy_presenter()
{
   detach_ui();
   stop_vulkan();
   stop_sdl_renderer();
   g->use_vulkan = false;
}

// Which presenter to start with: zc.cfg's Video Settings > Renderer, or
// ZC_RENDERER=auto|sdl|software|surface for test runs ("surface" is the
// last resort alone, to check it).
const int RENDERER_SURFACE_ONLY = -1;

int wanted_renderer()
{
   const char *e = SDL_getenv("ZC_RENDERER");
   if (e && *e) {
      if (!SDL_strcasecmp(e, "sdl"))
         return zcdisplay::RENDERER_SDL;
      if (!SDL_strcasecmp(e, "software"))
         return zcdisplay::RENDERER_SOFTWARE;
      if (!SDL_strcasecmp(e, "surface"))
         return RENDERER_SURFACE_ONLY;
      return zcdisplay::RENDERER_AUTO;
   }
   return zcport_sdl::renderer_choice();
}

// Greys out the close button while the game has no close button callback
// (as the reference does); unknown (the foundation never said) = enabled.
void apply_close_button()
{
#ifdef _WIN32
   HWND hwnd = (HWND)g->hwnd.load();
   if (!hwnd)
      return;
   HMENU menu = GetSystemMenu(hwnd, FALSE);
   if (!menu)
      return;
   bool enabled = g->close_enabled.load() != 0;
   EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
#endif
}

bool create_window(int w, int h, std::string *error)
{
   const std::string title = window_title();
   SDL_WindowFlags flags = SDL_WINDOW_HIDDEN;
   // A window under the test driver never takes the keyboard from the desktop.
   if (SDL_getenv("ZQ_UI_SCRIPT"))
      flags |= SDL_WINDOW_NOT_FOCUSABLE;
   const int want = wanted_renderer();
   if (g->vulkan_loaded && want == zcdisplay::RENDERER_AUTO)
      flags |= SDL_WINDOW_VULKAN;
   g->window = SDL_CreateWindow(title.c_str(), w, h, flags);
   if (!g->window && (flags & SDL_WINDOW_VULKAN)) {
      flags &= ~SDL_WINDOW_VULKAN;
      g->window = SDL_CreateWindow(title.c_str(), w, h, flags);
   }
   if (!g->window) {
      *error = SDL_GetError();
      return false;
   }
   if (flags & SDL_WINDOW_VULKAN) {
      g->vk = new zcport::VulkanPresenter;
      std::string err;
      if (g->vk->init(g->window, g->vsync, &err)) {
         g->use_vulkan = true;
         const char *dev = g->vk->device_name();
         g->vk_device = dev ? dev : "";
         attach_ui();   // zplayer's menus and dialogs (Dear ImGui over the picture)
      }
      else {
         SDL_Log("zcport: Vulkan unavailable (%s), trying SDL's renderer", err.c_str());
         destroy_presenter();
         SDL_DestroyWindow(g->window);
         flags &= ~SDL_WINDOW_VULKAN;
         g->window = SDL_CreateWindow(title.c_str(), w, h, flags);
         if (!g->window) {
            *error = SDL_GetError();
            return false;
         }
      }
   }
   // No Vulkan (or another renderer chosen): SDL's renderer, else the window
   // surface.
   if (!g->vk && want != RENDERER_SURFACE_ONLY)
      start_sdl_renderer(want == zcdisplay::RENDERER_SOFTWARE ? "software" : NULL);
   if (!g->vk && !g->sr)
      SDL_Log("zcport: showing the game through the window surface (no menus or dialogs)");
   SDL_PropertiesID props = SDL_GetWindowProperties(g->window);
   g->hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
   g->window_id = SDL_GetWindowID(g->window);
#ifdef _WIN32
   // A window that can't take focus is left off the taskbar unless it asks to be on it.
   if (flags & SDL_WINDOW_NOT_FOCUSABLE) {
      HWND hwnd = (HWND)g->hwnd.load();
      if (hwnd)
         SetWindowLongPtr(hwnd, GWL_EXSTYLE, GetWindowLongPtr(hwnd, GWL_EXSTYLE) | WS_EX_APPWINDOW);
   }
#endif
   apply_close_button();
   return true;
}

void center_on_primary()
{
   SDL_DisplayID d = SDL_GetPrimaryDisplay();
   SDL_SetWindowPosition(g->window, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
}

int do_gfx_open(int w, int h, int windowed, std::string *error)
{
   if (!g->window) {
      if (!create_window(w, h, error))
         return -1;
   }

   {
      // The game is about to replace the screen bitmap.
      std::lock_guard<std::mutex> guard(g->frame_mutex);
      g->display = NULL;
      g->pending_new = false;
   }

#if defined(__ANDROID__)
   if (windowed) {
      // Android has no window sizes: the window always fills the screen and
      // the presenter scales the game's picture into it. The game still
      // sees the windowed mode it asked for. Fullscreen hides the status
      // and navigation bars (immersive mode), so nothing covers the menus.
      SDL_ShowWindow(g->window);
      SDL_SetWindowFullscreen(g->window, true);
      SDL_SyncWindow(g->window);
   }
#else
   if (zcport_sdl::display_managed()) {
      // zplayer's display settings (platform_display.cpp): the saved window
      // size and monitor, the chosen fullscreen kind, a resizable window.
      if (zcport_sdl::display_open(w, h, windowed != 0, error) != 0) {
         g->mode_active = false;
         return -1;
      }
   }
   else if (windowed) {
      if (SDL_GetWindowFlags(g->window) & SDL_WINDOW_FULLSCREEN)
         SDL_SetWindowFullscreen(g->window, false);
      SDL_SetWindowResizable(g->window, false);
      SDL_SetWindowSize(g->window, w, h);
      center_on_primary();
      SDL_ShowWindow(g->window);
      SDL_SyncWindow(g->window);
      int pw = 0, ph = 0;
      SDL_GetWindowSizeInPixels(g->window, &pw, &ph);
#ifdef _WIN32
      // Windows' minimum window width (SM_CXMINTRACK, frame included):
      // the reference cannot get a narrower client area and fails. SDL
      // lifts that limit, so check it here.
      int bt = 0, bl = 0, bb = 0, br = 0;
      SDL_GetWindowBordersSize(g->window, &bt, &bl, &bb, &br);
      if (w + bl + br < GetSystemMetrics(SM_CXMINTRACK))
         pw = -1;
#endif
      if (pw != w || ph != h) {
         // Windows keeps a minimum window width; the reference fails then.
         char buf[128];
         snprintf(buf, sizeof(buf), "Unable to create a %dx%d window", w, h);
         *error = buf;
         g->mode_active = false;
         return -1;
      }
   }
#endif
   else {
      SDL_SetWindowFullscreenMode(g->window, NULL);   // desktop resolution
      SDL_ShowWindow(g->window);
      SDL_SetWindowFullscreen(g->window, true);
      SDL_SyncWindow(g->window);
   }
   // Raising calls SetForegroundWindow, which takes the keyboard even from a
   // window that can't be focused: the test driver's windows must not.
   if (!(SDL_GetWindowFlags(g->window) & SDL_WINDOW_NOT_FOCUSABLE))
      SDL_RaiseWindow(g->window);

   g->disp_w = w;
   g->disp_h = h;
   g->windowed = windowed != 0;
   g->mode_active = true;
   g->window_hz = display_hz(SDL_GetDisplayForWindow(g->window));
   if (g->vk)
      g->vk->notify_resized();
   update_mapping();
   show_black(w, h);
   return 0;
}

void do_gfx_close()
{
   {
      std::lock_guard<std::mutex> guard(g->frame_mutex);
      g->display = NULL;
      g->pending_new = false;
   }
   g->mode_active = false;
   if (!g->window)
      return;
   if (SDL_GetWindowFlags(g->window) & SDL_WINDOW_FULLSCREEN) {
      SDL_SetWindowFullscreen(g->window, false);
      SDL_SyncWindow(g->window);
   }
   // The window stays open, as with the reference build.
   int w = g->disp_w, h = g->disp_h;
   if (w > 0 && h > 0)
      show_black(w, h);
}

void update_cursor()
{
   bool hide = g->window && g->mode_active && mouse_driver != NULL && g->os_cursor.load() == 0 &&
               !zcui::session_active();
   if (hide != g->cursor_hidden) {
      if (hide)
         SDL_HideCursor();
      else
         SDL_ShowCursor();
      g->cursor_hidden = hide;
   }
}

// ---------------------------------------------------------------------------
// platform thread: input
// ---------------------------------------------------------------------------

void window_to_display(float wx, float wy, int *dx, int *dy)
{
   float density = g->window ? SDL_GetWindowPixelDensity(g->window) : 1.0f;
   if (density <= 0.0f)
      density = 1.0f;
   float px = wx * density, py = wy * density;
   float sw = g->dst_x1 - g->dst_x0, sh = g->dst_y1 - g->dst_y0;
   int w = g->disp_w, h = g->disp_h;
   if (sw <= 0 || sh <= 0 || w <= 0 || h <= 0) {
      *dx = (int)std::floor(px);
      *dy = (int)std::floor(py);
      return;
   }
   *dx = (int)std::floor((px - g->dst_x0) * w / sw);
   *dy = (int)std::floor((py - g->dst_y0) * h / sh);
}

void handle_key(const SDL_KeyboardEvent &e)
{
   zcport_key_set_locks(zcport_sdl::lock_flags(e.mod));
   int sc = zcport_sdl::map_scancode(e.scancode);
   if (!sc)
      return;
   if (e.down) {
      if (e.repeat)
         return;   // the foundation repeats keys itself
      zcport_key_down(sc, zcport_sdl::key_char(e.scancode, e.mod, sc));
   }
   else {
      zcport_key_up(sc);
   }
}

int mouse_button_index(Uint8 b)
{
   switch (b) {
      case SDL_BUTTON_LEFT:   return 0;
      case SDL_BUTTON_RIGHT:  return 1;
      case SDL_BUTTON_MIDDLE: return 2;
      case SDL_BUTTON_X1:     return 3;
      case SDL_BUTTON_X2:     return 4;
      default:                return -1;
   }
}

void release_everything()
{
   zcport_key_release_all();
   for (int b = 0; b < 5; b++)
      zcport_mouse_button(b, 0);
}

// ---- joysticks ------------------------------------------------------------------

void refresh_joy(Joy &j)
{
   ZCPORT_JOY_STATE &st = j.st;
   memset(&st, 0, sizeof(st));
   st.name = j.name.c_str();
   if (j.gp) {
      // Axes: left stick, right stick, triggers. Buttons in the order the
      // reference's DirectInput driver reports an XInput pad: A B X Y LB RB
      // Back Start LS RS, then Guide. D-pad: hat 0.
      static const SDL_GamepadAxis axes[] = {
         SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX,
         SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
      };
      static const SDL_GamepadButton buttons[] = {
         SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
         SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
         SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK,
         SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_GUIDE
      };
      st.num_axes = 6;
      for (int i = 0; i < 6; i++)
         st.axis[i] = SDL_GetGamepadAxis(j.gp, axes[i]);
      st.num_buttons = 11;
      for (int i = 0; i < 11; i++)
         st.button[i] = SDL_GetGamepadButton(j.gp, buttons[i]) ? 1 : 0;
      st.num_hats = 1;
      int hat = 0;
      if (SDL_GetGamepadButton(j.gp, SDL_GAMEPAD_BUTTON_DPAD_UP))    hat |= 1;
      if (SDL_GetGamepadButton(j.gp, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) hat |= 2;
      if (SDL_GetGamepadButton(j.gp, SDL_GAMEPAD_BUTTON_DPAD_DOWN))  hat |= 4;
      if (SDL_GetGamepadButton(j.gp, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  hat |= 8;
      st.hat[0] = hat;
      return;
   }
   if (j.js) {
      int na = SDL_GetNumJoystickAxes(j.js);
      int nb = SDL_GetNumJoystickButtons(j.js);
      int nh = SDL_GetNumJoystickHats(j.js);
      st.num_axes = std::max(0, std::min(na, ZCPORT_JOY_MAX_AXES));
      st.num_buttons = std::max(0, std::min(nb, ZCPORT_JOY_MAX_BUTTONS));
      st.num_hats = std::max(0, std::min(nh, ZCPORT_JOY_MAX_HATS));
      for (int i = 0; i < st.num_axes; i++)
         st.axis[i] = SDL_GetJoystickAxis(j.js, i);
      for (int i = 0; i < st.num_buttons; i++)
         st.button[i] = SDL_GetJoystickButton(j.js, i) ? 1 : 0;
      for (int i = 0; i < st.num_hats; i++)
         st.hat[i] = SDL_GetJoystickHat(j.js, i) & 15;   // same bits as ours
   }
}

void refresh_joys()
{
   std::lock_guard<std::mutex> guard(g->joy_mutex);
   for (size_t i = 0; i < g->joys.size(); i++)
      refresh_joy(g->joys[i]);
}

void add_joy(SDL_JoystickID id)
{
   std::lock_guard<std::mutex> guard(g->joy_mutex);
   for (size_t i = 0; i < g->joys.size(); i++)
      if (g->joys[i].id == id)
         return;
   Joy j;
   j.id = id;
   j.js = NULL;
   j.gp = NULL;
   if (SDL_IsGamepad(id))
      j.gp = SDL_OpenGamepad(id);
   if (!j.gp)
      j.js = SDL_OpenJoystick(id);
   if (!j.gp && !j.js)
      return;
   const char *n = j.gp ? SDL_GetGamepadName(j.gp) : SDL_GetJoystickName(j.js);
   j.name = n ? n : "Joystick";
   memset(&j.st, 0, sizeof(j.st));
   g->joys.push_back(j);
   refresh_joy(g->joys.back());
}

void remove_joy(SDL_JoystickID id)
{
   std::lock_guard<std::mutex> guard(g->joy_mutex);
   for (size_t i = 0; i < g->joys.size(); i++) {
      if (g->joys[i].id == id) {
         if (g->joys[i].gp)
            SDL_CloseGamepad(g->joys[i].gp);
         else if (g->joys[i].js)
            SDL_CloseJoystick(g->joys[i].js);
         g->joys.erase(g->joys.begin() + i);
         return;
      }
   }
}

void close_joys()
{
   std::lock_guard<std::mutex> guard(g->joy_mutex);
   for (size_t i = 0; i < g->joys.size(); i++) {
      if (g->joys[i].gp)
         SDL_CloseGamepad(g->joys[i].gp);
      else if (g->joys[i].js)
         SDL_CloseJoystick(g->joys[i].js);
   }
   g->joys.clear();
}

// ---- events ------------------------------------------------------------------------

// On-screen keyboard text -> key presses (letters, digits, space, a few
// signs), with the character for the game's keyboard buffer.
void type_text(const char *utf8)
{
   for (const unsigned char *p = (const unsigned char *)utf8; *p; ++p) {
      int c = *p, sc = 0;
      if (c >= 0x80)
         continue;   // multi-byte UTF-8: not typeable in the game's fonts
      if (c >= 'a' && c <= 'z')
         sc = KEY_A + (c - 'a');
      else if (c >= 'A' && c <= 'Z')
         sc = KEY_A + (c - 'A');
      else if (c >= '0' && c <= '9')
         sc = KEY_0 + (c - '0');
      else if (c == ' ')
         sc = KEY_SPACE;
      else if (c == '-')
         sc = KEY_MINUS;
      else if (c == '.')
         sc = KEY_STOP;
      else if (c == ',')
         sc = KEY_COMMA;
      else if (c == 39)          /* ' */
         sc = KEY_QUOTE;
      else if (c == 10 || c == 13)
         sc = KEY_ENTER;
      if (!sc)
         continue;
      if (key[sc])
         continue;   // a real key made this text: it is already down, and has to stay down
      zcport_key_down(sc, c == 10 ? 13 : c);
      zcport_key_up(sc);
   }
}

void handle_event(const SDL_Event &ev)
{
   if (zcport_sdl::display_event(ev))
      return;   // Alt+Enter (platform_display.cpp)
   if (zcui_platform::event(ev))
      return;   // input for an open menu or dialog, not for the game
   if (zcport_touch::event(ev))
      return;   // the on-screen gamepad (platform_touch.cpp)
   bool joy_changed = false;
   switch (ev.type) {
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
         if (ev.type == SDL_EVENT_KEY_DOWN)
            zcport_touch::physical_input();
         handle_key(ev.key);
         break;

      case SDL_EVENT_TEXT_INPUT:
         // Text from an on-screen keyboard (Android; the touch controls'
         // keyboard button), typed into the game as key presses.
         if (g->mode_active && ev.text.text)
            type_text(ev.text.text);
         break;

      case SDL_EVENT_MOUSE_MOTION: {
         if (!g->mode_active)
            break;
         int x, y;
         window_to_display(ev.motion.x, ev.motion.y, &x, &y);
         g->rel_acc_x += ev.motion.xrel;
         g->rel_acc_y += ev.motion.yrel;
         int rx = (int)g->rel_acc_x, ry = (int)g->rel_acc_y;
         g->rel_acc_x -= rx;
         g->rel_acc_y -= ry;
         zcport_mouse_motion(x, y, rx, ry);
         break;
      }
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP: {
         int b = mouse_button_index(ev.button.button);
         if (b >= 0) {
            if (g->mode_active && ev.button.down) {
               int x, y;
               window_to_display(ev.button.x, ev.button.y, &x, &y);
               zcport_mouse_motion(x, y, 0, 0);
            }
            zcport_mouse_button(b, ev.button.down ? 1 : 0);
         }
         break;
      }
      case SDL_EVENT_MOUSE_WHEEL: {
         float fy = ev.wheel.y, fx = ev.wheel.x;
         if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
            fy = -fy;
            fx = -fx;
         }
         g->wheel_acc_y += fy;
         g->wheel_acc_x += fx;
         int dz = (int)g->wheel_acc_y, dw = (int)g->wheel_acc_x;
         g->wheel_acc_y -= dz;
         g->wheel_acc_x -= dw;
         if (dz || dw)
            zcport_mouse_wheel(dz, dw);
         break;
      }

      case SDL_EVENT_WINDOW_MOUSE_ENTER:
         g->mouse_inside = true;
         zcport_mouse_set_on_screen(1);
         break;
      case SDL_EVENT_WINDOW_MOUSE_LEAVE:
         g->mouse_inside = false;
         zcport_mouse_set_on_screen(0);
         break;

      case SDL_EVENT_WINDOW_FOCUS_GAINED:
         if (!g->focused) {
            g->focused = true;
            if (g->switched_out) {
               g->switched_out = false;
               std::lock_guard<std::mutex> guard(g->switch_mutex);
               g->switch_queue.push_back(SWITCH_IN);
            }
         }
         break;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
         zcport_touch::release_all();
         if (g->focused) {
            g->focused = false;
            g->switched_out = true;
            release_everything();
            std::lock_guard<std::mutex> guard(g->switch_mutex);
            g->switch_queue.push_back(SWITCH_OUT);
         }
         break;

      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
         // The game's callback runs here, as Allegro's does on its window
         // thread. Without one the close button does nothing.
         zcport_close_button_pressed();
         break;

      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
         if (g->vk)
            g->vk->notify_resized();
         update_mapping();
         g->need_repaint = true;
         break;
      case SDL_EVENT_WINDOW_EXPOSED:
      case SDL_EVENT_WINDOW_RESTORED:
      case SDL_EVENT_WINDOW_SHOWN:
         g->need_repaint = true;
         break;
      case SDL_EVENT_RENDER_DEVICE_RESET:
         if (g->sr)
            g->sr->device_reset();
         g->need_repaint = true;
         break;
      case SDL_EVENT_RENDER_DEVICE_LOST:
         if (g->sr)
            g->sr->device_lost();   // the next present goes to the window surface
         g->need_repaint = true;
         break;
      case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
         if (g->window)
            g->window_hz = display_hz(SDL_GetDisplayForWindow(g->window));
         if (g->vk)
            g->vk->notify_resized();
         update_mapping();
         g->need_repaint = true;
         break;
      case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
         g->primary_hz = display_hz(SDL_GetPrimaryDisplay());
         if (g->window)
            g->window_hz = display_hz(SDL_GetDisplayForWindow(g->window));
         break;

      case SDL_EVENT_JOYSTICK_ADDED:
         add_joy(ev.jdevice.which);
         break;
      case SDL_EVENT_JOYSTICK_REMOVED:
         remove_joy(ev.jdevice.which);
         break;
      case SDL_EVENT_JOYSTICK_AXIS_MOTION:
      case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
      case SDL_EVENT_JOYSTICK_BUTTON_UP:
      case SDL_EVENT_JOYSTICK_HAT_MOTION:
      case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      case SDL_EVENT_GAMEPAD_BUTTON_UP:
         if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN)
            zcport_touch::physical_input();
         joy_changed = true;
         break;

      default:
         if (ev.type == g->wake_type)
            g->wake_pending = false;
         break;
   }
   if (joy_changed)
      refresh_joys();
}

// ---------------------------------------------------------------------------
// audio
// ---------------------------------------------------------------------------

struct Audio
{
   SDL_AudioStream *stream;         // set and cleared on the platform thread
   ZCPORT_AUDIO_MIX_FUNC mix;
   void *user;
   int frame_bytes;
   bool subsystem;                  // SDL_INIT_AUDIO is ours to quit
   std::vector<unsigned char> buf;  // SDL's audio thread
};

Audio g_audio = { NULL, NULL, NULL, 4, false, std::vector<unsigned char>() };

// SDL's audio thread, with the stream locked: hands the stream what it asks
// for (whole frames) from the zcport mixer.
void SDLCALL audio_feed(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
   (void)userdata;
   (void)total;
   if (additional <= 0 || !g_audio.mix)
      return;
   const int fb = g_audio.frame_bytes;
   const int bytes = (additional + fb - 1) / fb * fb;
   if ((int)g_audio.buf.size() < bytes)
      g_audio.buf.resize((size_t)bytes);
   g_audio.mix(g_audio.user, &g_audio.buf[0], bytes);
   SDL_PutAudioStreamData(stream, &g_audio.buf[0], bytes);
}

// Platform thread (or the calling thread without one).
int do_audio_open(int freq, int channels, int bits, ZCPORT_AUDIO_MIX_FUNC mix, void *user,
                  std::string *err)
{
   if (g_audio.stream)
      return -1;   // one device at a time
   if (!SDL_WasInit(SDL_INIT_AUDIO)) {
      if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
         *err = SDL_GetError();
         return -1;
      }
      g_audio.subsystem = true;
   }
   // Small device blocks: the mixer runs a little more than two blocks
   // behind the game's clock.
   if (!SDL_GetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES))
      SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
   SDL_AudioSpec spec;
   SDL_zero(spec);
   spec.format = (bits == 8) ? SDL_AUDIO_U8 : SDL_AUDIO_S16;
   spec.channels = channels;
   spec.freq = freq;
   g_audio.mix = mix;
   g_audio.user = user;
   g_audio.frame_bytes = channels * (bits / 8);
   g_audio.buf.assign(32768, 0);
   SDL_AudioStream *s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audio_feed, NULL);
   if (!s) {
      *err = SDL_GetError();
      g_audio.mix = NULL;
      if (g_audio.subsystem) {
         SDL_QuitSubSystem(SDL_INIT_AUDIO);
         g_audio.subsystem = false;
      }
      return -1;
   }
   g_audio.stream = s;
   SDL_ResumeAudioStreamDevice(s);
   return 0;
}

void do_audio_close()
{
   if (g_audio.stream) {
      // Waits for a running callback; none comes afterwards.
      SDL_DestroyAudioStream(g_audio.stream);
      g_audio.stream = NULL;
   }
   g_audio.mix = NULL;
   g_audio.user = NULL;
   if (g_audio.subsystem) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      g_audio.subsystem = false;
   }
}

// ---------------------------------------------------------------------------
// the platform thread
// ---------------------------------------------------------------------------

#ifdef _WIN32
// A window under the test driver must never hold the keyboard, but Windows can
// still hand it the foreground on its own, despite WS_EX_NOACTIVATE. While it
// is in front this process is allowed to give the foreground back. Other
// no-activate windows (parallel test runs) are never the ones to return to.
static void give_back_foreground()
{
   static HWND last_other = NULL;
   if (!g->window || !(SDL_GetWindowFlags(g->window) & SDL_WINDOW_NOT_FOCUSABLE))
      return;
   HWND own = (HWND)g->hwnd.load();
   HWND front = GetForegroundWindow();
   if (!own || !front)
      return;
   if (front != own) {
      if (!(GetWindowLongPtr(front, GWL_EXSTYLE) & WS_EX_NOACTIVATE))
         last_other = front;
   }
   else if (last_other && IsWindow(last_other))
      SetForegroundWindow(last_other);
}
#endif

// zc.cfg's controller_hidapi in [zeldadx] (Settings > Controls > Extra Pad
// Support, zc_sys.cpp): 0 or 1, or -1 when it isn't there. SDL starts here
// before the game reads zc.cfg, so this one value is read from the file directly.
static int cfg_hidapi_setting()
{
   FILE *f = fopen("zc.cfg", "r");
   if (!f)
      return -1;
   static const char key[] = "controller_hidapi";
   char line[512];
   bool in_section = false;
   int value = -1;
   while (fgets(line, sizeof(line), f)) {
      const char *s = line;
      while (*s == ' ' || *s == '\t')
         s++;
      if (*s == '[') {
         in_section = strncmp(s, "[zeldadx]", 9) == 0;
         continue;
      }
      if (!in_section || strncmp(s, key, sizeof(key) - 1) != 0)
         continue;
      s += sizeof(key) - 1;
      while (*s == ' ' || *s == '\t')
         s++;
      if (*s++ != '=')
         continue;
      while (*s == ' ' || *s == '\t')
         s++;
      if (*s >= '0' && *s <= '9')
         value = (*s == '0') ? 0 : 1;
   }
   fclose(f);
   return value;
}

// SDL's HIDAPI controller drivers. On Windows they have been seen writing a
// device's HID data over the game's memory while SDL lists devices; that is
// inside SDL, which this project doesn't change. So on Windows they stay off
// unless the player turns them on: XInput, Windows.Gaming.Input, GameInput and
// DirectInput still carry Xbox, PlayStation, Switch Pro, 8BitDo and most other
// pads, and SDL's built-in layouts map their buttons. Turning them on is only
// needed for pads that work through HIDAPI alone (a Switch Pro Controller on
// USB, GameCube adapters, the Steam Controller). Elsewhere they stay on unless
// turned off.
static bool use_hidapi()
{
   const int setting = cfg_hidapi_setting();
#ifdef _WIN32
   return setting == 1;
#else
   return setting != 0;
#endif
}

void platform_thread_main()
{
   if (!use_hidapi())
      SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
   SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");          // rest() like the reference
   SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
   SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
   SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
   SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
   SDL_SetMainReady();

   bool ok = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);
   std::string err;
   if (!ok)
      err = SDL_GetError();
   if (ok) {
      g->wake_type = SDL_RegisterEvents(1);
#if defined(__APPLE__)
      // macOS: MoltenVK ships next to the program (SDL only looks in the system's own
      // places); a Vulkan SDK's loader is the fallback.
      {
         const char *base = SDL_GetBasePath();
         std::string mvk = std::string(base ? base : "") + "libMoltenVK.dylib";
         g->vulkan_loaded = (base && access(mvk.c_str(), R_OK) == 0 && SDL_Vulkan_LoadLibrary(mvk.c_str()))
                            || SDL_Vulkan_LoadLibrary(NULL);
      }
#else
      g->vulkan_loaded = SDL_Vulkan_LoadLibrary(NULL);
#endif
      if (!g->vulkan_loaded)
         SDL_Log("zcport: no Vulkan loader on this system (%s)", SDL_GetError());
      g->primary_hz = display_hz(SDL_GetPrimaryDisplay());
      int n = 0;
      SDL_JoystickID *ids = SDL_GetJoysticks(&n);
      for (int i = 0; ids && i < n; i++)
         add_joy(ids[i]);
      SDL_free(ids);
   }

   {
      std::lock_guard<std::mutex> guard(g->init_mutex);
      g->init_done = true;
      g->init_ok = ok;
      g->init_error = err;
   }
   g->init_cv.notify_all();
   if (!ok)
      return;

   while (!g->quit) {
      int timeout = 100;
      if (g->window && g->mode_active) {
         // Wake when the next repaint is due (a menu or dialog redraws at
         // the refresh rate; a whole-millisecond period, 1000 / hz, would
         // wake just short of it and present every other period).
         int hz = g->window_hz.load();
         uint64_t period = 1000000u / (uint64_t)(hz > 0 ? hz : 60);
         uint64_t now = now_us(), due = g->last_present_us + period;
         timeout = due > now ? (int)((due - now + 999) / 1000) : (int)(period / 1000);
         if (timeout < 1)
            timeout = 1;
      }
      SDL_Event ev;
      if (SDL_WaitEventTimeout(&ev, timeout)) {
         handle_event(ev);
         while (SDL_PollEvent(&ev))
            handle_event(ev);
      }
      run_commands();
      update_cursor();
      maybe_present();
#ifdef _WIN32
      give_back_foreground();
#endif
   }

   run_commands();
   do_audio_close();     // normally done by remove_sound() already
   close_joys();
   destroy_presenter();
   zcui_platform::shutdown();
   if (g->window) {
      SDL_DestroyWindow(g->window);
      g->window = NULL;
   }
   g->hwnd = NULL;
   g->window_id = 0;
   if (g->vulkan_loaded)
      SDL_Vulkan_UnloadLibrary();
   SDL_Quit();
}

#if defined(__APPLE__)
// macOS (Cocoa) allows windows and their events only on the main thread, so there the
// platform loop runs on the main thread and the game on a thread of its own: main()
// calls zcport_sdl_host_main(), which starts the game and then waits here until
// hook_init asks for the loop (instead of hook_init starting a thread).
bool g_host = false;                // zcport_sdl_host_main() is hosting the loop
std::thread::id g_host_tid;         // the main thread
std::mutex g_host_mutex;
std::condition_variable g_host_cv;
bool g_host_start = false;          // hook_init asked for the loop
bool g_host_running = false;        // the loop runs on the main thread

void host_wait_loop_done()
{
   std::unique_lock<std::mutex> lock(g_host_mutex);
   g_host_cv.wait(lock, [] { return !g_host_start && !g_host_running; });
}
#endif

// ---------------------------------------------------------------------------
// hooks (game thread unless noted)
// ---------------------------------------------------------------------------

int hook_init(void)
{
   if (!g)
      g = new State;
   if (g->running)
      return 0;
   g->init_done = false;
   g->quit = false;
   g->running = true;
#if defined(__APPLE__)
   if (g_host) {
      g->tid = g_host_tid;
      {
         std::lock_guard<std::mutex> guard(g_host_mutex);
         g_host_start = true;
      }
      g_host_cv.notify_all();
   }
   else
#endif
   {
      g->thread = std::thread(platform_thread_main);
      g->tid = g->thread.get_id();
   }
   std::unique_lock<std::mutex> lock(g->init_mutex);
   g->init_cv.wait(lock, [] { return g->init_done; });
   if (!g->init_ok) {
      lock.unlock();
#if defined(__APPLE__)
      if (g_host)
         host_wait_loop_done();
      else
#endif
      g->thread.join();
      g->running = false;
      std::string msg = "SDL initialisation failed: " + g->init_error;
      zcport_set_error(msg.c_str());
      fprintf(stderr, "%s\n", msg.c_str());   // the game's log only says "Failed Init!"
      return -1;
   }
   return 0;
}

void hook_exit(void)
{
   if (!g || !g->running)
      return;
   if (on_platform_thread()) {
      // exit() from an input callback (e.g. the Ctrl+Alt+End emergency exit
      // of the foundation) runs the exit functions here: the process is
      // ending, so just let the loop stop.
      g->quit = true;
      if (g->thread.joinable())     // (on macOS the loop is the main thread's, no thread object)
         g->thread.detach();
      g->running = false;
      return;
   }
   run_on_platform([] { g->quit = true; }, false);
#if defined(__APPLE__)
   if (g_host)
      host_wait_loop_done();
   else
#endif
   if (g->thread.joinable())
      g->thread.join();
   {
      std::lock_guard<std::mutex> guard(g->cmd_mutex);
      g->running = false;
      g->cmds.clear();
   }
   g->cmd_cv.notify_all();
   g->mode_active = false;
   g->display = NULL;
}

void deliver_switch_callbacks()
{
   for (;;) {
      int dir;
      {
         std::lock_guard<std::mutex> guard(g->switch_mutex);
         if (g->switch_queue.empty())
            return;
         dir = g->switch_queue.front();
         g->switch_queue.pop_front();
      }
      zcport_display_switch_notify(dir);
   }
}

// The caller holds g->handover_mutex.
void hand_over_frame_locked(BITMAP *display, const RGB *palette, bool with_pointer)
{
   if (!g || !g->running || !display)
      return;
   uint32_t lut[256];
   build_lut(palette ? palette : _current_palette, lut);
   Frame &f = g->work;
   f.w = display->w;
   f.h = display->h;
   f.px.resize((size_t)f.w * f.h);
   convert_bitmap(display, lut, &f.px[0]);
   if (with_pointer) {
      BITMAP *spr = NULL;
      int x = 0, y = 0;
      if (zcport_mouse_get_pointer(display, &spr, &x, &y) && spr)
         draw_pointer(spr, x, y, lut, &f.px[0], f.w, f.h);
   }
   {
      std::lock_guard<std::mutex> guard(g->frame_mutex);
      std::swap(g->pending.px, f.px);
      g->pending.w = f.w;
      g->pending.h = f.h;
      g->pending_new = true;
      g->display = display;
   }
   g->last_handover_us = now_us();
   wake_platform();
}

void hand_over_frame(BITMAP *display, const RGB *palette, bool with_pointer)
{
   if (!g || !g->running || !display)
      return;
   std::lock_guard<std::mutex> guard(g->handover_mutex);
   hand_over_frame_locked(display, palette, with_pointer);
}

void hook_pump_events(void)
{
   if (!g || !g->running)
      return;
   deliver_switch_callbacks();

   // SWITCH_PAUSE / SWITCH_AMNESIA: the game stops while switched out.
   int mode = get_display_switch_mode();
   if ((mode == SWITCH_PAUSE || mode == SWITCH_AMNESIA) && g->mode_active &&
       g->switched_out && !g->in_pause) {
      g->in_pause = true;
      while (g->running && g->mode_active && g->switched_out) {
         int m = get_display_switch_mode();
         if (m != SWITCH_PAUSE && m != SWITCH_AMNESIA)
            break;
         std::this_thread::sleep_for(std::chrono::milliseconds(10));
         deliver_switch_callbacks();
      }
      g->in_pause = false;
      deliver_switch_callbacks();
   }

   if (!g->mode_active || !zcport_display)
      return;
   int hz = g->window_hz.load();
   uint64_t period = 1000000u / (uint64_t)(hz > 0 ? hz : 60);
   uint64_t now = now_us();
   if (now - g->last_handover_us.load() + 1500 < period)
      return;
   // Busy: a present is handing a frame over or the display is closing. Either way this
   // pump has nothing to add, and waiting could deadlock a pump on the platform thread
   // against a close waiting for that thread.
   std::unique_lock<std::mutex> lock(g->handover_mutex, std::try_to_lock);
   if (!lock.owns_lock())
      return;
   // checked again now that a close can't run alongside
   if (!g->mode_active || !zcport_display)
      return;
   hand_over_frame_locked(zcport_display, _current_palette, true);
}

void hook_message(const char *msg)
{
   std::string title = window_title();
#if defined(__APPLE__)
   // macOS shows message boxes only from the main thread, which runs the platform loop;
   // with no loop running (it waits for hook_init) a box from here could hang.
   if (g_host && !on_platform_thread()) {
      std::string text = msg ? msg : "";
      if (g && g->running)
         run_on_platform([title, text] {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), text.c_str(), NULL);
         }, true);
      else
         fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str());
      return;
   }
#endif
   SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, title.c_str(), msg ? msg : "", NULL);
}

void hook_set_window_title(const char *title)
{
   std::string t = title ? title : "";
   {
      std::lock_guard<std::mutex> guard(g_title_mutex);
      g_title = t;
      g_title_set = true;
   }
   if (!g || !g->running)
      return;
   run_on_platform([t] {
      if (g->window)
         SDL_SetWindowTitle(g->window, t.c_str());
   }, true);
}

int hook_get_desktop_resolution(int *w, int *h)
{
   if (!g || !g->running)
      return -1;
   int rw = 0, rh = 0;
   bool ok = false;
   run_on_platform([&] {
      const SDL_DisplayMode *m = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
      if (m) {
         rw = m->w;
         rh = m->h;
         ok = true;
      }
   }, true);
   if (!ok)
      return -1;
   if (w) *w = rw;
   if (h) *h = rh;
   return 0;
}

int hook_desktop_color_depth(void)
{
   return 32;
}

void *hook_native_window(void)
{
   return g ? g->hwnd.load() : NULL;
}

int hook_gfx_open(int w, int h, int color_depth, int windowed)
{
   (void)color_depth;
   if (!g || !g->running)
      return -1;
   // The game's saved display settings, read here on the game thread.
   zcport_sdl::display_before_open();
   int r = -1;
   std::string err;
   std::string rend;
   run_on_platform([&] {
      r = do_gfx_open(w, h, windowed != 0 ? 1 : 0, &err);
      rend = zcport_sdl::renderer_in_use();
   }, true);
   if (r != 0 && !err.empty())
      zcport_set_error(err.c_str());
   static std::string logged;
   if (r == 0 && rend != logged) {
      al_trace("Renderer: %s\n", rend.c_str());
      logged = rend;
   }
   return r;
}

void hook_gfx_close(void)
{
   if (!g || !g->running)
      return;
   // No frame is being copied from the display once this returns, and none starts until
   // the next mode opens (mode_active), so close_mode can free the bitmap.
   std::lock_guard<std::mutex> guard(g->handover_mutex);
   run_on_platform([] { do_gfx_close(); }, true);
}

void hook_gfx_present(BITMAP *display, const RGB *palette)
{
   hand_over_frame(display, palette, true);
}

void hook_gfx_vsync(void)
{
   if (!g)
      return;
   int hz = g->window_hz.load();
   if (hz <= 0)
      hz = 60;
   const uint64_t period = 1000000000ull / (uint64_t)hz;
   uint64_t t = SDL_GetTicksNS();
   uint64_t next = (t / period + 1) * period;
   SDL_DelayPrecise(next - t);
}

int hook_gfx_is_windowed(void)
{
   return (g && g->windowed) ? 1 : 0;
}

void hook_close_button_enable(int enabled)
{
   if (!g)
      g = new State;
   g->close_enabled = enabled ? 1 : 0;
   if (g->running)
      /* wait, so the menu state is final when set_close_button_callback()
       * returns (the reference changes it synchronously) */
      run_on_platform([] { apply_close_button(); }, true);
}

int hook_gfx_refresh_rate(void)
{
   return g ? g->primary_hz.load() : 0;
}

void hook_mouse_warp(int x, int y)
{
   if (!g || !g->running)
      return;
   run_on_platform([x, y] {
      if (!g->window || !g->mode_active || !g->focused || !g->mouse_inside)
         return;
      int w = g->disp_w, h = g->disp_h;
      if (w <= 0 || h <= 0)
         return;
      float px = g->dst_x0 + (x + 0.5f) * (g->dst_x1 - g->dst_x0) / w;
      float py = g->dst_y0 + (y + 0.5f) * (g->dst_y1 - g->dst_y0) / h;
      float density = SDL_GetWindowPixelDensity(g->window);
      if (density <= 0.0f)
         density = 1.0f;
      SDL_WarpMouseInWindow(g->window, px / density, py / density);
   }, false);
}

int hook_mouse_show_os_cursor(int cursor)
{
   if (!g || !g->running)
      return -1;
   g->os_cursor = cursor;
   run_on_platform([cursor] {
      if (cursor != 0) {
         SDL_SystemCursor c = SDL_SYSTEM_CURSOR_DEFAULT;
         if (cursor == MOUSE_CURSOR_BUSY)
            c = SDL_SYSTEM_CURSOR_WAIT;
         else if (cursor == MOUSE_CURSOR_EDIT)
            c = SDL_SYSTEM_CURSOR_TEXT;
         static SDL_Cursor *cursors[SDL_SYSTEM_CURSOR_COUNT];
         if (!cursors[c])
            cursors[c] = SDL_CreateSystemCursor(c);
         if (cursors[c])
            SDL_SetCursor(cursors[c]);
      }
      update_cursor();
   }, false);
   return 0;
}

int hook_joy_count(void)
{
   if (!g || !g->running)
      return 0;
   std::lock_guard<std::mutex> guard(g->joy_mutex);
   return (int)g->joys.size();
}

int hook_joy_poll(int index, ZCPORT_JOY_STATE *state)
{
   if (!g || !g->running || !state)
      return -1;
   std::lock_guard<std::mutex> guard(g->joy_mutex);
   if (index < 0 || index >= (int)g->joys.size())
      return -1;
   *state = g->joys[index].st;
   return 0;
}

int hook_audio_open(int freq, int channels, int bits, ZCPORT_AUDIO_MIX_FUNC mix, void *user, int *out_freq)
{
   if (!mix || freq <= 0 || channels < 1 || channels > 2 || (bits != 8 && bits != 16))
      return -1;
   int r = -1;
   std::string err;
   if (g && g->running)
      run_on_platform([&] { r = do_audio_open(freq, channels, bits, mix, user, &err); }, true);
   else
      r = do_audio_open(freq, channels, bits, mix, user, &err);
   if (r != 0) {
      al_trace("zcport: no audio device (%s), sound is silent\n", err.c_str());
      return -1;
   }
   // SDL's stream converts to the device's own format and rate.
   if (out_freq)
      *out_freq = freq;
   return 0;
}

void hook_audio_close(void)
{
   if (g && g->running)
      run_on_platform([] { do_audio_close(); }, true);
   else
      do_audio_close();
}

void hook_audio_lock(void)
{
   if (g_audio.stream)
      SDL_LockAudioStream(g_audio.stream);
}

void hook_audio_unlock(void)
{
   if (g_audio.stream)
      SDL_UnlockAudioStream(g_audio.stream);
}

ZCPORT_PLATFORM make_table()
{
   ZCPORT_PLATFORM t;
   memset(&t, 0, sizeof(t));
   t.version = ZCPORT_PLATFORM_VERSION;
   t.name = "SDL3";
   t.init = hook_init;
   t.exit = hook_exit;
   t.pump_events = hook_pump_events;
   t.get_ticks_us = NULL;          // the foundation's steady clock
   t.sleep_ms = NULL;              // std::this_thread::sleep_for, as Allegro sleeps
   t.message = hook_message;
   t.set_window_title = hook_set_window_title;
   t.get_desktop_resolution = hook_get_desktop_resolution;
   t.desktop_color_depth = hook_desktop_color_depth;
   t.get_executable_name = NULL;
   t.native_window = hook_native_window;
   t.gfx_open = hook_gfx_open;
   t.gfx_close = hook_gfx_close;
   t.gfx_present = hook_gfx_present;
   t.gfx_vsync = hook_gfx_vsync;
   t.gfx_is_windowed = hook_gfx_is_windowed;
   t.mouse_warp = hook_mouse_warp;
   t.mouse_show_os_cursor = hook_mouse_show_os_cursor;
   t.joy_count = hook_joy_count;
   t.joy_poll = hook_joy_poll;
   t.audio_open = hook_audio_open;
   t.audio_close = hook_audio_close;
   t.audio_lock = hook_audio_lock;
   t.audio_unlock = hook_audio_unlock;
   t.gfx_refresh_rate = hook_gfx_refresh_rate;
   t.close_button_enable = hook_close_button_enable;
   return t;
}

ZCPORT_PLATFORM g_table = make_table();

// Registers the platform before main() (zcport_sdl's link options keep
// this object in every program that links the library).
struct AutoRegister
{
   AutoRegister() { zcport_sdl_register(); }
};
AutoRegister g_auto_register;

} // namespace

// ---------------------------------------------------------------------------
// public API (zcport/platform_sdl.h)
// ---------------------------------------------------------------------------

extern "C" int zcport_sdl_link_anchor = 1;

#if defined(__APPLE__)
namespace
{
struct HostGame
{
   int (*fn)(int, char **);
   int argc;
   char **argv;
};

void *host_game_thread(void *p)
{
   HostGame *h = (HostGame *)p;
   // The game normally ends the process itself (exit); a return from its main() does it here.
   exit(h->fn(h->argc, h->argv));
   return NULL;
}
} // namespace

// macOS: called by main() on the main thread. Runs game(argc, argv) on a thread of its
// own, with the main thread's usual stack size (secondary threads get 512 KB otherwise),
// and runs the platform loop here whenever hook_init asks for it. Never returns: the
// game's exit() ends the process. (Returning from main would run the exit functions a
// second time, alongside the game's own exit.)
extern "C" int zcport_sdl_host_main(int (*game)(int, char **), int argc, char **argv)
{
   static HostGame h;
   h.fn = game;
   h.argc = argc;
   h.argv = argv;
   g_host_tid = std::this_thread::get_id();
   g_host = true;

   pthread_attr_t attr;
   pthread_attr_init(&attr);
   pthread_attr_setstacksize(&attr, (size_t)64 << 20);
   pthread_t t;
   int r = pthread_create(&t, &attr, host_game_thread, &h);
   pthread_attr_destroy(&attr);
   if (r != 0) {
      g_host = false;               // no thread: the game on this thread, as elsewhere
      return game(argc, argv);
   }
   pthread_detach(t);

   for (;;) {
      {
         std::unique_lock<std::mutex> lock(g_host_mutex);
         g_host_cv.wait(lock, [] { return g_host_start; });
         g_host_start = false;
         g_host_running = true;
      }
      platform_thread_main();
      {
         std::lock_guard<std::mutex> guard(g_host_mutex);
         g_host_running = false;
      }
      g_host_cv.notify_all();
   }
}
#endif

extern "C" void zcport_sdl_register(void)
{
   if (zcport_get_platform() != &g_table)
      zcport_set_platform(&g_table);
   // MIDI goes to the system's synthesizer (winmm on Windows).
   if (!zcport_get_midi_backend())
      zcport_set_midi_backend(zcport_sdl::midi_backend());
}

extern "C" void zcport_place_window_on_display(int display)
{
   if (!g || !g->running || display <= 0)
      return;
   run_on_platform([display] {
      if (!g->window || !g->mode_active || !g->windowed)
         return;
      // Left to right, then top to bottom (platform_display.cpp, shared
      // with the Video Settings' monitor list).
      int count = 0;
      if (!zcport_sdl::place_on_display(display, &count)) {
         al_trace("window_display: screen %d requested but only %d connected, leaving the window where it is\n",
                  display, count);
         return;
      }
      g->window_hz = display_hz(SDL_GetDisplayForWindow(g->window));
   }, true);
}

extern "C" unsigned long zcport_sdl_frames_presented(void)
{
   return g ? g->frames_presented.load() : 0;
}

extern "C" int zcport_sdl_last_frame(uint32_t *out, int max_pixels, int *w, int *h)
{
   if (!g)
      return 0;
   std::lock_guard<std::mutex> guard(g->shown_mutex);
   if (g->shown.w <= 0 || g->shown.h <= 0 || g->shown.px.empty())
      return 0;
   if ((long long)g->shown.w * g->shown.h > max_pixels || !out)
      return 0;
   memcpy(out, &g->shown.px[0], (size_t)g->shown.w * g->shown.h * 4);
   if (w) *w = g->shown.w;
   if (h) *h = g->shown.h;
   return 1;
}

extern "C" int zcport_sdl_using_vulkan(void)
{
   if (!g || !g->running)
      return -1;
   int r = -1;
   run_on_platform([&r] { r = g->window ? (g->use_vulkan ? 1 : 0) : -1; }, true);
   return r;
}

extern "C" uint32_t zcport_sdl_window_id(void)
{
   return g ? g->window_id.load() : 0;
}

extern "C" int zcport_sdl_window_geometry(int *pw, int *ph, float *x0, float *y0, float *x1, float *y1)
{
   if (!g || !g->running)
      return 0;
   int ok = 0;
   run_on_platform([&] {
      if (!g->window)
         return;
      update_mapping();
      if (pw) *pw = g->win_px_w;
      if (ph) *ph = g->win_px_h;
      if (x0) *x0 = g->dst_x0;
      if (y0) *y0 = g->dst_y0;
      if (x1) *x1 = g->dst_x1;
      if (y1) *y1 = g->dst_y1;
      ok = 1;
   }, true);
   return ok;
}

extern "C" void zcport_sdl_sync(void)
{
   if (!g || !g->running)
      return;
   // A command queued now runs after the events already in SDL's queue
   // (the loop handles all pending events before the commands).
   run_on_platform([] {
      SDL_Event ev;
      SDL_PumpEvents();
      while (SDL_PollEvent(&ev))
         handle_event(ev);
   }, true);
}

// ---------------------------------------------------------------------------
// for platform_display.cpp (platform_sdl_internal.h)
// ---------------------------------------------------------------------------

namespace zcport_sdl
{

bool running()
{
   return g && g->running;
}

bool on_platform_thread()
{
   return ::on_platform_thread();
}

void run(const std::function<void()> &fn)
{
   run_on_platform(fn, true);
}

void post(const std::function<void()> &fn)
{
   if (!g || !g->running)
      return;
   {
      std::lock_guard<std::mutex> guard(g->cmd_mutex);
      g->cmds.push_back(fn);
      ++g->cmds_queued;
   }
   wake_platform();
}

SDL_Window *window()
{
   return g ? g->window : NULL;
}

bool mode_active()
{
   return g && g->mode_active;
}

void logical_size(int *w, int *h)
{
   bool on = g && g->mode_active;
   if (w) *w = on ? g->disp_w.load() : 0;
   if (h) *h = on ? g->disp_h.load() : 0;
}

void window_reconfigured(bool windowed)
{
   if (!g || !g->window)
      return;
   g->windowed = windowed;
   g->window_hz = display_hz(SDL_GetDisplayForWindow(g->window));
   if (g->vk)
      g->vk->notify_resized();
   update_mapping();
   g->need_repaint = true;
}

void set_scale_modes(zcport::ScaleMode windowed, zcport::ScaleMode fullscreen)
{
   if (!g)
      return;
   g->scale_windowed = windowed;
   g->scale_fullscreen = fullscreen;
   update_mapping();
   g->need_repaint = true;
}

void set_vsync(bool on)
{
   if (!g)
      return;
   g->vsync = on;   // also for a presenter started later
   if (g->vk)
      g->vk->set_vsync(on);
   if (g->sr)
      g->sr->set_vsync(on);
   if (g->vk || g->sr)
      g->need_repaint = true;
}

std::string renderer_in_use()
{
   if (!g || !g->window)
      return std::string();
   if (g->vk && g->use_vulkan)
      return g->vk_device.empty() ? std::string("Vulkan") : "Vulkan, " + g->vk_device;
   if (g->sr)
      return zcport::render_driver_title(g->sr->driver_name());
   return "Window surface (no menus)";
}

int window_refresh_hz()
{
   return g ? g->window_hz.load() : 0;
}

} // namespace zcport_sdl

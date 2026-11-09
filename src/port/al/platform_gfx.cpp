// zcport platform module: graphics modes and the screen bitmap.
//
// set_gfx_mode() creates "screen" as an ordinary memory bitmap flagged
// BMP_ID_VIDEO (zcport_display) and asks the platform (zcport/platform.h
// gfx_open) for a window of that size; the platform shows the bitmap through
// the current palette. Without a platform (or without a gfx_open hook) the
// mode is "headless": everything works, nothing is shown.
//
// What the reference build (Allegro 4.4.2, Windows, DirectDraw) does and
// this file reproduces, measured with tests/conformance/test_platform_gfx_sdl:
//   - GFX_AUTODETECT, GFX_AUTODETECT_WINDOWED, GFX_DIRECTX_WIN: the windowed
//     DirectDraw driver (id GFX_DIRECTX_WIN, capabilities 0x07b30088);
//     GFX_SAFE and GFX_GDI: the GDI driver (id GFX_GDI, capabilities 0);
//     fullscreen ids: GFX_DIRECTX_ACCEL (GFX_DIRECTX_SOFT, _SAFE as asked).
//   - windowed widths must be a multiple of 4 and the window must be able
//     to get that client size (Windows' minimum window width); a virtual
//     size other than 0x0 or the screen size fails.
//   - every call increments _gfx_mode_set_count (also when it fails); a
//     failure closes the previous mode (screen = NULL, gfx_driver = NULL)
//     but leaves the window open.
//   - the new screen is cleared to 0, the palette is reset to the default
//     palette (8-bit), the mouse pointer is hidden (show_mouse(NULL)) and
//     moved to the centre of the new screen, the display switch mode is
//     reset to the driver's default (windowed: SWITCH_PAUSE) which also
//     removes the switch callbacks; GFX_TEXT resets it to SWITCH_BACKGROUND.
//   - GFX_TEXT closes the mode but keeps the window open and visible.
//   - windowed vsync() returns at once; set_palette() does not wait either.
//   - no triple buffering, no hardware scrolling: enable_triple_buffer(),
//     request_scroll(), scroll_screen(), request_video_bitmap() fail (-1),
//     poll_scroll() returns 0. show_video_bitmap() of a screen-sized video
//     bitmap makes "screen" draw into (and show) that bitmap's memory.
//   - get_refresh_rate(): 0 without a mode, the display's rate with one;
//     retrace_count then runs at that rate (70 Hz without a mode).
//
// Written from scratch for zcport (no Allegro code).

#include "platform_internal.h"

// Owned by the color module (color_palette.cpp).
extern "C" int _palette_color8[256];

#include <cstring>

#ifdef _WIN32
   #include <winalleg.h>
#endif

// ---------------------------------------------------------------------------
// drivers
// ---------------------------------------------------------------------------

namespace
{
   GFX_DRIVER make_driver(int id, const char *name, int windowed)
   {
      GFX_DRIVER d;
      memset(&d, 0, sizeof(d));
      d.id = id;
      d.name = name;
      d.desc = name;
      d.ascii_name = name;
      d.linear = TRUE;
      d.windowed = windowed;
      return d;
   }

   GFX_DRIVER g_drv_dxwin = make_driver(GFX_DIRECTX_WIN, "zcport window", TRUE);
   GFX_DRIVER g_drv_gdi = make_driver(GFX_GDI, "zcport window (GDI)", TRUE);
   GFX_DRIVER g_drv_accel = make_driver(GFX_DIRECTX_ACCEL, "zcport fullscreen", FALSE);
   GFX_DRIVER g_drv_soft = make_driver(GFX_DIRECTX_SOFT, "zcport fullscreen (soft)", FALSE);
   GFX_DRIVER g_drv_safe = make_driver(GFX_DIRECTX_SAFE, "zcport fullscreen (safe)", FALSE);

   // Capabilities the reference reports for its windowed DirectDraw driver:
   // GFX_HW_HLINE, GFX_HW_FILL, the VRAM/system blit and stretch bits.
   const int kCapsDirectX = 0x07b30088;

   int g_requested_refresh = 0;
   int g_refresh = 0;

   // Rows of the bitmap whose memory zcport_display shows after
   // show_video_bitmap() (NULL: its own memory).
   BITMAP *g_shown = NULL;
   unsigned char **g_own_lines = NULL;

   // What the reference does to the color state on every successful
   // graphics mode (measured by the color and platform modules):
   // palette_color[] for 8 bpp becomes the identity, the 32-bit pixel layout
   // follows the desktop (A8R8G8B8: red in bits 16-23; the 15/16/24-bit
   // layouts keep their defaults), and the default palette is set.
   void apply_mode_color_state(void)
   {
      for (int i = 0; i < PAL_SIZE; i++)
         _palette_color8[i] = i;

      _rgb_r_shift_32 = 16;
      _rgb_g_shift_32 = 8;
      _rgb_b_shift_32 = 0;

      set_palette(default_palette);
   }

   const ZCPORT_PLATFORM *plat()
   {
      return zcport_get_platform();
   }

   // Puts zcport_display's rows back on its own memory.
   void restore_own_lines(void)
   {
      if (g_own_lines && zcport_display) {
         for (int y = 0; y < zcport_display->h; y++)
            zcport_display->line[y] = g_own_lines[y];
      }
      delete[] g_own_lines;
      g_own_lines = NULL;
      g_shown = NULL;
   }

   // Closes the current mode (the window, if any, stays open).
   void close_mode(void)
   {
      if (!screen && !gfx_driver && !zcport_display)
         return;
      show_mouse(NULL);
      const ZCPORT_PLATFORM *p = plat();
      if (p && p->gfx_close)
         p->gfx_close();          // the platform forgets the display first
      restore_own_lines();
      BITMAP *old = zcport_display;
      zcport_display = NULL;
      if (screen == old)
         screen = NULL;
      if (old)
         destroy_bitmap(old);
      screen = NULL;
      gfx_driver = NULL;
      gfx_capabilities = 0;
      g_refresh = 0;
      zcport_platform::timer_set_retrace_rate(70);
   }

   void gfx_exit_func(void)
   {
      set_gfx_mode(GFX_TEXT, 0, 0, 0, 0);
   }

   bool g_exit_registered = false;
}

// ---------------------------------------------------------------------------
// set_gfx_mode
// ---------------------------------------------------------------------------

extern "C" int set_gfx_mode(int card, int w, int h, int v_w, int v_h)
{
   _gfx_mode_set_count++;

   if (card == GFX_TEXT) {
      close_mode();
      set_display_switch_mode(SWITCH_BACKGROUND);
      return 0;
   }

   GFX_DRIVER *drv = NULL;
   switch (card) {
      case GFX_AUTODETECT:
      case GFX_AUTODETECT_WINDOWED:
      case GFX_DIRECTX_WIN:
      case GFX_ZCPORT:
         drv = &g_drv_dxwin;
         break;
      case GFX_SAFE:
      case GFX_GDI:
         drv = &g_drv_gdi;
         break;
      case GFX_AUTODETECT_FULLSCREEN:
      case GFX_DIRECTX_ACCEL:
         drv = &g_drv_accel;
         break;
      case GFX_DIRECTX_SOFT:
         drv = &g_drv_soft;
         break;
      case GFX_DIRECTX_SAFE:
         drv = &g_drv_safe;
         break;
      default:
         break;
   }

   // Any failure below leaves no mode set (the window stays).
   close_mode();

   if (!drv) {
      zcport_set_error("Unable to find a suitable graphics driver");
      return -1;
   }

   if (zcport_gfx_fit_virtual && v_w > 0 && v_h > 0) {
      w = v_w;
      h = v_h;
   }

   int depth = get_color_depth();
   if (zcport_bytes_per_pixel(depth) == 0) {
      zcport_set_error("Unsupported color depth");
      return -1;
   }
   if (w <= 0 || h <= 0) {
      zcport_set_error("Unsupported screen size");
      return -1;
   }
   if ((v_w != 0 && v_w != w) || (v_h != 0 && v_h != h)) {
      zcport_set_error("Unsupported virtual resolution");
      return -1;
   }
   bool windowed = drv->windowed != 0;
   if (windowed && (w % 4) != 0) {
      zcport_set_error(card == GFX_AUTODETECT_WINDOWED || card == GFX_AUTODETECT
                       ? "Overlays not supported" : "Unsupported screen size");
      return -1;
   }

   const ZCPORT_PLATFORM *p = plat();
   if (p && p->gfx_open) {
      allegro_error[0] = 0;
      if (p->gfx_open(w, h, depth, windowed ? 1 : 0) != 0) {
         if (!allegro_error[0])
            zcport_set_error("Unable to create the window");
         return -1;
      }
   }

   BITMAP *bmp = zcport_create_bitmap_flags(depth, w, h, BMP_ID_VIDEO);
   if (!bmp) {
      if (p && p->gfx_close)
         p->gfx_close();
      zcport_set_error("Not enough memory");
      return -1;
   }
   clear_to_color(bmp, 0);

   drv->w = w;
   drv->h = h;
   drv->vid_mem = (long)w * h * zcport_bytes_per_pixel(depth);
   gfx_driver = drv;
   gfx_capabilities = (drv == &g_drv_gdi) ? 0 : kCapsDirectX;
   screen = bmp;
   zcport_display = bmp;

   // every successful mode, whatever its depth (as the reference)
   apply_mode_color_state();

   g_refresh = 60;
   if (p && p->gfx_refresh_rate) {
      int r = p->gfx_refresh_rate();
      if (r > 0)
         g_refresh = r;
   }
   zcport_platform::timer_set_retrace_rate(g_refresh);

   // Mouse: range = the new screen, pointer hidden, centred.
   zcport_mouse_display_changed();
   if (mouse_driver)
      position_mouse(w / 2, h / 2);

   set_display_switch_mode(windowed ? SWITCH_PAUSE : SWITCH_AMNESIA);

   if (!g_exit_registered) {
      _add_exit_func(gfx_exit_func, "set_gfx_mode");
      g_exit_registered = true;
   }

   // First (black) frame; also tells the platform which bitmap to show.
   if (p && p->gfx_present)
      p->gfx_present(zcport_display, _current_palette);
   return 0;
}

int zcport_gfx_fit_virtual = 0;

extern "C" void zcport_present_bitmap(BITMAP *bmp)
{
   const ZCPORT_PLATFORM *p = plat();
   zcport_display = bmp;
   if (p && p->gfx_present)
      p->gfx_present(bmp, _current_palette);
}

extern "C" void zcport_gfx_mouse_shown(BITMAP *bmp)
{
   if (gfx_driver && zcport_display && bmp && is_same_bitmap(bmp, zcport_display))
      gfx_capabilities |= GFX_HW_CURSOR;
   else
      gfx_capabilities &= ~GFX_HW_CURSOR;
}

// ---------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------

extern "C" int is_windowed_mode(void)
{
   return gfx_driver ? gfx_driver->windowed : FALSE;
}

extern "C" int get_gfx_mode_type(int graphics_card)
{
   switch (graphics_card) {
      case GFX_AUTODETECT_WINDOWED:
         return GFX_TYPE_WINDOWED | GFX_TYPE_DEFINITE | GFX_TYPE_MAGIC;
      case GFX_AUTODETECT_FULLSCREEN:
         return GFX_TYPE_FULLSCREEN | GFX_TYPE_DEFINITE | GFX_TYPE_MAGIC;
      case GFX_AUTODETECT:
      case GFX_SAFE:
      case GFX_TEXT:
         return GFX_TYPE_MAGIC;
      case GFX_DIRECTX_WIN:
      case GFX_GDI:
      case GFX_ZCPORT:
         return GFX_TYPE_WINDOWED | GFX_TYPE_DEFINITE;
      case GFX_DIRECTX_ACCEL:
      case GFX_DIRECTX_SOFT:
      case GFX_DIRECTX_SAFE:
         return GFX_TYPE_FULLSCREEN | GFX_TYPE_DEFINITE;
      default:
         return GFX_TYPE_UNKNOWN;
   }
}

extern "C" int get_gfx_mode(void)
{
   return gfx_driver ? gfx_driver->id : GFX_NONE;
}

extern "C" void request_refresh_rate(int rate)
{
   g_requested_refresh = rate;
}

extern "C" int get_refresh_rate(void)
{
   return gfx_driver ? g_refresh : 0;
}

extern "C" GFX_MODE_LIST *get_gfx_mode_list(int card)
{
   // The windowed drivers have no mode list; the fullscreen ones show the
   // desktop scaled, so there is no list of real modes either.
   (void)card;
   return NULL;
}

extern "C" void destroy_gfx_mode_list(GFX_MODE_LIST *gfx_mode_list)
{
   if (!gfx_mode_list)
      return;
   free(gfx_mode_list->mode);
   free(gfx_mode_list);
}

// ---------------------------------------------------------------------------
// retrace, page flipping, scrolling
// ---------------------------------------------------------------------------

extern "C" void vsync(void)
{
   const ZCPORT_PLATFORM *p = plat();
   if (gfx_driver && !gfx_driver->windowed && zcport_display && p) {
      // Fullscreen: show the finished frame and wait for the retrace.
      if (p->gfx_present)
         p->gfx_present(zcport_display, _current_palette);
      if (p->gfx_vsync)
         p->gfx_vsync();
      zcport_pump_events();
      return;
   }
   // Windowed: the reference returns at once. Let the platform show the
   // frame (rate limited).
   zcport_pump_events();
}

extern "C" int enable_triple_buffer(void)
{
   return -1;
}

extern "C" int scroll_screen(int x, int y)
{
   (void)x;
   (void)y;
   return -1;
}

extern "C" int request_scroll(int x, int y)
{
   (void)x;
   (void)y;
   return -1;
}

extern "C" int poll_scroll(void)
{
   return 0;
}

extern "C" int show_video_bitmap(BITMAP *bitmap)
{
   if (!gfx_driver || !zcport_display || !bitmap)
      return -1;
   if (bitmap->w != zcport_display->w || bitmap->h != zcport_display->h)
      return -1;
   if (!is_video_bitmap(bitmap))
      return -1;
   if (bitmap_color_depth(bitmap) != bitmap_color_depth(zcport_display))
      return -1;

   if (bitmap == zcport_display || bitmap == g_shown)
      return 0;

   if (!g_own_lines) {
      g_own_lines = new unsigned char *[zcport_display->h];
      for (int y = 0; y < zcport_display->h; y++)
         g_own_lines[y] = zcport_display->line[y];
   }
   for (int y = 0; y < zcport_display->h; y++)
      zcport_display->line[y] = bitmap->line[y];
   g_shown = bitmap;

   const ZCPORT_PLATFORM *p = plat();
   if (p && p->gfx_present)
      p->gfx_present(zcport_display, _current_palette);
   return 0;
}

extern "C" int request_video_bitmap(BITMAP *bitmap)
{
   (void)bitmap;
   return -1;
}

// ---------------------------------------------------------------------------
// window
// ---------------------------------------------------------------------------

#ifdef _WIN32
extern "C" HWND win_get_window(void)
{
   const ZCPORT_PLATFORM *p = plat();
   if (p && p->native_window)
      return (HWND)p->native_window();
   return NULL;
}
#endif

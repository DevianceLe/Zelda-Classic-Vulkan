// zcport foundation: mouse state, the pointer (drawn by the platform as an
// overlay, see zcport_mouse_get_pointer) and the platform feed API.
//
// Written from scratch for zcport (no Allegro code).

#include "foundation_internal.h"

#include <cstring>

using zcport_foundation::input_lock;

// ---------------------------------------------------------------------------
// public state
// ---------------------------------------------------------------------------

volatile int mouse_x = 0;
volatile int mouse_y = 0;
volatile int mouse_z = 0;
volatile int mouse_w = 0;
volatile int mouse_b = 0;
volatile int mouse_pos = 0;
volatile int freeze_mouse_flag = FALSE;

BITMAP *mouse_sprite = NULL;
int mouse_x_focus = 0;
int mouse_y_focus = 0;

void (*mouse_callback)(int flags) = NULL;

BITMAP *_mouse_screen = NULL;
int _mouse_installed = FALSE;

MOUSE_DRIVER mousedrv_none =
{
   MOUSEDRV_NONE, "No mouse", "No mouse", "No mouse",
   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static MOUSE_DRIVER mousedrv_zcport =
{
   MOUSEDRV_ZCPORT, "zcport mouse", "zcport mouse", "zcport mouse",
   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

MOUSE_DRIVER *mouse_driver = NULL;

// ---------------------------------------------------------------------------
// private state
// ---------------------------------------------------------------------------

#define ZCPORT_SCARED_SIZE 16

namespace
{
   BITMAP *g_default_sprite = NULL;
   BITMAP *g_user_sprite = NULL;      // what set_mouse_sprite() was given

   bool g_range_set = false;
   int g_range_x1 = 0, g_range_y1 = 0, g_range_x2 = -1, g_range_y2 = -1;

   int g_speed_x = 2, g_speed_y = 2;
   int g_mickey_x = 0, g_mickey_y = 0;

   int g_on_screen = TRUE;

   int g_cursor = MOUSE_CURSOR_ALLEGRO;
   BITMAP *g_cursor_bitmaps[AL_NUM_MOUSE_CURSORS];

   BITMAP *g_scared_screen[ZCPORT_SCARED_SIZE];
   int g_scared_freeze[ZCPORT_SCARED_SIZE];
   int g_scared_depth = 0;

   int g_drawn_x = 0, g_drawn_y = 0;  // pointer position last reported

   // My own default arrow: 1 = outline, 2 = fill, 0 = transparent.
   const char *const g_arrow[] =
   {
      "1.........",
      "11........",
      "121.......",
      "1221......",
      "12221.....",
      "122221....",
      "1222221...",
      "12222221..",
      "122222221.",
      "1222222221",
      "1222211111",
      "1221221...",
      "121.1221..",
      "11..1221..",
      "1....1221.",
      ".....1111.",
   };

   int pixel_value(int depth, int r, int g, int b)
   {
      switch (depth) {
         case 15: return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
         case 16: return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
         case 24:
         case 32: return (r << 16) | (g << 8) | b;
      }
      // 8 bpp: palette indices the pointer uses when no sprite is set
      return (r ? 255 : 16);
   }

   void put(BITMAP *bmp, int x, int y, int c)
   {
      unsigned char *row = bmp->line[y];
      switch (bitmap_color_depth(bmp)) {
         case 8:  row[x] = (unsigned char)c; break;
         case 15:
         case 16: ((uint16_t *)row)[x] = (uint16_t)c; break;
         case 24: row[x * 3] = (unsigned char)c; row[x * 3 + 1] = (unsigned char)(c >> 8);
                  row[x * 3 + 2] = (unsigned char)(c >> 16); break;
         case 32: ((uint32_t *)row)[x] = (uint32_t)c; break;
      }
   }

   BITMAP *make_default_sprite()
   {
      int depth = get_color_depth();
      if (!_get_vtable(depth))
         depth = 8;
      BITMAP *bmp = create_bitmap_ex(depth, 10, 16);
      if (!bmp)
         return NULL;
      int mask = bitmap_mask_color(bmp);
      int outline = pixel_value(depth, 0, 0, 0);
      int fill = pixel_value(depth, 255, 255, 255);
      for (int y = 0; y < 16; y++) {
         for (int x = 0; x < 10; x++) {
            char c = g_arrow[y][x];
            put(bmp, x, y, (c == '1') ? outline : ((c == '2') ? fill : mask));
         }
      }
      return bmp;
   }

   void get_range(int *x1, int *y1, int *x2, int *y2)
   {
      if (g_range_set) {
         *x1 = g_range_x1; *y1 = g_range_y1; *x2 = g_range_x2; *y2 = g_range_y2;
         return;
      }
      *x1 = 0;
      *y1 = 0;
      *x2 = SCREEN_W - 1;
      *y2 = SCREEN_H - 1;
   }

   void clamp_to_range(int *x, int *y)
   {
      int x1, y1, x2, y2;
      get_range(&x1, &y1, &x2, &y2);
      if (x2 >= x1) {
         if (*x < x1) *x = x1;
         if (*x > x2) *x = x2;
      }
      if (y2 >= y1) {
         if (*y < y1) *y = y1;
         if (*y > y2) *y = y2;
      }
   }

   void set_position(int x, int y)
   {
      mouse_x = x;
      mouse_y = y;
      mouse_pos = (int)(((unsigned int)x << 16) | ((unsigned int)y & 0xFFFF));
   }

   BITMAP *current_sprite()
   {
      if (g_cursor != MOUSE_CURSOR_ALLEGRO && g_cursor > MOUSE_CURSOR_NONE &&
          g_cursor < AL_NUM_MOUSE_CURSORS && g_cursor_bitmaps[g_cursor])
         return g_cursor_bitmaps[g_cursor];
      return mouse_sprite;
   }
}

void zcport_foundation::mouse_reset_range_to_display()
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   g_range_set = false;
}

extern "C" void zcport_mouse_display_changed(void)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   g_range_set = false;
   if (!mouse_driver)
      return;
   int x = mouse_x, y = mouse_y;
   clamp_to_range(&x, &y);
   set_position(x, y);
}

// ---------------------------------------------------------------------------
// installation and polling
// ---------------------------------------------------------------------------

extern "C" int install_mouse(void)
{
   if (mouse_driver)
      return 0;
   if (!system_driver || system_driver == &system_none)
      return -1;

   std::lock_guard<std::recursive_mutex> guard(input_lock());
   mouse_driver = &mousedrv_zcport;
   _mouse_installed = TRUE;
   g_range_set = false;
   mouse_b = 0;
   mouse_z = 0;
   mouse_w = 0;
   g_mickey_x = g_mickey_y = 0;
   g_scared_depth = 0;
   _mouse_screen = NULL;
   if (!mouse_sprite)
      set_mouse_sprite(NULL);
   int x = (SCREEN_W - 1) / 2, y = (SCREEN_H - 1) / 2;
   if (x < 0) x = 0;
   if (y < 0) y = 0;
   set_position(x, y);
   _add_exit_func(remove_mouse, "remove_mouse");
   return 3;
}

extern "C" void remove_mouse(void)
{
   if (!mouse_driver)
      return;
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   _mouse_screen = NULL;
   mouse_driver = NULL;
   _mouse_installed = FALSE;
   mouse_b = 0;
   g_scared_depth = 0;
   freeze_mouse_flag = FALSE;
   _remove_exit_func(remove_mouse);
}

extern "C" int poll_mouse(void)
{
   if (!mouse_driver)
      return -1;
   zcport_pump_events();
   return 0;
}

extern "C" int mouse_needs_poll(void)
{
   // Events only arrive when the platform pumps them.
   return TRUE;
}

extern "C" void enable_hardware_cursor(void)
{
}

extern "C" void disable_hardware_cursor(void)
{
}

extern "C" int show_os_cursor(int cursor)
{
   const ZCPORT_PLATFORM *p = zcport_get_platform();
   if (p && p->mouse_show_os_cursor)
      return p->mouse_show_os_cursor(cursor);
   return -1;
}

extern "C" int mouse_on_screen(void)
{
   return g_on_screen;
}

// ---------------------------------------------------------------------------
// pointer
// ---------------------------------------------------------------------------

// Shows the pointer on 'bmp' (NULL hides it). scare/unscare use this
// directly: the reference keeps GFX_HW_CURSOR through them.
static void set_mouse_screen(BITMAP *bmp)
{
   _mouse_screen = bmp;
   g_drawn_x = mouse_x - mouse_x_focus;
   g_drawn_y = mouse_y - mouse_y_focus;
}

extern "C" void show_mouse(BITMAP *bmp)
{
   if (!mouse_driver)
      return;
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   set_mouse_screen(bmp);
   /* the reference reports a hardware cursor while the program shows the
    * pointer on the screen (GFX_HW_CURSOR in gfx_capabilities) */
   zcport_gfx_mouse_shown(bmp);
}

extern "C" void scare_mouse(void)
{
   if (!mouse_driver)
      return;
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   BITMAP *hide = (_mouse_screen && is_same_bitmap(screen, _mouse_screen)) ? _mouse_screen : NULL;
   if (g_scared_depth < ZCPORT_SCARED_SIZE) {
      g_scared_screen[g_scared_depth] = hide;
      g_scared_freeze[g_scared_depth] = FALSE;
   }
   if (hide)
      set_mouse_screen(NULL);
   g_scared_depth++;
}

extern "C" void scare_mouse_area(int x, int y, int w, int h)
{
   if (!mouse_driver)
      return;
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   BITMAP *hide = NULL;
   int freeze = FALSE;

   if (_mouse_screen && is_same_bitmap(screen, _mouse_screen)) {
      BITMAP *spr = current_sprite();
      int px = mouse_x - mouse_x_focus;
      int py = mouse_y - mouse_y_focus;
      int pw = spr ? spr->w : 0;
      int ph = spr ? spr->h : 0;
      bool overlap = (px < x + w) && (px + pw > x) && (py < y + h) && (py + ph > y);
      if (overlap) {
         hide = _mouse_screen;
      }
      else if (!freeze_mouse_flag) {
         freeze = TRUE;
      }
   }

   if (g_scared_depth < ZCPORT_SCARED_SIZE) {
      g_scared_screen[g_scared_depth] = hide;
      g_scared_freeze[g_scared_depth] = freeze;
   }
   if (hide)
      set_mouse_screen(NULL);
   if (freeze)
      freeze_mouse_flag = TRUE;
   g_scared_depth++;
}

extern "C" void unscare_mouse(void)
{
   if (!mouse_driver)
      return;
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   if (g_scared_depth > 0)
      g_scared_depth--;
   if (g_scared_depth < ZCPORT_SCARED_SIZE) {
      if (g_scared_screen[g_scared_depth])
         set_mouse_screen(g_scared_screen[g_scared_depth]);
      if (g_scared_freeze[g_scared_depth])
         freeze_mouse_flag = FALSE;
      g_scared_screen[g_scared_depth] = NULL;
      g_scared_freeze[g_scared_depth] = FALSE;
   }
}

extern "C" void position_mouse(int x, int y)
{
   if (!mouse_driver)
      return;
   {
      std::lock_guard<std::recursive_mutex> guard(input_lock());
      set_position(x, y);
   }
   const ZCPORT_PLATFORM *p = zcport_get_platform();
   if (p && p->mouse_warp)
      p->mouse_warp(x, y);
}

extern "C" void position_mouse_z(int z)
{
   if (!mouse_driver)
      return;
   mouse_z = z;
}

extern "C" void position_mouse_w(int w)
{
   if (!mouse_driver)
      return;
   mouse_w = w;
}

extern "C" void set_mouse_range(int x1, int y_1, int x2, int y2)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   g_range_set = true;
   g_range_x1 = x1;
   g_range_y1 = y_1;
   g_range_x2 = x2;
   g_range_y2 = y2;
   if (!mouse_driver)
      return;
   int x = mouse_x, y = mouse_y;
   clamp_to_range(&x, &y);
   set_position(x, y);
}

extern "C" void set_mouse_speed(int xspeed, int yspeed)
{
   g_speed_x = xspeed;
   g_speed_y = yspeed;
}

extern "C" void get_mouse_mickeys(int *mickeyx, int *mickeyy)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   if (mickeyx)
      *mickeyx = g_mickey_x;
   if (mickeyy)
      *mickeyy = g_mickey_y;
   g_mickey_x = g_mickey_y = 0;
}

extern "C" void set_mouse_sprite(BITMAP *sprite)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   g_user_sprite = sprite;
   if (sprite) {
      mouse_sprite = sprite;
   }
   else {
      if (g_default_sprite) {
         destroy_bitmap(g_default_sprite);
         g_default_sprite = NULL;
      }
      g_default_sprite = make_default_sprite();
      mouse_sprite = g_default_sprite;
   }
}

extern "C" void set_mouse_sprite_focus(int x, int y)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   mouse_x_focus = x;
   mouse_y_focus = y;
}

extern "C" void select_mouse_cursor(int cursor)
{
   if (cursor < 0 || cursor >= AL_NUM_MOUSE_CURSORS)
      return;
   g_cursor = cursor;
}

extern "C" void set_mouse_cursor_bitmap(int cursor, BITMAP *bmp)
{
   if (cursor < 0 || cursor >= AL_NUM_MOUSE_CURSORS)
      return;
   g_cursor_bitmaps[cursor] = bmp;
}

extern "C" int zcport_mouse_get_pointer(BITMAP *display, BITMAP **sprite, int *x, int *y)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   if (!mouse_driver || !display || !_mouse_screen || !is_same_bitmap(_mouse_screen, display))
      return 0;
   if (g_cursor == MOUSE_CURSOR_NONE)
      return 0;
   BITMAP *spr = current_sprite();
   if (!spr)
      return 0;

   if (!freeze_mouse_flag) {
      g_drawn_x = mouse_x - mouse_x_focus;
      g_drawn_y = mouse_y - mouse_y_focus;
   }
   if (sprite)
      *sprite = spr;
   if (x)
      *x = g_drawn_x + (_mouse_screen->x_ofs - display->x_ofs);
   if (y)
      *y = g_drawn_y + (_mouse_screen->y_ofs - display->y_ofs);
   return 1;
}

// ---------------------------------------------------------------------------
// platform feed (zcport/input.h)
// ---------------------------------------------------------------------------

extern "C" void zcport_mouse_motion(int x, int y, int dx, int dy)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   if (!mouse_driver)
      return;
   g_mickey_x += dx;
   g_mickey_y += dy;
   clamp_to_range(&x, &y);
   if (x == mouse_x && y == mouse_y)
      return;
   set_position(x, y);
   if (mouse_callback)
      mouse_callback(MOUSE_FLAG_MOVE);
}

extern "C" void zcport_mouse_button(int button, int down)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   if (!mouse_driver || button < 0 || button > 30)
      return;
   int bit = 1 << button;
   int old = mouse_b;
   int now = down ? (old | bit) : (old & ~bit);
   if (now == old)
      return;
   mouse_b = now;
   if (mouse_callback) {
      int flags = 0;
      switch (button) {
         case 0: flags = down ? MOUSE_FLAG_LEFT_DOWN : MOUSE_FLAG_LEFT_UP; break;
         case 1: flags = down ? MOUSE_FLAG_RIGHT_DOWN : MOUSE_FLAG_RIGHT_UP; break;
         case 2: flags = down ? MOUSE_FLAG_MIDDLE_DOWN : MOUSE_FLAG_MIDDLE_UP; break;
      }
      if (flags)
         mouse_callback(flags);
   }
}

extern "C" void zcport_mouse_wheel(int dz, int dw)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   if (!mouse_driver)
      return;
   int flags = 0;
   if (dz) {
      mouse_z += dz;
      flags |= MOUSE_FLAG_MOVE_Z;
   }
   if (dw) {
      mouse_w += dw;
      flags |= MOUSE_FLAG_MOVE_W;
   }
   if (flags && mouse_callback)
      mouse_callback(flags);
}

extern "C" void zcport_mouse_set_on_screen(int on_screen)
{
   g_on_screen = on_screen ? TRUE : FALSE;
}

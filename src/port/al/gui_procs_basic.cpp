// zcport GUI: the standard dialog procedures that need neither text nor
// blitting: d_yield_proc, d_clear_proc, d_box_proc, d_keyboard_proc.
//
// Written from scratch for zcport (no Allegro code). What they draw and
// answer was measured on the reference build
// (tests/conformance/test_gui_engine_procs.cpp).

#include "gui_internal.h"

extern "C" int d_yield_proc(int msg, DIALOG *d, int c)
{
   (void)d;
   (void)c;
   // Gives the CPU away while the dialog is idle (the reference sleeps
   // about one timer tick here).
   if (msg == MSG_IDLE) {
      zcport_pump_events();
      zcport_sleep_ms(1);
   }
   return D_O_K;
}

extern "C" int d_clear_proc(int msg, DIALOG *d, int c)
{
   (void)c;
   if (msg == MSG_DRAW) {
      // Clears the whole GUI bitmap and leaves the clip rectangle set to
      // it. When the GUI bitmap is 'screen' itself its size is taken from
      // the graphics mode (SCREEN_W/H; none: nothing is cleared).
      BITMAP *bmp = gui_get_screen();
      int w = (bmp == screen) ? SCREEN_W : bmp->w;
      int h = (bmp == screen) ? SCREEN_H : bmp->h;
      set_clip_rect(bmp, 0, 0, w - 1, h - 1);
      clear_to_color(bmp, d->bg);
   }
   return D_O_K;
}

extern "C" int d_box_proc(int msg, DIALOG *d, int c)
{
   (void)c;
   if (msg == MSG_DRAW) {
      BITMAP *bmp = gui_get_screen();
      int fg = (d->flags & D_DISABLED) ? gui_mg_color : d->fg;
      rectfill(bmp, d->x + 1, d->y + 1, d->x + d->w - 2, d->y + d->h - 2, d->bg);
      rect(bmp, d->x, d->y, d->x + d->w - 1, d->y + d->h - 1, fg);
   }
   return D_O_K;
}

typedef int (*KeyboardCallback)(void);

extern "C" int d_keyboard_proc(int msg, DIALOG *d, int c)
{
   switch (msg) {
      case MSG_START:
         // invisible
         d->w = d->h = 0;
         break;

      case MSG_KEY:
         // the dialog manager matched the key field
         return ((KeyboardCallback)d->dp)();

      case MSG_XCHAR:
         // a scancode in d1 or d2
         if ((c >> 8) == d->d1 || (c >> 8) == d->d2)
            return ((KeyboardCallback)d->dp)() | D_USED_CHAR;
         break;
   }
   return D_O_K;
}

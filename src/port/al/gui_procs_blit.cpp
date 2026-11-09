// zcport GUI: the standard dialog procedures that draw bitmaps:
// d_bitmap_proc and d_icon_proc.
//
// Written from scratch for zcport (no Allegro code). What they draw and how
// they react was measured on the reference build (test_gui_blit_procs.cpp).
//
// This file needs the blit module (blit, stretch_blit); it uses only the
// public Allegro API.
//
// Objects of a dialog run by a UI host (zcport/gui_host.h) are shown by the
// host, so these procedures draw nothing for them.

#include <allegro.h>
#include "zcport/gui_host.h"

extern "C" int d_bitmap_proc(int msg, DIALOG *d, int c)
{
   (void)c;
   // A game procedure that calls this one to draw is not shown by the host.
   if (msg == MSG_DRAW && d->dp && (d->proc != d_bitmap_proc || !zcport_gui_host_owns(d))) {
      BITMAP *bmp = (BITMAP *)d->dp;
      blit(bmp, gui_get_screen(), 0, 0, d->x, d->y, d->w, d->h);
   }
   return D_O_K;
}

namespace
{
   // The icon's focus frame: every other pixel of the top and bottom rows
   // (from x1, short of x2) and of the left and right columns (from y1,
   // short of y2), in one colour.
   void dotted_frame(BITMAP *bmp, int x1, int y1, int x2, int y2, int color)
   {
      for (int x = x1; x < x2; x += 2) {
         putpixel(bmp, x, y1, color);
         putpixel(bmp, x, y2, color);
      }
      for (int y = y1; y < y2; y += 2) {
         putpixel(bmp, x1, y, color);
         putpixel(bmp, x2, y, color);
      }
   }
}

extern "C" int d_icon_proc(int msg, DIALOG *d, int c)
{
   if (msg != MSG_DRAW)
      return d_button_proc(msg, d, c);
   if (zcport_gui_host_owns(d))
      return D_O_K;

   BITMAP *gui_bmp = gui_get_screen();
   BITMAP *image = (BITMAP *)d->dp;
   int depth = 0;
   int indent = d->d2 ? d->d2 : 2;

   // the disabled image wins over the selected one; without a selected
   // image a selected icon is pushed in instead
   if ((d->flags & D_DISABLED) && d->dp3)
      image = (BITMAP *)d->dp3;
   else if ((d->flags & D_SELECTED) && d->dp2)
      image = (BITMAP *)d->dp2;
   if ((d->flags & D_SELECTED) && !d->dp2)
      depth = d->d1 ? d->d1 : 2;

   if (!image)
      return D_O_K;

   // the image, pushed in by 'depth' when selected without its own image
   stretch_blit(image, gui_bmp, 0, 0, image->w - depth, image->h - depth,
                d->x + depth, d->y + depth, d->w - depth, d->h - depth);

   // the focus frame moves with the pushed-in image (a pressed exit icon
   // has none)
   if ((d->flags & D_GOTFOCUS) &&
       (!(d->flags & D_SELECTED) || !(d->flags & D_EXIT)))
      dotted_frame(gui_bmp, d->x + indent + depth, d->y + indent + depth,
                   d->x + d->w - indent - 1 + depth, d->y + d->h - indent - 1 + depth, d->fg);

   if (depth > 0) {
      // the shadow on the top and left sides (over the frame)
      rectfill(gui_bmp, d->x, d->y, d->x + d->w - 1, d->y + depth - 1, d->bg);
      rectfill(gui_bmp, d->x, d->y, d->x + depth - 1, d->y + d->h - 1, d->bg);
   }

   return D_O_K;
}

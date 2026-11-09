// zcport GUI: the text helpers (gui_textout_ex, gui_strlen) and the
// standard dialog procedures that draw text or use colours: d_text_proc,
// d_ctext_proc, d_rtext_proc, d_button_proc, d_check_proc, d_radio_proc,
// d_shadow_box_proc, and the gui_*_proc hooks.
//
// Written from scratch for zcport (no Allegro code). What they draw and how
// they react was measured on the reference build (test_gui_text_procs.cpp).
//
// This file needs the text module (text_length, textout_ex, font) and
// makecol(); it uses only the public Allegro API, nothing zcport-internal.

#include <allegro.h>

#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// '&' markers
// ---------------------------------------------------------------------------

namespace
{
   // A copy of a string without its '&' shortcut markers ("&&" stands for
   // one '&'), and which character the last marker pointed at.
   struct Stripped
   {
      char *text;     // malloc'ed, in the current text encoding
      int marked;     // character index of the underlined character, -1 none

      explicit Stripped(const char *s)
      {
         if (!s)
            s = "";
         int size = ustrsizez(s);
         text = (char *)malloc(size > 0 ? size : 1);
         marked = -1;
         if (!text)
            return;

         char *out = text;
         int count = 0;
         const char *p = s;
         int c;
         while ((c = ugetxc(&p)) != 0) {
            if (c == '&') {
               if (ugetc(p) == '&') {
                  ugetxc(&p);           // "&&": a literal '&'
               }
               else {
                  marked = count;       // underline the next character
                  continue;
               }
            }
            out += usetc(out, c);
            count++;
         }
         usetc(out, 0);
      }

      ~Stripped()
      {
         free(text);
      }

      const char *str() const { return text ? text : ""; }

   private:
      Stripped(const Stripped &);
      Stripped &operator=(const Stripped &);
   };

   // Width of the first n characters of s.
   int prefix_length(const FONT *f, const char *s, int n)
   {
      int bytes = uoffset(s, n);
      char *tmp = (char *)malloc(bytes + 8);
      if (!tmp)
         return 0;
      memcpy(tmp, s, bytes);
      usetc(tmp + bytes, 0);
      int len = text_length(f, tmp);
      free(tmp);
      return len;
   }

   // Width of character number n of s (0 past the end).
   int char_length_at(const FONT *f, const char *s, int n)
   {
      char tmp[16];
      int c = ugetat(s, n);
      if (!c)
         return 0;
      int bytes = usetc(tmp, c);
      usetc(tmp + bytes, 0);
      return text_length(f, tmp);
   }
}

extern "C" int gui_textout_ex(BITMAP *bmp, AL_CONST char *s, int x, int y, int color, int bg, int centre)
{
   Stripped text(s);
   int len = text_length(font, text.str());

   if (centre)
      x -= len / 2;

   if (bmp) {
      textout_ex(bmp, font, text.str(), x, y, color, bg);

      if (text.marked >= 0) {
         int ux = x + prefix_length(font, text.str(), text.marked);
         int uw = char_length_at(font, text.str(), text.marked);
         int uy = y + text_height(font) - gui_font_baseline;
         hline(bmp, ux, uy, ux + uw - 1, color);
      }
   }

   return len;
}

extern "C" int gui_strlen(AL_CONST char *s)
{
   Stripped text(s);
   return text_length(font, text.str());
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

namespace
{
   // The focus rectangle: a one pixel frame alternating between fg and bg,
   // starting with fg in the (x1, y1) corner.
   void dotted_rect(BITMAP *bmp, int x1, int y1, int x2, int y2, int fg, int bg)
   {
      for (int x = x1; x <= x2; x++) {
         putpixel(bmp, x, y1, ((x - x1 + y1 - y1) & 1) ? bg : fg);
         putpixel(bmp, x, y2, ((x - x1 + y2 - y1) & 1) ? bg : fg);
      }
      for (int y = y1 + 1; y < y2; y++) {
         putpixel(bmp, x1, y, ((y - y1) & 1) ? bg : fg);
         putpixel(bmp, x2, y, ((x2 - x1 + y - y1) & 1) ? bg : fg);
      }
   }

   inline int foreground(const DIALOG *d)
   {
      return (d->flags & D_DISABLED) ? gui_mg_color : d->fg;
   }

   inline const char *text_of(const DIALOG *d)
   {
      return d->dp ? (const char *)d->dp : "";
   }

   enum Align { ALIGN_LEFT, ALIGN_CENTRE, ALIGN_RIGHT };

   int text_proc(int msg, DIALOG *d, Align align)
   {
      if (msg != MSG_DRAW)
         return D_O_K;

      FONT *old_font = font;
      if (d->dp2)
         font = (FONT *)d->dp2;

      BITMAP *bmp = gui_get_screen();
      const char *s = text_of(d);
      switch (align) {
         case ALIGN_LEFT:
            gui_textout_ex(bmp, s, d->x, d->y, foreground(d), d->bg, FALSE);
            break;
         case ALIGN_CENTRE:
            gui_textout_ex(bmp, s, d->x + d->w / 2, d->y, foreground(d), d->bg, TRUE);
            break;
         case ALIGN_RIGHT:
            gui_textout_ex(bmp, s, d->x + d->w - gui_strlen(s), d->y, foreground(d), d->bg, FALSE);
            break;
      }

      font = old_font;
      return D_O_K;
   }

   inline bool mouse_inside(const DIALOG *d)
   {
      int x = gui_mouse_x();
      int y = gui_mouse_y();
      return x >= d->x && y >= d->y && x < d->x + d->w && y < d->y + d->h;
   }
}

// ---------------------------------------------------------------------------
// boxes and text
// ---------------------------------------------------------------------------

extern "C" int d_shadow_box_proc(int msg, DIALOG *d, int c)
{
   (void)c;
   if (msg == MSG_DRAW) {
      BITMAP *bmp = gui_get_screen();
      int black = makecol(0, 0, 0);
      rectfill(bmp, d->x + 1, d->y + 1, d->x + d->w - 3, d->y + d->h - 3, d->bg);
      rect(bmp, d->x, d->y, d->x + d->w - 2, d->y + d->h - 2, foreground(d));
      vline(bmp, d->x + d->w - 1, d->y + 1, d->y + d->h - 1, black);
      hline(bmp, d->x + 1, d->y + d->h - 1, d->x + d->w - 1, black);
   }
   return D_O_K;
}

extern "C" int d_text_proc(int msg, DIALOG *d, int c)
{
   (void)c;
   return text_proc(msg, d, ALIGN_LEFT);
}

extern "C" int d_ctext_proc(int msg, DIALOG *d, int c)
{
   (void)c;
   return text_proc(msg, d, ALIGN_CENTRE);
}

extern "C" int d_rtext_proc(int msg, DIALOG *d, int c)
{
   (void)c;
   return text_proc(msg, d, ALIGN_RIGHT);
}

// ---------------------------------------------------------------------------
// buttons
// ---------------------------------------------------------------------------

extern "C" int d_button_proc(int msg, DIALOG *d, int c)
{
   (void)c;
   switch (msg) {
      case MSG_DRAW: {
         BITMAP *bmp = gui_get_screen();
         bool sel = (d->flags & D_SELECTED) != 0;
         int g = sel ? 1 : 0;                    // pressed in by one pixel
         int ink = sel ? d->bg : foreground(d);  // frame and text
         int paper = sel ? foreground(d) : d->bg;

         rectfill(bmp, d->x + 1 + g, d->y + 1 + g, d->x + d->w - 3 + g, d->y + d->h - 3 + g, paper);
         rect(bmp, d->x + g, d->y + g, d->x + d->w - 2 + g, d->y + d->h - 2 + g, ink);
         gui_textout_ex(bmp, text_of(d), d->x + d->w / 2 + g, d->y + d->h / 2 - text_height(font) / 2 + g,
                        ink, -1, TRUE);

         if (sel) {
            vline(bmp, d->x, d->y, d->y + d->h - 2, d->bg);
            hline(bmp, d->x, d->y, d->x + d->w - 2, d->bg);
         }
         else {
            int black = makecol(0, 0, 0);
            vline(bmp, d->x + d->w - 1, d->y + 1, d->y + d->h - 2, black);
            hline(bmp, d->x + 1, d->y + d->h - 1, d->x + d->w - 1, black);
         }

         if ((d->flags & D_GOTFOCUS) && (!sel || !(d->flags & D_EXIT)))
            dotted_rect(bmp, d->x + 1 + g, d->y + 1 + g, d->x + d->w - 3 + g, d->y + d->h - 3 + g, ink, paper);
         break;
      }

      case MSG_WANTFOCUS:
         return D_WANTFOCUS;

      case MSG_KEY:
         // like a click: exit, or toggle
         if (d->flags & D_EXIT)
            return D_CLOSE;
         d->flags ^= D_SELECTED;
         object_message(d, MSG_DRAW, 0);
         break;

      case MSG_CLICK: {
         // Follow the mouse until the button is released. While the mouse
         // is over it an exit button shows pressed (selected), a toggle
         // button the opposite of its state; elsewhere the original look.
         int invert = (d->flags & D_EXIT) ? 0 : (d->flags & D_SELECTED);
         while (gui_mouse_b()) {
            int want = (mouse_inside(d) ? D_SELECTED : 0) ^ invert;
            if ((d->flags & D_SELECTED) != want) {
               d->flags ^= D_SELECTED;
               object_message(d, MSG_DRAW, 0);
            }
            broadcast_dialog_message(MSG_IDLE, 0);   // others keep animating
         }
         // an exit button that ends up pressed closes the dialog
         if ((d->flags & D_SELECTED) && (d->flags & D_EXIT)) {
            d->flags ^= D_SELECTED;
            return D_CLOSE;
         }
         break;
      }
   }
   return D_O_K;
}

extern "C" int d_check_proc(int msg, DIALOG *d, int c)
{
   if (msg == MSG_DRAW) {
      BITMAP *bmp = gui_get_screen();
      int fg = foreground(d);
      int th = text_height(font);
      int ty = d->y + (d->h - (th - gui_font_baseline)) / 2;
      int bx, tx;

      if (d->d1) {
         bx = d->x;
         tx = d->x + th + th / 2;
      }
      else {
         tx = d->x;
         bx = d->x + gui_strlen(text_of(d)) + th / 2;
      }

      rectfill(bmp, d->x, d->y, d->x + d->w - 1, d->y + d->h - 1, d->bg);
      if (d->flags & D_GOTFOCUS)
         dotted_rect(bmp, d->x, d->y, d->x + d->w - 1, d->y + d->h - 1, fg, d->bg);
      gui_textout_ex(bmp, text_of(d), tx, ty, fg, -1, FALSE);
      rect(bmp, bx, ty, bx + th - 1, ty + th - 1, fg);
      if (d->flags & D_SELECTED) {
         line(bmp, bx + 1, ty + 1, bx + th - 2, ty + th - 2, fg);
         line(bmp, bx + th - 2, ty + 1, bx + 1, ty + th - 2, fg);
      }
      return D_O_K;
   }
   return d_button_proc(msg, d, c);
}

extern "C" int d_radio_proc(int msg, DIALOG *d, int c)
{
   switch (msg) {
      case MSG_DRAW: {
         BITMAP *bmp = gui_get_screen();
         int fg = foreground(d);
         int th = text_height(font);
         int ty = d->y + (d->h - (th - gui_font_baseline)) / 2;

         rectfill(bmp, d->x, d->y, d->x + d->w - 1, d->y + d->h - 1, d->bg);
         if (d->flags & D_GOTFOCUS)
            dotted_rect(bmp, d->x, d->y, d->x + d->w - 1, d->y + d->h - 1, fg, d->bg);
         gui_textout_ex(bmp, text_of(d), d->x + th + th / 2, ty, fg, -1, FALSE);

         // the square frame follows the text; the circle and the "selected"
         // marks are centred on the object
         int cx = d->x + th / 2;
         int cy = d->y + d->h / 2;
         int r = th / 4;
         if (d->d2 == 1) {
            rect(bmp, d->x, ty, d->x + th - 1, ty + th - 1, fg);
            if (d->flags & D_SELECTED)
               rectfill(bmp, cx - r, cy - r, cx + r - 1, cy + r - 1, fg);
         }
         else {
            circle(bmp, cx, cy, th / 2, fg);
            if (d->flags & D_SELECTED)
               circlefill(bmp, cx, cy, r, fg);
         }
         return D_O_K;
      }

      case MSG_KEY:
      case MSG_CLICK: {
         // A selected radio button ignores this. Otherwise it behaves like
         // a toggle button; once selected it deselects the rest of its
         // group (d1), without hearing its own message.
         if (d->flags & D_SELECTED)
            return D_O_K;
         int r = d_button_proc(msg, d, c);
         if (d->flags & D_SELECTED) {
            d->flags &= ~D_SELECTED;
            broadcast_dialog_message(MSG_RADIO, d->d1);
            d->flags |= D_SELECTED;
         }
         return r;
      }

      case MSG_RADIO:
         if (c == d->d1 && (d->flags & D_SELECTED)) {
            d->flags &= ~D_SELECTED;
            object_message(d, MSG_DRAW, 0);
         }
         return D_O_K;
   }
   return d_button_proc(msg, d, c);
}

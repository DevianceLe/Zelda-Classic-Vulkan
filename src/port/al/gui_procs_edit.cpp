// zcport GUI: the editing and list dialog procedures (d_edit_proc,
// d_list_proc, d_text_list_proc, d_textbox_proc, d_slider_proc) and the
// gui_*_proc hooks of the standard dialogs.
//
// Written from scratch for zcport (no Allegro code), after the Allegro 4.4
// documentation. The player only refers to d_edit_proc and d_list_proc (it
// compares procedure pointers and replaces the list procedure before a
// dialog runs) and never runs these; their drawing and key handling follow
// the reference in the common cases but were not compared exhaustively (see
// docs/zplayer-port.md).
//
// Needs the text module; uses only the public Allegro API.

#include <allegro.h>

#include <stdlib.h>
#include <string.h>

namespace
{
   inline int fg_of(const DIALOG *d)
   {
      return (d->flags & D_DISABLED) ? gui_mg_color : d->fg;
   }

   // Width of the first n characters of s.
   int prefix_width(const char *s, int n)
   {
      int bytes = uoffset(s, n);
      char *tmp = (char *)malloc(bytes + 8);
      if (!tmp)
         return 0;
      memcpy(tmp, s, bytes);
      usetc(tmp + bytes, 0);
      int w = text_length(font, tmp);
      free(tmp);
      return w;
   }

   int char_width(int c)
   {
      char tmp[16];
      int n = usetc(tmp, c ? c : ' ');
      usetc(tmp + n, 0);
      return text_length(font, tmp);
   }

   void dotted(BITMAP *bmp, int x1, int y1, int x2, int y2, int fg, int bg)
   {
      for (int x = x1; x <= x2; x++) {
         putpixel(bmp, x, y1, ((x - x1) & 1) ? bg : fg);
         putpixel(bmp, x, y2, ((x - x1 + y2 - y1) & 1) ? bg : fg);
      }
      for (int y = y1 + 1; y < y2; y++) {
         putpixel(bmp, x1, y, ((y - y1) & 1) ? bg : fg);
         putpixel(bmp, x2, y, ((x2 - x1 + y - y1) & 1) ? bg : fg);
      }
   }
}

// ---------------------------------------------------------------------------
// d_edit_proc: dp = text buffer, d1 = maximum length, d2 = cursor position
// ---------------------------------------------------------------------------

extern "C" int d_edit_proc(int msg, DIALOG *d, int c)
{
   char *s = (char *)d->dp;
   int len = s ? ustrlen(s) : 0;

   if (d->d2 > len)
      d->d2 = len;
   if (d->d2 < 0)
      d->d2 = 0;

   switch (msg) {
      case MSG_START:
         d->d2 = len;
         break;

      case MSG_DRAW: {
         BITMAP *bmp = gui_get_screen();
         int fg = fg_of(d);
         int th = text_height(font);

         // first character shown: the cursor must fit in the box
         int first = 0;
         int cursor_w = char_width(d->d2 < len ? ugetat(s, d->d2) : ' ');
         while (first < d->d2 && prefix_width(s + uoffset(s, first), d->d2 - first) + cursor_w > d->w)
            first++;

         rectfill(bmp, d->x, d->y, d->x + d->w - 1, d->y + th - 1, d->bg);
         int x = d->x;
         for (int i = first; i <= len && x < d->x + d->w; i++) {
            int ch = (i < len) ? ugetat(s, i) : ' ';
            char buf[16];
            int n = usetc(buf, ch);
            usetc(buf + n, 0);
            int cw = text_length(font, buf);
            bool cursor = (i == d->d2) && (d->flags & D_GOTFOCUS);
            if (i < len || cursor) {
               if (cursor) {
                  rectfill(bmp, x, d->y, x + cw - 1, d->y + th - 1, fg);
                  textout_ex(bmp, font, buf, x, d->y, d->bg, -1);
               }
               else {
                  textout_ex(bmp, font, buf, x, d->y, fg, -1);
               }
            }
            x += cw;
         }
         break;
      }

      case MSG_CLICK: {
         // the cursor goes to the character under the mouse
         int mx = gui_mouse_x();
         int x = d->x;
         d->d2 = len;
         for (int i = 0; i < len; i++) {
            int cw = char_width(ugetat(s, i));
            if (mx < x + cw) {
               d->d2 = i;
               break;
            }
            x += cw;
         }
         object_message(d, MSG_DRAW, 0);
         break;
      }

      case MSG_WANTFOCUS:
      case MSG_LOSTFOCUS:
      case MSG_KEY:
         // keeps the focus until another object takes it
         return D_WANTFOCUS;

      case MSG_CHAR: {
         int scan = c >> 8;
         int ascii = c & 0xFF;
         if (scan == KEY_LEFT) {
            if (d->d2 > 0)
               d->d2--;
         }
         else if (scan == KEY_RIGHT) {
            if (d->d2 < len)
               d->d2++;
         }
         else if (scan == KEY_HOME) {
            d->d2 = 0;
         }
         else if (scan == KEY_END) {
            d->d2 = len;
         }
         else if (scan == KEY_DEL) {
            if (d->d2 < len)
               uremove(s, d->d2);
         }
         else if (scan == KEY_BACKSPACE || ascii == 8) {
            if (d->d2 > 0) {
               d->d2--;
               uremove(s, d->d2);
            }
         }
         else if (ascii == '\r') {
            if (d->flags & D_EXIT) {
               object_message(d, MSG_DRAW, 0);
               return D_CLOSE;
            }
            return D_O_K;
         }
         else if (scan == KEY_TAB || ascii == 9) {
            return D_O_K;
         }
         else {
            // printable characters come as MSG_UCHAR
            return D_O_K;
         }
         object_message(d, MSG_DRAW, 0);
         return D_USED_CHAR;
      }

      case MSG_UCHAR:
         if (c >= ' ' && uisok(c)) {
            if (len < d->d1) {
               uinsert(s, d->d2, c);
               d->d2++;
               object_message(d, MSG_DRAW, 0);
            }
            return D_USED_CHAR;
         }
         break;
   }
   return D_O_K;
}

// ---------------------------------------------------------------------------
// d_list_proc: dp = list function, d1 = selection, d2 = scroll position,
// dp2 = optional multiple selection flags
// ---------------------------------------------------------------------------

typedef const char *(*ListFunc)(int index, int *list_size);

namespace
{
   int list_size(DIALOG *d)
   {
      int size = 0;
      ((ListFunc)d->dp)(-1, &size);
      return size;
   }

   int visible_rows(DIALOG *d)
   {
      int th = text_height(font);
      int rows = (th > 0) ? (d->h - 4) / th : 0;
      return rows > 0 ? rows : 1;
   }

   void keep_selection_visible(DIALOG *d, int size)
   {
      int rows = visible_rows(d);
      if (d->d1 >= size)
         d->d1 = size - 1;
      if (d->d1 < 0)
         d->d1 = 0;
      if (d->d2 > d->d1)
         d->d2 = d->d1;
      if (d->d1 >= d->d2 + rows)
         d->d2 = d->d1 - rows + 1;
      if (d->d2 > size - rows)
         d->d2 = size - rows;
      if (d->d2 < 0)
         d->d2 = 0;
   }

   void draw_list(DIALOG *d)
   {
      BITMAP *bmp = gui_get_screen();
      int fg = fg_of(d);
      int th = text_height(font);
      int size = list_size(d);
      int rows = visible_rows(d);
      bool bar = size > rows;
      int bar_w = bar ? 12 : 0;
      int right = d->x + d->w - 2 - bar_w;
      char *sel = (char *)d->dp2;

      rect(bmp, d->x, d->y, d->x + d->w - 1, d->y + d->h - 1, fg);
      rectfill(bmp, d->x + 1, d->y + 1, right, d->y + d->h - 2, d->bg);

      for (int r = 0; r < rows; r++) {
         int i = d->d2 + r;
         int y = d->y + 2 + r * th;
         if (i >= size)
            break;
         bool selected = (i == d->d1) || (sel && sel[i]);
         int ink = selected ? d->bg : fg;
         if (selected)
            rectfill(bmp, d->x + 2, y, right - 1, y + th - 1, fg);
         const char *text = ((ListFunc)d->dp)(i, NULL);
         if (text)
            textout_ex(bmp, font, text, d->x + 8, y, ink, -1);
         if (i == d->d1 && (d->flags & D_GOTFOCUS))
            dotted(bmp, d->x + 2, y, right - 1, y + th - 1, fg, d->bg);
      }

      if (bar) {
         int bx = right + 1;
         rectfill(bmp, bx, d->y + 1, d->x + d->w - 2, d->y + d->h - 2, d->bg);
         vline(bmp, bx, d->y + 1, d->y + d->h - 2, fg);
         int track = d->h - 4;
         int len = track * rows / size;
         if (len < 4)
            len = 4;
         int pos = (size > rows) ? (track - len) * d->d2 / (size - rows) : 0;
         int hy = d->y + 2 + pos;
         for (int y = hy; y < hy + len; y++)
            for (int x = bx + 2; x < d->x + d->w - 2; x++)
               if ((x + y) & 1)
                  putpixel(bmp, x, y, fg);
      }

      if (d->flags & D_GOTFOCUS)
         dotted(bmp, d->x + 1, d->y + 1, d->x + d->w - 2, d->y + d->h - 2, fg, d->bg);
   }
}

extern "C" int d_list_proc(int msg, DIALOG *d, int c)
{
   int size = d->dp ? list_size(d) : 0;

   switch (msg) {
      case MSG_START:
         keep_selection_visible(d, size);
         break;

      case MSG_DRAW:
         if (d->dp)
            draw_list(d);
         break;

      case MSG_WANTFOCUS:
         return D_WANTFOCUS;

      case MSG_KEY:
         if (d->flags & D_EXIT)
            return D_CLOSE;
         break;

      case MSG_CLICK: {
         int th = text_height(font);
         int rows = visible_rows(d);
         while (gui_mouse_b()) {
            int r = (th > 0) ? (gui_mouse_y() - d->y - 2) / th : 0;
            if (r < 0)
               r = 0;
            if (r >= rows)
               r = rows - 1;
            int i = d->d2 + r;
            if (i < size && i != d->d1) {
               d->d1 = i;
               object_message(d, MSG_DRAW, 0);
            }
            broadcast_dialog_message(MSG_IDLE, 0);
         }
         break;
      }

      case MSG_DCLICK:
         if (d->flags & D_EXIT)
            return D_CLOSE;
         break;

      case MSG_WHEEL: {
         int old = d->d2;
         d->d2 -= c;
         int rows = visible_rows(d);
         if (d->d2 > size - rows)
            d->d2 = size - rows;
         if (d->d2 < 0)
            d->d2 = 0;
         if (d->d2 != old)
            object_message(d, MSG_DRAW, 0);
         break;
      }

      case MSG_CHAR: {
         int scan = c >> 8;
         int old = d->d1;
         int rows = visible_rows(d);
         switch (scan) {
            case KEY_UP:   d->d1--; break;
            case KEY_DOWN: d->d1++; break;
            case KEY_HOME: d->d1 = 0; break;
            case KEY_END:  d->d1 = size - 1; break;
            case KEY_PGUP: d->d1 -= rows - 1; break;
            case KEY_PGDN: d->d1 += rows - 1; break;
            default:
               return D_O_K;
         }
         keep_selection_visible(d, size);
         if (d->d1 != old)
            object_message(d, MSG_DRAW, 0);
         return D_USED_CHAR;
      }
   }
   return D_O_K;
}

// d_text_list_proc: a list box with type-ahead (dp3 holds the typed length).
extern "C" int d_text_list_proc(int msg, DIALOG *d, int c)
{
   switch (msg) {
      case MSG_START:
      case MSG_CLICK:
      case MSG_DCLICK:
      case MSG_WANTFOCUS:
      case MSG_LOSTFOCUS:
         d->dp3 = 0;
         break;

      case MSG_CHAR: {
         int ascii = c & 0xFF;
         if (ascii >= ' ' && ascii != 127 && d->dp) {
            int size = list_size(d);
            int typed = (int)(size_t)d->dp3;
            for (int i = 0; i < size; i++) {
               const char *text = ((ListFunc)d->dp)(i, NULL);
               if (!text)
                  continue;
               int n = ustrlen(text);
               if (n > typed && utolower(ugetat(text, typed)) == utolower(ascii)) {
                  bool same = true;
                  if (d->d1 >= 0 && d->d1 < size && typed > 0) {
                     const char *cur = ((ListFunc)d->dp)(d->d1, NULL);
                     same = cur && ustrnicmp(cur, text, typed) == 0;
                  }
                  if (same) {
                     d->d1 = i;
                     d->dp3 = (void *)(size_t)(typed + 1);
                     keep_selection_visible(d, size);
                     object_message(d, MSG_DRAW, 0);
                     return D_USED_CHAR;
                  }
               }
            }
            return D_USED_CHAR;
         }
         d->dp3 = 0;
         break;
      }
   }
   return d_list_proc(msg, d, c);
}

// ---------------------------------------------------------------------------
// d_textbox_proc: dp = text, word wrapped (character wrapped if
// D_SELECTED); d1 = number of lines, d2 = first line shown
// ---------------------------------------------------------------------------

namespace
{
   // Breaks the text into lines of at most 'width' pixels. Calls out(start,
   // length) per line (byte offsets); returns the number of lines.
   template <class Out>
   int wrap_lines(const char *s, int width, bool by_char, Out out)
   {
      int lines = 0;
      int pos = 0;
      int len = ustrlen(s);
      while (pos < len) {
         int end = pos;
         int last_space = -1;
         int w = 0;
         while (end < len) {
            int ch = ugetat(s, end);
            if (ch == '\n')
               break;
            int cw = char_width(ch);
            if (w + cw > width && end > pos)
               break;
            if (ch == ' ')
               last_space = end;
            w += cw;
            end++;
         }
         int next = end;
         if (end < len && ugetat(s, end) == '\n') {
            next = end + 1;
         }
         else if (end < len && !by_char && last_space > pos) {
            end = last_space;
            next = last_space + 1;
         }
         out(pos, end - pos);
         lines++;
         pos = next;
      }
      return lines;
   }

   struct DrawLine
   {
      BITMAP *bmp;
      const char *s;
      int x, y, th, first, line, bottom, fg;
      void operator()(int start, int n)
      {
         if (line >= first) {
            int ly = y + (line - first) * th;
            if (ly + th <= bottom) {
               int b0 = uoffset(s, start);
               int b1 = uoffset(s, start + n);
               char *tmp = (char *)malloc(b1 - b0 + 8);
               if (tmp) {
                  memcpy(tmp, s + b0, b1 - b0);
                  usetc(tmp + (b1 - b0), 0);
                  textout_ex(bmp, font, tmp, x, ly, fg, -1);
                  free(tmp);
               }
            }
         }
         line++;
      }
   };

   struct CountLine
   {
      void operator()(int, int) {}
   };
}

extern "C" int d_textbox_proc(int msg, DIALOG *d, int c)
{
   const char *s = d->dp ? (const char *)d->dp : "";
   int th = text_height(font);
   int rows = (th > 0) ? (d->h - 8) / th : 1;
   if (rows < 1)
      rows = 1;
   bool by_char = (d->flags & D_SELECTED) != 0;

   switch (msg) {
      case MSG_START:
         d->d1 = wrap_lines(s, d->w - 8, by_char, CountLine());
         break;

      case MSG_DRAW: {
         BITMAP *bmp = gui_get_screen();
         int fg = fg_of(d);
         int lines = wrap_lines(s, d->w - 8, by_char, CountLine());
         bool bar = lines > rows;
         int text_w = d->w - 8 - (bar ? 12 : 0);
         lines = wrap_lines(s, text_w, by_char, CountLine());
         d->d1 = lines;
         rect(bmp, d->x, d->y, d->x + d->w - 1, d->y + d->h - 1, fg);
         rectfill(bmp, d->x + 1, d->y + 1, d->x + d->w - 2, d->y + d->h - 2, d->bg);
         DrawLine dl = { bmp, s, d->x + 4, d->y + 4, th, d->d2, 0, d->y + d->h - 2, fg };
         wrap_lines(s, text_w, by_char, dl);
         if (bar)
            vline(bmp, d->x + d->w - 13, d->y + 1, d->y + d->h - 2, fg);
         break;
      }

      case MSG_WANTFOCUS:
         return (d->d1 > rows) ? D_WANTFOCUS : D_O_K;

      case MSG_CHAR: {
         int scan = c >> 8;
         int old = d->d2;
         switch (scan) {
            case KEY_UP:   d->d2--; break;
            case KEY_DOWN: d->d2++; break;
            case KEY_HOME: d->d2 = 0; break;
            case KEY_END:  d->d2 = d->d1 - rows; break;
            case KEY_PGUP: d->d2 -= rows - 1; break;
            case KEY_PGDN: d->d2 += rows - 1; break;
            default:
               return D_O_K;
         }
         if (d->d2 > d->d1 - rows)
            d->d2 = d->d1 - rows;
         if (d->d2 < 0)
            d->d2 = 0;
         if (d->d2 != old)
            object_message(d, MSG_DRAW, 0);
         return D_USED_CHAR;
      }

      case MSG_WHEEL: {
         int old = d->d2;
         d->d2 -= c;
         if (d->d2 > d->d1 - rows)
            d->d2 = d->d1 - rows;
         if (d->d2 < 0)
            d->d2 = 0;
         if (d->d2 != old)
            object_message(d, MSG_DRAW, 0);
         break;
      }
   }
   return D_O_K;
}

// ---------------------------------------------------------------------------
// d_slider_proc: value d2 in 0..d1; vertical if h >= w; dp = handle bitmap
// (optional), dp2 = int callback(void *dp3, int d2)
// ---------------------------------------------------------------------------

typedef int (*SliderCallback)(void *dp3, int d2);

namespace
{
   int slider_changed(DIALOG *d)
   {
      object_message(d, MSG_DRAW, 0);
      if (d->dp2)
         return ((SliderCallback)d->dp2)(d->dp3, d->d2);
      return D_O_K;
   }
}

extern "C" int d_slider_proc(int msg, DIALOG *d, int c)
{
   bool vertical = d->h >= d->w;
   int range = d->d1 > 0 ? d->d1 : 1;

   switch (msg) {
      case MSG_DRAW: {
         BITMAP *bmp = gui_get_screen();
         int fg = fg_of(d);
         rectfill(bmp, d->x, d->y, d->x + d->w - 1, d->y + d->h - 1, d->bg);
         if (vertical) {
            int cx = d->x + d->w / 2;
            rectfill(bmp, cx - 1, d->y, cx + 1, d->y + d->h - 1, fg);
            int hy = d->y + d->h - 1 - (d->h - 1) * d->d2 / range;
            rectfill(bmp, d->x + 1, hy - 2, d->x + d->w - 2, hy + 2, fg);
         }
         else {
            int cy = d->y + d->h / 2;
            rectfill(bmp, d->x, cy - 1, d->x + d->w - 1, cy + 1, fg);
            int hx = d->x + (d->w - 1) * d->d2 / range;
            rectfill(bmp, hx - 2, d->y + 1, hx + 2, d->y + d->h - 2, fg);
         }
         if (d->flags & D_GOTFOCUS)
            dotted(bmp, d->x, d->y, d->x + d->w - 1, d->y + d->h - 1, fg, d->bg);
         break;
      }

      case MSG_WANTFOCUS:
         return D_WANTFOCUS;

      case MSG_CHAR: {
         int scan = c >> 8;
         int old = d->d2;
         int step = (range >= 8) ? range / 8 : 1;
         switch (scan) {
            case KEY_LEFT:  case KEY_DOWN: d->d2--; break;
            case KEY_RIGHT: case KEY_UP:   d->d2++; break;
            case KEY_PGDN: d->d2 -= step; break;
            case KEY_PGUP: d->d2 += step; break;
            case KEY_HOME: d->d2 = 0; break;
            case KEY_END:  d->d2 = d->d1; break;
            default:
               return D_O_K;
         }
         if (d->d2 < 0)
            d->d2 = 0;
         if (d->d2 > d->d1)
            d->d2 = d->d1;
         if (d->d2 != old)
            return slider_changed(d) | D_USED_CHAR;
         return D_USED_CHAR;
      }

      case MSG_WHEEL: {
         int old = d->d2;
         d->d2 += c;
         if (d->d2 < 0)
            d->d2 = 0;
         if (d->d2 > d->d1)
            d->d2 = d->d1;
         if (d->d2 != old)
            return slider_changed(d);
         break;
      }

      case MSG_CLICK: {
         int ret = D_O_K;
         while (gui_mouse_b()) {
            int v;
            if (vertical)
               v = (d->y + d->h - 1 - gui_mouse_y()) * range / (d->h > 1 ? d->h - 1 : 1);
            else
               v = (gui_mouse_x() - d->x) * range / (d->w > 1 ? d->w - 1 : 1);
            if (v < 0)
               v = 0;
            if (v > d->d1)
               v = d->d1;
            if (v != d->d2) {
               d->d2 = v;
               ret |= slider_changed(d);
            }
            broadcast_dialog_message(MSG_IDLE, 0);
         }
         return ret;
      }
   }
   return D_O_K;
}

// ---------------------------------------------------------------------------
// hooks used by the standard dialogs (alert, file selector)
// ---------------------------------------------------------------------------

DIALOG_PROC gui_shadow_box_proc = d_shadow_box_proc;
DIALOG_PROC gui_ctext_proc = d_ctext_proc;
DIALOG_PROC gui_button_proc = d_button_proc;
DIALOG_PROC gui_edit_proc = d_edit_proc;
DIALOG_PROC gui_list_proc = d_list_proc;
DIALOG_PROC gui_text_list_proc = d_text_list_proc;

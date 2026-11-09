// zcport GUI: the dialog manager.
//
// init_dialog / update_dialog / shutdown_dialog, do_dialog, the message
// helpers (object_message, dialog_message, broadcast_dialog_message,
// offer_focus) and the small dialog utilities.
//
// Written from scratch for zcport (no Allegro code). The behaviour follows
// the Allegro 4.4 documentation, and where the documentation is silent it
// was measured on the reference build with scripted input and logging dialog
// procedures (tests/conformance/test_gui_engine_*.cpp). The notes below
// describe what update_dialog() does, step by step, because quests' and the
// game's dialog procedures can observe every message and its order.
//
// A UI host (zcport/gui_host.h) can take over dialogs: init_dialog() offers
// each new dialog to it, and update/shutdown_dialog() of a hosted dialog
// only call the host. Without a host nothing here changes.

#include "gui_internal.h"
#include "zcport/gui_host.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <new>

using namespace zcport_gui;

// ---------------------------------------------------------------------------
// public variables
// ---------------------------------------------------------------------------

DIALOG *active_dialog = NULL;
int dialog_count = 0;
MENU *active_menu = NULL;

int gui_mouse_focus = TRUE;
int gui_fg_color = 255;
int gui_mg_color = 8;
int gui_bg_color = 0;
int gui_font_baseline = 0;

void (*gui_menu_draw_menu)(int x, int y, int w, int h) = NULL;
void (*gui_menu_draw_menu_item)(MENU *m, int x, int y, int w, int h, int bar, int sel) = NULL;

// The game busy-waits on these (while(gui_mouse_b());), so they poll, which
// pumps the platform's events.
static int default_gui_mouse_x(void) { poll_mouse(); return mouse_x; }
static int default_gui_mouse_y(void) { poll_mouse(); return mouse_y; }
static int default_gui_mouse_z(void) { poll_mouse(); return mouse_z; }
static int default_gui_mouse_b(void) { poll_mouse(); return mouse_b; }

int (*gui_mouse_x)(void) = default_gui_mouse_x;
int (*gui_mouse_y)(void) = default_gui_mouse_y;
int (*gui_mouse_z)(void) = default_gui_mouse_z;
int (*gui_mouse_b)(void) = default_gui_mouse_b;

// ---------------------------------------------------------------------------
// the GUI bitmap
// ---------------------------------------------------------------------------

static BITMAP *g_gui_bitmap = NULL;      // NULL: whatever 'screen' is now

extern "C" void gui_set_screen(BITMAP *bmp)
{
   g_gui_bitmap = bmp;
}

extern "C" BITMAP *gui_get_screen(void)
{
   return g_gui_bitmap ? g_gui_bitmap : screen;
}

// ---------------------------------------------------------------------------
// dialog utilities
// ---------------------------------------------------------------------------

extern "C" void position_dialog(DIALOG *dialog, int x, int y)
{
   // The top left corner of the bounding box of all objects goes to x, y.
   int left = INT_MAX, top = INT_MAX;
   for (DIALOG *d = dialog; d->proc; d++) {
      if (d->x < left) left = d->x;
      if (d->y < top) top = d->y;
   }
   if (left == INT_MAX)
      return;
   int dx = x - left, dy = y - top;
   for (DIALOG *d = dialog; d->proc; d++) {
      d->x += dx;
      d->y += dy;
   }
}

extern "C" void centre_dialog(DIALOG *dialog)
{
   int left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN;
   for (DIALOG *d = dialog; d->proc; d++) {
      if (d->x < left) left = d->x;
      if (d->y < top) top = d->y;
      if (d->x + d->w > right) right = d->x + d->w;
      if (d->y + d->h > bottom) bottom = d->y + d->h;
   }
   if (left == INT_MAX)
      return;
   position_dialog(dialog, (SCREEN_W - (right - left)) / 2, (SCREEN_H - (bottom - top)) / 2);
}

extern "C" void set_dialog_color(DIALOG *dialog, int fg, int bg)
{
   for (DIALOG *d = dialog; d->proc; d++) {
      d->fg = fg;
      d->bg = bg;
   }
}

extern "C" int find_dialog_focus(DIALOG *dialog)
{
   for (int i = 0; dialog[i].proc; i++) {
      if (dialog[i].flags & D_GOTFOCUS)
         return i;
   }
   return -1;
}

// ---------------------------------------------------------------------------
// messages
// ---------------------------------------------------------------------------

int zcport_gui::draw_object(DIALOG *d, int c)
{
   if (d->flags & D_HIDDEN)
      return D_O_K;

   BITMAP *gui_bmp = gui_get_screen();
   bool scared = false;
   if (_mouse_screen && gui_bmp && is_same_bitmap(gui_bmp, _mouse_screen)) {
      scare_mouse_area(d->x, d->y, d->w, d->h);
      scared = true;
   }
   if (gui_bmp)
      acquire_bitmap(gui_bmp);
   int r = d->proc(MSG_DRAW, d, c);
   if (gui_bmp)
      release_bitmap(gui_bmp);
   if (scared)
      unscare_mouse();
   return r;
}

extern "C" int object_message(DIALOG *dialog, int msg, int c)
{
   int r;
   if (msg == MSG_DRAW) {
      if (dialog->flags & D_HIDDEN)
         return D_O_K;
      r = draw_object(dialog, c);
   }
   else {
      r = dialog->proc(msg, dialog, c);
   }

   // "Redraw me" is remembered in the object and done by the dialog manager.
   if (r & D_REDRAWME) {
      dialog->flags |= D_DIRTY;
      r &= ~D_REDRAWME;
   }
   return r;
}

namespace
{
   inline bool is_dirty_and_visible(const DIALOG *d)
   {
      return (d->flags & (D_DIRTY | D_HIDDEN)) == D_DIRTY;
   }
}

extern "C" int dialog_message(DIALOG *dialog, int msg, int c, int *obj)
{
   int res = D_O_K;

   for (int i = 0; dialog[i].proc; i++) {
      DIALOG *d = dialog + i;

      // Hidden objects only get MSG_START, MSG_END and the user messages.
      if ((d->flags & D_HIDDEN) && msg != MSG_START && msg != MSG_END && msg < MSG_USER)
         continue;

      int r = object_message(d, msg, c);
      if (r != D_O_K) {
         res |= r;
         if (obj)
            *obj = i;
      }

      // While idling, an object that asked to be redrawn is redrawn at once.
      if (msg == MSG_IDLE && is_dirty_and_visible(d)) {
         d->flags &= ~D_DIRTY;
         object_message(d, MSG_DRAW, 0);
      }
   }

   return res;
}

extern "C" int broadcast_dialog_message(int msg, int c)
{
   if (!active_dialog)
      return D_O_K;
   int nowhere;
   return dialog_message(active_dialog, msg, c, &nowhere);
}

extern "C" int offer_focus(DIALOG *dialog, int obj, int *focus_obj, int force)
{
   if (obj == *focus_obj)
      return D_O_K;

   // Hidden and disabled objects are not even asked; nothing changes.
   if (obj >= 0 && (dialog[obj].flags & (D_HIDDEN | D_DISABLED)))
      return D_O_K;

   bool wanted = false;
   int answer = D_O_K;
   if (obj >= 0) {
      answer = object_message(dialog + obj, MSG_WANTFOCUS, 0);
      wanted = (answer & D_WANTFOCUS) != 0;
   }

   // The rest of the answer is passed on.
   int res = answer & ~D_WANTFOCUS;

   // Unless forced, the focus stays where it is when nobody takes it.
   if (!wanted && !force)
      return res;

   if (*focus_obj >= 0) {
      DIALOG *old = dialog + *focus_obj;
      int r = object_message(old, MSG_LOSTFOCUS, 0);
      // An object can refuse to lose the focus to "nobody".
      if (!wanted && (r & D_WANTFOCUS))
         return D_O_K;
      res |= r & ~D_WANTFOCUS;
      old->flags &= ~D_GOTFOCUS;
      *focus_obj = -1;
      res |= object_message(old, MSG_DRAW, 0);
   }

   if (wanted) {
      DIALOG *d = dialog + obj;
      *focus_obj = obj;
      d->flags |= D_GOTFOCUS;
      res |= object_message(d, MSG_GOTFOCUS, 0);
      res |= object_message(d, MSG_DRAW, 0);
   }

   return res;
}

// ---------------------------------------------------------------------------
// dialog players
// ---------------------------------------------------------------------------

namespace
{
   // Double click detection. After a click the dialog manager waits for the
   // button to be released and pressed again; meanwhile it only idles.
   enum ClickState
   {
      CLICK_HELD,       // clicked, button still down
      CLICK_RELEASED,   // released, a second press would be a double click
      CLICK_AGAIN,      // pressed again in time: double click
      CLICK_EXPIRED     // too slow, or the mouse moved away
   };

   // How long the button may stay down / up (measured on the reference:
   // the wait ends about 0.24 s after the click or the release).
   const uint32_t DCLICK_TIME_MS = 240;
   // How far the mouse may move from where it was clicked (reference: 6
   // pixels still count, 10 do not).
   const int DCLICK_RANGE = 8;

   struct Player
   {
      DIALOG_PLAYER pub;           // first: init_dialog() returns &pub
      int click_state;
      uint32_t click_mark;         // when click_state last changed
      Player *older;               // the dialog that was active before
      const ZCPORT_GUI_HOST *host; // runs this dialog (NULL: we do)
      void *host_state;
   };

   Player *g_newest = NULL;        // the most recently started dialog
   bool g_switch_hooked = false;
   const ZCPORT_GUI_HOST *g_host = NULL;

   inline Player *as_player(DIALOG_PLAYER *p)
   {
      return reinterpret_cast<Player *>(p);
   }

   int count_objects(const DIALOG *dialog)
   {
      int n = 0;
      while (dialog[n].proc)
         n++;
      return n;
   }

   inline bool contains(const DIALOG *d, int x, int y)
   {
      return x >= d->x && y >= d->y && x < d->x + d->w && y < d->y + d->h;
   }

   // The object under the mouse: the last visible, enabled object that
   // contains the pointer and does not answer MSG_WANTMOUSE with
   // D_DONTWANTMOUSE.
   int object_under_mouse(DIALOG *dialog)
   {
      int x = gui_mouse_x();
      int y = gui_mouse_y();
      int found = -1;
      for (int i = 0; dialog[i].proc; i++) {
         DIALOG *d = dialog + i;
         if (d->flags & (D_HIDDEN | D_DISABLED))
            continue;
         if (!contains(d, x, y))
            continue;
         if (!(object_message(d, MSG_WANTMOUSE, 0) & D_DONTWANTMOUSE))
            found = i;
      }
      return found;
   }

   // Sends a message on behalf of a player: a non-zero answer is collected
   // in res and the object becomes player->obj.
   int tell(DIALOG_PLAYER *p, int obj, int msg, int c)
   {
      int r = object_message(p->dialog + obj, msg, c);
      if (r != D_O_K) {
         p->res |= r;
         p->obj = obj;
      }
      return r;
   }

   void broadcast(DIALOG_PLAYER *p, int msg)
   {
      p->res |= dialog_message(p->dialog, msg, 0, &p->obj);
   }

   // Redraw everything after the window got the focus back.
   void on_switch_in(void)
   {
      for (Player *q = g_newest; q; q = q->older)
         q->pub.res |= D_REDRAW;
   }

   void link_player(Player *p)
   {
      p->older = g_newest;
      g_newest = p;
      active_dialog = p->pub.dialog;
      ++dialog_count;
      if (!g_switch_hooked) {
         g_switch_hooked = (set_display_switch_callback(SWITCH_IN, on_switch_in) == 0);
      }
   }

   void unlink_player(Player *p)
   {
      for (Player **q = &g_newest; *q; q = &(*q)->older) {
         if (*q == p) {
            *q = p->older;
            break;
         }
      }
      active_dialog = g_newest ? g_newest->pub.dialog : NULL;
      --dialog_count;
      if (!g_newest && g_switch_hooked) {
         remove_display_switch_callback(on_switch_in);
         g_switch_hooked = false;
      }
   }

   // Mouse button presses and releases go to the object under the mouse,
   // left, middle, right; c is the new button state.
   void button_messages(DIALOG_PLAYER *p, int buttons)
   {
      static const int bits[3] = { 1, 4, 2 };              // left, middle, right
      static const int press[3] = { MSG_LPRESS, MSG_MPRESS, MSG_RPRESS };
      static const int release[3] = { MSG_LRELEASE, MSG_MRELEASE, MSG_RRELEASE };
      int old = p->mouse_b;

      for (int i = 0; i < 3; i++) {
         if ((buttons & bits[i]) && !(old & bits[i]))
            tell(p, p->mouse_obj, press[i], buttons);
         else if (!(buttons & bits[i]) && (old & bits[i]))
            tell(p, p->mouse_obj, release[i], buttons);
      }
   }

   // Mouse input with no object under the mouse: the reference idles the
   // dialog instead (the result counts, but player->obj is not changed).
   void idle_for_nobody(DIALOG_PLAYER *p)
   {
      int nowhere;
      p->res |= dialog_message(p->dialog, MSG_IDLE, 0, &nowhere);
   }

   void start_click_wait(Player *pl)
   {
      DIALOG_PLAYER *p = &pl->pub;
      p->click_wait = TRUE;
      p->mouse_ox = gui_mouse_x();
      p->mouse_oy = gui_mouse_y();
      pl->click_state = CLICK_HELD;
      pl->click_mark = zcport_ticks_ms();
   }

   // Advances the double click detection; returns the new state.
   int advance_click_wait(Player *pl)
   {
      DIALOG_PLAYER *p = &pl->pub;
      uint32_t now = zcport_ticks_ms();

      if (std::abs(gui_mouse_x() - p->mouse_ox) > DCLICK_RANGE ||
          std::abs(gui_mouse_y() - p->mouse_oy) > DCLICK_RANGE) {
         pl->click_state = CLICK_EXPIRED;
         return pl->click_state;
      }

      // The reference samples the button in a timer; here it is sampled
      // when the dialog updates, so a release noticed only after the
      // button time ran out counts as too late.
      int buttons = gui_mouse_b();
      switch (pl->click_state) {
         case CLICK_HELD:
            if (now - pl->click_mark >= DCLICK_TIME_MS) {
               pl->click_state = CLICK_EXPIRED;
            }
            else if (!buttons) {
               pl->click_state = CLICK_RELEASED;
               pl->click_mark = now;
            }
            break;
         case CLICK_RELEASED:
            if (buttons)
               pl->click_state = CLICK_AGAIN;
            else if (now - pl->click_mark >= DCLICK_TIME_MS)
               pl->click_state = CLICK_EXPIRED;
            break;
      }
      return pl->click_state;
   }

   // Draws every active dialog except the most recent one, oldest first.
   void draw_dialogs_below_newest(Player *q)
   {
      if (!q || q == g_newest)
         return;
      draw_dialogs_below_newest(q->older);
      int nowhere;
      dialog_message(q->pub.dialog, MSG_DRAW, 0, &nowhere);
   }

   // Full redraw if one was asked for, then the objects marked D_DIRTY.
   // D_REDRAW_ALL redraws the dialogs under the most recent one (as the
   // reference does, even when an older dialog asked) and this one.
   void redraw_pending(DIALOG_PLAYER *p)
   {
      if (p->res & D_REDRAW_ALL) {
         p->res &= ~D_REDRAW_ALL;
         p->res |= D_REDRAW;
         if (g_newest)
            draw_dialogs_below_newest(g_newest->older);
      }
      if (p->res & D_REDRAW) {
         p->res &= ~D_REDRAW;
         broadcast(p, MSG_DRAW);
      }
      DIALOG *dialog = p->dialog;
      for (int i = 0; dialog[i].proc; i++) {
         if (is_dirty_and_visible(dialog + i)) {
            dialog[i].flags &= ~D_DIRTY;
            tell(p, i, MSG_DRAW, 0);
         }
      }
   }

   // Tries to give the focus to obj; true if it took it.
   bool try_focus(DIALOG_PLAYER *p, int obj)
   {
      p->res |= offer_focus(p->dialog, obj, &p->focus_obj, FALSE);
      return p->focus_obj == obj;
   }

   // Tab / Shift+Tab: the next (dir 1) or previous (dir -1) object, in
   // dialog order, that wants the focus. Without a focus: the first one.
   void focus_in_order(DIALOG_PLAYER *p, int dir)
   {
      int n = count_objects(p->dialog);
      int from = p->focus_obj;

      if (from < 0) {
         for (int i = 0; i < n; i++) {
            if (try_focus(p, i))
               return;
         }
         return;
      }

      for (int k = 1; k < n; k++) {
         int i = ((from + dir * k) % n + n) % n;
         if (try_focus(p, i))
            return;
      }
   }

   // Arrow keys: the nearest objects first (the reference measures the gap
   // between the boxes; which side they are on does not matter).
   void focus_by_distance(DIALOG_PLAYER *p, Axis axis)
   {
      int from = p->focus_obj;
      if (from < 0) {
         focus_in_order(p, 1);
         return;
      }

      int n = count_objects(p->dialog);
      int *order = (int *)std::malloc(sizeof(int) * (n > 0 ? n : 1));
      if (!order)
         return;
      int count = 0;
      for (int i = 0; i < n; i++) {
         if (i != from && !(p->dialog[i].flags & (D_HIDDEN | D_DISABLED)))
            order[count++] = i;
      }
      order_by_distance(p->dialog, from, axis, order, count);
      for (int k = 0; k < count; k++) {
         if (try_focus(p, order[k]))
            break;
      }
      std::free(order);
   }

   // Returns true when the key was used up (the update ends here).
   bool handle_key(DIALOG_PLAYER *p, int ascii, int scancode)
   {
      DIALOG *dialog = p->dialog;
      int combo = (scancode << 8) | ((ascii <= 255) ? ascii : '^');

      // 1. the object with the focus
      if (p->focus_obj >= 0) {
         int r = tell(p, p->focus_obj, MSG_CHAR, combo);
         if (r & (D_USED_CHAR | D_CLOSE))
            return true;
         r = tell(p, p->focus_obj, MSG_UCHAR, ascii);
         if (r & (D_USED_CHAR | D_CLOSE))
            return true;
      }

      // 2. keyboard shortcuts (the key field): a character matches without
      //    regard to case; a key without a character matches the field
      //    holding its readkey() value (scancode << 8).
      {
         int want = ascii ? shortcut_lower(ascii) : combo;
         for (int i = 0; dialog[i].proc; i++) {
            if (dialog[i].flags & (D_HIDDEN | D_DISABLED))
               continue;
            int k = ascii ? shortcut_lower(dialog[i].key) : dialog[i].key;
            if (k == want) {
               tell(p, i, MSG_KEY, combo);
               return true;
            }
         }
      }

      // 3. everybody (until somebody uses it)
      for (int i = 0; dialog[i].proc; i++) {
         if (dialog[i].flags & (D_HIDDEN | D_DISABLED))
            continue;
         int r = tell(p, i, MSG_XCHAR, combo);
         if (r & D_USED_CHAR)
            return true;
      }

      // 4. the default keyboard interface
      if ((ascii == '\r' || ascii == ' ') && p->focus_obj >= 0) {
         tell(p, p->focus_obj, MSG_KEY, combo);
         return true;
      }
      if (ascii == 27) {
         p->res |= D_CLOSE;
         p->obj = -1;
         return true;
      }
      switch (scancode) {
         case KEY_TAB:
            focus_in_order(p, (ascii == '\t') ? 1 : -1);
            break;
         case KEY_LEFT:
         case KEY_RIGHT:
            focus_by_distance(p, AXIS_HORIZONTAL);
            break;
         case KEY_UP:
         case KEY_DOWN:
            focus_by_distance(p, AXIS_VERTICAL);
            break;
      }
      return false;
   }
}

// ---------------------------------------------------------------------------
// UI host
// ---------------------------------------------------------------------------

extern "C" void zcport_set_gui_host(const ZCPORT_GUI_HOST *host)
{
   g_host = host;
}

extern "C" const ZCPORT_GUI_HOST *zcport_gui_host(void)
{
   return g_host;
}

extern "C" int zcport_gui_host_owns(const DIALOG *d)
{
   if (!d)
      return FALSE;
   for (Player *q = g_newest; q; q = q->older) {
      if (!q->host)
         continue;
      const DIALOG *first = q->pub.dialog;
      if (d < first)
         continue;
      const DIALOG *end = first;
      while (end->proc)
         end++;
      if (d < end)
         return TRUE;
   }
   return FALSE;
}

extern "C" DIALOG_PLAYER *init_dialog(DIALOG *dialog, int focus_obj)
{
   Player *pl = (Player *)std::calloc(1, sizeof(Player));
   if (!pl) {
      *allegro_errno = ENOMEM;
      return NULL;
   }

   // The GUI bitmap is clipped to the screen size of the graphics mode
   // (whatever bitmap it is; with no graphics mode nothing can be drawn).
   BITMAP *gui_bmp = gui_get_screen();
   if (gui_bmp)
      set_clip_rect(gui_bmp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

   DIALOG_PLAYER *p = &pl->pub;
   p->dialog = dialog;
   p->obj = -1;
   p->res = D_REDRAW;
   p->mouse_obj = -1;
   p->focus_obj = -1;
   p->joy_on = TRUE;
   p->click_wait = FALSE;
   p->mouse_oz = gui_mouse_z();
   p->mouse_b = gui_mouse_b();
   pl->click_state = CLICK_EXPIRED;

   link_player(pl);

   // a UI host may take the dialog (it sends MSG_START etc. itself)
   if (g_host) {
      void *state = g_host->start(p, focus_obj);
      if (state) {
         pl->host = g_host;
         pl->host_state = state;
         return p;
      }
   }

   broadcast(p, MSG_START);

   for (DIALOG *d = dialog; d->proc; d++)
      d->flags &= ~D_GOTFOCUS;

   p->mouse_obj = object_under_mouse(dialog);
   if (p->mouse_obj >= 0)
      dialog[p->mouse_obj].flags |= D_GOTMOUSE;

   // The focus goes to focus_obj if it wants it (even when hidden), or else
   // to the object under the mouse. No MSG_GOTFOCUS: the first redraw shows it.
   int want = (focus_obj >= 0) ? focus_obj : p->mouse_obj;
   if (want >= 0 && (object_message(dialog + want, MSG_WANTFOCUS, 0) & D_WANTFOCUS)) {
      dialog[want].flags |= D_GOTFOCUS;
      p->focus_obj = want;
   }

   return p;
}

// One step of the dialog. Order (each step can be seen by the procedures):
//   - a close requested earlier ends the dialog;
//   - mouse button changes: the object under the mouse is offered the focus
//     and gets MSG_xPRESS / MSG_xRELEASE;
//   - while a double click is pending, only idle (returns TRUE);
//   - a D_WANTFOCUS answer from earlier moves the focus to that object;
//   - the object under the mouse is tracked (MSG_GOTMOUSE/LOSTMOUSE), the
//     focus follows it if gui_mouse_focus is set;
//   - mouse wheel, mouse click;
//   - joystick or keyboard input;
//   - redraw (D_REDRAW), objects marked D_DIRTY, MSG_IDLE.
extern "C" int update_dialog(DIALOG_PLAYER *player)
{
   Player *pl = as_player(player);
   DIALOG_PLAYER *p = player;
   DIALOG *dialog = p->dialog;

   if (pl->host)
      return pl->host->update(pl->host_state, p);

   if (p->res & D_CLOSE)
      return FALSE;

   // mouse buttons (with nobody under the mouse the change is not taken
   // note of, so it counts again in the next update)
   int buttons = gui_mouse_b();
   if (buttons != p->mouse_b) {
      p->res |= offer_focus(dialog, p->mouse_obj, &p->focus_obj, FALSE);
      if (p->mouse_obj >= 0) {
         button_messages(p, buttons);
         p->mouse_b = buttons;
      }
      else {
         idle_for_nobody(p);
      }
   }

   // waiting for a double click?
   if (p->click_wait) {
      int state = advance_click_wait(pl);
      if (state == CLICK_HELD || state == CLICK_RELEASED) {
         idle_for_nobody(p);       // player->obj is left alone here too
         redraw_pending(p);
         return TRUE;
      }
      p->click_wait = FALSE;
      if (state == CLICK_AGAIN && p->mouse_obj >= 0 &&
          contains(dialog + p->mouse_obj, gui_mouse_x(), gui_mouse_y()))
         tell(p, p->mouse_obj, MSG_DCLICK, 0);
      goto done;
   }

   p->res &= ~D_USED_CHAR;

   // somebody asked for the focus
   if (p->res & D_WANTFOCUS) {
      p->res &= ~D_WANTFOCUS;
      p->res |= offer_focus(dialog, p->obj, &p->focus_obj, FALSE);
   }

   // the object under the mouse
   {
      int now_under = object_under_mouse(dialog);
      if (now_under != p->mouse_obj) {
         if (p->mouse_obj >= 0) {
            dialog[p->mouse_obj].flags &= ~D_GOTMOUSE;
            tell(p, p->mouse_obj, MSG_LOSTMOUSE, 0);
         }
         if (now_under >= 0) {
            dialog[now_under].flags |= D_GOTMOUSE;
            tell(p, now_under, MSG_GOTMOUSE, 0);
         }
         p->mouse_obj = now_under;
         if (gui_mouse_focus && p->mouse_obj != p->focus_obj)
            p->res |= offer_focus(dialog, p->mouse_obj, &p->focus_obj, TRUE);
      }
   }

   // mouse click
   {
      int b = gui_mouse_b();
      if (b) {
         if (p->mouse_obj >= 0) {
            p->res |= offer_focus(dialog, p->mouse_obj, &p->focus_obj, FALSE);
            tell(p, p->mouse_obj, MSG_CLICK, b);
            if (p->res == D_O_K)
               start_click_wait(pl);
         }
         else {
            idle_for_nobody(p);
         }
      }
   }

   // mouse wheel
   {
      int z = gui_mouse_z();
      if (z != p->mouse_oz) {
         int delta = z - p->mouse_oz;
         p->mouse_oz = z;
         if (p->mouse_obj >= 0) {
            p->res |= offer_focus(dialog, p->mouse_obj, &p->focus_obj, FALSE);
            tell(p, p->mouse_obj, MSG_WHEEL, delta);
         }
         else {
            idle_for_nobody(p);
         }
      }
   }

   // joystick and keyboard
   {
      JoyRequest joy_req = read_joystick();
      int ascii = 0, scancode = 0;
      bool have_key = false;

      if (p->joy_on) {
         if (joy_req.released)
            p->joy_on = FALSE;
      }
      else if (joy_req.key) {
         ascii = joy_req.key & 0xFF;
         scancode = (joy_req.key >> 8) & 0xFF;
         have_key = true;
         p->joy_on = TRUE;
      }

      if (!have_key && keypressed()) {
         ascii = ureadkey(&scancode);
         have_key = true;
      }

      if (have_key && handle_key(p, ascii, scancode))
         goto done;
   }

   redraw_pending(p);
   broadcast(p, MSG_IDLE);

done:
   if (p->res & D_CLOSE) {
      p->res &= ~D_CLOSE;
      return FALSE;
   }
   return 1;
}

extern "C" int shutdown_dialog(DIALOG_PLAYER *player)
{
   Player *pl = as_player(player);
   DIALOG_PLAYER *p = player;

   if (pl->host) {
      pl->host->end(pl->host_state, p);
   }
   else {
      broadcast(p, MSG_END);

      if (p->mouse_obj >= 0)
         p->dialog[p->mouse_obj].flags &= ~D_GOTMOUSE;
   }

   unlink_player(pl);

   int obj = p->obj;
   std::free(pl);
   return obj;
}

extern "C" int do_dialog(DIALOG *dialog, int focus_obj)
{
   BITMAP *mouse_bmp = _mouse_screen;
   BITMAP *gui_bmp = gui_get_screen();
   int mode_count = _gfx_mode_set_count;

   // The pointer is shown on the GUI bitmap while the dialog runs.
   bool moved_mouse = false;
   if (!is_same_bitmap(_mouse_screen, gui_bmp) && !(gfx_capabilities & GFX_HW_CURSOR)) {
      show_mouse(gui_bmp);
      moved_mouse = true;
   }

   DIALOG_PLAYER *player = init_dialog(dialog, focus_obj);
   if (!player)
      return -1;

   while (update_dialog(player)) {
      // update_dialog() pumps the platform through the input hooks.
   }

   if (moved_mouse && _gfx_mode_set_count == mode_count && !(gfx_capabilities & GFX_HW_CURSOR))
      show_mouse(mouse_bmp);

   return shutdown_dialog(player);
}

// ---------------------------------------------------------------------------
// keyboard shortcut comparison
// ---------------------------------------------------------------------------

int zcport_gui::shortcut_lower(int c)
{
   if (c >= 'A' && c <= 'Z')
      return c + ('a' - 'A');
   if (c >= 0xC0 && c <= 0xDE && c != 0xD7)
      return c + 0x20;
   return c;
}

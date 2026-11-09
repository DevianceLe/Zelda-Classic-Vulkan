// zcport foundation: keyboard state, the keyboard buffer, software key
// repeat and the platform feed API (zcport/input.h).
//
// Written from scratch for zcport (no Allegro code). Behaviour is checked
// against the reference build by tests/conformance/test_foundation_keyboard.cpp.

#include "foundation_internal.h"

#include <cstdlib>
#include <cstring>

namespace zcport_foundation
{
   // Never destroyed: allegro_exit() (an atexit function) still takes it
   // in remove_keyboard()/remove_mouse(), after function-local statics
   // created later than its registration are gone (bionic aborts on a
   // destroyed mutex).
   std::recursive_mutex &input_lock()
   {
      static std::recursive_mutex *m = new std::recursive_mutex;
      return *m;
   }
}

using zcport_foundation::input_lock;

// ---------------------------------------------------------------------------
// public state
// ---------------------------------------------------------------------------

volatile char key[KEY_MAX];
volatile int key_shifts = 0;
int three_finger_flag = TRUE;
int key_led_flag = TRUE;

int (*keyboard_callback)(int key) = NULL;
int (*keyboard_ucallback)(int key, int *scancode) = NULL;
void (*keyboard_lowlevel_callback)(int scancode) = NULL;

static KEYBOARD_DRIVER keyboard_zcport =
{
   KEYBOARD_ZCPORT,
   "zcport keyboard",
   "zcport keyboard",
   "zcport keyboard",
   FALSE,              /* autorepeat: zcport repeats in software */
   NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

KEYBOARD_DRIVER *keyboard_driver = NULL;

// ---------------------------------------------------------------------------
// the buffer
// ---------------------------------------------------------------------------

#define ZCPORT_KEY_BUFFER_SIZE 64

namespace
{
   struct KeyEntry
   {
      int ch;
      int scancode;
   };

   KeyEntry g_buffer[ZCPORT_KEY_BUFFER_SIZE];
   int g_head = 0;           // next entry to read
   int g_tail = 0;           // next free slot

   int g_lock_flags = 0;

   int g_key_delay = 250;
   int g_key_repeat = 33;
   int g_repeat_scan = -1;
   int g_repeat_ch = 0;
   uint32_t g_repeat_next = 0;

   int (*g_keypressed_hook)(void) = NULL;
   int (*g_readkey_hook)(void) = NULL;

   bool buffer_empty()
   {
      return g_head == g_tail;
   }

   // Applies the user callbacks and stores the key.
   void add_key(int ch, int scancode)
   {
      std::lock_guard<std::recursive_mutex> guard(input_lock());

      if (keyboard_ucallback) {
         ch = keyboard_ucallback(ch, &scancode);
         if (!ch && !scancode)
            return;
      }
      else if (keyboard_callback) {
         int c = keyboard_callback(((ch <= 0xFF) ? ch : '^') | (scancode << 8));
         if (!c)
            return;
         ch = c & 0xFF;
         scancode = (c >> 8) & 0xFF;
      }

      int next = (g_tail + 1) % ZCPORT_KEY_BUFFER_SIZE;
      if (next == g_head)
         return;                       // full: the key is lost
      g_buffer[g_tail].ch = ch;
      g_buffer[g_tail].scancode = scancode;
      g_tail = next;
   }

   bool pop_key(int *ch, int *scancode)
   {
      std::lock_guard<std::recursive_mutex> guard(input_lock());
      if (buffer_empty())
         return false;
      *ch = g_buffer[g_head].ch;
      *scancode = g_buffer[g_head].scancode;
      g_head = (g_head + 1) % ZCPORT_KEY_BUFFER_SIZE;
      return true;
   }

   void update_shifts()
   {
      int s = g_lock_flags & (KB_SCROLOCK_FLAG | KB_NUMLOCK_FLAG | KB_CAPSLOCK_FLAG);
      if (key[KEY_LSHIFT] || key[KEY_RSHIFT])
         s |= KB_SHIFT_FLAG;
      if (key[KEY_LCONTROL] || key[KEY_RCONTROL])
         s |= KB_CTRL_FLAG;
      if (key[KEY_ALT] || key[KEY_ALTGR])
         s |= KB_ALT_FLAG;
      if (key[KEY_LWIN])
         s |= KB_LWIN_FLAG;
      if (key[KEY_RWIN])
         s |= KB_RWIN_FLAG;
      if (key[KEY_MENU])
         s |= KB_MENU_FLAG;
      if (key[KEY_COMMAND])
         s |= KB_COMMAND_FLAG;
      key_shifts = s;
   }

   void clear_state()
   {
      std::lock_guard<std::recursive_mutex> guard(input_lock());
      for (int i = 0; i < KEY_MAX; i++)
         key[i] = 0;
      g_head = g_tail = 0;
      g_repeat_scan = -1;
      update_shifts();
   }
}

// ---------------------------------------------------------------------------
// software repeat
// ---------------------------------------------------------------------------

void zcport_foundation::keyboard_update_repeat()
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   if (g_repeat_scan < 0 || g_key_delay <= 0 || !keyboard_driver)
      return;
   uint32_t now = zcport_ticks_ms();
   if ((int32_t)(now - g_repeat_next) < 0)
      return;
   add_key(g_repeat_ch, g_repeat_scan);
   uint32_t step = (uint32_t)(g_key_repeat > 0 ? g_key_repeat : 1);
   g_repeat_next += step;
   // Do not queue a burst after a long stall (one repeat per pump).
   if ((int32_t)(now - g_repeat_next) >= 0)
      g_repeat_next = now + step;
   if (g_key_repeat <= 0)
      g_repeat_scan = -1;
}

// ---------------------------------------------------------------------------
// installation
// ---------------------------------------------------------------------------

extern "C" int install_keyboard(void)
{
   if (keyboard_driver)
      return 0;
   // Like Allegro with SYSTEM_NONE: there is no keyboard driver.
   if (!system_driver || system_driver == &system_none)
      return -1;

   clear_state();
   keyboard_driver = &keyboard_zcport;
   _add_exit_func(remove_keyboard, "remove_keyboard");
   return 0;
}

extern "C" void remove_keyboard(void)
{
   if (!keyboard_driver)
      return;
   clear_state();
   keyboard_driver = NULL;
   _remove_exit_func(remove_keyboard);
}

extern "C" void install_keyboard_hooks(int (*keypressed_fn)(void), int (*readkey_fn)(void))
{
   g_keypressed_hook = keypressed_fn;
   g_readkey_hook = readkey_fn;
}

extern "C" int poll_keyboard(void)
{
   if (!keyboard_driver)
      return -1;
   zcport_pump_events();
   return 0;
}

extern "C" int keyboard_needs_poll(void)
{
   // Events only arrive when the platform pumps them.
   return TRUE;
}

// ---------------------------------------------------------------------------
// reading
// ---------------------------------------------------------------------------

extern "C" int keypressed(void)
{
   if (g_keypressed_hook)
      return g_keypressed_hook();
   if (!keyboard_driver)
      return FALSE;
   zcport_pump_events();
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   return buffer_empty() ? FALSE : TRUE;
}

// The next key readkey() would return, left in the buffer; 0 when there is none.
extern "C" int peekkey(void)
{
   if (!keyboard_driver)
      return 0;
   zcport_pump_events();
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   if (buffer_empty())
      return 0;
   int ch = g_buffer[g_head].ch;
   return ((ch <= 0xFF) ? ch : '^') | (g_buffer[g_head].scancode << 8);
}

extern "C" int ureadkey(int *scancode)
{
   if (g_readkey_hook) {
      int c = g_readkey_hook();
      if (scancode)
         *scancode = (c >> 8) & 0xFF;
      return c & 0xFF;
   }

   if (!keyboard_driver) {
      if (scancode)
         *scancode = 0;
      return 0;
   }

   int ch = 0, sc = 0;
   for (;;) {
      if (pop_key(&ch, &sc))
         break;
      zcport_pump_events();
      if (pop_key(&ch, &sc))
         break;
      zcport_sleep_ms(1);
   }

   if (scancode)
      *scancode = sc;
   return ch;
}

extern "C" int readkey(void)
{
   if (g_readkey_hook)
      return g_readkey_hook();
   int scancode = 0;
   int c = ureadkey(&scancode);
   return ((c <= 0xFF) ? c : '^') | (scancode << 8);
}

extern "C" void simulate_ukeypress(int keycode, int scancode)
{
   add_key(keycode, scancode);
}

extern "C" void simulate_keypress(int keycode)
{
   simulate_ukeypress(keycode & 0xFF, (keycode >> 8) & 0xFF);
}

extern "C" void clear_keybuf(void)
{
   if (keyboard_driver)
      zcport_pump_events();
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   g_head = g_tail = 0;
}

extern "C" void set_leds(int leds)
{
   (void)leds;
}

extern "C" void set_keyboard_rate(int delay, int repeat)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   g_key_delay = delay;
   g_key_repeat = repeat;
   if (delay <= 0)
      g_repeat_scan = -1;
}

// ---------------------------------------------------------------------------
// scancode tables (US layout)
// ---------------------------------------------------------------------------

static int us_ascii(int scancode)
{
   if (scancode >= KEY_A && scancode <= KEY_Z)
      return 'a' + (scancode - KEY_A);
   if (scancode >= KEY_0 && scancode <= KEY_9)
      return '0' + (scancode - KEY_0);
   if (scancode >= KEY_0_PAD && scancode <= KEY_9_PAD)
      return '0' + (scancode - KEY_0_PAD);
   switch (scancode) {
      case KEY_ESC:        return 27;
      case KEY_TILDE:      return '`';
      case KEY_MINUS:      return '-';
      case KEY_EQUALS:     return '=';
      case KEY_BACKSPACE:  return 8;
      case KEY_TAB:        return 9;
      case KEY_OPENBRACE:  return '[';
      case KEY_CLOSEBRACE: return ']';
      case KEY_ENTER:      return 13;
      case KEY_COLON:      return ';';
      case KEY_QUOTE:      return '\'';
      case KEY_BACKSLASH:  return '\\';
      case KEY_BACKSLASH2: return '\\';
      case KEY_COMMA:      return ',';
      case KEY_STOP:       return '.';
      case KEY_SLASH:      return '/';
      case KEY_SPACE:      return ' ';
      case KEY_SLASH_PAD:  return '/';
      case KEY_ASTERISK:   return '*';
      case KEY_MINUS_PAD:  return '-';
      case KEY_PLUS_PAD:   return '+';
      case KEY_DEL_PAD:    return '.';
      case KEY_ENTER_PAD:  return 13;
      case KEY_EQUALS_PAD: return '=';
      case KEY_BACKQUOTE:  return '`';
      case KEY_SEMICOLON:  return ';';
   }
   return 0;
}

extern "C" int scancode_to_ascii(int scancode)
{
   return us_ascii(scancode);
}

static const char *const g_key_names[KEY_MAX] =
{
   "(none)",
   "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
   "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
   "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
   "0 PAD", "1 PAD", "2 PAD", "3 PAD", "4 PAD",
   "5 PAD", "6 PAD", "7 PAD", "8 PAD", "9 PAD",
   "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
   "ESC", "TILDE", "MINUS", "EQUALS", "BACKSPACE", "TAB",
   "OPENBRACE", "CLOSEBRACE", "ENTER", "COLON", "QUOTE",
   "BACKSLASH", "BACKSLASH2", "COMMA", "STOP", "SLASH", "SPACE",
   "INSERT", "DEL", "HOME", "END", "PGUP", "PGDN",
   "LEFT", "RIGHT", "UP", "DOWN",
   "SLASH_PAD", "ASTERISK", "MINUS_PAD", "PLUS_PAD", "DEL_PAD", "ENTER_PAD",
   "PRTSCR", "PAUSE",
   "ABNT_C1", "YEN", "KANA", "CONVERT", "NOCONVERT", "AT", "CIRCUMFLEX",
   "COLON2", "KANJI",
   "EQUALS_PAD", "BACKQUOTE", "SEMICOLON", "COMMAND",
   "UNKNOWN1", "UNKNOWN2", "UNKNOWN3", "UNKNOWN4",
   "UNKNOWN5", "UNKNOWN6", "UNKNOWN7", "UNKNOWN8",
   "LSHIFT", "RSHIFT", "LCONTROL", "RCONTROL", "ALT", "ALTGR",
   "LWIN", "RWIN", "MENU", "SCRLOCK", "NUMLOCK", "CAPSLOCK"
};

// The same table under the name the engine's replay code reads it by.
extern "C" { extern const char *const *const _keyboard_common_names; }
const char *const *const _keyboard_common_names = g_key_names;

extern "C" const char *scancode_to_name(int scancode)
{
   if (scancode < 0 || scancode >= KEY_MAX)
      return "(none)";
   return g_key_names[scancode];
}

// ---------------------------------------------------------------------------
// platform feed (zcport/input.h)
// ---------------------------------------------------------------------------

extern "C" void zcport_key_down(int scancode, int ch)
{
   if (scancode <= 0 || scancode >= KEY_MAX)
      return;

   std::lock_guard<std::recursive_mutex> guard(input_lock());
   if (key[scancode])
      return;

   key[scancode] = (char)-1;
   update_shifts();

   if (keyboard_lowlevel_callback)
      keyboard_lowlevel_callback(scancode);

   if (three_finger_flag &&
       (key_shifts & KB_CTRL_FLAG) && (key_shifts & KB_ALT_FLAG) &&
       (scancode == KEY_END || scancode == KEY_DEL)) {
      // the emergency exit sequence
      exit(EXIT_FAILURE);
   }

   if (ch >= 0) {
      add_key(ch, scancode);
      if (g_key_delay > 0) {
         g_repeat_scan = scancode;
         g_repeat_ch = ch;
         g_repeat_next = zcport_ticks_ms() + (uint32_t)g_key_delay;
      }
   }
}

extern "C" void zcport_key_up(int scancode)
{
   if (scancode <= 0 || scancode >= KEY_MAX)
      return;

   std::lock_guard<std::recursive_mutex> guard(input_lock());
   if (!key[scancode])
      return;

   key[scancode] = 0;
   update_shifts();

   if (keyboard_lowlevel_callback)
      keyboard_lowlevel_callback(scancode | 0x80);

   if (scancode == g_repeat_scan)
      g_repeat_scan = -1;
}

extern "C" void zcport_key_release_all(void)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   for (int i = 1; i < KEY_MAX; i++) {
      if (key[i])
         zcport_key_up(i);
   }
   g_repeat_scan = -1;
}

extern "C" void zcport_key_set_locks(int lock_flags)
{
   std::lock_guard<std::recursive_mutex> guard(input_lock());
   g_lock_flags = lock_flags & (KB_SCROLOCK_FLAG | KB_NUMLOCK_FLAG | KB_CAPSLOCK_FLAG);
   update_shifts();
}

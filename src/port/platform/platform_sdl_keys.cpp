// zcport SDL platform: SDL keyboard events -> Allegro scancodes and the
// character Allegro puts in the keyboard buffer (see zcport/input.h,
// zcport_key_down()).
//
// Scancodes are physical key positions on both sides (SDL scancodes are USB
// HID positions, the reference's DirectInput codes are PC scancodes), so a
// key keeps its KEY_* whatever the keyboard layout. Characters come from
// the layout (SDL_GetKeyFromScancode with Shift / Caps Lock / AltGr).
//
// Written from scratch for zcport (no Allegro code).

#include <allegro.h>
#include <SDL3/SDL.h>

#include "platform_sdl_internal.h"

namespace zcport_sdl
{

int map_scancode(SDL_Scancode sc)
{
   if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
      return KEY_A + (sc - SDL_SCANCODE_A);
   if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
      return KEY_1 + (sc - SDL_SCANCODE_1);
   if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
      return KEY_F1 + (sc - SDL_SCANCODE_F1);
   if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9)
      return KEY_1_PAD + (sc - SDL_SCANCODE_KP_1);

   switch (sc) {
      case SDL_SCANCODE_0:            return KEY_0;
      case SDL_SCANCODE_KP_0:         return KEY_0_PAD;
      case SDL_SCANCODE_ESCAPE:       return KEY_ESC;
      case SDL_SCANCODE_GRAVE:        return KEY_TILDE;
      case SDL_SCANCODE_MINUS:        return KEY_MINUS;
      case SDL_SCANCODE_EQUALS:       return KEY_EQUALS;
      case SDL_SCANCODE_BACKSPACE:    return KEY_BACKSPACE;
      case SDL_SCANCODE_TAB:          return KEY_TAB;
      case SDL_SCANCODE_LEFTBRACKET:  return KEY_OPENBRACE;
      case SDL_SCANCODE_RIGHTBRACKET: return KEY_CLOSEBRACE;
      case SDL_SCANCODE_RETURN:       return KEY_ENTER;
      case SDL_SCANCODE_SEMICOLON:    return KEY_COLON;
      case SDL_SCANCODE_APOSTROPHE:   return KEY_QUOTE;
      case SDL_SCANCODE_BACKSLASH:    return KEY_BACKSLASH;
      case SDL_SCANCODE_NONUSHASH:    return KEY_BACKSLASH;
      case SDL_SCANCODE_NONUSBACKSLASH: return KEY_BACKSLASH2;
      case SDL_SCANCODE_COMMA:        return KEY_COMMA;
      case SDL_SCANCODE_PERIOD:       return KEY_STOP;
      case SDL_SCANCODE_SLASH:        return KEY_SLASH;
      case SDL_SCANCODE_SPACE:        return KEY_SPACE;
      case SDL_SCANCODE_INSERT:       return KEY_INSERT;
      case SDL_SCANCODE_DELETE:       return KEY_DEL;
      case SDL_SCANCODE_HOME:         return KEY_HOME;
      case SDL_SCANCODE_END:          return KEY_END;
      case SDL_SCANCODE_PAGEUP:       return KEY_PGUP;
      case SDL_SCANCODE_PAGEDOWN:     return KEY_PGDN;
      case SDL_SCANCODE_LEFT:         return KEY_LEFT;
      case SDL_SCANCODE_RIGHT:        return KEY_RIGHT;
      case SDL_SCANCODE_UP:           return KEY_UP;
      case SDL_SCANCODE_DOWN:         return KEY_DOWN;
      case SDL_SCANCODE_KP_DIVIDE:    return KEY_SLASH_PAD;
      case SDL_SCANCODE_KP_MULTIPLY:  return KEY_ASTERISK;
      case SDL_SCANCODE_KP_MINUS:     return KEY_MINUS_PAD;
      case SDL_SCANCODE_KP_PLUS:      return KEY_PLUS_PAD;
      case SDL_SCANCODE_KP_PERIOD:    return KEY_DEL_PAD;
      case SDL_SCANCODE_KP_COMMA:     return KEY_DEL_PAD;
      case SDL_SCANCODE_KP_ENTER:     return KEY_ENTER_PAD;
      case SDL_SCANCODE_KP_EQUALS:    return KEY_EQUALS_PAD;
      case SDL_SCANCODE_PRINTSCREEN:  return KEY_PRTSCR;
      case SDL_SCANCODE_PAUSE:        return KEY_PAUSE;
      case SDL_SCANCODE_INTERNATIONAL1: return KEY_ABNT_C1;
      case SDL_SCANCODE_INTERNATIONAL2: return KEY_KANA;
      case SDL_SCANCODE_INTERNATIONAL3: return KEY_YEN;
      case SDL_SCANCODE_INTERNATIONAL4: return KEY_CONVERT;
      case SDL_SCANCODE_INTERNATIONAL5: return KEY_NOCONVERT;
      case SDL_SCANCODE_LANG1:        return KEY_KANA;
      case SDL_SCANCODE_LANG2:        return KEY_KANJI;
      case SDL_SCANCODE_LSHIFT:       return KEY_LSHIFT;
      case SDL_SCANCODE_RSHIFT:       return KEY_RSHIFT;
      case SDL_SCANCODE_LCTRL:        return KEY_LCONTROL;
      case SDL_SCANCODE_RCTRL:        return KEY_RCONTROL;
      case SDL_SCANCODE_LALT:         return KEY_ALT;
      case SDL_SCANCODE_RALT:         return KEY_ALTGR;
      case SDL_SCANCODE_LGUI:         return KEY_LWIN;
      case SDL_SCANCODE_RGUI:         return KEY_RWIN;
      case SDL_SCANCODE_APPLICATION:  return KEY_MENU;
      case SDL_SCANCODE_MENU:         return KEY_MENU;
      case SDL_SCANCODE_SCROLLLOCK:   return KEY_SCRLOCK;
      case SDL_SCANCODE_NUMLOCKCLEAR: return KEY_NUMLOCK;
      case SDL_SCANCODE_CAPSLOCK:     return KEY_CAPSLOCK;
      default:                        return 0;
   }
}

int key_char(SDL_Scancode sc, SDL_Keymod mod, int allegro_scancode)
{
   int k = allegro_scancode;
   if (k >= KEY_MODIFIERS && k < KEY_MAX)
      return -1;      // modifiers and lock keys: no buffer entry

   const bool numlock = (mod & SDL_KMOD_NUM) != 0;
   const bool altgr = (mod & SDL_KMOD_RALT) != 0;
   const bool ctrl = (mod & SDL_KMOD_CTRL) != 0 && !altgr;
   const bool alt = (mod & SDL_KMOD_LALT) != 0 && !altgr;

   // Keys that always give the same character.
   switch (k) {
      case KEY_ENTER:
      case KEY_ENTER_PAD:
         return ctrl ? 10 : 13;
      case KEY_BACKSPACE:
         return ctrl ? 127 : 8;
      case KEY_TAB:
         return (ctrl || alt) ? 0 : 9;
      case KEY_ESC:
         return 27;
      default:
         break;
   }

   // Keypad: digits and '.' with Num Lock, navigation keys without.
   if (k >= KEY_0_PAD && k <= KEY_9_PAD)
      return (numlock && !alt && !ctrl) ? ('0' + (k - KEY_0_PAD)) : 0;
   if (k == KEY_DEL_PAD)
      return (numlock && !alt && !ctrl) ? '.' : 0;
   if (k == KEY_SLASH_PAD)    return (alt || ctrl) ? 0 : '/';
   if (k == KEY_ASTERISK)     return (alt || ctrl) ? 0 : '*';
   if (k == KEY_MINUS_PAD)    return (alt || ctrl) ? 0 : '-';
   if (k == KEY_PLUS_PAD)     return (alt || ctrl) ? 0 : '+';

   if (alt)
      return 0;

   // What the layout gives for this key with Shift / Caps Lock / AltGr.
   SDL_Keymod layout_mod = (SDL_Keymod)(mod & (SDL_KMOD_SHIFT | SDL_KMOD_CAPS | SDL_KMOD_MODE));
   if (altgr)
      layout_mod = (SDL_Keymod)(layout_mod | SDL_KMOD_MODE);
   SDL_Keycode code = SDL_GetKeyFromScancode(sc, layout_mod, false);

   if (ctrl) {
      // Control characters: Ctrl+letter = 1..26, a few punctuation keys.
      SDL_Keycode base = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
      if (base >= 'a' && base <= 'z')
         return (int)(base - 'a') + 1;
      switch (base) {
         case '[':  return 27;
         case '\\': return 28;
         case ']':  return 29;
         case ' ':  return 32;
         default:   return 0;
      }
   }

   if (code == SDLK_UNKNOWN || (code & SDLK_SCANCODE_MASK))
      return 0;       // not a character key
   if (code < 32 || code == 127)
      return 0;
   return (int)code;
}

int lock_flags(SDL_Keymod mod)
{
   int f = 0;
   if (mod & SDL_KMOD_SCROLL) f |= KB_SCROLOCK_FLAG;
   if (mod & SDL_KMOD_NUM)    f |= KB_NUMLOCK_FLAG;
   if (mod & SDL_KMOD_CAPS)   f |= KB_CAPSLOCK_FLAG;
   return f;
}

} // namespace zcport_sdl

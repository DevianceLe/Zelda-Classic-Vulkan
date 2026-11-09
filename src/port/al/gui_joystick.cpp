// zcport GUI: the dialog manager's joystick input.
//
// Written from scratch for zcport (no Allegro code). Measured on the
// reference build (tests/conformance/test_gui_engine_joystick.cpp): the
// dialog manager reads only the first joystick's first stick (the digital
// d1/d2 flags of axis 0 and 1) and its buttons 0 and 1, and turns them into
// the key presses left/right/up/down and space. After one such "key" the
// joystick is ignored until the stick is centred and the buttons released
// (DIALOG_PLAYER::joy_on).
//
// num_joysticks, joy[] and poll_joystick() belong to the input layer's
// joystick module. The dialog manager reaches them through weak references,
// so zcport also links without that module (for instance in the
// conformance tests); there is then simply no joystick.

#include "gui_internal.h"

using namespace zcport_gui;

extern "C"
{
   int zcport_gui_fallback_num_joysticks = 0;
   JOYSTICK_INFO zcport_gui_fallback_joy[MAX_JOYSTICKS];
   int zcport_gui_fallback_poll_joystick(void) { return -1; }
}

#if defined(_MSC_VER)
   // The linker uses the fallback only if nothing defines the real symbol.
   #if defined(_M_IX86)
      #pragma comment(linker, "/alternatename:_num_joysticks=_zcport_gui_fallback_num_joysticks")
      #pragma comment(linker, "/alternatename:_joy=_zcport_gui_fallback_joy")
      #pragma comment(linker, "/alternatename:_poll_joystick=_zcport_gui_fallback_poll_joystick")
   #else
      #pragma comment(linker, "/alternatename:num_joysticks=zcport_gui_fallback_num_joysticks")
      #pragma comment(linker, "/alternatename:joy=zcport_gui_fallback_joy")
      #pragma comment(linker, "/alternatename:poll_joystick=zcport_gui_fallback_poll_joystick")
   #endif
   static inline bool joystick_module_linked() { return true; }
#elif defined(__GNUC__) || defined(__clang__)
   #pragma weak num_joysticks
   #pragma weak joy
   #pragma weak poll_joystick
   static inline bool joystick_module_linked()
   {
      return &num_joysticks != 0 && &joy[0] != 0 && &poll_joystick != 0;
   }
#else
   #error "zcport gui_joystick.cpp: no weak symbol support for this compiler"
#endif

JoyRequest zcport_gui::read_joystick()
{
   JoyRequest r;
   r.released = true;
   r.key = 0;

   if (!joystick_module_linked())
      return r;

   // The reference reads joy[0] whatever num_joysticks says (it is all
   // zeroes without a joystick). Whether it polls is not visible from the
   // outside; polling here keeps the state fresh in dialogs that run
   // without the game's own input loop.
   if (num_joysticks > 0)
      poll_joystick();

   const JOYSTICK_INFO &j = joy[0];
   const JOYSTICK_AXIS_INFO &ax = j.stick[0].axis[0];
   const JOYSTICK_AXIS_INFO &ay = j.stick[0].axis[1];
   bool button = j.button[0].b || j.button[1].b;

   if (ax.d1 || ax.d2 || ay.d1 || ay.d2 || button)
      r.released = false;

   if (ax.d1)
      r.key = KEY_LEFT << 8;
   else if (ax.d2)
      r.key = KEY_RIGHT << 8;
   else if (ay.d1)
      r.key = KEY_UP << 8;
   else if (ay.d2)
      r.key = KEY_DOWN << 8;
   else if (button)
      r.key = (KEY_SPACE << 8) | ' ';

   return r;
}

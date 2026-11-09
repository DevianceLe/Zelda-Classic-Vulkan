// zcport platform module: joysticks.
//
// The platform reports each device as raw axes (-32768..32767), buttons
// and hats (zcport/platform.h ZCPORT_JOY_STATE). poll_joystick() turns that
// into Allegro's JOYSTICK_INFO layout:
//   - stick 0, 1, ...: consecutive pairs of axes (0+1, 2+3, ...), a last odd
//     axis alone; flags JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;
//     pos = axis * 128 / 32768 (-128..127), d1 = pos < -64, d2 = pos > 64.
//     (For gamepads the platform orders the axes left stick, right stick,
//     triggers, so stick 1 is the right stick, which is what the game's
//     default js_stick_2_* settings expect.)
//   - then one stick per hat: 2 digital axes (x: left/right, y: up/down),
//     pos -128 / 0 / 128, flags JOYFLAG_DIGITAL | JOYFLAG_SIGNED.
//   - at most MAX_JOYSTICK_STICKS sticks, MAX_JOYSTICK_BUTTONS buttons.
// No calibration is needed (like the reference's DirectInput driver).
//
// Written from scratch for zcport (no Allegro code).

#include "platform_internal.h"

#include <cstdio>
#include <cstring>

JOYSTICK_INFO joy[MAX_JOYSTICKS];
int num_joysticks = 0;

JOYSTICK_DRIVER joystick_none =
{
   JOY_TYPE_NONE, "None", "No joystick", "None",
   NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static JOYSTICK_DRIVER joystick_zcport =
{
   JOY_TYPE_ZCPORT, "zcport joystick", "zcport joystick", "zcport joystick",
   NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

JOYSTICK_DRIVER *joystick_driver = NULL;

namespace
{
   bool g_installed = false;

   const char *const kAxisNames[2] = { "X", "Y" };
   char g_stick_names[MAX_JOYSTICKS][MAX_JOYSTICK_STICKS][16];
   char g_button_names[MAX_JOYSTICK_BUTTONS][8];
   bool g_names_ready = false;

   void init_names()
   {
      if (g_names_ready)
         return;
      for (int b = 0; b < MAX_JOYSTICK_BUTTONS; b++)
         snprintf(g_button_names[b], sizeof(g_button_names[b]), "B%d", b + 1);
      g_names_ready = true;
   }

   int scale_axis(int v)
   {
      if (v < -32768) v = -32768;
      if (v > 32767) v = 32767;
      return (v * 128) / 32768;   // -128 .. 127
   }

   void set_axis(JOYSTICK_AXIS_INFO &a, int pos, const char *name)
   {
      a.pos = pos;
      a.d1 = (pos < -64) ? TRUE : FALSE;
      a.d2 = (pos > 64) ? TRUE : FALSE;
      a.name = name;
   }

   void fill_info(int n, const ZCPORT_JOY_STATE &st)
   {
      JOYSTICK_INFO &j = joy[n];
      j.flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE;

      int nb = st.num_buttons;
      if (nb > MAX_JOYSTICK_BUTTONS) nb = MAX_JOYSTICK_BUTTONS;
      if (nb > ZCPORT_JOY_MAX_BUTTONS) nb = ZCPORT_JOY_MAX_BUTTONS;
      if (nb < 0) nb = 0;
      j.num_buttons = nb;
      for (int b = 0; b < MAX_JOYSTICK_BUTTONS; b++) {
         j.button[b].b = (b < nb && st.button[b]) ? TRUE : FALSE;
         j.button[b].name = g_button_names[b];
      }

      int s = 0;
      int na = st.num_axes;
      if (na > ZCPORT_JOY_MAX_AXES) na = ZCPORT_JOY_MAX_AXES;
      for (int a = 0; a < na && s < MAX_JOYSTICK_STICKS; a += 2, s++) {
         JOYSTICK_STICK_INFO &k = j.stick[s];
         k.flags = JOYFLAG_DIGITAL | JOYFLAG_ANALOGUE | JOYFLAG_SIGNED;
         k.num_axis = (a + 1 < na) ? 2 : 1;
         set_axis(k.axis[0], scale_axis(st.axis[a]), kAxisNames[0]);
         if (k.num_axis == 2)
            set_axis(k.axis[1], scale_axis(st.axis[a + 1]), kAxisNames[1]);
         else
            set_axis(k.axis[1], 0, kAxisNames[1]);
         snprintf(g_stick_names[n][s], sizeof(g_stick_names[n][s]), "Stick %d", s + 1);
         k.name = g_stick_names[n][s];
      }

      int nh = st.num_hats;
      if (nh > ZCPORT_JOY_MAX_HATS) nh = ZCPORT_JOY_MAX_HATS;
      for (int h = 0; h < nh && s < MAX_JOYSTICK_STICKS; h++, s++) {
         JOYSTICK_STICK_INFO &k = j.stick[s];
         int m = st.hat[h];
         k.flags = JOYFLAG_DIGITAL | JOYFLAG_SIGNED;
         k.num_axis = 2;
         int x = (m & 8) ? -128 : ((m & 2) ? 128 : 0);
         int y = (m & 1) ? -128 : ((m & 4) ? 128 : 0);
         set_axis(k.axis[0], x, kAxisNames[0]);
         set_axis(k.axis[1], y, kAxisNames[1]);
         snprintf(g_stick_names[n][s], sizeof(g_stick_names[n][s]), "Hat %d", h + 1);
         k.name = g_stick_names[n][s];
      }
      j.num_sticks = s;
      for (; s < MAX_JOYSTICK_STICKS; s++) {
         JOYSTICK_STICK_INFO &k = j.stick[s];
         k.flags = 0;
         k.num_axis = 0;
         set_axis(k.axis[0], 0, kAxisNames[0]);
         set_axis(k.axis[1], 0, kAxisNames[1]);
         k.name = "";
      }
   }

   void clear_info(int n)
   {
      memset(&joy[n], 0, sizeof(joy[n]));
   }

   void update(void)
   {
      const ZCPORT_PLATFORM *p = zcport_get_platform();
      int count = (p && p->joy_count) ? p->joy_count() : 0;
      if (count < 0) count = 0;
      if (count > MAX_JOYSTICKS) count = MAX_JOYSTICKS;

      for (int n = 0; n < count; n++) {
         ZCPORT_JOY_STATE st;
         memset(&st, 0, sizeof(st));
         if (p->joy_poll && p->joy_poll(n, &st) == 0)
            fill_info(n, st);
         else
            clear_info(n);
      }
      for (int n = count; n < MAX_JOYSTICKS; n++)
         clear_info(n);
      num_joysticks = count;
   }

   void joystick_exit_func(void)
   {
      remove_joystick();
   }
}

extern "C" int install_joystick(int type)
{
   if (g_installed)
      return 0;
   init_names();
   for (int n = 0; n < MAX_JOYSTICKS; n++)
      clear_info(n);
   num_joysticks = 0;

   if (type == JOY_TYPE_NONE) {
      joystick_driver = &joystick_none;
   }
   else {
      joystick_driver = &joystick_zcport;
      update();
   }
   g_installed = true;
   _add_exit_func(joystick_exit_func, "remove_joystick");
   return 0;
}

extern "C" void remove_joystick(void)
{
   if (!g_installed)
      return;
   for (int n = 0; n < MAX_JOYSTICKS; n++)
      clear_info(n);
   num_joysticks = 0;
   joystick_driver = NULL;
   g_installed = false;
   _remove_exit_func(joystick_exit_func);
}

extern "C" int poll_joystick(void)
{
   if (!g_installed)
      return -1;
   // Like the other polling functions, gives the platform a chance to show
   // the screen (the game's input loops call this).
   zcport_pump_events();
   if (joystick_driver == &joystick_zcport)
      update();
   return 0;
}

extern "C" int save_joystick_data(const char *filename)
{
   (void)filename;
   return 0;
}

extern "C" int load_joystick_data(const char *filename)
{
   (void)filename;
   return g_installed ? 0 : -1;
}

extern "C" const char *calibrate_joystick_name(int n)
{
   (void)n;
   return NULL;
}

extern "C" int calibrate_joystick(int n)
{
   (void)n;
   return -1;
}

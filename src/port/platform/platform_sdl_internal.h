// zcport SDL platform: private declarations shared by the
// src/port/platform/platform_*.cpp files.

#ifndef ZCPORT_PLATFORM_SDL_INTERNAL_H
#define ZCPORT_PLATFORM_SDL_INTERNAL_H

#include <SDL3/SDL.h>

#include <functional>
#include <string>

#include "vk_present.h"

struct ZCPORT_MIDI_BACKEND;

namespace zcport_sdl
{
   // The MIDI backend of this system (midi_winmm.cpp): winmm's MIDI mapper
   // on Windows, TinySoundFont on Android and Linux (midi_tsf.cpp), NULL elsewhere.
   const ZCPORT_MIDI_BACKEND *midi_backend();

   // SDL scancode -> Allegro KEY_* (0 = no Allegro key).
   int map_scancode(SDL_Scancode sc);

   // The character for the keyboard buffer (zcport_key_down() 'ch').
   int key_char(SDL_Scancode sc, SDL_Keymod mod, int allegro_scancode);

   // KB_SCROLOCK_FLAG / KB_NUMLOCK_FLAG / KB_CAPSLOCK_FLAG of 'mod'.
   int lock_flags(SDL_Keymod mod);

   // ---- platform_sdl.cpp, for platform_display.cpp ------------------------

   // The platform thread runs.
   bool running();
   bool on_platform_thread();

   // Run 'fn' on the platform thread: run() waits (inline when called on
   // it), post() never waits (queued even when called on it; it runs after
   // the current event / frame).
   void run(const std::function<void()> &fn);
   void post(const std::function<void()> &fn);

   // Platform thread: the game window (NULL before the first mode), whether
   // a graphics mode is open, the game's screen size.
   SDL_Window *window();
   bool mode_active();
   void logical_size(int *w, int *h);

   // Platform thread: the window changed size, mode or monitor; refreshes
   // the refresh rate, the swapchain, the picture placement and repaints.
   void window_reconfigured(bool windowed);

   // Platform thread: the scaling for windowed / fullscreen windows, vsync.
   void set_scale_modes(zcport::ScaleMode windowed, zcport::ScaleMode fullscreen);
   void set_vsync(bool on);

   // Any thread: the refresh rate the window's display runs at.
   int window_refresh_hz();

   // Platform thread: what shows the game ("Vulkan, <GPU>", "Direct3D 11",
   // ...), empty before the window exists.
   std::string renderer_in_use();

   // ---- platform_display.cpp ------------------------------------------------

   // The game's settings loader ran: the display module manages the window.
   bool display_managed();

   // Game thread, from the gfx_open hook before the platform opens the
   // mode: runs the game's settings loader once.
   void display_before_open();

   // The renderer the settings ask for (zcdisplay::Renderer); read when the
   // window is made, so a change takes effect at the next start.
   int renderer_choice();

   // Platform thread, managed only: shows the window for a w x h mode,
   // windowed or in the chosen fullscreen kind, with the settings' size and
   // monitor. Returns 0, or -1 with *error set.
   int display_open(int w, int h, bool windowed, std::string *error);

   // Platform thread, every SDL event before anything else sees it.
   // Returns true when the event is consumed (Alt+Enter).
   bool display_event(const SDL_Event &ev);

   // Platform thread: centres the windowed window on monitor 'display'
   // (1 = leftmost) in its usable area, keeping the title bar on screen, as
   // zc_place_window_on_display() (src/zsys.cpp) does. Returns false (and
   // leaves it) when that monitor does not exist; *count = monitors.
   bool place_on_display(int display, int *count);
}

#endif

// zplayer's on-screen gamepad (platform_touch.cpp): what platform_sdl.cpp
// calls, on the platform thread.
//
// Written from scratch for zcport.

#ifndef ZCPORT_TOUCH_INTERNAL_H
#define ZCPORT_TOUCH_INTERNAL_H

#include <SDL3/SDL.h>

namespace zcport_touch
{

// After the window and the UI exist / before the window goes away.
void attach(SDL_Window *window);
void detach();

// The game's picture inside the window, in window pixels.
void set_picture(float x0, float y0, float x1, float y1, int win_px_w, int win_px_h);

// Every SDL event, after the UI had it. True when the event belongs to the
// touch controls (finger events on a control, and the mouse events SDL makes
// from touches) and must not reach the game.
bool event(const SDL_Event &ev);

// A keyboard key or gamepad was used ("Auto" hides the controls).
void physical_input();

// Lift every finger (focus lost, a menu opened).
void release_all();

} // namespace zcport_touch

#endif

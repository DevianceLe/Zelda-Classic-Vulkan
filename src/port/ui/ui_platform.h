// zplayer UI layer, platform side (platform thread only): what
// platform_sdl.cpp and vk_present.cpp call. The public side is
// zcport/ui.h.
//
// Written from scratch for zcport.

#ifndef ZCPORT_UI_PLATFORM_H
#define ZCPORT_UI_PLATFORM_H

#if !defined(__SWITCH__)
#include <SDL3/SDL.h>
#endif

struct ImDrawData;

namespace zcui_platform
{

// After the window and the Vulkan presenter exist (and the presenter's
// ImGui renderer was initialised): attach the UI to that window. The ImGui
// context itself is created once and kept for the whole run.
#if defined(__SWITCH__)
bool attach();    // platform_switch.cpp: one fixed 1280 x 720 screen
#else
bool attach(SDL_Window *window);
#endif

// Before the window or presenter goes away.
void detach();

// The presenter's ImGui renderer is (not) usable; set by vk_present.cpp.
void set_renderer_ready(bool ready);

// Every SDL event. Returns true when the event belongs to the UI only (a
// session is open and it is keyboard, mouse, text or gamepad input), so the
// platform must not pass it on to the game.
#if !defined(__SWITCH__)
bool event(const SDL_Event &ev);
#endif

// A session is open: present at the refresh rate, show the OS cursor.
bool wants_continuous();

// Build one UI frame; returns its draw data, or NULL when nothing is to be
// drawn (no renderer, or no layer drew anything).
ImDrawData *build_frame();

// Once at exit.
void shutdown();

} // namespace zcui_platform

#endif

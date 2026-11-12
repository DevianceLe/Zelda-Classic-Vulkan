/*
 * zcport: the SDL3 + Vulkan platform (library zcport_sdl).
 *
 * Linking zcport_sdl is enough: it registers itself with zcport_set_platform()
 * before main() (the library's link options keep the registering object in
 * the program). zcport_sdl_register() does the same explicitly.
 *
 * Threads: the platform owns one thread that initialises SDL, owns the
 * window, the Vulkan presenter and the event loop (it is SDL's "main thread").
 * Input reaches key[], mouse_x, ... from that thread at any time, as with
 * Allegro's input thread; the close button callback runs there too. The
 * display switch callbacks are called on the game thread, from the next
 * zcport_pump_events() (keypressed(), rest(), poll_joystick(), vsync() ...).
 *
 * When frames are shown: the game thread hands the screen over (converted
 * through _current_palette, with the mouse pointer drawn on top) at most once
 * per display refresh, from zcport_pump_events() and vsync(), and from
 * set_gfx_mode() and show_video_bitmap(); the platform thread presents each
 * new frame at once (Vulkan mailbox/immediate present, never blocking the
 * game). If the game has not handed a frame over for 100 ms (a busy loop, a
 * long load) the platform thread copies the screen itself at the refresh
 * rate, without the pointer, so the window never freezes.
 *
 * C++98 compatible header.
 */

#ifndef ZCPORT_PLATFORM_SDL_H
#define ZCPORT_PLATFORM_SDL_H

#include <stdint.h>

#ifdef __cplusplus
   extern "C" {
#endif

/* Registers the SDL platform (zcport_set_platform). Idempotent. */
void zcport_sdl_register(void);

/* Moves the game window to screen 'display' counting from the left
 * (1 = leftmost; screens with the same left edge: top first), centred in
 * its work area, keeping the title bar on screen. 0, a fullscreen mode or a
 * screen that does not exist leave it where it is (logged with al_trace).
 * Same behaviour as zc_place_window_on_display() in src/zsys.cpp. */
void zcport_place_window_on_display(int display);

/* ---- gamepads (Settings > Gamepad Setup, src/port/game/ui_gamepad.cpp) ------ */

/* Adds an SDL gamepad mapping ("GUID,name,a:b1,...") for this run and keeps it in
 * gamecontrollerdb.txt beside zc.cfg, in place of an earlier line for the same GUID; that
 * file is read when the platform starts. The pad then reads in the game's standard layout.
 * Returns 0, or -1 when SDL refused the mapping. Any thread. */
int zcport_sdl_gamepad_add_mapping(const char *mapping);

/* ---- for tests ------------------------------------------------------------- */

/* Number of frames the platform thread has presented so far. */
unsigned long zcport_sdl_frames_presented(void);

/* Copies the last presented frame (0xAARRGGBB, w*h pixels, rows packed)
 * into 'out' if it has room for max_pixels; returns 1 and sets *w/*h, or 0
 * if there is none. */
int zcport_sdl_last_frame(uint32_t *out, int max_pixels, int *w, int *h);

/* 1 if the frames go through Vulkan, 0 if through the SDL window surface
 * fallback, -1 without a window. */
int zcport_sdl_using_vulkan(void);

/* SDL window id of the game window (0 if none), for synthetic events. */
uint32_t zcport_sdl_window_id(void);

/* Size of the window's drawable area in pixels and where the display bitmap
 * is drawn in it (x0, y0, x1, y1); returns 0 without a window. */
int zcport_sdl_window_geometry(int *pw, int *ph, float *x0, float *y0, float *x1, float *y1);

/* Waits until the platform thread has handled every event queued so far
 * (including events pushed with SDL_PushEvent) and run its pending work. */
void zcport_sdl_sync(void);

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_PLATFORM_SDL_H */

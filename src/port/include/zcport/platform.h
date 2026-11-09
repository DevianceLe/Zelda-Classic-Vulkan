/*
 * zcport internal contract: the platform hook table.
 *
 * Code in the zcport library (src/port/al) never calls SDL or the OS
 * windowing/audio APIs directly. It calls the platform through this table,
 * which the platform layer (src/port/platform, target zcport_sdl) registers
 * with zcport_set_platform() before allegro_init()/install_allegro() (or at
 * the latest from its own init hook).
 *
 * Every hook is optional: a NULL member, or no platform at all (the
 * conformance tests), makes the zcport_platform_*() wrappers below fall
 * back to a portable default (std::chrono clock, std::this_thread sleep,
 * stderr for messages) or to a no-op.
 *
 * Threads: hooks are called from the game thread unless noted.
 *
 * C++98 compatible header.
 */

#ifndef ZCPORT_PLATFORM_H
#define ZCPORT_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
   extern "C" {
#endif

struct BITMAP;
struct RGB;

#define ZCPORT_PLATFORM_VERSION  1

/* Joystick snapshot the platform fills in for the joystick module. */
#define ZCPORT_JOY_MAX_AXES     16
#define ZCPORT_JOY_MAX_BUTTONS  32
#define ZCPORT_JOY_MAX_HATS     4

typedef struct ZCPORT_JOY_STATE
{
   int num_axes;
   int axis[ZCPORT_JOY_MAX_AXES];      /* -32768 .. 32767 */
   int num_buttons;
   int button[ZCPORT_JOY_MAX_BUTTONS]; /* 0 / 1 */
   int num_hats;
   int hat[ZCPORT_JOY_MAX_HATS];       /* bit mask: 1 up, 2 right, 4 down, 8 left */
   const char *name;                   /* UTF-8, owned by the platform */
} ZCPORT_JOY_STATE;

/* Called by the platform's audio thread to get 'bytes' bytes of mixed
 * audio in the format passed to audio_open(). */
typedef void (*ZCPORT_AUDIO_MIX_FUNC)(void *user, void *buffer, int bytes);

typedef struct ZCPORT_PLATFORM
{
   int version;                 /* ZCPORT_PLATFORM_VERSION */
   const char *name;            /* e.g. "SDL3" */

   /* ---- system ---------------------------------------------------------- */

   /* install_allegro(): return 0 on success. */
   int  (*init)(void);
   /* allegro_exit(), after all exit functions ran. */
   void (*exit)(void);

   /* Process pending OS events and feed them to zcport/input.h. Called
    * from zcport_pump_events(), i.e. from keypressed(), readkey(),
    * poll_*(), rest(), vsync(), the default gui_mouse_*() ... Must be
    * cheap when there is nothing to do. The platform may also present
    * the display bitmap from here (rate limited) so drawing to the screen
    * shows up the way it does with Allegro's windowed drivers. */
   void (*pump_events)(void);

   /* Monotonic clock in microseconds. Any thread. */
   uint64_t (*get_ticks_us)(void);
   /* Sleep at least ms milliseconds (0 = yield). Any thread. */
   void (*sleep_ms)(unsigned int ms);

   /* allegro_message(): show a blocking message (UTF-8). */
   void (*message)(const char *msg);
   /* set_window_title() (UTF-8). */
   void (*set_window_title)(const char *title);
   /* get_desktop_resolution(): return 0 and fill w/h, or -1. */
   int  (*get_desktop_resolution)(int *w, int *h);
   /* desktop_color_depth(): 0 if unknown. */
   int  (*desktop_color_depth)(void);
   /* get_executable_name(): fill out (UTF-8), return 0, or -1 to let
    * the foundation use its own OS query. */
   int  (*get_executable_name)(char *out, int size);
   /* win_get_window(): native window handle (HWND on Windows) or NULL. */
   void *(*native_window)(void);

   /* ---- graphics (used by the gfx mode module) -------------------------- */

   /* Open or reconfigure the game window for a w*h display of the given
    * color depth. 'windowed' is 1 for windowed, 0 for fullscreen, -1 for
    * "don't care". Return 0 on success, -1 on failure. */
   int  (*gfx_open)(int w, int h, int color_depth, int windowed);
   /* Close the window (set_gfx_mode(GFX_TEXT, ...)). */
   void (*gfx_close)(void);
   /* Show 'display' (an ordinary zcport BITMAP). For 8 bpp, 'palette'
    * (256 entries, 6-bit components) gives the colors. The platform draws
    * the mouse pointer on top, see zcport_mouse_get_pointer(). */
   void (*gfx_present)(struct BITMAP *display, const struct RGB *palette);
   /* vsync(): wait for the next display refresh (and present). */
   void (*gfx_vsync)(void);
   /* 1 if the window is windowed, 0 if fullscreen. */
   int  (*gfx_is_windowed)(void);

   /* ---- mouse ------------------------------------------------------------ */

   /* position_mouse(): move the OS pointer to display bitmap coords. */
   void (*mouse_warp)(int x, int y);
   /* show_os_cursor(): 0 = hide, else MOUSE_CURSOR_*; return 0 on success. */
   int  (*mouse_show_os_cursor)(int cursor);

   /* ---- joystick --------------------------------------------------------- */

   int  (*joy_count)(void);
   /* Fill 'state' for joystick 'index'; return 0 on success. */
   int  (*joy_poll)(int index, ZCPORT_JOY_STATE *state);

   /* ---- audio ------------------------------------------------------------ */

   /* Start an audio device that calls 'mix' from its own thread.
    * bits is 8 (unsigned) or 16 (signed native endian). Return 0 on
    * success and store the actual rate in *out_freq. */
   int  (*audio_open)(int freq, int channels, int bits,
                      ZCPORT_AUDIO_MIX_FUNC mix, void *user, int *out_freq);
   void (*audio_close)(void);
   /* Exclude the mix callback while game code changes mixer state. */
   void (*audio_lock)(void);
   void (*audio_unlock)(void);

   /* ---- added by the platform module (may be NULL) ------------------------ */

   /* get_refresh_rate() / retrace_count after set_gfx_mode(): the refresh
    * rate of the primary display in Hz (what DirectDraw reports to the
    * reference build), 0 if unknown. */
   int  (*gfx_refresh_rate)(void);

   /* set_close_button_callback(): 1 if a callback is installed, 0 if not.
    * Like the reference, the window's close button is greyed out (and
    * ignored) without a callback. The foundation should call it from
    * set_close_button_callback() and once (with 0) from install_allegro(). */
   void (*close_button_enable)(int enabled);
} ZCPORT_PLATFORM;

/* Registers the platform (NULL unregisters). The table must stay valid. */
void zcport_set_platform(const ZCPORT_PLATFORM *platform);
const ZCPORT_PLATFORM *zcport_get_platform(void);

/* ---- wrappers with defaults (foundation) -------------------------------- */

/* Pumps OS events (platform->pump_events) and, for the keyboard, generates
 * software auto-repeat. Safe to call often. */
void zcport_pump_events(void);

uint64_t zcport_ticks_us(void);       /* monotonic, any thread */
uint32_t zcport_ticks_ms(void);       /* monotonic, wraps after ~49 days */
void zcport_sleep_ms(unsigned int ms);

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_PLATFORM_H */

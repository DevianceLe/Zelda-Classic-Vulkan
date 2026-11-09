/*
 * zcport internal contract: input feed API (platform -> foundation).
 *
 * The platform layer translates OS events into these calls (normally from
 * its pump_events hook). The foundation keeps all Allegro-visible state:
 * key[], key_shifts, the keyboard buffer, software key repeat, mouse_x/y/
 * z/w/b, mouse_pos, the mouse callbacks, the close button and display
 * switch callbacks.
 *
 * All functions are safe to call from the game thread. They are also safe
 * from another thread (state is guarded by an internal lock), but the user
 * callbacks (keyboard_callback, mouse_callback, ...) then run on that
 * thread, as Allegro's do on its input thread.
 *
 * C++98 compatible header.
 */

#ifndef ZCPORT_INPUT_H
#define ZCPORT_INPUT_H

#ifdef __cplusplus
   extern "C" {
#endif

struct BITMAP;

/* ---- keyboard ------------------------------------------------------------ */

/* A key went down. scancode is an Allegro KEY_* value (1..KEY_MAX-1).
 * 'ch' is what Allegro would put in the keyboard buffer for this press:
 *   -1  no buffer entry (modifier and lock keys: KEY_LSHIFT..KEY_CAPSLOCK)
 *    0  non-character key (arrows, F1..F12, Home, ...): readkey() gives
 *       scancode << 8
 *   >0  the Unicode character, after the OS keyboard layout, Shift and
 *       Caps Lock; Ctrl+letter gives 1..26; Enter 13, Backspace 8, Tab 9,
 *       Esc 27; with Alt held (and not AltGr) Allegro reports 0.
 * Do NOT forward OS auto-repeat events: the foundation repeats the last
 * pressed key itself at the rate given by set_keyboard_rate(), as Allegro
 * does. Pressing a key that is already down is ignored. */
void zcport_key_down(int scancode, int ch);

/* A key went up. */
void zcport_key_up(int scancode);

/* Releases every key that is down (focus lost). */
void zcport_key_release_all(void);

/* Current state of the lock keys: any of KB_SCROLOCK_FLAG,
 * KB_NUMLOCK_FLAG, KB_CAPSLOCK_FLAG. Shift/Ctrl/Alt/Win/Menu flags of
 * key_shifts are derived from key[] by the foundation. */
void zcport_key_set_locks(int lock_flags);

/* ---- mouse ---------------------------------------------------------------- */

/* Absolute pointer position in display bitmap pixels (the platform maps
 * window coordinates through its scaling). dx/dy are the raw relative
 * motion for get_mouse_mickeys(). Positions are clamped to the range set
 * by set_mouse_range() (default: the display size). */
void zcport_mouse_motion(int x, int y, int dx, int dy);

/* button: 0 left, 1 right, 2 middle (mouse_b bits 1, 2, 4), 3.. extra */
void zcport_mouse_button(int button, int down);

/* Wheel: dz vertical (positive = away from the user), dw horizontal. */
void zcport_mouse_wheel(int dz, int dw);

/* Whether the pointer is inside the window (mouse_on_screen()). */
void zcport_mouse_set_on_screen(int on_screen);

/* What the platform should draw over 'display' when presenting it.
 * Returns 1 and fills the fields if the Allegro pointer is visible on that
 * bitmap (show_mouse() was given it or a bitmap sharing its surface, not
 * scared, mouse installed), 0 otherwise. x/y is where the top-left of
 * 'sprite' goes, in display coordinates (mouse position minus the focus;
 * frozen while freeze_mouse_flag is set). 'sprite' is drawn like
 * draw_sprite(): pixels equal to its mask color are transparent. */
int zcport_mouse_get_pointer(struct BITMAP *display, struct BITMAP **sprite, int *x, int *y);

/* ---- window --------------------------------------------------------------- */

/* The window's close button was pressed. Calls the function given to
 * set_close_button_callback(); returns 1 if there was one, 0 if not (the
 * platform then decides; Allegro ignores the close button without a
 * callback). */
int zcport_close_button_pressed(void);

/* The window lost (SWITCH_OUT) or regained (SWITCH_IN) the focus. Calls
 * the set_display_switch_callback() callbacks for that direction. The
 * platform implements the pausing of SWITCH_PAUSE / SWITCH_AMNESIA itself
 * (see get_display_switch_mode()). */
void zcport_display_switch_notify(int dir);

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_INPUT_H */

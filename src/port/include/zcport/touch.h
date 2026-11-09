/*
 * zplayer on touch screens: the on-screen gamepad (src/port/platform/
 * platform_touch.cpp). Each button presses a key of the game's own key
 * bindings; the game side tells the platform which ones.
 *
 * C++98-compatible C header.
 */

#ifndef ZCPORT_TOUCH_H
#define ZCPORT_TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    ZCPORT_TOUCH_UP,
    ZCPORT_TOUCH_DOWN,
    ZCPORT_TOUCH_LEFT,
    ZCPORT_TOUCH_RIGHT,
    ZCPORT_TOUCH_A,
    ZCPORT_TOUCH_B,
    ZCPORT_TOUCH_START,
    ZCPORT_TOUCH_MAP,
    ZCPORT_TOUCH_L,
    ZCPORT_TOUCH_R,
    ZCPORT_TOUCH_EX1,
    ZCPORT_TOUCH_EX2,
    ZCPORT_TOUCH_EX3,
    ZCPORT_TOUCH_EX4,
    ZCPORT_TOUCH_MENU,
    ZCPORT_TOUCH_KEYS
};

/* Fills keys[ZCPORT_TOUCH_KEYS] with Allegro KEY_ codes (0 = none). Called
 * on the platform thread whenever a button is pressed, so changed bindings
 * apply at once; it only reads the game's binding variables. */
typedef void (*ZcportTouchKeysFn)(int *keys);

void zcport_touch_set_key_source(ZcportTouchKeysFn fn);

#ifdef __cplusplus
}
#endif

#endif /* ZCPORT_TOUCH_H */

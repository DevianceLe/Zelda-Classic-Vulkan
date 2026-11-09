/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/mouse.h - mouse state and pointer.
 *
 * The platform feeds events through zcport/input.h and draws the pointer
 * as an overlay at present time (see zcport_mouse_get_pointer()); the
 * screen bitmap itself never contains pointer pixels.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_MOUSE_H
#define ALLEGRO_MOUSE_H

#include "base.h"

#ifdef __cplusplus
   extern "C" {
#endif

#define MOUSEDRV_AUTODETECT  -1
#define MOUSEDRV_NONE        0
#define MOUSEDRV_ZCPORT      AL_ID('Z','C','P','T')

typedef struct MOUSE_DRIVER
{
   int  id;
   AL_CONST char *name;
   AL_CONST char *desc;
   AL_CONST char *ascii_name;
   AL_METHOD(int,  init, (void));
   AL_METHOD(void, exit, (void));
   AL_METHOD(void, poll, (void));
   AL_METHOD(void, timer_poll, (void));
   AL_METHOD(void, position, (int x, int y));
   AL_METHOD(void, set_range, (int x1, int y_1, int x2, int y2));
   AL_METHOD(void, set_speed, (int xspeed, int yspeed));
   AL_METHOD(void, get_mickeys, (int *mickeyx, int *mickeyy));
   AL_METHOD(int,  analyse_data, (AL_CONST char *buffer, int size));
   AL_METHOD(void,  enable_hardware_cursor, (int mode));
   AL_METHOD(int,  select_system_cursor, (int cursor));
} MOUSE_DRIVER;

AL_VAR(MOUSE_DRIVER, mousedrv_none);
AL_VAR(MOUSE_DRIVER *, mouse_driver);

AL_FUNC(int, install_mouse, (void));
AL_FUNC(void, remove_mouse, (void));

AL_FUNC(int, poll_mouse, (void));
AL_FUNC(int, mouse_needs_poll, (void));

AL_FUNC(void, enable_hardware_cursor, (void));
AL_FUNC(void, disable_hardware_cursor, (void));

#define MOUSE_CURSOR_NONE        0
#define MOUSE_CURSOR_ALLEGRO     1
#define MOUSE_CURSOR_ARROW       2
#define MOUSE_CURSOR_BUSY        3
#define MOUSE_CURSOR_QUESTION    4
#define MOUSE_CURSOR_EDIT        5
#define AL_NUM_MOUSE_CURSORS     6

AL_VAR(struct BITMAP *, mouse_sprite);
AL_VAR(int, mouse_x_focus);
AL_VAR(int, mouse_y_focus);

AL_VAR(volatile int, mouse_x);
AL_VAR(volatile int, mouse_y);
AL_VAR(volatile int, mouse_z);
AL_VAR(volatile int, mouse_w);
AL_VAR(volatile int, mouse_b);
AL_VAR(volatile int, mouse_pos);

AL_VAR(volatile int, freeze_mouse_flag);

#define MOUSE_FLAG_MOVE             1
#define MOUSE_FLAG_LEFT_DOWN        2
#define MOUSE_FLAG_LEFT_UP          4
#define MOUSE_FLAG_RIGHT_DOWN       8
#define MOUSE_FLAG_RIGHT_UP         16
#define MOUSE_FLAG_MIDDLE_DOWN      32
#define MOUSE_FLAG_MIDDLE_UP        64
#define MOUSE_FLAG_MOVE_Z           128
#define MOUSE_FLAG_MOVE_W           256

AL_FUNCPTR(void, mouse_callback, (int flags));

AL_FUNC(void, show_mouse, (struct BITMAP *bmp));
AL_FUNC(void, scare_mouse, (void));
AL_FUNC(void, scare_mouse_area, (int x, int y, int w, int h));
AL_FUNC(void, unscare_mouse, (void));
AL_FUNC(void, position_mouse, (int x, int y));
AL_FUNC(void, position_mouse_z, (int z));
AL_FUNC(void, position_mouse_w, (int w));
AL_FUNC(void, set_mouse_range, (int x1, int y_1, int x2, int y2));
AL_FUNC(void, set_mouse_speed, (int xspeed, int yspeed));
AL_FUNC(void, select_mouse_cursor, (int cursor));
AL_FUNC(void, set_mouse_cursor_bitmap, (int cursor, struct BITMAP *bmp));
AL_FUNC(void, set_mouse_sprite_focus, (int x, int y));
AL_FUNC(void, get_mouse_mickeys, (int *mickeyx, int *mickeyy));
AL_FUNC(void, set_mouse_sprite, (struct BITMAP *sprite));
AL_FUNC(int, show_os_cursor, (int cursor));
AL_FUNC(int, mouse_on_screen, (void));

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_MOUSE_H */

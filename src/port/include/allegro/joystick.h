/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/joystick.h - joystick state.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_JOYSTICK_H
#define ALLEGRO_JOYSTICK_H

#include "base.h"

#ifdef __cplusplus
   extern "C" {
#endif

#define JOY_TYPE_AUTODETECT      -1
#define JOY_TYPE_NONE            0
#define JOY_TYPE_DIRECTX         AL_ID('D','X',' ',' ')
#define JOY_TYPE_WIN32           AL_ID('W','3','2',' ')
#define JOY_TYPE_ZCPORT          AL_ID('Z','C','P','T')

#define MAX_JOYSTICKS            8
#define MAX_JOYSTICK_AXIS        3
#define MAX_JOYSTICK_STICKS      5
#define MAX_JOYSTICK_BUTTONS     32

/* joystick status flags */
#define JOYFLAG_DIGITAL          1
#define JOYFLAG_ANALOGUE         2
#define JOYFLAG_CALIB_DIGITAL    4
#define JOYFLAG_CALIB_ANALOGUE   8
#define JOYFLAG_CALIBRATE        16
#define JOYFLAG_SIGNED           32
#define JOYFLAG_UNSIGNED         64

#define JOYFLAG_ANALOG           JOYFLAG_ANALOGUE
#define JOYFLAG_CALIB_ANALOG     JOYFLAG_CALIB_ANALOGUE

typedef struct JOYSTICK_AXIS_INFO
{
   int pos;
   int d1, d2;
   AL_CONST char *name;
} JOYSTICK_AXIS_INFO;

typedef struct JOYSTICK_STICK_INFO
{
   int flags;
   int num_axis;
   JOYSTICK_AXIS_INFO axis[MAX_JOYSTICK_AXIS];
   AL_CONST char *name;
} JOYSTICK_STICK_INFO;

typedef struct JOYSTICK_BUTTON_INFO
{
   int b;
   AL_CONST char *name;
} JOYSTICK_BUTTON_INFO;

typedef struct JOYSTICK_INFO
{
   int flags;
   int num_sticks;
   int num_buttons;
   JOYSTICK_STICK_INFO stick[MAX_JOYSTICK_STICKS];
   JOYSTICK_BUTTON_INFO button[MAX_JOYSTICK_BUTTONS];
} JOYSTICK_INFO;

typedef struct JOYSTICK_DRIVER
{
   int  id;
   AL_CONST char *name;
   AL_CONST char *desc;
   AL_CONST char *ascii_name;
   AL_METHOD(int, init, (void));
   AL_METHOD(void, exit, (void));
   AL_METHOD(int, poll, (void));
   AL_METHOD(int, save_data, (void));
   AL_METHOD(int, load_data, (void));
   AL_METHOD(AL_CONST char *, calibrate_name, (int n));
   AL_METHOD(int, calibrate, (int n));
} JOYSTICK_DRIVER;

AL_ARRAY(JOYSTICK_INFO, joy);
AL_VAR(int, num_joysticks);

AL_VAR(JOYSTICK_DRIVER, joystick_none);
AL_VAR(JOYSTICK_DRIVER *, joystick_driver);

AL_FUNC(int, install_joystick, (int type));
AL_FUNC(void, remove_joystick, (void));

AL_FUNC(int, poll_joystick, (void));

AL_FUNC(int, save_joystick_data, (AL_CONST char *filename));
AL_FUNC(int, load_joystick_data, (AL_CONST char *filename));

AL_FUNC(AL_CONST char *, calibrate_joystick_name, (int n));
AL_FUNC(int, calibrate_joystick, (int n));

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_JOYSTICK_H */

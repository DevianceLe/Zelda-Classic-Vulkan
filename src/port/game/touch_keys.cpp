// zplayer: tells the on-screen gamepad (src/port/platform/platform_touch.cpp)
// which keys the game is bound to, so its buttons press exactly what the
// player configured under Settings > Controls.
//
// Written for zcport.

#include "zcport/touch.h"

// The game's key bindings (src/zelda.h; set from zc.cfg by zc_sys.cpp).
extern int Akey, Bkey, Skey, Lkey, Rkey, Pkey, Exkey1, Exkey2, Exkey3, Exkey4;
extern int DUkey, DDkey, DLkey, DRkey;

namespace
{

void current_keys(int *keys)
{
    keys[ZCPORT_TOUCH_UP] = DUkey;
    keys[ZCPORT_TOUCH_DOWN] = DDkey;
    keys[ZCPORT_TOUCH_LEFT] = DLkey;
    keys[ZCPORT_TOUCH_RIGHT] = DRkey;
    keys[ZCPORT_TOUCH_A] = Akey;
    keys[ZCPORT_TOUCH_B] = Bkey;
    keys[ZCPORT_TOUCH_START] = Skey;
    keys[ZCPORT_TOUCH_MAP] = Pkey;
    keys[ZCPORT_TOUCH_L] = Lkey;
    keys[ZCPORT_TOUCH_R] = Rkey;
    keys[ZCPORT_TOUCH_EX1] = Exkey1;
    keys[ZCPORT_TOUCH_EX2] = Exkey2;
    keys[ZCPORT_TOUCH_EX3] = Exkey3;
    keys[ZCPORT_TOUCH_EX4] = Exkey4;
}

struct Register
{
    Register() { zcport_touch_set_key_source(current_keys); }
} g_register;

} // namespace

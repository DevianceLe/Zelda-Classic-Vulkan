// Forwards to the from-scratch API header, plus the Windows window handle.
#ifndef ZC_A5_WINDOWS_H
#define ZC_A5_WINDOWS_H

#include <windows.h>
#include "zc_a5_api.h"

HWND al_get_win_window_handle(ALLEGRO_DISPLAY* d);

#endif

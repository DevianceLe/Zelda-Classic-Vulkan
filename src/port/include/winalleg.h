/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * winalleg.h - lets Windows code include <windows.h> next to allegro.h.
 *
 * windows.h defines a BITMAP struct and an RGB macro that clash with the
 * Allegro names; as with Allegro, the Windows BITMAP is renamed while
 * windows.h is parsed and the RGB macro is removed afterwards (use
 * WINDOWS_RGB for the Windows one).
 *
 * win_get_window() returns the HWND of the game window (platform module;
 * NULL before set_gfx_mode or without a platform).
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef WIN_ALLEGRO_H
#define WIN_ALLEGRO_H

#ifndef ALLEGRO_H
   #error Please include allegro.h before winalleg.h!
#endif

#ifdef _WIN32

#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#undef BITMAP

#define WINDOWS_RGB(r,g,b)  ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#undef RGB

#ifdef __cplusplus
   extern "C" {
#endif

AL_FUNC(HWND, win_get_window, (void));

#ifdef __cplusplus
   }
#endif

#endif /* _WIN32 */

#endif /* WIN_ALLEGRO_H */

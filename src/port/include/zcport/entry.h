/*
 * zcport internal contract: how the zplayer process starts.
 *
 *   OS / SDL3                  src/port/entry/zplayer_main.cpp       game
 *   ---------                  -------------------------------       ----
 *   WinMain (Windows)    --+
 *   SDL_main (Android)   --+-> zplayer_entry(argc, argv)
 *   main (other systems) --+     1. zcport_sdl_register()     (platform)
 *                                2. zcport_game_main(argc, argv) -> the
 *                                   game's main() (src/zelda.cpp),
 *                                   renamed by ZCPORT_MAGIC_MAIN
 *                                   (allegro/internal/alconfig.h)
 *
 * argv: on Windows the entry passes the C runtime's ANSI argv (__argc /
 * __argv), the same bytes the Allegro build's main() receives, not SDL's
 * UTF-8 conversion. Elsewhere it is what the system or SDLActivity gives.
 *
 * The working directory is not changed: like the Allegro build, the player
 * opens zelda.dat, zc.cfg, zc.sav ... relative to it (docs/zplayer-port.md,
 * "Running zplayer"). On Android the entry will have to chdir to the app's
 * storage first (phase 5).
 *
 * C++98 compatible header. Include after <allegro.h> only if you need it;
 * it does not include SDL.
 */

#ifndef ZCPORT_ENTRY_H
#define ZCPORT_ENTRY_H

#ifdef __cplusplus
   extern "C" {
#endif

/* zplayer's window title: shown from startup (zplayer_main.cpp) and set
 * again where the game sets its own title (src/zelda.cpp). */
#ifndef ZPLAYER_WINDOW_TITLE
#define ZPLAYER_WINDOW_TITLE "ZC 2.53.444 - Vulkan"
#endif

/* The game's main(), see ZCPORT_MAGIC_MAIN in allegro/internal/alconfig.h. */
int zcport_game_main(int argc, char *argv[]);

/* Implemented by the platform layer (target zcport_sdl).
 *
 * Called exactly once by the entry point, on the thread that will run the
 * game, before zcport_game_main() and therefore before allegro_init().
 * It must register the platform hook table with zcport_set_platform()
 * (zcport/platform.h). It must not open a window or initialise SDL
 * subsystems that the platform's init hook (install_allegro) opens anyway;
 * SDL_SetMainReady() has already been called (by SDL_RunApp). Calling it
 * again must be harmless. Conformance tests that link zcport_sdl
 * (test_*_sdl*.cpp) call it from CONF_PRE_INIT on the port side. */
void zcport_sdl_register(void);

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_ENTRY_H */

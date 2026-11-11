// zplayer: the process entry point.
//
// The game's main() (src/zelda.cpp) is compiled as zcport_game_main()
// (ZCPORT_MAGIC_MAIN, src/port/include/allegro/internal/alconfig.h). This
// file provides the entry point the system actually calls and hands over to
// it; the contract is in src/port/include/zcport/entry.h.
//
//   Windows   WinMain() below -> SDL_RunApp() -> zplayer_entry()
//   Android   SDLActivity (Java) -> SDL_main() (exported by SDL_main.h from
//             the main() below) -> zplayer_entry()
//   others    SDL_main.h's main() -> SDL_RunApp() -> SDL_main() = the main()
//             below -> zplayer_entry()
//
// This file must not include <allegro.h>: the replacement headers rename
// main, and SDL_main.h does too.
//
// Written from scratch for zcport.

#if defined(_WIN32)
// zplayer supplies WinMain itself (see below), so SDL_main.h must not.
#define SDL_MAIN_HANDLED
#endif
#include <SDL3/SDL_main.h>

#include "zcport/entry.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#endif

#if defined(_WIN32)
extern "C" void zplayer_install_crash_report(void); // crash_win32.cpp
#endif
#if defined(__ANDROID__)
extern "C" int zplayer_android_prepare_data(void); // android_data.cpp
#endif
#if defined(__APPLE__)
#include <SDL3/SDL_filesystem.h>
#include <string>
#include <unistd.h>

// The player opens its data relative to the working directory. Started from a
// terminal inside its folder that's already right; double-clicked in Finder it
// starts in the user's home folder, so move to the program's own folder when the
// game data isn't where it started.
static void zplayer_macos_find_data(void)
{
    if(access("zelda.dat", R_OK) == 0)
        return;
    const char *base = SDL_GetBasePath();
    if(!base)
        return;
    std::string dat = std::string(base) + "zelda.dat";
    if(access(dat.c_str(), R_OK) == 0)
        chdir(base);
}
#endif

// zcport (foundation_system.cpp); the platform keeps the title until the
// window exists. Declared here because this file must not include <allegro.h>.
extern "C" void set_window_title(const char *name);

static int SDLCALL zplayer_entry(int argc, char *argv[])
{
#if defined(_WIN32)
    zplayer_install_crash_report();
#endif
    // The platform layer registers its hook table before the game calls
    // allegro_init().
    zcport_sdl_register();
#if defined(__ANDROID__)
    // Game data from the APK into internal storage, which becomes the
    // working directory (the player opens its files relative to it).
    zplayer_android_prepare_data();
#endif
#if defined(__APPLE__)
    zplayer_macos_find_data();
#endif
    // Instead of the executable's name until the game sets its title.
    set_window_title(ZPLAYER_WINDOW_TITLE);
    return zcport_game_main(argc, argv);
}

#if defined(_WIN32)

// The Allegro build's main() receives the command line in the ANSI code
// page, split by the C runtime rules. The C runtime has exactly that ready
// in __argc/__argv (also for /SUBSYSTEM:WINDOWS programs); SDL_RunApp()
// passes a non-NULL argv on unchanged (SDL's own parsing would produce
// UTF-8, a different byte sequence for non-ASCII quest paths).
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;
    return SDL_RunApp(__argc, __argv, zplayer_entry, NULL);
}

#else

// SDL_main.h renames this to SDL_main(): exported for SDLActivity on
// Android, called by SDL's own main() on the other systems.
int main(int argc, char *argv[])
{
    return zplayer_entry(argc, argv);
}

#endif

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
#if (defined(__APPLE__) || defined(__linux__)) && !defined(__ANDROID__)
#define ZCPORT_FIND_DATA 1
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <string>
#include <vector>
#include <unistd.h>

extern char **environ;

// The programs open their data (zelda.dat or zquest.dat, the other .dat files, the
// configs) relative to the working directory. Started from a terminal inside their
// folder that's already right. Double-clicked in Finder, started from a desktop
// launcher or file manager, or typed as ~/zc/zqeditor in another folder, they start
// elsewhere, so move to the program's own folder when its data isn't where it
// started but is there. Before that, command-line arguments and ZC_*, ZQ* and
// ZPLAYER_* environment variables that name files or folders where it started (a
// quest to open, the files a test switch reads and writes, ZQ_UI_SCRIPT) get the
// starting folder in front, so they still name the same thing.

namespace
{

// Switches whose next arguments name files or folders, one letter for each of those
// arguments: 'i' for a file the program reads (or reads and writes), 'o' for one it
// only writes, '-' for one that is not a path (a number, "30", "item").
struct PathSwitch
{
    const char *name;
    const char *args;
};

const PathSwitch path_switches[] =
{
    // zqeditor's test switches (zquest.cpp main)
    { "-resave", "io" },
    { "-qst-own-save", "io" },
    { "-qst-lens-edit-test", "io--" },
    { "-qst-string-edit-test", "io---" },
    { "-qst-capture-check", "io" },
    { "-qst-rewrite-check", "io" },
    { "-qst-dump-hashes", "io" },
    { "-qst-compare", "iio" },
    { "-qst-save-newer", "io-o" },
    { "-qst-tile-check", "io" },
    { "-qst-tile-limit-test", "io" },
    { "-qst-roundtrip-check", "io" },
    { "-qst-dump-record", "i--o" },
    { "-qst-dump-combo", "i-o" },
    { "-qst-sections", "io" },
    // zplayer's (zelda.cpp main, replay.cpp replay_init)
    { "-standalone", "i" },
    { "-savefile", "i" },
    { "-quest-load-check", "io" },
    { "-q255-check", "io" },
    { "-record", "o" },
    { "-replay", "i" },
    { "-framehash", "o" },
    { "-quest-smoke", "o" },
    { "-quest-tour", "o" },
    { "-framedump", "o--" },
};

// The letters for the arguments after a switch, or NULL for any other argument.
const char *switch_args(const char *a)
{
    for(size_t i = 0; i < sizeof(path_switches) / sizeof(path_switches[0]); ++i)
        if(strcasecmp(a, path_switches[i].name) == 0)
            return path_switches[i].args;
    return NULL;
}

// "out.qst" or "Shot.JPG", not "30", "1.5" or "item".
bool has_letter_extension(const char *a)
{
    const char *dot = strrchr(a, '.');
    if(!dot || dot == a || !dot[1] || strchr(dot, '/'))
        return false;
    bool letter = false;
    for(const char *c = dot + 1; *c; ++c)
    {
        if(!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9')))
            return false;
        if(!(*c >= '0' && *c <= '9'))
            letter = true;
    }
    return letter;
}

// Whether a name means a file or folder in the starting folder, and gets that folder in
// front; otherwise it stays as it is (not a path, or one in the program's folder).
//   'o'  a file the program writes: always the starting folder's.
//   'i'  a file it reads: the starting folder's if it is there, else the program folder's
//        if it is there (a quest named as 2.53 expects, from its own folder), else the
//        starting folder's (a new file, or a mistyped name reported with the folder meant).
//   'g'  any other argument or a variable's value: as 'i' when it looks like a path (it
//        has a folder part or an extension with a letter), but a name in a folder that
//        isn't in the starting folder stays as it is.
bool from_start(const char *a, char how, const std::string &base)
{
    if(!a || !a[0] || a[0] == '-' || a[0] == '/')
        return false;
    if(how == 'o')
        return true;
    const char *slash = strrchr(a, '/');
    if(how == 'g' && !slash && !has_letter_extension(a))
        return false;
    if(access(a, F_OK) == 0)
        return true;
    if(access((base + a).c_str(), F_OK) == 0)
        return false;
    if(how == 'g' && slash)
        return access(std::string(a, slash - a).c_str(), F_OK) == 0;
    return true;
}

bool our_variable(const char *name_value)
{
    return strncmp(name_value, "ZC_", 3) == 0 || strncmp(name_value, "ZQ", 2) == 0 ||
           strncmp(name_value, "ZPLAYER_", 8) == 0;
}

} // namespace

// Called first thing in main(), before the program starts its threads, as it may set
// variables. (Only the ZQ_UI_SCRIPT test driver, autotest.cpp, starts earlier: it makes
// that path absolute itself and waits for the window.)
static void zcport_find_data(int argc, char *argv[])
{
#if defined(ZC_ZQEDITOR)
    const char *dat = "zquest.dat";
#else
    const char *dat = "zelda.dat";
#endif
    if(access(dat, R_OK) == 0)
        return;
    const char *b = SDL_GetBasePath();
    if(!b || !*b)
        return;
    std::string base(b);
    if(base[base.size() - 1] != '/')
        base += '/';
    if(access((base + dat).c_str(), R_OK) != 0)
        return;

    // What needs the starting folder in front.
    std::vector<int> args;
    for(int i = 1; i < argc; ++i)
    {
        if(const char *spec = switch_args(argv[i]))
        {
            for(; *spec && i + 1 < argc; ++spec)
            {
                ++i;
                if(*spec != '-' && from_start(argv[i], *spec, base))
                    args.push_back(i);
            }
        }
        else if(from_start(argv[i], 'g', base))
            args.push_back(i);
    }
    std::vector<std::string> names, values;
    for(char **e = environ; e && *e; ++e)
    {
        const char *eq = strchr(*e, '=');
        if(eq && our_variable(*e) && from_start(eq + 1, 'g', base))
        {
            names.push_back(std::string(*e, eq - *e));
            values.push_back(eq + 1);
        }
    }
    if(!args.empty() || !names.empty())
    {
        char *c = getcwd(NULL, 0);      // of any length: glibc and macOS allocate it
        if(!c)
        {
            // without it those names would mean files in the program's folder instead
            fprintf(stderr, "Can't read the current folder's name, so staying in it (the data files are in %s)\n", base.c_str());
            return;
        }
        std::string cwd(c);
        free(c);
        if(cwd.empty() || cwd[cwd.size() - 1] != '/')
            cwd += '/';
        for(size_t k = 0; k < args.size(); ++k)
            if(char *copy = strdup((cwd + argv[args[k]]).c_str()))
                argv[args[k]] = copy;   // kept for the whole run, as argv itself
        for(size_t k = 0; k < names.size(); ++k)
            // (SDL's own copy of the environment, read by SDL_getenv, as well)
            SDL_setenv_unsafe(names[k].c_str(), (cwd + values[k]).c_str(), 1);
    }
    // (if this fails the program stays where it started and reports the missing data itself)
    int moved = chdir(base.c_str());
    (void)moved;
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

#if defined(__APPLE__)
// platform_sdl.cpp: runs the game on a thread of its own and SDL's platform loop on
// this, the main thread, the only one macOS lets use windows and events.
extern "C" int zcport_sdl_host_main(int (*game)(int, char **), int argc, char **argv);
#endif

// SDL_main.h renames this to SDL_main(): exported for SDLActivity on
// Android, called by SDL's own main() on the other systems.
int main(int argc, char *argv[])
{
#if defined(ZCPORT_FIND_DATA)
    zcport_find_data(argc, argv);   // before macOS starts the game's thread
#endif
#if defined(__APPLE__)
    return zcport_sdl_host_main(zplayer_entry, argc, argv);
#else
    return zplayer_entry(argc, argv);
#endif
}

#endif

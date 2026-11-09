// zplayer: is_only_instance() (src/single_instance.h) where the Allegro
// build has no implementation that fits.
//
// - Windows: src/win32.cpp's named mutex is used unchanged, so zplayer and
//   the Allegro zelda.exe see each other (they share zc.sav when they run in
//   the same folder).
// - Linux and other desktop Unix: src/single_instance_unix.cpp, added by
//   src/port/CMakeLists.txt.
// - Android (here): the system starts at most one instance of the activity
//   (SDL's template manifest declares it launchMode="singleInstance"), so
//   there is never another one.
//
// Written for zcport.

#if defined(__ANDROID__)

#include "single_instance.h"

bool is_only_instance(const char* name)
{
    (void)name;
    return true;
}

#endif

/*
 * zplayer on 64-bit Linux/Android (LP64): the game's own sources keep the
 * integer model they were written for (32-bit Windows).
 *
 * Forced into every game source file (src/port/CMakeLists.txt, -include)
 * when building zplayer for an LP64 system, never on Windows (where 'long'
 * is 32-bit anyway) and never into zcport, the platform layer or the
 * third-party libraries.
 *
 * - 'long' is 32-bit in the game, as on Windows: quest and save files are
 *   read into 'long' fields, ZScript arithmetic relies on 32-bit wraparound,
 *   'dword' (unsigned long) must hold exactly 32 bits. The system, C++ and
 *   zcport headers are included FIRST, so their declarations and structures
 *   keep the real 'long'; only code that follows sees 'long' as 'int'.
 * - LONG_MAX / LONG_MIN / ULONG_MAX follow (32-bit limits).
 * - rand() / srand() / RAND_MAX are Microsoft's (the same sequence as the
 *   Windows build, so enemies and scripts get the same random numbers).
 *
 * Every header the game includes must be listed below before the macro;
 * a header first included after it would see 'long' as 'int'.
 *
 * Written from scratch for zcport.
 */

#ifndef ZCPORT_LP64_H
#define ZCPORT_LP64_H

#if defined(__cplusplus) && defined(__LP64__) && !defined(_WIN32)

/* ---- C and POSIX headers ------------------------------------------------ */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#if !defined(__SWITCH__)  /* no Unix sockets in libnx, and the game doesn't need them there */
#include <sys/socket.h>
#include <sys/un.h>
#endif
#if __has_include(<alloca.h>)
#include <alloca.h>
#endif
#if __has_include(<malloc.h>)
#include <malloc.h>
#endif
#if __has_include(<libintl.h>)
#include <libintl.h>
#endif

/* ---- C++ headers -------------------------------------------------------- */
#include <algorithm>
#include <atomic>
#include <bitset>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeinfo>
#include <utility>
#include <vector>

/* ---- zcport (the Allegro API) ------------------------------------------- */
#include <allegro.h>
#include <allegro/internal/aintern.h>
#if __has_include(<allegro/alcompat.h>)
#include <allegro/alcompat.h>
#endif
#if __has_include(<loadpng.h>)
#include <loadpng.h>
#endif
#if __has_include(<jpgalleg.h>)
#include <jpgalleg.h>
#endif

/* ---- Dear ImGui (zplayer's menus and dialogs, when on the include path) -- */
/* ---- SDL3 (zplayer's video settings talk to the platform layer) ------- */
#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#endif

/* Only in the UI files, which are built as C++11 (ImGui needs it); the
 * game itself is C++98 with GCC/Clang. */
#if __has_include(<imgui.h>) && __cplusplus >= 201103L
#include <imgui.h>
#include <imgui_internal.h> /* pulls in the x86 intrinsics headers */
#endif

/* ---- newlib (Nintendo Switch) ------------------------------------------- */
/* <ctype.h> defines _X as a macro; src/zc_array.h has parameters named _X. */
#if defined(__SWITCH__)
#undef _X
#endif

/* ---- Microsoft's rand() (zcport: foundation_rand.cpp) ------------------- */
extern "C" int zcport_msvc_rand(void);
extern "C" void zcport_msvc_srand(unsigned int seed);
#define rand zcport_msvc_rand
#define srand zcport_msvc_srand
#undef RAND_MAX
#define RAND_MAX 0x7fff

/* ---- Bison's parser (ZQuest's ZScript compiler) ------------------------- */
/* Bison writes its size types as __PTRDIFF_TYPE__ / __SIZE_TYPE__, which the
 * compiler spells "long int" / "long unsigned int": "int int" after the macro
 * below. The typedefs, declared before it, keep their real width. */
#define YYPTRDIFF_T ptrdiff_t
#define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
#define YYSIZE_T size_t

/* ---- 32-bit 'long' for the game code ------------------------------------ */
#undef LONG_MAX
#undef LONG_MIN
#undef ULONG_MAX
#define LONG_MAX INT_MAX
#define LONG_MIN INT_MIN
#define ULONG_MAX UINT_MAX
#define long int

#endif /* C++ on LP64, not Windows */

#endif /* ZCPORT_LP64_H */

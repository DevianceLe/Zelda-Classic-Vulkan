/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/internal/alconfig.h - platform detection and the declaration
 * macros (AL_FUNC, AL_VAR, ...) used by the public headers.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_INTERNAL_ALCONFIG_H
#define ALLEGRO_INTERNAL_ALCONFIG_H

/* ---- platform ---------------------------------------------------------- */

#if defined(_WIN32)
   #ifndef ALLEGRO_WINDOWS
      #define ALLEGRO_WINDOWS
   #endif
   #if defined(_MSC_VER) && !defined(ALLEGRO_MSVC)
      #define ALLEGRO_MSVC
   #endif
   #if defined(__MINGW32__) && !defined(ALLEGRO_MINGW32)
      #define ALLEGRO_MINGW32
   #endif
   #define ALLEGRO_PLATFORM_STR  "zcport-Windows"
#elif defined(__ANDROID__)
   #ifndef ALLEGRO_UNIX
      #define ALLEGRO_UNIX
   #endif
   #ifndef ALLEGRO_LINUX
      #define ALLEGRO_LINUX
   #endif
   #define ALLEGRO_ANDROID
   #define ALLEGRO_PLATFORM_STR  "zcport-Android"
#elif defined(__APPLE__)
   #ifndef ALLEGRO_UNIX
      #define ALLEGRO_UNIX
   #endif
   #ifndef ALLEGRO_MACOSX
      #define ALLEGRO_MACOSX
   #endif
   #define ALLEGRO_PLATFORM_STR  "zcport-MacOSX"
#else
   #ifndef ALLEGRO_UNIX
      #define ALLEGRO_UNIX
   #endif
   #if defined(__linux__) && !defined(ALLEGRO_LINUX)
      #define ALLEGRO_LINUX
   #endif
   #define ALLEGRO_PLATFORM_STR  "zcport-Unix"
#endif

#if defined(_M_IX86) || defined(__i386__)
   #define ALLEGRO_I386
#elif defined(_M_X64) || defined(__x86_64__)
   #define ALLEGRO_AMD64
#elif defined(_M_ARM) || defined(__arm__) || defined(__aarch64__) || defined(_M_ARM64)
   #define ALLEGRO_ARM
#endif

#define ALLEGRO_LITTLE_ENDIAN
#define ALLEGRO_MULTITHREADED

/* The game was written against Allegro's MSVC configuration, which makes
 * these system headers available implicitly (zc_sys.cpp uses _getcwd from
 * <direct.h>, for example). Provide the same environment. */
#if defined(_MSC_VER)
   #include <io.h>
   #include <fcntl.h>
   #include <direct.h>
   #include <malloc.h>
#else
   /* Linux / Android: getcwd, chdir, access ... (Allegro's Unix configuration
    * pulls these in as well) */
   #include <unistd.h>
   #include <fcntl.h>
#endif

/* zcport has no assembler code at all. */
#ifndef ALLEGRO_NO_ASM
   #define ALLEGRO_NO_ASM
#endif
#ifndef ALLEGRO_USE_C
   #define ALLEGRO_USE_C
#endif

/* All color depths are available. */
#define ALLEGRO_COLOR8
#define ALLEGRO_COLOR16
#define ALLEGRO_COLOR24
#define ALLEGRO_COLOR32

/* zcport's library and tests never rename main(); see END_OF_MAIN below.
 * The zplayer executable does (ZCPORT_MAGIC_MAIN, next section). */
#ifndef ALLEGRO_NO_MAGIC_MAIN
   #define ALLEGRO_NO_MAGIC_MAIN
#endif

/* ---- program entry point (zplayer only) ---------------------------------
 *
 * With ZCPORT_MAGIC_MAIN defined (set by the CMake targets zplayer and
 * zplayer_compile_check only, never by the zcport library or the
 * conformance tests) the program's main() is compiled under the name
 * zcport_game_main(), with C linkage. The real process entry point is
 * src/port/entry/zplayer_main.cpp: WinMain on Windows, the SDL_main()
 * that SDLActivity calls on Android, SDL3's main() elsewhere. It sets up the
 * platform and then calls zcport_game_main(argc, argv). See zcport/entry.h.
 *
 * This keeps the game's main() unchanged and keeps SDL out of the
 * replacement headers. END_OF_MAIN() stays empty. */
#ifdef ZCPORT_MAGIC_MAIN
   #ifdef __cplusplus
      extern "C" int zcport_game_main(int argc, char *argv[]);
   #else
      int zcport_game_main(int argc, char *argv[]);
   #endif
   #undef main
   #define main zcport_game_main
#endif

/* ---- standard integer types --------------------------------------------- */

#include <stdint.h>

#ifndef LONG_LONG
   #define LONG_LONG  long long
#endif

/* ---- declaration helpers ------------------------------------------------ */

#ifndef AL_CONST
   #define AL_CONST     const
#endif

#ifndef INLINE
   #ifdef __cplusplus
      #define INLINE    inline
   #elif defined(_MSC_VER)
      #define INLINE    __inline
   #else
      #define INLINE    static inline
   #endif
#endif

#define AL_VAR(type, name)                 extern type name
#define AL_ARRAY(type, name)               extern type name[]
#define AL_FUNC(type, name, args)          type name args
#define AL_METHOD(type, name, args)        type (*name) args
#define AL_FUNCPTR(type, name, args)       extern type (*name) args
#define AL_PRINTFUNC(type, name, args, a, b)  AL_FUNC(type, name, args)
#define AL_FUNC_DEPRECATED(type, name, args)  AL_FUNC(type, name, args)
#define AL_PRINTFUNC_DEPRECATED(type, name, args, a, b)  AL_FUNC(type, name, args)

/* Allegro's inline helpers expand to a plain prototype here: the bodies
 * live in the zcport library (src/port/al). */
#define AL_INLINE(type, name, args, code)             type name args;
#define AL_INLINE_DEPRECATED(type, name, args, code)  type name args;

#define AL_ALIAS(DECL, CALL)                       \
   static INLINE DECL { return CALL; }
#define AL_ALIAS_VOID_RET(DECL, CALL)              \
   static INLINE void DECL { CALL; }

#ifndef ZERO_SIZE_ARRAY
   #if defined(_MSC_VER)
      #pragma warning(disable: 4200)  /* zero-sized array in struct */
   #endif
   #define ZERO_SIZE_ARRAY(type, name)   type name[]
#endif

/* ---- no-ops kept for source compatibility ------------------------------- */

#ifndef END_OF_MAIN
   #define END_OF_MAIN()
#endif

#define END_OF_FUNCTION(x)
#define END_OF_STATIC_FUNCTION(x)
#define LOCK_DATA(d, s)
#define LOCK_CODE(c, s)
#define UNLOCK_DATA(d, s)
#define LOCK_VARIABLE(x)
#define LOCK_FUNCTION(x)

/* ---- file name conventions ---------------------------------------------- */

#ifndef ALLEGRO_LFN
   #define ALLEGRO_LFN  1
#endif

#if defined(ALLEGRO_WINDOWS)
   #define OTHER_PATH_SEPARATOR  '\\'
   #define DEVICE_SEPARATOR      ':'
#else
   #define OTHER_PATH_SEPARATOR  '/'
   #define DEVICE_SEPARATOR      '\0'
#endif

#ifndef FA_RDONLY
   #define FA_RDONLY       1
   #define FA_HIDDEN       2
   #define FA_SYSTEM       4
   #define FA_LABEL        8
   #define FA_DIREC        16
   #define FA_ARCH         32
#endif
#define FA_NONE         0
#define FA_ALL          (~FA_NONE)

/* ---- missing C library functions ---------------------------------------- */

#ifdef __cplusplus
   extern "C" {
#endif

#if !defined(ALLEGRO_MSVC) && !defined(ALLEGRO_MINGW32)
   AL_FUNC(int, _alemu_stricmp, (AL_CONST char *s1, AL_CONST char *s2));
   AL_FUNC(char *, _alemu_strlwr, (char *string));
   AL_FUNC(char *, _alemu_strupr, (char *string));
   #ifndef stricmp
      #define stricmp _alemu_stricmp
   #endif
   #ifndef strlwr
      #define strlwr _alemu_strlwr
   #endif
   #ifndef strupr
      #define strupr _alemu_strupr
   #endif
#endif

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_INTERNAL_ALCONFIG_H */

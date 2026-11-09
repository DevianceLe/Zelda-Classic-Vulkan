/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/debug.h - ASSERT, TRACE, al_trace and handlers.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_DEBUG_H
#define ALLEGRO_DEBUG_H

#include "base.h"

#ifdef __cplusplus
   extern "C" {
#endif

AL_FUNC(void, al_assert, (AL_CONST char *file, int linenr));
AL_PRINTFUNC(void, al_trace, (AL_CONST char *msg, ...), 1, 2);

AL_FUNC(void, register_assert_handler, (AL_METHOD(int, handler, (AL_CONST char *msg))));
AL_FUNC(void, register_trace_handler, (AL_METHOD(int, handler, (AL_CONST char *msg))));

#ifdef __cplusplus
   }
#endif

#undef ASSERT
#undef TRACE

/* The game defines DEBUGMODE (zc_alleg.h) before including allegro.h, so
 * ASSERT is live in the player exactly as with the reference build. */
#ifdef DEBUGMODE
   #define ASSERT(condition)     { if (!(condition)) al_assert(__FILE__, __LINE__); }
   #define TRACE                 al_trace
#else
   #define ASSERT(condition)
   #define TRACE                 1 ? (void) 0 : al_trace
#endif

#endif /* ALLEGRO_DEBUG_H */

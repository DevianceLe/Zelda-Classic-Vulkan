/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/fmaths.h - fixed point arithmetic.
 *
 * The basic arithmetic (conversions, add, sub, mul, div, floor, ceil) is
 * implemented inline here by the foundation, because the C++ fix class and
 * a lot of game code use it in inner loops. The results are checked against
 * the reference build by tests/conformance/test_foundation_fixed.cpp.
 *
 * Semantics (reference build: Allegro 4.4.2, MSVC x86, so the
 * "i386" code paths of Allegro apply):
 *   ftofix   clamps to +-0x7FFFFFFF outside [-32767, 32767] and sets
 *            *allegro_errno = ERANGE; rounds half away from zero.
 *   fixmul   multiplies in double precision, then ftofix().
 *   fixdiv   divides in double precision, then ftofix(); y == 0 sets
 *            ERANGE and returns +-0x7FFFFFFF by the sign of x.
 *   fixadd/fixsub clamp to +-0x7FFFFFFF and set ERANGE on overflow.
 *   fixfloor/fixceil/fixtoi/itofix as documented.
 *
 * The trig/sqrt functions are NOT implemented by the foundation; they are
 * plain functions (Allegro uses inline table lookups) implemented by the
 * math module, which must reproduce Allegro's lookup tables bit for bit.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_FMATHS_H
#define ALLEGRO_FMATHS_H

#include "base.h"
#include "fixed.h"

#ifdef __cplusplus
   #define ZCPORT_INLINE  inline
   extern "C" {
#elif defined(_MSC_VER)
   #define ZCPORT_INLINE  static __inline
#else
   #define ZCPORT_INLINE  static inline
#endif

/* Implemented by the math module (Allegro: table lookups / iteration). */
AL_FUNC(fixed, fixsqrt, (fixed x));
AL_FUNC(fixed, fixhypot, (fixed x, fixed y));
AL_FUNC(fixed, fixatan, (fixed x));
AL_FUNC(fixed, fixatan2, (fixed y, fixed x));
AL_FUNC(fixed, fixcos, (fixed x));
AL_FUNC(fixed, fixsin, (fixed x));
AL_FUNC(fixed, fixtan, (fixed x));
AL_FUNC(fixed, fixacos, (fixed x));
AL_FUNC(fixed, fixasin, (fixed x));

AL_ARRAY(fixed, _cos_tbl);
AL_ARRAY(fixed, _tan_tbl);
AL_ARRAY(fixed, _acos_tbl);

/* ---- foundation: inline basic arithmetic -------------------------------- */

#define ZCPORT_FIX_MAX   ((fixed)0x7FFFFFFF)
#define ZCPORT_FIX_MIN   ((fixed)-0x7FFFFFFF)

ZCPORT_INLINE fixed ftofix(double x)
{
   if (x > 32767.0) {
      *allegro_errno = ERANGE;
      return ZCPORT_FIX_MAX;
   }
   if (x < -32767.0) {
      *allegro_errno = ERANGE;
      return ZCPORT_FIX_MIN;
   }
   return (fixed)(x * 65536.0 + ((x < 0) ? -0.5 : 0.5));
}

ZCPORT_INLINE double fixtof(fixed x)
{
   return (double)x / 65536.0;
}

/* fixadd/fixsub: the result wraps like 32-bit two's complement arithmetic;
 * overflow is only reported when both operands push the same way and the
 * wrapped result has the wrong sign (then +-0x7FFFFFFF and ERANGE). So
 * 0 - INT_MIN gives INT_MIN without an error, exactly like the reference. */
ZCPORT_INLINE fixed fixadd(fixed x, fixed y)
{
   fixed r = (fixed)((uint32_t)x + (uint32_t)y);
   if (r >= 0) {
      if ((x < 0) && (y < 0)) {
         *allegro_errno = ERANGE;
         return ZCPORT_FIX_MIN;
      }
   }
   else if ((x > 0) && (y > 0)) {
      *allegro_errno = ERANGE;
      return ZCPORT_FIX_MAX;
   }
   return r;
}

ZCPORT_INLINE fixed fixsub(fixed x, fixed y)
{
   fixed r = (fixed)((uint32_t)x - (uint32_t)y);
   if (r >= 0) {
      if ((x < 0) && (y > 0)) {
         *allegro_errno = ERANGE;
         return ZCPORT_FIX_MIN;
      }
   }
   else if ((x > 0) && (y < 0)) {
      *allegro_errno = ERANGE;
      return ZCPORT_FIX_MAX;
   }
   return r;
}

ZCPORT_INLINE fixed fixmul(fixed x, fixed y)
{
   return ftofix(fixtof(x) * fixtof(y));
}

ZCPORT_INLINE fixed fixdiv(fixed x, fixed y)
{
   if (y == 0) {
      *allegro_errno = ERANGE;
      return (x < 0) ? ZCPORT_FIX_MIN : ZCPORT_FIX_MAX;
   }
   return ftofix(fixtof(x) / fixtof(y));
}

ZCPORT_INLINE int fixfloor(fixed x)
{
   /* arithmetic shift written portably */
   return (x >= 0) ? (int)(x >> 16) : (int)~((~x) >> 16);
}

ZCPORT_INLINE int fixceil(fixed x)
{
   if (x > (fixed)0x7FFF0000) {
      *allegro_errno = ERANGE;
      return 0x7FFF;
   }
   return fixfloor(x + 0xFFFF);
}

ZCPORT_INLINE fixed itofix(int x)
{
   return (fixed)((uint32_t)x << 16);
}

ZCPORT_INLINE int fixtoi(fixed x)
{
   return fixfloor(x) + ((x & 0x8000) >> 15);
}

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_FMATHS_H */

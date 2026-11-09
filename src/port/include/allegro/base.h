/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/base.h - version numbers, basic macros, allegro_errno.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_BASE_H
#define ALLEGRO_BASE_H

#ifndef ALLEGRO_NO_STD_HEADERS
   #include <errno.h>
   #include <limits.h>
   #include <stdarg.h>
   #include <stddef.h>
   #include <stdlib.h>
   #include <time.h>
   #include <string.h>
#endif

#include "internal/alconfig.h"

#ifdef __cplusplus
   extern "C" {
#endif

/* The API level zcport emulates (what the reference player links). */
#define ALLEGRO_VERSION          4
#define ALLEGRO_SUB_VERSION      4
#define ALLEGRO_WIP_VERSION      2
#define ALLEGRO_VERSION_STR      "4.4.2"
#define ALLEGRO_DATE_STR         "2011"
#define ALLEGRO_DATE             20110519    /* yyyymmdd */

/* Marks this as the zcport replacement (never defined by real Allegro). */
#define ALLEGRO_ZCPORT           1

#ifndef TRUE
   #define TRUE         -1
   #define FALSE        0
#endif

#undef MIN
#undef MAX
#undef MID
#undef CLAMP
#undef ABS
#undef SGN

#define MIN(x,y)     (((x) < (y)) ? (x) : (y))
#define MAX(x,y)     (((x) > (y)) ? (x) : (y))

/* Median of three values. */
#define MID(x,y,z)   (((x) > (y))                                        \
                        ? (((y) > (z)) ? (y) : (((x) > (z)) ? (z) : (x))) \
                        : (((y) > (z)) ? (((z) > (x)) ? (z) : (x)) : (y)))

/* CLAMP(lo, value, hi) */
#define CLAMP(x,y,z) MAX((x), MIN((y), (z)))

#define ABS(x)       (((x) >= 0) ? (x) : (-(x)))
#define SGN(x)       (((x) >= 0) ? 1 : -1)

#define AL_PI        3.14159265358979323846

#define AL_ID(a,b,c,d)     (((a)<<24) | ((b)<<16) | ((c)<<8) | (d))

/* Points at the errno variable passed to install_allegro(). */
AL_VAR(int *, allegro_errno);

typedef struct _DRIVER_INFO         /* info about a hardware driver */
{
   int id;                          /* integer ID */
   void *driver;                    /* the driver structure */
   int autodetect;                  /* set to allow autodetection */
} _DRIVER_INFO;

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_BASE_H */

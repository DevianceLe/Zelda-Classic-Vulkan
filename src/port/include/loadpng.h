/*
 * zcport: replacement for the loadpng add-on header.
 *
 * The player only calls loadpng_init(), which makes save_bitmap()/load_bitmap()
 * understand ".png". zcport implements that itself (no libpng).
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ZCPORT_LOADPNG_H
#define ZCPORT_LOADPNG_H

#include "allegro.h"

#ifdef __cplusplus
   extern "C" {
#endif

/* Registers the "png" bitmap file type. Returns 0 on success. */
AL_FUNC(int, loadpng_init, (void));

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_LOADPNG_H */

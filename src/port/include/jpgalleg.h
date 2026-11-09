/*
 * zcport: replacement for the JPGalleg add-on header.
 *
 * The player only calls jpgalleg_init(), which makes save_bitmap()/load_bitmap()
 * understand ".jpg". zcport implements that itself.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ZCPORT_JPGALLEG_H
#define ZCPORT_JPGALLEG_H

#include "allegro.h"

#ifdef __cplusplus
   extern "C" {
#endif

/* Registers the "jpg" bitmap file type. Returns 0 on success. */
AL_FUNC(int, jpgalleg_init, (void));

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_JPGALLEG_H */

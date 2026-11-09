/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/compiled.h - compiled sprites (stored as plain bitmaps).
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_COMPILED_H
#define ALLEGRO_COMPILED_H

#include "base.h"

#ifdef __cplusplus
   extern "C" {
#endif

struct BITMAP;

/* Without assembler, Allegro's compiled sprites are just bitmaps. */
typedef struct BITMAP COMPILED_SPRITE;

AL_FUNC(COMPILED_SPRITE *, get_compiled_sprite, (struct BITMAP *bitmap, int planar));
AL_FUNC(void, destroy_compiled_sprite, (COMPILED_SPRITE *sprite));
AL_FUNC(void, draw_compiled_sprite, (struct BITMAP *bmp, AL_CONST COMPILED_SPRITE *sprite, int x, int y));

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_COMPILED_H */

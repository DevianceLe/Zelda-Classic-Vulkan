/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/rle.h - RLE sprites.
 *
 * The encoding of RLE_SPRITE->dat is private to the drawing module (the
 * game never reads it); w, h, color_depth and size must be filled in.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_RLE_H
#define ALLEGRO_RLE_H

#include "base.h"

#ifdef __cplusplus
   extern "C" {
#endif

struct BITMAP;

typedef struct RLE_SPRITE           /* a RLE compressed sprite */
{
   int w, h;                        /* width and height in pixels */
   int color_depth;                 /* color depth of the image */
   int size;                        /* size of sprite data in bytes */
   ZERO_SIZE_ARRAY(signed char, dat);
} RLE_SPRITE;

AL_FUNC(RLE_SPRITE *, get_rle_sprite, (struct BITMAP *bitmap));
AL_FUNC(void, destroy_rle_sprite, (RLE_SPRITE *sprite));

AL_FUNC(void, draw_rle_sprite, (struct BITMAP *bmp, AL_CONST struct RLE_SPRITE *sprite, int x, int y));
AL_FUNC(void, draw_trans_rle_sprite, (struct BITMAP *bmp, AL_CONST struct RLE_SPRITE *sprite, int x, int y));
AL_FUNC(void, draw_lit_rle_sprite, (struct BITMAP *bmp, AL_CONST struct RLE_SPRITE *sprite, int x, int y, int color));

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_RLE_H */

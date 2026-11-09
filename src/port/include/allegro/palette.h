/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/palette.h - RGB and PALETTE.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_PALETTE_H
#define ALLEGRO_PALETTE_H

#include "base.h"

#ifdef __cplusplus
   extern "C" {
#endif

/* Palette entries hold 6-bit components (0..63). */
typedef struct RGB
{
   unsigned char r, g, b;
   unsigned char filler;
} RGB;

#define PAL_SIZE     256

typedef RGB PALETTE[PAL_SIZE];

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_PALETTE_H */

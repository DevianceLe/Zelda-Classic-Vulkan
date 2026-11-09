/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/fixed.h - the 16.16 fixed point type.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_FIXED_H
#define ALLEGRO_FIXED_H

#include "base.h"

#ifdef __cplusplus
   extern "C" {
#endif

typedef int32_t fixed;

AL_VAR(AL_CONST fixed, fixtorad_r);
AL_VAR(AL_CONST fixed, radtofix_r);

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_FIXED_H */

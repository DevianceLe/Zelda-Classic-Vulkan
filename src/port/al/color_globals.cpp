/*
 * zcport: global color tables used by translucent and lit drawing.
 *
 * color_map: the 8-bit COLOR_MAP used by DRAW_MODE_TRANS, draw_trans_sprite
 * and friends (NULL until the game sets one, like Allegro).
 * rgb_map:   the RGB_MAP makecol8() and the create_*_table() functions use
 * when set (NULL until the game sets one).
 * _current_palette: the palette last set with set_palette() and friends
 * (all zero until then, like the reference without a graphics mode).
 *
 * Written from scratch for zcport.
 */

#include <allegro.h>

extern "C" {

COLOR_MAP *color_map = NULL;

RGB_MAP *rgb_map = NULL;

/* The palette currently set with set_palette()/set_palette_range(); the
 * platform presents 8-bit displays through it. */
PALETTE _current_palette;

} // extern "C"

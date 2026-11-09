/*
 * zcport color module: the blocking palette fades (fade_from_range and
 * friends). One fade_interpolate() step per vsync(), like the reference:
 * positions 0, speed, 2*speed, ... below 64, then the destination palette.
 *
 * A separate file so that programs which never fade do not need vsync()
 * (gfx mode module) at link time.
 *
 * Written from scratch for zcport.
 */

#include "color_internal.h"

#include <string.h>

extern "C" {

/* The blocking fades: one palette step per display refresh. */
void fade_from_range(AL_CONST PALETTE source, AL_CONST PALETTE dest, int speed, int from, int to)
{
    PALETTE temp;
    if(speed < 1)
        speed = 1;
    memcpy(temp, source, sizeof(PALETTE));
    for(int pos = 0; pos < 64; pos += speed)
    {
        fade_interpolate(source, dest, temp, pos, from, to);
        set_palette_range(temp, from, to, TRUE);
        vsync();
    }
    set_palette_range(dest, from, to, TRUE);
}

void fade_in_range(AL_CONST PALETTE p, int speed, int from, int to)
{
    fade_from_range(black_palette, p, speed, from, to);
}

void fade_out_range(int speed, int from, int to)
{
    PALETTE temp;
    get_palette(temp);
    fade_from_range(temp, black_palette, speed, from, to);
}

void fade_from(AL_CONST PALETTE source, AL_CONST PALETTE dest, int speed)
{
    fade_from_range(source, dest, speed, 0, PAL_SIZE - 1);
}

void fade_in(AL_CONST PALETTE p, int speed)
{
    fade_in_range(p, speed, 0, PAL_SIZE - 1);
}

void fade_out(int speed)
{
    fade_out_range(speed, 0, PAL_SIZE - 1);
}

} // extern "C"

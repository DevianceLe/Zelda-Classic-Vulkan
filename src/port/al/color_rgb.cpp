/*
 * zcport color module: pixel formats, makecol/getr families, bestfit_color
 * and create_rgb_table.
 *
 * Pixel formats: the component shifts are globals (the graphics driver
 * may change them in set_gfx_mode; the reference's defaults without a
 * graphics mode are below). 15/16-bit components are scaled up to 0..255
 * by bit replication (_rgb_scale_5/_rgb_scale_6).
 *
 * bestfit_color: the palette entry with the smallest weighted squared
 * distance (weights 30^2, 59^2, 11^2 for r, g, b on 6-bit components);
 * the first one wins ties; entry 0 is only considered for the mask pink
 * (63, 0, 63). Each component difference is taken modulo 128 (a
 * difference of +-64 counts as 0), which matters only for requests
 * outside 0..63; the reference behaves the same way.
 *
 * create_rgb_table: see the comment at the function.
 *
 * Written from scratch for zcport.
 */

#include "color_internal.h"

#include <string.h>

extern "C" {

int _rgb_r_shift_15 = 0;
int _rgb_g_shift_15 = 5;
int _rgb_b_shift_15 = 10;
int _rgb_r_shift_16 = 0;
int _rgb_g_shift_16 = 5;
int _rgb_b_shift_16 = 11;
int _rgb_r_shift_24 = 0;
int _rgb_g_shift_24 = 8;
int _rgb_b_shift_24 = 16;
int _rgb_r_shift_32 = 0;
int _rgb_g_shift_32 = 8;
int _rgb_b_shift_32 = 16;
int _rgb_a_shift_32 = 24;

/* v -> (v << 3) | (v >> 2) and (v << 2) | (v >> 4): 0 maps to 0, the
 * maximum maps to 255. */
int _rgb_scale_5[32] =
{
     0,   8,  16,  24,  33,  41,  49,  57,  66,  74,  82,  90,  99, 107, 115, 123,
   132, 140, 148, 156, 165, 173, 181, 189, 198, 206, 214, 222, 231, 239, 247, 255
};

int _rgb_scale_6[64] =
{
     0,   4,   8,  12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  52,  56,  60,
    65,  69,  73,  77,  81,  85,  89,  93,  97, 101, 105, 109, 113, 117, 121, 125,
   130, 134, 138, 142, 146, 150, 154, 158, 162, 166, 170, 174, 178, 182, 186, 190,
   195, 199, 203, 207, 211, 215, 219, 223, 227, 231, 235, 239, 243, 247, 251, 255
};

} // extern "C"

namespace
{

/* Weighted square of a component difference, reduced modulo 128 the way
 * the reference's lookup does: 0..63 and 65..127 mirror, 64 counts 0. */
inline int weighted_sq(int d, int weight)
{
    d &= 0x7F;
    if(d == 64)
        return 0;
    if(d > 64)
        d = 128 - d;
    return d * d * weight;
}

const int W_R = 30 * 30;
const int W_G = 59 * 59;
const int W_B = 11 * 11;

} // namespace

namespace zccolor
{

int color_distance(int dr, int dg, int db)
{
    return weighted_sq(dr, W_R) + weighted_sq(dg, W_G) + weighted_sq(db, W_B);
}

} // namespace zccolor

extern "C" {

/* ---- packing ------------------------------------------------------------- */

int makecol15(int r, int g, int b)
{
    return ((r >> 3) << _rgb_r_shift_15) | ((g >> 3) << _rgb_g_shift_15) | ((b >> 3) << _rgb_b_shift_15);
}

int makecol16(int r, int g, int b)
{
    return ((r >> 3) << _rgb_r_shift_16) | ((g >> 2) << _rgb_g_shift_16) | ((b >> 3) << _rgb_b_shift_16);
}

int makecol24(int r, int g, int b)
{
    return (r << _rgb_r_shift_24) | (g << _rgb_g_shift_24) | (b << _rgb_b_shift_24);
}

int makecol32(int r, int g, int b)
{
    return (r << _rgb_r_shift_32) | (g << _rgb_g_shift_32) | (b << _rgb_b_shift_32);
}

int makeacol32(int r, int g, int b, int a)
{
    return (r << _rgb_r_shift_32) | (g << _rgb_g_shift_32) | (b << _rgb_b_shift_32) | (a << _rgb_a_shift_32);
}

int makecol8(int r, int g, int b)
{
    if(rgb_map)
        return rgb_map->data[r >> 3][g >> 3][b >> 3];
    return bestfit_color(_current_palette, r >> 2, g >> 2, b >> 2);
}

int makecol_depth(int color_depth, int r, int g, int b)
{
    switch(color_depth)
    {
    case 8:  return makecol8(r, g, b);
    case 15: return makecol15(r, g, b);
    case 16: return makecol16(r, g, b);
    case 24: return makecol24(r, g, b);
    case 32: return makecol32(r, g, b);
    }
    return 0;
}

int makecol(int r, int g, int b)
{
    return makecol_depth(_color_depth, r, g, b);
}

int makeacol_depth(int color_depth, int r, int g, int b, int a)
{
    if(color_depth == 32)
        return makeacol32(r, g, b, a);
    return makecol_depth(color_depth, r, g, b);
}

int makeacol(int r, int g, int b, int a)
{
    return makeacol_depth(_color_depth, r, g, b, a);
}

/* ---- unpacking ----------------------------------------------------------- */

int getr8(int c) { return _rgb_scale_6[_current_palette[c & 0xFF].r & 63]; }
int getg8(int c) { return _rgb_scale_6[_current_palette[c & 0xFF].g & 63]; }
int getb8(int c) { return _rgb_scale_6[_current_palette[c & 0xFF].b & 63]; }

int getr15(int c) { return _rgb_scale_5[(c >> _rgb_r_shift_15) & 0x1F]; }
int getg15(int c) { return _rgb_scale_5[(c >> _rgb_g_shift_15) & 0x1F]; }
int getb15(int c) { return _rgb_scale_5[(c >> _rgb_b_shift_15) & 0x1F]; }

int getr16(int c) { return _rgb_scale_5[(c >> _rgb_r_shift_16) & 0x1F]; }
int getg16(int c) { return _rgb_scale_6[(c >> _rgb_g_shift_16) & 0x3F]; }
int getb16(int c) { return _rgb_scale_5[(c >> _rgb_b_shift_16) & 0x1F]; }

int getr24(int c) { return (c >> _rgb_r_shift_24) & 0xFF; }
int getg24(int c) { return (c >> _rgb_g_shift_24) & 0xFF; }
int getb24(int c) { return (c >> _rgb_b_shift_24) & 0xFF; }

int getr32(int c) { return (c >> _rgb_r_shift_32) & 0xFF; }
int getg32(int c) { return (c >> _rgb_g_shift_32) & 0xFF; }
int getb32(int c) { return (c >> _rgb_b_shift_32) & 0xFF; }
int geta32(int c) { return (c >> _rgb_a_shift_32) & 0xFF; }

int getr_depth(int color_depth, int c)
{
    switch(color_depth)
    {
    case 8:  return getr8(c);
    case 15: return getr15(c);
    case 16: return getr16(c);
    case 24: return getr24(c);
    case 32: return getr32(c);
    }
    return 0;
}

int getg_depth(int color_depth, int c)
{
    switch(color_depth)
    {
    case 8:  return getg8(c);
    case 15: return getg15(c);
    case 16: return getg16(c);
    case 24: return getg24(c);
    case 32: return getg32(c);
    }
    return 0;
}

int getb_depth(int color_depth, int c)
{
    switch(color_depth)
    {
    case 8:  return getb8(c);
    case 15: return getb15(c);
    case 16: return getb16(c);
    case 24: return getb24(c);
    case 32: return getb32(c);
    }
    return 0;
}

int geta_depth(int color_depth, int c)
{
    if(color_depth == 32)
        return geta32(c);
    return 0;
}

int getr(int c) { return getr_depth(_color_depth, c); }
int getg(int c) { return getg_depth(_color_depth, c); }
int getb(int c) { return getb_depth(_color_depth, c); }
int geta(int c) { return geta_depth(_color_depth, c); }

/* ---- palette search ------------------------------------------------------ */

int bestfit_color(AL_CONST PALETTE pal, int r, int g, int b)
{
    int first = (r == 63 && g == 0 && b == 63) ? 0 : 1;
    int best = 0;
    int lowest = 0x7FFFFFFF;

    for(int i = first; i < PAL_SIZE; ++i)
    {
        /* green has the largest weight: test it alone first */
        int d = weighted_sq(pal[i].g - g, W_G);
        if(d >= lowest)
            continue;
        d += weighted_sq(pal[i].r - r, W_R);
        if(d >= lowest)
            continue;
        d += weighted_sq(pal[i].b - b, W_B);
        if(d >= lowest)
            continue;
        best = i;
        if(d == 0)
            break;
        lowest = d;
    }
    return best;
}

/*
 * create_rgb_table: region growing from the palette's own cells, which is
 * what the reference does (measured black-box):
 *  - seeds: each entry 1..255 claims its cell (r/2, g/2, b/2) unless an
 *    earlier entry already did (later entries in a taken cell never
 *    appear; entry 0 is never a seed);
 *  - the seeds' cells are queued in entry order; a queued cell is taken
 *    out (first in, first out) and offers its entry to its six neighbours
 *    (order g+, r-, g-, b-, b+, r+): a neighbour without an entry takes
 *    it, one with an entry takes it when it is strictly closer to the
 *    neighbour's color (2r, 2g, 2b) by the bestfit_color() metric; a
 *    neighbour that changed is queued again unless it is still queued;
 *  - when entry 0 is the mask pink (63, 0, 63) the pink cell (31, 0, 31)
 *    is set to 0 at the end.
 *
 * KNOWN DIFFERENCE: the reference's processing order is not exactly
 * this one, so where two entries are equally close (and, rarely, where
 * the flood is blocked) a few cells name another entry: 0.04-0.06% of the
 * cells for random or game-like palettes, about 4% for palettes made of a
 * few evenly spaced levels (many exact ties). An exact nearest-color
 * search differs from the reference more (0.15-0.5%).
 * tests/conformance/test_color_rgb.cpp (color_rgb_table,
 * color_rgb_table_quality) reports it.
 */
void create_rgb_table(RGB_MAP *table, AL_CONST PALETTE pal, AL_METHOD(void, callback, (int pos)))
{
    const int CELLS = 32 * 32 * 32;
    static short owner[CELLS];         /* palette entry, -1 = none yet */
    static unsigned char queued[CELLS];
    static int ring[CELLS];            /* a cell is queued at most once at a time */
    int head = 0, count = 0;

    for(int i = 0; i < CELLS; ++i)
    {
        owner[i] = -1;
        queued[i] = 0;
    }

    for(int i = 1; i < PAL_SIZE; ++i)
    {
        int cell = (((pal[i].r >> 1) & 31) << 10) | (((pal[i].g >> 1) & 31) << 5) | ((pal[i].b >> 1) & 31);
        if(owner[cell] >= 0)
            continue;
        owner[cell] = (short)i;
        queued[cell] = 1;
        ring[(head + count++) % CELLS] = cell;
    }

    /* neighbour order g+, r-, g-, b-, b+, r+ */
    static const int STEP_R[6] = { 0, -1, 0, 0, 0, 1 };
    static const int STEP_G[6] = { 1, 0, -1, 0, 0, 0 };
    static const int STEP_B[6] = { 0, 0, 0, -1, 1, 0 };

    long pops = 0;
    int calls = 0;
    while(count > 0)
    {
        int cell = ring[head];
        head = (head + 1) % CELLS;
        --count;
        queued[cell] = 0;

        int c = owner[cell];
        const RGB &p = pal[c];
        int r = cell >> 10, g = (cell >> 5) & 31, b = cell & 31;

        for(int k = 0; k < 6; ++k)
        {
            int nr = r + STEP_R[k], ng = g + STEP_G[k], nb = b + STEP_B[k];
            if(nr < 0 || nr > 31 || ng < 0 || ng > 31 || nb < 0 || nb > 31)
                continue;
            int n = (nr << 10) | (ng << 5) | nb;
            int o = owner[n];
            if(o == c)
                continue;
            if(o >= 0)
            {
                int mine = weighted_sq(p.r - 2 * nr, W_R) + weighted_sq(p.g - 2 * ng, W_G) + weighted_sq(p.b - 2 * nb, W_B);
                const RGB &q = pal[o];
                int theirs = weighted_sq(q.r - 2 * nr, W_R) + weighted_sq(q.g - 2 * ng, W_G) + weighted_sq(q.b - 2 * nb, W_B);
                if(mine >= theirs)
                    continue;
            }
            owner[n] = (short)c;
            if(!queued[n])
            {
                queued[n] = 1;
                ring[(head + count++) % CELLS] = n;
            }
        }

        /* 256 progress calls, one per 128 cells processed */
        if(callback && ++pops % 128 == 0 && calls < 256)
            callback(calls++);
    }
    while(callback && calls < 256)
        callback(calls++);

    for(int r = 0; r < 32; ++r)
        for(int g = 0; g < 32; ++g)
            for(int b = 0; b < 32; ++b)
            {
                int o = owner[(r << 10) | (g << 5) | b];
                table->data[r][g][b] = (unsigned char)(o < 0 ? 0 : o);
            }

    if(pal[0].r == 63 && pal[0].g == 0 && pal[0].b == 63)
        table->data[31][0][31] = 0;
}

} // extern "C"

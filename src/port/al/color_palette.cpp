/*
 * zcport color module: palettes.
 *
 * set_palette / set_palette_range / set_color store into _current_palette
 * (the palette the platform presents 8-bit displays with). Outside 8-bit
 * color depth they also refresh the palette_color entries of the current
 * depth. get_* copy whole RGB entries (the filler byte included).
 *
 * palette_color: one lookup table per color depth; the header maps the
 * name to zcport_palette_color_table(), which picks the table of the
 * current color depth (the reference switches the pointer in
 * set_color_depth()). All tables start zero-filled; set_gfx_mode() fills
 * _palette_color8 with the identity (gfx mode module).
 *
 * fade_interpolate: out = (src * (63 - pos) + dst * pos) / 64 per component
 * with C integer division, stored into the unsigned char components; the
 * filler byte of the output is left alone. This is the reference
 * behaviour, including pos outside 0..64 (measured black-box).
 *
 * Written from scratch for zcport.
 */

#include "color_internal.h"

#include <string.h>

extern "C" {

/* Unset palette: all black. */
PALETTE black_palette;

/* The Atari ST desktop colours, 16 entries repeated 16 times (the table
 * is in the Allegro documentation). */
PALETTE desktop_palette =
{
#define ZC_DESKTOP16 \
    {63,63,63,0}, {63, 0, 0,0}, { 0,63, 0,0}, {63,63, 0,0}, \
    { 0, 0,63,0}, {63, 0,63,0}, { 0,63,63,0}, {16,16,16,0}, \
    {31,31,31,0}, {63,31,31,0}, {31,63,31,0}, {63,63,31,0}, \
    {31,31,63,0}, {63,31,63,0}, {31,63,63,0}, { 0, 0, 0,0},
    ZC_DESKTOP16 ZC_DESKTOP16 ZC_DESKTOP16 ZC_DESKTOP16
    ZC_DESKTOP16 ZC_DESKTOP16 ZC_DESKTOP16 ZC_DESKTOP16
    ZC_DESKTOP16 ZC_DESKTOP16 ZC_DESKTOP16 ZC_DESKTOP16
    ZC_DESKTOP16 ZC_DESKTOP16 ZC_DESKTOP16 ZC_DESKTOP16
#undef ZC_DESKTOP16
};

/* The standard VGA BIOS palette (16 CGA colours, 16 greys, 3 x 3 blocks of
 * 24 hues, 7 black entries) with entry 255 white, as the reference build
 * reports it. */
PALETTE default_palette =
{
    { 0, 0, 0,0}, { 0, 0,42,0}, { 0,42, 0,0}, { 0,42,42,0}, {42, 0, 0,0}, {42, 0,42,0}, {42,21, 0,0}, {42,42,42,0},
    {21,21,21,0}, {21,21,63,0}, {21,63,21,0}, {21,63,63,0}, {63,21,21,0}, {63,21,63,0}, {63,63,21,0}, {63,63,63,0},
    { 0, 0, 0,0}, { 5, 5, 5,0}, { 8, 8, 8,0}, {11,11,11,0}, {14,14,14,0}, {17,17,17,0}, {20,20,20,0}, {24,24,24,0},
    {28,28,28,0}, {32,32,32,0}, {36,36,36,0}, {40,40,40,0}, {45,45,45,0}, {50,50,50,0}, {56,56,56,0}, {63,63,63,0},
    { 0, 0,63,0}, {16, 0,63,0}, {31, 0,63,0}, {47, 0,63,0}, {63, 0,63,0}, {63, 0,47,0}, {63, 0,31,0}, {63, 0,16,0},
    {63, 0, 0,0}, {63,16, 0,0}, {63,31, 0,0}, {63,47, 0,0}, {63,63, 0,0}, {47,63, 0,0}, {31,63, 0,0}, {16,63, 0,0},
    { 0,63, 0,0}, { 0,63,16,0}, { 0,63,31,0}, { 0,63,47,0}, { 0,63,63,0}, { 0,47,63,0}, { 0,31,63,0}, { 0,16,63,0},
    {31,31,63,0}, {39,31,63,0}, {47,31,63,0}, {55,31,63,0}, {63,31,63,0}, {63,31,55,0}, {63,31,47,0}, {63,31,39,0},
    {63,31,31,0}, {63,39,31,0}, {63,47,31,0}, {63,55,31,0}, {63,63,31,0}, {55,63,31,0}, {47,63,31,0}, {39,63,31,0},
    {31,63,31,0}, {31,63,39,0}, {31,63,47,0}, {31,63,55,0}, {31,63,63,0}, {31,55,63,0}, {31,47,63,0}, {31,39,63,0},
    {45,45,63,0}, {49,45,63,0}, {54,45,63,0}, {58,45,63,0}, {63,45,63,0}, {63,45,58,0}, {63,45,54,0}, {63,45,49,0},
    {63,45,45,0}, {63,49,45,0}, {63,54,45,0}, {63,58,45,0}, {63,63,45,0}, {58,63,45,0}, {54,63,45,0}, {49,63,45,0},
    {45,63,45,0}, {45,63,49,0}, {45,63,54,0}, {45,63,58,0}, {45,63,63,0}, {45,58,63,0}, {45,54,63,0}, {45,49,63,0},
    { 0, 0,28,0}, { 7, 0,28,0}, {14, 0,28,0}, {21, 0,28,0}, {28, 0,28,0}, {28, 0,21,0}, {28, 0,14,0}, {28, 0, 7,0},
    {28, 0, 0,0}, {28, 7, 0,0}, {28,14, 0,0}, {28,21, 0,0}, {28,28, 0,0}, {21,28, 0,0}, {14,28, 0,0}, { 7,28, 0,0},
    { 0,28, 0,0}, { 0,28, 7,0}, { 0,28,14,0}, { 0,28,21,0}, { 0,28,28,0}, { 0,21,28,0}, { 0,14,28,0}, { 0, 7,28,0},
    {14,14,28,0}, {17,14,28,0}, {21,14,28,0}, {24,14,28,0}, {28,14,28,0}, {28,14,24,0}, {28,14,21,0}, {28,14,17,0},
    {28,14,14,0}, {28,17,14,0}, {28,21,14,0}, {28,24,14,0}, {28,28,14,0}, {24,28,14,0}, {21,28,14,0}, {17,28,14,0},
    {14,28,14,0}, {14,28,17,0}, {14,28,21,0}, {14,28,24,0}, {14,28,28,0}, {14,24,28,0}, {14,21,28,0}, {14,17,28,0},
    {20,20,28,0}, {22,20,28,0}, {24,20,28,0}, {26,20,28,0}, {28,20,28,0}, {28,20,26,0}, {28,20,24,0}, {28,20,22,0},
    {28,20,20,0}, {28,22,20,0}, {28,24,20,0}, {28,26,20,0}, {28,28,20,0}, {26,28,20,0}, {24,28,20,0}, {22,28,20,0},
    {20,28,20,0}, {20,28,22,0}, {20,28,24,0}, {20,28,26,0}, {20,28,28,0}, {20,26,28,0}, {20,24,28,0}, {20,22,28,0},
    { 0, 0,16,0}, { 4, 0,16,0}, { 8, 0,16,0}, {12, 0,16,0}, {16, 0,16,0}, {16, 0,12,0}, {16, 0, 8,0}, {16, 0, 4,0},
    {16, 0, 0,0}, {16, 4, 0,0}, {16, 8, 0,0}, {16,12, 0,0}, {16,16, 0,0}, {12,16, 0,0}, { 8,16, 0,0}, { 4,16, 0,0},
    { 0,16, 0,0}, { 0,16, 4,0}, { 0,16, 8,0}, { 0,16,12,0}, { 0,16,16,0}, { 0,12,16,0}, { 0, 8,16,0}, { 0, 4,16,0},
    { 8, 8,16,0}, {10, 8,16,0}, {12, 8,16,0}, {14, 8,16,0}, {16, 8,16,0}, {16, 8,14,0}, {16, 8,12,0}, {16, 8,10,0},
    {16, 8, 8,0}, {16,10, 8,0}, {16,12, 8,0}, {16,14, 8,0}, {16,16, 8,0}, {14,16, 8,0}, {12,16, 8,0}, {10,16, 8,0},
    { 8,16, 8,0}, { 8,16,10,0}, { 8,16,12,0}, { 8,16,14,0}, { 8,16,16,0}, { 8,14,16,0}, { 8,12,16,0}, { 8,10,16,0},
    {11,11,16,0}, {12,11,16,0}, {13,11,16,0}, {15,11,16,0}, {16,11,16,0}, {16,11,15,0}, {16,11,13,0}, {16,11,12,0},
    {16,11,11,0}, {16,12,11,0}, {16,13,11,0}, {16,15,11,0}, {16,16,11,0}, {15,16,11,0}, {13,16,11,0}, {12,16,11,0},
    {11,16,11,0}, {11,16,12,0}, {11,16,13,0}, {11,16,15,0}, {11,16,16,0}, {11,15,16,0}, {11,13,16,0}, {11,12,16,0},
    { 0, 0, 0,0}, { 0, 0, 0,0}, { 0, 0, 0,0}, { 0, 0, 0,0}, { 0, 0, 0,0}, { 0, 0, 0,0}, { 0, 0, 0,0}, {63,63,63,0},
};

int _palette_color8[256];
int _palette_color15[256];
int _palette_color16[256];
int _palette_color24[256];
int _palette_color32[256];

} // extern "C"

namespace
{

/* The table palette_color named last; kept for color depths without a
 * table (the reference leaves the pointer alone for those). */
int *g_last_palette_table = _palette_color8;

/* select_palette() backup. */
PALETTE g_saved_palette;
int g_saved_table[256];
int *g_saved_table_ptr = NULL;

void store_range(const RGB *p, int from, int to)
{
    if(from < 0)
        from = 0;
    if(to > PAL_SIZE - 1)
        to = PAL_SIZE - 1;

    int *table = (_color_depth != 8) ? zccolor::palette_table_for_depth(_color_depth) : NULL;

    for(int c = from; c <= to; ++c)
    {
        _current_palette[c] = p[c];
        if(table)
            table[c] = makecol(_rgb_scale_6[p[c].r & 63], _rgb_scale_6[p[c].g & 63], _rgb_scale_6[p[c].b & 63]);
    }
}

} // namespace

namespace zccolor
{

int *palette_table_for_depth(int depth)
{
    switch(depth)
    {
    case 8:  return _palette_color8;
    case 15: return _palette_color15;
    case 16: return _palette_color16;
    case 24: return _palette_color24;
    case 32: return _palette_color32;
    }
    return NULL;
}

} // namespace zccolor

extern "C" {

int *zcport_palette_color_table(void)
{
    int *t = zccolor::palette_table_for_depth(_color_depth);
    if(t)
        g_last_palette_table = t;
    return g_last_palette_table;
}

void set_palette_range(AL_CONST PALETTE p, int from, int to, int retracesync)
{
    (void)retracesync;  /* the platform presents with _current_palette */
    store_range(p, from, to);
}

void set_palette(AL_CONST PALETTE p)
{
    set_palette_range(p, 0, PAL_SIZE - 1, TRUE);
}

void set_color(int idx, AL_CONST RGB *p)
{
    if(idx < 0 || idx >= PAL_SIZE)
        return;
    /* store_range indexes p[c] with c == idx */
    store_range(p - idx, idx, idx);
}

void _set_color(int idx, AL_CONST RGB *p)
{
    set_color(idx, p);
}

void get_palette_range(PALETTE p, int from, int to)
{
    if(from < 0)
        from = 0;
    if(to > PAL_SIZE - 1)
        to = PAL_SIZE - 1;
    for(int c = from; c <= to; ++c)
        p[c] = _current_palette[c];
}

void get_palette(PALETTE p)
{
    get_palette_range(p, 0, PAL_SIZE - 1);
}

void get_color(int idx, RGB *p)
{
    if(idx < 0 || idx >= PAL_SIZE)
        return;
    *p = _current_palette[idx];
}

void fade_interpolate(AL_CONST PALETTE source, AL_CONST PALETTE dest, PALETTE output, int pos, int from, int to)
{
    if(from < 0)
        from = 0;
    if(to > PAL_SIZE - 1)
        to = PAL_SIZE - 1;
    for(int c = from; c <= to; ++c)
    {
        output[c].r = (unsigned char)(((int)source[c].r * (63 - pos) + (int)dest[c].r * pos) / 64);
        output[c].g = (unsigned char)(((int)source[c].g * (63 - pos) + (int)dest[c].g * pos) / 64);
        output[c].b = (unsigned char)(((int)source[c].b * (63 - pos) + (int)dest[c].b * pos) / 64);
    }
}

void select_palette(AL_CONST PALETTE p)
{
    memcpy(g_saved_palette, _current_palette, sizeof(PALETTE));
    g_saved_table_ptr = zccolor::palette_table_for_depth(_color_depth);
    if(g_saved_table_ptr)
        memcpy(g_saved_table, g_saved_table_ptr, sizeof(g_saved_table));
    store_range(p, 0, PAL_SIZE - 1);
}

void unselect_palette(void)
{
    memcpy(_current_palette, g_saved_palette, sizeof(PALETTE));
    if(g_saved_table_ptr)
        memcpy(g_saved_table_ptr, g_saved_table, sizeof(g_saved_table));
}

} // extern "C"

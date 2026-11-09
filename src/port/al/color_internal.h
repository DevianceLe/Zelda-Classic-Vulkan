/*
 * zcport color module: private helpers shared by the color_*.cpp files
 * (palettes, color conversion, blenders, color maps, fonts and text).
 * Not a contract; other modules use the public API and zcport/*.h.
 *
 * Written from scratch for zcport.
 */

#ifndef ZCPORT_COLOR_INTERNAL_H
#define ZCPORT_COLOR_INTERNAL_H

#include <allegro.h>
#include <allegro/internal/aintern.h>

#include <stdint.h>

namespace zccolor
{

/* Raw pixel access of a memory bitmap (no clipping, no drawing mode). */
inline uint32_t get_raw(BITMAP *b, int x, int y)
{
    const unsigned char *row = b->line[y];
    switch(b->vtable->color_depth)
    {
    case 8:
        return row[x];
    case 15:
    case 16:
        return ((const uint16_t *)row)[x];
    case 24:
    {
        const unsigned char *p = row + x * 3;
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    }
    default:
        return ((const uint32_t *)row)[x];
    }
}

inline void put_raw(BITMAP *b, int x, int y, uint32_t c)
{
    unsigned char *row = b->line[y];
    switch(b->vtable->color_depth)
    {
    case 8:
        row[x] = (unsigned char)c;
        break;
    case 15:
    case 16:
        ((uint16_t *)row)[x] = (uint16_t)c;
        break;
    case 24:
    {
        unsigned char *p = row + x * 3;
        p[0] = (unsigned char)c;
        p[1] = (unsigned char)(c >> 8);
        p[2] = (unsigned char)(c >> 16);
        break;
    }
    default:
        ((uint32_t *)row)[x] = c;
        break;
    }
}

/* The palette_color table for a color depth, or NULL for other depths. */
int *palette_table_for_depth(int depth);

/* Distance used by bestfit_color(): weighted squared differences of 6-bit
 * components; each difference is taken modulo 128 like the reference
 * (so out-of-range requests behave the same). */
int color_distance(int dr, int dg, int db);

} /* namespace zccolor */

#endif /* ZCPORT_COLOR_INTERNAL_H */

/*
 * zcport primitives module: private helpers shared by prim_*.cpp.
 * Not a contract; other modules use zcport/draw.h.
 */

#ifndef ZCPORT_PRIM_INTERNAL_H
#define ZCPORT_PRIM_INTERNAL_H

#include <allegro.h>
#include "zcport/core.h"
#include "zcport/draw.h"

#include <stdint.h>

namespace zcprim
{

/* Raw pixel access: no clipping, no drawing mode. */
inline uint32_t raw_get(BITMAP *b, int x, int y)
{
    unsigned char *row = b->line[y];

    switch(b->vtable->color_depth)
    {
    case 8:
        return row[x];
    case 15:
    case 16:
        return ((uint16_t *)row)[x];
    case 24:
    {
        unsigned char *p = row + x * 3;
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    }
    default:
        return ((uint32_t *)row)[x];
    }
}

inline void raw_put(BITMAP *b, int x, int y, uint32_t c)
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

/* True when (x, y) passes the bitmap's clip test (or clipping is off). */
inline bool inside_clip(BITMAP *b, int x, int y)
{
    if(!b->clip)
        return true;

    return x >= b->cl && x < b->cr && y >= b->ct && y < b->cb;
}

/* putpixel semantics: clip, then write with the drawing mode. */
inline void put_clipped(BITMAP *b, int x, int y, int color)
{
    if(inside_clip(b, x, y))
        zcport_put_mode(b, x, y, color);
}

/* The putpixel-style callback the do_* helpers drive for line/circle/... */
void put_clipped_cb(BITMAP *b, int x, int y, int color);

/* vline semantics: either order, clipping, drawing mode. */
void vfill(BITMAP *b, int x, int y1, int y2, int color);

} // namespace zcprim

#endif

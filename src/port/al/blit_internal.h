/*
 * zcport blit module: private helpers shared by blit_*.cpp.
 * Not a contract; other modules must not include this.
 *
 * Written from scratch for zcport.
 */

#ifndef ZCPORT_BLIT_INTERNAL_H
#define ZCPORT_BLIT_INTERNAL_H

#include <allegro.h>
#include <allegro/internal/aintern.h>
#include "zcport/core.h"
#include "zcport/draw.h"

#include <stdint.h>
#include <string.h>

namespace zcblit
{

inline int depth_of(const BITMAP *b)
{
    return b->vtable->color_depth;
}

inline int bpp_of_depth(int depth)
{
    return (depth + 7) / 8;
}

inline int bpp_of(const BITMAP *b)
{
    return bpp_of_depth(b->vtable->color_depth);
}

inline uint32_t mask_of_depth(int depth)
{
    switch(depth)
    {
    case 8: return MASK_COLOR_8;
    case 15: return MASK_COLOR_15;
    case 16: return MASK_COLOR_16;
    case 24: return MASK_COLOR_24;
    default: return MASK_COLOR_32;
    }
}

inline uint32_t mask_of(const BITMAP *b)
{
    return mask_of_depth(b->vtable->color_depth);
}

/* Pixel access by bytes per pixel. 24-bit pixels are B, G, R in memory and
 * read as 0x00RRGGBB. */
template <int BPP> struct Pix;

template <> struct Pix<1>
{
    static inline uint32_t get(const unsigned char *row, int x) { return row[x]; }
    static inline void put(unsigned char *row, int x, uint32_t c) { row[x] = (unsigned char)c; }
};

template <> struct Pix<2>
{
    static inline uint32_t get(const unsigned char *row, int x)
    {
        uint16_t v;
        memcpy(&v, row + x * 2, 2);
        return v;
    }
    static inline void put(unsigned char *row, int x, uint32_t c)
    {
        uint16_t v = (uint16_t)c;
        memcpy(row + x * 2, &v, 2);
    }
};

template <> struct Pix<3>
{
    static inline uint32_t get(const unsigned char *row, int x)
    {
        const unsigned char *p = row + x * 3;
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
    }
    static inline void put(unsigned char *row, int x, uint32_t c)
    {
        unsigned char *p = row + x * 3;
        p[0] = (unsigned char)c;
        p[1] = (unsigned char)(c >> 8);
        p[2] = (unsigned char)(c >> 16);
    }
};

template <> struct Pix<4>
{
    static inline uint32_t get(const unsigned char *row, int x)
    {
        uint32_t v;
        memcpy(&v, row + x * 4, 4);
        return v;
    }
    static inline void put(unsigned char *row, int x, uint32_t c)
    {
        memcpy(row + x * 4, &c, 4);
    }
};

inline uint32_t get_pixel(const BITMAP *b, int x, int y)
{
    const unsigned char *row = b->line[y];
    switch(bpp_of(b))
    {
    case 1: return Pix<1>::get(row, x);
    case 2: return Pix<2>::get(row, x);
    case 3: return Pix<3>::get(row, x);
    default: return Pix<4>::get(row, x);
    }
}

inline void put_pixel(BITMAP *b, int x, int y, uint32_t c)
{
    unsigned char *row = b->line[y];
    switch(bpp_of(b))
    {
    case 1: Pix<1>::put(row, x, c); break;
    case 2: Pix<2>::put(row, x, c); break;
    case 3: Pix<3>::put(row, x, c); break;
    default: Pix<4>::put(row, x, c); break;
    }
}

/* ---- color components ---------------------------------------------------- */

/* 6-bit (palette, 16-bit green) and 5-bit components expanded to 8 bits. */
inline int scale6(int v)
{
    v &= 63;
    return (v << 2) | (v >> 4);
}

inline int scale5(int v)
{
    v &= 31;
    return (v << 3) | (v >> 2);
}

/* ---- pixel formats (blit_convert.cpp) ------------------------------------ */

/* makecol_depth(): packs 8-bit r, g, b into a pixel of 'depth' using the
 * current _rgb_*_shift_* layout (8-bit: makecol8). */
uint32_t make_pixel(int depth, int r, int g, int b);

/* getr_depth() etc.: the 8-bit components of a pixel of 'depth' (8-bit
 * pixels through the current palette). */
void split_pixel(int depth, uint32_t c, int &r, int &g, int &b);

/* ---- palette expansion (blit_convert.cpp) -------------------------------- */

/* 'table' receives the pixel value, in 'depth', of each 8-bit palette index
 * of the current palette (what an 8-bit pixel becomes on a hicolor or
 * truecolor bitmap). */
void palette_expansion(int depth, uint32_t table[256]);

/* blit() between bitmaps of different color depths, already clipped. */
void blit_convert(BITMAP *src, BITMAP *dst, int sx, int sy, int dx, int dy, int w, int h);

/* ---- blending hooks ------------------------------------------------------- */

/* Translucent pixel on a hicolor/truecolor bitmap (zcport_trans_blender). */
inline uint32_t blend_trans(int depth, uint32_t src, uint32_t dst)
{
    return (uint32_t)zcport_trans_blender(depth, (unsigned long)src, (unsigned long)dst);
}

/* Lit pixel on a hicolor/truecolor bitmap (zcport_lit_blender). */
inline uint32_t blend_lit(int depth, uint32_t pixel, int alpha)
{
    return (uint32_t)zcport_lit_blender(depth, (unsigned long)pixel, alpha);
}

/* Lit texel of a 3D polygon (zcport_lit_blender_poly). */
inline uint32_t blend_lit_poly(int depth, uint32_t pixel, int alpha)
{
    return (uint32_t)zcport_lit_blender_poly(depth, (unsigned long)pixel, alpha);
}

} // namespace zcblit

#endif

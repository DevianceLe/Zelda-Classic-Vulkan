/*
 * zcport blit module: draw_sprite() and its flipped forms, draw_sprite_ex(),
 * draw_trans_sprite(), draw_lit_sprite(), draw_character_ex().
 *
 * A sprite covers the rectangle (x, y)-(x + w - 1, y + h - 1) of the
 * destination; when clipping is on that rectangle is cut to the clip
 * rectangle, flipped sprites are mirrored inside the full rectangle first.
 * Mask-colored sprite pixels are skipped (except by 8-bit translucent
 * sprites, which map every pixel through color_map). An 8-bit sprite drawn
 * on a hicolor/truecolor bitmap goes through the current palette.
 * Checked against the reference build by test_blit_core.cpp.
 *
 * Written from scratch for zcport.
 */

#include "blit_internal.h"

using namespace zcblit;

namespace
{

unsigned long lit_passthrough(int, unsigned long pixel, int)
{
    return pixel;
}

/* Visible destination rectangle of a sprite drawn at (x, y). */
struct Area
{
    int x0, x1, y0, y1;
};

bool sprite_area(BITMAP *bmp, BITMAP *spr, int x, int y, Area &a)
{
    a.x0 = x;
    a.x1 = x + spr->w;
    a.y0 = y;
    a.y1 = y + spr->h;
    if(bmp->clip)
    {
        if(a.x0 < bmp->cl) a.x0 = bmp->cl;
        if(a.x1 > bmp->cr) a.x1 = bmp->cr;
        if(a.y0 < bmp->ct) a.y0 = bmp->ct;
        if(a.y1 > bmp->cb) a.y1 = bmp->cb;
    }
    return a.x0 < a.x1 && a.y0 < a.y1;
}

/* Calls f(sprite_row, sprite_x, dest_row, dest_x) for every visible pixel. */
template <class F>
void each_pixel(BITMAP *bmp, BITMAP *spr, int x, int y, bool hflip, bool vflip, F f)
{
    Area a;
    if(!sprite_area(bmp, spr, x, y, a))
        return;
    for(int dy = a.y0; dy < a.y1; ++dy)
    {
        int sy = vflip ? spr->h - 1 - (dy - y) : (dy - y);
        const unsigned char *s = spr->line[sy];
        unsigned char *d = bmp->line[dy];
        if(hflip)
        {
            for(int dx = a.x0; dx < a.x1; ++dx)
                f(s, spr->w - 1 - (dx - x), d, dx);
        }
        else
        {
            for(int dx = a.x0; dx < a.x1; ++dx)
                f(s, dx - x, d, dx);
        }
    }
}

template <int BPP>
void masked_same(BITMAP *bmp, BITMAP *spr, int x, int y, bool hflip, bool vflip)
{
    const uint32_t mask = mask_of(spr);
    each_pixel(bmp, spr, x, y, hflip, vflip,
               [mask](const unsigned char *s, int sx, unsigned char *d, int dx)
               {
                   uint32_t c = Pix<BPP>::get(s, sx);
                   if(c != mask)
                       Pix<BPP>::put(d, dx, c);
               });
}

/* 8-bit sprite on a hicolor/truecolor bitmap. */
template <int BPP>
void masked_expand(BITMAP *bmp, BITMAP *spr, int x, int y, bool hflip, bool vflip)
{
    uint32_t pal[256];
    palette_expansion(depth_of(bmp), pal);
    each_pixel(bmp, spr, x, y, hflip, vflip,
               [&pal](const unsigned char *s, int sx, unsigned char *d, int dx)
               {
                   unsigned c = s[sx];
                   if(c != MASK_COLOR_8)
                       Pix<BPP>::put(d, dx, pal[c]);
               });
}

void draw_masked(BITMAP *bmp, BITMAP *spr, int x, int y, bool hflip, bool vflip)
{
    if(!bmp || !spr)
        return;
    if(depth_of(spr) == 8 && depth_of(bmp) != 8)
    {
        switch(bpp_of(bmp))
        {
        case 2: masked_expand<2>(bmp, spr, x, y, hflip, vflip); break;
        case 3: masked_expand<3>(bmp, spr, x, y, hflip, vflip); break;
        default: masked_expand<4>(bmp, spr, x, y, hflip, vflip); break;
        }
        return;
    }
    switch(bpp_of(bmp))
    {
    case 1: masked_same<1>(bmp, spr, x, y, hflip, vflip); break;
    case 2: masked_same<2>(bmp, spr, x, y, hflip, vflip); break;
    case 3: masked_same<3>(bmp, spr, x, y, hflip, vflip); break;
    default: masked_same<4>(bmp, spr, x, y, hflip, vflip); break;
    }
}

template <int BPP>
void trans_hi(BITMAP *bmp, BITMAP *spr, int x, int y, bool hflip, bool vflip)
{
    const uint32_t mask = mask_of(spr);
    const int depth = depth_of(bmp);
    each_pixel(bmp, spr, x, y, hflip, vflip,
               [mask, depth](const unsigned char *s, int sx, unsigned char *d, int dx)
               {
                   uint32_t c = Pix<BPP>::get(s, sx);
                   if(c != mask)
                       Pix<BPP>::put(d, dx, blend_trans(depth, c, Pix<BPP>::get(d, dx)));
               });
}

/* skip_mask8: draw_sprite_ex() skips mask pixels of 8-bit sprites too,
 * draw_trans_sprite() does not. */
void draw_trans(BITMAP *bmp, BITMAP *spr, int x, int y, bool hflip, bool vflip, bool skip_mask8)
{
    if(!bmp || !spr)
        return;
    if(depth_of(bmp) == 8)
    {
        if(!color_map)
            return;
        const COLOR_MAP *map = color_map;
        if(skip_mask8)
            each_pixel(bmp, spr, x, y, hflip, vflip,
                       [map](const unsigned char *s, int sx, unsigned char *d, int dx)
                       {
                           unsigned c = s[sx];
                           if(c != MASK_COLOR_8)
                               d[dx] = map->data[c][d[dx]];
                       });
        else
            each_pixel(bmp, spr, x, y, hflip, vflip,
                       [map](const unsigned char *s, int sx, unsigned char *d, int dx)
                       {
                           d[dx] = map->data[s[sx]][d[dx]];
                       });
        return;
    }
    switch(bpp_of(bmp))
    {
    case 2: trans_hi<2>(bmp, spr, x, y, hflip, vflip); break;
    case 3: trans_hi<3>(bmp, spr, x, y, hflip, vflip); break;
    default: trans_hi<4>(bmp, spr, x, y, hflip, vflip); break;
    }
}

template <int BPP>
void lit_hi(BITMAP *bmp, BITMAP *spr, int x, int y, int color, bool hflip, bool vflip)
{
    const uint32_t mask = mask_of(spr);
    const int depth = depth_of(bmp);
    each_pixel(bmp, spr, x, y, hflip, vflip,
               [mask, depth, color](const unsigned char *s, int sx, unsigned char *d, int dx)
               {
                   uint32_t c = Pix<BPP>::get(s, sx);
                   if(c != mask)
                       Pix<BPP>::put(d, dx, blend_lit(depth, c, color));
               });
}

void draw_lit(BITMAP *bmp, BITMAP *spr, int x, int y, int color, bool hflip, bool vflip)
{
    if(!bmp || !spr)
        return;
    if(depth_of(bmp) == 8)
    {
        if(!color_map)
            return;
        const unsigned char *lit = color_map->data[color & 0xFF];
        each_pixel(bmp, spr, x, y, hflip, vflip,
                   [lit](const unsigned char *s, int sx, unsigned char *d, int dx)
                   {
                       unsigned c = s[sx];
                       if(c != MASK_COLOR_8)
                           d[dx] = lit[c];
                   });
        return;
    }
    switch(bpp_of(bmp))
    {
    case 2: lit_hi<2>(bmp, spr, x, y, color, hflip, vflip); break;
    case 3: lit_hi<3>(bmp, spr, x, y, color, hflip, vflip); break;
    default: lit_hi<4>(bmp, spr, x, y, color, hflip, vflip); break;
    }
}

template <int BPP>
void character(BITMAP *bmp, BITMAP *spr, int x, int y, int color, int bg)
{
    const uint32_t fg = (uint32_t)color, back = (uint32_t)bg;
    if(bg < 0)
        each_pixel(bmp, spr, x, y, false, false,
                   [fg](const unsigned char *s, int sx, unsigned char *d, int dx)
                   {
                       if(s[sx] != MASK_COLOR_8)
                           Pix<BPP>::put(d, dx, fg);
                   });
    else
        each_pixel(bmp, spr, x, y, false, false,
                   [fg, back](const unsigned char *s, int sx, unsigned char *d, int dx)
                   {
                       Pix<BPP>::put(d, dx, (s[sx] != MASK_COLOR_8) ? fg : back);
                   });
}

} // namespace

zcport_lit_blend_fn zcport_lit_blender = lit_passthrough;
zcport_lit_blend_fn zcport_lit_blender_poly = lit_passthrough;

extern "C" void draw_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y)
{
    draw_masked(bmp, sprite, x, y, false, false);
}

extern "C" void draw_sprite_h_flip(BITMAP *bmp, BITMAP *sprite, int x, int y)
{
    draw_masked(bmp, sprite, x, y, true, false);
}

extern "C" void draw_sprite_v_flip(BITMAP *bmp, BITMAP *sprite, int x, int y)
{
    draw_masked(bmp, sprite, x, y, false, true);
}

extern "C" void draw_sprite_vh_flip(BITMAP *bmp, BITMAP *sprite, int x, int y)
{
    draw_masked(bmp, sprite, x, y, true, true);
}

extern "C" void draw_trans_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y)
{
    draw_trans(bmp, sprite, x, y, false, false, false);
}

extern "C" void draw_lit_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int color)
{
    draw_lit(bmp, sprite, x, y, color, false, false);
}

extern "C" void draw_sprite_ex(BITMAP *bmp, BITMAP *sprite, int x, int y, int mode, int flip)
{
    const bool hflip = (flip & DRAW_SPRITE_H_FLIP) != 0;
    const bool vflip = (flip & DRAW_SPRITE_V_FLIP) != 0;
    switch(mode)
    {
    case DRAW_SPRITE_TRANS:
        draw_trans(bmp, sprite, x, y, hflip, vflip, true);
        break;
    case DRAW_SPRITE_LIT:
        draw_lit(bmp, sprite, x, y, 0, hflip, vflip);
        break;
    default:
        draw_masked(bmp, sprite, x, y, hflip, vflip);
        break;
    }
}

extern "C" void draw_character_ex(BITMAP *bmp, BITMAP *sprite, int x, int y, int color, int bg)
{
    if(!bmp || !sprite)
        return;
    switch(bpp_of(bmp))
    {
    case 1: character<1>(bmp, sprite, x, y, color, bg); break;
    case 2: character<2>(bmp, sprite, x, y, color, bg); break;
    case 3: character<3>(bmp, sprite, x, y, color, bg); break;
    default: character<4>(bmp, sprite, x, y, color, bg); break;
    }
}

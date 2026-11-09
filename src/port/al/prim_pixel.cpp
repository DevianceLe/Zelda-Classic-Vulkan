/*
 * zcport primitives: drawing modes, pixels, spans, rectangles.
 *
 * drawing_mode/solid_mode/xor_mode, putpixel/_putpixel/getpixel,
 * hline/vline/_allegro_hline/_allegro_vline, rectfill and the shared span
 * writer (zcport_hfill). Results are checked pixel by pixel against the
 * reference build by tests/conformance/test_prim_core.
 *
 * Written from scratch for zcport.
 */

#include "prim_internal.h"

using namespace zcprim;

namespace
{

struct DrawState
{
    int mode;
    BITMAP *pattern;
    int x_anchor, y_anchor;
    int x_mask, y_mask;
};

DrawState g_draw = { DRAW_MODE_SOLID, NULL, 0, 0, 0, 0 };

unsigned long passthrough_blend(int, unsigned long src, unsigned long)
{
    return src;
}

int mask_color(int depth)
{
    switch(depth)
    {
    case 15: return MASK_COLOR_15;
    case 16: return MASK_COLOR_16;
    case 24: return MASK_COLOR_24;
    case 32: return MASK_COLOR_32;
    default: return MASK_COLOR_8;
    }
}

/* Pattern pixel covering (x, y): the pattern repeats from the anchor. */
inline uint32_t pattern_at(int x, int y)
{
    return raw_get(g_draw.pattern, (x - g_draw.x_anchor) & g_draw.x_mask,
                   (y - g_draw.y_anchor) & g_draw.y_mask);
}

} // namespace

zcport_blend_fn zcport_trans_blender = passthrough_blend;

extern "C" int zcport_drawing_mode(void)
{
    return g_draw.mode;
}

extern "C" void zcport_put_mode(BITMAP *b, int x, int y, int color)
{
    int depth = b->vtable->color_depth;

    switch(g_draw.mode)
    {
    case DRAW_MODE_SOLID:
        raw_put(b, x, y, (uint32_t)color);
        break;

    case DRAW_MODE_XOR:
        raw_put(b, x, y, raw_get(b, x, y) ^ (uint32_t)color);
        break;

    case DRAW_MODE_TRANS:
        if(depth == 8)
        {
            if(color_map)
                raw_put(b, x, y, color_map->data[color & 0xFF][raw_get(b, x, y)]);
        }
        else
            raw_put(b, x, y, (uint32_t)zcport_trans_blender(depth, (unsigned long)color, raw_get(b, x, y)));
        break;

    case DRAW_MODE_COPY_PATTERN:
        raw_put(b, x, y, pattern_at(x, y));
        break;

    case DRAW_MODE_SOLID_PATTERN:
    {
        uint32_t mask = (uint32_t)mask_color(depth);
        raw_put(b, x, y, (pattern_at(x, y) == mask) ? mask : (uint32_t)color);
        break;
    }

    case DRAW_MODE_MASKED_PATTERN:
        if(pattern_at(x, y) != (uint32_t)mask_color(depth))
            raw_put(b, x, y, (uint32_t)color);
        break;
    }
}

extern "C" void zcport_hfill(BITMAP *b, int x1, int y, int x2, int color)
{
    if(x2 < x1)
    {
        int t = x1;
        x1 = x2;
        x2 = t;
    }

    if(b->clip)
    {
        if(y < b->ct || y >= b->cb)
            return;

        if(x1 < b->cl)
            x1 = b->cl;

        if(x2 >= b->cr)
            x2 = b->cr - 1;

        if(x2 < x1)
            return;
    }

    if(g_draw.mode == DRAW_MODE_SOLID && b->vtable->color_depth == 8)
    {
        memset(b->line[y] + x1, color, (size_t)(x2 - x1 + 1));
        return;
    }

    for(int x = x1; x <= x2; ++x)
        zcport_put_mode(b, x, y, color);
}

namespace zcprim
{

void put_clipped_cb(BITMAP *b, int x, int y, int color)
{
    put_clipped(b, x, y, color);
}

void vfill(BITMAP *b, int x, int y1, int y2, int color)
{
    if(y2 < y1)
    {
        int t = y1;
        y1 = y2;
        y2 = t;
    }

    if(b->clip)
    {
        if(x < b->cl || x >= b->cr)
            return;

        if(y1 < b->ct)
            y1 = b->ct;

        if(y2 >= b->cb)
            y2 = b->cb - 1;

        if(y2 < y1)
            return;
    }

    for(int y = y1; y <= y2; ++y)
        zcport_put_mode(b, x, y, color);
}

} // namespace zcprim

extern "C" {

void drawing_mode(int mode, BITMAP *pattern, int x_anchor, int y_anchor)
{
    g_draw.mode = mode;
    g_draw.pattern = pattern;
    g_draw.x_anchor = x_anchor;
    g_draw.y_anchor = y_anchor;

    if(pattern)
    {
        g_draw.x_mask = pattern->w - 1;
        g_draw.y_mask = pattern->h - 1;
    }
    else
    {
        g_draw.x_mask = g_draw.y_mask = 0;
    }

    /* pattern modes need a pattern; without one they behave as solid */
    if(!pattern && (mode == DRAW_MODE_COPY_PATTERN || mode == DRAW_MODE_SOLID_PATTERN ||
                    mode == DRAW_MODE_MASKED_PATTERN))
        g_draw.mode = DRAW_MODE_SOLID;
}

void xor_mode(int on)
{
    drawing_mode(on ? DRAW_MODE_XOR : DRAW_MODE_SOLID, NULL, 0, 0);
}

void solid_mode(void)
{
    drawing_mode(DRAW_MODE_SOLID, NULL, 0, 0);
}

void putpixel(BITMAP *bmp, int x, int y, int color)
{
    put_clipped(bmp, x, y, color);
}

void _putpixel(BITMAP *bmp, int x, int y, int color)
{
    /* the underscore version is unclipped and always solid */
    raw_put(bmp, x, y, (uint32_t)color);
}

int getpixel(BITMAP *bmp, int x, int y)
{
    /* outside the bitmap (the clip rect does not matter): -1 */
    if(x < 0 || y < 0 || x >= bmp->w || y >= bmp->h)
        return -1;

    return (int)raw_get(bmp, x, y);
}

void hline(BITMAP *bmp, int x1, int y, int x2, int color)
{
    zcport_hfill(bmp, x1, y, x2, color);
}

void _allegro_hline(BITMAP *bmp, int x1, int y, int x2, int color)
{
    zcport_hfill(bmp, x1, y, x2, color);
}

void vline(BITMAP *bmp, int x, int y1, int y2, int color)
{
    vfill(bmp, x, y1, y2, color);
}

void _allegro_vline(BITMAP *bmp, int x, int y1, int y2, int color)
{
    vfill(bmp, x, y1, y2, color);
}

void rectfill(BITMAP *bmp, int x1, int y1, int x2, int y2, int color)
{
    if(y2 < y1)
    {
        int t = y1;
        y1 = y2;
        y2 = t;
    }

    int saved_clip = bmp->clip;

    if(bmp->clip)
    {
        if(x2 < x1)
        {
            int t = x1;
            x1 = x2;
            x2 = t;
        }

        if(x1 < bmp->cl) x1 = bmp->cl;
        if(x2 >= bmp->cr) x2 = bmp->cr - 1;
        if(x2 < x1) return;
        if(y1 < bmp->ct) y1 = bmp->ct;
        if(y2 >= bmp->cb) y2 = bmp->cb - 1;
        if(y2 < y1) return;

        /* already clipped: skip the per-row clip tests */
        bmp->clip = FALSE;
    }

    for(int y = y1; y <= y2; ++y)
        zcport_hfill(bmp, x1, y, x2, color);

    bmp->clip = saved_clip;
}

} // extern "C"

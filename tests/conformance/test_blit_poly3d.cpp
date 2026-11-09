/*
 * test_blit_poly3d.cpp - 3D polygons (quad3d_f, triangle3d_f, polygon3d_f
 * and the fixed point quad3d / triangle3d / polygon3d) must be pixel-exact
 * against the reference build on 8-bit bitmaps.
 *
 * Covers every POLYTYPE_* that src/script_drawing.cpp can pass (0..14) the
 * way it calls them: Screen->Quad / Triangle (integer corners, z = 0,
 * u/v = 0 or the texture size, one cset color) and Quad3D / Triangle3D
 * (script-supplied z, u, v and per-vertex colors), plus fractional
 * coordinates, tiled textures, clipping and sub-bitmaps.
 */
#include "conf.h"

#include <string.h>

namespace
{

COLOR_MAP g_map;
RGB_MAP g_rgb;

void init_tables()
{
    for(int a = 0; a < PAL_SIZE; ++a)
        for(int b = 0; b < PAL_SIZE; ++b)
            g_map.data[a][b] = (unsigned char)((a * 3 + b * 7 + (a ^ b) + 11) & 0xFF);
    for(int r = 0; r < 32; ++r)
        for(int g = 0; g < 32; ++g)
            for(int b = 0; b < 32; ++b)
                g_rgb.data[r][g][b] = (unsigned char)((r * 5 + g * 3 + b * 9 + (r ^ g ^ b)) & 0xFF);
}

/* A texture of w x h whose texels name their coordinates; every 5th texel
 * is 0 (transparent for the MASK types). */
BITMAP *make_texture(int w, int h)
{
    BITMAP *t = create_bitmap_ex(8, w, h);
    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
            t->line[y][x] = (unsigned char)(((x + y * 3) % 5 == 4) ? 0 : 1 + (x + y * 16) % 254);
    return t;
}

void background(BITMAP *b)
{
    for(int y = 0; y < b->h; ++y)
        for(int x = 0; x < b->w; ++x)
            b->line[y][x] = (unsigned char)((x * 3 + y * 5) & 0xFF);
}

struct Saved
{
    COLOR_MAP *cmap;
    RGB_MAP *rmap;
};

Saved use_tables()
{
    Saved s;
    s.cmap = color_map;
    s.rmap = rgb_map;
    init_tables();
    color_map = &g_map;
    rgb_map = &g_rgb;
    return s;
}

void restore(const Saved &s)
{
    color_map = s.cmap;
    rgb_map = s.rmap;
}

V3D_f vtx(float x, float y, float z, float u, float v, int c)
{
    V3D_f r;
    r.x = x;
    r.y = y;
    r.z = z;
    r.u = u;
    r.v = v;
    r.c = c;
    return r;
}

/* The shapes scripts typically draw (corners in the game's order). */
struct Shape
{
    int x[4], y[4];
};

const Shape kShapes[] =
{
    { { 10, 10, 26, 26 }, { 10, 26, 26, 10 } },    /* 16x16 square, game order */
    { { 4, 4, 36, 36 }, { 5, 37, 37, 5 } },        /* 32x32 */
    { { 20, 8, 20, 32 }, { 4, 20, 36, 20 } },      /* diamond */
    { { 3, 9, 40, 30 }, { 2, 38, 30, 4 } },        /* irregular convex */
    { { 10, 10, 26, 26 }, { 10, 10, 26, 26 } },    /* degenerate (zero area) */
    { { 26, 26, 10, 10 }, { 10, 26, 26, 10 } },    /* mirrored winding */
    { { -8, -8, 30, 30 }, { -6, 25, 25, -6 } },    /* off the top-left */
    { { 30, 20, 50, 60 }, { 30, 50, 60, 35 } },    /* off the bottom-right */
    { { 12, 2, 44, 20 }, { 1, 30, 40, 9 } },       /* rotated rectangle */
    { { 15, 15, 16, 16 }, { 15, 16, 16, 15 } },    /* 1x1 */
};
const int kNumShapes = 10;

} // namespace

CONF_TEST(quad3d_script_quad)
{
    /* Screen->Quad: z = 0, u/v spanning the whole texture. */
    Saved saved = use_tables();
    BITMAP *dst = create_bitmap_ex(8, 48, 48);
    const int tsz[][2] = { {16, 16}, {32, 16}, {16, 32}, {64, 64} };
    for(int type = 0; type <= 14; ++type)
    {
        conf::Seq s(ctx, conf::nm("type%d", type));
        for(int ti = 0; ti < 4; ++ti)
        {
            BITMAP *tex = make_texture(tsz[ti][0], tsz[ti][1]);
            float tw = (float)tsz[ti][0], th = (float)tsz[ti][1];
            for(int si = 0; si < kNumShapes; ++si)
            {
                const Shape &sh = kShapes[si];
                int color = (si * 37 + ti * 11 + type) & 0xFF;
                V3D_f v1 = vtx((float)sh.x[0], (float)sh.y[0], 0, 0, 0, color);
                V3D_f v2 = vtx((float)sh.x[1], (float)sh.y[1], 0, 0, th, color);
                V3D_f v3 = vtx((float)sh.x[2], (float)sh.y[2], 0, tw, th, color);
                V3D_f v4 = vtx((float)sh.x[3], (float)sh.y[3], 0, tw, 0, color);
                background(dst);
                quad3d_f(dst, type, tex, &v1, &v2, &v3, &v4);
                s.add_bitmap(dst, "tex %dx%d shape %d", tsz[ti][0], tsz[ti][1], si);
                background(dst);
                triangle3d_f(dst, type, tex, &v1, &v2, &v3);
                s.add_bitmap(dst, "triangle tex %dx%d shape %d", tsz[ti][0], tsz[ti][1], si);
            }
            destroy_bitmap(tex);
        }
    }
    destroy_bitmap(dst);
    restore(saved);
}

CONF_TEST(quad3d_script_3d)
{
    /* Screen->Quad3D / Triangle3D: integer positions with z, u, v and four
     * colors from the script. */
    Saved saved = use_tables();
    BITMAP *dst = create_bitmap_ex(8, 64, 64);
    BITMAP *tex = make_texture(32, 32);
    for(int type = 0; type <= 14; ++type)
    {
        conf::Seq s(ctx, conf::nm("type%d", type));
        for(int i = 0; i < 120; ++i)
        {
            V3D_f v[4];
            for(int k = 0; k < 4; ++k)
            {
                int z;
                switch(ctx.rng.range(0, 3))
                {
                case 0: z = 0; break;
                case 1: z = ctx.rng.range(1, 10); break;
                case 2: z = ctx.rng.range(-50, 50); break;
                default: z = ctx.rng.range(100, 2000); break;
                }
                /* drawn in the order the 32-bit MSVC build evaluated the
                 * vtx() arguments (C++ leaves it open; x64 differs) */
                const int c = (type == POLYTYPE_GRGB) ? (int)(ctx.rng.next() & 0xFFFFFF) : ctx.rng.range(0, 255);
                const float x = (float)ctx.rng.range(-20, 80);
                const float y = (float)ctx.rng.range(-20, 80);
                const float tu = (float)ctx.rng.range(-40, 80);
                const float tv = (float)ctx.rng.range(-40, 80);
                v[k] = vtx(x, y, (float)z, tu, tv, c);
            }
            background(dst);
            if(i % 2)
                quad3d_f(dst, type, tex, &v[0], &v[1], &v[2], &v[3]);
            else
                triangle3d_f(dst, type, tex, &v[0], &v[1], &v[2]);
            s.add_bitmap(dst, "i=%d %s (%g,%g,%g) (%g,%g,%g) (%g,%g,%g) (%g,%g,%g)", i, (i % 2) ? "quad" : "tri",
                         v[0].x, v[0].y, v[0].z, v[1].x, v[1].y, v[1].z, v[2].x, v[2].y, v[2].z, v[3].x, v[3].y, v[3].z);
        }
    }
    destroy_bitmap(tex);
    destroy_bitmap(dst);
    restore(saved);
}

CONF_TEST(poly3d_float_random)
{
    /* Fractional positions, positive z, clipping and sub-bitmaps. */
    Saved saved = use_tables();
    BITMAP *root = create_bitmap_ex(8, 70, 60);
    BITMAP *tex = make_texture(16, 32);
    for(int type = 0; type <= 14; ++type)
    {
        conf::Seq s(ctx, conf::nm("type%d", type));
        for(int i = 0; i < 100; ++i)
        {
            BITMAP *dst = root;
            if(i % 3 == 0)
                dst = create_sub_bitmap(root, 3, 4, 60, 50);
            /* The random values are drawn in a fixed order, the one the
             * 32-bit MSVC build used for these calls (clip rect: last
             * argument first; vertex: the color, then x, y, z, u, v). C++
             * leaves the order of argument evaluation open and the x64
             * compiler picks another one, which changed the inputs. */
            if(i % 4 == 1)
            {
                const int cb = ctx.rng.range(30, 49), cr = ctx.rng.range(30, 59);
                const int ct = ctx.rng.range(0, 20), cl = ctx.rng.range(0, 20);
                set_clip_rect(dst, cl, ct, cr, cb);
            }
            V3D_f v[5];
            V3D_f *pv[5];
            for(int k = 0; k < 5; ++k)
            {
                const int c = (type == POLYTYPE_GRGB) ? (int)(ctx.rng.next() & 0xFFFFFF) : ctx.rng.range(0, 255);
                const float x = ctx.rng.range(-2000, 8000) / 100.0f;
                const float y = ctx.rng.range(-2000, 7000) / 100.0f;
                const float z = ctx.rng.range(100, 5000) / 100.0f;
                const float tu = ctx.rng.range(-3000, 6000) / 100.0f;
                const float tv = ctx.rng.range(-3000, 6000) / 100.0f;
                v[k] = vtx(x, y, z, tu, tv, c);
                pv[k] = &v[k];
            }
            background(root);
            switch(i % 3)
            {
            case 0: quad3d_f(dst, type, tex, &v[0], &v[1], &v[2], &v[3]); break;
            case 1: triangle3d_f(dst, type, tex, &v[0], &v[1], &v[2]); break;
            default: polygon3d_f(dst, type, tex, 3 + i % 3, pv); break;
            }
            s.add_bitmap(root, "i=%d", i);
            if(dst != root)
                destroy_bitmap(dst);
            else
                set_clip_rect(root, 0, 0, root->w - 1, root->h - 1);
        }
    }
    destroy_bitmap(tex);
    destroy_bitmap(root);
    restore(saved);
}

CONF_TEST(poly3d_fixed_random)
{
    /* The fixed point entry points. */
    Saved saved = use_tables();
    BITMAP *dst = create_bitmap_ex(8, 64, 64);
    BITMAP *tex = make_texture(32, 16);
    for(int type = 0; type <= 14; ++type)
    {
        conf::Seq s(ctx, conf::nm("type%d", type));
        for(int i = 0; i < 60; ++i)
        {
            V3D v[4];
            V3D *pv[4];
            for(int k = 0; k < 4; ++k)
            {
                v[k].x = ctx.rng.range(-10 << 16, 70 << 16);
                v[k].y = ctx.rng.range(-10 << 16, 70 << 16);
                v[k].z = ctx.rng.range(1 << 16, 40 << 16);
                v[k].u = ctx.rng.range(-20 << 16, 50 << 16);
                v[k].v = ctx.rng.range(-20 << 16, 50 << 16);
                v[k].c = (type == POLYTYPE_GRGB) ? (int)(ctx.rng.next() & 0xFFFFFF) : ctx.rng.range(0, 255);
                pv[k] = &v[k];
            }
            background(dst);
            switch(i % 3)
            {
            case 0: quad3d(dst, type, tex, &v[0], &v[1], &v[2], &v[3]); break;
            case 1: triangle3d(dst, type, tex, &v[0], &v[1], &v[2]); break;
            default: polygon3d(dst, type, tex, 4, pv); break;
            }
            s.add_bitmap(dst, "i=%d", i);
        }
    }
    destroy_bitmap(tex);
    destroy_bitmap(dst);
    restore(saved);
}

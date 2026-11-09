/*
 * test_prim_core.cpp - drawing primitives and drawing modes must be
 * pixel-exact against the reference build.
 *
 * Covers putpixel/getpixel, hline/vline/line/fastline, rect/rectfill,
 * circle/circlefill, ellipse/ellipsefill, arc, triangle, polygon, floodfill,
 * spline/calc_spline, the do_line/do_circle/do_ellipse/do_arc point streams,
 * all six drawing modes (translucency through a fixed COLOR_MAP), clipping,
 * sub-bitmaps, and the other color depths.
 */
#include "conf.h"

#include <string.h>

namespace
{

const int W = 64, H = 48;

/* Random coordinate: mostly on the bitmap, sometimes off it or huge. */
int coord(conf::Rng &rng, int size)
{
    switch(rng.range(0, 9))
    {
    case 0: return rng.range(-size, 2 * size);
    case 1: return rng.range(-3, 3);
    case 2: return size - 1 + rng.range(-2, 2);
    case 3: return rng.range(-2000, 2000);   /* far off the bitmap; larger values overflow
                                             * 16.16 math inside the reference (UB) */
    default: return rng.range(0, size - 1);
    }
}

int radius(conf::Rng &rng)
{
    switch(rng.range(0, 7))
    {
    case 0: return 0;   /* negative radii are undefined behavior in the reference (it can crash) */
    case 1: return rng.range(60, 200);
    default: return rng.range(1, 30);
    }
}

/* A deterministic "translucency" table: not symmetric on purpose. */
COLOR_MAP g_map;
void init_map()
{
    for(int a = 0; a < PAL_SIZE; ++a)
        for(int b = 0; b < PAL_SIZE; ++b)
            g_map.data[a][b] = (unsigned char)((a * 7 + b * 3 + (a ^ b)) & 0xFF);
}

BITMAP *make_pattern(int w, int h, int salt)
{
    BITMAP *p = create_bitmap_ex(8, w, h);
    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
            p->line[y][x] = (unsigned char)(((x * 5 + y * 11 + salt) % 7 == 0) ? 0 : (x * 13 + y * 29 + salt) & 0xFF);
    return p;
}

/* Fills the bitmap with a busy background so xor/trans/masked modes show. */
void background(BITMAP *b, int salt)
{
    for(int y = 0; y < b->h; ++y)
        for(int x = 0; x < b->w; ++x)
            b->line[y][x] = (unsigned char)((x * 3 + y * 5 + salt) & 0xFF);
}

enum Prim
{
    P_PUTPIXEL, P_HLINE, P_VLINE, P_LINE, P_FASTLINE, P_RECT, P_RECTFILL,
    P_CIRCLE, P_CIRCLEFILL, P_ELLIPSE, P_ELLIPSEFILL, P_ARC, P_TRIANGLE,
    P_POLYGON, P_SPLINE, P_COUNT
};

const char *const kPrimName[P_COUNT] =
{
    "putpixel", "hline", "vline", "line", "fastline", "rect", "rectfill",
    "circle", "circlefill", "ellipse", "ellipsefill", "arc", "triangle",
    "polygon", "spline"
};

/* Draws one random instance of primitive p on b; returns a short label. */
void draw_random(conf::Rng &rng, BITMAP *b, int p, int color)
{
    int w = b->w, h = b->h;
    switch(p)
    {
    case P_PUTPIXEL: putpixel(b, coord(rng, w), coord(rng, h), color); break;
    case P_HLINE:    hline(b, coord(rng, w), coord(rng, h), coord(rng, w), color); break;
    case P_VLINE:    vline(b, coord(rng, w), coord(rng, h), coord(rng, h), color); break;
    case P_LINE:     line(b, coord(rng, w), coord(rng, h), coord(rng, w), coord(rng, h), color); break;
    case P_FASTLINE: fastline(b, coord(rng, w), coord(rng, h), coord(rng, w), coord(rng, h), color); break;
    case P_RECT:     rect(b, coord(rng, w), coord(rng, h), coord(rng, w), coord(rng, h), color); break;
    case P_RECTFILL: rectfill(b, coord(rng, w), coord(rng, h), coord(rng, w), coord(rng, h), color); break;
    case P_CIRCLE:   circle(b, coord(rng, w), coord(rng, h), radius(rng), color); break;
    case P_CIRCLEFILL: circlefill(b, coord(rng, w), coord(rng, h), radius(rng), color); break;
    case P_ELLIPSE:  ellipse(b, coord(rng, w), coord(rng, h), radius(rng), radius(rng), color); break;
    case P_ELLIPSEFILL: ellipsefill(b, coord(rng, w), coord(rng, h), radius(rng), radius(rng), color); break;
    case P_ARC:
    {
        fixed a1 = (fixed)(rng.next() % 0x2000000) - 0x1000000;
        fixed a2 = (fixed)(rng.next() % 0x2000000) - 0x1000000;
        if(rng.range(0, 4) == 0) a2 = a1;
        arc(b, coord(rng, w), coord(rng, h), a1, a2, radius(rng), color);
        break;
    }
    case P_TRIANGLE:
        triangle(b, coord(rng, w), coord(rng, h), coord(rng, w), coord(rng, h), coord(rng, w), coord(rng, h), color);
        break;
    case P_POLYGON:
    {
        int n = rng.range(3, 9), pts[18];
        for(int i = 0; i < n; ++i)
        {
            /* keep polygons within sane ranges: Allegro's edge tables are int based */
            pts[i * 2] = rng.range(-20, w + 20);
            pts[i * 2 + 1] = rng.range(-20, h + 20);
        }
        polygon(b, n, pts, color);
        break;
    }
    case P_SPLINE:
    {
        int pts[8];
        for(int i = 0; i < 8; i += 2)
        {
            pts[i] = rng.range(-30, w + 30);
            pts[i + 1] = rng.range(-30, h + 30);
        }
        spline(b, pts, color);
        break;
    }
    }
}

void run_mode(conf::Ctx &ctx, const char *mode_name, int mode, BITMAP *pattern, int ax, int ay)
{
    BITMAP *b = create_bitmap_ex(8, W, H);
    for(int p = 0; p < P_COUNT; ++p)
    {
        char name[64];
        sprintf(name, "%s_%s", mode_name, kPrimName[p]);
        conf::Seq s(ctx, name);
        background(b, p);
        drawing_mode(mode, pattern, ax, ay);
        int count = (p == P_PUTPIXEL) ? 400 : 120;
        for(int i = 0; i < count; ++i)
        {
            int color = ctx.rng.range(0, 255);
            draw_random(ctx.rng, b, p, color);
            if(p != P_PUTPIXEL || (i % 20) == 19)
                s.add_bitmap(b, "%d", i);
        }
        solid_mode();
        s.end();
    }
    destroy_bitmap(b);
}

/* ---- point streams of the do_* helpers -------------------------------------- */

conf::Seq *g_points = 0;
void record_point(BITMAP *, int x, int y, int d)
{
    char buf[64];
    sprintf(buf, "%d,%d,%d", x, y, d);
    g_points->add_str(buf);
}

} // namespace

CONF_TEST(prim_point_streams)
{
    BITMAP *b = create_bitmap_ex(8, W, H);

    {
        conf::Seq s(ctx, "do_line");
        g_points = &s;
        for(int i = 0; i < 400; ++i)
        {
            int x1 = ctx.rng.range(-50, 100), y1 = ctx.rng.range(-50, 100);
            int x2 = ctx.rng.range(-50, 100), y2 = ctx.rng.range(-50, 100);
            s.add_str("--");
            do_line(b, x1, y1, x2, y2, i, record_point);
        }
        s.end();
    }
    {
        conf::Seq s(ctx, "do_circle");
        g_points = &s;
        for(int r = 0; r <= 40; ++r)
        {
            s.add_str("--");
            do_circle(b, 10, 20, r, r, record_point);
        }
        s.end();
    }
    {
        conf::Seq s(ctx, "do_ellipse");
        g_points = &s;
        for(int i = 0; i < 300; ++i)
        {
            int rx = ctx.rng.range(0, 40), ry = ctx.rng.range(0, 40);
            s.add_str("--");
            do_ellipse(b, 30, 20, rx, ry, i, record_point);
        }
        s.end();
    }
    {
        conf::Seq s(ctx, "do_arc");
        g_points = &s;
        for(int i = 0; i < 300; ++i)
        {
            fixed a1 = (fixed)(ctx.rng.next() % 0x3000000) - 0x1800000;
            fixed a2 = (fixed)(ctx.rng.next() % 0x3000000) - 0x1800000;
            if(i % 10 == 0) a2 = a1;
            if(i % 10 == 1) a2 = a1 + itofix(256);
            s.add_str("--");
            do_arc(b, 30, 20, a1, a2, ctx.rng.range(0, 35), i, record_point);
        }
        s.end();
    }
    {
        conf::Seq s(ctx, "calc_spline");
        for(int i = 0; i < 200; ++i)
        {
            int pts[8], xs[64], ys[64];
            for(int k = 0; k < 8; ++k)
                pts[k] = ctx.rng.range(-100, 200);
            int n = ctx.rng.range(2, 64);
            calc_spline(pts, n, xs, ys);
            for(int k = 0; k < n; ++k)
                s.add_int(xs[k] * 100000 + ys[k], "%d/%d", i, k);
        }
        s.end();
    }
    g_points = 0;
    destroy_bitmap(b);
}

CONF_TEST(prim_solid)
{
    run_mode(ctx, "solid", DRAW_MODE_SOLID, NULL, 0, 0);
}

CONF_TEST(prim_xor)
{
    run_mode(ctx, "xor", DRAW_MODE_XOR, NULL, 0, 0);
}

CONF_TEST(prim_trans)
{
    init_map();
    COLOR_MAP *old = color_map;
    color_map = &g_map;
    run_mode(ctx, "trans", DRAW_MODE_TRANS, NULL, 0, 0);
    color_map = old;
}

CONF_TEST(prim_patterns)
{
    BITMAP *p8 = make_pattern(8, 8, 1);
    BITMAP *p16x4 = make_pattern(16, 4, 7);
    run_mode(ctx, "copypat8", DRAW_MODE_COPY_PATTERN, p8, 3, 5);
    run_mode(ctx, "solidpat8", DRAW_MODE_SOLID_PATTERN, p8, 0, 0);
    run_mode(ctx, "maskedpat16x4", DRAW_MODE_MASKED_PATTERN, p16x4, 2, 9);
    run_mode(ctx, "copypat16x4", DRAW_MODE_COPY_PATTERN, p16x4, 11, 3);
    destroy_bitmap(p8);
    destroy_bitmap(p16x4);
}

CONF_TEST(prim_clipping)
{
    BITMAP *parent = create_bitmap_ex(8, 80, 60);
    BITMAP *sub = create_sub_bitmap(parent, 9, 7, 50, 37);
    for(int p = 0; p < P_COUNT; ++p)
    {
        char name[64];
        sprintf(name, "clip_%s", kPrimName[p]);
        conf::Seq s(ctx, name);
        background(parent, p * 3);
        for(int i = 0; i < 80; ++i)
        {
            BITMAP *target = (i & 1) ? sub : parent;
            /* clip rects may extend past the bitmap, but must not be reversed
             * (reversed rects are undefined behavior in the reference) */
            int x1 = ctx.rng.range(-5, target->w + 5), y1 = ctx.rng.range(-5, target->h + 5);
            int x2 = ctx.rng.range(-5, target->w + 5), y2 = ctx.rng.range(-5, target->h + 5);
            if(x2 < x1) { int t = x1; x1 = x2; x2 = t; }
            if(y2 < y1) { int t = y1; y1 = y2; y2 = t; }
            set_clip_rect(target, x1, y1, x2, y2);
            draw_random(ctx.rng, target, p, ctx.rng.range(0, 255));
            s.add_bitmap(parent, "%d", i);
            set_clip_rect(target, 0, 0, target->w - 1, target->h - 1);
        }
        s.end();
    }
    {
        /* getpixel outside, inside, on sub-bitmaps and with clipping */
        conf::Seq s(ctx, "getpixel");
        background(parent, 5);
        set_clip_rect(sub, 4, 4, 20, 20);
        for(int y = -3; y < 64; y += 2)
            for(int x = -3; x < 84; x += 3)
            {
                s.add_int(getpixel(parent, x, y));
                s.add_int(getpixel(sub, x, y));
            }
        s.end();
    }
    destroy_bitmap(sub);
    destroy_bitmap(parent);
}

CONF_TEST(prim_floodfill)
{
    BITMAP *b = create_bitmap_ex(8, W, H);
    conf::Seq s(ctx, "floodfill");
    for(int i = 0; i < 150; ++i)
    {
        clear_to_color(b, 0);
        /* a maze of random shapes in two colors to fill around */
        for(int k = 0; k < 12; ++k)
            draw_random(ctx.rng, b, ctx.rng.range(P_LINE, P_TRIANGLE), ctx.rng.range(1, 2));
        if(i % 3 == 1)
        {
            init_map();
            color_map = &g_map;
            drawing_mode(DRAW_MODE_TRANS, NULL, 0, 0);
        }
        /* seed inside the bitmap: outside seeds are undefined behavior in the reference */
        floodfill(b, ctx.rng.range(0, W - 1), ctx.rng.range(0, H - 1), ctx.rng.range(0, 255));
        solid_mode();
        s.add_bitmap(b, "%d", i);
    }
    s.end();
    destroy_bitmap(b);
}

CONF_TEST(prim_depths)
{
    const int depths[] = { 15, 16, 24, 32 };
    for(int d = 0; d < 4; ++d)
    {
        BITMAP *b = create_bitmap_ex(depths[d], 40, 30);
        char name[32];
        sprintf(name, "depth%d", depths[d]);
        if(!b)
        {
            /* the reference needs a graphics system for non-8-bit bitmaps;
             * run this test with --system auto */
            ctx.check_str(name, "create_bitmap_ex failed");
            continue;
        }
        conf::Seq s(ctx, name);
        clear_to_color(b, 0);
        for(int p = 0; p < P_COUNT; ++p)
            for(int i = 0; i < 20; ++i)
            {
                int color = (int)(ctx.rng.next() & ((depths[d] == 15) ? 0x7FFF : (depths[d] == 16) ? 0xFFFF : 0xFFFFFF));
                draw_random(ctx.rng, b, p, color);
                s.add_bitmap(b, "%s %d", kPrimName[p], i);
            }
        /* xor in hicolor/truecolor */
        drawing_mode(DRAW_MODE_XOR, NULL, 0, 0);
        for(int i = 0; i < 20; ++i)
        {
            draw_random(ctx.rng, b, P_RECTFILL, (int)(ctx.rng.next() & 0x7FFF));
            s.add_bitmap(b, "xor %d", i);
        }
        solid_mode();
        for(int y = 0; y < 30; y += 7)
            for(int x = 0; x < 40; x += 5)
                s.add_int(getpixel(b, x, y), "get %d,%d", x, y);
        s.end();
        destroy_bitmap(b);
    }
}

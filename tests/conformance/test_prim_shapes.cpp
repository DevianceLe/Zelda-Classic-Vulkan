/*
 * test_prim_shapes.cpp - the edge cases of the drawing primitives that the
 * random tests in test_prim_core rarely hit, and the variants the player
 * uses (script drawing, transitions, jwin widgets):
 *
 *   - do_arc at and next to every quadrant boundary, zero and tiny radii,
 *     full laps, fine sweeps of the end angle, script-style arcs;
 *   - do_ellipse for every small radius pair and large radii, ellipse /
 *     ellipsefill / circle / circlefill at transition-wipe sizes;
 *   - polygon rasterization rules: single edges of every slope, polygons
 *     with horizontal edges, repeated and collinear vertices, 1 and 2
 *     vertices, large coordinates;
 *   - spline point counts and joints, fastline end point clipping;
 *   - every pixel's overdraw count, with a counting translucency table;
 *   - _allegro_hline / _allegro_vline / hline / vline with clipping and
 *     sub-bitmaps, 1x1 and 2x2 patterns (the jwin scroll bars), patterns
 *     on hicolor/truecolor bitmaps, floodfill on noise, clipped and in
 *     every mode.
 *
 * Inputs stay inside what the reference defines (see the conformance
 * README): no negative radii, no reversed clip rectangles, flood seeds on
 * the bitmap, spline control polygons at least 3 pixels long.
 */
#include "conf.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>

namespace
{

/* do_* point stream recorder */
conf::Seq *g_seq = 0;
std::string g_pts;
void rec_point(BITMAP *, int x, int y, int)
{
    char b[32];
    sprintf(b, "%d,%d;", x, y);
    g_pts += b;
}

/* counting translucency table: every draw adds one */
COLOR_MAP g_count_map;
void init_count_map()
{
    for(int a = 0; a < PAL_SIZE; ++a)
        for(int c = 0; c < PAL_SIZE; ++c)
            g_count_map.data[a][c] = (unsigned char)(c + 1);
}

struct CountMode
{
    COLOR_MAP *old;
    CountMode()
    {
        init_count_map();
        old = color_map;
        color_map = &g_count_map;
        drawing_mode(DRAW_MODE_TRANS, NULL, 0, 0);
    }
    ~CountMode()
    {
        solid_mode();
        color_map = old;
    }
};

void background(BITMAP *b, int salt)
{
    for(int y = 0; y < b->h; ++y)
        for(int x = 0; x < b->w; ++x)
            b->line[y][x] = (unsigned char)((x * 3 + y * 5 + salt) & 0xFF);
}

} // namespace

/* ---- arcs ------------------------------------------------------------------ */

CONF_TEST(arc_boundaries)
{
    /* every pair of angles at, one unit beside and half a degree beside the
     * four axes, plus unnormalised angles */
    fixed angs[40];
    int na = 0;
    const int d[] = { -0x8000, -2, -1, 0, 1, 2, 0x8000 };
    for(int k = 0; k < 4; ++k)
        for(int j = 0; j < 7; ++j)
            angs[na++] = (itofix(k * 64) + d[j]) & 0xFFFFFF;
    angs[na++] = itofix(256);
    angs[na++] = -itofix(64);
    angs[na++] = itofix(300);
    angs[na++] = itofix(-1000) + 12345;

    const int radii[] = { 0, 1, 2, 3, 5, 20 };
    for(int ri = 0; ri < 6; ++ri)
    {
        conf::Seq s(ctx, conf::nm("r%d", radii[ri]));
        for(int i = 0; i < na; ++i)
            for(int j = 0; j < na; ++j)
            {
                g_pts.clear();
                do_arc(NULL, 0, 0, angs[i], angs[j], radii[ri], 0, rec_point);
                s.add_str(g_pts.c_str(), "a1=%d a2=%d", (int)angs[i], (int)angs[j]);
            }
        s.end();
    }
}

CONF_TEST(arc_sweeps)
{
    /* fixed start, end angle in 1/8 steps around the whole circle: every
     * end point and every lap boundary */
    const int radii[] = { 0, 1, 7, 20 };
    for(int ri = 0; ri < 4; ++ri)
    {
        conf::Seq s(ctx, conf::nm("r%d", radii[ri]));
        for(int a1i = 0; a1i < 4; ++a1i)
        {
            fixed a1 = itofix(a1i * 37 + 5) + a1i * 1234;
            for(int st = 0; st < 2048; ++st)
            {
                fixed a2 = st * 0x2000;
                g_pts.clear();
                do_arc(NULL, 0, 0, a1, a2, radii[ri], 0, rec_point);
                s.add_str(g_pts.c_str(), "a1=%d a2=%d", (int)a1, (int)a2);
            }
        }
        s.end();
    }
}

CONF_TEST(arc_grid)
{
    /* 40 x 40 start/end angles, radii 0..35 */
    conf::Seq s(ctx, "grid");
    for(int r = 0; r <= 35; r += (r < 6) ? 1 : 7)
        for(int i = 0; i < 40; ++i)
            for(int j = 0; j < 40; ++j)
            {
                fixed a1 = (i < 32) ? itofix(i * 8) + 0x4CCC : itofix((i - 32) * 32);
                fixed a2 = (j < 32) ? itofix(j * 8) + 0x4CCC : itofix((j - 32) * 32);
                g_pts.clear();
                do_arc(NULL, 0, 0, a1, a2, r, 0, rec_point);
                s.add_str(g_pts.c_str(), "r=%d a1=%d a2=%d", r, (int)a1, (int)a2);
            }
    s.end();
}

CONF_TEST(arc_script_style)
{
    /* what Screen->Arc does: degrees converted to binary angles, arcs and
     * pie slices (arc + two radii + floodfill), translucent and solid,
     * clipped against the bitmap edges; and the jwin radio button arc */
    BITMAP *b = create_bitmap_ex(8, 80, 60);
    conf::Seq s(ctx, "script");
    for(int i = 0; i < 160; ++i)
    {
        int deg1 = ctx.rng.range(-360, 720), deg2 = ctx.rng.range(-360, 720);
        if(i % 4 == 0) { deg1 = (i / 4 % 8) * 45; deg2 = deg1 + 90 * (1 + i % 3); }
        fixed sa = (itofix(deg1) / 360) * 256;
        fixed ea = (itofix(deg2) / 360) * 256;
        int r = ctx.rng.range(0, 45);
        int cx = ctx.rng.range(-10, 90), cy = ctx.rng.range(-10, 70);
        int color = ctx.rng.range(1, 255);
        background(b, i);
        if(i % 3 == 1)
        {
            CountMode m;
            arc(b, cx, cy, sa, ea, r, color);
        }
        else
            arc(b, cx, cy, sa, ea, r, color);
        s.add_bitmap(b, "deg %d..%d r=%d at %d,%d", deg1, deg2, r, cx, cy);

        if(i % 5 == 0 && cx >= 0 && cy >= 0 && cx < 80 && cy < 60)
        {
            clear_to_color(b, 0);
            arc(b, cx, cy, sa, ea, r, color);
            line(b, cx, cy, cx + fixtoi(fixcos(-sa) * r), cy + fixtoi(fixsin(-sa) * r), color);
            line(b, cx, cy, cx + fixtoi(fixcos(-ea) * r), cy + fixtoi(fixsin(-ea) * r), color);
            int fx = cx + fixtoi(fixcos(-(ea + sa) / 2) * r / 2);
            int fy = cy + fixtoi(fixsin(-(ea + sa) / 2) * r / 2);
            if(fx >= 0 && fy >= 0 && fx < 80 && fy < 60)
                floodfill(b, fx, fy, color);
            s.add_bitmap(b, "pie %d..%d r=%d", deg1, deg2, r);
        }
    }
    for(int r = 0; r < 12; ++r)
    {
        clear_to_color(b, 0);
        circlefill(b, 20, 20 + r, r, 3);
        arc(b, 20, 20 + r, itofix(32), itofix(160), r, 5);
        circlefill(b, 20, 20 + r, r - 1 < 0 ? 0 : r - 1, 4);
        arc(b, 20, 20 + r, itofix(32), itofix(160), r - 1 < 0 ? 0 : r - 1, 6);
        s.add_bitmap(b, "jwin radio r=%d", r);
    }
    s.end();
    destroy_bitmap(b);
}

/* ---- ellipses and circles ------------------------------------------------------- */

CONF_TEST(ellipse_streams)
{
    conf::Seq s(ctx, "do_ellipse");
    for(int rx = 0; rx <= 45; ++rx)
        for(int ry = 0; ry <= 45; ++ry)
        {
            g_pts.clear();
            do_ellipse(NULL, 0, 0, rx, ry, 0, rec_point);
            s.add_str(g_pts.c_str(), "rx=%d ry=%d", rx, ry);
        }
    const int big[][2] = { {100, 3}, {3, 100}, {200, 150}, {150, 200}, {300, 1}, {1, 300},
                           {500, 499}, {64, 128}, {1000, 7}, {800, 700} };
    /* (radius pairs like 1000 x 700 and larger lose precision inside the
     * reference: the port differs there by a few pixels; not compared) */
    for(int i = 0; i < 10; ++i)
    {
        g_pts.clear();
        do_ellipse(NULL, 0, 0, big[i][0], big[i][1], 0, rec_point);
        s.add_str(g_pts.c_str(), "rx=%d ry=%d", big[i][0], big[i][1]);
    }
    s.end();
}

CONF_TEST(wipe_shapes)
{
    /* the transition wipes: black_opening() ellipses (radii multiples of
     * 8), circles and triangles of up to several hundred pixels on a
     * 320x240 bitmap */
    BITMAP *b = create_bitmap_ex(8, 320, 240);
    conf::Seq s(ctx, "wipes");
    for(int i = 0; i < 48; ++i)
    {
        int x = ctx.rng.range(-40, 360), y = ctx.rng.range(-40, 280);
        clear_to_color(b, 15);
        switch(i % 4)
        {
        case 0:
            ellipsefill(b, x, y, ctx.rng.range(0, 70) * 8, ctx.rng.range(0, 40) * 8, 0);
            break;
        case 1:
            circlefill(b, x, y, ctx.rng.range(0, 700), 0);
            break;
        case 2:
        {
            double r = ctx.rng.range(0, 700);
            double a = ctx.rng.range(0, 6283) / 1000.0;
            triangle(b, x + (int)(cos(a) * r), y - (int)(sin(a) * r),
                     x + (int)(cos(a + 2.0944) * r), y - (int)(sin(a + 2.0944) * r),
                     x + (int)(cos(a + 4.18879) * r), y - (int)(sin(a + 4.18879) * r), 0);
            break;
        }
        default:
        {
            int w = ctx.rng.range(0, 400);
            circlefill(b, x, y, w, 0);
            circle(b, x, y, w + 2, 0);
            circle(b, x, y, w + 5, 0);
            ellipse(b, x, y, w / 2 + 3, w + 1, 7);
            break;
        }
        }
        s.add_bitmap(b, "%d at %d,%d", i, x, y);
    }
    s.end();
    destroy_bitmap(b);
}

CONF_TEST(overdraw_counts)
{
    /* how many times each pixel is written, for every shape primitive
     * (matters for translucent and XOR drawing) */
    BITMAP *b = create_bitmap_ex(8, 100, 80);
    conf::Seq s(ctx, "counts");
    for(int i = 0; i < 400; ++i)
    {
        clear_to_color(b, 0);
        {
            CountMode m;
            int x = ctx.rng.range(-20, 120), y = ctx.rng.range(-20, 100);
            int r1 = ctx.rng.range(0, 60), r2 = ctx.rng.range(0, 60);
            switch(i % 8)
            {
            case 0: circle(b, x, y, r1, 1); break;
            case 1: circlefill(b, x, y, r1, 1); break;
            case 2: ellipse(b, x, y, r1, r2, 1); break;
            case 3: ellipsefill(b, x, y, r1, r2, 1); break;
            case 4: rect(b, x, y, ctx.rng.range(-20, 120), ctx.rng.range(-20, 100), 1); break;
            case 5: line(b, x, y, ctx.rng.range(-20, 120), ctx.rng.range(-20, 100), 1); break;
            case 6: fastline(b, x, y, ctx.rng.range(-20, 120), ctx.rng.range(-20, 100), 1); break;
            default:
                arc(b, x, y, (fixed)(ctx.rng.next() & 0xFFFFFF), (fixed)(ctx.rng.next() & 0xFFFFFF), r1, 1);
                break;
            }
        }
        s.add_bitmap(b, "%d", i);
    }
    s.end();
    destroy_bitmap(b);
}

/* ---- polygons ------------------------------------------------------------------ */

CONF_TEST(polygon_edges)
{
    /* one edge of every slope as the left and as the right side of a
     * triangle whose third vertex is far off the bitmap */
    BITMAP *b = create_bitmap_ex(8, 260, 40);
    conf::Seq s(ctx, "edges");
    for(int side = 0; side < 2; ++side)
        for(int dy = 1; dy <= 12; ++dy)
            for(int dx = -30; dx <= 30; ++dx)
            {
                clear_to_color(b, 0);
                triangle(b, 130, 5, 130 + dx, 5 + dy, side ? 200 : -200, 5 + dy / 2, 1);
                s.add_bitmap(b, "side=%d dx=%d dy=%d", side, dx, dy);
            }
    s.end();
    destroy_bitmap(b);
}

CONF_TEST(polygon_grid)
{
    /* vertices on a coarse grid: many horizontal edges, shared rows,
     * repeated vertices and self intersections; solid and counted */
    BITMAP *b = create_bitmap_ex(8, 64, 48);
    conf::Seq s(ctx, "grid");
    for(int i = 0; i < 1200; ++i)
    {
        int n = ctx.rng.range(3, 8), p[16];
        int g = (i % 3) + 2;
        for(int k = 0; k < n * 2; k += 2)
        {
            p[k] = 8 + ctx.rng.range(0, 48 / g) * g;
            p[k + 1] = 6 + ctx.rng.range(0, 36 / g) * g;
        }
        clear_to_color(b, 0);
        if(i & 1)
        {
            CountMode m;
            polygon(b, n, p, 1);
        }
        else
            polygon(b, n, p, 1);
        s.add_bitmap(b, "%d n=%d", i, n);
    }
    s.end();
    destroy_bitmap(b);
}

CONF_TEST(polygon_odd)
{
    /* 1 and 2 vertices, collinear and repeated vertices, large coordinates
     * (up to the 14.18 fixed point range of the reference) */
    BITMAP *b = create_bitmap_ex(8, 64, 48);
    conf::Seq s(ctx, "odd");
    for(int i = 0; i < 300; ++i)
    {
        int n, p[16];
        if(i < 100)
        {
            n = 1 + i % 2;
            for(int k = 0; k < n * 2; k += 2) { p[k] = ctx.rng.range(-5, 68); p[k + 1] = ctx.rng.range(-5, 52); }
        }
        else if(i < 200)
        {
            n = 3 + i % 3;
            int x0 = ctx.rng.range(0, 63), y0 = ctx.rng.range(0, 47);
            int dx = ctx.rng.range(-3, 3), dy = ctx.rng.range(-3, 3);
            for(int k = 0; k < n; ++k) { p[k * 2] = x0 + dx * k * (i % 2); p[k * 2 + 1] = y0 + dy * k * (i % 2); }
        }
        else
        {
            n = 3 + i % 2;
            for(int k = 0; k < n * 2; k += 2) { p[k] = 32 + ctx.rng.range(-400, 400) * 10; p[k + 1] = 24 + ctx.rng.range(-400, 400) * 10; }
        }
        clear_to_color(b, 0);
        {
            CountMode m;
            polygon(b, n, p, 1);
        }
        s.add_bitmap(b, "%d n=%d", i, n);
    }
    s.end();
    destroy_bitmap(b);
}

CONF_TEST(polygon_script_quads)
{
    /* Screen->Rectangle with rotation: four rotated corners, filled with
     * polygon(), solid and translucent, on a sub-bitmap with clipping */
    BITMAP *parent = create_bitmap_ex(8, 96, 72);
    BITMAP *b = create_sub_bitmap(parent, 8, 6, 80, 60);
    conf::Seq s(ctx, "quads");
    for(int i = 0; i < 200; ++i)
    {
        int x1 = ctx.rng.range(-20, 90), y1 = ctx.rng.range(-20, 70);
        int x2 = x1 + ctx.rng.range(0, 50), y2 = y1 + ctx.rng.range(0, 40);
        int rx = ctx.rng.range(0, 80), ry = ctx.rng.range(0, 60);
        fixed ra = (itofix(ctx.rng.range(0, 359)) / 360) * 256;
        fixed fc = fixcos(ra), fs = fixsin(ra);
        int xy[8];
        xy[0] = rx + fixtoi(fc * (x1 - rx) - fs * (y1 - ry));
        xy[1] = ry + fixtoi(fs * (x1 - rx) + fc * (y1 - ry));
        xy[2] = rx + fixtoi(fc * (x2 - rx) - fs * (y1 - ry));
        xy[3] = ry + fixtoi(fs * (x2 - rx) + fc * (y1 - ry));
        xy[4] = rx + fixtoi(fc * (x2 - rx) - fs * (y2 - ry));
        xy[5] = ry + fixtoi(fs * (x2 - rx) + fc * (y2 - ry));
        xy[6] = rx + fixtoi(fc * (x1 - rx) - fs * (y2 - ry));
        xy[7] = ry + fixtoi(fs * (x1 - rx) + fc * (y2 - ry));
        background(parent, i);
        if(i % 3 == 0)
            set_clip_rect(b, ctx.rng.range(0, 30), ctx.rng.range(0, 20), ctx.rng.range(40, 85), ctx.rng.range(30, 65));
        if(i % 2)
        {
            CountMode m;
            polygon(b, 4, xy, 9);
        }
        else
            polygon(b, 4, xy, 9);
        set_clip_rect(b, 0, 0, b->w - 1, b->h - 1);
        s.add_bitmap(parent, "%d", i);
    }
    s.end();
    destroy_bitmap(b);
    destroy_bitmap(parent);
}

/* ---- splines and lines ------------------------------------------------------- */

CONF_TEST(spline_shapes)
{
    BITMAP *b = create_bitmap_ex(8, 64, 48);
    conf::Seq s(ctx, "splines");
    for(int i = 0; i < 400; ++i)
    {
        int p[8];
        int sc = (i < 150) ? 1 : (i < 300) ? 3 : (i < 380) ? 8 : 60;
        double len;
        do
        {
            for(int k = 0; k < 8; k += 2)
            {
                p[k] = 32 + ctx.rng.range(-30, 30) * sc / 4;
                p[k + 1] = 24 + ctx.rng.range(-22, 22) * sc / 4;
            }
            len = 0;
            for(int k = 0; k < 3; ++k)
                len += sqrt((double)(p[k * 2 + 2] - p[k * 2]) * (p[k * 2 + 2] - p[k * 2]) +
                            (double)(p[k * 2 + 3] - p[k * 2 + 1]) * (p[k * 2 + 3] - p[k * 2 + 1]));
        }
        while(len < 3.0);   /* shorter control polygons read uninitialized data in the reference */
        clear_to_color(b, 0);
        if(i & 1)
        {
            CountMode m;
            spline(b, p, 1);
        }
        else
            spline(b, p, 1);
        s.add_bitmap(b, "%d", i);
    }
    /* long curves: the 64 point limit */
    const int big[][8] = {
        { -1000, 24, 1100, 10, -1000, 40, 1060, 30 },
        { -400, 24, 500, 10, -400, 40, 460, 30 },
        { -300, 20, 300, 30, -300, 40, 300, 10 },
        { -150, 20, 150, 30, -160, 40, 150, 10 },
        { -5000, 24, 5000, 10, -5000, 40, 5000, 30 },
    };
    for(int i = 0; i < 5; ++i)
    {
        clear_to_color(b, 0);
        {
            CountMode m;
            spline(b, big[i], 1);
        }
        s.add_bitmap(b, "big %d", i);
    }
    s.end();
    destroy_bitmap(b);
}

CONF_TEST(fastline_clip)
{
    BITMAP *b = create_bitmap_ex(8, 64, 48);
    conf::Seq s(ctx, "fastline");
    for(int i = 0; i < 600; ++i)
    {
        int x1 = ctx.rng.range(-80, 140), y1 = ctx.rng.range(-80, 130);
        int x2 = ctx.rng.range(-80, 140), y2 = ctx.rng.range(-80, 130);
        if(i % 4 == 0) { x1 = ctx.rng.range(0, 63); y1 = ctx.rng.range(0, 47); x2 = ctx.rng.range(0, 63); y2 = ctx.rng.range(0, 47); }
        if(i % 7 == 0) { x1 = ctx.rng.range(-3000, 3000); y1 = ctx.rng.range(-3000, 3000); }
        int cl = ctx.rng.range(0, 30), ct = ctx.rng.range(0, 20);
        int cr = ctx.rng.range(cl, 63), cb = ctx.rng.range(ct, 47);
        if(i % 3 == 0) { cl = 0; ct = 0; cr = 63; cb = 47; }
        clear_to_color(b, 0);
        set_clip_rect(b, cl, ct, cr, cb);
        {
            CountMode m;
            fastline(b, x1, y1, x2, y2, 1);
        }
        set_clip_rect(b, 0, 0, 63, 47);
        s.add_bitmap(b, "%d,%d-%d,%d clip %d,%d,%d,%d", x1, y1, x2, y2, cl, ct, cr, cb);
    }
    s.end();
    destroy_bitmap(b);
}

/* ---- spans, patterns, flood fill --------------------------------------------- */

CONF_TEST(spans_and_patterns)
{
    BITMAP *parent = create_bitmap_ex(8, 70, 50);
    BITMAP *sub = create_sub_bitmap(parent, 5, 4, 60, 40);
    BITMAP *pat1 = create_bitmap_ex(8, 1, 1);
    BITMAP *pat2 = create_bitmap_ex(8, 2, 2);
    putpixel(pat1, 0, 0, 77);
    putpixel(pat2, 0, 1, 11);
    putpixel(pat2, 1, 0, 11);
    putpixel(pat2, 0, 0, 22);
    putpixel(pat2, 1, 1, 0);
    COLOR_MAP *old_map = color_map;
    conf::Seq s(ctx, "spans");
    for(int i = 0; i < 600; ++i)
    {
        BITMAP *t = (i & 1) ? sub : parent;
        background(parent, i);
        int x1 = ctx.rng.range(-10, 80), y1 = ctx.rng.range(-10, 60);
        int x2 = ctx.rng.range(-10, 80), y2 = ctx.rng.range(-10, 60);
        if(i % 3 == 0)
        {
            int cx1 = ctx.rng.range(-3, t->w), cy1 = ctx.rng.range(-3, t->h);
            int cx2 = ctx.rng.range(cx1, t->w + 3), cy2 = ctx.rng.range(cy1, t->h + 3);
            set_clip_rect(t, cx1, cy1, cx2, cy2);
        }
        int mode = i % 6;
        switch(mode)
        {
        case 1: drawing_mode(DRAW_MODE_XOR, NULL, 0, 0); break;
        case 2: init_count_map(); color_map = &g_count_map; drawing_mode(DRAW_MODE_TRANS, NULL, 0, 0); break;
        case 3: drawing_mode(DRAW_MODE_COPY_PATTERN, pat2, 0, 0); break;
        case 4: drawing_mode(DRAW_MODE_SOLID_PATTERN, pat2, ctx.rng.range(0, 5), ctx.rng.range(0, 5)); break;
        case 5: drawing_mode(DRAW_MODE_MASKED_PATTERN, (i % 4 < 2) ? pat1 : pat2, 1, 0); break;
        default: break;
        }
        int c = ctx.rng.range(0, 255);
        switch((i / 6) % 6)
        {
        case 0: _allegro_hline(t, x1, y1, x2, c); break;
        case 1: _allegro_vline(t, x1, y1, y2, c); break;
        case 2: hline(t, x1, y1, x2, c); break;
        case 3: vline(t, x1, y1, y2, c); break;
        case 4: rectfill(t, x1, y1, x2, y2, c); break;
        default: rect(t, x1, y1, x2, y2, c); putpixel(t, x2, y1, c); break;
        }
        solid_mode();
        color_map = old_map;
        set_clip_rect(t, 0, 0, t->w - 1, t->h - 1);
        s.add_bitmap(parent, "%d", i);
    }
    s.end();
    destroy_bitmap(pat1);
    destroy_bitmap(pat2);
    destroy_bitmap(sub);
    destroy_bitmap(parent);
}

CONF_TEST(hicolor_patterns)
{
    /* pattern and XOR modes on hicolor / truecolor bitmaps with patterns
     * of the same depth (jwin creates its scroll bar pattern with the
     * screen's depth) */
    const int depths[] = { 15, 16, 24, 32 };
    for(int d = 0; d < 4; ++d)
    {
        BITMAP *b = create_bitmap_ex(depths[d], 40, 30);
        BITMAP *p = create_bitmap_ex(depths[d], 2, 2);
        BITMAP *p4 = create_bitmap_ex(depths[d], 4, 4);
        if(!b || !p || !p4)
        {
            ctx.check_str(conf::nm("depth%d", depths[d]), "create_bitmap_ex failed");
            continue;
        }
        int mask = (depths[d] == 15) ? MASK_COLOR_15 : (depths[d] == 16) ? MASK_COLOR_16 : MASK_COLOR_32;
        int lim = (depths[d] == 15) ? 0x7FFF : (depths[d] == 16) ? 0xFFFF : 0xFFFFFF;
        putpixel(p, 0, 0, 0x1234 & lim);
        putpixel(p, 1, 0, mask);
        putpixel(p, 0, 1, 0x4321 & lim);
        putpixel(p, 1, 1, 0x00FF & lim);
        for(int y = 0; y < 4; ++y)
            for(int x = 0; x < 4; ++x)
                putpixel(p4, x, y, ((x + y) % 3 == 0) ? mask : (int)((ctx.rng.next() & lim)));
        conf::Seq s(ctx, conf::nm("depth%d", depths[d]));
        for(int i = 0; i < 120; ++i)
        {
            clear_to_color(b, (int)(ctx.rng.next() & lim));
            switch(i % 4)
            {
            case 0: drawing_mode(DRAW_MODE_COPY_PATTERN, p, ctx.rng.range(0, 3), ctx.rng.range(0, 3)); break;
            case 1: drawing_mode(DRAW_MODE_SOLID_PATTERN, p4, ctx.rng.range(0, 3), ctx.rng.range(0, 3)); break;
            case 2: drawing_mode(DRAW_MODE_MASKED_PATTERN, p4, 1, 2); break;
            default: drawing_mode(DRAW_MODE_XOR, NULL, 0, 0); break;
            }
            int c = (int)(ctx.rng.next() & lim);
            int x1 = ctx.rng.range(-5, 45), y1 = ctx.rng.range(-5, 35);
            switch((i / 4) % 5)
            {
            case 0: rectfill(b, x1, y1, ctx.rng.range(-5, 45), ctx.rng.range(-5, 35), c); break;
            case 1: circlefill(b, x1, y1, ctx.rng.range(0, 20), c); break;
            case 2: line(b, x1, y1, ctx.rng.range(-5, 45), ctx.rng.range(-5, 35), c); break;
            case 3: { int pts[6] = { x1, y1, ctx.rng.range(-5, 45), ctx.rng.range(-5, 35), ctx.rng.range(-5, 45), ctx.rng.range(-5, 35) };
                      polygon(b, 3, pts, c); break; }
            default: ellipsefill(b, x1, y1, ctx.rng.range(0, 20), ctx.rng.range(0, 15), c); break;
            }
            solid_mode();
            s.add_bitmap(b, "%d", i);
        }
        s.end();
        destroy_bitmap(p4);
        destroy_bitmap(p);
        destroy_bitmap(b);
    }
}

CONF_TEST(floodfill_noise)
{
    /* noise bitmaps: the region is 4-connected, decided by the original
     * colors, clipped; also XOR and counted translucency */
    BITMAP *b = create_bitmap_ex(8, 16, 12);
    COLOR_MAP *old_map = color_map;
    conf::Seq s(ctx, "noise");
    for(int i = 0; i < 300; ++i)
    {
        for(int y = 0; y < 12; ++y)
            for(int x = 0; x < 16; ++x)
                b->line[y][x] = (unsigned char)((ctx.rng.range(0, 2) == 0) ? 1 : 0);
        int cl = 0, ct = 0, cr = 15, cb = 11;
        if(i % 2) { cl = ctx.rng.range(0, 6); ct = ctx.rng.range(0, 5); cr = ctx.rng.range(cl, 15); cb = ctx.rng.range(ct, 11); }
        set_clip_rect(b, cl, ct, cr, cb);
        int sx = ctx.rng.range(0, 15), sy = ctx.rng.range(0, 11);
        if(i % 3 == 1)
            drawing_mode(DRAW_MODE_XOR, NULL, 0, 0);
        else if(i % 3 == 2)
        {
            init_count_map();
            color_map = &g_count_map;
            drawing_mode(DRAW_MODE_TRANS, NULL, 0, 0);
        }
        floodfill(b, sx, sy, 2 + i % 7);
        solid_mode();
        color_map = old_map;
        set_clip_rect(b, 0, 0, 15, 11);
        s.add_bitmap(b, "%d seed %d,%d clip %d,%d,%d,%d", i, sx, sy, cl, ct, cr, cb);
    }
    s.end();

    /* a large open area (deep recursion / long spans) */
    BITMAP *big = create_bitmap_ex(8, 320, 240);
    clear_to_color(big, 0);
    for(int k = 0; k < 40; ++k)
        circle(big, ctx.rng.range(0, 319), ctx.rng.range(0, 239), ctx.rng.range(3, 60), 1);
    floodfill(big, 160, 120, 5);
    ctx.check_bitmap("big", big);
    destroy_bitmap(big);
    destroy_bitmap(b);
}

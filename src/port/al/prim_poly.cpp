/*
 * zcport primitives: floodfill, spline, calc_spline.
 * (polygon and triangle live in prim_polygon.cpp.)
 *
 * Matched to the reference build pixel for pixel by
 * tests/conformance/test_prim_core.
 *
 * Written from scratch for zcport.
 */

#include "prim_internal.h"

#include <math.h>

#include <algorithm>
#include <vector>

using namespace zcprim;

extern "C" {
/* ---- flood fill ------------------------------------------------------------- */

void floodfill(BITMAP *bmp, int x, int y, int color)
{
    int cl = 0, ct = 0, cr = bmp->w, cb = bmp->h;

    if(bmp->clip)
    {
        cl = bmp->cl;
        ct = bmp->ct;
        cr = bmp->cr;
        cb = bmp->cb;
    }

    if(x < cl || x >= cr || y < ct || y >= cb)
        return;

    uint32_t src = raw_get(bmp, x, y);
    int w = cr - cl, h = cb - ct;

    /* 'done' marks pixels already filled, so the region is decided by the
     * original colors even when the drawing mode changes pixels to 'src' */
    std::vector<unsigned char> done((size_t)w * h, 0);
    std::vector<std::pair<int, int> > stack;
    stack.push_back(std::make_pair(x, y));

    int saved_clip = bmp->clip;
    bmp->clip = FALSE;

    while(!stack.empty())
    {
        int sx = stack.back().first, sy = stack.back().second;
        stack.pop_back();

        if(done[(size_t)(sy - ct) * w + (sx - cl)] || raw_get(bmp, sx, sy) != src)
            continue;

        /* extend the run left and right */
        int lx = sx, rx = sx;
        while(lx - 1 >= cl && !done[(size_t)(sy - ct) * w + (lx - 1 - cl)] && raw_get(bmp, lx - 1, sy) == src)
            --lx;
        while(rx + 1 < cr && !done[(size_t)(sy - ct) * w + (rx + 1 - cl)] && raw_get(bmp, rx + 1, sy) == src)
            ++rx;

        for(int i = lx; i <= rx; ++i)
            done[(size_t)(sy - ct) * w + (i - cl)] = 1;

        zcport_hfill(bmp, lx, sy, rx, color);

        /* seed the rows above and below */
        for(int ny = sy - 1; ny <= sy + 1; ny += 2)
        {
            if(ny < ct || ny >= cb)
                continue;

            for(int i = lx; i <= rx; ++i)
                if(!done[(size_t)(ny - ct) * w + (i - cl)] && raw_get(bmp, i, ny) == src)
                    stack.push_back(std::make_pair(i, ny));
        }
    }

    bmp->clip = saved_clip;
    (void)h;
}

/* ---- splines ---------------------------------------------------------------- */

void calc_spline(AL_CONST int points[8], int npts, int *out_x, int *out_y)
{
    /* Cubic Bezier through forward differences: with step h = 1/(n-1) and
     * x(t) = a t^3 + b t^2 + c t + d, the differences are
     * D1 = a h^3 + b h^2 + c h, D2 = 6 a h^3 + 2 b h^2, D3 = 6 a h^3.
     * The loop pre-increments, so start one step "before" t = 0. */
    double h = 1.0 / (npts - 1);
    double h2 = h * h;
    double h3 = h2 * h;

    for(int axis = 0; axis < 2; ++axis)
    {
        int p0 = points[axis], p1 = points[2 + axis], p2 = points[4 + axis], p3 = points[6 + axis];
        int *out = axis ? out_y : out_x;

        double b_term = h2 * (3 * (p2 - 2 * p1 + p0));
        double a_term = h3 * (p3 + 3 * (-p2 + p1) - p0);

        double d3 = 6 * a_term;
        double d2 = -6 * a_term + 2 * b_term;
        double d1 = a_term - b_term + 3 * h * (p1 - p0);
        double v = p0;

        out[0] = p0;
        v += .5;

        for(int i = 1; i < npts; ++i)
        {
            d2 += d3;
            d1 += d2;
            v += d1;
            out[i] = (int)v;
        }
    }
}

namespace
{
/* do_line callback for spline segments: plots every point except the
 * segment's end point, which the next segment starts with. */
struct SegmentSkip
{
    int end_x, end_y;
    bool skip_end;
};
SegmentSkip g_segment;

void spline_point(BITMAP *bmp, int x, int y, int color)
{
    if(g_segment.skip_end && x == g_segment.end_x && y == g_segment.end_y)
        return;
    put_clipped(bmp, x, y, color);
}
}

void spline(BITMAP *bmp, AL_CONST int points[8], int color)
{
    const int kMaxPoints = 64;
    int xs[kMaxPoints], ys[kMaxPoints];

    /* The number of points grows with the square root of the length of the
     * control polygon: (int)(1.2 * sqrt(length)), at most 64. */
    double len = 0;
    for(int i = 0; i < 3; ++i)
    {
        double dx = points[i * 2 + 2] - points[i * 2];
        double dy = points[i * 2 + 3] - points[i * 2 + 1];
        len += sqrt(dx * dx + dy * dy);
    }

    int n = (int)(sqrt(len) * 1.2);
    if(n > kMaxPoints)
        n = kMaxPoints;
    /* Below 2 points (control polygon shorter than ~2.8 pixels) the
     * reference draws one segment to an uninitialized point; draw the
     * single segment between the end points instead. */
    if(n < 2)
        n = 2;

    calc_spline(points, n, xs, ys);

    /* Consecutive segments share their joint; it is plotted once (matters
     * for XOR and translucent modes). */
    for(int i = 1; i < n; ++i)
    {
        g_segment.end_x = xs[i];
        g_segment.end_y = ys[i];
        g_segment.skip_end = (i < n - 1);
        do_line(bmp, xs[i - 1], ys[i - 1], xs[i], ys[i], color, spline_point);
    }
}

} // extern "C"

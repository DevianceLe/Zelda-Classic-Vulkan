/*
 * zcport primitives: polygon() and triangle().
 *
 * Rasterization rule (established by black-box comparison with the
 * reference build, see tests/conformance/test_prim_core and
 * test_prim_shapes):
 *
 *  - Every row from the topmost to the bottommost vertex is scanned. On a
 *    row y each non-horizontal edge contributes the x range it covers
 *    inside the band [y - 1/2, y + 1/2] (clipped to the edge's own rows),
 *    computed in 14.18 fixed point with the slope truncated toward zero.
 *  - Edges that continue each other through a vertex on that row (the
 *    polygon passes through the row there, possibly along horizontal
 *    edges) are joined into one range.
 *  - The ranges are sorted by their left end and taken in pairs; each pair
 *    fills from the leftmost to the rightmost x of both. A left end
 *    rounds halves up, a right end rounds halves down.
 *  - Horizontal edges are filled completely on their row.
 *  - Every pixel of a row is written once (the union of the spans), so
 *    XOR / translucent modes see each pixel once.
 *
 * triangle() is polygon() with three vertices.
 *
 * Written from scratch for zcport.
 */

#include "prim_internal.h"

#include <stdint.h>

#include <algorithm>
#include <vector>

using namespace zcprim;

namespace
{

const int kShift = 18;

/* 32-bit two's complement helpers (the reference computes in int). */
inline int32_t wrap(uint32_t v)
{
    return (int32_t)v;
}

inline int32_t shl(int v)
{
    return wrap((uint32_t)v << kShift);
}

inline int left_pixel(int32_t v)
{
    return (int)(wrap((uint32_t)v + (1u << (kShift - 1))) >> kShift);
}

inline int right_pixel(int32_t v)
{
    return (int)(wrap((uint32_t)v + (1u << (kShift - 1)) - 1u) >> kShift);
}

struct Range
{
    int32_t lo, hi;
};

bool range_less(const Range &a, const Range &b)
{
    if(a.lo != b.lo)
        return a.lo < b.lo;
    return a.hi < b.hi;
}

struct Span
{
    int l, r;
};

bool span_less(const Span &a, const Span &b)
{
    return a.l < b.l;
}

/* The x range edge (ax, ay)-(bx, by) (ay != by) covers on row y, which
 * must lie within its rows. */
Range edge_range(int ax, int ay, int bx, int by, int y)
{
    if(by < ay)
    {
        std::swap(ax, bx);
        std::swap(ay, by);
    }

    int dy = by - ay;
    int32_t dx = shl(bx - ax) / dy;            /* truncates toward zero */
    int32_t half = (int32_t)((dx + 1) >> 1);
    uint32_t x0 = (uint32_t)shl(ax);
    int k = y - ay;

    int32_t lo = (k == 0) ? wrap(x0)
                          : wrap(x0 + (uint32_t)half + (uint32_t)(k - 1) * (uint32_t)dx);
    int32_t hi = (k == dy) ? wrap(x0 + (uint32_t)dy * (uint32_t)dx)
                           : wrap(x0 + (uint32_t)half + (uint32_t)k * (uint32_t)dx);
    Range r;
    r.lo = std::min(lo, hi);
    r.hi = std::max(lo, hi);
    return r;
}

} // namespace

extern "C" {

void polygon(BITMAP *bmp, int vertices, AL_CONST int *points, int color)
{
    if(vertices < 1)
        return;

    const int n = vertices;
#define PX(i) points[(i) * 2]
#define PY(i) points[(i) * 2 + 1]

    int top = PY(0), bottom = PY(0);
    for(int i = 1; i < n; ++i)
    {
        top = std::min(top, PY(i));
        bottom = std::max(bottom, PY(i));
    }

    if(bmp->clip)
    {
        top = std::max(top, bmp->ct);
        bottom = std::min(bottom, bmp->cb - 1);
    }

    /* edge i runs from vertex i to vertex i + 1 */
    std::vector<char> active(n), used(n);
    std::vector<Range> ranges, row_ranges;
    std::vector<Span> spans;
    ranges.resize(n);

    for(int y = top; y <= bottom; ++y)
    {
        for(int i = 0; i < n; ++i)
        {
            int j = (i + 1) % n;
            int y1 = PY(i), y2 = PY(j);
            active[i] = (y1 != y2) && std::min(y1, y2) <= y && y <= std::max(y1, y2);
            used[i] = 0;
            if(active[i])
                ranges[i] = edge_range(PX(i), y1, PX(j), y2, y);
        }

        row_ranges.clear();
        spans.clear();

        for(int i = 0; i < n; ++i)
        {
            if(!active[i] || used[i])
                continue;

            used[i] = 1;
            Range g = ranges[i];

            /* follow the outline forwards (+1) and backwards (-1) through
             * vertices on this row where it passes from one side of the
             * row to the other */
            for(int dir = 1; dir >= -1; dir -= 2)
            {
                int e = i;
                for(;;)
                {
                    int shared = (dir > 0) ? (e + 1) % n : e;   /* vertex between e and the next edge */
                    if(PY(shared) != y)
                        break;

                    int k = (e + dir + n) % n;
                    int hmin = PX(shared), hmax = PX(shared);
                    int guard = 0;
                    while(guard < n && PY(k) == PY((k + 1) % n))
                    {
                        hmin = std::min(hmin, std::min(PX(k), PX((k + 1) % n)));
                        hmax = std::max(hmax, std::max(PX(k), PX((k + 1) % n)));
                        k = (k + dir + n) % n;
                        ++guard;
                    }

                    if(!active[k] || used[k])
                        break;

                    /* the far ends of e and k must lie on opposite sides */
                    int far_e = (dir > 0) ? PY(e) : PY((e + 1) % n);
                    int far_k = (dir > 0) ? PY((k + 1) % n) : PY(k);
                    if((far_e < y) == (far_k < y))
                        break;

                    if(guard)
                    {
                        g.lo = std::min(g.lo, shl(hmin));
                        g.hi = std::max(g.hi, shl(hmax));
                    }
                    g.lo = std::min(g.lo, ranges[k].lo);
                    g.hi = std::max(g.hi, ranges[k].hi);
                    used[k] = 1;
                    e = k;
                }
            }

            row_ranges.push_back(g);
        }

        std::sort(row_ranges.begin(), row_ranges.end(), range_less);

        for(size_t k = 0; k + 1 < row_ranges.size(); k += 2)
        {
            Span s;
            s.l = left_pixel(std::min(row_ranges[k].lo, row_ranges[k + 1].lo));
            s.r = right_pixel(std::max(row_ranges[k].hi, row_ranges[k + 1].hi));
            if(s.l <= s.r)
                spans.push_back(s);
        }

        /* horizontal edges on this row are filled completely */
        for(int i = 0; i < n; ++i)
        {
            int j = (i + 1) % n;
            if(PY(i) == y && PY(j) == y)
            {
                Span s;
                s.l = std::min(PX(i), PX(j));
                s.r = std::max(PX(i), PX(j));
                spans.push_back(s);
            }
        }

        if(spans.empty())
            continue;

        /* write the union of the spans, each pixel once */
        std::sort(spans.begin(), spans.end(), span_less);
        int cur_l = spans[0].l, cur_r = spans[0].r;
        for(size_t k = 1; k < spans.size(); ++k)
        {
            if(spans[k].l <= cur_r + 1)
                cur_r = std::max(cur_r, spans[k].r);
            else
            {
                zcport_hfill(bmp, cur_l, y, cur_r, color);
                cur_l = spans[k].l;
                cur_r = spans[k].r;
            }
        }
        zcport_hfill(bmp, cur_l, y, cur_r, color);
    }
#undef PX
#undef PY
}

void triangle(BITMAP *bmp, int x1, int y1, int x2, int y2, int x3, int y3, int color)
{
    int pts[6] = { x1, y1, x2, y2, x3, y3 };
    polygon(bmp, 3, pts, color);
}

} // extern "C"

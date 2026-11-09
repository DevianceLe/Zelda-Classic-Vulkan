/*
 * zcport primitives: lines, rectangles, circles, ellipses, arcs.
 *
 * do_line/line/fastline, rect, do_circle/circle/circlefill,
 * do_ellipse/ellipse/ellipsefill, do_arc/arc. The do_* helpers emit points
 * in a fixed order through a callback; the drawing functions feed them a
 * clipped putpixel. Checked against the reference build by
 * tests/conformance/test_prim_core and test_prim_shapes (pixels, point
 * streams, overdraw counts).
 *
 * Written from scratch for zcport.
 */

#include "prim_internal.h"

#include <math.h>

using namespace zcprim;

typedef void (*point_proc)(BITMAP *, int, int, int);

namespace
{

/* Bounding-box test used by the shape drawers: returns false when the box
 * is entirely outside the clip rect; sets *inside when it is entirely
 * within it (then per-pixel clipping can be skipped). */
bool box_visible(BITMAP *b, int x1, int y1, int x2, int y2, bool *inside)
{
    *inside = false;

    if(!b->clip)
    {
        *inside = true;
        return true;
    }

    if(x1 >= b->cr || y1 >= b->cb || x2 < b->cl || y2 < b->ct)
        return false;

    *inside = (x1 >= b->cl && y1 >= b->ct && x2 < b->cr && y2 < b->cb);
    return true;
}

/* Runs 'body' with clipping switched off when the shape is fully inside. */
struct ClipScope
{
    BITMAP *b;
    int saved;
    ClipScope(BITMAP *bmp, bool inside) : b(bmp), saved(bmp->clip)
    {
        if(inside)
            b->clip = FALSE;
    }
    ~ClipScope()
    {
        b->clip = saved;
    }
};

} // namespace

extern "C" {

/* ---- lines ------------------------------------------------------------------ */

void do_line(BITMAP *bmp, int x1, int y1, int x2, int y2, int d, point_proc proc)
{
    int dx = x2 - x1, dy = y2 - y1;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;

    if(adx == 0 && ady == 0)
    {
        proc(bmp, x1, y1, d);
        return;
    }

    /* Bresenham along the major axis (x on ties), from the first point to
     * the last inclusive; the minor coordinate steps when the error term
     * reaches zero. */
    int x = x1, y = y1;

    if(adx >= ady)
    {
        int err = 2 * ady - adx;

        for(int i = 0; i <= adx; ++i)
        {
            proc(bmp, x, y, d);

            if(err >= 0)
            {
                y += sy;
                err += 2 * ady - 2 * adx;
            }
            else
                err += 2 * ady;

            x += sx;
        }
    }
    else
    {
        int err = 2 * adx - ady;

        for(int i = 0; i <= ady; ++i)
        {
            proc(bmp, x, y, d);

            if(err >= 0)
            {
                x += sx;
                err += 2 * adx - 2 * ady;
            }
            else
                err += 2 * adx;

            y += sy;
        }
    }
}

void line(BITMAP *bmp, int x1, int y1, int x2, int y2, int color)
{
    if(x1 == x2)
    {
        vfill(bmp, x1, y1, y2, color);
        return;
    }

    if(y1 == y2)
    {
        zcport_hfill(bmp, x1, y1, x2, color);
        return;
    }

    bool inside;
    if(!box_visible(bmp, x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2, x1 > x2 ? x1 : x2, y1 > y2 ? y1 : y2, &inside))
        return;

    ClipScope scope(bmp, inside);
    do_line(bmp, x1, y1, x2, y2, color, put_clipped_cb);
}

namespace
{
/* 32-bit wrapping multiply (the reference computes the intercepts in int). */
inline int mul32(int a, int b)
{
    return (int)((unsigned)a * (unsigned)b);
}

/* Region code of (x, y) against the inclusive rectangle. */
inline int region(int x, int y, int cl, int ct, int cr, int cb)
{
    int c = 0;
    if(x < cl) c |= 1;
    else if(x > cr) c |= 2;
    if(y < ct) c |= 4;
    else if(y > cb) c |= 8;
    return c;
}

void put_mode_cb(BITMAP *b, int x, int y, int color)
{
    zcport_put_mode(b, x, y, color);
}
} // namespace

void fastline(BITMAP *bmp, int x1, int y1, int x2, int y2, int color)
{
    /* fastline() clips the end points to the clip rectangle first
     * (outcode clipping: the first end point outside before the second,
     * top, bottom, left, right edge in that order, intercepts by
     * truncating integer division) and then draws the line between the
     * clipped end points; that can choose other pixels than line(). */
    if(bmp->clip)
    {
        const int cl = bmp->cl, ct = bmp->ct, cr = bmp->cr - 1, cb = bmp->cb - 1;

        for(int guard = 0; guard < 16; ++guard)
        {
            int c1 = region(x1, y1, cl, ct, cr, cb);
            int c2 = region(x2, y2, cl, ct, cr, cb);

            if(!(c1 | c2))
                break;
            if(c1 & c2)
                return;

            int c = c1 ? c1 : c2;
            int x, y;

            if(c & 4)
            {
                x = x1 + mul32(x2 - x1, ct - y1) / (y2 - y1);
                y = ct;
            }
            else if(c & 8)
            {
                x = x1 + mul32(x2 - x1, cb - y1) / (y2 - y1);
                y = cb;
            }
            else if(c & 1)
            {
                y = y1 + mul32(y2 - y1, cl - x1) / (x2 - x1);
                x = cl;
            }
            else
            {
                y = y1 + mul32(y2 - y1, cr - x1) / (x2 - x1);
                x = cr;
            }

            if(c == c1)
            {
                x1 = x;
                y1 = y;
            }
            else
            {
                x2 = x;
                y2 = y;
            }
        }

        if(region(x1, y1, cl, ct, cr, cb) | region(x2, y2, cl, ct, cr, cb))
            return;
    }

    do_line(bmp, x1, y1, x2, y2, color, put_mode_cb);
}

void rect(BITMAP *bmp, int x1, int y1, int x2, int y2, int color)
{
    if(x2 < x1)
    {
        int t = x1;
        x1 = x2;
        x2 = t;
    }

    if(y2 < y1)
    {
        int t = y1;
        y1 = y2;
        y2 = t;
    }

    /* every pixel once (matters for xor/trans): top, bottom, then the
     * sides without the corners */
    zcport_hfill(bmp, x1, y1, x2, color);

    if(y2 > y1)
        zcport_hfill(bmp, x1, y2, x2, color);

    if(y2 - 1 >= y1 + 1)
    {
        vfill(bmp, x1, y1 + 1, y2 - 1, color);

        if(x2 > x1)
            vfill(bmp, x2, y1 + 1, y2 - 1, color);
    }
}

/* ---- circles ---------------------------------------------------------------- */

void do_circle(BITMAP *bmp, int x, int y, int radius, int d, point_proc proc)
{
    /* Midpoint circle, one octant at a time with 8-way symmetry; points on
     * the axes and the diagonal are emitted once. */
    int cx = 0, cy = radius;
    int df = 1 - radius;
    int d_e = 3;
    int d_se = -2 * radius + 5;

    do
    {
        proc(bmp, x + cx, y + cy, d);
        if(cx) proc(bmp, x - cx, y + cy, d);
        if(cy) proc(bmp, x + cx, y - cy, d);
        if(cx && cy) proc(bmp, x - cx, y - cy, d);

        if(cx != cy)
        {
            proc(bmp, x + cy, y + cx, d);
            if(cx) proc(bmp, x + cy, y - cx, d);
            if(cy) proc(bmp, x - cy, y + cx, d);
            if(cx && cy) proc(bmp, x - cy, y - cx, d);
        }

        if(df < 0)
        {
            df += d_e;
            d_e += 2;
            d_se += 2;
        }
        else
        {
            df += d_se;
            d_e += 2;
            d_se += 4;
            cy--;
        }

        cx++;
    }
    while(cx <= cy);
}

void circle(BITMAP *bmp, int x, int y, int radius, int color)
{
    bool inside;
    if(!box_visible(bmp, x - radius, y - radius, x + radius, y + radius, &inside))
        return;

    ClipScope scope(bmp, inside);
    do_circle(bmp, x, y, radius, color, put_clipped_cb);
}

void circlefill(BITMAP *bmp, int x, int y, int radius, int color)
{
    bool inside;
    if(!box_visible(bmp, x - radius, y - radius, x + radius, y + radius, &inside))
        return;

    ClipScope scope(bmp, inside);

    /* Same midpoint walk as do_circle; each row is filled exactly once:
     * rows y+-cx at half-width cy every step, rows y+-cy at half-width cx
     * only when cy is about to move inwards. */
    int cx = 0, cy = radius;
    int df = 1 - radius;
    int d_e = 3;
    int d_se = -2 * radius + 5;

    do
    {
        zcport_hfill(bmp, x - cy, y - cx, x + cy, color);
        if(cx)
            zcport_hfill(bmp, x - cy, y + cx, x + cy, color);

        if(df < 0)
        {
            df += d_e;
            d_e += 2;
            d_se += 2;
        }
        else
        {
            if(cx != cy)
            {
                zcport_hfill(bmp, x - cx, y - cy, x + cx, color);
                if(cy)
                    zcport_hfill(bmp, x - cx, y + cy, x + cx, color);
            }

            df += d_se;
            d_e += 2;
            d_se += 4;
            cy--;
        }

        cx++;
    }
    while(cx <= cy);
}

/* ---- ellipses --------------------------------------------------------------- */

namespace
{
/* Emits the up to four mirror images of (x, y) around the centre, each
 * distinct point once, in the order (x,y) (-x,y) (x,-y) (-x,-y). */
inline void ellipse_points(BITMAP *bmp, int cx, int cy, int x, int y, int d, point_proc proc)
{
    proc(bmp, cx + x, cy + y, d);
    if(x)
        proc(bmp, cx - x, cy + y, d);
    if(y)
    {
        proc(bmp, cx + x, cy - y, d);
        if(x)
            proc(bmp, cx - x, cy - y, d);
    }
}
}

void do_ellipse(BITMAP *bmp, int ix, int iy, int rx, int ry, int d, point_proc proc)
{
    /* The outline is traced in two parts. With
     *     F(x, y) = ry^2 x^2 + rx^2 y^2 - rx^2 ry^2
     * every step picks, of the two candidate pixels, the one with the
     * smaller |F| (the one whose squared "radius" is closer to the curve):
     *  1. from (rx, 0) one point per row going up, x either staying or
     *     moving one inwards, while the curve is steep there
     *     (ry^2 x >= rx^2 y, tested with the x of the previous row; a
     *     column that reached x = 0 always continues up to ry);
     *  2. from (0, ry) one point per column going right, y either staying
     *     or moving one down, for the columns part 1 did not reach. */
    long long a2 = (long long)rx * rx, b2 = (long long)ry * ry;
    long long ab2 = a2 * b2;
    int x = rx, y = 0;

    ellipse_points(bmp, ix, iy, x, y, d, proc);

    for(y = 1; y <= ry; ++y)
    {
        if(x != 0 && b2 * x < a2 * y)
            break;

        /* F(x, y) + F(x - 1, y) > 0: x - 1 is closer */
        long long fy = a2 * y * y - ab2;
        long long sum = b2 * ((long long)x * x + (long long)(x - 1) * (x - 1)) + 2 * fy;
        if(x > 0 && sum > 0)
            --x;

        ellipse_points(bmp, ix, iy, x, y, d, proc);
    }

    int last_x = x;
    y = ry;

    for(x = 0; x < last_x; ++x)
    {
        long long fx = b2 * x * x - ab2;
        long long sum = a2 * ((long long)y * y + (long long)(y - 1) * (y - 1)) + 2 * fx;
        if(y > 0 && sum > 0)
            --y;

        ellipse_points(bmp, ix, iy, x, y, d, proc);
    }
}

void ellipse(BITMAP *bmp, int x, int y, int rx, int ry, int color)
{
    bool inside;
    if(!box_visible(bmp, x - rx, y - ry, x + rx, y + ry, &inside))
        return;

    ClipScope scope(bmp, inside);
    do_ellipse(bmp, x, y, rx, ry, color, put_clipped_cb);
}

namespace
{
/* Collects, per row offset, the widest x the ellipse outline reaches. */
struct RowSpan
{
    int cy;
    int *half;   /* half[row + ry] = max |x| on that row, -1 if none */
    int ry;
};
RowSpan *g_span = 0;

void span_cb(BITMAP *, int x, int y, int)
{
    int row = y - g_span->cy + g_span->ry;
    int hx = x < 0 ? -x : x;

    if(g_span->half[row] < hx)
        g_span->half[row] = hx;
}
}

void ellipsefill(BITMAP *bmp, int x, int y, int rx, int ry, int color)
{
    if(rx < 0) rx = 0;
    if(ry < 0) ry = 0;

    bool inside;
    if(!box_visible(bmp, x - rx, y - ry, x + rx, y + ry, &inside))
        return;

    int rows = 2 * ry + 1;
    int *half = new int[rows];
    for(int i = 0; i < rows; ++i)
        half[i] = -1;

    RowSpan rs;
    rs.cy = 0;
    rs.half = half;
    rs.ry = ry;
    g_span = &rs;
    do_ellipse(bmp, 0, 0, rx, ry, 0, span_cb);
    g_span = 0;

    ClipScope scope(bmp, inside);
    for(int i = 0; i < rows; ++i)
        if(half[i] >= 0)
            zcport_hfill(bmp, x - half[i], y + i - ry, x + half[i], color);

    delete[] half;
}

/* ---- arcs ------------------------------------------------------------------- */

namespace
{
/* Arc walk quadrants in screen coordinates (y grows downwards), counter-
 * clockwise: 0 east->north, 1 north->west, 2 west->south, 3 south->east,
 * with the direction each one moves x and y in. */
const int kArcDx[4] = { -1, -1, 1, 1 };
const int kArcDy[4] = { -1, 1, 1, -1 };

/* Quadrant of a binary angle in [0, 256): an axis angle belongs to the
 * quadrant that ends there, except 0 which starts quadrant 0. */
inline int arc_quadrant(int32_t b)
{
    return (b <= 0x400000) ? 0 : (int)((b - 1) >> 22);
}

/* The point (r cos a, -r sin a) rounded to the nearest pixel. */
inline void arc_point(int32_t b, int r, int *px, int *py)
{
    double t = (double)b / 65536.0 * (3.14159265358979323846 / 128.0);
    *px = (int)floor(r * cos(t) + 0.5);
    *py = -(int)floor(r * sin(t) + 0.5);
}

/* Has (x, y) reached or passed (ex, ey) in both coordinates, moving the
 * way quadrant q does? */
inline bool arc_reached(int q, int x, int y, int ex, int ey)
{
    q &= 3;
    bool okx = (kArcDx[q] > 0) ? (x >= ex) : (x <= ex);
    bool oky = (kArcDy[q] > 0) ? (y >= ey) : (y <= ey);
    return okx && oky;
}

/* Is (x, y) at (or past) the axis that ends quadrant q? */
inline bool arc_at_end(int q, int x, int y)
{
    switch(q & 3)
    {
    case 0: return x <= 0;
    case 1: return y >= 0;
    case 2: return x >= 0;
    default: return y <= 0;
    }
}
} // namespace

void do_arc(BITMAP *bmp, int x, int y, fixed ang1, fixed ang2, int r, int d, point_proc proc)
{
    /* Behaviour established by black-box comparison with the reference
     * (tests/conformance/test_prim_core, test_prim_shapes):
     *  - the start and end points are the rounded points of the circle at
     *    ang1 and ang2; the walk goes counter-clockwise from the start
     *    point, one pixel at a time, each step taking whichever of the
     *    three neighbours in the quadrant's direction lies closest to the
     *    circle (|x^2 + y^2 - r^2| smallest; x move, y move, diagonal on
     *    ties);
     *  - it stops after the first point that reaches the end point in both
     *    coordinates within the end quadrant (one lap later if the end
     *    angle is not after the start angle in the same quadrant), or
     *    before coming back to the start point;
     *  - a start point lying on the axis that ends its quadrant is moved
     *    into the next quadrant first, and if it then already reaches the
     *    end point nothing is drawn. */
    if(r < 0)
        return;   /* undefined in the reference */

    int32_t b1 = ang1 & 0xFFFFFF, b2 = ang2 & 0xFFFFFF;

    if(r == 0)
    {
        /* the reference's walk around a zero radius circle */
        proc(bmp, x, y, d);
        if(b2 < b1 || (b1 < 0x400000 && b2 >= 0xC00000))
        {
            proc(bmp, x - 1, y, d);
            proc(bmp, x, y, d);
            proc(bmp, x + 1, y, d);
        }
        return;
    }

    int sx, sy, ex, ey;
    arc_point(b1, r, &sx, &sy);
    arc_point(b2, r, &ex, &ey);

    int q = arc_quadrant(b1), qe = arc_quadrant(b2);
    if(qe < q || (qe == q && b2 < b1))
        qe += 4;

    if(q < qe && arc_at_end(q, sx, sy))
    {
        ++q;
        if(q == qe && arc_reached(q, sx, sy, ex, ey))
            return;
    }

    const long long rr = (long long)r * r;
    int px = sx, py = sy;

    /* a lap has fewer than 8 r points; the cap only guards odd inputs */
    for(long long guard = 16LL * r + 64; guard > 0; --guard)
    {
        proc(bmp, x + px, y + py, d);

        if(q == qe && arc_reached(q, px, py, ex, ey))
            break;

        if(q < qe && arc_at_end(q, px, py))
        {
            ++q;
            if(q == qe && arc_reached(q, px, py, ex, ey))
                break;
        }

        int dx = kArcDx[q & 3], dy = kArcDy[q & 3];
        long long ex1 = (long long)(px + dx) * (px + dx) + (long long)py * py - rr;
        long long ey1 = (long long)px * px + (long long)(py + dy) * (py + dy) - rr;
        long long eb = (long long)(px + dx) * (px + dx) + (long long)(py + dy) * (py + dy) - rr;
        if(ex1 < 0) ex1 = -ex1;
        if(ey1 < 0) ey1 = -ey1;
        if(eb < 0) eb = -eb;

        if(ex1 <= ey1 && ex1 <= eb)
            px += dx;
        else if(ey1 <= eb)
            py += dy;
        else
        {
            px += dx;
            py += dy;
        }

        if(px == sx && py == sy)
            break;
    }
}

void arc(BITMAP *bmp, int x, int y, fixed ang1, fixed ang2, int r, int color)
{
    do_arc(bmp, x, y, ang1, ang2, r, color, put_clipped_cb);
}

} // extern "C"

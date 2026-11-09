/*
 * zcport blit module: 3D polygons (polygon3d_f, triangle3d_f, quad3d_f and
 * the fixed point polygon3d, triangle3d, quad3d) with every POLYTYPE_*.
 *
 * Rules (established against the reference build, test_blit_poly3d.cpp):
 *   - an edge covers the rows ceil(y_top) .. ceil(y_bottom) - 1 and is
 *     crossed by row y at exactly y; a span covers ceil(x_left) ..
 *     ceil(x_right) - 1;
 *   - values are interpolated in 16.16 fixed point along the edges (colors,
 *     light levels, affine u/v) or in float (1/z, u/z, v/z), and across a
 *     span from per-span gradients (polygons) or from constant gradients
 *     (triangles: the middle vertex against the long edge at its height);
 *   - polygon3d_f walks a ring of the non-empty edges with two walkers
 *     starting at the topmost edge, segment by segment; edges that cross
 *     (concave or self-intersecting input) draw what the reference draws;
 *   - perspective mapping evaluates u/v exactly every 4 pixels (double
 *     precision) and steps linearly in between;
 *   - the top clip moves an edge's start in one step; the left clip moves a
 *     span's start in one step.
 *
 * Written from scratch for zcport.
 */

#include "blit_internal.h"

#include <math.h>
#include <stdlib.h>

#include <vector>

#if defined(_MSC_VER)
#pragma float_control(precise, on)
#pragma fp_contract(off)
#endif

using namespace zcblit;

namespace
{

/* float/double -> int like the x86 truncating conversion: out of range and
 * NaN give INT_MIN (the same on every platform). */
inline int d2i(double f)
{
    if(!(f >= -2147483648.0 && f < 2147483648.0))
        return (int)0x80000000;
    return (int)f;
}

inline fixed wrap_mul(fixed a, int n)
{
    return (fixed)((uint32_t)a * (uint32_t)n);
}

enum
{
    I_FLAT = 1,
    I_COL = 2,     /* 8-bit gouraud color, or the light level of the LIT types */
    I_RGB = 4,     /* r, g, b gouraud */
    I_UV = 8,      /* affine fixed u, v */
    I_Z = 16,      /* perspective: float 1/z, u/z, v/z */
    I_MASK = 32,
    I_LIT = 64,
    I_TRANS = 128
};

int type_flags(int type, int depth)
{
    switch(type)
    {
    case POLYTYPE_FLAT: return I_FLAT;
    case POLYTYPE_GCOL: return depth == 8 ? I_COL : I_RGB;
    case POLYTYPE_GRGB: return I_RGB;
    case POLYTYPE_ATEX: return I_UV;
    case POLYTYPE_PTEX: return I_Z;
    case POLYTYPE_ATEX_MASK: return I_UV | I_MASK;
    case POLYTYPE_PTEX_MASK: return I_Z | I_MASK;
    case POLYTYPE_ATEX_LIT: return I_UV | I_LIT | I_COL;
    case POLYTYPE_PTEX_LIT: return I_Z | I_LIT | I_COL;
    case POLYTYPE_ATEX_MASK_LIT: return I_UV | I_MASK | I_LIT | I_COL;
    case POLYTYPE_PTEX_MASK_LIT: return I_Z | I_MASK | I_LIT | I_COL;
    case POLYTYPE_ATEX_TRANS: return I_UV | I_TRANS;
    case POLYTYPE_PTEX_TRANS: return I_Z | I_TRANS;
    case POLYTYPE_ATEX_MASK_TRANS: return I_UV | I_MASK | I_TRANS;
    case POLYTYPE_PTEX_MASK_TRANS: return I_Z | I_MASK | I_TRANS;
    default: return -1;
    }
}

/* Interpolated values at a point, or their change per step. */
struct Vals
{
    fixed c, r, g, b;
    fixed u, v;
    float z, fu, fv;
};

struct Edge
{
    int top, bottom;
    fixed x, dx;
    Vals val, step;
};

/* r, g, b (0-255) of a vertex color: GRGB takes a 24-bit triplet, GCOL on
 * a hicolor/truecolor bitmap a pixel value of that depth. */
void vertex_rgb(int type, int depth, int c, int &r, int &g, int &b)
{
    if(type == POLYTYPE_GCOL && depth != 8)
    {
        split_pixel(depth, (uint32_t)c, r, g, b);
        return;
    }
    r = (c >> 16) & 255;
    g = (c >> 8) & 255;
    b = c & 255;
}

/* ---- edges ---------------------------------------------------------------- */

void clear_vals(Edge &e)
{
    memset(&e.val, 0, sizeof(e.val));
    memset(&e.step, 0, sizeof(e.step));
}

/* Color / light / rgb parts, common to both vertex types. */
template <class V>
void edge_colors(Edge &e, const V *a, const V *b, fixed step, fixed height, int flags, int type, int depth)
{
    if(flags & I_COL)
    {
        e.step.c = fixdiv(itofix(b->c - a->c), height);
        e.val.c = itofix(a->c) + fixmul(step, e.step.c);
    }
    if(flags & I_RGB)
    {
        int r1, g1, b1, r2, g2, b2;
        vertex_rgb(type, depth, a->c, r1, g1, b1);
        vertex_rgb(type, depth, b->c, r2, g2, b2);
        e.step.r = fixdiv(itofix(r2 - r1), height);
        e.val.r = itofix(r1) + fixmul(step, e.step.r);
        e.step.g = fixdiv(itofix(g2 - g1), height);
        e.val.g = itofix(g1) + fixmul(step, e.step.g);
        e.step.b = fixdiv(itofix(b2 - b1), height);
        e.val.b = itofix(b1) + fixmul(step, e.step.b);
    }
}

/* Perspective part: q = 1/z and u/z, v/z scaled to 16.16 at both ends. */
void edge_persp(Edge &e, float qa, float qb, float ua, float ub, float va, float vb, float h1, float stepf)
{
    e.step.z = (qb - qa) * h1;
    e.val.z = e.step.z * stepf + qa;
    e.step.fu = (ub - ua) * h1;
    e.step.fv = (vb - va) * h1;
    e.val.fu = e.step.fu * stepf + ua;
    e.val.fv = e.step.fv * stepf + va;
}

/* Builds the edge between a and b (either order). Returns false for an
 * edge that covers no row. */
bool make_edge(Edge &e, const V3D_f *a, const V3D_f *b, int flags, int type, int depth)
{
    if(b->y < a->y)
    {
        const V3D_f *t = a;
        a = b;
        b = t;
    }
    e.top = fixceil(ftofix(a->y));
    e.bottom = fixceil(ftofix(b->y)) - 1;
    if(e.bottom < e.top)
        return false;

    const float h1 = 1.0f / (b->y - a->y);
    const fixed height = ftofix(b->y - a->y);
    const fixed step = (e.top << 16) - ftofix(a->y);
    const float stepf = (float)fixtof(step);

    e.dx = ftofix((b->x - a->x) * h1);
    e.x = ftofix(a->x) + fixmul(step, e.dx);
    clear_vals(e);
    edge_colors(e, a, b, step, height, flags, type, depth);
    if(flags & I_UV)
    {
        e.step.u = ftofix((b->u - a->u) * h1);
        e.step.v = ftofix((b->v - a->v) * h1);
        e.val.u = ftofix(a->u) + fixmul(step, e.step.u);
        e.val.v = ftofix(a->v) + fixmul(step, e.step.v);
    }
    if(flags & I_Z)
    {
        const float qa = 1.0f / a->z, qb = 1.0f / b->z;
        edge_persp(e, qa, qb, a->u * qa * 65536.0f, b->u * qb * 65536.0f,
                   a->v * qa * 65536.0f, b->v * qb * 65536.0f, h1, stepf);
    }
    return true;
}

bool make_edge(Edge &e, const V3D *a, const V3D *b, int flags, int type, int depth)
{
    if(b->y < a->y)
    {
        const V3D *t = a;
        a = b;
        b = t;
    }
    e.top = fixceil(a->y);
    e.bottom = fixceil(b->y) - 1;
    if(e.bottom < e.top)
        return false;

    const fixed height = b->y - a->y;
    const fixed step = (e.top << 16) - a->y;

    e.dx = fixdiv(b->x - a->x, height);
    e.x = a->x + fixmul(step, e.dx);
    clear_vals(e);
    edge_colors(e, a, b, step, height, flags, type, depth);
    if(flags & I_UV)
    {
        e.step.u = fixdiv(b->u - a->u, height);
        e.step.v = fixdiv(b->v - a->v, height);
        e.val.u = a->u + fixmul(step, e.step.u);
        e.val.v = a->v + fixmul(step, e.step.v);
    }
    if(flags & I_Z)
    {
        const float qa = 65536.0f / a->z, qb = 65536.0f / b->z;
        edge_persp(e, qa, qb, a->u * qa, b->u * qb, a->v * qa, b->v * qb, 65536.0f / height,
                   (float)fixtof(step));
    }
    return true;
}

void vals_add(Vals &v, const Vals &d, int flags)
{
    if(flags & I_COL)
        v.c += d.c;
    if(flags & I_RGB)
    {
        v.r += d.r;
        v.g += d.g;
        v.b += d.b;
    }
    if(flags & I_UV)
    {
        v.u += d.u;
        v.v += d.v;
    }
    if(flags & I_Z)
    {
        v.z += d.z;
        v.fu += d.fu;
        v.fv += d.fv;
    }
}

/* Moves values 'gap' steps ahead in one go. */
void vals_skip(Vals &v, const Vals &d, int gap, int flags)
{
    if(flags & I_COL)
        v.c += wrap_mul(d.c, gap);
    if(flags & I_RGB)
    {
        v.r += wrap_mul(d.r, gap);
        v.g += wrap_mul(d.g, gap);
        v.b += wrap_mul(d.b, gap);
    }
    if(flags & I_UV)
    {
        v.u += wrap_mul(d.u, gap);
        v.v += wrap_mul(d.v, gap);
    }
    if(flags & I_Z)
    {
        v.z += d.z * gap;
        v.fu += d.fu * gap;
        v.fv += d.fv * gap;
    }
}

void edge_advance(Edge &e, int flags)
{
    e.x += e.dx;
    vals_add(e.val, e.step, flags);
}

/* Clips an edge to the rows [clip_t, clip_b]. */
bool edge_clip(Edge &e, int clip_t, int clip_b, int flags)
{
    if(e.top < clip_t)
    {
        const int gap = clip_t - e.top;
        e.top = clip_t;
        e.x += wrap_mul(e.dx, gap);
        vals_skip(e.val, e.step, gap, flags);
    }
    if(e.bottom > clip_b)
        e.bottom = clip_b;
    return e.bottom >= e.top;
}

/* ---- scanline fillers ------------------------------------------------------ */

struct Ctx3d
{
    BITMAP *bmp;
    int type, flags, depth, color;
    const unsigned char *tex;    /* texture row 0 */
    int tpitch;                  /* bytes between texture rows */
    int umask, vmask;
    uint32_t mask;               /* the texture's mask color */
};

inline uint32_t pack_rgb(int depth, fixed r, fixed g, fixed b)
{
    return make_pixel(depth, (r >> 16) & 255, (g >> 16) & 255, (b >> 16) & 255);
}

/* One textured pixel: texel t at destination pixel x of row. */
template <int BPP>
inline void put_texel(const Ctx3d &k, unsigned char *row, int x, uint32_t t, fixed c)
{
    if((k.flags & I_MASK) && t == k.mask)
        return;
    if(BPP == 1)
    {
        const COLOR_MAP *map = color_map;
        if(k.flags & I_LIT)
            row[x] = map->data[(c >> 16) & 0xFF][t];
        else if(k.flags & I_TRANS)
            row[x] = map->data[t][row[x]];
        else
            row[x] = (unsigned char)t;
        return;
    }
    if(k.flags & I_LIT)
        t = blend_lit_poly(k.depth, t, (c >> 16) & 0xFF);
    else if(k.flags & I_TRANS)
        t = blend_trans(k.depth, t, Pix<BPP>::get(row, x));
    Pix<BPP>::put(row, x, t);
}

template <int BPP>
inline uint32_t texel(const Ctx3d &k, fixed u, fixed v)
{
    return Pix<BPP>::get(k.tex + ((v >> 16) & k.vmask) * k.tpitch, (u >> 16) & k.umask);
}

/* Fills pixels x .. x+w-1 of row y starting from values s, stepping by d. */
template <int BPP>
void fill(const Ctx3d &k, int y, int x, int w, const Vals &s, const Vals &d)
{
    BITMAP *bmp = k.bmp;
    unsigned char *row = bmp->line[y];

    if(k.flags & I_FLAT)
    {
        zcport_hfill(bmp, x, y, x + w - 1, k.color);
        return;
    }
    if(k.flags & I_RGB)
    {
        fixed r = s.r, g = s.g, b = s.b;
        for(int i = 0; i < w; ++i, r += d.r, g += d.g, b += d.b)
        {
            if(BPP == 1)
                row[x + i] = rgb_map ? rgb_map->data[(r >> 19) & 31][(g >> 19) & 31][(b >> 19) & 31] : 0;
            else
                Pix<BPP>::put(row, x + i, pack_rgb(k.depth, r, g, b));
        }
        return;
    }
    if(!(k.flags & (I_UV | I_Z)))
    {
        /* 8-bit GCOL */
        fixed c = s.c;
        for(int i = 0; i < w; ++i, c += d.c)
            row[x + i] = (unsigned char)(c >> 16);
        return;
    }

    fixed c = s.c;
    if(k.flags & I_UV)
    {
        fixed u = s.u, v = s.v;
        for(int i = 0; i < w; ++i)
        {
            put_texel<BPP>(k, row, x + i, texel<BPP>(k, u, v), c);
            u += d.u;
            v += d.v;
            c += d.c;
        }
        return;
    }

    /* Perspective: u/v exact (double precision) at the start of every run
     * of 4 pixels, linear steps of a quarter of the difference (rounded
     * down) inside it; the running u/v carry over from run to run. */
    double q = s.z, fu = s.fu, fv = s.fv;
    const double dq = d.z * 4.0, dfu = d.fu * 4.0, dfv = d.fv * 4.0;
    double q1 = 1.0 / q;
    fixed u = d2i(fu * q1), v = d2i(fv * q1);
    for(int i = 0; i < w;)
    {
        fu += dfu;
        fv += dfv;
        q += dq;
        q1 = 1.0 / q;
        const fixed du = (fixed)((uint32_t)d2i(fu * q1) - (uint32_t)u) >> 2;
        const fixed dv = (fixed)((uint32_t)d2i(fv * q1) - (uint32_t)v) >> 2;
        const int end = (w - i < 4) ? w : i + 4;
        for(; i < end; ++i)
        {
            put_texel<BPP>(k, row, x + i, texel<BPP>(k, u, v), c);
            u += du;
            v += dv;
            c += d.c;
        }
    }
}

typedef void (*FillFn)(const Ctx3d &, int, int, int, const Vals &, const Vals &);

/* Clips the span x1..x2 of row y to the bitmap and fills it. */
void clip_fill(const Ctx3d &k, FillFn fn, int y, int x1, int x2, Vals &s, const Vals &d)
{
    BITMAP *bmp = k.bmp;
    const int cl = bmp->clip ? bmp->cl : 0;
    const int cr = bmp->clip ? bmp->cr : bmp->w;
    if(x1 < cl)
    {
        vals_skip(s, d, cl - x1, k.flags);
        x1 = cl;
    }
    if(x2 >= cr)
        x2 = cr - 1;
    if(x1 <= x2)
        fn(k, y, x1, x2 - x1 + 1, s, d);
}

/* Span between edges l and r with gradients computed for this span. */
void polygon_span(const Ctx3d &k, FillFn fn, int y, const Edge &l, const Edge &r)
{
    const int x1 = fixceil(l.x);
    const int x2 = fixceil(r.x) - 1;
    if(x2 < x1)
        return;

    Vals s, d;
    memset(&s, 0, sizeof(s));
    memset(&d, 0, sizeof(d));
    const int flags = k.flags;
    if(!(flags & I_FLAT))
    {
        const fixed step = (x1 << 16) - l.x;
        const fixed width = r.x - l.x;
        const float stepf = (float)fixtof(step);
        if(flags & I_COL)
        {
            d.c = fixdiv(r.val.c - l.val.c, width);
            s.c = l.val.c + fixmul(step, d.c);
        }
        if(flags & I_RGB)
        {
            d.r = fixdiv(r.val.r - l.val.r, width);
            s.r = l.val.r + fixmul(step, d.r);
            d.g = fixdiv(r.val.g - l.val.g, width);
            s.g = l.val.g + fixmul(step, d.g);
            d.b = fixdiv(r.val.b - l.val.b, width);
            s.b = l.val.b + fixmul(step, d.b);
        }
        if(flags & I_UV)
        {
            d.u = fixdiv(r.val.u - l.val.u, width);
            d.v = fixdiv(r.val.v - l.val.v, width);
            s.u = l.val.u + fixmul(step, d.u);
            s.v = l.val.v + fixmul(step, d.v);
        }
        if(flags & I_Z)
        {
            const float w1 = 65536.0f / width;
            d.z = (r.val.z - l.val.z) * w1;
            d.fu = (r.val.fu - l.val.fu) * w1;
            d.fv = (r.val.fv - l.val.fv) * w1;
            s.z = d.z * stepf + l.val.z;
            s.fu = d.fu * stepf + l.val.fu;
            s.fv = d.fv * stepf + l.val.fv;
        }
    }
    clip_fill(k, fn, y, x1, x2, s, d);
}

/* ---- setup -------------------------------------------------------------- */

bool setup(Ctx3d &k, FillFn &fn, BITMAP *bmp, int type, BITMAP *texture, int color)
{
    if(!bmp)
        return false;
    type &= ~POLYTYPE_ZBUF;
    k.bmp = bmp;
    k.type = type;
    k.depth = depth_of(bmp);
    k.flags = type_flags(type, k.depth);
    k.color = color;
    if(k.flags < 0)
        return false;
    if((k.flags & (I_LIT | I_TRANS)) && k.depth == 8 && !color_map)
        return false;
    if((k.flags & I_RGB) && k.depth == 8 && !rgb_map)
        return false;
    k.tex = NULL;
    k.tpitch = 0;
    k.umask = k.vmask = 0;
    k.mask = 0;
    if(k.flags & (I_UV | I_Z))
    {
        if(!texture || bpp_of(texture) != bpp_of(bmp))
            return false;
        k.tex = texture->line[0];
        k.tpitch = texture->w * bpp_of(texture);   /* rows taken as contiguous, like the reference */
        k.umask = texture->w - 1;
        k.vmask = texture->h - 1;
        k.mask = mask_of(texture);
    }
    switch(bpp_of(bmp))
    {
    case 1: fn = fill<1>; break;
    case 2: fn = fill<2>; break;
    case 3: fn = fill<3>; break;
    default: fn = fill<4>; break;
    }
    return true;
}

template <class V>
void render_polygon(BITMAP *bmp, int type, BITMAP *texture, int vc, V *vtx[])
{
    if(vc < 3)
        return;
    Ctx3d k;
    FillFn fn;
    if(!setup(k, fn, bmp, type, texture, vtx[0]->c))
        return;
    const int clip_t = bmp->clip ? bmp->ct : 0;
    const int clip_b = bmp->clip ? bmp->cb - 1 : bmp->h - 1;

    /* The edges that cover a visible row, in vertex order, as a ring: edge
     * i runs from vertex i-1 to vertex i. */
    std::vector<Edge> edges((size_t)vc);
    int n = 0, top = 0x7FFFFFFF, bottom = -0x7FFFFFFF, first = 0;
    for(int i = 0; i < vc; ++i)
    {
        Edge &e = edges[(size_t)n];
        if(!make_edge(e, vtx[(i + vc - 1) % vc], vtx[i], k.flags, k.type, k.depth) ||
           !edge_clip(e, clip_t, clip_b, k.flags))
            continue;
        if(e.top < top)
        {
            top = e.top;
            first = n;
        }
        if(e.bottom > bottom)
            bottom = e.bottom;
        ++n;
    }
    if(n < 2)
        return;

    /* Two walkers: one on the topmost edge, the other on the edge before it
     * if that one also starts on the top row, else on the edge after it;
     * each walks away from the other. Rows go in segments that end where
     * either walker's edge ends; a segment runs from the walker further
     * left at its start (the one going right on a tie) to the other. An
     * edge a walker reaches starts from its own top-row state. */
    int ia = first, ib = (first + 1) % n, da = n - 1, db = 1;
    if(edges[(size_t)((first + n - 1) % n)].top == top)
    {
        ib = (first + n - 1) % n;
        da = 1;
        db = n - 1;
    }
    int ytop = top;
    for(int guard = 0; guard < 3 * n + 3; ++guard)
    {
        Edge *ea = &edges[(size_t)ia], *eb = &edges[(size_t)ib];
        const int ybot = (ea->bottom < eb->bottom) ? ea->bottom : eb->bottom;
        Edge *l = ea, *r = eb;
        if(eb->x < ea->x || (eb->x == ea->x && eb->dx < ea->dx))
        {
            l = eb;
            r = ea;
        }
        for(int y = ytop; y <= ybot; ++y)
        {
            polygon_span(k, fn, y, *l, *r);
            edge_advance(*l, k.flags);
            edge_advance(*r, k.flags);
        }
        if(ybot >= bottom)
            break;
        if(ea->bottom <= ybot)
            ia = (ia + da) % n;
        if(eb->bottom <= ybot)
            ib = (ib + db) % n;
        ytop = ybot + 1;
    }
}

/* ---- triangles ------------------------------------------------------------ */

/* Rows y0..y1 between edges l and r; each starts from the left edge's
 * values and the constant gradients d. */
void triangle_rows(const Ctx3d &k, FillFn fn, Edge &l, Edge &r, int y0, int y1, const Vals &d)
{
    const int flags = k.flags;
    for(int y = y0; y <= y1; ++y)
    {
        const int x1 = fixceil(l.x);
        const int x2 = fixceil(r.x) - 1;
        if(x1 <= x2)
        {
            Vals s;
            memset(&s, 0, sizeof(s));
            const fixed step = (x1 << 16) - l.x;
            const float stepf = (float)fixtof(step);
            if(flags & I_COL)
                s.c = l.val.c + fixmul(step, d.c);
            if(flags & I_RGB)
            {
                s.r = l.val.r + fixmul(step, d.r);
                s.g = l.val.g + fixmul(step, d.g);
                s.b = l.val.b + fixmul(step, d.b);
            }
            if(flags & I_UV)
            {
                s.u = l.val.u + fixmul(step, d.u);
                s.v = l.val.v + fixmul(step, d.v);
            }
            if(flags & I_Z)
            {
                s.z = d.z * stepf + l.val.z;
                s.fu = d.fu * stepf + l.val.fu;
                s.fv = d.fv * stepf + l.val.fv;
            }
            clip_fill(k, fn, y, x1, x2, s, d);
        }
        edge_advance(l, flags);
        edge_advance(r, flags);
    }
}

/* Vertex values in the fixed point / float forms the gradients use. */
inline fixed vx(const V3D_f *v) { return ftofix(v->x); }
inline fixed vx(const V3D *v) { return v->x; }
inline fixed vy(const V3D_f *v) { return ftofix(v->y); }
inline fixed vy(const V3D *v) { return v->y; }
inline fixed vu(const V3D_f *v) { return ftofix(v->u); }
inline fixed vu(const V3D *v) { return v->u; }
inline fixed vv(const V3D_f *v) { return ftofix(v->v); }
inline fixed vv(const V3D *v) { return v->v; }
inline float vq(const V3D_f *v) { return 1.0f / v->z; }
inline float vq(const V3D *v) { return 65536.0f / v->z; }
inline float vfu(const V3D_f *v, float q) { return v->u * q * 65536.0f; }
inline float vfu(const V3D *v, float q) { return v->u * q; }
inline float vfv(const V3D_f *v, float q) { return v->v * q * 65536.0f; }
inline float vfv(const V3D *v, float q) { return v->v * q; }

template <class V>
void render_triangle(BITMAP *bmp, int type, BITMAP *texture, V *v1, V *v2, V *v3)
{
    Ctx3d k;
    FillFn fn;
    if(!v1 || !v2 || !v3 || !setup(k, fn, bmp, type, texture, v1->c))
        return;
    const int flags = k.flags;

    /* a top, b middle, c bottom */
    V *a = v1, *b = v2, *c = v3, *t;
    if(b->y < a->y) { t = a; a = b; b = t; }
    if(c->y < a->y) { t = a; a = c; c = t; }
    if(c->y < b->y) { t = b; b = c; c = t; }

    Edge longe, upper, lower;
    if(!make_edge(longe, a, c, flags, k.type, k.depth))
        return;

    /* Constant gradients: b against the long edge at b's height (the long
     * edge's own fixed point / float state moved there with fixmul). */
    Edge at = longe;
    const fixed dy = vy(b) - itofix(longe.top);
    const float dyf = (float)fixtof(dy);
    at.x += fixmul(dy, at.dx);
    if(flags & I_COL)
        at.val.c += fixmul(dy, at.step.c);
    if(flags & I_RGB)
    {
        at.val.r += fixmul(dy, at.step.r);
        at.val.g += fixmul(dy, at.step.g);
        at.val.b += fixmul(dy, at.step.b);
    }
    if(flags & I_UV)
    {
        at.val.u += fixmul(dy, at.step.u);
        at.val.v += fixmul(dy, at.step.v);
    }
    if(flags & I_Z)
    {
        at.val.z += at.step.z * dyf;
        at.val.fu += at.step.fu * dyf;
        at.val.fv += at.step.fv * dyf;
    }
    const fixed width = vx(b) - at.x;
    if(width == 0)
        return;

    Vals d;
    memset(&d, 0, sizeof(d));
    if(flags & I_COL)
        d.c = fixdiv(itofix(b->c) - at.val.c, width);
    if(flags & I_RGB)
    {
        int rb, gb, bb;
        vertex_rgb(k.type, k.depth, b->c, rb, gb, bb);
        d.r = fixdiv(itofix(rb) - at.val.r, width);
        d.g = fixdiv(itofix(gb) - at.val.g, width);
        d.b = fixdiv(itofix(bb) - at.val.b, width);
    }
    if(flags & I_UV)
    {
        d.u = fixdiv(vu(b) - at.val.u, width);
        d.v = fixdiv(vv(b) - at.val.v, width);
    }
    if(flags & I_Z)
    {
        const float w1 = 65536.0f / width;
        const float qb = vq(b);
        d.z = (qb - at.val.z) * w1;
        d.fu = (vfu(b, qb) - at.val.fu) * w1;
        d.fv = (vfv(b, qb) - at.val.fv) * w1;
    }

    const int clip_t = bmp->clip ? bmp->ct : 0;
    const int clip_b = bmp->clip ? bmp->cb - 1 : bmp->h - 1;
    if(!edge_clip(longe, clip_t, clip_b, flags))
        return;
    const bool long_left = width > 0;
    if(make_edge(upper, a, b, flags, k.type, k.depth) && edge_clip(upper, clip_t, clip_b, flags))
    {
        if(long_left)
            triangle_rows(k, fn, longe, upper, upper.top, upper.bottom, d);
        else
            triangle_rows(k, fn, upper, longe, upper.top, upper.bottom, d);
    }
    if(make_edge(lower, b, c, flags, k.type, k.depth) && edge_clip(lower, clip_t, clip_b, flags))
    {
        if(long_left)
            triangle_rows(k, fn, longe, lower, lower.top, lower.bottom, d);
        else
            triangle_rows(k, fn, lower, longe, lower.top, lower.bottom, d);
    }
}

} // namespace

extern "C" {

void polygon3d_f(BITMAP *bmp, int type, BITMAP *texture, int vc, V3D_f *vtx[])
{
    render_polygon(bmp, type, texture, vc, vtx);
}

void triangle3d_f(BITMAP *bmp, int type, BITMAP *texture, V3D_f *v1, V3D_f *v2, V3D_f *v3)
{
    render_triangle(bmp, type, texture, v1, v2, v3);
}

void quad3d_f(BITMAP *bmp, int type, BITMAP *texture, V3D_f *v1, V3D_f *v2, V3D_f *v3, V3D_f *v4)
{
    V3D_f *v[4] = { v1, v2, v3, v4 };
    render_polygon(bmp, type, texture, 4, v);
}

void polygon3d(BITMAP *bmp, int type, BITMAP *texture, int vc, V3D *vtx[])
{
    render_polygon(bmp, type, texture, vc, vtx);
}

void triangle3d(BITMAP *bmp, int type, BITMAP *texture, V3D *v1, V3D *v2, V3D *v3)
{
    render_triangle(bmp, type, texture, v1, v2, v3);
}

void quad3d(BITMAP *bmp, int type, BITMAP *texture, V3D *v1, V3D *v2, V3D *v3, V3D *v4)
{
    V3D *v[4] = { v1, v2, v3, v4 };
    render_polygon(bmp, type, texture, 4, v);
}

} // extern "C"

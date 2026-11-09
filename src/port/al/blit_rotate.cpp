/*
 * zcport blit module: rotated / scaled / flipped sprites.
 *
 * Every rotation function maps the sprite onto a parallelogram of the
 * destination (given by where the sprite's corners land, in 16.16 fixed
 * point) and fills it scanline by scanline: a destination pixel is drawn
 * when its center lies inside the parallelogram, with the sprite pixel its
 * center maps to (nearest neighbour). Checked against the reference build
 * by test_blit_rotate.cpp.
 *
 * Written from scratch for zcport.
 */

#include "blit_internal.h"

#include <math.h>

/* The corner and gradient math must round like the reference build:
 * no reassociation or contraction even when the project uses /fp:fast. */
#if defined(_MSC_VER)
#pragma float_control(precise, on)
#pragma fp_contract(off)
#endif

using namespace zcblit;

namespace
{

enum RotMode
{
    ROT_MASKED,
    ROT_TRANS,
    ROT_LIT
};

/* Draws destination pixels x0..x1-1 of row y; sprite coordinates (16.16)
 * start at (u, v) and advance by (du, dv) per pixel. */
template <int BPP, int SBPP>
void draw_span(BITMAP *bmp, BITMAP *spr, int y, int x0, int x1, fixed u, fixed v,
               fixed du, fixed dv, RotMode mode, int color, const uint32_t *pal)
{
    unsigned char *d = bmp->line[y];
    const int depth = depth_of(bmp);
    /* Between different depths the reference compares the raw sprite pixel
     * with the destination's mask color. */
    const uint32_t mask = (depth == depth_of(spr)) ? mask_of(spr) : mask_of(bmp);
    const COLOR_MAP *map = color_map;
    const fixed wlim = spr->w << 16, hlim = spr->h << 16;
    for(int x = x0; x < x1; ++x, u += du, v += dv)
    {
        if(u < 0 || v < 0 || u >= wlim || v >= hlim)
            continue;
        uint32_t c = Pix<SBPP>::get(spr->line[v >> 16], u >> 16);
        if(c == mask)
            continue;
        if(SBPP == 1 && BPP != 1)
            c = pal[c];
        switch(mode)
        {
        case ROT_MASKED:
            Pix<BPP>::put(d, x, c);
            break;
        case ROT_TRANS:
            if(BPP == 1)
                d[x] = map->data[c & 0xFF][d[x]];
            else
                Pix<BPP>::put(d, x, blend_trans(depth, c, Pix<BPP>::get(d, x)));
            break;
        case ROT_LIT:
            if(BPP == 1)
                d[x] = map->data[color & 0xFF][c & 0xFF];
            else
                Pix<BPP>::put(d, x, blend_lit(depth, c, color));
            break;
        }
    }
}

/* Sprite and destination of different depths (sprite not 8-bit): every
 * pixel goes through its r, g, b. */
template <int BPP>
void draw_span_convert(BITMAP *bmp, BITMAP *spr, int y, int x0, int x1, fixed u, fixed v,
                       fixed du, fixed dv, RotMode mode, int color, const uint32_t *)
{
    unsigned char *d = bmp->line[y];
    const int depth = depth_of(bmp), sdepth = depth_of(spr);
    const uint32_t mask = mask_of(bmp);   /* raw sprite pixel vs destination mask */
    const COLOR_MAP *map = color_map;
    const fixed wlim = spr->w << 16, hlim = spr->h << 16;
    for(int x = x0; x < x1; ++x, u += du, v += dv)
    {
        if(u < 0 || v < 0 || u >= wlim || v >= hlim)
            continue;
        uint32_t c = get_pixel(spr, u >> 16, v >> 16);
        if(c == mask)
            continue;
        int r, g, b;
        split_pixel(sdepth, c, r, g, b);
        c = make_pixel(depth, r, g, b);
        switch(mode)
        {
        case ROT_MASKED:
            Pix<BPP>::put(d, x, c);
            break;
        case ROT_TRANS:
            if(BPP == 1)
                d[x] = map->data[c & 0xFF][d[x]];
            else
                Pix<BPP>::put(d, x, blend_trans(depth, c, Pix<BPP>::get(d, x)));
            break;
        case ROT_LIT:
            if(BPP == 1)
                d[x] = map->data[color & 0xFF][c & 0xFF];
            else
                Pix<BPP>::put(d, x, blend_lit(depth, c, color));
            break;
        }
    }
}

typedef void (*SpanFn)(BITMAP *, BITMAP *, int, int, int, fixed, fixed, fixed, fixed, RotMode, int, const uint32_t *);

SpanFn pick_span(BITMAP *bmp, BITMAP *spr)
{
    const int db = bpp_of(bmp), sb = bpp_of(spr);
    if(depth_of(bmp) != depth_of(spr) && sb != 1)
    {
        switch(db)
        {
        case 1: return draw_span_convert<1>;
        case 2: return draw_span_convert<2>;
        case 3: return draw_span_convert<3>;
        default: return draw_span_convert<4>;
        }
    }
    if(sb == 1 && db != 1)
    {
        switch(db)
        {
        case 2: return draw_span<2, 1>;
        case 3: return draw_span<3, 1>;
        default: return draw_span<4, 1>;
        }
    }
    switch(db)
    {
    case 1: return draw_span<1, 1>;
    case 2: return draw_span<2, 2>;
    case 3: return draw_span<3, 3>;
    default: return draw_span<4, 4>;
    }
}

/* A corner of the parallelogram: destination position and the sprite
 * coordinates that land there (all 16.16). */
struct Corner
{
    fixed x, y, u, v;
};

/* One side of the parallelogram on the current scanline: where its boundary
 * crosses the scanline center, the sprite coordinates there, and how both
 * change from one scanline to the next. */
struct Side
{
    fixed x, dx;
    fixed u, du;
    fixed v, dv;
    int until;      /* first scanline that belongs to the next edge */
};

/* Starts the edge a->b at scanline 'row'. */
void side_start(Side &s, const Corner &a, const Corner &b, int row, bool with_sprite)
{
    const fixed gap = (row << 16) + 0x8000 - a.y;
    const fixed height = b.y - a.y;
    s.dx = fixdiv(b.x - a.x, height);
    s.x = a.x + fixmul(gap, s.dx);
    if(with_sprite)
    {
        s.du = fixdiv(b.u - a.u, height);
        s.u = a.u + fixmul(gap, s.du);
        s.dv = fixdiv(b.v - a.v, height);
        s.v = a.v + fixmul(gap, s.dv);
    }
    else
        s.u = s.du = s.v = s.dv = 0;
    s.until = (b.y + 0x8000) >> 16;
}

inline bool outside(fixed c, int size)
{
    return (unsigned)(c >> 16) >= (unsigned)size;
}

inline fixed wrap_mul(int n, fixed d)
{
    return (fixed)((uint32_t)n * (uint32_t)d);
}

/* Narrows the span [lpos, rpos] (16.16 positions of its first and last
 * pixel) so that the sprite coordinate c (at the first pixel, changing by d
 * per pixel) stays inside [0, size). 'other' is the other coordinate, which
 * moves along when the span start moves. Returns false when nothing is
 * left. */
bool keep_inside(fixed &lpos, fixed &rpos, fixed &c, fixed &other, fixed d, fixed dother, int size)
{
    if(outside(c, size))
    {
        if((c < 0 && d <= 0) || (c > 0 && d >= 0))
            return false;
        do
        {
            c += d;
            other += dother;
            lpos += 0x10000;
            if(lpos > rpos)
                return false;
        } while(outside(c, size));
    }
    fixed last = c + wrap_mul((rpos - lpos) >> 16, d);
    if(outside(last, size))
    {
        if((last < 0 && d <= 0) || (last > 0 && d >= 0))
        {
            do
            {
                rpos -= 0x10000;
                last -= d;
                if(lpos > rpos)
                    return false;
            } while(outside(last, size));
        }
        else
            return false;
    }
    return true;
}

/*
 * Maps the sprite onto the parallelogram whose corners px[i], py[i] are where
 * the sprite's corners (0,0), (w,0), (w,h), (0,h) land. The sprite side of
 * the far corners is one 1/65536 inside the sprite, so sampling never
 * leaves it.
 */
void map_parallelogram(BITMAP *bmp, BITMAP *spr, const fixed px[4], const fixed py[4],
                       RotMode mode, int color)
{
    if(mode != ROT_MASKED && depth_of(bmp) == 8 && !color_map)
        return;

    /* Topmost corner (the first one on ties) and the walking direction:
     * +1 if the next corner in index order is on the right. */
    int top = 0;
    for(int i = 1; i < 4; ++i)
        if(py[i] < py[top])
            top = i;
    const int nxt = (top + 1) & 3, prv = (top + 3) & 3;
    const int step = ((double)(px[nxt] - px[top]) * (double)(py[prv] - py[top]) >
                      (double)(px[prv] - px[top]) * (double)(py[nxt] - py[top])) ? 1 : 3;

    /* c[0] top, c[1] right neighbour, c[2] opposite, c[3] left neighbour. */
    Corner c[4];
    for(int i = 0, k = top; i < 4; ++i, k = (k + step) & 3)
    {
        c[i].x = px[k];
        c[i].y = py[k];
        c[i].u = (k == 1 || k == 2) ? (spr->w << 16) - 1 : 0;
        c[i].v = (k >= 2) ? (spr->h << 16) - 1 : 0;
    }

    int clip_top, clip_bot;
    fixed clip_left, clip_right;
    if(bmp->clip)
    {
        clip_top = bmp->ct;
        clip_bot = bmp->cb;
        clip_left = bmp->cl << 16;
        clip_right = (bmp->cr << 16) - 1;
    }
    else
    {
        clip_top = 0;
        clip_bot = bmp->h;
        clip_left = 0;
        clip_right = (bmp->w << 16) - 1;
    }

    int row = (c[0].y + 0x8000) >> 16;
    if(row < clip_top)
        row = clip_top;
    int bottom = (c[2].y + 0x8000) >> 16;
    if(bottom > clip_bot)
        bottom = clip_bot;
    if(row >= bottom || clip_left > clip_right)
        return;

    /* Sprite coordinate change per destination pixel to the right. */
    const fixed du = (fixed)((py[3] - py[0]) * 65536.0 * (65536.0 * spr->w) /
                             ((px[1] - px[0]) * (double)(py[3] - py[0]) -
                              (px[3] - px[0]) * (double)(py[1] - py[0])));
    const fixed dv = (fixed)((py[1] - py[0]) * 65536.0 * (65536.0 * spr->h) /
                             ((px[3] - px[0]) * (double)(py[1] - py[0]) -
                              (px[1] - px[0]) * (double)(py[3] - py[0])));

    uint32_t pal[256];
    if(bpp_of(spr) == 1 && bpp_of(bmp) != 1)
        palette_expansion(depth_of(bmp), pal);
    SpanFn span = pick_span(bmp, spr);

    Side left, right;
    side_start(left, c[0], c[3], row, true);
    if(left.until > bottom)
        left.until = bottom;
    side_start(right, c[0], c[1], row, false);
    bool left_second = false, right_second = false;

    for(;; ++row)
    {
        if(row >= left.until)
        {
            if(row >= bottom || left_second)
                break;
            side_start(left, c[3], c[2], row, true);
            if(left.until > bottom)
                left.until = bottom;
            left_second = true;
        }
        if(row >= right.until && !right_second)
        {
            side_start(right, c[1], c[2], row, false);
            right_second = true;
        }

        fixed lpos = (left.x + 0x8000) & ~0xFFFF;
        if(lpos < clip_left)
            lpos = clip_left;
        const fixed off = lpos + 0x7FFF - left.x;
        fixed u = left.u + fixmul(off, du);
        fixed v = left.v + fixmul(off, dv);
        fixed rpos = (right.x - 0x8000) | 0xFFFF;
        if(rpos > clip_right)
            rpos = clip_right;

        if(lpos <= rpos &&
           keep_inside(lpos, rpos, u, v, du, dv, spr->w) &&
           keep_inside(lpos, rpos, v, u, dv, du, spr->h))
        {
            span(bmp, spr, row, lpos >> 16, (rpos >> 16) + 1, u, v, du, dv, mode, color, pal);
        }

        left.x += left.dx;
        left.u += left.du;
        left.v += left.dv;
        right.x += right.dx;
    }
}

/* Where the sprite's corners land for a rotation around (cx, cy) of the
 * sprite placed at (x, y) of the bitmap (all 16.16). */
void pivot_scaled_flip(BITMAP *bmp, BITMAP *spr, fixed x, fixed y, fixed cx, fixed cy,
                       fixed angle, fixed scale, bool vflip, RotMode mode, int color)
{
    if(!bmp || !spr)
        return;

    /* The angle (256 = full turn) taken into [-128, 128), then real sine
     * and cosine (not the fixsin/fixcos tables) rounded to 16.16. */
    int a = (int)(angle & 0xFFFFFF);
    if(a >= 0x800000)
        a -= 0x1000000;
    const double rad = a * (3.14159265358979323846 / (double)0x800000);
    const double dc = cos(rad), ds = sin(rad);
    const fixed c = (fixed)(dc >= 0 ? dc * 0x10000 + 0.5 : dc * 0x10000 - 0.5);
    const fixed s = (fixed)(ds >= 0 ? ds * 0x10000 + 0.5 : ds * 0x10000 - 0.5);

    /* The scale applies to the sprite's size and pivot. */
    const fixed w = fixmul(itofix(spr->w), scale), h = fixmul(itofix(spr->h), scale);
    cx = fixmul(cx, scale);
    cy = fixmul(cy, scale);

    /* Destination of the sprite's top-left, top-right and bottom-left
     * corners as drawn (before flipping). */
    const fixed x0 = x - fixmul(cx, c) + fixmul(cy, s);
    const fixed y0 = y - fixmul(cx, s) - fixmul(cy, c);
    const fixed x1 = x0 + fixmul(w, c);
    const fixed y1 = y0 + fixmul(w, s);
    const fixed x3 = x0 - fixmul(h, s);
    const fixed y3 = y0 + fixmul(h, c);
    const fixed x2 = x1 + x3 - x0;
    const fixed y2 = y1 + y3 - y0;

    fixed px[4], py[4];
    if(!vflip)
    {
        px[0] = x0; py[0] = y0;
        px[1] = x1; py[1] = y1;
        px[2] = x2; py[2] = y2;
        px[3] = x3; py[3] = y3;
    }
    else
    {
        /* sprite row 0 lands where the last row would */
        px[0] = x3; py[0] = y3;
        px[1] = x2; py[1] = y2;
        px[2] = x1; py[2] = y1;
        px[3] = x0; py[3] = y0;
    }
    map_parallelogram(bmp, spr, px, py, mode, color);
}

/* rotate_*: the sprite's top-left at (x, y), rotated around its center. */
void rotate_common(BITMAP *bmp, BITMAP *spr, int x, int y, fixed angle, fixed scale,
                   bool vflip, RotMode mode, int color)
{
    if(!spr)
        return;
    pivot_scaled_flip(bmp, spr, (x << 16) + (spr->w * scale) / 2, (y << 16) + (spr->h * scale) / 2,
                      spr->w << 15, spr->h << 15, angle, scale, vflip, mode, color);
}

void pivot_common(BITMAP *bmp, BITMAP *spr, int x, int y, int cx, int cy, fixed angle,
                  fixed scale, bool vflip, RotMode mode, int color)
{
    pivot_scaled_flip(bmp, spr, itofix(x), itofix(y), itofix(cx), itofix(cy), angle, scale,
                      vflip, mode, color);
}

} // namespace

extern "C" {

void rotate_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle)
{
    rotate_common(bmp, sprite, x, y, angle, itofix(1), false, ROT_MASKED, 0);
}

void rotate_sprite_v_flip(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle)
{
    rotate_common(bmp, sprite, x, y, angle, itofix(1), true, ROT_MASKED, 0);
}

void rotate_scaled_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle, fixed scale)
{
    rotate_common(bmp, sprite, x, y, angle, scale, false, ROT_MASKED, 0);
}

void rotate_scaled_sprite_v_flip(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle, fixed scale)
{
    rotate_common(bmp, sprite, x, y, angle, scale, true, ROT_MASKED, 0);
}

void pivot_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, itofix(1), false, ROT_MASKED, 0);
}

void pivot_sprite_v_flip(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, itofix(1), true, ROT_MASKED, 0);
}

void pivot_scaled_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle, fixed scale)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, scale, false, ROT_MASKED, 0);
}

void pivot_scaled_sprite_v_flip(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle, fixed scale)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, scale, true, ROT_MASKED, 0);
}

void rotate_sprite_trans(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle)
{
    rotate_common(bmp, sprite, x, y, angle, itofix(1), false, ROT_TRANS, 0);
}

void rotate_sprite_v_flip_trans(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle)
{
    rotate_common(bmp, sprite, x, y, angle, itofix(1), true, ROT_TRANS, 0);
}

void rotate_scaled_sprite_trans(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle, fixed scale)
{
    rotate_common(bmp, sprite, x, y, angle, scale, false, ROT_TRANS, 0);
}

void rotate_scaled_sprite_v_flip_trans(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle, fixed scale)
{
    rotate_common(bmp, sprite, x, y, angle, scale, true, ROT_TRANS, 0);
}

void pivot_sprite_trans(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, itofix(1), false, ROT_TRANS, 0);
}

void pivot_sprite_v_flip_trans(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, itofix(1), true, ROT_TRANS, 0);
}

void pivot_scaled_sprite_trans(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle, fixed scale)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, scale, false, ROT_TRANS, 0);
}

void pivot_scaled_sprite_v_flip_trans(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle, fixed scale)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, scale, true, ROT_TRANS, 0);
}

void rotate_sprite_lit(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle, int color)
{
    rotate_common(bmp, sprite, x, y, angle, itofix(1), false, ROT_LIT, color);
}

void rotate_sprite_v_flip_lit(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle, int color)
{
    rotate_common(bmp, sprite, x, y, angle, itofix(1), true, ROT_LIT, color);
}

void rotate_scaled_sprite_lit(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle, fixed scale, int color)
{
    rotate_common(bmp, sprite, x, y, angle, scale, false, ROT_LIT, color);
}

void rotate_scaled_sprite_v_flip_lit(BITMAP *bmp, BITMAP *sprite, int x, int y, fixed angle, fixed scale, int color)
{
    rotate_common(bmp, sprite, x, y, angle, scale, true, ROT_LIT, color);
}

void pivot_sprite_lit(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle, int color)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, itofix(1), false, ROT_LIT, color);
}

void pivot_sprite_v_flip_lit(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle, int color)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, itofix(1), true, ROT_LIT, color);
}

void pivot_scaled_sprite_lit(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle, fixed scale, int color)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, scale, false, ROT_LIT, color);
}

void pivot_scaled_sprite_v_flip_lit(BITMAP *bmp, BITMAP *sprite, int x, int y, int cx, int cy, fixed angle, fixed scale, int color)
{
    pivot_common(bmp, sprite, x, y, cx, cy, angle, scale, true, ROT_LIT, color);
}

} // extern "C"

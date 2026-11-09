/*
 * zcport blit module: blit() and masked_blit().
 *
 * Both clip the source rectangle to the source bitmap and the destination
 * rectangle to the destination's clip rectangle, then copy. blit() handles
 * overlapping areas (same bitmap, or sub-bitmaps sharing memory) like a copy
 * through a temporary buffer and converts between color depths
 * (blit_convert.cpp). Checked against the reference build by
 * tests/conformance/test_blit_core.cpp.
 *
 * Written from scratch for zcport.
 */

#include "blit_internal.h"

using namespace zcblit;

namespace
{

/* Clips a blit rectangle: the source to the source bitmap, the destination
 * to the destination clip rectangle. Returns false when nothing is left. */
bool clip_rect(BITMAP *src, BITMAP *dst, int &sx, int &sy, int &dx, int &dy, int &w, int &h)
{
    if(sx >= src->w || sy >= src->h || dx >= dst->cr || dy >= dst->cb)
        return false;

    if(sx < 0)
    {
        w += sx;
        dx -= sx;
        sx = 0;
    }
    if(sy < 0)
    {
        h += sy;
        dy -= sy;
        sy = 0;
    }
    if(sx + w > src->w)
        w = src->w - sx;
    if(sy + h > src->h)
        h = src->h - sy;

    if(dx < dst->cl)
    {
        int d = dst->cl - dx;
        w -= d;
        sx += d;
        dx = dst->cl;
    }
    if(dy < dst->ct)
    {
        int d = dst->ct - dy;
        h -= d;
        sy += d;
        dy = dst->ct;
    }
    if(dx + w > dst->cr)
        w = dst->cr - dx;
    if(dy + h > dst->cb)
        h = dst->cb - dy;

    return w > 0 && h > 0;
}

template <int BPP>
void masked_rows(BITMAP *src, BITMAP *dst, int sx, int sy, int dx, int dy, int w, int h)
{
    const uint32_t mask = mask_of(src);
    for(int y = 0; y < h; ++y)
    {
        const unsigned char *s = src->line[sy + y];
        unsigned char *d = dst->line[dy + y];
        for(int x = 0; x < w; ++x)
        {
            uint32_t c = Pix<BPP>::get(s, sx + x);
            if(c != mask)
                Pix<BPP>::put(d, dx + x, c);
        }
    }
}

} // namespace

extern "C" void blit(BITMAP *src, BITMAP *dst, int sx, int sy, int dx, int dy, int w, int h)
{
    if(!src || !dst)
        return;
    if(!clip_rect(src, dst, sx, sy, dx, dy, w, h))
        return;

    if(depth_of(src) != depth_of(dst))
    {
        blit_convert(src, dst, sx, sy, dx, dy, w, h);
        return;
    }

    const int bpp = bpp_of(src);
    const size_t bytes = (size_t)w * bpp;

    /* Rows may overlap when both bitmaps share memory: copy in the order
     * that never overwrites a row before it is read. memmove handles the
     * overlap inside one row. */
    const uintptr_t first_src = (uintptr_t)(src->line[sy] + sx * bpp);
    const uintptr_t first_dst = (uintptr_t)(dst->line[dy] + dx * bpp);
    if(first_dst > first_src)
    {
        for(int y = h - 1; y >= 0; --y)
            memmove(dst->line[dy + y] + dx * bpp, src->line[sy + y] + sx * bpp, bytes);
    }
    else
    {
        for(int y = 0; y < h; ++y)
            memmove(dst->line[dy + y] + dx * bpp, src->line[sy + y] + sx * bpp, bytes);
    }
}

extern "C" void masked_blit(BITMAP *src, BITMAP *dst, int sx, int sy, int dx, int dy, int w, int h)
{
    if(!src || !dst)
        return;
    if(!clip_rect(src, dst, sx, sy, dx, dy, w, h))
        return;

    switch(bpp_of(dst))
    {
    case 1: masked_rows<1>(src, dst, sx, sy, dx, dy, w, h); break;
    case 2: masked_rows<2>(src, dst, sx, sy, dx, dy, w, h); break;
    case 3: masked_rows<3>(src, dst, sx, sy, dx, dy, w, h); break;
    default: masked_rows<4>(src, dst, sx, sy, dx, dy, w, h); break;
    }
}

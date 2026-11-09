/*
 * zcport blit module: stretch_blit(), masked_stretch_blit(), stretch_sprite().
 *
 * Destination pixel i (counted from the unclipped destination origin) takes
 * the source pixel floor(i * source_size / dest_size), in both directions.
 * Only the destination is clipped (to its clip rectangle when clipping is
 * on); the source rectangle must lie inside the source bitmap, as Allegro
 * requires. Checked against the reference build by test_blit_core.cpp.
 *
 * Written from scratch for zcport.
 */

#include "blit_internal.h"

#include <vector>

using namespace zcblit;

namespace
{

template <int BPP, bool MASKED>
void stretch_rows(BITMAP *src, BITMAP *dst, int sx, int sy, int sw, int sh,
                  int dx, int dy, int dw, int dh, int x0, int x1, int y0, int y1)
{
    const uint32_t mask = mask_of(src);

    /* Source column of every visible destination column. */
    std::vector<int> cols((size_t)(x1 - x0));
    {
        long long first = (long long)(x0 - dx) * sw;
        int col = sx + (int)(first / dw);
        int rem = (int)(first % dw);
        const int step = sw / dw, extra = sw % dw;
        for(int x = x0; x < x1; ++x)
        {
            cols[(size_t)(x - x0)] = col;
            col += step;
            rem += extra;
            if(rem >= dw)
            {
                rem -= dw;
                ++col;
            }
        }
    }

    long long firsty = (long long)(y0 - dy) * sh;
    int row = sy + (int)(firsty / dh);
    int rem = (int)(firsty % dh);
    const int ystep = sh / dh, yextra = sh % dh;

    for(int y = y0; y < y1; ++y)
    {
        const unsigned char *s = src->line[row];
        unsigned char *d = dst->line[y];
        for(int x = x0; x < x1; ++x)
        {
            uint32_t c = Pix<BPP>::get(s, cols[(size_t)(x - x0)]);
            if(!MASKED || c != mask)
                Pix<BPP>::put(d, x, c);
        }
        row += ystep;
        rem += yextra;
        if(rem >= dh)
        {
            rem -= dh;
            ++row;
        }
    }
}

void do_stretch(BITMAP *src, BITMAP *dst, int sx, int sy, int sw, int sh,
                int dx, int dy, int dw, int dh, bool masked)
{
    if(!src || !dst)
        return;
    if(sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;

    int x0 = dx, x1 = dx + dw, y0 = dy, y1 = dy + dh;
    if(dst->clip)
    {
        if(x0 < dst->cl) x0 = dst->cl;
        if(x1 > dst->cr) x1 = dst->cr;
        if(y0 < dst->ct) y0 = dst->ct;
        if(y1 > dst->cb) y1 = dst->cb;
    }
    if(x0 >= x1 || y0 >= y1)
        return;

#define ZCBLIT_STRETCH(B)                                                         \
    if(masked)                                                                \
        stretch_rows<B, true>(src, dst, sx, sy, sw, sh, dx, dy, dw, dh, x0, x1, y0, y1); \
    else                                                                      \
        stretch_rows<B, false>(src, dst, sx, sy, sw, sh, dx, dy, dw, dh, x0, x1, y0, y1)

    switch(bpp_of(dst))
    {
    case 1: ZCBLIT_STRETCH(1); break;
    case 2: ZCBLIT_STRETCH(2); break;
    case 3: ZCBLIT_STRETCH(3); break;
    default: ZCBLIT_STRETCH(4); break;
    }
#undef ZCBLIT_STRETCH
}

} // namespace

extern "C" void stretch_blit(BITMAP *s, BITMAP *d, int s_x, int s_y, int s_w, int s_h,
                             int d_x, int d_y, int d_w, int d_h)
{
    do_stretch(s, d, s_x, s_y, s_w, s_h, d_x, d_y, d_w, d_h, false);
}

extern "C" void masked_stretch_blit(BITMAP *s, BITMAP *d, int s_x, int s_y, int s_w, int s_h,
                                    int d_x, int d_y, int d_w, int d_h)
{
    do_stretch(s, d, s_x, s_y, s_w, s_h, d_x, d_y, d_w, d_h, true);
}

extern "C" void stretch_sprite(BITMAP *bmp, BITMAP *sprite, int x, int y, int w, int h)
{
    if(!sprite)
        return;
    do_stretch(sprite, bmp, 0, 0, sprite->w, sprite->h, x, y, w, h, true);
}

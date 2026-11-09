/*
 * zcport blit module: RLE sprites.
 *
 * Encoding of RLE_SPRITE->dat (the format Allegro datafiles store, so the
 * datafile loader can fill it directly; 'size' is the byte count):
 *   - a stream of counts in the sprite's word size: signed 8-bit for 8 bpp,
 *     signed 16-bit for 15/16 bpp, signed 32-bit for 24/32 bpp;
 *   - each row is a sequence of runs: a positive count n is followed by n
 *     pixels (one word each: 24-bit pixels are stored as 32-bit 0x00RRGGBB
 *     words), a negative count -n skips n transparent pixels;
 *   - every row ends with a marker: 0 for 8 bpp, otherwise the depth's mask
 *     color (MASK_COLOR_15/16/24/32) as a word. The marker is tested before
 *     the word is read as a count.
 * get_rle_sprite() emits solid runs of at most 127 pixels and transparent
 * runs of at most 128, keeps trailing transparent runs, like the reference
 * encoder (test_blit_rle checks the bytes). The drawers accept any run
 * split. Sprites are allocated with _AL_MALLOC(sizeof(RLE_SPRITE) + size)
 * and freed with _AL_FREE by destroy_rle_sprite().
 *
 * Written from scratch for zcport.
 */

#include "blit_internal.h"

#include <vector>

using namespace zcblit;

namespace
{

const int kMaxSolid = 127;
const int kMaxSkip = 128;

/* Word size of the encoded stream for a color depth. */
int word_size(int depth)
{
    switch(depth)
    {
    case 8: return 1;
    case 15:
    case 16: return 2;
    default: return 4;
    }
}

uint32_t eol_marker(int depth)
{
    return depth == 8 ? 0u : mask_of_depth(depth);
}

void put_word(std::vector<unsigned char> &out, int ws, uint32_t v)
{
    unsigned char b[4];
    memcpy(b, &v, 4); /* little-endian host */
    if(ws == 1)
        out.push_back(b[0]);
    else if(ws == 2)
    {
        uint16_t s = (uint16_t)v;
        unsigned char t[2];
        memcpy(t, &s, 2);
        out.push_back(t[0]);
        out.push_back(t[1]);
    }
    else
        out.insert(out.end(), b, b + 4);
}

/* Reads a word: the raw (zero-extended) value and the signed count. */
struct Word
{
    uint32_t raw;
    int count;
};

inline Word read_word(const unsigned char *&p, int ws)
{
    Word w;
    if(ws == 1)
    {
        w.raw = *p;
        w.count = (signed char)*p;
        p += 1;
    }
    else if(ws == 2)
    {
        uint16_t v;
        memcpy(&v, p, 2);
        w.raw = v;
        w.count = (int16_t)v;
        p += 2;
    }
    else
    {
        uint32_t v;
        memcpy(&v, p, 4);
        w.raw = v;
        w.count = (int32_t)v;
        p += 4;
    }
    return w;
}

enum RleMode
{
    RLE_SOLID,
    RLE_TRANS,
    RLE_LIT
};

/* Draws an RLE sprite whose depth equals the destination's. */
template <int BPP, int WS>
void draw_rle(BITMAP *bmp, const RLE_SPRITE *spr, int x, int y, RleMode mode, int color)
{
    const int depth = depth_of(bmp);
    const uint32_t eol = eol_marker(spr->color_depth);

    int x0 = x, x1 = x + spr->w, y0 = y, y1 = y + spr->h;
    if(bmp->clip)
    {
        if(x0 < bmp->cl) x0 = bmp->cl;
        if(x1 > bmp->cr) x1 = bmp->cr;
        if(y0 < bmp->ct) y0 = bmp->ct;
        if(y1 > bmp->cb) y1 = bmp->cb;
    }
    if(x0 >= x1 || y0 >= y1)
        return;

    const COLOR_MAP *map = color_map;
    if(depth == 8 && mode != RLE_SOLID && !map)
        return;

    /* 15/16-bit RLE data is signed 16-bit: the reference hands its pixels to
     * the blenders sign-extended. */
    struct
    {
        uint32_t operator()(uint32_t c) const { return WS == 2 ? (uint32_t)(int32_t)(int16_t)c : c; }
    } sext;

    const unsigned char *p = (const unsigned char *)spr->dat;
    for(int row = 0; row < spr->h && y + row < y1; ++row)
    {
        const int dy = y + row;
        const bool visible = dy >= y0;
        unsigned char *d = visible ? bmp->line[dy] : NULL;
        int dx = x;
        for(;;)
        {
            Word w = read_word(p, WS);
            if(w.raw == eol)
                break;
            if(w.count < 0)
            {
                dx -= w.count;
                continue;
            }
            for(int i = 0; i < w.count; ++i, ++dx)
            {
                uint32_t c;
                if(WS == 1)
                    c = p[0];
                else if(WS == 2)
                {
                    uint16_t v;
                    memcpy(&v, p, 2);
                    c = v;
                }
                else
                {
                    memcpy(&c, p, 4);
                }
                p += WS;
                if(!visible || dx < x0 || dx >= x1)
                    continue;
                switch(mode)
                {
                case RLE_SOLID:
                    Pix<BPP>::put(d, dx, c);
                    break;
                case RLE_TRANS:
                    if(BPP == 1)
                        d[dx] = map->data[c & 0xFF][d[dx]];
                    else
                        Pix<BPP>::put(d, dx, blend_trans(depth, sext(c), Pix<BPP>::get(d, dx)));
                    break;
                case RLE_LIT:
                    if(BPP == 1)
                        d[dx] = map->data[color & 0xFF][c & 0xFF];
                    else
                        Pix<BPP>::put(d, dx, blend_lit(depth, sext(c), color));
                    break;
                }
            }
        }
    }
}

void draw_any(BITMAP *bmp, const RLE_SPRITE *spr, int x, int y, RleMode mode, int color)
{
    if(!bmp || !spr)
        return;
    if(depth_of(bmp) != spr->color_depth)
        return; /* Allegro requires matching depths */
    switch(depth_of(bmp))
    {
    case 8: draw_rle<1, 1>(bmp, spr, x, y, mode, color); break;
    case 15:
    case 16: draw_rle<2, 2>(bmp, spr, x, y, mode, color); break;
    case 24: draw_rle<3, 4>(bmp, spr, x, y, mode, color); break;
    default: draw_rle<4, 4>(bmp, spr, x, y, mode, color); break;
    }
}

} // namespace

extern "C" RLE_SPRITE *get_rle_sprite(BITMAP *bitmap)
{
    if(!bitmap)
        return NULL;

    const int depth = depth_of(bitmap);
    const int ws = word_size(depth);
    const uint32_t mask = mask_of_depth(depth);
    const uint32_t eol = eol_marker(depth);

    std::vector<unsigned char> out;
    out.reserve((size_t)(bitmap->w + 2) * bitmap->h * ws);

    for(int y = 0; y < bitmap->h; ++y)
    {
        int x = 0;
        while(x < bitmap->w)
        {
            if(get_pixel(bitmap, x, y) == mask)
            {
                int n = 0;
                while(x < bitmap->w && n < kMaxSkip && get_pixel(bitmap, x, y) == mask)
                {
                    ++n;
                    ++x;
                }
                put_word(out, ws, (uint32_t)(-n));
            }
            else
            {
                int start = x, n = 0;
                while(x < bitmap->w && n < kMaxSolid && get_pixel(bitmap, x, y) != mask)
                {
                    ++n;
                    ++x;
                }
                put_word(out, ws, (uint32_t)n);
                for(int i = start; i < start + n; ++i)
                    put_word(out, ws, get_pixel(bitmap, i, y));
            }
        }
        put_word(out, ws, eol);
    }

    RLE_SPRITE *s = (RLE_SPRITE *)_AL_MALLOC(sizeof(RLE_SPRITE) + out.size());
    if(!s)
        return NULL;
    s->w = bitmap->w;
    s->h = bitmap->h;
    s->color_depth = depth;
    s->size = (int)out.size();
    if(!out.empty())
        memcpy(s->dat, out.data(), out.size());
    return s;
}

extern "C" void destroy_rle_sprite(RLE_SPRITE *sprite)
{
    if(sprite)
        _AL_FREE(sprite);
}

extern "C" void draw_rle_sprite(BITMAP *bmp, AL_CONST RLE_SPRITE *sprite, int x, int y)
{
    draw_any(bmp, sprite, x, y, RLE_SOLID, 0);
}

extern "C" void draw_trans_rle_sprite(BITMAP *bmp, AL_CONST RLE_SPRITE *sprite, int x, int y)
{
    draw_any(bmp, sprite, x, y, RLE_TRANS, 0);
}

extern "C" void draw_lit_rle_sprite(BITMAP *bmp, AL_CONST RLE_SPRITE *sprite, int x, int y, int color)
{
    draw_any(bmp, sprite, x, y, RLE_LIT, color);
}

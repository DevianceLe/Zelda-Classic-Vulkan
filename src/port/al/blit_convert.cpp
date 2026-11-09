/*
 * zcport blit module: pixel formats and color depth conversion.
 *
 * Hicolor and truecolor pixels are packed and unpacked through the
 * _rgb_*_shift_* variables (like makecol_depth / getr_depth), so the layout
 * follows whatever the color / graphics mode modules set; 8-bit pixels
 * expand through the current palette, and reduce to 8-bit with makecol8().
 * Checked against the reference build by test_blit_depths.cpp.
 *
 * Written from scratch for zcport.
 */

#include "blit_internal.h"

using namespace zcblit;

/*
 * The variables and makecol8() below belong to the color module. Until that
 * module is linked in, MSVC resolves their names to these stand-ins
 * (/alternatename is only used when no other definition exists), with
 * Allegro's values before any graphics mode is set. This keeps the blit
 * module and its tests linkable on their own; the real definitions win as
 * soon as they exist.
 */
#if defined(_MSC_VER)
extern "C" {
RGB_MAP *zcblit_standin_rgb_map = NULL;
int zcblit_standin_r15 = 0, zcblit_standin_g15 = 5, zcblit_standin_b15 = 10;
int zcblit_standin_r16 = 0, zcblit_standin_g16 = 5, zcblit_standin_b16 = 11;
int zcblit_standin_r24 = 0, zcblit_standin_g24 = 8, zcblit_standin_b24 = 16;
int zcblit_standin_r32 = 0, zcblit_standin_g32 = 8, zcblit_standin_b32 = 16, zcblit_standin_a32 = 24;

/* Only for linking without the color module: rgb_map, else the first
 * closest palette entry. */
int zcblit_standin_makecol8(int r, int g, int b)
{
    if(rgb_map)
        return rgb_map->data[r >> 3][g >> 3][b >> 3];
    int best = 0;
    long bestd = 0x7FFFFFFF;
    for(int i = 0; i < PAL_SIZE; ++i)
    {
        long dr = _current_palette[i].r - (r >> 2), dg = _current_palette[i].g - (g >> 2),
             db = _current_palette[i].b - (b >> 2);
        long d = dr * dr + dg * dg + db * db;
        if(d < bestd)
        {
            bestd = d;
            best = i;
        }
    }
    return best;
}
}
#if defined(_M_IX86)
#define ZCBLIT_ALT(name, standin) __pragma(comment(linker, "/alternatename:_" #name "=_" #standin))
#else
#define ZCBLIT_ALT(name, standin) __pragma(comment(linker, "/alternatename:" #name "=" #standin))
#endif
ZCBLIT_ALT(rgb_map, zcblit_standin_rgb_map)
ZCBLIT_ALT(_rgb_r_shift_15, zcblit_standin_r15)
ZCBLIT_ALT(_rgb_g_shift_15, zcblit_standin_g15)
ZCBLIT_ALT(_rgb_b_shift_15, zcblit_standin_b15)
ZCBLIT_ALT(_rgb_r_shift_16, zcblit_standin_r16)
ZCBLIT_ALT(_rgb_g_shift_16, zcblit_standin_g16)
ZCBLIT_ALT(_rgb_b_shift_16, zcblit_standin_b16)
ZCBLIT_ALT(_rgb_r_shift_24, zcblit_standin_r24)
ZCBLIT_ALT(_rgb_g_shift_24, zcblit_standin_g24)
ZCBLIT_ALT(_rgb_b_shift_24, zcblit_standin_b24)
ZCBLIT_ALT(_rgb_r_shift_32, zcblit_standin_r32)
ZCBLIT_ALT(_rgb_g_shift_32, zcblit_standin_g32)
ZCBLIT_ALT(_rgb_b_shift_32, zcblit_standin_b32)
ZCBLIT_ALT(_rgb_a_shift_32, zcblit_standin_a32)
ZCBLIT_ALT(makecol8, zcblit_standin_makecol8)
#endif

namespace zcblit
{

uint32_t make_pixel(int depth, int r, int g, int b)
{
    switch(depth)
    {
    case 8:
        return (uint32_t)makecol8(r, g, b);
    case 15:
        return (uint32_t)(((r >> 3) << _rgb_r_shift_15) | ((g >> 3) << _rgb_g_shift_15) |
                          ((b >> 3) << _rgb_b_shift_15));
    case 16:
        return (uint32_t)(((r >> 3) << _rgb_r_shift_16) | ((g >> 2) << _rgb_g_shift_16) |
                          ((b >> 3) << _rgb_b_shift_16));
    case 24:
        return (uint32_t)((r << _rgb_r_shift_24) | (g << _rgb_g_shift_24) | (b << _rgb_b_shift_24));
    default:
        return (uint32_t)((r << _rgb_r_shift_32) | (g << _rgb_g_shift_32) | (b << _rgb_b_shift_32));
    }
}

void split_pixel(int depth, uint32_t c, int &r, int &g, int &b)
{
    switch(depth)
    {
    case 8:
        r = scale6(_current_palette[c & 0xFF].r);
        g = scale6(_current_palette[c & 0xFF].g);
        b = scale6(_current_palette[c & 0xFF].b);
        break;
    case 15:
        r = scale5((int)(c >> _rgb_r_shift_15));
        g = scale5((int)(c >> _rgb_g_shift_15));
        b = scale5((int)(c >> _rgb_b_shift_15));
        break;
    case 16:
        r = scale5((int)(c >> _rgb_r_shift_16));
        g = scale6((int)(c >> _rgb_g_shift_16));
        b = scale5((int)(c >> _rgb_b_shift_16));
        break;
    case 24:
        r = (int)(c >> _rgb_r_shift_24) & 255;
        g = (int)(c >> _rgb_g_shift_24) & 255;
        b = (int)(c >> _rgb_b_shift_24) & 255;
        break;
    default:
        r = (int)(c >> _rgb_r_shift_32) & 255;
        g = (int)(c >> _rgb_g_shift_32) & 255;
        b = (int)(c >> _rgb_b_shift_32) & 255;
        break;
    }
}

void palette_expansion(int depth, uint32_t table[256])
{
    for(int i = 0; i < 256; ++i)
    {
        const RGB &c = _current_palette[i];
        table[i] = make_pixel(depth, scale6(c.r), scale6(c.g), scale6(c.b));
    }
}

/* With COLORCONV_KEEP_TRANS, what a visible pixel that would become the
 * destination's mask color turns into instead: the mask color with the
 * lowest green bit set (for 8-bit, the palette color makecol8() picks for
 * that pink). */
static uint32_t not_quite_mask(int depth)
{
    switch(depth)
    {
    case 8: return (uint32_t)makecol8(255, 8, 255);
    case 15: return MASK_COLOR_15 | (1u << _rgb_g_shift_15);
    case 16: return MASK_COLOR_16 | (1u << _rgb_g_shift_16);
    case 24: return MASK_COLOR_24 | (1u << _rgb_g_shift_24);
    default: return MASK_COLOR_32 | (1u << _rgb_g_shift_32);
    }
}

void blit_convert(BITMAP *src, BITMAP *dst, int sx, int sy, int dx, int dy, int w, int h)
{
    const int sd = depth_of(src), dd = depth_of(dst);
    const bool keep_trans = (get_color_conversion() & COLORCONV_KEEP_TRANS) != 0;
    const uint32_t smask = mask_of_depth(sd), dmask = mask_of_depth(dd);
    const uint32_t substitute = keep_trans ? not_quite_mask(dd) : 0;

    uint32_t pal[256];
    if(sd == 8)
        palette_expansion(dd, pal);

    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
        {
            const uint32_t c = get_pixel(src, sx + x, sy + y);
            uint32_t out;
            if(keep_trans && c == smask)
                out = dmask;
            else
            {
                if(sd == 8)
                    out = pal[c];
                else
                {
                    int r, g, b;
                    split_pixel(sd, c, r, g, b);
                    out = make_pixel(dd, r, g, b);
                }
                if(keep_trans && out == dmask)
                    out = substitute;
            }
            put_pixel(dst, dx + x, dy + y, out);
        }
}

} // namespace zcblit

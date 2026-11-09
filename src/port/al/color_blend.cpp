/*
 * zcport color module: truecolor blender state.
 *
 * A blender combines a source color x with a destination color y using
 * the factor n (0..255): translucent drawing on 15/16/24/32-bit bitmaps
 * calls blender(x = source, y = destination, n = _blender_alpha). The
 * color module installs zcport_trans_blender (zcport/draw.h) whenever a
 * blender mode is set, so the primitives and sprite modules can blend
 * through it.
 *
 * The trans blenders interpolate y + (x - y) * f / scale on all components
 * at once, with the components spread out in one 32-bit word (green
 * shifted up by 16 for 15/16 bits, red/blue together and green apart for
 * 24/32 bits). Borrows between the spread components happen exactly as
 * in the reference, so results are bit-identical, including inputs with
 * stray high bits (checked black-box against the reference build).
 *
 * Written from scratch for zcport.
 */

#include "color_internal.h"
#include "zcport/draw.h"

extern "C" {

BLENDER_FUNC _blender_func15 = NULL;
BLENDER_FUNC _blender_func16 = NULL;
BLENDER_FUNC _blender_func24 = NULL;
BLENDER_FUNC _blender_func32 = NULL;
BLENDER_FUNC _blender_func15x = NULL;
BLENDER_FUNC _blender_func16x = NULL;
BLENDER_FUNC _blender_func24x = NULL;
int _blender_col_15 = 0;
int _blender_col_16 = 0;
int _blender_col_24 = 0;
int _blender_col_32 = 0;
int _blender_alpha = 0;

} // extern "C"

namespace
{

/* 15/16-bit: f = 0..32 from n = 0..255; components spread so that each
 * one has five spare bits above it for the multiplication. */
inline unsigned long hicolor_interp(unsigned long x, unsigned long y, unsigned long n, unsigned long spread_mask)
{
    unsigned long f = n ? (n + 1) / 8 : 0;
    unsigned long xs = ((x & 0xFFFF) | (x << 16)) & spread_mask;
    unsigned long ys = ((y & 0xFFFF) | (y << 16)) & spread_mask;
    unsigned long res = ((xs - ys) * f / 32 + ys) & spread_mask;
    return (res & 0xFFFF) | (res >> 16);
}

unsigned long trans15(unsigned long x, unsigned long y, unsigned long n)
{
    return hicolor_interp(x, y, n, 0x03E07C1FUL);
}

unsigned long trans16(unsigned long x, unsigned long y, unsigned long n)
{
    return hicolor_interp(x, y, n, 0x07E0F81FUL);
}

/* 24/32-bit: f = n + 1 (0 stays 0) over 256; red and blue share one
 * word, green is done separately. */
unsigned long trans24(unsigned long x, unsigned long y, unsigned long n)
{
    unsigned long f = n ? n + 1 : 0;
    unsigned long rb = (((x & 0xFF00FFUL) - (y & 0xFF00FFUL)) * f / 256 + y) & 0xFF00FFUL;
    unsigned long g = (((x & 0xFF00UL) - (y & 0xFF00UL)) * f / 256 + (y & 0xFF00UL)) & 0xFF00UL;
    return rb | g;
}

/* The "x" blenders (RGBA source onto another depth) of the normal modes. */
unsigned long blend_black(unsigned long x, unsigned long y, unsigned long n)
{
    (void)x; (void)y; (void)n;
    return 0;
}

unsigned long dispatch(int depth, unsigned long src, unsigned long dest)
{
    BLENDER_FUNC f = NULL;
    switch(depth)
    {
    case 15: f = _blender_func15; break;
    case 16: f = _blender_func16; break;
    case 24: f = _blender_func24; break;
    case 32: f = _blender_func32; break;
    }
    if(!f)
        return src;
    return f(src, dest, (unsigned long)_blender_alpha);
}

} // namespace

extern "C" {

void set_blender_mode_ex(BLENDER_FUNC b15, BLENDER_FUNC b16, BLENDER_FUNC b24, BLENDER_FUNC b32,
                         BLENDER_FUNC b15x, BLENDER_FUNC b16x, BLENDER_FUNC b24x,
                         int r, int g, int b, int a)
{
    _blender_func15 = b15;
    _blender_func16 = b16;
    _blender_func24 = b24;
    _blender_func32 = b32;
    _blender_func15x = b15x;
    _blender_func16x = b16x;
    _blender_func24x = b24x;
    _blender_col_15 = makecol15(r, g, b);
    _blender_col_16 = makecol16(r, g, b);
    _blender_col_24 = makecol24(r, g, b);
    _blender_col_32 = makecol32(r, g, b);
    _blender_alpha = a;
    zcport_trans_blender = dispatch;
}

void set_blender_mode(BLENDER_FUNC b15, BLENDER_FUNC b16, BLENDER_FUNC b24, int r, int g, int b, int a)
{
    set_blender_mode_ex(b15, b16, b24, b24, blend_black, blend_black, blend_black, r, g, b, a);
}

void set_trans_blender(int r, int g, int b, int a)
{
    set_blender_mode(trans15, trans16, trans24, r, g, b, a);
}

} // extern "C"

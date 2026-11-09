/*
 * zcport: color conversion mode (set_color_conversion / get_color_conversion)
 * and _color_load_depth, which loaders use to pick the depth of a bitmap
 * they read from a file.
 *
 * The mode decides how bitmaps of another color depth are converted when
 * datafiles or image files are loaded. The value lives in _color_conv
 * (defined with the bitmap state in foundation_bitmap.cpp, default
 * COLORCONV_TOTAL).
 *
 * Written from scratch for zcport.
 */

#include <allegro.h>
#include <allegro/internal/aintern.h>

namespace
{
// COLORCONV_<from>_TO_<to> for every pair, from gfx.h's layout.
struct ConvFlag
{
    int from;
    int has_alpha;
    int to;
    int flag;
};

const ConvFlag kConvFlags[] =
{
    { 8, 0, 15, COLORCONV_8_TO_15 },   { 8, 0, 16, COLORCONV_8_TO_16 },
    { 8, 0, 24, COLORCONV_8_TO_24 },   { 8, 0, 32, COLORCONV_8_TO_32 },
    { 15, 0, 8, COLORCONV_15_TO_8 },   { 15, 0, 16, COLORCONV_15_TO_16 },
    { 15, 0, 24, COLORCONV_15_TO_24 }, { 15, 0, 32, COLORCONV_15_TO_32 },
    { 16, 0, 8, COLORCONV_16_TO_8 },   { 16, 0, 15, COLORCONV_16_TO_15 },
    { 16, 0, 24, COLORCONV_16_TO_24 }, { 16, 0, 32, COLORCONV_16_TO_32 },
    { 24, 0, 8, COLORCONV_24_TO_8 },   { 24, 0, 15, COLORCONV_24_TO_15 },
    { 24, 0, 16, COLORCONV_24_TO_16 }, { 24, 0, 32, COLORCONV_24_TO_32 },
    { 32, 0, 8, COLORCONV_32_TO_8 },   { 32, 0, 15, COLORCONV_32_TO_15 },
    { 32, 0, 16, COLORCONV_32_TO_16 }, { 32, 0, 24, COLORCONV_32_TO_24 },
    { 32, 1, 8, COLORCONV_32A_TO_8 },  { 32, 1, 15, COLORCONV_32A_TO_15 },
    { 32, 1, 16, COLORCONV_32A_TO_16 },{ 32, 1, 24, COLORCONV_32A_TO_24 },
};
}

extern "C" {

void set_color_conversion(int mode)
{
    _color_conv = mode;
}

int get_color_conversion(void)
{
    return _color_conv;
}

// A bitmap stored at 'depth' is loaded at the current color depth when the
// conversion mode has the bit for that pair, else at its own depth.
int _color_load_depth(int depth, int hasalpha)
{
    if(depth == _color_depth)
        return depth;

    for(size_t i = 0; i < sizeof(kConvFlags) / sizeof(kConvFlags[0]); ++i)
    {
        const ConvFlag &c = kConvFlags[i];

        if(c.from == depth && c.has_alpha == (hasalpha ? 1 : 0) && c.to == _color_depth)
            return (_color_conv & c.flag) ? _color_depth : depth;
    }

    return depth;
}

} // extern "C"

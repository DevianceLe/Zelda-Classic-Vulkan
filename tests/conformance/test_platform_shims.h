/*
 * test_platform_shims.h - stand-ins for colour functions the platform
 * tests use (set_palette, get_palette, default_palette, makecol32), for
 * port builds where the colour module is not in the library yet.
 *
 * Port side, MSVC x86 only: each stand-in is bound with /alternatename, so
 * the linker uses it only when zcport has no definition of its own (the
 * real function wins as soon as it exists; verified: the linker searches
 * the libraries first). The stand-ins do what the platform needs to see:
 * set_palette() writes _current_palette, which the platform shows.
 */
#ifndef TEST_PLATFORM_SHIMS_H
#define TEST_PLATFORM_SHIMS_H

#if defined(CONF_SIDE_PORT) && defined(_MSC_VER) && defined(_M_IX86)

extern "C" void conf_shim_set_palette_range(const RGB *p, int from, int to, int vsync)
{
    (void)vsync;
    for(int i = from; i <= to && i < PAL_SIZE; i++)
        if(i >= 0)
            _current_palette[i] = p[i];
}

extern "C" void conf_shim_set_palette(const RGB *p)
{
    conf_shim_set_palette_range(p, 0, PAL_SIZE - 1, 1);
}

extern "C" void conf_shim_get_palette(RGB *p)
{
    for(int i = 0; i < PAL_SIZE; i++)
        p[i] = _current_palette[i];
}

extern "C" int conf_shim_makecol32(int r, int g, int b)
{
    return (r << 16) | (g << 8) | b;
}

/* Filled before main() from the values the reference prints. */
extern "C" PALETTE conf_shim_default_palette;
PALETTE conf_shim_default_palette;
namespace
{
    struct ConfShimPaletteInit
    {
        ConfShimPaletteInit()
        {
            static const unsigned char v[256][3] = {
                {0,0,0},{0,0,42},{0,42,0},{0,42,42},{42,0,0},{42,0,42},{42,21,0},{42,42,42},
                {21,21,21},{21,21,63},{21,63,21},{21,63,63},{63,21,21},{63,21,63},{63,63,21},{63,63,63},
                {0,0,0},{5,5,5},{8,8,8},{11,11,11},{14,14,14},{17,17,17},{20,20,20},{24,24,24},
                {28,28,28},{32,32,32},{36,36,36},{40,40,40},{45,45,45},{50,50,50},{56,56,56},{63,63,63},
                {0,0,63},{16,0,63},{31,0,63},{47,0,63},{63,0,63},{63,0,47},{63,0,31},{63,0,16},
                {63,0,0},{63,16,0},{63,31,0},{63,47,0},{63,63,0},{47,63,0},{31,63,0},{16,63,0},
                {0,63,0},{0,63,16},{0,63,31},{0,63,47},{0,63,63},{0,47,63},{0,31,63},{0,16,63},
                {31,31,63},{39,31,63},{47,31,63},{55,31,63},{63,31,63},{63,31,55},{63,31,47},{63,31,39},
                {63,31,31},{63,39,31},{63,47,31},{63,55,31},{63,63,31},{55,63,31},{47,63,31},{39,63,31},
                {31,63,31},{31,63,39},{31,63,47},{31,63,55},{31,63,63},{31,55,63},{31,47,63},{31,39,63},
                {45,45,63},{49,45,63},{54,45,63},{58,45,63},{63,45,63},{63,45,58},{63,45,54},{63,45,49},
                {63,45,45},{63,49,45},{63,54,45},{63,58,45},{63,63,45},{58,63,45},{54,63,45},{49,63,45},
                {45,63,45},{45,63,49},{45,63,54},{45,63,58},{45,63,63},{45,58,63},{45,54,63},{45,49,63},
                {0,0,28},{7,0,28},{14,0,28},{21,0,28},{28,0,28},{28,0,21},{28,0,14},{28,0,7},
                {28,0,0},{28,7,0},{28,14,0},{28,21,0},{28,28,0},{21,28,0},{14,28,0},{7,28,0},
                {0,28,0},{0,28,7},{0,28,14},{0,28,21},{0,28,28},{0,21,28},{0,14,28},{0,7,28},
                {14,14,28},{17,14,28},{21,14,28},{24,14,28},{28,14,28},{28,14,24},{28,14,21},{28,14,17},
                {28,14,14},{28,17,14},{28,21,14},{28,24,14},{28,28,14},{24,28,14},{21,28,14},{17,28,14},
                {14,28,14},{14,28,17},{14,28,21},{14,28,24},{14,28,28},{14,24,28},{14,21,28},{14,17,28},
                {20,20,28},{22,20,28},{24,20,28},{26,20,28},{28,20,28},{28,20,26},{28,20,24},{28,20,22},
                {28,20,20},{28,22,20},{28,24,20},{28,26,20},{28,28,20},{26,28,20},{24,28,20},{22,28,20},
                {20,28,20},{20,28,22},{20,28,24},{20,28,26},{20,28,28},{20,26,28},{20,24,28},{20,22,28},
                {0,0,16},{4,0,16},{8,0,16},{12,0,16},{16,0,16},{16,0,12},{16,0,8},{16,0,4},
                {16,0,0},{16,4,0},{16,8,0},{16,12,0},{16,16,0},{12,16,0},{8,16,0},{4,16,0},
                {0,16,0},{0,16,4},{0,16,8},{0,16,12},{0,16,16},{0,12,16},{0,8,16},{0,4,16},
                {8,8,16},{10,8,16},{12,8,16},{14,8,16},{16,8,16},{16,8,14},{16,8,12},{16,8,10},
                {16,8,8},{16,10,8},{16,12,8},{16,14,8},{16,16,8},{14,16,8},{12,16,8},{10,16,8},
                {8,16,8},{8,16,10},{8,16,12},{8,16,14},{8,16,16},{8,14,16},{8,12,16},{8,10,16},
                {11,11,16},{12,11,16},{13,11,16},{15,11,16},{16,11,16},{16,11,15},{16,11,13},{16,11,12},
                {16,11,11},{16,12,11},{16,13,11},{16,15,11},{16,16,11},{15,16,11},{13,16,11},{12,16,11},
                {11,16,11},{11,16,12},{11,16,13},{11,16,15},{11,16,16},{11,15,16},{11,13,16},{11,12,16},
                {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{63,63,63}
            };
            for(int i = 0; i < 256; i++)
                {
                conf_shim_default_palette[i].r = v[i][0];
                conf_shim_default_palette[i].g = v[i][1];
                conf_shim_default_palette[i].b = v[i][2];
                conf_shim_default_palette[i].filler = 0;
            }
        }
    } conf_shim_palette_init;
}

#pragma comment(linker, "/alternatename:_set_palette=_conf_shim_set_palette")
#pragma comment(linker, "/alternatename:_set_palette_range=_conf_shim_set_palette_range")
#pragma comment(linker, "/alternatename:_get_palette=_conf_shim_get_palette")
#pragma comment(linker, "/alternatename:_makecol32=_conf_shim_makecol32")
#pragma comment(linker, "/alternatename:_default_palette=_conf_shim_default_palette")

#endif

#endif

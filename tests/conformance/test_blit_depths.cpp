/*
 * test_blit_depths.cpp - the blit module on hicolor and truecolor bitmaps:
 * blits between color depths (palette expansion of 8-bit pixels, 15/16/24/32
 * conversions, reduction to 8-bit through rgb_map), 8-bit sprites drawn on
 * truecolor bitmaps, 3D polygons on 15/16/24/32 bpp, and translucent / lit
 * drawing through a blender.
 *
 * The blender is defined by this test (any deterministic function will do):
 * on the reference side it is installed with set_blender_mode_ex(), on the
 * port side through the zcport blender hooks, so the comparison checks how
 * the drawing functions call the blender, not the color module's blenders
 * (those have their own tests).
 */
#include "conf.h"

#include <string.h>

#ifdef CONF_SIDE_PORT
#include "zcport/draw.h"

#if defined(_MSC_VER)
/* set_palette() belongs to the color module. Until that module is linked
 * in, MSVC resolves the name to this stand-in (it only updates
 * _current_palette, which is all the blit module reads); /alternatename is
 * ignored as soon as a real set_palette exists. */
extern "C" void conf_blit_set_palette_standin(const RGB *p)
{
    memcpy(_current_palette, p, sizeof(PALETTE));
}
#if defined(_M_IX86)
#pragma comment(linker, "/alternatename:_set_palette=_conf_blit_set_palette_standin")
#else
#pragma comment(linker, "/alternatename:set_palette=conf_blit_set_palette_standin")
#endif
#endif
#endif

namespace
{

const int kDepths[] = { 15, 16, 24, 32 };

void raw_put(BITMAP *b, int x, int y, unsigned c)
{
    switch(bitmap_color_depth(b))
    {
    case 8: b->line[y][x] = (unsigned char)c; break;
    case 15:
    case 16: ((unsigned short *)b->line[y])[x] = (unsigned short)c; break;
    case 24:
        b->line[y][x * 3 + 0] = (unsigned char)c;
        b->line[y][x * 3 + 1] = (unsigned char)(c >> 8);
        b->line[y][x * 3 + 2] = (unsigned char)(c >> 16);
        break;
    default: ((unsigned int *)b->line[y])[x] = c; break;
    }
}

unsigned mask_of(int depth)
{
    switch(depth)
    {
    case 8: return 0;
    case 15: return 0x7C1F;
    case 16: return 0xF81F;
    default: return 0xFF00FF;
    }
}

/* Valid pixel values of a depth (no stray high bits, no alpha). */
unsigned random_pixel(conf::Rng &rng, int depth)
{
    switch(depth)
    {
    case 8: return (unsigned)rng.range(1, 255);
    case 15: return (unsigned)rng.bits(15);
    case 16: return (unsigned)rng.bits(16);
    default: return (unsigned)rng.bits(24);
    }
}

void fill_random(BITMAP *b, conf::Rng &rng, int mask_pct)
{
    int depth = bitmap_color_depth(b);
    for(int y = 0; y < b->h; ++y)
        for(int x = 0; x < b->w; ++x)
            raw_put(b, x, y, (rng.range(0, 99) < mask_pct) ? mask_of(depth) : random_pixel(rng, depth));
}

/* A palette using every 6-bit level. */
void test_palette(PALETTE pal, int salt)
{
    for(int i = 0; i < PAL_SIZE; ++i)
    {
        pal[i].r = (unsigned char)((i + salt) & 63);
        pal[i].g = (unsigned char)((i * 3 + salt) & 63);
        pal[i].b = (unsigned char)((i * 7 + 5 + salt) & 63);
        pal[i].filler = 0;
    }
}

RGB_MAP g_rgb;
void init_rgb_map()
{
    for(int r = 0; r < 32; ++r)
        for(int g = 0; g < 32; ++g)
            for(int b = 0; b < 32; ++b)
                g_rgb.data[r][g][b] = (unsigned char)((r * 5 + g * 3 + b * 9 + (r ^ g ^ b)) & 0xFF);
}

/* ---- the test blender ---------------------------------------------------- */

/* Deterministic mix of x, y and n, kept inside the depth's pixel range. */
unsigned long mix(int depth, unsigned long x, unsigned long y, unsigned long n)
{
    unsigned long v = (x * 3u + y * 5u + n * 7u + ((x ^ y) >> 1)) ^ (n << 3);
    switch(depth)
    {
    case 15: return v & 0x7FFF;
    case 16: return v & 0xFFFF;
    default: return v & 0xFFFFFF;
    }
}

unsigned long b15(unsigned long x, unsigned long y, unsigned long n) { return mix(15, x, y, n); }
unsigned long b16(unsigned long x, unsigned long y, unsigned long n) { return mix(16, x, y, n); }
unsigned long b24(unsigned long x, unsigned long y, unsigned long n) { return mix(24, x, y, n); }
unsigned long b32(unsigned long x, unsigned long y, unsigned long n) { return mix(32, x, y, n); }

const int kBlendR = 200, kBlendG = 90, kBlendB = 40, kBlendA = 77;

/* makecol_depth(kBlendR, kBlendG, kBlendB) in the current pixel layout. */
int blend_color(int depth)
{
    switch(depth)
    {
    case 15: return ((kBlendR >> 3) << _rgb_r_shift_15) | ((kBlendG >> 3) << _rgb_g_shift_15) | ((kBlendB >> 3) << _rgb_b_shift_15);
    case 16: return ((kBlendR >> 3) << _rgb_r_shift_16) | ((kBlendG >> 2) << _rgb_g_shift_16) | ((kBlendB >> 3) << _rgb_b_shift_16);
    case 24: return (kBlendR << _rgb_r_shift_24) | (kBlendG << _rgb_g_shift_24) | (kBlendB << _rgb_b_shift_24);
    default: return (kBlendR << _rgb_r_shift_32) | (kBlendG << _rgb_g_shift_32) | (kBlendB << _rgb_b_shift_32);
    }
}

#ifdef CONF_SIDE_PORT
unsigned long port_trans(int depth, unsigned long src, unsigned long dest)
{
    return mix(depth, src, dest, kBlendA);
}

unsigned long port_lit(int depth, unsigned long pixel, int alpha)
{
    return mix(depth, (unsigned long)blend_color(depth), pixel, (unsigned long)alpha);
}

unsigned long port_lit_poly(int depth, unsigned long pixel, int alpha)
{
    return mix(depth, pixel, (unsigned long)blend_color(depth), (unsigned long)alpha);
}

zcport_blend_fn g_saved_trans;
zcport_lit_blend_fn g_saved_lit, g_saved_lit_poly;
#endif

void use_test_blender()
{
#ifdef CONF_SIDE_REF
    set_blender_mode_ex(b15, b16, b24, b32, b15, b16, b24, kBlendR, kBlendG, kBlendB, kBlendA);
#else
    g_saved_trans = zcport_trans_blender;
    g_saved_lit = zcport_lit_blender;
    g_saved_lit_poly = zcport_lit_blender_poly;
    zcport_trans_blender = port_trans;
    zcport_lit_blender = port_lit;
    zcport_lit_blender_poly = port_lit_poly;
#endif
}

void restore_blender()
{
#ifdef CONF_SIDE_REF
    set_trans_blender(0, 0, 0, 128);
#else
    zcport_trans_blender = g_saved_trans;
    zcport_lit_blender = g_saved_lit;
    zcport_lit_blender_poly = g_saved_lit_poly;
#endif
}

V3D_f vtx(float x, float y, float z, float u, float v, int c)
{
    V3D_f r;
    r.x = x; r.y = y; r.z = z; r.u = u; r.v = v; r.c = c;
    return r;
}

} // namespace

/* ------------------------------------------------------------------------- */

CONF_TEST(depths_blit_from_8)
{
    PALETTE pal;
    test_palette(pal, 0);
    set_palette(pal);
    BITMAP *src = create_bitmap_ex(8, 256, 2);
    for(int x = 0; x < 256; ++x)
    {
        src->line[0][x] = (unsigned char)x;
        src->line[1][x] = (unsigned char)(255 - x);
    }
    for(int di = 0; di < 4; ++di)
    {
        BITMAP *dst = create_bitmap_ex(kDepths[di], 260, 4);
        clear_to_color(dst, 0);
        blit(src, dst, 0, 0, 2, 1, 256, 2);
        ctx.check_bitmap(conf::nm("depth%d", kDepths[di]), dst);
        /* sprites: index 0 is transparent */
        clear_to_color(dst, 0x1234);
        draw_sprite(dst, src, 1, 1);
        ctx.check_bitmap(conf::nm("sprite_depth%d", kDepths[di]), dst);
        destroy_bitmap(dst);
    }
    /* COLORCONV_KEEP_TRANS */
    set_color_conversion(COLORCONV_KEEP_TRANS);
    for(int di = 0; di < 4; ++di)
    {
        BITMAP *dst = create_bitmap_ex(kDepths[di], 260, 4);
        clear_to_color(dst, 0);
        blit(src, dst, 0, 0, 2, 1, 256, 2);
        ctx.check_bitmap(conf::nm("keep_trans_depth%d", kDepths[di]), dst);
        destroy_bitmap(dst);
    }
    set_color_conversion(COLORCONV_NONE);
    destroy_bitmap(src);
}

CONF_TEST(depths_rotate_cases)
{
    PALETTE pal;
    test_palette(pal, 2);
    set_palette(pal);
    BITMAP *s8 = create_bitmap_ex(8, 8, 2);
    BITMAP *s16 = create_bitmap_ex(16, 8, 2);
    for(int x = 0; x < 8; ++x)
    {
        s8->line[0][x] = (unsigned char)(x + 1);
        s8->line[1][x] = (unsigned char)(x * 20 + 9);
        raw_put(s16, x, 0, (unsigned)(0x0841 * (x + 1)));
        raw_put(s16, x, 1, (unsigned)(0x1234 + x * 0x0711));
    }
    s8->line[0][2] = 0;                 /* transparent */
    raw_put(s16, 2, 0, 0xF81F);         /* transparent */
    raw_put(s16, 5, 0, 0x7C1F);         /* 15-bit mask value in a 16-bit sprite */
    BITMAP *d15 = create_bitmap_ex(15, 12, 4), *d32 = create_bitmap_ex(32, 12, 4);
    clear_to_color(d15, 0);
    rotate_sprite(d15, s8, 1, 1, 0);
    ctx.check_bitmap("s8_d15", d15);
    clear_to_color(d32, 0);
    rotate_sprite(d32, s8, 1, 1, 0);
    ctx.check_bitmap("s8_d32", d32);
    clear_to_color(d15, 0);
    rotate_sprite(d15, s16, 1, 1, 0);
    ctx.check_bitmap("s16_d15", d15);
    clear_to_color(d32, 0);
    rotate_sprite(d32, s16, 1, 1, 0);
    ctx.check_bitmap("s16_d32", d32);
    destroy_bitmap(d15);
    destroy_bitmap(d32);
    destroy_bitmap(s8);
    destroy_bitmap(s16);
}

CONF_TEST(depths_rotate_8bit_sprite)
{
    /* rotation functions draw between any two depths: 8-bit sprites go
     * through the palette, the other depths through r, g, b */
    PALETTE pal;
    test_palette(pal, 2);
    set_palette(pal);
    const int sdepths[] = { 8, 15, 16, 24, 32 };
    for(int si = 0; si < 5; ++si)
        for(int di = 0; di < 4; ++di)
        {
            if(sdepths[si] == kDepths[di])
                continue;
            conf::Seq s(ctx, conf::nm("from%d_to%d", sdepths[si], kDepths[di]));
            for(int i = 0; i < 20; ++i)
            {
                BITMAP *spr = create_bitmap_ex(sdepths[si], ctx.rng.range(1, 20), ctx.rng.range(1, 20));
                fill_random(spr, ctx.rng, 25);
                BITMAP *dst = create_bitmap_ex(kDepths[di], 32, 32);
                clear_to_color(dst, 0);
                fixed angle = itofix(ctx.rng.range(0, 255)) + ctx.rng.bits(16);
                if(i % 2)
                    rotate_sprite(dst, spr, ctx.rng.range(-5, 20), ctx.rng.range(-5, 20), angle);
                else
                    pivot_scaled_sprite(dst, spr, 16, 16, spr->w / 2, spr->h / 2, angle, ctx.rng.range(0x8000, 0x20000));
                s.add_bitmap(dst, "i=%d", i);
                destroy_bitmap(dst);
                destroy_bitmap(spr);
            }
        }
}

CONF_TEST(depths_blit_between)
{
    /* 15/16/24/32 among themselves (and the same depth, clipped) */
    for(int si = 0; si < 4; ++si)
        for(int di = 0; di < 4; ++di)
        {
            BITMAP *src = create_bitmap_ex(kDepths[si], 64, 16);
            /* every component value appears */
            for(int y = 0; y < 16; ++y)
                for(int x = 0; x < 64; ++x)
                {
                    int r = (x * 4 + y) & 255, g = (x * 3 + y * 16) & 255, b = (255 - x * 4) & 255;
                    unsigned c;
                    switch(kDepths[si])
                    {
                    case 15: c = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3); break;
                    case 16: c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); break;
                    default: c = (unsigned)((r << 16) | (g << 8) | b); break;
                    }
                    if((x + y) % 11 == 0)
                        c = mask_of(kDepths[si]);
                    raw_put(src, x, y, c);
                }
            BITMAP *dst = create_bitmap_ex(kDepths[di], 70, 20);
            clear_to_color(dst, 0);
            set_clip_rect(dst, 2, 1, 66, 18);
            blit(src, dst, 0, 0, 3, 2, 64, 16);
            ctx.check_bitmap(conf::nm("from%d_to%d", kDepths[si], kDepths[di]), dst);
            set_color_conversion(COLORCONV_KEEP_TRANS);
            clear_to_color(dst, 0);
            blit(src, dst, 0, 0, 3, 2, 64, 16);
            ctx.check_bitmap(conf::nm("keep_trans_from%d_to%d", kDepths[si], kDepths[di]), dst);
            set_color_conversion(COLORCONV_NONE);
            destroy_bitmap(dst);
            destroy_bitmap(src);
        }
}

CONF_TEST(depths_keep_trans_pink)
{
    /* KEEP_TRANS: visible pixels that would turn into the mask color */
    RGB_MAP *old = rgb_map;
    init_rgb_map();
    rgb_map = &g_rgb;
    PALETTE pal;
    test_palette(pal, 1);
    pal[5].r = 63; pal[5].g = 0; pal[5].b = 63;
    pal[6].r = 63; pal[6].g = 1; pal[6].b = 63;
    set_palette(pal);
    set_color_conversion(COLORCONV_KEEP_TRANS);
    BITMAP *s8 = create_bitmap_ex(8, 4, 1);
    s8->line[0][0] = 0;
    s8->line[0][1] = 5;
    s8->line[0][2] = 6;
    s8->line[0][3] = 7;
    BITMAP *s32 = create_bitmap_ex(32, 5, 1);
    raw_put(s32, 0, 0, 0xFF00FF);
    raw_put(s32, 1, 0, 0xFF02FF);
    raw_put(s32, 2, 0, 0xFC03FC);
    raw_put(s32, 3, 0, 0x123456);
    raw_put(s32, 4, 0, 0x000000);
    for(int di = 0; di < 4; ++di)
    {
        BITMAP *d = create_bitmap_ex(kDepths[di], 5, 1);
        clear_to_color(d, 0);
        blit(s8, d, 0, 0, 0, 0, 4, 1);
        ctx.check_bitmap(conf::nm("from8_to%d", kDepths[di]), d);
        clear_to_color(d, 0);
        blit(s32, d, 0, 0, 0, 0, 5, 1);
        ctx.check_bitmap(conf::nm("from32_to%d", kDepths[di]), d);
        destroy_bitmap(d);
    }
    BITMAP *d8 = create_bitmap_ex(8, 5, 1);
    clear_to_color(d8, 0);
    blit(s32, d8, 0, 0, 0, 0, 5, 1);
    ctx.check_bitmap("from32_to8", d8);
    destroy_bitmap(d8);
    set_color_conversion(COLORCONV_NONE);
    destroy_bitmap(s32);
    destroy_bitmap(s8);
    rgb_map = old;
}

CONF_TEST(depths_blit_to_8)
{
    /* reduction to 8-bit with an rgb_map */
    RGB_MAP *old = rgb_map;
    init_rgb_map();
    rgb_map = &g_rgb;
    PALETTE pal;
    test_palette(pal, 3);
    set_palette(pal);
    for(int si = 0; si < 4; ++si)
    {
        BITMAP *src = create_bitmap_ex(kDepths[si], 64, 16);
        fill_random(src, ctx.rng, 10);
        BITMAP *dst = create_bitmap_ex(8, 66, 18);
        clear_to_color(dst, 0);
        blit(src, dst, 0, 0, 1, 1, 64, 16);
        ctx.check_bitmap(conf::nm("from%d", kDepths[si]), dst);
        set_color_conversion(COLORCONV_KEEP_TRANS);
        clear_to_color(dst, 0);
        blit(src, dst, 0, 0, 1, 1, 64, 16);
        ctx.check_bitmap(conf::nm("keep_trans_from%d", kDepths[si]), dst);
        set_color_conversion(COLORCONV_NONE);
        destroy_bitmap(dst);
        destroy_bitmap(src);
    }
    rgb_map = old;
}

CONF_TEST(depths_poly3d)
{
    /* 3D polygons on hicolor/truecolor bitmaps (no blender involved) */
    RGB_MAP *old = rgb_map;
    const int types[] = { POLYTYPE_FLAT, POLYTYPE_GCOL, POLYTYPE_GRGB, POLYTYPE_ATEX, POLYTYPE_PTEX,
                          POLYTYPE_ATEX_MASK, POLYTYPE_PTEX_MASK };
    for(int di = 0; di < 4; ++di)
    {
        const int depth = kDepths[di];
        BITMAP *tex = create_bitmap_ex(depth, 16, 32);
        fill_random(tex, ctx.rng, 20);
        BITMAP *dst = create_bitmap_ex(depth, 48, 48);
        for(int ti = 0; ti < 7; ++ti)
        {
            conf::Seq s(ctx, conf::nm("depth%d_type%d", depth, types[ti]));
            for(int i = 0; i < 40; ++i)
            {
                V3D_f v[4];
                for(int k = 0; k < 4; ++k)
                {
                    int c;
                    if(types[ti] == POLYTYPE_GRGB)
                        c = (int)(ctx.rng.next() & 0xFFFFFF);
                    else
                        c = (int)random_pixel(ctx.rng, depth);
                    /* drawn in the order the 32-bit MSVC build evaluated the
                     * vtx() arguments (C++ leaves it open; x64 differs) */
                    const float x = ctx.rng.range(-500, 5300) / 100.0f;
                    const float y = ctx.rng.range(-500, 5300) / 100.0f;
                    const float z = ctx.rng.range(100, 4000) / 100.0f;
                    const float tu = ctx.rng.range(-2000, 5000) / 100.0f;
                    const float tv = ctx.rng.range(-2000, 5000) / 100.0f;
                    v[k] = vtx(x, y, z, tu, tv, c);
                }
                clear_to_color(dst, 0);
                if(i % 2)
                    quad3d_f(dst, types[ti], tex, &v[0], &v[1], &v[2], &v[3]);
                else
                    triangle3d_f(dst, types[ti], tex, &v[0], &v[1], &v[2]);
                s.add_bitmap(dst, "i=%d", i);
            }
        }
        destroy_bitmap(dst);
        destroy_bitmap(tex);
    }
    rgb_map = old;
}

CONF_TEST(depths_blended_cases)
{
    /* single cases with a pixel dump on mismatch */
    use_test_blender();
    for(int di = 0; di < 4; ++di)
    {
        const int depth = kDepths[di];
        BITMAP *spr = create_bitmap_ex(depth, 16, 16);
        fill_random(spr, ctx.rng, 25);
        BITMAP *dst = create_bitmap_ex(depth, 24, 24);
        RLE_SPRITE *rle = get_rle_sprite(spr);
        clear_to_color(dst, 0);
        draw_trans_rle_sprite(dst, rle, 3, 4);
        ctx.check_bitmap(conf::nm("trans_rle%d", depth), dst);
        clear_to_color(dst, 0);
        draw_lit_rle_sprite(dst, rle, 3, 4, 100);
        ctx.check_bitmap(conf::nm("lit_rle%d", depth), dst);
        clear_to_color(dst, 0);
        V3D_f a = vtx(2, 2, 1, 0, 0, 30), b = vtx(20, 4, 2, 16, 0, 30), c = vtx(5, 21, 3, 0, 16, 200);
        triangle3d_f(dst, POLYTYPE_ATEX_LIT, spr, &a, &b, &c);
        ctx.check_bitmap(conf::nm("atex_lit%d", depth), dst);
        destroy_rle_sprite(rle);
        destroy_bitmap(dst);
        destroy_bitmap(spr);
    }
    restore_blender();
}

CONF_TEST(depths_blended)
{
    /* translucent and lit drawing through the blender, every depth */
    use_test_blender();
    for(int di = 0; di < 4; ++di)
    {
        const int depth = kDepths[di];
        BITMAP *ptex = create_bitmap_ex(depth, 16, 16);
        fill_random(ptex, ctx.rng, 25);
        conf::Seq s(ctx, conf::nm("depth%d", depth));
        for(int i = 0; i < 60; ++i)
        {
            BITMAP *spr = create_bitmap_ex(depth, ctx.rng.range(1, 24), ctx.rng.range(1, 24));
            fill_random(spr, ctx.rng, 25);
            BITMAP *dst = create_bitmap_ex(depth, 40, 40);
            fill_random(dst, ctx.rng, 0);
            int x = ctx.rng.range(-10, 30), y = ctx.rng.range(-10, 30);
            int color = ctx.rng.range(0, 255);
            RLE_SPRITE *rle = get_rle_sprite(spr);
            switch(i % 8)
            {
            case 0: draw_trans_sprite(dst, spr, x, y); break;
            case 1: draw_lit_sprite(dst, spr, x, y, color); break;
            case 2: draw_trans_rle_sprite(dst, rle, x, y); break;
            case 3: draw_lit_rle_sprite(dst, rle, x, y, color); break;
            case 4: rotate_sprite_trans(dst, spr, x, y, itofix(ctx.rng.range(0, 255))); break;
            case 5: rotate_sprite_lit(dst, spr, x, y, itofix(ctx.rng.range(0, 255)), color); break;
            case 6:
            {
                V3D_f a = vtx((float)x, (float)y, 1, 0, 0, color), b = vtx((float)x + 20, (float)y + 3, 2, 16, 0, color),
                      c = vtx((float)x + 5, (float)y + 25, 3, 0, 16, 255 - color);
                triangle3d_f(dst, POLYTYPE_ATEX_LIT, ptex, &a, &b, &c);
                break;
            }
            default:
            {
                V3D_f a = vtx((float)x, (float)y, 1, 0, 0, color), b = vtx((float)x + 20, (float)y + 3, 2, 16, 0, color),
                      c = vtx((float)x + 22, (float)y + 25, 3, 16, 16, color), d = vtx((float)x + 2, (float)y + 21, 1, 0, 16, color);
                quad3d_f(dst, POLYTYPE_PTEX_MASK_TRANS, ptex, &a, &b, &c, &d);
                break;
            }
            }
            s.add_bitmap(dst, "i=%d spr %dx%d at %d,%d color=%d", i, spr->w, spr->h, x, y, color);
            destroy_rle_sprite(rle);
            destroy_bitmap(dst);
            destroy_bitmap(spr);
        }
        s.end();
        destroy_bitmap(ptex);
    }
    restore_blender();
}

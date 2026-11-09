/*
 * test_blit_rotate.cpp - rotated, scaled and flipped sprites must be
 * pixel-exact against the reference build.
 *
 * rotate_sprite (what scripts use for every rotated tile and bitmap draw),
 * rotate_scaled_sprite, the _v_flip forms, pivot_sprite and
 * pivot_scaled_sprite, and their _trans and _lit forms through a fixed
 * 8-bit COLOR_MAP. Every integer angle, fractional, negative and
 * out-of-range angles, scales from tiny to large, odd and even sprite
 * sizes, clipping, sub-bitmaps and the other color depths.
 *
 * Sprites whose pixels encode their own coordinates make any mapping
 * difference visible in the hashed result.
 */
#include "conf.h"

#include <string.h>

namespace
{

const int kDepths[] = { 8, 15, 16, 24, 32 };
const int kNumDepths = 5;

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

unsigned random_pixel(conf::Rng &rng, int depth)
{
    switch(depth)
    {
    case 8: return (unsigned)rng.range(1, 255);
    case 15:
    case 16: return (unsigned)rng.bits(16);
    case 24: return (unsigned)rng.bits(24);
    default: return rng.next();
    }
}

void fill_random(BITMAP *b, conf::Rng &rng, int mask_pct)
{
    int depth = bitmap_color_depth(b);
    unsigned mask = (unsigned)bitmap_mask_color(b);
    for(int y = 0; y < b->h; ++y)
        for(int x = 0; x < b->w; ++x)
            raw_put(b, x, y, (rng.range(0, 99) < mask_pct) ? mask : random_pixel(rng, depth));
}

/* 8-bit sprite whose pixels name their coordinates (never 0). */
BITMAP *coord_sprite(int w, int h)
{
    BITMAP *b = create_bitmap_ex(8, w, h);
    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
            b->line[y][x] = (unsigned char)(1 + (x + y * 17) % 255);
    return b;
}

void random_clip(BITMAP *b, conf::Rng &rng)
{
    if(rng.range(0, 2) != 0)
    {
        set_clip_rect(b, 0, 0, b->w - 1, b->h - 1);
        return;
    }
    int x1 = rng.range(0, b->w - 1), x2 = rng.range(0, b->w - 1);
    int y1 = rng.range(0, b->h - 1), y2 = rng.range(0, b->h - 1);
    if(x2 < x1) { int t = x1; x1 = x2; x2 = t; }
    if(y2 < y1) { int t = y1; y1 = y2; y2 = t; }
    set_clip_rect(b, x1, y1, x2, y2);
}

COLOR_MAP g_map;
void init_map()
{
    for(int a = 0; a < PAL_SIZE; ++a)
        for(int b = 0; b < PAL_SIZE; ++b)
            g_map.data[a][b] = (unsigned char)((a * 3 + b * 11 + (a ^ b) + 5) & 0xFF);
}

fixed random_angle(conf::Rng &rng)
{
    switch(rng.range(0, 7))
    {
    case 0: return itofix(rng.range(0, 255));
    case 1: return itofix(rng.range(0, 3) * 64);
    case 2: return itofix(rng.range(-600, 600)) + rng.bits(16);
    case 3: return (fixed)rng.next();          /* any 16.16 value */
    case 4: return itofix(rng.range(0, 3) * 64) + rng.range(-3, 3);
    default: return itofix(rng.range(0, 255)) + rng.bits(16);
    }
}

fixed random_scale(conf::Rng &rng)
{
    switch(rng.range(0, 6))
    {
    case 0: return itofix(1);
    case 1: return itofix(rng.range(2, 4));
    case 2: return 0x8000;
    case 3: return rng.range(0x0400, 0x4000);  /* tiny */
    case 4: return rng.range(0x40000, 0x80000); /* 4x..8x */
    default: return rng.range(0x4000, 0x30000);
    }
}

enum Fn
{
    F_ROTATE, F_ROTATE_V, F_ROTATE_SCALED, F_ROTATE_SCALED_V,
    F_PIVOT, F_PIVOT_V, F_PIVOT_SCALED, F_PIVOT_SCALED_V, F_COUNT
};

const char *const kFnName[F_COUNT] =
{
    "rotate", "rotate_v_flip", "rotate_scaled", "rotate_scaled_v_flip",
    "pivot", "pivot_v_flip", "pivot_scaled", "pivot_scaled_v_flip"
};

enum Variant { V_PLAIN, V_TRANS, V_LIT };

void call(int fn, int variant, BITMAP *bmp, BITMAP *spr, int x, int y, int cx, int cy,
          fixed angle, fixed scale, int color)
{
    if(variant == V_PLAIN)
    {
        switch(fn)
        {
        case F_ROTATE: rotate_sprite(bmp, spr, x, y, angle); break;
        case F_ROTATE_V: rotate_sprite_v_flip(bmp, spr, x, y, angle); break;
        case F_ROTATE_SCALED: rotate_scaled_sprite(bmp, spr, x, y, angle, scale); break;
        case F_ROTATE_SCALED_V: rotate_scaled_sprite_v_flip(bmp, spr, x, y, angle, scale); break;
        case F_PIVOT: pivot_sprite(bmp, spr, x, y, cx, cy, angle); break;
        case F_PIVOT_V: pivot_sprite_v_flip(bmp, spr, x, y, cx, cy, angle); break;
        case F_PIVOT_SCALED: pivot_scaled_sprite(bmp, spr, x, y, cx, cy, angle, scale); break;
        default: pivot_scaled_sprite_v_flip(bmp, spr, x, y, cx, cy, angle, scale); break;
        }
    }
    else if(variant == V_TRANS)
    {
        switch(fn)
        {
        case F_ROTATE: rotate_sprite_trans(bmp, spr, x, y, angle); break;
        case F_ROTATE_V: rotate_sprite_v_flip_trans(bmp, spr, x, y, angle); break;
        case F_ROTATE_SCALED: rotate_scaled_sprite_trans(bmp, spr, x, y, angle, scale); break;
        case F_ROTATE_SCALED_V: rotate_scaled_sprite_v_flip_trans(bmp, spr, x, y, angle, scale); break;
        case F_PIVOT: pivot_sprite_trans(bmp, spr, x, y, cx, cy, angle); break;
        case F_PIVOT_V: pivot_sprite_v_flip_trans(bmp, spr, x, y, cx, cy, angle); break;
        case F_PIVOT_SCALED: pivot_scaled_sprite_trans(bmp, spr, x, y, cx, cy, angle, scale); break;
        default: pivot_scaled_sprite_v_flip_trans(bmp, spr, x, y, cx, cy, angle, scale); break;
        }
    }
    else
    {
        switch(fn)
        {
        case F_ROTATE: rotate_sprite_lit(bmp, spr, x, y, angle, color); break;
        case F_ROTATE_V: rotate_sprite_v_flip_lit(bmp, spr, x, y, angle, color); break;
        case F_ROTATE_SCALED: rotate_scaled_sprite_lit(bmp, spr, x, y, angle, scale, color); break;
        case F_ROTATE_SCALED_V: rotate_scaled_sprite_v_flip_lit(bmp, spr, x, y, angle, scale, color); break;
        case F_PIVOT: pivot_sprite_lit(bmp, spr, x, y, cx, cy, angle, color); break;
        case F_PIVOT_V: pivot_sprite_v_flip_lit(bmp, spr, x, y, cx, cy, angle, color); break;
        case F_PIVOT_SCALED: pivot_scaled_sprite_lit(bmp, spr, x, y, cx, cy, angle, scale, color); break;
        default: pivot_scaled_sprite_v_flip_lit(bmp, spr, x, y, cx, cy, angle, scale, color); break;
        }
    }
}

/* Random cases of one function / variant / depth. */
void random_cases(conf::Ctx &ctx, conf::Seq &s, int fn, int variant, int depth, int count)
{
    for(int i = 0; i < count; ++i)
    {
        BITMAP *spr;
        if(depth == 8 && ctx.rng.range(0, 2) == 0)
            spr = coord_sprite(ctx.rng.range(1, 34), ctx.rng.range(1, 34));
        else
        {
            spr = create_bitmap_ex(depth, ctx.rng.range(1, 34), ctx.rng.range(1, 34));
            fill_random(spr, ctx.rng, 25);
        }
        BITMAP *root = create_bitmap_ex(depth, ctx.rng.range(8, 64), ctx.rng.range(8, 64));
        fill_random(root, ctx.rng, 5);
        BITMAP *dst = root;
        if(ctx.rng.range(0, 4) == 0)
            dst = create_sub_bitmap(root, ctx.rng.range(0, 4), ctx.rng.range(0, 4), root->w - 4, root->h - 4);
        random_clip(dst, ctx.rng);

        int x = ctx.rng.range(-40, dst->w + 10), y = ctx.rng.range(-40, dst->h + 10);
        if(ctx.rng.range(0, 9) == 0)
        {
            x = ctx.rng.range(-1000, 1000);
            y = ctx.rng.range(-1000, 1000);
        }
        int cx = ctx.rng.range(0, 3) ? ctx.rng.range(0, spr->w) : ctx.rng.range(-20, spr->w + 20);
        int cy = ctx.rng.range(0, 3) ? ctx.rng.range(0, spr->h) : ctx.rng.range(-20, spr->h + 20);
        fixed angle = random_angle(ctx.rng);
        fixed scale = random_scale(ctx.rng);
        int color = ctx.rng.range(0, 255);
        call(fn, variant, dst, spr, x, y, cx, cy, angle, scale, color);
        s.add_bitmap(root, "i=%d spr %dx%d at %d,%d c=%d,%d angle=0x%08x scale=0x%08x color=%d",
                     i, spr->w, spr->h, x, y, cx, cy, (unsigned)angle, (unsigned)scale, color);
        if(dst != root)
            destroy_bitmap(dst);
        destroy_bitmap(root);
        destroy_bitmap(spr);
    }
}

} // namespace

CONF_TEST(rotate_every_angle)
{
    /* Every integer angle and a fractional one, odd and even sprite sizes. */
    const int sizes[][2] = { {16, 16}, {15, 15}, {16, 8}, {7, 12}, {1, 1}, {2, 3}, {32, 32} };
    BITMAP *dst = create_bitmap_ex(8, 64, 64);
    for(int si = 0; si < 7; ++si)
    {
        BITMAP *spr = coord_sprite(sizes[si][0], sizes[si][1]);
        conf::Seq s(ctx, conf::nm("size%dx%d", sizes[si][0], sizes[si][1]));
        for(int a = 0; a < 256; ++a)
        {
            clear_to_color(dst, 0);
            rotate_sprite(dst, spr, 20, 20, itofix(a));
            s.add_bitmap(dst, "angle=%d", a);
            clear_to_color(dst, 0);
            fixed fa = itofix(a) + ctx.rng.bits(16);
            rotate_sprite(dst, spr, 20, 20, fa);
            s.add_bitmap(dst, "angle=0x%08x", (unsigned)fa);
        }
        s.end();
        destroy_bitmap(spr);
    }
    destroy_bitmap(dst);
}

CONF_TEST(rotate_script_degrees)
{
    /* What Screen->DrawTile/DrawBitmap etc. pass: whole degrees through the
     * game's degrees_to_fixed(), onto a sub-bitmap target, sprites that are
     * sub-bitmaps of a tile sheet. */
    BITMAP *sheet = create_bitmap_ex(8, 64, 48);
    for(int y = 0; y < 48; ++y)
        for(int x = 0; x < 64; ++x)
            sheet->line[y][x] = (unsigned char)(((x * 7 + y * 13) % 5 == 0) ? 0 : 1 + (x + y * 3) % 250);
    BITMAP *tile16 = create_sub_bitmap(sheet, 16, 0, 16, 16);
    BITMAP *tile32 = create_sub_bitmap(sheet, 0, 16, 32, 32);
    BITMAP *root = create_bitmap_ex(8, 80, 80);
    BITMAP *dst = create_sub_bitmap(root, 4, 6, 70, 66);
    conf::Seq s(ctx, "degrees");
    for(int d = -360; d <= 720; ++d)
    {
        fixed angle = ftofix(d * 0.7111111111111);
        clear_to_color(root, 0);
        rotate_sprite(dst, (d & 1) ? tile16 : tile32, 20 + d % 7, 18 - d % 5, angle);
        s.add_bitmap(root, "deg=%d", d);
    }
    s.end();
    destroy_bitmap(dst);
    destroy_bitmap(root);
    destroy_bitmap(tile32);
    destroy_bitmap(tile16);
    destroy_bitmap(sheet);
}

CONF_TEST(rotate_scaled_grid)
{
    BITMAP *spr = coord_sprite(16, 16);
    BITMAP *dst = create_bitmap_ex(8, 96, 96);
    const fixed scales[] = { 0x10000, 0x8000, 0x18000, 0x20000, 0x30000, 0x4000, 0x1234, 0x2C000 };
    for(int si = 0; si < 8; ++si)
    {
        conf::Seq s(ctx, conf::nm("scale%08x", (unsigned)scales[si]));
        for(int a = 0; a < 256; a += 3)
        {
            clear_to_color(dst, 0);
            rotate_scaled_sprite(dst, spr, 40, 40, itofix(a), scales[si]);
            s.add_bitmap(dst, "angle=%d", a);
            clear_to_color(dst, 0);
            rotate_scaled_sprite_v_flip(dst, spr, 40, 40, itofix(a) + 0x5555, scales[si]);
            s.add_bitmap(dst, "vflip angle=%d", a);
        }
    }
    destroy_bitmap(spr);
    destroy_bitmap(dst);
}

CONF_TEST(rotate_random_8)
{
    for(int fn = 0; fn < F_COUNT; ++fn)
    {
        conf::Seq s(ctx, kFnName[fn]);
        random_cases(ctx, s, fn, V_PLAIN, 8, 250);
    }
}

CONF_TEST(rotate_stress_8)
{
    /* Many small sprites, extreme scales and arbitrary angles: reaches the
     * rarely used corrections at the span ends. */
    conf::Seq s(ctx, "stress");
    BITMAP *dst = create_bitmap_ex(8, 48, 48);
    for(int i = 0; i < 6000; ++i)
    {
        BITMAP *spr = coord_sprite(ctx.rng.range(1, 6), ctx.rng.range(1, 6));
        clear_to_color(dst, 0);
        fixed angle = (fixed)ctx.rng.next();
        fixed scale = ctx.rng.range(0, 1) ? ctx.rng.range(0x800, 0x30000) : ctx.rng.range(0x30000, 0xA0000);
        int x = ctx.rng.range(-10, 40), y = ctx.rng.range(-10, 40);
        if(i % 2)
            pivot_scaled_sprite(dst, spr, x, y, ctx.rng.range(-3, 8), ctx.rng.range(-3, 8), angle, scale);
        else
            rotate_scaled_sprite_v_flip(dst, spr, x, y, angle, scale);
        s.add_bitmap(dst, "i=%d spr %dx%d at %d,%d angle=0x%08x scale=0x%08x", i, spr->w, spr->h, x, y,
                     (unsigned)angle, (unsigned)scale);
        destroy_bitmap(spr);
    }
    s.end();
    destroy_bitmap(dst);
}

CONF_TEST(rotate_trans_lit_8)
{
    init_map();
    COLOR_MAP *old_map = color_map;
    color_map = &g_map;
    for(int fn = 0; fn < F_COUNT; ++fn)
    {
        conf::Seq st(ctx, conf::nm("%s_trans", kFnName[fn]));
        random_cases(ctx, st, fn, V_TRANS, 8, 80);
        st.end();
        conf::Seq sl(ctx, conf::nm("%s_lit", kFnName[fn]));
        random_cases(ctx, sl, fn, V_LIT, 8, 80);
    }
    color_map = old_map;
}

CONF_TEST(rotate_depths)
{
    for(int di = 1; di < kNumDepths; ++di)
    {
        for(int fn = 0; fn < F_COUNT; fn += 2)
        {
            conf::Seq s(ctx, conf::nm("%s_depth%d", kFnName[fn], kDepths[di]));
            random_cases(ctx, s, fn, V_PLAIN, kDepths[di], 60);
        }
    }
}

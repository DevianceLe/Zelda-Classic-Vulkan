/*
 * test_blit_rotdbg.cpp - small rotation cases, one check_bitmap each, so a
 * mismatch shows the differing pixels (diagnostics for test_blit_rotate).
 */
#include "conf.h"

namespace
{

BITMAP *coord_sprite(int w, int h)
{
    BITMAP *b = create_bitmap_ex(8, w, h);
    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
            b->line[y][x] = (unsigned char)(1 + x + y * 16);
    return b;
}

} // namespace

CONF_TEST(rotdbg)
{
    const int sizes[][2] = { {8, 8}, {7, 5}, {16, 16}, {1, 1}, {3, 2} };
    const int angles[] = { 0, 1, 5, 10, 16, 30, 32, 45, 64, 100, 128, 192, 200 };
    BITMAP *dst = create_bitmap_ex(8, 40, 40);
    for(int si = 0; si < 5; ++si)
    {
        BITMAP *spr = coord_sprite(sizes[si][0], sizes[si][1]);
        for(int ai = 0; ai < 13; ++ai)
        {
            clear_to_color(dst, 0);
            rotate_sprite(dst, spr, 12, 12, itofix(angles[ai]));
            ctx.check_bitmap(conf::nm("s%dx%d_a%d", sizes[si][0], sizes[si][1], angles[ai]), dst);
        }
        destroy_bitmap(spr);
    }
    destroy_bitmap(dst);
}

CONF_TEST(rotdbg_cases)
{
    BITMAP *dst = create_bitmap_ex(8, 64, 64);
    BITMAP *spr = coord_sprite(16, 16);
    clear_to_color(dst, 0);
    rotate_sprite(dst, spr, 20, 20, 0x0003385e);
    ctx.check_bitmap("a3385e", dst);
    clear_to_color(dst, 0);
    rotate_sprite(dst, spr, 20, 20, 0x0004a9dc);
    ctx.check_bitmap("a4a9dc", dst);
    clear_to_color(dst, 0);
    rotate_scaled_sprite_v_flip(dst, spr, 20, 20, itofix(3) + 0x5555, itofix(1));
    ctx.check_bitmap("vf3", dst);
    clear_to_color(dst, 0);
    rotate_scaled_sprite_v_flip(dst, spr, 20, 20, itofix(6) + 0x5555, itofix(1));
    ctx.check_bitmap("vf6", dst);
    destroy_bitmap(spr);
    destroy_bitmap(dst);
}

CONF_TEST(rotdbg_scaled)
{
    const fixed scales[] = { 0x8000, 0x20000, 0x18000 };
    const int angles[] = { 0, 10, 32, 64, 100 };
    BITMAP *dst = create_bitmap_ex(8, 60, 60);
    BITMAP *spr = coord_sprite(8, 8);
    for(int si = 0; si < 3; ++si)
        for(int ai = 0; ai < 5; ++ai)
        {
            clear_to_color(dst, 0);
            rotate_scaled_sprite(dst, spr, 20, 20, itofix(angles[ai]), scales[si]);
            ctx.check_bitmap(conf::nm("sc%08x_a%d", (unsigned)scales[si], angles[ai]), dst);
            clear_to_color(dst, 0);
            rotate_scaled_sprite_v_flip(dst, spr, 20, 20, itofix(angles[ai]), scales[si]);
            ctx.check_bitmap(conf::nm("vf_sc%08x_a%d", (unsigned)scales[si], angles[ai]), dst);
        }
    destroy_bitmap(spr);
    destroy_bitmap(dst);
}

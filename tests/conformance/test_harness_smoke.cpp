/*
 * test_harness_smoke.cpp - proves the reference/port pipeline end to end.
 * Deliberately tiny: an 8-bit memory bitmap, clear_to_color, putpixel,
 * getpixel, and itofix/fixtoi.
 */
#include "conf.h"

CONF_TEST(smoke_framework)
{
    /* The framework itself must be identical on both sides. */
    ctx.check_int("int", -123456789);
    ctx.check_hex("hex", 0xDEADBEEFu);
    ctx.check_bool("bool", true);
    ctx.check_double("double", 0.1);
    ctx.check_str("str", "Zelda \"Classic\"\n");
    ctx.check_str("null_str", NULL);
    ctx.check_hash("hash_abc", conf::hash_string("abc"));

    conf::Seq rng(ctx, "rng");

    for(int i = 0; i < 16; ++i)
        rng.add_hex(ctx.rng.next(), "draw %d", i);

    rng.end();
}

CONF_TEST(smoke_bitmap)
{
    BITMAP *bmp = create_bitmap_ex(8, 16, 12);
    ctx.expect("create", bmp != NULL);

    if(!bmp)
        return;

    ctx.check_int("w", bmp->w);
    ctx.check_int("h", bmp->h);
    ctx.check_int("depth", bitmap_color_depth(bmp));

    clear_to_color(bmp, 3);
    ctx.check_bitmap("cleared", bmp);

    putpixel(bmp, 0, 0, 10);
    putpixel(bmp, 15, 11, 200);
    putpixel(bmp, 7, 5, 255);
    putpixel(bmp, -1, 3, 9);   /* outside: clipped */
    putpixel(bmp, 16, 0, 9);   /* outside: clipped */
    ctx.check_bitmap("pixels", bmp);

    ctx.check_int("get_0_0", getpixel(bmp, 0, 0));
    ctx.check_int("get_7_5", getpixel(bmp, 7, 5));
    ctx.check_int("get_1_1", getpixel(bmp, 1, 1));

    for(int i = 0; i < 64; ++i)
        putpixel(bmp, ctx.rng.range(0, 15), ctx.rng.range(0, 11), ctx.rng.range(0, 255));

    ctx.check_bitmap("random", bmp);
    destroy_bitmap(bmp);
}

CONF_TEST(smoke_fixed)
{
    static const int ints[] = { 0, 1, -1, 2, -2, 100, -100, 32767, -32768 };
    conf::Seq to_fix(ctx, "itofix");

    for(size_t i = 0; i < sizeof(ints) / sizeof(ints[0]); ++i)
        to_fix.add_hex((unsigned)itofix(ints[i]), "itofix(%d)", ints[i]);

    to_fix.end();

    /* fixtoi rounds to nearest, halves towards +infinity */
    static const int fixes[] = { 0x00000000, 0x00010000, 0x00018000, 0x00017FFF, -0x00018000,
                                 -0x00017FFF, 0x7FFF0000, -0x7FFF0000, 0x0000FFFF, -0x00008000 };
    conf::Seq to_int(ctx, "fixtoi");

    for(size_t i = 0; i < sizeof(fixes) / sizeof(fixes[0]); ++i)
        to_int.add_int(fixtoi(fixes[i]), "fixtoi(0x%08x)", (unsigned)fixes[i]);

    to_int.end();

    conf::Seq round_trip(ctx, "random_round_trip");

    for(int i = 0; i < 1000; ++i)
    {
        int v = ctx.rng.range(-32768, 32767);
        round_trip.add_int(fixtoi(itofix(v)), "fixtoi(itofix(%d))", v);
    }

    round_trip.end();
}

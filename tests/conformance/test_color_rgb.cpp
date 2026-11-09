/*
 * test_color_rgb.cpp - pixel formats and color searches must match the
 * reference build: the shift globals and scale tables, makecol* /
 * makeacol* / getr* families for every depth (also out-of-range inputs,
 * which only shift), makecol8 with and without rgb_map, bestfit_color
 * (also out-of-range requests) and create_rgb_table.
 *
 * Global state (palette, rgb_map, color depth) is restored at the end of
 * each test.
 */
#include "conf.h"

#include <string.h>

namespace
{

void random_palette(conf::Rng &rng, RGB *p)
{
    for(int i = 0; i < PAL_SIZE; ++i)
    {
        p[i].r = (unsigned char)rng.range(0, 63);
        p[i].g = (unsigned char)rng.range(0, 63);
        p[i].b = (unsigned char)rng.range(0, 63);
        p[i].filler = 0;
    }
}

/* A palette shaped like the game's: 16 csets of 16, many repeated colors
 * (black, the cset's "transparent" entry), gradients. */
void game_like_palette(conf::Rng &rng, RGB *p)
{
    for(int cs = 0; cs < 16; ++cs)
    {
        for(int i = 0; i < 16; ++i)
        {
            RGB &c = p[cs * 16 + i];
            if(i == 0 || rng.range(0, 9) == 0)
            {
                c.r = c.g = c.b = 0;
            }
            else if(rng.range(0, 3) == 0)
            {
                c = p[rng.range(0, cs * 16 + i - 1)];
            }
            else
            {
                int base = rng.range(0, 40);
                c.r = (unsigned char)((base + i * rng.range(0, 3)) & 63);
                c.g = (unsigned char)((base / 2 + i * 2) & 63);
                c.b = (unsigned char)(rng.range(0, 63));
            }
            c.filler = 0;
        }
    }
}

int any_component(conf::Rng &rng)
{
    switch(rng.range(0, 5))
    {
    case 0: return rng.range(-300, -1);
    case 1: return rng.range(256, 700);
    default: return rng.range(0, 255);
    }
}

struct SaveState
{
    PALETTE pal;
    RGB_MAP *map;
    SaveState() : map(rgb_map) { get_palette(pal); }
    ~SaveState() { set_color_depth(8); set_palette(pal); rgb_map = map; }
};

} // namespace

CONF_TEST(color_formats)
{
    ctx.checkf("shift15", "%d %d %d", _rgb_r_shift_15, _rgb_g_shift_15, _rgb_b_shift_15);
    ctx.checkf("shift16", "%d %d %d", _rgb_r_shift_16, _rgb_g_shift_16, _rgb_b_shift_16);
    ctx.checkf("shift24", "%d %d %d", _rgb_r_shift_24, _rgb_g_shift_24, _rgb_b_shift_24);
    ctx.checkf("shift32", "%d %d %d %d", _rgb_r_shift_32, _rgb_g_shift_32, _rgb_b_shift_32, _rgb_a_shift_32);
    conf::Seq s5(ctx, "scale5");
    for(int i = 0; i < 32; ++i)
        s5.add_int(_rgb_scale_5[i], "_rgb_scale_5[%d]", i);
    s5.end();
    conf::Seq s6(ctx, "scale6");
    for(int i = 0; i < 64; ++i)
        s6.add_int(_rgb_scale_6[i], "_rgb_scale_6[%d]", i);
    s6.end();
}

CONF_TEST(color_makecol)
{
    SaveState keep;
    conf::Seq s(ctx, "makecol_n");
    for(int k = 0; k < 3000; ++k)
    {
        int r = any_component(ctx.rng), g = any_component(ctx.rng), b = any_component(ctx.rng), a = any_component(ctx.rng);
        if(k < 1000) { r &= 255; g &= 255; b &= 255; a &= 255; }
        s.add_hex((unsigned)makecol15(r, g, b), "makecol15(%d,%d,%d)", r, g, b);
        s.add_hex((unsigned)makecol16(r, g, b), "makecol16(%d,%d,%d)", r, g, b);
        s.add_hex((unsigned)makecol24(r, g, b), "makecol24(%d,%d,%d)", r, g, b);
        s.add_hex((unsigned)makecol32(r, g, b), "makecol32(%d,%d,%d)", r, g, b);
        s.add_hex((unsigned)makeacol32(r, g, b, a), "makeacol32(%d,%d,%d,%d)", r, g, b, a);
    }
    s.end();

    const int depths[] = { 8, 15, 16, 24, 32, 7, 0 };
    PALETTE pal;
    random_palette(ctx.rng, pal);
    set_palette(pal);
    conf::Seq d(ctx, "makecol_depth");
    for(int k = 0; k < 400; ++k)
    {
        int r = ctx.rng.range(0, 255), g = ctx.rng.range(0, 255), b = ctx.rng.range(0, 255), a = ctx.rng.range(0, 255);
        for(int i = 0; i < 7; ++i)
        {
            d.add_hex((unsigned)makecol_depth(depths[i], r, g, b), "makecol_depth(%d,%d,%d,%d)", depths[i], r, g, b);
            d.add_hex((unsigned)makeacol_depth(depths[i], r, g, b, a), "makeacol_depth(%d,%d,%d,%d,%d)", depths[i], r, g, b, a);
        }
    }
    d.end();

    conf::Seq c(ctx, "makecol_current");
    for(int i = 0; i < 5; ++i)
    {
        set_color_depth(depths[i]);
        for(int k = 0; k < 100; ++k)
        {
            int r = ctx.rng.range(0, 255), g = ctx.rng.range(0, 255), b = ctx.rng.range(0, 255), a = ctx.rng.range(0, 255);
            c.add_hex((unsigned)makecol(r, g, b), "depth %d makecol(%d,%d,%d)", depths[i], r, g, b);
            c.add_hex((unsigned)makeacol(r, g, b, a), "depth %d makeacol(%d,%d,%d,%d)", depths[i], r, g, b, a);
        }
    }
    c.end();
}

CONF_TEST(color_getrgb)
{
    SaveState keep;
    PALETTE pal;
    random_palette(ctx.rng, pal);
    set_palette(pal);

    conf::Seq s(ctx, "get_n");
    for(int k = 0; k < 3000; ++k)
    {
        int c = (int)ctx.rng.next();
        if(k % 3 == 0) c &= 0xFFFF;
        s.add_int(getr15(c), "getr15(%08x)", c);
        s.add_int(getg15(c), "getg15(%08x)", c);
        s.add_int(getb15(c), "getb15(%08x)", c);
        s.add_int(getr16(c), "getr16(%08x)", c);
        s.add_int(getg16(c), "getg16(%08x)", c);
        s.add_int(getb16(c), "getb16(%08x)", c);
        s.add_int(getr24(c), "getr24(%08x)", c);
        s.add_int(getg24(c), "getg24(%08x)", c);
        s.add_int(getb24(c), "getb24(%08x)", c);
        s.add_int(getr32(c), "getr32(%08x)", c);
        s.add_int(getg32(c), "getg32(%08x)", c);
        s.add_int(getb32(c), "getb32(%08x)", c);
        s.add_int(geta32(c), "geta32(%08x)", c);
    }
    s.end();

    conf::Seq p8(ctx, "get8");
    for(int c = 0; c < 256; ++c)
    {
        p8.add_int(getr8(c), "getr8(%d)", c);
        p8.add_int(getg8(c), "getg8(%d)", c);
        p8.add_int(getb8(c), "getb8(%d)", c);
    }
    p8.end();

    const int depths[] = { 8, 15, 16, 24, 32, 7 };
    conf::Seq d(ctx, "get_depth");
    for(int k = 0; k < 300; ++k)
    {
        for(int i = 0; i < 6; ++i)
        {
            int c = depths[i] == 8 ? ctx.rng.range(0, 255) : (int)ctx.rng.next();
            d.add_int(getr_depth(depths[i], c), "getr_depth(%d,%08x)", depths[i], c);
            d.add_int(getg_depth(depths[i], c), "getg_depth(%d,%08x)", depths[i], c);
            d.add_int(getb_depth(depths[i], c), "getb_depth(%d,%08x)", depths[i], c);
            d.add_int(geta_depth(depths[i], c), "geta_depth(%d,%08x)", depths[i], c);
        }
    }
    d.end();

    conf::Seq g(ctx, "get_current");
    for(int i = 0; i < 5; ++i)
    {
        set_color_depth(depths[i]);
        for(int k = 0; k < 100; ++k)
        {
            int c = depths[i] == 8 ? ctx.rng.range(0, 255) : (int)ctx.rng.next();
            g.add_int(getr(c), "depth %d getr(%08x)", depths[i], c);
            g.add_int(getg(c), "depth %d getg(%08x)", depths[i], c);
            g.add_int(getb(c), "depth %d getb(%08x)", depths[i], c);
            g.add_int(geta(c), "depth %d geta(%08x)", depths[i], c);
        }
    }
    g.end();

    /* round trips through each format */
    conf::Seq rt(ctx, "round_trip");
    for(int k = 0; k < 500; ++k)
    {
        int r = ctx.rng.range(0, 255), gg = ctx.rng.range(0, 255), b = ctx.rng.range(0, 255);
        int c15 = makecol15(r, gg, b), c16 = makecol16(r, gg, b);
        rt.add_int(getr15(c15) * 65536 + getg15(c15) * 256 + getb15(c15), "15 (%d,%d,%d)", r, gg, b);
        rt.add_int(getr16(c16) * 65536 + getg16(c16) * 256 + getb16(c16), "16 (%d,%d,%d)", r, gg, b);
    }
    rt.end();
}

CONF_TEST(color_bestfit)
{
    PALETTE pal;
    conf::Seq s(ctx, "bestfit");
    for(int t = 0; t < 40; ++t)
    {
        if(t % 2)
            game_like_palette(ctx.rng, pal);
        else
            random_palette(ctx.rng, pal);
        if(t % 5 == 0)
        {
            pal[0].r = 63; pal[0].g = 0; pal[0].b = 63;
        }
        for(int q = 0; q < 300; ++q)
        {
            int r, g, b;
            switch(q % 6)
            {
            case 0: r = 63; g = 0; b = 63; break;
            case 1: { int i = ctx.rng.range(0, 255); r = pal[i].r; g = pal[i].g; b = pal[i].b; break; }
            case 2: r = ctx.rng.range(-200, 300); g = ctx.rng.range(-200, 300); b = ctx.rng.range(-200, 300); break;
            default: r = ctx.rng.range(0, 63); g = ctx.rng.range(0, 63); b = ctx.rng.range(0, 63); break;
            }
            s.add_int(bestfit_color(pal, r, g, b), "palette %d bestfit(%d,%d,%d)", t, r, g, b);
        }
    }
    s.end();
}

CONF_TEST(color_makecol8)
{
    SaveState keep;
    PALETTE pal;
    game_like_palette(ctx.rng, pal);
    set_palette(pal);
    rgb_map = NULL;
    conf::Seq s(ctx, "no_map");
    for(int k = 0; k < 3000; ++k)
    {
        int r = any_component(ctx.rng), g = any_component(ctx.rng), b = any_component(ctx.rng);
        s.add_int(makecol8(r, g, b), "makecol8(%d,%d,%d)", r, g, b);
    }
    s.end();

    /* with an arbitrary rgb_map (only the lookup is tested here) */
    static RGB_MAP map;
    for(int r = 0; r < 32; ++r)
        for(int g = 0; g < 32; ++g)
            for(int b = 0; b < 32; ++b)
                map.data[r][g][b] = (unsigned char)ctx.rng.range(0, 255);
    rgb_map = &map;
    conf::Seq m(ctx, "with_map");
    for(int k = 0; k < 3000; ++k)
    {
        int r = ctx.rng.range(0, 255), g = ctx.rng.range(0, 255), b = ctx.rng.range(0, 255);
        m.add_int(makecol8(r, g, b), "makecol8(%d,%d,%d)", r, g, b);
        m.add_int(makecol(r, g, b), "makecol(%d,%d,%d)", r, g, b);
    }
    m.end();
}

namespace
{
int g_rgb_calls;
int g_rgb_pos_hash;
void rgb_callback(int pos)
{
    ++g_rgb_calls;
    g_rgb_pos_hash = g_rgb_pos_hash * 31 + pos;
}
}

CONF_TEST(color_rgb_table)
{
    static RGB_MAP table;
    PALETTE pal;
    conf::Seq s(ctx, "tables");
    for(int t = 0; t < 12; ++t)
    {
        switch(t % 4)
        {
        case 0: random_palette(ctx.rng, pal); break;
        case 1: game_like_palette(ctx.rng, pal); break;
        case 2: memset(pal, 0, sizeof pal); pal[1].r = pal[1].g = pal[1].b = 63; break;
        default:
            for(int i = 0; i < PAL_SIZE; ++i)
            {
                pal[i].r = (unsigned char)(i >> 2);
                pal[i].g = (unsigned char)((i * 3) & 63);
                pal[i].b = (unsigned char)(63 - (i >> 2));
            }
            break;
        }
        if(t == 5)
        {
            pal[0].r = 63; pal[0].g = 0; pal[0].b = 63;
        }
        memset(&table, 0xEE, sizeof table);
        create_rgb_table(&table, pal, NULL);
        s.add_mem(&table, sizeof table, "palette %d", t);
    }
    s.end();

    g_rgb_calls = 0;
    g_rgb_pos_hash = 0;
    random_palette(ctx.rng, pal);
    create_rgb_table(&table, pal, rgb_callback);
    ctx.check_int("callback_calls", g_rgb_calls);
    ctx.check_int("callback_pos_hash", g_rgb_pos_hash);
}

/* How far a table is from the exact nearest-color answer: the number of
 * cells naming bestfit_color()'s choice for the cell color (2r, 2g, 2b),
 * and the number naming some entry at the same minimal distance. Shows
 * the size of a difference in color_rgb_table. */
CONF_TEST(color_rgb_table_quality)
{
    static RGB_MAP table;
    PALETTE pal;
    conf::Seq s(ctx, "quality");
    for(int t = 0; t < 6; ++t)
    {
        if(t & 1)
            game_like_palette(ctx.rng, pal);
        else
            random_palette(ctx.rng, pal);
        create_rgb_table(&table, pal, NULL);
        int same = 0, optimal = 0;
        for(int r = 0; r < 32; ++r)
            for(int g = 0; g < 32; ++g)
                for(int b = 0; b < 32; ++b)
                {
                    int v = table.data[r][g][b];
                    int e = bestfit_color(pal, 2 * r, 2 * g, 2 * b);
                    same += v == e;
                    int dv = 900 * (pal[v].r - 2 * r) * (pal[v].r - 2 * r) + 3481 * (pal[v].g - 2 * g) * (pal[v].g - 2 * g) + 121 * (pal[v].b - 2 * b) * (pal[v].b - 2 * b);
                    int de = 900 * (pal[e].r - 2 * r) * (pal[e].r - 2 * r) + 3481 * (pal[e].g - 2 * g) * (pal[e].g - 2 * g) + 121 * (pal[e].b - 2 * b) * (pal[e].b - 2 * b);
                    optimal += dv == de;
                }
        s.add_str(conf::nm("same %d optimal %d of 32768", same, optimal), "palette %d (%s)", t, (t & 1) ? "game-like" : "random");
    }
    s.end();
}

/* Separate from color_rgb_table: properties every correct table has, so
 * that a failure of the exact comparison can be judged. */
CONF_TEST(color_rgb_table_zero_entries)
{
    static RGB_MAP table;
    PALETTE pal;
    conf::Seq s(ctx, "pink_cell");
    for(int t = 0; t < 6; ++t)
    {
        random_palette(ctx.rng, pal);
        if(t & 1)
        {
            pal[0].r = 63; pal[0].g = 0; pal[0].b = 63;
        }
        if(t == 4)
        {
            pal[0].r = 62; pal[0].g = 1; pal[0].b = 63;   /* same cell, not exactly pink */
        }
        create_rgb_table(&table, pal, NULL);
        int zeros = 0;
        for(int r = 0; r < 32; ++r)
            for(int g = 0; g < 32; ++g)
                for(int b = 0; b < 32; ++b)
                    zeros += table.data[r][g][b] == 0;
        s.add_int(table.data[31][0][31] == 0, "palette %d pink cell is 0", t);
        s.add_int(zeros, "palette %d zero entries", t);
    }
    s.end();
}

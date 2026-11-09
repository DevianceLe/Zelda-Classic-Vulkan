/*
 * test_color_palette.cpp - palettes must behave like the reference build:
 * set_palette / set_palette_range / set_color, the get_* readers (filler
 * bytes included), the built-in palettes, palette_color for every color
 * depth, select_palette / unselect_palette and fade_interpolate (also with
 * positions outside 0..64, which the game passes).
 *
 * All tests restore the palette and color depth they change.
 */
#include "conf.h"

#include <string.h>

namespace
{

void random_palette(conf::Rng &rng, RGB *p, int maxv = 63)
{
    for(int i = 0; i < PAL_SIZE; ++i)
    {
        p[i].r = (unsigned char)rng.range(0, maxv);
        p[i].g = (unsigned char)rng.range(0, maxv);
        p[i].b = (unsigned char)rng.range(0, maxv);
        p[i].filler = (unsigned char)rng.range(0, 255);
    }
}

struct SavePalette
{
    PALETTE saved;
    SavePalette() { get_palette(saved); }
    ~SavePalette() { set_palette(saved); set_color_depth(8); }
};

} // namespace

CONF_TEST(color_palette_set_get)
{
    SavePalette keep;
    PALETTE p, q;

    random_palette(ctx.rng, p);
    set_palette(p);
    memset(q, 0xA5, sizeof q);
    get_palette(q);
    ctx.check_mem("get_palette", q, sizeof q);            /* filler copied too */
    ctx.check_mem("current", _current_palette, sizeof(PALETTE));

    conf::Seq s(ctx, "ranges");
    for(int k = 0; k < 60; ++k)
    {
        random_palette(ctx.rng, p);
        int from = ctx.rng.range(0, 255), to = ctx.rng.range(0, 255);
        if(k % 5 == 0) to = from;
        if(k % 7 == 0) { from = 0; to = 255; }
        set_palette_range(p, from, to, k & 1);
        memset(q, 0x5A, sizeof q);
        int gf = ctx.rng.range(0, 255), gt = ctx.rng.range(0, 255);
        get_palette_range(q, gf, gt);
        s.add_mem(_current_palette, sizeof(PALETTE), "set %d..%d", from, to);
        s.add_mem(q, sizeof q, "get %d..%d after set %d..%d", gf, gt, from, to);
    }
    s.end();

    conf::Seq c(ctx, "set_color");
    for(int k = 0; k < 200; ++k)
    {
        RGB v;
        v.r = (unsigned char)ctx.rng.range(0, 255);   /* stored raw, even above 63 */
        v.g = (unsigned char)ctx.rng.range(0, 255);
        v.b = (unsigned char)ctx.rng.range(0, 255);
        v.filler = (unsigned char)ctx.rng.range(0, 255);
        int idx = ctx.rng.range(0, 255);
        set_color(idx, &v);
        RGB out;
        memset(&out, 0x33, sizeof out);
        get_color(idx, &out);
        c.add_mem(&out, sizeof out, "set_color(%d, %d,%d,%d,%d)", idx, v.r, v.g, v.b, v.filler);
        c.add_mem(&_current_palette[idx], sizeof(RGB), "current[%d]", idx);
    }
    c.end();
    ctx.check_mem("final", _current_palette, sizeof(PALETTE));
}

CONF_TEST(color_palette_builtin)
{
    ctx.check_mem("black_palette", black_palette, sizeof(PALETTE));
    ctx.check_mem("desktop_palette", desktop_palette, sizeof(PALETTE));
    ctx.check_mem("default_palette", default_palette, sizeof(PALETTE));
    ctx.check_palette("default_rgb", default_palette);
}

CONF_TEST(color_palette_color_tables)
{
    SavePalette keep;
    PALETTE p;

    /* no graphics mode: the 8-bit table is still all zero */
    set_color_depth(8);
    conf::Seq z(ctx, "depth8_before");
    for(int i = 0; i < 256; ++i)
        z.add_int(palette_color[i], "palette_color[%d]", i);
    z.end();

    const int depths[] = { 15, 16, 24, 32, 8 };
    for(int round = 0; round < 3; ++round)
    {
        for(int d = 0; d < 5; ++d)
        {
            set_color_depth(depths[d]);
            random_palette(ctx.rng, p);
            if(round == 1)
                set_palette_range(p, ctx.rng.range(0, 100), ctx.rng.range(100, 255), 0);
            else
                set_palette(p);
            conf::Seq s(ctx, conf::nm("round%d_depth%d", round, depths[d]));
            for(int i = 0; i < 256; ++i)
                s.add_int(palette_color[i], "palette_color[%d]", i);
            s.end();
        }
        /* switching depth shows each depth's own table */
        for(int d = 0; d < 5; ++d)
        {
            set_color_depth(depths[d]);
            conf::Seq s(ctx, conf::nm("round%d_view%d", round, depths[d]));
            for(int i = 0; i < 256; ++i)
                s.add_int(palette_color[i], "palette_color[%d]", i);
            s.end();
        }
    }

    /* set_color outside 8-bit updates one entry */
    set_color_depth(16);
    RGB v = { 40, 50, 60, 0 };
    set_color(77, &v);
    ctx.check_int("set_color_16", palette_color[77]);
    ctx.check_int("neighbour_16", palette_color[78]);
}

CONF_TEST(color_palette_select)
{
    SavePalette keep;
    PALETTE a, b, q;
    const int depths[] = { 8, 16, 32 };
    for(int d = 0; d < 3; ++d)
    {
        set_color_depth(depths[d]);
        random_palette(ctx.rng, a);
        random_palette(ctx.rng, b);
        set_palette(a);
        select_palette(b);
        get_palette(q);
        ctx.check_mem(conf::nm("selected_%d", depths[d]), q, sizeof q);
        conf::Seq s(ctx, conf::nm("selected_table_%d", depths[d]));
        for(int i = 0; i < 256; ++i)
            s.add_int(palette_color[i], "palette_color[%d]", i);
        s.end();
        unselect_palette();
        get_palette(q);
        ctx.check_mem(conf::nm("restored_%d", depths[d]), q, sizeof q);
        conf::Seq t(ctx, conf::nm("restored_table_%d", depths[d]));
        for(int i = 0; i < 256; ++i)
            t.add_int(palette_color[i], "palette_color[%d]", i);
        t.end();
    }
}

CONF_TEST(color_fade_interpolate)
{
    PALETTE src, dst, out;
    conf::Seq s(ctx, "fade");
    for(int k = 0; k < 400; ++k)
    {
        random_palette(ctx.rng, src);
        random_palette(ctx.rng, dst);
        if(k % 4 == 0)
            memcpy(dst, black_palette, sizeof dst);
        int pos;
        switch(k % 5)
        {
        case 0: pos = ctx.rng.range(0, 64); break;
        case 1: pos = ctx.rng.range(-80, 0); break;
        case 2: pos = ctx.rng.range(64, 160); break;
        case 3: pos = (ctx.rng.range(130, 180) - 138) << 1; break;   /* link.cpp */
        default: pos = (k / 5) % 70; break;
        }
        int from = ctx.rng.range(0, 255), to = ctx.rng.range(0, 255);
        if(k % 3 == 0) { from = 0; to = 255; }
        memset(out, 0x77, sizeof out);
        random_palette(ctx.rng, out);
        fade_interpolate(src, dst, out, pos, from, to);
        s.add_mem(out, sizeof out, "pos %d range %d..%d", pos, from, to);
    }
    s.end();

    /* in place, as pal.cpp does (output == source) */
    conf::Seq ip(ctx, "in_place");
    random_palette(ctx.rng, src);
    for(int pos = -2; pos <= 66; ++pos)
    {
        fade_interpolate(src, black_palette, src, pos, 16, 239);
        ip.add_mem(src, sizeof src, "pos %d", pos);
    }
    ip.end();
}

/*
 * test_color_blend.cpp - truecolor blender state and the trans blenders
 * must match the reference build bit for bit: set_trans_blender (the game
 * calls set_trans_blender(0, 0, 0, 128)), set_blender_mode,
 * set_blender_mode_ex, the installed functions called directly on random
 * colors (also with stray high bits and factors above 255), and
 * translucent putpixel/rectfill on 15/16/24/32-bit bitmaps.
 */
#include "conf.h"
#include <allegro/internal/aintern.h>

namespace
{

unsigned long my_b15(unsigned long x, unsigned long y, unsigned long n) { return x * 3 + y + n; }
unsigned long my_b16(unsigned long x, unsigned long y, unsigned long n) { return x ^ y ^ n; }
unsigned long my_b24(unsigned long x, unsigned long y, unsigned long n) { return (x + y) * (n + 1); }
unsigned long my_b32(unsigned long x, unsigned long y, unsigned long n) { return x - y + n; }
unsigned long my_bx(unsigned long x, unsigned long y, unsigned long n) { return x + 2 * y + 3 * n; }

const char *which(BLENDER_FUNC f)
{
    if(!f) return "NULL";
    if(f == my_b15) return "b15";
    if(f == my_b16) return "b16";
    if(f == my_b24) return "b24";
    if(f == my_b32) return "b32";
    if(f == my_bx) return "bx";
    return "other";
}

void state(conf::Ctx &ctx, const char *c)
{
    ctx.checkf(c, "alpha %d col15 %08x col16 %08x col24 %08x col32 %08x", _blender_alpha,
               (unsigned)_blender_col_15, (unsigned)_blender_col_16, (unsigned)_blender_col_24, (unsigned)_blender_col_32);
}

void calls(conf::Ctx &ctx, const char *c, int count)
{
    conf::Seq s(ctx, c);
    BLENDER_FUNC fs[7] = { _blender_func15, _blender_func16, _blender_func24, _blender_func32,
                           _blender_func15x, _blender_func16x, _blender_func24x };
    const char *names[7] = { "15", "16", "24", "32", "15x", "16x", "24x" };
    for(int k = 0; k < count; ++k)
    {
        unsigned long x = ctx.rng.next(), y = ctx.rng.next(), n;
        switch(k % 4)
        {
        case 0: x &= 0xFFFF; y &= 0xFFFF; n = (unsigned long)ctx.rng.range(0, 255); break;
        case 1: x &= 0xFFFFFF; y &= 0xFFFFFF; n = (unsigned long)ctx.rng.range(0, 255); break;
        case 2: n = (unsigned long)ctx.rng.range(0, 400); break;
        default: n = (unsigned long)_blender_alpha; break;
        }
        for(int i = 0; i < 7; ++i)
            if(fs[i])
                s.add_hex(fs[i](x, y, n), "func%s(%08lx,%08lx,%lu)", names[i], x, y, n);
    }
    s.end();
}

} // namespace

CONF_TEST(color_blend_initial)
{
    /* before any blender mode is set (first test of the program) */
    ctx.checkf("funcs", "%s %s %s %s %s %s %s", which(_blender_func15), which(_blender_func16), which(_blender_func24),
               which(_blender_func32), which(_blender_func15x), which(_blender_func16x), which(_blender_func24x));
    state(ctx, "state");
}

CONF_TEST(color_blend_trans)
{
    conf::Seq st(ctx, "states");
    for(int k = 0; k < 40; ++k)
    {
        int r = ctx.rng.range(0, 255), g = ctx.rng.range(0, 255), b = ctx.rng.range(0, 255), a = ctx.rng.range(0, 255);
        if(k == 0) { r = g = b = 0; a = 128; }        /* subscr.cpp */
        if(k % 7 == 3) { r = ctx.rng.range(-50, 400); a = ctx.rng.range(-10, 300); }
        set_trans_blender(r, g, b, a);
        st.add_str(conf::nm("alpha %d col15 %08x col16 %08x col24 %08x col32 %08x", _blender_alpha,
                            (unsigned)_blender_col_15, (unsigned)_blender_col_16, (unsigned)_blender_col_24,
                            (unsigned)_blender_col_32), "set_trans_blender(%d,%d,%d,%d)", r, g, b, a);
    }
    st.end();
    set_trans_blender(0, 0, 0, 128);
    ctx.check_bool("f32_is_f24", _blender_func32 == _blender_func24);
    ctx.check_bool("x_same", _blender_func15x == _blender_func16x && _blender_func16x == _blender_func24x);
    ctx.check_bool("x_differs", _blender_func15x != _blender_func15);
    calls(ctx, "trans_calls", 4000);
}

CONF_TEST(color_blend_modes)
{
    set_blender_mode(my_b15, my_b16, my_b24, 10, 200, 33, 99);
    ctx.checkf("mode_funcs", "%s %s %s %s", which(_blender_func15), which(_blender_func16), which(_blender_func24), which(_blender_func32));
    state(ctx, "mode_state");
    /* the x blenders set_blender_mode installs */
    {
        conf::Seq s(ctx, "mode_x_calls");
        for(int k = 0; k < 200; ++k)
        {
            unsigned long x = ctx.rng.next(), y = ctx.rng.next(), n = (unsigned long)ctx.rng.range(0, 255);
            s.add_hex(_blender_func15x(x, y, n), "15x(%08lx,%08lx,%lu)", x, y, n);
            s.add_hex(_blender_func16x(x, y, n), "16x(%08lx,%08lx,%lu)", x, y, n);
            s.add_hex(_blender_func24x(x, y, n), "24x(%08lx,%08lx,%lu)", x, y, n);
        }
        s.end();
    }
    set_blender_mode_ex(my_b15, my_b16, my_b24, my_b32, my_bx, my_bx, NULL, 1, 2, 3, 4);
    ctx.checkf("ex_funcs", "%s %s %s %s %s %s %s", which(_blender_func15), which(_blender_func16), which(_blender_func24),
               which(_blender_func32), which(_blender_func15x), which(_blender_func16x), which(_blender_func24x));
    state(ctx, "ex_state");
    set_trans_blender(0, 0, 0, 128);
}

/* Translucent drawing on hicolor/truecolor bitmaps goes through the
 * installed blender (primitives module + this module's blenders). */
CONF_TEST(color_blend_draw)
{
    const int depths[] = { 15, 16, 24, 32 };
    for(int d = 0; d < 4; ++d)
    {
        BITMAP *b = create_bitmap_ex(depths[d], 24, 16);
        conf::Seq s(ctx, conf::nm("depth%d", depths[d]));
        for(int k = 0; k < 12; ++k)
        {
            int a = ctx.rng.range(0, 255);
            if(k == 0) a = 128;
            set_trans_blender(ctx.rng.range(0, 255), ctx.rng.range(0, 255), ctx.rng.range(0, 255), a);
            for(int y = 0; y < b->h; ++y)
                for(int x = 0; x < b->w; ++x)
                    putpixel(b, x, y, makecol_depth(depths[d], ctx.rng.range(0, 255), ctx.rng.range(0, 255), ctx.rng.range(0, 255)));
            drawing_mode(DRAW_MODE_TRANS, NULL, 0, 0);
            int c = makecol_depth(depths[d], ctx.rng.range(0, 255), ctx.rng.range(0, 255), ctx.rng.range(0, 255));
            rectfill(b, 2, 3, 20, 12, c);
            for(int i = 0; i < 20; ++i)
                putpixel(b, ctx.rng.range(0, 23), ctx.rng.range(0, 15), makecol_depth(depths[d], ctx.rng.range(0, 255), 0, 255));
            solid_mode();
            s.add_bitmap(b, "alpha %d", a);
        }
        s.end();
        destroy_bitmap(b);
    }
    set_trans_blender(0, 0, 0, 128);
}

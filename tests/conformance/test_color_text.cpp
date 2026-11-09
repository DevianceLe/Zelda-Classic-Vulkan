/*
 * test_color_text.cpp - text output and metrics must be pixel-exact against
 * the reference build: the built-in default font (every glyph), mono and
 * color fonts built identically on both sides (test_color_fontkit.h) with
 * glyphs narrower, shorter and taller than the font, fg/bg combinations
 * (-1 = transparent), clipping, sub-bitmaps, drawing modes, every color
 * depth, allegro_404_char, textout_centre/right/justify and the textprintf
 * family with the formats the game uses.
 *
 * Strings are ASCII or UTF-8 (the default text format; the game switches
 * to U_ASCII, which the unicode module covers).
 */
#include "conf.h"
#include "test_color_fontkit.h"

#include <stdio.h>
#include <string.h>
#include <string>

namespace
{

/* UTF-8 encoding of one code point (enough for the test strings). */
void put_utf8(std::string &s, int c)
{
    if(c < 0x80)
        s += (char)c;
    else if(c < 0x800)
    {
        s += (char)(0xC0 | (c >> 6));
        s += (char)(0x80 | (c & 0x3F));
    }
    else
    {
        s += (char)(0xE0 | (c >> 12));
        s += (char)(0x80 | ((c >> 6) & 0x3F));
        s += (char)(0x80 | (c & 0x3F));
    }
}

/* Printable form of a test string for labels (bytes >= 0x7F and controls as \xNN). */
const char *esc(const std::string &t);
const char *esc(const char *t) { return esc(std::string(t)); }
const char *esc(const std::string &t)
{
    static std::string out[4];
    static int slot = 0;
    std::string &o = out[slot++ & 3];
    o.clear();
    for(size_t i = 0; i < t.size(); ++i)
    {
        unsigned char c = (unsigned char)t[i];
        if(c < 0x20 || c >= 0x7F || c == '\\' || c == '"')
        {
            char buf[8];
            sprintf(buf, "\\x%02X", c);
            o += buf;
        }
        else
            o += (char)c;
    }
    return o.c_str();
}

colorkit::Glyph random_glyph(conf::Rng &rng, int w, int h, bool mono)
{
    colorkit::Glyph g;
    g.w = w;
    g.h = h;
    for(int i = 0; i < w * h; ++i)
    {
        if(mono)
            g.px.push_back((unsigned char)(rng.range(0, 2) == 0 ? 1 : 0));
        else
            g.px.push_back((unsigned char)(rng.range(0, 3) == 0 ? 0 : rng.range(1, 255)));
    }
    return g;
}

/* A font with one or two ranges inside 33..126 and glyphs of varied size. */
colorkit::Spec random_spec(conf::Rng &rng, bool mono, int height, bool with_404)
{
    colorkit::Spec s;
    s.height = height;
    s.mono = mono;
    int nr = rng.range(1, 2);
    int lo = 33;
    for(int r = 0; r < nr; ++r)
    {
        colorkit::Range rg;
        rg.begin = lo + rng.range(0, 10);
        rg.end = rg.begin + rng.range(5, 25);
        if(rg.end > 127) rg.end = 127;
        lo = rg.end + rng.range(1, 5);
        for(int c = rg.begin; c < rg.end; ++c)
        {
            int w = rng.range(1, 12);
            int h;
            switch(rng.range(0, 4))
            {
            case 0: h = rng.range(1, height); break;
            case 1: h = height + rng.range(1, 3); break;
            default: h = height; break;
            }
            if(!mono && h > height + 1) h = height;
            rg.glyphs.push_back(random_glyph(rng, w, h, mono));
        }
        s.ranges.push_back(rg);
        if(lo > 110) break;
    }
    if(with_404)
    {
        colorkit::Range rg;
        rg.begin = '^';
        rg.end = '^' + 1;
        rg.glyphs.push_back(random_glyph(rng, rng.range(2, 6), height, mono));
        s.ranges.push_back(rg);
    }
    return s;
}

/* A string of characters from the font's ranges plus some it lacks. */
std::string random_text(conf::Rng &rng, const colorkit::Spec &s, int len)
{
    std::string t;
    for(int i = 0; i < len; ++i)
    {
        if(rng.range(0, 7) == 0)
            put_utf8(t, rng.range(0, 3) == 0 ? rng.range(0x80, 0x17F) : rng.range(33, 126));
        else
        {
            const colorkit::Range &rg = s.ranges[(size_t)rng.range(0, (int)s.ranges.size() - 1)];
            t += (char)rng.range(rg.begin, rg.end - 1);
        }
    }
    return t;
}

int color_for(conf::Rng &rng, int depth)
{
    switch(rng.range(0, 3))
    {
    case 0: return -1;
    default:
        if(depth == 8)
            return rng.range(0, 255);
        return makecol_depth(depth, rng.range(0, 255), rng.range(0, 255), rng.range(0, 255));
    }
}

COLOR_MAP g_map;
void init_map()
{
    for(int a = 0; a < PAL_SIZE; ++a)
        for(int b = 0; b < PAL_SIZE; ++b)
            g_map.data[a][b] = (unsigned char)((a * 5 + b * 3 + (a ^ b)) & 0xFF);
}

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

void fill_random(conf::Rng &rng, BITMAP *b)
{
    int depth = bitmap_color_depth(b);
    for(int y = 0; y < b->h; ++y)
        for(int x = 0; x < b->w; ++x)
            putpixel(b, x, y, depth == 8 ? rng.range(0, 255) : makecol_depth(depth, rng.range(0, 255), rng.range(0, 255), rng.range(0, 255)));
}

/* Draws many random strings with one font, on one depth, in one mode. */
void draw_round(conf::Ctx &ctx, conf::Seq &seq, FONT *f, const colorkit::Spec &spec, int depth, int mode, const char *what)
{
    BITMAP *b = create_bitmap_ex(depth, 72, 40);
    BITMAP *sub = create_sub_bitmap(b, 9, 5, 40, 27);
    for(int k = 0; k < 24; ++k)
    {
        fill_random(ctx.rng, b);
        BITMAP *dst = (k % 5 == 4) ? sub : b;
        set_clip_rect(b, 0, 0, b->w - 1, b->h - 1);
        if(k % 3 == 1)
            set_clip_rect(dst, ctx.rng.range(0, 20), ctx.rng.range(0, 15), ctx.rng.range(10, 60), ctx.rng.range(8, 35));
        if(k % 11 == 7)
            set_clip_state(dst, 0);
        int fg = color_for(ctx.rng, depth), bg = color_for(ctx.rng, depth);
        std::string t = random_text(ctx.rng, spec, ctx.rng.range(0, 9));
        int x = ctx.rng.range(-10, 50), y = ctx.rng.range(-8, 30);
        if(k % 11 == 7)
        {
            /* clipping off: stay inside the bitmap (anything else is undefined) */
            int len = text_length(f, t.c_str());
            x = ctx.rng.range(0, 10);
            if(x + len > dst->w)
                t = "";
            y = 4;
        }
        if(mode != DRAW_MODE_SOLID)
            drawing_mode(mode, NULL, 0, 0);
        textout_ex(dst, f, t.c_str(), x, y, fg, bg);
        solid_mode();
        set_clip_state(dst, 1);
        seq.add_bitmap(b, "%s depth %d mode %d k %d textout(\"%s\", %d, %d, fg %d, bg %d)", what, depth, mode, k, esc(t), x, y, fg, bg);
        seq.add_int(text_length(f, t.c_str()), "%s text_length(\"%s\")", what, esc(t));
    }
    destroy_bitmap(sub);
    destroy_bitmap(b);
}

struct SaveState
{
    PALETTE pal;
    COLOR_MAP *map;
    int c404;
    SaveState() : map(color_map), c404(allegro_404_char) { get_palette(pal); }
    ~SaveState() { solid_mode(); color_map = map; set_palette(pal); set_color_depth(8); allegro_404_char = c404; }
};

} // namespace

CONF_TEST(color_text_default_font)
{
    ctx.check_int("height", text_height(font));
    ctx.check_int("vt_height", font->vtable->font_height(font));
    int n = get_font_ranges(font);
    ctx.check_int("ranges", n);
    for(int r = 0; r < n; ++r)
        ctx.checkf(conf::nm("range%d", r), "%d..%d", get_font_range_begin(font, r), get_font_range_end(font, r));
    ctx.checkf("range_all", "%d..%d", get_font_range_begin(font, -1), get_font_range_end(font, -1));
    ctx.checkf("range_bad", "%d %d", get_font_range_begin(font, n), get_font_range_end(font, n + 3));
    ctx.check_bool("is_mono", is_mono_font(font) != 0);
    ctx.check_bool("is_color", is_color_font(font) != 0);

    conf::Seq cl(ctx, "char_length");
    for(int c = 0; c < 9000; ++c)
        cl.add_int(font->vtable->char_length(font, c), "char_length(%d)", c);
    cl.end();

    BITMAP *b = create_bitmap_ex(8, 10, 10);
    conf::Seq g(ctx, "glyphs");
    for(int r = 0; r < n; ++r)
    {
        for(int c = get_font_range_begin(font, r); c <= get_font_range_end(font, r); ++c)
        {
            clear_to_color(b, 7);
            int w = font->vtable->render_char(font, c, 1, -1, b, 1, 1);
            g.add_bitmap(b, "render_char(%d, 1, -1)", c);
            g.add_int(w, "render_char(%d) width", c);
            clear_to_color(b, 7);
            font->vtable->render_char(font, c, 200, 3, b, 2, 0);
            g.add_bitmap(b, "render_char(%d, 200, 3)", c);
        }
    }
    g.end();
    destroy_bitmap(b);

    const char *strings[] = { "Hello, World!", "", "~`!@#$%^&*()_+-={}[]|\\:;\"'<>,.?/", "Link 0123456789", "\x7f", "a\tb\nc" };
    BITMAP *t = create_bitmap_ex(8, 300, 12);
    conf::Seq s(ctx, "strings");
    for(int i = 0; i < 6; ++i)
    {
        clear_to_color(t, 0);
        textout_ex(t, font, strings[i], 3, 2, 15, -1);
        s.add_bitmap(t, "textout(\"%s\")", esc(strings[i]));
        s.add_int(text_length(font, strings[i]), "text_length(\"%s\")", esc(strings[i]));
    }
    /* code points above 127 (UTF-8 in the default text format) */
    for(int i = 0; i < 40; ++i)
    {
        std::string u;
        for(int k = 0; k < 12; ++k)
        {
            int c;
            switch(ctx.rng.range(0, 4))
            {
            case 0: c = ctx.rng.range(0x80, 0xA0); break;   /* missing */
            case 1: c = 0x20AC; break;
            case 2: c = ctx.rng.range(0x100, 0x17F); break;
            default: c = ctx.rng.range(0xA1, 0xFF); break;
            }
            put_utf8(u, c);
        }
        clear_to_color(t, 0);
        textout_ex(t, font, u.c_str(), ctx.rng.range(-5, 5), ctx.rng.range(-3, 5), ctx.rng.range(0, 255), ctx.rng.range(-1, 20));
        s.add_bitmap(t, "utf8 string %d", i);
        s.add_int(text_length(font, u.c_str()), "utf8 text_length %d", i);
    }
    s.end();
    destroy_bitmap(t);
}

CONF_TEST(color_text_mono)
{
    SaveState keep;
    init_map();
    color_map = &g_map;
    PALETTE pal;
    random_palette(ctx.rng, pal);
    set_palette(pal);
    set_trans_blender(0, 0, 0, 128);
    const int depths[] = { 8, 15, 16, 24, 32 };
    for(int t = 0; t < 8; ++t)
    {
        colorkit::Spec spec = random_spec(ctx.rng, true, ctx.rng.range(1, 16), t % 2 == 0);
        FONT *f = colorkit::build(spec);
        conf::Seq s(ctx, conf::nm("font%d", t));
        s.add_int(text_height(f), "text_height");
        draw_round(ctx, s, f, spec, 8, DRAW_MODE_SOLID, "solid");
        draw_round(ctx, s, f, spec, 8, t % 2 ? DRAW_MODE_XOR : DRAW_MODE_TRANS, "mode");
        draw_round(ctx, s, f, spec, depths[1 + t % 4], DRAW_MODE_SOLID, "deep");
        s.end();
        colorkit::release(f);
    }
}

CONF_TEST(color_text_color)
{
    SaveState keep;
    init_map();
    color_map = &g_map;
    PALETTE pal;
    random_palette(ctx.rng, pal);
    set_palette(pal);
    set_trans_blender(0, 0, 0, 100);
    const int depths[] = { 8, 15, 16, 24, 32 };
    for(int t = 0; t < 8; ++t)
    {
        colorkit::Spec spec = random_spec(ctx.rng, false, ctx.rng.range(1, 16), t % 2 == 1);
        FONT *f = colorkit::build(spec);
        conf::Seq s(ctx, conf::nm("font%d", t));
        s.add_int(text_height(f), "text_height");
        draw_round(ctx, s, f, spec, 8, DRAW_MODE_SOLID, "solid");
        draw_round(ctx, s, f, spec, 8, t % 2 ? DRAW_MODE_XOR : DRAW_MODE_TRANS, "mode");
        draw_round(ctx, s, f, spec, depths[1 + t % 4], DRAW_MODE_SOLID, "deep");
        if(t < 4)
            draw_round(ctx, s, f, spec, depths[1 + t % 4], DRAW_MODE_TRANS, "deep_trans");
        s.end();
        colorkit::release(f);
    }
}

CONF_TEST(color_text_vtable)
{
    SaveState keep;
    for(int t = 0; t < 6; ++t)
    {
        bool mono = t < 3;
        colorkit::Spec spec = random_spec(ctx.rng, mono, ctx.rng.range(3, 12), t == 1 || t == 4);
        FONT *f = colorkit::build(spec);
        conf::Seq s(ctx, conf::nm("font%d", t));
        for(int c = 0; c < 300; ++c)
            s.add_int(f->vtable->char_length(f, c), "char_length(%d)", c);
        BITMAP *b = create_bitmap_ex(8, 20, 20);
        for(int c = 30; c < 130; ++c)
        {
            clear_to_color(b, 9);
            int w = f->vtable->render_char(f, c, t == 2 ? -1 : 4, c % 3 ? -1 : 6, b, 3, 2);
            s.add_int(w, "render_char(%d) width", c);
            s.add_bitmap(b, "render_char(%d)", c);
        }
        destroy_bitmap(b);
        s.add_int(get_font_ranges(f), "ranges");
        for(int r = -1; r <= get_font_ranges(f); ++r)
            s.add_str(conf::nm("%d..%d", get_font_range_begin(f, r), get_font_range_end(f, r)), "range %d", r);
        s.add_int(is_mono_font(f) != 0, "is_mono_font");
        s.add_int(is_color_font(f) != 0, "is_color_font");
        s.end();
        colorkit::release(f);
    }
}

CONF_TEST(color_text_404)
{
    SaveState keep;
    colorkit::Spec spec = random_spec(ctx.rng, true, 7, true);
    /* a second glyph to switch allegro_404_char to */
    colorkit::Range extra;
    extra.begin = 'z';
    extra.end = 'z' + 1;
    extra.glyphs.push_back(random_glyph(ctx.rng, 5, 7, true));
    spec.ranges.push_back(extra);
    FONT *f = colorkit::build(spec);
    BITMAP *b = create_bitmap_ex(8, 120, 10);
    const int chars[] = { '^', 'z', '?', 0x41, 0x7E };
    conf::Seq s(ctx, "fallback");
    for(int i = 0; i < 5; ++i)
    {
        allegro_404_char = chars[i];
        std::string t = random_text(ctx.rng, spec, 10);
        clear_to_color(b, 0);
        textout_ex(b, f, t.c_str(), 1, 1, 3, 4);
        s.add_bitmap(b, "404 = %d: \"%s\"", chars[i], esc(t));
        s.add_int(text_length(f, t.c_str()), "404 = %d: text_length", chars[i]);
        s.add_int(f->vtable->char_length(f, 0x7F), "404 = %d: char_length(0x7F)", chars[i]);
        s.add_int(font->vtable->char_length(font, 0x85), "404 = %d: default char_length(0x85)", chars[i]);
    }
    s.end();
    destroy_bitmap(b);
    colorkit::release(f);
}

CONF_TEST(color_text_align)
{
    SaveState keep;
    colorkit::Spec spec = random_spec(ctx.rng, true, 6, false);
    FONT *f = colorkit::build(spec);
    BITMAP *b = create_bitmap_ex(8, 100, 10);
    conf::Seq s(ctx, "align");
    for(int k = 0; k < 120; ++k)
    {
        FONT *ff = (k & 1) ? f : font;
        std::string t = (k & 1) ? random_text(ctx.rng, spec, ctx.rng.range(0, 6)) : std::string("Abc def").substr(0, (size_t)ctx.rng.range(0, 7));
        int x = ctx.rng.range(-20, 120), y = ctx.rng.range(-4, 6);
        clear_to_color(b, 0);
        switch(k % 3)
        {
        case 0: textout_centre_ex(b, ff, t.c_str(), x, y, 5, -1); break;
        case 1: textout_right_ex(b, ff, t.c_str(), x, y, 5, 6); break;
        default: textout_ex(b, ff, t.c_str(), x, y, 5, 6); break;
        }
        s.add_bitmap(b, "k %d \"%s\" at %d,%d", k, esc(t), x, y);
    }
    s.end();
    destroy_bitmap(b);
    colorkit::release(f);
}

CONF_TEST(color_text_justify)
{
    SaveState keep;
    BITMAP *b = create_bitmap_ex(8, 160, 10);
    const char *texts[] = { "A B", "one two three", "a  b   c", " lead trail ", "single", "", "x y z w v u", "AB CD EF GH", "tab\tsep\r\nend" };
    conf::Seq s(ctx, "justify");
    for(int k = 0; k < 160; ++k)
    {
        const char *t = texts[k % 9];
        int x1 = ctx.rng.range(-10, 40), x2 = x1 + ctx.rng.range(-10, 140);
        int len = text_length(font, t);
        int diff;
        switch(k % 4)
        {
        case 0: diff = ctx.rng.range(0, 200); break;
        case 1: diff = x2 - x1 - len; break;           /* boundary */
        case 2: diff = x2 - x1 - len - 1; break;
        default: diff = ctx.rng.range(-20, 20); break;
        }
        clear_to_color(b, 0);
        textout_justify_ex(b, font, t, x1, x2, 1, diff, 9, (k & 8) ? 2 : -1);
        s.add_bitmap(b, "\"%s\" x1 %d x2 %d diff %d", esc(t), x1, x2, diff);
        if(k % 8 == 0)
        {
            clear_to_color(b, 0);
            textprintf_justify_ex(b, font, x1, x2, 1, diff, 3, -1, "%s %d", t, k);
            s.add_bitmap(b, "printf \"%s %d\" x1 %d x2 %d diff %d", esc(t), k, x1, x2, diff);
        }
    }
    s.end();
    destroy_bitmap(b);
}

CONF_TEST(color_text_printf)
{
    SaveState keep;
    BITMAP *b = create_bitmap_ex(8, 320, 10);
    conf::Seq s(ctx, "printf");
    for(int k = 0; k < 60; ++k)
    {
        int v = ctx.rng.range(-100000, 100000);
        int x = ctx.rng.range(-10, 300), y = ctx.rng.range(-3, 4);
        double d = (double)ctx.rng.range(-1000000, 1000000) / 977.0;
        clear_to_color(b, 0);
        switch(k % 10)
        {
        case 0: textprintf_ex(b, font, x, y, 7, -1, "%d", v); break;
        case 1: textprintf_ex(b, font, x, y, 7, 1, "%s", "string arg"); break;
        case 2: textprintf_ex(b, font, x, y, 7, -1, "%c%c", 'A' + k % 26, 'a' + k % 26); break;
        case 3: textprintf_ex(b, font, x, y, 7, -1, "%3d", v % 1000); break;
        case 4: textprintf_ex(b, font, x, y, 7, -1, "%.4f", d); break;
        case 5: textprintf_ex(b, font, x, y, 7, -1, "%-5ld|", (long)(v % 100)); break;
        case 6: textprintf_ex(b, font, x, y, 7, -1, "%X %d:%02X", v & 0xFFFF, k, v & 0xFF); break;
        case 7: textprintf_centre_ex(b, font, x, y, 7, 2, "CSet: %d", v % 16); break;
        case 8: textprintf_right_ex(b, font, x, y, 7, -1, "%5d", v % 100000); break;
        default: textprintf_ex(b, font, x, y, 7, -1, "%2d - %-2d (%d) %s", k, v % 10, v, "end"); break;
        }
        s.add_bitmap(b, "k %d v %d d %.6f at %d,%d", k, v, d, x, y);
    }
    s.end();
    destroy_bitmap(b);

    /* long output: the reference formats into a 512-byte buffer */
    colorkit::Spec spec;
    spec.height = 1;
    spec.mono = true;
    colorkit::Range rg;
    rg.begin = 32;
    rg.end = 127;
    for(int c = 32; c < 127; ++c)
    {
        colorkit::Glyph g;
        g.w = 1 + c % 3;
        g.h = 1;
        for(int i = 0; i < g.w; ++i)
            g.px.push_back((unsigned char)(i == 0));
        rg.glyphs.push_back(g);
    }
    spec.ranges.push_back(rg);
    FONT *f = colorkit::build(spec);
    BITMAP *wide = create_bitmap_ex(8, 2400, 1);
    std::string big;
    for(int i = 0; i < 700; ++i)
        big += (char)('a' + i % 26);
    conf::Seq l(ctx, "long");
    for(int n = 500; n <= 700; n += 3)
    {
        clear_to_color(wide, 0);
        std::string part = big.substr(0, (size_t)n);
        textprintf_ex(wide, f, 0, 0, 1, -1, "%s", part.c_str());
        l.add_bitmap(wide, "printf %d chars", n);
        clear_to_color(wide, 0);
        textout_ex(wide, f, part.c_str(), 0, 0, 1, -1);
        l.add_bitmap(wide, "textout %d chars", n);
        l.add_int(text_length(f, part.c_str()), "text_length %d chars", n);
    }
    l.end();
    destroy_bitmap(wide);
    colorkit::release(f);
}

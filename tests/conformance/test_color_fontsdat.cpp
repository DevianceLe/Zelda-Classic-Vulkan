/*
 * test_color_fontsdat.cpp - the game's fonts (fonts.dat) must measure and
 * draw exactly like the reference build.
 *
 * Reference side: load_datafile("fonts.dat") with the game's password, and
 * the reference's own text code draws its FONT objects.
 * Port side: the datafile loader belongs to another module, so the port
 * program gets the same glyph data from the reference DLL itself: it loads
 * alleg44.dll at run time (LoadLibrary, nothing linked), calls its
 * load_datafile, reads the glyphs out of the reference's font structures
 * (layout as in allegro/include/allegro/internal/aintern.h, mirrored
 * below), and builds zcport fonts from them with zcport/font_internal.h.
 * The DLL is only a data source: all measuring and drawing on the port side
 * is zcport's. Each font's glyph data is hashed on both sides, so a
 * transfer problem shows up as its own failure.
 *
 * Skips when fonts.dat (or, on the port side, alleg44.dll) is missing.
 */
#include "conf.h"
#include "conf_zc.h"
#include "test_color_fontkit.h"

#include <string>
#include <vector>

namespace
{

#define CF_DAT_ID(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
const int CF_DAT_FONT = CF_DAT_ID('F', 'O', 'N', 'T');
const int CF_DAT_END = -1;

struct LoadedFonts
{
    bool ok;
    std::string why;
    std::vector<int> index;                 /* datafile object index of each font */
    std::vector<colorkit::Spec> specs;
    std::vector<FONT *> fonts;              /* what each side draws with */
};

#ifdef CONF_SIDE_REF

DATAFILE *g_dat = NULL;

void load(conf::Ctx &ctx, LoadedFonts &out)
{
    out.ok = false;
    packfile_password(conf::zc::datapwd());
    g_dat = load_datafile(ctx.data("fonts.dat").c_str());
    packfile_password(NULL);
    if(!g_dat)
    {
        out.why = "load_datafile failed";
        return;
    }
    for(int i = 0; g_dat[i].type != DAT_END; ++i)
    {
        if(g_dat[i].type != DAT_FONT)
            continue;
        colorkit::Spec s;
        if(!colorkit::extract((FONT *)g_dat[i].dat, s))
            continue;
        out.index.push_back(i);
        out.specs.push_back(s);
        out.fonts.push_back((FONT *)g_dat[i].dat);
    }
    out.ok = true;
}

void unload(LoadedFonts &out)
{
    if(g_dat)
        unload_datafile(g_dat);
    g_dat = NULL;
    out.fonts.clear();
}

#else /* CONF_SIDE_PORT */

extern "C" __declspec(dllimport) void *__stdcall LoadLibraryA(const char *name);
extern "C" __declspec(dllimport) void *__stdcall GetProcAddress(void *module, const char *name);

/* The reference's structures, as in its headers. */
struct RefDatafile { void *dat; int type; long size; void *prop; };
struct RefFont { void *data; int height; void *vtable; };
struct RefGlyph { short w, h; unsigned char dat[1]; };
struct RefMonoData { int begin, end; RefGlyph **glyphs; RefMonoData *next; };
struct RefBitmap
{
    int w, h, clip, cl, cr, ct, cb;
    void *vtable, *write_bank, *read_bank, *dat;
    unsigned long id;
    void *extra;
    int x_ofs, y_ofs, seg;
    unsigned char *line[1];
};
struct RefColorData { int begin, end; RefBitmap **bitmaps; RefColorData *next; };

typedef int (*ref_install_t)(int, int *, int (*)(void (*)(void)));
typedef void (*ref_password_t)(const char *);
typedef RefDatafile *(*ref_load_t)(const char *);
typedef void (*ref_unload_t)(RefDatafile *);

RefDatafile *g_dat = NULL;
ref_unload_t g_unload = NULL;
int g_ref_errno = 0;

int ref_atexit(void (*fn)(void))
{
    return atexit(fn);
}

void load(conf::Ctx &ctx, LoadedFonts &out)
{
    out.ok = false;
    void *dll = LoadLibraryA("alleg44.dll");
    if(!dll)
    {
        out.why = "alleg44.dll not found";
        return;
    }
    ref_install_t install = (ref_install_t)GetProcAddress(dll, "install_allegro");
    ref_password_t password = (ref_password_t)GetProcAddress(dll, "packfile_password");
    ref_load_t loadf = (ref_load_t)GetProcAddress(dll, "load_datafile");
    g_unload = (ref_unload_t)GetProcAddress(dll, "unload_datafile");
    void **vt_mono = (void **)GetProcAddress(dll, "font_vtable_mono");
    void **vt_color = (void **)GetProcAddress(dll, "font_vtable_color");
    if(!install || !password || !loadf || !g_unload || !vt_mono || !vt_color)
    {
        out.why = "alleg44.dll lacks an export";
        return;
    }
    static bool installed = false;
    if(!installed)
    {
        if(install(CF_DAT_ID('N', 'O', 'N', 'E'), &g_ref_errno, ref_atexit) != 0)
        {
            out.why = "reference install_allegro failed";
            return;
        }
        installed = true;
    }
    password(conf::zc::datapwd());
    g_dat = loadf(ctx.data("fonts.dat").c_str());
    password(NULL);
    if(!g_dat)
    {
        out.why = "load_datafile failed";
        return;
    }
    for(int i = 0; g_dat[i].type != CF_DAT_END; ++i)
    {
        if(g_dat[i].type != CF_DAT_FONT)
            continue;
        RefFont *rf = (RefFont *)g_dat[i].dat;
        colorkit::Spec s;
        s.height = rf->height;
        if(rf->vtable == *vt_mono)
        {
            s.mono = true;
            for(RefMonoData *md = (RefMonoData *)rf->data; md; md = md->next)
            {
                colorkit::Range rg;
                rg.begin = md->begin;
                rg.end = md->end;
                for(int c = 0; c < md->end - md->begin; ++c)
                {
                    RefGlyph *g = md->glyphs[c];
                    colorkit::Glyph gl;
                    gl.w = g->w;
                    gl.h = g->h;
                    int stride = (g->w + 7) / 8;
                    for(int y = 0; y < g->h; ++y)
                        for(int x = 0; x < g->w; ++x)
                            gl.px.push_back((unsigned char)((g->dat[y * stride + x / 8] >> (7 - (x & 7))) & 1));
                    rg.glyphs.push_back(gl);
                }
                s.ranges.push_back(rg);
            }
        }
        else if(rf->vtable == *vt_color)
        {
            s.mono = false;
            for(RefColorData *cd = (RefColorData *)rf->data; cd; cd = cd->next)
            {
                colorkit::Range rg;
                rg.begin = cd->begin;
                rg.end = cd->end;
                for(int c = 0; c < cd->end - cd->begin; ++c)
                {
                    RefBitmap *b = cd->bitmaps[c];
                    colorkit::Glyph gl;
                    gl.w = b->w;
                    gl.h = b->h;
                    for(int y = 0; y < b->h; ++y)
                        for(int x = 0; x < b->w; ++x)
                            gl.px.push_back(b->line[y][x]);
                    rg.glyphs.push_back(gl);
                }
                s.ranges.push_back(rg);
            }
        }
        else
            continue;
        out.index.push_back(i);
        out.specs.push_back(s);
        out.fonts.push_back(colorkit::build(s));
    }
    out.ok = true;
}

void unload(LoadedFonts &out)
{
    for(size_t i = 0; i < out.fonts.size(); ++i)
        colorkit::release(out.fonts[i]);
    out.fonts.clear();
    if(g_dat && g_unload)
        g_unload(g_dat);
    g_dat = NULL;
}

#endif

bool g_loaded = false;
LoadedFonts g_fonts;

LoadedFonts *fonts(conf::Ctx &ctx)
{
    if(!ctx.have_data("fonts.dat"))
    {
        ctx.skip("fonts.dat not found");
        return NULL;
    }
    if(!g_loaded)
    {
        load(ctx, g_fonts);
        g_loaded = true;
    }
    if(!g_fonts.ok)
    {
        ctx.skip("%s", g_fonts.why.c_str());
        return NULL;
    }
    return &g_fonts;
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

std::string range_text(const colorkit::Range &rg, int from, int count)
{
    std::string t;
    for(int c = from; c < rg.end && c < from + count; ++c)
    {
        /* UTF-8 (the default text format) for code points above 127 */
        if(c < 0x80)
            t += (char)c;
        else
        {
            t += (char)(0xC0 | (c >> 6));
            t += (char)(0x80 | (c & 0x3F));
        }
    }
    return t;
}

} // namespace

CONF_TEST(color_fontsdat_glyphs)
{
    LoadedFonts *lf = fonts(ctx);
    if(!lf)
        return;
    ctx.check_int("fonts", (int)lf->specs.size());
    conf::Seq s(ctx, "glyph_data");
    for(size_t i = 0; i < lf->specs.size(); ++i)
    {
        const colorkit::Spec &sp = lf->specs[i];
        s.add_hex(colorkit::hash_spec(sp), "object %d: height %d %s, %d ranges", lf->index[i], sp.height,
                  sp.mono ? "mono" : "color", (int)sp.ranges.size());
    }
    s.end();
}

CONF_TEST(color_fontsdat_metrics)
{
    LoadedFonts *lf = fonts(ctx);
    if(!lf)
        return;
    for(size_t i = 0; i < lf->fonts.size(); ++i)
    {
        FONT *f = lf->fonts[i];
        conf::Seq s(ctx, conf::nm("object%d", lf->index[i]));
        s.add_int(text_height(f), "text_height");
        s.add_int(get_font_ranges(f), "ranges");
        s.add_int(get_font_range_begin(f, -1), "begin");
        s.add_int(get_font_range_end(f, -1), "end");
        for(int c = 0; c < 300; ++c)
            s.add_int(f->vtable->char_length(f, c), "char_length(%d)", c);
        const colorkit::Range &rg = lf->specs[i].ranges[0];
        for(int c = rg.begin; c < rg.end; c += 16)
        {
            std::string t = range_text(rg, c, 16);
            s.add_int(text_length(f, t.c_str()), "text_length(%d..%d)", c, c + 15);
        }
        s.add_int(text_length(f, "Zelda Classic 2.53 - LEVEL-1"), "text_length(sample)");
        s.end();
    }
}

CONF_TEST(color_fontsdat_render)
{
    LoadedFonts *lf = fonts(ctx);
    if(!lf)
        return;
    PALETTE saved, pal;
    get_palette(saved);
    random_palette(ctx.rng, pal);
    set_palette(pal);
    BITMAP *b = create_bitmap_ex(8, 256, 32);
    for(size_t i = 0; i < lf->fonts.size(); ++i)
    {
        FONT *f = lf->fonts[i];
        const colorkit::Range &rg = lf->specs[i].ranges[0];
        conf::Seq s(ctx, conf::nm("object%d", lf->index[i]));
        int variant = 0;
        for(int c = rg.begin; c < rg.end; c += 12, ++variant)
        {
            std::string t = range_text(rg, c, 12);
            int fg, bg;
            switch(variant % 4)
            {
            case 0: fg = ctx.rng.range(1, 255); bg = -1; break;
            case 1: fg = ctx.rng.range(1, 255); bg = ctx.rng.range(0, 255); break;
            case 2: fg = -1; bg = -1; break;
            default: fg = -1; bg = ctx.rng.range(0, 255); break;
            }
            clear_to_color(b, 3);
            int x = ctx.rng.range(-6, 20), y = ctx.rng.range(-4, 12);
            textout_ex(b, f, t.c_str(), x, y, fg, bg);
            s.add_bitmap(b, "chars %d..%d at %d,%d fg %d bg %d", c, c + 11, x, y, fg, bg);
        }
        /* the way the game draws: centred, right-aligned, printf, clipped */
        clear_to_color(b, 0);
        set_clip_rect(b, 10, 4, 200, 20);
        textout_centre_ex(b, f, "LEVEL-1  LIFE  -B- -A-", 128, 2, 1, -1);
        textprintf_right_ex(b, f, 250, 14, 2, 0, "%d", 12345);
        textprintf_ex(b, f, -3, 22, -1, 5, "%3d rupees %c", 7, 'X');
        set_clip_rect(b, 0, 0, b->w - 1, b->h - 1);
        s.add_bitmap(b, "game-like lines");
        s.end();
    }
    destroy_bitmap(b);

    /* 404 behaviour on a real font: '^' exists in all of them */
    BITMAP *t = create_bitmap_ex(8, 128, 24);
    conf::Seq m(ctx, "missing_chars");
    for(size_t i = 0; i < lf->fonts.size(); ++i)
    {
        clear_to_color(t, 0);
        /* U+0100 and U+20AC are outside every range of fonts.dat */
        textout_ex(t, lf->fonts[i], "a\xC4\x80" "b\xE2\x82\xAC" "c", 2, 2, 9, 4);
        m.add_bitmap(t, "object %d", lf->index[i]);
    }
    m.end();
    destroy_bitmap(t);
    set_palette(saved);
}

CONF_TEST(color_fontsdat_unload)
{
    LoadedFonts *lf = fonts(ctx);
    if(!lf)
        return;
    unload(*lf);
    g_fonts.ok = false;
    g_fonts.why = "unloaded";
    ctx.check_bool("unloaded", true);
}

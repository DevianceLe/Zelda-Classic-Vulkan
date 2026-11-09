/*
 * test_files_datafile.cpp - load_datafile & co. must give the reference's
 * content for every object of the game's datafiles (zelda.dat, sfx.dat,
 * fonts.dat, qst.dat, zquest.dat): types, sizes, properties, bitmap pixels,
 * palettes, RLE sprites, sample fields and data, MIDI tracks, nested
 * datafiles, raw data, and fonts (height, ranges, every glyph, char_length).
 * Also hand-made datafiles with bitmaps/RLE sprites/fonts of every stored
 * depth loaded under several color depths and color conversion modes, the
 * object lookup helpers and the failure cases.
 *
 * Fonts: FONT->data is private to each side. The test reads Allegro's
 * FONT_MONO_DATA / FONT_COLOR_DATA on the reference side and the zcport font
 * model (zcport/font_internal.h) on the port side and prints the same
 * normalized form (range bounds, mono flag, glyph size and pixel values).
 */
#include "conf.h"
#include "conf_zc.h"

#include <allegro/internal/aintern.h>
#ifdef CONF_SIDE_PORT
#include <zcport/font_internal.h>
#endif

#include <errno.h>
#include <string.h>

#include <string>
#include <vector>

namespace
{

const int g_props[] = { DAT_NAME, DAT_ID('O', 'R', 'I', 'G'), DAT_ID('D', 'A', 'T', 'E'), DAT_ID('X', 'P', 'O', 'S'),
                        DAT_ID('Y', 'P', 'O', 'S'), DAT_ID('X', 'S', 'I', 'Z'), DAT_ID('Y', 'S', 'I', 'Z'),
                        DAT_ID('X', 'C', 'R', 'P'), DAT_ID('Y', 'C', 'R', 'P'), DAT_ID('B', 'A', 'C', 'K'),
                        DAT_ID('H', 'N', 'A', 'M'), DAT_ID('P', 'A', 'C', 'K'), DAT_ID('Q', 'Q', 'Q', 'Q') };

void add_glyph_pixels(conf::Hash &h, BITMAP *b)
{
    h.add_i32(b->w);
    h.add_i32(b->h);
    h.add_i32(bitmap_color_depth(b));
    int bpp = conf::bytes_per_pixel(bitmap_color_depth(b));

    for(int y = 0; y < b->h; ++y)
        h.add_bytes(b->line[y], (size_t)(b->w * bpp));
}

/* Normalized font: height, then per range (begin, end, mono) and per glyph
 * a hash of its size and pixels (mono pixels as 0/1 bytes). */
void add_font(conf::Seq &q, FONT *f)
{
    q.add_int(f->height, "height");
#ifdef CONF_SIDE_REF
    if(f->vtable == font_vtable_mono)
    {
        for(FONT_MONO_DATA *m = (FONT_MONO_DATA *)f->data; m; m = m->next)
        {
            q.add_int(m->begin, "range begin");
            q.add_int(m->end, "range end");
            q.add_int(1, "mono");

            for(int c = m->begin; c < m->end; ++c)
            {
                FONT_GLYPH *g = m->glyphs[c - m->begin];
                conf::Hash h;

                if(g)
                {
                    h.add_i32(g->w);
                    h.add_i32(g->h);
                    h.add_i32(8);
                    int stride = (g->w + 7) / 8;

                    for(int y = 0; y < g->h; ++y)
                        for(int x = 0; x < g->w; ++x)
                            h.add_u8((g->dat[y * stride + x / 8] >> (7 - (x & 7))) & 1);
                }
                else
                    h.add_i32(-1);

                q.add_hex(h.value(), "glyph %d", c);
            }
        }
    }
    else if(f->vtable == font_vtable_color)
    {
        for(FONT_COLOR_DATA *m = (FONT_COLOR_DATA *)f->data; m; m = m->next)
        {
            q.add_int(m->begin, "range begin");
            q.add_int(m->end, "range end");
            q.add_int(0, "mono");

            for(int c = m->begin; c < m->end; ++c)
            {
                BITMAP *g = m->bitmaps[c - m->begin];
                conf::Hash h;

                if(g)
                    add_glyph_pixels(h, g);
                else
                    h.add_i32(-1);

                q.add_hex(h.value(), "glyph %d", c);
            }
        }
    }
    else
        q.add_str("unknown font vtable");
#else
    if(zcport_is_zcport_font(f))
    {
        ZCPORT_FONT_DATA *d = (ZCPORT_FONT_DATA *)f->data;

        for(int i = 0; i < d->num_ranges; ++i)
        {
            ZCPORT_FONT_RANGE *r = &d->ranges[i];
            q.add_int(r->begin, "range begin");
            q.add_int(r->end, "range end");
            q.add_int(r->mono ? 1 : 0, "mono");

            for(int c = r->begin; c < r->end; ++c)
            {
                BITMAP *g = r->glyphs[c - r->begin];
                conf::Hash h;

                if(g)
                    add_glyph_pixels(h, g);
                else
                    h.add_i32(-1);

                q.add_hex(h.value(), "glyph %d", c);
            }
        }
    }
    else
        q.add_str("unknown font vtable");
#endif

    for(int c = 0; c < 300; ++c)
        q.add_int(f->vtable->char_length(f, c), "char_length %d", c);
}

void add_object(conf::Ctx &ctx, conf::Seq &q, const DATAFILE *d, const std::string &path);

void check_datafile(conf::Ctx &ctx, const DATAFILE *dat, const std::string &path)
{
    int n = 0;

    for(; dat[n].type != DAT_END; ++n)
    {
        std::string p = path + conf::nm("%d", n);
        conf::Seq q(ctx, p.c_str());
        add_object(ctx, q, &dat[n], p);
    }

    ctx.check_int((path + "count").c_str(), n);
    ctx.check_bool((path + "end_dat_null").c_str(), dat[n].dat == NULL);
}

void add_object(conf::Ctx &ctx, conf::Seq &q, const DATAFILE *d, const std::string &path)
{
    q.add_hex((unsigned)d->type, "type");
    q.add_int(d->size, "size");

    for(size_t i = 0; i < sizeof(g_props) / sizeof(g_props[0]); ++i)
        q.add_str(get_datafile_property(d, g_props[i]), "prop %d", (int)i);

    switch(d->type)
    {
    case DAT_FILE:
        q.end();
        check_datafile(ctx, (const DATAFILE *)d->dat, path + "_");
        return;

    case DAT_BITMAP:
        q.add_bitmap((BITMAP *)d->dat, "bitmap");
        break;

    case DAT_RLE_SPRITE:
    {
        RLE_SPRITE *s = (RLE_SPRITE *)d->dat;
        q.add_int(s->w, "w");
        q.add_int(s->h, "h");
        q.add_int(s->color_depth, "depth");
        q.add_int(s->size, "rle size");
        q.add_mem(s->dat, (size_t)s->size, "rle data");
        break;
    }

    case DAT_PALETTE:
    {
        const RGB *p = (const RGB *)d->dat;
        q.add_hex(conf::hash_palette(p), "palette");
        break;
    }

    case DAT_SAMPLE:
    {
        SAMPLE *s = (SAMPLE *)d->dat;
        q.add_int(s->bits, "bits");
        q.add_int(s->stereo, "stereo");
        q.add_int(s->freq, "freq");
        q.add_int(s->priority, "priority");
        q.add_int((long long)s->len, "len");
        q.add_int((long long)s->loop_start, "loop_start");
        q.add_int((long long)s->loop_end, "loop_end");
        q.add_int((long long)s->param, "param");
        size_t bytes = (size_t)s->len * (s->stereo ? 2 : 1) * (s->bits == 8 ? 1 : 2);
        q.add_mem(s->data, bytes, "sample data");

        if(s->len > 0)
            q.add_int(s->bits == 8 ? ((unsigned char *)s->data)[0] : ((unsigned short *)s->data)[0], "first value");

        break;
    }

    case DAT_MIDI:
    {
        MIDI *m = (MIDI *)d->dat;
        q.add_int(m->divisions, "divisions");

        for(int t = 0; t < MIDI_TRACKS; ++t)
        {
            q.add_int(m->track[t].len, "track %d len", t);
            q.add_int(m->track[t].data != NULL, "track %d data", t);

            if(m->track[t].data)
                q.add_mem(m->track[t].data, (size_t)m->track[t].len, "track %d", t);
        }

        break;
    }

    case DAT_FONT:
        add_font(q, (FONT *)d->dat);
        break;

    default:
        q.add_mem(d->dat, (size_t)d->size, "data");
        break;
    }
}

DATAFILE *load_with(const char *file, const char *pw)
{
    packfile_password(pw);
    DATAFILE *d = load_datafile(file);
    packfile_password(NULL);
    return d;
}

void check_file(conf::Ctx &ctx, const char *name, const char *pw)
{
    if(!ctx.have_data(name))
    {
        ctx.skip("%s not found", name);
        return;
    }

    DATAFILE *d = load_with(ctx.data(name).c_str(), pw);
    ctx.check_bool("loaded", d != NULL);

    if(!d)
        return;

    check_datafile(ctx, d, "obj_");
    unload_datafile(d);
}

} /* namespace */

CONF_TEST(dat_zelda)
{
    check_file(ctx, "zelda.dat", NULL);
}

CONF_TEST(dat_sfx)
{
    check_file(ctx, "sfx.dat", NULL);
}

CONF_TEST(dat_fonts)
{
    check_file(ctx, "fonts.dat", conf::zc::datapwd());
}

CONF_TEST(dat_qst)
{
    check_file(ctx, "qst.dat", conf::zc::datapwd());
}

CONF_TEST(dat_zquest)
{
    check_file(ctx, "zquest.dat", conf::zc::datapwd());
}

/* The player's own sequence: COLORCONV_NONE, 8-bit, then the three loads
 * (zelda.cpp), fonts with the data password. */
CONF_TEST(dat_game_sequence)
{
    if(!ctx.have_data("zelda.dat") || !ctx.have_data("fonts.dat") || !ctx.have_data("sfx.dat"))
    {
        ctx.skip("game data not found");
        return;
    }

    set_color_conversion(COLORCONV_NONE);
    DATAFILE *data = load_with(ctx.data("zelda.dat").c_str(), NULL);
    DATAFILE *fonts = load_with(ctx.data("fonts.dat").c_str(), conf::zc::datapwd());
    DATAFILE *sfx = load_with(ctx.data("sfx.dat").c_str(), NULL);
    ctx.check_bool("zelda", data != NULL);
    ctx.check_bool("fonts", fonts != NULL);
    ctx.check_bool("sfx", sfx != NULL);

    if(data)
        ctx.check_mem("signature", data[0].dat, (size_t)data[0].size);

    if(fonts)
        ctx.check_mem("font_signature", fonts[0].dat, (size_t)fonts[0].size);

    if(sfx)
        ctx.check_mem("sfx_signature", sfx[0].dat, (size_t)sfx[0].size);

    /* lookups the API offers */
    if(data)
    {
        const char *names[] = { "BMP_MOUSE", "bmp_mouse", "TITLE_25/BMP_3", "ZC_MIDI/TITLE", "ZC_MIDI", "NOPE",
                                "TITLE_25/NOPE", "BMP_MOUSE/X", "", "GrabberInfo" };

        for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        {
            DATAFILE *o = find_datafile_object(data, names[i]);
            ctx.checkf(conf::nm("find_%d", (int)i), "%s", o ? get_datafile_property(o, DAT_NAME) : "(null)");

            if(o)
                ctx.check_hex(conf::nm("find_type_%d", (int)i), (unsigned)o->type);
        }

        ctx.check_str("prop_missing", get_datafile_property(&data[1], DAT_ID('Z', 'Z', 'Z', 'Z')));
    }

    if(sfx)
        unload_datafile(sfx);

    if(fonts)
        unload_datafile(fonts);

    if(data)
        unload_datafile(data);
}

CONF_TEST(dat_objects)
{
    if(!ctx.have_data("zelda.dat"))
    {
        ctx.skip("game data not found");
        return;
    }

    struct Case
    {
        const char *file;
        const char *obj;
        bool pwd;
    };
    static const Case cases[] = {
        { "zelda.dat", "BMP_MOUSE", false }, { "zelda.dat", "pal_gui", false }, { "zelda.dat", "RLE_CREDITS", false },
        { "zelda.dat", "TITLE_25/BMP_2", false }, { "zelda.dat", "ZC_MIDI", false }, { "zelda.dat", "WAV_00_AGFIRE", false },
        { "zelda.dat", "NOPE", false }, { "fonts.dat", "FONT_NES", true }, { "fonts.dat", "FONT_GUI_PROP", true },
        { "fonts.dat", "FONT_NES", false }, { "sfx.dat", "WAV_SEA", false },
    };

    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        packfile_password(cases[i].pwd ? conf::zc::datapwd() : NULL);
        DATAFILE *o = load_datafile_object(ctx.data(cases[i].file).c_str(), cases[i].obj);
        packfile_password(NULL);
        std::string p = conf::nm("lo_%d", (int)i);
        ctx.check_bool((p + "_loaded").c_str(), o != NULL);

        if(o)
        {
            conf::Seq q(ctx, p.c_str());
            add_object(ctx, q, o, p);
            q.end();
            unload_datafile_object(o);
        }
    }

    /* load_datafile on "file.dat#nested" */
    DATAFILE *n = load_with((ctx.data("zelda.dat") + "#TITLE_DX").c_str(), NULL);
    ctx.check_bool("nested_loaded", n != NULL);

    if(n)
    {
        check_datafile(ctx, n, "nested_");
        unload_datafile(n);
    }
}

CONF_TEST(dat_failures)
{
    std::string junk = ctx.tmp("dat_junk.dat");
    PACKFILE *f = pack_fopen(junk.c_str(), F_WRITE);
    pack_fputs("this is not a datafile", f);
    pack_fclose(f);
    errno = 0;
    ctx.checkf("missing", "%s", load_datafile(ctx.tmp("dat_no_such.dat").c_str()) ? "loaded" : "NULL");
    ctx.checkf("junk", "%s", load_datafile(junk.c_str()) ? "loaded" : "NULL");

    /* a packed file that is not a datafile */
    f = pack_fopen(junk.c_str(), F_WRITE_PACKED);
    pack_mputl(DAT_ID('N', 'O', 'P', 'E'), f);
    pack_fclose(f);
    ctx.checkf("packed_junk", "%s", load_datafile(junk.c_str()) ? "loaded" : "NULL");

    if(ctx.have_data("fonts.dat"))
        ctx.checkf("no_password", "%s", load_datafile(ctx.data("fonts.dat").c_str()) ? "loaded" : "NULL");

    if(ctx.have_data("1st.qst"))
        ctx.checkf("quest_file", "%s", load_datafile(ctx.data("1st.qst").c_str()) ? "loaded" : "NULL");

    /* an empty datafile */
    f = pack_fopen(junk.c_str(), F_WRITE_PACKED);
    pack_mputl(DAT_MAGIC, f);
    pack_mputl(0, f);
    pack_fclose(f);
    DATAFILE *d = load_datafile(junk.c_str());
    ctx.check_bool("empty_loaded", d != NULL);

    if(d)
    {
        ctx.check_hex("empty_end", (unsigned)d[0].type);
        unload_datafile(d);
    }

    delete_file(junk.c_str());
}

/* ---- hand-made datafiles: every stored depth, conversions ------------------ */

namespace
{

void put_prop(PACKFILE *f, int type, const char *s)
{
    pack_mputl(DAT_PROPERTY, f);
    pack_mputl(type, f);
    pack_mputl((long)strlen(s), f);
    pack_fwrite(s, (long)strlen(s), f);
}

/* Writes one object; compressed chunks are LZSS data made by a "wp" file. */
void put_object(PACKFILE *f, int type, const char *name, const std::vector<unsigned char> &data, bool pack,
                const std::string &tmp)
{
    put_prop(f, DAT_NAME, name);
    put_prop(f, DAT_ID('O', 'R', 'I', 'G'), "made by test_files_datafile");
    pack_mputl(type, f);

    if(!pack)
    {
        pack_mputl((long)data.size(), f);
        pack_mputl((long)data.size(), f);
        pack_fwrite(&data[0], (long)data.size(), f);
        return;
    }

    PACKFILE *z = pack_fopen(tmp.c_str(), F_WRITE_PACKED);
    pack_fwrite(&data[0], (long)data.size(), z);
    pack_fclose(z);
    std::vector<unsigned char> raw;
    z = pack_fopen(tmp.c_str(), F_READ);
    int c;

    while((c = pack_getc(z)) != EOF)
        raw.push_back((unsigned char)c);

    pack_fclose(z);
    raw.erase(raw.begin(), raw.begin() + 4);
    pack_mputl((long)raw.size(), f);
    pack_mputl(-(long)data.size(), f);
    pack_fwrite(&raw[0], (long)raw.size(), f);
}

struct Bytes
{
    std::vector<unsigned char> v;
    void w(int x) { v.push_back((unsigned char)(x >> 8)); v.push_back((unsigned char)x); }
    void l(long x) { w((int)(x >> 16)); w((int)x); }
    void b(int x) { v.push_back((unsigned char)x); }
    void iw(int x) { v.push_back((unsigned char)x); v.push_back((unsigned char)(x >> 8)); }
    void il(long x) { iw((int)x); iw((int)(x >> 16)); }
};

Bytes make_bitmap(conf::Rng &rng, int bits, int w, int h)
{
    Bytes o;
    o.w(bits);
    o.w(w);
    o.w(h);

    for(int i = 0; i < w * h; ++i)
    {
        switch(bits)
        {
        case 8: o.b(rng.range(0, 9) == 0 ? 0 : rng.bits(8)); break;
        case 15:
        case 16: o.iw(rng.range(0, 9) == 0 ? 0xF81F : rng.bits(16)); break;
        case 24:
        case 32:
            if(rng.range(0, 9) == 0) { o.b(255); o.b(0); o.b(255); }
            else { o.b(rng.bits(8)); o.b(rng.bits(8)); o.b(rng.bits(8)); }
            break;
        default: o.b(rng.bits(8)); o.b(rng.bits(8)); o.b(rng.bits(8)); o.b(rng.bits(8)); break;
        }
    }

    return o;
}

/* An RLE stream: per row runs of solid pixels (positive count) and skips
 * (negative), ended by the mask value. */
Bytes make_rle(conf::Rng &rng, int bits, int w, int h)
{
    Bytes body;
    int n = 0;

    for(int y = 0; y < h; ++y)
    {
        int x = 0;

        while(x < w)
        {
            int len = rng.range(1, w - x < 6 ? w - x : 6);
            bool solid = rng.range(0, 2) != 0;

            if(bits == 8)
            {
                body.b(solid ? len : (-len & 0xFF));
                ++n;

                if(solid)
                    for(int k = 0; k < len; ++k, ++n)
                        body.b(rng.range(1, 255));
            }
            else if(bits == 16)
            {
                body.iw(solid ? len : -len);
                n += 2;

                if(solid)
                    for(int k = 0; k < len; ++k, n += 2)
                        body.iw(rng.bits(16) & 0xF7DE);
            }
            else
            {
                body.il(solid ? len : -len);
                n += 4;

                if(solid)
                    for(int k = 0; k < len; ++k, n += 4)
                    {
                        body.b(rng.bits(8));
                        body.b(rng.bits(8));
                        body.b(rng.bits(7));
                    }
            }

            x += len;
        }

        if(bits == 8) { body.b(0); n += 1; }
        else if(bits == 16) { body.iw(0xF81F); n += 2; }
        else { body.il(0xFF00FF); n += 4; }
    }

    Bytes o;
    o.w(bits);
    o.w(w);
    o.w(h);
    o.l(n);
    o.v.insert(o.v.end(), body.v.begin(), body.v.end());
    return o;
}

Bytes make_font(conf::Rng &rng, bool mono)
{
    Bytes o;
    o.w(0);
    o.w(2);

    for(int r = 0; r < 2; ++r)
    {
        int begin = r == 0 ? 32 : 0x100 + rng.range(0, 50);
        int end = begin + rng.range(3, 12);
        o.b(mono ? 1 : 0);
        o.l(begin);
        o.l(end - 1);

        for(int c = begin; c < end; ++c)
        {
            int w = rng.range(1, 13), h = rng.range(1, 11);
            o.w(w);
            o.w(h);
            int n = mono ? ((w + 7) / 8) * h : w * h;

            for(int k = 0; k < n; ++k)
                o.b(mono ? rng.bits(8) : (rng.range(0, 3) == 0 ? 0 : rng.bits(8)));
        }
    }

    return o;
}

Bytes make_sample(conf::Rng &rng, int bits, bool stereo, int len)
{
    Bytes o;
    o.w(stereo ? -bits : bits);
    o.w(rng.range(8000, 44100));
    o.l(len);

    for(int i = 0; i < len * (stereo ? 2 : 1); ++i)
    {
        if(bits == 8)
            o.b(rng.bits(8));
        else
            o.iw(rng.bits(16));
    }

    return o;
}

Bytes make_midi(conf::Rng &rng)
{
    Bytes o;
    o.w(rng.range(48, 480));

    for(int t = 0; t < 32; ++t)
    {
        int len = t < 5 ? rng.range(0, 40) : 0;
        o.l(len);

        for(int k = 0; k < len; ++k)
            o.b(rng.bits(8));
    }

    return o;
}

} /* namespace */

CONF_TEST(dat_handmade)
{
    std::string fn = ctx.tmp("dat_hand.dat");
    std::string tmp = ctx.tmp("dat_hand_z.bin");

    for(int outer = 0; outer < 3; ++outer)
    {
        const char *pw = outer == 2 ? "s3cr3t" : NULL;
        packfile_password(pw);
        PACKFILE *f = pack_fopen(fn.c_str(), outer == 0 ? F_WRITE_NOPACK : F_WRITE_PACKED);
        packfile_password(NULL);
        pack_mputl(DAT_MAGIC, f);
        pack_mputl(16, f);
        /* (-32, RGBA, is left out: the reference does not read its pixels) */
        static const int depths[] = { 8, 15, 16, 24, 32 };

        for(int k = 0; k < 5; ++k)
            put_object(f, DAT_BITMAP, conf::nm("BMP_%d", k), make_bitmap(ctx.rng, depths[k], 5 + k, 4 + k).v, (k + outer) & 1, tmp);

        put_object(f, DAT_RLE_SPRITE, "RLE_8", make_rle(ctx.rng, 8, 13, 6).v, outer == 1, tmp);
        put_object(f, DAT_RLE_SPRITE, "RLE_16", make_rle(ctx.rng, 16, 9, 5).v, false, tmp);
        put_object(f, DAT_RLE_SPRITE, "RLE_32", make_rle(ctx.rng, 32, 7, 4).v, true, tmp);
        put_object(f, DAT_FONT, "FONT_MONO", make_font(ctx.rng, true).v, true, tmp);
        put_object(f, DAT_FONT, "FONT_COLOR", make_font(ctx.rng, false).v, false, tmp);
        put_object(f, DAT_SAMPLE, "SAMP_8", make_sample(ctx.rng, 8, false, 100).v, false, tmp);
        put_object(f, DAT_SAMPLE, "SAMP_16S", make_sample(ctx.rng, 16, true, 70).v, true, tmp);
        put_object(f, DAT_MIDI, "MIDI", make_midi(ctx.rng).v, true, tmp);
        Bytes pal;

        for(int i = 0; i < 1024; ++i)
            pal.b(i & 3 ? ctx.rng.range(0, 63) : ctx.rng.bits(8));

        put_object(f, DAT_PALETTE, "PAL", pal.v, false, tmp);
        Bytes raw;

        for(int i = 0; i < 77; ++i)
            raw.b(ctx.rng.bits(8));

        put_object(f, DAT_ID('C', 'U', 'S', 'T'), "CUSTOM", raw.v, true, tmp);
        put_object(f, DAT_DATA, "EMPTYISH", std::vector<unsigned char>(1, 7), false, tmp);
        pack_fclose(f);

        /* Conversions from and to 8 bits use the palette (color module,
         * tested there); here: everything between the other depths. */
        const int hi = COLORCONV_TOTAL & ~(COLORCONV_EXPAND_256 | COLORCONV_REDUCE_TO_256);
        static const int modes[][2] = { { 8, COLORCONV_NONE }, { 16, COLORCONV_NONE }, { 32, COLORCONV_NONE },
                                        { 16, hi }, { 32, hi }, { 15, hi }, { 24, hi | COLORCONV_KEEP_TRANS },
                                        { 32, COLORCONV_KEEP_ALPHA & hi }, { 16, COLORCONV_32_TO_16 | COLORCONV_24_TO_16 },
                                        { 15, COLORCONV_PARTIAL }, { 24, COLORCONV_MOST | COLORCONV_KEEP_TRANS } };
        (void)hi;

        for(size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m)
        {
            set_color_depth(modes[m][0]);
            set_color_conversion(modes[m][1]);
            packfile_password(pw);
            DATAFILE *d = load_datafile(fn.c_str());
            packfile_password(NULL);
            std::string p = conf::nm("h%d_m%d_", outer, (int)m);
            ctx.check_bool((p + "loaded").c_str(), d != NULL);

            if(d)
            {
                check_datafile(ctx, d, p);
                unload_datafile(d);
            }

            set_color_depth(8);
            set_color_conversion(COLORCONV_NONE);
        }
    }

    delete_file(fn.c_str());
    delete_file(tmp.c_str());
}

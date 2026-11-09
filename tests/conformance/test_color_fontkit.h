/*
 * test_color_fontkit.h - builds identical FONTs on both sides of the
 * conformance tests from plain glyph data, and reads glyph data back.
 *
 *   reference: Allegro's own structures (FONT_MONO_DATA / FONT_COLOR_DATA,
 *              font_vtable_mono / font_vtable_color from aintern.h), so the
 *              reference's own text code draws them;
 *   port:      zcport/font_internal.h (zcport_font_create and friends).
 *
 * Only for the color module's tests (test_color_*.cpp). C++98.
 */
#ifndef TEST_COLOR_FONTKIT_H
#define TEST_COLOR_FONTKIT_H

#include "conf.h"
#include <allegro/internal/aintern.h>
#ifdef CONF_SIDE_PORT
#include "zcport/font_internal.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <vector>

namespace colorkit
{

struct Glyph
{
    int w, h;
    std::vector<unsigned char> px;     /* w*h: mono 0/1, color 8-bit values (0 = mask) */
    Glyph() : w(0), h(0) {}
    unsigned char at(int x, int y) const { return px[(size_t)(y * w + x)]; }
};

struct Range
{
    int begin, end;                    /* end exclusive */
    std::vector<Glyph> glyphs;         /* end - begin */
};

struct Spec
{
    int height;
    bool mono;
    std::vector<Range> ranges;
};

inline conf::u64 hash_spec(const Spec &s)
{
    conf::Hash h;
    h.add_i32(s.height);
    h.add_i32(s.mono ? 1 : 0);
    h.add_i32((int)s.ranges.size());
    for(size_t r = 0; r < s.ranges.size(); ++r)
    {
        const Range &rg = s.ranges[r];
        h.add_i32(rg.begin);
        h.add_i32(rg.end);
        for(size_t g = 0; g < rg.glyphs.size(); ++g)
        {
            const Glyph &gl = rg.glyphs[g];
            h.add_i32(gl.w);
            h.add_i32(gl.h);
            if(!gl.px.empty())
                h.add_bytes(&gl.px[0], gl.px.size());
        }
    }
    return h.value();
}

#ifdef CONF_SIDE_REF

inline FONT *build(const Spec &s)
{
    FONT *f = (FONT *)calloc(1, sizeof(FONT));
    f->height = s.height;
    if(s.mono)
    {
        FONT_MONO_DATA *head = NULL, *tail = NULL;
        for(size_t r = 0; r < s.ranges.size(); ++r)
        {
            const Range &rg = s.ranges[r];
            FONT_MONO_DATA *md = (FONT_MONO_DATA *)calloc(1, sizeof(FONT_MONO_DATA));
            md->begin = rg.begin;
            md->end = rg.end;
            md->glyphs = (FONT_GLYPH **)calloc((size_t)(rg.end - rg.begin), sizeof(FONT_GLYPH *));
            for(int c = 0; c < rg.end - rg.begin; ++c)
            {
                const Glyph &gl = rg.glyphs[(size_t)c];
                int stride = (gl.w + 7) / 8;
                FONT_GLYPH *g = (FONT_GLYPH *)calloc(1, sizeof(FONT_GLYPH) + (size_t)(stride * gl.h) + 1);
                g->w = (short)gl.w;
                g->h = (short)gl.h;
                for(int y = 0; y < gl.h; ++y)
                    for(int x = 0; x < gl.w; ++x)
                        if(gl.at(x, y))
                            g->dat[y * stride + x / 8] |= (unsigned char)(0x80 >> (x & 7));
                md->glyphs[c] = g;
            }
            if(tail) tail->next = md; else head = md;
            tail = md;
        }
        f->data = head;
        f->vtable = font_vtable_mono;
    }
    else
    {
        FONT_COLOR_DATA *head = NULL, *tail = NULL;
        for(size_t r = 0; r < s.ranges.size(); ++r)
        {
            const Range &rg = s.ranges[r];
            FONT_COLOR_DATA *cd = (FONT_COLOR_DATA *)calloc(1, sizeof(FONT_COLOR_DATA));
            cd->begin = rg.begin;
            cd->end = rg.end;
            cd->bitmaps = (BITMAP **)calloc((size_t)(rg.end - rg.begin), sizeof(BITMAP *));
            for(int c = 0; c < rg.end - rg.begin; ++c)
            {
                const Glyph &gl = rg.glyphs[(size_t)c];
                BITMAP *b = create_bitmap_ex(8, gl.w, gl.h);
                for(int y = 0; y < gl.h; ++y)
                    for(int x = 0; x < gl.w; ++x)
                        b->line[y][x] = gl.at(x, y);
                cd->bitmaps[c] = b;
            }
            if(tail) tail->next = cd; else head = cd;
            tail = cd;
        }
        f->data = head;
        f->vtable = font_vtable_color;
    }
    return f;
}

/* Fonts made by build() (our allocations: never destroy_font them). */
inline void release(FONT *f)
{
    if(!f)
        return;
    if(f->vtable == font_vtable_mono)
    {
        FONT_MONO_DATA *md = (FONT_MONO_DATA *)f->data;
        while(md)
        {
            FONT_MONO_DATA *next = md->next;
            for(int c = 0; c < md->end - md->begin; ++c)
                free(md->glyphs[c]);
            free(md->glyphs);
            free(md);
            md = next;
        }
    }
    else
    {
        FONT_COLOR_DATA *cd = (FONT_COLOR_DATA *)f->data;
        while(cd)
        {
            FONT_COLOR_DATA *next = cd->next;
            for(int c = 0; c < cd->end - cd->begin; ++c)
                destroy_bitmap(cd->bitmaps[c]);
            free(cd->bitmaps);
            free(cd);
            cd = next;
        }
    }
    free(f);
}

/* Reads any reference FONT (also ones load_datafile made). */
inline bool extract(FONT *f, Spec &out)
{
    out.ranges.clear();
    out.height = f->height;
    if(f->vtable == font_vtable_mono)
    {
        out.mono = true;
        for(FONT_MONO_DATA *md = (FONT_MONO_DATA *)f->data; md; md = md->next)
        {
            Range rg;
            rg.begin = md->begin;
            rg.end = md->end;
            for(int c = 0; c < md->end - md->begin; ++c)
            {
                FONT_GLYPH *g = md->glyphs[c];
                Glyph gl;
                gl.w = g->w;
                gl.h = g->h;
                int stride = (g->w + 7) / 8;
                for(int y = 0; y < g->h; ++y)
                    for(int x = 0; x < g->w; ++x)
                        gl.px.push_back((g->dat[y * stride + x / 8] >> (7 - (x & 7))) & 1);
                rg.glyphs.push_back(gl);
            }
            out.ranges.push_back(rg);
        }
        return true;
    }
    if(f->vtable == font_vtable_color)
    {
        out.mono = false;
        for(FONT_COLOR_DATA *cd = (FONT_COLOR_DATA *)f->data; cd; cd = cd->next)
        {
            Range rg;
            rg.begin = cd->begin;
            rg.end = cd->end;
            for(int c = 0; c < cd->end - cd->begin; ++c)
            {
                BITMAP *b = cd->bitmaps[c];
                Glyph gl;
                gl.w = b->w;
                gl.h = b->h;
                for(int y = 0; y < b->h; ++y)
                    for(int x = 0; x < b->w; ++x)
                        gl.px.push_back(b->line[y][x]);
                rg.glyphs.push_back(gl);
            }
            out.ranges.push_back(rg);
        }
        return true;
    }
    return false;
}

#else /* CONF_SIDE_PORT */

inline FONT *build(const Spec &s)
{
    FONT *f = zcport_font_create(s.height);
    for(size_t r = 0; r < s.ranges.size(); ++r)
    {
        const Range &rg = s.ranges[r];
        ZCPORT_FONT_RANGE *range = zcport_font_add_range(f, rg.begin, rg.end, s.mono ? 1 : 0);
        for(int c = 0; c < rg.end - rg.begin; ++c)
        {
            const Glyph &gl = rg.glyphs[(size_t)c];
            BITMAP *b = create_bitmap_ex(8, gl.w, gl.h);
            for(int y = 0; y < gl.h; ++y)
                for(int x = 0; x < gl.w; ++x)
                    b->line[y][x] = gl.at(x, y);
            zcport_font_set_glyph(range, rg.begin + c, b);
        }
    }
    return f;
}

inline void release(FONT *f)
{
    destroy_font(f);
}

inline bool extract(FONT *f, Spec &out)
{
    if(!zcport_is_zcport_font(f))
        return false;
    const ZCPORT_FONT_DATA *d = (const ZCPORT_FONT_DATA *)f->data;
    out.ranges.clear();
    out.height = f->height;
    out.mono = d->num_ranges > 0 && d->ranges[0].mono;
    for(int r = 0; r < d->num_ranges; ++r)
    {
        const ZCPORT_FONT_RANGE &src = d->ranges[r];
        Range rg;
        rg.begin = src.begin;
        rg.end = src.end;
        for(int c = 0; c < src.end - src.begin; ++c)
        {
            BITMAP *b = src.glyphs[c];
            Glyph gl;
            if(b)
            {
                gl.w = b->w;
                gl.h = b->h;
                for(int y = 0; y < b->h; ++y)
                    for(int x = 0; x < b->w; ++x)
                        gl.px.push_back(b->line[y][x]);
            }
            rg.glyphs.push_back(gl);
        }
        out.ranges.push_back(rg);
    }
    return true;
}

#endif

} /* namespace colorkit */

#endif /* TEST_COLOR_FONTKIT_H */

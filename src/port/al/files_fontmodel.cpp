// zcport files module: a stand-in for the FONT data model of
// zcport/font_internal.h.
//
// The data model belongs to the foundation module. Until a module defines
// zcport_font_create() & co., the datafile loader still has to build fonts,
// so this file provides the same functions under other names and tells the
// linker to use them only when the real ones are missing (MSVC
// /alternatename; weak symbols elsewhere). Once the real data model is
// linked in, everything here is unused.
//
// Written from scratch for zcport.

#include "files_internal.h"

#include <zcport/font_internal.h>

#include <stdlib.h>
#include <string.h>

namespace
{

int fb_font_height(const FONT *f)
{
    return f->height;
}

int fb_char_length(const FONT *f, int ch);

int fb_text_length(const FONT *f, const char *text)
{
    int w = 0;

    for(const unsigned char *p = (const unsigned char *)text; *p; ++p)
        w += fb_char_length(f, *p);

    return w;
}

int fb_render_char(const FONT *f, int ch, int fg, int bg, BITMAP *bmp, int x, int y)
{
    (void)fg;
    (void)bg;
    (void)bmp;
    (void)x;
    (void)y;
    return fb_char_length(f, ch);
}

void fb_render(const FONT *f, const char *text, int fg, int bg, BITMAP *bmp, int x, int y)
{
    (void)f;
    (void)text;
    (void)fg;
    (void)bg;
    (void)bmp;
    (void)x;
    (void)y;
}

void fb_destroy(FONT *f);

int fb_get_font_ranges(FONT *f)
{
    return ((ZCPORT_FONT_DATA *)f->data)->num_ranges;
}

int fb_get_font_range_begin(FONT *f, int range)
{
    ZCPORT_FONT_DATA *d = (ZCPORT_FONT_DATA *)f->data;

    if(d->num_ranges <= 0)
        return -1;

    if(range < 0)
        range = 0;

    if(range >= d->num_ranges)
        return -1;

    return d->ranges[range].begin;
}

int fb_get_font_range_end(FONT *f, int range)
{
    ZCPORT_FONT_DATA *d = (ZCPORT_FONT_DATA *)f->data;

    if(d->num_ranges <= 0)
        return -1;

    if(range < 0)
        range = d->num_ranges - 1;

    if(range >= d->num_ranges)
        return -1;

    return d->ranges[range].end - 1;
}

FONT *fb_extract(FONT *, int, int)
{
    return NULL;
}

FONT *fb_merge(FONT *, FONT *)
{
    return NULL;
}

int fb_transpose(FONT *, int)
{
    return -1;
}

FONT_VTABLE g_fb_vtable = { fb_font_height, fb_char_length, fb_text_length, fb_render_char, fb_render, fb_destroy,
                            fb_get_font_ranges, fb_get_font_range_begin, fb_get_font_range_end, fb_extract,
                            fb_merge, fb_transpose };

} /* namespace */

extern "C" {

struct FONT *zcfiles_fb_font_create(int height)
{
    FONT *f = (FONT *)calloc(1, sizeof(FONT));
    ZCPORT_FONT_DATA *d = (ZCPORT_FONT_DATA *)calloc(1, sizeof(ZCPORT_FONT_DATA));

    if(!f || !d)
    {
        free(f);
        free(d);
        return NULL;
    }

    f->data = d;
    f->height = height;
    f->vtable = &g_fb_vtable;
    return f;
}

ZCPORT_FONT_RANGE *zcfiles_fb_font_add_range(struct FONT *f, int begin, int end, int mono)
{
    if(!f || end <= begin)
        return NULL;

    ZCPORT_FONT_DATA *d = (ZCPORT_FONT_DATA *)f->data;
    ZCPORT_FONT_RANGE *r = (ZCPORT_FONT_RANGE *)realloc(d->ranges, sizeof(ZCPORT_FONT_RANGE) * (d->num_ranges + 1));

    if(!r)
        return NULL;

    d->ranges = r;
    ZCPORT_FONT_RANGE *nr = &d->ranges[d->num_ranges++];
    nr->begin = begin;
    nr->end = end;
    nr->mono = mono;
    nr->glyphs = (BITMAP **)calloc((size_t)(end - begin), sizeof(BITMAP *));
    return nr;
}

int zcfiles_fb_font_set_glyph(ZCPORT_FONT_RANGE *range, int ch, struct BITMAP *glyph)
{
    if(!range || ch < range->begin || ch >= range->end)
        return -1;

    if(range->glyphs[ch - range->begin])
        destroy_bitmap(range->glyphs[ch - range->begin]);

    range->glyphs[ch - range->begin] = glyph;
    return 0;
}

struct BITMAP *zcfiles_fb_font_find_glyph(const struct FONT *f, int ch, const ZCPORT_FONT_RANGE **range_out)
{
    const ZCPORT_FONT_DATA *d = (const ZCPORT_FONT_DATA *)f->data;

    for(int i = 0; i < d->num_ranges; ++i)
    {
        const ZCPORT_FONT_RANGE *r = &d->ranges[i];

        if(ch >= r->begin && ch < r->end && r->glyphs[ch - r->begin])
        {
            if(range_out)
                *range_out = r;

            return r->glyphs[ch - r->begin];
        }
    }

    return NULL;
}

struct BITMAP *zcfiles_fb_font_get_glyph(const struct FONT *f, int ch, const ZCPORT_FONT_RANGE **range_out)
{
    BITMAP *g = zcfiles_fb_font_find_glyph(f, ch, range_out);

    if(!g)
        g = zcfiles_fb_font_find_glyph(f, '^', range_out);   /* Allegro's default 404 character */

    return g;
}

int zcfiles_fb_is_zcport_font(const struct FONT *f)
{
    return f && f->vtable == &g_fb_vtable;
}

void zcfiles_fb_font_destroy(struct FONT *f)
{
    if(!f)
        return;

    ZCPORT_FONT_DATA *d = (ZCPORT_FONT_DATA *)f->data;

    for(int i = 0; i < d->num_ranges; ++i)
    {
        ZCPORT_FONT_RANGE *r = &d->ranges[i];

        for(int k = 0; k < r->end - r->begin; ++k)
            if(r->glyphs[k])
                destroy_bitmap(r->glyphs[k]);

        free(r->glyphs);
    }

    free(d->ranges);
    free(d);
    free(f);
}

void zcfiles_fb_font_set_hooks(const ZCPORT_FONT_HOOKS *hooks)
{
    (void)hooks;
}

} /* extern "C" */

namespace
{
int fb_char_length(const FONT *f, int ch)
{
    BITMAP *g = zcfiles_fb_font_get_glyph(f, ch, NULL);
    return g ? g->w : 0;
}

void fb_destroy(FONT *f)
{
    zcfiles_fb_font_destroy(f);
}
} /* namespace */

/* Pixel layout of 15/16/24/32-bit memory bitmaps: the color module
 * defines the _rgb_*_shift_* variables; until then these values (Allegro's
 * defaults without a graphics driver) stand in for them. */
extern "C" {
int zcfiles_fb_rgb_r_shift_15 = 0;
int zcfiles_fb_rgb_g_shift_15 = 5;
int zcfiles_fb_rgb_b_shift_15 = 10;
int zcfiles_fb_rgb_r_shift_16 = 0;
int zcfiles_fb_rgb_g_shift_16 = 5;
int zcfiles_fb_rgb_b_shift_16 = 11;
int zcfiles_fb_rgb_r_shift_24 = 0;
int zcfiles_fb_rgb_g_shift_24 = 8;
int zcfiles_fb_rgb_b_shift_24 = 16;
int zcfiles_fb_rgb_r_shift_32 = 0;
int zcfiles_fb_rgb_g_shift_32 = 8;
int zcfiles_fb_rgb_b_shift_32 = 16;
int zcfiles_fb_rgb_a_shift_32 = 24;
}

/* The linker directives that select these functions are in
 * files_datafile.cpp (the object that uses the font model), so the linker
 * sees them whenever fonts are loaded. */

#if !defined(_MSC_VER)
extern "C" {
__attribute__((weak)) struct FONT *zcport_font_create(int h) { return zcfiles_fb_font_create(h); }
__attribute__((weak)) ZCPORT_FONT_RANGE *zcport_font_add_range(struct FONT *f, int b, int e, int m) { return zcfiles_fb_font_add_range(f, b, e, m); }
__attribute__((weak)) int zcport_font_set_glyph(ZCPORT_FONT_RANGE *r, int ch, struct BITMAP *g) { return zcfiles_fb_font_set_glyph(r, ch, g); }
__attribute__((weak)) struct BITMAP *zcport_font_find_glyph(const struct FONT *f, int ch, const ZCPORT_FONT_RANGE **r) { return zcfiles_fb_font_find_glyph(f, ch, r); }
__attribute__((weak)) struct BITMAP *zcport_font_get_glyph(const struct FONT *f, int ch, const ZCPORT_FONT_RANGE **r) { return zcfiles_fb_font_get_glyph(f, ch, r); }
__attribute__((weak)) int zcport_is_zcport_font(const struct FONT *f) { return zcfiles_fb_is_zcport_font(f); }
__attribute__((weak)) void zcport_font_destroy(struct FONT *f) { zcfiles_fb_font_destroy(f); }
__attribute__((weak)) void zcport_font_set_hooks(const ZCPORT_FONT_HOOKS *h) { zcfiles_fb_font_set_hooks(h); }
}
#endif

/*
 * zcport color module: the FONT data model (zcport/font_internal.h), the
 * vtable every zcport font uses, the font utilities of allegro/font.h,
 * allegro_404_char and the built-in default font.
 *
 * Measuring and drawing text needs string iteration and the drawing code;
 * those vtable members (text_length, render_char, render) go through the
 * hooks color_text.cpp installs (zcport_font_set_hooks), as the contract
 * describes.
 *
 * Range queries follow the reference: get_font_range_end() returns the last
 * code point of a range (inclusive); range -1 means the whole font; an
 * out-of-range index gives -1.
 *
 * Written from scratch for zcport.
 */

#include "color_internal.h"
#include "zcport/font_internal.h"

#include <stdlib.h>
#include <string.h>

namespace zccolor
{
void build_default_font(FONT *f);   /* color_font_default.cpp */
}

namespace
{

const ZCPORT_FONT_HOOKS *g_hooks = NULL;

ZCPORT_FONT_DATA *data_of(const FONT *f)
{
    return (ZCPORT_FONT_DATA *)f->data;
}

/* ---- vtable members -------------------------------------------------------- */

int vt_font_height(AL_CONST FONT *f)
{
    return f->height;
}

int vt_char_length(AL_CONST FONT *f, int ch)
{
    BITMAP *g = zcport_font_get_glyph(f, ch, NULL);
    return g ? g->w : 0;
}

int vt_text_length(AL_CONST FONT *f, AL_CONST char *text)
{
    if(g_hooks && g_hooks->text_length)
        return g_hooks->text_length(f, text);
    /* no text module: one byte per character */
    int len = 0;
    for(const unsigned char *p = (const unsigned char *)text; *p; ++p)
        len += vt_char_length(f, *p);
    return len;
}

int vt_render_char(AL_CONST FONT *f, int ch, int fg, int bg, BITMAP *bmp, int x, int y)
{
    if(g_hooks && g_hooks->render_char)
        return g_hooks->render_char(f, ch, fg, bg, bmp, x, y);
    return 0;
}

void vt_render(AL_CONST FONT *f, AL_CONST char *text, int fg, int bg, BITMAP *bmp, int x, int y)
{
    if(g_hooks && g_hooks->render)
        g_hooks->render(f, text, fg, bg, bmp, x, y);
}

void vt_destroy(FONT *f)
{
    zcport_font_destroy(f);
}

int vt_get_font_ranges(FONT *f)
{
    if(!f)
        return -1;
    return data_of(f)->num_ranges;
}

/* Like the reference, which walks its list of ranges: an index past the
 * last range names the last range; -1 means the whole font. */
int vt_get_font_range_begin(FONT *f, int range)
{
    if(!f)
        return -1;
    ZCPORT_FONT_DATA *d = data_of(f);
    if(d->num_ranges == 0)
        return -1;
    if(range < 0)
        range = 0;
    if(range >= d->num_ranges)
        range = d->num_ranges - 1;
    return d->ranges[range].begin;
}

int vt_get_font_range_end(FONT *f, int range)
{
    if(!f)
        return -1;
    ZCPORT_FONT_DATA *d = data_of(f);
    if(d->num_ranges == 0)
        return -1;
    if(range < 0 || range >= d->num_ranges)
        range = d->num_ranges - 1;
    return d->ranges[range].end - 1;
}

BITMAP *copy_glyph(BITMAP *g)
{
    if(!g)
        return NULL;
    BITMAP *c = create_bitmap_ex(bitmap_color_depth(g), g->w, g->h);
    if(!c)
        return NULL;
    int bytes = g->w * ((bitmap_color_depth(g) + 7) / 8);
    for(int y = 0; y < g->h; ++y)
        memcpy(c->line[y], g->line[y], (size_t)bytes);
    return c;
}

/* begin..end inclusive; -1 means "from the first" / "to the last". */
FONT *vt_extract_font_range(FONT *f, int begin, int end)
{
    if(!f)
        return NULL;
    ZCPORT_FONT_DATA *d = data_of(f);
    if(begin == -1)
        begin = vt_get_font_range_begin(f, -1);
    if(end == -1)
        end = vt_get_font_range_end(f, -1);
    if(begin < 0 || end < begin)
        return NULL;

    FONT *out = zcport_font_create(f->height);
    if(!out)
        return NULL;
    for(int r = 0; r < d->num_ranges; ++r)
    {
        const ZCPORT_FONT_RANGE &src = d->ranges[r];
        int lo = src.begin > begin ? src.begin : begin;
        int hi = src.end - 1 < end ? src.end - 1 : end;
        if(lo > hi)
            continue;
        ZCPORT_FONT_RANGE *dst = zcport_font_add_range(out, lo, hi + 1, src.mono);
        if(!dst)
            break;
        for(int ch = lo; ch <= hi; ++ch)
            zcport_font_set_glyph(dst, ch, copy_glyph(src.glyphs[ch - src.begin]));
    }
    return out;
}

FONT *vt_merge_fonts(FONT *f1, FONT *f2)
{
    if(!f1 || !f2)
        return NULL;
    FONT *out = zcport_font_create(f1->height > f2->height ? f1->height : f2->height);
    if(!out)
        return NULL;
    const FONT *srcs[2] = { f1, f2 };
    for(int s = 0; s < 2; ++s)
    {
        ZCPORT_FONT_DATA *d = data_of(srcs[s]);
        for(int r = 0; r < d->num_ranges; ++r)
        {
            const ZCPORT_FONT_RANGE &src = d->ranges[r];
            ZCPORT_FONT_RANGE *dst = zcport_font_add_range(out, src.begin, src.end, src.mono);
            if(!dst)
                continue;
            for(int ch = src.begin; ch < src.end; ++ch)
                zcport_font_set_glyph(dst, ch, copy_glyph(src.glyphs[ch - src.begin]));
        }
    }
    return out;
}

int vt_transpose_font(FONT *f, int drange)
{
    if(!f)
        return -1;
    ZCPORT_FONT_DATA *d = data_of(f);
    if(d->num_ranges > 0 && d->ranges[0].begin + drange < 0)
        return -1;
    for(int r = 0; r < d->num_ranges; ++r)
    {
        d->ranges[r].begin += drange;
        d->ranges[r].end += drange;
    }
    return 0;
}

/* The default font: a static FONT so that 'font' is valid from the start;
 * the glyphs are added by a dynamic initializer below. */
ZCPORT_FONT_DATA g_default_data = { 0, NULL };

} // namespace

extern "C" {

FONT_VTABLE zcport_font_vtable =
{
    vt_font_height,
    vt_char_length,
    vt_text_length,
    vt_render_char,
    vt_render,
    vt_destroy,
    vt_get_font_ranges,
    vt_get_font_range_begin,
    vt_get_font_range_end,
    vt_extract_font_range,
    vt_merge_fonts,
    vt_transpose_font
};

int allegro_404_char = '^';

} // extern "C"

namespace
{

FONT g_default_font = { &g_default_data, 8, &zcport_font_vtable };

struct DefaultFontInit
{
    DefaultFontInit() { zccolor::build_default_font(&g_default_font); }
};
DefaultFontInit g_default_font_init;

} // namespace

extern "C" {

FONT *font = &g_default_font;

/* ---- font_internal.h ------------------------------------------------------- */

FONT *zcport_font_create(int height)
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
    f->vtable = &zcport_font_vtable;
    return f;
}

ZCPORT_FONT_RANGE *zcport_font_add_range(FONT *f, int begin, int end, int mono)
{
    if(!f || end <= begin)
        return NULL;
    ZCPORT_FONT_DATA *d = data_of(f);
    BITMAP **glyphs = (BITMAP **)calloc((size_t)(end - begin), sizeof(BITMAP *));
    if(!glyphs)
        return NULL;
    ZCPORT_FONT_RANGE *ranges = (ZCPORT_FONT_RANGE *)realloc(d->ranges, sizeof(ZCPORT_FONT_RANGE) * (size_t)(d->num_ranges + 1));
    if(!ranges)
    {
        free(glyphs);
        return NULL;
    }
    d->ranges = ranges;
    ZCPORT_FONT_RANGE *r = &d->ranges[d->num_ranges++];
    r->begin = begin;
    r->end = end;
    r->mono = mono ? 1 : 0;
    r->glyphs = glyphs;
    return r;
}

int zcport_font_set_glyph(ZCPORT_FONT_RANGE *range, int ch, BITMAP *glyph)
{
    if(!range || ch < range->begin || ch >= range->end)
        return -1;
    BITMAP **slot = &range->glyphs[ch - range->begin];
    if(*slot && *slot != glyph)
        destroy_bitmap(*slot);
    *slot = glyph;
    return 0;
}

BITMAP *zcport_font_find_glyph(const FONT *f, int ch, const ZCPORT_FONT_RANGE **range_out)
{
    if(f && f->data)
    {
        const ZCPORT_FONT_DATA *d = (const ZCPORT_FONT_DATA *)f->data;
        for(int r = 0; r < d->num_ranges; ++r)
        {
            const ZCPORT_FONT_RANGE *range = &d->ranges[r];
            if(ch >= range->begin && ch < range->end && range->glyphs[ch - range->begin])
            {
                if(range_out)
                    *range_out = range;
                return range->glyphs[ch - range->begin];
            }
        }
    }
    if(range_out)
        *range_out = NULL;
    return NULL;
}

BITMAP *zcport_font_get_glyph(const FONT *f, int ch, const ZCPORT_FONT_RANGE **range_out)
{
    BITMAP *g = zcport_font_find_glyph(f, ch, range_out);
    if(!g)
        g = zcport_font_find_glyph(f, allegro_404_char, range_out);
    return g;
}

int zcport_is_zcport_font(const FONT *f)
{
    return f && f->vtable == &zcport_font_vtable;
}

void zcport_font_destroy(FONT *f)
{
    if(!f || f == &g_default_font)
        return;
    ZCPORT_FONT_DATA *d = data_of(f);
    if(d)
    {
        for(int r = 0; r < d->num_ranges; ++r)
        {
            ZCPORT_FONT_RANGE &range = d->ranges[r];
            for(int i = 0; i < range.end - range.begin; ++i)
                if(range.glyphs[i])
                    destroy_bitmap(range.glyphs[i]);
            free(range.glyphs);
        }
        free(d->ranges);
        free(d);
    }
    free(f);
}

void zcport_font_set_hooks(const ZCPORT_FONT_HOOKS *hooks)
{
    g_hooks = hooks;
}

/* ---- allegro/font.h and text.h utilities ----------------------------------- */

void destroy_font(FONT *f)
{
    if(f && f->vtable && f->vtable->destroy)
        f->vtable->destroy(f);
}

int get_font_ranges(FONT *f)
{
    if(!f || !f->vtable->get_font_ranges)
        return -1;
    return f->vtable->get_font_ranges(f);
}

int get_font_range_begin(FONT *f, int range)
{
    if(!f || !f->vtable->get_font_range_begin)
        return -1;
    return f->vtable->get_font_range_begin(f, range);
}

int get_font_range_end(FONT *f, int range)
{
    if(!f || !f->vtable->get_font_range_end)
        return -1;
    return f->vtable->get_font_range_end(f, range);
}

FONT *extract_font_range(FONT *f, int begin, int end)
{
    if(!f || !f->vtable->extract_font_range)
        return NULL;
    return f->vtable->extract_font_range(f, begin, end);
}

FONT *merge_fonts(FONT *f1, FONT *f2)
{
    if(!f1 || !f2 || !f1->vtable->merge_fonts)
        return NULL;
    return f1->vtable->merge_fonts(f1, f2);
}

int transpose_font(FONT *f, int drange)
{
    if(!f || !f->vtable->transpose_font)
        return -1;
    return f->vtable->transpose_font(f, drange);
}

static int all_ranges(FONT *f, int mono)
{
    if(!zcport_is_zcport_font(f))
        return 0;
    ZCPORT_FONT_DATA *d = data_of(f);
    if(d->num_ranges == 0)
        return 0;
    for(int r = 0; r < d->num_ranges; ++r)
        if(d->ranges[r].mono != mono)
            return 0;
    return 1;
}

int is_mono_font(FONT *f)
{
    return all_ranges(f, 1);
}

int is_color_font(FONT *f)
{
    return all_ranges(f, 0);
}

int is_trans_font(FONT *f)
{
    (void)f;
    return 0;
}

int font_has_alpha(FONT *f)
{
    (void)f;
    return 0;
}

void make_trans_font(FONT *f)
{
    (void)f;
}

int is_compatible_font(FONT *f1, FONT *f2)
{
    if(!zcport_is_zcport_font(f1) || !zcport_is_zcport_font(f2))
        return 0;
    return is_mono_font(f1) == is_mono_font(f2) && is_color_font(f1) == is_color_font(f2);
}

} // extern "C"

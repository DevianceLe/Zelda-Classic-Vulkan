// zcport files module: datafiles (load_datafile & co.) and the
// "file.dat#object" special file names.
//
// Format: docs/datafile.txt (big-endian numbers; DAT_MAGIC, an object
// count, then per object its properties, type id and a chunk holding the
// data). Object types: DATA (and any unknown type) as raw bytes, FILE
// (nested datafile), FONT (built with the zcport/font_internal.h data
// model), SAMP, MIDI, BMP/CMP/XCMP (bitmaps of every stored depth, converted
// as get_color_conversion() allows), RLE (RLE_SPRITE with Allegro's run
// layout), PAL; register_datafile_object() can add or replace readers.
//
// Written from scratch for zcport.

#include "files_internal.h"

#include <zcport/core.h>
#include <zcport/font_internal.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

using namespace zcfiles;

/* Font model stand-in (files_fontmodel.cpp): MSVC uses these only when no
 * module defines the real zcport_font_* functions (weak symbols elsewhere). */
#if defined(_MSC_VER)
#if defined(_M_IX86)
#define ZCF_SYM(x) "_" #x
#else
#define ZCF_SYM(x) #x
#endif
#define ZCF_ALT(real, fb) __pragma(comment(linker, "/alternatename:" ZCF_SYM(real) "=" ZCF_SYM(fb)))
ZCF_ALT(zcport_font_create, zcfiles_fb_font_create)
ZCF_ALT(zcport_font_add_range, zcfiles_fb_font_add_range)
ZCF_ALT(zcport_font_set_glyph, zcfiles_fb_font_set_glyph)
ZCF_ALT(zcport_font_find_glyph, zcfiles_fb_font_find_glyph)
ZCF_ALT(zcport_font_get_glyph, zcfiles_fb_font_get_glyph)
ZCF_ALT(zcport_is_zcport_font, zcfiles_fb_is_zcport_font)
ZCF_ALT(zcport_font_destroy, zcfiles_fb_font_destroy)
ZCF_ALT(zcport_font_set_hooks, zcfiles_fb_font_set_hooks)
ZCF_ALT(_rgb_r_shift_15, zcfiles_fb_rgb_r_shift_15)
ZCF_ALT(_rgb_g_shift_15, zcfiles_fb_rgb_g_shift_15)
ZCF_ALT(_rgb_b_shift_15, zcfiles_fb_rgb_b_shift_15)
ZCF_ALT(_rgb_r_shift_16, zcfiles_fb_rgb_r_shift_16)
ZCF_ALT(_rgb_g_shift_16, zcfiles_fb_rgb_g_shift_16)
ZCF_ALT(_rgb_b_shift_16, zcfiles_fb_rgb_b_shift_16)
ZCF_ALT(_rgb_r_shift_24, zcfiles_fb_rgb_r_shift_24)
ZCF_ALT(_rgb_g_shift_24, zcfiles_fb_rgb_g_shift_24)
ZCF_ALT(_rgb_b_shift_24, zcfiles_fb_rgb_b_shift_24)
ZCF_ALT(_rgb_r_shift_32, zcfiles_fb_rgb_r_shift_32)
ZCF_ALT(_rgb_g_shift_32, zcfiles_fb_rgb_g_shift_32)
ZCF_ALT(_rgb_b_shift_32, zcfiles_fb_rgb_b_shift_32)
ZCF_ALT(_rgb_a_shift_32, zcfiles_fb_rgb_a_shift_32)
#endif

namespace
{

struct ErrnoKeeper
{
    int saved;
    ErrnoKeeper() : saved(errno) {}
    ~ErrnoKeeper() { errno = saved; }
};

int ci_equal(const char *a, const char *b)
{
    for(;; ++a, ++b)
    {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);

        if(ca != cb)
            return 0;

        if(!ca)
            return 1;
    }
}

/* ---- registered object types --------------------------------------------- */

struct ObjectType
{
    int id;
    void *(*load)(PACKFILE *f, long size);
    void (*destroy)(void *data);
};

std::vector<ObjectType> &custom_types()
{
    static std::vector<ObjectType> t;
    return t;
}

const ObjectType *find_custom(int id)
{
    std::vector<ObjectType> &t = custom_types();

    for(size_t i = 0; i < t.size(); ++i)
        if(t[i].id == id)
            return &t[i];

    return NULL;
}

/* ---- color depth conversion ------------------------------------------------ */

/* The depth a bitmap stored at 'depth' is loaded at (set_color_conversion
 * flags and the current color depth). */
int load_depth(int depth, bool alpha)
{
    int target = get_color_depth();
    int conv = get_color_conversion();

    if(depth == target)
        return depth;

    static const int depths[5] = { 8, 15, 16, 24, 32 };
    int from = -1, to = -1;

    for(int i = 0; i < 5; ++i)
    {
        if(depths[i] == depth)
            from = i;

        if(depths[i] == target)
            to = i;
    }

    if(from < 0 || to < 0)
        return depth;

    int flag;

    if(depth == 32 && alpha)
    {
        if(target == 32)
            return depth;

        /* COLORCONV_32A_TO_8 .. _24 */
        flag = COLORCONV_32A_TO_8 << (to == 0 ? 0 : to - 1);
    }
    else
    {
        /* flags are 4 per source depth: to the other depths in order */
        int k = to < from ? to : to - 1;
        flag = 1 << (from * 4 + k);
    }

    return (conv & flag) ? target : depth;
}

int scale5(int v)
{
    return (v << 3) | (v >> 2);
}

int scale6(int v)
{
    return (v << 2) | (v >> 4);
}

/* r, g, b (0..255) of a pixel of a memory bitmap of the given depth (the
 * pixel layout of the _rgb_*_shift_* variables) */
void split_pixel(int depth, int c, int *r, int *g, int *b)
{
    switch(depth)
    {
    case 15:
        *r = scale5((c >> _rgb_r_shift_15) & 0x1F);
        *g = scale5((c >> _rgb_g_shift_15) & 0x1F);
        *b = scale5((c >> _rgb_b_shift_15) & 0x1F);
        break;

    case 16:
        *r = scale5((c >> _rgb_r_shift_16) & 0x1F);
        *g = scale6((c >> _rgb_g_shift_16) & 0x3F);
        *b = scale5((c >> _rgb_b_shift_16) & 0x1F);
        break;

    case 8:
    {
        const RGB &p = _current_palette[c & 0xFF];
        *r = scale6(p.r & 0x3F);
        *g = scale6(p.g & 0x3F);
        *b = scale6(p.b & 0x3F);
        break;
    }

    case 24:
        *r = (c >> _rgb_r_shift_24) & 0xFF;
        *g = (c >> _rgb_g_shift_24) & 0xFF;
        *b = (c >> _rgb_b_shift_24) & 0xFF;
        break;

    default:
        *r = (c >> _rgb_r_shift_32) & 0xFF;
        *g = (c >> _rgb_g_shift_32) & 0xFF;
        *b = (c >> _rgb_b_shift_32) & 0xFF;
        break;
    }
}

int pack_pixel(int depth, int r, int g, int b)
{
    switch(depth)
    {
    case 15: return ((r >> 3) << _rgb_r_shift_15) | ((g >> 3) << _rgb_g_shift_15) | ((b >> 3) << _rgb_b_shift_15);
    case 16: return ((r >> 3) << _rgb_r_shift_16) | ((g >> 2) << _rgb_g_shift_16) | ((b >> 3) << _rgb_b_shift_16);
    case 24: return (r << _rgb_r_shift_24) | (g << _rgb_g_shift_24) | (b << _rgb_b_shift_24);
    case 8:
    {
        /* nearest palette entry */
        int best = 0, bestd = 0x7FFFFFFF;

        for(int i = 1; i < PAL_SIZE; ++i)
        {
            const RGB &p = _current_palette[i];
            int dr = scale6(p.r & 0x3F) - r, dg = scale6(p.g & 0x3F) - g, db = scale6(p.b & 0x3F) - b;
            int d = dr * dr + dg * dg + db * db;

            if(d < bestd)
            {
                bestd = d;
                best = i;
            }
        }

        return best;
    }
    default: return (r << _rgb_r_shift_32) | (g << _rgb_g_shift_32) | (b << _rgb_b_shift_32);
    }
}

int mask_of(int depth)
{
    switch(depth)
    {
    case 8: return 0;
    case 15: return 0x7C1F;
    case 16: return 0xF81F;
    default: return 0xFF00FF;
    }
}

int get_px(BITMAP *b, int depth, int x, int y)
{
    unsigned char *row = b->line[y];

    switch(depth)
    {
    case 8: return row[x];
    case 15:
    case 16: return ((unsigned short *)row)[x];
    case 24: return row[x * 3] | (row[x * 3 + 1] << 8) | (row[x * 3 + 2] << 16);
    default: return (int)((uint32_t *)row)[x];
    }
}

void put_px(BITMAP *b, int depth, int x, int y, int c)
{
    unsigned char *row = b->line[y];

    switch(depth)
    {
    case 8: row[x] = (unsigned char)c; break;
    case 15:
    case 16: ((unsigned short *)row)[x] = (unsigned short)c; break;
    case 24:
        row[x * 3] = (unsigned char)c;
        row[x * 3 + 1] = (unsigned char)(c >> 8);
        row[x * 3 + 2] = (unsigned char)(c >> 16);
        break;
    default: ((uint32_t *)row)[x] = (uint32_t)c; break;
    }
}

BITMAP *convert_bitmap(BITMAP *src, int from, int to)
{
    BITMAP *dst = create_bitmap_ex(to, src->w, src->h);

    if(!dst)
        return src;

    bool keep_trans = (get_color_conversion() & COLORCONV_KEEP_TRANS) != 0;

    for(int y = 0; y < src->h; ++y)
        for(int x = 0; x < src->w; ++x)
        {
            int c = get_px(src, from, x, y);

            if(keep_trans && c == mask_of(from))
            {
                put_px(dst, to, x, y, mask_of(to));
                continue;
            }

            int r, g, b;
            split_pixel(from, c, &r, &g, &b);
            int o = pack_pixel(to, r, g, b);

            if(keep_trans && o == mask_of(to) && to != 8)
                o ^= 1;   /* stay off the mask color */

            put_px(dst, to, x, y, o);
        }

    destroy_bitmap(src);
    return dst;
}

/* ---- object readers -------------------------------------------------------- */

int read_word(PACKFILE *f)
{
    return pack_mgetw(f);
}

short read_sword(PACKFILE *f)
{
    return (short)pack_mgetw(f);
}

BITMAP *read_bitmap_data(PACKFILE *f, int *stored_depth)
{
    int bits = read_sword(f);
    int w = read_word(f);
    int h = read_word(f);
    bool alpha = bits == -32;
    int depth = bits < 0 ? -bits : bits;

    if(depth != 8 && depth != 15 && depth != 16 && depth != 24 && depth != 32)
        return NULL;

    BITMAP *b = create_bitmap_ex(depth, w, h);

    if(!b)
        return NULL;

    for(int y = 0; y < h; ++y)
    {
        switch(depth)
        {
        case 8:
            pack_fread(b->line[y], w, f);
            break;

        case 15:
        case 16:
            for(int x = 0; x < w; ++x)
            {
                int c = pack_igetw(f);
                int r = scale5((c >> 11) & 0x1F), g = scale6((c >> 5) & 0x3F), bl = scale5(c & 0x1F);
                put_px(b, depth, x, y, pack_pixel(depth, r, g, bl));
            }

            break;

        default:
            for(int x = 0; x < w; ++x)
            {
                int r = pack_getc(f), g = pack_getc(f), bl = pack_getc(f);
                int c = pack_pixel(depth, r, g, bl);

                if(alpha)
                    c |= pack_getc(f) << _rgb_a_shift_32;

                put_px(b, depth, x, y, c);
            }

            break;
        }
    }

    int target = load_depth(depth, alpha);

    if(target != depth)
        b = convert_bitmap(b, depth, target);

    if(stored_depth)
        *stored_depth = depth;

    return b;
}

/* RLE layout (allegro/rle.h, Allegro's): per row, counts of the depth's
 * size (8: signed char, 15/16: short, 24/32: int32): n > 0 n solid pixels
 * follow, n < 0 skip -n pixels; the row ends with the mask color value. */
BITMAP *rle_to_bitmap(const RLE_SPRITE *s)
{
    int depth = s->color_depth;
    BITMAP *b = create_bitmap_ex(depth, s->w, s->h);

    if(!b)
        return NULL;

    int mask = mask_of(depth);

    for(int y = 0; y < s->h; ++y)
        for(int x = 0; x < s->w; ++x)
            put_px(b, depth, x, y, mask);

    const signed char *p8 = s->dat;
    const short *p16 = (const short *)s->dat;
    const int32_t *p32 = (const int32_t *)s->dat;
    const char *end = (const char *)s->dat + s->size;

    for(int y = 0; y < s->h; ++y)
    {
        int x = 0;

        for(;;)
        {
            int c;

            if(depth == 8)
            {
                if((const char *)p8 >= end)
                    return b;

                c = *p8++;

                if(c == 0)
                    break;
            }
            else if(depth <= 16)
            {
                if((const char *)p16 >= end)
                    return b;

                c = *p16++;

                if(c == (short)mask)
                    break;
            }
            else
            {
                if((const char *)p32 >= end)
                    return b;

                c = *p32++;

                if(c == mask)
                    break;
            }

            if(c < 0)
            {
                x -= c;
                continue;
            }

            for(int k = 0; k < c; ++k, ++x)
            {
                int v = depth == 8 ? (unsigned char)*p8++ : depth <= 16 ? (unsigned short)*p16++ : *p32++;

                if(x >= 0 && x < s->w)
                    put_px(b, depth, x, y, v);
            }
        }
    }

    return b;
}

RLE_SPRITE *bitmap_to_rle(BITMAP *b)
{
    int depth = bitmap_color_depth(b);
    int mask = mask_of(depth);
    int limit = depth == 8 ? 127 : depth <= 16 ? 0x7FFF : 0x7FFFFFFF;
    std::vector<int32_t> out;

    for(int y = 0; y < b->h; ++y)
    {
        int x = 0;

        while(x < b->w)
        {
            bool solid = get_px(b, depth, x, y) != mask;
            int n = 0;

            while(x + n < b->w && n < limit && (get_px(b, depth, x + n, y) != mask) == solid)
                ++n;

            if(solid)
            {
                out.push_back(n);

                for(int k = 0; k < n; ++k)
                    out.push_back(get_px(b, depth, x + k, y));
            }
            else
                out.push_back(-n);

            x += n;
        }

        out.push_back(depth == 8 ? 0 : mask);
    }

    int unit = depth == 8 ? 1 : depth <= 16 ? 2 : 4;
    size_t size = out.size() * (size_t)unit;
    RLE_SPRITE *s = (RLE_SPRITE *)malloc(sizeof(RLE_SPRITE) + size);

    if(!s)
        return NULL;

    s->w = b->w;
    s->h = b->h;
    s->color_depth = depth;
    s->size = (int)size;

    for(size_t i = 0; i < out.size(); ++i)
    {
        if(unit == 1)
            ((signed char *)s->dat)[i] = (signed char)out[i];
        else if(unit == 2)
            ((short *)s->dat)[i] = (short)out[i];
        else
            ((int32_t *)s->dat)[i] = out[i];
    }

    return s;
}

void *read_rle(PACKFILE *f)
{
    int bits = read_sword(f);
    int w = read_word(f);
    int h = read_word(f);
    long size = pack_mgetl(f);
    int depth = bits < 0 ? -bits : bits;

    if(size < 0)
        return NULL;

    RLE_SPRITE *s = (RLE_SPRITE *)malloc(sizeof(RLE_SPRITE) + (size_t)size);

    if(!s)
        return NULL;

    s->w = w;
    s->h = h;
    s->color_depth = depth;
    s->size = (int)size;

    if(depth == 8)
        pack_fread(s->dat, size, f);
    else if(depth == 15 || depth == 16)
    {
        /* 16-bit run counts and 5.6.5 pixels */
        short *p = (short *)s->dat;
        long n = size / 2;

        for(int y = 0; y < h && n > 0; ++y)
        {
            for(;;)
            {
                short c = (short)pack_igetw(f);
                *p++ = c;
                --n;

                if(c == (short)mask_of(depth) || n <= 0)
                    break;

                if(c > 0)
                {
                    for(int k = 0; k < c && n > 0; ++k, --n)
                    {
                        int v = pack_igetw(f);
                        int r = scale5((v >> 11) & 0x1F), g = scale6((v >> 5) & 0x3F), bl = scale5(v & 0x1F);
                        *p++ = (short)pack_pixel(depth, r, g, bl);
                    }
                }
            }
        }
    }
    else
    {
        int32_t *p = (int32_t *)s->dat;
        long n = size / 4;

        for(int y = 0; y < h && n > 0; ++y)
        {
            for(;;)
            {
                int32_t c = (int32_t)pack_igetl(f);
                *p++ = c;
                --n;

                if(c == mask_of(32) || n <= 0)
                    break;

                if(c > 0)
                {
                    for(int k = 0; k < c && n > 0; ++k, --n)
                    {
                        int r = pack_getc(f), g = pack_getc(f), bl = pack_getc(f);
                        int v = pack_pixel(32, r, g, bl);

                        if(bits == -32)
                            v |= pack_getc(f) << _rgb_a_shift_32;

                        *p++ = v;
                    }
                }
            }
        }
    }

    int target = load_depth(depth, bits == -32);

    if(target != depth)
    {
        /* converted like a bitmap, then run-length coded again */
        BITMAP *b = rle_to_bitmap(s);
        free(s);

        if(!b)
            return NULL;

        b = convert_bitmap(b, depth, target);
        s = bitmap_to_rle(b);
        destroy_bitmap(b);
    }

    return s;
}

void *read_palette(PACKFILE *f)
{
    RGB *p = (RGB *)malloc(sizeof(RGB) * PAL_SIZE);

    if(!p)
        return NULL;

    for(int i = 0; i < PAL_SIZE; ++i)
    {
        p[i].r = (unsigned char)pack_getc(f);
        p[i].g = (unsigned char)pack_getc(f);
        p[i].b = (unsigned char)pack_getc(f);
        p[i].filler = (unsigned char)pack_getc(f);
    }

    return p;
}

void *read_sample(PACKFILE *f)
{
    int bits = read_sword(f);
    int freq = read_word(f);
    long len = pack_mgetl(f);
    int stereo = bits < 0;

    if(bits < 0)
        bits = -bits;

    SAMPLE *s = (SAMPLE *)calloc(1, sizeof(SAMPLE));

    if(!s)
        return NULL;

    s->bits = bits;
    s->stereo = stereo ? TRUE : FALSE;
    s->freq = freq;
    s->priority = 128;
    s->len = (unsigned long)len;
    s->loop_start = 0;
    s->loop_end = (unsigned long)len;
    s->param = 0;

    unsigned long count = (unsigned long)len * (stereo ? 2 : 1);

    if(bits == 8)
    {
        s->data = malloc(count ? count : 1);
        pack_fread(s->data, (long)count, f);
    }
    else
    {
        unsigned short *d = (unsigned short *)malloc((count ? count : 1) * 2);
        s->data = d;

        for(unsigned long i = 0; i < count; ++i)
            d[i] = (unsigned short)pack_igetw(f);
    }

    return s;
}

void *read_midi(PACKFILE *f)
{
    MIDI *m = (MIDI *)calloc(1, sizeof(MIDI));

    if(!m)
        return NULL;

    m->divisions = read_word(f);

    for(int i = 0; i < MIDI_TRACKS; ++i)
    {
        long len = pack_mgetl(f);
        m->track[i].len = (int)len;

        if(len > 0)
        {
            m->track[i].data = (unsigned char *)malloc((size_t)len);
            pack_fread(m->track[i].data, len, f);
        }
        else
            m->track[i].data = NULL;
    }

    return m;
}

BITMAP *mono_glyph(int w, int h, const unsigned char *bits)
{
    BITMAP *g = create_bitmap_ex(8, w, h);

    if(!g)
        return NULL;

    int stride = (w + 7) / 8;

    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
            g->line[y][x] = (bits[y * stride + x / 8] >> (7 - (x & 7))) & 1;

    return g;
}

void *read_font(PACKFILE *f)
{
    int fsize = read_sword(f);

    if(fsize == 8 || fsize == 16)
    {
        FONT *font = zcport_font_create(fsize);
        ZCPORT_FONT_RANGE *r = zcport_font_add_range(font, ' ', ' ' + 95, 1);
        unsigned char buf[16];

        for(int c = 0; c < 95; ++c)
        {
            pack_fread(buf, fsize, f);
            zcport_font_set_glyph(r, ' ' + c, mono_glyph(8, fsize, buf));
        }

        return font;
    }

    if(fsize == -1)
    {
        /* old proportional font: 8-bit pixels, drawn as mono */
        std::vector<BITMAP *> glyphs;
        int height = 0;

        for(int c = 0; c < 95; ++c)
        {
            int w = read_word(f), h = read_word(f);
            BITMAP *g = create_bitmap_ex(8, w, h);

            for(int y = 0; y < h; ++y)
                for(int x = 0; x < w; ++x)
                {
                    int v = pack_getc(f);

                    if(g)
                        g->line[y][x] = v ? 1 : 0;
                }

            if(h > height)
                height = h;

            glyphs.push_back(g);
        }

        FONT *font = zcport_font_create(height);
        ZCPORT_FONT_RANGE *r = zcport_font_add_range(font, ' ', ' ' + 95, 1);

        for(int c = 0; c < 95; ++c)
            zcport_font_set_glyph(r, ' ' + c, glyphs[c]);

        return font;
    }

    if(fsize != 0)
        return NULL;

    int nranges = read_word(f);
    struct Pending
    {
        int begin, end, mono;
        std::vector<BITMAP *> glyphs;
    };
    std::vector<Pending> ranges;
    int height = 0;

    for(int i = 0; i < nranges; ++i)
    {
        Pending p;
        p.mono = pack_getc(f);
        p.begin = (int)pack_mgetl(f);
        p.end = (int)pack_mgetl(f) + 1;

        for(int c = p.begin; c < p.end; ++c)
        {
            int w = read_word(f), h = read_word(f);
            BITMAP *g;

            if(p.mono)
            {
                int n = ((w + 7) / 8) * h;
                std::vector<unsigned char> bits((size_t)(n > 0 ? n : 1));
                pack_fread(&bits[0], n, f);
                g = mono_glyph(w, h, &bits[0]);
            }
            else
            {
                g = create_bitmap_ex(8, w, h);

                for(int y = 0; y < h; ++y)
                    pack_fread(g->line[y], w, f);

                int target = load_depth(8, false);

                if(target != 8)
                    g = convert_bitmap(g, 8, target);
            }

            if(h > height)
                height = h;

            p.glyphs.push_back(g);
        }

        ranges.push_back(p);
    }

    FONT *font = zcport_font_create(height);

    for(size_t i = 0; i < ranges.size(); ++i)
    {
        ZCPORT_FONT_RANGE *r = zcport_font_add_range(font, ranges[i].begin, ranges[i].end, ranges[i].mono ? 1 : 0);

        for(int c = ranges[i].begin; c < ranges[i].end; ++c)
        {
            if(r)
                zcport_font_set_glyph(r, c, ranges[i].glyphs[c - ranges[i].begin]);
            else if(ranges[i].glyphs[c - ranges[i].begin])
                destroy_bitmap(ranges[i].glyphs[c - ranges[i].begin]);
        }
    }

    return font;
}

void *read_binary(PACKFILE *f, long size)
{
    void *p = malloc(size > 0 ? (size_t)size : 1);

    if(!p)
        return NULL;

    if(size > 0)
        pack_fread(p, size, f);

    return p;
}

DATAFILE *load_objects(PACKFILE *f, void (*callback)(DATAFILE *));
void destroy_object(DATAFILE *d);

void *read_object(PACKFILE *f, int type, long size, void (*callback)(DATAFILE *))
{
    const ObjectType *custom = find_custom(type);

    if(custom && custom->load)
        return custom->load(f, size);

    switch(type)
    {
    case DAT_FILE: return load_objects(f, callback);
    case DAT_FONT: return read_font(f);
    case DAT_SAMPLE: return read_sample(f);
    case DAT_MIDI: return read_midi(f);
    case DAT_BITMAP:
    case DAT_C_SPRITE:
    case DAT_XC_SPRITE: return read_bitmap_data(f, NULL);
    case DAT_RLE_SPRITE: return read_rle(f);
    case DAT_PALETTE: return read_palette(f);
    default: return read_binary(f, size);
    }
}

/* Reads the property list in front of an object; returns the object type. */
long read_properties(PACKFILE *f, std::vector<DATAFILE_PROPERTY> &props)
{
    long type;

    while((type = pack_mgetl(f)) == DAT_PROPERTY)
    {
        DATAFILE_PROPERTY p;
        p.type = (int)pack_mgetl(f);
        long size = pack_mgetl(f);

        if(size < 0)
            size = 0;

        p.dat = (char *)malloc((size_t)size + 1);
        pack_fread(p.dat, size, f);
        p.dat[size] = 0;
        props.push_back(p);
    }

    return type;
}

DATAFILE_PROPERTY *finish_props(std::vector<DATAFILE_PROPERTY> &props)
{
    if(props.empty())
        return NULL;

    DATAFILE_PROPERTY *p = (DATAFILE_PROPERTY *)malloc(sizeof(DATAFILE_PROPERTY) * (props.size() + 1));

    for(size_t i = 0; i < props.size(); ++i)
        p[i] = props[i];

    p[props.size()].dat = NULL;
    p[props.size()].type = DAT_END;
    return p;
}

void free_props(DATAFILE_PROPERTY *p)
{
    if(!p)
        return;

    for(DATAFILE_PROPERTY *q = p; q->type != DAT_END; ++q)
        free(q->dat);

    free(p);
}

const char *prop_name(const std::vector<DATAFILE_PROPERTY> &props)
{
    for(size_t i = 0; i < props.size(); ++i)
        if(props[i].type == DAT_NAME)
            return props[i].dat;

    return "";
}

bool read_one(PACKFILE *f, DATAFILE *out, void (*callback)(DATAFILE *), std::vector<DATAFILE_PROPERTY> &props, long type)
{
    PACKFILE *c = pack_fopen_chunk(f, FALSE);

    if(!c)
        return false;

    long size = (long)chunk_size(c);
    out->dat = read_object(c, (int)type, size, callback);
    out->type = (int)type;
    out->size = size;
    out->prop = finish_props(props);
    pack_fclose_chunk(c);

    if(!out->dat)
    {
        free_props(out->prop);
        out->prop = NULL;
        return false;
    }

    if(callback && type != DAT_FILE)
        callback(out);

    return true;
}

DATAFILE *load_objects(PACKFILE *f, void (*callback)(DATAFILE *))
{
    long count = pack_mgetl(f);

    if(count < 0 || pack_feof(f) && count == EOF)
        return NULL;

    DATAFILE *dat = (DATAFILE *)calloc((size_t)count + 1, sizeof(DATAFILE));

    if(!dat)
        return NULL;

    long n = 0;

    for(; n < count; ++n)
    {
        std::vector<DATAFILE_PROPERTY> props;
        long type = read_properties(f, props);

        if(!read_one(f, &dat[n], callback, props, type))
        {
            for(size_t i = 0; i < props.size(); ++i)
                free(props[i].dat);

            break;
        }
    }

    dat[n].type = DAT_END;
    dat[n].dat = NULL;
    dat[n].size = 0;
    dat[n].prop = NULL;

    if(n < count)
    {
        unload_datafile(dat);
        return NULL;
    }

    return dat;
}

void destroy_object(DATAFILE *d)
{
    if(!d->dat)
        return;

    const ObjectType *custom = find_custom(d->type);

    if(custom && custom->destroy)
    {
        custom->destroy(d->dat);
        return;
    }

    switch(d->type)
    {
    case DAT_FILE:
        unload_datafile((DATAFILE *)d->dat);
        break;

    case DAT_FONT:
        zcport_font_destroy((FONT *)d->dat);
        break;

    case DAT_SAMPLE:
        /* also stops any voice still playing it (files_destroy.cpp) */
        destroy_sample((SAMPLE *)d->dat);
        break;

    case DAT_MIDI:
        destroy_midi((MIDI *)d->dat);
        break;

    case DAT_BITMAP:
    case DAT_C_SPRITE:
    case DAT_XC_SPRITE:
        destroy_bitmap((BITMAP *)d->dat);
        break;

    default:
        free(d->dat);
        break;
    }

    d->dat = NULL;
}

/* Opens a datafile for reading, positioned after DAT_MAGIC (or, for a
 * "file.dat#object" name, at the start of that nested datafile). */
PACKFILE *open_datafile(const char *filename)
{
    PACKFILE *f = pack_fopen(filename, F_READ_PACKED);

    if(!f)
        return NULL;

    if(!strchr(filename, '#'))
    {
        if(pack_mgetl(f) != DAT_MAGIC)
        {
            pack_fclose(f);
            return NULL;
        }
    }

    return f;
}

std::vector<std::string> split_path(const char *path)
{
    std::vector<std::string> parts;
    std::string cur;

    for(const char *p = path; *p; ++p)
    {
        if(*p == '/')
        {
            parts.push_back(cur);
            cur.clear();
        }
        else
            cur += *p;
    }

    parts.push_back(cur);
    return parts;
}

} /* namespace */

/* ------------------------------------------------------------------------ */
/* special files                                                            */
/* ------------------------------------------------------------------------ */

namespace zcfiles
{

PACKFILE *open_special(const char *filename)
{
    int saved = errno;
    const char *hash = strchr(filename, '#');
    std::string fname(filename, (size_t)(hash - filename));
    std::vector<std::string> parts = split_path(hash + 1);

    PACKFILE *f = pack_fopen(fname.c_str(), F_READ_PACKED);

    if(!f)
    {
        errno = ENOENT;
        return NULL;
    }

    if(pack_mgetl(f) != DAT_MAGIC)
    {
        pack_fclose(f);
        errno = ENOENT;
        return NULL;
    }

    PACKFILE *stream = f;

    for(size_t k = 0; k < parts.size(); ++k)
    {
        long count = pack_mgetl(stream);
        bool found = false;

        for(long i = 0; i < count && !pack_feof(stream); ++i)
        {
            std::vector<DATAFILE_PROPERTY> props;
            long type = read_properties(stream, props);
            bool match = !parts[k].empty() && ci_equal(prop_name(props), parts[k].c_str());

            for(size_t p = 0; p < props.size(); ++p)
                free(props[p].dat);

            if(match && (k + 1 == parts.size() || type == DAT_FILE))
            {
                PACKFILE *c = pack_fopen_chunk(stream, FALSE);

                if(!c)
                    break;

                set_close_parent(c);
                stream = c;
                found = true;
                break;
            }

            long cs = pack_mgetl(stream);
            pack_mgetl(stream);

            if(cs > 0)
                pack_fseek(stream, (int)cs);
        }

        if(!found)
        {
            pack_fclose(stream);
            errno = ENOENT;
            return NULL;
        }
    }

    errno = saved;
    return stream;
}

uint64_t special_size(const char *filename)
{
    ErrnoKeeper keep;
    PACKFILE *f = open_special(filename);

    if(!f)
        return 0;

    int64_t sz = chunk_size(f);
    pack_fclose(f);
    return (uint64_t)(sz > 0 ? sz : 0);
}

} /* namespace zcfiles */

/* ------------------------------------------------------------------------ */
/* public API                                                               */
/* ------------------------------------------------------------------------ */

extern "C" {

DATAFILE *load_datafile_callback(AL_CONST char *filename, AL_METHOD(void, callback, (DATAFILE *)))
{
    PACKFILE *f = open_datafile(filename);

    if(!f)
        return NULL;

    DATAFILE *dat = load_objects(f, callback);
    pack_fclose(f);
    return dat;
}

DATAFILE *load_datafile(AL_CONST char *filename)
{
    return load_datafile_callback(filename, NULL);
}

void unload_datafile(DATAFILE *dat)
{
    if(!dat)
        return;

    for(DATAFILE *d = dat; d->type != DAT_END; ++d)
    {
        destroy_object(d);
        free_props(d->prop);
    }

    free(dat);
}

DATAFILE *load_datafile_object(AL_CONST char *filename, AL_CONST char *objectname)
{
    PACKFILE *f = open_datafile(filename);

    if(!f)
        return NULL;

    std::vector<std::string> parts = split_path(objectname);
    PACKFILE *stream = f;
    std::vector<PACKFILE *> chunks;
    DATAFILE *result = NULL;

    for(size_t k = 0; k < parts.size() && !result; ++k)
    {
        long count = pack_mgetl(stream);
        bool descended = false;

        for(long i = 0; i < count && !pack_feof(stream); ++i)
        {
            std::vector<DATAFILE_PROPERTY> props;
            long type = read_properties(stream, props);
            bool match = ci_equal(prop_name(props), parts[k].c_str());

            if(match && k + 1 == parts.size())
            {
                result = (DATAFILE *)calloc(1, sizeof(DATAFILE));

                if(!read_one(stream, result, NULL, props, type))
                {
                    free(result);
                    result = NULL;
                }

                break;
            }

            for(size_t p = 0; p < props.size(); ++p)
                free(props[p].dat);

            if(match && type == DAT_FILE)
            {
                PACKFILE *c = pack_fopen_chunk(stream, FALSE);

                if(!c)
                    break;

                chunks.push_back(c);
                stream = c;
                descended = true;
                break;
            }

            long cs = pack_mgetl(stream);
            pack_mgetl(stream);

            if(cs > 0)
                pack_fseek(stream, (int)cs);
        }

        if(!descended)
            break;
    }

    while(!chunks.empty())
    {
        pack_fclose_chunk(chunks.back());
        chunks.pop_back();
    }

    pack_fclose(f);
    return result;
}

void unload_datafile_object(DATAFILE *dat)
{
    if(!dat)
        return;

    destroy_object(dat);
    free_props(dat->prop);
    free(dat);
}

DATAFILE *find_datafile_object(AL_CONST DATAFILE *dat, AL_CONST char *objectname)
{
    std::vector<std::string> parts = split_path(objectname);
    const DATAFILE *cur = dat;

    for(size_t k = 0; k < parts.size() && cur; ++k)
    {
        const DATAFILE *hit = NULL;

        for(const DATAFILE *d = cur; d->type != DAT_END; ++d)
        {
            if(ci_equal(get_datafile_property(d, DAT_NAME), parts[k].c_str()))
            {
                hit = d;
                break;
            }
        }

        if(!hit)
            return NULL;

        if(k + 1 == parts.size())
            return (DATAFILE *)hit;

        if(hit->type != DAT_FILE)
            return NULL;

        cur = (const DATAFILE *)hit->dat;
    }

    return NULL;
}

AL_CONST char *get_datafile_property(AL_CONST DATAFILE *dat, int type)
{
    static const char empty[1] = { 0 };

    if(dat && dat->prop)
        for(const DATAFILE_PROPERTY *p = dat->prop; p->type != DAT_END; ++p)
            if(p->type == type)
                return p->dat ? p->dat : empty;

    return empty;
}

void register_datafile_object(int id_, AL_METHOD(void *, load, (struct PACKFILE *f, long size)), AL_METHOD(void, destroy, (void *data)))
{
    std::vector<ObjectType> &t = custom_types();

    for(size_t i = 0; i < t.size(); ++i)
    {
        if(t[i].id == id_)
        {
            if(load)
                t[i].load = load;

            if(destroy)
                t[i].destroy = destroy;

            return;
        }
    }

    ObjectType o;
    o.id = id_;
    o.load = load;
    o.destroy = destroy;
    t.push_back(o);
}

void fixup_datafile(DATAFILE *data)
{
    (void)data;
}

DATAFILE_INDEX *create_datafile_index(AL_CONST char *filename)
{
    PACKFILE *f = open_datafile(filename);

    if(!f)
        return NULL;

    long count = pack_mgetl(f);

    if(count < 0)
    {
        pack_fclose(f);
        return NULL;
    }

    DATAFILE_INDEX *idx = (DATAFILE_INDEX *)malloc(sizeof(DATAFILE_INDEX));
    idx->filename = _al_strdup(filename);
    idx->offset = (long *)malloc(sizeof(long) * ((size_t)count + 1));
    long pos = 8;   /* DAT_MAGIC and the count */

    for(long i = 0; i < count; ++i)
    {
        idx->offset[i] = pos;
        long type;

        while((type = pack_mgetl(f)) == DAT_PROPERTY)
        {
            pack_mgetl(f);
            long size = pack_mgetl(f);
            pack_fseek(f, (int)size);
            pos += 12 + size;
        }

        long cs = pack_mgetl(f);
        pack_mgetl(f);
        pack_fseek(f, (int)cs);
        pos += 12 + cs;
    }

    idx->offset[count] = -1;
    pack_fclose(f);
    return idx;
}

DATAFILE *load_datafile_object_indexed(AL_CONST DATAFILE_INDEX *index, int item)
{
    if(!index)
        return NULL;

    PACKFILE *f = pack_fopen(index->filename, F_READ_PACKED);

    if(!f)
        return NULL;

    long off = index->offset[item];

    if(off < 0 || pack_fseek(f, (int)off) != 0)
    {
        pack_fclose(f);
        return NULL;
    }

    std::vector<DATAFILE_PROPERTY> props;
    long type = read_properties(f, props);
    DATAFILE *d = (DATAFILE *)calloc(1, sizeof(DATAFILE));

    if(!read_one(f, d, NULL, props, type))
    {
        free(d);
        d = NULL;
    }

    pack_fclose(f);
    return d;
}

void destroy_datafile_index(DATAFILE_INDEX *index)
{
    if(!index)
        return;

    free(index->filename);
    free(index->offset);
    free(index);
}

} /* extern "C" */

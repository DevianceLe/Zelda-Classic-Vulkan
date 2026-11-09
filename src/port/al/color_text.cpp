/*
 * zcport color module: text output and measurement.
 *
 * Rendering rules (measured black-box against the reference build):
 *  - Characters come from the string in the current text encoding
 *    (ugetxc); every character advances by its glyph width
 *    (char_length). Missing characters use the glyph of allegro_404_char,
 *    or are skipped with width 0 if the font has none.
 *  - Mono glyphs are drawn vertically centred in the font height, offset
 *    (font height - glyph height) / 2 with C division: set pixels in fg
 *    (used as a raw pixel value, so -1 gives all bits set), clear pixels
 *    in bg unless bg is -1. The drawing mode is ignored.
 *  - Color glyphs are drawn at the top. fg == -1: when bg >= 0 the cell
 *    (glyph width x font height) is first filled with rectfill(), which
 *    honours the drawing mode; then the glyph's non-mask pixels are copied
 *    (8-bit glyphs onto deeper bitmaps go through _current_palette). fg >= 0:
 *    non-mask pixels in fg, mask pixels in bg unless bg is -1, within the
 *    glyph; the drawing mode is ignored.
 *  - Everything is clipped to the bitmap's clip rectangle (to the bitmap
 *    itself when clipping is off).
 *
 * Text decoding uses the unicode module's ugetxc so that set_uformat()
 * applies. When that module is not linked (this module's own conformance
 * tests), a weak reference falls back to a UTF-8 decoder, Allegro's
 * default format.
 *
 * Written from scratch for zcport.
 */

#include "color_internal.h"
#include "zcport/font_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- current-format decoding with a weak link to the unicode module -------- */

extern "C" {

static int color_utf8_getxc(AL_CONST char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    int c = *p++;
    if(c & 0x80)
    {
        int n = 1;
        while(n < 7 && (c & (0x80 >> n)))
            ++n;
        c &= (1 << (7 - n)) - 1;
        while(--n > 0)
        {
            int t = *p;
            if((t & 0xC0) != 0x80)
                break;
            c = (c << 6) | (t & 0x3F);
            ++p;
        }
    }
    *s = (const char *)p;
    return c;
}

int (*zcport_color_ugetxc_fallback)(AL_CONST char **s) = color_utf8_getxc;

} // extern "C"

#if defined(_MSC_VER)
#  if defined(_M_IX86)
#    pragma comment(linker, "/alternatename:_ugetxc=_zcport_color_ugetxc_fallback")
#  else
#    pragma comment(linker, "/alternatename:ugetxc=zcport_color_ugetxc_fallback")
#  endif
#  define ZC_COLOR_UGETXC() (ugetxc)
#elif defined(__GNUC__)
#  pragma weak ugetxc
#  define ZC_COLOR_UGETXC() (&ugetxc ? ugetxc : zcport_color_ugetxc_fallback)
#else
#  define ZC_COLOR_UGETXC() (ugetxc)
#endif

namespace
{

inline int next_char(const char **s)
{
    return ZC_COLOR_UGETXC()(s);
}

/* Clip rectangle as half-open bounds. */
struct Clip
{
    int l, t, r, b;
};

Clip clip_of(BITMAP *bmp)
{
    Clip c;
    if(bmp->clip)
    {
        c.l = bmp->cl; c.t = bmp->ct; c.r = bmp->cr; c.b = bmp->cb;
    }
    else
    {
        c.l = 0; c.t = 0; c.r = bmp->w; c.b = bmp->h;
    }
    return c;
}

void draw_mono_glyph(BITMAP *bmp, BITMAP *g, int x, int y, int fg, int bg)
{
    Clip c = clip_of(bmp);
    for(int gy = 0; gy < g->h; ++gy)
    {
        int py = y + gy;
        if(py < c.t || py >= c.b)
            continue;
        for(int gx = 0; gx < g->w; ++gx)
        {
            int px = x + gx;
            if(px < c.l || px >= c.r)
                continue;
            if(zccolor::get_raw(g, gx, gy))
                zccolor::put_raw(bmp, px, py, (uint32_t)fg);
            else if(bg >= 0)
                zccolor::put_raw(bmp, px, py, (uint32_t)bg);
        }
    }
}

/* An 8-bit glyph pixel as a color of the destination depth. */
uint32_t expand_color(int glyph_depth, int dest_depth, uint32_t v)
{
    if(glyph_depth == dest_depth || glyph_depth != 8)
        return v;
    const RGB &p = _current_palette[v & 0xFF];
    return (uint32_t)makecol_depth(dest_depth, _rgb_scale_6[p.r & 63], _rgb_scale_6[p.g & 63], _rgb_scale_6[p.b & 63]);
}

/* 'top' is the text line's top, 'y' where the glyph's first row goes. */
void draw_color_glyph(BITMAP *bmp, BITMAP *g, int x, int top, int y, int fg, int bg, int font_h)
{
    int gdepth = bitmap_color_depth(g);
    int ddepth = bitmap_color_depth(bmp);
    uint32_t mask = (uint32_t)bitmap_mask_color(g);

    if(fg < 0 && bg >= 0)
        rectfill(bmp, x, top, x + g->w - 1, top + font_h - 1, bg);

    Clip c = clip_of(bmp);
    for(int gy = 0; gy < g->h; ++gy)
    {
        int py = y + gy;
        if(py < c.t || py >= c.b)
            continue;
        for(int gx = 0; gx < g->w; ++gx)
        {
            int px = x + gx;
            if(px < c.l || px >= c.r)
                continue;
            uint32_t v = zccolor::get_raw(g, gx, gy);
            if(fg < 0)
            {
                if(v != mask)
                    zccolor::put_raw(bmp, px, py, expand_color(gdepth, ddepth, v));
            }
            else if(v != mask)
                zccolor::put_raw(bmp, px, py, (uint32_t)fg);
            else if(bg >= 0)
                zccolor::put_raw(bmp, px, py, (uint32_t)bg);
        }
    }
}

int hook_render_char(AL_CONST FONT *f, int ch, int fg, int bg, BITMAP *bmp, int x, int y)
{
    const ZCPORT_FONT_RANGE *range = NULL;
    BITMAP *g = zcport_font_get_glyph(f, ch, &range);
    if(!g || !range)
        return 0;
    int h = f->vtable->font_height(f);
    if(range->mono)
        draw_mono_glyph(bmp, g, x, y + (h - g->h) / 2, fg, bg);
    else
        draw_color_glyph(bmp, g, x, y, y + (h - g->h) / 2, fg, bg, h);
    return g->w;
}

/* True when the font's glyphs are color glyphs (a zcport font made of
 * color ranges; mixed fonts count by their first range). */
bool color_font(AL_CONST FONT *f)
{
    const ZCPORT_FONT_DATA *d = (const ZCPORT_FONT_DATA *)f->data;
    return d && d->num_ranges > 0 && !d->ranges[0].mono;
}

void hook_render(AL_CONST FONT *f, AL_CONST char *text, int fg, int bg, BITMAP *bmp, int x, int y)
{
    if(!text)
        return;
    /* Color fonts drawn in their own colors get one background rectangle
     * for the whole string (drawing mode honoured, also for an empty
     * string: rectfill then covers x - 1 .. x like the reference). */
    if(fg < 0 && bg >= 0 && color_font(f))
    {
        int len = f->vtable->text_length(f, text);
        rectfill(bmp, x, y, x + len - 1, y + f->vtable->font_height(f) - 1, bg);
        bg = -1;
    }
    const char *p = text;
    int ch;
    while((ch = next_char(&p)) != 0)
        x += f->vtable->render_char(f, ch, fg, bg, bmp, x, y);
}

int hook_text_length(AL_CONST FONT *f, AL_CONST char *text)
{
    if(!text)
        return 0;
    const char *p = text;
    int ch, len = 0;
    while((ch = next_char(&p)) != 0)
        len += f->vtable->char_length(f, ch);
    return len;
}

const ZCPORT_FONT_HOOKS g_text_hooks = { hook_text_length, hook_render_char, hook_render };

struct InstallHooks
{
    InstallHooks() { zcport_font_set_hooks(&g_text_hooks); }
};
InstallHooks g_install_hooks;

/* textprintf: formatted into a 512-byte buffer like the reference. */
const int PRINTF_BUF = 512;

void format_text(char *buf, const char *format, va_list ap)
{
    vsnprintf(buf, PRINTF_BUF, format, ap);
    buf[PRINTF_BUF - 1] = 0;
}

} // namespace

extern "C" {

int text_length(AL_CONST FONT *f, AL_CONST char *str)
{
    return f->vtable->text_length(f, str);
}

int text_height(AL_CONST FONT *f)
{
    return f->vtable->font_height(f);
}

void textout_ex(BITMAP *bmp, AL_CONST FONT *f, AL_CONST char *str, int x, int y, int color, int bg)
{
    f->vtable->render(f, str, color, bg, bmp, x, y);
}

void textout_centre_ex(BITMAP *bmp, AL_CONST FONT *f, AL_CONST char *str, int x, int y, int color, int bg)
{
    int len = text_length(f, str);
    textout_ex(bmp, f, str, x - len / 2, y, color, bg);
}

void textout_right_ex(BITMAP *bmp, AL_CONST FONT *f, AL_CONST char *str, int x, int y, int color, int bg)
{
    int len = text_length(f, str);
    textout_ex(bmp, f, str, x - len, y, color, bg);
}

/*
 * Words are separated by blanks (space, tab, newline, return). With fewer
 * than two words, no free space or more free space than 'diff' the string
 * is drawn normally at x1; otherwise the free space is shared out evenly
 * between the words (float positions, truncated to int).
 */
static bool is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void textout_justify_ex(BITMAP *bmp, AL_CONST FONT *f, AL_CONST char *str, int x1, int x2, int y, int diff, int color, int bg)
{
    char buf[PRINTF_BUF];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    /* split into words */
    char *words[PRINTF_BUF];
    int nwords = 0, minlen = 0;
    char *p = buf;
    while(*p)
    {
        while(is_blank(*p))
            ++p;
        if(!*p)
            break;
        words[nwords++] = p;
        while(*p && !is_blank(*p))
            ++p;
        if(*p)
            *p++ = 0;
    }
    for(int i = 0; i < nwords; ++i)
        minlen += text_length(f, words[i]);

    int space = x2 - x1 - minlen;
    if(nwords < 2 || space <= 0 || space > diff)
    {
        textout_ex(bmp, f, str, x1, y, color, bg);
        return;
    }

    /* Positions advance in single precision: step = width + space / gaps,
     * summed first, then added (volatile keeps every value a float even
     * under /fp:fast; this order reproduces the reference exactly). */
    volatile float left = (float)x1;
    volatile float gap = (float)space / (float)(nwords - 1);
    for(int i = 0; i < nwords; ++i)
    {
        textout_ex(bmp, f, words[i], (int)left, y, color, bg);
        volatile float step = (float)text_length(f, words[i]) + gap;
        left = left + step;
    }
}

void textprintf_ex(BITMAP *bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char *format, ...)
{
    char buf[PRINTF_BUF];
    va_list ap;
    va_start(ap, format);
    format_text(buf, format, ap);
    va_end(ap);
    textout_ex(bmp, f, buf, x, y, color, bg);
}

void textprintf_centre_ex(BITMAP *bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char *format, ...)
{
    char buf[PRINTF_BUF];
    va_list ap;
    va_start(ap, format);
    format_text(buf, format, ap);
    va_end(ap);
    textout_centre_ex(bmp, f, buf, x, y, color, bg);
}

void textprintf_right_ex(BITMAP *bmp, AL_CONST FONT *f, int x, int y, int color, int bg, AL_CONST char *format, ...)
{
    char buf[PRINTF_BUF];
    va_list ap;
    va_start(ap, format);
    format_text(buf, format, ap);
    va_end(ap);
    textout_right_ex(bmp, f, buf, x, y, color, bg);
}

void textprintf_justify_ex(BITMAP *bmp, AL_CONST FONT *f, int x1, int x2, int y, int diff, int color, int bg, AL_CONST char *format, ...)
{
    char buf[PRINTF_BUF];
    va_list ap;
    va_start(ap, format);
    format_text(buf, format, ap);
    va_end(ap);
    textout_justify_ex(bmp, f, buf, x1, x2, y, diff, color, bg);
}

} // extern "C"

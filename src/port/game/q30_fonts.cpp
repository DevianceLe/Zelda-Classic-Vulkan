// The fonts 2.55 and 3.0 quests can name that 2.53's fonts.dat doesn't have: upstream's font
// ids 27-70, 73, 74 and 76-100 (core/fonts.h), built into the player from the glyph tables
// in q30_fonts_data.h (made by q30_fonts_gen.py from upstream's classic_fonts.dat). 2.53's
// own fonts and fonts.dat are untouched; zelda.cpp's q30_font() asks here for newer quests only.

#include "precompiled.h" //always first

#include <string.h>

#include <zcport/font_internal.h>

#include "zc_alleg.h"

namespace
{

struct Q30FontRange
{
    unsigned short first, count;    // character codes first .. first + count - 1
};

struct Q30FontDef
{
    int id;                     // upstream's font id
    const char *name;           // upstream's name for it (msgfont_str)
    int height;                 // the tallest glyph, as Allegro's font loader sets it
    int ranges;
    Q30FontRange range[4];
    unsigned int offset;        // into q30_font_bits
};

#include "q30_fonts_data.h"

enum { q30_font_count = 101 };  // upstream's font_max

FONT *build(const Q30FontDef &d)
{
    FONT *f = zcport_font_create(d.height);

    if(!f)
        return NULL;

    const unsigned char *p = q30_font_bits + d.offset;

    for(int r = 0; r < d.ranges; ++r)
    {
        const int first = d.range[r].first, count = d.range[r].count;
        ZCPORT_FONT_RANGE *range = zcport_font_add_range(f, first, first + count, 1);

        for(int c = 0; c < count; ++c)
        {
            const int w = p[0], h = p[1], stride = (w + 7) / 8;
            p += 2;

            // a monochrome glyph: pixels 0 (background) or 1 (text), as the datafile loader makes them
            BITMAP *g = create_bitmap_ex(8, w, h);

            if(g)
            {
                for(int y = 0; y < h; ++y)
                    for(int x = 0; x < w; ++x)
                        g->line[y][x] = (p[y * stride + x / 8] >> (7 - (x & 7))) & 1;
            }

            p += stride * h;

            if(range && g)
                zcport_font_set_glyph(range, first + c, g);
            else if(g)
                destroy_bitmap(g);
        }
    }

    return f;
}

} // namespace

// Upstream's font with this id when 2.53's fonts.dat lacks it, made on first use and kept;
// NULL for every other id.
FONT *q30_upstream_font(int id)
{
    static FONT *made[q30_font_count];

    if(id < 0 || id >= q30_font_count)
        return NULL;

    if(made[id])
        return made[id];

    for(size_t i = 0; i < sizeof(q30_font_defs) / sizeof(q30_font_defs[0]); ++i)
    {
        if(q30_font_defs[i].id == id)
        {
            made[id] = build(q30_font_defs[i]);
            return made[id];
        }
    }

    return NULL;
}

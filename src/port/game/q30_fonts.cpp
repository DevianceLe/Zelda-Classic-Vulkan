// The fonts 2.55 and 3.0 quests can name that 2.53's fonts.dat doesn't have: upstream's font
// ids 27-70, 73, 74 and 76-100 (core/fonts.h), built into the player from the glyph tables
// in q30_fonts_data.h (made by q30_fonts_gen.py from upstream's classic_fonts.dat). 2.53's
// own fonts and fonts.dat are untouched; zelda.cpp's q30_font() asks here for newer quests only.

#include "precompiled.h" //always first

#include <string.h>

#include <zcport/font_internal.h>

#include "zc_alleg.h"
#include "zdefs.h"
#include "fontsdat.h"   // FONT_GUI_MONO

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

// ---- font ids as both programs draw them -------------------------------------------------------
// One mapping for the player (zelda.cpp q30_font, script_drawing.cpp get_zc_font) and ZQuest's
// subscreen layout editor (zq_subscr_layout.cpp), so a widget's font can't come out different in
// the two.

extern DATAFILE *fontsdata;
extern FONT *nfont, *zfont, *z3font, *z3smallfont, *deffont, *lfont, *lfont_l, *pfont, *mfont, *ztfont, *sfont, *sfont2,
       *sfont3, *spfont, *ssfont1, *ssfont2, *ssfont3, *ssfont4, *gblafont, *goronfont, *zoranfont, *hylian1font,
       *hylian2font, *hylian3font, *hylian4font, *gboraclefont, *gboraclepfont, *dsphantomfont, *dsphantompfont;

// The fonts 3.0 numbers past 2.53's (upstream's core/fonts.h): font_nfont 71, font_sfont3 72
// and font_nfont2 75 are in this engine's fonts.dat; the others (27-70, 73, 74, 76-100) are
// built from upstream's font data above. NULL for every other id (2.53's own ids among them).
FONT *q30_font_newer(int id)
{
    switch(id)
    {
    case 71:
        return nfont;               // "NFont", FONT_GUI_PROP

    case 72:
        return sfont3;              // "Small 3", FONT_12x8

    case 75:
        return fontsdata ? (FONT *)fontsdata[FONT_GUI_MONO].dat : NULL;     // "NFont2"
    }

    return q30_upstream_font(id);
}

// 2.53's fonts by their font_* number (script_drawing.cpp's get_zc_font); the default font for
// any other number.
FONT *q30_font_classic(int id)
{
    switch(id)
    {
    case font_z3font: return z3font;
    case font_z3smallfont: return z3smallfont;
    case font_deffont: return deffont;
    case font_lfont: return lfont;
    case font_lfont_l: return lfont_l;
    case font_pfont: return pfont;
    case font_mfont: return mfont;
    case font_ztfont: return ztfont;
    case font_sfont: return sfont;
    case font_sfont2: return sfont2;
    case font_spfont: return spfont;
    case font_ssfont1: return ssfont1;
    case font_ssfont2: return ssfont2;
    case font_ssfont3: return ssfont3;
    case font_ssfont4: return ssfont4;
    case font_gblafont: return gblafont;
    case font_goronfont: return goronfont;
    case font_zoranfont: return zoranfont;
    case font_hylian1font: return hylian1font;
    case font_hylian2font: return hylian2font;
    case font_hylian3font: return hylian3font;
    case font_hylian4font: return hylian4font;
    case font_gboraclefont: return gboraclefont;
    case font_gboraclepfont: return gboraclepfont;
    case font_dsphantomfont: return dsphantomfont;
    case font_dsphantompfont: return dsphantompfont;
    }

    return zfont;
}

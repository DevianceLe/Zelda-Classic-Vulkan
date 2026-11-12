// Saving an older quest as 2.55.17 or 3.0 Nightly: the palette (CSET), tile (TILE), misc colour
// (MCLR) and game icon (ICON) sections. See zq_writers.h.
//
// Each writer writes the section as upstream's ZQuest writes it after opening the original
// quest: its loader's conversions are done here from what ZQuest's 2.53 load holds, which is
// the same data before those conversions.
//
// Only quests made in 2.53.1 or older are handled; a 2.55 quest saved as 3.0 is not yet.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "tiles.h"
#include "colors.h"
#include "zq_writers.h"

extern miscQdata misc;
extern char palnames[MAXLEVELS][17];
bool isblanktile(tiledata *buf, int i);     // tiles.cpp

namespace
{

enum { ERR_WRITE = 3 };

struct Out
{
    std::vector<byte> b;

    void p8(byte v) { b.push_back(v); }
    void p16(word v) { p8((byte)v); p8((byte)(v >> 8)); }
    void p32(dword v) { p16((word)v); p16((word)(v >> 16)); }
    void pn(const void *p, size_t n)
    {
        const byte *s = (const byte *)p;
        b.insert(b.end(), s, s + n);
    }
};

// The whole section: id, version, cversion (2.55 keeps a separate cversion, 1 for all of these;
// 3.0 writes the version twice), size, data.
int put_section(PACKFILE *f, dword id, const ZqNewerContext &ctx, word v255, word v30, const Out &data)
{
    const word v = ctx.target == ZQ_NEWER_30 ? v30 : v255;
    Out h;
    h.p16(v);
    h.p16(ctx.target == ZQ_NEWER_30 ? v : 1);
    h.p32((dword)data.b.size());

    if(pack_mputl((long)id, f) == EOF || pack_fwrite(&h.b[0], 8, f) != 8)
        return ERR_WRITE;

    if(!data.b.empty() && pack_fwrite((void *)&data.b[0], (long)data.b.size(), f) != (long)data.b.size())
        return ERR_WRITE;

    return ZQW_OK;
}

bool handled(const ZqNewerContext &ctx)
{
    return ctx.src_zelda_version < 0x255;
}

// ---- MCLR: misc colours ---------------------------------------------------------------------
//
// Upstream reads the same fields 2.53 does (from the misc section before misc v3); only the six
// tiles are 32 bits wide now.

int zqn_misc_colors(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!handled(ctx))
        return ZQW_UNSUPPORTED;

    const zcolors &c = misc.colors;
    Out o;
    o.p8(c.text);
    o.p8(c.caption);
    o.p8(c.overw_bg);
    o.p8(c.dngn_bg);
    o.p8(c.dngn_fg);
    o.p8(c.cave_fg);
    o.p8(c.bs_dk);
    o.p8(c.bs_goal);
    o.p8(c.compass_lt);
    o.p8(c.compass_dk);
    o.p8(c.subscr_bg);
    o.p8(c.triframe_color);
    o.p8(c.link_dot);
    o.p8(c.bmap_bg);
    o.p8(c.bmap_fg);
    o.p8(c.triforce_cset);
    o.p8(c.triframe_cset);
    o.p8(c.overworld_map_cset);
    o.p8(c.dungeon_map_cset);
    o.p8(c.blueframe_cset);
    o.p8(c.HCpieces_cset);
    o.p8(c.subscr_shadow);
    o.p8(c.msgtext);
    o.p32((dword)c.triforce_tile);
    o.p32((dword)c.triframe_tile);
    o.p32((dword)c.overworld_map_tile);
    o.p32((dword)c.dungeon_map_tile);
    o.p32((dword)c.blueframe_tile);
    o.p32((dword)c.HCpieces_tile);
    return put_section(f, ID_COLORS, ctx, 4, 4, o);
}

ZqNewerRegistration g_reg_colors(ID_COLORS, zqn_misc_colors, "misc colours for 2.55.17 and 3.0");

// ---- ICON: game icons -----------------------------------------------------------------------

int zqn_icons(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!handled(ctx))
        return ZQW_UNSUPPORTED;

    Out o;

    for(int i = 0; i < 4; ++i)
        o.p32((dword)misc.icons[i]);

    return put_section(f, ID_ICONS, ctx, 10, 10, o);
}

ZqNewerRegistration g_reg_icons(ID_ICONS, zqn_icons, "game icons for 2.55.17 and 3.0");

// ---- CSET: palettes -------------------------------------------------------------------------
//
// 2.53 keeps 512 level palettes of 13 csets; upstream's are 17 csets, and its loader makes the
// four new ones of each level palette the file holds from csets 1, 5, 7 and 8 of the main
// palette. How many level palettes the file holds depends on its version: 15 for quests before
// 1.92 build 73, 256 before palette section v4, 512 from then on; the others stay empty. The
// sprite csets move from 2.53's place to upstream's. 3.0 also scales every colour from 6 bits
// to 8 (Allegro's table).

const int UP_SPRITE = 8719;     // poSPRITE255
const int UP_TOTAL = 8749;      // pdTOTAL255
const int UP_LEVEL = 17;

const byte g_scale6[64] =
{
    0,   4,   8,   12,  16,  20,  24,  28,
    32,  36,  40,  44,  48,  52,  56,  60,
    65,  69,  73,  77,  81,  85,  89,  93,
    97,  101, 105, 109, 113, 117, 121, 125,
    130, 134, 138, 142, 146, 150, 154, 158,
    162, 166, 170, 174, 178, 182, 186, 190,
    195, 199, 203, 207, 211, 215, 219, 223,
    227, 231, 235, 239, 243, 247, 251, 255
};

int zqn_csets(PACKFILE *f, const ZqNewerContext &ctx)
{
    // a 2.55 quest's palettes at 2.53's own version 4 are all in ZQuest's (zqn_255.cpp: later ones)
    if(!handled(ctx) && !(ctx.src_part && ctx.src_version == 4))
        return ZQW_UNSUPPORTED;

    const word version = ctx.src_zelda_version;
    const byte build = ctx.src_build;
    const word sv = version > 0x192 ? ctx.src_version : 0;
    int levels = 512;

    if(version < 0x192 || (version == 0x192 && build < 73))
        levels = 15;
    else if(sv < 4)
        levels = 256;

    // Colours ZQuest now has in a level palette past those the file held: a 2.53.1 file
    // (palette section v4) holds all 512, and upstream would read them all from it.
    for(size_t i = (size_t)(poLEVEL + pdLEVEL * levels) * 48; i < (size_t)newerpoSPRITE * 48 && levels < 512; ++i)
        if(colordata[i])
            levels = 512;

    std::vector<byte> up((size_t)UP_TOTAL * 48, 0);
    memcpy(&up[0], colordata, pdFULL * 48);

    for(int k = 0; k < levels; ++k)
    {
        byte *d = &up[(size_t)(poLEVEL + UP_LEVEL * k) * 48];
        memcpy(d, colordata + (size_t)(poLEVEL + pdLEVEL * k) * 48, pdLEVEL * 48);
        memcpy(d + 13 * 48, colordata + 1 * 48, 48);
        memcpy(d + 14 * 48, colordata + 5 * 48, 48);
        memcpy(d + 15 * 48, colordata + 7 * 48, 48);
        memcpy(d + 16 * 48, colordata + 8 * 48, 48);
    }

    memcpy(&up[(size_t)UP_SPRITE * 48], colordata + (size_t)newerpoSPRITE * 48, 30 * 48);

    if(ctx.target == ZQ_NEWER_30)
    {
        for(size_t i = 0; i < up.size(); ++i)
        {
            if(up[i] > 63)
            {
                // Upstream indexes Allegro's 64-entry table with it, past its end.
                zqw_last_error = "a palette colour is past what 2.53 can hold (over 63)";
                return 2;
            }

            up[i] = g_scale6[up[i]];
        }
    }

    Out o;
    o.pn(&up[0], up.size());
    o.pn(palnames, (size_t)MAXLEVELS * PALNAMESIZE);
    int cycles = count_palcycles(&misc);
    o.p16((word)cycles);

    for(int i = 0; i < cycles; ++i)
    {
        for(int j = 0; j < 3; ++j) o.p8(misc.cycles[i][j].first);
        for(int j = 0; j < 3; ++j) o.p8(misc.cycles[i][j].count);
        for(int j = 0; j < 3; ++j) o.p8(misc.cycles[i][j].speed);
    }

    return put_section(f, ID_CSETS, ctx, 5, 6, o);
}

ZqNewerRegistration g_reg_csets(ID_CSETS, zqn_csets, "palettes for 2.55.17 and 3.0");

// ---- TILE: tiles ----------------------------------------------------------------------------
//
// Up to the last tile that isn't blank, as upstream counts them; a blank tile is a single 0
// (its format byte), the others their format and data, 4-bit tiles packed two pixels a byte as
// 2.53 already keeps them.

int zqn_tiles(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!handled(ctx))
        return ZQW_UNSUPPORTED;

    const int used = count_tiles(newtilebuf);
    Out o;
    o.b.reserve((size_t)used * 129 + 4);
    o.p32((dword)used);

    for(int i = 0; i < used; ++i)
    {
        if(isblanktile(newtilebuf, i))
        {
            o.p8(0);
            continue;
        }

        const byte format = newtilebuf[i].format;

        if(format != tf4Bit && format != tf8Bit)
        {
            zqw_last_error = "a tile has a format 2.53 doesn't have";
            return 2;
        }

        o.p8(format);
        o.pn(newtilebuf[i].data, (size_t)tilesize(format));
    }

    return put_section(f, ID_TILES, ctx, 3, 4, o);
}

ZqNewerRegistration g_reg_tiles(ID_TILES, zqn_tiles, "tiles for 2.55.17 and 3.0");

} // namespace

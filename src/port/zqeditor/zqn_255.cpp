// Saving a quest made in 2.55 as ZC 2.55.17 or 3.0 Nightly: the shared part (zqn_255.h), and
// the sections whose data the targets' loaders take as it is, so only the section's version
// and cversion change.
//
// Which source versions those are was read from the targets' readers (2.55.17's and 3.0's
// qst.cpp) and checked against upstream's own conversions of the pack's 2.55 quests.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqn_255.h"

namespace
{

const dword ID_ZINFO_ = ZC_ID('Z', 'I', 'N', 'F');
const dword ID_ADVMUSIC_ = ZC_ID('A', 'M', 'U', 'S');

struct Newest
{
    dword id;
    word v255, cv255;   // 2.55.17: V_ and CV_
    word v30;           // 3.0 writes the version as the cversion too
};

const Newest g_newest[] =
{
    { ID_HEADER, 8, 3, 9 },
    { ID_RULES, 17, 1, 18 },
    { ID_STRINGS, 10, 2, 13 },
    { ID_MISC, 16, 7, 23 },
    { ID_TILES, 3, 1, 4 },
    { ID_COMBOS, 45, 1, 73 },
    { ID_CSETS, 5, 1, 6 },
    { ID_MAPS, 30, 9, 41 },
    { ID_DMAPS, 21, 1, 28 },
    { ID_DOORS, 1, 1, 1 },
    { ID_ITEMS, 59, 15, 71 },
    { ID_WEAPONS, 8, 1, 9 },
    { ID_COLORS, 4, 1, 4 },
    { ID_ICONS, 10, 1, 10 },
    { ID_INITDATA, 38, 15, 52 },
    { ID_GUYS, 48, 4, 60 },
    { ID_MIDIS, 4, 3, 5 },
    { ID_CHEATS, 1, 1, 1 },
    { ID_COMBOALIASES, 5, 1, 5 },
    { ID_LINKSPRITES, 16, 1, 16 },
    { ID_SUBSCREEN, 12, 3, 21 },
    { ID_ITEMDROPSETS, 2, 1, 2 },
    { ID_FFSCRIPT, 22, 1, 31 },
    { ID_SFX, 8, 5, 9 },
    { ID_FAVORITES, 4, 1, 4 },
    { ID_ZINFO_, 3, 0, 5 },
    { ID_ADVMUSIC_, 0, 0, 1 },
};

} // namespace

bool zqn255_source(const ZqNewerContext &ctx)
{
    return (ctx.target == ZQ_NEWER_255 || ctx.target == ZQ_NEWER_30) && ctx.src_zelda_version >= 0x255;
}

void zqn255_newest(dword id, int target, word *version, word *cversion)
{
    for(size_t i = 0; i < sizeof(g_newest) / sizeof(g_newest[0]); ++i)
    {
        if(g_newest[i].id != id)
            continue;

        *version = target == ZQ_NEWER_30 ? g_newest[i].v30 : g_newest[i].v255;
        *cversion = target == ZQ_NEWER_30 ? g_newest[i].v30 : g_newest[i].cv255;
        return;
    }

    *version = *cversion = 0;
}

bool zqn255_section(const ZqNewerContext &ctx, Zqn255Section &s)
{
    if(!ctx.src_part)
        return false;

    std::vector<byte> all;

    if(!zq_own_part_bytes(*ctx.src_part, all))
        return false;

    Zqn255In r(all);
    s.version = r.u16();
    s.cversion = r.u16();
    size_t extra = ctx.src_part->id == ID_RULES ? (s.version > 16 ? 4 : 0) : ctx.src_part->id == ID_FFSCRIPT ? (s.version >= 18 ? 2 : 0) : 0;
    s.extra = r.bytes(extra);
    dword size = r.u32();

    if(r.bad || (size_t)size != all.size() - r.pos)
    {
        zqw_last_error = "its own version's section isn't laid out as expected";
        return false;
    }

    s.data.assign(all.begin() + r.pos, all.end());
    return true;
}

int zqn255_emit(PACKFILE *f, dword id, word version, word cversion, const std::vector<byte> &extra, const std::vector<byte> &data)
{
    Zqn255Out o;
    o.w(version);
    o.w(cversion);
    o.put(extra);
    o.l((int64_t)data.size());

    if(pack_mputl((long)id, f) == EOF || pack_fwrite(&o.b[0], (long)o.b.size(), f) != (long)o.b.size())
        return 1;

    if(!data.empty() && pack_fwrite(&data[0], (long)data.size(), f) != (long)data.size())
        return 1;

    return ZQW_OK;
}

void zqn255_tape_set(Tape &t, const char *key, int64_t value)
{
    if(TapeField *f = t.find(key))
    {
        f->value = value;
        return;
    }

    TapeField x;
    x.key = key;
    x.value = value;
    t.f.push_back(x);
}

bool zqn255_tape_rebuild(bool (*schema)(TapeParser &, int), const Tape &src, int v, std::vector<byte> &out)
{
    Tape t;
    TapeParser p(&src, &t);

    if(!schema(p, v) || p.bad)
        return false;

    out.clear();
    t.encode(out);
    return true;
}

namespace
{

// ---- sections whose data the target takes as it is ---------------------------------------

struct SameData
{
    dword id;
    word from255, from30;   // the oldest source version whose data each target keeps as it is (0: none)
};

// From the readers:
// - ICON v10: four words of icons.
// - MCLR v4: the colours, with the 825-page tile fields (v3) and their CSets.
// - CMBA v5: aliases with 32-bit combos and layer masks; nothing newer.
// - CSET v6 (3.0): v6 adds nothing to the data; 3.0 reads a v5 section differently
//   (its palette names), so only v6 is copied.
// - TILE v3: 3.0's v4 is the same data (V_TILES_ZTILESET_MIN only marks the version).
// - LINK v16: the newest Link sprite layout in both.
// - DROP v2, CHT v1, DOOR v1: no newer versions (DOOR and DROP: see zqn_basic for their names).
// - FAVS v4: favorites with modes and the editor's positions.
// - DMAP v27 (3.0): v28 only adds a field the writer leaves at its default.
// - MISC v21 (3.0): v22/v23 hold nothing a v21 section lacks.
const SameData g_same[] =
{
    { ID_ICONS, 10, 10 },
    { ID_COLORS, 4, 4 },
    { ID_COMBOALIASES, 5, 5 },
    { ID_CSETS, 5, 6 },
    { ID_TILES, 3, 3 },
    { ID_LINKSPRITES, 16, 16 },
    { ID_FAVORITES, 4, 4 },
    { ID_CHEATS, 1, 1 },
    // 2.55.17 only: sections already at its newest version
    { ID_DMAPS, 21, 0 },
    { ID_ITEMS, 59, 0 },
    { ID_MAPS, 30, 0 },
    { ID_COMBOS, 45, 0 },
    { ID_INITDATA, 38, 0 },
    { ID_GUYS, 48, 0 },
    { ID_SUBSCREEN, 12, 0 },
    { ID_FFSCRIPT, 22, 0 },
};

const SameData *same_data(dword id)
{
    for(size_t i = 0; i < sizeof(g_same) / sizeof(g_same[0]); ++i)
        if(g_same[i].id == id)
            return &g_same[i];

    return NULL;
}

int write_same_data(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part)
        return ZQW_UNSUPPORTED;

    const SameData *sd = same_data(ctx.src_part->id);
    word from = sd ? (ctx.target == ZQ_NEWER_30 ? sd->from30 : sd->from255) : 0;

    if(!from || ctx.src_version < from)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    word v, cv;
    zqn255_newest(ctx.src_part->id, ctx.target, &v, &cv);

    if(s.version > v)
        return ZQW_UNSUPPORTED;     // newer than the target: never written older

    return zqn255_emit(f, ctx.src_part->id, v, cv, s.extra, s.data);
}

// ---- misc colours v3 --------------------------------------------------------------------
//
// Version 3 holds the six subscreen tiles twice: as words after blueframe_cset (what older
// readers took) and as dwords at the end, which the readers read last, so they win. Version 4
// drops the words.
int write_colors_v3(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version != 3)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    if(s.data.size() != 59)
    {
        zqw_last_error = "its misc colours aren't laid out as version 3's";
        return 2;
    }

    std::vector<byte> d(s.data.begin(), s.data.begin() + 20);
    d.insert(d.end(), s.data.begin() + 32, s.data.end());
    word v, cv;
    zqn255_newest(ID_COLORS, ctx.target, &v, &cv);
    return zqn255_emit(f, ID_COLORS, v, cv, s.extra, d);
}

// ---- tiles v2 -----------------------------------------------------------------------------
//
// Version 2 stores every tile up to its count whole: a format byte, then the data (128 bytes
// for a 4-bit tile, packed two pixels to the byte; 256 for 8-bit and for format 0; 512, 768,
// 1024 for 16, 24 and 32 bits). The loaders keep a tile whose data is all zeros as a blank tile
// of its format, and the newer writers write a blank tile as a single 0 and stop after the last
// tile that isn't blank (count_tiles). Other tiles go out as they came: a 4-bit tile unpacked
// and packed again is the same 128 bytes.
size_t tile_bytes(byte format)
{
    switch(format)
    {
    case 1: return 128;     // tf4Bit, packed
    case 3: return 512;     // tf16Bit
    case 4: return 768;     // tf24Bit
    case 5: return 1024;    // tf32Bit
    default: return 256;    // tf8Bit, and what tilesize() gives anything else
    }
}

int write_tiles_v2(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version != 2)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const size_t NEWMAXTILES_ = 214500;
    Zqn255In r(s.data);
    size_t count = r.u32();

    if(count > NEWMAXTILES_)
        count = NEWMAXTILES_;

    struct Tile { size_t at, len; byte format; bool blank; };
    std::vector<Tile> tiles;
    tiles.reserve(count);

    for(size_t i = 0; i < count && !r.bad; ++i)
    {
        Tile t;
        t.format = r.u8();
        t.len = tile_bytes(t.format);
        t.at = r.pos;
        r.skip(t.len);
        t.blank = true;

        for(size_t k = 0; !r.bad && k < t.len && t.blank; ++k)
            if(s.data[t.at + k])
                t.blank = false;

        tiles.push_back(t);
    }

    if(r.bad)
    {
        zqw_last_error = "its tiles aren't laid out as version 2's";
        return 2;
    }

    size_t used = tiles.size();

    while(used > 0 && tiles[used - 1].blank)
        --used;

    std::vector<byte> d;
    d.reserve(4 + used * 129);
    Zqn255Out o;
    o.l((int64_t)used);
    d.swap(o.b);

    for(size_t i = 0; i < used; ++i)
    {
        const Tile &t = tiles[i];

        if(t.blank)
        {
            d.push_back(0);
            continue;
        }

        d.push_back(t.format);
        // format 0 with data: upstream writes tilesize(0) = 256 bytes of it
        d.insert(d.end(), s.data.begin() + t.at, s.data.begin() + t.at + t.len);
    }

    word v, cv;
    zqn255_newest(ID_TILES, ctx.target, &v, &cv);
    return zqn255_emit(f, ID_TILES, v, cv, s.extra, d);
}

// ---- palettes v5, as 3.0 ------------------------------------------------------------------
//
// Version 5 already has 17 csets to a level palette and the sprite csets in their new place;
// 3.0's version 6 holds the colours as 8 bits instead of 6, which its reader makes with
// Allegro's table (_rgb_scale_6). The palette names and cycles stay as they are.
const byte g_scale6_[64] =
{
    0,   4,   8,   12,  16,  20,  24,  28,  32,  36,  40,  44,  48,  52,  56,  60,
    65,  69,  73,  77,  81,  85,  89,  93,  97,  101, 105, 109, 113, 117, 121, 125,
    130, 134, 138, 142, 146, 150, 154, 158, 162, 166, 170, 174, 178, 182, 186, 190,
    195, 199, 203, 207, 211, 215, 219, 223, 227, 231, 235, 239, 243, 247, 251, 255
};

int write_csets_v5_30(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version != 5 || ctx.target != ZQ_NEWER_30)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const size_t colours = (size_t)8749 * 48;     // pdTOTAL255 csets of 16 RGB colours

    if(s.data.size() < colours)
    {
        zqw_last_error = "its palettes aren't laid out as version 5's";
        return 2;
    }

    std::vector<byte> d = s.data;

    for(size_t i = 0; i < colours; ++i)
    {
        if(d[i] > 63)
        {
            zqw_last_error = "a palette colour is past what a 6-bit palette holds (over 63)";
            return 2;
        }

        d[i] = g_scale6_[d[i]];
    }

    return zqn255_emit(f, ID_CSETS, 6, 6, s.extra, d);
}

// ---- combo aliases v3, v4 ---------------------------------------------------------------
//
// The aliases are the same from version 3 on; version 4 added the combo pools (a count, then
// each pool), version 5 the autocombos (a count, then each). Both writers write the pools and
// autocombos up to the last one in use, so a section without them gets a count of 0.
int write_aliases_old(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 3 || ctx.src_version > 4)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    std::vector<byte> d = s.data;

    if(s.version < 4)
        d.push_back(0), d.push_back(0);     // no combo pools

    d.push_back(0), d.push_back(0);         // no autocombos
    return zqn255_emit(f, ID_COMBOALIASES, 5, ctx.target == ZQ_NEWER_30 ? 5 : 1, s.extra, d);
}

// ---- favorites v1-v3 ----------------------------------------------------------------------
//
// readfavorites: rows of 20 favorites (13 for a version 1 or 2 section of exactly 100), each a
// mode (from version 4; 0 is a combo) and a number; before version 4 a separate list of favorite
// aliases, which the loaders drop; from version 2 the editor's first combo / alias / pool list
// positions and map page buttons. The writers write rows of 20 and pages of 140, the
// favorites up to the last one set, 4 list positions and 9 map page buttons.
int write_favorites_old(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 1 || ctx.src_version > 3)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const int PER_ROW = 20, PER_PAGE = 140, MAXFAV = 140 * 9, COLS = 4, PAGES = 9;
    Zqn255In r(s.data);
    int per_row = PER_ROW;

    if(s.version >= 3)
        per_row = r.u16();

    int num = r.u16();

    if(num > MAXFAV)
    {
        zqw_last_error = "it has more favorite combos than the newer versions hold";
        return 2;
    }

    if(s.version < 3 && num == 100)
        per_row = 13;

    std::vector<long> fav(MAXFAV, -1);

    for(int i = 0; i < num && !r.bad; ++i)
    {
        long c = r.s32();

        if(per_row == PER_ROW)
            fav[i] = c;
        else if(per_row != 0)
        {
            int at = (i % per_row) + (i / per_row) * PER_ROW;

            if(at >= 0 && at < MAXFAV)
                fav[at] = c;
        }
    }

    int aliases = r.u16();      // dropped
    r.skip((size_t)aliases * 4);
    long first[COLS][3], page[PAGES][2];
    memset(first, 0, sizeof(first));
    memset(page, 0, sizeof(page));

    if(s.version >= 2)
    {
        int cols = r.u16();

        for(int q = 0; q < cols && !r.bad; ++q)
        {
            long a = r.s32(), b = r.s32(), c = r.s32();

            if(q < COLS)
                first[q][0] = a, first[q][1] = b, first[q][2] = c;
        }

        int pages = r.u16();

        for(int q = 0; q < pages && !r.bad; ++q)
        {
            long a = r.s32(), b = r.s32();

            if(q < PAGES)
                page[q][0] = a, page[q][1] = b;
        }
    }

    if(!r.at_end())
    {
        zqw_last_error = "its favorites aren't laid out as expected";
        return 2;
    }

    int count = 0;

    for(int i = MAXFAV - 1; i >= 0; --i)
        if(fav[i] != -1)
        {
            count = i + 1;
            break;
        }

    Zqn255Out o;
    o.w(PER_ROW);
    o.w(PER_PAGE);
    o.w(count);

    for(int i = 0; i < count; ++i)
    {
        o.c(0);
        o.l(fav[i]);
    }

    o.w(COLS);

    for(int q = 0; q < COLS; ++q)
        o.l(first[q][0]), o.l(first[q][1]), o.l(first[q][2]);

    o.w(PAGES);

    for(int q = 0; q < PAGES; ++q)
        o.l(page[q][0]), o.l(page[q][1]);

    return zqn255_emit(f, ID_FAVORITES, 4, ctx.target == ZQ_NEWER_30 ? 4 : 1, s.extra, o.b);
}

ZqNewerRegistration g_reg_aliases_old(ID_COMBOALIASES, write_aliases_old, "2.55 combo aliases v3-4");
ZqNewerRegistration g_reg_favs_old(ID_FAVORITES, write_favorites_old, "2.55 favorites v1-3");
ZqNewerRegistration g_reg_colors_v3(ID_COLORS, write_colors_v3, "2.55 misc colours v3");
ZqNewerRegistration g_reg_tiles_v2(ID_TILES, write_tiles_v2, "2.55 tiles v2");
ZqNewerRegistration g_reg_csets_v5(ID_CSETS, write_csets_v5_30, "2.55 palettes v5 as 3.0");
ZqNewerRegistration g_reg_icons(ID_ICONS, write_same_data, "2.55 game icons");
ZqNewerRegistration g_reg_colors(ID_COLORS, write_same_data, "2.55 misc colours");
ZqNewerRegistration g_reg_aliases(ID_COMBOALIASES, write_same_data, "2.55 combo aliases");
ZqNewerRegistration g_reg_csets(ID_CSETS, write_same_data, "2.55 palettes");
ZqNewerRegistration g_reg_tiles(ID_TILES, write_same_data, "2.55 tiles");
ZqNewerRegistration g_reg_link(ID_LINKSPRITES, write_same_data, "2.55 Link sprites");
ZqNewerRegistration g_reg_favs(ID_FAVORITES, write_same_data, "2.55 favorites");
ZqNewerRegistration g_reg_cheats(ID_CHEATS, write_same_data, "2.55 cheats");
ZqNewerRegistration g_reg_dmaps_255(ID_DMAPS, write_same_data, "2.55.17 DMaps as they are");
ZqNewerRegistration g_reg_items_255(ID_ITEMS, write_same_data, "2.55.17 items as they are");
ZqNewerRegistration g_reg_maps_255(ID_MAPS, write_same_data, "2.55.17 maps as they are");
ZqNewerRegistration g_reg_combos_255(ID_COMBOS, write_same_data, "2.55.17 combos as they are");
ZqNewerRegistration g_reg_init_255(ID_INITDATA, write_same_data, "2.55.17 init data as it is");
ZqNewerRegistration g_reg_guys_255(ID_GUYS, write_same_data, "2.55.17 enemies as they are");
ZqNewerRegistration g_reg_subs_255(ID_SUBSCREEN, write_same_data, "2.55.17 subscreens as they are");
ZqNewerRegistration g_reg_ffsc_255(ID_FFSCRIPT, write_same_data, "2.55.17 scripts as they are");

} // namespace

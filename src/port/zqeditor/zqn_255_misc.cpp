// Saving a quest made in 2.55 as 2.55.17 or 3.0 Nightly (zqn_255.h): the misc data (MISC).
//
// Ported from both versions' readmisc (versions 7-23) and writemisc (2.55.17: 16, 3.0: 23):
// - shops, info shops and warp rings as stored; names are written as strings, zeros after them
//   (some files have leftovers of older names there);
//   each shop's items without "has item" move to its end, and each info shop's empty strings
//   (bubble sorts in readmisc); shops and infos are counted again for the writer;
// - warp rings are counted up to 16 (count_warprings), though 9 are held: past the ninth it
//   reads the palette cycles that follow the rings in upstream's miscQdata, which the palettes
//   section sets (the cycles at the end of CSET);
// - older versions' missing parts take readmisc's defaults: no shop strings (v8), no script
//   values (v9; 3.0 writes their names as zeros), last-compiled -1 (v11), no misc sprites (v12),
//   cleared bottle types and shops (v13), the default misc sounds (v14, 15, 16), no save menus
//   (v17); bytes become words where the newer versions widened them (shop items v20, misc
//   sprites v21, misc sounds and save menu sounds v19).
// - save menus older than version 23 (a MIDI number for music before v18, byte sounds before v19,
//   a bare script before v22, no option offsets, shadows or texts before v23) aren't converted
//   yet: such a quest is refused; none in the pack has any.

#include "precompiled.h" //always first

#include <string.h>
#include <vector>

#include "zdefs.h"
#include "zq_writers.h"
#include "zqn_255.h"

extern miscQdata misc;

namespace
{

const int NUM_RINGS = 9, RING_BYTES = 28, SPRMAX = 256, SFXMAX = 256;

struct Shop
{
    byte name[32];
    word item[3], price[3], str[3];
    byte hasitem[3];
};

struct Info
{
    byte name[32];
    word str[3], price[3];
};

// The palette cycles as upstream holds them after loading the quest: those at the end of the
// palettes section, or ZQuest's for a section of 2.53's own layout (version 4).
bool palette_cycles(int target, byte *cycles /* 256*3*3 */)
{
    memset(cycles, 0, 256 * 3 * 3);
    ZqNewerContext c;
    c.target = target;
    c.src_zelda_version = 0x255;
    c.src_build = 0;
    c.src_part = zq_find_part(ID_CSETS);
    c.src_version = c.src_cversion = 0;

    if(!c.src_part)
        return true;

    zq_part_versions(*c.src_part, &c.src_version, &c.src_cversion);

    if(c.src_version < 5)
    {
        byte *p = cycles;

        for(int i = 0; i < 256; ++i)
            for(int j = 0; j < 3; ++j)
            {
                *p++ = misc.cycles[i][j].first;
                *p++ = misc.cycles[i][j].count;
                *p++ = misc.cycles[i][j].speed;
            }

        return true;
    }

    Zqn255Section s;

    if(!zqn255_section(c, s))
        return false;

    Zqn255In r(s.data, (size_t)8749 * 48 + 512 * 17);
    int n = r.u16();

    for(int i = 0; i < n && !r.bad; ++i)
    {
        byte first[3], count[3], speed[3];

        for(int j = 0; j < 3; ++j) first[j] = r.u8();
        for(int j = 0; j < 3; ++j) count[j] = r.u8();
        for(int j = 0; j < 3; ++j) speed[j] = r.u8();

        if(i < 256)
            for(int j = 0; j < 3; ++j)
            {
                cycles[(i * 3 + j) * 3] = first[j];
                cycles[(i * 3 + j) * 3 + 1] = count[j];
                cycles[(i * 3 + j) * 3 + 2] = speed[j];
            }
    }

    return !r.bad;
}

int write_misc(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 7 || ctx.src_version > 22)
        return ZQW_UNSUPPORTED;

    const bool t30 = ctx.target == ZQ_NEWER_30;

    if(!t30 && ctx.src_version > 16)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const int v = s.version;
    Zqn255In r(s.data);
    std::vector<Shop> shop(256);
    std::vector<Info> info(256);
    memset(&shop[0], 0, sizeof(Shop) * 256);
    memset(&info[0], 0, sizeof(Info) * 256);

    int shops = r.u16();

    if(shops > 256)
        r.bad = true;

    for(int i = 0; i < shops && !r.bad; ++i)
    {
        Shop &t = shop[i];
        std::vector<byte> nm = r.bytes(32);

        if(!r.bad)
            for(int k = 0; k < 32 && nm[k]; ++k)    // upstream's save has zeros after the name
                t.name[k] = nm[k];

        for(int j = 0; j < 3; ++j)
            t.item[j] = v >= 20 ? r.u16() : r.u8();

        for(int j = 0; j < 3; ++j)
            t.price[j] = r.u16();

        for(int j = 0; j < 3; ++j)
            t.hasitem[j] = r.u8();
    }

    for(int i = 0; i < 256; ++i)
    {
        Shop &t = shop[i];

        for(int j = 0; j < 2; ++j)
            for(int k = 0; k < 2 - j; ++k)
                if(!t.hasitem[k])
                {
                    word a = t.item[k]; t.item[k] = t.item[k + 1]; t.item[k + 1] = a;
                    word b = t.price[k]; t.price[k] = t.price[k + 1]; t.price[k + 1] = b;
                    byte c = t.hasitem[k]; t.hasitem[k] = t.hasitem[k + 1]; t.hasitem[k + 1] = c;
                }
    }

    int infos = r.u16();

    if(infos > 256)
        r.bad = true;

    for(int i = 0; i < infos && !r.bad; ++i)
    {
        Info &t = info[i];
        std::vector<byte> nm = r.bytes(32);

        if(!r.bad)
            for(int k = 0; k < 32 && nm[k]; ++k)
                t.name[k] = nm[k];

        for(int j = 0; j < 3; ++j)
            t.str[j] = r.u16();

        for(int j = 0; j < 3; ++j)
            t.price[j] = r.u16();
    }

    for(int i = 0; i < 256; ++i)
    {
        Info &t = info[i];

        for(int j = 0; j < 2; ++j)
            for(int k = 0; k < 2 - j; ++k)
                if(t.str[k] == 0)
                {
                    word a = t.str[k]; t.str[k] = t.str[k + 1]; t.str[k + 1] = a;
                    word b = t.price[k]; t.price[k] = t.price[k + 1]; t.price[k + 1] = b;
                }
    }

    // upstream's memory from the warp rings on: 9 rings, then the palette cycles
    std::vector<byte> mem((size_t)NUM_RINGS * RING_BYTES + 256 * 3 * 3, 0);
    int rings = r.u16();

    for(int i = 0; i < rings && !r.bad; ++i)
    {
        const bool keep = i < NUM_RINGS;
        byte *ring = keep ? &mem[(size_t)i * RING_BYTES] : NULL;

        for(int j = 0; j < 9; ++j)
        {
            word d = r.u16();

            if(keep)
                ring[j * 2] = (byte)d, ring[j * 2 + 1] = (byte)(d >> 8);
        }

        for(int j = 0; j < 9; ++j)
        {
            byte sc = r.u8();

            if(keep)
                ring[18 + j] = sc;
        }

        byte size = r.u8();

        if(keep)
            ring[27] = size;
    }

    if(!palette_cycles(ctx.target, &mem[(size_t)NUM_RINGS * RING_BYTES]))
    {
        zqw_last_error = "its palettes (their cycles) aren't laid out as expected";
        return 2;
    }

    std::vector<byte> triforce = r.bytes(8);
    word endstring = r.u16();

    if(v >= 8)
        for(int i = 0; i < shops && !r.bad; ++i)
            for(int j = 0; j < 3; ++j)
                shop[i].str[j] = r.u16();

    long questmisc[32];
    memset(questmisc, 0, sizeof(questmisc));

    if(v >= 9)
    {
        for(int q = 0; q < 32; ++q)
            questmisc[q] = r.s32();

        r.skip(32 * 128);
    }

    long last_compiled = v >= 11 ? r.s32() : -1;
    word sprites[SPRMAX];
    memset(sprites, 0, sizeof(sprites));

    if(v >= 21)
        for(int q = 0; q < SPRMAX; ++q)
            sprites[q] = r.u16();
    else if(v >= 12)
        for(int q = 0; q < SPRMAX; ++q)
            sprites[q] = r.u8();

    std::vector<byte> bottles, bottle_shops;

    if(v >= 13)
    {
        bottles = r.bytes((size_t)64 * (32 + 3 * 3 + 2));
        bottle_shops = r.bytes((size_t)256 * (32 + 3 * 8));
    }
    else
    {
        Zqn255Out b;

        for(int q = 0; q < 64; ++q)
        {
            b.zeros(32);

            for(int j = 0; j < 3; ++j)
                b.c(0xFF), b.w(0);

            b.c(0);
            b.c(0);
        }

        bottles = b.b;
        bottle_shops.assign((size_t)256 * (32 + 3 * 8), 0);
    }

    word sfx[SFXMAX];
    memset(sfx, 0, sizeof(sfx));

    if(v >= 14)
        for(int q = 0; q < SFXMAX; ++q)
            sfx[q] = v < 19 ? r.u8() : r.u16();
    else
    {
        sfx[0] = 41;    // sfxBUSHGRASS: WAV_ZN1GRASSCUT
        sfx[2] = 12;    // sfxLOWHEART: WAV_ER
    }

    if(v < 15)
    {
        sfx[4] = 19;    // WAV_OUCH
        sfx[5] = 42;    // WAV_ZN1HAMMERPOST
        sfx[6] = 21;    // WAV_PLACE
        sfx[7] = 5;     // WAV_CHIME
        sfx[8] = 18;    // WAV_MSG
        sfx[9] = 18;
    }

    if(v < 16)
    {
        sfx[10] = 57;   // WAV_ZN1TAP
        sfx[11] = 58;   // WAV_ZN1TAP2
    }

    std::vector<byte> menus;    // 3.0's save menus, from the count on

    if(v >= 17)
    {
        size_t at = r.pos;
        int count = r.u8();

        if(count > 0)
        {
            zqw_last_error = "it has save menus from before 3.0's version 23 (not converted yet)";
            return 2;
        }

        r.pos = at;
        menus = r.bytes(s.data.size() - at);
    }

    if(r.bad || (v < 17 && !r.at_end()))
    {
        zqw_last_error = "its misc data isn't laid out as expected";
        return 2;
    }

    // count_shops, count_infos, count_warprings
    int nshops = 0, ninfos = 0, nrings = 0;

    for(int i = 255; i >= 0 && !nshops; --i)
    {
        const Shop &t = shop[i];

        if(t.name[0])
            nshops = i + 1;

        for(int j = 2; j >= 0 && !nshops; --j)
            if(t.hasitem[j] || t.price[j])
                nshops = i + 1;
    }

    for(int i = 255; i >= 0 && !ninfos; --i)
    {
        const Info &t = info[i];

        if(t.name[0])
            ninfos = i + 1;

        for(int j = 2; j >= 0 && !ninfos; --j)
            if(t.str[j] || t.price[j])
                ninfos = i + 1;
    }

    for(int i = 15; i >= 0 && !nrings; --i)
        for(int j = 7; j >= 0 && !nrings; --j)
        {
            const byte *ring = &mem[(size_t)i * RING_BYTES];

            if(ring[j * 2] || ring[j * 2 + 1] || ring[18 + j])
                nrings = i + 1;
        }

    Zqn255Out o;
    o.w(nshops);

    for(int i = 0; i < nshops; ++i)
    {
        const Shop &t = shop[i];
        o.put(t.name, 32);

        for(int j = 0; j < 3; ++j)
        {
            if(t30)
                o.w(t.item[j]);
            else
                o.c(t.item[j]);
        }

        for(int j = 0; j < 3; ++j)
            o.w(t.price[j]);

        for(int j = 0; j < 3; ++j)
            o.c(t.hasitem[j]);
    }

    o.w(ninfos);

    for(int i = 0; i < ninfos; ++i)
    {
        const Info &t = info[i];
        o.put(t.name, 32);

        for(int j = 0; j < 3; ++j)
            o.w(t.str[j]);

        for(int j = 0; j < 3; ++j)
            o.w(t.price[j]);
    }

    o.w(nrings);

    for(int i = 0; i < nrings; ++i)
    {
        const byte *ring = &mem[(size_t)i * RING_BYTES];

        for(int j = 0; j < 9; ++j)
            o.w(ring[j * 2] | (ring[j * 2 + 1] << 8));

        for(int j = 0; j < 9; ++j)
            o.c(ring[18 + j]);

        o.c(ring[27]);
    }

    o.put(triforce);
    o.w(endstring);

    for(int i = 0; i < nshops; ++i)
        for(int j = 0; j < 3; ++j)
            o.w(shop[i].str[j]);

    for(int q = 0; q < 32; ++q)
        o.l(questmisc[q]);

    o.zeros(32 * 128);
    o.l(last_compiled);

    for(int q = 0; q < SPRMAX; ++q)
    {
        if(t30)
            o.w(sprites[q]);
        else
            o.c(sprites[q]);
    }

    o.put(bottles);
    o.put(bottle_shops);

    for(int q = 0; q < SFXMAX; ++q)
    {
        if(t30)
            o.w(sfx[q]);
        else
            o.c(sfx[q]);
    }

    if(t30)
    {
        if(menus.empty())
        {
            o.c(0);     // no save menus
            o.c(0);     // the game over and F6 menus
            o.c(0);
        }
        else
            o.put(menus);
    }

    std::vector<byte> none;
    return t30 ? zqn255_emit(f, ID_MISC, 23, 23, none, o.b) : zqn255_emit(f, ID_MISC, 16, 7, none, o.b);
}

ZqNewerRegistration g_reg_misc(ID_MISC, write_misc, "2.55 misc data");

} // namespace

// Saving a quest made in 2.55 as 2.55.17 or 3.0 Nightly (zqn_255.h): weapon sprites (WPN).
//
// 2.55's versions 7 and 8 store a count, the sprites' 64-byte names, then each sprite: version
// 7 a 16-bit tile first (which version 7's readers skip: the 32-bit tile at the end wins), then
// misc, csets, frames, speed and type bytes, the script word and the 32-bit tile. 2.55.17's
// version 8 is that without the 16-bit tile, always 256 sprites; 3.0's version 9 is a list of
// sprites, each with its name as a counted string, up to the last one that isn't empty
// (sprite_data_buf.normalize()). Ported from both versions' readweapons / writeweapons.
// 2.55.17 reads each name up to its end (strncat), so a version 8 section is laid out again
// too: some files have leftovers of older names after a name's end, which it writes as zeros.
// A version 9 section (3.0's own, which early 3.0 Nightlies wrote with a 2.55 header; up to
// MAXSPRITES, 8192, sprites) is read as 3.0's read_single_spritedata reads it, each name a
// counted string kept byte for byte, and written again up to its last sprite that isn't empty.

#include "precompiled.h" //always first

#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_writers.h"
#include "zqn_255.h"

namespace
{

struct Sprite
{
    std::string name;
    byte misc, csets, frames, speed, type;
    word script;
    long tile;

    Sprite() : misc(0), csets(0), frames(0), speed(0), type(0), script(0), tile(0) {}

    bool empty() const
    {
        return name.empty() && !misc && !csets && !frames && !speed && !type && !script && !tile;
    }
};

int write_weapons(PACKFILE *f, const ZqNewerContext &ctx)
{
    // (version 9 only as 3.0: 2.55.17 can't hold it)
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 7 || ctx.src_version > 9 ||
       (ctx.src_version == 9 && ctx.target != ZQ_NEWER_30))
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    Zqn255In r(s.data);
    int count = r.u16();
    // MAXSPRITES in 3.0 (Mystical Harps has 294); 2.55 had 256
    const int most = s.version >= 9 ? 8192 : 256;

    if(count > most)
    {
        zqw_last_error = "it has more weapon sprites than its version holds";
        return 2;
    }

    std::vector<Sprite> w(count > 256 ? count : 256);

    if(s.version >= 9)
    {
        for(int i = 0; i < count && !r.bad; ++i)
        {
            Sprite &t = w[i];
            std::vector<byte> nm = r.bytes(r.u8());     // p_getcstr: every byte, NULs too

            if(!r.bad && !nm.empty())
                t.name.assign((const char *)&nm[0], nm.size());

            t.misc = r.u8();
            t.csets = r.u8();
            t.frames = r.u8();
            t.speed = r.u8();
            t.type = r.u8();
            t.script = r.u16();
            t.tile = r.s32();
        }
    }
    else
    {
        for(int i = 0; i < count && !r.bad; ++i)
        {
            std::vector<byte> nm = r.bytes(64);

            if(!r.bad)
                w[i].name.assign((const char *)&nm[0], strnlen((const char *)&nm[0], 64));
        }

        for(int i = 0; i < count && !r.bad; ++i)
        {
            Sprite &t = w[i];

            if(s.version < 8)
                r.u16();    // the old 16-bit tile

            t.misc = r.u8();
            t.csets = r.u8();
            t.frames = r.u8();
            t.speed = r.u8();
            t.type = r.u8();
            t.script = r.u16();
            t.tile = r.s32();
        }
    }

    if(!r.at_end())
    {
        zqw_last_error = "its weapon sprites aren't laid out as expected";
        return 2;
    }

    Zqn255Out o;

    if(ctx.target == ZQ_NEWER_255)
    {
        o.w(256);

        for(int i = 0; i < 256; ++i)
        {
            char nm[64];
            memset(nm, 0, sizeof(nm));
            memcpy(nm, w[i].name.data(), w[i].name.size() < 63 ? w[i].name.size() : 63);
            o.put(nm, 64);
        }

        for(int i = 0; i < 256; ++i)
        {
            const Sprite &t = w[i];
            o.c(t.misc); o.c(t.csets); o.c(t.frames); o.c(t.speed); o.c(t.type);
            o.w(t.script);
            o.l(t.tile);
        }

        return zqn255_emit(f, ID_WEAPONS, 8, 1, s.extra, o.b);
    }

    // sprite_data_buf.normalize(): up to the last sprite that isn't empty
    int n = 0;

    for(int i = 0; i < (int)w.size(); ++i)
        if(!w[i].empty())
            n = i + 1;

    o.w(n);

    for(int i = 0; i < n; ++i)
    {
        const Sprite &t = w[i];
        size_t k = t.name.size() < 255 ? t.name.size() : 255;
        o.c((int64_t)k);
        o.put(t.name.data(), k);
        o.c(t.misc); o.c(t.csets); o.c(t.frames); o.c(t.speed); o.c(t.type);
        o.w(t.script);
        o.l(t.tile);
    }

    return zqn255_emit(f, ID_WEAPONS, 9, 9, s.extra, o.b);
}

ZqNewerRegistration g_reg_weapons(ID_WEAPONS, write_weapons, "2.55 weapon sprites");

// ---- Link's sprites (LINK) ------------------------------------------------------------------
//
// 2.55's versions 6 to 15 to the newest, 16 (both targets), as readherosprites3 reads them:
// each sprite is a 32-bit tile, a flip byte and an extend byte; what a version doesn't store is
// cleared, except that a section older than version 7 gives the drowning sprites (in water and
// in lava) the diving ones. Version 7 added a third hold sprite (the sword) and the block from
// the frozen sprites to the medallions, 9 side-view swimming, 10 its slash/stab/pound/charge,
// 11 the hammer offsets, 12 the side-view holds, 13 side-view casting, 14 side-view drowning,
// 15 the reverse slash, 16 the lifting sprites' frame counts; from 8 on the Hero's defences
// (one byte per weapon type, 146 of them) end the section.

const size_t SPR = 6;   // tile, flip, extend

int write_link(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 6 || ctx.src_version > 15)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const int v = s.version;
    Zqn255In r(s.data);
    Zqn255Out o;
    std::vector<byte> dive;

    // walking, stabbing, slashing, floating, swimming, diving, pounding (4 each)
    for(int g = 0; g < 7; ++g)
    {
        std::vector<byte> b = r.bytes(4 * SPR);

        if(g == 5)
            dive = b;

        o.put(b);
    }

    o.put(r.bytes(SPR));    // casting

    for(int i = 0; i < 2; ++i)  // holding on land and in water
    {
        o.put(r.bytes((v > 6 ? 3 : 2) * SPR));

        if(v <= 6)
            o.zeros(SPR);
    }

    o.put(r.bytes(8 * SPR));    // jumping, charging
    o.put(r.bytes(1));          // swimming speed

    if(v > 6)
    {
        o.put(r.bytes(8 * 4 * SPR));    // frozen ... pushing

        for(int q = 0; q < 4; ++q)      // lifting, with a frame count from version 16
        {
            o.put(r.bytes(SPR));
            o.c(0);
        }

        o.put(r.bytes(3 * 4 * SPR));    // walking while lifting, stunned, stunned in water
        o.put(r.bytes(2 * 4 * SPR));    // drowning, drowning in lava
        o.put(r.bytes(9 * 4 * SPR));    // falling ... bonking
        o.put(r.bytes(3 * SPR));        // medallions
    }
    else
    {
        o.zeros(8 * 4 * SPR);
        o.zeros(4 * (SPR + 1));
        o.zeros(3 * 4 * SPR);
        o.put(dive);
        o.put(dive);
        o.zeros(9 * 4 * SPR);
        o.zeros(3 * SPR);
    }

    if(v > 8) o.put(r.bytes(4 * SPR)); else o.zeros(4 * SPR);           // side-view swimming
    if(v > 9) o.put(r.bytes(4 * 4 * SPR)); else o.zeros(4 * 4 * SPR);   // its slash, stab, pound, charge
    if(v > 10) o.put(r.bytes(4 * 4)); else o.zeros(4 * 4);              // hammer offsets
    if(v > 11) o.put(r.bytes(3 * SPR)); else o.zeros(3 * SPR);          // side-view holds
    if(v > 12) o.put(r.bytes(SPR)); else o.zeros(SPR);                  // side-view casting
    if(v > 13) o.put(r.bytes(4 * SPR)); else o.zeros(4 * SPR);          // side-view drowning
    if(v > 14) o.put(r.bytes(4 * SPR)); else o.zeros(4 * SPR);          // reverse slash
    if(v > 7) o.put(r.bytes(146)); else o.zeros(146);                   // the Hero's defences

    if(!r.at_end())
    {
        zqw_last_error = "its Link sprites aren't laid out as expected";
        return 2;
    }

    word nv, ncv;
    zqn255_newest(ID_LINKSPRITES, ctx.target, &nv, &ncv);
    return zqn255_emit(f, ID_LINKSPRITES, nv, ncv, s.extra, o.b);
}

ZqNewerRegistration g_reg_link(ID_LINKSPRITES, write_link, "2.55 Link sprites v6-15");

} // namespace

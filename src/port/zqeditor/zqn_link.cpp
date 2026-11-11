// The LINK section (Link's sprites; upstream calls him the Hero) saved as 2.55.17 or as 3.0
// Nightly, both at section version 16, for quests made in 2.53.1 or older (see zq_writers.h).
//
// Upstream reads a 2.53-era section (versions 1 to 5) with readherosprites2, as 2.53 does:
// the sprites 2.53 has come out the same, and everything upstream added is cleared, except
// that the drowning sprites copy the diving ones. A quest with no such section (2.10 and
// older) keeps the defaults both versions derive from the animation style and the weapon
// sprites, and nothing else is set at all, so the drowning sprites stay empty too. Either
// way ZQuest's own sprites are upstream's; when the user changed them, they are written as
// upstream would read them from a section of 2.53's own version.

#include "precompiled.h" //always first

#include <string.h>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "zc_custom.h"
#include "zquest.h"
#include "zq_writers.h"
#include "zqw_itemguy.h"

namespace
{

using zqw_ig::put_le;

const int V_HEROSPRITES_NEW = 16;
const int CV_HEROSPRITES_255 = 1;
const int U_wMax = 146;             // one defence per weapon type, all left at 0

struct LinkSprites
{
    int walk[4][3], stab[4][3], slash[4][3], flt[4][3], swim[4][3], dive[4][3], pound[4][3];
    int jump[4][3], charge[4][3], casting[3], hold[2][2][3];
    byte swim_speed;
};

void take(LinkSprites &s)
{
    memset(&s, 0, sizeof(s));       // compared whole, padding included
    memcpy(s.walk, walkspr, sizeof(s.walk));
    memcpy(s.stab, stabspr, sizeof(s.stab));
    memcpy(s.slash, slashspr, sizeof(s.slash));
    memcpy(s.flt, floatspr, sizeof(s.flt));
    memcpy(s.swim, swimspr, sizeof(s.swim));
    memcpy(s.dive, divespr, sizeof(s.dive));
    memcpy(s.pound, poundspr, sizeof(s.pound));
    memcpy(s.jump, jumpspr, sizeof(s.jump));
    memcpy(s.charge, chargespr, sizeof(s.charge));
    memcpy(s.casting, castingspr, sizeof(s.casting));
    memcpy(s.hold, holdspr, sizeof(s.hold));
    s.swim_speed = zinit.link_swim_speed;
}

LinkSprites g_loaded;

void snapshot()
{
    take(g_loaded);
}

ZqLoadHookRegistration g_hook(snapshot);

void put_sprite(std::vector<byte> &o, const int *spr)
{
    put_le(o, (dword)spr[spr_tile], 4);
    o.push_back((byte)spr[spr_flip]);
    o.push_back((byte)spr[spr_extend]);
}

void put_sprites(std::vector<byte> &o, const int (*spr)[3], int n)
{
    for(int i = 0; i < n; ++i)
        put_sprite(o, spr[i]);
}

void put_empty(std::vector<byte> &o, int n, int per = 6)
{
    o.insert(o.end(), (size_t)(n * per), (byte)0);
}

int zqn_link(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;     // 2.55 quests saved as 3.0 come later

    if(ctx.src_part && ctx.src_version > 5)
        return ZQW_UNSUPPORTED;

    LinkSprites s;
    take(s);

    // Upstream copies the diving sprites to the drowning ones only when it reads a section.
    bool read_section = ctx.src_part != NULL || memcmp(&s, &g_loaded, sizeof(s)) != 0;
    std::vector<byte> out;
    put_le(out, V_HEROSPRITES_NEW, 2);
    put_le(out, ctx.target == ZQ_NEWER_30 ? V_HEROSPRITES_NEW : CV_HEROSPRITES_255, 2);
    put_le(out, 0, 4);

    put_sprites(out, s.walk, 4);
    put_sprites(out, s.stab, 4);
    put_sprites(out, s.slash, 4);
    put_sprites(out, s.flt, 4);
    put_sprites(out, s.swim, 4);
    put_sprites(out, s.dive, 4);
    put_sprites(out, s.pound, 4);
    put_sprite(out, s.casting);

    // land and water: both hands as 2.53 has them, and the sword hold upstream added
    for(int i = 0; i < 2; ++i)
    {
        put_sprites(out, s.hold[i], 2);
        put_empty(out, 1);
    }

    put_sprites(out, s.jump, 4);
    put_sprites(out, s.charge, 4);
    out.push_back(s.swim_speed);

    put_empty(out, 8 * 4);          // frozen, frozen in water, on fire, on fire in water, digging,
                                    // using a rod, using a cane, pushing
    put_empty(out, 4, 7);           // lifting (with a frame count)
    put_empty(out, 3 * 4);          // walking while lifting, stunned, stunned in water

    for(int k = 0; k < 2; ++k)      // drowning, drowning in lava
    {
        if(read_section)
            put_sprites(out, s.dive, 4);
        else
            put_empty(out, 4);
    }

    put_empty(out, 9 * 4);          // falling, shocked, shocked in water, pulling the sword,
                                    // reading, 180-degree slash, Z4 slash, dashing, bonking
    put_empty(out, 3);              // medallions
    put_empty(out, 5 * 4);          // side-view swimming: plain, slash, stab, pound, charge
    put_empty(out, 4, 4);           // hammer offsets
    put_empty(out, 3);              // side-view swimming holds
    put_empty(out, 1);              // side-view swimming casting
    put_empty(out, 4);              // side-view drowning
    put_empty(out, 4);              // reverse slash
    put_empty(out, U_wMax, 1);      // the Hero's defences

    zqw_ig::set_le(out, 4, (dword)(out.size() - 8), 4);
    return zqw_ig::write_part(f, ID_LINKSPRITES, out);
}

ZqNewerRegistration g_register(ID_LINKSPRITES, zqn_link, "Link's sprites for 2.55.17 and 3.0");

} // namespace

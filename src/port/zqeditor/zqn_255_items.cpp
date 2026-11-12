// Saving a quest made in 2.55 as 3.0 Nightly (zqn_255.h): the items (ITEM).
//
// Each item read with the tape (zq_tape_item) the way 3.0's read_single_item_old (versions
// before 67) and read_single_item (67-70) read it:
// - before version 63 the item's weapon fields (imitated weapon, default defence, InitD, hit
//   box, script, size overrides, burning sprites, light radii, movement) go into its weapon
//   data; from 63 the weapon data is its own block;
// - before 71 the three scripts (action, pickup, sprite) are bare numbers, and all three get the
//   item's InitD;
// - before 62 the item and its weapon get the movement of their kind; before 63 bombs move their
//   landing / hitting-solid flags and lift settings into the weapon, the lift glove its "break
//   when landing", and burning-sprite items their ignite flag;
// then update_old_item's conversions for the item's version (from 45 on: raft speed, pickup
// flags, candle step speed, shield blocking front, second costs, flippers' dive button, spin
// and quake scroll sounds, the fire items' strength flags, the sword's beam sound; level 0
// becomes 1). 3.0's writer then writes items up to the last one that isn't a default item
// (itemsbuf.normalize()).

#include "precompiled.h" //always first

#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_writers.h"
#include "zq_tape.h"
#include "zqn_255.h"

namespace
{

enum
{
    IT_SWORD = 0, IT_ARROW = 2, IT_CANDLE = 3, IT_BAIT = 5, IT_SHIELD = 12, IT_RAFT = 14, IT_BOOK = 16, IT_FLIPPERS = 19,
    IT_DIVINEFIRE = 24, IT_BOMB = 27, IT_SBOMB = 28, IT_SPINSCROLL = 51, IT_QUAKESCROLL = 53, IT_SPINSCROLL2 = 60,
    IT_QUAKESCROLL2 = 61, IT_MISC = 66, IT_MIRROR = 273, IT_LIFTGLOVE = 279
};

const dword IF_FLAG1 = 1u << 4, IF_FLAG3 = 1u << 10, IF_FLAG5 = 1u << 12, IF_FLAG9 = 1u << 16, IF_FLAG10 = 1u << 17,
            IF_FLAG11 = 1u << 18, IF_BURNING = 1u << 30;
const dword MOVE_GRAV = 1, MOVE_PITFALL = 2;
const word WF_BREAK_WHEN_LANDING = 1u << 1, WF_STOP_WHEN_LANDING = 1u << 7, WF_STOP_WHEN_HIT_SOLID = 1u << 8,
           WF_UPDATE_IGNITE = 1u << 9;
const signed char CR_NONE = -1, CR_BOMBS = 2, CR_ARROWS = 3, CR_SBOMBS = 6;
const int WAV_SCALE_ = 21, WAV_BEAM_ = 2, WAV_ZN1CHARGE_ = 35, WAV_ZN1CHARGE2_ = 36, INT_BTN_A_ = 1;

typedef Zqn255Script Script;
typedef Zqn255Weapon Wd;

// 3.0's itemdata, as written
struct Item
{
    std::string name, display_name;
    long tile, ltm, type, power;
    byte misc_flags, csets, frames, speed, delay, level, pickup_hearts, drawlayer, pickupflag;
    dword flags;
    signed char count, cost_counter[2];
    word amount, max, playsound, usesound, usesound2, pstring, psflags, pickup_litems;
    short setmax, cost_amount[2], pickup_litem_level;
    word wpn[10];
    long misc[10];
    long weaprange, weapduration, pattern[10], duplicates;
    long hxofs, hyofs, hxsz, hysz, hzsz, xofs, yofs, wpnsprite, costtimer[2], override, tilew, tileh, pickup;
    Script sc, collect, sprite;
    dword moveflags;
    Wd wd;
    long cooldown, vp_suspend, vp_despawn;

    Item()
    {
        tile = ltm = power = 0;
        type = IT_MISC;
        level = 1;
        misc_flags = csets = frames = speed = delay = pickup_hearts = drawlayer = pickupflag = 0;
        flags = 0;
        count = CR_NONE;
        cost_counter[0] = cost_counter[1] = 0;
        amount = max = usesound = usesound2 = pstring = psflags = pickup_litems = 0;
        playsound = WAV_SCALE_;
        setmax = cost_amount[0] = cost_amount[1] = 0;
        pickup_litem_level = -1;
        memset(wpn, 0, sizeof(wpn));
        memset(misc, 0, sizeof(misc));
        weaprange = weapduration = duplicates = 0;
        memset(pattern, 0, sizeof(pattern));
        hxofs = hyofs = hxsz = hysz = hzsz = xofs = yofs = wpnsprite = override = tilew = tileh = pickup = 0;
        costtimer[0] = costtimer[1] = 0;
        moveflags = MOVE_GRAV | MOVE_PITFALL;
        cooldown = vp_suspend = vp_despawn = 0;
    }
};

void put_le(std::vector<byte> &o, uint64_t v, int n)
{
    for(int k = 0; k < n; ++k)
        o.push_back((byte)(v >> (8 * k)));
}

void put_str(std::vector<byte> &o, const std::string &s)
{
    size_t n = s.size() < 255 ? s.size() : 255;
    o.push_back((byte)n);
    o.insert(o.end(), s.begin(), s.begin() + n);
}

void put_script(std::vector<byte> &o, const Script &s)
{
    put_le(o, s.script, 2);
    put_le(o, 8, 2);

    for(int j = 0; j < 8; ++j)
        put_le(o, (dword)s.args[j], 4);

    put_le(o, 0, 2);
}

// write_single_item
void put_item(std::vector<byte> &o, const Item &t)
{
    put_str(o, t.name);
    put_le(o, (dword)t.tile, 4);
    o.push_back(t.misc_flags);
    o.push_back(t.csets);
    o.push_back(t.frames);
    o.push_back(t.speed);
    o.push_back(t.delay);
    put_le(o, (dword)t.ltm, 4);
    put_le(o, (dword)t.type, 4);
    o.push_back(t.level);
    put_le(o, (dword)t.power, 4);
    put_le(o, t.flags, 4);
    o.push_back((byte)t.count);
    put_le(o, t.amount, 2);
    put_le(o, (word)t.setmax, 2);
    put_le(o, t.max, 2);
    put_le(o, t.playsound, 2);

    for(int q = 0; q < 10; ++q)
        put_le(o, t.wpn[q], 2);

    o.push_back(t.pickup_hearts);
    put_le(o, (dword)t.misc[0], 4);
    put_le(o, (dword)t.misc[1], 4);
    put_le(o, (word)t.cost_amount[0], 2);
    put_le(o, (word)t.cost_amount[1], 2);

    for(int q = 2; q < 10; ++q)
        put_le(o, (dword)t.misc[q], 4);

    put_le(o, t.usesound, 2);
    put_le(o, t.usesound2, 2);
    put_le(o, (dword)t.weaprange, 4);
    put_le(o, (dword)t.weapduration, 4);

    for(int q = 0; q < 10; ++q)
        put_le(o, (dword)t.pattern[q], 4);

    put_le(o, (dword)t.duplicates, 4);
    o.push_back(t.drawlayer);
    const long box[7] = { t.hxofs, t.hyofs, t.hxsz, t.hysz, t.hzsz, t.xofs, t.yofs };

    for(int q = 0; q < 7; ++q)
        put_le(o, (dword)box[q], 4);

    put_le(o, (dword)t.wpnsprite, 4);
    put_le(o, (dword)t.costtimer[0], 4);
    put_le(o, (dword)t.costtimer[1], 4);
    put_le(o, (dword)t.override, 4);
    put_le(o, (dword)t.tilew, 4);
    put_le(o, (dword)t.tileh, 4);
    put_le(o, (dword)t.pickup, 4);
    put_le(o, t.pstring, 2);
    put_le(o, t.psflags, 2);
    o.push_back((byte)t.cost_counter[0]);
    o.push_back((byte)t.cost_counter[1]);
    put_script(o, t.sc);
    put_script(o, t.collect);
    put_script(o, t.sprite);
    o.push_back(t.pickupflag);
    put_str(o, t.display_name);
    put_le(o, t.pickup_litems, 2);
    put_le(o, (word)t.pickup_litem_level, 2);
    put_le(o, t.moveflags, 4);
    // write_weap_data
    const Wd &w = t.wd;
    put_le(o, 5, 2);
    put_le(o, w.flags, 2);
    put_le(o, w.moveflags, 4);
    put_le(o, w.wflags, 2);

    for(int q = 0; q < 5; ++q)
    {
        put_le(o, w.burnsprs[q], 2);
        o.push_back(w.light_rads[q]);
        put_le(o, (dword)w.light_offsets[q], 4);
    }

    o.push_back(w.glow);
    const long wv[10] = { w.override_flags, w.tilew, w.tileh, w.hxsz, w.hysz, w.hzsz, w.hxofs, w.hyofs, w.xofs, w.yofs };

    for(int q = 0; q < 10; ++q)
        put_le(o, (dword)wv[q], 4);

    put_le(o, (dword)w.step, 4);
    o.push_back(w.unblockable);
    put_le(o, (dword)w.timeout, 4);
    o.push_back(w.imitate);
    o.push_back(w.defdef);
    o.push_back(w.lift_level);
    o.push_back(w.lift_time);
    put_le(o, (dword)w.lift_height, 4);
    put_script(o, w.sc);
    put_le(o, (word)w.pierce, 2);
    put_le(o, (dword)w.bounce_mult, 4);
    put_le(o, (dword)w.bounce_add, 4);
    put_le(o, (dword)w.vp_suspend, 4);
    put_le(o, (dword)w.vp_despawn, 4);
    put_le(o, (dword)t.cooldown, 4);
    put_le(o, (dword)t.vp_suspend, 4);
    put_le(o, (dword)t.vp_despawn, 4);
}

void script_from(const TapeIndex &t, const std::string &key, Script &s)
{
    char k[64];
    snprintf(k, sizeof(k), "%s.script", key.c_str());
    s.script = (word)t.get(k);

    for(int q = 0; q < 8; ++q)
    {
        snprintf(k, sizeof(k), "%s.initd%d", key.c_str(), q);
        s.args[q] = t.get(k);
    }
}

void weapon_from(const TapeIndex &t, Wd &w)
{
    char k[40], n[24];
#define W(name) (snprintf(k, sizeof(k), "wd.%s", name), t.get(k))
#define WD(name, dflt) (snprintf(k, sizeof(k), "wd.%s", name), t.find(k) ? t.get(k) : (dflt))
    w.flags = (word)W("flags");
    w.moveflags = (dword)W("moveflags");
    w.wflags = (word)W("wflags");

    for(int q = 0; q < 5; ++q)
    {
        snprintf(n, sizeof(n), "burnspr%d", q);
        w.burnsprs[q] = (word)W(n);
        snprintf(n, sizeof(n), "lightrad%d", q);
        w.light_rads[q] = (byte)W(n);
        snprintf(n, sizeof(n), "lightofs%d", q);
        w.light_offsets[q] = W(n);
    }

    w.glow = (byte)W("glow");
    w.override_flags = W("override");
    w.tilew = W("tilew"); w.tileh = W("tileh"); w.hxsz = W("hxsz"); w.hysz = W("hysz"); w.hzsz = W("hzsz");
    w.hxofs = W("hxofs"); w.hyofs = W("hyofs"); w.xofs = W("xofs"); w.yofs = W("yofs");
    w.step = W("step");
    w.unblockable = (byte)W("unblockable");
    w.timeout = W("timeout");
    w.imitate = (byte)W("imitate");
    w.defdef = (byte)W("defdef");
    w.lift_level = (byte)W("liftlvl");
    w.lift_time = (byte)W("lifttime");
    w.lift_height = W("lifthei");
    script_from(t, t.find("wd.script") ? "wd" : "wd.sc", w.sc);
    w.pierce = (short)W("pierce");
    w.bounce_mult = WD("bouncemult", w.bounce_mult);
    w.bounce_add = WD("bounceadd", w.bounce_add);
    w.vp_suspend = WD("vpsuspend", w.vp_suspend);
    w.vp_despawn = WD("vpdespawn", w.vp_despawn);
#undef W
#undef WD
}

// read_single_item_old / read_single_item from the tape
void read_item(const TapeIndex &t, int s, Item &it)
{
    char k[32];
#define T(key) t.get(key)
    it.tile = T("tile");
    it.misc_flags = (byte)T("misc");
    it.csets = (byte)T("csets");
    it.frames = (byte)T("frames");
    it.speed = (byte)T("speed");
    it.delay = (byte)T("delay");
    it.ltm = T("ltm");
    it.type = T("family");
    it.level = (byte)T("fam_type");
    it.power = T("power");
    it.flags = (dword)T("flags");
    it.count = (signed char)T("count");
    it.amount = (word)T("amount");
    it.setmax = (short)T("setmax");
    it.max = (word)T("max");
    it.playsound = (word)T("playsound");

    if(s < 71)
    {
        it.sc.script = (word)T("script");
        it.collect.script = (word)T("collect_script");
        it.sprite.script = (word)T("sprite_script");

        for(int j = 0; j < 8; ++j)
        {
            snprintf(k, sizeof(k), "initd%d", j);
            it.sc.args[j] = it.collect.args[j] = it.sprite.args[j] = T(k);
        }
    }
    else
    {
        script_from(t, "sc.action", it.sc);
        script_from(t, "sc.pickup", it.collect);
        script_from(t, "sc.sprite", it.sprite);
    }

    for(int j = 0; j < 10; ++j)
    {
        snprintf(k, sizeof(k), "wpn%d", j);
        it.wpn[j] = (word)T(k);
        snprintf(k, sizeof(k), "misc%d", j + 1);
        it.misc[j] = T(k);
    }

    it.pickup_hearts = (byte)T("pickup_hearts");
    it.cost_amount[0] = (short)T("cost0");
    it.cost_amount[1] = (short)T("cost1");
    it.usesound = (word)T("usesound");
    it.usesound2 = (word)T("usesound2");

    if(s < 50 && it.type == IT_MIRROR)
    {
        it.misc[1] = it.misc[0];
        it.usesound2 = it.usesound;
    }

    it.weaprange = T("weaprange");
    it.weapduration = T("weapduration");

    for(int j = 0; j < 10; ++j)
    {
        snprintf(k, sizeof(k), "pattern%d", j);
        it.pattern[j] = T(k);
    }

    it.duplicates = T("duplicates");
    it.drawlayer = (byte)T("drawlayer");
    it.hxofs = T("hxofs"); it.hyofs = T("hyofs"); it.hxsz = T("hxsz"); it.hysz = T("hysz"); it.hzsz = T("hzsz");
    it.xofs = T("xofs"); it.yofs = T("yofs");
    it.wpnsprite = T("wpnsprite");
    it.costtimer[0] = T("costtimer0");
    it.costtimer[1] = T("costtimer1");
    it.override = T("override");
    it.tilew = T("tilew");
    it.tileh = T("tileh");
    it.pickup = T("pickup");
    it.pstring = (word)T("pstring");
    it.psflags = (word)T("psflags");
    it.cost_counter[0] = (signed char)T("costctr0");
    it.cost_counter[1] = (signed char)T("costctr1");
    it.pickupflag = (byte)T("pickupflag");
    const TapeField *dn = t.find("display_name");

    if(dn)
        it.display_name.assign((const char *)(dn->bytes.empty() ? NULL : &dn->bytes[0]), dn->bytes.size());

    if(s >= 60)
    {
        it.pickup_litems = (word)T("pickup_litems");
        long lvl = (short)T("pickup_litem_level");
        it.pickup_litem_level = (short)(lvl < -1 ? -1 : lvl > 511 ? 511 : lvl);
    }

    if(s < 63)
    {
        Wd &w = it.wd;
        w.imitate = (byte)T("w.imitate");
        w.defdef = (byte)T("w.defdef");

        for(int j = 0; j < 8; ++j)
        {
            snprintf(k, sizeof(k), "w.initd%d", j);
            w.sc.args[j] = T(k);
        }

        w.hxofs = T("w.hxofs"); w.hyofs = T("w.hyofs"); w.hxsz = T("w.hxsz"); w.hysz = T("w.hysz");
        w.hzsz = T("w.hzsz"); w.xofs = T("w.xofs"); w.yofs = T("w.yofs");
        w.sc.script = (word)T("w.script");
        w.override_flags = T("w.override");
        w.tilew = T("w.tilew");
        w.tileh = T("w.tileh");

        for(int j = 0; j < 5; ++j)
        {
            snprintf(k, sizeof(k), "burnspr%d", j);
            w.burnsprs[j] = (word)T(k);
            snprintf(k, sizeof(k), "lightrad%d", j);
            w.light_rads[j] = (byte)T(k);
        }
    }

    if(s >= 62)
    {
        it.moveflags = (dword)T("moveflags");

        if(s < 63)
            it.wd.moveflags = (dword)T("w.moveflags");
    }
    else
    {
        it.moveflags = MOVE_GRAV | MOVE_PITFALL;

        switch(it.type)
        {
        case IT_DIVINEFIRE:
            if(!(it.flags & IF_FLAG3))
            {
                it.wd.moveflags = 0;
                break;
            }

            // fall through
        case IT_BOMB: case IT_SBOMB: case IT_BAIT: case IT_LIFTGLOVE: case IT_CANDLE: case IT_BOOK:
            it.wd.moveflags = MOVE_GRAV | MOVE_PITFALL;
            break;

        default:
            it.wd.moveflags = 0;
            break;
        }
    }

    if(s >= 63)
        weapon_from(t, it.wd);
    else
    {
        if(it.flags & IF_BURNING)
            it.wd.wflags |= WF_UPDATE_IGNITE;
        else
            it.wd.wflags &= ~WF_UPDATE_IGNITE;

        switch(it.type)
        {
        case IT_LIFTGLOVE:
            it.wd.wflags = WF_BREAK_WHEN_LANDING;
            break;

        case IT_BOMB: case IT_SBOMB:
            if(it.flags & IF_FLAG3)
                it.wd.wflags |= WF_STOP_WHEN_LANDING;

            if(it.flags & IF_FLAG5)
                it.wd.wflags |= WF_STOP_WHEN_HIT_SOLID;

            it.flags &= ~(IF_FLAG3 | IF_FLAG5);

            if(it.misc[3])
            {
                it.wd.lift_level = (byte)it.misc[3];
                it.wd.lift_time = (byte)it.misc[4];
                it.wd.lift_height = (long)((unsigned long)it.misc[5] * 10000UL);
                it.misc[3] = it.misc[4] = it.misc[5] = 0;
            }

            break;
        }
    }

    it.cooldown = T("cooldown");

    if(t.find("vpsuspend"))
    {
        it.vp_suspend = T("vpsuspend");
        it.vp_despawn = T("vpdespawn");
    }
#undef T
}

// update_old_item, for the versions from 45 on
void update_item(Item &it, int s)
{
    if(s < 67)
    {
        if(s < 46 && it.type == IT_RAFT)
            it.misc[0] = 1;

        if(s < 47)
            it.pickupflag = 0;

        if(s < 51 && it.type == IT_CANDLE)
            it.misc[3] = 50;

        if(s < 52 && it.type == IT_SHIELD)
            it.flags |= IF_FLAG1;

        if(s < 53)
        {
            switch(it.type)
            {
            case IT_ARROW: it.cost_counter[1] = CR_ARROWS; it.cost_amount[1] = 1; break;
            case IT_BOMB: it.cost_counter[1] = CR_BOMBS; it.cost_amount[1] = 1; break;
            case IT_SBOMB: it.cost_counter[1] = CR_SBOMBS; it.cost_amount[1] = 1; break;
            default: it.cost_counter[1] = CR_NONE; it.cost_amount[1] = 0; break;
            }

            it.costtimer[1] = 0;
        }

        if(s < 54 && it.type == IT_FLIPPERS)
            it.misc[2] = INT_BTN_A_;

        if(s < 55)
        {
            if(it.type == IT_SPINSCROLL || it.type == IT_QUAKESCROLL)
                it.usesound2 = WAV_ZN1CHARGE_;
            else if(it.type == IT_SPINSCROLL2 || it.type == IT_QUAKESCROLL2)
                it.usesound2 = WAV_ZN1CHARGE2_;
        }

        if(s < 56)
        {
            switch(it.type)
            {
            case IT_DIVINEFIRE:
                it.flags &= ~(IF_FLAG9 | IF_FLAG10);    // made in 2.55 or later: neither strong nor magic
                it.flags |= IF_FLAG11;
                break;

            case IT_CANDLE:
                if(it.level > 1) it.flags |= IF_FLAG9; else it.flags &= ~IF_FLAG9;
                it.flags &= ~(IF_FLAG10 | IF_FLAG11);
                break;

            case IT_BOOK:
                it.flags |= IF_FLAG9 | IF_FLAG10;
                it.flags &= ~IF_FLAG11;
                break;
            }
        }

        if(s < 61 && it.type == IT_SWORD)
            it.usesound2 = WAV_BEAM_;
    }

    if(it.level == 0)
        it.level = 1;
}

// Every item of the quest's own-version section as 3.0 holds it after the load. 0: not a
// section these take (none set), 1: read, 2: refused (zqw_last_error says why).
int read_items(const ZqNewerContext &ctx, std::vector<Item> &items)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 45 || ctx.src_version > 70 || ctx.target != ZQ_NEWER_30)
        return 0;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const int v = s.version;
    Zqn255In r(s.data);
    const int count = r.u16();

    if(count > 512 || (v < 67 && s.data.size() < 2 + 64 * (size_t)count))
    {
        zqw_last_error = "its items aren't laid out as expected";
        return 2;
    }

    size_t at = 2 + (v < 67 ? 64 * (size_t)count : 0);
    items.assign(count, Item());

    for(int i = 0; i < count; ++i)
    {
        Tape tape;
        TapeParser p(&s.data[0], s.data.size(), at, &tape);

        if(!zq_tape_item(p, v) || p.bad)
        {
            zqw_last_error = "an item of its item section couldn't be read";
            return 2;
        }

        at = p.pos;
        const TapeIndex t(tape);
        Item &it = items[i];

        if(v < 67)
        {
            char nm[64];
            memcpy(nm, &s.data[2 + 64 * (size_t)i], 64);
            nm[63] = 0;
            it.name = nm;
        }
        else
        {
            const TapeField *nf = t.find("name");

            if(nf)
                it.name.assign((const char *)(nf->bytes.empty() ? NULL : &nf->bytes[0]), nf->bytes.size());
        }

        read_item(t, v, it);
        update_item(it, v);
    }

    if(at != s.data.size())
    {
        zqw_last_error = "its item section has bytes past its last item";
        return 2;
    }

    return 1;
}

int write_items(PACKFILE *f, const ZqNewerContext &ctx)
{
    std::vector<Item> items;
    const int rc = read_items(ctx, items);

    if(rc != 1)
        return rc == 0 ? ZQW_UNSUPPORTED : 2;

    const int count = (int)items.size();

    // itemsbuf.normalize(): items equal to a fresh one drop off the end
    std::vector<byte> blank;
    put_item(blank, Item());
    std::vector<std::vector<byte> > recs(count);
    int n = 0;

    for(int i = 0; i < count; ++i)
    {
        put_item(recs[i], items[i]);

        if(recs[i] != blank)
            n = i + 1;
    }

    std::vector<byte> body;
    put_le(body, (dword)n, 2);

    for(int i = 0; i < n; ++i)
        body.insert(body.end(), recs[i].begin(), recs[i].end());

    std::vector<byte> none;
    return zqn255_emit(f, ID_ITEMS, 71, 71, none, body);
}

ZqNewerRegistration g_reg_items(ID_ITEMS, write_items, "2.55 items as 3.0");

} // namespace

bool zqn255_items_loaded(int target, std::vector<Zqn255ItemWeapon> &out)
{
    ZqNewerContext c;
    c.target = target;
    c.src_zelda_version = 0x255;
    c.src_build = 0;
    c.src_part = zq_find_part(ID_ITEMS);
    c.src_version = c.src_cversion = 0;

    if(c.src_part)
        zq_part_versions(*c.src_part, &c.src_version, &c.src_cversion);

    std::vector<Item> items;

    if(read_items(c, items) != 1)
        return false;

    out.resize(items.size());

    for(size_t i = 0; i < items.size(); ++i)
    {
        out[i].type = items[i].type;
        out[i].wd = items[i].wd;
    }

    return true;
}

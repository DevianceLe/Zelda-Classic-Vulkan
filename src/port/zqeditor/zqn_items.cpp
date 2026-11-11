// The ITEM and WPN sections of a quest saved as 2.55.17 or 3.0 Nightly (the newer-version
// save, zq_writers.h), for quests made in 2.53.1 or older.
//
// Upstream's loader rebuilds every old item as it reads it: each record starts from the
// item's defaults, takes what the old format stores, and then goes through the conversions
// upstream made since (item families taking over what quest rules used to do, use and cost
// fields that 2.53 lacks, the clean-up of greyed-out values in 2.53 quests, and so on). 2.53's
// own load does the older part of the same work, but not always the same way (a book's use
// sound, a 1.9x heart container's maximum, the names of three renamed items), and it can't
// hold what came later. So these writers don't start from ZQuest's converted items: they read
// the original part's records the way upstream does and convert them as upstream does, for
// every record the user didn't change. A record the user changed is read the same way from
// what the quest's own-version save writes for it now (its unchanged fields as stored, the
// changed ones ZQuest's), so the result is what upstream makes of the quest saved in its own
// version and opened there. Where that save can't hold the change (a field the old format
// lacks), the record is ZQuest's, taken as a 2.53.1 record would be (its conversions are
// already in it) and given only the newer ones.
//
// The conversions read quest rules as upstream holds them when the items are read (the
// rules as stored, with upstream's own load-time changes) and the rules as stored in the
// file (upstream's "deprecated" copy), both from zqn_upstream_rules (zqn_rules.h); a rule the
// user changed since the load is read as the quest's own-version save stores it. Init data
// older than version 15 then puts its own values into the fairies, potions, swords and
// hookshots, as it does in 2.53: those come from ZQuest's items, where 2.53's load put the
// same values.
//
// Weapon sprites: 2.53's load converts the stored ones exactly as upstream does, so their data
// is ZQuest's; sprites past the ones the file stores are empty upstream, apart from the ones
// its conversions copy into. The names differ (renamed sprites, and 3.0 copying names along
// with the sprites it copies). No rule takes part. 2.55.17 leaves each stored sprite's script
// field uninitialised, where these files hold 0 (see UpWpn).

#include "precompiled.h" //always first

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "sfx.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqw_itemguy.h"
#include "zqn_rules.h"

extern itemdata *itemsbuf;
extern wpndata *wpnsbuf;
extern char *item_string[ITEMCNT];
extern char *weapon_string[WPNCNT];
extern byte quest_rules[QUESTRULES_SIZE];
extern itemdata default_items[iLast];
extern const char *old_item_string[iLast];
extern const char *old_weapon_string[wLast];

namespace
{

using namespace zqw_ig;

// ---- values upstream has that 2.53 doesn't name ---------------------------------------

const dword UF_FLAG6 = 0x00002000, UF_FLAG7 = 0x00004000, UF_FLAG8 = 0x00008000;
const dword UF_FLAG9 = 0x00010000, UF_FLAG10 = 0x00020000, UF_FLAG11 = 0x00040000;
const signed char CR_NONE = -1, CR_BOMBS = 2, CR_ARROWS = 3, CR_SBOMBS = 6;
const long MOVE_GRAV = 1, MOVE_PITFALL = 2;
const word WFLAG_STOP_WHEN_LANDING = 128, WFLAG_STOP_WHEN_HIT_SOLID = 256;
const int WAV_ZN1CHARGE_U = 35, WAV_ZN1CHARGE2_U = 36, WAV_BEAM_U = 2, INT_BTN_A_U = 1;

enum { T255 = ZQ_NEWER_255, T30 = ZQ_NEWER_30 };

// Names upstream renamed (old_item_string / old_weapon_string, the same in 2.55.17 and 3.0).
const char *up_item_name(int i)
{
    switch(i)
    {
    case iDinsFire: return "Divine Fire";
    case iFaroresWind: return "Divine Escape";
    case iNayrusLove: return "Divine Protection";
    }

    return old_item_string[i];
}

const char *up_weapon_name(int i)
{
    switch(i)
    {
    case wDINSFIRE1A: return "Divine Fire (Rising)";
    case wDINSFIRE1B: return "Divine Fire (Falling)";
    case wDINSFIRES1A: return "Divine Fire Trail (Rising)";
    case wDINSFIRES1B: return "Divine Fire Trail (Falling)";
    case wNAYRUSLOVE1A: return "Divine Protection (Left)";
    case wNAYRUSLOVE1B: return "Divine Protection (Left, Returning)";
    case wNAYRUSLOVES1A: return "Divine Protection Trail (Left)";
    case wNAYRUSLOVES1B: return "Divine Protection Trail (Left, Returning)";
    case wNAYRUSLOVE2A: return "Divine Protection (Right)";
    case wNAYRUSLOVE2B: return "Divine Protection (Right, Returning)";
    case wNAYRUSLOVES2A: return "Divine Protection Trail (Right)";
    case wNAYRUSLOVES2B: return "Divine Protection Trail (Right, Returning)";
    case iwNayrusLoveShieldFront: return "Divine Protection Shield (Front)";
    case iwNayrusLoveShieldBack: return "Divine Protection Shield (Back)";
    }

    return old_weapon_string[i];
}

// reset_itemname / reset_weaponname
std::string reset_name(int i, bool item)
{
    if(item ? i < iLast : i < wLast)
        return item ? up_item_name(i) : up_weapon_name(i);

    char buf[16];
    sprintf(buf, "zz%03d", i);
    return buf;
}

// ---- what was loaded ------------------------------------------------------------------

itemdata g_items[MAXITEMS];
char g_item_names[ITEMCNT][64];
wpndata g_wpns[MAXWPNS];
char g_wpn_names[WPNCNT][64];

void snapshot()
{
    memcpy(g_items, itemsbuf, sizeof(g_items));
    memcpy(g_wpns, wpnsbuf, sizeof(g_wpns));

    for(int i = 0; i < ITEMCNT; ++i)
        memcpy(g_item_names[i], item_string[i], 64);

    for(int i = 0; i < WPNCNT; ++i)
        memcpy(g_wpn_names[i], weapon_string[i], 64);
}

ZqLoadHookRegistration g_hook(snapshot);

bool same_item(const itemdata &a, const itemdata &b)
{
#define CMP(x) if(a.x != b.x) return false;
    CMP(tile) CMP(misc) CMP(csets) CMP(frames) CMP(speed) CMP(delay) CMP(ltm)
    CMP(family) CMP(fam_type) CMP(power) CMP(flags) CMP(script) CMP(count) CMP(amount)
    CMP(setmax) CMP(max) CMP(playsound) CMP(collect_script)
    CMP(wpn) CMP(wpn2) CMP(wpn3) CMP(wpn4) CMP(wpn5) CMP(wpn6) CMP(wpn7) CMP(wpn8) CMP(wpn9) CMP(wpn10)
    CMP(pickup_hearts) CMP(misc1) CMP(misc2) CMP(misc3) CMP(misc4) CMP(misc5) CMP(misc6)
    CMP(misc7) CMP(misc8) CMP(misc9) CMP(misc10) CMP(magic) CMP(usesound)
#undef CMP

    for(int j = 0; j < 8; ++j)
        if(a.initiald[j] != b.initiald[j])
            return false;

    return a.initiala[0] == b.initiala[0] && a.initiala[1] == b.initiala[1];
}

// ZQN_ITEMS_ALL_CHANGED (for the checks) sends every record down the path of a record the user
// changed:
//   =1      every stored item and sprite counts as changed: an item is read from the quest's
//           own-version save of the items (or, for a quest with no item section, or when that
//           save can't hold them, merged field by field over upstream's conversion of the
//           original), a sprite is taken from ZQuest's data. Each field still takes ZQuest's
//           value only where it differs from the load, so with nothing really changed the result
//           has to be what the unchanged quest gives.
//   =whole  every field of every item and every name is ZQuest's: each record is taken whole as
//           a 2.53.1 record would be, as for a changed family, level or flags the own-version
//           save can't hold. Where the quest's format doesn't store a field, that is 2.53's own
//           default for it, which differs from upstream's by design.
enum { FORCE_NONE, FORCE_ROUTE, FORCE_WHOLE };

int force_mode()
{
    static int mode = -1;

    if(mode < 0)
    {
        const char *e = getenv("ZQN_ITEMS_ALL_CHANGED");
        mode = !e || !*e ? FORCE_NONE : !strcmp(e, "whole") ? FORCE_WHOLE : FORCE_ROUTE;
    }

    return mode;
}

// every field of every record counts as changed
bool all_changed()
{
    return force_mode() == FORCE_WHOLE;
}

// whether the item's record goes the way of a changed one
bool item_changed(int i)
{
    return force_mode() != FORCE_NONE || !same_item(itemsbuf[i], g_items[i]);
}

// whether ZQuest's data for the item differs from the load (or counts as if)
bool item_value_changed(int i)
{
    return all_changed() || !same_item(itemsbuf[i], g_items[i]);
}

bool item_name_changed(int i)
{
    return all_changed() || strcmp(item_string[i], g_item_names[i]) != 0;
}

bool wpn_changed(int i)
{
    const wpndata &a = wpnsbuf[i], &b = g_wpns[i];
    return force_mode() != FORCE_NONE || a.tile != b.tile || a.misc != b.misc || a.csets != b.csets || a.frames != b.frames || a.speed != b.speed || a.type != b.type;
}

bool wpn_name_changed(int i)
{
    return all_changed() || strcmp(weapon_string[i], g_wpn_names[i]) != 0;
}

// ---- reading the original part ----------------------------------------------------------

struct In
{
    const std::vector<byte> &b;
    size_t pos;
    bool bad;

    In(const std::vector<byte> &bytes, size_t at) : b(bytes), pos(at), bad(false) {}

    dword get(int n)
    {
        if(pos + n > b.size())
        {
            bad = true;
            pos += n;
            return 0;
        }

        dword v = 0;

        for(int k = 0; k < n; ++k)
            v |= (dword)b[pos + k] << (8 * k);

        pos += n;
        return v;
    }

    byte u8() { return (byte)get(1); }
    word u16() { return (word)get(2); }
    long s32() { return (long)get(4); }

    std::string name(size_t max)
    {
        std::string s;

        for(size_t k = 0; k < 64; ++k)
        {
            byte c = pos + k < b.size() ? b[pos + k] : 0;

            if(!c)
                break;

            if(s.size() < max)
                s += (char)c;
        }

        if(pos + 64 > b.size())
            bad = true;

        pos += 64;
        return s;
    }
};

// ---- the rules upstream reads items with ----------------------------------------------

// The rules as upstream holds them while it reads the items, from the one place that knows
// upstream's rule changes at load (zqn_rules.h), for the quest as it is now: a rule the user
// changed since the load is read as the quest's own-version save stores it. Every rule the
// item conversions test is one of the first 160, numbered alike in 2.53 and upstream.
struct Rules
{
    ZqnRules up;

    // deprecated_rules: the bits as the file stores them, which the old-item conversions test
    bool dep(int bit) const
    {
        return zqn_rule(up.deprecated, bit);
    }

    // get_qr while upstream reads the items: the stored bits after readrules' changes. Of the
    // readers between the rules and the items only the strings reader changes a rule, and not
    // one these conversions test. (The lens hint rules that old init data clears, before 1.92
    // build 129, are cleared only after the items are read, so they are as stored here.)
    bool qr(int bit) const
    {
        return zqn_rule(up.after_readrules, bit);
    }
};

bool load_rules(Rules &r, const ZqNewerContext &ctx)
{
    return zqn_upstream_rules(ctx.target, r.up);
}

// ---- upstream's item record ---------------------------------------------------------------

// The fields an old quest's item has in upstream after the load; everything else either
// target holds stays at its default and is written as such.
struct UpItem
{
    std::string name;
    long tile;
    byte misc_flags, csets, frames, speed, delay;
    long ltm;
    long type;
    byte level;
    long power;
    dword flags;
    word script, collect_script;
    signed char count;
    word amount;
    short setmax;
    word max, playsound;
    long initd[8];
    byte inita[2];
    word wpn[10];
    byte pickup_hearts;
    long misc[10];
    short cost_amount[2];
    word usesound, usesound2;
    long magiccosttimer[2];
    signed char cost_counter[2];
    byte pickupflag;
    bool present;               // 3.0 only: the item is in upstream's list at all
    bool collect_args;          // 3.0: the collect script got the item's InitD
    long moveflags, wd_moveflags;   // 3.0
    word wd_wflags;
    byte lift_level, lift_time;
    long lift_height;           // a zfix: the value times 10000

    void clear()
    {
        name.clear();
        tile = ltm = power = type = 0;
        misc_flags = csets = frames = speed = delay = level = 0;
        flags = 0;
        script = collect_script = amount = max = playsound = usesound = usesound2 = 0;
        count = 0;
        setmax = 0;
        memset(initd, 0, sizeof(initd));
        memset(inita, 0, sizeof(inita));
        memset(wpn, 0, sizeof(wpn));
        pickup_hearts = 0;
        memset(misc, 0, sizeof(misc));
        memset(cost_amount, 0, sizeof(cost_amount));
        memset(magiccosttimer, 0, sizeof(magiccosttimer));
        memset(cost_counter, 0, sizeof(cost_counter));
        pickupflag = 0;
        present = true;
        collect_args = false;
        moveflags = wd_moveflags = 0;
        wd_wflags = 0;
        lift_level = 0;
        lift_time = 16;
        lift_height = 80000;
    }

    // itemdata(): 3.0's member defaults (2.55 zeroes the record, but its readers set or
    // later fix the same fields)
    void fresh()
    {
        clear();
        type = itype_misc;
        level = 1;
        count = -1;
        playsound = WAV_SCALE;
        moveflags = MOVE_GRAV | MOVE_PITFALL;
    }
};

// The rows default_items spells out; the rest of its iLast rows (the custom items) are left
// to the compiler: zeros in 2.53 and 2.55.17, a fresh record in 3.0.
const int DEFAULT_ITEM_ROWS = 123;

// reset_itembuf: everything but the tile, misc, csets, frames, speed, delay and ltm (and the
// name) from the item's defaults. upstream's default_items is 2.53's apart from the shields'
// block flags; its other fields are the record's own defaults.
void reset_itembuf(UpItem &t, int id, int target)
{
    if(id >= iLast)
        return;

    const itemdata &d = default_items[id];
    UpItem keep = t;
    t.fresh();
    t.name = keep.name;
    t.tile = (word)keep.tile;
    t.misc_flags = keep.misc_flags;
    t.csets = keep.csets;
    t.frames = keep.frames;
    t.speed = keep.speed;
    t.delay = keep.delay;
    t.ltm = keep.ltm;
    t.present = keep.present;

    if(id >= DEFAULT_ITEM_ROWS && target == T30)
        return;

    t.type = d.family;
    t.level = d.fam_type;
    t.power = d.power;
    t.flags = d.flags;
    t.script = d.script;
    t.count = d.count;
    t.amount = d.amount;
    t.collect_script = d.collect_script;
    t.setmax = d.setmax;
    t.max = d.max;
    t.playsound = d.playsound;

    for(int j = 0; j < 8; ++j)
        t.initd[j] = d.initiald[j];

    t.inita[0] = d.initiala[0];
    t.inita[1] = d.initiala[1];
    const byte w[10] = { d.wpn, d.wpn2, d.wpn3, d.wpn4, d.wpn5, d.wpn6, d.wpn7, d.wpn8, d.wpn9, d.wpn10 };

    for(int j = 0; j < 10; ++j)
        t.wpn[j] = w[j];

    t.pickup_hearts = d.pickup_hearts;
    const long m[10] = { d.misc1, d.misc2, d.misc3, d.misc4, d.misc5, d.misc6, d.misc7, d.misc8, d.misc9, d.misc10 };

    for(int j = 0; j < 10; ++j)
        t.misc[j] = m[j];

    t.cost_amount[0] = d.magic;
    t.usesound = d.usesound;

    // the shields block every script weapon type upstream, where 2.53 has one flag for them
    if(id == iShield)
        t.misc[0] = 0xFFC3F;
    else if(id == iMShield)
    {
        t.misc[0] = 0xFFE7F;
        t.misc[1] = 0xFFE28;
    }

    // init_def_items
    if(id == iBombs)
        t.cost_counter[1] = CR_BOMBS;
    else if(id == iArrow || id == iSArrow || id == iGArrow)
        t.cost_counter[1] = CR_ARROWS;
    else if(id == iSBomb)
        t.cost_counter[1] = CR_SBOMBS;
}

// What 2.53's item record holds, as a 2.53.1 file stores it (s_version 25).
void from_253(UpItem &t, const itemdata &d)
{
    t.tile = d.tile;
    t.misc_flags = d.misc;
    t.csets = d.csets;
    t.frames = d.frames;
    t.speed = d.speed;
    t.delay = d.delay;
    t.ltm = d.ltm;
    t.type = d.family;
    t.level = d.fam_type;
    t.power = d.power;
    t.flags = d.flags;
    t.script = d.script;
    t.count = d.count;
    t.amount = d.amount;
    t.collect_script = d.collect_script;
    t.setmax = d.setmax;
    t.max = d.max;
    t.playsound = d.playsound;

    for(int j = 0; j < 8; ++j)
        t.initd[j] = d.initiald[j];

    t.inita[0] = d.initiala[0];
    t.inita[1] = d.initiala[1];
    const byte w[10] = { d.wpn, d.wpn2, d.wpn3, d.wpn4, d.wpn5, d.wpn6, d.wpn7, d.wpn8, d.wpn9, d.wpn10 };

    for(int j = 0; j < 10; ++j)
        t.wpn[j] = w[j];

    t.pickup_hearts = d.pickup_hearts;
    const long m[10] = { d.misc1, d.misc2, d.misc3, d.misc4, d.misc5, d.misc6, d.misc7, d.misc8, d.misc9, d.misc10 };

    for(int j = 0; j < 10; ++j)
        t.misc[j] = m[j];

    t.cost_amount[0] = d.magic;
    t.usesound = d.usesound;
    t.usesound2 = 0;
    t.collect_args = true;
}

// read_single_item_old's record, s_version > 1, for a quest of 1.93 or newer.
void read_record(In &r, UpItem &t, int s)
{
    t.tile = r.u16();
    t.misc_flags = r.u8();
    t.csets = r.u8();
    t.frames = r.u8();
    t.speed = r.u8();
    t.delay = r.u8();
    t.ltm = r.s32();
    t.type = r.u8();

    if(s < 16 && t.type == 0xFF)
        t.type = itype_misc;

    t.level = r.u8();

    if(s > 5)
    {
        t.power = r.u8();
        t.flags = r.u16();
    }
    else if(r.u8())
        t.flags |= ITEM_GAMEDATA;

    t.script = r.u16();

    if(s <= 3 && t.script > NUMSCRIPTITEM)
        t.script = 0;

    t.count = (signed char)r.u8();
    t.amount = r.u16();
    t.collect_script = r.u16();

    if(s <= 3 && t.collect_script > NUMSCRIPTITEM)
        t.collect_script = 0;

    t.setmax = (short)r.u16();
    t.max = r.u16();
    t.playsound = r.u8();

    for(int j = 0; j < 8; ++j)
        t.initd[j] = r.s32();

    t.collect_args = true;
    t.inita[0] = r.u8();
    t.inita[1] = r.u8();

    if(s <= 4)
        return;

    if(s > 5)
    {
        int n = s >= 15 ? 10 : 4;

        for(int q = 0; q < n; ++q)
            t.wpn[q] = r.u8();

        t.pickup_hearts = r.u8();

        if(s < 15)
        {
            t.misc[0] = r.u16();
            t.misc[1] = r.u16();
        }
        else
        {
            t.misc[0] = r.s32();
            t.misc[1] = r.s32();

            // version 24: shields could block script weapons before
            if(s < 24 && t.type == itype_shield)
                t.misc[0] |= shSCRIPT;
        }

        t.cost_amount[0] = r.u8();
    }
    else if(r.u8())
        t.flags |= ITEM_EDIBLE;

    if(s >= 12)
    {
        if(s < 15)
        {
            t.misc[2] = r.u16();
            t.misc[3] = r.u16();
        }
        else
            for(int q = 2; q < 10; ++q)
                t.misc[q] = r.s32();

        t.usesound = r.u8();
        t.usesound2 = 0;
    }
}

// The part of read_single_item_old after the stored fields (3.0 only).
void read_tail_30(UpItem &t)
{
    // v62: the item's and its weapon's movement
    t.moveflags = MOVE_GRAV | MOVE_PITFALL;

    switch(t.type)
    {
    case itype_dinsfire:
        if(!(t.flags & ITEM_FLAG3))
        {
            t.wd_moveflags = 0;
            break;
        }

        // fall through
    case itype_bomb:
    case itype_sbomb:
    case itype_bait:
    case itype_candle:
    case itype_book:
        t.wd_moveflags = MOVE_GRAV | MOVE_PITFALL;
        break;

    default:
        t.wd_moveflags = 0;
        break;
    }

    // v63: bomb flags moved into the weapon
    if(t.type == itype_bomb || t.type == itype_sbomb)
    {
        if(t.flags & ITEM_FLAG3)
            t.wd_wflags |= WFLAG_STOP_WHEN_LANDING;

        if(t.flags & ITEM_FLAG5)
            t.wd_wflags |= WFLAG_STOP_WHEN_HIT_SOLID;

        t.flags &= ~(ITEM_FLAG3 | ITEM_FLAG5);

        if(t.misc[3])
        {
            t.lift_level = (byte)t.misc[3];
            t.lift_time = (byte)t.misc[4];
            t.lift_height = (long)((unsigned long)t.misc[5] * 10000UL);
            t.misc[3] = t.misc[4] = t.misc[5] = 0;
        }
    }
}

// version < 0x254: values greyed out in 2.53's item editor are cleared, by family.
struct Nuke
{
    int type;
    dword flags;
    word misc;      // bit n: misc n+1
    int wpn_from;
};

const Nuke g_nuke[] =
{
    {   0, 0x01000, 0x3FC,  3 },   // sword
    {   1, 0x01800, 0x3F2,  3 },   // brang
    {   2, 0x01C20, 0x3FE,  3 },   // arrow
    {   3, 0x01400, 0x3FF,  3 },   // candle
    {   4, 0x01C20, 0x3FC,  1 },   // whistle
    {   5, 0x01C30, 0x3FE,  1 },   // bait
    {   6, 0x01C30, 0x3FF,  0 },   // letter
    {   7, 0x01C00, 0x3FC,  0 },   // potion
    {   8, 0x01420, 0x3FF,  3 },   // wand
    {   9, 0x01C30, 0x3FE,  0 },   // ring
    {  10, 0x01C30, 0x3FC,  0 },   // wallet
    {  11, 0x01C30, 0x3FF,  0 },   // amulet
    {  12, 0x01C30, 0x3FC,  0 },   // shield
    {  13, 0x01C30, 0x3FF,  0 },   // bow
    {  14, 0x01C30, 0x3FF,  0 },   // raft
    {  15, 0x01C30, 0x3FF,  0 },   // ladder
    {  16, 0x01C20, 0x3FF,  2 },   // book
    {  17, 0x01C20, 0x3FF,  0 },   // magic key
    {  18, 0x01C20, 0x3FF,  0 },   // bracelet
    {  19, 0x01C30, 0x3FF,  0 },   // flippers
    {  20, 0x01C20, 0x3FF,  0 },   // boots
    {  21, 0x01C20, 0x3F0,  4 },   // hookshot
    {  22, 0x01C30, 0x3FE,  0 },   // lens
    {  23, 0x01C30, 0x3FF,  2 },   // hammer
    {  24, 0x01810, 0x3FC,  5 },   // divine fire
    {  25, 0x01C30, 0x3FE,  0 },   // divine escape
    {  26, 0x01000, 0x3FE, 10 },   // divine protection
    {  27, 0x01C20, 0x3F8,  2 },   // bomb
    {  28, 0x01C30, 0x3F8,  2 },   // super bomb
    {  29, 0x01C30, 0x3FE,  0 },   // clock
    {  30, 0x01C30, 0x3FF,  0 },   // key
    {  31, 0x01C30, 0x3FF,  0 },   // magic container
    {  32, 0x01C20, 0x3FC,  0 },   // triforce piece
    {  33, 0x01C30, 0x3FF,  0 },   // map
    {  34, 0x01C30, 0x3FF,  0 },   // compass
    {  35, 0x01C30, 0x3FF,  0 },   // boss key
    {  36, 0x01C30, 0x3FC,  0 },   // quiver
    {  37, 0x01C30, 0x3FF,  0 },   // level key
    {  38, 0x01420, 0x3F8,  5 },   // cane of byrna
    {  39, 0x01C30, 0x3FF,  0 },   // rupee
    {  40, 0x01C30, 0x3FF,  0 },   // arrow ammo
    {  41, 0x01C00, 0x3F8,  0 },   // fairy
    {  42, 0x01C30, 0x3FF,  0 },   // magic
    {  43, 0x01C30, 0x3FF,  0 },   // heart
    {  44, 0x01C30, 0x3FF,  0 },   // heart container
    {  45, 0x01C30, 0x3FF,  0 },   // heart piece
    {  46, 0x01C30, 0x3FF,  0 },   // kill all enemies
    {  47, 0x01C30, 0x3FF,  0 },   // bomb ammo
    {  48, 0x01C20, 0x3FC,  0 },   // bomb bag
    {  49, 0x01C30, 0x3FF,  0 },   // roc's feather
    {  50, 0x01C30, 0x3FE,  1 },   // hover boots
    {  51, 0x01C30, 0x3FE,  0 },   // spin scroll
    {  52, 0x01C30, 0x3FF,  0 },   // cross scroll
    {  53, 0x01C30, 0x3FC,  0 },   // quake scroll
    {  54, 0x01C20, 0x3FE,  0 },   // whisp ring
    {  55, 0x01C30, 0x3FC,  0 },   // charge ring
    {  56, 0x01C30, 0x3FE,  0 },   // peril scroll
    {  57, 0x01C20, 0x3FE,  0 },   // wealth medal
    {  58, 0x01C30, 0x3FC,  0 },   // heart ring
    {  59, 0x01C30, 0x3FC,  0 },   // magic ring
    {  60, 0x01C30, 0x3FE,  0 },   // spin scroll 2
    {  61, 0x01C30, 0x3FC,  0 },   // quake scroll 2
    {  62, 0x01C30, 0x3FE,  0 },   // stone of agony
    {  63, 0x01C30, 0x3FF,  0 },   // stomp boots
    {  64, 0x01C30, 0x3FE,  0 },   // whimsical ring
    {  65, 0x01C30, 0x3FE,  0 },   // peril ring
    {  66, 0x01C30, 0x3FF,  0 },   // misc
    {  67, 0x01C30, 0x3FF,  0 },   // custom 1 to 20
    {  68, 0x01C30, 0x3FF,  0 },
    {  69, 0x01C30, 0x3FF,  0 },
    {  70, 0x01C30, 0x3FF,  0 },
    {  71, 0x01C30, 0x3FF,  0 },
    {  72, 0x01C30, 0x3FF,  0 },
    {  73, 0x01C30, 0x3FF,  0 },
    {  74, 0x01C30, 0x3FF,  0 },
    {  75, 0x01C30, 0x3FF,  0 },
    {  76, 0x01C30, 0x3FF,  0 },
    {  77, 0x01C30, 0x3FF,  0 },
    {  78, 0x01C30, 0x3FF,  0 },
    {  79, 0x01C30, 0x3FF,  0 },
    {  80, 0x01C30, 0x3FF,  0 },
    {  81, 0x01C30, 0x3FF,  0 },
    {  82, 0x01C30, 0x3FF,  0 },
    {  83, 0x01C30, 0x3FF,  0 },
    {  84, 0x01C30, 0x3FF,  0 },
    {  85, 0x01C30, 0x3FF,  0 },
    {  86, 0x01C30, 0x3FF,  0 },
    {  87, 0x01C30, 0x3FF,  0 },   // bow and arrow
    {  88, 0x01C30, 0x3FF,  0 },   // letter or potion
};

void set_flag(dword &flags, dword f, bool on)
{
    if(on)
        flags |= f;
    else
        flags &= ~f;
}

// update_old_item (3.0) and the matching part of 2.55.17's readitems, for one item of a
// section older than either knows. items holds the items converted so far (the used letter
// copies the letter's look).
void update_old_item(std::vector<UpItem> &items, int index, int s, word version, const Rules &R, int target)
{
    UpItem &t = items[index];

    if(s < 2 && index == iLetterUsed)
    {
        reset_itembuf(t, iLetterUsed, target);
        t.name = up_item_name(index);
        const UpItem &letter = items[iLetter];
        t.tile = letter.tile;
        t.csets = letter.csets;
        t.misc_flags = letter.misc_flags;
        t.frames = letter.frames;
        t.speed = letter.speed;
        t.ltm = letter.ltm;
    }

    if(s < 3)
    {
        switch(index)
        {
        case iRocsFeather: case iHoverBoots: case iSpinScroll: case iL2SpinScroll:
        case iCrossScroll: case iQuakeScroll: case iL2QuakeScroll: case iWhispRing:
        case iL2WhispRing: case iChargeRing: case iL2ChargeRing: case iPerilScroll:
        case iWalletL3: case iQuiverL4: case iBombBagL4: case iBracelet: case iL2Bracelet:
        case iOldGlove: case iL2Ladder: case iWealthMedal: case iL2WealthMedal: case iL3WealthMedal:
            reset_itembuf(t, index, target);
            t.name = up_item_name(index);
            break;

        case iSShield:
            reset_itembuf(t, index, target);
            t.name = up_item_name(index);

            // 2.55 renames the other two shields too
            if(target == T255)
            {
                items[iShield].name = up_item_name(iShield);
                items[iMShield].name = up_item_name(iMShield);
            }

            break;
        }
    }

    if(s < 5)
    {
        switch(index)
        {
        case iHeartRing: case iL2HeartRing: case iL3HeartRing: case iMagicRing:
        case iL2MagicRing: case iL3MagicRing: case iL4MagicRing:
            reset_itembuf(t, index, target);
            t.name = up_item_name(index);
            break;
        }
    }

    if(s < 6)
    {
        if(index != iBPotion && index != iRPotion && R.dep(32))
            t.flags |= ITEM_KEEPOLD;

        switch(index)
        {
        case iTriforce:
            t.level = 1;
            break;

        case iBigTri:
            t.level = 0;
            break;

        case iBombs:
            t.level = i_bomb;
            t.power = 4;
            t.wpn[0] = wBOMB;
            t.wpn[1] = wBOOM;
            t.misc[0] = 50;

            if(R.dep(qr_SLOWBOMBFUSES_DEP))
                t.misc[0] = 200;

            break;

        case iSBomb:
            t.level = i_sbomb;
            t.power = 16;
            t.wpn[0] = wSBOMB;
            t.wpn[1] = wSBOOM;
            t.misc[0] = 50;

            if(R.dep(qr_SLOWBOMBFUSES_DEP))
                t.misc[0] = 400;

            break;

        case iBook:
            if(R.dep(qr_FIREMAGICSPRITE_DEP))
                t.wpn[0] = wFIREMAGIC;

            break;

        case iSArrow:
            t.wpn[1] = R.dep(27) ? wSSPARKLE : 0;
            t.power = 4;
            t.flags |= ITEM_GAMEDATA;
            t.wpn[0] = wSARROW;
            break;

        case iGArrow:
            t.wpn[1] = R.dep(28) ? wGSPARKLE : 0;
            t.power = 8;
            t.flags |= ITEM_GAMEDATA | ITEM_FLAG1;
            t.wpn[0] = wGARROW;
            break;

        case iBrang:
            t.power = 0;
            t.wpn[0] = wBRANG;
            t.misc[0] = 36;
            break;

        case iMBrang:
            t.wpn[1] = R.dep(29) ? wMSPARKLE : 0;
            t.power = 0;
            t.wpn[0] = wMBRANG;
            break;

        case iFBrang:
            t.wpn[2] = R.dep(30) ? wFSPARKLE : 0;
            t.power = 2;
            t.wpn[0] = wFBRANG;
            break;

        case iBoots:
            t.cost_amount[0] = R.dep(qr_MAGICBOOTS_DEP) ? 1 : 0;
            t.power = 7;
            break;

        case iWand:
            t.cost_amount[0] = R.dep(qr_MAGICWAND_DEP) ? 8 : 0;
            t.power = 2;
            t.wpn[0] = wWAND;
            t.wpn[2] = wMAGIC;
            break;

        case iBCandle:
            t.cost_amount[0] = R.dep(qr_MAGICCANDLE_DEP) ? 4 : 0;
            t.power = 1;
            t.flags |= ITEM_GAMEDATA | ITEM_FLAG1;
            t.wpn[2] = wFIRE;
            break;

        case iRCandle:
            t.cost_amount[0] = R.dep(qr_MAGICCANDLE_DEP) ? 4 : 0;
            t.power = 1;
            t.wpn[2] = wFIRE;
            break;

        case iSword:
            t.power = 1;
            t.flags |= ITEM_FLAG4 | ITEM_FLAG2;
            t.wpn[0] = t.wpn[2] = wSWORD;
            t.wpn[1] = wSWORDSLASH;
            break;

        case iWSword:
            t.power = 2;
            t.flags |= ITEM_FLAG4 | ITEM_FLAG2;
            t.wpn[0] = t.wpn[2] = wWSWORD;
            t.wpn[1] = wWSWORDSLASH;
            break;

        case iMSword:
            t.power = 4;
            t.flags |= ITEM_FLAG4 | ITEM_FLAG2;
            t.wpn[0] = t.wpn[2] = wMSWORD;
            t.wpn[1] = wMSWORDSLASH;
            break;

        case iXSword:
            t.power = 8;
            t.flags |= ITEM_FLAG4 | ITEM_FLAG2;
            t.wpn[0] = t.wpn[2] = wXSWORD;
            t.wpn[1] = wXSWORDSLASH;
            break;

        case iNayrusLove:
            if(R.dep(qr_FLICKERINGNAYRUSLOVEROCKET_DEP))
                t.flags |= ITEM_FLAG1;

            if(R.dep(qr_TRANSLUCENTNAYRUSLOVEROCKET_DEP))
                t.flags |= ITEM_FLAG2;

            t.wpn[0] = wNAYRUSLOVE1A;
            t.wpn[1] = wNAYRUSLOVE1B;
            t.wpn[2] = wNAYRUSLOVES1A;
            t.wpn[3] = wNAYRUSLOVES1B;
            t.wpn[5] = wNAYRUSLOVE2A;
            t.wpn[6] = wNAYRUSLOVE2B;
            t.wpn[7] = wNAYRUSLOVES2A;
            t.wpn[8] = wNAYRUSLOVES2B;
            t.wpn[4] = iwNayrusLoveShieldFront;
            t.wpn[9] = iwNayrusLoveShieldBack;
            t.misc[0] = 512;
            t.cost_amount[0] = 64;
            break;

        case iLens:
            t.misc[0] = 60;

            if(!R.qr(qr_ENABLEMAGIC))
                t.flags |= ITEM_RUPEE_MAGIC;

            t.cost_amount[0] = R.qr(qr_ENABLEMAGIC) ? 2 : 1;
            break;

        case iArrow:
            t.power = 2;
            t.wpn[0] = wARROW;
            break;

        case iHoverBoots:
            t.misc[0] = 45;
            t.wpn[0] = iwHover;
            break;

        case iDinsFire:
            t.power = 8;
            t.wpn[0] = wDINSFIRE1A;
            t.wpn[1] = wDINSFIRE1B;
            t.wpn[2] = wDINSFIRES1A;
            t.wpn[3] = wDINSFIRES1B;
            t.misc[0] = 32;
            t.misc[1] = 200;
            t.cost_amount[0] = 32;
            break;

        case iFaroresWind:
            t.cost_amount[0] = 32;
            break;

        case iHookshot:
            t.power = 0;
            t.flags &= ~ITEM_FLAG1;
            t.wpn[0] = wHSHEAD;
            t.wpn[1] = wHSCHAIN_H;
            t.wpn[3] = wHSHANDLE;
            t.wpn[2] = wHSCHAIN_V;
            t.misc[0] = 50;
            t.misc[1] = 100;
            break;

        case iLongshot:
            t.power = 0;
            t.flags &= ~ITEM_FLAG1;
            t.wpn[0] = wLSHEAD;
            t.wpn[1] = wLSCHAIN_H;
            t.wpn[3] = wLSHANDLE;
            t.wpn[2] = wLSCHAIN_V;
            t.misc[0] = 99;
            t.misc[1] = 100;
            break;

        case iHammer:
            t.power = 4;
            t.wpn[0] = wHAMMER;
            t.wpn[1] = iwHammerSmack;
            break;

        case iCByrna:
            t.power = 1;
            t.wpn[0] = wCBYRNA;
            t.wpn[1] = wCBYRNASLASH;
            t.wpn[2] = wCBYRNAORB;
            t.misc[0] = 4;
            t.misc[1] = 16;
            t.misc[2] = 1;
            t.cost_amount[0] = 1;
            break;

        case iWhistle:
            t.wpn[0] = wWIND;
            t.misc[0] = 3;
            t.flags |= ITEM_FLAG1;
            break;

        case iBRing:
            t.power = 2;
            t.misc[0] = spBLUE;
            break;

        case iRRing:
            t.power = 4;
            t.misc[0] = spRED;
            break;

        case iGRing:
            t.power = 8;
            t.misc[0] = spGOLD;
            break;

        case iSpinScroll:
            t.power = 2;
            t.misc[0] = 1;
            break;

        case iL2SpinScroll:
            t.type = itype_spinscroll2;
            t.level = 1;
            t.cost_amount[0] = 8;
            t.power = 2;
            t.misc[0] = 20;
            break;

        case iQuakeScroll:
            t.misc[0] = 0x10;
            t.misc[1] = 64;
            break;

        case iL2QuakeScroll:
            t.type = itype_quakescroll2;
            t.level = 1;
            t.power = 2;
            t.misc[0] = 0x20;
            t.misc[1] = 192;
            t.cost_amount[0] = 8;
            break;

        case iChargeRing:
            t.misc[0] = 64;
            t.misc[1] = 128;
            break;

        case iL2ChargeRing:
            t.misc[0] = 32;
            t.misc[1] = 64;
            break;

        case iOldGlove:
            t.flags |= ITEM_FLAG1;
            // fall through
        case iBombBagL4:
        case iWalletL3:
        case iQuiverL4:
        case iBracelet:
            t.power = 1;
            break;

        case iL2Bracelet:
            t.power = 2;
            break;

        case iMKey:
            t.power = 0xFF;
            t.flags |= ITEM_FLAG1;
            break;
        }
    }

    if(s < 7)
    {
        switch(index)
        {
        case iStoneAgony: case iStompBoots: case iPerilRing: case iWhimsicalRing:
            reset_itembuf(t, index, target);
            t.name = up_item_name(index);
            break;
        }
    }

    if(s < 8)
    {
        switch(index)
        {
        case iMShield:
            t.misc[0] |= shFLAME;
            t.misc[1] |= shFIREBALL | shMAGIC;

            if(R.qr(qr_SWORDMIRROR))
                t.misc[1] |= shSWORD;

            // fall through
        case iShield:
            t.misc[0] |= shFIREBALL | shSWORD | shMAGIC;
            // fall through
        case iSShield:
            t.misc[0] |= shROCK | shARROW | shBRANG | shSCRIPT;

            if(R.dep(102))
                t.misc[1] |= shROCK;

            break;

        case iWhispRing:
            t.power = 1;
            t.flags |= ITEM_GAMEDATA | ITEM_FLAG1;
            t.misc[0] = 3;
            break;

        case iL2WhispRing:
            t.power = 0;
            t.flags |= ITEM_GAMEDATA | ITEM_FLAG1;
            t.misc[0] = 3;
            break;

        case iL2Ladder:
        case iBow:
        case iCByrna:
            t.power = 1;
            break;
        }
    }

    if(s < 9 && index == iClock)
        t.misc[0] = R.dep(qr_TEMPCLOCKS_DEP) ? 256 : 0;

    if(s < 10 && t.type == itype_bomb)
        set_flag(t.flags, ITEM_FLAG1, R.qr(qr_LONGBOMBBOOM_DEP));

    if(s < 11 && t.type == itype_triforcepiece)
    {
        t.flags = t.level ? ITEM_GAMEDATA : 0;
        t.playsound = t.level ? WAV_SCALE : WAV_CLEARED;
    }

    if(s < 12)
    {
        switch(index)
        {
        case iFBrang:
            t.misc[3] |= shFIREBALL | shSWORD | shMAGIC;
            // fall through
        case iMBrang:
            t.misc[2] |= shSWORD | shMAGIC;
            // fall through
        case iHookshot:
        case iLongshot:
            t.misc[2] |= shFIREBALL;
            // fall through
        case iBrang:
            t.misc[2] |= shBRANG | shROCK | shARROW;
            break;
        }

        switch(t.type)
        {
        case itype_hoverboots: t.usesound = WAV_ZN1HOVER; break;
        case itype_wand: t.usesound = WAV_WAND; break;
        case itype_book: t.usesound = WAV_FIRE; break;      // 2.53 gives it WAV_WAND
        case itype_arrow: t.usesound = WAV_ARROW; break;
        case itype_hookshot: t.usesound = WAV_HOOKSHOT; break;
        case itype_brang: t.usesound = WAV_BRANG; break;
        case itype_shield: t.usesound = WAV_CHINK; break;
        case itype_sword: t.usesound = WAV_SWORD; break;
        case itype_whistle: t.usesound = WAV_WHISTLE; break;
        case itype_hammer: t.usesound = WAV_HAMMER; break;
        case itype_dinsfire: t.usesound = WAV_ZN1DINSFIRE; break;
        case itype_faroreswind: t.usesound = WAV_ZN1FARORESWIND; break;
        case itype_nayruslove: t.usesound = WAV_ZN1NAYRUSLOVE1; break;
        case itype_bomb: case itype_sbomb: case itype_quakescroll: case itype_quakescroll2:
            t.usesound = WAV_BOMB;
            break;
        case itype_spinscroll: case itype_spinscroll2:
            t.usesound = WAV_ZN1SPINATTACK;
            break;
        }
    }

    if(s < 13)
    {
        if(t.type == itype_whistle)
        {
            t.misc[0] = t.power == 2 ? 4 : 3;
            t.power = 1;
            t.flags |= ITEM_FLAG1;
        }
        else if(t.type == itype_wand)
            t.flags |= ITEM_FLAG1;
        else if(t.type == itype_book)
        {
            t.flags |= ITEM_FLAG1;
            t.power = 2;
        }
    }

    if(s < 14)
    {
        if(t.type == itype_fairy)
        {
            t.usesound = WAV_SCALE;

            if(t.level)
                t.misc[2] = 50;
        }
        else if(t.type == itype_potion)
            t.flags |= ITEM_GAINOLD;
    }

    if(s < 17)
    {
        if(t.type == itype_candle && !t.wpn[2])
            t.wpn[2] = wFIRE;
        else if(t.type == itype_arrow && t.power > 4)
            t.flags |= ITEM_FLAG1;
    }

    if(s < 18)
    {
        if(t.type == itype_whistle)
            t.misc[1] = 8;
        else if(t.type == itype_bait)
            t.misc[0] = 768;
        else if(t.type == itype_triforcepiece && (t.flags & ITEM_GAMEDATA))
        {
            t.misc[1] = 1;
            t.flags |= ITEM_FLAG1;
        }
    }

    if(s < 19 && t.type == itype_nayruslove)
    {
        if(R.dep(qr_NOBOMBPALFLASH + 1))
            t.flags |= ITEM_FLAG3;

        if(R.dep(qr_NOBOMBPALFLASH + 2))
            t.flags |= ITEM_FLAG4;
    }

    if(s < 20 && t.type == itype_nayruslove)
    {
        t.wpn[5] = wNAYRUSLOVE2A;
        t.wpn[6] = wNAYRUSLOVE2B;
        t.wpn[7] = wNAYRUSLOVES2A;
        t.wpn[8] = wNAYRUSLOVES2B;
        t.wpn[4] = iwNayrusLoveShieldFront;
        t.wpn[9] = iwNayrusLoveShieldBack;
    }

    if(s < 21 && (t.flags & 0x0100))
    {
        t.flags &= ~0x0100;

        if(t.type == itype_sword || t.type == itype_wand || t.type == itype_candle || t.type == itype_cbyrna)
            t.flags |= ITEM_FLAG4;
    }

    if(s < 22 && (t.type == itype_sbomb || t.type == itype_bomb))
        t.misc[2] = t.power / 2;

    if(s < 23)
    {
        if(t.type == itype_dinsfire)
            t.wpn[4] = wFIRE;
        else if(t.type == itype_book)
            t.wpn[1] = wFIRE;
    }

    if(s < 25)
    {
        if(t.type == itype_bombbag)
            t.flags |= ITEM_FLAG1;

        if(t.type == itype_dinsfire)
            t.flags |= ITEM_FLAG3;
    }

    // (upstream's test for quests older than 2.53 here, a triforce flag, can never be true)

    if(version < 0x254)
    {
        for(size_t k = 0; k < sizeof(g_nuke) / sizeof(g_nuke[0]); ++k)
        {
            const Nuke &n = g_nuke[k];

            if(n.type != t.type)
                continue;

            t.flags &= ~n.flags;

            for(int j = 0; j < 10; ++j)
                if(n.misc & (1 << j))
                    t.misc[j] = 0;

            for(int j = n.wpn_from; j < 10; ++j)
                t.wpn[j] = 0;

            break;
        }
    }

    // quest rules ported into the items
    if(s <= 31)
    {
        if(t.type == itype_bomb || t.type == itype_sbomb)
            set_flag(t.flags, ITEM_FLAG2, R.qr(qr_OUCHBOMBS));
        else if(t.type == itype_brang)
            set_flag(t.flags, ITEM_FLAG4, R.qr(qr_BRANGPICKUP));
        else if(t.type == itype_wand)
            set_flag(t.flags, ITEM_FLAG3, R.qr(qr_NOWANDMELEE));
    }

    if(s <= 37)
    {
        if(t.type == itype_flippers)
            set_flag(t.flags, ITEM_FLAG1, R.qr(qr_NODIVING));
        else if(t.type == itype_sword || t.type == itype_wand)
            set_flag(t.flags, ITEM_FLAG5, R.qr(qr_QUICKSWORD));
        else if(t.type == itype_book || t.type == itype_candle)
            set_flag(t.flags, ITEM_FLAG3, R.qr(qr_FIREPROOFLINK));
    }

    if(s < 38)
    {
        if(t.type == itype_brang || t.type == itype_hookshot)
        {
            set_flag(t.flags, ITEM_FLAG4, R.qr(qr_BRANGPICKUP));
            set_flag(t.flags, ITEM_FLAG5 | UF_FLAG6, R.qr(qr_Z3BRANG_HSHOT));
        }
        else if(t.type == itype_arrow)
        {
            set_flag(t.flags, ITEM_FLAG4, R.qr(qr_BRANGPICKUP));
            set_flag(t.flags, ITEM_FLAG2, !R.qr(qr_Z3BRANG_HSHOT));
        }
    }

    if(s < 39)
    {
        if(t.type == itype_dinsfire || t.type == itype_book || t.type == itype_candle)
            set_flag(t.flags, ITEM_FLAG5, R.qr(qr_TEMPCANDLELIGHT));
        else if(t.type == itype_potion)
        {
            if(R.qr(qr_NONBUBBLEMEDICINE))
                t.flags &= ~(ITEM_FLAG3 | ITEM_FLAG4);
            else
            {
                t.flags |= ITEM_FLAG3;
                set_flag(t.flags, ITEM_FLAG4, R.qr(qr_ITEMBUBBLE));
            }
        }
        else if(t.type == itype_triforcepiece)
        {
            if(R.qr(qr_NONBUBBLETRIFORCE))
            {
                t.flags |= ITEM_FLAG3;
                set_flag(t.flags, ITEM_FLAG4, R.qr(qr_ITEMBUBBLE));
            }
            else
                t.flags &= ~(ITEM_FLAG3 | ITEM_FLAG4);
        }
    }

    if(s < 40)
    {
        if(t.type == itype_ring || t.type == itype_perilring)
            set_flag(t.flags, ITEM_FLAG1, R.qr(qr_RINGAFFECTDAMAGE));
        else if(t.type == itype_candle || t.type == itype_sword || t.type == itype_wand || t.type == itype_cbyrna)
            set_flag(t.flags, UF_FLAG8, R.qr(qr_SLASHFLIPFIX));

        if(t.type == itype_sword || t.type == itype_wand || t.type == itype_hammer)
            set_flag(t.flags, UF_FLAG7, R.qr(qr_NOITEMMELEE));
        else if(t.type == itype_cbyrna)
            t.flags |= UF_FLAG7;
    }

    if(s < 41 && t.type == itype_sword)
    {
        set_flag(t.flags, UF_FLAG9, R.qr(qr_SWORDMIRROR));
        set_flag(t.flags, UF_FLAG10, R.qr(qr_SLOWCHARGINGWALK));
    }

    if(s < 42)
    {
        if(t.type == itype_wand)
        {
            set_flag(t.flags, ITEM_FLAG3, R.qr(qr_NOWANDMELEE));
            t.flags &= ~UF_FLAG6;
        }
        else if(t.type == itype_hammer)
            t.flags &= ~ITEM_FLAG3;
        else if(t.type == itype_cbyrna)
        {
            t.flags |= ITEM_FLAG3;
            t.flags &= ~UF_FLAG6;
        }
        else if(t.type == itype_sword)
            set_flag(t.flags, UF_FLAG6, R.qr(qr_MELEEMAGICCOST));
    }

    if(s < 43 && t.type == itype_whistle)
        set_flag(t.flags, ITEM_FLAG3, R.qr(qr_WHIRLWINDMIRROR));

    if(s < 45 && t.type == itype_flippers)
    {
        t.misc[0] = 50;
        t.misc[1] = 30;
    }

    if(s < 46 && t.type == itype_raft)
        t.misc[0] = 1;

    if(s < 34)
    {
        if(t.flags & ITEM_RUPEE_MAGIC)
            t.cost_counter[0] = 1;
        else if(R.qr(qr_ENABLEMAGIC))
            t.cost_counter[0] = 4;
        else
        {
            t.cost_amount[0] = 0;
            t.cost_counter[0] = -1;
        }
    }

    if(s < 35 && t.type == itype_lens)
    {
        if(R.qr(qr_RAFTLENS))
            t.flags |= ITEM_FLAG4;

        if(R.qr(qr_LENSHINTS))
            t.flags |= ITEM_FLAG1;

        if(R.qr(qr_LENSSEESENEMIES))
            t.flags |= ITEM_FLAG5;
    }

    // s < 44: the sprite script is cleared (the record never had one); s < 47: no pickup flag
    if(s < 47)
        t.pickupflag = 0;

    if(s < 51 && t.type == itype_candle)
        t.misc[3] = 50;

    if(s < 52 && t.type == itype_shield)
        t.flags |= ITEM_FLAG1;

    if(s < 53)
    {
        switch(t.type)
        {
        case itype_arrow:
            t.cost_counter[1] = CR_ARROWS;
            t.cost_amount[1] = 1;
            break;

        case itype_bomb:
            t.cost_counter[1] = CR_BOMBS;
            t.cost_amount[1] = 1;
            break;

        case itype_sbomb:
            t.cost_counter[1] = CR_SBOMBS;
            t.cost_amount[1] = 1;
            break;

        default:
            t.cost_counter[1] = CR_NONE;
            t.cost_amount[1] = 0;
        }

        t.magiccosttimer[1] = 0;
    }

    if(s < 54 && t.type == itype_flippers)
        t.misc[2] = INT_BTN_A_U;

    if(s < 55)
    {
        if(t.type == itype_spinscroll || t.type == itype_quakescroll)
            t.usesound2 = WAV_ZN1CHARGE_U;
        else if(t.type == itype_spinscroll2 || t.type == itype_quakescroll2)
            t.usesound2 = WAV_ZN1CHARGE2_U;
    }

    if(s < 56)
    {
        if(t.type == itype_dinsfire)
        {
            set_flag(t.flags, UF_FLAG9, version < 0x255);
            set_flag(t.flags, UF_FLAG10, version < 0x250);
            t.flags |= UF_FLAG11;
        }
        else if(t.type == itype_candle)
        {
            set_flag(t.flags, UF_FLAG9, t.level > 1);
            t.flags &= ~(UF_FLAG10 | UF_FLAG11);
        }
        else if(t.type == itype_book)
        {
            t.flags |= UF_FLAG9 | UF_FLAG10;
            t.flags &= ~UF_FLAG11;
        }
    }

    if(target == T30 && s < 61 && t.type == itype_sword)
        t.usesound2 = WAV_BEAM_U;

    if(t.level == 0)
        t.level = 1;
}

// ---- the records as stored ----------------------------------------------------------------

// A section's records as read_single_item_old leaves them, before anything after it: for
// section versions above 1 the stored record over the item's defaults, otherwise only its
// look (over a fresh record, which the defaults replace later).
struct Stored
{
    int s, count;
    std::vector<std::string> names;
    std::vector<UpItem> recs;
};

// A part's bytes after its id. False when they aren't laid out the way readitems reads them.
bool read_stored(const std::vector<byte> &part, int target, Stored &out)
{
    if(part.size() < 10)
        return false;

    out.s = (word)get_le(part, 0, 2);
    out.count = (word)get_le(part, 8, 2);

    if(out.s > 25 || out.count > MAXITEMS)
        return false;

    In names(part, 10);
    In r(part, 10 + (out.s > 1 ? 64 * (size_t)out.count : 0));
    out.names.clear();
    out.recs.clear();

    for(int i = 0; i < out.count; ++i)
    {
        std::string name = out.s > 1 ? names.name(63) : reset_name(i, true);
        UpItem t;
        t.fresh();
        t.name = name;

        if(out.s > 1)
        {
            reset_itembuf(t, i, target);
            read_record(r, t, out.s);
        }
        else
        {
            t.tile = r.u16();
            t.misc_flags = r.u8();
            t.csets = r.u8();
            t.frames = r.u8();
            t.speed = r.u8();
            t.delay = r.u8();
            t.ltm = r.s32();
        }

        if(r.bad)
            return false;

        out.names.push_back(name);
        out.recs.push_back(t);
    }

    if(r.pos != part.size())
    {
        note("zqn items: section v%d has %u bytes, records end at %u\n", out.s, (unsigned)part.size(), (unsigned)r.pos);
        return false;
    }

    return true;
}

// The rest of read_single_item_old for one record: rec is what read_stored left for it (or,
// for a quest older than 1.93, the look ZQuest holds).
void finish_record(UpItem &t, const UpItem &rec, int i, int s, word version, byte build, int target)
{
    t = rec;

    if(version > 0x192 && s > 1)
    {
        if(target == T30)
            read_tail_30(t);

        return;
    }

    t.count = -1;
    t.flags = 0;
    t.wpn[0] = t.wpn[1] = t.wpn[2] = 0;
    t.pickup_hearts = 0;
    t.misc[0] = t.misc[1] = 0;
    t.usesound = 0;
    t.playsound = WAV_SCALE;

    if(version < 0x193 && (version < 0x192 || build < 186))
    {
        // pre-1.93 before build 186: the look, then the defaults
        t.type = 0xFF;
        reset_itembuf(t, i, target);
        return;
    }

    // version 1 (and pre-1.93 from build 186): the look, then the defaults
    t.type = itype_misc;
    reset_itembuf(t, i, target);

    if(target == T30)
        read_tail_30(t);
}

// ---- the quest's own-version save ---------------------------------------------------------

// A write-only file that keeps what is written to it.
struct VecSink
{
    std::vector<byte> *out;
};

int vs_fclose(void *) { return 0; }
int vs_getc(void *) { return EOF; }
int vs_ungetc(int, void *) { return EOF; }
int vs_feof(void *) { return 1; }
int vs_ferror(void *) { return 0; }
int vs_fseek(void *, int) { return -1; }

// The packfile callbacks use the system's own long (see zcport/lp64.h).
#pragma push_macro("long")
#undef long
long vs_fread(void *, long, void *) { return 0; }

#pragma push_macro("long")  // packfile callbacks use the system's long (zcport/lp64.h)
#undef long
long vs_fwrite(const void *p, long n, void *user)
{
    const byte *b = (const byte *)p;
    std::vector<byte> &o = *((VecSink *)user)->out;
    o.insert(o.end(), b, b + n);
    return n;
}
#pragma pop_macro("long")
#pragma pop_macro("long")

int vs_putc(int c, void *user)
{
    ((VecSink *)user)->out->push_back((byte)c);
    return c & 0xFF;
}

const PACKFILE_VTABLE g_vec_vtable = { vs_fclose, vs_getc, vs_ungetc, vs_fread, vs_putc, vs_fwrite, vs_fseek, vs_feof, vs_ferror };

// The part as the quest's own-version save would write it now, without its id. False when that
// save can't hold ZQuest's items as they are (its writer refuses), which is then no reason to
// refuse this save: the records the user changed are taken from ZQuest's data instead.
bool own_version_part(const ZqQuestPart &p, std::vector<byte> &out)
{
    out.clear();
    VecSink sink;
    sink.out = &out;
    PACKFILE *f = zq_open_sink(&g_vec_vtable, &sink);

    if(!f)
        return false;

    std::string err = zqw_last_error;
    int rc = zq_write_part(f, p);
    pack_fclose(f);
    zqw_last_error = err;

    if(rc != ZQW_OK || out.size() < 4)
    {
        note("zqn items: the own-version save can't hold the items as they are (%d)\n", rc);
        return false;
    }

    out.erase(out.begin(), out.begin() + 4);
    return true;
}

// ---- the whole section, as upstream holds it after the load ---------------------------

struct ItemSet
{
    std::vector<UpItem> items;      // MAXITEMS
};

// Whether the user changed anything of item i but its look (the only part of an item a quest
// older than 1.93 stores).
bool item_changed_past_look(int i)
{
    if(all_changed())
        return true;

    itemdata a = itemsbuf[i];
    const itemdata &b = g_items[i];
    a.tile = b.tile;
    a.misc = b.misc;
    a.csets = b.csets;
    a.frames = b.frames;
    a.speed = b.speed;
    a.delay = b.delay;
    a.ltm = b.ltm;
    return !same_item(a, b);
}

// A record the user changed that the own-version save can't hold: t is upstream's conversion
// of the original record, a is ZQuest's record converted as a 2.53.1 one would be. Each field
// the user changed takes a's value; the rest stay as upstream converts the original, so an
// edit to one field doesn't bring along 2.53's view of the others (a 1.9x book's use sound,
// say). A changed family, level or flags changes what the conversions derive from them (the
// weapon's movement, the costs, the bomb flags moved into the weapon), so then all of a is taken.
void merge_changed(UpItem &t, const UpItem &a, const itemdata &now, const itemdata &then)
{
#define CH(x) (now.x != then.x)
    if(CH(family) || CH(fam_type) || CH(flags))
    {
        std::string name = t.name;
        bool present = t.present;
        t = a;
        t.name = name;
        t.present = present;
        return;
    }

    if(CH(tile)) t.tile = a.tile;
    if(CH(misc)) t.misc_flags = a.misc_flags;
    if(CH(csets)) t.csets = a.csets;
    if(CH(frames)) t.frames = a.frames;
    if(CH(speed)) t.speed = a.speed;
    if(CH(delay)) t.delay = a.delay;
    if(CH(ltm)) t.ltm = a.ltm;
    if(CH(power)) t.power = a.power;
    if(CH(count)) t.count = a.count;
    if(CH(amount)) t.amount = a.amount;
    if(CH(setmax)) t.setmax = a.setmax;
    if(CH(max)) t.max = a.max;
    if(CH(playsound)) t.playsound = a.playsound;
    if(CH(pickup_hearts)) t.pickup_hearts = a.pickup_hearts;
    if(CH(usesound)) t.usesound = a.usesound;

    // the scripts and their arguments go together (3.0 gives the arguments to one of them)
    if(CH(script) || CH(collect_script) || memcmp(now.initiald, then.initiald, sizeof(now.initiald)) != 0
       || CH(initiala[0]) || CH(initiala[1]))
    {
        t.script = a.script;
        t.collect_script = a.collect_script;
        memcpy(t.initd, a.initd, sizeof(t.initd));
        memcpy(t.inita, a.inita, sizeof(t.inita));
        t.collect_args = a.collect_args;
    }

    const byte wn[10] = { now.wpn, now.wpn2, now.wpn3, now.wpn4, now.wpn5, now.wpn6, now.wpn7, now.wpn8, now.wpn9, now.wpn10 };
    const byte wt[10] = { then.wpn, then.wpn2, then.wpn3, then.wpn4, then.wpn5, then.wpn6, then.wpn7, then.wpn8, then.wpn9, then.wpn10 };

    for(int j = 0; j < 10; ++j)
        if(wn[j] != wt[j])
            t.wpn[j] = a.wpn[j];

    const long mn[10] = { now.misc1, now.misc2, now.misc3, now.misc4, now.misc5, now.misc6, now.misc7, now.misc8, now.misc9, now.misc10 };
    const long mt[10] = { then.misc1, then.misc2, then.misc3, then.misc4, then.misc5, then.misc6, then.misc7, then.misc8, then.misc9, then.misc10 };
    bool lift = false;

    for(int j = 0; j < 10; ++j)
    {
        if(mn[j] != mt[j])
        {
            t.misc[j] = a.misc[j];
            lift = lift || (j >= 3 && j <= 5);
        }
    }

    // 3.0 moves a bomb's misc 4 to 6 into its weapon's lifting
    if(lift && (a.type == itype_bomb || a.type == itype_sbomb))
    {
        for(int j = 3; j <= 5; ++j)
            t.misc[j] = a.misc[j];

        t.lift_level = a.lift_level;
        t.lift_time = a.lift_time;
        t.lift_height = a.lift_height;
    }

    // the cost, and whether the item costs anything at all
    if(CH(magic))
    {
        t.cost_amount[0] = a.cost_amount[0];
        t.cost_counter[0] = a.cost_counter[0];
    }
#undef CH
}

bool anything_changed()
{
    for(int i = 0; i < MAXITEMS; ++i)
        if(item_changed(i) || item_name_changed(i))
            return true;

    return false;
}

// Init data older than version 15 puts these values into the items after they're read; 2.53
// does the same, so ZQuest's items hold them.
void init_overrides(std::vector<UpItem> &items)
{
    const int fairies_potions[] = { iFairyStill, iFairyMoving, iRPotion, iBPotion };
    const int swords[] = { iSword, iWSword, iMSword, iXSword };

    for(int k = 0; k < 4; ++k)
    {
        int i = fairies_potions[k];
        items[i].misc[0] = itemsbuf[i].misc1;
        items[i].misc[1] = itemsbuf[i].misc2;

        if(i == iFairyStill || i == iFairyMoving)
            items[i].misc[2] = itemsbuf[i].misc3;

        items[i].flags |= itemsbuf[i].flags & (ITEM_FLAG1 | ITEM_FLAG2);
    }

    for(int k = 0; k < 4; ++k)
    {
        int i = swords[k];
        items[i].pickup_hearts = itemsbuf[i].pickup_hearts;
        items[i].misc[0] = itemsbuf[i].misc1;
        items[i].misc[1] = itemsbuf[i].misc2;

        if(!(itemsbuf[i].flags & ITEM_FLAG1))
            items[i].flags &= ~ITEM_FLAG1;
    }

    items[iHookshot].misc[0] = itemsbuf[iHookshot].misc1;
    items[iHookshot].misc[1] = itemsbuf[iHookshot].misc2;
    items[iLongshot].misc[0] = itemsbuf[iLongshot].misc1;
    items[iLongshot].misc[1] = itemsbuf[iLongshot].misc2;
}

bool old_init_data(const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version < 0x193)
        return true;

    const ZqQuestPart *p = zq_find_part(ID_INITDATA);
    word v, cv;
    return p && zq_part_versions(*p, &v, &cv) && v < 15;
}

int build_items(const ZqNewerContext &ctx, const Rules &R, ItemSet &out)
{
    int target = ctx.target;
    word version = ctx.src_zelda_version;
    byte build = ctx.src_build;
    std::vector<UpItem> &items = out.items;
    items.assign(MAXITEMS, UpItem());

    for(int i = 0; i < MAXITEMS; ++i)
        items[i].fresh();

    // the original part's records (a quest older than 1.93 holds every item's look)
    Stored orig;
    orig.s = 0;
    orig.count = MAXITEMS;

    if(version > 0x192)
    {
        std::vector<byte> part;

        if(!ctx.src_part || !read_part(*ctx.src_part, part) || !read_stored(part, target, orig))
        {
            if(part.size() >= 10 && ((word)get_le(part, 0, 2) > 25 || (word)get_le(part, 8, 2) > MAXITEMS))
                zqw_ig_refuse("Items: the quest's item section (version %d, %d items) isn't one this save knows.", (int)get_le(part, 0, 2), (int)get_le(part, 8, 2));

            return ERR_PARSE;
        }
    }

    int s = orig.s;

    // Records the user changed are what upstream reads from the quest's own-version save of
    // them, as it reads every other record from the original, when that save can hold them.
    Stored own;
    bool have_own = false;

    if(version > 0x192 && !all_changed() && anything_changed())
    {
        std::vector<byte> bytes;
        have_own = own_version_part(*ctx.src_part, bytes) && read_stored(bytes, target, own) && own.s == s;
    }

    int held = have_own && own.count > orig.count ? own.count : orig.count;

    // the names: stored from version 2, otherwise upstream's own
    for(int i = 0; i < MAXITEMS; ++i)
    {
        if(s <= 1)
            items[i].name = reset_name(i, true);
        else if(i < held)
            items[i].name = (have_own && i < own.count && (item_changed(i) || i >= orig.count)) ? own.names[i] : orig.names[i];

        // 3.0 holds the items read, the hardcoded ones and any that got a name
        if(target == T30)
            items[i].present = i < held || i < iLast || s <= 1;
    }

    // items the section doesn't hold: their defaults
    for(int i = held; i < MAXITEMS; ++i)
    {
        UpItem &t = items[i];

        if(target == T255)
        {
            std::string nm = t.name;
            t.clear();
            t.name = nm;
            t.count = -1;
            t.playsound = WAV_SCALE;
        }

        reset_itembuf(t, i, target);
    }

    // the section version each item's conversions start from
    std::vector<int> conv(MAXITEMS, s);

    // records the user changed that the own-version save can't hold: ZQuest's, to take the
    // changed fields from (merged) over upstream's conversion of the original
    std::vector<UpItem> zq(MAXITEMS);
    std::vector<char> merged(MAXITEMS, 0);

    for(int i = 0; i < held; ++i)
    {
        UpItem &t = items[i];
        std::string name = t.name;
        bool changed = item_changed(i);

        // a quest older than 1.93: the look as ZQuest holds it (the original's, unless changed)
        UpItem look;
        look.fresh();
        look.tile = itemsbuf[i].tile;
        look.misc_flags = itemsbuf[i].misc;
        look.csets = itemsbuf[i].csets;
        look.frames = itemsbuf[i].frames;
        look.speed = itemsbuf[i].speed;
        look.delay = itemsbuf[i].delay;
        look.ltm = itemsbuf[i].ltm;

        bool own_rec = changed && have_own && i < own.count;
        // (the checks' forced route sends a quest older than 1.93 this way too, to test the merge)
        bool zq_rec = changed && !own_rec && (version > 0x192 || force_mode() == FORCE_ROUTE || item_changed_past_look(i));

        if(zq_rec)
        {
            // what the own-version save can't hold: the record as 2.53 holds it, which is a
            // 2.53.1 file's record (its conversions are already in it)
            UpItem &a = zq[i];
            a = look;
            reset_itembuf(a, i, target);
            from_253(a, itemsbuf[i]);

            if(target == T30)
                read_tail_30(a);

            a.name = name;
            a.present = true;
            merged[i] = 1;
        }

        if(own_rec)
            finish_record(t, own.recs[i], i, s, version, build, target);
        else if(zq_rec && all_changed())
        {
            // the checks' forced path: the whole record is ZQuest's
            t = zq[i];
            conv[i] = 25;
            merged[i] = 0;
        }
        else if(version < 0x193)
            finish_record(t, look, i, s, version, build, target);
        else if(i < orig.count)
            finish_record(t, orig.recs[i], i, s, version, build, target);
        else
            finish_record(t, own.recs[i], i, s, version, build, target);    // written past the original's by the own-version save

        t.name = name;
        t.present = true;
    }

    // items past the ones either file holds that the user changed or named: ZQuest's, over
    // upstream's defaults
    for(int i = held; i < MAXITEMS; ++i)
    {
        if(item_value_changed(i))
        {
            UpItem &a = zq[i];
            a.fresh();
            a.name = items[i].name;
            reset_itembuf(a, i, target);
            from_253(a, itemsbuf[i]);

            if(target == T30)
                read_tail_30(a);

            a.present = true;
            items[i].present = true;

            if(all_changed())
            {
                items[i] = a;
                conv[i] = 25;
            }
            else
                merged[i] = 1;
        }

        if(item_name_changed(i))
            items[i].present = true;
    }

    // the newer conversions, on every item upstream holds
    for(int i = 0; i < MAXITEMS; ++i)
        if(items[i].present)
            update_old_item(items, i, conv[i], version, R, target);

    // ZQuest's records converted as 2.53.1 ones, then each changed field taken from them
    std::vector<UpItem> conv_zq;

    for(int i = 0; i < MAXITEMS; ++i)
    {
        if(!merged[i])
            continue;

        if(conv_zq.empty())
            conv_zq = items;

        conv_zq[i] = zq[i];
        update_old_item(conv_zq, i, 25, version, R, target);
        merge_changed(items[i], conv_zq[i], itemsbuf[i], g_items[i]);
    }

    // Version 1 sections and older: upstream gives the used letter the letter's look after
    // reading (with the user's changes to the letter's). A look the user gave the used letter
    // itself can't be kept by the quest's own-version save (2.53's reload does the same), so
    // there's no upstream file to match, and it is kept rather than dropped.
    if(s < 2)
    {
        const itemdata &now = itemsbuf[iLetterUsed], &then = g_items[iLetterUsed];

        if(now.tile != then.tile || now.csets != then.csets || now.misc != then.misc || now.frames != then.frames
           || now.speed != then.speed || now.ltm != then.ltm)
        {
            UpItem &t = items[iLetterUsed];
            t.tile = now.tile;
            t.csets = now.csets;
            t.misc_flags = now.misc;
            t.frames = now.frames;
            t.speed = now.speed;
            t.ltm = now.ltm;
        }
    }

    if(old_init_data(ctx))
        init_overrides(items);

    // names the user changed
    for(int i = 0; i < MAXITEMS; ++i)
        if(item_name_changed(i))
            items[i].name = item_string[i];

    return ZQW_OK;
}

// ---- writing --------------------------------------------------------------------------

void put_str(std::vector<byte> &o, const std::string &s)
{
    size_t n = s.size() < 255 ? s.size() : 255;
    o.push_back((byte)n);
    o.insert(o.end(), s.begin(), s.begin() + n);
}

void put_script(std::vector<byte> &o, word script, const long *args)
{
    put_le(o, script, 2);
    put_le(o, 8, 2);

    for(int j = 0; j < 8; ++j)
        put_le(o, args ? (dword)args[j] : 0, 4);

    put_le(o, 0, 2);            // no instance values
}

// 3.0's write_single_item
void put_item_30(std::vector<byte> &o, const UpItem &t)
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
    put_le(o, 0, 4);            // weaprange
    put_le(o, 0, 4);            // weapduration

    for(int q = 0; q < 10; ++q)
        put_le(o, 0, 4);        // weap_pattern

    put_le(o, 0, 4);            // duplicates
    o.push_back(0);             // drawlayer

    for(int q = 0; q < 7; ++q)
        put_le(o, 0, 4);        // hit box and offsets

    put_le(o, 0, 4);            // wpnsprite
    put_le(o, (dword)t.magiccosttimer[0], 4);
    put_le(o, (dword)t.magiccosttimer[1], 4);
    put_le(o, 0, 4);            // overrideFLAGS
    put_le(o, 0, 4);            // tilew
    put_le(o, 0, 4);            // tileh
    put_le(o, 0, 4);            // pickup
    put_le(o, 0, 2);            // pstring
    put_le(o, 0, 2);            // pickup_string_flags
    o.push_back((byte)t.cost_counter[0]);
    o.push_back((byte)t.cost_counter[1]);
    put_script(o, t.script, t.initd);
    put_script(o, t.collect_script, t.collect_args ? t.initd : NULL);
    put_script(o, 0, NULL);     // the sprite script (cleared for old quests)
    o.push_back(t.pickupflag);
    put_str(o, "");             // display_name
    put_le(o, 0, 2);            // pickup_litems
    put_le(o, 0xFFFF, 2);       // pickup_litem_level
    put_le(o, (dword)t.moveflags, 4);

    // write_weap_data
    put_le(o, 5, 2);            // V_WEAP_DATA
    put_le(o, 0, 2);            // flags
    put_le(o, (dword)t.wd_moveflags, 4);
    put_le(o, t.wd_wflags, 2);

    for(int q = 0; q < 5; ++q)
    {
        put_le(o, 0, 2);        // burnsprs
        o.push_back(0);         // light_rads
        put_le(o, 0, 4);        // light_offsets
    }

    o.push_back(0);             // glow_shape

    for(int q = 0; q < 10; ++q)
        put_le(o, 0, 4);        // override flags, tile size, hit box and offsets

    put_le(o, 0, 4);            // step
    o.push_back(0);             // unblockable
    put_le(o, 0, 4);            // timeout
    o.push_back(0);             // imitate_weapon
    o.push_back(0);             // default_defense
    o.push_back(t.lift_level);
    o.push_back(t.lift_time);
    put_le(o, (dword)t.lift_height, 4);
    put_script(o, 0, NULL);
    put_le(o, 0xFFFF, 2);       // pierce_count
    put_le(o, 0, 4);            // bounce_mult
    put_le(o, 0, 4);            // bounce_add
    put_le(o, 0, 4);            // viewport ranges
    put_le(o, 0, 4);
    put_le(o, 0, 4);            // cooldown
    put_le(o, 0, 4);            // viewport ranges
    put_le(o, 0, 4);
}

// 2.55.17's writeitems, one record
void put_item_255(std::vector<byte> &o, const UpItem &t)
{
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
    put_le(o, t.script, 2);
    o.push_back((byte)t.count);
    put_le(o, t.amount, 2);
    put_le(o, t.collect_script, 2);
    put_le(o, (word)t.setmax, 2);
    put_le(o, t.max, 2);
    o.push_back((byte)t.playsound);

    for(int j = 0; j < 8; ++j)
        put_le(o, (dword)t.initd[j], 4);

    o.push_back(t.inita[0]);
    o.push_back(t.inita[1]);

    for(int q = 0; q < 10; ++q)
        o.push_back((byte)t.wpn[q]);

    o.push_back(t.pickup_hearts);
    put_le(o, (dword)t.misc[0], 4);
    put_le(o, (dword)t.misc[1], 4);
    put_le(o, (word)t.cost_amount[0], 2);
    put_le(o, (word)t.cost_amount[1], 2);

    for(int q = 2; q < 10; ++q)
        put_le(o, (dword)t.misc[q], 4);

    o.push_back((byte)t.usesound);
    o.push_back((byte)t.usesound2);
    o.push_back(0);             // useweapon
    o.push_back(0);             // usedefence
    put_le(o, 0, 4);            // weaprange
    put_le(o, 0, 4);            // weapduration

    for(int q = 0; q < 10; ++q)
        put_le(o, 0, 4);        // weap_pattern

    put_le(o, 0, 4);            // duplicates

    for(int q = 0; q < 8; ++q)
        put_le(o, 0, 4);        // weap_initiald

    o.push_back(0);             // weap_initiala
    o.push_back(0);
    o.push_back(0);             // drawlayer

    for(int q = 0; q < 14; ++q)
        put_le(o, 0, 4);        // item and weapon hit boxes and offsets

    put_le(o, 0, 2);            // weaponscript
    put_le(o, 0, 4);            // wpnsprite
    put_le(o, (dword)t.magiccosttimer[0], 4);
    put_le(o, (dword)t.magiccosttimer[1], 4);

    for(int q = 0; q < 6; ++q)
        put_le(o, 0, 4);        // override flags and tile sizes, item and weapon

    put_le(o, 0, 4);            // pickup
    put_le(o, 0, 2);            // pstring
    put_le(o, 0, 2);            // pickup_string_flags
    o.push_back((byte)t.cost_counter[0]);
    o.push_back((byte)t.cost_counter[1]);

    // InitD labels, reset for old quests, and the sprite script's InitD
    for(int q = 0; q < 8; ++q)
    {
        char label[65];
        memset(label, 0, sizeof(label));
        sprintf(label, "InitD[%d]", q);

        for(int k = 0; k < 3; ++k)
            o.insert(o.end(), label, label + 65);

        put_le(o, 0, 4);
    }

    o.push_back(0);             // sprite_initiala
    o.push_back(0);
    put_le(o, 0, 2);            // sprite_script
    o.push_back(t.pickupflag);
    put_str(o, "");             // display_name

    for(int q = 0; q < 5; ++q)
    {
        o.push_back(0);         // burnsprs
        o.push_back(0);         // light_rads
    }
}

// ---- the edit test --------------------------------------------------------------------

// ZQN_ITEMS_EDIT_TEST (for the checks only; nothing happens unless it is set): when the newer
// save first asks for the items or the weapon sprites, some items, sprites and quest rules
// change first, as edits in ZQuest's dialogs would. That is after the load has finished (every
// load hook, then the parts' fingerprints), where a user's edits happen; from a load hook they
// would count as loaded.
//   ZQN_ITEMS_EDIT_TEST=1              the changes; the newer save then saves the changed quest
//   ZQN_ITEMS_EDIT_TEST=<file.qst>     the changes, then the quest saved in its own version to
//                                      that file with ZQuest's save (zq_save_own_format, as
//                                      File > Save). Upstream's conversion of that file is what
//                                      a newer save of the same changed quest (=1) must match.
//   ZQN_ITEMS_EDIT_RULES="34,-107,+48" quest rules flipped (n), cleared (-n) or set (+n) too
//   ZQN_ITEMS_EDIT_LOG=<file>          what was changed and how the own-version save went
// Only what the quest's own format stores is changed, so that save carries every change.

FILE *g_edit_log = NULL;

void edit_note(const char *fmt, ...)
{
    if(!g_edit_log)
        return;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_edit_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_edit_log);
}

// The section version of a part, 0 when the quest has none (a quest older than 1.93); count is
// the records it holds (all of them for a quest older than 1.93).
int section_version(dword id, int *count)
{
    const ZqQuestPart *p = zq_find_part(id);
    byte head[10];
    *count = id == ID_ITEMS ? MAXITEMS : MAXWPNS;

    if(!p || !zq_part_read(*p, 0, head, sizeof(head)))
        return 0;

    *count = head[8] | (head[9] << 8);
    return head[0] | (head[1] << 8);
}

void edit_rules(const char *e)
{
    for(const char *p = e; *p;)
    {
        char sign = 0;

        if(*p == '+' || *p == '-')
            sign = *p++;

        char *end;
        long n = strtol(p, &end, 10);

        if(end == p)
        {
            if(*p)
                ++p;

            continue;
        }

        if(n >= 0 && n < QUESTRULES_SIZE * 8)
        {
            set_bit(quest_rules, (int)n, sign == '+' ? 1 : sign == '-' ? 0 : !get_bit(quest_rules, (int)n));
            edit_note("rule %ld now %d", n, get_bit(quest_rules, (int)n) ? 1 : 0);
        }

        p = end;
    }
}

// s: the item section's version (0 before 1.93); held: the items it holds
void edit_items(int s, int held)
{
    // the look, which every version stores; upstream's conversion of a version 1 section (or
    // older) copies the letter's look to the used letter
    itemsbuf[iSword].tile = itemsbuf[iSword].tile + 1;
    itemsbuf[iLetter].csets ^= 1;

    // (the own-version save of a version 1 section keeps the change only if the used letter looks
    // the same, as 2.53's reload of that file makes it)
    if(s == 1)
        itemsbuf[iLetterUsed].csets = itemsbuf[iLetter].csets;

    edit_note("items: sword tile %ld, letter csets %d", (long)itemsbuf[iSword].tile, itemsbuf[iLetter].csets);

    if(s > 1)
    {
        strcpy(item_string[iWSword], "Edited Sword");

        // a level the conversions read (candles of level 2 and up get a flag upstream)
        itemsbuf[iBCandle].fam_type = (byte)(itemsbuf[iBCandle].fam_type > 1 ? 1 : 2);
        itemsbuf[iMKey].initiald[0] += 10000;

        // past the ones the file holds (a version 1 section can't add one: 2.53 reads the
        // custom items it adds as items of no family)
        if(held < MAXITEMS)
            itemsbuf[MAXITEMS - 1].tile = itemsbuf[MAXITEMS - 1].tile + 3;

        edit_note("items: white sword renamed, blue candle level %d, magic key InitD[0] %ld, item %d tile %ld",
                  itemsbuf[iBCandle].fam_type, (long)itemsbuf[iMKey].initiald[0], MAXITEMS - 1, (long)itemsbuf[MAXITEMS - 1].tile);
    }

    if(s > 5)
    {
        itemsbuf[iDinsFire].power = (byte)(itemsbuf[iDinsFire].power + 1);
        itemsbuf[iBrang].flags ^= ITEM_EDIBLE;
        itemsbuf[iBombs].misc1 += 10;
        itemsbuf[iWand].magic = (byte)(itemsbuf[iWand].magic + 2);
        edit_note("items: divine fire power %d, boomerang flags 0x%x, bomb misc1 %ld, wand cost %d", itemsbuf[iDinsFire].power,
                  (unsigned)itemsbuf[iBrang].flags, (long)itemsbuf[iBombs].misc1, itemsbuf[iWand].magic);
    }

    if(s >= 12)
    {
        itemsbuf[iHammer].usesound = (byte)(itemsbuf[iHammer].usesound == WAV_BOMB ? WAV_HAMMER : WAV_BOMB);
        edit_note("items: hammer sound %d", itemsbuf[iHammer].usesound);
    }

    if(s >= 15)
    {
        // 3.0 moves a bomb's misc 4 to 6 into its weapon's lifting
        itemsbuf[iBombs].misc4 = itemsbuf[iBombs].misc4 ? 0 : 2;
        itemsbuf[iBombs].misc5 = 24;
        itemsbuf[iBombs].misc6 = 12;
        edit_note("items: bomb misc4-6 %ld %ld %ld", (long)itemsbuf[iBombs].misc4, (long)itemsbuf[iBombs].misc5, (long)itemsbuf[iBombs].misc6);
    }
}

// s: the weapon section's version (0 before 1.93); count: the sprites it holds
void edit_weapons(int s, int count, word zv)
{
    wpnsbuf[wSWORD].tile = wpnsbuf[wSWORD].tile + 1;
    wpnsbuf[wARROW].csets ^= 1;
    wpnsbuf[wBRANG].frames = (byte)(wpnsbuf[wBRANG].frames + 1);
    wpnsbuf[wBRANG].speed = (byte)(wpnsbuf[wBRANG].speed + 2);

    // sprites upstream copies into others after reading them: the explosion into the super
    // bomb's before section version 2, the hookshot's head into the longshot's before 2.10.
    // 2.53's reload of the quest saved in its own version makes the same copies, so that save
    // keeps the change only with the copy made here too.
    wpnsbuf[wBOOM].type ^= 1;
    wpnsbuf[wHSHEAD].tile = wpnsbuf[wHSHEAD].tile + 2;

    if(s < 2 && wSBOOM < count)
        wpnsbuf[wSBOOM] = wpnsbuf[wBOOM];

    if(zv < 0x210)
        wpnsbuf[wLSHEAD] = wpnsbuf[wHSHEAD];

    edit_note("weapons: sword tile %ld, arrow csets %d, boomerang frames %d speed %d, explosion type %d, hookshot head tile %ld",
              (long)wpnsbuf[wSWORD].tile, wpnsbuf[wARROW].csets, wpnsbuf[wBRANG].frames, wpnsbuf[wBRANG].speed, wpnsbuf[wBOOM].type,
              (long)wpnsbuf[wHSHEAD].tile);

    if(s > 2)
    {
        strcpy(weapon_string[wFIRE], "Edited Fire");
        edit_note("weapons: fire renamed");
    }
}

void edit_test()
{
    static bool done = false;

    if(done)
        return;

    done = true;
    const char *e = getenv("ZQN_ITEMS_EDIT_TEST");

    if(!e || !*e)
        return;

    const char *log = getenv("ZQN_ITEMS_EDIT_LOG");
    g_edit_log = log && *log ? fopen(log, "a") : NULL;
    edit_note("quest version 0x%x build %d", zq_quest_file.zelda_version, zq_quest_file.build);

    const char *rules = getenv("ZQN_ITEMS_EDIT_RULES");

    if(rules && *rules)
        edit_rules(rules);

    int held, count;
    int is = section_version(ID_ITEMS, &held);
    int ws = section_version(ID_WEAPONS, &count);
    edit_note("item section v%d holding %d, weapon section v%d holding %d", is, held, ws, count);
    edit_items(is, held);
    edit_weapons(ws, count, zq_quest_file.zelda_version);

    if(strcmp(e, "1"))
    {
        std::string why;
        int r = zq_save_own_format(e, true, why);
        edit_note("own-version save to %s: %s %s", e, r ? "NOT SAVED" : "SAVED", why.c_str());
    }

    if(g_edit_log)
        fclose(g_edit_log);

    g_edit_log = NULL;
}

int encode_items(const ZqNewerContext &ctx, std::vector<byte> &out)
{
    Rules R;

    if(!load_rules(R, ctx))
    {
        zqw_ig_refuse("Items: the quest's rules couldn't be read from the opened file.");
        return ERR_PARSE;
    }

    ItemSet set;
    int rc = build_items(ctx, R, set);

    if(rc != ZQW_OK)
    {
        if(zqw_last_error.empty())
            zqw_ig_refuse("Items: the opened file's item section couldn't be read.");

        return rc;
    }

    std::vector<UpItem> &items = set.items;
    std::vector<byte> body;

    if(ctx.target == T30)
    {
        // itemsbuf.normalize(): items equal to a fresh one drop off the end
        std::vector<std::vector<byte> > recs(MAXITEMS);
        UpItem blank;
        blank.fresh();
        std::vector<byte> blank_bytes;
        put_item_30(blank_bytes, blank);
        int n = 0;

        for(int i = 0; i < MAXITEMS; ++i)
        {
            if(!items[i].present)
                continue;

            put_item_30(recs[i], items[i]);

            if(recs[i] != blank_bytes)
                n = i + 1;
        }

        put_le(body, (dword)n, 2);

        for(int i = 0; i < n; ++i)
        {
            if(items[i].present)
                body.insert(body.end(), recs[i].begin(), recs[i].end());
            else
                body.insert(body.end(), blank_bytes.begin(), blank_bytes.end());
        }

        out.clear();
        put_le(out, 71, 2);     // V_ITEMS
        put_le(out, 71, 2);     // the cversion is the version now
    }
    else
    {
        put_le(body, MAXITEMS, 2);

        for(int i = 0; i < MAXITEMS; ++i)
        {
            char nm[64];
            memset(nm, 0, sizeof(nm));
            strncpy(nm, items[i].name.c_str(), 63);
            body.insert(body.end(), nm, nm + 64);
        }

        for(int i = 0; i < MAXITEMS; ++i)
            put_item_255(body, items[i]);

        out.clear();
        put_le(out, 59, 2);     // V_ITEMS
        put_le(out, 15, 2);     // CV_ITEMS
    }

    put_le(out, (dword)body.size(), 4);
    out.insert(out.end(), body.begin(), body.end());
    return ZQW_OK;
}

int zqn_items(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    edit_test();
    std::vector<byte> out;
    int rc = encode_items(ctx, out);
    return rc != ZQW_OK ? rc : write_part(f, ID_ITEMS, out);
}

ZqNewerRegistration g_reg_items(ID_ITEMS, zqn_items, "items for 2.55.17 and 3.0");

// ---- weapon sprites ---------------------------------------------------------------------

// A sprite's script field (2.55.17 only), an upstream bug: 2.55.17's readweapons reads each
// sprite into a local wpndata that is never cleared, and reads its script only from section
// version 7 on, so every sprite a quest of 2.53.1 or older stores (and every sprite copied from
// one) gets whatever that stack memory held, and its writer writes that back. It depends on
// what ran before: 10757 or 10804 in upstream's conversions of the pack's quests, and 49840,
// 49887, 4137 or 4184, changing from run to run, in its conversions of the same quests saved
// with a few items changed. These files hold 0, what a clean read gives and what 3.0 (which
// resets each sprite before reading it) holds.

struct UpWpn
{
    std::string name;
    long tile;
    byte misc, csets, frames, speed, type;
    word script;            // 2.55 only: always 0 for these quests
    bool present;           // 3.0: in upstream's list
};

int encode_weapons(const ZqNewerContext &ctx, std::vector<byte> &out)
{
    int target = ctx.target;
    word version = ctx.src_zelda_version;
    byte build = ctx.src_build;
    int s = 0, count = MAXWPNS;
    std::vector<byte> part;

    if(version < 0x186)
        count = version < 0x185 ? 32 : 64;

    if(version > 0x192)
    {
        if(!ctx.src_part || !read_part(*ctx.src_part, part) || part.size() < 10)
        {
            zqw_ig_refuse("Weapon sprites: the opened file's section couldn't be read.");
            return ERR_PARSE;
        }

        s = (word)get_le(part, 0, 2);
        count = (word)get_le(part, 8, 2);

        if(s > 6 || count > MAXWPNS)
        {
            zqw_ig_refuse("Weapon sprites: the quest's section (version %d, %d sprites) isn't one this save knows.", s, count);
            return ERR_PARSE;
        }
    }

    std::vector<UpWpn> w(MAXWPNS);
    In names(part, 10);

    for(int i = 0; i < MAXWPNS; ++i)
    {
        UpWpn &t = w[i];
        t.tile = 0;
        t.misc = t.csets = t.frames = t.speed = t.type = 0;
        t.script = 0;
        t.present = true;

        if(s <= 2)
            t.name = reset_name(i, false);
        else if(i < count)
            t.name = names.name(target == T30 ? 64 : 63);
        else
            t.present = false;      // nothing upstream sets: an empty sprite

        // 2.53 reads and converts what's stored exactly as upstream does (and ZQuest's data is
        // the user's, changed or not)
        if(i < count)
        {
            t.tile = wpnsbuf[i].tile;
            t.misc = wpnsbuf[i].misc;
            t.csets = wpnsbuf[i].csets;
            t.frames = wpnsbuf[i].frames;
            t.speed = wpnsbuf[i].speed;
            t.type = wpnsbuf[i].type;
        }
    }

    // What upstream does after reading the names and the sprites. A sprite it copies from
    // another takes the user's changes to that one along, as 2.53's reload of the quest saved in
    // its own version would.
    if(s > 2 && s < 4)
    {
        w[iwHover].name = up_weapon_name(iwHover);
        w[wFIREMAGIC].name = up_weapon_name(wFIREMAGIC);
        w[iwHover].present = w[wFIREMAGIC].present = true;
    }

    if(s > 2 && s < 5)
    {
        w[iwQuarterHearts].name = up_weapon_name(iwQuarterHearts);
        w[iwQuarterHearts].present = true;
    }

    // the copies made after reading: 3.0 copies the names along with the sprites
    struct Copy { int to, from; bool when; };
    const Copy copies[] =
    {
        { wSBOOM, wBOOM, s < 2 },
        { wHSCHAIN_V, wHSCHAIN_H, version < 0x192 || (version == 0x192 && build < 129) },
        { wLSHEAD, wHSHEAD, version < 0x210 },
        { wLSCHAIN_H, wHSCHAIN_H, version < 0x210 },
        { wLSHANDLE, wHSHANDLE, version < 0x210 },
        { wLSCHAIN_V, wHSCHAIN_V, version < 0x210 },
    };

    for(size_t k = 0; k < sizeof(copies) / sizeof(copies[0]); ++k)
    {
        if(!copies[k].when)
            continue;

        UpWpn &a = w[copies[k].to];
        const UpWpn &b = w[copies[k].from];
        std::string nm = a.name;
        bool present = a.present;
        a = b;

        if(target == T255)
        {
            a.name = nm;
            a.present = present;
        }
        else
            a.present = true;
    }

    if(s < 5)
    {
        w[iwQuarterHearts].tile = 1;
        w[iwQuarterHearts].csets = 1;
        w[iwQuarterHearts].present = true;
    }

    // The sprites and names the user changed are ZQuest's. Where those steps would replace them
    // (a sprite upstream copies into, or a name it resets), the quest's own-version save can't
    // keep the change either (2.53's reload does the same), so there's no upstream file to
    // match, and the change is kept rather than dropped: upstream doesn't repeat those steps
    // on a 2.55 or 3.0 file.
    for(int i = 0; i < MAXWPNS; ++i)
    {
        if(wpn_changed(i))
        {
            w[i].tile = wpnsbuf[i].tile;
            w[i].misc = wpnsbuf[i].misc;
            w[i].csets = wpnsbuf[i].csets;
            w[i].frames = wpnsbuf[i].frames;
            w[i].speed = wpnsbuf[i].speed;
            w[i].type = wpnsbuf[i].type;
            w[i].present = true;
        }

        if(wpn_name_changed(i))
        {
            w[i].name = weapon_string[i];
            w[i].present = true;
        }
    }

    std::vector<byte> body;

    if(target == T30)
    {
        // sprite_data_buf.normalize(): empty sprites drop off the end
        int n = 0;

        for(int i = 0; i < MAXWPNS; ++i)
        {
            const UpWpn &t = w[i];

            if(t.present && (!t.name.empty() || t.tile || t.misc || t.csets || t.frames || t.speed || t.type))
                n = i + 1;
        }

        put_le(body, (dword)n, 2);

        for(int i = 0; i < n; ++i)
        {
            const UpWpn &t = w[i];
            put_str(body, t.present ? t.name : std::string());
            body.push_back(t.misc);
            body.push_back(t.csets);
            body.push_back(t.frames);
            body.push_back(t.speed);
            body.push_back(t.type);
            put_le(body, 0, 2);     // script
            put_le(body, (dword)t.tile, 4);
        }

        out.clear();
        put_le(out, 9, 2);      // V_WEAPONS
        put_le(out, 9, 2);
    }
    else
    {
        put_le(body, MAXWPNS, 2);

        for(int i = 0; i < MAXWPNS; ++i)
        {
            char nm[64];
            memset(nm, 0, sizeof(nm));
            strncpy(nm, w[i].name.c_str(), 63);
            body.insert(body.end(), nm, nm + 64);
        }

        for(int i = 0; i < MAXWPNS; ++i)
        {
            const UpWpn &t = w[i];
            body.push_back(t.misc);
            body.push_back(t.csets);
            body.push_back(t.frames);
            body.push_back(t.speed);
            body.push_back(t.type);
            put_le(body, t.script, 2);
            put_le(body, (dword)t.tile, 4);
        }

        out.clear();
        put_le(out, 8, 2);      // V_WEAPONS
        put_le(out, 1, 2);      // CV_WEAPONS
    }

    put_le(out, (dword)body.size(), 4);
    out.insert(out.end(), body.begin(), body.end());
    return ZQW_OK;
}

int zqn_weapons(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    edit_test();
    std::vector<byte> out;
    int rc = encode_weapons(ctx, out);
    return rc != ZQW_OK ? rc : write_part(f, ID_WEAPONS, out);
}

ZqNewerRegistration g_reg_weapons(ID_WEAPONS, zqn_weapons, "weapon sprites for 2.55.17 and 3.0");

} // namespace

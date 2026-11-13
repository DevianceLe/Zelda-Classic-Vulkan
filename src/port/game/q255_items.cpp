// ITEM section of a 2.55 / 3.0 quest, section versions 25 to 71.
//
// Kept beside itemsbuf (q30_triggers.cpp): usesound2 (v49+)
//
// Read and dropped, because 2.53's itemdata has no field for them:
//   cost_amount[1] (v53), cost_counter[1] (v53), magiccosttimer[0..1] (v27+)
//   weapon: imitate_weapon, default_defense (v26..62, then in the weapon block);
//     weaprange, weapduration, weap_pattern[10] (v26+)
//   weapon: duplicates (v27+), InitA[2] (v27..66), wpnsprite (v27+)
//   weapon: InitD[8], hit box and offsets (7 values), script (v27..62)
//   weapon: override flags, tile width, tile height (v29..62)
//   weapon: burn sprites (v58..62), light radii (v59..62), moveflags (v62)
//   weapon block (v63+): flags, moveflags, wflags, 5 x burn sprite + light radius
//     + light offset, glow shape, override flags, tile size, hit box and offsets,
//     step, unblockable, timeout, imitate_weapon, default_defense, lift level /
//     time / height, script and its args, pierce count, bounce (block v2+),
//     viewport ranges (block v4+)
//   item: drawlayer, hit box and offsets (7 values) (v27+); override flags, tile width, tile height (v28+)
//   item: display_name (v57+), pickup_litems, pickup_litem_level (v60+),
//     moveflags (v62+), cooldown (v64+), viewport suspend / despawn range (v70+)
//   pickup: pickup (v30+), pstring (v32+), pickup_string_flags (v33+), pickupflag (v48+)
//   sprite script: 8 sets of label bytes + 1 long, script number (v44+), InitA[2] (v44..66)
//   script settings (v71+): the sprite script, and each script's instance values;
//     collect script's args are kept only when the item has no action script,
//     as 2.53 gives both scripts the same InitD
//
// Wider in the file than in 2.53, narrowed on load:
//   name (text from v67): cut to 63 characters
//   tile (long from v36): bounded to 0..NEWMAXTILES-1, 2.55 allows up to 214500
//   type (long from v31): anything 2.53 has no family for, such as the scripted
//     weapons at 256 and up, becomes itype_misc; truncating would make 256 a sword
//   power (long from v31): bounded to 0..255
//   flags (long from v41): only the low 16 bits are kept, item_flag9 and up are lost
//   count: counters 32 and up (added after 2.53) become "no counter"
//   script numbers: anything past 2.53's 256 item scripts becomes "no script"
//   playsound, usesound (word from v66), weapon sprites (word from v68): a value
//     over 255 has nothing to refer to in 2.53, so it becomes 0
//   cost_amount[0] (short in v53): bounded to 0..255 into magic
//   cost_counter[0] (v34+): 2.53 only charges magic or rupees, chosen by
//     ITEM_RUPEE_MAGIC, so that flag is set from it; any other counter makes the
//     item free
//
// update_old_item's fixes for older section versions are not applied.

#include "precompiled.h" //always first

#include "q30_scripts.h"

#include <string.h>

#include "q255.h"
#include "sfx.h"
#include "q30_triggers.h"

extern itemdata *itemsbuf;
extern char *item_string[ITEMCNT];

static long item_bound(long v, long lo, long hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// An sfx or weapon sprite number that doesn't fit 2.53's byte.
static byte item_id_byte(word v)
{
    return v <= 255 ? (byte)v : 0;
}

static word g_item_sprite_script, g_item_weapon_script;
static dword g_item_override_flags;     // kept for scripts (itemdata->SizeFlags)
static std::string g_item_display_name;
static long g_item_sprite_initd[8], g_item_weapon_initd[8];

static word item_script_id(word v)
{
    return v < NUMSCRIPTITEM ? v : 0;
}

// 2.53 keeps 32 counters; the file's byte is signed, with -1 for none.
// The counter an item adds to: 2.53's 32, or the newer counters past them that gamedata keeps
// (q30_hi_counter, up to Q30_MAX_COUNTERS); anything else none.
static char item_counter(byte v)
{
    signed char c = (signed char)v;
    return (c < 0 || c >= Q30_MAX_COUNTERS) ? (char)-1 : (char)c;
}

// Counter numbers are the same in both versions: 1 is rupees, 4 is magic.
static void item_apply_cost_counter(itemdata &it, signed char counter)
{
    if(counter == 1)
    {
        it.flags |= ITEM_RUPEE_MAGIC;
        return;
    }

    it.flags &= ~ITEM_RUPEE_MAGIC;

    if(counter != 4)
        it.magic = 0;
}

// p_getvar(script_config*): script number, run_args as a sized array (at most 8
// are read, as upstream does), then a word-sized map of word key to long value.
// args may be NULL.
static word read_script_config(Q255Section &s, long *args)
{
    word script = s.igetw();
    word argc = s.igetw();

    for(int j = 0; j < argc && j < 8; ++j)
    {
        long v = s.igetl_s();

        if(args)
            args[j] = v;
    }

    word entries = s.igetw();
    s.skip((size_t)entries * (2 + 4));
    return script;
}

// read_weap_data
static void skip_weapon_data(Q255Section &s)
{
    word v = s.igetw();
    s.skip(2 + 4 + 2);          // flags, moveflags, wflags

    for(int q = 0; q < 5; ++q)  // WPNSPR_MAX
    {
        s.skip(v < 1 ? 1 : 2);  // burn sprite
        s.skip(1);              // light radius

        if(v >= 3)
            s.skip(4);          // light offset
    }

    s.skip(1);                  // glow shape
    s.skip(10 * 4);             // override flags, tile size, hit box and offsets
    s.skip(4);                  // step
    s.skip(1);                  // unblockable
    s.skip(4);                  // timeout
    s.skip(2);                  // imitate_weapon, default_defense
    s.skip(2 + 4);              // lift level, time, height

    if(v < 5)
    {
        g_item_weapon_script = s.igetw();
        for(int i = 0; i < 8; ++i)
            g_item_weapon_initd[i] = s.igetl_s();
    }
    else
    {
        g_item_weapon_script = read_script_config(s, g_item_weapon_initd);
    }

    s.skip(2);                  // pierce count

    if(v >= 2)
        s.skip(4 + 4);          // bounce

    if(v >= 4)
        s.skip(4 + 4);          // viewport ranges
}

static dword g_item_raw_flags = 0;   // all 32 bits; itemdata keeps only the low 16
static long g_item_pickup = 0;       // the item's own pickup flags, how they apply (pickupflag),
static int g_item_pickup_mode = 0;   // its pickup string and that string's flags, all kept for
static int g_item_pstring = 0, g_item_psflags = 0;     // the player (q30_set_item_pickup)
static int g_cost_counter[2] = { -1, -1 };  // upstream's two item costs (counter, amount),
static int g_cost_amount[2] = { 0, 0 };     // kept for the player (q30_set_item_cost)
static long g_true_family = 0;   // the family as stored, which itemdata may not hold
static int g_item_usesound2 = 0; // the second use sound (the switch hook's switch sound)

#ifdef ZC_PLAYER_BUILD
// upstream's item flag bits past what 2.53 names (item_flag9.. are bits 16..)
enum
{
    UF1 = 0x10, UF2 = 0x20, UF3 = 0x400, UF4 = 0x800, UF5 = 0x1000, UF6 = 0x2000, UF7 = 0x4000,
    UF8 = 0x8000, UF9 = 0x10000, UF10 = 0x20000, UF11 = 0x40000
};

static void set_flag(dword &f, dword bit, bool on)
{
    if(on)
        f |= bit;
    else
        f &= ~bit;
}

static bool rule(int id) { return q30_quest_rule(id); }

extern word quest_header_zelda_version;

// upstream's readitems (qst_items.cpp, "Port quest rules to items" and the s_version fixes
// after it), the parts that set flags; rule numbers are upstream's, the same as 2.53's for these
static void update_old_item_flags(int v, long family, int level, dword &f)
{
    enum { R_FIREPROOF = 33, R_NONBUBBLEMED = 52, R_NONBUBBLETRI = 54, R_Z3BRANG = 61, R_NOITEMMELEE = 62,
           R_QUICKSWORD = 65, R_NODIVING = 90, R_TEMPCANDLE = 97, R_RINGDAMAGE = 94, R_BRANGPICKUP = 107,
           R_SLASHFLIP = 112, R_SLOWCHARGE = 114, R_NOWANDMELEE = 115, R_SWORDMIRROR = 117,
           R_LENSENEMIES = 119, R_ITEMBUBBLE = 132, R_MELEEMAGIC = 156, R_WHIRLMIRROR = 30,
           R_RAFTLENS = 28, R_LENSHINTS = 22 };

    if(v <= 37)
    {
        if(family == itype_flippers)
            set_flag(f, UF1, rule(R_NODIVING));
        else if(family == itype_sword || family == itype_wand)
            set_flag(f, UF5, rule(R_QUICKSWORD));
        else if(family == itype_book || family == itype_candle)
            set_flag(f, UF3, rule(R_FIREPROOF));
    }

    if(v < 38)
    {
        if(family == itype_brang || family == itype_hookshot)
        {
            set_flag(f, UF4, rule(R_BRANGPICKUP));
            set_flag(f, UF5 | UF6, rule(R_Z3BRANG));
        }
        else if(family == itype_arrow)
        {
            set_flag(f, UF4, rule(R_BRANGPICKUP));
            set_flag(f, UF2, !rule(R_Z3BRANG));
        }
    }

    if(v < 39)
    {
        if(family == itype_dinsfire || family == itype_book || family == itype_candle)
            set_flag(f, UF5, rule(R_TEMPCANDLE));
        else if(family == itype_potion)
        {
            if(rule(R_NONBUBBLEMED))
                f &= ~(UF3 | UF4);
            else
            {
                f |= UF3;
                set_flag(f, UF4, rule(R_ITEMBUBBLE));
            }
        }
        else if(family == itype_triforcepiece)
        {
            if(rule(R_NONBUBBLETRI))
            {
                f |= UF3;
                set_flag(f, UF4, rule(R_ITEMBUBBLE));
            }
            else
                f &= ~(UF3 | UF4);
        }
    }

    if(v < 40)
    {
        if(family == itype_ring)
            set_flag(f, UF1, rule(R_RINGDAMAGE));
        else if(family == itype_candle || family == itype_sword || family == itype_wand || family == itype_cbyrna)
            set_flag(f, UF8, rule(R_SLASHFLIP));

        if(family == itype_sword || family == itype_wand || family == itype_hammer)
            set_flag(f, UF7, rule(R_NOITEMMELEE));
        else if(family == itype_cbyrna)
            f |= UF7;
    }

    if(v < 41 && family == itype_sword)
    {
        set_flag(f, UF9, rule(R_SWORDMIRROR));
        set_flag(f, UF10, rule(R_SLOWCHARGE));
    }

    if(v < 42)
    {
        if(family == itype_wand)
        {
            set_flag(f, UF3, rule(R_NOWANDMELEE));
            f &= ~UF6;
        }
        else if(family == itype_hammer)
            f &= ~UF3;
        else if(family == itype_cbyrna)
        {
            f |= UF3;
            f &= ~UF6;
        }
        else if(family == itype_sword)
            set_flag(f, UF6, rule(R_MELEEMAGIC));
    }

    if(v < 43 && family == itype_whistle)
        set_flag(f, UF3, rule(R_WHIRLMIRROR));

    if(v < 35 && family == itype_lens)
    {
        if(rule(R_RAFTLENS)) f |= UF4;
        if(rule(R_LENSHINTS)) f |= UF1;
        if(rule(R_LENSENEMIES)) f |= UF5;
    }

    if(v < 52 && family == itype_shield)
        f |= UF1;                       // 'Block Front'

    if(v < 56)
    {
        if(family == itype_dinsfire)
        {
            set_flag(f, UF9, quest_header_zelda_version < 0x255);   // Strong Fire
            set_flag(f, UF10, quest_header_zelda_version < 0x250);  // Magic Fire
            f |= UF11;                                              // Divine Fire
        }
        else if(family == itype_candle)
        {
            set_flag(f, UF9, level > 1);
            f &= ~(UF10 | UF11);
        }
        else if(family == itype_book)
        {
            f |= UF9 | UF10;
            f &= ~UF11;
        }
    }
}
#endif

static void read_item(Q255Section &s, itemdata &it)
{
    long tile = s.version > 35 ? s.igetl_s() : (long)s.igetw();
    q255_note_tile("item", (unsigned long)tile);
    it.tile = (tile_id)item_bound(tile, 0, NEWMAXTILES - 1);
    it.misc = s.getc();
    it.csets = s.getc();
    it.frames = s.getc();
    it.speed = s.getc();
    it.delay = s.getc();
    it.ltm = s.igetl_s();

    long family = s.version >= 31 ? s.igetl_s() : (long)s.getc();
    g_true_family = family;
    if(family < 0 || family >= itype_max)
        family = itype_misc;
it.family = (byte)family;

    it.fam_type = s.getc();

    if(s.version >= 31)
        it.power = (byte)item_bound(s.igetl_s(), 0, 255);
    else
        it.power = s.getc();

    g_item_raw_flags = 0;

    if(s.version >= 41)
    {
        g_item_raw_flags = (dword)s.igetl();
        it.flags = (word)(g_item_raw_flags & 0xFFFF);
    }
    else
    {
        it.flags = s.igetw();
        g_item_raw_flags = it.flags;
    }

#ifdef ZC_PLAYER_BUILD
    // upstream's readitems fixes for older section versions: behaviours that were quest rules
    // became item flags, set from the rules for items saved before them. The player keeps all
    // 32 bits (q30_item_flags); ZQuest keeps the flags as stored, so its re-saves stay exact.
    {
        dword f = g_item_raw_flags;
        update_old_item_flags(s.version, family, it.fam_type, f);
        g_item_raw_flags = f;
        it.flags = (word)(f & 0xFFFF);
    }
#endif

    if(s.version < 71)
        it.script = item_script_id(s.igetw());

    it.count = item_counter(s.getc());
    it.amount = s.igetw();

    if(s.version < 71)
        it.collect_script = item_script_id(s.igetw());

    it.setmax = s.igetw_s();
    it.max = s.igetw();

    if(s.version >= 66)
        it.playsound = item_id_byte(s.igetw());
    else
        it.playsound = s.getc();

    if(s.version < 71)
    {
        for(int j = 0; j < 8; ++j)
            it.initiald[j] = s.igetl_s();
    }

    if(s.version < 67)
    {
        for(int j = 0; j < 2; ++j)
            it.initiala[j] = s.getc();
    }

    byte *const wpns[10] =
    {
        &it.wpn, &it.wpn2, &it.wpn3, &it.wpn4, &it.wpn5,
        &it.wpn6, &it.wpn7, &it.wpn8, &it.wpn9, &it.wpn10
    };

    for(int j = 0; j < 10; ++j)
        *wpns[j] = s.version >= 68 ? item_id_byte(s.igetw()) : s.getc();

    it.pickup_hearts = s.getc();
    it.misc1 = s.igetl_s();
    it.misc2 = s.igetl_s();

    if(s.version >= 53)
    {
        g_cost_amount[0] = s.igetw_s();
        it.magic = (byte)item_bound(g_cost_amount[0], 0, 255);
        g_cost_amount[1] = s.igetw_s();     // cost_amount[1], kept for the player
    }
    else
    {
        it.magic = s.getc();
        g_cost_amount[0] = it.magic;
    }

    it.misc3 = s.igetl_s();
    it.misc4 = s.igetl_s();
    it.misc5 = s.igetl_s();
    it.misc6 = s.igetl_s();
    it.misc7 = s.igetl_s();
    it.misc8 = s.igetl_s();
    it.misc9 = s.igetl_s();
    it.misc10 = s.igetl_s();

    if(s.version >= 66)
    {
        it.usesound = item_id_byte(s.igetw());
        g_item_usesound2 = s.igetw();
    }
    else
    {
        it.usesound = s.getc();

        if(s.version >= 49)
            g_item_usesound2 = s.getc();
    }

    if(s.version >= 26)
    {
        if(s.version < 63)
            s.skip(2);          // imitate_weapon, default_defense

        s.skip(4 + 4);          // weaprange, weapduration
        s.skip(10 * 4);         // weap_pattern
    }

    if(s.version >= 27)
    {
        s.skip(4);              // duplicates

        if(s.version < 63)
        {
            for(int i = 0; i < 8; ++i)
                g_item_weapon_initd[i] = s.igetl_s();   // weapon InitD
        }

        if(s.version < 67)
            s.skip(2);          // weapon InitA

        s.skip(1);              // drawlayer
        s.skip(7 * 4);          // item hit box and offsets

        if(s.version < 63)
        {
            s.skip(7 * 4);      // weapon hit box and offsets
            g_item_weapon_script = s.igetw();           // weapon script
        }

        s.skip(4);              // wpnsprite
        s.skip(s.version >= 53 ? 2 * 4 : 4);    // magiccosttimer
    }

    if(s.version >= 28)
    {
        g_item_override_flags = s.igetl();     // override flags
        s.skip(2 * 4);          // tile width, tile height
    }
    if(s.version >= 29 && s.version < 63)
        s.skip(3 * 4);          // the same for the weapon
    if(s.version >= 30)
        g_item_pickup = s.igetl_s();            // pickup (the item's own pickup flags)
    if(s.version >= 32)
        g_item_pstring = s.igetw();             // pstring
    if(s.version >= 33)
        g_item_psflags = s.igetw();             // pickup_string_flags

    if(s.version >= 34)
    {
        signed char counter = (signed char)s.getc();
        g_cost_counter[0] = counter;

        if(s.version >= 53)
            g_cost_counter[1] = (signed char)s.getc();      // cost_counter[1], kept for the player

        item_apply_cost_counter(it, counter);
    }
    else    // upstream's default counter for older items: rupees, magic, or none
    {
        g_cost_counter[0] = (it.flags & ITEM_RUPEE_MAGIC) ? 1 : q30_quest_rule(qr_ENABLEMAGIC) ? 4 : -1;

        if(g_cost_counter[0] < 0)
            g_cost_amount[0] = 0;
    }

    if(s.version < 53)      // upstream's second cost for older items: ammunition
    {
        const long fam = g_true_family;
        g_cost_counter[1] = fam == itype_arrow ? 3 : fam == itype_bomb ? 2 : fam == itype_sbomb ? 6 : -1;
        g_cost_amount[1] = g_cost_counter[1] >= 0 ? 1 : 0;
    }

    if(s.version >= 71)
    {
        long collect_args[8];
        memset(collect_args, 0, sizeof(collect_args));

        it.script = item_script_id(read_script_config(s, it.initiald));
        it.collect_script = item_script_id(read_script_config(s, collect_args));
        g_item_sprite_script = read_script_config(s, g_item_sprite_initd);    // sprite script

        if(it.script == 0 && it.collect_script != 0)
            memcpy(it.initiald, collect_args, sizeof(collect_args));
    }
    else if(s.version >= 44)
    {
        s.skip(8 * (65 + (s.version < 63 ? 65 : 0) + 65 + 4));  // script labels, one long each

        if(s.version < 67)
            s.skip(2);          // sprite InitA

        g_item_sprite_script = s.igetw();              // sprite script
    }

    if(s.version >= 48)
        g_item_pickup_mode = s.getc();          // pickupflag: how the pickup flags apply

    if(s.version >= 57)
        g_item_display_name = s.getcstr();     // display_name, kept for scripts and subscreens

    if(s.version >= 58 && s.version < 63)
        s.skip(5 * (s.version >= 59 ? 2 : 1));  // burn sprites, light radii

    if(s.version >= 60)
    {
        s.skip(s.version >= 65 ? 2 : 1);        // pickup_litems
        s.skip(2);              // pickup_litem_level
    }

    if(s.version >= 62)
    {
        s.skip(4);              // moveflags

        if(s.version < 63)
            s.skip(4);          // weapon moveflags
    }

    if(s.version >= 63)
        skip_weapon_data(s);

    if(s.version >= 64)
        s.skip(4);              // cooldown

    if(s.version >= 70)
        s.skip(4 + 4);          // viewport suspend / despawn range
}

int q255_read_items(Q255Section &s)
{
    if(s.keep)
    {
        q30_clear_item_families();
        q30_clear_item_extras();
        q30_clear_high_items();
        q30_set_item_usesound2(-1, 0);
        q30_set_item_bunny(-1, false);
        q30_set_item_pickup(-1, 0, 0, 0, 0);
        q30_set_item_flags32(-1, 0);
        q30_set_item_cost(-1, 0, 0, 0, 0);
    }

    if(s.version < 25 || s.version > 71)
        return Q255_NOT_HANDLED;

    word items_to_read = s.igetw();

    if(s.failed)
        return qe_invalid;

    // Up to v66 all the names come first; from v67 each item starts with its own.
    std::vector<std::string> high_names;    // items past MAXITEMS, for q30_set_high_item

    if(s.version < 67)
    {
        for(int i = 0; i < items_to_read; ++i)
        {
            char name[64];
            s.read(name, 64);

            if(s.failed)
                return qe_invalid;

            name[63] = 0;

            if(s.keep && i < MAXITEMS)
                strcpy(item_string[i], name);
            else if(s.keep)
                high_names.push_back(name);
        }
    }

    if(s.keep)
    {
        // Items the file doesn't cover get their defaults.
        for(int i = 0; i < MAXITEMS; ++i)
        {
            memset(&itemsbuf[i], 0, sizeof(itemdata));
            itemsbuf[i].count = -1;
            itemsbuf[i].playsound = WAV_SCALE;
            reset_itembuf(&itemsbuf[i], i);

            if(i >= items_to_read)
                reset_itemname(i);
        }
    }

    for(int i = 0; i < items_to_read; ++i)
    {
        std::string high_name;

        if(s.version >= 67)
        {
            std::string name = s.getcstr();

            if(s.failed)
                return qe_invalid;

            if(s.keep && i < MAXITEMS)
            {
                strncpy(item_string[i], name.c_str(), 63);
                item_string[i][63] = 0;
            }
            else
                high_name = name;
        }
        else if(i >= MAXITEMS && i - MAXITEMS < (int)high_names.size())
            high_name = high_names[i - MAXITEMS];

        itemdata tempitem;
        memset(&tempitem, 0, sizeof(itemdata));
        g_item_sprite_script = g_item_weapon_script = 0;
        g_item_override_flags = 0;
        g_item_usesound2 = 0;
        g_item_pickup = g_item_pickup_mode = g_item_pstring = g_item_psflags = 0;
        g_cost_counter[0] = g_cost_counter[1] = -1;
        g_cost_amount[0] = g_cost_amount[1] = 0;
        g_item_display_name.clear();
        memset(g_item_sprite_initd, 0, sizeof(g_item_sprite_initd));
        memset(g_item_weapon_initd, 0, sizeof(g_item_weapon_initd));
        read_item(s, tempitem);

        if(s.failed)
            return qe_invalid;

#ifdef ZC_PLAYER_BUILD
        if(getenv("ZC_LIGHTSCAN") && i < MAXITEMS && (tempitem.family == itype_candle || tempitem.family == itype_book))
            al_trace("q30 light: item %d '%s' family %d flag2 %d flag5 %d rule %d newdark %d itemv %d\n", i, item_string[i], tempitem.family,
                     (tempitem.flags & ITEM_FLAG2) != 0, (tempitem.flags & ITEM_FLAG5) != 0, q30_quest_rule(qr_TEMPCANDLELIGHT),
                     q30_quest_rule(237), s.version);
#endif

        if(s.keep && i < MAXITEMS)
        {
            memcpy(&itemsbuf[i], &tempitem, sizeof(itemdata));
            q30_set_item_family(i, g_true_family);
            q30_set_item_usesound2(i, g_item_usesound2);
            q30_set_item_passive_script(i, (g_item_raw_flags & (1u << 23)) != 0);
            q30_set_item_bunny(i, (g_item_raw_flags & 0x04000000) != 0);   // ITEM_BUNNY_ENABLED
            q30_set_item_pickup(i, g_item_pickup, g_item_pickup_mode, g_item_pstring, g_item_psflags);
            q30_set_item_flags32(i, g_item_raw_flags);
            q30_set_item_cost(i, g_cost_counter[0], g_cost_amount[0], g_cost_counter[1], g_cost_amount[1]);
q30_set_item_sprite_script(i, g_item_sprite_script, g_item_sprite_initd);
            q30_set_item_weapon_script(i, g_item_weapon_script, g_item_weapon_initd);
            q30_set_item_override_flags(i, (int)g_item_override_flags);
            q30_set_item_display_name(i, g_item_display_name.c_str());
        }
        else if(s.keep)
            q30_set_high_item(i, tempitem, g_true_family, high_name.c_str());   // 256 and up
    }

    if(s.keep)
    {
        for(int i = 0; i < MAXITEMS; ++i)
        {
            if(itemsbuf[i].fam_type == 0)
                itemsbuf[i].fam_type = 1;
        }
    }

    return 0;
}

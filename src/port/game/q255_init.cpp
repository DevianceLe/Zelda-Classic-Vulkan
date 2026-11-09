// 2.55 init data (INIT), Link sprites (LINK) and subscreens (SUBS). See q255.h.
//
// Read and dropped, 2.53 has no field for it:
//
// INIT
//   max super bombs (v19+): 2.53 derives it from max_bombs / bomb_ratio
//   Link walk step (v20+)
//   subscreen scroll speed (v21+)
//   HP per heart, magic per block, Link and enemy damage multipliers (v22+): 2.53 hardcodes 16, 32, 2, 4
//   custom counters 1-25, start and max (v23+)
//   dither type, arg, percent; default light radius; transdark percent (v24+)
//   darkness colour (v25+)
//   swim gravity (v26+)
//   sideswim up, side and down steps (v27+)
//   exit water jump (v28+)
//   bunny LTM (v30+)
//   switchhook style (v31+)
//   generic script init data (v33+, as bounded maps from v37), instance init (v50+)
//   Link swim multiplier and divisor (v34+)
//   screen script data (v35+)
//   shove offset (v36+)
//   sprite flicker speed, colour, transparency (v38+)
//   air drag (v39+)
//   light wave rate and size (v40+), region mapping (v41+)
//   custom level item bits (v42+)
//   bottle slots (v43+)
//   level switch states (v44+)
//   item spawn flicker, timeout duration, timeout flicker, flicker speed (v45+)
//   sprite Z thresholds (v46+)
//   Link item box offset and size (v48+)
//   message advance delay (v49+)
//   global and hero script configs (v50+)
//   viewport follow settings (v51+, v52+)
//
// INIT values that lose range:
//   bombs, super bombs, max bombs, arrows, max arrows: words from v19, clamped to a byte
//   gravity: from v26 a long in 1/100ths of 2.53's unit; divided by 100 and clamped to a byte
//   magic drain rate (v32+): only 1 or 2 fit 2.53's double magic bit, 0 becomes 1 and above 2 becomes 2
//   keys (v37+): a word, clamped to a byte
//   counters 7 and up, max super bombs (v37+)
//   start and max life, continue hearts (v37+): stored in HP, converted to hearts with the quest's
//     HP per heart; damage and healing elsewhere stay in raw HP, so a quest not using 16 per heart
//     plays with different proportions, and the same goes for magic per block
//   "more" message icon (v49+): moved to each string, the 224, 64 defaults are used here
//
// LINK
//   third hold sprite (sword), land and water (v7+)
//   frozen, frozen water, on fire, on fire water, digging, using rod, using cane, pushing,
//     lifting, lifting walk, stunned, stunned water, drowning, lava drowning, falling, shocked,
//     shocked water, pull sword, reading, slash 180, Z4 slash, dash, bonk, medallion sprites (v7+)
//   Link defenses, one byte per weapon type, 146 of them (v8+)
//   sideswim sprites (v9+)
//   sideswim slash, stab, pound and charge sprites (v10+)
//   hammer offsets (v11+)
//   sideswim hold sprites (v12+)
//   sideswim casting sprite (v13+)
//   side drowning sprites (v14+)
//   reverse slash sprites (v15+)
//   lifting animation frame counts (v16+)
//
// SUBS, up to v7
//   the byte stored after every non-text object (a dp1 placeholder, only text objects use dp1)
//
// SUBS, v8 and up: 3.0's pages and widgets become 2.53's flat object list.
//   Converted, first page only: frame, text, line, rect, time, magic and life meter, button item
//     (A and B), counter, default counters, minimap title, minimap, large map, background colour,
//     item slot, McGuffin frame, McGuffin, tile block, mini tile, selector, life and magic gauge
//     pieces, text box, selected item text.
//   Read and dropped: misc gauge, button counter, percent bar, item cooldown gauge and text;
//     overlay and map subscreens; pages after the first; widgets past 2.53's object limit;
//     widgets disabled by script; button items for X and Y; counters showing only things 2.53
//     can't (life, magic, max HP, max MP, custom 26 and up).
//   Lost on converted widgets: labels, page and script button actions, selector overrides, display
//     conditions (items, counters, level items, dmaps, screens), compass level items, map room
//     colours (v19+), gauge grids, animation and infinite items, second counter colours (v12+),
//     max digits, fonts added after 2.53 (they become the Zelda font).

#include "precompiled.h" //always first

#include <string.h>

#include "q255.h"
#include <map>
#include "q30_scripts.h"
#include "zsys.h"
#include "subscr.h"
#include "zc_custom.h"

extern zinitdata zinit;
extern int link_animation_speed; //lower is faster animation

static byte clamp_byte(long v)
{
    return v < 0 ? (byte)0 : (v > 255 ? (byte)255 : (byte)v);
}

// ---- upstream container layouts -------------------------------------------------

// count items of each bytes, checked against what's left so a bad count can't wrap.
static void walk_items(Q255Section &s, size_t count, size_t each)
{
    if(each && count > s.left() / each)
    {
        s.failed = true;
        return;
    }

    s.skip(count * each);
}

// p_getbvec / p_getbmap with a word size and keys and values of a fixed size.
static void skip_bounded_w(Q255Section &s, size_t value_size)
{
    word sz = s.igetw();
    if(!sz)
        return;

    byte pairs = s.getc();
    word count = s.igetw();
    walk_items(s, count, value_size + (pairs ? 2 : 0));
}

// The same with a dword size and keys.
static void skip_bounded_l(Q255Section &s, size_t value_size)
{
    dword sz = s.igetl();
    if(!sz)
        return;

    byte pairs = s.getc();
    dword count = s.igetl();
    walk_items(s, count, value_size + (pairs ? 4 : 0));
}

// A bounded_vec<word, byte> (bitstrings too) into dst; anything past dst_size is dropped.
static void get_bounded_bytes(Q255Section &s, byte *dst, size_t dst_size)
{
    word sz = s.igetw();
    if(!sz)
        return;

    byte pairs = s.getc();
    word count = s.igetw();

    for(int q = 0; q < count && !s.failed; ++q)
    {
        word k = pairs ? s.igetw() : (word)q;
        byte val = s.getc();
        if(k < dst_size)
            dst[k] = val;
    }
}

// bounded_map<word, word> into dst.
static void get_word_map(Q255Section &s, word *dst, int dst_size)
{
    word sz = s.igetw();
    if(!sz)
        return;

    byte pairs = s.getc();
    word count = s.igetw();

    for(int q = 0; q < count && !s.failed; ++q)
    {
        word k = pairs ? s.igetw() : (word)q;
        word v = s.igetw();
        if(k < dst_size)
            dst[k] = v;
    }
}

// bounded_vec<byte, int32_t>
static void skip_initd(Q255Section &s)
{
    byte sz = s.getc();
    if(!sz)
        return;

    byte pairs = s.getc();
    byte count = s.getc();
    walk_items(s, count, pairs ? 5 : 4);
}

// bounded_map<dword, int32_t>
static void skip_long_map(Q255Section &s)
{
    skip_bounded_l(s, 4);
}

// std::map<word, int>, through p_getlmap
static void skip_inst_init(Q255Section &s)
{
    dword n = s.igetl();
    walk_items(s, n, 6);
}

typedef void (*q255_value_skipper)(Q255Section &s);

// bounded_map<word, something variable>
static void skip_nested_w(Q255Section &s, q255_value_skipper value)
{
    word sz = s.igetw();
    if(!sz)
        return;

    byte pairs = s.getc();
    word count = s.igetw();

    for(int q = 0; q < count && !s.failed; ++q)
    {
        if(pairs)
            s.skip(2);
        value(s);
    }
}

// bounded_map<dword, something variable>
static void skip_nested_l(Q255Section &s, q255_value_skipper value)
{
    dword sz = s.igetl();
    if(!sz)
        return;

    byte pairs = s.getc();
    dword count = s.igetl();

    for(dword q = 0; q < count && !s.failed; ++q)
    {
        if(pairs)
            s.skip(4);
        value(s);
    }
}

// p_getvar(script_config_nosavescript): run_args (the reader stops at 8) and inst_init.
static void skip_script_args(Q255Section &s)
{
    word n = s.igetw();
    s.skip((size_t)zc_min((int)n, 8) * 4);

    word m = s.igetw();
    walk_items(s, m, 6);
}

// p_getvar(script_config)
static void skip_script_config(Q255Section &s)
{
    s.skip(2);
    skip_script_args(s);
}

// p_getarr of script_config_nosavescript
static void skip_script_config_array(Q255Section &s, int capacity)
{
    word n = s.igetw();

    for(int q = 0; q < n && q < capacity && !s.failed; ++q)
        skip_script_args(s);
}

// ---- INIT ---------------------------------------------------------------------

static void set_level_items(zinitdata &temp, int level, bool map, bool compass, bool boss_key, bool mcguffin)
{
    set_bit(temp.map, level, map ? 1 : 0);
    set_bit(temp.compass, level, compass ? 1 : 0);
    set_bit(temp.boss_key, level, boss_key ? 1 : 0);

    if(level >= 1 && level <= 8)
        set_bit(&temp.triforce, level - 1, mcguffin ? 1 : 0);
}

// v37 and up: a different layout altogether.
static void read_init_new(Q255Section &s, zinitdata &temp)
{
    const int v = s.version;

    temp.subscreen = ssdtMAX;

    byte item_bits[32];
    memset(item_bits, 0, 32);

    if(v >= 47)
        get_bounded_bytes(s, item_bits, 32);
    else
        s.read(item_bits, 32);

    for(int i = 0; i < 256; ++i)
        temp.items[i] = (item_bits[i / 8] & (1 << (i % 8))) != 0;

    // level item bits: 1 McGuffin, 2 map, 4 compass, 8 boss killed, 16 boss key
    if(v >= 42)
    {
        for(int q = 0; q < MAXLEVELS; ++q)
        {
            word litems = v >= 44 ? s.igetw() : (word)s.getc();
            set_level_items(temp, q, (litems & 2) != 0, (litems & 4) != 0, (litems & 16) != 0, (litems & 1) != 0);
        }
    }
    else
    {
        byte l_map[MAXLEVELS / 8], l_compass[MAXLEVELS / 8], l_boss[MAXLEVELS / 8], l_mcguffin[MAXLEVELS / 8];

        for(int q = 0; q < MAXLEVELS / 8; ++q)
        {
            l_map[q] = s.getc();
            l_compass[q] = s.getc();
            l_boss[q] = s.getc();
            l_mcguffin[q] = s.getc();
        }

        for(int q = 0; q < MAXLEVELS; ++q)
            set_level_items(temp, q, get_bit(l_map, q) != 0, get_bit(l_compass, q) != 0, get_bit(l_boss, q) != 0, get_bit(l_mcguffin, q) != 0);
    }

    get_bounded_bytes(s, temp.level_keys, MAXLEVELS);

    // life, money, bombs, arrows, magic, keys, super bombs; the rest have no field here
    word counter[7], mcounter[7];
    memset(counter, 0, sizeof(counter));
    memset(mcounter, 0, sizeof(mcounter));
    mcounter[1] = 255;
    mcounter[5] = 255;

    byte num_counters = s.getc();
    int q;

    for(q = 0; q < num_counters; ++q)
    {
        word val = s.igetw();
        if(q < 7)
            counter[q] = val;
    }

    for(q = 0; q < num_counters; ++q)
    {
        word val = s.igetw();
        if(q < 7)
            mcounter[q] = val;
    }

    temp.bomb_ratio = s.getc();
    temp.hcp = s.getc();
    temp.hcp_per_hc = s.getc();
    word cont_heart = s.igetw();
    byte hp_per_heart = s.getc();
    s.skip(1);  // magic per block
    s.skip(2);  // Link and enemy damage multipliers
    s.skip(6);  // dither, light radius, transdark, darkness colour

    temp.ss_grid_x = (int)s.igetl_s();
    temp.ss_grid_y = (int)s.igetl_s();
    temp.ss_grid_xofs = (int)s.igetl_s();
    temp.ss_grid_yofs = (int)s.igetl_s();
    temp.ss_grid_color = (int)s.igetl_s();
    temp.ss_bbox_1_color = (int)s.igetl_s();
    temp.ss_bbox_2_color = (int)s.igetl_s();
    temp.ss_flags = (int)s.igetl_s();
    temp.ss_grid_x = zc_max(temp.ss_grid_x, 1);
    temp.ss_grid_y = zc_max(temp.ss_grid_y, 1);

    // INIT_FL_CONTPERCENT, INIT_FL_CANSLASH
    byte flags = 0;
    get_bounded_bytes(s, &flags, 1);

    bool percent = (flags & 1) != 0;
    set_bit(temp.misc, idM_CONTPERCENT, percent ? 1 : 0);
    set_bit(temp.misc, idM_CANSLASH, (flags & 2) ? 1 : 0);

    temp.last_map = s.getc();
    temp.last_screen = s.getc();

    temp.msg_more_x = 224;
    temp.msg_more_y = 64;

    if(v < 49)
    {
        temp.msg_more_x = s.getc();
        byte more_y = s.getc();
        temp.msg_more_y = more_y > 160 ? 160 : more_y;
        temp.msg_more_is_offset = s.getc() ? 1 : 0;
    }

    temp.msg_speed = s.getc();

    if(v >= 49)
        s.skip(1);  // message advance delay

    temp.gravity = clamp_byte(s.igetl_s() / 100);
    s.skip(4);  // swim gravity
    temp.terminalv = s.igetw();
    temp.link_swim_speed = s.getc();
    s.skip(2);  // swim multiplier and divisor
    s.skip(6);  // sideswim steps
    s.skip(4);  // exit water jump
    s.skip(2);  // walk step
    temp.linkanimationstyle = s.getc();
    temp.jump_link_layer_threshold = s.getc();
    s.skip(4);  // bunny LTM
    temp.start_dmap = s.igetw();
    s.skip(2);  // subscreen speed
    s.skip(1);  // switchhook style

    byte drain = s.getc();
    set_bit(temp.misc, idM_DOUBLEMAGIC, drain <= 1 ? 1 : 0);

    s.skip(4);  // shove offset
    // Generic scripts: which run at game start and their exit, reload and event states.
    {
        byte run_flags[Q30_GENERIC_SLOTS / 8 + 1];
        memset(run_flags, 0, sizeof(run_flags));
        get_bounded_bytes(s, run_flags, sizeof(run_flags));

        static word exit_states[Q30_GENERIC_SLOTS], reload_states[Q30_GENERIC_SLOTS];
        static long gen_initd[Q30_GENERIC_SLOTS][8];
        static dword event_states[Q30_GENERIC_SLOTS];
        static std::map<dword, long> gen_data[Q30_GENERIC_SLOTS];
        memset(exit_states, 0, sizeof(exit_states));
        memset(reload_states, 0, sizeof(reload_states));
        memset(gen_initd, 0, sizeof(gen_initd));
        memset(event_states, 0, sizeof(event_states));
        for(int q = 0; q < Q30_GENERIC_SLOTS; ++q)
            gen_data[q].clear();

        get_word_map(s, exit_states, Q30_GENERIC_SLOTS);
        get_word_map(s, reload_states, Q30_GENERIC_SLOTS);

        // bounded_map<word, bounded_vec<byte, int>>
        {
            word sz = s.igetw();
            if(sz)
            {
                byte pairs = s.getc();
                word count = s.igetw();

                for(int q = 0; q < count && !s.failed; ++q)
                {
                    word slot = pairs ? s.igetw() : (word)q;
                    byte vsz = s.getc();
                    if(!vsz)
                        continue;

                    byte vpairs = s.getc();
                    byte vcount = s.getc();

                    for(int i = 0; i < vcount && !s.failed; ++i)
                    {
                        byte k = vpairs ? s.getc() : (byte)i;
                        long v = s.igetl_s();
                        if(slot < Q30_GENERIC_SLOTS && k < 8)
                            gen_initd[slot][k] = v;
                    }
                }
            }
        }

        // bounded_map<word, dword>
        {
            word sz = s.igetw();
            if(sz)
            {
                byte pairs = s.getc();
                word count = s.igetw();

                for(int q = 0; q < count && !s.failed; ++q)
                {
                    word slot = pairs ? s.igetw() : (word)q;
                    dword v = s.igetl();
                    if(slot < Q30_GENERIC_SLOTS)
                        event_states[slot] = v;
                }
            }
        }

        // bounded_map<word, bounded_map<dword, int>>
        {
            word sz = s.igetw();
            if(sz)
            {
                byte pairs = s.getc();
                word count = s.igetw();

                for(int q = 0; q < count && !s.failed; ++q)
                {
                    word slot = pairs ? s.igetw() : (word)q;
                    dword vsz = s.igetl();
                    if(!vsz)
                        continue;

                    byte vpairs = s.getc();
                    dword vcount = s.igetl();

                    for(dword i = 0; i < vcount && !s.failed; ++i)
                    {
                        dword k = vpairs ? s.igetl() : i;
                        long v = s.igetl_s();
                        if(slot < Q30_GENERIC_SLOTS)
                            gen_data[slot][k] = v;
                    }
                }
            }
        }

        if(s.keep && !s.failed)
        {
            q30_clear_generic_init();

            for(int q = 1; q < Q30_GENERIC_SLOTS; ++q)
                q30_set_generic_init(q, (run_flags[q / 8] & (1 << (q % 8))) != 0, exit_states[q], reload_states[q], event_states[q], gen_initd[q], &gen_data[q]);
        }
    }

    skip_nested_l(s, skip_long_map);    // screen script data

    if(v >= 38)
        s.skip(3);  // sprite flicker
    if(v >= 39)
        s.skip(4);  // air drag

    // Some z3 builds wrote only the region mapping byte at v40; nothing else follows it there.
    if(v == 40 && s.left() == 1)
        s.skip(1);
    else
    {
        if(v >= 40)
            s.skip(4);  // light wave rate and size
        if(v >= 41)
            s.skip(1);  // region mapping
    }

    if(v >= 43)
        s.skip(256);    // bottle slots
    if(v >= 44)
        skip_bounded_w(s, 4);   // level switch states
    if(v >= 45)
        s.skip(7);      // item flicker and timeout
    if(v >= 46)
        s.skip(5 * 2);  // sprite Z thresholds
    if(v >= 48)
        s.skip(4);      // Link's item box

    if(v >= 50)
    {
        skip_nested_w(s, skip_inst_init);   // generic script instance init
        skip_script_config_array(s, 8);     // global scripts
        skip_script_config_array(s, 5);     // hero scripts
    }

    if(v >= 51)
        s.skip(2);      // viewport follow deadzone
    if(v >= 52)
        s.skip(1 + 1 + 4 + 4 + 2);  // viewport follow lookahead and recentering

    int hp = hp_per_heart ? hp_per_heart : 16;
    temp.hc = clamp_byte(mcounter[0] / hp);
    temp.start_heart = counter[0] / hp;
    temp.cont_heart = percent ? cont_heart : cont_heart / hp;
    temp.rupies = counter[1];
    temp.max_rupees = mcounter[1];
    temp.bombs = clamp_byte(counter[2]);
    temp.max_bombs = clamp_byte(mcounter[2]);
    temp.arrows = clamp_byte(counter[3]);
    temp.max_arrows = clamp_byte(mcounter[3]);
    temp.magic = counter[4];
    temp.max_magic = mcounter[4];
    temp.keys = clamp_byte(counter[5]);
    temp.max_keys = mcounter[5];
    temp.super_bombs = clamp_byte(counter[6]);
}

int q255_read_initdata(Q255Section &s)
{
    if(s.version < 18 || s.version > 52)
        return Q255_NOT_HANDLED;

    const int v = s.version;

    zinitdata temp;
    memset(&temp, 0, sizeof(zinitdata));

    if(v >= 37)
    {
        read_init_new(s, temp);

        if(s.failed)
            return qe_invalid;

        if(s.keep)
        {
            memcpy(&zinit, &temp, sizeof(zinitdata));
            link_animation_speed = (zinit.linkanimationstyle == las_zelda3slow) ? 2 : 1;
        }

        return 0;
    }

    for(int i = 0; i < 256; ++i)
        temp.items[i] = (s.getc() != 0);

    if(v < 29)
    {
        temp.bombs = s.getc();
        temp.super_bombs = s.getc();
    }

    temp.hc = s.getc();
    temp.start_heart = s.igetw();
    temp.cont_heart = s.igetw();
    temp.hcp = s.getc();
    temp.hcp_per_hc = s.getc();

    if(v < 29)
        temp.max_bombs = s.getc();

    temp.keys = s.getc();
    temp.rupies = s.igetw();
    temp.triforce = s.getc();
    s.read(temp.map, 64);
    s.read(temp.compass, 64);
    s.read(temp.boss_key, 64);
    s.read(temp.misc, 16);
    temp.last_map = s.getc();
    temp.last_screen = s.getc();
    temp.max_magic = s.igetw();
    temp.magic = s.igetw();
    temp.bomb_ratio = s.getc();
    temp.msg_more_x = s.getc();
    temp.msg_more_y = s.getc();
    temp.subscreen = s.getc();
    temp.start_dmap = s.igetw();
    temp.linkanimationstyle = s.getc();

    if(v < 29)
    {
        temp.arrows = s.getc();
        temp.max_arrows = s.getc();
    }

    s.read(temp.level_keys, MAXLEVELS);

    temp.ss_grid_x = (int)s.igetw();
    temp.ss_grid_y = (int)s.igetw();
    temp.ss_grid_xofs = (int)s.igetw();
    temp.ss_grid_yofs = (int)s.igetw();
    temp.ss_grid_color = (int)s.igetw();
    temp.ss_bbox_1_color = (int)s.igetw();
    temp.ss_bbox_2_color = (int)s.igetw();
    temp.ss_flags = (int)s.igetw();
    temp.ss_grid_x = zc_max(temp.ss_grid_x, 1);
    temp.ss_grid_y = zc_max(temp.ss_grid_y, 1);

    temp.subscreen_style = s.getc();
    temp.usecustomsfx = s.getc();
    temp.max_rupees = s.igetw();
    temp.max_keys = s.igetw();
    temp.gravity = s.getc();    // replaced by the long from v26
    temp.terminalv = s.igetw();
    temp.msg_speed = s.getc();
    temp.transition_type = s.getc();
    temp.jump_link_layer_threshold = s.getc();
    temp.msg_more_is_offset = s.getc();

    if(v >= 19)
    {
        temp.bombs = clamp_byte(s.igetw());
        temp.super_bombs = clamp_byte(s.igetw());
        temp.max_bombs = clamp_byte(s.igetw());
        s.skip(2);  // max super bombs
        temp.arrows = clamp_byte(s.igetw());
        temp.max_arrows = clamp_byte(s.igetw());
    }

    if(v >= 20)
        s.skip(2);  // walk step
    if(v >= 21)
        s.skip(2);  // subscreen speed
    if(v > 21)
        s.skip(4);  // HP per heart, magic per block, damage multipliers
    if(v > 22)
        s.skip(25 * 2 * 2); // custom counters: 25 start, 25 max
    if(v > 23)
        s.skip(5);  // dither, light radius, transdark
    if(v > 24)
        s.skip(1);  // darkness colour

    if(v > 25)
    {
        temp.gravity = clamp_byte(s.igetl_s() / 100);
        s.skip(4);  // swim gravity
    }

    if(v > 26)
        s.skip(6);  // sideswim steps
    if(v > 27)
        s.skip(4);  // exit water jump
    if(v > 29)
        s.skip(4);  // bunny LTM
    if(v > 30)
        s.skip(1);  // switchhook style

    if(v > 31)
    {
        byte drain = s.getc();
        set_bit(temp.misc, idM_DOUBLEMAGIC, drain <= 1 ? 1 : 0);
    }

    if(v > 32)
    {
        word numgenscript = s.igetw();
        if(numgenscript > 512)
            return qe_invalid;

        for(int q = 1; q < numgenscript && !s.failed; ++q)
        {
            byte flags = s.getc();
            if(!(flags & 2))
                continue;

            s.skip(2 + 2 + 8 * 4);  // exit state, reload state, init data
            s.skip(4);              // a size the next count repeats
            dword n = s.igetl();
            walk_items(s, n, 4);    // data
            s.skip(4);              // event state
        }
    }

    if(v > 33)
        s.skip(2);  // swim multiplier and divisor

    if(v > 34)
    {
        dword num_used = s.igetl();

        for(dword q = 0; q < num_used && !s.failed; ++q)
        {
            dword sz = s.igetl();
            if(sz)
            {
                dword n = s.igetl();
                walk_items(s, n, 4);
            }
        }
    }

    if(v > 35)
        s.skip(4);  // shove offset

    if(s.failed)
        return qe_invalid;

    if(s.keep)
    {
        memcpy(&zinit, &temp, sizeof(zinitdata));
        link_animation_speed = (zinit.linkanimationstyle == las_zelda3slow) ? 2 : 1;
    }

    return 0;
}

// ---- LINK ---------------------------------------------------------------------

// Tiles are words up to v5 and longs after that.
static int sprite_size(const Q255Section &s)
{
    return s.version >= 6 ? 6 : 4;
}

static void get_sprite(Q255Section &s, int *dst)
{
    int tile = s.version >= 6 ? (int)s.igetl() : (int)s.igetw();
    byte flip = s.getc();
    byte extend = s.getc();

    if(tile > 0)
        q255_note_tile("link", (unsigned long)tile);

    if(s.keep && !s.failed)
    {
        dst[spr_tile] = (tile < 0 || tile >= NEWMAXTILES) ? 0 : tile;
        dst[spr_flip] = flip > 3 ? 0 : (int)flip;
        dst[spr_extend] = extend > 2 ? 0 : (int)extend;
    }
}

static void get_sprites(Q255Section &s, int (*dst)[3], int count)
{
    for(int i = 0; i < count && !s.failed; ++i)
        get_sprite(s, dst[i]);
}

static void skip_sprites(Q255Section &s, int count)
{
    s.skip((size_t)count * sprite_size(s));
}

int q255_read_linksprites(Q255Section &s)
{
    if(s.version < 5 || s.version > 16)
        return Q255_NOT_HANDLED;

    const int v = s.version;

    if(s.keep)
    {
        zinit.link_swim_speed = 67; //default
        setuplinktiles(zinit.linkanimationstyle);
    }

    get_sprites(s, walkspr, 4);
    get_sprites(s, stabspr, 4);
    get_sprites(s, slashspr, 4);
    get_sprites(s, floatspr, 4);
    get_sprites(s, swimspr, 4);
    get_sprites(s, divespr, 4);
    get_sprites(s, poundspr, 4);
    get_sprite(s, castingspr);

    for(int i = 0; i < 2; ++i)
    {
        get_sprites(s, holdspr[i], 2);
        if(v > 6)
            skip_sprites(s, 1); // sword hold
    }

    get_sprites(s, jumpspr, 4);
    get_sprites(s, chargespr, 4);

    byte swim_speed = s.getc();
    if(s.keep && !s.failed)
        zinit.link_swim_speed = swim_speed;

    if(v > 6)
        skip_sprites(s, 23 * 4 + 3);    // 23 four-direction sets, 3 medallion sprites
    if(v > 15)
        s.skip(4);                      // lifting sprite frame counts, one byte each
    if(v > 8)
        skip_sprites(s, 4);             // sideswim
    if(v > 9)
        skip_sprites(s, 4 * 4);         // sideswim slash, stab, pound, charge
    if(v > 10)
        s.skip(4 * 4);                  // hammer offsets
    if(v > 11)
        skip_sprites(s, 3);             // sideswim hold
    if(v > 12)
        skip_sprites(s, 1);             // sideswim casting
    if(v > 13)
        skip_sprites(s, 4);             // side drowning
    if(v > 14)
        skip_sprites(s, 4);             // reverse slash
    if(v > 7)
        s.skip(146);                    // Link defenses (wMax when the format was set)

    return s.failed ? qe_invalid : 0;
}

// ---- SUBS ---------------------------------------------------------------------
//
// qst.cpp also forces d3 on life meters when the old qr_24HC bit is set, but it
// clears that bit for rules version 8 and up, and every 2.55 quest is past that.

static bool sub_has_text(int type)
{
    return type == ssoTEXT || type == ssoTEXTBOX || type == ssoCURRENTITEMTEXT || type == ssoCURRENTITEMCLASSTEXT;
}

static void clear_sub_object(subscreen_object &o)
{
    if(sub_has_text(o.type) && o.dp1 != NULL)
        delete [] (char *)o.dp1;

    memset(&o, 0, sizeof(subscreen_object));
}

static void read_sub_object(Q255Section &s, subscreen_object &t)
{
    memset(&t, 0, sizeof(subscreen_object));

    t.type = s.getc();
    t.pos = s.getc();
    t.x = s.igetw();
    t.y = s.igetw();
    t.w = s.igetw();
    t.h = s.igetw();
    t.colortype1 = s.getc();
    t.color1 = s.igetw_s();
    t.colortype2 = s.getc();
    t.color2 = s.igetw_s();
    t.colortype3 = s.getc();
    t.color3 = s.igetw_s();
    t.d1 = (int)s.igetl_s();
    t.d2 = (int)s.igetl_s();
    t.d3 = (int)s.igetl_s();
    t.d4 = (int)s.igetl_s();
    t.d5 = (int)s.igetl_s();
    t.d6 = (int)s.igetl_s();
    t.d7 = (int)s.igetl_s();
    t.d8 = (int)s.igetl_s();
    t.d9 = (int)s.igetl_s();
    t.d10 = (int)s.igetl_s();
    t.speed = s.getc();
    t.delay = s.getc();
    t.frame = s.igetw();

    if(sub_has_text(t.type))
    {
        // A non-empty string is stored with its terminator, an empty one not at all.
        size_t n = s.igetw();
        if(n)
            ++n;

        if(s.keep && s.has(n))
        {
            char *text = new char[n + 1];
            s.read(text, n);
            text[n] = 0;
            t.dp1 = text;
        }
        else
            s.skip(n);
    }
    else
        s.skip(1);

    // v7 changed "always show" on magic gauges from 0 to -1; 2.53 still uses 0.
    if(s.version >= 7 && t.type == ssoMAGICGAUGE && t.d9 == -1)
        t.d9 = 0;
}

static void read_sub_group(Q255Section &s, int i)
{
    char name[64];
    s.read(name, 64);
    byte ss_type = s.getc();
    int numsub = s.igetw();

    subscreen_group &g = custom_subscreen[i];
    int j;

    // The format stops at this limit too: no file holds objects past it.
    for(j = 0; j < MAXSUBSCREENITEMS && j < numsub && !s.failed; ++j)
    {
        if(s.keep)
            clear_sub_object(g.objects[j]);

        subscreen_object t;
        read_sub_object(s, t);

        if(s.keep && !s.failed)
            g.objects[j] = t;
    }

    if(!s.keep || s.failed)
        return;

    if(numsub > 0)
    {
        memcpy(g.name, name, 64);
        g.name[63] = 0;
        g.ss_type = ss_type;
    }

    for(j = numsub; j < MAXSUBSCREENITEMS; ++j)
        clear_sub_object(g.objects[j]);
}

// ---- SUBS, v8 and up: pages of widgets ------------------------------------------

// Widget types of the new format (widgNULL...), kept apart from 2.53's sso types.
enum
{
    WIDG_NULL, WIDG_FRAME, WIDG_TEXT, WIDG_LINE, WIDG_RECT,
    WIDG_TIME, WIDG_MMETER, WIDG_LMETER, WIDG_BTNITM, WIDG_COUNTER,
    WIDG_OLDCTR, WIDG_MMAPTITLE, WIDG_MMAP, WIDG_LMAP, WIDG_BGCOLOR,
    WIDG_ITEMSLOT, WIDG_MCGUFF_FRAME, WIDG_MCGUFF, WIDG_TILEBLOCK, WIDG_MINITILE,
    WIDG_SELECTOR, WIDG_LGAUGE, WIDG_MGAUGE, WIDG_TEXTBOX, WIDG_SELECTEDTEXT,
    WIDG_MISCGAUGE, WIDG_BTNCOUNTER, WIDG_COUNTERPERCBAR, WIDG_ITMCOOLDOWNGAUGE, WIDG_ITMCOOLDOWNTEXT,
    WIDG_MAX
};

static const dword WIDG_SELECTABLE = 0x01;      // SUBSCRFLAG_SELECTABLE
static const dword WIDG_SELOVERRIDE = 0x04;     // SUBSCRFLAG_SELOVERRIDE

struct SubColor255
{
    byte type;
    short color;
};

struct SubWidget255
{
    byte posflags;
    short x, y;
    word w, h;
    dword genflags, flags;
    int pos, pos_up, pos_down, pos_left, pos_right;
    bool disabled;
};

static void get_sub_color(Q255Section &s, SubColor255 &c)
{
    c.type = s.getc();
    c.color = s.igetw_s();
}

static void get_sub_colors(Q255Section &s, SubColor255 *c, int count)
{
    for(int i = 0; i < count; ++i)
        get_sub_color(s, c[i]);
}

// 2.53 picks a random colour for special values it doesn't know.
static void put_sub_color(byte &type, short &color, const SubColor255 &c)
{
    type = c.type;
    color = c.color;

    if(c.type != ssctMISC)
        return;

    if(c.color == ssctMSGTEXT)
        color = ssctTEXT;
    else if(c.color < 0 || c.color > ssctMSGTEXT)
    {
        // transparent and "default", which upstream also draws as transparent
        type = ssctSYSTEM;
        color = -1;
    }
}

static void put_sub_colors(subscreen_object &t, const SubColor255 *c)
{
    put_sub_color(t.colortype1, t.color1, c[0]);
    put_sub_color(t.colortype2, t.color2, c[1]);
    put_sub_color(t.colortype3, t.color3, c[2]);
}

static void put_sub_cset(subscreen_object &t, const SubColor255 &c)
{
    t.colortype1 = c.type;
    t.color1 = c.color;
}

// Upstream stores its font ids; 2.53 wants its own ssf numbers.
static int sub_font(long font)
{
    switch(font)
    {
    case 0:
        return ssfZELDA;
    case 1:
        return ssfZ3;
    case 2:
        return ssfZ3SMALL;
    case 8:
        return ssfZTIME;
    case 9:
        return ssfSMALL;
    case 11:
        return ssfSMALLPROP;
    case 12:
        return ssfSS1;
    case 13:
        return ssfSS2;
    case 14:
        return ssfSS3;
    case 15:
        return ssfSS4;
    case 16:
        return ssfGBLA;
    case 17:
        return ssfGORON;
    case 18:
        return ssfZORAN;
    case 19:
        return ssfHYLIAN1;
    case 20:
        return ssfHYLIAN2;
    case 21:
        return ssfHYLIAN3;
    case 22:
        return ssfHYLIAN4;
    case 23:
        return ssfGBORACLE;
    case 24:
        return ssfGBORACLEP;
    case 25:
        return ssfDSPHANTOM;
    case 26:
        return ssfDSPHANTOMP;
    case 71:
        return ssfPROP;
    }

    return ssfZELDA;
}

// A counter of the new format as 2.53's ssc number, or -1 if 2.53 can't show it.
static int sub_counter(long ctr)
{
    switch(ctr)
    {
    case 1:
        return sscRUPEES;
    case 2:
        return sscBOMBS;
    case 3:
        return sscARROWS;
    case 5:     // the plain key counter
        return sscGENKEYNOMAGIC;
    case 6:
        return sscSBOMBS;
    case -9:
        return sscGENKEYMAGIC;
    case -8:
        return sscGENKEYNOMAGIC;
    case -7:
        return sscLEVKEYMAGIC;
    case -6:
        return sscLEVKEYNOMAGIC;
    case -5:
        return sscANYKEYMAGIC;
    case -4:
        return sscANYKEYNOMAGIC;
    }

    // crCUSTOM1 to crCUSTOM25
    if(ctr >= 7 && ctr <= 31)
        return sscSCRIPT1 + (int)(ctr - 7);

    return -1;
}

static int sub_tile(long tile)
{
    if(tile > 0)
        q255_note_tile("subscreen", (unsigned long)tile);

    return (tile < 0 || tile >= NEWMAXTILES) ? 0 : (int)tile;
}

// A tile number and corner packed as tile * 4 + corner.
static int sub_minitile(long tilecrn)
{
    if(tilecrn < 0)
        return 0;

    return (sub_tile(tilecrn >> 2) << 2) | (int)(tilecrn & 3);
}

// 2.53 has no way to say "no item": item 0 is what its own default objects use.
static int sub_item(long item)
{
    return (item < 0 || item >= MAXITEMS) ? 0 : (int)item;
}

// SubscrTransition::read
static void skip_sub_transition(Q255Section &s)
{
    s.skip(1);                          // type
    s.skip(s.version < 17 ? 1 : 2);     // sfx
    s.skip(2);                          // flags
    byte args = s.getc();
    walk_items(s, args, 4);
}

// SubscrSelectorInfo::read
static void skip_selector_info(Q255Section &s)
{
    s.skip(8);
    byte n = s.getc();
    walk_items(s, n, 12);
}

// SubscrWidget::read, everything after the widget type
static void read_widget_base(Q255Section &s, SubWidget255 &b)
{
    const int v = s.version;

    memset(&b, 0, sizeof(SubWidget255));

    s.skip(1);      // 'type', the sso type it was converted from, if any
    b.posflags = s.getc();
    b.x = s.igetw_s();
    b.y = s.igetw_s();
    b.w = s.igetw();
    b.h = s.igetw();
    b.genflags = s.igetl();
    b.flags = s.igetl();

    if(v >= 9)
        s.getwstr();    // label
    if(v >= 11)
        s.skip(1);      // compat flags

    if(b.genflags & WIDG_SELECTABLE)
    {
        b.pos = (int)s.igetl_s();
        b.pos_up = (int)s.igetl_s();
        b.pos_down = (int)s.igetl_s();
        b.pos_left = (int)s.igetl_s();
        b.pos_right = (int)s.igetl_s();
        s.getwstr();    // selected text override

        if(b.genflags & WIDG_SELOVERRIDE)
            skip_selector_info(s);

        if(v >= 21)
        {
            s.skip(1);  // script buttons
            skip_script_config(s);
        }
        else
        {
            word script = s.igetw();
            if(script)
            {
                s.skip(1);
                s.skip(8 * 4);
            }
        }

        byte pg_mode = s.getc();
        if(pg_mode)
        {
            s.skip(2);  // page buttons and target
            skip_sub_transition(s);
        }
    }

    if(v >= 14)
    {
        word count = s.igetw();
        walk_items(s, count, v < 18 ? 1 : 2);   // required items
        count = s.igetw();
        walk_items(s, count, v < 18 ? 1 : 2);   // forbidden items
        s.skip(2 + 2 + 1);                      // required counter
        s.skip(v >= 15 ? 2 : 1);                // required level items
        s.skip(2);                              // level item level
        b.disabled = s.getc() != 0;

        if(v >= 16)
        {
            byte floors = s.getc();
            walk_items(s, floors, 1);
            word levels = s.igetw();
            walk_items(s, levels, 2);
            word dmaps = s.igetw();
            walk_items(s, dmaps, 2);
            word maps = s.igetw();
            walk_items(s, maps, 2);
            byte screens = s.getc();
            walk_items(s, screens, 1);
            s.skip(2 + 2 + 4 + 4 + 4 + 2);      // screen and level state conditions
        }
    }
}

// SW_GaugePiece::read
static void read_gauge(Q255Section &s, const SubWidget255 &b, subscreen_object &t)
{
    long tiles[4];
    byte csets[4];

    for(int i = 0; i < 4; ++i)
    {
        tiles[i] = s.igetl_s();
        csets[i] = s.getc();
    }

    word frames = s.igetw();
    word speed = 0, delay = 0;
    bool still = frames <= 1;
    bool no_anim = still || !(b.flags & 0xC0);                          // ANIM_UNDER, ANIM_OVER
    bool no_speed = still || (!no_anim && frames <= 2 && (b.flags & 0x200));    // ANIM_SKIP
    bool no_inf = !(b.flags & 0xC00);                                   // INFITM_REQ, INFITM_BAN

    if(!no_speed)
    {
        speed = s.igetw();
        delay = s.igetw();
    }

    word container = s.igetw();
    s.skip(1);      // units per frame
    byte wid = s.getc();
    byte hei = s.getc();

    if(wid || hei)
        s.skip(1 + 1 + 2 + 2);  // grid spacing and offsets
    if(!no_anim)
        s.skip(2);              // animation value
    if(!no_inf)
        s.skip(s.version < 18 ? 2 : 4); // infinite item

    t.d1 = container;
    t.d2 = sub_minitile(tiles[0]);
    t.d3 = sub_minitile(tiles[1]);
    t.d4 = sub_minitile(tiles[2]);
    t.d5 = sub_minitile(tiles[3]);
    t.colortype1 = csets[0];
    t.color1 = csets[1];
    t.colortype2 = csets[2];
    t.color2 = csets[3];
    t.d6 = frames;
    t.d7 = speed;
    t.d8 = delay;
    t.d10 = (int)(b.flags & 0x1F);  // MOD1-4, UNQLAST: the same bits as 2.53's d10
}

// Font, alignment and shadow style, which most text widgets start with.
static void read_text_style(Q255Section &s, subscreen_object &t, bool has_align = true)
{
    t.d1 = sub_font(s.igetl_s());
    t.d2 = has_align ? s.getc() : 0;
    t.d3 = s.getc();
}

// Reads one widget (after its type byte) and fills t with the 2.53 equivalent.
// Returns false when there isn't one.
static bool read_widget(Q255Section &s, byte ty, subscreen_object &t, std::string &text)
{
    if(ty >= WIDG_MAX)
    {
        s.failed = true;
        return false;
    }

    SubWidget255 b;
    read_widget_base(s, b);

    memset(&t, 0, sizeof(subscreen_object));
    t.pos = b.posflags & (sspUP | sspDOWN | sspSCROLLING);
    t.x = (word)b.x;
    t.y = (word)b.y;
    t.w = b.w;
    t.h = b.h;

    const int f1 = (b.flags & 1) ? 1 : 0;
    const int f2 = (b.flags & 2) ? 1 : 0;
    const int f4 = (b.flags & 4) ? 1 : 0;

    SubColor255 c[3];
    bool convert = true;

    switch(ty)
    {
    case WIDG_NULL:
        convert = false;
        break;

    case WIDG_FRAME:
    {
        t.type = sso2X2FRAME;
        t.d1 = sub_tile(s.igetl_s());
        get_sub_color(s, c[0]);
        put_sub_cset(t, c[0]);
        t.d3 = f2;  // overlay
        t.d4 = f1;  // transparent
        break;
    }

    case WIDG_TEXT:
        t.type = ssoTEXT;
        read_text_style(s, t);
        text = s.getwstr();
        get_sub_colors(s, c, 3);
        put_sub_colors(t, c);
        break;

    case WIDG_LINE:
        t.type = ssoLINE;
        get_sub_color(s, c[0]);
        put_sub_color(t.colortype1, t.color1, c[0]);
        t.d4 = f1;
        break;

    case WIDG_RECT:
        t.type = ssoRECT;
        get_sub_color(s, c[0]); // fill
        get_sub_color(s, c[1]); // outline
        put_sub_color(t.colortype1, t.color1, c[1]);
        put_sub_color(t.colortype2, t.color2, c[0]);
        t.d1 = f2;  // filled
        t.d2 = f1;  // transparent
        break;

    case WIDG_TIME:
        // Upstream only draws time when the subscreen asks for it, like ssoSSTIME; 2.53's
        // other time object for the alternate format ignores that.
        t.type = f1 ? ssoBSTIME : ssoSSTIME;
        read_text_style(s, t);
        get_sub_colors(s, c, 3);
        put_sub_colors(t, c);
        break;

    case WIDG_MMETER:
        t.type = ssoMAGICMETER;
        break;

    case WIDG_LMETER:
    {
        byte rows = s.getc();
        t.type = ssoLIFEMETER;
        t.d2 = f1;  // bottom-up
        t.d3 = rows >= 3 ? 1 : 0;
        break;
    }

    case WIDG_BTNITM:
    {
        byte btn = s.getc();
        t.type = ssoBUTTONITEM;
        t.d1 = btn;
        t.d2 = f1;  // transparent
        convert = btn <= 3;
        break;
    }

    case WIDG_COUNTER:
    {
        t.type = ssoCOUNTER;
        read_text_style(s, t);
        get_sub_colors(s, c, 3);
        put_sub_colors(t, c);

        long ctrs[3];
        ctrs[0] = s.igetl_s();
        ctrs[1] = s.igetl_s();
        ctrs[2] = s.igetl_s();
        t.d4 = s.getc();    // min digits
        s.skip(1);          // max digits
        t.d10 = sub_item(s.igetl_s());
        t.d5 = s.getc();    // infinite character
        t.d6 = f1 | (f2 << 1);

        if(s.version >= 12)
            s.skip(3 * 3);  // second colours

        // Only the first one drives the number in 2.53, so a showable one goes first.
        int shown[3] = { -1, -1, -1 };
        int n = 0;

        for(int i = 0; i < 3; ++i)
        {
            int ssc = sub_counter(ctrs[i]);
            if(ssc >= 0)
                shown[n++] = ssc;
        }

        convert = n > 0;
        t.d7 = shown[0];
        t.d8 = shown[1];
        t.d9 = shown[2];
        break;
    }

    case WIDG_OLDCTR:
        t.type = ssoCOUNTERS;
        read_text_style(s, t, false);
        get_sub_colors(s, c, 3);
        put_sub_colors(t, c);
        t.d4 = s.getc();    // digits
        t.d10 = sub_item(s.igetl_s());
        t.d5 = s.getc();    // infinite character
        t.d2 = f1;          // show X
        break;

    case WIDG_BTNCOUNTER:
        read_text_style(s, t);
        get_sub_colors(s, c, 3);
        s.skip(1 + 1 + 1 + 1 + 1);  // digits, infinite character, button, cost index
        if(s.version >= 12)
            s.skip(3 * 3);          // second colours
        convert = false;
        break;

    case WIDG_MMAPTITLE:
        t.type = ssoMINIMAPTITLE;
        read_text_style(s, t);
        get_sub_colors(s, c, 3);
        put_sub_colors(t, c);
        t.d4 = f1;          // needs the map
        if(s.version >= 10)
            s.skip(1);      // tab size
        break;

    case WIDG_MMAP:
        t.type = ssoMINIMAP;
        if(s.version >= 13)
            s.skip(s.version >= 15 ? 2 : 1);    // compass level items
        get_sub_colors(s, c, 3);
        put_sub_colors(t, c);
        if(s.version >= 19)
            s.skip(3);      // visited room colour
        if(s.version >= 20)
            s.skip(3 * 2);  // unvisited room and background colours
        t.d1 = f1;
        t.d2 = f2;
        t.d3 = f4;
        break;

    case WIDG_LMAP:
        t.type = ssoLARGEMAP;
        get_sub_colors(s, c, 2);
        put_sub_color(t.colortype1, t.color1, c[0]);
        put_sub_color(t.colortype2, t.color2, c[1]);
        t.d1 = f1;
        t.d2 = f2;  // rooms
        t.d3 = f4;  // Link
        t.d10 = (b.flags & 8) ? 1 : 0;
        break;

    case WIDG_BGCOLOR:
        t.type = ssoCLEAR;
        get_sub_color(s, c[0]);
        put_sub_color(t.colortype1, t.color1, c[0]);
        break;

    case WIDG_ITEMSLOT:
    {
        long iclass = s.igetl_s();
        long iid = s.igetl_s();
        bool selectable = (b.genflags & WIDG_SELECTABLE) != 0;

        t.type = ssoCURRENTITEM;
        t.d1 = (iclass < 0 || iclass >= itype_max) ? (int)itype_misc : (int)iclass;
        t.d2 = f1 ? 0 : 1;  // invisible
        t.d3 = selectable ? b.pos : -1;
        t.d4 = selectable ? b.pos_up : -1;
        t.d5 = selectable ? b.pos_down : -1;
        t.d6 = selectable ? b.pos_left : -1;
        t.d7 = selectable ? b.pos_right : -1;
        t.d8 = (iid >= 0 && iid < MAXITEMS) ? (int)iid + 1 : 0;
        break;
    }

    case WIDG_MCGUFF_FRAME:
    {
        long frame_tile = s.igetl_s();
        long piece_tile = s.igetl_s();
        byte frame_cset = s.getc();
        byte piece_cset = s.getc();
        get_sub_colors(s, c, 2);

        t.type = ssoTRIFRAME;
        t.d1 = sub_tile(frame_tile);
        t.d2 = frame_cset;
        t.d3 = sub_tile(piece_tile);
        t.d4 = piece_cset;
        t.d5 = f1;
        t.d6 = f2;
        t.d7 = f4;
        put_sub_color(t.colortype1, t.color1, c[0]);
        put_sub_color(t.colortype2, t.color2, c[1]);
        break;
    }

    case WIDG_MCGUFF:
    {
        long tile = s.igetl_s();
        long number = s.igetl_s();
        byte flip = s.getc();
        get_sub_color(s, c[0]);

        t.type = ssoTRIFORCE;
        t.d1 = sub_tile(tile);
        t.d2 = flip;
        t.d3 = f1;  // overlay
        t.d4 = f2;  // transparent
        t.d5 = (int)number;
        put_sub_cset(t, c[0]);
        break;
    }

    case WIDG_TILEBLOCK:
    {
        long tile = s.igetl_s();
        byte flip = s.getc();
        get_sub_color(s, c[0]);

        t.type = ssoTILEBLOCK;
        t.d1 = sub_tile(tile);
        t.d2 = flip;
        t.d3 = f1;  // overlay
        t.d4 = f2;  // transparent
        put_sub_cset(t, c[0]);
        break;
    }

    case WIDG_MINITILE:
    {
        long tile = s.igetl_s();
        long special = s.igetl_s();
        byte crn = s.getc();
        byte flip = s.getc();
        get_sub_color(s, c[0]);

        t.type = ssoMINITILE;

        if(tile < 0)
        {
            t.d1 = -1;
            t.d2 = (int)special;
            t.d3 = crn;
        }
        else
            t.d1 = (sub_tile(tile) << 2) | (crn & 3);

        t.d4 = flip;
        t.d5 = f1;  // overlay
        t.d6 = f2;  // transparent
        put_sub_cset(t, c[0]);
        break;
    }

    case WIDG_SELECTOR:
        t.type = f4 ? ssoSELECTOR2 : ssoSELECTOR1;
        t.d4 = f1;  // transparent
        t.d5 = f2;  // large
        break;

    case WIDG_LGAUGE:
        t.type = ssoLIFEGAUGE;
        read_gauge(s, b, t);
        break;

    case WIDG_MGAUGE:
    {
        t.type = ssoMAGICGAUGE;
        read_gauge(s, b, t);
        short show = s.igetw_s();
        // -1 is "always" in the new format, 2.53 uses 0 for that
        t.d9 = show == -1 ? 0 : show;
        break;
    }

    case WIDG_MISCGAUGE:
        read_gauge(s, b, t);
        s.skip(2 + 2);  // counter, per container
        convert = false;
        break;

    case WIDG_ITMCOOLDOWNGAUGE:
        read_gauge(s, b, t);
        s.skip(4 + 4 + 1 + 4 + 4);  // item class, item, button, points, per container
        convert = false;
        break;

    case WIDG_ITMCOOLDOWNTEXT:
        read_text_style(s, t);
        get_sub_colors(s, c, 3);
        s.skip(4 + 4 + 1);  // item class, item, button
        convert = false;
        break;

    case WIDG_TEXTBOX:
        t.type = ssoTEXTBOX;
        read_text_style(s, t);
        t.d5 = s.getc();    // tab size
        text = s.getwstr();
        get_sub_colors(s, c, 3);
        put_sub_colors(t, c);
        t.d4 = f1;          // word wrap
        break;

    case WIDG_SELECTEDTEXT:
        t.type = ssoSELECTEDITEMNAME;
        read_text_style(s, t);
        t.d5 = s.getc();    // tab size
        get_sub_colors(s, c, 3);
        put_sub_colors(t, c);
        t.d4 = f1;          // word wrap
        break;

    case WIDG_COUNTERPERCBAR:
        s.skip(2);          // counter
        get_sub_colors(s, c, 2);
        convert = false;
        break;
    }

    return convert && !b.disabled && !s.failed;
}

static void store_sub_object(subscreen_group &g, int &count, subscreen_object t, const std::string &text)
{
    if(count >= MAXSUBSCREENITEMS)
        return;

    if(sub_has_text(t.type))
    {
        size_t len = strlen(text.c_str());
        char *copy = new char[len + 1];
        memcpy(copy, text.c_str(), len + 1);
        t.dp1 = copy;
    }

    g.objects[count++] = t;
}

// SubscrPage::read; dest is NULL for a page that isn't kept.
static void read_sub_page(Q255Section &s, subscreen_group *dest)
{
    s.skip(4);      // cursor position

    word widgets = s.igetw();
    int stored = 0;

    for(int i = 0; i < widgets && !s.failed; ++i)
    {
        byte ty = s.getc();
        subscreen_object t;
        std::string text;

        if(read_widget(s, ty, t, text) && dest)
            store_sub_object(*dest, stored, t, text);
    }

    if(s.version >= 16)
    {
        byte transitions = s.getc();

        for(int i = 0; i < transitions && !s.failed; ++i)
        {
            s.skip(4);      // page buttons, mode, target, flags
            skip_sub_transition(s);
        }
    }
}

// Empty slots end 2.53's search through the subscreens, so a kept one needs an object.
static void put_sub_placeholder(subscreen_group &g)
{
    if(g.objects[0].type == ssoNULL)
        g.objects[0].type = ssoNONE;
}

// ZCSubscreen::read. slot is where it goes in custom_subscreen, -1 if it isn't kept.
static void read_new_subscreen(Q255Section &s, int slot, byte ss_type)
{
    std::string name = s.getcstr();
    byte file_type = s.getc();
    dword flags = s.igetl();

    if(file_type == 0 || file_type == 3)   // active and map subscreens
    {
        if(flags & 2)                       // SUBFLAG_ACT_OVERRIDESEL
            skip_selector_info(s);

        s.skip(4 * 2);      // default buttons
        s.skip(1 + 1);      // page buttons
        skip_sub_transition(s);
        skip_sub_transition(s);

        if(s.version >= 21)
            skip_script_config(s);
        else
        {
            word script = s.igetw();
            if(script)
                s.skip(8 * 4);
        }
    }

    subscreen_group *g = NULL;

    if(s.keep && slot >= 0 && slot < MAXCUSTOMSUBSCREENS && !s.failed)
    {
        g = &custom_subscreen[slot];
        strncpy(g->name, name.c_str(), 63);
        g->name[63] = 0;
        g->ss_type = ss_type;
    }

    byte pages = s.getc();

    for(int q = 0; q < pages && !s.failed; ++q)
        read_sub_page(s, q == 0 ? g : NULL);

    if(g)
        put_sub_placeholder(*g);
}

static int read_new_subscreens(Q255Section &s)
{
    if(s.keep)
    {
        for(int i = 0; i < MAXCUSTOMSUBSCREENS; ++i)
        {
            for(int j = 0; j < MAXSUBSCREENITEMS; ++j)
                clear_sub_object(custom_subscreen[i].objects[j]);

            custom_subscreen[i].ss_type = 0;
            memset(custom_subscreen[i].name, 0, 64);
        }
    }

    // active, passive, overlay and (from v16) map subscreens, in that order
    int lists = s.version >= 16 ? 4 : 3;
    int next_slot = 0;
    int kept[2] = { 0, 0 };

    for(int list = 0; list < lists && !s.failed; ++list)
    {
        byte count = s.getc();

        for(int q = 0; q < count && !s.failed; ++q)
        {
            int slot = -1;

            if(list < 2 && next_slot < MAXCUSTOMSUBSCREENS)
            {
                slot = next_slot++;
                ++kept[list];
            }

            read_new_subscreen(s, slot, (byte)list);
        }
    }

    if(s.failed)
        return qe_invalid;

    // update_subscreens has to find at least one of each kind.
    if(s.keep)
    {
        for(int type = 0; type < 2 && next_slot < MAXCUSTOMSUBSCREENS; ++type)
        {
            if(kept[type])
                continue;

            custom_subscreen[next_slot].ss_type = (byte)type;
            put_sub_placeholder(custom_subscreen[next_slot]);
            ++next_slot;
        }
    }

    return 0;
}

int q255_read_subscreens(Q255Section &s)
{
    if(s.version < 6 || s.version > 21)
        return Q255_NOT_HANDLED;

    if(s.version >= 8)
        return read_new_subscreens(s);

    for(int i = 0; i < MAXCUSTOMSUBSCREENS && !s.failed; ++i)
        read_sub_group(s, i);

    return s.failed ? qe_invalid : 0;
}

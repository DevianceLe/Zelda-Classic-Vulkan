// Saving a quest made in 2.55 as 3.0 Nightly (zqn_255.h): the init data (INIT).
//
// Read with the tape (zq_tape_init) the way 3.0's readinitdata reads each version: before
// version 37 with readinitdata_old (2.53's fields, then 2.55's additions in order, each missing
// one at the value that reader gives it, and life in HP at the end), from 37 the newer layout.
// Then what the rest of the load does to the init data:
// - Link's sprites (read after the init data) set the swimming speed, and for init data older
//   than version 34 the newer swimming movement from it (2/3 of a step when fast, 1/2 when slow);
// - an old "continue at full life" rule (qr_CONTFULL_DEP, as stored) becomes the init flag, with
//   the continue HP 100 percent and the starting life the maximum;
// - before version 46 the sprite height thresholds come from the old "drawn above layer 3" one;
// - before version 48 (newer layout only) the hero's item box is 16 high at y 0 under
//   qr_LTTPCOLLISION;
// - the "more" icon of messages goes to the strings (zqn_255_strings.cpp).
// The editor's map and screen are written as the editor shows them (the quest's own in a batch
// save). Then 3.0's writeinitdata, version 52.
//
// Old generic script settings and screen script data (versions 33-36) are only taken when
// empty; a quest that has any is refused (none in the pack has).

#include "precompiled.h" //always first

#include <string.h>
#include <vector>

#include "zdefs.h"
#include "zq_class.h"
#include "zq_writers.h"
#include "zq_tape.h"
#include "zqn_255.h"

extern word map_count;
extern bool zq_batch_load;

namespace
{

enum { CR_LIFE, CR_MONEY, CR_BOMBS, CR_ARROWS, CR_MAGIC, CR_KEYS, CR_SBOMBS, CR_CUSTOM1, MAX_COUNTERS = 107 };
enum { LI_MCGUFFIN = 1, LI_MAP = 2, LI_COMPASS = 4, LI_BOSSKEY = 16 };
enum { QR_CONTFULL_DEP = 20, QR_LTTPCOLLISION = 80 };
const int NUM_LEVELS = 512;

// p_putbvec / p_putbmap of number containers with a default of 0 (as zqn_init.cpp)
void put_bounded(Zqn255Out &o, int size_bytes, dword size, const std::vector<long> &vals, int value_bytes)
{
    if(size_bytes == 1) o.c(size); else if(size_bytes == 2) o.w(size); else o.l(size);

    if(!size)
        return;

    size_t used = vals.size();

    while(used && !vals[used - 1])
        --used;

    size_t set = 0;

    for(size_t q = 0; q < used; ++q)
        if(vals[q])
            ++set;

    const bool pairs = set * (size_t)(value_bytes + size_bytes) <= used * (size_t)value_bytes;
    o.c(pairs ? 1 : 0);
    const size_t n = pairs ? set : used;

    if(size_bytes == 1) o.c((int64_t)n); else if(size_bytes == 2) o.w((int64_t)n); else o.l((int64_t)n);

    for(size_t q = 0; q < used; ++q)
    {
        if(pairs)
        {
            if(!vals[q])
                continue;

            if(size_bytes == 1) o.c((int64_t)q); else if(size_bytes == 2) o.w((int64_t)q); else o.l((int64_t)q);
        }

        if(value_bytes == 1) o.c(vals[q]); else if(value_bytes == 2) o.w(vals[q]); else o.l(vals[q]);
    }
}

void put_bits(Zqn255Out &o, const std::vector<byte> &bits)    // bit i in bits[i/8]
{
    std::vector<long> v(bits.begin(), bits.end());
    put_bounded(o, 2, 65535, v, 1);
}

void put_empty(Zqn255Out &o, int size_bytes, dword size)
{
    put_bounded(o, size_bytes, size, std::vector<long>(), 1);
}

bool bit_of(const std::vector<byte> &b, int n)
{
    return (size_t)(n >> 3) < b.size() && ((b[n >> 3] >> (n & 7)) & 1);
}

void set_bit_in(std::vector<byte> &b, int n, bool on)
{
    if((size_t)(n >> 3) >= b.size())
        b.resize((n >> 3) + 1, 0);

    if(on)
        b[n >> 3] |= (byte)(1 << (n & 7));
    else
        b[n >> 3] &= (byte)~(1 << (n & 7));
}

// a bitstring field's bits (bit i in byte i/8)
std::vector<byte> bitstr_bits(const TapeField *f)
{
    std::vector<byte> out;

    if(!f)
        return out;

    std::vector<byte> vals = zq_bitstr_bits(f->bytes);    // one byte per index

    for(size_t i = 0; i < vals.size(); ++i)
        if(vals[i])
            for(int k = 0; k < 8; ++k)
                if((vals[i] >> k) & 1)
                    set_bit_in(out, (int)(i * 8 + k), true);

    return out;
}

// a p_getbvec field's values (one byte per index)
std::vector<long> bvec_values(const TapeField *f)
{
    std::vector<long> out;

    if(!f)
        return out;

    std::vector<byte> vals = zq_bitstr_bits(f->bytes);
    out.assign(vals.begin(), vals.end());
    return out;
}

// The quest's own section of another id (its version, data).
bool other_section(int target, dword id, Zqn255Section &s)
{
    ZqNewerContext c;
    c.target = target;
    c.src_zelda_version = 0x255;
    c.src_build = 0;
    c.src_part = zq_find_part(id);
    c.src_version = c.src_cversion = 0;

    if(!c.src_part)
        return false;

    zq_part_versions(*c.src_part, &c.src_version, &c.src_cversion);
    return zqn255_section(c, s);
}

struct Init
{
    std::vector<byte> items, flags;
    word litems[NUM_LEVELS];
    std::vector<long> level_keys;
    word counter[MAX_COUNTERS], mcounter[MAX_COUNTERS];
    long bomb_ratio, hcp, hcp_per_hc, cont_heart, hp_per_heart, magic_per_block, hero_dmg, ene_dmg;
    long dither_type, dither_arg, dither_percent, def_lightrad, transdark_percent, darkcol;
    long ss[8];
    long last_map, last_screen, msg_speed, msg_advance_delay;
    long gravity, swimgravity, terminalv, swim_speed, swim_mult, swim_div;
    long sideswim_up, sideswim_side, sideswim_down, exit_water_jump, hero_step, anim_style, jump_threshold;
    long bunny_ltm, start_dmap, subscr_speed, switchhook, magic_drain, shove_offset;
    // the generic script settings and screen data, as the newer layout stores them (empty: none)
    std::vector<byte> gen_doscript, gen_exitstate, gen_reloadstate, gen_initd, gen_eventstate, gen_data, screen_data;
    long flicker_speed, flicker_color, flicker_transp, air_drag, light_wave_rate, light_wave_size, region_mapping;
    std::vector<byte> bottles, lvlswitches;
    long item_spawn_flicker, item_timeout_dur, item_timeout_flicker, item_flicker_speed;
    long z_threshold[5];
    long itembox[4];
    std::vector<byte> gen_inst, global_scripts, hero_scripts;
    long vp[4], vp_lookahead_speed, vp_recenter_speed, vp_recenter_delay;

    // zinitdata's defaults (3.0's initdata.h)
    Init()
    {
        memset(litems, 0, sizeof(litems));
        memset(counter, 0, sizeof(counter));
        memset(mcounter, 0, sizeof(mcounter));
        mcounter[CR_MONEY] = mcounter[CR_KEYS] = 255;
        level_keys.assign(NUM_LEVELS, 0);
        bomb_ratio = 4; hcp = 0; hcp_per_hc = 4; cont_heart = 100;
        hp_per_heart = 16; magic_per_block = 32; hero_dmg = 2; ene_dmg = 4;
        dither_type = 0; dither_arg = 0; dither_percent = 20; def_lightrad = 24; transdark_percent = 0; darkcol = 253;
        ss[0] = 8; ss[1] = 8; ss[2] = 0; ss[3] = 0; ss[4] = 8; ss[5] = 15; ss[6] = 7; ss[7] = 0;
        last_map = last_screen = 0; msg_speed = 5; msg_advance_delay = 50;
        gravity = 1600; swimgravity = 5; terminalv = 320; swim_speed = 67; swim_mult = 2; swim_div = 3;
        sideswim_up = 150; sideswim_side = 100; sideswim_down = 75; exit_water_jump = 0; hero_step = 150;
        anim_style = 0; jump_threshold = 255; bunny_ltm = 0; start_dmap = 0; subscr_speed = 1; switchhook = 0;
        magic_drain = 2; shove_offset = 65000;
        flicker_speed = 1; flicker_color = 0; flicker_transp = 0; air_drag = 100; light_wave_rate = 0;
        light_wave_size = 0; region_mapping = 0;
        bottles.assign(256, 0);
        item_spawn_flicker = 32; item_timeout_dur = 512; item_timeout_flicker = 0; item_flicker_speed = 2;
        memset(z_threshold, 0, sizeof(z_threshold));
        itembox[0] = 0; itembox[1] = 8; itembox[2] = 16; itembox[3] = 8;
        vp[0] = vp[1] = vp[2] = vp[3] = 0; vp_lookahead_speed = 10000; vp_recenter_speed = 0; vp_recenter_delay = 30;
    }
};

// readinitdata_old, for the versions 2.55 wrote (19-36)
bool read_old(const TapeIndex &t, int v, Init &z, std::string &why)
{
    const TapeField *items = t.find("items");

    for(int i = 0; items && i < 256 && i < (int)items->bytes.size(); ++i)
        if(items->bytes[i])
            set_bit_in(z.items, i, true);

    if(v < 29)
    {
        z.counter[CR_BOMBS] = (word)t.get("bombs_old");
        z.counter[CR_SBOMBS] = (word)t.get("super_bombs_old");
        z.mcounter[CR_BOMBS] = (word)t.get("max_bombs_old");
    }

    z.mcounter[CR_LIFE] = (word)t.get("hc");
    z.counter[CR_LIFE] = (word)t.get("start_heart");
    z.cont_heart = t.get("cont_heart");
    z.hcp = t.get("hcp");
    z.hcp_per_hc = t.get("hcp_per_hc");
    z.counter[CR_KEYS] = (word)t.get("keys");
    z.counter[CR_MONEY] = (word)t.get("rupies");
    const long tri = t.get("triforce");

    for(int q = 0; q < 8; ++q)
        if((tri >> q) & 1)
            z.litems[q + 1] |= LI_MCGUFFIN;

    const TapeField *maps = t.find("map_bits"), *comps = t.find("compass_bits"), *boss = t.find("bosskey_bits");

    for(int q = 0; q < 64 * 8; ++q)
    {
        if(maps && bit_of(maps->bytes, q)) z.litems[q] |= LI_MAP;
        if(comps && bit_of(comps->bytes, q)) z.litems[q] |= LI_COMPASS;
        if(boss && bit_of(boss->bytes, q)) z.litems[q] |= LI_BOSSKEY;
    }

    const TapeField *misc = t.find("misc");
    const bool contpercent = misc && bit_of(misc->bytes, 0);
    set_bit_in(z.flags, 0, contpercent);                       // INIT_FL_CONTPERCENT
    z.magic_drain = misc && bit_of(misc->bytes, 1) ? 1 : 2;     // double magic
    set_bit_in(z.flags, 1, misc && bit_of(misc->bytes, 2));    // INIT_FL_CANSLASH
    z.last_map = t.get("last_map");
    z.last_screen = t.get("last_screen");
    z.mcounter[CR_MAGIC] = (word)t.get("max_magic");
    z.counter[CR_MAGIC] = (word)t.get("magic");
    z.bomb_ratio = t.get("bomb_ratio");

    if(z.bomb_ratio < 1)
        z.bomb_ratio = 1;

    z.start_dmap = t.get("start_dmap");
    z.anim_style = t.get("animation_style");

    if(v < 29)
    {
        z.counter[CR_ARROWS] = (word)t.get("arrows_old");
        z.mcounter[CR_ARROWS] = (word)t.get("max_arrows_old");
    }

    const TapeField *keys = t.find("level_keys");

    for(int i = 0; keys && i < NUM_LEVELS && i < (int)keys->bytes.size(); ++i)
        z.level_keys[i] = keys->bytes[i];

    static const char *const grid[8] = { "ss_grid_x", "ss_grid_y", "ss_grid_xofs", "ss_grid_yofs", "ss_grid_color",
                                         "ss_bbox_1_color", "ss_bbox_2_color", "ss_flags" };

    for(int q = 0; q < 8; ++q)
        z.ss[q] = (word)t.get(grid[q]);     // words, read into 32 bits

    if(z.ss[0] < 1) z.ss[0] = 1;
    if(z.ss[1] < 1) z.ss[1] = 1;

    z.mcounter[CR_MONEY] = (word)t.get("max_rupees");
    z.mcounter[CR_KEYS] = (word)t.get("max_keys");
    z.gravity = t.get("gravity_old") * 100;
    z.terminalv = t.get("terminalv");
    z.msg_speed = t.get("msg_speed");
    z.jump_threshold = t.get("jump_threshold");

    if(v >= 19)
    {
        z.counter[CR_BOMBS] = (word)t.get("bombs");
        z.counter[CR_SBOMBS] = (word)t.get("super_bombs");
        z.mcounter[CR_BOMBS] = (word)t.get("max_bombs");
        z.mcounter[CR_SBOMBS] = (word)t.get("max_sbombs");
        z.counter[CR_ARROWS] = (word)t.get("arrows");
        z.mcounter[CR_ARROWS] = (word)t.get("max_arrows");
    }

    z.hero_step = v >= 20 ? t.get("hero_step") : 150;
    z.subscr_speed = v >= 21 ? t.get("subscr_speed") : 1;

    if(v > 21)
    {
        z.hp_per_heart = t.get("hp_per_heart");
        z.magic_per_block = t.get("magic_per_block");
        z.hero_dmg = t.get("hero_dmg_mult");
        z.ene_dmg = t.get("ene_dmg_mult");
    }

    if(v > 22)
    {
        char k[24];

        for(int q = 0; q < 25; ++q)
        {
            snprintf(k, sizeof(k), "custom%d", q);
            z.counter[CR_CUSTOM1 + q] = (word)t.get(k);
            snprintf(k, sizeof(k), "mcustom%d", q);
            z.mcounter[CR_CUSTOM1 + q] = (word)t.get(k);
        }
    }

    if(v > 23)
    {
        z.dither_type = t.get("dither_type");
        z.dither_arg = t.get("dither_arg");
        z.dither_percent = t.get("dither_percent");
        z.def_lightrad = t.get("def_lightrad");
        z.transdark_percent = t.get("transdark_percent");
    }

    if(v > 24)
        z.darkcol = t.get("darkcol");

    if(v > 25)
    {
        z.gravity = t.get("gravity");
        z.swimgravity = t.get("swimgravity");
    }

    if(v > 26)
    {
        z.sideswim_up = t.get("sideswim_up");
        z.sideswim_side = t.get("sideswim_side");
        z.sideswim_down = t.get("sideswim_down");
    }

    if(v > 27) z.exit_water_jump = t.get("exit_water_jump");
    if(v > 29) z.bunny_ltm = t.get("bunny_ltm");
    z.switchhook = v > 30 ? t.get("switchhook_style") : 1;
    if(v > 31) z.magic_drain = t.get("magic_drain");

    if(v > 32)
    {
        // a word count, then per script a flags byte (2: stored) and its settings
        const TapeField *g = t.find("generic");

        if(g)
            for(size_t q = 0; q < g->bytes.size(); ++q)
                if(q >= 2 && g->bytes[q])
                {
                    why = "it has old generic script settings in its init data (not converted yet)";
                    return false;
                }
    }

    if(v > 33)
    {
        z.swim_mult = t.get("swim_mult");
        z.swim_div = t.get("swim_div");
    }

    if(v > 34)
    {
        const TapeField *sd = t.find("screen_data");

        if(sd)
            for(size_t q = 0; q < sd->bytes.size(); ++q)
                if(sd->bytes[q])
                {
                    why = "it has old screen script data in its init data (not converted yet)";
                    return false;
                }
    }

    if(v > 35)
        z.shove_offset = t.get("shove_offset");

    z.counter[CR_LIFE] = (word)(z.counter[CR_LIFE] * z.hp_per_heart);
    z.mcounter[CR_LIFE] = (word)(z.mcounter[CR_LIFE] * z.hp_per_heart);

    if(!contpercent)
        z.cont_heart = (word)(z.cont_heart * z.hp_per_heart);

    return true;
}

std::vector<byte> raw_of(const TapeIndex &t, const char *key)
{
    const TapeField *f = t.find(key);
    return f ? f->bytes : std::vector<byte>();
}

// readinitdata's newer layout (37 on)
void read_new(const TapeIndex &t, int v, Init &z, bool lttp_collision)
{
    if(v >= 47)
        z.items = bitstr_bits(t.find("items"));
    else
    {
        const TapeField *f = t.find("items");

        if(f)
            z.items = f->bytes;
    }

    char k[32];

    if(v >= 42)
    {
        for(int q = 0; q < NUM_LEVELS; ++q)
        {
            snprintf(k, sizeof(k), "litem%d", q);
            z.litems[q] = (word)t.get(k);
        }
    }
    else
    {
        for(int q = 0; q < NUM_LEVELS / 8; ++q)
        {
            static const char *const what[4] = { "lmap", "lcompass", "lboss", "lmcg" };
            static const word li[4] = { LI_MAP, LI_COMPASS, LI_BOSSKEY, LI_MCGUFFIN };

            for(int w = 0; w < 4; ++w)
            {
                snprintf(k, sizeof(k), "%s%d", what[w], q);
                const long b = t.get(k);

                for(int j = 0; j < 8; ++j)
                    if((b >> j) & 1)
                        z.litems[q * 8 + j] |= li[w];
            }
        }
    }

    std::vector<long> lk = bvec_values(t.find("level_keys"));
    z.level_keys.assign(NUM_LEVELS, 0);

    for(size_t i = 0; i < lk.size() && i < (size_t)NUM_LEVELS; ++i)
        z.level_keys[i] = lk[i];

    const int counters = (int)t.get("num_counters");

    for(int q = 0; q < counters && q < MAX_COUNTERS; ++q)
    {
        snprintf(k, sizeof(k), "ctr%d", q);
        z.counter[q] = (word)t.get(k);
        snprintf(k, sizeof(k), "mctr%d", q);
        z.mcounter[q] = (word)t.get(k);
    }

    z.bomb_ratio = t.get("bomb_ratio");
    z.hcp = t.get("hcp");
    z.hcp_per_hc = t.get("hcp_per_hc");
    z.cont_heart = t.get("cont_heart");
    z.hp_per_heart = t.get("hp_per_heart");
    z.magic_per_block = t.get("magic_per_block");
    z.hero_dmg = t.get("hero_dmg_mult");
    z.ene_dmg = t.get("ene_dmg_mult");
    z.dither_type = t.get("dither_type");
    z.dither_arg = t.get("dither_arg");
    z.dither_percent = t.get("dither_percent");
    z.def_lightrad = t.get("def_lightrad");
    z.transdark_percent = t.get("transdark_percent");
    z.darkcol = t.get("darkcol");
    static const char *const grid[8] = { "ss_grid_x", "ss_grid_y", "ss_grid_xofs", "ss_grid_yofs", "ss_grid_color",
                                         "ss_bbox_1_color", "ss_bbox_2_color", "ss_flags" };

    for(int q = 0; q < 8; ++q)
        z.ss[q] = t.get(grid[q]);

    z.flags = bitstr_bits(t.find("flags"));
    z.last_map = t.get("last_map");
    z.last_screen = t.get("last_screen");
    z.msg_speed = t.get("msg_speed");

    if(v >= 49)
        z.msg_advance_delay = t.get("msg_advance_delay");

    z.gravity = t.get("gravity");
    z.swimgravity = t.get("swimgravity");
    z.terminalv = t.get("terminalv");
    z.swim_speed = t.get("swim_speed");
    z.swim_mult = t.get("swim_mult");
    z.swim_div = t.get("swim_div");
    z.sideswim_up = t.get("sideswim_up");
    z.sideswim_side = t.get("sideswim_side");
    z.sideswim_down = t.get("sideswim_down");
    z.exit_water_jump = t.get("exit_water_jump");
    z.hero_step = t.get("hero_step");
    z.anim_style = t.get("animation_style");
    z.jump_threshold = t.get("jump_threshold");
    z.bunny_ltm = t.get("bunny_ltm");
    z.start_dmap = t.get("start_dmap");
    z.subscr_speed = t.get("subscr_speed");
    z.switchhook = t.get("switchhook_style");
    z.magic_drain = t.get("magic_drain");
    z.shove_offset = t.get("shove_offset");
    z.gen_doscript = raw_of(t, "gen_doscript");
    z.gen_exitstate = raw_of(t, "gen_exitstate");
    z.gen_reloadstate = raw_of(t, "gen_reloadstate");
    z.gen_initd = raw_of(t, "gen_initd");
    z.gen_eventstate = raw_of(t, "gen_eventstate");
    z.gen_data = raw_of(t, "gen_data");
    z.screen_data = raw_of(t, "screen_data");

    if(v >= 38)
    {
        z.flicker_speed = t.get("flicker_speed");
        z.flicker_color = t.get("flicker_color");
        z.flicker_transp = t.get("flicker_transp");
    }

    if(v >= 39) z.air_drag = t.get("air_drag");

    if(t.find("region_only"))
        z.region_mapping = t.get("region_only");
    else
    {
        if(v >= 40)
        {
            z.light_wave_rate = t.get("light_wave_rate");
            z.light_wave_size = t.get("light_wave_size");
        }

        if(v >= 41) z.region_mapping = t.get("region_mapping");
    }

    if(v >= 43)
    {
        const TapeField *b = t.find("bottles");

        if(b && b->bytes.size() == 256)
            z.bottles = b->bytes;
    }

    if(v >= 44) z.lvlswitches = raw_of(t, "lvlswitches");

    if(v >= 45)
    {
        z.item_spawn_flicker = t.get("item_spawn_flicker");
        z.item_timeout_dur = t.get("item_timeout_dur");
        z.item_timeout_flicker = t.get("item_timeout_flicker");
        z.item_flicker_speed = t.get("item_flicker_speed");
    }

    if(v >= 46)
        for(int q = 0; q < 5; ++q)
        {
            snprintf(k, sizeof(k), "z_threshold%d", q);
            z.z_threshold[q] = t.get(k);
        }

    if(v >= 48)
    {
        z.itembox[0] = t.get("itembox_xofs");
        z.itembox[1] = t.get("itembox_yofs");
        z.itembox[2] = t.get("itembox_w");
        z.itembox[3] = t.get("itembox_h");
    }
    else if(lttp_collision)
    {
        z.itembox[1] = 0;
        z.itembox[3] = 16;
    }

    if(v >= 50)
    {
        z.gen_inst = raw_of(t, "gen_inst");
        z.global_scripts = raw_of(t, "global_scripts");
        z.hero_scripts = raw_of(t, "hero_scripts");
    }

    // the viewport (3.0's own versions 51 and 52, from 3.0 prereleases)
    if(v >= 51)
    {
        z.vp[0] = t.get("vf.deadzone_w");
        z.vp[1] = t.get("vf.deadzone_h");
    }

    if(v >= 52)
    {
        z.vp[2] = t.get("vf.lookahead_x");
        z.vp[3] = t.get("vf.lookahead_y");
        z.vp_lookahead_speed = t.get("vf.lookahead_speed");
        z.vp_recenter_speed = t.get("vf.recenter_speed");
        z.vp_recenter_delay = t.get("vf.recenter_delay");
    }
}

// A script slot's configuration without its script: 8 InitD values, no instance values.
void put_script_configs(Zqn255Out &o, int count)
{
    o.w(count);

    for(int q = 0; q < count; ++q)
    {
        o.w(8);
        o.zeros(8 * 4);
        o.w(0);
    }
}

void put_raw_or_empty(Zqn255Out &o, const std::vector<byte> &raw, int size_bytes, dword size)
{
    if(!raw.empty())
        o.put(raw);
    else
        put_empty(o, size_bytes, size);
}

int write_init(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 19 || ctx.src_version > 52 || ctx.target != ZQ_NEWER_30)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const int v = s.version;
    Tape tape;
    TapeParser p(s.data.empty() ? NULL : &s.data[0], s.data.size(), 0, &tape);

    if(!zq_tape_init(p, v) || p.bad || p.pos != s.data.size())
    {
        zqw_last_error = "its init data isn't laid out as expected";
        return 2;
    }

    const TapeIndex t(tape);

    // the rules: as stored (the old continue rule) and as 3.0 holds them by then (zqn_255_rules)
    Zqn255Section rules;
    bool contfull = false, lttp = false;

    if(other_section(ctx.target, ID_RULES, rules) && rules.data.size() >= 100)
    {
        contfull = bit_of(rules.data, QR_CONTFULL_DEP);
        lttp = bit_of(rules.data, QR_LTTPCOLLISION);    // not one readrules changes for 2.55 quests
    }

    Init z;
    std::string why;

    if(v < 37 ? !read_old(t, v, z, why) : (read_new(t, v, z, lttp), false))
    {
        zqw_last_error = why;
        return 2;
    }

    // Link's sprites: the swimming speed, and for init data older than 34 the newer movement
    Zqn255Section link;

    if(other_section(ctx.target, ID_LINKSPRITES, link) && link.version > 4)
    {
        const size_t at = 168 + 6 + 2 * (size_t)(link.version > 6 ? 3 : 2) * 6 + 48;

        if(at < link.data.size())
            z.swim_speed = link.data[at];
    }

    if(v < 34)
    {
        const bool fast = z.swim_speed > 60;
        z.swim_mult = fast ? 2 : 1;
        z.swim_div = fast ? 3 : 2;
    }

    if(contfull)
    {
        set_bit_in(z.flags, 0, true);
        z.cont_heart = 100;
        z.counter[CR_LIFE] = z.mcounter[CR_LIFE];
    }

    if(v < 46)
    {
        z.z_threshold[0] = z.z_threshold[1] = z.z_threshold[2] = z.jump_threshold;
        z.z_threshold[3] = z.z_threshold[4] = 0xFFFF;
    }

    // the editor's map and screen (the quest's own in a batch save)
    if(!zq_batch_load)
    {
        int map = Map.getCurrMap(), scr = Map.getCurrScr();

        if(map_count > 0 && map > map_count - 1)
            map = map_count - 1;

        z.last_map = map;
        z.last_screen = scr > 135 ? 135 : scr;
    }

    Zqn255Out o;
    put_bits(o, z.items);

    for(int q = 0; q < NUM_LEVELS; ++q)
        o.w(z.litems[q]);

    put_bounded(o, 2, NUM_LEVELS, z.level_keys, 1);
    o.c(MAX_COUNTERS);

    for(int q = 0; q < MAX_COUNTERS; ++q)
        o.w(z.counter[q]);

    for(int q = 0; q < MAX_COUNTERS; ++q)
        o.w(z.mcounter[q]);

    o.c(z.bomb_ratio);
    o.c(z.hcp);
    o.c(z.hcp_per_hc);
    o.w(z.cont_heart);
    o.c(z.hp_per_heart);
    o.c(z.magic_per_block);
    o.c(z.hero_dmg);
    o.c(z.ene_dmg);
    o.c(z.dither_type);
    o.c(z.dither_arg);
    o.c(z.dither_percent);
    o.c(z.def_lightrad);
    o.c(z.transdark_percent);
    o.c(z.darkcol);

    for(int q = 0; q < 8; ++q)
        o.l(z.ss[q]);

    put_bits(o, z.flags);
    o.c(z.last_map);
    o.c(z.last_screen);
    o.c(z.msg_speed);
    o.c(z.msg_advance_delay);
    o.l(z.gravity);
    o.l(z.swimgravity);
    o.w(z.terminalv);
    o.c(z.swim_speed);
    o.c(z.swim_mult);
    o.c(z.swim_div);
    o.w(z.sideswim_up);
    o.w(z.sideswim_side);
    o.w(z.sideswim_down);
    o.l(z.exit_water_jump);
    o.w(z.hero_step);
    o.c(z.anim_style);
    o.c(z.jump_threshold);
    o.l(z.bunny_ltm);
    o.w(z.start_dmap);
    o.w(z.subscr_speed);
    o.c(z.switchhook);
    o.c(z.magic_drain);
    o.l(z.shove_offset);
    put_raw_or_empty(o, z.gen_doscript, 2, 65535);
    put_raw_or_empty(o, z.gen_exitstate, 2, 512);
    put_raw_or_empty(o, z.gen_reloadstate, 2, 512);
    put_raw_or_empty(o, z.gen_initd, 2, 512);
    put_raw_or_empty(o, z.gen_eventstate, 2, 512);
    put_raw_or_empty(o, z.gen_data, 2, 512);
    put_raw_or_empty(o, z.screen_data, 4, 255 * 136);
    o.c(z.flicker_speed);
    o.c(z.flicker_color);
    o.c(z.flicker_transp);
    o.l(z.air_drag);
    o.w(z.light_wave_rate);
    o.w(z.light_wave_size);
    o.c(z.region_mapping);
    o.put(z.bottles);
    put_raw_or_empty(o, z.lvlswitches, 2, NUM_LEVELS);
    o.w(z.item_spawn_flicker);
    o.w(z.item_timeout_dur);
    o.w(z.item_timeout_flicker);
    o.c(z.item_flicker_speed);

    for(int q = 0; q < 5; ++q)
        o.w(z.z_threshold[q]);

    for(int q = 0; q < 4; ++q)
        o.c(z.itembox[q]);

    put_raw_or_empty(o, z.gen_inst, 2, 512);

    if(!z.global_scripts.empty())
        o.put(z.global_scripts);
    else
        put_script_configs(o, 8);

    if(!z.hero_scripts.empty())
        o.put(z.hero_scripts);
    else
        put_script_configs(o, 5);

    for(int q = 0; q < 4; ++q)
        o.c(z.vp[q]);

    o.l(z.vp_lookahead_speed);
    o.l(z.vp_recenter_speed);
    o.w(z.vp_recenter_delay);
    std::vector<byte> none;
    return zqn255_emit(f, ID_INITDATA, 52, 52, none, o.b);
}

ZqNewerRegistration g_reg_init(ID_INITDATA, write_init, "2.55 init data as 3.0");

// ---- 2.55.17 --------------------------------------------------------------------------------
//
// 2.55.17's readinitdata reads versions before 37 with the same readinitdata_old as 3.0 (read_old
// above), and the newer layout (37) as read_new does; the rest of its load does the same to the
// init data (Link's swimming speed, the old "continue at full life" rule). Its zinitdata keeps
// the levels' items as four bit arrays (maps, compasses, boss keys, McGuffins) and the messages'
// "more" icon, and writeinitdata writes its version 38: the newer layout with the sprite flicker
// settings at the end.
int write_init_255(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 19 || ctx.src_version > 37 || ctx.target != ZQ_NEWER_255)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const int v = s.version;
    Tape tape;
    TapeParser p(s.data.empty() ? NULL : &s.data[0], s.data.size(), 0, &tape);

    if(!zq_tape_init(p, v) || p.bad || p.pos != s.data.size())
    {
        zqw_last_error = "its init data isn't laid out as expected";
        return 2;
    }

    const TapeIndex t(tape);
    Zqn255Section rules;
    bool contfull = false;

    if(other_section(ctx.target, ID_RULES, rules) && rules.data.size() >= 100)
        contfull = bit_of(rules.data, QR_CONTFULL_DEP);

    Init z;
    std::string why;

    if(v < 37 ? !read_old(t, v, z, why) : (read_new(t, v, z, false), false))
    {
        zqw_last_error = why;
        return 2;
    }

    // Link's sprites: the swimming speed, and for init data older than 34 the newer movement
    Zqn255Section link;

    if(other_section(ctx.target, ID_LINKSPRITES, link) && link.version > 4)
    {
        const size_t at = 168 + 6 + 2 * (size_t)(link.version > 6 ? 3 : 2) * 6 + 48;

        if(at < link.data.size())
            z.swim_speed = link.data[at];
    }

    if(v < 34)
    {
        const bool fast = z.swim_speed > 60;
        z.swim_mult = fast ? 2 : 1;
        z.swim_div = fast ? 3 : 2;
    }

    if(contfull)
    {
        set_bit_in(z.flags, 0, true);
        z.cont_heart = 100;
        z.counter[CR_LIFE] = z.mcounter[CR_LIFE];
    }

    if(!zq_batch_load)
    {
        int map = Map.getCurrMap(), scr = Map.getCurrScr();

        if(map_count > 0 && map > map_count - 1)
            map = map_count - 1;

        z.last_map = map;
        z.last_screen = scr > 135 ? 135 : scr;
    }

    // the "more" icon (from version 18 its Y can be an offset)
    const long more_x = t.get("msg_more_x"), more_y = t.get("msg_more_y");
    const long more_offset = t.find("msg_more_is_offset") ? t.get("msg_more_is_offset") : 0;

    Zqn255Out o;
    std::vector<byte> items = z.items;
    items.resize(256 / 8, 0);
    o.put(&items[0], 256 / 8);

    // the levels' items as bit arrays, 8 levels to the byte, the four arrays interleaved
    for(int q = 0; q < NUM_LEVELS / 8; ++q)
    {
        static const word li[4] = { LI_MAP, LI_COMPASS, LI_BOSSKEY, LI_MCGUFFIN };

        for(int w = 0; w < 4; ++w)
        {
            byte b = 0;

            for(int j = 0; j < 8; ++j)
                if(z.litems[q * 8 + j] & li[w])
                    b |= (byte)(1 << j);

            o.c(b);
        }
    }

    put_bounded(o, 2, NUM_LEVELS, z.level_keys, 1);
    o.c(MAX_COUNTERS);

    for(int q = 0; q < MAX_COUNTERS; ++q)
        o.w(z.counter[q]);

    for(int q = 0; q < MAX_COUNTERS; ++q)
        o.w(z.mcounter[q]);

    o.c(z.bomb_ratio);
    o.c(z.hcp);
    o.c(z.hcp_per_hc);
    o.w(z.cont_heart);
    o.c(z.hp_per_heart);
    o.c(z.magic_per_block);
    o.c(z.hero_dmg);
    o.c(z.ene_dmg);
    o.c(z.dither_type);
    o.c(z.dither_arg);
    o.c(z.dither_percent);
    o.c(z.def_lightrad);
    o.c(z.transdark_percent);
    o.c(z.darkcol);

    for(int q = 0; q < 8; ++q)
        o.l(z.ss[q]);

    put_bits(o, z.flags);
    o.c(z.last_map);
    o.c(z.last_screen);
    o.c(more_x);
    o.c(more_y);
    o.c(more_offset);
    o.c(z.msg_speed);
    o.l(z.gravity);
    o.l(z.swimgravity);
    o.w(z.terminalv);
    o.c(z.swim_speed);
    o.c(z.swim_mult);
    o.c(z.swim_div);
    o.w(z.sideswim_up);
    o.w(z.sideswim_side);
    o.w(z.sideswim_down);
    o.l(z.exit_water_jump);
    o.w(z.hero_step);
    o.c(z.anim_style);
    o.c(z.jump_threshold);
    o.l(z.bunny_ltm);
    o.w(z.start_dmap);
    o.w(z.subscr_speed);
    o.c(z.switchhook);
    o.c(z.magic_drain);
    o.l(z.shove_offset);
    put_raw_or_empty(o, z.gen_doscript, 2, 65535);
    put_raw_or_empty(o, z.gen_exitstate, 2, 512);
    put_raw_or_empty(o, z.gen_reloadstate, 2, 512);
    put_raw_or_empty(o, z.gen_initd, 2, 512);
    put_raw_or_empty(o, z.gen_eventstate, 2, 512);
    put_raw_or_empty(o, z.gen_data, 2, 512);
    put_raw_or_empty(o, z.screen_data, 4, 255 * 136);
    o.c(z.flicker_speed);
    o.c(z.flicker_color);
    o.c(z.flicker_transp);
    std::vector<byte> none;
    return zqn255_emit(f, ID_INITDATA, 38, 15, none, o.b);
}

ZqNewerRegistration g_reg_init_255(ID_INITDATA, write_init_255, "2.55 init data as 2.55.17");

} // namespace

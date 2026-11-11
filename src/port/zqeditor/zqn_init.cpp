// The init data (INIT) of a quest made in 2.53.1 or older, saved as 2.55.17 (section version
// 38) or 3.0 Nightly (version 52). See zq_writers.h for what a newer-version writer does.
//
// Upstream reads old init data with readinitdata_old, which is 2.53's own reader with the
// values moved into its newer fields: the hearts become the life counter in HP (16 per heart),
// the old item bytes and counters become the counter tables, the level item bytes become one
// word per level (3.0) or four bit arrays (2.55), and everything 2.53 never had takes the
// reader's default. 2.53's load already made every other conversion that reader makes (old
// item families, the rules it folds in and clears, the "continue at full life" rule), so the
// values all come from ZQuest's init data as it stands.
//
// Two values come from other parts of the load:
//   - the swimming speed belongs to Link's sprites: the sprite reader sets it (67 unless the
//     part stores it), and runs when the quest has a LINK part, a GUY part of version 2 or
//     older, or no sections. When it doesn't run, 2.53 and 2.55 keep the 0 the init reader
//     left, while 3.0's init data starts at 67. When it runs, it also sets the newer swimming
//     movement (2/3 of a step when the old speed was fast, 1/2 when slow); otherwise that
//     stays at 2/3;
//   - the last map and screen are the ones the editor shows when it saves.
//
// And one differs between the targets: 2.55's init data starts at 0 where 3.0's has defaults,
// so Link's step, which the reader sets in a part it skips for quests older than 1.92 build
// 27, is 0 for those in 2.55 and 150 in 3.0.
//
// Upstream writes the "more" icon of messages (3.0 moved it from the init data to each string)
// in the strings; zqn_strings.cpp does that.

#include "precompiled.h" //always first

#include <string.h>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "zq_class.h"
#include "zq_writers.h"
#include "zqn_edit_test.h"

extern zinitdata zinit;
extern word map_count;
extern bool zq_batch_load;

namespace
{

// Upstream's counters (general.h): life, money, bombs, arrows, magic, keys, super bombs, then
// 100 script counters.
enum { crLIFE_U, crMONEY_U, crBOMBS_U, crARROWS_U, crMAGIC_U, crKEYS_U, crSBOMBS_U, MAX_COUNTERS_U = 107 };
const int NUM_LEVELS = 512;             // MAXLEVELS, the same in 2.53, 2.55 and 3.0
const int NUM_SCRIPTS_GENERIC = 512;
const dword NUM_SCREENS = 255 * 136;    // MAXSCRS: every screen of every map
const int HEART_HP = 16;

struct Buf
{
    std::vector<byte> b;

    void c(long v) { b.push_back((byte)v); }
    void w(long v) { c(v); c(v >> 8); }
    void l(long v) { c(v); c(v >> 8); c(v >> 16); c(v >> 24); }
    void n(int bytes, long v) { if(bytes == 1) c(v); else if(bytes == 2) w(v); else l(v); }
    void zeros(size_t k) { b.insert(b.end(), k, (byte)0); }
};

// p_putbvec / p_putbmap of upstream's bounded containers (packfile.h), for values that are
// numbers with a default of 0: the container's size, then, if that isn't 0, whether keys are
// stored, a count, and the values. Keys are stored when that takes no more room than every
// value up to the last one that isn't 0.
void put_bounded(Buf &o, int size_bytes, dword size, const std::vector<long> &vals, int value_bytes)
{
    o.n(size_bytes, (long)size);

    if(!size)
        return;

    size_t used = vals.size();

    while(used && !vals[used - 1])
        --used;

    size_t set = 0;

    for(size_t q = 0; q < used; ++q)
        if(vals[q])
            ++set;

    bool pairs = set * (size_t)(value_bytes + size_bytes) <= used * (size_t)value_bytes;
    o.c(pairs ? 1 : 0);
    o.n(size_bytes, (long)(pairs ? set : used));

    for(size_t q = 0; q < used; ++q)
    {
        if(pairs)
        {
            if(!vals[q])
                continue;

            o.n(size_bytes, (long)q);
        }

        o.n(value_bytes, vals[q]);
    }
}

// A container that holds nothing.
void put_empty(Buf &o, int size_bytes, dword size)
{
    put_bounded(o, size_bytes, size, std::vector<long>(), 1);
}

// upstream's bitstring: a bounded vector of bytes, 65535 long
void put_bits(Buf &o, const std::vector<long> &bytes)
{
    put_bounded(o, 2, 65535, bytes, 1);
}

// The init data as upstream holds it after opening the quest.
struct Up
{
    std::vector<long> items;        // bytes of item bits
    word litems[NUM_LEVELS];        // 3.0: per level, 1 McGuffin, 2 map, 4 compass, 16 boss key
    std::vector<long> level_keys;
    word counter[MAX_COUNTERS_U], mcounter[MAX_COUNTERS_U];
    byte bomb_ratio, hcp, hcp_per_hc;
    word cont_heart;
    long ss[8];
    std::vector<long> flags;
    byte last_map, last_screen;
    byte msg_speed;
    long gravity;
    word terminalv;
    byte swim_speed, swim_mult, swim_div;
    byte anim_style, jump_threshold;
    word start_dmap;
    byte magic_drain;
    word step;
};

bool level_bit(const byte *bits, int level)
{
    return (bits[level >> 3] >> (level & 7)) & 1;
}

// Whether Link's sprite reader runs on this quest's load (see the top of the file).
bool hero_sprites_read(const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version < 0x193 || zq_find_part(ID_LINKSPRITES))
        return true;

    const ZqQuestPart *guys = zq_find_part(ID_GUYS);
    word v, cv;
    return guys && zq_part_versions(*guys, &v, &cv) && v <= 2;
}

// The init data's last map and screen right after the load, and again after each save in the
// quest's own version (which stores the editor's view in zinit, as 2.53's save does).
byte g_loaded_map, g_loaded_screen;

void note_loaded()
{
    g_loaded_map = zinit.last_map;
    g_loaded_screen = zinit.last_screen;
}

ZqLoadHookRegistration g_hook(note_loaded);

// The map and screen upstream's editor shows when it saves: where the editor is now. The batch
// save has no editor view, and ZQuest's batch load doesn't move the view to the quest's last
// screen as upstream's open does, so there it's the quest's own.
void last_map_screen(byte &m, byte &s)
{
    int map = g_loaded_map, scr = g_loaded_screen;

    if(!zq_batch_load)
    {
        map = Map.getCurrMap();
        scr = Map.getCurrScr();
    }

    if(map_count > 0 && map > map_count - 1)
        map = map_count - 1;

    if(scr > 135)
        scr = 135;

    m = (byte)map;
    s = (byte)scr;
}

void convert(const ZqNewerContext &ctx, Up &u)
{
    const zinitdata &z = zinit;

    u.items.assign(32, 0);

    for(int i = 0; i < 256; ++i)
        if(z.items[i])
            u.items[i >> 3] |= 1 << (i & 7);

    for(int q = 0; q < NUM_LEVELS; ++q)
    {
        word li = 0;

        if(q >= 1 && q <= 8 && ((z.triforce >> (q - 1)) & 1))
            li |= 1;
        if(level_bit(z.map, q))
            li |= 2;
        if(level_bit(z.compass, q))
            li |= 4;
        if(level_bit(z.boss_key, q))
            li |= 16;

        u.litems[q] = li;
    }

    u.level_keys.assign(NUM_LEVELS, 0);

    for(int q = 0; q < NUM_LEVELS; ++q)
        u.level_keys[q] = z.level_keys[q];

    memset(u.counter, 0, sizeof(u.counter));
    memset(u.mcounter, 0, sizeof(u.mcounter));
    // the reader reads hearts and multiplies them by the HP of a heart at the end
    u.counter[crLIFE_U] = (word)(z.start_heart * HEART_HP);
    u.mcounter[crLIFE_U] = (word)(z.hc * HEART_HP);
    u.counter[crMONEY_U] = z.rupies;
    u.mcounter[crMONEY_U] = z.max_rupees;
    u.counter[crBOMBS_U] = z.bombs;
    u.mcounter[crBOMBS_U] = z.max_bombs;
    u.counter[crARROWS_U] = z.arrows;
    u.mcounter[crARROWS_U] = z.max_arrows;
    u.counter[crMAGIC_U] = z.magic;
    u.mcounter[crMAGIC_U] = z.max_magic;
    u.counter[crKEYS_U] = z.keys;
    u.mcounter[crKEYS_U] = z.max_keys;
    u.counter[crSBOMBS_U] = z.super_bombs;
    // the reader stores no super bomb maximum for these versions (the game took it from the
    // bomb ratio); it works one out only for quests older than 2.50
    u.bomb_ratio = z.bomb_ratio ? z.bomb_ratio : 1;
    u.mcounter[crSBOMBS_U] = ctx.src_zelda_version < 0x250 ? (word)(z.max_bombs / u.bomb_ratio) : 0;

    u.hcp = z.hcp;
    u.hcp_per_hc = z.hcp_per_hc;
    bool cont_percent = get_bit((byte *)z.misc, idM_CONTPERCENT) != 0;
    u.cont_heart = cont_percent ? z.cont_heart : (word)(z.cont_heart * HEART_HP);
    u.ss[0] = z.ss_grid_x;
    u.ss[1] = z.ss_grid_y;
    u.ss[2] = z.ss_grid_xofs;
    u.ss[3] = z.ss_grid_yofs;
    u.ss[4] = z.ss_grid_color;
    u.ss[5] = z.ss_bbox_1_color;
    u.ss[6] = z.ss_bbox_2_color;
    u.ss[7] = z.ss_flags;
    // init flags: continue as a percentage, can slash
    u.flags.assign(1, (cont_percent ? 1 : 0) | (get_bit((byte *)z.misc, idM_CANSLASH) ? 2 : 0));
    last_map_screen(u.last_map, u.last_screen);
    u.msg_speed = z.msg_speed;
    u.gravity = (long)z.gravity * 100;
    u.terminalv = z.terminalv;
    u.swim_speed = z.link_swim_speed;
    u.swim_mult = 2;
    u.swim_div = 3;

    if(hero_sprites_read(ctx))
    {
        // the sprite reader turns the old speed into the newer movement's fraction of a step:
        // 2/3 when swimming was fast, 1/2 when slow
        bool fast = u.swim_speed > 60;
        u.swim_mult = fast ? 2 : 1;
        u.swim_div = fast ? 3 : 2;
    }
    else if(ctx.target == ZQ_NEWER_30)
        u.swim_speed = 67;

    u.anim_style = z.linkanimationstyle;
    u.jump_threshold = z.jump_link_layer_threshold;
    u.start_dmap = z.start_dmap;
    u.magic_drain = get_bit((byte *)z.misc, idM_DOUBLEMAGIC) ? 1 : 2;
    // Link's step: the reader sets 1.5 pixels a frame in a part it skips for quests from before
    // 1.92 build 27, which then keep 2.55's 0 or 3.0's 150
    bool skipped = ctx.src_zelda_version < 0x192 || (ctx.src_zelda_version == 0x192 && ctx.src_build <= 26);
    u.step = (skipped && ctx.target == ZQ_NEWER_255) ? 0 : 150;
}

void put_counters(Buf &o, const Up &u)
{
    o.c(MAX_COUNTERS_U);

    for(int q = 0; q < MAX_COUNTERS_U; ++q)
        o.w(u.counter[q]);

    for(int q = 0; q < MAX_COUNTERS_U; ++q)
        o.w(u.mcounter[q]);
}

// From the bomb ratio to the end of the generic script and screen data, which 2.55 and 3.0
// write alike but for the "more" icon (2.55) and the message advance delay (3.0).
void put_middle(Buf &o, const Up &u, int target)
{
    o.c(u.bomb_ratio);
    o.c(u.hcp);
    o.c(u.hcp_per_hc);
    o.w(u.cont_heart);
    o.c(HEART_HP);
    o.c(32);                    // magic per block
    o.c(2);                     // Link's damage multiplier
    o.c(4);                     // enemies' damage multiplier
    o.c(0);                     // dither type, argument and percent
    o.c(0);
    o.c(20);
    o.c(24);                    // default light radius
    o.c(0);                     // transparent darkness percent
    o.c(253);                   // darkness colour: BLACK

    for(int q = 0; q < 8; ++q)
        o.l(u.ss[q]);

    put_bits(o, u.flags);
    o.c(u.last_map);
    o.c(u.last_screen);

    if(target == ZQ_NEWER_255)
    {
        o.c(zinit.msg_more_x);
        o.c(zinit.msg_more_y);
        o.c(zinit.msg_more_is_offset);
        o.c(u.msg_speed);
    }
    else
    {
        o.c(u.msg_speed);
        o.c(50);                // message advance delay
    }

    o.l(u.gravity);
    o.l(5);                     // swimming gravity
    o.w(u.terminalv);
    o.c(u.swim_speed);
    o.c(u.swim_mult);
    o.c(u.swim_div);
    o.w(150);                   // sideview swimming steps: up, sideways, down
    o.w(100);
    o.w(75);
    o.l(0);                     // jump out of water
    o.w(u.step);
    o.c(u.anim_style);
    o.c(u.jump_threshold);
    o.l(0);                     // bunny item
    o.w(u.start_dmap);
    o.w(1);                     // subscreen scroll speed
    o.c(1);                     // switch hook style
    o.c(u.magic_drain);
    o.l(65000);                 // shove offset, 6.5
    put_empty(o, 2, 65535);     // generic scripts that run
    put_empty(o, 2, NUM_SCRIPTS_GENERIC);   // their exit states, reload states, InitD,
    put_empty(o, 2, NUM_SCRIPTS_GENERIC);   // event states and data
    put_empty(o, 2, NUM_SCRIPTS_GENERIC);
    put_empty(o, 2, NUM_SCRIPTS_GENERIC);
    put_empty(o, 2, NUM_SCRIPTS_GENERIC);
    put_empty(o, 4, NUM_SCREENS);           // screen data
    o.c(1);                     // sprite flicker speed, colour, transparency
    o.c(0);
    o.c(0);
}

// A script slot's configuration without its script: 8 InitD values and the instance values.
void put_script_configs(Buf &o, int count)
{
    o.w(count);

    for(int q = 0; q < count; ++q)
    {
        o.w(8);
        o.zeros(8 * 4);
        o.w(0);
    }
}

void body_255(Buf &o, const Up &u)
{
    for(int q = 0; q < 32; ++q)
        o.c(u.items[q]);

    for(int q = 0; q < NUM_LEVELS / 8; ++q)
    {
        byte map = 0, compass = 0, boss_key = 0, mcguffin = 0;

        for(int k = 0; k < 8; ++k)
        {
            word li = u.litems[q * 8 + k];
            map |= ((li >> 1) & 1) << k;
            compass |= ((li >> 2) & 1) << k;
            boss_key |= ((li >> 4) & 1) << k;
            mcguffin |= (li & 1) << k;
        }

        o.c(map);
        o.c(compass);
        o.c(boss_key);
        o.c(mcguffin);
    }

    put_bounded(o, 2, NUM_LEVELS, u.level_keys, 1);
    put_counters(o, u);
    put_middle(o, u, ZQ_NEWER_255);
}

void body_30(Buf &o, const Up &u)
{
    put_bits(o, u.items);

    for(int q = 0; q < NUM_LEVELS; ++q)
        o.w(u.litems[q]);

    put_bounded(o, 2, NUM_LEVELS, u.level_keys, 1);
    put_counters(o, u);
    put_middle(o, u, ZQ_NEWER_30);
    o.l(100);                   // air drag, 0.01
    o.w(0);                     // light wave rate and size
    o.w(0);
    o.c(0);                     // region mapping
    o.zeros(256);               // bottle slots
    put_empty(o, 2, NUM_LEVELS);    // level switch states
    o.w(32);                    // item spawn flicker, timeout, timeout flicker, flicker speed
    o.w(512);
    o.w(0);
    o.c(2);

    // sprite height thresholds: the old single "drawn above layer 3" height for the ground,
    // layer 3 and layer 4; none for overhead and layer 5
    o.w(u.jump_threshold);
    o.w(u.jump_threshold);
    o.w(u.jump_threshold);
    o.w(0xFFFF);
    o.w(0xFFFF);
    o.c(0);                     // Link's item box: x and y offset, width, height
    o.c(8);
    o.c(16);
    o.c(8);
    put_empty(o, 2, NUM_SCRIPTS_GENERIC);   // generic scripts' instance values
    put_script_configs(o, 8);   // global scripts
    put_script_configs(o, 5);   // Link's scripts
    o.c(0);                     // viewport follow: deadzone width, height, lookahead x, y
    o.c(0);
    o.c(0);
    o.c(0);
    o.l(10000);                 // lookahead speed 1, recenter speed 0, recenter delay 30
    o.l(0);
    o.w(30);
}

int zqn_initdata(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    zqn_edit_test();
    Up u;
    convert(ctx, u);
    Buf body;
    word version;

    if(ctx.target == ZQ_NEWER_255)
    {
        body_255(body, u);
        version = 38;
    }
    else
    {
        body_30(body, u);
        version = 52;
    }

    // 3.0 writes the version again where the cversion was; 2.55's init data cversion is 15
    word cversion = ctx.target == ZQ_NEWER_255 ? 15 : version;

    if(!p_mputl(ID_INITDATA, f) || !p_iputw(version, f) || !p_iputw(cversion, f) || !p_iputl((long)body.b.size(), f))
        return 1;

    if(!body.b.empty() && !pfwrite(&body.b[0], (long)body.b.size(), f))
        return 1;

    return ZQW_OK;
}

ZqNewerRegistration g_reg(ID_INITDATA, zqn_initdata, "init data for 2.55.17 and 3.0");

} // namespace

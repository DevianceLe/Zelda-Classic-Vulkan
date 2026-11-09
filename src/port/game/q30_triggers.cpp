// 3.0 combo triggers and combo extras. See q30_triggers.h.

#include "precompiled.h" //always first

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <algorithm>
#include <map>

#include "zelda.h"
#include "link.h"
#include "maps.h"
#include "guys.h"
#include "items.h"
#include "weapons.h"
#include "decorations.h"
#include "sfx.h"
#include "tiles.h"
#include "zc_sys.h"
#include "pal.h"
#include "q30.h"
#include "q30_triggers.h"

extern word quest_header_zelda_version;
extern newcombo *combobuf;
extern LinkClass Link;
extern sprite_list guys, Lwpns, Ewpns, items, decorations;
extern std::vector<mapscr> TheMaps;
extern word map_count;
void playLevelMusic();
bool usekey();
extern word msg_count;
extern bool darkroom, naturaldark;

static bool msgstr_blocking_step()
{
    return msgstr != 0 && get_bit(quest_rules, qr_MSGFREEZE);
}

// ---- data ----------------------------------------------------------------------------

Q30Trigger::Q30Trigger()
{
    triggerlevel = 0;
    triggerbtn = 0;
    triggeritem = 0;
    trigtimer = 0;
    trigsfx = 0;
    trigchange = 0;
    trigprox = 0;
    trigctr = 0;
    trigctramnt = 0;
    triglbeam = 0;
    trigcschange = 0;
    spawnitem = 0;
    spawnenemy = 0;
    exstate = -1;
    exdoor_dir = -1;
    exdoor_ind = 0;
    combopos_state = 0;
    spawnip = 0;
    trigcopycat = 0;
    trigcooldown = 0;
    trig_lstate = trig_gstate = 0;
    trig_statetime = 0;
    trig_genscr = 0;
    trig_group = 0;
    trig_group_val = 0;
    trig_levelitems = 0;
    req_level_state = unreq_level_state = 0;
    trigdmlevel = -1;
    trigstatemap = trigstatescreen = 0;
    trigtint[0] = trigtint[1] = trigtint[2] = 0;
    triglvlpalette = trigbosspalette = -1;
    trigquaketime = trigwavytime = -1;
    trig_swjinxtime = trig_itmjinxtime = trig_shieldjinxtime = trig_stuntime = trig_bunnytime = -2;
    trig_pushtime = 8;
    prompt_cid = fail_prompt_cid = 0;
    prompt_cs = fail_prompt_cs = 0;
    prompt_x = 12;
    prompt_y = -8;
    trig_msgstr = fail_msgstr = 0;
    req_player_x = req_player_y = req_player_z = req_player_jump = 0;
    dest_player_x = dest_player_y = dest_player_z = player_bounce = 0;
    req_player_dir = 0;
    dest_player_dir = -1;
    force_ice_combo = -1;
    force_ice_vx = force_ice_vy = 0;
    trig_gravity = trig_terminal_v = 0;
    play_music = -2;
    set_music_refresh = -1;
    viewport_cond_range = 0;
    chance_numerator = chance_denominator = 1;
    sfx_pan = -1;
    sfx_volume = 100;
    sfx_frequency = -1;
    large_combo_copycat = 0;
}

Q30WeaponData::Q30WeaponData()
{
    flags = 0;
    moveflags = 0;
    wflags = 0;
    memset(burnsprs, 0, sizeof(burnsprs));
    memset(light_rads, 0, sizeof(light_rads));
    memset(light_offsets, 0, sizeof(light_offsets));
    glow_shape = 0;
    override_flags = 0;
    tilew = tileh = hxsz = hysz = hzsz = hxofs = hyofs = xofs = yofs = 0;
    step = 0;
    unblockable = 0;
    timeout = 0;
    imitate_weapon = default_defense = 0;
    lift_level = 0;
    lift_time = 16;
    lift_height = 80000;
    script = 0;
    memset(initd, 0, sizeof(initd));
    pierce_count = -1;
    bounce_mult = bounce_add = 0;
    viewport_suspend_range = viewport_despawn_range = 0;
}

Q30Combo::Q30Combo()
{
    type = 0;
    flag = 0;
    walk = 0xF0;
    memset(attributes, 0, sizeof(attributes));
    usrflags = 0;
    genflags = 0;
    script = 0;
    memset(initd, 0, sizeof(initd));
    only_gentrig = 0;
    liftcmb = liftundercmb = 0;
    liftcs = liftundercs = 0;
    liftdmg = liftlvl = liftitm = liftflags = liftgfx = 0;
    liftsprite = liftsfx = 0;
    liftbreaksprite = -1;
    liftbreaksfx = 0;
    lifthei = 8;
    lifttime = 16;
    lift_parent_item = 0;
    speed_mult = speed_div = 1;
    speed_add = 0;
    sfx_appear = sfx_disappear = sfx_loop = sfx_walking = sfx_standing = sfx_tap = sfx_landing = 0;
    sfx_falling = sfx_drowning = sfx_lava_drowning = 0;
    spr_appear = spr_disappear = spr_walking = spr_standing = spr_falling = spr_drowning = spr_lava_drowning = 0;
    z_height = z_step_height = 0;
    dive_under_level = 0;
    large_combo_dirs = 0;
    delay = 0;
    cs2_flash_duration = 0;
}

namespace
{

Q30Combo *g_combos[MAXCOMBOS];
byte g_rules[256];
bool g_rules_known = false;
bool g_scripts_pending = false;     // q30_clear_tables runs after the combo section is read

} // namespace

void q30_combos_clear()
{
    q30_cutscene_clear();
    q30_lift_reset();

for(int i = 0; i < MAXCOMBOS; ++i)
    {
        delete g_combos[i];
        g_combos[i] = NULL;
    }
}

void q30_combo_store(int id, Q30Combo *data)
{
    if(id < 0 || id >= MAXCOMBOS)
    {
        delete data;
        return;
    }

    delete g_combos[id];
    g_combos[id] = data;
    g_scripts_pending = true;
}

void q30_set_quest_rules(const byte *rules, size_t bytes)
{
    memset(g_rules, 0, sizeof(g_rules));
    memcpy(g_rules, rules, zc_min(bytes, sizeof(g_rules)));
    g_rules_known = true;
}

const Q30Combo *q30_combo(int id)
{
    if(quest_header_zelda_version < 0x255 || id < 0 || id >= MAXCOMBOS)
        return NULL;

    return g_combos[id];
}

int q30_combo_type(int id)
{
    const Q30Combo *c = q30_combo(id);
    return c ? c->type : combobuf[id].type;
}

int q30_combo_flag(int id)
{
    const Q30Combo *c = q30_combo(id);
    return c ? c->flag : combobuf[id].flag;
}

// A histogram of what the quest uses, for ZC_Q30_TRIGSTATS=1.
void q30_trigger_stats()
{
    if(!getenv("ZC_Q30_TRIGSTATS"))
        return;

    int types[256], flags[256], tf[Q30TF_MAX];
    int combos = 0, with_trig = 0, ntrig = 0, usrf = 0, attrs = 0;
    int f_level = 0, f_btn = 0, f_item = 0, f_timer = 0, f_sfx = 0, f_chg = 0, f_prox = 0, f_ctr = 0, f_lbeam = 0, f_cs = 0;
    int f_spitem = 0, f_spen = 0, f_ex = 0, f_copy = 0, f_cool = 0, f_lst = 0, f_gst = 0, f_genscr = 0, f_grp = 0;
    int f_exdoor = 0, f_litem = 0, f_dmlvl = 0, f_tint = 0, f_pal = 0, f_quake = 0, f_wavy = 0, f_jinx = 0, f_stun = 0;
    int f_bunny = 0, f_prompt = 0, f_msg = 0, f_fmsg = 0, f_pos = 0, f_ice = 0, f_grav = 0, f_music = 0, f_chance = 0;
    int f_reqst = 0, f_scrst = 0, f_cpos = 0, f_lcc = 0;
    int f_liftable = 0, f_speed = 0, f_sfxs = 0, f_sprs = 0, f_z = 0, f_scripts = 0;
    memset(types, 0, sizeof(types));
    memset(flags, 0, sizeof(flags));
    memset(tf, 0, sizeof(tf));

    for(int i = 0; i < MAXCOMBOS; ++i)
    {
        const Q30Combo *c = g_combos[i];

        if(!c)
            continue;

        ++combos;
        ++types[c->type];
        ++flags[c->flag];

        if(c->usrflags)
            ++usrf;

        for(int q = 0; q < 24; ++q)
            if(c->attributes[q])
            {
                ++attrs;
                break;
            }

        if(c->liftflags & 1) ++f_liftable;
        if(c->speed_mult != 1 || c->speed_div != 1 || c->speed_add) ++f_speed;
        if(c->sfx_appear || c->sfx_disappear || c->sfx_loop || c->sfx_walking || c->sfx_standing || c->sfx_tap || c->sfx_landing) ++f_sfxs;
        if(c->spr_appear || c->spr_disappear || c->spr_walking || c->spr_standing) ++f_sprs;
        if(c->z_height || c->z_step_height) ++f_z;
        if(q30_combo_script(i)) ++f_scripts;

        if(c->triggers.empty())
            continue;

        ++with_trig;

        for(size_t t = 0; t < c->triggers.size(); ++t)
        {
            const Q30Trigger &tr = c->triggers[t];
            ++ntrig;

            for(int b = 0; b < Q30TF_MAX; ++b)
                if(tr.flags.get(b))
                    ++tf[b];

            if(tr.triggerlevel) ++f_level;
            if(tr.triggerbtn) ++f_btn;
            if(tr.triggeritem) ++f_item;
            if(tr.trigtimer) ++f_timer;
            if(tr.trigsfx) ++f_sfx;
            if(tr.trigchange) ++f_chg;
            if(tr.trigprox) ++f_prox;
            if(tr.trigctramnt) ++f_ctr;
            if(tr.triglbeam) ++f_lbeam;
            if(tr.trigcschange) ++f_cs;
            if(tr.spawnitem) ++f_spitem;
            if(tr.spawnenemy) ++f_spen;
            if(tr.exstate > -1) ++f_ex;
            if(tr.trigcopycat) ++f_copy;
            if(tr.trigcooldown) ++f_cool;
            if(tr.flags.get(Q30TF_LEVELSTATE) || tr.flags.get(Q30TF_TRIGLEVELSTATE)) ++f_lst;
            if(tr.flags.get(Q30TF_GLOBALSTATE) || tr.flags.get(Q30TF_TRIGGLOBALSTATE)) ++f_gst;
            if(tr.trig_genscr) ++f_genscr;
            if(tr.trig_group) ++f_grp;
            if(tr.exdoor_dir > -1) ++f_exdoor;
            if(tr.trig_levelitems) ++f_litem;
            if(tr.trigdmlevel > -1) ++f_dmlvl;
            if(tr.trigtint[0] || tr.trigtint[1] || tr.trigtint[2]) ++f_tint;
            if(tr.triglvlpalette > -1 || tr.trigbosspalette > -1) ++f_pal;
            if(tr.trigquaketime > -1) ++f_quake;
            if(tr.trigwavytime > -1) ++f_wavy;
            if(tr.trig_swjinxtime > -2 || tr.trig_itmjinxtime > -2 || tr.trig_shieldjinxtime > -2) ++f_jinx;
            if(tr.trig_stuntime > -2) ++f_stun;
            if(tr.trig_bunnytime > -2) ++f_bunny;
            if(tr.prompt_cid || tr.fail_prompt_cid) ++f_prompt;
            if(tr.trig_msgstr) ++f_msg;
            if(tr.fail_msgstr) ++f_fmsg;
            if(tr.dest_player_x || tr.dest_player_y || tr.dest_player_z || tr.player_bounce || tr.dest_player_dir > -1
                || tr.req_player_x || tr.req_player_y || tr.req_player_z || tr.req_player_dir) ++f_pos;
            if(tr.force_ice_combo > -1) ++f_ice;
            if(tr.trig_gravity) ++f_grav;
            if(tr.play_music > -2) ++f_music;
            if(tr.chance_numerator != tr.chance_denominator) ++f_chance;
            if(tr.req_level_state || tr.unreq_level_state || !tr.req_global_state.empty() || !tr.unreq_global_state.empty()) ++f_reqst;
            if(!tr.req_screen_state.empty() || !tr.unreq_screen_state.empty() || !tr.req_screen_ex_state.empty() || !tr.unreq_screen_ex_state.empty()) ++f_scrst;
            if(tr.combopos_state) ++f_cpos;
            if(tr.large_combo_copycat) ++f_lcc;
        }
    }

    al_trace("q30 combos with extra data %d, with triggers %d, triggers %d, usrflags %d, attributes %d\n", combos, with_trig, ntrig, usrf, attrs);
    al_trace("q30 triggers: level %d btn %d item %d timer %d sfx %d change %d prox %d ctr %d lbeam %d cs %d spawnitem %d spawnenemy %d exstate %d copycat %d cooldown %d lstate %d gstate %d genscr %d group %d\n",
             f_level, f_btn, f_item, f_timer, f_sfx, f_chg, f_prox, f_ctr, f_lbeam, f_cs, f_spitem, f_spen, f_ex, f_copy, f_cool, f_lst, f_gst, f_genscr, f_grp);
    al_trace("q30 triggers: exdoor %d litem %d dmlvl %d tint %d palette %d quake %d wavy %d jinx %d stun %d bunny %d prompt %d msg %d failmsg %d pos %d ice %d gravity %d music %d chance %d reqstates %d scrstates %d cposstate %d largecopy %d\n",
             f_exdoor, f_litem, f_dmlvl, f_tint, f_pal, f_quake, f_wavy, f_jinx, f_stun, f_bunny, f_prompt, f_msg, f_fmsg, f_pos, f_ice, f_grav, f_music, f_chance, f_reqst, f_scrst, f_cpos, f_lcc);
    al_trace("q30 combos: liftable %d speed %d sfx %d sprites %d zheight %d scripts %d\n", f_liftable, f_speed, f_sfxs, f_sprs, f_z, f_scripts);

    for(int i = 0; i < 256; ++i)
        if(types[i])
            al_trace("q30 type %d: %d\n", i, types[i]);

    for(int i = 0; i < 256; ++i)
        if(flags[i])
            al_trace("q30 flag %d: %d\n", i, flags[i]);

    for(int b = 0; b < Q30TF_MAX; ++b)
        if(tf[b])
            al_trace("q30 trigflag %d: %d\n", b, tf[b]);
}

// ---- runtime ---------------------------------------------------------------------

namespace
{

inline long a_int(long v) { return v / 10000; }

enum { ctrigNONE = 0, ctrigIGNORE_SIGN = 1, ctrigSECRETS = 2, ctrigSWITCHSTATE = 4 };

// A combo position, on a layer or in an ffc.
struct H
{
    int layer;      // 0 = the screen, 1-6 = layers; -1 for an ffc
    int pos;        // 0..175
    int ffc;        // -1 for layer positions

    H(int l, int p) : layer(l), pos(p), ffc(-1) {}
    explicit H(int f) : layer(-1), pos(0), ffc(f) {}
    bool is_ffc() const { return ffc >= 0; }
};

// The screen a handle is on. NULL for a layer that doesn't exist.
mapscr *h_scr(const H &h)
{
    if(h.layer > 0)
    {
        mapscr *m = &tmpscr2[h.layer - 1];

        if(!m->valid || m->data.empty())
            return NULL;

        return m;
    }

    return &tmpscr[0];
}

int h_data(const H &h)
{
    if(h.is_ffc())
        return tmpscr[0].ffdata[h.ffc];

    mapscr *m = h_scr(h);
    return m ? m->data[h.pos] : 0;
}

int h_cset(const H &h)
{
    if(h.is_ffc())
        return tmpscr[0].ffcset[h.ffc];

    mapscr *m = h_scr(h);
    return m ? m->cset[h.pos] : 0;
}

int h_sflag(const H &h)
{
    if(h.is_ffc())
        return 0;

    mapscr *m = h_scr(h);
    return m ? m->sflag[h.pos] : 0;
}

void h_set_data(const H &h, int val)
{
    if(h.is_ffc())
    {
        tmpscr[0].ffdata[h.ffc] = (word)val;
        return;
    }

    mapscr *m = h_scr(h);

    if(m)
    {
        m->data[h.pos] = (word)val;
    }
}

void h_set_cset(const H &h, int val)
{
    if(h.is_ffc())
    {
        tmpscr[0].ffcset[h.ffc] = (byte)val;
        return;
    }

    mapscr *m = h_scr(h);

    if(m)
    {
        m->cset[h.pos] = (byte)val;
    }
}

void h_xy(const H &h, int &x, int &y)
{
    if(h.is_ffc())
    {
        x = (int)(tmpscr[0].ffx[h.ffc] / 10000);
        y = (int)(tmpscr[0].ffy[h.ffc] / 10000);
        return;
    }

    x = (h.pos & 15) * 16;
    y = (h.pos >> 4) * 16;
}

void h_size(const H &h, int &w, int &ht)
{
    if(h.is_ffc())
    {
        // ffwidth/ffheight hold tiles in the upper 2 bits, pixels in the low 6
        w = (tmpscr[0].ffwidth[h.ffc] & 63) + 1;
        ht = (tmpscr[0].ffheight[h.ffc] & 63) + 1;
        return;
    }

    w = ht = 16;
}

void h_center(const H &h, int &x, int &y)
{
    h_xy(h, x, y);
    int w, ht;
    h_size(h, w, ht);
    x += w / 2;
    y += ht / 2;
}

const Q30Combo *h_combo(const H &h)
{
    return q30_combo(h_data(h));
}

struct TrigInfo
{
    word clk;
    byte cooldown;
    TrigInfo() : clk(0), cooldown(0) {}
};

struct PosInfo
{
    int data;
    word type_clk;
    byte pushes[4];
    bool crumbling, appeared;
    word sfx_onchange, spr_onchange;
    std::vector<TrigInfo> trig;

    PosInfo() { clear(); }

    void clear()
    {
        data = -1;
        type_clk = 0;
        memset(pushes, 0, sizeof(pushes));
        crumbling = appeared = false;
        sfx_onchange = spr_onchange = 0;
        trig.clear();
    }

    TrigInfo &t(size_t idx)
    {
        if(idx >= trig.size())
            trig.resize(idx + 1);

        return trig[idx];
    }
};

const int POSLAYERS = 7;
std::vector<PosInfo> g_pos;
PosInfo g_ffcpos[MAXFFCS];

void pos_setup()
{
    if(g_pos.empty())
        g_pos.resize(POSLAYERS * 176);
}

PosInfo &h_info(const H &h)
{
    pos_setup();

    if(h.is_ffc())
        return g_ffcpos[h.ffc];

    return g_pos[h.layer * 176 + h.pos];
}

// per-position data that survives on the screen: exstates and friends
std::vector<dword> g_xstates;
std::vector<byte> g_xdoors;                 // [mi*4 + dir]: bits per door index
std::map<int, std::vector<byte> > g_posstates;

dword g_lvlswitches[MAXLEVELS];
long g_gswitch_timers[256];                 // 0 off, -1 on until toggled, >0 frames left

void states_setup()
{
    if(g_xstates.empty())
    {
        g_xstates.resize(MAXMAPS2 * MAPSCRSNORMAL, 0);
        g_xdoors.resize(MAXMAPS2 * MAPSCRSNORMAL * 4, 0);
    }
}

// Saved states live in one extra array at the end of game->globalRAM, so game saves
// and continues carry them with no change to the save format. Only 2.55+ quests ever
// create it. Layout: magic, id, then (kind, key, value) triples for what isn't zero.
// Countdown timers aren't stored: upstream zeroes them when a game is loaded.
const long Q30_STATE_MAGIC = 0x51333053;
bool g_states_dirty = false;
long g_state_id = 0;
long g_state_serial = 0;

inline void states_mark() { g_states_dirty = true; }

ZScriptArray *state_array()
{
    if(!game)
        return NULL;

    for(size_t i = game->globalRAM.size(); i > 0; --i)
    {
        ZScriptArray &a = game->globalRAM[i - 1];

        if(a.Size() >= 2 && a[0] == Q30_STATE_MAGIC)
            return &a;
    }

    return NULL;
}

void states_flush()
{
    if(!g_states_dirty || !game)
        return;

    g_states_dirty = false;
    states_setup();
    std::vector<long> out;

    for(size_t i = 0; i < g_xstates.size(); ++i)
        if(g_xstates[i])
        {
            out.push_back(0);
            out.push_back((long)i);
            out.push_back((long)g_xstates[i]);
        }

    for(size_t i = 0; i < g_xdoors.size(); ++i)
        if(g_xdoors[i])
        {
            out.push_back(1);
            out.push_back((long)i);
            out.push_back((long)g_xdoors[i]);
        }

    for(int i = 0; i < MAXLEVELS; ++i)
        if(g_lvlswitches[i])
        {
            out.push_back(2);
            out.push_back(i);
            out.push_back((long)g_lvlswitches[i]);
        }

    for(int i = 0; i < 256; ++i)
        if(g_gswitch_timers[i] == -1)
        {
            out.push_back(3);
            out.push_back(i);
            out.push_back(-1);
        }

    for(std::map<int, std::vector<byte> >::iterator it = g_posstates.begin(); it != g_posstates.end(); ++it)
        for(size_t k = 0; k < it->second.size(); ++k)
            if(it->second[k])
            {
                out.push_back(4);
                out.push_back(((long)it->first << 12) | (long)k);
                out.push_back(it->second[k]);
            }

    ZScriptArray *a = state_array();

    if(!a)
    {
        if(out.empty())
            return;

        game->globalRAM.resize(game->globalRAM.size() + 1);
        a = &game->globalRAM.back();
    }

    a->Resize(2 + out.size());
    g_state_id = ((long)time(NULL) << 6) ^ (++g_state_serial & 63);
    (*a)[0] = Q30_STATE_MAGIC;
    (*a)[1] = g_state_id;

    for(size_t i = 0; i < out.size(); ++i)
        (*a)[2 + i] = out[i];
}

// Tables follow the game data: a loaded save or a new game replaces them. The id tells
// whether game data still matches what's in memory, as it does across a continue.
void states_sync()
{
    ZScriptArray *a = state_array();
    long id = a ? (*a)[1] : 0;

    if(id == g_state_id)
        return;

    states_setup();
    g_xstates.assign(g_xstates.size(), 0);
    g_xdoors.assign(g_xdoors.size(), 0);
    g_posstates.clear();
    memset(g_lvlswitches, 0, sizeof(g_lvlswitches));
    memset(g_gswitch_timers, 0, sizeof(g_gswitch_timers));
    g_state_id = id;
    g_states_dirty = false;
    q30_cutscene_clear();
    q30_lift_reset();

    if(!a)
        return;

    for(dword i = 2; i + 2 < a->Size(); i += 3)
    {
        long kind = (*a)[i], key = (*a)[i + 1], val = (*a)[i + 2];

        switch(kind)
        {
        case 0:
            if(key >= 0 && (size_t)key < g_xstates.size()) g_xstates[key] = (dword)val;
            break;

        case 1:
            if(key >= 0 && (size_t)key < g_xdoors.size()) g_xdoors[key] = (byte)val;
            break;

        case 2:
            if(key >= 0 && key < MAXLEVELS) g_lvlswitches[key] = (dword)val;
            break;

        case 3:
            if(key >= 0 && key < 256) g_gswitch_timers[key] = -1;
            break;

        case 4:
        {
            std::vector<byte> &v = g_posstates[(int)(key >> 12)];

            if(v.empty())
                v.resize(7 * 176 + 1 + 32 + 1000, 0);

            if((size_t)(key & 4095) < v.size())
                v[key & 4095] = (byte)val;

            break;
        }
        }
    }
}

int cur_mi()
{
    return (currmap * MAPSCRSNORMAL) + homescr;
}

bool rule(int id)
{
    return g_rules_known && get_bit(g_rules, id) != 0;
}

// upstream's rule numbers, from qrs.h
enum
{
    qr30_OLD_FFC_FUNCTIONALITY = 400,
    qr30_BROKEN_COPYCAT_SELF_TRIGGER = 567,
    qr30_BROKEN_SELF_TRIGGERING_TRIGGERS = 569,
    qr30_AUTOCOMBO_ANY_LAYER = 346,
    qr30_AUTOCOMBO_LAYER_1 = 232,
    qr30_AUTOCOMBO_LAYER_2 = 233,
    qr30_CUSTOMCOMBOS_EVERY_LAYER = 340,
    qr30_CUSTOMCOMBOSLAYERS1AND2 = 197,
    qr30_DUNGEON_DMAPS_PERM_SECRETS = 204
};

bool can_perm_secret()
{
    return !isdungeon() || rule(qr30_DUNGEON_DMAPS_PERM_SECRETS);
}

bool do_trigger(const H &h, size_t idx, int special = 0, weapon *w = NULL);
void toggle_switches(dword flags, bool entry);
void toggle_gswitch(int state, bool entry);

typedef bool (*TrigCond)(const Q30Trigger &t, size_t idx, void *ctx);

bool execute(const H &h, TrigCond cond, void *ctx, int special = 0, weapon *w = NULL)
{
    const Q30Combo *c = h_combo(h);

    if(!c || c->triggers.empty())
        return false;

    int cid = h_data(h);
    bool ret = false;

    for(size_t idx = 0; idx < c->triggers.size(); ++idx)
    {
        const Q30Trigger &t = c->triggers[idx];

        if(!cond(t, idx, ctx))
            continue;

        ret = true;
        bool success = do_trigger(h, idx, special, w);
        (void)success;

        if(success && t.flags.get(Q30TF_CANCEL_TRIGGER))
            break;

        if(h_data(h) != cid)
            break;
    }

    return ret;
}

bool cond_flag(const Q30Trigger &t, size_t, void *ctx)
{
    return t.flags.get((size_t)(intptr_t)ctx);
}

bool cond_flag2(const Q30Trigger &t, size_t, void *ctx)
{
    const int *f = (const int *)ctx;
    return t.flags.get(f[0]) || t.flags.get(f[1]);
}

void for_all_positions(void (*fn)(const H &, void *), void *ctx, bool include_ffc = true)
{
    for(int lyr = 0; lyr < POSLAYERS; ++lyr)
    {
        if(lyr > 0 && (tmpscr2[lyr - 1].data.empty() || !tmpscr2[lyr - 1].valid))
            continue;

        for(int pos = 0; pos < 176; ++pos)
            fn(H(lyr, pos), ctx);
    }

    if(include_ffc)
    {
        for(int i = 0; i < MAXFFCS; ++i)
        {
            if(tmpscr[0].ffdata[i] && !(tmpscr[0].ffflags[i] & ffCHANGER))
                fn(H(i), ctx);
        }
    }
}

// is this combo position's ffc at the given point?
bool ffc_at(int i, int x, int y)
{
    int fx = (int)(tmpscr[0].ffx[i] / 10000);
    int fy = (int)(tmpscr[0].ffy[i] / 10000);
    return !(x < fx || x > fx + (tmpscr[0].ffwidth[i] & 63) || y < fy || y > fy + (tmpscr[0].ffheight[i] & 63))
        && !(tmpscr[0].ffflags[i] & (ffCHANGER | ffETHEREAL));
}

// ---- effects --------------------------------------------------------------------

int g_copycat_id = 0;
int g_copycat_skip_lyr = -1, g_copycat_skip_pos = -1, g_copycat_skip_ffc = -1;

bool cond_copycat(const Q30Trigger &t, size_t, void *)
{
    return t.trigcopycat == g_copycat_id && !t.flags.get(Q30TF_NO_COPYCAT_CAUSE);
}

void copycat_visit(const H &h, void *)
{
    if(h.is_ffc())
    {
        if(h.ffc == g_copycat_skip_ffc)
            return;
    }
    else if(h.layer == g_copycat_skip_lyr && h.pos == g_copycat_skip_pos)
        return;

    execute(h, cond_copycat, NULL);
}

void trig_copycat(byte copyid)
{
    if(g_copycat_id || !copyid)
        return;

    g_copycat_id = copyid;
    for_all_positions(copycat_visit, NULL, !rule(qr30_OLD_FFC_FUNCTIONALITY));
    g_copycat_id = 0;
}

void trig_copycat_from(int cid, const H &h, byte trigcopyid)
{
    if(g_copycat_id)
        return;

    bool skip_self = !rule(qr30_BROKEN_COPYCAT_SELF_TRIGGER) || h_data(h) == cid;

    if(h.is_ffc())
    {
        if(skip_self)
            g_copycat_skip_ffc = h.ffc;

        trig_copycat(trigcopyid);
        g_copycat_skip_ffc = -1;
    }
    else
    {
        if(skip_self)
        {
            g_copycat_skip_lyr = h.layer;
            g_copycat_skip_pos = h.pos;
        }

        trig_copycat(trigcopyid);
        g_copycat_skip_lyr = -1;
        g_copycat_skip_pos = -1;
    }
}

// the positions forming a "large combo", linked through large_combo_dirs
void large_combo_handles(const H &base, std::vector<H> &out)
{
    out.clear();
    out.push_back(base);

    if(base.is_ffc())
        return;

    static const int xo[4] = {0, 0, -1, 1};
    static const int yo[4] = {-1, 1, 0, 0};
    static const int opp[4] = {1, 0, 3, 2};
    std::vector<int> visited(1, base.pos);

    for(size_t n = 0; n < out.size(); ++n)
    {
        H cur = out[n];
        const Q30Combo *c = h_combo(cur);

        if(!c || !c->large_combo_dirs)
            continue;

        int cx = cur.pos & 15, cy = cur.pos >> 4;

        for(int dir = 0; dir < 4; ++dir)
        {
            if(!(c->large_combo_dirs & (1 << dir)))
                continue;

            int nx = cx + xo[dir], ny = cy + yo[dir];

            if(nx < 0 || nx > 15 || ny < 0 || ny > 10)
                continue;

            int npos = ny * 16 + nx;

            if(std::find(visited.begin(), visited.end(), npos) != visited.end())
                continue;

            H nh(cur.layer, npos);
            const Q30Combo *nc = h_combo(nh);

            if(!nc || !(nc->large_combo_dirs & (1 << opp[dir])))
                continue;

            visited.push_back(npos);
            out.push_back(nh);
        }
    }

    // top-left first: its settings are the ones read
    size_t lowest = 0;

    for(size_t q = 1; q < out.size(); ++q)
        if(out[q].pos < out[lowest].pos)
            lowest = q;

    std::swap(out[0], out[lowest]);
}

std::map<int, std::vector<byte> > g_large_active;

bool cond_large(const Q30Trigger &t, size_t, void *ctx)
{
    return t.large_combo_copycat == *(byte *)ctx && t.flags.get(Q30TF_TRIGGERED_BY_LARGE_COMBO_COPYCAT);
}

bool handle_large_copycat(const H &h, const Q30Trigger &t, weapon *w)
{
    if(h.is_ffc() || !t.flags.get(Q30TF_TRIGGER_LARGE_COMBO_COPYCAT))
        return false;

    std::vector<H> handles;
    large_combo_handles(h, handles);

    if(handles.empty())
        return false;

    byte id = t.large_combo_copycat;
    int key = handles[0].layer * 176 + handles[0].pos;
    std::vector<byte> &active = g_large_active[key];

    if(std::find(active.begin(), active.end(), id) != active.end())
        return false;

    active.push_back(id);

    for(size_t q = 0; q < handles.size(); ++q)
        execute(handles[q], cond_large, &id, 0, w);

    g_large_active[key].erase(std::find(g_large_active[key].begin(), g_large_active[key].end(), id));
    return true;
}

void change_combo(const H &h, int new_cid)
{
    if(new_cid < 0)
        new_cid = 0;
    else if(new_cid >= MAXCOMBOS)
        new_cid = MAXCOMBOS - 1;

    h_set_data(h, new_cid);
}

int cur_dlevel()
{
    return dlevel < MAXLEVELS ? (dlevel > 0 ? dlevel : 0) : 0;
}

int trig_counter(const Q30Trigger &t)
{
    long cost = t.trigctramnt;

    if(!cost)
        return 0;

    if(t.flags.get(Q30TF_COUNTERDISCOUNT))
    {
        int wmedal = current_item_id(itype_wealthmedal);

        if(wmedal >= 0)
        {
            if(itemsbuf[wmedal].flags & ITEM_FLAG1)
                cost = (cost * itemsbuf[wmedal].misc1) / 100;
            else
                cost += itemsbuf[wmedal].misc1;

            if(t.trigctramnt > 0 && cost < 0)
                cost = 1;
        }
    }

    return (int)cost;
}

int ctr_value(int ctr, bool *inf)
{
    *inf = false;

    if(ctr < 0 || ctr >= 32)
        return 0;

    if(ctr == 2 && current_item_power(itype_bombbag))
        *inf = true;
    else if(ctr == 3 && current_item_power(itype_quiver))
        *inf = true;
    else if(ctr == 1 && current_item_power(itype_wallet))
        *inf = true;

    return game->get_counter((byte)ctr);
}

int ctr_max(int ctr)
{
    return (ctr < 0 || ctr >= 32) ? 0 : game->get_maxcounter((byte)ctr);
}

void ctr_modify(int ctr, int amount, bool gradual)
{
    if(ctr < 0 || ctr >= 32)
        return;

    if(gradual)
        game->change_dcounter((short)amount, (byte)ctr);
    else
        game->change_counter((short)amount, (byte)ctr);
}

bool g_generic_secrets = false, g_generic_switchstate = false;

bool item_exists_cheap(int item_id)
{
    return item_id >= 0 && item_id < MAXITEMS;
}

// Cooldown and other per-position state of an extra weapon, by the weapon's uid.
std::map<long, std::vector<int> > g_wpn_done;       // positions already checked this weapon

bool wpn_done(weapon *w, int key)
{
    std::vector<int> &v = g_wpn_done[w->getUID()];
    return std::find(v.begin(), v.end(), key) != v.end();
}

void wpn_mark(weapon *w, int key)
{
    g_wpn_done[w->getUID()].push_back(key);
}

std::map<long, int> g_wpn_burn;             // fire levels triggers gave a weapon, by uid
bool g_respawn_forced = false;
int g_stun_clk = 0;
int g_pending_warp_type = -1, g_pending_warp_index = 0;
int g_pending_clear = 0;

void do_weapon_fx(weapon *w, const Q30Trigger &t)
{
    if(!w)
        return;

    if(t.flags.get(Q30TF_KILLWPN))
    {
        switch(w->id)
        {
        case wSword:
        case wHammer:
            break;

        default:
            if(w->dead == 0)
                w->dead = 1;
        }
    }

    int levels = (t.flags.get(Q30TF_IGNITE_ANYFIRE) ? 1 : 0) | (t.flags.get(Q30TF_IGNITE_STRONGFIRE) ? 2 : 0)
                 | (t.flags.get(Q30TF_IGNITE_MAGICFIRE) ? 4 : 0) | (t.flags.get(Q30TF_IGNITE_DIVINEFIRE) ? 8 : 0);

    if(levels)
    {
        if(t.flags.get(Q30TF_UNIGNITE_WEAPONS))
            g_wpn_burn[w->getUID()] &= ~levels;
        else
            g_wpn_burn[w->getUID()] |= levels;
    }
}

// Upstream's trigger_string
void play_string(int str)
{
    switch(str)
    {
    case -1:
        str = tmpscr[0].str;
        break;

    case -2:
        str = tmpscr[0].catchall;
        break;

    default:
        if(str <= -10 && str >= -17)
        {
            int di = (currdmap << 7) + homescr - (DMaps[currdmap].type == dmOVERW ? 0 : DMaps[currdmap].xoff);
            str = game->screen_d[di][-(str + 10)] / 10000L;
        }
    }

    if(str > 0 && str < msg_count)
        donewmsg(str);
}

void trigger_string(const Q30Trigger &t, bool success)
{
    long v = (success ? t.trig_msgstr : t.fail_msgstr);

    if(v)
        play_string((int)(v / 10000));
}

bool has_item_for(int item_id)
{
    return item_id >= 0 && item_id < MAXITEMS && game->item[item_id] && !item_disabled(item_id);
}

int q30_abs(int v) { return v < 0 ? -v : v; }

bool conditions(const H &h, size_t idx, bool &hasitem, bool side_effects)
{
    const Q30Combo *c = h_combo(h);

    if(!c || idx >= c->triggers.size())
        return false;

    const Q30Trigger &t = c->triggers[idx];

    if(t.flags.get(Q30TF_MAPVIEW_ONLY))
        return false;

    int hero_x = int(Link.getX()), hero_y = int(Link.getY());
    int center_x, center_y;
    h_center(h, center_x, center_y);

    // viewport conditions: a combo is on screen unless its position is out of the 256x176 area
    {
        bool on_screen = t.flags.get(Q30TF_VIEWPORT_REQ_ONSCREEN);
        bool off_screen = t.flags.get(Q30TF_VIEWPORT_REQ_OFFSCREEN);

        if(on_screen && off_screen)
            return false;

        if(on_screen || off_screen)
        {
            int cx, cy, w, ht;
            h_xy(h, cx, cy);
            h_size(h, w, ht);
            int r = t.viewport_cond_range;
            bool inside = !(cx + w <= -r || cx >= 256 + r || cy + ht <= -r || cy >= 176 + r);

            if(on_screen != inside)
                return false;
        }
    }

    if(t.triggeritem)
    {
        hasitem = has_item_for(t.triggeritem);

        if(t.flags.get(Q30TF_INVERTITEM))
        {
            if(hasitem)
                return false;
        }
        else if(!hasitem)
            return false;
    }

    if(t.trigprox)
    {
        int hx = hero_x + 8;
        int hy = hero_y + ((t.flags.get(Q30TF_PROX_USE_SOL_HITBOX) && !get_bit(quest_rules, qr_LTTPCOLLISION)) ? 12 : 8);
        double d;

        if(t.flags.get(Q30TF_PROX_USE_SQUARE))
            d = zc_max(q30_abs(hx - center_x), q30_abs(hy - center_y));
        else
            d = sqrt((double)(hx - center_x) * (hx - center_x) + (double)(hy - center_y) * (hy - center_y));

        if(t.flags.get(Q30TF_INVERTPROX))
        {
            if(d < t.trigprox)
                return false;
        }
        else if(d >= t.trigprox)
            return false;
    }

    if(t.flags.get(Q30TF_REQ_X_LE) || t.flags.get(Q30TF_REQ_X_GE) || t.flags.get(Q30TF_REQ_Y_LE) || t.flags.get(Q30TF_REQ_Y_GE)
        || t.flags.get(Q30TF_REQ_JUMP_LE) || t.flags.get(Q30TF_REQ_JUMP_GE))
    {
        int targ_x = a_int(t.req_player_x), targ_y = a_int(t.req_player_y);

        if(t.flags.get(Q30TF_REQ_X_REL))
            targ_x += center_x - 8;

        if(t.flags.get(Q30TF_REQ_Y_REL))
            targ_y += center_y - 8;

        if(t.flags.get(Q30TF_REQ_X_LE) && hero_x > targ_x)
            return false;

        if(t.flags.get(Q30TF_REQ_X_GE) && hero_x < targ_x)
            return false;

        if(t.flags.get(Q30TF_REQ_Y_LE) && hero_y > targ_y)
            return false;

        if(t.flags.get(Q30TF_REQ_Y_GE) && hero_y < targ_y)
            return false;

        long jump = -(long)int(Link.getFall()) * 100;   // fall is 1/100 pixel per frame, negative is up; upstream compares it to a zfix

        if(t.flags.get(Q30TF_REQ_JUMP_LE) && jump > t.req_player_jump)
            return false;

        if(t.flags.get(Q30TF_REQ_JUMP_GE) && jump < t.req_player_jump)
            return false;
    }

    long z_pix = (long)int(Link.getZ()) * 10000;

    if(t.flags.get(Q30TF_INVERT_PLAYER_Z))
    {
        if(z_pix >= t.req_player_z)
            return false;
    }
    else if(z_pix < t.req_player_z)
        return false;

    if(t.req_player_dir && !(t.req_player_dir & (1 << Link.getDir())))
        return false;

    if(t.flags.get(Q30TF_PLAYER_STANDING) && int(Link.getZ()) > 0)
        return false;

    if(t.flags.get(Q30TF_PLAYER_NOTSTANDING) && int(Link.getZ()) <= 0)
        return false;

    if(t.flags.get(Q30TF_COND_DARK) || t.flags.get(Q30TF_COND_NODARK))
    {
        bool lit = !darkroom;

        if(t.flags.get(Q30TF_COND_DARK) && lit)
            return false;

        if(t.flags.get(Q30TF_COND_NODARK) && !lit)
            return false;
    }

    states_setup();
    int mi = cur_mi();

    if(t.trigstatemap > 0)
    {
        if(t.trigstatemap > map_count)
            return false;

        mi = (t.trigstatemap - 1) * MAPSCRSNORMAL + t.trigstatescreen;
    }

    if(!t.req_screen_state.empty() || !t.unreq_screen_state.empty())
    {
        for(int q = 0; q < 16; ++q)
        {
            bool has = (game->maps[mi] & (1 << q)) != 0;

            if(t.req_screen_state.get(q) && !has)
                return false;

            if(t.unreq_screen_state.get(q) && has)
                return false;
        }
    }

    if(!t.req_screen_ex_state.empty() || !t.unreq_screen_ex_state.empty())
    {
        for(int q = 0; q < 32; ++q)
        {
            bool has = (g_xstates[mi] & (1u << q)) != 0;

            if(t.req_screen_ex_state.get(q) && !has)
                return false;

            if(t.unreq_screen_ex_state.get(q) && has)
                return false;
        }
    }

    for(int dir = 0; dir < 4; ++dir)
    {
        if(t.req_screen_ex_door[dir].empty() && t.unreq_screen_ex_door[dir].empty())
            continue;

        for(int q = 0; q < 8; ++q)
        {
            bool has = (g_xdoors[mi * 4 + dir] & (1 << q)) != 0;

            if(t.req_screen_ex_door[dir].get(q) && !has)
                return false;

            if(t.unreq_screen_ex_door[dir].get(q) && has)
                return false;
        }
    }

    {
        bool req = t.flags.get(Q30TF_COMBOPOSSTATE_REQ);
        bool unreq = t.flags.get(Q30TF_COMBOPOSSTATE_UNREQ);

        if(req && unreq)
            return false;

        if(req || unreq)
        {
            int key = h.is_ffc() ? 1000 + h.ffc : h.layer * 176 + h.pos;
            byte state = q30_pos_state(cur_mi(), key);

            if(req != ((state & (1 << t.combopos_state)) != 0))
                return false;
        }
    }

    {
        int lvl = t.trigdmlevel > -1 ? t.trigdmlevel : cur_dlevel();

        if(t.flags.get(Q30TF_LITEM_COND) && (t.trig_levelitems & game->lvlitems[lvl]) != t.trig_levelitems)
            return false;

        if(t.flags.get(Q30TF_LITEM_REVCOND) && (t.trig_levelitems & game->lvlitems[lvl]) == t.trig_levelitems)
            return false;

        if((t.req_level_state & g_lvlswitches[lvl]) != t.req_level_state)
            return false;

        if(t.unreq_level_state & g_lvlswitches[lvl])
            return false;

        for(int q = 0; q < 256; ++q)
        {
            if(t.req_global_state.get(q) && !g_gswitch_timers[q])
                return false;

            if(t.unreq_global_state.get(q) && g_gswitch_timers[q])
                return false;
        }
    }

    if(t.chance_numerator < t.chance_denominator || !t.chance_numerator)
    {
        if(t.chance_numerator <= 0 || t.chance_denominator <= 0)
            return false;

        if((rand() % t.chance_denominator) >= t.chance_numerator)
            return false;
    }

    // everything checking for side effects goes here
    int ctrcost = trig_counter(t);

    if(ctrcost)
    {
        bool inf = false;
        bool perc = t.flags.get(Q30TF_COUNTER_PERCENT);
        bool partial = t.flags.get(Q30TF_COUNTER_PARTIAL_CONSUME);
        int ctrval = ctr_value(t.trigctr, &inf);
        int ctrmax = ctr_max(t.trigctr);

        if(perc)
            ctrcost = (int)((int64_t)ctrcost * ctrmax / 100);

        if(side_effects && t.flags.get(Q30TF_CTRNONLYTRIG) && t.flags.get(Q30TF_COUNTEREAT))
        {
            if(!inf && (partial || ctrval >= ctrcost))
                ctr_modify(t.trigctr, -ctrcost, t.flags.get(Q30TF_COUNTER_GRADUAL));
        }

        if(t.flags.get(Q30TF_COUNTERGE) && ctrval < ctrcost && !inf)
            return false;

        if(t.flags.get(Q30TF_COUNTERLT) && (ctrval >= ctrcost || inf))
            return false;
    }

    return true;
}

// force-trigger handlers for exstates etc.

void do_ex_trigger(const H &h, size_t idx);

bool force_ex_trigger(const H &h, size_t idx, int xstate = -1)
{
    const Q30Combo *c = h_combo(h);

    if(!c || idx >= c->triggers.size())
        return false;

    const Q30Trigger &t = c->triggers[idx];

    if(t.flags.get(Q30TF_UNSETEXSTATE))
        return false;

    if(t.exstate > -1 && (xstate < 0 || xstate == t.exstate))
    {
        states_setup();

        if(xstate >= 0 || (g_xstates[cur_mi()] & (1u << t.exstate)))
        {
            do_ex_trigger(h, idx);
            return true;
        }
    }

    return false;
}

bool force_ex_door_trigger(const H &h, size_t idx, int dir = -1, int ind = 0)
{
    const Q30Combo *c = h_combo(h);

    if(!c || idx >= c->triggers.size() || dir > 3 || ind > 7)
        return false;

    const Q30Trigger &t = c->triggers[idx];

    if(t.flags.get(Q30TF_UNSETEXDOOR))
        return false;

    if(t.exdoor_dir > -1 && (dir < 0 || (dir == t.exdoor_dir && ind == t.exdoor_ind)))
    {
        states_setup();

        if(dir >= 0 || (g_xdoors[cur_mi() * 4 + t.exdoor_dir] & (1 << t.exdoor_ind)))
        {
            do_ex_trigger(h, idx);
            return true;
        }
    }

    return false;
}

bool force_combopos_trigger(const H &h, size_t idx)
{
    const Q30Combo *c = h_combo(h);

    if(!c || idx >= c->triggers.size())
        return false;

    const Q30Trigger &t = c->triggers[idx];

    if(!t.flags.get(Q30TF_COMBOPOSSTATE_CAUSE))
        return false;

    int key = h.is_ffc() ? 1000 + h.ffc : h.layer * 176 + h.pos;

    if(q30_pos_state(cur_mi(), key) & (1 << t.combopos_state))
    {
        do_ex_trigger(h, idx);
        return true;
    }

    return false;
}

void do_ex_trigger(const H &h, size_t idx)
{
    int cid = h_data(h);
    int ocs = h_cset(h);
    const Q30Combo *c = q30_combo(cid);

    if(!c || idx >= c->triggers.size())
        return;

    const Q30Trigger &t = c->triggers[idx];
    handle_large_copycat(h, t, NULL);

    if(t.trigcschange || t.flags.get(Q30TF_CSET_CHANGE_ABSOLUTE))
    {
        if(t.flags.get(Q30TF_CSET_CHANGE_ABSOLUTE))
            h_set_cset(h, t.trigcschange & 15);
        else
            h_set_cset(h, (ocs + t.trigcschange) & 15);
    }

    if(t.trigchange || t.flags.get(Q30TF_CMB_CHANGE_ABSOLUTE))
    {
        if(t.flags.get(Q30TF_CMB_CHANGE_ABSOLUTE))
            change_combo(h, t.trigchange);
        else
            change_combo(h, cid + t.trigchange);
    }

    if(t.flags.get(Q30TF_RESETANIM))
    {
        int cid2 = h_data(h);
        reset_combo_animation(cid2);
        reset_combo_animation2(cid2);
    }

    if(t.trigcopycat)
        trig_copycat_from(cid, h, t.trigcopycat);
}

void trigger_type_effects(const H &h, const Q30Combo *c, const Q30Trigger &t, size_t idx,
                          weapon *w, int special, int cid, bool &used_bit);

void results(const H &h, const Q30Trigger &t, size_t idx, bool &hasitem, bool &used_bit, int special, PosInfo &timer,
             int cid, int ocs, weapon *w)
{
    int combo_x, combo_y, center_x, center_y;
    h_xy(h, combo_x, combo_y);
    h_center(h, center_x, center_y);

    if(t.flags.get(Q30TF_TOGGLEDARK))
    {
        // same effect as lit/dark toggling
        naturaldark = !naturaldark;
        lighting(false, false);
    }

    if(t.flags.get(Q30TF_SECRETS))
    {
        used_bit = true;

        if(!(special & ctrigSECRETS) && !g_generic_secrets)
        {
            g_generic_secrets = true;
            hidden_entrance(0, true, false);
            g_generic_secrets = false;

            if(tmpscr[0].secretsfx)
                sfx(tmpscr[0].secretsfx);
        }

        if(can_perm_secret() && !(tmpscr[0].flags5 & fTEMPSECRETS))
            setmapflag(mSECRET);
    }

    if(t.flags.get(Q30TF_LEVELSTATE))
    {
        used_bit = true;

        if(!(special & ctrigSWITCHSTATE) && !g_generic_switchstate)
        {
            g_generic_switchstate = true;
            (states_mark(), g_lvlswitches[cur_dlevel()]) ^= (1u << t.trig_lstate);
            toggle_switches(1u << t.trig_lstate, false);
            g_generic_switchstate = false;
        }
    }

    if(t.flags.get(Q30TF_GLOBALSTATE))
    {
        used_bit = true;

        if(!(special & ctrigSWITCHSTATE) && !g_generic_switchstate)
        {
            long tmr = t.trig_statetime;
            int pair = t.trig_gstate;
            bool oldstate = g_gswitch_timers[pair] != 0;

            if(tmr > 0)
                (states_mark(), g_gswitch_timers[pair]) = tmr;
            else if(g_gswitch_timers[pair])
                (states_mark(), g_gswitch_timers[pair]) = 0;
            else
                (states_mark(), g_gswitch_timers[pair]) = -1;

            if(oldstate != (g_gswitch_timers[pair] != 0))
            {
                g_generic_switchstate = true;
                toggle_gswitch(pair, false);
                g_generic_switchstate = false;
            }
        }
    }

    if(t.trigsfx)
    {
        if(t.flags.get(Q30TF_SFX_KILL))
            stop_sfx(t.trigsfx);
        else
            sfx(t.trigsfx, t.sfx_pan < 0 ? pan(combo_x) : t.sfx_pan, t.flags.get(Q30TF_SFX_LOOP), !t.flags.get(Q30TF_SFX_NO_RESTART));
    }

    // A weapon's trigger runs inside Lwpns/Ewpns.animate: clearing a list there would
    // delete the running weapon, so these wait for the start of the next update.
    if(t.flags.get(Q30TF_KILLENEMIES))
        g_pending_clear |= 1;

    if(t.flags.get(Q30TF_CLEARENEMIES))
        g_pending_clear |= 2;

    if(t.flags.get(Q30TF_CLEARLWEAPONS))
        g_pending_clear |= 4;

    if(t.flags.get(Q30TF_CLEAREWEAPONS))
        g_pending_clear |= 8;

    if(t.triggeritem && hasitem && t.flags.get(Q30TF_CONSUMEITEM))
        takeitem(t.triggeritem);

    if(!t.flags.get(Q30TF_CTRNONLYTRIG) && t.flags.get(Q30TF_COUNTEREAT))
    {
        int ctrcost = trig_counter(t);

        if(ctrcost)
        {
            bool inf = false;
            int ctrval = ctr_value(t.trigctr, &inf);
            int ctrmax = ctr_max(t.trigctr);

            if(t.flags.get(Q30TF_COUNTER_PERCENT))
                ctrcost = (int)((int64_t)ctrcost * ctrmax / 100);

            if(!inf && (t.flags.get(Q30TF_COUNTER_PARTIAL_CONSUME) || ctrval >= ctrcost))
                ctr_modify(t.trigctr, -ctrcost, t.flags.get(Q30TF_COUNTER_GRADUAL));
        }
    }

    bool trigexstate = true;

    if(t.spawnenemy)
    {
        bool enm_ex = t.flags.get(Q30TF_EXSTENEMY);
        int numcreated = addenemy(combo_x, combo_y, t.spawnenemy, -10);
        (void)numcreated;

        if(enm_ex)
        {
            trigexstate = false;

            if(guys.Count() > 0)
                q30_note_enemy_exstate(((sprite *)guys.spr(guys.Count() - 1))->getUID(), t.exstate);
        }
    }

    if(t.spawnitem)
    {
        bool itm_ex = t.flags.get(Q30TF_EXSTITEM);
        bool specitem = t.flags.get(Q30TF_SPCITEM);

        if(specitem && getmapflag(mBELOW))
        {
            if(itm_ex)
                trigexstate = true;
        }
        else
        {
            int pflags = t.spawnip & (ipHOLDUP | ipTIMER );

            if(specitem)
                pflags |= ipONETIME2;

            int item_id = t.spawnitem;

            if(item_id < 0)
                item_id = select_dropitem(-item_id, combo_x, combo_y);

            item *itm = NULL;

            if(item_exists_cheap(item_id))
            {
                itm = new item((fix)combo_x, (fix)combo_y, (fix)0, item_id, pflags, 0);
                items.add(itm);
            }

            if(itm_ex)
            {
                trigexstate = false;

                if(itm)
                    q30_note_item_exstate(itm->getUID(), t.exstate);
            }

            if(t.flags.get(Q30TF_AUTOGRABITEM) && itm)
            {
                Link.checkitems(items.Count() - 1);     // pick up at once
            }
        }
    }

    // level items
    {
        int lvl = t.trigdmlevel > -1 ? t.trigdmlevel : cur_dlevel();
        bool set_it = t.flags.get(Q30TF_LITEM_SET), unset_it = t.flags.get(Q30TF_LITEM_UNSET);

        if(set_it && unset_it)
            game->lvlitems[lvl] ^= (byte)t.trig_levelitems;
        else if(set_it)
            game->lvlitems[lvl] |= (byte)t.trig_levelitems;
        else if(unset_it)
            game->lvlitems[lvl] &= (byte)~t.trig_levelitems;
    }

    // graphics
    if(t.flags.get(Q30TF_TINT_CLEAR))
        q30_tint_clear();

    if(t.trigtint[0] || t.trigtint[1] || t.trigtint[2])
        q30_tint(t.trigtint[0], t.trigtint[1], t.trigtint[2]);

    if(t.triglvlpalette > -1)
        loadlvlpal(t.triglvlpalette);
    else if(t.triglvlpalette == -2)
        loadlvlpal(tmpscr[0].color);

    if(t.trigbosspalette > -1)
        loadpalset(csBOSS, pSprite(t.trigbosspalette));

    if(t.trigquaketime > -1)
        quakeclk = t.trigquaketime;

    if(t.trigwavytime > -1)
        wavy = t.trigwavytime;

    // status effects
    if(t.trig_swjinxtime > -2)
        Link.setSwordClk(t.trig_swjinxtime);

    if(t.trig_itmjinxtime > -2)
        Link.setItemClk(t.trig_itmjinxtime);

    if(t.trig_stuntime > 0)
        g_stun_clk = t.trig_stuntime;
    else if(t.trig_stuntime == 0)
        g_stun_clk = 0;

    // shield jinx and bunny have no state in 2.53's Link

    if(t.play_music > -2)
    {
        if(t.play_music == -1)
            playLevelMusic();
        else
            q30_play_quest_music(t.play_music);
    }

    if(t.exstate > -1)
    {
        states_setup();

        if(t.flags.get(Q30TF_UNSETEXSTATE))
            (states_mark(), g_xstates[cur_mi()]) &= ~(1u << t.exstate);
        else if(trigexstate)
            (states_mark(), g_xstates[cur_mi()]) |= (1u << t.exstate);
    }

    if(t.exdoor_dir > -1)
    {
        states_setup();

        if(t.flags.get(Q30TF_UNSETEXDOOR))
            (states_mark(), g_xdoors[cur_mi() * 4 + t.exdoor_dir]) &= (byte)~(1 << t.exdoor_ind);
        else
            (states_mark(), g_xdoors[cur_mi() * 4 + t.exdoor_dir]) |= (byte)(1 << t.exdoor_ind);
    }

    {
        bool do_set = t.flags.get(Q30TF_COMBOPOSSTATE_SET), do_unset = t.flags.get(Q30TF_COMBOPOSSTATE_UNSET);

        if(do_set || do_unset)
        {
            int key = h.is_ffc() ? 1000 + h.ffc : h.layer * 176 + h.pos;
            byte &state = q30_pos_state(cur_mi(), key);
            states_mark();
byte flag = (byte)(1 << t.combopos_state);

            if(do_set && do_unset)
                state ^= flag;
            else if(do_set)
                state |= flag;
            else
                state &= (byte)~flag;
        }
    }

    if(t.flags.get(Q30TF_RESPAWN_HERE))
    {
        g_respawn_forced = false;
        Link.setEntryPoints(center_x - 8, center_y - 8);
        g_respawn_forced = true;
    }

    if(t.flags.get(Q30TF_RESET_RESPAWN))
        g_respawn_forced = false;

    if(t.flags.get(Q30TF_REVERT_GRAVITY))
        q30_revert_gravity();
    else if(t.flags.get(Q30TF_SET_GRAVITY))
        q30_set_gravity(t.trig_gravity, t.trig_terminal_v);

    if(handle_large_copycat(h, t, w))
        used_bit = true;

    if(t.trigcschange || t.flags.get(Q30TF_CSET_CHANGE_ABSOLUTE))
    {
        used_bit = true;

        if(t.flags.get(Q30TF_CSET_CHANGE_ABSOLUTE))
            h_set_cset(h, t.trigcschange & 15);
        else
            h_set_cset(h, (ocs + t.trigcschange) & 15);
    }

    if(t.trigchange || t.flags.get(Q30TF_CMB_CHANGE_ABSOLUTE))
    {
        used_bit = true;

        if(t.flags.get(Q30TF_CMB_CHANGE_ABSOLUTE))
            change_combo(h, t.trigchange);
        else
            change_combo(h, cid + t.trigchange);
    }

    if(t.flags.get(Q30TF_RESETANIM))
    {
        int cid2 = h_data(h);
        reset_combo_animation(cid2);
        reset_combo_animation2(cid2);
    }

    if(t.trigcopycat)
    {
        used_bit = true;
        trig_copycat_from(cid, h, t.trigcopycat);
    }

    timer.data = h_data(h);

    if(t.trigcooldown)
        timer.t(idx).cooldown = t.trigcooldown;

    if(t.player_bounce)
        Link.setFall((fix)(-(t.player_bounce / 100)));

    if(t.flags.get(Q30TF_SETPLAYER_X_ABS))
        Link.setX(a_int(t.dest_player_x));
    else if(t.flags.get(Q30TF_SETPLAYER_X_REL_CMB))
        Link.setX(a_int(t.dest_player_x) + center_x - 8);
    else if(t.dest_player_x)
        Link.setX(int(Link.getX()) + a_int(t.dest_player_x));

    if(t.flags.get(Q30TF_SETPLAYER_Y_ABS))
        Link.setY(a_int(t.dest_player_y));
    else if(t.flags.get(Q30TF_SETPLAYER_Y_REL_CMB))
        Link.setY(a_int(t.dest_player_y) + center_y - 8);
    else if(t.dest_player_y)
        Link.setY(int(Link.getY()) + a_int(t.dest_player_y));

    if(t.flags.get(Q30TF_SETPLAYER_Z_ABS))
        Link.setZ(a_int(t.dest_player_z));
    else if(t.dest_player_z)
        Link.setZ(int(Link.getZ()) + a_int(t.dest_player_z));

    if(t.dest_player_dir > -1)
        Link.setDir(t.dest_player_dir);

    // ice: q30_ice state lives on the Link side
    if(t.force_ice_combo > -1)
        q30_force_ice(t.force_ice_combo);

    if(t.flags.get(Q30TF_FORCE_ICE_VX) || t.flags.get(Q30TF_FORCE_ICE_VY))
        q30_force_ice_velocity(t.flags.get(Q30TF_FORCE_ICE_VX), t.force_ice_vx, t.flags.get(Q30TF_FORCE_ICE_VY), t.force_ice_vy);
}

struct ActiveTrig
{
    bool is_ffc;
    int id, layer;
    size_t idx;
};

std::vector<ActiveTrig> g_active;

bool active_has(const H &h, size_t idx)
{
    int id = h.is_ffc() ? h.ffc : h.pos;

    for(size_t i = 0; i < g_active.size(); ++i)
        if(g_active[i].is_ffc == h.is_ffc() && g_active[i].id == id && g_active[i].layer == h.layer && g_active[i].idx == idx)
            return true;

    return false;
}

static int g_trig_trace = -1;

bool run_trigger_core(const H &h, size_t idx, int special, weapon *w)
{
    if(h.is_ffc() && rule(qr30_OLD_FFC_FUNCTIONALITY))
        return false;

    const Q30Combo *c = h_combo(h);

    if(!c || idx >= c->triggers.size())
        return false;

    if(h.is_ffc() && (tmpscr[0].ffflags[h.ffc] & ffCHANGER))
        return false;   // changers can't trigger

    const Q30Trigger &t = c->triggers[idx];
    int cid = h_data(h);
    int ocs = h_cset(h);
    bool hasitem = false;

    if(w && t.flags.get(Q30TF_SEPARATEWEAPON))
    {
        do_weapon_fx(w, t);
        return true;
    }

    bool force_ex = false;

    for(size_t q = 0; q < c->triggers.size(); ++q)
    {
        const Q30Trigger &ex = c->triggers[q];

        if(ex.exstate > -1 && !ex.flags.get(Q30TF_UNSETEXSTATE) && force_ex_trigger(h, q))
        {
            force_ex = true;

            if(h_data(h) != cid)
                break;
        }

        if(ex.exdoor_dir > -1 && !ex.flags.get(Q30TF_UNSETEXDOOR) && force_ex_door_trigger(h, q))
        {
            force_ex = true;

            if(h_data(h) != cid)
                break;
        }

        if(ex.flags.get(Q30TF_COMBOPOSSTATE_CAUSE) && force_combopos_trigger(h, q))
        {
            force_ex = true;

            if(h_data(h) != cid)
                break;
        }
    }

    if(force_ex)
        return true;

    if(g_trig_trace < 0)
        g_trig_trace = getenv("ZC_Q30_TRIGTRACE") ? 1 : 0;

    if(!conditions(h, idx, hasitem, true))
    {
        if(g_trig_trace)
            al_trace("q30 trig FAIL cid %d idx %d type %d lyr %d pos %d ffc %d wpn %d\n", cid, (int)idx, c->type, h.layer, h.pos, h.ffc, w ? w->id : -1);

        trigger_string(t, false);
        return false;
    }

    if(g_trig_trace)
        al_trace("q30 trig RUN cid %d idx %d type %d lyr %d pos %d ffc %d wpn %d special %d\n", cid, (int)idx, c->type, h.layer, h.pos, h.ffc, w ? w->id : -1, special);

    int key = h.is_ffc() ? 2000 + h.ffc : h.layer * 176 + h.pos;
    bool check_bit = w ? wpn_done(w, key) : false;
    bool used_bit = false;
    PosInfo &timer = h_info(h);
    bool dorun = !timer.t(idx).cooldown;

    if(dorun)
    {
        if(t.flags.get(Q30TF_CMBTYPEFX) || c->type == Q30C_CUSTOMBLOCK)
            trigger_type_effects(h, c, t, idx, w, special, cid, used_bit);

        if(!check_bit)
            results(h, t, idx, hasitem, used_bit, special, timer, cid, ocs, w);

        if(w && used_bit)
            wpn_mark(w, key);
    }

    if(w)
        do_weapon_fx(w, t);

    if(dorun && !check_bit)
    {
        if(t.trig_genscr)
            q30_run_generic_script(t.trig_genscr);

        trigger_string(t, true);
    }

    return true;
}

bool do_trigger(const H &h, size_t idx, int special, weapon *w)
{
    if(rule(qr30_BROKEN_SELF_TRIGGERING_TRIGGERS))
    {
        bool broken_ret = run_trigger_core(h, idx, special, w);
        states_flush();
        return broken_ret;
    }

    if(active_has(h, idx))
        return false;

    ActiveTrig a;
    a.is_ffc = h.is_ffc();
    a.id = h.is_ffc() ? h.ffc : h.pos;
    a.layer = h.layer;
    a.idx = idx;
    g_active.push_back(a);
    bool ret = run_trigger_core(h, idx, special, w);
    g_active.pop_back();
    states_flush();
    return ret;
}

// ---- types --------------------------------------------------------------------

void trigger_cswitch_block(const H &h, const Q30Combo *c);
bool trigger_shooter(const Q30Combo *c, int wx, int wy);
void trigger_stepfx(const H &h, const Q30Combo *c, bool stepped);
void trigger_sign(const Q30Combo *c);
void do_cswitch(const Q30Combo *c, weapon *w);
void do_generic_effect(const H &h, const Q30Combo *c, int cid, weapon *w);
void trigger_cuttable(const H &h, const Q30Combo *c);
bool trigger_step(const H &h, const Q30Combo *c);
void trigger_chest(const H &h, const Q30Combo *c);
void trigger_lockblock(const H &h, const Q30Combo *c);
void trigger_armos_grave(const H &h, const Q30Combo *c);

// A warp can't run while triggers are being walked: Link's frame picks it up.
void queue_warp(int type)
{
    int letter;

    switch(type)
    {
    case cSTAIRB: case cSWIMWARPB: case cDIVEWARPB: case cPITB: case cAWARPB: case cSWARPB: letter = 1; break;
    case cSTAIRC: case cSWIMWARPC: case cDIVEWARPC: case cPITC: case cAWARPC: case cSWARPC: letter = 2; break;
    case cSTAIRD: case cSWIMWARPD: case cDIVEWARPD: case cPITD: case cAWARPD: case cSWARPD: letter = 3; break;
    case cSTAIRR: case cPITR: case cAWARPR: case cSWARPR: letter = rand() % 4; break;
    default: letter = 0;
    }

    bool side = (type >= cAWARPA && type <= cAWARPR) || (type >= cSWARPA && type <= cSWARPR);
    g_pending_warp_type = side ? 1 : 0;
    g_pending_warp_index = letter;
}

void trigger_damage(const Q30Combo *c)
{
    if(Link.getHClk())
        return;

    int dmg = (c->usrflags & 0x1) ? -a_int(c->attributes[0]) : combo_class_buf[c->type].modify_hp_amount;

    if(dmg >= 0)
        return;

    int boots = current_item_id(itype_boots);

    if(boots >= 0 && !(tmpscr[0].flags5 & fDAMAGEWITHBOOTS) && (4 << itemsbuf[boots].power) >= -dmg
       && checkmagiccost(boots))
    {
        paymagiccost(boots);
        return;
    }

    int ring = current_item_id(itype_ring);
    int hurt = -dmg;

    if(ring >= 0 && itemsbuf[ring].power > 0)
        hurt = zc_max(1, hurt / itemsbuf[ring].power);

    game->set_life(zc_max(0, (int)game->get_life() - hurt));
    Link.setHClk(48);
    sfx(WAV_OUCH, pan(int(Link.getX())));
}

void trigger_type_effects(const H &h, const Q30Combo *c, const Q30Trigger &t, size_t idx,
                          weapon *w, int special, int cid, bool &used_bit)
{
    int type = c->type;

    switch(type)
    {
    case cSCRIPT1: case cSCRIPT2: case cSCRIPT3: case cSCRIPT4: case cSCRIPT5:
    case Q30C_SCRIPT6: case Q30C_SCRIPT7: case Q30C_SCRIPT8: case Q30C_SCRIPT9: case Q30C_SCRIPT10:
    case Q30C_TRIGGERGENERIC:
        do_generic_effect(h, c, cid, w);
        break;

    case Q30C_CUSTOMBLOCK:
        if(w)
        {
            if(w->dead == 0)
                w->dead = 1;

            if(a_int(c->attributes[8]))
                sfx(a_int(c->attributes[8]));
        }

        break;
    }

    int key = h.is_ffc() ? 2000 + h.ffc : h.layer * 176 + h.pos;

    if(w && wpn_done(w, key))
        return;

    used_bit = true;

    switch(type)
    {
    case Q30C_CSWITCH:
        do_cswitch(c, w);
        break;

    case Q30C_CSWITCHBLOCK:
        trigger_cswitch_block(h, c);
        break;

    case Q30C_SIGNPOST:
        if(!(special & ctrigIGNORE_SIGN))
            trigger_sign(c);

        break;

    case cSLASH: case cSLASHITEM: case cBUSH: case cFLOWERS: case cTALLGRASS:
    case cTALLGRASSNEXT: case cSLASHNEXT: case cSLASHNEXTITEM: case cBUSHNEXT:
    case cSLASHTOUCHY: case cSLASHITEMTOUCHY: case cBUSHTOUCHY: case cFLOWERSTOUCHY:
    case cTALLGRASSTOUCHY: case cSLASHNEXTTOUCHY: case cSLASHNEXTITEMTOUCHY:
    case cBUSHNEXTTOUCHY:
        trigger_cuttable(h, c);
        break;

    case cSTEP: case cSTEPSAME: case cSTEPALL:
        trigger_step(h, c);
        break;

    case cCHEST: case cLOCKEDCHEST: case cBOSSCHEST:
        trigger_chest(h, c);
        break;

    case cLOCKBLOCK: case cBOSSLOCKBLOCK:
        trigger_lockblock(h, c);
        break;

    case cARMOS: case cBSGRAVE: case cGRAVE:
        trigger_armos_grave(h, c);
        break;

    case Q30C_STEPSFX:
        trigger_stepfx(h, c, false);
        break;

    case cSTAIR: case cSTAIRB: case cSTAIRC: case cSTAIRD: case cSTAIRR:
    case cSWIMWARP: case cSWIMWARPB: case cSWIMWARPC: case cSWIMWARPD:
    case cDIVEWARP: case cDIVEWARPB: case cDIVEWARPC: case cDIVEWARPD:
    case cPIT: case cPITB: case cPITC: case cPITD: case cPITR:
    case cAWARPA: case cAWARPB: case cAWARPC: case cAWARPD: case cAWARPR:
    case cSWARPA: case cSWARPB: case cSWARPC: case cSWARPD: case cSWARPR:
        queue_warp(type);
        break;

    case cDAMAGE1: case cDAMAGE2: case cDAMAGE3: case cDAMAGE4:
    case cDAMAGE5: case cDAMAGE6: case cDAMAGE7:
        trigger_damage(c);
        break;

    case Q30C_SHOOTER:
    {
        int x, y;
        h_xy(h, x, y);
        trigger_shooter(c, x, y);
        break;
    }

    case Q30C_CUTSCENETRIG:
        q30_cutscene_flags(c);
        break;

    default:
        used_bit = false;
    }
}

// ---- generic combos ---------------------------------------------------------------

void do_generic_effect(const H &h, const Q30Combo *c, int cid, weapon *w)
{
    if(c->type < Q30C_TRIGGERGENERIC && !(c->usrflags & 0x100))
        return;     // script combos need the Engine flag

    int x, y;
    h_xy(h, x, y);
    mapscr *m = h_scr(h);

    if(!m)
        return;

    int key = h.is_ffc() ? 2000 + h.ffc : h.layer * 176 + h.pos;

    if(w && wpn_done(w, key) && !(c->usrflags & 0x10))
    {
        if(c->usrflags & 0x80)
        {
            if(w->id != wSword && w->id != wHammer && w->dead == 0)
                w->dead = 1;
        }

        return;
    }

    if(c->usrflags & 0x1)           // decoration
    {
        int dtype = -1;

        if(c->usrflags & 0x200)
            dtype = a_int(c->attributes[8]);

        switch(dtype)
        {
        case -1:
            break;              // sprite: comboSprite, not available here

        case 2:
            decorations.add(new dFlowerClippings((fix)x, (fix)y, dFLOWERCLIPPINGS, 0));
            break;

        case 3:
            decorations.add(new dGrassClippings((fix)x, (fix)y, dGRASSCLIPPINGS, 0));
            break;

        default:
            decorations.add(new dBushLeaves((fix)x, (fix)y, dBUSHLEAVES, 0));
            break;
        }
    }

    int it = -1;

    if(c->usrflags & 0x2)
    {
        long id = a_int(c->attributes[9]);

        if(c->usrflags & 0x400)
            it = (int)id;
        else
            it = select_dropitem((int)id, x, y);
    }

    if(it >= 0 && it < MAXITEMS)
        items.add(new item((fix)x, (fix)y, (fix)0, it, ipBIGRANGE + ipTIMER, 0));

    if((c->usrflags & 0x20) && !getmapflag(mBELOW) && m->catchall < MAXITEMS)
    {
        items.add(new item((fix)x, (fix)y, (fix)0, m->catchall, ipONETIME2 | ipBIGRANGE
                           | ((itemsbuf[m->catchall].family == itype_triforcepiece || (m->flags3 & fHOLDITEM)) ? ipHOLDUP : 0)
                          , 0));
    }

    int ft = a_int(c->attributes[11]);

    if(ft < 0)
        ft = 0;
    else if(ft > 43)
        ft = 43;

    if(c->usrflags & 0x40)      // screen secret combo
    {
        if(!h.is_ffc())
        {
            h_set_data(h, tmpscr[0].secretcombo[ft]);
            h_set_cset(h, tmpscr[0].secretcset[ft]);
            m->sflag[h.pos] = tmpscr[0].secretflag[ft];
        }
        else
        {
            h_set_data(h, tmpscr[0].secretcombo[ft]);
            h_set_cset(h, tmpscr[0].secretcset[ft]);
        }

        if(a_int(c->attributes[10]) > 0)
            sfx(a_int(c->attributes[10]), pan(x));
    }

    if(c->usrflags & 0x8)       // loop to next combo / undercombo
    {
        const Q30Combo *loop = c;
        int lc = cid, guard = 0;

        do
        {
            if(loop->usrflags & 0x800)
            {
                h_set_data(h, tmpscr[0].undercombo);
                h_set_cset(h, tmpscr[0].undercset);

                if(!h.is_ffc())
                    m->sflag[h.pos] = 0;
            }
            else
                h_set_data(h, zc_min(h_data(h) + 1, MAXCOMBOS - 1));

            if(loop->usrflags & 0x800)
                break;

            if(!(loop->usrflags & 0x10))
                break;

            lc = h_data(h);
            loop = q30_combo(lc);
        }
        while(loop && (loop->usrflags & 0x10) && loop->type == Q30C_TRIGGERGENERIC && lc < MAXCOMBOS - 1 && ++guard < 1024);

        if(a_int(c->attributes[10]) > 0)
            sfx(a_int(c->attributes[10]), pan(x));
    }

    if(c->usrflags & 0x2000)
        addenemy(x, y, a_int(c->attributes[12]), (c->usrflags & 0x1000) ? 0 : -15);

    if(w)
    {
        wpn_mark(w, key);

        if((c->usrflags & 0x80) && w->id != wSword && w->id != wHammer && w->dead == 0)
            w->dead = 1;
    }
}

void trigger_sign(const Q30Combo *c)
{
    play_string(a_int(c->attributes[0]));
}

void do_cswitch(const Q30Combo *c, weapon *w)
{
    unsigned pair = a_int(c->attributes[8]);

    if(pair > 255)
        return;

    if(c->usrflags & 0x400)         // global state
    {
        long tmr = a_int(c->attributes[2]);
        bool oldstate = g_gswitch_timers[pair] != 0;

        if(tmr > 0)
            (states_mark(), g_gswitch_timers[pair]) = tmr;
        else if(g_gswitch_timers[pair])
            (states_mark(), g_gswitch_timers[pair]) = 0;
        else
            (states_mark(), g_gswitch_timers[pair]) = -1;

        if(oldstate != (g_gswitch_timers[pair] != 0))
            toggle_gswitch(pair, false);
    }
    else
    {
        if(pair > 31)
            return;

        (states_mark(), g_lvlswitches[cur_dlevel()]) ^= (1u << pair);
        toggle_switches(1u << pair, false);
    }

    if(w && (c->usrflags & 0x1) && w->dead == 0 && w->id != wSword && w->id != wHammer)
        w->dead = 1;

    if(a_int(c->attributes[9]))
        sfx(a_int(c->attributes[9]));
}

void apply_block_toggle(const H &h, int cmbofs, int csofs)
{
    change_combo(h, h_data(h) + cmbofs);
    h_set_cset(h, (h_cset(h) + csofs) & 15);
    int n = h_data(h);
    reset_combo_animation(n);
    reset_combo_animation2(n);
}

void trigger_cswitch_block(const H &h, const Q30Combo *c)
{
    int cmbofs = a_int(c->attributes[0]), csofs = a_int(c->attributes[1]);
    apply_block_toggle(h, cmbofs, csofs);

    int bx, by;
    h_xy(h, bx, by);

    if(h.is_ffc())
    {
        bx += 8;
        by += 8;
    }

    int rpos = ((bx / 16) & 15) + (by / 16) * 16;

    if(rpos < 0 || rpos > 175)
        return;

    for(int lyr = 0; lyr <= 6; ++lyr)
    {
        if(lyr == h.layer)
            continue;

        if(!(c->usrflags & (1 << lyr)))
            continue;

        H other(lyr, rpos);
        mapscr *m = h_scr(other);

        if(!m || !m->data[rpos])
            continue;

        apply_block_toggle(other, cmbofs, csofs);
    }

    if(c->usrflags & 0x400)
    {
        int fx = bx + 8, fy = by + 8;

        for(int q = 0; q < MAXFFCS; ++q)
        {
            if(h.is_ffc() && q == h.ffc)
                continue;

            if(tmpscr[0].ffdata[q] && ffc_at(q, fx, fy))
                apply_block_toggle(H(q), cmbofs, csofs);
        }
    }
}

void toggle_pos(mapscr *scr, int pos, int cmbofs, int csofs, bool entry, dword usrflags)
{
    int next = scr->data[pos] + cmbofs;

    if(next < 0)
        next = 0;
    else if(next >= MAXCOMBOS)
        next = MAXCOMBOS - 1;

    scr->data[pos] = (word)next;
    scr->cset[pos] = (byte)((scr->cset[pos] + csofs) & 15);

    if(entry && (usrflags & 0x80))
    {
        // skip through cycling combos on screen entry
        std::vector<int> seen(1, next);
        int guard = 0;

        while(combobuf[next].nextcombo != 0 && ++guard < 256)
        {
            int cid = combobuf[next].nextcombo;

            if(std::find(seen.begin(), seen.end(), cid) != seen.end())
                break;

            scr->data[pos] = (word)cid;

            if(!(combobuf[next].animflags & 0x04))
                scr->cset[pos] = combobuf[next].nextcset;

            seen.push_back(cid);
            next = cid;
        }
    }

    reset_combo_animation(scr->data[pos]);
    reset_combo_animation2(scr->data[pos]);
}

bool is_switch_type(int t)
{
    return t == Q30C_CSWITCH || t == Q30C_CSWITCHBLOCK;
}

struct LevelCond { dword flags; };

bool cond_level_trigger(const Q30Trigger &t, size_t, void *ctx)
{
    dword flags = *(dword *)ctx;
    return t.flags.get(Q30TF_TRIGLEVELSTATE) && t.trig_lstate < 32 && (flags & (1u << t.trig_lstate));
}

void toggle_switches(dword flags, bool entry)
{
    if(!flags)
        return;

    byte togglegrid[176];
    memset(togglegrid, 0, sizeof(togglegrid));

    for(int lyr = 0; lyr <= 6; ++lyr)
    {
        mapscr *scr = lyr ? &tmpscr2[lyr - 1] : &tmpscr[0];

        if(lyr && (!scr->valid || scr->data.empty()))
            continue;

        for(int pos = 0; pos < 176; ++pos)
        {
            const Q30Combo *c = q30_combo(scr->data[pos]);

            if(c && !c->triggers.empty())
                execute(H(lyr, pos), cond_level_trigger, &flags, ctrigSWITCHSTATE);

            if(!c)
                continue;

            unsigned attr = a_int(c->attributes[8]);

            if(!is_switch_type(c->type) || attr >= 32 || (c->usrflags & 0x400) || !(flags & (1u << attr)))
                continue;

            int cmbofs = a_int(c->attributes[0]), csofs = a_int(c->attributes[1]);
            dword cflags = c->usrflags;
            int ctype = c->type;

            toggle_pos(scr, pos, cmbofs, csofs, entry, cflags);
            togglegrid[pos] |= (byte)(1 << lyr);

            if(ctype == Q30C_CSWITCH)
                continue;

            for(int lyr2 = 0; lyr2 < 7; ++lyr2)
            {
                if(lyr == lyr2 || !(cflags & (1 << lyr2)) || (togglegrid[pos] & (1 << lyr2)))
                    continue;

                mapscr *s2 = lyr2 ? &tmpscr2[lyr2 - 1] : &tmpscr[0];

                if(lyr2 && (!s2->valid || s2->data.empty()))
                    continue;

                if(!s2->data[pos])
                    continue;

                const Q30Combo *c2 = q30_combo(s2->data[pos]);

                if(c2 && lyr2 > lyr && is_switch_type(c2->type) && !(cflags & 0x400)
                    && (unsigned)a_int(c2->attributes[8]) < 32 && (flags & (1u << a_int(c2->attributes[8]))))
                    continue;   // a switch/block that will be hit later in the loop

                toggle_pos(s2, pos, cmbofs, csofs, entry, cflags);
                togglegrid[pos] |= (byte)(1 << lyr2);
            }
        }
    }

    for(int i = 0; i < MAXFFCS; ++i)
    {
        if(tmpscr[0].ffdata[i])
            execute(H(i), cond_level_trigger, &flags, ctrigSWITCHSTATE);
    }
}

bool cond_global_trigger(const Q30Trigger &t, size_t, void *ctx)
{
    const bool *states = (const bool *)ctx;
    return t.flags.get(Q30TF_TRIGGLOBALSTATE) && states[t.trig_gstate];
}

void toggle_gswitches(bool *states, bool entry)
{
    byte togglegrid[176];
    memset(togglegrid, 0, sizeof(togglegrid));

    for(int lyr = 0; lyr <= 6; ++lyr)
    {
        mapscr *scr = lyr ? &tmpscr2[lyr - 1] : &tmpscr[0];

        if(lyr && (!scr->valid || scr->data.empty()))
            continue;

        for(int pos = 0; pos < 176; ++pos)
        {
            const Q30Combo *c = q30_combo(scr->data[pos]);

            if(!c)
                continue;

            if(!c->triggers.empty())
                execute(H(lyr, pos), cond_global_trigger, states, ctrigSWITCHSTATE);

            if(!(c->usrflags & 0x400))
                continue;

            if(!is_switch_type(c->type))
                continue;

            unsigned attr = a_int(c->attributes[8]);

            if(attr >= 256 || !states[attr])
                continue;

            int cmbofs = a_int(c->attributes[0]), csofs = a_int(c->attributes[1]);
            dword cflags = c->usrflags;
            int ctype = c->type;
            toggle_pos(scr, pos, cmbofs, csofs, entry, cflags);
            togglegrid[pos] |= (byte)(1 << lyr);

            if(ctype == Q30C_CSWITCH)
                continue;

            for(int lyr2 = 0; lyr2 <= 6; ++lyr2)
            {
                if(lyr == lyr2 || !(cflags & (1 << lyr2)) || (togglegrid[pos] & (1 << lyr2)))
                    continue;

                mapscr *s2 = lyr2 ? &tmpscr2[lyr2 - 1] : &tmpscr[0];

                if(lyr2 && (!s2->valid || s2->data.empty()))
                    continue;

                if(!s2->data[pos])
                    continue;

                const Q30Combo *c2 = q30_combo(s2->data[pos]);

                if(c2 && lyr2 > lyr && is_switch_type(c2->type) && (c2->usrflags & 0x400)
                    && (unsigned)a_int(c2->attributes[8]) < 256 && states[a_int(c2->attributes[8])])
                    continue;

                toggle_pos(s2, pos, cmbofs, csofs, entry, cflags);
                togglegrid[pos] |= (byte)(1 << lyr2);
            }
        }
    }

    for(int i = 0; i < MAXFFCS; ++i)
    {
        if(tmpscr[0].ffdata[i])
            execute(H(i), cond_global_trigger, states, ctrigSWITCHSTATE);
    }
}

void toggle_gswitch(int state, bool entry)
{
    bool states[256];
    memset(states, 0, sizeof(states));
    states[state] = true;
    toggle_gswitches(states, entry);
}

// ---- shooters ---------------------------------------------------------------------

// fires one weapon; returns the weapon, already added to its list
weapon *fire_shooter_wpn(const Q30Combo *c, int wx, int wy, double radians, bool angular, int dir)
{
    long weapid = a_int(c->attributes[9]);

    if(!weapid)
        return NULL;

    int weapspr = a_int(c->attributes[10]);
    int damage = a_int(c->attributes[18]);
    long steprate = c->attributes[2] / 100;

    if(damage < 0)
        damage = 0;

    bool lw = weapid < wEnemyWeapons;

    if(weapid >= wScript1 && weapid <= wScript10)
        lw = (c->usrflags & 0x10) != 0;

    bool boss = (c->usrflags & 0x80) != 0;
    weapon *wpn;

    if(lw)
    {
        int pitem = a_int(c->attributes[14]);

        if(pitem <= 0 || pitem >= MAXITEMS)
            pitem = -1;

        int plvl = pitem > -1 ? itemsbuf[pitem].fam_type : 0;
        wpn = new weapon((fix)wx, (fix)wy, (fix)0, weapid, plvl, damage, dir, pitem, Link.getUID());
        Lwpns.add(wpn);
    }
    else
    {
        wpn = new weapon((fix)wx, (fix)wy, (fix)0, weapid, 0, damage, dir, -1, -1);
        Ewpns.add(wpn);
    }

    if(weapspr > 0 && weapspr < MAXWPNS)
        wpn->LOADGFX(weapspr);

    wpn->angular = angular;
    wpn->angle = radians;
    wpn->dir = dir;
    wpn->step = (fix)steprate / 100.f * 1.f;
    wpn->xofs = 0;
    wpn->yofs = playing_field_offset;

    if(weapid == ewFireball || weapid == ewFireball2)
    {
        if(boss)
            wpn->type |= 1;
    }


    return wpn;
}

int dir_of_angle(double rad)
{
    double a = rad * 180 / PI;

    while(a < 0)
        a += 360;

    while(a >= 360)
        a -= 360;

    if(a < 22.5 || a >= 337.5) return right;
    if(a < 67.5) return r_down;
    if(a < 112.5) return down;
    if(a < 157.5) return l_down;
    if(a < 202.5) return left;
    if(a < 247.5) return l_up;
    if(a < 292.5) return up;
    return r_up;
}

bool trigger_shooter(const Q30Combo *c, int wx, int wy)
{
    if(c->type != Q30C_SHOOTER || !a_int(c->attributes[9]))
        return false;

    if(wx >= 256 || wx < -15 || wy >= 176 || wy < -15)
        return false;

    bool proxstop = (c->usrflags & 0x8) != 0;
    bool invprox = (c->usrflags & 0x100) != 0;
    double proxlim = c->attributes[1] / 10000.0;
    double dx = wx - (double)(int)Link.getX(), dy = wy - (double)(int)Link.getY();
    double dst = sqrt(dx * dx + dy * dy);

    if(proxstop && (invprox ? (dst > proxlim) : (dst <= proxlim)))
        return false;

    bool angular = (c->usrflags & 0x1) != 0;
    double radians = 0;
    int dir = -1;
    long zdeg = c->attributes[0];

    if(angular)
    {
        if(zdeg >= 0 && zdeg <= 3600000)
        {
            radians = (zdeg / 10000.0) * PI / 180.0;
            dir = dir_of_angle(radians);
        }
        else
        {
            double at_player = atan2(-dy, -dx);

            switch(a_int(zdeg))
            {
            case -1:
                angular = false;
                dir = dir_of_angle(at_player) ;

                // 4-dir
                if(dir == r_down || dir == r_up) dir = right;
                else if(dir == l_down || dir == l_up) dir = left;

                break;

            case -2:
                angular = false;
                dir = dir_of_angle(at_player);
                break;

            case -3:
                radians = at_player;
                dir = dir_of_angle(at_player);
                break;

            default:
                dir = right;
            }
        }
    }
    else
    {
        dir = a_int(zdeg);

        if(dir < -1) dir = -1;
        else if(dir > 15) dir = 15;
    }

    int shotsfx = a_int(c->attributes[8]);
    int shotcount = zc_max(1, a_int(c->attributes[11]));

    if((c->usrflags & 0x40) && shotcount > 1)
    {
        if(!angular)
        {
            angular = true;

            switch(dir)
            {
            case up: radians = -PI / 2; break;
            case down: radians = PI / 2; break;
            case left: radians = PI; break;
            case right: radians = 0; break;
            case l_up: radians = -3 * PI / 4; break;
            case r_up: radians = -PI / 4; break;
            case l_down: radians = 3 * PI / 4; break;
            case r_down: radians = PI / 4; break;
            }
        }

        double spread = (c->attributes[3] / 10000.0) * PI / 180.0;
        double start;

        if(shotcount & 1)
            start = radians - spread * (shotcount / 2);
        else
            start = radians - spread * (shotcount / 2.0 - 0.5);

        for(int q = 0; q < shotcount; ++q)
            fire_shooter_wpn(c, wx, wy, start + q * spread, angular, dir);
    }
    else
        fire_shooter_wpn(c, wx, wy, radians, angular, dir);

    if(shotsfx)
        sfx(shotsfx);

    return true;
}

void handle_shooter(const Q30Combo *c, PosInfo &timer, int wx, int wy)
{
    int lowrate = zc_max(0, a_int(c->attributes[16]));
    int highrate = zc_max(0, a_int(c->attributes[17]));
    bool splitrate = (c->usrflags & 0x2) != 0;
    bool instashot = (c->usrflags & 0x4) != 0;

    if(splitrate)
    {
        if(lowrate > highrate)
            return;

        if(lowrate == highrate)
            splitrate = false;
    }

    if(!splitrate && !lowrate)
        return;

    if(timer.type_clk > 1)
    {
        if(--timer.type_clk == 1 && !instashot)
            trigger_shooter(c, wx, wy);
    }
    else
    {
        int rate = splitrate ? (rand() % (highrate - lowrate + 1) + lowrate) : lowrate;
        timer.type_clk = (word)zc_max(1, rate);

        if(instashot || timer.type_clk == 1)
            trigger_shooter(c, wx, wy);
    }
}

void trigger_stepfx(const H &h, const Q30Combo *c, bool stepped)
{
    int tx, ty;
    h_xy(h, tx, ty);
    sfx(a_int(c->attributes[8]), pan(tx));

    if(!(c->usrflags & 0x1))
        return;

    int wpn = a_int(c->attributes[9]);
    int wpdir = a_int(c->attributes[10]);

    if(wpdir > r_down || wpdir < 0)
        wpdir = rand() & 3;

    int damg = a_int(c->attributes[0]);

    if(damg < 1)
        damg = 4;

    int parent = a_int(c->attributes[12]);

    if(parent <= 0 || parent >= MAXITEMS)
        parent = -1;

    int wlvl = parent > -1 ? itemsbuf[parent].fam_type : 0;

    switch(wpn)
    {
    case ewFireball: case ewArrow: case ewBrang: case ewSword: case ewRock: case ewMagic:
    case ewBomb: case ewSBomb: case ewLitBomb: case ewLitSBomb: case ewFireTrail: case ewFlame:
    case ewWind: case ewFlame2: case ewFlame2Trail: case ewIce: case ewFireball2:
        Ewpns.add(new weapon((fix)tx, (fix)ty, (fix)0, wpn, 0, damg, wpdir, -1, -1));
        break;

    case wBeam: case wBrang: case wBomb: case wSBomb: case wLitBomb: case wLitSBomb: case wArrow:
    case wWhistle: case wBait: case wMagic: case wWind: case wRefMagic: case wRefFireball: case wRefRock:
    case wRefBeam: case wFire: case wSSparkle: case wFSparkle:
        Lwpns.add(new weapon((fix)tx, (fix)ty, (fix)0, wpn, wlvl, damg, wpdir, parent, Link.getUID()));
        break;

    case wScript1: case wScript2: case wScript3: case wScript4: case wScript5:
    case wScript6: case wScript7: case wScript8: case wScript9: case wScript10:
        if(c->usrflags & 0x2)
            Lwpns.add(new weapon((fix)tx, (fix)ty, (fix)0, wpn, wlvl, damg, wpdir, parent, Link.getUID()));
        else
            Ewpns.add(new weapon((fix)tx, (fix)ty, (fix)0, wpn, 0, damg, wpdir, -1, -1));

        break;

    default:
        Ewpns.add(new weapon((fix)tx, (fix)ty, (fix)0, ewLitBomb, 0, damg, up, -1, -1));
        break;
    }

    if(!(c->usrflags & 0x4))
        change_combo(h, h_data(h) + 1);
}

// ---- cuttables, steps, chests, lock blocks, armos -----------------------------------

bool is_cuttable(int t)
{
    switch(t)
    {
    case cSLASH: case cSLASHITEM: case cBUSH: case cFLOWERS: case cTALLGRASS: case cTALLGRASSNEXT:
    case cSLASHNEXT: case cSLASHNEXTITEM: case cBUSHNEXT: case cSLASHTOUCHY: case cSLASHITEMTOUCHY:
    case cBUSHTOUCHY: case cFLOWERSTOUCHY: case cTALLGRASSTOUCHY: case cSLASHNEXTTOUCHY:
    case cSLASHNEXTITEMTOUCHY: case cBUSHNEXTTOUCHY:
        return true;
    }

    return false;
}

void trigger_cuttable(const H &h, const Q30Combo *c)
{
    int type = c->type;

    if(!is_cuttable(type) || h.is_ffc())
        return;

    mapscr *scr = h_scr(h);
    int flag = h_sflag(h);
    int flag2 = c->flag;
    int x, y;
    h_xy(h, x, y);
    bool skip_secrets = isCuttableNextType(type);
    bool done = false;

    if(!skip_secrets)
    {
        done = true;

        if(flag >= mfSECRETS01 && flag <= mfSECRETS16)
        {
            h_set_data(h, scr->secretcombo[flag - mfSECRETS01 + 4]);
            h_set_cset(h, scr->secretcset[flag - mfSECRETS01 + 4]);
            scr->sflag[h.pos] = scr->secretflag[flag - mfSECRETS01 + 4];
        }
        else if(flag == mfARMOS_SECRET || flag2 == mfARMOS_SECRET)
        {
            h_set_data(h, scr->secretcombo[sSTAIRS]);
            h_set_cset(h, scr->secretcset[sSTAIRS]);
            scr->sflag[h.pos] = scr->secretflag[sSTAIRS];
            sfx(scr->secretsfx);
        }
        else if((flag >= mfSWORD && flag <= mfXSWORD) || flag == mfSTRIKE || (flag2 >= mfSWORD && flag2 <= mfXSWORD) || flag2 == mfSTRIKE)
        {
            for(int i = 0; i <= 3; ++i)
                findentrance(x, y, mfSWORD + i, true);

            findentrance(x, y, mfSTRIKE, true);
        }
        else if(flag2 >= mfSECRETS01 && flag2 <= mfSECRETS16)
        {
            h_set_data(h, scr->secretcombo[flag2 - mfSECRETS01 + 4]);
            h_set_cset(h, scr->secretcset[flag2 - mfSECRETS01 + 4]);
            scr->sflag[h.pos] = scr->secretflag[flag2 - mfSECRETS01 + 4];
        }
        else
            done = false;
    }

    if(!done)
    {
        if(isCuttableNextType(type))
            change_combo(h, h_data(h) + 1);
        else
        {
            h_set_data(h, scr->undercombo);
            h_set_cset(h, scr->undercset);
            scr->sflag[h.pos] = 0;
        }
    }

    if((flag == mfARMOS_ITEM || flag2 == mfARMOS_ITEM) && (!getmapflag(mBELOW) ))
    {
        items.add(new item((fix)x, (fix)y, (fix)0, tmpscr[0].catchall, ipONETIME2 + ipBIGRANGE + ipHOLDUP
                          , 0));
        sfx(tmpscr[0].secretsfx);
    }
    else if(isCuttableItemType(type))
    {
        int it = -1;

        if(c->usrflags & 0x2)
        {
            long id = a_int(c->attributes[9]);

            if(c->usrflags & 0x400)
                it = (int)id;
            else
                it = select_dropitem((int)id, x, y);
        }
        else
            it = select_dropitem(12, x, y);

        if(it != -1)
            items.add(new item((fix)x, (fix)y, (fix)0, it, ipBIGRANGE + ipTIMER, 0));
    }

    if(get_bit(quest_rules, qr_MORESOUNDS))
    {
        if(c->usrflags & 0x4)
            sfx(a_int(c->attributes[10]), pan(x));
        else if(isBushType(type) || isFlowersType(type) || isGrassType(type))
            sfx(WAV_ZN1GRASSCUT);
    }

    if(isBushType(type))
        decorations.add(new dBushLeaves((fix)x, (fix)y, dBUSHLEAVES, 0));
    else if(isFlowersType(type))
        decorations.add(new dFlowerClippings((fix)x, (fix)y, dFLOWERCLIPPINGS, 0));
    else if(isGrassType(type))
        decorations.add(new dGrassClippings((fix)x, (fix)y, dGRASSCLIPPINGS, 0));
}

bool is_step_type(int t)
{
    return t == cSTEP || t == cSTEPSAME || t == cSTEPALL || t == cSTEPCOPY;
}

bool trigger_step(const H &h, const Q30Combo *c)
{
    if(!is_step_type(c->type) || c->type == cSTEPCOPY)
        return false;

    int req_item = a_int(c->attributes[9]);

    if(req_item > 0 && req_item < MAXITEMS && !game->item[req_item])
        return false;

    if(a_int(c->attributes[8]))
    {
        int x, y;
        h_xy(h, x, y);
        sfx(a_int(c->attributes[8]), pan(x));
    }

    switch(c->type)
    {
    case cSTEP:
        change_combo(h, h_data(h) + 1);
        break;

    case cSTEPSAME:
    {
        int id = h_data(h);

        for(int p = 0; p < 176; ++p)
        {
            H nh(0, p);

            if(h_data(nh) == id)
                change_combo(nh, id + 1);
        }

        if(h.is_ffc() || h.layer > 0)
            change_combo(h, h_data(h) + 1);

        break;
    }

    case cSTEPALL:
        for(int p = 0; p < 176; ++p)
        {
            H nh(0, p);

            if(is_step_type(q30_combo_type(h_data(nh))))
                change_combo(nh, h_data(nh) + 1);
        }

        if(h.is_ffc() || h.layer > 0)
            change_combo(h, h_data(h) + 1);

        break;
    }

    return true;
}

bool can_locked_use(const Q30Combo *c)
{
    int requireditem = (c->usrflags & 0x1) ? a_int(c->attributes[8]) : 0;
    bool itemonly = (c->usrflags & 0x2) != 0;
    int thecounter = a_int(c->attributes[9]);
    int ctr_amount = a_int(c->attributes[0]);

    if(requireditem && has_item_for(requireditem))
        return true;

    if((c->usrflags & 0x1) && itemonly)
        return false;

    if(c->usrflags & 0x8)
    {
        if(game->get_counter(thecounter) >= ctr_amount)
            return true;

        return false;
    }

    if(ctr_amount)
        return false;   // keys-only count not checked by this quest type

    return !requireditem && !itemonly;
}

bool try_locked(const Q30Combo *c)
{
    int requireditem = (c->usrflags & 0x1) ? a_int(c->attributes[8]) : 0;
    bool itemonly = (c->usrflags & 0x2) != 0;
    int thecounter = a_int(c->attributes[9]);
    int ctr_amount = a_int(c->attributes[0]);

    if(requireditem && has_item_for(requireditem))
    {
        if(c->usrflags & 0x10)
            takeitem(requireditem);

        return true;
    }

    if((c->usrflags & 0x1) && itemonly)
        return false;

    if(c->usrflags & 0x8)
    {
        bool nodrain = (c->usrflags & 0x80) != 0;
        bool thief = !nodrain && (c->usrflags & 0x20);

        if(game->get_counter(thecounter) >= ctr_amount)
        {
            if(!nodrain)
                game->change_counter(-ctr_amount, thecounter);

            return true;
        }

        if(thief)
        {
            game->change_counter(-game->get_counter(thecounter), thecounter);
            return false;
        }
    }
    else if(ctr_amount)
    {
        if(game->get_lkeys() + game->get_keys() < ctr_amount)
            return false;

        game->change_keys(-ctr_amount);
        return true;
    }
    else if(!requireditem && !itemonly)
    {
        return usekey();
    }

    return false;
}

void remove_xstate_positions(int xstate);

void trigger_chest(const H &h, const Q30Combo *c)
{
    dword flag = (dword)(1 << a_int(c->attributes[13]));

    switch(c->type)
    {
    case cLOCKEDCHEST:
        if(!try_locked(c))
        {
            play_string(a_int(c->attributes[3]));
            return;
        }

        if(c->usrflags & 0x8000)
        {
            states_setup();
            (states_mark(), g_xstates[cur_mi()]) |= flag;
            remove_xstate_positions(a_int(c->attributes[13]));
            break;
        }

        setmapflag(mLOCKEDCHEST);
        break;

    case cCHEST:
        if(c->usrflags & 0x8000)
        {
            states_setup();
            (states_mark(), g_xstates[cur_mi()]) |= flag;
            remove_xstate_positions(a_int(c->attributes[13]));
            break;
        }

        setmapflag(mCHEST);
        break;

    case cBOSSCHEST:
        if(!(game->lvlitems[cur_dlevel()] & liBOSSKEY))
        {
            play_string(a_int(c->attributes[3]));
            return;
        }

        if(c->usrflags & 0x8000)
        {
            states_setup();
            (states_mark(), g_xstates[cur_mi()]) |= flag;
            remove_xstate_positions(a_int(c->attributes[13]));
            break;
        }

        setmapflag(mBOSSCHEST);
        break;
    }

    sfx(a_int(c->attributes[11]));

    bool itemflag = false;

    if(h.is_ffc())
        itemflag = c->flag == mfARMOS_ITEM;
    else
    {
        for(int i = 6; i >= 0; --i)
        {
            H other(i, h.pos);

            if(h_sflag(other) == mfARMOS_ITEM || q30_combo_flag(h_data(other)) == mfARMOS_ITEM)
            {
                itemflag = true;
                break;
            }
        }
    }

    bool itemstate = false;
    int ipflag = 0;

    if(c->usrflags & 0x40)
    {
        int f = (homescr < 128 && get_bit(quest_rules, qr_ITEMPICKUPSETSBELOW)) ? mITEM : mBELOW;
        itemstate = getmapflag(f);
        ipflag = (homescr < 128 && get_bit(quest_rules, qr_ITEMPICKUPSETSBELOW)) ? ipONETIME : ipONETIME2;
    }

    if(itemflag && !itemstate)
    {
        int pflags = ipflag | ipBIGRANGE | ipHOLDUP;
        int itid = a_int(c->attributes[18]);

        if(itid <= -10 && itid >= -17)
        {
            int di = (currdmap << 7) + homescr - (DMaps[currdmap].type == dmOVERW ? 0 : DMaps[currdmap].xoff);
            itid = game->screen_d[di][-(itid + 10)] / 10000L;
        }
        else if(itid == -1)
            itid = tmpscr[0].catchall;

        if(itid < 0 || itid >= MAXITEMS)
            itid = 0;

        item *itm = new item(Link.getX(), Link.getY(), (fix)0, itid, pflags, 0);
        items.add(itm);
        Link.checkitems(items.Count() - 1);     // the chest hands it straight to him
    }
}

void trigger_lockblock(const H &h, const Q30Combo *c)
{
    dword flag = (dword)(1 << a_int(c->attributes[13]));

    switch(c->type)
    {
    case cLOCKBLOCK:
        if(!try_locked(c))
        {
            play_string(a_int(c->attributes[3]));
            return;
        }

        if(c->usrflags & 0x8000)
        {
            states_setup();
            (states_mark(), g_xstates[cur_mi()]) |= flag;
            remove_xstate_positions(a_int(c->attributes[13]));
            break;
        }

        setmapflag(mLOCKBLOCK);
        remove_lockblocks(0);
        break;

    case cBOSSLOCKBLOCK:
        if(!(game->lvlitems[cur_dlevel()] & liBOSSKEY))
        {
            play_string(a_int(c->attributes[3]));
            return;
        }

        if(c->usrflags & 0x8000)
        {
            states_setup();
            (states_mark(), g_xstates[cur_mi()]) |= flag;
            remove_xstate_positions(a_int(c->attributes[13]));
            break;
        }

        setmapflag(mBOSSLOCKBLOCK);
        remove_bosslockblocks(0);
        break;

    default:
        return;
    }

    if(a_int(c->attributes[11]))
        sfx(a_int(c->attributes[11]));
}

void trigger_armos_grave(const H &h, const Q30Combo *c)
{
    if(h.is_ffc())
        return;

    int tx, ty;
    h_xy(h, tx, ty);
    int id2 = 0;
    bool nextcmb = false;
    int eclk = -14;

    switch(c->type)
    {
    case cARMOS:
        if(c->usrflags & 0x1)
            id2 = a_int(c->attributes[8 + ((c->usrflags & 0x2) ? (rand() % 2) : 0)]);
        else
        {
            for(int i = 0; i < eMAXGUYS; ++i)
                if(guysbuf[i].family == eeWALK && guysbuf[i].misc9 == e9tARMOS)
                {
                    id2 = i;

                    if(guysbuf[i].family == eeWALK && guysbuf[i].misc9 == e9tARMOS)
                        eclk = 0;

                    break;
                }
        }

        break;

    case cBSGRAVE:
        nextcmb = true;

    case cGRAVE:
        if(c->usrflags & 0x1)
            id2 = a_int(c->attributes[8 + ((c->usrflags & 0x2) ? (rand() % 2) : 0)]);
        else
        {
            for(int i = 0; i < eMAXGUYS; ++i)
                if(guysbuf[i].family == eeGHINI)
                {
                    id2 = i;
                    eclk = 0;
                    break;
                }
        }

        if(nextcmb)
            change_combo(h, h_data(h) + 1);

        break;

    default:
        return;
    }

    addenemy(tx, ty + 3, id2, eclk);
}

// A chest or lock block with the exstate bit set removes the rest of its screen's copies.
void remove_xstate_positions(int xstate)
{
    for(int lyr = 0; lyr <= 6; ++lyr)
    {
        mapscr *scr = lyr ? &tmpscr2[lyr - 1] : &tmpscr[0];

        if(lyr && (!scr->valid || scr->data.empty()))
            continue;

        for(int pos = 0; pos < 176; ++pos)
        {
            H h(lyr, pos);
            const Q30Combo *c = h_combo(h);

            if(!c)
                continue;

            for(size_t q = 0; q < c->triggers.size(); ++q)
            {
                if(force_ex_trigger(h, q, xstate))
                    break;
            }

            if(c->usrflags & 0x8000)
            {
                switch(c->type)
                {
                case cCHEST: case cLOCKEDCHEST: case cBOSSCHEST: case cLOCKBLOCK: case cBOSSLOCKBLOCK:
                    if(a_int(c->attributes[13]) == xstate)
                        change_combo(h, h_data(h) + 1);

                    break;
                }
            }
        }
    }

    for(int i = 0; i < MAXFFCS; ++i)
    {
        H h(i);
        const Q30Combo *c = h_combo(h);

        if(!c)
            continue;

        for(size_t q = 0; q < c->triggers.size(); ++q)
        {
            if(force_ex_trigger(h, q, xstate))
                break;
        }
    }
}

// ---- crumbling floors -------------------------------------------------------------

void trigger_crumble(const Q30Combo *c, PosInfo &timer, int &cid)
{
    ++cid;
    timer.data = cid;
    const Q30Combo *nc = q30_combo(cid);

    if(nc && nc->type == Q30C_CRUMBLE && (c->usrflags & 0x1) && a_int(nc->attributes[8]) == 2)
        timer.crumbling = true;
}

bool hero_overlaps(int x, int y, int w, int h)
{
    int lx = int(Link.getX()), ly = int(Link.getY());
    return lx + 15 >= x && lx < x + w && ly + 15 >= y && ly < y + h;
}

bool handle_crumble(const Q30Combo *c, PosInfo &timer, int &cid, int x, int y, int w, int h)
{
    bool breaking = false;
    int ty = a_int(c->attributes[8]);

    if(ty == 2 && timer.crumbling)
        breaking = true;

    if(!breaking)
    {
        int sens_offset = a_int(c->attributes[9]);

        if(!sens_offset || (sens_offset * 2 < w && sens_offset * 2 < h))
        {
            if(sens_offset)
            {
                x += sens_offset;
                y += sens_offset;
                w -= sens_offset * 2;
                h -= sens_offset * 2;
            }

            breaking = hero_overlaps(x, y, w, h);
        }
    }

    if(breaking)
    {
        if(c->attributes[16] < 10000)
        {
            trigger_crumble(c, timer, cid);
            return true;
        }

        if(timer.type_clk)
        {
            if(!--timer.type_clk)
            {
                trigger_crumble(c, timer, cid);
                return true;
            }
        }
        else
            timer.type_clk = (word)a_int(c->attributes[16]);
    }
    else if(ty == 0 && timer.crumbling)
    {
        timer.type_clk = 0;
        int diff = a_int(c->attributes[17]);

        if(diff)
        {
            cid += diff;
            timer.data = cid;
        }
    }

    timer.crumbling = breaking;
    return false;
}

// ---- per-frame update -------------------------------------------------------------

int g_trig_groups[256];

void update_timers_for(const H &h, PosInfo &timer, const Q30Combo *c)
{
    int cid = h_data(h);
    bool do_trigger_timer = true;
    size_t max_trig = zc_max(c->triggers.size(), timer.trig.size());

    for(size_t idx = 0; idx < max_trig; ++idx)
    {
        if(do_trigger_timer && idx < c->triggers.size())
        {
            const Q30Trigger &t = c->triggers[idx];

            if(t.trigtimer)
            {
                TrigInfo &ti = timer.t(idx);

                if(++ti.clk >= t.trigtimer)
                {
                    ti.clk = 0;

                    if(do_trigger(h, idx) && t.flags.get(Q30TF_CANCEL_TRIGGER))
                        break;

                    timer.data = h_data(h);

                    if(h_data(h) != cid)
                    {
                        do_trigger_timer = false;
                    }
                }
            }
        }

        if(idx < timer.trig.size() && timer.trig[idx].cooldown)
            --timer.trig[idx].cooldown;
    }
}

void update_position(const H &h, void *)
{
    int cid = h_data(h);
    PosInfo &timer = h_info(h);

    if(timer.data != cid)
    {
        // new combo at this position
        const Q30Combo *oldc = q30_combo(timer.data);
        word sfx_onchange = oldc ? oldc->sfx_disappear : 0;
        word spr_onchange = oldc ? oldc->spr_disappear : 0;
        timer.clear();
        timer.data = cid;
        timer.appeared = false;
        timer.sfx_onchange = sfx_onchange;
        timer.spr_onchange = spr_onchange;
    }

    const Q30Combo *c = q30_combo(cid);

    if(!timer.appeared)
    {
        timer.appeared = true;

        if(c && c->sfx_appear)
            sfx(c->sfx_appear);

        if(timer.sfx_onchange)
            sfx(timer.sfx_onchange);

        timer.sfx_onchange = timer.spr_onchange = 0;
    }

    if(!c)
        return;

    update_timers_for(h, timer, c);

    if(h.is_ffc())
    {
        if(c->type == Q30C_SHOOTER)
        {
            int wx, wy;
            h_xy(h, wx, wy);
            wx += ((tmpscr[0].ffwidth[h.ffc] >> 6) * 8);
            wy += ((tmpscr[0].ffheight[h.ffc] >> 6) * 8);
            handle_shooter(c, timer, wx, wy);
        }

        return;
    }

    switch(c->type)
    {
    case Q30C_SHOOTER:
    {
        int x, y;
        h_xy(h, x, y);
        handle_shooter(c, timer, x, y);
        break;
    }

    case Q30C_CRUMBLE:
    {
        int x, y;
        h_xy(h, x, y);
        int newcid = cid;

        if(handle_crumble(c, timer, newcid, x, y, 16, 16) || newcid != cid)
            h_set_data(h, newcid);

        break;
    }
    }
}

bool g_loaded_pending = false;

} // namespace

// ---- bridges ----------------------------------------------------------------------

enum { qr30_NEW_DARKROOM = 237, qr30_BRIDGES_ABOVE_2 = 594, qr30_OLD_BRIDGE_COMBOS = 313,
       qr30_WATER_ON_LAYER_1 = 220, qr30_WATER_ON_LAYER_2 = 221 };

static int walk_byte(int cid)
{
    const Q30Combo *c = q30_combo(cid);
    return c ? c->walk : (combobuf[cid].walk | 0xF0);
}

bool q30_has_bridge_at(int x, int y, int layer)
{
    if(!q30_active() || layer < 1 || layer > 6 || x < 0 || y < 0 || x > 255 || y > 175)
        return false;

    if(layer > 2 && !rule(qr30_BRIDGES_ABOVE_2))
        return false;

    H h(layer, ((y >> 4) << 4) + (x >> 4));

    if(!h_scr(h))
        return false;

    int cid = h_data(h);

    if(q30_combo_type(cid) != Q30C_BRIDGE)
        return false;

    int b = ((x & 8) ? 4 : 1) << ((y & 8) ? 1 : 0);
    int w = walk_byte(cid);
    return rule(qr30_OLD_BRIDGE_COMBOS) ? (w & b) != 0 : ((w >> 4) & b) != 0;
}

bool q30_has_bridge_above(int x, int y, int target_layer)
{
    if(!q30_active())
        return false;

    int maxlayer = rule(qr30_BRIDGES_ABOVE_2) ? 6 : 2;

    for(int lyr = maxlayer; lyr > target_layer; --lyr)
        if(q30_has_bridge_at(x, y, lyr))
            return true;

    return false;
}

// Link's view of the screen's combo: nothing where a bridge covers it.
int q30_link_mapcombo(int x, int y)
{
    if(q30_active() && q30_has_bridge_above(x, y, 0))
        return 0;

    return MAPCOMBO(x, y);
}

static bool cell_solid(int pos, int b)
{
    const mapscr *s0 = &tmpscr[0];
    const mapscr *s1 = (s0->layermap[0] > 0 && !tmpscr2[0].data.empty()) ? &tmpscr2[0] : s0;
    const mapscr *s2 = (s0->layermap[1] > 0 && !tmpscr2[1].data.empty()) ? &tmpscr2[1] : s0;
    int c0 = s0->data[pos], c1 = s1->data[pos], c2 = s2->data[pos];
    int t0 = q30_combo_type(c0), t1 = q30_combo_type(c1), t2 = q30_combo_type(c2);
    int w0 = walk_byte(c0), w1 = walk_byte(c1), w2 = walk_byte(c2);
    const Q30Combo *x0 = q30_combo(c0), *x1 = q30_combo(c1), *x2 = q30_combo(c2);
    dword u0 = x0 ? x0->usrflags : 0, u1 = x1 ? x1->usrflags : 0, u2 = x2 ? x2->usrflags : 0;
    bool water0 = t0 < cMAX && iswater_type(t0), water1 = t1 < cMAX && iswater_type(t1), water2 = t2 < cMAX && iswater_type(t2);
    bool dried = (water0 || water1 || water2) && DRIEDLAKE;
    bool old_bridge = rule(qr30_OLD_BRIDGE_COMBOS);
    int cwalk = w0;

    if((t0 == Q30C_BRIDGE && old_bridge) || (water0 && ((w0 >> 4) & b) && (u0 & 0xC)))
        cwalk = 0;

    for(int l = 1; l <= 2; ++l)
    {
        const mapscr *sl = l == 1 ? s1 : s2;

        if(sl == s0)
            continue;

        int t = l == 1 ? t1 : t2, w = l == 1 ? w1 : w2;
        dword u = l == 1 ? u1 : u2;
        bool water = l == 1 ? water1 : water2;
        bool waterrule = rule(l == 1 ? qr30_WATER_ON_LAYER_1 : qr30_WATER_ON_LAYER_2);

        if(water && ((w >> 4) & b) && waterrule && !(u & 0xC))
            cwalk &= w;
        else if(t == Q30C_BRIDGE)
        {
            if(!old_bridge)
            {
                int efflag = (w & 0xF0) >> 4, newsolid = w & 0xF;
                cwalk = ((newsolid | cwalk) & (~efflag)) | (newsolid & efflag);
            }
            else
                cwalk &= w;
        }
        else if(water && waterrule && (u & 0xC) && ((w >> 4) & b))
            cwalk = 0;
        else
            cwalk |= w;
    }

    if(rule(qr30_BRIDGES_ABOVE_2))
    {
        for(int l = 3; l <= 6; ++l)
        {
            const mapscr *sl = &tmpscr2[l - 1];

            if(!sl->valid || sl->data.empty())
                continue;

            if(q30_combo_type(sl->data[pos]) == Q30C_BRIDGE)
            {
                int w = walk_byte(sl->data[pos]);

                if(!old_bridge)
                    w >>= 4;

                if(w & b)
                    return false;
            }
        }
    }

    return (cwalk & b) && !dried;
}

// _walkflag's two-cell test, with bridges and solid water.
bool q30_walkflag(int x, int y, int cnt)
{
    if(cnt == 0 && (tmpscr[0].flags7 & fSIDEVIEW) && q30_sideview_platform_at(x, y))
        return true;

int bx = (x >> 4) + (y & 0xF0);
    int b = 1;

    if(x & 8) b <<= 2;

    if(y & 8) b <<= 1;

    if(bx < 0 || bx > 175)
        return false;

    if(cell_solid(bx, b))
        return true;

    if(cnt == 1)
        return false;

    if(!(x & 8))
        return cell_solid(bx, b << 2);

    ++bx;
    return bx <= 175 && cell_solid(bx, (y & 8) ? 2 : 1);
}

// ---- walk speed -------------------------------------------------------------------

void q30_walk_speed(int x, int y, int z, int &dx, int &dy)
{
    if(!q30_active() || (z > 0 && !(tmpscr[0].flags2 & fAIRCOMBOS)))
        return;

    int px = x + 7, py = y + 8;

    if(px < 0 || py < 0 || px > 255 || py > 175)
        return;

    int pos = ((py >> 4) << 4) + (px >> 4);
    double mult = 1, add = 0;
    bool any = false;

    for(int q = 6; q >= 0; --q)
    {
        H h(q, pos);

        if(!h_scr(h))
            continue;

        const Q30Combo *c = h_combo(h);

        if(c && (c->speed_mult != 1 || c->speed_div > 1 || c->speed_add))
        {
            mult *= c->speed_mult;

            if(c->speed_div)
                mult /= c->speed_div;

            add += c->speed_add / 10000.0;
            any = true;
        }

        if(q > 0 && q30_has_bridge_at(px, py, q))
            break;
    }

    static double fx = 0, fy = 0;

    if(!any)
    {
        fx = fy = 0;
        return;
    }

    int *axes[2] = {&dx, &dy};
    double *fr[2] = {&fx, &fy};

    for(int a = 0; a < 2; ++a)
    {
        int v = *axes[a];

        if(!v)
        {
            *fr[a] = 0;
            continue;
        }

        double m = (v < 0 ? -v : v) * mult + add + *fr[a];

        if(m < 0)
            m = 0;
        else if(m > 3)
            m = 3;              // Link's solidity checks assume small steps

        int whole = (int)m;
        *fr[a] = m - whole;
        *axes[a] = v < 0 ? -whole : whole;
    }
}

bool q30_respawn_forced() { return q30_active() && g_respawn_forced; }
bool q30_lift_busy();
bool q30_link_stunned() { return q30_active() && (g_stun_clk > 0 || q30_lift_busy()); }

void q30_tint(int r, int g, int b);
void q30_tint_clear();
void q30_set_gravity(long gravity, long terminal);
void q30_revert_gravity();
static void reset_screen_effects();

// ---- exported state ---------------------------------------------------------------

bool q30_get_exstate(int mi, int bit)
{
    states_setup();
    return (g_xstates[mi] & (1u << bit)) != 0;
}

void q30_set_exstate(int mi, int bit, bool on)
{
    states_setup();

    if(on)
        (states_mark(), g_xstates[mi]) |= (1u << bit);
    else
        (states_mark(), g_xstates[mi]) &= ~(1u << bit);
}

bool q30_get_xdoor(int mi, int dir, int ind)
{
    states_setup();
    return (g_xdoors[mi * 4 + dir] & (1 << ind)) != 0;
}

void q30_set_xdoor(int mi, int dir, int ind, bool on)
{
    states_setup();

    if(on)
        (states_mark(), g_xdoors[mi * 4 + dir]) |= (byte)(1 << ind);
    else
        (states_mark(), g_xdoors[mi * 4 + dir]) &= (byte)~(1 << ind);
}

byte &q30_pos_state(int mi, int key)
{
    std::vector<byte> &v = g_posstates[mi];

    if(v.empty())
        v.resize(7 * 176 + 1 + 32 + 1000, 0);

    return v[key];
}

// ---- the game's calls -------------------------------------------------------------

bool q30_active()
{
    return quest_header_zelda_version >= 0x255;
}

static bool cond_screenload(const Q30Trigger &t, size_t, void *)
{
    return t.flags.get(Q30TF_SCREENLOAD);
}

static bool cond_screenunload(const Q30Trigger &t, size_t, void *)
{
    return t.flags.get(Q30TF_SCREENUNLOAD);
}

static void screen_state_on_load(const H &h, void *)
{
    const Q30Combo *c = h_combo(h);

    if(!c)
        return;

    // exstates, ex doors, combo position states already set: apply the combos' effects now
    for(size_t q = 0; q < c->triggers.size(); ++q)
    {
        if(force_ex_trigger(h, q) || force_ex_door_trigger(h, q) || force_combopos_trigger(h, q))
        {
            c = h_combo(h);

            if(!c)
                break;
        }
    }
}

static void screen_trig_visit(const H &h, void *ctx)
{
    TrigCond cond = ctx ? cond_screenload : cond_screenunload;
    int cid = h_data(h);
    int guard = 0;

    do
    {
        execute(h, cond, NULL);

        if(h_data(h) == cid)
            break;

        cid = h_data(h);
    }
    while(++guard < 32);
}

void q30_trig_screen_load()
{
    if(!q30_active() || tmpscr[0].data.empty())
        return;

    if(g_scripts_pending)
    {
        g_scripts_pending = false;

        for(int i = 0; i < MAXCOMBOS; ++i)
            if(g_combos[i] && g_combos[i]->script)
                q30_set_combo_script(i, g_combos[i]->script, g_combos[i]->initd);
    }

    pos_setup();
    states_setup();
    states_sync();
    g_respawn_forced = false;
    reset_screen_effects();

    for(size_t i = 0; i < g_pos.size(); ++i)
        g_pos[i].clear();

    for(int i = 0; i < MAXFFCS; ++i)
        g_ffcpos[i].clear();

    g_wpn_done.clear();
    g_wpn_burn.clear();
    g_large_active.clear();
    g_pending_warp_type = -1;
    g_stun_clk = 0;
    g_pending_clear = 0;

    // the level state that was on when the player left: apply all its combos
    toggle_switches(g_lvlswitches[cur_dlevel()], true);

    bool states[256];

    for(int q = 0; q < 256; ++q)
        states[q] = g_gswitch_timers[q] != 0;

    toggle_gswitches(states, true);

    for_all_positions(screen_state_on_load, NULL);

    // Timers start from the combos now on the screen, so loading doesn't count as a change
    for(int lyr = 0; lyr < POSLAYERS; ++lyr)
        for(int pos = 0; pos < 176; ++pos)
            g_pos[lyr * 176 + pos].data = h_data(H(lyr, pos));

    for(int i = 0; i < MAXFFCS; ++i)
        g_ffcpos[i].data = tmpscr[0].ffdata[i];

    for_all_positions(screen_trig_visit, (void *)1);

}

void q30_trig_screen_unload()
{
    if(!q30_active())
        return;

    for_all_positions(screen_trig_visit, NULL);
}

static void compute_trig_groups(const H &h, void *)
{
    const Q30Combo *c = h_combo(h);

    if(!c)
        return;

    for(size_t q = 0; q < c->triggers.size(); ++q)
    {
        const Q30Trigger &t = c->triggers[q];

        if(t.flags.get(Q30TF_TGROUP_CONTRIB))
            ++g_trig_groups[t.trig_group];
    }
}

static bool cond_group(const Q30Trigger &t, size_t, void *)
{
    bool ok = false;

    if(t.flags.get(Q30TF_TGROUP_LESS))
        ok = g_trig_groups[t.trig_group] < t.trig_group_val;

    if(!ok && t.flags.get(Q30TF_TGROUP_GREATER))
        ok = g_trig_groups[t.trig_group] > t.trig_group_val;

    return ok;
}

static void trig_group_visit(const H &h, void *)
{
    if(h.is_ffc() && (tmpscr[0].ffflags[h.ffc] & ffCHANGER))
        return;

    int cid = h_data(h);
    int guard = 0;
    bool recheck;
    std::vector<int> visited;
    visited.push_back(cid);

    do
    {
        recheck = false;
        const Q30Combo *c = q30_combo(cid);

        if(!c)
            break;

        bool cancelled = false;

        for(size_t idx = 0; idx < c->triggers.size(); ++idx)
        {
            const Q30Trigger &t = c->triggers[idx];

            if(!cond_group(t, idx, NULL))
                continue;

            if(do_trigger(h, idx) && t.flags.get(Q30TF_CANCEL_TRIGGER))
                cancelled = true;

            int cid2 = h_data(h);
            h_info(h).data = cid2;

            if(cancelled)
                break;

            if(cid != cid2)
            {
                if(std::find(visited.begin(), visited.end(), cid2) != visited.end())
                    break;

                visited.push_back(cid2);
                cid = cid2;
                recheck = true;
                break;
            }
        }
    }
    while(recheck && ++guard < 64);
}

static void new_darkroom_palette();

void q30_trig_update()
{
    if(!q30_active() || tmpscr[0].data.empty())
        return;

    if(g_pending_clear)
    {
        int pending = g_pending_clear;
        g_pending_clear = 0;

        if(pending & 1)
            kill_em_all();

        if(pending & 2)
            guys.clear();

        if(pending & 4)
            Lwpns.clear();

        if(pending & 8)
            Ewpns.clear();
    }

    new_darkroom_palette();
    q30_switchhook_update();
    pos_setup();
    for_all_positions(update_position, NULL);

    // gswitch timers
    {
        bool states[256];
        bool any = false;
        memset(states, 0, sizeof(states));

        for(int q = 0; q < 256; ++q)
        {
            if(g_gswitch_timers[q] > 0 && !--g_gswitch_timers[q])
            {
                states[q] = true;
                any = true;
            }
        }

        if(any)
            toggle_gswitches(states, false);
    }

    states_flush();
    memset(g_trig_groups, 0, sizeof(g_trig_groups));
    for_all_positions(compute_trig_groups, NULL);
for_all_positions(trig_group_visit, NULL);

    // looping sfx of the combos on screen
    {
        static std::map<word, bool> running;
        std::map<word, bool> present;

        for(int lyr = 0; lyr < POSLAYERS; ++lyr)
        {
            mapscr *scr = lyr ? &tmpscr2[lyr - 1] : &tmpscr[0];

            if(lyr && (!scr->valid || scr->data.empty()))
                continue;

            for(int pos = 0; pos < 176; ++pos)
            {
                const Q30Combo *c = q30_combo(scr->data[pos]);

                if(c && c->sfx_loop)
                    present[c->sfx_loop] = true;
            }
        }

        for(std::map<word, bool>::iterator it = present.begin(); it != present.end(); ++it)
            if(!running.count(it->first))
            {
                cont_sfx(it->first);
                running[it->first] = true;
            }

        for(std::map<word, bool>::iterator it = running.begin(); it != running.end();)
        {
            if(!present.count(it->first))
            {
                stop_sfx(it->first);
                running.erase(it++);
            }
            else
                ++it;
        }
    }
}

static bool cond_secrets(const Q30Trigger &t, size_t, void *)
{
    return t.flags.get(Q30TF_SECRETSTR);
}

static void secrets_visit(const H &h, void *)
{
    execute(h, cond_secrets, NULL, ctrigSECRETS);
}

void q30_trig_secrets()
{
    if(!q30_active())
        return;

    for_all_positions(secrets_visit, NULL);
}

static void shutter_visit(const H &h, void *)
{
    execute(h, cond_flag, (void *)(intptr_t)Q30TF_SHUTTER);
}

void q30_trig_shutters()
{
    if(!q30_active())
        return;

    for_all_positions(shutter_visit, NULL);
}

static void enemies_visit(const H &h, void *)
{
    execute(h, cond_flag, (void *)(intptr_t)Q30TF_ENEMIESKILLED);
}

void q30_trig_enemies_killed()
{
    if(!q30_active())
        return;

    for_all_positions(enemies_visit, NULL);
}

void q30_trig_level_state(dword changed)
{
    if(q30_active())
        toggle_switches(changed, false);
}

void q30_trig_global_state(int state)
{
    if(q30_active() && state >= 0 && state < 256)
        toggle_gswitch(state, false);
}

struct LensCtx { bool on; };

static void lens_visit(const H &h, void *ctx)
{
    execute(h, cond_flag, (void *)(intptr_t)(*(bool *)ctx ? Q30TF_LENSON : Q30TF_LENSOFF));
}

void q30_trig_lens(bool on)
{
    if(!q30_active())
        return;

    static int last = -1;

    if(last == (on ? 1 : 0))
        return;

    last = on ? 1 : 0;
    for_all_positions(lens_visit, &on);
}

struct QuakeCtx { int x, y, rad, level; bool super; };

static bool cond_quake(const Q30Trigger &t, size_t, void *ctx)
{
    QuakeCtx *q = (QuakeCtx *)ctx;

    if(!t.flags.get(Q30TF_QUAKESTUN) && !(q->super && t.flags.get(Q30TF_SQUAKESTUN)))
        return false;

    return t.flags.get(Q30TF_INVERTMINMAX) ? (q->level <= t.triggerlevel) : (q->level >= t.triggerlevel);
}

static void quake_visit(const H &h, void *ctx)
{
    QuakeCtx *q = (QuakeCtx *)ctx;
    int cx, cy;
    h_xy(h, cx, cy);
    double dx = q->x - cx, dy = q->y - cy;

    if(sqrt(dx * dx + dy * dy) <= q->rad)
        execute(h, cond_quake, ctx);
}

void q30_trig_quake(int x, int y, int rad, int level, bool super)
{
    if(!q30_active())
        return;

    QuakeCtx q = {x, y, rad, level < 1 ? 1 : level, super};
    for_all_positions(quake_visit, &q);
}

// ---- weapons --------------------------------------------------------------------

struct WeaponMatch
{
    weapon *w;
};

static bool match_weapon(weapon *w, const Q30Trigger &t)
{
    int wid = w->id;

    if(int(w->z) > 0 && t.flags.get(Q30TF_ONLY_GROUND_WPN))
        return false;

    if(wid < wEnemyWeapons)  // lweapon level check
    {
        int lv = w->type, tlv = t.triggerlevel;

        if(lv < 1)
            lv = 1;

        if(tlv < 1)
            tlv = 1;

        if(!(t.flags.get(Q30TF_INVERTMINMAX) ? lv <= tlv : lv >= tlv))
            return false;
    }

    int f = -1;

    switch(wid)
    {
    case wSword: f = Q30TF_SWORD; break;
    case wBeam: f = Q30TF_SWORDBEAM; break;
    case wBrang: f = Q30TF_BRANG; break;
    case wBomb: f = Q30TF_BOMB; break;
    case wSBomb: f = Q30TF_SBOMB; break;
    case wLitBomb: f = Q30TF_LITBOMB; break;
    case wLitSBomb: f = Q30TF_LITSBOMB; break;
    case wArrow: f = Q30TF_ARROW; break;
    case wFire: f = Q30TF_FIRE; break;
    case wWhistle: f = Q30TF_WHISTLE; break;
    case wBait: f = Q30TF_BAIT; break;
    case wWand: f = Q30TF_WAND; break;
    case wMagic: f = Q30TF_MAGIC; break;
    case wWind: f = Q30TF_WIND; break;
    case wRefMagic: f = Q30TF_REFMAGIC; break;
    case wRefFireball: f = Q30TF_REFFIREBALL; break;
    case wRefRock: f = Q30TF_REFROCK; break;
    case wHammer: f = Q30TF_HAMMER; break;
    case wHookshot: f = Q30TF_HOOKSHOT; break;
    case wFSparkle: case wSSparkle: f = Q30TF_SPARKLE; break;
    case wCByrna: f = Q30TF_BYRNA; break;
    case wRefBeam: f = Q30TF_REFBEAM; break;
    case wStomp: f = Q30TF_STOMP; break;
    case Q30W_THROWN: f = Q30TF_THROWN; break;

    case wScript1: case wScript2: case wScript3: case wScript4: case wScript5:
    case wScript6: case wScript7: case wScript8: case wScript9: case wScript10:
        f = (wid < wEnemyWeapons ? Q30TF_SCRIPT01 : Q30TF_EWSCRIPT01) + (wid - wScript1);
        break;

    case ewFireball: case ewFireball2: f = Q30TF_EWFIREBALL; break;
    case ewArrow: f = Q30TF_EWARROW; break;
    case ewBrang: f = Q30TF_EWBRANG; break;
    case ewSword: f = Q30TF_EWSWORD; break;
    case ewRock: f = Q30TF_EWROCK; break;
    case ewMagic: f = Q30TF_EWMAGIC; break;
    case ewBomb: f = Q30TF_EWBBLAST; break;
    case ewSBomb: f = Q30TF_EWSBBLAST; break;
    case ewLitBomb: f = Q30TF_EWLITBOMB; break;
    case ewLitSBomb: f = Q30TF_EWLITSBOMB; break;
    case ewFireTrail: f = Q30TF_EWFIRETRAIL; break;
    case ewFlame: f = Q30TF_EWFLAME; break;
    case ewWind: f = Q30TF_EWWIND; break;
    case ewFlame2: f = Q30TF_EWFLAME2; break;

    default:
        return false;
    }

    return f > -1 && t.flags.get(f);
}

static void match_weapon_at(weapon *w, int bx, int by, int layer)
{
    if((unsigned)bx > 255 || (unsigned)by > 175)
        return;

    if(!layer && !rule(qr30_OLD_FFC_FUNCTIONALITY))
    {
        for(int i = 0; i < MAXFFCS; ++i)
        {
            if(!tmpscr[0].ffdata[i] || !ffc_at(i, bx, by))
                continue;

            H h(i);
            int cid = h_data(h);
            const Q30Combo *c = q30_combo(cid);

            for(size_t idx = 0; c && idx < c->triggers.size(); ++idx)
            {
                if(!match_weapon(w, c->triggers[idx]))
                    continue;

                if(do_trigger(h, idx, 0, w) && c->triggers[idx].flags.get(Q30TF_CANCEL_TRIGGER))
                    break;

                if(h_data(h) != cid)
                    break;
            }
        }
    }

    H h(layer, ((by >> 4) << 4) + (bx >> 4));

    if(!h_scr(h))
        return;

    int cid = h_data(h);
    const Q30Combo *c = q30_combo(cid);

    if(!c)
        return;

    for(size_t idx = 0; idx < c->triggers.size(); ++idx)
    {
        if(!match_weapon(w, c->triggers[idx]))
            continue;

        if(do_trigger(h, idx, 0, w) && c->triggers[idx].flags.get(Q30TF_CANCEL_TRIGGER))
            break;

        if(h_data(h) != cid)
            break;
    }
}

void q30_trig_weapon(weapon *w)
{
    if(q30_active())
        q30_thrown_frame(w);

if(!q30_active() || screenscrolling || (w->dead != -1 && w->dead != 0 && w->id != wBomb && w->id != wSBomb && w->id != ewBomb && w->id != ewSBomb))
        return;

    switch(w->id)
    {
    case wSword: case wBeam: case wBrang: case wBomb: case wSBomb: case wLitBomb: case wLitSBomb:
    case wArrow: case wFire: case wWhistle: case wBait: case wWand: case wMagic: case wWind:
    case wRefMagic: case wRefFireball: case wRefRock: case wHammer: case wHookshot:
    case wFSparkle: case wSSparkle: case wCByrna: case wRefBeam: case wStomp:
    case wScript1: case wScript2: case wScript3: case wScript4: case wScript5:
    case wScript6: case wScript7: case wScript8: case wScript9: case wScript10:
    case ewFireball: case ewFireball2: case ewArrow: case ewBrang: case ewSword: case ewRock:
    case ewMagic: case ewBomb: case ewSBomb: case ewLitBomb: case ewLitSBomb: case ewFireTrail:
    case ewFlame: case ewWind: case ewFlame2: case Q30W_THROWN:
        break;

    default:
        return;
    }

    int layercount= rule(qr30_CUSTOMCOMBOS_EVERY_LAYER) ? 7 : (rule(qr30_CUSTOMCOMBOSLAYERS1AND2) ? 3 : 1);
    int wx = int(w->x), wy = int(w->y);

    if(!g_wpn_burn.empty())
    {
        std::map<long, int>::iterator burn = g_wpn_burn.find(w->getUID());

        if(burn != g_wpn_burn.end() && burn->second)
        {
            if(burn->second & 1) findentrance(wx, wy, mfBCANDLE, true);
            if(burn->second & 2) findentrance(wx, wy, mfRCANDLE, true);
            if(burn->second & 4) findentrance(wx, wy, mfWANDFIRE, true);
            if(burn->second & 8) findentrance(wx, wy, mfDINSFIRE, true);
            q30_trig_fire(w, wx, wy, burn->second);
        }
    }
int hw = zc_max(1, w->hxsz), hh = zc_max(1, w->hysz);

    if(w->id == wBomb || w->id == wSBomb || w->id == ewBomb || w->id == ewSBomb)
    {
        if(w->clk < w->misc - 1)
            return;

        // the blast covers the combos around the bomb
        for(int dx = -16; dx <= 16; dx += 16)
            for(int dy = -16; dy <= 16; dy += 16)
                for(int ly = 0; ly < layercount; ++ly)
                    match_weapon_at(w, wx + 8 + dx, wy + 8 + dy, ly);

        return;
    }

    for(int dx = 0; dx < hw; dx += 16)
    {
        for(int dy = 0; dy < hh; dy += 16)
            for(int ly = 0; ly < layercount; ++ly)
                match_weapon_at(w, wx + dx + w->hxofs, wy + dy + w->hyofs, ly);

        for(int ly = 0; ly < layercount; ++ly)
            match_weapon_at(w, wx + dx + w->hxofs, wy + w->hyofs + (hh - 1), ly);
    }

    for(int dy = 0; dy < hh; dy += 16)
        for(int ly = 0; ly < layercount; ++ly)
            match_weapon_at(w, wx + w->hxofs + (hw - 1), wy + dy + w->hyofs, ly);

    for(int ly = 0; ly < layercount; ++ly)
        match_weapon_at(w, wx + w->hxofs + (hw - 1), wy + w->hyofs + (hh - 1), ly);
}

void q30_trig_fire(weapon *w, int x, int y, int levels)
{
    if(!q30_active() || !levels)
        return;

    int flags[4];
    int n = 0;

    if(levels & 1) flags[n++] = Q30TF_ANYFIRE;
    if(levels & 2) flags[n++] = Q30TF_STRONGFIRE;
    if(levels & 4) flags[n++] = Q30TF_MAGICFIRE;
    if(levels & 8) flags[n++] = Q30TF_DIVINEFIRE;

    int positions[4][2] = {{x, y}, {x, y + 15}, {x + 15, y}, {x + 15, y + 15}};

    for(int q = 0; q < 7; ++q)
    {
        int visited = 0;

        for(int p = 0; p < 4; ++p)
        {
            int px = positions[p][0], py = positions[p][1];

            if(px < 0 || py < 0 || px > 255 || py > 175)
                continue;

            int rpos = ((py >> 4) << 4) + (px >> 4);

            if(visited & (1 << (rpos % 32)))
                continue;

            visited |= (1 << (rpos % 32));
            H h(q, rpos);

            if(!h_scr(h))
                continue;

            int cid = h_data(h);
            const Q30Combo *c = q30_combo(cid);

            if(!c)
                continue;

            for(size_t idx = 0; idx < c->triggers.size(); ++idx)
            {
                const Q30Trigger &t = c->triggers[idx];
                bool match = false;

                for(int f = 0; f < n; ++f)
                    if(t.flags.get(flags[f]))
                        match = true;

                if(!match || (int(w->z) > 0 && t.flags.get(Q30TF_ONLY_GROUND_WPN)))
                    continue;

                if(do_trigger(h, idx, 0, w) && t.flags.get(Q30TF_CANCEL_TRIGGER))
                    break;

                if(h_data(h) != cid)
                    break;
            }
        }
    }

    for(int i = 0; i < MAXFFCS; ++i)
    {
        if(!tmpscr[0].ffdata[i])
            continue;

        int fx = (int)(tmpscr[0].ffx[i] / 10000), fy = (int)(tmpscr[0].ffy[i] / 10000);

        if(fx > x + 15 || fx + 15 < x || fy > y + 15 || fy + 15 < y)
            continue;

        H h(i);
        const Q30Combo *c = h_combo(h);

        if(!c)
            continue;

        for(size_t idx = 0; idx < c->triggers.size(); ++idx)
        {
            bool match = false;

            for(int f = 0; f < n; ++f)
                if(c->triggers[idx].flags.get(flags[f]))
                    match = true;

            if(match)
                do_trigger(h, idx, 0, w);
        }
    }
}

// ---- Link's frame -------------------------------------------------------------------

static bool cond_auto(const Q30Trigger &t, size_t, void *)
{
    return t.flags.get(Q30TF_AUTOMATIC);
}

static bool cond_step_nonsens(const Q30Trigger &t, size_t, void *ctx)
{
    bool swim = *(bool *)ctx;
    return t.flags.get(Q30TF_STEP) || (swim && t.flags.get(Q30TF_SWIMTRIG));
}

static bool cond_step_sens(const Q30Trigger &t, size_t, void *ctx)
{
    bool swim = *(bool *)ctx;
    return t.flags.get(Q30TF_STEPSENS) || (swim && t.flags.get(Q30TF_SWIMSENSTRIG));
}

static bool cond_step_any(const Q30Trigger &t, size_t, void *ctx)
{
    bool swim = *(bool *)ctx;
    return t.flags.get(Q30TF_STEP) || t.flags.get(Q30TF_STEPSENS)
        || (swim && (t.flags.get(Q30TF_SWIMTRIG) || t.flags.get(Q30TF_SWIMSENSTRIG)));
}

static bool has_step_trigger(const Q30Combo *c)
{
    for(size_t q = 0; c && q < c->triggers.size(); ++q)
    {
        const Q30Trigger &t = c->triggers[q];

        if(t.flags.get(Q30TF_STEP) || t.flags.get(Q30TF_STEPSENS) || t.flags.get(Q30TF_SWIMTRIG) || t.flags.get(Q30TF_SWIMSENSTRIG))
            return true;
    }

    return false;
}

static void check_pitfall();
void q30_light_update();
static void lift_frame();
static void new_darkroom_palette();

void q30_trig_link_frame()
{
    if(!q30_active())
        return;

    if(g_stun_clk > 0)
        --g_stun_clk;

    q30_light_update();
    lift_frame();

if(g_pending_warp_type >= 0)
    {
        int type = g_pending_warp_type, index = g_pending_warp_index;
        g_pending_warp_type = -1;
        Link.dowarp(type, index);
        return;
    }

    int x = int(Link.getX()), y = int(Link.getY());

    // automatic triggers
    {
        int maxlyr = rule(qr30_AUTOCOMBO_ANY_LAYER) ? 6 : 2;

        for(int lyr = 0; lyr <= maxlyr; ++lyr)
        {
            if(!rule(qr30_AUTOCOMBO_ANY_LAYER))
            {
                if(lyr == 1 && !rule(qr30_AUTOCOMBO_LAYER_1))
                    continue;

                if(lyr == 2 && !rule(qr30_AUTOCOMBO_LAYER_2))
                    continue;
            }

            mapscr *scr = lyr ? &tmpscr2[lyr - 1] : &tmpscr[0];

            if(lyr && (!scr->valid || scr->data.empty()))
                continue;

            for(int pos = 0; pos < 176; ++pos)
                if(q30_combo(scr->data[pos]))
                    execute(H(lyr, pos), cond_auto, NULL);
        }
    }

    // step triggers
    if(Link.getAction() != freeze && !msgstr_blocking_step())
    {
        bool swim = Link.isSwimming();
        const int tx = x, ty = y;
        int step_rects[4][2] = {{tx + 4, ty + 4}, {tx + 11, ty + 4}, {tx + 4, ty + 11}, {tx + 11, ty + 11}};
        int sens_rects[4][2] = {{tx, ty + 8}, {tx + 15, ty + 8}, {tx, ty + 15}, {tx + 15, ty + 15}};
        bool z_clear = int(Link.getZ()) <= 0 || (tmpscr[0].flags2 & fAIRCOMBOS);

        if(z_clear)
        {
            bool found_nonstep = false;

            for(int s = 0; s < 4 && !found_nonstep; ++s)
            {
                int rpos = ((step_rects[s][1] >> 4) << 4) + (step_rects[s][0] >> 4);

                if(rpos < 0 || rpos > 175)
                    continue;

                bool found = false;

                for(int lyr = 6; lyr >= 0 && !found; --lyr)
                {
                    H h(lyr, rpos);

                    if(!h_scr(h))
                        continue;

                    found = has_step_trigger(h_combo(h));
                }

                if(!found)
                    found_nonstep = true;
            }

            bool done_rpos[176];
            memset(done_rpos, 0, sizeof(done_rpos));

            for(int s = 0; s < 4; ++s)
            {
                int rpos = ((step_rects[s][1] >> 4) << 4) + (step_rects[s][0] >> 4);

                if(rpos < 0 || rpos > 175 || done_rpos[rpos])
                    continue;

                done_rpos[rpos] = true;

                for(int lyr = 6; lyr >= 0; --lyr)
                {
                    H h(lyr, rpos);

                    if(h_scr(h) && !found_nonstep)
                        execute(h, cond_step_nonsens, &swim);
                }
            }

            memset(done_rpos, 0, sizeof(done_rpos));

            for(int s = 0; s < 4; ++s)
            {
                int rpos = ((sens_rects[s][1] >> 4) << 4) + (sens_rects[s][0] >> 4);

                if(rpos < 0 || rpos > 175 || done_rpos[rpos])
                    continue;

                done_rpos[rpos] = true;

                for(int lyr = 6; lyr >= 0; --lyr)
                {
                    H h(lyr, rpos);

                    if(h_scr(h))
                        execute(h, cond_step_sens, &swim);
                }
            }

            for(int i = 0; i < MAXFFCS; ++i)
            {
                if(!tmpscr[0].ffdata[i] || (tmpscr[0].ffflags[i] & ffCHANGER))
                    continue;

                bool found = false;

                for(int s = 0; s < 4 && !found; ++s)
                    found = ffc_at(i, step_rects[s][0], step_rects[s][1]);

                if(found)
                    execute(H(i), cond_step_any, &swim);
            }
        }
    }

    // dive triggers
    if(Link.isDiving())
    {
        int cpos = (((y + 8) >> 4) << 4) + ((x + 8) >> 4);

        for(int lyr = 6; lyr >= 0; --lyr)
        {
            if(cpos >= 0 && cpos < 176)
            {
                H h(lyr, cpos);

                if(h_scr(h))
                    execute(h, cond_flag, (void *)(intptr_t)Q30TF_DIVETRIG);
            }
        }

        int corners[4][2] = {{x, y + 8}, {x + 15, y + 8}, {x, y + 15}, {x + 15, y + 15}};

        for(int c = 0; c < 4; ++c)
        {
            int rpos = ((corners[c][1] >> 4) << 4) + (corners[c][0] >> 4);

            if(rpos < 0 || rpos > 175)
                continue;

            for(int lyr = 6; lyr >= 0; --lyr)
            {
                H h(lyr, rpos);

                if(h_scr(h))
                    execute(h, cond_flag, (void *)(intptr_t)Q30TF_DIVESENSTRIG);
            }
        }
    }

    check_pitfall();

    // stepped-on sfx combos, landmines
    {
        int rpos = (((y + 8) >> 4) << 4) + ((x + 8) >> 4);
        static int last_pos = -1;

        if(rpos != last_pos)
        {
            last_pos = rpos;

            if(rpos >= 0 && rpos < 176 && int(Link.getZ()) <= 0)
            {
                for(int lyr = 0; lyr < 7; ++lyr)
                {
                    H h(lyr, rpos);

                    if(!h_scr(h))
                        continue;

                    const Q30Combo *c = h_combo(h);

                    if(c && c->type == Q30C_STEPSFX)
                        trigger_stepfx(h, c, true);
                }
            }
        }
    }
}

// ---- pitfalls -------------------------------------------------------------------

static int pit_cid_at(int x, int y)
{
    int c = MAPFFCOMBO(x, y);

    if(c)
        return q30_combo_type(c) == Q30C_PITFALL ? c : 0;

    if(q30_has_bridge_above(x, y, 2))
        return 0;

    for(int lyr = 2; lyr >= 0; --lyr)
    {
        c = lyr ? MAPCOMBO2(lyr - 1, x, y) : MAPCOMBO(x, y);

        if(q30_combo_type(c) == Q30C_PITFALL)
            return c;

        if(lyr && q30_has_bridge_at(x, y, lyr))
            return 0;
    }

    return 0;
}

static void check_pitfall()
{
    int x = int(Link.getX()), y = int(Link.getY());
    int act = Link.getAction();

    if(int(Link.getZ()) > 0 || Link.getHoverClk() || act == rafting || act == drowning
       || Link.getLadderX() || Link.getLadderY() || msgstr)
        return;

    int x1 = x + 1, x2 = x + 14, y1 = y + 9, y2 = y + 14;

    if(!pit_cid_at(x1, y1) || !pit_cid_at(x1, y2) || !pit_cid_at(x2, y1) || !pit_cid_at(x2, y2))
        return;

    int cid = pit_cid_at(x + 8, y + 12);

    if(!cid)
        return;

    const Q30Combo *c = q30_combo(cid);

    if(!c)
        return;

    if(a_int(c->attributes[8]))
        sfx(a_int(c->attributes[8]), pan(x));

    int dmg = a_int(c->attributes[0]);
    bool dmg_perc = (c->usrflags & 0x4) != 0;
    bool warp = (c->usrflags & 0x1) != 0;
    int inv_timer = a_int(c->attributes[3]);

    if(!inv_timer && dmg > 0)
        inv_timer = 48;

    if(dmg)
    {
        int life = game->get_life();

        if(dmg_perc)
            life -= (zc_min(zc_max(dmg, -100), 100) * game->get_maxlife()) / 100;
        else
            life -= dmg;

        game->set_life(zc_max(0, zc_min(life, (int)game->get_maxlife())));
    }

    if(inv_timer > 0)
        Link.setHClk(inv_timer);

    // the combo's CType Causes-> triggers
    int cx = (x + 8) >> 4, cy = (y + 12) >> 4;

    for(int lyr = 2; lyr >= 0; --lyr)
    {
        H h(lyr, cy * 16 + cx);

        if(h_scr(h) && h_data(h) == cid)
        {
            execute(h, cond_flag, (void *)(intptr_t)Q30TF_CMBTYPECAUSES);
            break;
        }
    }

    if(warp)
        Link.dowarp(0, zc_max(0, zc_min(3, a_int(c->attributes[9]))));
    else
        Link.Drown();
}

static bool cond_land_here(const Q30Trigger &t, size_t, void *)
{
    return t.flags.get(Q30TF_PLAYERLANDHERE);
}

static bool cond_land_anywhere(const Q30Trigger &t, size_t, void *)
{
    return t.flags.get(Q30TF_PLAYERLANDANYWHERE);
}

static void land_anywhere_visit(const H &h, void *);

void q30_trig_land()
{
    if(!q30_active())
        return;

    int x = int(Link.getX()), y = int(Link.getY());
    int rposes[4] = {((y + 8) >> 4) * 16 + (x >> 4), ((y + 8) >> 4) * 16 + ((x + 15) >> 4),
                     ((y + 15) >> 4) * 16 + (x >> 4), ((y + 15) >> 4) * 16 + ((x + 15) >> 4)};

    for(int r = 0; r < 4; ++r)
    {
        bool dup = false;

        for(int q = 0; q < r; ++q)
            if(rposes[q] == rposes[r])
                dup = true;

        if(dup || rposes[r] < 0 || rposes[r] > 175)
            continue;

        for(int lyr = 0; lyr <= 6; ++lyr)
        {
            H h(lyr, rposes[r]);
            const Q30Combo *c = h_scr(h) ? h_combo(h) : NULL;

            if(!c)
                continue;

            if(c->sfx_landing)
                sfx(c->sfx_landing, pan(x));

            execute(h, cond_land_here, NULL);
        }
    }

    for_all_positions(land_anywhere_visit, NULL);
}

static void land_anywhere_visit(const H &h, void *)
{
    execute(h, cond_land_anywhere, NULL);
}

// ---- pushing, buttons and signs ----------------------------------------------------

static bool cond_push(const Q30Trigger &t, size_t, void *ctx)
{
    int pushing = *(int *)ctx;
    return t.flags.get(Q30TF_PUSH) && pushing && !(pushing % zc_max(1, (int)t.trig_pushtime));
}

// Two points just past Link's front, as upstream measures them.
static void push_points(int d, int x, int y, int &bx, int &by, int &bx2, int &by2)
{
    bool big = get_bit(quest_rules, qr_LTTPCOLLISION) != 0;

    switch(d)
    {
    case up:
        by = y + (big ? -2 : 6);
        by2 = by;
        bx = x + 4;
        bx2 = bx + 8;
        break;

    case down:
        by = y + 17;
        by2 = by;
        bx = x + 4;
        bx2 = bx + 8;
        break;

    case left:
        by = y + (big ? 0 : 8);
        by2 = y + 8;
        bx = x - 2;
        bx2 = x - 2;
        break;

    default:
        by = y + (big ? 0 : 8);
        by2 = y + 8;
        bx = x + 17;
        bx2 = x + 17;
        break;
    }
}

void q30_trig_push()
{
    if(!q30_active())
        return;

    int pushing = Link.getPushing();

    if(!pushing)
        return;

    int x = int(Link.getX()), y = int(Link.getY()), d = Link.getDir();
    int bx, by, bx2, by2;
    push_points(d, x, y, bx, by, bx2, by2);

    if(bx < 0 || bx2 < 0 || by < 0 || by2 < 0 || bx > 255 || bx2 > 255 || by > 175 || by2 > 175)
        return;

    int rpos1 = ((by >> 4) << 4) + (bx >> 4);
    int rpos2 = ((by2 >> 4) << 4) + (bx2 >> 4);

    for(int lyr = 0; lyr < 7; ++lyr)
    {
        H h(lyr, rpos1);

        if(h_scr(h))
            execute(h, cond_push, &pushing);
    }

    if(rpos1 != rpos2)
    {
        for(int lyr = 0; lyr < 7; ++lyr)
        {
            H h(lyr, rpos2);

            if(h_scr(h))
                execute(h, cond_push, &pushing);
        }
    }

    if(!rule(qr30_OLD_FFC_FUNCTIONALITY))
    {
        for(int i = 0; i < MAXFFCS; ++i)
        {
            if(tmpscr[0].ffdata[i] && (ffc_at(i, bx, by) || ffc_at(i, bx2, by2)))
                execute(H(i), cond_push, &pushing);
        }
    }
}

static bool btn_peek(byte mask)
{
    if((mask & 0x01) && cAbtn() && !Adown) return true;
    if((mask & 0x02) && cBbtn() && !Bdown) return true;
    if((mask & 0x04) && cLbtn() && !LBdown) return true;
    if((mask & 0x08) && cRbtn() && !RBdown) return true;
    if((mask & 0x10) && cEx1btn() && !Ex1down) return true;
    if((mask & 0x20) && cEx2btn() && !Ex2down) return true;
    if((mask & 0x40) && cEx3btn() && !Ex3down) return true;
    if((mask & 0x80) && cEx4btn() && !Ex4down) return true;
    return false;
}

static void btn_eat(byte mask)
{
    if(mask & 0x01) rAbtn();
    if(mask & 0x02) rBbtn();
    if(mask & 0x04) rLbtn();
    if(mask & 0x08) rRbtn();
    if(mask & 0x10) rEx1btn();
    if(mask & 0x20) rEx2btn();
    if(mask & 0x40) rEx3btn();
    if(mask & 0x80) rEx4btn();
}

static bool has_btn_trigger(const Q30Combo *c)
{
    for(size_t q = 0; c && q < c->triggers.size(); ++q)
        if(c->triggers[q].triggerbtn)
            return true;

    return false;
}

static bool combo_btn_target(const Q30Combo *c)
{
    return c && ((c->type == Q30C_SIGNPOST && !c->only_gentrig) || has_btn_trigger(c));
}

// The "effect" square of a combo at this pixel: whether the quadrant is set.
static bool effect_at(int x, int y, int layer)
{
    if(x < 0 || y < 0 || x > 255 || y > 175)
        return false;

    int pos = ((y >> 4) << 4) + (x >> 4);
    H h(layer, pos);
    const Q30Combo *c = h_combo(h);

    if(!c)
        return true;

    int quadrant = ((y & 8) ? 2 : 0) | ((x & 8) ? 1 : 0);
    return (c->walk >> 4) & (1 << quadrant) ? true : false;
}

int g_prompt_combo = 0, g_prompt_cset = 0, g_prompt_x = 0, g_prompt_y = 0;

struct BtnCtx
{
    int dir_flag;
    byte sign_input;
    PosInfo *cpos;
    bool found_trigger_dir, did_trigger;
    byte inputs_to_eat;
};

bool q30_trig_buttons(int d)
{
    if(!q30_active())
        return false;

    if(Link.getAction() == freeze || msgstr != 0)
        return false;

    int x = int(Link.getX()), y = int(Link.getY());
    g_prompt_combo = 0;

    if(int(Link.getZ()) > 0)
        return false;

    int bx, by, bx2, by2;
    push_points(d, x, y, bx, by, bx2, by2);

    int found = -1, found_lyr = 0, fx = -1, fy = -1, ffc = -1;

    if(bx >= 0 && by >= 0 && bx < 256 && by < 176)
    {
        const Q30Combo *c = q30_combo(MAPCOMBO(bx, by));

        if(combo_btn_target(c) && effect_at(bx, by, 0))
        {
            found = MAPCOMBO(bx, by);
            fx = bx;
            fy = by;
        }
    }

    if(bx2 >= 0 && by2 >= 0 && bx2 < 256 && by2 < 176)
    {
        const Q30Combo *c = q30_combo(MAPCOMBO(bx2, by2));

        if(combo_btn_target(c) && effect_at(bx2, by2, 0))
        {
            found = MAPCOMBO(bx2, by2);
            fx = bx2;
            fy = by2;
        }
    }

    if(!rule(qr30_OLD_FFC_FUNCTIONALITY))
    {
        for(int i = 0; i < MAXFFCS; ++i)
        {
            if(!tmpscr[0].ffdata[i] || !(ffc_at(i, bx, by) || ffc_at(i, bx2, by2)))
                continue;

            if(combo_btn_target(q30_combo(tmpscr[0].ffdata[i])))
            {
                ffc = i;
                break;
            }
        }
    }

    if(found < 0 && ffc < 0)
    {
        for(int i = 5; i >= 0 && found < 0; --i)
        {
            for(int p = 0; p < 2 && found < 0; ++p)
            {
                int px = p ? bx2 : bx, py = p ? by2 : by;

                if(px < 0 || py < 0 || px > 255 || py > 175)
                    continue;

                int cid = MAPCOMBO2(i, px, py);
                const Q30Combo *c = q30_combo(cid);

                if(combo_btn_target(c) && effect_at(px, py, i + 1))
                {
                    found = cid;
                    found_lyr = i + 1;
                    fx = px;
                    fy = py;
                }
            }
        }
    }

    if(found < 0 && ffc < 0)
        return false;

    int cid = ffc >= 0 ? tmpscr[0].ffdata[ffc] : found;
    const Q30Combo *c = q30_combo(cid);
    byte sign_input = 0;
    bool didprompt = false;

    if(c && c->type == Q30C_SIGNPOST && !c->only_gentrig)
    {
        bool blocked = (d == up && (c->usrflags & 0x200)) || (d == down && (c->usrflags & 0x100))
                       || (d == left && (c->usrflags & 0x800)) || (d == right && (c->usrflags & 0x400));

        if(!blocked)
        {
            byte intbtn = a_int(c->attributes[10]);

            if(intbtn)
            {
                sign_input = btn_peek(intbtn) ? intbtn : 0;

                if(!sign_input)
                {
                    if(c->usrflags & 0x1000)
                    {
                        g_prompt_combo = a_int(c->attributes[1]);
                        g_prompt_cset = a_int(c->attributes[12]);
                        g_prompt_x = a_int(c->attributes[16]);
                        g_prompt_y = a_int(c->attributes[17]);
                        didprompt = true;
                    }
                }
                else
                {
                    btn_eat(sign_input);
                    trigger_sign(c);
                }
            }
            else if(Link.getPushing() >= 8 && !(Link.getPushing() % 8))
                trigger_sign(c);
        }
    }

    // generic button triggers
    {
        int dir_flag;

        switch(d)
        {
        case down: dir_flag = Q30TF_BTN_TOP; break;
        case up: dir_flag = Q30TF_BTN_BOTTOM; break;
        case right: dir_flag = Q30TF_BTN_LEFT; break;
        default: dir_flag = Q30TF_BTN_RIGHT; break;
        }

        H h = ffc >= 0 ? H(ffc) : H(found_lyr, ((fy >> 4) << 4) + (fx >> 4));
        PosInfo &cpos = h_info(h);
        bool found_trigger_dir = false, did_trigger = false;
        byte inputs_to_eat = 0;
        int base_cid = h_data(h);

        if(c)
        {
            for(size_t idx = 0; idx < c->triggers.size(); ++idx)
            {
                const Q30Trigger &t = c->triggers[idx];

                if(!t.flags.get(dir_flag))
                    continue;

                found_trigger_dir = true;

                if(ffc < 0 && cpos.t(idx).cooldown)
                    continue;

                if(!(t.triggerbtn && (btn_peek(t.triggerbtn) || (t.triggerbtn & sign_input))))
                    continue;

                did_trigger = true;
                bool success = do_trigger(h, idx);

                if(success || t.fail_msgstr)
                    inputs_to_eat |= t.triggerbtn;

                if(success && t.flags.get(Q30TF_CANCEL_TRIGGER))
                    break;

                if(h_data(h) != base_cid)
                    break;
            }
        }

        if(inputs_to_eat)
            btn_eat(inputs_to_eat);

        if(!found_trigger_dir || didprompt || did_trigger)
            return did_trigger;

        if(c->type == Q30C_BUTTONPROMPT)
        {
            g_prompt_combo = a_int(c->attributes[0]);
            g_prompt_cset = a_int(c->attributes[8]);
            g_prompt_x = a_int(c->attributes[16]);
            g_prompt_y = a_int(c->attributes[17]);
        }
        else
        {
            for(size_t idx = 0; idx < c->triggers.size(); ++idx)
            {
                if(ffc < 0 && cpos.t(idx).cooldown)
                    continue;

                const Q30Trigger &t = c->triggers[idx];
                bool hasitem = false;
                bool cond = conditions(h, idx, hasitem, false);
                word pcid = cond ? t.prompt_cid : t.fail_prompt_cid;
                byte pcs = cond ? t.prompt_cs : t.fail_prompt_cs;

                if(pcid)
                {
                    g_prompt_combo = pcid;
                    g_prompt_cset = pcs;
                    g_prompt_x = t.prompt_x;
                    g_prompt_y = t.prompt_y;
                    break;
                }
            }
        }
    }

    return false;
}

void q30_draw_prompt(BITMAP *dest)
{
    if(!g_prompt_combo || !q30_active())
        return;

    overcombo(dest, int(Link.getX()) + g_prompt_x, int(Link.getY()) + g_prompt_y + playing_field_offset, g_prompt_combo, g_prompt_cset);
}

// ---- item and enemy states, misc hooks ----------------------------------------------

static std::map<long, int> g_item_exstate, g_enemy_exstate;

void q30_note_item_exstate(long uid, int exstate) { g_item_exstate[uid] = exstate; }
void q30_note_enemy_exstate(long uid, int exstate) { g_enemy_exstate[uid] = exstate; }

void q30_item_collected(long uid)
{
    std::map<long, int>::iterator it = g_item_exstate.find(uid);

    if(it == g_item_exstate.end())
        return;

    if(it->second >= 0)
    {
        states_setup();
        (states_mark(), g_xstates[cur_mi()]) |= (1u << it->second);
        states_flush();
    }

    g_item_exstate.erase(it);
}

void q30_enemy_killed(long uid)
{
    std::map<long, int>::iterator it = g_enemy_exstate.find(uid);

    if(it == g_enemy_exstate.end())
        return;

    if(it->second >= 0)
    {
        states_setup();
        (states_mark(), g_xstates[cur_mi()]) |= (1u << it->second);
        states_flush();
    }

    g_enemy_exstate.erase(it);
}

static void (*g_music_player)(long) = NULL;

void q30_play_quest_music(long id)
{
    // 3.0's advanced music objects belong to the audio code; the dmap's music otherwise
    if(g_music_player)
        g_music_player(id);
    else if(id > 0)
        playLevelMusic();
}

static void (*g_genscr_runner)(word) = NULL;

void q30_set_generic_script_runner(void (*fn)(word)) { g_genscr_runner = fn; }
void q30_set_music_player(void (*fn)(long)) { g_music_player = fn; }

void q30_run_generic_script(word id)
{
    if(g_genscr_runner)
        g_genscr_runner(id);
}

// ---- ice ---------------------------------------------------------------------------

static int g_script_ice = 0, g_ice_combo = 0, g_sliding = 0, g_ice_entry = 0, g_ice_mentry = 0;
static double g_ice_vx = 0, g_ice_vy = 0, g_ice_ax = 0, g_ice_ay = 0;

static bool icy_player(int cid)
{
    const Q30Combo *c = q30_combo(cid);
    return c && c->type == Q30C_ICY && (c->usrflags & 0x2);
}

static int get_icy(int x, int y)
{
    if(x < 0 || y < 0 || x > 255 || y > 175 || q30_has_bridge_above(x, y, 2))
        return 0;

    int c = MAPCOMBO2(1, x, y);

    if(icy_player(c))
        return c;

    if(q30_has_bridge_at(x, y, 2))
        return 0;

    c = MAPCOMBO2(0, x, y);

    if(icy_player(c))
        return c;

    if(q30_has_bridge_at(x, y, 1))
        return 0;

    c = MAPCOMBO(x, y);
    return icy_player(c) ? c : 0;
}

static void clear_ice()
{
    g_ice_vx = g_ice_vy = g_ice_ax = g_ice_ay = 0;
    g_sliding = g_ice_entry = g_ice_mentry = g_ice_combo = 0;
}

static double clamp_abs(double v, double cap)
{
    return v > cap ? cap : (v < -cap ? -cap : v);
}

void q30_force_ice(long combo)
{
    g_script_ice = (int)combo;
}

void q30_force_ice_velocity(bool setx, long vx, bool sety, long vy)
{
    int ic = g_script_ice ? (icy_player(g_script_ice) ? g_script_ice : 0) : g_ice_combo;
    const Q30Combo *c = ic ? q30_combo(ic) : NULL;

    if(!c)
        return;

    if(g_script_ice && !g_sliding)
    {
        g_sliding = 1;
        g_ice_vx = g_ice_vy = 0;
        g_ice_entry = g_ice_mentry = a_int(c->attributes[9]);
    }

    double cap = zc_max(0.0001, fabs(c->attributes[2] / 10000.0));

    if(setx)
        g_ice_vx = clamp_abs(vx / 10000.0, cap);

    if(sety)
        g_ice_vy = clamp_abs(vy / 10000.0, cap);
}

static bool slide_blocked(int nx, int ny, bool horizontal, int sign)
{
    bool big = get_bit(quest_rules, qr_LTTPCOLLISION) != 0;

    if(horizontal)
    {
        int px = nx + (sign > 0 ? 15 : 0);
        return _walkflag(px, ny + (big ? 0 : 8), 1) || _walkflag(px, ny + 15, 1);
    }

    int py = sign > 0 ? ny + 15 : ny + (big ? 0 : 8);
    return _walkflag(nx, py, 1) || _walkflag(nx + 8, py, 1) || _walkflag(nx + 15, py, 1);
}

// Returns 0 if Link isn't on ice, 1 sliding with no direction held, 2 with one held.
int q30_ice_move(bool u, bool d, bool l, bool r)
{
    if(!q30_active())
        return 0;

    int x = int(Link.getX()), y = int(Link.getY());
    bool inair = int(Link.getZ()) > 0;
    int ic = g_ice_combo;

    if(!inair)
    {
        const int sens = 2;
        bool big = get_bit(quest_rules, qr_LTTPCOLLISION) != 0;
        int ty = y + (big ? 0 : 8);
        int xs[5] = {x + 7, x + sens, x + sens, x + 15 - sens, x + 15 - sens};
        int ys[5] = {y + (big ? 8 : 12), ty + sens, y + 15 - sens, ty + sens, y + 15 - sens};
        ic = 0;

        for(int q = 0; q < 5 && !ic; ++q)
            ic = get_icy(xs[q], ys[q]);

        g_ice_combo = ic;
    }

    if(g_script_ice)
        ic = icy_player(g_script_ice) ? g_script_ice : 0;

    const Q30Combo *c = ic ? q30_combo(ic) : NULL;

    if(!c)
    {
        clear_ice();
        return 0;
    }

    double odx = (r ? 1.5 : 0) - (l ? 1.5 : 0), ody = (d ? 1.5 : 0) - (u ? 1.5 : 0);

    if(g_sliding == 2 && !inair)
    {
        if((g_ice_vx + odx) * g_ice_vx < 0)
            g_ice_vx = 0;

        if((g_ice_vy + ody) * g_ice_vy < 0)
            g_ice_vy = 0;

        g_sliding = 1;
    }

    if(!g_sliding)
    {
        if(inair)
            return 0;

        g_sliding = 1;
        double start = c->attributes[8] / 10000.0 / 100.0;
        g_ice_vx = odx * start;
        g_ice_vy = ody * start;
        g_ice_entry = g_ice_mentry = a_int(c->attributes[9]);
    }
    else
    {
        if(inair)
            g_sliding = 2;

        double accel = zc_max(0.0001, c->attributes[0] / 10000.0);
        double decel = zc_max(0.0001, c->attributes[1] / 10000.0);

        if(g_ice_entry && --g_ice_entry)
        {
            double perc = (double)g_ice_entry / g_ice_mentry;
            perc *= perc;
            decel = perc * 0.75 + (1 - perc) * decel;
        }

        if(inair)
        {
            accel = fabs(odx) / 100.0;

            if(accel)
                g_ice_vx += odx < 0 ? -accel : accel;

            accel = fabs(ody) / 100.0;

            if(accel)
                g_ice_vy += ody < 0 ? -accel : accel;

            decel = 0;      // 2.53's init data has no air drag
        }
        else
        {
            if(odx < 0)
                g_ice_vx = g_ice_vx - accel < odx ? zc_min(g_ice_vx, odx) : g_ice_vx - accel;
            else if(odx > 0)
                g_ice_vx = g_ice_vx + accel > odx ? zc_max(g_ice_vx, odx) : g_ice_vx + accel;

            if(ody < 0)
                g_ice_vy = g_ice_vy - accel < ody ? zc_min(g_ice_vy, ody) : g_ice_vy - accel;
            else if(ody > 0)
                g_ice_vy = g_ice_vy + accel > ody ? zc_max(g_ice_vy, ody) : g_ice_vy + accel;
        }

        if(decel > 0)
        {
            if(g_ice_vx)
            {
                if(!odx && fabs(g_ice_vx) < decel)
                    g_ice_vx = 0;
                else if(g_ice_vx > 0 && odx <= 0)
                    g_ice_vx -= decel;
                else if(g_ice_vx < 0 && odx >= 0)
                    g_ice_vx += decel;
            }

            if(g_ice_vy)
            {
                if(!ody && fabs(g_ice_vy) < decel)
                    g_ice_vy = 0;
                else if(g_ice_vy > 0 && ody <= 0)
                    g_ice_vy -= decel;
                else if(g_ice_vy < 0 && ody >= 0)
                    g_ice_vy += decel;
            }
        }
    }

    double cap = zc_max(0.0001, fabs(c->attributes[2] / 10000.0));
    g_ice_vx = clamp_abs(g_ice_vx, cap);
    g_ice_vy = clamp_abs(g_ice_vy, cap);

    if(!g_ice_vx && !g_ice_vy)
        g_ice_entry = 0;

    // whole pixels only, so 2.53's grid-based checks stay valid
    g_ice_ax += g_ice_vx;
    g_ice_ay += g_ice_vy;
    int sx = (int)g_ice_ax, sy = (int)g_ice_ay;
    g_ice_ax -= sx;
    g_ice_ay -= sy;
    int sign = sx < 0 ? -1 : 1;

    for(int q = 0; q < (sx < 0 ? -sx : sx); ++q)
    {
        if(slide_blocked(x + sign, y, true, sign))
        {
            g_ice_vx = g_ice_ax = 0;
            break;
        }

        x += sign;
    }

    sign = sy < 0 ? -1 : 1;

    for(int q = 0; q < (sy < 0 ? -sy : sy); ++q)
    {
        if(slide_blocked(x, y + sign, false, sign))
        {
            g_ice_vy = g_ice_ay = 0;
            break;
        }

        y += sign;
    }

    if(x != int(Link.getX()))
        Link.setX(x);

    if(y != int(Link.getY()))
        Link.setY(y);

    return (u || d || l || r) ? 2 : 1;
}

// ---- tint and gravity set by triggers -----------------------------------------------

static bool g_tinted = false;
static PALETTE g_tint_backup;
static int g_tint_add[3] = {0, 0, 0};

static void apply_tint()
{
    for(int i = 0; i <= 0xEF; ++i)
    {
        RAMpal[i].r = (byte)zc_max(0, zc_min(63, RAMpal[i].r + g_tint_add[0] / 4));
        RAMpal[i].g = (byte)zc_max(0, zc_min(63, RAMpal[i].g + g_tint_add[1] / 4));
        RAMpal[i].b = (byte)zc_max(0, zc_min(63, RAMpal[i].b + g_tint_add[2] / 4));
    }

    refreshpal = true;
}

void q30_tint(int r, int g, int b)
{
    if(g_tinted)
        memcpy(RAMpal, g_tint_backup, sizeof(PALETTE));
    else
    {
        memcpy(g_tint_backup, RAMpal, sizeof(PALETTE));
        g_tint_add[0] = g_tint_add[1] = g_tint_add[2] = 0;
    }

    g_tint_add[0] += r;
    g_tint_add[1] += g;
    g_tint_add[2] += b;
    g_tinted = true;
    apply_tint();
}

void q30_tint_clear()
{
    if(!g_tinted)
        return;

    memcpy(RAMpal, g_tint_backup, sizeof(PALETTE));
    g_tinted = false;
    refreshpal = true;
}

static void reapply_tint()
{
    if(!g_tinted)
        return;

    // the new screen's palette replaced what was tinted
    memcpy(g_tint_backup, RAMpal, sizeof(PALETTE));
    apply_tint();
}

static bool g_grav_set = false;
static int g_grav = 0, g_termv = 0;

void q30_set_gravity(long gravity, long terminal)
{
    g_grav_set = true;
    g_grav = (int)(gravity / 100);
    g_termv = (int)(terminal / 100);
}

void q30_revert_gravity()
{
    g_grav_set = false;
}

int q30_link_gravity()
{
    return (g_grav_set && q30_active()) ? g_grav : (int)zinit.gravity;
}

int q30_link_terminalv()
{
    return (g_grav_set && q30_active()) ? g_termv : (int)zinit.terminalv;
}

static void reset_screen_effects()
{
    g_grav_set = false;
    g_script_ice = 0;
    clear_ice();
    reapply_tint();
}

// ---- new mirrors and push blocks ----------------------------------------------------

// -1: no new mirror here. -2: it stops the weapon. Else the weapon's new direction.
int q30_mirror_new(int x, int y, int dir)
{
    if(!q30_active() || x < 0 || y < 0 || x > 255 || y > 175)
        return -1;

    const Q30Combo *c = q30_combo(MAPCOMBO(x, y));

    if(!c || c->type != Q30C_MIRRORNEW)
        return -1;

    int d = dir;

    if(d > 3)
        d = (d == l_up || d == r_up) ? up : down;

    int newdir = a_int(c->attributes[8 + d]);
    return (newdir < 0 || newdir > 7) ? -2 : newdir;
}

// Whether Link pushing at bx,by moves a generic push block.
bool q30_pushblock_ok(int bx, int by, int d, int pushing, bool enemies)
{
    if(!q30_active())
        return false;

    const Q30Combo *c = q30_combo(MAPCOMBO(bx, by));

    if(!c || c->type != Q30C_PUSHBLOCK)
        return false;

    if((c->usrflags & 0x20) && (pushing < 16 || enemies))
        return false;

    int heavy = a_int(c->attributes[8]);

    if(heavy)
    {
        int glove = current_item_id(itype_bracelet);

        if(glove < 0 || itemsbuf[glove].power < heavy)
            return false;
    }

    switch(d)
    {
    case up: return (c->usrflags & 0x1) != 0;
    case down: return (c->usrflags & 0x2) != 0;
    case left: return (c->usrflags & 0x4) != 0;
    case right: return (c->usrflags & 0x8) != 0;
    }

    return false;
}

void q30_pushblock_sfx(int bx, int by, int linkx)
{
    const Q30Combo *c = q30_combo(MAPCOMBO(bx, by));

    if(c && c->type == Q30C_PUSHBLOCK && a_int(c->attributes[9]))
        sfx(a_int(c->attributes[9]), pan(linkx));
}

// ---- light beams ------------------------------------------------------------------

namespace
{

const int BEAM_SOLID = -1;
const word SP_VISITED = 0x1, SP_MASK = 0x1F;
const int BEAM_AGE_LIMIT = 512;
const long BEAM_STEP_LIMIT = 200000;     // a bad map can't hang the game

struct BeamCell
{
    int type;
    bool blocks;
    int mirror[4];
};

BeamCell g_beam_cells[176];
dword g_beam_trig[176];
std::map<int, std::vector<word> > g_beam_grids;
long g_beam_steps = 0;
bool g_beam_present = false;
BITMAP *g_beam_bmp = NULL;
int g_beam_hero_pos = -1, g_beam_hero_dir = 0;
bool g_beam_hero_blocks = false, g_beam_hero_reflects = false;

inline word sp_flag(int d) { return (word)(0x2 << d); }
inline word sp_goflag(int d) { return (word)(0x20 << d); }

dword beam_flag(unsigned idx)
{
    return idx ? (dword)1 << (idx - 1) : ~(dword)0;
}

bool beam_turn(const BeamCell &cell, int &d)
{
    switch(cell.type)
    {
    case Q30C_LIGHTTARGET:
        if(cell.blocks)
            return false;

        break;

    case cMIRROR:
        d ^= 1;
        break;

    case cMIRRORSLASH:
        switch(d)
        {
        case up: d = right; break;
        case right: d = up; break;
        case down: d = left; break;
        case left: d = down; break;
        }

        break;

    case cMIRRORBACKSLASH:
        switch(d)
        {
        case up: d = left; break;
        case left: d = up; break;
        case down: d = right; break;
        case right: d = down; break;
        }

        break;

    case Q30C_MIRRORNEW:
    {
        int nd = cell.mirror[d];

        if(nd < 0 || nd > 7)
            return false;

        if(nd <= 3)
            d = nd;

        break;
    }
    }

    return true;
}

void beam_run(std::vector<word> &grid, int age, int d, int pos, unsigned set)
{
    if(d < 0 || d > 3)
        return;

    dword flag = beam_flag(set);
    bool do_age = true;

    while(++g_beam_steps < BEAM_STEP_LIMIT)
    {
        int x = pos & 15, y = pos >> 4;

        if((grid[pos] & sp_goflag(d)) == sp_goflag(d))
            return;

        grid[pos] |= sp_goflag(d);

        if((grid[pos] & sp_flag(d)) != sp_flag(d))
        {
            grid[pos] |= sp_flag(d);
            g_beam_trig[pos] |= flag;
            do_age = false;
            age = 0;
        }

        switch(d)
        {
        case up: --y; break;
        case down: ++y; break;
        case left: --x; break;
        case right: ++x; break;
        }

        if(x < 0 || x > 15 || y < 0 || y > 10)
            return;

        pos = y * 16 + x;
        const BeamCell &cell = g_beam_cells[pos];

        if(cell.type == BEAM_SOLID || cell.type == cBLOCKALL)
            return;

        if(pos == g_beam_hero_pos && g_beam_hero_blocks && d == (g_beam_hero_dir ^ 1))
            return;

        word opp = sp_flag(d ^ 1);

        if((grid[pos] & opp) != opp)
        {
            grid[pos] |= opp;
            g_beam_trig[pos] |= flag;
            do_age = false;
            age = 0;
        }

        if(do_age)
        {
            if(++age > BEAM_AGE_LIMIT)
                return;
        }
        else
            do_age = true;

        if(pos == g_beam_hero_pos && g_beam_hero_reflects)
            d = g_beam_hero_dir;
        else if(cell.type == cMAGICPRISM || cell.type == cMAGICPRISM4)
        {
            for(int nd = 0; nd < 4; ++nd)
            {
                if(cell.type == cMAGICPRISM && nd == (d ^ 1))
                    continue;

                beam_run(grid, age, nd, pos, set);
            }

            return;
        }
        else if(!beam_turn(cell, d))
            return;
    }
}

// An ffc's beam moves in steps of 16 from wherever the ffc is, so its spots are keyed
// by pixel position; upstream's lattice holds the same data.
long ff_key(int x, int y)
{
    return ((long)(y + 1000) << 12) | (long)(x + 1000);
}

int ff_cell(int x, int y)
{
    int cx = x < 0 ? 0 : (x > 255 ? 255 : x), cy = y < 0 ? 0 : (y > 175 ? 175 : y);
    return (cy >> 4) * 16 + (cx >> 4);
}

bool ff_valid(int x, int y)
{
    return x + 16 >= 0 && x - 16 < 256 && y + 16 >= 0 && y - 16 < 176;
}

bool ff_hits_hero(int x, int y, int wid)
{
    int bx = x - wid / 2, by = y - wid / 2;
    int hx = int(Link.getX()), hy = int(Link.getY());
    return hx + 15 >= bx && hx < bx + wid && hy + 15 >= by && hy < by + wid;
}

std::map<int, std::map<long, word> > g_ffbeams;

void ff_beam_run(std::map<long, word> &spots, int age, int d, int x, int y, unsigned set, int wid)
{
    if(d < 0 || d > 3)
        return;

    dword flag = beam_flag(set);
    bool do_age = true;

    while(ff_valid(x, y) && ++g_beam_steps < BEAM_STEP_LIMIT)
    {
        word &spot = spots[ff_key(x, y)];

        if((spot & sp_goflag(d)) == sp_goflag(d))
            return;

        spot |= sp_goflag(d);

        if((spot & sp_flag(d)) != sp_flag(d))
        {
            spot |= sp_flag(d);
            g_beam_trig[ff_cell(x, y)] |= flag;
            do_age = false;
            age = 0;
        }

        switch(d)
        {
        case up: y -= 16; break;
        case down: y += 16; break;
        case left: x -= 16; break;
        case right: x += 16; break;
        }

        int pos = ff_cell(x, y);
        const BeamCell &cell = g_beam_cells[pos];

        if(cell.type == BEAM_SOLID || cell.type == cBLOCKALL)
            return;

        if(cell.type == Q30C_MIRRORNEW && (cell.mirror[d] < 0 || cell.mirror[d] > 7))
            return;

        bool hit_hero = g_beam_hero_pos >= 0 && ff_hits_hero(x, y, wid);

        if(g_beam_hero_blocks && d == (g_beam_hero_dir ^ 1) && hit_hero)
            return;

        word &entered = spots[ff_key(x, y)];
        word opp = sp_flag(d ^ 1);

        if((entered & opp) != opp)
        {
            entered |= opp;
            g_beam_trig[pos] |= flag;
            do_age = false;
            age = 0;
        }

        if(!ff_valid(x, y))
            return;

        if(do_age)
        {
            if(++age > BEAM_AGE_LIMIT)
                return;
        }
        else
            do_age = true;

        if(g_beam_hero_reflects && hit_hero)
            d = g_beam_hero_dir;
        else if(cell.type == cMAGICPRISM || cell.type == cMAGICPRISM4)
        {
            for(int nd = 0; nd < 4; ++nd)
            {
                if(cell.type == cMAGICPRISM && nd == (d ^ 1))
                    continue;

                ff_beam_run(spots, age, nd, x, y, set, wid);
            }

            return;
        }
        else if(!beam_turn(cell, d))
            return;
    }
}

int beam_id(const Q30Combo *c)
{
    if(c->usrflags & 0x1)
        return zc_max(0, a_int(c->attributes[0])) | ((a_int(c->attributes[9]) % 12) << 24);

    int id = -((a_int(c->attributes[11]) << 16) | (a_int(c->attributes[10]) << 8) | a_int(c->attributes[9]));
    return id ? id : (1 << 24);
}

void beam_launch(const Q30Combo *c, int pos)
{
    int d = a_int(c->attributes[8]);
    unsigned set = a_int(c->attributes[12]);

    if(d < 0)
        d = 0;
    else if(d > 255)
        d = 255;

    std::vector<word> &grid = g_beam_grids[beam_id(c)];

    if(grid.empty())
        grid.resize(176, 0);

    if(d > 3)
    {
        grid[pos] |= SP_VISITED;
        g_beam_trig[pos] |= beam_flag(set);
    }

    if(g_beam_hero_reflects && pos == g_beam_hero_pos)
        d = g_beam_hero_dir;

    beam_run(grid, 0, d, pos, set);
}

int beam_offs(word val)
{
    static const int table[16] = {0, 1, 2, 9, 3, 5, 7, 14, 4, 6, 8, 13, 10, 12, 11, 15};
    int dirs = (val & SP_MASK) >> 1;

    if(!dirs)
        return val ? 0 : -1;

    return table[dirs];
}

void beam_draw_cell(BITMAP *b, int x, int y, word val, int id)
{
    int offs = beam_offs(val);

    if(offs < 0)
        return;

    if(id > 0 && id != (1 << 24))
    {
        puttile16(b, (id & 0xFFFFFF) + offs, x, y, (id >> 24) & 15, 0);
        return;
    }

    int cid = (id == (1 << 24)) ? 0 : -id;
    int colors[3] = {(cid >> 16) & 0xFF, (cid >> 8) & 0xFF, cid & 0xFF};
    int half[3] = {4, 2, 1};

    if(offs == 0)
    {
        circlefill(b, x + 8, y + 8, 7, colors[0]);
        circlefill(b, x + 8, y + 8, 5, colors[1]);
        circlefill(b, x + 8, y + 8, 3, colors[2]);
        return;
    }

    int dirs = (val & SP_MASK) >> 1;

    for(int q = 0; q < 3; ++q)
    {
        int h = half[q];

        if(dirs & 1)
            rectfill(b, x + 8 - h, y, x + 7 + h, y + 7, colors[q]);

        if(dirs & 2)
            rectfill(b, x + 8 - h, y + 8, x + 7 + h, y + 15, colors[q]);

        if(dirs & 4)
            rectfill(b, x, y + 8 - h, x + 7, y + 7 + h, colors[q]);

        if(dirs & 8)
            rectfill(b, x + 8, y + 8 - h, x + 15, y + 7 + h, colors[q]);

        rectfill(b, x + 8 - h, y + 8 - h, x + 7 + h, y + 7 + h, colors[q]);
    }
}

bool cond_light(const Q30Trigger &t, size_t, void *ctx)
{
    bool lit = (g_beam_trig[*(int *)ctx] & beam_flag(t.triglbeam)) != 0;
    return t.flags.get(lit ? Q30TF_LIGHTON : Q30TF_LIGHTOFF);
}

} // namespace

// Once per frame: builds the beams and fires light targets and Light-> triggers.
void q30_light_update()
{
    if(!q30_active() || tmpscr[0].data.empty())
        return;

    bool any_spot = false, any_light_use = false;
    memset(g_beam_trig, 0, sizeof(g_beam_trig));
    memset(g_beam_cells, 0, sizeof(g_beam_cells));
    g_beam_grids.clear();
    g_ffbeams.clear();
    g_beam_steps = 0;

    {
        bool seen[176];
        memset(seen, 0, sizeof(seen));

        for(int lyr = 6; lyr >= 0; --lyr)
        {
            if(lyr && (!tmpscr2[lyr - 1].valid || tmpscr2[lyr - 1].data.empty()))
                continue;

            for(int pos = 0; pos < 176; ++pos)
            {
                int cid = h_data(H(lyr, pos));
                const Q30Combo *c = q30_combo(cid);

                if(c && (c->type == Q30C_SPOTLIGHT || c->type == Q30C_LIGHTTARGET))
                    any_light_use = true;

                for(size_t q = 0; c && q < c->triggers.size(); ++q)
                    if(c->triggers[q].flags.get(Q30TF_LIGHTON) || c->triggers[q].flags.get(Q30TF_LIGHTOFF))
                        any_light_use = true;

                if(seen[pos])
                    continue;

                int t = q30_combo_type(cid);
                BeamCell &cell = g_beam_cells[pos];
                bool found = false;
                long mask = c ? c->attributes[0] : 0;

                switch(t)
                {
                case cMIRROR: case cMIRRORSLASH: case cMIRRORBACKSLASH: case cMAGICPRISM: case cMAGICPRISM4:
                    if(!mask || (mask & 0x200))
                    {
                        cell.type = t;
                        found = true;
                    }

                    break;

                case cBLOCKALL:
                    cell.type = t;
                    found = true;
                    break;

                case Q30C_LIGHTTARGET:
                    cell.type = t;
                    cell.blocks = c && (c->usrflags & 0x4);
                    found = true;
                    break;

                case Q30C_MIRRORNEW:
                    if(c && (!mask || (mask & 0x200)))
                    {
                        cell.type = t;

                        for(int d = 0; d < 4; ++d)
                            cell.mirror[d] = a_int(c->attributes[8 + d]);

                        found = true;
                    }

                    break;

                case Q30C_GLASS:
                    found = true;
                    break;

                case Q30C_SPOTLIGHT:
                    any_spot = true;
                    break;
                }

                if(found)
                    seen[pos] = true;
                else if(lyr < 3 && (walk_byte(cid) & 0xF))
                    cell.type = BEAM_SOLID;
            }
        }
    }

    for(int i = 0; i < MAXFFCS; ++i)
    {
        if(tmpscr[0].ffflags[i] & (ffCHANGER | ffETHEREAL))
            continue;

        const Q30Combo *c = q30_combo(tmpscr[0].ffdata[i]);

        if(c && c->type == Q30C_SPOTLIGHT && (c->usrflags & 0x2))
            any_spot = true;

        if(c && !c->triggers.empty())
            any_light_use = true;
    }

    if(!any_spot && !any_light_use)
    {
        g_beam_present = false;
        return;
    }

    if(any_spot)
    {
        int shield = current_item_id(itype_shield);
        g_beam_hero_reflects = shield >= 0 && (itemsbuf[shield].misc2 & 0x200);
        g_beam_hero_blocks = !g_beam_hero_reflects && shield >= 0 && (itemsbuf[shield].misc1 & 0x200);
        int hx = int(Link.getX()) + 8, hy = int(Link.getY()) + 8;
        g_beam_hero_pos = (hx >= 0 && hy >= 0 && hx < 256 && hy < 176) ? (hy >> 4) * 16 + (hx >> 4) : -1;
        g_beam_hero_dir = Link.getDir();

        if(g_beam_hero_pos >= 0 && (g_beam_cells[g_beam_hero_pos].type == BEAM_SOLID || g_beam_cells[g_beam_hero_pos].type == cBLOCKALL))
            g_beam_hero_pos = -1;

        for(int lyr = 0; lyr <= 6; ++lyr)
        {
            if(lyr && (!tmpscr2[lyr - 1].valid || tmpscr2[lyr - 1].data.empty()))
                continue;

            for(int pos = 0; pos < 176; ++pos)
            {
                const Q30Combo *c = q30_combo(h_data(H(lyr, pos)));

                if(c && c->type == Q30C_SPOTLIGHT)
                    beam_launch(c, pos);
            }
        }

        for(int i = 0; i < MAXFFCS; ++i)
        {
            if(tmpscr[0].ffflags[i] & (ffCHANGER | ffETHEREAL))
                continue;

            const Q30Combo *c = q30_combo(tmpscr[0].ffdata[i]);

            if(!c || c->type != Q30C_SPOTLIGHT || !(c->usrflags & 0x2))
                continue;

            int cx, cy;
            h_center(H(i), cx, cy);
            int d = a_int(c->attributes[8]);
            unsigned set = a_int(c->attributes[12]);
            int wid = a_int(c->attributes[13]);

            if(wid < 1)
                wid = 8;

            std::map<long, word> &spots = g_ffbeams[beam_id(c)];

            if(d > 3 && ff_valid(cx, cy))
            {
                spots[ff_key(cx, cy)] |= SP_VISITED;
                g_beam_trig[ff_cell(cx, cy)] |= beam_flag(set);
            }

            if(g_beam_hero_reflects && g_beam_hero_pos >= 0 && ff_hits_hero(cx, cy, wid))
                d = g_beam_hero_dir;

            switch(d)
            {
            case up: cy = zc_min(175, cy); break;
            case down: cy = zc_max(0, cy); break;
            case left: cx = zc_min(255, cx); break;
            case right: cx = zc_max(0, cx); break;
            }

            if(d >= 0 && d <= 3 && ff_valid(cx, cy))
                ff_beam_run(spots, 0, d, cx, cy, set, wid);
}
    }

    g_beam_present = !g_beam_grids.empty() || !g_ffbeams.empty();

    if(g_beam_present)
    {
        if(!g_beam_bmp)
            g_beam_bmp = create_bitmap_ex(8, 256, 176);

        if(g_beam_bmp)
        {
            clear_bitmap(g_beam_bmp);

            for(std::map<int, std::vector<word> >::iterator it = g_beam_grids.begin(); it != g_beam_grids.end(); ++it)
                for(int pos = 0; pos < 176; ++pos)
                    if(it->second[pos] & SP_MASK)
                        beam_draw_cell(g_beam_bmp, (pos & 15) * 16, (pos >> 4) * 16, it->second[pos], it->first);

            for(std::map<int, std::map<long, word> >::iterator it = g_ffbeams.begin(); it != g_ffbeams.end(); ++it)
                for(std::map<long, word>::iterator sp = it->second.begin(); sp != it->second.end(); ++sp)
                    if(sp->second & SP_MASK)
                        beam_draw_cell(g_beam_bmp, (int)(sp->first & 4095) - 1000 - 8, (int)(sp->first >> 12) - 1000 - 8, sp->second, it->first);
}
    }

    // targets and Light-> triggers
    bool had_target = false, all_lit = true;
    byte &solved_state = q30_pos_state(cur_mi(), 2264);
    bool solved = (solved_state & 1) != 0;

    for(int lyr = 0; lyr <= 6; ++lyr)
    {
        if(lyr && (!tmpscr2[lyr - 1].valid || tmpscr2[lyr - 1].data.empty()))
            continue;

        for(int pos = 0; pos < 176; ++pos)
        {
            H h(lyr, pos);
            const Q30Combo *c = h_combo(h);

            if(!c)
                continue;

            if(c->type != Q30C_LIGHTTARGET)
            {
                int ctx = pos;

                if(!c->triggers.empty())
                    execute(h, cond_light, &ctx);

                continue;
            }

            had_target = true;
            bool lit = (g_beam_trig[pos] & beam_flag(a_int(c->attributes[12]))) != 0;

            if(c->usrflags & 0x2)
                lit = !lit;

            if(c->usrflags & 0x1)       // the solved version of a target
            {
                if(!(solved || lit))
                {
                    change_combo(h, h_data(h) - 1);
                    all_lit = false;
                }
            }
            else if(solved || lit)
                change_combo(h, h_data(h) + 1);
            else
                all_lit = false;
        }
    }

    for(int i = 0; i < MAXFFCS; ++i)
    {
        if(!tmpscr[0].ffdata[i] || (tmpscr[0].ffflags[i] & ffCHANGER))
            continue;

        const Q30Combo *c = q30_combo(tmpscr[0].ffdata[i]);

        if(!c)
            continue;

        int cx, cy;
        h_center(H(i), cx, cy);

        if(cx < 0 || cy < 0 || cx > 255 || cy > 175)
            continue;

        int ctx = (cy >> 4) * 16 + (cx >> 4);

        if(c->type == Q30C_LIGHTTARGET)
        {
            had_target = true;
            bool lit = (g_beam_trig[ctx] & beam_flag(a_int(c->attributes[12]))) != 0;

            if(c->usrflags & 0x2)
                lit = !lit;

            if(c->usrflags & 0x1)
            {
                if(!(solved || lit))
                {
                    change_combo(H(i), h_data(H(i)) - 1);
                    all_lit = false;
                }
            }
            else if(solved || lit)
                change_combo(H(i), h_data(H(i)) + 1);
            else
                all_lit = false;
        }
        else if(!c->triggers.empty())
            execute(H(i), cond_light, &ctx);
    }

    if(had_target && all_lit && !solved)
    {
        hidden_entrance(0, true, false);
        sfx(tmpscr[0].secretsfx);

        if(!(tmpscr[0].flags5 & fTEMPSECRETS))
        {
            setmapflag(mSECRET);
            solved_state |= 1;
            states_mark();
            states_flush();
        }
    }
}

void q30_draw_beams(BITMAP *dest)
{
    if(g_beam_present && g_beam_bmp && q30_active())
        masked_blit(g_beam_bmp, dest, 0, 0, 0, playing_field_offset, 256, 176);
}

static std::map<int, byte> g_true_sflags;

void q30_clear_placed_flags()
{
    g_true_sflags.clear();
}

void q30_note_placed_flag(int screen_index, int pos, byte flag)
{
    if(flag && pos >= 0 && pos < 176)
        g_true_sflags[screen_index * 176 + pos] = flag;
}

int q30_true_placed_flag(int screen_index, int pos)
{
    std::map<int, byte>::const_iterator it = g_true_sflags.find(screen_index * 176 + pos);
    return it == g_true_sflags.end() ? 0 : it->second;
}

// ---- true item families --------------------------------------------------------------

static std::map<int, long> g_item_family;

void q30_clear_item_families()
{
    g_item_family.clear();
}

void q30_set_item_family(int item, long family)
{
    if(family >= itype_max)
        g_item_family[item] = family;
}

long q30_item_family(int item)
{
    if(item < 0 || item >= MAXITEMS)
        return -1;

    std::map<int, long>::const_iterator it = g_item_family.find(item);
    return it == g_item_family.end() ? (long)itemsbuf[item].family : it->second;
}

// The owned item of a family the 2.53 tables can't name, best level first.
int q30_current_item(long family)
{
    if(!q30_active() || !game)
        return -1;

    int best = -1;

    for(std::map<int, long>::const_iterator it = g_item_family.begin(); it != g_item_family.end(); ++it)
    {
        if(it->second != family || !game->item[it->first] || item_disabled(it->first))
            continue;

        if(best < 0 || itemsbuf[it->first].fam_type > itemsbuf[best].fam_type)
            best = it->first;
    }

    return best;
}

// ---- lifting -----------------------------------------------------------------------

static const long Q30_ITYPE_LIFTGLOVE = 279, Q30_ITYPE_LANTERN = 268;
enum { LIFT_DIS_SHIELD = 1, LIFT_DIS_ITEMS = 2, LIFT_DIS_SWIM = 4, LIFT_DROP_HIT = 8 };

struct ThrownInfo
{
    double ax, ay, z, fall, step;
    int death_sprite, death_sfx, drop_item, drop_set, drop_flags;
    bool break_solid;
};

static std::map<long, ThrownInfo> g_thrown;
static weapon *g_lift_wpn = NULL;
static ThrownInfo g_lift_info;
static int g_lift_clk = 0, g_lift_tclk = 0, g_lift_flags = 0, g_last_lift_id = -1;
static double g_lift_height = 0;

bool q30_lifting() { return q30_active() && g_lift_wpn != NULL; }
bool q30_lift_busy() { return q30_active() && g_lift_clk > 0; }

void q30_lift_reset()
{
    delete g_lift_wpn;
    g_lift_wpn = NULL;
    g_lift_clk = g_lift_tclk = 0;
    g_last_lift_id = -1;
    g_thrown.clear();
}

static void set_lift_flags(int liftid)
{
    if(liftid < 0)
        return;

    word fl = itemsbuf[liftid].flags;
    g_lift_flags = ((fl & ITEM_FLAG2) ? 0 : LIFT_DIS_SWIM) | ((fl & ITEM_FLAG3) ? LIFT_DIS_SHIELD : 0)
                   | ((fl & ITEM_FLAG4) ? LIFT_DIS_ITEMS : 0) | ((fl & ITEM_FLAG5) ? LIFT_DROP_HIT : 0);
}

static void handle_lift(bool dec)
{
    if(!g_lift_wpn)
    {
        g_lift_clk = 0;
        return;
    }

    int x = int(Link.getX()), y = int(Link.getY()), d = Link.getDir();
    g_lift_wpn->dir = d;

    if(g_lift_clk <= (dec ? 1 : 0))
    {
        g_lift_clk = g_lift_tclk = 0;
        g_lift_wpn->x = (fix)x;
        g_lift_wpn->y = (fix)y;
        g_lift_wpn->z = (fix)(int)g_lift_height;
        return;
    }

    if(dec)
        --g_lift_clk;

    double perc = g_lift_clk / (double)g_lift_tclk;
    int xd = 0, yd = 0;

    switch(d)
    {
    case up: yd = (int)(-16 * perc); break;
    case down: yd = (int)(16 * perc); break;
    case left: xd = (int)(-16 * perc); break;
    case right: xd = (int)(16 * perc); break;
    }

    g_lift_wpn->x = (fix)(x + xd);
    g_lift_wpn->y = (fix)(y + yd);
    g_lift_wpn->z = (fix)(int)(g_lift_height * (1.0 - perc));
}

static void release_lift(int step_100, int fall)
{
    weapon *w = g_lift_wpn;
    ThrownInfo info = g_lift_info;
    g_lift_wpn = NULL;
    g_lift_clk = g_lift_tclk = 0;
    info.ax = info.ay = 0;
    info.z = g_lift_height + (double)int(Link.getZ());
    info.fall = fall;
    info.step = step_100 / 100.0;
    w->z = (fix)(int)info.z;
    w->dir = Link.getDir();
    Lwpns.add(w);
    g_thrown[w->getUID()] = info;
}

void q30_drop_lift()
{
    if(!g_lift_wpn)
        return;

    handle_lift(false);
    release_lift(0, 0);
}

void q30_lift_on_hit()
{
    if(q30_active() && g_lift_wpn && (g_lift_flags & LIFT_DROP_HIT))
        q30_drop_lift();
}

static bool can_lift(int glove)
{
    if(glove < 0 || q30_link_stunned())
        return false;

    int act = Link.getAction();

    if(act == swimming)
        return (itemsbuf[glove].flags & ITEM_FLAG2) != 0;

    return act == none || act == walking;
}

static void set_lift_gfx(weapon *w, const Q30Combo *c, int cid, int cset)
{
    switch(c->liftgfx)
    {
    case 1:
        w->tile = combobuf[c->liftcmb].tile;
        w->cs = (c->liftflags & 0x20) ? cset : c->liftcs;
        w->flip = combobuf[c->liftcmb].flip;
        break;

    case 2:
        w->LOADGFX(c->liftsprite);
        break;

    default:
        w->tile = combobuf[cid].tile;
        w->cs = cset;
        w->flip = combobuf[cid].flip;
        break;
    }

    w->o_tile = w->tile;
    w->o_cset = w->cs;
    w->frames = 0;
}

static bool lift_combo(const H &base, int glove)
{
    const Q30Combo *first = h_combo(base);

    if(!first || !(first->liftflags & 0x1))
        return false;

    std::vector<H> handles;

    if(first->liftflags & 0x80)
        large_combo_handles(base, handles);
    else
        handles.push_back(base);

    if(handles.empty())
        return false;

    const H &main_h = handles[0];
    const Q30Combo *c = h_combo(main_h);

    if(!c || !(c->liftflags & 0x1) || c->liftlvl > itemsbuf[glove].fam_type)
        return false;

    int left_x = 256, top_y = 176, right_x = 0, bottom_y = 0;

    for(size_t q = 0; q < handles.size(); ++q)
    {
        int hx, hy;
        h_xy(handles[q], hx, hy);
        left_x = zc_min(left_x, hx);
        top_y = zc_min(top_y, hy);
        right_x = zc_max(right_x, hx + 16);
        bottom_y = zc_max(bottom_y, hy + 16);
    }

    int center_x = (left_x + right_x) / 2, center_y = (top_y + bottom_y) / 2;

    if(c->liftsfx)
        sfx(c->liftsfx, pan(center_x));
    else if(itemsbuf[glove].usesound)
        sfx(itemsbuf[glove].usesound, pan(center_x));

    int dropitem = -1, dropset = -1;
    bool hasitem = c->liftitm > 0;
    int pflags = ipBIGRANGE | ((c->liftflags & 0x8) ? ipONETIME2 : ipTIMER);

    if(hasitem)
    {
        if(c->liftflags & 0x2)
            dropset = c->liftitm;
        else
            dropitem = c->liftitm;
    }

    if(hasitem && (c->liftflags & 0x8) && getmapflag(mBELOW))
    {
        hasitem = false;
        dropitem = dropset = -1;
    }

    if(hasitem && (c->liftflags & 0x4))
    {
        if(dropset > -1)
            dropitem = select_dropitem(dropset, center_x - 8, center_y - 8);

        if(dropitem > -1)
            items.add(new item((fix)(center_x - 8), (fix)(center_y - 8), (fix)0, dropitem, pflags, 0));

        dropitem = dropset = -1;
    }

    int cid = h_data(main_h), cset = h_cset(main_h);
    weapon *w = new weapon((fix)left_x, (fix)top_y, (fix)0, Q30W_THROWN, itemsbuf[glove].fam_type,
                           c->liftdmg * DAMAGE_MULTIPLIER, Link.getDir() ^ 1, glove, Link.getUID());
    w->xofs = 0;
    w->yofs = playing_field_offset;
    w->hxofs = w->hyofs = 0;
    w->hxsz = zc_max(1, right_x - left_x);
    w->hysz = zc_max(1, bottom_y - top_y);
    w->hzsz = 8;
    w->txsz = zc_max(1, (right_x - left_x) / 16);
    w->tysz = zc_max(1, (bottom_y - top_y) / 16);
    set_lift_gfx(w, c, cid, cset);

    g_lift_info.ax = g_lift_info.ay = g_lift_info.z = g_lift_info.fall = g_lift_info.step = 0;
    g_lift_info.death_sprite = c->liftbreaksprite;
    g_lift_info.death_sfx = c->liftbreaksfx;
    g_lift_info.drop_item = dropitem;
    g_lift_info.drop_set = dropset;
    g_lift_info.drop_flags = pflags;
    g_lift_info.break_solid = (c->liftflags & 0x40) || (c->lift_weap.wflags & 0x4);

    g_lift_wpn = w;
    g_lift_clk = g_lift_tclk = c->lifttime;
    g_lift_height = c->lifthei < 0 ? 0 : c->lifthei;

    // read before the combo goes: the undercombos replace what c describes
    bool large = (first->liftflags & 0x80) != 0;

    for(size_t q = 0; q < handles.size(); ++q)
    {
        const Q30Combo *tmp = h_combo(handles[q]);

        if(!tmp || (large && !(tmp->liftflags & 0x80)))
            continue;

        int ucmb = tmp->liftundercmb, ucs = tmp->liftundercs;
        bool nocs = (tmp->liftflags & 0x10) != 0;
        h_set_data(handles[q], ucmb);

        if(!nocs)
            h_set_cset(handles[q], ucs);

        mapscr *m = h_scr(handles[q]);

        if(m && !handles[q].is_ffc())
            m->sflag[handles[q].pos] = 0;
    }

    return true;
}

void q30_do_liftglove(int liftid, bool passive)
{
    if(!g_lift_wpn)
        g_last_lift_id = -1;

    if(liftid < 0)
        liftid = (g_last_lift_id >= 0 && can_lift(g_last_lift_id)) ? g_last_lift_id : q30_current_item(Q30_ITYPE_LIFTGLOVE);

    if(!can_lift(liftid))
        return;

    byte intbtn = (byte)(itemsbuf[liftid].misc1 & 0xFF);

    if(passive && (!intbtn || !btn_peek(intbtn)))
        return;

    bool had_weapon = g_lift_wpn != NULL;

    if(!had_weapon && !checkmagiccost(liftid))
        return;

    if(g_lift_wpn)
    {
        if(!g_lift_clk)
        {
            handle_lift(false);
            release_lift(itemsbuf[liftid].misc2, -itemsbuf[liftid].misc3);
            g_last_lift_id = -1;

            if(itemsbuf[liftid].usesound)
                sfx(itemsbuf[liftid].usesound, pan(int(Link.getX())));
        }
        else
            g_last_lift_id = liftid;

        if(passive)
            btn_eat(intbtn);

        return;
    }

    int bx, by, bx2, by2;
    push_points(Link.getDir(), int(Link.getX()), int(Link.getY()), bx, by, bx2, by2);
    bool lifted = false;

    for(int lyr = 6; lyr >= 0 && !lifted; --lyr)
    {
        if(bx >= 0 && by >= 0 && bx < 256 && by < 176)
        {
            H h(lyr, ((by >> 4) << 4) + (bx >> 4));

            if(h_scr(h) && lift_combo(h, liftid))
                lifted = true;
        }

        if(!lifted && bx2 >= 0 && by2 >= 0 && bx2 < 256 && by2 < 176 && (bx2 >> 4 != bx >> 4 || by2 >> 4 != by >> 4))
        {
            H h(lyr, ((by2 >> 4) << 4) + (bx2 >> 4));

            if(h_scr(h) && lift_combo(h, liftid))
                lifted = true;
        }
    }

    if(!lifted)
    {
        g_last_lift_id = -1;
        return;
    }

    g_last_lift_id = liftid;
    paymagiccost(liftid);
    set_lift_flags(liftid);

    if(passive)
        btn_eat(intbtn);
}

// >=0: the use was handled, the return for startwpn. -1: not a lift matter.
int q30_startwpn(int itemid)
{
    if(!q30_active())
        return -1;

    bool glove = q30_item_family(itemid) == Q30_ITYPE_LIFTGLOVE;

    if(glove)
    {
        q30_do_liftglove(itemid, false);
        return 0;
    }

    if(g_lift_wpn && (g_lift_flags & LIFT_DIS_ITEMS))
        return 0;

    return -1;
}

static void lift_frame()
{
    if(g_lift_wpn)
    {
        if(Link.isSwimming() && (g_lift_flags & LIFT_DIS_SWIM))
            q30_drop_lift();
        else
            handle_lift(true);
    }

    if(Link.getAction() == none || Link.getAction() == walking || Link.getAction() == swimming)
        q30_do_liftglove(-1, true);
}

static void thrown_tick(weapon *w)
{
    std::map<long, ThrownInfo>::iterator it = g_thrown.find(w->getUID());

    if(it == g_thrown.end())
        return;

    ThrownInfo &t = it->second;
    int x = int(w->x), y = int(w->y);

    switch(w->dir)
    {
    case up: t.ay -= t.step; break;
    case down: t.ay += t.step; break;
    case left: t.ax -= t.step; break;
    case right: t.ax += t.step; break;
    }

    int sx = (int)t.ax, sy = (int)t.ay;
    t.ax -= sx;
    t.ay -= sy;
    x += sx;
    y += sy;
    w->x = (fix)x;
    w->y = (fix)y;

    t.fall += q30_link_gravity();

    if(t.fall > q30_link_terminalv())
        t.fall = q30_link_terminalv();

    t.z -= t.fall / 100.0;
    w->z = (fix)(int)(t.z < 0 ? 0 : t.z);

    bool solid = t.break_solid && _walkflag(x + 8, y + 8, 1);
    bool gone = x < -16 || y < -16 || x > 256 || y > 176;

    if(t.z > 0 && !solid && !gone)
        return;

    if(!gone)
    {
        if(t.drop_set > -1)
            t.drop_item = select_dropitem(t.drop_set, x, y);

        if(t.drop_item >= 0 && t.drop_item < MAXITEMS)
            items.add(new item((fix)x, (fix)y, (fix)0, t.drop_item, t.drop_flags, 0));

        switch(t.death_sprite)
        {
        case -2: decorations.add(new dBushLeaves((fix)x, (fix)y, dBUSHLEAVES, 0)); break;
        case -3: decorations.add(new dFlowerClippings((fix)x, (fix)y, dFLOWERCLIPPINGS, 0)); break;
        case -4: decorations.add(new dGrassClippings((fix)x, (fix)y, dGRASSCLIPPINGS, 0)); break;
        }

        if(t.death_sfx > 0)
            sfx(t.death_sfx, pan(x));
    }

    w->dead = 0;
    g_thrown.erase(it);
}

void q30_thrown_frame(weapon *w)
{
    if(w->id == Q30W_THROWN)
        thrown_tick(w);
}

void q30_draw_lift(BITMAP *dest)
{
    if(!g_lift_wpn || !q30_active())
        return;

    fix oz = g_lift_wpn->z;
    g_lift_wpn->z = oz + Link.getZ();
    g_lift_wpn->draw(dest);
    g_lift_wpn->z = oz;
}

// ---- new dark rooms ------------------------------------------------------------------

static BITMAP *g_dark_mask = NULL;

static void dark_shape(int cx, int cy, int shape, int rad)
{
    if(rad <= 0)
        return;

    int outer = rad + rad / 5;

    if(shape == 2)
    {
        rectfill(g_dark_mask, cx - outer, cy - outer, cx + outer, cy + outer, 2);
        rectfill(g_dark_mask, cx - rad, cy - rad, cx + rad, cy + rad, 0);
    }
    else
    {
        // cones are drawn as circles here
        circlefill(g_dark_mask, cx, cy, outer, 2);
        circlefill(g_dark_mask, cx, cy, rad, 0);
    }
}

static void dark_offset(int &cx, int &cy, int d, int offs)
{
    switch(d)
    {
    case up: cy -= offs; break;
    case down: cy += offs; break;
    case left: cx -= offs; break;
    case right: cx += offs; break;
    }
}

static void torch_light(const Q30Combo *c, int cx, int cy)
{
    int d = a_int(c->attributes[10]);
    int offs = a_int(c->attributes[11]);

    if(offs)
        dark_offset(cx, cy, d, offs);

    dark_shape(cx, cy, a_int(c->attributes[9]), a_int(c->attributes[8]));
}

void q30_draw_darkness(BITMAP *dest)
{
    if(!q30_active() || !rule(qr30_NEW_DARKROOM) || !(tmpscr[0].flags & fDARK))
        return;

    if(!g_dark_mask)
    {
        g_dark_mask = create_bitmap_ex(8, 256, 176);

        if(!g_dark_mask)
            return;
    }

    clear_to_color(g_dark_mask, 1);

    for(int lyr = 0; lyr <= 6; ++lyr)
    {
        if(lyr && (!tmpscr2[lyr - 1].valid || tmpscr2[lyr - 1].data.empty()))
            continue;

        for(int pos = 0; pos < 176; ++pos)
        {
            const Q30Combo *c = q30_combo(h_data(H(lyr, pos)));

            if(c && c->type == Q30C_TORCH)
                torch_light(c, (pos & 15) * 16 + 8, (pos >> 4) * 16 + 8);
        }
    }

    for(int i = 0; i < MAXFFCS; ++i)
    {
        const Q30Combo *c = tmpscr[0].ffdata[i] ? q30_combo(tmpscr[0].ffdata[i]) : NULL;

        if(c && c->type == Q30C_TORCH)
        {
            int cx, cy;
            h_center(H(i), cx, cy);
            torch_light(c, cx, cy);
        }
    }

    int lamp = q30_current_item(Q30_ITYPE_LANTERN);

    if(lamp >= 0 && checkmagiccost(lamp))
    {
        int hx = int(Link.getX()) + 8, hy = int(Link.getY()) + 8;
        dark_offset(hx, hy, Link.getDir(), itemsbuf[lamp].misc3);
        dark_shape(hx, hy, itemsbuf[lamp].misc1, itemsbuf[lamp].misc2);
    }

    for(int i = 0; i < Lwpns.Count(); ++i)
    {
        weapon *w = (weapon *)Lwpns.spr(i);

        if(w->isLit)
            dark_shape(int(w->x) + 8, int(w->y) + 8, 0, 32);
    }

    int yo = playing_field_offset;
    int black = BLACK;

    for(int y = 0; y < 176; ++y)
    {
        byte *line = g_dark_mask->line[y];

        for(int x = 0; x < 256; ++x)
        {
            if(line[x] == 1 || (line[x] == 2 && ((x + y) & 1)))
                putpixel(dest, x, y + yo, black);
        }
    }
}

// The lighting code still darkens the palette for an fDARK screen; new dark rooms
// show the screen in full colour under the overlay.
static void new_darkroom_palette()
{
    if(rule(qr30_NEW_DARKROOM) && (darkroom || naturaldark))
    {
        darkroom = false;
        naturaldark = false;
        loadlvlpal(DMaps[currdmap].color);

        if(get_bit(quest_rules, qr_FADECS5))
            loadpalset(5, 5);
    }
}

// ---- cutscene locks -----------------------------------------------------------------

static bool g_cutscene = false;
static long g_cutscene_btns = 0;
static int g_cutscene_err = 0;

void q30_cutscene_flags(const Q30Combo *c)
{
    if(c->usrflags & 0x1)
    {
        g_cutscene = false;
        return;
    }

    g_cutscene = true;
    g_cutscene_btns = c->attributes[0];
    g_cutscene_err = a_int(c->attributes[8]);
}

void q30_cutscene_clear()
{
    g_cutscene = false;
}

void q30_cutscene_input()
{
    if(!g_cutscene || !q30_active())
        return;

    if(game->get_life() <= 0)
    {
        g_cutscene = false;     // a dead Link must be able to continue
        return;
    }

    bool blocked = false;

    for(int q = 0; q < 18; ++q)
    {
        if(control_state[q] && !(g_cutscene_btns & (1L << q)))
        {
            control_state[q] = false;
            blocked = true;
        }
    }

    if(blocked && g_cutscene_err)
        sfx(g_cutscene_err);
}

// ---- flags at a position, past 2.53's range ---------------------------------------------

static bool layer_has_flag(const H &h, int flag)
{
    const mapscr *m = h_scr(h);

    if(!m)
        return false;

    if(q30_combo_flag(h_data(h)) == flag)
        return true;

    int index;

    if(h.layer == 0)
        index = currmap * MAPSCRS + currscr;
    else
    {
        int lm = tmpscr[0].layermap[h.layer - 1];

        if(!lm)
            return false;

        index = (lm - 1) * MAPSCRS + tmpscr[0].layerscreen[h.layer - 1];
    }

    return q30_true_placed_flag(index, h.pos) == flag;
}

static bool flag_at_point(int x, int y, int flag, int maxlayer)
{
    if(x < 0 || y < 0 || x > 255 || y > 175)
        return false;

    int pos = ((y >> 4) << 4) + (x >> 4);

    for(int l = 0; l <= maxlayer; ++l)
        if(layer_has_flag(H(l, pos), flag))
            return true;

    return false;
}

enum { Q30MF_SIDEVIEWLADDER = 161, Q30MF_SIDEVIEWPLATFORM = 162, Q30MF_UNSAFEGROUND_SENS = 167, Q30MF_UNSAFEGROUND = 168 };

bool q30_sideview_platform_at(int x, int y)
{
    if(!q30_active())
        return false;

    if(flag_at_point(x, y, Q30MF_SIDEVIEWPLATFORM, 2))
        return true;

    return flag_at_point(x, y, Q30MF_SIDEVIEWLADDER, 2) && !flag_at_point(x, y - 16, Q30MF_SIDEVIEWLADDER, 2);
}

bool q30_unsafe_ground(int x, int y)
{
    if(!q30_active())
        return false;

    for(int px = x; px <= x + 15; px += 15)
        for(int py = y + 8; py <= y + 15; py += 7)
            if(flag_at_point(px, py, Q30MF_UNSAFEGROUND_SENS, 6))
                return true;

    return flag_at_point(x + 8, y + 12, Q30MF_UNSAFEGROUND, 6);
}

// ---- switch hook ---------------------------------------------------------------------

static const long Q30_ITYPE_SWITCHHOOK = 274;
static bool g_hook_reset_pending = false;

long q30_item_switch_family(int itemid)
{
    return (q30_active() && q30_item_family(itemid) == Q30_ITYPE_SWITCHHOOK) ? (long)itype_hookshot : (long)itemsbuf[itemid].family;
}

static bool is_push_flag30(int f)
{
    switch(f)
    {
    case mfPUSHUD: case mfPUSHUDNS: case mfPUSHUDINS: case mfPUSHLR: case mfPUSHLRNS: case mfPUSHLRINS:
    case mfPUSHU: case mfPUSHUNS: case mfPUSHUINS: case mfPUSHD: case mfPUSHDNS: case mfPUSHDINS:
    case mfPUSHL: case mfPUSHLNS: case mfPUSHLINS: case mfPUSHR: case mfPUSHRNS: case mfPUSHRINS:
    case mfPUSH4: case mfPUSH4NS: case mfPUSH4INS:
        return true;
    }

    return false;
}

static bool hookable(const Q30Combo *c)
{
    return c && (c->type == Q30C_SWITCHHOOK || (c->genflags & 0x2));
}

// What the hook does to this layer; true if anything swapped.
static bool swap_layer(int q, int target, int player, int glove_level, int tx, int ty)
{
    H th(q, target), ph(q, player);
    mapscr *tscr = h_scr(th), *pscr = h_scr(ph);

    if(!tscr || !pscr)
        return false;

    const Q30Combo *c = h_combo(th);

    if(!hookable(c))
        return false;

    bool two_way = true, breaks = false;

    if(c->type == Q30C_SWITCHHOOK)
    {
        if((c->usrflags & 0x1) && h_data(ph))
            return false;

        if(zc_max(1, glove_level) < a_int(c->attributes[8]))
            return false;

        if(c->usrflags & 0x4)
        {
            two_way = false;
            breaks = true;
        }
    }
    else if(is_cuttable(c->type))
    {
        two_way = false;
        trigger_cuttable(th, c);
        return true;
    }

    if(breaks)
    {
        int it = -1;

        if(c->usrflags & 0x8)
        {
            int id = a_int(c->attributes[11]);
            it = (c->usrflags & 0x10) ? id : select_dropitem(id, tx, ty);
        }

        if(it >= 0 && it < MAXITEMS)
            items.add(new item((fix)tx, (fix)ty, (fix)0, it, ipBIGRANGE | ipTIMER, 0));

        if(a_int(c->attributes[10]))
            sfx(a_int(c->attributes[10]), pan(tx));

        if(c->usrflags & 0x20)
            change_combo(th, h_data(th) + 1);
        else
        {
            h_set_data(th, tscr->undercombo);
            h_set_cset(th, tscr->undercset);
        }

        tscr->sflag[target] = 0;
        return true;
    }

    if(two_way)
    {
        int f1 = tscr->sflag[target];
        bool push = (c->usrflags & 0x40) != 0 || is_push_flag30(f1) || is_push_flag30(c->flag);

        if(f1 == mfPUSHED)
            return false;           // locked in place

        if(c->type < cMAX && (c->type == cPUSH_WAIT || c->type == cPUSH_HW || c->type == cPUSH_HW2) && hasMainGuy())
            return false;

        int f2 = pscr->sflag[player];
        const Q30Combo *c2 = h_combo(ph);
        bool hole = f2 == mfBLOCKHOLE || f2 == mfBLOCKTRIGGER || (c2 && (c2->flag == mfBLOCKHOLE || c2->flag == mfBLOCKTRIGGER));
        int d1 = h_data(th), s1 = h_cset(th);

        if(push && hole)
        {
            // the block fills the hole or trigger under Link
            h_set_data(ph, d1);
            h_set_cset(ph, s1);
            pscr->sflag[player] = 0;
            h_set_data(th, tscr->undercombo);
            h_set_cset(th, tscr->undercset);
            tscr->sflag[target] = 0;
            return true;
        }

        h_set_data(th, h_data(ph));
        h_set_cset(th, h_cset(ph));
        tscr->sflag[target] = (byte)f2;
        h_set_data(ph, d1);
        h_set_cset(ph, s1);
        pscr->sflag[player] = (byte)f1;
    }

    return true;
}

// The hook's tip reached something: Link and the combo change places at once. Returns
// true if the head should end this frame.
bool q30_switchhook_frame(weapon *w)
{
    if(!q30_active() || w->parentitem < 0 || q30_item_family(w->parentitem) != Q30_ITYPE_SWITCHHOOK || w->misc != 0)
        return false;

    int wx = int(w->x), wy = int(w->y), tx, ty;

    switch(w->dir)
    {
    case up: tx = wx + 2; ty = wy + 7; break;
    case down: tx = wx + 12; ty = wy + 12; break;
    case left: tx = wx + 6; ty = wy + 13; break;
    default: tx = wx + 9; ty = wy + 13; break;
    }

    if(tx < 0 || ty < 0 || tx > 255 || ty > 175)
        return false;

    int target = ((ty >> 4) << 4) + (tx >> 4);
    int maxlayer = get_bit(quest_rules, qr_HOOKSHOTLAYERFIX) ? 2 : 0;
    bool found = false;

    for(int q = maxlayer; q >= 0 && !found; --q)
    {
        H h(q, target);
        found = h_scr(h) && hookable(h_combo(h));
    }

    if(!found)
        return false;

    int lx = int(Link.getX()) + 8, ly = int(Link.getY()) + 8;

    if(lx < 0 || ly < 0 || lx > 255 || ly > 175)
        return true;

    int player = ((ly >> 4) << 4) + (lx >> 4);

    if(player == target)
        return true;

    int glove_level = itemsbuf[w->parentitem].fam_type;
    int cx = (target & 15) * 16, cy = (target >> 4) * 16;
    bool swapped = false;

    for(int q = maxlayer; q >= 0; --q)
        swapped = swap_layer(q, target, player, glove_level, cx, cy) || swapped;

    if(swapped)
    {
        if(itemsbuf[w->parentitem].usesound)
            sfx(itemsbuf[w->parentitem].usesound, pan(cx));

        Link.setX(cx);
        Link.setY(cy);
        q30_drop_lift();
    }

    g_hook_reset_pending = true;    // the head can't be removed from inside its own animate
    return true;
}

void q30_switchhook_update()
{
    if(g_hook_reset_pending)
    {
        g_hook_reset_pending = false;
        Link.reset_hookshot();
    }
}

bool q30_quest_rule(int id)
{
    return quest_header_zelda_version >= 0x255 && g_rules_known && get_bit(g_rules, id) != 0;
}

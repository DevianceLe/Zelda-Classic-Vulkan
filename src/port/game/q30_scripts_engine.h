// The script engine for 2.55 and 3.0 quests: script instances, the frame stages that run them,
// and the sprite/combo/generic bookkeeping. Included once, at the end of src/ffscript.cpp.

#ifndef ZC_Q30_SCRIPTS_ENGINE_H
#define ZC_Q30_SCRIPTS_ENGINE_H

#include <set>

#include "q30_triggers.h"

extern dword q255_compat_rule_version;
extern sprite_list guys, items, Ewpns, Lwpns, Sitems, chainlinks, decorations;
extern ffscript *screenscripts[256];
extern ffscript *guyscripts[256];
extern ffscript *wpnscripts[256];

// ---- what each script type is keyed by ---------------------------------------------------

// Upstream's per-sprite script settings, kept beside the sprite. The extra fields are the parts
// of 3.0's sprite that 2.53's has no room for.
struct Zasm30SpriteState
{
    word script;
    long initd[8];
    long misc2[16];       // Misc[16..31]
    long fakez, fakefall, script_flip, script_tile, rotation, scale, gravity_strength, terminal_v;
    long no_colldet, shadow_spr, shadow_xofs, shadow_yofs, invis_clk, move_flags, fall_clk, drown_clk;
    long glow_rad, glow_shape, glow_ofs;
    long spawn_spr, death_spr;
    long sw_hooked;
    long weapon_flags, level, pstring, weap_timeout, unblockable, knockback_speed;
    bool engine_animate, script_started, auto_rotate, no_slide, no_script_kb;

    Zasm30SpriteState()
    {
        memset(this, 0, sizeof(*this));
        fakez = 0;
        script_tile = -1;
        script_flip = -1;
        scale = 100;
        engine_animate = true;
    }
};

static std::map<long, Zasm30SpriteState> zasm30_sprite_states;

static Zasm30SpriteState &zasm30_sprite_state(long uid)
{
    return zasm30_sprite_states[uid];
}

static Zasm30SpriteState *zasm30_sprite_state_if(long uid)
{
    std::map<long, Zasm30SpriteState>::iterator it = zasm30_sprite_states.find(uid);
    return it == zasm30_sprite_states.end() ? NULL : &it->second;
}

static std::map<int, Q30ScriptRef> zasm30_guy_scripts, zasm30_guy_weapon_scripts;
static std::map<int, dword> zasm30_guy_moveflags;       // what each enemy's editor data sets
static std::map<int, Q30ScriptRef> zasm30_item_sprite_scripts, zasm30_item_weapon_scripts;

static void zasm30_put_ref(std::map<int, Q30ScriptRef> &t, int key, word script, const long *initd)
{
    if(!script)
    {
        t.erase(key);
        return;
    }

    Q30ScriptRef ref;
    ref.script = script;
    if(initd)
        memcpy(ref.initd, initd, sizeof(ref.initd));
    t[key] = ref;
}

static const Q30ScriptRef *zasm30_get_ref(std::map<int, Q30ScriptRef> &t, int key)
{
    std::map<int, Q30ScriptRef>::iterator it = t.find(key);
    return it == t.end() ? NULL : &it->second;
}

void q30_clear_guy_scripts() { zasm30_guy_scripts.clear(); zasm30_guy_weapon_scripts.clear(); zasm30_guy_moveflags.clear(); }
void q30_set_guy_moveflags(int guy, dword flags) { if(flags) zasm30_guy_moveflags[guy] = flags; else zasm30_guy_moveflags.erase(guy); }
void q30_set_guy_script(int guy, word script, const long *initd) { zasm30_put_ref(zasm30_guy_scripts, guy, script, initd); }
const Q30ScriptRef *q30_guy_script(int guy) { return zasm30_get_ref(zasm30_guy_scripts, guy); }
void q30_set_guy_weapon_script(int guy, word script, const long *initd) { zasm30_put_ref(zasm30_guy_weapon_scripts, guy, script, initd); }
const Q30ScriptRef *q30_guy_weapon_script(int guy) { return zasm30_get_ref(zasm30_guy_weapon_scripts, guy); }

static bool zasm30_item_passive[Q30_MAX_ITEMS];  // upstream's item_passive_script; the reader fills it
static byte zasm30_item_doscript[Q30_MAX_ITEMS]; // upstream's data.doscript, for an item's own script

void q30_clear_item_scripts()
{
    zasm30_item_sprite_scripts.clear();
    zasm30_item_weapon_scripts.clear();
    memset(zasm30_item_passive, 0, sizeof(zasm30_item_passive));
}

void q30_set_item_passive_script(int id, bool on)
{
    if(id >= 0 && id < Q30_MAX_ITEMS)
        zasm30_item_passive[id] = on;
}

void q30_set_item_sprite_script(int item, word script, const long *initd) { zasm30_put_ref(zasm30_item_sprite_scripts, item, script, initd); }
const Q30ScriptRef *q30_item_sprite_script(int item) { return zasm30_get_ref(zasm30_item_sprite_scripts, item); }
void q30_set_item_weapon_script(int item, word script, const long *initd) { zasm30_put_ref(zasm30_item_weapon_scripts, item, script, initd); }
const Q30ScriptRef *q30_item_weapon_script(int item) { return zasm30_get_ref(zasm30_item_weapon_scripts, item); }

// ---- generic scripts ------------------------------------------------------------------------

Zasm30Generic zasm30_generic[Q30_GENERIC_SLOTS];
static int zasm30_max_generic = 0;
static std::vector<int> zasm30_generic_coded;  // the slots that hold a script (zasm30_count_generic)
int zasm30_genscript_timing = Z30T_START_FRAME;
static int zasm30_generic_frozen_depth = 0;

void q30_clear_generic_init()
{
    for(int i = 0; i < Q30_GENERIC_SLOTS; ++i)
        zasm30_generic[i] = Zasm30Generic();
}

void q30_set_generic_init(int slot, bool run, word exit_state, word reload_state, dword event_state,
                          const long *initd, const std::map<dword, long> *data)
{
    if(slot < 0 || slot >= Q30_GENERIC_SLOTS)
        return;

    Zasm30Generic &g = zasm30_generic[slot];
    g.loaded = true;
    g.run = run;
    g.exit_state = exit_state;
    g.reload_state = reload_state;
    g.event_state = event_state;
    if(initd)
        memcpy(g.initd, initd, sizeof(g.initd));
    g.data.clear();

    if(data)
    {
        for(std::map<dword, long>::const_iterator it = data->begin(); it != data->end(); ++it)
        {
            if(it->first >= g.data.size())
                g.data.resize(it->first + 1, 0);
            g.data[it->first] = it->second;
        }
    }
}

static void zasm30_generic_quit(int slot)
{
    zasm30_generic[slot].run = false;
    zasm30_drop_instance(SCRIPT_GENERIC, slot);
}

static void zasm30_generic_launch(int slot)
{
    zasm30_generic_quit(slot);
    Zasm30Generic &g = zasm30_generic[slot];
    g.run = true;
    g.atleast = true;
    g.waituntil = Z30T_START_FRAME;
    g.waitevent = false;
}

// A generic script's exit-state bits (GENSCR_ST_*) say when it stops.
void q30_generic_exit_state(int state)
{
    for(int q = 1; q <= zasm30_max_generic; ++q)
    {
        if((zasm30_generic[q].exit_state & (1 << state)) && zasm30_generic[q].run)
            zasm30_generic_quit(q);
    }
}

// Reloads scripts whose reload-state bit is set.
void q30_generic_reload_state(int state)
{
    for(int q = 1; q <= zasm30_max_generic; ++q)
    {
        if((zasm30_generic[q].reload_state & (1 << state)))
            zasm30_generic_launch(q);
    }
}

// A game event: any generic script waiting for it runs now.
void q30_generic_event(int event)
{
    for(int q = 1; q <= zasm30_max_generic; ++q)
    {
        Zasm30Generic &g = zasm30_generic[q];

        if(!g.run || !q30_generic_scripts[q] || !g.waitevent)
            continue;

        if(g.event_state & (1 << event))
        {
            if(zasm30_trace_on)
                al_trace("q30: generic event %d wakes generic script %d (frame %d)\n", event, q, frame);

            Zasm30Instance *in = zasm30_instance(SCRIPT_GENERIC, q);
            in->ri.d[2] = event * 10000;
            g.waitevent = false;
            zasm30_run(SCRIPT_GENERIC, q, q);
        }
    }
}

// Game->EventData while a generic script event is being thrown (upstream's FFCore.eventData).
static std::vector<long> zasm30_event_data;

// A running generic script is waiting in WaitEvent() for this event.
bool q30_generic_listening(int event)
{
    if(!zasm30_ffscript_version)
        return false;

    for(int q = 1; q <= zasm30_max_generic; ++q)
    {
        const Zasm30Generic &g = zasm30_generic[q];

        if(g.run && q30_generic_scripts[q] && g.waitevent && (g.event_state & (1 << event)))
            return true;
    }

    return false;
}

// Throws a generic script event (upstream's throwGenScriptEvent) with its Game->EventData, which
// the scripts it wakes may change: the changed values come back in `data`. The engine can throw
// one while a script runs, so the running script's state is put back afterwards.
void q30_throw_generic_event(int event, long *data, int count)
{
    if(!zasm30_ffscript_version || !zasm30_max_generic)
        return;

    if(!q30_generic_listening(event))
        return;

    std::vector<long> outer_data;
    outer_data.swap(zasm30_event_data);
    zasm30_event_data.assign(data, data + count);

    refInfo *saved_ri = ri;
    ffscript *saved_curscript = curscript;
    long (*saved_stack)[ZSCRIPT_STACK_ALLOC] = stack;
    Zasm30Frame *saved_frame = zasm30_cur_frame;
    Zasm30Instance *saved_inst = zasm30_cur_inst;
    long *saved_sp = zasm30_wide_sp;
    long saved_limit = zasm30_stack_limit;
    long saved_arg1 = sarg1, saved_arg2 = sarg2;
    byte saved_type = curScriptType;
    word saved_num = curScriptNum;
    long saved_key = zasm30_run_key;

    q30_generic_event(event);

    ri = saved_ri;
    curscript = saved_curscript;
    stack = saved_stack;
    zasm30_cur_frame = saved_frame;
    zasm30_cur_inst = saved_inst;
    zasm30_wide_sp = saved_sp;
    zasm30_stack_limit = saved_limit;
    sarg1 = saved_arg1;
    sarg2 = saved_arg2;
    curScriptType = saved_type;
    curScriptNum = saved_num;
    zasm30_run_key = saved_key;

    for(int n = 0; n < count && n < (int)zasm30_event_data.size(); ++n)
        data[n] = zasm30_event_data[n];

    zasm30_event_data.swap(outer_data);
}

// The same for an event with no data (INIT, CONTINUE, the screen and DMap changes...).
void q30_throw_generic_event(int event)
{
    q30_throw_generic_event(event, NULL, 0);
}

// A running generic script whose exit state has this bit stops; else one whose reload state has
// it starts over (upstream's timeExitAllGenscript).
void q30_generic_time_exit(int state)
{
    if(!zasm30_ffscript_version)
        return;

    for(int q = 1; q <= zasm30_max_generic; ++q)
    {
        Zasm30Generic &g = zasm30_generic[q];

        if(!g.run)
            continue;

        if(g.exit_state & (1 << state))
            zasm30_generic_quit(q);
        else if(g.reload_state & (1 << state))
            zasm30_generic_launch(q);
    }
}

// loadscr for the screen being played: before it loads (loaded false), a DMap or level change
// since the last one ends or restarts the generic scripts set to and throws CHANGE_DMAP and
// CHANGE_LEVEL, then the same for CHANGE_SCREEN; once it has loaded, CHANGE_SCREEN is thrown.
// Upstream does the DMap and level part where it warps or scrolls to another DMap.
static int zasm30_event_dmap = -1, zasm30_event_level = -1;

void q30_screen_change_events(bool loaded)
{
    if(!zasm30_ffscript_version)
        return;

    if(loaded)
    {
        q30_throw_generic_event(Z30EV_CHANGE_SCREEN);
        return;
    }

    const bool dmap = currdmap != zasm30_event_dmap, level = dlevel != zasm30_event_level;

    if(dmap)
        q30_generic_time_exit(Z30ST_CHANGE_DMAP);
    if(level)
        q30_generic_time_exit(Z30ST_CHANGE_LEVEL);

    zasm30_event_dmap = currdmap;
    zasm30_event_level = dlevel;

    if(dmap)
        q30_throw_generic_event(Z30EV_CHANGE_DMAP);
    if(level)
        q30_throw_generic_event(Z30EV_CHANGE_LEVEL);

    q30_generic_time_exit(Z30ST_CHANGE_SCREEN);
}

static void zasm30_generic_passive(int timing)
{
    if(!zasm30_max_generic)
        return;

    bool init = timing == Z30T_INIT;

    if(!init)
    {
        while(zasm30_genscript_timing != timing)
            zasm30_generic_passive(zasm30_genscript_timing);
    }

    // Only the slots that hold a script: a quest has 511, and this runs some 40 times a frame.
    for(size_t n = 0; n < zasm30_generic_coded.size(); ++n)
    {
        const int q = zasm30_generic_coded[n];
        Zasm30Generic &g = zasm30_generic[q];

        if(!g.run || !q30_generic_scripts[q] || g.waitevent)
            continue;

        if(!init && (g.waituntil > timing || (!g.atleast && g.waituntil != timing)))
            continue;

        zasm30_run(SCRIPT_GENERIC, q, q);
    }

    if(init || zasm30_genscript_timing >= Z30T_END_FRAME)
        zasm30_genscript_timing = Z30T_START_FRAME;
    else
        ++zasm30_genscript_timing;
}

static void zasm30_count_generic()
{
    zasm30_max_generic = 0;
    zasm30_generic_coded.clear();

    for(int q = 1; q < Q30_GENERIC_SLOTS; ++q)
    {
        if(q30_generic_scripts[q])
            zasm30_max_generic = q;

        // a slot with no script never runs (zasm30_prepare_type refuses it)
        if(q30_generic_scripts[q] && q30_generic_scripts[q][0].command != 0xFFFF)
            zasm30_generic_coded.push_back(q);
    }
}

// ---- the running script's frame ----------------------------------------------------------

static ffscript *zasm30_script_for(int type, word script)
{
    switch(type)
    {
    case SCRIPT_ITEM:       return script < NUMSCRIPTITEM ? itemscripts[script] : NULL;
    case SCRIPT_LINK:       return script < Q30_HERO_SLOTS ? q30_hero_scripts[script] : NULL;
    case SCRIPT_LWPN:       return script < Q30_LWPN_SLOTS ? q30_lwpn_scripts[script] : NULL;
    case SCRIPT_EWPN:       return script < Q30_EWPN_SLOTS ? q30_ewpn_scripts[script] : NULL;
    case SCRIPT_NPC:        return script < 256 ? guyscripts[script] : NULL;
    case SCRIPT_DMAP:
    case SCRIPT_PASSIVESUB:
    case SCRIPT_ACTIVESUB:
    case SCRIPT_ONMAP:      return script < Q30_DMAP_SLOTS ? q30_dmap_scripts[script] : NULL;
    case SCRIPT_ITEMSPRITE: return script < Q30_ITEMSPRITE_SLOTS ? q30_itemsprite_scripts[script] : NULL;
    case SCRIPT_COMBO:      return script < Q30_COMBO_SLOTS ? q30_combo_scripts[script] : NULL;
    case SCRIPT_GENERIC:
    case SCRIPT_GENERICFROZEN: return script < Q30_GENERIC_SLOTS ? q30_generic_scripts[script] : NULL;
    case SCRIPT_ENGINESUB:  return script < Q30_SUBSCREEN_SLOTS ? q30_subscreen_scripts[script] : NULL;
    }
    return NULL;
}

static bool zasm30_prepare_type(byte type, word script, long key)
{
    ffscript *code = zasm30_script_for(type, script);

    if(!code || code[0].command == 0xFFFF)
        return false;

    Zasm30Instance *in = zasm30_instance(type, key);

    if(!in->doscript)
        return false;

    zasm30_cur_inst = in;
    zasm30_cur_frame = &in->frame;
    ri = &in->ri;
    curscript = code;
    in->script = script;
    return true;
}

static const Zasm30Instance *zasm30_cur_inst_const() { return zasm30_cur_inst; }

// `this` and the InitD values when a script of a new type first runs.
static void zasm30_init_type(byte type, Zasm30Instance *in)
{
    switch(type)
    {
    case SCRIPT_ITEM:
        in->ri.idata = in->key;
        in->frame.inst[0] = in->key;
        memcpy(in->ri.d, itemsbuf[in->key].initiald, 8 * sizeof(long));
        memcpy(in->ri.a, itemsbuf[in->key].initiala, 2 * sizeof(long));
        break;

    case SCRIPT_NPC:
    {
        in->ri.guyref = in->key;
        Zasm30SpriteState &st = zasm30_sprite_state(in->key);
        memcpy(in->ri.d, st.initd, sizeof(st.initd));
        in->frame.inst[0] = in->key;
        break;
    }
    case SCRIPT_LWPN:
    {
        in->ri.lwpn = in->key;
        Zasm30SpriteState &st = zasm30_sprite_state(in->key);
        memcpy(in->ri.d, st.initd, sizeof(st.initd));
        in->frame.inst[0] = in->key;
        break;
    }
    case SCRIPT_EWPN:
    {
        in->ri.ewpn = in->key;
        Zasm30SpriteState &st = zasm30_sprite_state(in->key);
        memcpy(in->ri.d, st.initd, sizeof(st.initd));
        in->frame.inst[0] = in->key;
        break;
    }
    case SCRIPT_ITEMSPRITE:
    {
        in->ri.itemref = in->key;
        Zasm30SpriteState &st = zasm30_sprite_state(in->key);
        memcpy(in->ri.d, st.initd, sizeof(st.initd));
        in->frame.inst[0] = in->key;
        break;
    }
    case SCRIPT_DMAP:
    case SCRIPT_PASSIVESUB:
    case SCRIPT_ACTIVESUB:
    case SCRIPT_ONMAP:
    {
        int kind = type == SCRIPT_DMAP ? q30_dmap_active : type == SCRIPT_PASSIVESUB ? q30_dmap_passive_subscreen :
                   type == SCRIPT_ACTIVESUB ? q30_dmap_active_subscreen : q30_dmap_onmap;
        const Q30ScriptRef *ref = q30_dmap_script(currdmap, kind);
        in->frame.refs[Z30REF_DMAPDATA] = currdmap;
        in->frame.inst[0] = currdmap;
        if(ref)
            memcpy(in->ri.d, ref->initd, sizeof(ref->initd));
        if(zasm30_trace_on && ref)
            al_trace("q30: dmap %d script %d (type %d) starts with InitD %ld %ld %ld %ld %ld\n", (int)currdmap, (int)ref->script, (int)type,
                     (long)ref->initd[0], (long)ref->initd[1], (long)ref->initd[2], (long)ref->initd[3], (long)ref->initd[4]);
        break;
    }
    case SCRIPT_COMBO:
    {
        const int layer = in->key >> 16, pos = in->key & 0xFFFF;
        mapscr *m = layer ? &tmpscr2[layer - 1] : tmpscr;
        const int cid = (pos >= 0 && pos < 176) ? m->data[pos] : 0;
        const Q30ScriptRef *ref = q30_combo_script(cid);
        in->frame.refs[Z30REF_COMBODATA] = cid;
        in->frame.refs[Z30REF_COMBOPOS] = in->key;
        in->frame.inst[0] = cid;
        if(ref)
            memcpy(in->ri.d, ref->initd, sizeof(ref->initd));
        break;
    }
    case SCRIPT_GENERIC:
    case SCRIPT_GENERICFROZEN:
    {
        // `this` and the genericdata reference are the slot itself, as LOADGENERICDATA sets it
        // (upstream's genericdataref = script); slot * 10000 made this->Data[] and
        // this->EventListen[] miss the slot.
        word slot = in->script;
        in->frame.refs[Z30REF_GENERICDATA] = slot;
        in->frame.inst[0] = slot;
        if(slot < Q30_GENERIC_SLOTS)
            memcpy(in->ri.d, zasm30_generic[slot].initd, sizeof(zasm30_generic[slot].initd));
        break;
    }
    case SCRIPT_LINK:
    {
        extern long zasm30_hero_initd[Q30_HERO_SLOTS][8];
        if(in->script < Q30_HERO_SLOTS)
            memcpy(in->ri.d, zasm30_hero_initd[in->script], 8 * sizeof(long));
        break;
    }
    case SCRIPT_GLOBAL:
    {
        extern long zasm30_global_initd[Q30_GLOBAL_SLOTS][8];
        if(in->script < Q30_GLOBAL_SLOTS)
            memcpy(in->ri.d, zasm30_global_initd[in->script], 8 * sizeof(long));
        break;
    }
    default:
        break;
    }
}

// Things that hold on every run.
static void zasm30_setup_frame(byte type)
{
    Zasm30Frame &f = zasm30_frame();
    f.refs[Z30REF_SCREEN] = currscr * 10000;

    // An FFC script's `this`, instance variable 0 (SCR0) in 3.0 scripts, is its own FFC as REFFFC
    // reads it (upstream's set_script_this_ref(ri->ffcref()) on every run). Left at 0 it made
    // every 3.0 FFC script act on FFC 1: the VulturPeak cart made FFC 1 solid, not itself.
    if(type == SCRIPT_FFC && zasm30_ffscript_version)
        f.inst[0] = zasm30_ffscript_version >= 26 ? 0x7FF00000L + ri->ffcref : ri->ffcref * 10000;
}

// The script ran off its end, or quit.
static void zasm30_script_finished(byte type, word script, Zasm30Instance *in)
{
    switch(type)
    {
    case SCRIPT_ITEM:
        // A passive item's script that quits stays quit, as upstream's does.
        if(zasm30_item_passive[in->key] && game->get_item(in->key))
            itemsbuf[in->key].script = 0;

        zasm30_item_doscript[in->key] = 0;
        in->doscript = false;
        break;

    case SCRIPT_NPC:
    case SCRIPT_LWPN:
    case SCRIPT_EWPN:
    case SCRIPT_ITEMSPRITE:
    {
        if(Zasm30SpriteState *st = zasm30_sprite_state_if(in->key))
            st->script = 0;
        in->doscript = false;
        break;
    }
    case SCRIPT_GENERIC:
        if(script < Q30_GENERIC_SLOTS)
            zasm30_generic[script].run = false;
        in->doscript = false;
        break;
    default:
        in->doscript = false;
        break;
    }
}

static void zasm30_drop_all_state()
{
    for(std::map<std::pair<int,long>, Zasm30Instance*>::iterator it = zasm30_instances.begin(); it != zasm30_instances.end(); ++it)
        zasm30_retire_instance(it->second);

    zasm30_instances.clear();
    zasm30_sprite_states.clear();
    zasm30_ffc_waitdraw = 0;
}

extern void (*q30_sprite_destroy_hook)(long);

void q30_set_generic_script_runner(void (*fn)(word));
void q30_set_music_player(void (*fn)(long));
void q30_play_music(long ref);

// Combo triggers can start a generic script.
static void zasm30_launch_generic_slot(word slot)
{
    if(slot < Q30_GENERIC_SLOTS)
        zasm30_generic_launch(slot);
}

extern void (*q30_sprite_appearance_hook)(sprite *, int *, int *, fix *);
void q30_sprite_appearance(sprite *s, int *tile, int *flip, fix *yofs);

void q30_clear_engine()
{
    if(quest_header_zelda_version < 0x255)
        zasm30_ffscript_version = 0; // a 2.53 quest loaded after a newer one

    zasm30_drop_all_state();
    q30_set_generic_script_runner(zasm30_launch_generic_slot);
    q30_set_music_player(q30_play_music);
    q30_sprite_destroy_hook = q30_sprite_destroyed;
    q30_sprite_appearance_hook = q30_sprite_appearance;
}

void q30_sprite_destroyed(long uid)
{
    if(zasm30_ffscript_version == 0 && zasm30_instances.empty())
        return;

    if(!zasm30_sprite_states.empty())
        zasm30_sprite_states.erase(uid);

    static const int kinds[] = { SCRIPT_NPC, SCRIPT_LWPN, SCRIPT_EWPN, SCRIPT_ITEMSPRITE };

    for(int i = 0; i < 4; ++i)
        zasm30_drop_instance(kinds[i], uid);

    zasm30_sprite_released(uid);    // the objects it owned (sprite->OwnObject)
}

static int zasm30_run(byte type, word script, long key, bool)
{
    zasm30_run_key = key;
    return run_script(type, script, 0);
}

// ---- item scripts that keep running across frames ---------------------------------------------
// Quest rule 162, from FFScript::itemScriptEngine in upstream. zasm30_item_doscript holds upstream's
// states: 0 not running, 1 was just used (ran its first frame), 2 running, 4 told to stop.
// Passive items just run every frame while the Hero has them. Screen changes don't end a script:
// the scrolling loop keeps these going, and only a new or continued game resets them.

enum { Z30_QR_ITEMSCRIPTSKEEPRUNNING = 162, Z30_QR_OLD_ITEMDATA_SCRIPT_TIMING = 243,
       Z30_QR_PASSIVE_ITEM_SCRIPT_ONLY_HIGHEST = 268, Z30_QR_NOITEMWAITDRAW = 657 };

bool q30_items_keep_running()
{
    // Upstream switches the rule off for 2.55 quests older than build 44 as it reads them.
    return q30_quest_rule(Z30_QR_ITEMSCRIPTSKEEPRUNNING)
           && !(quest_header_zelda_version == 0x255 && quest_header_zelda_build < 44);
}

static bool zasm30_item_waitdraw_allowed()
{
    return !q30_quest_rule(Z30_QR_NOITEMWAITDRAW);
}

// Runs the item's script until its next Waitframe; a script that ended gives up its slot.
static void zasm30_item_run(int id)
{
    const word script = itemsbuf[id].script;
    ffscript *code = zasm30_script_for(SCRIPT_ITEM, script);

    if(!code || code[0].command == 0xFFFF)
    {
        zasm30_item_doscript[id] = 0;
        zasm30_drop_instance(SCRIPT_ITEM, id);
        return;
    }

    zasm30_item_slot_run = true;
    zasm30_run(SCRIPT_ITEM, script, id);
    zasm30_item_slot_run = false;

    if(zasm30_trace_on)
        al_trace("q30: item %d script %d state %d\n", id, (int)script, (int)zasm30_item_doscript[id]);

    if(!zasm30_item_doscript[id])
        zasm30_drop_instance(SCRIPT_ITEM, id);
}

bool q30_item_script_busy(int id)
{
    return id >= 0 && id < q30_items_end() && q30_items_keep_running() && zasm30_item_doscript[id] != 0;
}

// The player used the item.
void q30_use_item_script(int id)
{
    if(!q30_items_keep_running())
    {
        ZScriptVersion::RunScript(SCRIPT_ITEM, itemsbuf[id].script, id & 0xFFF);
        return;
    }

    zasm30_drop_instance(SCRIPT_ITEM, id);
    zasm30_item_doscript[id] = 1;
    zasm30_item_run(id);
}

static bool zasm30_item_passive_outranked(int id)
{
    return q30_quest_rule(Z30_QR_PASSIVE_ITEM_SCRIPT_ONLY_HIGHEST)
           && current_item(itemsbuf[id].family) > itemsbuf[id].fam_type;
}

// The Hero just got the item. run false is upstream's passiveitem_script(id, false), what its
// resetItems asks for: the script is set to start but doesn't run yet (a new game's
// zasm30_items_init, or in play the item engine's next turn, starts it).
void q30_passive_item_script(int id, bool run)
{
    if(id < 0 || id >= q30_items_end() || !itemsbuf[id].script || !zasm30_item_passive[id] || !q30_items_keep_running())
        return;

    zasm30_drop_instance(SCRIPT_ITEM, id);

    if(zasm30_item_passive_outranked(id))
    {
        zasm30_item_doscript[id] = 0;
        return;
    }

    zasm30_item_doscript[id] = 1;

    if(run)
        zasm30_item_run(id);
    else if(getenv("ZC_Q30_TRACE"))
        al_trace("q30: item %d's passive script %d waits for the item engine (a starting item)\n", id, (int)itemsbuf[id].script);
}

// whether the Hero owning the item runs a passive script (ZC_Q30_ITEMDUMP's survey)
bool q30_item_has_passive_script(int id)
{
    return id >= 0 && id < q30_items_end() && itemsbuf[id].script && zasm30_item_passive[id] && q30_items_keep_running();
}

// itemdata->RunScript(mode).
static void zasm30_item_run_command(int id, long mode)
{
    byte &state = zasm30_item_doscript[id];

    if(mode == 0)
        state = 4;
    else if(itemsbuf[id].script)
    {
        if(mode != 1)
            state = 2;
        else if(!state)
        {
            zasm30_drop_instance(SCRIPT_ITEM, id);
            state = 1;
        }
    }
}

static void zasm30_item_engine(bool waitdraw)
{
    if(!q30_items_keep_running())
        return;

    for(int q = 0; q < q30_items_end(); ++q)
    {
        const word script = itemsbuf[q].script;
        byte &state = zasm30_item_doscript[q];

        if(script == 0 || script >= NUMSCRIPTITEM || state == 0)
            continue;

        if(waitdraw)
        {
            std::map<std::pair<int,long>, Zasm30Instance*>::iterator it = zasm30_instances.find(std::make_pair((int)SCRIPT_ITEM, (long)q));

            if(it == zasm30_instances.end() || !it->second->waitdraw)
                continue;

            it->second->waitdraw = false;
        }

        if(zasm30_item_passive[q])
        {
            if(game->get_item(q))
            {
                if(zasm30_item_passive_outranked(q))
                {
                    state = 0;
                    zasm30_drop_instance(SCRIPT_ITEM, q);
                }
                else
                    zasm30_item_run(q);
            }

            continue;
        }

        if(state == 1)
            state = 2;      // it already ran this frame, when it was used
        else if(state == 2)
            zasm30_item_run(q);
        else if(state == 4)
        {
            state = 0;
            zasm30_drop_instance(SCRIPT_ITEM, q);
        }
    }
}

// Also what the scrolling loop calls.
void q30_item_scripts_tick(bool waitdraw)
{
    if(zasm30_ffscript_version)
        zasm30_item_engine(waitdraw);
}

// A new or continued game.
static void zasm30_items_init()
{
    for(int q = 0; q < q30_items_end(); ++q)
    {
        zasm30_drop_instance(SCRIPT_ITEM, q);
        zasm30_item_doscript[q] = (zasm30_item_passive[q] && itemsbuf[q].script && game->get_item(q)) ? 1 : 0;
    }
}

// ---- FakeZ -------------------------------------------------------------------------------------

// Gravity on a sprite's FakeZ, the way a real Z falls. FakeZ and its jump are in the script's numbers,
// the jump scaled by 100 as z's is.
static void zasm30_fake_gravity(Zasm30SpriteState &st)
{
    if(!st.fakez && !st.fakefall)
        return;

    st.fakez -= st.fakefall / 100;

    if(st.fakez < 0)
        st.fakez = st.fakefall = 0;
    else if(st.fakefall <= (long)zinit.terminalv * 10 * 10000)
        st.fakefall += (long)zinit.gravity * 10000;

    if(st.fakez <= 0 && st.fakefall > 0)
        st.fakefall = 0;
}

// ---- sprite scripts ---------------------------------------------------------------------------

// Fills a new sprite's script in from the data it was made from, once.
static Zasm30SpriteState *zasm30_npc_state(sprite *spr)
{
    Zasm30SpriteState &st = zasm30_sprite_state(spr->getUID());

    if(!st.script_started)
    {
        st.script_started = true;
        const int guy_id = spr->id & 0xFFF;
        std::map<int, dword>::const_iterator flags = zasm30_guy_moveflags.find(guy_id);

        if(flags != zasm30_guy_moveflags.end())
            st.move_flags = flags->second;

        const Q30ScriptRef *ref = q30_guy_script(guy_id);

        if(ref)
        {
            st.script = ref->script;
            memcpy(st.initd, ref->initd, sizeof(st.initd));
        }
    }

    return &st;
}

static Zasm30SpriteState *zasm30_item_state(item *spr)
{
    Zasm30SpriteState &st = zasm30_sprite_state(spr->getUID());

    if(!st.script_started)
    {
        st.script_started = true;
        const Q30ScriptRef *ref = q30_item_sprite_script(spr->id & 0xFFF);

        if(ref)
        {
            st.script = ref->script;
            memcpy(st.initd, ref->initd, sizeof(st.initd));
        }
    }

    return &st;
}

static Zasm30SpriteState *zasm30_lweapon_state(weapon *spr)
{
    Zasm30SpriteState &st = zasm30_sprite_state(spr->getUID());

    if(!st.script_started)
    {
        st.script_started = true;
        // A weapon an item made runs the script the item gave it (0 means the parent had none).
        const Q30ScriptRef *ref = spr->parentitem >= 0 ? q30_item_weapon_script(spr->parentitem) : NULL;

        if(ref)
        {
            st.script = ref->script;
            memcpy(st.initd, ref->initd, sizeof(st.initd));
        }
    }

    return &st;
}

static Zasm30SpriteState *zasm30_eweapon_state(weapon *spr)
{
    Zasm30SpriteState &st = zasm30_sprite_state(spr->getUID());

    if(!st.script_started)
    {
        st.script_started = true;
        enemy *parent = spr->parentid >= 0 ? (enemy *)guys.getByUID(spr->parentid) : NULL;
        const Q30ScriptRef *ref = parent ? q30_guy_weapon_script(parent->id & 0xFFF) : NULL;

        if(ref)
        {
            st.script = ref->script;
            memcpy(st.initd, ref->initd, sizeof(st.initd));
        }
    }

    return &st;
}

// Run one sprite's script for the normal pass, or for its Waitdraw.
static void zasm30_run_sprite(byte type, word script, long uid, bool waitdraw)
{
    if(script == 0 || !zasm30_script_for(type, script))
        return;

    Zasm30Instance *in = zasm30_instance(type, uid);

    if(!in->doscript)
        return;

    if(waitdraw)
    {
        if(!in->waitdraw)
            return;
        in->waitdraw = false;
    }

    zasm30_run(type, script, uid);
}

bool q30_run_npc_script(sprite *spr)
{
    if(!zasm30_ffscript_version)
        return false;

    enemy *e = (enemy *)spr;
    long uid = spr->getUID();
    Zasm30SpriteState *st = zasm30_npc_state(spr);

    if((canfall(spr->id) || (st->move_flags & 1)) && e->fading != fade_flicker && e->clk >= 0 && !(st->move_flags & (1 << 8)))    // move_no_fake_z
        zasm30_fake_gravity(*st);

    if(!st->script)
        return false;

    zasm30_run_sprite(SCRIPT_NPC, st->script, uid, false);

    // The script may have deleted the enemy.
    return guys.getByUID(uid) == NULL;
}

void q30_run_npc_scripts(bool waitdraw)
{
    if(!zasm30_ffscript_version)
        return;

    for(int i = 0; i < guys.Count(); ++i)
    {
        sprite *spr = guys.spr(i);
        long uid = spr->getUID();
        Zasm30SpriteState *st = zasm30_npc_state(spr);

        if(st->script)
            zasm30_run_sprite(SCRIPT_NPC, st->script, uid, waitdraw);

        if(guys.getByUID(uid) == NULL && i < guys.Count() && guys.spr(i) != spr)
            --i;
    }
}

void q30_run_lweapon_scripts(bool waitdraw)
{
    if(!zasm30_ffscript_version)
        return;

    for(int i = 0; i < Lwpns.Count(); ++i)
    {
        sprite *spr = Lwpns.spr(i);
        long uid = spr->getUID();
        Zasm30SpriteState *st = zasm30_lweapon_state((weapon *)spr);

        if(!waitdraw && (st->move_flags & 1) && !(st->move_flags & (1 << 8)))
            zasm30_fake_gravity(*st);

        if(st->script)
            zasm30_run_sprite(SCRIPT_LWPN, st->script, uid, waitdraw);

        if(Lwpns.getByUID(uid) == NULL && i < Lwpns.Count() && Lwpns.spr(i) != spr)
            --i;
    }
}

void q30_run_eweapon_scripts(bool waitdraw)
{
    if(!zasm30_ffscript_version)
        return;

    for(int i = 0; i < Ewpns.Count(); ++i)
    {
        sprite *spr = Ewpns.spr(i);
        long uid = spr->getUID();
        Zasm30SpriteState *st = zasm30_eweapon_state((weapon *)spr);

        if(!waitdraw && (st->move_flags & 1) && !(st->move_flags & (1 << 8)))
            zasm30_fake_gravity(*st);

        if(st->script)
            zasm30_run_sprite(SCRIPT_EWPN, st->script, uid, waitdraw);

        if(Ewpns.getByUID(uid) == NULL && i < Ewpns.Count() && Ewpns.spr(i) != spr)
            --i;
    }
}

void q30_run_itemsprite_scripts(bool waitdraw)
{
    if(!zasm30_ffscript_version)
        return;

    for(int i = 0; i < items.Count(); ++i)
    {
        sprite *spr = items.spr(i);
        long uid = spr->getUID();
        Zasm30SpriteState *st = zasm30_item_state((item *)spr);

        if(st->script)
            zasm30_run_sprite(SCRIPT_ITEMSPRITE, st->script, uid, waitdraw);

        if(items.getByUID(uid) == NULL && i < items.Count() && items.spr(i) != spr)
            --i;
    }
}

// ---- hero, dmap, combo ----------------------------------------------------------------------

static bool zasm30_hero_running[Q30_HERO_SLOTS];

static void zasm30_run_hero(int slot)
{
    if(slot >= Q30_HERO_SLOTS || !q30_hero_scripts[slot] || q30_hero_scripts[slot][0].command == 0xFFFF)
        return;

    zasm30_run(SCRIPT_LINK, slot, slot);
}

void q30_run_hero_active()
{
    if(zasm30_ffscript_version)
    {
        if(Zasm30SpriteState *hero = zasm30_sprite_state_if(Link.getUID()))
            zasm30_fake_gravity(*hero);

        zasm30_run_hero(2);
    }
}

void q30_run_hero_script(int slot)
{
    if(!zasm30_ffscript_version)
        return;

    zasm30_drop_instance(SCRIPT_LINK, slot);
    zasm30_run_hero(slot);
}

// A script run from a menu loop: runs until it quits, a frame at a time, drawing only what it draws.
static void zasm30_menu_script(byte type, word script, long key)
{
    Zasm30Instance *in = zasm30_instance(type, key);

    extern void doScriptMenuDraws();

    while(in->doscript && !Quit)
    {
        script_drawing_commands.Clear();
        load_control_state();
        zasm30_run(type, script, key);

        if(in->waitdraw && in->doscript)
        {
            zasm30_run(type, script, key);
            in->waitdraw = false;
        }

        clear_bitmap(framebuf);
        animate_combos();
        doScriptMenuDraws();
        advanceframe(true);
    }

    script_drawing_commands.Clear();
    zasm30_drop_instance(type, key);
}

void q30_run_ondeath()
{
    if(zasm30_ffscript_version && q30_hero_scripts[3] && q30_hero_scripts[3][0].command != 0xFFFF)
    {
        zasm30_drop_instance(SCRIPT_LINK, 3);
        zasm30_menu_script(SCRIPT_LINK, 3, 3);
    }
}

void q30_run_onwin()
{
    if(zasm30_ffscript_version && q30_hero_scripts[4] && q30_hero_scripts[4][0].command != 0xFFFF)
    {
        zasm30_drop_instance(SCRIPT_LINK, 4);
        zasm30_menu_script(SCRIPT_LINK, 4, 4);
    }
}

void q30_run_onsave()
{
    if(zasm30_ffscript_version && q30_global_scripts[7] && q30_global_scripts[7][0].command != 0xFFFF)
    {
        zasm30_drop_instance(SCRIPT_GLOBAL, 7);
        zasm30_menu_script(SCRIPT_GLOBAL, 7, 7);
    }
}

static int zasm30_last_dmap_run = -1;

static void zasm30_reset_dmap_if_changed()
{
    if(currdmap != zasm30_last_dmap_run)
    {
        zasm30_last_dmap_run = currdmap;
        zasm30_drop_instance(SCRIPT_DMAP, 0);
        zasm30_drop_instance(SCRIPT_PASSIVESUB, 0);
    }
}

void q30_run_dmap_active()
{
    if(!zasm30_ffscript_version)
        return;

    zasm30_reset_dmap_if_changed();
    const Q30ScriptRef *ref = q30_dmap_script(currdmap, q30_dmap_active);

    if(ref && ref->script)
        zasm30_run(SCRIPT_DMAP, ref->script, 0);
}

void q30_run_passive_subscreen()
{
    if(!zasm30_ffscript_version)
        return;

    zasm30_reset_dmap_if_changed();
    const Q30ScriptRef *ref = q30_dmap_script(currdmap, q30_dmap_passive_subscreen);

    if(ref && ref->script)
        zasm30_run(SCRIPT_PASSIVESUB, ref->script, 0);
}

static std::vector<word> zasm30_combo_cache;
static int zasm30_combo_cache_screen = -1;

// The current screen's combo scripts (q30_run_combo_scripts: in a 3.0 scrolling region, each
// screen of it in turn, with its own instances).
static void zasm30_combo_scripts_here(int waitdraw)
{
    const int screen_id = currmap * MAPSCRS + currscr;

    if(zasm30_combo_cache.size() != 7 * 176 || zasm30_combo_cache_screen != screen_id)
    {
        zasm30_combo_cache.assign(7 * 176, 0xFFFF);
        zasm30_combo_cache_screen = screen_id;

        for(int layer = 0; layer < 7; ++layer)
            for(int pos = 0; pos < 176; ++pos)
                zasm30_drop_instance(SCRIPT_COMBO, (layer << 16) | pos);
    }

    for(int layer = 0; layer < 7; ++layer)
    {
        extern bool zasm30_combo_layer_on(int layer);

        if(!zasm30_combo_layer_on(layer))
            continue;

        mapscr *m = layer ? &tmpscr2[layer - 1] : tmpscr;

        if(layer && m->valid == 0)
            continue;

        for(int pos = 0; pos < 176; ++pos)
        {
            const word cid = m->data[pos];
            const long key = (layer << 16) | pos;

            if(zasm30_combo_cache[layer * 176 + pos] != cid)
            {
                zasm30_combo_cache[layer * 176 + pos] = cid;
                zasm30_drop_instance(SCRIPT_COMBO, key);
            }

            const Q30ScriptRef *ref = q30_combo_script(cid);

            if(!ref || !ref->script || !zasm30_script_for(SCRIPT_COMBO, ref->script))
                continue;

            Zasm30Instance *in = zasm30_instance(SCRIPT_COMBO, key);

            if(!in->doscript)
                continue;

            if(waitdraw)
            {
                if(!in->waitdraw)
                    continue;
                in->waitdraw = false;
            }

            zasm30_run(SCRIPT_COMBO, ref->script, key);
        }
    }
}

void q30_run_combo_scripts(bool waitdraw)
{
    if(!zasm30_ffscript_version)
        return;

    if(!q30_region_each_screen(waitdraw ? Q30_EACH_COMBO_WAITDRAW : Q30_EACH_COMBO_SCRIPTS, zasm30_combo_scripts_here, waitdraw))
        zasm30_combo_scripts_here(waitdraw);
}

// The second half of the current screen's FFC scripts that called Waitdraw (in a 3.0 scrolling
// region, each screen of it in turn).
static void zasm30_ffc_waitdraw_here(int)
{
    if(!zasm30_ffc_waitdraw)
        return;

    for(byte i = 0; i < MAXFFCS; i++)
    {
        if((zasm30_ffc_waitdraw & (1u << i)) && tmpscr->ffscript[i])
        {
            zasm30_ffc_waitdraw &= ~(1u << i);
            ZScriptVersion::RunScript(SCRIPT_FFC, tmpscr->ffscript[i], i);
        }
    }

    zasm30_ffc_waitdraw = 0;
}

void q30_run_ffc_waitdraw()
{
    if(!zasm30_ffscript_version)
        return;

    if(!q30_region_each_screen(Q30_EACH_FFC_WAITDRAW, zasm30_ffc_waitdraw_here, 0))
        zasm30_ffc_waitdraw_here(0);
}

void q30_run_screen_waitdraw() {}

// The second half of a hero, DMap or passive subscreen script that called Waitdraw.
static void zasm30_run_waitdraw_half(byte type, word script, long key)
{
    std::map<std::pair<int,long>, Zasm30Instance*>::iterator it = zasm30_instances.find(std::make_pair((int)type, key));

    if(!script || it == zasm30_instances.end() || !it->second->waitdraw || !it->second->doscript)
        return;

    it->second->waitdraw = false;
    zasm30_run(type, script, key);
}

// Runs after the global script's Waitdraw half, with the generic timing at POST_PLAYER_WAITDRAW.
// Each script group runs its Waitdraw half, then the generic scripts waiting for the point that
// follows it, in upstream's order (zc/zelda.cpp game_loop). This used to run the WAITDRAW point
// here, which walked the generic timing all the way round to the next frame's WAITDRAW first, so
// every generic script ran twice a frame.
void q30_run_waitdraw_scripts()
{
    if(!zasm30_ffscript_version)
        return;

    if(q30_hero_scripts[2] && q30_hero_scripts[2][0].command != 0xFFFF)
        zasm30_run_waitdraw_half(SCRIPT_LINK, 2, 2);
    zasm30_generic_passive(Z30T_POST_PLAYER_WAITDRAW);

    const Q30ScriptRef *dmap = q30_dmap_script(currdmap, q30_dmap_active);
    if(dmap)
        zasm30_run_waitdraw_half(SCRIPT_DMAP, dmap->script, 0);
    zasm30_generic_passive(Z30T_POST_DMAPDATA_ACTIVE_WAITDRAW);

    const Q30ScriptRef *passive = q30_dmap_script(currdmap, q30_dmap_passive_subscreen);
    if(passive)
        zasm30_run_waitdraw_half(SCRIPT_PASSIVESUB, passive->script, 0);
    zasm30_generic_passive(Z30T_POST_DMAPDATA_PASSIVESUBSCREEN_WAITDRAW);

    q30_run_screen_waitdraw();
    zasm30_generic_passive(Z30T_POST_SCREEN_WAITDRAW);
    q30_run_ffc_waitdraw();
    zasm30_generic_passive(Z30T_POST_FFC_WAITDRAW);
    q30_run_combo_scripts(true);
    zasm30_generic_passive(Z30T_POST_COMBO_WAITDRAW);
    zasm30_item_engine(true);
    zasm30_generic_passive(Z30T_POST_ITEM_WAITDRAW);
    q30_run_npc_scripts(true);
    zasm30_generic_passive(Z30T_POST_NPC_WAITDRAW);
    q30_run_eweapon_scripts(true);
    zasm30_generic_passive(Z30T_POST_EWPN_WAITDRAW);
    q30_run_lweapon_scripts(true);
    zasm30_generic_passive(Z30T_POST_LWPN_WAITDRAW);
    q30_run_itemsprite_scripts(true);
    zasm30_generic_passive(Z30T_POST_ITEMSPRITE_WAITDRAW);
}

// Set while init_game or cont_game opens the screen before the global script is set up for the
// game (quests with the old Init timing, and continues): q30_warp_script_check then runs nothing.
bool q30_warp_scripts_held = false;

// How many times q30_warp_script_check has run the global script: init_game sees whether the
// opening gave it its first frame.
int q30_warp_global_runs = 0;

// Upstream's FFScript::warpScriptCheck, for newer quests. Before the engine holds the game for a
// screen's opening wipe, a warp's fade or zap, or the frame before a tile warp, "Scripts Draw
// During Warps" (qr_SCRIPTDRAWSINWARPS) runs the global, item, hero, DMap and passive subscreen
// scripts for one frame and then their Waitdraw halves, so what they set up and draw is there
// through it: at a game start Mystical Harps' HUD, A item and mermaid Ariana show through the
// opening, as upstream, not only once it ends. Without that rule, "Passive Subscreen Script Runs
// When Game Is Frozen" runs the passive subscreen script alone. (Upstream also runs a preload
// screen script here; this engine doesn't keep that flag.) Nothing for 2.53 quests.
// qr_SCRIPTDRAWSINWARPS (rule 165). Upstream switches it off for 2.55 quests older than build 44
// as it reads them (the same block that clears qr_ITEMSCRIPTSKEEPRUNNING, q30_items_keep_running).
static bool q30_scripts_draw_in_warps()
{
    return q30_quest_rule(165) && !(quest_header_zelda_version == 0x255 && quest_header_zelda_build < 44);
}

void q30_warp_script_check()
{
    // (and never from inside a running script: they don't nest here)
    if(!zasm30_ffscript_version || q30_warp_scripts_held || zasm30_run_depth > 0)
        return;

    const Q30ScriptRef *passive = q30_dmap_script(currdmap, q30_dmap_passive_subscreen);

    if(q30_scripts_draw_in_warps())     // (runWarpScripts(false), then (true))
    {
        if(g_doscript)
        {
            ZScriptVersion::RunScript(SCRIPT_GLOBAL, GLOBAL_SCRIPT_GAME);
            ++q30_warp_global_runs;
        }

        zasm30_item_engine(false);
        zasm30_run_hero(2);         // the hero's active script alone (no FakeZ step: not a frame)
        q30_run_dmap_active();
        q30_run_passive_subscreen();

        if(global_wait)
        {
            ZScriptVersion::RunScript(SCRIPT_GLOBAL, GLOBAL_SCRIPT_GAME);
            global_wait = false;
        }

        zasm30_item_engine(true);

        if(q30_hero_scripts[2] && q30_hero_scripts[2][0].command != 0xFFFF)
            zasm30_run_waitdraw_half(SCRIPT_LINK, 2, 2);

        const Q30ScriptRef *dmap = q30_dmap_script(currdmap, q30_dmap_active);
        if(dmap)
            zasm30_run_waitdraw_half(SCRIPT_DMAP, dmap->script, 0);
        if(passive)
            zasm30_run_waitdraw_half(SCRIPT_PASSIVESUB, passive->script, 0);
    }
    else if(q30_quest_rule(684))    // qr_PASSIVE_SUBSCRIPT_RUNS_WHEN_GAME_IS_FROZEN
    {
        q30_run_passive_subscreen();

        if(passive)
            zasm30_run_waitdraw_half(SCRIPT_PASSIVESUB, passive->script, 0);
    }
}

void q30_run_onlaunch()
{
    if(zasm30_ffscript_version && q30_global_scripts[4] && q30_global_scripts[4][0].command != 0xFFFF)
    {
        zasm30_drop_instance(SCRIPT_GLOBAL, 4);
        zasm30_menu_script(SCRIPT_GLOBAL, 4, 4);
    }
}

void q30_run_continue()
{
    // upstream cont_game: generic scripts set to end or restart on a continue do, then CONTINUE
    q30_generic_time_exit(Z30ST_CONTINUE);
    q30_throw_generic_event(Z30EV_CONTINUE);

    if(zasm30_ffscript_version)
        zasm30_items_init();

    if(zasm30_ffscript_version && q30_global_scripts[5] && q30_global_scripts[5][0].command != 0xFFFF)
    {
        zasm30_drop_instance(SCRIPT_GLOBAL, 5);
        zasm30_run(SCRIPT_GLOBAL, 5, 5);
        zasm30_drop_instance(SCRIPT_GLOBAL, 5);
    }
}

static void zasm30_update_input();

static void zasm30_frame_start();
static void z30_scrdata_savetest();
static void zasm30_subdump_tick();
static void z30_clear_script_lights();

void q30_gen_timing(int timing)
{
    if(!zasm30_ffscript_version)
        return;

    if(timing == Z30T_START_FRAME)
    {
        zasm30_frame_start();
        z30_scrdata_savetest();
        zasm30_subdump_tick();

        // with the frame's other script drawing, which game_loop now clears just before this stage
        // (upstream), and keeps while a message freezes the game
        const bool freezemsg = (msg_active || (intropos && intropos < 72) || (linkedmsgclk && get_bit(quest_rules, qr_MSGDISAPPEAR)))
                               && get_bit(quest_rules, qr_MSGFREEZE);
        if(!freezemsg)
            z30_clear_script_lights();
    }

    if(timing == Z30T_POST_POLL_INPUT)
        zasm30_update_input();

    zasm30_generic_passive(timing);

    // Item scripts run just after the Hero, or before for quests made before that timing changed.
    const bool old_timing = q255_compat_rule_version < 4 || q30_quest_rule(Z30_QR_OLD_ITEMDATA_SCRIPT_TIMING);

    if(timing == (old_timing ? Z30T_POST_EWPN_SCRIPT : Z30T_POST_PLAYER_ANIMATE))
        zasm30_item_engine(false);
}

bool q30_scripts_active()
{
    return zasm30_ffscript_version != 0;
}

// The last frame's remaining generic stages (POST_STRINGS, END_FRAME), run before the next frame
// clears the script drawing, as upstream runs them at the end of its frame. Nothing if they ran.
void q30_gen_frame_end()
{
    if(!zasm30_ffscript_version || zasm30_genscript_timing == Z30T_START_FRAME)
        return;

    zasm30_generic_passive(Z30T_END_FRAME);
}

// The scrolling loop (LinkClass::scrollscr) runs no game_loop, but upstream keeps the generic
// scripts going through it (run_generic_scrolling, run_scrolling_script): each frame they run
// every stage up to this one.
void q30_scroll_generic(int timing)
{
    if(zasm30_ffscript_version)
        zasm30_generic_passive(timing);
}

static void z30_reset_script_objects();
static void zasm30_reset_register_state();
static void z30_scrdata_game_init(bool first);     // q30_scripts_save.h

void q30_game_init(bool first)
{
    // limits a newer quest's script set (Game->MaxNPCs ...) end with that game
    guys.setMax(SLMAX);
    Lwpns.setMax(SLMAX);
    Ewpns.setMax(SLMAX);
    items.setMax(SLMAX);

    if(!zasm30_ffscript_version)
        return;

    z30_reset_script_objects();
    zasm30_reset_register_state();
    z30_scrdata_game_init(first);
    zasm30_items_init();

    // Newer scripts draw to the screen unless they ask otherwise. init_game runs before the
    // quest's script format is known, so this is the place that can tell.
    zscriptDrawingRenderTarget->SetCurrentRenderTarget(-1);

    zasm30_count_generic();
    zasm30_genscript_timing = Z30T_START_FRAME;

    if(getenv("ZC_Q30_TRACE"))     // which generic scripts start, and what ends them or wakes them
        for(int q = 1; q <= zasm30_max_generic; ++q)
            if(q30_generic_scripts[q])
                al_trace("q30: generic slot %d: run %d exit %04x reload %04x events %08lx\n", q, (int)zasm30_generic[q].run,
                         (unsigned)zasm30_generic[q].exit_state, (unsigned)zasm30_generic[q].reload_state, (unsigned long)zasm30_generic[q].event_state);

    for(int q = 1; q < Q30_GENERIC_SLOTS; ++q)
    {
        if(zasm30_generic[q].loaded)
        {
            if(first)
            {
                zasm30_generic[q].waituntil = Z30T_START_FRAME;
                zasm30_generic[q].atleast = true;
            }
            // A saved game keeps which ones ran through the quest's data here.
        }
    }

    // A loaded game: generic scripts set to end or restart on a reload do (upstream init_game).
    if(!first)
        q30_generic_time_exit(Z30ST_RELOAD);

    // Each generic that starts running does so at the game's start, then gets the INIT event.
    zasm30_generic_passive(Z30T_INIT);
    q30_throw_generic_event(Z30EV_INIT);
}

#endif

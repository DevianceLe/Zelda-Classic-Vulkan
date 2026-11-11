// ZQuest's store for 2.55 and 3.0 Nightly quests: what the player's section readers
// (src/port/game/q255_*.cpp) hand over that 2.53's structures have no room for.
//
// The player keeps the same data in its game code (q30_triggers.cpp, q30_audio.cpp,
// q30_core.cpp and the q30_scripts_*.h headers inside ffscript.cpp). ZQuest has none of
// that, so these are the same functions as plain storage: nothing here plays, draws or
// changes a value, so the quest writers can save back exactly what was read.
//
// A 2.53 quest never reaches the readers, so zq255_store_clear() runs on every load and
// on File > New to drop whatever the last newer quest left.

#include "precompiled.h" //always first

#include <string.h>
#include <stdlib.h>
#include <map>
#include <string>
#include <vector>

#include "zdefs.h"
#include "sfx.h"
#include "q255.h"
#include "q30.h"
#include "q30_audio.h"
#include "q30_scripts.h"
#include "q30_triggers.h"
#include "zq_q255_store.h"

extern newcombo *combobuf;
extern itemdata *itemsbuf;
extern SAMPLE customsfxdata[WAV_COUNT];

void zasm30_reset();
extern long zasm30_ffscript_version;

// ---- the store -----------------------------------------------------------------------

Zq255Store zq255;

Zq255Store::Zq255Store()
{
    q255_keep_for_editor = true;    // the readers may keep what ZQuest's writers need (q255.h)
    memset(combos, 0, sizeof(combos));
    memset(high_sfx, 0, sizeof(high_sfx));
    clear();
}

void Zq255Store::clear()
{
    for(int i = 0; i < MAXCOMBOS; ++i)
    {
        delete combos[i];
        combos[i] = NULL;
    }

    rules.clear();
    item_family.clear();
    item_usesound2.clear();
    item_display_name.clear();
    item_override_flags.clear();
    item_passive_script.clear();
    item_sprite_script.clear();
    item_weapon_script.clear();
    guy_defenses.clear();
    guy_script.clear();
    guy_weapon_script.clear();
    guy_moveflags.clear();
    guy_wpnsprite.clear();
    bottle_types.clear();
    bottle_shops.clear();
    init_bottle_slots.clear();
    misc_sfx.clear();
    misc_sprites.clear();
    init_switch_style = 0;
    item_box_set = false;
    memset(item_box, 0, sizeof(item_box));
    light_init_set = false;
    memset(light_init, 0, sizeof(light_init));
    placed_flags.clear();
    rooms.clear();
    msg_wrap_extra.clear();
    for(int i = 0; i < q30_dmap_kinds; ++i)
        dmap_scripts[i].clear();
    dmap_subscreens.clear();
    music.clear();
    dmap_music.clear();
    screen_music.clear();
    clear_sfx();
    high_tunes.clear();
    generic_init.clear();
    screen_data_init.clear();
    subscreens.clear();
    screen_scripts.clear();
}

void Zq255Store::clear_sfx()
{
    ogg_sfx.clear();

    for(int i = 0; i < Q30_MAX_SFX - WAV_COUNT; ++i)
    {
        if(high_sfx[i].data)
            free(high_sfx[i].data);

        memset(&high_sfx[i], 0, sizeof(high_sfx[i]));
        high_sfx[i].bits = 8;
        high_sfx[i].freq = 22050;
        high_sfx[i].priority = 128;
    }
}

void zq255_store_clear()
{
    zq255.clear();
    q30_script_tables_reset();
    zasm30_reset();
    zasm30_ffscript_version = 0;
}

extern zquestheader header;

bool zq255_quest_is_newer()
{
    return header.zelda_version >= 0x255;
}

int zq_tile_limit()
{
    return zq255_quest_is_newer() ? NEWMAXTILES : TILES_PER_PAGE * 252;
}

namespace
{

void put_ref(std::map<int, Q30ScriptRef> &table, int key, word script, const long *initd)
{
    if(!script && !initd)
    {
        table.erase(key);
        return;
    }

    Q30ScriptRef r;
    r.script = script;
    if(initd)
        memcpy(r.initd, initd, sizeof(r.initd));
    table[key] = r;
}

} // namespace

// ---- combos and rules (q30_triggers.h) ---------------------------------------------

void q30_combos_clear()
{
    for(int i = 0; i < MAXCOMBOS; ++i)
    {
        delete zq255.combos[i];
        zq255.combos[i] = NULL;
    }
}

void q30_combo_store(int id, Q30Combo *data)
{
    if(id < 0 || id >= MAXCOMBOS)
    {
        delete data;
        return;
    }

    delete zq255.combos[id];
    zq255.combos[id] = data;
}

const Q30Combo *q30_combo(int id)
{
    if(id < 0 || id >= MAXCOMBOS)
        return NULL;

    return zq255.combos[id];
}

int q30_combo_type(int id)
{
    const Q30Combo *c = q30_combo(id);
    return c ? c->type : combobuf[id].type;
}

void q30_set_quest_rules(const byte *rules, size_t bytes)
{
    zq255.rules.assign(rules, rules + bytes);
}

void q30_trigger_stats() {}
void q30_slope_audit() {}

// ---- items and enemies ---------------------------------------------------------------

void q30_clear_item_families() { zq255.item_family.clear(); }
void q30_set_item_family(int item, long family) { zq255.item_family[item] = family; }

long q30_item_family(int item)
{
    if(item < 0 || item >= MAXITEMS)
        return -1;

    std::map<int, long>::const_iterator it = zq255.item_family.find(item);
    return it == zq255.item_family.end() ? (long)itemsbuf[item].family : it->second;
}

void q30_set_item_usesound2(int item, int s) { zq255.item_usesound2[item] = s; }

void q30_clear_item_extras()
{
    zq255.item_display_name.clear();
    zq255.item_override_flags.clear();
}

void q30_set_item_display_name(int item, const char *name)
{
    zq255.item_display_name[item] = name ? name : "";
}

void q30_set_item_override_flags(int item, int flags) { zq255.item_override_flags[item] = flags; }
void q30_set_item_passive_script(int id, bool on) { zq255.item_passive_script[id] = on; }

void q30_set_item_sprite_script(int item, word script, const long *initd)
{
    put_ref(zq255.item_sprite_script, item, script, initd);
}

void q30_set_item_weapon_script(int item, word script, const long *initd)
{
    put_ref(zq255.item_weapon_script, item, script, initd);
}

void q30_clear_guy_extras() { zq255.guy_wpnsprite.clear(); }

void q30_clear_guy_scripts()
{
    zq255.guy_script.clear();
    zq255.guy_weapon_script.clear();
    zq255.guy_moveflags.clear();
}

void q30_set_guy_defenses(int guy, const byte *defs)
{
    if(guy < 0)
    {
        zq255.guy_defenses.clear();
        return;
    }

    if(!defs)
    {
        zq255.guy_defenses.erase(guy);
        return;
    }

    zq255.guy_defenses[guy].assign(defs, defs + Zq255Store::GUY_DEFENSES);
}

void q30_set_guy_script(int guy, word script, const long *initd) { put_ref(zq255.guy_script, guy, script, initd); }
void q30_set_guy_weapon_script(int guy, word script, const long *initd) { put_ref(zq255.guy_weapon_script, guy, script, initd); }
void q30_set_guy_moveflags(int guy, dword flags) { zq255.guy_moveflags[guy] = flags; }
void q30_set_guy_wpnsprite(int guy, int spr) { zq255.guy_wpnsprite[guy] = spr; }

// ---- misc data, init data, maps ------------------------------------------------------

void q30_bottles_clear()
{
    zq255.bottle_types.clear();
    zq255.bottle_shops.clear();
}

void q30_set_bottle_type(int i, const Q30BottleType &t) { zq255.bottle_types[i] = t; }
void q30_set_bottle_shop(int i, const Q30BottleShop &s) { zq255.bottle_shops[i] = s; }

void q30_set_init_bottle_slots(const byte *slots, int count)
{
    zq255.init_bottle_slots.assign(slots, slots + (count > 0 ? count : 0));
}

void q30_set_misc_sfx(int i, int s) { zq255.misc_sfx[i] = s; }
void q30_set_misc_sprite(int i, int spr) { zq255.misc_sprites[i] = spr; }
void q30_set_init_switch_style(int style) { zq255.init_switch_style = style; }

void q30_set_item_box(int xofs, int yofs, int w, int h)
{
    zq255.item_box_set = true;
    zq255.item_box[0] = xofs;
    zq255.item_box[1] = yofs;
    zq255.item_box[2] = w;
    zq255.item_box[3] = h;
}

void q30_set_light_init(int dith_type, int dith_arg, int dith_perc, int light_rad, int trans_perc, int dark_color)
{
    zq255.light_init_set = true;
    zq255.light_init[0] = dith_type;
    zq255.light_init[1] = dith_arg;
    zq255.light_init[2] = dith_perc;
    zq255.light_init[3] = light_rad;
    zq255.light_init[4] = trans_perc;
    zq255.light_init[5] = dark_color;
}

void q30_clear_placed_flags() { zq255.placed_flags.clear(); }

void q30_note_placed_flag(int screen_index, int pos, byte flag)
{
    zq255.placed_flags[screen_index * 176 + pos] = flag;
}

void q30_note_room(int screen_index, int room) { zq255.rooms[screen_index] = room; }

void q30_clear_tables()
{
    for(int i = 0; i < q30_dmap_kinds; ++i)
        zq255.dmap_scripts[i].clear();

    zq255.dmap_subscreens.clear();
}

void q30_set_msg_wrap_extra(int str, int extra) { zq255.msg_wrap_extra[str] = extra; }

void q30_set_dmap_script(int dmap, int kind, word script, const long *initd)
{
    if(kind >= 0 && kind < q30_dmap_kinds)
        put_ref(zq255.dmap_scripts[kind], dmap, script, initd);
}

void q30_set_dmap_subscreens(int dmap, int overlay, int map)
{
    zq255.dmap_subscreens[dmap] = std::make_pair(overlay, map);
}

void q30_set_dmap_texts(int, const char *, long) {}     // the player's (titles and intros as drawn)
void q30_set_dmap_floor(int, int) {}                    // the player's (subscreen conditions)
void q30_set_dmap_mirror(int, int) {}                   // the player's (the Magic Mirror)
int q30_dmap_mirror(int) { return -1; }                 // (for the player's usage dump)
void q30_item_pickup_info(int, long *pickup, int *mode, int *pstring, int *psflags)   // (the same)
{
    *pickup = 0;
    *mode = *pstring = *psflags = 0;
}
int q30_shop_string(int, int) { return 0; }             // (the same)
void q30_set_bunny_ltm(long) {}                         // the player's (the bunny form)
void q30_set_item_bunny(int, bool) {}                   // the player's (the bunny form)
void q30_set_item_pickup(int, long, int, int, int) {}  // the player's (item pickup flags and strings)
void q30_set_shop_string(int, int, int) {}              // the player's (shop item strings)
void q30_set_item_flags32(int, dword) {}                // the player's (item flags past 16 bits)
void q30_set_item_cost(int, int, int, int, int) {}      // the player's (both item costs)
void q30_sub_trans(int, int, const Q30SubTrans &) {}    // the player's (page change animations)
dword q30_item_flags(int) { return 0; }                 // (for the player's usage dump)
bool q30_quest_rule(int) { return false; }              // (the same)

void q30_clear_generic_init() { zq255.generic_init.clear(); }

void q30_set_generic_init(int slot, bool run, word exit_state, word reload_state, dword event_state,
                          const long *initd, const std::map<dword, long> *data)
{
    Zq255Store::GenericInit g;
    g.run = run;
    g.exit_state = exit_state;
    g.reload_state = reload_state;
    g.event_state = event_state;
    memset(g.initd, 0, sizeof(g.initd));
    if(initd)
        memcpy(g.initd, initd, sizeof(g.initd));
    if(data)
        g.data = *data;
    zq255.generic_init[slot] = g;
}

void q30_clear_screen_data_init() { zq255.screen_data_init.clear(); }

void q30_set_screen_data_init(int index, long size, int key, long value)
{
    Zq255Store::ScreenData &d = zq255.screen_data_init[index];
    d.size = size;
    if(key >= 0)
        d.values[key] = value;
}

// ---- subscreens ----------------------------------------------------------------------

void q30_sub_clear() { zq255.subscreens.clear(); }

void q30_sub_begin(int list, int slot, const char *name)
{
    Zq255Store::Subscreen s;
    s.list = list;
    s.slot = slot;
    s.name = name ? name : "";
    zq255.subscreens.push_back(s);
}

void q30_sub_page(int list, int cursor_pos)
{
    if(zq255.subscreens.empty())
        return;

    Zq255Store::SubPage p;
    p.cursor_pos = cursor_pos;
    zq255.subscreens.back().pages.push_back(p);
    (void)list;
}

// Page changes are the player's: ZQuest's writers keep a subscreen's own bytes.
void q30_sub_settings(int, dword, int, int) {}
void q30_sub_page_trans(int, int, int, int, int) {}
void q30_sub_page_objects(int, const subscreen_object *, int) {}
void q30_sub_widget_cond(int, int, int, const Q30SubCond &) {}     // the player's
void q30_sub_widget_seltext(int, const char *) {}                   // the player's

void q30_sub_widget(int list, int type, const char *label, int obj, int posflags, int x, int y, int w, int h,
                    int pos, dword genflags, dword flags, int, int, int)
{
    if(zq255.subscreens.empty())
        return;

    Zq255Store::Subscreen &s = zq255.subscreens.back();
    if(s.pages.empty())
        s.pages.push_back(Zq255Store::SubPage());

    Zq255Store::SubWidget wd;
    wd.type = type;
    wd.label = label ? label : "";
    wd.obj = obj;
    wd.posflags = posflags;
    wd.x = x;
    wd.y = y;
    wd.w = w;
    wd.h = h;
    wd.pos = pos;
    wd.genflags = genflags;
    wd.flags = flags;
    s.pages.back().widgets.push_back(wd);
    (void)list;
}

// ---- music and sound (q30_audio.h) ---------------------------------------------------

int q30_read_advmusic(Q255Section &s)
{
    if(s.version > 1)
        return qe_version;

    word count = s.igetw();

    if(s.failed)
        return qe_invalid;

    if(s.keep)
        zq255.music.clear();

    for(int i = 0; i < count; ++i)
    {
        Zq255Store::Music m;
        m.name = s.getwstr();
        m.path = s.getcstr();

        if(!m.path.empty())
        {
            m.loop_start = s.igetl_s();
            m.loop_end = s.igetl_s();
            m.xfade_in = s.igetl_s();
            m.xfade_out = s.igetl_s();
            m.track = s.getc();
        }

        m.midi = s.igetw_s();

        if(s.failed)
            return qe_invalid;

        if(s.keep)
            zq255.music.push_back(m);
    }

    return s.failed ? qe_invalid : 0;
}

void q30_audio_clear_screens() { zq255.screen_music.clear(); }

void q30_audio_clear_dmaps()
{
    zq255.music.clear();
    zq255.dmap_music.clear();
}

int q30_music_add(const char *name, const char *path, long loop_start, long loop_end, long xfade_in, long xfade_out, byte track, short midi)
{
    Zq255Store::Music m;
    m.name = name ? name : "";
    m.path = path ? path : "";
    m.loop_start = loop_start;
    m.loop_end = loop_end;
    m.xfade_in = xfade_in;
    m.xfade_out = xfade_out;
    m.track = track;
    m.midi = midi;
    zq255.music.push_back(m);
    return (int)zq255.music.size();
}

void q30_set_dmap_music(int dmap, int music, bool new_format)
{
    zq255.dmap_music[dmap] = std::make_pair(music, new_format);
}

void q30_set_screen_music(int index, long music)
{
    if(music > 0)
        zq255.screen_music[index] = music;
    else
        zq255.screen_music.erase(index);
}

// The player copies a music object's file and MIDI into the 2.53 DMap fields so its own
// music code can play them. ZQuest leaves the DMap fields as the quest has them.
void q30_finish_music_tables() {}

void q30_sfx_clear() { zq255.clear_sfx(); }

SAMPLE *q30_sfx_slot(int id)
{
    if(id <= 0 || id >= Q30_MAX_SFX)
        return NULL;

    return id < WAV_COUNT ? &customsfxdata[id] : &zq255.high_sfx[id - WAV_COUNT];
}

void q30_sfx_store_ogg(int id, const byte *data, dword len)
{
    if(id <= 0 || id >= Q30_MAX_SFX)
        return;

    zq255.ogg_sfx[id].assign(data, data + len);
}

// The player borrows a free low slot so a byte-sized screen field can name a sound past
// 255. ZQuest doesn't play these sounds, and borrowing would change the quest's sounds.
int q30_sfx_alias(int high)
{
    (void)high;
    return 0;
}

void q30_sfx_finish_aliases() {}

void q30_tunes_clear()
{
    for(size_t i = 0; i < zq255.high_tunes.size(); ++i)
        zq255.high_tunes[i].reset();

    zq255.high_tunes.clear();
}

void q30_store_tune(int index, const char *title, long start, long loop_start, long loop_end, short loop, short volume, byte flags, byte format, void *midi)
{
    if(index < MAXCUSTOMTUNES)
        return;

    size_t at = (size_t)(index - MAXCUSTOMTUNES);

    if(at >= zq255.high_tunes.size())
        zq255.high_tunes.resize(at + 1);

    zctune &t = zq255.high_tunes[at];
    strncpy(t.title, title ? title : "", sizeof(t.title) - 1);
    t.title[sizeof(t.title) - 1] = 0;
    t.start = start;
    t.loop_start = loop_start;
    t.loop_end = loop_end;
    t.loop = loop;
    t.volume = volume;
    t.flags = flags;
    t.format = format;
    t.data = midi;
}

// ---- scripts (the player's ffscript.cpp) ---------------------------------------------

struct Zasm30Literal
{
    std::string str;
    std::vector<long> vec;
};

ffscript *zasm30_stream = NULL;
std::map<const ffscript *, Zasm30Literal> zasm30_literals;
std::vector<std::map<int, long> > zasm30_inst_inits;
long zasm30_ffscript_version = 0;

void zasm30_reset()
{
    delete [] zasm30_stream;
    zasm30_stream = NULL;
    zasm30_literals.clear();
    zasm30_inst_inits.clear();
}

void zasm30_clear_screen_scripts() { zq255.screen_scripts.clear(); }

void zasm30_set_screen_script(int index, word script, const long *initd)
{
    put_ref(zq255.screen_scripts, index, script, initd);
}

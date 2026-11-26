// 3.0 scrolling regions, player only: sprites away from the camera, and the test switches that
// drive and trace a region. See q30_regions.h.
//
// Upstream (sprite.cpp, sprite_list::animate): in a scrolling region a sprite whose tile area and
// hit box both lie outside the viewport grown by its "viewport suspend range" is suspended: it
// isn't animated and its script doesn't run until the camera comes back; an enemy with segments
// stays awake while any part of it is in range. One whose "viewport despawn range" is set is
// removed once it is that far out. Enemies and their weapons have a suspend range of 48 pixels
// unless the quest says otherwise; items and Link's weapons have none. A weapon takes the ranges of
// the weapon data it was fired with (weapon::load_weap_data): its item's, its enemy's (48 unless
// the quest says otherwise) or a combo's own; an FFC has its own two (MAP v40+, 0: never).

#include "precompiled.h" //always first

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "zelda.h"
#include "zdefs.h"
#include "sprite.h"
#include "guys.h"
#include "items.h"
#include "weapons.h"
#include "link.h"
#include "q30_regions.h"
#include "q30_triggers.h"
#include "zc_sys.h"

extern LinkClass Link;
extern sprite_list guys, items, Ewpns, Lwpns;
extern bool Playing;
extern int directWpn;               // link.cpp: the item Link attacks with directly (-1: none)

// sprite.cpp (ZQuest builds it too, so it calls these only when the player has set them)
extern bool (*q30_sprite_frozen_hook)(sprite_list *list, int index);
extern bool (*q30_sprite_despawn_hook)(sprite_list *list, int index);
extern int q30_item_field[4];

namespace
{

enum { K_GUYS, K_EWPNS, K_ITEMS, K_LWPNS, KINDS, LOGGED = K_LWPNS };

// per enemy / item id, as the quest file gives them (none: the defaults)
std::vector<long> g_guy_suspend, g_guy_despawn, g_item_suspend, g_item_despawn;
// the same for the weapons an enemy or an item fires (their weapon data)
std::vector<long> g_guy_wsuspend, g_guy_wdespawn, g_item_wsuspend, g_item_wdespawn;
std::set<long> g_frozen[KINDS];     // the sprites (UIDs) the last animate left suspended
int g_frozen_frame[KINDS] = { -1, -1, -1, -1 };

// each weapon's ranges (by UID), fixed when it is first seen or fired with a combo's weapon data
struct Range
{
    long suspend, despawn;
};
std::map<long, Range> g_wpn_range;
std::set<long> g_despawned;         // the enemies (UIDs) a despawn range is removing
bool g_wpn_despawns = false;        // some weapon data of this quest has a despawn range

long range_of(const std::vector<long> &v, int id, long def)
{
    return (id >= 0 && (size_t)id < v.size()) ? v[id] : def;
}

void set_range(std::vector<long> &v, int id, long value, long def)
{
    if(id < 0 || id > 0xFFFF)
        return;

    if((size_t)id >= v.size())
        v.resize(id + 1, def);

    v[id] = value;
}

int kind_of(sprite_list *list)
{
    return list == &guys ? K_GUYS : list == &Ewpns ? K_EWPNS : list == &items ? K_ITEMS : list == &Lwpns ? K_LWPNS : -1;
}

// Upstream's weapon constructor: an enemy weapon starts with 48, then the weapon data it is fired
// with replaces both (get_weapon_data_for_id: its item's, else the enemy's that fired it).
Range weapon_range(sprite *s)
{
    const long uid = s->getUID();
    std::map<long, Range>::iterator it = g_wpn_range.find(uid);

    if(it != g_wpn_range.end())
        return it->second;

    weapon *w = (weapon *)s;
    Range r = { w->id > wEnemyWeapons ? 48 : 0, 0 };
    int item = w->parentitem;
    const bool sparkle = w->id == wSSparkle || w->id == wFSparkle;

    // (magic: the book's weapon data, else its item's, else the wand Link uses; sparkles have
    // none unless "sparkles inherit properties", qr 355)
    if(w->id == wMagic)
    {
        item = current_item_id(itype_book);

        if(item < 0 || item >= q30_items_end())
            item = w->parentitem;

        if(item < 0 || item >= q30_items_end())
            item = directWpn > -1 ? directWpn : current_item_id(itype_wand);
    }

    if(sparkle && !q30_quest_rule(355))
        ;
    else if(item >= 0 && item < q30_items_end())
    {
        r.suspend = range_of(g_item_wsuspend, item, 0);
        r.despawn = range_of(g_item_wdespawn, item, 0);
    }
    else if(w->parentid > -1)
    {
        sprite *e = guys.getByUID(w->parentid);

        if(e)
        {
            r.suspend = range_of(g_guy_wsuspend, e->id & 0xFFF, 48);
            r.despawn = range_of(g_guy_wdespawn, e->id & 0xFFF, 0);
        }
    }

    // (the weapons gone since leave their entries: drop them now and then)
    if(g_wpn_range.size() >= 512)
    {
        std::map<long, Range> live;

        for(int k = 0; k < 2; ++k)
        {
            sprite_list &l = k ? Lwpns : Ewpns;

            for(int i = 0; i < l.Count(); ++i)
                if((it = g_wpn_range.find(l.spr(i)->getUID())) != g_wpn_range.end())
                    live.insert(*it);
        }

        g_wpn_range.swap(live);
    }

    g_wpn_range[uid] = r;
    return r;
}

long suspend_range(int kind, sprite *s)
{
    switch(kind)
    {
    case K_GUYS:
        return range_of(g_guy_suspend, s->id & 0xFFF, 48);

    case K_EWPNS:
    case K_LWPNS:
        return weapon_range(s).suspend;

    default:
        return range_of(g_item_suspend, s->id, 0);
    }
}

long despawn_range(int kind, sprite *s)
{
    switch(kind)
    {
    case K_GUYS:
        return range_of(g_guy_despawn, s->id & 0xFFF, 0);

    case K_EWPNS:
    case K_LWPNS:
        return weapon_range(s).despawn;

    default:
        return range_of(g_item_despawn, s->id, 0);
    }
}

// upstream's rect_t::intersects_with
bool rects_meet(int l, int t, int r, int b, int x, int y, int w, int h)
{
    return (t > y ? t : y) < (b < y + h ? b : y + h) && (l > x ? l : x) < (r < x + w ? r : x + w);
}

// a tile area or a hit box reaches into the viewport grown by range
bool boxes_within(long range, int tx, int ty, int tw, int th, int hx, int hy, int hw, int hh)
{
    int l, t, r, b;
    q30_region_view_bounds(&l, &t, &r, &b);
    l -= range;
    t -= range;
    r += range;
    b += range;
    return rects_meet(l, t, r, b, tx, ty, tw, th) || rects_meet(l, t, r, b, hx, hy, hw, hh);
}

// the sprite's tile area or its hit box reaches into the viewport grown by range
bool within(const sprite *s, long range)
{
    return boxes_within(range, int(s->x + s->xofs), int(s->y + s->yofs), s->txsz * 16, s->tysz * 16,
                        int(s->x) + s->hxofs, int(s->y) + s->hyofs, s->hxsz, s->hysz);
}

// upstream's is_sprite_in_view: itself, or (an enemy made of several sprites: the core and, after
// it in the list, its segments, ids from 0x1000) any other part of it, or (an enemy's weapon: the
// enemy is its parent, as upstream's weapon constructor makes it) the enemy that fired it or
// another weapon of that enemy
bool in_view(sprite_list *list, int index, int kind)
{
    sprite *s = list->spr(index);
    const long range = suspend_range(kind, s);

    if(range <= 0 || within(s, range))
        return true;

    if(kind == K_EWPNS)
    {
        const long parent = ((weapon *)s)->parentid;
        sprite *e = parent > -1 ? guys.getByUID(parent) : NULL;

        if(!e)
            return false;

        const long er = suspend_range(K_GUYS, e);

        if(er <= 0 || within(e, er))
            return true;

        for(int i = 0; i < list->Count(); ++i)
        {
            sprite *o = list->spr(i);

            if(i != index && ((weapon *)o)->parentid == parent && (suspend_range(kind, o) <= 0 || within(o, suspend_range(kind, o))))
                return true;
        }

        return false;
    }

    if(kind != K_GUYS)
        return false;

    int first = index;

    while(first > 0 && (list->spr(first)->id & 0xF000))
        --first;

    for(int i = first; i < list->Count(); ++i)
    {
        sprite *o = list->spr(i);

        if(i > first && !(o->id & 0xF000))
            break;

        if(i != index && (suspend_range(kind, o) <= 0 || within(o, suspend_range(kind, o))))
            return true;
    }

    return false;
}

// What an enemy adds while it animates is its screen's (q30_regions.h). UIDs only grow, so what
// came after the newest one there was before the enemy animated is what it added.
int g_parent_scr = -1;
long g_uid_mark = 0;

void tag_children()
{
    if(g_parent_scr >= 0)
        for(int i = guys.Count() - 1; i >= 0; --i)
        {
            const long uid = guys.spr(i)->getUID();

            if(uid <= g_uid_mark)
                break;

            if(q30_region_sprite_screen(uid) < 0)
                q30_region_sprite_set_screen(uid, g_parent_scr);
        }

    g_parent_scr = -1;
}

bool frozen_hook(sprite_list *list, int index)
{
    // (item::animate removes an item that is far outside this: the screen, or the region)
    if(list == &items)
        q30_region_bounds(&q30_item_field[0], &q30_item_field[1], &q30_item_field[2], &q30_item_field[3]);

    if(!q30_region_on())
        return false;

    const int kind = kind_of(list);

    if(kind < 0)
        return false;

    // (a list animates once a frame: the first sprite asked about starts the frame's set)
    if(g_frozen_frame[kind] != frame)
    {
        g_frozen[kind].clear();
        g_frozen_frame[kind] = frame;
    }

    if(kind == K_GUYS)
    {
        tag_children();
        g_parent_scr = q30_region_sprite_screen(list->spr(index)->getUID());

        for(int i = 0; i < list->Count(); ++i)
            if(list->spr(i)->getUID() > g_uid_mark)
                g_uid_mark = list->spr(i)->getUID();
    }

    if(in_view(list, index, kind))
        return false;

    g_frozen[kind].insert(list->spr(index)->getUID());
    return true;
}

// After a sprite animated: beyond its despawn range (upstream's is_beyond_viewport_despawn_range).
// An enemy dies where it is, leaving nothing; anything else is removed (true).
bool despawn_hook(sprite_list *list, int index)
{
    if(!q30_region_on())
        return false;

    const int kind = kind_of(list);

    if(kind != K_GUYS && kind != K_ITEMS)
        return false;

    sprite *s = list->spr(index);
    const long range = despawn_range(kind, s);

    if(range <= 0 || within(s, range))
        return false;

    if(kind == K_ITEMS)
        return true;

    // (upstream: hp = -1000 and dying_despawn; the death sequence then runs with no sound, no
    // "never returns", no ringleader's kill and no item: enemy::Dead asks q30_vp_despawned)
    enemy *e = (enemy *)s;

    if(e->hp > -1000 && !e->dying)
        e->hp = -1000;

    if(g_despawned.insert(s->getUID()).second && getenv("ZC_VP_RANGELOG"))
        al_trace("q30 vp range: frame %d: enemy %d (uid %ld) despawned, %ld past the viewport\n", frame, s->id & 0xFFF, (long)s->getUID(), range);

    return false;
}

struct Hooks
{
    Hooks()
    {
        q30_sprite_frozen_hook = frozen_hook;
        q30_sprite_despawn_hook = despawn_hook;
    }
} g_hooks;

// ---- test switches --------------------------------------------------------------------------

struct KeyEvent
{
    int frame, control;
    bool down;
};

std::vector<KeyEvent> g_keys;
bool g_keys_read = false;

void read_keys()
{
    g_keys_read = true;
    const char *e = getenv("ZC_TEST_KEYS");

    if(!e)
        return;

    static const char *names[14] = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "START", "L", "R", "MAP", "EX1", "EX2", "EX3", "EX4" };
    std::string text(e);
    size_t at = 0;

    while(at < text.size())
    {
        size_t end = text.find(' ', at);

        if(end == std::string::npos)
            end = text.size();

        const std::string word = text.substr(at, end - at);
        at = end + 1;
        const size_t colon = word.find(':');

        if(colon == std::string::npos || colon + 2 > word.size())
            continue;

        KeyEvent k;
        k.frame = atoi(word.c_str());
        k.down = word[colon + 1] == '+';
        k.control = -1;

        for(int i = 0; i < 14; ++i)
            if(word.substr(colon + 2) == names[i])
                k.control = i;

        if(k.control >= 0)
            g_keys.push_back(k);
    }
}

} // namespace

void q30_region_tag_children()
{
    tag_children();
}

bool q30_vp_despawned(long uid)
{
    return !g_despawned.empty() && g_despawned.erase(uid) != 0;
}

void q30_vp_ranges_reset()
{
    g_parent_scr = -1;
    g_uid_mark = 0;
    g_guy_suspend.clear();
    g_guy_despawn.clear();
    g_item_suspend.clear();
    g_item_despawn.clear();
    g_guy_wsuspend.clear();
    g_guy_wdespawn.clear();
    g_item_wsuspend.clear();
    g_item_wdespawn.clear();
    g_wpn_range.clear();
    g_wpn_despawns = false;
    g_despawned.clear();

    for(int k = 0; k < KINDS; ++k)
    {
        g_frozen[k].clear();
        g_frozen_frame[k] = -1;
    }
}

void q30_vp_note(const char *what, int id, long suspend, long despawn);

void q30_vp_guy_ranges(int id, long suspend, long despawn)
{
    q30_vp_note("enemy", id, suspend, despawn);
    set_range(g_guy_suspend, id, suspend, 48);
    set_range(g_guy_despawn, id, despawn, 0);
}

void q30_vp_item_ranges(int id, long suspend, long despawn)
{
    q30_vp_note("item", id, suspend, despawn);
    set_range(g_item_suspend, id, suspend, 0);
    set_range(g_item_despawn, id, despawn, 0);
}

// ZC_VP_RANGELOG=1 (tests): every viewport range a quest sets is listed in the log as it loads, and
// (ZC_VP_RANGELOG=2) every FFC and weapon a range holds or removes.
void q30_vp_note(const char *what, int id, long suspend, long despawn)
{
    static const bool on = getenv("ZC_VP_RANGELOG") != NULL;

    if(on)
        al_trace("q30 vp range: %s %d suspend %ld despawn %ld\n", what, id, suspend, despawn);
}

void q30_vp_guy_weapon_ranges(int id, long suspend, long despawn)
{
    q30_vp_note("enemy weapons", id, suspend, despawn);
    set_range(g_guy_wsuspend, id, suspend, 48);
    set_range(g_guy_wdespawn, id, despawn, 0);
    g_wpn_despawns |= despawn > 0;
}

void q30_vp_item_weapon_ranges(int id, long suspend, long despawn)
{
    q30_vp_note("item weapons", id, suspend, despawn);
    set_range(g_item_wsuspend, id, suspend, 0);
    set_range(g_item_wdespawn, id, despawn, 0);
    g_wpn_despawns |= despawn > 0;
}

void q30_vp_item_weapon_get(int id, long *suspend, long *despawn)
{
    *suspend = range_of(g_item_wsuspend, id, 0);
    *despawn = range_of(g_item_wdespawn, id, 0);
}

void q30_vp_weapon_ranges(long uid, long suspend, long despawn)
{
    static bool listed = false;

    // (the first weapon a combo fires with its own data: list the quest's combos that have ranges)
    if(!listed && getenv("ZC_VP_RANGELOG"))
    {
        listed = true;

        for(int i = 0; i < MAXCOMBOS; ++i)
        {
            const Q30Combo *c = q30_combo(i);

            if(c && (c->misc_weap.viewport_suspend_range || c->misc_weap.viewport_despawn_range))
                q30_vp_note("combo weapon data", i, c->misc_weap.viewport_suspend_range, c->misc_weap.viewport_despawn_range);
        }
    }

    const Range r = { suspend, despawn };
    g_wpn_range[uid] = r;
    g_wpn_despawns |= despawn > 0;
}

// upstream's weapon::clip: a weapon with a despawn range goes when it is past it, nowhere else
bool q30_vp_weapon_despawn(sprite *w, bool *gone)
{
    if(!g_wpn_despawns)
        return false;

    const long range = weapon_range(w).despawn;

    if(range <= 0)
        return false;

    *gone = !within(w, range);
    return true;
}

bool q30_vp_shot_held(long suspend, long despawn, int tx, int ty, int tw, int th, int hx, int hy, int hw, int hh)
{
    return (suspend > 0 && !boxes_within(suspend, tx, ty, tw, th, hx, hy, hw, hh))
           || (despawn > 0 && !boxes_within(despawn, tx, ty, tw, th, hx, hy, hw, hh));
}

// An FFC of the current screen (or of the region screen standing in for it): its tile area (drawn
// from the playing field's top, as upstream's sprite offset has it) and its hit box are both
// outside the viewport grown by its range. Upstream tests it outside scrolling regions too.
bool q30_vp_ffc_beyond(int i, bool despawn)
{
    const long range = despawn ? tmpscr->ffvpdespawn[i] : tmpscr->ffvpsuspend[i];

    if(range <= 0)
        return false;

    const int x = int(tmpscr->ffx[i] / 10000), y = int(tmpscr->ffy[i] / 10000);
    return !boxes_within(range, x, y + 56, ((tmpscr->ffwidth[i] >> 6) + 1) * 16, ((tmpscr->ffheight[i] >> 6) + 1) * 16,
                         x, y, (tmpscr->ffwidth[i] & 63) + 1, (tmpscr->ffheight[i] & 63) + 1);
}

bool q30_vp_frozen(sprite_list *list, long uid)
{
    const int kind = kind_of(list);
    return kind >= 0 && q30_region_on() && g_frozen[kind].count(uid) != 0;
}

// ZC_TEST_KEYS="60:+RIGHT 400:-RIGHT 420:+A" (tests): while a game runs, the controls are these
// alone, pressed and released at those game frames (as an upstream replay's "D 60 Right").
void q30_test_keys(bool *control_state)
{
    if(!g_keys_read)
        read_keys();

    if(g_keys.empty() || !Playing)
        return;

    for(int i = 0; i < 14; ++i)
        control_state[i] = false;

    for(size_t i = 0; i < g_keys.size(); ++i)
        if(g_keys[i].frame <= frame)
            control_state[g_keys[i].control] = g_keys[i].down;
}

// ZC_REGION_SPRLOG=file (tests): once a frame, Link's place and the viewport in region pixels,
// then every enemy (G), item (I) and enemy weapon (E): id, region x, y, z, direction, the screen it
// belongs to and whether it is suspended (*). ZC_REGION_SPRLOG_FLOOR adds an enemy's floor and clock.
// While the screen scrolls Link's place stays the one from before the scroll, as in upstream's replay
// log (replay.cpp do_recording_poll: no "h" step while screenscrolling), which the sweeps compare it with.
void q30_region_sprlog()
{
    static const char *path = getenv("ZC_REGION_SPRLOG");
    static FILE *f = path ? fopen(path, "w") : NULL;
    static int hx = 0, hy = 0;

    if(!f || !Playing)
        return;

    int vx = 0, vy = 0;
    q30_region_viewport(&vx, &vy);
    const int ox = q30_region_offset_x(), oy = q30_region_offset_y();

    if(!screenscrolling)
    {
        // (whole pixels as upstream's zfix getInt, the nearest with halves away from 0: a Link at
        // y -0.5 is logged at -1, as upstream's "h" step)
        auto px = [](double v) { return (int)(v < 0 ? std::ceil(v - 0.5) : std::floor(v + 0.5)); };
        hx = px(double(Link.getX())) + ox;
        hy = px(double(Link.getY())) + oy;
    }

    fprintf(f, "%d scr %02X hero %d %d view %d %d", frame, currscr, hx, hy, vx, vy);
    sprite_list *lists[LOGGED] = { &guys, &Ewpns, &items };
    static const char tag[LOGGED] = { 'G', 'E', 'I' };

    for(int k = 0; k < LOGGED; ++k)
        for(int i = 0; i < lists[k]->Count(); ++i)
        {
            sprite *s = lists[k]->spr(i);
            fprintf(f, " %c%d,%d,%d,%d,%d,%02X%s", tag[k], s->id, int(s->x) + ox, int(s->y) + oy, int(s->z), s->dir,
                    q30_region_sprite_screen(s->getUID()) & 0xFF, g_frozen[k].count(s->getUID()) ? "*" : "");

            if(k == K_GUYS && getenv("ZC_REGION_SPRLOG_FLOOR"))
                fprintf(f, "[floor %d clk %d]", int(((enemy *)s)->floor_y) + oy, s->clk);
        }

    // (ZC_REGION_SPRLOG_LW: Link's weapons too, L)
    static const bool lw = getenv("ZC_REGION_SPRLOG_LW") != NULL;

    for(int i = 0; lw && i < Lwpns.Count(); ++i)
    {
        sprite *s = Lwpns.spr(i);
        fprintf(f, " L%d,%d,%d,%d,%d%s", s->id, int(s->x) + ox, int(s->y) + oy, int(s->z), s->dir, g_frozen[K_LWPNS].count(s->getUID()) ? "*" : "");
    }

    fputc('\n', f);
    fflush(f);
}

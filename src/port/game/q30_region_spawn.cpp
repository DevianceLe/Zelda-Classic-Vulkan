// 3.0 scrolling regions, player only: sprites away from the camera, and the test switches that
// drive and trace a region. See q30_regions.h.
//
// Upstream (sprite.cpp, sprite_list::animate): in a scrolling region a sprite whose tile area and
// hit box both lie outside the viewport grown by its "viewport suspend range" is suspended: it
// isn't animated and its script doesn't run until the camera comes back; an enemy with segments
// stays awake while any part of it is in range. One whose "viewport despawn range" is set is
// removed once it is that far out. Enemies and their weapons have a suspend range of 48 pixels
// unless the quest says otherwise; items and Link's weapons have none.

#include "precompiled.h" //always first

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "zelda.h"
#include "zdefs.h"
#include "sprite.h"
#include "guys.h"
#include "items.h"
#include "link.h"
#include "q30_regions.h"

extern LinkClass Link;
extern sprite_list guys, items, Ewpns, Lwpns;
extern bool Playing;

// sprite.cpp (ZQuest builds it too, so it calls these only when the player has set them)
extern bool (*q30_sprite_frozen_hook)(sprite_list *list, int index);
extern bool (*q30_sprite_despawn_hook)(sprite_list *list, int index);
extern int q30_item_field[4];

namespace
{

enum { K_GUYS, K_EWPNS, K_ITEMS, KINDS };

// per enemy / item id, as the quest file gives them (none: the defaults)
std::vector<long> g_guy_suspend, g_guy_despawn, g_item_suspend, g_item_despawn;
std::set<long> g_frozen[KINDS];     // the sprites (UIDs) the last animate left suspended
int g_frozen_frame[KINDS] = { -1, -1, -1 };

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
    return list == &guys ? K_GUYS : list == &Ewpns ? K_EWPNS : list == &items ? K_ITEMS : -1;
}

long suspend_range(int kind, const sprite *s)
{
    switch(kind)
    {
    case K_GUYS:
        return range_of(g_guy_suspend, s->id & 0xFFF, 48);

    case K_EWPNS:
        return 48;

    default:
        return range_of(g_item_suspend, s->id, 0);
    }
}

long despawn_range(int kind, const sprite *s)
{
    switch(kind)
    {
    case K_GUYS:
        return range_of(g_guy_despawn, s->id & 0xFFF, 0);

    case K_EWPNS:
        return 0;

    default:
        return range_of(g_item_despawn, s->id, 0);
    }
}

// upstream's rect_t::intersects_with
bool rects_meet(int l, int t, int r, int b, int x, int y, int w, int h)
{
    return (t > y ? t : y) < (b < y + h ? b : y + h) && (l > x ? l : x) < (r < x + w ? r : x + w);
}

// the sprite's tile area or its hit box reaches into the viewport grown by range
bool within(const sprite *s, long range)
{
    int l, t, r, b;
    q30_region_view_bounds(&l, &t, &r, &b);
    l -= range;
    t -= range;
    r += range;
    b += range;
    return rects_meet(l, t, r, b, int(s->x + s->xofs), int(s->y + s->yofs), s->txsz * 16, s->tysz * 16)
           || rects_meet(l, t, r, b, int(s->x) + s->hxofs, int(s->y) + s->hyofs, s->hxsz, s->hysz);
}

// upstream's is_sprite_in_view: itself, or (an enemy made of several sprites: the core and, after
// it in the list, its segments, ids from 0x1000) any other part of it
bool in_view(sprite_list *list, int index, int kind)
{
    sprite *s = list->spr(index);
    const long range = suspend_range(kind, s);

    if(range <= 0 || within(s, range))
        return true;

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

        if(i != index && suspend_range(kind, o) > 0 && within(o, suspend_range(kind, o)))
            return true;
    }

    return false;
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

    enemy *e = (enemy *)s;

    if(e->hp > -1000 && !e->dying)
    {
        e->hp = -1000;
        e->item_set = 0;
    }

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

void q30_vp_ranges_reset()
{
    g_guy_suspend.clear();
    g_guy_despawn.clear();
    g_item_suspend.clear();
    g_item_despawn.clear();

    for(int k = 0; k < KINDS; ++k)
    {
        g_frozen[k].clear();
        g_frozen_frame[k] = -1;
    }
}

void q30_vp_guy_ranges(int id, long suspend, long despawn)
{
    set_range(g_guy_suspend, id, suspend, 48);
    set_range(g_guy_despawn, id, despawn, 0);
}

void q30_vp_item_ranges(int id, long suspend, long despawn)
{
    set_range(g_item_suspend, id, suspend, 0);
    set_range(g_item_despawn, id, despawn, 0);
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
void q30_region_sprlog()
{
    static const char *path = getenv("ZC_REGION_SPRLOG");
    static FILE *f = path ? fopen(path, "w") : NULL;

    if(!f || !Playing)
        return;

    int vx = 0, vy = 0;
    q30_region_viewport(&vx, &vy);
    const int ox = q30_region_offset_x(), oy = q30_region_offset_y();
    fprintf(f, "%d scr %02X hero %d %d view %d %d", frame, currscr, int(Link.getX()) + ox, int(Link.getY()) + oy, vx, vy);
    sprite_list *lists[KINDS] = { &guys, &Ewpns, &items };
    static const char tag[KINDS] = { 'G', 'E', 'I' };

    for(int k = 0; k < KINDS; ++k)
        for(int i = 0; i < lists[k]->Count(); ++i)
        {
            sprite *s = lists[k]->spr(i);
            fprintf(f, " %c%d,%d,%d,%d,%d,%02X%s", tag[k], s->id, int(s->x) + ox, int(s->y) + oy, int(s->z), s->dir,
                    q30_region_sprite_screen(s->getUID()) & 0xFF, g_frozen[k].count(s->getUID()) ? "*" : "");

            if(k == K_GUYS && getenv("ZC_REGION_SPRLOG_FLOOR"))
                fprintf(f, "[floor %d clk %d]", int(((enemy *)s)->floor_y) + oy, s->clk);
        }

    fputc('\n', f);
    fflush(f);
}

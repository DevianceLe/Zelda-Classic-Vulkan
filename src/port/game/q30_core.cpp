// Tables the 2.55 and 3.0 quest readers fill. See q30.h.

#include "precompiled.h" //always first

#include <string.h>
#include <string>

#include "q30.h"

extern word quest_header_zelda_version;

namespace
{

std::map<int, Q30ScriptRef> g_combo_scripts;
std::map<int, int> g_msg_wrap_extra;

// A 3.0 string's margins and "more" icon (q30_set_msg_layout).
struct MsgLayout
{
    int box_w, box_h;   // w and h as the quest gives them (this engine keeps them 16 smaller)
    int margins[4];     // up, down, left, right
    bool has_icon;
    int sprite, anchor, x, y;
};

std::map<int, MsgLayout> g_msg_layouts;
std::map<int, std::vector<char> > g_msg_long_texts;    // q30_set_msg_long_text

// Items 256 and up (q30_set_high_item) and the init data's starting ones among them.
struct HighItem
{
    itemdata d;
    int family;         // upstream's family (itemdata's byte can't hold every one)
    std::string name;
};

std::map<int, HighItem> g_high_items;
std::vector<int> g_init_high_items;

}   // (the high items' door is outside the anonymous namespace)

extern itemdata *itemsbuf;

itemdata &q30_itemsbuf(int id)
{
    // the player's itemsbuf has room for every newer quest's item (q30_set_high_item)
    if(id >= 0 && id < (quest_header_zelda_version < 0x255 ? MAXITEMS : Q30_MAXITEMS))
        return itemsbuf[id];

    std::map<int, HighItem>::iterator it = g_high_items.find(id);

    if(it != g_high_items.end())
        return it->second.d;

    static itemdata none;
    memset(&none, 0, sizeof(none));
    none.count = -1;
    return none;
}

namespace
{

// q30_set_screen_guy: tile, cs, roomflags
struct ScreenGuy
{
    long tile;
    int cs, roomflags;
};

std::map<int, ScreenGuy> g_screen_guys;

// The layout of string str, or NULL for a 2.53 quest (one loaded after a newer one too)
// and for strings without their own.
const MsgLayout *msg_layout(int str)
{
    if(quest_header_zelda_version < 0x255)
        return NULL;

    std::map<int, MsgLayout>::const_iterator it = g_msg_layouts.find(str);
    return it == g_msg_layouts.end() ? NULL : &it->second;
}
std::map<int, Q30ScriptRef> g_dmap_scripts[q30_dmap_kinds];
std::map<int, std::pair<int, int> > g_dmap_subscreens;    // overlay, map
std::map<int, std::pair<std::string, long> > g_dmap_texts;  // title, intro string
std::map<int, int> g_dmap_floors;
std::map<int, int> g_dmap_mirrors;

void put(std::map<int, Q30ScriptRef> &table, int key, word script, const long *initd)
{
    if(!script)
    {
        table.erase(key);
        return;
    }

    Q30ScriptRef ref;
    ref.script = script;
    if(initd)
        memcpy(ref.initd, initd, sizeof(ref.initd));
    table[key] = ref;
}

const Q30ScriptRef *get(const std::map<int, Q30ScriptRef> &table, int key)
{
    std::map<int, Q30ScriptRef>::const_iterator it = table.find(key);
    return it == table.end() ? NULL : &it->second;
}

} // namespace

void q30_set_msg_wrap_extra(int str, int extra)
{
    if(extra)
        g_msg_wrap_extra[str] = extra;
    else
        g_msg_wrap_extra.erase(str);
}

int q30_msg_wrap_extra(int str)
{
    extern word quest_header_zelda_version;

    if(quest_header_zelda_version < 0x255) // a 2.53 quest loaded after a newer one
        return 0;

    std::map<int, int>::const_iterator it = g_msg_wrap_extra.find(str);
    return it == g_msg_wrap_extra.end() ? 0 : it->second;
}

void q30_clear_msg_layouts()
{
    g_msg_layouts.clear();
    g_msg_long_texts.clear();
}

void q30_set_screen_guy(int index, long tile, int cs, int roomflags)
{
    ScreenGuy g;
    g.tile = tile;
    g.cs = cs;
    g.roomflags = roomflags;
    g_screen_guys[index] = g;
}

bool q30_screen_guy(int index, long *tile, int *cs, int *roomflags)
{
    bool q30_quest_rule(int id);

    if(quest_header_zelda_version < 0x255 || q30_quest_rule(415))   // qr_OLD_GUY_HANDLING
        return false;

    std::map<int, ScreenGuy>::const_iterator it = g_screen_guys.find(index);

    if(it == g_screen_guys.end())
        return false;

    *tile = it->second.tile;
    *cs = it->second.cs;
    *roomflags = it->second.roomflags;
    return true;
}

void q30_set_items_end(int count);     // q30_triggers.cpp

void q30_clear_high_items()     // their data; the init data replaces its own list
{
    g_high_items.clear();
    q30_set_items_end(MAXITEMS);

    // itemsbuf's newer items: blank, as the reader leaves an item the file doesn't cover
    for(int i = MAXITEMS; i < Q30_MAXITEMS; ++i)
    {
        memset(&itemsbuf[i], 0, sizeof(itemdata));
        itemsbuf[i].count = -1;
        itemsbuf[i].fam_type = 1;
    }
}

// The reader hands over the items in order, so the last one sets how far item loops go.
void q30_set_high_item(int id, const itemdata &d, int family, const char *name)
{
    HighItem &h = g_high_items[id];
    h.d = d;
    h.family = family;
    h.name = name ? name : "";

    // and in itemsbuf, where the game reads every item (a level of 0 reads as 1, as for the others)
    if(id >= MAXITEMS && id < Q30_MAXITEMS)
    {
        itemsbuf[id] = d;

        if(itemsbuf[id].fam_type == 0)
            itemsbuf[id].fam_type = 1;

        q30_set_items_end(id + 1);
    }
}

const char *q30_item_name(int id)
{
    extern char *item_string[];

    if(id >= 0 && id < MAXITEMS)
        return item_string[id];

    std::map<int, HighItem>::const_iterator it = g_high_items.find(id);
    return (it == g_high_items.end() || quest_header_zelda_version < 0x255) ? "" : it->second.name.c_str();
}

const itemdata *q30_high_item(int id, int *family, const char **name)
{
    if(quest_header_zelda_version < 0x255)
        return NULL;

    std::map<int, HighItem>::const_iterator it = g_high_items.find(id);

    if(it == g_high_items.end())
        return NULL;

    if(family)
        *family = it->second.family;

    if(name)
        *name = it->second.name.c_str();

    return &it->second.d;
}

// bits: one per item, first_id the first one's number.
void q30_set_init_high_items(const byte *bits, int first_id, int count)
{
    g_init_high_items.clear();

    for(int i = 0; bits && i < count; ++i)
        if(bits[i / 8] & (1 << (i % 8)))
            g_init_high_items.push_back(first_id + i);
}

const std::vector<int> &q30_init_high_items()
{
    static const std::vector<int> none;
    return quest_header_zelda_version < 0x255 ? none : g_init_high_items;
}

void q30_set_msg_long_text(int str, const char *text)
{
    if(!text)
    {
        g_msg_long_texts.erase(str);
        return;
    }

    // zeros past the end, as MsgStr::s has: the message code peeks up to two bytes ahead
    const size_t n = strlen(text);
    std::vector<char> &v = g_msg_long_texts[str];
    v.assign(n + 8, 0);
    memcpy(&v[0], text, n);
}

char *q30_msg_long_text(int str)
{
    if(quest_header_zelda_version < 0x255 || g_msg_long_texts.empty())
        return NULL;

    std::map<int, std::vector<char> >::iterator it = g_msg_long_texts.find(str);
    return it == g_msg_long_texts.end() ? NULL : &it->second[0];
}

void q30_set_msg_layout(int str, int box_w, int box_h, const int margins[4], bool has_icon, int icon_sprite, int icon_anchor, int icon_x, int icon_y)
{
    MsgLayout l;
    l.box_w = box_w;
    l.box_h = box_h;
    memcpy(l.margins, margins, sizeof(l.margins));
    l.has_icon = has_icon;
    l.sprite = icon_sprite;
    l.anchor = icon_anchor;
    l.x = icon_x;
    l.y = icon_y;
    g_msg_layouts[str] = l;
}

// Text is drawn at the margins (upstream's msgstr::do_new: cursor_x = margins[left],
// cursor_y = margins[up]); 2.53 draws it 8 pixels in.
int q30_msg_text_x(int str)
{
    const MsgLayout *l = msg_layout(str);
    return l ? l->margins[2] : 8;
}

int q30_msg_text_y(int str)
{
    const MsgLayout *l = msg_layout(str);
    return l ? l->margins[0] : 8;
}

// Upstream stops a string that can't scroll once a line starts at h - margins[down]
// (msg_layout::bottom_margin_clip); 2.53 stops at h - 16 with lines counted from 8 down.
int q30_msg_height_extra(int str)
{
    const MsgLayout *l = msg_layout(str);
    return l ? 16 - l->margins[0] - l->margins[1] : 0;
}

// Upstream's message_string.cpp draw_icon: the icon's offset from the anchor it names on
// the box (or the screen).
bool q30_msg_more_icon(int str, int *x, int *y, int *sprite)
{
    extern MsgStr *MsgStrings;
    const MsgLayout *l = msg_layout(str);

    if(!l || !l->has_icon)
        return false;

    // message_anchor: up, down, left, right, l_up, r_up, l_down, r_down, center, screen,
    // screen_y_offset
    enum { a_up, a_down, a_left, a_right, a_l_up, a_r_up, a_l_down, a_r_down, a_center, a_screen, a_screen_y };
    const MsgStr &m = MsgStrings[str];
    const int bw = l->box_w, bh = l->box_h;     // the box as upstream sizes it
    int ix = l->x, iy = l->y;

    switch(l->anchor)
    {
    case a_l_up: case a_left: case a_l_down: ix += m.x; break;
    case a_up: case a_center: case a_down: ix += m.x + bw / 2; break;
    case a_r_up: case a_right: case a_r_down: ix += m.x + bw; break;
    default: break;
    }

    switch(l->anchor)
    {
    case a_screen_y: case a_l_up: case a_up: case a_r_up: iy += m.y; break;
    case a_left: case a_center: case a_right: iy += m.y + bh / 2; break;
    case a_l_down: case a_down: case a_r_down: iy += m.y + bh; break;
    default: break;
    }

    *x = ix;
    *y = iy;
    *sprite = l->sprite;
    return true;
}

void q30_clear_tables()
{
    g_combo_scripts.clear();

    for(int i = 0; i < q30_dmap_kinds; ++i)
        g_dmap_scripts[i].clear();

    g_dmap_subscreens.clear();
    g_dmap_texts.clear();
    g_dmap_floors.clear();
    g_dmap_mirrors.clear();
    g_screen_guys.clear();
}

void q30_set_dmap_mirror(int dmap, int mirror)
{
    if(mirror >= 0)
        g_dmap_mirrors[dmap] = mirror;
    else
        g_dmap_mirrors.erase(dmap);
}

int q30_dmap_mirror(int dmap)
{
    std::map<int, int>::const_iterator it = g_dmap_mirrors.find(dmap);
    return it == g_dmap_mirrors.end() ? -1 : it->second;
}

void q30_set_dmap_floor(int dmap, int floor)
{
    if(floor)
        g_dmap_floors[dmap] = floor;
    else
        g_dmap_floors.erase(dmap);
}

int q30_dmap_floor(int dmap)
{
    std::map<int, int>::const_iterator it = g_dmap_floors.find(dmap);
    return it == g_dmap_floors.end() ? 0 : it->second;
}

void q30_set_combo_script(int combo, word script, const long *initd)
{
    put(g_combo_scripts, combo, script, initd);
}

const Q30ScriptRef *q30_combo_script(int combo)
{
    return get(g_combo_scripts, combo);
}

void q30_set_dmap_script(int dmap, int kind, word script, const long *initd)
{
    if(kind >= 0 && kind < q30_dmap_kinds)
        put(g_dmap_scripts[kind], dmap, script, initd);
}

const Q30ScriptRef *q30_dmap_script(int dmap, int kind)
{
    return (kind >= 0 && kind < q30_dmap_kinds) ? get(g_dmap_scripts[kind], dmap) : NULL;
}

void q30_set_dmap_subscreens(int dmap, int overlay, int map)
{
    g_dmap_subscreens[dmap] = std::make_pair(overlay, map);

    if((overlay || map >= 0) && getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 dmapsub: dmap %d overlay %d map %d\n", dmap, overlay, map);
}

int q30_dmap_overlay_subscreen(int dmap)
{
    std::map<int, std::pair<int, int> >::const_iterator it = g_dmap_subscreens.find(dmap);
    return it == g_dmap_subscreens.end() ? 0 : it->second.first;
}

int q30_dmap_map_subscreen(int dmap)
{
    std::map<int, std::pair<int, int> >::const_iterator it = g_dmap_subscreens.find(dmap);
    return it == g_dmap_subscreens.end() ? -1 : it->second.second;
}

void q30_set_dmap_texts(int dmap, const char *title, long intro_string)
{
    g_dmap_texts[dmap] = std::make_pair(std::string(title ? title : ""), intro_string);

    if(intro_string && getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 dmap %d: intro string %ld, title '%s'\n", dmap, intro_string, title ? title : "");
}

const char *q30_dmap_title(int dmap)
{
    std::map<int, std::pair<std::string, long> >::const_iterator it = g_dmap_texts.find(dmap);
    return it == g_dmap_texts.end() ? NULL : it->second.first.c_str();
}

long q30_dmap_intro_string(int dmap)
{
    std::map<int, std::pair<std::string, long> >::const_iterator it = g_dmap_texts.find(dmap);
    return it == g_dmap_texts.end() ? 0 : it->second.second;
}

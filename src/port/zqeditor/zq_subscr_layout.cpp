// ZQuest's subscreen layout editor for 2.55 and 3.0 Nightly quests (Dear ImGui), after upstream's
// subscreen editor (zq/zq_subscr.cpp): a subscreen page drawn with the quest's own tiles, fonts,
// colours and widgets, on which widgets are picked, moved and resized with the mouse (snapped to
// a grid if wanted), added, deleted, duplicated and reordered; pages are added, removed and
// moved; the active, passive, overlay (and 3.0's map) subscreens are all there; a widget's
// whole property list is the subscreen data editor's list of its fields (zq_record_editor.cpp).
//
// The page is drawn as the player draws it: each widget becomes the 2.53 subscreen object the
// player's reader makes of it (q255_widget_object, q255_init.cpp), and subscr.cpp's
// show_custom_subscreen draws those through the player's own code for newer quests (fonts,
// system colours, counters past 32, items past 255, newer item slots and minimap titles, the
// selected text override). That code asks the player's q30_* functions; while this editor draws
// a page, the ones below answer them from the open quest and the preview. As upstream's editor,
// it draws widgets whatever their display conditions, disabled ones too. Widgets the player has
// nothing to draw for (a type 2.53 has no object for) are outlined only.
//
// The editor works on the whole SUBS section in the quest's own format version, taken apart
// into subscreens, pages and widgets (each widget kept as its own bytes, so nothing it holds is
// lost). It starts from the section as a save would write it now (zq255_subs_section: 2.53's
// subscreen edits and the subscreen data editor's are in it) and OK puts the whole section back
// (zq255_subs_put_section, zqw_init.cpp): the SUBS writer then writes it, and both other
// editors carry on from it. A new widget is laid out by the subscreen data editor's schema
// (zq_tape_subscreen_widget) with upstream's defaults for its type, in the section's own version;
// a 2.55 section (version 12 and older) only offers 2.55.17's widget types.
//
// Whole subscreens (added, deleted, renamed, duplicated, with the DMaps' subscreen numbers
// following), the Subscreen Settings and Page Settings dialogs, the wizards, Paste Properties
// and the View options are in zq_subscr_layout_more.h.
//
// Threads: the window is a UI layer (platform thread, UI lock held) during a modal session the
// game thread holds open; the game thread draws the page (Allegro-style bitmaps), runs the
// property list and puts the section in. Both touch the model under the UI lock.
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <allegro.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <set>
#include <string>
#include <vector>

#include "zdefs.h"
#include "subscr.h"
#include "sprite.h"
#include "gamedata.h"
#include "zcport/ui.h"
#include "zq_q255_store.h"
#include "zq_tape.h"

#include <imgui.h>
#include <imgui_internal.h>     // RegisterUserTexture, ImTextureDataQueueUpload (the page's texture)

extern bool saved;
extern int q255_header_version[3];
extern miscQdata misc;
extern gamedata *game;
extern zquestheader header;
extern subscreen_group *css;            // zq_subscr.cpp: what Bweapon looks through
extern int Awpn, Bwpn, Bpos;
extern zinitdata zinit;
extern PALETTE RAMpal;
extern sprite_list Sitems;
extern dmap *DMaps;
extern itemdata *itemsbuf;
extern char *item_string[ITEMCNT];
bool has_item(int item_type, int it);                   // zquest.cpp (always true)
long q30_item_family(int item);                         // q255_store_zq.cpp: upstream's class
void resetItems(gamedata *game2, zinitdata *zinit2, bool lvlitems);
int Bweapon(int pos);
void loadlvlpal(int level);

size_t q255_widget_extent(const byte *data, size_t size, int version, byte type);      // q255_init.cpp
bool q255_widget_object(const byte *data, size_t size, int version, byte type, subscreen_object &t, std::string &text,
                        size_t *extent, int *sel_pos, std::string *seltext);
size_t q255_subscreen_head_extent(const byte *data, size_t size, int version, int *file_type);
size_t q255_page_trans_extent(const byte *data, size_t size, int version);
dword q30_compat_rule_since(int id);                                                    // q30_types.cpp
FONT *q30_font_newer(int id);                                                           // q30_fonts.cpp
FONT *q30_font_classic(int id);
bool zq255_subs_section(std::vector<byte> &body, int *version, std::string &why);      // zqw_init.cpp
bool zq255_subs_put_section(const std::vector<byte> &body, std::string &why);
bool zq_edit_subscreen_widget(std::vector<byte> &bytes, int v, const char *title);      // zq_record_editor.cpp
bool zq255_dmap_records_all(std::vector<std::vector<byte> > &recs, int *version);      // zqw_maps.cpp
bool zq255_dmap_commit(int i, const std::vector<byte> &rec, std::string &why);
int select_tile(int &tile, int &flip, int type, int &cs, bool edit_cs, int exnow, bool always_use_flip);   // zq_tiles.cpp
extern char *sfx_string[WAV_COUNT];

// ---- what subscr.cpp asks while this editor draws ----------------------------------------------

namespace
{
bool g_view_on = false;         // a page is being drawn
bool g_view_all_items = false;  // the preview has every item (View > Show unowned items)
byte g_view_rules[100];         // the open quest's rules as its RULE section stores them
dword g_view_compat = 0;        // and its compat rule version (0 before RULE version 17)

// the selectable widgets of the page being drawn, in order: cursor position, selected text
std::vector<std::pair<int, std::string> > g_view_seltexts;

// The open quest's rules, for q30_compat_rule while pages are drawn.
void view_rules()
{
    memset(g_view_rules, 0, sizeof(g_view_rules));
    g_view_compat = 0;
    ZqRuleSection r;

    if(zq255_rules_get(r))
    {
        memcpy(g_view_rules, r.rules, sizeof(g_view_rules));
        g_view_compat = r.compat;
    }
    else
        memcpy(g_view_rules, zq255.rules.empty() ? g_view_rules : &zq255.rules[0], (std::min)(zq255.rules.size(), sizeof(g_view_rules)));
}

// Whether the preview's Link has an item (any of the quest's, 256 and up too).
bool view_owns(int id)
{
    if(id < 0 || id >= Q30_MAXITEMS)
        return false;

    if(g_view_all_items)    // every item that is one (as preview_items gives Link)
        return (itemsbuf[id].flags & ITEM_GAMEDATA) != 0 && (id < MAXITEMS || zq255.high_items.count(id) != 0);

    if(id < MAXITEMS)
        return game && game->get_item(id);

    return std::find(zq255.init_high_items.begin(), zq255.init_high_items.end(), id) != zq255.init_high_items.end();
}

// One past the quest's last item (the player's q30_items_end).
int view_items_end()
{
    return zq255.high_items.empty() ? MAXITEMS : (std::min)((int)Q30_MAXITEMS, zq255.high_items.rbegin()->first + 1);
}
}

bool zq_subscr_view_on()
{
    return g_view_on;
}

// The player's q30_* functions subscr.cpp asks when it draws a 2.55/3.0 quest's widgets (the
// player's own are in q30_triggers.cpp, q30_core.cpp and q30_scripts_*.h), answered for the page
// this editor draws: the open quest's data (ZQuest's store, q255_store_zq.cpp) and the preview's
// Link (the init data's items, or every item). Outside a page's drawing they say "not a newer
// quest", so nothing else in ZQuest takes these paths.

// "This is a 2.55/3.0 quest" (q30_triggers.h): only while a page is being drawn.
bool q30_active()
{
    return g_view_on;
}

// A rule as a load of the open quest has it upstream: its bit, or on for a quest saved before
// the rule existed (the same table as the player's, q30_compat_rule_since).
bool q30_compat_rule(int id)
{
    const bool bit = id >= 0 && id < 800 && (g_view_rules[id / 8] & (1 << (id % 8))) != 0;
    return bit || g_view_compat < q30_compat_rule_since(id);
}

// The DMap's whole title (minimap titles), NULL when the quest gave none.
const char *q30_dmap_title(int dmap)
{
    std::map<int, std::string>::const_iterator it = zq255.dmap_titles.find(dmap);
    return it == zq255.dmap_titles.end() ? NULL : it->second.c_str();
}

// An item's name, 256 and up too.
const char *q30_item_name(int id)
{
    if(id >= 0 && id < MAXITEMS)
        return item_string[id];

    std::map<int, Zq255Store::HighItem>::const_iterator it = zq255.high_items.find(id);
    return it == zq255.high_items.end() ? "" : it->second.name.c_str();
}

bool q30_item_has_display_name(int id)
{
    return zq255.item_display_name.count(id) != 0;
}

// As the player's: the display name when the item has one, with "%s" as a bottle's contents
// (the init data's, as the preview starts the game), otherwise its name.
std::string q30_item_shown_name(int id)
{
    enum { ITYPE_BOTTLE = 270, BOTTLE_TYPES = 64 };
    std::map<int, std::string>::const_iterator it = zq255.item_display_name.find(id);

    if(it == zq255.item_display_name.end() || it->second.empty())
        return (id >= 0 && id < view_items_end()) ? std::string(q30_item_name(id)) : std::string();

    std::string s = it->second, contents;

    if(id >= 0 && id < view_items_end() && q30_item_family(id) == ITYPE_BOTTLE)
    {
        const int slot = (int)itemsbuf[id].misc1;
        const int bind = (slot >= 0 && slot < (int)zq255.init_bottle_slots.size()) ? zq255.init_bottle_slots[slot] : 0;
        std::map<int, Q30BottleType>::const_iterator t = zq255.bottle_types.find(bind - 1);
        contents = (bind <= 0 || bind > BOTTLE_TYPES) ? std::string("Empty") : t == zq255.bottle_types.end() ? std::string() : std::string(t->second.name);
    }

    for(size_t q = 0; (q = s.find("%s", q)) != std::string::npos; q += contents.size())
        s.replace(q, 2, contents);

    return s;
}

// The selected text widget: the text the selectable widget at the cursor gives in place of an
// item's name (upstream SW_SelectedText), NULL for none: the first selectable widget there.
const char *q30_sub_selected_text(const subscreen_group *, int pos)
{
    if(!g_view_on)
        return NULL;

    for(size_t i = 0; i < g_view_seltexts.size(); ++i)
        if(g_view_seltexts[i].first == pos)
            return g_view_seltexts[i].second.empty() ? NULL : g_view_seltexts[i].second.c_str();

    return NULL;
}

// The player's choice for an item slot that names its item (d8: the item + 1, any of the quest's)
// or a class 2.53 has no family for (d10: the class + 1), as upstream's SW_ItemSlot's
// getDisplayItem: *id is the item drawn, -1 for none. With the preview's Link (ZQuest has no
// game state: bombs are always there, nothing is disabled). False: 2.53's own drawing.
bool q30_subscreen_item(const subscreen_object &o, int *id)
{
    if(!g_view_on || !game)
        return false;

    if(o.d8 > 0)
    {
        const int item = o.d8 - 1;
        const bool nosp = o.d9 != 0;    // SUBSCR_CURITM_IGNR_SP_DISPLAY

        if(item >= view_items_end())
            return false;

        switch(q30_item_family(item))
        {
        case itype_map:
        case itype_compass:
        case itype_bosskey:
            if(nosp)
                break;

            *id = has_item(q30_item_family(item), -1) ? item : -1;     // (ZQuest: always)
            return true;

        case itype_heartpiece:
            if(!nosp && misc.colors.HCpieces_tile)
            {
                *id = item;
                return true;
            }

            break;
        }

        *id = view_owns(item) ? item : -1;
        return true;
    }

    if(o.d10 > 0)
    {
        // the best item of that class Link has (q30_current_item)
        int best = -1;

        for(int i = 0; i < view_items_end(); ++i)
        {
            if(q30_item_family(i) != o.d10 - 1 || !view_owns(i))
                continue;

            if(best < 0 || itemsbuf[i].fam_type > itemsbuf[best].fam_type)
                best = i;
        }

        *id = best;
        return true;
    }

    return false;
}

// Upstream's font with this id, as the player's get_zc_font gives it (script_drawing.cpp), from
// the same mapping (q30_fonts.cpp): 3.0's fonts past 2.53's, then 2.53's by their font_* number.
FONT *zq_subscr_view_font(int id)
{
    if(FONT *f = q30_font_newer(id))
        return f;

    return q30_font_classic(id);
}

namespace
{

typedef std::vector<byte> Bytes;

// upstream's widget types (new_subscr.h widgNULL...)
enum
{
    W_NULL, W_FRAME, W_TEXT, W_LINE, W_RECT, W_TIME, W_MMETER, W_LMETER, W_BTNITM, W_COUNTER,
    W_OLDCTR, W_MMAPTITLE, W_MMAP, W_LMAP, W_BGCOLOR, W_ITEMSLOT, W_MCGUFF_FRAME, W_MCGUFF, W_TILEBLOCK, W_MINITILE,
    W_SELECTOR, W_LGAUGE, W_MGAUGE, W_TEXTBOX, W_SELECTEDTEXT, W_MISCGAUGE, W_BTNCOUNTER, W_COUNTERPERCBAR,
    W_ITMCOOLDOWNGAUGE, W_ITMCOOLDOWNTEXT, W_MAX,
    W_MAX_255 = W_BTNCOUNTER + 1    // 2.55.17's types end there
};

// One past the last widget type a section of version v holds: 2.55.17's 26 up to version 12
// (its readWidg refuses any other: "Invalid quest file"), all of 3.0's from 13 on.
int type_limit(int v)
{
    return v <= 12 ? W_MAX_255 : W_MAX;
}

const char *type_name(int ty)
{
    static const char *const names[] =
    {
        "(none)", "Frame", "Text", "Line", "Rectangle", "Time", "Magic meter", "Life meter", "Button item", "Counter",
        "Counter block", "Minimap title", "Minimap", "Large map", "Background color", "Item slot", "McGuffin frame",
        "McGuffin piece", "Tile block", "Mini tile", "Selector", "Life gauge", "Magic gauge", "Text box", "Selected text",
        "Misc gauge", "Button counter", "Counter bar", "Item cooldown gauge", "Item cooldown text"
    };

    return ty >= 0 && ty < W_MAX ? names[ty] : "?";
}

// A quest rule of the open quest (the rules as the rules editor last left them).
bool rule_on(int id)
{
    const size_t b = (size_t)id / 8;
    return b < zq255.rules.size() && (zq255.rules[b] & (1 << (id % 8))) != 0;
}

const char *list_name(int list)
{
    static const char *const names[4] = { "Active", "Passive", "Overlay", "Map" };
    return list >= 0 && list < 4 ? names[list] : "?";
}

// ---- the section taken apart ---------------------------------------------------------------

// Where a widget's base fields are in its bytes (byte 0 is its type; SubscrWidget::write).
enum { WO_SSO = 1, WO_POS = 2, WO_X = 3, WO_Y = 5, WO_W = 7, WO_H = 9, WO_GEN = 11, WO_FLAGS = 15, WO_LABEL = 19 };

int get16(const Bytes &b, size_t at) { return at + 2 <= b.size() ? (b[at] | (b[at + 1] << 8)) : 0; }
int get16s(const Bytes &b, size_t at) { return (short)get16(b, at); }
long get32(const Bytes &b, size_t at)
{
    return at + 4 <= b.size() ? (long)(int)((dword)b[at] | ((dword)b[at + 1] << 8) | ((dword)b[at + 2] << 16) | ((dword)b[at + 3] << 24)) : 0;
}
void put16(Bytes &b, size_t at, int v)
{
    if(at + 2 <= b.size())
    {
        b[at] = (byte)(v & 0xFF);
        b[at + 1] = (byte)((v >> 8) & 0xFF);
    }
}

struct Page
{
    Bytes cursor;               // where the cursor starts (4 bytes)
    std::vector<Bytes> widgets; // each: its type byte, then its data
    Bytes trans;                // version 16 on: the page's change transitions, their count first
};

struct Sub
{
    int list;                   // 0 active, 1 passive, 2 overlay, 3 map
    int kind;                   // its file type, the same numbers
    std::string name;
    Bytes head;                 // the name through the script: everything before the page count
    size_t def_at;              // active and map subscreens: where the default buttons are in head (0: none)
    int orig;                   // its number in its list when the editor opened (-1: made since), for the DMaps' numbers
    std::vector<Page> pages;

    // Upstream keeps pages for active and map subscreens only: it writes one page of any other
    // (ZCSubscreen::write, "if(pagecnt && !active) pagecnt = 1") and its editor hides their page
    // controls (zq_subscr.cpp nopages).
    bool paged() const { return kind == 0 || kind == 3; }
};

typedef std::vector<Sub> Model;

struct Reader
{
    const Bytes &d;
    size_t pos;
    bool bad;

    explicit Reader(const Bytes &data) : d(data), pos(0), bad(false) {}
    bool has(size_t n) const { return !bad && pos <= d.size() && n <= d.size() - pos; }
    int c() { if(!has(1)) { bad = true; return 0; } return d[pos++]; }
    int w() { if(!has(2)) { bad = true; return 0; } int v = d[pos] | (d[pos + 1] << 8); pos += 2; return v; }
    long l() { if(!has(4)) { bad = true; return 0; } long v = get32(d, pos); pos += 4; return v; }
    void skip(size_t n) { if(!has(n)) bad = true; else pos += n; }
};

// The whole section's data, walked with the player's reader (q255_init.cpp): each subscreen's
// head, widgets and page transitions are as long as it reads them.
bool parse(const Bytes &d, int v, Model &out)
{
    out.clear();
    Reader r(d);
    const int lists = v >= 16 ? 4 : 3;

    for(int list = 0; list < lists && !r.bad; ++list)
    {
        const int count = r.c();

        for(int q = 0; q < count && !r.bad; ++q)
        {
            Sub s;
            s.list = list;
            s.kind = 0;
            s.def_at = 0;
            s.orig = q;
            const size_t start = r.pos;
            const size_t head = r.has(1) ? q255_subscreen_head_extent(d.data() + r.pos, d.size() - r.pos, v, &s.kind) : 0;

            if(!head)
                return false;

            // the name (a byte's length), type, flags; active and map subscreens: the selector
            // override (when flags has 2: 8 bytes and 12 for each of its entries), then the default
            // buttons
            const int nlen = d[start];
            s.name.assign((const char *)d.data() + start + 1, (std::min)((size_t)nlen, head - 1));

            if(s.paged())
            {
                size_t at = 1 + nlen + 1 + 4;

                if(get32(d, start + 1 + nlen + 1) & 2)
                    at += 8 + 1 + (start + at + 8 < d.size() ? (size_t)d[start + at + 8] * 12 : 0);

                s.def_at = at;
            }

            r.skip(head);
            s.head.assign(d.begin() + start, d.begin() + r.pos);
            const int pages = r.c();

            for(int pg = 0; pg < pages && !r.bad; ++pg)
            {
                Page p;
                const size_t cat = r.pos;
                r.skip(4);

                if(r.bad)
                    return false;

                p.cursor.assign(d.begin() + cat, d.begin() + cat + 4);
                const int widgets = r.w();

                for(int i = 0; i < widgets && !r.bad; ++i)
                {
                    const size_t at = r.pos;
                    const int ty = r.c();
                    const size_t ext = (r.bad || r.pos >= d.size()) ? 0 : q255_widget_extent(d.data() + r.pos, d.size() - r.pos, v, (byte)ty);

                    if(!ext)
                        return false;

                    r.skip(ext);
                    p.widgets.push_back(Bytes(d.begin() + at, d.begin() + r.pos));
                }

                if(v >= 16 && !r.bad)
                {
                    const size_t tat = r.pos;
                    const size_t ext = r.has(1) ? q255_page_trans_extent(d.data() + r.pos, d.size() - r.pos, v) : 0;

                    if(!ext)
                        return false;

                    r.skip(ext);
                    p.trans.assign(d.begin() + tat, d.begin() + r.pos);
                }

                s.pages.push_back(p);
            }

            out.push_back(s);
        }
    }

    return !r.bad && r.pos == d.size();
}

void encode(const Model &m, int v, Bytes &out)
{
    out.clear();
    const int lists = v >= 16 ? 4 : 3;

    for(int list = 0; list < lists; ++list)
    {
        int count = 0;

        for(size_t i = 0; i < m.size(); ++i)
            count += m[i].list == list;

        out.push_back((byte)count);

        for(size_t i = 0; i < m.size(); ++i)
        {
            const Sub &s = m[i];

            if(s.list != list)
                continue;

            out.insert(out.end(), s.head.begin(), s.head.end());
            out.push_back((byte)s.pages.size());

            for(size_t pg = 0; pg < s.pages.size(); ++pg)
            {
                const Page &p = s.pages[pg];
                out.insert(out.end(), p.cursor.begin(), p.cursor.end());
                out.push_back((byte)(p.widgets.size() & 0xFF));
                out.push_back((byte)(p.widgets.size() >> 8));

                for(size_t w = 0; w < p.widgets.size(); ++w)
                    out.insert(out.end(), p.widgets[w].begin(), p.widgets[w].end());

                if(v >= 16)
                {
                    if(p.trans.empty())
                        out.push_back(0);
                    else
                        out.insert(out.end(), p.trans.begin(), p.trans.end());
                }
            }
        }
    }
}

std::string widget_label(const Bytes &w, int v)
{
    if(v < 9)
        return std::string();

    const int n = get16(w, WO_LABEL);

    if(WO_LABEL + 2 + (size_t)n > w.size())
        return std::string();

    return std::string((const char *)&w[WO_LABEL + 2], (size_t)n);
}

// A new widget of type ty at x, y, in the section's version: the schema lays it out, with
// upstream's defaults for the type (new_subscr.h) and zero for the rest. more: fields to set on
// top of the defaults (the wizards').
bool new_widget(int v, int ty, int x, int y, Bytes &out, const Tape *more = NULL)
{
    if(ty < 1 || ty >= type_limit(v))
        return false;

    Tape src;
    struct Set
    {
        Tape &t;
        explicit Set(Tape &tape) : t(tape) {}
        void operator()(const char *key, int64_t value)
        {
            TapeField f;
            f.key = key;
            f.value = value;
            t.f.push_back(f);
        }
    } set(src);

    set("type", ty);
    // upstream's SW_Time(ty) and SW_Selector(ty) keep the widget type in the old object type byte
    set("ssotype", (ty == W_TIME || ty == W_SELECTOR) ? ty : 0);
    set("posflags", sspUP | sspDOWN | sspSCROLLING);
    set("x", x);
    set("y", y);
    int w = 1, h = 1;

    switch(ty)
    {
    case W_LINE: case W_RECT: case W_MMAPTITLE: case W_TEXTBOX: case W_SELECTEDTEXT:
        w = h = 16;
        break;

    case W_COUNTERPERCBAR:
        w = 64;
        h = 6;
        break;
    }

    set("w", w);
    set("h", h);
    // display conditions: no counter, the current level
    set("req_counter", -1);
    set("req_litem_level", -1);
    set("req_lstate_level", -1);
    // text colours: the subscreen's text colour (ssctMISC, ssctTEXT)
    set("c_text_type", 255);
    set("c_text2_type", 255);

    switch(ty)
    {
    case W_COUNTER:
        set("ctr0", -1);
        set("ctr1", -1);
        set("ctr2", -1);
        set("maxdigits", 3);
        set("item", -1);
        set("infchar", 'A');
        break;

    case W_OLDCTR:
        set("item", -1);
        set("infchar", 'A');
        break;

    case W_BTNCOUNTER:
        set("maxdigits", 3);
        set("infchar", 'A');
        break;

    case W_MMAPTITLE: case W_TEXTBOX: case W_SELECTEDTEXT:
        set("tabsize", 4);
        break;

    case W_MMAP:
        set("compass_litems", 1);
        set("c_room_type", 254);    // ssctSYSTEM, -1
        set("c_room", -1);
        set("c_room_unvisited_type", 255);
        set("c_room_unvisited", 18);    // ssctDEFAULT
        set("c_bg_type", 255);
        set("c_bg", 18);
        break;

    case W_BGCOLOR:
        set("c_bg_type", 255);
        set("c_bg", 10);            // ssctSUBSCRBG
        break;

    case W_ITEMSLOT:
        set("item", -1);
        break;

    case W_LGAUGE: case W_MGAUGE: case W_MISCGAUGE: case W_ITMCOOLDOWNGAUGE:
        set("frames", 1);
        set("inf_item", -1);
        set("showdrain", -1);
        set("per_container", 1);
        set("points", 16);
        set("item", -1);
        set("button", 255);         // none (-1)
        break;

    case W_ITMCOOLDOWNTEXT:
        set("item", -1);
        set("button", 255);
        break;
    }

    for(size_t i = 0; more && i < more->f.size(); ++i)
    {
        if(TapeField *f = src.find(more->f[i].key.c_str()))
            *f = more->f[i];
        else
            src.f.push_back(more->f[i]);
    }

    Tape t;
    TapeParser p(&src, &t);

    if(!zq_tape_subscreen_widget(p, v, "", false))
        return false;

    out.clear();
    t.encode(out);
    return out.size() > 1 && q255_widget_extent(&out[1], out.size() - 1, v, (byte)ty) == out.size() - 1;
}

// A widget of a version `from` section laid out again for version `to`: the subscreen data
// editor's schema reads its fields and writes them in the new version's layout (fields the new
// version lacks are left out, new ones get their defaults). False when it can't be: a type the new
// version doesn't hold, or a result the player's reader doesn't read whole.
bool convert_widget(const Bytes &w, int from, int to, Bytes &out)
{
    if(from == to)
    {
        out = w;
        return true;
    }

    if(w.size() < 2 || w[0] < 1 || w[0] >= type_limit(to))
        return false;

    Tape fields, laid;
    TapeParser rd(w.data(), w.size(), 0, &fields);

    if(!zq_tape_subscreen_widget(rd, from, "", false) || rd.bad)
        return false;

    // a selectable widget's script: before version 21 a script word and (with a script) eight
    // InitDs ("script", "initd0".."initd7"); from 21 a script config ("script.script",
    // "script.argc", "script.initd<n>", instance values), which older versions can't hold
    struct Set
    {
        static void num(Tape &t, const std::string &key, int64_t value)
        {
            if(TapeField *f = t.find(key.c_str()))
                f->value = value;
            else
            {
                TapeField n;
                n.key = key;
                n.value = value;
                t.f.push_back(n);
            }
        }
    };

    if(from < 21 && to >= 21 && fields.find("script"))
    {
        Set::num(fields, "script.script", fields.get("script"));
        Set::num(fields, "script.argc", 8);

        for(int q = 0; q < 8; ++q)
            Set::num(fields, "script.initd" + std::to_string(q), fields.get(("initd" + std::to_string(q)).c_str()));
    }
    else if(from >= 21 && to < 21 && fields.find("script.script"))
    {
        if(fields.get("script.instc") > 0 || fields.get("script.argc") > 8)
            return false;

        Set::num(fields, "script", fields.get("script.script"));

        for(int q = 0; q < 8; ++q)
            Set::num(fields, "initd" + std::to_string(q), fields.get(("script.initd" + std::to_string(q)).c_str()));
    }

    TapeParser wr(&fields, &laid);

    if(!zq_tape_subscreen_widget(wr, to, "", false))
        return false;

    out.clear();
    laid.encode(out);
    return out.size() > 1 && out[0] == w[0] && q255_widget_extent(&out[1], out.size() - 1, to, out[0]) == out.size() - 1;
}

// A page's change transitions (version 16 on: their count, then each one's buttons, mode, target
// and flags and its transition: type, sound (a byte before version 17, a word from it), flags,
// argument count and arguments) laid out for version `to`. False when a sound doesn't fit.
bool convert_page_trans(const Bytes &t, int from, int to, Bytes &out)
{
    out.clear();

    if(to < 16)
        return true;

    if(from < 16 || t.empty())
    {
        out.assign(1, 0);
        return true;
    }

    if((from < 17) == (to < 17))
    {
        out = t;
        return true;
    }

    Reader r(t);
    const int n = r.c();
    out.push_back((byte)n);

    for(int i = 0; i < n && !r.bad; ++i)
    {
        for(int q = 0; q < 5; ++q)      // buttons, mode, target, flags; the transition's type
            out.push_back((byte)r.c());

        const int sfx = from < 17 ? r.c() : r.w();

        if(to < 17 && sfx > 255)
            return false;

        out.push_back((byte)(sfx & 0xFF));

        if(to >= 17)
            out.push_back((byte)(sfx >> 8));

        out.push_back((byte)r.c());     // flags (a word)
        out.push_back((byte)r.c());
        const int args = r.c();
        out.push_back((byte)args);

        for(int q = 0; q < args * 4 && !r.bad; ++q)
            out.push_back((byte)r.c());
    }

    return !r.bad && r.pos == t.size();
}

// ---- the editor's state ------------------------------------------------------------------------

enum { TEX_W = 256, TEX_H = 232 };     // the largest page (the overlay subscreen)
enum { MAX_UNDO = 100, MAX_WIDGETS = MAXSUBSCREENITEMS, MAX_PAGES = 255 };
enum { DRAG_NONE, DRAG_MOVE, DRAG_SIZE };

// A widget's box on the page as drawn, in subscreen pixels.
struct Box
{
    int x, y, w, h;
    bool drawn;         // the player draws it
    int unit_w, unit_h; // pixels per step of its width / height fields (0: they don't size it)
};

struct Editor
{
    bool open, close, focus, busy;
    bool is30;
    int v;                      // the section's version
    Model m;
    Bytes original;             // the section as the editor started
    std::vector<Model> undo;
    int sub, page;              // what's shown
    int sel;                    // the widget picked (-1: none)
    std::set<int> more;         // and others picked with it (Shift)
    int add_type;
    // view
    int zoom;
    bool grid, snap;
    int grid_x, grid_y, grid_xofs, grid_yofs;
    // the page as drawn (the game thread fills these)
    int canvas_h;
    std::vector<unsigned int> px;
    bool px_dirty;
    std::vector<Box> boxes;
    int boxes_for;              // the model change the boxes belong to
    int changes;                // counts model changes
    ImTextureData *tex;
    // a mouse drag on the page
    int drag;
    float drag_x, drag_y;
    std::vector<std::pair<int, std::pair<int, int> > > drag_start;     // widget, its x, y (or w, h)
    bool drag_undo;
    bool field_undo;            // a field of the inspector being typed in has its Undo step
    // copied widgets and a copied page (Edit > Copy, Page > Copy page); they outlast the editor
    // (and the quest), so each keeps the section version it is laid out for
    std::vector<Bytes> clip;
    int clip_sub, clip_page;    // where the widgets were copied from (-1: another session)
    int clip_v;
    Page page_clip;
    bool has_page_clip;
    int page_clip_v;
    bool ask_delete_page;       // asking whether to delete a page that holds widgets
    // view
    bool fit;                   // pick the largest zoom the window shows whole (next frame)
    bool all_items;             // the preview has every item, as upstream's "Show Unowned Items"
    bool ask_close;             // asking whether to drop the changes
    bool placed;                // the window has been shown
    ImVec2 vp_size;             // ZQuest's window's size then
    // requests to the game thread
    int want_props;             // a widget's property list
    bool want_ok;
    std::string error;

    Editor() : open(false), close(false), focus(false), busy(false), is30(false), v(0), sub(0), page(0), sel(-1),
               add_type(W_TEXT), zoom(2), grid(false), snap(false), grid_x(8), grid_y(8), grid_xofs(0), grid_yofs(0), canvas_h(176),
               px_dirty(false), boxes_for(-1), changes(0), tex(NULL), drag(DRAG_NONE), drag_x(0), drag_y(0), drag_undo(false),
               field_undo(false), clip_sub(-1), clip_page(-1), clip_v(0), has_page_clip(false), page_clip_v(0), ask_delete_page(false),
               fit(true), all_items(false), ask_close(false), placed(false), want_props(-1), want_ok(false) {}
};

Editor g_ed;

Sub *cur_sub()
{
    return g_ed.sub >= 0 && g_ed.sub < (int)g_ed.m.size() ? &g_ed.m[g_ed.sub] : NULL;
}

Page *cur_page()
{
    Sub *s = cur_sub();
    return s && g_ed.page >= 0 && g_ed.page < (int)s->pages.size() ? &s->pages[g_ed.page] : NULL;
}

int widget_count()
{
    Page *p = cur_page();
    return p ? (int)p->widgets.size() : 0;
}

// The page height upstream's editor and player give each kind of subscreen.
int page_height(int list)
{
    enum { QR_ACTIVE_SUB_IGNORE_8PX = 476, QR_HIDE_BOTTOM_8_PIXELS = 482 };
    const bool hide8 = rule_on(QR_HIDE_BOTTOM_8_PIXELS);

    switch(list)
    {
    case 1:
        return 56;

    case 2:
        return 224 + (hide8 ? 0 : 8);
    }

    return 168 + ((rule_on(QR_ACTIVE_SUB_IGNORE_8PX) || hide8) ? 0 : 8);
}

// Before a change: the model as it was, for Undo.
void remember()
{
    Editor &e = g_ed;
    e.undo.push_back(e.m);

    if(e.undo.size() > MAX_UNDO)
        e.undo.erase(e.undo.begin());
}

void changed()
{
    ++g_ed.changes;
}

void clear_picks()
{
    g_ed.sel = -1;
    g_ed.more.clear();
}

std::vector<int> picked()
{
    std::set<int> all(g_ed.more);

    if(g_ed.sel >= 0)
        all.insert(g_ed.sel);

    std::vector<int> out;

    for(std::set<int>::const_iterator it = all.begin(); it != all.end(); ++it)
        if(*it >= 0 && *it < widget_count())
            out.push_back(*it);

    return out;
}

void show_page(int sub, int page)
{
    Editor &e = g_ed;

    if(sub < 0 || sub >= (int)e.m.size())
        return;

    e.sub = sub;
    e.page = (std::max)(0, (std::min)(page, (int)e.m[sub].pages.size() - 1));
    e.canvas_h = page_height(e.m[sub].list);
    clear_picks();
    e.drag = DRAG_NONE;
    changed();
}

// ---- edits ---------------------------------------------------------------------------------------

void set_pos(Bytes &w, int x, int y)
{
    put16(w, WO_X, (std::max)(-32768, (std::min)(32767, x)));
    put16(w, WO_Y, (std::max)(-32768, (std::min)(32767, y)));
}

void set_size(Bytes &w, int wd, int ht)
{
    put16(w, WO_W, (std::max)(0, (std::min)(65535, wd)));
    put16(w, WO_H, (std::max)(0, (std::min)(65535, ht)));
}

int snap_to(int v, int step, int ofs)
{
    if(step <= 0)
        return v;

    return ofs + (int)floor((v - ofs) / (double)step + 0.5) * step;
}

void move_picked(int dx, int dy)
{
    Page *p = cur_page();
    std::vector<int> ws = picked();

    if(!p || ws.empty() || (!dx && !dy))
        return;

    remember();

    for(size_t i = 0; i < ws.size(); ++i)
    {
        Bytes &w = p->widgets[ws[i]];
        set_pos(w, get16s(w, WO_X) + dx, get16s(w, WO_Y) + dy);
    }

    changed();
}

void add_widget(int ty)
{
    Editor &e = g_ed;
    Page *p = cur_page();

    if(!p)
        return;

    if((int)p->widgets.size() >= MAX_WIDGETS)
    {
        e.error = "This page is full: it holds " + std::to_string((int)MAX_WIDGETS) + " widgets, as many as the player draws.";
        return;
    }

    if(ty < 1 || ty >= type_limit(e.v))
    {
        e.error = std::string("A ") + type_name(ty) + " can't go in this subscreen format (version " + std::to_string(e.v) +
                  " holds 2.55's widget types only).";
        return;
    }

    Bytes w;
    const int x = e.snap ? snap_to(8, e.grid_x, e.grid_xofs) : 8, y = e.snap ? snap_to(8, e.grid_y, e.grid_yofs) : 8;

    if(!new_widget(e.v, ty, x, y, w))
    {
        e.error = std::string("A new ") + type_name(ty) + " can't be laid out in this subscreen format.";
        return;
    }

    remember();
    p->widgets.push_back(w);
    clear_picks();
    e.sel = (int)p->widgets.size() - 1;
    e.error.clear();
    changed();
}

void delete_picked()
{
    Page *p = cur_page();
    std::vector<int> ws = picked();

    if(!p || ws.empty())
        return;

    remember();

    for(size_t i = ws.size(); i-- > 0;)
        p->widgets.erase(p->widgets.begin() + ws[i]);

    clear_picks();

    if(!p->widgets.empty())
        g_ed.sel = (std::min)(ws[0], (int)p->widgets.size() - 1);

    changed();
}

// As upstream's Duplicate: copies of the picked widgets at the end of the page, a grid step (at
// least 8 pixels) to the right and down.
void duplicate_picked()
{
    Editor &e = g_ed;
    Page *p = cur_page();
    std::vector<int> ws = picked();

    if(!p || ws.empty())
        return;

    if((int)(p->widgets.size() + ws.size()) > MAX_WIDGETS)
    {
        e.error = "This page can't hold that many more widgets.";
        return;
    }

    remember();
    clear_picks();
    const int dx = (std::max)(e.grid_x, 8), dy = (std::max)(e.grid_y, 8);

    for(size_t i = 0; i < ws.size(); ++i)
    {
        Bytes w = p->widgets[ws[i]];
        set_pos(w, get16s(w, WO_X) + dx, get16s(w, WO_Y) + dy);
        p->widgets.push_back(w);

        if(e.sel < 0)
            e.sel = (int)p->widgets.size() - 1;
        else
            e.more.insert((int)p->widgets.size() - 1);
    }

    changed();
}

// The picked widget one place later (drawn over the next) or earlier, or to the end or start.
void reorder(int how)
{
    Editor &e = g_ed;
    Page *p = cur_page();

    if(!p || e.sel < 0 || e.sel >= (int)p->widgets.size())
        return;

    const int n = (int)p->widgets.size(), at = e.sel;
    int to = at;

    switch(how)
    {
    case -1: to = at - 1; break;
    case 1: to = at + 1; break;
    case -2: to = 0; break;
    case 2: to = n - 1; break;
    }

    if(to < 0 || to >= n || to == at)
        return;

    remember();
    Bytes w = p->widgets[at];
    p->widgets.erase(p->widgets.begin() + at);
    p->widgets.insert(p->widgets.begin() + to, w);
    clear_picks();
    e.sel = to;
    changed();
}

// The default button positions (word: page in the low byte) as upstream's page changes keep them.
void fix_default_buttons(Sub &s, int how, int a, int b)
{
    if(!s.def_at)
        return;

    for(int q = 0; q < 4; ++q)
    {
        const size_t at = s.def_at + q * 2;
        int d = get16(s.head, at);

        if(how == 0)    // page a deleted
        {
            if((d & 0xFF) > a && (d & 0xFF) != 0xFF)
                --d;
            else if((d & 0xFF) == a)
                d = 255;
        }
        else            // pages a and b swapped
        {
            if((d & 0xFF) == a)
                d = (d & 0xFF00) | b;
            else if((d & 0xFF) == b)
                d = (d & 0xFF00) | a;
        }

        put16(s.head, at, d);
    }
}

// Why a subscreen's pages can't be added to or moved ("" when they can).
std::string pages_fixed(const Sub &s)
{
    return s.paged() ? std::string() : std::string("Only active and map subscreens have pages: upstream keeps one page of a ") +
           (s.kind == 1 ? "passive" : "overlay") + " subscreen.";
}

void add_page()
{
    Editor &e = g_ed;
    Sub *s = cur_sub();

    if(!s)
        return;

    if(!s->paged())
    {
        e.error = pages_fixed(*s);
        return;
    }

    if((int)s->pages.size() >= MAX_PAGES)
    {
        e.error = "A subscreen holds at most 255 pages.";
        return;
    }

    remember();
    Page p;
    p.cursor.assign(4, 0);
    const int at = (std::min)(e.page + 1, (int)s->pages.size());
    s->pages.insert(s->pages.begin() + at, p);

    // as upstream's add_page: the new page is swapped down into place
    for(int i = (int)s->pages.size() - 1; i > at; --i)
        fix_default_buttons(*s, 1, i, i - 1);

    show_page(e.sub, at);
}

// As upstream's delete_page: the page before is shown; the last page left is emptied instead
// (SubscrPage::clear: its widgets and cursor; its change transitions stay). A passive or overlay
// subscreen keeps its one page (one it was loaded with more than one can lose the others).
void delete_page()
{
    Editor &e = g_ed;
    Sub *s = cur_sub();

    if(!s || s->pages.empty())
        return;

    if(!s->paged() && s->pages.size() == 1)
    {
        e.error = pages_fixed(*s);
        return;
    }

    remember();

    if(s->pages.size() == 1)
    {
        s->pages[0].widgets.clear();
        s->pages[0].cursor.assign(4, 0);
    }
    else
    {
        fix_default_buttons(*s, 0, e.page, 0);
        s->pages.erase(s->pages.begin() + e.page);
    }

    show_page(e.sub, (std::max)(0, e.page - 1));
}

// Delete page from the window: as upstream's, asked first when the page holds widgets.
void delete_page_asked()
{
    Page *p = cur_page();

    if(p && !p->widgets.empty())
        g_ed.ask_delete_page = true;
    else
        delete_page();
}

void move_page(int dir)
{
    Editor &e = g_ed;
    Sub *s = cur_sub();
    const int to = e.page + dir;

    if(!s || to < 0 || to >= (int)s->pages.size())
        return;

    if(!s->paged())
    {
        e.error = pages_fixed(*s);
        return;
    }

    remember();
    std::swap(s->pages[e.page], s->pages[to]);
    fix_default_buttons(*s, 1, e.page, to);
    show_page(e.sub, to);
}

void select_all()
{
    Editor &e = g_ed;
    clear_picks();

    for(int i = 0; i < widget_count(); ++i)
    {
        if(e.sel < 0)
            e.sel = i;
        else
            e.more.insert(i);
    }
}

// Edit > Copy: the picked widgets, for Paste (on this page or another).
void copy_picked()
{
    Editor &e = g_ed;
    Page *p = cur_page();
    std::vector<int> ws = picked();

    if(!p || ws.empty())
        return;

    e.clip.clear();

    for(size_t i = 0; i < ws.size(); ++i)
        e.clip.push_back(p->widgets[ws[i]]);

    e.clip_sub = e.sub;
    e.clip_page = e.page;
    e.clip_v = e.v;
}

// Edit > Paste: the copied widgets at the end of this page, where they were (a grid step to the
// right and down on the page they came from, as Duplicate puts them), and picked. Widgets copied
// from another quest were laid out for this section's version when the editor opened (start).
void paste()
{
    Editor &e = g_ed;
    Page *p = cur_page();

    if(!p || e.clip.empty())
        return;

    if(e.clip_v != e.v)
    {
        e.error = "The copied widgets aren't in this subscreen format.";
        return;
    }

    for(size_t i = 0; i < e.clip.size(); ++i)
    {
        if(e.clip[i].empty() || e.clip[i][0] < 1 || e.clip[i][0] >= type_limit(e.v))
        {
            e.error = "A copied widget's type can't go in this subscreen format.";
            return;
        }
    }

    if((int)(p->widgets.size() + e.clip.size()) > MAX_WIDGETS)
    {
        e.error = "This page can't hold that many more widgets.";
        return;
    }

    remember();
    clear_picks();
    const bool same = e.clip_sub == e.sub && e.clip_page == e.page;
    const int dx = same ? (std::max)(e.grid_x, 8) : 0, dy = same ? (std::max)(e.grid_y, 8) : 0;

    for(size_t i = 0; i < e.clip.size(); ++i)
    {
        Bytes w = e.clip[i];
        set_pos(w, get16s(w, WO_X) + dx, get16s(w, WO_Y) + dy);
        p->widgets.push_back(w);

        if(e.sel < 0)
            e.sel = (int)p->widgets.size() - 1;
        else
            e.more.insert((int)p->widgets.size() - 1);
    }

    e.error.clear();
    changed();
}

// As upstream's align_objects: the other picked widgets moved so that the chosen edge or centre of
// each lines up with the current widget's, by their boxes as drawn.
enum { AL_LEFT, AL_CENTER, AL_RIGHT, AL_TOP, AL_MIDDLE, AL_BOTTOM };

void align_picked(int how)
{
    Editor &e = g_ed;
    Page *p = cur_page();
    std::vector<int> ws = picked();

    if(!p || e.sel < 0 || ws.size() < 2 || e.boxes_for != e.changes || e.boxes.size() != p->widgets.size())
        return;

    const Box &c = e.boxes[e.sel];
    bool any = false;

    for(size_t i = 0; i < ws.size(); ++i)
    {
        if(ws[i] == e.sel)
            continue;

        const Box &b = e.boxes[ws[i]];
        int dx = 0, dy = 0;

        switch(how)
        {
        case AL_LEFT: dx = c.x - b.x; break;
        case AL_CENTER: dx = (c.x + c.w / 2) - (b.x + b.w / 2); break;
        case AL_RIGHT: dx = (c.x + c.w - 1) - (b.x + b.w - 1); break;
        case AL_TOP: dy = c.y - b.y; break;
        case AL_MIDDLE: dy = (c.y + c.h / 2) - (b.y + b.h / 2); break;
        case AL_BOTTOM: dy = (c.y + c.h - 1) - (b.y + b.h - 1); break;
        }

        if(!dx && !dy)
            continue;

        if(!any)
            remember();

        any = true;
        Bytes &w = p->widgets[ws[i]];
        set_pos(w, get16s(w, WO_X) + dx, get16s(w, WO_Y) + dy);
    }

    if(any)
        changed();
}

// As upstream's grid_snap_objects: each picked widget moved so that the chosen edge or centre of
// its box is on the nearest grid line.
void grid_snap_picked(int how)
{
    Editor &e = g_ed;
    Page *p = cur_page();
    std::vector<int> ws = picked();

    if(!p || ws.empty() || e.boxes_for != e.changes || e.boxes.size() != p->widgets.size())
        return;

    bool any = false;

    for(size_t i = 0; i < ws.size(); ++i)
    {
        const Box &b = e.boxes[ws[i]];
        int dx = 0, dy = 0;

        switch(how)
        {
        case AL_LEFT: dx = snap_to(b.x, e.grid_x, e.grid_xofs) - b.x; break;
        case AL_CENTER: dx = snap_to(b.x + b.w / 2, e.grid_x, e.grid_xofs) - (b.x + b.w / 2); break;
        case AL_RIGHT: dx = snap_to(b.x + b.w - 1, e.grid_x, e.grid_xofs) - (b.x + b.w - 1); break;
        case AL_TOP: dy = snap_to(b.y, e.grid_y, e.grid_yofs) - b.y; break;
        case AL_MIDDLE: dy = snap_to(b.y + b.h / 2, e.grid_y, e.grid_yofs) - (b.y + b.h / 2); break;
        case AL_BOTTOM: dy = snap_to(b.y + b.h - 1, e.grid_y, e.grid_yofs) - (b.y + b.h - 1); break;
        }

        if(!dx && !dy)
            continue;

        if(!any)
            remember();

        any = true;
        Bytes &w = p->widgets[ws[i]];
        set_pos(w, get16s(w, WO_X) + dx, get16s(w, WO_Y) + dy);
    }

    if(any)
        changed();
}

// As upstream's distribute_objects: three or more picked widgets, in order of the chosen edge or
// centre, spaced evenly between the first and the last (which stay).
void distribute_picked(int how)
{
    Editor &e = g_ed;
    Page *p = cur_page();
    std::vector<int> ws = picked();

    if(!p || ws.size() < 3 || e.boxes_for != e.changes || e.boxes.size() != p->widgets.size())
        return;

    // each widget's edge or centre, and its index (the order where two are level)
    std::vector<std::pair<int, int> > at;

    for(size_t i = 0; i < ws.size(); ++i)
    {
        const Box &b = e.boxes[ws[i]];
        int v = 0;

        switch(how)
        {
        case AL_LEFT: v = b.x; break;
        case AL_CENTER: v = b.x + b.w / 2; break;
        case AL_RIGHT: v = b.x + b.w - 1; break;
        case AL_TOP: v = b.y; break;
        case AL_MIDDLE: v = b.y + b.h / 2; break;
        case AL_BOTTOM: v = b.y + b.h - 1; break;
        }

        at.push_back(std::make_pair(v, ws[i]));
    }

    std::sort(at.begin(), at.end());
    const int n = (int)at.size(), span = at[n - 1].first - at[0].first;
    const bool horiz = how == AL_LEFT || how == AL_CENTER || how == AL_RIGHT;
    bool any = false;

    for(int i = 1; i < n - 1; ++i)
    {
        const int d = at[0].first + span * i / (n - 1) - at[i].first;

        if(!d)
            continue;

        if(!any)
            remember();

        any = true;
        Bytes &w = p->widgets[at[i].second];

        if(horiz)
            set_pos(w, get16s(w, WO_X) + d, get16s(w, WO_Y));
        else
            set_pos(w, get16s(w, WO_X), get16s(w, WO_Y) + d);
    }

    if(any)
        changed();
}

// As upstream's Reverse: the picked widgets' drawing order turned around (the first picked one
// drawn where the last was, and so on); the others stay where they are.
void reverse_picked()
{
    Editor &e = g_ed;
    Page *p = cur_page();
    std::vector<int> ws = picked();

    if(!p || ws.size() < 2)
        return;

    remember();

    // the same places stay picked; the current widget's moves with it
    for(size_t i = 0, j = ws.size() - 1; i < j; ++i, --j)
    {
        std::swap(p->widgets[ws[i]], p->widgets[ws[j]]);

        if(e.sel == ws[i])
            e.sel = ws[j];
        else if(e.sel == ws[j])
            e.sel = ws[i];
    }

    changed();
}

// Page > Copy page / Paste page: as upstream's, the copy goes in as a new page after this one.
void copy_page()
{
    Editor &e = g_ed;
    Sub *s = cur_sub();

    if(!s || !s->paged())
        return;

    if(Page *p = cur_page())
    {
        e.page_clip = *p;
        e.has_page_clip = true;
        e.page_clip_v = e.v;
    }
}

void paste_page()
{
    Editor &e = g_ed;
    Sub *s = cur_sub();

    if(!s || !e.has_page_clip)
        return;

    if(!s->paged())
    {
        e.error = pages_fixed(*s);
        return;
    }

    if(e.page_clip_v != e.v)
    {
        e.error = "The copied page isn't in this subscreen format.";
        return;
    }

    if((int)s->pages.size() >= MAX_PAGES)
    {
        e.error = "A subscreen holds at most 255 pages.";
        return;
    }

    remember();
    const int at = (std::min)(e.page + 1, (int)s->pages.size());
    Page p = e.page_clip;
    s->pages.insert(s->pages.begin() + at, p);

    for(int i = (int)s->pages.size() - 1; i > at; --i)
        fix_default_buttons(*s, 1, i, i - 1);

    show_page(e.sub, at);
}

// whole subscreens, the settings dialogs, the wizards and the smaller options
#include "zq_subscr_layout_more.h"

// Whether the subscreens differ from how the editor started.
bool edited()
{
    Bytes now;
    encode(g_ed.m, g_ed.v, now);

    if(now != g_ed.original)
        return true;

    // the same bytes can still be other subscreens to the DMaps (one deleted, its copy kept)
    RefMap r;
    ref_map(g_ed.m, g_more.orig_count, r);
    return !r.same;
}

void do_undo()
{
    Editor &e = g_ed;

    if(e.undo.empty())
        return;

    e.m = e.undo.back();
    e.undo.pop_back();
    const int sub = (std::min)(e.sub, (int)e.m.size() - 1);
    show_page(sub, e.page);
}

// A menu of the widget types this section's version holds (2.55.17's up to version 12); a pick
// adds one to the page.
void new_widget_menu()
{
    Editor &e = g_ed;
    const int last = type_limit(e.v);

    for(int t = 1; t < last; ++t)
    {
        if(ImGui::MenuItem(type_name(t)))
        {
            e.add_type = t;
            add_widget(t);
        }
    }
}

// ---- the window ----------------------------------------------------------------------------------

void canvas_texture()
{
    Editor &e = g_ed;

    if(!e.tex)
    {
        e.tex = IM_NEW(ImTextureData)();
        e.tex->Create(ImTextureFormat_RGBA32, TEX_W, TEX_H);
        memset(e.tex->GetPixels(), 0, (size_t)TEX_W * TEX_H * 4);
        ImGui::RegisterUserTexture(e.tex);
        e.px_dirty = true;
    }

    if(e.px_dirty && e.px.size() == (size_t)TEX_W * TEX_H)
    {
        memcpy(e.tex->GetPixels(), &e.px[0], e.px.size() * 4);

        if(e.tex->Status == ImTextureStatus_OK || e.tex->Status == ImTextureStatus_WantUpdates)
            ImTextureDataQueueUpload(e.tex, 0, 0, TEX_W, TEX_H);

        e.px_dirty = false;
    }
}

// The widget under a point of the page: the last drawn first, as upstream picks.
int widget_at(float x, float y)
{
    Editor &e = g_ed;

    if(e.boxes_for != e.changes)
        return -1;

    for(int i = (int)e.boxes.size() - 1; i >= 0; --i)
    {
        const Box &b = e.boxes[i];

        if(x >= b.x && y >= b.y && x < b.x + (std::max)(b.w, 1) && y < b.y + (std::max)(b.h, 1))
            return i;
    }

    return -1;
}

void canvas()
{
    Editor &e = g_ed;
    Page *p = cur_page();
    const float z = e.zoom * zcui::scale();
    const ImVec2 size(TEX_W * z, e.canvas_h * z);
    const ImVec2 o = ImGui::GetCursorScreenPos();
    canvas_texture();
    ImGui::Image(e.tex->GetTexRef(), size, ImVec2(0, 0), ImVec2(1, e.canvas_h / (float)TEX_H));
    ImGui::SetCursorScreenPos(o);
    ImGui::InvisibleButton("##page", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ui_mark("page");
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float mx = (mouse.x - o.x) / z, my = (mouse.y - o.y) / z;
    const bool boxes_now = e.boxes_for == e.changes && p && e.boxes.size() == p->widgets.size();

    // the size handle: the picked widget's bottom right corner
    bool on_handle = false;

    if(boxes_now && e.sel >= 0 && e.sel < (int)e.boxes.size())
    {
        const Box &b = e.boxes[e.sel];
        const float hx = b.x + (std::max)(b.w, 1), hy = b.y + (std::max)(b.h, 1), r = 4.0f / e.zoom;
        on_handle = (b.unit_w || b.unit_h) && fabs(mx - hx) <= r && fabs(my - hy) <= r;
    }

    if(ImGui::IsItemActivated() && p && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const bool shift = ImGui::GetIO().KeyShift;
        e.drag = DRAG_NONE;
        e.drag_start.clear();

        if(on_handle)
        {
            const Bytes &w = p->widgets[e.sel];
            e.drag = DRAG_SIZE;
            e.drag_start.push_back(std::make_pair(e.sel, std::make_pair(get16(w, WO_W), get16(w, WO_H))));
        }
        else
        {
            const int hit = widget_at(mx, my);

            if(hit < 0)
            {
                if(!shift)
                    clear_picks();
            }
            else if(shift)
            {
                // Shift: adds the widget to the picked ones, or takes it out
                if(hit == e.sel)
                {
                    e.sel = e.more.empty() ? -1 : *e.more.begin();
                    e.more.erase(e.sel);
                }
                else if(e.more.count(hit))
                    e.more.erase(hit);
                else
                {
                    if(e.sel >= 0)
                        e.more.insert(e.sel);
                    e.sel = hit;
                }
            }
            else
            {
                if(hit != e.sel && !e.more.count(hit))
                    clear_picks();
                else if(e.sel >= 0 && hit != e.sel)
                {
                    e.more.insert(e.sel);
                    e.more.erase(hit);
                }

                e.sel = hit;
                e.drag = DRAG_MOVE;
                std::vector<int> ws = picked();

                for(size_t i = 0; i < ws.size(); ++i)
                {
                    const Bytes &w = p->widgets[ws[i]];
                    e.drag_start.push_back(std::make_pair(ws[i], std::make_pair(get16s(w, WO_X), get16s(w, WO_Y))));
                }
            }
        }

        e.drag_x = mx;
        e.drag_y = my;
        e.drag_undo = false;
    }

    if(e.drag != DRAG_NONE && ImGui::IsItemActive() && p && !e.drag_start.empty())
    {
        int dx = (int)floor(mx - e.drag_x + 0.5f), dy = (int)floor(my - e.drag_y + 0.5f);
        const int first = e.drag_start[0].first;

        if(e.drag == DRAG_MOVE)
        {
            // snapped: the widget held moves to grid points, the rest with it
            if(e.snap || ImGui::GetIO().KeyCtrl)
            {
                const int x0 = e.drag_start[0].second.first, y0 = e.drag_start[0].second.second;
                dx = snap_to(x0 + dx, e.grid_x, e.grid_xofs) - x0;
                dy = snap_to(y0 + dy, e.grid_y, e.grid_yofs) - y0;
            }

            bool moves = false;

            for(size_t i = 0; i < e.drag_start.size(); ++i)
            {
                const Bytes &w = p->widgets[e.drag_start[i].first];
                moves = moves || get16s(w, WO_X) != e.drag_start[i].second.first + dx ||
                        get16s(w, WO_Y) != e.drag_start[i].second.second + dy;
            }

            if(moves)
            {
                if(!e.drag_undo)
                {
                    remember();
                    e.drag_undo = true;
                }

                for(size_t i = 0; i < e.drag_start.size(); ++i)
                    set_pos(p->widgets[e.drag_start[i].first], e.drag_start[i].second.first + dx, e.drag_start[i].second.second + dy);

                changed();
            }
        }
        else if(first >= 0 && first < (int)p->widgets.size() && first < (int)e.boxes.size())
        {
            // the size, in the steps the widget's fields count (a frame's are 8 pixels)
            const Box &b = e.boxes[first];
            const int w0 = e.drag_start[0].second.first, h0 = e.drag_start[0].second.second;
            int nw = w0, nh = h0;

            if(b.unit_w)
                nw = (std::max)(1, w0 + (int)floor(dx / (double)b.unit_w + 0.5));
            if(b.unit_h)
                nh = (std::max)(1, h0 + (int)floor(dy / (double)b.unit_h + 0.5));

            if(e.snap && b.unit_w == 1)
                nw = (std::max)(1, snap_to(b.x + nw, e.grid_x, e.grid_xofs) - b.x);
            if(e.snap && b.unit_h == 1)
                nh = (std::max)(1, snap_to(b.y + nh, e.grid_y, e.grid_yofs) - b.y);

            Bytes &w = p->widgets[first];

            if(nw != get16(w, WO_W) || nh != get16(w, WO_H))
            {
                if(!e.drag_undo)
                {
                    remember();
                    e.drag_undo = true;
                }

                set_size(w, nw, nh);
                changed();
            }
        }
    }

    if(ImGui::IsItemDeactivated())
        e.drag = DRAG_NONE;

    // right click: the widget's menu (or the page's)
    if(ImGui::IsItemClicked(ImGuiMouseButton_Right) && p)
    {
        const int hit = widget_at(mx, my);

        if(hit >= 0 && hit != e.sel && !e.more.count(hit))
        {
            clear_picks();
            e.sel = hit;
        }

        g_more.menu_x = (int)floor(mx);     // a wizard started from the menu starts here
        g_more.menu_y = (int)floor(my);
        ImGui::OpenPopup("##pagemenu");
    }

    if(ImGui::BeginPopup("##pagemenu"))
    {
        const bool any = e.sel >= 0;

        if(ImGui::MenuItem("Properties...", NULL, false, any))
            e.want_props = e.sel;
        if(ImGui::MenuItem("Copy", "Ctrl+C", false, any))
            copy_picked();
        if(ImGui::MenuItem("Paste", "Ctrl+V", false, !e.clip.empty() && e.clip_v == e.v))
            paste();
        if(ImGui::MenuItem("Paste properties", NULL, false, any && !e.clip.empty() && e.clip_v == e.v))
            paste_properties();
        if(ImGui::MenuItem("Duplicate", "Ctrl+D", false, any))
            duplicate_picked();
        if(ImGui::MenuItem("Delete", "Del", false, any))
            delete_picked();
        ImGui::Separator();
        if(ImGui::MenuItem("Bring to front", NULL, false, any))
            reorder(2);
        if(ImGui::MenuItem("Bring forward", NULL, false, any))
            reorder(1);
        if(ImGui::MenuItem("Send backward", NULL, false, any))
            reorder(-1);
        if(ImGui::MenuItem("Send to back", NULL, false, any))
            reorder(-2);
        ImGui::Separator();

        if(ImGui::BeginMenu("New widget"))
        {
            new_widget_menu();
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Wizards"))
        {
            wizard_menu(true);
            ImGui::EndMenu();
        }

        ImGui::EndPopup();
    }

    // what's drawn over the page: the grid, every widget's outline, the picked ones
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(o, ImVec2(o.x + size.x, o.y + size.y), true);

    if(e.grid && e.grid_x > 0 && e.grid_y > 0)
    {
        const ImU32 gc = IM_COL32(128, 128, 128, 96);

        for(int x = e.grid_xofs % e.grid_x; x < TEX_W; x += e.grid_x)
            dl->AddLine(ImVec2(o.x + x * z, o.y), ImVec2(o.x + x * z, o.y + size.y), gc);

        for(int y = e.grid_yofs % e.grid_y; y < e.canvas_h; y += e.grid_y)
            dl->AddLine(ImVec2(o.x, o.y + y * z), ImVec2(o.x + size.x, o.y + y * z), gc);
    }

    if(boxes_now)
    {
        const int hover = ImGui::IsItemHovered() ? widget_at(mx, my) : -1;
        hint_draws(dl, o, z);

        for(int i = 0; i < (int)e.boxes.size(); ++i)
        {
            const Box &b = e.boxes[i];
            const ImVec2 a(o.x + b.x * z, o.y + b.y * z), c(o.x + (b.x + (std::max)(b.w, 1)) * z, o.y + (b.y + (std::max)(b.h, 1)) * z);
            ImU32 col = 0;
            float th = 1.0f;

            if(i == e.sel)
            {
                col = IM_COL32(255, 230, 40, 255);
                th = 2.0f;
            }
            else if(e.more.count(i))
            {
                col = IM_COL32(60, 220, 255, 255);
                th = 2.0f;
            }
            else if(i == hover)
                col = IM_COL32(255, 255, 255, 160);
            else if(!b.drawn)
                col = IM_COL32(200, 120, 255, 140);     // the player draws nothing for it

            if(col)
                dl->AddRect(a, c, col, 0.0f, 0, th);
        }

        if(e.sel >= 0 && e.sel < (int)e.boxes.size() && (e.boxes[e.sel].unit_w || e.boxes[e.sel].unit_h))
        {
            const Box &b = e.boxes[e.sel];
            const ImVec2 c(o.x + (b.x + (std::max)(b.w, 1)) * z, o.y + (b.y + (std::max)(b.h, 1)) * z);
            dl->AddRectFilled(ImVec2(c.x - 3, c.y - 3), ImVec2(c.x + 3, c.y + 3), IM_COL32(255, 230, 40, 255));
        }
    }

    dl->PopClipRect();
}

// The picked widget's place, size and visibility, and the buttons that act on it.
void inspector()
{
    Editor &e = g_ed;
    Page *p = cur_page();
    const float sc = zcui::scale();

    if(!p || e.sel < 0 || e.sel >= (int)p->widgets.size())
    {
        ImGui::TextDisabled("No widget picked: click one on the page or in the list.");
        return;
    }

    Bytes &w = p->widgets[e.sel];
    const std::string label = widget_label(w, e.v);
    ImGui::Text("Widget %d: %s", e.sel, type_name(w[0]));

    if(!label.empty())
        ImGui::SameLine(), ImGui::TextDisabled("\"%s\"", label.c_str());

    if(!e.more.empty())
        ImGui::SameLine(), ImGui::TextDisabled("(+%d more picked)", (int)e.more.size());

    if(e.boxes_for == e.changes && e.sel < (int)e.boxes.size() && !e.boxes[e.sel].drawn)
        ImGui::TextDisabled("The player draws nothing for this one (outlined in purple).");

    int x = get16s(w, WO_X), y = get16s(w, WO_Y), wd = get16(w, WO_W), ht = get16(w, WO_H);
    bool edited = false;
    ImGui::PushItemWidth(100 * sc);
    edited |= ImGui::InputInt("X##wx", &x);
    ImGui::SameLine();
    edited |= ImGui::InputInt("Y##wy", &y);
    edited |= ImGui::InputInt("W##ww", &wd);
    ImGui::SameLine();
    edited |= ImGui::InputInt("H##wh", &ht);
    ImGui::PopItemWidth();

    if(edited)
    {
        // one Undo step for each field typed in (or each +/- press)
        if(!e.field_undo)
            remember();

        e.field_undo = true;
        set_pos(w, x, y);
        set_size(w, wd, ht);
        changed();
    }

    if(!ImGui::IsAnyItemActive())
        e.field_undo = false;

    // where the widget shows (upstream's "Visible when the subscreen is")
    int pf = w[WO_POS];
    bool pe = false;
    ImGui::TextUnformatted("Shown:");
    ImGui::SameLine();
    pe |= ImGui::CheckboxFlags("up", &pf, sspUP);
    ui_mark("shown.up", (pf & sspUP) != 0);
    ImGui::SameLine();
    pe |= ImGui::CheckboxFlags("down", &pf, sspDOWN);
    ui_mark("shown.down", (pf & sspDOWN) != 0);
    ImGui::SameLine();
    pe |= ImGui::CheckboxFlags("scrolling", &pf, sspSCROLLING);
    ui_mark("shown.scrolling", (pf & sspSCROLLING) != 0);

    if(pe)
    {
        remember();
        w[WO_POS] = (byte)pf;
        changed();
    }

    if(ImGui::Button("Properties..."))
        e.want_props = e.sel;

    ImGui::SameLine();

    if(ImGui::Button("Duplicate"))
        duplicate_picked();

    ImGui::SameLine();

    if(ImGui::Button("Delete"))
        delete_picked();
}

// The page's widgets, in drawing order (later ones over earlier ones), and adding one.
void widget_list(float height)
{
    Editor &e = g_ed;
    Page *p = cur_page();
    const float sc = zcui::scale();
    const bool boxes_now = p && e.boxes_for == e.changes && e.boxes.size() == p->widgets.size();

    if(ImGui::BeginChild("widgets", ImVec2(0, height), ImGuiChildFlags_Borders))
    {
        ImGuiListClipper clip;
        clip.Begin(p ? (int)p->widgets.size() : 0);

        while(clip.Step())
        {
            for(int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
            {
                const Bytes &w = p->widgets[i];
                const std::string label = widget_label(w, e.v);
                char row[200];
                snprintf(row, sizeof(row), "%d: %s%s%s (%d, %d)%s##w%d", i, type_name(w[0]), label.empty() ? "" : ": ", label.c_str(),
                         get16s(w, WO_X), get16s(w, WO_Y), boxes_now && !e.boxes[i].drawn ? " (not drawn)" : "", i);

                if(ImGui::Selectable(row, i == e.sel || e.more.count(i) > 0))
                {
                    if(ImGui::GetIO().KeyShift && e.sel >= 0 && i != e.sel)
                    {
                        if(e.more.count(i))
                            e.more.erase(i);
                        else
                            e.more.insert(i);
                    }
                    else
                    {
                        clear_picks();
                        e.sel = i;
                    }
                }

                if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    e.want_props = i;
            }
        }
    }

    ImGui::EndChild();

    // a new widget: the types the section's version holds (2.55.17's up to version 12)
    const int last = type_limit(e.v);
    ImGui::SetNextItemWidth((std::max)(80.0f * sc, ImGui::GetContentRegionAvail().x - 110 * sc));

    if(ImGui::BeginCombo("##addtype", type_name(e.add_type), ImGuiComboFlags_HeightLarge))
    {
        for(int t = 1; t < last; ++t)
            if(ImGui::Selectable(type_name(t), t == e.add_type))
                e.add_type = t;

        ImGui::EndCombo();
    }

    ImGui::SameLine();

    if(ImGui::Button("Add widget"))
        add_widget(e.add_type);

    const float bw = 64 * sc;

    if(ImGui::Button("Up", ImVec2(bw, 0)))
        reorder(-1);
    ImGui::SameLine();
    if(ImGui::Button("Down", ImVec2(bw, 0)))
        reorder(1);
    ImGui::SameLine();
    if(ImGui::Button("To back", ImVec2(bw, 0)))
        reorder(-2);
    ImGui::SameLine();
    if(ImGui::Button("To front", ImVec2(bw, 0)))
        reorder(2);
}

// The subscreen shown (every active, passive, overlay and map one) and its pages.
void top_bar()
{
    Editor &e = g_ed;
    Sub *s = cur_sub();
    const float sc = zcui::scale();
    char cur[160];
    snprintf(cur, sizeof(cur), "%s: %s", s ? list_name(s->list) : "?", s ? s->name.c_str() : "");
    ImGui::SetNextItemWidth((std::min)(320 * sc, ImGui::GetContentRegionAvail().x * 0.45f));

    if(ImGui::BeginCombo("##sub", cur, ImGuiComboFlags_HeightLarge))
    {
        for(size_t i = 0; i < e.m.size(); ++i)
        {
            char row[160];
            snprintf(row, sizeof(row), "%s: %s##s%d", list_name(e.m[i].list), e.m[i].name.c_str(), (int)i);

            if(ImGui::Selectable(row, (int)i == e.sub))
            {
                show_page((int)i, 0);
                e.fit = true;   // another kind of subscreen can be another height
            }

            ui_mark("sub." + std::to_string((int)i));
        }

        ImGui::EndCombo();
    }
    else
        ui_mark("subs");

    if(!s)
        return;

    ImGui::SameLine();

    // passive and overlay subscreens have one page (upstream hides its page controls for them);
    // one loaded with more can still show and drop the others
    if(!s->paged() && s->pages.size() <= 1)
    {
        ImGui::TextDisabled("One page (only active and map subscreens have pages)");
        return;
    }

    if(ImGui::ArrowButton("##pgprev", ImGuiDir_Left) && e.page > 0)
        show_page(e.sub, e.page - 1);

    ImGui::SameLine();
    ImGui::Text("Page %d of %d", e.page + 1, (int)s->pages.size());
    ImGui::SameLine();

    if(ImGui::ArrowButton("##pgnext", ImGuiDir_Right) && e.page + 1 < (int)s->pages.size())
        show_page(e.sub, e.page + 1);

    if(s->paged())
    {
        ImGui::SameLine();

        if(ImGui::Button("Add page"))
            add_page();
    }

    ImGui::SameLine();

    if(ImGui::Button("Delete page"))
        delete_page_asked();
}

void menu_bar()
{
    Editor &e = g_ed;
    Page *p = cur_page();
    Sub *s = cur_sub();
    const bool any = p && e.sel >= 0;
    const bool several = any && !e.more.empty();

    if(!ImGui::BeginMenuBar())
        return;

    if(ImGui::BeginMenu("Edit"))
    {
        if(m_item("Undo", "Ctrl+Z", false, !e.undo.empty()))
            do_undo();
        ImGui::Separator();

        if(ImGui::BeginMenu("New widget", p != NULL))
        {
            new_widget_menu();
            ImGui::EndMenu();
        }

        if(m_item("Properties...", "Enter", false, any))
            e.want_props = e.sel;
        if(m_item("Copy", "Ctrl+C", false, any))
            copy_picked();
        if(m_item("Paste", "Ctrl+V", false, p && !e.clip.empty() && e.clip_v == e.v))
            paste();
        if(m_item("Paste properties", NULL, false, any && !e.clip.empty() && e.clip_v == e.v))
            paste_properties();
        if(m_item("Duplicate", "Ctrl+D", false, any))
            duplicate_picked();
        if(m_item("Delete", "Del", false, any))
            delete_picked();
        ImGui::Separator();
        if(m_item("Select all", "Ctrl+A", false, p && !p->widgets.empty()))
            select_all();
        if(m_item("Select none", NULL, false, any))
            clear_picks();
        ImGui::Separator();

        if(m_begin("Wizards", p != NULL))
        {
            wizard_menu(false);
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    ui_mark("Edit");
    subscreen_menu();

    if(ImGui::BeginMenu("Arrange"))
    {
        if(m_item("Bring to front", NULL, false, any))
            reorder(2);
        if(m_item("Bring forward", NULL, false, any))
            reorder(1);
        if(m_item("Send backward", NULL, false, any))
            reorder(-1);
        if(m_item("Send to back", NULL, false, any))
            reorder(-2);
        ImGui::Separator();
        if(m_item("Reverse", NULL, false, several))
            reverse_picked();
        ImGui::EndMenu();
    }

    if(ImGui::BeginMenu("Align"))
    {
        ImGui::TextDisabled("To the current widget's:");
        if(m_item("Left edges", NULL, false, several))
            align_picked(AL_LEFT);
        if(m_item("Horizontal centres", NULL, false, several))
            align_picked(AL_CENTER);
        if(m_item("Right edges", NULL, false, several))
            align_picked(AL_RIGHT);
        ImGui::Separator();
        if(m_item("Top edges", NULL, false, several))
            align_picked(AL_TOP);
        if(m_item("Vertical centres", NULL, false, several))
            align_picked(AL_MIDDLE);
        if(m_item("Bottom edges", NULL, false, several))
            align_picked(AL_BOTTOM);
        ImGui::Separator();

        if(ImGui::BeginMenu("To the grid", any))
        {
            static const char *const what[6] = { "Left edges", "Horizontal centres", "Right edges", "Top edges",
                                                 "Vertical centres", "Bottom edges" };

            for(int q = 0; q < 6; ++q)
                if(m_item(what[q]))
                    grid_snap_picked(q);

            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    if(ImGui::BeginMenu("Distribute"))
    {
        const bool three = any && e.more.size() >= 2;
        ImGui::TextDisabled("Three or more picked widgets, evenly by their:");
        if(m_item("Left edges##d", NULL, false, three))
            distribute_picked(AL_LEFT);
        if(m_item("Horizontal centres##d", NULL, false, three))
            distribute_picked(AL_CENTER);
        if(m_item("Right edges##d", NULL, false, three))
            distribute_picked(AL_RIGHT);
        ImGui::Separator();
        if(m_item("Top edges##d", NULL, false, three))
            distribute_picked(AL_TOP);
        if(m_item("Vertical centres##d", NULL, false, three))
            distribute_picked(AL_MIDDLE);
        if(m_item("Bottom edges##d", NULL, false, three))
            distribute_picked(AL_BOTTOM);
        ImGui::EndMenu();
    }

    if(ImGui::BeginMenu("Page"))
    {
        const int n = s ? (int)s->pages.size() : 0;
        const bool paged = s && s->paged();     // only active and map subscreens have pages

        if(!paged)
            ImGui::TextDisabled("Only active and map subscreens have pages.");
        if(m_item("Previous", "PgUp", false, e.page > 0))
            show_page(e.sub, e.page - 1);
        if(m_item("Next", "PgDn", false, e.page + 1 < n))
            show_page(e.sub, e.page + 1);
        ImGui::Separator();
        if(m_item("Add page", NULL, false, paged && n < MAX_PAGES))
            add_page();
        if(m_item("Delete page", NULL, false, s && (paged || n > 1)))
            delete_page_asked();
        if(m_item("Move page left", NULL, false, paged && e.page > 0))
            move_page(-1);
        if(m_item("Move page right", NULL, false, paged && e.page + 1 < n))
            move_page(1);
        ImGui::Separator();
        if(m_item("Copy page", NULL, false, paged && p != NULL))
            copy_page();
        if(m_item("Paste as new page", NULL, false, paged && e.has_page_clip && e.page_clip_v == e.v && n < MAX_PAGES))
            paste_page();
        ImGui::Separator();
        if(m_item("Page settings...", NULL, false, p != NULL))
            open_page_settings();
        ImGui::EndMenu();
    }

    ui_mark("Page");

    if(ImGui::BeginMenu("View"))
    {
        m_toggle("Show grid", &e.grid);
        m_toggle("Snap to grid", &e.snap);
        m_toggle("Show unowned items", &e.all_items);
        view_menu_more();
        ImGui::Separator();

        for(int z = 1; z <= 4; ++z)
        {
            char t[16];
            snprintf(t, sizeof(t), "Zoom %dx", z);

            if(m_item(t, NULL, e.zoom == z))
                e.zoom = z;
        }

        if(m_item("Zoom to fit"))
            e.fit = true;

        ImGui::EndMenu();
    }

    ui_mark("View");
    ImGui::EndMenuBar();
}

void view_bar()
{
    Editor &e = g_ed;
    const float sc = zcui::scale();
    ImGui::Checkbox("Grid", &e.grid);
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &e.snap);
    ImGui::SameLine();
    ImGui::PushItemWidth(110 * sc);
    ImGui::InputInt("Grid X", &e.grid_x);
    ImGui::SameLine();
    ImGui::InputInt("Grid Y", &e.grid_y);
    ImGui::PopItemWidth();
    e.grid_x = (std::max)(1, (std::min)(128, e.grid_x));
    e.grid_y = (std::max)(1, (std::min)(128, e.grid_y));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70 * sc);
    ImGui::SliderInt("Zoom", &e.zoom, 1, 4);
}

void keys()
{
    Editor &e = g_ed;

    if(ImGui::IsAnyItemActive() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) ||
       !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        return;

    const ImGuiIO &io = ImGui::GetIO();
    const int step_x = io.KeyShift ? e.grid_x : 1, step_y = io.KeyShift ? e.grid_y : 1;

    if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        move_picked(-step_x, 0);
    if(ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        move_picked(step_x, 0);
    if(ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        move_picked(0, -step_y);
    if(ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        move_picked(0, step_y);
    if(ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        delete_picked();
    if((ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) && e.sel >= 0)
        e.want_props = e.sel;
    if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
        duplicate_picked();
    if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
        copy_picked();
    if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
        paste();
    if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
        select_all();
    if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        do_undo();
    if(ImGui::IsKeyPressed(ImGuiKey_PageUp, false) && e.page > 0)
        show_page(e.sub, e.page - 1);
    if(ImGui::IsKeyPressed(ImGuiKey_PageDown, false) && cur_sub() && e.page + 1 < (int)cur_sub()->pages.size())
        show_page(e.sub, e.page + 1);
    if(ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        e.ask_close = true;
}

// Delete page on a page with widgets: as upstream's "Delete Current Page" question.
void ask_delete_page()
{
    Editor &e = g_ed;

    if(e.ask_delete_page)
    {
        e.ask_delete_page = false;
        ImGui::OpenPopup("Delete Current Page##zqsubdelpage");
    }

    if(ImGui::BeginPopupModal("Delete Current Page##zqsubdelpage", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const float sc = zcui::scale();
        const Page *p = cur_page();
        const int n = p ? (int)p->widgets.size() : 0;
        ImGui::Text("Delete page %d, including %s%d widget%s?", e.page + 1, n == 1 ? "its " : "all its ", n, n == 1 ? "" : "s");

        if(ImGui::Button("Delete", ImVec2(120 * sc, 0)))
        {
            delete_page();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if(ImGui::Button("Keep it", ImVec2(120 * sc, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

// Cancel (or Escape, or the window's close box): as upstream's "Exit without saving?", asked
// only when something was changed.
void ask_close()
{
    Editor &e = g_ed;

    if(e.ask_close)
    {
        e.ask_close = false;

        if(edited())
            ImGui::OpenPopup("Drop the changes?##zqsubclose");
        else
            e.close = true;
    }

    if(ImGui::BeginPopupModal("Drop the changes?##zqsubclose", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const float sc = zcui::scale();
        ImGui::TextUnformatted("Close the subscreen editor without keeping your changes?");

        if(ImGui::Button("Keep editing", ImVec2(120 * sc, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::SameLine();

        if(ImGui::Button("Drop them", ImVec2(120 * sc, 0)))
        {
            e.close = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void editor_layer(void *)
{
    Editor &e = g_ed;

    if(!e.open || e.busy)
        return;

    const float sc = zcui::scale();
    ImGuiViewport *vp = ImGui::GetMainViewport();
    // as large as it wants, or as the window is
    const ImVec2 size((std::min)(1000 * sc, vp->WorkSize.x - 8), (std::min)(690 * sc, vp->WorkSize.y - 8));
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);

    // ZQuest's window resized since: the editor laid out again for it, as it opened, and the page
    // zoomed to fit
    if(e.placed && (vp->WorkSize.x != e.vp_size.x || vp->WorkSize.y != e.vp_size.y))
    {
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        e.fit = true;
    }

    e.vp_size = vp->WorkSize;

    if(e.focus)
    {
        ImGui::SetNextWindowFocus();
        e.focus = false;
    }

    char title[128];
    snprintf(title, sizeof(title), "Subscreen Editor (%s, subscreen format %d)###zqsublayout", e.is30 ? "3.0 Nightly" : "2.55", e.v);
    bool open = true;

    if(ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar))
    {
        e.placed = true;
        menu_bar();
        top_bar();
        const ImGuiStyle &st = ImGui::GetStyle();
        const float bottom = ImGui::GetFrameHeightWithSpacing() * (e.error.empty() ? 2 : 3) + st.ItemSpacing.y;

        if(ImGui::BeginChild("body", ImVec2(0, -bottom)))
        {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float side = (std::min)(340 * sc, avail.x * 0.45f);
            const float left = (std::max)(64.0f, avail.x - side - st.ItemSpacing.x);

            if(e.fit)
            {
                // the largest zoom that shows the whole page
                const float zw = (left - st.WindowPadding.x * 2) / (TEX_W * sc), zh = (avail.y - st.WindowPadding.y * 2) / (e.canvas_h * sc);
                e.zoom = (std::max)(1, (std::min)(4, (int)floor((std::min)(zw, zh))));
                e.fit = false;
            }

            if(ImGui::BeginChild("page", ImVec2(left, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
                canvas();

            ImGui::EndChild();
            ImGui::SameLine();

            if(ImGui::BeginChild("side", ImVec2(0, 0)))
            {
                inspector();
                ImGui::Separator();
                const float list_h = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2 - st.ItemSpacing.y;
                widget_list((std::max)(80.0f * sc, list_h));
            }

            ImGui::EndChild();
        }

        ImGui::EndChild();
        view_bar();

        if(!e.error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", e.error.c_str());

        if(ImGui::Button("OK", ImVec2(90 * sc, 0)))
            e.want_ok = true;

        ui_mark("OK##editor");
        ImGui::SameLine();

        if(ImGui::Button("Cancel", ImVec2(90 * sc, 0)))
            e.ask_close = true;

        ui_mark("Cancel##editor");

        ImGui::SameLine();
        ImGui::BeginDisabled(e.undo.empty());

        if(ImGui::Button("Undo", ImVec2(90 * sc, 0)))
            do_undo();

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Drag widgets to move them, the yellow corner to size one; Shift picks more; right click for more.");
        keys();
        ask_delete_page();
        ask_close();
        more_popups();
    }

    ImGui::End();
    ui_map_flush();

    if(!open)
        e.ask_close = true;
}

// ---- drawing the page (game thread) --------------------------------------------------------------

// The preview's items: the init data's, or every item; a bomb, super bomb and arrow at least
// (as 2.53's subscreen editor gives); the starting life.
void preview_items(bool all)
{
    if(all)
    {
        for(int i = 0; i < MAXITEMS; ++i)
            if(itemsbuf[i].flags & ITEM_GAMEDATA)
                game->set_item_no_flush(i, true);
    }
    else
        resetItems(game, &zinit, true);

    // the life a new game starts with (zelda.cpp), so life meters and gauges show as they do in
    // the player when the game starts (2.53's and upstream's editors leave it at 0: all empty)
    game->set_life((std::min)((int)zinit.start_heart * HP_PER_HEART, (int)game->get_maxlife()));

    if(game->get_bombs() == 0)
        game->set_bombs(1);

    if(game->get_sbombs() == 0)
        game->set_sbombs(1);

    if(game->get_arrows() == 0)
        game->set_arrows(1);

    g_view_all_items = all;
    load_Sitems(&misc);
}

// The preview's counters for View > "Max out counters" (every counter at its maximum) and "Max
// out max counters" (the maximums at 65535 too, as upstream's get_ssc_ctrmax gives; life and
// magic keep theirs, so the meters stay the quest's size).
bool g_view_full_ctr = false, g_view_max_ctr = false;

void preview_state(bool all, bool full_ctr, bool max_ctr)
{
    preview_items(false);       // the init data's items and counters again

    if(all)
        preview_items(true);

    for(int c = 0; c < 32; ++c)
    {
        if(max_ctr && c != 0 && c != 4)
            game->set_maxcounter(65535, (byte)c);

        if(full_ctr)
            game->set_counter(game->get_maxcounter((byte)c), (byte)c);
    }

    g_view_full_ctr = full_ctr;
    g_view_max_ctr = max_ctr;
}

// The quest's state the page is drawn with, as 2.53's subscreen editor makes it (zq_subscr.cpp
// edit_subscreen): the init data's items and counters.
struct Preview
{
    gamedata *was;

    Preview() : was(game)
    {
        game = new gamedata;
        game->Clear();
        game->set_time(0);
        preview_state(false, false, false);
    }

    ~Preview()
    {
        Sitems.clear();
        delete game;
        game = was;
    }
};

void free_texts(subscreen_group &g)
{
    for(int i = 0; i < MAXSUBSCREENITEMS; ++i)
    {
        const int ty = g.objects[i].type;

        if((ty == ssoTEXT || ty == ssoTEXTBOX || ty == ssoCURRENTITEMTEXT || ty == ssoCURRENTITEMCLASSTEXT) && g.objects[i].dp1)
            delete [] (char *)g.objects[i].dp1;

        memset(&g.objects[i], 0, sizeof(subscreen_object));
    }
}

// Draws a page as the player does into an 8-bit bitmap of 256 by its height; boxes: each widget's
// box. pos2: which of the widgets' "shown when" bits to draw.
// Call with the UI lock held for the widget bytes (they are converted here, then drawn).
BITMAP *draw_page(const Page &p, int v, int height, int pos2, std::vector<Box> &boxes)
{
    static subscreen_group g;
    free_texts(g);
    boxes.assign(p.widgets.size(), Box());
    g_view_seltexts.clear();
    int count = 0;
    std::vector<int> obj_of(p.widgets.size(), -1);

    for(size_t i = 0; i < p.widgets.size(); ++i)
    {
        const Bytes &w = p.widgets[i];
        Box &b = boxes[i];
        b.x = get16s(w, WO_X);
        b.y = get16s(w, WO_Y);
        b.w = get16(w, WO_W);
        b.h = get16(w, WO_H);
        b.drawn = false;
        b.unit_w = b.unit_h = 1;
        subscreen_object t;
        std::string text, seltext;
        size_t ext = 0;
        int sel_pos = -1;
        const bool drawn = w.size() >= 2 && q255_widget_object(&w[1], w.size() - 1, v, w[0], t, text, &ext, &sel_pos, &seltext);

        // the selected text widget's override texts (q30_sub_selected_text)
        if(ext && sel_pos >= 0)
            g_view_seltexts.push_back(std::make_pair(sel_pos, seltext));

        if(!drawn || count >= MAXSUBSCREENITEMS)
            continue;

        if(t.type == ssoTEXT || t.type == ssoTEXTBOX || t.type == ssoCURRENTITEMTEXT || t.type == ssoCURRENTITEMCLASSTEXT)
        {
            const size_t len = strlen(text.c_str());
            char *c = new char[len + 1];
            memcpy(c, text.c_str(), len + 1);
            t.dp1 = c;
        }

        g.objects[count] = t;
        obj_of[i] = count++;
        b.drawn = true;
    }

    // the boxes 2.53's subscreen editor gives the objects (subscr.cpp sso_x...), at the widgets'
    // own (signed) places; how many pixels a step of width or height is
    g_view_on = true;

    for(size_t i = 0; i < p.widgets.size(); ++i)
    {
        if(obj_of[i] < 0)
            continue;

        subscreen_object &t = g.objects[obj_of[i]];
        Box &b = boxes[i];
        b.x += sso_x(&t) - (int)t.x;
        b.y += sso_y(&t) - (int)t.y;
        b.w = sso_w(&t);
        b.h = sso_h(&t);

        // text drawn centred or right of its place (textout_styled_aligned_ex's d2), as 2.53's
        // subscreen editor and upstream's getX place its box
        switch(t.type)
        {
        case ssoTEXT: case ssoBSTIME: case ssoTIME: case ssoSSTIME: case ssoCOUNTER:
            if(t.d2 == sstaCENTER)
                b.x -= b.w / 2;
            else if(t.d2 == sstaRIGHT)
                b.x -= b.w;

            break;
        }

        subscreen_object u = t;
        ++u.w;
        ++u.h;
        b.unit_w = (std::max)(0, sso_w(&u) - b.w);
        b.unit_h = (std::max)(0, sso_h(&u) - b.h);
    }

    BITMAP *bmp = create_bitmap_ex(8, TEX_W, height);

    if(bmp)
    {
        clear_to_color(bmp, 0);
        // the cursor where the page starts it, Bweapon's item for the B button there
        subscreen_group *const keep_css = css;
        const int keep_bpos = Bpos, keep_bwpn = Bwpn;
        css = &g;
        Bpos = (int)get32(p.cursor, 0);
        Bwpn = Bweapon(Bpos);
        // View: invisible items shown (2.53's own switch, read by subscr.cpp); the preview DMap
        const byte keep_ss = zinit.ss_flags;
        const word keep_dmap = zinit.start_dmap;
        zinit.ss_flags = (byte)(g_more.show_invis ? (keep_ss | ssflagSHOWINVIS) : (keep_ss & ~ssflagSHOWINVIS));

        if(g_more.preview_dmap >= 0 && g_more.preview_dmap < MAXDMAPS)
            zinit.start_dmap = (word)g_more.preview_dmap;

        update_subscr_items();
        show_custom_subscreen(bmp, &misc, &g, 0, 0, true, pos2);
        zinit.ss_flags = keep_ss;
        zinit.start_dmap = keep_dmap;
        css = keep_css;
        Bpos = keep_bpos;
        Bwpn = keep_bwpn;
    }

    g_view_on = false;
    return bmp;
}

void to_rgba(BITMAP *b, std::vector<unsigned int> &px)
{
    PALETTE pal;
    get_palette(pal);
    px.assign((size_t)TEX_W * TEX_H, 0xFF000000u);

    for(int y = 0; y < b->h && y < TEX_H; ++y)
        for(int x = 0; x < TEX_W; ++x)
        {
            const int i = b->line[y][x];
#ifdef ZC_ENGINE
            const int r = pal[i].r, gg = pal[i].g, bl = pal[i].b;
#else
            const int r = (pal[i].r << 2) | (pal[i].r >> 4), gg = (pal[i].g << 2) | (pal[i].g >> 4),
                      bl = (pal[i].b << 2) | (pal[i].b >> 4);
#endif
            px[(size_t)y * TEX_W + x] = 0xFF000000u | ((unsigned)bl << 16) | ((unsigned)gg << 8) | (unsigned)r;
        }
}

// The game thread: the page shown, drawn again when it changed and as its items animate, which
// they do as the player's do: one step a frame, 60 frames a second. reset: the editor just opened.
void redraw(bool reset)
{
    Editor &e = g_ed;
    std::vector<Box> boxes;
    BITMAP *b = NULL;
    int for_change = 0, height = 176, shown_sub = 0, shown_page = 0;
    static std::chrono::steady_clock::time_point next;
    static int drawn_change = -1;
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const std::chrono::microseconds frame(16667);
    int steps = 0;

    if(reset)
    {
        next = now;
        drawn_change = -1;
    }

    while(now >= next && steps < 4)
    {
        ++steps;
        next += frame;
    }

    if(now >= next)     // far behind (the property list was open): from now on
        next = now + frame;

    {
        zcui::Lock lock;
        Page *p = cur_page();

        if(!p)
            return;

        for_change = e.changes;
        height = e.canvas_h;
        bool items_changed = false;

        if(e.all_items != g_view_all_items || g_more.full_ctr != g_view_full_ctr || g_more.max_ctr != g_view_max_ctr)
        {
            preview_state(e.all_items, g_more.full_ctr, g_more.max_ctr);
            items_changed = true;
        }

        if(!steps && !items_changed && for_change == drawn_change)
            return;

        for(int i = 0; i < steps; ++i)
            Sitems.animate();

        drawn_change = for_change;
        shown_sub = e.sub;
        shown_page = e.page;
        b = draw_page(*p, e.v, height, sspUP | sspDOWN | sspSCROLLING, boxes);
    }

    if(!b)
        return;

    std::vector<unsigned int> px;
    to_rgba(b, px);

    // ZQ_SUBLAYOUT_DUMP=file.bmp: the page as the editor last drew it, and as it last drew each
    // page shown (file.bmp.s<sub>.p<page>.bmp); tests compare them with the player's pictures
    static const char *dump = getenv("ZQ_SUBLAYOUT_DUMP");

    if(dump)
    {
        static std::vector<unsigned int> last;

        if(px != last)
        {
            char name[1100];
            snprintf(name, sizeof(name), "%s.s%d.p%d.bmp", dump, shown_sub, shown_page);
            save_bmp(dump, b, RAMpal);
            save_bmp(name, b, RAMpal);
            last = px;
        }
    }

    destroy_bitmap(b);
    zcui::Lock lock;

    if(px != e.px)
    {
        e.px.swap(px);
        e.px_dirty = true;
        zcui::request_redraw();     // the window shows it (it may be idle otherwise)
    }

    e.boxes.swap(boxes);
    e.boxes_for = for_change;
}

// The game thread: a widget's property list (the subscreen data editor's fields for it).
void properties(int index)
{
    Editor &e = g_ed;
    Bytes w;
    int v = 0;
    char title[160];
    {
        zcui::Lock lock;
        Page *p = cur_page();

        if(!p || index < 0 || index >= (int)p->widgets.size())
            return;

        w = p->widgets[index];
        v = e.v;
        const std::string label = widget_label(w, v);
        snprintf(title, sizeof(title), "Widget %d of page %d: %s%s%s", index, e.page + 1, type_name(w[0]), label.empty() ? "" : ": ",
                 label.c_str());
        e.busy = true;
    }

    const byte type = w[0];
    const bool ok = zq_edit_subscreen_widget(w, v, title);
    zcui::Lock lock;
    e.busy = false;
    e.focus = true;
    Page *p = cur_page();

    if(!ok || !p || index >= (int)p->widgets.size())
        return;

    if(w.size() < 2 || w[0] != type || q255_widget_extent(&w[1], w.size() - 1, v, type) != w.size() - 1)
    {
        e.error = "Those properties don't make a widget the player reads; nothing changed.";
        return;
    }

    if(w != p->widgets[index])
    {
        remember();
        p->widgets[index] = w;
        changed();
    }
}

// The game thread: the section into the quest. False (with the reason shown) if it can't go in.
bool commit()
{
    Editor &e = g_ed;
    Bytes body;
    RefMap refs;
    {
        zcui::Lock lock;
        encode(e.m, e.v, body);
        ref_map(e.m, g_more.orig_count, refs);
    }

    if(body == e.original && refs.same)
        return true;

    // subscreens were deleted: the DMaps' subscreen numbers that move with them, worked out
    // before anything goes in
    std::string why;
    std::vector<std::pair<int, Bytes> > dmaps;

    if(!dmap_refs_plan(refs, dmaps, why))
    {
        zcui::Lock lock;
        e.error = "Can't keep this: " + why + ".";
        return false;
    }

    if(body != e.original && !zq255_subs_put_section(body, why))
    {
        zcui::Lock lock;
        e.error = "Can't keep this: " + why + ".";
        return false;
    }

    saved = false;
    std::string failed;

    for(size_t i = 0; i < dmaps.size(); ++i)
        if(!zq255_dmap_commit(dmaps[i].first, dmaps[i].second, why) && failed.empty())
            failed = "The subscreens are in, but DMap " + std::to_string(dmaps[i].first) + " kept its old subscreen numbers: " + why + ".";

    if(!failed.empty())
    {
        // the lists are in: what the editor holds now is what the DMaps count in
        zcui::Lock lock;
        e.original = body;
        refs_rebase();
        e.error = failed;
        return false;
    }

    return true;
}

// What outlasts an editor session (the type to add, the copied widgets and page) made to fit a
// section of version v, which may be another quest's: a type the version doesn't hold goes back
// to Text; copied widgets and a page's are laid out again for it (convert_widget), and those
// that can't be are dropped, with a note. Call with the UI lock held.
void fit_kept(int v)
{
    Editor &e = g_ed;

    if(e.add_type < 1 || e.add_type >= type_limit(v))
        e.add_type = W_TEXT;

    // the copy's place was in another session's subscreens: pasted where it was, not shifted
    e.clip_sub = e.clip_page = -1;
    int dropped = 0;

    if(!e.clip.empty() && e.clip_v != v)
    {
        std::vector<Bytes> kept;

        for(size_t i = 0; i < e.clip.size(); ++i)
        {
            Bytes w;

            if(convert_widget(e.clip[i], e.clip_v, v, w))
                kept.push_back(w);
            else
                ++dropped;
        }

        e.clip.swap(kept);
        e.clip_v = v;
    }

    if(e.has_page_clip && e.page_clip_v != v)
    {
        Page p;
        p.cursor = e.page_clip.cursor;

        for(size_t i = 0; i < e.page_clip.widgets.size(); ++i)
        {
            Bytes w;

            if(convert_widget(e.page_clip.widgets[i], e.page_clip_v, v, w))
                p.widgets.push_back(w);
            else
                ++dropped;
        }

        if(!convert_page_trans(e.page_clip.trans, e.page_clip_v, v, p.trans))
            p.trans.assign(v >= 16 ? 1 : 0, 0);

        e.page_clip = p;
        e.page_clip_v = v;
    }

    if(dropped)
        e.error = std::to_string(dropped) + " copied widget" + (dropped == 1 ? "" : "s") +
                  " can't go in this subscreen format (version " + std::to_string(v) + ") and " + (dropped == 1 ? "was" : "were") +
                  " left out of what Paste puts in.";
}

bool start(std::string &why)
{
    Bytes body;
    int v = 0;

    if(!zq255_subs_section(body, &v, why))
        return false;

    Model m;

    if(!parse(body, v, m) || m.empty())
    {
        why = "the subscreens don't read as their format says";
        return false;
    }

    zcui::Lock lock;
    Editor &e = g_ed;
    e.v = v;
    e.is30 = q255_header_version[0] >= 3;
    e.m.swap(m);
    e.original = body;
    e.undo.clear();
    e.error.clear();
    fit_kept(v);
    e.want_props = -1;
    e.want_ok = false;
    e.close = e.busy = false;
    e.focus = true;
    e.grid_x = zinit.ss_grid_x > 0 ? zinit.ss_grid_x : 8;
    e.grid_y = zinit.ss_grid_y > 0 ? zinit.ss_grid_y : 8;
    e.grid_xofs = zinit.ss_grid_xofs;
    e.grid_yofs = zinit.ss_grid_yofs;
    e.grid = (zinit.ss_flags & ssflagSHOWGRID) != 0;
    e.boxes.clear();
    e.boxes_for = -1;
    e.fit = true;
    e.placed = false;
    e.ask_close = false;
    more_start();
    show_page(0, 0);
    return true;
}

} // namespace

// Quest > Misc Data > Subscreens for a 2.55 / 3.0 quest whose subscreens are in upstream's own
// format. False when it can't be shown: *why says why (the quest isn't one, the UI isn't up).
bool zq_edit_subscreen_layout(std::string &why)
{
    if(!zcui::available())
    {
        why = "the UI isn't available";
        return false;
    }

    {
        zcui::Lock lock;

        if(g_ed.open)
        {
            why = "it is open already";
            return false;
        }
    }

    if(!start(why))
        return false;

    Preview preview;
    {
        zcui::Lock lock;
        g_ed.open = true;
    }

    view_rules();
    const int layer = zcui::add_layer(editor_layer, NULL, 590);
    zcui::begin_session();
    bool first = true;

    for(;;)
    {
        zcui::wait_frame(100);
        bool done, ok;
        int props;
        {
            zcui::Lock lock;
            done = g_ed.close;
            ok = g_ed.want_ok;
            props = g_ed.want_props;
            g_ed.want_ok = false;
            g_ed.want_props = -1;
        }

        if(ok && commit())
            break;

        if(done || !zcui::available())
            break;

        if(props >= 0)
        {
            properties(props);
            continue;
        }

        if(more_requests())
            continue;

        redraw(first);
        first = false;
    }

    zcui::end_session();
    zcui::remove_layer(layer);
    zcui::Lock lock;
    g_ed.open = false;
    g_ed.m.clear();
    g_ed.undo.clear();
    return true;
}

// -qst-subscreen-render in.qst sub page out.bmp (zquest.cpp): page `page` (from 0) of subscreen
// `sub` (counted through the active, passive, overlay and map lists, as the editor's list has
// them) drawn as the editor draws it, as an 8-bit picture with the palette of the quest's
// starting DMap's level: for comparing with the player's ZC_Q30_SUBDUMP picture, which draws the
// widgets shown with the subscreen up (the editor's page shows them all). 0 when written.
int zq_subscreen_render(int sub, int page, const char *out)
{
    std::string why;
    Bytes body;
    int v = 0;
    Model m;

    if(!zq255_subs_section(body, &v, why) || !parse(body, v, m) || sub < 0 || sub >= (int)m.size() || page < 0 ||
       page >= (int)m[sub].pages.size())
        return 2;

    loadlvlpal(DMaps[zinit.start_dmap].color);
    view_rules();
    Preview preview;
    std::vector<Box> boxes;
    // as the player's ZC_Q30_SUBDUMP picture: drawn, the items one animation step on, drawn again
    BITMAP *b = draw_page(m[sub].pages[page], v, page_height(m[sub].list), sspUP, boxes);

    if(b)
    {
        destroy_bitmap(b);
        Sitems.animate();
        b = draw_page(m[sub].pages[page], v, page_height(m[sub].list), sspUP, boxes);
    }

    if(!b)
        return 3;

    PALETTE pal;
    get_palette(pal);
    const int ret = save_bmp(out, b, pal) ? 4 : 0;
    destroy_bitmap(b);

    // the widgets' boxes beside it, as text
    std::string txt = std::string(out) + ".txt";

    if(FILE *f = fopen(txt.c_str(), "w"))
    {
        fprintf(f, "subscreen %d (%s: %s) page %d of %d, format %d\n", sub, list_name(m[sub].list), m[sub].name.c_str(), page + 1,
                (int)m[sub].pages.size(), v);

        for(size_t i = 0; i < boxes.size(); ++i)
            fprintf(f, "%d: %s at %d,%d box %d,%d %dx%d%s\n", (int)i, type_name(m[sub].pages[page].widgets[i][0]),
                    get16s(m[sub].pages[page].widgets[i], WO_X), get16s(m[sub].pages[page].widgets[i], WO_Y), boxes[i].x, boxes[i].y,
                    boxes[i].w, boxes[i].h, boxes[i].drawn ? "" : " (not drawn)");

        fclose(f);
    }

    return ret;
}

// For subscr.cpp's counter while this editor draws a page: whether it shows as infinite, by View >
// "Don't show 'infinite's" and "Show everything 'infinite'" (upstream's zq_view_noinf and
// zq_view_allinf). can: its counter or infinite item could make it so (upstream's can_inf).
bool zq_subscr_view_infinite(bool infinite, bool can)
{
    if(g_more.all_inf && can)
        return true;

    return g_more.no_inf ? false : infinite;
}

// -qst-subscreen-list in.qst report.txt (zquest.cpp): the open quest's subscreens by list (names,
// settings, pages, widget types and places) and every DMap's subscreen numbers, as text, for
// checking what the layout editor saved. 0 when written.
int zq_subscreen_list(const char *report)
{
    std::string why;
    Bytes body;
    int v = 0;
    Model m;
    FILE *f = fopen(report, "w");

    if(!f)
        return 3;

    if(!zq255_subs_section(body, &v, why) || !parse(body, v, m))
    {
        fprintf(f, "no new-format subscreens: %s\n", why.c_str());
        fclose(f);
        return 2;
    }

    list_report(f, m, v);
    fclose(f);
    return 0;
}

// Quest > Misc Data > Subscreens (zq_subscr.cpp onEditSubscreens): 1 when this editor was shown,
// 0 when the open quest isn't one for it (2.53's subscreen editor then: a 2.53 quest, or a 2.55
// one whose subscreens are still in 2.53's own format), -1 when it couldn't be (why says why).
int zq_subscreen_layout_menu(std::string &why)
{
    bool zq255_subs_record(std::vector<byte> &rec, int *version);      // zqw_init.cpp
    std::vector<byte> rec;

    if(!zq255_subs_record(rec, NULL))
        return 0;

    return zq_edit_subscreen_layout(why) ? 1 : -1;
}

// For zq255_subs_put_section (zqw_init.cpp): false, with the reason, when a section of version v
// doesn't take apart into widgets, or holds a widget type its version doesn't (a 3.0 type in a
// section of version 12 or older, which 2.55.17 refuses to load: our own reader takes it).
bool zq_subscr_section_types_ok(const std::vector<byte> &body, int v, std::string &why)
{
    Model m;

    if(!parse(body, v, m))
    {
        why = "the subscreens don't take apart into widgets";
        return false;
    }

    for(size_t i = 0; i < m.size(); ++i)
        for(size_t pg = 0; pg < m[i].pages.size(); ++pg)
            for(size_t q = 0; q < m[i].pages[pg].widgets.size(); ++q)
            {
                const int ty = m[i].pages[pg].widgets[q][0];

                if(ty < 1 || ty >= type_limit(v))
                {
                    why = std::string("subscreen \"") + m[i].name + "\" page " + std::to_string((int)pg + 1) + " has a " + type_name(ty) +
                          " widget, which subscreen format " + std::to_string(v) + " can't hold (2.55.17 doesn't know that type)";
                    return false;
                }
            }

    return true;
}

// -qst-subscreen-check in.qst report.txt (zquest.cpp): the layout editor's own checks on the open
// quest's subscreens, for running over many quests: the section taken apart and put together
// again must be the same bytes; every page is drawn; a new widget of each type the version holds,
// put on the first page, and a page added, copied, moved and deleted, must each make a section
// the player's reader reads whole. 0 when all hold (1 when one doesn't, 2 when the quest has no
// subscreens in upstream's own format).
int zq_subscreen_check(const char *report)
{
    FILE *f = fopen(report, "w");

    if(!f)
        return 3;

    std::string why;
    Bytes body;
    int v = 0;
    Model m;

    if(!zq255_subs_section(body, &v, why))
    {
        fprintf(f, "no new-format subscreens: %s\n", why.c_str());
        fclose(f);
        return 2;
    }

    int bad = 0;
    const bool parsed = parse(body, v, m);
    Bytes again;

    if(parsed)
        encode(m, v, again);

    fprintf(f, "format %d, %d bytes, %d subscreens: %s\n", v, (int)body.size(), (int)m.size(),
            !parsed ? "DOESN'T TAKE APART" : again == body ? "taken apart and put together the same" : "PUT TOGETHER DIFFERENT");
    bad += !parsed || again != body;

    // the player's reader on a section; true when it reads it whole
    struct Reads
    {
        static bool ok(const Bytes &b, int ver)
        {
            Q255Section r;
            r.data = b.empty() ? NULL : &b[0];
            r.size = b.size();
            r.version = (word)ver;
            r.keep = false;
            r.header = &header;
            return q255_read_subscreens(r) == 0 && !r.failed && r.pos == r.size;
        }
    };

    if(parsed)
    {
        // every page drawn
        Preview preview;
        view_rules();
        int pages = 0, widgets = 0, drawn = 0;

        for(size_t i = 0; i < m.size(); ++i)
            for(size_t pg = 0; pg < m[i].pages.size(); ++pg)
            {
                std::vector<Box> boxes;
                BITMAP *b = draw_page(m[i].pages[pg], v, page_height(m[i].list), sspUP | sspDOWN | sspSCROLLING, boxes);

                if(b)
                    destroy_bitmap(b);
                else
                    ++bad;

                ++pages;
                widgets += (int)boxes.size();

                for(size_t q = 0; q < boxes.size(); ++q)
                    drawn += boxes[q].drawn;
            }

        fprintf(f, "%d pages drawn, %d widgets, %d of them drawn by the player\n", pages, widgets, drawn);

        // a new widget of each type
        const int last = type_limit(v);
        std::string failed;

        for(int ty = 1; ty < last && !m.empty() && !m[0].pages.empty(); ++ty)
        {
            Model t = m;
            Bytes w, out;

            if(!new_widget(v, ty, 8, 8, w))
            {
                failed += std::string(" ") + type_name(ty) + " (doesn't lay out)";
                continue;
            }

            t[0].pages[0].widgets.push_back(w);
            encode(t, v, out);

            if(!Reads::ok(out, v))
                failed += std::string(" ") + type_name(ty) + " (doesn't read)";
        }

        fprintf(f, "new widgets of types 1-%d: %s\n", last - 1, failed.empty() ? "all read back" : ("FAILED:" + failed).c_str());
        bad += !failed.empty();

        // page edits on the first subscreen
        if(!m.empty())
        {
            zcui::Lock lock;
            Editor &e = g_ed;
            e.m = m;
            e.v = v;
            e.undo.clear();
            show_page(0, 0);
            add_page();
            add_widget(W_TEXT);
            copy_page();
            paste_page();
            move_page(-1);
            show_page(0, 0);
            delete_page();
            Bytes out;
            encode(e.m, v, out);
            const bool ok = Reads::ok(out, v);
            fprintf(f, "page edits (add, copy, move, delete): %d pages now, %s\n", (int)e.m[0].pages.size(),
                    ok ? "reads back" : "DOESN'T READ");
            bad += !ok;
            do_undo();
            do_undo();
            do_undo();
            do_undo();
            do_undo();
            encode(e.m, v, out);
            fprintf(f, "undone: %s\n", out == body ? "the same as loaded" : "DIFFERENT");
            bad += out != body;

            // passive and overlay subscreens keep their one page; the only page of a subscreen
            // deleted keeps its change transitions
            int fixed_ok = 0, fixed_bad = 0;

            for(size_t i = 0; i < m.size(); ++i)
            {
                if(m[i].paged())
                    continue;

                e.m = m;
                e.undo.clear();
                show_page((int)i, 0);
                const size_t n = e.m[i].pages.size();
                add_page();
                copy_page();
                paste_page();
                move_page(1);

                if(n <= 1)
                    delete_page();

                encode(e.m, v, out);
                (e.m[i].pages.size() == n && out == body) ? ++fixed_ok : ++fixed_bad;
            }

            fprintf(f, "passive/overlay subscreens keep their pages: %d ok, %d CHANGED\n", fixed_ok, fixed_bad);
            bad += fixed_bad;

            if(v >= 16)
            {
                e.m = m;
                e.undo.clear();
                show_page(0, 0);
                Sub &s0 = e.m[0];
                s0.pages.resize(1);
                s0.pages[0].trans.assign(1, 1);     // one transition: buttons, mode, target, flags, type,
                s0.pages[0].trans.insert(s0.pages[0].trans.end(), 4 + 1, 0);
                s0.pages[0].trans.insert(s0.pages[0].trans.end(), v < 17 ? 1 : 2, 0);     // sound
                s0.pages[0].trans.insert(s0.pages[0].trans.end(), 2 + 1, 0);              // flags, no arguments
                const Bytes trans = s0.pages[0].trans;
                delete_page();
                encode(e.m, v, out);
                const bool kept = e.m[0].pages.size() == 1 && e.m[0].pages[0].widgets.empty() && e.m[0].pages[0].trans == trans;
                fprintf(f, "only page deleted: %s\n", kept && Reads::ok(out, v) ? "emptied, transitions kept" : "WRONG");
                bad += !(kept && Reads::ok(out, v));
            }

            e.m.clear();
            e.undo.clear();
        }

        // the section's version takes only its own widget types
        {
            Model t = m;
            Bytes w, out;
            std::string why2;
            const int other = v <= 12 ? W_ITMCOOLDOWNTEXT : W_TEXT;
            bool laid = false;

            if(!t.empty() && !t[0].pages.empty() && new_widget(21, other, 8, 8, w))
            {
                // the widget laid out in this section's own layout (as a mistake would make it:
                // the schema and our reader take every type in every version)
                Tape fields, lay;
                TapeParser rd(w.data(), w.size(), 0, &fields);
                TapeParser wr(&fields, &lay);
                laid = zq_tape_subscreen_widget(rd, 21, "", false) && zq_tape_subscreen_widget(wr, v, "", false);
                w.clear();
                lay.encode(w);
                laid = laid && w.size() > 1 && q255_widget_extent(&w[1], w.size() - 1, v, w[0]) == w.size() - 1;

                if(laid)
                    t[0].pages[0].widgets.push_back(w);
            }

            encode(t, v, out);
            const bool refused = !zq_subscr_section_types_ok(out, v, why2);
            const bool want_refused = v <= 12;
            fprintf(f, "a widget of type %s in format %d: %s%s\n", type_name(other), v, refused ? "refused" : "taken",
                    refused == want_refused ? "" : " (WRONG)");
            bad += laid && refused != want_refused;
            bad += !zq_subscr_section_types_ok(body, v, why2);
        }

        // copied widgets laid out for another version (a clipboard taken to another quest)
        {
            const int to = v <= 12 ? 21 : 12;
            int ok = 0, refused = 0, wrong = 0;

            for(size_t i = 0; i < m.size(); ++i)
                for(size_t pg = 0; pg < m[i].pages.size(); ++pg)
                    for(size_t q = 0; q < m[i].pages[pg].widgets.size(); ++q)
                    {
                        const Bytes &w = m[i].pages[pg].widgets[q];
                        Bytes c, back;

                        if(!convert_widget(w, v, to, c))
                        {
                            (w[0] >= type_limit(to)) ? ++refused : ++wrong;
                            continue;
                        }

                        // back again: the same widget when nothing was lost on the way to a newer version
                        if(to > v && (!convert_widget(c, to, v, back) || back != w))
                        {
                            // the first few, for finding why
                            if(++wrong <= 3)
                            {
                                size_t at = 0;

                                while(at < w.size() && at < back.size() && w[at] == back[at])
                                    ++at;

                                fprintf(f, "  WRONG: subscreen %d page %d widget %d (%s): %d bytes, back %d bytes, first difference at %d\n",
                                        (int)i, (int)pg, (int)q, type_name(w[0]), (int)w.size(), (int)back.size(), (int)at);
                            }
                        }
                        else
                            ++ok;
                    }

            fprintf(f, "widgets laid out for format %d: %d ok, %d refused (types it lacks), %d WRONG\n", to, ok, refused, wrong);
            bad += wrong;
        }
    }

    fprintf(f, "%s\n", bad ? "FAILED" : "OK");
    fclose(f);
    return bad ? 1 : 0;
}

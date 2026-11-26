// ZQuest's interface per ZC build (zq_ui_profile.h): the profiles, which one the editor shows, and
// switching to it (the menu bar, the main screen's layout, the colours and the windows' look).
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "zdefs.h"
#include "jwin.h"
#include "zcport/ui.h"
#include "zq_builds.h"
#include "zq_ui_profile.h"

extern PALETTE RAMpal;
extern int jwin_pal[jcMAX];
extern int UseSmall;
extern int mapscreen_x, mapscreen_y, mapscreensize, showedges;
extern int zq_combo_columns, current_combolist, current_comboalist;
extern int command_buttonwidth, command_buttonheight;
extern int zq_screen_w, zq_screen_h;
extern bool is_large;
extern size_and_pos minimap, map_page_bar[9], combolist[3], combolistscrollers[3], comboaliaslist[3],
    comboalias_preview[3], combo_preview, combolist_window, panel[9], favorites_window, favorites_list,
    commands_window, commands_list, layer_panel;
extern void (*zcport_menu_before_view)(MENU *menu);     // port/game/ui_dialogs_host.cpp
extern "C" { extern int (*zcport_key_filter)(DIALOG *dialog, int ascii, int scancode); }   // port/al/gui_engine.cpp

long zq_builds_made(int *gen);                          // zq_builds.cpp
int zq_files_pick_row(const char *title, const char *question, const std::vector<std::string> &rows, int dflt);
void zq_ui_redraw();                                    // zquest.cpp
int onEnemyFlags();
int onDrawingModePool();
int onDrawingModeAuto();
int onCatchall();

#include "zq_ui_profiles_hand.h"

namespace
{

// ---- the profiles -------------------------------------------------------------------------------

const zq_ui_profile profile_2531 = { "2.53.1", 0x250, NULL, 0, NULL, NULL, NULL, 0 };

std::vector<const zq_ui_profile *> &registered()
{
    static std::vector<const zq_ui_profile *> list;
    return list;
}

struct Extra
{
    int (*proc)();
    int needs;
};

std::map<std::string, Extra> &extras()
{
    static std::map<std::string, Extra> list;
    return list;
}

// a profile with its build's place in the catalog
struct Known
{
    const zq_ui_profile *p;
    int gen;
    long order;
};

std::vector<zq_build> g_catalog;        // every build, oldest first (gen, then order)
std::vector<Known> g_known;             // every profile, oldest first
bool g_ready = false;

bool older(int gen_a, long order_a, int gen_b, long order_b)
{
    return gen_a != gen_b ? gen_a < gen_b : order_a < order_b;
}

// the catalog's build of that name (or whose name starts with it, then '+' or the end); a beta the
// catalog names only while one of its quests is open gets the order the catalog gives it then
// (zq_builds_old.cpp's dynamic_order), so it sorts between its neighbours
const zq_build *catalog_build(const char *name)
{
    const size_t n = strlen(name);

    for(size_t i = 0; i < g_catalog.size(); ++i)
    {
        const std::string &b = g_catalog[i].name;

        if(b == name || (b.size() > n && !b.compare(0, n, name) && b[n] == '+'))
            return &g_catalog[i];
    }

    static std::list<zq_build> betas;

    for(std::list<zq_build>::iterator i = betas.begin(); i != betas.end(); ++i)
        if(i->name == name)
            return &*i;

    int beta = 0;
    zq_build b;
    b.name = name;
    b.tag = 0;
    b.save_as = NULL;
    b.make_new = NULL;

    if(sscanf(name, "2.11 Beta %d", &beta) == 1)
    {
        b.gen = 0x211;
        b.order = 20080101L + beta;
    }
    else if(sscanf(name, "1.92 Beta %d", &beta) == 1)
    {
        b.gen = 0x192;
        b.order = 20010501L + beta;
    }
    else
        return NULL;

    betas.push_back(b);
    return &betas.back();
}

void prepare()
{
    if(g_ready)
        return;

    g_ready = true;
    zq_builds_192(g_catalog);
    zq_builds_old(g_catalog);
    zq_builds_255(g_catalog);
    zq_builds_30(g_catalog);
    std::stable_sort(g_catalog.begin(), g_catalog.end(), [](const zq_build &a, const zq_build &b)
    {
        return older(a.gen, a.order, b.gen, b.order);
    });

    // the generations' files, then the hand-written ones for builds no file has; 2.53.1 is today's
    std::vector<const zq_ui_profile *> all;

    for(size_t i = 0; i < registered().size(); ++i)
    {
        const zq_ui_profile *p = registered()[i];

        if(!strcmp(p->build, "2.53.1"))
        {
            al_trace("zq_ui: 2.53.1's profile is today's interface; a registered one is left out\n");
            continue;
        }

        // the later registration of a build wins
        for(size_t k = 0; k < all.size(); ++k)
            if(!strcmp(all[k]->build, p->build))
                all.erase(all.begin() + k--);

        all.push_back(p);
    }

    const zq_ui_profile *hand[] = { &profile_2531, &zq_ui_hand::profile_2103, &zq_ui_hand::profile_30_239 };

    for(size_t h = 0; h < sizeof(hand) / sizeof(hand[0]); ++h)
    {
        bool have = false;

        for(size_t k = 0; k < all.size() && !have; ++k)
            have = !strcmp(all[k]->build, hand[h]->build);

        if(!have)
            all.push_back(hand[h]);
    }

    for(size_t i = 0; i < all.size(); ++i)
    {
        Known k;
        k.p = all[i];
        k.gen = all[i]->gen;

        if(all[i]->order > 0)
            k.order = all[i]->order;        // its own place, before any name lookup
        else
        {
            const zq_build *b = catalog_build(all[i]->build);
            // a build the catalog doesn't list: after its generation's listed ones
            k.order = b ? b->order : 0x7FFFFFFFL;

            if(b)
                k.gen = b->gen;
        }

        g_known.push_back(k);
    }

    std::stable_sort(g_known.begin(), g_known.end(), [](const Known &a, const Known &b)
    {
        return older(a.gen, a.order, b.gen, b.order);
    });

    extras()["@interface"] = Extra{ onInterfaceOptions, 0 };
    extras()["@enemy_flags"] = Extra{ onEnemyFlags, 0 };
    extras()["@pool"] = Extra{ onDrawingModePool, 0x255 };
    extras()["@auto"] = Extra{ onDrawingModeAuto, 0x255 };
}

const Known *default_profile()
{
    for(size_t i = 0; i < g_known.size(); ++i)
        if(g_known[i].p == &profile_2531)
            return &g_known[i];

    return NULL;
}

// The profile for a build: its own, else the newest of its generation before it, else the oldest
// of its generation, else 2.53.1's.
const Known *profile_for(int gen, long order)
{
    const Known *before = NULL, *first = NULL;

    for(size_t i = 0; i < g_known.size(); ++i)
    {
        const Known &k = g_known[i];

        if(k.gen != gen)
            continue;

        if(!first)
            first = &k;

        if(order == -1 || k.order <= order)
            before = &k;
    }

    if(before)
        return before;

    return first ? first : default_profile();
}

// ---- actions: today's menu entries by path --------------------------------------------------------

std::map<std::string, MENU *> g_paths;

std::string entry_text(const char *text)
{
    std::string out;

    for(const char *c = text; c && *c && *c != '\t'; ++c)
        if(*c != '&')
            out += *c;

    while(!out.empty() && out[out.size() - 1] == ' ')
        out.erase(out.size() - 1);

    return out;
}

void walk(MENU *m, const std::string &prefix)
{
    for(; m && m->text; ++m)
    {
        if(m->child == zq_ui_quick_menu)
            continue;

        std::string here = prefix + (m->proc == onCatchall ? std::string("Catch All") : entry_text(m->text));

        if(m->child)
            walk(m->child, here + "/");
        else if(m->proc)
            g_paths[here] = m;
    }
}

// the oldest generation whose quests hold what these entries edit (their editors refuse others)
int path_needs(const std::string &path)
{
    static const struct { const char *path; int needs; } list[] =
    {
        { "Quest/2.55/3.0 String Data...", 0x255 },
        { "Quest/2.55/3.0 ZInfo...", 0x255 },
        { "Quest/Misc Data/2.55/3.0 Misc Data...", 0x255 },
        { "Quest/Misc Data/2.55/3.0 Subscreen Data...", 0x255 },
        { "Quest/Graphics/Combo Pools...", 0x255 },
        { "Quest/Graphics/Autocombos...", 0x255 },
        { "Quest/Audio/3.0 Music...", 0x300 },
        { "Screen/2.55/3.0 Screen Data...", 0x255 },
        { "Edit/Maps/Scrolling Regions...", 0x300 },
    };

    for(size_t i = 0; i < sizeof(list) / sizeof(list[0]); ++i)
        if(path == list[i].path)
            return list[i].needs;

    return 0;
}

// ---- built menu bars ------------------------------------------------------------------------------

struct Item
{
    MENU *item;
    MENU *src;          // today's entry it runs (its state shown), or NULL
    int needs;
    bool none;          // no function here
};

struct Bar
{
    const zq_ui_profile *p;
    MENU *bar;
    std::vector<Item> items;
};

std::list<std::unique_ptr<MENU[]>> g_menus;     // never freed: the menu shown may point into any
std::list<std::string> g_texts;
std::list<Bar> g_bars;
Bar *g_bar = NULL;                              // the bar shown (NULL: today's)
const Known *g_shown = NULL;
int g_quest_gen = 0x250;

char *keep_text(const char *t)
{
    g_texts.push_back(t ? t : "");
    return (char *)g_texts.back().c_str();
}

// rows[i..] at depth, up to the first row less deep: their MENU array
MENU *build_level(const zq_ui_profile *p, size_t &i, int depth, Bar &bar, bool top)
{
    std::vector<MENU> level;
    std::vector<size_t> pending;     // indices into bar.items of this level's entries

    while(i < (size_t)p->menu_rows && p->menu[i].depth >= depth)
    {
        const zq_ui_menu_row &r = p->menu[i];

        if(r.depth > depth)
        {
            ++i;     // rows deeper than a submenu's own: skipped (a table without its parent)
            continue;
        }

        MENU m;
        memset(&m, 0, sizeof(m));
        m.text = keep_text(r.text);
        ++i;
        Item it = { NULL, NULL, r.needs, false };

        if(!r.action)
        {
            if(r.text && r.text[0] && i < (size_t)p->menu_rows && p->menu[i].depth > depth)
                m.child = build_level(p, i, depth + 1, bar, false);
        }
        else if(r.action[0] == '@')
        {
            std::map<std::string, Extra>::iterator e = extras().find(r.action);

            if(e != extras().end())
            {
                m.proc = e->second.proc;
                it.needs = (std::max)(it.needs, e->second.needs);
            }
            else
            {
                it.none = true;
                al_trace("zq_ui: %s: no action %s\n", p->build, r.action);
            }
        }
        else if(r.action[0])
        {
            std::map<std::string, MENU *>::iterator e = g_paths.find(r.action);

            if(e != g_paths.end())
            {
                m.proc = e->second->proc;
                it.src = e->second;
                it.needs = (std::max)(it.needs, path_needs(r.action));
            }
            else
            {
                it.none = true;
                al_trace("zq_ui: %s: no menu entry %s\n", p->build, r.action);
            }
        }
        else
            it.none = true;

        if(it.none)
            m.flags = D_DISABLED;

        level.push_back(m);
        pending.push_back(bar.items.size());
        bar.items.push_back(it);
    }

    // every bar has the Interface menu at its end
    if(top)
    {
        MENU m;
        memset(&m, 0, sizeof(m));
        m.text = keep_text("&Interface");
        m.child = zq_ui_quick_menu;
        level.push_back(m);
    }

    std::unique_ptr<MENU[]> arr(new MENU[level.size() + 1]);

    for(size_t k = 0; k < level.size(); ++k)
        arr[k] = level[k];

    memset(&arr[level.size()], 0, sizeof(MENU));

    for(size_t k = 0; k < pending.size(); ++k)
        bar.items[pending[k]].item = &arr[k];

    MENU *out = arr.get();
    g_menus.push_back(std::move(arr));
    return out;
}

Bar *bar_of(const zq_ui_profile *p)
{
    for(std::list<Bar>::iterator b = g_bars.begin(); b != g_bars.end(); ++b)
        if(b->p == p)
            return &*b;

    if(g_paths.empty())
        walk(zq_ui_menu_253(), "");

    g_bars.push_back(Bar());
    Bar &bar = g_bars.back();
    bar.p = p;
    size_t i = 0;
    bar.bar = build_level(p, i, 0, bar, true);
    return &bar;
}

// ---- the Interface setting --------------------------------------------------------------------------

std::string setting()
{
    const char *s = get_config_string("zquest", "ui_profile", "match");
    return s && s[0] ? s : "match";
}

int onMatchBuild();
int onNewestBuild();

char g_showing[96] = "Showing: 2.53.1";

void sync_quick_menu()
{
    const std::string s = setting();
    zq_ui_quick_menu[0].flags = s == "match" ? D_SELECTED : 0;
    zq_ui_quick_menu[1].flags = s == "newest" ? D_SELECTED : 0;
    zq_ui_quick_menu[2].flags = (s != "match" && s != "newest") ? D_SELECTED : 0;
    snprintf(g_showing, sizeof(g_showing), "Showing: %s", g_shown ? g_shown->p->build : "2.53.1");
}

// the menu bar shown: its entries take today's entries' state (checked, disabled), and what the
// open quest's build can't hold is disabled
void before_view(MENU *menu)
{
    sync_quick_menu();

    if(!g_bar || menu != g_bar->bar)
        return;

    for(size_t i = 0; i < g_bar->items.size(); ++i)
    {
        Item &it = g_bar->items[i];

        if(!it.item)
            continue;

        int f = it.src ? it.src->flags : 0;

        if(it.none || (it.needs && g_quest_gen < it.needs))
            f |= D_DISABLED;

        it.item->flags = f;
    }
}

// ---- shortcut keys: what a profile's menus show beside an entry is what the key does ----------------

struct KeyObject
{
    DIALOG *d;
    void *dp;       // today's procedure
};

std::vector<KeyObject> g_keys;

// the scancode a shortcut text names ("F5", "Del"), or 0
int shortcut_scancode(const char *text)
{
    const char *tab = text ? strchr(text, '\t') : NULL;

    if(!tab)
        return 0;

    std::string k(tab + 1);

    while(!k.empty() && k[0] == ' ')
        k.erase(0, 1);

    while(!k.empty() && k[k.size() - 1] == ' ')
        k.erase(k.size() - 1);

    int f = 0;

    if((k[0] == 'F' || k[0] == 'f') && sscanf(k.c_str() + 1, "%d", &f) == 1 && f >= 1 && f <= 12)
        return KEY_F1 + f - 1;

    if(k == "Del" || k == "DEL")
        return KEY_DEL;

    return 0;
}

void bind_keys(const zq_ui_profile *p, Bar *bar)
{
    if(g_keys.empty())
        for(DIALOG *d = zq_ui_main_dialog(); d->proc; ++d)
            if(d->proc == d_keyboard_proc && !d->key && d->d1)
                g_keys.push_back(KeyObject{ d, d->dp });

    for(size_t i = 0; i < g_keys.size(); ++i)
        g_keys[i].d->dp = g_keys[i].dp;

    if(!p->menu || !bar)
        return;

    for(size_t i = 0; i < bar->items.size(); ++i)
    {
        const Item &it = bar->items[i];

        if(!it.item || !it.item->proc || it.none)
            continue;

        const int sc = shortcut_scancode(it.item->text);

        if(!sc)
            continue;

        bool bound = false;

        for(size_t k = 0; k < g_keys.size(); ++k)
            if(g_keys[k].d->d1 == sc)
            {
                g_keys[k].d->dp = (void *)it.item->proc;
                bound = true;
            }

        al_trace("zq_ui: %s: key %s -> %s%s\n", p->build, strchr(it.item->text, '\t') + 1, it.item->text,
                 bound ? "" : " (no key object)");
    }
}

// ---- the profile's key table (letters and modifiers): checked before the editor's own keys ------------

struct KeyBinding
{
    int scancode;
    int mods;           // KB_CTRL_FLAG | KB_SHIFT_FLAG | KB_ALT_FLAG
    int (*proc)();      // NULL: the key does nothing
    int needs;
};

std::vector<KeyBinding> g_key_table;

// "Ctrl+Shift+Z" -> scancode and modifiers; false when the name isn't known
bool parse_key(const char *name, int &scancode, int &mods)
{
    std::string n(name ? name : "");
    mods = 0;
    size_t plus;

    while((plus = n.find('+')) != std::string::npos && plus + 1 < n.size())
    {
        std::string m = n.substr(0, plus);

        for(size_t i = 0; i < m.size(); ++i)
            m[i] = (char)tolower((unsigned char)m[i]);

        if(m == "ctrl" || m == "control")
            mods |= KB_CTRL_FLAG;
        else if(m == "shift")
            mods |= KB_SHIFT_FLAG;
        else if(m == "alt")
            mods |= KB_ALT_FLAG;
        else
            return false;

        n = n.substr(plus + 1);
    }

    if(n.size() == 1 && isalpha((unsigned char)n[0]))
    {
        scancode = KEY_A + (toupper((unsigned char)n[0]) - 'A');
        return true;
    }

    if(n.size() == 1 && isdigit((unsigned char)n[0]))
    {
        scancode = KEY_0 + (n[0] - '0');
        return true;
    }

    int f = 0;

    if(n.size() >= 2 && (n[0] == 'F' || n[0] == 'f') && sscanf(n.c_str() + 1, "%d", &f) == 1 && f >= 1 && f <= 12)
    {
        scancode = KEY_F1 + f - 1;
        return true;
    }

    static const struct { const char *name; int sc; } named[] =
    {
        { "del", KEY_DEL }, { "delete", KEY_DEL }, { "ins", KEY_INSERT }, { "insert", KEY_INSERT },
        { "home", KEY_HOME }, { "end", KEY_END }, { "pgup", KEY_PGUP }, { "pgdn", KEY_PGDN },
        { "pageup", KEY_PGUP }, { "pagedown", KEY_PGDN }, { "esc", KEY_ESC }, { "escape", KEY_ESC },
        { "enter", KEY_ENTER }, { "space", KEY_SPACE }, { "tab", KEY_TAB }, { "up", KEY_UP },
        { "down", KEY_DOWN }, { "left", KEY_LEFT }, { "right", KEY_RIGHT }, { "minus", KEY_MINUS },
        { "equals", KEY_EQUALS }, { "comma", KEY_COMMA }, { "period", KEY_STOP }, { "slash", KEY_SLASH },
    };
    std::string low(n);

    for(size_t i = 0; i < low.size(); ++i)
        low[i] = (char)tolower((unsigned char)low[i]);

    for(size_t i = 0; i < sizeof(named) / sizeof(named[0]); ++i)
        if(low == named[i].name)
        {
            scancode = named[i].sc;
            return true;
        }

    return false;
}

// an action name's procedure and needs (NULL: none here)
int (*action_proc(const char *action, int &needs))()
{
    needs = 0;

    if(!action || !action[0])
        return NULL;

    if(action[0] == '@')
    {
        std::map<std::string, Extra>::iterator e = extras().find(action);

        if(e == extras().end())
            return NULL;

        needs = e->second.needs;
        return e->second.proc;
    }

    if(g_paths.empty())
        walk(zq_ui_menu_253(), "");

    std::map<std::string, MENU *>::iterator e = g_paths.find(action);

    if(e == g_paths.end())
        return NULL;

    needs = path_needs(action);
    return e->second->proc;
}

void load_key_table(const zq_ui_profile *p)
{
    g_key_table.clear();

    for(int i = 0; p->keys && i < p->key_count; ++i)
    {
        KeyBinding k;

        if(!parse_key(p->keys[i].key, k.scancode, k.mods))
        {
            al_trace("zq_ui: %s: no key %s\n", p->build, p->keys[i].key ? p->keys[i].key : "");
            continue;
        }

        int needs = 0;
        k.proc = action_proc(p->keys[i].action, needs);
        k.needs = (std::max)(needs, p->keys[i].needs);

        if(!k.proc && p->keys[i].action && p->keys[i].action[0])
            al_trace("zq_ui: %s: key %s: no action %s\n", p->build, p->keys[i].key, p->keys[i].action);

        g_key_table.push_back(k);
    }
}

// the dialog manager's key filter: -1 when the key isn't the table's, else what its action answered
int key_filter(DIALOG *dialog, int ascii, int scancode)
{
    (void)ascii;

    if(g_key_table.empty() || dialog != zq_ui_main_dialog())
        return -1;

    const int mods = key_shifts & (KB_CTRL_FLAG | KB_SHIFT_FLAG | KB_ALT_FLAG);

    for(size_t i = 0; i < g_key_table.size(); ++i)
    {
        const KeyBinding &k = g_key_table[i];

        if(k.scancode != scancode || k.mods != mods)
            continue;

        if(!k.proc || (k.needs && g_quest_gen < k.needs))
            return D_O_K;

        return k.proc();
    }

    return -1;
}

// ---- applying a profile -------------------------------------------------------------------------------

// what the user set: their colours (Options' colour set) and the windows' own look
bool g_user_captured = false;
RGB g_user_dvc[16];
int g_user_jc[jcMAX];

unsigned rgb8_of(const RGB &c)
{
    return ((unsigned)(c.r * 255 / 63) << 16) | ((unsigned)(c.g * 255 / 63) << 8) | (unsigned)(c.b * 255 / 63);
}

RGB rgb6_of(unsigned c)
{
    RGB out;
    out.r = (unsigned char)(((c >> 16) & 0xFF) * 63 / 255);
    out.g = (unsigned char)(((c >> 8) & 0xFF) * 63 / 255);
    out.b = (unsigned char)((c & 0xFF) * 63 / 255);
    out.filler = 0;
    return out;
}

void apply_look(const zq_ui_look *look)
{
    if(!look || look->keep)
    {
        if(g_user_captured)
        {
            for(int i = 1; i < 16; ++i)
                RAMpal[dvc(i)] = g_user_dvc[i];

            for(int i = 0; i < jcMAX && i < 13; ++i)
                jwin_pal[i] = g_user_jc[i];
        }

        zcui::set_look(0, NULL);
    }
    else
    {
        for(int i = 1; i <= 8; ++i)
            RAMpal[dvc(i)] = rgb6_of(look->dvc[i]);

        if(look->grad_divs > 1 && look->grad_divs <= 7)
        {
            const RGB a = rgb6_of(look->grad_from), b = rgb6_of(look->grad_to);
            const int n = look->grad_divs;

            for(int i = 0; i < n; ++i)
            {
                RGB &c = RAMpal[dvc(15 - n + 1) + i];
                c.r = (unsigned char)(a.r + (b.r - a.r) * i / (n - 1));
                c.g = (unsigned char)(a.g + (b.g - a.g) * i / (n - 1));
                c.b = (unsigned char)(a.b + (b.b - a.b) * i / (n - 1));
            }
        }

        for(int i = 0; i < 13; ++i)
            jwin_pal[i] = dvc((std::max)(1, (std::min)(15, look->jc[i])));

        unsigned cols[13];

        for(int i = 0; i < 13; ++i)
            cols[i] = rgb8_of(RAMpal[jwin_pal[i]]);

        zcui::set_look(look->windows, cols);
    }

    gui_bg_color = jwin_pal[jcBOX];
    gui_fg_color = jwin_pal[jcBOXFG];
    gui_mg_color = jwin_pal[jcMEDDARK];
    jwin_set_colors(jwin_pal);
    set_palette(RAMpal);
}

// the mode's own places (zq_apply_layout's), before a profile moves any
struct Rects
{
    size_and_pos minimap, map_page_bar[9], combolist[3], combolistscrollers[3], comboaliaslist[3],
        comboalias_preview[3], combo_preview, combolist_window, panel[9], favorites_window, favorites_list,
        commands_window, commands_list, layer_panel;
    int mapscreen_x, mapscreen_y, mapscreensize, showedges, combo_columns;
} g_base;

bool g_base_taken = false;

void take_base()
{
    g_base.minimap = minimap;
    g_base.combo_preview = combo_preview;
    g_base.combolist_window = combolist_window;
    g_base.favorites_window = favorites_window;
    g_base.favorites_list = favorites_list;
    g_base.commands_window = commands_window;
    g_base.commands_list = commands_list;
    g_base.layer_panel = layer_panel;
    memcpy(g_base.map_page_bar, map_page_bar, sizeof(map_page_bar));
    memcpy(g_base.combolist, combolist, sizeof(combolist));
    memcpy(g_base.combolistscrollers, combolistscrollers, sizeof(combolistscrollers));
    memcpy(g_base.comboaliaslist, comboaliaslist, sizeof(comboaliaslist));
    memcpy(g_base.comboalias_preview, comboalias_preview, sizeof(comboalias_preview));
    memcpy(g_base.panel, panel, sizeof(panel));
    g_base.mapscreen_x = mapscreen_x;
    g_base.mapscreen_y = mapscreen_y;
    g_base.mapscreensize = mapscreensize;
    g_base.showedges = showedges;
    g_base.combo_columns = 3;
    g_base_taken = true;
}

void restore_base()
{
    if(!g_base_taken)
        return;

    minimap = g_base.minimap;
    combo_preview = g_base.combo_preview;
    combolist_window = g_base.combolist_window;
    favorites_window = g_base.favorites_window;
    favorites_list = g_base.favorites_list;
    commands_window = g_base.commands_window;
    commands_list = g_base.commands_list;
    layer_panel = g_base.layer_panel;
    memcpy(map_page_bar, g_base.map_page_bar, sizeof(map_page_bar));
    memcpy(combolist, g_base.combolist, sizeof(combolist));
    memcpy(combolistscrollers, g_base.combolistscrollers, sizeof(combolistscrollers));
    memcpy(comboaliaslist, g_base.comboaliaslist, sizeof(comboaliaslist));
    memcpy(comboalias_preview, g_base.comboalias_preview, sizeof(comboalias_preview));
    memcpy(panel, g_base.panel, sizeof(panel));
    mapscreen_x = g_base.mapscreen_x;
    mapscreen_y = g_base.mapscreen_y;
    mapscreensize = g_base.mapscreensize;
    showedges = g_base.showedges;
    zq_combo_columns = g_base.combo_columns;
}

size_and_pos *rect_array(const char *what, int *count)
{
    static const struct { const char *name; size_and_pos *a; int n; } list[] =
    {
        { "panel", panel, 9 }, { "map_page_bar", map_page_bar, 9 }, { "combolist", combolist, 3 },
        { "combolistscrollers", combolistscrollers, 3 }, { "comboaliaslist", comboaliaslist, 3 },
        { "comboalias_preview", comboalias_preview, 3 }, { "minimap", &minimap, 1 },
        { "combo_preview", &combo_preview, 1 }, { "combolist_window", &combolist_window, 1 },
        { "favorites_window", &favorites_window, 1 }, { "favorites_list", &favorites_list, 1 },
        { "commands_window", &commands_window, 1 }, { "commands_list", &commands_list, 1 },
        { "layer_panel", &layer_panel, 1 },
    };

    for(size_t i = 0; i < sizeof(list) / sizeof(list[0]); ++i)
        if(!strcmp(list[i].name, what))
        {
            *count = list[i].n;
            return list[i].a;
        }

    *count = 0;
    return NULL;
}

void move_rects(const zq_ui_layout *l)
{
    if(!l || !l->rects)
        return;

    for(int i = 0; i < l->rect_count; ++i)
    {
        const zq_ui_rect &r = l->rects[i];

        // the map: where it is drawn, at which scale (2 or 3), with or without its edges
        if(r.what && !strcmp(r.what, "mapscreen"))
        {
            if(r.x != ZQ_UI_KEEP) mapscreen_x = r.x;
            if(r.y != ZQ_UI_KEEP) mapscreen_y = r.y;
            if(r.w != ZQ_UI_KEEP && r.w >= 1 && r.w <= 3) mapscreensize = r.w;
            if(r.h != ZQ_UI_KEEP) showedges = r.h ? 1 : 0;
            continue;
        }

        // the combo lists: how many show (1 to 3); a combo's size there (only 16 px so far)
        if(r.what && !strcmp(r.what, "combocolumns"))
        {
            if(r.w != ZQ_UI_KEEP && r.w >= 1 && r.w <= 3) zq_combo_columns = r.w;
            if(r.h != ZQ_UI_KEEP && r.h != 16) al_trace("zq_ui: combos of %d px in the lists aren't drawn yet (16 px)\n", r.h);
            continue;
        }

        int n = 0;
        size_and_pos *a = rect_array(r.what ? r.what : "", &n);

        for(int k = 0; k < n; ++k)
        {
            if(r.index >= 0 && r.index != k)
                continue;

            if(r.x != ZQ_UI_KEEP) a[k].x = r.x;
            if(r.y != ZQ_UI_KEEP) a[k].y = r.y;
            if(r.w != ZQ_UI_KEEP) a[k].w = r.w;
            if(r.h != ZQ_UI_KEEP) a[k].h = r.h;
        }
    }
}

// Every panel inside the screen: a profile's places made for another screen size (a 912-px wide one
// shown on 800 px) would otherwise be drawn past the bitmap's edge. A panel is moved in, then cut to
// the screen if it is still bigger.
void keep_on_screen()
{
    struct { size_and_pos *a; int n; int ux, uy; const char *name; } all[] =
    {
        { panel, 9, 1, 1, "panel" }, { map_page_bar, 9, 1, 1, "map_page_bar" }, { combolist, 3, 16, 16, "combolist" },
        { combolistscrollers, 3, 1, 1, "combolistscrollers" }, { comboaliaslist, 3, 16, 16, "comboaliaslist" },
        { comboalias_preview, 3, 1, 1, "comboalias_preview" }, { &minimap, 1, 1, 1, "minimap" },
        { &combo_preview, 1, 1, 1, "combo_preview" }, { &combolist_window, 1, 1, 1, "combolist_window" },
        { &favorites_window, 1, 1, 1, "favorites_window" }, { &favorites_list, 1, 16, 16, "favorites_list" },
        { &commands_window, 1, 1, 1, "commands_window" },
        { &commands_list, 1, command_buttonwidth, command_buttonheight, "commands_list" },
        { &layer_panel, 1, 1, 1, "layer_panel" },
    };

    // only a profile's own places (today's layouts are left exactly as they are)
    const zq_ui_layout *l = g_shown ? g_shown->p->layout : NULL;

    if(!l || !l->rects || l->rect_count <= 0)
        return;

    for(size_t k = 0; k < sizeof(all) / sizeof(all[0]); ++k)
        for(int i = 0; i < all[k].n; ++i)
        {
            size_and_pos &r = all[k].a[i];

            // the Small layout uses one of each and none of the Large layout's own panels
            if(!is_large && (i > 0 || all[k].a == map_page_bar || all[k].a == &favorites_window ||
                             all[k].a == &favorites_list || all[k].a == &commands_window ||
                             all[k].a == &commands_list || all[k].a == &layer_panel || all[k].a == &combolist_window))
                continue;

            if(r.x < 0 || r.y < 0 || r.w <= 0 || r.h <= 0)
                continue;       // not used in this layout

            const int ux = all[k].ux, uy = all[k].uy;
            int w = r.w * ux, h = r.h * uy;
            bool moved = false;

            if(r.x + w > zq_screen_w)
            {
                r.x = (std::max)(0, zq_screen_w - w);
                moved = true;
            }

            if(r.y + h > zq_screen_h)
            {
                r.y = (std::max)(0, zq_screen_h - h);
                moved = true;
            }

            if(r.x + r.w * ux > zq_screen_w)
                r.w = (zq_screen_w - r.x) / ux;

            if(r.y + r.h * uy > zq_screen_h)
                r.h = (zq_screen_h - r.y) / uy;

            if(moved)
                al_trace("zq_ui: %s: %s[%d] past the %dx%d screen, moved in\n", g_shown ? g_shown->p->build : "",
                         all[k].name, i, zq_screen_w, zq_screen_h);
        }
}

const zq_ui_layout *shown_layout()
{
    return g_shown ? g_shown->p->layout : NULL;
}

// the screen size a layout gives the mode (false: the mode's own)
bool layout_size(const zq_ui_layout *l, bool large, int &w, int &h)
{
    if(!l || l->screen_w <= 0 || l->screen_h <= 0)
        return false;

    // a layout's size is its own mode's (the user's choice of the other mode keeps that mode's size)
    if(l->large >= 0 && (l->large != 0) != large)
        return false;

    w = l->screen_w;
    h = l->screen_h;
    return true;
}

void show(const Known *k)
{
    if(!k)
        return;

    const bool same = k == g_shown;
    const zq_ui_layout *old_layout = g_shown ? g_shown->p->layout : NULL;
    g_shown = k;
    const zq_ui_profile *p = k->p;

    // the menu bar
    if(p->menu)
    {
        g_bar = bar_of(p);
        zq_ui_set_menu_bar(g_bar->bar);
    }
    else
    {
        g_bar = NULL;
        zq_ui_set_menu_bar(zq_ui_menu_253());
    }

    bind_keys(p, g_bar);
    load_key_table(p);

    // the layout: the mode (the user's own choice stays theirs), then the panels' places
    const bool user_large = UseSmall == 0;
    const bool large = (!p->layout || p->layout->large < 0) ? user_large : p->layout->large != 0;

    int sw = large ? 800 : 320, sh = large ? 600 : 240;
    layout_size(p->layout, large, sw, sh);

    if(large != is_large || sw != zq_screen_w || sh != zq_screen_h || p->layout != old_layout)
    {
        zq_switch_layout_to(large, true);       // it calls zq_ui_after_layout
        UseSmall = user_large ? 0 : 1;
    }
    else
    {
        restore_base();
        move_rects(p->layout);
    }

    if(!same)
        apply_look(p->look);

    al_trace("zq_ui: showing %s's interface\n", p->build);
    zq_ui_redraw();
}

const Known *wanted()
{
    prepare();
    int gen = 0;
    long order = zq_builds_made(&gen);
    g_quest_gen = gen ? gen : 0x250;
    const std::string s = setting();

    if(s == "newest" && !g_known.empty())
        return &g_known.back();

    const Known *own = profile_for(g_quest_gen, order);

    if(s != "match" && s != "newest")
    {
        const zq_build *b = catalog_build(s.c_str());

        // never an older build's interface than the quest's own
        if(b && !older(b->gen, b->order, g_quest_gen, order == -1 ? 0 : order))
            return profile_for(b->gen, b->order);
    }

    return own;
}

int onMatchBuild()
{
    set_config_string("zquest", "ui_profile", "match");
    flush_config_file();
    zq_ui_follow_quest();
    return D_O_K;
}

int onNewestBuild()
{
    set_config_string("zquest", "ui_profile", "newest");
    flush_config_file();
    zq_ui_follow_quest();
    return D_O_K;
}

struct Start
{
    Start()
    {
        zcport_menu_before_view = before_view;
        zcport_key_filter = key_filter;
    }
} g_start;

} // namespace

MENU zq_ui_quick_menu[] =
{
    { (char *)"&Match the Quest's Build",   onMatchBuild,              NULL,                     0,            NULL   },
    { (char *)"&Newest Build",              onNewestBuild,             NULL,                     0,            NULL   },
    { (char *)"&Choose a Build...",         onInterfaceOptions,        NULL,                     0,            NULL   },
    { (char *)"",                           NULL,                      NULL,                     0,            NULL   },
    { g_showing,                            NULL,                      NULL,                     D_DISABLED,   NULL   },
    {  NULL,                                NULL,                      NULL,                     0,            NULL   }
};

void zq_ui_add_profile(const zq_ui_profile *p)
{
    if(p && p->build)
        registered().push_back(p);
}

void zq_ui_add_action(const char *name, int (*proc)(), int needs)
{
    if(name && name[0] == '@' && proc)
        extras()[name] = Extra{ proc, needs };
}

void zq_ui_follow_quest()
{
    show(wanted());
}

void zq_ui_after_layout()
{
    int w = 0, h = 0;

    if(layout_size(shown_layout(), is_large, w, h))
    {
        zq_screen_w = w;
        zq_screen_h = h;
    }

    zq_combo_columns = 3;       // the layout's own count: a profile's rects may lower it
    take_base();
    move_rects(shown_layout());
    keep_on_screen();

    if(current_combolist >= zq_combo_columns)
        current_combolist = 0;

    if(current_comboalist >= zq_combo_columns)
        current_comboalist = 0;
}

void zq_ui_screen_size(bool large, int *w, int *h)
{
    layout_size(shown_layout(), large, *w, *h);
}

void zq_ui_capture_user_look()
{
    for(int i = 1; i < 16; ++i)
        g_user_dvc[i] = RAMpal[dvc(i)];

    for(int i = 0; i < jcMAX && i < 13; ++i)
        g_user_jc[i] = jwin_pal[i];

    g_user_captured = true;
}

const char *zq_ui_profile_name()
{
    return g_shown ? g_shown->p->build : "2.53.1";
}

// Etc > Interface...: match the quest's build, the newest, or a build from the quest's own on
int onInterfaceOptions()
{
    prepare();
    int gen = 0;
    long order = zq_builds_made(&gen);
    gen = gen ? gen : 0x250;
    const Known *own = profile_for(gen, order);
    std::vector<std::string> rows, values;
    rows.push_back(std::string("Match the quest's build (") + (own ? own->p->build : "2.53.1") + ")");
    values.push_back("match");
    rows.push_back(std::string("Newest (") + (g_known.empty() ? "3.0" : g_known.back().p->build) + ")");
    values.push_back("newest");

    for(size_t i = 0; i < g_catalog.size(); ++i)
    {
        const zq_build &b = g_catalog[i];

        if(older(b.gen, b.order, gen, order == -1 ? 0 : order))
            continue;

        if(std::find(values.begin(), values.end(), b.name) != values.end())
            continue;

        const Known *k = profile_for(b.gen, b.order);
        std::string row = b.name;

        if(k && strcmp(k->p->build, b.name.c_str()) && strncmp(b.name.c_str(), k->p->build, strlen(k->p->build)))
            row += std::string(" (as ") + k->p->build + ")";

        rows.push_back(row);
        values.push_back(b.name);
    }

    const std::string now = setting();
    int dflt = 0;

    for(size_t i = 0; i < values.size(); ++i)
        if(values[i] == now)
            dflt = (int)i;

    int pick = zq_files_pick_row("Interface", "Which build's interface should ZQuest show?", rows, dflt);

    if(pick < 0 || pick >= (int)values.size())
        return D_O_K;

    set_config_string("zquest", "ui_profile", values[pick].c_str());
    flush_config_file();
    zq_ui_follow_quest();
    return D_O_K;
}

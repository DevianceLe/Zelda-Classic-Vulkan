// Scripts' view of a 2.55 or 3.0 quest's subscreens: subscreendata, subscreenpage and
// subscreenwidget (upstream's zc/scripting/types/subscreen*.cpp and the SUBDATA_/SUBPAGE_/
// SUBWIDG_ commands in ffscript.cpp). Included once, from q30_scripts_cmds.h.
//
// The quest reader (q255_init.cpp) turns each subscreen's first page into 2.53 subscreen
// objects, which are what this engine draws, and tells this file what each upstream widget was
// (q30_sub_*): its type, label, position and which object it became, if any. A widget that became
// an object is read and changed through it, so script changes show; the rest (later pages,
// overlay and map subscreens, widget types 2.53 can't draw) keep only what the reader saw.

#ifndef ZC_Q30_SCRIPTS_SUBSCR_H
#define ZC_Q30_SCRIPTS_SUBSCR_H

#include <string>
#include <vector>

extern subscreen_group *current_subscreen_active, *current_subscreen_passive;
static int z30_set_string(long ptr, const std::string &s);    // q30_scripts_cmds.h
int subscreen_cset(miscQdata *misc, int c1, int c2);

// Upstream's widget types (new_subscr.h widgNULL...).
enum
{
    Z30W_NULL, Z30W_FRAME, Z30W_TEXT, Z30W_LINE, Z30W_RECT, Z30W_TIME, Z30W_MMETER, Z30W_LMETER,
    Z30W_BTNITM, Z30W_COUNTER, Z30W_OLDCTR, Z30W_MMAPTITLE, Z30W_MMAP, Z30W_LMAP, Z30W_BGCOLOR,
    Z30W_ITEMSLOT, Z30W_MCGUFF_FRAME, Z30W_MCGUFF, Z30W_TILEBLOCK, Z30W_MINITILE, Z30W_SELECTOR,
    Z30W_LGAUGE, Z30W_MGAUGE, Z30W_TEXTBOX, Z30W_SELECTEDTEXT, Z30W_MISCGAUGE, Z30W_BTNCOUNTER,
    Z30W_COUNTERPERCBAR, Z30W_ITMCOOLDOWNGAUGE, Z30W_ITMCOOLDOWNTEXT, Z30W_MAX
};

struct Z30SubWidget
{
    int type, obj;          // obj: index in its page's 2.53 objects, -1 if it has none
    std::string label;
    int posflags, x, y, w, h, pos;
    dword genflags, flags;
    int pg_mode, pg_btns, pg_targ;  // its page change while selected (upstream SubscrWidget pg_*)
    Q30SubTrans pg_tr;              // and that change's animation
    std::string seltext;            // its selected text override
};

// A page's button transition (upstream SubscrPageTransition), with its animation.
struct Z30PageTrans
{
    int btns, mode, targ, flags;
    Q30SubTrans tr;
};

struct Z30SubPage
{
    std::vector<Z30SubWidget> widgets;
    int cursor_pos;
    std::vector<Z30PageTrans> trans;
    // Its 2.53 objects, kept so it can be swapped in; while it is the page shown they live in
    // custom_subscreen[slot] (what the engine draws) and these are out of date. Texts are kept
    // apart (a text object's dp1 is NULL here).
    bool stored;
    std::vector<subscreen_object> objs;
    std::vector<std::string> texts;
    // display conditions: obj_cond[object] is an index into conds, -1 (or past the end) for none
    std::vector<Q30SubCond> conds;
    std::vector<int> obj_cond;
};

struct Z30Subscreen
{
    int slot;               // custom_subscreen[] index, -1 if not kept
    std::string name;
    std::vector<Z30SubPage> pages;
    int btn_left, btn_right;    // page buttons (INT_BTN_* bits)
    Q30SubTrans tr_left, tr_right;  // their animations
    dword flags;                // upstream's SUBFLAG_ (0x1: no page wrap)
    int curpage;                // the page in custom_subscreen[slot]
};

// active, passive, overlay, map: upstream's sstACTIVE..sstMAP
static std::vector<Z30Subscreen> zasm30_subs[4];
static bool z30_sub_any_cond = false;   // some widget has display conditions

// ---- filled by the reader ----------------------------------------------------------------------

void q30_sub_clear()
{
    for(int i = 0; i < 4; ++i)
        zasm30_subs[i].clear();

    z30_sub_any_cond = false;
}

void q30_sub_widget_seltext(int list, const char *text)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty() || zasm30_subs[list].back().pages.empty() ||
       zasm30_subs[list].back().pages.back().widgets.empty())
        return;

    zasm30_subs[list].back().pages.back().widgets.back().seltext = text ? text : "";
}

void q30_sub_widget_cond(int list, int first, int end, const Q30SubCond &c)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty() || zasm30_subs[list].back().pages.empty() || first < 0 || end <= first)
        return;

    Z30SubPage &p = zasm30_subs[list].back().pages.back();
    p.conds.push_back(c);

    if((int)p.obj_cond.size() < end)
        p.obj_cond.resize(end, -1);

    for(int i = first; i < end; ++i)
        p.obj_cond[i] = (int)p.conds.size() - 1;

    z30_sub_any_cond = true;

    if(getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 subscreen cond: list %d sub %d page %d objects %d-%d: items %d/%d counter %d %d %d litems 0x%x floors %d levels %d dmaps %d maps %d screens %d scrstate %x/%x lvlstate %x genflags 0x%lx\n",
                 list, (int)zasm30_subs[list].size() - 1, (int)zasm30_subs[list].back().pages.size() - 1, first, end - 1,
                 (int)c.owned.size(), (int)c.unowned.size(), c.counter, c.counter_cond, c.counter_val, c.litems, (int)c.floors.size(),
                 (int)c.levels.size(), (int)c.dmaps.size(), (int)c.maps.size(), (int)c.screens.size(), (unsigned)c.scrstate, (unsigned)c.exstate,
                 (unsigned)c.lvlstate, (unsigned long)c.genflags);
}

void q30_sub_begin(int list, int slot, const char *name)
{
    if(list < 0 || list >= 4)
        return;

    Z30Subscreen s;
    s.slot = slot;
    s.name = name ? name : "";
    s.btn_left = s.btn_right = 0;
    memset(&s.tr_left, 0, sizeof(s.tr_left));
    memset(&s.tr_right, 0, sizeof(s.tr_right));
    s.flags = 0;
    s.curpage = 0;
    zasm30_subs[list].push_back(s);
}

void q30_sub_trans(int list, int which, const Q30SubTrans &t)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty())
        return;

    Z30Subscreen &s = zasm30_subs[list].back();

    switch(which)
    {
    case 0:
        if(!s.pages.empty() && !s.pages.back().trans.empty())
            s.pages.back().trans.back().tr = t;
        break;

    case 1:
        if(!s.pages.empty() && !s.pages.back().widgets.empty())
            s.pages.back().widgets.back().pg_tr = t;
        break;

    case 2: s.tr_left = t; break;
    case 3: s.tr_right = t; break;
    }

    if((t.type || t.sfx) && getenv("ZC_Q30_SUBLOG"))   // diagnostics only
        al_trace("q30 subscreen transition: list %d sub %d which %d type %d sfx %d args %ld %ld %ld %ld\n", list,
                 (int)zasm30_subs[list].size() - 1, which, t.type, t.sfx, t.arg[0], t.arg[1], t.arg[2], t.arg[3]);
}

void q30_sub_settings(int list, dword flags, int btn_left, int btn_right)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty())
        return;

    Z30Subscreen &s = zasm30_subs[list].back();
    s.flags = flags;
    s.btn_left = btn_left;
    s.btn_right = btn_right;

    if(getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 subscreen: list %d sub %d (slot %d) page buttons left 0x%02x right 0x%02x flags 0x%x\n", list,
                 (int)zasm30_subs[list].size() - 1, s.slot, btn_left, btn_right, (unsigned)flags);
}

static bool z30_sub_has_text(int sso);

// Copies a page's objects out of an object array (text kept as strings).
static void z30_store_objects(Z30SubPage &p, const subscreen_object *objs, int count)
{
    p.objs.clear();
    p.texts.clear();

    for(int i = 0; i < count && i < MAXSUBSCREENITEMS && objs[i].type != ssoNULL; ++i)
    {
        subscreen_object o = objs[i];
        std::string t;

        if(z30_sub_has_text(o.type))
        {
            if(o.dp1)
                t = (const char *)o.dp1;
            o.dp1 = NULL;
        }

        p.objs.push_back(o);
        p.texts.push_back(t);
    }

    p.stored = true;
}

// Puts a kept page's objects into an object array, freeing the texts of what was there.
static void z30_load_objects(const Z30SubPage &p, subscreen_object *objs)
{
    for(int i = 0; i < MAXSUBSCREENITEMS; ++i)
    {
        if(z30_sub_has_text(objs[i].type) && objs[i].dp1)
            delete [] (char *)objs[i].dp1;
        memset(&objs[i], 0, sizeof(subscreen_object));
    }

    for(size_t i = 0; i < p.objs.size() && i < MAXSUBSCREENITEMS; ++i)
    {
        objs[i] = p.objs[i];

        if(z30_sub_has_text(objs[i].type))
        {
            char *c = new char[p.texts[i].size() + 1];
            memcpy(c, p.texts[i].c_str(), p.texts[i].size() + 1);
            objs[i].dp1 = c;
        }
    }

    // an empty slot ends 2.53's search through the subscreens (q255_init.cpp put_sub_placeholder)
    if(objs[0].type == ssoNULL)
        objs[0].type = ssoNONE;
}

void q30_sub_page_objects(int list, const subscreen_object *objs, int count)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty() || zasm30_subs[list].back().pages.empty())
        return;

    z30_store_objects(zasm30_subs[list].back().pages.back(), objs, count);
}

void q30_sub_page_trans(int list, int btns, int mode, int targ, int flags)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty() || zasm30_subs[list].back().pages.empty())
        return;

    Z30PageTrans t;
    t.btns = btns;
    t.mode = mode;
    t.targ = targ;
    t.flags = flags;
    memset(&t.tr, 0, sizeof(t.tr));
    zasm30_subs[list].back().pages.back().trans.push_back(t);

    if(getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 subscreen: list %d sub %d page %d transition: buttons 0x%02x mode %d target %d flags 0x%x\n", list,
                 (int)zasm30_subs[list].size() - 1, (int)zasm30_subs[list].back().pages.size() - 1, btns, mode, targ, flags);
}

void q30_sub_page(int list, int cursor_pos)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty())
        return;

    Z30SubPage p;
    p.cursor_pos = cursor_pos;
    p.stored = false;
    zasm30_subs[list].back().pages.push_back(p);

    // ZC_Q30_SUBLOG: each subscreen page the quest has (diagnostics only)
    if(getenv("ZC_Q30_SUBLOG"))
        al_trace("q30 subpage: list %d sub %d (slot %d) page %d\n", list, (int)zasm30_subs[list].size() - 1,
                 zasm30_subs[list].back().slot, (int)zasm30_subs[list].back().pages.size() - 1);
}

void q30_sub_widget(int list, int type, const char *label, int obj, int posflags, int x, int y, int w, int h,
                    int pos, dword genflags, dword flags, int pg_mode, int pg_btns, int pg_targ)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty() || zasm30_subs[list].back().pages.empty())
        return;

    Z30SubWidget wd;
    wd.type = type;
    wd.obj = obj;
    wd.label = label ? label : "";
    wd.posflags = posflags;
    wd.x = x;
    wd.y = y;
    wd.w = w;
    wd.h = h;
    wd.pos = pos;
    wd.genflags = genflags;
    wd.flags = flags;
    wd.pg_mode = pg_mode;
    wd.pg_btns = pg_btns;
    wd.pg_targ = pg_targ;
    memset(&wd.pg_tr, 0, sizeof(wd.pg_tr));
    zasm30_subs[list].back().pages.back().widgets.push_back(wd);

    if(pg_mode && getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 subscreen: list %d sub %d page %d widget %d (pos %d): page buttons 0x%02x mode %d target %d\n", list,
                 (int)zasm30_subs[list].size() - 1, (int)zasm30_subs[list].back().pages.size() - 1,
                 (int)zasm30_subs[list].back().pages.back().widgets.size() - 1, pos, pg_btns, pg_mode, pg_targ);
}

// ---- references ------------------------------------------------------------------------------
// As upstream's get_subref/from_subref: subscreen << 24 | page << 16 | (type + 1) << 13 | widget.

static long z30_subref(int sub, int ty, int pg, int ind)
{
    return (long)(((dword)(sub & 0xFF) << 24) | ((dword)(pg & 0xFF) << 16) | ((dword)((ty + 1) & 7) << 13) | (dword)(ind & 0x1FFF));
}

static bool z30_from_subref(long ref, int &sub, int &ty, int &pg, int &ind)
{
    const dword r = (dword)ref;
    ty = (int)((r >> 13) & 7) - 1;
    sub = (int)((r >> 24) & 0xFF);
    pg = (int)((r >> 16) & 0xFF);
    ind = (int)(r & 0x1FFF);
    return ty >= 0 && ty < 4;
}

// The overlay (ty 2) or map (ty 3) subscreen a dmap uses, as an index into that list; -1 for none.
static int z30_sub_dmap_index(int ty, int dmap)
{
    const int idx = ty == 2 ? q30_dmap_overlay_subscreen(dmap) : q30_dmap_map_subscreen(dmap);
    return (idx >= 0 && idx < (int)zasm30_subs[ty].size()) ? idx : -1;
}

// The one of each type the current dmap uses (upstream's new_sub_indexes), -1 for none.
static int z30_sub_current(int ty)
{
    if(ty == 2 || ty == 3)
        return z30_sub_dmap_index(ty, get_currdmap());

    subscreen_group *cur = ty == 0 ? current_subscreen_active : ty == 1 ? current_subscreen_passive : NULL;

    if(!cur)
        return -1;

    const int slot = (int)(cur - custom_subscreen);

    for(size_t i = 0; i < zasm30_subs[ty].size(); ++i)
        if(zasm30_subs[ty][i].slot == slot)
            return (int)i;

    return -1;
}

static Z30Subscreen *z30_subdata(long ref)
{
    int sub, ty, pg, ind;

    if(!z30_from_subref(ref, sub, ty, pg, ind) || sub >= (int)zasm30_subs[ty].size())
        return NULL;

    return &zasm30_subs[ty][sub];
}

static Z30SubPage *z30_subpage(long ref)
{
    int sub, ty, pg, ind;
    Z30Subscreen *s = z30_subdata(ref);

    if(!s || !z30_from_subref(ref, sub, ty, pg, ind) || pg >= (int)s->pages.size())
        return NULL;

    return &s->pages[pg];
}

// The widget, and the 2.53 object it is drawn as (NULL if none).
static Z30SubWidget *z30_subwidg(long ref, subscreen_object **obj = NULL)
{
    int sub, ty, pg, ind;
    Z30Subscreen *s = z30_subdata(ref);
    Z30SubPage *p = z30_subpage(ref);

    if(obj)
        *obj = NULL;

    if(!s || !p || !z30_from_subref(ref, sub, ty, pg, ind) || ind >= (int)p->widgets.size())
        return NULL;

    Z30SubWidget *w = &p->widgets[ind];

    if(obj && w->obj >= 0 && s->slot >= 0 && s->slot < MAXCUSTOMSUBSCREENS && w->obj < MAXSUBSCREENITEMS)
    {
        // the page shown is in custom_subscreen[]; another page's objects are kept apart
        if(pg == s->curpage)
            *obj = &custom_subscreen[s->slot].objects[w->obj];
        else if(p->stored && w->obj < (int)p->objs.size())
            *obj = &p->objs[w->obj];
    }

    // ZC_Q30_SUBLOG: which widget each reference reaches, once each (diagnostics only)
    static const bool log = getenv("ZC_Q30_SUBLOG") != NULL;
    static std::set<long> seen;
    if(log && seen.insert(ref).second)
        al_trace("q30 sublog: ref %ld -> list %d sub %d (slot %d) page %d widget %d: type %d label '%s' obj %d (sso %d)\n",
                 ref, ty, sub, s->slot, pg, ind, w->type, w->label.c_str(), w->obj,
                 (w->obj >= 0 && s->slot >= 0) ? custom_subscreen[s->slot].objects[w->obj].type : -1);

    return w;
}

static bool z30_sub_has_text(int sso)
{
    return sso == ssoTEXT || sso == ssoTEXTBOX || sso == ssoCURRENTITEMTEXT || sso == ssoCURRENTITEMCLASSTEXT;
}

// ---- display conditions (upstream SubscrWidget::check_conditions) -----------------------------

bool q30_get_exstate(int mi, int bit);      // q30_triggers.cpp
dword q30_level_switches(int level);

extern int Awpn, Bwpn, Xwpn, Ywpn;          // link.cpp: the items on the buttons

// upstream's get_ssc_ctr / get_ssc_ctrmax, for the counters 2.53 has (crNONE -1; the negative
// ones are its sscBTNCTR*, key and max life/magic counters)
static double z30_cond_counter(int ctr, bool max)
{
    enum { GENKEYMAGIC = -9, GENKEYNOMAGIC, LEVKEYMAGIC, LEVKEYNOMAGIC, ANYKEYMAGIC, ANYKEYNOMAGIC, MAXHP, MAXMP };
    enum { crMONEY = 1, crBOMBS = 2, crARROWS = 3, crSBOMBS = 6, COUNTERS = 32 };   // 2.53's counter numbers

    // upstream's simplify_counter: sscBTNCTRA_1..Y_1 (-17..-14) and A_0..Y_0 (-13..-10) are
    // the counter the item on A, B, X or Y spends (its second or first cost), if it costs any
    if(ctr >= -17 && ctr <= -10)
    {
        const int btn = (ctr + 17) % 4;
        const int wpn = (btn == 0 ? Awpn : btn == 1 ? Bwpn : btn == 2 ? Xwpn : Ywpn) & 0xFFF;
        const int c = (game && game->get_item(wpn)) ? q30_item_cost_counter(wpn, ctr <= -14 ? 1 : 0) : -1;
        return c < 0 ? 0 : z30_cond_counter(c, max);
    }

    if(ctr == -1 || ctr <= -10 || ctr >= COUNTERS)
        return 0;

    if(ctr == crARROWS && !get_bit(quest_rules, qr_TRUEARROWS) && max)
        ctr = crMONEY;

    if(ctr == MAXHP)
        return game->get_maxlife();

    if(ctr == MAXMP)
        return game->get_maxmagic();

    if(max)
        return ctr >= 0 ? game->get_maxcounter(ctr) : 65535;

    bool inf = false;
    long ret = 0;

    switch(ctr)
    {
    case crMONEY:
        inf = current_item_power(itype_wallet) != 0;
        break;

    case crBOMBS:
        inf = current_item_power(itype_bombbag) != 0;
        break;

    case crSBOMBS:
    {
        const int id = current_item_id(itype_bombbag);
        inf = id >= 0 && itemsbuf[id].power > 0 && (itemsbuf[id].flags & ITEM_FLAG1);
        break;
    }

    case crARROWS:
        inf = current_item_power(itype_quiver) != 0;
        if(!get_bit(quest_rules, qr_TRUEARROWS))
        {
            inf = inf || current_item_power(itype_wallet) != 0;
            ctr = crMONEY;
        }
        break;

    case GENKEYMAGIC: case LEVKEYMAGIC: case ANYKEYMAGIC:
    case GENKEYNOMAGIC: case LEVKEYNOMAGIC: case ANYKEYNOMAGIC:
    {
        if(ctr == GENKEYMAGIC || ctr == LEVKEYMAGIC || ctr == ANYKEYMAGIC)
        {
            const int id = current_item_id(itype_magickey);
            if(id >= 0)
                inf = (itemsbuf[id].flags & ITEM_FLAG1) ? itemsbuf[id].power >= get_dlevel() : itemsbuf[id].power == get_dlevel();
        }

        if(ctr == GENKEYNOMAGIC || ctr == ANYKEYNOMAGIC || ctr == GENKEYMAGIC || ctr == ANYKEYMAGIC)
            ret += game->get_keys();

        if(ctr == LEVKEYNOMAGIC || ctr == ANYKEYNOMAGIC || ctr == LEVKEYMAGIC || ctr == ANYKEYMAGIC)
            ret += game->get_lkeys();

        break;
    }
    }

    if(ctr >= 0)
        return inf ? game->get_maxcounter(ctr) : game->get_counter(ctr);

    return inf ? 65535 : zc_min(65535L, ret);
}

static bool z30_in(const std::vector<int> &set, int v)
{
    return set.empty() || std::find(set.begin(), set.end(), v) != set.end();
}

static bool z30_cond_ok(const Q30SubCond &c)
{
    enum
    {
        REQ_COUNTER_MAX = 0x8, REQ_COUNTER_PERC = 0x10, REQ_INVERT_LITEM = 0x20, REQ_INVERT_SCRSTATE = 0x80,
        REQ_ANY_ITEM = 0x100, REQ_ANY_LITEM = 0x200, REQ_ANY_SCRSTATE = 0x400, REQ_INVERT_LSTATE = 0x800, REQ_ANY_LSTATE = 0x1000
    };
    const dword gf = c.genflags;

    // items (upstream also checks the bunny form, which this engine doesn't have)
    bool pass = !(gf & REQ_ANY_ITEM);
    for(size_t i = 0; i < c.owned.size(); ++i)
    {
        const int id = c.owned[i];
        const bool have = id >= 0 && id < q30_items_end() && game->get_item(id) && !item_disabled(id);
        if(gf & REQ_ANY_ITEM)
        {
            if(have)
            {
                pass = true;
                break;
            }
        }
        else if(!have)
            return false;
    }
    if(!pass)
        return false;

    for(size_t i = 0; i < c.unowned.size(); ++i)
    {
        const int id = c.unowned[i];
        if(id >= 0 && id < q30_items_end() && game->get_item(id) && !item_disabled(id))
            return false;
    }

    const int dm = get_currdmap();
    const int target_screen = currscr >= 0x80 ? homescr : currscr;

    if(!z30_in(c.floors, q30_dmap_floor(dm)) || !z30_in(c.levels, DMaps[dm].level) || !z30_in(c.dmaps, dm) ||
       !z30_in(c.maps, currmap + 1) || !z30_in(c.screens, target_screen))
        return false;

    if(c.counter != -1 && c.counter_cond > 0)
    {
        double val = z30_cond_counter(c.counter, (gf & REQ_COUNTER_MAX) != 0);
        if(gf & REQ_COUNTER_PERC)
        {
            const double mx = z30_cond_counter(c.counter, true);
            val = mx ? (val / mx) * 100.0 : 0;
        }
        const double t = c.counter_val;
        bool ok = true;
        switch(c.counter_cond)
        {
        case 1: ok = val == t; break;   // CONDTY_EQ
        case 2: ok = val != t; break;   // NEQ
        case 3: ok = val > t; break;    // GREATER
        case 4: ok = val >= t; break;   // GREATEREQ
        case 5: ok = val < t; break;    // LESS
        case 6: ok = val <= t; break;   // LESSEQ
        }
        if(!ok)
            return false;
    }

    if(c.litems)
    {
        const int lvl = c.litem_level < 0 ? get_dlevel() : c.litem_level;
        if(lvl >= 0 && lvl < MAXLEVELS)
        {
            const bool inv = (gf & REQ_INVERT_LITEM) != 0;
            const int have = game->lvlitems[lvl] & c.litems;
            if(gf & REQ_ANY_LITEM)
            {
                if(inv ? have == c.litems : have == 0)
                    return false;
            }
            else if(inv ? have != 0 : have != c.litems)
                return false;
        }
    }

    if((c.scrstate_map || c.scrstate_scr < 0) && (c.scrstate || c.exstate))
    {
        const int m = c.scrstate_scr < 0 ? currmap : c.scrstate_map - 1;
        const int sc = c.scrstate_scr < 0 ? target_screen : c.scrstate_scr;
        if(m >= 0 && m < map_count && sc >= 0 && sc < MAPSCRSNORMAL)
        {
            const int mi = m * MAPSCRSNORMAL + sc;
            const dword sstate = game->maps[mi] & c.scrstate;
            dword ex = 0;
            for(int b = 0; b < 32; ++b)
                if((c.exstate >> b) & 1)
                    ex |= q30_get_exstate(mi, b) ? (1u << b) : 0;
            const bool inv = (gf & REQ_INVERT_SCRSTATE) != 0;
            if(gf & REQ_ANY_SCRSTATE)
            {
                if(inv ? (sstate == c.scrstate && ex == c.exstate) : !(sstate || ex))
                    return false;
            }
            else if(inv ? (sstate || ex) : (sstate != c.scrstate || ex != c.exstate))
                return false;
        }
    }

    if(c.lvlstate)
    {
        const int lvl = c.lstate_level < 0 ? dlevel : c.lstate_level;
        if(lvl >= 0 && lvl < MAXLEVELS)
        {
            const bool inv = (gf & REQ_INVERT_LSTATE) != 0;
            const dword have = q30_level_switches(lvl) & c.lvlstate;
            if(gf & REQ_ANY_LSTATE)
            {
                if(inv ? have == c.lvlstate : have == 0)
                    return false;
            }
            else if(inv ? have != 0 : have != c.lvlstate)
                return false;
        }
    }

    return true;
}

// The 2.53 drawing and the subscreen cursor (subscr.cpp, link.cpp): whether object i of a
// subscreen is shown, by its widget's display conditions. Always true for older quests.
bool q30_sub_object_shown(const subscreen_group *g, int i)
{
    if(!z30_sub_any_cond || !g || i < 0 || !q30_active())
        return true;

    for(int l = 0; l < 4; ++l)
    {
        for(size_t k = 0; k < zasm30_subs[l].size(); ++k)
        {
            const Z30Subscreen &s = zasm30_subs[l][k];

            if(s.slot < 0 || s.slot >= MAXCUSTOMSUBSCREENS || &custom_subscreen[s.slot] != g)
                continue;

            if(s.curpage < 0 || s.curpage >= (int)s.pages.size())
                return true;

            const Z30SubPage &p = s.pages[s.curpage];

            if(i >= (int)p.obj_cond.size() || p.obj_cond[i] < 0 || p.obj_cond[i] >= (int)p.conds.size())
                return true;

            return z30_cond_ok(p.conds[p.obj_cond[i]]);
        }
    }

    return true;
}

// The selected text widget (subscr.cpp): the text the selected widget at pos gives in place of
// an item's name (upstream SW_SelectedText), NULL for none.
const char *q30_sub_selected_text(const subscreen_group *g, int pos)
{
    if(!g || !q30_active())
        return NULL;

    for(int l = 0; l < 4; ++l)
    {
        for(size_t k = 0; k < zasm30_subs[l].size(); ++k)
        {
            const Z30Subscreen &s = zasm30_subs[l][k];

            if(s.slot < 0 || s.slot >= MAXCUSTOMSUBSCREENS || &custom_subscreen[s.slot] != g)
                continue;

            if(s.curpage < 0 || s.curpage >= (int)s.pages.size())
                return NULL;

            const Z30SubPage &p = s.pages[s.curpage];

            for(size_t i = 0; i < p.widgets.size(); ++i)
                if((p.widgets[i].genflags & 1) && p.widgets[i].pos == pos)     // SUBSCRFLAG_SELECTABLE
                    return p.widgets[i].seltext.empty() ? NULL : p.widgets[i].seltext.c_str();

            return NULL;
        }
    }

    return NULL;
}

// ---- page changes (upstream ZCSubscreen::check_btns, page_change, wrap_pg) ---------------------
// Instant: the page transitions' animations aren't drawn.

bool q30_passive_on_top();      // q255_basic.cpp: a 2.55/3.0 quest with the newer subscreens

bool q30_subscreen_lr_are_buttons()
{
    return q30_active() && q30_passive_on_top();
}

// The active (or map) subscreen being shown, if it is a 2.55/3.0 one.
static Z30Subscreen *z30_shown_active()
{
    static const int lists[2] = { 0, 3 };

    for(int l = 0; l < 2; ++l)
    {
        for(size_t i = 0; i < zasm30_subs[lists[l]].size(); ++i)
        {
            Z30Subscreen &s = zasm30_subs[lists[l]][i];

            if(s.slot >= 0 && s.slot < MAXCUSTOMSUBSCREENS && &custom_subscreen[s.slot] == current_subscreen_active)
                return &s;
        }
    }

    return NULL;
}

// ---- page change animations (upstream subscrpg_animate / SubscrTransition::draw) -------------
// Both pages are drawn once, as the change happens, and the frames between them are made from
// those two pictures: a slide, or a pixellation by upstream's static dither pattern.

static struct Z30PageAnim
{
    bool on;
    int clk;
    Q30SubTrans tr;
    BITMAP *from, *to, *work;
} g_pg_anim = { false, 0, { 0, 0, 0, { 0, 0, 0, 0 } }, NULL, NULL, NULL };

enum { Z30_SUB_W = 256, Z30_SUB_H = 176 };

// as the open subscreen draws (dosubscr: put_active_subscr with sspDOWN; the cursor is Bpos)
static void z30_sub_draw_page(BITMAP *dest)
{
    clear_bitmap(dest);
    bool q30_should_show_time();    // zc_subscr.cpp
    show_custom_subscreen(dest, &QMisc, current_subscreen_active, 0, 0, q30_should_show_time(), sspDOWN);
}

static bool z30_anim_dither(int x, int y, double perc)     // upstream dither_staticcheck
{
    const double s = sin((double)(x * double(x) + y * double(y)));
    const double c = cos(double(x) * y);
    return fabs(s - c) < perc * 2.0;
}

// While a page change animates, draws its frame in place of the active subscreen (put_active_subscr);
// false once it's over, or when none is under way.
bool q30_subscreen_anim_draw(BITMAP *dest, int dx, int dy)
{
    Z30PageAnim &a = g_pg_anim;

    if(!a.on || !a.from || !a.to || !a.work)
        return false;

    clear_bitmap(a.work);
    bool done = false;

    if(a.tr.type == 1)     // slide: the old page goes out one way, the new one follows
    {
        static const int normal_dir[16] = { up, down, left, right, l_up, r_up, l_down, r_down,
                                            up, r_up, right, r_down, down, l_down, left, l_up };
        const int dir = (a.tr.arg[0] >= 0 && a.tr.arg[0] < 16) ? normal_dir[a.tr.arg[0]] : -1;
        const int diff = int(a.clk * (a.tr.arg[1] / 10000.0));
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;

        switch(dir)
        {
        case up:    done = diff >= Z30_SUB_H; y1 = -diff; y2 = Z30_SUB_H - diff; break;
        case down:  done = diff >= Z30_SUB_H; y1 = diff; y2 = diff - Z30_SUB_H; break;
        case left:  done = diff >= Z30_SUB_W; x1 = -diff; x2 = Z30_SUB_W - diff; break;
        case right: done = diff >= Z30_SUB_W; x1 = diff; x2 = diff - Z30_SUB_W; break;
        default:    done = true; break;
        }

        if(!done)
        {
            blit(a.from, a.work, 0, 0, x1, y1, Z30_SUB_W, Z30_SUB_H);
            blit(a.to, a.work, 0, 0, x2, y2, Z30_SUB_W, Z30_SUB_H);
        }
    }
    else if(a.tr.type == 2)    // pixellate from one page to the other
    {
        const int dur = a.tr.arg[1];
        done = a.clk > dur;

        if(!done)
        {
            double perc = a.clk / double(dur + 1), iperc = 1.0 - perc;
            const bool inv = (a.tr.arg[0] & 1) != 0;
            const int ox = a.tr.arg[2], oy = a.tr.arg[3];

            if(inv)
            {
                const double t = perc;
                perc = iperc;
                iperc = t;
            }

            for(int y = 0; y < Z30_SUB_H; ++y)
                for(int x = 0; x < Z30_SUB_W; ++x)
                {
                    if(!z30_anim_dither(x + ox, y + oy, iperc) != !inv)
                        a.work->line[y][x] = a.from->line[y][x];
                    if(!z30_anim_dither(x + ox, y + oy, perc) != !inv)
                        a.work->line[y][x] = a.to->line[y][x];
                }
        }
    }
    else
        done = true;

    if(done)
    {
        a.on = false;
        return false;
    }

    blit(a.work, dest, 0, 0, dx, dy, Z30_SUB_W, Z30_SUB_H);
    ++a.clk;
    return true;
}

bool q30_subscreen_animating()
{
    return g_pg_anim.on;
}

// mode: upstream's PGGOTO_NEXT 1, PREV 2, TRG 3 (CURRENT 4 changes nothing). The page shown goes
// back to its kept copy (with its cursor), the new one into custom_subscreen[]. tr: the change's
// animation and sound (none for a script's or the reopening's change).
static bool z30_page_change(Z30Subscreen &s, int mode, int targ, bool nowrap, int &pos, const Q30SubTrans *tr = NULL)
{
    if(tr && g_pg_anim.on)
        return false;       // upstream: no change while one animates
    const int n = (int)s.pages.size();
    int pg = s.curpage;

    switch(mode)
    {
    case 1: ++pg; break;
    case 2: --pg; break;
    case 3:
        if(targ >= 0 && targ < n)
            pg = targ;
        break;
    default:
        return false;
    }

    if(pg < 0)
    {
        if(nowrap)
            return false;
        pg = n - 1;
    }
    else if(pg >= n)
    {
        if(nowrap)
            return false;
        pg = 0;
    }

    if(pg == s.curpage || s.slot < 0 || !s.pages[pg].stored)
        return false;

    const bool animate = tr && (tr->type == 1 || tr->type == 2) && &custom_subscreen[s.slot] == current_subscreen_active;

    if(tr && tr->sfx)
        sfx(tr->sfx);

    if(animate)
    {
        if(!g_pg_anim.from)
        {
            g_pg_anim.from = create_bitmap_ex(8, Z30_SUB_W, Z30_SUB_H);
            g_pg_anim.to = create_bitmap_ex(8, Z30_SUB_W, Z30_SUB_H);
            g_pg_anim.work = create_bitmap_ex(8, Z30_SUB_W, Z30_SUB_H);
        }

        z30_sub_draw_page(g_pg_anim.from);
    }

    subscreen_object *objs = custom_subscreen[s.slot].objects;
    s.pages[s.curpage].cursor_pos = pos;
    z30_store_objects(s.pages[s.curpage], objs, MAXSUBSCREENITEMS);
    z30_load_objects(s.pages[pg], objs);
    s.curpage = pg;
    pos = s.pages[pg].cursor_pos;

    if(animate && g_pg_anim.from)
    {
        z30_sub_draw_page(g_pg_anim.to);    // (pos is Bpos itself here: the dosubscr loop's)
        g_pg_anim.tr = *tr;
        g_pg_anim.clk = 0;
        g_pg_anim.on = true;

        if(const char *dump = getenv("ZC_SUBANIM_DUMP"))    // test runs: the two pages' pictures
        {
            PALETTE pal;
            get_palette(pal);
            std::string base(dump);
            save_bmp((base + "_from.bmp").c_str(), g_pg_anim.from, pal);
            save_bmp((base + "_to.bmp").c_str(), g_pg_anim.to, pal);
        }
    }

    if(getenv("ZC_Q30_SUBLOG"))
        al_trace("q30 subscreen: page %d of %d, cursor %d\n", pg, n, pos);

    return true;
}

// As upstream's subscreen loop: the selected widget's page buttons, then the page's transitions
// (each taking its buttons), then the subscreen's left and right page buttons.
void q30_subscreen_pages(int btns, int &pos)
{
    if(!btns || !q30_active())
        return;

    Z30Subscreen *s = z30_shown_active();

    if(getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 subscreen buttons 0x%02x: shown %s, %d pages, page %d, cursor %d\n", btns, s ? s->name.c_str() : "(not a 2.55/3.0 one)",
                 s ? (int)s->pages.size() : 0, s ? s->curpage : -1, pos);

    if(!s || s->pages.size() < 2 || s->curpage < 0 || s->curpage >= (int)s->pages.size())
        return;

    const int start = s->curpage;
    const Z30SubPage &cur = s->pages[start];

    for(size_t i = 0; i < cur.widgets.size(); ++i)
    {
        const Z30SubWidget &w = cur.widgets[i];

        if(w.pos == pos && w.pg_mode && (btns & w.pg_btns))
        {
            z30_page_change(*s, w.pg_mode, w.pg_targ, (w.genflags & 0x2) != 0, pos, &w.pg_tr);  // SUBSCRFLAG_PGGOTO_NOWRAP
            break;
        }
    }

    if(s->curpage != start)
        return;

    for(size_t i = 0; i < s->pages[start].trans.size() && s->curpage == start; ++i)
    {
        const Z30PageTrans t = s->pages[start].trans[i];

        if(t.mode && (btns & t.btns))
        {
            btns &= ~t.btns;
            z30_page_change(*s, t.mode, t.targ, (t.flags & 0x1) != 0, pos, &t.tr);  // SUBSCR_PAGE_TRANSITION_NOWRAP
        }
    }

    if(s->curpage != start)
        return;

    if(btns & s->btn_right)
        z30_page_change(*s, 1, 0, (s->flags & 0x1) != 0, pos, &s->tr_right);       // SUBFLAG_ACT_NOPAGEWRAP
    else if(btns & s->btn_left)
        z30_page_change(*s, 2, 0, (s->flags & 0x1) != 0, pos, &s->tr_left);
}

// Upstream keeps each page's cursor while the subscreen is closed (and only goes back to the
// first page with qr_OLD_SUBSCR); the first page keeps 2.53's cursor from the equipped item.
void q30_subscreen_cursor(bool open, int &pos)
{
    if(!q30_active())
        return;

    g_pg_anim.on = false;   // a page change animating as the subscreen opens or closes is over

    Z30Subscreen *s = z30_shown_active();

    if(!s || s->curpage < 0 || s->curpage >= (int)s->pages.size())
        return;

    if(!open)
        s->pages[s->curpage].cursor_pos = pos;
    else if(s->curpage != 0 && !q30_subscreen_lr_are_buttons())
    {
        int keep = pos;     // qr_OLD_SUBSCR: back to the first page, cursor as 2.53 set it
        z30_page_change(*s, 3, 0, true, keep);
    }
    else if(s->curpage != 0)
        pos = s->pages[s->curpage].cursor_pos;
}

// ---- overlay and map subscreens ---------------------------------------------------------------

void dosubscr(miscQdata *misc);

// put_passive_subscr, after the passive subscreen: the dmap's overlay subscreen, which upstream
// draws over the whole screen (its widgets are placed in screen coordinates, not the passive's).
void q30_draw_overlay_subscreen(BITMAP *dest, miscQdata *misc, int x, bool showtime, int pos2)
{
    if(!q30_active() || x < 0 || x >= dest->w)
        return;

    const int idx = z30_sub_dmap_index(2, get_currdmap());
    const int slot = idx >= 0 ? zasm30_subs[2][idx].slot : -1;

    static const bool log = getenv("ZC_Q30_SUBLOG") != NULL;     // diagnostics only
    static int logged = -2;
    if(log && logged != slot)
    {
        logged = slot;
        int n = 0;
        while(slot >= 0 && n < MAXSUBSCREENITEMS && custom_subscreen[slot].objects[n].type != ssoNULL)
            ++n;
        al_trace("q30 overlay: dmap %d overlay %d slot %d, %d objects, onto %dx%d\n", get_currdmap(), idx, slot, n, dest->w, dest->h);
    }

    if(slot < 0 || slot >= MAXCUSTOMSUBSCREENS)
        return;

    BITMAP *sub = create_sub_bitmap(dest, x, 0, dest->w - x, dest->h);

    if(!sub)
        return;

    show_custom_subscreen(sub, misc, &custom_subscreen[slot], 0, 0, showtime, pos2);
    destroy_bitmap(sub);
}

// onViewMap: a dmap with a map subscreen opens it, the way the active subscreen opens, instead of
// the old map (upstream: map_subscreen >= 0 means dosubscr(true)). Returns false to show the map.
bool q30_open_map_subscreen()
{
    if(!q30_active() || q30_dmap_map_subscreen(get_currdmap()) < 0)
        return false;

    const int idx = z30_sub_dmap_index(3, get_currdmap());
    const int slot = idx >= 0 ? zasm30_subs[3][idx].slot : -1;

    if(slot >= 0 && slot < MAXCUSTOMSUBSCREENS)
    {
        subscreen_group *was = current_subscreen_active;
        current_subscreen_active = &custom_subscreen[slot];
        dosubscr(&QMisc);
        current_subscreen_active = was;
    }

    return true;
}

// ZC_Q30_SUBDUMP="frame,file.bmp": draws the active subscreen as it would open, at that frame, to a
// file (diagnostics only; the tour never opens it).
static void zasm30_subdump_tick()
{
    static const char *spec = getenv("ZC_Q30_SUBDUMP");
    static bool done = false;
    int at = 0;
    char path[1024];

    if(!spec || done || sscanf(spec, "%d,%1023s", &at, path) != 2 || frame < at || !current_subscreen_active)
        return;

    done = true;

    // ZC_Q30_SUBDUMP_ITEMS="id,id,...": owned for the picture; bottles among them get slot
    // contents 1, 2, 3... in the order given
    if(const char *give = getenv("ZC_Q30_SUBDUMP_ITEMS"))
    {
        int type = 1;
        for(const char *p = give; *p; )
        {
            const int id = atoi(p);
            if(id > 0 && id < q30_items_end())
            {
                game->set_item(id, true);
                if(q30_is_bottle(id))
                    q30_set_bottle_slot((int)itemsbuf[id].misc1, type++);
            }
            while(*p && *p != ',') ++p;
            if(*p) ++p;
        }
    }

    BITMAP *b = create_bitmap_ex(8, 256, 176);

    if(!b)
        return;

    clear_bitmap(b);
    load_Sitems(&QMisc);
    show_custom_subscreen(b, &QMisc, current_subscreen_active, 0, 0, true, sspUP);
    Sitems.animate();      // as each frame of the open subscreen does: items pick their tiles
    clear_bitmap(b);
    show_custom_subscreen(b, &QMisc, current_subscreen_active, 0, 0, true, sspUP);
    PALETTE pal;
    get_palette(pal);
    save_bmp(path, b, pal);
    destroy_bitmap(b);
    al_trace("q30 subdump: frame %d dmap %d screen %d -> %s\n", frame, get_currdmap(), currscr, path);
}

static bool z30_sub_check_bomb_inner(int iid, bool super);

// upstream's check_bomb / check_sbomb (new_subscr.cpp) for an item slot naming a bomb item;
// ZC_Q30_BOMBSLOT=1 logs each change of what it decides for an item
static bool z30_sub_check_bomb(int iid, bool super)
{
    const bool shown = z30_sub_check_bomb_inner(iid, super);

    static const bool log = getenv("ZC_Q30_BOMBSLOT") != NULL;
    static std::map<int, bool> last;

    if(log && (last.find(iid) == last.end() || last[iid] != shown))
    {
        last[iid] = shown;
        al_trace("q30 bomb slot: item %d (%s) %s: rule 433 %d (compat %d), bombs %d, super bombs %d, cost counters %d %d (payable %d)\n",
                 iid, super ? "super" : "bomb", shown ? "shown" : "hidden", q30_compat_rule(433) ? 1 : 0,
                 (int)q255_compat_rule_version, game->get_bombs(), game->get_sbombs(),
                 q30_item_cost_counter(iid, 0), q30_item_cost_counter(iid, 1), q30_item_costs_payable(iid) ? 1 : 0);
    }

    return shown;
}

static bool z30_sub_check_bomb_inner(int iid, bool super)
{
    enum { QR_NEVERDISABLEAMMOONSUBSCREEN = 161, QR_BROKEN_BOMB_AMMO_COSTS = 433 };

    if(q30_quest_rule(QR_NEVERDISABLEAMMOONSUBSCREEN))
        return true;

    if(!super && current_item_power(itype_bombbag))
        return true;

    if(super)
    {
        // (upstream's get_subscr_item_id: the best bag Link has, its costs not asked)
        const int bag = current_item_id(itype_bombbag, false);
        if(bag >= 0 && itemsbuf[bag].power && (itemsbuf[bag].flags & ITEM_FLAG1))
            return true;
    }

    // qr_BROKEN_BOMB_AMMO_COSTS, which upstream turns on for quests saved before compat version
    // 61: shown while Link has bombs. Otherwise while he can pay the item's own costs, any
    // counter (a bomb's second cost is usually one bomb): 2.53's checkmagiccost knows only magic
    // and rupees.
    if(q30_compat_rule(QR_BROKEN_BOMB_AMMO_COSTS) ? (super ? game->get_sbombs() : game->get_bombs()) != 0
       : q30_item_costs_payable(iid))
        return true;

    // remote bombs: still usable with one out
    return itemsbuf[iid].misc1 == 0 && Lwpns.idCount(super ? wLitSBomb : wLitBomb) > 0;
}

// show_custom_subscreen's ssoCURRENTITEM in a 2.55/3.0 quest, for a slot that names its item (the
// reader keeps it + 1 in d8, any of the quest's items), or a class 2.53 has no family for (the
// reader keeps that class + 1 in d10). Returns true when it decided: *id is the item to draw, or
// -1 for none, as upstream's SW_ItemSlot::getDisplayItem (shown when Link owns that very item;
// 2.53 asked for the first item of its class and level instead).
bool q30_subscreen_item(const subscreen_object &o, int *id)
{
    if(!q30_active() || !game)
        return false;

    if(o.d8 > 0)
    {
        const int item = o.d8 - 1;
        const bool nosp = o.d9 != 0;    // SUBSCR_CURITM_IGNR_SP_DISPLAY
        bool select = false;

        if(item >= q30_items_end())
            return false;

        switch(q30_item_family(item))
        {
        case itype_bomb:
            select = z30_sub_check_bomb(item, false);
            break;

        case itype_bowandarrow:
        case itype_arrow:
            select = true;
            break;

        case itype_letterpotion:
            break;

        case itype_sbomb:
            select = z30_sub_check_bomb(item, true);
            break;

        // the special displays
        case itype_map:
        case itype_compass:
        case itype_bosskey:
            if(nosp)
                break;

            *id = has_item(q30_item_family(item), -1) ? item : -1;
            return true;

        case itype_heartpiece:
            if(nosp)
                break;

            if(QMisc.colors.HCpieces_tile)
            {
                *id = item;
                return true;
            }

            break;

        default:
            select = true;
            break;
        }

        *id = (select && !item_disabled(item) && game->get_item(item)) ? item : -1;
        return true;
    }

    if(o.d10 > 0)
    {
        *id = q30_current_item(o.d10 - 1);
        return true;
    }

    return false;
}

// ---- colours, as upstream's SubscrColorInfo get_int_color/set_int_color --------------------------

enum { Z30_SUB_SYS_COLORS = 16, Z30_SUB_TRANSPARENT = -(17 + 1 + 16), Z30_SUB_MIN_COLOR = -(19 + 16) };

static long z30_sub_int_color(byte type, short color)
{
    if(type == ssctSYSTEM)
        return color == -1 ? Z30_SUB_TRANSPARENT : -(color + 1);
    if(type == ssctMISC)
        return -(color + 1 + Z30_SUB_SYS_COLORS);
    if(type < 16)
        return type * 16 + color;
    return 0;
}

// The same conversions the reader makes (put_sub_color in q255_init.cpp): 2.53 has no message
// text colour, and draws transparent and "default" as nothing.
static void z30_sub_set_color(byte &type, short &color, long val)
{
    if(val > 255 || val < Z30_SUB_MIN_COLOR)
        return;

    if(val >= 0)
    {
        type = (byte)((val & 0xF0) >> 4);
        color = (short)(val & 0x0F);
    }
    else if(val == Z30_SUB_TRANSPARENT)
    {
        type = ssctSYSTEM;
        color = -1;
    }
    else if(val >= -Z30_SUB_SYS_COLORS)
    {
        type = ssctSYSTEM;
        color = (short)(-val - 1);
    }
    else
    {
        type = ssctMISC;
        color = (short)(-val - 1 - Z30_SUB_SYS_COLORS);

        if(color == ssctMSGTEXT)
            color = ssctTEXT;
        else if(color > ssctMSGTEXT)
        {
            type = ssctSYSTEM;
            color = -1;
        }
    }
}

// Which of an object's three colours a widget colour register means; -1 if the type has none.
static int z30_sub_color_slot(int wtype, int reg)
{
    switch(reg)
    {
    case Z30R_SUBWIDGTY_COLOR_TXT:
        switch(wtype)
        {
        case Z30W_TEXT: case Z30W_TEXTBOX: case Z30W_SELECTEDTEXT: case Z30W_TIME: case Z30W_COUNTER:
        case Z30W_OLDCTR: case Z30W_MMAPTITLE:
            return 1;
        case Z30W_MCGUFF_FRAME:
            return 2;           // the piece count's colour
        }
        return -1;
    case Z30R_SUBWIDGTY_COLOR_SHD:
        switch(wtype)
        {
        case Z30W_TEXT: case Z30W_TEXTBOX: case Z30W_SELECTEDTEXT: case Z30W_TIME: case Z30W_COUNTER:
        case Z30W_OLDCTR: case Z30W_MMAPTITLE:
            return 2;
        }
        return -1;
    case Z30R_SUBWIDGTY_COLOR_BG:
        switch(wtype)
        {
        case Z30W_TEXT: case Z30W_TEXTBOX: case Z30W_SELECTEDTEXT: case Z30W_TIME: case Z30W_COUNTER:
        case Z30W_OLDCTR: case Z30W_MMAPTITLE:
            return 3;
        case Z30W_BGCOLOR:
            return 1;
        }
        return -1;
    case Z30R_SUBWIDGTY_COLOR_OLINE:
        return wtype == Z30W_RECT ? 1 : wtype == Z30W_LINE ? 1 : wtype == Z30W_MCGUFF_FRAME ? 1 : -1;
    case Z30R_SUBWIDGTY_COLOR_FILL:
        return wtype == Z30W_RECT ? 2 : -1;
    }

    return -1;
}

static void z30_sub_color_ref(subscreen_object *o, int slot, byte *&type, short *&color)
{
    type = slot == 1 ? &o->colortype1 : slot == 2 ? &o->colortype2 : &o->colortype3;
    color = slot == 1 ? &o->color1 : slot == 2 ? &o->color2 : &o->color3;
}

// The 2.53 object's fields upstream's widget flags turn into (read_widget in q255_init.cpp).
static void z30_sub_apply_flags(const Z30SubWidget &w, subscreen_object &o)
{
    const int f1 = (w.flags & 1) ? 1 : 0, f2 = (w.flags & 2) ? 1 : 0, f4 = (w.flags & 4) ? 1 : 0;

    switch(w.type)
    {
    case Z30W_FRAME: o.d3 = f2; o.d4 = f1; break;
    case Z30W_LINE: o.d4 = f1; break;
    case Z30W_RECT: o.d1 = f2; o.d2 = f1; break;
    case Z30W_LMETER: o.d2 = f1; break;
    case Z30W_BTNITM: o.d2 = f1; break;
    case Z30W_COUNTER: o.d6 = f1 | (f2 << 1); break;
    case Z30W_OLDCTR: o.d2 = f1; break;
    case Z30W_MMAPTITLE: o.d4 = f1; break;
    case Z30W_MMAP: o.d1 = f1; o.d2 = f2; o.d3 = f4; break;
    case Z30W_LMAP: o.d1 = f1; o.d2 = f2; o.d3 = f4; o.d10 = (w.flags & 8) ? 1 : 0; break;
    case Z30W_ITEMSLOT: o.d2 = f1 ? 0 : 1; break;
    case Z30W_MCGUFF_FRAME: o.d5 = f1; o.d6 = f2; o.d7 = f4; break;
    case Z30W_MCGUFF: o.d3 = f1; o.d4 = f2; break;
    case Z30W_TILEBLOCK: o.d3 = f1; o.d4 = f2; break;
    case Z30W_MINITILE: o.d5 = f1; o.d6 = f2; break;
    case Z30W_SELECTOR: o.type = f4 ? ssoSELECTOR2 : ssoSELECTOR1; o.d4 = f1; o.d5 = f2; break;
    case Z30W_LGAUGE: case Z30W_MGAUGE: o.d10 = (int)(w.flags & 0x1F); break;
    case Z30W_TEXTBOX: case Z30W_SELECTEDTEXT: o.d4 = f1; break;
    }
}

// ---- fonts -------------------------------------------------------------------------------------
// The reader's sub_font, both ways: upstream's font ids against 2.53's ssf numbers.
static const int z30_sub_fonts[][2] =
{
    { 0, ssfZELDA }, { 1, ssfZ3 }, { 2, ssfZ3SMALL }, { 8, ssfZTIME }, { 9, ssfSMALL }, { 11, ssfSMALLPROP },
    { 12, ssfSS1 }, { 13, ssfSS2 }, { 14, ssfSS3 }, { 15, ssfSS4 }, { 16, ssfGBLA }, { 17, ssfGORON },
    { 18, ssfZORAN }, { 19, ssfHYLIAN1 }, { 20, ssfHYLIAN2 }, { 21, ssfHYLIAN3 }, { 22, ssfHYLIAN4 },
    { 23, ssfGBORACLE }, { 24, ssfGBORACLEP }, { 25, ssfDSPHANTOM }, { 26, ssfDSPHANTOMP }, { 71, ssfPROP }
};

static int z30_sub_font_to_ssf(long font)
{
    for(size_t i = 0; i < sizeof(z30_sub_fonts) / sizeof(z30_sub_fonts[0]); ++i)
        if(z30_sub_fonts[i][0] == font)
            return z30_sub_fonts[i][1];
    // the rest of upstream's fonts, as the reader keeps them (Q30_SSF_FONTS + id)
    if(font >= 0 && font < Q30_FONT_COUNT)
        return Q30_SSF_FONTS + (int)font;
    return -1;
}

static long z30_sub_font_from_ssf(int ssf)
{
    if(ssf >= Q30_SSF_FONTS && ssf < Q30_SSF_FONTS + Q30_FONT_COUNT)
        return ssf - Q30_SSF_FONTS;
    for(size_t i = 0; i < sizeof(z30_sub_fonts) / sizeof(z30_sub_fonts[0]); ++i)
        if(z30_sub_fonts[i][1] == ssf)
            return z30_sub_fonts[i][0];
    return 0;
}

static bool z30_sub_is_textual(int wtype)
{
    switch(wtype)
    {
    case Z30W_TEXT: case Z30W_TEXTBOX: case Z30W_SELECTEDTEXT: case Z30W_TIME: case Z30W_COUNTER:
    case Z30W_OLDCTR: case Z30W_MMAPTITLE: case Z30W_BTNCOUNTER: case Z30W_ITMCOOLDOWNTEXT:
        return true;
    }
    return false;
}

// ---- registers ---------------------------------------------------------------------------------

static bool z30_sub_unported(const long arg, bool write)
{
    zasm30_log_once(write ? "register write" : "register read", arg);
    return true;
}

// The tile or corner (what 0 or 1) a tile-like widget's index means, in its object's fields, or
// for what 2 its cset: as a colour pair (*ctype, *cval), or for a gauge piece a plain cset (*plain),
// which the reader keeps in colortype1, color1, colortype2, color2 for the four pieces. Returns
// false if the widget has no such thing there.
static bool z30_sub_tile_field(const Z30SubWidget &w, subscreen_object &o, int what, long idx, int **field,
                               byte **ctype, short **cval, bool *gauge)
{
    *field = NULL;
    *ctype = NULL;
    *cval = NULL;
    *gauge = false;

    switch(w.type)
    {
    case Z30W_FRAME: case Z30W_MCGUFF: case Z30W_TILEBLOCK:
        if(idx != 0 || what == 1) return false;
        if(what == 0) *field = &o.d1;
        else { *ctype = &o.colortype1; *cval = &o.color1; }
        return true;
    case Z30W_MINITILE:
        if(idx != 0) return false;
        if(what == 2) { *ctype = &o.colortype1; *cval = &o.color1; }
        else *field = &o.d1;
        return true;
    case Z30W_LGAUGE: case Z30W_MGAUGE:
        if(idx < 0 || idx > 3) return false;
        *gauge = true;
        if(what == 2)
        {
            if(idx == 0) *ctype = &o.colortype1;
            else if(idx == 1) *cval = &o.color1;
            else if(idx == 2) *ctype = &o.colortype2;
            else *cval = &o.color2;
            return true;
        }
        *field = idx == 0 ? &o.d2 : idx == 1 ? &o.d3 : idx == 2 ? &o.d4 : &o.d5;
        return true;
    }

    return false;
}

static bool zasm30_sub_get(const long arg, long &ret)
{
    switch(arg)
    {
    // subscreendata->
    case Z30R_SUBDATATYPE:
    {
        int sub, ty, pg, ind;
        ret = (z30_subdata(Z30_REF(SUBSCREENDATA)) && z30_from_subref(Z30_REF(SUBSCREENDATA), sub, ty, pg, ind)) ? ty * 10000 : 0;
        return true;
    }
    case Z30R_SUBDATANUMPG:
    {
        int sub, ty, pg, ind;
        Z30Subscreen *s = z30_subdata(Z30_REF(SUBSCREENDATA));
        ret = 0;
        if(s && z30_from_subref(Z30_REF(SUBSCREENDATA), sub, ty, pg, ind))
            ret = (ty == 0 || ty == 3) ? (long)s->pages.size() * 10000 : 10000;
        return true;
    }
    case Z30R_SUBDATAPAGES:
    {
        int sub, ty, pg, ind;
        Z30Subscreen *s = z30_subdata(Z30_REF(SUBSCREENDATA));
        ret = 0;
        if(s && z30_from_subref(Z30_REF(SUBSCREENDATA), sub, ty, pg, ind) && Z30_INDEX >= 0 && Z30_INDEX < (long)s->pages.size())
            ret = z30_subref(sub, ty, Z30_INDEX, 0);
        return true;
    }
    case Z30R_SUBDATACURPG:
    {
        Z30Subscreen *s = z30_subdata(Z30_REF(SUBSCREENDATA));
        ret = s ? s->curpage * 10000 : 0;
        return true;
    }
    case Z30R_SUBDATATRANSCLK: ret = -10000; return true;
    case Z30R_SUBDATATRANSFROMPG:
    case Z30R_SUBDATATRANSTOPG:
    case Z30R_SUBDATATRANSTY:
        ret = 0;
        return true;
    case Z30R_SUBDATACURSORPOS:
    {
        Z30Subscreen *s = z30_subdata(Z30_REF(SUBSCREENDATA));
        ret = 0;
        if(s && !s->pages.empty())
            ret = (s->slot >= 0 && &custom_subscreen[s->slot] == current_subscreen_active ? Bpos : s->pages[s->curpage].cursor_pos) * 10000;
        return true;
    }

    // subscreenpage->
    case Z30R_SUBPGINDEX:
    {
        int sub, ty, pg, ind;
        ret = (z30_subpage(Z30_REF(SUBSCREENPAGE)) && z30_from_subref(Z30_REF(SUBSCREENPAGE), sub, ty, pg, ind)) ? pg * 10000 : 0;
        return true;
    }
    case Z30R_SUBPGNUMWIDG:
    {
        Z30SubPage *p = z30_subpage(Z30_REF(SUBSCREENPAGE));
        ret = p ? (long)p->widgets.size() * 10000 : 0;
        return true;
    }
    case Z30R_SUBPGWIDGETS:
    {
        int sub, ty, pg, ind;
        Z30SubPage *p = z30_subpage(Z30_REF(SUBSCREENPAGE));
        ret = 0;
        if(p && z30_from_subref(Z30_REF(SUBSCREENPAGE), sub, ty, pg, ind) && Z30_INDEX >= 0 && Z30_INDEX < (long)p->widgets.size())
            ret = z30_subref(sub, ty, pg, Z30_INDEX);
        return true;
    }
    case Z30R_SUBPGSUBDATA:
    {
        int sub, ty, pg, ind;
        ret = (z30_subpage(Z30_REF(SUBSCREENPAGE)) && z30_from_subref(Z30_REF(SUBSCREENPAGE), sub, ty, pg, ind)) ? z30_subref(sub, ty, 0, 0) : 0;
        return true;
    }
    case Z30R_SUBPGCURSORPOS:
    {
        int sub, ty, pg, ind;
        Z30Subscreen *s = z30_subdata(Z30_REF(SUBSCREENPAGE));
        Z30SubPage *p = z30_subpage(Z30_REF(SUBSCREENPAGE));
        ret = 0;
        if(p && z30_from_subref(Z30_REF(SUBSCREENPAGE), sub, ty, pg, ind))
            ret = (pg == 0 && s->slot >= 0 && &custom_subscreen[s->slot] == current_subscreen_active ? Bpos : p->cursor_pos) * 10000;
        return true;
    }
    default:
        break;
    }

    // subscreenwidget->
    subscreen_object *o = NULL;
    Z30SubWidget *w = NULL;

    switch(arg)
    {
    case Z30R_SUBWIDGTYPE: case Z30R_SUBWIDGINDEX: case Z30R_SUBWIDGPAGE: case Z30R_SUBWIDGPOS:
    case Z30R_SUBWIDGPOSFLAG: case Z30R_SUBWIDGX: case Z30R_SUBWIDGY: case Z30R_SUBWIDGW: case Z30R_SUBWIDGH:
    case Z30R_SUBWIDGGENFLAG: case Z30R_SUBWIDGFLAG: case Z30R_SUBWIDGTY_TILE: case Z30R_SUBWIDGTY_CSET:
    case Z30R_SUBWIDGTY_CORNER: case Z30R_SUBWIDGTY_FLIP: case Z30R_SUBWIDGTY_FONT: case Z30R_SUBWIDGTY_ALIGN:
    case Z30R_SUBWIDGTY_SHADOWTY: case Z30R_SUBWIDGTY_COLOR_TXT: case Z30R_SUBWIDGTY_COLOR_SHD:
    case Z30R_SUBWIDGTY_COLOR_BG: case Z30R_SUBWIDGTY_COLOR_OLINE: case Z30R_SUBWIDGTY_COLOR_FILL:
    case Z30R_SUBWIDGTY_TABSIZE: case Z30R_SUBWIDGEQPITM:
        w = z30_subwidg(Z30_REF(SUBSCREENWIDG), &o);
        if(!w)
        {
            ret = 0;
            return true;
        }
        break;
    default:
        return false;
    }

    const long idx = Z30_INDEX;

    switch(arg)
    {
    case Z30R_SUBWIDGTYPE: ret = w->type * 10000; return true;
    case Z30R_SUBWIDGINDEX:
    case Z30R_SUBWIDGPAGE:
    {
        int sub, ty, pg, ind;
        z30_from_subref(Z30_REF(SUBSCREENWIDG), sub, ty, pg, ind);
        ret = (arg == Z30R_SUBWIDGINDEX ? ind : pg) * 10000;
        return true;
    }
    case Z30R_SUBWIDGPOS: ret = w->pos * 10000; return true;
    case Z30R_SUBWIDGPOSFLAG: ret = (idx >= 0 && idx < 3 && ((o ? o->pos : w->posflags) & (1 << idx))) ? 10000 : 0; return true;
    case Z30R_SUBWIDGX: ret = (o ? (short)o->x : w->x) * 10000; return true;
    case Z30R_SUBWIDGY: ret = (o ? (short)o->y : w->y) * 10000; return true;
    case Z30R_SUBWIDGW: ret = (o ? o->w : w->w) * 10000; return true;
    case Z30R_SUBWIDGH: ret = (o ? o->h : w->h) * 10000; return true;
    case Z30R_SUBWIDGGENFLAG: ret = (idx >= 0 && idx < 32 && (w->genflags & (1u << idx))) ? 10000 : 0; return true;
    case Z30R_SUBWIDGFLAG: ret = (idx >= 0 && idx < 32 && (w->flags & (1u << idx))) ? 10000 : 0; return true;

    case Z30R_SUBWIDGTY_TILE:
    case Z30R_SUBWIDGTY_CORNER:
    case Z30R_SUBWIDGTY_CSET:
    {
        int *field;
        byte *ctype;
        short *cval;
        bool gauge;
        const int what = arg == Z30R_SUBWIDGTY_TILE ? 0 : arg == Z30R_SUBWIDGTY_CORNER ? 1 : 2;

        ret = -10000;

        if(!o)
            return z30_sub_unported(arg, false);
        if(!z30_sub_tile_field(*w, *o, what, idx, &field, &ctype, &cval, &gauge))
            return true;

        if(what == 2)
        {
            // gauge pieces keep a plain cset; the others a colour pair, read as the cset it gives
            if(gauge) ret = (ctype ? *ctype : *cval) * 10000;
            else ret = subscreen_cset(&QMisc, *ctype, *cval) * 10000;
            return true;
        }

        const int v = *field;

        if(w->type == Z30W_MINITILE && v == -1)
            ret = what == 0 ? -(o->d2 + 1) * 10000 : o->d3 * 10000;     // a special tile
        else if(w->type == Z30W_MINITILE || gauge)
            ret = (what == 0 ? (v >> 2) : (v & 3)) * 10000;
        else
            ret = v * 10000;
        return true;
    }

    case Z30R_SUBWIDGTY_FLIP:
        if(!o)
            { ret = -10000; return z30_sub_unported(arg, false); }
        switch(w->type)
        {
        case Z30W_TILEBLOCK: case Z30W_MCGUFF: ret = o->d2 * 10000; return true;
        case Z30W_MINITILE: ret = o->d4 * 10000; return true;
        }
        ret = -10000;
        return true;

    case Z30R_SUBWIDGTY_FONT:
    case Z30R_SUBWIDGTY_ALIGN:
    case Z30R_SUBWIDGTY_SHADOWTY:
        if(!z30_sub_is_textual(w->type))
        {
            ret = -10000;
            return true;
        }
        if(!o)
            { ret = -10000; return z30_sub_unported(arg, false); }
        if(arg == Z30R_SUBWIDGTY_FONT) ret = z30_sub_font_from_ssf(o->d1) * 10000;
        else if(arg == Z30R_SUBWIDGTY_ALIGN) ret = (w->type == Z30W_OLDCTR ? 0 : o->d2) * 10000;
        else ret = o->d3 * 10000;
        return true;

    case Z30R_SUBWIDGTY_TABSIZE:
        if(w->type != Z30W_TEXTBOX && w->type != Z30W_SELECTEDTEXT)
        {
            ret = -10000;
            return true;
        }
        if(!o)
            { ret = -10000; return z30_sub_unported(arg, false); }
        ret = o->d5 * 10000;
        return true;

    case Z30R_SUBWIDGTY_COLOR_TXT: case Z30R_SUBWIDGTY_COLOR_SHD: case Z30R_SUBWIDGTY_COLOR_BG:
    case Z30R_SUBWIDGTY_COLOR_OLINE: case Z30R_SUBWIDGTY_COLOR_FILL:
    {
        const int slot = z30_sub_color_slot(w->type, arg);
        if(slot < 0)
        {
            ret = 0;
            return true;
        }
        if(!o)
            { ret = 0; return z30_sub_unported(arg, false); }
        // the reader puts a rectangle's outline in colour 1 and its fill in colour 2
        byte *ct;
        short *cv;
        z30_sub_color_ref(o, slot, ct, cv);
        ret = z30_sub_int_color(*ct, *cv) * 10000;
        return true;
    }

    case Z30R_SUBWIDGEQPITM:
    {
        // an item slot's item: the one it names, or Link's current one of its class
        ret = 0;
        if(w->type != Z30W_ITEMSLOT || !o)
            return true;
        int id = o->d8 > 0 ? o->d8 - 1 : -1;
        if(id < 0 && o->d1 >= 0 && o->d1 < itype_max)
            id = current_item_id(o->d1);
        ret = (id >= 0 ? id : -1) * 10000;
        return true;
    }
    }

    return false;
}

static bool zasm30_sub_set(const long arg, const long value)
{
    switch(arg)
    {
    case Z30R_SUBDATACURPG:
    {
        // Upstream: active and map subscreens only, bound to the pages there are.
        int sub, ty, pg, ind;
        Z30Subscreen *s = z30_subdata(Z30_REF(SUBSCREENDATA));
        if(!s || s->pages.empty() || !z30_from_subref(Z30_REF(SUBSCREENDATA), sub, ty, pg, ind) || (ty != 0 && ty != 3))
            return true;
        const bool shown = s->slot >= 0 && &custom_subscreen[s->slot] == current_subscreen_active;
        int pos = shown ? Bpos : s->pages[s->curpage].cursor_pos;
        if(z30_page_change(*s, 3, vbound(value / 10000, 0, (int)s->pages.size() - 1), true, pos) && shown)
            Bpos = pos;
        return true;
    }
    case Z30R_SUBDATATRANSCLK:
    case Z30R_SUBDATATRANSFROMPG:
    case Z30R_SUBDATATRANSTOPG:
    case Z30R_SUBDATATRANSTY:
        return z30_sub_unported(arg, true);
    case Z30R_SUBDATACURSORPOS:
    case Z30R_SUBPGCURSORPOS:
    {
        const bool page = arg == Z30R_SUBPGCURSORPOS;
        int sub, ty, pg = 0, ind;
        Z30Subscreen *s = z30_subdata(page ? Z30_REF(SUBSCREENPAGE) : Z30_REF(SUBSCREENDATA));
        Z30SubPage *p = page ? z30_subpage(Z30_REF(SUBSCREENPAGE)) : (s && !s->pages.empty() ? &s->pages[s->curpage] : NULL);
        if(page)
            z30_from_subref(Z30_REF(SUBSCREENPAGE), sub, ty, pg, ind);
        else if(s)
            pg = s->curpage;
        if(!p)
            return true;
        p->cursor_pos = vbound(value / 10000, 0, 255);
        if(pg == s->curpage && s->slot >= 0 && &custom_subscreen[s->slot] == current_subscreen_active)
            Bpos = p->cursor_pos;
        return true;
    }
    case Z30R_SUBDATATYPE:
    case Z30R_SUBDATAPAGES:
    case Z30R_SUBPGINDEX:
    case Z30R_SUBPGNUMWIDG:
    case Z30R_SUBPGWIDGETS:
    case Z30R_SUBPGSUBDATA:
        return true;    // read-only upstream too
    case Z30R_SUBDATANUMPG:
        return z30_sub_unported(arg, true);
    default:
        break;
    }

    subscreen_object *o = NULL;
    Z30SubWidget *w = NULL;

    switch(arg)
    {
    case Z30R_SUBWIDGTYPE: case Z30R_SUBWIDGINDEX: case Z30R_SUBWIDGPAGE: case Z30R_SUBWIDGEQPITM:
        return true;    // read-only
    case Z30R_SUBWIDGPOS:
    case Z30R_SUBWIDGPOSFLAG: case Z30R_SUBWIDGX: case Z30R_SUBWIDGY: case Z30R_SUBWIDGW: case Z30R_SUBWIDGH:
    case Z30R_SUBWIDGGENFLAG: case Z30R_SUBWIDGFLAG: case Z30R_SUBWIDGTY_TILE: case Z30R_SUBWIDGTY_CSET:
    case Z30R_SUBWIDGTY_CORNER: case Z30R_SUBWIDGTY_FLIP: case Z30R_SUBWIDGTY_FONT: case Z30R_SUBWIDGTY_ALIGN:
    case Z30R_SUBWIDGTY_SHADOWTY: case Z30R_SUBWIDGTY_COLOR_TXT: case Z30R_SUBWIDGTY_COLOR_SHD:
    case Z30R_SUBWIDGTY_COLOR_BG: case Z30R_SUBWIDGTY_COLOR_OLINE: case Z30R_SUBWIDGTY_COLOR_FILL:
    case Z30R_SUBWIDGTY_TABSIZE:
        w = z30_subwidg(Z30_REF(SUBSCREENWIDG), &o);
        if(!w)
            return true;
        break;
    default:
        return false;
    }

    const long idx = Z30_INDEX;
    const long v = value / 10000;

    switch(arg)
    {
    case Z30R_SUBWIDGPOS:
        w->pos = vbound(v, 0, 255);
        if(o && w->type == Z30W_ITEMSLOT && (w->genflags & 1))
            o->d3 = w->pos;
        return true;
    case Z30R_SUBWIDGPOSFLAG:
        if(idx >= 0 && idx < 3)
        {
            if(value) w->posflags |= 1 << idx;
            else w->posflags &= ~(1 << idx);
            if(o) o->pos = (byte)(w->posflags & 7);
        }
        return true;
    case Z30R_SUBWIDGX: w->x = vbound(v, -32768, 32767); if(o) o->x = (word)(short)w->x; return true;
    case Z30R_SUBWIDGY: w->y = vbound(v, -32768, 32767); if(o) o->y = (word)(short)w->y; return true;
    case Z30R_SUBWIDGW: w->w = vbound(v, 0, 65535); if(o) o->w = (word)w->w; return true;
    case Z30R_SUBWIDGH: w->h = vbound(v, 0, 65535); if(o) o->h = (word)w->h; return true;
    case Z30R_SUBWIDGGENFLAG:
        if(idx >= 0 && idx < 32)
        {
            if(value) w->genflags |= 1u << idx;
            else w->genflags &= ~(1u << idx);
        }
        return true;
    case Z30R_SUBWIDGFLAG:
        if(idx >= 0 && idx < 32)
        {
            if(value) w->flags |= 1u << idx;
            else w->flags &= ~(1u << idx);
            if(o) z30_sub_apply_flags(*w, *o);
        }
        return true;

    case Z30R_SUBWIDGTY_TILE:
    case Z30R_SUBWIDGTY_CORNER:
    case Z30R_SUBWIDGTY_CSET:
    {
        int *field;
        byte *ctype;
        short *cval;
        bool gauge;
        const int what = arg == Z30R_SUBWIDGTY_TILE ? 0 : arg == Z30R_SUBWIDGTY_CORNER ? 1 : 2;

        if(!o)
            return z30_sub_unported(arg, true);
        if(!z30_sub_tile_field(*w, *o, what, idx, &field, &ctype, &cval, &gauge))
            return true;

        if(what == 2)
        {
            const long cs = vbound(v, -sscsMAX, 15);
            if(gauge)
            {
                if(ctype) *ctype = (byte)vbound(cs, 0, 15);
                else *cval = (short)vbound(cs, 0, 15);
            }
            else if(cs >= 0)
            {
                *ctype = (byte)cs;
                *cval = 0;
            }
            else
            {
                *ctype = ssctMISC;
                *cval = (short)(-cs - 1);
            }
            return true;
        }

        if(what == 1)
        {
            const int crn = vbound(v, 0, 3);
            if(w->type == Z30W_MINITILE && *field == -1)
                o->d3 = crn;
            else
                *field = (*field & ~3) | crn;
            return true;
        }

        if(w->type == Z30W_MINITILE)
        {
            const long t = vbound(v, -7, NEWMAXTILES - 1);     // -ssmstMAX: upstream's special tiles
            const int crn = *field == -1 ? o->d3 : (*field & 3);
            if(t < 0)
            {
                *field = -1;
                o->d2 = -t - 1;
                o->d3 = crn;
            }
            else
                *field = (int)(t << 2) | crn;
        }
        else if(gauge)
            *field = (int)(vbound(v, 0, NEWMAXTILES - 1) << 2) | (*field & 3);
        else
            *field = vbound(v, 0, NEWMAXTILES - 1);
        return true;
    }

    case Z30R_SUBWIDGTY_FLIP:
        if(!o)
            return z30_sub_unported(arg, true);
        switch(w->type)
        {
        case Z30W_TILEBLOCK: case Z30W_MCGUFF: o->d2 = vbound(v, 0, 3); break;
        case Z30W_MINITILE: o->d4 = vbound(v, 0, 3); break;
        }
        return true;

    case Z30R_SUBWIDGTY_FONT:
    case Z30R_SUBWIDGTY_ALIGN:
    case Z30R_SUBWIDGTY_SHADOWTY:
        if(!z30_sub_is_textual(w->type))
            return true;
        if(!o)
            return z30_sub_unported(arg, true);
        if(arg == Z30R_SUBWIDGTY_FONT)
        {
            const int ssf = z30_sub_font_to_ssf(v);
            if(ssf < 0)
                return z30_sub_unported(arg, true);     // a font 2.53's subscreens have no number for
            o->d1 = ssf;
        }
        else if(arg == Z30R_SUBWIDGTY_ALIGN)
        {
            if(w->type != Z30W_OLDCTR)
                o->d2 = vbound(v, 0, 2);
        }
        else
            o->d3 = vbound(v, 0, sstsMAX - 1);
        return true;

    case Z30R_SUBWIDGTY_TABSIZE:
        if(w->type != Z30W_TEXTBOX && w->type != Z30W_SELECTEDTEXT)
            return true;
        if(!o)
            return z30_sub_unported(arg, true);
        o->d5 = vbound(v, 0, 255);
        return true;

    case Z30R_SUBWIDGTY_COLOR_TXT: case Z30R_SUBWIDGTY_COLOR_SHD: case Z30R_SUBWIDGTY_COLOR_BG:
    case Z30R_SUBWIDGTY_COLOR_OLINE: case Z30R_SUBWIDGTY_COLOR_FILL:
    {
        const int slot = z30_sub_color_slot(w->type, arg);
        if(slot < 0)
            return true;
        if(!o)
            return z30_sub_unported(arg, true);
        byte *ct;
        short *cv;
        z30_sub_color_ref(o, slot, ct, cv);
        z30_sub_set_color(*ct, *cv, v);
        return true;
    }
    }

    return false;
}

// ---- commands ----------------------------------------------------------------------------------

static bool zasm30_sub_command(word cmd)
{
    switch(cmd)
    {
    // Game->LoadSubscreenData(id, type): id -1 is the current one of that type.
    case Z30C_LOADSUBDATARV:
    {
        const long id = get_register(sarg1) / 10000;
        const long ty = sarg2 / 10000;
        long ref = 0;

        if(ty >= 0 && ty < 4)
        {
            const long sub = id == -1 ? z30_sub_current((int)ty) : id;

            if(sub >= 0 && sub < (long)zasm30_subs[ty].size() && sub < 256)
                ref = z30_subref((int)sub, (int)ty, 0, 0);
            else if(id != -1)
                Z_scripterrlog("Invalid Subscreen ID passed to Game->LoadSubData(): %ld\n", id);
        }
        else
            Z_scripterrlog("Invalid Subscreen Type passed to ???: %ld\n", ty);

        Z30_REF(SUBSCREENDATA) = ref;
        ri->d[2] = ref;
        return true;
    }

    case Z30C_SUBDATA_GET_NAME:
    case Z30C_SUBDATA_SET_NAME:
    {
        Z30Subscreen *s = z30_subdata(Z30_REF(SUBSCREENDATA));
        if(!s)
            return true;
        const long ptr = get_register(sarg1) / 10000;
        if(cmd == Z30C_SUBDATA_GET_NAME)
            z30_set_string(ptr, s->name);
        else
        {
            ArrayH::getString(ptr, s->name);
            if(s->slot >= 0)
            {
                strncpy(custom_subscreen[s->slot].name, s->name.c_str(), 63);
                custom_subscreen[s->slot].name[63] = 0;
            }
        }
        return true;
    }

    case Z30C_SUBPAGE_FIND_WIDGET_BY_LABEL:
    case Z30C_SUBPAGE_FIND_WIDGET:
    {
        ri->d[2] = 0;
        const long pref = SH::read_stack(SPR + 1);
        Z30_REF(SUBSCREENPAGE) = pref;
        Z30SubPage *p = z30_subpage(pref);
        int sub, ty, pg, ind;

        if(!p || !z30_from_subref(pref, sub, ty, pg, ind))
            return true;

        int found = -1;

        if(cmd == Z30C_SUBPAGE_FIND_WIDGET_BY_LABEL)
        {
            std::string lbl;
            ArrayH::getString(SH::read_stack(SPR) / 10000, lbl);

            for(size_t q = 0; lbl.size() && q < p->widgets.size() && found < 0; ++q)
                if(p->widgets[q].label == lbl)
                    found = (int)q;

            static const bool log = getenv("ZC_Q30_SUBLOG") != NULL;
            static std::set<std::string> seen;
            if(log && seen.insert(lbl).second)
                al_trace("q30 sublog: label '%s' on list %d sub %d page %d (%d widgets) -> widget %d\n", lbl.c_str(), ty, sub, pg,
                         (int)p->widgets.size(), found);
        }
        else
        {
            // upstream: active and map subscreens only, the selectable widget at that cursor position
            const int pos = SH::read_stack(SPR) / 10000;

            if(ty == 0 || ty == 3)
                for(size_t q = 0; q < p->widgets.size() && found < 0; ++q)
                    if((p->widgets[q].genflags & 1) && p->widgets[q].pos == pos)
                        found = (int)q;
        }

        if(found >= 0)
            ri->d[2] = z30_subref(sub, ty, pg, found);
        return true;
    }

    case Z30C_SUBWIDG_TY_GETTEXT:
    case Z30C_SUBWIDG_TY_SETTEXT:
    {
        subscreen_object *o;
        Z30SubWidget *w = z30_subwidg(Z30_REF(SUBSCREENWIDG), &o);

        if(!w)
            return true;

        if(w->type != Z30W_TEXT && w->type != Z30W_TEXTBOX)
        {
            Z_scripterrlog("Widget type %d does not have this function!\n", w->type);
            return true;
        }

        if(!o || !z30_sub_has_text(o->type))
        {
            zasm30_log_once("command", cmd);    // a text widget this engine doesn't draw
            return true;
        }

        const long ptr = get_register(sarg1) / 10000;

        if(cmd == Z30C_SUBWIDG_TY_GETTEXT)
            z30_set_string(ptr, std::string(o->dp1 ? (const char *)o->dp1 : ""));
        else
        {
            std::string s;
            ArrayH::getString(ptr, s, 65535);
            char *copy = new char[s.size() + 1];
            memcpy(copy, s.c_str(), s.size() + 1);
            if(o->dp1)
                delete [] (char *)o->dp1;
            o->dp1 = copy;
        }
        return true;
    }

    case Z30C_SUBWIDG_GET_LABEL:
    case Z30C_SUBWIDG_SET_LABEL:
    {
        Z30SubWidget *w = z30_subwidg(Z30_REF(SUBSCREENWIDG));
        if(!w)
            return true;
        const long ptr = get_register(sarg1) / 10000;
        if(cmd == Z30C_SUBWIDG_GET_LABEL)
            z30_set_string(ptr, w->label);
        else
            ArrayH::getString(ptr, w->label);
        return true;
    }
    }

    return false;
}

#endif

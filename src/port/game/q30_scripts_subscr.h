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

#include <set>
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
    bool has_sel;                   // its selector override (SUBSCRFLAG_SELOVERRIDE)
    Q30SubSel sel;
    int script, script_btns;        // the generic script a press runs (generic_scrconfig) and its buttons
    long script_initd[8];
    int maxdigits;                  // a counter's max digits (its selector box)
    Q30SubTy ty;                    // its type values (SUBWIDGTY_ registers), as upstream's SW_* members
    Q30SubCond req;                 // all of its display conditions (SUBWIDGREQ_ registers)
    int dirs[4];                    // pos_up, pos_down, pos_left, pos_right
    int obj_end;                    // the objects [obj, obj_end) it became
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

struct Z30SubScrConfig { long script, initd[8]; };

struct Z30Subscreen
{
    int slot;               // custom_subscreen[] index, -1 if not kept
    std::string name;
    std::vector<Z30SubPage> pages;
    int btn_left, btn_right;    // page buttons (INT_BTN_* bits)
    Q30SubTrans tr_left, tr_right;  // their animations
    dword flags;                // upstream's SUBFLAG_ (0x1: no page wrap)
    int curpage;                // the page in custom_subscreen[slot]
    int def_btns[4];            // A, B, X, Y on a new game: pos << 8 | page (page 255: none)
    bool has_sel;               // its selector override (SUBFLAG_ACT_OVERRIDESEL, flag 0x2)
    Q30SubSel sel;
    Z30SubScrConfig scr;        // its own script and InitD (upstream scrconfig)
};

// active, passive, overlay, map: upstream's sstACTIVE..sstMAP
static std::vector<Z30Subscreen> zasm30_subs[4];
static bool z30_sub_any_cond = false;   // some widget has display conditions
static bool z30_sub_read_newer = false; // the subscreens were read in the new format (version 8+)

// ---- filled by the reader ----------------------------------------------------------------------

void q30_sub_clear()
{
    for(int i = 0; i < 4; ++i)
        zasm30_subs[i].clear();

    z30_sub_any_cond = false;
    z30_sub_read_newer = false;
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

void q30_sub_read_new_format()
{
    z30_sub_read_newer = true;
}

// A quest whose new-format subscreens have no active one (upstream's editor deletes the last
// one): upstream's dosubscr opens nothing then (no CURRENT_ACTIVE_SUBSCREEN). The reader put a
// placeholder in for 2.53's search, which the player must not open.
bool q30_sub_none_active()
{
    return z30_sub_read_newer && zasm30_subs[0].empty();
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
    for(int q = 0; q < 4; ++q)
        s.def_btns[q] = 255;
    s.has_sel = false;
    memset(&s.sel, 0, sizeof(s.sel));
    memset(&s.scr, 0, sizeof(s.scr));
    zasm30_subs[list].push_back(s);
}

void q30_sub_script(int list, int script, const long *initd)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty())
        return;

    Z30SubScrConfig &c = zasm30_subs[list].back().scr;
    c.script = script;
    memcpy(c.initd, initd, sizeof(c.initd));

    if(getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 subscreen: list %d sub %d script %d\n", list, (int)zasm30_subs[list].size() - 1, script);
}

void q30_sub_selector(int list, const Q30SubSel &sel)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty())
        return;

    zasm30_subs[list].back().has_sel = true;
    zasm30_subs[list].back().sel = sel;
}

void q30_sub_widget_script(int list, int script, int btns, const long *initd)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty() || zasm30_subs[list].back().pages.empty() ||
       zasm30_subs[list].back().pages.back().widgets.empty())
        return;

    Z30SubWidget &w = zasm30_subs[list].back().pages.back().widgets.back();
    w.script = script;
    w.script_btns = btns;
    memcpy(w.script_initd, initd, sizeof(w.script_initd));

    if(getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 subscreen: list %d widget (pos %d) generic script %d on buttons 0x%02x\n", list, w.pos, script, btns);
}

void q30_sub_widget_digits(int list, int maxdigits)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty() || zasm30_subs[list].back().pages.empty() ||
       zasm30_subs[list].back().pages.back().widgets.empty())
        return;

    zasm30_subs[list].back().pages.back().widgets.back().maxdigits = maxdigits;
}

void q30_sub_widget_sel(int list, const Q30SubSel &sel)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty() || zasm30_subs[list].back().pages.empty() ||
       zasm30_subs[list].back().pages.back().widgets.empty())
        return;

    Z30SubWidget &w = zasm30_subs[list].back().pages.back().widgets.back();
    w.has_sel = true;
    w.sel = sel;

    if(getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 subscreen: list %d widget (pos %d) selector override %d,%d %dx%d, A tile %ld %dx%d cset %d frames %d, B tile %ld %dx%d\n",
                 list, w.pos, sel.x, sel.y, sel.w, sel.h, sel.t[0].tile, sel.t[0].sw, sel.t[0].sh, sel.t[0].cset, sel.t[0].frames,
                 sel.t[1].tile, sel.t[1].sw, sel.t[1].sh);
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

void q30_sub_settings(int list, dword flags, int btn_left, int btn_right, const word *def_btns)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty())
        return;

    Z30Subscreen &s = zasm30_subs[list].back();
    s.flags = flags;
    s.btn_left = btn_left;
    s.btn_right = btn_right;

    for(int q = 0; q < 4; ++q)
        s.def_btns[q] = def_btns ? def_btns[q] : 255;

    if(getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 subscreen: list %d sub %d (slot %d) page buttons left 0x%02x right 0x%02x flags 0x%x, default buttons 0x%04x 0x%04x 0x%04x 0x%04x\n",
                 list, (int)zasm30_subs[list].size() - 1, s.slot, btn_left, btn_right, (unsigned)flags,
                 s.def_btns[0], s.def_btns[1], s.def_btns[2], s.def_btns[3]);
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
    wd.has_sel = false;
    memset(&wd.sel, 0, sizeof(wd.sel));
    wd.script = wd.script_btns = 0;
    wd.maxdigits = 0;
    memset(wd.script_initd, 0, sizeof(wd.script_initd));
    q30_sub_ty_defaults(wd.ty);
    wd.req = Q30SubCond();
    wd.req.genflags = genflags;
    wd.req.counter = -1;
    wd.req.litem_level = -1;
    wd.req.lstate_level = -1;
    memset(wd.dirs, 0, sizeof(wd.dirs));
    wd.obj_end = obj >= 0 ? obj + 1 : -1;
    zasm30_subs[list].back().pages.back().widgets.push_back(wd);

    if(pg_mode && getenv("ZC_Q30_SUBLOG"))     // diagnostics only
        al_trace("q30 subscreen: list %d sub %d page %d widget %d (pos %d): page buttons 0x%02x mode %d target %d\n", list,
                 (int)zasm30_subs[list].size() - 1, (int)zasm30_subs[list].back().pages.size() - 1,
                 (int)zasm30_subs[list].back().pages.back().widgets.size() - 1, pos, pg_btns, pg_mode, pg_targ);
}

// Upstream's member defaults (new_subscr.h), for what a widget's type doesn't read.
void q30_sub_ty_defaults(Q30SubTy &t)
{
    memset(&t, 0, sizeof(t));
    t.frames = 1;
    t.speed = 1;
    t.inf_item = -1;
    t.per_container = 1;
    t.total = 16;
    t.showdrain = -1;
    t.infitm = -1;
    t.iid = -1;
    t.litems = 1;   // 1 << li_mcguffin
}

void q30_sub_widget_more(int list, const Q30SubTy &ty, const Q30SubCond &req, bool disabled, const int *dirs, int first, int end)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty() || zasm30_subs[list].back().pages.empty() ||
       zasm30_subs[list].back().pages.back().widgets.empty())
        return;

    Z30SubWidget &w = zasm30_subs[list].back().pages.back().widgets.back();
    w.ty = ty;
    w.req = req;
    w.req.disabled = disabled;
    for(int q = 0; q < 4; ++q)
        w.dirs[q] = dirs[q];
    w.obj_end = end > first ? end : -1;
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

    if(c.disabled)  // script-disable condition
        return false;

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

// subscr.cpp's 2.55/3.0 minimap (upstream SW_MMap::draw): the flags of the minimap widget object i
// of subscreen g came from, and its room colours (visited, unvisited, background: type and colour
// each). False, nothing set, when there is none.
bool q30_sub_minimap_info(const subscreen_group *g, int i, dword *flags, byte *ctype, short *color)
{
    if(!g || i < 0 || !q30_active())
        return false;

    for(int l = 0; l < 4; ++l)
    {
        for(size_t k = 0; k < zasm30_subs[l].size(); ++k)
        {
            const Z30Subscreen &s = zasm30_subs[l][k];

            if(s.slot < 0 || s.slot >= MAXCUSTOMSUBSCREENS || &custom_subscreen[s.slot] != g)
                continue;

            if(s.curpage < 0 || s.curpage >= (int)s.pages.size())
                return false;

            const std::vector<Z30SubWidget> &ws = s.pages[s.curpage].widgets;

            for(size_t w = 0; w < ws.size(); ++w)
            {
                const Z30SubWidget &wd = ws[w];

                if(wd.type != Z30W_MMAP || wd.obj < 0 || i < wd.obj || i >= (wd.obj_end > wd.obj ? wd.obj_end : wd.obj + 1))
                    continue;

                const int which[3] = { Q30SubTy::C_VIS, Q30SubTy::C_UNVIS, Q30SubTy::C_RBG };
                *flags = wd.flags;

                for(int q = 0; q < 3; ++q)
                {
                    ctype[q] = wd.ty.ctype[which[q]];
                    color[q] = wd.ty.color[which[q]];
                }

                return true;
            }

            return false;
        }
    }

    return false;
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
// Both pages are drawn each frame (z30_anim_redraw), and the frame is made from those two
// pictures: a slide, or a pixellation by upstream's static dither pattern.

static struct Z30PageAnim
{
    bool on;
    int clk;
    Q30SubTrans tr;
    BITMAP *from, *to, *work;
    // with SUBSCR_TRANS_NOHIDESELECTOR the pages' selectors are drawn each frame on copies of the
    // pictures (which leave them out): the subscreen, the pages and the old page's cursor
    BITMAP *from_sel, *to_sel;
    const struct Z30Subscreen *sub;
    int from_pg, from_pos, to_pg;
} g_pg_anim = { false, 0, { 0, 0, 0, { 0, 0, 0, 0 } }, NULL, NULL, NULL, NULL, NULL, NULL, 0, 0, 0 };

static void z30_anim_selectors(BITMAP *dest, const Z30Subscreen &s, int pg, int pos);

enum { Z30_SUB_W = 256, Z30_SUB_H = 176 };

// The newer selector (q30_sub_draw_selector) isn't drawn: upstream hides it while a page change
// animates, unless the change has SUBSCR_TRANS_NOHIDESELECTOR.
static bool z30_sel_hidden = false;

// as the open subscreen draws (dosubscr: put_active_subscr with sspDOWN; the cursor is Bpos)
static void z30_sub_draw_page(BITMAP *dest, bool hide_selector = false)
{
    clear_bitmap(dest);
    bool q30_should_show_time();    // zc_subscr.cpp
    z30_sel_hidden = hide_selector;
    show_custom_subscreen(dest, &QMisc, current_subscreen_active, 0, 0, q30_should_show_time(), sspDOWN);
    z30_sel_hidden = false;
}

static bool z30_anim_dither(int x, int y, double perc)     // upstream dither_staticcheck
{
    const double s = sin((double)(x * double(x) + y * double(y)));
    const double c = cos(double(x) * y);
    return fabs(s - c) < perc * 2.0;
}

// Upstream draws both pages anew each frame of the change (subscrpg_animate), so their items go on
// animating and the time runs on: the shown page as it is, the old one with its own objects put in
// for its picture (and its cursor), then taken out again.
static void z30_anim_redraw(Z30PageAnim &a)
{
    Z30Subscreen *s = const_cast<Z30Subscreen *>(a.sub);

    if(!s || s->slot < 0 || s->slot >= MAXCUSTOMSUBSCREENS || &custom_subscreen[s->slot] != current_subscreen_active
       || s->curpage != a.to_pg || a.from_pg < 0 || a.from_pg >= (int)s->pages.size() || !s->pages[a.from_pg].stored)
        return;

    z30_sub_draw_page(a.to, true);

    subscreen_object *objs = custom_subscreen[s->slot].objects;
    z30_store_objects(s->pages[a.to_pg], objs, MAXSUBSCREENITEMS);
    z30_load_objects(s->pages[a.from_pg], objs);
    const int keep = Bpos;
    Bpos = a.from_pos;
    s->curpage = a.from_pg;     // (its widgets' conditions and texts go by the page shown)
    z30_sub_draw_page(a.from, true);
    s->curpage = a.to_pg;
    Bpos = keep;
    z30_load_objects(s->pages[a.to_pg], objs);
}

// While a page change animates, draws its frame in place of the active subscreen (put_active_subscr);
// false once it's over, or when none is under way.
bool q30_subscreen_anim_draw(BITMAP *dest, int dx, int dy)
{
    Z30PageAnim &a = g_pg_anim;

    if(!a.on || !a.from || !a.to || !a.work)
        return false;

    z30_anim_redraw(a);

    clear_bitmap(a.work);
    bool done = false;
    BITMAP *from = a.from, *to = a.to;

    if((a.tr.flags & 0x1) && a.sub)     // SUBSCR_TRANS_NOHIDESELECTOR: the selectors go on animating
    {
        if(!a.from_sel)
        {
            a.from_sel = create_bitmap_ex(8, Z30_SUB_W, Z30_SUB_H);
            a.to_sel = create_bitmap_ex(8, Z30_SUB_W, Z30_SUB_H);
        }

        if(a.from_sel && a.to_sel)
        {
            blit(a.from, a.from_sel, 0, 0, 0, 0, Z30_SUB_W, Z30_SUB_H);
            z30_anim_selectors(a.from_sel, *a.sub, a.from_pg, a.from_pos);
            blit(a.to, a.to_sel, 0, 0, 0, 0, Z30_SUB_W, Z30_SUB_H);
            z30_anim_selectors(a.to_sel, *a.sub, a.to_pg, Bpos);
            from = a.from_sel;
            to = a.to_sel;
        }
    }

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
            blit(from, a.work, 0, 0, x1, y1, Z30_SUB_W, Z30_SUB_H);
            blit(to, a.work, 0, 0, x2, y2, Z30_SUB_W, Z30_SUB_H);
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
                        a.work->line[y][x] = from->line[y][x];
                    if(!z30_anim_dither(x + ox, y + oy, perc) != !inv)
                        a.work->line[y][x] = to->line[y][x];
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

        // (the pictures leave the selectors out: upstream hides them while a change animates, or
        // with SUBSCR_TRANS_NOHIDESELECTOR draws them anew each frame, q30_subscreen_anim_draw)
        z30_sub_draw_page(g_pg_anim.from, true);
        g_pg_anim.sub = &s;
        g_pg_anim.from_pg = s.curpage;
        g_pg_anim.from_pos = pos;
        g_pg_anim.to_pg = pg;
    }

    subscreen_object *objs = custom_subscreen[s.slot].objects;
    s.pages[s.curpage].cursor_pos = pos;
    z30_store_objects(s.pages[s.curpage], objs, MAXSUBSCREENITEMS);
    z30_load_objects(s.pages[pg], objs);
    s.curpage = pg;
    pos = s.pages[pg].cursor_pos;

    if(animate && g_pg_anim.from)
    {
        z30_sub_draw_page(g_pg_anim.to, true);     // (pos is Bpos itself here: the dosubscr loop's)
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

extern int directItem;      // link.cpp: the item Bweapon chose directly, -1 for a class's
static const Z30SubWidget *z30_sel_widget(const Z30SubPage &p, int pos);
static const subscreen_object *z30_btn_objects(const Z30Subscreen &s, int pg, int &n);
static int z30_btn_item_of(const subscreen_object &o);

// upstream's dosubscr: the buttons of a press (INT_BTN_* bits) the selected widget w passes to its
// page change. None when it can't be used: an item slot holding no item with
// SUBSCR_CURITM_NO_INTER_WO_ITEM, or one with NO_INTER_WO_EQUIP (when a button besides B equips)
// that can't equip or holds no item; with NO_INTER_WO_EQUIP only those that would equip its item.
static int z30_widget_btns(const Z30Subscreen &s, const Z30SubWidget &w, int btns)
{
    enum { QR_SUBSCR_PRESS_TO_EQUIP = 417, QR_NO_BUTTON_VERIFY = 424 };
    enum { NONEQP = 0x2, NO_INTER_WO_ITEM = 0x10, NO_INTER_WO_EQUIP = 0x20, NO_UNEQUIP = 0x40 };

    if(w.type != Z30W_ITEMSLOT)
        return btns;

    int n;
    const subscreen_object *objs = z30_btn_objects(s, s.curpage, n);
    const int keep = directItem;
    const int id = (w.obj >= 0 && w.obj < n) ? z30_btn_item_of(objs[w.obj]) : 0;    // getItemVal (0: none)
    directItem = keep;

    const bool use_a = get_bit(quest_rules, qr_SELECTAWPN) != 0, use_x = q30_use_x_button(), use_y = q30_use_y_button();
    const bool b_only = !(use_a || use_x || use_y || q30_quest_rule(QR_SUBSCR_PRESS_TO_EQUIP));
    const bool can_equip = !(w.flags & NONEQP);
    const bool can_unequip = q30_quest_rule(QR_NO_BUTTON_VERIFY) && !(w.flags & NO_UNEQUIP);
    const bool must_equip = !b_only && (w.flags & NO_INTER_WO_EQUIP);

    if((w.flags & NO_INTER_WO_ITEM) && !id)
        return 0;

    if(must_equip && (!can_equip || !id))
        return 0;

    if(must_equip)
        btns &= ((!can_unequip || Bwpn != id) ? 0x02 : 0) | ((use_a && (!can_unequip || Awpn != id)) ? 0x01 : 0) |
                ((use_x && (!can_unequip || Xwpn != id)) ? 0x10 : 0) | ((use_y && (!can_unequip || Ywpn != id)) ? 0x20 : 0);

    return btns;
}

bool q30_msg_run_generic_frozen(int script, const long *initd, int count);    // q30_scripts_cmds.h

// upstream's subscreen loop, before an item is equipped: the selected widget's generic script, when a
// press it takes (z30_widget_btns) is one of its script buttons. It runs frozen, there and then.
// False when the script left the subscreen shown no longer what it was (upstream skips the rest
// of the press then).
bool q30_subscreen_widget_script(int btns, int pos)
{
    if(!btns || !q30_active())
        return true;

    Z30Subscreen *s = z30_shown_active();

    if(!s || s->curpage < 0 || s->curpage >= (int)s->pages.size())
        return true;

    const Z30SubWidget *w = z30_sel_widget(s->pages[s->curpage], pos);

    if(!w || !w->script || !(z30_widget_btns(*s, *w, btns) & w->script_btns))
        return true;

    long initd[8];
    memcpy(initd, w->script_initd, sizeof(initd));
    const subscreen_group *shown = current_subscreen_active;
    q30_msg_run_generic_frozen(w->script, initd, 8);
    return current_subscreen_active == shown;
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

    // the selected widget (get_sel_widg, of any kind), with the buttons a press on it passes on
    const Z30SubWidget *sel = z30_sel_widget(cur, pos);

    if(sel && sel->pg_mode && (z30_widget_btns(*s, *sel, btns) & sel->pg_btns))
        z30_page_change(*s, sel->pg_mode, sel->pg_targ, (sel->genflags & 0x2) != 0, pos, &sel->pg_tr);  // SUBSCRFLAG_PGGOTO_NOWRAP

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
// first page with qr_OLD_SUBSCR, where 2.53's cursor from the equipped item stands). With the
// newer subscreens the buttons never move it: it opens where it was left, on any page.
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
    else if(s->curpage != 0 || q30_subscreen_lr_are_buttons())
        pos = s->pages[s->curpage].cursor_pos;  // (newer subscreens: on any page; the buttons don't move it)
}

// ---- the item buttons' slots by page (2.55/3.0 with the newer subscreens) ---------------------
// Upstream keeps each item button as a slot of the active subscreen, pos << 8 | page (page 255:
// none), and checks and moves it on that page: SubscrPage::movepos_legacy and get_item_pos, and
// ZCSubscreen::get_item_pos and get_page_pos. A page other than the one in custom_subscreen[] is
// looked at through its kept objects. link.cpp keeps the buttons (q30_btn).

extern int directItem;      // link.cpp: the item Bweapon chose directly, -1 for a class's

// The quest's subscreens are the newer ones read into pages (not 2.55's older subscreen format).
bool q30_sub_new_format()
{
    return !zasm30_subs[0].empty();
}

int q30_btn_pages()
{
    const Z30Subscreen *s = z30_shown_active();
    return s ? (int)s->pages.size() : 0;
}

int q30_btn_curpage()
{
    const Z30Subscreen *s = z30_shown_active();
    return (s && s->curpage >= 0 && s->curpage < (int)s->pages.size()) ? s->curpage : 0;
}

// A new game's slot for button b (0-3: A, B, X, Y): the active subscreen's def_btns, none for a
// page it doesn't have (upstream's init_game).
int q30_btn_default(int b)
{
    const Z30Subscreen *s = z30_shown_active();

    if(!s || b < 0 || b >= 4 || (s->def_btns[b] & 0xFF) >= (int)s->pages.size())
        return 255;

    return s->def_btns[b] & 0xFFFF;
}

// Page pg's objects as they are now (n of them): custom_subscreen[]'s for the page in it, else
// its kept copy.
static const subscreen_object *z30_btn_objects(const Z30Subscreen &s, int pg, int &n)
{
    n = 0;

    if(pg == s.curpage && s.slot >= 0 && s.slot < MAXCUSTOMSUBSCREENS)
    {
        const subscreen_object *o = custom_subscreen[s.slot].objects;

        while(n < MAXSUBSCREENITEMS && o[n].type != ssoNULL)
            ++n;

        return o;
    }

    const Z30SubPage &p = s.pages[pg];
    n = (int)p.objs.size();
    return n ? &p.objs[0] : NULL;
}

// Upstream's widget tests for object i: an item slot (the reader's cursor stops for other
// selectable widgets have no class), selectable, shown by its display conditions, and
// SUBSCR_CURITM_NONEQP (its widget's flag 0x2).
static bool z30_btn_is_slot(const subscreen_object &o)
{
    return o.type == ssoCURRENTITEM && o.d1 != -1;
}

static bool z30_btn_selectable(const subscreen_object &o)
{
    return o.type == ssoCURRENTITEM && o.d3 >= 0;
}

static bool z30_btn_cond(const Z30SubPage &p, int i)
{
    if(!z30_sub_any_cond || i >= (int)p.obj_cond.size() || p.obj_cond[i] < 0 || p.obj_cond[i] >= (int)p.conds.size())
        return true;

    return z30_cond_ok(p.conds[p.obj_cond[i]]);
}

static bool z30_btn_noneqp(const Z30SubPage &p, int i)
{
    for(size_t k = 0; k < p.widgets.size(); ++k)
        if(p.widgets[k].obj == i && p.widgets[k].type == Z30W_ITEMSLOT)
            return (p.widgets[k].flags & 0x2) != 0;

    return false;
}

// The item a slot holds now (upstream's getItemVal), as Bweapon gives it (0 for none; directItem
// set): Bweapon is asked with the one object standing in for the active subscreen.
static subscreen_group z30_btn_tmp;

static int z30_btn_item_of(const subscreen_object &o)
{
    directItem = -1;

    if(!z30_btn_is_slot(o) || o.d3 < 0)
        return 0;

    subscreen_group *keep = current_subscreen_active;
    z30_btn_tmp.objects[0] = o;
    z30_btn_tmp.objects[0].dp1 = NULL;
    z30_btn_tmp.objects[1].type = ssoNULL;
    current_subscreen_active = &z30_btn_tmp;
    const int ret = Bweapon(o.d3);
    current_subscreen_active = keep;
    return ret;
}

// SubscrPage::get_widg_pos: the first selectable object at pos (item_only: not a non-equippable slot).
static int z30_btn_widg_pos(const Z30SubPage &p, const subscreen_object *objs, int n, int pos, bool item_only)
{
    for(int i = 0; i < n; ++i)
    {
        if(!z30_btn_selectable(objs[i]))
            continue;

        if(item_only && z30_btn_is_slot(objs[i]) && z30_btn_noneqp(p, i))
            continue;

        if(objs[i].d3 == pos)
            return i;
    }

    return -1;
}

// SubscrPage::get_item_pos on page pg.
static int z30_btn_item_at(const Z30Subscreen &s, int pg, int pos, bool item_only)
{
    directItem = -1;

    if(pg < 0 || pg >= (int)s.pages.size())
        return 0;

    int n;
    const subscreen_object *objs = z30_btn_objects(s, pg, n);
    const int w = z30_btn_widg_pos(s.pages[pg], objs, n, pos, item_only);
    return w < 0 ? 0 : z30_btn_item_of(objs[w]);
}

// SubscrPage::movepos_legacy on page index (dir SEL_*; startp and the forbidden fp words as
// pos << 8 | page).
static int z30_btn_movepos(const Z30Subscreen &s, int index, int dir, int startp, int fp, int fp2, int fp3,
                           bool equip_only, bool item_only)
{
    enum { QR_FREEFORM_SUBSCREEN_CURSOR = 416, QR_NO_BUTTON_VERIFY = 424 };
    const Z30SubPage &p = s.pages[index];
    int n;
    const subscreen_object *objs = z30_btn_objects(s, index, n);

    //what will be returned when all else fails.
    //don't return the forbiddenpos... no matter what -DD
    int failpos;
    const bool start_empty = (startp & 0xFF) == 0xFF;

    if(start_empty)
    {
        failpos = startp;
        startp = 0;
    }
    else if(startp == fp || startp == fp2 || startp == fp3)
        failpos = 0xFF;
    else
        failpos = startp;

    item_only = item_only || !q30_quest_rule(QR_FREEFORM_SUBSCREEN_CURSOR);
    const bool verify = dir == SEL_VERIFY_RIGHT || dir == SEL_VERIFY_LEFT;

    if(verify)
    {
        equip_only = true;
        item_only = !q30_quest_rule(QR_NO_BUTTON_VERIFY);

        if(start_empty && !item_only)
            return startp;  //empty is valid

        if(startp != fp && startp != fp2 && startp != fp3)
        {
            const int w = z30_btn_widg_pos(p, objs, n, (startp >> 8) & 0xFF, item_only);

            if(w >= 0 && z30_btn_is_slot(objs[w]) && z30_btn_cond(p, w) && !z30_btn_noneqp(p, w) &&
               (!item_only || z30_btn_item_of(objs[w]) != 0))
                return startp;  //Valid selectable slot
        }
    }

    int at = -1, first_valid = -1, first_equip = -1;
    int curpos = (startp >> 8) & 0xFF;
    int cp2 = (curpos << 8) | index;

    for(int i = 0; i < n; ++i)
    {
        if(z30_btn_is_slot(objs[i]) && z30_btn_selectable(objs[i]) && z30_btn_cond(p, i))
        {
            if(first_valid == -1)
                first_valid = i;

            if(first_equip == -1 && (!equip_only || !z30_btn_noneqp(p, i)))
                first_equip = i;

            if(objs[i].d3 == curpos)
                at = i;

            if(at > -1 && first_valid > -1 && first_equip > -1)
                break;
        }
    }

    if(at == -1)
    {
        //can't find the current position
        // Switch to a valid weapon if there is one; otherwise,
        // the selector can simply disappear
        if(first_equip >= 0)
            return ((objs[first_equip].d3 & 0xFF) << 8) | index;

        if(first_valid >= 0)
            return ((objs[first_valid].d3 & 0xFF) << 8) | index;

        return failpos;     //FAILURE
    }

    //remember we've been here
    std::set<int> old_positions;
    old_positions.insert(cp2);

    for(;;)
    {
        const subscreen_object &here = objs[at];

        switch(dir)
        {
        case SEL_LEFT:
        case SEL_VERIFY_LEFT:
            curpos = here.d6 & 0xFF;
            break;

        case SEL_RIGHT:
        case SEL_VERIFY_RIGHT:
            curpos = here.d7 & 0xFF;
            break;

        case SEL_DOWN:
            curpos = here.d5 & 0xFF;
            break;

        case SEL_UP:
            curpos = here.d4 & 0xFF;
            break;
        }

        cp2 = (curpos << 8) | index;
        at = z30_btn_widg_pos(p, objs, n, curpos, false);  //find our new position

        if(at < 0)
            return failpos;

        //if we've already been here, give up
        if(old_positions.find(cp2) != old_positions.end())
            return failpos;

        old_positions.insert(cp2);

        //Valid stop point?
        if(z30_btn_cond(p, at) && cp2 != fp && cp2 != fp2 && cp2 != fp3 &&
           (!equip_only || !z30_btn_is_slot(objs[at]) || !z30_btn_noneqp(p, at)) &&
           (!item_only || z30_btn_item_of(objs[at]) != 0))
            return cp2;
    }
}

// The forbidden slot f as upstream passes it for button slot w on page pgn: only a slot on the
// same page counts, any slot when w is empty.
static int z30_btn_forbid(int f, int pgn, bool empty)
{
    if((f & 0xFF) == 0xFF)
        return 255;

    return (empty || (f & 0xFF) == pgn) ? f : 255;
}

// hero.cpp's verifyWpn (dir SEL_VERIFY_RIGHT), selectNextBTNWpn (any dir) and deselectbombsWPN
// (SEL_VERIFY_LEFT, deselect) for a button at slot w, the other buttons at f1-f3: false when
// upstream leaves the button's item as it is (no active subscreen; an empty button under
// qr_NO_BUTTON_VERIFY; a page the subscreen doesn't have, which deselect empties); otherwise w
// is the new slot and item what it holds (Bweapon's form, 0 for none; directItem set).
bool q30_btn_move(int dir, int &w, int f1, int f2, int f3, int &item, bool deselect)
{
    enum { QR_NO_BUTTON_VERIFY = 424 };
    const Z30Subscreen *s = z30_shown_active();

    if(!s)
        return false;

    const int pgn = w & 0xFF;
    const bool empty = pgn == 0xFF;

    if(empty && q30_quest_rule(QR_NO_BUTTON_VERIFY))
        return false;   //intentional nothingness

    const int pg = empty ? q30_btn_curpage() : pgn;

    if(pg >= (int)s->pages.size())
    {
        if(deselect)
            w = 255;    //set to nothingness

        return false;
    }

    w = z30_btn_movepos(*s, pg, dir, w, z30_btn_forbid(f1, pgn, empty), z30_btn_forbid(f2, pgn, empty),
                        z30_btn_forbid(f3, pgn, empty), true, true) & 0xFFFF;

    if((w & 0xFF) == 0xFF)
    {
        directItem = -1;
        item = 0;
    }
    else
        item = z30_btn_item_at(*s, pg, (w >> 8) & 0xFF, true);     // (the page looked on, as upstream)

    return true;
}

// ZCSubscreen::get_item_pos: what slot w holds now (0 for none; directItem set).
int q30_btn_item(int w)
{
    const Z30Subscreen *s = z30_shown_active();
    directItem = -1;

    if(!s || (w & 0xFF) >= (int)s->pages.size())
        return 0;

    return z30_btn_item_at(*s, w & 0xFF, (w >> 8) & 0xFF, false);
}

// The subscreen loop (dosubscr): the flags of the item slot under the cursor (pos, on the page
// shown), 0 if it isn't one. Upstream's can_equip is false with SUBSCR_CURITM_NONEQP (0x2), and
// its can_unequip (qr_NO_BUTTON_VERIFY) with SUBSCR_CURITM_NO_UNEQUIP (0x40).
dword q30_btn_slot_flags_at(int pos)
{
    const Z30Subscreen *s = z30_shown_active();

    if(!s || s->curpage < 0 || s->curpage >= (int)s->pages.size())
        return 0;

    const Z30SubPage &p = s->pages[s->curpage];
    int n;
    const subscreen_object *objs = z30_btn_objects(*s, s->curpage, n);
    const int w = z30_btn_widg_pos(p, objs, n, pos, false);

    if(w < 0 || !z30_btn_is_slot(objs[w]))
        return 0;

    for(size_t k = 0; k < p.widgets.size(); ++k)
        if(p.widgets[k].obj == w && p.widgets[k].type == Z30W_ITEMSLOT)
            return p.widgets[k].flags;

    return 0;
}

// ZCSubscreen::get_page_pos: the first slot, page by page, holding item id; 255 for none.
int q30_btn_find_item(int id)
{
    const Z30Subscreen *s = z30_shown_active();

    for(int pg = 0; s && pg < (int)s->pages.size(); ++pg)
    {
        int n;
        const subscreen_object *objs = z30_btn_objects(*s, pg, n);

        for(int i = 0; i < n; ++i)
        {
            if(z30_btn_is_slot(objs[i]) && z30_btn_item_of(objs[i]) == id)
                return ((objs[i].d3 & 0xFF) << 8) | pg;
        }
    }

    return 255;
}

// ---- the subscreen cursor with the newer subscreens (upstream SubscrPage) --------------------
// The cursor stands on any selectable widget: an item slot, or another widget's stop (the reader
// adds one, an item slot of no class, after each selectable widget that isn't a slot).

enum { Z30_QR_SUBSCR_OLD_SELECTOR = 271, Z30_QR_FREEFORM_SUBSCREEN_CURSOR = 416,
       Z30_QR_SUBSCR_SELECTOR_USES_ITEM_HITBOX = 571, Z30_QR_NO_L_R_BUTTON_INVENTORY_SWAP = 172 };

// SubscrPage::move_cursor, for the directions (dir SEL_*): with qr_FREEFORM_SUBSCREEN_CURSOR any
// selectable widget is a stop, otherwise only a slot holding an item; a position whose only widget
// is a slot that can't be equipped stops the move there (get_widg_pos skips it). false when the
// subscreen shown isn't a newer one (2.53's selectWpn_new moves the cursor).
bool q30_sub_move_cursor(int dir, int &pos)
{
    Z30Subscreen *s = z30_shown_active();

    if(!s || s->curpage < 0 || s->curpage >= (int)s->pages.size())
        return false;

    const Z30SubPage &p = s->pages[s->curpage];
    int n;
    const subscreen_object *objs = z30_btn_objects(*s, s->curpage, n);

    if(pos == 0xFF)     // verify startpos
        pos = 0;

    const bool item_only = !q30_quest_rule(Z30_QR_FREEFORM_SUBSCREEN_CURSOR);

    if(dir == SEL_VERIFY_RIGHT || dir == SEL_VERIFY_LEFT)
        return true;

    int at = -1, first = -1;

    for(int i = 0; i < n; ++i)
    {
        if(z30_btn_selectable(objs[i]) && z30_btn_cond(p, i))
        {
            if(first == -1 && objs[i].d3 >= 0)
                first = i;

            if(objs[i].d3 == pos)
                at = i;

            if(at > -1 && first > -1)
                break;
        }
    }

    if(at == -1)
    {
        if(first >= 0)
            pos = objs[first].d3;

        return true;
    }

    //remember we've been here
    std::set<int> old_positions;
    int cur = pos;
    old_positions.insert(cur);

    for(;;)
    {
        switch(dir)
        {
        case SEL_LEFT:  cur = objs[at].d6; break;
        case SEL_RIGHT: cur = objs[at].d7; break;
        case SEL_DOWN:  cur = objs[at].d5; break;
        case SEL_UP:    cur = objs[at].d4; break;
        }

        at = z30_btn_widg_pos(p, objs, n, cur, item_only);     //find our new position

        if(at < 0)
            return true;

        //if we've already been here, give up
        if(old_positions.find(cur) != old_positions.end())
            return true;

        old_positions.insert(cur);

        //Valid stop point?
        if((!item_only || z30_btn_item_of(objs[at]) != 0) && z30_btn_cond(p, at))
        {
            pos = cur;
            return true;
        }
    }
}

// qr_OLD_SUBSCR with the newer subscreens: upstream moves the item buttons by movepos_legacy on the
// shown page of the current active subscreen (init_game, verifyWpn, selectNextBTNWpn). Positions
// here are slot positions on that page, 0xFF for none (2.53's save keeps no page).
// Whether there is such a page (upstream's new_subscreen_active).
bool q30_old_btn_page()
{
    const Z30Subscreen *s = z30_shown_active();
    return s && s->curpage >= 0 && s->curpage < (int)s->pages.size();
}

// movepos_legacy (dir SEL_*) from start, f1-f3 forbidden: returns a position, 0xFF for none. As
// upstream's, an empty start under qr_NO_BUTTON_VERIFY gives slot 0 ("empty is valid" returns the
// start it set to 0).
int q30_old_btn_movepos(int dir, int start, int f1, int f2, int f3)
{
    Z30Subscreen *s = z30_shown_active();

    if(!q30_old_btn_page())
        return 0xFF;

    const int pg = s->curpage;
    const int w[4] = { start, f1, f2, f3 };
    int ws[4];

    for(int i = 0; i < 4; ++i)
        ws[i] = (w[i] < 0 || w[i] >= 0xFF) ? 0xFF : ((w[i] << 8) | pg);

    const int r = z30_btn_movepos(*s, pg, dir, ws[0], ws[1], ws[2], ws[3], true, true);
    return (r & 0xFF) == 0xFF ? 0xFF : (r >> 8) & 0xFF;
}

// get_item_pos(pos) on that page: the item the slot holds (0 for none), directItem set.
int q30_old_btn_item(int pos)
{
    directItem = -1;

    if(!q30_old_btn_page() || pos < 0 || pos >= 0xFF)
        return 0;

    const Z30Subscreen *s = z30_shown_active();
    return z30_btn_item_at(*s, s->curpage, pos, true);
}

// Whether the current dmap has an active subscreen as upstream sees it (update_subscreens: its
// index within the quest's active subscreens; else new_subscreen_active is null). 2.53's search
// here falls back to the last one.
bool q30_dmap_has_active_subscreen()
{
    const int idx = DMaps[get_currdmap()].active_subscreen;
    return idx >= 0 && idx < (int)zasm30_subs[0].size();
}

// With qr_OLD_SUBSCR, L and R move the cursor as upstream's movepos_legacy (items only, on this
// page), unless qr_NO_L_R_BUTTON_INVENTORY_SWAP; false when the subscreen isn't a newer one.
bool q30_sub_move_legacy(int dir, int &pos)
{
    Z30Subscreen *s = z30_shown_active();

    if(!s || s->curpage < 0 || s->curpage >= (int)s->pages.size())
        return false;

    if(!q30_quest_rule(Z30_QR_NO_L_R_BUTTON_INVENTORY_SWAP))
        pos = (z30_btn_movepos(*s, s->curpage, dir, ((pos & 0xFF) << 8) | s->curpage, 255, 255, 255, false, true) >> 8) & 0xFF;

    return true;
}

// The selected widget (SubscrPage::get_sel_widg: the first selectable widget at pos, whatever it
// shows or its conditions say), NULL for none.
static const Z30SubWidget *z30_sel_widget(const Z30SubPage &p, int pos)
{
    for(size_t i = 0; i < p.widgets.size(); ++i)
        if((p.widgets[i].genflags & 1) && p.widgets[i].pos == pos)     // SUBSCRFLAG_SELECTABLE
            return &p.widgets[i];

    return NULL;
}

static int z30_shadow_x(int s)
{
    switch(s)
    {
    case sstsSHADOWU: case sstsOUTLINE8: case sstsOUTLINEPLUS: case sstsOUTLINEX:
    case sstsSHADOWEDU: case sstsOUTLINED8: case sstsOUTLINEDPLUS: case sstsOUTLINEDX:
        return -1;
    }

    return 0;
}

static int z30_shadow_y(int s)
{
    switch(s)
    {
    case sstsOUTLINE8: case sstsOUTLINEPLUS: case sstsOUTLINEX:
    case sstsOUTLINED8: case sstsOUTLINEDPLUS: case sstsOUTLINEDX:
        return -1;
    }

    return 0;
}

static int z30_shadow_wh(int s)     // upstream's shadow_w and shadow_h: 1 for any shadow or outline
{
    return (s > sstsNORMAL && s < sstsMAX) ? 1 : 0;
}

// The widget's box as the selector sees it (upstream's getX, getY, getW and getH), from what it
// was read with and the object it became (o, NULL if none; scripts move a widget through it).
// A type with no object to measure (or one 2.53 can't draw) keeps the widget's own size.
static void z30_widget_box(const Z30SubWidget &w, const subscreen_object *o, int &x, int &y, int &bw, int &bh)
{
    x = o ? (short)o->x : w.x;
    y = o ? (short)o->y : w.y;
    bw = w.w;
    bh = w.h;

    switch(w.type)
    {
    case Z30W_FRAME:
        bw = w.w * 8;
        bh = w.h * 8;
        break;

    case Z30W_TEXT:
        if(o && o->type == ssoTEXT)
        {
            FONT *f = ss_font(o->d1);
            const int len = text_length(f, o->dp1 ? (const char *)o->dp1 : "");
            bw = len == 0 ? 8 : len;
            bh = text_height(f);
            x += z30_shadow_x(o->d3);
            y += z30_shadow_y(o->d3);

            if(o->d2 == sstaCENTER)
                x -= bw / 2;
            else if(o->d2 == sstaRIGHT)
                x -= bw;
        }
        break;

    case Z30W_TIME:
        if(o && (o->type == ssoBSTIME || o->type == ssoSSTIME || o->type == ssoTIME))
        {
            FONT *f = ss_font(o->d1);
            const dword tm = game ? game->get_time() : 0;
            bw = text_length(f, (w.flags & 0x1) ? time_str_short2(tm) : time_str_med(tm)) + z30_shadow_wh(o->d3);    // SUBSCR_TIME_ALTSTR
            bh = text_height(f) + z30_shadow_wh(o->d3);
            x += z30_shadow_x(o->d3);
            y += z30_shadow_y(o->d3);

            if(o->d2 == sstaCENTER)
                x -= bw / 2;
            else if(o->d2 == sstaRIGHT)
                x -= bw;
        }
        break;

    case Z30W_COUNTER:
        if(o && o->type == ssoCOUNTER)
        {
            FONT *f = ss_font(o->d1);
            int digitw = text_length(f, "0") * (w.maxdigits > 0 ? w.maxdigits : zc_max(1, o->d4));

            if(w.flags & 0x8)   // SUBSCR_COUNTER_FRACTION
                digitw = digitw * 2 + text_length(f, "/");

            bw = digitw + z30_shadow_wh(o->d3);
            bh = text_height(f) + z30_shadow_wh(o->d3);
            x += z30_shadow_x(o->d3);
            y += z30_shadow_y(o->d3);

            if(o->d2 == sstaCENTER)
                x -= bw / 2;
            else if(o->d2 == sstaRIGHT)
                x -= bw;
        }
        break;

    case Z30W_OLDCTR:
        if(o && o->type == ssoCOUNTERS)
        {
            bw = 32 + z30_shadow_wh(o->d3);
            bh = 32 + z30_shadow_wh(o->d3);
            x += z30_shadow_x(o->d3);
            y += z30_shadow_y(o->d3);
        }
        break;

    case Z30W_LGAUGE:
    case Z30W_MGAUGE:
        if(o && (o->type == ssoLIFEGAUGE || o->type == ssoMAGICGAUGE) && (o->d10 & SSO_GAUGE_NEWER))
        {
            // the grid as the reader keeps it: columns - 1, rows - 1, spacing, offsets
            const int sz = (w.flags & 0x20) ? 16 : 8;   // SUBSCR_GAUGE_FULLTILE
            const int wid = o->frames, hei = o->speed, xoff = o->color3, yoff = (short)o->frame;
            x += xoff < 0 ? hei * xoff : 0;
            y += yoff < 0 ? hei * yoff : 0;
            bw = (wid + 1) * sz + wid * o->delay + hei * abs(xoff);
            bh = (hei + 1) * sz + hei * o->colortype3 + wid * abs(yoff);
        }
        break;

    case Z30W_MMETER:
        x -= 10;
        bw = 82;
        bh = 8;
        break;

    case Z30W_LMETER:
    {
        const int rows = (o && o->d3) ? 3 : 2;

        if(!(w.flags & 0x1))    // SUBSCR_LIFEMET_BOT
            y += (4 - rows) * 8;

        bw = 64;
        bh = 8 * rows;
        break;
    }

    case Z30W_BTNITM:
    case Z30W_ITEMSLOT:
    case Z30W_MCGUFF:
        bw = bh = 16;
        break;

    case Z30W_MMAP:
        bw = 80;
        bh = 48;
        break;

    case Z30W_LMAP:
        bw = 16 * ((w.flags & 0x8) ? 9 : 7);   // SUBSCR_LMAP_LARGE
        bh = 80;
        break;

    case Z30W_MCGUFF_FRAME:
        bw = 16 * ((w.flags & 0x4) ? 7 : 6);   // SUBSCR_TRIFR_LGPC
        bh = 16 * ((w.flags & 0x4) ? 7 : 3);
        break;

    case Z30W_TILEBLOCK:
        bw = w.w * 16;
        bh = w.h * 16;
        break;

    case Z30W_MINITILE:
        bw = bh = 8;
        break;

    case Z30W_SELECTOR:
        bw = bh = (w.flags & 0x2) ? 32 : 16;   // SUBSCR_SELECTOR_LARGE
        break;
    }
}

// upstream's subscr_item_clk: counts the frames the passive subscreen is drawn (subscr.cpp)
int q30_subscr_item_clk = 0;

// upstream's dummyitem_animate followed by item::animate's frame step, for a selector drawn anew
// each frame: its frame from the subscreen clock.
static void z30_sel_animate(item &it, int clk)
{
    int fr = it.frames, spd = it.o_speed, del = it.o_delay;

    if(fr < 1)
        fr = 1;
    if(spd < 1)
        spd = 1;

    const int clkwid = spd * (fr + del);
    clk %= clkwid;
    clk = clk ? clk - 1 : clkwid - 1;   //To counter needing to call animate, calculate one clk back
    it.aframe = clk < (del * spd) ? 0 : (clk - del * spd) / spd;
    it.aclk = it.aframe ? (clk - del * spd) % spd : clk;

    q30_item_apply_size(&it, it.id, true);  // the selector item's own sizes, drawn in place

    if(it.flash)    // the item's flashing: its two csets, 8 frames each
    {
        it.cs = it.o_cset;

        if(frame & 8)
            it.cs >>= 4;
        else
            it.cs &= 15;
    }

    if(it.frames > 0)
    {
        int s = it.o_speed;

        if(it.aframe == 0)
            s *= it.o_delay + 1;

        if(++it.aclk >= s)
        {
            it.aclk = 0;

            if(++it.aframe >= it.frames)
                it.aframe = 0;
        }

        if(it.extend > 2)
        {
            if(it.o_tile / TILES_PER_ROW == (it.o_tile + it.txsz * it.aframe) / TILES_PER_ROW)
                it.tile = it.o_tile + it.txsz * it.aframe;
            else
                it.tile = it.o_tile + (it.txsz * it.aframe) + ((it.tysz - 1) * TILES_PER_ROW) *
                          (((it.o_tile + it.txsz * it.aframe) / TILES_PER_ROW) - (it.o_tile / TILES_PER_ROW));
        }
        else
            it.tile = it.o_tile + it.aframe;
    }
}

// Selector object o (2.53's ssoSELECTOR1/2) of page pg of subscreen s, whose objects are objs (n of
// them), with the cursor at pos, drawn as upstream's SW_Selector::draw: on the selected widget of
// any kind, at its box (or the item's hit box, with qr_SUBSCR_SELECTOR_USES_ITEM_HITBOX), the
// selector tile stretched to it; a widget's selector override, or the subscreen's, gives the tiles
// and the box. pos goes to 0 when nothing is selectable at it but something is there.
static void z30_draw_sel(BITMAP *dest, const Z30Subscreen &sub, int pg, int &pos, const subscreen_object *objs, int n,
                         const subscreen_object &o, int xofs, int yofs)
{
    const Z30Subscreen *s = &sub;
    const Z30SubPage &p = s->pages[pg];
    const Z30SubWidget *widg = z30_sel_widget(p, pos);

    if(!widg)
    {
        if(pos && (widg = z30_sel_widget(p, 0)) != NULL)
            pos = 0;

        if(!widg)
            return;
    }

    const int sel_type = (o.type == ssoSELECTOR2) ? 1 : 0;     // SUBSCR_SELECTOR_USEB
    const subscreen_object *wo = (widg->obj >= 0 && widg->obj < n && objs[widg->obj].type != ssoNULL) ? &objs[widg->obj] : NULL;
    bool big_sel = o.d5 != 0;
    const bool oldsel = q30_compat_rule(Z30_QR_SUBSCR_OLD_SELECTOR);
    const bool use_item_hitbox = q30_compat_rule(Z30_QR_SUBSCR_SELECTOR_USES_ITEM_HITBOX);

    item tempsel((fix)0, (fix)0, (fix)0, sel_type ? iSelectB : iSelectA, 0, 0, true);
    tempsel.xofs = tempsel.yofs = 0;
    tempsel.drawstyle = o.d4 ? 1 : 0;   // SUBSCR_SELECTOR_TRANSP

    // the item it holds (getItemVal) and that item's own hit box (overrideFLAGS: hit x/y offset,
    // width, height; 0, 0, 16, 16 for what they leave)
    int id = -1;

    if(wo && widg->type == Z30W_ITEMSLOT)
    {
        const int keep = directItem;
        const int it = z30_btn_item_of(*wo);
        directItem = keep;
        id = it ? it : -1;
    }

    struct { int hxofs, hyofs, hxsz, hysz; } ibox = { 0, 0, 16, 16 };

    if(id > -1)
        q30_item_hit_box(id, ibox.hxofs, ibox.hyofs, ibox.hxsz, ibox.hysz);

    if(!oldsel)
        big_sel = false;

    int sw = 16, sh = 16, dw = 16, dh = 16, sxofs = 0, syofs = 0, dxofs = 0, dyofs = 0;
    bool new_animate = true;
    int wx, wy, ww, wh;
    z30_widget_box(*widg, wo, wx, wy, ww, wh);

    if(widg->has_sel || s->has_sel)
    {
        const Q30SubSel &ss = widg->has_sel ? widg->sel : s->sel;
        sw = ss.t[sel_type].sw;
        sh = ss.t[sel_type].sh;

        if(widg->has_sel)
        {
            dw = ss.w;
            dh = ss.h;
            dxofs = ss.x;
            dyofs = ss.y;
        }
        else
        {
            if(use_item_hitbox && widg->type == Z30W_ITEMSLOT && id > -1)
            {
                dw = ibox.hxsz;
                dh = ibox.hysz;
                dxofs = widg->x + ibox.hxofs + (tempsel.extend > 2 ? (int)tempsel.xofs : 0);
                dyofs = widg->y + ibox.hyofs + (tempsel.extend > 2 ? (int)tempsel.yofs : 0);
            }
            else
            {
                dw = ww;
                dh = wh;
                dxofs = wx;
                dyofs = wy;
            }

            dw += ss.w;
            dh += ss.h;
            dxofs += ss.x;
            dyofs += ss.y;
        }

        big_sel = false;
        tempsel.tile = tempsel.o_tile = ss.t[sel_type].tile;
        tempsel.o_cset = ss.t[sel_type].cset;
        tempsel.cs = tempsel.o_cset & 0xF;
        tempsel.frames = ss.t[sel_type].frames;
        tempsel.o_speed = ss.t[sel_type].speed;
        tempsel.o_delay = ss.t[sel_type].delay;
        tempsel.extend = 3;
        tempsel.txsz = ((sw % 16) ? 1 : 0) + (sw / 16);
        tempsel.tysz = ((sh % 16) ? 1 : 0) + (sh / 16);
    }
    else
        new_animate = false;

    z30_sel_animate(tempsel, q30_subscr_item_clk);

    if(!tempsel.tile)
        return;

    if(!new_animate)
    {
        if(oldsel)
        {
            sw = dw = (tempsel.extend > 2 ? tempsel.txsz * 16 : 16);
            sh = dh = (tempsel.extend > 2 ? tempsel.tysz * 16 : 16);
            dxofs = widg->x + (tempsel.extend > 2 ? (int)tempsel.xofs : 0);
            dyofs = widg->y + (tempsel.extend > 2 ? (int)tempsel.yofs : 0);
        }
        else
        {
            sw = (tempsel.extend > 2 ? tempsel.hxsz : 16);
            sh = (tempsel.extend > 2 ? tempsel.hysz : 16);
            sxofs = (tempsel.extend > 2 ? tempsel.hxofs : 0);
            syofs = (tempsel.extend > 2 ? tempsel.hyofs : 0);

            if(use_item_hitbox && widg->type == Z30W_ITEMSLOT && id > -1)
            {
                dw = ibox.hxsz;
                dh = ibox.hysz;
                dxofs = widg->x + ibox.hxofs + (tempsel.extend > 2 ? (int)tempsel.xofs : 0);
                dyofs = widg->y + ibox.hyofs + (tempsel.extend > 2 ? (int)tempsel.yofs : 0);
            }
            else
            {
                dw = ww;
                dh = wh;
                dxofs = wx;
                dyofs = wy;
            }
        }
    }

    if(sw <= 0 || sh <= 0)
        return;

    BITMAP *tmp = create_bitmap_ex(8, sw, sh);

    if(!tmp)
        return;

    const int sx = vbound(sxofs, 0, sw), sy = vbound(syofs, 0, sh);

    for(int j = 0; j < 4; ++j)
    {
        clear_bitmap(tmp);
        tempsel.x = 0;
        tempsel.y = 0;
        tempsel.draw(tmp);

        const int tx = xofs + (big_sel ? (j % 2 ? 8 : -8) : 0);
        const int ty = yofs + (big_sel ? (j > 1 ? 8 : -8) : 0);

        if(sw - sx > 0 && sh - sy > 0 && dw > 0 && dh > 0)
            masked_stretch_blit(tmp, dest, sx, sy, sw - sx, sh - sy, tx + dxofs, ty + dyofs, dw, dh);

        if(!big_sel)
            break;

        tempsel.tile += zc_max(itemsbuf[tempsel.id].frames, 1);
    }

    destroy_bitmap(tmp);
}

// The 2.55/3.0 active or map subscreen that is custom_subscreen[] entry css, if any.
static Z30Subscreen *z30_sub_of_group(const subscreen_group *css)
{
    static const int lists[2] = { 0, 3 };

    for(int l = 0; l < 2; ++l)
        for(size_t i = 0; i < zasm30_subs[lists[l]].size(); ++i)
        {
            Z30Subscreen &s = zasm30_subs[lists[l]][i];

            if(s.slot >= 0 && s.slot < MAXCUSTOMSUBSCREENS && &custom_subscreen[s.slot] == css)
                return &s;
        }

    return NULL;
}

// subscr.cpp's selector object o on a newer subscreen (z30_draw_sel, the cursor Bpos); false when
// css isn't one (2.53's drawing then).
bool q30_sub_draw_selector(BITMAP *dest, const subscreen_group *css, const subscreen_object &o, int xofs, int yofs)
{
    if(!q30_active() || !css)
        return false;

    Z30Subscreen *s = z30_sub_of_group(css);

    if(!s || s->curpage < 0 || s->curpage >= (int)s->pages.size())
        return false;

    if(!z30_sel_hidden)
    {
        int n = 0;

        while(n < MAXSUBSCREENITEMS && css->objects[n].type != ssoNULL)
            ++n;

        z30_draw_sel(dest, *s, s->curpage, Bpos, css->objects, n, o, xofs, yofs);
    }

    return true;
}

// A page change animating with SUBSCR_TRANS_NOHIDESELECTOR: upstream draws both pages anew each
// frame, so their selectors go on animating. The pages' pictures leave them out; this puts page
// pg's selectors (cursor pos) on such a picture.
static void z30_anim_selectors(BITMAP *dest, const Z30Subscreen &s, int pg, int pos)
{
    if(pg < 0 || pg >= (int)s.pages.size())
        return;

    int n;
    const subscreen_object *objs = z30_btn_objects(s, pg, n);

    for(int i = 0; i < n; ++i)
    {
        const subscreen_object &o = objs[i];

        if((o.type == ssoSELECTOR1 || o.type == ssoSELECTOR2) && (o.pos & sspDOWN) && z30_btn_cond(s.pages[pg], i))
            z30_draw_sel(dest, s, pg, pos, objs, n, o, 0, 0);
    }
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
static void z30_engsub_map_open(subscreen_group *dmap_active);    // (below)
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
        z30_engsub_map_open(was);
        dosubscr(&QMisc);
        z30_engsub_map_open(NULL);
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

    // ZC_Q30_SUBDUMP_PAGE=n: page n (from 0) of the active subscreen instead of the one it opens at
    if(const char *pg = getenv("ZC_Q30_SUBDUMP_PAGE"))
    {
        if(Z30Subscreen *s = z30_shown_active())
        {
            int pos = 0;
            z30_page_change(*s, 3, atoi(pg), true, pos);
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

// upstream's check_bomb / check_sbomb (new_subscr.cpp) for an item slot naming a bomb item, or
// with iid -1 one naming the bomb or super bomb class; ZC_Q30_BOMBSLOT=1 logs each change of
// what it decides for an item (or class)
static bool z30_sub_check_bomb(int iid, bool super)
{
    const bool shown = z30_sub_check_bomb_inner(iid, super);

    static const bool log = getenv("ZC_Q30_BOMBSLOT") != NULL;
    static std::map<int, bool> last;
    const int key = iid >= 0 ? iid : (super ? -2 : -1);

    if(log && (last.find(key) == last.end() || last[key] != shown))
    {
        last[key] = shown;
        al_trace("q30 bomb slot: item %d (%s) %s: rule 433 %d (compat %d), rule 161 %d, bombs %d, super bombs %d, cost counters %d %d (payable %d)\n",
                 iid, super ? "super" : "bomb", shown ? "shown" : "hidden", q30_compat_rule(433) ? 1 : 0,
                 (int)q255_compat_rule_version, q30_quest_rule(161) ? 1 : 0, game->get_bombs(), game->get_sbombs(),
                 q30_item_cost_counter(iid, 0), q30_item_cost_counter(iid, 1), q30_item_costs_payable(iid) ? 1 : 0);
    }

    return shown;
}

// The selection (Bweapon, link.cpp) and the class slots' display (displaysubscreenitem, subscr.cpp)
// of a 2.55/3.0 quest ask this, as upstream's SW_ItemSlot getItemVal and getDisplayItem do.
bool q30_bomb_slot_usable(int iid, bool super)
{
    return z30_sub_check_bomb(iid, super);
}

// upstream's current_item_id(itype_bomb or itype_sbomb, true) > -1: Link has one of the class
// whose costs he can pay, any counter (2.53's checkmagiccost knows only magic and rupees)
static bool z30_sub_bomb_class_payable(bool super)
{
    const int fam = super ? itype_sbomb : itype_bomb;

    for(int i = 0; i < q30_items_end(); ++i)
    {
        if(itemsbuf[i].family == fam && game->get_item(i) && !item_disabled(i) && q30_item_costs_payable(i) &&
           q30_bunny_allows(i))
            return true;
    }

    return false;
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
    // and rupees. A class slot asks it of the class's items.
    if(q30_compat_rule(QR_BROKEN_BOMB_AMMO_COSTS) ? (super ? game->get_sbombs() : game->get_bombs()) != 0
       : (iid >= 0 ? q30_item_costs_payable(iid) : z30_sub_bomb_class_payable(super)))
        return true;

    // remote bombs: still usable with one out (a class slot: the best one Link has, costs not asked)
    const int remote = iid >= 0 ? iid : current_item_id(super ? itype_sbomb : itype_bomb, false);

    return remote >= 0 && itemsbuf[remote].misc1 == 0 && Lwpns.idCount(super ? wLitSBomb : wLitBomb) > 0;
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
    case Z30W_LGAUGE: case Z30W_MGAUGE:
        o.d10 = (o.d10 & SSO_GAUGE_NEWER) ? ((o.d10 & ~0xFFFF) | (int)(w.flags & 0xF3FF)) : (int)(w.flags & 0x1F);
        break;
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

// ---- type values, display conditions, cursor neighbours, display item --------------------------
// (upstream subscreenwidget.cpp: SUBWIDGTY_ANIMVAL...VSPACE, SUBWIDGREQ*, SUBWIDGPOSES, SUBWIDGDISPITM)

// The 2.53 object i of widget page p (custom_subscreen[] for the page shown), NULL if none.
static subscreen_object *z30_sub_page_obj(Z30Subscreen &s, Z30SubPage &p, int pg, int i)
{
    if(i < 0 || i >= MAXSUBSCREENITEMS || s.slot < 0 || s.slot >= MAXCUSTOMSUBSCREENS)
        return NULL;
    if(pg == s.curpage)
        return &custom_subscreen[s.slot].objects[i];
    if(p.stored && i < (int)p.objs.size())
        return &p.objs[i];
    return NULL;
}

// A script changed w's conditions: its objects follow them.
static void z30_sub_req_sync(Z30SubPage &p, Z30SubWidget &w)
{
    if(w.obj < 0 || w.obj_end <= w.obj)
        return;

    int c = (w.obj < (int)p.obj_cond.size()) ? p.obj_cond[w.obj] : -1;

    if(c < 0 || c >= (int)p.conds.size())
    {
        p.conds.push_back(w.req);
        c = (int)p.conds.size() - 1;
    }
    else
        p.conds[c] = w.req;

    if((int)p.obj_cond.size() < w.obj_end)
        p.obj_cond.resize(w.obj_end, -1);
    for(int i = w.obj; i < w.obj_end; ++i)
        p.obj_cond[i] = c;

    z30_sub_any_cond = true;
}

static bool z30_set_has(const std::vector<int> &v, int x)
{
    return std::find(v.begin(), v.end(), x) != v.end();
}

static void z30_set_put(std::vector<int> &v, int x, bool on)
{
    std::vector<int>::iterator it = std::find(v.begin(), v.end(), x);
    if(on && it == v.end())
        v.push_back(x);
    else if(!on && it != v.end())
        v.erase(it);
}

// SubscrColorInfo::set_int_color, as it stores (no 2.53 conversion)
static void z30_sub_raw_color(byte &type, short &color, long val)
{
    if(val > 255 || val < Z30_SUB_MIN_COLOR)
        return;
    if(val >= 0) { type = (byte)((val & 0xF0) >> 4); color = (short)(val & 0x0F); }
    else if(val == Z30_SUB_TRANSPARENT) { type = ssctSYSTEM; color = -1; }
    else if(val >= -Z30_SUB_SYS_COLORS) { type = ssctSYSTEM; color = (short)(-val - 1); }
    else { type = ssctMISC; color = (short)(-val - 1 - Z30_SUB_SYS_COLORS); }
}

// SW_ItemSlot::getDisplayItem for a class slot (q30_subscreen_item does the rest)
static int z30_sub_class_display(const subscreen_object &o)
{
    const int iclass = o.d10 > 0 ? o.d10 - 1 : o.d1;
    const bool nosp = o.d9 != 0;
    int family = -1;

    switch(iclass)
    {
    case itype_bomb: if(z30_sub_check_bomb(-1, false)) family = itype_bomb; break;
    case itype_bowandarrow: case itype_arrow: family = itype_arrow; break;
    case itype_letterpotion:
        if(current_item_id(itype_potion) > -1) family = itype_potion;
        else if(current_item_id(itype_letter) > -1) family = itype_letter;
        break;
    case itype_sbomb: if(z30_sub_check_bomb(-1, true)) family = itype_sbomb; break;
    case itype_map: if(nosp) break; return has_item(itype_map, -1) ? iMap : -1;
    case itype_compass: if(nosp) break; return has_item(itype_compass, -1) ? iCompass : -1;
    case itype_bosskey: if(nosp) break; return has_item(itype_bosskey, -1) ? iBossKey : -1;
    case itype_heartpiece: if(nosp) break; if(QMisc.colors.HCpieces_tile) return iHCPiece; break;
    default: family = iclass; break;
    }

    if(family < 0)
        return -1;
    const int id = o.d10 > 0 ? q30_current_item(family) : current_item_id(family);
    return (id < 0 || item_disabled(id)) ? -1 : id;
}

static bool z30_subwidg_more(const long arg, long &ret, const long value, const bool write)
{
    long bad = 0;       // what a type without the value reads (upstream's ret after bad_subwidg_type)

    switch(arg)
    {
    case Z30R_SUBWIDGTY_ANIMVAL: case Z30R_SUBWIDGTY_BUTTON: case Z30R_SUBWIDGTY_CONTAINER: case Z30R_SUBWIDGTY_DELAY:
    case Z30R_SUBWIDGTY_FRAMES: case Z30R_SUBWIDGTY_GAUGE_HEI: case Z30R_SUBWIDGTY_GAUGE_WID: case Z30R_SUBWIDGTY_GRIDX:
    case Z30R_SUBWIDGTY_GRIDY: case Z30R_SUBWIDGTY_HSPACE: case Z30R_SUBWIDGTY_INFITM: case Z30R_SUBWIDGTY_LITEMS:
    case Z30R_SUBWIDGTY_MAXDIG: case Z30R_SUBWIDGTY_MINDIG: case Z30R_SUBWIDGTY_PERCONTAINER: case Z30R_SUBWIDGTY_SHOWDRAIN:
    case Z30R_SUBWIDGTY_SPEED: case Z30R_SUBWIDGTY_TOTAL: case Z30R_SUBWIDGTY_UNITS: case Z30R_SUBWIDGTY_VSPACE:
    case Z30R_SUBWIDGTY_COUNTERS:
        bad = -10000;
        break;
    case Z30R_SUBWIDGTY_COSTIND:
        bad = -1;
        break;
    case Z30R_SUBWIDGTY_COLOR_BG2: case Z30R_SUBWIDGTY_COLOR_CMPBLNK: case Z30R_SUBWIDGTY_COLOR_CMPOFF:
    case Z30R_SUBWIDGTY_COLOR_PLAYER: case Z30R_SUBWIDGTY_COLOR_ROOM: case Z30R_SUBWIDGTY_COLOR_SHD2:
    case Z30R_SUBWIDGTY_COLOR_TXT2: case Z30R_SUBWIDGTY_COLOR_VISITED: case Z30R_SUBWIDGTY_COLOR_UNVISITED:
    case Z30R_SUBWIDGTY_FRAMECSET: case Z30R_SUBWIDGTY_FRAMETILE: case Z30R_SUBWIDGTY_INFCHAR: case Z30R_SUBWIDGTY_ITEMCLASS:
    case Z30R_SUBWIDGTY_ITEMID: case Z30R_SUBWIDGTY_NUMBER: case Z30R_SUBWIDGTY_PIECECSET: case Z30R_SUBWIDGTY_PIECETILE:
    case Z30R_SUBWIDGDISPITM: case Z30R_SUBWIDGPOSES:
    case Z30R_SUBWIDGREQCOUNTER: case Z30R_SUBWIDGREQCOUNTERCOND: case Z30R_SUBWIDGREQCOUNTERVAL: case Z30R_SUBWIDGREQLITEMLEVEL:
    case Z30R_SUBWIDGREQLITEMS: case Z30R_SUBWIDGREQOWNITEMS: case Z30R_SUBWIDGREQSCRIPTDISABLED: case Z30R_SUBWIDGREQUNOWNITEMS:
    case Z30R_SUBWIDGREQ_DMAP: case Z30R_SUBWIDGREQ_DMAP_FLOOR: case Z30R_SUBWIDGREQ_DMAP_LEVEL: case Z30R_SUBWIDGREQ_LEVEL_STATE:
    case Z30R_SUBWIDGREQ_LEVEL_STATE_LEVEL: case Z30R_SUBWIDGREQ_MAP: case Z30R_SUBWIDGREQ_SCREEN:
    case Z30R_SUBWIDGREQ_SCRSTATE_EXSTATE: case Z30R_SUBWIDGREQ_SCRSTATE_MAP: case Z30R_SUBWIDGREQ_SCRSTATE_SCREEN:
    case Z30R_SUBWIDGREQ_SCRSTATE_STATE:
        break;
    default:
        return false;
    }

    ret = 0;
    const long ref = Z30_REF(SUBSCREENWIDG);
    subscreen_object *o = NULL;
    Z30SubWidget *w = z30_subwidg(ref, &o);
    Z30Subscreen *sd = z30_subdata(ref);
    Z30SubPage *p = z30_subpage(ref);
    int sub, sty, pg, ind;

    if(!w || !sd || !p || !z30_from_subref(ref, sub, sty, pg, ind))
    {
        Z_scripterrlog("Invalid subscreenwidget pointer: %ld\n", ref);
        return true;
    }

    const long v = value / 10000;
    const int ty = w->type;
    Q30SubTy &t = w->ty;
    Q30SubCond &rq = w->req;
    const bool gauge = ty == Z30W_LGAUGE || ty == Z30W_MGAUGE || ty == Z30W_MISCGAUGE || ty == Z30W_ITMCOOLDOWNGAUGE;
    const bool og = o && (ty == Z30W_LGAUGE || ty == Z30W_MGAUGE);  // a gauge drawn as a 2.53 object
    bool ok = true;     // the type has this value

// a plain value of the type: get as is, set bounded (and into the object)
#define Z30_TYV(cond, field, lo, hi, setobj) \
    if(!(cond)) { ok = false; break; } \
    if(write) { const long val = vbound(v, (long)(lo), (long)(hi)); field = val; setobj; } \
    else ret = (long)(field) * 10000; \
    break;

    switch(arg)
    {
    // ---- type values ----
    case Z30R_SUBWIDGTY_ANIMVAL:
        Z30_TYV(gauge, t.anim_val, 0, 65535, if(og) o->d9 = (o->d9 & 0xFFFF) | (int)(val << 16))
    case Z30R_SUBWIDGTY_CONTAINER:
        Z30_TYV(gauge, t.container, 0, 65535, if(og) o->d1 = (int)val)
    case Z30R_SUBWIDGTY_DELAY:
        Z30_TYV(gauge, t.delay, 0, 65535, if(og) o->d8 = (int)val)
    case Z30R_SUBWIDGTY_FRAMES:
        Z30_TYV(gauge, t.frames, 1, 65535, if(og) o->d6 = (int)val)
    case Z30R_SUBWIDGTY_SPEED:
        Z30_TYV(gauge, t.speed, 1, 65535, if(og) o->d7 = (int)val)
    case Z30R_SUBWIDGTY_GRIDX:
        Z30_TYV(gauge, t.grid_x, -32768, 32767, if(og) o->color3 = (short)val)
    case Z30R_SUBWIDGTY_GRIDY:
        Z30_TYV(gauge, t.grid_y, -32768, 32767, if(og) o->frame = (word)(short)val)
    case Z30R_SUBWIDGTY_HSPACE:
        Z30_TYV(gauge, t.hspace, -128, 127, if(og) o->delay = (byte)(signed char)val)
    case Z30R_SUBWIDGTY_VSPACE:
        Z30_TYV(gauge, t.vspace, -128, 127, if(og) o->colortype3 = (byte)(signed char)val)
    case Z30R_SUBWIDGTY_GAUGE_WID:
    case Z30R_SUBWIDGTY_GAUGE_HEI:
    case Z30R_SUBWIDGTY_UNITS:
    {
        if(!gauge) { ok = false; break; }
        int &f = arg == Z30R_SUBWIDGTY_GAUGE_WID ? t.gauge_wid : arg == Z30R_SUBWIDGTY_GAUGE_HEI ? t.gauge_hei : t.units;
        if(!write) { ret = (f + 1) * 10000; break; }
        f = (int)vbound(v, 1L, arg == Z30R_SUBWIDGTY_UNITS ? 256L : 32L) - 1;
        if(og && arg == Z30R_SUBWIDGTY_GAUGE_WID) o->frames = (byte)f;
        else if(og && arg == Z30R_SUBWIDGTY_GAUGE_HEI) o->speed = (byte)f;
        else if(og && (o->d10 & SSO_GAUGE_NEWER)) o->d10 = (o->d10 & 0x00FFFFFF) | (f << 24);
        break;
    }
    case Z30R_SUBWIDGTY_BUTTON:
    {
        if(ty != Z30W_BTNITM && ty != Z30W_BTNCOUNTER && ty != Z30W_ITMCOOLDOWNGAUGE && ty != Z30W_ITMCOOLDOWNTEXT) { ok = false; break; }
        if(!write) { ret = t.btn * 10000; break; }
        const int val = (int)vbound(v, -1L, 3L);
        t.btn = (ty == Z30W_BTNITM || ty == Z30W_BTNCOUNTER) ? zc_max(0, val) : val;
        if(o && ty == Z30W_BTNITM) o->d1 = t.btn;
        break;
    }
    case Z30R_SUBWIDGTY_COSTIND:
        Z30_TYV(ty == Z30W_BTNCOUNTER, t.costind, 0, 1, (void)0)
    case Z30R_SUBWIDGTY_INFCHAR:
    {
        if(ty != Z30W_COUNTER && ty != Z30W_OLDCTR && ty != Z30W_BTNCOUNTER) { ok = false; break; }
        if(!write) { ret = (t.infchar & 0xFF) * 10000; break; }
        t.infchar = (int)vbound(v, 0L, 255L);
        if(o && ty == Z30W_COUNTER) o->d5 = (o->d5 & ~0xFF) | t.infchar;
        else if(o && ty == Z30W_OLDCTR) o->d5 = t.infchar;
        break;
    }
    case Z30R_SUBWIDGTY_INFITM:
    {
        const bool ctr = ty == Z30W_COUNTER || ty == Z30W_OLDCTR;
        if(!ctr && ty != Z30W_LGAUGE && ty != Z30W_MGAUGE && ty != Z30W_MISCGAUGE) { ok = false; break; }
        long &f = ctr ? t.infitm : t.inf_item;
        if(!write) { ret = f * 10000; break; }
        f = vbound(v, -1L, (long)Z30_MAX_ITEMS_UP - 1);
        if(o && ctr) o->d10 = f < 0 ? 0 : (int)f;
        break;
    }
    case Z30R_SUBWIDGTY_ITEMCLASS:
    {
        if(ty != Z30W_ITEMSLOT && ty != Z30W_ITMCOOLDOWNGAUGE && ty != Z30W_ITMCOOLDOWNTEXT) { ok = false; break; }
        if(!write) { ret = t.iclass * 10000; break; }
        t.iclass = vbound(v, 0L, 284L);      // itype_maxusable - 1
        if(o && ty == Z30W_ITEMSLOT)
        {
            o->d1 = t.iclass >= itype_max ? (int)itype_misc : (int)t.iclass;
            o->d10 = t.iclass >= itype_max ? (int)t.iclass + 1 : 0;
        }
        break;
    }
    case Z30R_SUBWIDGTY_ITEMID:
    {
        if(ty != Z30W_ITEMSLOT && ty != Z30W_ITMCOOLDOWNGAUGE && ty != Z30W_ITMCOOLDOWNTEXT) { ok = false; break; }
        if(!write) { ret = t.iid * 10000; break; }
        t.iid = vbound(v, -1L, (long)Z30_MAX_ITEMS_UP - 1);
        if(o && ty == Z30W_ITEMSLOT)
            o->d8 = (t.iid >= 0 && t.iid < Q30_MAXITEMS) ? (int)t.iid + 1 : 0;
        break;
    }
    case Z30R_SUBWIDGTY_LITEMS:
        Z30_TYV(ty == Z30W_MMAP, t.litems, 0, 255, (void)0)
    case Z30R_SUBWIDGTY_MAXDIG:
        Z30_TYV(ty == Z30W_COUNTER || ty == Z30W_BTNCOUNTER, t.maxdig, 0, 5, if(ty == Z30W_COUNTER) w->maxdigits = (int)val)
    case Z30R_SUBWIDGTY_MINDIG:
        Z30_TYV(ty == Z30W_COUNTER || ty == Z30W_BTNCOUNTER || ty == Z30W_OLDCTR, t.mindig, 0, 5,
                if(o && ty != Z30W_BTNCOUNTER) o->d4 = (int)val)
    case Z30R_SUBWIDGTY_NUMBER:
        Z30_TYV(ty == Z30W_MCGUFF, t.number, 0, 255, if(o) o->d5 = (int)val)
    case Z30R_SUBWIDGTY_PERCONTAINER:
        if(ty != Z30W_MISCGAUGE && ty != Z30W_ITMCOOLDOWNGAUGE) { ok = false; break; }
        if(write) t.per_container = zc_max(v, 1L); else ret = t.per_container * 10000;
        break;
    case Z30R_SUBWIDGTY_TOTAL:
        if(ty != Z30W_ITMCOOLDOWNGAUGE) { ok = false; break; }
        if(write) t.total = zc_max(v, 1L); else ret = t.total * 10000;
        break;
    case Z30R_SUBWIDGTY_SHOWDRAIN:
        Z30_TYV(ty == Z30W_MGAUGE, t.showdrain, -1, 32767, if(o) o->d9 = (o->d9 & ~0xFFFF) | ((val == -1 ? 0 : (int)val) & 0xFFFF))
    case Z30R_SUBWIDGTY_FRAMETILE:
        Z30_TYV(ty == Z30W_MCGUFF_FRAME, t.frame_tile, 0, NEWMAXTILES - 1, if(o) o->d1 = (int)val)
    case Z30R_SUBWIDGTY_PIECETILE:
        Z30_TYV(ty == Z30W_MCGUFF_FRAME, t.piece_tile, 0, NEWMAXTILES - 1, if(o) o->d3 = (int)val)
    case Z30R_SUBWIDGTY_FRAMECSET:
        Z30_TYV(ty == Z30W_MCGUFF_FRAME, t.frame_cset, 0, 15, if(o) o->d2 = (int)val)
    case Z30R_SUBWIDGTY_PIECECSET:
        Z30_TYV(ty == Z30W_MCGUFF_FRAME, t.piece_cset, 0, 15, if(o) o->d4 = (int)val)
    case Z30R_SUBWIDGTY_COUNTERS:
    {
        const long size = ty == Z30W_COUNTER ? 3 : (ty == Z30W_MISCGAUGE || ty == Z30W_COUNTERPERCBAR) ? 1 : 0;
        long idx = 0;
        if(!size)
            Z_scripterrlog("Widget type %d does not have this value!\n", ty);
        if(!size || !z30_array_index(Z30_INDEX, size, idx))
        {
            if(!write) ret = -10000;
            return true;
        }
        if(!write) ret = t.ctrs[idx] * 10000;
        else if(ty != Z30W_COUNTERPERCBAR) t.ctrs[idx] = (int)vbound(v, (long)Z30_SSC_MIN + 1, (long)Z30_MAX_COUNTERS_UP - 1);
        return true;
    }

    // colours (SubscrColorInfo), and the 2.53 object's where it has them
    case Z30R_SUBWIDGTY_COLOR_TXT2: case Z30R_SUBWIDGTY_COLOR_SHD2: case Z30R_SUBWIDGTY_COLOR_BG2:
    case Z30R_SUBWIDGTY_COLOR_CMPBLNK: case Z30R_SUBWIDGTY_COLOR_CMPOFF: case Z30R_SUBWIDGTY_COLOR_PLAYER:
    case Z30R_SUBWIDGTY_COLOR_ROOM: case Z30R_SUBWIDGTY_COLOR_VISITED: case Z30R_SUBWIDGTY_COLOR_UNVISITED:
    {
        int which = -1, slot = 0;   // slot: the 2.53 object's colour 1-3 it also is
        switch(arg)
        {
        case Z30R_SUBWIDGTY_COLOR_TXT2: if(ty == Z30W_COUNTER || ty == Z30W_BTNCOUNTER) which = Q30SubTy::C_TXT2; break;
        case Z30R_SUBWIDGTY_COLOR_SHD2: if(ty == Z30W_COUNTER || ty == Z30W_BTNCOUNTER) which = Q30SubTy::C_SHD2; break;
        case Z30R_SUBWIDGTY_COLOR_BG2: if(ty == Z30W_COUNTER || ty == Z30W_BTNCOUNTER) which = Q30SubTy::C_BG2; break;
        case Z30R_SUBWIDGTY_COLOR_CMPBLNK: if(ty == Z30W_MMAP) { which = Q30SubTy::C_CMPBLNK; slot = 2; } break;
        case Z30R_SUBWIDGTY_COLOR_CMPOFF: if(ty == Z30W_MMAP) { which = Q30SubTy::C_CMPOFF; slot = 3; } break;
        case Z30R_SUBWIDGTY_COLOR_PLAYER:
            if(ty == Z30W_MMAP) { which = Q30SubTy::C_PLR; slot = 1; }
            else if(ty == Z30W_LMAP) { which = Q30SubTy::C_PLR; slot = 2; }
            break;
        case Z30R_SUBWIDGTY_COLOR_ROOM: if(ty == Z30W_LMAP) { which = Q30SubTy::C_ROOM; slot = 1; } break;
        case Z30R_SUBWIDGTY_COLOR_VISITED: if(ty == Z30W_MMAP) which = Q30SubTy::C_VIS; break;
        case Z30R_SUBWIDGTY_COLOR_UNVISITED: if(ty == Z30W_MMAP) which = Q30SubTy::C_UNVIS; break;
        }
        if(which < 0) { ok = false; break; }
        if(!write) { ret = z30_sub_int_color(t.ctype[which], t.color[which]) * 10000; break; }
        const long val = vbound(v, (long)Z30_SUB_MIN_COLOR, 255L);
        z30_sub_raw_color(t.ctype[which], t.color[which], val);
        if(o && slot)
        {
            byte *ct;
            short *cv;
            z30_sub_color_ref(o, slot, ct, cv);
            z30_sub_set_color(*ct, *cv, val);
        }
        else if(o && ty == Z30W_COUNTER && (o->d6 & SSO_COUNTER_FULLCOLORS))
        {
            // the full colours the reader keeps: text in frames/frame, shadow in speed/d6 >> 16,
            // background in delay/d5 >> 16
            byte ct = 0;
            short cv = 0;
            z30_sub_set_color(ct, cv, val);
            if(which == Q30SubTy::C_TXT2) { o->frames = ct; o->frame = (word)cv; }
            else if(which == Q30SubTy::C_SHD2) { o->speed = ct; o->d6 = (o->d6 & 0xFFFF) | (int)((unsigned)(word)cv << 16); }
            else { o->delay = ct; o->d5 = (o->d5 & 0xFFFF) | (int)((unsigned)(word)cv << 16); }
        }
        break;
    }

    // ---- the item it shows ----
    case Z30R_SUBWIDGDISPITM:
    {
        if(write)
            break;      // read-only
        int id = -1;
        if(ty == Z30W_ITEMSLOT && o && !q30_subscreen_item(*o, &id))
            id = z30_sub_class_display(*o);
        ret = (ty == Z30W_ITEMSLOT && o ? id : -1) * 10000;
        break;
    }

    // ---- cursor neighbours ----
    case Z30R_SUBWIDGPOSES:
    {
        long idx = 0;
        if(!z30_array_index(Z30_INDEX, 4, idx))
        {
            if(!write) ret = -10000;
            return true;
        }
        if(!write) { ret = w->dirs[idx] * 10000; break; }
        w->dirs[idx] = (int)vbound(v, 0L, 255L);
        // the cursor's stops (item slots) take them
        for(int i = w->obj; i >= 0 && i < w->obj_end; ++i)
        {
            subscreen_object *so = z30_sub_page_obj(*sd, *p, pg, i);
            if(so && so->type == ssoCURRENTITEM && so->d3 >= 0)
                *(idx == 0 ? &so->d4 : idx == 1 ? &so->d5 : idx == 2 ? &so->d6 : &so->d7) = w->dirs[idx];
        }
        break;
    }

    // ---- display conditions ----
    case Z30R_SUBWIDGREQCOUNTER:
    case Z30R_SUBWIDGREQCOUNTERCOND:
    case Z30R_SUBWIDGREQCOUNTERVAL:
    case Z30R_SUBWIDGREQLITEMLEVEL:
    case Z30R_SUBWIDGREQLITEMS:
    case Z30R_SUBWIDGREQ_LEVEL_STATE_LEVEL:
    case Z30R_SUBWIDGREQ_SCRSTATE_MAP:
    case Z30R_SUBWIDGREQ_SCRSTATE_SCREEN:
    {
        int *f;
        long lo, hi;
        switch(arg)
        {
        case Z30R_SUBWIDGREQCOUNTER: f = &rq.counter; lo = Z30_SSC_MIN; hi = Z30_MAX_COUNTERS_UP; break;
        case Z30R_SUBWIDGREQCOUNTERCOND: f = &rq.counter_cond; lo = 0; hi = 6; break;
        case Z30R_SUBWIDGREQCOUNTERVAL: f = &rq.counter_val; lo = 0; hi = 65535; break;
        case Z30R_SUBWIDGREQLITEMLEVEL: f = &rq.litem_level; lo = -1; hi = Z30_MAX_LEVELS_UP; break;
        case Z30R_SUBWIDGREQLITEMS: f = &rq.litems; lo = 0; hi = 0xFFFF; break;   // LI_ALL
        case Z30R_SUBWIDGREQ_LEVEL_STATE_LEVEL: f = &rq.lstate_level; lo = -1; hi = Z30_MAX_LEVELS_UP; break;
        case Z30R_SUBWIDGREQ_SCRSTATE_MAP: f = &rq.scrstate_map; lo = 0; hi = map_count; break;
        default: f = &rq.scrstate_scr; lo = -1; hi = 127; break;   // MAPSCRSNORMAL - 1
        }
        if(!write) { ret = *f * 10000; break; }
        *f = (int)vbound(v, lo, hi);
        z30_sub_req_sync(*p, *w);
        break;
    }
    case Z30R_SUBWIDGREQSCRIPTDISABLED:
        if(!write) { ret = rq.disabled ? 10000 : 0; break; }
        rq.disabled = value != 0;
        z30_sub_req_sync(*p, *w);
        break;
    case Z30R_SUBWIDGREQOWNITEMS: case Z30R_SUBWIDGREQUNOWNITEMS: case Z30R_SUBWIDGREQ_DMAP_FLOOR:
    case Z30R_SUBWIDGREQ_DMAP_LEVEL: case Z30R_SUBWIDGREQ_DMAP: case Z30R_SUBWIDGREQ_SCREEN: case Z30R_SUBWIDGREQ_MAP:
    {
        std::vector<int> *set;
        long size;
        switch(arg)
        {
        case Z30R_SUBWIDGREQOWNITEMS: set = &rq.owned; size = Z30_MAX_ITEMS_UP; break;
        case Z30R_SUBWIDGREQUNOWNITEMS: set = &rq.unowned; size = Z30_MAX_ITEMS_UP; break;
        case Z30R_SUBWIDGREQ_DMAP_FLOOR: set = &rq.floors; size = 256; break;
        case Z30R_SUBWIDGREQ_DMAP_LEVEL: set = &rq.levels; size = Z30_MAX_LEVELS_UP; break;
        case Z30R_SUBWIDGREQ_DMAP: set = &rq.dmaps; size = 512; break;
        // upstream has these two the other way round: SCREEN is req_maps, MAP req_screens
        case Z30R_SUBWIDGREQ_SCREEN: set = &rq.maps; size = 255; break;
        default: set = &rq.screens; size = 128; break;
        }
        long idx = 0;
        if(!z30_array_index(Z30_INDEX, size, idx))
            return true;
        if(!write) { ret = z30_set_has(*set, (int)idx) ? 10000 : 0; break; }
        z30_set_put(*set, (int)idx, value != 0);
        z30_sub_req_sync(*p, *w);
        break;
    }
    case Z30R_SUBWIDGREQ_SCRSTATE_STATE: case Z30R_SUBWIDGREQ_SCRSTATE_EXSTATE: case Z30R_SUBWIDGREQ_LEVEL_STATE:
    {
        dword &f = arg == Z30R_SUBWIDGREQ_SCRSTATE_STATE ? rq.scrstate : arg == Z30R_SUBWIDGREQ_SCRSTATE_EXSTATE ? rq.exstate : rq.lvlstate;
        long idx = 0;
        if(!z30_array_index(Z30_INDEX, arg == Z30R_SUBWIDGREQ_SCRSTATE_STATE ? 17 : 32, idx))     // mMAXIND
            return true;
        if(!write) { ret = (f & (1u << idx)) ? 10000 : 0; break; }
        if(value) f |= 1u << idx;
        else f &= ~(1u << idx);
        z30_sub_req_sync(*p, *w);
        break;
    }
    }
#undef Z30_TYV

    if(!ok)
    {
        Z_scripterrlog("Widget type %d does not have this value!\n", ty);
        if(!write)
            ret = bad;
    }

    return true;
}

static bool zasm30_sub_get(const long arg, long &ret)
{
    if(z30_subwidg_more(arg, ret, 0, false))
        return true;

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
    {
        long unused = 0;
        if(z30_subwidg_more(arg, unused, value, true))
            return true;
    }

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
        const long ptr = zasm30_aptr(get_register(sarg1));
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
            ArrayH::getString(zasm30_aptr(SH::read_stack(SPR)), lbl);

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

        const long ptr = zasm30_aptr(get_register(sarg1));

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
        const long ptr = zasm30_aptr(get_register(sarg1));
        if(cmd == Z30C_SUBWIDG_GET_LABEL)
            z30_set_string(ptr, w->label);
        else
            ArrayH::getString(ptr, w->label);
        return true;
    }
    }

    return false;
}

// ---- the subscreen's own script (upstream run_active_subscript, ScriptType::EngineSubscreen) -------
// Upstream runs, each frame the subscreen is open, the active subscreen's script with `this` its
// active subscreen (get_subref(-1, sstACTIVE)), then the current dmap's map subscreen's script with
// `this` that map subscreen (get_subref(-1, sstMAP)), whichever of the two is open (also while the
// map button has the map subscreen open). Both run on one engine data (upstream keeps one for the
// type), so one instance here; `this` is set again before each run; the InitD come from the
// subscreen that starts it.
static bool z30_engsub_close = false;   // SUBDATA_CLOSE by that script: the subscreen closes
static long z30_engsub_cur = 0;         // `this` of the run going on
static subscreen_group *z30_engsub_dmap_active = NULL;   // the dmap's active one while the map one is open

// The dmap's active subscreen (which = 0) or map subscreen (which = 3) and its reference.
static Z30Subscreen *z30_engsub_target(int which, long &ref)
{
    ref = 0;

    if(which == 3)
    {
        const int i = z30_sub_dmap_index(3, get_currdmap());
        if(i < 0)
            return NULL;
        ref = z30_subref(i, 3, 0, 0);
        return &zasm30_subs[3][i];
    }

    subscreen_group *grp = z30_engsub_dmap_active ? z30_engsub_dmap_active : current_subscreen_active;
    for(size_t i = 0; grp && i < zasm30_subs[0].size(); ++i)
        if(zasm30_subs[0][i].slot >= 0 && &custom_subscreen[zasm30_subs[0][i].slot] == grp)
        {
            ref = z30_subref((int)i, 0, 0, 0);
            return &zasm30_subs[0][i];
        }

    return NULL;
}

static long z30_engsub_ref()
{
    return z30_engsub_cur;
}

static void zasm30_engsub_init(Zasm30Instance *in)
{
    const long ref = z30_engsub_ref();
    Z30Subscreen *s = z30_subdata(ref);
    in->frame.refs[Z30REF_SUBSCREENDATA] = ref;
    in->frame.inst[0] = ref;
    if(s)
        memcpy(in->ri.d, s->scr.initd, sizeof(s->scr.initd));
}

static bool z30_engsub_closes(long ref)    // SUBDATA_CLOSE (q30_scripts_sys.h)
{
    if(!zasm30_cur_inst || zasm30_cur_inst->type != SCRIPT_ENGINESUB || ref != z30_engsub_ref())
        return false;
    z30_engsub_close = true;
    return true;
}

void q30_subscreen_script_reset()
{
    zasm30_drop_instance(SCRIPT_ENGINESUB, 0);
    z30_engsub_close = false;
}

// q30_open_map_subscreen: the dmap's active subscreen while the map one stands in for it.
static void z30_engsub_map_open(subscreen_group *dmap_active)
{
    z30_engsub_dmap_active = dmap_active;
}

bool q30_run_subscreen_script()
{
    if(!q30_active() || !zasm30_ffscript_version)
        return false;

    static const int order[2] = { 0, 3 };

    for(int n = 0; n < 2; ++n)
    {
        long ref;
        Z30Subscreen *s = z30_engsub_target(order[n], ref);

        if(!s || s->scr.script <= 0 || !zasm30_script_for(SCRIPT_ENGINESUB, (word)s->scr.script))
            continue;

        z30_engsub_cur = ref;
        Zasm30Instance *in = zasm30_instance(SCRIPT_ENGINESUB, 0);
        if(in->initialized)
        {
            in->frame.refs[Z30REF_SUBSCREENDATA] = ref;   // (upstream sets it again every run)
            in->frame.inst[0] = ref;
        }

        z30_engsub_close = false;
        zasm30_run(SCRIPT_ENGINESUB, (word)s->scr.script, 0);

        const bool closed = z30_engsub_close;
        z30_engsub_close = false;
        if(closed)
            return true;
    }

    return false;
}

#endif

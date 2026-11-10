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
    int type, obj;          // obj: index in the subscreen's 2.53 objects, -1 if it has none
    std::string label;
    int posflags, x, y, w, h, pos;
    dword genflags, flags;
};

struct Z30SubPage
{
    std::vector<Z30SubWidget> widgets;
    int cursor_pos;
};

struct Z30Subscreen
{
    int slot;               // custom_subscreen[] index, -1 if not kept
    std::string name;
    std::vector<Z30SubPage> pages;
};

// active, passive, overlay, map: upstream's sstACTIVE..sstMAP
static std::vector<Z30Subscreen> zasm30_subs[4];

// ---- filled by the reader ----------------------------------------------------------------------

void q30_sub_clear()
{
    for(int i = 0; i < 4; ++i)
        zasm30_subs[i].clear();
}

void q30_sub_begin(int list, int slot, const char *name)
{
    if(list < 0 || list >= 4)
        return;

    Z30Subscreen s;
    s.slot = slot;
    s.name = name ? name : "";
    zasm30_subs[list].push_back(s);
}

void q30_sub_page(int list, int cursor_pos)
{
    if(list < 0 || list >= 4 || zasm30_subs[list].empty())
        return;

    Z30SubPage p;
    p.cursor_pos = cursor_pos;
    zasm30_subs[list].back().pages.push_back(p);

    // ZC_Q30_SUBLOG: each subscreen page the quest has (diagnostics only)
    if(getenv("ZC_Q30_SUBLOG"))
        al_trace("q30 subpage: list %d sub %d (slot %d) page %d\n", list, (int)zasm30_subs[list].size() - 1,
                 zasm30_subs[list].back().slot, (int)zasm30_subs[list].back().pages.size() - 1);
}

void q30_sub_widget(int list, int type, const char *label, int obj, int posflags, int x, int y, int w, int h,
                    int pos, dword genflags, dword flags)
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
    zasm30_subs[list].back().pages.back().widgets.push_back(wd);
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
        *obj = &custom_subscreen[s->slot].objects[w->obj];

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
            if(id > 0 && id < MAXITEMS)
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

// show_custom_subscreen's ssoCURRENTITEM, for an item slot the 2.53 code can't choose for: one
// naming an item of a family 2.53 has no number for (a bottle), or a class it has none for (the
// reader keeps that class + 1 in d10). Returns true when it decided: *id is the item to draw, or
// -1 for none, as upstream's SW_ItemSlot::getDisplayItem.
bool q30_subscreen_item(const subscreen_object &o, int *id)
{
    if(!q30_active() || !game)
        return false;

    if(o.d8 > 0)
    {
        const int item = o.d8 - 1;

        if(item >= MAXITEMS || q30_item_family(item) < itype_max)
            return false;   // a family 2.53 knows: its own rules

        *id = (game->item[item] && !item_disabled(item)) ? item : -1;
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
    return -1;
}

static long z30_sub_font_from_ssf(int ssf)
{
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
    // Only the first page is ever shown here, and pages never change by themselves.
    case Z30R_SUBDATACURPG: ret = 0; return true;
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
            ret = (s->slot >= 0 && &custom_subscreen[s->slot] == current_subscreen_active ? Bpos : s->pages[0].cursor_pos) * 10000;
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
        if(value / 10000 != 0)
            z30_sub_unported(arg, true);    // only the first page is ever drawn here
        return true;
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
        Z30SubPage *p = page ? z30_subpage(Z30_REF(SUBSCREENPAGE)) : (s && !s->pages.empty() ? &s->pages[0] : NULL);
        if(page)
            z30_from_subref(Z30_REF(SUBSCREENPAGE), sub, ty, pg, ind);
        if(!p)
            return true;
        p->cursor_pos = vbound(value / 10000, 0, 255);
        if(pg == 0 && s->slot >= 0 && &custom_subscreen[s->slot] == current_subscreen_active)
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

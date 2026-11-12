// The SUBS section of 2.55 and 3.0 Nightly quests in upstream's own format (versions 8-21) as a
// tape (zq_tape.h), as q255_init.cpp's read_new_subscreens reads it. Keys: "s<n>." for the
// subscreen n (counted through the active, passive, overlay and map lists in file order),
// "s<n>.p<k>." for its pages, "s<n>.p<k>.w<i>." for a page's widgets, "s<n>.p<k>.t<j>." for a
// page's change transitions.
//
// What 2.53's subscreen editor edits (the subscreens' names, and the place and size of page 1's
// widgets, which it converts) isn't "extra"; everything else is: the subscreens' settings and
// page buttons and transitions, the other pages' widgets, every widget's general flags, cursor
// links, page changes and display conditions, and each widget type's own fields (colours, fonts,
// tiles, counters, gauges...) except, on page 1, those 2.53's converted objects hold. Reading,
// each widget must end where the player's reader ends it (q255_widget_extent).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>

#include "zq_tape.h"

size_t q255_widget_extent(const byte *data, size_t size, int version, byte type);   // q255_init.cpp

namespace
{

int g_v;                // the section version, for the walkers

// SubscrSelectorInfo: 8 bytes, then a count and 12 bytes each
void walk_selector(TapeParser &p)
{
    p.rskip(8);
    p.rskip((size_t)p.rd(1) * 12);
}


// SubscrTransition
void transition(TapeParser &p, const char *pre, const char *what)
{
    char k[64], l[96];
    snprintf(k, sizeof(k), "%stype", pre);
    snprintf(l, sizeof(l), "%s: type", what);
    p.num(k, l, 1, false, TK_INT, TL_NONE, 0, "0 instant, 1 slide, 2 pixellate.");
    snprintf(k, sizeof(k), "%ssfx", pre);
    snprintf(l, sizeof(l), "%s: sound", what);
    p.u(k, l, g_v < 17 ? 1 : 2, TL_SFX);
    snprintf(k, sizeof(k), "%sflags", pre);
    snprintf(l, sizeof(l), "%s: flags", what);
    p.flags(k, l, 2, TL_SUBTRANSFLAGS);
    snprintf(k, sizeof(k), "%snargs", pre);
    const int args = (int)p.hidden(k, 1);

    for(int q = 0; q < args && !p.bad; ++q)
    {
        snprintf(k, sizeof(k), "%sarg%d", pre, q);
        snprintf(l, sizeof(l), "%s: argument %d", what, q + 1);
        p.s(k, l, 4);
    }
}

// A script and its eight InitDs, as versions before 21 store it (the InitDs only with a script);
// buttons: the byte widgets have between them.
void old_script(TapeParser &p, const char *pre, bool buttons)
{
    char k[64], l[64];
    snprintf(k, sizeof(k), "%sscript", pre);
    const int64_t script = p.u(k, "Script", 2);

    if(!script)
        return;

    if(buttons)
    {
        snprintf(k, sizeof(k), "%sscript_btns", pre);
        p.flags(k, "Script runs on buttons", 1, TL_INTBTNS);
    }

    for(int q = 0; q < 8; ++q)
    {
        snprintf(k, sizeof(k), "%sinitd%d", pre, q);
        snprintf(l, sizeof(l), "InitD[%d]", q);
        p.s(k, l, 4);
    }
}

// upstream's widget types (q255_init.cpp's WIDG_*)
enum
{
    W_NULL, W_FRAME, W_TEXT, W_LINE, W_RECT, W_TIME, W_MMETER, W_LMETER, W_BTNITM, W_COUNTER,
    W_OLDCTR, W_MMAPTITLE, W_MMAP, W_LMAP, W_BGCOLOR, W_ITEMSLOT, W_MCGUFF_FRAME, W_MCGUFF, W_TILEBLOCK, W_MINITILE,
    W_SELECTOR, W_LGAUGE, W_MGAUGE, W_TEXTBOX, W_SELECTEDTEXT, W_MISCGAUGE, W_BTNCOUNTER, W_COUNTERPERCBAR,
    W_ITMCOOLDOWNGAUGE, W_ITMCOOLDOWNTEXT, W_MAX
};

// The widget types the reader turns into 2.53 subscreen objects (on page 1, 2.53's subscreen
// editor edits what it converts).
bool converted_type(int ty)
{
    return ty != W_NULL && ty != W_BTNCOUNTER && ty != W_MISCGAUGE && ty != W_ITMCOOLDOWNGAUGE && ty != W_ITMCOOLDOWNTEXT &&
           ty != W_COUNTERPERCBAR && ty < W_MAX;
}

// Builds "<pre><name>" keys and "<what>" labels for the fields below.
struct Own
{
    TapeParser &p;
    const char *pre;
    bool hold;          // page 1, a converted type: what the conversion keeps is 2.53's
    char k[96];

    Own(TapeParser &tp, const char *prefix, bool h) : p(tp), pre(prefix), hold(h) {}

    const char *key(const char *name) { snprintf(k, sizeof(k), "%s%s", pre, name); return k; }
    void held(bool by_253) { p.extra(!(hold && by_253)); }

    int64_t s(const char *name, const char *label, int width, bool by_253, const char *help = NULL)
    {
        held(by_253);
        return p.num(key(name), label, width, true, TK_INT, TL_NONE, 0, help);
    }
    int64_t u(const char *name, const char *label, int width, bool by_253, const char *help = NULL)
    {
        held(by_253);
        return p.num(key(name), label, width, false, TK_INT, TL_NONE, 0, help);
    }
    int64_t e(const char *name, const char *label, int width, bool sign, int list, bool by_253)
    {
        held(by_253);
        return p.num(key(name), label, width, sign, TK_ENUM, list);
    }

    // a subscreen colour: its type and its value
    void color(const char *name, const char *what, bool by_253)
    {
        char n[48], l[96];
        snprintf(n, sizeof(n), "%s_type", name);
        snprintf(l, sizeof(l), "%s: colour type", what);
        u(n, l, 1, by_253, "0-13: a CSet (the colour picks one of its colours). 254: a system colour. "
                          "255: a subscreen colour (the colour: 0 text, 1 caption, 2 overworld background, "
                          "3 dungeon background, 4 dungeon foreground, 5 cave foreground, 6 ... 16 message text).");
        snprintf(l, sizeof(l), "%s: colour", what);
        s(name, l, 2, by_253);
    }

    void colors3(bool by_253)
    {
        color("c_text", "Text", by_253);
        color("c_shadow", "Shadow", by_253);
        color("c_bg", "Background", by_253);
    }

    // font, alignment, shadow style: what most text widgets start with
    void text_style(bool by_253, bool align = true)
    {
        s("font", "Font", 4, by_253, "Upstream's font number.");

        if(align)
            u("align", "Alignment", 1, by_253, "0 left, 1 centred, 2 right.");

        u("shadow", "Shadow style", 1, by_253, "0 none, 1 shadow, 2 shadow (U), 3 shadow (O), 4 shadow (+), 5 shadow (X), "
                                              "6 shadowed, 7 shadowed (U), 8 shadowed (O), 9 shadowed (+), 10 shadowed (X).");
    }

    void text(const char *name, const char *label, bool by_253)
    {
        held(by_253);
        p.wstr(key(name), label);
    }

    // SW_GaugePiece: its tiles and frames; some fields only follow with the flags that use them
    void gauge(int64_t flags, bool by_253)
    {
        static const char *const what[4] = { "Empty", "Partial 1", "Partial 2", "Full" };

        for(int q = 0; q < 4; ++q)
        {
            char n[32], l[64];
            snprintf(n, sizeof(n), "tile%d", q);
            snprintf(l, sizeof(l), "%s tile", what[q]);
            s(n, l, 4, by_253, "A tile times 4, plus the corner (0-3) for a mini tile.");
            snprintf(n, sizeof(n), "cset%d", q);
            snprintf(l, sizeof(l), "%s CSet", what[q]);
            u(n, l, 1, by_253);
        }

        const int64_t frames = u("frames", "Frames", 2, by_253);
        const bool still = frames <= 1;
        const bool no_anim = still || !(flags & 0xC0);
        const bool no_speed = still || (!no_anim && frames <= 2 && (flags & 0x200));
        const bool no_inf = !(flags & 0xC00);

        if(!no_speed)
        {
            u("speed", "Animation speed", 2, by_253);
            u("delay", "Animation delay", 2, by_253);
        }

        u("container", "Container", 2, by_253, "Which container (heart, magic...) this piece shows.");
        u("unit_per_frame", "Units per frame", 1, false);
        const int64_t wid = u("grid_w", "Grid width", 1, false);
        const int64_t hei = u("grid_h", "Grid height", 1, false);

        if(wid || hei)
        {
            u("grid_hspace", "Grid horizontal spacing", 1, false);
            u("grid_vspace", "Grid vertical spacing", 1, false);
            s("grid_xoff", "Grid X offset", 2, false);
            s("grid_yoff", "Grid Y offset", 2, false);
        }

        if(!no_anim)
            u("anim_val", "Animation value", 2, false);

        if(!no_inf)
            s("inf_item", "Infinite item", g_v < 18 ? 2 : 4, false);
    }
};

// The type's own fields, as read_widget reads them after the base.
void own_data(TapeParser &p, int v, const char *pre, int ty, int64_t flags, bool page1)
{
    Own o(p, pre, page1 && converted_type(ty));

    switch(ty)
    {
    case W_FRAME:
        o.s("tile", "Tile", 4, true);
        o.color("cs", "CSet", true);
        break;

    case W_TEXT:
        o.text_style(true);
        o.text("text", "Text", true);
        o.colors3(true);
        break;

    case W_LINE:
        o.color("c_line", "Line", true);
        break;

    case W_RECT:
        o.color("c_fill", "Fill", true);
        o.color("c_outline", "Outline", true);
        break;

    case W_TIME:
        o.text_style(true);
        o.colors3(true);
        break;

    case W_LMETER:
        o.u("rows", "Rows", 1, true);
        break;

    case W_BTNITM:
        o.u("button", "Button", 1, true, "0 A, 1 B, 2 X, 3 Y.");
        break;

    case W_COUNTER:
        o.text_style(true);
        o.colors3(true);
        o.e("ctr0", "Counter 1", 4, true, TL_COUNTER, true);
        o.e("ctr1", "Counter 2", 4, true, TL_COUNTER, true);
        o.e("ctr2", "Counter 3", 4, true, TL_COUNTER, true);
        o.u("mindigits", "Minimum digits", 1, true);
        o.u("maxdigits", "Maximum digits", 1, false);
        o.s("item", "Infinite item", 4, true, "-1: none.");
        o.u("infchar", "Infinite character", 1, true);

        if(v >= 12)
        {
            o.color("c_text2", "Second text", false);
            o.color("c_shadow2", "Second shadow", false);
            o.color("c_bg2", "Second background", false);
        }

        break;

    case W_OLDCTR:
        o.text_style(true, false);
        o.colors3(true);
        o.u("digits", "Digits", 1, true);
        o.s("item", "Infinite item", 4, true, "-1: none.");
        o.u("infchar", "Infinite character", 1, true);
        break;

    case W_BTNCOUNTER:
        o.text_style(false);
        o.colors3(false);
        o.u("mindigits", "Minimum digits", 1, false);
        o.u("maxdigits", "Maximum digits", 1, false);
        o.u("infchar", "Infinite character", 1, false);
        o.u("button", "Button", 1, false, "0 A, 1 B, 2 X, 3 Y.");
        o.u("costind", "Cost", 1, false, "Which of the item's costs to show.");

        if(v >= 12)
        {
            o.color("c_text2", "Second text", false);
            o.color("c_shadow2", "Second shadow", false);
            o.color("c_bg2", "Second background", false);
        }

        break;

    case W_MMAPTITLE:
        o.text_style(true);
        o.colors3(true);

        if(v >= 10)
            o.u("tabsize", "Tab size", 1, false);

        break;

    case W_MMAP:
        if(v >= 13)
        {
            o.held(false);
            p.num(o.key("compass_litems"), "Compass shows for level items", v >= 15 ? 2 : 1, false, TK_FLAGS, TL_LITEMBITS);
        }

        o.color("c_plr", "Player", true);
        o.color("c_cmp_blink", "Compass (blinking)", true);
        o.color("c_cmp_off", "Compass (off)", true);

        if(v >= 19)
            o.color("c_room", "Visited room", false);

        if(v >= 20)
        {
            o.color("c_room_unvisited", "Unvisited room", false);
            o.color("c_bg", "Background", false);
        }

        break;

    case W_LMAP:
        o.color("c_plr", "Player", true);
        o.color("c_room", "Room", true);
        break;

    case W_BGCOLOR:
        o.color("c_bg", "Background", true);
        break;

    case W_ITEMSLOT:
        o.e("iclass", "Item class", 4, true, TL_ITEMCLASS, true);
        o.s("item", "Item", 4, true, "A particular item (-1: the class's highest).");
        break;

    case W_MCGUFF_FRAME:
        o.s("frame_tile", "Frame tile", 4, true);
        o.s("piece_tile", "Piece tile", 4, true);
        o.u("frame_cset", "Frame CSet", 1, true);
        o.u("piece_cset", "Piece CSet", 1, true);
        o.color("c_outline", "Outline", true);
        o.color("c_number", "Number", true);
        break;

    case W_MCGUFF:
        o.s("tile", "Tile", 4, true);
        o.s("number", "Piece number", 4, true);
        o.u("flip", "Flip", 1, true);
        o.color("cs", "CSet", true);
        break;

    case W_TILEBLOCK:
        o.s("tile", "Tile", 4, true);
        o.u("flip", "Flip", 1, true);
        o.color("cs", "CSet", true);
        break;

    case W_MINITILE:
        o.s("tile", "Tile", 4, true, "-1: a special tile.");
        o.s("special", "Special tile", 4, true);
        o.u("corner", "Corner", 1, true);
        o.u("flip", "Flip", 1, true);
        o.color("cs", "CSet", true);
        break;

    case W_LGAUGE:
        o.gauge(flags, true);
        break;

    case W_MGAUGE:
        o.gauge(flags, true);
        o.s("showdrain", "Shown at magic drain", 2, true, "-1: always.");
        break;

    case W_MISCGAUGE:
        o.gauge(flags, false);
        o.e("counter", "Counter", 2, true, TL_COUNTER, false);
        o.u("per_container", "Per container", 2, false);
        break;

    case W_ITMCOOLDOWNGAUGE:
        o.gauge(flags, false);
        o.e("iclass", "Item class", 4, true, TL_ITEMCLASS, false);
        o.s("item", "Item", 4, false);
        o.u("button", "Button", 1, false, "0 A, 1 B, 2 X, 3 Y.");
        o.s("points", "Points", 4, false);
        o.s("per_container", "Per container", 4, false);
        break;

    case W_ITMCOOLDOWNTEXT:
        o.text_style(false);
        o.colors3(false);
        o.e("iclass", "Item class", 4, true, TL_ITEMCLASS, false);
        o.s("item", "Item", 4, false);
        o.u("button", "Button", 1, false, "0 A, 1 B, 2 X, 3 Y.");
        break;

    case W_TEXTBOX:
        o.text_style(true);
        o.u("tabsize", "Tab size", 1, true);
        o.text("text", "Text", true);
        o.colors3(true);
        break;

    case W_SELECTEDTEXT:
        o.text_style(true);
        o.u("tabsize", "Tab size", 1, true);
        o.colors3(true);
        break;

    case W_COUNTERPERCBAR:
        o.e("counter", "Counter", 2, true, TL_COUNTER, false);
        o.color("c_fill", "Fill", false);
        o.color("c_bg", "Background", false);
        break;
    }

    p.extra(true);
}

void widget(TapeParser &p, int v, const char *pre, bool page1)
{
    char k[80];
#define K(name) (snprintf(k, sizeof(k), "%s" name, pre), k)
    const int ty = (int)p.hidden(K("type"), 1);
    const size_t start = p.pos;
    size_t extent = 0;

    if(!p.src)
    {
        extent = q255_widget_extent(p.d ? p.d + start : NULL, p.left(), v, (byte)ty);

        if(!extent)
        {
            p.bad = true;
            return;
        }
    }

    p.hidden(K("ssotype"), 1);
    // page 1's place and size are 2.53's (its subscreen editor moves those widgets)
    p.extra(!page1);
    p.group("Widgets");
    p.flags(K("posflags"), "Shown", 1, TL_SUBPOSFLAGS);
    p.s(K("x"), "X", 2);
    p.s(K("y"), "Y", 2);
    p.u(K("w"), "Width", 2);
    p.u(K("h"), "Height", 2);
    p.extra(true);
    const int64_t genflags = p.flags(K("genflags"), "General flags", 4, TL_WIDGFLAGS);
    const int64_t wflags = p.num(K("flags"), "Widget flags", 4, false, TK_FLAGS, TL_WIDGSPEC, ty);

    if(v >= 9)
        p.wstr(K("label"), "Label");

    if(v >= 11)
        p.hidden(K("compat"), 1);

    if(genflags & 1)
    {
        p.s(K("pos"), "Cursor position", 4);
        p.s(K("pos_up"), "Cursor up goes to", 4);
        p.s(K("pos_down"), "Cursor down goes to", 4);
        p.s(K("pos_left"), "Cursor left goes to", 4);
        p.s(K("pos_right"), "Cursor right goes to", 4);
        p.wstr(K("seltext"), "Selected text");

        if(genflags & 4)
            p.raw(K("selector"), "selector override", walk_selector, 9);

        if(v >= 21)
        {
            p.flags(K("script_btns"), "Script runs on buttons", 1, TL_INTBTNS);
            p.script_config(K("script"), "Script");
        }
        else
            old_script(p, pre, true);

        const int64_t pg_mode = p.num(K("pg_mode"), "Page change", 1, false, TK_INT, TL_NONE, 0,
                                        "0 none, 1 next, 2 previous, 3 go to page.");

        if(pg_mode)
        {
            p.flags(K("pg_btns"), "Page change buttons", 1, TL_INTBTNS);
            p.u(K("pg_targ"), "Page change target", 1);
            transition(p, K("pt."), "Page change transition");
        }
    }

    if(v >= 14)
    {
        p.group("Conditions");
        p.nlist(K("req_items"), "Required items", 2, v < 18 ? 1 : 2, TL_ITEM);
        p.nlist(K("ban_items"), "Forbidden items", 2, v < 18 ? 1 : 2, TL_ITEM);
        p.s(K("req_counter"), "Counter", 2, TL_COUNTER);
        p.u(K("req_counter_val"), "Counter value", 2);
        p.num(K("req_counter_cond"), "Counter condition", 1, false, TK_INT, TL_NONE, 0, "0 none, 1 at least, 2 below.");
        p.flags(K("req_litems"), "Level items", v >= 15 ? 2 : 1, TL_LITEMBITS);
        p.s(K("req_litem_level"), "Level items: level (-1: current)", 2);
        p.u(K("disabled"), "Disabled", 1);

        if(v >= 16)
        {
            p.nlist(K("req_floors"), "Only on these floors", 1, 1, TL_NONE);
            p.nlist(K("req_levels"), "Only in these levels", 2, 2, TL_NONE);
            p.nlist(K("req_dmaps"), "Only on these DMaps", 2, 2, TL_NONE);
            p.nlist(K("req_maps"), "Only on these maps", 2, 2, TL_NONE);
            p.nlist(K("req_screens"), "Only on these screens", 1, 1, TL_NONE);
            p.u(K("req_scrstate_map"), "Screen state: map", 2);
            p.s(K("req_scrstate_scr"), "Screen state: screen", 2);
            p.flags(K("req_scrstate"), "Screen states", 4, TL_SCRSTATES);
            p.flags(K("req_exstate"), "Extra states", 4, TL_NONE);
            p.flags(K("req_lvlstate"), "Level states", 4, TL_NONE);
            p.s(K("req_lstate_level"), "Level states: level (-1: current)", 2);
        }
    }

    // the type's own fields; reading, they must end where the player's reader does
    p.group("Widgets");
    own_data(p, v, pre, ty, wflags, page1);

    if(!p.src && p.pos != start + extent)
        p.bad = true;
#undef K
}

} // namespace

bool zq_tape_subscreens(TapeParser &p, int v)
{
    if(v < 8 || v > 21)
        return false;       // the older format is 2.53's own subscreen editor's

    g_v = v;
    const int lists = v >= 16 ? 4 : 3;
    int sub = 0;
    char k[96], pre[64];

    for(int list = 0; list < lists && !p.bad; ++list)
    {
        snprintf(k, sizeof(k), "list%d", list);
        const int count = (int)p.hidden(k, 1);

        for(int q = 0; q < count && !p.bad; ++q, ++sub)
        {
            p.extra(false);
            p.group("Subscreens");
            snprintf(k, sizeof(k), "s%d.name", sub);
            p.cstr(k, "Name");
            snprintf(k, sizeof(k), "s%d.kind", sub);
            const int file_type = (int)p.hidden(k, 1);
            p.extra(true);
            snprintf(k, sizeof(k), "s%d.flags", sub);
            const int64_t flags = p.flags(k, "Flags", 4, TL_SUBFLAGS);

            if(file_type == 0 || file_type == 3)
            {
                if(flags & 2)
                {
                    snprintf(k, sizeof(k), "s%d.selector", sub);
                    p.raw(k, "selector", walk_selector, 9);
                }

                static const char *const btn[4] = { "A", "B", "X", "Y" };

                for(int b = 0; b < 4; ++b)
                {
                    char l[48];
                    snprintf(k, sizeof(k), "s%d.default_%s", sub, btn[b]);
                    snprintf(l, sizeof(l), "Default %s item position", btn[b]);
                    p.s(k, l, 2);
                }

                snprintf(k, sizeof(k), "s%d.btn_left", sub);
                p.flags(k, "Page left buttons", 1, TL_INTBTNS);
                snprintf(k, sizeof(k), "s%d.btn_right", sub);
                p.flags(k, "Page right buttons", 1, TL_INTBTNS);
                snprintf(pre, sizeof(pre), "s%d.tl.", sub);
                transition(p, pre, "Page left transition");
                snprintf(pre, sizeof(pre), "s%d.tr.", sub);
                transition(p, pre, "Page right transition");

                if(v >= 21)
                {
                    snprintf(k, sizeof(k), "s%d.script", sub);
                    p.script_config(k, "Script");
                }
                else
                {
                    snprintf(pre, sizeof(pre), "s%d.", sub);
                    old_script(p, pre, false);
                }
            }

            snprintf(k, sizeof(k), "s%d.pages", sub);
            const int pages = (int)p.hidden(k, 1);

            for(int pg = 0; pg < pages && !p.bad; ++pg)
            {
                p.group("Pages");
                p.extra(true);
                snprintf(k, sizeof(k), "s%d.p%d.cursor", sub, pg);
                p.s(k, "Cursor starts at", 4);
                snprintf(k, sizeof(k), "s%d.p%d.widgets", sub, pg);
                const int widgets = (int)p.hidden(k, 2);

                for(int w = 0; w < widgets && !p.bad; ++w)
                {
                    snprintf(pre, sizeof(pre), "s%d.p%d.w%d.", sub, pg, w);
                    widget(p, v, pre, pg == 0);
                }

                if(v >= 16)
                {
                    p.group("Pages");
                    p.extra(true);
                    snprintf(k, sizeof(k), "s%d.p%d.transitions", sub, pg);
                    const int trs = (int)p.hidden(k, 1);

                    for(int t = 0; t < trs && !p.bad; ++t)
                    {
                        snprintf(pre, sizeof(pre), "s%d.p%d.t%d.", sub, pg, t);
                        snprintf(k, sizeof(k), "%sbtns", pre);
                        p.flags(k, "Change buttons", 1, TL_INTBTNS);
                        snprintf(k, sizeof(k), "%smode", pre);
                        p.u(k, "Change mode", 1);
                        snprintf(k, sizeof(k), "%starg", pre);
                        p.u(k, "Change target", 1);
                        snprintf(k, sizeof(k), "%sflags", pre);
                        p.flags(k, "Change flags", 1, TL_NONE);
                        snprintf(k, sizeof(k), "%str.", pre);
                        transition(p, k, "Transition");
                    }
                }
            }
        }
    }

    return !p.bad;
}

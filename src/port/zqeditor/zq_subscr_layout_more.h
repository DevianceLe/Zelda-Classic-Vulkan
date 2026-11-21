// More of ZQuest's subscreen layout editor for 2.55 and 3.0 Nightly quests (zq_subscr_layout.cpp,
// which includes this inside its own namespace, after its model and edits), after upstream 3.0's
// editor:
// - whole subscreens added, deleted, renamed and duplicated in the active, passive, overlay and
//   map lists (zq/dialog/subscr_lists_edit.cpp, zq_subscr.cpp delete_subscreen), the DMaps'
//   subscreen numbers following as upstream moves them;
// - the Subscreen Settings and Page Settings dialogs (zq/dialog/subscr_settings.cpp,
//   subscr_page_settings.cpp, subscr_transition.cpp);
// - the wizards: Item Grid, Counter Block, Map Tile Grid (zq/dialog/subscreenwizard.cpp);
// - Paste Properties (SubscrWidget::copy_prop), Set Preview DMap and the View toggles.
// Everything stays in the section's own format version: what a version can't hold is refused,
// with the reason.
//
// Written for the port (Dear ImGui; no jwin or Allegro code).

// ---- tests: where things are in the window --------------------------------------------------

// ZQ_SUBLAYOUT_UIMAP=<file> (tests only): whenever the editor's window changes, the file lists
// the menus, buttons and fields made through the helpers below with their rectangles in window
// pixels and the value they hold (name, left, top, right, bottom, value, tab separated), after
// two lines of state, so a UI script (ZQ_UI_SCRIPT) can click them by name and see what it did.
struct UiMark
{
    std::string name, value;
    ImVec4 at;
};

struct UiMap
{
    const char *file;
    bool asked;
    std::vector<UiMark> now;
    std::string written;

    UiMap() : file(NULL), asked(false) {}

    bool on()
    {
        if(!asked)
        {
            asked = true;
            file = getenv("ZQ_SUBLAYOUT_UIMAP");
        }

        return file != NULL;
    }
};

UiMap g_uimap;

// The item just made, under this name, with the value it shows.
void ui_mark(const std::string &name, const std::string &value = std::string())
{
    if(!g_uimap.on())
        return;

    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    UiMark m;
    m.name = name;
    m.value = value;
    m.at = ImVec4(a.x, a.y, b.x, b.y);
    g_uimap.now.push_back(m);
}

void ui_mark(const std::string &name, int value)
{
    ui_mark(name, std::to_string(value));
}

bool m_item(const char *label, const char *shortcut = NULL, bool selected = false, bool enabled = true)
{
    const bool r = ImGui::MenuItem(label, shortcut, selected, enabled);
    ui_mark(label);
    return r;
}

// A menu item that is on or off.
bool m_toggle(const char *label, bool *v)
{
    const bool r = ImGui::MenuItem(label, NULL, v);
    ui_mark(label, *v);
    return r;
}

bool m_begin(const char *label, bool enabled = true)
{
    const bool r = ImGui::BeginMenu(label, enabled);
    ui_mark(label);
    return r;
}

bool m_button(const char *label, float width = 0)
{
    const bool r = ImGui::Button(label, ImVec2(width, 0));
    ui_mark(label);
    return r;
}

// pre: what the mark's name starts with, for a label several parts of a dialog share.
bool m_check(const char *label, bool *v, const std::string &pre = std::string())
{
    const bool r = ImGui::Checkbox(label, v);
    ui_mark(pre + label, *v);
    return r;
}

// A whole number kept between lo and hi.
bool m_int(const char *label, int *v, int lo, int hi, float width = 110, const std::string &pre = std::string())
{
    ImGui::SetNextItemWidth(width * zcui::scale());
    const bool r = ImGui::InputInt(label, v);
    *v = (std::max)(lo, (std::min)(hi, *v));
    ui_mark(pre + label, *v);
    return r;
}

// One of n names; each is marked "<label>.<name>" while the list is open.
bool m_combo(const char *label, int *v, const char *const *names, int n, float width = 150, const std::string &pre = std::string())
{
    bool r = false;
    ImGui::SetNextItemWidth(width * zcui::scale());

    if(ImGui::BeginCombo(label, *v >= 0 && *v < n ? names[*v] : "?"))
    {
        for(int i = 0; i < n; ++i)
        {
            if(ImGui::Selectable(names[i], i == *v))
            {
                *v = i;
                r = true;
            }

            ui_mark(pre + label + "." + names[i]);
        }

        ImGui::EndCombo();
    }
    else
        ui_mark(pre + label, *v);

    return r;
}

// ---- a subscreen's settings in its bytes -----------------------------------------------------

void out16(Bytes &o, int v)
{
    o.push_back((byte)(v & 0xFF));
    o.push_back((byte)((v >> 8) & 0xFF));
}

void out32(Bytes &o, long v)
{
    for(int k = 0; k < 4; ++k)
        o.push_back((byte)(((dword)v >> (8 * k)) & 0xFF));
}

std::string format_note(int v)
{
    return "this quest's subscreen format is version " + std::to_string(v) + (v <= 12 ? " (2.55)" : " (3.0)");
}

// upstream's SubscrTransition: how one page gives way to another
enum { TR_INSTANT, TR_SLIDE, TR_PIXEL };

struct Trans
{
    int type, sfx, flags;
    long arg[4];

    Trans() : type(TR_INSTANT), sfx(0), flags(0) { arg[0] = arg[1] = arg[2] = arg[3] = 0; }
};

// SubscrTransition::num_args
int trans_args(int type)
{
    return type == TR_SLIDE ? 2 : type == TR_PIXEL ? 4 : 0;
}

void get_trans(Reader &r, int v, Trans &t)
{
    t = Trans();
    t.type = r.c();
    t.sfx = v < 17 ? r.c() : r.w();
    t.flags = r.w();
    const int n = r.c();

    for(int q = 0; q < n && !r.bad; ++q)
    {
        const long a = r.l();

        if(q < 4)
            t.arg[q] = a;
    }
}

// As SubscrTransition::write; false when the section's version can't hold it.
bool put_trans(Bytes &o, int v, const Trans &t, std::string &why)
{
    if(v < 17 && t.sfx > 255)
    {
        why = "Sound " + std::to_string(t.sfx) + " can't be a transition's sound here: it is one byte before subscreen format 17, and " +
              format_note(v) + ".";
        return false;
    }

    o.push_back((byte)t.type);
    o.push_back((byte)(t.sfx & 0xFF));

    if(v >= 17)
        o.push_back((byte)((t.sfx >> 8) & 0xFF));

    out16(o, t.flags);
    const int n = trans_args(t.type);
    o.push_back((byte)n);

    for(int q = 0; q < n; ++q)
        out32(o, t.arg[q]);

    return true;
}

// upstream's SubscrSelectorTileInfo
struct SelTile
{
    int sw, sh;
    long tile;
    int cset, frames, speed, delay;

    SelTile() : sw(16), sh(16), tile(0), cset(0), frames(1), speed(1), delay(0) {}
};

// What a subscreen stores before its pages (ZCSubscreen::read): the name, type and flags; for an
// active or map subscreen the selector override (with flag 2), the default button positions, the
// page left / right buttons and transitions and the script.
struct Head
{
    std::string name;
    int kind;
    dword flags;
    int sel_x, sel_y, sel_w, sel_h;
    SelTile sel[2];
    int def[4];
    int btn_left, btn_right;
    Trans left, right;
    int script;
    std::vector<long> args;     // the script's InitD values as stored (format 21: any number; before: 8, only with a script)
    Bytes inst;                 // format 21: the script's instance values, as stored (6 bytes each)

    Head() : kind(0), flags(0), sel_x(0), sel_y(0), sel_w(0), sel_h(0), btn_left(0), btn_right(0), script(0)
    {
        def[0] = def[1] = def[2] = def[3] = 255;
    }

    bool paged() const { return kind == 0 || kind == 3; }
};

enum { SUBFLAG_NOPAGEWRAP = 1, SUBFLAG_OVERRIDESEL = 2 };

bool head_parse(const Bytes &b, int v, Head &h)
{
    h = Head();
    Reader r(b);
    const int nlen = r.c();

    if(!r.has((size_t)nlen))
        return false;

    h.name.assign((const char *)b.data() + r.pos, (size_t)nlen);
    r.skip((size_t)nlen);
    h.kind = r.c();
    h.flags = (dword)r.l();

    if(h.paged())
    {
        if(h.flags & SUBFLAG_OVERRIDESEL)
        {
            h.sel_x = (short)r.w();
            h.sel_y = (short)r.w();
            h.sel_w = (short)r.w();
            h.sel_h = (short)r.w();
            const int n = r.c();

            for(int q = 0; q < n && !r.bad; ++q)
            {
                SelTile t;
                t.sw = r.w();
                t.sh = r.w();
                t.tile = r.l();
                t.cset = r.c();
                t.frames = r.c();
                t.speed = r.c();
                t.delay = r.c();

                if(q < 2)       // upstream keeps two
                    h.sel[q] = t;
            }
        }

        for(int q = 0; q < 4; ++q)
            h.def[q] = r.w();

        h.btn_left = r.c();
        h.btn_right = r.c();
        get_trans(r, v, h.left);
        get_trans(r, v, h.right);
        h.script = r.w();

        if(v >= 21)
        {
            const int argc = r.w();

            for(int q = 0; q < argc && !r.bad; ++q)
                h.args.push_back(r.l());

            const size_t inst = (size_t)r.w() * 6;

            if(r.has(inst))
                h.inst.assign(b.begin() + r.pos, b.begin() + r.pos + inst);

            r.skip(inst);
        }
        else if(h.script)
        {
            for(int q = 0; q < 8; ++q)
                h.args.push_back(r.l());
        }
    }

    return !r.bad && r.pos == b.size();
}

// As ZCSubscreen::write writes it, in the section's version. False (why says why) when the
// version can't hold it.
bool head_encode(const Head &h, int v, Bytes &o, std::string &why)
{
    o.clear();

    if(h.name.size() > 255)
    {
        why = "A subscreen's name holds at most 255 characters.";
        return false;
    }

    o.push_back((byte)h.name.size());
    o.insert(o.end(), h.name.begin(), h.name.end());
    o.push_back((byte)h.kind);
    out32(o, (long)h.flags);

    if(!h.paged())
        return true;

    if(h.flags & SUBFLAG_OVERRIDESEL)
    {
        out16(o, h.sel_x);
        out16(o, h.sel_y);
        out16(o, h.sel_w);
        out16(o, h.sel_h);
        o.push_back(2);

        for(int q = 0; q < 2; ++q)
        {
            const SelTile &t = h.sel[q];
            out16(o, t.sw);
            out16(o, t.sh);
            out32(o, t.tile);
            o.push_back((byte)t.cset);
            o.push_back((byte)t.frames);
            o.push_back((byte)t.speed);
            o.push_back((byte)t.delay);
        }
    }

    for(int q = 0; q < 4; ++q)
        out16(o, h.def[q]);

    o.push_back((byte)h.btn_left);
    o.push_back((byte)h.btn_right);

    if(!put_trans(o, v, h.left, why) || !put_trans(o, v, h.right, why))
        return false;

    out16(o, h.script);

    if(v >= 21)
    {
        out16(o, (int)h.args.size());

        for(size_t q = 0; q < h.args.size(); ++q)
            out32(o, h.args[q]);

        out16(o, (int)(h.inst.size() / 6));
        o.insert(o.end(), h.inst.begin(), h.inst.end());
    }
    else if(h.script)
    {
        for(size_t q = 0; q < 8; ++q)
            out32(o, q < h.args.size() ? h.args[q] : 0);
    }

    return true;
}

// Where an active or map subscreen's default buttons are in its head (0: it has none), as
// parse() finds them.
size_t head_def_at(const Bytes &head)
{
    if(head.empty())
        return 0;

    const size_t nlen = head[0];

    if(head.size() < 1 + nlen + 5 || (head[1 + nlen] != 0 && head[1 + nlen] != 3))
        return 0;

    size_t at = 1 + nlen + 1 + 4;

    if(get32(head, 1 + nlen + 1) & SUBFLAG_OVERRIDESEL)
        at += 8 + 1 + (at + 8 < head.size() ? (size_t)head[at + 8] * 12 : 0);

    return at;
}

// upstream's SubscrPageTransition: a button that changes the page
enum { PGGOTO_NONE, PGGOTO_NEXT, PGGOTO_PREV, PGGOTO_TRG, PGGOTO_CURRENT };
enum { MAX_PAGE_TRANS = 8 };    // SUBSCRPAGE_MAX_TRANSITIONS

struct PageTr
{
    int btns, mode, targ, flags;
    Trans tr;

    PageTr() : btns(0), mode(PGGOTO_TRG), targ(0), flags(0) {}
};

bool page_trans_parse(const Bytes &b, int v, std::vector<PageTr> &out)
{
    out.clear();

    if(b.empty())
        return true;

    Reader r(b);
    const int n = r.c();

    for(int i = 0; i < n && !r.bad; ++i)
    {
        PageTr t;
        t.btns = r.c();
        t.mode = r.c();
        t.targ = r.c();
        t.flags = r.c();
        get_trans(r, v, t.tr);
        out.push_back(t);
    }

    return !r.bad && r.pos == b.size();
}

bool page_trans_encode(const std::vector<PageTr> &in, int v, Bytes &o, std::string &why)
{
    o.clear();

    if(v < 16)
    {
        if(!in.empty())
        {
            why = "A page's own change buttons came with subscreen format 16 (3.0); " + format_note(v) + ".";
            return false;
        }

        return true;
    }

    o.push_back((byte)in.size());

    for(size_t i = 0; i < in.size(); ++i)
    {
        o.push_back((byte)in[i].btns);
        o.push_back((byte)in[i].mode);
        o.push_back((byte)in[i].targ);
        o.push_back((byte)in[i].flags);

        if(!put_trans(o, v, in[i].tr, why))
            return false;
    }

    return true;
}

// ---- this part's state ------------------------------------------------------------------------

enum { POP_NONE, POP_RENAME, POP_DELETE, POP_SETTINGS, POP_PAGE, POP_WIZARD, POP_DMAP };
enum { WIZ_ITEM_GRID, WIZ_COUNTER_BLOCK, WIZ_TILE_GRID };
enum { GRID_W = 16, GRID_H = 8 };

// One layer of the Map Tile Grid wizard (upstream's TileBlockLayerData).
struct TileLayer
{
    long tile[GRID_W * GRID_H];
    int cset[GRID_W * GRID_H], flip[GRID_W * GRID_H];
    bool you_are_here;
    int litems;
    unsigned scrstates, exstates, lstates;
    int floor, level;
    bool inv_litem, any_litem, inv_scr, any_scr, inv_lstate, any_lstate;

    TileLayer() : you_are_here(false), litems(0), scrstates(0), exstates(0), lstates(0), floor(-1), level(-1), inv_litem(false),
                  any_litem(false), inv_scr(false), any_scr(false), inv_lstate(false), any_lstate(false)
    {
        memset(tile, 0, sizeof(tile));
        memset(cset, 0, sizeof(cset));
        memset(flip, 0, sizeof(flip));
    }
};

// The wizards' fields, kept between uses as upstream keeps its own (with its defaults).
struct Wizard
{
    int kind;
    // Item Grid
    int ig_first, ig_x, ig_y, ig_w, ig_h, ig_dx, ig_dy, ig_conn;
    bool ig_noneq;
    // Counter Block
    int cb_x, cb_y, cb_n, cb_space, cb_tile, cb_cset, cb_corner, cb_mind, cb_maxd, cb_font, cb_shadow;
    bool cb_horz, cb_show0;
    char cb_div[257], cb_inf[4];
    int cb_col[3][2];           // text, shadow, background: colour type, colour
    // Map Tile Grid
    int tg_first, tg_x, tg_y, tg_w, tg_h, tg_map, tg_scr, tg_dx, tg_dy, tg_level, tg_conn, tg_layer, tg_cell;
    bool tg_overlay, tg_transp;
    std::vector<TileLayer> layers;

    Wizard() : kind(WIZ_ITEM_GRID), ig_first(0), ig_x(0), ig_y(0), ig_w(5), ig_h(3), ig_dx(16), ig_dy(16), ig_conn(1), ig_noneq(false),
               cb_x(0), cb_y(0), cb_n(4), cb_space(0), cb_tile(0), cb_cset(1), cb_corner(0), cb_mind(0), cb_maxd(3), cb_font(0), cb_shadow(0),
               cb_horz(false), cb_show0(true), tg_first(0), tg_x(0), tg_y(0), tg_w(1), tg_h(1), tg_map(1), tg_scr(0), tg_dx(16), tg_dy(16),
               tg_level(-1), tg_conn(0), tg_layer(0), tg_cell(0), tg_overlay(true), tg_transp(false)
    {
        strcpy(cb_div, "X");
        strcpy(cb_inf, "A");
        cb_col[0][0] = 255;     // ssctMISC, the subscreen's text colour
        cb_col[0][1] = 0;
        cb_col[1][0] = cb_col[1][1] = 0;
        cb_col[2][0] = 254;     // ssctSYSTEM, none
        cb_col[2][1] = -1;
    }
};

struct More
{
    // View
    bool show_invis, full_ctr, max_ctr, no_inf, all_inf, hints;
    int preview_dmap;           // the DMap the page is drawn for (-1: the quest's starting DMap)
    // the lists as the editor opened: how many subscreens each held
    int orig_count[4];
    // dialogs
    int popup;                  // the one open (POP_*)
    std::string dlg_error;
    char name[256];
    Head head;
    std::vector<PageTr> ptr;
    int cursor;
    int dmap_typed;
    Wizard wiz;
    int menu_x, menu_y;         // where the page's menu was opened, for the wizards it starts
    // requests to the game thread
    int want_tile;              // the tile selector: 1 for the Counter Block's tile, 2 for a Map Tile Grid cell
    int reopen;                 // the dialog to show again when the tile selector closes

    More() : show_invis(false), full_ctr(false), max_ctr(false), no_inf(false), all_inf(false), hints(false), preview_dmap(-1), popup(POP_NONE),
             cursor(0), dmap_typed(-1), menu_x(0), menu_y(0), want_tile(0), reopen(POP_NONE)
    {
        orig_count[0] = orig_count[1] = orig_count[2] = orig_count[3] = 0;
        name[0] = 0;
    }
};

More g_more;

// ---- whole subscreens ---------------------------------------------------------------------------

int list_size(const Model &m, int list)
{
    int n = 0;

    for(size_t i = 0; i < m.size(); ++i)
        n += m[i].list == list;

    return n;
}

// A subscreen's number in its own list (what a DMap calls it).
int number_in_list(const Model &m, int sub)
{
    int n = 0;

    for(int i = 0; i < sub && i < (int)m.size(); ++i)
        n += m[i].list == m[sub].list;

    return n;
}

const char *list_lower(int list)
{
    static const char *const names[4] = { "active", "passive", "overlay", "map" };
    return list >= 0 && list < 4 ? names[list] : "?";
}

// Why a list can't take one more subscreen ("" when it can).
std::string list_full(int list)
{
    Editor &e = g_ed;

    if(list == 3 && e.v < 16)
        return "Map subscreens came with subscreen format 16 (3.0); " + format_note(e.v) + ", which has no list for them.";

    if(list_size(e.m, list) >= 255)
        return std::string("A quest holds at most 255 ") + list_lower(list) + " subscreens (the file counts them in one byte).";

    return std::string();
}

// Puts a subscreen at the end of its list and shows it.
void insert_subscreen(const Sub &s)
{
    Editor &e = g_ed;
    size_t at = 0;

    while(at < e.m.size() && e.m[at].list <= s.list)
        ++at;

    remember();
    e.m.insert(e.m.begin() + at, s);
    e.clip_sub = e.clip_page = -1;      // the copied widgets' place was counted in the old list
    e.error.clear();
    e.fit = true;
    show_page((int)at, 0);
}

// Subscreen > New: an empty subscreen of one page, as upstream's do_edit_subscr makes for the
// "<New ... Subscreen>" row.
void add_subscreen(int list)
{
    Editor &e = g_ed;
    const std::string full = list_full(list);

    if(!full.empty())
    {
        e.error = full;
        return;
    }

    Sub s;
    s.list = s.kind = list;
    s.orig = -1;
    Head h;
    h.kind = list;
    h.name = std::string("New ") + list_lower(list) + " subscreen";

    if(e.v >= 21 && h.paged())
        h.args.assign(8, 0);        // script_config's run arguments

    std::string why;
    int kind = -1;

    if(!head_encode(h, e.v, s.head, why) || q255_subscreen_head_extent(s.head.data(), s.head.size(), e.v, &kind) != s.head.size() || kind != list)
    {
        e.error = why.empty() ? std::string("A new subscreen can't be laid out in this subscreen format.") : why;
        return;
    }

    s.name = h.name;
    s.def_at = head_def_at(s.head);
    Page p;
    p.cursor.assign(4, 0);

    if(e.v >= 16)
        p.trans.assign(1, 0);

    s.pages.push_back(p);
    insert_subscreen(s);
}

// Subscreen > Duplicate: a copy at the end of the same list (upstream: Copy, then Paste on the
// list's "<New ... Subscreen>" row).
void duplicate_subscreen()
{
    Editor &e = g_ed;
    const Sub *cur = cur_sub();

    if(!cur)
        return;

    const std::string full = list_full(cur->list);

    if(!full.empty())
    {
        e.error = full;
        return;
    }

    Sub s = *cur;
    s.orig = -1;
    insert_subscreen(s);
}

// Why the shown subscreen can't be deleted ("" when it can).
std::string delete_refused()
{
    Editor &e = g_ed;
    const Sub *s = cur_sub();

    if(!s)
        return "No subscreen is shown.";

    if(s->list < 2 && list_size(e.m, s->list) <= 1)
        return std::string("This is the quest's only ") + list_lower(s->list) + " subscreen: every DMap uses one, so it stays (make another first).";

    return std::string();
}

// Subscreen > Delete, once confirmed. The DMaps' numbers follow when OK puts the subscreens in
// (dmap_refs_plan below).
void delete_subscreen()
{
    Editor &e = g_ed;
    const std::string why = delete_refused();

    if(!why.empty())
    {
        e.error = why;
        return;
    }

    remember();
    e.m.erase(e.m.begin() + e.sub);
    e.clip_sub = e.clip_page = -1;
    e.error.clear();
    e.fit = true;
    show_page((std::min)(e.sub, (int)e.m.size() - 1), 0);
}

// Subscreen > Rename: the name is the first thing in the subscreen's bytes (a byte's length).
void rename_subscreen(const std::string &name)
{
    Editor &e = g_ed;
    Sub *s = cur_sub();

    if(!s || s->head.empty() || name == s->name)
        return;

    if(name.size() > 255)
    {
        e.error = "A subscreen's name holds at most 255 characters.";
        return;
    }

    const size_t old = s->head[0];

    if(s->head.size() < 1 + old)
        return;

    remember();
    Bytes head;
    head.push_back((byte)name.size());
    head.insert(head.end(), name.begin(), name.end());
    head.insert(head.end(), s->head.begin() + 1 + old, s->head.end());
    s->head.swap(head);
    s->name = name;
    s->def_at = head_def_at(s->head);
    e.error.clear();
    changed();
}

// Where the DMaps' subscreen numbers go once the lists are as the editor has them, as upstream's
// delete_subscreen moves them one deletion at a time: a number after a deleted subscreen moves
// down with its subscreen; the deleted one's own number becomes 0 (no map subscreen: -1).
struct RefMap
{
    std::vector<int> to[4];
    int gone[4];
    bool same;
};

void ref_map(const Model &m, const int *orig_count, RefMap &r)
{
    int next[4] = { 0, 0, 0, 0 };
    r.same = true;

    for(int l = 0; l < 4; ++l)
        r.to[l].assign((size_t)orig_count[l], l == 3 ? -1 : 0);

    for(size_t i = 0; i < m.size(); ++i)
    {
        const int l = m[i].list;

        if(l < 0 || l > 3)
            continue;

        if(m[i].orig >= 0 && m[i].orig < orig_count[l])
            r.to[l][m[i].orig] = next[l];

        ++next[l];
    }

    for(int l = 0; l < 4; ++l)
    {
        int kept = 0;

        for(size_t i = 0; i < m.size(); ++i)
            kept += m[i].list == l && m[i].orig >= 0;

        r.gone[l] = orig_count[l] - kept;

        // a deleted subscreen's number only moves when it wasn't 0 (or the map list's) already
        for(size_t n = 0; n < r.to[l].size(); ++n)
            r.same = r.same && r.to[l][n] == (int)n;
    }
}

int ref_to(const RefMap &r, int list, int n)
{
    if(n < 0)
        return n;

    if(n < (int)r.to[list].size())
        return r.to[list][n];

    return n - r.gone[list];    // a number past the list's end moved down at every deletion
}

// The DMaps whose subscreen numbers move, each with its record changed. False (why says why)
// when a DMap's record doesn't read.
bool dmap_refs_plan(const RefMap &r, std::vector<std::pair<int, Bytes> > &changes, std::string &why)
{
    changes.clear();

    if(r.same)
        return true;

    std::vector<std::vector<byte> > recs;
    int dv = 0;

    if(!zq255_dmap_records_all(recs, &dv))
    {
        why = "the DMaps can't be read to move their subscreen numbers";
        return false;
    }

    static const char *const key[4] = { "active_subscreen", "passive_subscreen", "overlay_subscreen", "map_subscreen" };

    for(size_t i = 0; i < recs.size(); ++i)
    {
        Tape t;
        TapeParser p(recs[i].empty() ? NULL : &recs[i][0], recs[i].size(), 0, &t);

        if(!zq_tape_dmap(p, dv) || p.bad)
        {
            why = "DMap " + std::to_string((int)i) + "'s record doesn't read, so its subscreen numbers can't move";
            return false;
        }

        bool moved = false;

        for(int l = 0; l < 4; ++l)
        {
            TapeField *f = t.find(key[l]);

            if(!f)
                continue;       // this DMap format has no such number

            const int n = (int)f->value, to = ref_to(r, l, n);

            if(to != n)
            {
                f->value = to;
                moved = true;
            }
        }

        if(moved)
        {
            Bytes out;
            t.encode(out);
            changes.push_back(std::make_pair((int)i, out));
        }
    }

    return true;
}

// After the subscreens went in: the lists as they are now are what the DMaps' numbers count in.
void refs_rebase()
{
    Editor &e = g_ed;
    int next[4] = { 0, 0, 0, 0 };

    for(size_t i = 0; i < e.m.size(); ++i)
        if(e.m[i].list >= 0 && e.m[i].list < 4)
            e.m[i].orig = next[e.m[i].list]++;

    for(int l = 0; l < 4; ++l)
        g_more.orig_count[l] = next[l];

    e.undo.clear();     // older steps count in the lists as they were
}

// ---- Paste Properties ------------------------------------------------------------------------

// upstream's SubscrWidget::copy_prop (all = false): a widget of the same type takes the copied
// one's flags, "shown when", display conditions and its type's own settings, and keeps its own
// place, size, cursor links, selected text, script, page change, selector override and label,
// and what its type's copy_prop leaves alone: a counter's counters and infinite item, a button
// counter's button and cost, a gauge's container and grid spacing, a text box's text.
bool own_in_paste(int ty, const std::string &k)
{
    static const char *const base[] = { "type", "ssotype", "x", "y", "w", "h", "label", "compat", "pos", "pos_up", "pos_down", "pos_left",
                                        "pos_right", "seltext", "selector", "script_btns", "script", "pg_mode", "pg_btns", "pg_targ" };

    for(size_t i = 0; i < sizeof(base) / sizeof(base[0]); ++i)
        if(k == base[i])
            return true;

    if(k.compare(0, 7, "script.") == 0 || k.compare(0, 3, "pt.") == 0 || (k.compare(0, 5, "initd") == 0 && k.size() == 6))
        return true;

    switch(ty)
    {
    case W_COUNTER:
        return k == "ctr0" || k == "ctr1" || k == "ctr2" || k == "item";

    case W_BTNCOUNTER:
        return k == "button" || k == "costind";

    case W_LGAUGE: case W_MGAUGE: case W_MISCGAUGE: case W_ITMCOOLDOWNGAUGE:
        return k == "container" || k == "grid_hspace" || k == "grid_vspace" || k == "grid_xoff" || k == "grid_yoff";

    case W_TEXTBOX:
        return k == "text";
    }

    return false;
}

bool paste_props_onto(const Bytes &src, Bytes &dst, int v)
{
    if(src.size() < 2 || dst.size() < 2 || src[0] != dst[0])
        return false;

    Tape from, to, laid;
    TapeParser rs(src.data(), src.size(), 0, &from), rd(dst.data(), dst.size(), 0, &to);

    if(!zq_tape_subscreen_widget(rs, v, "", false) || rs.bad || !zq_tape_subscreen_widget(rd, v, "", false) || rd.bad)
        return false;

    Tape mix;

    for(size_t i = 0; i < from.f.size(); ++i)
        if(!own_in_paste(src[0], from.f[i].key))
            mix.f.push_back(from.f[i]);

    for(size_t i = 0; i < to.f.size(); ++i)
        if(own_in_paste(src[0], to.f[i].key))
            mix.f.push_back(to.f[i]);

    TapeParser wr(&mix, &laid);

    if(!zq_tape_subscreen_widget(wr, v, "", false))
        return false;

    Bytes out;
    laid.encode(out);

    if(out.size() < 2 || out[0] != dst[0] || q255_widget_extent(&out[1], out.size() - 1, v, out[0]) != out.size() - 1)
        return false;

    dst.swap(out);
    return true;
}

// Edit > Paste properties: the copied widget's properties onto every picked widget of its type.
void paste_properties()
{
    Editor &e = g_ed;
    Page *p = cur_page();
    const std::vector<int> ws = picked();

    if(!p || ws.empty() || e.clip.empty())
        return;

    if(e.clip_v != e.v)
    {
        e.error = "The copied widget isn't in this subscreen format.";
        return;
    }

    const Bytes &src = e.clip[0];
    std::vector<Bytes> made;
    int other = 0, failed = 0;

    for(size_t i = 0; i < ws.size(); ++i)
    {
        Bytes w = p->widgets[ws[i]];

        if(w.empty() || src.empty() || w[0] != src[0])
            ++other;
        else if(!paste_props_onto(src, w, e.v))
            ++failed;

        made.push_back(w);
    }

    if((int)ws.size() == other + failed)
    {
        e.error = failed ? std::string("The copied properties can't be laid out on the picked widget in this subscreen format.") :
                  std::string("Paste properties needs a picked widget of the copied one's type (") + type_name(src.empty() ? 0 : src[0]) + ").";
        return;
    }

    remember();

    for(size_t i = 0; i < ws.size(); ++i)
        p->widgets[ws[i]] = made[i];

    e.error = other + failed ? std::to_string(other + failed) + " of the picked widgets kept their properties (another type than the copied " +
              type_name(src[0]) + ")." : std::string();
    changed();
}

// ---- the wizards -------------------------------------------------------------------------------

// Fields for new_widget, by the subscreen data editor's keys.
struct Fields
{
    Tape t;

    void num(const char *key, int64_t value)
    {
        TapeField f;
        f.key = key;
        f.value = value;
        t.f.push_back(f);
    }

    void raw(const char *key, const Bytes &b)
    {
        TapeField f;
        f.key = key;
        f.bytes = b;
        t.f.push_back(f);
    }

    void text(const char *key, const std::string &s)
    {
        raw(key, Bytes(s.begin(), s.end()));
    }
};

enum { WGEN_SELECTABLE = 0x1, WGEN_INVERT_LITEM = 0x20, WGEN_INVERT_SCRSTATE = 0x80, WGEN_ANY_LITEM = 0x200, WGEN_ANY_SCRSTATE = 0x400,
       WGEN_INVERT_LSTATE = 0x800, WGEN_ANY_LSTATE = 0x1000 };

// A grid's cursor links (upstream's "Connections"): 1 loop left/right end to end, 2 wrap at the
// sides, 3 no wrapping. pos: the cell's cursor position; x, y: its place in a grid of w by h.
void grid_links(Fields &f, int conn, int pos, int x, int y, int w, int h)
{
    f.num("pos", pos);

    switch(conn)
    {
    case 1:
        f.num("pos_up", pos + (y == 0 ? (w * (h - 1)) : -w));
        f.num("pos_down", pos + (y == h - 1 ? -(w * (h - 1)) : w));
        f.num("pos_left", pos + ((x == 0 && y == 0) ? (w * h - 1) : -1));
        f.num("pos_right", pos + ((x == w - 1 && y == h - 1) ? -(w * h - 1) : 1));
        break;

    case 2:
        f.num("pos_up", pos + (y == 0 ? (w * (h - 1)) : -w));
        f.num("pos_down", pos + (y == h - 1 ? -(w * (h - 1)) : w));
        f.num("pos_left", pos + (x == 0 ? w - 1 : -1));
        f.num("pos_right", pos + (x == w - 1 ? -(w - 1) : 1));
        break;

    default:
        f.num("pos_up", y == 0 ? 255 : pos - w);
        f.num("pos_down", y == h - 1 ? 255 : pos + w);
        f.num("pos_left", x == 0 ? 255 : pos - 1);
        f.num("pos_right", x == w - 1 ? 255 : pos + 1);
        break;
    }
}

bool wizard_make(int ty, int x, int y, const Fields &f, std::vector<Bytes> &out, std::string &why)
{
    Bytes w;

    if(!new_widget(g_ed.v, ty, x, y, w, &f.t))
    {
        why = std::string("A ") + type_name(ty) + " with these settings can't be laid out in this subscreen format.";
        return false;
    }

    out.push_back(w);
    return true;
}

// Item Grid: a grid of item slots with their cursor links.
bool wizard_item_grid(std::vector<Bytes> &out, std::string &why)
{
    const Wizard &z = g_more.wiz;

    if(z.ig_conn && z.ig_first + z.ig_w * z.ig_h - 1 > 254)
    {
        why = "The grid's cursor positions would pass 254, the last one (255 means none): start lower or make the grid smaller.";
        return false;
    }

    for(int y = 0; y < z.ig_h; ++y)
        for(int x = 0; x < z.ig_w; ++x)
        {
            Fields f;
            f.num("flags", z.ig_noneq ? 2 : 0);     // SUBSCR_CURITM_NONEQP

            if(z.ig_conn)
            {
                f.num("genflags", WGEN_SELECTABLE);
                grid_links(f, z.ig_conn, z.ig_first + z.ig_w * y + x, x, y, z.ig_w, z.ig_h);
            }

            if(!wizard_make(W_ITEMSLOT, z.ig_x + x * z.ig_dx, z.ig_y + y * z.ig_dy, f, out, why))
                return false;
        }

    return true;
}

void put_colors(Fields &f, const int col[3][2])
{
    f.num("c_text_type", col[0][0]);
    f.num("c_text", col[0][1]);
    f.num("c_shadow_type", col[1][0]);
    f.num("c_shadow", col[1][1]);
    f.num("c_bg_type", col[2][0]);
    f.num("c_bg", col[2][1]);
}

// Counter Block: for each counter a mini tile, the dividing text and the counter itself.
bool wizard_counter_block(std::vector<Bytes> &out, std::string &why)
{
    const Wizard &z = g_more.wiz;
    FONT *font = zq_subscr_view_font(z.cb_font);

    if(!font)
    {
        why = "There is no font " + std::to_string(z.cb_font) + ".";
        return false;
    }

    const int dividerw = text_length(font, z.cb_div);
    const int width = 8 + dividerw + z.cb_maxd * text_length(font, "8") + z.cb_space;
    const int height = (std::max)(8, text_height(font) + z.cb_space);
    const int yoff = (8 - text_height(font)) / 2;
    int x = z.cb_x, y = z.cb_y;

    for(int q = 0; q < z.cb_n; ++q)
    {
        Fields mt;
        mt.num("tile", z.cb_tile);
        mt.num("cs_type", z.cb_cset);       // SubscrColorInfo::set_int_cset
        mt.num("cs", 0);
        mt.num("corner", z.cb_corner);

        Fields txt;
        txt.num("font", z.cb_font);
        txt.num("shadow", z.cb_shadow);
        txt.num("align", 2);
        txt.text("text", z.cb_div);
        put_colors(txt, z.cb_col);

        Fields ctr;
        ctr.num("font", z.cb_font);
        ctr.num("shadow", z.cb_shadow);
        ctr.num("align", 0);
        ctr.num("infchar", z.cb_inf[0] ? (byte)z.cb_inf[0] : 'A');
        ctr.num("mindigits", z.cb_mind);
        ctr.num("maxdigits", z.cb_maxd);
        ctr.num("flags", z.cb_show0 ? 1 : 0);   // SUBSCR_COUNTER_SHOW0
        put_colors(ctr, z.cb_col);

        if(!wizard_make(W_MINITILE, x, y, mt, out, why) || !wizard_make(W_TEXT, x + 8 + dividerw, y + yoff, txt, out, why) ||
           !wizard_make(W_COUNTER, x + 8 + dividerw, y + yoff, ctr, out, why))
            return false;

        if(z.cb_horz)
            x += width;
        else
            y += height;
    }

    return true;
}

// Why this section's version can't hold a Map Tile Grid layer's conditions ("" when it can):
// widgets have display conditions from format 14, and the ones on maps, screens, floors, levels,
// screen states and level states from 16.
std::string layer_refused(const TileLayer &l, int v)
{
    const bool newer = l.you_are_here || l.scrstates || l.exstates || l.lstates || l.floor >= 0 || l.level >= 0;
    const bool any = newer || l.litems;

    if(any && v < 14)
        return "Widgets have display conditions (level items, visited screens...) from subscreen format 14 (3.0); " + format_note(v) +
               ", so a layer can only be tiles that always show.";

    if(newer && v < 16)
        return "Conditions on screens, screen states, level states, floors and levels came with subscreen format 16; " + format_note(v) +
               ", which holds the level item condition only.";

    if(l.litems > 0xFF && v < 15)
        return "Level items past the first eight came with subscreen format 15; " + format_note(v) + ".";

    return std::string();
}

// Map Tile Grid: layers of tile blocks, one per screen of a grid of screens, each shown by its
// layer's conditions on that screen.
bool wizard_tile_grid(std::vector<Bytes> &out, std::string &why)
{
    const Wizard &z = g_more.wiz;
    const int w = z.tg_w, h = z.tg_h;

    if(z.tg_conn && z.tg_first + w * h - 1 > 254)
    {
        why = "The grid's cursor positions would pass 254, the last one (255 means none): start lower or make the grid smaller.";
        return false;
    }

    for(size_t li = 0; li < z.layers.size(); ++li)
    {
        const TileLayer &l = z.layers[li];
        why = layer_refused(l, g_ed.v);

        if(!why.empty())
        {
            why = "Layer " + std::to_string((int)li) + ": " + why;
            return false;
        }

        for(int y = 0; y < h; ++y)
            for(int x = 0; x < w; ++x)
            {
                const int screen = z.tg_scr + x + 0x10 * y, cell = y * GRID_W + x;

                if(screen < 0 || screen >= 0x80 || !l.tile[cell])   // no such screen; tile 0: no widget
                    continue;

                Fields f;
                int gen = 0;

                if(z.tg_conn)
                {
                    gen |= WGEN_SELECTABLE;
                    grid_links(f, z.tg_conn, z.tg_first + w * y + x, x, y, w, h);
                }

                gen |= (l.inv_litem ? WGEN_INVERT_LITEM : 0) | (l.any_litem ? WGEN_ANY_LITEM : 0) | (l.inv_scr ? WGEN_INVERT_SCRSTATE : 0) |
                       (l.any_scr ? WGEN_ANY_SCRSTATE : 0) | (l.inv_lstate ? WGEN_INVERT_LSTATE : 0) | (l.any_lstate ? WGEN_ANY_LSTATE : 0);
                f.num("genflags", gen);
                f.num("tile", l.tile[cell]);
                f.num("cs_type", l.cset[cell]);
                f.num("cs", 0);
                f.num("flip", l.flip[cell]);
                f.num("flags", (z.tg_overlay ? 1 : 0) | (z.tg_transp ? 2 : 0));     // SUBSCR_TILEBL_OVERLAY, _TRANSP
                f.num("req_litems", l.litems);
                f.num("req_lvlstate", l.lstates);

                if(l.litems || l.lstates)
                    f.num("req_litem_level", z.tg_level);

                f.num("req_scrstate", l.scrstates);
                f.num("req_exstate", l.exstates);

                if(l.scrstates || l.exstates)
                {
                    f.num("req_scrstate_map", z.tg_map);
                    f.num("req_scrstate_scr", screen);
                }

                if(l.you_are_here)
                {
                    Bytes maps, screens;
                    out16(maps, z.tg_map);
                    screens.push_back((byte)screen);
                    f.raw("req_maps", maps);
                    f.raw("req_screens", screens);
                }

                if(l.floor >= 0)
                    f.raw("req_floors", Bytes(1, (byte)l.floor));

                if(l.level >= 0)
                {
                    Bytes levels;
                    out16(levels, l.level);
                    f.raw("req_levels", levels);
                }

                if(!wizard_make(W_TILEBLOCK, z.tg_x + x * z.tg_dx, z.tg_y + y * z.tg_dy, f, out, why))
                    return false;
            }
    }

    if(out.empty())
    {
        why = z.layers.empty() ? "Add a layer first: each layer's tiles become tile blocks." :
              "No layer has a tile (other than tile 0) on a screen of the grid, so there is nothing to make.";
        return false;
    }

    return true;
}

// OK in a wizard: its widgets at the end of the page, all picked (upstream's group_select_widget).
bool wizard_finish(std::string &why)
{
    Editor &e = g_ed;
    Page *p = cur_page();

    if(!p)
    {
        why = "No page is shown.";
        return false;
    }

    std::vector<Bytes> made;
    const int kind = g_more.wiz.kind;

    if(!(kind == WIZ_ITEM_GRID ? wizard_item_grid(made, why) : kind == WIZ_COUNTER_BLOCK ? wizard_counter_block(made, why) :
         wizard_tile_grid(made, why)))
        return false;

    if(p->widgets.size() + made.size() > (size_t)MAX_WIDGETS)
    {
        why = "That makes " + std::to_string((int)made.size()) + " widgets, and this page holds " + std::to_string((int)p->widgets.size()) +
              " of the " + std::to_string((int)MAX_WIDGETS) + " the player draws.";
        return false;
    }

    remember();
    clear_picks();

    for(size_t i = 0; i < made.size(); ++i)
    {
        p->widgets.push_back(made[i]);

        if(e.sel < 0)
            e.sel = (int)p->widgets.size() - 1;
        else
            e.more.insert((int)p->widgets.size() - 1);
    }

    e.error.clear();
    changed();
    return true;
}

// Edit > Wizards (and the page's menu, where the wizard starts at the click).
void open_wizard(int kind, bool at_menu)
{
    More &x = g_more;
    Wizard &z = x.wiz;
    z.kind = kind;

    if(at_menu)
    {
        int *const px = kind == WIZ_ITEM_GRID ? &z.ig_x : kind == WIZ_COUNTER_BLOCK ? &z.cb_x : &z.tg_x;
        int *const py = kind == WIZ_ITEM_GRID ? &z.ig_y : kind == WIZ_COUNTER_BLOCK ? &z.cb_y : &z.tg_y;
        *px = x.menu_x;
        *py = x.menu_y;
    }

    x.dlg_error.clear();
    x.popup = POP_WIZARD;
}

void wizard_menu(bool at_menu)
{
    if(m_item("Item Grid..."))
        open_wizard(WIZ_ITEM_GRID, at_menu);
    if(m_item("Counter Block..."))
        open_wizard(WIZ_COUNTER_BLOCK, at_menu);
    if(m_item("Map Tile Grid..."))
        open_wizard(WIZ_TILE_GRID, at_menu);
}

// ---- the dialogs --------------------------------------------------------------------------------

void open_settings()
{
    More &x = g_more;
    const Sub *s = cur_sub();

    if(!s)
        return;

    if(!head_parse(s->head, g_ed.v, x.head))
    {
        g_ed.error = "This subscreen's settings don't read as its format says.";
        return;
    }

    if(x.head.paged() && x.head.args.size() < 8)
        x.head.args.resize(8, 0);

    x.dlg_error.clear();
    x.popup = POP_SETTINGS;
}

void open_page_settings()
{
    More &x = g_more;
    const Page *p = cur_page();

    if(!p)
        return;

    if(!page_trans_parse(p->trans, g_ed.v, x.ptr))
    {
        g_ed.error = "This page's change buttons don't read as its format says.";
        return;
    }

    x.cursor = (int)get32(p->cursor, 0);
    x.dlg_error.clear();
    x.popup = POP_PAGE;
}

// A dialog of this part: opened when asked for (and again after the tile selector hid the
// editor), until pop_end closes it.
bool pop_begin(int id, const char *title)
{
    if(g_more.popup != id)
        return false;

    if(!ImGui::IsPopupOpen(title))
        ImGui::OpenPopup(title);

    // in the middle of ZQuest's window and never larger than it (it scrolls then)
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2((std::max)(200.0f, vp->WorkSize.x - 16), (std::max)(150.0f, vp->WorkSize.y - 16)));
    if(!ImGui::BeginPopupModal(title, NULL, ImGuiWindowFlags_AlwaysAutoResize))
        return false;

    ui_mark("dialog");      // its title bar
    return true;
}

void pop_end(bool close)
{
    if(close)
    {
        g_more.popup = POP_NONE;
        g_more.dlg_error.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void dlg_error_line()
{
    if(!g_more.dlg_error.empty())
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520 * zcui::scale());
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", g_more.dlg_error.c_str());
        ImGui::PopTextWrapPos();
    }
}

// OK and Cancel; 1 for OK, 2 for Cancel.
int ok_cancel(const char *ok = "OK")
{
    const float sc = zcui::scale();
    int r = 0;

    if(m_button(ok, 100 * sc))
        r = 1;

    ImGui::SameLine();

    if(m_button("Cancel##dlg", 100 * sc))
        r = 2;

    return r;
}

// A button set (upstream's _INTBTN_PANEL_HELPER); each box is marked "<id>.<button>".
void buttons_ui(const char *id, int *btns)
{
    static const char *const names[8] = { "A", "B", "L", "R", "Ex1", "Ex2", "Ex3", "Ex4" };
    ImGui::PushID(id);

    for(int q = 0; q < 8; ++q)
    {
        bool on = ((*btns >> q) & 1) != 0;

        if(q % 4)
            ImGui::SameLine();

        if(ImGui::Checkbox(names[q], &on))
            *btns = on ? (*btns | (1 << q)) : (*btns & ~(1 << q));

        ui_mark(std::string(id) + "." + names[q], (*btns >> q) & 1);
    }

    ImGui::PopID();
}

// upstream's Transition Editor (subscr_transition.cpp), in place.
void trans_ui(const char *id, Trans &t)
{
    static const char *const types[3] = { "Instant", "Slide", "Pixellate" };
    static const char *const dirs[4] = { "Up", "Down", "Left", "Right" };
    const std::string pre = std::string(id) + ".";
    ImGui::PushID(id);
    int ty = t.type >= 0 && t.type < 3 ? t.type : 0;

    if(m_combo("Type", &ty, types, 3, 150, pre) && ty != t.type)
    {
        // upstream's defaults for the type's arguments: 4 pixels a frame; 45 frames
        t.type = ty;
        t.arg[0] = t.arg[2] = t.arg[3] = 0;
        t.arg[1] = ty == TR_SLIDE ? 40000 : ty == TR_PIXEL ? 45 : 0;
    }

    m_int("Sound", &t.sfx, 0, 65535, 110, pre);

    if(t.sfx > 0 && t.sfx < WAV_COUNT && sfx_string[t.sfx])
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", sfx_string[t.sfx]);
    }

    bool keep = (t.flags & 1) != 0;

    if(m_check("Don't hide the selector", &keep, pre))
        t.flags = keep ? (t.flags | 1) : (t.flags & ~1);

    if(t.type == TR_SLIDE)
    {
        int dir = (int)t.arg[0] & 3;

        if(m_combo("Direction", &dir, dirs, 4, 100, pre))
            t.arg[0] = dir;

        double speed = t.arg[1] / 10000.0;
        ImGui::SetNextItemWidth(110 * zcui::scale());

        if(ImGui::InputDouble("Speed (pixels a frame)", &speed, 0.5, 1.0, "%.4f"))
            t.arg[1] = (long)floor((std::max)(0.5, (std::min)(999.0, speed)) * 10000.0 + 0.5);

        ui_mark(pre + "Speed");
    }
    else if(t.type == TR_PIXEL)
    {
        bool inv = (t.arg[0] & 1) != 0;
        int dur = (int)t.arg[1], xo = (int)t.arg[2], yo = (int)t.arg[3];

        if(m_check("Invert", &inv, pre))
            t.arg[0] = inv ? (t.arg[0] | 1) : (t.arg[0] & ~1L);

        m_int("Duration (frames)", &dur, 1, 3600, 110, pre);
        m_int("Pattern X offset", &xo, -99999, 99999, 110, pre);
        m_int("Pattern Y offset", &yo, -99999, 99999, 110, pre);
        t.arg[1] = dur;
        t.arg[2] = xo;
        t.arg[3] = yo;
    }

    ImGui::PopID();
}

void seltile_ui(const char *id, SelTile &s)
{
    const std::string pre = std::string(id) + ".";
    int tile = (int)s.tile, cs = s.cset & 0xF, flash = (s.cset >> 4) & 0xF;
    ImGui::PushID(id);
    m_int("Tile", &tile, 0, 999999, 110, pre);
    m_int("CSet", &cs, 0, 15, 110, pre);
    m_int("Pixel width", &s.sw, 0, 256, 110, pre);
    m_int("Pixel height", &s.sh, 0, 256, 110, pre);
    m_int("Frames", &s.frames, 0, 255, 110, pre);
    m_int("Speed", &s.speed, 0, 255, 110, pre);
    m_int("Delay", &s.delay, 0, 255, 110, pre);
    m_int("Flash CSet", &flash, 0, 13, 110, pre);
    ImGui::PopID();
    s.tile = tile;
    s.cset = cs | (flash << 4);
}

// Subscreen > Settings (upstream's Subscreen Settings dialog).
void settings_popup()
{
    More &x = g_more;
    Editor &e = g_ed;

    if(!pop_begin(POP_SETTINGS, "Subscreen Settings###zqsubsettings"))
        return;

    Sub *s = cur_sub();
    Head &h = x.head;
    const float sc = zcui::scale();
    bool close = false;

    if(!s)
        close = true;
    else
    {
        ImGui::Text("%s subscreen %d: %s", list_name(s->list), number_in_list(e.m, e.sub), s->name.c_str());
        ImGui::Separator();

        if(!h.paged())
            ImGui::TextUnformatted(s->list == 1 ? "No settings for passive subscreens yet!" : "No settings for overlay subscreens yet!");
        else if(ImGui::BeginTabBar("##settabs"))
        {
            if(ImGui::BeginTabItem("Basic"))
            {
                ui_mark("tab.Basic");
                ImGui::BeginGroup();
                ImGui::TextUnformatted("Page Left");
                ImGui::SetItemTooltip("Pressing one of these buttons changes the subscreen's page by -1.");
                buttons_ui("left", &h.btn_left);
                trans_ui("left", h.left);
                ImGui::EndGroup();
                ImGui::SameLine(0, 28 * sc);
                ImGui::BeginGroup();
                ImGui::TextUnformatted("Page Right");
                ImGui::SetItemTooltip("Pressing one of these buttons changes the subscreen's page by +1.");
                buttons_ui("right", &h.btn_right);
                trans_ui("right", h.right);
                ImGui::EndGroup();
                ImGui::Separator();
                bool nowrap = (h.flags & SUBFLAG_NOPAGEWRAP) != 0;

                if(m_check("No Page Wrap", &nowrap))
                    h.flags = nowrap ? (h.flags | SUBFLAG_NOPAGEWRAP) : (h.flags & ~(dword)SUBFLAG_NOPAGEWRAP);

                ImGui::SetItemTooltip("Paging left from the first page or right from the last does nothing.");
                ImGui::EndTabItem();
            }
            else
                ui_mark("tab.Basic");

            if(ImGui::BeginTabItem("Selector"))
            {
                ui_mark("tab.Selector");
                bool over = (h.flags & SUBFLAG_OVERRIDESEL) != 0;

                if(m_check("Override Selector", &over))
                    h.flags = over ? (h.flags | SUBFLAG_OVERRIDESEL) : (h.flags & ~(dword)SUBFLAG_OVERRIDESEL);

                ImGui::SetItemTooltip("Change the selector for this subscreen.");
                ImGui::BeginDisabled(!over);
                m_int("Selector X-offset", &h.sel_x, -256, 256);
                ImGui::SameLine(300 * sc);
                m_int("Selector Y-offset", &h.sel_y, -256, 256);
                m_int("Selector Width-offset", &h.sel_w, -256, 256);
                ImGui::SameLine(300 * sc);
                m_int("Selector Height-offset", &h.sel_h, -256, 256);
                ImGui::BeginGroup();
                ImGui::TextUnformatted("Selector 1 Graphic");
                ImGui::SetItemTooltip("The tile, CSet and size selectors use with 'Use Selector 2' unchecked.");
                seltile_ui("sel1", h.sel[0]);
                ImGui::EndGroup();
                ImGui::SameLine(300 * sc);
                ImGui::BeginGroup();
                ImGui::TextUnformatted("Selector 2 Graphic");
                ImGui::SetItemTooltip("The tile, CSet and size selectors use with 'Use Selector 2' checked.");
                seltile_ui("sel2", h.sel[1]);
                ImGui::EndGroup();
                ImGui::EndDisabled();
                ImGui::TextDisabled("Pixel width and height: the part of the tiles drawn (rounded up to whole tiles for the tile block).");
                ImGui::EndTabItem();
            }
            else
                ui_mark("tab.Selector");

            if(ImGui::BeginTabItem("Script"))
            {
                ui_mark("tab.Script");
                ImGui::TextDisabled("Both the active and the map subscreen's script run when either is open.");
                m_int("Script slot (0: none)", &h.script, 0, 65535);
                // before format 21 the file keeps a subscreen's InitD values only with a script
                const bool held = e.v >= 21 || h.script != 0;
                ImGui::BeginDisabled(!held);

                for(int q = 0; q < 8 && q < (int)h.args.size(); ++q)
                {
                    char l[32];
                    snprintf(l, sizeof(l), "InitD[%d]", q);
                    int a = (int)h.args[q];

                    if(q % 2)
                        ImGui::SameLine();

                    ImGui::SetNextItemWidth(140 * sc);

                    if(ImGui::InputInt(l, &a, 0))
                        h.args[q] = a;

                    ui_mark(l, (int)h.args[q]);
                }

                ImGui::EndDisabled();

                if(!held)
                    ImGui::TextDisabled("This subscreen format (version %d) keeps InitD values only with a script.", e.v);

                ImGui::EndTabItem();
            }
            else
                ui_mark("tab.Script");

            ImGui::EndTabBar();
        }

        dlg_error_line();
        ImGui::Separator();
        const int r = ok_cancel();

        if(r == 2)
            close = true;
        else if(r == 1)
        {
            Head out = h;

            if(out.paged() && e.v < 21 && !out.script)
                out.args.clear();

            // InitD values a format 21 section never stored stay unstored while they are 0
            Head was;
            head_parse(s->head, e.v, was);

            while(out.args.size() > was.args.size() && !out.args.empty() && out.args.back() == 0 && e.v >= 21)
                out.args.pop_back();

            Bytes now, before;
            std::string why;
            int kind = -1;

            if(!head_encode(out, e.v, now, why))
                x.dlg_error = why;
            else if(q255_subscreen_head_extent(now.data(), now.size(), e.v, &kind) != now.size() || kind != s->kind)
                x.dlg_error = "These settings don't make a subscreen the player reads; nothing changed.";
            else
            {
                // only a real change counts (and is an Undo step)
                if(!head_encode(was, e.v, before, why) || now != before)
                {
                    remember();
                    s->head = now;
                    s->def_at = head_def_at(s->head);
                    changed();
                }

                close = true;
            }
        }
    }

    pop_end(close);
}

// Page > Page settings (upstream's Subscreen Page Settings dialog; the cursor's starting
// position is the subscreen data editor's field).
void page_popup()
{
    static const char *const modes[5] = { "None", "Next", "Previous", "Target", "Current" };
    More &x = g_more;
    Editor &e = g_ed;

    if(!pop_begin(POP_PAGE, "Subscreen Page Settings###zqsubpage"))
        return;

    Page *p = cur_page();
    Sub *s = cur_sub();
    bool close = false;

    if(!p || !s)
        close = true;
    else
    {
        ImGui::Text("Page %d of %s subscreen %d: %s", e.page + 1, list_lower(s->list), number_in_list(e.m, e.sub), s->name.c_str());
        ImGui::Separator();
        m_int("Cursor starts at", &x.cursor, -32768, 32767);
        ImGui::SetItemTooltip("The selector position the cursor is on when this page opens.");
        ImGui::SeparatorText("Transitions");

        if(e.v < 16)
        {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520 * zcui::scale());
            ImGui::TextDisabled("A page's own change buttons came with subscreen format 16 (3.0); %s, which has no room for them. "
                                "The subscreen's Page Left / Page Right buttons are in Subscreen > Settings.", format_note(e.v).c_str());
            ImGui::PopTextWrapPos();
        }
        else if(!s->paged())
            ImGui::TextDisabled("Only active and map subscreens change pages.");
        else
        {
            ImGui::TextDisabled("Change the current subscreen page when a button is pressed.");
            int drop = -1, swap = -1;

            for(int i = 0; i < (int)x.ptr.size(); ++i)
            {
                PageTr &t = x.ptr[i];
                char id[24], l[64];
                snprintf(id, sizeof(id), "t%d", i);
                snprintf(l, sizeof(l), "Transition %d###tr%d", i, i);
                ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);

                if(!ImGui::TreeNode(l))
                    continue;

                const std::string pre = std::string(id) + ".";
                ImGui::BeginDisabled(i == 0);

                if(ImGui::Button("Move left"))
                    swap = i - 1;

                ui_mark(pre + "Move left");

                ImGui::EndDisabled();
                ImGui::SameLine();

                if(ImGui::Button("Delete"))
                    drop = i;

                ui_mark(pre + "Delete");

                ImGui::SameLine();
                ImGui::BeginDisabled(i + 1 >= (int)x.ptr.size());

                if(ImGui::Button("Move right"))
                    swap = i;

                ui_mark(pre + "Move right");

                ImGui::EndDisabled();
                int mode = t.mode >= 0 && t.mode < 5 ? t.mode : 0, targ = t.targ + 1;

                // upstream offers every mode but None here
                if(m_combo("Mode", &mode, modes, 5, 110, pre) && mode != PGGOTO_NONE)
                    t.mode = mode;

                ImGui::SameLine();
                ImGui::BeginDisabled(t.mode != PGGOTO_TRG);

                if(m_int("Target page", &targ, 1, MAX_PAGES, 110, pre))
                    t.targ = targ - 1;

                ImGui::EndDisabled();
                bool nowrap = (t.flags & 1) != 0, swapmap = (t.flags & 2) != 0;

                if(m_check("No Wrap", &nowrap, pre))
                    t.flags = nowrap ? (t.flags | 1) : (t.flags & ~1);

                ImGui::SameLine();

                if(m_check("Swap Map Subscreen", &swapmap, pre))
                    t.flags = swapmap ? (t.flags | 2) : (t.flags & ~2);

                ImGui::SetItemTooltip("Changes between the active and map subscreens.");
                ImGui::TextUnformatted("Page Button:");
                buttons_ui(id, &t.btns);
                ImGui::TextUnformatted("Transition Effects:");
                trans_ui(id, t.tr);
                ImGui::TreePop();
            }

            if(drop >= 0)
                x.ptr.erase(x.ptr.begin() + drop);
            else if(swap >= 0 && swap + 1 < (int)x.ptr.size())
                std::swap(x.ptr[swap], x.ptr[swap + 1]);

            if(m_button("Add Transition"))
            {
                if((int)x.ptr.size() >= MAX_PAGE_TRANS)
                    x.dlg_error = "A page holds at most 8 change buttons.";
                else
                    x.ptr.push_back(PageTr());
            }
        }

        dlg_error_line();
        ImGui::Separator();
        const int r = ok_cancel();

        if(r == 2)
            close = true;
        else if(r == 1)
        {
            Bytes trans, cursor;
            std::string why;
            out32(cursor, x.cursor);

            if(!page_trans_encode(x.ptr, e.v, trans, why))
                x.dlg_error = why;
            else if(e.v >= 16 && q255_page_trans_extent(trans.data(), trans.size(), e.v) != trans.size())
                x.dlg_error = "These settings don't make a page the player reads; nothing changed.";
            else
            {
                // a page loaded without its count byte reads as one of no change buttons
                const Bytes none(e.v >= 16 ? 1 : 0, 0);
                const bool same_trans = trans == (p->trans.empty() ? none : p->trans);

                if(cursor != p->cursor || !same_trans)
                {
                    remember();
                    p->cursor = cursor;

                    if(!same_trans)
                        p->trans = trans;

                    changed();
                }

                close = true;
            }
        }
    }

    pop_end(close);
}

void rename_popup()
{
    More &x = g_more;

    if(!pop_begin(POP_RENAME, "Rename Subscreen###zqsubrename"))
        return;

    bool close = false;

    if(ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();

    ImGui::SetNextItemWidth(320 * zcui::scale());
    const bool enter = ImGui::InputText("Name", x.name, sizeof(x.name), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ui_mark("Name", x.name);
    const int r = ok_cancel();

    if(r == 1 || enter)
    {
        rename_subscreen(x.name);
        close = true;
    }
    else if(r == 2)
        close = true;

    pop_end(close);
}

// As upstream's "Delete ... Subscreen?" question.
void delete_popup()
{
    Editor &e = g_ed;

    if(!pop_begin(POP_DELETE, "Delete Subscreen?###zqsubdelete"))
        return;

    const Sub *s = cur_sub();
    bool close = !s;

    if(s)
    {
        const int n = number_in_list(e.m, e.sub);
        ImGui::Text("Are you sure you want to delete %s Subscreen %d '%s'?", list_name(s->list), n, s->name.c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520 * zcui::scale());
        ImGui::TextDisabled(s->list == 3 ? "When OK keeps the subscreens: DMaps that use it get no map subscreen, and DMaps that use a later one "
                            "follow it down a number, as in upstream's editor." :
                            "When OK keeps the subscreens: DMaps that use it get subscreen 0 of this list, and DMaps that use a later one "
                            "follow it down a number, as in upstream's editor.");
        ImGui::PopTextWrapPos();
        const int r = ok_cancel("Delete");

        if(r == 1)
        {
            delete_subscreen();
            close = true;
        }
        else if(r == 2)
            close = true;
    }

    pop_end(close);
}

// View > Set preview DMap (upstream's "Preview DMap?": -1 for none).
void dmap_popup()
{
    More &x = g_more;

    if(!pop_begin(POP_DMAP, "Preview DMap?###zqsubdmap"))
        return;

    bool close = false;
    m_int("Preview DMap", &x.dmap_typed, -1, MAXDMAPS - 1);
    ImGui::TextDisabled("The DMap the page is drawn for (its title, maps and level); -1: the quest's starting DMap.");
    const int r = ok_cancel();

    if(r == 1)
    {
        x.preview_dmap = x.dmap_typed;
        changed();
        close = true;
    }
    else if(r == 2)
        close = true;

    pop_end(close);
}

void wizard_item_grid_ui()
{
    static const char *const conn[4] = { "Non Selectable", "Loop L/R End to End", "Wrap At Sides", "No Wrapping" };
    Wizard &z = g_more.wiz;
    ImGui::TextDisabled("Creates a grid of items, with selection directions linked between them.");
    m_int("First cursor position", &z.ig_first, 0, 254);
    ImGui::SetItemTooltip("The first selector position used by the grid. It uses as many positions in order as there are items, unless it's Non Selectable.");
    m_int("X", &z.ig_x, -15, 256);
    ImGui::SameLine();
    m_int("Y", &z.ig_y, -15, 176);
    m_int("Items across", &z.ig_w, 1, 16);
    ImGui::SameLine();
    m_int("Items down", &z.ig_h, 1, 16);
    m_int("X spacing", &z.ig_dx, 1, 32);
    ImGui::SameLine();
    m_int("Y spacing", &z.ig_dy, 1, 32);
    m_check("Non Equippable", &z.ig_noneq);
    ImGui::SetItemTooltip("Items in this grid default to being non equippable, but may still be selectable.");
    ImGui::SeparatorText("Connections");

    for(int q = 0; q < 4; ++q)
    {
        if(q % 2)
            ImGui::SameLine(220 * zcui::scale());

        if(ImGui::RadioButton(conn[q], z.ig_conn == q))
            z.ig_conn = q;

        ui_mark(conn[q], z.ig_conn == q);
    }
}

void color_ui(const char *label, int col[2])
{
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(130 * zcui::scale());
    m_int((std::string(label) + ": type").c_str(), &col[0], 0, 255, 110);
    ImGui::SameLine();
    m_int((std::string(label) + ": colour").c_str(), &col[1], -32768, 32767, 110);
    ImGui::PopID();
}

void wizard_counter_block_ui()
{
    Wizard &z = g_more.wiz;

    if(!ImGui::BeginTabBar("##cbtabs"))
        return;

    const bool block = ImGui::BeginTabItem("Block");
    ui_mark("tab.Block");

    if(block)
        ImGui::EndTabItem();

    const bool colours = ImGui::BeginTabItem("Colours");
    ui_mark("tab.Colours");

    if(colours)
        ImGui::EndTabItem();

    ImGui::EndTabBar();

    if(colours)
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 520 * zcui::scale());
        ImGui::TextDisabled("Type 0-13: a CSet (the colour is one of its 16). 254: a system colour (-1: none). 255: a subscreen colour "
                            "(0: text).");
        ImGui::PopTextWrapPos();
        color_ui("Text", z.cb_col[0]);
        color_ui("Shadow", z.cb_col[1]);
        color_ui("Background", z.cb_col[2]);
        return;
    }

    if(!block)
        return;

    ImGui::TextDisabled("Creates a block of counters: for each, a mini tile, the dividing text and the counter.");
    m_int("X", &z.cb_x, -16, 256);
    ImGui::SameLine();
    m_int("Y", &z.cb_y, -16, 176);
    m_int("Counters", &z.cb_n, 2, 32);
    ImGui::SameLine();
    m_int("Spacing", &z.cb_space, -8, 255);
    m_int("Mini tile", &z.cb_tile, 0, 999999);
    ImGui::SameLine();

    if(m_button("Pick..."))
        g_more.want_tile = 1;

    m_int("Tile CSet", &z.cb_cset, 0, 15, 110);
    ImGui::SameLine();
    m_int("Tile corner", &z.cb_corner, 0, 3, 110);
    ImGui::SetNextItemWidth(110 * zcui::scale());
    ImGui::InputText("Divider", z.cb_div, sizeof(z.cb_div));
    ui_mark("Divider");
    ImGui::SetItemTooltip("Dividing text drawn between the mini tile and the counter.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(40 * zcui::scale());
    ImGui::InputText("Infinite character", z.cb_inf, 2);
    ui_mark("Infinite character");
    m_int("Minimum digits", &z.cb_mind, 0, 5);
    ImGui::SameLine();
    m_int("Maximum digits", &z.cb_maxd, 0, 5);
    m_check("Horizontal", &z.cb_horz);
    ImGui::SetItemTooltip("The counters are spaced horizontally instead of vertically.");
    ImGui::SameLine();
    m_check("Show Zero", &z.cb_show0);
    m_int("Font", &z.cb_font, 0, 255);
    ImGui::SetItemTooltip("Upstream's font number (0: the Zelda NES font).");
    ImGui::SameLine();
    m_int("Shadow style", &z.cb_shadow, 0, 10);
    ImGui::SetItemTooltip("0 none, 1 shadow, 2 shadow (U), 3 shadow (O), 4 shadow (+), 5 shadow (X), 6 shadowed, 7 shadowed (U), "
                          "8 shadowed (O), 9 shadowed (+), 10 shadowed (X).");
}

// A set of bits as boxes; names[bit] NULL: no box.
void bits_ui(const char *id, unsigned *v, const char *const *names, int n, int per_row)
{
    int shown = 0;
    ImGui::PushID(id);

    for(int b = 0; b < n; ++b)
    {
        if(!names[b])
            continue;

        if(shown % per_row)
            ImGui::SameLine((shown % per_row) * (per_row > 3 ? 150.0f : 230.0f) * zcui::scale());

        ++shown;
        bool on = ((*v >> b) & 1) != 0;

        if(ImGui::Checkbox(names[b], &on))
            *v = on ? (*v | (1u << b)) : (*v & ~(1u << b));

        ui_mark(std::string(id) + "." + names[b], (int)((*v >> b) & 1));
    }

    ImGui::PopID();
}

void hex_ui(const char *label, unsigned *v)
{
    ImGui::SetNextItemWidth(110 * zcui::scale());
    ImGui::InputScalar(label, ImGuiDataType_U32, v, NULL, NULL, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
    ui_mark(label);
}

void wizard_tile_grid_ui()
{
    static const char *const conn[4] = { "Non Selectable", "Loop L/R End to End", "Wrap At Sides", "No Wrapping" };
    static const char *const litems[16] = { "McGuffin", "Map", "Compass", "Boss killed", "Boss key", "Custom 1", "Custom 2", "Custom 3",
                                            "Custom 4", "Custom 5", "Custom 6", "Custom 7", "Custom 8", "Custom 9", "Custom 10", "Custom 11" };
    static const char *const scrstates[32] =
    {
        "Door up", "Door down", "Door left", "Door right", "Item", "Special item", "Enemies never return",
        "Enemies don't return (temporary)", "Lock block", "Boss lock block", "Chest", "Locked chest", "Boss chest", "Secrets",
        "Visited", "Light beams", "No enemies return"
    };
    More &x = g_more;
    Wizard &z = x.wiz;
    const float sc = zcui::scale();
    const int v = g_ed.v;

    if(!ImGui::BeginTabBar("##tgtabs"))
        return;

    if(ImGui::BeginTabItem("Grid"))
    {
        ui_mark("tab.Grid");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 600 * sc);
        ImGui::TextDisabled("Creates layers of tile blocks, one block for each screen of a grid of screens. A layer shown with the map and "
                            "a layer shown for visited screens make a tile map that fills in as rooms are visited.");
        ImGui::PopTextWrapPos();
        m_int("X", &z.tg_x, -15, 256);
        ImGui::SameLine();
        m_int("Y", &z.tg_y, -15, 176);
        m_int("Screens across", &z.tg_w, 1, GRID_W);
        ImGui::SameLine();
        m_int("Screens down", &z.tg_h, 1, GRID_H);
        m_int("X spacing", &z.tg_dx, 1, 32);
        ImGui::SameLine();
        m_int("Y spacing", &z.tg_dy, 1, 32);
        m_int("Map", &z.tg_map, 1, 255);
        ImGui::SetItemTooltip("The map whose screens the conditions check.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110 * sc);
        ImGui::InputInt("Top-left screen (hex)", &z.tg_scr, 1, 16, ImGuiInputTextFlags_CharsHexadecimal);
        ui_mark("Top-left screen (hex)");
        z.tg_scr = (std::max)(0, (std::min)(0x7F, z.tg_scr));
        m_int("Level", &z.tg_level, -1, MAXLEVELS - 1);
        ImGui::SetItemTooltip("The level whose items and states the level conditions need; -1: the current level.");
        ImGui::SameLine();
        m_int("First cursor position", &z.tg_first, 0, 254);
        m_check("Overlay", &z.tg_overlay);
        ImGui::SameLine();
        m_check("Transparent", &z.tg_transp);

        for(int q = 0; q < 4; ++q)
        {
            if(q)
                ImGui::SameLine();

            if(ImGui::RadioButton(conn[q], z.tg_conn == q))
                z.tg_conn = q;

            ui_mark(conn[q], z.tg_conn == q);
        }

        if(z.tg_scr + (z.tg_w - 1) + (z.tg_h - 1) * 0x10 >= 0x80)
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "The grid passes the last screen (7F): tiles for screens past it are left out.");

        ImGui::EndTabItem();
    }
    else
        ui_mark("tab.Grid");

    const bool tab_cond = ImGui::BeginTabItem("Layer conditions");
    ui_mark("tab.Layer conditions");

    if(tab_cond)
        ImGui::EndTabItem();

    const bool tab_tiles = ImGui::BeginTabItem("Layer tiles");
    ui_mark("tab.Layer tiles");

    if(tab_tiles)
        ImGui::EndTabItem();

    ImGui::EndTabBar();

    if(!tab_cond && !tab_tiles)
        return;

    // which layer (both layer tabs)
    if(m_button("Add layer"))
    {
        z.layers.push_back(TileLayer());
        z.tg_layer = (int)z.layers.size() - 1;
    }

    for(int i = 0; i < (int)z.layers.size(); ++i)
    {
        char l[24];
        snprintf(l, sizeof(l), "Layer %d", i);
        ImGui::SameLine();

        if(ImGui::RadioButton(l, z.tg_layer == i))
            z.tg_layer = i;

        ui_mark(l, z.tg_layer == i);
    }

    if(z.tg_layer < 0 || z.tg_layer >= (int)z.layers.size())
    {
        ImGui::TextDisabled("No layer yet: each layer's tiles become tile blocks with the layer's conditions.");
        return;
    }

    TileLayer &l = z.layers[z.tg_layer];
    ImGui::SameLine();

    if(m_button("Delete layer"))
    {
        z.layers.erase(z.layers.begin() + z.tg_layer);
        z.tg_layer = (std::max)(0, z.tg_layer - 1);
        return;
    }

    const std::string refused = layer_refused(l, v);

    if(!refused.empty())
    {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 600 * sc);
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", refused.c_str());
        ImGui::PopTextWrapPos();
    }

    if(tab_cond)
    {
        ImGui::TextUnformatted("Level items needed:");
        unsigned li = (unsigned)l.litems;
        bits_ui("litem", &li, litems, v >= 15 ? 16 : 8, 4);
        l.litems = (int)li;
        m_check("Invert##li", &l.inv_litem);
        ImGui::SameLine();
        m_check("Any##li", &l.any_litem);
        ImGui::TextUnformatted("Screen states needed on each tile's screen:");
        bits_ui("scr", &l.scrstates, scrstates, 32, 3);
        hex_ui("Extra states (hex)", &l.exstates);
        ImGui::SameLine();
        m_check("Invert##sc", &l.inv_scr);
        ImGui::SameLine();
        m_check("Any##sc", &l.any_scr);
        hex_ui("Level states (hex)", &l.lstates);
        ImGui::SameLine();
        m_check("Invert##ls", &l.inv_lstate);
        ImGui::SameLine();
        m_check("Any##ls", &l.any_lstate);
        m_check("You are here", &l.you_are_here);
        ImGui::SetItemTooltip("Each tile shows only while the hero is on its screen.");
        ImGui::SameLine();
        m_int("Floor", &l.floor, -1, 255, 110);
        ImGui::SetItemTooltip("Past -1: shown only on DMaps with this floor.");
        ImGui::SameLine();
        m_int("DMap level", &l.level, -1, MAXLEVELS - 1, 110);
        ImGui::SetItemTooltip("Past -1: shown only on DMaps of this level.");
        return;
    }

    // the layer's tiles: one cell for each screen of the grid; tile 0 makes no widget
    ImGui::TextUnformatted("Tiles (click a cell to edit it; 0: none):");
    z.tg_cell = (std::max)(0, z.tg_cell);

    if(z.tg_cell % GRID_W >= z.tg_w || z.tg_cell / GRID_W >= z.tg_h)
        z.tg_cell = 0;

    for(int y = 0; y < z.tg_h; ++y)
        for(int cx = 0; cx < z.tg_w; ++cx)
        {
            const int cell = y * GRID_W + cx;
            char b[32];
            snprintf(b, sizeof(b), "%ld##c%d", l.tile[cell], cell);

            if(cx)
                ImGui::SameLine(0, 2 * sc);

            const bool cur = cell == z.tg_cell;

            if(cur)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

            if(ImGui::Button(b, ImVec2(46 * sc, 0)))
                z.tg_cell = cell;

            if(cur)
                ImGui::PopStyleColor();

            snprintf(b, sizeof(b), "cell.%d.%d", cx, y);
            ui_mark(b, (int)l.tile[cell]);
        }

    int tile = (int)l.tile[z.tg_cell];
    ImGui::Text("Screen %02X:", z.tg_scr + z.tg_cell % GRID_W + 0x10 * (z.tg_cell / GRID_W));
    ImGui::SameLine();
    m_int("Tile", &tile, 0, 999999);
    l.tile[z.tg_cell] = tile;
    ImGui::SameLine();
    m_int("CSet", &l.cset[z.tg_cell], 0, 15, 110);
    ImGui::SameLine();
    m_int("Flip", &l.flip[z.tg_cell], 0, 7, 110);

    if(m_button("Pick tile..."))
        x.want_tile = 2;

    ImGui::SameLine();

    // the grid drawn as one block of the tile page (20 tiles a row), from this cell on
    if(m_button("Fill the rest from this tile"))
    {
        const int x0 = z.tg_cell % GRID_W, y0 = z.tg_cell / GRID_W;

        for(int y = y0; y < z.tg_h; ++y)
            for(int cx = 0; cx < z.tg_w; ++cx)
            {
                if(cx < x0)
                    continue;

                const int cell = y * GRID_W + cx;
                l.tile[cell] = l.tile[z.tg_cell] + (cx - x0) + (y - y0) * 20;
                l.cset[cell] = l.cset[z.tg_cell];
                l.flip[cell] = l.flip[z.tg_cell];
            }
    }

    ImGui::SetItemTooltip("This cell and those right of and below it take the tiles that follow this one on the tile page, as one block.");
}

void wizard_popup()
{
    More &x = g_more;
    const int kind = x.wiz.kind;
    char title[96];
    snprintf(title, sizeof(title), "Subscreen Wizard (%s)###zqsubwizard",
             kind == WIZ_ITEM_GRID ? "Item Grid" : kind == WIZ_COUNTER_BLOCK ? "Counter Block" : "Map Tile Grid");

    if(!pop_begin(POP_WIZARD, title))
        return;

    bool close = false;

    if(kind == WIZ_ITEM_GRID)
        wizard_item_grid_ui();
    else if(kind == WIZ_COUNTER_BLOCK)
        wizard_counter_block_ui();
    else
        wizard_tile_grid_ui();

    dlg_error_line();
    ImGui::Separator();
    const int r = ok_cancel();

    if(r == 2)
        close = true;
    else if(r == 1)
    {
        std::string why;

        if(wizard_finish(why))
            close = true;
        else
            x.dlg_error = why;
    }

    // The tile selector is ZQuest's own page: the dialog closes for it (an open modal dialog
    // keeps the mouse and keys from everything else) and comes back when it is done.
    if(x.want_tile && !close)
    {
        x.reopen = POP_WIZARD;
        close = true;
    }

    pop_end(close);
}

// Every dialog of this part (inside the editor's window).
void more_popups()
{
    rename_popup();
    delete_popup();
    settings_popup();
    page_popup();
    dmap_popup();
    wizard_popup();
}

// ---- the menus -----------------------------------------------------------------------------------

void subscreen_menu()
{
    More &x = g_more;
    Editor &e = g_ed;
    Sub *s = cur_sub();

    if(!m_begin("Subscreen"))
        return;

    if(m_item("Settings...", NULL, false, s != NULL))
        open_settings();

    if(m_item("Rename...", NULL, false, s != NULL))
    {
        snprintf(x.name, sizeof(x.name), "%s", s->name.c_str());
        x.popup = POP_RENAME;
    }

    if(m_item("Duplicate", NULL, false, s != NULL))
        duplicate_subscreen();

    if(m_item("Delete...", NULL, false, s != NULL))
    {
        const std::string why = delete_refused();

        if(why.empty())
            x.popup = POP_DELETE;
        else
            e.error = why;
    }

    ImGui::Separator();

    for(int l = 0; l < 4; ++l)
    {
        char t[48];
        snprintf(t, sizeof(t), "New %s subscreen", list_lower(l));

        if(m_item(t))
            add_subscreen(l);
    }

    ImGui::EndMenu();
}

// View: upstream's toggles and Set Preview DMap.
void view_menu_more()
{
    More &x = g_more;
    bool ch = false;
    ch |= m_toggle("Show invisible items", &x.show_invis);
    ch |= m_toggle("Max out counters", &x.full_ctr);
    ch |= m_toggle("Max out max counters", &x.max_ctr);

    if(m_toggle("Don't show 'infinite's", &x.no_inf))
    {
        x.all_inf = x.all_inf && !x.no_inf;
        ch = true;
    }

    if(m_toggle("Show everything 'infinite'", &x.all_inf))
    {
        x.no_inf = x.no_inf && !x.all_inf;
        ch = true;
    }

    m_toggle("Show hint draws", &x.hints);
    char t[64];

    if(x.preview_dmap >= 0)
        snprintf(t, sizeof(t), "Set preview DMap (%d)...", x.preview_dmap);
    else
        snprintf(t, sizeof(t), "Set preview DMap...");

    if(ImGui::MenuItem(t))
    {
        x.dmap_typed = x.preview_dmap;
        x.popup = POP_DMAP;
    }

    ui_mark("Set preview DMap...");

    if(ch)
        changed();
}

// "Show hint draws": every widget outlined with its type's name, the ones that draw nothing too
// (upstream marks empty text and item slots this way).
void hint_draws(ImDrawList *dl, const ImVec2 &o, float z)
{
    Editor &e = g_ed;
    const Page *p = cur_page();

    if(!g_more.hints || !p || e.boxes.size() != p->widgets.size())
        return;

    for(size_t i = 0; i < e.boxes.size(); ++i)
    {
        const Box &b = e.boxes[i];
        const ImVec2 a(o.x + b.x * z, o.y + b.y * z), c(o.x + (b.x + (std::max)(b.w, 1)) * z, o.y + (b.y + (std::max)(b.h, 1)) * z);
        dl->AddRect(a, c, IM_COL32(255, 96, 200, 200));
        dl->AddText(ImVec2(a.x + 2, a.y + 1), IM_COL32(255, 160, 220, 255), type_name(p->widgets[i].empty() ? 0 : p->widgets[i][0]));
    }
}

// The editor's window as it stands, for ZQ_SUBLAYOUT_UIMAP.
void ui_map_flush()
{
    UiMap &u = g_uimap;

    if(!u.on())
        return;

    Editor &e = g_ed;
    const Sub *s = cur_sub();
    char line[768];
    snprintf(line, sizeof(line), "# sub %d page %d subs %d pages %d widgets %d sel %d picked %d zoom %d scale %.3f popup %d undo %d\n", e.sub, e.page,
             (int)e.m.size(), s ? (int)s->pages.size() : 0, widget_count(), e.sel, (int)picked().size(), e.zoom, zcui::scale(), g_more.popup,
             (int)e.undo.size());
    std::string text = line;
    text += "# error " + e.error + " | " + g_more.dlg_error + "\n";

    for(size_t i = 0; i < u.now.size(); ++i)
    {
        snprintf(line, sizeof(line), "%s\t%d\t%d\t%d\t%d\t%s\n", u.now[i].name.c_str(), (int)u.now[i].at.x, (int)u.now[i].at.y,
                 (int)u.now[i].at.z, (int)u.now[i].at.w, u.now[i].value.c_str());
        text += line;
    }

    u.now.clear();

    if(text == u.written)
        return;

    const std::string tmp = std::string(u.file) + ".tmp";

    if(FILE *f = fopen(tmp.c_str(), "wb"))
    {
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
        remove(u.file);

        if(rename(tmp.c_str(), u.file) == 0)
            u.written = text;
    }
}

// ---- the game thread's part ------------------------------------------------------------------

// The tile selector for a wizard's tile (ZQuest's own, as the combo editor uses it). True when
// one was asked for.
bool more_requests()
{
    More &x = g_more;
    Editor &e = g_ed;
    int want, tile = 0, flip = 0, cs = 0, cell = 0, layer = 0;
    {
        zcui::Lock lock;
        want = x.want_tile;
        x.want_tile = 0;
        Wizard &z = x.wiz;
        layer = z.tg_layer;
        cell = z.tg_cell;

        if(want == 2 && (layer < 0 || layer >= (int)z.layers.size() || cell < 0 || cell >= GRID_W * GRID_H))
            want = 0;

        if(!want)
            return false;

        if(want == 1)
        {
            tile = z.cb_tile;
            cs = z.cb_cset;
        }
        else
        {
            tile = (int)z.layers[layer].tile[cell];
            cs = z.layers[layer].cset[cell];
            flip = z.layers[layer].flip[cell];
        }

        e.busy = true;
    }

    // the selector is ZQuest's own page: it reads the mouse and keys itself, which a UI session
    // keeps for the UI (as the hero editor's tile buttons do)
    zcui::end_session();
    const bool ok = select_tile(tile, flip, 1, cs, true, 0, want == 2) != 0;
    zcui::begin_session();
    zcui::Lock lock;
    e.busy = false;
    e.focus = true;
    x.popup = x.reopen;
    x.reopen = POP_NONE;
    Wizard &z = x.wiz;

    if(ok && want == 1)
    {
        z.cb_tile = tile;
        z.cb_cset = cs;
    }
    else if(ok && layer < (int)z.layers.size())
    {
        z.layers[layer].tile[cell] = tile;
        z.layers[layer].cset[cell] = cs;
        z.layers[layer].flip[cell] = flip;
    }

    return true;
}

// When the editor opens.
void more_start()
{
    More &x = g_more;
    x.popup = POP_NONE;
    x.reopen = POP_NONE;
    x.want_tile = 0;
    x.dlg_error.clear();
    x.show_invis = (zinit.ss_flags & ssflagSHOWINVIS) != 0;

    for(int l = 0; l < 4; ++l)
        x.orig_count[l] = list_size(g_ed.m, l);
}

// -qst-subscreen-list: the subscreens by list with their settings and pages, and every DMap's
// subscreen numbers, as text.
void list_report(FILE *f, const Model &m, int v)
{
    fprintf(f, "format %d\n", v);

    for(int l = 0; l < 4; ++l)
    {
        fprintf(f, "%s: %d\n", list_name(l), list_size(m, l));

        for(size_t i = 0; i < m.size(); ++i)
        {
            if(m[i].list != l)
                continue;

            const Sub &s = m[i];
            Head h;
            const bool ok = head_parse(s.head, v, h);
            fprintf(f, "  %d \"%s\" pages %d flags %lu", number_in_list(m, (int)i), s.name.c_str(), (int)s.pages.size(), (unsigned long)h.flags);

            if(!ok)
                fprintf(f, " HEAD DOESN'T READ");
            else if(h.paged())
            {
                fprintf(f, " left %d tr %d/%d/%d[%ld %ld %ld %ld] right %d tr %d/%d/%d[%ld %ld %ld %ld] def %d %d %d %d script %d args",
                        h.btn_left, h.left.type, h.left.sfx, h.left.flags, h.left.arg[0], h.left.arg[1], h.left.arg[2], h.left.arg[3],
                        h.btn_right, h.right.type, h.right.sfx, h.right.flags, h.right.arg[0], h.right.arg[1], h.right.arg[2], h.right.arg[3],
                        h.def[0], h.def[1], h.def[2], h.def[3], h.script);

                for(size_t q = 0; q < h.args.size(); ++q)
                    fprintf(f, " %ld", h.args[q]);

                if(h.flags & SUBFLAG_OVERRIDESEL)
                {
                    fprintf(f, " selector %d %d %d %d", h.sel_x, h.sel_y, h.sel_w, h.sel_h);

                    for(int q = 0; q < 2; ++q)
                        fprintf(f, " [%d %d %ld %d %d %d %d]", h.sel[q].sw, h.sel[q].sh, h.sel[q].tile, h.sel[q].cset, h.sel[q].frames,
                                h.sel[q].speed, h.sel[q].delay);
                }
            }

            fprintf(f, "\n");

            for(size_t pg = 0; pg < s.pages.size(); ++pg)
            {
                const Page &p = s.pages[pg];
                std::vector<PageTr> tr;
                page_trans_parse(p.trans, v, tr);
                fprintf(f, "    page %d cursor %ld widgets %d:", (int)pg, get32(p.cursor, 0), (int)p.widgets.size());

                for(size_t w = 0; w < p.widgets.size(); ++w)
                    fprintf(f, " %d@%d,%d:%d:%ld:%ld", p.widgets[w].empty() ? 0 : p.widgets[w][0], get16s(p.widgets[w], WO_X),
                            get16s(p.widgets[w], WO_Y), p.widgets[w].size() > WO_POS ? p.widgets[w][WO_POS] : 0, get32(p.widgets[w], WO_GEN),
                            get32(p.widgets[w], WO_FLAGS));

                for(size_t t = 0; t < tr.size(); ++t)
                    fprintf(f, " | change btns %d mode %d targ %d flags %d tr %d/%d/%d[%ld %ld %ld %ld]", tr[t].btns, tr[t].mode, tr[t].targ,
                            tr[t].flags, tr[t].tr.type, tr[t].tr.sfx, tr[t].tr.flags, tr[t].tr.arg[0], tr[t].tr.arg[1], tr[t].tr.arg[2],
                            tr[t].tr.arg[3]);

                fprintf(f, "\n");
            }
        }
    }

    std::vector<std::vector<byte> > recs;
    int dv = 0;

    if(!zq255_dmap_records_all(recs, &dv))
        return;

    fprintf(f, "dmaps (format %d): active passive overlay map\n", dv);

    for(size_t i = 0; i < recs.size(); ++i)
    {
        Tape t;
        TapeParser p(recs[i].empty() ? NULL : &recs[i][0], recs[i].size(), 0, &t);

        if(!zq_tape_dmap(p, dv) || p.bad)
        {
            fprintf(f, "  %d DOESN'T READ\n", (int)i);
            continue;
        }

        fprintf(f, "  %d: %d %d %d %d\n", (int)i, (int)t.get("active_subscreen"), (int)t.get("passive_subscreen"),
                (int)t.get("overlay_subscreen", 0), (int)t.get("map_subscreen", -1));
    }
}

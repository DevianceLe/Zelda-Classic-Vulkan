// The interface profiles' dialogs (zq_ui_profile.h): under a build's interface, our editors' dialogs
// show in that build's layout. When one of our (2.53.1) dialogs opens, it is told apart from the others
// by zquiinv's 2.53.1 definitions (its rows' procedures and texts); the shown build's dialog of the same
// name (zq_ui_dialogs_table.h, from each build's own source) gives every widget the two have in common
// its place (by text, then procedure, then order); ours the build lacks are hidden; the window takes the
// build's size and title. The data stays our editor's. When the dialog ends, it is put back as it was.
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <list>
#include <string>
#include <vector>

#include "zdefs.h"
#include "jwin.h"
#include "zq_ui_profile.h"
#include "zq_ui_dialogs_table.h"

extern "C" { extern void (*zcport_dialog_layout)(DIALOG *dialog, int opened); }    // port/al/gui_engine.cpp
extern int zq_screen_w, zq_screen_h;
int d_timer_proc(int msg, DIALOG *d, int c);
int d_dummy_proc(int msg, DIALOG *d, int c);

namespace
{

struct KnownProc
{
    const char *name;
    DIALOG_PROC proc;
    bool text;      // its dp is its text
};

const KnownProc known_procs[] =
{
    { "jwin_win_proc", jwin_win_proc, true },
    { "jwin_button_proc", jwin_button_proc, true },
    { "jwin_check_proc", jwin_check_proc, true },
    { "jwin_radio_proc", jwin_radio_proc, true },
    { "jwin_text_proc", jwin_text_proc, true },
    { "jwin_ctext_proc", jwin_ctext_proc, true },
    { "jwin_rtext_proc", jwin_rtext_proc, true },
    { "jwin_edit_proc", jwin_edit_proc, false },
    { "jwin_numedit_proc", jwin_numedit_proc, false },
    { "jwin_hexedit_proc", jwin_hexedit_proc, false },
    { "jwin_droplist_proc", jwin_droplist_proc, false },
    { "jwin_list_proc", jwin_list_proc, false },
    { "jwin_abclist_proc", jwin_abclist_proc, false },
    { "jwin_tab_proc", jwin_tab_proc, false },
    { "jwin_frame_proc", jwin_frame_proc, false },
    { "jwin_textbox_proc", jwin_textbox_proc, false },
    { "jwin_slider_proc", jwin_slider_proc, false },
    { "d_keyboard_proc", d_keyboard_proc, false },
    { "d_timer_proc", d_timer_proc, false },
    { "d_dummy_proc", d_dummy_proc, false },
};

const KnownProc *known(const char *name)
{
    for(size_t i = 0; i < sizeof(known_procs) / sizeof(known_procs[0]); ++i)
        if(!strcmp(known_procs[i].name, name))
            return &known_procs[i];

    return NULL;
}

// a widget's kind, for matching across builds (1.92's d_button_proc is 2.53's jwin_button_proc)
std::string kind_of(const char *type)
{
    std::string t(type ? type : "");
    struct { const char *part; const char *kind; } kinds[] =
    {
        { "win_proc", "window" }, { "shadow_box", "window" }, { "func_button", "button" }, { "button", "button" },
        { "check", "check" }, { "radio", "radio" }, { "ctext", "text" }, { "rtext", "text" },
        { "text_proc", "text" }, { "numedit", "edit" }, { "hexedit", "edit" }, { "edit_proc", "edit" },
        { "droplist", "list" }, { "abclist", "list" }, { "list_proc", "list" }, { "tab_proc", "tab" },
        { "keyboard", "keyboard" }, { "timer", "timer" }, { "dummy", "dummy" },
    };

    for(size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i)
        if(t.find(kinds[i].part) != std::string::npos)
            return kinds[i].kind;

    return t;
}

std::string norm(const char *s)
{
    std::string out;

    for(; s && *s; ++s)
        if(isalnum((unsigned char)*s))
            out += (char)tolower((unsigned char)*s);

    return out;
}

bool placed(const ZqUiDlgWidget &w)
{
    return w.x != -32768 && w.y != -32768 && w.w != -32768 && w.h != -32768;
}

// a widget that is never seen (keys, timers, loaders: no size)
bool unseen(const ZqUiDlgWidget &w)
{
    const std::string k = kind_of(w.type);
    return k == "keyboard" || k == "timer" || k == "dummy" || (w.w <= 0 && w.h <= 0);
}

int find_def(const char *build, const char *name)
{
    for(int i = 0; i < zq_ui_dlg_use_count; ++i)
        if(!strcmp(zq_ui_dlg_uses[i].build, build) && !strcmp(zq_ui_dlg_uses[i].name, name))
            return zq_ui_dlg_uses[i].def;

    return -1;
}

// Which 2.53.1 dialog this is: its name, or NULL.
const char *identify(DIALOG *d, int rows)
{
    const char *best = NULL;
    int best_score = 0;

    for(int u = 0; u < zq_ui_dlg_use_count; ++u)
    {
        const ZqUiDlgUse &use = zq_ui_dlg_uses[u];

        if(strcmp(use.build, "2.53.1"))
            continue;

        const ZqUiDlgDef &def = zq_ui_dlg_defs[use.def];

        if(def.count != rows)
            continue;

        int score = 0, checked = 0;

        for(int i = 0; i < rows; ++i)
        {
            const ZqUiDlgWidget &w = zq_ui_dlg_widgets[def.first + i];
            const KnownProc *k = known(w.type);

            if(!k)
                continue;

            ++checked;

            if(d[i].proc != k->proc)
            {
                score -= 4;
                continue;
            }

            score += 1;

            if(k->text && w.text && d[i].dp && !strcmp((const char *)d[i].dp, w.text))
                score += 1;
        }

        if(checked >= 2 && score > best_score && score * 10 >= checked * 9)
        {
            best_score = score;
            best = use.name;
        }
    }

    return best;
}

// zquiinv's id of the shown profile's build ("1.92b114", "2.11b6", "3.0.p239"), or "" for none
std::string build_id()
{
    const char *name = zq_ui_profile_name();
    int a = 0, b = 0, n = 0;

    if(!name || !strcmp(name, "2.53.1"))
        return "";

    if(sscanf(name, "%d.%d (Beta %d)", &a, &b, &n) == 3)
        return std::to_string(a) + "." + std::to_string(b) + "b" + std::to_string(n);

    if(sscanf(name, "%d.%d Beta %d", &a, &b, &n) == 3)
        return std::to_string(a) + "." + std::to_string(b) + "b" + std::to_string(n);

    if(sscanf(name, "3.0.0-prerelease.%d", &n) == 1)
        return "3.0.p" + std::to_string(n);

    // the catalog's newest 3.0 run (prereleases 236 to 239): its newest
    if(!strcmp(name, "3.0 Nightly 2026-11-06"))
        return "3.0.p239";

    return name;
}

// the build's own name for a dialog ours is ("scrdata_dlg" was 1.92's "scrflags_dlg")
int build_def(const std::string &build, const char *name)
{
    int def = find_def(build.c_str(), name);

    if(def >= 0)
        return def;

    static const struct { const char *ours; const char *theirs; } renames[] =
    {
        { "scrdata_dlg", "scrflags_dlg" },
    };

    for(size_t i = 0; i < sizeof(renames) / sizeof(renames[0]); ++i)
        if(!strcmp(renames[i].ours, name) && (def = find_def(build.c_str(), renames[i].theirs)) >= 0)
            return def;

    return -1;
}

struct Saved
{
    DIALOG *d;
    std::vector<DIALOG> rows;
};

std::list<Saved> g_saved;           // the dialogs laid out now (nested ones too)
std::list<std::string> g_titles;    // the build's window titles shown

void lay_out(DIALOG *d)
{
    const std::string build = build_id();

    if(build.empty())
        return;

    int rows = 0;

    while(d[rows].proc)
        ++rows;

    if(rows < 2 || d[0].proc != jwin_win_proc)
        return;

    const char *name = identify(d, rows);

    if(!name)
        return;

    const int ours_def = find_def("2.53.1", name);
    const int theirs_def = build_def(build, name);

    if(ours_def < 0 || theirs_def < 0)
    {
        al_trace("zq_ui: %s: %s has no dialog %s\n", zq_ui_profile_name(), build.c_str(), name);
        return;
    }

    const ZqUiDlgWidget *ours = zq_ui_dlg_widgets + zq_ui_dlg_defs[ours_def].first;
    const ZqUiDlgWidget *theirs = zq_ui_dlg_widgets + zq_ui_dlg_defs[theirs_def].first;
    const int their_count = zq_ui_dlg_defs[theirs_def].count;

    // the build's window: its first row when that is a frame, else what its widgets cover
    int wx = 0, wy = 0, ww = 0, wh = 0, win = -1;

    for(int j = 0; j < their_count; ++j)
        if(kind_of(theirs[j].type) == "window" && placed(theirs[j]))
        {
            win = j;
            wx = theirs[j].x; wy = theirs[j].y; ww = theirs[j].w; wh = theirs[j].h;
            break;
        }

    if(win < 0)
    {
        int x1 = 32767, y1 = 32767, x2 = -32767, y2 = -32767;

        for(int j = 0; j < their_count; ++j)
            if(placed(theirs[j]) && !unseen(theirs[j]))
            {
                x1 = (std::min)(x1, (int)theirs[j].x); y1 = (std::min)(y1, (int)theirs[j].y);
                x2 = (std::max)(x2, theirs[j].x + theirs[j].w); y2 = (std::max)(y2, theirs[j].y + theirs[j].h);
            }

        if(x2 <= x1 || y2 <= y1)
            return;

        wx = x1 - 6; wy = y1 - 24; ww = x2 - x1 + 12; wh = y2 - y1 + 30;
    }

    g_saved.push_back(Saved());
    g_saved.back().d = d;
    g_saved.back().rows.assign(d, d + rows);

    // our scale now (the Large layout's dialogs are bigger) and where our window is
    const double s = ours[0].w > 0 ? (double)d[0].w / ours[0].w : 1.0;

    // matching: by kind and text, then by procedure, then by kind in order after the nearest match above
    std::vector<int> match(their_count, -1);
    std::vector<bool> taken(rows, false);
    taken[0] = true;

    if(win >= 0)
        match[win] = 0;

    for(int j = 0; j < their_count; ++j)
    {
        if(match[j] >= 0 || !theirs[j].text || !theirs[j].text[0] || unseen(theirs[j]))
            continue;

        const std::string k = kind_of(theirs[j].type), t = norm(theirs[j].text);

        for(int i = 1; i < rows; ++i)
            if(!taken[i] && kind_of(ours[i].type) == k && ours[i].text && norm(ours[i].text) == t)
            {
                match[j] = i;
                taken[i] = true;
                break;
            }
    }

    for(int pass = 0; pass < 2; ++pass)
        for(int j = 0; j < their_count; ++j)
        {
            if(match[j] >= 0 || unseen(theirs[j]))
                continue;

            const std::string k = kind_of(theirs[j].type);
            int from = 1;

            // after the row ours matched to the build's nearest matched row above this one
            for(int a = j - 1; a >= 0; --a)
                if(match[a] > 0)
                {
                    from = match[a] + 1;
                    break;
                }

            for(int n = 0; n < rows && match[j] < 0; ++n)
            {
                const int i = 1 + (from - 1 + n) % (rows - 1);

                if(taken[i] || unseen(ours[i]))
                    continue;

                const bool same = pass == 0 ? !strcmp(ours[i].type, theirs[j].type) : kind_of(ours[i].type) == k;

                // a text widget only takes one of the same text
                if(same && (pass == 0 || !(theirs[j].text && theirs[j].text[0])))
                {
                    match[j] = i;
                    taken[i] = true;
                }
            }
        }

    // our window: the build's size and title
    const int W = (int)(ww * s + 0.5), H = (int)(wh * s + 0.5);
    const int X = (zq_screen_w - W) / 2, Y = (zq_screen_h - H) / 2;
    d[0].x = X; d[0].y = Y; d[0].w = W; d[0].h = H;

    if(win >= 0 && theirs[win].text && theirs[win].text[0])
    {
        g_titles.push_back(theirs[win].text);
        d[0].dp = (void *)g_titles.back().c_str();
    }

    std::vector<bool> shown(rows, false);
    shown[0] = true;

    for(int j = 0; j < their_count; ++j)
    {
        const int i = match[j];

        if(i <= 0 || !placed(theirs[j]))
            continue;

        d[i].x = X + (int)((theirs[j].x - wx) * s + 0.5);
        d[i].y = Y + (int)((theirs[j].y - wy) * s + 0.5);

        // a text keeps its own size (its font); the rest take the build's
        if(kind_of(ours[i].type) != "text")
        {
            d[i].w = (int)(theirs[j].w * s + 0.5);
            d[i].h = (int)(theirs[j].h * s + 0.5);
        }

        d[i].flags &= ~D_HIDDEN;
        shown[i] = true;
    }

    int hidden = 0;

    for(int i = 1; i < rows; ++i)
    {
        if(shown[i] || unseen(ours[i]))
            continue;

        // tabs: the build's dialog has its own (or none): ours stop showing and hiding their pages
        if(d[i].proc == jwin_tab_proc)
            d[i].dp3 = NULL;

        d[i].flags |= D_HIDDEN;
        ++hidden;
    }

    int moved = 0;

    for(int j = 0; j < their_count; ++j)
        moved += match[j] > 0;

    al_trace("zq_ui: %s: %s laid out as %s's (%d of its widgets placed, %d of ours hidden)\n",
             zq_ui_profile_name(), name, build.c_str(), moved, hidden);
}

void put_back(DIALOG *d)
{
    for(std::list<Saved>::iterator it = g_saved.begin(); it != g_saved.end(); ++it)
        if(it->d == d)
        {
            for(size_t i = 0; i < it->rows.size(); ++i)
            {
                DIALOG &o = d[i];
                const DIALOG &w = it->rows[i];
                o.x = w.x; o.y = w.y; o.w = w.w; o.h = w.h;
                o.flags = (o.flags & ~D_HIDDEN) | (w.flags & D_HIDDEN);
                o.dp3 = w.dp3;

                if(i == 0)
                    o.dp = w.dp;
            }

            g_saved.erase(it);
            return;
        }
}

void on_dialog(DIALOG *d, int opened)
{
    if(!d)
        return;

    if(opened)
        lay_out(d);
    else
        put_back(d);
}

struct Start
{
    Start()
    {
        zcport_dialog_layout = on_dialog;
    }
} g_start;

} // namespace

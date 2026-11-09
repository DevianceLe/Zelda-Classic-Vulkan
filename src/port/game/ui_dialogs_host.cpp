// zplayer dialog host, game-thread side: zcport's dialog manager hands every
// dialog to this host (zcport/gui_host.h); the ImGui layer
// (ui_dialogs_layer.cpp) shows it. See ui_dialogs_internal.h for the
// overall design.
//
// What one update_dialog() does, in Allegro's order where Allegro has one:
//   1. waits for the next UI frame if none was built since the last update
//      (callers loop on update_dialog() without pausing);
//   2. runs the events the layer recorded: clicks become MSG_CLICK (or the
//      menu callback, with active_menu set, as jwin_menu_proc does), keys go
//      through the dialog manager's key order (focused object, keyboard
//      shortcuts, MSG_XCHAR to every object, Escape closes), value changes
//      reach the object through MSG_ZCUI_SET / MSG_ZCUI_TEXT;
//   3. reads the keyboard buffer the same way (the game simulates keys, e.g.
//      System() drops the Game menu with simulate_keypress(KEY_G << 8));
//   4. redraws: MSG_DRAW to every object after D_REDRAW (and to objects that
//      changed or are D_DIRTY), drawing into a scratch bitmap instead of the
//      screen; text loaders such as d_stringloader refresh their strings
//      this way, exactly when jwin would have redrawn;
//   5. MSG_IDLE to every object;
//   6. builds the view the layer draws next.
// D_CLOSE from any of these closes the dialog with player->obj set as
// Allegro sets it (-1 for Escape).
//
// Written from scratch for zplayer.

#define NOMINMAX

#include "jwin.h"

#include "zcport/gui_host.h"
#include "zcport/ui.h"

#ifdef ZC_ENGINE
// Tab panels of the unified engine's widget toolkit (src/port/engine/a5/dialog_support.cpp).
int zcui_engine_tab_count(DIALOG *d);
const char *zcui_engine_tab_name(DIALOG *d, int i);
int zcui_engine_tab_current(DIALOG *d);
void zcui_engine_tab_select(DIALOG *d, int i);
// Its scrolling panes.
bool zcui_engine_is_pane(DIALOG *d);
void zcui_engine_pane_state(DIALOG *d, int *x, int *max_x, int *y, int *max_y);
void zcui_engine_pane_scroll(DIALOG *d, int x, int y);
// A widget's hover text, or an empty string.
std::string zcui_engine_tooltip(DIALOG *d);
#endif
#include "ui_dialogs.h"
#include "ui_dialogs_internal.h"
#if defined(__SWITCH__)
extern bool close_button_quit;   // src/zelda.cpp
#endif

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

extern int scheme[];
int d_dummy_proc(int msg, DIALOG *d, int c);    // an invisible helper, in both programs

using namespace zcui_dlg;

namespace
{

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

Shared g_shared;
std::atomic<int> g_outside_buttons(0);
std::atomic<int> g_activity(0);
std::atomic<unsigned int> g_layer_frames(0);

struct State
{
    HostDialog *hd;             // shared with the layer
    DIALOG_PLAYER *player;
    DIALOG *dialog;
    int count;
    bool unavailable;           // no UI: the dialog closes at once
    unsigned int last_frame;
    unsigned int applied_serial;
    int focus_obj;
    int open_menu;
    BITMAP *scratch;            // where procedures draw while this dialog is up
    bool overlay;               // the editor's main dialog
    int canvas_focus;           // the K_CUSTOM object that takes the keys, or -1
    PALETTE canvas_pal;
    bool canvas_pal_valid;
    std::vector<unsigned char> canvas_prev;   // scratch pixels last shown
    unsigned int open_menu_serial;
};

std::vector<State *> g_states;          // running dialogs, oldest first
int g_next_id = 1;
bool g_layer_added = false;
bool g_session = false;                 // our UI session is open
bool g_prompt_suspended = false;        // ... and closed for a prompt
std::string g_pending_text;

bool g_canvases = false;                // ZQuest: game procedures show as pictures
float g_text_scale = 1.0f;
const DIALOG *g_overlay_dialog = NULL;
BITMAP *g_real_screen = NULL;           // the game's screen ('screen' may be a scratch)
size_t g_page_floor = 0;                // dialogs below this index are hidden by a page
struct PageFrame
{
    size_t floor;
    BITMAP *saved;
};
std::vector<PageFrame> g_pages;
std::atomic<int> g_dlg_mouse_x(0);
std::atomic<int> g_dlg_mouse_y(0);
std::atomic<int> g_dlg_mouse_b(0);
State *g_pressing = NULL;               // the dialog inside a press on a K_CUSTOM object
unsigned int g_press_frame = 0;

void refresh_while_pressing();

// While a normal dialog is on top the game's mouse functions report the
// mouse in that dialog's coordinates; the editor itself and full-screen
// pages see the real mouse.
bool dialog_mouse_active()
{
    return g_canvases && g_states.size() > g_page_floor && !g_states.back()->overlay;
}

// ---------------------------------------------------------------------------
// diagnostics: ZPLAYER_UI_LOG=1 writes what the host does to zplayer_ui.log
// ---------------------------------------------------------------------------

FILE *log_file()
{
    static int state = 0;           // 0 unknown, 1 on, 2 off
    static FILE *f = NULL;
    if(state == 0)
    {
        const char *e = getenv("ZPLAYER_UI_LOG");
        state = (e && e[0] == '1') ? 1 : 2;
        if(state == 1)
            f = fopen("zplayer_ui.log", "w");
    }
    return f;
}

void dlg_log(const char *fmt, ...)
{
    FILE *f = log_file();
    if(!f)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc(10, f);
    fflush(f);
}

// ---------------------------------------------------------------------------
// gui_mouse_*() while dialogs are up
// ---------------------------------------------------------------------------
//
// While a dialog is up the platform gives the mouse to the UI, so the game's
// gui_mouse_*() would never change. The game uses them to notice a click
// outside its menu (System() closes the menu then) and user activity
// (input_idle(): the screen saver must not start while the user works in a
// dialog, and must end when he moves). These wrappers call whatever the
// functions were (the replay module counts the polls) and add the clicks
// outside every UI window and an activity counter.

int (*g_prev_mouse_x)(void) = NULL;
int (*g_prev_mouse_y)(void) = NULL;
int (*g_prev_mouse_z)(void) = NULL;
int (*g_prev_mouse_b)(void) = NULL;
bool g_mouse_hooked = false;

int host_mouse_x(void) { return dialog_mouse_active() ? g_dlg_mouse_x.load() : (g_prev_mouse_x ? g_prev_mouse_x() : 0); }
int host_mouse_y(void) { return dialog_mouse_active() ? g_dlg_mouse_y.load() : (g_prev_mouse_y ? g_prev_mouse_y() : 0); }
int host_mouse_z(void) { return (g_prev_mouse_z ? g_prev_mouse_z() : 0) + g_activity.load(); }
int host_mouse_b(void)
{
    if(dialog_mouse_active())
    {
        refresh_while_pressing();
        return g_dlg_mouse_b.load();
    }
    return (g_prev_mouse_b ? g_prev_mouse_b() : 0) | g_outside_buttons.load();
}

void hook_mouse()
{
    if(g_mouse_hooked)
        return;
    g_prev_mouse_x = gui_mouse_x;
    g_prev_mouse_y = gui_mouse_y;
    g_prev_mouse_z = gui_mouse_z;
    g_prev_mouse_b = gui_mouse_b;
    gui_mouse_x = host_mouse_x;
    gui_mouse_y = host_mouse_y;
    gui_mouse_z = host_mouse_z;
    gui_mouse_b = host_mouse_b;
    g_mouse_hooked = true;
}

void unhook_mouse()
{
    if(!g_mouse_hooked)
        return;
    if(gui_mouse_x == host_mouse_x) gui_mouse_x = g_prev_mouse_x;
    if(gui_mouse_y == host_mouse_y) gui_mouse_y = g_prev_mouse_y;
    if(gui_mouse_z == host_mouse_z) gui_mouse_z = g_prev_mouse_z;
    if(gui_mouse_b == host_mouse_b) gui_mouse_b = g_prev_mouse_b;
    g_mouse_hooked = false;
    g_outside_buttons = 0;
}

// ---------------------------------------------------------------------------
// drawing into a scratch bitmap
// ---------------------------------------------------------------------------
//
// The host shows the dialog itself, but procedures and callbacks still draw
// when they get MSG_DRAW or a value changes (d_line_proc, the sound
// dialog's set_vol(), ...). That goes to a scratch bitmap of the screen's
// size, so the game picture under the UI stays as it was.

struct ScreenToScratch
{
    BITMAP *saved;      // what to put back, if 'screen' was replaced
    explicit ScreenToScratch(State &st, bool on = true) : saved(NULL)
    {
        if(!on || !screen || st.overlay)
            return;
        BITMAP *&s = st.scratch;
        if(!s || s->w != screen->w || s->h != screen->h ||
           bitmap_color_depth(s) != bitmap_color_depth(screen))
        {
            if(s)
                destroy_bitmap(s);
            s = create_bitmap_ex(bitmap_color_depth(screen), screen->w, screen->h);
            if(s)
                clear_to_color(s, scheme[jcBOX]);
        }
        if(s)
        {
            saved = screen;
            screen = s;
        }
    }
    ~ScreenToScratch()
    {
        // (a menu command may have given the game a new screen meanwhile)
        if(saved)
            screen = saved;
    }
};

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

int count_objects(const DIALOG *d)
{
    int n = 0;
    while(d[n].proc)
        ++n;
    return n;
}

inline bool usable(const DIALOG &d)
{
    return !(d.flags & (D_HIDDEN | D_DISABLED));
}

// A message on behalf of the player: like the dialog manager, a non-zero
// answer is collected in res and the object becomes player->obj
// (object_message() turns D_REDRAWME into D_DIRTY).
int tell(State &st, int obj, int msg, int c)
{
    int r = object_message(st.dialog + obj, msg, c);
    if(r != D_O_K)
    {
        st.player->res |= r;
        st.player->obj = obj;
    }
    return r;
}

// After the user changed something the whole dialog is redrawn (MSG_DRAW
// to every object, in this update). jwin's procedures only redraw the
// object itself, but its callbacks then draw what depends on it straight
// into the screen (the sound dialog's set_vol() prints the new volume);
// here the text loaders (d_stringloader) and the wrapping procedures
// (d_maxbombsedit_proc, ...) refresh what the view shows on their MSG_DRAW.
void changed(State &st)
{
    st.player->res |= D_REDRAW;
}

void redraw_all(State &st)
{
    ScreenToScratch to_scratch(st);
    if(g_canvases && !st.overlay && st.scratch)
        clear_to_color(st.scratch, scheme[jcBOX]);
    for(int i = 0; i < st.count; ++i)
    {
        if(st.dialog[i].flags & D_HIDDEN)
            continue;
        st.dialog[i].flags &= ~D_DIRTY;
        tell(st, i, MSG_DRAW, 0);
    }
}

void redraw_dirty(State &st)
{
    ScreenToScratch to_scratch(st);
    for(int i = 0; i < st.count; ++i)
    {
        DIALOG &d = st.dialog[i];
        if((d.flags & (D_DIRTY | D_HIDDEN)) == D_DIRTY)
        {
            d.flags &= ~D_DIRTY;
            tell(st, i, MSG_DRAW, 0);
        }
    }
}

// jwin_tab_proc shows the objects of its selected panel and hides the
// others (and those of a hidden tab control), on every message. The host
// keeps the same D_HIDDEN flags; nested tab controls come after their
// parent in the array, so one pass in order is enough.

// The object framing a dialog: the toolkit keeps its own root object before it.
int window_index(State &st)
{
#ifdef ZC_ENGINE
    if(st.count > 1 && st.dialog[0].dp3 == &newGuiMarker)
        return 1;
#else
    (void)st;
#endif
    return 0;
}

void apply_tabs(State &st)
{
    DIALOG *dlg = st.dialog;
    for(int i = 0; i < st.count; ++i)
    {
        DIALOG &t = dlg[i];
        if(!t.dp || !t.dp3 || kind_of(&t) != K_TAB)
            continue;
        TABPANEL *panel = (TABPANEL *)t.dp;
        DIALOG *base = (DIALOG *)t.dp3;
        bool hidden = (t.flags & D_HIDDEN) != 0;
        for(int p = 0; panel[p].text; ++p)
        {
            if(!panel[p].dialog)
                continue;
            bool show = !hidden && (panel[p].flags & D_SELECTED);
            for(int k = 0; panel[p].dialog[k] >= 0; ++k)
            {
                DIALOG *o = base + panel[p].dialog[k];
                if(o < dlg || o >= dlg + st.count)
                    continue;
                if(show)
                    o->flags &= ~D_HIDDEN;
                else
                    o->flags |= D_HIDDEN;
            }
        }
    }
}

// The letter after the '&' of a menu or button text (lower case), or 0.
char mnemonic_of(const char *s)
{
    for(; s && *s; ++s)
    {
        if(*s == '&')
        {
            if(s[1] == '&')
            {
                ++s;
                continue;
            }
            char c = s[1];
            if(c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
            return c;
        }
    }
    return 0;
}

// Game text (Latin-1) to UTF-8; '&' marks shortcuts, "&&" is a '&'.
std::string to_utf8(const char *s, bool shortcuts)
{
    std::string out;
    if(!s)
        return out;
    for(const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
        unsigned char c = *p;
        if(shortcuts && c == '&')
        {
            if(p[1] == '&')
            {
                out += '&';
                ++p;
            }
            continue;
        }
        if(c == '\t')
        {
            out += "    ";
            continue;
        }
        if(c < 0x80)
            out += (char)c;
        else
        {
            out += (char)(0xC0 | (c >> 6));
            out += (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// Edit text: printable ASCII only (what the edit objects accept).
std::string to_edit_text(const char *s)
{
    std::string out;
    for(const unsigned char *p = (const unsigned char *)s; p && *p; ++p)
        out += (*p >= 32 && *p < 127) ? (char)*p : '?';
    return out;
}

// ---------------------------------------------------------------------------
// keys
// ---------------------------------------------------------------------------

int lower_key(int c)
{
    if(c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    if(c >= 0xC0 && c <= 0xDE && c != 0xD7)
        return c + 0x20;
    return c;
}

// Alt+letter on a menu bar: the top-level item with that letter after '&'.
bool menu_alt_key(State &st, DIALOG &d, int ascii, int scancode)
{
    if(ascii != 0 || scancode < KEY_A || scancode > KEY_Z || !d.dp)
        return false;
    char letter = (char)('a' + (scancode - KEY_A));
    MENU *m = (MENU *)d.dp;
    for(int i = 0; m[i].text; ++i)
    {
        if(m[i].text[0] && mnemonic_of(m[i].text) == letter)
        {
            st.open_menu = i;
            ++st.open_menu_serial;
            return true;
        }
    }
    return false;
}

// A key for the dialog, in the dialog manager's order: the object with the
// focus (MSG_CHAR, MSG_UCHAR), keyboard shortcuts (the key field, letters
// without regard to case), MSG_XCHAR to every object (d_keyboard_proc;
// Alt+letter drops a menu down), and Escape closes the dialog. Enter, Space
// and the arrow keys need no default here: ImGui moves the focus and
// presses the focused widget itself.
void handle_key(State &st, int ascii, int scancode, int focus)
{
    DIALOG *dlg = st.dialog;
    int combo = (scancode << 8) | ((ascii <= 255) ? ascii : '^');
    dlg_log("  dialog %d key ascii %d scancode %d focus %d", st.hd->id, ascii, scancode, focus);

    if(focus >= 0 && focus < st.count && usable(dlg[focus]))
    {
        int r = tell(st, focus, MSG_CHAR, combo);
        if(r & (D_USED_CHAR | D_CLOSE))
            return;
        r = tell(st, focus, MSG_UCHAR, ascii);
        if(r & (D_USED_CHAR | D_CLOSE))
            return;
    }

    int want = ascii ? lower_key(ascii) : combo;
    for(int i = 0; i < st.count; ++i)
    {
        if(!usable(dlg[i]) || !dlg[i].key)
            continue;
        int k = ascii ? lower_key(dlg[i].key) : dlg[i].key;
        if(k == want)
        {
            tell(st, i, MSG_KEY, combo);
            return;
        }
    }

    for(int i = 0; i < st.count; ++i)
    {
        if(!usable(dlg[i]))
            continue;
        if(kind_of(&dlg[i]) == K_MENU)
        {
            if(menu_alt_key(st, dlg[i], ascii, scancode))
                return;
            continue;
        }
        int r = tell(st, i, MSG_XCHAR, combo);
        if(r & D_USED_CHAR)
            return;
    }

    if(ascii == 27)
    {
        st.player->res |= D_CLOSE;
        st.player->obj = -1;
    }
}

// ---------------------------------------------------------------------------
// events
// ---------------------------------------------------------------------------

void run_event(State &st, const Event &ev)
{
    DIALOG_PLAYER *p = st.player;
    dlg_log("  dialog %d event %d obj %d value %d key %d/%d", st.hd->id, (int)ev.type, ev.obj, ev.value,
            ev.ascii, ev.scancode);
    if(ev.obj >= st.count)
        return;
    DIALOG *d = (ev.obj >= 0) ? st.dialog + ev.obj : NULL;

    switch(ev.type)
    {
    case Event::CLICK:
        if(d && usable(*d))
        {
            tell(st, ev.obj, MSG_CLICK, 0);
            changed(st);
        }
        break;

    case Event::DCLICK:
        if(d && usable(*d))
            tell(st, ev.obj, MSG_DCLICK, 0);
        break;

    case Event::SET:
#ifdef ZC_ENGINE
        if(d && usable(*d) && zcui_engine_is_pane(d))
        {
            zcui_engine_pane_scroll(d, ev.value2, ev.value);
            changed(st);
            break;
        }
#endif
        if(d && usable(*d))
        {
            {
                // slider callbacks draw their value into the screen
                ScreenToScratch to_scratch(st);
                tell(st, ev.obj, MSG_ZCUI_SET, ev.value);
            }
            changed(st);
        }
        break;

    case Event::TEXT:
        if(d && usable(*d))
        {
            g_pending_text = ev.text;
            tell(st, ev.obj, MSG_ZCUI_TEXT, 0);
            changed(st);
        }
        break;

    case Event::TAB:
#ifdef ZC_ENGINE
        if(d && usable(*d) && d->dp && (d->flags & D_NEW_GUI) && kind_of(d) == K_TAB)
        {
            zcui_engine_tab_select(d, ev.value);
            changed(st);
            break;
        }
#endif
        if(d && usable(*d) && d->dp && kind_of(d) == K_TAB)
        {
            TABPANEL *panel = (TABPANEL *)d->dp;
            int n = 0;
            while(panel[n].text)
                ++n;
            if(ev.value >= 0 && ev.value < n)
            {
                for(int i = 0; i < n; ++i)
                    panel[i].flags &= ~D_SELECTED;
                panel[ev.value].flags |= D_SELECTED;
                apply_tabs(st);
                changed(st);
            }
        }
        break;

    case Event::MENU_PICK:
        // jwin_menu_proc: the chosen item becomes active_menu, its callback
        // runs, the keyboard buffer is cleared, and the callback's answer is
        // the menu object's answer.
        if(d && usable(*d) && ev.menu && ev.menu->text && !(ev.menu->flags & D_DISABLED))
        {
            active_menu = ev.menu;
            int r = ev.menu->proc ? ev.menu->proc() : D_O_K;
            clear_keybuf();
            if(kind_of(d) == K_POPUP)
            {
                popup_chosen = ev.menu;
                p->res |= D_CLOSE;
                p->obj = ev.obj;
            }
            if(r & D_REDRAWME)
            {
                d->flags |= D_DIRTY;
                r &= ~D_REDRAWME;
            }
            if(r != D_O_K)
            {
                p->res |= r;
                p->obj = ev.obj;
            }
        }
        break;

    case Event::KEY:
        handle_key(st, ev.ascii, ev.scancode, ev.obj);
        break;

    case Event::CANVAS:
        if(d && usable(*d))
        {
            // as the dialog manager: the object takes the focus if it wants it
            if(ev.obj != st.canvas_focus && (object_message(d, MSG_WANTFOCUS, 0) & D_WANTFOCUS))
            {
                if(st.canvas_focus >= 0 && st.canvas_focus < st.count)
                {
                    st.dialog[st.canvas_focus].flags &= ~D_GOTFOCUS;
                    tell(st, st.canvas_focus, MSG_LOSTFOCUS, 0);
                }
                st.canvas_focus = ev.obj;
                d->flags |= D_GOTFOCUS;
                tell(st, ev.obj, MSG_GOTFOCUS, 0);
            }
            g_pressing = &st;
            tell(st, ev.obj, MSG_CLICK, ev.value);
            g_pressing = NULL;
            changed(st);
        }
        break;

    case Event::CLOSE:
        // jwin_win_proc's close button (D_EXIT)
        if(d && (d->flags & D_EXIT))
        {
#ifdef ZC_ENGINE
            // a toolkit window asks its dialog, which decides whether to close
            if(d->flags & D_NEW_GUI)
            {
                int r = new_gui_event(d - 1, geCLOSE);
                if(r > 0)
                {
                    p->res |= r;
                    p->obj = ev.obj;
                }
                break;
            }
#endif
            p->res |= D_CLOSE;
            p->obj = ev.obj;
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// the view
// ---------------------------------------------------------------------------

void list_items(DIALOG &d, std::vector<std::string> &items)
{
    ListData *data = (ListData *)d.dp;
#ifdef ZC_ENGINE
    if(!data || !(data->unownedFunc || data->ownedFunc))
        return;
#else
    if(!data || !data->listFunc)
        return;
#endif
    int n = 0;
    data->listFunc(-1, &n);
    if(n > 8192)
        n = 8192;
    for(int i = 0; i < n; ++i)
    {
        int dummy = 0;
        const char *s = data->listFunc(i, &dummy);
        items.push_back(to_utf8(s, false));
    }
}

int add_menu(DialogView &v, MENU *m, int depth)
{
    int n = 0;
    while(m[n].text)
        ++n;
    int first = (int)v.menu.size();
    v.menu.resize(first + n);
    for(int i = 0; i < n; ++i)
    {
        MenuNode &node = v.menu[first + i];
        const char *text = m[i].text;
        node.item = &m[i];
        node.flags = m[i].flags;
        node.separator = (text[0] == 0);
        node.mnemonic = mnemonic_of(text);
        const char *tab = strchr(text, '\t');
        if(tab)
        {
            std::string head(text, tab - text);
            node.label = to_utf8(head.c_str(), true);
            node.shortcut = to_utf8(tab + 1, false);
        }
        else
            node.label = to_utf8(text, true);
        if(node.label.empty() && !node.separator)
            node.label = " ";
    }
    for(int i = 0; i < n; ++i)
    {
        if(m[i].child && depth < 8)
        {
            int c = add_menu(v, m[i].child, depth + 1);
            int cn = 0;
            while(m[i].child[cn].text)
                ++cn;
            v.menu[first + i].first_child = c;
            v.menu[first + i].child_count = cn;
        }
    }
    return first;
}

// A palette component as 0-255. The unified engine keeps full 8-bit palettes.
#ifdef ZC_ENGINE
#define PAL_8BIT(c) ((int)(c))
#else
#define PAL_8BIT(c) (((int)(c) << 2) | ((int)(c) >> 4))
#endif

unsigned int fnv(const std::vector<unsigned int> &px)
{
    unsigned int h = 2166136261u;
    for(size_t i = 0; i < px.size(); ++i)
    {
        h ^= px[i];
        h *= 16777619u;
    }
    return h;
}

int add_image(DialogView &v, BITMAP *bmp, const PALETTE pal)
{
    if(!bmp || bmp->w <= 0 || bmp->h <= 0 || bmp->w > 2048 || bmp->h > 2048)
        return -1;
    Image img;
    img.w = bmp->w;
    img.h = bmp->h;
    img.rgba.resize((size_t)img.w * img.h);
    bool eight = bitmap_color_depth(bmp) == 8;
    for(int y = 0; y < img.h; ++y)
    {
        for(int x = 0; x < img.w; ++x)
        {
            int c = getpixel(bmp, x, y);
            int r, g, b;
            if(eight)
            {
                const RGB &e = pal[c & 255];
                r = PAL_8BIT(e.r);
                g = PAL_8BIT(e.g);
                b = PAL_8BIT(e.b);
            }
            else
            {
                int depth = bitmap_color_depth(bmp);
                r = getr_depth(depth, c);
                g = getg_depth(depth, c);
                b = getb_depth(depth, c);
            }
            img.rgba[(size_t)y * img.w + x] = 0xFF000000u | ((unsigned)b << 16) | ((unsigned)g << 8) | (unsigned)r;
        }
    }
    img.hash = fnv(img.rgba);
    v.images.push_back(img);
    return (int)v.images.size() - 1;
}

// What the dialog's procedures drew, from its scratch bitmap into the picture
// the layer shows behind the widgets (self-drawn objects, and whatever the
// game's wrappers paint around standard ones): only what changed. The dialog
// background's color is transparent, so the UI's own window shows through.
void update_canvas(State &st, DialogView &v)
{
    BITMAP *b = st.scratch;
    if(!g_canvases || st.overlay || !b || bitmap_color_depth(b) != 8)
        return;

    int W = b->w * CANVAS_SCALE, H = b->h * CANVAS_SCALE;
    v.canvas_w = W;
    v.canvas_h = H;

    PALETTE pal;
    get_palette(pal);
    bool forced = !st.canvas_pal_valid || memcmp(pal, st.canvas_pal, sizeof(PALETTE)) != 0;
    if(forced)
    {
        memcpy(st.canvas_pal, pal, sizeof(PALETTE));
        st.canvas_pal_valid = true;
    }
    size_t cells = (size_t)b->w * b->h;
    if(st.canvas_prev.size() != cells)
    {
        st.canvas_prev.assign(cells, 0);
        forced = true;
    }

    unsigned int colors[256];
    for(int i = 0; i < 256; ++i)
    {
        int r = PAL_8BIT(pal[i].r);
        int g = PAL_8BIT(pal[i].g);
        int bl = PAL_8BIT(pal[i].b);
        colors[i] = 0xFF000000u | ((unsigned)bl << 16) | ((unsigned)g << 8) | (unsigned)r;
    }
    colors[scheme[jcBOX] & 255] = 0;

    zcui::Lock lock;
    HostDialog &hd = *st.hd;
    if(hd.canvas.size() != (size_t)W * H)
    {
        hd.canvas.assign((size_t)W * H, 0);
        forced = true;
    }

    {
        int x0 = v.box_x < 0 ? 0 : v.box_x, y0 = v.box_y < 0 ? 0 : v.box_y;
        int x1 = v.box_x + v.box_w > b->w ? b->w : v.box_x + v.box_w;
        int y1 = v.box_y + v.box_h > b->h ? b->h : v.box_y + v.box_h;
        if(x1 <= x0 || y1 <= y0)
            return;

        bool changed_here = forced;
        for(int y = y0; y < y1 && !changed_here; ++y)
            changed_here = memcmp(b->line[y] + x0, &st.canvas_prev[(size_t)y * b->w + x0], (size_t)(x1 - x0)) != 0;
        if(!changed_here)
            return;

        for(int y = y0; y < y1; ++y)
        {
            memcpy(&st.canvas_prev[(size_t)y * b->w + x0], b->line[y] + x0, (size_t)(x1 - x0));
            for(int x = x0; x < x1; ++x)
            {
                unsigned int c = colors[b->line[y][x]];
                for(int yy = 0; yy < CANVAS_SCALE; ++yy)
                {
                    unsigned int *out = &hd.canvas[(size_t)(y * CANVAS_SCALE + yy) * W + x * CANVAS_SCALE];
                    for(int xx = 0; xx < CANVAS_SCALE; ++xx)
                        out[xx] = c;
                }
            }
        }

        int dx0 = x0 * CANVAS_SCALE, dy0 = y0 * CANVAS_SCALE;
        int dx1 = x1 * CANVAS_SCALE, dy1 = y1 * CANVAS_SCALE;
        if(hd.dirty_x1 <= hd.dirty_x0)
        {
            hd.dirty_x0 = dx0;
            hd.dirty_y0 = dy0;
            hd.dirty_x1 = dx1;
            hd.dirty_y1 = dy1;
        }
        else
        {
            if(dx0 < hd.dirty_x0) hd.dirty_x0 = dx0;
            if(dy0 < hd.dirty_y0) hd.dirty_y0 = dy0;
            if(dx1 > hd.dirty_x1) hd.dirty_x1 = dx1;
            if(dy1 > hd.dirty_y1) hd.dirty_y1 = dy1;
        }
    }
}

void build_view(State &st, DialogView &v)
{
    DIALOG *dlg = st.dialog;
    v = DialogView();
    v.ready = true;
    v.focus_obj = st.focus_obj;
    v.open_menu = st.open_menu;
    v.open_menu_serial = st.open_menu_serial;
    v.applied_serial = st.applied_serial;
    v.overlay = st.overlay;
    v.canvas_focus = st.canvas_focus;
    v.text_scale = g_text_scale;
    if(screen)
    {
        v.screen_w = screen->w;
        v.screen_h = screen->h;
    }

    PALETTE pal;
    bool have_pal = false;

    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    bool have_box = false;

    const int win = window_index(st);
    if(st.count > win && kind_of(&dlg[win]) == K_WINDOW && !(dlg[win].flags & D_HIDDEN))
    {
        DIALOG &w = dlg[win];
        v.has_window = true;
        v.win_obj = win;
        v.alert = (w.dp3 == &alert_marker);
        v.title = to_utf8((const char *)w.dp, true);
        v.closable = (w.flags & D_EXIT) != 0;
        // without the title bar and the frame jwin draws
        int top = w.dp ? 24 : 4;
        bx0 = w.x + 3;
        by0 = w.y + top;
        bx1 = w.x + w.w - 3;
        by1 = w.y + w.h - 3;
        have_box = true;
    }

    int pane_view = -1;          // the scrolling pane the following objects are inside
    for(int i = 0; i < st.count; ++i)
    {
        DIALOG &d = dlg[i];
        int kind = kind_of(&d);
#ifdef ZC_ENGINE
        if(!(d.flags & D_SCROLLING))
            pane_view = -1;
        if(zcui_engine_is_pane(&d))
            kind = K_PANE;
#endif
        // (a pane's mouse helper covers the whole screen and draws nothing)
        if(kind == K_NONE && g_canvases && !st.overlay && d.w > 1 && d.h > 1 && d.w < 2000 &&
           d.proc != d_dummy_proc && d.proc != d_keyboard_proc)
            kind = K_CUSTOM;
        if(kind == K_NONE || (d.flags & D_HIDDEN))
            continue;
        if(kind == K_WINDOW)
            continue;
        if(kind == K_MENU || kind == K_POPUP)
        {
            if(v.menu_obj < 0 && d.dp)
            {
                v.menu_obj = i;
                v.popup = (kind == K_POPUP);
                add_menu(v, (MENU *)d.dp, 0);
                int n = 0;
                while(((MENU *)d.dp)[n].text)
                    ++n;
                v.menu_top = n;
            }
            continue;
        }

        WidgetView w;
        w.obj = i;
        w.kind = kind;
        w.x = d.x;
        w.y = d.y;
        w.w = d.w;
        w.h = d.h;
        w.flags = d.flags;
        w.d1 = d.d1;
        w.d2 = d.d2;
        w.key = d.key;
        w.pane = pane_view;
#ifdef ZC_ENGINE
        if(d.flags & D_NEW_GUI)
            w.tooltip = to_utf8(zcui_engine_tooltip(&d).c_str(), false);
        if(pane_view >= 0)
        {
            // scrolled entirely out of its pane: nothing to show or press
            const WidgetView &pn = v.widgets[pane_view];
            if(w.y >= pn.y + pn.h || w.y + std::max(w.h, 1) <= pn.y || w.x >= pn.x + pn.w || w.x + std::max(w.w, 1) <= pn.x)
                continue;
        }
        if(kind == K_TEXT)
        {
            // the toolkit's labels align their text inside their box
            int align = object_message(&d, MSG_ZCUI_ALIGN, 0);
            if(align == 2)
            {
                kind = w.kind = K_CTEXT;
                w.x += w.w / 2;
            }
            else if(align == 3)
            {
                kind = w.kind = K_RTEXT;
                w.x += w.w;
            }
        }
#endif

        switch(kind)
        {
        case K_TEXT:
        case K_CTEXT:
        case K_RTEXT:
        case K_BUTTON:
            if(kind == K_BUTTON && !d.dp)
            {
                // a button with no text of its own (an icon)
                g_pending_text.clear();
                object_message(&d, MSG_ZCUI_LABEL, 0);
                w.text = g_pending_text;
            }
            else
                w.text = to_utf8((const char *)d.dp, true);
            break;
        case K_CHECK:
        case K_RADIO:
            w.text = to_utf8((const char *)d.dp, true);
            w.value = (d.flags & D_SELECTED) ? 1 : 0;
            break;
        case K_EDIT:
        case K_NUMEDIT:
        case K_HEXEDIT:
            w.text = to_edit_text((const char *)d.dp);
            w.limit = d.d1 > 0 ? d.d1 : 0;
            if(!d.dp)
                w.flags |= JW_READONLY;
            break;
        case K_LIST:
        case K_ABCLIST:
        case K_DROPLIST:
            list_items(d, w.items);
            w.value = d.d1;
            break;
        case K_TEXTBOX:
            w.text = to_utf8((const char *)d.dp, false);
            w.wrap = !(d.flags & D_SELECTED);
            break;
        case K_SLIDER:
            w.limit = d.d1;
            w.value = d.d2;
            break;
#ifdef ZC_ENGINE
        case K_PANE:
            zcui_engine_pane_state(&d, &w.d1, &w.d2, &w.value, &w.limit);
            break;
#endif
        case K_TAB:
#ifdef ZC_ENGINE
            if(d.dp && (d.flags & D_NEW_GUI))
            {
                for(int p = 0; p < zcui_engine_tab_count(&d); ++p)
                    w.items.push_back(to_utf8(zcui_engine_tab_name(&d, p), true));
                w.value = zcui_engine_tab_current(&d);
            }
            else
#endif
            if(d.dp)
            {
                TABPANEL *panel = (TABPANEL *)d.dp;
                for(int p = 0; panel[p].text; ++p)
                {
                    w.items.push_back(to_utf8(panel[p].text, true));
                    if(panel[p].flags & D_SELECTED)
                        w.value = p;
                }
            }
            break;
        case K_BITMAP:
            if(!have_pal)
            {
                get_palette(pal);
                have_pal = true;
            }
            w.image = add_image(v, (BITMAP *)d.dp, pal);
            if(w.image < 0)
                continue;
            break;
        }

        // keep everything visible inside the area shown (what's in a scrolling
        // pane is clipped to it, however far it reaches)
        if(w.pane < 0 && (w.w > 0 || w.h > 0 || kind == K_TEXT || kind == K_CTEXT || kind == K_RTEXT))
        {
            int x0 = w.x, y0 = w.y, x1 = w.x + w.w, y1 = w.y + (w.h > 0 ? w.h : 8);
            if(kind == K_CTEXT)
            {
                x0 = w.x - w.w / 2;
                x1 = w.x + w.w / 2;
            }
            else if(kind == K_RTEXT)
            {
                x0 = w.x - w.w;
                x1 = w.x;
            }
            // the editor hides an object by moving it out of the window
            if(g_canvases && v.has_window && !v.alert &&
               (x0 >= dlg[win].x + dlg[win].w || y0 >= dlg[win].y + dlg[win].h || x1 < dlg[win].x || y1 < dlg[win].y))
                continue;
            if(!have_box)
            {
                bx0 = x0;
                by0 = y0;
                bx1 = x1;
                by1 = y1;
                have_box = true;
            }
            else if(!v.has_window)
            {
                if(x0 < bx0) bx0 = x0;
                if(y0 < by0) by0 = y0;
                if(x1 > bx1) bx1 = x1;
                if(y1 > by1) by1 = y1;
            }
            else
            {
                // objects reaching past the window's frame widen the area
                // (only to the right and down: the frame's top is the title)
                if(x1 > bx1) bx1 = x1;
                if(y1 > by1) by1 = y1;
                if(x0 < bx0) bx0 = x0;
                if(y0 < by0) by0 = y0;
            }
        }
        v.widgets.push_back(w);
        if(kind == K_PANE)
            pane_view = (int)v.widgets.size() - 1;
    }

    v.box_x = bx0;
    v.box_y = by0;
    v.box_w = bx1 > bx0 ? bx1 - bx0 : 1;
    v.box_h = by1 > by0 ? by1 - by0 : 1;
    update_canvas(st, v);
}

void publish_view(State &st)
{
    DialogView v;
    build_view(st, v);
    zcui::Lock lock;
    std::swap(st.hd->view, v);
}

// A procedure tracking a press polls the mouse in its own loop: show what it
// draws meanwhile, once per UI frame.
void refresh_while_pressing()
{
    if(!g_pressing)
        return;
    unsigned int frame = g_layer_frames.load();
    if(frame != g_press_frame)
    {
        g_press_frame = frame;
        publish_view(*g_pressing);
    }
}

// ---------------------------------------------------------------------------
// sessions
// ---------------------------------------------------------------------------

bool ui_available()
{
    if(zcui::available())
        return true;
    // the presenter may still be starting (a dialog right after
    // set_gfx_mode); give it a moment, once
    static bool gave_up = false;
    if(gave_up)
        return false;
    for(int i = 0; i < 100 && !zcui::available(); ++i)
        rest(20);
    if(zcui::available())
        return true;
    gave_up = true;
    al_trace("zplayer: no UI to show dialogs on; dialogs are cancelled\n");
    return false;
}

void open_session()
{
    if(!g_session && !g_prompt_suspended)
    {
        zcui::begin_session();
        g_session = true;
    }
}

void close_session()
{
    if(g_session)
    {
        zcui::end_session();
        g_session = false;
    }
}

// The UI takes the input while a dialog that is not hidden needs it; the
// editor's main dialog does not.
void update_session()
{
    bool want = false;
    for(size_t i = g_page_floor; i < g_states.size(); ++i)
        if(!g_states[i]->overlay)
            want = true;
    if(want)
        open_session();
    else
        close_session();
}

// ---------------------------------------------------------------------------
// the host
// ---------------------------------------------------------------------------

void *host_start(DIALOG_PLAYER *player, int focus_obj)
{
    if(!g_layer_added)
    {
        zcui::add_layer(layer, NULL, 100);
        g_layer_added = true;
    }

    State *st = new State();
    st->player = player;
    st->dialog = player->dialog;
    st->count = count_objects(player->dialog);
    st->unavailable = !ui_available();
    st->last_frame = g_layer_frames.load();
    st->applied_serial = 0;
    st->focus_obj = -1;
    st->open_menu = -1;
    st->open_menu_serial = 0;
    st->overlay = g_canvases && player->dialog == g_overlay_dialog;
    st->canvas_focus = -1;
    if(g_states.empty() && screen)
        g_real_screen = screen;
    st->hd = new HostDialog();
    st->hd->id = g_next_id++;

    // as init_dialog(): MSG_START, then the focus goes to focus_obj if it
    // wants it (no MSG_GOTFOCUS)
    int obj = -1;
    int r = dialog_message(st->dialog, MSG_START, 0, &obj);
    if(r != D_O_K)
    {
        player->res |= r;
        player->obj = obj;
    }
    for(int i = 0; i < st->count; ++i)
        st->dialog[i].flags &= ~D_GOTFOCUS;
    if(focus_obj >= 0 && focus_obj < st->count &&
       (object_message(st->dialog + focus_obj, MSG_WANTFOCUS, 0) & D_WANTFOCUS))
    {
        st->dialog[focus_obj].flags |= D_GOTFOCUS;
        player->focus_obj = focus_obj;
        st->focus_obj = focus_obj;
    }
    apply_tabs(*st);

    dlg_log("start dialog %d: %d objects, kind of [0] %d, focus %d%s", st->hd->id, st->count,
            st->count > 0 ? kind_of(&st->dialog[0]) : -1, focus_obj, st->unavailable ? " (no UI)" : "");
    bool first = g_states.empty();
    g_states.push_back(st);
    {
        zcui::Lock lock;
        g_shared.stack.push_back(st->hd);
    }
    if(first)
        hook_mouse();
    update_session();
    zcui::request_redraw();
    return st;
}

int host_update(void *state, DIALOG_PLAYER *p)
{
    State &st = *(State *)state;

    if(st.unavailable)
    {
        p->obj = -1;
        return FALSE;
    }

#if defined(__SWITCH__)
    // HOME is closing the software: the game has to get back to its loop.
    if(close_button_quit)
    {
        p->obj = -1;
        return FALSE;
    }
#endif

    if(st.overlay && screen)
        g_real_screen = screen;

    // ZQuest's procedures draw at any time, not only for MSG_DRAW
    ScreenToScratch keep_off_screen(st, g_canvases);

    // one step per UI frame (the editor's main loop paces itself)
    if(!st.overlay && g_layer_frames.load() == st.last_frame)
        zcui::wait_frame(40);
    st.last_frame = g_layer_frames.load();

    // as the dialog manager does each update (the replay module notices
    // an open dialog by these polls)
    (void)gui_mouse_x();
    (void)gui_mouse_y();
    (void)gui_mouse_z();
    (void)gui_mouse_b();

    std::vector<Event> events;
    {
        zcui::Lock lock;
        events.swap(st.hd->events);
    }

    p->res &= ~D_USED_CHAR;
    apply_tabs(st);

    for(size_t i = 0; i < events.size(); ++i)
    {
        st.applied_serial = events[i].serial;
        if(p->res & D_CLOSE)
            continue;
        run_event(st, events[i]);
    }

    while(!(p->res & D_CLOSE) && keypressed())
    {
        int scancode = 0;
        int ascii = ureadkey(&scancode);
        handle_key(st, ascii, scancode, -1);
    }

    if(!(p->res & D_CLOSE))
    {
        apply_tabs(st);
        if(p->res & (D_REDRAW | D_REDRAW_ALL))
        {
            p->res &= ~(D_REDRAW | D_REDRAW_ALL);
            redraw_all(st);
        }
        redraw_dirty(st);

        int obj = -1;
        int r = dialog_message(st.dialog, MSG_IDLE, 0, &obj);
        if(r != D_O_K)
        {
            p->res |= r;
            p->obj = obj;
        }
        redraw_dirty(st);
    }
    p->res &= ~(D_WANTFOCUS | D_USED_CHAR);

    publish_view(st);

    if(p->res & D_CLOSE)
    {
        dlg_log("  dialog %d closes, obj %d", st.hd->id, p->obj);
        p->res &= ~D_CLOSE;
        return FALSE;
    }
    return 1;
}

void host_end(void *state, DIALOG_PLAYER *p)
{
    State *st = (State *)state;
    dlg_log("end dialog %d, obj %d", st->hd->id, p->obj);

    int obj = -1;
    dialog_message(p->dialog, MSG_END, 0, &obj);

    {
        zcui::Lock lock;
        std::vector<HostDialog *> &s = g_shared.stack;
        for(size_t i = 0; i < s.size(); ++i)
        {
            if(s[i] == st->hd)
            {
                s.erase(s.begin() + i);
                break;
            }
        }
    }
    {
        zcui::Lock lock;
        canvas_release(st->hd);
    }
    if(st->scratch)
        destroy_bitmap(st->scratch);
    delete st->hd;
    for(size_t i = 0; i < g_states.size(); ++i)
    {
        if(g_states[i] == st)
        {
            g_states.erase(g_states.begin() + i);
            break;
        }
    }
    delete st;

    update_session();
    if(g_states.empty())
    {
        close_session();
        unhook_mouse();
    }
    zcui::request_redraw();
}

const ZCPORT_GUI_HOST g_host_table = { host_start, host_update, host_end };

struct Registrar
{
    Registrar() { zcport_set_gui_host(&g_host_table); }
} g_registrar;

} // namespace

// ---------------------------------------------------------------------------
// shared with the markers and the layer
// ---------------------------------------------------------------------------

int zcui_dlg::kind_of(DIALOG *d)
{
    if(!d || !d->proc)
        return K_NONE;
    if(d->proc == d_bitmap_proc)
        return K_BITMAP;
    static std::map<DIALOG_PROC, int> known;
    std::map<DIALOG_PROC, int>::iterator it = known.find(d->proc);
    if(it != known.end())
        return it->second;
    int r = d->proc(MSG_ZCUI_KIND, d, 0);
    int kind = ((r & KIND_MASK) == KIND_MAGIC) ? (r & ~KIND_MASK) : K_NONE;
    known[d->proc] = kind;
    return kind;
}

const char *zcui_dlg::pending_text()
{
    return g_pending_text.c_str();
}

void zcui_dlg::set_pending_text(const std::string &s)
{
    g_pending_text = s;
}

MENU *zcui_dlg::popup_chosen = NULL;

void zcui_dlg::set_dialog_mouse(int x, int y, int buttons)
{
    g_dlg_mouse_x = x;
    g_dlg_mouse_y = y;
    g_dlg_mouse_b = buttons;
}

void zcui_dialogs_enable_canvases(float text_scale)
{
    g_canvases = true;
    g_text_scale = text_scale;
}

// Drops down a top-level menu of the newest dialog, as Alt+letter would.
void zcui_dialogs_open_menu(int index)
{
    if(g_states.empty())
        return;
    g_states.back()->open_menu = index;
    ++g_states.back()->open_menu_serial;
}

void zcui_dialogs_mark_overlay(DIALOG *dialog)
{
    g_overlay_dialog = dialog;
}

void zcui_page_begin()
{
    if(!g_canvases)
        return;
    PageFrame f;
    f.floor = g_page_floor;
    f.saved = screen;
    g_pages.push_back(f);
    g_page_floor = g_states.size();
    {
        zcui::Lock lock;
        g_shared.floor = g_page_floor;
    }
    if(g_real_screen)
        screen = g_real_screen;
    update_session();
    zcui::request_redraw();
}

void zcui_page_end()
{
    if(!g_canvases || g_pages.empty())
        return;
    PageFrame f = g_pages.back();
    g_pages.pop_back();
    g_page_floor = f.floor;
    {
        zcui::Lock lock;
        g_shared.floor = g_page_floor;
    }
    screen = f.saved;
    update_session();
    zcui::request_redraw();
}

Shared &zcui_dlg::shared()
{
    return g_shared;
}

int zcui_dlg::outside_buttons() { return g_outside_buttons.load(); }
int zcui_dlg::activity() { return g_activity.load(); }
void zcui_dlg::set_outside_buttons(int b) { g_outside_buttons = b; }
void zcui_dlg::add_activity() { ++g_activity; }
unsigned int zcui_dlg::layer_frames() { return g_layer_frames.load(); }
void zcui_dlg::count_layer_frame() { ++g_layer_frames; }

// ---------------------------------------------------------------------------
// the "press a key" prompt
// ---------------------------------------------------------------------------

void zcui_dialog_prompt_begin(const char *line1, const char *line2, const char *line3)
{
    {
        zcui::Lock lock;
        g_shared.prompt = true;
        g_shared.prompt_lines[0] = line1 ? line1 : "";
        g_shared.prompt_lines[1] = line2 ? line2 : "";
        g_shared.prompt_lines[2] = line3 ? line3 : "";
    }
    // the game's own wait reads the keyboard and joysticks: let them through
    if(g_session)
    {
        zcui::end_session();
        g_session = false;
        g_prompt_suspended = true;
    }
    zcui::request_redraw();
    if(zcui::available())
        zcui::wait_frame(100);
}

void zcui_dialog_prompt_end()
{
    {
        zcui::Lock lock;
        g_shared.prompt = false;
        // the key that ended the wait must not also act on the dialog
        g_shared.swallow_keys = true;
    }
    if(g_prompt_suspended)
    {
        g_prompt_suspended = false;
        if(!g_states.empty())
            open_session();
    }
    zcui::request_redraw();
}

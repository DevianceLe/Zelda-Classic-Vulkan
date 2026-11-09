// zplayer: what the player needs from jwin (src/jwin.cpp is not part of
// zplayer).
//
// - The jwin dialog procedures are markers: the dialog host
//   (ui_dialogs_host.cpp) recognises them by what they answer to
//   MSG_ZCUI_KIND and shows their objects with Dear ImGui. They draw nothing
//   and keep only the state changes other code can see or relies on: a
//   D_EXIT button or list answers D_CLOSE, a plain button toggles
//   D_SELECTED, a radio button clears the others of its group, a slider
//   calls its callback when its value changes, an edit object takes the
//   text the host gives it. The game's own procedures that wrap a jwin one
//   (d_kbutton_proc, d_midilist_proc, jwin_initlist_proc, ...) see these
//   answers exactly as they would see jwin's.
// - jwin_alert() / jwin_alert3(): a message box run by the host (same
//   return values and keyboard shortcuts).
// - jwin_center_dialog(), jwin_set_colors(), jwin_set_dialog_color(),
//   close_dlg(), mouse_in_rect(), create_rgb_table_range().
//
// Written from scratch for zplayer (no jwin or Allegro code).

#define NOMINMAX

#include "precompiled.h" //always first

#include "jwin.h"
#include "ui_dialogs_internal.h"

#include <string.h>

using namespace zcui_dlg;

extern int zq_screen_w, zq_screen_h;

// The unified engine's widgets also want to hear about what happens to them.
#ifdef ZC_ENGINE
#define ZC_EVENT(d, ev) GUI_EVENT(d, ev)
#define ZC_EV(ev) (int)(ev)
#define LIST_READY(data) ((data)->unownedFunc || (data)->ownedFunc)
#else
#define ZC_EVENT(d, ev) ((void)0)
#define ZC_EV(ev) -1
#define LIST_READY(data) ((data)->listFunc)
#endif

int zcui_dlg::alert_marker = 0;

// ---------------------------------------------------------------------------
// colours
// ---------------------------------------------------------------------------

// The default scheme in 0xRRGGBB (classic light grey windows).
int jwin_colors[jcMAX] =
{
    0xC0C0C0, 0xF0F0F0, 0xD0D0D0, 0x808080, 0x404040, 0x000000,
    0x000080, 0x1084D0, 0xFFFFFF, 0xFFFFFF, 0x000000, 0x000080, 0xFFFFFF
};

int scheme[jcMAX];

void jwin_set_colors(int *colors)
{
    // 8-bit screens take palette indices, others 0xRRGGBB values.
    bool indexed = !screen || bitmap_color_depth(screen) == 8;
    for(int i = 0; i < jcMAX; ++i)
    {
        int c = colors[i];
        scheme[i] = indexed ? c : makecol((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
#ifdef ZC_ENGINE
        jwin_pal[i] = scheme[i];
#endif
    }
}

void jwin_set_dialog_color(DIALOG *dialog)
{
    for(DIALOG *d = dialog; d->proc; ++d)
    {
        d->fg = scheme[jcMEDDARK];
        d->bg = scheme[jcBOX];
    }
}

// ---------------------------------------------------------------------------
// small utilities
// ---------------------------------------------------------------------------

int close_dlg()
{
    return D_CLOSE;
}

int mouse_in_rect(int x, int y, int w, int h)
{
    int mx = gui_mouse_x(), my = gui_mouse_y();
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

// Moves every object so that the first one (the window) is centred on the
// game's screen (zq_screen_w x zq_screen_h).
void (*zcport_before_center_dialog)(DIALOG *) = NULL;  // ZQuest: see zq_remember_dialog()

void jwin_center_dialog(DIALOG *dialog)
{
    if(zcport_before_center_dialog)
        zcport_before_center_dialog(dialog);

    int dx = (zq_screen_w - dialog[0].w) / 2 - dialog[0].x;
    int dy = (zq_screen_h - dialog[0].h) / 2 - dialog[0].y;
    for(DIALOG *d = dialog; d->proc; ++d)
    {
        d->x += dx;
        d->y += dy;
    }
}

// ---------------------------------------------------------------------------
// marker procedures
// ---------------------------------------------------------------------------

namespace
{

inline int kind_answer(int kind)
{
    return KIND_MAGIC | kind;
}

// Activation of a button-like object (MSG_KEY, or MSG_CLICK: the host only
// sends it for a completed click): an exit object closes the dialog, any
// other one toggles. event: the widget event to report, or -1.
int activate_toggle(DIALOG *d, int event = -1)
{
    if(!(d->flags & D_EXIT))
        d->flags ^= D_SELECTED;
#ifdef ZC_ENGINE
    if(event >= 0)
    {
        GUI_EVENT(d, (guiEvent)event);
    }
#else
    (void)event;
#endif
    return (d->flags & D_EXIT) ? D_CLOSE : D_O_K;
}

int count_list(DIALOG *d)
{
    ListData *data = (ListData *)d->dp;
    int n = 0;
    if(data && LIST_READY(data))
        data->listFunc(-1, &n);
    return n;
}

// Plain list selection (d1) and scroll position (d2) kept consistent.
void select_list_item(DIALOG *d, int index)
{
    int n = count_list(d);
    if(n <= 0)
        index = 0;
    else if(index >= n)
        index = n - 1;
    if(index < 0)
        index = 0;
    d->d1 = index;
    if(d->d2 > d->d1)
        d->d2 = d->d1;
    if(d->d2 < 0)
        d->d2 = 0;
}

int edit_marker(int kind, int msg, DIALOG *d, int c)
{
    switch(msg)
    {
    case MSG_ZCUI_KIND:
        return kind_answer(kind);

    case MSG_START:
        // the text cursor goes to the end
        if(d->dp)
            d->d2 = (int)strlen((char *)d->dp);
        break;

    case MSG_WANTFOCUS:
    case MSG_LOSTFOCUS:
    case MSG_KEY:
        return D_WANTFOCUS;

    case MSG_CHAR:
        // Enter in an exit edit object closes the dialog; the other keys
        // are the host's business (ImGui edits the text).
        if((c >> 8) == KEY_ENTER)
        {
            ZC_EVENT(d, geENTER);
            if(d->flags & D_EXIT)
                return D_CLOSE;
        }
        break;

    case MSG_ZCUI_TEXT:
        if(d->dp && !(d->flags & D_READONLY))
        {
            const char *s = pending_text();
            int max = d->d1 > 0 ? d->d1 : 0;
            int len = (int)strlen(s);
            if(len > max)
                len = max;
            memcpy(d->dp, s, len);
            ((char *)d->dp)[len] = 0;
            d->d2 = len;
            ZC_EVENT(d, geCHANGE_VALUE);
        }
        break;
    }
    return D_O_K;
}

int list_marker(int kind, int msg, DIALOG *d, int c)
{
    switch(msg)
    {
    case MSG_ZCUI_KIND:
        return kind_answer(kind);

    case MSG_WANTFOCUS:
        return D_WANTFOCUS;

    case MSG_ZCUI_SET:
        select_list_item(d, c);
        ZC_EVENT(d, geCHANGE_SELECTION);
        break;

    case MSG_DCLICK:
        ZC_EVENT(d, geDCLICK);
        // fall through
    case MSG_KEY:
        if((d->flags & D_EXIT) && count_list(d) > 0)
            return D_CLOSE;
        break;
    }
    return D_O_K;
}

int check_marker(int kind, int msg, DIALOG *d)
{
    switch(msg)
    {
    case MSG_ZCUI_KIND:
        return kind_answer(kind);
    case MSG_WANTFOCUS:
        return D_WANTFOCUS;
    case MSG_KEY:
    case MSG_CLICK:
        return activate_toggle(d, ZC_EV(geTOGGLE));
    }
    return D_O_K;
}

int radio_marker(int kind, int msg, DIALOG *d, int c)
{
    switch(msg)
    {
    case MSG_ZCUI_KIND:
        return kind_answer(kind);

    case MSG_WANTFOCUS:
        return D_WANTFOCUS;

    case MSG_KEY:
    case MSG_CLICK:
        if(d->flags & D_SELECTED)
            return D_O_K;
        if(d->flags & D_EXIT)
            return D_CLOSE;
        // the others of the group (d1) let go, this one is selected
        broadcast_dialog_message(MSG_RADIO, d->d1);
        d->flags |= D_SELECTED;
        ZC_EVENT(d, geRADIO);
        return D_O_K;

    case MSG_RADIO:
        if(c == d->d1)
            d->flags &= ~D_SELECTED;
        break;
    }
    return D_O_K;
}

int display_marker(int kind, int msg)
{
    return msg == MSG_ZCUI_KIND ? kind_answer(kind) : D_O_K;
}

} // namespace

int jwin_win_proc(int msg, DIALOG *d, int c)
{
    (void)d; (void)c;
    return display_marker(K_WINDOW, msg);
}

int jwin_frame_proc(int msg, DIALOG *d, int c)
{
    (void)d; (void)c;
    return display_marker(K_FRAME, msg);
}

int jwin_guitest_proc(int msg, DIALOG *d, int c)
{
    (void)d; (void)c;
    return display_marker(K_FRAME, msg);
}

int jwin_text_proc(int msg, DIALOG *d, int c)
{
    (void)d; (void)c;
    return display_marker(K_TEXT, msg);
}

int jwin_ctext_proc(int msg, DIALOG *d, int c)
{
    (void)d; (void)c;
    return display_marker(K_CTEXT, msg);
}

int jwin_rtext_proc(int msg, DIALOG *d, int c)
{
    (void)d; (void)c;
    return display_marker(K_RTEXT, msg);
}

int jwin_textbox_proc(int msg, DIALOG *d, int c)
{
    (void)d; (void)c;
    return display_marker(K_TEXTBOX, msg);
}

int jwin_menu_proc(int msg, DIALOG *d, int c)
{
    (void)d; (void)c;
    return display_marker(K_MENU, msg);
}

int jwin_tab_proc(int msg, DIALOG *d, int c)
{
    (void)d; (void)c;
    return display_marker(K_TAB, msg);
}

int jwin_hline_proc(int msg, DIALOG *d, int c)
{
    (void)d; (void)c;
    return display_marker(K_HLINE, msg);
}

int jwin_vline_proc(int msg, DIALOG *d, int c)
{
    (void)d; (void)c;
    return display_marker(K_VLINE, msg);
}

int jwin_button_proc(int msg, DIALOG *d, int c)
{
    (void)c;
    switch(msg)
    {
    case MSG_ZCUI_KIND:
        return kind_answer(K_BUTTON);
    case MSG_WANTFOCUS:
        return D_WANTFOCUS;
    case MSG_KEY:
    case MSG_CLICK:
        return activate_toggle(d, ZC_EV(geCLICK));
    }
    return D_O_K;
}

// A button that calls the void() function in dp3 when it is pressed.
int jwin_func_button_proc(int msg, DIALOG *d, int c)
{
    if(msg == MSG_KEY || msg == MSG_CLICK)
    {
        typedef void (*Func)(void);
        Func f = (Func)d->dp3;
        if(f)
            f();
        return D_O_K;
    }
    return jwin_button_proc(msg, d, c);
}

int jwin_check_proc(int msg, DIALOG *d, int c)
{
    (void)c;
    return check_marker(K_CHECK, msg, d);
}

int jwin_checkfont_proc(int msg, DIALOG *d, int c)
{
    (void)c;
    return check_marker(K_CHECK, msg, d);
}

int jwin_radio_proc(int msg, DIALOG *d, int c)
{
    return radio_marker(K_RADIO, msg, d, c);
}

int jwin_radiofont_proc(int msg, DIALOG *d, int c)
{
    return radio_marker(K_RADIO, msg, d, c);
}

int jwin_edit_proc(int msg, DIALOG *d, int c)
{
    return edit_marker(K_EDIT, msg, d, c);
}

int jwin_numedit_proc(int msg, DIALOG *d, int c)
{
    return edit_marker(K_NUMEDIT, msg, d, c);
}

int jwin_hexedit_proc(int msg, DIALOG *d, int c)
{
    return edit_marker(K_HEXEDIT, msg, d, c);
}

int jwin_list_proc(int msg, DIALOG *d, int c)
{
    return list_marker(K_LIST, msg, d, c);
}

int jwin_abclist_proc(int msg, DIALOG *d, int c)
{
    return list_marker(K_ABCLIST, msg, d, c);
}

// A drop list keeps its selection in d1 and d2; with D_EXIT a new choice
// closes the dialog.
int jwin_droplist_proc(int msg, DIALOG *d, int c)
{
    switch(msg)
    {
    case MSG_ZCUI_KIND:
        return kind_answer(K_DROPLIST);

    case MSG_WANTFOCUS:
        return D_WANTFOCUS;

    case MSG_ZCUI_SET:
    {
        int old = d->d1;
        int n = count_list(d);
        int v = c;
        if(v >= n)
            v = n - 1;
        if(v < 0)
            v = 0;
        d->d1 = d->d2 = v;
        if(old != v)
            ZC_EVENT(d, geCHANGE_SELECTION);
        if(old != v && (d->flags & D_EXIT))
            return D_CLOSE;
        break;
    }
    }
    return D_O_K;
}

// A slider's value is d2 (0 to d1); dp2 is an optional callback
// int f(void *dp3, int d2), called when the value changes, whose answer is
// the slider's.
int jwin_slider_proc(int msg, DIALOG *d, int c)
{
    switch(msg)
    {
    case MSG_ZCUI_KIND:
        return kind_answer(K_SLIDER);

    case MSG_WANTFOCUS:
    case MSG_LOSTFOCUS:
        return D_WANTFOCUS;

    case MSG_ZCUI_SET:
    {
        int v = c;
        if(v > d->d1)
            v = d->d1;
        if(v < 0)
            v = 0;
        if(v == d->d2)
            break;
        d->d2 = v;
        ZC_EVENT(d, geCHANGE_VALUE);
        if(d->dp2)
        {
            typedef int (*Callback)(void *, int);
            return ((Callback)d->dp2)(d->dp3, d->d2);
        }
        break;
    }
    }
    return D_O_K;
}

// ---------------------------------------------------------------------------
// alerts
// ---------------------------------------------------------------------------

namespace
{

enum { A_WIN, A_S1, A_S2, A_S3, A_B1, A_B2, A_B3, A_COUNT };

DIALOG g_alert[A_COUNT + 1];

void alert_object(DIALOG &d, DIALOG_PROC proc, int flags, int key, const char *text)
{
    memset(&d, 0, sizeof(d));
    d.proc = proc;
    d.flags = flags;
    d.key = key;
    d.dp = (void *)(text ? text : "");
}

} // namespace

// Up to three lines and three buttons (missing ones are NULL); c1..c3 are
// the buttons' keyboard shortcuts. Returns 1, 2 or 3 for the button chosen;
// Escape or the window's close button answer 3.
int jwin_alert3(const char *title, const char *s1, const char *s2, const char *s3, const char *b1, const char *b2, const char *b3, int c1, int c2, int c3, FONT *title_font)
{
    (void)title_font;
    alert_object(g_alert[A_WIN], jwin_win_proc, title ? D_EXIT : 0, 0, NULL);
    g_alert[A_WIN].dp = (void *)title;
    g_alert[A_WIN].dp3 = &alert_marker;
    g_alert[A_WIN].x = 0;
    g_alert[A_WIN].y = 0;
    g_alert[A_WIN].w = 320;
    g_alert[A_WIN].h = 120;
    alert_object(g_alert[A_S1], jwin_ctext_proc, 0, 0, s1);
    alert_object(g_alert[A_S2], jwin_ctext_proc, 0, 0, s2);
    alert_object(g_alert[A_S3], jwin_ctext_proc, 0, 0, s3);
    alert_object(g_alert[A_B1], jwin_button_proc, D_EXIT | (b1 ? 0 : D_HIDDEN), c1, b1);
    alert_object(g_alert[A_B2], jwin_button_proc, D_EXIT | (b2 ? 0 : D_HIDDEN), c2, b2);
    alert_object(g_alert[A_B3], jwin_button_proc, D_EXIT | (b3 ? 0 : D_HIDDEN), c3, b3);
    memset(&g_alert[A_COUNT], 0, sizeof(DIALOG));

    clear_keybuf();
    int c = do_dialog(g_alert, A_B1);
    position_mouse_z(0);

    if(c == A_B1)
        return 1;
    if(c == A_B2)
        return 2;
    return 3;
}

// One or two buttons: returns 1 or 2 (Escape answers 2).
int jwin_alert(const char *title, const char *s1, const char *s2, const char *s3, const char *b1, const char *b2, int c1, int c2, FONT *title_font)
{
    int r = jwin_alert3(title, s1, s2, s3, b1, b2, NULL, c1, c2, 0, title_font);
    return r > 2 ? 2 : r;
}

// ---------------------------------------------------------------------------
// colour map for a palette range
// ---------------------------------------------------------------------------

namespace
{

// The weighted squared distance bestfit_color() uses (6-bit components;
// green counts most, blue least).
inline int colour_distance(const RGB &p, int r, int g, int b)
{
    int dr = (int)p.r - r, dg = (int)p.g - g, db = (int)p.b - b;
    return dg * dg * (59 * 59) + dr * dr * (30 * 30) + db * db * (11 * 11);
}

} // namespace

// Fills 'table' so that every 15-bit colour maps to a palette entry between
// 'start' and 'end' (inclusive) close to it: each entry claims its own cell
// (the first one wins a shared cell), then the cells grow into their
// neighbours, taking a neighbour over when they are strictly closer to its
// colour. 'callback' (optional) is called 256 times with 0..255.
void create_rgb_table_range(RGB_MAP *table, AL_CONST PALETTE pal, unsigned char start, unsigned char end, void (*callback)(int pos))
{
    const int CELLS = 32 * 32 * 32;
    static short owner[CELLS];
    static unsigned char queued[CELLS];
    static int ring[CELLS];
    int head = 0, count = 0;

    for(int i = 0; i < CELLS; ++i)
    {
        owner[i] = -1;
        queued[i] = 0;
    }

    for(int i = start; i <= end && i < PAL_SIZE; ++i)
    {
        int cell = ((pal[i].r >> 1) << 10) | ((pal[i].g >> 1) << 5) | (pal[i].b >> 1);
        if(owner[cell] >= 0)
            continue;
        owner[cell] = (short)i;
        queued[cell] = 1;
        ring[(head + count++) % CELLS] = cell;
    }

    static const int DR[6] = { 0, 0, 1, -1, 0, 0 };
    static const int DG[6] = { 0, 0, 0, 0, 1, -1 };
    static const int DB[6] = { 1, -1, 0, 0, 0, 0 };

    while(count > 0)
    {
        int cell = ring[head];
        head = (head + 1) % CELLS;
        --count;
        queued[cell] = 0;

        int c = owner[cell];
        int r = cell >> 10, g = (cell >> 5) & 31, b = cell & 31;
        for(int k = 0; k < 6; ++k)
        {
            int nr = r + DR[k], ng = g + DG[k], nb = b + DB[k];
            if(nr < 0 || nr > 31 || ng < 0 || ng > 31 || nb < 0 || nb > 31)
                continue;
            int n = (nr << 10) | (ng << 5) | nb;
            int o = owner[n];
            if(o == c)
                continue;
            if(o >= 0 && colour_distance(pal[c], nr * 2, ng * 2, nb * 2) >= colour_distance(pal[o], nr * 2, ng * 2, nb * 2))
                continue;
            owner[n] = (short)c;
            if(!queued[n])
            {
                queued[n] = 1;
                ring[(head + count++) % CELLS] = n;
            }
        }
    }

    for(int r = 0; r < 32; ++r)
        for(int g = 0; g < 32; ++g)
            for(int b = 0; b < 32; ++b)
            {
                int o = owner[(r << 10) | (g << 5) | b];
                table->data[r][g][b] = (unsigned char)(o < 0 ? start : o);
            }

    if(callback)
        for(int i = 0; i < 256; ++i)
            callback(i);
}

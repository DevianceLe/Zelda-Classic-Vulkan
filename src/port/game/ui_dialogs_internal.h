// zplayer dialog host: private declarations shared by jwin_port.cpp,
// ui_dialogs_host.cpp (game thread) and ui_dialogs_layer.cpp (ImGui, on the
// platform thread). Not used by the game.
//
// How it works
//
//   The game's DIALOG tables and their logic stay as they are. zcport's
//   dialog manager hands every dialog to this host (zcport/gui_host.h).
//   jwin's procedures (jwin_button_proc, ...) are markers here: they draw
//   nothing and keep only the state logic other code relies on (a D_EXIT
//   button answers MSG_CLICK with D_CLOSE, a radio button clears its group,
//   a slider calls its callback, ...). Each marker answers MSG_ZCUI_KIND
//   with what it is, so the host also recognises the game's own procedures
//   that wrap a jwin one (d_midilist_proc, jwin_initlist_proc,
//   d_kbutton_proc, ...): they pass unknown messages on to the marker.
//
//   Game thread (update_dialog): takes the events the ImGui layer recorded,
//   turns them into the messages Allegro would send (MSG_CLICK, MSG_KEY,
//   MSG_XCHAR, menu callbacks, value changes through MSG_ZCUI_SET so that a
//   wrapping procedure sees the change happen inside its call), redraws
//   (MSG_DRAW, so text loaders such as d_stringloader refresh their
//   strings), idles, and then builds a DialogView: a copy of everything the
//   layer needs (texts, values, list items, menu tree, bitmaps). Game
//   procedures and callbacks therefore only ever run on the game thread.
//
//   Platform thread (the layer): draws the views with ImGui and records
//   events. It never touches a DIALOG.
//
//   Both sides share the views and event queues under zcui::Lock; the game
//   thread holds it only to swap them, never while a procedure runs.

#ifndef ZPLAYER_UI_DIALOGS_INTERNAL_H
#define ZPLAYER_UI_DIALOGS_INTERNAL_H

#include <allegro.h>

#include <string>
#include <vector>

namespace zcui_dlg
{

// ---- what a procedure is ------------------------------------------------------

enum Kind
{
    K_NONE = 0,   // not shown (invisible helpers, unknown procedures)
    K_WINDOW,     // jwin_win_proc
    K_FRAME,      // jwin_frame_proc, jwin_guitest_proc
    K_TEXT,       // jwin_text_proc
    K_CTEXT,      // jwin_ctext_proc
    K_RTEXT,      // jwin_rtext_proc
    K_BUTTON,     // jwin_button_proc, jwin_func_button_proc
    K_CHECK,      // jwin_check_proc, jwin_checkfont_proc
    K_RADIO,      // jwin_radio_proc, jwin_radiofont_proc
    K_EDIT,       // jwin_edit_proc
    K_NUMEDIT,    // jwin_numedit_proc
    K_HEXEDIT,    // jwin_hexedit_proc
    K_LIST,       // jwin_list_proc
    K_ABCLIST,    // jwin_abclist_proc
    K_DROPLIST,   // jwin_droplist_proc
    K_TEXTBOX,    // jwin_textbox_proc
    K_SLIDER,     // jwin_slider_proc
    K_MENU,       // jwin_menu_proc
    K_TAB,        // jwin_tab_proc
    K_HLINE,      // jwin_hline_proc
    K_VLINE,      // jwin_vline_proc
    K_BITMAP,     // d_bitmap_proc (zcport)
    K_CUSTOM,     // a game procedure that draws itself, shown as a picture
    K_POPUP,      // a context menu (jwin_do_menu)
    K_PANE        // a scrolling pane: its scroll bars; its children are clipped to it
};

// User messages the markers understand (Allegro's procedures ignore them).
enum
{
    MSG_ZCUI_KIND = MSG_USER + 0x5A00,   // answer KIND_MAGIC | kind
    MSG_ZCUI_SET,                        // c = new value (slider d2, list or
                                         // drop list selection)
    MSG_ZCUI_TEXT,                       // the edit text becomes pending_text()
    MSG_ZCUI_LABEL,                      // pending_text() becomes a button's label
                                         // (buttons with no text of their own)
    MSG_ZCUI_ALIGN                       // answers 1 + text alignment (0: left,
                                         // 1: centre, 2: right); others answer 0
};

const int KIND_MAGIC = 0x5A430000;
const int KIND_MASK = 0xFFFF0000;

// Pixels of a K_CUSTOM picture per pixel of the game's screen. The UI scales
// pictures smoothly, so they are enlarged first to keep them crisp.
const int CANVAS_SCALE = 3;

// From jwin.h, for the files that do not include it.
const int JW_READONLY = D_USER;     // D_READONLY: an edit object nobody types into
const int JW_FR_INV = 2;            // frame styles (jwin_frame_proc d1)
const int JW_FR_DEEP = 3;

// The kind of d->proc (asked once per procedure, then remembered).
int kind_of(DIALOG *d);

// The text MSG_ZCUI_TEXT stores (game thread only).
const char *pending_text();
void set_pending_text(const std::string &s);

// dp3 of the window object of an alert built by jwin_alert3(): the host
// lays such a dialog out as a message box.
extern int alert_marker;

// ---- what the layer draws ------------------------------------------------------

struct Image
{
    int w, h;
    std::vector<unsigned int> rgba;      // 0xAABBGGRR, w * h
    unsigned int hash;                   // of rgba
    Image() : w(0), h(0), hash(0) {}
};

struct WidgetView
{
    int obj;                    // index in the DIALOG array
    int kind;
    int x, y, w, h;             // dialog coordinates
    int flags;                  // DIALOG flags
    int d1, d2;
    int key;
    int value;                  // check/radio selected, slider position,
                                // list/drop list selection, selected tab
    int limit;                  // slider maximum, edit length
    std::string text;           // label, edit text, text box contents (UTF-8)
    std::vector<std::string> items;  // list items, tab names
    int image;                  // index into DialogView::images, or -1
    bool wrap;                  // text box: word wrap
    int pane;                   // index in DialogView::widgets of the pane
                                // clipping this widget, or -1
    std::string tooltip;        // shown while the mouse is over it (UTF-8)
    WidgetView() : obj(-1), kind(K_NONE), x(0), y(0), w(0), h(0), flags(0), d1(0), d2(0),
                   key(0), value(0), limit(0), image(-1), wrap(true), pane(-1) {}
};

struct MenuNode
{
    std::string label;          // without '&', UTF-8
    std::string shortcut;       // the text after '\t'
    int flags;                  // D_SELECTED, D_DISABLED
    MENU *item;                 // the game's MENU entry
    int first_child;            // index into DialogView::menu, or -1
    int child_count;
    bool separator;             // empty text
    char mnemonic;              // lower case letter after '&', or 0
    MenuNode() : flags(0), item(NULL), first_child(-1), child_count(0), separator(false), mnemonic(0) {}
};

struct DialogView
{
    bool ready;                 // built at least once after MSG_DRAW
    bool alert;                 // lay out as a message box
    bool has_window;            // a jwin_win_proc frames the dialog
    int win_obj;
    std::string title;
    bool closable;              // the window has D_EXIT (close button)
    int box_x, box_y, box_w, box_h;     // area to show, dialog coordinates
    std::vector<WidgetView> widgets;    // in dialog order
    std::vector<MenuNode> menu;         // top level: [0, menu_top)
    int menu_top;
    int menu_obj;               // the jwin_menu_proc object, or -1
    int focus_obj;              // initial focus
    bool overlay;               // the editor's main dialog: only its menu bar
    bool popup;                 // a context menu at the mouse
    int canvas_w, canvas_h;     // size of HostDialog::canvas (0: no K_CUSTOM)
    int screen_w, screen_h;     // the game's screen
    int canvas_focus;           // the K_CUSTOM object that takes keys, or -1
    float text_scale;           // text size against the dialog's coordinates
    std::vector<Image> images;
    int open_menu;              // top-level menu to drop down (Alt+letter)
    unsigned int open_menu_serial;
    unsigned int applied_serial;        // last layer event the game applied
    DialogView() : ready(false), alert(false), has_window(false), win_obj(-1), closable(false),
                   box_x(0), box_y(0), box_w(0), box_h(0), menu_top(0), menu_obj(-1),
                   focus_obj(-1), overlay(false), popup(false), canvas_w(0), canvas_h(0),
                   screen_w(0), screen_h(0), canvas_focus(-1), text_scale(1.0f),
                   open_menu(-1), open_menu_serial(0), applied_serial(0) {}
};

// ---- what the layer records ------------------------------------------------------

struct Event
{
    enum Type
    {
        CLICK,          // a button, check box or radio button was activated
        DCLICK,         // double click on a list item
        SET,            // new value (slider, list, drop list)
        TEXT,           // new edit text
        TAB,            // another tab chosen (value = panel)
        MENU_PICK,      // a menu item was chosen
        KEY,            // a key for the dialog (Allegro ascii / scancode)
        CLOSE,          // the window's close button
        CANVAS          // a press on a K_CUSTOM object (value = mouse buttons)
    };
    Type type;
    int obj;
    int value;
    std::string text;
    MENU *menu;
    int ascii, scancode;
    unsigned int serial;
    int value2;                 // SET on a pane: value is y, value2 is x
    Event() : type(CLICK), obj(-1), value(0), menu(NULL), ascii(0), scancode(0), serial(0), value2(0) {}
};

// ---- shared state (under zcui::Lock) ------------------------------------------------

// Layer-only state of one object (values the user changed that the game
// has not applied yet, edit buffers, ...).
struct LayerObject
{
    unsigned int pending_serial;    // > applied_serial: use pending_value
    int pending_value;
    std::vector<char> edit;         // InputText buffer
    bool edit_active;
    int tab_shown;                  // tab ImGui showed last frame
    int list_shown;                 // selection the list last scrolled to
    LayerObject() : pending_serial(0), pending_value(0), edit_active(false), tab_shown(-1), list_shown(-2) {}
};

struct HostDialog
{
    int id;                         // unique, for ImGui window ids

    // shared
    DialogView view;
    std::vector<Event> events;
    std::vector<unsigned int> canvas;   // RGBA of the K_CUSTOM objects
    int dirty_x0, dirty_y0, dirty_x1, dirty_y1;    // changed part (x1 <= x0: none)

    // layer only
    bool popup_closed;              // the context menu reported that it closed
    unsigned int next_serial;
    std::vector<LayerObject> objects;   // by object index
    int frames;                     // frames drawn
    bool was_top;
    int focused_obj;                // ImGui focus last frame
    bool list_focused;              // a list had the focus last frame
    bool popup_open;                // a popup (menu, combo) was open last frame
    unsigned int open_menu_seen;

    HostDialog() : id(0), dirty_x0(0), dirty_y0(0), dirty_x1(0), dirty_y1(0), popup_closed(false),
                   next_serial(1), frames(0), was_top(false), focused_obj(-1),
                   list_focused(false), popup_open(false), open_menu_seen(0) {}
};

struct Shared
{
    std::vector<HostDialog *> stack;    // oldest first; the last one has input
    size_t floor;                       // entries below it are hidden (a page)
    bool prompt;                        // a "press a key" prompt is up
    std::string prompt_lines[3];
    bool swallow_keys;                  // ignore keys until all are released
    Shared() : floor(0), prompt(false), swallow_keys(false) {}
};

Shared &shared();

// The ImGui layer (ui_dialogs_layer.cpp), registered by the host.
void layer(void *);

// Mouse state for the game's gui_mouse_*() while dialogs are up (written by
// the layer): buttons pressed outside every window, and a counter that
// moves on any user input (so input_idle() sees activity).
int outside_buttons();
int activity();
void set_outside_buttons(int b);
void add_activity();
unsigned int layer_frames();
void count_layer_frame();

// The mouse in the top dialog's coordinates, written by the layer.
void set_dialog_mouse(int x, int y, int buttons);

// The dialog's picture is no longer needed (with zcui::Lock held).
void canvas_release(HostDialog *hd);

// The item chosen in the last context menu, or NULL.
extern MENU *popup_chosen;

} // namespace zcui_dlg

#endif

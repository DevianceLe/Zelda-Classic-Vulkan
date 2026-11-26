// zq_ui_profile.h: ZQuest's interface per ZC build. Each build's editor had its own menus, main
// screen layout, look and dialogs; a profile holds them as data (tables). ZQuest shows the profile
// of the open quest's build (File > New in a build, opening a quest), or the one the user picked
// (Etc > Interface..., the Interface menu on the bar): "Match the quest's build" (the default),
// "Newest", or a build from the open quest's own up to the newest. The interface never changes
// what the quest holds: under a newer build's interface, entries for what the quest's build can't
// hold are shown disabled.
//
// The format (stable; one file per generation registers its profiles, see zq_ui_profiles_README.txt):
// - Menus: rows in order (a tree written depth first), each with its depth, the text the build
//   showed and the action it runs. An action names one of this ZQuest's editor functions by its
//   place in today's (2.53.1) menus, as the menu audit writes it: the texts without '&' and the
//   shortcut, joined by '/' ("Quest/Misc Data/Shop Types", "Screen/Catch All"); or an extra action
//   "@name" (zq_ui_add_action; zq_ui_profile.cpp has "@interface" and a few more). An entry with
//   no function here ("") is shown disabled with the build's text. The shortcut after '\t' (F1-F12,
//   Del) is bound to the entry's action while the profile is shown: the keys do what the menu says.
// - Layout: Large (800x600), Small (320x240) or the user's own choice, plus panels moved or
//   resized from that mode's places.
// - Look: the editor's own colours (the dark VGA colours dvc(1..8), the title gradient dvc(9..15),
//   and which of them each jwin role takes) and the windows' style (the port's own, or classic:
//   square, framed, in those colours).
// - Dialogs: the build's dialogs, by the editor's dialog name, as JSON (zquiinv's dialog files).
//   Kept with the profile; drawing them is a later step.
//
// 2.53.1's profile is today's interface exactly and the default: its menus are zquest.cpp's own
// tables, its layout and colours are what the user set in Options.
//
// Written for the port (no jwin or Allegro code).
#pragma once

#define ZQ_UI_KEEP (-9999)          // a layout value left as the mode has it

struct zq_ui_menu_row
{
    int depth;              // 0: a menu on the bar; 1: its entries; 2: a submenu's entries; ...
    const char *text;       // as the build showed it ('&' the key letter, '\t' then the shortcut);
                            // "" a separator
    const char *action;     // NULL: a submenu (the rows after it, one deeper, are its entries) or a
                            // separator; "": no function here (shown disabled); else an action name
    int needs;              // 0, or the oldest generation (0x255, 0x300) whose quests hold what the
                            // entry edits: disabled for older quests (an action can say so itself)
};

struct zq_ui_rect
{
    const char *what;       // "panel", "minimap", "combolist", "comboaliaslist", "comboalias_preview",
                            // "combolistscrollers", "combo_preview", "combolist_window", "map_page_bar",
                            // "favorites_window", "favorites_list", "commands_window", "commands_list",
                            // "layer_panel"
    int index;              // the array element; -1: every one (panel[0..8], combolist[0..2], ...)
    int x, y, w, h;         // ZQ_UI_KEEP: that value as the mode has it
};

struct zq_ui_layout
{
    const char *name;
    int large;              // 1: the Large layout (800x600), 0: Small (320x240), -1: the user's choice
    int screen_w, screen_h; // 0: the mode's own (other sizes: reserved, not applied yet)
    const zq_ui_rect *rects;
    int rect_count;
};

struct zq_ui_look
{
    const char *name;
    bool keep;              // true: the colours the user set in Options, the port's own windows
    unsigned dvc[9];        // 0xRRGGBB of dvc(1..8) (dvc[0] unused)
    unsigned grad_from;     // the title gradient over dvc(16-grad_divs..15), 0xRRGGBB
    unsigned grad_to;
    int grad_divs;          // 0: dvc(9..15) left as they are
    int jc[13];             // jcBOX, jcLIGHT, jcMEDLT, jcMEDDARK, jcDARK, jcBOXFG, jcTITLEL, jcTITLER,
                            // jcTITLEFG, jcTEXTBG, jcTEXTFG, jcSELBG, jcSELFG: which dvc (1..15) each takes
    int windows;            // 0: the port's own window style; 1: classic, in the jwin colours
    int font;               // 0: the port's UI font; 1: a small pixel font (reserved, not applied yet)
};

struct zq_ui_dialog
{
    const char *name;       // the editor's dialog, as its title reads in 2.53.1 ("Screen Data")
    const char *json;       // the build's dialog (zquiinv's JSON), or NULL
};

struct zq_ui_profile
{
    const char *build;      // the catalog's build name as File > New lists it ("2.10.3", "2.11 Beta 6",
                            // "2.55.19", "3.0 Nightly 2026-11-06"; a 2.11/1.92 beta the catalog names
                            // only while its quest is open: "2.11 Beta 12")
    int gen;                // its generation: 0x190, 0x192, 0x210, 0x211, 0x250, 0x255, 0x300 (with
                            // order below set, the generation it sorts in)
    const zq_ui_menu_row *menu;     // NULL: zquest.cpp's own menus (2.53.1)
    int menu_rows;
    const zq_ui_layout *layout;     // NULL: the user's choice, as in 2.53.1
    const zq_ui_look *look;         // NULL: the user's colours, as in 2.53.1
    const zq_ui_dialog *dialogs;    // NULL: today's dialogs
    int dialog_count;
    long order;             // 0: the build's place is the catalog's (by name); else its own order on the
                            // catalog's scale within gen (a build the catalog lists only as part of a
                            // run, as "3.0.0-prerelease.N" with zq30_order_of_prerelease(N), or a beta
                            // it doesn't list); used before any name lookup
};

// Registering (from a generation's own file, before main(): see zq_ui_profiles_README.txt). The
// data must stay (static tables). A build listed twice: the later registration wins.
void zq_ui_add_profile(const zq_ui_profile *p);
// An extra action "@name" for menu rows: proc as a menu entry's; needs as zq_ui_menu_row::needs.
void zq_ui_add_action(const char *name, int (*proc)(), int needs);

struct zq_ui_registrar
{
    explicit zq_ui_registrar(void (*add)()) { add(); }
};

// ---- the editor's side --------------------------------------------------------------------------

typedef struct MENU MENU;
typedef struct DIALOG DIALOG;

// zquest.cpp
MENU *zq_ui_menu_253();                 // today's menu bar (the_menu)
void zq_ui_set_menu_bar(MENU *bar);     // what the editor's menu bar shows
DIALOG *zq_ui_main_dialog();            // the editor's main dialog (its menu bar, its shortcut keys)
void zq_switch_layout(bool large);

// zq_ui_profile.cpp
void zq_ui_follow_quest();              // after New, Open, Revert and the start: the profile to show
void zq_ui_after_layout();              // zq_apply_layout's end: the profile's panel places
void zq_ui_capture_user_look();         // main(), once the user's colours are set
const char *zq_ui_profile_name();       // the build whose interface is shown
int onInterfaceOptions();               // Etc > Interface...
extern MENU zq_ui_quick_menu[];         // the bar's Interface menu (every profile has it)

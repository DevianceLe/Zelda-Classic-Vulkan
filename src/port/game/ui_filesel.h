/*
 * zplayer (ZC_PORT): the file selector's shared state, between the game side
 * (ui_filesel.cpp: the jwinfsel.h functions, all path logic and file system
 * access, on the game thread) and the view (ui_filesel_view.cpp: the ImGui
 * layer, on the platform thread). Everything in State is guarded by the UI
 * lock (zcport/ui.h).
 *
 * The view never touches the file system or the game's path rules: it shows
 * what the game side put in State and queues Requests (clicks, keys, edits),
 * which the game thread carries out in order between two UI frames.
 *
 * Paths and names are "native" (the game's encoding, see ui_game.h) unless
 * the member says UTF-8.
 */

#ifndef ZC_PORT_GAME_UI_FILESEL_H
#define ZC_PORT_GAME_UI_FILESEL_H

#include "ui_game.h"

#include <string>
#include <vector>

namespace zcui_fsel
{

enum Mode
{
    MODE_FILE,      // jwin_file_select_ex
    MODE_DIR,       // jwin_dfile_select_ex
    MODE_BROWSE     // jwin_file_browse_ex (a list of filters)
};

enum RowKind
{
    ROW_UP,         // ".."
    ROW_DIR,
    ROW_FILE
};

struct Row
{
    RowKind kind;
    std::string name;       // as the original list held it: dirs end with a separator
    std::string label;      // UTF-8, without the separator
    std::string size_text;  // UTF-8
    std::string time_text;  // UTF-8
    bool highlight;         // a file of the first extension (a quest, say)
};

struct Crumb
{
    std::string label;      // UTF-8
    std::string path;       // ends with a separator
};

enum RequestKind
{
    REQ_SELECT,             // a row became the selection: the path field shows it
    REQ_OPEN,               // a row was double-clicked / Enter / A: select, then open it
    REQ_EDIT,               // the path field was edited (text)
    REQ_ENTER,              // Enter in the path field (text)
    REQ_GOTO,               // a place, drive or breadcrumb (text = folder)
    REQ_UP,                 // the parent folder
    REQ_OK,                 // the OK button (text = the path field)
    REQ_CANCEL,             // Cancel, Esc, B, the close button (text = the path field)
    REQ_FILTER              // a filter was chosen (index)
};

struct Request
{
    RequestKind kind;
    int index;
    std::string text;       // UTF-8 for EDIT / ENTER / OK / CANCEL, native for GOTO
    std::string dir;        // the folder the view showed (SELECT / OPEN)
    bool keys;              // came from the keyboard or a gamepad (not the mouse)
};

struct State
{
    bool active;
    unsigned serial;                    // one per opening

    // Set when the selector opens.
    Mode mode;
    std::string title;                  // UTF-8
    std::string ok_label, cancel_label; // UTF-8
    std::vector<std::string> filters;   // UTF-8 names (MODE_BROWSE)
    std::string filter_text;            // UTF-8, e.g. "*.qst" (MODE_FILE / MODE_DIR)
    std::vector<zcui_fs::Place> places; // game folder and user folders
    std::vector<zcui_fs::Place> drives;
    size_t path_capacity;               // bytes for the path field (UTF-8)

    // Updated by the game thread.
    int filter;                         // MODE_BROWSE
    std::string dir;                    // the folder shown, ends with a separator
    bool at_root;
    bool unreadable;
    bool loading;
    std::vector<Crumb> crumbs;
    std::vector<Row> rows;
    unsigned list_serial;               // changes with every listing
    std::string path_utf8;              // the path field (the original's edit field)
    unsigned path_serial;               // changes when the game side sets path_utf8
    int sel;                            // selected row or -1 (the view sets it too)
    bool scroll_to_sel;                 // the view scrolls to 'sel' once
    bool focus_sel;                     // the view moves keyboard focus to 'sel' (or the
                                        // first row) once: after keyboard navigation

    // Updated by the view.
    std::vector<Request> requests;

    State() : active(false), serial(0), mode(MODE_FILE), path_capacity(0), filter(0), at_root(false),
              unreadable(false), loading(false), list_serial(0), path_serial(0), sel(-1), scroll_to_sel(false), focus_sel(false) {}
};

/* The one selector (there is never more than one open). */
State &state();

/* The ImGui layer (ui_filesel_view.cpp); 'user' is unused. */
void view_layer(void *user);

} // namespace zcui_fsel

#endif /* ZC_PORT_GAME_UI_FILESEL_H */

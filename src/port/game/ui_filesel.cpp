// zplayer (ZC_PORT): the file selector. It replaces src/jwinfsel.cpp (jwin's
// version of Allegro's file selector) with a Dear ImGui file browser; the
// window is ui_filesel_view.cpp, this file is everything the game can
// observe: the functions of src/jwinfsel.h, their arguments, return values
// and what ends up in the caller's path buffer.
//
// The original keeps one edit field that is the caller's buffer itself, and
// these rules (kept here; written from scratch):
//
//  - An empty path starts as the current directory with a separator. The
//    field is then made canonical (canonicalize_filename(): absolute, lower
//    case drive letter, '\' separators on Windows), and again every time a
//    row is selected, which writes folder + name into it (folder names end
//    with a separator).
//  - Opening (Enter, a double click): "./" is added to an empty field or one
//    ending with ':'; the field is made canonical; a folder (one that ends
//    with a separator, or names a folder) is listed, anything else (a file,
//    or a name that does not exist yet) closes the selector as chosen.
//  - The list: folders first, then the files whose extension is one of the
//    filter's (separated by ' ', ',' or ';'; none = all files), hidden and
//    system files left out, sorted without case, numbers by value.
//  - On close the field is in the caller's buffer (also after Cancel: the
//    Save MIDI dialog starts again where the user browsed to).
//    jwin_file_select_ex / jwin_file_browse_ex return FALSE for Cancel, Esc,
//    the close button, or a path without a file name; jwin_dfile_select_ex
//    returns TRUE for a path without one. file_select and dfile_select add
//    ".ext" to a chosen name without an extension when the filter is a
//    single extension. jwin_file_browse_ex never adds one: in the original
//    the filter string is freed before that test, so the test always fails.
//    It writes the filter index to *list_sel only when it returns TRUE.
//  - Changing the filter drops the file name from the field when its
//    extension does not fit the new filter.
//  - "OK" opens like Enter (the original's OK returned FALSE when a folder
//    was selected; here it opens the folder), except in jwin_dfile_select_ex,
//    where it returns the field as it is.
//
// Filters with attribute sets ("/+r-h" in Allegro's syntax) are not
// supported; the player does not use them. title_font, width and height are
// ignored (the window is sized to the display).
//
// Threads: the game thread opens a UI session (zcport/ui.h), registers the
// view's layer and carries out the view's requests between UI frames, so all
// file system access happens on the game thread, as before.
//
// Written from scratch for zplayer.

#include <stdint.h>
#include "jwinfsel.h"                   // the API; brings in <allegro.h>
#include <allegro/internal/aintern.h>   // _al_getdrive, _al_getdcwd

#include "zcport/ui.h"
#include "ui_filesel.h"
#if defined(__SWITCH__)
extern bool close_button_quit;   // src/zelda.cpp
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace zcui_fsel
{

State &state()
{
    static State s;
    return s;
}

} // namespace zcui_fsel

using namespace zcui_fsel;

namespace
{

enum Result
{
    RES_CANCEL,     // Cancel, Esc, B, the close button
    RES_CHOSEN,     // opened a file (or a new name)
    RES_OK          // the OK button in jwin_dfile_select_ex
};

struct Session
{
    Mode mode;
    int size;                           // the caller's buffer size
    std::string edit;                   // the original's edit field
    std::vector<std::string> tokens;    // the current filter's extensions
    EXT_LIST *list;                     // MODE_BROWSE
    int filter;
    bool done;
    Result result;

    Session() : mode(MODE_FILE), size(0), list(NULL), filter(0), done(false), result(RES_CANCEL) {}
};

// ---------------------------------------------------------------------------
// strings and paths
// ---------------------------------------------------------------------------

bool is_sep(char c)
{
    return c == '/' || c == OTHER_PATH_SEPARATOR;
}

/* What ustrzcpy(dest, size, s) leaves: at most size-1 bytes. */
std::string clip(const std::string &s, int size)
{
    if(size <= 0)
        return std::string();
    if((int)s.size() > size - 1)
        return s.substr(0, (size_t)(size - 1));
    return s;
}

/* canonicalize_filename() into a 1024 byte buffer, as the original. */
std::string canonical(const std::string &s)
{
    char b[1024];
    canonicalize_filename(b, s.c_str(), sizeof(b));
    return b;
}

/* replace_filename(dest, dir, name, size). */
std::string join(const std::string &dir, const std::string &name, int size)
{
    std::vector<char> b((size_t)(size > 1 ? size : 2), 0);
    replace_filename(&b[0], dir.c_str(), name.c_str(), (int)b.size());
    return &b[0];
}

/* The folder part of a path, ending with a separator. */
std::string folder_of(const std::string &path)
{
    return join(path, "", (int)path.size() + 2);
}

bool same_ci(const std::string &a, const std::string &b)
{
    if(a.size() != b.size())
        return false;
    for(size_t i = 0; i < a.size(); ++i)
        if(tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    return true;
}

/* Extensions of a filter string, split at ' ', ',' and ';'. */
std::vector<std::string> ext_tokens(const char *ext)
{
    std::vector<std::string> out;
    if(!ext)
        return out;
    std::string cur;
    for(const char *p = ext;; ++p)
    {
        char c = *p;
        if(c == 0 || c == ' ' || c == ',' || c == ';')
        {
            if(!cur.empty() && cur[0] != '/')   // attribute sets: not supported
                out.push_back(cur);
            cur.clear();
            if(c == 0)
                break;
        }
        else
            cur += c;
    }
    return out;
}

bool ext_matches(const std::vector<std::string> &tokens, const std::string &name)
{
    if(tokens.empty())
        return true;
    std::string ext = get_extension(name.c_str());
    for(size_t i = 0; i < tokens.size(); ++i)
        if(same_ci(ext, tokens[i]))
            return true;
    return false;
}

/* Name order: no case, digit runs by value, '.' before everything else
 * (so "foo.bar" comes before "foo-1.bar" and "foo9" before "foo10"). */
bool name_less(const std::string &x, const std::string &y)
{
    const char *a = x.c_str(), *b = y.c_str();
    while(*a && *b)
    {
        if(isdigit((unsigned char)*a) && isdigit((unsigned char)*b))
        {
            const char *a0 = a, *b0 = b;
            while(*a0 == '0' && isdigit((unsigned char)a0[1]))
                ++a0;
            while(*b0 == '0' && isdigit((unsigned char)b0[1]))
                ++b0;
            const char *a1 = a0, *b1 = b0;
            while(isdigit((unsigned char)*a1))
                ++a1;
            while(isdigit((unsigned char)*b1))
                ++b1;
            if(a1 - a0 != b1 - b0)
                return a1 - a0 < b1 - b0;
            int c = strncmp(a0, b0, (size_t)(a1 - a0));
            if(c != 0)
                return c < 0;
            if(a1 - a != b1 - b)
                return a1 - a < b1 - b;  // fewer leading zeros first
            a = a1;
            b = b1;
            continue;
        }
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if(ca != cb)
        {
            if(ca == '.')
                return true;
            if(cb == '.')
                return false;
            return ca < cb;
        }
        ++a;
        ++b;
    }
    if(*a || *b)
        return *b != 0;
    return strcmp(x.c_str(), y.c_str()) < 0;
}

std::string format_size(uint64_t n)
{
    char b[32];
    if(n < 1024ULL)
        snprintf(b, sizeof(b), "%u B", (unsigned)n);
    else if(n < 1024ULL * 1024ULL)
        snprintf(b, sizeof(b), "%.1f KB", (double)n / 1024.0);
    else if(n < 1024ULL * 1024ULL * 1024ULL)
        snprintf(b, sizeof(b), "%.1f MB", (double)n / (1024.0 * 1024.0));
    else
        snprintf(b, sizeof(b), "%.1f GB", (double)n / (1024.0 * 1024.0 * 1024.0));
    return b;
}

bool is_root(const std::string &dir)
{
#ifdef _WIN32
    return dir.size() <= 3 && dir.size() >= 2 && dir[1] == DEVICE_SEPARATOR;
#elif defined(__SWITCH__)
    /* "sdmc:/": a device and nothing after its one separator */
    return dir.size() >= 2 && dir.find('/') == dir.size() - 1 && dir[dir.size() - 2] == DEVICE_SEPARATOR;
#else
    return dir == "/";
#endif
}

std::vector<Crumb> make_crumbs(const std::string &dir)
{
    std::vector<Crumb> out;
    size_t start = 0;
#ifdef _WIN32
    if(dir.size() >= 2 && dir[1] == DEVICE_SEPARATOR)
    {
        Crumb c;
        c.label = std::string(1, (char)toupper((unsigned char)dir[0])) + ":";
        c.path = dir.substr(0, dir.size() >= 3 ? 3 : 2);
        out.push_back(c);
        start = dir.size() >= 3 ? 3 : 2;
    }
#else
    if(!dir.empty() && dir[0] == '/')
    {
        Crumb c;
        c.label = "/";
        c.path = "/";
        out.push_back(c);
        start = 1;
    }
#endif
    for(size_t i = start; i < dir.size(); ++i)
    {
        if(!is_sep(dir[i]))
            continue;
        if(i > start)
        {
            Crumb c;
            c.label = zcui_fs::native_to_utf8(dir.substr(start, i - start).c_str());
            c.path = dir.substr(0, i + 1);
            out.push_back(c);
        }
        start = i + 1;
    }
    return out;
}

/* When 'to' is an ancestor of 'from', the name (with its separator) of the
 * folder in 'to' that leads back to 'from': the row to select after going
 * up, as the original does after "cd ..". */
std::string child_toward(const std::string &from, const std::string &to)
{
    if(from.size() <= to.size() || !same_ci(from.substr(0, to.size()), to))
        return std::string();
    std::string rest = from.substr(to.size());
    for(size_t i = 0; i < rest.size(); ++i)
        if(is_sep(rest[i]))
            return rest.substr(0, i + 1);
    return std::string();
}

// ---------------------------------------------------------------------------
// the session (game thread)
// ---------------------------------------------------------------------------

void publish_edit(const Session &se)
{
    std::string u = zcui_fs::native_to_utf8(se.edit.c_str());
    zcui::Lock lock;
    State &st = state();
    st.path_utf8 = u;
    ++st.path_serial;
}

/* The field from the view's text (UTF-8); it holds at most size-1 bytes. */
void edit_from_view(Session &se, const std::string &utf8)
{
    se.edit = clip(zcui_fs::utf8_to_native(utf8.c_str()), se.size);
}

/* MSG_START of the edit field. */
void edit_canonical(Session &se)
{
    se.edit = clip(canonical(se.edit), se.size);
}

/* Lists the folder of the field; 'select' is the row to select (by name,
 * case ignored), 'keys' gives the list the keyboard focus. */
void show_folder(Session &se, const std::string &select, bool keys)
{
    State &st = state();
    const std::string dir = folder_of(se.edit);
    {
        zcui::Lock lock;
        st.loading = true;
    }
    zcui::request_redraw();

    std::vector<zcui_fs::Entry> entries;
    bool readable = zcui_fs::list_dir(dir, entries);

    std::vector<Row> dirs, files;
    for(size_t i = 0; i < entries.size(); ++i)
    {
        zcui_fs::Entry &e = entries[i];
        if(e.hidden || e.name.empty())
            continue;
        fix_filename_case(&e.name[0]);
        Row r;
        r.label = zcui_fs::native_to_utf8(e.name.c_str());
        r.time_text = zcui_fs::format_time(e.mtime);
        r.highlight = false;
        if(e.dir)
        {
            r.kind = ROW_DIR;
            r.name = e.name + OTHER_PATH_SEPARATOR;
            dirs.push_back(r);
        }
        else if(ext_matches(se.tokens, e.name))
        {
            r.kind = ROW_FILE;
            r.name = e.name;
            r.size_text = format_size(e.size);
            r.highlight = !se.tokens.empty();
            files.push_back(r);
        }
    }
    struct ByName
    {
        bool operator()(const Row &a, const Row &b) const { return name_less(a.name, b.name); }
    };
    std::sort(dirs.begin(), dirs.end(), ByName());
    std::sort(files.begin(), files.end(), ByName());

    std::vector<Row> rows;
    const bool root = is_root(dir);
    if(!root)
    {
        Row up;
        up.kind = ROW_UP;
        up.name = std::string("..") + OTHER_PATH_SEPARATOR;
        up.label = "..";
        up.highlight = false;
        rows.push_back(up);
    }
    rows.insert(rows.end(), dirs.begin(), dirs.end());
    rows.insert(rows.end(), files.begin(), files.end());

    int sel = -1;
    if(!select.empty())
        for(size_t i = 0; i < rows.size(); ++i)
            if(rows[i].kind != ROW_UP && same_ci(rows[i].name, select))
            {
                sel = (int)i;
                break;
            }

    std::vector<Crumb> crumbs = make_crumbs(dir);
    {
        zcui::Lock lock;
        st.dir = dir;
        st.at_root = root;
        st.unreadable = !readable;
        st.crumbs.swap(crumbs);
        st.rows.swap(rows);
        ++st.list_serial;
        st.sel = sel;
        st.scroll_to_sel = sel >= 0;
        st.focus_sel = keys;
        st.loading = false;
    }
    zcui::request_redraw();
}

void finish(Session &se, Result r)
{
    se.done = true;
    se.result = r;
}

/* MSG_KEY of the edit field: open what it names. */
void open_edit(Session &se, bool keys)
{
    std::string s = se.edit;
    if(s.empty() || s[s.size() - 1] == DEVICE_SEPARATOR)
        s = clip(s + "./", se.size);
    s = clip(canonical(s), se.size - 1);    // room for a separator
    const char last = s.empty() ? 0 : s[s.size() - 1];
    if(!is_sep(last))
    {
        int attr = 0;
        if(file_exists(s.c_str(), FA_RDONLY | FA_HIDDEN | FA_DIREC, &attr) && (attr & FA_DIREC))
        {
            std::vector<char> b(s.begin(), s.end());
            b.push_back(0);
            b.push_back(0);
            put_backslash(&b[0]);
            s = &b[0];
        }
        else
        {
            se.edit = s;
            publish_edit(se);
            finish(se, RES_CHOSEN);
            return;
        }
    }
    std::string old_dir;
    {
        zcui::Lock lock;
        old_dir = state().dir;
    }
    se.edit = s;
    publish_edit(se);
    show_folder(se, child_toward(old_dir, folder_of(s)), keys);
}

/* A row became the selection (the original's list proc). */
bool select_row(Session &se, const Request &r)
{
    std::string name;
    {
        zcui::Lock lock;
        State &st = state();
        if(r.dir != st.dir || r.index < 0 || r.index >= (int)st.rows.size())
            return false;               // a request for a list that is gone
        name = st.rows[(size_t)r.index].name;
    }
    se.edit = clip(canonical(join(r.dir, name, se.size)), se.size);
    publish_edit(se);
    return true;
}

void go_to(Session &se, const std::string &folder, bool keys)
{
    std::string old_dir;
    {
        zcui::Lock lock;
        old_dir = state().dir;
    }
    se.edit = clip(folder, se.size);
    edit_canonical(se);
    publish_edit(se);
    show_folder(se, child_toward(old_dir, folder_of(se.edit)), keys);
}

void change_filter(Session &se, int index)
{
    if(!se.list || index < 0 || index == se.filter)
        return;
    int count = 0;
    while(se.list[count].text)
        ++count;
    if(index >= count)
        return;
    se.filter = index;
    const char *ext = se.list[index].ext;
    se.tokens = ext_tokens(ext);

    std::string dir;
    {
        zcui::Lock lock;
        dir = state().dir;
    }
    // A file name whose extension does not fit the new filter is dropped.
    if(ext && get_filename(se.edit.c_str())[0])
    {
        std::vector<std::string> all;
        std::string cur;
        for(const char *p = ext;; ++p)
        {
            if(*p == 0 || *p == ' ' || *p == ',' || *p == ';')
            {
                if(!cur.empty())
                    all.push_back(cur);
                cur.clear();
                if(*p == 0)
                    break;
            }
            else
                cur += *p;
        }
        std::string fext = get_extension(se.edit.c_str());
        bool fits = false;
        for(size_t i = 0; i < all.size(); ++i)
            fits = fits || same_ci(fext, all[i]);
        if(!fits)
            se.edit = join(dir, "", 256);
    }
    edit_canonical(se);
    publish_edit(se);
    show_folder(se, get_filename(se.edit.c_str()), false);
}

void handle(Session &se, const Request &r)
{
    switch(r.kind)
    {
    case REQ_SELECT:
        select_row(se, r);
        break;
    case REQ_OPEN:
        if(select_row(se, r))
            open_edit(se, r.keys);
        break;
    case REQ_EDIT:
        edit_from_view(se, r.text);
        break;
    case REQ_ENTER:
        edit_from_view(se, r.text);
        open_edit(se, r.keys);
        break;
    case REQ_GOTO:
        go_to(se, r.text, r.keys);
        break;
    case REQ_UP:
    {
        std::string dir;
        {
            zcui::Lock lock;
            dir = state().dir;
        }
        if(dir.empty() || is_root(dir))
            break;
        // as selecting ".." and opening it
        se.edit = clip(canonical(join(dir, std::string("..") + OTHER_PATH_SEPARATOR, se.size)), se.size);
        open_edit(se, r.keys);
        break;
    }
    case REQ_OK:
        edit_from_view(se, r.text);
        if(se.mode == MODE_DIR)
            finish(se, RES_OK);
        else
            open_edit(se, r.keys);
        break;
    case REQ_CANCEL:
        edit_from_view(se, r.text);
        finish(se, RES_CANCEL);
        break;
    case REQ_FILTER:
        change_filter(se, r.index);
        break;
    }
}

std::string filter_text(const std::vector<std::string> &tokens)
{
    if(tokens.empty())
        return "All files";
    std::string s;
    for(size_t i = 0; i < tokens.size(); ++i)
    {
        if(i)
            s += "; ";
        s += "*." + zcui_fs::native_to_utf8(tokens[i].c_str());
    }
    return s;
}

/* The selector's places: the game's folder (the working directory) first. */
std::vector<zcui_fs::Place> make_places()
{
    std::vector<zcui_fs::Place> out;
    char cwd[1024];
    _al_getdcwd(_al_getdrive(), cwd, sizeof(cwd) - 1);
    put_backslash(cwd);
    zcui_fs::Place game;
    game.drive = false;
    game.label = "Game folder";
    game.path = canonical(cwd);
    std::string name = game.path.substr(0, game.path.size() - 1);
    game.detail = zcui_fs::native_to_utf8(get_filename(name.c_str()));
    out.push_back(game);
    std::vector<zcui_fs::Place> user = zcui_fs::user_folders();
    for(size_t i = 0; i < user.size(); ++i)
    {
        user[i].path = canonical(user[i].path);
        out.push_back(user[i]);
    }
    return out;
}

/* Runs the selector on se.edit until it closes. */
Result run(Session &se, const char *message, const std::vector<std::string> &filters)
{
    edit_canonical(se);
    if(!zcui::available())
        return RES_CANCEL;              // no window to show it in

    static bool layer_added = false;
    if(!layer_added)
    {
        zcui::add_layer(view_layer, NULL, 900);
        layer_added = true;
    }

    std::vector<zcui_fs::Place> places = make_places();
    std::vector<zcui_fs::Place> drives = zcui_fs::drives();
    for(size_t i = 0; i < drives.size(); ++i)
        drives[i].path = canonical(drives[i].path);
    const char *ok = get_config_text("OK");
    const char *cancel = get_config_text("Cancel");
    {
        zcui::Lock lock;
        State &st = state();
        unsigned serial = st.serial + 1;
        unsigned list_serial = st.list_serial;
        st = State();
        st.serial = serial;
        st.list_serial = list_serial;
        st.mode = se.mode;
        st.title = zcui_fs::native_to_utf8(message);
        st.ok_label = zcui_fs::native_to_utf8(ok ? ok : "OK");
        st.cancel_label = zcui_fs::native_to_utf8(cancel ? cancel : "Cancel");
        st.filters = filters;
        st.filter = se.filter;
        st.filter_text = filter_text(se.tokens);
        st.places = places;
        st.drives = drives;
        st.path_capacity = (size_t)se.size * 3 + 16;
        st.path_utf8 = zcui_fs::native_to_utf8(se.edit.c_str());
        st.path_serial = 1;
        st.loading = true;
        st.active = true;
    }

    zcui::begin_session();
    show_folder(se, get_filename(se.edit.c_str()), false);
    while(!se.done)
    {
        std::vector<Request> reqs;
        {
            zcui::Lock lock;
            reqs.swap(state().requests);
        }
        for(size_t i = 0; i < reqs.size() && !se.done; ++i)
            handle(se, reqs[i]);
        if(se.done)
            break;
#if defined(__SWITCH__)
        if(close_button_quit)   // HOME is closing the software
        {
            finish(se, RES_CANCEL);
            break;
        }
#endif
        if(!zcui::available())
        {
            finish(se, RES_CANCEL);
            break;
        }
        zcui::wait_frame(100);
    }
    {
        zcui::Lock lock;
        State &st = state();
        st.active = false;
        st.requests.clear();
        st.rows.clear();
    }
    zcui::end_session();
    zcui::request_redraw();
    return se.result;
}

/* An empty path starts in the current directory. */
void prepare_path(char *path, int size)
{
    if(path[0])
        return;
    _al_getdcwd(_al_getdrive(), path, size - 1);
    fix_filename_case(path);
    fix_filename_slashes(path);
    put_backslash(path);
}

void before_open()
{
    clear_keybuf();
    while(gui_mouse_b())
        rest(1);
}

void copy_out(char *path, const std::string &s, int size)
{
    std::string c = clip(s, size);
    memcpy(path, c.c_str(), c.size() + 1);
}

/* ".ext" for a name without an extension when 'ext' is a single one. */
void add_extension(char *path, int size, const char *ext)
{
    char *p = get_extension(path);
    if(*p || !ext || strpbrk(ext, " ,;"))
        return;
    int room = size - (int)(p - path) - 1;      // bytes after the '.'
    if(room < 2)
        return;
    *p++ = '.';
    std::string c = clip(ext, room);
    memcpy(p, c.c_str(), c.size() + 1);
}

} // namespace

// ---------------------------------------------------------------------------
// jwinfsel.h
// ---------------------------------------------------------------------------

int jwin_file_select_ex(AL_CONST char *message, char *path, AL_CONST char *ext, int size, int width, int height, FONT *title_font)
{
    (void)width;
    (void)height;
    (void)title_font;
    prepare_path(path, size);
    before_open();

    Session se;
    se.mode = MODE_FILE;
    se.size = size;
    se.edit = path;
    se.tokens = ext_tokens(ext);
    Result r = run(se, message, std::vector<std::string>());
    copy_out(path, se.edit, size);

    if(r == RES_CANCEL || !get_filename(path)[0])
        return FALSE;
    add_extension(path, size, ext);
    return TRUE;
}

int jwin_dfile_select_ex(AL_CONST char *message, char *path, AL_CONST char *ext, int size, int width, int height, FONT *title_font)
{
    (void)width;
    (void)height;
    (void)title_font;
    prepare_path(path, size);
    before_open();

    Session se;
    se.mode = MODE_DIR;
    se.size = size;
    se.edit = path;
    se.tokens = ext_tokens(ext);
    Result r = run(se, message, std::vector<std::string>());
    copy_out(path, se.edit, size);

    if(r == RES_CANCEL)
        return FALSE;
    if(!get_filename(path)[0])
        return TRUE;
    add_extension(path, size, ext);
    return TRUE;
}

int jwin_file_browse_ex(AL_CONST char *message, char *path, EXT_LIST *list, int *list_sel, int size, int width, int height, FONT *title_font)
{
    (void)width;
    (void)height;
    (void)title_font;
    prepare_path(path, size);
    before_open();

    std::vector<std::string> names;
    for(int i = 0; list[i].text; ++i)
        names.push_back(zcui_fs::native_to_utf8(list[i].text));

    Session se;
    se.mode = MODE_BROWSE;
    se.size = size;
    se.edit = path;
    se.list = list;
    se.filter = *list_sel;
    se.tokens = ext_tokens(list[*list_sel].ext);
    Result r = run(se, message, names);
    copy_out(path, se.edit, size);

    if(r == RES_CANCEL || !get_filename(path)[0])
        return FALSE;
    *list_sel = se.filter;
    return TRUE;                        // no extension added (see the top)
}

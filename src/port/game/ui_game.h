/*
 * zplayer (ZC_PORT): Dear ImGui stand-ins for the jwin drawing the player did
 * outside its dialogs, called from the game thread. The drawing itself
 * happens in UI layers on the platform thread (zcport/ui.h).
 *
 *  - msgbox_*: the progress message box of src/zsys.cpp (box_start, box_out,
 *    box_eol, box_end, box_pause), shown over the game, not modal.
 *  - prompt_*: the small "Press a key" boxes of the key and button settings
 *    (src/zc_sys.cpp).
 *
 * The file selector (the functions of src/jwinfsel.h) is
 * src/port/game/ui_filesel.cpp; directory listing and text conversion for
 * both are in src/port/game/ui_fs_os.cpp (namespace zcui_fs).
 *
 * C++ only. Include after <allegro.h>; needs neither ImGui nor SDL.
 */

#ifndef ZC_PORT_GAME_UI_GAME_H
#define ZC_PORT_GAME_UI_GAME_H

#include <stdint.h>
#include <string>
#include <vector>

namespace zcui_game
{

/* The message box. msgbox_start() opens it (title may be NULL), msgbox_text()
 * appends to the current line, msgbox_eol() starts a new one,
 * msgbox_wait(true) marks it as waiting for a key (box_pause, box_end(true)),
 * msgbox_end() closes it. Text is in the game's (native) encoding. */
void msgbox_start(const char *title);
void msgbox_text(const char *text);
void msgbox_eol();
void msgbox_wait(bool waiting);
void msgbox_end();

/* A small centred box with 'count' lines (at most 4), shown over everything
 * until prompt_hide(). */
void prompt_show(const char *const *lines, int count);
void prompt_hide();

} // namespace zcui_game

namespace zcui_fs
{

/* File names in the game's encoding ("native": the ANSI code page on
 * Windows, which zcport's file functions use; UTF-8 elsewhere) to and from
 * UTF-8 for ImGui. */
std::string native_to_utf8(const char *s);
std::string utf8_to_native(const char *s);

struct Entry
{
    std::string name;            // native
    bool dir;
    bool hidden;                 // hidden or system
    uint64_t size;     // bytes (files)
    int64_t mtime;             // seconds since 1970, 0 = unknown
};

/* Lists 'dir' (native, ending with a separator) without "." and "..".
 * Returns false if it cannot be read. */
bool list_dir(const std::string &dir, std::vector<Entry> &out);

struct Place
{
    std::string label;           // UTF-8
    std::string detail;          // UTF-8, e.g. "Local Disk"
    std::string path;            // native, ending with a separator
    bool drive;
};

/* Drives (Windows: the drive letters, without touching removable media) and
 * a few common folders that exist (Desktop, Documents, Downloads). */
std::vector<Place> drives();
std::vector<Place> user_folders();

/* "2026-11-07 14:03" in local time, "" for 0. */
std::string format_time(int64_t t);

} // namespace zcui_fs

#endif /* ZC_PORT_GAME_UI_GAME_H */

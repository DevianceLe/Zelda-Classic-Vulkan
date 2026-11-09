// zplayer: the game's jwin dialogs, menus and alerts shown with Dear ImGui.
//
// What the game itself calls (zc_sys.cpp, under ZC_PORT). The dialog host
// and the jwin replacement are in ui_dialogs_*.cpp and jwin_port.cpp; see
// ui_dialogs_internal.h for how they fit together.

#ifndef ZPLAYER_UI_DIALOGS_H
#define ZPLAYER_UI_DIALOGS_H

// A "Press a key" prompt over the open dialogs while the game waits for a
// key or joystick button itself (kb_getkey(), j_getbtn() with
// next_press_key() / next_press_btn()). Between begin and end the keyboard,
// mouse and joysticks reach the game as during play, so the game's own wait
// sees them; the dialogs below take no input. Lines may be NULL.
void zcui_dialog_prompt_begin(const char *line1, const char *line2, const char *line3);
void zcui_dialog_prompt_end();

// ZQuest (zqeditor). The player uses none of these.
struct DIALOG;

// Game procedures that draw themselves (tile and combo pickers, previews) are
// shown as live pictures inside their dialogs and receive mouse clicks.
void zcui_dialogs_enable_canvases(float text_scale);

// 'dialog' runs for the whole session under a software-drawn editor: only
// its menu bar is shown, and the editor keeps the mouse and keyboard.
void zcui_dialogs_mark_overlay(DIALOG *dialog);

// A full-screen software page (tile editor, combo pages, ...) runs between
// these: the dialogs below it are hidden, and it draws to the real screen and
// reads the real mouse and keyboard. Dialogs it opens are shown as usual.
void zcui_page_begin();
void zcui_page_end();

#endif

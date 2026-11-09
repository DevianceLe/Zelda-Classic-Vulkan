/*
 * zcport extension: a UI host for the dialog manager.
 *
 * zplayer shows its dialogs with Dear ImGui instead of drawing them into the
 * game's 8-bit screen. To keep the game's DIALOG tables and their logic
 * unchanged, the dialog manager hands every dialog to a registered host:
 *
 *   - init_dialog() links the new player as usual (active_dialog is the
 *     dialog) and then offers it to the host's start(). A non-NULL return
 *     value means the host runs this dialog; NULL leaves it to the built-in
 *     dialog manager (procedures get MSG_START, MSG_DRAW, MSG_CLICK, ...).
 *   - update_dialog() of a hosted dialog returns the host's update(); FALSE
 *     closes it, with player->obj holding the object that closed it (-1 for
 *     none), as Allegro reports it.
 *   - shutdown_dialog() calls the host's end() before the player is
 *     unlinked and returns player->obj.
 *   do_dialog() and popup_dialog() are built on these, so they follow.
 *
 * The DIALOG array stays the caller's: the host reads it and changes it the
 * way the procedures do (flags, d1, d2, dp text). Everything here runs on
 * the thread that runs the dialog.
 *
 * Without a host (the default) the dialog manager behaves exactly as before;
 * the conformance tests never register one.
 *
 * C++98 compatible header.
 */

#ifndef ZCPORT_GUI_HOST_H
#define ZCPORT_GUI_HOST_H

#ifdef __cplusplus
   extern "C" {
#endif

struct DIALOG;
struct DIALOG_PLAYER;

typedef struct ZCPORT_GUI_HOST
{
   /* A new dialog (player->dialog). Return the host's state for it, or NULL
      to leave it to the built-in dialog manager. */
   void *(*start)(struct DIALOG_PLAYER *player, int focus_obj);

   /* One step of a hosted dialog: update_dialog()'s return value. */
   int (*update)(void *state, struct DIALOG_PLAYER *player);

   /* The dialog ends (shutdown_dialog()); the state is not used again. */
   void (*end)(void *state, struct DIALOG_PLAYER *player);
} ZCPORT_GUI_HOST;

/* Registers the host (NULL: none). Dialogs already running keep the host
   they started with. */
void zcport_set_gui_host(const ZCPORT_GUI_HOST *host);
const ZCPORT_GUI_HOST *zcport_gui_host(void);

/* Non-zero while 'd' is an object of a dialog a host runs. The standard
   procedures that draw into the GUI bitmap (d_bitmap_proc, d_icon_proc)
   draw nothing for such objects: the host shows them itself. */
int zcport_gui_host_owns(const struct DIALOG *d);

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_GUI_HOST_H */

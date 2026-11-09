// zcport GUI module: private declarations shared by the gui_*.cpp files.
// Not a contract; other modules must not include this.
//
// File layout (each file only needs what its link dependencies provide, so
// the dialog manager links without the text and blit modules):
//   gui_engine.cpp      dialog manager, message dispatch, focus, mouse, keys
//   gui_focus_order.cpp arrow-key focus ordering
//   gui_joystick.cpp    joystick-to-key translation of the dialog manager
//   gui_procs_basic.cpp d_yield/d_clear/d_box/d_shadow_box/d_keyboard_proc
//   gui_procs_blit.cpp  d_bitmap/d_icon/d_slider_proc (blit module)
//   gui_popup.cpp       popup_dialog (blit module)
//   gui_text.cpp        gui_textout_ex, gui_strlen, text procs and the
//                       gui_*_proc hooks (text module)

#ifndef ZCPORT_GUI_INTERNAL_H
#define ZCPORT_GUI_INTERNAL_H

#include <allegro.h>
#include <allegro/internal/aintern.h>
#include "zcport/core.h"
#include "zcport/platform.h"

namespace zcport_gui
{
    // ---- joystick (gui_joystick.cpp) ----------------------------------------

    // What the first joystick asks the dialog manager to do.
    struct JoyRequest
    {
        bool released;   // stick 0 centred and buttons 0/1 up
        int key;         // readkey()-format key it stands for, 0 = none
    };

    JoyRequest read_joystick();

    // ---- arrow keys (gui_focus_order.cpp) -----------------------------------

    enum Axis { AXIS_HORIZONTAL, AXIS_VERTICAL };

    // Sorts the object indices in 'order' (n entries) by how close each
    // object of 'dialog' is to 'from' along 'axis', the order in which the
    // arrow keys offer them the focus. The sort is the one the reference
    // uses, so objects at the same distance come out in the same order.
    void order_by_distance(const DIALOG *dialog, int from, Axis axis, int *order, int n);

    // ---- keyboard shortcuts ----------------------------------------------------

    // Lower case for comparing keyboard shortcuts (ASCII and Latin-1).
    int shortcut_lower(int c);

    // ---- drawing helpers -------------------------------------------------------

    // Sends MSG_DRAW the way object_message() does (hidden objects are not
    // drawn, the mouse pointer is kept off the area).
    int draw_object(DIALOG *d, int c);
}

#endif

// zcport foundation: private declarations shared by the foundation_*.cpp
// files. Not part of any contract; other modules must not include this.

#ifndef ZCPORT_FOUNDATION_INTERNAL_H
#define ZCPORT_FOUNDATION_INTERNAL_H

#include <allegro.h>
#include <allegro/internal/aintern.h>
#include "zcport/core.h"
#include "zcport/input.h"
#include "zcport/platform.h"

#include <mutex>

namespace zcport_foundation
{
    // Generates software key repeat; called from zcport_pump_events().
    void keyboard_update_repeat();

    // Mouse range defaults follow the display size; called when the
    // display changes size (from install_mouse and the pointer query).
    void mouse_reset_range_to_display();

    // One lock for the input state (keyboard buffer, mouse fields).
    std::recursive_mutex &input_lock();
}

#endif

// zcport platform module (gfx mode, timers, joystick, sound stubs):
// private declarations shared by the src/port/al/platform_*.cpp files.
// Not part of any contract; other modules must not include this.

#ifndef ZCPORT_PLATFORM_INTERNAL_H
#define ZCPORT_PLATFORM_INTERNAL_H

#include <allegro.h>
#include <allegro/internal/aintern.h>
#include "zcport/core.h"
#include "zcport/platform.h"

namespace zcport_platform
{
    // retrace_count runs at this rate (70 Hz without a graphics mode, the
    // refresh rate of the display with one). Thread-safe.
    void timer_set_retrace_rate(int hz);
}

#endif

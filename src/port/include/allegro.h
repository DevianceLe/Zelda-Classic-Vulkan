/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro.h - umbrella header, same include order as Allegro 4.4.
 *
 * This is NOT Allegro. It is an independent implementation of the subset
 * of the Allegro 4.4 API that the Zelda Classic player uses, on top of
 * SDL3 + Vulkan (src/port). The real Allegro headers live in
 * allegro/include and are used by ZQuest and by the reference build.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_H
#define ALLEGRO_H

#include "allegro/base.h"

#include "allegro/system.h"
#include "allegro/debug.h"

#include "allegro/unicode.h"

#include "allegro/mouse.h"
#include "allegro/timer.h"
#include "allegro/keyboard.h"
#include "allegro/joystick.h"

#include "allegro/palette.h"
#include "allegro/gfx.h"
#include "allegro/color.h"
#include "allegro/draw.h"
#include "allegro/rle.h"
#include "allegro/compiled.h"
#include "allegro/text.h"
#include "allegro/font.h"

#include "allegro/config.h"
#include "allegro/gui.h"
#include "allegro/sound.h"

#include "allegro/file.h"
#include "allegro/lzss.h"
#include "allegro/datafile.h"

#include "allegro/fixed.h"
#include "allegro/fmaths.h"
#include "allegro/3d.h"

#ifndef ALLEGRO_NO_COMPATIBILITY
   #include "allegro/alcompat.h"
#endif

#ifndef ALLEGRO_NO_FIX_CLASS
   #ifdef __cplusplus
      #include "allegro/fix.h"
   #endif
#endif

#endif /* ALLEGRO_H */

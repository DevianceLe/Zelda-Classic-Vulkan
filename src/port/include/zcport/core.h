/*
 * zcport internal contract: foundation helpers shared by all modules.
 *
 * Include after <allegro.h>. C++98 compatible header.
 */

#ifndef ZCPORT_CORE_H
#define ZCPORT_CORE_H

#include "platform.h"

#ifdef __cplusplus
   extern "C" {
#endif

struct BITMAP;

/* ---- system ---------------------------------------------------------------- */

/* Nonzero between install_allegro() and allegro_exit(). */
int zcport_system_installed(void);

/* Copies msg (truncated) into allegro_error[ALLEGRO_ERROR_SIZE]. */
void zcport_set_error(const char *msg);

/* Writes 'msg' unformatted through al_trace's handler / allegro.log path
 * (useful for very long messages). */
void zcport_trace_raw(const char *msg);

/* ---- display --------------------------------------------------------------- */

/* The bitmap set_gfx_mode() created and the platform presents, or NULL.
 * Unlike 'screen' (which game code may reassign temporarily) this always
 * names the real display. Set by the gfx mode module (foundation defines
 * the variable). */
extern struct BITMAP *zcport_display;

/* Unified engine only: set_gfx_mode(w, h, v_w, v_h) opens a v_w*v_h display. */
extern int zcport_gfx_fit_virtual;
/* Unified engine only: config values end at " #?" (a note follows). */
extern int zcport_config_value_notes;
/* Unified engine only: shows bmp (and keeps showing it) instead of the screen. */
void zcport_present_bitmap(struct BITMAP *bmp);

/* The gfx mode module calls this after set_gfx_mode() changed the display:
 * the default mouse range becomes the new display size (unless the game
 * set one with set_mouse_range()) and the position is clamped to it.
 * (destroy_bitmap() of the old screen already did show_mouse(NULL).) */
void zcport_mouse_display_changed(void);

/* Defined by the gfx mode module, for show_mouse() to call with its
 * argument: like the reference's windowed drivers (which use a hardware
 * cursor), gfx_capabilities has GFX_HW_CURSOR exactly while the pointer is
 * shown on the screen (bmp is the display or shares its surface) in a
 * graphics mode; scare_mouse() does not change it. set_gfx_mode() clears it
 * (it hides the pointer). */
void zcport_gfx_mouse_shown(struct BITMAP *bmp);

/* ---- bitmaps --------------------------------------------------------------- */

/* Bytes per pixel for a color depth: 8->1, 15/16->2, 24->3, 32->4, else 0. */
int zcport_bytes_per_pixel(int color_depth);

/* create_bitmap_ex() with extra BMP_ID_* flags (e.g. BMP_ID_VIDEO for the
 * screen, BMP_ID_SYSTEM). Returns NULL for an unsupported depth, negative
 * size or out of memory. */
struct BITMAP *zcport_create_bitmap_flags(int color_depth, int width, int height, unsigned long id_flags);

/* Bytes between the start of two rows of 'bmp' (also for sub-bitmaps):
 * the root bitmap's width times bytes per pixel. */
int zcport_bitmap_pitch(struct BITMAP *bmp);

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_CORE_H */

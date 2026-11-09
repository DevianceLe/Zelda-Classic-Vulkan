/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/internal/aintern.h - the internal symbols the game uses
 * (memory helpers, _mouse_screen, _gfx_mode_set_count, FONT_VTABLE, drive
 * helpers) plus the state zcport modules share with each other.
 *
 * Ownership of the shared variables (which module defines them) is noted
 * next to each declaration.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_AINTERN_H
#define ALLEGRO_AINTERN_H

#ifndef ALLEGRO_H
   #include "../../allegro.h"
#endif

#ifdef __cplusplus
   extern "C" {
#endif

/* ---- memory (foundation) ------------------------------------------------- */

#define _AL_MALLOC(SIZE)         (_al_malloc(SIZE))
#define _AL_MALLOC_ATOMIC(SIZE)  (_al_malloc(SIZE))
#define _AL_FREE(PTR)            (_al_free(PTR))
#define _AL_REALLOC(PTR, SIZE)   (_al_realloc(PTR, SIZE))

AL_FUNC(void *, _al_malloc, (size_t size));
AL_FUNC(void, _al_free, (void *mem));
AL_FUNC(void *, _al_realloc, (void *mem, size_t size));
AL_FUNC(char *, _al_strdup, (AL_CONST char *string));

/* size 0 frees and returns NULL; NULL ptr acts as malloc; if the block
 * cannot be resized the old block is freed and NULL returned. */
AL_FUNC(void *, _al_sane_realloc, (void *ptr, size_t size));
AL_FUNC(char *, _al_sane_strncpy, (char *dest, const char *src, size_t n));

/* ---- exit functions (foundation) ---------------------------------------- */

/* Registers a function allegro_exit() calls (most recent first). Modules
 * use this for remove_keyboard/remove_timer/remove_sound etc. Adding the
 * same function twice keeps one entry. */
AL_FUNC(void, _add_exit_func, (AL_METHOD(void, func, (void)), AL_CONST char *desc));
AL_FUNC(void, _remove_exit_func, (AL_METHOD(void, func, (void))));

/* ---- file system wrappers (file module) --------------------------------- */

AL_FUNC(int, _al_file_isok, (AL_CONST char *filename));
AL_FUNC(uint64_t, _al_file_size_ex, (AL_CONST char *filename));
AL_FUNC(time_t, _al_file_time, (AL_CONST char *filename));
AL_FUNC(int, _al_drive_exists, (int drive));
AL_FUNC(int, _al_getdrive, (void));
AL_FUNC(void, _al_getdcwd, (int drive, char *buf, int size));

/* ---- graphics (foundation unless noted) --------------------------------- */

#define BYTES_PER_PIXEL(bpp)     (((int)(bpp) + 7) / 8)

/* current color depth for create_bitmap() (set_color_depth) */
AL_VAR(int, _color_depth);

/* incremented by every successful set_gfx_mode() (gfx mode module) */
AL_VAR(int, _gfx_mode_set_count);

/* color conversion mode set by set_color_conversion() (gfx mode module) */
AL_VAR(int, _color_conv);

/* The color depth a bitmap of 'depth' bits per pixel read from a file is
 * converted to (hasalpha: a 32-bit image with an alpha channel): the
 * current color depth (set_color_depth) if the conversion mode
 * (set_color_conversion) has the COLORCONV_* bit for that pair (the
 * COLORCONV_32A_* bits when hasalpha), else 'depth' unchanged.
 * Used by the game's own GIF loader, src/load_gif.cpp (owner: files). */
AL_FUNC(int, _color_load_depth, (int depth, int hasalpha));

/* the static pixel-format tables (NULL for unsupported depths) */
AL_FUNC(GFX_VTABLE *, _get_vtable, (int color_depth));

/* ---- mouse (foundation) ------------------------------------------------- */

AL_VAR(BITMAP *, _mouse_screen);     /* bitmap set by show_mouse() */
AL_VAR(int, _mouse_installed);

/* ---- drawing mode state (drawing module) -------------------------------- */

AL_VAR(int, _drawing_mode);
AL_VAR(BITMAP *, _drawing_pattern);
AL_VAR(int, _drawing_x_anchor);
AL_VAR(int, _drawing_y_anchor);
AL_VAR(unsigned int, _drawing_x_mask);
AL_VAR(unsigned int, _drawing_y_mask);

/* ---- truecolor blender state (color module) ----------------------------- */

AL_VAR(BLENDER_FUNC, _blender_func15);
AL_VAR(BLENDER_FUNC, _blender_func16);
AL_VAR(BLENDER_FUNC, _blender_func24);
AL_VAR(BLENDER_FUNC, _blender_func32);
AL_VAR(BLENDER_FUNC, _blender_func15x);
AL_VAR(BLENDER_FUNC, _blender_func16x);
AL_VAR(BLENDER_FUNC, _blender_func24x);
AL_VAR(int, _blender_col_15);
AL_VAR(int, _blender_col_16);
AL_VAR(int, _blender_col_24);
AL_VAR(int, _blender_col_32);
AL_VAR(int, _blender_alpha);

/* ---- palette lookup tables behind palette_color (color module) ---------- */

AL_ARRAY(int, _palette_color8);
AL_ARRAY(int, _palette_color15);
AL_ARRAY(int, _palette_color16);
AL_ARRAY(int, _palette_color24);
AL_ARRAY(int, _palette_color32);

/* ---- fonts --------------------------------------------------------------- */

typedef struct FONT_VTABLE
{
   AL_METHOD(int, font_height, (AL_CONST FONT *f));
   AL_METHOD(int, char_length, (AL_CONST FONT *f, int ch));
   AL_METHOD(int, text_length, (AL_CONST FONT *f, AL_CONST char *text));
   AL_METHOD(int, render_char, (AL_CONST FONT *f, int ch, int fg, int bg, BITMAP *bmp, int x, int y));
   AL_METHOD(void, render, (AL_CONST FONT *f, AL_CONST char *text, int fg, int bg, BITMAP *bmp, int x, int y));
   AL_METHOD(void, destroy, (FONT *f));

   AL_METHOD(int, get_font_ranges, (FONT *f));
   AL_METHOD(int, get_font_range_begin, (FONT *f, int range));
   AL_METHOD(int, get_font_range_end, (FONT *f, int range));
   AL_METHOD(FONT *, extract_font_range, (FONT *f, int begin, int end));
   AL_METHOD(FONT *, merge_fonts, (FONT *f1, FONT *f2));
   AL_METHOD(int, transpose_font, (FONT *f, int drange));
} FONT_VTABLE;

/* The vtable of every zcport font (color module; see zcport/font_internal.h). */
AL_VAR(FONT_VTABLE, zcport_font_vtable);

/* ---- misc ---------------------------------------------------------------- */

#define _AL_RAND_MAX  0xFFFF
AL_FUNC(void, _al_srand, (int seed));
AL_FUNC(int, _al_rand, (void));

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_AINTERN_H */

/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/font.h - the FONT type and font utilities.
 *
 * FONT->data points to a ZCPORT_FONT_DATA (see zcport/font_internal.h) for
 * every font zcport creates; FONT->vtable points to zcport_font_vtable.
 * FONT_VTABLE itself is declared in allegro/internal/aintern.h, as in
 * Allegro (the game calls font->vtable->char_length directly).
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_FONT_H
#define ALLEGRO_FONT_H

#include "base.h"

#ifdef __cplusplus
   extern "C" {
#endif

struct BITMAP;
struct RGB;

typedef struct FONT_GLYPH           /* a single monochrome font character */
{
   short w, h;
   ZERO_SIZE_ARRAY(unsigned char, dat);
} FONT_GLYPH;

struct FONT_VTABLE;

typedef struct FONT
{
   void *data;
   int height;
   struct FONT_VTABLE *vtable;
} FONT;

AL_FUNC(int, font_has_alpha, (FONT *f));
AL_FUNC(void, make_trans_font, (FONT *f));

AL_FUNC(int, is_trans_font, (FONT *f));
AL_FUNC(int, is_color_font, (FONT *f));
AL_FUNC(int, is_mono_font, (FONT *f));
AL_FUNC(int, is_compatible_font, (FONT *f1, FONT *f2));

AL_FUNC(void, register_font_file_type, (AL_CONST char *ext, FONT *(*load)(AL_CONST char *filename, struct RGB *pal, void *param)));
AL_FUNC(FONT *, load_font, (AL_CONST char *filename, struct RGB *pal, void *param));

AL_FUNC(FONT *, load_dat_font, (AL_CONST char *filename, struct RGB *pal, void *param));
AL_FUNC(FONT *, load_bios_font, (AL_CONST char *filename, struct RGB *pal, void *param));
AL_FUNC(FONT *, load_grx_font, (AL_CONST char *filename, struct RGB *pal, void *param));
AL_FUNC(FONT *, load_grx_or_bios_font, (AL_CONST char *filename, struct RGB *pal, void *param));
AL_FUNC(FONT *, load_bitmap_font, (AL_CONST char *fname, struct RGB *pal, void *param));
AL_FUNC(FONT *, load_txt_font, (AL_CONST char *fname, struct RGB *pal, void *param));

AL_FUNC(FONT *, grab_font_from_bitmap, (struct BITMAP *bmp));

AL_FUNC(int, get_font_ranges, (FONT *f));
AL_FUNC(int, get_font_range_begin, (FONT *f, int range));
AL_FUNC(int, get_font_range_end, (FONT *f, int range));
AL_FUNC(FONT *, extract_font_range, (FONT *f, int begin, int end));
AL_FUNC(FONT *, merge_fonts, (FONT *f1, FONT *f2));
AL_FUNC(int, transpose_font, (FONT *f, int drange));

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_FONT_H */

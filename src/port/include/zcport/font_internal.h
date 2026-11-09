/*
 * zcport internal contract: the FONT data model.
 *
 * Every FONT zcport creates has
 *    f->vtable == &zcport_font_vtable
 *    f->data   -> ZCPORT_FONT_DATA
 *    f->height == the font height (text_height())
 *
 * A font is a list of glyph ranges. Each range covers the code points
 * [begin, end) and is either monochrome or color:
 *    mono  glyph pixels are 0 (background) or 1 (foreground); textout
 *          draws 1 in the text color, 0 in bg (or not at all if bg == -1).
 *    color glyph pixels are color values of the glyph bitmap's depth; the
 *          glyph's mask color (0 for 8 bpp) is transparent. With text color
 *          -1 the glyph's own colors are used, otherwise every non-mask
 *          pixel is drawn in the text color (Allegro's color-font rules).
 * Glyph bitmaps are ordinary zcport memory BITMAPs owned by the font:
 * 8 bpp for mono ranges; for color ranges the depth the loader produced
 * (8 bpp for the game's datafile fonts). A glyph's width is its advance
 * (char_length()); glyphs of one font may differ in height and are drawn
 * vertically centred in the font height, (height - glyph height) / 2
 * (C division), like the reference. Build a font with all-mono or
 * all-color ranges (the reference has no mixed fonts; text drawing treats
 * a font by its first range).
 *
 * Who does what:
 *    color module    (src/port/al/color_font*.cpp, color_text.cpp) this data
 *                    model, zcport_font_vtable, allegro_404_char, the
 *                    default 'font', text_length, textout_*, textprintf_*
 *                    and rendering. The text part installs zcport_font_hooks
 *                    so that vtable->text_length/render_char/render work.
 *    datafile module builds fonts from DAT_FONT objects with the
 *                    functions below (mono glyphs: pixels 0/1 in 8-bit
 *                    bitmaps; color glyphs: the object's bitmaps).
 *
 * Code points missing from every range (or whose slot is NULL) are drawn
 * and measured with the glyph of allegro_404_char; if that is missing as
 * well the character has width 0 and draws nothing. Ranges are searched in
 * the order they were added.
 *
 * C++98 compatible header.
 */

#ifndef ZCPORT_FONT_INTERNAL_H
#define ZCPORT_FONT_INTERNAL_H

#ifdef __cplusplus
   extern "C" {
#endif

struct BITMAP;
struct FONT;

typedef struct ZCPORT_FONT_RANGE
{
   int begin;                  /* first code point */
   int end;                    /* one past the last code point */
   int mono;                   /* nonzero: glyph pixels are 0/1 */
   struct BITMAP **glyphs;     /* end - begin entries; an entry may be NULL */
} ZCPORT_FONT_RANGE;

typedef struct ZCPORT_FONT_DATA
{
   int num_ranges;
   ZCPORT_FONT_RANGE *ranges;  /* in the order they were added */
} ZCPORT_FONT_DATA;

/* Creates an empty font of the given height. */
struct FONT *zcport_font_create(int height);

/* Appends a range [begin, end) to the font and returns it; the glyph slots
 * start NULL. Returns NULL on failure (end <= begin, out of memory).
 * The ranges are one array: the returned pointer is valid until the next
 * zcport_font_add_range() on the same font (fill a range's glyphs before
 * adding the next one, or use data->ranges[i]). */
ZCPORT_FONT_RANGE *zcport_font_add_range(struct FONT *f, int begin, int end, int mono);

/* Gives the glyph for code point 'ch' to the range (the font takes
 * ownership and destroys it with the font; a glyph already in that slot is
 * destroyed). Returns 0, or -1 if ch is not in the range. */
int zcport_font_set_glyph(ZCPORT_FONT_RANGE *range, int ch, struct BITMAP *glyph);

/* The glyph bitmap and range for 'ch' (no 404 substitution); NULL if none.
 * 'range_out' may be NULL. */
struct BITMAP *zcport_font_find_glyph(const struct FONT *f, int ch, const ZCPORT_FONT_RANGE **range_out);

/* Like zcport_font_find_glyph but falls back to allegro_404_char. */
struct BITMAP *zcport_font_get_glyph(const struct FONT *f, int ch, const ZCPORT_FONT_RANGE **range_out);

/* True for fonts made by zcport_font_create(). */
int zcport_is_zcport_font(const struct FONT *f);

/* Frees the font, its ranges and glyphs. */
void zcport_font_destroy(struct FONT *f);

/* Hooks the text module installs for the vtable members that need text
 * iteration or drawing. With no hooks text_length() counts bytes as
 * characters and render/render_char draw nothing. */
typedef struct ZCPORT_FONT_HOOKS
{
   int  (*text_length)(const struct FONT *f, const char *text);
   int  (*render_char)(const struct FONT *f, int ch, int fg, int bg, struct BITMAP *bmp, int x, int y);
   void (*render)(const struct FONT *f, const char *text, int fg, int bg, struct BITMAP *bmp, int x, int y);
} ZCPORT_FONT_HOOKS;

void zcport_font_set_hooks(const ZCPORT_FONT_HOOKS *hooks);

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_FONT_INTERNAL_H */

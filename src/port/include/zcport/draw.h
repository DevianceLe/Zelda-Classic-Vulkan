/*
 * zcport internal contract: drawing-mode state and the pixel/span writers
 * the primitives module implements, for other modules (3D polygons, etc.).
 *
 * Include after <allegro.h>. C++98 compatible header.
 */

#ifndef ZCPORT_DRAW_H
#define ZCPORT_DRAW_H

#ifdef __cplusplus
   extern "C" {
#endif

struct BITMAP;

/* Current drawing mode, as set by drawing_mode()/solid_mode()/xor_mode(). */
int zcport_drawing_mode(void);

/* Writes one pixel at (x, y) of 'bmp' honoring the drawing mode, WITHOUT
 * clipping (the caller has clipped). */
void zcport_put_mode(struct BITMAP *bmp, int x, int y, int color);

/* Fills the span x1..x2 (either order) of row y honoring clipping (when
 * bmp->clip is set) and the drawing mode. Allegro's "hfill". */
void zcport_hfill(struct BITMAP *bmp, int x1, int y, int x2, int color);

/* Blender used by DRAW_MODE_TRANS (and translucent sprites) on hicolor and
 * truecolor bitmaps: returns the blend of 'src' over 'dest' for 'depth'.
 * The color module installs it (set_trans_blender etc.); until then it
 * returns 'src'. */
typedef unsigned long (*zcport_blend_fn)(int depth, unsigned long src, unsigned long dest);
extern zcport_blend_fn zcport_trans_blender;

/* Blender used by lit drawing (draw_lit_sprite, draw_lit_rle_sprite, the
 * *_lit rotation functions) on hicolor and truecolor bitmaps: Allegro's
 * _blender_funcXX(_blender_col_XX, pixel, alpha), i.e.
 * 'pixel' lit by the blender color with strength 'alpha' (0-255). The color
 * module installs it next to zcport_trans_blender; the blit module defines
 * it and until then it returns 'pixel'. */
typedef unsigned long (*zcport_lit_blend_fn)(int depth, unsigned long pixel, int alpha);
extern zcport_lit_blend_fn zcport_lit_blender;

/* The same for lit 3D polygons (POLYTYPE_*_LIT), which call the blender
 * with the arguments the other way round: _blender_funcXX(pixel,
 * _blender_col_XX, alpha). Defined by the blit module (returns 'pixel')
 * until the color module installs it. */
extern zcport_lit_blend_fn zcport_lit_blender_poly;

#ifdef __cplusplus
   }
#endif

#endif /* ZCPORT_DRAW_H */

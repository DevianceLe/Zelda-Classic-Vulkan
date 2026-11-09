/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/gfx.h - BITMAP, graphics modes, bitmap creation and clipping.
 *
 * Bitmap memory model (zcport, all bitmaps are memory bitmaps):
 *   - one contiguous block, rows packed with no padding:
 *       line[y] = (unsigned char *)dat + y * w * BYTES_PER_PIXEL(depth)
 *     (plus a few spare bytes at the end of the block);
 *   - pixel formats: 8 bpp palette index; 15 bpp 0RRRRRGGGGGBBBBB and
 *     16 bpp RRRRRGGGGGGBBBBB in a uint16_t; 24 bpp 3 bytes B,G,R in memory;
 *     32 bpp 0xAARRGGBB in a uint32_t (shifts: see _rgb_*_shift_* in color.h);
 *   - sub-bitmaps share the parent's memory: line[] points into the parent,
 *     x_ofs/y_ofs hold the offset from the root bitmap, id has BMP_ID_SUB;
 *   - vtable->color_depth and vtable->mask_color describe the format.
 *   - the screen (created by set_gfx_mode) is a memory bitmap flagged
 *     BMP_ID_VIDEO that the platform presents.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_GFX_H
#define ALLEGRO_GFX_H

#include "3d.h"
#include "base.h"
#include "fixed.h"

#ifdef __cplusplus
   extern "C" {
#endif

struct RLE_SPRITE;
struct FONT_GLYPH;
struct RGB;

#define GFX_TEXT                       -1
#define GFX_AUTODETECT                 0
#define GFX_AUTODETECT_FULLSCREEN      1
#define GFX_AUTODETECT_WINDOWED        2
#define GFX_SAFE                       AL_ID('S','A','F','E')
#define GFX_NONE                       AL_ID('N','O','N','E')

/* Windows driver ids (alwin.h in Allegro); the game names them under
 * ALLEGRO_WINDOWS. zcport reports GFX_ZCPORT for its own driver. */
#define GFX_DIRECTX              AL_ID('D','X','A','C')
#define GFX_DIRECTX_ACCEL        AL_ID('D','X','A','C')
#define GFX_DIRECTX_SAFE         AL_ID('D','X','S','A')
#define GFX_DIRECTX_SOFT         AL_ID('D','X','S','O')
#define GFX_DIRECTX_WIN          AL_ID('D','X','W','N')
#define GFX_DIRECTX_OVL          AL_ID('D','X','O','V')
#define GFX_GDI                  AL_ID('G','D','I','B')
#define GFX_ZCPORT               AL_ID('Z','C','P','T')

/* Linux / Mac driver ids, named by game code on those platforms. */
#define GFX_XWINDOWS             AL_ID('X','W','I','N')
#define GFX_XWINDOWS_FULLSCREEN  AL_ID('X','W','F','S')
#define GFX_XDGA2                AL_ID('D','G','A','2')
#define GFX_XDGA2_SOFT           AL_ID('D','G','A','S')
#define GFX_FBCON                AL_ID('F','B',' ',' ')
#define GFX_VBEAF                AL_ID('V','B','A','F')
#define GFX_SVGALIB              AL_ID('S','V','G','A')
#define GFX_QUARTZ_FULLSCREEN    AL_ID('Q','Z','F','L')
#define GFX_QUARTZ_WINDOW        AL_ID('Q','Z','W','N')

#define DRAW_SPRITE_NORMAL 0
#define DRAW_SPRITE_LIT 1
#define DRAW_SPRITE_TRANS 2

#define DRAW_SPRITE_NO_FLIP 0x0
#define DRAW_SPRITE_H_FLIP  0x1
#define DRAW_SPRITE_V_FLIP  0x2
#define DRAW_SPRITE_VH_FLIP 0x3

#define blender_mode_none            0
#define blender_mode_trans           1
#define blender_mode_add             2
#define blender_mode_burn            3
#define blender_mode_color           4
#define blender_mode_difference      5
#define blender_mode_dissolve        6
#define blender_mode_dodge           7
#define blender_mode_hue             8
#define blender_mode_invert          9
#define blender_mode_luminance      10
#define blender_mode_multiply       11
#define blender_mode_saturation     12
#define blender_mode_screen         13
#define blender_mode_alpha          14

typedef struct GFX_MODE
{
   int width, height, bpp;
} GFX_MODE;

typedef struct GFX_MODE_LIST
{
   int num_modes;
   GFX_MODE *mode;
} GFX_MODE_LIST;

/* The graphics driver. zcport fills in id/name/desc/ascii_name, w/h (the
 * physical screen size, used by SCREEN_W/SCREEN_H), windowed and linear;
 * the method pointers are NULL. */
typedef struct GFX_DRIVER
{
   int  id;
   AL_CONST char *name;
   AL_CONST char *desc;
   AL_CONST char *ascii_name;
   AL_METHOD(struct BITMAP *, init, (int w, int h, int v_w, int v_h, int color_depth));
   AL_METHOD(void, exit, (struct BITMAP *b));
   AL_METHOD(int, scroll, (int x, int y));
   AL_METHOD(void, vsync, (void));
   AL_METHOD(void, set_palette, (AL_CONST struct RGB *p, int from, int to, int retracesync));
   AL_METHOD(int, request_scroll, (int x, int y));
   AL_METHOD(int, poll_scroll, (void));
   AL_METHOD(void, enable_triple_buffer, (void));
   AL_METHOD(struct BITMAP *, create_video_bitmap, (int width, int height));
   AL_METHOD(void, destroy_video_bitmap, (struct BITMAP *bitmap));
   AL_METHOD(int, show_video_bitmap, (struct BITMAP *bitmap));
   AL_METHOD(int, request_video_bitmap, (struct BITMAP *bitmap));
   AL_METHOD(struct BITMAP *, create_system_bitmap, (int width, int height));
   AL_METHOD(void, destroy_system_bitmap, (struct BITMAP *bitmap));
   AL_METHOD(int, set_mouse_sprite, (struct BITMAP *sprite, int xfocus, int yfocus));
   AL_METHOD(int, show_mouse, (struct BITMAP *bmp, int x, int y));
   AL_METHOD(void, hide_mouse, (void));
   AL_METHOD(void, move_mouse, (int x, int y));
   AL_METHOD(void, drawing_mode, (void));
   AL_METHOD(void, save_video_state, (void));
   AL_METHOD(void, restore_video_state, (void));
   AL_METHOD(void, set_blender_mode, (int mode, int r, int g, int b, int a));
   AL_METHOD(GFX_MODE_LIST *, fetch_mode_list, (void));
   int w, h;                     /* physical (not virtual!) screen size */
   int linear;                   /* true if video memory is linear */
   long bank_size;               /* bank size, in bytes */
   long bank_gran;               /* bank granularity, in bytes */
   long vid_mem;                 /* video memory size, in bytes */
   long vid_phys_base;           /* physical address of video memory */
   int windowed;                 /* true if driver runs windowed */
} GFX_DRIVER;

AL_VAR(GFX_DRIVER *, gfx_driver);
AL_VAR(int, gfx_capabilities);

#define GFX_CAN_SCROLL                    0x00000001
#define GFX_CAN_TRIPLE_BUFFER             0x00000002
#define GFX_HW_CURSOR                     0x00000004
#define GFX_HW_HLINE                      0x00000008
#define GFX_HW_HLINE_XOR                  0x00000010
#define GFX_HW_HLINE_SOLID_PATTERN        0x00000020
#define GFX_HW_HLINE_COPY_PATTERN         0x00000040
#define GFX_HW_FILL                       0x00000080
#define GFX_HW_FILL_XOR                   0x00000100
#define GFX_HW_FILL_SOLID_PATTERN         0x00000200
#define GFX_HW_FILL_COPY_PATTERN          0x00000400
#define GFX_HW_LINE                       0x00000800
#define GFX_HW_LINE_XOR                   0x00001000
#define GFX_HW_TRIANGLE                   0x00002000
#define GFX_HW_TRIANGLE_XOR               0x00004000
#define GFX_HW_GLYPH                      0x00008000
#define GFX_HW_VRAM_BLIT                  0x00010000
#define GFX_HW_VRAM_BLIT_MASKED           0x00020000
#define GFX_HW_MEM_BLIT                   0x00040000
#define GFX_HW_MEM_BLIT_MASKED            0x00080000
#define GFX_HW_SYS_TO_VRAM_BLIT           0x00100000
#define GFX_HW_SYS_TO_VRAM_BLIT_MASKED    0x00200000
#define GFX_SYSTEM_CURSOR                 0x00400000
#define GFX_HW_VRAM_STRETCH_BLIT          0x00800000
#define GFX_HW_VRAM_STRETCH_BLIT_MASKED   0x01000000
#define GFX_HW_SYS_STRETCH_BLIT           0x02000000
#define GFX_HW_SYS_STRETCH_BLIT_MASKED    0x04000000

/* Describes a pixel format. zcport only uses color_depth and mask_color;
 * the remaining members are kept NULL and exist for source compatibility. */
typedef struct GFX_VTABLE
{
   int color_depth;
   int mask_color;
   void *unwrite_bank;
   AL_METHOD(void, set_clip, (struct BITMAP *bmp));
   AL_METHOD(void, acquire, (struct BITMAP *bmp));
   AL_METHOD(void, release, (struct BITMAP *bmp));
} GFX_VTABLE;

AL_VAR(GFX_VTABLE, __linear_vtable8);
AL_VAR(GFX_VTABLE, __linear_vtable15);
AL_VAR(GFX_VTABLE, __linear_vtable16);
AL_VAR(GFX_VTABLE, __linear_vtable24);
AL_VAR(GFX_VTABLE, __linear_vtable32);

typedef struct BITMAP            /* a bitmap structure */
{
   int w, h;                     /* width and height in pixels */
   int clip;                     /* flag if clipping is turned on */
   int cl, cr, ct, cb;           /* clip left, right, top and bottom values */
   GFX_VTABLE *vtable;           /* pixel format description */
   void *write_bank;             /* unused in zcport (NULL) */
   void *read_bank;              /* unused in zcport (NULL) */
   void *dat;                    /* the memory we allocated for the bitmap */
   unsigned long id;             /* for identifying sub-bitmaps */
   void *extra;                  /* points to a structure with more info */
   int x_ofs;                    /* horizontal offset (for sub-bitmaps) */
   int y_ofs;                    /* vertical offset (for sub-bitmaps) */
   int seg;                      /* bitmap segment (0) */
   ZERO_SIZE_ARRAY(unsigned char *, line);
} BITMAP;

#define BMP_ID_VIDEO       0x80000000
#define BMP_ID_SYSTEM      0x40000000
#define BMP_ID_SUB         0x20000000
#define BMP_ID_PLANAR      0x10000000
#define BMP_ID_NOBLIT      0x08000000
#define BMP_ID_LOCKED      0x04000000
#define BMP_ID_AUTOLOCK    0x02000000
#define BMP_ID_MASK        0x01FFFFFF

AL_VAR(BITMAP *, screen);

#define SCREEN_W     (gfx_driver ? gfx_driver->w : 0)
#define SCREEN_H     (gfx_driver ? gfx_driver->h : 0)

#define VIRTUAL_W    (screen ? screen->w : 0)
#define VIRTUAL_H    (screen ? screen->h : 0)

#define COLORCONV_NONE              0

#define COLORCONV_8_TO_15           1
#define COLORCONV_8_TO_16           2
#define COLORCONV_8_TO_24           4
#define COLORCONV_8_TO_32           8
#define COLORCONV_15_TO_8           0x10
#define COLORCONV_15_TO_16          0x20
#define COLORCONV_15_TO_24          0x40
#define COLORCONV_15_TO_32          0x80
#define COLORCONV_16_TO_8           0x100
#define COLORCONV_16_TO_15          0x200
#define COLORCONV_16_TO_24          0x400
#define COLORCONV_16_TO_32          0x800
#define COLORCONV_24_TO_8           0x1000
#define COLORCONV_24_TO_15          0x2000
#define COLORCONV_24_TO_16          0x4000
#define COLORCONV_24_TO_32          0x8000
#define COLORCONV_32_TO_8           0x10000
#define COLORCONV_32_TO_15          0x20000
#define COLORCONV_32_TO_16          0x40000
#define COLORCONV_32_TO_24          0x80000
#define COLORCONV_32A_TO_8          0x100000
#define COLORCONV_32A_TO_15         0x200000
#define COLORCONV_32A_TO_16         0x400000
#define COLORCONV_32A_TO_24         0x800000
#define COLORCONV_DITHER_PAL        0x1000000
#define COLORCONV_DITHER_HI         0x2000000
#define COLORCONV_KEEP_TRANS        0x4000000

#define COLORCONV_DITHER            (COLORCONV_DITHER_PAL | COLORCONV_DITHER_HI)

#define COLORCONV_EXPAND_256        (COLORCONV_8_TO_15 | COLORCONV_8_TO_16 | \
                                     COLORCONV_8_TO_24 | COLORCONV_8_TO_32)

#define COLORCONV_REDUCE_TO_256     (COLORCONV_15_TO_8 | COLORCONV_16_TO_8 | \
                                     COLORCONV_24_TO_8 | COLORCONV_32_TO_8 | \
                                     COLORCONV_32A_TO_8)

#define COLORCONV_EXPAND_15_TO_16    COLORCONV_15_TO_16

#define COLORCONV_REDUCE_16_TO_15    COLORCONV_16_TO_15

#define COLORCONV_EXPAND_HI_TO_TRUE (COLORCONV_15_TO_24 | COLORCONV_15_TO_32 | \
                                     COLORCONV_16_TO_24 | COLORCONV_16_TO_32)

#define COLORCONV_REDUCE_TRUE_TO_HI (COLORCONV_24_TO_15 | COLORCONV_24_TO_16 | \
                                     COLORCONV_32_TO_15 | COLORCONV_32_TO_16)

#define COLORCONV_24_EQUALS_32      (COLORCONV_24_TO_32 | COLORCONV_32_TO_24)

#define COLORCONV_TOTAL             (COLORCONV_EXPAND_256 |          \
                                     COLORCONV_REDUCE_TO_256 |       \
                                     COLORCONV_EXPAND_15_TO_16 |     \
                                     COLORCONV_REDUCE_16_TO_15 |     \
                                     COLORCONV_EXPAND_HI_TO_TRUE |   \
                                     COLORCONV_REDUCE_TRUE_TO_HI |   \
                                     COLORCONV_24_EQUALS_32 |        \
                                     COLORCONV_32A_TO_15 |           \
                                     COLORCONV_32A_TO_16 |           \
                                     COLORCONV_32A_TO_24)

#define COLORCONV_PARTIAL           (COLORCONV_EXPAND_15_TO_16 |     \
                                     COLORCONV_REDUCE_16_TO_15 |     \
                                     COLORCONV_24_EQUALS_32)

#define COLORCONV_MOST              (COLORCONV_EXPAND_15_TO_16 |     \
                                     COLORCONV_REDUCE_16_TO_15 |     \
                                     COLORCONV_EXPAND_HI_TO_TRUE |   \
                                     COLORCONV_REDUCE_TRUE_TO_HI |   \
                                     COLORCONV_24_EQUALS_32)

#define COLORCONV_KEEP_ALPHA        (COLORCONV_TOTAL                 \
                                     & ~(COLORCONV_32A_TO_8 |        \
                                         COLORCONV_32A_TO_15 |       \
                                         COLORCONV_32A_TO_16 |       \
                                         COLORCONV_32A_TO_24))

AL_FUNC(GFX_MODE_LIST *, get_gfx_mode_list, (int card));
AL_FUNC(void, destroy_gfx_mode_list, (GFX_MODE_LIST *gfx_mode_list));

/* foundation (bitmap core) */
AL_FUNC(void, set_color_depth, (int depth));
AL_FUNC(int, get_color_depth, (void));

/* gfx mode / platform module */
AL_FUNC(void, set_color_conversion, (int mode));
AL_FUNC(int, get_color_conversion, (void));
AL_FUNC(void, request_refresh_rate, (int rate));
AL_FUNC(int, get_refresh_rate, (void));
AL_FUNC(int, set_gfx_mode, (int card, int w, int h, int v_w, int v_h));
AL_FUNC(int, scroll_screen, (int x, int y));
AL_FUNC(int, request_scroll, (int x, int y));
AL_FUNC(int, poll_scroll, (void));
AL_FUNC(int, show_video_bitmap, (BITMAP *bitmap));
AL_FUNC(int, request_video_bitmap, (BITMAP *bitmap));
AL_FUNC(int, enable_triple_buffer, (void));

/* foundation (bitmap core) */
AL_FUNC(BITMAP *, create_bitmap, (int width, int height));
AL_FUNC(BITMAP *, create_bitmap_ex, (int color_depth, int width, int height));
AL_FUNC(BITMAP *, create_sub_bitmap, (BITMAP *parent, int x, int y, int width, int height));
AL_FUNC(BITMAP *, create_video_bitmap, (int width, int height));
AL_FUNC(BITMAP *, create_system_bitmap, (int width, int height));
AL_FUNC(void, destroy_bitmap, (BITMAP *bitmap));
AL_FUNC(void, set_clip_rect, (BITMAP *bitmap, int x1, int y_1, int x2, int y2));
AL_FUNC(void, add_clip_rect, (BITMAP *bitmap, int x1, int y_1, int x2, int y2));
AL_FUNC(void, clear_bitmap, (BITMAP *bitmap));

/* gfx mode / platform module */
AL_FUNC(void, vsync, (void));

#define GFX_TYPE_UNKNOWN     0
#define GFX_TYPE_WINDOWED    1
#define GFX_TYPE_FULLSCREEN  2
#define GFX_TYPE_DEFINITE    4
#define GFX_TYPE_MAGIC       8

AL_FUNC(int, get_gfx_mode_type, (int graphics_card));
AL_FUNC(int, get_gfx_mode, (void));

#define SWITCH_NONE           0
#define SWITCH_PAUSE          1
#define SWITCH_AMNESIA        2
#define SWITCH_BACKGROUND     3
#define SWITCH_BACKAMNESIA    4

#define SWITCH_IN             0
#define SWITCH_OUT            1

AL_FUNC(int, set_display_switch_mode, (int mode));
AL_FUNC(int, get_display_switch_mode, (void));
AL_FUNC(int, set_display_switch_callback, (int dir, AL_METHOD(void, cb, (void))));
AL_FUNC(void, remove_display_switch_callback, (AL_METHOD(void, cb, (void))));

AL_FUNC(void, lock_bitmap, (struct BITMAP *bmp));

/* gfx.inl in Allegro: real functions in zcport. */
AL_FUNC(int, is_windowed_mode, (void));                         /* gfx mode / platform */
AL_FUNC(void, clear_to_color, (BITMAP *bitmap, int color));     /* foundation */
AL_FUNC(int, bitmap_color_depth, (BITMAP *bmp));                /* foundation */
AL_FUNC(int, bitmap_mask_color, (BITMAP *bmp));                 /* foundation */
AL_FUNC(int, is_same_bitmap, (BITMAP *bmp1, BITMAP *bmp2));     /* foundation */
AL_FUNC(int, is_linear_bitmap, (BITMAP *bmp));                  /* foundation */
AL_FUNC(int, is_planar_bitmap, (BITMAP *bmp));                  /* foundation */
AL_FUNC(int, is_memory_bitmap, (BITMAP *bmp));                  /* foundation */
AL_FUNC(int, is_screen_bitmap, (BITMAP *bmp));                  /* foundation */
AL_FUNC(int, is_video_bitmap, (BITMAP *bmp));                   /* foundation */
AL_FUNC(int, is_system_bitmap, (BITMAP *bmp));                  /* foundation */
AL_FUNC(int, is_sub_bitmap, (BITMAP *bmp));                     /* foundation */
AL_FUNC(void, acquire_bitmap, (BITMAP *bmp));                   /* foundation (no-op) */
AL_FUNC(void, release_bitmap, (BITMAP *bmp));                   /* foundation (no-op) */
AL_FUNC(void, acquire_screen, (void));                          /* foundation (no-op) */
AL_FUNC(void, release_screen, (void));                          /* foundation (no-op) */
AL_FUNC(int, is_inside_bitmap, (BITMAP *bmp, int x, int y, int clip)); /* foundation */
AL_FUNC(void, get_clip_rect, (BITMAP *bitmap, int *x1, int *y_1, int *x2, int *y2)); /* foundation */
AL_FUNC(void, set_clip_state, (BITMAP *bitmap, int state));     /* foundation */
AL_FUNC(int, get_clip_state, (BITMAP *bitmap));                 /* foundation */

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_GFX_H */

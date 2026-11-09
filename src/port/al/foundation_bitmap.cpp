// zcport foundation: the bitmap core (creation, sub-bitmaps, clipping,
// clearing, identity tests) and the graphics globals other modules fill in
// (screen, gfx_driver, gfx_capabilities, _gfx_mode_set_count, ...).
//
// Written from scratch for zcport (no Allegro code). Behaviour is checked
// against the reference build by tests/conformance/test_foundation_bitmap.cpp.

#include "foundation_internal.h"

#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// graphics globals (the gfx mode module sets them)
// ---------------------------------------------------------------------------

BITMAP *screen = NULL;
GFX_DRIVER *gfx_driver = NULL;
int gfx_capabilities = 0;
int _gfx_mode_set_count = 0;
int _color_conv = COLORCONV_TOTAL;
BITMAP *zcport_display = NULL;

int _color_depth = 8;

// ---------------------------------------------------------------------------
// pixel formats
// ---------------------------------------------------------------------------

GFX_VTABLE __linear_vtable8  = {  8, MASK_COLOR_8,  NULL, NULL, NULL, NULL };
GFX_VTABLE __linear_vtable15 = { 15, MASK_COLOR_15, NULL, NULL, NULL, NULL };
GFX_VTABLE __linear_vtable16 = { 16, MASK_COLOR_16, NULL, NULL, NULL, NULL };
GFX_VTABLE __linear_vtable24 = { 24, MASK_COLOR_24, NULL, NULL, NULL, NULL };
GFX_VTABLE __linear_vtable32 = { 32, MASK_COLOR_32, NULL, NULL, NULL, NULL };

extern "C" GFX_VTABLE *_get_vtable(int color_depth)
{
   switch (color_depth) {
      case 8:  return &__linear_vtable8;
      case 15: return &__linear_vtable15;
      case 16: return &__linear_vtable16;
      case 24: return &__linear_vtable24;
      case 32: return &__linear_vtable32;
   }
   return NULL;
}

extern "C" int zcport_bytes_per_pixel(int color_depth)
{
   switch (color_depth) {
      case 8:  return 1;
      case 15:
      case 16: return 2;
      case 24: return 3;
      case 32: return 4;
   }
   return 0;
}

extern "C" void set_color_depth(int depth)
{
   _color_depth = depth;
}

extern "C" int get_color_depth(void)
{
   return _color_depth;
}

// ---------------------------------------------------------------------------
// creation / destruction
// ---------------------------------------------------------------------------

// Each root bitmap carries a number in the BMP_ID_MASK bits; sub-bitmaps
// copy it, which is what is_same_bitmap() compares.
static unsigned long g_next_bitmap_number = 1;

static unsigned long new_bitmap_number(void)
{
   unsigned long n = g_next_bitmap_number++ & BMP_ID_MASK;
   if (n == 0)
      n = g_next_bitmap_number++ & BMP_ID_MASK;
   return n;
}

static BITMAP *alloc_bitmap_struct(int height)
{
   size_t bytes = sizeof(BITMAP) + sizeof(unsigned char *) * (size_t)(height > 0 ? height : 0);
   return (BITMAP *)calloc(1, bytes);
}

// The bitmap's root width (for row pitch) is kept in 'extra' for
// sub-bitmaps; root bitmaps leave 'extra' NULL.
struct ZcportSubInfo
{
   BITMAP *root;
};

extern "C" BITMAP *zcport_create_bitmap_flags(int color_depth, int width, int height, unsigned long id_flags)
{
   GFX_VTABLE *vt = _get_vtable(color_depth);
   if (!vt || width < 0 || height < 0)
      return NULL;

   int bpp = zcport_bytes_per_pixel(color_depth);
   size_t pitch = (size_t)width * (size_t)bpp;
   // A few spare bytes so code reading a whole 32-bit word at the last
   // pixel of an 8/16/24 bpp bitmap stays inside the block.
   size_t data_bytes = pitch * (size_t)height + 16;

   BITMAP *bmp = alloc_bitmap_struct(height);
   if (!bmp)
      return NULL;

   unsigned char *data = (unsigned char *)calloc(1, data_bytes);
   if (!data) {
      free(bmp);
      return NULL;
   }

   bmp->w = width;
   bmp->h = height;
   bmp->clip = TRUE;
   bmp->cl = 0;
   bmp->cr = width;
   bmp->ct = 0;
   bmp->cb = height;
   bmp->vtable = vt;
   bmp->write_bank = NULL;
   bmp->read_bank = NULL;
   bmp->dat = data;
   bmp->id = (id_flags & ~(unsigned long)BMP_ID_MASK) | new_bitmap_number();
   bmp->extra = NULL;
   bmp->x_ofs = 0;
   bmp->y_ofs = 0;
   bmp->seg = 0;

   for (int y = 0; y < height; y++)
      bmp->line[y] = data + pitch * (size_t)y;

   return bmp;
}

extern "C" BITMAP *create_bitmap_ex(int color_depth, int width, int height)
{
   return zcport_create_bitmap_flags(color_depth, width, height, 0);
}

extern "C" BITMAP *create_bitmap(int width, int height)
{
   return create_bitmap_ex(_color_depth, width, height);
}

extern "C" BITMAP *create_video_bitmap(int width, int height)
{
   // Allegro needs a graphics driver for video bitmaps.
   if (!gfx_driver)
      return NULL;

   // As in the reference, the first screen-sized video bitmap after each
   // set_gfx_mode() is the visible page itself: it shares the display's
   // memory (reads see the screen, writes appear on it).
   static int shared_for_mode = -1;
   if (zcport_display && width == zcport_display->w && height == zcport_display->h &&
       shared_for_mode != _gfx_mode_set_count) {
      BITMAP *page = create_sub_bitmap(zcport_display, 0, 0, width, height);
      if (page) {
         // Its own identity: it is not "the screen" (is_screen_bitmap() is
         // false for it on the reference); only the memory is shared.
         page->id = (page->id & ~(unsigned long)BMP_ID_MASK) | new_bitmap_number() | BMP_ID_VIDEO;
         shared_for_mode = _gfx_mode_set_count;
         return page;
      }
   }

   int depth = screen ? bitmap_color_depth(screen) : _color_depth;
   return zcport_create_bitmap_flags(depth, width, height, BMP_ID_VIDEO);
}

extern "C" BITMAP *create_system_bitmap(int width, int height)
{
   if (!gfx_driver)
      return create_bitmap(width, height);
   int depth = screen ? bitmap_color_depth(screen) : _color_depth;
   return zcport_create_bitmap_flags(depth, width, height, BMP_ID_SYSTEM);
}

static BITMAP *root_of(BITMAP *bmp)
{
   if (bmp && (bmp->id & BMP_ID_SUB) && bmp->extra)
      return ((ZcportSubInfo *)bmp->extra)->root;
   return bmp;
}

extern "C" int zcport_bitmap_pitch(BITMAP *bmp)
{
   BITMAP *root = root_of(bmp);
   if (!root)
      return 0;
   return root->w * zcport_bytes_per_pixel(bitmap_color_depth(root));
}

extern "C" BITMAP *create_sub_bitmap(BITMAP *parent, int x, int y, int width, int height)
{
   if (!parent)
      return NULL;

   if (x < 0)
      x = 0;
   if (y < 0)
      y = 0;
   if (x + width > parent->w)
      width = parent->w - x;
   if (y + height > parent->h)
      height = parent->h - y;
   if (width < 0)
      width = 0;
   if (height < 0)
      height = 0;

   BITMAP *bmp = alloc_bitmap_struct(height);
   if (!bmp)
      return NULL;
   ZcportSubInfo *info = (ZcportSubInfo *)calloc(1, sizeof(ZcportSubInfo));
   if (!info) {
      free(bmp);
      return NULL;
   }
   info->root = root_of(parent);

   int bpp = zcport_bytes_per_pixel(bitmap_color_depth(parent));

   bmp->w = width;
   bmp->h = height;
   bmp->clip = TRUE;
   bmp->cl = 0;
   bmp->cr = width;
   bmp->ct = 0;
   bmp->cb = height;
   bmp->vtable = parent->vtable;
   bmp->write_bank = parent->write_bank;
   bmp->read_bank = parent->read_bank;
   bmp->dat = NULL;
   bmp->id = parent->id | BMP_ID_SUB;
   bmp->extra = info;
   bmp->x_ofs = parent->x_ofs + x;
   bmp->y_ofs = parent->y_ofs + y;
   bmp->seg = parent->seg;

   for (int i = 0; i < height; i++)
      bmp->line[i] = parent->line[y + i] + x * bpp;

   return bmp;
}

extern "C" void destroy_bitmap(BITMAP *bitmap)
{
   if (!bitmap)
      return;

   if (bitmap == _mouse_screen)
      show_mouse(NULL);
   if (bitmap == zcport_display)
      zcport_display = NULL;

   if (bitmap->id & BMP_ID_SUB) {
      free(bitmap->extra);
   }
   else {
      free(bitmap->dat);
   }
   free(bitmap);
}

extern "C" void lock_bitmap(BITMAP *bmp)
{
   (void)bmp;
}

// ---------------------------------------------------------------------------
// clipping
// ---------------------------------------------------------------------------

static int clamp_int(int lo, int v, int hi)
{
   return (v < lo) ? lo : ((v > hi) ? hi : v);
}

extern "C" void set_clip_rect(BITMAP *bitmap, int x1, int y_1, int x2, int y2)
{
   if (!bitmap)
      return;

   // inclusive coordinates in, inclusive-exclusive inside
   x2++;
   y2++;

   bitmap->cl = clamp_int(0, x1, bitmap->w - 1);
   bitmap->ct = clamp_int(0, y_1, bitmap->h - 1);
   bitmap->cr = clamp_int(0, x2, bitmap->w);
   bitmap->cb = clamp_int(0, y2, bitmap->h);
   bitmap->clip = TRUE;
}

extern "C" void add_clip_rect(BITMAP *bitmap, int x1, int y_1, int x2, int y2)
{
   if (!bitmap)
      return;

   int cx1, cy1, cx2, cy2;
   get_clip_rect(bitmap, &cx1, &cy1, &cx2, &cy2);

   if (x1 < cx1) x1 = cx1;
   if (y_1 < cy1) y_1 = cy1;
   if (x2 > cx2) x2 = cx2;
   if (y2 > cy2) y2 = cy2;

   set_clip_rect(bitmap, x1, y_1, x2, y2);
}

extern "C" void get_clip_rect(BITMAP *bitmap, int *x1, int *y_1, int *x2, int *y2)
{
   if (x1)  *x1 = bitmap->cl;
   if (y_1) *y_1 = bitmap->ct;
   if (x2)  *x2 = bitmap->cr - 1;
   if (y2)  *y2 = bitmap->cb - 1;
}

extern "C" void set_clip_state(BITMAP *bitmap, int state)
{
   bitmap->clip = state;
}

extern "C" int get_clip_state(BITMAP *bitmap)
{
   return bitmap->clip;
}

extern "C" int is_inside_bitmap(BITMAP *bmp, int x, int y, int clip)
{
   if (clip) {
      if (bmp->clip)
         return (x >= bmp->cl) && (y >= bmp->ct) && (x < bmp->cr) && (y < bmp->cb);
      return TRUE;
   }
   return ((unsigned int)x < (unsigned int)bmp->w) && ((unsigned int)y < (unsigned int)bmp->h);
}

// ---------------------------------------------------------------------------
// clearing
// ---------------------------------------------------------------------------

extern "C" void clear_to_color(BITMAP *bitmap, int color)
{
   if (!bitmap)
      return;

   int x1 = bitmap->cl, x2 = bitmap->cr;
   int y1 = bitmap->ct, y2 = bitmap->cb;
   if (x1 >= x2 || y1 >= y2)
      return;

   int depth = bitmap_color_depth(bitmap);
   int n = x2 - x1;

   for (int y = y1; y < y2; y++) {
      unsigned char *row = bitmap->line[y];
      switch (depth) {
         case 8:
            memset(row + x1, (unsigned char)color, (size_t)n);
            break;
         case 15:
         case 16: {
            uint16_t c = (uint16_t)color;
            uint16_t *p = (uint16_t *)(row) + x1;
            for (int i = 0; i < n; i++)
               p[i] = c;
            break;
         }
         case 24: {
            unsigned char *p = row + x1 * 3;
            unsigned char b0 = (unsigned char)(color & 0xFF);
            unsigned char b1 = (unsigned char)((color >> 8) & 0xFF);
            unsigned char b2 = (unsigned char)((color >> 16) & 0xFF);
            for (int i = 0; i < n; i++) {
               p[0] = b0;
               p[1] = b1;
               p[2] = b2;
               p += 3;
            }
            break;
         }
         case 32: {
            uint32_t c = (uint32_t)color;
            uint32_t *p = (uint32_t *)(row) + x1;
            for (int i = 0; i < n; i++)
               p[i] = c;
            break;
         }
      }
   }
}

extern "C" void clear_bitmap(BITMAP *bitmap)
{
   clear_to_color(bitmap, 0);
}

// ---------------------------------------------------------------------------
// identity and format queries (gfx.inl in Allegro)
// ---------------------------------------------------------------------------

extern "C" int bitmap_color_depth(BITMAP *bmp)
{
   return bmp->vtable->color_depth;
}

extern "C" int bitmap_mask_color(BITMAP *bmp)
{
   return bmp->vtable->mask_color;
}

extern "C" int is_same_bitmap(BITMAP *bmp1, BITMAP *bmp2)
{
   if (!bmp1 || !bmp2)
      return FALSE;
   if (bmp1 == bmp2)
      return TRUE;
   unsigned long m1 = bmp1->id & BMP_ID_MASK;
   unsigned long m2 = bmp2->id & BMP_ID_MASK;
   return (m1 && m1 == m2) ? TRUE : FALSE;
}

extern "C" int is_linear_bitmap(BITMAP *bmp)
{
   (void)bmp;
   return TRUE;
}

extern "C" int is_planar_bitmap(BITMAP *bmp)
{
   (void)bmp;
   return FALSE;
}

extern "C" int is_memory_bitmap(BITMAP *bmp)
{
   return (bmp->id & (BMP_ID_VIDEO | BMP_ID_SYSTEM)) == 0;
}

extern "C" int is_screen_bitmap(BITMAP *bmp)
{
   return is_same_bitmap(screen, bmp);
}

extern "C" int is_video_bitmap(BITMAP *bmp)
{
   return (bmp->id & BMP_ID_VIDEO) != 0;
}

extern "C" int is_system_bitmap(BITMAP *bmp)
{
   return (bmp->id & BMP_ID_SYSTEM) != 0;
}

extern "C" int is_sub_bitmap(BITMAP *bmp)
{
   return (bmp->id & BMP_ID_SUB) != 0;
}

extern "C" void acquire_bitmap(BITMAP *bmp)
{
   (void)bmp;
}

extern "C" void release_bitmap(BITMAP *bmp)
{
   (void)bmp;
}

extern "C" void acquire_screen(void)
{
}

extern "C" void release_screen(void)
{
}

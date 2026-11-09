// zqeditor: load_bitmap() support for PNG, JPEG and BMP, decoded by the Windows
// Imaging Component (image_wic.cpp). ZQuest is Windows only, so this needs no
// image library. Saving PNG and JPEG stays in zcport (files_pngjpg.cpp).
//
// Written from scratch for zcport.

#include "allegro.h"

#include "image_wic.h"

extern "C" void zqeditor_register_image_loaders(void);

namespace
{

BITMAP *load_with_wic(AL_CONST char *filename, RGB *pal)
{
    ZqwicImage image;

    if(zqwic_decode(filename, &image) != 0)
        return NULL;

    BITMAP *b = create_bitmap_ex(image.indexed ? 8 : 32, (int)image.w, (int)image.h);

    if(b)
    {
        const unsigned char *p = image.pixels;

        for(int y = 0; y < b->h; ++y)
        {
            for(int x = 0; x < b->w; ++x)
            {
                if(image.indexed)
                    b->line[y][x] = *p++;
                else
                {
                    ((unsigned int *)b->line[y])[x] = (unsigned int)makecol32(p[2], p[1], p[0]);
                    p += 4;
                }
            }
        }

        if(image.indexed && pal)
        {
            for(int i = 0; i < PAL_SIZE; ++i)
            {
                pal[i].r = (unsigned char)(((image.palette[i] >> 16) & 255) >> 2);
                pal[i].g = (unsigned char)(((image.palette[i] >> 8) & 255) >> 2);
                pal[i].b = (unsigned char)((image.palette[i] & 255) >> 2);
                pal[i].filler = 0;
            }
        }
    }

    zqwic_free(&image);
    return b;
}

} // namespace

// Added after loadpng_init() and jpgalleg_init(): their entries keep saving,
// and load_bitmap() picks the first entry that can load.
void zqeditor_register_image_loaders(void)
{
    static const char *const kinds[] = { "png", "jpg", "jpeg", "bmp" };

    for(int i = 0; i < 4; ++i)
        register_bitmap_file_type(kinds[i], load_with_wic, NULL);
}

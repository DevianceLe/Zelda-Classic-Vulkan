// bitmap->Read() for 2.55/3.0 scripts (q30_scripts_cmds.h): an image file as an 8-bit bitmap.
// PNG and BMP go through SDL3's own readers (SDL_LoadPNG, SDL_LoadBMP); the others load_bitmap
// reads (PCX, TGA). An indexed image keeps its colour indexes, as upstream's Allegro readers
// keep them; a true-colour one takes the nearest colours of the palette in use (upstream gives
// it an optimised palette of its own, which nothing then shows).
//
// Written from scratch for zcport.

#include <allegro.h>

#include <cstring>

#if defined(__has_include)
#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#define Q30_IMAGE_SDL 1
#endif
#endif

#ifdef Q30_IMAGE_SDL
static bool q30_image_ext(const char *path, const char *ext)
{
    const size_t n = strlen(path), m = strlen(ext);
    return n >= m && SDL_strcasecmp(path + n - m, ext) == 0;
}

static BITMAP *q30_image_from_surface(SDL_Surface *s)
{
    BITMAP *bmp = create_bitmap_ex(8, s->w, s->h);

    if(!bmp)
        return NULL;

    if(s->format == SDL_PIXELFORMAT_INDEX8)
    {
        for(int y = 0; y < s->h; ++y)
            memcpy(bmp->line[y], (const unsigned char *)s->pixels + (size_t)y * s->pitch, (size_t)s->w);

        return bmp;
    }

    SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);

    if(!c)
    {
        destroy_bitmap(bmp);
        return NULL;
    }

    PALETTE pal;
    get_palette(pal);

    for(int y = 0; y < c->h; ++y)
    {
        const unsigned char *p = (const unsigned char *)c->pixels + (size_t)y * c->pitch;

        for(int x = 0; x < c->w; ++x, p += 4)
            bmp->line[y][x] = (unsigned char)bestfit_color(pal, p[0] >> 2, p[1] >> 2, p[2] >> 2);
    }

    SDL_DestroySurface(c);
    return bmp;
}
#endif

BITMAP *q30_load_image(const char *path)
{
#ifdef Q30_IMAGE_SDL
    SDL_Surface *s = NULL;

    if(q30_image_ext(path, ".png"))
        s = SDL_LoadPNG(path);
    else if(q30_image_ext(path, ".bmp"))
        s = SDL_LoadBMP(path);

    if(s)
    {
        BITMAP *bmp = q30_image_from_surface(s);
        SDL_DestroySurface(s);
        return bmp;
    }
#endif

    PALETTE pal;
    get_palette(pal);
    BITMAP *bmp = load_bitmap(path, pal);

    if(bmp && bitmap_color_depth(bmp) != 8)
    {
        destroy_bitmap(bmp);
        return NULL;
    }

    return bmp;
}

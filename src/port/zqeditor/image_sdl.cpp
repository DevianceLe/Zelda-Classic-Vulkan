// zqeditor: image decoding for Linux and macOS, where there is no Windows Imaging
// Component (image_wic.cpp). SDL3 itself reads PNG and BMP; JPEG has no decoder here
// yet, so loading one fails as an unknown format. Same interface as image_wic.cpp
// (image_wic.h), so image_loaders.cpp is shared.
//
// Written from scratch for zcport.

#if !defined(_WIN32)

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>

#include "image_wic.h"

namespace
{

bool ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), m = strlen(tail);
    return n >= m && SDL_strcasecmp(s + n - m, tail) == 0;
}

} // namespace

int zqwic_decode(const char *path, ZqwicImage *out)
{
    memset(out, 0, sizeof(*out));
    if(!path)
        return -1;

    SDL_Surface *s = NULL;
    if(ends_with(path, ".png"))
        s = SDL_LoadPNG(path);
    else if(ends_with(path, ".bmp"))
        s = SDL_LoadBMP(path);
    if(!s)
        return -1;

    out->w = (unsigned)s->w;
    out->h = (unsigned)s->h;

    SDL_Palette *pal = SDL_GetSurfacePalette(s);
    if(s->format == SDL_PIXELFORMAT_INDEX8 && pal)
    {
        // kept as indexes with their palette, as WIC gives an 8-bit image
        out->indexed = 1;
        out->pixels = (unsigned char *)malloc((size_t)out->w * out->h);
        if(!out->pixels)
        {
            SDL_DestroySurface(s);
            return -1;
        }
        for(unsigned y = 0; y < out->h; ++y)
            memcpy(out->pixels + (size_t)y * out->w, (const unsigned char *)s->pixels + (size_t)y * s->pitch, out->w);
        for(int i = 0; i < 256; ++i)
        {
            if(i < pal->ncolors)
            {
                const SDL_Color &c = pal->colors[i];
                out->palette[i] = 0xFF000000u | ((unsigned)c.r << 16) | ((unsigned)c.g << 8) | (unsigned)c.b;
            }
            else
                out->palette[i] = 0xFF000000u;
        }
        SDL_DestroySurface(s);
        return 0;
    }

    // everything else as four bytes per pixel, B G R A in memory
    SDL_Surface *c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_BGRA32);
    SDL_DestroySurface(s);
    if(!c)
        return -1;
    out->indexed = 0;
    out->pixels = (unsigned char *)malloc((size_t)out->w * out->h * 4);
    if(!out->pixels)
    {
        SDL_DestroySurface(c);
        return -1;
    }
    for(unsigned y = 0; y < out->h; ++y)
        memcpy(out->pixels + (size_t)y * out->w * 4, (const unsigned char *)c->pixels + (size_t)y * c->pitch, (size_t)out->w * 4);
    SDL_DestroySurface(c);
    return 0;
}

void zqwic_free(ZqwicImage *image)
{
    if(image)
    {
        free(image->pixels);
        image->pixels = NULL;
    }
}

#endif

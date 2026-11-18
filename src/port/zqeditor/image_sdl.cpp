// zqeditor: image decoding for Linux and macOS, where there is no Windows Imaging
// Component (image_wic.cpp). SDL3 itself reads PNG and BMP, and JPEG through the
// decoder it carries for camera frames (SDL_ConvertPixels from SDL_PIXELFORMAT_MJPG).
// The format is told from the file's first bytes, as WIC does, and from its name only
// when they say nothing known. Same interface as image_wic.cpp (image_wic.h), so
// image_loaders.cpp is shared.
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

// A JPEG file's width and height, from its frame header (an SOF marker), or false when
// the data isn't a JPEG.
bool jpeg_size(const unsigned char *d, size_t n, int *w, int *h)
{
    if(n < 4 || d[0] != 0xFF || d[1] != 0xD8)
        return false;
    size_t i = 2;
    while(i + 4 <= n)
    {
        if(d[i] != 0xFF)
            return false;
        unsigned char m = d[i + 1];
        if(m == 0xFF)
        {
            ++i;            // fill byte
            continue;
        }
        if(m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7))
        {
            i += 2;         // markers without a length
            continue;
        }
        size_t len = ((size_t)d[i + 2] << 8) | d[i + 3];
        if(len < 2 || i + 2 + len > n)
            return false;
        // SOF0-SOF15, except DHT (C4), JPG (C8) and DAC (CC)
        if(m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)
        {
            if(len < 7)
                return false;
            *h = (d[i + 5] << 8) | d[i + 6];
            *w = (d[i + 7] << 8) | d[i + 8];
            return *w > 0 && *h > 0;
        }
        if(m == 0xDA || m == 0xD9)
            return false;   // image data or the end before any frame header
        i += 2 + len;
    }
    return false;
}

// A JPEG decoded as a surface of four bytes per pixel, or NULL.
SDL_Surface *load_jpeg(const char *path)
{
    size_t n = 0;
    unsigned char *data = (unsigned char *)SDL_LoadFile(path, &n);
    if(!data)
        return NULL;
    SDL_Surface *s = NULL;
    int w = 0, h = 0;
    if(n <= 0x7FFFFFFF && jpeg_size(data, n, &w, &h))
    {
        s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_BGRA32);
        // (the "pitch" of a JPEG source is its length in bytes)
        if(s && !SDL_ConvertPixels(w, h, SDL_PIXELFORMAT_MJPG, data, (int)n,
                                   SDL_PIXELFORMAT_BGRA32, s->pixels, s->pitch))
        {
            SDL_DestroySurface(s);
            s = NULL;
        }
    }
    SDL_free(data);
    return s;
}

enum Kind { KIND_UNKNOWN, KIND_PNG, KIND_BMP, KIND_JPEG };

// The kind of image a file holds, from its first bytes, as WIC tells it on Windows: a
// JPEG saved with a .png name (common with pictures from the web) still loads.
Kind kind_of_data(const char *path)
{
    unsigned char b[8];
    size_t n = 0;
    if(SDL_IOStream *io = SDL_IOFromFile(path, "rb"))
    {
        n = SDL_ReadIO(io, b, sizeof(b));
        SDL_CloseIO(io);
    }
    static const unsigned char png[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if(n >= 8 && memcmp(b, png, 8) == 0)
        return KIND_PNG;
    if(n >= 3 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF)
        return KIND_JPEG;
    if(n >= 2 && b[0] == 'B' && b[1] == 'M')
        return KIND_BMP;
    return KIND_UNKNOWN;
}

// The kind its name gives, for data whose first bytes say nothing known.
Kind kind_of_name(const char *path)
{
    if(ends_with(path, ".png"))
        return KIND_PNG;
    if(ends_with(path, ".bmp"))
        return KIND_BMP;
    if(ends_with(path, ".jpg") || ends_with(path, ".jpeg"))
        return KIND_JPEG;
    return KIND_UNKNOWN;
}

} // namespace

int zqwic_decode(const char *path, ZqwicImage *out)
{
    memset(out, 0, sizeof(*out));
    if(!path)
        return -1;

    Kind kind = kind_of_data(path);
    if(kind == KIND_UNKNOWN)
        kind = kind_of_name(path);
    SDL_Surface *s = NULL;
    if(kind == KIND_PNG)
        s = SDL_LoadPNG(path);
    else if(kind == KIND_BMP)
        s = SDL_LoadBMP(path);
    else if(kind == KIND_JPEG)
        s = load_jpeg(path);
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

// zcport files module: bitmap file writers and the bitmap file type
// registry (save_bitmap, register_bitmap_file_type).
//
// BMP, PCX and TGA follow their published formats with the choices of the
// reference build (header fields, run-length coding, row order, 6-bit
// palette entries scaled to 8 bits), so the files are byte-identical;
// tests/conformance/test_files_save.cpp compares them. Files are written
// through the packfile layer (a packfile password applies, as in Allegro).
// PNG and JPG writers (loadpng_init / jpgalleg_init) are in
// files_pngjpg.cpp.
//
// Written from scratch for zcport.

#include "files_internal.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <string>
#include <vector>

namespace
{

struct FileType
{
    std::string ext;
    BITMAP *(*load)(const char *filename, RGB *pal);
    int (*save)(const char *filename, BITMAP *bmp, const RGB *pal);
};

std::vector<FileType> &file_types()
{
    static std::vector<FileType> t;

    if(t.empty())
    {
        FileType bmp = { "bmp", NULL, save_bmp };
        FileType lbm = { "lbm", NULL, NULL };
        FileType pcx = { "pcx", load_pcx, save_pcx };
        FileType tga = { "tga", load_tga, save_tga };
        t.push_back(bmp);
        t.push_back(lbm);
        t.push_back(pcx);
        t.push_back(tga);
    }

    return t;
}

bool ext_equal(const std::string &a, const char *b)
{
    size_t n = strlen(b);

    if(a.size() != n)
        return false;

    for(size_t i = 0; i < n; ++i)
        if(tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;

    return true;
}

int scale5(int v)
{
    return (v << 3) | (v >> 2);
}

int scale6(int v)
{
    return (v << 2) | (v >> 4);
}

/* r, g, b (0..255) of pixel (x, y); 8-bit pixels through the palette */
void pixel_rgb(BITMAP *b, const RGB *pal, int x, int y, int *r, int *g, int *bl)
{
    int depth = bitmap_color_depth(b);
    unsigned char *row = b->line[y];

    switch(depth)
    {
    case 8:
    {
        const RGB &p = pal[row[x]];
        *r = scale6(p.r & 0x3F);
        *g = scale6(p.g & 0x3F);
        *bl = scale6(p.b & 0x3F);
        break;
    }

    case 15:
    {
        int c = ((unsigned short *)row)[x];
        *r = scale5((c >> _rgb_r_shift_15) & 0x1F);
        *g = scale5((c >> _rgb_g_shift_15) & 0x1F);
        *bl = scale5((c >> _rgb_b_shift_15) & 0x1F);
        break;
    }

    case 16:
    {
        int c = ((unsigned short *)row)[x];
        *r = scale5((c >> _rgb_r_shift_16) & 0x1F);
        *g = scale6((c >> _rgb_g_shift_16) & 0x3F);
        *bl = scale5((c >> _rgb_b_shift_16) & 0x1F);
        break;
    }

    case 24:
    {
        int c = row[x * 3] | (row[x * 3 + 1] << 8) | (row[x * 3 + 2] << 16);
        *r = (c >> _rgb_r_shift_24) & 0xFF;
        *g = (c >> _rgb_g_shift_24) & 0xFF;
        *bl = (c >> _rgb_b_shift_24) & 0xFF;
        break;
    }

    default:
    {
        uint32_t c = ((uint32_t *)row)[x];
        *r = (int)((c >> _rgb_r_shift_32) & 0xFF);
        *g = (int)((c >> _rgb_g_shift_32) & 0xFF);
        *bl = (int)((c >> _rgb_b_shift_32) & 0xFF);
        break;
    }
    }
}

const RGB *palette_or_current(const RGB *pal)
{
    return pal ? pal : _current_palette;
}

int finish(PACKFILE *f)
{
    bool err = pack_ferror(f) != 0;
    int r = pack_fclose(f);

    if(err || r)
        return err ? (errno ? errno : EIO) : r;

    return 0;
}

} /* namespace */

extern "C" {

void register_bitmap_file_type(AL_CONST char *ext, AL_METHOD(BITMAP *, load, (AL_CONST char *filename, RGB *pal)),
                               AL_METHOD(int, save, (AL_CONST char *filename, BITMAP *bmp, AL_CONST RGB *pal)))
{
    /* appended; save_bitmap uses the first entry for an extension */
    FileType t;
    t.ext = ext ? ext : "";
    t.load = load;
    t.save = save;
    file_types().push_back(t);
}

BITMAP *load_bitmap(AL_CONST char *filename, RGB *pal)
{
    const char *ext = get_extension(filename);
    std::vector<FileType> &t = file_types();

    /* the first entry for the extension that can load */
    for(size_t i = 0; i < t.size(); ++i)
    {
        if(ext_equal(t[i].ext, ext) && t[i].load)
            return zcfiles::fixup_loaded_bitmap(t[i].load(filename, pal), pal);
    }

    return NULL;
}

int save_bitmap(AL_CONST char *filename, BITMAP *bmp, AL_CONST RGB *pal)
{
    const char *ext = get_extension(filename);
    std::vector<FileType> &t = file_types();

    for(size_t i = 0; i < t.size(); ++i)
    {
        if(ext_equal(t[i].ext, ext))
        {
            if(t[i].save)
                return t[i].save(filename, bmp, pal);

            return 1;
        }
    }

    return 1;
}

int save_bmp_pf(PACKFILE *f, BITMAP *bmp, AL_CONST RGB *pal)
{
    int depth = bitmap_color_depth(bmp);
    int w = bmp->w, h = bmp->h;
    bool eight = depth == 8;
    int stride = eight ? (w + 3) & ~3 : (w * 3 + 3) & ~3;
    long image = (long)stride * h;
    long offset = 54 + (eight ? 1024 : 0);
    pal = palette_or_current(pal);

    /* BITMAPFILEHEADER */
    pack_putc('B', f);
    pack_putc('M', f);
    pack_iputl(offset + image, f);
    pack_iputw(0, f);
    pack_iputw(0, f);
    pack_iputl(offset, f);

    /* BITMAPINFOHEADER */
    pack_iputl(40, f);
    pack_iputl(w, f);
    pack_iputl(h, f);
    pack_iputw(1, f);
    pack_iputw(eight ? 8 : 24, f);
    pack_iputl(0, f);
    pack_iputl(image, f);
    pack_iputl(0xB12, f);
    pack_iputl(0xB12, f);
    pack_iputl(eight ? 256 : 0, f);
    pack_iputl(eight ? 256 : 0, f);

    if(eight)
    {
        for(int i = 0; i < 256; ++i)
        {
            pack_putc(scale6(pal[i].b & 0x3F), f);
            pack_putc(scale6(pal[i].g & 0x3F), f);
            pack_putc(scale6(pal[i].r & 0x3F), f);
            pack_putc(0, f);
        }
    }

    std::vector<unsigned char> row((size_t)stride);

    for(int y = h - 1; y >= 0; --y)
    {
        memset(&row[0], 0, row.size());

        if(eight)
            memcpy(&row[0], bmp->line[y], (size_t)w);
        else
        {
            for(int x = 0; x < w; ++x)
            {
                int r, g, b;
                pixel_rgb(bmp, pal, x, y, &r, &g, &b);
                row[x * 3] = (unsigned char)b;
                row[x * 3 + 1] = (unsigned char)g;
                row[x * 3 + 2] = (unsigned char)r;
            }
        }

        pack_fwrite(&row[0], stride, f);
    }

    return pack_ferror(f) ? -1 : 0;
}

int save_bmp(AL_CONST char *filename, BITMAP *bmp, AL_CONST RGB *pal)
{
    PACKFILE *f = pack_fopen(filename, F_WRITE);

    if(!f)
        return errno ? errno : -1;

    save_bmp_pf(f, bmp, pal);
    return finish(f);
}

int save_pcx_pf(PACKFILE *f, BITMAP *bmp, AL_CONST RGB *pal)
{
    int depth = bitmap_color_depth(bmp);
    int w = bmp->w, h = bmp->h;
    bool eight = depth == 8;
    int planes = eight ? 1 : 3;
    int line = w;
    pal = palette_or_current(pal);

    pack_putc(10, f);
    pack_putc(5, f);
    pack_putc(1, f);
    pack_putc(8, f);
    pack_iputw(0, f);
    pack_iputw(0, f);
    pack_iputw(w - 1, f);
    pack_iputw(h - 1, f);
    pack_iputw(320, f);
    pack_iputw(200, f);

    for(int i = 0; i < 16; ++i)
    {
        pack_putc(scale6(pal[i].r & 0x3F), f);
        pack_putc(scale6(pal[i].g & 0x3F), f);
        pack_putc(scale6(pal[i].b & 0x3F), f);
    }

    pack_putc(0, f);
    pack_putc(planes, f);
    pack_iputw(line, f);
    pack_iputw(1, f);
    pack_iputw(w, f);
    pack_iputw(h, f);

    for(int i = 0; i < 54; ++i)
        pack_putc(0, f);

    /* one scanline holds all planes; the run-length coding runs over it */
    std::vector<unsigned char> scan((size_t)(line * planes));

    for(int y = 0; y < h; ++y)
    {
        for(int x = 0; x < w; ++x)
        {
            if(eight)
                scan[x] = bmp->line[y][x];
            else
            {
                int c[3];
                pixel_rgb(bmp, pal, x, y, &c[0], &c[1], &c[2]);

                for(int p = 0; p < 3; ++p)
                    scan[(size_t)(p * line + x)] = (unsigned char)c[p];
            }
        }

        /* runs of up to 63 */
        int n = line * planes;
        int x = 0;

        while(x < n)
        {
            int run = 1;

            while(x + run < n && run < 63 && scan[x + run] == scan[x])
                ++run;

            if(run > 1 || scan[x] >= 0xC0)
                pack_putc(0xC0 | run, f);

            pack_putc(scan[x], f);
            x += run;
        }
    }

    if(eight)
    {
        pack_putc(12, f);

        for(int i = 0; i < 256; ++i)
        {
            pack_putc(scale6(pal[i].r & 0x3F), f);
            pack_putc(scale6(pal[i].g & 0x3F), f);
            pack_putc(scale6(pal[i].b & 0x3F), f);
        }
    }

    return pack_ferror(f) ? -1 : 0;
}

int save_pcx(AL_CONST char *filename, BITMAP *bmp, AL_CONST RGB *pal)
{
    PACKFILE *f = pack_fopen(filename, F_WRITE);

    if(!f)
        return errno ? errno : -1;

    save_pcx_pf(f, bmp, pal);
    return finish(f);
}

int save_tga_pf(PACKFILE *f, BITMAP *bmp, AL_CONST RGB *pal)
{
    int depth = bitmap_color_depth(bmp);
    int w = bmp->w, h = bmp->h;
    bool eight = depth == 8;
    int outbits = eight ? 8 : depth == 32 ? 32 : depth == 24 ? 24 : 16;
    pal = palette_or_current(pal);

    pack_putc(0, f);                    /* id length */
    pack_putc(eight ? 1 : 0, f);        /* color map type */
    pack_putc(eight ? 1 : 2, f);        /* uncompressed color-mapped / true color */
    pack_iputw(0, f);
    pack_iputw(eight ? 256 : 0, f);
    pack_putc(eight ? 24 : 0, f);
    pack_iputw(0, f);
    pack_iputw(0, f);
    pack_iputw(w, f);
    pack_iputw(h, f);
    pack_putc(outbits, f);
    pack_putc(0, f);                    /* descriptor: bottom-up rows */

    if(eight)
    {
        for(int i = 0; i < 256; ++i)
        {
            pack_putc(scale6(pal[i].b & 0x3F), f);
            pack_putc(scale6(pal[i].g & 0x3F), f);
            pack_putc(scale6(pal[i].r & 0x3F), f);
        }
    }

    for(int y = h - 1; y >= 0; --y)
    {
        for(int x = 0; x < w; ++x)
        {
            if(eight)
            {
                pack_putc(bmp->line[y][x], f);
                continue;
            }

            int r, g, b;
            pixel_rgb(bmp, pal, x, y, &r, &g, &b);

            if(outbits == 16)
            {
                /* 5.5.5 words */
                pack_iputw(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3), f);
                continue;
            }

            pack_putc(b, f);
            pack_putc(g, f);
            pack_putc(r, f);

            if(outbits == 32)
                pack_putc((int)((((uint32_t *)bmp->line[y])[x] >> _rgb_a_shift_32) & 0xFF), f);
        }
    }

    return pack_ferror(f) ? -1 : 0;
}

int save_tga(AL_CONST char *filename, BITMAP *bmp, AL_CONST RGB *pal)
{
    PACKFILE *f = pack_fopen(filename, F_WRITE);

    if(!f)
        return errno ? errno : -1;

    save_tga_pf(f, bmp, pal);
    return finish(f);
}

} /* extern "C" */

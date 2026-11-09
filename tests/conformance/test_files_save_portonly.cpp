/*
 * test_files_save_portonly.cpp - the PNG and JPG screenshot writers that
 * loadpng_init() / jpgalleg_init() register (the reference build gets them
 * from add-on libraries the conformance programs do not link, and their
 * bytes are not meant to match). The PNG is decoded here and compared pixel
 * by pixel; the JPEG's marker structure is checked. Port only.
 */
#include "conf.h"

#include <loadpng.h>
#include <jpgalleg.h>

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

namespace
{

std::vector<unsigned char> slurp(const std::string &fn)
{
    std::vector<unsigned char> v;
    FILE *f = fopen(fn.c_str(), "rb");

    if(!f)
        return v;

    int c;

    while((c = fgetc(f)) != EOF)
        v.push_back((unsigned char)c);

    fclose(f);
    return v;
}

uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint32_t crc32_ref(const unsigned char *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;

    for(size_t i = 0; i < n; ++i)
    {
        c ^= p[i];

        for(int k = 0; k < 8; ++k)
            c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }

    return ~c;
}

int s6(int v) { return (v << 2) | (v >> 4); }
int s5(int v) { return (v << 3) | (v >> 2); }

/* Decodes our PNG (stored deflate blocks only). Returns "" or a problem. */
std::string decode_png(const std::vector<unsigned char> &f, int *w, int *h, int *type, std::vector<unsigned char> *plte,
                       std::vector<unsigned char> *raw)
{
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

    if(f.size() < 8 || memcmp(&f[0], sig, 8) != 0)
        return "signature";

    size_t p = 8;
    std::vector<unsigned char> z;
    bool end = false;

    while(p + 12 <= f.size())
    {
        uint32_t len = be32(&f[p]);

        if(p + 12 + len > f.size())
            return "chunk length";

        std::string type_s((const char *)&f[p + 4], 4);

        if(crc32_ref(&f[p + 4], len + 4) != be32(&f[p + 8 + len]))
            return "crc " + type_s;

        const unsigned char *d = &f[p + 8];

        if(type_s == "IHDR")
        {
            *w = (int)be32(d);
            *h = (int)be32(d + 4);

            if(d[8] != 8 || d[10] || d[11] || d[12])
                return "ihdr fields";

            *type = d[9];
        }
        else if(type_s == "PLTE")
            plte->assign(d, d + len);
        else if(type_s == "IDAT")
            z.insert(z.end(), d, d + len);
        else if(type_s == "IEND")
        {
            end = true;
            p += 12 + len;
            break;
        }

        p += 12 + len;
    }

    if(!end || p != f.size())
        return "iend";

    if(z.size() < 6 || ((z[0] << 8) | z[1]) % 31 != 0 || (z[0] & 0x0F) != 8)
        return "zlib header";

    size_t q = 2;

    for(;;)
    {
        if(q + 5 > z.size())
            return "deflate block";

        int hdr = z[q];

        if((hdr >> 1) != 0)
            return "not a stored block";

        int n = z[q + 1] | (z[q + 2] << 8);
        int nn = z[q + 3] | (z[q + 4] << 8);

        if((n ^ 0xFFFF) != nn)
            return "stored length";

        q += 5;
        raw->insert(raw->end(), z.begin() + (std::ptrdiff_t)q, z.begin() + (std::ptrdiff_t)(q + n));
        q += (size_t)n;

        if(hdr & 1)
            break;
    }

    uint32_t a = 1, b = 0;

    for(size_t i = 0; i < raw->size(); ++i)
    {
        a = (a + (*raw)[i]) % 65521;
        b = (b + a) % 65521;
    }

    if(q + 4 != z.size() || be32(&z[q]) != ((b << 16) | a))
        return "adler32";

    return "";
}

} /* namespace */

CONF_TEST(png_8bit)
{
    ctx.check_int("init", loadpng_init());
    static const int sizes[][2] = { { 1, 1 }, { 3, 5 }, { 256, 224 }, { 300, 250 } };

    for(int s = 0; s < 4; ++s)
    {
        int W = sizes[s][0], H = sizes[s][1];
        PALETTE pal;

        for(int i = 0; i < PAL_SIZE; ++i)
        {
            pal[i].r = (unsigned char)ctx.rng.range(0, 63);
            pal[i].g = (unsigned char)ctx.rng.range(0, 63);
            pal[i].b = (unsigned char)ctx.rng.range(0, 63);
        }

        BITMAP *bmp = create_bitmap_ex(8, W, H);

        for(int y = 0; y < H; ++y)
            for(int x = 0; x < W; ++x)
                bmp->line[y][x] = (unsigned char)ctx.rng.bits(8);

        std::string fn = ctx.tmp(s & 1 ? "shot.PNG" : "shot.png");
        ctx.check_int(conf::nm("ret_%d", s), save_bitmap(fn.c_str(), bmp, pal));
        int w = 0, h = 0, type = -1;
        std::vector<unsigned char> plte, raw;
        std::string err = decode_png(slurp(fn), &w, &h, &type, &plte, &raw);
        ctx.expect(conf::nm("valid_%d", s), err.empty(), "%s", err.c_str());

        if(!err.empty())
            continue;

        bool ok = w == W && h == H && type == 3 && plte.size() == 768 && raw.size() == (size_t)(H * (W + 1));

        for(int i = 0; ok && i < 256; ++i)
            ok = plte[i * 3] == s6(pal[i].r) && plte[i * 3 + 1] == s6(pal[i].g) && plte[i * 3 + 2] == s6(pal[i].b);

        for(int y = 0; ok && y < H; ++y)
        {
            ok = raw[(size_t)(y * (W + 1))] == 0;

            for(int x = 0; ok && x < W; ++x)
                ok = raw[(size_t)(y * (W + 1) + 1 + x)] == bmp->line[y][x];
        }

        ctx.expect(conf::nm("pixels_%d", s), ok, "decoded image differs");
        destroy_bitmap(bmp);
        delete_file(fn.c_str());
    }
}

CONF_TEST(png_truecolor)
{
    loadpng_init();
    static const int depths[] = { 15, 16, 24, 32 };

    for(int d = 0; d < 4; ++d)
    {
        int W = 13, H = 7;
        BITMAP *bmp = create_bitmap_ex(depths[d], W, H);
        int bpp = conf::bytes_per_pixel(depths[d]);

        for(int y = 0; y < H; ++y)
            for(int x = 0; x < W * bpp; ++x)
                bmp->line[y][x] = (unsigned char)ctx.rng.bits(8);

        std::string fn = ctx.tmp("tc.png");
        ctx.check_int(conf::nm("ret_%d", depths[d]), save_bitmap(fn.c_str(), bmp, NULL));
        int w = 0, h = 0, type = -1;
        std::vector<unsigned char> plte, raw;
        std::string err = decode_png(slurp(fn), &w, &h, &type, &plte, &raw);
        ctx.expect(conf::nm("valid_%d", depths[d]), err.empty(), "%s", err.c_str());

        if(err.empty())
        {
            bool ok = w == W && h == H && type == 2 && raw.size() == (size_t)(H * (W * 3 + 1));

            for(int y = 0; ok && y < H; ++y)
                for(int x = 0; ok && x < W; ++x)
                {
                    int r, g, b;
                    unsigned char *row = bmp->line[y];

                    /* the default pixel layout without a graphics driver: red lowest */
                    if(depths[d] == 15)
                    {
                        int c = ((unsigned short *)row)[x];
                        r = s5(c & 31); g = s5((c >> 5) & 31); b = s5((c >> 10) & 31);
                    }
                    else if(depths[d] == 16)
                    {
                        int c = ((unsigned short *)row)[x];
                        r = s5(c & 31); g = s6((c >> 5) & 63); b = s5((c >> 11) & 31);
                    }
                    else
                    {
                        r = row[x * bpp]; g = row[x * bpp + 1]; b = row[x * bpp + 2];
                    }

                    const unsigned char *px = &raw[(size_t)(y * (W * 3 + 1) + 1 + x * 3)];
                    ok = px[0] == r && px[1] == g && px[2] == b;
                }

            ctx.expect(conf::nm("pixels_%d", depths[d]), ok, "decoded image differs");
        }

        destroy_bitmap(bmp);
        delete_file(fn.c_str());
    }
}

CONF_TEST(jpg_structure)
{
    ctx.check_int("init", jpgalleg_init());
    static const int depths[] = { 8, 16, 32 };

    for(int d = 0; d < 3; ++d)
    {
        int W = 37 + d * 50, H = 23 + d * 30;
        BITMAP *bmp = create_bitmap_ex(depths[d], W, H);
        int bpp = conf::bytes_per_pixel(depths[d]);

        for(int y = 0; y < H; ++y)
            for(int x = 0; x < W * bpp; ++x)
                bmp->line[y][x] = (unsigned char)((x * 3 + y * 5) ^ ctx.rng.bits(3));

        PALETTE pal;

        for(int i = 0; i < PAL_SIZE; ++i)
        {
            pal[i].r = (unsigned char)(i >> 2);
            pal[i].g = (unsigned char)((255 - i) >> 2);
            pal[i].b = (unsigned char)((i * 3) & 63);
        }

        std::string fn = ctx.tmp(d == 1 ? "shot.JPG" : "shot.jpg");
        ctx.check_int(conf::nm("ret_%d", d), save_bitmap(fn.c_str(), bmp, pal));
        std::vector<unsigned char> f = slurp(fn);
        std::string err;

        if(f.size() < 4 || f[0] != 0xFF || f[1] != 0xD8 || f[f.size() - 2] != 0xFF || f[f.size() - 1] != 0xD9)
            err = "SOI/EOI";

        size_t p = 2;
        bool sof = false, sos = false;
        int dqt = 0, dht = 0;

        while(err.empty() && p + 4 <= f.size())
        {
            if(f[p] != 0xFF)
            {
                err = "marker expected";
                break;
            }

            int m = f[p + 1];
            int len = (f[p + 2] << 8) | f[p + 3];

            if(m == 0xDB)
                ++dqt;
            else if(m == 0xC4)
                ++dht;
            else if(m == 0xC0)
            {
                sof = true;

                if(((f[p + 5] << 8) | f[p + 6]) != H || ((f[p + 7] << 8) | f[p + 8]) != W || f[p + 9] != 3)
                    err = "SOF0 fields";
            }
            else if(m == 0xDA)
            {
                sos = true;
                p += 2 + (size_t)len;

                /* entropy-coded data: 0xFF only as 0xFF 0x00, up to EOI */
                for(; p + 1 < f.size(); ++p)
                {
                    if(f[p] == 0xFF)
                    {
                        if(f[p + 1] == 0x00)
                            ++p;
                        else if(f[p + 1] == 0xD9 && p + 2 == f.size())
                            break;
                        else
                        {
                            err = "bad byte in scan";
                            break;
                        }
                    }
                }

                break;
            }

            p += 2 + (size_t)len;
        }

        if(err.empty() && !(sof && sos && dqt == 2 && dht == 4))
            err = "missing segments";

        ctx.expect(conf::nm("valid_%d", d), err.empty(), "%s", err.c_str());
        destroy_bitmap(bmp);
        delete_file(fn.c_str());
    }
}

// zcport files module: PNG and JPEG writers for screenshots
// (loadpng_init / jpgalleg_init register them as "png" / "jpg" with
// save_bitmap). No libraries: the PNG uses stored (uncompressed) deflate
// blocks; the JPEG is a baseline sequential JFIF (4:4:4, the example
// quantization and Huffman tables of the JPEG specification, Annex K).
// Neither is meant to match the add-on libraries' bytes; both are valid
// files. Loading is not provided (the player only saves screenshots).
//
// Written from scratch for zcport.

#include "files_internal.h"

#include <loadpng.h>
#include <jpgalleg.h>

#include <errno.h>
#include <math.h>
#include <string.h>

#include <vector>

namespace
{

int scale5(int v) { return (v << 3) | (v >> 2); }
int scale6(int v) { return (v << 2) | (v >> 4); }

void rgb_at(BITMAP *b, const RGB *pal, int x, int y, int *r, int *g, int *bl)
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

int write_out(const char *filename, const std::vector<unsigned char> &data)
{
    PACKFILE *f = pack_fopen(filename, F_WRITE);

    if(!f)
        return errno ? errno : -1;

    if(!data.empty())
        pack_fwrite(&data[0], (long)data.size(), f);

    bool err = pack_ferror(f) != 0;
    int r = pack_fclose(f);
    return err ? -1 : r;
}

/* ---- PNG -------------------------------------------------------------------- */

uint32_t crc_table[256];
bool crc_ready = false;

uint32_t crc32_of(const unsigned char *p, size_t n, uint32_t crc)
{
    if(!crc_ready)
    {
        for(uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;

            for(int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;

            crc_table[i] = c;
        }

        crc_ready = true;
    }

    crc = ~crc;

    for(size_t i = 0; i < n; ++i)
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);

    return ~crc;
}

void put32(std::vector<unsigned char> &o, uint32_t v)
{
    o.push_back((unsigned char)(v >> 24));
    o.push_back((unsigned char)(v >> 16));
    o.push_back((unsigned char)(v >> 8));
    o.push_back((unsigned char)v);
}

void png_chunk(std::vector<unsigned char> &o, const char *type, const std::vector<unsigned char> &data)
{
    put32(o, (uint32_t)data.size());
    size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), data.begin(), data.end());
    put32(o, crc32_of(&o[start], o.size() - start, 0));
}

int save_png(const char *filename, BITMAP *bmp, const RGB *pal)
{
    if(!pal)
        pal = _current_palette;

    int w = bmp->w, h = bmp->h;
    bool eight = bitmap_color_depth(bmp) == 8;
    std::vector<unsigned char> out;
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    out.insert(out.end(), sig, sig + 8);

    std::vector<unsigned char> ihdr;
    put32(ihdr, (uint32_t)w);
    put32(ihdr, (uint32_t)h);
    ihdr.push_back(8);                 /* bit depth */
    ihdr.push_back(eight ? 3 : 2);     /* palette / RGB */
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    png_chunk(out, "IHDR", ihdr);

    if(eight)
    {
        std::vector<unsigned char> plte;

        for(int i = 0; i < 256; ++i)
        {
            plte.push_back((unsigned char)scale6(pal[i].r & 0x3F));
            plte.push_back((unsigned char)scale6(pal[i].g & 0x3F));
            plte.push_back((unsigned char)scale6(pal[i].b & 0x3F));
        }

        png_chunk(out, "PLTE", plte);
    }

    /* filtered rows (filter type 0) */
    std::vector<unsigned char> raw;
    raw.reserve((size_t)h * (size_t)(1 + w * (eight ? 1 : 3)));

    for(int y = 0; y < h; ++y)
    {
        raw.push_back(0);

        for(int x = 0; x < w; ++x)
        {
            if(eight)
                raw.push_back(bmp->line[y][x]);
            else
            {
                int r, g, b;
                rgb_at(bmp, pal, x, y, &r, &g, &b);
                raw.push_back((unsigned char)r);
                raw.push_back((unsigned char)g);
                raw.push_back((unsigned char)b);
            }
        }
    }

    /* zlib stream of stored deflate blocks */
    std::vector<unsigned char> z;
    z.push_back(0x78);
    z.push_back(0x01);
    size_t at = 0;

    do
    {
        size_t n = raw.size() - at;

        if(n > 65535)
            n = 65535;

        bool last = at + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back((unsigned char)(n & 0xFF));
        z.push_back((unsigned char)(n >> 8));
        z.push_back((unsigned char)(~n & 0xFF));
        z.push_back((unsigned char)((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + (std::ptrdiff_t)at, raw.begin() + (std::ptrdiff_t)(at + n));
        at += n;
    }
    while(at < raw.size());

    uint32_t a = 1, b = 0;

    for(size_t i = 0; i < raw.size(); ++i)
    {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }

    put32(z, (b << 16) | a);
    png_chunk(out, "IDAT", z);
    png_chunk(out, "IEND", std::vector<unsigned char>());
    return write_out(filename, out);
}

/* ---- JPEG ------------------------------------------------------------------- */

const unsigned char zigzag[64] = { 0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                   12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                   35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                   58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63 };

const unsigned char base_luma[64] = { 16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                                      14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                                      18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                                      49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99 };

const unsigned char base_chroma[64] = { 17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                                        24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 };

/* Annex K.3 tables: code counts per length 1..16, then the values */
const unsigned char dc_luma_bits[16] = { 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
const unsigned char dc_luma_vals[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
const unsigned char dc_chroma_bits[16] = { 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
const unsigned char dc_chroma_vals[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
const unsigned char ac_luma_bits[16] = { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
const unsigned char ac_luma_vals[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14,
    0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09,
    0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
    0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65,
    0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88,
    0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
    0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
    0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
    0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };
const unsigned char ac_chroma_bits[16] = { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };
const unsigned char ac_chroma_vals[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32,
    0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16,
    0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64,
    0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86,
    0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8,
    0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
    0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };

struct Huff
{
    unsigned short code[256];
    unsigned char len[256];

    void build(const unsigned char *bits, const unsigned char *vals)
    {
        memset(len, 0, sizeof(len));
        int k = 0;
        unsigned c = 0;

        for(int l = 1; l <= 16; ++l)
        {
            for(int i = 0; i < bits[l - 1]; ++i, ++k)
            {
                code[vals[k]] = (unsigned short)c;
                len[vals[k]] = (unsigned char)l;
                ++c;
            }

            c <<= 1;
        }
    }
};

struct BitWriter
{
    std::vector<unsigned char> &out;
    uint32_t acc;
    int n;

    explicit BitWriter(std::vector<unsigned char> &o) : out(o), acc(0), n(0) {}

    void put(unsigned v, int bits)
    {
        for(int i = bits - 1; i >= 0; --i)
        {
            acc = (acc << 1) | ((v >> i) & 1);

            if(++n == 8)
            {
                out.push_back((unsigned char)acc);

                if(acc == 0xFF)
                    out.push_back(0);   /* byte stuffing */

                acc = 0;
                n = 0;
            }
        }
    }

    void flush()
    {
        while(n)
            put(1, 1);   /* pad with ones */
    }
};

void fdct(const float in[64], float out[64])
{
    static float cosv[8][8];
    static bool ready = false;

    if(!ready)
    {
        for(int x = 0; x < 8; ++x)
            for(int u = 0; u < 8; ++u)
                cosv[x][u] = (float)cos((2 * x + 1) * u * 3.14159265358979323846 / 16.0);

        ready = true;
    }

    for(int v = 0; v < 8; ++v)
        for(int u = 0; u < 8; ++u)
        {
            double s = 0;

            for(int y = 0; y < 8; ++y)
                for(int x = 0; x < 8; ++x)
                    s += in[y * 8 + x] * cosv[x][u] * cosv[y][v];

            double cu = u ? 1.0 : 1.0 / sqrt(2.0), cv = v ? 1.0 : 1.0 / sqrt(2.0);
            out[v * 8 + u] = (float)(0.25 * cu * cv * s);
        }
}

int magnitude_bits(int v)
{
    if(v < 0)
        v = -v;

    int n = 0;

    while(v)
    {
        ++n;
        v >>= 1;
    }

    return n;
}

void encode_block(BitWriter &bw, const float px[64], const unsigned char q[64], int *dc_prev, const Huff &dc,
                  const Huff &ac)
{
    float coef[64];
    fdct(px, coef);
    int z[64];

    for(int i = 0; i < 64; ++i)
    {
        float v = coef[zigzag[i]] / (float)q[i];
        z[i] = (int)(v < 0 ? v - 0.5f : v + 0.5f);
    }

    int diff = z[0] - *dc_prev;
    *dc_prev = z[0];
    int nb = magnitude_bits(diff);
    bw.put(dc.code[nb], dc.len[nb]);

    if(nb)
        bw.put((unsigned)(diff < 0 ? diff - 1 : diff) & ((1u << nb) - 1), nb);

    int run = 0;

    for(int i = 1; i < 64; ++i)
    {
        if(z[i] == 0)
        {
            ++run;
            continue;
        }

        while(run > 15)
        {
            bw.put(ac.code[0xF0], ac.len[0xF0]);
            run -= 16;
        }

        nb = magnitude_bits(z[i]);
        int sym = (run << 4) | nb;
        bw.put(ac.code[sym], ac.len[sym]);
        bw.put((unsigned)(z[i] < 0 ? z[i] - 1 : z[i]) & ((1u << nb) - 1), nb);
        run = 0;
    }

    if(run)
        bw.put(ac.code[0x00], ac.len[0x00]);
}

void put16(std::vector<unsigned char> &o, int v)
{
    o.push_back((unsigned char)(v >> 8));
    o.push_back((unsigned char)v);
}

void put_dht(std::vector<unsigned char> &o, int cls_id, const unsigned char *bits, const unsigned char *vals)
{
    int n = 0;

    for(int i = 0; i < 16; ++i)
        n += bits[i];

    o.push_back(0xFF);
    o.push_back(0xC4);
    put16(o, 2 + 1 + 16 + n);
    o.push_back((unsigned char)cls_id);
    o.insert(o.end(), bits, bits + 16);
    o.insert(o.end(), vals, vals + n);
}

int save_jpg(const char *filename, BITMAP *bmp, const RGB *pal)
{
    if(!pal)
        pal = _current_palette;

    const int quality = 90;
    int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    unsigned char ql[64], qc[64];

    for(int i = 0; i < 64; ++i)
    {
        int l = (base_luma[zigzag[i]] * scale + 50) / 100;
        int c = (base_chroma[zigzag[i]] * scale + 50) / 100;
        ql[i] = (unsigned char)(l < 1 ? 1 : l > 255 ? 255 : l);
        qc[i] = (unsigned char)(c < 1 ? 1 : c > 255 ? 255 : c);
    }

    std::vector<unsigned char> o;
    int w = bmp->w, h = bmp->h;

    o.push_back(0xFF);
    o.push_back(0xD8);                      /* SOI */
    static const unsigned char app0[] = { 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F', 0, 1, 1, 0, 0, 1, 0, 1, 0, 0 };
    o.insert(o.end(), app0, app0 + sizeof(app0));

    for(int t = 0; t < 2; ++t)
    {
        o.push_back(0xFF);
        o.push_back(0xDB);                  /* DQT, tables in zigzag order */
        put16(o, 67);
        o.push_back((unsigned char)t);
        o.insert(o.end(), t ? qc : ql, (t ? qc : ql) + 64);
    }

    o.push_back(0xFF);
    o.push_back(0xC0);                      /* SOF0 */
    put16(o, 17);
    o.push_back(8);
    put16(o, h);
    put16(o, w);
    o.push_back(3);

    for(int c = 0; c < 3; ++c)
    {
        o.push_back((unsigned char)(c + 1));
        o.push_back(0x11);
        o.push_back(c ? 1 : 0);
    }

    put_dht(o, 0x00, dc_luma_bits, dc_luma_vals);
    put_dht(o, 0x10, ac_luma_bits, ac_luma_vals);
    put_dht(o, 0x01, dc_chroma_bits, dc_chroma_vals);
    put_dht(o, 0x11, ac_chroma_bits, ac_chroma_vals);

    o.push_back(0xFF);
    o.push_back(0xDA);                      /* SOS */
    put16(o, 12);
    o.push_back(3);

    for(int c = 0; c < 3; ++c)
    {
        o.push_back((unsigned char)(c + 1));
        o.push_back(c ? 0x11 : 0x00);
    }

    o.push_back(0);
    o.push_back(63);
    o.push_back(0);

    Huff dcl, acl, dcc, acc;
    dcl.build(dc_luma_bits, dc_luma_vals);
    acl.build(ac_luma_bits, ac_luma_vals);
    dcc.build(dc_chroma_bits, dc_chroma_vals);
    acc.build(ac_chroma_bits, ac_chroma_vals);
    BitWriter bw(o);
    int prev[3] = { 0, 0, 0 };

    for(int by = 0; by < h; by += 8)
        for(int bx = 0; bx < w; bx += 8)
        {
            float ycc[3][64];

            for(int y = 0; y < 8; ++y)
                for(int x = 0; x < 8; ++x)
                {
                    int sx = bx + x < w ? bx + x : w - 1;
                    int sy = by + y < h ? by + y : h - 1;
                    int r, g, b;
                    rgb_at(bmp, pal, sx, sy, &r, &g, &b);
                    ycc[0][y * 8 + x] = 0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
                    ycc[1][y * 8 + x] = -0.168736f * r - 0.331264f * g + 0.5f * b;
                    ycc[2][y * 8 + x] = 0.5f * r - 0.418688f * g - 0.081312f * b;
                }

            encode_block(bw, ycc[0], ql, &prev[0], dcl, acl);
            encode_block(bw, ycc[1], qc, &prev[1], dcc, acc);
            encode_block(bw, ycc[2], qc, &prev[2], dcc, acc);
        }

    bw.flush();
    o.push_back(0xFF);
    o.push_back(0xD9);                      /* EOI */
    return write_out(filename, o);
}

} /* namespace */

extern "C" {

int loadpng_init(void)
{
    register_bitmap_file_type("png", NULL, save_png);
    return 0;
}

int jpgalleg_init(void)
{
    register_bitmap_file_type("jpg", NULL, save_jpg);
    register_bitmap_file_type("jpeg", NULL, save_jpg);
    return 0;
}

} /* extern "C" */

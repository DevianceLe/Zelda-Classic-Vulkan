// zcport files module: readers for the file formats ZQuest imports (PCX and
// TGA images, MIDI files and WAV samples), and the conversion of loaded
// truecolor images for an 8-bit program. The player never loads these.
//
// The formats follow their published specifications. MIDI and samples are
// built like the datafile readers in files_datafile.cpp, so destroy_midi() and
// destroy_sample() free them the same way.
//
// Written from scratch for zcport.

#include "files_internal.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

namespace
{

typedef std::vector<unsigned char> Bytes;

bool read_file(const char *filename, Bytes *out)
{
    PACKFILE *f = pack_fopen(filename, F_READ);

    if(!f)
        return false;

    unsigned char chunk[65536];
    long n;

    while((n = pack_fread(chunk, (long)sizeof(chunk), f)) > 0)
        out->insert(out->end(), chunk, chunk + n);

    pack_fclose(f);
    return true;
}

unsigned le16(const Bytes &d, size_t at) { return d[at] | (d[at + 1] << 8); }
unsigned be16(const Bytes &d, size_t at) { return (d[at] << 8) | d[at + 1]; }
unsigned long le32(const Bytes &d, size_t at)
{
    return (unsigned long)d[at] | ((unsigned long)d[at + 1] << 8) |
           ((unsigned long)d[at + 2] << 16) | ((unsigned long)d[at + 3] << 24);
}
unsigned long be32(const Bytes &d, size_t at)
{
    return ((unsigned long)d[at] << 24) | ((unsigned long)d[at + 1] << 16) |
           ((unsigned long)d[at + 2] << 8) | (unsigned long)d[at + 3];
}

bool tag_at(const Bytes &d, size_t at, const char *tag)
{
    return at + 4 <= d.size() && memcmp(&d[at], tag, 4) == 0;
}

void put32(BITMAP *b, int x, int y, int r, int g, int bl)
{
    ((uint32_t *)b->line[y])[x] = (uint32_t)makecol32(r, g, bl);
}

// ---- truecolor to 8 bits (median cut over a 5-bit-per-channel histogram) ----

struct Box
{
    std::vector<int> colors;   // 15-bit colors
    long count;
};

int channel(int color, int axis) { return (color >> (10 - axis * 5)) & 31; }

struct AxisLess
{
    int axis;
    explicit AxisLess(int a) : axis(a) {}
    bool operator()(int a, int b) const { return channel(a, axis) < channel(b, axis); }
};

void box_extent(const Box &b, int *axis, int *range)
{
    *range = -1;
    *axis = 0;

    for(int a = 0; a < 3; ++a)
    {
        int lo = 31, hi = 0;

        for(size_t i = 0; i < b.colors.size(); ++i)
        {
            int v = channel(b.colors[i], a);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }

        if(hi - lo > *range)
        {
            *range = hi - lo;
            *axis = a;
        }
    }
}

BITMAP *quantize(BITMAP *src, RGB *pal)
{
    std::vector<long> hist(32768, 0);

    for(int y = 0; y < src->h; ++y)
    {
        const uint32_t *row = (const uint32_t *)src->line[y];

        for(int x = 0; x < src->w; ++x)
        {
            int c = (int)row[x];
            ++hist[((getr32(c) >> 3) << 10) | ((getg32(c) >> 3) << 5) | (getb32(c) >> 3)];
        }
    }

    std::vector<Box> boxes(1);
    boxes[0].count = 0;

    for(int c = 0; c < 32768; ++c)
    {
        if(hist[c])
        {
            boxes[0].colors.push_back(c);
            boxes[0].count += hist[c];
        }
    }

    // Entry 0 stays black: it is the transparent color of 8-bit bitmaps.
    while(boxes.size() < 255)
    {
        int best = -1, best_axis = 0;
        double best_score = 0;

        for(size_t i = 0; i < boxes.size(); ++i)
        {
            if(boxes[i].colors.size() < 2)
                continue;

            int axis, range;
            box_extent(boxes[i], &axis, &range);
            double score = (double)range * (double)boxes[i].count;

            if(range > 0 && score > best_score)
            {
                best = (int)i;
                best_axis = axis;
                best_score = score;
            }
        }

        if(best < 0)
            break;

        Box &b = boxes[best];
        std::sort(b.colors.begin(), b.colors.end(), AxisLess(best_axis));
        size_t cut = 1;
        long sum = hist[b.colors[0]];

        while(cut + 1 < b.colors.size() && sum * 2 < b.count)
            sum += hist[b.colors[cut++]];

        Box other;
        other.colors.assign(b.colors.begin() + (long)cut, b.colors.end());
        other.count = b.count - sum;
        b.colors.resize(cut);
        b.count = sum;
        boxes.push_back(other);
    }

    std::vector<unsigned char> map(32768, 0);
    memset(pal, 0, sizeof(RGB) * PAL_SIZE);

    for(size_t i = 0; i < boxes.size(); ++i)
    {
        double r = 0, g = 0, bl = 0;

        for(size_t k = 0; k < boxes[i].colors.size(); ++k)
        {
            int c = boxes[i].colors[k];
            double n = (double)hist[c];
            r += channel(c, 0) * n;
            g += channel(c, 1) * n;
            bl += channel(c, 2) * n;
            map[c] = (unsigned char)(i + 1);
        }

        double n = boxes[i].count ? (double)boxes[i].count : 1.0;
        pal[i + 1].r = (unsigned char)(r / n * 63.0 / 31.0 + 0.5);
        pal[i + 1].g = (unsigned char)(g / n * 63.0 / 31.0 + 0.5);
        pal[i + 1].b = (unsigned char)(bl / n * 63.0 / 31.0 + 0.5);
    }

    BITMAP *out = create_bitmap_ex(8, src->w, src->h);

    if(out)
    {
        for(int y = 0; y < src->h; ++y)
        {
            const uint32_t *row = (const uint32_t *)src->line[y];

            for(int x = 0; x < src->w; ++x)
            {
                int c = (int)row[x];
                out->line[y][x] = map[((getr32(c) >> 3) << 10) | ((getg32(c) >> 3) << 5) | (getb32(c) >> 3)];
            }
        }
    }

    destroy_bitmap(src);
    return out;
}

} // namespace

namespace zcfiles
{

// Loaders return 8-bit images as they are and everything else as 32-bit; the
// program's color conversion setting decides what it finally receives.
BITMAP *fixup_loaded_bitmap(BITMAP *bmp, RGB *pal)
{
    if(!bmp || bitmap_color_depth(bmp) == 8 || _color_load_depth(32, FALSE) != 8)
        return bmp;

    PALETTE scratch;
    return quantize(bmp, pal ? pal : scratch);
}

} // namespace zcfiles

extern "C" {

BITMAP *load_pcx(AL_CONST char *filename, RGB *pal)
{
    Bytes d;

    if(!read_file(filename, &d) || d.size() < 128 || d[0] != 10 || d[2] != 1)
        return NULL;

    int bpp = d[3];
    int w = (int)le16(d, 8) - (int)le16(d, 4) + 1;
    int h = (int)le16(d, 10) - (int)le16(d, 6) + 1;
    int planes = d[65];
    int stride = (int)le16(d, 66);

    if(w <= 0 || h <= 0 || bpp != 8 || (planes != 1 && planes != 3) || stride < w)
        return NULL;

    BITMAP *b = create_bitmap_ex(planes == 1 ? 8 : 32, w, h);

    if(!b)
        return NULL;

    std::vector<unsigned char> row((size_t)stride * planes);
    size_t at = 128;

    for(int y = 0; y < h; ++y)
    {
        size_t filled = 0;

        while(filled < row.size() && at < d.size())
        {
            int c = d[at++], run = 1;

            if((c & 0xC0) == 0xC0)
            {
                run = c & 0x3F;
                c = at < d.size() ? d[at++] : 0;
            }

            while(run-- > 0 && filled < row.size())
                row[filled++] = (unsigned char)c;
        }

        for(int x = 0; x < w; ++x)
        {
            if(planes == 1)
                b->line[y][x] = row[x];
            else
                put32(b, x, y, row[x], row[(size_t)x + stride], row[(size_t)x + 2 * (size_t)stride]);
        }
    }

    if(planes == 1 && pal)
    {
        memset(pal, 0, sizeof(RGB) * PAL_SIZE);

        if(d.size() >= 769 && d[d.size() - 769] == 12)
        {
            const unsigned char *p = &d[d.size() - 768];

            for(int i = 0; i < PAL_SIZE; ++i)
            {
                pal[i].r = p[i * 3] >> 2;
                pal[i].g = p[i * 3 + 1] >> 2;
                pal[i].b = p[i * 3 + 2] >> 2;
            }
        }
    }

    return b;
}

BITMAP *load_tga(AL_CONST char *filename, RGB *pal)
{
    Bytes d;

    if(!read_file(filename, &d) || d.size() < 18)
        return NULL;

    int kind = d[2];
    bool rle = kind >= 9;
    int base = rle ? kind - 8 : kind;
    int w = (int)le16(d, 12), h = (int)le16(d, 14), bpp = d[16];
    bool top_down = (d[17] & 0x20) != 0;

    if(w <= 0 || h <= 0 || (base != 1 && base != 2 && base != 3))
        return NULL;

    if((base == 1 || base == 3) && bpp != 8)
        return NULL;

    if(base == 2 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32)
        return NULL;

    size_t at = 18 + (size_t)d[0];
    unsigned map_first = le16(d, 3), map_len = le16(d, 5);
    int map_bits = d[7];
    size_t map_bytes = (size_t)(map_bits + 1) / 8;

    if(pal)
        memset(pal, 0, sizeof(RGB) * PAL_SIZE);

    if(d[1])
    {
        if(at + map_bytes * map_len > d.size())
            return NULL;

        for(unsigned i = 0; i < map_len && pal && map_first + i < (unsigned)PAL_SIZE; ++i)
        {
            const unsigned char *p = &d[at + map_bytes * i];
            int r, g, bl;

            if(map_bytes >= 3)
            {
                bl = p[0];
                g = p[1];
                r = p[2];
            }
            else
            {
                int v = p[0] | (p[1] << 8);
                r = ((v >> 10) & 31) << 3;
                g = ((v >> 5) & 31) << 3;
                bl = (v & 31) << 3;
            }

            pal[map_first + i].r = (unsigned char)(r >> 2);
            pal[map_first + i].g = (unsigned char)(g >> 2);
            pal[map_first + i].b = (unsigned char)(bl >> 2);
        }

        at += map_bytes * map_len;
    }

    if(base == 3 && pal)
    {
        for(int i = 0; i < PAL_SIZE; ++i)
            pal[i].r = pal[i].g = pal[i].b = (unsigned char)(i >> 2);
    }

    BITMAP *b = create_bitmap_ex(base == 2 ? 32 : 8, w, h);

    if(!b)
        return NULL;

    size_t pixel_bytes = (size_t)(bpp + 1) / 8;
    unsigned char run_pixel[4] = { 0, 0, 0, 0 };
    int run = 0, raw = 0;

    for(int row = 0; row < h; ++row)
    {
        int y = top_down ? row : h - 1 - row;

        for(int x = 0; x < w; ++x)
        {
            unsigned char px[4] = { 0, 0, 0, 0 };

            if(rle && !run && !raw)
            {
                if(at >= d.size())
                    return b;

                int head = d[at++];

                if(head & 0x80)
                {
                    run = (head & 0x7F) + 1;

                    if(at + pixel_bytes > d.size())
                        return b;

                    memcpy(run_pixel, &d[at], pixel_bytes);
                    at += pixel_bytes;
                }
                else
                    raw = head + 1;
            }

            if(rle && run)
            {
                memcpy(px, run_pixel, pixel_bytes);
                --run;
            }
            else
            {
                if(at + pixel_bytes > d.size())
                    return b;

                memcpy(px, &d[at], pixel_bytes);
                at += pixel_bytes;

                if(rle)
                    --raw;
            }

            if(base != 2)
                b->line[y][x] = px[0];
            else if(pixel_bytes == 2)
            {
                int v = px[0] | (px[1] << 8);
                put32(b, x, y, ((v >> 10) & 31) << 3, ((v >> 5) & 31) << 3, (v & 31) << 3);
            }
            else
                put32(b, x, y, px[2], px[1], px[0]);
        }
    }

    return b;
}

MIDI *load_midi(AL_CONST char *filename)
{
    Bytes d;

    if(!read_file(filename, &d))
        return NULL;

    size_t at = 0;

    // A RIFF MIDI file wraps a standard one in its "data" chunk.
    if(tag_at(d, 0, "RIFF") && tag_at(d, 8, "RMID"))
    {
        at = 12;

        while(at + 8 <= d.size() && !tag_at(d, at, "data"))
            at += 8 + (size_t)((le32(d, at + 4) + 1) & ~1ul);

        at += 8;
    }

    if(!tag_at(d, at, "MThd") || at + 14 > d.size())
        return NULL;

    size_t head = be32(d, at + 4);
    unsigned format = be16(d, at + 8), tracks = be16(d, at + 10), division = be16(d, at + 12);

    if(format > 1 || tracks > MIDI_TRACKS)
        return NULL;

    MIDI *m = (MIDI *)calloc(1, sizeof(MIDI));

    if(!m)
        return NULL;

    m->divisions = (int)division;
    at += 8 + head;

    for(unsigned i = 0; i < tracks; ++i)
    {
        if(!tag_at(d, at, "MTrk") || at + 8 > d.size())
        {
            destroy_midi(m);
            return NULL;
        }

        size_t len = be32(d, at + 4);
        at += 8;

        if(at + len > d.size())
        {
            destroy_midi(m);
            return NULL;
        }

        m->track[i].len = (int)len;
        m->track[i].data = (unsigned char *)malloc(len ? len : 1);

        if(!m->track[i].data)
        {
            destroy_midi(m);
            return NULL;
        }

        if(len)
            memcpy(m->track[i].data, &d[at], len);

        at += len;
    }

    return m;
}

SAMPLE *load_wav(AL_CONST char *filename)
{
    Bytes d;

    if(!read_file(filename, &d) || !tag_at(d, 0, "RIFF") || !tag_at(d, 8, "WAVE"))
        return NULL;

    int channels = 0, freq = 0, bits = 0;
    size_t at = 12;

    while(at + 8 <= d.size())
    {
        size_t len = le32(d, at + 4);
        size_t body = at + 8;

        if(tag_at(d, at, "fmt ") && body + 16 <= d.size())
        {
            if(le16(d, body) != 1)     // PCM only
                return NULL;

            channels = (int)le16(d, body + 2);
            freq = (int)le32(d, body + 4);
            bits = (int)le16(d, body + 14);
        }
        else if(tag_at(d, at, "data"))
        {
            if((channels != 1 && channels != 2) || (bits != 8 && bits != 16) || freq <= 0)
                return NULL;

            len = std::min(len, d.size() - body);
            size_t samples = len / (bits / 8);
            size_t frames = samples / (size_t)channels;

            SAMPLE *s = (SAMPLE *)calloc(1, sizeof(SAMPLE));

            if(!s)
                return NULL;

            s->bits = bits;
            s->stereo = channels == 2 ? TRUE : FALSE;
            s->freq = freq;
            s->priority = 128;
            s->len = (unsigned long)frames;
            s->loop_start = 0;
            s->loop_end = (unsigned long)frames;
            s->param = 0;
            s->data = malloc(len ? len : 1);

            if(!s->data)
            {
                free(s);
                return NULL;
            }

            if(bits == 8)
            {
                if(len)
                    memcpy(s->data, &d[body], len);
            }
            else
            {
                // Samples are stored unsigned in memory.
                unsigned short *out = (unsigned short *)s->data;

                for(size_t i = 0; i < samples; ++i)
                    out[i] = (unsigned short)(le16(d, body + i * 2) ^ 0x8000);
            }

            return s;
        }

        at = body + ((len + 1) & ~(size_t)1);
    }

    return NULL;
}

} /* extern "C" */

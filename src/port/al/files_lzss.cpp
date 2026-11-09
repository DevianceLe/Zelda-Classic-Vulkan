// zcport files module: LZSS coding of packed PACKFILEs.
//
// Stream format (docs/packfile.txt): a 4096-byte ring buffer that starts
// zero-filled with the write index at 4078; a flags byte announces the next
// eight tokens (bit set = one literal byte, bit clear = a two-byte
// back-reference: 12-bit ring position, 4-bit length - 3).
//
// The encoder is the classic Lempel-Ziv-Storer-Szymanski scheme with a
// binary search tree over the 4096 most recent positions and a lookahead of
// 18 bytes (H. Okumura's public-domain formulation of the algorithm); the
// match choice (first longest match found on the tree path, full-length
// matches replace the older node) is what makes the output byte-identical
// to the reference build's files. The public lzss_* API sits on top.
//
// Written from scratch for zcport.

#include "files_internal.h"

#include <string.h>

namespace zcfiles
{

namespace
{
const int RING = 4096;          /* N */
const int LOOK = 18;            /* F */
const int MINREF = 2;           /* THRESHOLD: matches longer than this are coded */
const int NONE = RING;          /* "no node" */
const int START = RING - LOOK;  /* 4078 */
}

/* ------------------------------------------------------------------------ */
/* Decoder                                                                  */
/* ------------------------------------------------------------------------ */

LzssDecoder::LzssDecoder()
{
    reset();
}

void LzssDecoder::reset()
{
    memset(ring_, 0, sizeof(ring_));
    r_ = START;
    flags_ = 0;
    bits_left_ = 0;
    pair_pos_ = 0;
    pair_left_ = 0;
    last_literal_ = false;
    fresh_ = false;
    key_pos_ = 0;
}

void LzssDecoder::set_flag_key(const std::string &key)
{
    key_ = key;
    key_pos_ = 0;
}

int LzssDecoder::next_flag_bit(ByteSource src, void *ctx)
{
    if(bits_left_ == 0)
    {
        int c = src(ctx);

        if(c < 0)
            return -1;

        if(!key_.empty())
        {
            c ^= (unsigned char)key_[key_pos_];

            if(++key_pos_ >= key_.size())
                key_pos_ = 0;
        }

        flags_ = (unsigned)c & 0xFF;
        bits_left_ = 8;
    }

    int bit = (int)(flags_ & 1);
    flags_ >>= 1;
    --bits_left_;
    return bit;
}

int LzssDecoder::read(ByteSource src, void *ctx, unsigned char *out, int max, bool *hit_end, bool stop_after_cut_pair)
{
    int n = 0;
    fresh_ = false;

    if(pair_left_ > 0)
    {
        while(pair_left_ > 0 && n < max)
        {
            unsigned char c = ring_[pair_pos_];
            pair_pos_ = (pair_pos_ + 1) & (RING - 1);
            ring_[r_] = c;
            r_ = (r_ + 1) & (RING - 1);
            out[n++] = c;
            --pair_left_;
        }

        if(stop_after_cut_pair || pair_left_ > 0)
            return n;
    }

    while(n < max)
    {
        int bit = next_flag_bit(src, ctx);

        if(bit < 0)
        {
            *hit_end = true;
            break;
        }

        if(bit)
        {
            int c = src(ctx);

            if(c < 0)
            {
                *hit_end = true;
                break;
            }

            ring_[r_] = (unsigned char)c;
            r_ = (r_ + 1) & (RING - 1);
            out[n++] = (unsigned char)c;
            last_literal_ = true;
            fresh_ = true;
        }
        else
        {
            int lo = src(ctx);

            if(lo < 0)
            {
                *hit_end = true;
                break;
            }

            int hi = src(ctx);

            if(hi < 0)
            {
                *hit_end = true;
                break;
            }

            pair_pos_ = lo | ((hi & 0xF0) << 4);
            pair_left_ = (hi & 0x0F) + MINREF + 1;
            last_literal_ = false;
            fresh_ = true;

            while(pair_left_ > 0 && n < max)
            {
                unsigned char c = ring_[pair_pos_];
                pair_pos_ = (pair_pos_ + 1) & (RING - 1);
                ring_[r_] = c;
                r_ = (r_ + 1) & (RING - 1);
                out[n++] = c;
                --pair_left_;
            }

            if(pair_left_ > 0)
                return n;
        }
    }

    return n;
}

int LzssDecoder::read_until_literal(ByteSource src, void *ctx, unsigned char *out, int max, bool *done)
{
    int n = 0;

    /* one token per call: the rest of a cut pair, or the next token */
    if(pair_left_ > 0)
    {
        while(pair_left_ > 0 && n < max)
        {
            unsigned char c = ring_[pair_pos_];
            pair_pos_ = (pair_pos_ + 1) & (RING - 1);
            ring_[r_] = c;
            r_ = (r_ + 1) & (RING - 1);
            out[n++] = c;
            --pair_left_;
        }

        return n;
    }

    {
        int bit = next_flag_bit(src, ctx);

        if(bit < 0)
        {
            *done = true;
            return n;
        }

        if(bit)
        {
            int c = src(ctx);

            if(c < 0)
            {
                *done = true;
                return n;
            }

            ring_[r_] = (unsigned char)c;
            r_ = (r_ + 1) & (RING - 1);
            out[n++] = (unsigned char)c;
            last_literal_ = true;
            *done = true;
            return n;
        }

        int lo = src(ctx);
        int hi = lo < 0 ? -1 : src(ctx);

        if(hi < 0)
        {
            *done = true;
            return n;
        }

        pair_pos_ = lo | ((hi & 0xF0) << 4);
        pair_left_ = (hi & 0x0F) + MINREF + 1;
        last_literal_ = false;

        while(pair_left_ > 0 && n < max)
        {
            unsigned char c = ring_[pair_pos_];
            pair_pos_ = (pair_pos_ + 1) & (RING - 1);
            ring_[r_] = c;
            r_ = (r_ + 1) & (RING - 1);
            out[n++] = c;
            --pair_left_;
        }
    }

    return n;
}

/* ------------------------------------------------------------------------ */
/* Encoder                                                                */
/* ------------------------------------------------------------------------ */

struct LzssEncoder::Tree
{
    unsigned char text[RING + LOOK - 1];
    int left[RING + 1];
    int right[RING + 257];
    int parent[RING + 1];
};

LzssEncoder::LzssEncoder() : t_(new Tree)
{
    reset();
}

LzssEncoder::~LzssEncoder()
{
    delete t_;
}

void LzssEncoder::reset()
{
    memset(t_->text, 0, sizeof(t_->text));

    for(int i = RING + 1; i <= RING + 256; ++i)
        t_->right[i] = NONE;

    for(int i = 0; i < RING; ++i)
        t_->parent[i] = NONE;

    in_.clear();
    in_pos_ = 0;
    phase_ = 0;
    s_ = 0;
    r_ = START;
    len_ = 0;
    last_match_ = 0;
    match_pos_ = 0;
    match_len_ = 0;
    code_[0] = 0;
    code_ptr_ = 1;
    mask_ = 1;
}

/* Inserts the string at position r into the tree and leaves the longest
 * match with an existing string in match_pos_ / match_len_. */
void LzssEncoder::insert_node(int r)
{
    Tree &t = *t_;
    const unsigned char *key = &t.text[r];
    int p = RING + 1 + key[0];
    int cmp = 1;

    t.right[r] = t.left[r] = NONE;
    match_len_ = 0;

    for(;;)
    {
        if(cmp >= 0)
        {
            if(t.right[p] != NONE)
                p = t.right[p];
            else
            {
                t.right[p] = r;
                t.parent[r] = p;
                return;
            }
        }
        else
        {
            if(t.left[p] != NONE)
                p = t.left[p];
            else
            {
                t.left[p] = r;
                t.parent[r] = p;
                return;
            }
        }

        int i = 1;

        for(; i < LOOK; ++i)
        {
            cmp = (int)key[i] - (int)t.text[p + i];

            if(cmp != 0)
                break;
        }

        if(i > match_len_)
        {
            match_pos_ = p;
            match_len_ = i;

            if(i >= LOOK)
                break;
        }
    }

    /* Same string as node p: r takes p's place in the tree. */
    t.parent[r] = t.parent[p];
    t.left[r] = t.left[p];
    t.right[r] = t.right[p];
    t.parent[t.left[p]] = r;
    t.parent[t.right[p]] = r;

    if(t.right[t.parent[p]] == p)
        t.right[t.parent[p]] = r;
    else
        t.left[t.parent[p]] = r;

    t.parent[p] = NONE;
}

void LzssEncoder::delete_node(int p)
{
    Tree &t = *t_;
    int q;

    if(t.parent[p] == NONE)
        return;

    if(t.right[p] == NONE)
        q = t.left[p];
    else if(t.left[p] == NONE)
        q = t.right[p];
    else
    {
        q = t.left[p];

        if(t.right[q] != NONE)
        {
            do
                q = t.right[q];
            while(t.right[q] != NONE);

            t.right[t.parent[q]] = t.left[q];
            t.parent[t.left[q]] = t.parent[q];
            t.left[q] = t.left[p];
            t.parent[t.left[p]] = q;
        }

        t.right[q] = t.right[p];
        t.parent[t.right[p]] = q;
    }

    t.parent[q] = t.parent[p];

    if(t.right[t.parent[p]] == p)
        t.right[t.parent[p]] = q;
    else
        t.left[t.parent[p]] = q;

    t.parent[p] = NONE;
}

int LzssEncoder::getc_in()
{
    if(in_pos_ >= in_.size())
        return -1;

    return in_[in_pos_++];
}

void LzssEncoder::flush_code(ByteSink sink, void *ctx)
{
    for(int i = 0; i < code_ptr_; ++i)
        sink(code_[i], ctx);

    code_[0] = 0;
    code_ptr_ = 1;
    mask_ = 1;
}

void LzssEncoder::write(const unsigned char *data, size_t n, ByteSink sink, void *ctx)
{
    if(phase_ == 2 || n == 0)
        return;

    /* drop consumed input now and then */
    if(in_pos_ > 65536)
    {
        in_.erase(in_.begin(), in_.begin() + (std::ptrdiff_t)in_pos_);
        in_pos_ = 0;
    }

    in_.insert(in_.end(), data, data + n);
    run(false, sink, ctx);
}

void LzssEncoder::finish(ByteSink sink, void *ctx)
{
    if(phase_ == 2)
        return;

    run(true, sink, ctx);
}

/* Runs the coder as far as the buffered input allows. Without final_ a step
 * only starts when a whole lookahead of input is available, so a step never
 * sees the end of the input before the stream really ends: the output is the
 * same however the input is split into writes. */
void LzssEncoder::run(bool final_, ByteSink sink, void *ctx)
{
    Tree &t = *t_;

    if(phase_ == 0)
    {
        if(!final_ && in_.size() - in_pos_ < (size_t)LOOK)
            return;

        int c;

        for(len_ = 0; len_ < LOOK && (c = getc_in()) >= 0; ++len_)
            t.text[r_ + len_] = (unsigned char)c;

        if(len_ == 0)
        {
            phase_ = 2;
            return;
        }

        for(int i = 1; i <= LOOK; ++i)
            insert_node(r_ - i);

        insert_node(r_);
        phase_ = 1;
    }

    while(phase_ == 1)
    {
        if(!final_ && in_.size() - in_pos_ < (size_t)LOOK)
            return;

        if(match_len_ > len_)
            match_len_ = len_;

        if(match_len_ <= MINREF)
        {
            match_len_ = 1;
            code_[0] |= mask_;
            code_[code_ptr_++] = t.text[r_];
        }
        else
        {
            code_[code_ptr_++] = (unsigned char)match_pos_;
            code_[code_ptr_++] = (unsigned char)(((match_pos_ >> 4) & 0xF0) | (match_len_ - (MINREF + 1)));
        }

        mask_ = (unsigned char)(mask_ << 1);

        if(mask_ == 0)
            flush_code(sink, ctx);

        last_match_ = match_len_;
        int i = 0, c;

        for(; i < last_match_ && (c = getc_in()) >= 0; ++i)
        {
            delete_node(s_);
            t.text[s_] = (unsigned char)c;

            if(s_ < LOOK - 1)
                t.text[s_ + RING] = (unsigned char)c;

            s_ = (s_ + 1) & (RING - 1);
            r_ = (r_ + 1) & (RING - 1);
            insert_node(r_);
        }

        while(i++ < last_match_)
        {
            delete_node(s_);
            s_ = (s_ + 1) & (RING - 1);
            r_ = (r_ + 1) & (RING - 1);

            if(--len_)
                insert_node(r_);
        }

        if(len_ <= 0)
        {
            if(code_ptr_ > 1)
                flush_code(sink, ctx);

            phase_ = 2;
        }
    }
}

namespace
{
void sink_vector(int c, void *ctx)
{
    ((std::vector<unsigned char> *)ctx)->push_back((unsigned char)c);
}
}

std::vector<unsigned char> lzss_compress(const unsigned char *data, size_t n)
{
    std::vector<unsigned char> out;
    LzssEncoder e;
    e.write(data, n, sink_vector, &out);
    e.finish(sink_vector, &out);
    return out;
}

} /* namespace zcfiles */

/* ------------------------------------------------------------------------ */
/* Public LZSS API (allegro/lzss.h)                                         */
/* ------------------------------------------------------------------------ */

struct LZSS_PACK_DATA
{
    zcfiles::LzssEncoder enc;
};

struct LZSS_UNPACK_DATA
{
    zcfiles::LzssDecoder dec;
};

namespace
{
void sink_packfile(int c, void *ctx)
{
    pack_putc(c, (PACKFILE *)ctx);
}

int source_packfile(void *ctx)
{
    return pack_getc((PACKFILE *)ctx);
}
}

extern "C" {

LZSS_PACK_DATA *create_lzss_pack_data(void)
{
    return new LZSS_PACK_DATA;
}

void free_lzss_pack_data(LZSS_PACK_DATA *dat)
{
    delete dat;
}

int lzss_write(PACKFILE *file, LZSS_PACK_DATA *dat, int size, unsigned char *buf, int last)
{
    if(size > 0)
        dat->enc.write(buf, (size_t)size, sink_packfile, file);

    if(last)
        dat->enc.finish(sink_packfile, file);

    return pack_ferror(file) ? EOF : 0;
}

LZSS_UNPACK_DATA *create_lzss_unpack_data(void)
{
    return new LZSS_UNPACK_DATA;
}

void free_lzss_unpack_data(LZSS_UNPACK_DATA *dat)
{
    delete dat;
}

int lzss_read(PACKFILE *file, LZSS_UNPACK_DATA *dat, int s, unsigned char *buf)
{
    bool end = false;

    if(s <= 0)
        return 0;

    return dat->dec.read(source_packfile, file, buf, s, &end, false);
}

} /* extern "C" */

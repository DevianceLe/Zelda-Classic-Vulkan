// zcport files module: private declarations shared by the files_*.cpp
// sources (packfiles, LZSS, datafiles, file names, bitmap savers).
// Not part of any contract; other modules must not include this.
//
// Written from scratch for zcport.

#ifndef ZCPORT_FILES_INTERNAL_H
#define ZCPORT_FILES_INTERNAL_H

#include <allegro.h>
#include <allegro/internal/aintern.h>

#include <stdint.h>
#include <stdio.h>

#include <string>
#include <vector>

/* files_password.cpp */
const char *zcport_packfile_password(void);

namespace zcfiles
{

/* ---- LZSS (files_lzss.cpp) ------------------------------------------------- */

/* A byte source for the decoder: returns 0..255 or -1 at the end. */
typedef int (*ByteSource)(void *ctx);
/* A byte sink for the encoder. */
typedef void (*ByteSink)(int c, void *ctx);

class LzssDecoder
{
public:
    LzssDecoder();
    void reset();

    /* Old-format password files XOR every flags byte with the password. */
    void set_flag_key(const std::string &key);

    /* Produces up to max bytes. Sets *hit_end when the source ran out.
     * With stop_after_cut_pair, a call that starts by finishing a pair cut
     * by the previous call returns right after it (how the packed PACKFILE
     * layer of the reference behaves); the lzss_read() API goes on. */
    int read(ByteSource src, void *ctx, unsigned char *out, int max, bool *hit_end, bool stop_after_cut_pair = true);

    /* Decodes on until a literal has been produced (or the source ends, or
     * max bytes were produced). Returns the count; *done is set when a
     * literal ended the run or the source ran out. */
    int read_until_literal(ByteSource src, void *ctx, unsigned char *out, int max, bool *done);

    bool last_was_literal() const { return last_literal_; }
    /* the last read() decoded at least one new token (not only the rest of
     * a cut pair) */
    bool fresh() const { return fresh_; }
    bool pair_pending() const { return pair_left_ > 0; }

private:
    int next_flag_bit(ByteSource src, void *ctx);   /* 0/1, or -1 at the end */
    unsigned char ring_[4096];
    int r_;
    unsigned flags_;
    int bits_left_;
    int pair_pos_;
    int pair_left_;
    bool last_literal_;
    bool fresh_;
    std::string key_;
    size_t key_pos_;
};

class LzssEncoder
{
public:
    LzssEncoder();
    ~LzssEncoder();
    void reset();
    /* Feeds input; output goes to the sink as it is produced. */
    void write(const unsigned char *data, size_t n, ByteSink sink, void *ctx);
    /* Ends the stream (flushes everything). */
    void finish(ByteSink sink, void *ctx);

private:
    void run(bool final_, ByteSink sink, void *ctx);
    void insert_node(int r);
    void delete_node(int p);
    void flush_code(ByteSink sink, void *ctx);
    int getc_in();

    struct Tree;
    Tree *t_;
    std::vector<unsigned char> in_;
    size_t in_pos_;
    int phase_;            /* 0 = start, 1 = coding, 2 = done */
    int s_, r_, len_, last_match_;
    int match_pos_, match_len_;
    unsigned char code_[17];
    int code_ptr_;
    unsigned char mask_;
};

/* Compresses a whole buffer (fresh state), as a packed chunk stores it. */
std::vector<unsigned char> lzss_compress(const unsigned char *data, size_t n);

/* ---- packfiles (files_packfile.cpp) ---------------------------------------- */

/* The XOR mask applied to a magic number for a password (new_format adds
 * the 42 of current Allegro files). Empty password: 0. */
uint32_t magic_mask(const std::string &pw, bool new_format);

/* pack_fclose(chunk) also closes the chunk's parent chain. */
void set_close_parent(PACKFILE *chunk);

/* Data size of a chunk opened for reading (uncompressed). */
int64_t chunk_size(PACKFILE *chunk);

/* Opens "file.dat#object" (read only). NULL + errno on failure. */
PACKFILE *open_special(const char *filename);

/* Size of the "file.dat#object" object, 0 if missing. */
uint64_t special_size(const char *filename);

/* What load_bitmap() hands to the program (files_loaders.cpp). */
BITMAP *fixup_loaded_bitmap(BITMAP *bmp, RGB *pal);

/* Plain OS helpers (UTF-8 file names on Windows). */
FILE *os_fopen(const char *filename, const char *mode);
bool os_stat(const char *filename, uint64_t *size, int *attrib, time_t *mtime);

} /* namespace zcfiles */

#endif

// zcport files module: PACKFILE I/O.
//
// A PACKFILE is a chain of nodes:
//   file node      an OS file, read or written through a 4 KB buffer; with a
//                  password every byte is XORed with the password, cycling,
//                  from the first byte of the file (the magic included);
//   packed node    LZSS decoding/encoding on top of a parent node;
//   chunk node     a length-prefixed part of the parent (optionally LZSS);
//   vtable node    pack_fopen_vtable() (the user's functions).
// The magic of "rp"/"wp"/"w!" files is F_PACK_MAGIC / F_NOPACK_MAGIC XORed
// with a mask derived from the password (magic_mask()). Old-format files
// (mask without the final 42) are read too: their LZSS flags bytes carry
// the password instead of the whole byte stream.
//
// Observable details (EOF flags, return values, errno) follow the reference
// build; tests/conformance/test_files_pack.cpp pins them down.
//
// Written from scratch for zcport.

#include "files_internal.h"
#include "zcport/pack_record.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#endif

using namespace zcfiles;

namespace
{

enum Kind
{
    K_FILE_READ,
    K_FILE_WRITE,
    K_PACK_READ,
    K_PACK_WRITE,
    K_CHUNK_READ,
    K_CHUNK_PACK_READ,
    K_CHUNK_WRITE
};

struct Node
{
    PACKFILE *pf;
    Kind kind;

    FILE *fp;
    std::string pw;          /* raw-layer password stream ("" = none) */
    size_t pwpos;

    unsigned char buf[F_BUF_SIZE];
    int pos, size;           /* read buffer / write fill */
    int64_t todo;            /* file read: bytes left on disk; chunk: data bytes left */
    bool eof_pending;        /* packed: the decoder reached the end of its input */
    bool overread_done;      /* packed chunk: finished reading past its end */
    bool at_end;             /* file read: the last refill reached the end of the file */
    int unget;               /* pushed back byte, -1 none */

    PACKFILE *parent;
    bool close_parent;       /* pack_fclose also closes the parent chain */
    LzssDecoder *dec;
    LzssEncoder *enc;

    int64_t chunk_raw;       /* packed chunk: compressed size / raw chunk: stored size */
    int64_t chunk_data;      /* chunk read: data size (uncompressed) */
    int64_t consumed;        /* raw bytes taken from the parent */
    std::vector<unsigned char> wdata;   /* chunk write */
    bool wpack;

    Node() : pf(0), kind(K_FILE_READ), fp(0), pwpos(0), pos(0), size(0), todo(0),
             eof_pending(false), overread_done(false), at_end(false), unget(-1), parent(0), close_parent(false),
             dec(0), enc(0), chunk_raw(0), consumed(0), wpack(false)
    {
    }
};

Node *node_of(PACKFILE *f)
{
    return (Node *)f->userdata;
}

void set_flag(PACKFILE *f, int flag)
{
    f->normal.flags |= flag;
}

/* ---- raw OS layer -------------------------------------------------------- */

int os_seek_cur(FILE *fp, int64_t off)
{
#ifdef _WIN32
    return _fseeki64(fp, off, SEEK_CUR);
#else
    return fseeko(fp, (off_t)off, SEEK_CUR);
#endif
}

void crypt_bytes(Node *n, unsigned char *p, int count)
{
    if(n->pw.empty())
        return;

    for(int i = 0; i < count; ++i)
    {
        p[i] ^= (unsigned char)n->pw[n->pwpos];

        if(++n->pwpos >= n->pw.size())
            n->pwpos = 0;
    }
}

void file_flush(Node *n)
{
    if(n->size > 0)
    {
        crypt_bytes(n, n->buf, n->size);

        if(fwrite(n->buf, 1, (size_t)n->size, n->fp) != (size_t)n->size)
        {
            set_flag(n->pf, PACKFILE_FLAG_ERROR);
        }

        n->size = 0;
    }
}

int node_getc(Node *n);
int node_putc(Node *n, int c);

int src_parent(void *ctx)
{
    Node *n = (Node *)ctx;
    int c = pack_getc(n->parent);

    if(c >= 0)
        ++n->consumed;

    return c;
}

void sink_parent(int c, void *ctx)
{
    pack_putc(c, ((Node *)ctx)->parent);
}

/* True when the raw parent of a packed node has no more bytes. */
bool parent_exhausted(Node *n)
{
    Node *p = node_of(n->parent);

    if(p->unget >= 0 || p->pos < p->size)
        return false;

    return p->kind == K_FILE_READ ? p->at_end || p->todo <= 0 : false;
}

/* Fills the read buffer. Returns the byte count (0 = end). */
int refill(Node *n)
{
    n->pos = 0;
    n->size = 0;

    switch(n->kind)
    {
    case K_FILE_READ:
    {
        if(n->todo <= 0)
        {
            n->at_end = true;
            return 0;
        }

        size_t want = n->todo < F_BUF_SIZE ? (size_t)n->todo : (size_t)F_BUF_SIZE;
        size_t got = fread(n->buf, 1, want, n->fp);

        if(got < want)
        {
            n->todo = 0;

            if(ferror(n->fp))
                set_flag(n->pf, PACKFILE_FLAG_ERROR);
        }
        else
            n->todo -= (int64_t)got;

        if(n->todo <= 0)
            n->at_end = true;

        crypt_bytes(n, n->buf, (int)got);
        n->size = (int)got;
        return n->size;
    }

    case K_PACK_READ:
    {
        /* When the reference knows the end of a packed file (pack_feof true
         * right after its last byte): when a refill decoded new tokens and
         * then ran out of input, or filled the buffer with a literal that
         * was the last input byte. A refill that only finished a pair cut
         * by the previous one, or that filled the buffer with a pair, does
         * not see the end; the next read returns EOF. */
        bool end = false;
        n->size = n->dec->read(src_parent, n, n->buf, F_BUF_SIZE, &end, false);

        if(end && n->dec->fresh())
            n->eof_pending = true;
        else if(!end && n->size == F_BUF_SIZE && n->dec->fresh() && n->dec->last_was_literal() &&
                !n->dec->pair_pending() && parent_exhausted(n))
            n->eof_pending = true;

        return n->size;
    }

    case K_CHUNK_READ:
    {
        if(n->todo <= 0)
            return 0;

        long want = n->todo < F_BUF_SIZE ? (long)n->todo : (long)F_BUF_SIZE;
        long got = pack_fread(n->buf, want, n->parent);

        if(got < 0)
            got = 0;

        n->consumed += got;

        if(got < want)
            n->todo = 0;
        else
            n->todo -= got;

        n->size = (int)got;
        return n->size;
    }

    case K_CHUNK_PACK_READ:
    {
        if(n->todo > 0)
        {
            bool end = false;
            int want = n->todo < F_BUF_SIZE ? (int)n->todo : F_BUF_SIZE;
            n->size = n->dec->read(src_parent, n, n->buf, want, &end);
            n->todo -= n->size;

            if(end)
            {
                n->eof_pending = true;
                n->overread_done = true;
                n->todo = 0;
            }

            return n->size;
        }

        /* All the data was produced: the reference goes on decoding the
         * bytes that follow the chunk until a literal comes out, unless the
         * data itself ended with a literal. */
        if(n->overread_done || (n->dec->last_was_literal() && !n->dec->pair_pending()))
        {
            n->overread_done = true;
            return 0;
        }

        bool done = false;
        n->size = n->dec->read_until_literal(src_parent, n, n->buf, F_BUF_SIZE, &done);

        if(done)
            n->overread_done = true;

        return n->size;
    }

    default:
        return 0;
    }
}

bool node_at_end(Node *n)
{
    switch(n->kind)
    {
    case K_FILE_READ:
        return n->at_end;

    case K_CHUNK_READ:
        return n->todo <= 0;

    case K_PACK_READ:
        return n->eof_pending;

    case K_CHUNK_PACK_READ:
        return n->overread_done || (n->todo <= 0 && n->dec->last_was_literal() && !n->dec->pair_pending());

    default:
        return true;
    }
}

int node_getc(Node *n)
{
    if(n->unget >= 0)
    {
        int c = n->unget;
        n->unget = -1;
        return c;
    }

    if(n->pos < n->size)
        return n->buf[n->pos++];

    if((n->pf->normal.flags & PACKFILE_FLAG_EOF) || refill(n) == 0)
    {
        set_flag(n->pf, PACKFILE_FLAG_EOF);
        return EOF;
    }

    return n->buf[n->pos++];
}

int node_putc(Node *n, int c)
{
    unsigned char b = (unsigned char)c;

    switch(n->kind)
    {
    case K_FILE_WRITE:
        n->buf[n->size++] = b;

        if(n->size >= F_BUF_SIZE)
            file_flush(n);

        return b;

    case K_PACK_WRITE:
        n->buf[n->size++] = b;

        if(n->size >= F_BUF_SIZE)
        {
            n->enc->write(n->buf, (size_t)n->size, sink_parent, n);
            n->size = 0;
        }

        return b;

    case K_CHUNK_WRITE:
        n->wdata.push_back(b);
        return b;

    default:
        return EOF;
    }
}

/* ---- vtable for nodes ------------------------------------------------------ */

int nv_fclose(void *u);

int nv_getc(void *u)
{
    Node *n = (Node *)u;

    if(n->pf->normal.flags & PACKFILE_FLAG_WRITE)
        return EOF;

    return node_getc(n);
}

int nv_ungetc(int c, void *u)
{
    Node *n = (Node *)u;
    n->unget = (unsigned char)c;
    n->pf->normal.flags &= ~PACKFILE_FLAG_EOF;
    return (unsigned char)c;
}

long nv_fread(void *p, long count, void *u)
{
    Node *n = (Node *)u;
    unsigned char *out = (unsigned char *)p;
    long i = 0;

    if(n->pf->normal.flags & PACKFILE_FLAG_WRITE)
        return 0;

    while(i < count)
    {
        if(n->unget >= 0)
        {
            out[i++] = (unsigned char)n->unget;
            n->unget = -1;
            continue;
        }

        if(n->pos >= n->size)
        {
            if((n->pf->normal.flags & PACKFILE_FLAG_EOF) || refill(n) == 0)
            {
                set_flag(n->pf, PACKFILE_FLAG_EOF);
                break;
            }
        }

        long take = n->size - n->pos;

        if(take > count - i)
            take = count - i;

        memcpy(out + i, n->buf + n->pos, (size_t)take);
        n->pos += (int)take;
        i += take;
    }

    return i;
}

int nv_putc(int c, void *u)
{
    Node *n = (Node *)u;

    if(!(n->pf->normal.flags & PACKFILE_FLAG_WRITE))
        return EOF;

    return node_putc(n, c);
}

long nv_fwrite(const void *p, long count, void *u)
{
    Node *n = (Node *)u;
    const unsigned char *in = (const unsigned char *)p;

    if(!(n->pf->normal.flags & PACKFILE_FLAG_WRITE))
        return 0;

    if(n->kind == K_CHUNK_WRITE)
    {
        n->wdata.insert(n->wdata.end(), in, in + count);
        return count;
    }

    for(long i = 0; i < count; ++i)
        node_putc(n, in[i]);

    return count;
}

int nv_fseek(void *u, int offset)
{
    Node *n = (Node *)u;

    if(n->pf->normal.flags & PACKFILE_FLAG_WRITE)
        return -1;

    if(offset < 0)
        return -1;

    int left = offset;

    if(left > 0 && n->unget >= 0)
    {
        n->unget = -1;
        --left;
    }

    while(left > 0)
    {
        if(n->pos >= n->size)
        {
            /* a packed chunk skips no further than its data */
            if(n->kind == K_CHUNK_PACK_READ && n->todo <= 0)
                break;

            /* raw files: skip whole buffers on disk */
            if(n->kind == K_FILE_READ && n->pw.empty() && left > F_BUF_SIZE && n->todo > F_BUF_SIZE)
            {
                int64_t skip = ((int64_t)(left - 1) / F_BUF_SIZE) * F_BUF_SIZE;

                if(skip > n->todo)
                    skip = (n->todo / F_BUF_SIZE) * F_BUF_SIZE;

                if(skip > 0 && os_seek_cur(n->fp, skip) == 0)
                {
                    n->todo -= skip;
                    left -= (int)skip;
                    continue;
                }
            }

            if((n->pf->normal.flags & PACKFILE_FLAG_EOF) || refill(n) == 0)
            {
                set_flag(n->pf, PACKFILE_FLAG_EOF);
                break;
            }
        }

        int take = n->size - n->pos;

        if(take > left)
            take = left;

        n->pos += take;
        left -= take;
    }

    /* seeking past the end is not an error (the reference returns 0) */
    return 0;
}

int nv_feof(void *u)
{
    Node *n = (Node *)u;

    if(n->unget >= 0)
        return 0;

    if(n->pf->normal.flags & PACKFILE_FLAG_EOF)
        return PACKFILE_FLAG_EOF;

    if(n->pos < n->size)
        return 0;

    return node_at_end(n) ? PACKFILE_FLAG_EOF : 0;
}

int nv_ferror(void *u)
{
    Node *n = (Node *)u;
    return n->pf->normal.flags & PACKFILE_FLAG_ERROR;
}

const PACKFILE_VTABLE g_node_vtable = { nv_fclose, nv_getc, nv_ungetc, nv_fread, nv_putc,
                                        nv_fwrite, nv_fseek, nv_feof, nv_ferror };

PACKFILE *new_node_packfile(Kind kind, Node **out)
{
    PACKFILE *f = (PACKFILE *)calloc(1, sizeof(PACKFILE));

    if(!f)
        return NULL;

    Node *n = new Node;
    n->pf = f;
    n->kind = kind;
    f->vtable = &g_node_vtable;
    f->userdata = n;
    f->is_normal_packfile = 1;
    f->normal.hndl = -1;
    f->normal.flags = 0;

    if(kind == K_FILE_WRITE || kind == K_PACK_WRITE || kind == K_CHUNK_WRITE)
        f->normal.flags |= PACKFILE_FLAG_WRITE;

    if(kind == K_PACK_READ || kind == K_PACK_WRITE || kind == K_CHUNK_PACK_READ)
        f->normal.flags |= PACKFILE_FLAG_PACK;

    if(kind == K_CHUNK_READ || kind == K_CHUNK_PACK_READ || kind == K_CHUNK_WRITE)
        f->normal.flags |= PACKFILE_FLAG_CHUNK;

    *out = n;
    return f;
}

void free_node(PACKFILE *f)
{
    Node *n = node_of(f);
    delete n->dec;
    delete n->enc;
    delete n;
    free(f);
}

/* Closes one node; returns 0 or an errno value. */
int close_node(PACKFILE *f)
{
    Node *n = node_of(f);
    int ret = 0;

    switch(n->kind)
    {
    case K_FILE_WRITE:
        file_flush(n);

        if(fclose(n->fp) != 0)
            ret = errno;

        if(f->normal.flags & PACKFILE_FLAG_ERROR)
            ret = ret ? ret : EIO;

        break;

    case K_FILE_READ:
        fclose(n->fp);
        break;

    case K_PACK_WRITE:
        if(n->size > 0)
            n->enc->write(n->buf, (size_t)n->size, sink_parent, n);

        n->size = 0;
        n->enc->finish(sink_parent, n);
        ret = pack_fclose(n->parent);
        n->parent = NULL;
        break;

    case K_PACK_READ:
        pack_fclose(n->parent);
        n->parent = NULL;
        break;

    default:
        break;
    }

    PACKFILE *parent = n->close_parent ? n->parent : NULL;
    free_node(f);

    if(parent)
        pack_fclose(parent);

    return ret;
}

int nv_fclose(void *u)
{
    Node *n = (Node *)u;
    return close_node(n->pf);
}

/* ---- open ----------------------------------------------------------------- */

struct Mode
{
    bool read, write, pack, nopack;
};

Mode parse_mode(const char *mode)
{
    Mode m = { false, false, false, false };

    for(const char *p = mode; *p; ++p)
    {
        switch(*p)
        {
        case 'r': case 'R': m.read = true; break;
        case 'w': case 'W': m.write = true; break;
        case 'p': case 'P': m.pack = true; break;
        case '!': m.nopack = true; m.pack = false; break;
        default: break;
        }
    }

    if(m.nopack)
        m.pack = false;

    return m;
}

PACKFILE *open_file_read(const char *filename, const std::string &pw, int *err)
{
    FILE *fp = os_fopen(filename, "rb");

    if(!fp)
    {
        *err = errno;
        return NULL;
    }

    Node *n;
    PACKFILE *f = new_node_packfile(K_FILE_READ, &n);
    n->fp = fp;
    n->pw = pw;
    setvbuf(fp, NULL, _IONBF, 0);

#ifdef _WIN32
    int64_t sz = _filelengthi64(_fileno(fp));
#else
    fseeko(fp, 0, SEEK_END);
    int64_t sz = ftello(fp);
    fseeko(fp, 0, SEEK_SET);
#endif
    n->todo = sz < 0 ? 0 : sz;
    return f;
}

PACKFILE *open_file_write(const char *filename, const std::string &pw, int *err)
{
    FILE *fp = os_fopen(filename, "wb");

    if(!fp)
    {
        *err = errno;
        return NULL;
    }

    Node *n;
    PACKFILE *f = new_node_packfile(K_FILE_WRITE, &n);
    n->fp = fp;
    n->pw = pw;
    setvbuf(fp, NULL, _IONBF, 0);
    return f;
}

/* Old-format files only scramble their magic: undo the stream XOR on the
 * bytes the first refill already scrambled and stop scrambling. (The first
 * refill started at file offset 0, so byte i used key byte i % length.) */
void drop_stream_key(Node *rn)
{
    const std::string &pw = rn->pw;

    for(int i = rn->pos; i < rn->size; ++i)
        rn->buf[i] ^= (unsigned char)pw[(size_t)i % pw.size()];

    rn->pw.clear();
}

void put_magic(PACKFILE *f, uint32_t v)
{
    node_putc(node_of(f), (int)((v >> 24) & 0xFF));
    node_putc(node_of(f), (int)((v >> 16) & 0xFF));
    node_putc(node_of(f), (int)((v >> 8) & 0xFF));
    node_putc(node_of(f), (int)(v & 0xFF));
}

PACKFILE *open_normal(const char *filename, const Mode &m)
{
    std::string pw = zcport_packfile_password();
    int saved_errno = errno;
    int err = 0;

    if(m.write)
    {
        PACKFILE *raw = open_file_write(filename, pw, &err);

        if(!raw)
        {
            errno = err;
            return NULL;
        }

        if(m.pack)
        {
            put_magic(raw, (uint32_t)F_PACK_MAGIC ^ magic_mask(pw, true));
            Node *n;
            PACKFILE *f = new_node_packfile(K_PACK_WRITE, &n);
            n->parent = raw;
            n->enc = new LzssEncoder;
            errno = saved_errno;
            return f;
        }

        if(m.nopack)
            put_magic(raw, (uint32_t)F_NOPACK_MAGIC ^ magic_mask(pw, true));

        errno = saved_errno;
        return raw;
    }

    PACKFILE *raw = open_file_read(filename, pw, &err);

    if(!raw)
    {
        errno = err;
        return NULL;
    }

    if(!m.pack)
    {
        errno = saved_errno;
        return raw;
    }

    Node *rn = node_of(raw);
    uint32_t magic = 0;
    int i = 0;

    for(; i < 4; ++i)
    {
        int c = node_getc(rn);

        if(c < 0)
            break;

        magic = (magic << 8) | (uint32_t)c;
    }

    uint32_t m_new = magic_mask(pw, true);
    uint32_t m_old = magic_mask(pw, false);

    if(i == 4 && magic == ((uint32_t)F_PACK_MAGIC ^ m_new))
    {
        Node *n;
        PACKFILE *f = new_node_packfile(K_PACK_READ, &n);
        n->parent = raw;
        n->dec = new LzssDecoder;
        errno = saved_errno;
        return f;
    }

    if(i == 4 && magic == ((uint32_t)F_NOPACK_MAGIC ^ m_new))
    {
        errno = saved_errno;
        return raw;
    }

    if(i == 4 && !pw.empty() && magic == ((uint32_t)F_PACK_MAGIC ^ m_old))
    {
        /* old format: only the LZSS flags bytes are scrambled */
        drop_stream_key(rn);
        Node *n;
        PACKFILE *f = new_node_packfile(K_PACK_READ, &n);
        n->parent = raw;
        n->dec = new LzssDecoder;
        n->dec->set_flag_key(pw);
        f->normal.flags |= PACKFILE_FLAG_OLD_CRYPT;
        raw->normal.flags |= PACKFILE_FLAG_OLD_CRYPT;
        errno = saved_errno;
        return f;
    }

    if(i == 4 && !pw.empty() && magic == ((uint32_t)F_NOPACK_MAGIC ^ m_old))
    {
        drop_stream_key(rn);
        raw->normal.flags |= PACKFILE_FLAG_OLD_CRYPT;
        errno = saved_errno;
        return raw;
    }

    close_node(raw);
    errno = EDOM;
    return NULL;
}

} /* namespace */

/* ------------------------------------------------------------------------ */

namespace zcfiles
{

uint32_t magic_mask(const std::string &pw, bool new_format)
{
    if(pw.empty())
        return 0;

    uint32_t m = 0;

    /* the password bytes as (signed) chars, folded over the four bytes */
    for(size_t i = 0; i < pw.size(); ++i)
        m ^= (uint32_t)(int32_t)(signed char)pw[i] << ((i & 3) * 8);

    size_t pos = 0;

    for(int k = 0; k < 4; ++k)
    {
        m ^= (uint32_t)(int32_t)(signed char)pw[pos] << (24 - k * 8);

        if(++pos >= pw.size())
            pos = 0;
    }

    if(new_format)
        m ^= 42;

    return m;
}

void set_close_parent(PACKFILE *chunk)
{
    if(chunk && chunk->vtable == &g_node_vtable)
        node_of(chunk)->close_parent = true;
}

int64_t chunk_size(PACKFILE *chunk)
{
    if(chunk && chunk->vtable == &g_node_vtable)
        return node_of(chunk)->chunk_data;

    return 0;
}

FILE *os_fopen(const char *filename, const char *mode)
{
    return fopen(filename, mode);
}

} /* namespace zcfiles */

extern "C" {

PACKFILE *pack_fopen(AL_CONST char *filename, AL_CONST char *mode)
{
    if(!filename || !mode)
    {
        errno = EINVAL;
        return NULL;
    }

    Mode m = parse_mode(mode);

    /* a real file wins, so '#' can be part of an ordinary folder or file name */
    if(strchr(filename, '#') && !zcfiles::os_stat(filename, NULL, NULL, NULL))
    {
        if(!m.write)
            return open_special(filename);

        /* the reference reports EROFS and opens the plain name */
        PACKFILE *f = open_normal(filename, m);

        if(f)
            errno = EROFS;

        return f;
    }

    if(!m.read && !m.write)
        m.read = true;

    return open_normal(filename, m);
}

PACKFILE *pack_fopen_vtable(AL_CONST PACKFILE_VTABLE *vtable, void *userdata)
{
    PACKFILE *f = (PACKFILE *)calloc(1, sizeof(PACKFILE));

    if(!f)
    {
        errno = ENOMEM;
        return NULL;
    }

    f->vtable = vtable;
    f->userdata = userdata;
    f->is_normal_packfile = 0;
    f->normal.hndl = -1;
    return f;
}

int pack_fclose(PACKFILE *f)
{
    if(!f)
        return 0;

    if(f->vtable == &g_node_vtable)
    {
        int saved = errno;
        int ret = close_node(f);

        if(ret)
        {
            errno = ret;
            return ret;
        }

        errno = saved;
        return 0;
    }

    int ret = f->vtable->pf_fclose(f->userdata);
    free(f);

    if(ret)
        errno = ret;

    return ret;
}

// ---- recording (zcport extension, see zcport/pack_record.h) -------------------------

static PACKFILE *g_rec_file = NULL;
static zcport_pack_record_fn g_rec_fn = NULL;
static void *g_rec_user = NULL;

void zcport_pack_record(PACKFILE *f, zcport_pack_record_fn fn, void *user)
{
    g_rec_file = fn ? f : NULL;
    g_rec_fn = fn;
    g_rec_user = user;
}

static inline void rec_bytes(PACKFILE *f, const void *p, long n)
{
    if(f == g_rec_file && n > 0)
        g_rec_fn(g_rec_user, (const unsigned char *)p, n, 0);
}

int pack_fseek(PACKFILE *f, int offset)
{
    // While recording, a forward skip is read, so the skipped bytes are recorded too.
    if(f == g_rec_file && offset > 0)
    {
        unsigned char buf[4096];

        while(offset > 0)
        {
            long want = offset < (int)sizeof(buf) ? offset : (long)sizeof(buf);
            long got = f->vtable->pf_fread(buf, want, f->userdata);
            rec_bytes(f, buf, got);

            if(got != want)
                return -1;

            offset -= (int)want;
        }

        return 0;
    }

    return f->vtable->pf_fseek(f->userdata, offset);
}

PACKFILE *pack_fopen_chunk(PACKFILE *f, int pack)
{
    if(!f || f->vtable != &g_node_vtable)
        return NULL;

    if(f->normal.flags & PACKFILE_FLAG_WRITE)
    {
        Node *n;
        PACKFILE *c = new_node_packfile(K_CHUNK_WRITE, &n);
        n->parent = f;
        n->wpack = pack != 0;
        return c;
    }

    long raw = pack_mgetl(f);
    long data = pack_mgetl(f);

    Node *n;
    PACKFILE *c;

    if(data < 0)
    {
        c = new_node_packfile(K_CHUNK_PACK_READ, &n);
        n->dec = new LzssDecoder;
        n->todo = -(int64_t)data;
    }
    else
    {
        c = new_node_packfile(K_CHUNK_READ, &n);
        n->todo = data;
    }

    n->chunk_raw = raw;
    n->chunk_data = n->todo;
    n->parent = f;
    return c;
}

/* Reads a packfile magic number from an already opened stream: a packed payload
 * returns a new packfile that decompresses from f and owns it, an unpacked one
 * returns f itself, anything else NULL with f left open. */
PACKFILE *pack_fopen_unpack_parent(PACKFILE *f)
{
    if(!f || f->vtable != &g_node_vtable)
        return NULL;

    uint32_t magic = 0;

    for(int i = 0; i < 4; ++i)
    {
        int c = pack_getc(f);

        if(c < 0)
            return NULL;

        magic = (magic << 8) | (uint32_t)c;
    }

    uint32_t mask = magic_mask(zcport_packfile_password(), true);

    if(magic == ((uint32_t)F_NOPACK_MAGIC ^ mask))
        return f;

    if(magic != ((uint32_t)F_PACK_MAGIC ^ mask))
        return NULL;

    Node *n;
    PACKFILE *p = new_node_packfile(K_PACK_READ, &n);
    n->parent = f;
    n->dec = new LzssDecoder;
    return p;
}

PACKFILE *pack_fclose_chunk(PACKFILE *f)
{
    if(!f || f->vtable != &g_node_vtable)
        return NULL;

    Node *n = node_of(f);

    if(!(f->normal.flags & PACKFILE_FLAG_CHUNK))
        return NULL;

    PACKFILE *parent = n->parent;

    if(n->kind == K_CHUNK_WRITE)
    {
        if(n->wpack)
        {
            std::vector<unsigned char> z = lzss_compress(n->wdata.empty() ? NULL : &n->wdata[0], n->wdata.size());
            pack_mputl((long)z.size(), parent);
            pack_mputl(-(long)n->wdata.size(), parent);

            if(!z.empty())
                pack_fwrite(&z[0], (long)z.size(), parent);
        }
        else
        {
            pack_mputl((long)n->wdata.size(), parent);
            pack_mputl((long)n->wdata.size(), parent);

            if(!n->wdata.empty())
                pack_fwrite(&n->wdata[0], (long)n->wdata.size(), parent);
        }
    }
    else
    {
        /* skip what is left of the stored bytes (nothing if the reader
         * already went past them) */
        int64_t skip = n->kind == K_CHUNK_READ ? n->todo : n->chunk_raw - n->consumed;

        while(skip > 0)
        {
            int step = skip > INT_MAX ? INT_MAX : (int)skip;

            if(pack_fseek(parent, step) != 0)
                break;

            skip -= step;
        }
    }

    free_node(f);
    return parent;
}

int pack_getc(PACKFILE *f)
{
    int c = f->vtable->pf_getc(f->userdata);

    if(f == g_rec_file && c != EOF)
    {
        unsigned char b = (unsigned char)c;
        g_rec_fn(g_rec_user, &b, 1, 0);
    }

    return c;
}

int pack_putc(int c, PACKFILE *f)
{
    return f->vtable->pf_putc(c, f->userdata);
}

int pack_feof(PACKFILE *f)
{
    return f->vtable->pf_feof(f->userdata);
}

int pack_ferror(PACKFILE *f)
{
    return f->vtable->pf_ferror(f->userdata);
}

int pack_igetw(PACKFILE *f)
{
    int b1 = pack_getc(f);

    if(b1 == EOF)
        return EOF;

    int b2 = pack_getc(f);

    if(b2 == EOF)
        return EOF;

    return (b2 << 8) | b1;
}

long pack_igetl(PACKFILE *f)
{
    int b[4];

    for(int i = 0; i < 4; ++i)
        if((b[i] = pack_getc(f)) == EOF)
            return EOF;

    // A signed 32-bit value, as with Allegro where long is 32 bits (on LP64
    // systems the cast through int32_t sign-extends: datafiles mark
    // compressed chunks with a negative size).
    return (long)(int32_t)(((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[0]);
}

int pack_iputw(int w, PACKFILE *f)
{
    int b1 = (w & 0xFF00) >> 8;
    int b2 = w & 0x00FF;

    if(pack_putc(b2, f) == b2 && pack_putc(b1, f) == b1)
        return w;

    return EOF;
}

long pack_iputl(long l, PACKFILE *f)
{
    unsigned long u = (unsigned long)l;
    int b[4] = { (int)(u & 0xFF), (int)((u >> 8) & 0xFF), (int)((u >> 16) & 0xFF), (int)((u >> 24) & 0xFF) };

    for(int i = 0; i < 4; ++i)
        if(pack_putc(b[i], f) != b[i])
            return EOF;

    return l;
}

int pack_mgetw(PACKFILE *f)
{
    int b1 = pack_getc(f);

    if(b1 == EOF)
        return EOF;

    int b2 = pack_getc(f);

    if(b2 == EOF)
        return EOF;

    return (b1 << 8) | b2;
}

long pack_mgetl(PACKFILE *f)
{
    int b[4];

    for(int i = 0; i < 4; ++i)
        if((b[i] = pack_getc(f)) == EOF)
            return EOF;

    // Signed 32-bit, sign-extended on LP64 (see pack_igetl).
    return (long)(int32_t)(((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3]);
}

int pack_mputw(int w, PACKFILE *f)
{
    int b1 = (w & 0xFF00) >> 8;
    int b2 = w & 0x00FF;

    if(pack_putc(b1, f) == b1 && pack_putc(b2, f) == b2)
        return w;

    return EOF;
}

long pack_mputl(long l, PACKFILE *f)
{
    unsigned long u = (unsigned long)l;
    int b[4] = { (int)((u >> 24) & 0xFF), (int)((u >> 16) & 0xFF), (int)((u >> 8) & 0xFF), (int)(u & 0xFF) };

    for(int i = 0; i < 4; ++i)
        if(pack_putc(b[i], f) != b[i])
            return EOF;

    return l;
}

long pack_fread(void *p, long n, PACKFILE *f)
{
    long got = f->vtable->pf_fread(p, n, f->userdata);
    rec_bytes(f, p, got);
    return got;
}

long pack_fwrite(AL_CONST void *p, long n, PACKFILE *f)
{
    return f->vtable->pf_fwrite(p, n, f->userdata);
}

int pack_ungetc(int c, PACKFILE *f)
{
    int r = f->vtable->pf_ungetc(c, f->userdata);

    if(f == g_rec_file && r != EOF)
        g_rec_fn(g_rec_user, NULL, 1, 1);   // take the last byte back

    return r;
}

char *pack_fgets(char *p, int max, PACKFILE *f)
{
    if(pack_ferror(f))
        return NULL;

    int c = pack_getc(f);

    if(c == EOF)
        return NULL;

    pack_ungetc(c, f);

    int n = 0;
    int room = max - 1;

    for(;;)
    {
        c = pack_getc(f);

        if(c == EOF)
            break;

        if(c == '\r' || c == '\n')
        {
            if(c == '\r')
            {
                int d = pack_getc(f);

                if(d != '\n' && d != EOF)
                    pack_ungetc(d, f);
            }

            p[n] = 0;
            return p;
        }

        if(n >= room)
        {
            /* the line does not fit: put the byte back, report no line */
            pack_ungetc(c, f);

            if(room >= 0)
                p[n] = 0;

            return NULL;
        }

        p[n++] = (char)c;
    }

    if(room >= 0)
        p[n] = 0;

    return p;
}

int pack_fputs(AL_CONST char *p, PACKFILE *f)
{
    for(; *p; ++p)
    {
        if(*p == '\n')
        {
            if(pack_putc('\r', f) == EOF || pack_putc('\n', f) == EOF)
                return -1;
        }
        else if(pack_putc((unsigned char)*p, f) == EOF)
            return -1;
    }

    return pack_ferror(f) ? -1 : 0;
}

void *pack_get_userdata(PACKFILE *f)
{
    return f->userdata;
}

} /* extern "C" */

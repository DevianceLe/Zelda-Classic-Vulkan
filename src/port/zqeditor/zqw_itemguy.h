// What the ITEM and GUY section writers (zqw_items.cpp, zqw_guys.cpp) share: a record
// rebuilt from its original bytes field by field, a section in memory as a file for
// qst.cpp's readers, and the script settings 2.55 stores in its records.

#ifndef ZQW_ITEMGUY_H
#define ZQW_ITEMGUY_H

#include <stdarg.h>
#include <stdio.h>
#include <string>
#include "zq_writers.h"

// A refusal: logged, and kept as the reason the save shows the user (zqw_last_error).
inline void zqw_ig_refuse(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    al_trace("%s", buf);
    std::string r = buf;

    while(!r.empty() && (r[r.size() - 1] == '\n' || r[r.size() - 1] == '.'))
        r.erase(r.size() - 1);

    zqw_last_error = r;
}

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#ifndef _getpid
#define _getpid getpid
#endif
#endif
#include <map>
#include <string>
#include <vector>

#include "zdefs.h"
#include "q30.h"
#include "zq_writers.h"

namespace zqw_ig
{

enum
{
    ERR_CANT_HOLD = 1,      // ZQuest's data has something this version can't hold
    ERR_PARSE = 2,          // the original part isn't laid out the way the reader reads it
    ERR_READER = 3          // the reader refused what was written
};

// How a record is written: as it was, with only its changed fields encoded, or with
// every field encoded from ZQuest's data.
enum { M_COPY, M_MINIMAL, M_DIRECT };

// The ways each record was tried. A record that doesn't come back as ZQuest holds it gets
// the other way, if it has one left: a forced record keeps its unchanged fields after all,
// as a real save does; a changed record takes every field from ZQuest's data, but only
// where the player reads the section with the same reader as ZQuest (qst.cpp's), so the
// stored bytes that differ are ones that reader doesn't use. A record with no original
// can only be encoded whole.
struct Tries
{
    std::vector<int> &mode;
    std::vector<byte> tried;

    explicit Tries(std::vector<int> &m) : mode(m), tried(m.size(), 0)
    {
        for(size_t i = 0; i < m.size(); ++i)
            tried[i] = (byte)(m[i] == M_MINIMAL ? 1 : (m[i] == M_DIRECT ? 2 : 0));
    }

    bool next(int i, bool has_original, bool whole_ok)
    {
        if(i < 0 || i >= (int)mode.size())
            return false;

        if(mode[i] == M_DIRECT && has_original && !(tried[i] & 1))
        {
            mode[i] = M_MINIMAL;
            tried[i] |= 1;
            return true;
        }

        if(mode[i] == M_MINIMAL && whole_ok && !(tried[i] & 2))
        {
            mode[i] = M_DIRECT;
            tried[i] |= 2;
            return true;
        }

        return false;
    }
};

// ZQW_IG_LOG=file: what the writers did, for tracking down a problem.
inline bool logging()
{
    const char *path = getenv("ZQW_IG_LOG");
    return path && *path;
}

// Lines start with the process id, so checks run side by side can share the log; each
// process first names the file it was given.
inline void note(const char *fmt, ...)
{
    if(!logging())
        return;

    FILE *f = fopen(getenv("ZQW_IG_LOG"), "a");

    if(!f)
        return;

    static bool named = false;
    int pid = (int)_getpid();

    if(!named)
    {
#ifdef _WIN32
        fprintf(f, "%d == %s\n", pid, __argc > 2 ? __argv[2] : "?");
#else
        fprintf(f, "%d == ?\n", pid);
#endif
        named = true;
    }

    fprintf(f, "%d ", pid);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fclose(f);
}

// With the log on: where a record's new bytes first differ from the original's.
inline void note_record_change(const char *what, int version, int i, const byte *was, size_t was_n, const byte *now, size_t now_n)
{
    if(!logging())
        return;

    size_t k = 0;

    while(k < was_n && k < now_n && was[k] == now[k])
        ++k;

    if(k == was_n && k == now_n)
        return;

    char a[40], b[40];
    a[0] = b[0] = 0;

    for(size_t j = k; j < k + 8; ++j)
    {
        if(j < was_n)
            sprintf(a + strlen(a), "%02x", was[j]);

        if(j < now_n)
            sprintf(b + strlen(b), "%02x", now[j]);
    }

    note("%s v%d: record %d differs at %u of %u/%u: %s -> %s\n", what, version, i, (unsigned)k, (unsigned)was_n, (unsigned)now_n, a, b);
}

// Every field encoded from ZQuest's data. ZQW_IG_FORCE=1 does it in -qst-rewrite-check too,
// which then shows where the encoding alone doesn't give the original bytes back.
inline bool forced()
{
    static int env = -1;

    if(env < 0)
        env = getenv("ZQW_IG_FORCE") ? 1 : 0;

    return zqw_force_serialize || env == 1;
}

// ---- bytes ----------------------------------------------------------------------------

inline bool read_part(const ZqQuestPart &p, std::vector<byte> &out)
{
    out.resize((size_t)p.length);
    return out.empty() || zq_part_read(p, 0, &out[0], out.size());
}

inline dword get_le(const std::vector<byte> &b, size_t at, int n)
{
    dword v = 0;

    for(int k = 0; k < n; ++k)
        v |= (dword)b[at + k] << (8 * k);

    return v;
}

inline void put_le(std::vector<byte> &b, dword v, int n)
{
    for(int k = 0; k < n; ++k)
        b.push_back((byte)(v >> (8 * k)));
}

inline void set_le(std::vector<byte> &b, size_t at, dword v, int n)
{
    for(int k = 0; k < n; ++k)
        b[at + k] = (byte)(v >> (8 * k));
}

// The whole part, id first.
inline int write_part(PACKFILE *f, dword id, const std::vector<byte> &bytes)
{
    if(pack_mputl((long)id, f) == EOF)
        return ERR_READER;

    if(!bytes.empty() && pack_fwrite(&bytes[0], (long)bytes.size(), f) != (long)bytes.size())
        return ERR_READER;

    return ZQW_OK;
}

// ---- a section held in memory, as a file for qst.cpp's readers ------------------------

struct MemFile
{
    const byte *data;
    size_t size, pos;
    int ungot;
};

inline int mf_fclose(void *) { return 0; }

inline int mf_getc(void *u)
{
    MemFile *m = (MemFile *)u;

    if(m->ungot >= 0)
    {
        int c = m->ungot;
        m->ungot = -1;
        return c;
    }

    return m->pos < m->size ? m->data[m->pos++] : EOF;
}

inline int mf_ungetc(int c, void *u)
{
    ((MemFile *)u)->ungot = c;
    return c;
}

// The packfile callbacks use the system's own long (see zcport/lp64.h).
#pragma push_macro("long")
#undef long
inline long mf_fread(void *p, long n, void *u)
{
    unsigned char *out = (unsigned char *)p;
    long done = 0;

    while(done < n)
    {
        int c = mf_getc(u);

        if(c == EOF)
            break;

        out[done++] = (unsigned char)c;
    }

    return done;
}
inline int mf_putc(int, void *) { return EOF; }
inline long mf_fwrite(const void *, long, void *) { return 0; }
#pragma pop_macro("long")

inline int mf_fseek(void *u, int offset)
{
    MemFile *m = (MemFile *)u;

    if(offset < 0 || m->pos + offset > m->size)
        return -1;

    m->pos += offset;
    return 0;
}

inline int mf_feof(void *u)
{
    MemFile *m = (MemFile *)u;
    return m->ungot < 0 && m->pos >= m->size;
}

inline int mf_ferror(void *) { return 0; }

inline PACKFILE *open_mem(MemFile &m, const std::vector<byte> &bytes)
{
    static PACKFILE_VTABLE vt = { mf_fclose, mf_getc, mf_ungetc, mf_fread, mf_putc, mf_fwrite, mf_fseek, mf_feof, mf_ferror };
    m.data = bytes.empty() ? NULL : &bytes[0];
    m.size = bytes.size();
    m.pos = 0;
    m.ungot = -1;
    return pack_fopen_vtable(&vt, &m);
}

// True when the reader took every byte of it.
inline bool mem_used_up(const MemFile &m)
{
    return m.pos == m.size && m.ungot < 0;
}

// ---- one record, rebuilt from its original bytes --------------------------------------

struct Rec
{
    const byte *in;         // the original's bytes, NULL when the original has no such record
    size_t size, pos;
    bool direct;            // every field takes ZQuest's value
    bool short_read;        // the original ended inside the record
    std::vector<byte> *out;

    Rec(const byte *data, size_t n, size_t at, bool all, std::vector<byte> *o)
        : in(data), size(n), pos(at), direct(all || !data), short_read(false), out(o) {}

    bool have(size_t n) const
    {
        return in && pos <= size && n <= size - pos;
    }

    // The original's value of the next n bytes, 0 when there is none.
    dword peek(int n) const
    {
        if(!have((size_t)n))
            return 0;

        dword v = 0;

        for(int k = 0; k < n; ++k)
            v |= (dword)in[pos + k] << (8 * k);

        return v;
    }

    void advance(size_t n)
    {
        if(!in)
            return;

        if(!have(n))
            short_read = true;

        pos += n;
    }

    void copy(size_t n)
    {
        out->insert(out->end(), in + pos, in + pos + n);
    }

    // A little-endian field n bytes wide: the original's bytes while the value is the one
    // loaded, otherwise ZQuest's value.
    void field(int n, long now, long then)
    {
        if(have((size_t)n) && !direct && now == then)
            copy((size_t)n);
        else
            put_le(*out, (dword)now, n);

        advance((size_t)n);
    }

    // A field that always takes ZQuest's value.
    void put(int n, long now)
    {
        put_le(*out, (dword)now, n);
        advance((size_t)n);
    }

    // Bytes ZQuest has no field for.
    void keep(size_t n)
    {
        if(have(n))
            copy(n);
        else
            out->insert(out->end(), n, (byte)0);

        advance(n);
    }

    // A 64-byte name.
    void name64(const char *now, const char *then)
    {
        if(have(64) && !direct && strcmp(now, then) == 0)
            copy(64);
        else
        {
            char buf[64];
            memset(buf, 0, sizeof(buf));
            strncpy(buf, now, 63);
            out->insert(out->end(), buf, buf + 64);
        }

        advance(64);
    }

    // Text with its length in a byte in front (p_getcstr).
    void cstr(const std::string &now, const std::string &then)
    {
        size_t n = have(1) ? (size_t)in[pos] : 0;

        if(have(1 + n) && !direct && now == then)
            copy(1 + n);
        else
        {
            size_t len = now.size() < 255 ? now.size() : 255;
            out->push_back((byte)len);
            out->insert(out->end(), now.begin(), now.begin() + len);
        }

        advance(1 + n);
    }
};

template<class K, class V> V get_or(const std::map<K, V> &m, K k, V def)
{
    typename std::map<K, V>::const_iterator it = m.find(k);
    return it == m.end() ? def : it->second;
}

inline bool same_ref(const Q30ScriptRef &a, const Q30ScriptRef &b)
{
    return a.script == b.script && memcmp(a.initd, b.initd, sizeof(a.initd)) == 0;
}

// p_getvar(script_config*), as the 2.55 readers read it: the script, a counted array of
// run arguments (at most 8 kept), then a counted map of word keys to long values (the
// instance's init values, kept as they are). The item reader takes no more than 8
// arguments from the file; the enemy reader skips the rest (skip_past_8).
// args_now NULL: the arguments aren't ZQuest's here, so they stay as they are;
// args_then NULL: they weren't ZQuest's at the load.
inline void script_config(Rec &r, long script_now, long script_then, const long *args_now, const long *args_then, bool skip_past_8)
{
    r.field(2, script_now, script_then);
    int argc = r.in ? (int)r.peek(2) : 0;
    int read = argc < 8 ? argc : 8;
    bool mine = args_now && (r.direct || !args_then || memcmp(args_now, args_then, 8 * sizeof(long)) != 0);
    int need = 0;

    if(mine)
        for(int j = 0; j < 8; ++j)
            if(args_now[j])
                need = j + 1;

    if(mine && need > read)
    {
        // more arguments than the original stored: all 8, as upstream writes them
        put_le(*r.out, 8, 2);
        r.advance(2);

        for(int j = 0; j < 8; ++j)
            put_le(*r.out, (dword)args_now[j], 4);

        r.advance(4 * (size_t)read);

        if(skip_past_8 && argc > 8)
            r.advance(4 * (size_t)(argc - 8));
    }
    else
    {
        r.keep(2);

        for(int j = 0; j < read; ++j)
        {
            if(mine)
                r.put(4, args_now[j]);
            else
                r.keep(4);
        }

        if(skip_past_8 && argc > 8)
            r.keep(4 * (size_t)(argc - 8));
    }

    word entries = (word)r.peek(2);
    r.keep(2);
    r.keep((size_t)entries * (2 + 4));
}

// read_weap_data, 2.55's weapon block (in items from v63, in enemies from v54), which carries
// its own version: only its script is ZQuest's.
inline void weapon_block(Rec &r, const Q30ScriptRef &w, bool skip_past_8)
{
    word v = (word)r.peek(2);
    r.keep(2);
    r.keep(2 + 4 + 2);          // flags, moveflags, wflags

    for(int q = 0; q < 5; ++q)  // WPNSPR_MAX
    {
        r.keep(v < 1 ? 1 : 2);  // burn sprite
        r.keep(1);              // light radius

        if(v >= 3)
            r.keep(4);          // light offset
    }

    r.keep(1);                  // glow shape
    r.keep(10 * 4);             // override flags, tile size, hit box and offsets
    r.keep(4);                  // step
    r.keep(1);                  // unblockable
    r.keep(4);                  // timeout
    r.keep(2);                  // imitate_weapon, default_defense
    r.keep(2 + 4);              // lift level, time, height

    if(v < 5)
    {
        r.field(2, w.script, w.script);

        for(int i = 0; i < 8; ++i)
            r.field(4, w.initd[i], w.initd[i]);
    }
    else
        script_config(r, w.script, w.script, w.initd, w.initd, skip_past_8);

    r.keep(2);                  // pierce count

    if(v >= 2)
        r.keep(4 + 4);          // bounce

    if(v >= 4)
        r.keep(4 + 4);          // viewport ranges
}

} // namespace zqw_ig

#endif

// Helpers of the newer-version writers for sounds, tunes and scripts (zqn_media.cpp,
// zqn_scripts.cpp): sections too big to build in memory twice in a 32-bit ZQuest (a quest's
// scripts can come to hundreds of MB in 3.0's layout) are written in two runs of the same code,
// one that only counts the bytes, for the section's size, and one that writes them.

#ifndef ZQN_OUT_H
#define ZQN_OUT_H

#include <string.h>
#include <vector>

#include "zdefs.h"
#include "zq_sections.h"
#include "zq_writers.h"

namespace zqn
{

// The edit test (zqn_edit_wg.cpp, ZQN_WG_EDIT=1): each of these writers calls it first; the
// first call after a load changes ZQuest's subscreens, sounds, tunes and scripts as the user
// would. Nothing happens without the environment variable.
void wg_edit_test_once();

// Where a section's data goes: nowhere (f NULL: only counted) or the file.
struct Out
{
    PACKFILE *f;
    unsigned long long n;       // bytes so far
    bool failed;
    std::vector<byte> pending;

    explicit Out(PACKFILE *file) : f(file), n(0), failed(false) {}
    ~Out() { flush(); }

    void flush()
    {
        if(f && !pending.empty())
        {
            if(pack_fwrite(&pending[0], (long)pending.size(), f) != (long)pending.size())
                failed = true;

            pending.clear();
        }
    }

    void put(const void *p, size_t k)
    {
        n += k;

        if(!f || !k)
            return;

        const byte *b = (const byte *)p;

        if(k >= 65536)
        {
            flush();

            if(pack_fwrite(b, (long)k, f) != (long)k)
                failed = true;

            return;
        }

        pending.insert(pending.end(), b, b + k);

        if(pending.size() >= 65536)
            flush();
    }

    void c(long v) { byte b = (byte)v; put(&b, 1); }
    void w(long v) { byte b[2] = { (byte)v, (byte)(v >> 8) }; put(b, 2); }
    void l(long v) { byte b[4] = { (byte)v, (byte)(v >> 8), (byte)(v >> 16), (byte)(v >> 24) }; put(b, 4); }
    void zeros(size_t k)
    {
        static const byte z[4096] = { 0 };

        while(k)
        {
            size_t m = k < sizeof(z) ? k : sizeof(z);
            put(z, m);
            k -= m;
        }
    }
};

// Writes a section whose data body(out) produces: its id, version, cversion, any extra field
// (2 bytes for FFSC's metadata version, when extra >= 0), size and data. body runs twice.
template<class Body>
int write_section(PACKFILE *f, dword id, word version, word cversion, long extra, Body &body)
{
    Out count(NULL);

    if(!body(count))
        return 1;

    if(count.n > 0x7FFFFFFFULL)
    {
        zqw_last_error = "the section would be bigger than the format can say";
        return 1;
    }

    if(pack_mputl((long)id, f) == EOF)
        return 1;

    Out out(f);
    out.w(version);
    out.w(cversion);

    if(extra >= 0)
        out.w(extra);

    out.l((long)count.n);
    unsigned long long start = out.n;

    if(!body(out))
        return 1;

    out.flush();

    if(out.failed || out.n - start != count.n)
    {
        zqw_last_error = "writing the section failed";
        return 1;
    }

    return ZQW_OK;
}

// A part of the opened file read front to back without holding it whole (zq_part_read, a
// window at a time).
struct PartReader
{
    const ZqQuestPart *part;
    long long pos;
    bool bad;
    std::vector<byte> win;
    long long win_at;

    explicit PartReader(const ZqQuestPart *p) : part(p), pos(0), bad(false), win_at(-1) {}

    bool fill(long long at, size_t k)
    {
        if(win_at >= 0 && at >= win_at && at + (long long)k <= win_at + (long long)win.size())
            return true;

        size_t want = k > (1u << 20) ? k : (1u << 20);
        long long left = part->length - at;

        if(at < 0 || left < (long long)k)
            return false;

        if((long long)want > left)
            want = (size_t)left;

        win.resize(want);

        if(!zq_part_read(*part, at, &win[0], want))
        {
            win_at = -1;
            return false;
        }

        win_at = at;
        return true;
    }

    bool read(void *buf, size_t k)
    {
        if(bad)
            return false;

        if(!k)
            return true;

        if(!fill(pos, k))
        {
            bad = true;
            return false;
        }

        memcpy(buf, &win[(size_t)(pos - win_at)], k);
        pos += (long long)k;
        return true;
    }

    long c() { byte b = 0; read(&b, 1); return b; }
    long w() { byte b[2] = { 0, 0 }; read(b, 2); return b[0] | (b[1] << 8); }
    long l()
    {
        byte b[4] = { 0, 0, 0, 0 };
        read(b, 4);
        return (long)((dword)b[0] | ((dword)b[1] << 8) | ((dword)b[2] << 16) | ((dword)b[3] << 24));
    }
    void skip(long long k)
    {
        if(k < 0 || pos + k > part->length)
            bad = true;
        else
            pos += k;
    }
};

} // namespace zqn

#endif

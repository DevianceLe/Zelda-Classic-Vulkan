// Shared by the section writers in zqw_init.cpp (init data, Link sprites, subscreens, cheats,
// item drop sets, favorites) and zqw_scripts.cpp (scripts). See zq_writers.h for what a
// writer does.
//
// These writers keep every byte nobody changed. A writer walks the original part the way the
// section's reader does, and for every field asks one question: is ZQuest's value for it
// still the value it had right after the load (the snapshot each file takes in its load hook)?
// If so, the field's original bytes are copied, which keeps whatever the loader converted or
// threw away. Only a changed field is encoded from ZQuest's data, in that version's format.
// With zqw_force_serialize every field ZQuest holds is encoded, to test the encoders.

#ifndef ZQW_INIT_H
#define ZQW_INIT_H

#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_writers.h"

namespace zqwe
{

// A writer's result when ZQuest's data has something the part's version can't hold. The
// save stops instead of dropping it; zqwe::why() says what it was.
enum { CANT_HOLD = 41, BAD_PART = 42 };

void set_why(const std::string &s);
const std::string &why();

// The quest's header says 2.53.0/2.53.1 (builds 31 to 33): ZQuest's own 2.53 writers save
// those, as the Allegro ZQuest does, so these writers leave them alone.
bool header_is_253(const ZqPartContext &ctx);

// For testing the writers on changed data, which the round-trip check doesn't do by itself:
// ZQW_E_MUTATE=1 changes some values right after the first load's snapshot (so the parts look
// changed), and ZQW_E_NOFORCE=1 keeps the round trip from forcing every field, so only the
// changed ones are encoded. The round trip then has to come back SAME as well.
bool testing_unforced();
bool mutate_now(bool &done);

struct Buf
{
    std::vector<byte> b;

    void c(long v) { b.push_back((byte)v); }
    void w(long v) { c(v); c(v >> 8); }
    void l(long v) { c(v); c(v >> 8); c(v >> 16); c(v >> 24); }
    void put(const void *p, size_t n)
    {
        const byte *q = (const byte *)p;
        b.insert(b.end(), q, q + n);
    }
    void put(const Buf &o) { b.insert(b.end(), o.b.begin(), o.b.end()); }
    size_t size() const { return b.size(); }
};

// The original part, read whole, and walked like the reader walks it.
struct Src
{
    std::vector<byte> d;
    size_t pos;
    bool bad;           // ran past the end: the reader would have failed here

    Src() : pos(0), bad(false) {}

    bool has(size_t n) const { return !bad && pos <= d.size() && n <= d.size() - pos; }
    size_t left() const { return pos <= d.size() ? d.size() - pos : 0; }
    const byte *at() const { return d.empty() ? NULL : &d[0] + (pos < d.size() ? pos : d.size()); }

    long c() { if(!has(1)) { bad = true; return 0; } return d[pos++]; }
    long w() { if(!has(2)) { bad = true; return 0; } long v = d[pos] | (d[pos + 1] << 8); pos += 2; return v; }
    long ws() { return (short)w(); }
    long l()
    {
        if(!has(4)) { bad = true; return 0; }
        dword v = (dword)d[pos] | ((dword)d[pos + 1] << 8) | ((dword)d[pos + 2] << 16) | ((dword)d[pos + 3] << 24);
        pos += 4;
        return (long)(int)v;
    }
    void skip(size_t n) { if(!has(n)) { bad = true; return; } pos += n; }
};

// The part's own bytes (everything after its id).
bool load_part(const ZqQuestPart &p, Src &s);

// What comes before a section's data: version, cversion, any extra field, the size.
struct Head
{
    word version, cversion;
    size_t extra;           // bytes between the cversion and the size (FFSC v18+ has a word)
    dword size;             // as the file says it
    size_t body;            // where the data starts in the part
};

bool read_head(Src &s, size_t extra, Head &h);

// Writes the part: the id, the head as it was (the size only changes when the data's length
// did, so a size that was wrong in the original stays as it was), then the data.
int emit(PACKFILE *f, dword id, const Src &orig, const Head &h, const Buf &body);

// Field-by-field copying: each call takes the next field of the original and writes either
// its original bytes (ZQuest's value unchanged since the load) or the value encoded.
struct Patch
{
    Src &s;
    Buf &o;
    bool force;
    bool cant;              // a value didn't fit its field
    std::string what;       // the first one that didn't

    Patch(Src &src, Buf &out) : s(src), o(out), force(zqw_force_serialize && !testing_unforced()), cant(false) {}

    void fail(const char *name)
    {
        if(!cant)
            what = name;
        cant = true;
    }

    void copy(size_t n)
    {
        if(!s.has(n))
        {
            s.bad = true;
            return;
        }
        o.put(s.at(), n);
        s.pos += n;
    }

    bool keep(bool same) const { return same && !force; }

    // n original bytes, or the n bytes of enc
    void field(bool same, size_t n, const byte *enc)
    {
        if(keep(same))
            copy(n);
        else if(!s.has(n))
            s.bad = true;
        else
        {
            o.put(enc, n);
            s.pos += n;
        }
    }

    // A value the reader takes as unsigned (u) or signed (s), of 1, 2 or 4 bytes.
    void u8(long cur, long snap, const char *name = "a byte value")
    {
        if(!keep(cur == snap) && (cur < 0 || cur > 255))
            fail(name);
        byte e[1] = { (byte)cur };
        field(cur == snap, 1, e);
    }
    void s8(long cur, long snap, const char *name = "a byte value")
    {
        if(!keep(cur == snap) && (cur < -128 || cur > 127))
            fail(name);
        byte e[1] = { (byte)cur };
        field(cur == snap, 1, e);
    }
    void u16(long cur, long snap, const char *name = "a word value")
    {
        if(!keep(cur == snap) && (cur < 0 || cur > 65535))
            fail(name);
        byte e[2] = { (byte)cur, (byte)(cur >> 8) };
        field(cur == snap, 2, e);
    }
    void s16(long cur, long snap, const char *name = "a word value")
    {
        if(!keep(cur == snap) && (cur < -32768 || cur > 32767))
            fail(name);
        byte e[2] = { (byte)cur, (byte)(cur >> 8) };
        field(cur == snap, 2, e);
    }
    void x32(long cur, long snap)
    {
        byte e[4] = { (byte)cur, (byte)(cur >> 8), (byte)(cur >> 16), (byte)(cur >> 24) };
        field(cur == snap, 4, e);
    }
    // A field whose sameness the caller worked out (a value converted on the load, say).
    void put16(bool same, long enc, const char *name = "a word value")
    {
        if(!keep(same) && (enc < 0 || enc > 65535))
            fail(name);
        byte e[2] = { (byte)enc, (byte)(enc >> 8) };
        field(same, 2, e);
    }
    void put32(bool same, long enc)
    {
        byte e[4] = { (byte)enc, (byte)(enc >> 8), (byte)(enc >> 16), (byte)(enc >> 24) };
        field(same, 4, e);
    }
    void bool8(bool cur, bool snap)
    {
        byte e[1] = { (byte)(cur ? 1 : 0) };
        field(cur == snap, 1, e);
    }
    void bytes(const void *cur, const void *snap, size_t n)
    {
        field(memcmp(cur, snap, n) == 0, n, (const byte *)cur);
    }
};

// What a value no version of the part can hold looks like in the reason given.
inline int cant_hold(const std::string &part, const std::string &what)
{
    set_why(part + ": this version of the format can't hold " + what);
    return CANT_HOLD;
}

// p_getbvec, p_getbmap and the like (upstream's bounded containers, as the 2.55 readers take
// them): a declared size, then, if that isn't 0, whether keys are stored, a count and the
// entries. Decoded so single entries can be changed and the container written again in
// the same shape.
struct Bounded
{
    int size_bytes, key_bytes, value_bytes;     // of the size and count, the keys, the values
    long declared;              // the size as stored
    bool pairs;
    std::vector<long> keys, values;
    size_t start, end;          // where it was in the part

    Bounded() : size_bytes(2), key_bytes(2), value_bytes(1), declared(0), pairs(false), start(0), end(0) {}

    bool read(Src &s, int sz_bytes, int k_bytes, int v_bytes);
    void write(Buf &o) const;
    long get(long key, long dflt) const;
    void set(long key, long value);
};

} // namespace zqwe

#endif

// Saving a quest made in 2.55 as ZC 2.55.17 or 3.0 Nightly (the newer-version save,
// zq_writers.h): the shared part.
//
// A quest made in 2.53.1 or older is written from ZQuest's data (zqn_*.cpp). A 2.55 quest
// holds much more than ZQuest's 2.53 structures do, so its sections start from what the quest's
// own-version save writes for them now (the bytes as loaded, or the section's writer's bytes
// with the user's edits), and are then converted the way the target's loader converts a 2.55
// section of that version, and written in the target's newest format. Upstream's own ZQuest,
// opening the 2.55 quest and saving it, is the reference these writers are checked against.

#ifndef ZQN_255_H
#define ZQN_255_H

#include <string.h>
#include <string>
#include <vector>

#include "zq_writers.h"
#include "zq_tape.h"

// The quest being saved was made in 2.55 or later (its own sections are 2.55's, not 2.53's).
bool zqn255_source(const ZqNewerContext &ctx);

// One section as the quest's own-version save writes it now.
struct Zqn255Section
{
    word version, cversion;
    std::vector<byte> extra;    // RULE's compat rule version (v17+), FFSC's ZASM metadata version
    std::vector<byte> data;     // what follows the size
};

// The section of ctx's part. False when the quest has no such section, or the own-version save
// can't write it as it is now (zqw_last_error then says why, if anything does).
bool zqn255_section(const ZqNewerContext &ctx, Zqn255Section &s);

// The target's newest section version and cversion: what its writer writes.
void zqn255_newest(dword id, int target, word *version, word *cversion);

// Writes a whole section: id, version, cversion, extra, size, data. ZQW_OK, or 1 when the file
// can't be written.
int zqn255_emit(PACKFILE *f, dword id, word version, word cversion, const std::vector<byte> &extra,
                const std::vector<byte> &data);

// The quest rules (100 bytes) as the target holds them after loading the quest (zqn_255_rules).
bool zqn255_rules_loaded(int target, std::vector<byte> &rules);

// The version of the scripts' compiler that last compiled the quest, as its misc data stores it
// (zscript_last_compiled_version; -1 when the section is older than version 11 or missing).
// False when the misc data can't be read (zqn_255_misc.cpp).
bool zqn255_last_compiled(long &value);

struct Zqn255Script
{
    word script;
    long args[8];

    Zqn255Script() : script(0) { memset(args, 0, sizeof(args)); }
};

// 3.0's weapon_data
struct Zqn255Weapon
{
    word flags, wflags;
    dword moveflags;
    word burnsprs[5];
    byte light_rads[5];
    long light_offsets[5];
    byte glow;
    long override_flags, tilew, tileh, hxsz, hysz, hzsz, hxofs, hyofs, xofs, yofs, step;
    byte unblockable;
    long timeout;
    byte imitate, defdef, lift_level, lift_time;
    long lift_height;
    Zqn255Script sc;
    short pierce;
    long bounce_mult, bounce_add, vp_suspend, vp_despawn;

    Zqn255Weapon()
    {
        memset(this, 0, sizeof(*this));
        lift_time = 16;
        lift_height = 80000;
        pierce = -1;
    }
};

// Each item's type and weapon data as 3.0 holds them after loading the quest (zqn_255_items):
// combos lifted with a bomb take the bomb's.
struct Zqn255ItemWeapon
{
    long type;
    Zqn255Weapon wd;
};
bool zqn255_items_loaded(int target, std::vector<Zqn255ItemWeapon> &out);

// 3.0's music list for a 2.55 quest (zqn_255_dmaps.cpp): its entries in order (entry n is
// number n+1), each DMap's music number, each screen's (map * 136 + screen; -1 the DMap's,
// 0 none).
struct Zqn255MusicEntry
{
    std::string name, path;
    byte track;
    short midi;
    long loop_start, loop_end, xfade_in, xfade_out;

    Zqn255MusicEntry() : track(0), midi(0), loop_start(0), loop_end(0), xfade_in(0), xfade_out(0) {}
};

struct Zqn255MusicList
{
    std::vector<Zqn255MusicEntry> music;
    std::vector<word> dmap_music;
    std::vector<long> screen_music;
};

bool zqn255_music_list(int target, Zqn255MusicList &out);

// The region ids a version 21 DMap section holds for each map (64 bytes; empty: none), which
// 3.0 keeps with the maps.
bool zqn255_dmap_regions(int target, std::vector<std::vector<byte> > &regions);

// A DMap's type byte and subscreen numbers.
struct Zqn255DmapSubs
{
    int type;
    int active, passive, overlay, map;
};

// The DMaps' subscreen numbers as 3.0 writes them. dmaps holds every DMap (512) as its DMAP
// section stores it (overlay 0 before DMAP version 19, map -1 before 24; DMaps past the
// section's count as 3.0 holds unread ones). This applies the init data's subscreen set-up
// (init data before version 37), the old subscreens' fix-up for numbers the old engine couldn't
// reach, and validate_subscreens against the lists 3.0 ends up with (the subscreen writer's).
bool zqn255_dmap_subscreens(int target, std::vector<Zqn255DmapSubs> &dmaps);

// Sets a number field of a tape (adding the field if the record's version doesn't hold it), for
// conversions before a rebuild in a newer version.
void zqn255_tape_set(Tape &t, const char *key, int64_t value);

// Lays a (converted) record out again as version v of its schema: every field takes the source
// tape's value by key, or the schema's default.
bool zqn255_tape_rebuild(bool (*schema)(TapeParser &, int), const Tape &src, int v, std::vector<byte> &out);

// Little-endian reading of a section's data; any read past the end sets bad.
struct Zqn255In
{
    const std::vector<byte> &d;
    size_t pos;
    bool bad;

    Zqn255In(const std::vector<byte> &data, size_t at = 0) : d(data), pos(at), bad(false) {}

    uint64_t get(int width)
    {
        if(bad || pos + (size_t)width > d.size())
        {
            bad = true;
            return 0;
        }

        uint64_t v = 0;

        for(int k = 0; k < width; ++k)
            v |= (uint64_t)d[pos + k] << (8 * k);

        pos += width;
        return v;
    }

    byte u8() { return (byte)get(1); }
    word u16() { return (word)get(2); }
    dword u32() { return (dword)get(4); }
    signed char s8() { return (signed char)get(1); }
    short s16() { return (short)get(2); }
    long s32() { return (long)(int)get(4); }

    void skip(size_t n)
    {
        if(bad || pos + n > d.size())
            bad = true;
        else
            pos += n;
    }

    // n raw bytes
    std::vector<byte> bytes(size_t n)
    {
        std::vector<byte> v;

        if(bad || pos + n > d.size())
        {
            bad = true;
            return v;
        }

        v.assign(d.begin() + pos, d.begin() + pos + n);
        pos += n;
        return v;
    }

    bool at_end() const { return !bad && pos == d.size(); }
};

// Little-endian writing.
struct Zqn255Out
{
    std::vector<byte> b;

    void c(int64_t v) { b.push_back((byte)v); }
    void w(int64_t v) { c(v); c(v >> 8); }
    void l(int64_t v) { c(v); c(v >> 8); c(v >> 16); c(v >> 24); }
    void put(const void *p, size_t n) { const byte *q = (const byte *)p; b.insert(b.end(), q, q + n); }
    void put(const std::vector<byte> &v) { b.insert(b.end(), v.begin(), v.end()); }
    void zeros(size_t n) { b.insert(b.end(), n, (byte)0); }
};

#endif

// The misc data (MISC: shops, info shops, warp rings, triforce pieces, the end string and what
// newer versions added) of a quest made in 2.53.1 or older, saved as 2.55.17 (section version
// 16) or 3.0 Nightly (version 23). See zq_writers.h for what a newer-version writer does.
//
// Upstream's reader keeps what 2.53's keeps, and everything newer starts at its defaults:
// no script values, no misc sprites, empty bottle types and bottle shops, the default misc
// sounds and, in 3.0, no save menus.
//
// What 2.53's load leaves that upstream's doesn't: 2.53 reads the misc data over what ZQuest
// held before, so warp ring entries the file doesn't store (the ninth entry of each ring
// before section version 6, rings past the count the file gives) keep whatever the last quest
// had, where upstream's are 0. The writer works out from the original part which entries the
// file stored, and writes 0 for the others unless they were changed since the load.
//
// Shop and info shop names: older editors left what a longer name had held after the
// terminator, and 2.53 keeps those bytes; upstream's conversion writes zeros after the name.
//
// Upstream counts warp rings up to 16 (count_warprings) though it holds 9, so the count and
// the rings past the ninth come from the palette cycles that follow the rings in memory. That
// looks like an upstream bug; the writer does the same, from ZQuest's palette cycles, which
// are upstream's (both readers clear them and read the same ones).

#include "precompiled.h" //always first

#include <string.h>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "zq_writers.h"
#include "zqn_edit_test.h"

extern miscQdata misc;

namespace
{

const int NUM_RINGS = 9;
const int RING_BYTES = 28;      // a warp ring in upstream's memory: 9 words, 9 bytes, 1 byte

struct Buf
{
    std::vector<byte> b;

    void c(long v) { b.push_back((byte)v); }
    void w(long v) { c(v); c(v >> 8); }
    void l(long v) { c(v); c(v >> 8); c(v >> 16); c(v >> 24); }
    void put(const void *p, size_t n) { const byte *q = (const byte *)p; b.insert(b.end(), q, q + n); }
    void fill(size_t n, byte v) { b.insert(b.end(), n, v); }
};

// Which warp ring entries the opened file stored, and the rings right after the load.
struct Loaded
{
    bool dmap[NUM_RINGS][9], scr[NUM_RINGS][9], size[NUM_RINGS];
    warpring rings[NUM_RINGS];
};

Loaded g_loaded;

void mark_all(bool v)
{
    for(int i = 0; i < NUM_RINGS; ++i)
    {
        for(int j = 0; j < 9; ++j)
            g_loaded.dmap[i][j] = g_loaded.scr[i][j] = v;

        g_loaded.size[i] = v;
    }
}

// Walks a sectioned misc part (readmisc, 1.93 and up) as far as the warp rings and the wind
// warps, marking the entries it stores. False if the part is cut short.
bool walk_part(const ZqQuestPart &p)
{
    std::vector<byte> d((size_t)p.length);

    if(d.empty() || !zq_part_read(p, 0, &d[0], d.size()))
        return false;

    size_t pos = 8;     // version, cversion, size
    word v = (word)(d[0] | (d[1] << 8));
#define NEED(n) if(pos + (n) > d.size()) return false
#define WORD(x) NEED(2); x = (word)(d[pos] | (d[pos + 1] << 8)); pos += 2
    word shops, infos, rings, wind;
    WORD(shops);
    pos += (size_t)shops * ((v > 6 ? 32 : 0) + 3 + 6 + (v > 3 ? 3 : 0));
    WORD(infos);
    pos += (size_t)infos * ((v > 6 ? 32 : 0) + 6 + 6);
    WORD(rings);
    int per = v > 5 ? 9 : 8;
    int dmap_bytes = v <= 3 ? 1 : 2;

    for(int i = 0; i < rings; ++i)
    {
        pos += (size_t)per * (dmap_bytes + 1) + 1;

        if(i >= NUM_RINGS)
            continue;

        for(int j = 0; j < per; ++j)
            g_loaded.dmap[i][j] = g_loaded.scr[i][j] = true;

        g_loaded.size[i] = true;
    }

    if(v <= 5)
    {
        WORD(wind);

        for(int i = 0; i < wind && i < 9; ++i)
            g_loaded.dmap[8][i] = g_loaded.scr[8][i] = true;

        if(wind)
            g_loaded.size[8] = true;
    }

    NEED(0);
#undef WORD
#undef NEED
    return true;
}

void note_loaded()
{
    memcpy(g_loaded.rings, misc.warp, sizeof(g_loaded.rings));
    const ZqQuestPart *p = zq_find_part(ID_MISC);

    // a new quest (nothing came from a file), or a 2.55 or newer one (its reader starts from
    // zeros itself, and this writer doesn't take those yet)
    if(zq_quest_file.parts.empty() || zq_quest_file.zelda_version >= 0x255)
        mark_all(true);
    else if(zq_quest_file.zelda_version < 0x193)
    {
        // pre-1.93: eight rings of eight entries, then the nine wind warps in the ninth
        mark_all(true);

        for(int i = 0; i < 8; ++i)
            g_loaded.dmap[i][8] = g_loaded.scr[i][8] = false;
    }
    else
    {
        mark_all(false);

        if(!p || !walk_part(*p))
            mark_all(true);
    }
}

ZqLoadHookRegistration g_hook(note_loaded);

// misc.warp as upstream holds it: an entry the file didn't store is 0, unless it was changed.
void upstream_rings(warpring *out)
{
    for(int i = 0; i < NUM_RINGS; ++i)
    {
        const warpring &now = misc.warp[i], &was = g_loaded.rings[i];
        warpring &o = out[i];

        for(int j = 0; j < 9; ++j)
        {
            o.dmap[j] = (g_loaded.dmap[i][j] || now.dmap[j] != was.dmap[j]) ? now.dmap[j] : 0;
            o.scr[j] = (g_loaded.scr[i][j] || now.scr[j] != was.scr[j]) ? now.scr[j] : 0;
        }

        o.size = (g_loaded.size[i] || now.size != was.size) ? now.size : 0;
    }
}

// Upstream's memory from the warp rings on: 9 rings, then the palette cycles.
void ring_memory(const warpring *rings, std::vector<byte> &mem)
{
    mem.assign((size_t)NUM_RINGS * RING_BYTES + 256 * 3 * 3, 0);

    for(int i = 0; i < NUM_RINGS; ++i)
    {
        byte *r = &mem[(size_t)i * RING_BYTES];

        for(int j = 0; j < 9; ++j)
        {
            r[j * 2] = (byte)rings[i].dmap[j];
            r[j * 2 + 1] = (byte)(rings[i].dmap[j] >> 8);
            r[18 + j] = rings[i].scr[j];
        }

        r[27] = rings[i].size;
    }

    byte *c = &mem[(size_t)NUM_RINGS * RING_BYTES];

    for(int i = 0; i < 256; ++i)
    {
        for(int j = 0; j < 3; ++j)
        {
            *c++ = misc.cycles[i][j].first;
            *c++ = misc.cycles[i][j].count;
            *c++ = misc.cycles[i][j].speed;
        }
    }
}

word ring_dmap(const std::vector<byte> &mem, int i, int j)
{
    const byte *r = &mem[(size_t)i * RING_BYTES];
    return (word)(r[j * 2] | (r[j * 2 + 1] << 8));
}

byte ring_scr(const std::vector<byte> &mem, int i, int j)
{
    return mem[(size_t)i * RING_BYTES + 18 + j];
}

// count_shops, count_infos, count_warprings (qst.cpp)
int count_shops()
{
    for(int i = 255; i >= 0; --i)
    {
        const shoptype &s = misc.shop[i];

        if(s.name[0])
            return i + 1;

        for(int j = 2; j >= 0; --j)
            if(s.hasitem[j] || s.price[j])
                return i + 1;
    }

    return 0;
}

int count_infos()
{
    for(int i = 255; i >= 0; --i)
    {
        const infotype &s = misc.info[i];

        if(s.name[0])
            return i + 1;

        for(int j = 2; j >= 0; --j)
            if(s.str[j] || s.price[j])
                return i + 1;
    }

    return 0;
}

int count_rings(const std::vector<byte> &mem)
{
    for(int i = 15; i >= 0; --i)
        for(int j = 7; j >= 0; --j)
            if(ring_dmap(mem, i, j) || ring_scr(mem, i, j))
                return i + 1;

    return 0;
}

// The misc sounds of a quest older than misc version 14 (readmisc): grass cut, low hearts,
// Link hurt, hammer pound, subscreen item assigned, subscreen cursor, refill, drain, tap
// and hollow tap.
void misc_sounds(word *sfx)
{
    memset(sfx, 0, 256 * sizeof(word));
    sfx[0] = 41;    // WAV_ZN1GRASSCUT
    sfx[2] = 12;    // WAV_ER
    sfx[4] = 19;    // WAV_OUCH
    sfx[5] = 42;    // WAV_ZN1HAMMERPOST
    sfx[6] = 21;    // WAV_PLACE
    sfx[7] = 5;     // WAV_CHIME
    sfx[8] = 18;    // WAV_MSG
    sfx[9] = 18;
    sfx[10] = 57;   // WAV_ZN1TAP
    sfx[11] = 58;   // WAV_ZN1TAP2
}

// A shop's or info shop's name: 32 bytes. Older editors left what an earlier, longer name had
// after the terminator; upstream's conversion writes the name and zeros after it.
void put_name(Buf &o, const char *name)
{
    size_t n = 0;

    while(n < 32 && name[n])
        ++n;

    o.put(name, n);
    o.fill(32 - n, 0);
}

void body(Buf &o, int target)
{
    bool v30 = target == ZQ_NEWER_30;
    int shops = count_shops();
    o.w(shops);

    for(int i = 0; i < shops; ++i)
    {
        const shoptype &s = misc.shop[i];
        put_name(o, s.name);

        for(int j = 0; j < 3; ++j)
        {
            if(v30)
                o.w(s.item[j]);
            else
                o.c(s.item[j]);
        }

        for(int j = 0; j < 3; ++j)
            o.w(s.price[j]);

        for(int j = 0; j < 3; ++j)
            o.c(s.hasitem[j]);
    }

    int infos = count_infos();
    o.w(infos);

    for(int i = 0; i < infos; ++i)
    {
        const infotype &s = misc.info[i];
        put_name(o, s.name);

        for(int j = 0; j < 3; ++j)
            o.w(s.str[j]);

        for(int j = 0; j < 3; ++j)
            o.w(s.price[j]);
    }

    warpring rings[NUM_RINGS];
    upstream_rings(rings);
    std::vector<byte> mem;
    ring_memory(rings, mem);
    int count = count_rings(mem);
    o.w(count);

    for(int i = 0; i < count; ++i)
    {
        for(int j = 0; j < 9; ++j)
            o.w(ring_dmap(mem, i, j));

        for(int j = 0; j < 9; ++j)
            o.c(ring_scr(mem, i, j));

        o.c(mem[(size_t)i * RING_BYTES + 27]);
    }

    o.put(misc.triforce, 8);
    o.w(misc.endstring);
    o.fill((size_t)shops * 3 * 2, 0);       // shop item strings
    o.fill(32 * 4, 0);                      // script values
    o.fill(32 * 128, 0);                    // their names
    o.l(-1);                                // the ZScript version scripts were last compiled with
    o.fill(v30 ? 512 : 256, 0);             // misc sprites

    for(int q = 0; q < 64; ++q)             // bottle types: no counters
    {
        o.fill(32, 0);

        for(int j = 0; j < 3; ++j)
        {
            o.c(0xFF);
            o.w(0);
        }

        o.c(0);
        o.c(0);
    }

    o.fill(256 * (32 + 3 * 8), 0);          // bottle shops
    word sfx[256];
    misc_sounds(sfx);

    for(int q = 0; q < 256; ++q)
    {
        if(v30)
            o.w(sfx[q]);
        else
            o.c(sfx[q]);
    }

    if(v30)
    {
        o.c(0);     // save menus
        o.c(0);     // the game over and F6 menus
        o.c(0);
    }
}

int zqn_misc(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    zqn_edit_test();
    Buf o;
    body(o, ctx.target);
    word version = ctx.target == ZQ_NEWER_255 ? 16 : 23;
    word cversion = ctx.target == ZQ_NEWER_255 ? 7 : version;

    if(!p_mputl(ID_MISC, f) || !p_iputw(version, f) || !p_iputw(cversion, f) || !p_iputl((long)o.b.size(), f))
        return 1;

    if(!o.b.empty() && !pfwrite(&o.b[0], (long)o.b.size(), f))
        return 1;

    return ZQW_OK;
}

ZqNewerRegistration g_reg(ID_MISC, zqn_misc, "misc data for 2.55.17 and 3.0");

} // namespace

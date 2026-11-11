// Saving the map screens (MAP) and DMaps (DMAP) of a quest made in 2.53.1 or older as 2.55.17 or
// 3.0 Nightly: the newer-version save of zq_writers.h.
//
// The reference is upstream's own ZQuest opening the original file and saving it. Upstream's
// readers convert old screens and DMaps as they read them, and not always the way ZQuest's 2.53
// loader does (2.53 renumbers two 1.92 warp types upstream keeps, upstream clears 1.90 enemies
// past 76, works out who a screen's guy is from the rules, and so on). So a record nobody changed
// is read again from the original file's bytes, with a port of upstream's reader for that file's
// version (read_screen, read_dmap), and gets upstream's conversions after loading too.
//
// When the user changed screens or DMaps, the bytes read are those the quest's own-format save
// would write now (its own writers, zq_write_part): an edited quest saved in its own version and
// then opened by upstream is what saving the same edit as a newer version must match, and
// upstream reads such a file with that version's conversions, changed fields included.
//
// Only when the quest's own version can't hold the edit (its writer refuses) is a changed record
// read from ZQuest's data, as upstream would read it from a 2.53.1 quest. Each of its fields the
// user didn't touch (the 2.53.1 reading of it now is what it was at load) still takes upstream's
// value for the original record, so an edit changes only what was edited.
//
// Rules this depends on, as upstream has them after loading an old quest (agent wA owns RULE):
// - qr_NOFAIRYGUYFIRES: a screen's guy fires flag (room flags). Upstream switches it on for
//   quests before 2.11 build 18, as 2.53 does, so ZQuest's rules hold upstream's value.
// - qr_ENABLEMAGIC: the passive subscreen a DMap gets when the quest has no custom subscreens.
// - qr_OLD_FFC_FUNCTIONALITY: upstream's compat rules switch it on for every quest this reads
//   (compat rule version 0 < 34), so every valid screen is written with 32 FFC slots.
// - old rule 14 as the file holds it (deprecated_rules): level-0 DMaps before DMap version 7
//   get the "view map" flag.
//
// 3.0 keeps music as "advanced music" (the AMUS section, agent wA's): reading an old quest makes
// one for each DMap with music, in DMap order ("DMap n"), then one for each MIDI the screens use,
// in screen order ("MIDI n"). The DMaps' and screens' music fields are indices into that list;
// zqn_music_entries() gives the list itself, worked out the same way.

#include "precompiled.h" //always first
#include <stdint.h>

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "sfx.h"
#include "subscr.h"
#include "zq_writers.h"
#include "zqn_maps.h"

extern dmap *DMaps;
extern std::vector<mapscr> TheMaps;
extern word map_count;
extern newcombo *combobuf;
extern zinitdata zinit;
extern byte quest_rules[QUESTRULES_SIZE];
extern subscreen_group custom_subscreen[MAXCUSTOMSUBSCREENS];
bool setMapCount2(int c);   // zq_class.cpp: the map count setting (the edit test uses it)

namespace
{

// ---- bytes ------------------------------------------------------------------------------

struct In
{
    const byte *d;
    size_t n, pos;
    bool bad;

    In(const std::vector<byte> &v, size_t at) : d(v.empty() ? NULL : &v[0]), n(v.size()), pos(at), bad(false) {}
    In(const byte *p, size_t size) : d(p), n(size), pos(0), bad(false) {}
    bool has(size_t k) const { return !bad && pos <= n && k <= n - pos; }
    byte g8()
    {
        if(!has(1)) { bad = true; return 0; }
        return d[pos++];
    }
    word g16()
    {
        word lo = g8();
        return (word)(lo | (g8() << 8));
    }
    dword g32()
    {
        dword lo = g16();
        return lo | ((dword)g16() << 16);
    }
    void skip(size_t k)
    {
        if(!has(k)) { bad = true; return; }
        pos += k;
    }
    void take(void *dst, size_t k)
    {
        if(!has(k)) { bad = true; memset(dst, 0, k); return; }
        memcpy(dst, d + pos, k);
        pos += k;
    }
};

struct Out
{
    std::vector<byte> b;

    void p8(byte v) { b.push_back(v); }
    void p16(word v) { p8((byte)v); p8((byte)(v >> 8)); }
    void p32(dword v) { p16((word)v); p16((word)(v >> 16)); }
    void pn(const void *p, size_t n)
    {
        const byte *s = (const byte *)p;
        b.insert(b.end(), s, s + n);
    }
    void zeros(size_t n) { b.insert(b.end(), n, 0); }
};

enum { ERR_READ = 1, ERR_WRITE = 2 };

int refuse(int code, const char *why)
{
    zqw_last_error = why;
    return code;
}

// One whole section: its id, version, cversion, size and data, as upstream's writers write it.
int put_section(PACKFILE *f, dword id, word version, word cversion, const Out &data)
{
    Out h;
    h.p8((byte)(id >> 24));
    h.p8((byte)(id >> 16));
    h.p8((byte)(id >> 8));
    h.p8((byte)id);
    h.p16(version);
    h.p16(cversion);
    h.p32((dword)data.b.size());

    if(pack_fwrite(&h.b[0], (long)h.b.size(), f) != (long)h.b.size())
        return refuse(ERR_WRITE, "the file couldn't be written");

    if(!data.b.empty() && pack_fwrite(&data.b[0], (long)data.b.size(), f) != (long)data.b.size())
        return refuse(ERR_WRITE, "the file couldn't be written");

    return ZQW_OK;
}

bool part_bytes(const ZqQuestPart &p, std::vector<byte> &out)
{
    out.resize((size_t)p.length);
    return p.length == 0 || zq_part_read(p, 0, &out[0], (size_t)p.length);
}

int part_index(dword id)
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == id)
            return (int)i;

    return -1;
}

uint64_t fnv(const std::vector<byte> &b)
{
    uint64_t h = 14695981039346656037ULL;

    for(size_t i = 0; i < b.size(); ++i)
    {
        h ^= b[i];
        h *= 1099511628211ULL;
    }

    return h;
}

// The quest file's header version tests the readers make.
bool hdr_before(word hv, byte hb, word v, int b) { return hv < v || (hv == v && hb < b); }
bool hdr_after(word hv, byte hb, word v, int b) { return hv > v || (hv == v && hb > b); }

// ---- upstream's values ----------------------------------------------------------------

const int SQUARES = 176;
const int OLD_FFCS = 32;            // every version read here has 32 FFC slots a screen
const int U_MAXGUYS = 512;

const byte U_DNONE = 16, U_DWALL = 0, U_WTSCROLL = 3;
const word U_RFL_ALWAYS_GUY = 0x0001, U_RFL_GUYFIRES = 0x0002;

// 3.0's screen state bits (core/mapscr.h): what an old screen's "no reset" and "no carry over"
// get added when it is read
const dword U_DOOR_UP = 0x1, U_DOOR_DOWN = 0x2, U_DOOR_LEFT = 0x4, U_DOOR_RIGHT = 0x8, U_NEVERRET = 0x40,
            U_TMPNORET = 0x80, U_VISITED = 0x4000, U_LIGHTBEAM = 0x8000, U_NO_ENEMIES_RETURN = 0x10000;
const dword U_DEF_NORESET = U_TMPNORET | U_VISITED;
const dword U_DEF_NOCARRY = U_TMPNORET | U_VISITED | U_DOOR_UP | U_DOOR_DOWN | U_DOOR_LEFT | U_DOOR_RIGHT | U_NEVERRET |
                            U_NO_ENEMIES_RETURN;

const dword U_FFC_CHANGER = 0x20, U_FFC_IGNOREHOLDUP = 0x400;
const long U_DMF_CAVES4 = 0x1 | 0x2 | 0x4 | 0x8;     // caves, 3 stairs, whirlwind, guy caves
const long U_DMF_VIEWMAP = 0x100, U_DMF_MINIMAPCOLORFIX = 0x400;
const long U_DMF_CUSTOM_GRAVITY = 0x800000, U_DMF_CUSTOM_VIEWPORT = 0x1000000;   // 3.0

// ---- a screen as upstream holds it --------------------------------------------------------

struct UFfc
{
    word data;
    byte cset;
    word delay;
    long x, y, vx, vy, ax, ay;      // zfix: the value times 10000
    byte link;
    long hit_width, hit_height;
    byte txsz, tysz;
    dword flags;
    word script;
    long initd[8];
    long inita[2];                  // 2.55 only
};

struct UScr
{
    byte valid, guy;
    long guytile;
    byte guycs;
    word roomflags, str;
    byte room;
    word item;
    byte hasitem;
    byte tilewarptype[4];
    byte tilewarpoverlayflags;
    word door_combo_set;
    byte warpreturnx[4], warpreturny[4];
    word warpreturnc;
    byte stairx, stairy, itemx, itemy;
    word color;
    byte door[4];
    word tilewarpdmap[4];
    byte tilewarpscr[4];
    byte exitdir;
    word enemy[10];
    byte pattern;
    byte sidewarptype[4];
    byte sidewarpoverlayflags, warparrivalx, warparrivaly;
    byte path[4];
    byte sidewarpscr[4];
    word sidewarpdmap[4];
    byte sidewarpindex;
    word undercombo;
    byte undercset;
    word catchall;
    byte flags[11];                 // flags .. flags10, flags11 (2.53's enemy flags)
    byte csensitive;
    dword noreset, nocarry;
    byte layermap[6], layerscreen[6], layeropacity[6];
    word timedwarptics;
    byte nextmap, nextscr;
    word secretcombo[128];
    byte secretcset[128], secretflag[128];
    word data[SQUARES];
    byte sflag[SQUARES], cset[SQUARES];
    UFfc ffc[OLD_FFCS];
    word oceansfx, bosssfx, secretsfx, holdupsfx;
    short screen_midi;              // as the file has it; 3.0 makes it an index into its music
    byte lens_layer;
    byte old_cpage;                 // a pre-1.93 screen's combo page
};

// A fresh screen: mapscr's defaults (2.55.17 base/mapscr.h, 3.0 core/mapscr.h).
void blank_screen(UScr &s, int target)
{
    memset(&s, 0, sizeof(s));
    s.guytile = -1;

    for(int k = 0; k < 4; ++k)
    {
        s.door[k] = U_DNONE;
        s.sidewarptype[k] = U_WTSCROLL;
    }

    s.csensitive = 1;
    s.noreset = target == ZQ_NEWER_30 ? U_DEF_NORESET : 0;
    s.nocarry = target == ZQ_NEWER_30 ? U_DEF_NOCARRY : 0;

    for(int k = 0; k < 6; ++k)
        s.layeropacity[k] = 255;

    s.secretsfx = WAV_SECRET;
    s.holdupsfx = WAV_PICKUP;
    s.screen_midi = -1;

    for(int k = 0; k < OLD_FFCS; ++k)
        s.ffc[k].txsz = s.ffc[k].tysz = 1;
}

// readmapscreen_old (2.55.17 qst.cpp, 3.0 core/qst_maps.cpp): one screen of a quest with this
// header and map section version. The two upstreams differ in the "no reset" and "no carry"
// states and in what an FFC keeps; *old_ffc is set when an FFC is read from a map section of
// version 11 or older (upstream's fixffcs).
void read_screen(In &in, word hv, byte hb, word ver, int target, bool nofairyfires, UScr &s, bool *old_ffc)
{
    blank_screen(s, target);
    const bool b7 = hdr_after(hv, hb, 0x211, 7);
    byte tb;

    s.valid = in.g8();
    s.guy = in.g8();
    s.guytile = -1;

    if(s.guy == gFAIRY)
        s.roomflags |= U_RFL_ALWAYS_GUY;
    else
        s.roomflags &= ~U_RFL_ALWAYS_GUY;

    if(s.guy != gFAIRY || !nofairyfires)
        s.roomflags |= U_RFL_GUYFIRES;
    else
        s.roomflags &= ~U_RFL_GUYFIRES;

    s.str = hdr_before(hv, hb, 0x192, 146) ? (word)in.g8() : in.g16();
    s.room = in.g8();
    s.item = in.g8();

    if(hdr_before(hv, hb, 0x211, 14))
        s.hasitem = s.item != 0 ? 1 : 0;
    else
        s.hasitem = in.g8();

    if(hdr_before(hv, hb, 0x192, 154))
        in.skip(1);

    s.tilewarptype[0] = in.g8();

    if(hv < 0x193)
        in.skip(1);

    for(int i = 1; i < 4; ++i)
        s.tilewarptype[i] = b7 ? in.g8() : 0;

    if(hdr_after(hv, hb, 0x192, 153))
        s.door_combo_set = in.g16();

    s.warpreturnx[0] = in.g8();

    for(int i = 1; i < 4; ++i)
        s.warpreturnx[i] = b7 ? in.g8() : 0;

    s.warpreturny[0] = in.g8();

    for(int i = 1; i < 4; ++i)
        s.warpreturny[i] = b7 ? in.g8() : 0;

    if(b7)
    {
        if(ver >= 18)
            s.warpreturnc = in.g16();
        else
        {
            tb = in.g8();
            s.warpreturnc = (word)(tb << 8 | tb);
        }
    }

    s.stairx = in.g8();
    s.stairy = in.g8();
    s.itemx = in.g8();
    s.itemy = in.g8();
    s.color = ver > 15 ? in.g16() : (word)in.g8();
    s.flags[10] = in.g8();

    for(int k = 0; k < 4; ++k)
        s.door[k] = in.g8();

    if(ver <= 11)
    {
        s.tilewarpdmap[0] = in.g8();

        for(int i = 1; i < 4; ++i)
            s.tilewarpdmap[i] = b7 ? (word)in.g8() : 0;
    }
    else
        for(int i = 0; i < 4; ++i)
            s.tilewarpdmap[i] = in.g16();

    s.tilewarpscr[0] = in.g8();

    for(int i = 1; i < 4; ++i)
        s.tilewarpscr[i] = b7 ? in.g8() : 0;

    s.tilewarpoverlayflags = ver >= 15 ? in.g8() : 0;
    s.exitdir = in.g8();

    if(hv < 0x193)
        in.skip(1);

    if(hv == 0x192 && hb > 145 && hb < 154)
        in.skip(1);

    for(int k = 0; k < 10; ++k)
    {
        int e;

        if(hdr_before(hv, hb, 0x192, 10))
        {
            e = in.g8();

            // 76 is the highest enemy 1.90 could set; upstream clears anything past it
            if(hv <= 0x190 && e > 76)
                e = 0;
        }
        else
            e = in.g16();

        if(hdr_before(hv, hb, 0x192, 108))
        {
            if(e >= 57)
                e += 5;
            else if(e >= 52)
                e += 1;
        }

        if(ver < 9 && e > 0)
            e += 10;

        s.enemy[k] = (word)e;

        if((unsigned)s.enemy[k] > (unsigned)U_MAXGUYS)
            s.enemy[k] = 0;
    }

    s.pattern = in.g8();
    s.sidewarptype[0] = in.g8();

    for(int i = 1; i < 4; ++i)
        s.sidewarptype[i] = b7 ? in.g8() : 0;

    s.sidewarpoverlayflags = ver >= 15 ? in.g8() : 0;
    s.warparrivalx = in.g8();
    s.warparrivaly = in.g8();

    for(int k = 0; k < 4; ++k)
        s.path[k] = in.g8();

    s.sidewarpscr[0] = in.g8();

    for(int i = 1; i < 4; ++i)
        s.sidewarpscr[i] = b7 ? in.g8() : 0;

    if(ver <= 11)
    {
        s.sidewarpdmap[0] = in.g8();

        for(int i = 1; i < 4; ++i)
            s.sidewarpdmap[i] = b7 ? (word)in.g8() : 0;
    }
    else
        for(int i = 0; i < 4; ++i)
            s.sidewarpdmap[i] = in.g16();

    s.sidewarpindex = b7 ? in.g8() : 0;
    s.undercombo = in.g16();

    if(hv < 0x193)
        s.old_cpage = in.g8();

    s.undercset = in.g8();
    s.catchall = in.g16();
    s.flags[0] = in.g8();
    s.flags[1] = in.g8();
    s.flags[2] = in.g8();

    if(hdr_after(hv, hb, 0x211, 1))
        s.flags[3] = in.g8();

    if(b7)
    {
        s.flags[4] = in.g8();
        dword nr = in.g16(), nc = in.g16();

        if(target == ZQ_NEWER_30)
        {
            nr |= U_LIGHTBEAM | U_TMPNORET | U_VISITED;
            nc |= U_LIGHTBEAM | U_TMPNORET | U_VISITED | U_DOOR_UP | U_DOOR_DOWN | U_DOOR_LEFT | U_DOOR_RIGHT |
                  U_NEVERRET | U_NO_ENEMIES_RETURN;
        }

        s.noreset = nr;
        s.nocarry = nc;

        if(s.flags[4] & 32)
        {
            s.flags[4] &= ~32;
            s.noreset |= 48;
        }

        // in 3.0 the "no carry" bit tested here is always set by now (door up): matched
        if(ver < 8)
        {
            if(s.noreset & 1)
                s.noreset |= 8192;

            if(s.nocarry & 1)
            {
                s.nocarry |= 8192;
                s.nocarry &= ~1;
            }
        }
    }
    else
    {
        s.flags[4] = 0;

        if(target == ZQ_NEWER_30)
        {
            s.noreset = U_LIGHTBEAM | U_TMPNORET | U_VISITED;
            s.nocarry = U_LIGHTBEAM | U_TMPNORET | U_VISITED | U_DOOR_UP | U_DOOR_DOWN | U_DOOR_LEFT | U_DOOR_RIGHT |
                        U_NEVERRET | U_NO_ENEMIES_RETURN;
        }
        else
            s.noreset = s.nocarry = 0;
    }

    if(hdr_after(hv, hb, 0x211, 9))
        s.flags[5] = in.g8();

    if(ver > 5)
    {
        for(int k = 6; k < 10; ++k)
            s.flags[k] = in.g8();

        s.csensitive = in.g8();
    }
    else
        s.csensitive = 1;

    if(ver < 14)
    {
        if(s.flags[0] & 8)
            s.bosssfx = (s.flags[2] & 2) ? WAV_DODONGO : (s.flags[1] & 32) ? WAV_VADER : WAV_ROAR;

        if(s.flags[0] & 128)
            s.oceansfx = WAV_SEA;

        s.secretsfx = (s.flags[2] & 64) ? 0 : WAV_SECRET;
        s.flags[2] &= ~66;
        s.flags[1] &= ~32;
        s.flags[0] &= ~136;
    }
    else
    {
        s.oceansfx = in.g8();
        s.bosssfx = in.g8();
        s.secretsfx = in.g8();
    }

    s.holdupsfx = ver < 15 ? (word)WAV_PICKUP : (word)in.g8();

    if(hdr_after(hv, hb, 0x192, 97))
    {
        for(int k = 0; k < 6; ++k) s.layermap[k] = in.g8();
        for(int k = 0; k < 6; ++k) s.layerscreen[k] = in.g8();
    }
    else if(hv == 0x192 && hb > 23 && hb < 98)
    {
        s.layermap[2] = in.g8();
        s.layerscreen[2] = in.g8();
        s.layermap[4] = in.g8();
        s.layerscreen[4] = in.g8();
    }

    if(hv == 0x192 && hb > 149)
        in.skip(36);        // layer sizes, speeds and delays

    if(hdr_after(hv, hb, 0x192, 149))
        for(int k = 0; k < 6; ++k)
            s.layeropacity[k] = in.g8();

    if(hdr_after(hv, hb, 0x192, 153))
    {
        if(hv == 0x192 && hb > 153)
            in.skip(1);

        s.timedwarptics = in.g16();
    }

    int extras = 0;

    if(hdr_before(hv, hb, 0x192, 24))
        extras = 15;
    else if(hv == 0x192 && hb < 98)
        extras = 11;
    else if(hv == 0x192 && hb < 150)
        extras = 32;
    else if(hv == 0x192 && hb < 154)
        extras = 64;
    else if(hv < 0x193)
        extras = 62;

    in.skip(extras);

    if(hdr_after(hv, hb, 0x211, 2))
    {
        s.nextmap = in.g8();
        s.nextscr = in.g8();
    }

    int secretcombos = 128;

    if(hdr_before(hv, hb, 0x192, 137))
        secretcombos = 20;
    else if(hv == 0x192 && hb < 154)
        secretcombos = 256;

    if(hdr_before(hv, hb, 0x192, 154))
    {
        for(int k = 0; k < secretcombos; ++k)
        {
            tb = in.g8();

            if(k < 128)
                s.secretcombo[k] = tb;
        }
    }
    else
        for(int k = 0; k < 128; ++k)
            s.secretcombo[k] = in.g16();

    if(hdr_after(hv, hb, 0x192, 153))
    {
        for(int k = 0; k < 128; ++k) s.secretcset[k] = in.g8();
        for(int k = 0; k < 128; ++k) s.secretflag[k] = in.g8();
    }

    if(hv == 0x192 && hb > 97 && hb < 154)
        in.skip(1);

    for(int k = 0; k < SQUARES; ++k)
        s.data[k] = in.g16();

    if(hv == 0x192 && hb > 20 && hb < 24)
        in.skip(2);

    if(hdr_after(hv, hb, 0x192, 20))
        for(int k = 0; k < SQUARES; ++k)
        {
            s.sflag[k] = in.g8();

            if(hv == 0x192 && hb < 24)
                in.skip(3);
        }

    if(hdr_after(hv, hb, 0x192, 97))
        for(int k = 0; k < SQUARES; ++k)
            s.cset[k] = in.g8();

    if(hdr_before(hv, hb, 0x192, 154))
    {
        s.undercset = (s.undercombo >> 8) & 7;
        s.undercombo = (word)((s.undercombo & 0xFF) + (s.old_cpage << 8));
    }

    if(hdr_before(hv, hb, 0x192, 137))
    {
        s.secretcombo[sSBOMB] = s.secretcombo[sBOMB];
        s.secretcombo[sRCANDLE] = s.secretcombo[sBCANDLE];
        s.secretcombo[sWANDFIRE] = s.secretcombo[sBCANDLE];
        s.secretcombo[sDINSFIRE] = s.secretcombo[sBCANDLE];
        s.secretcombo[sSARROW] = s.secretcombo[sARROW];
        s.secretcombo[sGARROW] = s.secretcombo[sARROW];
    }

    if(hdr_before(hv, hb, 0x192, 154))
    {
        for(int k = 0; k < SQUARES; ++k)
        {
            if(hv == 0x192 && hb > 149)
            {
                if(hb != 153)
                    s.cset[k] = (s.data[k] >> 8) & 7;
            }
            else
            {
                if(hdr_before(hv, hb, 0x192, 21))
                    s.sflag[k] = (byte)(s.data[k] >> 11);

                s.cset[k] = (s.data[k] >> 8) & 7;
            }

            s.data[k] = (word)((s.data[k] & 0xFF) + (s.old_cpage << 8));
        }
    }

    s.screen_midi = ver > 4 ? (short)in.g16() : (short)-1;
    s.lens_layer = ver >= 17 ? in.g8() : 0;

    if(ver > 6)
    {
        dword bits = in.g32();

        for(int m = 0; m < OLD_FFCS; ++m)
        {
            UFfc &c = s.ffc[m];
            memset(&c, 0, sizeof(c));
            c.txsz = c.tysz = 1;

            if(!((bits >> m) & 1))
                continue;

            c.data = in.g16();
            c.cset = in.g8();
            c.delay = in.g16();
            long *pos[6] = { &c.x, &c.y, &c.vx, &c.vy, &c.ax, &c.ay };

            for(int q = 0; q < 6; ++q)
            {
                if(ver < 9)
                {
                    // p_igetf_DO_NOT_USE: a float with its bytes the other way round
                    byte raw[4], rev[4];
                    in.take(raw, 4);

                    for(int i = 0; i < 4; ++i)
                        rev[3 - i] = raw[i];

                    float fv;
                    memcpy(&fv, rev, 4);
                    *pos[q] = (long)(int)(fv * 10000);
                }
                else
                    *pos[q] = (long)in.g32();
            }

            c.link = in.g8();

            if(ver > 7)
            {
                tb = in.g8();
                c.hit_width = (tb & 0x3F) + 1;
                c.txsz = (byte)((tb >> 6) + 1);
                tb = in.g8();
                c.hit_height = (tb & 0x3F) + 1;
                c.tysz = (byte)((tb >> 6) + 1);
                c.flags = in.g32();
            }
            else
            {
                c.hit_width = c.hit_height = 16;
                c.txsz = c.tysz = 1;
                c.flags = 0;
            }

            if(hv == 0x211 || (hv == 0x250 && hb < 20))
                c.flags |= U_FFC_IGNOREHOLDUP;

            c.script = ver > 9 ? in.g16() : 0;

            if(ver > 10)
            {
                for(int q = 0; q < 8; ++q)
                    c.initd[q] = (long)in.g32();

                c.inita[0] = in.g8() * 10000;
                c.inita[1] = in.g8() * 10000;
            }
            else
                c.inita[0] = c.inita[1] = 10000;

            if(target == ZQ_NEWER_30)
                c.inita[0] = c.inita[1] = 0;     // 3.0 has no InitA

            if(ver <= 11 && old_ffc)
                *old_ffc = true;
        }
    }

    // the whistle's new flags
    if(ver < 13 && (s.flags[0] & fWHISTLE))
        s.flags[6] |= fWHISTLEPAL | fWHISTLEWATER;

    for(int k = 0; k < 4; ++k)
        if(s.door[k] == U_DNONE)
            s.door[k] = U_DWALL;
}

// ---- ZQuest's screens, read as upstream reads a 2.53.1 quest ------------------------------

// A screen in 2.53's own form (writemapscreen, map section version 18), which holds all of it.
void encode_253(Out &o, const mapscr &m)
{
    o.p8(m.valid);
    o.p8(m.guy);
    o.p16(m.str);
    o.p8(m.room);
    o.p8(m.item);
    o.p8(m.hasitem);
    o.pn(m.tilewarptype, 4);
    o.p16(m.door_combo_set);
    o.pn(m.warpreturnx, 4);
    o.pn(m.warpreturny, 4);
    o.p16(m.warpreturnc);
    o.p8(m.stairx);
    o.p8(m.stairy);
    o.p8(m.itemx);
    o.p8(m.itemy);
    o.p16(m.color);
    o.p8(m.enemyflags);
    o.pn(m.door, 4);
    for(int i = 0; i < 4; ++i) o.p16(m.tilewarpdmap[i]);
    o.pn(m.tilewarpscr, 4);
    o.p8(m.tilewarpoverlayflags);
    o.p8(m.exitdir);
    for(int k = 0; k < 10; ++k) o.p16(m.enemy[k]);
    o.p8(m.pattern);
    o.pn(m.sidewarptype, 4);
    o.p8(m.sidewarpoverlayflags);
    o.p8(m.warparrivalx);
    o.p8(m.warparrivaly);
    o.pn(m.path, 4);
    o.pn(m.sidewarpscr, 4);
    for(int i = 0; i < 4; ++i) o.p16(m.sidewarpdmap[i]);
    o.p8(m.sidewarpindex);
    o.p16(m.undercombo);
    o.p8(m.undercset);
    o.p16(m.catchall);
    o.p8(m.flags);
    o.p8(m.flags2);
    o.p8(m.flags3);
    o.p8(m.flags4);
    o.p8(m.flags5);
    o.p16(m.noreset);
    o.p16(m.nocarry);
    o.p8(m.flags6);
    o.p8(m.flags7);
    o.p8(m.flags8);
    o.p8(m.flags9);
    o.p8(m.flags10);
    o.p8(m.csensitive);
    o.p8(m.oceansfx);
    o.p8(m.bosssfx);
    o.p8(m.secretsfx);
    o.p8(m.holdupsfx);
    o.pn(m.layermap, 6);
    o.pn(m.layerscreen, 6);
    o.pn(m.layeropacity, 6);
    o.p16(m.timedwarptics);
    o.p8(m.nextmap);
    o.p8(m.nextscr);
    for(int k = 0; k < 128; ++k) o.p16(m.secretcombo[k]);
    o.pn(m.secretcset, 128);
    o.pn(m.secretflag, 128);
    for(int k = 0; k < SQUARES; ++k) o.p16(k < (int)m.data.size() ? m.data[k] : 0);
    for(int k = 0; k < SQUARES; ++k) o.p8(k < (int)m.sflag.size() ? m.sflag[k] : 0);
    for(int k = 0; k < SQUARES; ++k) o.p8(k < (int)m.cset.size() ? m.cset[k] : 0);
    o.p16((word)m.screen_midi);
    o.p8(m.lens_layer);
    o.p32(m.numff);

    for(int k = 0; k < OLD_FFCS; ++k)
    {
        if(!((m.numff >> k) & 1))
            continue;

        o.p16(m.ffdata[k]);
        o.p8(m.ffcset[k]);
        o.p16(m.ffdelay[k]);
        o.p32((dword)m.ffx[k]);
        o.p32((dword)m.ffy[k]);
        o.p32((dword)m.ffxdelta[k]);
        o.p32((dword)m.ffydelta[k]);
        o.p32((dword)m.ffxdelta2[k]);
        o.p32((dword)m.ffydelta2[k]);
        o.p8(m.fflink[k]);
        o.p8(m.ffwidth[k]);
        o.p8(m.ffheight[k]);
        o.p32(m.ffflags[k]);
        o.p16(m.ffscript[k]);
        for(int q = 0; q < 8; ++q) o.p32((dword)m.initd[k][q]);
        o.p8((byte)(m.inita[k][0] / 10000));
        o.p8((byte)(m.inita[k][1] / 10000));
    }
}

bool nofairy_rule()
{
    return get_bit(quest_rules, qr_NOFAIRYGUYFIRES) != 0;
}

// What upstream makes of a screen in 2.53's own form, as from a 2.53.1 quest.
void screen_from_253(const std::vector<byte> &enc, int target, UScr &out)
{
    In in(enc, 0);
    read_screen(in, 0x250, 33, 18, target, nofairy_rule(), out, NULL);
}

// A changed screen: each field of it the user didn't touch (its 2.53.1 reading now is what it
// was at load) takes upstream's value for the original screen.
#define MF(f) out.f = (now.f == was.f) ? orig.f : now.f
#define MA(f) for(size_t k_ = 0; k_ < sizeof(out.f) / sizeof(out.f[0]); ++k_) MF(f[k_])

void merge_screen(UScr &out, const UScr &orig, const UScr &now, const UScr &was)
{
    MF(valid); MF(guy); MF(guytile); MF(guycs); MF(roomflags); MF(str); MF(room); MF(item); MF(hasitem);
    MA(tilewarptype); MF(tilewarpoverlayflags); MF(door_combo_set); MA(warpreturnx); MA(warpreturny);
    MF(warpreturnc); MF(stairx); MF(stairy); MF(itemx); MF(itemy); MF(color); MA(door); MA(tilewarpdmap);
    MA(tilewarpscr); MF(exitdir); MA(enemy); MF(pattern); MA(sidewarptype); MF(sidewarpoverlayflags);
    MF(warparrivalx); MF(warparrivaly); MA(path); MA(sidewarpscr); MA(sidewarpdmap); MF(sidewarpindex);
    MF(undercombo); MF(undercset); MF(catchall); MA(flags); MF(csensitive); MF(noreset); MF(nocarry);
    MA(layermap); MA(layerscreen); MA(layeropacity); MF(timedwarptics); MF(nextmap); MF(nextscr);
    MA(secretcombo); MA(secretcset); MA(secretflag); MA(data); MA(sflag); MA(cset);
    MF(oceansfx); MF(bosssfx); MF(secretsfx); MF(holdupsfx); MF(screen_midi); MF(lens_layer); MF(old_cpage);

    for(int q = 0; q < OLD_FFCS; ++q)
    {
        MF(ffc[q].data); MF(ffc[q].cset); MF(ffc[q].delay); MF(ffc[q].x); MF(ffc[q].y); MF(ffc[q].vx);
        MF(ffc[q].vy); MF(ffc[q].ax); MF(ffc[q].ay); MF(ffc[q].link); MF(ffc[q].hit_width);
        MF(ffc[q].hit_height); MF(ffc[q].txsz); MF(ffc[q].tysz); MF(ffc[q].flags); MF(ffc[q].script);
        MA(ffc[q].initd); MA(ffc[q].inita);
    }
}

// ---- a DMap as upstream holds it ----------------------------------------------------------

struct UDmap
{
    byte map;
    word level;
    signed char xoff;
    byte compass;
    word color;
    byte midi;                      // 3.0 turns it and the enhanced music into advanced music
    byte cont, type;
    byte grid[8];
    char name[22], title[22], intro[74];
    long tile[4];                   // minimap 1, minimap 2, large map 1, large map 2
    byte tcset[4];
    char tmusic[57];
    byte tmusictrack;
    byte active_subscreen, passive_subscreen;
    byte disabled[32];              // the item bits
    long flags;
    char sideview;
    short mirrordmap;
    bool from_file;                 // read from the file: 3.0 then gives it its music
};

void blank_dmap(UDmap &d)
{
    memset(&d, 0, sizeof(d));
}

// read_one_dmap (2.55.17 qst.cpp, 3.0 core/qst_dmaps.cpp) for the DMap section versions of
// quests up to 2.53.1. rule14: the old rule 14 as the file holds it.
void read_dmap(In &in, word hv, byte hb, word sv, int index, bool rule14, UDmap &d)
{
    blank_dmap(d);
    d.from_file = true;
    char legacy_title[22];
    sprintf(legacy_title, "                    ");
    sprintf(d.intro, "                                                                        ");

    d.map = in.g8();
    d.level = sv <= 4 ? (word)in.g8() : in.g16();
    d.xoff = (signed char)in.g8();
    d.compass = in.g8();
    d.color = sv > 8 ? in.g16() : (word)in.g8();
    d.midi = in.g8();
    d.cont = in.g8();
    d.type = in.g8();

    if((d.type & dmfTYPE) == dmOVERW && hv >= 0x210)
        d.xoff = 0;

    for(int j = 0; j < 8; ++j)
        d.grid[j] = in.g8();

    if(hdr_before(hv, hb, 0x192, 41))
    {
        if(d.level > 0 && d.level < 10)
            sprintf(legacy_title, "LEVEL-%d             ", d.level);

        strcpy(d.title, legacy_title);

        if(index == 0 && hv <= 0x190)
        {
            int c = (int)d.cont - d.xoff, p = (int)d.compass - d.xoff;
            d.cont = (byte)(c > 0 ? c : 0);
            d.compass = (byte)(p > 0 ? p : 0);
        }

        if(d.level == 0)
            d.flags = U_DMF_CAVES4;
    }
    else
    {
        in.take(d.name, 21);
        d.name[21] = 0;
        in.take(legacy_title, 21);
        legacy_title[21] = 0;
        strcpy(d.title, legacy_title);
        in.take(d.intro, 73);
        d.intro[73] = 0;

        // these stop here, with no more of the DMap read (3.0 makes it no music either)
        if(hdr_before(hv, hb, 0x192, 152))
        {
            if((d.type & dmfTYPE) == dmOVERW)
                d.flags = U_DMF_CAVES4;

            d.from_file = false;
            return;
        }

        for(int t = 0; t < 4; ++t)
        {
            if(hv < 0x193)
                in.skip(1);

            d.tile[t] = sv >= 11 ? (long)in.g32() : (long)in.g16();
            d.tcset[t] = in.g8();
        }

        in.take(d.tmusic, 56);
        d.tmusic[56] = 0;
    }

    if(sv > 1)
    {
        d.tmusictrack = in.g8();
        d.active_subscreen = in.g8();
        d.passive_subscreen = in.g8();
    }

    if(sv > 2)
        in.take(d.disabled, 32);

    if(sv >= 6)
        d.flags = (long)in.g32();
    else if(sv > 3)
        d.flags = (signed char)in.g8();
    else if(d.level == 0 && hdr_before(hv, hb, 0x211, 18))
        d.flags = U_DMF_CAVES4;
    else
        d.flags = 0;

    if(sv < 7 && d.level == 0 && rule14)
        d.flags |= U_DMF_VIEWMAP;

    if(sv < 8)
    {
        if(d.level == 0 && (d.type & dmfTYPE) == dmDNGN)
            d.type |= dmCAVE;
        else if((d.type & dmfTYPE) == dmCAVE)
            d.flags |= U_DMF_MINIMAPCOLORFIX;
    }

    if(hdr_after(hv, hb, 0x192, 40) && hv < 0x193)
        in.skip(1);

    d.sideview = sv >= 10 ? (char)in.g8() : 0;
    d.mirrordmap = sv >= 16 ? (short)in.g16() : (short)-1;
}

// What upstream makes of ZQuest's DMap, as from a 2.53.1 quest (DMap section version 9; its
// tiles taken whole, which that version would cut to 16 bits).
void dmap_from_253(const dmap &z, UDmap &d)
{
    blank_dmap(d);
    d.from_file = true;
    d.map = z.map;
    d.level = z.level;
    d.xoff = (signed char)z.xoff;
    d.compass = z.compass;
    d.color = z.color;
    d.midi = z.midi;
    d.cont = z.cont;
    d.type = z.type;

    if((d.type & dmfTYPE) == dmOVERW)
        d.xoff = 0;

    memcpy(d.grid, z.grid, 8);
    memcpy(d.name, z.name, 21);
    memcpy(d.title, z.title, 21);
    memcpy(d.intro, z.intro, 73);
    d.tile[0] = z.minimap_1_tile;
    d.tile[1] = z.minimap_2_tile;
    d.tile[2] = z.largemap_1_tile;
    d.tile[3] = z.largemap_2_tile;
    d.tcset[0] = z.minimap_1_cset;
    d.tcset[1] = z.minimap_2_cset;
    d.tcset[2] = z.largemap_1_cset;
    d.tcset[3] = z.largemap_2_cset;
    memcpy(d.tmusic, z.tmusic, 56);
    d.tmusictrack = z.tmusictrack;
    d.active_subscreen = z.active_subscreen;
    d.passive_subscreen = z.passive_subscreen;

    for(int j = 0; j < MAXITEMS && j < (int)sizeof(z.disableditems); ++j)
        if(z.disableditems[j])
            d.disabled[j / 8] |= (byte)(1 << (j % 8));

    d.flags = z.flags;
    d.mirrordmap = -1;
}

void merge_dmap(UDmap &out, const UDmap &orig, const UDmap &now, const UDmap &was)
{
    MF(map); MF(level); MF(xoff); MF(compass); MF(color); MF(midi); MF(cont); MF(type); MA(grid);
    MA(tile); MA(tcset); MF(tmusictrack); MF(active_subscreen); MF(passive_subscreen); MA(disabled);
    MF(flags); MF(sideview); MF(mirrordmap); MF(from_file);

    // strings as a whole
    if(!memcmp(now.name, was.name, sizeof(now.name))) memcpy(out.name, orig.name, sizeof(out.name));
    else memcpy(out.name, now.name, sizeof(out.name));
    if(!strcmp(now.title, was.title)) memcpy(out.title, orig.title, sizeof(out.title));
    else memcpy(out.title, now.title, sizeof(out.title));
    if(!memcmp(now.intro, was.intro, sizeof(now.intro))) memcpy(out.intro, orig.intro, sizeof(out.intro));
    else memcpy(out.intro, now.intro, sizeof(out.intro));
    if(!memcmp(now.tmusic, was.tmusic, sizeof(now.tmusic))) memcpy(out.tmusic, orig.tmusic, sizeof(out.tmusic));
    else memcpy(out.tmusic, now.tmusic, sizeof(out.tmusic));
}

#undef MF
#undef MA

// ---- ZQuest's data as loaded --------------------------------------------------------------

struct Snapshot
{
    bool valid;
    word maps;                              // map_count
    std::vector<byte> screens;              // every screen in 2.53's form, one after another
    std::vector<size_t> screen_at;          // where each starts (and one more: the end)
    std::vector<uint64_t> screen_hash;
    std::vector<dmap> dmaps;

    Snapshot() : valid(false), maps(0) {}
};

Snapshot g_snap;

void note_loaded()
{
    g_snap.valid = true;
    g_snap.maps = map_count;
    g_snap.screens.clear();
    g_snap.screen_at.clear();
    g_snap.screen_hash.clear();

    for(size_t i = 0; i < TheMaps.size(); ++i)
    {
        Out o;
        encode_253(o, TheMaps[i]);
        g_snap.screen_at.push_back(g_snap.screens.size());
        g_snap.screen_hash.push_back(fnv(o.b));
        g_snap.screens.insert(g_snap.screens.end(), o.b.begin(), o.b.end());
    }

    g_snap.screen_at.push_back(g_snap.screens.size());
    g_snap.dmaps.assign(DMaps, DMaps + MAXDMAPS);
}

ZqLoadHookRegistration g_hook(note_loaded);

// ---- the edit test ---------------------------------------------------------------------------

// ZQN_EDIT=1 (for testing the changed-record path end to end): the first time a newer writer
// runs after a load, which is after every load snapshot and part fingerprint was taken, some
// screens, the combos placed on them and some DMaps change as an edit in ZQuest would, only in
// ways the quest's own version can hold. ZQN_EDIT_SAVE=<file> then saves the edited quest in
// its own version there (File > Save) and writes how that went to <file>.txt, so upstream can
// convert the edited file and the newer save of the same edit can be compared with it.
bool in_use(const dmap &d)
{
    if(d.map || d.level || d.xoff || d.compass || d.cont || d.name[0])
        return true;

    for(int j = 0; j < 8; ++j)
        if(d.grid[j])
            return true;

    return false;
}

void edit_test_once()
{
    static bool done = false;
    const char *e = getenv("ZQN_EDIT");

    if(done || !e || !*e || !g_snap.valid)
        return;

    done = true;
    const word hv = zq_quest_file.zelda_version;
    const byte hb = zq_quest_file.build;
    const bool flat = hv < 0x193;
    word map_v = 0, dmap_v = 0, cv = 0;
    const ZqQuestPart *mp = zq_find_part(ID_MAPS), *dp = zq_find_part(ID_DMAPS);

    if(mp)
        zq_part_versions(*mp, &map_v, &cv);

    if(dp)
        zq_part_versions(*dp, &dmap_v, &cv);

    // screens: every ninth from the fifth, normal screens only (an old quest's template
    // screens make its door sets and secret combos)
    int screens = 0, kind = 0;

    for(size_t i = 4; i < TheMaps.size(); i += 9)
    {
        mapscr &m = TheMaps[i];

        if(!(m.valid & mVALID) || (i % MAPSCRS) >= 0x80 || m.data.size() < SQUARES || m.cset.size() < SQUARES ||
           m.sflag.size() < SQUARES)
            continue;

        switch(kind++ % 4)
        {
        case 0:     // combos placed
            m.data[20] ^= 1;
            m.cset[21] = m.cset[22];
            m.sflag[23] = m.sflag[24];
            m.data[40] = m.data[41];
            m.cset[40] = m.cset[41];
            break;

        case 1:     // warps, doors and the under combo
            m.tilewarpscr[0] ^= 1;
            m.sidewarpscr[0] ^= 1;
            m.warparrivalx = m.warparrivaly;
            m.door[0] = m.door[1];
            m.undercombo = m.data[0];
            m.undercset = m.cset[0];
            break;

        case 2:     // enemies, the room and its flags
            m.enemy[9] = m.enemy[8];
            m.catchall ^= 1;

            if(m.guy)
                m.guy = 0;

            m.flags ^= fDARK;
            m.flags3 ^= fHOLDITEM;
            break;

        case 3:     // an FFC and the screen's music
            for(int k = 0; k < MAXFFCS; ++k)
                if((m.numff >> k) & 1)
                {
                    m.ffdelay[k] ^= 1;
                    m.ffy[k] = m.ffx[k];
                    break;
                }

            if(!flat && map_v > 4)
                m.screen_midi = (kind / 4) % 2 ? 1 : 4;     // the overworld's, or the first tune

            break;
        }

        ++screens;
    }

    // ZQN_EDIT=2 adds a map too, as the map count setting does, with the first screen of the
    // first map copied onto its first screen
    if(!strcmp(e, "2") && map_count < MAXMAPS2)
    {
        const size_t first = (size_t)map_count * MAPSCRS;

        if(setMapCount2(map_count + 1) && TheMaps.size() >= first + MAPSCRS)
            for(int j = 0; j < 0x80; ++j)
                if(TheMaps[j].valid & mVALID)
                {
                    TheMaps[first] = TheMaps[j];
                    ++screens;
                    break;
                }
    }

    // DMaps: every third one in use from the second, and for a sectioned quest one new one
    // after the last in use
    int last_used = -1, dmaps = 0;
    const int cap = flat ? (hdr_before(hv, hb, 0x192, 5) ? 32 : OLDMAXDMAPS) : MAXDMAPS;

    for(int i = 0; i < cap; ++i)
        if(in_use(DMaps[i]))
            last_used = i;

    bool music_on = false, music_off = false;

    for(int i = 1; i <= last_used; i += 3)
    {
        dmap &d = DMaps[i];

        if(!in_use(d))
            continue;

        d.grid[0] ^= 0x01;
        d.compass ^= 1;
        d.color ^= 1;

        if(!flat)
        {
            d.name[0] = d.name[0] == 'E' ? 'F' : 'E';

            if(dmap_v > 2)
                d.disableditems[5] ^= 1;

            if(dmap_v > 3)
                d.flags ^= dmfNOCOMPASS;
        }

        // one DMap gets music, one loses it
        if(!music_on && !d.midi && !d.tmusic[0])
        {
            d.midi = 2;
            music_on = true;
        }
        else if(!music_off && d.midi && !d.tmusic[0])
        {
            d.midi = 0;
            music_off = true;
        }

        ++dmaps;
    }

    if(!flat && last_used + 1 < MAXDMAPS)
    {
        dmap &d = DMaps[last_used + 1];
        d.map = DMaps[0].map;
        d.level = 3;
        d.type = dmDNGN;
        d.color = 1;
        d.grid[0] = 0xF0;
        d.grid[1] = 0x0F;
        strcpy(d.name, "New DMap");
        d.midi = 3;

        // DMap sections before version 4 hold no flags: their reader makes them none for a
        // level above 0
        if(dmap_v <= 3)
            d.flags = 0;
        ++dmaps;
    }

    char line[200];
    snprintf(line, sizeof(line), "edited %d screens and %d DMaps (quest 0x%x build %d, maps v%d, DMaps v%d)", screens, dmaps,
             hv, hb, map_v, dmap_v);
    al_trace("zqn edit test: %s\n", line);
    const char *save = getenv("ZQN_EDIT_SAVE");

    if(!save || !*save)
        return;

    std::string why;
    int r = zq_save_own_format(save, true, why);
    FILE *out = fopen((std::string(save) + ".txt").c_str(), "w");

    if(out)
    {
        fprintf(out, "%s\nown-format save: %s%s%s\n", line, r ? "FAILED " : "SAVED", r ? ": " : "", why.c_str());
        fclose(out);
    }
}

// ---- where the original records are --------------------------------------------------------

// The original file's maps or DMaps: its header version and the section version they were read
// with (0 before 1.93), the bytes, and where the records start.
struct Source
{
    word hv;
    byte hb;
    word sv;
    std::vector<byte> bytes;
    size_t at;
    int count;              // maps, or DMaps
    bool rule14;            // the old rule 14 as stored
    bool own;               // the bytes are the own-format save of the user's edit
};

// Pre-1.93 quests have no sections: the readers walk one body in a fixed order (strings, door
// combo sets, DMaps, misc data, items, weapons, enemies (nothing stored), maps). These are the
// sizes those readers take, for the versions in the test pack (as zqw_flat.cpp walks them).
bool flat_supported(word v, byte b)
{
    return v == 0x190 || (v == 0x192 && (b == 152 || b == 163 || b == 182 || b == 183));
}

bool bf(word v, byte b, int b0)
{
    return v < 0x192 || (v == 0x192 && b < b0);
}

size_t flat_misc_size(word v, byte b)
{
    size_t n = 160 + 16 * (bf(v, b, 146) ? 10 : 14) + 144 + (bf(v, b, 73) ? 16 : 256) * 9 + 18 + 8 + 33 + 7;

    if(v == 0x192 && b > 145)
        n += 256;

    n += (bf(v, b, 73) ? 3 : 4) * 2;

    if(bf(v, b, 30))
        return n;

    n += 16 * (v == 0x192 && b < 146 ? 25 : 72) + 2 + ((v == 0x192 && b < 73) ? 198 : 196);
    return n;
}

// Where a pre-1.93 body's DMaps and maps start.
bool flat_offsets(const std::vector<byte> &body, word v, byte b, size_t *dmaps_at, size_t *maps_at)
{
    In in(body, 0);
    size_t len = bf(v, b, 148) ? 76 : 108;

    if(bf(v, b, 31))
        in.skip(128 * len);
    else
    {
        word n = in.g16();
        in.skip(n * len);
    }

    if(!bf(v, b, 158))
    {
        word n = in.g16();
        in.skip((size_t)n * 638);
    }

    *dmaps_at = in.pos;
    in.skip((size_t)(bf(v, b, 5) ? 32 : OLDMAXDMAPS) * (bf(v, b, 41) ? 16 : 204));
    in.skip(flat_misc_size(v, b));
    in.skip((size_t)(v < 0x186 ? 64 : 256) * 8);
    in.skip((size_t)(v < 0x185 ? 32 : (v < 0x186 ? 64 : 256)) * 8);
    *maps_at = in.pos;
    return !in.bad;
}

// The old rule 14 as the file stores it (readrules' deprecated_rules).
bool stored_rule14(word hv)
{
    if(hv < 0x193)
    {
        // the header holds the first rules (readheader)
        const ZqQuestPart *h = zq_find_part(ID_HEADER);
        byte r[2];
        return h && zq_part_read(*h, 37, r, 2) && (r[1] & (1 << 6));
    }

    const ZqQuestPart *rules = zq_find_part(ID_RULES);
    byte rb[2];
    return rules && zq_part_read(*rules, 8, rb, 2) && ((rb[14 >> 3] >> (14 & 7)) & 1);
}

// ---- what the own-format save would write ------------------------------------------------

// Whether the user changed any screen (or added or deleted maps), or any DMap, since the load.
bool screens_edited()
{
    if(!g_snap.valid)
        return false;

    if(g_snap.maps != map_count || g_snap.screen_hash.size() != TheMaps.size())
        return true;

    for(size_t i = 0; i < TheMaps.size(); ++i)
    {
        Out o;
        encode_253(o, TheMaps[i]);

        if(fnv(o.b) != g_snap.screen_hash[i])
            return true;
    }

    return false;
}

bool dmaps_edited()
{
    if(!g_snap.valid || g_snap.dmaps.size() != MAXDMAPS)
        return false;

    for(int i = 0; i < MAXDMAPS; ++i)
        if(memcmp(&g_snap.dmaps[i], &DMaps[i], sizeof(dmap)))
            return true;

    return false;
}

// A write-only file into a byte vector.
int vs_fclose(void *) { return 0; }
int vs_getc(void *) { return EOF; }
int vs_ungetc(int, void *) { return EOF; }
#pragma push_macro("long")  // packfile callbacks use the system's long (zcport/lp64.h)
#undef long
long vs_fread(void *, long, void *) { return 0; }
#pragma pop_macro("long")
int vs_feof(void *) { return 1; }
int vs_ferror(void *) { return 0; }
int vs_fseek(void *, int) { return -1; }

int vs_putc(int c, void *user)
{
    ((std::vector<byte> *)user)->push_back((byte)c);
    return c & 0xFF;
}

#pragma push_macro("long")  // packfile callbacks use the system's long (zcport/lp64.h)
#undef long
long vs_fwrite(const void *p, long n, void *user)
{
    const byte *b = (const byte *)p;
    ((std::vector<byte> *)user)->insert(((std::vector<byte> *)user)->end(), b, b + n);
    return n;
}
#pragma pop_macro("long")

const PACKFILE_VTABLE g_vec_vtable = { vs_fclose, vs_getc, vs_ungetc, vs_fread, vs_putc, vs_fwrite, vs_fseek, vs_feof, vs_ferror };

// The part as the quest's own-format save would write it now (what its reader reads: no
// section id). An edited quest saved in its own version and opened by upstream is what an edit
// saved as a newer version must match, so edited records are read from these bytes with
// upstream's reader for the file's version, as upstream reads that file. False when the part's
// writer can't hold the edit.
bool own_save_bytes(const ZqQuestPart &p, std::vector<byte> &out)
{
    // ZQN_EDIT_MERGE=1 (for testing): as if the own version couldn't hold any edit
    const char *merge = getenv("ZQN_EDIT_MERGE");

    if(merge && *merge)
        return false;

    std::vector<byte> b;
    PACKFILE *f = zq_open_sink(&g_vec_vtable, &b);

    if(!f)
        return false;

    const std::string err = zqw_last_error;
    const int rc = zq_write_part(f, p);
    pack_fclose(f);
    zqw_last_error = err;
    const size_t skip = zq_part_is_raw(p.id) ? 0 : 4;

    if(rc != ZQW_OK || b.size() < skip)
        return false;

    out.assign(b.begin() + skip, b.end());
    return true;
}

// The original maps (want_maps) or DMaps. False when the file has none to read. When the user
// changed them and the quest's own version can hold the change, the bytes are those its own-
// format save would write (s.own).
bool find_source(const ZqNewerContext &ctx, bool want_maps, Source &s)
{
    s.hv = ctx.src_zelda_version;
    s.hb = ctx.src_build;
    s.sv = 0;
    s.at = 0;
    s.count = 0;
    s.own = false;
    s.rule14 = stored_rule14(s.hv);

    if(s.hv < 0x193)
    {
        const ZqQuestPart *body = zq_find_part(0);
        const ZqQuestPart *h = zq_find_part(ID_HEADER);
        byte mc;

        if(!body || !h || !flat_supported(s.hv, s.hb) || !part_bytes(*body, s.bytes) || !zq_part_read(*h, 39, &mc, 1))
            return false;

        // one body holds both: its own-format save writes both, and the header the map count
        if((screens_edited() || dmaps_edited()) && own_save_bytes(*body, s.bytes))
        {
            s.own = true;
            mc = (byte)map_count;
        }

        size_t dmaps_at, maps_at;

        if(!flat_offsets(s.bytes, s.hv, s.hb, &dmaps_at, &maps_at))
            return false;

        s.at = want_maps ? maps_at : dmaps_at;
        s.count = want_maps ? mc : (bf(s.hv, s.hb, 5) ? 32 : OLDMAXDMAPS);
        return true;
    }

    // the part itself, not the context's: the map writer needs the DMaps too
    const ZqQuestPart *part = zq_find_part(want_maps ? ID_MAPS : ID_DMAPS);
    word cv;

    if(!part || !zq_part_versions(*part, &s.sv, &cv) || !part_bytes(*part, s.bytes) || s.bytes.size() < 10)
        return false;

    if((want_maps ? screens_edited() : dmaps_edited()) && own_save_bytes(*part, s.bytes))
    {
        s.own = true;

        if(s.bytes.size() < 10)
            return false;

        s.sv = (word)(s.bytes[0] | (s.bytes[1] << 8));
    }

    s.at = 10;
    s.count = s.bytes[8] | (s.bytes[9] << 8);
    return true;
}

// ---- DMaps as upstream holds them after loading ---------------------------------------------

// Upstream's subscreen lists, which decide what DMap subscreen numbers survive the save.
struct SubscreenState
{
    int active, passive;            // how many of each
    bool has_old;                   // the old-subscreen fix-up (fixup_old_dmap_subscreen_indices)
    int reach[2], fallback_type, fallback_index;
};

// setupsubscreens: two of each
void subscreens_default(SubscreenState &st)
{
    st.active = st.passive = 2;
    st.has_old = false;
    st.reach[0] = st.reach[1] = 0;
    st.fallback_type = st.fallback_index = -1;
}

// read_old_subscreens: the custom subscreens the file has, as ZQuest holds them
void subscreens_custom(SubscreenState &st)
{
    st.active = st.passive = 0;
    st.has_old = false;
    st.reach[0] = st.reach[1] = 0;
    st.fallback_type = st.fallback_index = -1;
    bool hit_empty = false;

    for(int i = 0; i < MAXCUSTOMSUBSCREENS; ++i)
    {
        const subscreen_group &g = custom_subscreen[i];

        if(g.objects[0].type == ssoNULL)
        {
            hit_empty = true;
            continue;
        }

        bool passive = g.ss_type == sstPASSIVE;
        int &n = passive ? st.passive : st.active;
        ++n;

        if(!hit_empty)
        {
            st.reach[passive ? sstPASSIVE : sstACTIVE] = n;
            st.fallback_type = passive ? sstPASSIVE : sstACTIVE;
            st.fallback_index = n - 1;
            st.has_old = true;
        }
    }
}

// Every DMap as upstream holds it after loading the quest and before writing it: read from the
// file (or, changed, from ZQuest), then the subscreens the init data and the custom subscreens
// give it. *file_count: how many DMaps the file has.
bool final_dmaps(const ZqNewerContext &ctx, std::vector<UDmap> &out, int *file_count)
{
    Source src;
    const bool have_src = find_source(ctx, false, src);

    // a quest whose file has DMaps this can't read again (a sectioned quest with none starts
    // from empty ones, as upstream's does)
    if(!have_src && (ctx.src_zelda_version < 0x193 || zq_find_part(ID_DMAPS)))
        return false;

    out.resize(MAXDMAPS);
    In in(src.bytes, src.at);
    const int count = have_src ? (src.count < MAXDMAPS ? src.count : MAXDMAPS) : 0;
    *file_count = count;
    UDmap now, was;

    for(int i = 0; i < MAXDMAPS; ++i)
    {
        UDmap orig;

        if(i < count)
        {
            read_dmap(in, src.hv, src.hb, src.sv, i, src.rule14, orig);

            if(in.bad)
                return false;
        }
        else
            blank_dmap(orig);

        // the own-format save's bytes hold the edits already
        const bool unchanged = src.own ||
                               (g_snap.valid && i < (int)g_snap.dmaps.size() && !memcmp(&g_snap.dmaps[i], &DMaps[i], sizeof(dmap)));

        if(unchanged)
            out[i] = orig;
        else
        {
            dmap_from_253(DMaps[i], now);

            // a DMap past the file's has no original to keep fields of: it's ZQuest's alone
            if(i < count && g_snap.valid && i < (int)g_snap.dmaps.size())
            {
                dmap_from_253(g_snap.dmaps[i], was);
                merge_dmap(out[i], orig, now, was);
            }
            else
                out[i] = now;

            // read from the file once it's saved, so 3.0 gives it its music, except in the
            // 1.92 versions whose reader stops before the music (read_dmap)
            out[i].from_file = !(!hdr_before(src.hv, src.hb, 0x192, 41) && hdr_before(src.hv, src.hb, 0x192, 152));
        }
    }

    // The subscreens: set from each DMap's type and the magic rule when the quest has no custom
    // subscreens (when the init data is read, or at the end for a pre-1.93 quest), then the
    // custom subscreens the file has replace the lists.
    SubscreenState st;
    subscreens_default(st);
    const bool magic = get_bit(quest_rules, qr_ENABLEMAGIC) != 0;
    bool set_from_type = false;

    if(ctx.src_zelda_version < 0x193)
        set_from_type = true;
    else
    {
        // in file order: the init data (setupsubscreens, and the DMaps' numbers when the DMaps
        // came first) and the subscreens; the later of the two leaves its lists
        const int pd = part_index(ID_DMAPS), pi = part_index(ID_INITDATA), ps = part_index(ID_SUBSCREEN);
        const bool init_sets = pi >= 0 && zinit.subscreen != ssdtMAX;
        set_from_type = init_sets && pi > pd;

        if(ps >= 0 && (!init_sets || ps > pi))
            subscreens_custom(st);
    }

    if(set_from_type)
        for(int i = 0; i < MAXDMAPS; ++i)
        {
            int type = out[i].type & dmfTYPE;
            out[i].active_subscreen = (type == dmOVERW || type == dmBSOVERW) ? 0 : 1;
            out[i].passive_subscreen = magic ? 0 : 1;
        }

    // fixup_old_dmap_subscreen_indices: a number past what the old engine could reach lands on
    // the last subscreen before the first empty one, copied into the other list if it's the
    // other type
    if(st.has_old && st.fallback_type >= 0)
    {
        int fallback[2] = { -1, -1 };
        fallback[st.fallback_type] = st.fallback_index;

        for(int i = 0; i < MAXDMAPS; ++i)
        {
            for(int t = 0; t < 2; ++t)
            {
                byte &sub = t == sstACTIVE ? out[i].active_subscreen : out[i].passive_subscreen;

                if(sub < st.reach[t])
                    continue;

                if(fallback[t] < 0)
                {
                    int &n = t == sstPASSIVE ? st.passive : st.active;
                    ++n;
                    fallback[t] = n - 1;
                }

                sub = (byte)fallback[t];
            }
        }
    }

    // validate_subscreens, as each DMap is written
    for(int i = 0; i < MAXDMAPS; ++i)
    {
        if(out[i].active_subscreen >= st.active)
            out[i].active_subscreen = 0;

        if(out[i].passive_subscreen >= st.passive)
            out[i].passive_subscreen = 0;
    }

    return true;
}

// 3.0: a DMap's music, made from its MIDI and enhanced music (read_one_dmap)
int dmap_midi_30(const UDmap &d)
{
    // convert_from_old_midi_id(midi + (MIDIOFFSET_DMAP - MIDIOFFSET_ZSCRIPT))
    int m = (int)d.midi - 3;

    switch(m)
    {
    case -3: return 0;
    case -2: return -5;     // overworld
    case -1: return -1;     // dungeon
    case 0: return -4;      // level 9
    }

    return m < 0 ? 0 : m;
}

bool dmap_has_music_30(const UDmap &d)
{
    return d.from_file && (dmap_midi_30(d) != 0 || d.tmusic[0] != 0);
}

// 3.0's music for a screen's MIDI: convert_from_old_midi_id(midi - 3, for a screen)
int screen_midi_30(short m)
{
    switch(m - 3)
    {
    case -3: return 0;
    case -2: return -5;
    case -1: return -1;
    case 0: return -4;
    }

    return m - 3 < 0 ? 0 : m - 3;
}

// ---- screens as upstream holds them after loading --------------------------------------------

// MakeDoors (2.55.17 qst.cpp, 3.0 core/qst_maps.cpp): a 1.90 or early 1.92 quest stores no door
// combo sets; each screen gets the set its map's template screen makes with the screen's combo
// page, a new one the first time it differs from every set so far.
const int door_u[9][4] =
{
    {37,38,53,54}, {37,38,39,40}, {37,38,55,56}, {37,38,39,40}, {37,38,53,54},
    {37,38,53,54}, {37,38,53,54}, {7,8,23,24}, {7,8,41,42}
};
const int door_d[9][4] =
{
    {117,118,133,134}, {135,136,133,134}, {119,120,133,134}, {135,136,133,134}, {117,118,133,134},
    {117,118,133,134}, {117,118,133,134}, {151,152,167,168}, {137,138,167,168}
};
const int door_l[9][6] =
{
    {66,67,82,83,98,99}, {66,68,82,84,98,100}, {66,69,82,85,98,101}, {66,68,82,84,98,100},
    {66,67,82,83,98,99}, {66,67,82,83,98,99}, {66,67,82,83,98,99}, {64,65,80,81,96,97},
    {64,65,80,114,96,97}
};
const int door_r[9][6] =
{
    {76,77,92,93,108,109}, {75,77,91,93,107,109}, {74,77,90,93,106,109}, {75,77,91,93,107,109},
    {76,77,92,93,108,109}, {76,77,92,93,108,109}, {76,77,92,93,108,109}, {78,79,94,95,110,111},
    {78,79,125,95,110,111}
};

struct DoorSet      // upstream's DoorComboSet, without the name
{
    word combo_u[9][4];
    byte cset_u[9][4];
    word combo_d[9][4];
    byte cset_d[9][4];
    word combo_l[9][6];
    byte cset_l[9][6];
    word combo_r[9][6];
    byte cset_r[9][6];
    word bomb_u[2];
    byte bombcset_u[2];
    word bomb_d[2];
    byte bombcset_d[2];
    word bomb_l[3];
    byte bombcset_l[3];
    word bomb_r[3];
    byte bombcset_r[3];
    word walk[4];
    byte walkcset[4];
};

int make_doors(const UScr &t, const UScr &s, std::vector<DoorSet> &sets)
{
    if(!(s.valid & mVALID))
        return 0;

    DoorSet d;
    memset(&d, 0, sizeof(d));
    // tdcmbdat: the template's combo, on this screen's page; tdcmbcset is always 2
    word c[SQUARES];

    for(int k = 0; k < SQUARES; ++k)
        c[k] = (word)((t.data[k] & 0xFF) + (s.old_cpage << 8));

    for(int i = 0; i < 9; ++i)
    {
        for(int j = 0; j < 4; ++j)
        {
            d.combo_u[i][j] = c[door_u[i][j]];
            d.cset_u[i][j] = 2;
            d.combo_d[i][j] = c[door_d[i][j]];
            d.cset_d[i][j] = 2;
        }

        for(int j = 0; j < 6; ++j)
        {
            d.combo_l[i][j] = c[door_l[i][j]];
            d.cset_l[i][j] = 2;
            d.combo_r[i][j] = c[door_r[i][j]];
            d.cset_r[i][j] = 2;
        }
    }

    d.bomb_u[0] = c[57];
    d.bomb_u[1] = c[58];
    d.bombcset_u[0] = d.bombcset_u[1] = 2;
    d.bomb_d[0] = c[121];
    d.bomb_d[1] = c[122];
    d.bombcset_d[0] = d.bombcset_d[1] = 2;
    d.bomb_l[1] = c[115];
    d.bombcset_l[0] = d.bombcset_l[1] = d.bombcset_l[2] = 2;
    d.bomb_r[1] = c[124];
    d.bombcset_r[0] = d.bombcset_r[1] = d.bombcset_r[2] = 2;

    for(int k = 0; k < 4; ++k)
    {
        d.walk[k] = c[34];
        d.walkcset[k] = (byte)c[34];    // the combo number, cut to a byte (as upstream does)
    }

    size_t k = 0;

    while(k < sets.size() && memcmp(&sets[k], &d, sizeof(d)))
        ++k;

    if(k >= 256)        // MAXDOORCOMBOSETS
        return 0;

    if(k == sets.size())
        sets.push_back(d);

    return (int)k;
}

// A pre-1.93 screen's secret combos are places on its map's second template screen (readmaps).
void template_secrets(UScr &s, const UScr &t2, byte cpage)
{
    for(int k = 0; k < 128; ++k)
    {
        word pos = s.secretcombo[k];

        if(pos >= SQUARES)
            pos = 0;

        s.secretcset[k] = t2.cset[pos];
        s.secretflag[k] = t2.sflag[pos];
        s.secretcombo[k] = (word)((t2.data[pos] & 0xFF) + (cpage << 8));
    }
}

// All maps' screens as upstream holds them. False when the original can't be read. made_doors,
// if given, gets the door combo sets MakeDoors made (quests before 1.92 build 154).
bool final_screens(const ZqNewerContext &ctx, std::vector<UScr> &out, std::vector<DoorSet> *made_doors = NULL)
{
    Source src;
    const bool have_src = find_source(ctx, true, src);

    if(!have_src)
        return false;

    const int target = ctx.target;
    const bool nofairy = nofairy_rule();
    const int maps = map_count < MAXMAPS2 ? map_count : MAXMAPS2;
    const int src_maps = src.count < MAXMAPS2 ? src.count : MAXMAPS2;
    const int per_map = hdr_before(src.hv, src.hb, 0x192, 137) ? MAPSCRS192b136 : MAPSCRS;

    // ZQuest adds and removes maps only at the end (the map count setting), so a screen keeps
    // its place: those past the original maps are new, the others line up with the original
    In in(src.bytes, src.at);
    bool old_ffc = false;
    out.resize((size_t)maps * MAPSCRS);
    std::vector<UScr> orig(MAPSCRS);
    UScr now, was;
    std::vector<DoorSet> door_sets;     // MakeDoors' sets: none stored before 1.92 build 158

    for(int i = 0; i < maps || i < src_maps; ++i)
    {
        const bool have_orig = i < src_maps;

        if(have_orig)
        {
            for(int j = 0; j < per_map; ++j)
            {
                read_screen(in, src.hv, src.hb, src.sv, target, nofairy, orig[j], &old_ffc);

                if(in.bad)
                    return false;
            }

            // readmaps: a 1.90 map has 132 screens; the second template is a copy of the first
            // and the last three are blank
            if(per_map == MAPSCRS192b136)
            {
                orig[TEMPLATE2] = orig[TEMPLATE];

                if(target == ZQ_NEWER_30)
                    orig[TEMPLATE2].old_cpage = 0;   // 3.0 keeps the pages apart, not copied

                for(int j = TEMPLATE2 + 1; j < MAPSCRS; ++j)
                {
                    blank_screen(orig[j], target);
                    orig[j].valid = mVERSION;
                }
            }

            if(hdr_before(src.hv, src.hb, 0x192, 154))
            {
                const UScr t = orig[TEMPLATE], t2 = orig[TEMPLATE2];

                for(int j = 0; j < MAPSCRS; ++j)
                {
                    orig[j].door_combo_set = (word)make_doors(t, orig[j], door_sets);
                    template_secrets(orig[j], t2, orig[j].old_cpage);
                }
            }
        }

        if(i >= maps)
            continue;       // a map deleted since the load

        for(int j = 0; j < MAPSCRS; ++j)
        {
            const size_t idx = (size_t)i * MAPSCRS + j;

            if(idx >= TheMaps.size())
                return false;

            // the own-format save's bytes hold the edits already
            if(src.own)
            {
                if(!have_orig)
                    return false;

                out[idx] = orig[j];
                continue;
            }

            Out enc;
            encode_253(enc, TheMaps[idx]);
            const bool known = g_snap.valid && idx + 1 < g_snap.screen_at.size();
            const bool unchanged = known && fnv(enc.b) == g_snap.screen_hash[idx];

            if(have_orig && unchanged)
            {
                out[idx] = orig[j];
                continue;
            }

            screen_from_253(enc.b, target, now);

            if(have_orig && known)
            {
                std::vector<byte> old(g_snap.screens.begin() + g_snap.screen_at[idx],
                                      g_snap.screens.begin() + g_snap.screen_at[idx + 1]);
                screen_from_253(old, target, was);
                merge_screen(out[idx], orig[j], now, was);
            }
            else
                out[idx] = now;
        }
    }

    if(made_doors)
        made_doors->swap(door_sets);

    // loadquest's fixffcs: after maps of version 11 or older, an FFC showing a changer combo
    // is a changer
    if(old_ffc)
        for(size_t idx = 0; idx < out.size(); ++idx)
            for(int m = 0; m < OLD_FFCS; ++m)
            {
                word data = out[idx].ffc[m].data;

                if(data < MAXCOMBOS && combobuf[data].type == cCHANGE)
                    out[idx].ffc[m].flags |= U_FFC_CHANGER;
            }

    return true;
}

// ---- writing a screen -------------------------------------------------------------------

const dword SCRHAS_ROOMDATA = 0x1, SCRHAS_ITEM = 0x2, SCRHAS_TWARP = 0x4, SCRHAS_SWARP = 0x8, SCRHAS_WARPRET = 0x10,
            SCRHAS_LAYERS = 0x20, SCRHAS_MAZE = 0x40, SCRHAS_D_S_U = 0x80, SCRHAS_FLAGS = 0x100, SCRHAS_ENEMY = 0x200,
            SCRHAS_CARRY = 0x400, SCRHAS_SCRIPT = 0x800, SCRHAS_SECRETS = 0x2000, SCRHAS_COMBOFLAG = 0x4000,
            SCRHAS_MISC = 0x8000;

// writemapscreen (2.55.17 and 3.0 zq_class.cpp). music: 3.0's music index for the screen.
void write_screen(Out &o, const UScr &s, int j, int target, long music)
{
    const bool v30 = target == ZQ_NEWER_30;
    const bool is_0x80 = j >= 0x80;
    o.p8(s.valid);

    if(!(s.valid & mVALID))
        return;

    dword has = 0;

    if(s.guytile || s.guy || s.roomflags || s.str || s.room || s.catchall)
        has |= SCRHAS_ROOMDATA;

    if(s.hasitem || (is_0x80 && (s.itemx || s.itemy)))
        has |= SCRHAS_ITEM;

    if((s.warpreturnc & 0x00FF) || s.tilewarpoverlayflags)
        has |= SCRHAS_TWARP;
    else
        for(int q = 0; q < 4; ++q)
            if(s.tilewarptype[q] || s.tilewarpdmap[q] || s.tilewarpscr[q])
            {
                has |= SCRHAS_TWARP;
                break;
            }

    if((s.warpreturnc & 0xFF00) || s.sidewarpindex || s.sidewarpoverlayflags)
        has |= SCRHAS_SWARP;
    else
        for(int q = 0; q < 4; ++q)
            if(s.sidewarptype[q] != U_WTSCROLL || s.sidewarpdmap[q] || s.sidewarpscr[q])
            {
                has |= SCRHAS_SWARP;
                break;
            }

    if(s.warparrivalx || s.warparrivaly)
        has |= SCRHAS_WARPRET;
    else
        for(int q = 0; q < 4; ++q)
            if(s.warpreturnx[q] || s.warpreturny[q])
            {
                has |= SCRHAS_WARPRET;
                break;
            }

    for(int q = 0; q < 6; ++q)
        if(s.layermap[q] || s.layerscreen[q] || s.layeropacity[q] != 255)
        {
            has |= SCRHAS_LAYERS;
            break;
        }

    if(s.exitdir)
        has |= SCRHAS_MAZE;
    else
        for(int q = 0; q < 4; ++q)
            if(s.path[q])
            {
                has |= SCRHAS_MAZE;
                break;
            }

    if(s.door_combo_set || s.stairx || s.stairy || s.undercombo || s.undercset)
        has |= SCRHAS_D_S_U;
    else
        for(int q = 0; q < 4; ++q)
            if(s.door[q] != U_DNONE)
            {
                has |= SCRHAS_D_S_U;
                break;
            }

    for(int q = 0; q < 11; ++q)
        if(s.flags[q])
        {
            has |= SCRHAS_FLAGS;
            break;
        }

    if(s.pattern)
        has |= SCRHAS_ENEMY;
    else
        for(int q = 0; q < 10; ++q)
            if(s.enemy[q])
            {
                has |= SCRHAS_ENEMY;
                break;
            }

    if(v30)
    {
        if(s.noreset != U_DEF_NORESET || s.nocarry != U_DEF_NOCARRY || s.nextmap || s.nextscr)
            has |= SCRHAS_CARRY;
    }
    else if(s.noreset || s.nocarry || s.nextmap || s.nextscr)
        has |= SCRHAS_CARRY;

    for(int q = 0; q < 128; ++q)
        if(s.secretcombo[q] || s.secretcset[q] || s.secretflag[q])
        {
            has |= SCRHAS_SECRETS;
            break;
        }

    for(int q = 0; q < SQUARES; ++q)
        if(s.data[q] || s.cset[q] || s.sflag[q])
        {
            has |= SCRHAS_COMBOFLAG;
            break;
        }

    if(s.color || s.csensitive != 1 || s.oceansfx || s.bosssfx || s.secretsfx || s.holdupsfx || s.timedwarptics ||
       (v30 ? music != -1 : s.screen_midi != -1) || s.lens_layer)
        has |= SCRHAS_MISC;

    o.p32(has);

    if(has & SCRHAS_ROOMDATA)
    {
        o.p8(s.guy);
        o.p32((dword)s.guytile);
        o.p8(s.guycs);
        o.p16(s.roomflags);
        o.p16(s.str);
        o.p8(s.room);
        o.p16(s.catchall);
    }

    if(has & SCRHAS_ITEM)
    {
        if(v30)
            o.p16(s.item);
        else
            o.p8((byte)s.item);

        o.p8(s.hasitem);
        o.p8(s.itemx);
        o.p8(s.itemy);
    }

    if(has & (SCRHAS_SWARP | SCRHAS_TWARP))
        o.p16(s.warpreturnc);

    if(has & SCRHAS_TWARP)
    {
        o.pn(s.tilewarptype, 4);
        for(int k = 0; k < 4; ++k) o.p16(s.tilewarpdmap[k]);
        o.pn(s.tilewarpscr, 4);
        o.p8(s.tilewarpoverlayflags);
    }

    if(has & SCRHAS_SWARP)
    {
        o.pn(s.sidewarptype, 4);
        for(int k = 0; k < 4; ++k) o.p16(s.sidewarpdmap[k]);
        o.pn(s.sidewarpscr, 4);
        o.p8(s.sidewarpoverlayflags);
        o.p8(s.sidewarpindex);
    }

    if(has & SCRHAS_WARPRET)
    {
        o.pn(s.warpreturnx, 4);
        o.pn(s.warpreturny, 4);
        o.p8(s.warparrivalx);
        o.p8(s.warparrivaly);
    }

    if(has & SCRHAS_LAYERS)
    {
        o.pn(s.layermap, 6);
        o.pn(s.layerscreen, 6);
        o.pn(s.layeropacity, 6);
        o.p8(0);        // hidden layers
        o.p8(0);        // layers hidden by scripts
    }

    if(has & SCRHAS_MAZE)
    {
        o.pn(s.path, 4);
        o.p8(s.exitdir);

        if(v30)
            o.p8(0);    // maze transition wipe
    }

    if(has & SCRHAS_D_S_U)
    {
        o.p16(s.door_combo_set);
        o.pn(s.door, 4);
        o.p8(s.stairx);
        o.p8(s.stairy);
        o.p16(s.undercombo);
        o.p8(s.undercset);
    }

    if(has & SCRHAS_FLAGS)
        o.pn(s.flags, 11);

    if(has & SCRHAS_ENEMY)
    {
        for(int k = 0; k < 10; ++k) o.p16(s.enemy[k]);
        o.p8(s.pattern);
    }

    if(has & SCRHAS_CARRY)
    {
        if(v30)
        {
            o.p32(s.noreset);
            o.p32(s.nocarry);
            o.p32(0);   // extra states reset
            o.p32(0);   // and carried over
        }
        else
        {
            o.p16((word)s.noreset);
            o.p16((word)s.nocarry);
        }

        o.p8(s.nextmap);
        o.p8(s.nextscr);
    }

    if(has & SCRHAS_SECRETS)
    {
        for(int k = 0; k < 128; ++k) o.p16(s.secretcombo[k]);
        o.pn(s.secretcset, 128);
        o.pn(s.secretflag, 128);
    }

    if(has & SCRHAS_COMBOFLAG)
    {
        for(int k = 0; k < SQUARES; ++k) o.p16(s.data[k]);
        o.pn(s.sflag, SQUARES);
        o.pn(s.cset, SQUARES);
    }

    if(has & SCRHAS_MISC)
    {
        o.p16(s.color);
        o.p8(s.csensitive);

        if(v30)
        {
            o.p16(s.oceansfx);
            o.p16(s.bosssfx);
            o.p16(s.secretsfx);
            o.p16(s.holdupsfx);
        }
        else
        {
            o.p8((byte)s.oceansfx);
            o.p8((byte)s.bosssfx);
            o.p8((byte)s.secretsfx);
            o.p8((byte)s.holdupsfx);
        }

        o.p16(s.timedwarptics);

        if(v30)
            o.p32((dword)music);
        else
            o.p16((word)s.screen_midi);

        o.p8(s.lens_layer);
        o.p8(0);        // lens shows
        o.p8(0);        // lens hides
    }

    // qr_OLD_FFC_FUNCTIONALITY, on for every old quest: all 32 FFC slots
    o.p16(OLD_FFCS);

    for(int k = 0; k < OLD_FFCS; ++k)
    {
        const UFfc &c = s.ffc[k];
        o.p16(c.data);

        if(!c.data)
            continue;

        o.p8(c.cset);
        o.p16(c.delay);
        o.p32((dword)c.x);
        o.p32((dword)c.y);
        o.p32((dword)c.vx);
        o.p32((dword)c.vy);
        o.p32((dword)c.ax);
        o.p32((dword)c.ay);
        o.p8(c.link);
        o.p32((dword)c.hit_width);
        o.p32((dword)c.hit_height);
        o.p8(c.txsz);
        o.p8(c.tysz);
        o.p32(c.flags);

        if(v30)
        {
            // the script and its run arguments (InitD), no instance settings
            o.p16(c.script);
            o.p16(8);
            for(int q = 0; q < 8; ++q) o.p32((dword)c.initd[q]);
            o.p16(0);
            o.p8(1);        // layer
            o.p32(0);       // viewport suspend range
            o.p32(0);       // viewport despawn range
        }
        else
        {
            o.p16(c.script);
            for(int q = 0; q < 8; ++q) o.p32((dword)c.initd[q]);
            o.p8((byte)(c.inita[0] / 10000));
            o.p8((byte)(c.inita[1] / 10000));
        }
    }

    o.p32(0);           // the user's notes

    // 3.0 writes a screen gravity when flags10 has either of its two low bits
    if(v30 && (s.flags[9] & 0x03))
    {
        o.p32(0);
        o.p32(0);
    }
}

// ---- the writers -------------------------------------------------------------------------

int zqn_maps(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    edit_test_once();

    std::vector<UScr> scr;

    if(!final_screens(ctx, scr))
        return refuse(ERR_READ, "the maps of the quest as opened couldn't be read again");

    const int target = ctx.target;
    const int maps = map_count < MAXMAPS2 ? map_count : MAXMAPS2;

    // 3.0: each screen's music, given out in the order the screens are read (all of them, valid
    // or not) after the DMaps'
    std::vector<long> music(scr.size(), -1);

    if(target == ZQ_NEWER_30)
    {
        std::vector<UDmap> dm;
        int file_count = 0, dmap_entries = 0;

        if(!final_dmaps(ctx, dm, &file_count))
            return refuse(ERR_READ, "the DMaps of the quest as opened couldn't be read again");

        for(int i = 0; i < MAXDMAPS; ++i)
            if(dmap_has_music_30(dm[i]))
                ++dmap_entries;

        std::vector<int> midis;

        for(size_t idx = 0; idx < scr.size(); ++idx)
            music[idx] = zqn_screen_music_index(scr[idx].screen_midi, dmap_entries, midis);
    }

    Out o;
    o.p16(map_count);

    for(int i = 0; i < maps; ++i)
    {
        byte valid = 0;

        for(int j = 0; j < MAPSCRS && !valid; ++j)
            if(scr[(size_t)i * MAPSCRS + j].valid & mVALID)
                valid = 1;

        o.p8(valid);

        if(!valid)
            continue;

        for(int q = 0; q < 6; ++q)
            o.p16(0);           // auto layers

        if(target == ZQ_NEWER_30)
        {
            o.p16(0);           // auto palette
            o.zeros(64);        // scrolling regions
        }

        for(int j = 0; j < MAPSCRS; ++j)
            write_screen(o, scr[(size_t)i * MAPSCRS + j], j, target, music[(size_t)i * MAPSCRS + j]);
    }

    if(target == ZQ_NEWER_30)
        return put_section(f, ID_MAPS, 41, 41, o);

    return put_section(f, ID_MAPS, 30, 9, o);
}

ZqNewerRegistration g_reg_maps(ID_MAPS, zqn_maps, "maps for 2.55.17 and 3.0");

// count_dmaps (2.55.17 and 3.0): the DMaps up to the last that isn't empty
int count_dmaps_u(const std::vector<UDmap> &d, const std::vector<int> &music, int target)
{
    for(int i = MAXDMAPS - 1; i >= 0; --i)
    {
        const UDmap &m = d[i];
        bool found = m.map || m.level || m.xoff || m.compass || m.color || m.cont || m.type;

        if(target == ZQ_NEWER_30)
            found = found || music[i];
        else
            found = found || m.midi || m.tmusic[0];

        for(int j = 0; j < 8; ++j)
            found = found || m.grid[j];

        found = found || m.name[0] || m.title[0] || m.intro[0];

        for(int t = 0; t < 4; ++t)
            found = found || m.tile[t] || m.tcset[t];

        if(found)
            return i + 1;
    }

    return 0;
}

void put_script_config(Out &o)
{
    o.p16(0);       // script
    o.p16(8);       // run arguments
    o.zeros(32);
    o.p16(0);       // instance settings
}

int zqn_dmaps(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    edit_test_once();

    std::vector<UDmap> dm;
    int file_count = 0;

    if(!final_dmaps(ctx, dm, &file_count))
        return refuse(ERR_READ, "the DMaps of the quest as opened couldn't be read again");

    const bool v30 = ctx.target == ZQ_NEWER_30;
    std::vector<int> music(MAXDMAPS, 0);

    if(v30)
    {
        int next = 0;

        for(int i = 0; i < MAXDMAPS; ++i)
            if(dmap_has_music_30(dm[i]))
                music[i] = ++next;
    }

    const int count = count_dmaps_u(dm, music, ctx.target);
    Out o;
    o.p16((word)count);

    for(int i = 0; i < count; ++i)
    {
        const UDmap &d = dm[i];
        o.p8(d.map);
        o.p16(d.level);
        o.p8((byte)d.xoff);
        o.p8(d.compass);
        o.p16(d.color);

        if(!v30)
            o.p8(d.midi);

        o.p8(d.cont);
        o.p8(d.type);
        o.pn(d.grid, 8);
        o.pn(d.name, 21);
        size_t tl = strlen(d.title);
        o.p16((word)tl);
        o.pn(d.title, tl);
        o.pn(d.intro, 73);

        for(int t = 0; t < 4; ++t)
        {
            o.p32((dword)d.tile[t]);
            o.p8(d.tcset[t]);
        }

        if(!v30)
        {
            o.pn(d.tmusic, 56);
            o.p8(d.tmusictrack);
        }

        o.p8(d.active_subscreen);
        o.p8(d.passive_subscreen);

        if(v30)
        {
            // the disabled items as a bitstring (p_putbitstr): up to the last byte that has any
            int cap = 0, nz = 0;

            for(int q = 0; q < 32; ++q)
                if(d.disabled[q])
                {
                    cap = q + 1;
                    ++nz;
                }

            const bool pairs = nz * 3 <= cap;
            o.p16(0xFFFF);
            o.p8(pairs ? 1 : 0);
            o.p16((word)(pairs ? nz : cap));

            for(int q = 0; q < cap; ++q)
            {
                if(pairs)
                {
                    if(!d.disabled[q])
                        continue;

                    o.p16((word)q);
                }

                o.p8(d.disabled[q]);
            }

            o.p32((dword)d.flags);
            o.p8((byte)d.sideview);

            for(int k = 0; k < 4; ++k)
                put_script_config(o);    // the DMap's, its subscreens' and its map's scripts

            o.p16((word)d.mirrordmap);
            o.p8(0);                    // overlay subscreen
            o.p32(0);                   // intro string

            if(d.flags & U_DMF_CUSTOM_GRAVITY)
            {
                o.p32(0);
                o.p32(0);
            }

            if(d.flags & U_DMF_CUSTOM_VIEWPORT)
            {
                // ViewportFollowSettings' defaults
                o.p8(0);
                o.p8(0);
                o.p8(0);
                o.p8(0);
                o.p32(10000);
                o.p32(0);
                o.p16(30);
            }

            o.p16(0xFFFF);              // no map subscreen
            o.p8(0);                    // floor
            o.p16((word)music[i]);
        }
        else
        {
            o.pn(d.disabled, 32);
            o.p32((dword)d.flags);
            o.p8((byte)d.sideview);
            o.p16(0);                   // script
            o.zeros(32 + 8 * 65);       // its InitD and their labels
            o.p16(0);                   // active subscreen script
            o.p16(0);                   // passive subscreen script
            o.zeros(32 + 8 * 65);
            o.p16(0);                   // on-map script
            o.zeros(32 + 8 * 65);
            o.p16((word)d.mirrordmap);
            o.zeros(16);                // enhanced music loop points and fades
            o.p8(0);                    // overlay subscreen
            o.p32(0);                   // intro string
            o.zeros(64);                // reserved
        }
    }

    if(v30)
        return put_section(f, ID_DMAPS, 28, 28, o);

    return put_section(f, ID_DMAPS, 21, 1, o);
}

ZqNewerRegistration g_reg_dmaps(ID_DMAPS, zqn_dmaps, "DMaps for 2.55.17 and 3.0");

} // namespace

int zqn_screen_music_index(short screen_midi, int dmap_entries, std::vector<int> &midis)
{
    // vbound(m, 0, -1) for none or the DMap's music; otherwise find_or_make_midi_music
    if(screen_midi <= 0)
        return screen_midi < -1 ? -1 : screen_midi;

    int midi = screen_midi_30(screen_midi);

    if(midi == 0)
        return 0;

    for(size_t k = 0; k < midis.size(); ++k)
        if(midis[k] == midi)
            return dmap_entries + (int)k + 1;

    midis.push_back(midi);
    return dmap_entries + (int)midis.size();
}

bool zqn_music_entries(const ZqNewerContext &ctx, std::vector<ZqnMusicEntry> &out)
{
    edit_test_once();
    out.clear();
    std::vector<UDmap> dm;
    int file_count = 0;

    if(!final_dmaps(ctx, dm, &file_count))
        return false;

    for(int i = 0; i < MAXDMAPS; ++i)
    {
        if(!dmap_has_music_30(dm[i]))
            continue;

        ZqnMusicEntry e;
        char name[32];
        sprintf(name, "DMap %d", i);
        e.name = name;
        e.midi = dmap_midi_30(dm[i]);
        e.path = dm[i].tmusic;
        e.track = dm[i].tmusictrack;
        out.push_back(e);
    }

    std::vector<UScr> scr;

    if(!final_screens(ctx, scr))
        return false;

    const int dmap_entries = (int)out.size();
    std::vector<int> midis;
    const int maps = map_count < MAXMAPS2 ? map_count : MAXMAPS2;

    for(int i = 0; i < maps; ++i)
        for(int j = 0; j < MAPSCRS; ++j)
            zqn_screen_music_index(scr[(size_t)i * MAPSCRS + j].screen_midi, dmap_entries, midis);

    for(size_t k = 0; k < midis.size(); ++k)
    {
        ZqnMusicEntry e;
        char name[32];
        sprintf(name, "MIDI %d", midis[k]);
        e.name = name;
        e.midi = midis[k];
        e.track = 0;
        out.push_back(e);
    }

    return true;
}

bool zqn_made_door_sets(const ZqNewerContext &ctx, std::vector<DoorComboSet> &out)
{
    edit_test_once();
    out.clear();
    std::vector<UScr> scr;
    std::vector<DoorSet> sets;

    if(!final_screens(ctx, scr, &sets))
        return false;

    for(size_t k = 0; k < sets.size(); ++k)
    {
        const DoorSet &d = sets[k];
        DoorComboSet c;
        memset(&c, 0, sizeof(c));
        snprintf(c.name, sizeof(c.name), "Door Combo Set %d", (int)k);
        memcpy(c.doorcombo_u, d.combo_u, sizeof(c.doorcombo_u));
        memcpy(c.doorcset_u, d.cset_u, sizeof(c.doorcset_u));
        memcpy(c.doorcombo_d, d.combo_d, sizeof(c.doorcombo_d));
        memcpy(c.doorcset_d, d.cset_d, sizeof(c.doorcset_d));
        memcpy(c.doorcombo_l, d.combo_l, sizeof(c.doorcombo_l));
        memcpy(c.doorcset_l, d.cset_l, sizeof(c.doorcset_l));
        memcpy(c.doorcombo_r, d.combo_r, sizeof(c.doorcombo_r));
        memcpy(c.doorcset_r, d.cset_r, sizeof(c.doorcset_r));
        memcpy(c.bombdoorcombo_u, d.bomb_u, sizeof(c.bombdoorcombo_u));
        memcpy(c.bombdoorcset_u, d.bombcset_u, sizeof(c.bombdoorcset_u));
        memcpy(c.bombdoorcombo_d, d.bomb_d, sizeof(c.bombdoorcombo_d));
        memcpy(c.bombdoorcset_d, d.bombcset_d, sizeof(c.bombdoorcset_d));
        memcpy(c.bombdoorcombo_l, d.bomb_l, sizeof(c.bombdoorcombo_l));
        memcpy(c.bombdoorcset_l, d.bombcset_l, sizeof(c.bombdoorcset_l));
        memcpy(c.bombdoorcombo_r, d.bomb_r, sizeof(c.bombdoorcombo_r));
        memcpy(c.bombdoorcset_r, d.bombcset_r, sizeof(c.bombdoorcset_r));
        memcpy(c.walkthroughcombo, d.walk, sizeof(c.walkthroughcombo));
        memcpy(c.walkthroughcset, d.walkcset, sizeof(c.walkthroughcset));
        out.push_back(c);
    }

    return true;
}

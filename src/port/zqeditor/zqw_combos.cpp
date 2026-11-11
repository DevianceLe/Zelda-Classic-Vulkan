// ZQuest's writers for the combo sections, for quests saved in a version other than 2.53:
// CMBO (combos) and CMBA (combo aliases). See zq_writers.h.
//
// Each writer walks the part as it was loaded, one combo or alias at a time. A record
// nobody changed is copied from the original bytes. A changed one is encoded from ZQuest's
// data in that version's format, then read back the way the loader reads it: if the quest
// would not load the same, the format can't hold the change, and the save fails with an
// error instead of losing it (the details go to allegro.log).
//
// Quests with a 2.53.0/2.53.1 header are left to ZQuest's own 2.53 writers, so they save
// exactly as before.
//
// 2.55 and 3.0 combos have far more in them than newcombo (attributes, triggers, scripts and
// the rest, kept in the Q30Combo table for the player). ZQuest edits only what newcombo
// holds, so a changed 2.55 combo is its original record with those fields put in again; a
// field that still reads back as what ZQuest holds keeps its original bytes, so upstream's
// type and flag numbers past 2.53's survive.

#include "precompiled.h" //always first
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "tiles.h"
#include "zq_writers.h"
#include "q255.h"
#include "q30_triggers.h"

extern newcombo *combobuf;
extern combo_alias combo_aliases[MAXCOMBOALIASES];
extern byte quest_rules[QUESTRULES_SIZE];

// q255_combos.cpp, for ZQuest only: one combo read as the 2.55 loader reads it, keeping nothing.
int q255_editor_read_combo(Q255Section &s, newcombo *c, Q30Combo *x);

namespace
{

// ---- helpers ----------------------------------------------------------------------------

enum { ERR_READ = 1, ERR_CANT_HOLD = 2, ERR_WRITE = 3 };

bool is_253(const ZqPartContext &c)
{
    return c.zelda_version == 0x250 && c.build >= 31 && c.build <= 33;
}

bool part_bytes(const ZqQuestPart &p, std::vector<byte> &out)
{
    out.resize((size_t)p.length);
    return p.length == 0 || zq_part_read(p, 0, &out[0], (size_t)p.length);
}

struct In
{
    const byte *d;
    size_t n, pos;
    bool bad;

    In(const std::vector<byte> &v, size_t at) : d(v.empty() ? NULL : &v[0]), n(v.size()), pos(at), bad(false) {}
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
    void copy(const std::vector<byte> &src, size_t from, size_t to)
    {
        if(to > from)
            b.insert(b.end(), src.begin() + from, src.begin() + to);
    }
};

dword le32(const std::vector<byte> &v, size_t at)
{
    return (dword)v[at] | ((dword)v[at + 1] << 8) | ((dword)v[at + 2] << 16) | ((dword)v[at + 3] << 24);
}

void set16(std::vector<byte> &v, size_t at, word x)
{
    v[at] = (byte)x;
    v[at + 1] = (byte)(x >> 8);
}

void set32(std::vector<byte> &v, size_t at, dword x)
{
    set16(v, at, (word)x);
    set16(v, at + 2, (word)(x >> 16));
}

// The whole part: its id, the version and cversion as they were, the size grown or shrunk
// by as much as the data (so a size an old ZQuest got wrong stays wrong by the same amount),
// then the data.
int put_part(PACKFILE *f, dword id, const std::vector<byte> &orig, const Out &body)
{
    if(orig.size() < 8)
        return ERR_READ;

    int64_t size = (int64_t)le32(orig, 4) + (int64_t)body.b.size() - (int64_t)(orig.size() - 8);
    Out head;
    head.pn(&orig[0], 4);
    head.p32((dword)size);

    if(pack_mputl((long)id, f) == EOF || pack_fwrite(&head.b[0], 8, f) != 8)
        return ERR_WRITE;

    if(!body.b.empty() && pack_fwrite(&body.b[0], (long)body.b.size(), f) != (long)body.b.size())
        return ERR_WRITE;

    return ZQW_OK;
}

// ZQW_STRICT (for testing the writers): a record that doesn't read back as ZQuest holds it
// fails the save even when nobody changed it, instead of being copied from the original.
bool strict()
{
    static int s = -1;

    if(s < 0)
        s = getenv("ZQW_STRICT") ? 1 : 0;

    return s == 1;
}

// ZQW_EXACT (for testing the encoders): an unchanged record encoded again must come out as
// its original bytes, or it counts as not reading back.
bool exact_differs(dword id, int index, bool unchanged, const std::vector<byte> &rec, const std::vector<byte> &src,
                   size_t from, size_t to)
{
    static int s = -1;

    if(s < 0)
        s = getenv("ZQW_EXACT") ? 1 : 0;

    if(s != 1 || !unchanged)
        return false;

    if(rec.size() == to - from && (rec.empty() || !memcmp(&rec[0], &src[from], rec.size())))
        return false;

    size_t k = 0;

    while(k < rec.size() && from + k < to && rec[k] == src[from + k])
        ++k;

    al_trace("zqw %c%c%c%c: record %d encodes differently from byte %d (%d bytes, was %d)\n", (char)(id >> 24),
             (char)(id >> 16), (char)(id >> 8), (char)id, index, (int)k, (int)rec.size(), (int)(to - from));
    return true;
}

// ZQW_MUTATE (for testing the writers on changed records): right after a load, once the
// records as loaded are noted, some of them are changed. Every change sets a field to a
// value the record already holds elsewhere, so it is the same after the next load, and the
// round-trip check then compares the changed quest with itself.
bool mutate()
{
    static int s = -1;

    if(s < 0)
        s = getenv("ZQW_MUTATE") ? 1 : 0;

    return s == 1;
}

const char *tag_of(dword id)
{
    static char t[5];

    for(int k = 0; k < 4; ++k)
        t[k] = (char)(id >> (24 - 8 * k));

    t[4] = 0;
    return t;
}

// A changed record that doesn't read back as ZQuest holds it, or an unchanged one when
// ZQW_STRICT is set: what happened goes to allegro.log, and true means fail the save.
const char *section_words(dword id)
{
    switch(id)
    {
    case ID_COMBOS: return "combo";
    case ID_COMBOALIASES: return "combo alias";
    case ID_MAPS: return "screen";
    case ID_DMAPS: return "DMap";
    case ID_DOORS: return "door combo set";
    }

    return "record";
}

bool refuse(dword id, int index, bool unchanged, const char *what)
{
    // a changed record the save stops on: the reason the user sees (zq_writers.h)
    if(!unchanged)
    {
        char buf[200];
        snprintf(buf, sizeof(buf), "%s %d %s", section_words(id), index, what);
        zqw_last_error = buf;
    }

    al_trace("zqw %s: record %d %s%s\n", tag_of(id), index, what,
             unchanged && !strict() ? " (unchanged, its original bytes are kept)" : "");
    return !unchanged || strict();
}

// ---- CMBO: what 2.53 holds of a combo ---------------------------------------------------

// The fields of newcombo a quest file holds, to tell whether two combos are the same.
// The tile goes in whole: some files hold more than 16 bits of it.
void combo_key(Out &o, const newcombo &c)
{
    o.p32((dword)c.tile);
    o.p8(c.flip);
    o.p8(c.walk);
    o.p8(c.type);
    o.p8(c.csets);
    o.p8(c.frames);
    o.p8(c.speed);
    o.p16(c.nextcombo);
    o.p8(c.nextcset);
    o.p8(c.flag);
    o.p8(c.skipanim);
    o.p16(c.nexttimer);
    o.p8(c.skipanimy);
    o.p8(c.animflags);
}

const size_t COMBO_KEY = 19;

std::vector<byte> g_combos_loaded;     // every combo as loaded, as combo_key has it

void note_combos()
{
    reset_combo_animations();       // as a save does, so animated combos show their own tiles
    reset_combo_animations2();
    Out o;
    o.b.reserve((size_t)MAXCOMBOS * COMBO_KEY);

    for(int i = 0; i < MAXCOMBOS; ++i)
        combo_key(o, combobuf[i]);

    g_combos_loaded.swap(o.b);

    if(mutate())
        for(int i = 3; i < MAXCOMBOS; i += 7)
            if(combobuf[i].tile)
            {
                combobuf[i].nextcset = combobuf[i].csets;
                combobuf[i].flip |= 1;
                combobuf[i].speed = combobuf[i].frames;
            }
}

ZqLoadHookRegistration g_hook_combos(note_combos);

bool same_combo(const newcombo &a, const newcombo &b)
{
    Out x, y;
    combo_key(x, a);
    combo_key(y, b);
    return x.b == y.b;
}

bool combo_unchanged(int i)
{
    if(i < 0 || i >= MAXCOMBOS || g_combos_loaded.size() < (size_t)(i + 1) * COMBO_KEY)
        return false;

    Out o;
    combo_key(o, combobuf[i]);
    return !memcmp(&o.b[0], &g_combos_loaded[(size_t)i * COMBO_KEY], COMBO_KEY);
}

// How many combos to write: those in the file, and any after them that were changed (the
// ones past the end load empty, and stay so when they aren't written).
int combo_count(int in_file)
{
    for(int i = MAXCOMBOS - 1; i >= in_file; --i)
        if(!combo_unchanged(i))
            return i + 1;

    return in_file;
}

// ---- CMBO: 2.53's readcombos (quests before 2.55) -----------------------------------------

struct ComboRead
{
    word sv;
    bool swap_goriya;   // Goriya tiles were swapped round in 2.11 build 7
};

// The slashable types before combos v7 became the "touchy" ones at load.
const byte g_untouchy[8][2] =
{
    { cSLASH, cSLASHTOUCHY }, { cSLASHITEM, cSLASHITEMTOUCHY }, { cBUSH, cBUSHTOUCHY },
    { cFLOWERS, cFLOWERSTOUCHY }, { cTALLGRASS, cTALLGRASSTOUCHY }, { cSLASHNEXT, cSLASHNEXTTOUCHY },
    { cSLASHNEXTITEM, cSLASHNEXTITEMTOUCHY }, { cBUSHNEXT, cBUSHNEXTTOUCHY }
};

tile_id swap_goriya(tile_id t)
{
    switch(t)
    {
    case 130: return 132;
    case 131: return 133;
    case 132: return 130;
    case 133: return 131;
    }

    return t;
}

// One combo as readcombos reads it from a sectioned quest, with the conversions it makes
// over the whole table afterwards (header 1.93 or later: no padding, no hookshot fix).
void read_combo_old(In &in, const ComboRead &r, newcombo &c)
{
    memset(&c, 0, sizeof(c));
    c.tile = in.g16();
    c.flip = in.g8();
    c.walk = in.g8();
    c.type = in.g8();
    c.csets = in.g8();
    c.frames = in.g8();
    c.speed = in.g8();
    c.nextcombo = in.g16();
    c.nextcset = in.g8();

    if(r.sv >= 3)
        c.flag = in.g8();

    if(r.sv >= 4)
    {
        c.skipanim = in.g8();
        c.nexttimer = in.g16();
    }

    if(r.sv >= 5)
        c.skipanimy = in.g8();

    if(r.sv >= 6)
    {
        c.animflags = in.g8();

        if(r.sv == 6)
            c.animflags = c.animflags ? AF_FRESH : 0;
    }

    if(r.swap_goriya)
        c.tile = swap_goriya(c.tile);

    if(r.sv < 7)
        for(int k = 0; k < 8; ++k)
            if(c.type == g_untouchy[k][0])
            {
                c.type = g_untouchy[k][1];
                break;
            }
}

// raw/loaded (or NULL): the original record and what it reads as. A type both a slashable
// and its touchy form read as, and v6's animation flags (any value reads as AF_FRESH),
// keep their original bytes while they still read as ZQuest's.
void write_combo_old(Out &o, const ComboRead &r, const newcombo &c, const std::vector<byte> *raw = NULL,
                     size_t raw_at = 0, const newcombo *loaded = NULL)
{
    const size_t start = o.b.size();
    tile_id tile = r.swap_goriya ? swap_goriya(c.tile) : c.tile;
    byte type = c.type;

    if(r.sv < 7)
        for(int k = 0; k < 8; ++k)
            if(type == g_untouchy[k][1])
            {
                type = g_untouchy[k][0];
                break;
            }

    o.p16((word)tile);
    o.p8(c.flip);
    o.p8(c.walk);
    o.p8(type);
    o.p8(c.csets);
    o.p8(c.frames);
    o.p8(c.speed);
    o.p16(c.nextcombo);
    o.p8(c.nextcset);

    if(r.sv >= 3)
        o.p8(c.flag);

    if(r.sv >= 4)
    {
        o.p8(c.skipanim);
        o.p16(c.nexttimer);
    }

    if(r.sv >= 5)
        o.p8(c.skipanimy);

    if(r.sv >= 6)
        o.p8(c.animflags);

    if(!raw || !loaded || raw_at + o.b.size() - start > raw->size())
        return;

    if(loaded->type == c.type)
        o.b[start + 4] = (*raw)[raw_at + 4];

    if(r.sv >= 6 && loaded->animflags == c.animflags)
        o.b.back() = (*raw)[raw_at + o.b.size() - 1 - start];
}

// Types a version's file can hold. The loader can't tell an old type from a newer one, but
// combos before v7 had no touchy slashables (the loader makes them), so the types after
// those in the list (four-way eyeball, tall grass->next, the script types) are 2.50's.
bool combo_type_fits(word sv, byte type)
{
    return sv >= 7 || type < cEYEBALL_4;
}

int write_combos_old(PACKFILE *f, const ZqPartContext &ctx)
{
    if(is_253(ctx) || ctx.zelda_version < 0x193 || ctx.zelda_version >= 0x255 || ctx.version > V_COMBOS)
        return ZQW_UNSUPPORTED;

    std::vector<byte> orig;

    if(!part_bytes(*ctx.part, orig) || orig.size() < 10)
        return ERR_READ;

    reset_combo_animations();
    reset_combo_animations2();
    ComboRead r;
    r.sv = ctx.version;
    r.swap_goriya = (ctx.zelda_version < 0x211 || (ctx.zelda_version == 0x211 && ctx.build < 7)) &&
                    !get_bit(quest_rules, qr_NEWENEMYTILES);
    In in(orig, 8);
    const int old_count = in.g16();
    const int count = combo_count(old_count);
    Out o;
    o.p16((word)count);
    newcombo loaded, got;

    for(int i = 0; i < count; ++i)
    {
        size_t start = in.pos;

        if(i < old_count)
        {
            read_combo_old(in, r, loaded);

            if(in.bad)
                return ERR_READ;
        }

        const bool unchanged = i < old_count && combo_unchanged(i);

        if(unchanged && !zqw_force_serialize)
        {
            o.copy(orig, start, in.pos);
            continue;
        }

        // first with the original bytes of what reads the same, then without
        Out rec;
        bool ok = false;

        for(int pass = i < old_count ? 0 : 1; pass < 2 && !ok; ++pass)
        {
            rec.b.clear();
            write_combo_old(rec, r, combobuf[i], pass == 0 ? &orig : NULL, start, pass == 0 ? &loaded : NULL);
            In back(rec.b, 0);
            read_combo_old(back, r, got);
            ok = !back.bad && same_combo(combobuf[i], got);
        }

        if(ok && combo_type_fits(r.sv, combobuf[i].type) && !exact_differs(ID_COMBOS, i, unchanged, rec.b, orig, start, in.pos))
            o.pn(&rec.b[0], rec.b.size());
        else if(refuse(ID_COMBOS, i, unchanged, "can't be held in this version"))
            return ERR_CANT_HOLD;
        else
            o.copy(orig, start, in.pos);
    }

    o.copy(orig, in.pos, orig.size());
    return put_part(f, ctx.part->id, orig, o);
}

ZqWriterRegistration g_reg_combos_old(ID_COMBOS, write_combos_old, "combos, readcombos versions");

// ---- CMBO: 2.55 and 3.0 (q255_read_combos) ----------------------------------------------

enum { CHAS_ANIM = 0x0008, CHAS_SCRIPT = 0x0010, CHAS_BASIC = 0x0020 };

// Reads one combo as q255_read_combos does; false when it can't.
bool read_combo_255(const std::vector<byte> &body, size_t at, word v, size_t *end, newcombo &c)
{
    Q255Section s;
    s.data = body.empty() ? NULL : &body[0];
    s.size = body.size();
    s.pos = at;
    s.version = v;
    s.keep = false;
    Q30Combo x;
    int ret = q255_editor_read_combo(s, &c, &x);
    *end = s.pos;
    return ret == 0 && !s.failed;
}

// Where the fields 2.53 holds sit in a combo of the old flat format (v7-32).
struct FlatLayout
{
    size_t tile_bytes;      // 2 before v11, then 4
    size_t o_tile;          // from v15, where the animation's first tile is; 0 if none
    size_t bytes;           // the whole record
};

FlatLayout flat_layout(word v)
{
    FlatLayout l;
    l.tile_bytes = v >= 11 ? 4 : 2;
    size_t at = l.tile_bytes + 15;  // tile up to animflags

    if(v >= 8) at += 16 + 4 + (v >= 20 ? 2 : 0);   // attributes, usrflags, genflags
    if(v >= 10) at += 12;                           // trigger flags
    else if(v == 9) at += 8;
    if(v >= 9) at += 4;                             // the trigger fields, as they grew
    if(v >= 22) at += 1;
    if(v >= 24) at += 2;
    if(v >= 25) at += 1;
    if(v >= 27) at += 4;
    if(v >= 29) at += 7;
    if(v >= 30) at += 1;
    if(v >= 31) at += 11;
    if(v >= 32) at += 1;
    if(v >= 12) at += 11;                           // label
    if(v >= 13) at += 4;                            // attribytes 0-3
    if(v >= 14) at += 10;                           // script and two InitDs

    l.o_tile = v >= 15 ? at : 0;

    if(v >= 15) at += 4 + 2;                        // o_tile, cur_frame, aclk
    if(v >= 17) at += 4 + 16;                       // attribytes 4-7, attrishorts

    l.bytes = at;
    return l;
}

// Upstream's flag numbers: its mfPUSHED is 104, 2.53's comes after its mfMAX.
byte flag_255(byte flag)
{
    return flag == mfPUSHED ? (byte)104 : flag;
}

void set_flat_tile(std::vector<byte> &rec, word v, tile_id tile)
{
    FlatLayout l = flat_layout(v);

    if(l.tile_bytes == 4)
        set32(rec, 0, (dword)tile);
    else
        set16(rec, 0, (word)tile);

    if(l.o_tile)
        set32(rec, l.o_tile, (dword)tile);
}

// The flat record (v7-32) with ZQuest's fields put in; was is what the original reads as
// (all zero for a new combo, whose record is all zero too). The fields the reader narrows
// keep their bytes when they still read as ZQuest's value: a type or flag 2.53 has no
// number for reads as none and stays upstream's, and so does a tile past 2.53's.
void patch_flat(std::vector<byte> &rec, word v, const newcombo &was, const newcombo &c, bool fresh)
{
    const size_t t = flat_layout(v).tile_bytes;

    if(fresh)
        rec[t + 1] = 0xF0;  // upstream's default walk: the effect squares on

    if(was.tile != c.tile) set_flat_tile(rec, v, c.tile);
    rec[t] = c.flip;
    rec[t + 1] = (byte)((rec[t + 1] & 0xF0) | c.walk);     // the high nibble: upstream's effect squares
    if(was.type != c.type) rec[t + 2] = c.type;
    rec[t + 3] = c.csets;
    rec[t + 4] = c.frames;
    rec[t + 5] = c.speed;
    set16(rec, t + 6, c.nextcombo);
    rec[t + 8] = c.nextcset;
    if(was.flag != c.flag) rec[t + 9] = flag_255(c.flag);
    rec[t + 10] = c.skipanim;
    set16(rec, t + 11, c.nexttimer);
    rec[t + 13] = c.skipanimy;
    rec[t + 14] = c.animflags;
}

// The v33+ record: which groups it has, then the groups. Only BASIC and ANIM hold fields
// of newcombo; they are put in again (added when the original had none and ZQuest's values
// aren't what an absent group reads as) and the rest is copied.
bool patch_grouped(const std::vector<byte> &raw, word v, const newcombo &was, const newcombo &c, std::vector<byte> &rec)
{
    In in(raw, 0);
    word has = v < 55 ? (word)in.g8() : in.g16();
    const size_t has_bytes = v < 55 ? 1 : 2;
    size_t basic_at = 0, script_at = 0, script_end = 0, anim_at = 0, anim_end = 0;

    if(has & CHAS_BASIC)
    {
        basic_at = in.pos;
        in.skip(9);
    }

    script_at = in.pos;

    if(has & CHAS_SCRIPT)
    {
        if(v >= 41)
            in.skip(in.g8());
        else
            in.skip(11);

        if(v >= 72)
        {
            in.skip(2);
            in.skip((size_t)in.g16() * 4);
            in.skip((size_t)in.g16() * 6);
        }
        else
            in.skip(2 + (v >= 43 ? 8 : 2) * 4);
    }

    script_end = in.pos;

    if(has & CHAS_ANIM)
    {
        anim_at = in.pos;
        in.skip(8 + (v >= 73 ? 3 : 0));
    }

    anim_end = in.pos;

    if(in.bad)
        return false;

    // BASIC: tile, flip, walk, type, flag, csets
    byte basic[9];

    if(has & CHAS_BASIC)
        memcpy(basic, &raw[basic_at], 9);
    else
    {
        memset(basic, 0, 9);
        basic[5] = 0xF0;    // upstream's default walk: the effect squares on
    }

    // as in patch_flat, the narrowed fields keep their bytes while they read as ZQuest's
    if(was.tile != c.tile)
    {
        basic[0] = (byte)c.tile;
        basic[1] = (byte)(c.tile >> 8);
        basic[2] = (byte)(c.tile >> 16);
        basic[3] = (byte)(c.tile >> 24);
    }

    basic[4] = c.flip;
    basic[5] = (byte)((basic[5] & 0xF0) | c.walk);
    if(was.type != c.type) basic[6] = c.type;
    if(was.flag != c.flag) basic[7] = flag_255(c.flag);
    basic[8] = c.csets;

    const bool basic_now = (has & CHAS_BASIC) || c.tile || c.flip || c.walk || c.type || c.flag || c.csets;

    // ANIM: frames, speed, nextcombo, nextcset, skipanim, skipanimy, animflags, then from
    // v73 a delay and a flash duration
    byte anim[11];
    const size_t anim_bytes = 8 + (v >= 73 ? 3 : 0);

    if(has & CHAS_ANIM)
        memcpy(anim, &raw[anim_at], anim_bytes);
    else
        memset(anim, 0, sizeof(anim));

    anim[0] = c.frames;
    anim[1] = c.speed;
    anim[2] = (byte)c.nextcombo;
    anim[3] = (byte)(c.nextcombo >> 8);
    anim[4] = c.nextcset;
    anim[5] = c.skipanim;
    anim[6] = c.skipanimy;
    anim[7] = c.animflags;

    const bool anim_now = (has & CHAS_ANIM) || c.frames || c.speed || c.nextcombo || c.nextcset || c.skipanim ||
                          c.skipanimy || c.animflags;

    if(basic_now) has |= CHAS_BASIC;
    if(anim_now) has |= CHAS_ANIM;

    rec.clear();
    rec.push_back((byte)has);

    if(has_bytes == 2)
        rec.push_back((byte)(has >> 8));

    if(basic_now)
        rec.insert(rec.end(), basic, basic + 9);

    rec.insert(rec.end(), raw.begin() + script_at, raw.begin() + script_end);

    if(anim_now)
        rec.insert(rec.end(), anim, anim + anim_bytes);

    rec.insert(rec.end(), raw.begin() + anim_end, raw.end());
    return true;
}

int write_combos_255(PACKFILE *f, const ZqPartContext &ctx)
{
    const word v = ctx.version;

    if(ctx.zelda_version != 0x255 || v < 7 || v > 73)
        return ZQW_UNSUPPORTED;

    std::vector<byte> orig;

    if(!part_bytes(*ctx.part, orig) || orig.size() < 10)
        return ERR_READ;

    reset_combo_animations();
    reset_combo_animations2();
    const std::vector<byte> body(orig.begin() + 8, orig.end());
    In in(body, 0);
    const int old_count = in.g16();
    const int count = combo_count(old_count);
    Out o;
    o.p16((word)count);
    size_t pos = 2;
    newcombo was, got;
    memset(&was, 0, sizeof(was));

    for(int i = 0; i < count; ++i)
    {
        size_t start = pos, end = pos;
        const bool have_raw = i < old_count;

        if(have_raw)
        {
            if(!read_combo_255(body, pos, v, &end, was))
                return ERR_READ;

            pos = end;
        }

        if(i >= MAXCOMBOS)      // read past and dropped by the loader
        {
            o.copy(body, start, end);
            continue;
        }

        const bool unchanged = have_raw && combo_unchanged(i);

        if(unchanged && !zqw_force_serialize)
        {
            o.copy(body, start, end);
            continue;
        }

        const newcombo &c = combobuf[i];
        std::vector<byte> rec;

        if(!have_raw)
            memset(&was, 0, sizeof(was));

        if(v <= 32)
        {
            if(have_raw)
                rec.assign(body.begin() + start, body.begin() + end);
            else
                rec.assign(flat_layout(v).bytes, 0);

            patch_flat(rec, v, was, c, !have_raw);
        }
        else
        {
            std::vector<byte> raw;

            if(have_raw)
                raw.assign(body.begin() + start, body.begin() + end);
            else
                raw.assign(v < 55 ? 1 : 2, 0);

            if(!patch_grouped(raw, v, was, c, rec))
                return ERR_READ;
        }

        // An animated combo's tile can come from either of two fields of the flat format:
        // if what ZQuest holds doesn't read back, it goes in both.
        size_t back_end = 0;
        bool ok = read_combo_255(rec, 0, v, &back_end, got) && back_end == rec.size();

        if(ok && got.tile != c.tile && v <= 32)
        {
            set_flat_tile(rec, v, c.tile);
            ok = read_combo_255(rec, 0, v, &back_end, got) && back_end == rec.size();
        }

        if(ok && same_combo(c, got) && !exact_differs(ID_COMBOS, i, unchanged, rec, body, start, end))
            o.pn(&rec[0], rec.size());
        else if(refuse(ID_COMBOS, i, unchanged, "can't be held in this version"))
            return ERR_CANT_HOLD;
        else
            o.copy(body, start, end);
    }

    o.copy(body, pos, body.size());
    return put_part(f, ctx.part->id, orig, o);
}

ZqWriterRegistration g_reg_combos_255(ID_COMBOS, write_combos_255, "combos, 2.55/3.0 versions 7-73");

// ---- CMBA: combo aliases, every version -------------------------------------------------
//
// readcomboaliases reads 256 aliases before v2 and 2048 from then on; 2.55's reader reads
// 8192 from v3 and keeps the first 2048, then skips combo pools (v4) and autocombos (v5),
// which stay as they are. Neither converts anything.

int alias_cells(byte width, byte height, byte mask)
{
    return (width + 1) * (height + 1) * (comboa_lmasktotal(mask) + 1);
}

// An alias as the file holds it; the cset goes in whole, to see one past a byte.
void alias_key(Out &o, const combo_alias &a)
{
    o.p16(a.combo);
    o.p16(a.cset);
    o.p8(a.width);
    o.p8(a.height);
    o.p8(a.layermask);
    int n = alias_cells(a.width, a.height, a.layermask);

    for(int k = 0; k < n; ++k) o.p16(a.combos ? a.combos[k] : 0);
    for(int k = 0; k < n; ++k) o.p8(a.csets ? a.csets[k] : 0);
}

void write_alias(Out &o, const combo_alias &a)
{
    o.p16(a.combo);
    o.p8((byte)a.cset);
    o.p8(a.width);
    o.p8(a.height);
    o.p8(a.layermask);
    int n = alias_cells(a.width, a.height, a.layermask);

    for(int k = 0; k < n; ++k) o.p16(a.combos ? a.combos[k] : 0);
    for(int k = 0; k < n; ++k) o.p8(a.csets ? a.csets[k] : 0);
}

std::vector<std::vector<byte> > g_aliases_loaded;

void note_aliases()
{
    g_aliases_loaded.resize(MAXCOMBOALIASES);

    for(int j = 0; j < MAXCOMBOALIASES; ++j)
    {
        Out o;
        alias_key(o, combo_aliases[j]);
        g_aliases_loaded[j].swap(o.b);
    }

    if(mutate())
        for(int j = 4; j < OLDMAXCOMBOALIASES; j += 9)
            if(combo_aliases[j].combos)
                combo_aliases[j].combo = combo_aliases[j].combos[0];
}

ZqLoadHookRegistration g_hook_aliases(note_aliases);

bool alias_unchanged(int j)
{
    if(j < 0 || j >= MAXCOMBOALIASES || (size_t)j >= g_aliases_loaded.size())
        return false;

    Out o;
    alias_key(o, combo_aliases[j]);
    return o.b == g_aliases_loaded[j];
}

int write_aliases(PACKFILE *f, const ZqPartContext &ctx)
{
    const word v = ctx.version;
    int in_file;

    if(is_253(ctx) || ctx.zelda_version < 0x193)
        return ZQW_UNSUPPORTED;

    if(ctx.zelda_version == 0x255)
    {
        if(v < 2 || v > 5)
            return ZQW_UNSUPPORTED;

        in_file = v >= 3 ? 8192 : 2048;
    }
    else
    {
        if(v > V_COMBOALIASES)
            return ZQW_UNSUPPORTED;

        in_file = v < 2 ? OLDMAXCOMBOALIASES : MAXCOMBOALIASES;
    }

    std::vector<byte> orig;

    if(!part_bytes(*ctx.part, orig) || orig.size() < 8)
        return ERR_READ;

    In in(orig, 8);
    Out o;

    for(int j = 0; j < in_file; ++j)
    {
        size_t start = in.pos;
        in.skip(3);
        byte width = in.g8(), height = in.g8(), mask = in.g8();
        in.skip((size_t)alias_cells(width, height, mask) * 3);

        if(in.bad)
            return ERR_READ;

        if(j >= MAXCOMBOALIASES)    // read past and dropped by 2.55's reader
        {
            o.copy(orig, start, in.pos);
            continue;
        }

        const bool unchanged = alias_unchanged(j);

        if(unchanged && !zqw_force_serialize)
        {
            o.copy(orig, start, in.pos);
            continue;
        }

        Out rec;
        write_alias(rec, combo_aliases[j]);

        if(combo_aliases[j].cset < 256 && !exact_differs(ID_COMBOALIASES, j, unchanged, rec.b, orig, start, in.pos))
            o.pn(&rec.b[0], rec.b.size());
        else if(refuse(ID_COMBOALIASES, j, unchanged, "can't be held in this version"))
            return ERR_CANT_HOLD;
        else
            o.copy(orig, start, in.pos);
    }

    // Aliases the file has no room for must be as the load left them.
    for(int j = in_file; j < MAXCOMBOALIASES; ++j)
        if(!alias_unchanged(j) && refuse(ID_COMBOALIASES, j, false, "is past what this version holds"))
            return ERR_CANT_HOLD;

    o.copy(orig, in.pos, orig.size());     // 2.55's combo pools and autocombos
    return put_part(f, ctx.part->id, orig, o);
}

ZqWriterRegistration g_reg_aliases(ID_COMBOALIASES, write_aliases, "combo aliases, all sectioned versions");

} // namespace

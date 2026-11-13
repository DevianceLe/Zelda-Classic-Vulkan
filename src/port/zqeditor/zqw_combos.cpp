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
#include "sfx.h"
#include "tiles.h"
#include "zq_writers.h"
#include "q255.h"
#include "q30_triggers.h"
#include "zq_q255_store.h"
#include "zqn_255.h"

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
void note_q30_combos();

void note_combos()
{
    reset_combo_animations();       // as a save does, so animated combos show their own tiles
    reset_combo_animations2();
    Out o;
    o.b.reserve((size_t)MAXCOMBOS * COMBO_KEY);

    for(int i = 0; i < MAXCOMBOS; ++i)
        combo_key(o, combobuf[i]);

    g_combos_loaded.swap(o.b);
    note_q30_combos();

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

bool q30_unchanged(int i);

bool combo_unchanged(int i)
{
    if(i < 0 || i >= MAXCOMBOS || g_combos_loaded.size() < (size_t)(i + 1) * COMBO_KEY)
        return false;

    Out o;
    combo_key(o, combobuf[i]);
    return !memcmp(&o.b[0], &g_combos_loaded[(size_t)i * COMBO_KEY], COMBO_KEY) && q30_unchanged(i);
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

// ---- CMBO: a whole 2.55/3.0 combo encoded again ------------------------------------------
//
// For a combo whose newer fields were edited (the combo editor for 2.55 and 3.0 quests): the
// record written from newcombo and the Q30Combo table, field by field as q255_combos.cpp
// reads it, in the section's own version. Upstream's load fixes are undone where the format
// allows; where it can't hold a value, the read-back check in write_combos_255 refuses it.

enum
{
    CHAS_ATTRIB = 0x0001, CHAS_FLAG = 0x0002, CHAS_TRIG = 0x0004, CHAS_LIFT = 0x0040,
    CHAS_GENERAL = 0x0080, CHAS_MISC_WEAP_DATA = 0x0100
};

void put_cstr(Out &o, const std::string &s)
{
    const size_t n = s.size() < 255 ? s.size() : 255;
    o.p8((byte)n);
    o.pn(s.data(), n);
}

// upstream's p_putbitstr: the size (65535), then pairs or plain bytes, whichever is shorter
void put_bitstr(Out &o, const Q30Bits &b)
{
    size_t cap = b.v.size() < 65535 ? b.v.size() : 65535;

    while(cap && !b.v[cap - 1])
        --cap;

    size_t set = 0;

    for(size_t q = 0; q < cap; ++q)
        if(b.v[q])
            ++set;

    const bool pairs = set * 3 <= cap;
    o.p16(65535);
    o.p8(pairs ? 1 : 0);
    o.p16((word)(pairs ? set : cap));

    for(size_t q = 0; q < cap; ++q)
    {
        if(pairs)
        {
            if(!b.v[q])
                continue;

            o.p16((word)q);
        }

        o.p8(b.v[q]);
    }
}

// a script_config: the script, its eight InitDs, then the instance values as they were read
void put_script_config(Out &o, word script, const long *initd, const std::vector<byte> &inst_raw)
{
    o.p16(script);
    o.p16(8);

    for(int q = 0; q < 8; ++q)
        o.p32((dword)initd[q]);

    if(inst_raw.size() >= 2)
        o.pn(&inst_raw[0], inst_raw.size());
    else
        o.p16(0);
}

// d's fields all equal z's (data_version aside)
bool weapon_same(const Q30WeaponData &d, const Q30WeaponData &z)
{
    return d.flags == z.flags && d.moveflags == z.moveflags && d.wflags == z.wflags &&
           !memcmp(d.burnsprs, z.burnsprs, sizeof(z.burnsprs)) && !memcmp(d.light_rads, z.light_rads, sizeof(z.light_rads)) &&
           !memcmp(d.light_offsets, z.light_offsets, sizeof(z.light_offsets)) && d.glow_shape == z.glow_shape &&
           d.override_flags == z.override_flags && d.tilew == z.tilew && d.tileh == z.tileh && d.hxsz == z.hxsz &&
           d.hysz == z.hysz && d.hzsz == z.hzsz && d.hxofs == z.hxofs && d.hyofs == z.hyofs && d.xofs == z.xofs &&
           d.yofs == z.yofs && d.step == z.step && d.unblockable == z.unblockable && d.timeout == z.timeout &&
           d.imitate_weapon == z.imitate_weapon && d.default_defense == z.default_defense &&
           d.lift_level == z.lift_level && d.lift_time == z.lift_time && d.lift_height == z.lift_height &&
           d.script == z.script && !memcmp(d.initd, z.initd, sizeof(z.initd)) && d.pierce_count == z.pierce_count &&
           d.bounce_mult == z.bounce_mult && d.bounce_add == z.bounce_add &&
           d.viewport_suspend_range == z.viewport_suspend_range && d.viewport_despawn_range == z.viewport_despawn_range;
}

bool weapon_blank(const Q30WeaponData &d)
{
    static const Q30WeaponData z;
    return weapon_same(d, z);
}

// 3.0's weapon_data::def_lifting_data: what a combo's lift weapon data is when nothing set it
// (it falls and pitfalls, breaks when it lands, glows), which 3.0's writecombo_loop counts as
// blank (is_blank_lifting).
bool weapon_blank_lifting(const Q30WeaponData &d)
{
    static Q30WeaponData z;
    static bool made = false;

    if(!made)
    {
        z.moveflags |= 3;           // move_obeys_grav | move_can_pitfall
        z.wflags |= 2;              // WFLAG_BREAK_WHEN_LANDING
        z.flags |= 1;               // wdata_glow_rad
        made = true;
    }

    return weapon_same(d, z);
}

// read_weapon_data's format, in data version wv
void put_weapon_data(Out &o, const Q30WeaponData &d, word wv)
{
    o.p16(wv);
    o.p16(d.flags);
    o.p32(d.moveflags);
    o.p16(d.wflags);

    for(int q = 0; q < 5; ++q)
    {
        if(wv < 1)
            o.p8((byte)d.burnsprs[q]);
        else
            o.p16(d.burnsprs[q]);

        o.p8(d.light_rads[q]);

        if(wv >= 3)
            o.p32((dword)d.light_offsets[q]);
    }

    o.p8(d.glow_shape);
    o.p32((dword)d.override_flags);
    o.p32((dword)d.tilew);
    o.p32((dword)d.tileh);
    o.p32((dword)d.hxsz);
    o.p32((dword)d.hysz);
    o.p32((dword)d.hzsz);
    o.p32((dword)d.hxofs);
    o.p32((dword)d.hyofs);
    o.p32((dword)d.xofs);
    o.p32((dword)d.yofs);
    o.p32((dword)d.step);
    o.p8(d.unblockable);
    o.p32((dword)d.timeout);
    o.p8(d.imitate_weapon);
    o.p8(d.default_defense);
    o.p8(d.lift_level);
    o.p8(d.lift_time);
    o.p32((dword)d.lift_height);

    if(wv < 5)
    {
        o.p16(d.script);

        for(int q = 0; q < 8; ++q)
            o.p32((dword)d.initd[q]);
    }
    else
        put_script_config(o, d.script, d.initd, d.inst_raw);

    o.p16((word)d.pierce_count);

    if(wv >= 2)
    {
        o.p32((dword)d.bounce_mult);
        o.p32((dword)d.bounce_add);
    }

    if(wv >= 4)
    {
        o.p32((dword)d.viewport_suspend_range);
        o.p32((dword)d.viewport_despawn_range);
    }
}

// read_trigger's format in section version v
void put_trigger(Out &o, word v, const Q30Trigger &t)
{
    if(v >= 52)
        put_cstr(o, t.label);

    if(v < 57)
    {
        const int words = (v < 36) ? 3 : 6;

        for(int q = 0; q < words; ++q)
        {
            dword val = 0;

            for(int b = 0; b < 32; ++b)
                if(t.flags.get((size_t)q * 32 + b))
                    val |= 1u << b;

            o.p32(val);
        }
    }
    else
        put_bitstr(o, t.flags);

    o.p32((dword)t.triggerlevel);
    o.p8(t.triggerbtn);
    if(v >= 65) o.p16(t.triggeritem); else o.p8((byte)t.triggeritem);
    if(v >= 53) o.p16(t.trigtimer); else o.p8((byte)t.trigtimer);
    if(v >= 64) o.p16(t.trigsfx); else o.p8((byte)t.trigsfx);
    o.p32((dword)t.trigchange);
    o.p16(t.trigprox);
    if(v >= 53) o.p16((word)t.trigctr); else o.p8((byte)t.trigctr);
    o.p32((dword)t.trigctramnt);
    o.p8(t.triglbeam);
    o.p8((byte)t.trigcschange);
    if(v >= 65) o.p32((dword)t.spawnitem); else o.p16((word)t.spawnitem);
    o.p16((word)t.spawnenemy);
    o.p8((byte)t.exstate);
    o.p32((dword)t.spawnip);
    o.p8(t.trigcopycat);
    o.p8(t.trigcooldown);

    if(v >= 35)
    {
        o.p16(t.prompt_cid);
        o.p8(t.prompt_cs);
        o.p16((word)t.prompt_x);
        o.p16((word)t.prompt_y);
    }

    if(v >= 36)
    {
        o.p8(t.trig_lstate);
        o.p8(t.trig_gstate);
        o.p32((dword)t.trig_statetime);
    }

    if(v >= 37)
        o.p16(t.trig_genscr);

    if(v >= 38)
    {
        o.p8(t.trig_group);
        o.p16(t.trig_group_val);
    }

    if(v >= 45)
    {
        o.p8((byte)t.exdoor_dir);
        o.p8(t.exdoor_ind);
    }

    if(v >= 46)
    {
        if(v >= 59) o.p16(t.trig_levelitems); else o.p8((byte)t.trig_levelitems);
        o.p16((word)t.trigdmlevel);

        for(int q = 0; q < 3; ++q)
        {
            if(v >= 48)
                o.p16((word)t.trigtint[q]);
            else
            {
                // six-bit values the reader scales up: the one that reads back as this, if any
                const int want = t.trigtint[q];
                signed char found = 0;

                for(int val = -63; val <= 63; ++val)
                {
                    const int a = val < 0 ? -val : val;
                    const int scaled = (a * 255) / 63;

                    if((val < 0 ? -scaled : scaled) == want)
                    {
                        found = (signed char)val;
                        break;
                    }
                }

                o.p8((byte)found);
            }
        }

        o.p16((word)t.triglvlpalette);
        o.p16((word)t.trigbosspalette);
        o.p16((word)t.trigquaketime);
        o.p16((word)t.trigwavytime);
        o.p16((word)t.trig_swjinxtime);
        o.p16((word)t.trig_itmjinxtime);
        o.p16((word)t.trig_stuntime);
        o.p16((word)t.trig_bunnytime);
        o.p8(t.trig_pushtime);
    }

    if(v >= 47)
        o.p16((word)t.trig_shieldjinxtime);

    if(v >= 53)
    {
        o.p32(t.req_level_state);
        o.p32(t.unreq_level_state);
        put_bitstr(o, t.req_global_state);
        put_bitstr(o, t.unreq_global_state);
        o.p16(t.fail_prompt_cid);
        o.p8(t.fail_prompt_cs);
        o.p32((dword)t.trig_msgstr);
        o.p32((dword)t.fail_msgstr);
        o.p32((dword)t.player_bounce);
        o.p32((dword)t.req_player_z);
    }

    if(v >= 54)
    {
        o.p8(t.req_player_dir);
        o.p32((dword)t.dest_player_x);
        o.p32((dword)t.dest_player_y);
        o.p32((dword)t.dest_player_z);
        o.p32((dword)t.req_player_jump);
        o.p32((dword)t.req_player_x);
        o.p32((dword)t.req_player_y);
    }

    if(v >= 56)
    {
        o.p8((byte)t.dest_player_dir);
        o.p32((dword)t.force_ice_combo);
        o.p32((dword)t.force_ice_vx);
        o.p32((dword)t.force_ice_vy);
    }

    if(v >= 58)
    {
        o.p32((dword)t.trig_gravity);
        o.p32((dword)t.trig_terminal_v);
    }

    if(v >= 61)
    {
        put_bitstr(o, t.req_screen_state);
        put_bitstr(o, t.unreq_screen_state);
        put_bitstr(o, t.req_screen_ex_state);
        put_bitstr(o, t.unreq_screen_ex_state);
        o.p8(t.trigstatemap);
        o.p8(t.trigstatescreen);
    }

    if(v >= 62)
    {
        o.p32((dword)t.play_music);
        o.p8((byte)t.set_music_refresh);
    }

    if(v >= 68)
    {
        o.p16((word)t.viewport_cond_range);
        o.p32((dword)t.chance_numerator);
        o.p32((dword)t.chance_denominator);
        o.p16((word)t.sfx_pan);
        o.p8(t.sfx_volume);
        o.p32((dword)t.sfx_frequency);
    }

    if(v >= 69)
    {
        for(int dir = 0; dir < 4; ++dir)
        {
            put_bitstr(o, t.req_screen_ex_door[dir]);
            put_bitstr(o, t.unreq_screen_ex_door[dir]);
        }
    }

    if(v >= 70)
        o.p8(t.combopos_state);

    if(v >= 71)
        o.p8(t.large_combo_copycat);
}

// The fields a combo of the v33+ format would have groups for (upstream's writecombo_loop,
// with the tile and flag also counting for BASIC so they are never dropped). lift_rule_30: the
// lift weapon data counts as 3.0's writecombo_loop counts it (blank when it is the lifting
// default, or when the file had none: data_version -1 and the plain default, which 3.0 holds as
// the lifting default); otherwise any data that isn't the plain default makes a lift group.
word combo_groups(word v, const newcombo &c, const Q30Combo &x, dword tile, bool lift_rule_30 = false)
{
    word has = 0;

    for(int q = 0; q < 24; ++q)
        if(x.attributes[q])
            has |= CHAS_ATTRIB;

    if(!x.triggers.empty() || (v >= 52 && x.only_gentrig))
        has |= CHAS_TRIG;

    if(x.usrflags || x.genflags)
        has |= CHAS_FLAG;

    if(c.frames || c.speed || c.nextcombo || c.nextcset || c.skipanim || c.skipanimy || c.animflags || x.delay ||
       x.cs2_flash_duration)
        has |= CHAS_ANIM;

    if(x.script || !x.label.empty())
        has |= CHAS_SCRIPT;

    for(int q = 0; q < 8 && !(has & CHAS_SCRIPT); ++q)
        if(x.initd[q])
            has |= CHAS_SCRIPT;

    if(tile || c.flip || x.walk != 0xF0 || x.type || x.flag || c.csets)
        has |= CHAS_BASIC;

    const bool lift_weap_set = lift_rule_30 ?
        !((x.lift_weap.data_version < 0 && weapon_blank(x.lift_weap)) || weapon_blank_lifting(x.lift_weap)) :
        !weapon_blank(x.lift_weap);

    if(x.liftcmb || x.liftcs || x.liftdmg || x.liftlvl || x.liftitm || x.liftflags || x.liftgfx || x.liftsprite ||
       x.liftsfx || x.liftundercmb || x.liftundercs || x.liftbreaksprite != -1 || x.liftbreaksfx || x.lifthei != 8 ||
       x.lifttime != 16 || x.lift_parent_item || lift_weap_set)
        has |= CHAS_LIFT;

    if(x.speed_mult != 1 || x.speed_div != 1 || x.speed_add || x.sfx_appear || x.sfx_disappear || x.sfx_loop ||
       x.sfx_walking || x.sfx_standing || x.spr_appear || x.spr_disappear || x.spr_walking || x.spr_standing ||
       x.sfx_tap || x.sfx_landing || x.spr_falling || x.spr_drowning || x.spr_lava_drowning || x.sfx_falling ||
       x.sfx_drowning || x.sfx_lava_drowning || x.z_height || x.z_step_height || x.dive_under_level ||
       x.large_combo_dirs)
        has |= CHAS_GENERAL;

    if(!weapon_blank(x.misc_weap))
        has |= CHAS_MISC_WEAP_DATA;

    return has;
}

// A v33+ record. keep_has: the groups the original had, which stay (an absent group and a
// present one at its defaults read the same). weap_version: the weapon data version to use
// where the combo has none of its own (another combo's in the same section, or -1: none known).
bool encode_grouped(Out &o, word v, const newcombo &c, const Q30Combo &x, dword tile, word keep_has, int weap_version,
                    bool lift_rule_30 = false)
{
    word has = (word)(keep_has | combo_groups(v, c, x, tile, lift_rule_30));

    if(v < 55)
    {
        if(has & 0xFF00)
            return false;               // misc weapon data: no room before v55

        o.p8((byte)has);
    }
    else
        o.p16(has);

    if(has & CHAS_BASIC)
    {
        o.p32(tile);
        o.p8(c.flip);
        o.p8(x.walk);
        o.p8(x.type);
        o.p8(x.flag);
        o.p8(c.csets);
    }

    if(has & CHAS_SCRIPT)
    {
        if(v >= 41)
            put_cstr(o, x.label);
        else
        {
            char label[11];
            memset(label, 0, sizeof(label));
            strncpy(label, x.label.c_str(), sizeof(label));
            o.pn(label, 11);
        }

        if(v >= 72)
            put_script_config(o, x.script, x.initd, x.script_inst_raw);
        else
        {
            o.p16(x.script);
            const int n = (v >= 43) ? 8 : 2;

            for(int q = 0; q < n; ++q)
                o.p32((dword)x.initd[q]);
        }
    }

    if(has & CHAS_ANIM)
    {
        o.p8(c.frames);
        o.p8(c.speed);
        o.p16(c.nextcombo);
        o.p8(c.nextcset);
        o.p8(c.skipanim);
        o.p8(c.skipanimy);
        o.p8(c.animflags);

        if(v >= 73)
        {
            o.p16(x.delay);
            o.p8(x.cs2_flash_duration);
        }
    }

    if(has & CHAS_ATTRIB)
    {
        if(v < 63)
        {
            for(int q = 0; q < 4; ++q)
                o.p32((dword)x.attributes[q]);

            for(int q = 0; q < 8; ++q)
                o.p8((byte)(x.attributes[8 + q] / 10000));

            for(int q = 0; q < 8; ++q)
                o.p16((word)(x.attributes[16 + q] / 10000));
        }
        else
        {
            o.p16(24);

            for(int q = 0; q < 24; ++q)
                o.p32((dword)x.attributes[q]);
        }
    }

    if(has & CHAS_FLAG)
    {
        o.p32(x.usrflags);
        o.p16(x.genflags);
    }

    if(has & CHAS_TRIG)
    {
        if(v >= 52)
        {
            if(x.triggers.size() > 255)
                return false;

            o.p8((byte)x.triggers.size());

            for(size_t q = 0; q < x.triggers.size(); ++q)
                put_trigger(o, v, x.triggers[q]);

            o.p8(x.only_gentrig);
        }
        else
        {
            // one trigger, always there; ONLYGENTRIG is one of its flags
            Q30Trigger t = x.triggers.empty() ? Q30Trigger() : x.triggers[0];

            if(x.triggers.size() > 1)
                return false;

            if(x.only_gentrig)
                t.flags.set(Q30TF_ONLYGENTRIG);

            put_trigger(o, v, t);
        }
    }

    if(has & CHAS_LIFT)
    {
        o.p16(x.liftcmb);
        o.p8(x.liftcs);
        o.p16(x.liftundercmb);
        o.p8(x.liftundercs);
        o.p8(x.liftdmg);
        o.p8(x.liftlvl);
        o.p8(x.liftitm);
        o.p8(x.liftflags);
        o.p8(x.liftgfx);
        if(v < 66) o.p8((byte)x.liftsprite); else o.p16(x.liftsprite);
        if(v < 64) o.p8((byte)x.liftsfx); else o.p16(x.liftsfx);
        o.p16((word)x.liftbreaksprite);
        o.p8(x.liftbreaksfx);

        if(v >= 34)
        {
            o.p8(x.lifthei);
            o.p8(x.lifttime);
        }

        if(v >= 65)
            o.p16(x.lift_parent_item);
        else if(v >= 39)
            o.p8((byte)x.lift_parent_item);

        if(v >= 51 && v < 55)
        {
            o.p8(x.lift_weap.light_rads[0]);
            o.p8(x.lift_weap.glow_shape);
        }

        if(v >= 55)
        {
            // its own version, else the section's, else the oldest (every reader takes it; what
            // it can't hold doesn't read back, and the save refuses it)
            const int wv = x.lift_weap.data_version >= 0 ? x.lift_weap.data_version : (weap_version >= 0 ? weap_version : 0);
            put_weapon_data(o, x.lift_weap, (word)wv);
        }
    }

    if(has & CHAS_GENERAL)
    {
        o.p8(x.speed_mult);
        o.p8(x.speed_div);
        o.p32((dword)x.speed_add);

        if(v >= 42)
        {
            if(v < 64)
            {
                o.p8((byte)x.sfx_appear);
                o.p8((byte)x.sfx_disappear);
                o.p8((byte)x.sfx_loop);
                o.p8((byte)x.sfx_walking);
                o.p8((byte)x.sfx_standing);
            }
            else
            {
                o.p16(x.sfx_appear);
                o.p16(x.sfx_disappear);
                o.p16(x.sfx_loop);
                o.p16(x.sfx_walking);
                o.p16(x.sfx_standing);
            }

            if(v < 66)
            {
                o.p8((byte)x.spr_appear);
                o.p8((byte)x.spr_disappear);
                o.p8((byte)x.spr_walking);
                o.p8((byte)x.spr_standing);
            }
            else
            {
                o.p16(x.spr_appear);
                o.p16(x.spr_disappear);
                o.p16(x.spr_walking);
                o.p16(x.spr_standing);
            }
        }

        if(v < 64)
        {
            if(v >= 44)
                o.p8((byte)x.sfx_tap);

            if(v >= 49)
                o.p8((byte)x.sfx_landing);
        }
        else
        {
            o.p16(x.sfx_tap);
            o.p16(x.sfx_landing);
        }

        if(v >= 50)
        {
            if(v < 66)
            {
                o.p8((byte)x.spr_falling);
                o.p8((byte)x.spr_drowning);
                o.p8((byte)x.spr_lava_drowning);
            }
            else
            {
                o.p16(x.spr_falling);
                o.p16(x.spr_drowning);
                o.p16(x.spr_lava_drowning);
            }

            if(v < 64)
            {
                o.p8((byte)x.sfx_falling);
                o.p8((byte)x.sfx_drowning);
                o.p8((byte)x.sfx_lava_drowning);
            }
            else
            {
                o.p16(x.sfx_falling);
                o.p16(x.sfx_drowning);
                o.p16(x.sfx_lava_drowning);
            }
        }

        if(v >= 56)
        {
            o.p32((dword)x.z_height);
            o.p32((dword)x.z_step_height);
        }

        if(v >= 60)
            o.p8(x.dive_under_level);

        if(v >= 71)
            o.p8(x.large_combo_dirs);
    }

    if(has & CHAS_MISC_WEAP_DATA)
    {
        const int wv = x.misc_weap.data_version >= 0 ? x.misc_weap.data_version : (weap_version >= 0 ? weap_version : 0);
        put_weapon_data(o, x.misc_weap, (word)wv);
    }

    return true;
}

// The script types that got CMBTYPEFX added before v23 (read_combo_old).
bool script_type_v23(byte type)
{
    switch(type)
    {
    case cSCRIPT1: case cSCRIPT2: case cSCRIPT3: case cSCRIPT4: case cSCRIPT5:
    case Q30C_SCRIPT6: case Q30C_SCRIPT7: case Q30C_SCRIPT8: case Q30C_SCRIPT9: case Q30C_SCRIPT10:
    case Q30C_TRIGGERGENERIC: case Q30C_CSWITCH:
        return true;
    }

    return false;
}

// The types whose usrflags 0x8000 became genflags 0x0001 before v20 (read_combo_old).
bool genflag_type_v20(byte type)
{
    switch(type)
    {
    case cPUSH_WAIT: case cPUSH_HEAVY: case cPUSH_HW: case cL_STATUE: case cR_STATUE:
    case cPUSH_HEAVY2: case cPUSH_HW2: case cPOUND: case cC_STATUE: case cMIRROR:
    case cMIRRORSLASH: case cMIRRORBACKSLASH: case cMAGICPRISM: case cMAGICPRISM4:
    case cMAGICSPONGE: case cEYEBALL_A: case cEYEBALL_B: case cEYEBALL_4:
    case cBUSH: case cFLOWERS: case cLOCKBLOCK: case cLOCKBLOCK2:
    case cBOSSLOCKBLOCK: case cBOSSLOCKBLOCK2: case cCHEST: case cCHEST2:
    case cLOCKEDCHEST: case cLOCKEDCHEST2: case cBOSSCHEST: case cBOSSCHEST2:
    case cBUSHNEXT: case cBUSHTOUCHY: case cFLOWERSTOUCHY: case cBUSHNEXTTOUCHY:
    case Q30C_SIGNPOST: case Q30C_CSWITCHBLOCK: case Q30C_TORCH: case Q30C_TRIGGERGENERIC:
        return true;
    }

    return false;
}

// A record of the old flat format (v7-32), with upstream's old-format load fixes undone.
// raw: the original record, for the two bytes the loader skips (cur_frame, aclk).
bool encode_flat(Out &o, word v, const newcombo &c, const Q30Combo &x, dword tile, const std::vector<byte> *raw)
{
    if(x.triggers.size() > 1)
        return false;

    Q30Trigger t = x.triggers.empty() ? Q30Trigger() : x.triggers[0];
    dword usrflags = x.usrflags;
    long attributes[24];
    memcpy(attributes, x.attributes, sizeof(attributes));
    dword trigflags[3] = { 0, 0, 0 };

    for(int b = 0; b < 96; ++b)
        if(t.flags.get(b))
            trigflags[b / 32] |= 1u << (b % 32);

    // undo, newest fix first
    if(v < 20 && genflag_type_v20(x.type) && (x.genflags & 0x0001))
        usrflags |= 0x8000;

    if(v < 28)
    {
        switch(x.type)
        {
        case cLOCKBLOCK: case cLOCKEDCHEST:
            if(usrflags & 0x80)
                usrflags |= 0x40;
            else
                usrflags &= ~0x40u;
            usrflags &= ~0x80u;
            break;
        }
    }

    if(v < 27)
    {
        trigflags[0] &= ~(0x00040000u | 0x00080000u);

        if(t.trigchange == 1)
            trigflags[0] |= 0x00040000;
        else if(t.trigchange == -1)
            trigflags[0] |= 0x00080000;
    }

    if(v < 25)
    {
        switch(x.type)
        {
        case cLOCKBLOCK: case cBOSSLOCKBLOCK:
            // the lock sound: the default unless the file says the quest sets its own
            if(attributes[8 + 3] != (long)WAV_DOOR * 10000)
                usrflags |= 0x4;
            break;
        }
    }

    if(v < 23 && script_type_v23(x.type))
        trigflags[Q30TF_CMBTYPEFX / 32] &= ~(1u << (Q30TF_CMBTYPEFX % 32));

    if(v < 19)
        for(int q = 0; q < 4; ++q)
            attributes[q] /= 10000;

    if(v >= 11)
        o.p32(tile);
    else
        o.p16((word)tile);

    o.p8(c.flip);
    o.p8(v < 18 ? (byte)(x.walk & 0x0F) : x.walk);
    o.p8(x.type);
    o.p8(c.csets);
    o.p8(c.frames);
    o.p8(c.speed);
    o.p16(c.nextcombo);
    o.p8(c.nextcset);
    o.p8(x.flag);
    o.p8(c.skipanim);
    o.p16(c.nexttimer);
    o.p8(c.skipanimy);
    o.p8(c.animflags);

    if(v >= 8)
    {
        for(int q = 0; q < 4; ++q)
            o.p32((dword)attributes[q]);

        o.p32(usrflags);

        if(v >= 20)
            o.p16(x.genflags);
    }

    if(v >= 10)
        for(int q = 0; q < 3; ++q)
            o.p32(trigflags[q]);
    else if(v == 9)
        for(int q = 0; q < 2; ++q)
            o.p32(trigflags[q]);

    if(v >= 9)
        o.p32((dword)t.triggerlevel);

    if(v >= 22)
        o.p8(t.triggerbtn);

    if(v >= 24)
    {
        o.p8((byte)t.triggeritem);
        o.p8((byte)t.trigtimer);
    }

    if(v >= 25)
        o.p8((byte)t.trigsfx);

    if(v >= 27)
        o.p32((dword)t.trigchange);

    if(v >= 29)
    {
        o.p16(t.trigprox);
        o.p8((byte)t.trigctr);
        o.p32((dword)t.trigctramnt);
    }

    if(v >= 30)
        o.p8(t.triglbeam);

    if(v >= 31)
    {
        o.p8((byte)t.trigcschange);
        o.p16((word)t.spawnitem);
        o.p16((word)t.spawnenemy);
        o.p8((byte)t.exstate);
        o.p32((dword)t.spawnip);
        o.p8(t.trigcopycat);
    }

    if(v >= 32)
        o.p8(t.trigcooldown);

    if(v >= 12)
    {
        char label[11];
        memset(label, 0, sizeof(label));
        strncpy(label, x.label.c_str(), sizeof(label));
        o.pn(label, 11);
    }

    if(v >= 13)
        for(int q = 0; q < 4; ++q)
            o.p8((byte)(attributes[8 + q] / 10000));

    if(v >= 14)
    {
        o.p16(x.script);

        for(int q = 0; q < 2; ++q)
            o.p32((dword)x.initd[q]);
    }

    if(v >= 15)
    {
        o.p32(tile);    // o_tile: the animation's first tile

        const size_t at = flat_layout(v).o_tile + 4;

        if(raw && raw->size() >= at + 2)
            o.pn(&(*raw)[at], 2);
        else
        {
            o.p8(0);
            o.p8(0);
        }
    }

    if(v >= 17)
    {
        for(int q = 4; q < 8; ++q)
            o.p8((byte)(attributes[8 + q] / 10000));

        for(int q = 0; q < 8; ++q)
            o.p16((word)(attributes[16 + q] / 10000));
    }

    return true;
}

// read_combo_255 that also returns the Q30Combo part.
bool read_combo_255x(const std::vector<byte> &body, size_t at, word v, size_t *end, newcombo &c, Q30Combo &x)
{
    Q255Section s;
    s.data = body.empty() ? NULL : &body[0];
    s.size = body.size();
    s.pos = at;
    s.version = v;
    s.keep = false;
    int ret = q255_editor_read_combo(s, &c, &x);
    *end = s.pos;
    return ret == 0 && !s.failed;
}

// ---- CMBO: the Q30Combo part, for telling edits apart and checking what reads back -------

// The canonical bytes of a combo's newer part: every group in the newest format. The weapon
// data's own version is left out (it says how the file held it, not what it holds).
void q30_canon(const Q30Combo &x, Out &o)
{
    Q30Combo y = x;
    y.lift_weap.data_version = y.misc_weap.data_version = -1;
    newcombo c;
    memset(&c, 0, sizeof(c));
    encode_grouped(o, 73, c, y, 0, 0x01FF, 5);
}

uint64_t q30_hash(const Q30Combo *x)
{
    if(!x)
        return 0;

    Out o;
    q30_canon(*x, o);
    uint64_t h = 14695981039346656037ULL;
    zq_hash_add(&h, o.b.empty() ? NULL : &o.b[0], o.b.size());
    return h ? h : 1;
}

std::vector<uint64_t> g_q30_loaded;    // each combo's q30_hash as loaded

void note_q30_combos()
{
    g_q30_loaded.assign(MAXCOMBOS, 0);

    for(int i = 0; i < MAXCOMBOS; ++i)
        g_q30_loaded[i] = q30_hash(zq255.combos[i]);
}

bool q30_unchanged(int i)
{
    if(g_q30_loaded.size() != (size_t)MAXCOMBOS)
        return zq255.combos[i] == NULL;

    return q30_hash(zq255.combos[i]) == g_q30_loaded[i];
}

// The section's fingerprint takes in the newer part (nothing for a quest that has none).
void hash_q30_combos(uint64_t *h)
{
    for(int i = 0; i < MAXCOMBOS; ++i)
        if(zq255.combos[i])
        {
            const uint64_t x = q30_hash(zq255.combos[i]);
            zq_hash_add(h, &i, sizeof(i));
            zq_hash_add(h, &x, sizeof(x));
        }
}

ZqStoreHashRegistration g_hash_combos(ID_COMBOS, hash_q30_combos);

// ZQW_FULL_ENCODE (for testing the encoders): every combo written goes through the whole-record
// encoder, as if its newer part had been edited.
bool full_encode()
{
    static int s = -1;

    if(s < 0)
        s = getenv("ZQW_FULL_ENCODE") ? 1 : 0;

    return s == 1;
}

byte narrow_type(byte t) { return t < cMAX ? t : (byte)cNONE; }
byte narrow_flag(byte f) { return f == 104 ? (byte)mfPUSHED : (f >= mfMAX ? (byte)mfNONE : f); }

// The combo as a whole: the table's newer part, with what ZQuest's 2.53 editing changed in
// newcombo (type, flag, the walk's low nibble) taking over where they no longer agree.
void effective_q30(int i, const newcombo &c, Q30Combo &x)
{
    x = zq255.combos[i] ? *zq255.combos[i] : Q30Combo();

    if(narrow_type(x.type) != c.type)
        x.type = c.type;

    if(narrow_flag(x.flag) != c.flag)
        x.flag = flag_255(c.flag);

    x.walk = (byte)((x.walk & 0xF0) | (c.walk & 0x0F));
}

// upstream's update_combo fixes (apply_load_fixes in q255_combos.cpp) undone for an old
// format: the reader sets these whatever the record says, so the record keeps none of them
// (the landing sound isn't even in the format before v49).
void undo_update_combo(word v, Q30Combo &x)
{
    const bool water = x.type == cWATER || x.type == cSHALLOWWATER;

    if(v < 49 && water && x.sfx_landing == WAV_ZN1SPLASH)
        x.sfx_landing = 0;

    if(v < 40)
    {
        if(water && x.attributes[8 + 6] == 56L * 10000)
            x.attributes[8 + 6] = 0;
        else if((x.type == cTALLGRASS || x.type == cTALLGRASSNEXT || x.type == cTALLGRASSTOUCHY) &&
                x.attributes[8 + 6] == 55L * 10000)
            x.attributes[8 + 6] = 0;
    }
}

// The tile as the original record holds it, past what newcombo keeps.
dword raw_tile(const std::vector<byte> &raw, word v)
{
    if(v <= 32)
    {
        FlatLayout l = flat_layout(v);

        if(raw.size() < l.bytes)
            return 0;

        return l.tile_bytes == 4 ? le32(raw, 0) : (dword)(raw[0] | (raw[1] << 8));
    }

    const size_t hb = v < 55 ? 1 : 2;

    if(raw.size() < hb)
        return 0;

    const word has = hb == 1 ? raw[0] : (word)(raw[0] | (raw[1] << 8));
    return (has & CHAS_BASIC) && raw.size() >= hb + 4 ? le32(raw, hb) : 0;
}

// The weapon data version this section uses, from any combo that has weapon data (-1: none).
int section_weap_version()
{
    int wv = -1;

    for(int i = 0; i < MAXCOMBOS; ++i)
        if(const Q30Combo *x = zq255.combos[i])
        {
            if(x->lift_weap.data_version > wv) wv = x->lift_weap.data_version;
            if(x->misc_weap.data_version > wv) wv = x->misc_weap.data_version;
        }

    return wv;
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
    int weap_version = -1;      // found when first needed

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

        // the newer part was edited (or a new combo has one): the whole record is encoded again
        if(full_encode() || (!q30_unchanged(i) && (zq255.combos[i] || have_raw)))
        {
            std::vector<byte> raw;

            if(have_raw)
                raw.assign(body.begin() + start, body.begin() + end);

            Q30Combo x;
            effective_q30(i, c, x);
            const dword tile = (have_raw && was.tile == c.tile) ? raw_tile(raw, v) : (dword)c.tile;
            word keep_has = 0;

            if(have_raw && v > 32)
                keep_has = v < 55 ? raw[0] : (word)(raw[0] | (raw[1] << 8));

            if(weap_version < 0)
                weap_version = section_weap_version();

            Out e;
            Q30Combo ux = x;
            undo_update_combo(v, ux);
            const bool enc = v <= 32 ? encode_flat(e, v, c, ux, tile, have_raw ? &raw : NULL)
                                     : encode_grouped(e, v, c, ux, tile, keep_has, weap_version);
            newcombo gc;
            Q30Combo gx;
            size_t back_end = 0;
            bool ok = enc && !e.b.empty() && read_combo_255x(e.b, 0, v, &back_end, gc, gx) && back_end == e.b.size();

            if(ok)
            {
                Out a, b;
                q30_canon(x, a);
                q30_canon(gx, b);
                ok = same_combo(c, gc) && a.b == b.b;

                // ZQW_DEBUG_COMBO: where the newer part read back differently (its canonical bytes)
                if(!ok && getenv("ZQW_DEBUG_COMBO"))
                {
                    size_t k = 0;

                    while(k < a.b.size() && k < b.b.size() && a.b[k] == b.b[k])
                        ++k;

                    al_trace("zqw CMBO debug: record %d v%d: newcombo %s, canonical differs at %d (%d vs %d bytes): %02x vs %02x\n",
                             i, v, same_combo(c, gc) ? "same" : "differs", (int)k, (int)a.b.size(), (int)b.b.size(),
                             k < a.b.size() ? a.b[k] : 0, k < b.b.size() ? b.b[k] : 0);
                }
            }
            else if(getenv("ZQW_DEBUG_COMBO"))
                al_trace("zqw CMBO debug: record %d v%d: %s\n", i, v, enc ? "doesn't read back" : "can't encode");

            // Bytes the reader doesn't see (a value an old load fix replaces, say) are kept as the
            // original record had them, so a record encoded again changes only what was edited.
            if(ok && have_raw && e.b.size() == raw.size() && e.b != raw)
            {
                Out want;
                q30_canon(x, want);

                for(size_t k = 0; k < e.b.size(); ++k)
                {
                    if(e.b[k] == raw[k])
                        continue;

                    const byte mine = e.b[k];
                    e.b[k] = raw[k];
                    newcombo tc;
                    Q30Combo tx;
                    size_t te = 0;
                    bool same = read_combo_255x(e.b, 0, v, &te, tc, tx) && te == e.b.size() && same_combo(c, tc);

                    if(same)
                    {
                        Out got;
                        q30_canon(tx, got);
                        same = got.b == want.b;
                    }

                    if(!same)
                        e.b[k] = mine;
                }
            }

            if(ok && !exact_differs(ID_COMBOS, i, unchanged, e.b, body, start, end))
                o.pn(&e.b[0], e.b.size());
            else if(refuse(ID_COMBOS, i, unchanged, "can't be held in this version"))
                return ERR_CANT_HOLD;
            else
                o.copy(body, start, end);

            continue;
        }

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

// ---- a quest made in 2.55, saved as 3.0 (zqn_255.h) -----------------------------------------
//
// Each combo of the quest's own-version section read as 3.0's readcombos reads it (the reader
// ZQuest loads 2.55 combos with: the newer part holds 3.0's conversions for the section's
// version, update_combo), then written as 3.0's version 73 (the groups a combo has, weapon data
// as its version 5). 3.0 writes the combos up to the last one with a tile (count_combos).

int zqn255_combos(PACKFILE *f, const ZqNewerContext &ctx)
{
    // (version 73, 3.0's own, from 3.0 prereleases: read and written again the same way)
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.target != ZQ_NEWER_30 || ctx.src_version < 7 || ctx.src_version > 73)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const word v = s.version;
    const std::vector<byte> &body = s.data;
    In in(body, 0);
    const int count = in.g16();
    size_t pos = 2;
    std::vector<std::vector<byte> > recs;
    int used = 0;
    std::vector<byte> rules;

    if(!zqn255_rules_loaded(ctx.target, rules))
        return 2;

    const bool edit_combo_0 = (rules[230 >> 3] >> (230 & 7)) & 1;     // qr_ALLOW_EDITING_COMBO_0
    bool items_read = false;
    std::vector<Zqn255ItemWeapon> items;

    for(int i = 0; i < count; ++i)
    {
        size_t end = pos;
        newcombo c;
        Q30Combo x;

        if(!read_combo_255x(body, pos, v, &end, c, x))
        {
            zqw_last_error = "a combo of its combo section couldn't be read";
            return 2;
        }

        std::vector<byte> raw(body.begin() + pos, body.begin() + end);
        pos = end;

        if(i >= MAXCOMBOS)      // read past and dropped by the loader
            continue;

        const dword tile = raw_tile(raw, v);

        // readcombo_loop before version 55: a liftable combo's thrown weapon is its parent
        // item's when that's a bomb, else one that breaks when it lands (and on solid ground with
        // the combo's flag); it falls, and glows as the combo said
        if(v >= 33 && v < 55 && !raw.empty() && (raw[0] & 0x40))   // CHAS_LIFT
        {
            if(!items_read)
            {
                items_read = true;

                if(!zqn255_items_loaded(ctx.target, items))
                    items.clear();
            }

            Q30WeaponData &w = x.lift_weap;
            const long ptype = x.lift_parent_item < items.size() ? items[x.lift_parent_item].type : 66;  // itype_misc

            if(ptype == 27 || ptype == 28)  // itype_bomb, itype_sbomb
            {
                const byte glow = w.light_rads[0];
                const Zqn255Weapon &d = items[x.lift_parent_item].wd;
                w.flags = d.flags;
                w.moveflags = d.moveflags;
                w.wflags = d.wflags;

                for(int q = 0; q < 5; ++q)
                {
                    w.burnsprs[q] = d.burnsprs[q];
                    w.light_rads[q] = d.light_rads[q];
                    w.light_offsets[q] = d.light_offsets[q];
                }

                w.glow_shape = d.glow;
                w.override_flags = d.override_flags;
                w.tilew = d.tilew; w.tileh = d.tileh; w.hxsz = d.hxsz; w.hysz = d.hysz; w.hzsz = d.hzsz;
                w.hxofs = d.hxofs; w.hyofs = d.hyofs; w.xofs = d.xofs; w.yofs = d.yofs;
                w.step = d.step;
                w.unblockable = d.unblockable;
                w.timeout = d.timeout;
                w.imitate_weapon = d.imitate;
                w.default_defense = d.defdef;
                w.lift_level = d.lift_level;
                w.lift_time = d.lift_time;
                w.lift_height = d.lift_height;
                w.script = d.sc.script;

                for(int q = 0; q < 8; ++q)
                    w.initd[q] = d.sc.args[q];

                w.pierce_count = d.pierce;
                w.bounce_mult = d.bounce_mult;
                w.bounce_add = d.bounce_add;
                w.viewport_suspend_range = d.vp_suspend;
                w.viewport_despawn_range = d.vp_despawn;

                if(!(w.wflags & 0x200))     // WFLAG_UPDATE_IGNITE_SPRITE
                {
                    w.flags |= 1;           // wdata_glow_rad
                    w.light_rads[0] = glow;
                }
            }
            else
            {
                w.wflags |= 2;              // WFLAG_BREAK_WHEN_LANDING
                w.flags |= 1;               // wdata_glow_rad
            }

            w.moveflags |= 3;               // move_obeys_grav | move_can_pitfall

            if(x.liftflags & 0x40)          // LF_BREAKONSOLID
                w.wflags |= 4;              // WFLAG_BREAK_ON_SOLID
        }

        // after the combos (readcombos_old, readcombos): water before version 16 does 4 damage,
        // and combo 0 is plain unless the quest lets it be edited
        if(v < 16 && x.type == 3)      // cWATER
            x.attributes[0] = 4 * 10000;

        if(i == 0 && !edit_combo_0)
        {
            x.walk = 0xF0;
            x.type = 0;
            x.flag = 0;
            c.walk = 0;
            c.type = 0;
            c.flag = 0;
        }

        // 3.0's writecombo_loop writes the basic group only for a tile, flip, walkability other
        // than 0xF0, type or CSet: a combo with nothing but a map flag loses the flag
        if(!tile && !c.flip && x.walk == 0xF0 && !x.type && !c.csets)
        {
            x.flag = 0;
            c.flag = 0;
        }

        // weapon data read from the file (version 55 on) goes out in 3.0's own weapon data
        // version, as write_weap_data writes it
        if(x.lift_weap.data_version >= 0)
            x.lift_weap.data_version = 5;

        if(x.misc_weap.data_version >= 0)
            x.misc_weap.data_version = 5;

        Out e;

        if(!encode_grouped(e, 73, c, x, tile, 0, 5, true))
        {
            char why[96];
            snprintf(why, sizeof(why), "combo %d can't be written as 3.0's version 73", i);
            zqw_last_error = why;
            return 2;
        }

        recs.push_back(e.b);

        if(tile)
            used = i + 1;
    }

    if(pos != body.size())
    {
        zqw_last_error = "its combo section has bytes past its last combo";
        return 2;
    }

    Out o;
    o.p16((word)used);
    // ZQN255_CMBO_TRACE=<file> (tests only): where each combo starts in the section's data
    const char *trace = getenv("ZQN255_CMBO_TRACE");
    FILE *tf = trace && *trace ? fopen(trace, "w") : NULL;

    for(int i = 0; i < used; ++i)
    {
        if(tf)
            fprintf(tf, "%d %d\n", i, (int)o.b.size());

        o.pn(recs[i].empty() ? NULL : &recs[i][0], recs[i].size());
    }

    if(tf)
        fclose(tf);

    std::vector<byte> none;
    return zqn255_emit(f, ID_COMBOS, 73, 73, none, o.b);
}

ZqNewerRegistration g_reg_combos_255_30(ID_COMBOS, zqn255_combos, "2.55 combos as 3.0");

// ---- a quest made in an older 2.55 build, saved as 2.55.17 (zqn_255.h) -------------------------
//
// 2.55.17 reads versions 7-32 with readcombos_old and 33-45 with readcombo_loop, with the same load
// fixes the reader ZQuest loads 2.55 combos with applies for those versions (update_combo, the
// cuttables' CMBTYPEFX for quests from before 2.55.14, ...), except the landing sound 3.0 gives
// water (2.55.17's combos have none); then water before version 16 does 4 damage and combo 0 is
// plain unless the quest lets it be edited. Its writecombo_loop writes version 45 with the groups
// that hold anything: the basic group only for a tile, flip, walkability other than 0xF0, type or
// CSet (a combo with nothing but a map flag loses the flag) and the trigger group only when some
// trigger field isn't at its default. Combos up to the last one with a tile (count_combos).

int zqn255_17_combos(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.target != ZQ_NEWER_255 || ctx.src_version < 7 || ctx.src_version > 45)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const word v = s.version;
    const std::vector<byte> &body = s.data;
    In in(body, 0);
    const int count = in.g16();
    size_t pos = 2;
    std::vector<std::vector<byte> > recs;
    int used = 0;
    std::vector<byte> rules;

    if(!zqn255_rules_loaded(ctx.target, rules))
        return 2;

    const bool edit_combo_0 = (rules[230 >> 3] >> (230 & 7)) & 1;     // qr_ALLOW_EDITING_COMBO_0
    Out blank_trigger;
    put_trigger(blank_trigger, 45, Q30Trigger());

    for(int i = 0; i < count; ++i)
    {
        size_t end = pos;
        newcombo c;
        Q30Combo x;

        if(!read_combo_255x(body, pos, v, &end, c, x))
        {
            zqw_last_error = "a combo of its combo section couldn't be read";
            return 2;
        }

        std::vector<byte> raw(body.begin() + pos, body.begin() + end);
        pos = end;

        if(i >= MAXCOMBOS)      // read past and dropped by the loader
            continue;

        const dword tile = raw_tile(raw, v);
        x.sfx_landing = 0;      // 3.0's water splash (v<49): 2.55.17 has no landing sound

        if(v < 16 && x.type == 3)      // cWATER
            x.attributes[0] = 4 * 10000;

        if(i == 0 && !edit_combo_0)
        {
            x.walk = 0xF0;
            x.type = 0;
            x.flag = 0;
            c.walk = 0;
            c.type = 0;
            c.flag = 0;
        }

        if(!tile && !c.flip && x.walk == 0xF0 && !x.type && !c.csets)
        {
            x.flag = 0;
            c.flag = 0;
        }

        // a trigger at its defaults (with no ONLYGENTRIG) isn't written
        if(x.triggers.size() == 1 && !x.only_gentrig)
        {
            Out t;
            put_trigger(t, 45, x.triggers[0]);

            if(t.b == blank_trigger.b)
                x.triggers.clear();
        }

        Out e;

        if(!encode_grouped(e, 45, c, x, tile, 0, -1))
        {
            char why[96];
            snprintf(why, sizeof(why), "combo %d can't be written as 2.55.17's version 45", i);
            zqw_last_error = why;
            return 2;
        }

        recs.push_back(e.b);

        if(tile)
            used = i + 1;
    }

    if(pos != body.size())
    {
        zqw_last_error = "its combo section has bytes past its last combo";
        return 2;
    }

    Out o;
    o.p16((word)used);

    for(int i = 0; i < used; ++i)
        o.pn(recs[i].empty() ? NULL : &recs[i][0], recs[i].size());

    std::vector<byte> none;
    return zqn255_emit(f, ID_COMBOS, 45, 1, none, o.b);
}

ZqNewerRegistration g_reg_combos_255_17(ID_COMBOS, zqn255_17_combos, "2.55 combos as 2.55.17");

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

// The combo editor (zq_combo_editor.cpp): whether two newer parts hold the same.
bool zq255_q30_same(const Q30Combo &a, const Q30Combo &b)
{
    Out x, y;
    q30_canon(a, x);
    q30_canon(b, y);
    return x.b == y.b;
}

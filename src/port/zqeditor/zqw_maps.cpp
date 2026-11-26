// ZQuest's writers for the map sections, for quests saved in a version other than 2.53:
// MAP (map screens), DMAP and DOOR (door combo sets). See zq_writers.h.
//
// Each writer walks the part as it was loaded, one record at a time (a screen, a DMap, a
// door combo set). A record nobody changed is copied from the original bytes. A changed one
// is encoded from ZQuest's data in that version's format, then read back the way the loader
// reads it: if the quest would not load the same, the format can't hold the change, and the
// save fails with an error instead of losing it (the details go to allegro.log).
//
// Quests with a 2.53.0/2.53.1 header are left to ZQuest's own 2.53 writers, so they save
// exactly as before.
//
// 2.55 and 3.0 quests are read by the player's readers (src/port/game/q255_maps.cpp), which
// drop what 2.53 has no room for and narrow some values. For those, a changed record keeps
// every field from its original bytes that still reads back as what ZQuest holds, so the
// dropped fields and the values 2.53 narrows survive; only the fields the user changed are
// encoded again.

#include "precompiled.h" //always first
#include <stdint.h>

#include <stdio.h>
#include <string.h>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "sfx.h"
#include "subscr.h"
#include "zq_writers.h"
#include "q255.h"
#include "zq_q255_store.h"
#include "zq_tape.h"

extern DoorComboSet *DoorComboSets;
extern dmap *DMaps;
extern std::vector<mapscr> TheMaps;
extern word map_count;
extern newcombo *combobuf;
extern zinitdata zinit;
extern zquestheader header;
extern int q255_header_version[3];     // q255_basic.cpp: a 2.55/3.0 quest's header version
void clear_screen(mapscr *temp_scr);   // qst.cpp

// q255_maps.cpp, for ZQuest only: one screen or DMap read as the 2.55 loader reads it,
// keeping nothing beside it.
int q255_editor_read_screen(Q255Section &s, mapscr *t, int screen, const word *autolayers, word autopalette);
int q255_editor_read_dmap(Q255Section &s, dmap *d);

namespace
{

// ---- helpers ----------------------------------------------------------------------------

enum { ERR_READ = 1, ERR_CANT_HOLD = 2, ERR_WRITE = 3 };

bool is_253(const ZqPartContext &c)
{
    return c.zelda_version == 0x250 && c.build >= 31 && c.build <= 33;
}

// The part's bytes after its id: version, cversion, size, then the data.
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

// A file to read part bytes with the loader's own readers.
struct MemFile
{
    const byte *d;
    size_t n, pos;
    int ungot;
};

int mf_fclose(void *) { return 0; }
int mf_getc(void *u)
{
    MemFile *m = (MemFile *)u;

    if(m->ungot >= 0)
    {
        int c = m->ungot;
        m->ungot = -1;
        return c;
    }

    return m->pos < m->n ? m->d[m->pos++] : EOF;
}
int mf_ungetc(int c, void *u)
{
    ((MemFile *)u)->ungot = c;
    return c;
}
#pragma push_macro("long")  // packfile callbacks use the system's long (zcport/lp64.h)
#undef long
long mf_fread(void *p, long n, void *u)
{
    byte *out = (byte *)p;
    long done = 0;

    while(done < n)
    {
        int c = mf_getc(u);

        if(c == EOF)
            break;

        out[done++] = (byte)c;
    }

    return done;
}
#pragma pop_macro("long")
int mf_putc(int, void *) { return EOF; }
#pragma push_macro("long")  // packfile callbacks use the system's long (zcport/lp64.h)
#undef long
long mf_fwrite(const void *, long, void *) { return 0; }
#pragma pop_macro("long")
int mf_fseek(void *u, int offset)
{
    MemFile *m = (MemFile *)u;

    if(offset < 0 || m->pos + offset > m->n)
        return -1;

    m->pos += offset;
    return 0;
}
int mf_feof(void *u)
{
    MemFile *m = (MemFile *)u;
    return m->ungot < 0 && m->pos >= m->n;
}
int mf_ferror(void *) { return 0; }

const PACKFILE_VTABLE g_mem_vtable = { mf_fclose, mf_getc, mf_ungetc, mf_fread, mf_putc, mf_fwrite, mf_fseek, mf_feof, mf_ferror };

size_t mf_used(const MemFile &m)
{
    return m.pos - (m.ungot >= 0 ? 1 : 0);
}

// The quest's header as the loader had it, for the readers' version tests.
zquestheader header_for(const ZqPartContext &ctx)
{
    zquestheader h;
    memset(&h, 0, sizeof(h));
    h.zelda_version = ctx.zelda_version;
    h.build = ctx.build;
    return h;
}

const ZqQuestPart *find_part(dword id)
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == id)
            return &zq_quest_file.parts[i];

    return NULL;
}

int part_index(dword id)
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == id)
            return (int)i;

    return -1;
}

// ---- DOOR: door combo sets ----------------------------------------------------------------
//
// The readers (readdoorcombosets, which 2.55 quests use too) read the same thing for every
// sectioned version: no conversions, a count and that many sets.

const size_t DOOR_SET_BYTES = 605;

void put_door_set(Out &o, const DoorComboSet &d)
{
    o.pn(d.name, sizeof(d.name));

    for(int j = 0; j < 9; ++j) for(int k = 0; k < 4; ++k) o.p16(d.doorcombo_u[j][k]);
    for(int j = 0; j < 9; ++j) for(int k = 0; k < 4; ++k) o.p8(d.doorcset_u[j][k]);
    for(int j = 0; j < 9; ++j) for(int k = 0; k < 4; ++k) o.p16(d.doorcombo_d[j][k]);
    for(int j = 0; j < 9; ++j) for(int k = 0; k < 4; ++k) o.p8(d.doorcset_d[j][k]);
    for(int j = 0; j < 9; ++j) for(int k = 0; k < 6; ++k) o.p16(d.doorcombo_l[j][k]);
    for(int j = 0; j < 9; ++j) for(int k = 0; k < 6; ++k) o.p8(d.doorcset_l[j][k]);
    for(int j = 0; j < 9; ++j) for(int k = 0; k < 6; ++k) o.p16(d.doorcombo_r[j][k]);
    for(int j = 0; j < 9; ++j) for(int k = 0; k < 6; ++k) o.p8(d.doorcset_r[j][k]);

    for(int j = 0; j < 2; ++j) o.p16(d.bombdoorcombo_u[j]);
    for(int j = 0; j < 2; ++j) o.p8(d.bombdoorcset_u[j]);
    for(int j = 0; j < 2; ++j) o.p16(d.bombdoorcombo_d[j]);
    for(int j = 0; j < 2; ++j) o.p8(d.bombdoorcset_d[j]);
    for(int j = 0; j < 3; ++j) o.p16(d.bombdoorcombo_l[j]);
    for(int j = 0; j < 3; ++j) o.p8(d.bombdoorcset_l[j]);
    for(int j = 0; j < 3; ++j) o.p16(d.bombdoorcombo_r[j]);
    for(int j = 0; j < 3; ++j) o.p8(d.bombdoorcset_r[j]);

    for(int j = 0; j < 4; ++j) o.p16(d.walkthroughcombo[j]);
    for(int j = 0; j < 4; ++j) o.p8(d.walkthroughcset[j]);

    o.pn(d.flags, 2);
}

std::vector<byte> g_doors_loaded;      // every set as loaded, in its file form

void note_doors()
{
    Out o;

    for(int i = 0; i < MAXDOORCOMBOSETS; ++i)
        put_door_set(o, DoorComboSets[i]);

    g_doors_loaded.swap(o.b);

    if(mutate())
        for(int i = 1; i < door_combo_set_count && i < MAXDOORCOMBOSETS; i += 2)
            DoorComboSets[i].walkthroughcombo[3] = DoorComboSets[i].walkthroughcombo[2];
}

ZqLoadHookRegistration g_hook_doors(note_doors);

int write_doors(PACKFILE *f, const ZqPartContext &ctx)
{
    if(is_253(ctx) || ctx.zelda_version < 0x193)
        return ZQW_UNSUPPORTED;

    std::vector<byte> orig;

    if(!part_bytes(*ctx.part, orig) || orig.size() < 10)
        return ERR_READ;

    In in(orig, 8);
    const int old_count = in.g16();

    if(10 + (size_t)old_count * DOOR_SET_BYTES > orig.size())
        return ERR_READ;

    const int count = door_combo_set_count < MAXDOORCOMBOSETS ? door_combo_set_count : MAXDOORCOMBOSETS;
    Out o;
    o.p16((word)count);

    for(int i = 0; i < count; ++i)
    {
        Out rec;
        put_door_set(rec, DoorComboSets[i]);
        const size_t at = 10 + (size_t)i * DOOR_SET_BYTES;
        bool unchanged = i < old_count && g_doors_loaded.size() >= (size_t)(i + 1) * DOOR_SET_BYTES &&
                         !memcmp(&rec.b[0], &g_doors_loaded[(size_t)i * DOOR_SET_BYTES], DOOR_SET_BYTES);

        if(unchanged && !zqw_force_serialize)
            o.copy(orig, at, at + DOOR_SET_BYTES);
        else if(!exact_differs(ID_DOORS, i, unchanged, rec.b, orig, at, at + DOOR_SET_BYTES))
            o.pn(&rec.b[0], rec.b.size());
        else if(refuse(ID_DOORS, i, unchanged, "doesn't encode as it was"))
            return ERR_CANT_HOLD;
        else
            o.copy(orig, at, at + DOOR_SET_BYTES);
    }

    // whatever followed the sets the reader read (nothing, in a quest that loaded)
    o.copy(orig, 10 + (size_t)old_count * DOOR_SET_BYTES, orig.size());
    return put_part(f, ctx.part->id, orig, o);
}

ZqWriterRegistration g_reg_doors(ID_DOORS, write_doors, "door combo sets, all sectioned versions");

// ---- DMAP: DMaps, 2.53's readdmaps (quests before 2.55) -------------------------------------

// A DMap in 2.53's own form (writedmaps), to tell whether two are the same; what that form
// would cut short (tiles past 16 bits, item bits other than 0 and 1) goes after it.
void dmap_bytes_253(Out &o, const dmap &d)
{
    o.p8(d.map);
    o.p16(d.level);
    o.p8((byte)d.xoff);
    o.p8(d.compass);
    o.p16(d.color);
    o.p8(d.midi);
    o.p8(d.cont);
    o.p8(d.type);
    o.pn(d.grid, 8);
    o.pn(d.name, sizeof(d.name));
    o.pn(d.title, sizeof(d.title));
    o.pn(d.intro, sizeof(d.intro));
    o.p16((word)d.minimap_1_tile);
    o.p8(d.minimap_1_cset);
    o.p16((word)d.minimap_2_tile);
    o.p8(d.minimap_2_cset);
    o.p16((word)d.largemap_1_tile);
    o.p8(d.largemap_1_cset);
    o.p16((word)d.largemap_2_tile);
    o.p8(d.largemap_2_cset);
    o.pn(d.tmusic, sizeof(d.tmusic));
    o.p8(d.tmusictrack);
    o.p8(d.active_subscreen);
    o.p8(d.passive_subscreen);
    byte disabled[32];
    memset(disabled, 0, sizeof(disabled));

    for(int j = 0; j < MAXITEMS; ++j)
        if(d.disableditems[j])
            disabled[j / 8] |= (byte)(1 << (j % 8));

    o.pn(disabled, 32);
    o.p32((dword)d.flags);
    o.p16((word)(d.minimap_1_tile >> 16));
    o.p16((word)(d.minimap_2_tile >> 16));
    o.p16((word)(d.largemap_1_tile >> 16));
    o.p16((word)(d.largemap_2_tile >> 16));

    for(int j = 0; j < MAXITEMS; ++j)
        if(d.disableditems[j] > 1)
            o.p8(d.disableditems[j]);
}

std::vector<std::vector<byte> > g_dmaps_loaded;

void note_dmaps()
{
    g_dmaps_loaded.resize(MAXDMAPS);

    for(int i = 0; i < MAXDMAPS; ++i)
    {
        Out o;
        dmap_bytes_253(o, DMaps[i]);
        g_dmaps_loaded[i].swap(o.b);
    }

    if(mutate())
        for(int i = 1; i < count_dmaps(); i += 3)
        {
            DMaps[i].grid[7] = DMaps[i].grid[6];
            DMaps[i].title[19] = DMaps[i].title[18];
            DMaps[i].compass = DMaps[i].cont;
        }
}

ZqLoadHookRegistration g_hook_dmaps(note_dmaps);

bool dmap_unchanged(int i)
{
    if(i < 0 || i >= MAXDMAPS || (size_t)i >= g_dmaps_loaded.size())
        return false;

    Out o;
    dmap_bytes_253(o, DMaps[i]);
    return o.b == g_dmaps_loaded[i];
}

void blank_dmap_253(dmap &d)
{
    memset(&d, 0, sizeof(d));
    sprintf(d.title, "                    ");
    sprintf(d.intro, "                                                                        ");
}

// What the header and rules make of an old DMap (readdmaps).
struct DmapRead
{
    word sv, version;
    byte build;
    bool viewmap_rule;      // old rule 14, as saved: level-0 DMaps get dmfVIEWMAP before v7
    bool subscreens_from_init;  // see dmap_subscreens_from_init
};

// One DMap as readdmaps reads it from a sectioned quest (header 1.93 or later).
void read_dmap_old(In &in, const DmapRead &r, dmap &d)
{
    blank_dmap_253(d);
    d.map = in.g8();
    d.level = r.sv <= 4 ? (word)in.g8() : in.g16();
    d.xoff = (char)in.g8();
    d.compass = in.g8();
    d.color = r.sv > 8 ? in.g16() : (word)in.g8();
    d.midi = in.g8();
    d.cont = in.g8();
    d.type = in.g8();

    if((d.type & dmfTYPE) == dmOVERW && r.version >= 0x210)
        d.xoff = 0;

    for(int j = 0; j < 8; ++j)
        d.grid[j] = in.g8();

    for(size_t j = 0; j < sizeof(d.name); ++j) d.name[j] = (char)in.g8();
    for(size_t j = 0; j < sizeof(d.title); ++j) d.title[j] = (char)in.g8();
    for(size_t j = 0; j < sizeof(d.intro); ++j) d.intro[j] = (char)in.g8();

    d.minimap_1_tile = in.g16();
    d.minimap_1_cset = in.g8();
    d.minimap_2_tile = in.g16();
    d.minimap_2_cset = in.g8();
    d.largemap_1_tile = in.g16();
    d.largemap_1_cset = in.g8();
    d.largemap_2_tile = in.g16();
    d.largemap_2_cset = in.g8();

    for(size_t j = 0; j < sizeof(d.tmusic); ++j)
        d.tmusic[j] = (char)in.g8();

    if(r.sv > 1)
    {
        d.tmusictrack = in.g8();
        d.active_subscreen = in.g8();
        d.passive_subscreen = in.g8();
    }

    if(r.sv > 2)
    {
        byte di[32];

        for(int j = 0; j < 32; ++j)
            di[j] = in.g8();

        for(int j = 0; j < MAXITEMS; ++j)
            d.disableditems[j] = (di[j / 8] & (1 << (j % 8))) ? 1 : 0;
    }

    if(r.sv >= 6)
        d.flags = (long)in.g32();
    else if(r.sv > 3)
        d.flags = (char)in.g8();
    else if(d.level == 0 && (r.version < 0x211 || (r.version == 0x211 && r.build < 18)))
        d.flags = dmfCAVES | dmf3STAIR | dmfWHIRLWIND | dmfGUYCAVES;
    else
        d.flags = 0;

    if(r.sv < 7 && d.level == 0 && r.viewmap_rule)
        d.flags |= dmfVIEWMAP;

    if(r.sv < 8)
    {
        if(d.level == 0 && (d.type & dmfTYPE) == dmDNGN)
            d.type |= dmCAVE;
        else if((d.type & dmfTYPE) == dmCAVE)
            d.flags |= dmfMINIMAPCOLORFIX;
    }
}

// The same DMap encoded for that version. read_dmap_old tells whether it reads back.
// raw/loaded (or NULL): the original record and what it reads as. The fields the reader
// changes keep their original bytes while they still read as ZQuest's: an overworld's
// offset (read as 0), a level-0 dungeon or cave (both read as a cave) and its flags.
void write_dmap_old(Out &o, const DmapRead &r, const dmap &d, const std::vector<byte> *raw, size_t at, const dmap *loaded)
{
    byte type = d.type;

    // Before v7, level-0 dungeons were read as caves without the minimap colour fix that
    // caves get: such a cave is written as the dungeon it came from.
    if(r.sv < 8 && d.level == 0 && (type & dmfTYPE) == dmCAVE && !(d.flags & dmfMINIMAPCOLORFIX))
        type = (byte)(type & ~dmfTYPE);

    const size_t start = o.b.size();
    o.p8(d.map);

    if(r.sv <= 4)
        o.p8((byte)d.level);
    else
        o.p16(d.level);

    const size_t xoff_at = o.b.size() - start;
    o.p8((byte)d.xoff);
    o.p8(d.compass);

    if(r.sv > 8)
        o.p16(d.color);
    else
        o.p8((byte)d.color);

    o.p8(d.midi);
    o.p8(d.cont);
    const size_t type_at = o.b.size() - start;
    o.p8(type);
    o.pn(d.grid, 8);
    o.pn(d.name, sizeof(d.name));
    o.pn(d.title, sizeof(d.title));
    o.pn(d.intro, sizeof(d.intro));
    o.p16((word)d.minimap_1_tile);
    o.p8(d.minimap_1_cset);
    o.p16((word)d.minimap_2_tile);
    o.p8(d.minimap_2_cset);
    o.p16((word)d.largemap_1_tile);
    o.p8(d.largemap_1_cset);
    o.p16((word)d.largemap_2_tile);
    o.p8(d.largemap_2_cset);
    o.pn(d.tmusic, sizeof(d.tmusic));

    const size_t subscreens_at = o.b.size() - start + 1;

    if(r.sv > 1)
    {
        o.p8(d.tmusictrack);
        o.p8(d.active_subscreen);
        o.p8(d.passive_subscreen);
    }

    if(r.sv > 2)
    {
        byte di[32];
        memset(di, 0, sizeof(di));

        for(int j = 0; j < MAXITEMS; ++j)
            if(d.disableditems[j])
                di[j / 8] |= (byte)(1 << (j % 8));

        o.pn(di, 32);
    }

    const size_t flags_at = o.b.size() - start;
    const size_t flags_bytes = r.sv >= 6 ? 4 : (r.sv > 3 ? 1 : 0);

    if(r.sv >= 6)
        o.p32((dword)d.flags);
    else if(r.sv > 3)
        o.p8((byte)d.flags);

    if(!raw || !loaded || at + o.b.size() - start > raw->size())
        return;

    // the layout is the version's, so the original's fields are where these are
    if(loaded->xoff == d.xoff)
        o.b[start + xoff_at] = (*raw)[at + xoff_at];

    if(loaded->type == d.type && loaded->flags == d.flags && loaded->level == d.level)
    {
        o.b[start + type_at] = (*raw)[at + type_at];

        for(size_t k = 0; k < flags_bytes; ++k)
            o.b[start + flags_at + k] = (*raw)[at + flags_at + k];
    }

    // the init data sets the subscreens after these are read
    if(r.sv > 1 && r.subscreens_from_init)
        for(size_t k = 0; k < 2; ++k)
            o.b[start + subscreens_at + k] = (*raw)[at + subscreens_at + k];
}

// How many DMaps to write: those in the file, and any after them that were changed. The
// ones past the end are blank when loaded (blank_dmap_253 and a cave type) and stay so
// on the next load when they aren't written.
int dmap_count(int in_file)
{
    for(int i = MAXDMAPS - 1; i >= in_file; --i)
        if(!dmap_unchanged(i))
            return i + 1;

    return in_file;
}

// Reading the init data section after the DMaps sets every DMap's subscreens from its type
// and the magic rule, unless the quest uses custom subscreens (loadquest in qst.cpp). Those
// two fields then say nothing about what the DMap section holds.
bool dmap_subscreens_from_init()
{
    int d = part_index(ID_DMAPS), i = part_index(ID_INITDATA);
    return i > d && d >= 0 && zinit.subscreen != ssdtMAX;
}

// Whether the DMap read back (got) is what ZQuest holds (want).
bool same_dmap(const dmap &want, const dmap &got, bool subscreens_from_init)
{
    dmap a = want, b = got;

    if(subscreens_from_init)
    {
        b.active_subscreen = a.active_subscreen;
        b.passive_subscreen = a.passive_subscreen;
    }

    Out oa, ob;
    dmap_bytes_253(oa, a);
    dmap_bytes_253(ob, b);
    return oa.b == ob.b;
}

int write_dmaps(PACKFILE *f, const ZqPartContext &ctx)
{
    if(is_253(ctx) || ctx.zelda_version < 0x193 || ctx.zelda_version >= 0x255 || ctx.version > V_DMAPS)
        return ZQW_UNSUPPORTED;

    std::vector<byte> orig;

    if(!part_bytes(*ctx.part, orig) || orig.size() < 10)
        return ERR_READ;

    DmapRead r;
    r.sv = ctx.version;
    r.version = ctx.zelda_version;
    r.build = ctx.build;
    r.viewmap_rule = false;

    // deprecated_rules in qst.cpp: the rules as the rules section holds them
    const ZqQuestPart *rules = find_part(ID_RULES);
    byte rb[2];

    if(rules && zq_part_read(*rules, 8, rb, 2))
        r.viewmap_rule = (rb[14 >> 3] >> (14 & 7)) & 1;

    const bool from_init = dmap_subscreens_from_init();
    r.subscreens_from_init = from_init;
    In in(orig, 8);
    const int old_count = in.g16();
    const int read_count = old_count < MAXDMAPS ? old_count : MAXDMAPS;
    const int count = dmap_count(read_count);

    Out o;
    o.p16((word)count);
    dmap loaded, got;

    for(int i = 0; i < count; ++i)
    {
        size_t start = in.pos;
        bool have_raw = i < read_count;

        if(have_raw)
        {
            read_dmap_old(in, r, loaded);

            if(in.bad)
                return ERR_READ;
        }

        const bool unchanged = have_raw && dmap_unchanged(i);

        if(unchanged && !zqw_force_serialize)
        {
            o.copy(orig, start, in.pos);
            continue;
        }

        // first with the original bytes of what reads the same, then without
        Out rec;
        bool ok = false;

        for(int pass = have_raw ? 0 : 1; pass < 2 && !ok; ++pass)
        {
            rec.b.clear();
            write_dmap_old(rec, r, DMaps[i], pass == 0 ? &orig : NULL, start, pass == 0 ? &loaded : NULL);
            In back(rec.b, 0);
            read_dmap_old(back, r, got);
            ok = !back.bad && back.pos == rec.b.size() && same_dmap(DMaps[i], got, from_init);
        }

        if(ok && !exact_differs(ID_DMAPS, i, unchanged, rec.b, orig, start, in.pos))
            o.pn(&rec.b[0], rec.b.size());
        else if(zqw_lenient && !rec.b.empty())
            o.pn(&rec.b[0], rec.b.size());          // as that build's ZQuest: its loader converts it
        else if(refuse(ID_DMAPS, i, unchanged, "can't be held in this version"))
            return ERR_CANT_HOLD;
        else
            o.copy(orig, start, in.pos);
    }

    o.copy(orig, in.pos, orig.size());
    return put_part(f, ctx.part->id, orig, o);
}

ZqWriterRegistration g_reg_dmaps(ID_DMAPS, write_dmaps, "DMaps, readdmaps versions");

// ---- MAP: map screens, 2.53's readmapscreen (quests before 2.55) ---------------------------

const int SCREEN_SQUARES = 176;     // 16 x 11; every map has this size in these versions

// readmapscreen's tests on the header: true when the header is 2.11 past the given build,
// or newer.
bool after_211(word version, byte build, int b)
{
    return version > 0x211 || (version == 0x211 && build > b);
}

bool same_ffc(const mapscr &a, const mapscr &b, int k);

void put_float(Out &o, long fixed)
{
    // p_igetf: a float with its bytes the other way round
    float v = (float)fixed / 10000.0f;
    byte b[sizeof(float)];
    memcpy(b, &v, sizeof(float));

    for(int i = (int)sizeof(float) - 1; i >= 0; --i)
        o.p8(b[i]);
}

// One screen in the format readmapscreen reads for that header and section version.
// With 2.53's header and v18 this is ZQuest's own writemapscreen, byte for byte.
// raw/loaded (or NULL): the original record and what it reads as. Where the reader changes
// what it reads (old sound flags, carry flags, whistle flags, an FFC's float position or
// its flags), the original bytes stay while those fields still read as ZQuest's.
void write_screen_old(Out &o, word hv, byte hb, word v, const mapscr &m, const std::vector<byte> *raw = NULL,
                      size_t raw_at = 0, const mapscr *loaded = NULL)
{
    const size_t start = o.b.size();
    size_t flags_at = 0, flags5_at = 0, flags7_at = 0;
    size_t ffc_at[MAXFFCS], ffc_end[MAXFFCS];
    const bool b7 = after_211(hv, hb, 7);
    byte flags = m.flags, flags2 = m.flags2, flags3 = m.flags3;

    // Before v14 the screen's sounds were flags (readmapscreen turns them into sounds).
    if(v < 14)
    {
        if(m.bosssfx == WAV_ROAR)
            flags |= 8;
        else if(m.bosssfx == WAV_VADER)
        {
            flags |= 8;
            flags2 |= 32;
        }
        else if(m.bosssfx == WAV_DODONGO)
        {
            flags |= 8;
            flags3 |= 2;
        }

        if(m.oceansfx == WAV_SEA)
            flags |= 128;

        if(m.secretsfx != WAV_SECRET)
            flags3 |= 64;
    }

    o.p8(m.valid);
    o.p8(m.guy);
    o.p16(m.str);
    o.p8(m.room);
    o.p8(m.item);

    if(!(hv < 0x211 || (hv == 0x211 && hb < 14)))
        o.p8(m.hasitem);

    o.p8(m.tilewarptype[0]);

    if(b7)
        for(int i = 1; i < 4; ++i) o.p8(m.tilewarptype[i]);

    o.p16(m.door_combo_set);
    o.p8(m.warpreturnx[0]);

    if(b7)
        for(int i = 1; i < 4; ++i) o.p8(m.warpreturnx[i]);

    o.p8(m.warpreturny[0]);

    if(b7)
    {
        for(int i = 1; i < 4; ++i) o.p8(m.warpreturny[i]);

        if(v >= 18)
            o.p16(m.warpreturnc);
        else
            o.p8((byte)m.warpreturnc);
    }

    o.p8(m.stairx);
    o.p8(m.stairy);
    o.p8(m.itemx);
    o.p8(m.itemy);

    if(v > 15)
        o.p16(m.color);
    else
        o.p8((byte)m.color);

    o.p8(m.enemyflags);

    for(int k = 0; k < 4; ++k) o.p8(m.door[k]);

    if(v <= 11)
    {
        o.p8((byte)m.tilewarpdmap[0]);

        if(b7)
            for(int i = 1; i < 4; ++i) o.p8((byte)m.tilewarpdmap[i]);
    }
    else
        for(int i = 0; i < 4; ++i) o.p16(m.tilewarpdmap[i]);

    o.p8(m.tilewarpscr[0]);

    if(b7)
        for(int i = 1; i < 4; ++i) o.p8(m.tilewarpscr[i]);

    if(v >= 15)
        o.p8(m.tilewarpoverlayflags);

    o.p8(m.exitdir);

    for(int k = 0; k < 10; ++k)
    {
        word e = m.enemy[k];

        if(v < 9 && e)      // read back with 10 added
            e = (word)(e - 10);

        o.p16(e);
    }

    o.p8(m.pattern);
    o.p8(m.sidewarptype[0]);

    if(b7)
        for(int i = 1; i < 4; ++i) o.p8(m.sidewarptype[i]);

    if(v >= 15)
        o.p8(m.sidewarpoverlayflags);

    o.p8(m.warparrivalx);
    o.p8(m.warparrivaly);

    for(int k = 0; k < 4; ++k) o.p8(m.path[k]);

    o.p8(m.sidewarpscr[0]);

    if(b7)
        for(int i = 1; i < 4; ++i) o.p8(m.sidewarpscr[i]);

    if(v <= 11)
    {
        o.p8((byte)m.sidewarpdmap[0]);

        if(b7)
            for(int i = 1; i < 4; ++i) o.p8((byte)m.sidewarpdmap[i]);
    }
    else
        for(int i = 0; i < 4; ++i) o.p16(m.sidewarpdmap[i]);

    if(b7)
        o.p8(m.sidewarpindex);

    o.p16(m.undercombo);
    o.p8(m.undercset);
    o.p16(m.catchall);
    flags_at = o.b.size() - start;
    o.p8(flags);
    o.p8(flags2);
    o.p8(flags3);

    if(after_211(hv, hb, 1))
        o.p8(m.flags4);

    if(b7)
    {
        flags5_at = o.b.size() - start;
        o.p8(m.flags5);
        o.p16(m.noreset);
        o.p16(m.nocarry);
    }

    if(after_211(hv, hb, 9))
        o.p8(m.flags6);

    if(v > 5)
    {
        flags7_at = o.b.size() - start;
        o.p8(m.flags7);
        o.p8(m.flags8);
        o.p8(m.flags9);
        o.p8(m.flags10);
        o.p8(m.csensitive);
    }

    if(v >= 14)
    {
        o.p8(m.oceansfx);
        o.p8(m.bosssfx);
        o.p8(m.secretsfx);
    }

    if(v >= 15)
        o.p8(m.holdupsfx);

    o.pn(m.layermap, 6);
    o.pn(m.layerscreen, 6);
    o.pn(m.layeropacity, 6);
    o.p16(m.timedwarptics);

    if(after_211(hv, hb, 2))
    {
        o.p8(m.nextmap);
        o.p8(m.nextscr);
    }

    for(int k = 0; k < 128; ++k) o.p16(m.secretcombo[k]);

    o.pn(m.secretcset, 128);
    o.pn(m.secretflag, 128);

    for(int k = 0; k < SCREEN_SQUARES; ++k) o.p16(k < (int)m.data.size() ? m.data[k] : 0);
    for(int k = 0; k < SCREEN_SQUARES; ++k) o.p8(k < (int)m.sflag.size() ? m.sflag[k] : 0);
    for(int k = 0; k < SCREEN_SQUARES; ++k) o.p8(k < (int)m.cset.size() ? m.cset[k] : 0);

    if(v > 4)
        o.p16((word)m.screen_midi);

    if(v >= 17)
        o.p8(m.lens_layer);

    for(int k = 0; k < MAXFFCS; ++k)
        ffc_at[k] = ffc_end[k] = 0;

    if(v > 6)
    {
        o.p32(m.numff);

        for(int k = 0; k < MAXFFCS; ++k)
        {
            if(!((m.numff >> k) & 1))
                continue;

            ffc_at[k] = o.b.size() - start;
            o.p16(m.ffdata[k]);
            o.p8(m.ffcset[k]);
            o.p16(m.ffdelay[k]);

            if(v < 9)
            {
                put_float(o, m.ffx[k]);
                put_float(o, m.ffy[k]);
                put_float(o, m.ffxdelta[k]);
                put_float(o, m.ffydelta[k]);
                put_float(o, m.ffxdelta2[k]);
                put_float(o, m.ffydelta2[k]);
            }
            else
            {
                o.p32((dword)m.ffx[k]);
                o.p32((dword)m.ffy[k]);
                o.p32((dword)m.ffxdelta[k]);
                o.p32((dword)m.ffydelta[k]);
                o.p32((dword)m.ffxdelta2[k]);
                o.p32((dword)m.ffydelta2[k]);
            }

            o.p8(m.fflink[k]);

            if(v > 7)
            {
                o.p8(m.ffwidth[k]);
                o.p8(m.ffheight[k]);
                o.p32(m.ffflags[k]);
            }

            if(v > 9)
                o.p16(m.ffscript[k]);

            if(v > 10)
            {
                for(int q = 0; q < 8; ++q)
                    o.p32((dword)m.initd[k][q]);

                o.p8((byte)(m.inita[k][0] / 10000));
                o.p8((byte)(m.inita[k][1] / 10000));
            }

            ffc_end[k] = o.b.size() - start;
        }
    }

    // The original's fields are where these are while the layout is the same: the version
    // decides it, and past the flags which FFCs there are.
    if(!raw || !loaded || raw_at + o.b.size() - start > raw->size())
        return;

    const mapscr &l = *loaded;
    byte *out = &o.b[start];
    const byte *in = &(*raw)[raw_at];

    if(v < 14 && l.flags == m.flags && l.flags2 == m.flags2 && l.flags3 == m.flags3 && l.bosssfx == m.bosssfx &&
       l.oceansfx == m.oceansfx && l.secretsfx == m.secretsfx && l.flags7 == m.flags7)
        memcpy(out + flags_at, in + flags_at, 3);

    if(b7 && l.flags5 == m.flags5 && l.noreset == m.noreset && l.nocarry == m.nocarry)
        memcpy(out + flags5_at, in + flags5_at, 5);

    if(v > 5 && v < 13 && l.flags7 == m.flags7 && l.flags == m.flags)
        out[flags7_at] = in[flags7_at];

    if(v > 6 && l.numff == m.numff)
        for(int k = 0; k < MAXFFCS; ++k)
            if(ffc_end[k] > ffc_at[k] && same_ffc(l, m, k))
                memcpy(out + ffc_at[k], in + ffc_at[k], ffc_end[k] - ffc_at[k]);
}

void screen_bytes_253(Out &o, const mapscr &m)
{
    write_screen_old(o, 0x250, 31, 18, m);
}

uint64_t screen_hash(const mapscr &m)
{
    Out o;
    screen_bytes_253(o, m);
    return fnv(o.b);
}

std::vector<uint64_t> g_screens_loaded;   // each screen's hash in 2.53's form

// The open quest's MAP section version when it is a 2.55 / 3.0 one (0 otherwise).
word newer_map_version()
{
    if(!zq255_quest_is_newer() || header.zelda_version != 0x255)
        return 0;

    const ZqQuestPart *part = find_part(ID_MAPS);
    byte b[2];

    if(!part || part->length < 10 || !zq_part_read(*part, 0, b, 2))
        return 0;

    const word v = (word)(b[0] | (b[1] << 8));
    return v >= 18 && v <= 41 ? v : 0;
}

// What upstream's mapscr starts with (2.55.18 and 3.0) where 2.53's clearscr and the reader of
// an unused screen leave something else: what a screen gets once it is used.
void unused_defaults(mapscr &m)
{
    m.csensitive = 1;
    m.secretsfx = 27;
    m.holdupsfx = 20;
    m.screen_midi = -1;

    for(int q = 0; q < 4; ++q)
        m.sidewarptype[q] = wtSCROLL;
}

void note_maps()
{
    // An unused screen of a v23+ quest: nothing of it is in the file, and the reader leaves it
    // cleared; ZQuest holds it as upstream's ZQuest does, so a paste or a combo drawn there
    // makes the screen upstream's would be.
    if(newer_map_version() >= 23)
        for(size_t i = 0; i < TheMaps.size(); ++i)
            if(!(TheMaps[i].valid & mVALID))
                unused_defaults(TheMaps[i]);

    g_screens_loaded.resize(TheMaps.size());

    for(size_t i = 0; i < TheMaps.size(); ++i)
        g_screens_loaded[i] = screen_hash(TheMaps[i]);

    if(!mutate())
        return;

    for(size_t i = 2; i < TheMaps.size(); i += 5)
    {
        mapscr &m = TheMaps[i];

        if(!(m.valid & mVALID))
            continue;

        m.data[17] = m.data[16];
        m.cset[17] = m.cset[16];
        m.sflag[18] = m.sflag[16];
        m.secretcombo[5] = m.secretcombo[4];
        m.door[0] = m.door[1];
        m.enemy[9] = m.enemy[8];
        m.warparrivalx = m.warparrivaly;

        for(int k = 0; k < MAXFFCS; ++k)
            if((m.numff >> k) & 1)
            {
                m.ffcset[k] = m.ffcset[k] & 0x0F;
                m.fflink[k] = 0;
                break;
            }
    }
}

ZqLoadHookRegistration g_hook_maps(note_maps);

bool screen_unchanged(size_t index)
{
    return index < TheMaps.size() && index < g_screens_loaded.size() && screen_hash(TheMaps[index]) == g_screens_loaded[index];
}

bool same_screen(const mapscr &want, const mapscr &got)
{
    Out a, b;
    screen_bytes_253(a, want);
    screen_bytes_253(b, got);

    if(a.b != b.b && strict())
    {
        size_t k = 0;

        while(k < a.b.size() && k < b.b.size() && a.b[k] == b.b[k])
            ++k;

        al_trace("zqw MAP : reads back different at byte %d of 2.53's form (%d, read %d)\n", (int)k,
                 k < a.b.size() ? a.b[k] : -1, k < b.b.size() ? b.b[k] : -1);
    }

    return a.b == b.b;
}

// Reads one screen with readmapscreen from bytes[at...]; returns how many bytes it took,
// 0 when it failed.
size_t read_screen_old(const std::vector<byte> &bytes, size_t at, zquestheader &h, word version, mapscr &out)
{
    MemFile m;
    m.d = bytes.empty() ? NULL : &bytes[0];
    m.n = bytes.size();
    m.pos = at;
    m.ungot = -1;
    PACKFILE *f = pack_fopen_vtable(&g_mem_vtable, &m);

    if(!f)
        return 0;

    zcmap zm;
    memset(&zm, 0, sizeof(zm));
    zm.tileWidth = 16;
    zm.tileHeight = 11;
    clear_screen(&out);
    int ret = readmapscreen(f, &h, &out, &zm, version);
    size_t used = mf_used(m) - at;
    pack_fclose(f);
    return ret == 0 ? used : 0;
}

// After a quest whose maps are v11 or older loads, FFCs showing a changer combo get the
// changer flag (loadquest's fixffcs).
void fix_old_ffcs(mapscr &m, word version)
{
    if(version > 11)
        return;

    for(int k = 0; k < MAXFFCS; ++k)
        if(combobuf[m.ffdata[k]].type == cCHANGE)
            m.ffflags[k] |= ffCHANGER;
}

int write_maps(PACKFILE *f, const ZqPartContext &ctx)
{
    if(is_253(ctx) || ctx.zelda_version < 0x193 || ctx.zelda_version >= 0x255 || ctx.version > V_MAPS)
        return ZQW_UNSUPPORTED;

    std::vector<byte> orig;

    if(!part_bytes(*ctx.part, orig) || orig.size() < 10)
        return ERR_READ;

    zquestheader h = header_for(ctx);
    const word v = ctx.version;
    In in(orig, 8);
    const int old_maps = in.g16();
    const int read_maps = old_maps < MAXMAPS2 ? old_maps : MAXMAPS2;
    const int maps = map_count < MAXMAPS2 ? map_count : MAXMAPS2;
    size_t pos = 10;
    Out o;
    o.p16((word)maps);
    mapscr loaded, got;

    for(int i = 0; i < (maps > read_maps ? maps : read_maps); ++i)
    {
        for(int j = 0; j < MAPSCRS; ++j)
        {
            const size_t index = (size_t)i * MAPSCRS + j;
            size_t start = pos, used = 0;

            if(i < read_maps)
            {
                used = read_screen_old(orig, pos, h, v, loaded);

                if(!used)
                    return ERR_READ;

                fix_old_ffcs(loaded, v);
                pos += used;
            }

            if(i >= maps)
                continue;       // the map was deleted

            if(index >= TheMaps.size())
                return ERR_READ;

            const bool unchanged = used && screen_unchanged(index);

            if(unchanged && !zqw_force_serialize)
            {
                o.copy(orig, start, pos);
                continue;
            }

            // first with the original bytes of what reads the same, then without
            Out rec;
            bool ok = false;

            for(int pass = used ? 0 : 1; pass < 2 && !ok; ++pass)
            {
                rec.b.clear();
                write_screen_old(rec, h.zelda_version, h.build, v, TheMaps[index], pass == 0 ? &orig : NULL, start,
                                 pass == 0 ? &loaded : NULL);
                size_t back = read_screen_old(rec.b, 0, h, v, got);
                fix_old_ffcs(got, v);
                ok = back == rec.b.size() && same_screen(TheMaps[index], got);
            }

            if(ok && !exact_differs(ID_MAPS, (int)index, unchanged, rec.b, orig, start, pos))
                o.pn(&rec.b[0], rec.b.size());
            else if(zqw_lenient && !rec.b.empty())
                o.pn(&rec.b[0], rec.b.size());      // as that build's ZQuest: its loader converts it
            else if(refuse(ID_MAPS, (int)index, unchanged, "can't be held in this version"))
                return ERR_CANT_HOLD;
            else
                o.copy(orig, start, pos);
        }
    }

    o.copy(orig, pos, orig.size());
    return put_part(f, ctx.part->id, orig, o);
}

ZqWriterRegistration g_reg_maps(ID_MAPS, write_maps, "maps, readmapscreen versions");

// ---- MAP: 2.55 and 3.0 (q255_read_maps, versions 18-41) -----------------------------------

word get16(const byte *p) { return (word)(p[0] | (p[1] << 8)); }
dword get32(const byte *p) { return (dword)get16(p) | ((dword)get16(p + 2) << 16); }

// Upstream's flag numbers: its mfPUSHED is 104, 2.53's comes after its mfMAX.
byte flag_255(byte flag)
{
    return flag == mfPUSHED ? (byte)104 : flag;
}

// One screen as the 2.55 reader reads it from bytes[at...]; how many bytes it took, 0 when
// it failed.
size_t read_screen_255(const std::vector<byte> &bytes, size_t at, word v, int screen, const word *autolayers,
                       word autopalette, mapscr &out)
{
    Q255Section s;
    s.data = bytes.empty() ? NULL : &bytes[0];
    s.size = bytes.size();
    s.pos = at;
    s.version = v;
    s.keep = false;
    int ret = q255_editor_read_screen(s, &out, screen, autolayers, autopalette);
    return ret == 0 && s.pos > at ? s.pos - at : 0;
}

// The flat format up to v22 starts with what 2.53's v18 holds, in 2.53's order: the values
// as the file has them, before the reader narrows them (doors, rooms, flags and the rest).
void parse_screen_253(In &in, mapscr &m)
{
    clear_screen(&m);
    m.valid = in.g8();
    m.guy = in.g8();
    m.str = in.g16();
    m.room = in.g8();
    m.item = in.g8();
    m.hasitem = in.g8();
    for(int k = 0; k < 4; ++k) m.tilewarptype[k] = in.g8();
    m.door_combo_set = in.g16();
    for(int k = 0; k < 4; ++k) m.warpreturnx[k] = in.g8();
    for(int k = 0; k < 4; ++k) m.warpreturny[k] = in.g8();
    m.warpreturnc = in.g16();
    m.stairx = in.g8();
    m.stairy = in.g8();
    m.itemx = in.g8();
    m.itemy = in.g8();
    m.color = in.g16();
    m.enemyflags = in.g8();
    for(int k = 0; k < 4; ++k) m.door[k] = in.g8();
    for(int k = 0; k < 4; ++k) m.tilewarpdmap[k] = in.g16();
    for(int k = 0; k < 4; ++k) m.tilewarpscr[k] = in.g8();
    m.tilewarpoverlayflags = in.g8();
    m.exitdir = in.g8();
    for(int k = 0; k < 10; ++k) m.enemy[k] = in.g16();
    m.pattern = in.g8();
    for(int k = 0; k < 4; ++k) m.sidewarptype[k] = in.g8();
    m.sidewarpoverlayflags = in.g8();
    m.warparrivalx = in.g8();
    m.warparrivaly = in.g8();
    for(int k = 0; k < 4; ++k) m.path[k] = in.g8();
    for(int k = 0; k < 4; ++k) m.sidewarpscr[k] = in.g8();
    for(int k = 0; k < 4; ++k) m.sidewarpdmap[k] = in.g16();
    m.sidewarpindex = in.g8();
    m.undercombo = in.g16();
    m.undercset = in.g8();
    m.catchall = in.g16();
    m.flags = in.g8();
    m.flags2 = in.g8();
    m.flags3 = in.g8();
    m.flags4 = in.g8();
    m.flags5 = in.g8();
    m.noreset = in.g16();
    m.nocarry = in.g16();
    m.flags6 = in.g8();
    m.flags7 = in.g8();
    m.flags8 = in.g8();
    m.flags9 = in.g8();
    m.flags10 = in.g8();
    m.csensitive = in.g8();
    m.oceansfx = in.g8();
    m.bosssfx = in.g8();
    m.secretsfx = in.g8();
    m.holdupsfx = in.g8();
    for(int k = 0; k < 6; ++k) m.layermap[k] = in.g8();
    for(int k = 0; k < 6; ++k) m.layerscreen[k] = in.g8();
    for(int k = 0; k < 6; ++k) m.layeropacity[k] = in.g8();
    m.timedwarptics = in.g16();
    m.nextmap = in.g8();
    m.nextscr = in.g8();
    for(int k = 0; k < 128; ++k) m.secretcombo[k] = in.g16();
    for(int k = 0; k < 128; ++k) m.secretcset[k] = in.g8();
    for(int k = 0; k < 128; ++k) m.secretflag[k] = in.g8();
    for(int k = 0; k < SCREEN_SQUARES; ++k) m.data[k] = in.g16();
    for(int k = 0; k < SCREEN_SQUARES; ++k) m.sflag[k] = in.g8();
    for(int k = 0; k < SCREEN_SQUARES; ++k) m.cset[k] = in.g8();
    m.screen_midi = (short)in.g16();
    m.lens_layer = in.g8();
    m.numff = in.g32();

    for(int k = 0; k < MAXFFCS; ++k)
    {
        if(!((m.numff >> k) & 1))
            continue;

        m.ffdata[k] = in.g16();
        m.ffcset[k] = in.g8();
        m.ffdelay[k] = in.g16();
        m.ffx[k] = (long)in.g32();
        m.ffy[k] = (long)in.g32();
        m.ffxdelta[k] = (long)in.g32();
        m.ffydelta[k] = (long)in.g32();
        m.ffxdelta2[k] = (long)in.g32();
        m.ffydelta2[k] = (long)in.g32();
        m.fflink[k] = in.g8();
        m.ffwidth[k] = in.g8();
        m.ffheight[k] = in.g8();
        m.ffflags[k] = in.g32();
        m.ffscript[k] = in.g16();
        for(int q = 0; q < 8; ++q) m.initd[k][q] = (long)in.g32();
        m.inita[k][0] = in.g8() * 10000;
        m.inita[k][1] = in.g8() * 10000;
    }
}

bool same_ffc(const mapscr &a, const mapscr &b, int k)
{
    bool pa = (a.numff >> k) & 1, pb = (b.numff >> k) & 1;

    if(pa != pb)
        return false;

    if(!pa)
        return true;

    if(a.ffdata[k] != b.ffdata[k] || a.ffcset[k] != b.ffcset[k] || a.ffdelay[k] != b.ffdelay[k] ||
       a.ffx[k] != b.ffx[k] || a.ffy[k] != b.ffy[k] || a.ffxdelta[k] != b.ffxdelta[k] || a.ffydelta[k] != b.ffydelta[k] ||
       a.ffxdelta2[k] != b.ffxdelta2[k] || a.ffydelta2[k] != b.ffydelta2[k] || a.fflink[k] != b.fflink[k] ||
       a.ffwidth[k] != b.ffwidth[k] || a.ffheight[k] != b.ffheight[k] || a.ffflags[k] != b.ffflags[k] ||
       a.ffscript[k] != b.ffscript[k] || a.inita[k][0] != b.inita[k][0] || a.inita[k][1] != b.inita[k][1])
        return false;

    return !memcmp(a.initd[k], b.initd[k], sizeof(a.initd[k]));
}

bool same_enemies(const mapscr &a, const mapscr &b)
{
    return a.pattern == b.pattern && !memcmp(a.enemy, b.enemy, sizeof(a.enemy));
}

// A changed screen of the flat format (v18-22): ZQuest's values, except that a field the
// reader narrows keeps the file's value when it still reads as ZQuest's (raw: the record's
// values as the file has them, loaded: as they read). keep false puts ZQuest's in everywhere.
void merge_flat(mapscr &out, const mapscr &now, const mapscr &loaded, const mapscr &raw, bool keep)
{
    out = now;

    for(int k = 0; k < 128; ++k)
        out.secretflag[k] = flag_255(now.secretflag[k]);

    for(int k = 0; k < SCREEN_SQUARES; ++k)
        out.sflag[k] = flag_255(now.sflag[k]);

    if(!keep)
        return;

    if(loaded.room == now.room) out.room = raw.room;
    for(int k = 0; k < 4; ++k)
        if(loaded.door[k] == now.door[k]) out.door[k] = raw.door[k];

    if(same_enemies(loaded, now))
    {
        out.pattern = raw.pattern;
        memcpy(out.enemy, raw.enemy, sizeof(out.enemy));
    }

    if(loaded.undercombo == now.undercombo) out.undercombo = raw.undercombo;

    if(loaded.flags5 == now.flags5 && loaded.noreset == now.noreset)
    {
        out.flags5 = raw.flags5;
        out.noreset = raw.noreset;
    }

    for(int k = 0; k < 128; ++k)
    {
        if(loaded.secretcombo[k] == now.secretcombo[k]) out.secretcombo[k] = raw.secretcombo[k];
        if(loaded.secretflag[k] == now.secretflag[k]) out.secretflag[k] = raw.secretflag[k];
    }

    for(int k = 0; k < SCREEN_SQUARES; ++k)
    {
        if(loaded.data[k] == now.data[k]) out.data[k] = raw.data[k];
        if(loaded.sflag[k] == now.sflag[k]) out.sflag[k] = raw.sflag[k];
    }

    for(int k = 0; k < MAXFFCS; ++k)
    {
        if(!((loaded.numff >> k) & 1) || !((now.numff >> k) & 1) || !((raw.numff >> k) & 1))
            continue;

        if(loaded.ffdata[k] == now.ffdata[k]) out.ffdata[k] = raw.ffdata[k];
        if(loaded.ffflags[k] == now.ffflags[k]) out.ffflags[k] = raw.ffflags[k];
    }
}

// The groups of the v23+ format, in file order, and their bits in the screen's "has" dword
// (the DMap of a warp return has none of its own: it comes with either kind of warp).
enum { G_ROOM, G_ITEM, G_WRC, G_TWARP, G_SWARP, G_WARPRET, G_LAYERS, G_MAZE, G_DSU, G_FLAGS, G_ENEMY,
       G_CARRY, G_SCRIPT, G_SECRETS, G_COMBOFLAG, G_MISC, G_COUNT };

const dword g_group_bit[G_COUNT] =
{
    0x0001, 0x0002, 0, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200,
    0x0400, 0x0800, 0x2000, 0x4000, 0x8000
};

// Where everything is in a v23+ screen record.
struct Grouped
{
    byte valid;
    dword has;
    size_t at[G_COUNT], end[G_COUNT];
    dword mask;                         // before v25: which FFCs are there
    std::vector<size_t> ffc_at, ffc_end;    // each FFC slot (by mask before v25)
    size_t notes_at, notes_end, grav_at, grav_end;
};

void skip_script_config(In &in)
{
    in.skip(2);
    in.skip((size_t)in.g16() * 4);
    in.skip((size_t)in.g16() * 6);
}

size_t ffc_tail(word v)   // what follows the combo in an FFC
{
    return 1 + 2 + 24 + 1 + (v < 24 ? 2 : 10) + 4;
}

void skip_ffc(In &in, word v, bool masked)
{
    word combo = in.g16();

    if(!masked && combo == 0)
        return;

    in.skip(ffc_tail(v));

    if(v < 41)
        in.skip(2 + 32);
    else
        skip_script_config(in);

    in.skip(v < 33 ? 2 : 1);

    if(v >= 40)
        in.skip(8);
}

bool parse_grouped(const std::vector<byte> &b, size_t at, word v, Grouped &g)
{
    In in(b, at);
    g.valid = in.g8();
    g.has = 0;
    g.mask = 0;
    g.ffc_at.clear();
    g.ffc_end.clear();

    for(int i = 0; i < G_COUNT; ++i)
        g.at[i] = g.end[i] = 0;

    g.notes_at = g.notes_end = g.grav_at = g.grav_end = 0;

    if(!(g.valid & mVALID))
        return !in.bad;

    g.has = in.g32();
    byte flags10 = 0;

    for(int i = 0; i < G_COUNT && !in.bad; ++i)
    {
        bool there = i == G_WRC ? (g.has & (g_group_bit[G_TWARP] | g_group_bit[G_SWARP])) != 0 : (g.has & g_group_bit[i]) != 0;

        if(!there)
            continue;

        g.at[i] = in.pos;

        switch(i)
        {
        case G_ROOM:      in.skip(1 + (v > 26 ? 7 : 0) + 2 + 1 + 2); break;
        case G_ITEM:      in.skip((v < 39 ? 1 : 2) + 3); break;
        case G_WRC:       in.skip(2); break;
        case G_TWARP:     in.skip(17); break;
        case G_SWARP:     in.skip(18); break;
        case G_WARPRET:   in.skip(10); break;
        case G_LAYERS:    in.skip(20); break;
        case G_MAZE:      in.skip(5 + (v >= 32 ? 1 : 0)); break;
        case G_DSU:       in.skip(11); break;
        case G_FLAGS:     in.skip(9); flags10 = in.g8(); in.skip(1); break;
        case G_ENEMY:     in.skip(21); break;
        case G_CARRY:     in.skip((v < 34 ? 4 : 16) + 2); break;
        case G_SCRIPT:
            if(v < 41)
                in.skip(2 + 1 + 32);
            else
            {
                skip_script_config(in);
                in.skip(1);
            }
            break;
        case G_SECRETS:   in.skip(512); break;
        case G_COMBOFLAG: in.skip(704); break;
        case G_MISC:      in.skip(3 + (v < 38 ? 4 : 8) + 2 + (v < 37 ? 2 : 4) + 1 + (v > 27 ? 2 : 0)); break;
        }

        g.end[i] = in.pos;
    }

    const bool masked = v < 25;
    int slots = MAXFFCS;

    if(masked)
        g.mask = in.g32();
    else
        slots = in.g16();

    for(int m = 0; m < slots && !in.bad; ++m)
    {
        if(masked && !((g.mask >> m) & 1))
        {
            g.ffc_at.push_back(0);
            g.ffc_end.push_back(0);
            continue;
        }

        g.ffc_at.push_back(in.pos);
        skip_ffc(in, v, masked);
        g.ffc_end.push_back(in.pos);
    }

    if(v > 29)
    {
        g.notes_at = in.pos;
        in.skip(in.g32());
        g.notes_end = in.pos;
    }

    if(v >= 35 && (flags10 & 0x03))
    {
        g.grav_at = in.pos;
        in.skip(8);
        g.grav_end = in.pos;
    }

    return !in.bad;
}

// Whether a group reads the same from two screens (the fields 2.53 holds).
bool same_group(int i, const mapscr &a, const mapscr &b)
{
    switch(i)
    {
    case G_ROOM:
        return a.guy == b.guy && a.str == b.str && a.room == b.room && a.catchall == b.catchall;
    case G_ITEM:
        return a.item == b.item && a.hasitem == b.hasitem && a.itemx == b.itemx && a.itemy == b.itemy;
    case G_WRC:
        return a.warpreturnc == b.warpreturnc;
    case G_TWARP:
        return !memcmp(a.tilewarptype, b.tilewarptype, 4) && !memcmp(a.tilewarpdmap, b.tilewarpdmap, sizeof(a.tilewarpdmap)) &&
               !memcmp(a.tilewarpscr, b.tilewarpscr, 4) && a.tilewarpoverlayflags == b.tilewarpoverlayflags;
    case G_SWARP:
        return !memcmp(a.sidewarptype, b.sidewarptype, 4) && !memcmp(a.sidewarpdmap, b.sidewarpdmap, sizeof(a.sidewarpdmap)) &&
               !memcmp(a.sidewarpscr, b.sidewarpscr, 4) && a.sidewarpoverlayflags == b.sidewarpoverlayflags &&
               a.sidewarpindex == b.sidewarpindex;
    case G_WARPRET:
        return !memcmp(a.warpreturnx, b.warpreturnx, 4) && !memcmp(a.warpreturny, b.warpreturny, 4) &&
               a.warparrivalx == b.warparrivalx && a.warparrivaly == b.warparrivaly;
    case G_LAYERS:
        return !memcmp(a.layermap, b.layermap, 6) && !memcmp(a.layerscreen, b.layerscreen, 6) && !memcmp(a.layeropacity, b.layeropacity, 6);
    case G_MAZE:
        return !memcmp(a.path, b.path, 4) && a.exitdir == b.exitdir;
    case G_DSU:
        return a.door_combo_set == b.door_combo_set && !memcmp(a.door, b.door, 4) && a.stairx == b.stairx &&
               a.stairy == b.stairy && a.undercombo == b.undercombo && a.undercset == b.undercset;
    case G_FLAGS:
        return a.flags == b.flags && a.flags2 == b.flags2 && a.flags3 == b.flags3 && a.flags4 == b.flags4 &&
               a.flags5 == b.flags5 && a.flags6 == b.flags6 && a.flags7 == b.flags7 && a.flags8 == b.flags8 &&
               a.flags9 == b.flags9 && a.flags10 == b.flags10 && a.enemyflags == b.enemyflags;
    case G_ENEMY:
        return same_enemies(a, b);
    case G_CARRY:
        return a.noreset == b.noreset && a.nocarry == b.nocarry && a.nextmap == b.nextmap && a.nextscr == b.nextscr;
    case G_SCRIPT:
        return true;    // nothing in it that 2.53 holds
    case G_SECRETS:
        return !memcmp(a.secretcombo, b.secretcombo, sizeof(a.secretcombo)) && !memcmp(a.secretcset, b.secretcset, 128) &&
               !memcmp(a.secretflag, b.secretflag, 128);
    case G_COMBOFLAG:
        return a.data == b.data && a.sflag == b.sflag && a.cset == b.cset;
    case G_MISC:
        return a.color == b.color && a.csensitive == b.csensitive && a.oceansfx == b.oceansfx && a.bosssfx == b.bosssfx &&
               a.secretsfx == b.secretsfx && a.holdupsfx == b.holdupsfx && a.timedwarptics == b.timedwarptics &&
               a.screen_midi == b.screen_midi && a.lens_layer == b.lens_layer;
    }

    return true;
}

// One group of a changed screen. rp: the group's original bytes, NULL when the screen had
// none; then the fields 2.53 doesn't hold get upstream's defaults (0). A field the reader
// narrows keeps its original bytes when it still reads as ZQuest's value (keep).
void put_group(Out &o, int i, word v, const mapscr &c, const mapscr &l, const byte *rp, bool keep)
{
    const byte *kp = keep ? rp : NULL;   // where narrowed fields may come from

    switch(i)
    {
    case G_ROOM:
    {
        const size_t room_at = 1 + (v > 26 ? 7 : 0) + 2;
        o.p8(c.guy);

        if(v > 26)
        {
            // guy tile, CSet, room flags; a screen without the group has upstream's -1 for the
            // tile ("the guy's own"), which 0 isn't (tile 0)
            if(rp) o.pn(rp + 1, 7);
            else { o.p32(0xFFFFFFFF); o.p8(0); o.p16(0); }
        }

        o.p16(c.str);
        o.p8(kp && l.room == c.room ? kp[room_at] : c.room);
        o.p16(kp && l.catchall == c.catchall ? get16(kp + room_at + 1) : c.catchall);
        break;
    }

    case G_ITEM:
    {
        const size_t ib = v < 39 ? 1 : 2;

        if(kp && l.item == c.item && l.hasitem == c.hasitem)
            o.pn(kp, ib + 1);
        else
        {
            if(ib == 1) o.p8(c.item);
            else o.p16(c.item);

            o.p8(c.hasitem);
        }

        o.p8(c.itemx);
        o.p8(c.itemy);
        break;
    }

    case G_WRC:
        o.p16(c.warpreturnc);
        break;

    case G_TWARP:
        o.pn(c.tilewarptype, 4);
        for(int k = 0; k < 4; ++k) o.p16(c.tilewarpdmap[k]);
        o.pn(c.tilewarpscr, 4);
        o.p8(c.tilewarpoverlayflags);
        break;

    case G_SWARP:
        o.pn(c.sidewarptype, 4);
        for(int k = 0; k < 4; ++k) o.p16(c.sidewarpdmap[k]);
        o.pn(c.sidewarpscr, 4);
        o.p8(c.sidewarpoverlayflags);
        o.p8(c.sidewarpindex);
        break;

    case G_WARPRET:
        o.pn(c.warpreturnx, 4);
        o.pn(c.warpreturny, 4);
        o.p8(c.warparrivalx);
        o.p8(c.warparrivaly);
        break;

    case G_LAYERS:
        o.pn(c.layermap, 6);
        o.pn(c.layerscreen, 6);
        o.pn(c.layeropacity, 6);
        if(rp) o.pn(rp + 18, 2);
        else { o.p8(0); o.p8(0); }
        break;

    case G_MAZE:
        o.pn(c.path, 4);
        o.p8(c.exitdir);
        if(v >= 32) o.p8(rp ? rp[5] : 0);
        break;

    case G_DSU:
        o.p16(c.door_combo_set);
        for(int k = 0; k < 4; ++k) o.p8(kp && l.door[k] == c.door[k] ? kp[2 + k] : c.door[k]);
        o.p8(c.stairx);
        o.p8(c.stairy);
        o.p16(kp && l.undercombo == c.undercombo ? get16(kp + 8) : c.undercombo);
        o.p8(c.undercset);
        break;

    case G_FLAGS:
        o.p8(c.flags); o.p8(c.flags2); o.p8(c.flags3); o.p8(c.flags4); o.p8(c.flags5);
        o.p8(c.flags6); o.p8(c.flags7); o.p8(c.flags8); o.p8(c.flags9); o.p8(c.flags10);
        o.p8(c.enemyflags);
        break;

    case G_ENEMY:
        for(int k = 0; k < 10; ++k) o.p16(c.enemy[k]);
        o.p8(c.pattern);
        break;

    case G_CARRY:
        if(v < 34)
        {
            o.p16(c.noreset);
            o.p16(c.nocarry);
        }
        else
        {
            // upstream's are 32 bits; 2.53 holds the low 16
            o.p32((rp ? get32(rp) & 0xFFFF0000 : 0) | c.noreset);
            o.p32((rp ? get32(rp + 4) & 0xFFFF0000 : 0) | c.nocarry);
            if(rp) o.pn(rp + 8, 8);
            else for(int k = 0; k < 8; ++k) o.p8(0);
        }

        o.p8(c.nextmap);
        o.p8(c.nextscr);
        break;

    case G_SECRETS:
        for(int k = 0; k < 128; ++k) o.p16(kp && l.secretcombo[k] == c.secretcombo[k] ? get16(kp + 2 * k) : c.secretcombo[k]);
        o.pn(c.secretcset, 128);
        for(int k = 0; k < 128; ++k) o.p8(kp && l.secretflag[k] == c.secretflag[k] ? kp[384 + k] : flag_255(c.secretflag[k]));
        break;

    case G_COMBOFLAG:
        for(int k = 0; k < SCREEN_SQUARES; ++k) o.p16(kp && l.data[k] == c.data[k] ? get16(kp + 2 * k) : c.data[k]);
        for(int k = 0; k < SCREEN_SQUARES; ++k) o.p8(kp && l.sflag[k] == c.sflag[k] ? kp[352 + k] : flag_255(c.sflag[k]));
        for(int k = 0; k < SCREEN_SQUARES; ++k) o.p8(c.cset[k]);
        break;

    case G_MISC:
    {
        o.p16(c.color);
        o.p8(c.csensitive);
        size_t at = 3;
        const byte sfx[4] = { c.oceansfx, c.bosssfx, c.secretsfx, c.holdupsfx };
        const byte lsfx[4] = { l.oceansfx, l.bosssfx, l.secretsfx, l.holdupsfx };

        for(int k = 0; k < 4; ++k)
        {
            if(v < 38)
                o.p8(sfx[k]);
            else
                o.p16(kp && lsfx[k] == sfx[k] ? get16(kp + at + 2 * k) : sfx[k]);
        }

        at += v < 38 ? 4 : 8;
        o.p16(c.timedwarptics);
        at += 2;

        if(v < 37)
            o.p16((word)c.screen_midi);
        else
            o.p32(kp && l.screen_midi == c.screen_midi ? get32(kp + at) : (dword)(long)c.screen_midi);

        at += v < 37 ? 2 : 4;

        if(kp && l.lens_layer == c.lens_layer)
            o.pn(kp + at, 1 + (v > 27 ? 2 : 0));
        else
        {
            o.p8(c.lens_layer);

            if(v > 27)
            {
                o.p8(0);
                o.p8(0);
            }
        }
        break;
    }
    }
}

// One FFC of a changed v23+ screen; rp its original bytes or NULL.
void put_ffc(Out &o, word v, const mapscr &c, const mapscr &l, int k, const byte *rp, bool keep)
{
    const bool same_l = rp && keep && ((l.numff >> k) & 1);
    o.p16(same_l && l.ffdata[k] == c.ffdata[k] ? get16(rp) : c.ffdata[k]);
    o.p8(c.ffcset[k]);
    o.p16(c.ffdelay[k]);
    o.p32((dword)c.ffx[k]);
    o.p32((dword)c.ffy[k]);
    o.p32((dword)c.ffxdelta[k]);
    o.p32((dword)c.ffydelta[k]);
    o.p32((dword)c.ffxdelta2[k]);
    o.p32((dword)c.ffydelta2[k]);
    o.p8(c.fflink[k]);
    size_t at = 2 + 1 + 2 + 24 + 1;

    if(v < 24)
    {
        o.p8(c.ffwidth[k]);
        o.p8(c.ffheight[k]);
        at += 2;
    }
    else
    {
        // a hitbox and a tile size of their own, which ffwidth and ffheight hold narrowed
        const bool w = same_l && l.ffwidth[k] == c.ffwidth[k], h = same_l && l.ffheight[k] == c.ffheight[k];
        o.p32(w ? get32(rp + at) : (dword)((c.ffwidth[k] & 63) + 1));
        o.p32(h ? get32(rp + at + 4) : (dword)((c.ffheight[k] & 63) + 1));
        o.p8(w ? rp[at + 8] : (byte)((c.ffwidth[k] >> 6) + 1));
        o.p8(h ? rp[at + 9] : (byte)((c.ffheight[k] >> 6) + 1));
        at += 10;
    }

    o.p32(same_l && l.ffflags[k] == c.ffflags[k] ? get32(rp + at) : c.ffflags[k]);
    at += 4;

    if(v < 41)
    {
        o.p16(c.ffscript[k]);
        for(int q = 0; q < 8; ++q) o.p32((dword)c.initd[k][q]);
        at += 2 + 32;
    }
    else
    {
        const size_t sc = at;
        word args = rp ? get16(rp + sc + 2) : 0;
        const size_t inst_at = sc + 4 + (size_t)args * 4;
        word inst = rp ? get16(rp + inst_at) : 0;
        const size_t sc_end = inst_at + 2 + (size_t)inst * 6;

        if(rp && l.ffscript[k] == c.ffscript[k] && !memcmp(l.initd[k], c.initd[k], sizeof(c.initd[k])))
            o.pn(rp + sc, sc_end - sc);
        else
        {
            // the script, its run arguments (the first 8 are the InitDs) and inst_init as it was
            o.p16(c.ffscript[k]);
            word n = args > 8 ? args : 8;
            o.p16(n);

            for(word q = 0; q < n; ++q)
                o.p32(q < 8 ? (dword)c.initd[k][q] : get32(rp + sc + 4 + 4 * q));

            if(rp) o.pn(rp + inst_at, 2 + (size_t)inst * 6);
            else o.p16(0);
        }

        at = sc_end;
    }

    if(v < 33)
    {
        o.p8((byte)(c.inita[k][0] / 10000));
        o.p8((byte)(c.inita[k][1] / 10000));
        at += 2;
    }
    else
    {
        o.p8(rp ? rp[at] : 0);      // layer
        at += 1;
    }

    if(v >= 40)
    {
        if(rp) o.pn(rp + at, 8);
        else for(int q = 0; q < 8; ++q) o.p8(0);
    }
}

// A changed screen in the v23+ format. raw/g: the original record (g NULL for none),
// loaded: what it reads as, blank: what a valid screen with no groups reads as. exact is
// cleared when an unchanged group was encoded and didn't come out as its original bytes.
void put_screen_grouped(Out &o, word v, const mapscr &c, const mapscr &loaded, const mapscr &blank,
                        const std::vector<byte> &raw, const Grouped *g, bool keep, bool *exact)
{
    o.p8(c.valid);

    if(!(c.valid & mVALID))
        return;     // nothing else of an unused screen is in the file

    const bool rawvalid = g && (g->valid & mVALID);
    bool there[G_COUNT];
    const byte *rp[G_COUNT];
    dword has = 0;

    for(int i = 0; i < G_COUNT; ++i)
    {
        rp[i] = rawvalid && g->end[i] > g->at[i] ? &raw[g->at[i]] : NULL;
        const mapscr &ref = rp[i] ? loaded : blank;
        there[i] = rp[i] ? true : !same_group(i, c, ref);
    }

    // The warp return DMap comes with either kind of warp: one of them is written for it.
    there[G_WRC] = there[G_WRC] || there[G_TWARP] || there[G_SWARP];

    if(there[G_WRC] && !there[G_TWARP] && !there[G_SWARP])
        there[G_TWARP] = true;

    if(!there[G_TWARP] && !there[G_SWARP])
        there[G_WRC] = false;

    for(int i = 0; i < G_COUNT; ++i)
        if(there[i])
            has |= g_group_bit[i];

    if(rawvalid)
        has |= g->has & ~(dword)0xEFFF;     // bits the reader doesn't know: as they were

    o.p32(has);

    // A group or FFC nobody changed is copied, except when the round-trip check has every
    // record encoded: then it goes through the encoder too, which must give the same bytes.
    for(int i = 0; i < G_COUNT; ++i)
    {
        if(!there[i])
            continue;

        const bool same = rp[i] && same_group(i, c, loaded);

        if(same && (!zqw_force_serialize || i == G_SCRIPT))   // the script: nothing 2.53 holds
            o.pn(rp[i], g->end[i] - g->at[i]);
        else
        {
            size_t from = o.b.size();
            put_group(o, i, v, c, loaded, rp[i], keep);

            if(same && keep && (o.b.size() - from != g->end[i] - g->at[i] || memcmp(&o.b[from], rp[i], o.b.size() - from)))
            {
                al_trace("zqw MAP : group %d of an unchanged screen encodes differently\n", i);
                *exact = false;
            }
        }
    }

    // FFCs
    if(v < 25)
    {
        o.p32(c.numff);

        for(int k = 0; k < MAXFFCS; ++k)
        {
            if(!((c.numff >> k) & 1))
                continue;

            const byte *fp = rawvalid && ((g->mask >> k) & 1) ? &raw[g->ffc_at[k]] : NULL;
            const bool same = fp && same_ffc(c, loaded, k);

            if(same && !zqw_force_serialize)
                o.pn(fp, g->ffc_end[k] - g->ffc_at[k]);
            else
            {
                size_t from = o.b.size();
                put_ffc(o, v, c, loaded, k, fp, keep);

                if(same && keep && (o.b.size() - from != g->ffc_end[k] - g->ffc_at[k] || memcmp(&o.b[from], fp, o.b.size() - from)))
                {
                    al_trace("zqw MAP : FFC %d of an unchanged screen encodes differently\n", k);
                    *exact = false;
                }
            }
        }
    }
    else
    {
        int slots = rawvalid ? (int)g->ffc_at.size() : 0;

        for(int k = MAXFFCS - 1; k >= slots; --k)
            if((c.numff >> k) & 1)
            {
                slots = k + 1;
                break;
            }

        o.p16((word)slots);

        for(int k = 0; k < slots; ++k)
        {
            const bool in_raw = rawvalid && (size_t)k < g->ffc_at.size();
            const byte *fp = in_raw ? &raw[g->ffc_at[k]] : NULL;

            const bool same = fp && ((loaded.numff >> k) & 1) && same_ffc(c, loaded, k);

            if(k >= MAXFFCS)
                o.pn(fp, g->ffc_end[k] - g->ffc_at[k]);
            else if(!((c.numff >> k) & 1))
                o.p16(0);   // an empty slot
            else if(same && !zqw_force_serialize)
                o.pn(fp, g->ffc_end[k] - g->ffc_at[k]);
            else
            {
                size_t from = o.b.size();
                put_ffc(o, v, c, loaded, k, fp && ((loaded.numff >> k) & 1) ? fp : NULL, keep);

                if(same && keep && (o.b.size() - from != g->ffc_end[k] - g->ffc_at[k] || memcmp(&o.b[from], fp, o.b.size() - from)))
                {
                    al_trace("zqw MAP : FFC %d of an unchanged screen encodes differently\n", k);
                    *exact = false;
                }
            }
        }
    }

    if(v > 29)
    {
        if(rawvalid)
            o.copy(raw, g->notes_at, g->notes_end);
        else
            o.p32(0);
    }

    if(v >= 35 && (c.flags10 & 0x03))
    {
        if(rawvalid && g->grav_end > g->grav_at)
            o.copy(raw, g->grav_at, g->grav_end);
        else
            for(int k = 0; k < 8; ++k) o.p8(0);
    }
}

// ---- MAP: the screen editor's fields (zq_record_editor.cpp) ---------------------------------

// ZQW_TAPE_CHECK=1 (for testing the record editor's schemas, zq_tape.cpp): a record read as a
// tape must end where the writer's walk says and encode back to its own bytes.
bool tape_check_on()
{
    static int env = -1;

    if(env < 0)
        env = getenv("ZQW_TAPE_CHECK") ? 1 : 0;

    return env == 1;
}

int g_tape_bad = 0, g_tape_records = 0;

void tape_check(const char *what, int index, word v, const std::vector<byte> &bytes, size_t at, size_t end,
                bool (*schema)(TapeParser &, int))
{
    Tape t;
    TapeParser p(bytes.empty() ? NULL : &bytes[0], end, at, &t);
    bool ok = schema(p, v) && p.pos == end;
    std::vector<byte> enc;

    if(ok)
    {
        t.encode(enc);
        ok = enc.size() == end - at && (enc.empty() || !memcmp(&enc[0], &bytes[at], enc.size()));
    }

    ++g_tape_records;

    if(!ok && g_tape_bad++ < 5)
        al_trace("zqw tape %s v%d: record %d doesn't read and encode back (%d of %d bytes)\n", what, v, index,
                 (int)(p.pos - at), (int)(end - at));
}

void tape_check_report(const char *what, word v)
{
    if(tape_check_on())
        al_trace("zqw tape %s v%d: %d records, %d bad\n", what, v, g_tape_records, g_tape_bad);

    g_tape_bad = g_tape_records = 0;
}

// Sets a field from an edited one of the same key and kind.
void take_field(TapeField &to, const TapeField &from)
{
    if(to.kind != from.kind)
        return;

    to.value = from.value;
    to.bytes = from.bytes;
}

// A screen's record with its edited 2.55/3.0-only fields put in: the record read as a tape,
// the fields set by key, the groups they need switched on, and the record laid out again.
// False when the record can't be read.
bool screen_raw(size_t index, std::vector<byte> &rec, word &v);     // (below)

bool apply_screen_fields(size_t index, word v, std::vector<byte> &rec)
{
    std::map<int, std::map<std::string, TapeField> >::const_iterator it = zq255.screen_fields.find((int)index);

    if(it == zq255.screen_fields.end() || it->second.empty())
        return true;

    Tape t;
    TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);

    if(!zq_tape_screen(p, v) || p.pos != rec.size())
        return false;

    TapeField *has = t.find("has");

    if(v >= 23 && !has)
        return true;    // an unused screen: nothing to put in

    if(has)
    {
        // the groups the edited fields belong to: from a walk with every group on
        Tape all, src = t;
        src.find("has")->value = 0xEFFF;
        TapeParser q(&src, &all);
        zq_tape_screen(q, v);

        for(std::map<std::string, TapeField>::const_iterator f = it->second.begin(); f != it->second.end(); ++f)
            if(!t.find(f->first.c_str()))
                if(const TapeField *g = all.find(f->first.c_str()))
                    has->value |= g->gate;
    }

    for(std::map<std::string, TapeField>::const_iterator f = it->second.begin(); f != it->second.end(); ++f)
        if(TapeField *x = t.find(f->first.c_str()))
            take_field(*x, f->second);

    Tape out;
    TapeParser r(&t, &out);
    zq_tape_screen(r, v);

    for(std::map<std::string, TapeField>::const_iterator f = it->second.begin(); f != it->second.end(); ++f)
        if(TapeField *x = out.find(f->first.c_str()))
            take_field(*x, f->second);

    // A Lens show/hide edit: upstream leaves the screen's old lens byte as it was loaded. With
    // any layer shown or hidden no reader looks at it, so it keeps its loaded value here too
    // (zq255_screen_fields_sync put the layer the new show/hide mean into TheMaps).
    if(it->second.count("lens_show") || it->second.count("lens_hide"))
    {
        TapeField *ll = out.find("lens_layer"), *sh = out.find("lens_show"), *hd = out.find("lens_hide");
        std::vector<byte> orig;
        word ov = 0;

        if(ll && sh && hd && (sh->value | hd->value) && screen_raw(index, orig, ov) && ov == v)
        {
            Tape ot;
            TapeParser op(orig.empty() ? NULL : &orig[0], orig.size(), 0, &ot);

            if(zq_tape_screen(op, v) && op.pos == orig.size())
                if(const TapeField *ol = ot.find("lens_layer"))
                    ll->value = ol->value;
        }
    }

    rec.clear();
    out.encode(rec);
    return true;
}

bool has_screen_fields(size_t index)
{
    std::map<int, std::map<std::string, TapeField> >::const_iterator it = zq255.screen_fields.find((int)index);
    return it != zq255.screen_fields.end() && !it->second.empty();
}

// A screen a .map import put in (zq255.screen_records): its record, in the section's version,
// stands for the loaded one. NULL for a screen that keeps its loaded record.
const std::vector<byte> *imported_screen(size_t index)
{
    std::map<int, std::vector<byte> >::const_iterator it = zq255.screen_records.find((int)index);
    return it == zq255.screen_records.end() ? NULL : &it->second;
}

// What a map's own fields are in the v23+ format (before its screens).
struct MapHead
{
    byte valid;
    word autolayers[6];
    word autopalette;
};

// What a valid screen with no groups reads as (v23+).
bool blank_screen(word v, mapscr &blank)
{
    Out s;
    s.p8(mVALID);
    s.p32(0);

    if(v < 25) s.p32(0); else s.p16(0);
    if(v > 29) s.p32(0);

    word nolayers[6] = { 0, 0, 0, 0, 0, 0 };
    return v < 23 || read_screen_255(s.b, 0, v, 0, nolayers, 0, blank);
}

// How a screen comes out of write_maps_255 (screen_out).
enum { SO_NEW, SO_COPY, SO_CANT, SO_BAD };

// Screen `index` (screen j of its map) as write_maps_255 lays it out: ZQuest's screen (TheMaps)
// put on its record src[from, to), which reads as `loaded` (used false: there is none, loaded is
// a cleared screen), the screen editor's fields in. layers, palette: what the map's unused
// screens read with. SO_NEW: out holds the record; SO_COPY: the record as it is; SO_CANT: it
// can't be held (kept_as_is: the screen is unchanged); SO_BAD: the record doesn't read.
int screen_out(size_t index, int j, word v, const mapscr &loaded, bool used, const std::vector<byte> &src,
               size_t from, size_t to, const word *layers, word palette, const mapscr &blank,
               std::vector<byte> &out, bool &kept_as_is)
{
    const mapscr &c = TheMaps[index];
    const bool unchanged = used && screen_unchanged(index);
    kept_as_is = unchanged && !has_screen_fields(index);

    if(unchanged && !zqw_force_serialize && !has_screen_fields(index))
        return SO_COPY;

    // An unused screen is its valid byte alone (v23+), as upstream writes one: nothing else of
    // it is in the file, so there is nothing to read back.
    if(v >= 23 && !(c.valid & mVALID) && !has_screen_fields(index))
    {
        out.assign(1, c.valid);
        return SO_NEW;
    }

    const std::vector<byte> rec_raw(src.begin() + from, src.begin() + to);
    mapscr got, raw253, merged;
    Grouped g;
    bool ok = false, encoder_ok = true;
    Out rec;

    for(int pass = 0; pass < 2 && !ok && encoder_ok; ++pass)
    {
        const bool keep = pass == 0;
        rec.b.clear();

        if(v < 23)
        {
            if(used)
            {
                In r(rec_raw, 0);
                parse_screen_253(r, raw253);
                merge_flat(merged, c, loaded, raw253, keep);
                screen_bytes_253(rec, merged);
                rec.copy(rec_raw, r.pos, rec_raw.size());   // what follows: v19-22's fields 2.53 has no room for
            }
            else
            {
                merge_flat(merged, c, loaded, loaded, false);
                screen_bytes_253(rec, merged);

                if(v >= 19) for(int q = 0; q < 100; ++q) rec.p8(0);
                if(v >= 20) for(int q = 0; q < 34; ++q) rec.p8(0);
                if(v >= 21) rec.p8(0);
                if(v >= 22) { rec.p8(0); rec.p8(0); }
            }
        }
        else
        {
            const Grouped *gp = NULL;

            if(used)
            {
                if(!parse_grouped(rec_raw, 0, v, g))
                    return SO_BAD;

                gp = &g;
            }

            bool exact = true;
            put_screen_grouped(rec, v, c, loaded, blank, rec_raw, gp, keep, &exact);

            if(!exact && strict())
            {
                encoder_ok = false;     // a bug in the encoder: fail the check
                break;
            }
        }

        // the 2.55/3.0-only fields edited in the screen editor
        if(!apply_screen_fields(index, v, rec.b))
        {
            encoder_ok = false;
            break;
        }

        // a map written anew has no layers or palette of its own for unused screens
        got.screen_midi = c.screen_midi;
        size_t back = read_screen_255(rec.b, 0, v, j, layers, palette, got);
        ok = back == rec.b.size() && same_screen(c, got);
    }

    if(!ok || !encoder_ok)
        return SO_CANT;

    out.swap(rec.b);
    return SO_NEW;
}

// Maps that Quest > Map Count added (zq255_maps_counted): the file's head for one, if it has
// one, is not theirs. Map -> the auto palette it starts with (3.0's setMapCount2 gives a new
// map the current one's).
std::map<int, word> g_new_maps;

// Whether every screen of map i is unused in ZQuest now.
bool map_unused(int i)
{
    for(int j = 0; j < MAPSCRS; ++j)
    {
        const size_t index = (size_t)i * MAPSCRS + j;

        if(index >= TheMaps.size() || (TheMaps[index].valid & mVALID))
            return false;
    }

    return true;
}

int write_maps_255(PACKFILE *f, const ZqPartContext &ctx)
{
    const word v = ctx.version;

    if(ctx.zelda_version != 0x255 || v < 18 || v > 41)
        return ZQW_UNSUPPORTED;

    std::vector<byte> orig;

    if(!part_bytes(*ctx.part, orig) || orig.size() < 10)
        return ERR_READ;

    const std::vector<byte> body(orig.begin() + 8, orig.end());
    In in(body, 0);
    const int old_maps = in.g16();
    const int stored = old_maps < MAXMAPS2 ? old_maps : MAXMAPS2;
    const int maps = map_count < MAXMAPS2 ? map_count : MAXMAPS2;

    if(maps < stored && old_maps > MAXMAPS2)
    {
        al_trace("zqw MAP : maps past the %d this ZQuest holds would move\n", MAXMAPS2);
        return ERR_CANT_HOLD;
    }

    const int count = maps == stored ? old_maps : maps;
    Out o;
    o.p16((word)count);

    // what a valid screen with no groups reads as
    mapscr blank;

    if(!blank_screen(v, blank))
        return ERR_READ;

    mapscr loaded, got;
    const word no_layers[6] = { 0, 0, 0, 0, 0, 0 };

    for(int i = 0; i < (count > old_maps ? count : old_maps); ++i)
    {
        MapHead mh;
        memset(&mh, 0, sizeof(mh));
        mh.valid = 1;
        const bool map_in_raw = i < old_maps;
        size_t head_at = in.pos;

        if(map_in_raw)
        {
            if(v > 22)
                mh.valid = in.g8();

            if(mh.valid)
            {
                if(v > 25)
                {
                    for(int q = 0; q < 6; ++q) mh.autolayers[q] = in.g16();
                    if(v >= 36) mh.autopalette = in.g16();
                }

                if(v >= 31)
                    in.skip(64);
            }

            if(in.bad)
                return ERR_READ;
        }

        const size_t head_end = in.pos;

        // past what the loader keeps (copied), or deleted (dropped)
        if(i >= MAXMAPS2 || i >= count)
        {
            size_t at = in.pos;

            for(int j = 0; mh.valid && j < MAPSCRS; ++j)
            {
                size_t used = read_screen_255(body, at, v, j, mh.autolayers, mh.autopalette, got);

                if(!used)
                    return ERR_READ;

                at += used;
            }

            if(i < count)
                o.copy(body, head_at, at);

            in.pos = at;
            continue;
        }

        // A map Quest > Map Count added: where the file has one (maps were taken away and given
        // back), that one isn't it. Its screens are passed over and the map is written anew,
        // with no auto layers and the palette it was given.
        std::map<int, word>::const_iterator nm = g_new_maps.find(i);
        const bool fresh = nm != g_new_maps.end();
        bool raw_map = map_in_raw;

        if(fresh)
        {
            for(int j = 0; map_in_raw && mh.valid && j < MAPSCRS; ++j)
            {
                size_t used = read_screen_255(body, in.pos, v, j, mh.autolayers, mh.autopalette, got);

                if(!used)
                    return ERR_READ;

                in.pos += used;
            }

            memset(&mh, 0, sizeof(mh));
            mh.valid = 1;
            mh.autopalette = v >= 36 ? nm->second : 0;
            raw_map = false;
        }

        // A map the file holds without screens loads cleared; if a screen of it changed,
        // it is written whole.
        bool any_changed = false;
        const bool unused_now = v > 22 && map_unused(i);

        if(!mh.valid || unused_now)
            for(int j = 0; j < MAPSCRS && !any_changed; ++j)
                any_changed = !screen_unchanged((size_t)i * MAPSCRS + j) || imported_screen((size_t)i * MAPSCRS + j);

        // A map with no screen in use now (cleared, or added) is its valid byte alone, as
        // upstream writes one; a map as the file holds it stays as it is.
        if(unused_now && (!raw_map || any_changed))
        {
            for(int j = 0; raw_map && mh.valid && j < MAPSCRS; ++j)
            {
                size_t used = read_screen_255(body, in.pos, v, j, mh.autolayers, mh.autopalette, got);

                if(!used)
                    return ERR_READ;

                in.pos += used;
            }

            o.p8(0);
            continue;
        }

        // the map's scrolling region ids as the region editor left them (zq_cpool.cpp); the
        // reader keeps every valid map's, so an unedited map's come out as they were
        const bool region_kept = v >= 31 && (size_t)i < q255_map_region_ids.size() && q255_map_region_ids[i].size() == 64;

        if(raw_map && (mh.valid || !any_changed))
        {
            o.copy(body, head_at, head_end);

            if(region_kept && mh.valid)
                memcpy(&o.b[o.b.size() - 64], &q255_map_region_ids[i][0], 64);
        }
        else
        {
            if(v > 22) o.p8(1);
            if(v > 25)
            {
                for(int q = 0; q < 6; ++q) o.p16(0);
                if(v >= 36) o.p16(fresh ? mh.autopalette : 0);
            }
            if(v >= 31) for(int q = 0; q < 64; ++q) o.p8(region_kept ? q255_map_region_ids[i][q] : 0);
        }

        if(raw_map && !mh.valid && !any_changed)
            continue;

        const bool screens_in_raw = raw_map && mh.valid;
        // what the map's unused screens read with: its head's, none for a map written anew
        const word *layers = screens_in_raw ? mh.autolayers : no_layers;
        const word palette = screens_in_raw || fresh ? mh.autopalette : 0;

        for(int j = 0; j < MAPSCRS; ++j)
        {
            const size_t index = (size_t)i * MAPSCRS + j;
            size_t start = in.pos, used = 0;

            // clear_screen leaves the screen's MIDI as it was, and the reader doesn't set it for
            // an unused screen: there it stays what ZQuest has (the loader did the same)
            if(index < TheMaps.size())
                loaded.screen_midi = got.screen_midi = TheMaps[index].screen_midi;

            if(screens_in_raw)
            {
                used = read_screen_255(body, in.pos, v, j, mh.autolayers, mh.autopalette, loaded);

                if(!used)
                    return ERR_READ;

                if(tape_check_on())
                    tape_check("MAP", (int)index, v, body, in.pos, in.pos + used, zq_tape_screen);

                in.pos += used;
            }
            else
                clear_screen(&loaded);

            if(index >= TheMaps.size())
                return ERR_READ;

            // A screen a .map import or a map or screen operation put in: its record stands for
            // the loaded one, read as the loader would read it here.
            const std::vector<byte> *imp = imported_screen(index);

            if(imp)
            {
                used = read_screen_255(*imp, 0, v, j, layers, palette, loaded);

                if(used != imp->size())
                    return ERR_READ;
            }

            const std::vector<byte> &src = imp ? *imp : body;
            const size_t from = imp ? 0 : start, to = imp ? imp->size() : in.pos;
            std::vector<byte> rec;
            bool kept_as_is = false;    // a screen with fields edited in the screen editor counts as changed
            const int r = screen_out(index, j, v, loaded, used != 0, src, from, to, layers, palette, blank, rec, kept_as_is);

            if(r == SO_BAD)
                return ERR_READ;

            if(r == SO_COPY)
                o.copy(src, from, to);
            else if(r == SO_NEW && !exact_differs(ID_MAPS, (int)index, kept_as_is, rec, src, from, to))
                o.pn(&rec[0], rec.size());
            else if(refuse(ID_MAPS, (int)index, kept_as_is, "can't be held in this version"))
                return ERR_CANT_HOLD;
            else
                o.copy(src, from, to);
        }
    }

    o.copy(body, in.pos, body.size());
    tape_check_report("MAP", v);
    return put_part(f, ctx.part->id, orig, o);
}

ZqWriterRegistration g_reg_maps_255(ID_MAPS, write_maps_255, "maps, 2.55/3.0 versions 18-41");

// ---- DMAP: 2.55 and 3.0 (q255_read_dmaps, versions 9-28) ----------------------------------

bool read_dmap_255(const std::vector<byte> &bytes, size_t at, word v, size_t *end, dmap &d)
{
    Q255Section s;
    s.data = bytes.empty() ? NULL : &bytes[0];
    s.size = bytes.size();
    s.pos = at;
    s.version = v;
    s.keep = false;
    int ret = q255_editor_read_dmap(s, &d);
    *end = s.pos;
    return ret == 0 && !s.failed;
}

const long DMF_GRAVITY_255 = 0x800000;     // upstream: the DMap has fields for its own gravity
const long DMF_VIEWPORT_255 = 0x1000000;   // and for its viewport

// A changed DMap: its original record with ZQuest's values put in, keeping the original
// bytes of a field that still reads as ZQuest's (keep). False when the record can't be
// laid out (the flags that say which fields follow changed).
bool put_dmap_255(Out &o, word v, const dmap &c, const dmap &l, const std::vector<byte> &raw, bool keep)
{
    In in(raw, 0);
    const bool have = !raw.empty();
    const size_t tb = v >= 11 ? 4 : 2;
    const byte *rp = have ? &raw[0] : NULL;

    o.p8(c.map);
    o.p16(c.level);
    o.p8(have && keep && l.xoff == c.xoff ? rp[3] : (byte)c.xoff);
    o.p8(c.compass);
    o.p16(c.color);
    size_t at = 7;

    if(v < 25)
    {
        o.p8(c.midi);
        ++at;
    }

    o.p8(c.cont);
    o.p8(c.type);
    o.pn(c.grid, 8);
    o.pn(c.name, 21);
    at += 2 + 8 + 21;

    if(v < 20)
    {
        o.pn(c.title, 21);
        at += 21;
    }
    else
    {
        size_t tl = have ? get16(rp + at) : 0;

        if(have && keep && !memcmp(l.title, c.title, sizeof(c.title)))
            o.pn(rp + at, 2 + tl);
        else
        {
            // the reader takes up to 20 characters into a space-filled title
            size_t n = 0;

            while(n < 20 && c.title[n])
                ++n;

            while(n > 0 && c.title[n - 1] == ' ')
                --n;

            o.p16((word)n);
            o.pn(c.title, n);
        }

        at += 2 + tl;
    }

    o.pn(c.intro, 73);
    at += 73;
    const tile_id tiles[4] = { c.minimap_1_tile, c.minimap_2_tile, c.largemap_1_tile, c.largemap_2_tile };
    const tile_id ltiles[4] = { l.minimap_1_tile, l.minimap_2_tile, l.largemap_1_tile, l.largemap_2_tile };
    const byte csets[4] = { c.minimap_1_cset, c.minimap_2_cset, c.largemap_1_cset, c.largemap_2_cset };

    for(int k = 0; k < 4; ++k)
    {
        if(have && keep && ltiles[k] == tiles[k])
            o.pn(rp + at, tb);
        else if(tb == 4)
            o.p32((dword)tiles[k]);
        else
            o.p16((word)tiles[k]);

        o.p8(csets[k]);
        at += tb + 1;
    }

    if(v < 25)
    {
        o.pn(c.tmusic, 56);
        o.p8(c.tmusictrack);
        at += 57;
    }

    o.p8(c.active_subscreen);
    o.p8(c.passive_subscreen);
    at += 2;

    if(v >= 26)
    {
        // a bitstring: past item 255 it holds what 2.53 has no room for
        size_t bs_at = at;
        In b(raw, at);
        std::vector<byte> bits;
        word size = 0;

        if(have)
        {
            size = b.g16();

            if(size)
            {
                byte pairs = b.g8();
                word n = b.g16();

                for(word q = 0; q < n && !b.bad; ++q)
                {
                    word idx = pairs ? b.g16() : q;
                    byte val = b.g8();

                    if(idx >= bits.size())
                        bits.resize((size_t)idx + 1, 0);

                    bits[idx] = val;
                }
            }

            if(b.bad)
                return false;
        }

        at = b.pos;

        if(have && keep && !memcmp(l.disableditems, c.disableditems, sizeof(c.disableditems)))
            o.copy(raw, bs_at, at);
        else
        {
            if(bits.size() < 32)
                bits.resize(32, 0);

            for(int j = 0; j < 32; ++j)
                bits[j] = 0;

            for(int j = 0; j < MAXITEMS; ++j)
                if(c.disableditems[j])
                    bits[j / 8] |= (byte)(1 << (j % 8));

            word n = 0;

            for(size_t q = 0; q < bits.size(); ++q)
                if(bits[q])
                    ++n;

            if(!n)
                o.p16(0);
            else
            {
                o.p16(size ? size : (word)bits.size());
                o.p8(1);    // index and value pairs
                o.p16(n);

                for(size_t q = 0; q < bits.size(); ++q)
                    if(bits[q])
                    {
                        o.p16((word)q);
                        o.p8(bits[q]);
                    }
            }
        }
    }
    else
    {
        byte di[32];
        memset(di, 0, sizeof(di));

        for(int j = 0; j < MAXITEMS; ++j)
            if(c.disableditems[j])
                di[j / 8] |= (byte)(1 << (j % 8));

        o.pn(di, 32);
        at += 32;
    }

    // the flags say whether the gravity and viewport fields follow
    const long old_flags = have ? (long)get32(rp + at) : 0;
    const long tail_bits = (v > 22 ? DMF_GRAVITY_255 : 0) | (v >= 28 ? DMF_VIEWPORT_255 : 0);

    if((old_flags ^ c.flags) & tail_bits)
        return false;

    o.p32((dword)c.flags);
    at += 4;

    if(have)
        o.copy(raw, at, raw.size());
    else
    {
        // a new DMap: upstream's defaults for what 2.53 has no field for
        if(v >= 10) o.p8(0);                        // sideview

        if(v >= 27)
            for(int q = 0; q < 4; ++q) { o.p16(0); o.p16(0); o.p16(0); }
        else
        {
            if(v >= 12) { o.p16(0); for(int q = 0; q < 32; ++q) o.p8(0); }
            if(v >= 13) for(int q = 0; q < 8 * 65; ++q) o.p8(0);
            if(v >= 14) { o.p16(0); o.p16(0); for(int q = 0; q < 32 + 8 * 65; ++q) o.p8(0); }
            if(v >= 15) { o.p16(0); for(int q = 0; q < 32 + 8 * 65; ++q) o.p8(0); }
        }

        if(v >= 16) o.p16(0);                       // mirror DMap
        if(v >= 18 && v < 25) for(int q = 0; q < 16; ++q) o.p8(0);
        if(v >= 19) o.p8(0);                        // overlay subscreen
        if(v >= 20) o.p32(0);                       // intro string
        if(v == 21) for(int q = 0; q < 64; ++q) o.p8(0);
        if(v > 23) { o.p16((word)-1); o.p8(0); }    // no map subscreen, floor
        if(v >= 25) o.p16(0);                       // music
    }

    return true;
}

int write_dmaps_255(PACKFILE *f, const ZqPartContext &ctx)
{
    const word v = ctx.version;

    if(ctx.zelda_version != 0x255 || v < 9 || v > 28)
        return ZQW_UNSUPPORTED;

    std::vector<byte> orig;

    if(!part_bytes(*ctx.part, orig) || orig.size() < 10)
        return ERR_READ;

    const std::vector<byte> body(orig.begin() + 8, orig.end());
    In in(body, 0);
    const int old_count = in.g16();
    const int count = dmap_count(old_count < MAXDMAPS ? old_count : MAXDMAPS);
    int total = old_count > count ? old_count : count;

    // an imported DMap past the file's last one (zq255_dmap_commit) goes in as its record
    if(!zq255.dmap_records.empty() && zq255.dmap_records.rbegin()->first >= total)
        total = zq255.dmap_records.rbegin()->first + 1;

    Out o;
    o.p16((word)total);
    size_t pos = 2;
    dmap loaded, got;

    for(int i = 0; i < total; ++i)
    {
        size_t start = pos, end = pos;
        const bool have_raw = i < old_count;

        if(have_raw)
        {
            if(!read_dmap_255(body, pos, v, &end, loaded))
                return ERR_READ;

            pos = end;
        }

        if(have_raw && tape_check_on())
            tape_check("DMAP", i, v, body, start, end, zq_tape_dmap);

        if(i >= MAXDMAPS)
        {
            o.copy(body, start, end);
            continue;
        }

        // a DMap edited in the DMap editor for newer quests: its record as edited
        std::map<int, std::vector<byte> >::const_iterator edited = zq255.dmap_records.find(i);

        if(edited != zq255.dmap_records.end())
        {
            size_t back = 0;

            if(!edited->second.empty() && read_dmap_255(edited->second, 0, v, &back, got) && back == edited->second.size() &&
               same_dmap(DMaps[i], got, false))
                o.pn(&edited->second[0], edited->second.size());
            else if(refuse(ID_DMAPS, i, false, "doesn't read back as edited"))
                return ERR_CANT_HOLD;

            continue;
        }

        const bool unchanged = have_raw && dmap_unchanged(i);

        if(unchanged && !zqw_force_serialize)
        {
            o.copy(body, start, end);
            continue;
        }

        if(!have_raw)
            blank_dmap_253(loaded);

        const std::vector<byte> raw(body.begin() + start, body.begin() + end);
        bool ok = false;
        Out rec;

        for(int pass = 0; pass < 2 && !ok; ++pass)
        {
            rec.b.clear();

            if(!put_dmap_255(rec, v, DMaps[i], loaded, raw, pass == 0))
                break;

            size_t back = 0;
            ok = read_dmap_255(rec.b, 0, v, &back, got) && back == rec.b.size() && same_dmap(DMaps[i], got, false);
        }

        if(ok && !exact_differs(ID_DMAPS, i, unchanged, rec.b, body, start, end))
            o.pn(&rec.b[0], rec.b.size());
        else if(refuse(ID_DMAPS, i, unchanged, "can't be held in this version"))
            return ERR_CANT_HOLD;
        else
            o.copy(body, start, end);
    }

    o.copy(body, pos, body.size());
    tape_check_report("DMAP", v);
    return put_part(f, ctx.part->id, orig, o);
}

ZqWriterRegistration g_reg_dmaps_255(ID_DMAPS, write_dmaps_255, "DMaps, 2.55/3.0 versions 9-28");

// Edited DMaps and screen fields count in their sections' fingerprints (nothing for a quest
// with none).
void hash_dmap_records(uint64_t *h)
{
    for(std::map<int, std::vector<byte> >::const_iterator it = zq255.dmap_records.begin(); it != zq255.dmap_records.end(); ++it)
    {
        const int i = it->first;
        const unsigned n = (unsigned)it->second.size();
        zq_hash_add(h, &i, sizeof(i));
        zq_hash_add(h, &n, sizeof(n));
        zq_hash_add(h, it->second.empty() ? NULL : &it->second[0], it->second.size());
    }
}

void hash_screen_fields(uint64_t *h)
{
    for(std::map<int, std::map<std::string, TapeField> >::const_iterator it = zq255.screen_fields.begin();
        it != zq255.screen_fields.end(); ++it)
        for(std::map<std::string, TapeField>::const_iterator f = it->second.begin(); f != it->second.end(); ++f)
        {
            zq_hash_add(h, &it->first, sizeof(it->first));
            zq_hash_add(h, f->first.data(), f->first.size());
            zq_hash_add(h, &f->second.value, sizeof(f->second.value));
            zq_hash_add(h, f->second.bytes.empty() ? NULL : &f->second.bytes[0], f->second.bytes.size());
        }

    for(std::map<int, word>::const_iterator it = g_new_maps.begin(); it != g_new_maps.end(); ++it)
    {
        zq_hash_add(h, &it->first, sizeof(it->first));
        zq_hash_add(h, &it->second, sizeof(it->second));
    }

    // and the screens a .map import or a map or screen operation put in
    for(std::map<int, std::vector<byte> >::const_iterator it = zq255.screen_records.begin(); it != zq255.screen_records.end(); ++it)
    {
        const unsigned n = (unsigned)it->second.size();
        zq_hash_add(h, &it->first, sizeof(it->first));
        zq_hash_add(h, &n, sizeof(n));
        zq_hash_add(h, it->second.empty() ? NULL : &it->second[0], it->second.size());
    }
}

ZqStoreHashRegistration g_hash_dmaps(ID_DMAPS, hash_dmap_records);
ZqStoreHashRegistration g_hash_screens(ID_MAPS, hash_screen_fields);

// The open quest's DMap part and each record's place in its body (and the body's end).
bool dmap_layout(std::vector<byte> &orig, std::vector<byte> &body, word &v, std::vector<size_t> &rec_at)
{
    const ZqQuestPart *part = find_part(ID_DMAPS);

    if(!part || header.zelda_version != 0x255 || !part_bytes(*part, orig) || orig.size() < 10)
        return false;

    v = (word)(orig[0] | (orig[1] << 8));

    if(v < 9 || v > 28)
        return false;

    body.assign(orig.begin() + 8, orig.end());
    const int count = body[0] | (body[1] << 8);
    size_t pos = 2;
    dmap d;
    rec_at.clear();

    for(int i = 0; i < count; ++i)
    {
        size_t end = 0;
        rec_at.push_back(pos);

        if(!read_dmap_255(body, pos, v, &end, d))
            return false;

        pos = end;
    }

    rec_at.push_back(pos);
    return true;
}

// The open quest's MAP part, walked to a screen: its record (as loaded) and the version.
bool screen_raw(size_t index, std::vector<byte> &rec, word &v)
{
    const ZqQuestPart *part = find_part(ID_MAPS);
    std::vector<byte> orig;

    if(!part || header.zelda_version != 0x255 || !part_bytes(*part, orig) || orig.size() < 10)
        return false;

    v = (word)(orig[0] | (orig[1] << 8));

    if(v < 18 || v > 41)
        return false;

    if(const std::vector<byte> *imp = imported_screen(index))
    {
        rec = *imp;
        return true;
    }

    const std::vector<byte> body(orig.begin() + 8, orig.end());
    In in(body, 0);
    const int maps = in.g16();
    mapscr scratch;

    for(int i = 0; i < maps; ++i)
    {
        byte valid = 1;
        word autolayers[6] = { 0, 0, 0, 0, 0, 0 };
        word autopalette = 0;

        if(v > 22)
            valid = in.g8();

        if(valid)
        {
            if(v > 25)
            {
                for(int q = 0; q < 6; ++q) autolayers[q] = in.g16();
                if(v >= 36) autopalette = in.g16();
            }

            if(v >= 31)
                in.skip(64);
        }

        if(in.bad)
            return false;

        if(!valid)
        {
            if((size_t)i == index / MAPSCRS)
                return false;   // the file has no screens for this map

            continue;
        }

        for(int j = 0; j < MAPSCRS; ++j)
        {
            const size_t used = read_screen_255(body, in.pos, v, j, autolayers, autopalette, scratch);

            if(!used)
                return false;

            if((size_t)i * MAPSCRS + j == index)
            {
                rec.assign(body.begin() + in.pos, body.begin() + in.pos + used);
                return true;
            }

            in.pos += used;
        }
    }

    return false;
}

} // namespace

// ---- the record editor's access (zq_record_editor.cpp) ---------------------------------

// DMap i's record as it stands (edited, or as loaded) and the section version.
// Every DMap record zq255_dmap_record gives, read in one pass (zq_compile.cpp's walk).
bool zq255_dmap_records_all(std::vector<std::vector<byte> > &recs, int *version)
{
    std::vector<byte> orig, body;
    std::vector<size_t> rec_at;
    word v = 0;
    recs.clear();

    if(!dmap_layout(orig, body, v, rec_at))
        return false;

    if(version)
        *version = v;

    for(int i = 0; i < MAXDMAPS && i + 1 < (int)rec_at.size(); ++i)
    {
        std::map<int, std::vector<byte> >::const_iterator it = zq255.dmap_records.find(i);
        recs.push_back(it != zq255.dmap_records.end() ? it->second :
                       std::vector<byte>(body.begin() + rec_at[i], body.begin() + rec_at[i + 1]));
    }

    return true;
}

// Every screen record the file has (as zq255_screen_record gives them), read in one pass.
// indices: each one's screen index (map * MAPSCRS + screen), as zq255_screen_record takes it.
bool zq255_screen_records_all(std::vector<std::vector<byte> > &recs, int *version, std::vector<int> *indices)
{
    recs.clear();

    if(indices)
        indices->clear();

    const ZqQuestPart *part = find_part(ID_MAPS);
    std::vector<byte> orig;

    if(!part || header.zelda_version != 0x255 || !part_bytes(*part, orig) || orig.size() < 10)
        return false;

    const word v = (word)(orig[0] | (orig[1] << 8));

    if(v < 18 || v > 41)
        return false;

    if(version)
        *version = v;

    const std::vector<byte> body(orig.begin() + 8, orig.end());
    In in(body, 0);
    const int maps = in.g16();
    mapscr scratch;
    std::map<int, std::vector<byte> >::const_iterator imp = zq255.screen_records.begin();

    // the screens a .map import put in, in their places (in a map the file has no screens for too)
    auto imported_before = [&](int at)
    {
        for(; imp != zq255.screen_records.end() && imp->first < at; ++imp)
        {
            if((size_t)imp->first >= TheMaps.size())
                continue;   // on a map deleted since

            recs.push_back(imp->second);

            if(indices)
                indices->push_back(imp->first);
        }
    };

    for(int i = 0; i < maps; ++i)
    {
        byte valid = 1;
        word autolayers[6] = { 0, 0, 0, 0, 0, 0 };
        word autopalette = 0;

        if(v > 22)
            valid = in.g8();

        if(valid)
        {
            if(v > 25)
            {
                for(int q = 0; q < 6; ++q) autolayers[q] = in.g16();
                if(v >= 36) autopalette = in.g16();
            }

            if(v >= 31)
                in.skip(64);
        }

        if(in.bad)
            return false;

        if(!valid)
            continue;

        for(int j = 0; j < MAPSCRS; ++j)
        {
            const size_t used = read_screen_255(body, in.pos, v, j, autolayers, autopalette, scratch);

            if(!used)
                return false;

            imported_before(i * MAPSCRS + j);

            if(imp != zq255.screen_records.end() && imp->first == i * MAPSCRS + j)
                recs.push_back((imp++)->second);
            else
                recs.push_back(std::vector<byte>(body.begin() + in.pos, body.begin() + in.pos + used));

            if(indices)
                indices->push_back(i * MAPSCRS + j);

            in.pos += used;
        }
    }

    imported_before(0x7FFFFFFF);
    return true;
}

// A screen's loaded record with the fields edited since put in, as a save writes them (true and
// rec as it was when there are none; false when the record can't be laid out).
bool zq255_screen_fields_apply(int index, int version, std::vector<byte> &rec)
{
    return index >= 0 && apply_screen_fields((size_t)index, (word)version, rec);
}

bool zq255_dmap_record(int i, std::vector<byte> &rec, int *version)
{
    std::vector<byte> orig, body;
    std::vector<size_t> rec_at;
    word v = 0;

    if(i < 0 || i >= MAXDMAPS || !dmap_layout(orig, body, v, rec_at) || i + 1 >= (int)rec_at.size())
        return false;

    if(version)
        *version = v;

    std::map<int, std::vector<byte> >::const_iterator it = zq255.dmap_records.find(i);

    if(it != zq255.dmap_records.end())
        rec = it->second;
    else
        rec.assign(body.begin() + rec_at[i], body.begin() + rec_at[i + 1]);

    return true;
}

// Puts an edited DMap record in: the section (with every edited record) is read, DMap i takes
// what the reader makes of it, and the store keeps its scripts, subscreens and music.
bool zq255_dmap_commit(int i, const std::vector<byte> &rec, std::string &why)
{
    std::vector<byte> orig, body;
    std::vector<size_t> rec_at;
    word v = 0;

    if(i < 0 || i >= MAXDMAPS || !dmap_layout(orig, body, v, rec_at))
    {
        why = "this quest's DMaps can't be edited this way";
        return false;
    }

    // A DMap past the file's last one (an import): the DMaps between go in as they stand (an
    // edited record, or ZQuest's DMap encoded), as the writer will write them.
    const int in_file = (int)rec_at.size() - 1;
    const int n = i + 1 > in_file ? i + 1 : in_file;
    std::vector<byte> sec;
    sec.push_back((byte)n);
    sec.push_back((byte)(n >> 8));

    for(int k = 0; k < n; ++k)
    {
        std::map<int, std::vector<byte> >::const_iterator it = zq255.dmap_records.find(k);

        if(k == i)
            sec.insert(sec.end(), rec.begin(), rec.end());
        else if(it != zq255.dmap_records.end())
            sec.insert(sec.end(), it->second.begin(), it->second.end());
        else if(k < in_file)
            sec.insert(sec.end(), body.begin() + rec_at[k], body.begin() + rec_at[k + 1]);
        else
        {
            dmap blank;
            blank_dmap_253(blank);
            Out o;

            if(!put_dmap_255(o, v, DMaps[k], blank, std::vector<byte>(), false))
            {
                why = "a DMap before it can't be laid out in this version";
                return false;
            }

            sec.insert(sec.end(), o.b.begin(), o.b.end());
        }
    }

    sec.insert(sec.end(), body.begin() + rec_at.back(), body.end());

    // everything the reader changes, kept to be put back but for DMap i
    std::vector<dmap> dmaps(DMaps, DMaps + MAXDMAPS);
    const std::vector<Zq255Store::Music> music = zq255.music;
    const std::map<int, std::pair<int, bool> > dmap_music = zq255.dmap_music;
    std::map<int, Q30ScriptRef> scripts[q30_dmap_kinds];

    for(int k = 0; k < q30_dmap_kinds; ++k)
        scripts[k] = zq255.dmap_scripts[k];

    const std::map<int, std::pair<int, int> > subs = zq255.dmap_subscreens;

    Q255Section s;
    s.data = sec.empty() ? NULL : &sec[0];
    s.size = sec.size();
    s.version = v;
    s.cversion = (word)(orig[2] | (orig[3] << 8));
    s.keep = true;
    s.header = &header;
    const int ret = q255_read_dmaps(s);
    const bool ok = ret == 0 && !s.failed;

    const dmap got = DMaps[i];
    std::map<int, std::pair<int, bool> >::const_iterator gm = zq255.dmap_music.find(i);
    const bool have_music = gm != zq255.dmap_music.end();
    const std::pair<int, bool> got_music = have_music ? gm->second : std::make_pair(0, false);
    Zq255Store::Music got_object;

    if(have_music && !got_music.second && got_music.first >= 1 && got_music.first <= (int)zq255.music.size())
        got_object = zq255.music[got_music.first - 1];

    std::map<int, Q30ScriptRef> got_scripts[q30_dmap_kinds];

    for(int k = 0; k < q30_dmap_kinds; ++k)
        got_scripts[k] = zq255.dmap_scripts[k];

    const std::map<int, std::pair<int, int> > got_subs = zq255.dmap_subscreens;

    memcpy(DMaps, &dmaps[0], sizeof(dmap) * MAXDMAPS);
    zq255.music = music;
    zq255.dmap_music = dmap_music;

    for(int k = 0; k < q30_dmap_kinds; ++k)
        zq255.dmap_scripts[k] = scripts[k];

    zq255.dmap_subscreens = subs;

    if(!ok)
    {
        why = "the DMap reader doesn't take the record";
        return false;
    }

    DMaps[i] = got;

    for(int k = 0; k < q30_dmap_kinds; ++k)
    {
        std::map<int, Q30ScriptRef>::const_iterator x = got_scripts[k].find(i);

        if(x != got_scripts[k].end())
            zq255.dmap_scripts[k][i] = x->second;
        else
            zq255.dmap_scripts[k].erase(i);
    }

    std::map<int, std::pair<int, int> >::const_iterator gs = got_subs.find(i);

    if(gs != got_subs.end())
        zq255.dmap_subscreens[i] = gs->second;
    else
        zq255.dmap_subscreens.erase(i);

    if(!have_music)
        zq255.dmap_music.erase(i);
    else if(got_music.second)
        zq255.dmap_music[i] = got_music;
    else
    {
        // an older DMap's own music object (loop points, crossfades): a new one at the end
        zq255.music.push_back(got_object);
        zq255.dmap_music[i] = std::make_pair((int)zq255.music.size(), false);
    }

    zq255.dmap_records[i] = rec;
    return true;
}

// Screen index's record as loaded and its section version (the editor puts the edited fields
// in itself); false for a screen the file has no record for.
bool zq255_screen_record(int index, std::vector<byte> &rec, int *version)
{
    word v = 0;

    if(index < 0 || !screen_raw((size_t)index, rec, v))
        return false;

    if(version)
        *version = v;

    return true;
}

// After the screen editor's edits: what those fields make of fields 2.53 also holds (the
// screen's MIDI from its music, the lens layer from the Lens's layers) goes into TheMaps,
// so the screen reads back as ZQuest holds it. Only for the fields the edit touched.
void zq255_screen_fields_sync(int index, bool music, bool lens)
{
    std::vector<byte> rec;
    word v = 0;

    if(index < 0 || (size_t)index >= TheMaps.size() || !screen_raw((size_t)index, rec, v) ||
       !apply_screen_fields((size_t)index, v, rec))
        return;

    mapscr got;
    const word no_layers[6] = { 0, 0, 0, 0, 0, 0 };

    if(!read_screen_255(rec, 0, v, index % MAPSCRS, no_layers, 0, got))
        return;

    if(music)
        TheMaps[index].screen_midi = got.screen_midi;

    if(lens)
        TheMaps[index].lens_layer = got.lens_layer;
}

// Test runs (-qst-lens-edit-test): the 2.55/3.0 screen data editor's edit of a screen's Lens
// "shows" layers (zq_record_editor.cpp), on one screen of a v28+ quest.
bool zq255_test_lens_edit(int index, int show)
{
    std::vector<byte> rec;
    word v = 0;

    if(index < 0 || (size_t)index >= TheMaps.size() || !screen_raw((size_t)index, rec, v) || v < 28)
        return false;

    Tape t;
    TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);

    if(!zq_tape_screen(p, v))
        return false;

    const TapeField *f = t.find("lens_show");

    if(!f)
        return false;

    TapeField e = *f;
    e.value = show;
    zq255.screen_fields[index]["lens_show"] = e;
    zq255_screen_fields_sync(index, false, true);
    return true;
}

// ---- .zdmap files: export and import (zq_exports.cpp) -------------------------------------
//
// A .zdmap file holds DMaps as the DMap section does, one record after another (upstream's
// write_one_dmap, read_one_dmap), with the section's version and cversion in its header.

// DMaps first..last as a .zdmap file holds them: each one's record in the DMap section's own
// version as the quest's own save writes it now. DMaps past the section's last one are blank
// (blank when loaded, and unchanged since: the save writes any changed one) and are encoded as
// they stand in that version. version, cversion: the section's.
bool zq255_dmap_export(int first, int last, std::vector<byte> &recs, word *version, word *cversion, std::string &why)
{
    const ZqQuestPart *part = find_part(ID_DMAPS);
    std::vector<byte> all;
    recs.clear();

    if(!part || header.zelda_version != 0x255 || !zq_own_part_bytes(*part, all) || all.size() < 10)
    {
        why = "this quest's DMaps couldn't be read";
        return false;
    }

    const word v = (word)(all[0] | (all[1] << 8));

    if(v < 9 || v > 28)
    {
        why = "this quest's DMaps aren't in a format a .zdmap file holds";
        return false;
    }

    const std::vector<byte> body(all.begin() + 8, all.end());
    const int n = body[0] | (body[1] << 8);
    std::vector<size_t> at;
    size_t pos = 2;
    dmap d;

    for(int i = 0; i < n; ++i)
    {
        size_t end = 0;
        at.push_back(pos);

        if(!read_dmap_255(body, pos, v, &end, d))
        {
            why = "a DMap of this quest's DMap section couldn't be read";
            return false;
        }

        pos = end;
    }

    at.push_back(pos);

    for(int i = first; i <= last && i < MAXDMAPS; ++i)
    {
        if(i < n)
        {
            recs.insert(recs.end(), body.begin() + at[i], body.begin() + at[i + 1]);
            continue;
        }

        dmap blank, got;
        blank_dmap_253(blank);
        Out o;
        size_t back = 0;

        if(!put_dmap_255(o, v, DMaps[i], blank, std::vector<byte>(), false) || !read_dmap_255(o.b, 0, v, &back, got) ||
           back != o.b.size() || !same_dmap(DMaps[i], got, false))
        {
            char b[64];
            snprintf(b, sizeof(b), "DMap %d can't be written in this quest's version", i);
            why = b;
            return false;
        }

        recs.insert(recs.end(), o.b.begin(), o.b.end());
    }

    *version = v;
    *cversion = (word)(all[2] | (all[3] << 8));
    return true;
}

// The DMap section's version of the open quest (0: not a 2.55 / 3.0 DMap section).
int zq255_dmap_version()
{
    std::vector<byte> orig, body;
    std::vector<size_t> rec_at;
    word v = 0;
    return dmap_layout(orig, body, v, rec_at) ? v : 0;
}

// n DMap records of version v laid end to end (a .zdmap file's), split from data[at...]; false if
// they don't read as that version.
bool zq255_dmap_split(const std::vector<byte> &data, size_t at, word v, int n, std::vector<std::vector<byte> > &recs)
{
    recs.clear();

    if(v < 9 || v > 28)
        return false;

    dmap d;

    for(int i = 0; i < n; ++i)
    {
        size_t end = 0;

        if(!read_dmap_255(data, at, v, &end, d))
            return false;

        recs.push_back(std::vector<byte>(data.begin() + at, data.begin() + end));
        at = end;
    }

    return true;
}

// ---- .map files: export and import (zq_exports.cpp) ----------------------------------------
//
// A .map file holds one map's 136 screens, each one's record as the MAP section holds it
// (upstream's zmap::save and zmap::load: the section's version as a word, a byte that was once
// the build, then writemapscreen's record of each screen). A screen a .map import puts in is
// kept as a whole record in the section's version (zq255.screen_records): the save writes it,
// the screen data editor and the script remap read it, and TheMaps (2.53's view, which the map
// painter edits) holds what it reads as, as for a screen as loaded.

void q30_set_screen_music(int index, long music);   // q255_store_zq.cpp

namespace
{

// A map's place in a MAP section's data (after its size): whether the section has it and
// holds screens for it, the auto layers and palette its unused screens read, where its first
// screen starts.
struct MapPlace
{
    bool found, screens;
    word autolayers[6];
    word autopalette;
    size_t at;
};

bool find_map(const std::vector<byte> &body, word v, int map, MapPlace &mp)
{
    memset(&mp, 0, sizeof(mp));
    In in(body, 0);
    const int maps = in.g16();
    mapscr scratch;

    for(int i = 0; i < maps && !in.bad; ++i)
    {
        byte valid = 1;
        word autolayers[6] = { 0, 0, 0, 0, 0, 0 };
        word autopalette = 0;

        if(v > 22)
            valid = in.g8();

        if(valid)
        {
            if(v > 25)
            {
                for(int q = 0; q < 6; ++q) autolayers[q] = in.g16();
                if(v >= 36) autopalette = in.g16();
            }

            if(v >= 31)
                in.skip(64);
        }

        if(in.bad)
            return false;

        if(i == map)
        {
            mp.found = true;
            mp.screens = valid != 0;
            memcpy(mp.autolayers, autolayers, sizeof(autolayers));
            mp.autopalette = autopalette;
            mp.at = in.pos;
            return true;
        }

        for(int j = 0; valid && j < MAPSCRS; ++j)
        {
            const size_t used = read_screen_255(body, in.pos, v, j, autolayers, autopalette, scratch);

            if(!used)
                return false;

            in.pos += used;
        }
    }

    return !in.bad;     // (found false: a map the section doesn't have)
}

// A v37+ screen record's music object (0: none of its own).
long record_music(const std::vector<byte> &rec, word v)
{
    if(v < 37 || rec.empty())
        return 0;

    Tape t;
    TapeParser p(&rec[0], rec.size(), 0, &t);
    const TapeField *f = zq_tape_screen(p, v) ? t.find("music") : NULL;
    return f ? (long)f->value : 0;
}

} // namespace

// The open quest's MAP section version, 0 when it isn't a 2.55 / 3.0 one.
int zq255_map_version()
{
    const ZqQuestPart *part = find_part(ID_MAPS);
    std::vector<byte> orig;

    if(!part || header.zelda_version != 0x255 || !part_bytes(*part, orig) || orig.size() < 10)
        return 0;

    const word v = (word)(orig[0] | (orig[1] << 8));
    return v >= 18 && v <= 41 ? v : 0;
}

// Map `map`'s 136 screen records as the quest's own-version save writes them now (edits and
// imports in), and the section's version. A map the section holds no screens for (v23+) has
// only unused screens: each one's record is its valid byte, as upstream writes an unused screen.
bool zq255_map_export(int map, std::vector<std::vector<byte> > &recs, word *version, std::string &why)
{
    recs.clear();
    const ZqQuestPart *part = find_part(ID_MAPS);
    std::vector<byte> all;

    if(!part || header.zelda_version != 0x255 || !zq_own_part_bytes(*part, all) || all.size() < 10)
    {
        why = "the quest's maps couldn't be written";
        return false;
    }

    const word v = (word)(all[0] | (all[1] << 8));
    const std::vector<byte> body(all.begin() + 8, all.end());
    MapPlace mp;

    if(v < 18 || v > 41 || !find_map(body, v, map, mp) || !mp.found)
    {
        why = "the map isn't in the quest's map section as expected";
        return false;
    }

    size_t at = mp.at;
    mapscr scratch;

    for(int j = 0; j < MAPSCRS; ++j)
    {
        const size_t index = (size_t)map * MAPSCRS + j;

        if(!mp.screens)
        {
            recs.push_back(std::vector<byte>(1, index < TheMaps.size() ? (byte)(TheMaps[index].valid & ~mVALID) : (byte)0));
            continue;
        }

        const size_t used = read_screen_255(body, at, v, j, mp.autolayers, mp.autopalette, scratch);

        if(!used)
        {
            why = "a screen of the map doesn't read back";
            recs.clear();
            return false;
        }

        recs.push_back(std::vector<byte>(body.begin() + at, body.begin() + at + used));
        at += used;
    }

    *version = v;
    return true;
}

// The 136 screen records of version v laid end to end from data[at...] (a .map file's after its
// header), as each reads; false if they don't read as that version.
bool zq255_map_split(const std::vector<byte> &data, size_t at, word v, std::vector<std::vector<byte> > &recs)
{
    recs.clear();

    if(v < 18 || v > 41)
        return false;

    const word no_layers[6] = { 0, 0, 0, 0, 0, 0 };
    mapscr scratch;

    for(int j = 0; j < MAPSCRS; ++j)
    {
        const size_t used = read_screen_255(data, at, v, j, no_layers, 0, scratch);

        if(!used)
            return false;

        recs.push_back(std::vector<byte>(data.begin() + at, data.begin() + at + used));
        at += used;
    }

    return true;
}

// A .map file of 2.53 or older (its header the version and build of the ZC that wrote it, as
// 2.53's zmap::save writes it): its 136 screens read by 2.53's reader as 2.53's import reads them
// (with that version as the header's), each written again as 2.53's own record, which is MAP
// format 18, where 2.55's formats begin. As 2.53's import, a file whose first screen has no
// version mark and whose header isn't 2.53's own is no map file.
bool zq255_map_from_253(const std::vector<byte> &file, std::vector<std::vector<byte> > &recs, std::string &why)
{
    recs.clear();

    if(file.size() < 3)
    {
        why = "it isn't a map file";
        return false;
    }

    const word version = (word)(file[0] | (file[1] << 8));
    const byte build = file[2];
    zquestheader h = header;
    h.zelda_version = version;
    h.build = build;
    size_t at = 3;

    for(int j = 0; j < MAPSCRS; ++j)
    {
        mapscr m;
        const size_t used = read_screen_old(file, at, h, version, m);

        if(!used)
        {
            why = "it holds less than a whole map (incomplete data)";
            recs.clear();
            return false;
        }

        if(j == 0 && !(m.valid & mVERSION) && !(version == ZELDA_VERSION && build == VERSION_BUILD))
        {
            why = "it isn't a map file (invalid version)";
            recs.clear();
            return false;
        }

        Out o;
        screen_bytes_253(o, m);
        recs.push_back(o.b);
        at += used;
    }

    return true;
}

// .map import: map `map`'s screens j with a record in recs (136 of them, in the section's
// version; an empty one leaves its screen as it is) replaced. Every record is read first, as the
// loader reads it here (the map's auto layers where a save keeps the map's own fields), so
// nothing changes if one doesn't read. Each screen then counts as unchanged since, so a save
// writes its record as it is until the screen is edited; the 2.55/3.0-only fields edited on
// it before are dropped (the import replaces the whole screen).
bool zq255_map_take(int map, const std::vector<std::vector<byte> > &recs, std::string &why)
{
    const ZqQuestPart *part = find_part(ID_MAPS);
    std::vector<byte> orig;
    const word v = (word)zq255_map_version();

    if(!v || !part || !part_bytes(*part, orig) || orig.size() < 10 || recs.size() != MAPSCRS || map < 0 ||
       (size_t)(map + 1) * MAPSCRS > TheMaps.size())
    {
        why = "the quest's maps can't take it";
        return false;
    }

    const std::vector<byte> body(orig.begin() + 8, orig.end());
    MapPlace mp;

    if(!find_map(body, v, map, mp))
    {
        why = "the quest's map section isn't laid out as expected";
        return false;
    }

    // as write_maps_255: a map the file holds screens for keeps its own fields; one written anew
    // has no auto layers, and the palette Map Count gave it
    std::map<int, word>::const_iterator nm = g_new_maps.find(map);
    const bool head = mp.found && mp.screens && nm == g_new_maps.end();
    const word no_layers[6] = { 0, 0, 0, 0, 0, 0 };
    const word palette = head ? mp.autopalette : nm != g_new_maps.end() && v >= 36 ? nm->second : 0;
    std::vector<mapscr> got(MAPSCRS);

    for(int j = 0; j < MAPSCRS; ++j)
    {
        if(recs[j].empty())
            continue;

        const size_t index = (size_t)map * MAPSCRS + j;
        got[j].screen_midi = TheMaps[index].screen_midi;

        if(read_screen_255(recs[j], 0, v, j, head ? mp.autolayers : no_layers, palette, got[j]) != recs[j].size())
        {
            char b[96];
            snprintf(b, sizeof(b), "screen %X doesn't read in this quest's format %d", j, (int)v);
            why = b;
            return false;
        }
    }

    if(g_screens_loaded.size() < TheMaps.size())
        g_screens_loaded.resize(TheMaps.size(), 0);

    for(int j = 0; j < MAPSCRS; ++j)
    {
        if(recs[j].empty())
            continue;

        const int index = map * MAPSCRS + j;
        TheMaps[index] = got[j];
        zq255.screen_records[index] = recs[j];
        zq255.screen_fields.erase(index);
        g_screens_loaded[index] = screen_hash(TheMaps[index]);
        q30_set_screen_music(index, record_music(recs[j], v));
    }

    return true;
}

// ---- map and screen operations (zq_class.cpp's zmap) --------------------------------------
//
// zmap's operations (Copy and the pastes, clearscr, Template, Undo, Quest > Map Count) change
// TheMaps, 2.53's view of each screen. A 2.55/3.0 screen's record holds more: the fields 2.53's
// mapscr has no room for, and the values it narrows (room types, flags, items). The writer
// takes that from the screen's record, kept by its place: the file's record there, or one put
// in for it (zq255.screen_records), with the screen editor's edits on it (zq255.screen_fields).
// So after an operation every screen it changed gets a record of its own, made as upstream's
// ZQuest (2.55.18 and 3.0 zq_class.cpp) leaves that screen: what upstream copies comes from the
// copied screen's record whole, what it clears is cleared, the rest stays the screen's own.
// TheMaps then holds what that record reads as, as for a screen as loaded.

namespace
{

// The MAP section of the quest as it stands (a save puts what it wrote in the loaded one's
// place), each map's head and each screen's record in it.
struct MapsAt
{
    int64_t offset, length;
    word v;
    std::vector<byte> body;
    std::vector<MapPlace> maps;
    std::vector<std::vector<size_t> > at;   // a map with screens: where each record starts, then the end

    MapsAt() : offset(-1), length(-1), v(0) {}
};

MapsAt g_at;

// The open quest's MAP version when its screens are 2.55/3.0 records (0 otherwise).
word ops_version()
{
    return newer_map_version();
}

const MapsAt *maps_at()
{
    if(!ops_version())
        return NULL;

    const ZqQuestPart *part = find_part(ID_MAPS);

    if(g_at.v && g_at.offset == part->offset && g_at.length == part->length)
        return &g_at;

    g_at = MapsAt();
    std::vector<byte> orig;

    if(!part_bytes(*part, orig) || orig.size() < 10)
        return NULL;

    MapsAt m;
    m.v = (word)(orig[0] | (orig[1] << 8));
    m.body.assign(orig.begin() + 8, orig.end());
    In in(m.body, 0);
    const int maps = in.g16();
    mapscr scratch;

    for(int i = 0; i < maps && !in.bad; ++i)
    {
        MapPlace mp;
        memset(&mp, 0, sizeof(mp));
        byte valid = 1;

        if(m.v > 22)
            valid = in.g8();

        if(valid)
        {
            if(m.v > 25)
            {
                for(int q = 0; q < 6; ++q) mp.autolayers[q] = in.g16();
                if(m.v >= 36) mp.autopalette = in.g16();
            }

            if(m.v >= 31)
                in.skip(64);
        }

        if(in.bad)
            return NULL;

        mp.found = true;
        mp.screens = valid != 0;
        mp.at = in.pos;
        std::vector<size_t> at;

        for(int j = 0; valid && j < MAPSCRS; ++j)
        {
            at.push_back(in.pos);
            const size_t used = read_screen_255(m.body, in.pos, m.v, j, mp.autolayers, mp.autopalette, scratch);

            if(!used)
                return NULL;

            in.pos += used;
        }

        if(valid)
            at.push_back(in.pos);

        m.maps.push_back(mp);
        m.at.push_back(at);
    }

    if(in.bad)
        return NULL;

    m.offset = part->offset;
    m.length = part->length;
    g_at = m;
    return &g_at;
}

// What write_maps_255 reads screen index's record with: the map's head (auto layers and
// palette for its unused screens), and where the file has the record (from == to: nowhere).
struct ScreenAt
{
    word layers[6];
    word palette;
    size_t from, to;
};

void screen_at(const MapsAt &m, size_t index, ScreenAt &s)
{
    const int i = (int)(index / MAPSCRS), j = (int)(index % MAPSCRS);
    memset(&s, 0, sizeof(s));
    std::map<int, word>::const_iterator nm = g_new_maps.find(i);

    if(nm != g_new_maps.end())
        s.palette = m.v >= 36 ? nm->second : 0;
    else if(i < (int)m.maps.size() && m.maps[i].screens)
    {
        memcpy(s.layers, m.maps[i].autolayers, sizeof(s.layers));
        s.palette = m.maps[i].autopalette;
        s.from = m.at[i][j];
        s.to = m.at[i][j + 1];
    }
}

// Screen index's record as a save would write it now: its record with ZQuest's screen and the
// screen editor's edits put on it (false: it can't be laid out).
bool screen_now(const MapsAt &m, size_t index, std::vector<byte> &rec)
{
    if(index >= TheMaps.size())
        return false;

    ScreenAt s;
    screen_at(m, index, s);
    const int j = (int)(index % MAPSCRS);
    mapscr loaded, blank;

    if(!blank_screen(m.v, blank))
        return false;

    loaded.screen_midi = TheMaps[index].screen_midi;
    const std::vector<byte> *imp = imported_screen(index);
    const std::vector<byte> &src = imp ? *imp : m.body;
    const size_t from = imp ? 0 : s.from, to = imp ? imp->size() : s.to;
    const bool used = to > from;

    if(used)
    {
        if(read_screen_255(src, from, m.v, j, s.layers, s.palette, loaded) != to - from)
            return false;
    }
    else
        clear_screen(&loaded);

    bool kept = false;
    const int r = screen_out(index, j, m.v, loaded, used, src, from, to, s.layers, s.palette, blank, rec, kept);

    if(r == SO_COPY)
        rec.assign(src.begin() + from, src.begin() + to);

    return r == SO_NEW || r == SO_COPY;
}

// Makes rec screen index's record, as a .map import does (zq255_map_take): the screen
// editor's edits are in it, and TheMaps takes what it reads as (read_in; an unused screen
// keeps what ZQuest has: nothing else of it is in the record).
bool set_record(const MapsAt &m, size_t index, const std::vector<byte> &rec, bool read_in)
{
    if(index >= TheMaps.size() || rec.empty())
        return false;

    if(read_in && (m.v < 23 || (rec[0] & mVALID)))
    {
        ScreenAt s;
        screen_at(m, index, s);
        mapscr got;
        got.screen_midi = TheMaps[index].screen_midi;

        if(read_screen_255(rec, 0, m.v, (int)(index % MAPSCRS), s.layers, s.palette, got) != rec.size())
            return false;

        TheMaps[index] = got;
    }

    zq255.screen_records[(int)index] = rec;
    zq255.screen_fields.erase((int)index);

    if(g_screens_loaded.size() < TheMaps.size())
        g_screens_loaded.resize(TheMaps.size(), 0);

    g_screens_loaded[index] = screen_hash(TheMaps[index]);
    q30_set_screen_music((int)index, record_music(rec, m.v));
    return true;
}

// A screen record's fields by key with every group on: a field of a group the record leaves
// out has what it reads as there (the schema's default).
bool fields_all(word v, const std::vector<byte> &rec, Tape &all)
{
    Tape t;
    TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);

    if(!zq_tape_screen(p, v) || p.pos != rec.size())
        return false;

    if(TapeField *has = t.find("has"))
        has->value = 0xEFFF;

    TapeParser q(&t, &all);
    zq_tape_screen(q, v);
    return !q.bad;
}

// Puts fields into a screen record by key: one the record has takes the value, one it lacks
// comes in with the group it belongs to switched on. counts: the record's own counts set first
// (which FFC slots it has). The record is laid out again; false when it doesn't read, or it is
// an unused screen.
bool tape_put(word v, std::vector<byte> &rec, const std::vector<TapeField> &edits,
              const std::vector<std::pair<std::string, int64_t> > &counts)
{
    Tape t;
    TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);

    if(!zq_tape_screen(p, v) || p.pos != rec.size())
        return false;

    if(TapeField *has = t.find("has"))
    {
        Tape all;

        if(!fields_all(v, rec, all))
            return false;

        for(size_t k = 0; k < edits.size(); ++k)
            if(!t.find(edits[k].key.c_str()))
                if(const TapeField *g = all.find(edits[k].key.c_str()))
                    has->value |= g->gate;
    }
    else if(v >= 23)
        return false;

    for(size_t k = 0; k < counts.size(); ++k)
        if(TapeField *x = t.find(counts[k].first.c_str()))
            x->value = counts[k].second;

    for(size_t k = 0; k < edits.size(); ++k)
    {
        if(TapeField *x = t.find(edits[k].key.c_str()))
            take_field(*x, edits[k]);
        else
            t.f.push_back(edits[k]);
    }

    Tape out;
    TapeParser r(&t, &out);
    zq_tape_screen(r, v);

    if(r.bad)
        return false;

    std::vector<byte> b;
    out.encode(b);
    Tape chk;
    TapeParser c(b.empty() ? NULL : &b[0], b.size(), 0, &chk);

    if(!zq_tape_screen(c, v) || c.pos != b.size())
        return false;

    rec.swap(b);
    return true;
}

// A field key a paste takes: the name itself, or with "#" at its end the name and a number
// (one field of an array: "door#" is door0 to door3).
bool key_is(const std::string &key, const char *name)
{
    const size_t n = strlen(name);

    if(!n || name[n - 1] != '#')
        return key == name;

    if(key.size() < n || key.compare(0, n - 1, name, n - 1))
        return false;

    for(size_t k = n - 1; k < key.size(); ++k)
        if(key[k] < '0' || key[k] > '9')
            return false;

    return key.size() > n - 1;
}

void take_keys(const Tape &from, const char *const *names, std::vector<TapeField> &edits)
{
    for(size_t k = 0; k < from.f.size(); ++k)
        for(const char *const *n = names; *n; ++n)
            if(key_is(from.f[k].key, *n))
            {
                edits.push_back(from.f[k]);
                break;
            }
}

// FFC m's field key ("ffc3.x" -> 3, rest ".x"); -1 for a field of no FFC.
int ffc_of(const std::string &key, std::string *rest)
{
    if(key.compare(0, 3, "ffc") || key.size() < 5 || key[3] < '0' || key[3] > '9')
        return -1;

    size_t k = 3;
    int m = 0;

    while(k < key.size() && key[k] >= '0' && key[k] <= '9')
        m = m * 10 + (key[k++] - '0');

    if(k >= key.size() || key[k] != '.')
        return -1;

    if(rest)
        *rest = key.substr(k);

    return m;
}

// The fields each of upstream's pastes takes from the copied screen (zmap::Paste... in
// 2.55.18 and 3.0); a field the quest's version doesn't have isn't there to take.
const char *const k_combos[] = { "door_combo_set", "door#", "data", "sflag", "cset", NULL };
const char *const k_toall[] = { "data", "sflag", "cset", NULL };
const char *const k_under[] = { "undercombo", "undercset", NULL };
const char *const k_secrets[] = { "secretcombo", "secretcset", "secretflag", NULL };
// (and flags2's warp bits, which ZQuest's paste puts in TheMaps)
const char *const k_warps[] = { "sidewarpindex", "tilewarptype#", "tilewarpdmap#", "tilewarpscr#", "sidewarptype#",
                                "sidewarpdmap#", "sidewarpscr#", "sidewarpoverlayflags", "tilewarpoverlayflags", NULL };
// (and flags2 but its warp bits; enemyflags is upstream's flags11; 2.55.18 pastes the screen
// MIDI, 3.0 its music, which a version before MAP 37 holds as the MIDI)
const char *const k_screendata[] = { "csensitive", "oceansfx", "bosssfx", "secretsfx", "holdupsfx", "flags", "flags3",
                                     "flags4", "flags5", "flags6", "flags7", "flags8", "flags9", "flags10", "enemyflags",
                                     "item", "hasitem", "itemx", "itemy", "nextmap", "nextscr", "nocarry", "noreset",
                                     "exstate_reset", "exstate_carry", "path#", "pattern", "exitdir", "screen_midi",
                                     "music", "stairx", "stairy", "timedwarptics", NULL };
const char *const k_warpret[] = { "warpreturnc", "warparrivalx", "warparrivaly", "warpreturnx#", "warpreturny#", NULL };
const char *const k_doors[] = { "door#", "door_combo_set", NULL };
const char *const k_layers[] = { "layermap#", "layerscreen#", "layeropacity#", NULL };
const char *const k_room[] = { "room", "catchall", NULL };
const char *const k_guy[] = { "guy", "guytile", "guycs", "str", NULL };     // and two room flags
const char *const k_palette[] = { "color", NULL };
const char *const k_enemies[] = { "enemy#", NULL };
const char *const k_ffc_counts[] = { "ffc_count", "numff", NULL };   // which FFC slots a screen has

// What Copy took: the copied screen's record as it stood then.
struct Copied
{
    bool on;
    word v;
    int ffc;
    std::vector<byte> rec;

    Copied() : on(false), v(0), ffc(-1) {}
};

Copied g_copy;

// Undo (zmap::Ugo / Uhuilai): each undo slot's screen state beside 2.53's copy of the screen.
struct UndoScreen
{
    bool rec_on, fields_on, music_on, hash_on;
    std::vector<byte> rec;
    std::map<std::string, TapeField> fields;
    long music;
    uint64_t hash;

    UndoScreen() : rec_on(false), fields_on(false), music_on(false), hash_on(false), music(0), hash(0) {}
};

std::vector<UndoScreen> g_undo;

void state_take(int index, UndoScreen &u)
{
    u = UndoScreen();

    if(index < 0)
        return;

    std::map<int, std::vector<byte> >::const_iterator r = zq255.screen_records.find(index);
    std::map<int, std::map<std::string, TapeField> >::const_iterator f = zq255.screen_fields.find(index);
    std::map<int, long>::const_iterator mu = zq255.screen_music.find(index);

    if((u.rec_on = r != zq255.screen_records.end()))
        u.rec = r->second;

    if((u.fields_on = f != zq255.screen_fields.end()))
        u.fields = f->second;

    if((u.music_on = mu != zq255.screen_music.end()))
        u.music = mu->second;

    if((u.hash_on = (size_t)index < g_screens_loaded.size()))
        u.hash = g_screens_loaded[index];
}

void state_put(int index, const UndoScreen &u)
{
    if(index < 0)
        return;

    if(u.rec_on)
        zq255.screen_records[index] = u.rec;
    else
        zq255.screen_records.erase(index);

    if(u.fields_on)
        zq255.screen_fields[index] = u.fields;
    else
        zq255.screen_fields.erase(index);

    if(u.music_on)
        zq255.screen_music[index] = u.music;
    else
        zq255.screen_music.erase(index);

    if((size_t)index < TheMaps.size())
    {
        if(g_screens_loaded.size() < TheMaps.size())
            g_screens_loaded.resize(TheMaps.size(), 0);

        g_screens_loaded[index] = u.hash_on ? u.hash : 0;
    }
}

// A new quest forgets the last one's maps and undo (what Copy took stays, as 2.53's copy does).
void ops_loaded()
{
    g_new_maps.clear();
    g_undo.clear();
    g_at = MapsAt();
}

ZqLoadHookRegistration g_hook_ops(ops_loaded);

// ---- the init screen script data (upstream's zinit.screen_data) ----
//
// Upstream's Copy takes the copied screen's init screen data (copyscrdata), Paste All and
// Paste All To All put it on each screen they paste (2.55.18 and 3.0 zq_class.cpp); Delete,
// Template, Map Count and the other pastes leave it as it is. 2.55.18's Copy reads it with
// operator[], which leaves an empty entry for the screen; its Undo swaps the screens only.
// 3.0's undo and redo of Paste All paste the copied data again, the redo of Paste All To All
// too. The INIT writer (zqw_init.cpp) writes the store's.

Zq255Store::ScreenData g_copy_data;     // what Copy took (size 0: none)

// what the last operation's undo does with the screen data (3.0)
struct DataUndo
{
    int kind;       // 0 nothing, 1 Paste All (index: its screen), 2 Paste All To All (index: the map's first screen)
    int index;
    int swaps;
};

DataUndo g_data_undo = { 0, 0, 0 };

// The open quest's INIT section version when it is a 2.55 / 3.0 one (35 on hold screen data).
int init_version()
{
    const ZqQuestPart *part = zq_find_part(ID_INITDATA);
    byte b[2] = { 0, 0 };

    if(!part || header.zelda_version != 0x255 || !zq_part_read(*part, 0, b, 2))
        return 0;

    return b[0] | (b[1] << 8);
}

void data_copied(int index)
{
    std::map<int, Zq255Store::ScreenData>::const_iterator i = zq255.screen_data_init.find(index);

    if(i != zq255.screen_data_init.end())
        g_copy_data = i->second;
    else
    {
        g_copy_data = Zq255Store::ScreenData();
        const int v = init_version();

        if(v >= 35 && v <= 38 && q255_header_version[0] < 3)
            zq255.screen_data_init[index];      // 2.55.18's operator[]
    }
}

void data_put(int index)
{
    if(index >= 0 && init_version() >= 35)
        zq255.screen_data_init[index] = g_copy_data;   // size 0: the default, as operator= leaves it
}

void data_pasted_all(int index)
{
    if(init_version() < 35)
        return;

    data_put(index);

    if(g_data_undo.kind == 0)
    {
        g_data_undo.kind = 1;
        g_data_undo.index = index;
        g_data_undo.swaps = 0;
    }
    else if(g_data_undo.kind == 1 && g_data_undo.index != index)
    {
        g_data_undo.kind = 2;       // one operation pasting every screen: Paste All To All
        g_data_undo.index = index - index % MAPSCRS;
    }
}

void data_undone()
{
    if(q255_header_version[0] < 3)
        return;

    if(g_data_undo.kind == 1)
        data_put(g_data_undo.index);
    else if(g_data_undo.kind == 2 && ++g_data_undo.swaps % 2 == 0)
    {
        for(int x = 0; x < 128; ++x)
            data_put(g_data_undo.index + x);
    }
}

} // namespace

void zq255_screen_copied(int index, int ffc)
{
    if(ffc < 0 && index >= 0)
        data_copied(index);

    g_copy.on = false;
    const MapsAt *m = maps_at();

    if(m && index >= 0 && screen_now(*m, (size_t)index, g_copy.rec))
    {
        g_copy.on = true;
        g_copy.v = m->v;
        g_copy.ffc = ffc;
    }
}

void zq255_screen_pasted(int index, int kind, int ffc)
{
    if(kind == ZQ255_PASTE_ALL && index >= 0)
        data_pasted_all(index);

    const MapsAt *m = maps_at();

    if(!m || !g_copy.on || g_copy.v != m->v || index < 0 || (size_t)index >= TheMaps.size())
        return;

    const word v = m->v;

    if(kind == ZQ255_PASTE_ALL)
    {
        set_record(*m, (size_t)index, g_copy.rec, true);
        return;
    }

    // the screen as the paste left it in TheMaps, on its own record
    std::vector<byte> rec;

    if(!screen_now(*m, (size_t)index, rec))
        return;

    Tape from, to;
    std::vector<TapeField> edits;
    std::vector<std::pair<std::string, int64_t> > counts;

    if(!fields_all(v, g_copy.rec, from) || !fields_all(v, rec, to))
        return;

    switch(kind)
    {
    case ZQ255_PASTE_COMBOS:      take_keys(from, k_combos, edits); break;
    case ZQ255_PASTE_TOALL:       take_keys(from, k_toall, edits); break;
    case ZQ255_PASTE_UNDERCOMBO:  take_keys(from, k_under, edits); break;
    case ZQ255_PASTE_SECRETS:     take_keys(from, k_secrets, edits); break;
    case ZQ255_PASTE_WARPS:       take_keys(from, k_warps, edits); break;
    case ZQ255_PASTE_SCREENDATA:  take_keys(from, k_screendata, edits); break;
    case ZQ255_PASTE_WARPRETURN:  take_keys(from, k_warpret, edits); break;
    case ZQ255_PASTE_DOORS:       take_keys(from, k_doors, edits); break;
    case ZQ255_PASTE_LAYERS:      take_keys(from, k_layers, edits); break;
    case ZQ255_PASTE_ROOM:        take_keys(from, k_room, edits); break;
    case ZQ255_PASTE_PALETTE:     take_keys(from, k_palette, edits); break;
    case ZQ255_PASTE_ENEMIES:     take_keys(from, k_enemies, edits); break;

    case ZQ255_PASTE_GUY:
    {
        take_keys(from, k_guy, edits);
        const TapeField *a = from.find("roomflags"), *b = to.find("roomflags");

        if(a && b)
        {
            // upstream's RFL_ALWAYS_GUY and RFL_GUYFIRES
            TapeField e = *b;
            e.value = (b->value & ~(int64_t)3) | (a->value & 3);
            edits.push_back(e);
        }

        break;
    }

    case ZQ255_PASTE_FFCS:
        // every FFC slot, as many as the copied screen has
        for(size_t k = 0; k < from.f.size(); ++k)
            if(ffc_of(from.f[k].key, NULL) >= 0)
                edits.push_back(from.f[k]);

        for(const char *const *n = k_ffc_counts; *n; ++n)
            if(const TapeField *c = from.find(*n))
                counts.push_back(std::make_pair(std::string(*n), c->value));

        break;

    case ZQ255_PASTE_ONEFFC:
    {
        // the copied FFC into slot ffc, but for its place
        if(g_copy.ffc < 0 || ffc < 0)
            return;

        char to_key[16];
        snprintf(to_key, sizeof(to_key), "ffc%d", ffc);

        for(size_t k = 0; k < from.f.size(); ++k)
        {
            std::string rest;

            if(ffc_of(from.f[k].key, &rest) == g_copy.ffc && rest != ".x" && rest != ".y")
            {
                TapeField e = from.f[k];
                e.key = to_key + rest;
                edits.push_back(e);
            }
        }

        if(const TapeField *c = to.find("ffc_count"))
            counts.push_back(std::make_pair(std::string("ffc_count"), c->value > ffc ? c->value : (int64_t)ffc + 1));
        else if(const TapeField *c = to.find("numff"))
            counts.push_back(std::make_pair(std::string("numff"), c->value | ((int64_t)1 << ffc)));

        break;
    }

    default:
        return;
    }

    if(!edits.empty() && !tape_put(v, rec, edits, counts))
        return;     // left as the paste made it in TheMaps

    set_record(*m, (size_t)index, rec, true);
}

void zq255_screen_cleared(int index)
{
    const MapsAt *m = maps_at();

    if(!m || index < 0 || (size_t)index >= TheMaps.size())
        return;

    // upstream's clearscr: the map's auto layers (2.55.18 and 3.0), and its auto palette (3.0)
    ScreenAt s;
    screen_at(*m, (size_t)index, s);
    mapscr &c = TheMaps[index];
    const int j = index % MAPSCRS;

    for(int q = 0; q < 6 && m->v > 25; ++q)
    {
        const byte layer = s.layers[q] < 256 ? (byte)s.layers[q] : (byte)0;
        c.layermap[q] = layer;
        c.layerscreen[q] = layer ? (byte)j : 0;
    }

    if(m->v >= 36)
        c.color = s.palette;

    unused_defaults(c);

    // nothing of the screen's record or edits is left: it is written from ZQuest's screen alone
    zq255.screen_fields.erase(index);
    zq255.screen_records.erase(index);
    mapscr loaded, blank;
    clear_screen(&loaded);
    loaded.screen_midi = c.screen_midi;
    std::vector<byte> rec;
    bool kept = false;

    if(blank_screen(m->v, blank) &&
       screen_out((size_t)index, j, m->v, loaded, false, m->body, 0, 0, s.layers, s.palette, blank, rec, kept) == SO_NEW)
        set_record(*m, (size_t)index, rec, false);
}

void zq255_screen_templated(int index, int tmpl)
{
    const MapsAt *m = maps_at();
    std::vector<byte> rec, trec;

    if(!m || index < 0 || tmpl < 0 || (size_t)index >= TheMaps.size() || (size_t)tmpl >= TheMaps.size() ||
       !screen_now(*m, (size_t)index, rec) || !screen_now(*m, (size_t)tmpl, trec))
        return;

    // The squares Template takes from the template screen keep that one's values as its record
    // has them (a flag or combo 2.53 narrows); the floor and the doors are ZQuest's.
    Tape a, t;

    if(fields_all(m->v, rec, a) && fields_all(m->v, trec, t))
    {
        const TapeField *ad = a.find("data"), *as = a.find("sflag"), *ac = a.find("cset");
        const TapeField *td = t.find("data"), *ts = t.find("sflag"), *tc = t.find("cset");

        if(ad && as && ac && td && ts && tc && ad->bytes.size() == 352 && td->bytes.size() == 352 && as->bytes.size() == 176 &&
           ts->bytes.size() == 176 && ac->bytes.size() == 176 && tc->bytes.size() == 176)
        {
            std::vector<TapeField> e(3);
            e[0] = *ad;
            e[1] = *as;
            e[2] = *ac;
            const mapscr &c = TheMaps[index], &tm = TheMaps[tmpl];

            for(int k = 0; k < 176; ++k)
            {
                const int x = k & 15, y = k >> 4;
                const bool border = k < 32 || k >= 144 || (y >= 2 && y <= 9 && (x < 2 || x >= 14));

                if(border && c.data[k] == tm.data[k] && c.cset[k] == tm.cset[k] && c.sflag[k] == tm.sflag[k])
                {
                    e[0].bytes[2 * k] = td->bytes[2 * k];
                    e[0].bytes[2 * k + 1] = td->bytes[2 * k + 1];
                    e[1].bytes[k] = ts->bytes[k];
                    e[2].bytes[k] = tc->bytes[k];
                }
            }

            std::vector<std::pair<std::string, int64_t> > none;
            tape_put(m->v, rec, e, none);
        }
    }

    set_record(*m, (size_t)index, rec, true);
}

void zq255_screen_undo_note(int slot, int index)
{
    if(slot == 0)
        g_data_undo.kind = 0;   // a new operation: its own undo

    if(!ops_version() || slot < 0)
        return;

    if(g_undo.size() < (size_t)MAPSCRS + 6)
        g_undo.resize(MAPSCRS + 6);

    if((size_t)slot < g_undo.size())
        state_take(index, g_undo[slot]);
}

void zq255_screen_undo_swap(int slot, int index)
{
    if(slot == 0)
        data_undone();

    if(!ops_version() || slot < 0 || (size_t)slot >= g_undo.size() || index < 0)
        return;

    UndoScreen now;
    state_take(index, now);
    state_put(index, g_undo[slot]);
    g_undo[slot] = now;
}

void zq255_maps_counted(int old_count, int new_count, int from_map)
{
    const MapsAt *m = maps_at();

    if(!m)
        return;

    if(new_count < old_count)
    {
        // the maps taken away take their screens' records and edits along
        const int first = new_count * MAPSCRS;

        zq255.screen_records.erase(zq255.screen_records.lower_bound(first), zq255.screen_records.end());
        zq255.screen_fields.erase(zq255.screen_fields.lower_bound(first), zq255.screen_fields.end());
        zq255.screen_music.erase(zq255.screen_music.lower_bound(first), zq255.screen_music.end());
        g_new_maps.erase(g_new_maps.lower_bound(new_count), g_new_maps.end());
        return;
    }

    // New maps have no auto layers; 3.0 gives each the current map's auto palette.
    ScreenAt s;
    screen_at(*m, (size_t)from_map * MAPSCRS, s);

    for(int i = old_count; i < new_count; ++i)
        g_new_maps[i] = m->v >= 36 ? s.palette : 0;
}

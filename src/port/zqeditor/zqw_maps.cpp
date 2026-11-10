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

extern DoorComboSet *DoorComboSets;
extern dmap *DMaps;
extern std::vector<mapscr> TheMaps;
extern word map_count;
extern newcombo *combobuf;
extern zinitdata zinit;
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

    long long size = (long long)le32(orig, 4) + (long long)body.b.size() - (long long)(orig.size() - 8);
    Out head;
    head.pn(&orig[0], 4);
    head.p32((dword)size);

    if(pack_mputl((long)id, f) == EOF || pack_fwrite(&head.b[0], 8, f) != 8)
        return ERR_WRITE;

    if(!body.b.empty() && pack_fwrite(&body.b[0], (long)body.b.size(), f) != (long)body.b.size())
        return ERR_WRITE;

    return ZQW_OK;
}

unsigned long long fnv(const std::vector<byte> &b)
{
    unsigned long long h = 14695981039346656037ULL;

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
int mf_putc(int, void *) { return EOF; }
long mf_fwrite(const void *, long, void *) { return 0; }
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

unsigned long long screen_hash(const mapscr &m)
{
    Out o;
    screen_bytes_253(o, m);
    return fnv(o.b);
}

std::vector<unsigned long long> g_screens_loaded;   // each screen's hash in 2.53's form

void note_maps()
{
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
            if(rp) o.pn(rp + 1, 7);
            else for(int k = 0; k < 7; ++k) o.p8(0);
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

// What a map's own fields are in the v23+ format (before its screens).
struct MapHead
{
    byte valid;
    word autolayers[6];
    word autopalette;
};

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
    {
        Out s;
        s.p8(mVALID);
        s.p32(0);

        if(v < 25) s.p32(0); else s.p16(0);
        if(v > 29) s.p32(0);

        word nolayers[6] = { 0, 0, 0, 0, 0, 0 };

        if(v >= 23 && !read_screen_255(s.b, 0, v, 0, nolayers, 0, blank))
            return ERR_READ;
    }

    mapscr loaded, got, raw253, merged;
    Grouped g;
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

        // A map the file holds without screens loads cleared; if a screen of it changed,
        // it is written whole.
        bool any_changed = false;

        if(!mh.valid)
            for(int j = 0; j < MAPSCRS && !any_changed; ++j)
                any_changed = !screen_unchanged((size_t)i * MAPSCRS + j);

        if(map_in_raw && (mh.valid || !any_changed))
            o.copy(body, head_at, head_end);
        else
        {
            if(v > 22) o.p8(1);
            if(v > 25)
            {
                for(int q = 0; q < 6; ++q) o.p16(0);
                if(v >= 36) o.p16(0);
            }
            if(v >= 31) for(int q = 0; q < 64; ++q) o.p8(0);
        }

        if(map_in_raw && !mh.valid && !any_changed)
            continue;

        const bool screens_in_raw = map_in_raw && mh.valid;

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

                in.pos += used;
            }
            else
                clear_screen(&loaded);

            if(index >= TheMaps.size())
                return ERR_READ;

            const mapscr &c = TheMaps[index];
            const bool unchanged = used && screen_unchanged(index);

            if(unchanged && !zqw_force_serialize)
            {
                o.copy(body, start, in.pos);
                continue;
            }

            const std::vector<byte> rec_raw(body.begin() + start, body.begin() + in.pos);
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
                            return ERR_READ;

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

                // a map written anew has no layers or palette of its own for unused screens
                const bool head_kept = screens_in_raw;
                got.screen_midi = c.screen_midi;
                size_t back = read_screen_255(rec.b, 0, v, j, head_kept ? mh.autolayers : no_layers,
                                              head_kept ? mh.autopalette : 0, got);
                ok = back == rec.b.size() && same_screen(c, got);
            }

            if(ok && encoder_ok && !exact_differs(ID_MAPS, (int)index, unchanged, rec.b, body, start, in.pos))
                o.pn(&rec.b[0], rec.b.size());
            else if(refuse(ID_MAPS, (int)index, unchanged, "can't be held in this version"))
                return ERR_CANT_HOLD;
            else
                o.copy(body, start, in.pos);
        }
    }

    o.copy(body, in.pos, body.size());
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
    const int total = old_count > count ? old_count : count;
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

        if(i >= MAXDMAPS)
        {
            o.copy(body, start, end);
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
    return put_part(f, ctx.part->id, orig, o);
}

ZqWriterRegistration g_reg_dmaps_255(ID_DMAPS, write_dmaps_255, "DMaps, 2.55/3.0 versions 9-28");

} // namespace

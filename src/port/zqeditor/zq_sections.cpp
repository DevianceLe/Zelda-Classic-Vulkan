// See zq_sections.h. The loader (qst.cpp, q255_core.cpp) calls qst_section_hook around each
// part; while a part is being read, the port's file layer hands over every byte it reads
// (zcport/pack_record.h).

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>

#include "zdefs.h"
#include "qst.h"
#include "colors.h"
#include "zcport/pack_record.h"
#include "zq_sections.h"

extern void (*qst_section_hook)(PACKFILE *f, dword id, bool start);

// ZQuest's 2.53 writers (zq_class.cpp), called here only to fingerprint its data
extern zquestheader header;
extern miscQdata misc;
int writeheader(PACKFILE *f, zquestheader *Header);
int writerules(PACKFILE *f, zquestheader *Header);
int writestrings(PACKFILE *f, word version, word build, word start_msgstr, word max_msgstrs);
int writedoorcombosets(PACKFILE *f, zquestheader *Header);
int writedmaps(PACKFILE *f, word version, word build, word start_dmap, word max_dmaps);
int writemisc(PACKFILE *f, zquestheader *Header, miscQdata *Misc);
int writemisccolors(PACKFILE *f, zquestheader *Header, miscQdata *Misc);
int writegameicons(PACKFILE *f, zquestheader *Header, miscQdata *Misc);
int writeitems(PACKFILE *f, zquestheader *Header);
int writeweapons(PACKFILE *f, zquestheader *Header);
int writemaps(PACKFILE *f, zquestheader *Header);
int writecombos(PACKFILE *f, word version, word build, word start_combo, word max_combos);
int writecomboaliases(PACKFILE *f, word version, word build);
int writecolordata(PACKFILE *f, miscQdata *Misc, word version, word build, word start_cset, word max_csets);
int writetiles(PACKFILE *f, word version, word build, int start_tile, int max_tiles);
int writemidis(PACKFILE *f);
int writecheats(PACKFILE *f, zquestheader *Header);
int writeinitdata(PACKFILE *f, zquestheader *Header);
int writeguys(PACKFILE *f, zquestheader *Header);
int writelinksprites(PACKFILE *f, zquestheader *Header);
int writesubscreens(PACKFILE *f, zquestheader *Header);
int writeffscript(PACKFILE *f, zquestheader *Header);
int writesfx(PACKFILE *f, zquestheader *Header);
int writeitemdropsets(PACKFILE *f, zquestheader *Header);
int writefavorites(PACKFILE *f, zquestheader *Header);
void reset_combo_animations();
void reset_combo_animations2();
extern tiledata *newtilebuf;
int count_tiles(tiledata *buf);
int tilesize(byte format);

ZqQuestFileCopy zq_quest_file;

namespace
{

const dword ID_DAMAGED = 0xFFFFFFFF;    // qst.cpp passes this while it searches past bad data
const dword ID_TAIL = 0xFFFFFFFE;       // qst.cpp: whatever follows the last part, just before closing

bool is_raw(dword id)                   // parts written without an id in front
{
    return id == ID_HEADER || id == 0 || id == ID_DAMAGED || id == ID_TAIL;
}

// ---- the store: the parts' bytes live in a temporary file, not in memory. A quest's
// decoded file can be 200 MB, too much to hold a second time inside a 32-bit ZQuest.

FILE *g_store = NULL;
std::string g_store_path;
long long g_store_size = 0;         // bytes recorded, those still in g_pending included
std::vector<byte> g_pending;        // not written yet; the last byte stays here for pack_ungetc
bool g_store_cleanup_set = false;

void store_close()
{
    if(g_store)
    {
        fclose(g_store);
        g_store = NULL;
    }

    if(!g_store_path.empty())
        remove(g_store_path.c_str());

    g_store_size = 0;
    g_pending.clear();
}

void store_at_exit()
{
    store_close();
}

bool store_open()
{
    store_close();

    if(g_store_path.empty())
    {
        const char *dir = getenv("TEMP");

        if(!dir || !*dir)
            dir = getenv("TMP");

        char name[64];
        sprintf(name, "zq_parts_%lu.tmp", (unsigned long)_getpid());
        g_store_path = std::string(dir && *dir ? dir : ".") + "\\" + name;
    }

    if(!g_store_cleanup_set)
    {
        atexit(store_at_exit);
        g_store_cleanup_set = true;
    }

    g_store = fopen(g_store_path.c_str(), "w+b");
    return g_store != NULL;
}

// keep: how many of the last bytes stay pending
void store_flush(size_t keep)
{
    if(!g_store || g_pending.size() <= keep)
        return;

    size_t n = g_pending.size() - keep;
    _fseeki64(g_store, 0, SEEK_END);

    if(fwrite(&g_pending[0], 1, n, g_store) != n)
        zq_quest_file.inexact = true;

    g_pending.erase(g_pending.begin(), g_pending.begin() + n);
}

void record(void *user, const unsigned char *bytes, long n, int unget)
{
    ZqQuestPart *p = (ZqQuestPart *)user;

    if(unget)
    {
        if(p->length > 0 && !g_pending.empty())
        {
            g_pending.pop_back();
            --p->length;
            --g_store_size;
        }
        else
            zq_quest_file.inexact = true;
        return;
    }

    g_pending.insert(g_pending.end(), bytes, bytes + n);
    p->length += n;
    g_store_size += n;

    if(g_pending.size() >= (1 << 20))
        store_flush(1);
}

void hook(PACKFILE *f, dword id, bool start)
{
    zcport_pack_record(NULL, NULL, NULL);

    if(!start)
        return;

    if(id == ID_DAMAGED)
        zq_quest_file.inexact = true;

    ZqQuestPart part;
    part.id = id;
    part.offset = g_store_size;
    zq_quest_file.parts.push_back(part);
    zcport_pack_record(f, record, &zq_quest_file.parts.back());

    if(id == ID_TAIL)   // the loader is done: keep the rest of the file, then stop
    {
        byte buf[4096];

        while(pack_fread(buf, sizeof(buf), f) > 0)
            ;

        zcport_pack_record(NULL, NULL, NULL);

        if(zq_quest_file.parts.back().length == 0)
            zq_quest_file.parts.pop_back();
    }
}

} // namespace

void zq_sections_begin_load()
{
    zq_quest_file.parts.clear();
    zq_quest_file.inexact = false;

    // ZQ_NO_SECTIONS: for tracking down a problem, load without keeping parts
    if(!getenv("ZQ_NO_SECTIONS") && store_open())
        qst_section_hook = hook;
}

extern int qst_open_enc_method;
extern bool qst_open_compressed;

void zq_sections_end_load(bool loaded)
{
    zcport_pack_record(NULL, NULL, NULL);
    qst_section_hook = NULL;
    store_flush(0);
    zq_quest_file.enc_method = qst_open_enc_method;
    zq_quest_file.compressed = qst_open_compressed;
    zq_quest_file.zelda_version = header.zelda_version;
    zq_quest_file.build = header.build;

    if(!loaded || !g_store)
    {
        zq_quest_file.parts.clear();
        zq_quest_file.inexact = false;
        store_close();
    }
}

bool zq_part_read(const ZqQuestPart &p, long long at, void *buf, size_t n)
{
    if(!g_store || at < 0 || at + (long long)n > p.length)
        return false;

    store_flush(0);
    _fseeki64(g_store, p.offset + at, SEEK_SET);
    return fread(buf, 1, n, g_store) == n;
}

namespace
{

// Appends to the store what a writer writes, after the first 'skip' bytes (the part's id).
struct StoreSink
{
    long long skip;
};

int ss_fclose(void *) { return 0; }
int ss_getc(void *) { return EOF; }
int ss_ungetc(int, void *) { return EOF; }
long ss_fread(void *, long, void *) { return 0; }
int ss_feof(void *) { return 1; }
int ss_ferror(void *) { return 0; }
int ss_fseek(void *, int) { return -1; }

long ss_fwrite(const void *p, long n, void *user)
{
    StoreSink *s = (StoreSink *)user;
    const byte *b = (const byte *)p;
    long take = n;

    while(take > 0 && s->skip > 0)
    {
        ++b;
        --take;
        --s->skip;
    }

    g_pending.insert(g_pending.end(), b, b + take);
    g_store_size += take;

    if(g_pending.size() >= (1 << 20))
        store_flush(0);

    return n;
}

int ss_putc(int c, void *user)
{
    byte b = (byte)c;
    ss_fwrite(&b, 1, user);
    return c & 0xFF;
}

const PACKFILE_VTABLE g_store_vtable = { ss_fclose, ss_getc, ss_ungetc, ss_fread, ss_putc, ss_fwrite, ss_fseek, ss_feof, ss_ferror };

} // namespace

bool zq_part_replace(ZqQuestPart &p, int (*writer)(PACKFILE *f, const ZqQuestPart &p), int *rc)
{
    *rc = 1;

    if(!g_store)
        return false;

    store_flush(0);
    long long start = g_store_size;
    StoreSink sink;
    sink.skip = is_raw(p.id) ? 0 : 4;
    PACKFILE *f = zq_open_sink(&g_store_vtable, &sink);

    if(!f)
        return false;

    *rc = writer(f, p);     // may read p's old bytes: they stay where they are until it's done
    pack_fclose(f);
    store_flush(0);

    if(*rc != 0)
        return false;       // what it wrote stays unused at the end of the store

    p.offset = start;
    p.length = g_store_size - start;
    return true;
}

void zq_sections_forget()
{
    zq_quest_file.parts.clear();
    zq_quest_file.inexact = false;
    store_close();
}

// ---- fingerprints -------------------------------------------------------------------

namespace
{

// A write-only file that keeps a 64-bit FNV-1a hash of what is written to it.
struct HashSink
{
    unsigned long long h;
};

int hs_fclose(void *) { return 0; }
int hs_getc(void *) { return EOF; }
int hs_ungetc(int, void *) { return EOF; }
long hs_fread(void *, long, void *) { return 0; }
int hs_feof(void *) { return 1; }
int hs_ferror(void *) { return 0; }
int hs_fseek(void *, int) { return -1; }

void hs_add(HashSink *s, unsigned char b)
{
    s->h ^= b;
    s->h *= 1099511628211ULL;
}

int hs_putc(int c, void *user)
{
    hs_add((HashSink *)user, (unsigned char)c);
    return c & 0xFF;
}

long hs_fwrite(const void *p, long n, void *user)
{
    const unsigned char *b = (const unsigned char *)p;

    for(long i = 0; i < n; ++i)
        hs_add((HashSink *)user, b[i]);

    return n;
}

const PACKFILE_VTABLE g_hash_vtable = { hs_fclose, hs_getc, hs_ungetc, hs_fread, hs_putc, hs_fwrite, hs_fseek, hs_feof, hs_ferror };

} // namespace

// Version arguments are 2.53's, as a 2.53 save passes them, so the result doesn't depend on
// the version that was loaded.
bool zq_write_253_section(PACKFILE *f, dword id)
{
    switch(id)
    {
    case ID_HEADER:       return writeheader(f, &header) == 0;
    case ID_RULES:        return writerules(f, &header) == 0;
    case ID_STRINGS:      return writestrings(f, ZELDA_VERSION, VERSION_BUILD, 0, MAXMSGS) == 0;
    case ID_DOORS:        return writedoorcombosets(f, &header) == 0;
    case ID_DMAPS:        return writedmaps(f, ZELDA_VERSION, VERSION_BUILD, 0, MAXDMAPS) == 0;
    case ID_MISC:         return writemisc(f, &header, &misc) == 0;
    case ID_COLORS:       return writemisccolors(f, &header, &misc) == 0;
    case ID_ICONS:        return writegameicons(f, &header, &misc) == 0;
    case ID_ITEMS:        return writeitems(f, &header) == 0;
    case ID_WEAPONS:      return writeweapons(f, &header) == 0;
    case ID_MAPS:         return writemaps(f, &header) == 0;
    case ID_COMBOS:       return writecombos(f, ZELDA_VERSION, VERSION_BUILD, 0, MAXCOMBOS) == 0;
    case ID_COMBOALIASES: return writecomboaliases(f, ZELDA_VERSION, VERSION_BUILD) == 0;
    case ID_CSETS:        return writecolordata(f, &misc, ZELDA_VERSION, VERSION_BUILD, 0, newerpdTOTAL) == 0;
    case ID_TILES:        return writetiles(f, ZELDA_VERSION, VERSION_BUILD, 0, NEWMAXTILES) == 0;
    case ID_MIDIS:        return writemidis(f) == 0;
    case ID_CHEATS:       return writecheats(f, &header) == 0;
    case ID_INITDATA:     return writeinitdata(f, &header) == 0;
    case ID_GUYS:         return writeguys(f, &header) == 0;
    case ID_LINKSPRITES:  return writelinksprites(f, &header) == 0;
    case ID_SUBSCREEN:    return writesubscreens(f, &header) == 0;
    case ID_FFSCRIPT:     return writeffscript(f, &header) == 0;
    case ID_SFX:          return writesfx(f, &header) == 0;
    case ID_ITEMDROPSETS: return writeitemdropsets(f, &header) == 0;
    case ID_FAVORITES:    return writefavorites(f, &header) == 0;
    }

    return false;
}

bool zq_part_is_raw(dword id)
{
    return is_raw(id);
}

PACKFILE *zq_open_sink(const PACKFILE_VTABLE *vtable, void *user)
{
    PACKFILE *f = pack_fopen_vtable(vtable, user);

    if(f)
        f->normal.flags |= PACKFILE_FLAG_WRITE;

    return f;
}

namespace
{

// What a pre-1.93 body holds, in the order 2.53 writes it, and what ZQuest could add that such
// a body can't hold (so a change to those is noticed and the writer can refuse it rather than
// the save dropping it). Editor favorites are left out: they were never part of that format.
const dword g_body_ids[] =
{
    ID_RULES, ID_STRINGS, ID_DOORS, ID_DMAPS, ID_MISC, ID_ITEMS, ID_WEAPONS, ID_GUYS, ID_MAPS,
    ID_COMBOS, ID_CSETS, ID_TILES, ID_MIDIS, ID_CHEATS, ID_INITDATA,
    ID_COLORS, ID_ICONS, ID_SUBSCREEN, ID_SFX, ID_LINKSPRITES, ID_ITEMDROPSETS, ID_COMBOALIASES, ID_FFSCRIPT
};

enum { FP_NONE, FP_OK, FP_FAILED };

// The open quest's misc section is older than version 3 and so also holds the colours and icons.
bool misc_holds_colors()
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
    {
        const ZqQuestPart &p = zq_quest_file.parts[i];
        byte v[2];

        if(p.id == ID_MISC)
            return zq_part_read(p, 0, v, 2) && (v[0] | (v[1] << 8)) < 3;
    }

    return false;
}

// FP_NONE: ZQuest has no 2.53 writer for this part, so nothing here can change it.
// FP_FAILED: a writer failed, so its output says nothing about whether the part changed.
int fingerprint(dword id, unsigned long long *out)
{
    bool has_writer = false;

    if(id == ID_HEADER || id == 0)
        has_writer = true;
    else
        for(size_t i = 0; i < sizeof(g_body_ids) / sizeof(g_body_ids[0]) && !has_writer; ++i)
            has_writer = g_body_ids[i] == id;

    if(!has_writer && id != ID_COMBOALIASES && id != ID_COLORS && id != ID_ICONS && id != ID_LINKSPRITES &&
       id != ID_SUBSCREEN && id != ID_FFSCRIPT && id != ID_SFX && id != ID_ITEMDROPSETS && id != ID_FAVORITES)
        return FP_NONE;

    HashSink sink;
    sink.h = 14695981039346656037ULL;
    PACKFILE *f = zq_open_sink(&g_hash_vtable, &sink);

    if(!f)
        return FP_FAILED;

    reset_combo_animations();       // as a save does, so animated combos show their own tiles
    reset_combo_animations2();
    bool ok = true;

    // More tiles than a 2.53 file holds (2.55 quests reach 214,500): the 2.53 tile writer
    // refuses them, so these are hashed tile by tile instead.
    if(id == ID_TILES && count_tiles(newtilebuf) > TILES_PER_PAGE * 252)
    {
        for(int t = 0; t < NEWMAXTILES; ++t)
        {
            p_putc(newtilebuf[t].format, f);

            if(newtilebuf[t].data)
                pfwrite(newtilebuf[t].data, tilesize(newtilebuf[t].format), f);
        }

        pack_fclose(f);
        *out = sink.h;
        return FP_OK;
    }

    if(id == 0)
    {
        for(size_t i = 0; i < sizeof(g_body_ids) / sizeof(g_body_ids[0]); ++i)
            ok = zq_write_253_section(f, g_body_ids[i]) && ok;
    }
    else if(id == ID_MISC && misc_holds_colors())
    {
        // Before misc version 3 the subscreen colours and game icons live in the misc section
        // (2.10 files have no MCLR or ICON section), so a change to them changes this part.
        ok = zq_write_253_section(f, ID_MISC);
        ok = zq_write_253_section(f, ID_COLORS) && ok;
        ok = zq_write_253_section(f, ID_ICONS) && ok;
    }
    else
        ok = zq_write_253_section(f, id);

    pack_fclose(f);
    *out = sink.h;
    return ok ? FP_OK : FP_FAILED;
}

} // namespace

void zq_writers_note_loaded();     // zq_writers.cpp: the writer areas' own copies of records

namespace
{

struct Absent
{
    dword id;
    int fp_state;
    unsigned long long fp;
};

std::vector<Absent> g_absent;

const dword g_absent_candidates[] =
{
    ID_RULES, ID_STRINGS, ID_MISC, ID_TILES, ID_COMBOS, ID_COMBOALIASES, ID_CSETS, ID_MAPS, ID_DMAPS,
    ID_DOORS, ID_ITEMS, ID_WEAPONS, ID_COLORS, ID_ICONS, ID_INITDATA, ID_GUYS, ID_MIDIS, ID_CHEATS,
    ID_LINKSPRITES, ID_SUBSCREEN, ID_ITEMDROPSETS, ID_FFSCRIPT, ID_SFX
};

const char *absent_name(dword id)
{
    switch(id)
    {
    case ID_LINKSPRITES: return "Link's sprites";
    case ID_SUBSCREEN: return "subscreens";
    case ID_ITEMDROPSETS: return "item drop sets";
    case ID_FFSCRIPT: return "scripts";
    case ID_SFX: return "sound effects";
    case ID_COLORS: return "misc colors";
    case ID_ICONS: return "game icons";
    case ID_COMBOALIASES: return "combo aliases";
    case ID_CHEATS: return "cheat codes";
    case ID_DOORS: return "door combo sets";
    case ID_MIDIS: return "tunes";
    }

    return "a section";
}

void note_absent()
{
    g_absent.clear();

    // a pre-1.93 body covers all of these itself (g_body_ids)
    if(zq_quest_file.parts.empty() || zq_quest_file.zelda_version < 0x193)
        return;

    for(size_t i = 0; i < sizeof(g_absent_candidates) / sizeof(g_absent_candidates[0]); ++i)
    {
        dword id = g_absent_candidates[i];
        bool present = false;

        for(size_t k = 0; k < zq_quest_file.parts.size() && !present; ++k)
            present = zq_quest_file.parts[k].id == id;

        // an old misc section carries the colours and icons itself
        if(present || ((id == ID_COLORS || id == ID_ICONS) && misc_holds_colors()))
            continue;

        Absent a;
        a.id = id;
        a.fp = 0;
        a.fp_state = fingerprint(id, &a.fp);
        g_absent.push_back(a);
    }
}

} // namespace

void zq_absent_changed(std::vector<std::string> &names)
{
    for(size_t i = 0; i < g_absent.size(); ++i)
    {
        unsigned long long now = 0;
        int r = fingerprint(g_absent[i].id, &now);

        if(r != g_absent[i].fp_state || now != g_absent[i].fp)
            names.push_back(std::string(absent_name(g_absent[i].id)) + " (this version's file has no place for them)");
    }
}

void zq_sections_note_loaded()
{
    zq_writers_note_loaded();

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
    {
        ZqQuestPart &p = zq_quest_file.parts[i];
        int r = p.id == ID_DAMAGED ? FP_NONE : fingerprint(p.id, &p.fingerprint);
        p.has_fingerprint = r != FP_NONE;
        p.fingerprint_failed = r == FP_FAILED;
    }

    note_absent();
}

bool zq_part_changed(const ZqQuestPart &p)
{
    if(!p.has_fingerprint)
        return false;

    if(p.fingerprint_failed)        // can't tell: treat it as changed, never copy old bytes over edits
        return true;

    unsigned long long now = 0;
    return fingerprint(p.id, &now) != FP_OK || now != p.fingerprint;
}

namespace
{

// Compares the next n bytes of the decoded file with data. pos counts the file's bytes,
// diff is set at the first difference (or where the file ran out).
void compare_next(PACKFILE *f, const byte *data, size_t n, long long &pos, long long &diff)
{
    byte buf[65536];

    while(n > 0)
    {
        size_t want = n < sizeof(buf) ? n : sizeof(buf);
        long got = pack_fread(buf, (long)want, f);

        for(long i = 0; diff < 0 && i < got; ++i)
            if(buf[i] != data[i])
                diff = pos + i;

        if(got < (long)want && diff < 0)
            diff = pos + (got > 0 ? got : 0);

        pos += got > 0 ? got : 0;

        if(got < (long)want)
            return;

        data += want;
        n -= want;
    }
}

} // namespace

int zq_sections_check(const char *filename, const char *report)
{
    int open_error = 0;
    char deletefilename[1024];
    deletefilename[0] = 0;
    PACKFILE *f = open_quest_file(&open_error, filename, deletefilename, true, true, false);
    long long pos = 0, diff = -1, parts_total = 0;
    bool read_ok = true;

    if(f)
    {
        std::vector<byte> chunk(1 << 20);

        for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        {
            const ZqQuestPart &p = zq_quest_file.parts[i];

            if(!is_raw(p.id))
            {
                byte id[4] = { (byte)(p.id >> 24), (byte)(p.id >> 16), (byte)(p.id >> 8), (byte)p.id };
                compare_next(f, id, 4, pos, diff);
                parts_total += 4;
            }

            for(long long at = 0; at < p.length; at += (long long)chunk.size())
            {
                size_t n = (size_t)(p.length - at < (long long)chunk.size() ? p.length - at : (long long)chunk.size());

                if(!zq_part_read(p, at, &chunk[0], n))
                {
                    read_ok = false;
                    break;
                }

                compare_next(f, &chunk[0], n, pos, diff);
            }

            parts_total += p.length;
        }

        // anything left in the file wasn't kept
        long got;

        while((got = pack_fread(&chunk[0], (long)chunk.size(), f)) > 0)
        {
            if(diff < 0)
                diff = pos;
            pos += got;
        }

        pack_fclose(f);
    }

    if(deletefilename[0])
        delete_file(deletefilename);

    bool identical = f && read_ok && diff < 0 && pos == parts_total;
    FILE *out = fopen(report, "w");

    if(out)
    {
        fprintf(out, "%s: file %lld bytes, parts %lld bytes in %lu parts\n", identical ? "IDENTICAL" : "DIFFERENT",
                pos, parts_total, (unsigned long)zq_quest_file.parts.size());

        if(!identical)
            fprintf(out, "first difference at byte %lld%s\n", diff, read_ok ? "" : " (the kept copy couldn't be read)");

        if(zq_quest_file.inexact)
            fprintf(out, "the loader searched past damaged data\n");

        // Nothing was edited, so every part must still match its fingerprint.
        int unstable = 0;

        std::string which;

        for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
            if(zq_part_changed(zq_quest_file.parts[i]))
            {
                ++unstable;
                char tag[16];
                const ZqQuestPart &p = zq_quest_file.parts[i];
                sprintf(tag, " %c%c%c%c%s", (char)(p.id >> 24), (char)(p.id >> 16), (char)(p.id >> 8), (char)p.id,
                        p.fingerprint_failed ? "(writer failed)" : "");
                which += p.id == ID_HEADER ? " HDR" : (p.id == 0 ? " BODY" : tag);
            }

        fprintf(out, "parts that look changed right after the load: %d%s\n", unstable, which.c_str());

        long long at = 0;

        for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        {
            const ZqQuestPart &p = zq_quest_file.parts[i];
            size_t idlen = is_raw(p.id) ? 0 : 4;
            char name[5] = "????";

            if(p.id == ID_HEADER)
                strcpy(name, "HDR ");
            else if(p.id == 0)
                strcpy(name, "BODY");
            else if(p.id == ID_TAIL)
                strcpy(name, "TAIL");
            else if(p.id != ID_DAMAGED)
                for(int k = 0; k < 4; ++k)
                {
                    char c = (char)(p.id >> (24 - 8 * k));
                    name[k] = (c >= 0x20 && c < 0x7F) ? c : '?';
                }

            byte vb[4] = { 0, 0, 0, 0 };
            bool has_version = !is_raw(p.id) && zq_part_read(p, 0, vb, 4);

            if(has_version)
                fprintf(out, "%s at %10lld  %10lld bytes  v %d cv %d\n", name, at, (long long)idlen + p.length,
                        vb[0] | (vb[1] << 8), vb[2] | (vb[3] << 8));
            else
                fprintf(out, "%s at %10lld  %10lld bytes\n", name, at, (long long)idlen + p.length);
            at += (long long)idlen + p.length;
        }

        fclose(out);
    }

    return identical ? 0 : 1;
}

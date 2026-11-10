// Saving a quest older than 1.93 (1.90 and the 1.92 builds before sections) in its own
// format. Such a file is a header and one flat body with no sections; ZQuest keeps the two
// as parts (zq_sections.h: the header, and the body as part 0), and these writers write
// them again.
//
// The loader converts a lot as it reads these quests: rules are switched on for the old
// behaviour, warp types renumbered, enemy numbers moved, combos given their page, palettes
// moved, and most of the item, enemy and init data filled in from defaults and rules. Writing
// from ZQuest's data alone can't give the original bytes back, so the body writer walks the
// original body record by record, mirroring the readers in qst.cpp, copies every record
// nobody changed, and encodes only the changed ones, undoing the load conversions. Bytes the
// readers skip or throw away come from the original record. A field the old format can't
// hold, or holds only as something the loader works out for itself, must still be what the
// load made of it: if it isn't, the writer refuses rather than drop the change.
//
// The header holds more than its own fields: the rules, the map count, the string count of
// a 1.90 quest, which tunes exist, and whether tiles and cheat codes follow. Both writers work
// those out from ZQuest's data the same way, and the body writer checks that the header in
// the file says what the body needs.
//
// Supported: 1.90, and 1.92 builds 152, 163, 182 and 183 (every pre-1.93 build in the test
// pack). Other builds are left to nobody (ZQW_UNSUPPORTED), since their branches are untested.

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "colors.h"
#include "tiles.h"
#include "sfx.h"
#include "md5.h"
#include "zq_writers.h"

extern zquestheader header;
extern miscQdata misc;
extern byte quest_rules[QUESTRULES_SIZE];
extern byte midi_flags[MIDIFLAGS_SIZE];
extern word map_count;
extern std::vector<mapscr> TheMaps;
extern dmap *DMaps;
extern MsgStr *MsgStrings;
extern int msg_strings_size;
extern word msg_count;
extern DoorComboSet *DoorComboSets;
extern word door_combo_set_count;
extern zctune *customtunes;
extern ZCHEATS zcheats;
extern zinitdata zinit;
extern char palnames[MAXLEVELS][17];
extern newcombo *combobuf;
extern tiledata *newtilebuf;
extern itemdata *itemsbuf;
extern wpndata *wpnsbuf;
extern guydata *guysbuf;
extern char *item_string[ITEMCNT];
extern char *weapon_string[WPNCNT];
extern char *guy_string[eMAXGUYS];

// ZQuest's 2.53 writers (zq_class.cpp), used here only to notice changes to data a pre-1.93
// quest has no room for
int writesubscreens(PACKFILE *f, zquestheader *Header);
int writelinksprites(PACKFILE *f, zquestheader *Header);
int writesfx(PACKFILE *f, zquestheader *Header);
int writeitemdropsets(PACKFILE *f, zquestheader *Header);
int writecomboaliases(PACKFILE *f, word version, word build);
int writeffscript(PACKFILE *f, zquestheader *Header);
int writefavorites(PACKFILE *f, zquestheader *Header);

namespace
{

// ---- small tools ----------------------------------------------------------------------

// ZQW_FLAT_LOG=<file>: why a writer refused, and (with ZQW_FLAT_SELFTEST) every record whose
// encoding differs from its original bytes. For tracking down problems.
void flat_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    al_trace("zqw_flat: %s\n", buf);
    const char *path = getenv("ZQW_FLAT_LOG");

    if(path && *path)
    {
        FILE *f = fopen(path, "a");

        if(f)
        {
            fprintf(f, "%s\n", buf);
            fclose(f);
        }
    }
}

// A refusal: logged as above, and kept as the reason the save shows the user.
void flat_refuse(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    zqw_last_error = buf;
    flat_log("%s", buf);
}

// A cursor over original bytes. Reading past the end sets bad and gives zeros.
struct In
{
    const byte *p;
    size_t n, at;
    bool bad;

    In(const byte *p_, size_t n_) : p(p_), n(n_), at(0), bad(false) {}

    const byte *take(size_t k)
    {
        static const byte zeros[65536] = { 0 };

        if(bad || at + k > n)
        {
            bad = true;
            return zeros;
        }

        const byte *r = p + at;
        at += k;
        return r;
    }

    int c() { return take(1)[0]; }
    int w() { const byte *r = take(2); return r[0] | (r[1] << 8); }
    dword l() { const byte *r = take(4); return r[0] | (r[1] << 8) | (r[2] << 16) | ((dword)r[3] << 24); }
    dword ml() { const byte *r = take(4); return ((dword)r[0] << 24) | (r[1] << 16) | (r[2] << 8) | r[3]; }
};

struct Out
{
    std::vector<byte> b;

    void c(int v) { b.push_back((byte)v); }
    void w(int v) { c(v); c(v >> 8); }
    void l(dword v) { c(v); c(v >> 8); c(v >> 16); c(v >> 24); }
    void mw(int v) { c(v >> 8); c(v); }
    void ml(dword v) { c(v >> 24); c(v >> 16); c(v >> 8); c(v); }
    void put(const void *p, size_t n) { const byte *s = (const byte *)p; b.insert(b.end(), s, s + n); }
};

// A record's original bytes, or zeros for a record the file didn't have.
struct Raw
{
    std::vector<byte> zeros;
    const byte *p;
    size_t n;

    Raw(const byte *p_, size_t n_) : p(p_), n(n_)
    {
        if(!p)
        {
            zeros.assign(n ? n : 1, 0);
            p = &zeros[0];
        }
    }
};

// FNV-1a over what a writer writes, to notice changes cheaply
struct HashSink
{
    unsigned long long h;
};

void hs_add(HashSink *s, unsigned char b)
{
    s->h ^= b;
    s->h *= 1099511628211ULL;
}

int hs_fclose(void *) { return 0; }
int hs_getc(void *) { return EOF; }
int hs_ungetc(int, void *) { return EOF; }
long hs_fread(void *, long, void *) { return 0; }
int hs_putc(int c, void *u) { hs_add((HashSink *)u, (unsigned char)c); return c & 0xFF; }
long hs_fwrite(const void *p, long n, void *u)
{
    for(long i = 0; i < n; ++i)
        hs_add((HashSink *)u, ((const unsigned char *)p)[i]);

    return n;
}
int hs_fseek(void *, int) { return -1; }
int hs_feof(void *) { return 1; }
int hs_ferror(void *) { return 0; }
const PACKFILE_VTABLE g_hash_vtable = { hs_fclose, hs_getc, hs_ungetc, hs_fread, hs_putc, hs_fwrite, hs_fseek, hs_feof, hs_ferror };

unsigned long long hash_bytes(const void *p, size_t n, unsigned long long h = 14695981039346656037ULL)
{
    for(size_t i = 0; i < n; ++i)
    {
        h ^= ((const unsigned char *)p)[i];
        h *= 1099511628211ULL;
    }

    return h;
}

// A read-only file over bytes in memory, to run qst.cpp's own readers on what is written.
struct MemIn
{
    const byte *p;
    long n, at;
};

int mi_fclose(void *) { return 0; }
int mi_getc(void *u) { MemIn *m = (MemIn *)u; return m->at < m->n ? m->p[m->at++] : EOF; }
int mi_ungetc(int c, void *u) { MemIn *m = (MemIn *)u; if(m->at > 0) { --m->at; return c & 0xFF; } return EOF; }
long mi_fread(void *buf, long n, void *u)
{
    MemIn *m = (MemIn *)u;
    long k = m->n - m->at < n ? m->n - m->at : n;

    if(k > 0)
    {
        memcpy(buf, m->p + m->at, k);
        m->at += k;
    }

    return k > 0 ? k : 0;
}
int mi_putc(int, void *) { return EOF; }
long mi_fwrite(const void *, long, void *) { return 0; }
int mi_fseek(void *u, int off)
{
    MemIn *m = (MemIn *)u;

    if(off < 0 || m->at + off > m->n)
    {
        m->at = m->n;
        return -1;
    }

    m->at += off;
    return 0;
}
int mi_feof(void *u) { MemIn *m = (MemIn *)u; return m->at >= m->n; }
int mi_ferror(void *) { return 0; }
const PACKFILE_VTABLE g_memin_vtable = { mi_fclose, mi_getc, mi_ungetc, mi_fread, mi_putc, mi_fwrite, mi_fseek, mi_feof, mi_ferror };

// ---- the versions ---------------------------------------------------------------------

bool supported(word v, byte b)
{
    return v == 0x190 || (v == 0x192 && (b == 152 || b == 163 || b == 182 || b == 183));
}

// The fields of a pre-1.93 header that say how the body is read, as readheader reads them.
struct HdrInfo
{
    byte rules[QUESTRULES_SIZE];
    byte map_count;
    byte str_count;
    byte tiles;
    byte cheats;
    byte midi[MIDIFLAGS_SIZE];      // which tunes follow, as readtunes sees it
};

// Offsets in a pre-1.93 header (readheader's first branch)
enum
{
    oh_id = 0, oh_version = 32, oh_internal = 34, oh_questnum = 36, oh_rules01 = 37, oh_mapcount = 39,
    oh_strcount = 40, oh_tiles = 41, oh_midi4 = 42, oh_cheats = 46, oh_rules23 = 61, oh_ver = 64,
    oh_title = 73, oh_author = 138, oh_pwdkey = 204, oh_pwd = 206, oh_minver = 236, oh_build = 245,
    oh_keyfile = 246, oh_rules4 = 256, oh_midi32 = 272, oh_template = 322
};

size_t header_size(word v, byte b)
{
    size_t n = 236;

    if(v >= 0x177)
        n += 20;
    if(v >= 0x187)
        n += 16;
    if(v >= 0x192)
        n += 50;
    if(v == 0x192 && b >= 145)
        n += 280;

    return n;
}

void header_info(const std::vector<byte> &h, word v, byte b, HdrInfo &out)
{
    memset(&out, 0, sizeof(out));
    memcpy(out.rules, &h[oh_rules01], 2);
    memcpy(out.rules + 2, &h[oh_rules23], 2);

    if(v >= 0x187)
        memcpy(out.rules + 4, &h[oh_rules4], 16);

    out.map_count = h[oh_mapcount];
    out.str_count = h[oh_strcount];
    out.tiles = h[oh_tiles];
    out.cheats = h[oh_cheats];
    memcpy(out.midi, &h[oh_midi4], 4);

    if(v == 0x192 && b >= 178)
        memcpy(out.midi, &h[oh_midi32], 32);
}

// ---- the snapshot: ZQuest's data as loaded (or as last saved) ---------------------------

struct Snap
{
    bool valid;
    word v;
    byte b;
    std::vector<byte> hdr;              // the header part's bytes
    HdrInfo h;
    zquestheader header;
    byte rules[QUESTRULES_SIZE];
    word map_count, msg_count;
    std::vector<MsgStr> strings;
    word door_count;
    std::vector<DoorComboSet> doors;
    std::vector<dmap> dmaps;
    miscQdata misc;
    std::vector<itemdata> items;
    std::vector<char> item_names;
    std::vector<wpndata> weapons;
    std::vector<char> weapon_names;
    std::vector<guydata> guys;
    std::vector<char> guy_names;
    std::vector<mapscr> screens;
    std::vector<newcombo> combos;
    std::vector<byte> colors;
    std::vector<char> palnames;
    std::vector<unsigned long long> tiles;
    std::vector<unsigned long long> tunes;
    ZCHEATS cheats;
    zinitdata init;
    unsigned long long others;

    Snap() : valid(false) {}
};

Snap g_snap;

unsigned long long tile_hash(int t)
{
    const tiledata &td = newtilebuf[t];
    unsigned long long h = hash_bytes(&td.format, 1);
    return td.data ? hash_bytes(td.data, tilesize(td.format), h) : h;
}

unsigned long long tune_hash(int i)
{
    const zctune &t = customtunes[i];
    unsigned long long h = hash_bytes(t.title, sizeof(t.title));
    h = hash_bytes(&t.start, sizeof(t.start), h);
    h = hash_bytes(&t.loop_start, sizeof(t.loop_start), h);
    h = hash_bytes(&t.loop_end, sizeof(t.loop_end), h);
    h = hash_bytes(&t.loop, sizeof(t.loop), h);
    h = hash_bytes(&t.volume, sizeof(t.volume), h);
    h = hash_bytes(&t.flags, sizeof(t.flags), h);
    h = hash_bytes(&t.format, sizeof(t.format), h);
    byte present = t.data != NULL;
    h = hash_bytes(&present, 1, h);

    if(t.data && t.format == MFORMAT_MIDI)
    {
        const MIDI *m = (const MIDI *)t.data;
        h = hash_bytes(&m->divisions, sizeof(m->divisions), h);

        for(int k = 0; k < MIDI_TRACKS; ++k)
        {
            h = hash_bytes(&m->track[k].len, sizeof(m->track[k].len), h);

            if(m->track[k].len > 0 && m->track[k].data)
                h = hash_bytes(m->track[k].data, m->track[k].len, h);
        }
    }

    return h;
}

// What ZQuest's 2.53 writers write for data a pre-1.93 quest has nowhere to put.
unsigned long long others_hash()
{
    HashSink sink;
    sink.h = 14695981039346656037ULL;
    PACKFILE *f = zq_open_sink(&g_hash_vtable, &sink);

    if(!f)
        return 0;

    writesubscreens(f, &header);
    writelinksprites(f, &header);
    writesfx(f, &header);
    writeitemdropsets(f, &header);
    writecomboaliases(f, ZELDA_VERSION, VERSION_BUILD);
    writeffscript(f, &header);
    writefavorites(f, &header);
    pack_fclose(f);
    return sink.h;
}

const ZqQuestPart *find_part(dword id)
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == id)
            return &zq_quest_file.parts[i];

    return NULL;
}

bool read_part(const ZqQuestPart &p, std::vector<byte> &out)
{
    out.resize((size_t)p.length);
    return p.length == 0 || zq_part_read(p, 0, &out[0], (size_t)p.length);
}

void flat_note_loaded()
{
    Snap &s = g_snap;
    s = Snap();
    word v = zq_quest_file.zelda_version;
    byte b = zq_quest_file.build;
    const ZqQuestPart *hp = find_part(ID_HEADER);

    if(v >= 0x193 || !supported(v, b) || !hp || !find_part(0) || !read_part(*hp, s.hdr) || s.hdr.size() != header_size(v, b))
    {
        s.hdr.clear();
        return;
    }

    s.v = v;
    s.b = b;
    header_info(s.hdr, v, b, s.h);

    if(getenv("ZQW_FLAT_TRACE"))    // ZQW_FLAT_TRACE: the parts as kept, and each area's end as the writer walks it
        for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
            flat_log("loaded part %d: id 0x%08X, %lld bytes", (int)i, (unsigned)zq_quest_file.parts[i].id, zq_quest_file.parts[i].length);

    reset_combo_animations();       // as a save does, so animated combos show their own tiles
    reset_combo_animations2();

    // The fingerprints that follow run ZQuest's 2.53 writers, and some of those tidy the data
    // as they go (writeguys gives enemies without a hit sound the default one). Run them first,
    // so the copy here is the data as it stands from now on.
    {
        HashSink sink;
        sink.h = 0;
        PACKFILE *f = zq_open_sink(&g_hash_vtable, &sink);

        if(f)
        {
            static const dword ids[] = { ID_HEADER, ID_RULES, ID_STRINGS, ID_DOORS, ID_DMAPS, ID_MISC, ID_ITEMS,
                                         ID_WEAPONS, ID_GUYS, ID_MAPS, ID_COMBOS, ID_CSETS, ID_TILES, ID_MIDIS,
                                         ID_CHEATS, ID_INITDATA, ID_COLORS, ID_ICONS };

            for(size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
                zq_write_253_section(f, ids[i]);

            pack_fclose(f);
        }

        reset_combo_animations();
        reset_combo_animations2();
    }

    s.others = others_hash();
    s.header = header;
    memcpy(s.rules, quest_rules, QUESTRULES_SIZE);
    s.map_count = map_count;
    s.msg_count = msg_count;
    s.strings.assign(MsgStrings, MsgStrings + msg_strings_size);
    s.door_count = door_combo_set_count;
    s.doors.assign(DoorComboSets, DoorComboSets + MAXDOORCOMBOSETS);
    s.dmaps.assign(DMaps, DMaps + MAXDMAPS);
    s.misc = misc;
    s.items.assign(itemsbuf, itemsbuf + MAXITEMS);
    s.item_names.resize(ITEMCNT * 64);

    for(int i = 0; i < ITEMCNT; ++i)
        memcpy(&s.item_names[i * 64], item_string[i], 64);

    s.weapons.assign(wpnsbuf, wpnsbuf + MAXWPNS);
    s.weapon_names.resize(WPNCNT * 64);

    for(int i = 0; i < WPNCNT; ++i)
        memcpy(&s.weapon_names[i * 64], weapon_string[i], 64);

    s.guys.assign(guysbuf, guysbuf + MAXGUYS);
    s.guy_names.resize(eMAXGUYS * 64);

    for(int i = 0; i < eMAXGUYS; ++i)
        memcpy(&s.guy_names[i * 64], guy_string[i], 64);

    s.screens = TheMaps;
    s.combos.assign(combobuf, combobuf + MAXCOMBOS);
    s.colors.assign(colordata, colordata + newerpsTOTAL);
    s.palnames.assign(&palnames[0][0], &palnames[0][0] + MAXLEVELS * 17);
    s.tiles.resize(NEWMAXTILES);

    for(int t = 0; t < NEWMAXTILES; ++t)
        s.tiles[t] = tile_hash(t);

    s.tunes.resize(MAXCUSTOMTUNES);

    for(int i = 0; i < MAXCUSTOMTUNES; ++i)
        s.tunes[i] = tune_hash(i);

    s.cheats = zcheats;
    s.init = zinit;
    s.valid = true;
}

// ---- writing: the state of one write --------------------------------------------------

struct Ctx
{
    word v;
    byte b;
    bool force;
    bool selftest;
    HdrInfo h0;         // how the original body was read
    HdrInfo h1;         // how the body written now will be read
    std::string why;

    // before 1.92 build b0: (version < 0x192) || (version == 0x192 && build < b0)
    bool bf(int b0) const { return v < 0x192 || (v == 0x192 && b < b0); }
};

bool fail(Ctx &c, const char *fmt, ...)
{
    if(c.why.empty())
    {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        buf[sizeof(buf) - 1] = 0;
        c.why = buf;
    }

    return false;
}

// Under ZQW_FLAT_SELFTEST, unchanged records are encoded too and compared with their original
// bytes (the original bytes are still what is written).
void selftest_note(Ctx &c, const char *what, int index, const Raw &raw, const Out &o, size_t start)
{
    if(!raw.p || raw.zeros.size())
        return;

    size_t n = o.b.size() - start;

    if(n != raw.n || memcmp(&o.b[start], raw.p, n))
    {
        size_t k = 0;

        while(k < n && k < raw.n && o.b[start + k] == raw.p[k])
            ++k;

        flat_log("selftest %s %d: encoded %d bytes of %d, first difference at %d", what, index, (int)n, (int)raw.n, (int)k);
    }
}

// ---- rules ------------------------------------------------------------------------------

// What the loader makes of the rules a pre-1.93 header holds (readheader, readrules with no
// section version, readinitdata and loadquest's own clean-up).
void rules_forward(const byte *S, byte *Q, word v, byte b)
{
    memcpy(Q, S, QUESTRULES_SIZE);
    bool before149 = v < 0x192 || (v == 0x192 && b < 149);
    bool before129 = v < 0x192 || (v == 0x192 && b < 129);
    bool before168 = v < 0x192 || (v == 0x192 && b < 168);

    // readheader
    if(v >= 0x187 && v <= 0x190)
        set_bit(Q, qr_MEANPLACEDTRAPS, 0);

    if(before149)
        set_bit(Q, qr_BRKNSHLDTILES, get_bit(Q, qr_BRKBLSHLDS_DEP));

    // readrules: s_version 0
    static const int cleared[] = { 14, 27, 28, 29, 30, 32, 36, 49, 50, 51, 68, 75, 76, 98, 110, 113, 116, 102, 132 };

    for(size_t i = 0; i < sizeof(cleared) / sizeof(cleared[0]); ++i)
        set_bit(Q, cleared[i], 0);

    if(v < 0x211)
    {
        set_bit(Q, qr_SMOOTHVERTICALSCROLLING, 1);
        set_bit(Q, qr_REPLACEOPENDOORS, 1);
        set_bit(Q, qr_OLDLENSORDER, 1);
        set_bit(Q, qr_NOFAIRYGUYFIRES, 1);
        set_bit(Q, qr_TRIGGERSREPEAT, 1);
    }

    set_bit(Q, qr_WALLFLIERS, 1);
    set_bit(Q, qr_NOBOMBPALFLASH, 1);
    set_bit(Q, qr_NOSCROLLCONTINUE, 1);
    set_bit(Q, qr_OLDSTYLEWARP, 1);

    if(v < 0x210)
        set_bit(Q, qr_OLDHOOKSHOTGRAB, 1);

    if(v < 0x211)
    {
        set_bit(Q, qr_OLDPICKUP, 1);
        set_bit(Q, qr_NOSOLIDDAMAGECOMBOS, 1);
        set_bit(Q, qr_ITEMPICKUPSETSBELOW, 1);
    }

    set_bit(Q, qr_HOOKSHOTDOWNBUG, 1);
    set_bit(Q, qr_OFFSCREENWEAPONS, 1);
    set_bit(Q, qr_OFFSETEWPNCOLLISIONFIX, 1);
    set_bit(Q, qr_HOLDNOSTOPMUSIC, 1);
    set_bit(Q, qr_CAVEEXITNOSTOPMUSIC, 1);
    set_bit(Q, 10, 0);
    set_bit(Q, 27, 0);
    set_bit(Q, 46, 0);
    set_bit(Q, qr_HEARTSREQUIREDFIX, 0);
    set_bit(Q, qr_PUSHBLOCKCSETFIX, 1);
    set_bit(Q, 12, 0);
    set_bit(Q, qr_NOROPE2FLASH_DEP, 0);
    set_bit(Q, qr_NOBUBBLEFLASH_DEP, 0);
    set_bit(Q, qr_GHINI2BLINK_DEP, 0);
    set_bit(Q, qr_PHANTOMGHINI2_DEP, 0);
    set_bit(Q, qr_NOCLOCKS_DEP, 0);
    set_bit(Q, qr_ALLOW10RUPEEDROPS_DEP, 0);
    set_bit(Q, qr_SLOWENEMYANIM_DEP, 0);
    set_bit(Q, qr_BRKBLSHLDS_DEP, 0);
    set_bit(Q, qr_OLDTRIBBLES_DEP, 0);
    set_bit(Q, qr_SHOPCHEAT, 1);
    set_bit(Q, qr_BITMAPOFFSETFIX, 1);
    set_bit(Q, qr_OLDSIDEVIEWSPIKES, 1);
    set_bit(Q, qr_NOGANONINTRO, 0);

    // readinitdata moves these into the init and item data
    if(before129)
    {
        for(int i = 0; i < idBP_MAX; ++i)
            set_bit(Q, qr_LENSHINTS + i, 0);

        set_bit(Q, qr_HIDECARRIEDITEMS, 0);
    }

    if(before168)
        set_bit(Q, qr_FREEFORM, 0);

    // loadquest
    set_bit(Q, qr_CONTFULL_DEP, 0);
}

// The stored rules that load as Q, starting from the stored rules raw: every bit the loader
// keeps as it is comes from Q, every bit it overwrites stays as it was in the file.
bool rules_inverse(const byte *raw, const byte *Q, byte *S, word v, byte b)
{
    memcpy(S, raw, QUESTRULES_SIZE);
    byte T[QUESTRULES_SIZE];
    rules_forward(S, T, v, b);

    for(int bit = 0; bit < QUESTRULES_SIZE * 8; ++bit)
    {
        if(get_bit(T, bit) == get_bit((byte *)Q, bit))
            continue;

        bool fixed = false;

        for(int k = -1; k < QUESTRULES_SIZE * 8 && !fixed; ++k)
        {
            int src = k < 0 ? bit : k;

            if(k == bit)
                continue;

            byte S2[QUESTRULES_SIZE], T2[QUESTRULES_SIZE];
            memcpy(S2, S, QUESTRULES_SIZE);
            set_bit(S2, src, !get_bit(S2, src));
            rules_forward(S2, T2, v, b);
            bool ok = get_bit(T2, bit) == get_bit((byte *)Q, bit);

            for(int d = 0; d < QUESTRULES_SIZE * 8 && ok; ++d)
                if(d != bit && get_bit(T2, d) != get_bit(T, d) && get_bit(T, d) == get_bit((byte *)Q, d))
                    ok = false;

            if(ok)
            {
                memcpy(S, S2, QUESTRULES_SIZE);
                memcpy(T, T2, QUESTRULES_SIZE);
                fixed = true;
            }
        }

        if(!fixed)
            return false;
    }

    return true;
}

// ---- the header ---------------------------------------------------------------------------

void decrypt_pwd(const byte *enc, short key, char *out)
{
    char tmp[30];
    memset(tmp, 0, 30);

    if(key != 0)
    {
        memcpy(tmp, enc, 30);
        tmp[29] = 0;

        for(int i = 0; i < 30; ++i)
        {
            tmp[i] -= key;
            int t = key >> 15;
            key = (short)((key << 1) + t);
        }
    }

    memcpy(out, tmp, 30);
}

void md5_of(const char *s, size_t n, unsigned char *out)
{
    cvs_MD5Context ctx;
    cvs_MD5Init(&ctx);
    cvs_MD5Update(&ctx, (const unsigned char *)s, (unsigned)n);
    cvs_MD5Final(out, &ctx);
}

bool pwd_matches(const byte *enc, short key)
{
    char plain[31];
    decrypt_pwd(enc, key, plain);
    plain[30] = 0;
    unsigned char md5[16];
    md5_of(plain, strlen(plain), md5);
    return memcmp(md5, header.pwd_hash, 16) == 0;
}

bool tiles_changed()
{
    for(int t = 0; t < NEWMAXTILES; ++t)
        if(tile_hash(t) != g_snap.tiles[t])
            return true;

    return false;
}

bool cheats_empty()
{
    ZCHEATS z;
    memset(&z, 0, sizeof(z));
    return zcheats.flags == 0 && !memcmp(zcheats.codes, z.codes, sizeof(z.codes));
}

// The header as it must be written now, from ZQuest's data and the header as loaded.
bool compute_header(Ctx &c, std::vector<byte> &h)
{
    const Snap &s = g_snap;
    h = s.hdr;
    word v = c.v;
    byte b = c.b;

    if(strncmp(header.id_str, QH_IDSTR, sizeof(QH_IDSTR)))      // as readheader compares it
        return fail(c, "the header: its id string isn't the pre-1.93 one");

    memcpy(&h[oh_id], header.id_str, 31);
    h[oh_version] = v & 0xFF;
    h[oh_version + 1] = v >> 8;
    h[oh_internal] = header.internal & 0xFF;
    h[oh_internal + 1] = header.internal >> 8;
    h[oh_questnum] = header.quest_number;

    // rules
    byte stored[QUESTRULES_SIZE];

    if(!rules_inverse(s.h.rules, quest_rules, stored, v, b))
        return fail(c, "the rules: a rule is set that the loader of this version always overrides");

    if(c.bf(168))
    {
        if(zinit.subscreen > 1)
            return fail(c, "the init data: this version only has the original and the new subscreen");

        set_bit(stored, qr_FREEFORM, zinit.subscreen ? 1 : 0);
    }

    if(get_bit(stored, qr_CONTFULL_DEP) &&
       !(get_bit(zinit.misc, idM_CONTPERCENT) && zinit.cont_heart == 100 && zinit.start_heart == zinit.hc))
    {
        if(c.bf(27))
            return fail(c, "the init data: continue hearts are set by a rule in this version");

        set_bit(stored, qr_CONTFULL_DEP, 0);
    }

    memcpy(&h[oh_rules01], stored, 2);
    memcpy(&h[oh_rules23], stored + 2, 2);

    if(v >= 0x187)
        memcpy(&h[oh_rules4], stored + 4, 16);

    if(map_count > 255)
        return fail(c, "the maps: this version holds at most 255 maps");

    h[oh_mapcount] = (byte)map_count;

    // a 1.90 quest's string count
    if(c.bf(31))
    {
        if(msg_count > 128)
            return fail(c, "the strings: this version holds at most 128 strings");

        byte raw = s.h.str_count;
        int raw_count = (signed char)raw < 0 ? 128 : (raw > 128 ? 128 : raw);

        if(raw_count != msg_count)
            h[oh_strcount] = (byte)msg_count;
    }
    else if(c.v == 0x192 && c.b < 140)
        return fail(c, "the strings: build not supported");

    // which data follow
    for(int i = 0; i < ZQ_MAXDATA; ++i)
        if(i != ZQ_TILES && i != ZQ_CHEATS2 && header.data_flags[i])
            return fail(c, "the header: a data flag is set that this version doesn't have");

    if(header.data_flags[ZQ_CHEATS2] != 1)
        return fail(c, "the cheats: this version always allows cheats");

    h[oh_tiles] = (s.h.tiles || header.data_flags[ZQ_TILES] || tiles_changed()) ? (s.h.tiles ? s.h.tiles : 1) : 0;
    h[oh_cheats] = (s.h.cheats || !cheats_empty()) ? (s.h.cheats ? s.h.cheats : 1) : 0;

    if(v == 0x192 && b >= 178)
        memcpy(&h[oh_midi32], midi_flags, 32);
    else
    {
        for(int i = 4; i < MIDIFLAGS_SIZE; ++i)
            if(midi_flags[i])
                return fail(c, "the tunes: this version holds only 32 tunes");

        memcpy(&h[oh_midi4], midi_flags, 4);
    }

    memcpy(&h[oh_ver], header.version, 9);
    memcpy(&h[oh_title], header.title, 65);
    memcpy(&h[oh_author], header.author, 65);

    // the password: kept as the file had it while it still matches
    short key = (short)(s.hdr[oh_pwdkey] | (s.hdr[oh_pwdkey + 1] << 8));

    if(!pwd_matches(&s.hdr[oh_pwd], key))
    {
        unsigned char md5[16];
        md5_of("", 0, md5);

        if(!memcmp(md5, header.pwd_hash, 16))
            key = 0;
        else
        {
            size_t len = strlen(header.password);

            if(!header.dirty_password || len > 28)
                return fail(c, "the header: the password can't be written in this version");

            if(key == 0)
                key = 0x2F35;

            short k = key;
            byte enc[30];
            memcpy(enc, &s.hdr[oh_pwd], 30);

            for(int i = 0; i < 29; ++i)
            {
                enc[i] = (byte)((i < (int)len ? header.password[i] : 0) + k);
                int t = k >> 15;
                k = (short)((k << 1) + t);
            }

            memcpy(&h[oh_pwd], enc, 30);
        }

        h[oh_pwdkey] = key & 0xFF;
        h[oh_pwdkey + 1] = (key >> 8) & 0xFF;

        if(!pwd_matches(&h[oh_pwd], key))
            return fail(c, "the header: the password can't be written in this version");
    }

    if(v >= 0x177)
    {
        memcpy(&h[oh_minver], header.minver, 9);
        h[oh_build] = b;

        if(header.use_keyfile)
            return fail(c, "the header: this version has no key files");
    }

    if(v == 0x192 && b >= 145)
    {
        for(size_t i = 280; i < sizeof(header.templatepath); ++i)
            if(header.templatepath[i])
                return fail(c, "the header: the template path is too long for this version");

        memcpy(&h[oh_template], header.templatepath, 280);
    }

    return true;
}

// ---- strings ------------------------------------------------------------------------------

bool string_defaults(const MsgStr &m, int i)
{
    for(int k = 72; k <= MSGSIZE; ++k)
        if(m.s[k])
            return false;

    return m.tile == 0 && m.cset == 0 && !m.trans && m.font == font_zfont && m.y == 32 && m.sfx == WAV_MSG &&
           m.x == 24 && m.w == 24 * 8 && m.h == 3 * 8 && m.hspace == 0 && m.vspace == 0 && m.stringflags == 0 &&
           m.listpos == i;
}

bool encode_string(Ctx &c, Out &o, const Raw &raw, int i)
{
    const MsgStr &m = MsgStrings[i];
    In r(raw.p, raw.n);

    if(!string_defaults(m, i))
    {
        if(i >= msg_count && raw.zeros.empty())
        {
            o.put(raw.p, raw.n);    // a slot past the strings in use: not ZQuest's to change
            return true;
        }

        return fail(c, "string %d: this version holds only the text and the next string", i);
    }

    o.put(m.s, 72);
    r.take(72);
    o.c(r.c());                 // the loader drops the 73rd character

    if(c.bf(148))
    {
        int flag = r.c();

        if(m.nextstring == 0)
            flag = 0;
        else if(m.nextstring == i + 1)
            flag = flag ? flag : 1;
        else if(i < msg_count)
            return fail(c, "string %d: in this version a string can only continue to the one after it", i);

        o.c(flag);
        o.c(r.c());
        o.c(r.c());
    }
    else
    {
        o.c(r.c());
        r.w();
        o.w(m.nextstring);
        o.put(r.take(32), 32);
    }

    return true;
}

bool do_strings(Ctx &c, In &in, Out &o)
{
    int n0, n1;
    const Snap &s = g_snap;

    if(c.bf(31))
        n0 = n1 = 128;
    else
    {
        n0 = in.w();
        n1 = msg_count;
        o.w(n1);
    }

    size_t len = c.bf(148) ? 76 : 108;

    for(int i = 0; i < (n0 > n1 ? n0 : n1); ++i)
    {
        const byte *p = i < n0 ? in.take(len) : NULL;

        if(i >= n1)
            continue;

        if(i >= msg_strings_size)
            return fail(c, "string %d: missing", i);

        Raw raw(p, len);
        bool same = p && i < (int)s.strings.size() && !memcmp(&MsgStrings[i], &s.strings[i], sizeof(MsgStr));

        if(same && !c.force && !c.selftest)
        {
            o.put(p, len);
            continue;
        }

        size_t start = o.b.size();

        if(!encode_string(c, o, raw, i))
            return false;

        if(same && !c.force)
        {
            selftest_note(c, "string", i, raw, o, start);
            o.b.resize(start);
            o.put(p, len);
        }
    }

    return true;
}

// ---- door combo sets ------------------------------------------------------------------------

void encode_door(Out &o, In &r, const DoorComboSet &d)
{
    o.put(d.name, 21);
    r.take(21);
    o.c(r.c());

    for(int j = 0; j < 9; ++j) for(int k = 0; k < 4; ++k) o.w(d.doorcombo_u[j][k]);
    o.put(d.doorcset_u, 36);
    for(int j = 0; j < 9; ++j) for(int k = 0; k < 4; ++k) o.w(d.doorcombo_d[j][k]);
    o.put(d.doorcset_d, 36);
    for(int j = 0; j < 9; ++j) for(int k = 0; k < 6; ++k) o.w(d.doorcombo_l[j][k]);
    o.put(d.doorcset_l, 54);
    for(int j = 0; j < 9; ++j) for(int k = 0; k < 6; ++k) o.w(d.doorcombo_r[j][k]);
    o.put(d.doorcset_r, 54);
    r.take(540);

    for(int j = 0; j < 2; ++j) o.w(d.bombdoorcombo_u[j]);
    o.put(d.bombdoorcset_u, 2);
    for(int j = 0; j < 2; ++j) o.w(d.bombdoorcombo_d[j]);
    o.put(d.bombdoorcset_d, 2);
    for(int j = 0; j < 3; ++j) o.w(d.bombdoorcombo_l[j]);
    o.put(d.bombdoorcset_l, 3);
    r.take(21);
    o.c(r.c());
    for(int j = 0; j < 3; ++j) o.w(d.bombdoorcombo_r[j]);
    o.put(d.bombdoorcset_r, 3);
    r.take(9);
    o.c(r.c());
    for(int j = 0; j < 4; ++j) o.w(d.walkthroughcombo[j]);
    o.put(d.walkthroughcset, 4);
    o.put(d.flags, 2);
    o.put(d.expansion, 30);
}

bool do_doors(Ctx &c, In &in, Out &o)
{
    const Snap &s = g_snap;

    if(c.bf(158))
    {
        // Nothing stored: the loader makes the door combo sets from each map's template screen.
        if(door_combo_set_count != s.door_count ||
           memcmp(DoorComboSets, &s.doors[0], sizeof(DoorComboSet) * (s.door_count > door_combo_set_count ? s.door_count : door_combo_set_count)))
            return fail(c, "the door combo sets: this version makes them from the maps' template screens");

        return true;
    }

    int n0 = in.w(), n1 = door_combo_set_count;
    o.w(n1);

    for(int i = 0; i < (n0 > n1 ? n0 : n1); ++i)
    {
        const byte *p = i < n0 ? in.take(638) : NULL;

        if(i >= n1)
            continue;

        Raw raw(p, 638);
        bool same = p && !memcmp(&DoorComboSets[i], &s.doors[i], sizeof(DoorComboSet));

        if(same && !c.force && !c.selftest)
        {
            o.put(p, 638);
            continue;
        }

        size_t start = o.b.size();
        In r(raw.p, raw.n);
        encode_door(o, r, DoorComboSets[i]);

        if(same && !c.force)
        {
            selftest_note(c, "door set", i, raw, o, start);
            o.b.resize(start);
            o.put(p, 638);
        }
    }

    return true;
}

// ---- dmaps --------------------------------------------------------------------------------

bool encode_dmap(Ctx &c, Out &o, const Raw &raw, int i)
{
    const dmap &d = DMaps[i];
    In r(raw.p, raw.n);

    if(d.level > 255 || d.color > 255)
        return fail(c, "dmap %d: this version holds levels and colours up to 255", i);

    // The loader turns a level 0 dungeon into a cave; a cave read as a cave gets the minimap
    // colour fix, which tells the two apart.
    byte T = d.type, S = T;
    bool cave = (T & dmfTYPE) == dmCAVE;

    if(d.level == 0 && cave && !(d.flags & dmfMINIMAPCOLORFIX))
        S = T & ~dmfTYPE;

    long fx = 0;

    if(d.level == 0)
        fx = dmfCAVES | dmf3STAIR | dmfWHIRLWIND | dmfGUYCAVES;

    if(d.level == 0 && get_bit(c.h1.rules, 14))
        fx |= dmfVIEWMAP;

    byte T2 = S;

    if(d.level == 0 && (S & dmfTYPE) == dmDNGN)
        T2 = S | dmCAVE;
    else if((S & dmfTYPE) == dmCAVE)
        fx |= dmfMINIMAPCOLORFIX;

    if(T2 != T || fx != d.flags)
        return fail(c, "dmap %d: this version works out the dmap flags from the level and type", i);

    int type = T & dmfTYPE;
    byte active = (type == dmOVERW || type == dmBSOVERW) ? 0 : 1;
    byte passive = get_bit(quest_rules, qr_ENABLEMAGIC) ? 0 : 1;

    if(d.active_subscreen != active || d.passive_subscreen != passive || d.tmusictrack)
        return fail(c, "dmap %d: this version has no subscreen or music track choice", i);

    for(int k = 0; k < iMax; ++k)
        if(d.disableditems[k])
            return fail(c, "dmap %d: this version can't disable items", i);

    byte xoff = (byte)d.xoff, compass = d.compass, cont = d.cont;

    if(c.bf(41) && i == 0 && c.v <= 0x190)
    {
        cont = (byte)(cont + xoff);
        compass = (byte)(compass + xoff);
    }

    o.c(d.map);
    o.c(d.level);
    o.c(xoff);
    o.c(compass);
    o.c(d.color);
    o.c(d.midi);
    o.c(cont);
    o.c(S);
    o.put(d.grid, 8);

    if(c.bf(41))
    {
        dmap e;
        memset(&e, 0, sizeof(e));
        sprintf(e.title, "                    ");
        sprintf(e.intro, "                                                                        ");

        if(d.level > 0 && d.level < 10)
            sprintf(e.title, "LEVEL-%d             ", d.level);

        if(memcmp(d.name, e.name, sizeof(e.name)) || memcmp(d.title, e.title, sizeof(e.title)) ||
           memcmp(d.intro, e.intro, sizeof(e.intro)) || memcmp(d.tmusic, e.tmusic, sizeof(e.tmusic)) ||
           d.minimap_1_tile || d.minimap_2_tile || d.largemap_1_tile || d.largemap_2_tile ||
           d.minimap_1_cset || d.minimap_2_cset || d.largemap_1_cset || d.largemap_2_cset)
            return fail(c, "dmap %d: this version has no names, titles, intros, map tiles or music", i);

        return true;
    }

    if(d.minimap_1_tile > 65535 || d.minimap_2_tile > 65535 || d.largemap_1_tile > 65535 || d.largemap_2_tile > 65535)
        return fail(c, "dmap %d: a map tile past 65535", i);

    r.take(16);
    o.put(d.name, 21);
    o.put(d.title, 21);
    o.put(d.intro, 73);
    r.take(115);
    o.c(r.c());
    o.w(d.minimap_1_tile);
    o.c(d.minimap_1_cset);
    r.take(3);
    o.c(r.c());
    o.w(d.minimap_2_tile);
    o.c(d.minimap_2_cset);
    r.take(3);
    o.c(r.c());
    o.w(d.largemap_1_tile);
    o.c(d.largemap_1_cset);
    r.take(3);
    o.c(r.c());
    o.w(d.largemap_2_tile);
    o.c(d.largemap_2_cset);
    r.take(3);
    o.put(d.tmusic, 56);
    r.take(56);
    o.c(r.c());
    return true;
}

bool do_dmaps(Ctx &c, In &in, Out &o)
{
    const Snap &s = g_snap;
    int n = c.bf(5) ? 32 : OLDMAXDMAPS;
    size_t len = c.bf(41) ? 16 : 204;

    for(int i = 0; i < n; ++i)
    {
        const byte *p = in.take(len);
        Raw raw(p, len);
        bool same = !memcmp(&DMaps[i], &s.dmaps[i], sizeof(dmap));

        if(same && !c.force && !c.selftest)
        {
            o.put(p, len);
            continue;
        }

        size_t start = o.b.size();

        if(!encode_dmap(c, o, raw, i))
            return false;

        if(same && !c.force)
        {
            selftest_note(c, "dmap", i, raw, o, start);
            o.b.resize(start);
            o.put(p, len);
        }
    }

    for(int i = n; i < MAXDMAPS; ++i)
        if(memcmp(&DMaps[i], &s.dmaps[i], sizeof(dmap)))
            return fail(c, "dmap %d: this version holds only %d dmaps", i, n);

    return true;
}

// ---- misc data: shops, info, warp rings, palette cycles, colours ----------------------------

size_t misc_compare_size()
{
    return offsetof(miscQdata, endstring) + sizeof(word);
}

bool misc_same(const miscQdata &a, const miscQdata &b)
{
    return !memcmp(&a, &b, misc_compare_size());
}

// readmisc moves the empty slots of a shop or info to the end, one bubble sort pass at a time
void sort_shop(byte *item, byte *has, word *price)
{
    for(int j = 0; j < 2; ++j)
        for(int k = 0; k < 2 - j; ++k)
            if(has[k] == 0)
            {
                byte t = item[k]; item[k] = item[k + 1]; item[k + 1] = t;
                word p = price[k]; price[k] = price[k + 1]; price[k + 1] = p;
                t = has[k]; has[k] = has[k + 1]; has[k + 1] = t;
            }
}

void sort_info(word *str, word *price)
{
    for(int j = 0; j < 2; ++j)
        for(int k = 0; k < 2 - j; ++k)
            if(str[k] == 0)
            {
                word t = str[k]; str[k] = str[k + 1]; str[k + 1] = t;
                t = price[k]; price[k] = price[k + 1]; price[k + 1] = t;
            }
}

const int g_perms[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };

bool encode_misc(Ctx &c, Out &o, In &r)
{
    const miscQdata &m = misc;

    // shops
    for(int i = 0; i < 16; ++i)
    {
        const shoptype &sh = m.shop[i];
        const byte *rp = r.take(10);
        byte item[3], has[3];
        word price[3];

        // the order in the file is kept while it still sorts into ZQuest's
        for(int k = 0; k < 3; ++k)
        {
            item[k] = rp[k];
            has[k] = rp[k] != 0;
            price[k] = rp[4 + 2 * k] | (rp[5 + 2 * k] << 8);
        }

        sort_shop(item, has, price);

        if(!memcmp(item, sh.item, 3) && !memcmp(has, sh.hasitem, 3) && !memcmp(price, sh.price, 6))
        {
            o.put(rp, 10);
            continue;
        }

        bool found = false;

        for(int p = 0; p < 6 && !found; ++p)
        {
            for(int k = 0; k < 3; ++k)
            {
                item[k] = sh.item[g_perms[p][k]];
                has[k] = item[k] != 0;
                price[k] = sh.price[g_perms[p][k]];
            }

            byte i2[3], h2[3];
            word p2[3];
            memcpy(i2, item, 3);
            memcpy(h2, has, 3);
            memcpy(p2, price, 6);
            sort_shop(i2, h2, p2);

            if(!memcmp(i2, sh.item, 3) && !memcmp(h2, sh.hasitem, 3) && !memcmp(p2, sh.price, 6))
                found = true;
        }

        if(!found)
            return fail(c, "shop %d: in this version an empty item slot is item 0", i);

        o.put(item, 3);
        o.c(rp[3]);

        for(int k = 0; k < 3; ++k)
            o.w(price[k]);
    }

    // infos
    size_t infolen = c.bf(146) ? 10 : 14;

    for(int i = 0; i < 16; ++i)
    {
        const infotype &inf = m.info[i];
        const byte *rp = r.take(infolen);
        In ri(rp, infolen);
        word str[3], price[3];

        for(int k = 0; k < 3; ++k)
            str[k] = c.bf(146) ? ri.c() : ri.w();

        ri.take(c.bf(146) ? 1 : 2);

        for(int k = 0; k < 3; ++k)
            price[k] = ri.w();

        sort_info(str, price);

        if(!memcmp(str, inf.str, 6) && !memcmp(price, inf.price, 6))
        {
            o.put(rp, infolen);
            continue;
        }

        bool found = false;

        for(int p = 0; p < 6 && !found; ++p)
        {
            for(int k = 0; k < 3; ++k)
            {
                str[k] = inf.str[g_perms[p][k]];
                price[k] = inf.price[g_perms[p][k]];
            }

            word s2[3], p2[3];
            memcpy(s2, str, 6);
            memcpy(p2, price, 6);
            sort_info(s2, p2);

            if(!memcmp(s2, inf.str, 6) && !memcmp(p2, inf.price, 6))
                found = true;
        }

        if(!found)
            return fail(c, "info %d: can't be written in this version", i);

        for(int k = 0; k < 3; ++k)
        {
            if(c.bf(146))
            {
                if(str[k] > 255)
                    return fail(c, "info %d: this version holds strings up to 255 here", i);

                o.c(str[k]);
            }
            else
                o.w(str[k]);
        }

        o.put(rp + (c.bf(146) ? 3 : 6), c.bf(146) ? 1 : 2);

        for(int k = 0; k < 3; ++k)
            o.w(price[k]);
    }

    // warp rings
    for(int i = 0; i < 8; ++i)
    {
        const warpring &w = m.warp[i];

        for(int k = 0; k < 8; ++k)
        {
            if(w.dmap[k] > 255)
                return fail(c, "warp ring %d: this version holds dmaps up to 255 here", i);

            o.c(w.dmap[k]);
        }

        o.put(w.scr, 8);
        o.c(w.size);
        r.take(17);
        o.c(r.c());
    }

    // palette cycles (saved with the palettes from 1.93 on)
    int cycles = c.bf(73) ? 16 : 256;

    for(int i = 0; i < cycles; ++i)
        for(int k = 0; k < 3; ++k)
        {
            o.c(m.cycles[i][k].first);
            o.c(m.cycles[i][k].count);
            o.c(m.cycles[i][k].speed);
        }

    r.take(cycles * 9);

    // the whistle's warps, warp ring 8 from 1.93 on
    for(int i = 0; i < 9; ++i)
    {
        if(m.warp[8].dmap[i] > 255)
            return fail(c, "the whistle warps: this version holds dmaps up to 255 here");

        o.c(m.warp[8].dmap[i]);
        o.c(m.warp[8].scr[i]);
    }

    r.take(18);
    o.put(m.triforce, 8);
    r.take(8);

    // colours
    const zcolors &z = m.colors;
    const byte cols[20] = { z.text, z.caption, z.overw_bg, z.dngn_bg, z.dngn_fg, z.cave_fg, z.bs_dk, z.bs_goal,
                            z.compass_lt, z.compass_dk, z.subscr_bg, z.triframe_color, z.link_dot, z.bmap_bg,
                            z.bmap_fg, z.triforce_cset, z.triframe_cset, z.overworld_map_cset, z.dungeon_map_cset,
                            z.blueframe_cset };
    o.put(cols, 20);
    const tile_id tiles[6] = { z.triforce_tile, z.triframe_tile, z.overworld_map_tile, z.dungeon_map_tile,
                               z.blueframe_tile, z.HCpieces_tile };

    for(int k = 0; k < 6; ++k)
    {
        if(tiles[k] > 65535)
            return fail(c, "the map styles: a tile past 65535");

        o.w(tiles[k]);
    }

    o.c(z.HCpieces_cset);
    r.take(33);
    o.put(r.take(7), 7);

    if(c.v == 0x192 && c.b > 145)
        o.put(r.take(256), 256);

    int icons = c.bf(73) ? 3 : 4;

    for(int k = 0; k < icons; ++k)
    {
        if(m.icons[k] > 65535)
            return fail(c, "the game icons: a tile past 65535");

        o.w(m.icons[k]);
    }

    r.take(icons * 2);

    if(c.bf(30))
        return true;

    o.put(r.take(16 * (c.v == 0x192 && c.b < 146 ? 25 : 72)), 16 * (c.v == 0x192 && c.b < 146 ? 25 : 72));

    if(c.bf(146))
    {
        if(m.endstring > 255)
            return fail(c, "the end string: this version holds strings up to 255 here");

        o.c(m.endstring);
        r.c();
        o.c(r.c());
    }
    else
    {
        o.w(m.endstring);
        r.w();
    }

    size_t exp = (c.v == 0x192 && c.b < 73) ? 198 : 196;
    o.put(r.take(exp), exp);
    return true;
}

size_t misc_size(const Ctx &c)
{
    size_t n = 160 + 16 * (c.bf(146) ? 10 : 14) + 144 + (c.bf(73) ? 16 : 256) * 9 + 18 + 8 + 33 + 7;

    if(c.v == 0x192 && c.b > 145)
        n += 256;

    n += (c.bf(73) ? 3 : 4) * 2;

    if(c.bf(30))
        return n;

    n += 16 * (c.v == 0x192 && c.b < 146 ? 25 : 72) + 2 + ((c.v == 0x192 && c.b < 73) ? 198 : 196);
    return n;
}

bool do_misc(Ctx &c, In &in, Out &o)
{
    size_t len = misc_size(c);
    const byte *p = in.take(len);
    Raw raw(p, len);
    bool same = misc_same(misc, g_snap.misc);

    if(same && !c.force && !c.selftest)
    {
        o.put(p, len);
        return true;
    }

    size_t start = o.b.size();
    In r(raw.p, raw.n);

    if(!encode_misc(c, o, r))
        return false;

    if(o.b.size() - start != len)
        return fail(c, "the misc data: wrote %d bytes, not %d", (int)(o.b.size() - start), (int)len);

    // Read it back with the game's own reader, from the data as loaded (readmisc keeps what it
    // doesn't read), and see that it gives ZQuest's data.
    zquestheader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.zelda_version = c.v;
    hdr.build = c.b;
    MemIn mi;
    mi.p = &o.b[start];
    mi.n = (long)len;
    mi.at = 0;
    PACKFILE *mf = pack_fopen_vtable(&g_memin_vtable, &mi);

    if(!mf)
        return fail(c, "the misc data: out of memory");

    miscQdata *back = new miscQdata;
    *back = g_snap.misc;
    int rr = readmisc(mf, &hdr, back, true);
    pack_fclose(mf);
    bool ok = rr == 0 && mi.at == mi.n && misc_same(*back, misc);
    delete back;

    if(!ok)
        return fail(c, "the misc data (shops, info, warp rings, colours): something this version can't hold changed");

    if(same && !c.force)
    {
        selftest_note(c, "misc", 0, raw, o, start);
        o.b.resize(start);
        o.put(p, len);
    }

    return true;
}

// ---- items ----------------------------------------------------------------------------------

bool sword_or_hookshot(int i)
{
    return i == iSword || i == iWSword || i == iMSword || i == iXSword || i == iHookshot;
}

// The item with the fields this version holds (and the init data holds for it) put back as
// loaded: what is left must be as loaded too.
void item_held_from(itemdata &dst, const itemdata &src, int i, const Ctx &c)
{
    if(i != iLetterUsed)
    {
        dst.tile = src.tile;
        dst.misc = src.misc;
        dst.csets = src.csets;
        dst.frames = src.frames;
        dst.speed = src.speed;
    }

    dst.delay = src.delay;

    if(!c.bf(27) && sword_or_hookshot(i))
    {
        dst.misc2 = src.misc2;

        if(i != iHookshot)
        {
            dst.pickup_hearts = src.pickup_hearts;
            dst.misc1 = src.misc1;
            dst.flags = (dst.flags & ~ITEM_FLAG1) | (src.flags & ITEM_FLAG1);
        }
    }
}

bool encode_item(Ctx &c, Out &o, const Raw &raw, int i)
{
    const itemdata &it = itemsbuf[i];
    itemdata t = it;
    item_held_from(t, g_snap.items[i], i, c);

    if(memcmp(&t, &g_snap.items[i], sizeof(itemdata)) || memcmp(item_string[i], &g_snap.item_names[i * 64], 64))
        return fail(c, "item %d: this version holds only its graphics (the rest comes from defaults and rules)", i);

    In r(raw.p, raw.n);

    if(i == iLetterUsed)            // the used letter looks like the letter
        o.put(r.take(6), 6);
    else
    {
        if(it.tile > 65535)
            return fail(c, "item %d: a tile past 65535", i);

        o.w(it.tile);
        o.c(it.misc);
        o.c(it.csets);
        o.c(it.frames);
        o.c(it.speed);
        r.take(6);
    }

    o.c(it.delay);
    r.c();
    o.c(r.c());
    return true;
}

bool do_items(Ctx &c, In &in, Out &o)
{
    int n = c.v < 0x186 ? 64 : MAXITEMS;

    for(int i = 0; i < n; ++i)
    {
        const byte *p = in.take(8);
        Raw raw(p, 8);
        bool same = !memcmp(&itemsbuf[i], &g_snap.items[i], sizeof(itemdata)) &&
                    !memcmp(item_string[i], &g_snap.item_names[i * 64], 64);

        if(same && !c.force && !c.selftest)
        {
            o.put(p, 8);
            continue;
        }

        size_t start = o.b.size();

        if(!encode_item(c, o, raw, i))
            return false;

        if(same && !c.force)
        {
            selftest_note(c, "item", i, raw, o, start);
            o.b.resize(start);
            o.put(p, 8);
        }
    }

    return true;
}

// ---- weapons --------------------------------------------------------------------------------

bool wpn_same(const wpndata &a, const wpndata &b)
{
    return a.tile == b.tile && a.misc == b.misc && a.csets == b.csets && a.frames == b.frames && a.speed == b.speed && a.type == b.type;
}

// The weapons the loader copies from another, for this version, or -1.
int weapon_source(const Ctx &c, int i)
{
    if(i == wSBOOM)
        return wBOOM;

    if(c.bf(129) && i == wHSCHAIN_V)
        return wHSCHAIN_H;

    if(c.v < 0x210)
    {
        if(i == wLSHEAD) return wHSHEAD;
        if(i == wLSCHAIN_H) return wHSCHAIN_H;
        if(i == wLSHANDLE) return wHSHANDLE;
        if(i == wLSCHAIN_V) return wHSCHAIN_V;
    }

    return -1;
}

bool encode_weapon(Ctx &c, Out &o, const Raw &raw, int i)
{
    const wpndata &w = wpnsbuf[i];
    In r(raw.p, raw.n);

    if(memcmp(weapon_string[i], &g_snap.weapon_names[i * 64], 64))
        return fail(c, "weapon %d: this version has no weapon names", i);

    int src = weapon_source(c, i);

    if(src >= 0)
    {
        // Left alone, it follows its source when the quest is read again, as it always did in
        // this version; changed on its own, it can't be held.
        if(!wpn_same(w, wpnsbuf[src]) && !wpn_same(w, g_snap.weapons[i]))
            return fail(c, "weapon %d: in this version it looks like weapon %d", i, src);

        o.put(raw.p, 8);
        return true;
    }

    if((w.misc & WF_BEHIND) != (i == ewFIRETRAIL ? WF_BEHIND : 0))
        return fail(c, "weapon %d: this version can't choose which weapons are drawn behind", i);

    if(w.tile > 65535)
        return fail(c, "weapon %d: a tile past 65535", i);

    int rtile = r.w(), rmisc = r.c(), rcsets = r.c();

    if(i == iwQuarterHearts)
    {
        if(w.tile != 1 || w.csets != 1)
            return fail(c, "weapon %d: this version sets its tile and cset itself", i);

        o.w(rtile);
    }
    else
        o.w(w.tile);

    o.c((w.misc & ~WF_BEHIND) | (rmisc & WF_BEHIND));
    o.c(i == iwQuarterHearts ? rcsets : w.csets);
    o.c(w.frames);
    o.c(w.speed);
    o.c(w.type);
    r.take(3);
    o.c(r.c());
    return true;
}

bool do_weapons(Ctx &c, In &in, Out &o)
{
    int n = c.v < 0x185 ? 32 : (c.v < 0x186 ? 64 : MAXWPNS);

    for(int i = 0; i < n; ++i)
    {
        const byte *p = in.take(8);
        Raw raw(p, 8);
        bool same = wpn_same(wpnsbuf[i], g_snap.weapons[i]) && !memcmp(weapon_string[i], &g_snap.weapon_names[i * 64], 64);

        if(same && !c.force && !c.selftest)
        {
            o.put(p, 8);
            continue;
        }

        size_t start = o.b.size();

        if(!encode_weapon(c, o, raw, i))
            return false;

        if(same && !c.force)
        {
            selftest_note(c, "weapon", i, raw, o, start);
            o.b.resize(start);
            o.put(p, 8);
        }
    }

    return true;
}

// ---- map screens ----------------------------------------------------------------------------

bool screen_same(const mapscr &a, const mapscr &b, bool with_doors)
{
#define SAME(f) (memcmp(&a.f, &b.f, sizeof(a.f)) == 0)
    return SAME(valid) && SAME(guy) && SAME(str) && SAME(room) && SAME(item) && SAME(hasitem) && SAME(tilewarptype) &&
           SAME(tilewarpoverlayflags) && (!with_doors || SAME(door_combo_set)) && SAME(warpreturnx) && SAME(warpreturny) &&
           SAME(warpreturnc) && SAME(stairx) && SAME(stairy) && SAME(itemx) && SAME(itemy) && SAME(color) &&
           SAME(enemyflags) && SAME(door) && SAME(tilewarpdmap) && SAME(tilewarpscr) && SAME(exitdir) && SAME(enemy) &&
           SAME(pattern) && SAME(sidewarptype) && SAME(sidewarpoverlayflags) && SAME(warparrivalx) && SAME(warparrivaly) &&
           SAME(path) && SAME(sidewarpscr) && SAME(sidewarpdmap) && SAME(sidewarpindex) && SAME(undercombo) &&
           SAME(undercset) && SAME(catchall) && SAME(flags) && SAME(flags2) && SAME(flags3) && SAME(flags4) &&
           SAME(flags5) && SAME(flags6) && SAME(flags7) && SAME(flags8) && SAME(flags9) && SAME(flags10) &&
           SAME(csensitive) && SAME(noreset) && SAME(nocarry) && SAME(layermap) && SAME(layerscreen) &&
           SAME(layeropacity) && SAME(timedwarptics) && SAME(nextmap) && SAME(nextscr) && SAME(secretcombo) &&
           SAME(secretcset) && SAME(secretflag) && a.data == b.data && a.sflag == b.sflag && a.cset == b.cset &&
           SAME(viewX) && SAME(viewY) && SAME(scrWidth) && SAME(scrHeight) && SAME(numff) && SAME(ffdata) &&
           SAME(ffcset) && SAME(ffdelay) && SAME(ffx) && SAME(ffy) && SAME(ffxdelta) && SAME(ffydelta) &&
           SAME(ffxdelta2) && SAME(ffydelta2) && SAME(ffflags) && SAME(ffwidth) && SAME(ffheight) && SAME(fflink) &&
           SAME(ffscript) && SAME(initd) && SAME(inita) && SAME(initialized) && SAME(script_entry) &&
           SAME(script_occupancy) && SAME(script_exit) && SAME(oceansfx) && SAME(bosssfx) && SAME(secretsfx) &&
           SAME(holdupsfx) && SAME(old_cpage) && SAME(screen_midi) && SAME(lens_layer);
#undef SAME
}

void blank_screen(mapscr &s)      // qst.cpp's clear_screen
{
    s.zero_memory();

    for(int j = 0; j < 6; ++j)
        s.layeropacity[j] = 255;

    for(int j = 0; j < 32; ++j)
    {
        s.ffwidth[j] = 15;
        s.ffheight[j] = 15;
    }
}

int screen_len(const Ctx &c)
{
    int n = 2 + (c.bf(146) ? 1 : 2) + 2 + (c.bf(154) ? 1 : 0) + 1 + 1;   // ... tilewarptype, dropped byte
    n += (!c.bf(154)) ? 2 : 0;                                          // door combo set
    n += 2 + 4 + 1 + 1 + 4 + 1 + 1 + 1 + 1;                             // ... exitdir, dropped byte
    n += (c.v == 0x192 && c.b > 145 && c.b < 154) ? 1 : 0;
    n += c.bf(10) ? 10 : 20;
    n += 1 + 1 + 2 + 4 + 1 + 1 + 2 + 1 + 1 + 2 + 3;                     // pattern .. flags3
    n += !c.bf(98) ? 12 : 0;
    n += (c.v == 0x192 && c.b > 149) ? 36 : 0;
    n += !c.bf(150) ? 6 : 0;
    n += !c.bf(154) ? 3 : 0;
    n += c.bf(24) ? 15 : (c.bf(98) ? 11 : (c.bf(150) ? 32 : (c.bf(154) ? 64 : 62)));
    n += c.bf(137) ? 20 : (c.bf(154) ? 256 : 256 + 256);
    n += (c.v == 0x192 && c.b > 97 && c.b < 154) ? 1 : 0;
    n += 352;
    n += !c.bf(21) ? 176 : 0;
    n += !c.bf(98) ? 176 : 0;
    return n;
}

int old_warp_type(const Ctx &c, int T, int raw, bool &ok)
{
    ok = true;

    if(c.v < 0x192 || (c.v == 0x192 && c.b <= 163))
    {
        if(T == wtNOWARP)
            return 8;

        if(T == wtWHISTLE)
            return (raw == 9 || raw == 10) ? raw : 9;

        if(T == 8)
            ok = false;
    }

    if(T > 255)
        ok = false;

    return T;
}

// The secret combos of a 1.90/early 1.92 screen are places on its map's second template
// screen: the loader takes the combo, cset and flag found there.
void derive_secrets(mapscr &s, const mapscr &t2)
{
    for(int k = 0; k < 128; ++k)
    {
        int pos = s.secretcombo[k];

        if(pos >= (int)t2.data.size())
            continue;

        s.secretcset[k] = t2.cset[pos];
        s.secretflag[k] = t2.sflag[pos];
        s.secretcombo[k] = (t2.data[pos] & 0xFF) + (s.old_cpage << 8);
    }
}

bool encode_screen(Ctx &c, Out &o, const Raw &raw, int map, int scr)
{
    const mapscr &s = TheMaps[map * MAPSCRS + scr];
    // the second template screen as the loader will see it: in 1.90 a copy of the first
    const mapscr &t2 = TheMaps[map * MAPSCRS + (c.bf(137) ? TEMPLATE : TEMPLATE2)];
    In r(raw.p, raw.n);
    size_t start = o.b.size();
    bool old_page = c.bf(154);       // combos are a byte plus the screen's combo page
    int cpage = s.old_cpage;

#define FAILS(msg) return fail(c, "map %d screen %02X: %s", map + 1, scr, msg)
    o.c(s.valid);
    o.c(s.guy);
    r.take(2);

    if(c.bf(146))
    {
        if(s.str > 255)
            FAILS("this version holds strings up to 255 here");

        o.c(s.str);
        r.c();
    }
    else
    {
        o.w(s.str);
        r.w();
    }

    o.c(s.room);
    o.c(s.item);
    r.take(2);

    if(old_page)
        o.c(r.c());

    bool ok;
    int rt = r.c();
    o.c(old_warp_type(c, s.tilewarptype[0], rt, ok));

    if(!ok)
        FAILS("a tile warp type this version doesn't have");

    o.c(r.c());

    if(!old_page)
    {
        o.w(s.door_combo_set);
        r.w();
    }

    o.c(s.warpreturnx[0]);
    o.c(s.warpreturny[0]);
    o.c(s.stairx);
    o.c(s.stairy);
    o.c(s.itemx);
    o.c(s.itemy);

    if(s.color > 255)
        FAILS("this version holds colours up to 255 here");

    o.c(s.color);
    o.c(s.enemyflags);
    o.put(s.door, 4);

    if(s.tilewarpdmap[0] > 255 || s.sidewarpdmap[0] > 255)
        FAILS("this version holds warp dmaps up to 255");

    o.c(s.tilewarpdmap[0]);
    o.c(s.tilewarpscr[0]);
    o.c(s.exitdir);
    r.take(15);
    o.c(r.c());

    if(c.v == 0x192 && c.b > 145 && c.b < 154)
        o.c(r.c());

    for(int k = 0; k < 10; ++k)
    {
        int e = s.enemy[k], x = 0;

        if(e)
        {
            int y = e - 10;

            if(y < 1)
                FAILS("an enemy this version doesn't have");

            if(c.bf(108))
            {
                if(y >= 62)
                    x = y - 5;
                else if(y >= 53 && y <= 57)
                    x = y - 1;
                else if(y <= 51)
                    x = y;
                else
                    FAILS("an enemy this version doesn't have");
            }
            else
                x = y;
        }

        if(c.bf(10))
        {
            if(x > 255)
                FAILS("an enemy this version doesn't have");

            o.c(x);
            r.c();
        }
        else
        {
            o.w(x);
            r.w();
        }
    }

    o.c(s.pattern);
    r.c();
    rt = r.c();
    o.c(old_warp_type(c, s.sidewarptype[0], rt, ok));

    if(!ok)
        FAILS("a side warp type this version doesn't have");

    o.c(s.warparrivalx);
    o.c(s.warparrivaly);
    o.put(s.path, 4);
    o.c(s.sidewarpscr[0]);
    o.c(s.sidewarpdmap[0]);
    r.take(8);
    int rund = r.w();

    if(old_page)
    {
        if((s.undercombo >> 8) != cpage || s.undercset > 7)
            FAILS("all combos on a screen must be on one combo page in this version");

        o.w((s.undercombo & 0xFF) | (s.undercset << 8) | (rund & 0xF800));
        o.c(cpage);
        r.c();
        o.c(r.c());
    }
    else
    {
        o.w(s.undercombo);
        o.c(s.old_cpage);
        o.c(s.undercset);
        r.take(2);
    }

    o.w(s.catchall);
    r.w();

    // The sound effects were screen flags in this version.
    int rf1 = r.c(), rf2 = r.c(), rf3 = r.c();
    int f1 = s.flags, f2 = s.flags2, f3 = s.flags3;

    if((f1 & (8 | 128)) || (f2 & 32) || (f3 & (2 | 64)))
        FAILS("a screen flag this version uses for sound effects");

    if(s.bosssfx == 0)
    {
        f3 |= rf3 & 2;
        f2 |= rf2 & 32;
    }
    else if(s.bosssfx == WAV_DODONGO)
    {
        f1 |= 8;
        f3 |= 2;
        f2 |= rf2 & 32;
    }
    else if(s.bosssfx == WAV_VADER)
    {
        f1 |= 8;
        f2 |= 32;
    }
    else if(s.bosssfx == WAV_ROAR)
        f1 |= 8;
    else
        FAILS("a boss sound this version doesn't have");

    if(s.oceansfx == WAV_SEA)
        f1 |= 128;
    else if(s.oceansfx)
        FAILS("an ambient sound this version doesn't have");

    if(s.secretsfx == 0)
        f3 |= 64;
    else if(s.secretsfx != WAV_SECRET)
        FAILS("a secret sound this version doesn't have");

    o.c(f1);
    o.c(f2);
    o.c(f3);

    if(!c.bf(98))
    {
        o.put(s.layermap, 6);
        o.put(s.layerscreen, 6);
        r.take(12);
    }

    if(c.v == 0x192 && c.b > 149)
        o.put(r.take(36), 36);

    if(!c.bf(150))
    {
        o.put(s.layeropacity, 6);
        r.take(6);
    }

    if(!c.bf(154))
    {
        o.c(r.c());
        o.w(s.timedwarptics);
        r.w();
    }

    int extras = c.bf(24) ? 15 : (c.bf(98) ? 11 : (c.bf(150) ? 32 : (c.bf(154) ? 64 : 62)));
    o.put(r.take(extras), extras);

    if(old_page)
    {
        int stored = c.bf(137) ? 20 : 256;

        for(int k = 0; k < stored; ++k)
        {
            int rp = r.c();

            if(k >= 128 || (c.bf(137) && k >= 20))
            {
                o.c(rp);
                continue;
            }

            // a place on the template screen with this combo, cset and flag; the file's own first
            int want = s.secretcombo[k];
            int pos = -1;

            for(int q = -1; q < (int)t2.data.size() && pos < 0; ++q)
            {
                int p = q < 0 ? rp : q;

                if(p < (int)t2.data.size() && (t2.data[p] & 0xFF) + (cpage << 8) == want &&
                   t2.cset[p] == s.secretcset[k] && t2.sflag[p] == s.secretflag[k])
                    pos = p;
            }

            if(pos < 0)
                FAILS("a secret combo that isn't on the map's template screen");

            o.c(pos);
        }
    }
    else
    {
        for(int k = 0; k < 128; ++k)
            o.w(s.secretcombo[k]);

        o.put(s.secretcset, 128);
        o.put(s.secretflag, 128);
        r.take(512);
    }

    if(c.v == 0x192 && c.b > 97 && c.b < 154)
        o.c(r.c());

    if(s.data.size() < 176 || s.sflag.size() < 176 || s.cset.size() < 176)
        FAILS("missing combos");

    for(int k = 0; k < 176; ++k)
    {
        int rd = r.w();

        if(old_page)
        {
            if((s.data[k] >> 8) != cpage || s.cset[k] > 7)
                FAILS("all combos on a screen must be on one combo page in this version, with csets up to 7");

            int d = (s.data[k] & 0xFF) | (s.cset[k] << 8);

            if(c.bf(21))
            {
                if(s.sflag[k] > 31)
                    FAILS("this version holds combo flags up to 31");

                d |= s.sflag[k] << 11;
            }
            else
                d |= rd & 0xF800;

            o.w(d);
        }
        else
            o.w(s.data[k]);
    }

    if(!c.bf(21))
    {
        o.put(&s.sflag[0], 176);
        r.take(176);
    }

    if(!c.bf(98))
    {
        if(old_page && !(c.v == 0x192 && c.b == 153))
            o.put(r.take(176), 176);        // read, then worked out from the combos
        else
        {
            o.put(&s.cset[0], 176);
            r.take(176);
        }
    }

    if(o.b.size() - start != raw.n)
        FAILS("wrote the wrong number of bytes");

    // Read it back with the game's own reader, add what readmaps works out, and compare.
    zquestheader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.zelda_version = c.v;
    hdr.build = c.b;
    zcmap zm;
    memset(&zm, 0, sizeof(zm));
    zm.tileWidth = 16;
    zm.tileHeight = 11;
    MemIn mi;
    mi.p = &o.b[start];
    mi.n = (long)raw.n;
    mi.at = 0;
    PACKFILE *mf = pack_fopen_vtable(&g_memin_vtable, &mi);

    if(!mf)
        FAILS("out of memory");

    mapscr back;
    blank_screen(back);
    int rr = readmapscreen(mf, &hdr, &back, &zm, 0);
    pack_fclose(mf);

    if(rr || mi.at != mi.n)
        FAILS("can't be read back");

    if(old_page)
        derive_secrets(back, t2);

    if(!screen_same(back, s, !old_page))
        FAILS("something this version can't hold changed (layers, warps, flags, sounds, FFCs...)");
#undef FAILS

    return true;
}

bool do_maps(Ctx &c, In &in, Out &o)
{
    const Snap &s = g_snap;
    int screens = c.bf(137) ? MAPSCRS192b136 : MAPSCRS;
    int len = screen_len(c);
    int m0 = c.h0.map_count, m1 = map_count;

    if((int)TheMaps.size() < m1 * MAPSCRS)
        return fail(c, "the maps: missing screens");

    for(int m = 0; m < (m0 > m1 ? m0 : m1); ++m)
    {
        for(int j = 0; j < screens; ++j)
        {
            const byte *p = m < m0 ? in.take(len) : NULL;

            if(m >= m1)
                continue;

            int idx = m * MAPSCRS + j;
            Raw raw(p, len);
            bool same = p && idx < (int)s.screens.size() && screen_same(TheMaps[idx], s.screens[idx], true);

            if(same && !c.force && !c.selftest)
            {
                o.put(p, len);
                continue;
            }

            size_t start = o.b.size();

            if(!encode_screen(c, o, raw, m, j))
                return false;

            if(same && !c.force)
            {
                selftest_note(c, "screen", idx, raw, o, start);
                o.b.resize(start);
                o.put(p, len);
            }
        }

        if(m >= m1)
            continue;

        bool known = (m + 1) * MAPSCRS <= (int)s.screens.size();

        // 1.90 has 132 screens a map: the loader copies the template screen to the second
        // template, and makes the last three.
        if(c.bf(137))
        {
            // Left alone, the second template follows the first when the quest is read again.
            const mapscr &t2 = TheMaps[m * MAPSCRS + TEMPLATE2];

            if(!screen_same(t2, TheMaps[m * MAPSCRS + TEMPLATE], true) && (!known || !screen_same(t2, s.screens[m * MAPSCRS + TEMPLATE2], true)))
                return fail(c, "map %d: in this version the second template screen is a copy of the first", m + 1);

            for(int j = 133; j < MAPSCRS && known; ++j)
                if(!screen_same(TheMaps[m * MAPSCRS + j], s.screens[m * MAPSCRS + j], true))
                    return fail(c, "map %d screen %02X: this version has only 132 screens a map", m + 1, j);
        }

        // and makes each screen's door combo set from the template screen
        if(c.bf(154) && known)
            for(int j = 0; j < MAPSCRS; ++j)
                if(TheMaps[m * MAPSCRS + j].door_combo_set != s.screens[m * MAPSCRS + j].door_combo_set)
                    return fail(c, "map %d screen %02X: this version makes the door combo set from the template screen", m + 1, j);
    }

    return true;
}

// ---- combos ---------------------------------------------------------------------------------

bool combo_same(const newcombo &a, const newcombo &b)
{
    return a.tile == b.tile && a.flip == b.flip && a.walk == b.walk && a.type == b.type && a.csets == b.csets &&
           a.frames == b.frames && a.speed == b.speed && a.nextcombo == b.nextcombo && a.nextcset == b.nextcset &&
           a.flag == b.flag && a.skipanim == b.skipanim && a.nexttimer == b.nexttimer && a.skipanimy == b.skipanimy &&
           a.animflags == b.animflags;
}

bool combo_empty(const newcombo &a)
{
    newcombo z;
    memset(&z, 0, sizeof(z));
    return combo_same(a, z);
}

// What the loader makes of a stored combo type (readcombos, no section version)
int combo_type_loaded(const Ctx &c, int t)
{
    if(c.bf(185) && t == cHOOKSHOTONLY)
        t = cLADDERHOOKSHOT;

    switch(t)
    {
    case cSLASH: return cSLASHTOUCHY;
    case cSLASHITEM: return cSLASHITEMTOUCHY;
    case cBUSH: return cBUSHTOUCHY;
    case cFLOWERS: return cFLOWERSTOUCHY;
    case cTALLGRASS: return cTALLGRASSTOUCHY;
    case cSLASHNEXT: return cSLASHNEXTTOUCHY;
    case cSLASHNEXTITEM: return cSLASHNEXTITEMTOUCHY;
    case cBUSHNEXT: return cBUSHNEXTTOUCHY;
    }

    return t;
}

int goriya_tile(int t)
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

bool encode_combo(Ctx &c, Out &o, const Raw &raw, int i)
{
    const newcombo &cb = combobuf[i];
    In r(raw.p, raw.n);
    int rtile = r.w(), rflip = r.c(), rwalk = r.c(), rtype = r.c();
    (void)rflip;
    (void)rwalk;
    r.c();
    int type = cb.type;

    if(combo_type_loaded(c, rtype) == cb.type)
        type = rtype;
    else if(combo_type_loaded(c, cb.type) != cb.type)
        return fail(c, "combo %d: this version reads this combo type as another one", i);

    if(cb.flag || cb.skipanim || cb.nexttimer || cb.skipanimy || cb.animflags)
        return fail(c, "combo %d: this version has no inherent flags or animation options", i);

    if(cb.tile > 65535)
        return fail(c, "combo %d: a tile past 65535", i);

    int tile = cb.tile;

    if(c.v >= 0x192 && !get_bit(quest_rules, qr_NEWENEMYTILES))
        tile = goriya_tile(tile);       // the loader swaps the Goriya tiles back

    (void)rtile;
    o.w(tile);
    o.c(cb.flip);
    o.c(cb.walk);
    o.c(type);
    o.c(cb.csets);
    o.put(r.take(2), 2);

    if(c.v < 0x192)
    {
        if(cb.frames || cb.speed || cb.nextcombo || cb.nextcset)
            return fail(c, "combo %d: this version has no combo animation", i);

        return true;
    }

    o.c(cb.frames);
    o.c(cb.speed);
    o.w(cb.nextcombo);
    o.c(cb.nextcset);
    r.take(5);
    o.put(r.take(11), 11);
    return true;
}

bool do_combos(Ctx &c, In &in, Out &o)
{
    int n0, n1;
    size_t len = c.v < 0x192 ? 8 : 24;

    if(c.v < 0x174 || c.v == 0x191)
        return fail(c, "the combos: version not supported");

    if(c.v < 0x191)
        n0 = n1 = 2048;
    else
    {
        n0 = in.w();
        n1 = n0;

        for(int i = MAXCOMBOS - 1; i >= n1; --i)
            if(!combo_empty(combobuf[i]))
            {
                n1 = i + 1;
                break;
            }

        o.w(n1);
    }

    for(int i = n1; i < MAXCOMBOS; ++i)
        if(!combo_empty(combobuf[i]))
            return fail(c, "combo %d: this version holds only %d combos", i, n1);

    for(int i = 0; i < (n0 > n1 ? n0 : n1); ++i)
    {
        const byte *p = i < n0 ? in.take(len) : NULL;

        if(i >= n1)
            continue;

        Raw raw(p, len);
        bool same = p && combo_same(combobuf[i], g_snap.combos[i]);

        if(same && !c.force && !c.selftest)
        {
            o.put(p, len);
            continue;
        }

        size_t start = o.b.size();

        if(!encode_combo(c, o, raw, i))
            return false;

        if(same && !c.force)
        {
            selftest_note(c, "combo", i, raw, o, start);
            o.b.resize(start);
            o.put(p, len);
        }
    }

    return true;
}

// ---- palettes -------------------------------------------------------------------------------

// Where the loader puts the stored cset k, or -1 for one it throws away.
int cset_place(const Ctx &c, int k)
{
    if(c.bf(73))
    {
        if(k < oldpoSPRITE)
            return k;

        int t = k - oldpoSPRITE;

        if(t < 8)
            return newerpoSPRITE + t;

        if(t < 11)
            return newerpoSPRITE + t + 1;

        if(t == 11)
            return -1;

        return newerpoSPRITE + t;
    }

    if(k < newpoSPRITE)
        return k;

    return newerpoSPRITE + (k - newpoSPRITE);
}

bool do_colors(Ctx &c, In &in, Out &o)
{
    const Snap &s = g_snap;
    int n = c.bf(73) ? oldpdTOTAL : newpdTOTAL;

    // the csets the loader clears
    int z0 = c.bf(73) ? oldpoSPRITE : newpoSPRITE;

    for(int k = z0 * 48; k < newerpoSPRITE * 48; ++k)
        if(colordata[k])
            return fail(c, "the palettes: this version has no csets here (cset %d)", k / 48);

    if(c.bf(73))
        for(int k = 0; k < 48; ++k)
            if(colordata[(newerpoSPRITE + 8) * 48 + k])
                return fail(c, "the palettes: this version has no sprite cset 8");

    for(int k = 0; k < n; ++k)
    {
        const byte *p = in.take(48);
        int at = cset_place(c, k);
        bool same = at < 0 || !memcmp(colordata + at * 48, &s.colors[at * 48], 48);

        if((same && !c.force) || at < 0)
            o.put(p, 48);
        else
            o.put(colordata + at * 48, 48);
    }

    if(c.bf(76))
    {
        if(memcmp(&palnames[0][0], &s.palnames[0], MAXLEVELS * 17))
            return fail(c, "the palettes: this version has no palette names");

        return true;
    }

    for(int k = OLDMAXLEVELS * 17; k < MAXLEVELS * 17; ++k)
        if((&palnames[0][0])[k])
            return fail(c, "the palettes: this version names only %d palettes", OLDMAXLEVELS);

    const byte *p = in.take(OLDMAXLEVELS * 17);

    if(!c.force && !memcmp(&palnames[0][0], &s.palnames[0], OLDMAXLEVELS * 17))
        o.put(p, OLDMAXLEVELS * 17);
    else
        o.put(&palnames[0][0], OLDMAXLEVELS * 17);

    return true;
}

// ---- tiles ----------------------------------------------------------------------------------

// The loader swaps a few tiles around (readtiles): mem[t] = file[idx[t]] afterwards.
void tile_swaps(const Ctx &c, bool bszelda, bool newenemytiles, int floattile, std::vector<int> &idx)
{
    idx.resize(NEWMAXTILES);

    for(int t = 0; t < NEWMAXTILES; ++t)
        idx[t] = t;

    if(c.bf(186) && bszelda)
    {
        int t = idx[23];
        idx[23] = idx[24];
        idx[24] = idx[25];
        idx[25] = idx[26];
        idx[26] = t;

        if(floattile >= 0 && floattile + 12 < NEWMAXTILES)
        {
            t = idx[floattile + 11];
            idx[floattile + 11] = idx[floattile + 12];
            idx[floattile + 12] = t;
        }
    }

    if(c.v < 0x211 && !newenemytiles)
    {
        int t = idx[130];
        idx[130] = idx[132];
        idx[132] = t;
        t = idx[131];
        idx[131] = idx[133];
        idx[133] = t;
    }
}

bool tile_blank(int t)
{
    const tiledata &td = newtilebuf[t];

    if(td.format != tf4Bit || !td.data)
        return false;

    for(int k = 0; k < tilesize(tf4Bit); ++k)
        if(td.data[k])
            return false;

    return true;
}

bool do_tiles(Ctx &c, In &in, Out &o)
{
    const Snap &s = g_snap;
    int n0 = 0, n1 = 0;

    if(c.h0.tiles)
        n0 = c.v < 0x174 ? TILES_PER_PAGE * 4 : (c.v < 0x191 ? OLDMAXTILES : in.w());

    std::vector<int> idx, idx0;
    tile_swaps(c, get_bit(quest_rules, qr_BSZELDA) != 0, get_bit(quest_rules, qr_NEWENEMYTILES) != 0, wpnsbuf[iwSwim].tile, idx);
    tile_swaps(c, get_bit((byte *)s.rules, qr_BSZELDA) != 0, get_bit((byte *)s.rules, qr_NEWENEMYTILES) != 0, s.weapons[iwSwim].tile, idx0);
    bool same_swaps = idx == idx0;

    if(!c.h1.tiles)
    {
        if(c.h0.tiles)
            return fail(c, "the tiles: the header says there are none");

        return true;        // default tiles, unchanged (else the header says tiles follow)
    }

    if(c.v < 0x191)
        n1 = c.v < 0x174 ? TILES_PER_PAGE * 4 : OLDMAXTILES;
    else
    {
        // the tiles the file had, and as many more as the last one ZQuest uses
        n1 = n0;

        for(int t = 0; t < NEWMAXTILES; ++t)
            if(idx[t] >= n1 && !tile_blank(t))
                n1 = idx[t] + 1;

        if(n1 > 65535)
            n1 = 65535;

        o.w(n1);
    }

    std::vector<int> inv(NEWMAXTILES);

    for(int t = 0; t < NEWMAXTILES; ++t)
        inv[idx[t]] = t;

    for(int t = 0; t < NEWMAXTILES; ++t)
        if(idx[t] >= n1 && !tile_blank(t))
            return fail(c, "tile %d: this version holds only %d tiles", t, n1);

    int nmax = n0 > n1 ? n0 : n1;

    for(int k = 0; k < nmax; ++k)
    {
        const byte *p = k < n0 ? in.take(128) : NULL;

        if(k >= n1)
            continue;

        int t = inv[k];
        const tiledata &td = newtilebuf[t];

        if(td.format != tf4Bit || !td.data)
            return fail(c, "tile %d: this version has only 16-colour tiles", t);

        bool same = p && same_swaps && tile_hash(t) == s.tiles[t];

        if(same && !c.force)
            o.put(p, 128);
        else
            o.put(td.data, 128);
    }

    return true;
}

// ---- tunes ----------------------------------------------------------------------------------

bool skip_midi(In &in)
{
    in.take(2);

    for(int k = 0; k < MIDI_TRACKS && !in.bad; ++k)
    {
        dword len = in.ml();

        if((long)len > 0)
            in.take(len);
    }

    return !in.bad;
}

bool encode_tune(Ctx &c, Out &o, const Raw &raw, int i)
{
    const zctune &t = customtunes[i];
    In r(raw.p, raw.n);

    if(!t.data || t.format != MFORMAT_MIDI)
        return fail(c, "tune %d: this version holds only MIDIs", i + 1);

    if(t.flags)
        return fail(c, "tune %d: this version has no tune flags", i + 1);

    // the title: 20 characters, as the loader copies it (up to the first zero)
    const byte *rt = r.take(20);
    char back[36];
    memset(back, 0, 36);
    strncpy(back, (const char *)rt, 20);

    if(!memcmp(back, t.title, 36))
        o.put(rt, 20);
    else
    {
        for(int k = 20; k < 36; ++k)
            if(t.title[k])
                return fail(c, "tune %d: this version holds titles of 20 characters", i + 1);

        o.put(t.title, 20);
    }

    o.l(t.start);
    o.l(t.loop_start);
    o.l(t.loop_end);
    o.w(t.loop);
    o.w(t.volume);
    r.take(16);
    o.put(r.take(4), 4);
    const MIDI *m = (const MIDI *)t.data;
    o.mw(m->divisions);

    for(int k = 0; k < MIDI_TRACKS; ++k)
    {
        o.ml(m->track[k].len);

        if(m->track[k].len > 0)
            o.put(m->track[k].data, m->track[k].len);
    }

    return true;
}

bool do_tunes(Ctx &c, In &in, Out &o)
{
    int n = c.bf(178) ? MAXCUSTOMMIDIS192b177 : MAXCUSTOMTUNES;

    for(int i = n; i < MAXCUSTOMTUNES; ++i)
        if(customtunes[i].data || g_snap.tunes[i] != tune_hash(i))
            return fail(c, "tune %d: this version holds only %d tunes", i + 1, n);

    for(int i = 0; i < n; ++i)
    {
        bool had = get_bit(c.h0.midi, i) != 0, has = get_bit(c.h1.midi, i) != 0;
        const byte *p = NULL;
        size_t len = 0;

        if(had)
        {
            size_t at = in.at;
            in.take(40);

            if(!skip_midi(in))
                return fail(c, "tune %d: can't be read", i + 1);

            p = in.p + at;
            len = in.at - at;
        }

        if(has != (customtunes[i].data != NULL))
            return fail(c, "tune %d: the tune flags don't match the tunes", i + 1);

        if(!has)
            continue;

        bool same = had && tune_hash(i) == g_snap.tunes[i];

        if(same && !c.force)
        {
            o.put(p, len);
            continue;
        }

        Raw raw(p, had ? 40 : 40);

        if(!encode_tune(c, o, raw, i))
            return false;
    }

    return true;
}

// ---- cheats ---------------------------------------------------------------------------------

bool do_cheats(Ctx &c, In &in, Out &o)
{
    const byte *p = c.h0.cheats ? in.take(168) : NULL;

    if(!c.h1.cheats)
    {
        if(!cheats_empty())
            return fail(c, "the cheats: the header says there are none");

        return true;
    }

    if(p && !c.force && !memcmp(&zcheats, &g_snap.cheats, sizeof(ZCHEATS)))
    {
        o.put(p, 168);
        return true;
    }

    o.l(zcheats.flags);
    o.put(zcheats.codes, sizeof(zcheats.codes));
    return true;
}

// ---- init data ------------------------------------------------------------------------------

// The item families of the init data, in file order (readinitdata, pre-1.93)
struct InitFamily
{
    int family;
    bool shifted;       // rings and bracelets start at level 2
    bool converted;     // before build 174 a level number, not a bit string (but not the potion)
};

const InitFamily g_fam_a[7] = { { itype_ring, true, true }, { itype_sword, false, true }, { itype_shield, false, true },
                                { itype_wallet, false, true }, { itype_bracelet, true, true }, { itype_amulet, false, true },
                                { itype_bow, false, true } };
const InitFamily g_fam_b[5] = { { itype_candle, false, true }, { itype_brang, false, true }, { itype_arrow, false, true },
                                { itype_potion, false, false }, { itype_whistle, false, true } };
const InitFamily g_fam_c[9] = { { itype_wand, false, false }, { itype_letter, false, false }, { itype_lens, false, false },
                                { itype_hookshot, false, false }, { itype_bait, false, false }, { itype_hammer, false, false },
                                { itype_dinsfire, false, false }, { itype_faroreswind, false, false },
                                { itype_nayruslove, false, false } };
const int g_items6[6] = { iRaft, iLadder, iBook, iMKey, iFlippers, iBoots };
const int g_equip_bits[6] = { idE_RAFT, idE_LADDER, idE_BOOK, idE_KEY, idE_FLIPPERS, idE_BOOTS };
const int g_items_ids[6] = { iWand, iLetter, iLens, iHookshot, iBait, iHammer };
const int g_items_bits[6] = { idI_WAND, idI_LETTER, idI_LENS, idI_HOOKSHOT, idI_BAIT, idI_HAMMER };
const int g_items2_ids[3] = { iDinsFire, iFaroresWind, iNayrusLove };
const int g_items2_bits[3] = { idI_DFIRE, idI_FWIND, idI_NLOVE };

// The stored item bytes of an init record
struct InitItems
{
    byte six[6];
    byte a[7], b[5], cc[9];
    byte equip, items, items2;
};

// The levels a stored byte gives a family, as readinitdata works them out
int family_levels(const Ctx &c, const InitFamily &f, byte stored, bool &ok)
{
    ok = true;
    signed char x = (signed char)stored;
    int lv = x;

    if(c.bf(174) && f.converted)
    {
        if(x < 0 || x > 8)
        {
            ok = false;
            return 0;
        }

        lv = (signed char)(x ? (1 << (x - 1)) : 0);
    }

    if(f.shifted)
        lv = (signed char)(lv << 1);

    return lv;
}

void simulate_items(const Ctx &c, const InitItems &ii, zinitdata &z, bool &ok)
{
    ok = true;

    for(int i = 0; i < MAXITEMS; ++i)
        z.items[i] = false;

    bool fok;

    if(!c.bf(174))
        for(int k = 0; k < 6; ++k)
            z.items[g_items6[k]] = ii.six[k] != 0;

    for(int k = 0; k < 7; ++k)
    {
        addOldStyleFamily(&z, itemsbuf, g_fam_a[k].family, (char)family_levels(c, g_fam_a[k], ii.a[k], fok));
        ok = ok && fok;
    }

    for(int k = 0; k < 5; ++k)
    {
        addOldStyleFamily(&z, itemsbuf, g_fam_b[k].family, (char)family_levels(c, g_fam_b[k], ii.b[k], fok));
        ok = ok && fok;
    }

    if(!c.bf(174))
        for(int k = 0; k < 9; ++k)
            addOldStyleFamily(&z, itemsbuf, g_fam_c[k].family, (char)ii.cc[k]);

    if(c.bf(174))
    {
        for(int k = 0; k < 6; ++k)
            z.items[g_items6[k]] = get_bit((byte *)&ii.equip, g_equip_bits[k]) != 0;

        for(int k = 0; k < 6; ++k)
            z.items[g_items_ids[k]] = get_bit((byte *)&ii.items, g_items_bits[k]) != 0;

        for(int k = 0; k < 3; ++k)
            z.items[g_items2_ids[k]] = get_bit((byte *)&ii.items2, g_items2_bits[k]) != 0;
    }

    if(c.v < 0x211)
    {
        int sshield = getItemID(itemsbuf, itype_shield, i_smallshield);

        if(sshield != -1)
            z.items[sshield] = true;
    }
}

bool items_match(const zinitdata &a, const zinitdata &b)
{
    return !memcmp(a.items, b.items, sizeof(a.items));
}

// The stored byte for a family's starting items, from ZQuest's data
bool family_stored(const Ctx &c, const InitFamily &f, byte &out)
{
    int levels = 0;

    for(int lv = 1; lv <= 8; ++lv)
    {
        int id = getItemID(itemsbuf, f.family, lv);

        if(id != -1 && zinit.items[id])
            levels |= 1 << (lv - 1);
    }

    if(f.shifted)
    {
        if(levels & 1)
            return false;

        levels >>= 1;
    }

    if(c.bf(174) && f.converted)
    {
        if(levels == 0)
            out = 0;
        else
        {
            int bit = 0;

            while(!(levels & (1 << bit)))
                ++bit;

            if(levels != (1 << bit))
                return false;       // one level of each family only

            out = (byte)(bit + 1);
        }

        return true;
    }

    out = (byte)levels;
    return true;
}

bool init_same()
{
    if(memcmp(&zinit, &g_snap.init, sizeof(zinitdata)))
        return false;

    static const int ids[5] = { iSword, iWSword, iMSword, iXSword, iHookshot };

    for(int k = 0; k < 5; ++k)
    {
        const itemdata &a = itemsbuf[ids[k]], &b = g_snap.items[ids[k]];

        if(a.pickup_hearts != b.pickup_hearts || a.misc1 != b.misc1 || a.misc2 != b.misc2 || (a.flags ^ b.flags) & ITEM_FLAG1)
            return false;
    }

    return true;
}

bool encode_init(Ctx &c, Out &o, const Raw &raw)
{
    const zinitdata &z = zinit;
    In r(raw.p, raw.n);
    bool newer = !c.bf(174);
    bool contfull = get_bit(c.h1.rules, qr_CONTFULL_DEP) != 0;

    // what this version can't hold must be as loaded
    zinitdata t = z;
    const zinitdata &s = g_snap.init;
    memcpy(t.items, s.items, sizeof(t.items));
    t.bombs = s.bombs; t.super_bombs = s.super_bombs; t.hc = s.hc; t.start_heart = s.start_heart;
    t.cont_heart = s.cont_heart; t.hcp = s.hcp; t.max_bombs = s.max_bombs; t.keys = s.keys; t.rupies = s.rupies;
    t.triforce = s.triforce; memcpy(t.map, s.map, 32); memcpy(t.compass, s.compass, 32);
    memcpy(t.boss_key, s.boss_key, 32); memcpy(t.misc, s.misc, 16); t.last_map = s.last_map;
    t.last_screen = s.last_screen; t.max_magic = s.max_magic; t.magic = s.magic; t.msg_more_x = s.msg_more_x;
    t.msg_more_y = s.msg_more_y; t.subscreen = s.subscreen;

    if(memcmp(&t, &s, sizeof(zinitdata)))
        return fail(c, "the init data: something changed that this version sets itself");

    // the starting items: the file's bytes while they still give ZQuest's items
    InitItems fromraw, fromdata;
    memset(&fromraw, 0, sizeof(fromraw));
    memset(&fromdata, 0, sizeof(fromdata));
    {
        In q(raw.p, raw.n);

        if(newer)
            memcpy(fromraw.six, q.take(6), 6);

        memcpy(fromraw.a, q.take(7), 7);

        if(newer)
            q.take(32);

        memcpy(fromraw.b, q.take(5), 5);
        q.take(2);

        if(newer)
            memcpy(fromraw.cc, q.take(9), 9);
        else
        {
            fromraw.equip = (byte)q.c();
            fromraw.items = (byte)q.c();
            q.take(9 + 64 + 16 + 4 + 2 + 2 + 5 + 5 + 3 + 32);
            fromraw.items2 = (byte)q.c();
        }
    }

    zinitdata sim = z;
    bool ok;
    simulate_items(c, fromraw, sim, ok);
    const InitItems *ii = &fromraw;

    if(!ok || !items_match(sim, z))
    {
        bool good = true;

        for(int k = 0; k < 6; ++k)
            fromdata.six[k] = z.items[g_items6[k]] ? (fromraw.six[k] ? fromraw.six[k] : 1) : 0;

        for(int k = 0; k < 7 && good; ++k)
            good = family_stored(c, g_fam_a[k], fromdata.a[k]);

        for(int k = 0; k < 5 && good; ++k)
            good = family_stored(c, g_fam_b[k], fromdata.b[k]);

        for(int k = 0; k < 9 && good; ++k)
            good = family_stored(c, g_fam_c[k], fromdata.cc[k]);

        fromdata.equip = fromraw.equip;
        fromdata.items = fromraw.items;
        fromdata.items2 = fromraw.items2;

        for(int k = 0; k < 6; ++k)
        {
            set_bit(&fromdata.equip, g_equip_bits[k], z.items[g_items6[k]] ? 1 : 0);
            set_bit(&fromdata.items, g_items_bits[k], z.items[g_items_ids[k]] ? 1 : 0);
        }

        for(int k = 0; k < 3; ++k)
            set_bit(&fromdata.items2, g_items2_bits[k], z.items[g_items2_ids[k]] ? 1 : 0);

        if(good)
            simulate_items(c, fromdata, sim, ok);

        if(!good || !ok || !items_match(sim, z))
            return fail(c, "the init data: this version can't start the player with these items");

        ii = &fromdata;
    }

    // the sword and hookshot values the init data keeps for the items
    const int swords[4] = { iSword, iWSword, iMSword, iXSword };

    for(int k = 0; k < 4; ++k)
    {
        const itemdata &it = itemsbuf[swords[k]];

        if(it.pickup_hearts > 255 || it.misc1 < 0 || it.misc1 > 255 || it.misc2 < 0 || it.misc2 > 255)
            return fail(c, "item %d: this version holds sword hearts and beam values up to 255", swords[k]);
    }

    if(itemsbuf[iHookshot].misc2 < 0 || itemsbuf[iHookshot].misc2 > 255)
        return fail(c, "item %d: this version holds hookshot links up to 255", iHookshot);

    if(z.start_heart > 255 || z.cont_heart > 255 || z.max_magic % MAGICPERBLOCK || z.magic % MAGICPERBLOCK ||
       z.max_magic / MAGICPERBLOCK > 255 || z.magic / MAGICPERBLOCK > 255)
        return fail(c, "the init data: hearts up to 255, magic in whole blocks up to 255");

    if(newer)
    {
        o.put(ii->six, 6);
        r.take(6);
    }

    o.put(ii->a, 7);
    r.take(7);

    if(newer)
        o.put(r.take(32), 32);

    o.put(ii->b, 5);
    r.take(5);
    o.c(z.bombs);
    o.c(z.super_bombs);
    r.take(2);

    if(newer)
    {
        o.put(ii->cc, 9);
        r.take(9);
        o.c(r.c());
        o.put(r.take(32), 32);
    }
    else
    {
        o.c(ii->equip);
        o.c(ii->items);
        r.take(2);
    }

    o.c(z.hc);
    r.c();
    int rstart = r.c();
    int rcont = r.c();
    o.c(contfull ? rstart : z.start_heart);
    o.c(contfull ? rcont : z.cont_heart);
    o.c(z.hcp);
    o.c(z.max_bombs);
    o.c(z.keys);
    o.w(z.rupies);
    o.c(z.triforce);
    r.take(6);
    o.put(z.map, 32);
    o.put(z.compass, 32);
    r.take(64);

    if(newer)
    {
        o.put(z.boss_key, 32);
        r.take(32);
    }

    const byte *rmisc = r.take(16);
    byte misc16[16];
    memcpy(misc16, z.misc, 16);

    if(contfull)
        set_bit(misc16, idM_CONTPERCENT, get_bit((byte *)rmisc, idM_CONTPERCENT));

    o.put(misc16, 16);

    for(int k = 0; k < 4; ++k)
        o.c(itemsbuf[swords[k]].pickup_hearts);

    r.take(4);
    o.c(z.last_map);
    o.c(z.last_screen);
    o.c(z.max_magic / MAGICPERBLOCK);
    o.c(z.magic / MAGICPERBLOCK);
    r.take(4);

    for(int k = 0; k < 4; ++k)
        o.c(itemsbuf[swords[k]].misc1);

    r.take(4);
    byte percent = (byte)r.c();

    for(int k = 0; k < 4; ++k)
        set_bit(&percent, k, (itemsbuf[swords[k]].flags & ITEM_FLAG1) ? 1 : 0);

    o.c(percent);

    for(int k = 0; k < 4; ++k)
        o.c(itemsbuf[swords[k]].misc2);

    r.take(4);
    o.c(itemsbuf[iHookshot].misc2);
    r.c();
    o.c(z.msg_more_x);
    o.c(z.msg_more_y);
    r.take(2);
    int rsub = r.c();
    o.c(c.bf(168) ? rsub : z.subscreen);

    if(!newer)
    {
        o.put(z.boss_key, 32);
        r.take(32);
    }
    else
        o.put(r.take(2), 2);        // the start dmap and animation style: set by the loader

    if(!newer)
    {
        o.c(ii->items2);
        r.c();
    }

    o.put(r.take(96), 96);

    if(newer)
        o.put(r.take(2), 2);

    return true;
}

bool do_init(Ctx &c, In &in, Out &o)
{
    if(c.bf(27))
    {
        // Nothing stored: everything comes from defaults and rules (the subscreen from a rule).
        // The last map and screen are where the editor stands (writeinitdata sets them), not
        // quest data, and 1.90 starts them at 0.
        zinitdata t = zinit;
        t.subscreen = g_snap.init.subscreen;
        t.last_map = g_snap.init.last_map;
        t.last_screen = g_snap.init.last_screen;

        if(memcmp(&t, &g_snap.init, sizeof(zinitdata)))
            return fail(c, "the init data: this version has none of its own");

        return true;
    }

    size_t len = c.bf(174) ? 255 : 336;
    const byte *p = in.take(len);
    Raw raw(p, len);
    bool same = init_same();

    if(same && !c.force && !c.selftest)
    {
        o.put(p, len);
        return true;
    }

    size_t start = o.b.size();

    if(!encode_init(c, o, raw))
        return false;

    if(o.b.size() - start != len)
        return fail(c, "the init data: wrote %d bytes, not %d", (int)(o.b.size() - start), (int)len);

    if(same && !c.force)
    {
        selftest_note(c, "init", 0, raw, o, start);
        o.b.resize(start);
        o.put(p, len);
    }

    return true;
}

// ---- the writers --------------------------------------------------------------------------

bool setup(Ctx &c, const ZqPartContext &ctx, int &rc)
{
    c.v = ctx.zelda_version;
    c.b = ctx.build;
    c.force = zqw_force_serialize;
    c.selftest = getenv("ZQW_FLAT_SELFTEST") != NULL;

    if(c.v >= 0x193 || !supported(c.v, c.b))
    {
        rc = ZQW_UNSUPPORTED;
        return false;
    }

    if(!g_snap.valid || g_snap.v != c.v || g_snap.b != c.b)
    {
        flat_refuse("no copy of the quest as loaded");
        rc = 1;
        return false;
    }

    c.h0 = g_snap.h;
    return true;
}

int write_flat_header(PACKFILE *f, const ZqPartContext &ctx)
{
    Ctx c;
    int rc = 0;

    if(!setup(c, ctx, rc))
        return rc;

    std::vector<byte> h;

    if(!compute_header(c, h))
    {
        flat_refuse("can't write the header: %s", c.why.c_str());
        return 2;
    }

    return pack_fwrite(&h[0], (long)h.size(), f) == (long)h.size() ? ZQW_OK : 3;
}

int write_flat_body(PACKFILE *f, const ZqPartContext &ctx)
{
    Ctx c;
    int rc = 0;

    if(!setup(c, ctx, rc))
        return rc;

    // The header in the file must say what this body needs (it holds the counts and rules).
    std::vector<byte> h, inhdr;
    const ZqQuestPart *hp = find_part(ID_HEADER);

    if(!compute_header(c, h))
    {
        flat_refuse("can't write the quest data: %s", c.why.c_str());
        return 2;
    }

    if(!hp || !read_part(*hp, inhdr) || inhdr != h)
    {
        flat_refuse("can't write the quest data: the header has to be written with it (it holds the rules and counts)");
        return 4;
    }

    header_info(h, c.v, c.b, c.h1);
    std::vector<byte> body;

    if(!read_part(*ctx.part, body) || body.empty())
    {
        flat_refuse("can't read the quest data as loaded");
        return 5;
    }

    // what this version has no room for at all
    reset_combo_animations();
    reset_combo_animations2();

    if(memcmp(guysbuf, &g_snap.guys[0], sizeof(guydata) * MAXGUYS))
        c.why = "the enemies: this version has no enemy editor data (enemies come from defaults and rules)";
    else
    {
        for(int i = 0; i < eMAXGUYS && c.why.empty(); ++i)
            if(memcmp(guy_string[i], &g_snap.guy_names[i * 64], 64))
                c.why = "the enemies: this version has no enemy names";
    }

    if(c.why.empty() && others_hash() != g_snap.others)
        c.why = "subscreens, sounds, Link's sprites, item drop sets, combo aliases, scripts or favourite combos: this version has none";

    In in(&body[0], body.size());
    Out o;
    o.b.reserve(body.size() + 4096);
    typedef bool (*Area)(Ctx &, In &, Out &);
    static const Area areas[] = { do_strings, do_doors, do_dmaps, do_misc, do_items, do_weapons, do_maps, do_combos,
                                  do_colors, do_tiles, do_tunes, do_cheats, do_init };
    static const char *names[] = { "strings", "doors", "dmaps", "misc", "items", "weapons", "maps", "combos",
                                   "colors", "tiles", "tunes", "cheats", "init" };
    bool trace = getenv("ZQW_FLAT_TRACE") != NULL;
    bool ok = c.why.empty();

    for(size_t k = 0; k < sizeof(areas) / sizeof(areas[0]) && ok; ++k)
    {
        ok = areas[k](c, in, o);

        if(trace)
            flat_log("walk %s end %d -> %d%s", names[k], (int)in.at, (int)o.b.size(), ok ? "" : " (failed)");
    }

    if(ok && (in.bad || in.at != in.n))
    {
        ok = false;
        fail(c, "the quest data as loaded doesn't read the way these writers think (%d of %d bytes)", (int)in.at, (int)in.n);
    }

    if(!ok)
    {
        flat_refuse("can't write the quest data: %s", c.why.c_str());
        return 6;
    }

    return pack_fwrite(&o.b[0], (long)o.b.size(), f) == (long)o.b.size() ? ZQW_OK : 7;
}

ZqWriterRegistration g_reg_flat_header(ID_HEADER, write_flat_header, "pre-1.93 header (zqw_flat)");
ZqWriterRegistration g_reg_flat_body(0, write_flat_body, "pre-1.93 quest data (zqw_flat)");
ZqLoadHookRegistration g_hook_flat(flat_note_loaded);

} // namespace

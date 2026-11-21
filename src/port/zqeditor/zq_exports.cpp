// See zq_exports.h.
//
// Where each file's format comes from (upstream's zq_files.cpp, zq_class.cpp, zq_tiles.cpp and
// zquest.cpp, 2.55.17 and 3.0 alike unless said):
// - .zdmap (writesomedmaps): -1 (V_ZDMAP), the program's version (0x255) and build (61), the DMap
//   section's version and cversion, the most DMaps there are (512), the first and last DMap
//   written and last - first, then each DMap's record as the DMap section holds it.
// - .zcombo (writecombofile): the program's version and build, the combo section's version and
//   cversion, the first combo and how many, then each combo's record as the combo section holds
//   it (from version 33). File > Export > Combo Set writes every combo.
// - .ztileset (writetilefile): the program's version and build, the tile section's version
//   (3.0: 4) and cversion, the first tile and how many, then each tile: 3.0 a format byte, 0 for
//   a blank tile, then a 4-bit tile packed two pixels a byte or an 8-bit one's 256 bytes; 2.55.17
//   (version 3) every tile as its format and its unpacked bytes (256 for a 4-bit tile).
// - .zqs (save_msgstrs), .guy (save_guys): the whole strings or enemy section, its id first.
// - .zgp (save_zgp): 'GPAK', version 1, cversion 1, then the tile, combo, palette, item, weapon
//   sprite, game icon, misc colour and door set sections, each with its id first.
// Every one is written as a plain file (no packing, no password).
//
// The sections come from what the quest's own-version save would write now (zq_own_part_bytes),
// so edits made since the load are in them and they are in the quest's own section versions.

#include "precompiled.h" //always first
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <sys/types.h>
#endif

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "tiles.h"
#include "zq_class.h"
#include "zq_writers.h"
#include "zq_q255_store.h"
#include "zq_exports.h"
#include "q255.h"
#include "q30_triggers.h"
#include "zq_tape.h"
#include "zqn_255.h"

extern zquestheader header;
extern word msg_count;
extern MsgStr *MsgStrings;
extern int msg_strings_size;
extern char *guy_string[];
extern char *item_string[];
extern itemdata *itemsbuf;
extern guydata *guysbuf;
extern dmap *DMaps;
extern newcombo *combobuf;
extern wpndata *wpnsbuf;
extern miscQdata misc;      // ZQuest's (zquest.cpp)
extern byte *colordata;
extern char palnames[MAXLEVELS][17];
extern DoorComboSet *DoorComboSets;

extern int q255_header_version[3];
extern int q255_header_build;
extern char q255_quest_built_with[48];
extern int q255_header_version_ids[8];
extern bool q255_header_nightly;
extern std::string q255_header_version_text;

// The area writers' access for these files (zqw_combos.cpp, zqw_maps.cpp, zqw_guys.cpp,
// zqw_items.cpp).
bool zq255_combo_export(int first, int count, std::vector<byte> &recs, word *version, std::string &why);
bool zq255_combo_import(const std::vector<byte> &data, word v, int count, int dest, bool whole, int *placed, std::string &why);
bool zq255_dmap_export(int first, int last, std::vector<byte> &recs, word *version, word *cversion, std::string &why);
bool zq255_dmap_split(const std::vector<byte> &data, size_t at, word v, int n, std::vector<std::vector<byte> > &recs);
int zq255_dmap_version();
bool zq255_dmap_commit(int i, const std::vector<byte> &rec, std::string &why);
bool zq255_dmap_record(int i, std::vector<byte> &rec, int *version);
int zq255_guys_version();
bool zq255_guys_take(const std::vector<byte> &sec, std::string &why);
int zq255_items_version(int *count);
bool zq255_items_take(const std::vector<byte> &sec, std::string &why);
void zq255_strings_rebased();
bool zq255_item_record(int i, std::vector<byte> &rec, int *version);
bool zq255_item_commit(int i, const std::vector<byte> &rec, std::string &why);
bool zq255_guy_record(int i, std::vector<byte> &rec, int *version);
bool zq255_guy_commit(int i, const std::vector<byte> &rec, std::string &why);

namespace
{

// An import put a whole section in as the quest's part (zq_part_rebase): the quest's file
// doesn't hold it yet although nothing counts as changed, so a timed save is due. Cleared after
// every load and every save in the quest's own version (timed saves too).
bool g_import_pending = false;

void forget_import_pending()
{
    g_import_pending = false;
}

ZqLoadHookRegistration g_hook_import(forget_import_pending);

const word UP_VERSION = 0x255;      // what upstream's 2.55.17 and 3.0 write as their program's
const dword UP_BUILD = 61;          // version and build (ZELDA_VERSION, VERSION_BUILD)
const int ZDMAP_KIND = -1;          // V_ZDMAP
const int MAX_ZDMAPS = 512;         // MAXDMAPS, both versions

// ---- little helpers ----------------------------------------------------------------------

struct Out
{
    std::vector<byte> b;

    void p8(byte v) { b.push_back(v); }
    void p16(word v) { p8((byte)v); p8((byte)(v >> 8)); }
    void p32(dword v) { p16((word)v); p16((word)(v >> 16)); }
    void m32(dword v) { p8((byte)(v >> 24)); p8((byte)(v >> 16)); p8((byte)(v >> 8)); p8((byte)v); }
    void pn(const std::vector<byte> &v) { b.insert(b.end(), v.begin(), v.end()); }
};

struct In
{
    const std::vector<byte> &d;
    size_t pos;
    bool bad;

    In(const std::vector<byte> &v, size_t at = 0) : d(v), pos(at), bad(false) {}
    bool has(size_t n) const { return !bad && pos <= d.size() && n <= d.size() - pos; }
    dword get(int n)
    {
        if(!has((size_t)n)) { bad = true; return 0; }
        dword v = 0;
        for(int k = 0; k < n; ++k) v |= (dword)d[pos + k] << (8 * k);
        pos += (size_t)n;
        return v;
    }
    word w() { return (word)get(2); }
    dword l() { return get(4); }
    dword m32()
    {
        if(!has(4)) { bad = true; return 0; }
        dword v = ((dword)d[pos] << 24) | ((dword)d[pos + 1] << 16) | ((dword)d[pos + 2] << 8) | (dword)d[pos + 3];
        pos += 4;
        return v;
    }
};

bool write_file(const char *path, const std::vector<byte> &b, std::string &why)
{
    FILE *f = fopen(path, "wb");

    if(!f)
    {
        why = "the file couldn't be written";
        return false;
    }

    bool ok = b.empty() || fwrite(&b[0], 1, b.size(), f) == b.size();

    if(fclose(f) != 0)
        ok = false;

    if(!ok)
    {
        remove(path);
        why = "the file couldn't be written";
    }

    return ok;
}

bool read_file(const char *path, std::vector<byte> &b, std::string &why)
{
    b.clear();
    FILE *f = fopen(path, "rb");

    if(!f)
    {
        why = "the file couldn't be opened";
        return false;
    }

    byte buf[65536];
    size_t n;

    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
        b.insert(b.end(), buf, buf + n);

    fclose(f);
    return true;
}

// A section as the quest's own-version save writes it now: version, cversion, size and data.
bool own_section(dword id, std::vector<byte> &sec, std::string &why)
{
    const ZqQuestPart *p = zq_find_part(id);

    if(!p || !zq_own_part_bytes(*p, sec) || sec.size() < 8)
    {
        why = "this quest's " + std::string(id == ID_STRINGS ? "strings" : id == ID_GUYS ? "enemies" : id == ID_TILES ? "tiles" :
              id == ID_COMBOS ? "combos" : id == ID_CSETS ? "palettes" : id == ID_ITEMS ? "items" : id == ID_WEAPONS ? "weapon sprites" :
              id == ID_ICONS ? "game icons" : id == ID_COLORS ? "misc colours" : id == ID_DOORS ? "door sets" : "data") +
              " can't be written" + (zqw_last_error.empty() ? std::string("") : ": " + zqw_last_error);
        return false;
    }

    return true;
}

// The combo section for a .zgp: as the quest's own save writes it, but one in an early 2.55
// format (before version 33) as saving the quest as 2.55.17 converts it (version 45), as the
// .zcombo export does: what 2.55.17 itself writes for that quest, and what ZQuest's import reads.
bool combo_section(std::vector<byte> &sec, std::string &why)
{
    if(!own_section(ID_COMBOS, sec, why))
        return false;

    const word v = (word)(sec[0] | (sec[1] << 8));

    if(v < 7 || v >= 33)
        return true;

    std::vector<byte> conv;

    if(!zq_newer_section_bytes(ID_COMBOS, ZQ_NEWER_255, conv, why) || conv.size() < 12)
        return false;

    sec.assign(conv.begin() + 4, conv.end());
    return true;
}

// A section (version, cversion, size, data) the way the readers take it, from a file's bytes at
// in.pos (just after its id). False if it runs past the end.
bool take_section(In &in, word *v, word *cv, std::vector<byte> &data)
{
    *v = in.w();
    *cv = in.w();
    dword size = in.l();

    if(in.bad || !in.has(size))
        return false;

    data.assign(in.d.begin() + in.pos, in.d.begin() + in.pos + size);
    in.pos += size;
    return true;
}

// The whole section again: version, cversion, size, data.
std::vector<byte> whole_section(word v, word cv, const std::vector<byte> &data)
{
    Out o;
    o.p16(v);
    o.p16(cv);
    o.p32((dword)data.size());
    o.pn(data);
    return o.b;
}

// Whether the quest's own save would still write this section (a dry run of its writer): an
// import that brought in something the quest's version can't hold says so straight away.
bool section_still_saves(dword id, std::string &why)
{
    const ZqQuestPart *p = zq_find_part(id);

    if(!p)
        return true;

    std::vector<byte> out;
    PACKFILE *f = zq_open_vector_sink(&out);

    if(!f)
        return true;

    zqw_last_error.clear();
    int rc = zq_write_part(f, *p);
    pack_fclose(f);

    if(rc == ZQW_OK || rc == ZQW_UNSUPPORTED)
        return true;

    why = zq_part_name(*p) + " can't hold it" + (zqw_last_error.empty() ? std::string("") : ": " + zqw_last_error);
    return false;
}

const char *section_tag(dword id)
{
    static char t[5];

    for(int k = 0; k < 4; ++k)
        t[k] = (char)(id >> (24 - 8 * k));

    t[4] = 0;
    return t;
}

// The open quest's section version (0: it has no such section).
int quest_section_version(dword id)
{
    const ZqQuestPart *p = zq_find_part(id);
    word v = 0, cv = 0;
    return p && zq_part_versions(*p, &v, &cv) ? v : 0;
}

// A file in a newer format than the quest's section: the quest is saved in its own version, which
// has no place for what the newer format added, so it stays out.
std::string newer_words(const char *what, int file_v, int quest_v)
{
    char b[200];
    snprintf(b, sizeof(b), "its %s are in format %d, newer than this quest's format %d, which can't hold them", what, file_v, quest_v);
    return b;
}

// ---- a section laid out in an older format of its line ------------------------------------------

// An enemy or item section's records as tapes, with the names a section holds in front of them
// (enemies always, items before format 67; 64 bytes each).
struct SecTapes
{
    int count;
    std::vector<std::string> names;
    std::vector<Tape> recs;
};

bool split_records(dword id, int v, const std::vector<byte> &data, SecTapes &s)
{
    const bool item = id == ID_ITEMS;
    size_t at = 0;
    s.names.clear();
    s.recs.clear();

    if(item)
    {
        if(data.size() < 2)
            return false;

        s.count = data[0] | (data[1] << 8);
        at = 2;
    }
    else
        s.count = MAXGUYS;

    if(!item || v < 67)
    {
        if(data.size() < at + 64 * (size_t)s.count)
            return false;

        for(int i = 0; i < s.count; ++i)
            s.names.push_back(std::string((const char *)&data[at + 64 * (size_t)i], 64));

        at += 64 * (size_t)s.count;
    }

    s.recs.resize((size_t)s.count);

    for(int i = 0; i < s.count; ++i)
    {
        TapeParser p(data.empty() ? NULL : &data[0], data.size(), at, &s.recs[i]);

        if(!(item ? zq_tape_item(p, v) : zq_tape_guy(p, v)) || p.bad)
            return false;

        at = p.pos;
    }

    return at == data.size();
}

// The format of the weapon data the quest's own enemies or items hold (upstream's weapon_data
// carries a format number of its own, in every record), -1 when they hold none.
int quest_weapon_data_version(dword id)
{
    std::vector<byte> rec;
    int rv = 0;

    if(!(id == ID_ITEMS ? zq255_item_record(0, rec, &rv) : zq255_guy_record(0, rec, &rv)) || rec.empty())
        return -1;

    Tape t;
    TapeParser p(&rec[0], rec.size(), 0, &t);

    if(!(id == ID_ITEMS ? zq_tape_item(p, rv) : zq_tape_guy(p, rv)) || p.bad || !t.find("wd.version"))
        return -1;

    return (int)t.get("wd.version");
}

// A record of the line's newest format, for the schema of an older one: the fields the older
// format keeps under another name (an enemy's script before format 59, its weapon's script before
// 54; an item's scripts before 71, its weapon's data before 63) take the newer field's value, and
// the weapon data is laid out in the format the quest's own records hold it in (wd: -1 none).
// What the older format has no field for stays behind; the proof in lay_out_older then tells
// whether that lost anything.
void older_keys(dword id, Tape &t, int v, int wd)
{
    char from[48], to[48];
    const bool config = t.find("wd.sc.script") != NULL;     // weapon data from its format 5

    if(id == ID_ITEMS && v < 71)
    {
        // one set of arguments for the three scripts
        zqn255_tape_set(t, "script", t.get("sc.action.script"));
        zqn255_tape_set(t, "collect_script", t.get("sc.pickup.script"));
        zqn255_tape_set(t, "sprite_script", t.get("sc.sprite.script"));

        for(int q = 0; q < 8; ++q)
        {
            snprintf(from, sizeof(from), "sc.action.initd%d", q);
            snprintf(to, sizeof(to), "initd%d", q);
            zqn255_tape_set(t, to, t.get(from));
        }
    }

    if(id == ID_ITEMS && v < 63)
    {
        // the weapon's data with the item
        static const char *same[] = { "imitate", "defdef", "hxofs", "hyofs", "hxsz", "hysz", "hzsz", "xofs", "yofs", "override",
                                      "tilew", "tileh", "moveflags" };

        for(size_t k = 0; k < sizeof(same) / sizeof(same[0]); ++k)
        {
            snprintf(from, sizeof(from), "wd.%s", same[k]);
            snprintf(to, sizeof(to), "w.%s", same[k]);
            zqn255_tape_set(t, to, t.get(from));
        }

        zqn255_tape_set(t, "w.script", t.get(config ? "wd.sc.script" : "wd.script"));

        for(int q = 0; q < 8; ++q)
        {
            snprintf(from, sizeof(from), config ? "wd.sc.initd%d" : "wd.initd%d", q);
            snprintf(to, sizeof(to), "w.initd%d", q);
            zqn255_tape_set(t, to, t.get(from));
        }

        for(int q = 0; q < 5; ++q)
        {
            snprintf(from, sizeof(from), "wd.burnspr%d", q);
            snprintf(to, sizeof(to), "burnspr%d", q);
            zqn255_tape_set(t, to, t.get(from));
            snprintf(from, sizeof(from), "wd.lightrad%d", q);
            snprintf(to, sizeof(to), "lightrad%d", q);
            zqn255_tape_set(t, to, t.get(from));
        }
    }

    // weapon data in the quest's own format of it: before 5 its script is a number and eight
    // arguments, not a script config
    if(wd >= 0 && t.find("wd.version") && wd < t.get("wd.version"))
    {
        if(wd < 5 && config)
        {
            zqn255_tape_set(t, "wd.script", t.get("wd.sc.script"));

            for(int q = 0; q < 8; ++q)
            {
                snprintf(from, sizeof(from), "wd.sc.initd%d", q);
                snprintf(to, sizeof(to), "wd.initd%d", q);
                zqn255_tape_set(t, to, t.get(from));
            }
        }

        zqn255_tape_set(t, "wd.version", wd);
    }

    if(id == ID_GUYS && v < 59)
    {
        zqn255_tape_set(t, "script", t.get("sc.script"));

        for(int q = 0; q < 8; ++q)
        {
            snprintf(from, sizeof(from), "sc.initd%d", q);
            snprintf(to, sizeof(to), "initd%d", q);
            zqn255_tape_set(t, to, t.get(from));
        }
    }

    if(id == ID_GUYS && v < 54)
    {
        // the weapon's script: weapon data keeps it as a script config from its own format 5
        zqn255_tape_set(t, "w.script", t.get(config ? "wd.sc.script" : "wd.script"));

        for(int q = 0; q < 8; ++q)
        {
            snprintf(from, sizeof(from), config ? "wd.sc.initd%d" : "wd.initd%d", q);
            snprintf(to, sizeof(to), "w.initd%d", q);
            zqn255_tape_set(t, to, t.get(from));
        }

        if(v >= 52)
        {
            // formats 52 and 53 hold these of the weapon's data with the enemy (the step speed in
            // hundredths of a pixel, where weapon data keeps ten-thousandths)
            static const char *same[] = { "unblockable", "moveflags", "override", "tilew", "tileh", "hxsz", "hysz", "hzsz",
                                          "hxofs", "hyofs", "xofs", "yofs" };

            for(size_t k = 0; k < sizeof(same) / sizeof(same[0]); ++k)
            {
                snprintf(from, sizeof(from), "wd.%s", same[k]);
                snprintf(to, sizeof(to), "w.%s", same[k]);
                zqn255_tape_set(t, to, t.get(from));
            }

            zqn255_tape_set(t, "w.step", t.get("wd.step") / 100);

            for(int q = 0; q < 5; ++q)
            {
                snprintf(from, sizeof(from), "wd.burnspr%d", q);
                snprintf(to, sizeof(to), "w.burnspr%d", q);
                zqn255_tape_set(t, to, t.get(from));
                snprintf(from, sizeof(from), "wd.lightrad%d", q);
                snprintf(to, sizeof(to), "w.lightrad%d", q);
                zqn255_tape_set(t, to, t.get(from));
            }
        }
    }
}

// Whether two fields of the same key hold the same.
bool same_field(const TapeField &a, const TapeField &b)
{
    return a.value == b.value && a.bytes == b.bytes;
}

// A section in its line's newest format (newest: its id first, as the converters give it) laid
// out again in the older format qv of the same line (out: version, cversion, size, data), record
// by record: the tape's rebuild, every field of the older format from the newer record's field of
// that name. For a quest whose own section sits between formats, when the file's converter gives
// only the line's newest.
//
// The proof that nothing was lost: the result is converted up again the way saving the quest as
// the target converts that section (the port of the loader's reader for format qv, checked
// against upstream), and must give the newest-format section back. A field that comes back
// different is one format qv has no place for, which the loader makes up when it reads that
// format (not_held): an enemy's second weapon sound before format 60 is its weapon sound; an
// item's strong / magic / divine fire flags before format 56 are what the loader gives the
// item's class in a quest of that version (a 2.53 quest's Din's Fire is strong fire, a 2.55
// quest's of that format isn't). Those are counted for the import's note (g_format_note). Any
// other difference refuses the file, with the reason.
bool not_held(dword id, const TapeField &a, const TapeField *b, int qv)
{
    if(id == ID_GUYS && qv < 60 && a.key == "firesfx2")
        return true;

    // ITEM_FLAG9, 10 and 11
    return id == ID_ITEMS && qv < 56 && a.key == "flags" && b && a.bytes == b->bytes && !((a.value ^ b->value) & ~(int64_t)0x70000);
}

// What an import brought in differently because the quest's format has no place for it (shown
// with the import's result).
std::string g_format_note;

bool lay_out_older(dword id, int target, const std::vector<byte> &newest, int qv, word qcv, const char *what,
                   std::vector<byte> &out, std::string &why)
{
    const bool item = id == ID_ITEMS;
    char buf[300];
    In in(newest, 4);
    word nv = 0, ncv = 0;
    std::vector<byte> nd;
    SecTapes a;

    if(newest.size() < 12 || !take_section(in, &nv, &ncv, nd) || !split_records(id, nv, nd, a))
    {
        why = std::string("its ") + what + " couldn't be laid out in this quest's format";
        return false;
    }

    Out o;

    if(item)
        o.p16((word)a.count);

    if(!item || qv < 67)
        for(int i = 0; i < a.count; ++i)
        {
            char nm[64];
            memset(nm, 0, sizeof(nm));

            if(!a.names.empty())
                memcpy(nm, a.names[i].data(), 64);
            else if(const TapeField *nf = a.recs[i].find("name"))
                memcpy(nm, nf->bytes.empty() ? "" : (const char *)&nf->bytes[0], nf->bytes.size() < 63 ? nf->bytes.size() : 63);

            o.b.insert(o.b.end(), nm, nm + 64);
        }

    const int wd = quest_weapon_data_version(id);

    for(int i = 0; i < a.count; ++i)
    {
        Tape t = a.recs[i];
        older_keys(id, t, qv, wd);
        std::vector<byte> rec;

        if(!zqn255_tape_rebuild(item ? zq_tape_item : zq_tape_guy, t, qv, rec))
        {
            snprintf(buf, sizeof(buf), "%s %d of the file couldn't be laid out in this quest's format %d", item ? "item" : "enemy", i, qv);
            why = buf;
            return false;
        }

        o.pn(rec);
    }

    out = whole_section((word)qv, qcv, o.b);

    // the proof
    std::vector<byte> back;
    std::string w;

    if(!zq_newer_convert_section(id, target, out, back, w))
    {
        snprintf(buf, sizeof(buf), "its %s couldn't be laid out in this quest's format %d: ", what, qv);
        why = buf + w;
        return false;
    }

    if(back == newest)
        return true;

    In bin(back, 4);
    word bv = 0, bcv = 0;
    std::vector<byte> bd;
    SecTapes b;

    if(back.size() < 12 || !take_section(bin, &bv, &bcv, bd) || bv != nv || !split_records(id, bv, bd, b) || b.count != a.count)
    {
        snprintf(buf, sizeof(buf), "its %s don't stay as they are in this quest's format %d", what, qv);
        why = buf;
        return false;
    }

    int lost = 0;
    std::string first_lost, lost_what;

    for(int i = 0; i < a.count; ++i)
    {
        std::string key;

        if(!a.names.empty() && !b.names.empty() && strncmp(a.names[i].c_str(), b.names[i].c_str(), 64))
            key = "name";

        for(size_t k = 0; k < a.recs[i].f.size() && key.empty(); ++k)
        {
            const TapeField &fa = a.recs[i].f[k];
            const TapeField *fb = b.recs[i].find(fa.key.c_str());

            if(fb && same_field(fa, *fb))
                continue;

            if(not_held(id, fa, fb, qv))
            {
                if(!lost++)
                {
                    snprintf(buf, sizeof(buf), "%s %d", item ? "item" : "enemy", i);
                    first_lost = buf;
                    lost_what = fa.key == "firesfx2" ? "second weapon sound" : "fire strength flags";
                }
            }
            else
                key = fa.label.empty() ? fa.key : fa.label + " (" + fa.key + ")";
        }

        if(key.empty() && b.recs[i].f.size() != a.recs[i].f.size())
            key = "layout";

        if(!key.empty())
        {
            snprintf(buf, sizeof(buf), "this quest's %s are in format %d, which can't hold what %s %d of the file becomes when converted (its %s)",
                     what, qv, item ? "item" : "enemy", i, key.c_str());
            why = buf;
            return false;
        }
    }

    if(lost)
    {
        snprintf(buf, sizeof(buf), "this quest's %s format %d has no place for the %s of %d of them (%s%s): they are as its version reads them; ",
                 item ? "item" : "enemy", qv, lost_what.c_str(), lost, first_lost.c_str(), lost > 1 ? " first" : "");
        g_format_note += buf;
    }

    return true;
}

// A file's section (version v, cversion cv, data) for a quest whose own section is in another
// format. Upstream's imports read a file of any older format with its loader's reader, which
// converts it to its newest; here an older file is converted the same way by what saving a quest
// as 2.55.18 or 3.0 Nightly converts that section with (zq_newer_convert_section: checked
// against upstream's loader), which gives the generation's newest format, for a quest whose
// section is in exactly that format (2.55.18's or 3.0's). A quest whose section sits between
// formats gets the file converted up to its own format (zq_newer_convert_section_to: the same
// conversions, stopped at that format), which the 2.55 converters of strings, enemies and items
// can do; where none can (3.0's in-between formats), and for a newer file, which would lose
// what the quest's format can't hold, it is refused with the reason. True with v, cv and data
// the section to bring in.
bool section_for_quest(dword id, const char *what, word &v, word &cv, std::vector<byte> &data, std::string &why)
{
    const int qv = quest_section_version(id);

    if(!qv)
    {
        why = std::string("this quest's ") + what + " can't be replaced this way";
        return false;
    }

    if(v == qv)
        return true;

    if(v > qv)
    {
        why = newer_words(what, v, qv);
        return false;
    }

    word n255 = 0, n30 = 0, ncv = 0;
    zqn255_newest(id, ZQ_NEWER_255, &n255, &ncv);
    zqn255_newest(id, ZQ_NEWER_30, &n30, &ncv);

    std::vector<byte> out;
    bool converted;

    // the line the quest's section belongs to, and the quest's own cversion
    const int target = qv <= n255 ? ZQ_NEWER_255 : ZQ_NEWER_30;
    const int newest = target == ZQ_NEWER_255 ? n255 : n30;
    const ZqQuestPart *part = zq_find_part(id);
    word pv = 0, pcv = 0;

    if(!part || !zq_part_versions(*part, &pv, &pcv))
        pcv = cv;

    // A file in one of 2.53's formats or older (2.53's own exports: enemies 24, items 25), which
    // the 2.55 line's converters don't start from: converted as a quest of 2.53.1 saved as the
    // line's newest version has that section converted, then laid out in the quest's format.
    const bool old_file = (id == ID_GUYS && v <= 24) || (id == ID_ITEMS && v <= 25);

    if(old_file)
        converted = id == ID_GUYS ? zqn_guys_from_old_file(whole_section(v, cv, data), target, out, why)
                                  : zqn_items_from_old_file(whole_section(v, cv, data), target, out, why);
    else if(qv == newest)
        converted = zq_newer_convert_section(id, target, whole_section(v, cv, data), out, why);
    else
    {
        // between formats: up to the quest's own (its cversion too), where the section's converter
        // can stop there; otherwise up to the line's newest, to be laid out in the quest's
        std::string w;
        converted = zq_newer_convert_section_to(id, target, whole_section(v, cv, data), (word)qv, pcv, out, w);

        if(!converted && (id == ID_GUYS || id == ID_ITEMS))
            converted = zq_newer_convert_section(id, target, whole_section(v, cv, data), out, why);
        else if(!converted)
            why = w;

        if(!converted && target == ZQ_NEWER_30 && id != ID_GUYS && id != ID_ITEMS)
        {
            char b[300];
            snprintf(b, sizeof(b), "its %s are in format %d and this quest's in format %d, between 2.55.18's (%d) and 3.0 Nightly's (%d); "
                     "ZQuest doesn't convert older %s to a format in between yet", what, v, qv, n255, n30, what);
            why = b;
            return false;
        }
    }

    // the line's newest format, for a quest whose section is in an older one of the line
    if(converted && (id == ID_GUYS || id == ID_ITEMS) && out.size() >= 6 && (out[4] | (out[5] << 8)) == newest && qv != newest)
    {
        std::vector<byte> older;

        if(!lay_out_older(id, target, out, qv, pcv, what, older, why))
            return false;

        Out o;
        o.m32(id);
        o.pn(older);
        out.swap(o.b);
    }

    if(!converted)
    {
        char b[120];
        snprintf(b, sizeof(b), "its %s (format %d) couldn't be converted to this quest's format %d: ", what, v, qv);
        why = b + why;
        return false;
    }

    // the converter's section: id, version, cversion, size, data
    In in(out, 4);
    word nv = 0, nc = 0;
    std::vector<byte> nd;

    if(out.size() < 12 || !take_section(in, &nv, &nc, nd) || nv != qv)
    {
        why = std::string("its ") + what + " couldn't be converted to this quest's format";
        return false;
    }

    v = nv;
    cv = nc;
    data.swap(nd);
    return true;
}

// A DMap's music before DMap format 25 (a MIDI number and a music file, with loop points from
// format 18), as the music object upstream's reader makes of it (read_one_dmap; the MIDI through
// convert_from_old_midi_id). False when the DMap has no music.
bool old_dmap_music(const Tape &t, int index, Zq255Store::Music &m)
{
    int midi = (int)t.get("midi") - 3;      // MIDIOFFSET_DMAP - MIDIOFFSET_ZSCRIPT

    switch(midi)
    {
    case -3: midi = 0; break;
    case -4: midi = -3; break;      // MIDI_GAMEOVER
    case -2: midi = -5; break;      // MIDI_OVERWORLD
    case -1: midi = -1; break;      // MIDI_DUNGEON
    case 0: midi = -4; break;       // MIDI_LEVEL9
    default: if(midi < 0) midi = 0; break;
    }

    m = Zq255Store::Music();
    m.midi = (short)midi;

    if(const TapeField *f = t.find("tmusic"))
        for(size_t k = 0; k < f->bytes.size() && f->bytes[k]; ++k)
            m.path += (char)f->bytes[k];

    m.track = (byte)t.get("tmusictrack");
    m.loop_start = (long)t.get("loop_start");
    m.loop_end = (long)t.get("loop_end");
    m.xfade_in = (long)t.get("xfade_in");
    m.xfade_out = (long)t.get("xfade_out");

    if(m.path.empty() && !m.midi)
        return false;

    char name[32];
    snprintf(name, sizeof(name), "DMap %d", index);
    m.name = name;
    return true;
}

// DMap records of an older format laid out in the quest's, as upstream's reader reads an older
// DMap (read_one_dmap, which its import uses, and its loader for a whole quest): what the older
// format lacks takes the reader's default (no mirror DMap, no overlay or map subscreen, no intro
// string, floor 0, no subscreen or map scripts), a title that was 20 characters wide becomes a
// string, and an overworld DMap's map X offset is 0. Across the formats that changed how a DMap
// holds something:
//   25  its music: the MIDI and music file become one of the quest's music objects ("DMap n"),
//       which the DMap then names by number. Upstream's import adds a new object every time; an
//       object the quest already has just like it is used again here, so importing a quest's
//       own DMaps into a copy of it saved as 3.0 gives the DMaps that copy has.
//   26  its disabled items: 32 bytes of bits become a bit string.
//   27  its scripts: the script numbers and their eight arguments become script configs (the
//       passive subscreen's arguments are the active subscreen's).
// Region ids (in format 21 only) must be all 0: later formats keep regions with the maps.
// first: the first record's DMap number. The music objects go into the quest's list (zq255.music):
// the caller puts the list back if the import fails.
bool dmaps_to_format(std::vector<std::vector<byte> > &recs, int first, int v, int qv, std::string &why)
{
    char buf[240], k[48], k2[48];

    if(v < 9)
    {
        snprintf(buf, sizeof(buf), "its DMaps are in format %d, older than ZQuest converts", v);
        why = buf;
        return false;
    }

    for(size_t i = 0; i < recs.size(); ++i)
    {
        std::vector<byte> &rec = recs[i];
        Tape t;
        TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);

        if(!zq_tape_dmap(p, v) || p.pos != rec.size())
        {
            snprintf(buf, sizeof(buf), "DMap %d of the file doesn't read as its format says", first + (int)i);
            why = buf;
            return false;
        }

        if(v < 20 && qv >= 20)
            if(TapeField *tf = t.find("title"))
            {
                // the title as a string: its text up to the first 0
                size_t n = 0;

                while(n < tf->bytes.size() && tf->bytes[n])
                    ++n;

                tf->bytes.resize(n);
            }

        if(v == 21 && qv > 21)
            if(const TapeField *rf = t.find("regions"))
                for(size_t q = 0; q < rf->bytes.size(); ++q)
                    if(rf->bytes[q])
                    {
                        snprintf(buf, sizeof(buf), "DMap %d holds region ids, which this quest's format keeps with its maps", first + (int)i);
                        why = buf;
                        return false;
                    }

        if((t.get("type") & 0x7F) == 1)     // dmOVERW
            zqn255_tape_set(t, "xoff", 0);

        if(v < 25 && qv >= 25)
        {
            Zq255Store::Music m;
            int number = 0;

            if(old_dmap_music(t, first + (int)i, m))
            {
                for(size_t q = 0; q < zq255.music.size() && !number; ++q)
                {
                    const Zq255Store::Music &x = zq255.music[q];

                    if(x.name == m.name && x.path == m.path && x.midi == m.midi && x.track == m.track && x.loop_start == m.loop_start &&
                       x.loop_end == m.loop_end && x.xfade_in == m.xfade_in && x.xfade_out == m.xfade_out)
                        number = (int)q + 1;
                }

                if(!number)
                {
                    if(zq255.music.size() >= 65535)
                    {
                        why = "this quest's music list is full";
                        return false;
                    }

                    zq255.music.push_back(m);
                    number = (int)zq255.music.size();
                }
            }

            zqn255_tape_set(t, "music", number);
        }

        if(v < 26 && qv >= 26)
            if(TapeField *df = t.find("disabled"))
            {
                const std::vector<byte> bits(df->bytes);
                df->bytes = zq_bitstr_store(bits);
            }

        if(v < 27 && qv >= 27)
        {
            static const struct { const char *config, *script, *args; } sc[] =
            {
                { "sc.active", "script", "initd" }, { "sc.asub", "asub_script", "sub_initd" },
                { "sc.psub", "psub_script", "sub_initd" }, { "sc.onmap", "onmap_script", "onmap_initd" }
            };

            for(int q = 0; q < 4; ++q)
            {
                int64_t script = t.get(sc[q].script);

                if(q > 0 && script >= 256)      // NUMSCRIPTSDMAP: only a damaged quest has one past it
                    script = 0;

                snprintf(k, sizeof(k), "%s.script", sc[q].config);
                zqn255_tape_set(t, k, script);

                for(int a = 0; a < 8; ++a)
                {
                    snprintf(k, sizeof(k), "%s.initd%d", sc[q].config, a);
                    snprintf(k2, sizeof(k2), "%s%d", sc[q].args, a);
                    zqn255_tape_set(t, k, t.get(k2));
                }
            }
        }

        std::vector<byte> out;

        if(!zqn255_tape_rebuild(zq_tape_dmap, t, qv, out))
        {
            snprintf(buf, sizeof(buf), "DMap %d couldn't be laid out in this quest's format %d", first + (int)i, qv);
            why = buf;
            return false;
        }

        rec.swap(out);
    }

    return true;
}

// The combos an import replaces (first .. first + n - 1), to put back when the result can't be
// kept: ZQuest's combos and their newer parts.
struct ComboKeep
{
    int first;
    std::vector<newcombo> old;
    std::vector<Q30Combo *> old_x;
    bool done;

    ComboKeep(int from, int n) : first(from), old(combobuf + from, combobuf + from + n), old_x((size_t)n, (Q30Combo *)NULL), done(false)
    {
        for(int k = 0; k < n; ++k)
            if(zq255.combos[from + k])
                old_x[k] = new Q30Combo(*zq255.combos[from + k]);
    }

    // the combos as they were (the store takes the copies over)
    void restore()
    {
        if(done)
            return;

        done = true;
        reset_combo_animations();
        reset_combo_animations2();

        for(size_t k = 0; k < old.size(); ++k)
        {
            combobuf[first + (int)k] = old[k];
            q30_combo_store(first + (int)k, old_x[k]);
            old_x[k] = NULL;
        }

        setup_combo_animations();
        setup_combo_animations2();
    }

    ~ComboKeep()
    {
        for(size_t k = 0; k < old_x.size(); ++k)
            delete old_x[k];
    }
};

// ---- tiles -----------------------------------------------------------------------------------

// 2.53's tiles keep a 4-bit tile packed, two pixels a byte (low nibble first), as 3.0's files do.
bool tile_blank(int i)
{
    const tiledata &t = newtilebuf[i];

    if(!t.data)
        return true;

    const int n = tilesize(t.format);

    for(int k = 0; k < n; ++k)
        if(t.data[k])
            return false;

    return true;
}

// 2.55.17's format: the tile's format and its bytes unpacked (256 for a 4-bit one).
void put_tile_255(Out &o, int i)
{
    const tiledata &t = newtilebuf[i];
    const byte format = (t.format >= tf4Bit && t.format < tfMax) ? t.format : (byte)tf4Bit;
    o.p8(format);

    if(format == tf4Bit)
    {
        for(int k = 0; k < 128; ++k)
        {
            const byte b = t.data ? t.data[k] : 0;
            o.p8(b & 15);
            o.p8(b >> 4);
        }

        return;
    }

    const int n = tilesize(format);

    for(int k = 0; k < n; ++k)
        o.p8(t.data ? t.data[k] : 0);
}

// 3.0's write_tile_entry.
void put_tile_30(Out &o, int i)
{
    if(tile_blank(i))
    {
        o.p8(0);
        return;
    }

    const tiledata &t = newtilebuf[i];
    o.p8(t.format);
    const int n = tilesize(t.format);

    for(int k = 0; k < n; ++k)
        o.p8(t.data[k]);
}

// The size upstream's tile formats take in a file (its tilesize; 3.0's packed 4-bit is 128).
int up_tile_bytes(byte format, bool packed4)
{
    switch(format)
    {
    case tf4Bit: return packed4 ? 128 : 256;
    case tf8Bit: return 256;
    case tf16Bit: return 512;
    case tf24Bit: return 768;
    case tf32Bit: return 1024;
    }

    return -1;
}

// One tile into newtilebuf[d] from upstream's bytes (packed4: a 4-bit tile two pixels a byte).
void store_tile(int d, byte format, const byte *src, bool packed4)
{
    if(format == tfInvalid)
    {
        reset_tile(newtilebuf, d, tf4Bit);
        return;
    }

    reset_tile(newtilebuf, d, format);

    if(format == tf4Bit && !packed4)
    {
        for(int k = 0; k < 128; ++k)
            newtilebuf[d].data[k] = (byte)((src[2 * k] & 15) | ((src[2 * k + 1] & 15) << 4));
    }
    else
        memcpy(newtilebuf[d].data, src, tilesize(format));
}

// ---- reading a section with one of 2.53's own readers (door sets) --------------------------

struct MemFile
{
    const byte *data;
    size_t size, pos;
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

    return m->pos < m->size ? m->data[m->pos++] : EOF;
}
int mf_ungetc(int c, void *u) { ((MemFile *)u)->ungot = c; return c; }
#pragma push_macro("long")  // packfile callbacks use the system's long (zcport/lp64.h)
#undef long
long mf_fread(void *p, long n, void *u)
{
    MemFile *m = (MemFile *)u;
    byte *out = (byte *)p;
    long got = 0;

    while(got < n)
    {
        int c = mf_getc(u);

        if(c == EOF)
            break;

        out[got++] = (byte)c;
    }

    return got;
}
#pragma pop_macro("long")
int mf_putc(int, void *) { return EOF; }
#pragma push_macro("long")
#undef long
long mf_fwrite(const void *, long, void *) { return 0; }
#pragma pop_macro("long")
int mf_fseek(void *u, int off)
{
    MemFile *m = (MemFile *)u;
    m->pos += (size_t)off;
    return 0;
}
int mf_feof(void *u)
{
    MemFile *m = (MemFile *)u;
    return m->ungot < 0 && m->pos >= m->size;
}
int mf_ferror(void *) { return 0; }

PACKFILE_VTABLE mf_vtable = { mf_fclose, mf_getc, mf_ungetc, mf_fread, mf_putc, mf_fwrite, mf_fseek, mf_feof, mf_ferror };

// ---- reading a section with the 2.55 / 3.0 readers -----------------------------------------

int read_255(int (*reader)(Q255Section &), word v, word cv, const std::vector<byte> &data, bool keep, zquestheader *as = NULL)
{
    Q255Section s;
    s.data = data.empty() ? NULL : &data[0];
    s.size = data.size();
    s.version = v;
    s.cversion = cv;
    s.keep = keep;
    s.header = as ? as : &header;
    s.misc = &misc;
    int ret = reader(s);

    if(ret == 0 && (s.failed || s.pos != s.size))
        ret = qe_invalid;

    return ret;
}

} // namespace

// ---- which version --------------------------------------------------------------------------

bool zqx_is_30()
{
    return q255_header_version[0] >= 3;
}

// ---- exports --------------------------------------------------------------------------------

bool zqx_export_dmaps(const char *path, int first, int last, std::string &why)
{
    if(first > last)
        std::swap(first, last);

    first = vbound(first, 0, MAX_ZDMAPS - 1);
    last = vbound(last, 0, MAX_ZDMAPS - 1);
    std::vector<byte> recs;
    word v = 0, cv = 0;

    if(!zq255_dmap_export(first, last, recs, &v, &cv, why))
        return false;

    Out o;
    o.p32((dword)ZDMAP_KIND);
    o.p32(UP_VERSION);
    o.p32(UP_BUILD);
    o.p16(v);
    o.p16(cv);
    o.p32(MAX_ZDMAPS);
    o.p32((dword)first);
    o.p32((dword)last);
    o.p32((dword)(last - first));
    o.pn(recs);
    return write_file(path, o.b, why);
}

bool zqx_export_strings(const char *path, std::string &why)
{
    std::vector<byte> sec;

    if(!own_section(ID_STRINGS, sec, why))
        return false;

    Out o;
    o.m32(ID_STRINGS);
    o.pn(sec);
    return write_file(path, o.b, why);
}

bool zqx_export_combos(const char *path, std::string &why)
{
    std::vector<byte> recs;
    word v = 0;

    if(!zq255_combo_export(0, MAXCOMBOS, recs, &v, why))
        return false;

    // the cversion upstream writes: 3.0 the version again, 2.55.17 its CV_COMBOS (1)
    word cv = 1;
    const ZqQuestPart *p = zq_find_part(ID_COMBOS);
    word pv = 0, pcv = 0;

    if(p && zq_part_versions(*p, &pv, &pcv) && pv == v)
        cv = pcv;
    else if(zqx_is_30())
        cv = v;

    Out o;
    o.p32(UP_VERSION);
    o.p32(UP_BUILD);
    o.p16(v);
    o.p16(cv);
    o.p32(0);
    o.p32(MAXCOMBOS);
    o.pn(recs);
    return write_file(path, o.b, why);
}

bool zqx_export_tiles(const char *path, int first_page, int pages, std::string &why)
{
    Out o;
    o.p32(UP_VERSION);
    o.p32(UP_BUILD);

    if(zqx_is_30())
    {
        // 3.0: the pages asked for, version 4 (V_TILES; the cversion is the version again)
        const int all_pages = NEWMAXTILES / TILES_PER_PAGE;
        first_page = vbound(first_page, 0, all_pages - 1);
        pages = vbound(pages, 1, all_pages - first_page);
        const int first = first_page * TILES_PER_PAGE, count = pages * TILES_PER_PAGE;
        o.p16(4);
        o.p16(4);
        o.p32((dword)first);
        o.p32((dword)count);

        for(int i = first; i < first + count; ++i)
            put_tile_30(o, i);
    }
    else
    {
        // 2.55.17: every tile, version 3, cversion 1 (V_TILES, CV_TILES)
        o.p16(3);
        o.p16(1);
        o.p32(0);
        o.p32(NEWMAXTILES);

        for(int i = 0; i < NEWMAXTILES; ++i)
            put_tile_255(o, i);
    }

    return write_file(path, o.b, why);
}

bool zqx_export_guys(const char *path, std::string &why)
{
    std::vector<byte> sec;

    if(!own_section(ID_GUYS, sec, why))
        return false;

    Out o;
    o.m32(ID_GUYS);
    o.pn(sec);
    return write_file(path, o.b, why);
}

bool zqx_export_zgp(const char *path, std::string &why)
{
    static const dword ids[] = { ID_TILES, ID_COMBOS, ID_CSETS, ID_ITEMS, ID_WEAPONS, ID_ICONS, ID_COLORS, ID_DOORS };
    Out o;
    o.m32(ID_GRAPHICSPACK);
    o.p16(V_GRAPHICSPACK);
    o.p16(1);       // the cversion: 3.0 writes the version again, 2.55.17 its CV_GRAPHICSPACK; both 1

    for(size_t k = 0; k < sizeof(ids) / sizeof(ids[0]); ++k)
    {
        std::vector<byte> sec;

        if(ids[k] == ID_COMBOS ? !combo_section(sec, why) : !own_section(ids[k], sec, why))
            return false;

        o.m32(ids[k]);
        o.pn(sec);
    }

    return write_file(path, o.b, why);
}

bool zqx_export_quest(const char *path, bool packed, std::string &why)
{
    return zq_save_own_format_packed(path, packed, why) == 0;
}

// ---- imports --------------------------------------------------------------------------------

bool zqx_import_dmaps(const char *path, int *max_map, std::string &why, std::string &note)
{
    note.clear();
    std::vector<byte> b;

    if(!read_file(path, b, why))
        return false;

    // readsomedmaps: a negative first number says the newer layout (V_ZDMAP); otherwise it is
    // the program's version and the file holds one DMap
    In in(b);
    word v = 0;
    int first = 0, last = 0;

    if(b.size() >= 14 && In(b).m32() == ID_DMAPS)
    {
        // An export of 2.53 or older (.dmp), which upstream's import doesn't read: the whole DMAP
        // section, its id first, every DMap from 0. A format before 2.53's own (9) is read by
        // 2.53's reader and written again as 9, as ZQuest reads such a file for an older quest.
        in.m32();
        v = in.w();
        in.w();     // cversion
        in.l();     // size
        int count = in.w();

        if(in.bad || !v || v > 28 || count < 1 || count > MAX_ZDMAPS)
        {
            why = "it isn't a DMap file";
            return false;
        }

        if(v < 9)
        {
            const std::vector<dmap> kept(DMaps, DMaps + MAXDMAPS);
            std::vector<byte> again;
            MemFile m;
            m.data = &b[4];
            m.size = b.size() - 4;
            m.pos = 0;
            m.ungot = -1;
            PACKFILE *f = pack_fopen_vtable(&mf_vtable, &m);
            bool ok = f && readdmaps(f, NULL, ZELDA_VERSION, VERSION_BUILD, 0, MAXDMAPS, true) == 0;

            if(f)
                pack_fclose(f);

            if(ok)
            {
                PACKFILE *o = zq_open_vector_sink(&again);
                ok = o && writedmaps(o, ZELDA_VERSION, VERSION_BUILD, 0, MAXDMAPS) == 0;

                if(o)
                    pack_fclose(o);
            }

            memcpy(DMaps, &kept[0], sizeof(dmap) * MAXDMAPS);

            if(!ok || again.size() < 14)
            {
                why = "its DMaps don't read as their format says";
                return false;
            }

            b.swap(again);
            in.pos = 4;
            v = in.w();
            in.w();
            in.l();
            count = in.w();
        }

        last = count - 1;
    }
    else
    {
        const int kind = (int)in.l();
        dword zversion = kind < 0 ? in.l() : (dword)kind;
        in.l();     // build
        v = in.w();
        in.w();     // cversion

        if(kind < 0)
        {
            in.l();     // the most DMaps
            first = (int)in.l();
            last = (int)in.l();
            in.l();     // last - first
        }

        if(in.bad)
        {
            why = "it isn't a DMap file";
            return false;
        }

        if(zversion > UP_VERSION || v > 28)
        {
            why = "it was made by a newer version of ZC than ZQuest reads";
            return false;
        }
    }

    if(first < 0 || last < first || last >= MAX_ZDMAPS)
    {
        why = "its DMap numbers are out of range";
        return false;
    }

    const int qv = zq255_dmap_version();

    if(v > qv)
    {
        why = newer_words("DMaps", v, qv);
        return false;
    }

    std::vector<std::vector<byte> > recs;

    if(!zq255_dmap_split(b, in.pos, v, last - first + 1, recs))
    {
        why = "its DMaps don't read as their format says";
        return false;
    }

    // everything a commit changes (and the music objects older DMaps' music becomes), to put back
    // if one fails
    const std::vector<dmap> dmaps(DMaps, DMaps + MAXDMAPS);
    const std::map<int, std::vector<byte> > records = zq255.dmap_records;
    const std::vector<Zq255Store::Music> music = zq255.music;

    if(v < qv && !dmaps_to_format(recs, first, v, qv, why))
    {
        zq255.music = music;
        return false;
    }
    const std::map<int, std::pair<int, bool> > dmap_music = zq255.dmap_music;
    std::map<int, Q30ScriptRef> scripts[q30_dmap_kinds];

    for(int k = 0; k < q30_dmap_kinds; ++k)
        scripts[k] = zq255.dmap_scripts[k];

    const std::map<int, std::pair<int, int> > subs = zq255.dmap_subscreens;
    bool ok = true;

    for(int i = first; i <= last && ok; ++i)
        ok = zq255_dmap_commit(i, recs[i - first], why);

    if(ok)
        ok = section_still_saves(ID_DMAPS, why);

    if(!ok)
    {
        memcpy(DMaps, &dmaps[0], sizeof(dmap) * MAXDMAPS);
        zq255.dmap_records = records;
        zq255.music = music;
        zq255.dmap_music = dmap_music;

        for(int k = 0; k < q30_dmap_kinds; ++k)
            zq255.dmap_scripts[k] = scripts[k];

        zq255.dmap_subscreens = subs;
        return false;
    }

    if(max_map)
    {
        *max_map = 0;

        for(int i = first; i <= last; ++i)
            if(DMaps[i].map > *max_map)
                *max_map = DMaps[i].map;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "DMaps %d to %d", first, last);
    note = buf;
    return true;
}

bool zqx_dmaps_fit_maps(int maps, std::string &why)
{
    for(int i = 0; i < MAXDMAPS; ++i)
    {
        if(DMaps[i].map < maps)
            continue;

        // its record (an edited or imported one, or as loaded), with the map set to 0
        std::vector<byte> rec;
        int v = 0;
        std::map<int, std::vector<byte> >::const_iterator it = zq255.dmap_records.find(i);

        if(it != zq255.dmap_records.end())
        {
            rec = it->second;
            v = zq255_dmap_version();
        }
        else if(!zq255_dmap_record(i, rec, &v))
        {
            // a DMap the file doesn't hold: the writer lays it out from ZQuest's DMap
            DMaps[i].map = 0;
            continue;
        }

        Tape t;
        TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);
        TapeField *f = NULL;

        if(!zq_tape_dmap(p, v) || p.pos != rec.size() || !(f = t.find("map")))
        {
            char b[80];
            snprintf(b, sizeof(b), "DMap %d's map couldn't be changed", i);
            why = b;
            return false;
        }

        f->value = 0;
        std::vector<byte> out;
        t.encode(out);

        if(!zq255_dmap_commit(i, out, why))
            return false;
    }

    return true;
}

bool zqx_import_strings(const char *path, std::string &why, std::string &note)
{
    note.clear();
    std::vector<byte> b, data;

    if(!read_file(path, b, why))
        return false;

    In in(b);
    word v = 0, cv = 0;

    if(in.m32() != ID_STRINGS || !take_section(in, &v, &cv, data))
    {
        why = "it isn't a string table";
        return false;
    }

    std::vector<byte> own;

    if(!own_section(ID_STRINGS, own, why))
        return false;

    const word qv = (word)(own[0] | (own[1] << 8));

    if(v < 5 || v > 13)
    {
        why = v > 13 ? "it was made by a newer version of ZC than ZQuest reads" : "its strings are in a format ZQuest doesn't read";
        return false;
    }

    // A newer file's settings this quest's format has no place for would be lost; an older
    // one's texts and settings are converted as upstream's loader converts them (string control
    // codes, defaults for what the format lacks).
    if(!section_for_quest(ID_STRINGS, "strings", v, cv, data, why))
        return false;

    if(v != qv)
    {
        why = "its strings couldn't be converted to this quest's format";
        return false;
    }

    // the file's strings walked as their format says (and how many there are)
    Tape t;
    TapeParser p(data.empty() ? NULL : &data[0], data.size(), 0, &t);

    if(!zq_tape_strings(p, v) || p.pos != data.size())
    {
        why = "its strings don't read as their format says";
        return false;
    }

    const int count = (int)t.get("count");

    // what the reader and this import change, to put back if the result can't be saved
    const int keep_size = msg_strings_size;
    const std::vector<MsgStr> strings(MsgStrings, MsgStrings + keep_size);
    const word keep_count = msg_count;
    const std::map<int, int> wrap = zq255.msg_wrap_extra;

    if(read_255(q255_read_strings, v, cv, data, true) != 0)
    {
        why = "its strings don't read";
        memcpy(MsgStrings, &strings[0], sizeof(MsgStr) * (keep_size < msg_strings_size ? keep_size : msg_strings_size));
        msg_count = keep_count;
        zq255.msg_wrap_extra = wrap;
        return false;
    }

    // The file's table becomes the quest's string part, as it is (texts with every 3.0 string
    // control code, settings and all): the strings as read are what the writer compares with
    // from now on.
    Out o;
    o.m32(ID_STRINGS);
    o.pn(whole_section(v, (word)(own[2] | (own[3] << 8)), data));
    zq255_strings_rebased();

    if(!zq_part_rebase(ID_STRINGS, o.b))
    {
        why = "this quest's string part couldn't be replaced";
        return false;
    }

    g_import_pending = true;
    char buf[64];
    snprintf(buf, sizeof(buf), "%d strings", count);
    note = buf;
    return true;
}

bool zqx_import_combos(const char *path, int dest, std::string &why, std::string &note)
{
    note.clear();
    std::vector<byte> b;

    if(!read_file(path, b, why))
        return false;

    In in(b);
    const dword zversion = in.l();
    in.l();     // build
    const word v = in.w();
    in.w();     // cversion
    in.l();     // the first combo it was written from
    const int count = (int)in.l();

    if(in.bad || count < 0)
    {
        why = "it isn't a combo file";
        return false;
    }

    if(zversion > UP_VERSION || v > 73)
    {
        why = "it was made by a newer version of ZC than ZQuest reads";
        return false;
    }

    // (before version 33 a .zcombo file had a layout of its own, upstream's readcombofile_old)
    if(v < 33)
    {
        char buf[120];
        snprintf(buf, sizeof(buf), "it is in the old .zcombo layout (format %d), which ZQuest doesn't read", v);
        why = buf;
        return false;
    }

    dest = vbound(dest, 0, MAXCOMBOS - 1);
    const int n = count < MAXCOMBOS - dest ? count : MAXCOMBOS - dest;

    // the combos it replaces are put back if the result can't be saved (a newer file's fields
    // the quest's combo format has no place for)
    ComboKeep keep(dest, n);
    const std::vector<byte> recs(b.begin() + in.pos, b.end());
    int placed = 0;

    if(!zq255_combo_import(recs, v, count, dest, false, &placed, why))
        return false;

    if(!section_still_saves(ID_COMBOS, why))
    {
        keep.restore();
        return false;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%d combos, from combo %d", placed, dest);
    note = buf;
    return true;
}

bool zqx_import_tiles(const char *path, int dest, std::string &why, std::string &note)
{
    note.clear();
    std::vector<byte> b;

    if(!read_file(path, b, why))
        return false;

    In in(b);
    const dword zversion = in.l();
    in.l();     // build
    const word v = in.w();
    in.w();     // cversion
    in.l();     // the first tile it was written from
    const int count = (int)in.l();

    if(in.bad || count < 0)
    {
        why = "it isn't a tile file";
        return false;
    }

    if(zversion > UP_VERSION || v > 4)
    {
        why = "it was made by a newer version of ZC than ZQuest reads";
        return false;
    }

    // 3.0's readtilefile_to_location: from version 4 a format of 0 is a blank tile and a 4-bit
    // tile is packed; before, every tile is its format and its unpacked bytes
    const bool v4 = v >= 4;
    const int limit = zq_tile_limit();
    int placed = 0;

    // read through once first: a file that doesn't read changes nothing
    {
        In c(b, in.pos);

        for(int k = 0; k < count; ++k)
        {
            const byte format = (byte)c.get(1);

            // (a file that ends before its last tile doesn't read: the end isn't a blank tile)
            if(!c.bad && v4 && format == tfInvalid)
                continue;

            const int n = c.bad ? -1 : up_tile_bytes(format, v4);

            if(c.bad || n < 0 || !c.has((size_t)n))
            {
                why = "its tiles don't read as their format says";
                return false;
            }

            c.pos += (size_t)n;
        }
    }

    for(int k = 0; k < count && dest + k < limit; ++k)
    {
        const byte format = (byte)in.get(1);

        if(v4 && format == tfInvalid)
        {
            reset_tile(newtilebuf, dest + k, tf4Bit);
            ++placed;
            continue;
        }

        const int n = up_tile_bytes(format, v4);
        store_tile(dest + k, format, &b[in.pos], v4);
        in.pos += (size_t)n;
        ++placed;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%d tiles, from tile %d", placed, dest);
    note = buf;
    return true;
}

bool zqx_import_guys(const char *path, std::string &why, std::string &note)
{
    note.clear();
    g_format_note.clear();
    std::vector<byte> b, data;

    if(!read_file(path, b, why))
        return false;

    In in(b);
    word v = 0, cv = 0;

    if(in.m32() != ID_GUYS || !take_section(in, &v, &cv, data))
    {
        why = "it isn't an enemy file";
        return false;
    }

    if(v > 60)
    {
        why = "it was made by a newer version of ZC than ZQuest reads";
        return false;
    }

    // an older file's enemies are converted as upstream's loader converts them
    if(!section_for_quest(ID_GUYS, "enemies", v, cv, data, why))
        return false;

    if(!zq255_guys_take(whole_section(v, cv, data), why))
        return false;

    note = "every enemy";

    if(!g_format_note.empty())
        note += "; " + g_format_note.substr(0, g_format_note.size() - 2);

    return true;
}

// ---- one item or enemy (.zitem, .znpc) -------------------------------------------------------

namespace
{

// Whether the single file of this format carries the name in front of the record: an enemy's
// always (readonenpc), an item's until its record holds the name itself (readoneitem).
bool one_file_has_name(bool item, int v)
{
    return !item || v < 67;
}

}

bool zqx_export_one(bool item, int index, const char *path, std::string &why)
{
    const ZqQuestPart *part = zq_find_part(item ? ID_ITEMS : ID_GUYS);
    word v = 0, cv = 0;
    std::vector<byte> rec;
    int rv = 0;

    if(!part || !zq_part_versions(*part, &v, &cv) ||
       !(item ? zq255_item_record(index, rec, &rv) : zq255_guy_record(index, rec, &rv)) || rv != v)
    {
        why = item ? "this quest holds no such item" : "this quest holds no such enemy";
        return false;
    }

    Out o;
    o.p32(UP_VERSION);
    o.p32(UP_BUILD);
    o.p16(v);
    o.p16(cv);

    if(one_file_has_name(item, v))
    {
        char name[64];
        memset(name, 0, sizeof(name));
        strncpy(name, item ? item_string[index] : guy_string[index], 63);
        o.b.insert(o.b.end(), name, name + 64);
    }

    o.pn(rec);
    return write_file(path, o.b, why);
}

bool zqx_import_one(bool item, int index, const char *path, std::string &why, std::string &note)
{
    note.clear();
    std::vector<byte> b;

    if(!read_file(path, b, why))
        return false;

    In in(b);
    const dword zversion = in.l();
    in.l();     // the build
    const word v = in.w();
    in.w();     // the cversion: nothing reads it

    if(in.bad || zversion < 0x192 || zversion > 0x999 || !v)
    {
        why = item ? "it isn't an item file" : "it isn't an enemy file";
        return false;
    }

    // upstream's own check
    if(zversion > UP_VERSION)
    {
        why = "it was made by a newer version of ZC than ZQuest reads";
        return false;
    }

    const ZqQuestPart *part = zq_find_part(item ? ID_ITEMS : ID_GUYS);
    word qv = 0, qcv = 0;
    std::vector<byte> now;

    if(!part || !zq_part_versions(*part, &qv, &qcv) ||
       !(item ? zq255_item_record(index, now, NULL) : zq255_guy_record(index, now, NULL)))
    {
        why = item ? "this quest holds no such item" : "this quest holds no such enemy";
        return false;
    }

    if(v != qv)
    {
        char t[200];
        snprintf(t, sizeof(t), "it is in %s format %d and this quest's %s are in format %d; one %s isn't converted between "
                 "formats", item ? "item" : "enemy", (int)v, item ? "items" : "enemies", (int)qv, item ? "item" : "enemy");
        why = t;
        return false;
    }

    char name[64];
    memset(name, 0, sizeof(name));

    if(one_file_has_name(item, v))
    {
        if(!in.has(64))
        {
            why = "the file ends early";
            return false;
        }

        memcpy(name, &b[in.pos], 63);
        in.pos += 64;
    }

    // the record as the quest's format lays it out. 2.55.17's files end before the last fields
    // of the section's record (an item's display name, burn sprites and light radii; an enemy's
    // move flags and shadow, death and spawn sprites), which its reader leaves at 0: the same
    // here, for a file that ends where a 2.55 one does.
    std::vector<byte> rec(b.begin() + in.pos, b.end());
    const size_t in_file = rec.size();
    size_t used = 0;

    for(int pass = 0; pass < 2 && !used; ++pass)
    {
        Tape t;
        TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);

        if((item ? zq_tape_item(p, v) : zq_tape_guy(p, v)) && !p.bad)
            used = p.pos;
        else if(pass == 0 && v <= (item ? 59 : 48))
            rec.insert(rec.end(), 64, 0);
        else
            break;
    }

    // (all of the file, and with the fields added nothing but them past its end)
    if(!used || (rec.size() == in_file ? used != in_file : used < in_file))
    {
        why = "it doesn't read as its format says";
        return false;
    }

    rec.resize(used);

    if(!(item ? zq255_item_commit(index, rec, why) : zq255_guy_commit(index, rec, why)))
        return false;

    if(one_file_has_name(item, v))
    {
        char *to = item ? item_string[index] : guy_string[index];
        strncpy(to, name, 63);
        to[63] = 0;
    }

    extern bool saved;
    saved = false;
    note = std::string("replaced ") + (item ? "item " : "enemy ") + std::to_string(index);
    return true;
}

namespace
{

// ZQuest's palettes again from the quest's own palette section as it was (version, cversion,
// size, data), after a graphics pack's older palettes were tried in their place. Empty: nothing
// to put back.
void restore_palettes(const std::vector<byte> &before)
{
    if(before.size() < 8)
        return;

    In in(before);
    word v = 0, cv = 0;
    std::vector<byte> data;

    if(take_section(in, &v, &cv, data))
        read_255(q255_read_csets, v, cv, data, true);
}

}

bool zqx_import_zgp(const char *path, std::string &why, std::string &note)
{
    note.clear();
    g_format_note.clear();
    std::vector<byte> b;

    if(!read_file(path, b, why))
        return false;

    In in(b);

    if(in.m32() != ID_GRAPHICSPACK)
    {
        why = "it isn't a graphics pack";
        return false;
    }

    in.w();     // version
    in.w();     // cversion

    // load_zgp: these sections, in this order
    static const dword ids[] = { ID_TILES, ID_COMBOS, ID_CSETS, ID_ITEMS, ID_WEAPONS, ID_ICONS, ID_COLORS, ID_DOORS };
    const int n = (int)(sizeof(ids) / sizeof(ids[0]));
    word v[8], cv[8];
    std::vector<byte> data[8];

    for(int k = 0; k < n; ++k)
    {
        if(in.m32() != ids[k] || !take_section(in, &v[k], &cv[k], data[k]))
        {
            why = std::string("its ") + section_tag(ids[k]) + " section is missing or damaged";
            return false;
        }
    }

    zquestheader old_tiles;
    zquestheader *tiles_as = NULL;
    std::vector<byte> palettes_before;      // the quest's palette section, while the file's are tried

    // Everything is checked before anything goes in: each section's format must be one the
    // readers take, and the items in the quest's own format or an older one that converts to it
    // (they replace the quest's item section). The combos are checked as they go in, first.
    {
        // (older items are converted to the quest's format once all else is known to read)
        if(v[3] > quest_section_version(ID_ITEMS))
        {
            why = newer_words("items", v[3], quest_section_version(ID_ITEMS));
            return false;
        }

        if(v[0] < 1 || v[0] > 4 || v[1] < 7 || v[1] > 73 || v[2] < 4 || v[2] > 6 || v[4] < 6 || v[4] > 9 ||
           v[5] < 1 || v[5] > 10 || v[6] < 2 || v[6] > 4 || v[7] != V_DOORS)
        {
            why = "it holds sections in formats ZQuest doesn't read";
            return false;
        }

        // The tiles of a pack 2.53 or an older version wrote are counted in a word, where 2.55
        // and 3.0 count them in four bytes; the pack doesn't say which program wrote it, so its
        // tiles are read the one way that takes the whole section (upstream reads the newer way
        // only, and so can't read a 2.53 pack).
        if(read_255(q255_read_tiles, v[0], cv[0], data[0], false) != 0)
        {
            old_tiles = header;
            old_tiles.zelda_version = 0x250;
            old_tiles.build = 33;
            tiles_as = &old_tiles;
        }

        // a dry walk of the media sections
        if(read_255(q255_read_tiles, v[0], cv[0], data[0], false, tiles_as) != 0 || read_255(q255_read_csets, v[2], cv[2], data[2], false) != 0 ||
           read_255(q255_read_weapons, v[4], cv[4], data[4], false) != 0 || read_255(q255_read_icons, v[5], cv[5], data[5], false) != 0 ||
           read_255(q255_read_colors, v[6], cv[6], data[6], false) != 0)
        {
            why = "its sections don't read as their formats say";
            return false;
        }

        // weapon sprites, game icons and misc colours go through ZQuest's own data, which the
        // quest's writers lay out in its formats: older ones read into it as upstream reads
        // them, newer ones hold what the quest's formats have no place for
        static const struct { int k; dword id; const char *what; } held[] =
        {
            { 4, ID_WEAPONS, "weapon sprites" }, { 5, ID_ICONS, "game icons" }, { 6, ID_COLORS, "misc colours" }
        };

        for(size_t q = 0; q < sizeof(held) / sizeof(held[0]); ++q)
        {
            const int qv = quest_section_version(held[q].id);

            if(qv && v[held[q].k] > qv)
            {
                why = newer_words(held[q].what, v[held[q].k], qv);
                return false;
            }
        }

        if(!section_for_quest(ID_ITEMS, "items", v[3], cv[3], data[3], why))
            return false;

        // The palettes too: ZQuest's own palettes hold 2.53's CSets and 6 bits a colour, and the
        // rest (2.55's four more CSets a level, 3.0's 8 bits) is in the quest's palette part,
        // which becomes the file's. Palettes in 2.53's format (4) are converted from ZQuest's own
        // palettes, as a quest of 2.53 saved as a newer version has them converted: so the file's
        // go into ZQuest's first (and the quest's come back if the import stops after this).
        if(v[2] < 5 && v[2] < quest_section_version(ID_CSETS))
        {
            if(!own_section(ID_CSETS, palettes_before, why))
                return false;

            read_255(q255_read_csets, v[2], cv[2], data[2], true);
        }

        if(!section_for_quest(ID_CSETS, "palettes", v[2], cv[2], data[2], why))
        {
            restore_palettes(palettes_before);
            return false;
        }
    }

    // Combos first, as load_zgp reads them before the items: readcombos clears every combo, then
    // reads the file's: past the file's last one, each is what a blank record reads as. The
    // quest's combo format may not hold them (a newer file's fields): that is known here, before
    // anything else goes in, and the combos are put back.
    ComboKeep keep_combos(0, MAXCOMBOS);
    {
        In c(data[1]);
        const int count = c.w();
        std::vector<byte> recs(data[1].begin() + 2, data[1].end());

        if(!zq255_combo_import(recs, v[1], count, 0, true, NULL, why) || !section_still_saves(ID_COMBOS, why))
        {
            keep_combos.restore();
            restore_palettes(palettes_before);
            return false;
        }
    }

    // items: the quest's item section becomes the file's (it can still refuse, and then nothing
    // has changed)
    if(!zq255_items_take(whole_section(v[3], cv[3], data[3]), why))
    {
        keep_combos.restore();
        restore_palettes(palettes_before);
        return false;
    }

    // tiles
    read_255(q255_read_tiles, v[0], cv[0], data[0], true, tiles_as);

    // palettes, weapon sprites, game icons, misc colours
    read_255(q255_read_csets, v[2], cv[2], data[2], true);
    read_255(q255_read_weapons, v[4], cv[4], data[4], true);
    read_255(q255_read_icons, v[5], cv[5], data[5], true);
    read_255(q255_read_colors, v[6], cv[6], data[6], true);

    // The palettes (in the quest's own format by now): the quest's palette part becomes the
    // file's, as 2.53's palettes keep 6 bits a colour and 3.0's 8 (the writer keeps the part's
    // low bits, and the CSets 2.53 doesn't hold).
    {
        const ZqQuestPart *p = zq_find_part(ID_CSETS);
        word pv = 0, pcv = 0;

        if(p && zq_part_versions(*p, &pv, &pcv) && pv == v[2])
        {
            Out o;
            o.m32(ID_CSETS);
            o.pn(whole_section(pv, pcv, data[2]));

            if(!zq_part_rebase(ID_CSETS, o.b))
                note += "the palettes' 2.55/3.0 colours couldn't be kept; ";
        }
    }

    g_import_pending = true;

    // door sets, with 2.53's reader (2.55 and 3.0 keep its format)
    {
        std::vector<byte> whole = whole_section(v[7], cv[7], data[7]);
        MemFile m;
        m.data = &whole[0];
        m.size = whole.size();
        m.pos = 0;
        m.ungot = -1;
        PACKFILE *f = pack_fopen_vtable(&mf_vtable, &m);

        if(f)
        {
            if(readdoorcombosets(f, &header, true) != 0)
                note += "the door sets didn't read; ";

            pack_fclose(f);
        }
    }

    // what the quest's own save can't hold now
    std::string cant;

    for(int k = 0; k < n; ++k)
    {
        std::string w;

        if(!section_still_saves(ids[k], w))
            cant += (cant.empty() ? "" : "; ") + w;
    }

    if(!cant.empty())
        note += "these parts can't be saved as they are now: " + cant;

    note = g_format_note + note;
    why.clear();
    return true;
}

// ---- whole quests -----------------------------------------------------------------------------

namespace
{

// The generation (0: 2.53 or older, 1: 2.55, 2: 3.0) and version numbers of the quest in a file
// (a .qst, .zqt or .qsu, not encoded), from its header: ids as q255_header_version_ids (major,
// minor, patch, fourth, alpha, beta, gamma, release) and the version text (3.0's); made: the
// version as the title shows it.
bool peek_quest(const char *path, bool packed, int *gen, int ids[8], std::string &text, std::string &made, std::string &why)
{
    // packed with no password (2.55 and 3.0), with 2.53's data password (2.53's templates), or
    // not packed
    PACKFILE *f = pack_fopen_password(path, packed ? F_READ_PACKED : F_READ, "");

    if(!f && packed)
        f = pack_fopen_password(path, F_READ_PACKED, datapwd);

    if(!f && packed)
        f = pack_fopen_password(path, F_READ, "");

    if(!f)
    {
        why = "it couldn't be opened";
        return false;
    }

    std::vector<byte> b(65536);
    long n = pack_fread(&b[0], (long)b.size(), f);
    pack_fclose(f);
    b.resize(n > 0 ? (size_t)n : 0);
    In in(b);
    const size_t idlen = sizeof(((zquestheader *)NULL)->id_str);

    if(b.size() < idlen + 14)
    {
        why = "it isn't a quest file";
        return false;
    }

    gen[0] = 0;

    for(int k = 0; k < 8; ++k)
        ids[k] = 0;

    text.clear();
    made = "an old version of ZC";

    if(!memcmp(&b[0], QH_IDSTR, strlen(QH_IDSTR)))
        return true;            // before 1.93

    if(memcmp(&b[0], QH_NEWIDSTR, strlen(QH_NEWIDSTR)))
    {
        why = "it isn't a quest file";
        return false;
    }

    in.pos = idlen;
    in.m32();                   // the header's id
    const word hv = in.w();
    in.w();                     // cversion
    const dword size = in.l();
    const word zv = in.w();

    if(in.bad || size < 2 || !in.has(size - 2))
    {
        why = "its header is damaged";
        return false;
    }

    if(zv != 0x255)
    {
        made = zv > 0x255 ? "a newer version of ZC" : "2.53 or older";
        gen[0] = zv > 0x255 ? 3 : 0;
        return true;
    }

    // the player's header reader, with what it notes about the open quest put back after
    char built[48];
    memcpy(built, q255_quest_built_with, sizeof(built));
    const int hbuild = q255_header_build;
    int hver[3], hids[8];
    memcpy(hver, q255_header_version, sizeof(hver));
    memcpy(hids, q255_header_version_ids, sizeof(hids));
    const bool hnightly = q255_header_nightly;
    const std::string htext = q255_header_version_text;

    zquestheader h;
    memset(&h, 0, sizeof(h));
    Q255Section s;
    s.data = &b[in.pos];
    s.size = size - 2;
    s.version = hv;
    s.keep = true;
    s.header = &h;
    const int ret = q255_read_header(s, NULL, NULL);

    for(int k = 0; k < 8; ++k)
        ids[k] = q255_header_version_ids[k];

    text = q255_header_version_text;
    made = q255_quest_built_with;

    memcpy(q255_quest_built_with, built, sizeof(built));
    q255_header_build = hbuild;
    memcpy(q255_header_version, hver, sizeof(hver));
    memcpy(q255_header_version_ids, hids, sizeof(hids));
    q255_header_nightly = hnightly;
    q255_header_version_text = htext;

    if(ret != 0 || s.failed)
    {
        why = "its header doesn't read";
        return false;
    }

    gen[0] = ids[0] >= 3 ? 2 : 1;

    if(made.empty())
        made = gen[0] == 2 ? "3.0" : "2.55";

    return true;
}

} // namespace

namespace
{

// A version as numbers to compare in order: major, minor, patch, fourth, then the stage (alpha 1,
// beta 2, gamma 3, release 4, none 0) and its number, then a 3.0 prerelease's number from the
// version text ("3.0.0-prerelease.211+..."; none: past every prerelease).
std::vector<int64_t> version_order(const int *id, const std::string &text)
{
    std::vector<int64_t> v(id, id + 4);
    const int stage = id[7] ? 4 : id[6] ? 3 : id[5] ? 2 : id[4] ? 1 : 0;
    v.push_back(stage);
    v.push_back(stage ? id[3 + stage] : 0);
    const size_t at = text.find("-prerelease.");
    v.push_back(at == std::string::npos ? INT64_MAX : atoll(text.c_str() + at + 12));
    return v;
}

} // namespace

bool zqx_quest_not_older(const char *path, bool packed, std::string &made, std::string &why)
{
    int gen = 0, ids[8];
    std::string text;

    if(!peek_quest(path, packed, &gen, ids, text, made, why))
        return false;

    const int own_gen = zqx_is_30() ? 2 : 1;
    bool older = gen < own_gen;

    if(gen == own_gen)
        older = version_order(ids, text) < version_order(q255_header_version_ids, q255_header_version_text);

    if(older)
    {
        why = "it was made in " + made + ", an older version than this quest's (" + std::string(q255_quest_built_with) +
              "), and a quest is saved in its own version or newer, never older. Open it with File > Open instead.";
        return false;
    }

    return true;
}

bool zqx_timed_save_due()
{
    // ZQuest's own "unsaved" flag first, as upstream's: ZQuest's data can move without an edit
    // (a screen marked valid as it is shown), which alone isn't worth a timed save
    extern bool saved;

    if(saved)
        return false;

    return g_import_pending || zq_changed_since_save();
}

// ---- timed saves ----------------------------------------------------------------------------

namespace
{

std::string dir_of(const std::string &p)
{
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string("") : p.substr(0, s + 1);
}

std::string name_of(const std::string &p)
{
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

bool make_dir(const std::string &d)
{
#ifdef _WIN32
    if(_mkdir(d.c_str()) == 0 || errno == EEXIST)
        return true;
#else
    if(mkdir(d.c_str(), 0755) == 0 || errno == EEXIST)
        return true;
#endif
    return false;
}

// The version upstream names a quest's timed saves with: 3.0 its header's version text (header
// v9) or major.minor.patch (zquestheader::getVerStr, zelda_version_string); 2.55.17 the release
// "2.55.N", or for a quest from before the release "2.55.N <stage> <number>" (getVerStr).
std::string upstream_version_name()
{
    const int *id = q255_header_version_ids;
    char b[160];

    if(zqx_is_30())
    {
        // (its header keeps 44 characters of the text)
        if(!q255_header_version_text.empty())
            return q255_header_version_text.substr(0, 44);

        snprintf(b, sizeof(b), "%d.%d.%d", id[0], id[1], id[2]);
        return b;
    }

    if(!(id[0] < 2 || (id[0] == 2 && id[1] < 55) || (id[0] == 2 && id[1] == 55 && id[7] == 0)))
    {
        snprintf(b, sizeof(b), "%d.%d.%d", id[0], id[1], id[2]);
        return b;
    }

    const char *stage = id[7] ? "Release" : id[6] ? "Gamma" : id[5] ? "Beta" : id[4] ? "Alpha" : "Unknown";
    const int n = id[7] ? id[7] : id[6] ? id[6] : id[5] ? id[5] : id[4];
    char alpha[80];

    if(q255_header_nightly)
    {
        if(n < 0)
            snprintf(alpha, sizeof(alpha), "Nightly (%s ?\?)", stage);
        else
            snprintf(alpha, sizeof(alpha), "Nightly (%s %d/%d)", stage, n - 1, n);
    }
    else if(n < 0)
        snprintf(alpha, sizeof(alpha), "%s ?\?", stage);
    else
        snprintf(alpha, sizeof(alpha), "%s %d", stage, n);

    if(id[3] > 0)
        snprintf(b, sizeof(b), "%d.%d.%d.%d %s", id[0], id[1], id[2], id[3], alpha);
    else
        snprintf(b, sizeof(b), "%d.%d.%d %s", id[0], id[1], id[2], alpha);

    return b;
}

struct BackupFile
{
    std::string path;
    int64_t when;
};

// The files in dir whose names hold marker, with their last-write times.
void list_marked(const std::string &dir, const char *marker, std::vector<BackupFile> &out)
{
    out.clear();
#ifdef _WIN32
    struct __finddata64_t fd;
    intptr_t h = _findfirst64((dir + "\\*").c_str(), &fd);

    if(h == -1)
        return;

    do
    {
        if(fd.attrib & _A_SUBDIR)
            continue;

        if(!strstr(fd.name, marker))
            continue;

        BackupFile b;
        b.path = dir + "\\" + fd.name;
        b.when = (int64_t)fd.time_write;
        out.push_back(b);
    }
    while(_findnext64(h, &fd) == 0);

    _findclose(h);
#else
    DIR *d = opendir(dir.c_str());

    if(!d)
        return;

    while(struct dirent *e = readdir(d))
    {
        if(!strstr(e->d_name, marker))
            continue;

        BackupFile b;
        b.path = dir + "/" + e->d_name;
        struct stat st;

        if(stat(b.path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
            continue;

        b.when = (int64_t)st.st_mtime;
        out.push_back(b);
    }

    closedir(d);
#endif
}

} // namespace

int zqx_timed_save(const char *quest_path, int retention, bool uncompressed, std::string &path, std::string &why)
{
    const std::string q = quest_path;
    const std::string name = name_of(q);
#ifdef _WIN32
    const std::string folder = dir_of(q) + "backups." + name;
#else
    const std::string folder = q + ".backups";
#endif
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y_%m_%d %H%M%S", tm);
    const std::string ver = upstream_version_name();
    std::string file = std::string(stamp) + "--autosave--" + (ver.empty() ? std::string("") : "v" + ver + "--") + name;

    // .qsu when not packed, as fs::path::replace_extension makes it: the name from its last dot
    // on is replaced (the name starts with the date, so a dot is never its first character)
    if(uncompressed)
    {
        size_t dot = file.find_last_of('.');

        if(dot != std::string::npos && dot > 0)
            file = file.substr(0, dot);

        file += ".qsu";
    }

    if(!make_dir(folder))
    {
        why = "the folder " + folder + " couldn't be made";
        return 1;
    }

#ifdef _WIN32
    path = folder + "\\" + file;
#else
    path = folder + "/" + file;
#endif
    int r = zq_save_own_format_packed(path.c_str(), !uncompressed, why);

    if(r)
        return r;

    // perform_backup_retention: past the limit, the oldest timed save goes (one at a time)
    if(retention > 0)
    {
        std::vector<BackupFile> files;
        list_marked(folder, "--autosave--", files);

        if((int)files.size() > retention)
        {
            size_t oldest = 0;

            for(size_t k = 1; k < files.size(); ++k)
                if(files[k].when < files[oldest].when)
                    oldest = k;

            remove(files[oldest].path.c_str());
        }
    }

    return 0;
}

// ---- test switches --------------------------------------------------------------------------
//
// The same exports and imports the menus run, from the command line, for checking files against
// upstream's (the menus are checked through the window):
//   -qst-export in.qst dmaps|strings|combos|tiles|guys|zgp|zqt|qsu|zitem|znpc out [a b]
//       (dmaps: first and last DMap; tiles: first page and page count; zitem, znpc: which one)
//   -qst-import in.qst dmaps|strings|combos|tiles|guys|zgp|zqt|qsu file out.qst [dest]
//       (combos and tiles: the first combo or tile; zitem, znpc (also accepted here): the item
//       or enemy to replace; then saved in the quest's own version)
//   -qst-timed-save in.qst retention uncompressed
// The result goes to allegro.log; the exit code is 0 when it worked.

int load_quest(const char *filename, bool compressed, bool encrypted);
extern bool skip_quest_password;
extern bool zq_batch_load;

int zqx_test_switches(int argc, char **argv)
{
    int at = 0;

    for(int i = 1; i < argc && !at; ++i)
        if(!strcmp(argv[i], "-qst-export") || !strcmp(argv[i], "-qst-import") || !strcmp(argv[i], "-qst-timed-save"))
            at = i;

    if(!at || argc <= at + 2)
        return -1;

    const std::string sw = argv[at];
    skip_quest_password = true;
    zq_batch_load = true;
    int ret = load_quest(argv[at + 1], true, true);
    std::string why, note, kind = argv[at + 2];
    bool ok = false;

    if(ret != qe_OK)
        why = "the quest didn't load";
    else if(!zq255_quest_is_newer())
        why = "not a 2.55 / 3.0 quest";
    else if(sw == "-qst-timed-save" && !strcmp(argv[at + 2], "due"))
    {
        // -qst-timed-save in.qst due: whether a timed save would run straight after opening
        // the quest were it marked unsaved (it shouldn't), and which parts count as changed
        extern bool saved;
        saved = false;
        ok = true;
        note = zqx_timed_save_due() ? "due:" : "not due";
        std::vector<std::string> absent;
        zq_absent_changed(absent);

        for(size_t i = 0; i < absent.size(); ++i)
            note += " " + absent[i];

        for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
            if(zq_part_changed(zq_quest_file.parts[i]))
                note += " " + zq_part_name(zq_quest_file.parts[i]);
    }
    else if(sw == "-qst-timed-save")
    {
        std::string path;
        ok = zqx_timed_save(argv[at + 1], atoi(argv[at + 2]), argc > at + 3 && atoi(argv[at + 3]) != 0, path, why) == 0;
        note = path;
    }
    else if(argc <= at + 3)
        why = "too few arguments";
    else if(sw == "-qst-export")
    {
        const char *out = argv[at + 3];
        const int a = argc > at + 4 ? atoi(argv[at + 4]) : 0;
        const int b = argc > at + 5 ? atoi(argv[at + 5]) : (kind == "dmaps" ? MAX_ZDMAPS - 1 : 1);

        if(kind == "dmaps") ok = zqx_export_dmaps(out, a, b, why);
        else if(kind == "strings") ok = zqx_export_strings(out, why);
        else if(kind == "combos") ok = zqx_export_combos(out, why);
        else if(kind == "tiles") ok = zqx_export_tiles(out, a, b, why);
        else if(kind == "guys") ok = zqx_export_guys(out, why);
        else if(kind == "zitem" || kind == "znpc") ok = zqx_export_one(kind == "zitem", a, out, why);
        else if(kind == "zgp") ok = zqx_export_zgp(out, why);
        else if(kind == "zqt" || kind == "qsu") ok = zqx_export_quest(out, kind == "zqt", why);
        else if(kind == "all")
        {
            // every kind into the folder out (DMaps 0-511; 3.0 tiles: every page), as
            // <kind>.<extension>, and what didn't work in note
            static const char *kinds[] = { "dmaps.zdmap", "strings.zqs", "combos.zcombo", "tiles.ztileset", "guys.guy", "gp.zgp", "quest.qsu", "quest.zqt" };
            ok = true;

            for(int k = 0; k < 8; ++k)
            {
                const std::string path = std::string(out) + "/" + kinds[k];
                std::string w;
                bool r = false;

                switch(k)
                {
                case 0: r = zqx_export_dmaps(path.c_str(), 0, MAX_ZDMAPS - 1, w); break;
                case 1: r = zqx_export_strings(path.c_str(), w); break;
                case 2: r = zqx_export_combos(path.c_str(), w); break;
                case 3: r = zqx_export_tiles(path.c_str(), 0, NEWMAXTILES / TILES_PER_PAGE, w); break;
                case 4: r = zqx_export_guys(path.c_str(), w); break;
                case 5: r = zqx_export_zgp(path.c_str(), w); break;
                default: r = zqx_export_quest(path.c_str(), k == 7, w); break;
                }

                if(!r)
                {
                    ok = false;
                    why += std::string(why.empty() ? "" : "; ") + kinds[k] + ": " + w;
                }
            }
        }
        else why = "unknown kind";
    }
    else if(argc <= at + 4)
        why = "too few arguments";
    else
    {
        const char *file = argv[at + 3], *out = argv[at + 4];
        const int dest = argc > at + 5 ? atoi(argv[at + 5]) : 0;
        int max_map = 0;
        std::string made;

        if(kind == "dmaps") ok = zqx_import_dmaps(file, &max_map, why, note);
        else if(kind == "strings") ok = zqx_import_strings(file, why, note);
        else if(kind == "combos") ok = zqx_import_combos(file, dest, why, note);
        else if(kind == "tiles") ok = zqx_import_tiles(file, dest, why, note);
        else if(kind == "guys") ok = zqx_import_guys(file, why, note);
        else if(kind == "zitem" || kind == "znpc") ok = zqx_import_one(kind == "zitem", dest, file, why, note);
        else if(kind == "zgp") ok = zqx_import_zgp(file, why, note);
        else if(kind == "zqt" || kind == "qsu")
        {
            ok = zqx_quest_not_older(file, kind == "zqt", made, why);

            if(ok)
            {
                ret = load_quest(file, kind == "zqt", false);
                ok = ret == qe_OK;
                note = "made in " + made;

                if(!ok)
                    why = "it didn't load";
            }
        }
        else why = "unknown kind";

        // ZQX_SAME_CHECK=1: instead of saving, which parts a save would now write differently
        // from the quest's file (an import of the quest's own exports should change none, and
        // neither should an import that was refused)
        if(getenv("ZQX_SAME_CHECK"))
        {
            std::string changed;

            for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
            {
                const ZqQuestPart &p = zq_quest_file.parts[i];
                std::vector<byte> now, was((size_t)(p.length > 0 ? p.length : 0));

                if(!zq_own_part_bytes(p, now))
                    changed += " " + zq_part_name(p) + "(can't be written)";
                else if(now.size() != was.size() || (!was.empty() && (!zq_part_read(p, 0, &was[0], was.size()) || memcmp(&now[0], &was[0], was.size()))))
                    changed += " " + zq_part_name(p);
            }

            (ok ? note : why) += changed.empty() ? "; nothing a save writes changed" : "; changed:" + changed;
        }
        else if(ok)
        {
            std::string w;
            ok = zq_save_own_format(out, true, w) == 0;

            if(!ok)
                why = "imported, but the save failed: " + w;
        }
    }

    al_trace("%s %s: %s %s\n", sw.c_str(), kind.c_str(), ok ? "ok" : "FAILED", ok ? note.c_str() : why.c_str());

    // ZQX_REPORT=file: the same line there too (allegro.log is shared by parallel runs)
    if(const char *rep = getenv("ZQX_REPORT"))
        if(FILE *f = fopen(rep, "a"))
        {
            fprintf(f, "%s", sw.c_str());

            for(int k = at + 1; k < argc; ++k)
                fprintf(f, " %s", argv[k]);

            fprintf(f, ": %s %s\n", ok ? "ok" : "FAILED", ok ? note.c_str() : why.c_str());
            fclose(f);
        }

    return ok ? 0 : 3;
}

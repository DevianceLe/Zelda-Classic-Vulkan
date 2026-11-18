// Compiling a 2.55 or 3.0 Nightly quest's ZScript in ZQuest: the work behind the compile dialog
// (zq_compile_dialog.cpp). See zq_compile.h for the outline.
//
// The quest's scripts section (FFSC) is read once after every load and every save in the
// quest's own format: the slot names of all 13 script types (what 2.53's maps keep only for
// three of them) and each slot's record as stored, for the slots a compile leaves alone (upstream
// never touches slot 0 of the types that start at slot 1, and 2.55.17 keeps an imported ZASM
// script whose name the compile doesn't give).
//
// A compile runs the generation's own compiler program (zscript255 / zscript30, beside ZQuest)
// as upstream's ZQuest runs its zscript: the buffer in a file, -console, -qr with the quest's
// rules as upstream's loader has them, -linked, -include with the quest's own scripts folder,
// and (3.0) the quest's compiler settings. It hands back every script as that generation's
// ZQuest stores it in a slot (-assembled), so nothing here assembles ZASM.
//
// Assigning: each slot that names a compiled script gets its record; a named slot whose script
// the compile didn't give keeps its name but loses its code; an empty slot is empty (upstream's
// do_slots, and 2.55.17's do_script_disassembly before it). The whole section is then written in
// the generation's format and read back with the player's reader into ZQuest's tables (the
// "commit"), so everything else in ZQuest sees the new scripts; the save writes it again with
// the buffer and names as they are then (zq_compile_section, called by zqw_scripts.cpp). The
// quest's "last compiled ZScript version" (MISC) becomes the generation's (22 or 31), as
// upstream sets it.
//
// Written for the port; the slot rules are ported from upstream ZQuest Classic (GPL) 3.0 and
// 2.55.17, src/zq/zquest.cpp (do_slots, smart_slot_named, smart_slot_type, reload_scripts,
// do_script_disassembly) and src/zq/zq_class.cpp (writeffscript).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "q255.h"
#include "zq_compile.h"
#include "zq_compile_proc.h"
#include "zq_q255_store.h"
#include "zq_rule_names.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zq_tape.h"
#include "../zscript255/zs255_result.h"

extern std::string zScript;
extern std::map<int, std::pair<std::string, std::string> > ffcmap;
extern std::map<int, std::pair<std::string, std::string> > globalmap;
extern std::map<int, std::pair<std::string, std::string> > itemmap;
extern char *filepath;
extern bool saved;
extern int q255_header_version[3];
extern zquestheader header;
bool zq255_quest_is_newer();
void zasm30_reset();
bool zq255_misc_commit(const std::map<std::string, TapeField> &edits, std::string &why);
bool zq255_item_records_all(std::vector<std::vector<byte> > &recs, int *version);  // zqw_items.cpp
bool zq255_guy_records_all(std::vector<std::vector<byte> > &recs, int *version);   // zqw_guys.cpp
bool zq255_dmap_records_all(std::vector<std::vector<byte> > &recs, int *version);  // zqw_maps.cpp
bool zq255_screen_records_all(std::vector<std::vector<byte> > &recs, int *version);
extern itemdata *itemsbuf;
extern std::vector<mapscr> TheMaps;
bool zq255_init_record(std::vector<byte> &rec, int *version);                 // zqw_init.cpp
bool zq255_subs_record(std::vector<byte> &rec, int *version);
bool zq255_misc_record(std::vector<byte> &rec, int *version);                 // zqw_basic.cpp

namespace
{

const ZqSlotTypeInfo g_types[ZQS_COUNT] =
{
    { "FFC", 512, 511, 1 },
    { "Global", 8, 8, 0 },
    { "Item", 256, 255, 1 },
    { "NPC", 256, 255, 1 },
    { "LWeapon", 256, 255, 1 },
    { "EWeapon", 256, 255, 1 },
    { "Hero", 5, 4, 1 },
    { "DMap", 256, 255, 1 },
    { "Screen", 256, 255, 1 },
    { "Item Sprite", 256, 255, 1 },
    { "Combo", 512, 511, 1 },
    { "Generic", 512, 511, 1 },
    { "Subscreen", 256, 255, 1 },
};

// The slot names upstream's Smart Assign matches global and hero scripts by.
const char *const g_global_slotnames[8] = { "Init", "Active", "onExit", "onSaveLoad", "onLaunch", "onContGame", "onF6Menu", "onSave" };
const char *const g_hero_slotnames[4] = { "Init", "Active", "onDeath", "onWin" };

// The record tables of the section in file order: a slot type, or -1 for the old weapon scripts
// both generations write as empty slots.
const int g_table_order[14] =
{
    ZQS_FFC, ZQS_ITEM, ZQS_NPC, -1, ZQS_SCREEN, ZQS_GLOBAL, ZQS_HERO,
    ZQS_LWEAPON, ZQS_EWEAPON, ZQS_DMAP, ZQS_ITEMSPRITE, ZQS_COMBO, ZQS_GENERIC, ZQS_SUBSCREEN
};

const word TERMINATOR = 0xFFFF;
const byte ZMETA_IMPORTED_FLAG = 0x04;
const int V_255 = 22, META_255 = 5;     // 2.55.17's V_FFSCRIPT and METADATA_V
const int V_30 = 31, META_30 = 7;       // 3.0's

// A slot's record as the quest's section has it.
struct LoadedSlot
{
    bool code;                  // it holds a script
    byte flags;                 // its metadata's flags (ZMETA_*)
    int script_type;            // its metadata's script type
    std::string script_name;    // its metadata's script name
    std::vector<word> exports;  // 3.0: the ids of its exported variables
    std::vector<byte> bytes;    // the record, kept for slots holding a script
    LoadedSlot() : code(false), flags(0), script_type(0) {}
};

struct Store
{
    bool active;                // a 2.55/3.0 quest whose scripts section was read
    bool is30;
    int version, meta;          // the section's version and metadata layout as loaded
    int last_compiled;          // MISC's "last compiled ZScript version" (-1 none)
    std::map<int, std::string> names[ZQS_COUNT];
    std::vector<LoadedSlot> loaded[ZQS_COUNT];
    std::vector<byte> loaded_debug;     // 3.0: the debug data of the compile the slots hold

    // after a compile was assigned
    bool pending;
    long serial;
    std::vector<std::vector<byte> > records[ZQS_COUNT];     // by table slot; empty: an empty slot
    std::vector<byte> zasm, debug;

    Store() : active(false), is30(false), version(0), meta(0), last_compiled(-1), pending(false), serial(0) {}
};

Store g_st;
std::mutex g_mutex;

// ---- reading the section ----------------------------------------------------------------------

struct Rd
{
    const byte *d;
    size_t n, pos;
    bool bad;

    Rd(const byte *data, size_t size) : d(data), n(size), pos(0), bad(false) {}
    bool has(size_t k) const { return !bad && pos <= n && k <= n - pos; }
    long c() { if(!has(1)) { bad = true; return 0; } return d[pos++]; }
    long w() { if(!has(2)) { bad = true; return 0; } long v = d[pos] | (d[pos + 1] << 8); pos += 2; return v; }
    long l()
    {
        if(!has(4)) { bad = true; return 0; }
        dword v = (dword)d[pos] | ((dword)d[pos + 1] << 8) | ((dword)d[pos + 2] << 16) | ((dword)d[pos + 3] << 24);
        pos += 4;
        return (long)(int)v;
    }
    void skip(size_t k) { if(!has(k)) { bad = true; return; } pos += k; }
    void cstr() { skip((size_t)c()); }
    std::string cstr_get()
    {
        size_t k = (size_t)c();
        if(!has(k)) { bad = true; return std::string(); }
        std::string v((const char *)d + pos, k);
        pos += k;
        return v;
    }
    void wstr() { skip((size_t)w()); }
    void lstr() { long k = l(); if(k < 0) bad = true; else skip((size_t)k); }
};

void label_help(Rd &r, int count)
{
    for(int i = 0; i < count && !r.bad; ++i)
    {
        r.cstr();
        r.wstr();
    }
}

// A script's metadata in the layout 'meta' (the reader's skip_meta), noting its type and flags.
void read_meta(Rd &r, int meta, LoadedSlot &out)
{
    r.skip(6);
    out.script_type = (int)r.c();

    for(int i = 0; i < 8 && !r.bad; ++i)
    {
        if(meta < 3)
            r.skip(33);
        else
            r.cstr();
    }

    r.skip(8);
    out.flags = (byte)r.c();
    r.skip(8);

    if(meta == 2)
        r.skip(66);
    else if(meta > 2)
    {
        out.script_name = r.cstr_get();
        r.cstr();

        if(meta >= 7)
        {
            r.wstr();
            r.wstr();
        }

        if(meta < 6)
        {
            label_help(r, meta < 5 ? 4 : 10);
            label_help(r, 8);
            label_help(r, 8);
        }
        else
        {
            long count = r.w();
            if(count > 24)
                r.bad = true;
            label_help(r, (int)count);
        }

        label_help(r, 16);
    }

    if(meta > 3)
    {
        label_help(r, 8);
        r.skip(8);
    }
}

void skip_literal(Rd &r)
{
    r.lstr();
    long count = r.l();
    if(count < 0 || (size_t)count > (r.n - r.pos) / 4)
        r.bad = true;
    else
        r.skip((size_t)count * 4);
}

// A 3.0 slot's two tables: the instance variables' initial values, and the exported variables
// (their ids are kept in 'exports').
void instance_tables(Rd &r, int v, std::vector<word> &exports)
{
    long size = r.w();

    if(size)
    {
        long pairs = r.c();
        long count = r.w();
        r.skip((size_t)count * (pairs ? 6 : 4));
    }

    size = r.w();

    if(size)
    {
        long pairs = r.c();
        long count = r.w();

        for(long i = 0; i < count && !r.bad; ++i)
        {
            // stored as (id, variable) pairs, or as every id from 0 with unused ones blank
            const long id = pairs ? r.w() : i;
            if(!r.cstr_get().empty())
                exports.push_back((word)id);
            r.lstr();
            r.skip(1);

            if(v >= 31)
            {
                r.skip(3);
                long names = r.w();
                for(long k = 0; k < names && !r.bad; ++k)
                {
                    r.skip(4);
                    r.lstr();
                }
            }

            r.skip(8);
        }
    }
}

// One slot's record. keep: store it (else only walk it).
void read_slot(Rd &r, int v, int meta, LoadedSlot *out)
{
    LoadedSlot tmp;
    LoadedSlot &s = out ? *out : tmp;
    size_t at = r.pos;

    if(v >= 27)
    {
        long exists = r.c();

        if(exists)
        {
            read_meta(r, meta, s);
            r.skip(8);      // start and end
            if(v >= 30)
                instance_tables(r, v, s.exports);
            s.code = true;
        }
    }
    else
    {
        long n = r.l();

        if(v >= 16)
            read_meta(r, meta, s);

        for(long j = 0; j < n && !r.bad; ++j)
        {
            if(r.w() == TERMINATOR)
                break;
            s.code = true;
            r.skip(8);
            if(v >= 24)
                r.skip(4);
            if(v >= 21)
                skip_literal(r);
        }
    }

    if(out && s.code && !r.bad)
        s.bytes.assign(r.d + at, r.d + r.pos);
}

void read_table(Rd &r, int v, int meta, int count, int type, Store *st)
{
    std::vector<LoadedSlot> *tab = NULL;

    if(st && type >= 0)
    {
        tab = &st->loaded[type];
        tab->assign((size_t)g_types[type].table, LoadedSlot());
    }

    for(int i = 0; i < count && !r.bad; ++i)
        read_slot(r, v, meta, tab && i < (int)tab->size() ? &(*tab)[i] : NULL);
}

void read_names(Rd &r, int type, Store *st)
{
    long count = r.w();

    for(long i = 0; i < count && !r.bad; ++i)
    {
        long id = r.w();
        long len = r.l();

        if(len < 0 || len > 1024 || !r.has((size_t)len))
        {
            r.bad = true;
            return;
        }

        // as upstream's maps keep them: up to a NUL, ids within the table
        std::string name((const char *)r.d + r.pos, (size_t)len);
        r.pos += (size_t)len;
        size_t nul = name.find('\0');
        if(nul != std::string::npos)
            name.resize(nul);

        if(!st || type < 0)
            continue;

        int limit = type == ZQS_GLOBAL ? 8 : g_types[type].table - 1;

        if(id >= limit)
            continue;

        if(type == ZQS_GLOBAL && name == "~Continue")
            st->names[type][(int)id] = "";
        else
            st->names[type][(int)id] = name;
    }
}

// The whole body, as q255_read_ffscript walks it. False if it doesn't fit exactly.
bool read_body(const byte *d, size_t n, int v, int meta, Store *st)
{
    Rd r(d, n);

    if(v >= 27)
    {
        long count = r.l();

        for(long j = 0; j < count && !r.bad; ++j)
        {
            r.skip(2 + 12);
            skip_literal(r);
        }
    }

    read_table(r, v, meta, 512, ZQS_FFC, st);
    read_table(r, v, meta, 256, ZQS_ITEM, st);
    read_table(r, v, meta, 256, ZQS_NPC, st);
    read_table(r, v, meta, 256, -1, st);
    read_table(r, v, meta, 256, ZQS_SCREEN, st);
    read_table(r, v, meta, v > 16 ? 8 : (v > 13 ? 7 : 4), ZQS_GLOBAL, st);
    read_table(r, v, meta, v > 10 ? 5 : 3, ZQS_HERO, st);

    if(v == 9)
    {
        read_table(r, v, meta, 256, ZQS_EWEAPON, st);
        read_table(r, v, meta, 256, ZQS_DMAP, st);
    }

    if(v >= 10)
    {
        read_table(r, v, meta, 256, ZQS_LWEAPON, st);
        read_table(r, v, meta, 256, ZQS_EWEAPON, st);
        read_table(r, v, meta, 256, ZQS_DMAP, st);
    }

    if(v >= 12)
        read_table(r, v, meta, 256, ZQS_ITEMSPRITE, st);
    if(v >= 15)
        read_table(r, v, meta, 512, ZQS_COMBO, st);
    if(v > 19)
        read_table(r, v, meta, (int)r.w(), ZQS_GENERIC, st);
    if(v > 21)
        read_table(r, v, meta, (int)r.w(), ZQS_SUBSCREEN, st);

    r.lstr();       // the source

    read_names(r, ZQS_FFC, st);
    read_names(r, ZQS_GLOBAL, st);
    read_names(r, ZQS_ITEM, st);

    if(v > 8)
    {
        read_names(r, ZQS_NPC, st);
        read_names(r, ZQS_LWEAPON, st);
        read_names(r, ZQS_EWEAPON, st);
        read_names(r, ZQS_HERO, st);
        read_names(r, ZQS_DMAP, st);
        read_names(r, ZQS_SCREEN, st);
    }

    if(v > 11)
        read_names(r, ZQS_ITEMSPRITE, st);
    if(v >= 15)
        read_names(r, ZQS_COMBO, st);
    if(v > 19)
        read_names(r, ZQS_GENERIC, st);
    if(v > 21)
        read_names(r, ZQS_SUBSCREEN, st);

    if(v >= 29)
    {
        // the debug data
        const long len = r.l();

        if(len < 0 || !r.has((size_t)len))
            r.bad = true;
        else
        {
            if(st)
                st->loaded_debug.assign(r.d + r.pos, r.d + r.pos + len);
            r.pos += (size_t)len;
        }
    }

    return !r.bad && r.pos == n;
}

const ZqQuestPart *find_part(dword id)
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == id)
            return &zq_quest_file.parts[i];
    return NULL;
}

bool part_bytes(const ZqQuestPart &p, std::vector<byte> &out)
{
    out.resize((size_t)p.length);
    return p.length == 0 || zq_part_read(p, 0, &out[0], out.size());
}

int read_last_compiled()
{
    const ZqQuestPart *p = find_part(ID_MISC);
    std::vector<byte> b;

    if(!p || !part_bytes(*p, b) || b.size() < 8)
        return -1;

    const int v = b[0] | (b[1] << 8);

    if(v < 11)
        return -1;

    const std::vector<byte> body(b.begin() + 8, b.end());
    Tape t;
    TapeParser tp(body.empty() ? NULL : &body[0], body.size(), 0, &t);

    if(!zq_tape_misc(tp, v))
        return -1;

    const TapeField *f = t.find("zscript_version");

    if(!f || f->bytes.size() != 4)
        return -1;

    return (int)((dword)f->bytes[0] | ((dword)f->bytes[1] << 8) | ((dword)f->bytes[2] << 16) | ((dword)f->bytes[3] << 24));
}

// After every load and every save in the quest's own format.
void note_loaded()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    long serial = g_st.serial;
    g_st = Store();
    g_st.serial = serial;

    const ZqQuestPart *p = zq255_quest_is_newer() ? find_part(ID_FFSCRIPT) : NULL;
    std::vector<byte> b;

    if(!p || !part_bytes(*p, b) || b.size() < 8)
        return;

    const int v = b[0] | (b[1] << 8);

    if(v < 6 || v > 31)
        return;

    // from version 18 a metadata version word comes before the size; the layout that fits the
    // section exactly is the one its reader uses
    size_t body = v >= 18 ? 10 : 8;

    if(b.size() < body)
        return;

    int meta = -1;
    int stored = v >= 18 ? (b[4] | (b[5] << 8)) : 0;

    if(v < 16 || (stored <= 7 && read_body(&b[body], b.size() - body, v, stored, NULL)))
        meta = v < 16 ? 0 : stored;

    for(int m = 0; m <= 7 && meta < 0; ++m)
        if(read_body(&b[body], b.size() - body, v, m, NULL))
            meta = m;

    if(meta < 0 || !read_body(&b[body], b.size() - body, v, meta, &g_st))
    {
        g_st = Store();
        g_st.serial = serial;
        return;
    }

    for(int t = 0; t < ZQS_COUNT; ++t)
        if(g_st.loaded[t].empty())
            g_st.loaded[t].assign((size_t)g_types[t].table, LoadedSlot());

    g_st.active = true;
    g_st.version = v;
    g_st.meta = meta;
    // A 2.55 header with a 3.0 Nightly's script layout (versions past 2.55.17's 22) is a 3.0 quest.
    g_st.is30 = q255_header_version[0] >= 3 || v > V_255;
    g_st.last_compiled = read_last_compiled();
}

ZqLoadHookRegistration g_hook(note_loaded);

// A compile that was assigned counts as a change to the scripts section even when the new code
// is the same as the old (its metadata carries the compiler's build date, as upstream's does).
void hash_pending(uint64_t *h)
{
    if(g_st.pending)
        zq_hash_add(h, &g_st.serial, sizeof(g_st.serial));
}

ZqStoreHashRegistration g_hash(ID_FFSCRIPT, hash_pending);

// ---- the compiler -------------------------------------------------------------------------------

int slot_type_of(int parser_id)
{
    // ParserScriptType / ScriptTypeID, the same numbers in both generations
    switch(parser_id)
    {
    case 1: return ZQS_GLOBAL;
    case 2: return ZQS_FFC;
    case 3: return ZQS_ITEM;
    case 4: return ZQS_NPC;
    case 5: return ZQS_EWEAPON;
    case 6: return ZQS_LWEAPON;
    case 7: return ZQS_HERO;
    case 8: return ZQS_SCREEN;
    case 9: return ZQS_DMAP;
    case 10: return ZQS_ITEMSPRITE;
    case 12: return ZQS_COMBO;
    case 13: return ZQS_SUBSCREEN;
    case 14: return ZQS_GENERIC;
    }
    return -1;
}

bool read_file(const std::string &path, std::vector<byte> &out)
{
    out.clear();
    FILE *f = fopen(path.c_str(), "rb");

    if(!f)
        return false;

    byte buf[65536];
    size_t k;

    while((k = fread(buf, 1, sizeof(buf), f)) > 0)
        out.insert(out.end(), buf, buf + k);

    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

dword get32(const std::vector<byte> &d, size_t &pos, bool &ok)
{
    if(pos + 4 > d.size())
    {
        ok = false;
        return 0;
    }

    dword v = (dword)d[pos] | ((dword)d[pos + 1] << 8) | ((dword)d[pos + 2] << 16) | ((dword)d[pos + 3] << 24);
    pos += 4;
    return v;
}

bool get_bytes(const std::vector<byte> &d, size_t &pos, std::vector<byte> &out, bool &ok)
{
    dword len = get32(d, pos, ok);

    if(!ok || len > d.size() - pos)
        return ok = false;

    out.assign(d.begin() + pos, d.begin() + pos + len);
    pos += len;
    return true;
}

// zscript30's -assembled file (zscript30/zq/zq_assemble.cpp).
bool read_assembled_30(const std::string &path, ZqCompileResult &r)
{
    std::vector<byte> d;

    if(!read_file(path, d) || d.size() < 24 || memcmp(&d[0], "ZS30ASM", 8) != 0)
        return false;

    size_t pos = 8;
    bool ok = true;
    dword format = get32(d, pos, ok), v = get32(d, pos, ok), meta = get32(d, pos, ok);
    get32(d, pos, ok);

    if(!ok || format != 2 || v != (dword)V_30 || meta != (dword)META_30)
        return false;

    std::map<std::string, int> types;
    dword count = get32(d, pos, ok);

    for(dword i = 0; i < count && ok; ++i)
    {
        std::vector<byte> nm;
        get_bytes(d, pos, nm, ok);
        int id = (int)get32(d, pos, ok);
        std::string name(nm.begin(), nm.end());
        types[name] = slot_type_of(id);

        if(ok && !name.empty() && name[0] != '~' && types[name] >= 0)
            r.names[types[name]].push_back(name);
    }

    count = get32(d, pos, ok);

    for(dword i = 0; i < count && ok; ++i)
    {
        ZqCompiledScript s;
        std::vector<byte> nm;
        get_bytes(d, pos, nm, ok);
        s.name.assign(nm.begin(), nm.end());

        if(pos >= d.size())
            ok = false;
        else
            ++pos;      // format

        get_bytes(d, pos, s.record, ok);
        s.type = types.count(s.name) ? types[s.name] : -1;
        r.scripts[s.name] = s;
    }

    get_bytes(d, pos, r.zasm, ok);
    get_bytes(d, pos, r.debug, ok);

    // the scripts whose instance variables moved since the quest's last compile
    count = get32(d, pos, ok);

    for(dword i = 0; i < count && ok; ++i)
    {
        std::vector<byte> nm;
        get_bytes(d, pos, nm, ok);
        ZqVarRemap &m = r.remaps[std::string(nm.begin(), nm.end())];
        dword k = get32(d, pos, ok);

        if(!ok || k > (d.size() - pos) / 3)
            return false;

        for(dword j = 0; j < k; ++j, pos += 3)
            m.old_kind[(word)(d[pos] | (d[pos + 1] << 8))] = d[pos + 2];

        k = get32(d, pos, ok);

        if(!ok || k > (d.size() - pos) / 5)
            return false;

        for(dword j = 0; j < k; ++j, pos += 5)
        {
            const word old_id = (word)(d[pos] | (d[pos + 1] << 8));
            m.old_to_new[old_id] = (word)(d[pos + 2] | (d[pos + 3] << 8));
            m.new_kind[old_id] = d[pos + 4];
        }

        k = get32(d, pos, ok);

        if(!ok || k > (d.size() - pos) / 6)
            return false;

        for(dword j = 0; j < k; ++j, pos += 6)
            m.initd_moves.push_back(std::make_pair((int)(d[pos] | (d[pos + 1] << 8) | (d[pos + 2] << 16) | (d[pos + 3] << 24)),
                                                   (word)(d[pos + 4] | (d[pos + 5] << 8))));
    }

    return ok && pos == d.size();
}

bool read_assembled_255(const std::string &path, ZqCompileResult &r)
{
    std::vector<zs255_result::AssembledScript> list;

    if(!zs255_result::read_assembled(path, list))
        return false;

    for(size_t i = 0; i < list.size(); ++i)
    {
        ZqCompiledScript s;
        s.name = list[i].name;
        s.type = slot_type_of(list[i].type);
        s.record = list[i].record;
        r.scripts[s.name] = s;
    }

    for(std::map<std::string, ZqCompiledScript>::const_iterator it = r.scripts.begin(); it != r.scripts.end(); ++it)
        if(!it->first.empty() && it->first[0] != '~' && it->second.type >= 0)
            r.names[it->second.type].push_back(it->first);

    return true;
}

// The quest's rules as upstream's loader has them, as zscript's -qr wants them.
std::string rules_hex(bool is30, std::vector<std::pair<word, long> > *settings)
{
    ZqRuleSection s;
    unsigned char rules[100];
    memset(rules, 0, sizeof(rules));

    if(zq255_rules_get(s))
    {
        memcpy(rules, s.rules, s.total < 100 ? s.total : 100);
        char title[66], author[66];
        memcpy(title, header.title, 65);
        memcpy(author, header.author, 65);
        title[65] = author[65] = 0;
        ZqRuleQuest q;
        q.zelda_version = header.zelda_version;
        q.build = header.build;
        q.major = q255_header_version[0];
        q.minor = q255_header_version[1];
        q.patch = q255_header_version[2];
        q.title = title;
        q.author = author;
        q.s_version = s.version;
        q.compat = s.compat;
        zq_rules_upstream(is30, rules, q);

        if(settings && s.has_settings)
            *settings = s.settings;
    }

    std::string hex;
    char buf[3];

    for(int i = 0; i < 100; ++i)
    {
        snprintf(buf, sizeof(buf), "%02X", rules[i]);
        hex += buf;
    }

    return hex;
}

std::string quest_scripts_folder()
{
    std::string q = filepath ? filepath : "";
    size_t slash = q.find_last_of("/\\");
    q = slash == std::string::npos ? std::string() : q.substr(0, slash + 1);
    return q + "scripts";
}

struct RunState
{
    ZqCompileResult *out;
    std::string console;
    long last;
};

void on_message(int code, void *user)
{
    RunState &st = *(RunState *)user;
    FILE *f = fopen(st.console.c_str(), "rb");

    if(!f)
        return;

    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    std::string text;

    if(end > st.last)
    {
        text.resize((size_t)(end - st.last));
        fseek(f, st.last, SEEK_SET);
        size_t got = fread(&text[0], 1, text.size(), f);
        text.resize(got);
        st.last += (long)got;
    }

    fclose(f);
    ZqCompileMessage m;
    m.code = code;
    m.text = text;
    std::lock_guard<std::mutex> lock(g_mutex);
    st.out->messages.push_back(m);
}

// Every map entry upstream's reload_scripts makes: ids 0 to slots - 1, global 0 "~Init".
void fill_maps(std::map<int, std::string> names[ZQS_COUNT])
{
    for(int t = 0; t < ZQS_COUNT; ++t)
        for(int i = 0; i < g_types[t].slots; ++i)
            names[t][i];

    names[ZQS_GLOBAL][0] = "~Init";
}

std::string inc_script_name(const std::string &name)
{
    size_t pos = name.find_last_not_of("0123456789");
    pos = name.find_first_of("0123456789", pos);
    char buf[32];

    if(pos == std::string::npos)
        return name + "2";

    snprintf(buf, sizeof(buf), "%d", atoi(name.substr(pos).c_str()) + 1);
    return name.substr(0, pos) + buf;
}

std::vector<byte> empty_record(bool is30)
{
    if(is30)
        return std::vector<byte>(1, 0);

    std::vector<uint8_t> r = zs255_result::empty_record();
    return std::vector<byte>(r.begin(), r.end());
}

void put16(std::vector<byte> &o, long v)
{
    o.push_back((byte)v);
    o.push_back((byte)(v >> 8));
}

void put32(std::vector<byte> &o, long v)
{
    put16(o, v);
    put16(o, v >> 16);
}

// The section as the generation's writeffscript writes it (the id first).
bool build_section(const Store &st, const std::map<int, std::string> names[ZQS_COUNT], std::vector<byte> &out, std::string &why)
{
    const bool is30 = st.is30;
    const std::vector<byte> empty = empty_record(is30);
    std::vector<byte> body;

    if(is30)
        body.insert(body.end(), st.zasm.begin(), st.zasm.end());

    for(int k = 0; k < 14; ++k)
    {
        const int t = g_table_order[k];
        const int count = t < 0 ? 256 : g_types[t].table;

        if(t == ZQS_GENERIC || t == ZQS_SUBSCREEN)
            put16(body, count);

        for(int i = 0; i < count; ++i)
        {
            const std::vector<byte> *rec = &empty;

            if(t >= 0 && i < (int)st.records[t].size() && !st.records[t][i].empty())
                rec = &st.records[t][i];

            body.insert(body.end(), rec->begin(), rec->end());
        }
    }

    put32(body, (long)zScript.size());
    body.insert(body.end(), zScript.begin(), zScript.end());

    for(int t = 0; t < ZQS_COUNT; ++t)
    {
        long count = 0;

        for(std::map<int, std::string>::const_iterator it = names[t].begin(); it != names[t].end(); ++it)
            if(!it->second.empty())
                ++count;

        put16(body, count);

        for(std::map<int, std::string>::const_iterator it = names[t].begin(); it != names[t].end(); ++it)
        {
            if(it->second.empty())
                continue;

            if(it->second.size() > 1024)
            {
                why = "the " + std::string(g_types[t].name) + " script name \"" + it->second.substr(0, 40) + "...\" is longer than 1024 characters";
                return false;
            }

            put16(body, it->first);
            put32(body, (long)it->second.size());
            body.insert(body.end(), it->second.begin(), it->second.end());
        }
    }

    if(is30)
    {
        put32(body, (long)st.debug.size());
        body.insert(body.end(), st.debug.begin(), st.debug.end());
    }

    out.clear();
    out.push_back('F');
    out.push_back('F');
    out.push_back('S');
    out.push_back('C');
    put16(out, is30 ? V_30 : V_255);
    put16(out, is30 ? V_30 : 1);        // cversion: 3.0 writes the version again, 2.55.17 CV_FFSCRIPT (1)
    put16(out, is30 ? META_30 : META_255);
    put32(out, (long)body.size());
    out.insert(out.end(), body.begin(), body.end());
    return true;
}

// Reads a built section with the player's reader. keep: into ZQuest's tables.
bool read_back(const std::vector<byte> &sec, bool keep)
{
    Q255Section s;
    s.data = &sec[14];
    s.size = sec.size() - 14;
    s.version = (word)(sec[4] | (sec[5] << 8));
    s.cversion = (word)(sec[6] | (sec[7] << 8));
    s.keep = keep;
    s.header = &header;
    return q255_read_ffscript(s) == 0 && !s.failed && s.pos == s.size;
}

} // namespace

const ZqSlotTypeInfo &zq_slot_type(int t)
{
    return g_types[t < 0 ? 0 : (t >= ZQS_COUNT ? ZQS_COUNT - 1 : t)];
}

bool zq_compile_available()
{
    return g_st.active;
}

bool zq_compile_is30()
{
    return g_st.is30;
}

int zq_compile_last_version()
{
    return g_st.last_compiled;
}

std::map<int, std::string> &zq_slot_names(int t)
{
    return g_st.names[t];
}

bool zq_compile_pending()
{
    return g_st.pending;
}

void zq_compile_lock()
{
    g_mutex.lock();
}

void zq_compile_unlock()
{
    g_mutex.unlock();
}

std::string zq_compile_compiler_path()
{
    const std::string dir = zq_program_dir();
#ifdef _WIN32
    const char *ext = ".exe";
#else
    const char *ext = "";
#endif
    return g_st.is30 ? dir + "zscript30/zscript30" + ext : dir + "zscript255" + ext;
}

void zq_compile_run(const std::string &source, ZqCompileResult &out, volatile bool *cancel)
{
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    const bool is30 = g_st.is30;
    out.is30 = is30;
    const std::string dir = zq_program_dir();
    const std::string tmp = zq_temp_dir();
    char pid[32];
    snprintf(pid, sizeof(pid), "%ld", zq_process_id());

    // As upstream: 3.0's ZQuest writes the buffer as "ZQ_BUFFER" in the folder it shares with its
    // zscript (the name the debug data then holds), 2.55.17's to a temporary file.
    const std::string work = is30 ? dir + "zscript30/" : dir + "zscript255_files/";
    const std::string buffer = is30 ? work + "ZQ_BUFFER" : tmp + "zq_buffer_" + pid + ".zs";
    const std::string console = tmp + "zq_console_" + pid + ".txt";
    const std::string assembled = tmp + "zq_assembled_" + pid + ".bin";
    const std::string tmp2 = tmp + "zq_tmp2_" + pid;
    const std::string olddebug = tmp + "zq_olddebug_" + pid + ".bin";

    FILE *f = fopen(buffer.c_str(), "wb");

    if(!f || fwrite(source.data(), 1, source.size(), f) != source.size())
    {
        if(f)
            fclose(f);
        out.code = ZQ_PROC_NOT_RUN;
        out.error = "the script buffer couldn't be written to " + buffer;
        return;
    }

    fclose(f);
    remove(assembled.c_str());
    remove(console.c_str());

    std::vector<std::pair<word, long> > settings;
    const std::string qr = rules_hex(is30, &settings);
    char cfg_key[32];
    snprintf(cfg_key, sizeof(cfg_key), "include_paths_%s", is30 ? "30" : "255");
    std::string includes = get_config_string("Compiler", cfg_key, "include/;headers/;scripts/");

    // upstream splits the paths on ';', drops empty ones and adds the quest's own scripts folder
    std::string joined;
    size_t at = 0;

    while(at <= includes.size())
    {
        size_t semi = includes.find(';', at);
        std::string one = includes.substr(at, semi == std::string::npos ? std::string::npos : semi - at);
        if(!one.empty())
            joined += one + ";";
        if(semi == std::string::npos)
            break;
        at = semi + 1;
    }

    joined += quest_scripts_folder();

    std::vector<std::string> args;

    if(!is30)
    {
        args.push_back("-chdir");
        args.push_back(work);
    }

    args.push_back("-input");
    args.push_back(is30 ? std::string("ZQ_BUFFER") : buffer);   // 3.0 names it as upstream does: the messages show the name
    args.push_back("-console");
    args.push_back(console);
    args.push_back("-qr");
    args.push_back(qr);
    args.push_back("-linked");
    args.push_back("-include");
    args.push_back(joined);

    if(is30)
    {
        for(size_t i = 0; i < settings.size(); ++i)
        {
            char v[32];
            snprintf(v, sizeof(v), "%ld", settings[i].second);
            args.push_back(settings[i].first == 0 ? "-DEFAULT_STATIC_SCRIPT_MEMBERS" : "-INVALID_SETTING");
            args.push_back(v);
        }

        // the debug data of the compile the slots hold now, so the compiler can say which
        // scripts' instance variables moved (see check_var_remaps)
        std::vector<byte> old;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            old = g_st.loaded_debug;
        }
        remove(olddebug.c_str());

        if(!old.empty())
        {
            FILE *of = fopen(olddebug.c_str(), "wb");
            const bool ok = of && fwrite(&old[0], 1, old.size(), of) == old.size();

            if(of)
                fclose(of);

            if(ok)
            {
                args.push_back("-remap-old");
                args.push_back(olddebug);
            }
        }
    }
    else
    {
        args.push_back("-out");
        args.push_back(tmp2);
    }

    args.push_back("-assembled");
    args.push_back(assembled);

    RunState st;
    st.out = &out;
    st.console = console;
    st.last = 0;
    std::string why;
    const int timeout = get_config_int("Compiler", "compiler_timeout", 30);
    const std::string exe = zq_compile_compiler_path();
    const int code = zq_run_linked_compiler(exe, args, is30 ? work : dir, timeout, cancel, on_message, &st, why);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        out.code = code;
        out.error = why;
    }

    if(code == 0)
    {
        bool ok = is30 ? read_assembled_30(assembled, out) : read_assembled_255(assembled, out);

        if(!ok)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            out.code = ZQ_PROC_DIED;
            out.error = "the compiler's results (" + assembled + ") couldn't be read";
        }
    }

    remove(assembled.c_str());
    remove(tmp2.c_str());
    remove(console.c_str());
    remove(buffer.c_str());
    remove(olddebug.c_str());
    out.ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
}

bool zq_slot_found(const ZqCompileResult &r, int t, const std::string &name)
{
    if(name.empty())
        return false;
    if(t == ZQS_GLOBAL && name == "~Init")
        return true;
    return r.scripts.count(name) != 0;
}

void zq_slots_auto(const ZqCompileResult &r, int mode, std::map<int, std::string> names[ZQS_COUNT])
{
    fill_maps(names);

    if(mode != 2)
        return;

    // smart_slot_named: global and hero slots that are free take the script named like the slot,
    // matched first exactly, then ignoring case
    for(int pass = 0; pass < 2; ++pass)
    {
        const int t = pass ? ZQS_HERO : ZQS_GLOBAL;
        const char *const *slotnames = pass ? g_hero_slotnames : g_global_slotnames;
        const int first = pass ? 0 : 1, last = pass ? 4 : 8;

        for(int q = first; q < last; ++q)
        {
            std::string &lval = names[t][q];
            if(!lval.empty())
                continue;
            bool done = false;

            for(size_t i = 0; i < r.names[t].size() && !done; ++i)
                if(r.names[t][i] == slotnames[q])
                {
                    lval = r.names[t][i];
                    done = true;
                }

            std::string lc_slot = slotnames[q];
            std::transform(lc_slot.begin(), lc_slot.end(), lc_slot.begin(), ::tolower);

            for(size_t i = 0; i < r.names[t].size() && !done; ++i)
            {
                std::string lc = r.names[t][i];
                std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
                if(lc == lc_slot)
                {
                    lval = r.names[t][i];
                    done = true;
                }
            }
        }
    }

    // smart_slot_type: every other unassigned script into the first free slot of its type
    static const int types[11] = { ZQS_FFC, ZQS_ITEM, ZQS_NPC, ZQS_LWEAPON, ZQS_EWEAPON, ZQS_SCREEN, ZQS_DMAP,
                                   ZQS_ITEMSPRITE, ZQS_COMBO, ZQS_GENERIC, ZQS_SUBSCREEN };

    for(int k = 0; k < 11; ++k)
    {
        const int t = types[k];

        for(size_t i = 0; i < r.names[t].size(); ++i)
        {
            const std::string &rval = r.names[t][i];
            int first_open = -1;
            bool done = false;

            for(int q = 0; q < g_types[t].slots; ++q)
            {
                const std::string &lval = names[t][q];

                if(lval.empty())
                {
                    if(first_open < 0)
                        first_open = q;
                }
                else if(lval == rval)
                {
                    done = true;
                    break;
                }
            }

            if(!done)
            {
                if(first_open < 0)
                    break;      // no slots left
                names[t][first_open] = rval;
            }
        }
    }
}

// 3.0: a script's exported variables have values set per placed script (on FFCs, combos, items,
// enemies, DMaps, screens, subscreens, init data), stored by the variable's index. When a compile
// moves those variables, or turns run() parameters into exported variables, upstream's ZQuest
// moves the stored values with them (src/zq/script_slot_remap.cpp, plan_slot_remaps and
// apply_slot_remaps). This ZQuest doesn't hold those values in a form it can move, so such an
// assignment is refused instead of leaving the values on the wrong variables. As upstream, only
// a slot that keeps the script it had is looked at. False (why set) when a script is affected.
// What the placed scripts of one slot hold: the exported variables that have a value set on
// some placed script, and the run arguments (InitD) set to something other than 0.
struct InstUse
{
    std::set<word> vars;
    std::set<int> args;
};

// (slot type, slot) -> InstUse, filled by the tape hook below
static std::map<std::pair<int, int>, InstUse> *g_inst_uses = NULL;
static int g_inst_section = 0;      // which section the walk is in (an InstSection)

enum InstSection { IS_ITEM, IS_GUY, IS_DMAP, IS_SCREEN, IS_INIT, IS_MISC, IS_SUBS };

static bool key_is(const char *key, const char *want)
{
    return strcmp(key, want) == 0;
}

// The slot type a script config is for, by the section and its field key (upstream's
// for_each_inst_init lists the same configs). -1: not one of them.
static int inst_type(int section, const char *key)
{
    const size_t n = strlen(key);
    const bool weapon = n >= 5 && strcmp(key + n - 5, "wd.sc") == 0;

    switch(section)
    {
    case IS_ITEM:
        return weapon ? ZQS_LWEAPON : key_is(key, "sc.sprite") ? ZQS_ITEMSPRITE : ZQS_ITEM;
    case IS_GUY:
        return weapon ? ZQS_EWEAPON : ZQS_NPC;
    case IS_DMAP:
        return ZQS_DMAP;
    case IS_SCREEN:
        return strncmp(key, "ffc", 3) == 0 ? ZQS_FFC : ZQS_SCREEN;
    case IS_INIT:
        return key_is(key, "init.global") ? ZQS_GLOBAL : key_is(key, "init.hero") ? ZQS_HERO :
               strncmp(key, "init.gen", 8) == 0 ? ZQS_GENERIC : -1;
    case IS_MISC:
        return key_is(key, "misc.save_menu") ? ZQS_GENERIC : -1;
    case IS_SUBS:
    {
        // "s3.script" is a subscreen's own script; a widget's ("s3.p0.w2.script") is generic
        int sub = 0, len = 0;
        return sscanf(key, "s%d.script%n", &sub, &len) == 1 && key[len] == 0 ? ZQS_SUBSCREEN : ZQS_GENERIC;
    }
    }

    return -1;
}

static void note_inst(int type, int script, const long *initd, const byte *inst, size_t pairs)
{
    if(type < 0 || (script <= 0 && type != ZQS_GLOBAL && type != ZQS_HERO && type != ZQS_GENERIC))
        return;

    InstUse &u = (*g_inst_uses)[std::make_pair(type, script)];

    for(size_t q = 0; inst && q < pairs; ++q)
        u.vars.insert((word)(inst[q * 6] | (inst[q * 6 + 1] << 8)));

    for(int q = 0; initd && q < 8; ++q)
        if(initd[q])
            u.args.insert(q);
}

static void inst_hook(const char *key, int script, const long *initd, const byte *inst, size_t pairs)
{
    if(g_inst_uses)
        note_inst(inst_type(g_inst_section, key), script, initd, inst, pairs);
}

typedef bool (*InstSchema)(TapeParser &, int);

static bool walk_inst(int section, InstSchema schema, const std::vector<byte> &rec, int v)
{
    Tape t;
    TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);
    g_inst_section = section;
    return schema(p, v) && !p.bad;
}

// Combo instance values, kept as bytes with their word count in front
static void note_combo_inst(int type, word script, const long *initd, const std::vector<byte> &raw)
{
    const size_t pairs = raw.size() >= 2 ? (size_t)(raw[0] | (raw[1] << 8)) : 0;
    note_inst(type, script, initd, raw.size() >= 2 + pairs * 6 ? &raw[2] : NULL, pairs);
}

// Every placed script's stored run arguments and exported-variable values, as ZQuest holds
// the quest now (its edits included): screens and their FFCs, combos and their weapon data,
// DMaps, items, enemies, subscreens and their widgets, save menus, init data. False when a
// section couldn't be walked; the caller then has to assume values are there.
static bool collect_inst_uses(std::map<std::pair<int, int>, InstUse> &uses)
{
    uses.clear();
    g_inst_uses = &uses;
    zq_tape_inst_hook = inst_hook;
    bool ok = true;
    std::vector<byte> rec;
    std::vector<std::vector<byte> > recs;
    int v = 0;

    // a section that has records but can't be laid out counts as not walked
    ok = (zq255_item_records_all(recs, &v) || !find_part(ID_ITEMS)) && ok;
    for(size_t i = 0; i < recs.size(); ++i)
        ok = walk_inst(IS_ITEM, zq_tape_item, recs[i], v) && ok;

    ok = (zq255_guy_records_all(recs, &v) || !find_part(ID_GUYS)) && ok;
    for(size_t i = 0; i < recs.size(); ++i)
        ok = walk_inst(IS_GUY, zq_tape_guy, recs[i], v) && ok;

    ok = (zq255_dmap_records_all(recs, &v) || !find_part(ID_DMAPS)) && ok;
    for(size_t i = 0; i < recs.size(); ++i)
        ok = walk_inst(IS_DMAP, zq_tape_dmap, recs[i], v) && ok;

    ok = (zq255_screen_records_all(recs, &v) || !find_part(ID_MAPS)) && ok;
    for(size_t i = 0; i < recs.size(); ++i)
        ok = walk_inst(IS_SCREEN, zq_tape_screen, recs[i], v) && ok;

    // run arguments set in 2.53's dialogs since the load (FFCs and items), which the records
    // above don't show yet
    for(size_t s = 0; s < TheMaps.size(); ++s)
        for(int k = 0; k < 32; ++k)
            if(TheMaps[s].ffscript[k])
                note_inst(ZQS_FFC, TheMaps[s].ffscript[k], TheMaps[s].initd[k], NULL, 0);

    for(int i = 0; itemsbuf && i < MAXITEMS; ++i)
        if(itemsbuf[i].script)
            note_inst(ZQS_ITEM, itemsbuf[i].script, itemsbuf[i].initiald, NULL, 0);

    if(zq255_init_record(rec, &v))
        ok = walk_inst(IS_INIT, zq_tape_init, rec, v) && ok;

    if(zq255_misc_record(rec, &v))
        ok = walk_inst(IS_MISC, zq_tape_misc, rec, v) && ok;

    if(zq255_subs_record(rec, &v))
        ok = walk_inst(IS_SUBS, zq_tape_subscreens, rec, v) && ok;

    for(int i = 0; i < MAXCOMBOS; ++i)
    {
        const Q30Combo *c = zq255.combos[i];

        if(!c)
            continue;

        note_combo_inst(ZQS_COMBO, c->script, c->initd, c->script_inst_raw);

        // a combo's misc weapon is an LWeapon or an EWeapon by its type: count it for both
        const Q30WeaponData *w[2] = { &c->lift_weap, &c->misc_weap };

        for(int k = 0; k < 2; ++k)
        {
            note_combo_inst(ZQS_LWEAPON, w[k]->script, w[k]->initd, w[k]->inst_raw);

            if(k == 1)
                note_combo_inst(ZQS_EWEAPON, w[k]->script, w[k]->initd, w[k]->inst_raw);
        }
    }

    zq_tape_inst_hook = NULL;
    g_inst_uses = NULL;

    // testing: ZQ_COMPILE_FAKE_INST="type,slot,var;..." adds stored values (var -1: InitD[0])
    if(const char *fake = getenv("ZQ_COMPILE_FAKE_INST"))
    {
        int t = 0, sl = 0, var = 0, len = 0;

        while(sscanf(fake, "%d,%d,%d%n", &t, &sl, &var, &len) == 3)
        {
            InstUse &u = uses[std::make_pair(t, sl)];

            if(var < 0)
                u.args.insert(0);
            else
                u.vars.insert((word)var);

            fake += len;

            if(*fake != ';')
                break;

            ++fake;
        }
    }

    if(getenv("ZQ_COMPILE_TRACE"))
        for(std::map<std::pair<int, int>, InstUse>::const_iterator it = uses.begin(); it != uses.end(); ++it)
            fprintf(stderr, "inst use: type %d slot %d: %d variables, %d run arguments\n", it->first.first,
                    it->first.second, (int)it->second.vars.size(), (int)it->second.args.size());

    if(getenv("ZQ_COMPILE_TRACE"))
        fprintf(stderr, "inst use: every section walked: %s\n", ok ? "yes" : "no");

    return ok;
}

static bool check_var_remaps(const ZqCompileResult &r, const std::map<int, std::string> names[ZQS_COUNT], std::string &why)
{
    // As upstream's plan_slot_remaps, only stored values decide: a moved variable that no placed
    // script has a value for, or a run() parameter no placed script sets, needs nothing moved.
    std::map<std::pair<int, int>, InstUse> uses;
    const bool all_known = collect_inst_uses(uses);

    std::string list;
    int count = 0;

    for(int t = 0; t < ZQS_COUNT; ++t)
    {
        const ZqSlotTypeInfo &ti = g_types[t];

        for(std::map<int, std::string>::const_iterator it = names[t].begin(); it != names[t].end(); ++it)
        {
            const int slot = it->first + ti.offset;

            if(slot < 0 || slot >= ti.table || it->second.empty())
                continue;

            const LoadedSlot &ls = g_st.loaded[t][slot];
            std::map<std::string, ZqVarRemap>::const_iterator rm = r.remaps.find(it->second);
            std::map<std::string, ZqCompiledScript>::const_iterator sc = r.scripts.find(it->second);

            if(!ls.code || ls.script_name != it->second || rm == r.remaps.end() || sc == r.scripts.end())
                continue;

            const ZqVarRemap &m = rm->second;
            bool moved = false;
            std::map<std::pair<int, int>, InstUse>::const_iterator ui = uses.find(std::make_pair(t, slot));
            const InstUse *use = ui == uses.end() ? NULL : &ui->second;

            // an exported variable of the old script that is gone, elsewhere, or of another kind,
            // and that some placed script has a value for
            for(size_t i = 0; i < ls.exports.size() && !moved; ++i)
            {
                const word id = ls.exports[i];

                if(all_known && (!use || !use->vars.count(id)))
                    continue;

                std::map<word, word>::const_iterator to = m.old_to_new.find(id);
                std::map<word, byte>::const_iterator ok = m.old_kind.find(id), nk = m.new_kind.find(id);
                moved = to == m.old_to_new.end() || to->second != id ||
                        (ok != m.old_kind.end() && nk != m.new_kind.end() && ok->second != nk->second);
            }

            // a run() parameter that became an exported variable no old variable maps to, and
            // that some placed script sets
            if(!moved && !m.initd_moves.empty() && (!all_known || use))
            {
                LoadedSlot now;
                Rd rd(sc->second.record.empty() ? NULL : &sc->second.record[0], sc->second.record.size());
                read_slot(rd, V_30, META_30, &now);

                for(size_t i = 0; i < m.initd_moves.size() && !moved; ++i)
                {
                    const word id = m.initd_moves[i].second;
                    bool taken = false;

                    if(all_known && !use->args.count(m.initd_moves[i].first))
                        continue;

                    for(std::map<word, word>::const_iterator o = m.old_to_new.begin(); o != m.old_to_new.end(); ++o)
                        taken = taken || o->second == id;

                    moved = !taken && std::find(now.exports.begin(), now.exports.end(), id) != now.exports.end();
                }
            }

            if(moved && ++count <= 6)
                list += std::string(list.empty() ? "" : ", ") + ti.name + " script \"" + it->second + "\"";
        }
    }

    if(!count)
        return true;

    if(count > 6)
        list += " and " + std::to_string(count - 6) + " more";

    why = "the exported variables of " + list + " changed place (or were made from run() parameters) since the "
          "quest's last compile, and placed scripts have values set for them. ZC 3.0's ZQuest then moves those "
          "values; this ZQuest can't move them yet, and they would end up on the wrong variables";
    return false;
}

bool zq_compile_assign(const ZqCompileResult &r, std::map<int, std::string> names[ZQS_COUNT], std::string &why)
{
    if(!g_st.active || r.code != 0 || r.is30 != g_st.is30)
    {
        why = "there are no compiled scripts for this quest";
        return false;
    }

    Store st = g_st;
    fill_maps(names);

    if(st.is30 && !check_var_remaps(r, names, why))
        return false;

    for(int t = 0; t < ZQS_COUNT; ++t)
    {
        const ZqSlotTypeInfo &ti = g_types[t];
        st.records[t].assign((size_t)ti.table, std::vector<byte>());

        // the table slots no name id reaches stay as they are (slot 0 of most types)
        for(int i = 0; i < ti.offset && i < ti.table; ++i)
        {
            const LoadedSlot &ls = g_st.loaded[t][i];

            if(!ls.code)
                continue;

            if(g_st.version != (st.is30 ? V_30 : V_255))
            {
                why = std::string("slot 0 of the ") + ti.name + " scripts holds a script in an older layout of the "
                      "scripts section, which this ZQuest can't carry into the new one";
                return false;
            }

            st.records[t][i] = ls.bytes;
        }

        for(std::map<int, std::string>::iterator it = names[t].begin(); it != names[t].end(); ++it)
        {
            const int slot = it->first + ti.offset;

            if(slot < 0 || slot >= ti.table || it->second.empty())
                continue;

            std::string &name = it->second;

            // 2.55.17's do_script_disassembly: a slot named like a compiled script of another
            // type is renamed (name2, name3...); a slot whose script wasn't compiled keeps an
            // imported ZASM script, and loses anything else
            if(!st.is30 && !(t == ZQS_GLOBAL && it->first == 0))
            {
                std::map<std::string, ZqCompiledScript>::const_iterator found = r.scripts.find(name);

                if(found != r.scripts.end() && found->second.type != t)
                {
                    while(r.scripts.count(name))
                        name = inc_script_name(name);
                }

                if(!r.scripts.count(name))
                {
                    const LoadedSlot &ls = g_st.loaded[t][slot];

                    if(ls.code && (ls.flags & ZMETA_IMPORTED_FLAG))
                    {
                        if(g_st.version != V_255)
                        {
                            why = std::string("the imported ZASM script in ") + ti.name + " slot " + std::to_string(it->first + 1) +
                                  " is in an older layout of the scripts section, which this ZQuest can't carry into 2.55.17's";
                            return false;
                        }

                        st.records[t][slot] = ls.bytes;
                    }

                    continue;
                }
            }

            std::map<std::string, ZqCompiledScript>::const_iterator found = r.scripts.find(name);

            if(found != r.scripts.end())
                st.records[t][slot] = found->second.record;
        }

        // Hero slots 1 and up, and the rest: what the names don't reach stays empty
    }

    st.zasm = r.zasm;
    st.debug = r.debug;

    for(int t = 0; t < ZQS_COUNT; ++t)
    {
        st.names[t].clear();

        for(std::map<int, std::string>::const_iterator it = names[t].begin(); it != names[t].end(); ++it)
            if(!it->second.empty())
                st.names[t][it->first] = it->second;
    }

    std::vector<byte> sec;

    if(!build_section(st, st.names, sec, why))
        return false;

    if(!read_back(sec, false))
    {
        why = "the compiled scripts section doesn't read back";
        return false;
    }

    // the commit: ZQuest's tables as the reader fills them from the new section
    for(std::map<int, std::pair<std::string, std::string> >::iterator it = ffcmap.begin(); it != ffcmap.end(); ++it)
        it->second.second = "";
    for(std::map<int, std::pair<std::string, std::string> >::iterator it = globalmap.begin(); it != globalmap.end(); ++it)
        it->second.second = "";
    for(std::map<int, std::pair<std::string, std::string> >::iterator it = itemmap.begin(); it != itemmap.end(); ++it)
        it->second.second = "";

    zasm30_reset();

    if(!read_back(sec, true))
    {
        why = "the compiled scripts section doesn't read back";
        return false;
    }

    // the slots as they are now: what the next compile in this session starts from
    for(int t = 0; t < ZQS_COUNT; ++t)
        st.loaded[t].clear();

    st.loaded_debug.clear();
    st.version = st.is30 ? V_30 : V_255;
    st.meta = st.is30 ? META_30 : META_255;

    if(!read_body(&sec[14], sec.size() - 14, st.version, st.meta, &st))
    {
        why = "the compiled scripts section doesn't read back";
        return false;
    }

    for(int t = 0; t < ZQS_COUNT; ++t)
        if(st.loaded[t].empty())
            st.loaded[t].assign((size_t)g_types[t].table, LoadedSlot());

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        st.pending = true;
        ++st.serial;
        g_st = st;
    }

    // MISC's "last compiled ZScript version", as upstream sets it after a compile
    const int now = st.is30 ? V_30 : V_255;

    if(g_st.last_compiled != now)
    {
        const ZqQuestPart *p = find_part(ID_MISC);
        std::vector<byte> b;

        if(p && part_bytes(*p, b) && b.size() >= 8 && (b[0] | (b[1] << 8)) >= 11)
        {
            std::map<std::string, TapeField> edits = zq255.misc_fields;
            TapeField f;
            f.key = "zscript_version";
            f.kind = TK_BLOB;
            f.width = 4;
            f.bytes.resize(4);
            f.bytes[0] = (byte)now;
            f.bytes[1] = (byte)(now >> 8);
            f.bytes[2] = (byte)(now >> 16);
            f.bytes[3] = (byte)(now >> 24);
            edits["zscript_version"] = f;
            std::string mwhy;

            if(zq255_misc_commit(edits, mwhy))
                g_st.last_compiled = now;
        }
    }

    saved = false;
    return true;
}

bool zq_compile_section(std::vector<byte> &out, std::string &why)
{
    if(!g_st.pending)
        return false;

    return build_section(g_st, g_st.names, out, why);
}

// ---- test switches ---------------------------------------------------------------------------------

// -qst-dump-part in.qst ID out.bin: a part's kept bytes (after its id), for comparing sections.
int zq_dump_part(const char *id, const char *out)
{
    if(strlen(id) != 4)
        return 2;

    const dword want = ((dword)(byte)id[0] << 24) | ((dword)(byte)id[1] << 16) | ((dword)(byte)id[2] << 8) | (dword)(byte)id[3];
    const ZqQuestPart *p = find_part(want);
    std::vector<byte> b;

    if(!p || !part_bytes(*p, b))
        return 2;

    FILE *f = fopen(out, "wb");

    if(!f)
        return 1;

    bool ok = b.empty() || fwrite(&b[0], 1, b.size(), f) == b.size();
    ok = fclose(f) == 0 && ok;
    return ok ? 0 : 1;
}

// -qst-compile in.qst out.qst quick|smart report.txt: compiles the quest's buffer, assigns the
// slots as Quick or Smart Assign does and saves the quest in its own format (for batch checks;
// the dialog does the same through the window). With the environment variable
// ZQ_COMPILE_BUFFER=<file> the buffer is first loaded from that file, as the dialog's Load does.
int zq_compile_batch(const char *out, const char *mode, const char *report)
{
    FILE *rep = fopen(report, "w");

    if(!rep)
        return 1;

    if(!g_st.active)
    {
        fprintf(rep, "not compiled here: no 2.55/3.0 scripts section\n");
        fclose(rep);
        return 2;
    }

    fprintf(rep, "generation %s, section v%d meta %d, last compiled %d, compiler %s\n", g_st.is30 ? "3.0" : "2.55",
            g_st.version, g_st.meta, g_st.last_compiled, zq_compile_compiler_path().c_str());

    if(const char *file = getenv("ZQ_COMPILE_BUFFER"))
    {
        std::vector<byte> text;

        if(!*file || !read_file(file, text))
        {
            fprintf(rep, "the buffer file %s couldn't be read\n", file);
            fclose(rep);
            return 2;
        }

        zScript.assign(text.begin(), text.end());
        saved = false;
        fprintf(rep, "buffer loaded from %s (%d bytes)\n", file, (int)text.size());
    }

    ZqCompileResult r;
    zq_compile_run(zScript, r, NULL);

    for(size_t i = 0; i < r.messages.size(); ++i)
        fprintf(rep, "[%d] %s\n", r.messages[i].code, r.messages[i].text.c_str());

    fprintf(rep, "result %d (%d ms)%s%s, %d scripts\n", r.code, r.ms, r.error.empty() ? "" : ": ", r.error.c_str(), (int)r.scripts.size());

    if(r.code != 0)
    {
        fclose(rep);
        return 3;
    }

    std::map<int, std::string> names[ZQS_COUNT];

    for(int t = 0; t < ZQS_COUNT; ++t)
        names[t] = g_st.names[t];

    zq_slots_auto(r, !strcmp(mode, "smart") ? 2 : 1, names);
    std::string why;

    if(!zq_compile_assign(r, names, why))
    {
        fprintf(rep, "assign refused: %s\n", why.c_str());
        fclose(rep);
        return 4;
    }

    for(int t = 0; t < ZQS_COUNT; ++t)
        for(std::map<int, std::string>::const_iterator it = g_st.names[t].begin(); it != g_st.names[t].end(); ++it)
            fprintf(rep, "slot %s %d %s%s\n", g_types[t].name, it->first + 1, it->second.c_str(),
                    zq_slot_found(r, t, it->second) ? "" : " (missing)");

    int ret = zq_save_own_format(out, true, why);
    fprintf(rep, "save %d%s%s\n", ret, why.empty() ? "" : ": ", why.c_str());
    fclose(rep);
    return ret ? 5 : 0;
}

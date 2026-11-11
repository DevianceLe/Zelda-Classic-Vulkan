// Scripts (FFSC) of a quest made in 2.53.1 or older, saved as 2.55.17 (section version 22) or
// 3.0 Nightly (version 26: 3.0 keeps the old layout, writeffscript_old, for scripts last
// compiled by an old compiler, which every such quest's are). See zq_writers.h.
//
// Both versions read an old quest's scripts into their own slot tables: the FFC, item, enemy,
// screen, global (4 of today's 8) and Link (3 of 5) scripts, the source and the slot names; the
// old weapon scripts are read and dropped, and every slot the old format has no place for is
// empty. 3.0 also throws away a script whose first command it doesn't know or which jumps
// outside itself; 2.55.17 keeps every script up to its terminator. Neither has metadata for old
// scripts, so each slot gets the empty one.
//
// The scripts are taken as upstream reads them: the file's own FFSC part, which keeps what
// 2.53's load changed (it moves jumps that leave the script back into it, and drops slot names
// past the 4 global slots it has). A script, the source or a table of slot names the user changed
// in ZQuest is taken from ZQuest's data, as upstream reads it from a 2.53 save.
//
// A quest's scripts can be millions of commands, which 3.0's layout makes hundreds of MB: the
// section is written straight to the file, one script at a time (zqn_out.h), and the opened
// file's part is read a window at a time.

#include "precompiled.h" //always first
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqn_out.h"

extern ffscript *ffscripts[512];
extern ffscript *itemscripts[256];
extern ffscript *guyscripts[256];
extern ffscript *wpnscripts[256];
extern ffscript *globalscripts[NUMSCRIPTGLOBAL];
extern ffscript *linkscripts[3];
extern ffscript *screenscripts[256];
extern std::string zScript;
extern std::map<int, std::pair<std::string, std::string> > ffcmap;
extern std::map<int, std::pair<std::string, std::string> > globalmap;
extern std::map<int, std::pair<std::string, std::string> > itemmap;

using zqn::Out;
using zqn::PartReader;

namespace
{

const word TERMINATOR = 0xFFFF;

struct Cmd
{
    word command;
    long arg1, arg2;
};

typedef std::vector<Cmd> Zasm;
typedef std::vector<std::pair<int, std::string> > Bindings;

// The old tables, in the order the format lists them.
enum { T_FFC, T_ITEM, T_GUY, T_WPN, T_SCREEN, T_GLOBAL, T_LINK, T_COUNT };

ffscript **const g_tables[T_COUNT] = { ffscripts, itemscripts, guyscripts, wpnscripts, screenscripts, globalscripts, linkscripts };
const int g_sizes[T_COUNT] = { 512, 256, 256, 256, 256, NUMSCRIPTGLOBAL, 3 };

// ---- ZQuest's scripts as loaded ------------------------------------------------------------

// ZQuest's script as 2.53's writer saves it: up to and with the terminator.
void zquest_script(const ffscript *code, Zasm &z)
{
    z.clear();

    for(long j = 0; code; ++j)
    {
        Cmd c;
        c.command = code[j].command;
        c.arg1 = c.command == TERMINATOR ? 0 : code[j].arg1;
        c.arg2 = c.command == TERMINATOR ? 0 : code[j].arg2;
        z.push_back(c);

        if(c.command == TERMINATOR)
            break;
    }
}

// What a script looks like, to tell whether it changed since the load without keeping a copy.
uint64_t script_hash(const ffscript *code)
{
    uint64_t h = 14695981039346656037ULL;

    for(long j = 0; code; ++j)
    {
        long v[3] = { code[j].command, code[j].command == TERMINATOR ? 0 : code[j].arg1,
                      code[j].command == TERMINATOR ? 0 : code[j].arg2 };

        for(int k = 0; k < 3; ++k)
            for(int b = 0; b < 4; ++b)
            {
                h ^= (byte)(v[k] >> (8 * b));
                h *= 1099511628211ULL;
            }

        if(code[j].command == TERMINATOR)
            break;
    }

    return h;
}

Bindings zquest_bindings(const std::map<int, std::pair<std::string, std::string> > &m)
{
    Bindings b;

    for(std::map<int, std::pair<std::string, std::string> >::const_iterator it = m.begin(); it != m.end(); ++it)
        if(it->second.second != "")
            b.push_back(std::make_pair(it->first, it->second.second));

    return b;
}

struct Snapshot
{
    std::vector<uint64_t> tables[T_COUNT];
    std::string source;
    Bindings ffc, global, item;
    bool taken;

    Snapshot() : taken(false) {}
};

Snapshot g_snap;

void take_snapshot()
{
    for(int t = 0; t < T_COUNT; ++t)
    {
        g_snap.tables[t].resize(g_sizes[t]);

        for(int i = 0; i < g_sizes[t]; ++i)
            g_snap.tables[t][i] = script_hash(g_tables[t][i]);
    }

    g_snap.source = zScript;
    g_snap.ffc = zquest_bindings(ffcmap);
    g_snap.global = zquest_bindings(globalmap);
    g_snap.item = zquest_bindings(itemmap);
    g_snap.taken = true;
}

ZqLoadHookRegistration g_hook(take_snapshot);

// ZQN_FORCE_ZQUEST=1 (for testing): take everything from ZQuest's data, as if the user had
// changed it, to check that path against upstream's conversion of the same quest.
bool force_zquest()
{
    const char *e = getenv("ZQN_FORCE_ZQUEST");
    return e && *e == '1';
}

bool slot_changed(int t, int i)
{
    return force_zquest() || !g_snap.taken || script_hash(g_tables[t][i]) != g_snap.tables[t][i];
}

bool anything_changed()
{
    for(int t = 0; t < T_COUNT; ++t)
        for(int i = 0; i < g_sizes[t]; ++i)
            if(slot_changed(t, i))
                return true;

    return zScript != g_snap.source || zquest_bindings(ffcmap) != g_snap.ffc || zquest_bindings(globalmap) != g_snap.global ||
           zquest_bindings(itemmap) != g_snap.item;
}

// ---- the file's FFSC part, as upstream's readffscript reads it (versions 1 to 6) ------------

// Reads one script (read_old_ffscript): its count (1000 before version 2), then commands up to
// the count or the terminator, whichever comes first.
bool read_script(PartReader &r, int v, Zasm *z)
{
    long num = v >= 2 ? r.l() : 1000;

    if(r.bad || num < 0 || num > 10000000)
        return false;

    if(z)
        z->clear();

    for(long j = 0; j < num && !r.bad; ++j)
    {
        Cmd c;
        c.command = (word)r.w();
        c.arg1 = c.arg2 = 0;

        if(c.command != TERMINATOR)
        {
            if(z)
            {
                c.arg1 = r.l();
                c.arg2 = r.l();
            }
            else
                r.skip(8);
        }

        if(z)
            z->push_back(c);

        if(c.command == TERMINATOR)
            break;
    }

    return !r.bad;
}

bool read_bindings(PartReader &r, Bindings &b)
{
    long count = r.w();

    for(long i = 0; i < count && !r.bad; ++i)
    {
        long id = r.w();
        long size = r.l();

        if(size < 0 || size > 1024)
            return false;

        std::vector<char> name((size_t)size + 1, 0);
        r.read(&name[0], (size_t)size);
        b.push_back(std::make_pair((int)id, std::string(&name[0])));
    }

    return !r.bad;
}

// Where each of the part's scripts starts, and what follows them.
struct Scan
{
    int version;
    std::vector<int64_t> at[T_COUNT];
    bool has_names;     // version > 2: the source and the slot names
    std::string source;
    Bindings ffc, global, item;

    Scan() : version(0), has_names(false) {}
};

bool scan_part(const ZqQuestPart &part, Scan &sc)
{
    PartReader r(&part);
    int v = (int)r.w();
    r.skip(2 + 4);          // cversion, size
    sc.version = v;
    int counts[T_COUNT] = { v < 2 ? 256 : 512, 256, 256, 256, 256, v > 4 ? 4 : 3, 3 };

    for(int t = 0; t < T_COUNT && !r.bad; ++t)
    {
        if(t > T_FFC && v <= 1)
            break;

        sc.at[t].resize(counts[t]);

        for(int i = 0; i < counts[t]; ++i)
        {
            sc.at[t][i] = r.pos;

            if(!read_script(r, v, NULL))
                return false;
        }
    }

    sc.has_names = v > 2;

    if(sc.has_names)
    {
        long size = r.l();

        if(r.bad || size < 0 || size > 1024 * 1024 * 10)
            return false;

        std::vector<char> buf((size_t)size + 1, 0);
        r.read(&buf[0], (size_t)size);
        sc.source = &buf[0];

        if(!read_bindings(r, sc.ffc) || !read_bindings(r, sc.global))
            return false;

        if(v > 3 && !read_bindings(r, sc.item))
            return false;
    }

    return !r.bad;
}

// ---- what upstream holds after the load ----------------------------------------------------

// Where a slot's script comes from: nowhere (the slot was never read), the file, or ZQuest.
struct Source
{
    enum { S_NONE, S_FILE, S_ZQUEST } kind;
    int64_t at;
    const ffscript *code;

    Source() : kind(S_NONE), at(0), code(NULL) {}
};

struct Held
{
    const ZqQuestPart *part;
    int version;
    std::vector<Source> ffc, item, guy, screen, global, hero;
    std::string source;
    std::map<int, std::string> ffcmap, globalmap, itemmap;
    bool continue_disabled;

    Held() : part(NULL), version(0), continue_disabled(false) {}
};

void apply_bindings(const Bindings &b, std::map<int, std::string> &m, int limit, bool global, bool &continue_disabled)
{
    for(size_t i = 0; i < b.size(); ++i)
    {
        int id = b[i].first;

        if(id >= limit)
            continue;

        if(global && b[i].second == "~Continue")
        {
            m[id] = "";
            continue_disabled = true;
        }
        else
            m[id] = b[i].second;
    }
}

std::vector<Source> &held_table(Held &h, int t)
{
    static std::vector<Source> dropped;

    switch(t)
    {
    case T_FFC: return h.ffc;
    case T_ITEM: return h.item;
    case T_GUY: return h.guy;
    case T_SCREEN: return h.screen;
    case T_GLOBAL: return h.global;
    case T_LINK: return h.hero;
    }

    dropped.clear();
    return dropped;         // the old weapon scripts
}

// Upstream's tables for the quest: from the file, with ZQuest's data where the user changed it.
bool held_tables(const ZqNewerContext &ctx, Held &held)
{
    Scan sc;
    bool from_file = false;
    Bindings ffc, global, item;
    bool has_names = false, item_names = false;

    if(ctx.src_part)
    {
        if(!scan_part(*ctx.src_part, sc))
        {
            zqw_last_error = "the scripts as loaded don't read the way upstream reads them";
            return false;
        }

        from_file = true;
        held.part = ctx.src_part;
        held.version = sc.version;

        for(int t = 0; t < T_COUNT; ++t)
        {
            std::vector<Source> &dest = held_table(held, t);
            dest.resize(sc.at[t].size());

            for(size_t i = 0; i < sc.at[t].size(); ++i)
            {
                if((int)i < g_sizes[t] && slot_changed(t, (int)i))
                {
                    dest[i].kind = Source::S_ZQUEST;
                    dest[i].code = g_tables[t][i];
                }
                else
                {
                    dest[i].kind = Source::S_FILE;
                    dest[i].at = sc.at[t][i];
                }
            }
        }

        has_names = sc.has_names;
        item_names = sc.version > 3;
        held.source = sc.source;
        ffc = sc.ffc;
        global = sc.global;
        item = sc.item;
        bool force = force_zquest();

        if(g_snap.taken && (force || zScript != g_snap.source))
        {
            held.source = zScript;
            has_names = true;
        }

        if(g_snap.taken && (force || zquest_bindings(ffcmap) != g_snap.ffc))
            ffc = zquest_bindings(ffcmap), has_names = true;
        if(g_snap.taken && (force || zquest_bindings(globalmap) != g_snap.global))
            global = zquest_bindings(globalmap), has_names = true;
        if(g_snap.taken && (force || zquest_bindings(itemmap) != g_snap.item))
            item = zquest_bindings(itemmap), has_names = true, item_names = true;
    }
    else if(anything_changed())
    {
        // scripts given to a quest that had none: as upstream reads a 2.53 save of them
        held.version = 6;
        int counts[T_COUNT] = { 512, 256, 256, 256, 256, 4, 3 };

        for(int t = 0; t < T_COUNT; ++t)
        {
            std::vector<Source> &dest = held_table(held, t);
            dest.resize(counts[t]);

            for(int i = 0; i < counts[t]; ++i)
            {
                dest[i].kind = Source::S_ZQUEST;
                dest[i].code = g_tables[t][i];
            }
        }

        has_names = item_names = true;
        held.source = zScript;
        ffc = zquest_bindings(ffcmap);
        global = zquest_bindings(globalmap);
        item = zquest_bindings(itemmap);
    }

    if(!has_names)
        held.source.clear();

    held.globalmap[0] = "~Init";
    bool unused = false;

    if(has_names)
    {
        apply_bindings(ffc, held.ffcmap, 512 - 1, false, unused);
        apply_bindings(global, held.globalmap, 8, true, held.continue_disabled);

        if(item_names || !from_file)
            apply_bindings(item, held.itemmap, 256 - 1, false, unused);
    }

    return true;
}

// ---- writing --------------------------------------------------------------------------------

const word GOTO = 11, GOTOTRUE = 16, GOTOFALSE = 17, GOTOLESS = 18, GOTOMORE = 19, LOOP = 65;
const word CALLFUNC = 1105, GOTOCMP = 1108, NUMCOMMANDS_30 = 1190;

// read_old_ffscript's checks in 3.0: a script whose first command it doesn't know, or with a
// jump outside the script, is emptied.
void check_30(Zasm &z)
{
    if(!z.empty() && z[0].command >= NUMCOMMANDS_30 && z[0].command != TERMINATOR)
    {
        z.clear();
        return;
    }

    for(size_t i = 0; i < z.size(); ++i)
    {
        switch(z[i].command)
        {
        case GOTO: case GOTOTRUE: case GOTOFALSE: case GOTOLESS: case GOTOMORE: case GOTOCMP: case CALLFUNC: case LOOP:
            if(z[i].arg1 < 0 || z[i].arg1 >= (long)z.size())
            {
                z.clear();
                return;
            }

            break;
        }
    }
}

// The empty script metadata of each version's layout.
void empty_meta(Out &o, int target)
{
    for(int q = 0; q < 3; ++q)
        o.w(0);             // ZASM, metadata and ZScript versions

    o.c(0);                 // script type: none

    for(int q = 0; q < 8; ++q)
        o.c(0);             // run identifiers

    for(int q = 0; q < 8; ++q)
        o.c(1);             // run types: ZMETA_NULL_TYPE

    o.c(0);                 // flags

    for(int q = 0; q < 4; ++q)
        o.w(0);             // compiler version

    o.c(0);                 // script name
    o.c(0);                 // author

    if(target == ZQ_NEWER_30)
    {
        o.w(0);             // script info
        o.w(0);             // script setup
        o.w(24);            // attributes

        for(int q = 0; q < 24; ++q)
        {
            o.c(0);
            o.w(0);
        }
    }
    else
    {
        for(int q = 0; q < 10 + 8 + 8; ++q)     // attributes, attribytes, attrishorts
        {
            o.c(0);
            o.w(0);
        }
    }

    for(int q = 0; q < 16 + 8; ++q)             // user flags, InitD labels
    {
        o.c(0);
        o.w(0);
    }

    for(int q = 0; q < 8; ++q)
        o.c(0xFF);          // InitD types: -1
}

struct Writer
{
    const Held &h;
    int target;
    PartReader reader;
    Zasm z;

    Writer(const Held &held, int t) : h(held), target(t), reader(held.part) {}

    bool slot(const Source *src, bool disabled)
    {
        bool has = src && src->kind != Source::S_NONE && !disabled;
        z.clear();

        if(has && src->kind == Source::S_FILE)
        {
            reader.pos = src->at;

            if(!read_script(reader, h.version, &z))
                return false;
        }
        else if(has)
            zquest_script(src->code, z);

        if(target == ZQ_NEWER_30)
        {
            if(has)
                check_30(z);
        }
        else
        {
            // 2.55.17 keeps a script up to its first terminator; an empty slot is just that
            size_t end = 0;

            while(end < z.size() && z[end].command != TERMINATOR)
                ++end;

            z.resize(end);
            Cmd t;
            t.command = TERMINATOR;
            t.arg1 = t.arg2 = 0;
            z.push_back(t);
        }

        o->l((long)z.size());
        empty_meta(*o, target);

        for(size_t j = 0; j < z.size(); ++j)
        {
            o->w(z[j].command);

            if(z[j].command == TERMINATOR)
                break;

            o->l(z[j].arg1);
            o->l(z[j].arg2);

            if(target == ZQ_NEWER_30)
                o->l(0);    // third argument

            o->l(0);        // no string
            o->l(0);        // no array
        }

        return true;
    }

    bool slots(const std::vector<Source> *table, int count, int disabled_index = -1)
    {
        for(int i = 0; i < count; ++i)
        {
            const Source *s = (table && i < (int)table->size()) ? &(*table)[i] : NULL;

            if(!slot(s, i == disabled_index))
                return false;
        }

        return true;
    }

    void bindings(const std::map<int, std::string> &m)
    {
        long count = 0;

        for(std::map<int, std::string>::const_iterator it = m.begin(); it != m.end(); ++it)
            if(!it->second.empty())
                ++count;

        o->w(count);

        for(std::map<int, std::string>::const_iterator it = m.begin(); it != m.end(); ++it)
            if(!it->second.empty())
            {
                o->w(it->first);
                o->l((long)it->second.size());
                o->put(it->second.data(), it->second.size());
            }
    }

    Out *o;

    bool operator()(Out &out)
    {
        o = &out;
        bool ok = slots(&h.ffc, 512) && slots(&h.item, 256) && slots(&h.guy, 256) &&
                  slots(NULL, 256) &&                   // the old weapon scripts are dropped
                  slots(&h.screen, 256) && slots(&h.global, 8, h.continue_disabled ? 3 : -1) && slots(&h.hero, 5) &&
                  slots(NULL, 256) && slots(NULL, 256) && slots(NULL, 256) &&     // lweapon, eweapon, DMap
                  slots(NULL, 256) && slots(NULL, 512);                           // item sprite, combo

        if(!ok)
        {
            zqw_last_error = "the scripts as loaded couldn't be read again";
            return false;
        }

        o->w(512);
        slots(NULL, 512);                   // generic
        o->w(256);
        slots(NULL, 256);                   // subscreen
        o->l((long)h.source.size());
        o->put(h.source.data(), h.source.size());
        bindings(h.ffcmap);
        bindings(h.globalmap);
        bindings(h.itemmap);
        std::map<int, std::string> none;

        for(int q = 0; q < 10; ++q)         // npc, lweapon, eweapon, hero, DMap, screen, item sprite, combo, generic, subscreen
            bindings(none);

        return true;
    }
};

int zqn_scripts(PACKFILE *f, const ZqNewerContext &ctx)
{
    zqn::wg_edit_test_once();

    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    if(ctx.src_part && ctx.src_version > 6)
    {
        zqw_last_error = "the quest's scripts are in a section version this writer doesn't read";
        return 1;
    }

    Held h;

    if(!held_tables(ctx, h))
        return 1;

    Writer w(h, ctx.target);
    return zqn::write_section(f, ID_FFSCRIPT, ctx.target == ZQ_NEWER_30 ? 26 : 22, 1,
                              ctx.target == ZQ_NEWER_30 ? 7 : 5, w);    // the ZASM metadata version
}

ZqNewerRegistration g_reg(ID_FFSCRIPT, zqn_scripts, "scripts for 2.55.17 and 3.0");

} // namespace

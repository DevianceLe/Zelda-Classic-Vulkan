// Section writer for scripts (FFSC): the compiled ZASM of every script slot, the ZScript
// source and the slot names, for quests made by 2.11 and 2.50.x (section versions 5 and 6)
// and by 2.55 and 3.0 Nightly (versions 18 to 31). See zqw_init.h for how the writers keep
// what nobody changed.
//
// Each script slot, the source and each table of names is a record: one nobody changed is
// copied as it was. ZQuest can compile scripts only for 2.50 and 2.53 quests, where the
// format is 2.53's own, so there a recompiled script goes out as ZQuest has it. A 2.55 or
// 3.0 quest's scripts only change if ZQuest's data does; then:
//   v18-26: a changed slot is written from ZQuest's commands, with the script's meta and
//     each command's third argument and literal taken from the original where it has them;
//   v27 and up (3.0): every script is a stub into one shared command stream, which is
//     written from ZQuest's copy of it; a slot can only keep its stub, or lose its script.
//   Slots 2.53 has no place for (global 4-7, hero, weapon, DMap, combo, generic and
//     subscreen scripts), their name tables, and the debugger's tables are copied.
// What these can't hold, and the save refuses: 2.53 code in a 3.0 quest's slots, a 3.0
// script given back to a slot that had none, and Link scripts in a 2.55 or 3.0 quest (the
// 2.55 reader doesn't give them to 2.53's Link slots).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqw_init.h"

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

// The 2.55 reader's store of what 2.53's ffscript has no room for (q255_store_zq.cpp).
struct Zasm30Literal
{
    std::string str;
    std::vector<long> vec;
};

extern ffscript *zasm30_stream;
extern std::map<const ffscript *, Zasm30Literal> zasm30_literals;

using namespace zqwe;

namespace
{

const word TERMINATOR = 0xFFFF;
const word ZASM30_ENTER = 0xFFFE;
const word STACKWRITEATVV_IF = 1115;    // the one command whose third argument the reader keeps

struct Cmd
{
    word command;
    long arg1, arg2;
};

typedef std::vector<Cmd> Script;

// A script up to and with its terminator.
void copy_script(const ffscript *code, Script &out)
{
    out.clear();

    if(!code)
        return;

    for(long j = 0; j < 30000000; ++j)
    {
        Cmd c;
        c.command = code[j].command;
        c.arg1 = code[j].arg1;
        c.arg2 = code[j].arg2;
        out.push_back(c);

        if(c.command == TERMINATOR)
            break;
    }
}

bool script_same(const ffscript *code, const Script &snap)
{
    if(!code)
        return snap.empty();

    for(size_t j = 0; j < snap.size(); ++j)
    {
        if(code[j].command != snap[j].command)
            return false;
        if(code[j].command == TERMINATOR)
            return true;
        if(code[j].arg1 != snap[j].arg1 || code[j].arg2 != snap[j].arg2)
            return false;
    }

    return snap.empty() ? code[0].command == TERMINATOR : false;
}

long script_length(const ffscript *code)
{
    long n = 0;

    while(code && code[n].command != TERMINATOR)
        ++n;

    return n;
}

typedef std::map<int, std::string> Names;

void copy_names(const std::map<int, std::pair<std::string, std::string> > &m, Names &out)
{
    out.clear();

    for(std::map<int, std::pair<std::string, std::string> >::const_iterator it = m.begin(); it != m.end(); ++it)
        if(it->second.second != "")
            out[it->first] = it->second.second;
}

struct ScriptSnap
{
    std::vector<Script> ffc, item, guy, wpn, screen, global, link;
    std::string source;
    Names ffc_names, global_names, item_names;
    Script stream;
    long stream_count;
};

ScriptSnap g_snap;

ffscript **const g_tables[7] = { ffscripts, itemscripts, guyscripts, wpnscripts, screenscripts, globalscripts, linkscripts };
const int g_table_size[7] = { 512, 256, 256, 256, 256, NUMSCRIPTGLOBAL, 3 };

std::vector<Script> &snap_table(int t)
{
    switch(t)
    {
    case 0: return g_snap.ffc;
    case 1: return g_snap.item;
    case 2: return g_snap.guy;
    case 3: return g_snap.wpn;
    case 4: return g_snap.screen;
    case 5: return g_snap.global;
    }

    return g_snap.link;
}

void take_snapshot()
{
    for(int t = 0; t < 7; ++t)
    {
        std::vector<Script> &v = snap_table(t);
        v.resize(g_table_size[t]);

        for(int i = 0; i < g_table_size[t]; ++i)
            copy_script(g_tables[t][i], v[i]);
    }

    g_snap.source = zScript;
    copy_names(ffcmap, g_snap.ffc_names);
    copy_names(globalmap, g_snap.global_names);
    copy_names(itemmap, g_snap.item_names);

    // 3.0's shared stream: its length is the reader's count, past which it put a terminator
    g_snap.stream.clear();
    g_snap.stream_count = 0;
    const ZqQuestPart *part = NULL;

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == ID_FFSCRIPT)
            part = &zq_quest_file.parts[i];

    Src s;
    Head h;

    if(zasm30_stream && part && zq_quest_file.zelda_version >= 0x255 && load_part(*part, s) && read_head(s, 2, h) && h.version >= 27)
    {
        long n = s.l();

        for(long j = 0; j < n; ++j)
        {
            Cmd c;
            c.command = zasm30_stream[j].command;
            c.arg1 = zasm30_stream[j].arg1;
            c.arg2 = zasm30_stream[j].arg2;
            g_snap.stream.push_back(c);
        }

        g_snap.stream_count = n;
    }

    // ZQW_E_MUTATE (zqw_init.h): a command's argument, the source and a slot name
    static bool mutated = false;

    if(mutate_now(mutated))
    {
        for(int i = 0; i < 512; ++i)
            if(ffscripts[i] && ffscripts[i][0].command != TERMINATOR && ffscripts[i][0].command != ZASM30_ENTER)
            {
                ffscripts[i][0].arg2 ^= 1;
                break;
            }

        zScript += "\n// changed";

        for(std::map<int, std::pair<std::string, std::string> >::iterator it = ffcmap.begin(); it != ffcmap.end(); ++it)
            if(it->second.second != "")
            {
                it->second.second += "x";
                break;
            }
    }
}

ZqLoadHookRegistration g_hook(take_snapshot);

bool names_same(const std::map<int, std::pair<std::string, std::string> > &m, const Names &snap)
{
    Names now;
    copy_names(m, now);
    return now == snap;
}

void encode_names(Buf &o, const std::map<int, std::pair<std::string, std::string> > &m)
{
    Names now;
    copy_names(m, now);
    o.w((long)now.size());

    for(Names::const_iterator it = now.begin(); it != now.end(); ++it)
    {
        o.w(it->first);
        o.l((long)it->second.size());
        o.put(it->second.data(), it->second.size());
    }
}

// A table of slot names: a count, then the slot, the name's length and the name.
bool walk_names(Src &s, bool limit)
{
    long count = s.w();

    for(long i = 0; i < count && !s.bad; ++i)
    {
        s.skip(2);
        long len = s.l();

        if(len < 0 || (limit && len > 1024))
            s.bad = true;
        else
            s.skip((size_t)len);
    }

    return !s.bad;
}

void names_record(Patch &p, const std::map<int, std::pair<std::string, std::string> > &m, const Names &snap, bool limit)
{
    Src &s = p.s;
    size_t at = s.pos;
    walk_names(s, limit);
    size_t end = s.pos;
    s.pos = at;

    if(p.keep(names_same(m, snap)))
        p.copy(end - at);
    else
    {
        encode_names(p.o, m);
        s.pos = end;
    }
}

// The source, as a length and the text.
void source_record(Patch &p)
{
    Src &s = p.s;
    size_t at = s.pos;
    long n = s.l();

    if(n < 0)
    {
        s.bad = true;
        return;
    }

    s.skip((size_t)n);
    size_t end = s.pos;
    s.pos = at;

    if(p.keep(zScript == g_snap.source))
        p.copy(end - at);
    else
    {
        p.o.l((long)zScript.size());
        p.o.put(zScript.data(), zScript.size());
        s.pos = end;
    }
}

// ---- section versions 5 and 6 (readffscript): 2.53's own layout -------------------------

void old_script(Patch &p, ffscript *code, const Script &snap)
{
    Src &s = p.s;
    size_t at = s.pos;
    long n = s.l();

    for(long j = 0; j < n && !s.bad; ++j)
    {
        if(s.w() == TERMINATOR)
            break;
        s.skip(8);
    }

    size_t end = s.pos;
    s.pos = at;

    if(p.keep(script_same(code, snap)))
    {
        p.copy(end - at);
        return;
    }

    long len = script_length(code);
    p.o.l(len + 1);

    for(long j = 0; j < len; ++j)
    {
        p.o.w(code[j].command);
        p.o.l(code[j].arg1);
        p.o.l(code[j].arg2);
    }

    p.o.w(TERMINATOR);
    s.pos = end;
}

void ffsc_old(Patch &p, int v)
{
    Src &s = p.s;

    // v5 and v6 read the same; the version also tells the engine which ZScript it was
    for(int t = 0; t < 7 && !s.bad; ++t)
        for(int i = 0; i < g_table_size[t] && !s.bad; ++i)
            old_script(p, g_tables[t][i], snap_table(t)[i]);

    if(v > 2)
    {
        source_record(p);
        names_record(p, ffcmap, g_snap.ffc_names, false);
        names_record(p, globalmap, g_snap.global_names, false);

        if(v > 3)
            names_record(p, itemmap, g_snap.item_names, false);
    }
}

// ---- 2.55 and 3.0 (q255_read_ffscript) ---------------------------------------------------

// The 2.55 reader's limits.
const long MAX_COMMANDS = 10000000;
const long MAX_STREAM_COMMANDS = 25000000;
const word MAX_META_ATTRIBUTES = 24;

void skip_cstr(Src &s) { long n = s.c(); s.skip((size_t)n); }
void skip_wstr(Src &s) { long n = s.w(); s.skip((size_t)n); }
void skip_lstr(Src &s) { long n = s.l(); if(n < 0) s.bad = true; else s.skip((size_t)n); }

void skip_label_help(Src &s, int count)
{
    for(int i = 0; i < count && !s.bad; ++i)
    {
        skip_cstr(s);
        skip_wstr(s);
    }
}

void skip_meta(Src &s, int meta)
{
    s.skip(7);

    for(int i = 0; i < 8 && !s.bad; ++i)
    {
        if(meta < 3)
            s.skip(33);
        else
            skip_cstr(s);
    }

    s.skip(8 + 1 + 8);

    if(meta == 2)
        s.skip(66);
    else if(meta > 2)
    {
        skip_cstr(s);
        skip_cstr(s);

        if(meta >= 7)
        {
            skip_wstr(s);
            skip_wstr(s);
        }

        if(meta < 6)
        {
            skip_label_help(s, meta < 5 ? 4 : 10);
            skip_label_help(s, 8);
            skip_label_help(s, 8);
        }
        else
        {
            long count = s.w();
            if(count > MAX_META_ATTRIBUTES)
                s.bad = true;
            skip_label_help(s, (int)count);
        }

        skip_label_help(s, 16);
    }

    if(meta > 3)
    {
        skip_label_help(s, 8);
        s.skip(8);
    }
}

// A literal: text with a long length, then longs with a long count.
void skip_literal(Src &s)
{
    long sz = s.l();
    if(sz < 0)
        s.bad = true;
    else
        s.skip((size_t)sz);
    long count = s.l();
    if(count < 0 || (size_t)count > s.left() / 4)
        s.bad = true;
    else
        s.skip((size_t)count * 4);
}

// What the reader made of a literal, to compare with what ZQuest keeps.
void read_literal(Src &s, Zasm30Literal &lit)
{
    long sz = s.l();
    if(sz < 0 || !s.has((size_t)sz))
    {
        s.bad = true;
        return;
    }
    lit.str.assign((const char *)s.at(), (size_t)sz);
    s.skip((size_t)sz);
    long count = s.l();
    if(count < 0 || (size_t)count > s.left() / 4)
    {
        s.bad = true;
        return;
    }
    for(long i = 0; i < count; ++i)
        lit.vec.push_back(s.l());
}

void skip_instance_tables(Src &s, int v)
{
    long size = s.w();

    if(size)
    {
        long pairs = s.c();
        long count = s.w();
        if((size_t)count > s.left() / (pairs ? 6 : 4))
            s.bad = true;
        else
            s.skip((size_t)count * (pairs ? 6 : 4));
    }

    size = s.w();

    if(size)
    {
        long pairs = s.c();
        long count = s.w();

        for(long i = 0; i < count && !s.bad; ++i)
        {
            if(pairs)
                s.skip(2);
            skip_cstr(s);
            skip_lstr(s);
            s.skip(1);

            if(v >= 31)
            {
                s.skip(3);
                long names = s.w();
                for(long n = 0; n < names && !s.bad; ++n)
                {
                    s.skip(4);
                    skip_lstr(s);
                }
            }

            s.skip(8);
        }
    }
}

struct Ffsc255
{
    Src &s;
    Patch *p;       // NULL: only walking, to find the meta layout
    int v, meta;

    Ffsc255(Src &src, Patch *patch, int version, int m) : s(src), p(patch), v(version), meta(m) {}

    void copy_from(size_t at)
    {
        size_t end = s.pos;
        s.pos = at;
        p->copy(end - at);
    }

    // The literal of the command at index j of the original, as raw bytes.
    struct OrigCmd
    {
        size_t at, a3_at, lit_at, end;
        word command;
    };

    // One script of the v18-26 layout. code: ZQuest's slot, NULL where 2.53 has none.
    void script(ffscript *code, const Script *snap)
    {
        size_t at = s.pos;
        long n = s.l();

        if(n < 0 || n > MAX_COMMANDS)
        {
            s.bad = true;
            return;
        }

        if(v >= 16)
            skip_meta(s, meta);

        size_t cmds_at = s.pos;
        std::vector<OrigCmd> orig;

        for(long j = 0; j < n && !s.bad; ++j)
        {
            OrigCmd c;
            c.at = s.pos;
            c.command = (word)s.w();

            if(c.command == TERMINATOR)
                break;

            s.skip(8);
            c.a3_at = s.pos;
            if(v >= 24)
                s.skip(4);
            c.lit_at = s.pos;
            if(v >= 21)
                skip_literal(s);
            c.end = s.pos;
            orig.push_back(c);
        }

        if(!p || s.bad)
            return;

        if(!code || p->keep(script_same(code, *snap)))
        {
            copy_from(at);
            return;
        }

        size_t end = s.pos;
        long len = script_length(code);
        Buf &o = p->o;
        o.l(len + 1);
        o.put(&s.d[at + 4], cmds_at - at - 4);     // the script's meta

        for(long j = 0; j < len; ++j)
        {
            const ffscript &c = code[j];
            const OrigCmd *oc = j < (long)orig.size() ? &orig[j] : NULL;
            std::map<const ffscript *, Zasm30Literal>::const_iterator lit = zasm30_literals.find(&c);
            o.w(c.command);
            o.l(c.arg1);
            o.l(c.arg2);

            if(v >= 24)
            {
                // the reader keeps the third argument only for this one command
                if(c.command == STACKWRITEATVV_IF && lit != zasm30_literals.end() && lit->second.vec.size() == 1)
                    o.l(lit->second.vec[0]);
                else if(oc)
                    o.put(&s.d[oc->a3_at], 4);
                else
                    o.l(0);
            }

            if(v >= 21)
                literal(o, c, lit, oc);
        }

        o.w(TERMINATOR);
        s.pos = end;
    }

    // A command's literal back from ZQuest's, or the original's where it's the same.
    void literal(Buf &o, const ffscript &c, std::map<const ffscript *, Zasm30Literal>::const_iterator lit, const OrigCmd *oc)
    {
        Zasm30Literal had;

        if(oc)
        {
            Src t;
            t.d.assign(s.d.begin() + oc->lit_at, s.d.begin() + oc->end);
            read_literal(t, had);
        }

        Zasm30Literal now;

        if(lit != zasm30_literals.end())
            now = lit->second;

        if(c.command == STACKWRITEATVV_IF)
            now.vec = had.vec;      // that entry holds the third argument instead

        if(oc && now.str == had.str && now.vec == had.vec)
        {
            o.put(&s.d[oc->lit_at], oc->end - oc->lit_at);
            return;
        }

        o.l((long)now.str.size());
        o.put(now.str.data(), now.str.size());
        o.l((long)now.vec.size());

        for(size_t i = 0; i < now.vec.size(); ++i)
            o.l(now.vec[i]);
    }

    // v27 and up: the shared stream of every script's commands.
    void stream()
    {
        size_t at = s.pos;
        long n = s.l();

        if(n < 0 || n > MAX_STREAM_COMMANDS)
        {
            s.bad = true;
            return;
        }

        std::vector<OrigCmd> orig;

        if(p)
            orig.reserve((size_t)n);

        for(long j = 0; j < n && !s.bad; ++j)
        {
            OrigCmd c;
            c.at = s.pos;
            c.command = (word)s.w();
            s.skip(8);
            c.a3_at = s.pos;
            s.skip(4);
            c.lit_at = s.pos;
            skip_literal(s);
            c.end = s.pos;

            if(p)
                orig.push_back(c);
        }

        if(!p || s.bad)
            return;

        bool same = zasm30_stream && n == g_snap.stream_count;

        for(long j = 0; j < n && same; ++j)
            same = zasm30_stream[j].command == g_snap.stream[j].command && zasm30_stream[j].arg1 == g_snap.stream[j].arg1 &&
                   zasm30_stream[j].arg2 == g_snap.stream[j].arg2;

        if(p->keep(same))
        {
            copy_from(at);
            return;
        }

        if(!zasm30_stream || n != g_snap.stream_count)
        {
            p->fail("a changed 3.0 command stream");
            copy_from(at);
            return;
        }

        size_t end = s.pos;
        Buf &o = p->o;
        o.l(n);

        for(long j = 0; j < n; ++j)
        {
            const ffscript &c = zasm30_stream[j];
            std::map<const ffscript *, Zasm30Literal>::const_iterator lit = zasm30_literals.find(&c);
            o.w(c.command);
            o.l(c.arg1);
            o.l(c.arg2);

            if(c.command == STACKWRITEATVV_IF && lit != zasm30_literals.end() && lit->second.vec.size() == 1)
                o.l(lit->second.vec[0]);
            else
                o.put(&s.d[orig[j].a3_at], 4);

            literal(o, c, lit, &orig[j]);
        }

        s.pos = end;
    }

    // One slot of the v27+ layout: whether it's used, then where its code is in the stream.
    void stream_script(ffscript *code, const Script *snap)
    {
        size_t at = s.pos;
        long exists = s.c();
        long start = -1;
        size_t start_at = 0;

        if(exists)
        {
            skip_meta(s, meta);
            start_at = s.pos;
            start = s.l();
            s.skip(4);

            if(v >= 30)
                skip_instance_tables(s, v);
        }

        if(!p || s.bad)
            return;

        if(!code || p->keep(script_same(code, *snap)))
        {
            copy_from(at);
            return;
        }

        size_t end = s.pos;
        bool stub = code[0].command == ZASM30_ENTER && code[1].command == TERMINATOR;
        bool empty = code[0].command == TERMINATOR;
        bool had_code = exists && start >= 0;

        if(!stub && !empty)
            p->fail("2.53 scripts in a 3.0 quest's slots");
        else if(stub && (!had_code || code[0].arg1 != start || code[0].arg2 != (*snap)[0].arg2))
            p->fail("a 3.0 script moved to another slot");
        else if(empty && had_code)
        {
            p->o.c(0);      // the slot loses its script
            s.pos = end;
            return;
        }

        if(stub)
        {
            // the slot as it was, its start from ZQuest's stub
            s.pos = at;
            p->copy(start_at - at);
            p->o.l(code[0].arg1);
            s.pos = start_at + 4;
            p->copy(end - s.pos);
        }
        else
            copy_from(at);

        s.pos = end;
    }

    void one(ffscript *code, const Script *snap)
    {
        if(v >= 27)
            stream_script(code, snap);
        else
            script(code, snap);
    }

    // count scripts, the first 'limit' of them in 2.53's table t (-1: none)
    void scripts(int count, int t, int limit)
    {
        for(int i = 0; i < count && !s.bad; ++i)
        {
            bool home = t >= 0 && i < limit;
            one(home ? g_tables[t][i] : NULL, home ? &snap_table(t)[i] : NULL);
        }
    }

    void names(const std::map<int, std::pair<std::string, std::string> > *m, const Names *snap)
    {
        if(!p || !m)
        {
            size_t at = s.pos;
            walk_names(s, true);

            if(p)
                copy_from(at);
            return;
        }

        names_record(*p, *m, *snap, true);
    }

    void run()
    {
        if(v >= 27)
            stream();

        scripts(512, 0, NUMSCRIPTFFC);
        scripts(256, 1, NUMSCRIPTITEM);
        scripts(256, 2, NUMSCRIPTGUYS);
        scripts(256, 3, NUMSCRIPTWEAPONS);
        scripts(256, 4, NUMSCRIPTSCREEN);
        scripts(v > 16 ? 8 : (v > 13 ? 7 : 4), 5, NUMSCRIPTGLOBAL);
        scripts(v > 10 ? 5 : 3, -1, 0);         // hero scripts: kept apart from 2.53's Link slots

        if(v == 9)
        {
            scripts(256, -1, 0);
            scripts(256, -1, 0);
        }

        if(v >= 10)
        {
            scripts(256, -1, 0);
            scripts(256, -1, 0);
            scripts(256, -1, 0);
        }

        if(v >= 12)
            scripts(256, -1, 0);
        if(v >= 15)
            scripts(512, -1, 0);

        if(v > 19)
        {
            size_t at = s.pos;
            long count = s.w();
            if(p)
            {
                s.pos = at;
                p->copy(2);
            }
            scripts((int)count, -1, 0);
        }

        if(v > 21)
        {
            size_t at = s.pos;
            long count = s.w();
            if(p)
            {
                s.pos = at;
                p->copy(2);
            }
            scripts((int)count, -1, 0);
        }

        if(p)
            source_record(*p);
        else
        {
            long n = s.l();
            if(n < 0)
                s.bad = true;
            else
                s.skip((size_t)n);
        }

        names(&ffcmap, &g_snap.ffc_names);
        names(&globalmap, &g_snap.global_names);
        names(&itemmap, &g_snap.item_names);

        if(v > 8)
            for(int i = 0; i < 6; ++i)
                names(NULL, NULL);

        if(v > 11)
            names(NULL, NULL);
        if(v >= 15)
            names(NULL, NULL);
        if(v > 19)
            names(NULL, NULL);
        if(v > 21)
            names(NULL, NULL);

        if(v >= 29)
        {
            size_t at = s.pos;
            long n = s.l();
            s.skip((size_t)(dword)n);
            if(p)
                copy_from(at);
        }
    }
};

// From v18 the meta layout is the one that fits the section exactly, as the reader finds it.
int find_meta(const Src &orig, size_t body, int v)
{
    if(v < 18)
        return 0;

    for(int meta = 0; meta <= 7; ++meta)
    {
        Src s;
        s.d = orig.d;
        s.pos = body;
        Ffsc255 w(s, NULL, v, meta);
        w.run();

        if(!s.bad && s.pos == s.d.size())
            return meta;
    }

    return -1;
}

int write_ffscript(PACKFILE *f, const ZqPartContext &ctx)
{
    const int v = ctx.version;
    bool q255 = ctx.zelda_version >= 0x255;

    if(header_is_253(ctx) && v == V_FFSCRIPT && ctx.cversion == CV_FFSCRIPT)
        return ZQW_UNSUPPORTED;

    if(q255 ? (v < 6 || v > 31) : (v != 5 && v != 6))
        return ZQW_UNSUPPORTED;

    Src s;
    Head h;

    if(!load_part(*ctx.part, s) || !read_head(s, q255 && v >= 18 ? 2 : 0, h))
    {
        set_why(zq_part_name(*ctx.part) + ": the kept copy of the part couldn't be read");
        return BAD_PART;
    }

    Buf body;
    Patch p(s, body);

    if(q255)
    {
        int meta = find_meta(s, h.body, v);

        if(meta < 0)
        {
            set_why(zq_part_name(*ctx.part) + ": no script meta layout fits the part");
            return BAD_PART;
        }

        // 2.55 keeps Link's scripts as hero scripts, which 2.53's Link slots never get
        for(int i = 0; i < 3; ++i)
            if(!script_same(linkscripts[i], g_snap.link[i]))
                p.fail("Link scripts (this version's reader keeps them apart from 2.53's)");

        Ffsc255 w(s, &p, v, meta);
        w.run();
    }
    else
        ffsc_old(p, v);

    if(s.bad)
    {
        set_why(zq_part_name(*ctx.part) + ": the part doesn't have the layout its version says");
        return BAD_PART;
    }

    if(p.cant)
        return cant_hold(zq_part_name(*ctx.part), p.what);

    p.copy(s.left());
    return emit(f, ctx.part->id, s, h, body);
}

ZqWriterRegistration g_reg_ffscript(ID_FFSCRIPT, write_ffscript, "scripts v5, v6, 2.55 v6-v31");

} // namespace

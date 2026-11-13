// Saving a quest made in 2.55 as 3.0 Nightly (zqn_255.h): the scripts (FFSC).
//
// 2.55 wrote section versions 18 to 22; early 3.0 Nightlies whose quests still say 2.55 wrote
// up to 26 in the same layout. 3.0 reads them with read_old_ffscript, and since their scripts
// weren't compiled by its own compiler (the quest's last compiled version is at most 26) it
// writes them back in that layout as version 26 (writeffscript_old):
// - each script is its command count, its metadata, then its commands up to the terminator;
//   each command now with a third argument (0 before version 24) and its string and array
//   literals (none before version 21);
// - the metadata is written in the newest layout, version 7 (the 2-byte field before the
//   section size); the quest's own layout is read the way read_one_zmeta reads it, quirks
//   included (read_meta);
// - a script whose first command 3.0 doesn't know, or which jumps outside itself, keeps its
//   metadata but no commands;
// - the old weapon scripts, which 3.0 reads and drops, and the tables the quest's version
//   doesn't have (generic scripts before version 20, subscreen scripts before 22) are empty
//   slots;
// - the source and the 13 tables of slot names are written as 3.0's maps hold them: names end
//   at a NUL, ids past a table's end are dropped, global slot 0 is "~Init" unless the quest
//   names it, and an old "~Continue" global name is dropped along with global script 3's
//   commands.
//
// Versions 27 to 30 (3.0's single shared command stream, from early 3.0 Nightlies whose quests
// still say 2.55) are written as 31 by 3.0's newer writer (write_scripts_31 below).
//
// A quest's scripts can be tens of MB, more in 3.0's layout: the section is read once into an
// index, then written straight to the file (zqn_out.h).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqn_255.h"
#include "zqn_out.h"

namespace
{

const word TERMINATOR = 0xFFFF;

// 3.0's command numbers (components/zasm/defines.h) for the jump check.
const word GOTO_ = 11, GOTOTRUE_ = 16, GOTOFALSE_ = 17, GOTOLESS_ = 18, GOTOMORE_ = 19, LOOP_ = 65;
const word CALLFUNC_ = 1105, GOTOCMP_ = 1108, GOTOTABLE_ = 1187, GOTORANGES_ = 1188, NUMCOMMANDS_30 = 1190;

const int ATTRIBUTES = 24;          // NUM_ZMETA_ATTRIBUTES
const word META_30 = 7;             // METADATA_V
const long MAX_COMMANDS = 10000000; // read_old_ffscript's limit
const long MAX_LITERALS = 65535;    // zasm_literals::max_size
const long MAX_SOURCE = 1024 * 1024 * 10;
const long MAX_NAME = 1024;

// The script tables, in the order the section holds them, and how many slots 3.0 writes.
enum
{
    T_FFC, T_ITEM, T_GUY, T_WEAPON, T_SCREEN, T_GLOBAL, T_HERO, T_LWPN, T_EWPN, T_DMAP,
    T_ITEMSPRITE, T_COMBO, T_GENERIC, T_SUBSCREEN, T_COUNT
};

const int g_slots[T_COUNT] = { 512, 256, 256, 256, 256, 8, 5, 256, 256, 256, 256, 512, 512, 256 };

// The slot name tables (ffc, global, item, npc, lweapon, eweapon, hero, DMap, screen, item
// sprite, combo, generic, subscreen): the ids 3.0's reader keeps are below these.
const int NAME_TABLES = 13;
const int g_name_limit[NAME_TABLES] = { 511, 8, 255, 255, 255, 255, 4, 255, 255, 255, 511, 511, 255 };
const int GLOBAL_NAMES = 1, GLOBAL_SCRIPT_ONSAVELOAD = 3;

// ---- metadata -------------------------------------------------------------------------------

// zasm_meta as 3.0 holds it; the constructor gives zero()'s values.
struct Meta
{
    word zasm_v, meta_v, ffscript_v;
    byte type;
    std::string run_idens[8];
    byte run_types[8];
    byte flags;
    word compiler[4];
    std::string name, author, info, setup;
    std::string attr[ATTRIBUTES], attr_help[ATTRIBUTES];
    std::string usr[16], usr_help[16];
    std::string initd[8], initd_help[8];
    byte initd_type[8];

    Meta() : zasm_v(0), meta_v(0), ffscript_v(0), type(0), flags(0)
    {
        memset(run_types, 1, sizeof(run_types));     // ZMETA_NULL_TYPE
        memset(compiler, 0, sizeof(compiler));
        memset(initd_type, 0xFF, sizeof(initd_type)); // -1
    }
};

// p_getcstr: a byte count, then that many bytes, added to what the string already holds (it
// doesn't clear it first).
void get_cstr(Zqn255In &r, std::string &s)
{
    size_t n = r.u8();
    std::vector<byte> b = r.bytes(n);

    if(!b.empty())
        s.append((const char *)&b[0], b.size());
}

std::string up_to_nul(const std::vector<byte> &b)
{
    size_t n = 0;

    while(n < b.size() && b[n])
        ++n;

    return std::string(b.empty() ? "" : (const char *)&b[0], n);
}

// p_getwstr: a word count, then that many bytes, taken up to the first NUL.
std::string get_wstr(Zqn255In &r)
{
    size_t n = r.u16();
    return up_to_nul(r.bytes(n));
}

// The fixed 33-byte texts of metadata versions 1 and 2, up to the first NUL.
std::string get_text33(Zqn255In &r)
{
    return up_to_nul(r.bytes(33));
}

// read_one_zmeta, for every layout up to 3.0's.
bool read_meta(Zqn255In &r, word zv, Meta &m)
{
    m.zasm_v = r.u16();
    m.meta_v = r.u16();
    m.ffscript_v = r.u16();
    m.type = r.u8();

    for(int q = 0; q < 8; ++q)
    {
        if(zv < 3)
            m.run_idens[q] = get_text33(r);
        else
            get_cstr(r, m.run_idens[q]);
    }

    for(int q = 0; q < 8; ++q)
        m.run_types[q] = r.u8();

    m.flags = r.u8();

    for(int q = 0; q < 4; ++q)
        m.compiler[q] = r.u16();

    if(zv == 2)
    {
        m.name = get_text33(r);
        m.author = get_text33(r);
    }
    else if(zv > 2)
    {
        get_cstr(r, m.name);
        get_cstr(r, m.author);

        if(zv >= 7)
        {
            m.info = get_wstr(r);
            m.setup = get_wstr(r);
        }

        int count = zv < 5 ? 4 : 10;

        if(zv < 6)
        {
            // the attributes, then the old attribytes as attributes 8-15 (over 8 and 9 only
            // when not empty), then the attrishorts as 16-23. The attribytes are read into one
            // string that the label read doesn't clear, so each label is read after the
            // previous one's help text.
            for(int q = 0; q < count; ++q)
            {
                get_cstr(r, m.attr[q]);
                m.attr_help[q] = get_wstr(r);
            }

            std::string tmp;

            for(int q = 0; q < 8; ++q)
            {
                get_cstr(r, tmp);

                if(8 + q >= count || !tmp.empty())
                    m.attr[8 + q] = tmp;

                tmp = get_wstr(r);

                if(8 + q >= count || !tmp.empty())
                    m.attr_help[8 + q] = tmp;
            }

            for(int q = 0; q < 8; ++q)
            {
                get_cstr(r, m.attr[16 + q]);
                m.attr_help[16 + q] = get_wstr(r);
            }
        }
        else
        {
            count = r.u16();

            if(count > ATTRIBUTES)
                return false;   // 3.0 refuses the quest

            for(int q = 0; q < count; ++q)
            {
                get_cstr(r, m.attr[q]);
                m.attr_help[q] = get_wstr(r);
            }
        }

        for(int q = 0; q < 16; ++q)
        {
            get_cstr(r, m.usr[q]);
            m.usr_help[q] = get_wstr(r);
        }
    }

    if(zv > 3)
    {
        for(int q = 0; q < 8; ++q)
        {
            get_cstr(r, m.initd[q]);
            m.initd_help[q] = get_wstr(r);
        }

        for(int q = 0; q < 8; ++q)
            m.initd_type[q] = r.u8();
    }
    else
    {
        for(int q = 0; q < 8; ++q)
            m.initd[q] = m.run_idens[q];
    }

    return !r.bad;
}

// p_putcstr and p_putwstr: at most 255 and 65535 bytes.
void put_cstr(Zqn255Out &o, const std::string &s)
{
    size_t n = s.size() < 255 ? s.size() : 255;
    o.c((int64_t)n);
    o.put(s.data(), n);
}

void put_wstr(Zqn255Out &o, const std::string &s)
{
    size_t n = s.size() < 65535 ? s.size() : 65535;
    o.w((int64_t)n);
    o.put(s.data(), n);
}

// write_one_ffscript_meta
std::vector<byte> meta_bytes(const Meta &m)
{
    Zqn255Out o;
    o.w(m.zasm_v);
    o.w(m.meta_v);
    o.w(m.ffscript_v);
    o.c(m.type);

    for(int q = 0; q < 8; ++q)
        put_cstr(o, m.run_idens[q]);

    for(int q = 0; q < 8; ++q)
        o.c(m.run_types[q]);

    o.c(m.flags);

    for(int q = 0; q < 4; ++q)
        o.w(m.compiler[q]);

    put_cstr(o, m.name);
    put_cstr(o, m.author);
    put_wstr(o, m.info);
    put_wstr(o, m.setup);
    o.w(ATTRIBUTES);

    for(int q = 0; q < ATTRIBUTES; ++q)
    {
        put_cstr(o, m.attr[q]);
        put_wstr(o, m.attr_help[q]);
    }

    for(int q = 0; q < 16; ++q)
    {
        put_cstr(o, m.usr[q]);
        put_wstr(o, m.usr_help[q]);
    }

    for(int q = 0; q < 8; ++q)
    {
        put_cstr(o, m.initd[q]);
        put_wstr(o, m.initd_help[q]);
    }

    for(int q = 0; q < 8; ++q)
        o.c(m.initd_type[q]);

    return o.b;
}

// ---- the scripts ------------------------------------------------------------------------------

// One script as 3.0 holds it: its metadata, and where its commands are in the section's data.
struct Script
{
    std::vector<byte> meta;     // in 3.0's layout
    size_t at, end;             // the commands as the quest has them
    long count;                 // the commands 3.0 keeps, with the terminator; 0: none

    Script() : at(0), end(0), count(0) {}
};

struct Scripts
{
    int version;
    std::vector<Script> tables[T_COUNT];
    std::string source;
    std::map<int, std::string> names[NAME_TABLES];

    Scripts() : version(0) {}
};

bool single_jump(word command)
{
    switch(command)
    {
    case GOTO_: case GOTOTRUE_: case GOTOFALSE_: case GOTOLESS_: case GOTOMORE_:
    case GOTOCMP_: case CALLFUNC_: case LOOP_:
        return true;
    }

    return false;
}

// The lowest and highest pc a script jumps to (zasm_find_invalid_jump's targets).
struct Targets
{
    bool any;
    long lo, hi;

    Targets() : any(false), lo(0), hi(0) {}

    void add(long pc)
    {
        if(!any || pc < lo)
            lo = pc;
        if(!any || pc > hi)
            hi = pc;
        any = true;
    }
};

// read_old_ffscript: the count, the metadata, then the commands up to the count or the
// terminator (which 3.0 keeps), whichever comes first. A script whose first command 3.0
// doesn't know, or with a jump outside it, keeps none.
bool read_script(Zqn255In &r, int v, word zv, Script *sc)
{
    long num = r.s32();

    if(r.bad || num < 0 || num > MAX_COMMANDS)
    {
        zqw_last_error = "a script is longer than 3.0 reads";
        return false;
    }

    Meta m;

    if(!read_meta(r, zv, m))
    {
        zqw_last_error = "a script's metadata isn't laid out as its version says";
        return false;
    }

    size_t at = r.pos;
    long count = 0, literals = 0;
    word first = TERMINATOR;
    Targets jumps;

    for(long j = 0; j < num; ++j)
    {
        word command = r.u16();

        if(r.bad)
            break;

        if(j == 0)
            first = command;

        ++count;

        if(command == TERMINATOR)
            break;

        long arg1 = r.s32();
        r.skip(v >= 24 ? 8 : 4);    // arg2, and arg3 from version 24

        if(single_jump(command))
            jumps.add(arg1);

        if(v >= 21)
        {
            // the sizes are checked against what is left before skipping, so a bad one can't
            // wrap the position around in a 32-bit build
            dword chars = r.u32();

            if(r.bad || chars > r.d.size() - r.pos)
            {
                r.bad = true;
                break;
            }

            r.skip(chars);
            dword values = r.u32();

            if(r.bad || values > (r.d.size() - r.pos) / 4)
            {
                r.bad = true;
                break;
            }

            if(chars || values)
                ++literals;

            if(values && (command == GOTOTABLE_ || command == GOTORANGES_))
            {
                // zasm_jump_targets: a table's targets after its first value; ranges have
                // the default first, then each range's start, end and target
                std::vector<long> vec(values);

                for(dword q = 0; q < values; ++q)
                    vec[q] = r.s32();

                if(command == GOTOTABLE_)
                {
                    for(dword q = 1; q < values; ++q)
                        jumps.add(vec[q]);
                }
                else
                {
                    jumps.add(vec[0]);

                    for(dword q = 0; q < (values - 1) / 3; ++q)
                        jumps.add(vec[3 + q * 3]);
                }
            }
            else
                r.skip((size_t)values * 4);
        }
    }

    if(r.bad)
    {
        zqw_last_error = "a script's commands run past the end of the section";
        return false;
    }

    if(literals > MAX_LITERALS)
    {
        zqw_last_error = "a script has more string and array literals than 3.0 reads";
        return false;
    }

    if(!sc)
        return true;

    sc->meta = meta_bytes(m);
    sc->at = at;
    sc->end = r.pos;
    sc->count = count;

    if(count > 0 && first >= NUMCOMMANDS_30 && first != TERMINATOR)
        sc->count = 0;
    else if(jumps.any && (jumps.lo < 0 || jumps.hi >= count))
        sc->count = 0;

    return true;
}

bool read_table(Zqn255In &r, int v, word zv, int count, std::vector<Script> *table)
{
    if(table)
        table->resize(count);

    for(int i = 0; i < count; ++i)
        if(!read_script(r, v, zv, table ? &(*table)[i] : NULL))
            return false;

    return true;
}

// A table of slot names: a count, then each name's slot and text. 3.0 keeps the text up to a
// NUL, and only the slots below the table's limit; a slot named twice takes the last.
bool read_names(Zqn255In &r, int table, Scripts &s, bool &continue_disabled)
{
    int count = r.u16();

    for(int i = 0; i < count && !r.bad; ++i)
    {
        int id = r.u16();
        long size = r.s32();

        if(r.bad || size < 0 || size > MAX_NAME)
            return false;

        std::string name = up_to_nul(r.bytes((size_t)size));

        if(r.bad || id >= g_name_limit[table])
            continue;

        if(table == GLOBAL_NAMES && name == "~Continue")
        {
            s.names[table][id] = "";
            continue_disabled = true;
        }
        else
            s.names[table][id] = name;
    }

    return !r.bad;
}

// readffscript, for versions 18 to 26.
bool read_scripts(const std::vector<byte> &d, int v, word zv, Scripts &s)
{
    Zqn255In r(d);
    s.version = v;

    // every table up to the combo scripts has 3.0's count of slots; the old weapon scripts
    // are read and dropped
    for(int t = T_FFC; t <= T_COMBO; ++t)
        if(!read_table(r, v, zv, g_slots[t], t == T_WEAPON ? NULL : &s.tables[t]))
            return false;

    if(v > 19)
    {
        int count = r.u16();

        if(r.bad || count > g_slots[T_GENERIC] || !read_table(r, v, zv, count, &s.tables[T_GENERIC]))
            return false;
    }

    if(v > 21)
    {
        int count = r.u16();

        if(r.bad || count > g_slots[T_SUBSCREEN] || !read_table(r, v, zv, count, &s.tables[T_SUBSCREEN]))
            return false;
    }

    long size = r.s32();

    if(r.bad || size < 0 || size > MAX_SOURCE)
        return false;

    s.source = up_to_nul(r.bytes((size_t)size));

    // the tables of slot names this version has (all but generic before 20 and subscreen
    // before 22); 3.0 names global slot 0 "~Init" before reading them
    int tables = NAME_TABLES - (v > 19 ? 0 : 1) - (v > 21 ? 0 : 1);
    bool continue_disabled = false;
    s.names[GLOBAL_NAMES][0] = "~Init";

    for(int t = 0; t < tables; ++t)
        if(!read_names(r, t, s, continue_disabled))
            return false;

    // an old "~Continue" global: 3.0 disables global script 3 (its metadata stays)
    if(continue_disabled)
        s.tables[T_GLOBAL][GLOBAL_SCRIPT_ONSAVELOAD].count = 0;

    return r.at_end();
}

// ---- writing --------------------------------------------------------------------------------

struct Body
{
    const std::vector<byte> &d;
    const Scripts &s;
    std::vector<byte> empty_meta;

    Body(const std::vector<byte> &data, const Scripts &scripts) : d(data), s(scripts), empty_meta(meta_bytes(Meta())) {}

    // A script's commands in version 26's layout: the third argument and the literals follow
    // the first two arguments; nothing follows the terminator.
    void commands(zqn::Out &o, const Script &sc)
    {
        const int v = s.version;

        if(v >= 24)
        {
            o.put(&d[sc.at], sc.end - sc.at);
            return;
        }

        size_t p = sc.at;

        for(long j = 0; j < sc.count; ++j)
        {
            o.put(&d[p], 2);
            word command = (word)(d[p] | (d[p + 1] << 8));
            p += 2;

            if(command == TERMINATOR)
                break;

            o.put(&d[p], 8);        // arg1, arg2
            p += 8;
            o.l(0);                 // arg3

            if(v >= 21)
            {
                size_t start = p;
                Zqn255In r(d, p);
                r.skip(r.u32());
                r.skip((size_t)r.u32() * 4);
                p = r.pos;
                o.put(&d[start], p - start);
            }
            else
                o.zeros(8);         // no string, no array
        }
    }

    void script(zqn::Out &o, const Script *sc)
    {
        if(!sc)
        {
            o.l(0);
            o.put(&empty_meta[0], empty_meta.size());
            return;
        }

        o.l(sc->count);
        o.put(&sc->meta[0], sc->meta.size());

        if(sc->count > 0)
            commands(o, *sc);
    }

    bool operator()(zqn::Out &o)
    {
        for(int t = 0; t < T_COUNT; ++t)
        {
            if(t == T_GENERIC || t == T_SUBSCREEN)
                o.w(g_slots[t]);

            const std::vector<Script> &table = s.tables[t];

            for(int i = 0; i < g_slots[t]; ++i)
                script(o, i < (int)table.size() ? &table[i] : NULL);
        }

        o.l((long)s.source.size());
        o.put(s.source.data(), s.source.size());

        for(int t = 0; t < NAME_TABLES; ++t)
        {
            const std::map<int, std::string> &m = s.names[t];
            long count = 0;

            for(std::map<int, std::string>::const_iterator it = m.begin(); it != m.end(); ++it)
                if(!it->second.empty())
                    ++count;

            o.w(count);

            for(std::map<int, std::string>::const_iterator it = m.begin(); it != m.end(); ++it)
                if(!it->second.empty())
                {
                    o.w(it->first);
                    o.l((long)it->second.size());
                    o.put(it->second.data(), it->second.size());
                }
        }

        return true;
    }
};

int write_scripts(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || ctx.target != ZQ_NEWER_30 || !ctx.src_part || ctx.src_version < 18 || ctx.src_version > 26)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    if(s.version < 18 || s.version > 26)
        return ZQW_UNSUPPORTED;

    word zv = s.extra.size() == 2 ? (word)(s.extra[0] | (s.extra[1] << 8)) : 0;

    if(s.extra.size() != 2 || zv > META_30)
    {
        zqw_last_error = "its scripts' metadata is in a layout 3.0 doesn't know";
        return 2;
    }

    Scripts scripts;
    zqw_last_error.clear();

    if(!read_scripts(s.data, s.version, zv, scripts))
    {
        if(zqw_last_error.empty())
            zqw_last_error = "its scripts aren't laid out as their version says";

        return 2;
    }

    Body body(s.data, scripts);
    return zqn::write_section(f, ID_FFSCRIPT, 26, 1, META_30, body);
}

ZqNewerRegistration g_reg_scripts(ID_FFSCRIPT, write_scripts, "2.55 scripts as 3.0");

// ---- 2.55.17 --------------------------------------------------------------------------------
//
// 2.55.17 reads versions 18 to 21 with readffscript / read_one_ffscript and writes them as its
// version 22 (writeffscript / write_one_ffscript):
// - every command up to the terminator is kept as it is (2.55.17 checks none); each command now
//   has its string and array literals (none before version 21), and a script's count becomes
//   the commands up to and with the terminator (recalc_size); a slot with no commands is one
//   terminator;
// - the metadata is written in 2.55.17's layout, version 5: 10 attributes, then the attribytes,
//   attrishorts and user flags kept apart (3.0 folds them into its 24 attributes);
// - tables the quest's version doesn't have (generic scripts before 20, subscreen scripts
//   before 22) and the old weapon scripts are new, empty scripts;
// - the slot names as for 3.0 (the same rules in both readers).

const word META_255 = 5;    // 2.55.17's METADATA_V

// 2.55.17's zasm_meta; the constructor gives zero()'s values.
struct Meta255
{
    word zasm_v, meta_v, ffscript_v;
    byte type;
    std::string run_idens[8];
    byte run_types[8];
    byte flags;
    word compiler[4];
    std::string name, author;
    std::string attr[10], attr_help[10];
    std::string attribytes[8], attribytes_help[8];
    std::string attrishorts[8], attrishorts_help[8];
    std::string usr[16], usr_help[16];
    std::string initd[8], initd_help[8];
    byte initd_type[8];

    Meta255() : zasm_v(0), meta_v(0), ffscript_v(0), type(0), flags(0)
    {
        memset(run_types, 1, sizeof(run_types));     // ZMETA_NULL_TYPE
        memset(compiler, 0, sizeof(compiler));
        memset(initd_type, 0xFF, sizeof(initd_type)); // -1
    }
};

// read_one_ffscript's metadata, for layouts 0 to 5.
bool read_meta_255(Zqn255In &r, word zv, Meta255 &m)
{
    m.zasm_v = r.u16();
    m.meta_v = r.u16();
    m.ffscript_v = r.u16();
    m.type = r.u8();

    for(int q = 0; q < 8; ++q)
    {
        if(zv < 3)
            m.run_idens[q] = get_text33(r);
        else
            get_cstr(r, m.run_idens[q]);
    }

    for(int q = 0; q < 8; ++q)
        m.run_types[q] = r.u8();

    m.flags = r.u8();

    for(int q = 0; q < 4; ++q)
        m.compiler[q] = r.u16();

    if(zv == 2)
    {
        m.name = get_text33(r);
        m.author = get_text33(r);
    }
    else if(zv > 2)
    {
        get_cstr(r, m.name);
        get_cstr(r, m.author);

        for(int q = 0; q < (zv < 5 ? 4 : 10); ++q)
        {
            get_cstr(r, m.attr[q]);
            m.attr_help[q] = get_wstr(r);
        }

        for(int q = 0; q < 8; ++q)
        {
            get_cstr(r, m.attribytes[q]);
            m.attribytes_help[q] = get_wstr(r);
        }

        for(int q = 0; q < 8; ++q)
        {
            get_cstr(r, m.attrishorts[q]);
            m.attrishorts_help[q] = get_wstr(r);
        }

        for(int q = 0; q < 16; ++q)
        {
            get_cstr(r, m.usr[q]);
            m.usr_help[q] = get_wstr(r);
        }
    }

    if(zv > 3)
    {
        for(int q = 0; q < 8; ++q)
        {
            get_cstr(r, m.initd[q]);
            m.initd_help[q] = get_wstr(r);
        }

        for(int q = 0; q < 8; ++q)
            m.initd_type[q] = r.u8();
    }
    else
    {
        for(int q = 0; q < 8; ++q)
            m.initd[q] = m.run_idens[q];
    }

    return !r.bad;
}

// write_one_ffscript's metadata
std::vector<byte> meta_bytes_255(const Meta255 &m)
{
    Zqn255Out o;
    o.w(m.zasm_v);
    o.w(m.meta_v);
    o.w(m.ffscript_v);
    o.c(m.type);

    for(int q = 0; q < 8; ++q)
        put_cstr(o, m.run_idens[q]);

    for(int q = 0; q < 8; ++q)
        o.c(m.run_types[q]);

    o.c(m.flags);

    for(int q = 0; q < 4; ++q)
        o.w(m.compiler[q]);

    put_cstr(o, m.name);
    put_cstr(o, m.author);

    for(int q = 0; q < 10; ++q)
    {
        put_cstr(o, m.attr[q]);
        put_wstr(o, m.attr_help[q]);
    }

    for(int q = 0; q < 8; ++q)
    {
        put_cstr(o, m.attribytes[q]);
        put_wstr(o, m.attribytes_help[q]);
    }

    for(int q = 0; q < 8; ++q)
    {
        put_cstr(o, m.attrishorts[q]);
        put_wstr(o, m.attrishorts_help[q]);
    }

    for(int q = 0; q < 16; ++q)
    {
        put_cstr(o, m.usr[q]);
        put_wstr(o, m.usr_help[q]);
    }

    for(int q = 0; q < 8; ++q)
    {
        put_cstr(o, m.initd[q]);
        put_wstr(o, m.initd_help[q]);
    }

    for(int q = 0; q < 8; ++q)
        o.c(m.initd_type[q]);

    return o.b;
}

// One script as 2.55.17 holds it: its metadata and where its commands are (the terminator
// included); count is the commands it writes, the terminator included.
struct Script255
{
    std::vector<byte> meta;
    size_t at, end;
    long count;

    Script255() : at(0), end(0), count(0) {}
};

// The tables in writeffscript's order (the same as the section's).
const int T255_COUNT = T_COUNT;

struct Scripts255
{
    int version;
    std::vector<Script255> tables[T255_COUNT];
    std::string source;
    std::map<int, std::string> names[NAME_TABLES];

    Scripts255() : version(0) {}
};

// read_one_ffscript: the count, the metadata, then the commands up to the count or the
// terminator. A slot with no commands holds a terminator only; one with commands but no
// terminator isn't something 2.55.17 can write back (recalc_size runs past it).
bool read_script_255(Zqn255In &r, int v, word zv, Script255 *sc)
{
    long num = r.s32();

    if(r.bad || num < 0 || num > MAX_COMMANDS)
    {
        zqw_last_error = "a script is longer than 2.55.17 reads";
        return false;
    }

    Meta255 m;

    if(!read_meta_255(r, zv, m))
    {
        zqw_last_error = "a script's metadata isn't laid out as its version says";
        return false;
    }

    size_t at = r.pos;
    long count = 0;

    for(long j = 0; j < num && !r.bad; ++j)
    {
        word command = r.u16();

        if(command == TERMINATOR)
        {
            count = j + 1;
            break;
        }

        r.skip(8);  // arg1, arg2

        if(v >= 21)
        {
            dword chars = r.u32();

            if(r.bad || chars > r.d.size() - r.pos)
            {
                r.bad = true;
                break;
            }

            r.skip(chars);
            dword values = r.u32();

            if(r.bad || values > (r.d.size() - r.pos) / 4)
            {
                r.bad = true;
                break;
            }

            r.skip((size_t)values * 4);
        }
    }

    if(r.bad)
    {
        zqw_last_error = "a script's commands run past the end of the section";
        return false;
    }

    if(num > 0 && count == 0)
    {
        zqw_last_error = "a script has no end command";
        return false;
    }

    if(!sc)
        return true;

    sc->meta = meta_bytes_255(m);
    sc->at = at;
    sc->end = r.pos;
    sc->count = num == 0 ? 1 : count;
    return true;
}

bool read_table_255(Zqn255In &r, int v, word zv, int count, std::vector<Script255> *table)
{
    if(table)
        table->resize(count);

    for(int i = 0; i < count; ++i)
        if(!read_script_255(r, v, zv, table ? &(*table)[i] : NULL))
            return false;

    return true;
}

// readffscript, for versions 18 to 22.
bool read_scripts_255(const std::vector<byte> &d, int v, word zv, Scripts255 &s)
{
    Zqn255In r(d);
    s.version = v;

    for(int t = T_FFC; t <= T_COMBO; ++t)
        if(!read_table_255(r, v, zv, g_slots[t], t == T_WEAPON ? NULL : &s.tables[t]))
            return false;

    if(v > 19)
    {
        int count = r.u16();

        if(r.bad || count > g_slots[T_GENERIC] || !read_table_255(r, v, zv, count, &s.tables[T_GENERIC]))
            return false;
    }

    if(v > 21)
    {
        int count = r.u16();

        if(r.bad || count > g_slots[T_SUBSCREEN] || !read_table_255(r, v, zv, count, &s.tables[T_SUBSCREEN]))
            return false;
    }

    long size = r.s32();

    if(r.bad || size < 0 || size > MAX_SOURCE)
        return false;

    s.source = up_to_nul(r.bytes((size_t)size));

    // the slot names: the same reading as 3.0's (read_names, which takes Scripts)
    Scripts names;
    names.names[GLOBAL_NAMES][0] = "~Init";
    int tables = NAME_TABLES - (v > 19 ? 0 : 1) - (v > 21 ? 0 : 1);
    bool continue_disabled = false;

    for(int t = 0; t < tables; ++t)
        if(!read_names(r, t, names, continue_disabled))
            return false;

    for(int t = 0; t < NAME_TABLES; ++t)
        s.names[t] = names.names[t];

    // an old "~Continue" global: global script 3 is disabled (its metadata stays)
    if(continue_disabled && (int)s.tables[T_GLOBAL].size() > GLOBAL_SCRIPT_ONSAVELOAD)
    {
        Script255 &g = s.tables[T_GLOBAL][GLOBAL_SCRIPT_ONSAVELOAD];
        g.count = 1;
        g.end = g.at;
    }

    return r.at_end();
}

struct Body255
{
    const std::vector<byte> &d;
    const Scripts255 &s;
    std::vector<byte> empty_meta;

    Body255(const std::vector<byte> &data, const Scripts255 &scripts) : d(data), s(scripts), empty_meta(meta_bytes_255(Meta255())) {}

    // A script's commands in version 22's layout (from version 21 they are already).
    void commands(zqn::Out &o, const Script255 &sc)
    {
        if(sc.end == sc.at)
        {
            o.w(TERMINATOR);
            return;
        }

        if(s.version >= 21)
        {
            o.put(&d[sc.at], sc.end - sc.at);
            return;
        }

        size_t p = sc.at;

        for(long j = 0; j < sc.count; ++j)
        {
            o.put(&d[p], 2);
            word command = (word)(d[p] | (d[p + 1] << 8));
            p += 2;

            if(command == TERMINATOR)
                break;

            o.put(&d[p], 8);        // arg1, arg2
            p += 8;
            o.zeros(8);             // no string, no array
        }
    }

    void script(zqn::Out &o, const Script255 *sc)
    {
        if(!sc)
        {
            o.l(1);
            o.put(&empty_meta[0], empty_meta.size());
            o.w(TERMINATOR);
            return;
        }

        o.l(sc->count);
        o.put(&sc->meta[0], sc->meta.size());
        commands(o, *sc);
    }

    bool operator()(zqn::Out &o)
    {
        for(int t = 0; t < T255_COUNT; ++t)
        {
            if(t == T_GENERIC || t == T_SUBSCREEN)
                o.w(g_slots[t]);

            const std::vector<Script255> &table = s.tables[t];

            for(int i = 0; i < g_slots[t]; ++i)
                script(o, i < (int)table.size() ? &table[i] : NULL);
        }

        o.l((long)s.source.size());
        o.put(s.source.data(), s.source.size());

        for(int t = 0; t < NAME_TABLES; ++t)
        {
            const std::map<int, std::string> &m = s.names[t];
            long count = 0;

            for(std::map<int, std::string>::const_iterator it = m.begin(); it != m.end(); ++it)
                if(!it->second.empty())
                    ++count;

            o.w(count);

            for(std::map<int, std::string>::const_iterator it = m.begin(); it != m.end(); ++it)
                if(!it->second.empty())
                {
                    o.w(it->first);
                    o.l((long)it->second.size());
                    o.put(it->second.data(), it->second.size());
                }
        }

        return true;
    }
};

int write_scripts_255(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || ctx.target != ZQ_NEWER_255 || !ctx.src_part || ctx.src_version < 18 || ctx.src_version > 21)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    if(s.version < 18 || s.version > 21)
        return ZQW_UNSUPPORTED;

    word zv = s.extra.size() == 2 ? (word)(s.extra[0] | (s.extra[1] << 8)) : 0;

    if(s.extra.size() != 2 || zv > META_255)
    {
        zqw_last_error = "its scripts' metadata is in a layout 2.55.17 doesn't know";
        return 2;
    }

    Scripts255 scripts;
    zqw_last_error.clear();

    if(!read_scripts_255(s.data, s.version, zv, scripts))
    {
        if(zqw_last_error.empty())
            zqw_last_error = "its scripts aren't laid out as their version says";

        return 2;
    }

    Body255 body(s.data, scripts);
    return zqn::write_section(f, ID_FFSCRIPT, 22, 1, META_255, body);
}

ZqNewerRegistration g_reg_scripts_255(ID_FFSCRIPT, write_scripts_255, "2.55 scripts as 2.55.17");

// ---- versions 27-30, as 3.0's newer writer (31) ---------------------------------------------
//
// From version 27 every script's commands are in one shared stream (read_quest_zasm): a count,
// then each command (its number, three arguments, its string and array literals). Each slot is
// then a byte saying whether it holds a script, and if it does, its metadata, its first and end
// pc in the stream and (from version 30) its instance variable initial values and exported
// variables (two bounded maps). 3.0 keeps the stream as it is and writes, in version 31
// (writeffscript, write_quest_zasm, write_one_ffscript):
// - the stream as read (refused like 3.0 refuses it: too many commands or literals, or a jump
//   outside it);
// - each slot as 3.0 holds it: a script only when it ends past pc 0 and the stream doesn't start
//   with the terminator (script_data::valid), with its metadata in layout 7 and its maps (fresh,
//   empty ones of 256 before version 30); the exported variables in 31's layout, which adds an
//   engine type, a custom export type and custom names;
// - the old weapon scripts and the tables the version lacks as empty slots, the source and the
//   slot names as for the older versions;
// - the debug data at the end as stored (version 29 on: 3.0 decodes and encodes it again, the
//   same bytes), or an empty one.
// 3.0 writes this layout only when the misc data says the scripts were last compiled by a newer
// compiler than version 26; otherwise it would write the old layout from the shared stream, which
// isn't converted here.

const long MAX_COMMANDS_31 = 25000000;     // read_quest_zasm's limit
const word META_31 = 7;

// sizeof the bounded maps' values in upstream's build (x64): p_putbmap picks pairs or a plain
// list by it
const size_t SIZEOF_INT32 = 4, SIZEOF_EXPORTED_VARIABLE = 96;

// One value of a bounded map: its bytes as 3.0 writes them, and whether it is the map's default.
struct MapValue
{
    std::vector<byte> out;
    bool is_default;
};

typedef bool (*MapValueReader)(Zqn255In &r, MapValue &v);

// p_getvar(int32_t)
bool read_int_value(Zqn255In &r, MapValue &v)
{
    const dword x = r.u32();
    Zqn255Out o;
    o.l(x);
    v.out = o.b;
    v.is_default = x == 0;
    return !r.bad;
}

// exported_variable for version 30's packfile (no engine type, custom type or names), written in
// 31's (p_putvar(exported_variable)); version 31's own (from 3.0 prereleases) as it is: the same
// fields with an engine type (a word), a custom export type (a byte) and custom names (a word
// count, then each name's fixed-point value and text) between the button type and the range.
bool read_export_31(Zqn255In &r, MapValue &v)
{
    const size_t at = r.pos;
    std::string name, help;
    get_cstr(r, name);
    const dword hl = r.u32();

    if(r.bad || hl > r.d.size() - r.pos)
        return false;

    help = up_to_nul(r.bytes(hl));
    const byte btn = r.u8();
    const word engine = r.u16();
    const byte custom = r.u8();
    const word names = r.u16();

    for(word q = 0; q < names && !r.bad; ++q)
    {
        r.skip(4);
        const dword nl = r.u32();

        if(r.bad || nl > r.d.size() - r.pos)
            return false;

        r.skip(nl);
    }

    const dword mn = r.u32(), mx = r.u32();

    if(r.bad)
        return false;

    v.out.assign(r.d.begin() + at, r.d.begin() + r.pos);
    v.is_default = name.empty() && help.empty() && btn == 0xFF && !engine && !custom && !names && mn == 0x80000000u &&
                   mx == 0x7FFFFFFFu;
    return true;
}

bool read_export_30(Zqn255In &r, MapValue &v)
{
    std::string name, help;
    get_cstr(r, name);
    const dword hl = r.u32();

    if(r.bad || hl > r.d.size() - r.pos)
        return false;

    help = up_to_nul(r.bytes(hl));     // p_getlstr keeps the text up to a 0
    const byte btn = r.u8();
    const dword mn = r.u32(), mx = r.u32();
    Zqn255Out o;
    put_cstr(o, name);
    o.l((int64_t)help.size());
    o.put(help.data(), help.size());
    o.c(btn);
    o.w(0);         // engine_type: none
    o.c(0);         // export_custom_type: none
    o.w(0);         // custom_export_names: none
    o.l(mn);
    o.l(mx);
    v.out = o.b;
    // exported_variable{}: btn_type -1, min -214748.3648, max 214748.3647
    v.is_default = name.empty() && help.empty() && btn == 0xFF && mn == 0x80000000u && mx == 0x7FFFFFFFu;
    return !r.bad;
}

// A bounded map<word, T> as p_getbmap reads it and p_putbmap writes it again: its size, then
// (when not 0) a pairs byte, a count, and the entries (key and value pairs, or values from 0).
// normalize drops the entries at the default and past the size.
bool copy_bmap(Zqn255In &r, MapValueReader read, size_t sizeof_value, const std::vector<byte> &default_out, Zqn255Out &o)
{
    const word sz = r.u16();
    std::map<word, std::vector<byte> > m;

    if(sz)
    {
        const byte pairs = r.u8();
        const word count = r.u16();

        for(word q = 0; q < count && !r.bad; ++q)
        {
            const word k = pairs ? r.u16() : q;
            MapValue v;

            if(!read(r, v))
                return false;

            if(v.is_default || k >= sz)
                m.erase(k);
            else
                m[k] = v.out;
        }
    }

    if(r.bad)
        return false;

    o.w(sz);

    if(!sz)
        return true;

    const size_t writecnt_v = m.empty() ? 0 : (size_t)m.rbegin()->first + 1;
    const size_t writecnt_m = m.size();
    const bool pairs = writecnt_m * (sizeof_value + 2) <= writecnt_v * sizeof_value;
    o.c(pairs ? 1 : 0);
    o.w((int64_t)(pairs ? writecnt_m : writecnt_v));

    if(pairs)
    {
        for(std::map<word, std::vector<byte> >::const_iterator it = m.begin(); it != m.end(); ++it)
        {
            o.w(it->first);
            o.put(it->second);
        }

        return true;
    }

    // a plain list: the default where a key has none
    for(size_t q = 0; q < writecnt_v; ++q)
    {
        std::map<word, std::vector<byte> >::const_iterator it = m.find((word)q);
        o.put(it == m.end() ? default_out : it->second);
    }

    return true;
}

// The maps' defaults as p_putvar writes them: 0, and exported_variable{} in 31's layout (no
// name or help, button type -1, no engine or custom type, no custom names, the widest range).
const byte g_int_default[4] = { 0, 0, 0, 0 };
const byte g_export_default[] = { 0, 0, 0, 0, 0, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0xFF, 0xFF, 0xFF, 0x7F };

// A fresh bounded map of 256 with nothing set, as p_putbmap writes it.
void put_empty_bmap(Zqn255Out &o)
{
    o.w(256);       // MAX_SCRIPT_INST_VARIABLES
    o.c(1);         // pairs
    o.w(0);
}

struct Slot31
{
    std::vector<byte> out;      // as 3.0 writes the slot
};

struct Scripts31
{
    int version;
    size_t zasm_at, zasm_end;   // the shared stream, its count included
    long zasm_count;
    bool zasm_valid;
    std::vector<Slot31> tables[T_COUNT];
    std::string source;
    std::map<int, std::string> names[NAME_TABLES];
    std::vector<byte> debug;    // the debug data as p_putlvec writes it

    Scripts31() : version(0), zasm_at(0), zasm_end(0), zasm_count(0), zasm_valid(false) {}
};

// read_quest_zasm, with 3.0's checks: the command count, the literals, the jumps
bool read_zasm_31(Zqn255In &r, Scripts31 &s)
{
    s.zasm_at = r.pos;
    const long num = r.s32();

    if(r.bad || num < 0 || num > MAX_COMMANDS_31)
    {
        zqw_last_error = "its script commands are more than 3.0 reads";
        return false;
    }

    long literals = 0;
    word first = TERMINATOR;
    Targets jumps;

    for(long j = 0; j < num && !r.bad; ++j)
    {
        const word command = r.u16();

        if(j == 0)
            first = command;

        const long arg1 = r.s32();
        r.skip(8);      // arg2, arg3

        if(single_jump(command))
            jumps.add(arg1);

        const dword chars = r.u32();

        if(r.bad || chars > r.d.size() - r.pos)
        {
            r.bad = true;
            break;
        }

        r.skip(chars);
        const dword values = r.u32();

        if(r.bad || values > (r.d.size() - r.pos) / 4)
        {
            r.bad = true;
            break;
        }

        if(chars || values)
            ++literals;

        if(values && (command == GOTOTABLE_ || command == GOTORANGES_))
        {
            std::vector<long> vec(values);

            for(dword q = 0; q < values; ++q)
                vec[q] = r.s32();

            if(command == GOTOTABLE_)
            {
                for(dword q = 1; q < values; ++q)
                    jumps.add(vec[q]);
            }
            else
            {
                jumps.add(vec[0]);

                for(dword q = 0; q < (values - 1) / 3; ++q)
                    jumps.add(vec[3 + q * 3]);
            }
        }
        else
            r.skip((size_t)values * 4);

        if(command == TERMINATOR)
        {
            // write_quest_zasm leaves terminators out but counts them: such a stream doesn't
            // read back, so it isn't written
            zqw_last_error = "its script commands hold an end command, which 3.0 can't write back";
            return false;
        }
    }

    if(r.bad)
    {
        zqw_last_error = "its script commands run past the end of the section";
        return false;
    }

    if(literals > MAX_LITERALS)
    {
        zqw_last_error = "its scripts have more string and array literals than 3.0 reads";
        return false;
    }

    if(jumps.any && (jumps.lo < 0 || jumps.hi >= num))
    {
        zqw_last_error = "its scripts jump outside their commands, which 3.0 refuses";
        return false;
    }

    s.zasm_end = r.pos;
    s.zasm_count = num;
    s.zasm_valid = num > 0 && first != TERMINATOR;
    return true;
}

// read_one_ffscript for versions 27-30, and the slot as write_one_ffscript writes it
bool read_slot_31(Zqn255In &r, const Scripts31 &s, word zv, Slot31 *slot)
{
    const byte exists = r.u8();
    Zqn255Out o;

    if(r.bad)
        return false;

    if(!exists)
    {
        if(slot)
            slot->out.assign(1, 0);

        return true;
    }

    Meta m;

    if(!read_meta(r, zv, m))
    {
        zqw_last_error = "a script's metadata isn't laid out as its version says";
        return false;
    }

    const long pc = r.s32(), end_pc = r.s32();
    Zqn255Out maps;

    if(s.version >= 30)
    {
        const std::vector<byte> int_default(g_int_default, g_int_default + sizeof(g_int_default));
        const std::vector<byte> export_default(g_export_default, g_export_default + sizeof(g_export_default));

        if(!copy_bmap(r, read_int_value, SIZEOF_INT32, int_default, maps) ||
           !copy_bmap(r, s.version >= 31 ? read_export_31 : read_export_30, SIZEOF_EXPORTED_VARIABLE, export_default, maps))
        {
            zqw_last_error = "a script's variables aren't laid out as expected";
            return false;
        }
    }
    else
    {
        put_empty_bmap(maps);
        put_empty_bmap(maps);
    }

    if(r.bad)
        return false;

    if(!slot)
        return true;

    if(!end_pc || !s.zasm_valid)
    {
        slot->out.assign(1, 0);     // not a valid script: written as an empty slot
        return true;
    }

    o.c(1);
    std::vector<byte> mb = meta_bytes(m);
    o.put(mb);
    o.l(pc);
    o.l(end_pc);
    o.put(maps.b);
    slot->out = o.b;
    return true;
}

bool read_table_31(Zqn255In &r, const Scripts31 &s, word zv, int count, std::vector<Slot31> *table)
{
    if(table)
        table->resize(count);

    for(int i = 0; i < count; ++i)
        if(!read_slot_31(r, s, zv, table ? &(*table)[i] : NULL))
            return false;

    return true;
}

// readffscript, for versions 27 to 30
bool read_scripts_31(const std::vector<byte> &d, int v, word zv, Scripts31 &s)
{
    Zqn255In r(d);
    s.version = v;

    if(!read_zasm_31(r, s))
        return false;

    for(int t = T_FFC; t <= T_COMBO; ++t)
        if(!read_table_31(r, s, zv, g_slots[t], t == T_WEAPON ? NULL : &s.tables[t]))
            return false;

    for(int t = T_GENERIC; t <= T_SUBSCREEN; ++t)
    {
        const int count = r.u16();

        if(r.bad || count > g_slots[t] || !read_table_31(r, s, zv, count, &s.tables[t]))
            return false;
    }

    const long size = r.s32();

    if(r.bad || size < 0 || size > MAX_SOURCE)
        return false;

    s.source = up_to_nul(r.bytes((size_t)size));
    Scripts names;
    names.names[GLOBAL_NAMES][0] = "~Init";
    bool continue_disabled = false;

    for(int t = 0; t < NAME_TABLES; ++t)
        if(!read_names(r, t, names, continue_disabled))
            return false;

    for(int t = 0; t < NAME_TABLES; ++t)
        s.names[t] = names.names[t];

    // an old "~Continue" global: global script 3 is disabled (an empty slot)
    if(continue_disabled && (int)s.tables[T_GLOBAL].size() > GLOBAL_SCRIPT_ONSAVELOAD)
        s.tables[T_GLOBAL][GLOBAL_SCRIPT_ONSAVELOAD].out.assign(1, 0);

    Zqn255Out dbg;

    if(v >= 29)
    {
        const dword n = r.u32();

        if(r.bad || n > r.d.size() - r.pos)
            return false;

        std::vector<byte> b = r.bytes(n);

        if(b.empty() || b[0] != 1)      // DebugData::decode takes version 1 only
        {
            zqw_last_error = "its scripts' debug data isn't in a version 3.0 reads";
            return false;
        }

        dbg.l((int64_t)b.size());
        dbg.put(b);
    }
    else
    {
        // DebugData{}.encode(): its version, then no files, lines, types, scopes or symbols
        static const byte empty[] = { 1, 0, 0, 0, 0, 0 };
        dbg.l((int64_t)sizeof(empty));
        dbg.put(empty, sizeof(empty));
    }

    s.debug = dbg.b;
    return r.at_end();
}

struct Body31
{
    const std::vector<byte> &d;
    const Scripts31 &s;

    Body31(const std::vector<byte> &data, const Scripts31 &scripts) : d(data), s(scripts) {}

    bool operator()(zqn::Out &o)
    {
        o.put(&d[s.zasm_at], s.zasm_end - s.zasm_at);

        for(int t = 0; t < T_COUNT; ++t)
        {
            if(t == T_GENERIC || t == T_SUBSCREEN)
                o.w(g_slots[t]);

            const std::vector<Slot31> &table = s.tables[t];

            for(int i = 0; i < g_slots[t]; ++i)
            {
                if(i < (int)table.size())
                    o.put(&table[i].out[0], table[i].out.size());
                else
                    o.c(0);
            }
        }

        o.l((long)s.source.size());
        o.put(s.source.data(), s.source.size());

        for(int t = 0; t < NAME_TABLES; ++t)
        {
            const std::map<int, std::string> &m = s.names[t];
            long count = 0;

            for(std::map<int, std::string>::const_iterator it = m.begin(); it != m.end(); ++it)
                if(!it->second.empty())
                    ++count;

            o.w(count);

            for(std::map<int, std::string>::const_iterator it = m.begin(); it != m.end(); ++it)
                if(!it->second.empty())
                {
                    o.w(it->first);
                    o.l((long)it->second.size());
                    o.put(it->second.data(), it->second.size());
                }
        }

        o.put(&s.debug[0], s.debug.size());
        return true;
    }
};

int write_scripts_31(PACKFILE *f, const ZqNewerContext &ctx)
{
    // (version 31, 3.0's own, from 3.0 prereleases: read and written again the same way)
    if(!zqn255_source(ctx) || ctx.target != ZQ_NEWER_30 || !ctx.src_part || ctx.src_version < 27 || ctx.src_version > 31)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    word zv = s.extra.size() == 2 ? (word)(s.extra[0] | (s.extra[1] << 8)) : 0;

    if(s.extra.size() != 2 || zv > META_30)
    {
        zqw_last_error = "its scripts' metadata is in a layout 3.0 doesn't know";
        return 2;
    }

    long last_compiled = -1;

    if(!zqn255_last_compiled(last_compiled))
    {
        zqw_last_error = "its misc data (the scripts' compiler version) isn't laid out as expected";
        return 2;
    }

    if(last_compiled <= 26)
    {
        zqw_last_error = "its scripts were last compiled by an older compiler, which 3.0 would write in the "
                         "old layout from the shared commands (not converted yet)";
        return 2;
    }

    Scripts31 scripts;
    zqw_last_error.clear();

    if(!read_scripts_31(s.data, s.version, zv, scripts))
    {
        if(zqw_last_error.empty())
            zqw_last_error = "its scripts aren't laid out as their version says";

        return 2;
    }

    Body31 body(s.data, scripts);
    return zqn::write_section(f, ID_FFSCRIPT, 31, 31, META_31, body);
}

ZqNewerRegistration g_reg_scripts_31(ID_FFSCRIPT, write_scripts_31, "3.0 Nightly scripts (27-30) as 3.0");

} // namespace

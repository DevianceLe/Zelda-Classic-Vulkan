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
// Versions 27 to 30 (3.0's single shared command stream) are written as 31 by 3.0's newer
// writer, and aren't taken here.
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

} // namespace

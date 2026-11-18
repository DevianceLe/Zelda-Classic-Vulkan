// Saving a quest made in 2.55 as 2.55.17 or 3.0 Nightly (zqn_255.h): the editor names (ZINF);
// and the open quest's names, for ZQuest's lists and saves (zq_zinfo.h).
//
// readzinfo (zinfo.cpp, the same in both): item class names and help (512, or a count from
// version 1), then from version 1 combo type names and help, and map flag names and help; from
// 2 counter names, from 3 weapon names, from 4 (3.0) enemy class names, from 5 level item names,
// abbreviations and help. A version 0 section has no map flag names, and upstream's ZQuest
// (not its player) gives a quest with a 0x255 header the old scripted flags' names and a help
// line. writezinfo writes every kind its version has, each name as a counted string (cut at its
// first NUL, at most 255 characters) and each help as a word-counted one: the 512 item classes,
// cMAX combo types (183 in 2.55.17, 184 in 3.0), 256 map flags, 107 counters, 146 weapons, and
// in 3.0 73 enemy classes and 16 level items.
//
// A quest whose header's external_zinfo byte is set has no ZINF section: its names are in
// '<quest>.zinfo' beside it, the whole section as writezinfo writes it (id, version, cversion,
// size, names), unpacked. Upstream's loaders read that file when it is there, as they read the
// section (without looking at its id or size, or past the names), and give the quest no names
// when it isn't. Their save keeps the flag and writes the names to '<saved quest>.zinfo' instead
// of the quest file; zq_save_newer does the same with what write_zinfo writes, and the save in
// the quest's own version writes the file back as it was opened (zq_writers.cpp).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zq_zinfo.h"
#include "zqn_255.h"

namespace
{

const dword ID_ZINFO_ = ZC_ID('Z', 'I', 'N', 'F');

// how many of each kind 3.0 holds (2.55.17: 183 combo types, and no enemy classes or level items)
const int ITYPES = 512, CTYPES = 184, MFLAGS = 256, COUNTERS = 107, WEAPONS = 146, ETYPES = 73, LITEMS = 16;

struct NameHelp
{
    std::string name, help;
};

// Every name upstream's zinfo holds (empty: none, so upstream's own name shows)
struct Names
{
    std::vector<NameHelp> ic, ct, mf, li;
    std::vector<std::string> ctr, wpn, et, liabbr;

    Names() : ic(ITYPES), ct(CTYPES), mf(MFLAGS), li(LITEMS), ctr(COUNTERS), wpn(WEAPONS), et(ETYPES), liabbr(LITEMS) {}
};

struct OldFlag
{
    int flag;
    const char *name;
};

// zinfo.cpp's old_mapflag_strings
const OldFlag g_old_flags[] =
{
    { 120, "Pit or Hole (Scripted)" }, { 121, "Pit or Hole, Fall Down Floor (Scripted)" },
    { 122, "Fire or Lava (Scripted)" }, { 123, "Ice (Scripted)" }, { 124, "Ice, Damaging (Scripted)" },
    { 125, "Damage-1 (Scripted)" }, { 126, "Damage-2 (Scripted)" }, { 127, "Damage-4 (Scripted)" },
    { 128, "Damage-8 (Scripted)" }, { 129, "Damage-16 (Scripted)" }, { 130, "Damage-32 (Scripted)" },
    { 144, "Dig Spot (Scripted)" }, { 145, "Dig Spot, Next (Scripted)" }, { 146, "Dig Spot, Special Item (Scripted)" },
    { 147, "Pot, Slashable (Scripted)" }, { 148, "Pot, Liftable (Scripted)" }, { 149, "Pot, Slash or Lift (Scripted)" },
    { 150, "Rock, Lift Normal (Scripted)" }, { 151, "Rock, Lift Heavy (Scripted)" }, { 152, "Dropset Item (Scripted)" },
    { 153, "Special Item (Scripted)" }, { 154, "Drop Key (Scripted)" }, { 155, "Drop level-Specific Key (Scripted)" },
    { 156, "Drop Compass (Scripted)" }, { 157, "Drop Map (Scripted)" }, { 158, "Drop Bosskey (Scripted)" },
    { 159, "Spawn NPC (Scripted)" }, { 160, "SwitchHook Spot (Scripted)" }
};

const char *const g_scripted_help = "These flags have no built-in effect, but can be given special significance with ZASM or ZScript.";

// a name as the reader stores it and strlen sees it
std::string c_str_of(const std::vector<byte> &b)
{
    size_t n = 0;

    while(n < b.size() && b[n])
        ++n;

    return std::string((const char *)(b.empty() ? NULL : &b[0]), n);
}

std::string read_cstr(Zqn255In &r)
{
    return c_str_of(r.bytes(r.u8()));
}

std::string read_wstr(Zqn255In &r)
{
    return c_str_of(r.bytes(r.u16()));
}

void put_name(Zqn255Out &o, const std::string &s)
{
    size_t n = s.size() < 255 ? s.size() : 255;
    o.c((int64_t)n);
    o.put(s.data(), n);
}

void put_help(Zqn255Out &o, const std::string &s)
{
    size_t n = s.size() & 0xFFFF;   // p_iputw of the length
    o.w((int64_t)n);
    o.put(s.data(), n);
}

// readzinfo: the names a version v section holds, from r's position on (r.bad if they run past
// the end).
void read_names(Zqn255In &r, int v, Names &n)
{
    int count = v > 0 ? r.u16() : 512;

    if(count > ITYPES)
        r.bad = true;

    for(int q = 0; q < count && !r.bad; ++q)
    {
        n.ic[q].name = read_cstr(r);
        n.ic[q].help = read_wstr(r);
    }

    if(v > 0)
    {
        count = r.u16();

        for(int q = 0; q < count && !r.bad; ++q)
        {
            std::string a = read_cstr(r), b = read_wstr(r);

            if(q < CTYPES)
                n.ct[q].name = a, n.ct[q].help = b;
        }

        count = r.u16();

        for(int q = 0; q < count && !r.bad; ++q)
        {
            std::string a = read_cstr(r), b = read_wstr(r);

            if(q < MFLAGS)
                n.mf[q].name = a, n.mf[q].help = b;
        }
    }
    else
    {
        for(size_t k = 0; k < sizeof(g_old_flags) / sizeof(g_old_flags[0]); ++k)
        {
            n.mf[g_old_flags[k].flag].name = g_old_flags[k].name;
            n.mf[g_old_flags[k].flag].help = g_scripted_help;
        }
    }

    if(v > 1)
    {
        count = r.u16();

        for(int q = 0; q < count && !r.bad; ++q)
        {
            std::string a = read_cstr(r);

            if(q < COUNTERS)
                n.ctr[q] = a;
        }
    }

    if(v > 2)
    {
        count = r.u16();

        for(int q = 0; q < count && !r.bad; ++q)
        {
            std::string a = read_cstr(r);

            if(q < WEAPONS)
                n.wpn[q] = a;
        }
    }

    if(v > 3)
    {
        count = r.u16();

        for(int q = 0; q < count && !r.bad; ++q)
        {
            std::string a = read_cstr(r);

            if(q < ETYPES)
                n.et[q] = a;
        }
    }

    if(v > 4)
    {
        count = r.u8();

        for(int q = 0; q < count && !r.bad; ++q)
        {
            std::string a = read_cstr(r), b = read_cstr(r), c = read_wstr(r);

            if(q < LITEMS)
                n.li[q].name = a, n.liabbr[q] = b, n.li[q].help = c;
        }
    }
}

// The names in a side file (or a section with its id in front): the version after the id, then
// the names after the cversion and size.
bool read_whole_section(const std::vector<byte> &b, Names &n)
{
    Zqn255In r(b);
    r.skip(4);
    const int v = r.u16();
    r.skip(2 + 4);
    read_names(r, v, n);
    return !r.bad;
}

// The open quest's names
struct QuestNames
{
    bool external;              // its header points to a side file
    std::string side_path;      // that file's name
    bool side_found;            // it was there when the quest was opened
    std::vector<byte> side;     // its bytes then
    bool have;                  // names were read, from the side file or the quest's section
    Names names;

    QuestNames() : external(false), side_found(false), have(false) {}
};

QuestNames g_quest;

// The header's external_zinfo byte (header version 6 on), just after the build's time zone: at
// 5645 in the section's data from version 8 on, 5631 before (the quest version and minimum
// version grew from 9 bytes to 16 in version 8). The header part starts with the 31-byte id
// string, then the section's id, version, cversion and size.
bool header_external()
{
    const ZqQuestPart *h = zq_find_part(ID_HEADER);
    byte head[31 + 12];

    if(!h || h->length < (int64_t)sizeof(head) || !zq_part_read(*h, 0, head, sizeof(head)) || memcmp(head + 31, "HDR ", 4))
        return false;

    const int v = head[35] | (head[36] << 8);

    if(v < 6)
        return false;

    const int64_t at = (int64_t)sizeof(head) + (v >= 8 ? 5645 : 5631);
    byte b = 0;
    return at < h->length && zq_part_read(*h, at, &b, 1) && b != 0;
}

bool read_file(const std::string &path, std::vector<byte> &out)
{
    FILE *f = fopen(path.c_str(), "rb");

    if(!f)
        return false;

    out.clear();
    byte buf[4096];
    size_t n;

    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.insert(out.end(), buf, buf + n);

    fclose(f);
    return true;
}

int write_zinfo(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx))
        return ZQW_UNSUPPORTED;

    const bool t30 = ctx.target == ZQ_NEWER_30;
    const int ctypes = t30 ? 184 : 183;
    Names n;
    std::vector<byte> store_data;
    int store_version = 0;

    // A quest that keeps its names beside it: the side file, or no names when there was none
    // (upstream reads only that file then).
    if(g_quest.external)
    {
        if(g_quest.side_found && !read_whole_section(g_quest.side, n))
        {
            zqw_last_error = "its editor names (" + g_quest.side_path + ") aren't laid out as expected";
            return 2;
        }
    }
    // the ZInfo editor's store (its changes included) while it holds the quest's names
    else if(zq_zinfo_store_bytes(store_data, store_version))
    {
        Zqn255In r(store_data);
        read_names(r, store_version, n);

        if(!r.at_end())
        {
            zqw_last_error = "its editor names aren't laid out as expected";
            return 2;
        }
    }
    else if(ctx.src_part)
    {
        Zqn255Section s;

        if(!zqn255_section(ctx, s))
            return 2;

        Zqn255In r(s.data);
        read_names(r, s.version, n);

        if(!r.at_end())
        {
            zqw_last_error = "its editor names aren't laid out as expected";
            return 2;
        }
    }

    Zqn255Out o;
    o.w(ITYPES);

    for(int q = 0; q < ITYPES; ++q)
        put_name(o, n.ic[q].name), put_help(o, n.ic[q].help);

    o.w(ctypes);

    for(int q = 0; q < ctypes; ++q)
        put_name(o, n.ct[q].name), put_help(o, n.ct[q].help);

    o.w(MFLAGS);

    for(int q = 0; q < MFLAGS; ++q)
        put_name(o, n.mf[q].name), put_help(o, n.mf[q].help);

    o.w(COUNTERS);

    for(int q = 0; q < COUNTERS; ++q)
        put_name(o, n.ctr[q]);

    o.w(WEAPONS);

    for(int q = 0; q < WEAPONS; ++q)
        put_name(o, n.wpn[q]);

    if(t30)
    {
        o.w(ETYPES);

        for(int q = 0; q < ETYPES; ++q)
            put_name(o, n.et[q]);

        o.c(LITEMS);

        for(int q = 0; q < LITEMS; ++q)
            put_name(o, n.li[q].name), put_name(o, n.liabbr[q]), put_help(o, n.li[q].help);
    }

    std::vector<byte> none;
    return zqn255_emit(f, ID_ZINFO_, t30 ? 5 : 3, t30 ? 5 : 0, none, o.b);
}

ZqNewerRegistration g_reg_zinfo(ID_ZINFO_, write_zinfo, "2.55 editor names");

} // namespace

// ---- the open quest's names (zq_zinfo.h) ----------------------------------------------------

std::string zq_zinfo_path(const char *quest_file)
{
    // the extension: after the last '.' that follows the last path or drive separator
    std::string s = quest_file ? quest_file : "";
    const size_t at = s.find_last_of("./\\:");

    if(at != std::string::npos && s[at] == '.')
        s.erase(at);

    return s + ".zinfo";
}

void zq_zinfo_loaded(const char *filename)
{
    g_quest = QuestNames();

    // only 2.55 and 3.0 quests have names (and the header byte); a 2.53 quest has neither
    if(!filename || zq_quest_file.parts.empty() || zq_quest_file.zelda_version < 0x255)
        return;

    g_quest.external = header_external();

    if(g_quest.external)
    {
        g_quest.side_path = zq_zinfo_path(filename);
        g_quest.side_found = read_file(g_quest.side_path, g_quest.side);
        g_quest.have = g_quest.side_found && read_whole_section(g_quest.side, g_quest.names);
        return;
    }

    const ZqQuestPart *p = zq_find_part(ID_ZINFO_);

    if(!p || p->length < 8)
        return;

    // the part as loaded: everything after the section's id
    std::vector<byte> b((size_t)p->length + 4);
    b[0] = 'Z', b[1] = 'I', b[2] = 'N', b[3] = 'F';

    if(zq_part_read(*p, 0, &b[4], (size_t)p->length))
        g_quest.have = read_whole_section(b, g_quest.names);
}

bool zq_zinfo_external()
{
    return g_quest.external;
}

bool zq_zinfo_side(std::vector<byte> *bytes, int *version)
{
    if(!g_quest.external || !g_quest.side_found)
        return false;

    if(bytes)
        *bytes = g_quest.side;

    if(version)
        *version = g_quest.side.size() >= 6 ? (g_quest.side[4] | (g_quest.side[5] << 8)) : -1;

    return true;
}

const char *zq_quest_name(int kind, int index)
{
    // the ZInfo editor's store, while it holds the quest's names (with the editor's changes)
    const char *from_store = NULL;

    if(index >= 0 && zq_zinfo_store_answers(kind, index, &from_store))
        return from_store;

    if(!g_quest.have || index < 0)
        return NULL;

    const Names &n = g_quest.names;
    const std::string *s = NULL;

    switch(kind)
    {
    case ZQN_ITEMCLASS: if(index < ITYPES) s = &n.ic[index].name; break;
    case ZQN_ITEMCLASS_HELP: if(index < ITYPES) s = &n.ic[index].help; break;
    case ZQN_COMBOTYPE: if(index < CTYPES) s = &n.ct[index].name; break;
    case ZQN_COMBOTYPE_HELP: if(index < CTYPES) s = &n.ct[index].help; break;
    case ZQN_MAPFLAG: if(index < MFLAGS) s = &n.mf[index].name; break;
    case ZQN_MAPFLAG_HELP: if(index < MFLAGS) s = &n.mf[index].help; break;
    case ZQN_COUNTER: if(index < COUNTERS) s = &n.ctr[index]; break;
    case ZQN_WEAPON: if(index < WEAPONS) s = &n.wpn[index]; break;
    case ZQN_ENEMYCLASS: if(index < ETYPES) s = &n.et[index]; break;
    case ZQN_LEVELITEM: if(index < LITEMS) s = &n.li[index].name; break;
    case ZQN_LEVELITEM_ABBR: if(index < LITEMS) s = &n.liabbr[index]; break;
    case ZQN_LEVELITEM_HELP: if(index < LITEMS) s = &n.li[index].help; break;
    }

    return s && !s->empty() ? s->c_str() : NULL;
}

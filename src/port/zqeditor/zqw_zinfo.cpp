// The editor names of 2.55 and 3.0 Nightly quests (the ZINF section): read into ZQuest's store
// after a load, written back in the quest's own ZINF version when the ZInfo editor
// (zq_zinfo_editor.cpp) changed them, and handed to ZQuest's lists (zq_zinfo_name).
//
// readzinfo (upstream's zinfo.cpp, the same in 2.55.17 and 3.0): item class names and help
// (512 of them in version 0, then a count), from version 1 combo type names and help and map
// flag names and help, from 2 counter names, from 3 weapon names, from 4 enemy class names,
// from 5 level item names, abbreviations and help. Each name is a byte count and that many
// characters, each help a word count and that many. The store keeps every entry as the file
// has it, in the file's count, so an unedited entry writes back byte for byte.
//
// A quest whose header keeps its names in a .zinfo file beside it has no ZINF section; its
// names aren't read here, and the ZInfo editor doesn't offer to change them (that file is
// outside the quest's own sections).

#include "precompiled.h" //always first

#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zq_q255_store.h"
#include "zq_zinfo.h"

extern zquestheader header;

namespace
{

const dword ID_ZINFO_ = ZC_ID('Z', 'I', 'N', 'F');

struct In
{
    const std::vector<byte> &d;
    size_t pos;
    bool bad;

    In(const std::vector<byte> &data, size_t at) : d(data), pos(at), bad(false) {}

    unsigned u8()
    {
        if(bad || pos + 1 > d.size()) { bad = true; return 0; }
        return d[pos++];
    }
    unsigned u16()
    {
        if(bad || pos + 2 > d.size()) { bad = true; return 0; }
        unsigned v = d[pos] | (d[pos + 1] << 8);
        pos += 2;
        return v;
    }
    unsigned u32()
    {
        if(bad || pos + 4 > d.size()) { bad = true; return 0; }
        unsigned v = (unsigned)d[pos] | ((unsigned)d[pos + 1] << 8) | ((unsigned)d[pos + 2] << 16) | ((unsigned)d[pos + 3] << 24);
        pos += 4;
        return v;
    }
    std::string str(size_t n)
    {
        if(bad || n > d.size() - pos) { bad = true; return std::string(); }
        std::string s(n ? (const char *)&d[pos] : "", n);
        pos += n;
        return s;
    }
    std::string cstr() { return str(u8()); }
    std::string wstr() { return str(u16()); }
};

void put8(std::vector<byte> &o, unsigned v) { o.push_back((byte)v); }
void put16(std::vector<byte> &o, unsigned v) { o.push_back((byte)v); o.push_back((byte)(v >> 8)); }
void put32(std::vector<byte> &o, unsigned v) { put16(o, v & 0xFFFF); put16(o, v >> 16); }

// a name as writezinfo writes it: at most 255 characters after a byte count
void put_cstr(std::vector<byte> &o, const std::string &s)
{
    const size_t n = s.size() < 255 ? s.size() : 255;
    put8(o, (unsigned)n);
    o.insert(o.end(), s.begin(), s.begin() + n);
}

void put_wstr(std::vector<byte> &o, const std::string &s)
{
    const size_t n = s.size() < 65535 ? s.size() : 65535;
    put16(o, (unsigned)n);
    o.insert(o.end(), s.begin(), s.begin() + n);
}

const ZqQuestPart *zinfo_part()
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == ID_ZINFO_)
            return &zq_quest_file.parts[i];

    return NULL;
}

// The section's data (after its size) taken apart into z. False if it isn't laid out as
// readzinfo reads it.
bool parse(const std::vector<byte> &all, Zq255Store::Zinfo &z)
{
    In r(all, 0);
    const int v = (int)r.u16();
    r.u16();            // cversion
    const unsigned size = r.u32();

    if(r.bad || v > 5 || (size_t)size != all.size() - r.pos)
        return false;

    z = Zq255Store::Zinfo();
    z.present = true;
    z.version = v;

    // item classes, combo types and map flags: a name and a help each
    for(int kind = ZQZ_ITEMCLASS; kind <= ZQZ_MAPFLAG && !r.bad; ++kind)
    {
        if(kind > ZQZ_ITEMCLASS && v < 1)
            break;

        const unsigned n = (kind == ZQZ_ITEMCLASS && v == 0) ? 512 : r.u16();

        for(unsigned q = 0; q < n && !r.bad; ++q)
        {
            Zq255Store::ZinfoEntry e;
            e.name = r.cstr();
            e.help = r.wstr();
            z.kinds[kind].push_back(e);
        }
    }

    // counters (2), weapons (3), enemy classes (4): names only
    for(int kind = ZQZ_COUNTER; kind <= ZQZ_ENEMYCLASS && !r.bad; ++kind)
    {
        if(v < kind - ZQZ_COUNTER + 2)
            break;

        const unsigned n = r.u16();

        for(unsigned q = 0; q < n && !r.bad; ++q)
        {
            Zq255Store::ZinfoEntry e;
            e.name = r.cstr();
            z.kinds[kind].push_back(e);
        }
    }

    // level items (5): name, abbreviation, help
    if(v > 4 && !r.bad)
    {
        const unsigned n = r.u8();

        for(unsigned q = 0; q < n && !r.bad; ++q)
        {
            Zq255Store::ZinfoEntry e;
            e.name = r.cstr();
            e.abbr = r.cstr();
            e.help = r.wstr();
            z.kinds[ZQZ_LEVELITEM].push_back(e);
        }
    }

    return !r.bad && r.pos == all.size();
}

// The section's data from the store, as writezinfo writes the version it has.
void encode(const Zq255Store::Zinfo &z, std::vector<byte> &o)
{
    const int v = z.version;

    for(int kind = ZQZ_ITEMCLASS; kind <= ZQZ_MAPFLAG; ++kind)
    {
        if(kind > ZQZ_ITEMCLASS && v < 1)
            break;

        const std::vector<Zq255Store::ZinfoEntry> &k = z.kinds[kind];

        if(!(kind == ZQZ_ITEMCLASS && v == 0))
            put16(o, (unsigned)k.size());

        for(size_t q = 0; q < k.size(); ++q)
        {
            put_cstr(o, k[q].name);
            put_wstr(o, k[q].help);
        }
    }

    for(int kind = ZQZ_COUNTER; kind <= ZQZ_ENEMYCLASS; ++kind)
    {
        if(v < kind - ZQZ_COUNTER + 2)
            break;

        const std::vector<Zq255Store::ZinfoEntry> &k = z.kinds[kind];
        put16(o, (unsigned)k.size());

        for(size_t q = 0; q < k.size(); ++q)
            put_cstr(o, k[q].name);
    }

    if(v > 4)
    {
        const std::vector<Zq255Store::ZinfoEntry> &k = z.kinds[ZQZ_LEVELITEM];
        put8(o, (unsigned)k.size());

        for(size_t q = 0; q < k.size(); ++q)
        {
            put_cstr(o, k[q].name);
            put_cstr(o, k[q].abbr);
            put_wstr(o, k[q].help);
        }
    }
}

// After a load (and an own-format save): the quest's names, from its ZINF part.
void load_zinfo()
{
    zq255.zinfo = Zq255Store::Zinfo();
    const ZqQuestPart *p = zinfo_part();

    if(!p || header.zelda_version < 0x255)
        return;

    std::vector<byte> all((size_t)p->length);

    if(p->length && !zq_part_read(*p, 0, &all[0], all.size()))
        return;

    Zq255Store::Zinfo z;

    if(parse(all, z))
        zq255.zinfo = z;
    else
        al_trace("ZInfo: the quest's ZINF section isn't laid out as expected; its names aren't shown\n");
}

ZqLoadHookRegistration g_hook(load_zinfo);

void hash_zinfo(uint64_t *h)
{
    const Zq255Store::Zinfo &z = zq255.zinfo;

    if(!z.present)
        return;

    std::vector<byte> o;
    put16(o, (unsigned)z.version);
    encode(z, o);
    zq_hash_add(h, &o[0], o.size());
}

ZqStoreHashRegistration g_hash(ID_ZINFO_, hash_zinfo);

// The ZINF part in the quest's own version, from the store.
int write_zinfo_own(PACKFILE *f, const ZqPartContext &ctx)
{
    if(ctx.zelda_version < 0x255 || ctx.version > 5)
        return ZQW_UNSUPPORTED;

    const Zq255Store::Zinfo &z = zq255.zinfo;

    if(!z.present || z.version != ctx.version)
    {
        zqw_last_error = "the editor names (ZInfo) couldn't be read from the quest, so they can't be written back";
        return 2;
    }

    std::vector<byte> data, o;
    encode(z, data);
    put16(o, ctx.version);
    put16(o, ctx.cversion);
    put32(o, (unsigned)data.size());
    o.insert(o.end(), data.begin(), data.end());

    if(pack_mputl((long)ID_ZINFO_, f) == EOF || pack_fwrite(&o[0], (long)o.size(), f) != (long)o.size())
        return 1;

    return ZQW_OK;
}

ZqWriterRegistration g_reg(ID_ZINFO_, write_zinfo_own, "editor names (ZInfo) v0-v5, 2.55 and 3.0");

int g_defaults_only = 0;    // zq_zinfo_defaults_only: the name lists give upstream's own names

// readzinfo in upstream's ZQuest (zinfo.cpp, 2.55.17 and 3.0): a quest whose ZINF is version 0
// (no map flag names) and whose header is 0x255 gets the names 2.55 gave its scripted map flags,
// old_mapflag_strings, with one help text. The editor shows them; the section stays as it is.
// (zqn_255_zinfo.cpp, which writes them into a Save As 3.0, has the same table.)
struct OldFlag
{
    int flag;
    const char *name;
};

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

// the old name of map flag f in such a quest, or NULL
const char *old_flag_name(int f)
{
    if(g_defaults_only || !zq255.zinfo.present || zq255.zinfo.version != 0 || header.zelda_version != 0x255)
        return NULL;

    for(size_t i = 0; i < sizeof(g_old_flags) / sizeof(g_old_flags[0]); ++i)
        if(g_old_flags[i].flag == f)
            return g_old_flags[i].name;

    return NULL;
}

const Zq255Store::ZinfoEntry *entry(int kind, int index)
{
    if(kind < 0 || kind >= ZQZ_KINDS || index < 0 || g_defaults_only || !zq255.zinfo.present || !zq255_quest_is_newer())
        return NULL;

    const std::vector<Zq255Store::ZinfoEntry> &k = zq255.zinfo.kinds[kind];
    return (size_t)index < k.size() ? &k[index] : NULL;
}

// the store holds the open quest's names (its ZINF section); otherwise the names read at load
// (zqn_255_zinfo.cpp: a side .zinfo file) are the quest's
bool store_holds()
{
    return zq255.zinfo.present && zq255_quest_is_newer();
}

// zqn_255_zinfo.cpp's name (or help) of kind for index, for a quest the store holds no names of
const char *loaded_name(int kind, int index, bool help)
{
    if(g_defaults_only)
        return NULL;

    switch(kind)
    {
    case ZQZ_ITEMCLASS: return zq_quest_name(help ? ZQN_ITEMCLASS_HELP : ZQN_ITEMCLASS, index);
    case ZQZ_COMBOTYPE: return zq_quest_name(help ? ZQN_COMBOTYPE_HELP : ZQN_COMBOTYPE, index);
    case ZQZ_MAPFLAG: return zq_quest_name(help ? ZQN_MAPFLAG_HELP : ZQN_MAPFLAG, index);
    case ZQZ_LEVELITEM: return zq_quest_name(help ? ZQN_LEVELITEM_HELP : ZQN_LEVELITEM, index);
    case ZQZ_COUNTER: return help ? NULL : zq_quest_name(ZQN_COUNTER, index);
    case ZQZ_WEAPON: return help ? NULL : zq_quest_name(ZQN_WEAPON, index);
    case ZQZ_ENEMYCLASS: return help ? NULL : zq_quest_name(ZQN_ENEMYCLASS, index);
    }

    return NULL;
}

// upstream's valid_str: set and not empty (the reader keeps characters up to the first NUL)
const char *valid(const std::string &s)
{
    return !s.empty() && s[0] ? s.c_str() : NULL;
}

} // namespace

// While on, zq_zinfo_name and friends give NULL, so the name lists give upstream's defaults
// (the ZInfo editor shows them beside the quest's own). Calls nest.
void zq_zinfo_defaults_only(bool on)
{
    g_defaults_only += on ? 1 : -1;
}

const char *zq_zinfo_name(int kind, int index)
{
    if(kind == ZQZ_MAPFLAG)
        if(const char *old = old_flag_name(index))
            return old;

    if(!store_holds())
        return loaded_name(kind, index, false);

    const Zq255Store::ZinfoEntry *e = entry(kind, index);
    return e ? valid(e->name) : NULL;
}

const char *zq_zinfo_help(int kind, int index)
{
    if(kind == ZQZ_MAPFLAG && old_flag_name(index))
        return g_scripted_help;

    if(!store_holds())
        return loaded_name(kind, index, true);

    const Zq255Store::ZinfoEntry *e = entry(kind, index);
    return e ? valid(e->help) : NULL;
}

// True when map flag f has a name ZQuest gives it in this quest (zq_zinfo_name) that the ZInfo
// editor can't change, because the quest's ZINF version has no map flags.
bool zq_zinfo_old_mapflag(int f)
{
    return old_flag_name(f) != NULL;
}

const char *zq_zinfo_abbr(int index)
{
    if(!store_holds())
        return g_defaults_only ? NULL : zq_quest_name(ZQN_LEVELITEM_ABBR, index);

    const Zq255Store::ZinfoEntry *e = entry(ZQZ_LEVELITEM, index);
    return e ? valid(e->abbr) : NULL;
}

// -qst-dump-record q zinfo 0 out (zq_tests.cpp): every entry the section holds that isn't
// empty, by kind.
void zq_zinfo_dump(FILE *f)
{
    static const char *kinds[ZQZ_KINDS] = { "itemclass", "combotype", "mapflag", "counter", "weapon", "enemyclass", "levelitem" };
    const Zq255Store::Zinfo &z = zq255.zinfo;
    fprintf(f, "ZINF: %s, version %d\n", z.present ? "present" : "absent", z.version);

    for(int k = 0; k < ZQZ_KINDS; ++k)
    {
        fprintf(f, "%s: %d entries\n", kinds[k], (int)z.kinds[k].size());

        for(size_t q = 0; q < z.kinds[k].size(); ++q)
        {
            const Zq255Store::ZinfoEntry &e = z.kinds[k][q];

            if(e.name.empty() && e.help.empty() && e.abbr.empty())
                continue;

            fprintf(f, "  %s %d: name \"%s\"", kinds[k], (int)q, e.name.c_str());

            if(!e.abbr.empty())
                fprintf(f, " abbr \"%s\"", e.abbr.c_str());

            if(!e.help.empty())
                fprintf(f, " help \"%s\"", e.help.c_str());

            fprintf(f, "\n");
        }
    }
}

// zq_quest_name's hook (zq_zinfo.h): the store answers while it holds the quest's names, so the
// lists show the ZInfo editor's changes.
bool zq_zinfo_store_answers(int kind, int index, const char **out)
{
    if(g_defaults_only)
    {
        *out = NULL;
        return true;
    }

    if(!store_holds())
        return false;

    switch(kind)
    {
    case ZQN_ITEMCLASS: *out = zq_zinfo_name(ZQZ_ITEMCLASS, index); break;
    case ZQN_ITEMCLASS_HELP: *out = zq_zinfo_help(ZQZ_ITEMCLASS, index); break;
    case ZQN_COMBOTYPE: *out = zq_zinfo_name(ZQZ_COMBOTYPE, index); break;
    case ZQN_COMBOTYPE_HELP: *out = zq_zinfo_help(ZQZ_COMBOTYPE, index); break;
    case ZQN_MAPFLAG: *out = zq_zinfo_name(ZQZ_MAPFLAG, index); break;
    case ZQN_MAPFLAG_HELP: *out = zq_zinfo_help(ZQZ_MAPFLAG, index); break;
    case ZQN_COUNTER: *out = zq_zinfo_name(ZQZ_COUNTER, index); break;
    case ZQN_WEAPON: *out = zq_zinfo_name(ZQZ_WEAPON, index); break;
    case ZQN_ENEMYCLASS: *out = zq_zinfo_name(ZQZ_ENEMYCLASS, index); break;
    case ZQN_LEVELITEM: *out = zq_zinfo_name(ZQZ_LEVELITEM, index); break;
    case ZQN_LEVELITEM_ABBR: *out = zq_zinfo_abbr(index); break;
    case ZQN_LEVELITEM_HELP: *out = zq_zinfo_help(ZQZ_LEVELITEM, index); break;
    default: *out = NULL; break;
    }

    return true;
}

bool zq_zinfo_store_bytes(std::vector<byte> &data, int &version)
{
    if(!store_holds())
        return false;

    data.clear();
    encode(zq255.zinfo, data);
    version = zq255.zinfo.version;
    return true;
}

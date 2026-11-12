// Saving a quest made in 2.55 as 2.55.17 or 3.0 Nightly (zqn_255.h): the editor names (ZINF).
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
// A quest whose header keeps its names in a .zinfo file beside it (none in the pack has that
// file) is read with no names; this save writes those empty names into the quest itself rather
// than beside it, which reads the same.

#include "precompiled.h" //always first

#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_writers.h"
#include "zqn_255.h"

namespace
{

const dword ID_ZINFO_ = ZC_ID('Z', 'I', 'N', 'F');

struct NameHelp
{
    std::string name, help;
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

int write_zinfo(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx))
        return ZQW_UNSUPPORTED;

    const bool t30 = ctx.target == ZQ_NEWER_30;
    const int ITYPES = 512, CTYPES = t30 ? 184 : 183, MFLAGS = 256, COUNTERS = 107, WEAPONS = 146, ETYPES = 73, LITEMS = 16;
    std::vector<NameHelp> ic(ITYPES), ct(CTYPES), mf(MFLAGS), li(LITEMS);
    std::vector<std::string> ctr(COUNTERS), wpn(WEAPONS), et(ETYPES), liabbr(LITEMS);
    Zqn255Section s;
    s.version = 0;

    if(ctx.src_part)
    {
        if(!zqn255_section(ctx, s))
            return 2;

        const int v = s.version;
        Zqn255In r(s.data);
        int n = v > 0 ? r.u16() : 512;

        if(n > ITYPES)
            r.bad = true;

        for(int q = 0; q < n && !r.bad; ++q)
        {
            ic[q].name = read_cstr(r);
            ic[q].help = read_wstr(r);
        }

        if(v > 0)
        {
            n = r.u16();

            for(int q = 0; q < n && !r.bad; ++q)
            {
                std::string a = read_cstr(r), b = read_wstr(r);

                if(q < CTYPES)
                    ct[q].name = a, ct[q].help = b;
            }

            n = r.u16();

            for(int q = 0; q < n && !r.bad; ++q)
            {
                std::string a = read_cstr(r), b = read_wstr(r);

                if(q < MFLAGS)
                    mf[q].name = a, mf[q].help = b;
            }
        }
        else
        {
            for(size_t k = 0; k < sizeof(g_old_flags) / sizeof(g_old_flags[0]); ++k)
            {
                mf[g_old_flags[k].flag].name = g_old_flags[k].name;
                mf[g_old_flags[k].flag].help = g_scripted_help;
            }
        }

        if(v > 1)
        {
            n = r.u16();

            for(int q = 0; q < n && !r.bad; ++q)
            {
                std::string a = read_cstr(r);

                if(q < COUNTERS)
                    ctr[q] = a;
            }
        }

        if(v > 2)
        {
            n = r.u16();

            for(int q = 0; q < n && !r.bad; ++q)
            {
                std::string a = read_cstr(r);

                if(q < WEAPONS)
                    wpn[q] = a;
            }
        }

        if(v > 3)
        {
            n = r.u16();

            for(int q = 0; q < n && !r.bad; ++q)
            {
                std::string a = read_cstr(r);

                if(q < ETYPES)
                    et[q] = a;
            }
        }

        if(v > 4)
        {
            n = r.u8();

            for(int q = 0; q < n && !r.bad; ++q)
            {
                std::string a = read_cstr(r), b = read_cstr(r), c = read_wstr(r);

                if(q < LITEMS)
                    li[q].name = a, liabbr[q] = b, li[q].help = c;
            }
        }

        if(!r.at_end())
        {
            zqw_last_error = "its editor names aren't laid out as expected";
            return 2;
        }
    }

    Zqn255Out o;
    o.w(ITYPES);

    for(int q = 0; q < ITYPES; ++q)
        put_name(o, ic[q].name), put_help(o, ic[q].help);

    o.w(CTYPES);

    for(int q = 0; q < CTYPES; ++q)
        put_name(o, ct[q].name), put_help(o, ct[q].help);

    o.w(MFLAGS);

    for(int q = 0; q < MFLAGS; ++q)
        put_name(o, mf[q].name), put_help(o, mf[q].help);

    o.w(COUNTERS);

    for(int q = 0; q < COUNTERS; ++q)
        put_name(o, ctr[q]);

    o.w(WEAPONS);

    for(int q = 0; q < WEAPONS; ++q)
        put_name(o, wpn[q]);

    if(t30)
    {
        o.w(ETYPES);

        for(int q = 0; q < ETYPES; ++q)
            put_name(o, et[q]);

        o.c(LITEMS);

        for(int q = 0; q < LITEMS; ++q)
            put_name(o, li[q].name), put_name(o, liabbr[q]), put_help(o, li[q].help);
    }

    std::vector<byte> none;
    return zqn255_emit(f, ID_ZINFO_, t30 ? 5 : 3, t30 ? 5 : 0, none, o.b);
}

ZqNewerRegistration g_reg_zinfo(ID_ZINFO_, write_zinfo, "2.55 editor names");

} // namespace

// Newer-version writers (zq_writers.h) for the header, the editor names (ZINF), the rules, door
// combo sets, cheats, item drop sets, editor favorites and, for 3.0 only, the advanced music
// (AMUS): a quest made in 2.53.1 or older, saved as ZC 2.55.17 or 3.0 Nightly as upstream's
// own ZQuest saves it after opening the original file. Ported from 2.55.17's and 3.0's
// zq/zq_class.cpp (writeheader, writerules, writedoorcombosets, writecheats,
// writeitemdropsets, writefavorites, write_adv_music) and zinfo.cpp (writezinfo); what each
// field holds is what their loaders make of the original quest.
//
// The header's fields that say which version the file is (zelda_version and build, the version
// numbers, the release state, 3.0's nightly flag) are the target's own, so 2.55.17 and 3.0
// treat the file as one of theirs: compareVer and the compatibility rules go by them. The
// fields that only name the program that saved the file are ours, and neither version reads
// them for anything but display and logs (readheader, print_quest_metadata, the ZC console,
// the quest browser): the build date and time, the build's time zone, 2.55's developer
// signoff, the compiler, and 3.0's version string, which is the target version with this
// program named in its build metadata ("3.0.0+ZQuest-Vulkan-<this build's version>"). Where the target
// writes a fixed value whatever its build (3.0's compiler name, the product name, 2.55's
// module), the file carries that value.

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqn_doors.h"
#include "zqn_music.h"
#include "zqn_rules.h"
#include "zqn_255.h"

extern zquestheader header;
extern DoorComboSet *DoorComboSets;
extern word door_combo_set_count;
extern ZCHEATS zcheats;
extern item_drop_object item_drop_sets[MAXITEMDROPSETS];
extern int favorite_combos[MAXFAVORITECOMBOS];
extern std::vector<mapscr> TheMaps;
extern word map_count;
int count_item_drop_sets();     // zquest.cpp

namespace zqnb
{

namespace
{

enum { ZQN_BAD_PART = 2, ZQN_WRITE_FAILED = 3, ZQN_REFUSED = 4 };

const dword ID_ZINFO_ = ZC_ID('Z', 'I', 'N', 'F');
const dword ID_ADVMUSIC_ = ZC_ID('A', 'M', 'U', 'S');

// ---- bytes out ----------------------------------------------------------------------------

struct Out
{
    std::vector<byte> b;

    void c(int v) { b.push_back((byte)v); }
    void w(int v) { c(v); c(v >> 8); }
    void l(dword v) { c((int)v); c((int)(v >> 8)); c((int)(v >> 16)); c((int)(v >> 24)); }
    void put(const void *p, size_t n) { const byte *q = (const byte *)p; b.insert(b.end(), q, q + n); }
    void zeros(size_t n) { b.insert(b.end(), n, (byte)0); }

    // a fixed-size text field: the string, then zeros to n bytes
    void text(const char *s, size_t n)
    {
        size_t k = strlen(s);

        if(k > n)
            k = n;

        put(s, k);
        zeros(n - k);
    }

    // a C string of at most n characters from src, then zeros to a field of size bytes
    void cfield(const char *src, size_t n, size_t size)
    {
        size_t k = 0;

        while(k < n && src[k])
            ++k;

        put(src, k);
        zeros(size - k);
    }

    // p_putcstr: a byte length (at most 255), then the characters
    void cstr(const std::string &s)
    {
        size_t n = s.size() < 255 ? s.size() : 255;
        c((int)n);
        put(s.data(), n);
    }

    // p_putwstr: a word length, then the characters
    void wstr(const std::string &s)
    {
        size_t n = s.size() < 65535 ? s.size() : 65535;
        w((int)n);
        put(s.data(), n);
    }
};

bool target_ok(const ZqNewerContext &ctx)
{
    return (ctx.target == ZQ_NEWER_255 || ctx.target == ZQ_NEWER_30) && ctx.src_zelda_version < 0x255;
}

// A 2.55 quest's section at the same version as 2.53's own: ZQuest's 2.53 data holds all of it,
// and these writers write it as they do a 2.53 quest's (door combo sets v1, item drop sets v2).
bool target_ok_253_layout(const ZqNewerContext &ctx, word v253)
{
    return target_ok(ctx) || ((ctx.target == ZQ_NEWER_255 || ctx.target == ZQ_NEWER_30) && ctx.src_zelda_version >= 0x255 &&
                              ctx.src_part && ctx.src_version == v253);
}

// A section as both versions write it: id, version, cversion (2.55.17 its CV_ constant, 3.0 the
// version again), any extra field, the size, the data.
int emit(PACKFILE *f, dword id, word version, word cversion, const Out *extra, const Out &body)
{
    Out o;
    o.w(version);
    o.w(cversion);

    if(extra)
        o.put(extra->b.empty() ? NULL : &extra->b[0], extra->b.size());

    o.l((dword)body.b.size());

    if(pack_mputl((long)id, f) == EOF)
        return ZQN_WRITE_FAILED;

    if(pack_fwrite(&o.b[0], (long)o.b.size(), f) != (long)o.b.size())
        return ZQN_WRITE_FAILED;

    if(!body.b.empty() && pack_fwrite(&body.b[0], (long)body.b.size(), f) != (long)body.b.size())
        return ZQN_WRITE_FAILED;

    return ZQW_OK;
}

// ---- the header ---------------------------------------------------------------------------

// INTERNAL_VERSION of both versions' editors
const word UPSTREAM_INTERNAL = 0xA721;

// This program's build date and time, as BUILDTM_* and __DATE__/__TIME__ give upstream's.
void build_time(int *year, int *month, int *day, int *hour, int *minute)
{
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;   // "Nov 10 2026"
    const char *t = __TIME__;   // "08:56:05"
    const char *m = strstr(months, std::string(d, 3).c_str());
    *month = m ? (int)(m - months) / 3 + 1 : 0;
    *day = atoi(d + 4);
    *year = atoi(d + 7);
    *hour = atoi(t);
    *minute = atoi(t + 3);
}

// 3.0's version string: "3.0.0", and this program in semver build metadata. 3.0 shows it as
// the version that saved the quest (header dialog, quest browser, logs), and its ZQuest makes
// a backup before saving over a file whose string isn't its own; nothing else reads it. Its
// field is 44 characters.
std::string version_string_30()
{
    std::string s = "3.0.0";
#ifdef ZPLAYER_WINDOW_TITLE
    std::string meta;

    for(const char *p = ZPLAYER_WINDOW_TITLE; *p; ++p)
    {
        char ch = *p;
        bool ok = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '-';
        meta += ok ? ch : '-';
    }

    if(!meta.empty())
        s += "+" + meta;
#endif

    if(s.size() > 44)
        s.resize(44);

    return s;
}

// The compiler, as each version's writeheader names it: 2.55's name and version strings for
// the compiler it was built with, 3.0's fixed "Unknown" name (metadata.h) with MSVC's version
// number, both with the version as four numbers.
void compiler_fields(Out &o, bool t30)
{
#if defined(_MSC_VER)
    char cver[16];
    sprintf(cver, "%d", _MSC_VER);
    o.text(t30 ? "Unknown" : "MSVC", 256);
    o.text(cver, 256);
#elif defined(__clang__)
    char cver[32];
    sprintf(cver, "%d.%d.%d", __clang_major__, __clang_minor__, __clang_patchlevel__);
    o.text(t30 ? "Unknown" : "Clang", 256);
    o.text(t30 ? "Unknown" : cver, 256);
#elif defined(__GNUC__)
    char cver[32];
    sprintf(cver, "%d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
    o.text(t30 ? "Unknown" : "GCC", 256);
    o.text(t30 ? "Unknown" : cver, 256);
#else
    o.text("Unknown", 256);
    o.text("Unknown", 256);
#endif
}

void compiler_numbers(Out &o, bool t30)
{
#if defined(_MSC_VER)
    (void)t30;
    o.l(_MSC_VER / 100);
    o.l(_MSC_VER % 100);
    o.l(_MSC_FULL_VER % 100000);
    o.l(_MSC_BUILD);
#elif defined(__clang__)
    o.l(t30 ? 0 : __clang_major__);
    o.l(t30 ? 0 : __clang_minor__);
    o.l(t30 ? 0 : __clang_patchlevel__);
    o.l(0);
#elif defined(__GNUC__)
    o.l(t30 ? 0 : __GNUC__);
    o.l(t30 ? 0 : __GNUC_MINOR__);
    o.l(t30 ? 0 : __GNUC_PATCHLEVEL__);
    o.l(0);
#else
    (void)t30;
    o.l(0); o.l(0); o.l(0); o.l(0);
#endif
}

int zqn_header(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!target_ok(ctx) && !((ctx.target == ZQ_NEWER_255 || ctx.target == ZQ_NEWER_30) && ctx.src_zelda_version >= 0x255))
        return ZQW_UNSUPPORTED;

    bool t30 = ctx.target == ZQ_NEWER_30;

    // The id string, and a zero for its 31st byte (whatever the original file had there)
    Out id;
    id.put(QH_NEWIDSTR, 30);
    id.c(0);

    Out o;
    o.w(0x255);         // ZELDA_VERSION
    o.c(61);            // VERSION_BUILD
    o.put(header.pwd_hash, 16);
    o.w(UPSTREAM_INTERNAL);
    o.c(header.quest_number);

    // The quest's version and minimum version (the 9 bytes old headers have, in 16-byte
    // fields), title and author, each as a string: upstream's save has zeros after its end
    // where the original file often had leftovers of older text. Headers from version 8 (2.55
    // and 3.0 quests) hold 16 bytes of each, of which ZQuest's own header keeps only the first 8
    // characters: those come from the quest's own-version header (which carries any edit).
    char version[17], minver[17];
    memset(version, 0, sizeof(version));
    memset(minver, 0, sizeof(minver));
    memcpy(version, header.version, sizeof(header.version) < 16 ? sizeof(header.version) : 16);
    memcpy(minver, header.minver, sizeof(header.minver) < 16 ? sizeof(header.minver) : 16);
    size_t vlen = sizeof(header.version) < 16 ? sizeof(header.version) : 16;
    std::vector<byte> own;

    // the header part: the 31-byte id string, then the section (id, version, cversion, size,
    // then the data: program version, build, password hash, internal, quest number, version...)
    if(ctx.src_zelda_version >= 0x255 && ctx.src_part && zq_own_part_bytes(*ctx.src_part, own) && own.size() >= 43 + 54 &&
       !memcmp(&own[31], "HDR ", 4) && (own[35] | (own[36] << 8)) >= 8)
    {
        memcpy(version, &own[43 + 22], 16);
        memcpy(minver, &own[43 + 38], 16);
        vlen = 16;
    }

    o.cfield(version, vlen, 16);
    o.cfield(minver, vlen, 16);
    o.cfield(header.title, 64, 65);
    o.cfield(header.author, 64, 65);
    o.c(header.use_keyfile);

    // the data flags: the save sets "has tiles" and "has cheats"; readheader keeps no others
    byte flags[ZQ_MAXDATA];
    memset(flags, 0, sizeof(flags));
    flags[ZQ_TILES] = 1;
    flags[ZQ_CHEATS2] = 1;
    o.put(flags, sizeof(flags));

    // the template path as a string too: some old files carry leftover memory after its end
    // (2.50.0's "The Madness Dungeon"), which upstream's save writes as zeros
    o.cfield(header.templatepath, sizeof(header.templatepath) - 1, sizeof(header.templatepath));
    o.c(0);             // the old map count, always written as 0

    // the version that saved the file: the target's release
    if(t30)
    {
        o.l(3); o.l(0); o.l(0);     // 3.0.0
        o.l(0);                     // the fourth part, deprecated
        o.l(0); o.l(0); o.l(0); o.l(0);     // alpha/beta/gamma/release, deprecated in 3.0
    }
    else
    {
        o.l(2); o.l(55); o.l(17); o.l(0);   // 2.55.17
        o.l(0); o.l(0); o.l(0); o.l(1);     // a release
    }

    // the build that saved it: ours
    int year, month, day, hour, minute;
    build_time(&year, &month, &day, &hour, &minute);
    o.w(year);
    o.c(month);
    o.c(day);
    o.c(hour);
    o.c(minute);
    // the developer signoff: none (3.0 writes none; 2.55's builds write their builder's
    // DEV_SIGNOFF, which isn't ours to claim, and print it only when it isn't empty)
    o.zeros(256);
    compiler_fields(o, t30);
    o.text("ZQuest Classic", 1024);     // the product
    o.c(1);                             // compiler signature version
    compiler_numbers(o, t30);
    o.w(0);                             // developer id, unused

    // the module the data came from: 2.55's classic module; 3.0 has no modules
    o.text(t30 ? "" : "modules/classic.zmod", 1024);
    o.text(__DATE__, 256);
    o.text(__TIME__, 256);

    // the build's time zone: not known (__TIME__ is the build machine's clock, local time on
    // a desktop and UTC on a CI runner); empty is what headers before version 5 read as
    o.zeros(6);
    o.c(0);                             // zinfo in the quest file, not beside it
    o.c(0);                             // not a nightly (3.0: isStableRelease of 3.0.0)

    if(t30)
        o.cstr(version_string_30());

    Out sec;
    sec.w(t30 ? 9 : 8);                 // V_HEADER
    sec.w(t30 ? 9 : 3);                 // CV_HEADER (2.55.17), the version again (3.0)
    sec.l((dword)o.b.size());

    if(pack_fwrite(&id.b[0], (long)id.b.size(), f) != (long)id.b.size() || pack_mputl((long)ID_HEADER, f) == EOF ||
       pack_fwrite(&sec.b[0], (long)sec.b.size(), f) != (long)sec.b.size() ||
       pack_fwrite(&o.b[0], (long)o.b.size(), f) != (long)o.b.size())
        return ZQN_WRITE_FAILED;

    return ZQW_OK;
}

// ---- the editor names (zinfo) --------------------------------------------------------------

// An old quest has no zinfo: upstream's names are all empty, and writezinfo writes only the
// counts of each kind of name with a zero length for each.
int zqn_zinfo(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!target_ok(ctx))
        return ZQW_UNSUPPORTED;

    bool t30 = ctx.target == ZQ_NEWER_30;
    Out o;
    const int itypes = 512, mapflags = 256, counters = 107, weapons = 146;
    const int ctypes = t30 ? 184 : 183;     // cMAX

    o.w(itypes);
    o.zeros(itypes * 3);        // name length byte, help length word
    o.w(ctypes);
    o.zeros(ctypes * 3);
    o.w(mapflags);
    o.zeros(mapflags * 3);
    o.w(counters);
    o.zeros(counters);
    o.w(weapons);
    o.zeros(weapons);

    if(t30)
    {
        const int etypes = 73, litems = 16;     // eeMAX, li_max
        o.w(etypes);
        o.zeros(etypes);
        o.c(litems);
        o.zeros(litems * 4);    // name and abbreviation length bytes, help length word
    }

    return emit(f, ID_ZINFO_, t30 ? 5 : 3, t30 ? 5 : 0, NULL, o);
}

// ---- the rules ------------------------------------------------------------------------------

int zqn_rules_section(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!target_ok(ctx))
        return ZQW_UNSUPPORTED;

    bool t30 = ctx.target == ZQ_NEWER_30;
    ZqnRules r;

    if(!zqn_upstream_rules(ctx.target, r))
        return ZQN_BAD_PART;

    // A changed rule the target's loader would set back for a quest of this version: its own
    // conversion of the quest saved in its version would lose the change, so say so.
    if(r.overridden_rule >= 0)
    {
        char buf[200];
        snprintf(buf, sizeof(buf), "rule %d was changed, but %s sets it for every quest made in this quest's version", r.overridden_rule,
                 t30 ? "3.0" : "2.55.17");
        zqw_last_error = buf;
        return ZQN_REFUSED;
    }

    Out compat;
    compat.l(t30 ? 122 : 65);   // V_COMPATRULE: every compatibility rule the version knows is decided

    Out o;
    o.put(r.now, ZQN_RULES_SIZE);

    if(t30)
    {
        o.l(1);     // V_COMPILESETTING
        o.w(0);     // no compiler settings: an old quest has none
    }

    return emit(f, ID_RULES, t30 ? 18 : 17, t30 ? 18 : 1, &compat, o);
}

// ---- door combo sets -------------------------------------------------------------------

// upstream's MakeDoors (core/qst_maps.cpp): which template positions each door's combos are
const int g_door_u[9][4] =
{
    {37, 38, 53, 54}, {37, 38, 39, 40}, {37, 38, 55, 56}, {37, 38, 39, 40}, {37, 38, 53, 54},
    {37, 38, 53, 54}, {37, 38, 53, 54}, {7, 8, 23, 24}, {7, 8, 41, 42}
};

const int g_door_d[9][4] =
{
    {117, 118, 133, 134}, {135, 136, 133, 134}, {119, 120, 133, 134}, {135, 136, 133, 134}, {117, 118, 133, 134},
    {117, 118, 133, 134}, {117, 118, 133, 134}, {151, 152, 167, 168}, {137, 138, 167, 168}
};

const int g_door_l[9][6] =
{
    {66, 67, 82, 83, 98, 99}, {66, 68, 82, 84, 98, 100}, {66, 69, 82, 85, 98, 101}, {66, 68, 82, 84, 98, 100},
    {66, 67, 82, 83, 98, 99}, {66, 67, 82, 83, 98, 99}, {66, 67, 82, 83, 98, 99}, {64, 65, 80, 81, 96, 97},
    {64, 65, 80, 114, 96, 97}
};

const int g_door_r[9][6] =
{
    {76, 77, 92, 93, 108, 109}, {75, 77, 91, 93, 107, 109}, {74, 77, 90, 93, 106, 109}, {75, 77, 91, 93, 107, 109},
    {76, 77, 92, 93, 108, 109}, {76, 77, 92, 93, 108, 109}, {76, 77, 92, 93, 108, 109}, {78, 79, 94, 95, 110, 111},
    {78, 79, 125, 95, 110, 111}
};

// ZQuest's door sets as loaded, to tell whether the user changed them since.
std::vector<DoorComboSet> g_loaded_doors;

void note_loaded_doors()
{
    g_loaded_doors.assign(DoorComboSets, DoorComboSets + door_combo_set_count);
}

ZqLoadHookRegistration g_door_hook(note_loaded_doors);

bool doors_changed()
{
    if(g_loaded_doors.size() != door_combo_set_count)
        return true;

    for(size_t i = 0; i < g_loaded_doors.size(); ++i)
        if(memcmp(&g_loaded_doors[i], &DoorComboSets[i], sizeof(DoorComboSet)))
            return true;

    return false;
}

// A set as MakeDoors builds it from a map's template screen, the combos on the given page
// (tdcmbdat: the template's combo's low byte, plus the screen's old combo page). Every cset is 2.
void make_door_set(int map, byte page, DoorComboSet &d)
{
    memset(&d, 0, sizeof(d));
    const mapscr &t = TheMaps[map * MAPSCRS + TEMPLATE];
#define ZQN_TD(pos) (word)((t.data[pos] & 0xFF) + (page << 8))

    for(int i = 0; i < 9; ++i)
        for(int j = 0; j < 4; ++j)
        {
            d.doorcombo_u[i][j] = ZQN_TD(g_door_u[i][j]);
            d.doorcset_u[i][j] = 2;
            d.doorcombo_d[i][j] = ZQN_TD(g_door_d[i][j]);
            d.doorcset_d[i][j] = 2;
        }

    for(int i = 0; i < 9; ++i)
        for(int j = 0; j < 6; ++j)
        {
            d.doorcombo_l[i][j] = ZQN_TD(g_door_l[i][j]);
            d.doorcset_l[i][j] = 2;
            d.doorcombo_r[i][j] = ZQN_TD(g_door_r[i][j]);
            d.doorcset_r[i][j] = 2;
        }

    d.bombdoorcombo_u[0] = ZQN_TD(57);
    d.bombdoorcombo_u[1] = ZQN_TD(58);
    d.bombdoorcset_u[0] = d.bombdoorcset_u[1] = 2;
    d.bombdoorcombo_d[0] = ZQN_TD(121);
    d.bombdoorcombo_d[1] = ZQN_TD(122);
    d.bombdoorcset_d[0] = d.bombdoorcset_d[1] = 2;
    d.bombdoorcombo_l[1] = ZQN_TD(115);
    d.bombdoorcset_l[0] = d.bombdoorcset_l[1] = d.bombdoorcset_l[2] = 2;
    d.bombdoorcombo_r[1] = ZQN_TD(124);
    d.bombdoorcset_r[0] = d.bombdoorcset_r[1] = d.bombdoorcset_r[2] = 2;

    // the walk-through combo, and (an upstream slip kept as is) the same value as its cset
    for(int i = 0; i < 4; ++i)
    {
        d.walkthroughcombo[i] = ZQN_TD(34);
        d.walkthroughcset[i] = (byte)ZQN_TD(34);
    }

#undef ZQN_TD
}

// MakeDoors compares sets field by field; the name isn't one of them.
bool same_door_set(const DoorComboSet &a, const DoorComboSet &b)
{
    return !memcmp((const byte *)&a + sizeof(a.name), (const byte *)&b + sizeof(b.name), sizeof(a) - sizeof(a.name));
}

// MakeDoors over every screen of every map, as 3.0's readmaps runs it: in map order, each map
// after its screens are read. Before 1.92 beta 137 a map has 132 screens in the file; 3.0
// copies screen 131 to 132 without its combo page (so page 0) and leaves 133-135 empty.
bool made_door_sets(std::vector<DoorComboSet> &sets, std::vector<word> &screen_set)
{
    word zv = zq_quest_file.zelda_version;
    byte build = zq_quest_file.build;

    if(!(zv < 0x192 || (zv == 0x192 && build < 154)) || doors_changed())
        return false;

    bool copied_132 = zv < 0x192 || (zv == 0x192 && build < 137);
    sets.clear();
    screen_set.assign((size_t)map_count * MAPSCRS, 0);

    for(int m = 0; m < map_count; ++m)
    {
        for(int s = 0; s < MAPSCRS; ++s)
        {
            size_t at = (size_t)m * MAPSCRS + s;

            if(at >= TheMaps.size())
                return false;

            if(copied_132 && s > 132)
                continue;   // empty screens: not valid, set 0

            const mapscr &scr = TheMaps[copied_132 && s == 132 ? at - 1 : at];

            if(!(scr.valid & mVALID))
                continue;

            DoorComboSet d;
            make_door_set(m, copied_132 && s == 132 ? 0 : scr.old_cpage, d);
            size_t k = 0;

            while(k < sets.size() && !same_door_set(sets[k], d))
                ++k;

            if(k >= MAXDOORCOMBOSETS)
                continue;

            if(k == sets.size())
            {
                sprintf(d.name, "Door Combo Set %d", (int)k);
                sets.push_back(d);
            }

            screen_set[at] = (word)k;
        }
    }

    return true;
}

int zqn_doors(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!target_ok_253_layout(ctx, 1))
        return ZQW_UNSUPPORTED;

    std::vector<DoorComboSet> made;
    std::vector<word> screen_set;
    const DoorComboSet *sets = DoorComboSets;
    int count = door_combo_set_count;

    if(ctx.target == ZQ_NEWER_30 && zqn_made_door_sets(made, screen_set))
    {
        sets = made.empty() ? NULL : &made[0];
        count = (int)made.size();
    }

    Out o;
    o.w(count);

    for(int i = 0; i < count; ++i)
    {
        const DoorComboSet &d = sets[i];
        char name[21];
        memset(name, 0, sizeof(name));
        strncpy(name, d.name, sizeof(name) - 1);
        o.put(name, sizeof(name));

        for(int j = 0; j < 9; ++j)
            for(int k = 0; k < 4; ++k)
                o.w(d.doorcombo_u[j][k]);

        o.put(d.doorcset_u, sizeof(d.doorcset_u));

        for(int j = 0; j < 9; ++j)
            for(int k = 0; k < 4; ++k)
                o.w(d.doorcombo_d[j][k]);

        o.put(d.doorcset_d, sizeof(d.doorcset_d));

        for(int j = 0; j < 9; ++j)
            for(int k = 0; k < 6; ++k)
                o.w(d.doorcombo_l[j][k]);

        o.put(d.doorcset_l, sizeof(d.doorcset_l));

        for(int j = 0; j < 9; ++j)
            for(int k = 0; k < 6; ++k)
                o.w(d.doorcombo_r[j][k]);

        o.put(d.doorcset_r, sizeof(d.doorcset_r));

        for(int j = 0; j < 2; ++j)
            o.w(d.bombdoorcombo_u[j]);

        o.put(d.bombdoorcset_u, 2);

        for(int j = 0; j < 2; ++j)
            o.w(d.bombdoorcombo_d[j]);

        o.put(d.bombdoorcset_d, 2);

        for(int j = 0; j < 3; ++j)
            o.w(d.bombdoorcombo_l[j]);

        o.put(d.bombdoorcset_l, 3);

        for(int j = 0; j < 3; ++j)
            o.w(d.bombdoorcombo_r[j]);

        o.put(d.bombdoorcset_r, 3);

        for(int j = 0; j < 4; ++j)
            o.w(d.walkthroughcombo[j]);

        o.put(d.walkthroughcset, 4);
        o.put(d.flags, 2);
    }

    return emit(f, ID_DOORS, 1, 1, NULL, o);
}

// ---- cheats ---------------------------------------------------------------------------------

// The save turns cheats on in the header; the codes are what the quest had (none when its
// header said it had none).
int zqn_cheats(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!target_ok(ctx))
        return ZQW_UNSUPPORTED;

    Out o;
    o.c(1);
    o.l(zcheats.flags);
    o.put(zcheats.codes, sizeof(zcheats.codes));
    return emit(f, ID_CHEATS, 1, 1, NULL, o);
}

// ---- item drop sets -------------------------------------------------------------------------

int zqn_dropsets(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!target_ok_253_layout(ctx, 2))
        return ZQW_UNSUPPORTED;

    int n = count_item_drop_sets();
    Out o;
    o.w(n);

    for(int i = 0; i < n; ++i)
    {
        // the name as a string: upstream's files have zeros after its end, where old files often
        // kept leftovers of an earlier name
        const item_drop_object &d = item_drop_sets[i];
        o.cfield(d.name, 64, 64);

        for(int j = 0; j < 10; ++j)
            o.w(d.item[j]);

        for(int j = 0; j < 11; ++j)
            o.w(d.chance[j]);
    }

    return emit(f, ID_ITEMDROPSETS, 2, ctx.target == ZQ_NEWER_30 ? 2 : 1, NULL, o);
}

// ---- favorites -----------------------------------------------------------------------------

// 2.53 keeps 100 favorite combos, 13 to a row; upstream rows are 20 long, and its reader moves
// each of 100 old favorites to the same row and column (readfavorites, "port old favorite
// combos"). The editor's scroll positions and map page buttons aren't in an old favorites
// section; upstream sets them to zero.
int zqn_favorites(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!target_ok(ctx))
        return ZQW_UNSUPPORTED;

    const int per_row = 20, per_page = 140, old_per_row = 13;
    std::vector<long> fav;

    for(int i = 0; i < MAXFAVORITECOMBOS; ++i)
    {
        int at = (i % old_per_row) + (i / old_per_row) * per_row;

        if((int)fav.size() <= at)
            fav.resize(at + 1, -1);

        fav[at] = favorite_combos[i];
    }

    int count = 0;

    for(int i = (int)fav.size() - 1; i >= 0; --i)
        if(fav[i] != -1)
        {
            count = i + 1;
            break;
        }

    Out o;
    o.w(per_row);
    o.w(per_page);
    o.w(count);

    for(int i = 0; i < count; ++i)
    {
        o.c(0);         // the favorite's mode: a combo
        o.l((dword)fav[i]);
    }

    o.w(4);             // MAX_COMBO_COLS: first combo, alias and pool list positions
    o.zeros(4 * 12);
    o.w(9);             // MAX_MAPPAGE_BTNS: map and screen
    o.zeros(9 * 8);
    return emit(f, ID_FAVORITES, 4, ctx.target == ZQ_NEWER_30 ? 4 : 1, NULL, o);
}

// ---- advanced music (3.0) ---------------------------------------------------------------

int zqn_advmusic(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!target_ok(ctx) || ctx.target != ZQ_NEWER_30)
        return ZQW_UNSUPPORTED;

    ZqnQuestMusic qm;

    if(!zqn_quest_music(qm))
        return ZQN_BAD_PART;

    Out o;
    o.w((int)qm.music.size());

    for(size_t i = 0; i < qm.music.size(); ++i)
    {
        const ZqnMusic &m = qm.music[i];
        o.wstr(m.name);
        o.cstr(m.path);

        if(!m.path.empty())
        {
            o.l(0); o.l(0);     // loop start and end: old quests have none
            o.l(0); o.l(0);     // crossfade in and out
            o.c(m.track);
        }

        o.w(m.midi);
    }

    return emit(f, ID_ADVMUSIC_, 1, 1, NULL, o);
}

ZqNewerRegistration g_reg_header(ID_HEADER, zqn_header, "header for 2.55.17 and 3.0");
ZqNewerRegistration g_reg_zinfo(ID_ZINFO_, zqn_zinfo, "editor names for 2.55.17 and 3.0");
ZqNewerRegistration g_reg_rules(ID_RULES, zqn_rules_section, "rules for 2.55.17 and 3.0");
ZqNewerRegistration g_reg_doors(ID_DOORS, zqn_doors, "door combo sets for 2.55.17 and 3.0");
ZqNewerRegistration g_reg_cheats(ID_CHEATS, zqn_cheats, "cheats for 2.55.17 and 3.0");
ZqNewerRegistration g_reg_drops(ID_ITEMDROPSETS, zqn_dropsets, "item drop sets for 2.55.17 and 3.0");
ZqNewerRegistration g_reg_favs(ID_FAVORITES, zqn_favorites, "favorites for 2.55.17 and 3.0");
ZqNewerRegistration g_reg_amus(ID_ADVMUSIC_, zqn_advmusic, "advanced music for 3.0");

} // namespace

} // namespace zqnb

bool zqn_made_door_sets(std::vector<DoorComboSet> &sets, std::vector<word> &screen_set)
{
    return zqnb::made_door_sets(sets, screen_set);
}

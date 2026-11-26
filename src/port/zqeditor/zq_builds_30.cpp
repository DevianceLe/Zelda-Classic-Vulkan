// The 3.0 builds ZQuest can save a quest as or start a new quest in (zq_builds.h).
//
// Upstream's 3.0 builds are its 3.0.0-prerelease.N tags on main (its site's 3.0 nightlies, 239 of
// them to 2026-11-19) and the commit our 3.0 Nightly writers follow (ef321f8, between 236 and
// 237). tools/zq_gen_30_builds.py reads every one's section versions, cversions, compat rule
// version, ZASM metadata version and header writer from upstream's history and groups the runs of
// builds that write quests alike (zq_builds_30_table.h): each run is one entry here, named by the
// date of its first build. 240 builds, 89 formats.
//
// Save As a build: the quest is written as 3.0 Nightly (zq_save_newer, every section in the newest
// format, with upstream's conversions of an older quest made), then each section whose version
// the build doesn't have is laid out again in the build's own version, the way that build's
// reader reads it: the record schemas of zq_tape.h (fields by name; a field the build doesn't
// have is left out, one it has that the newest doesn't gets the reader's default), the combo
// encoder of zqw_combos.cpp, and small steps for the rules, scripts and header. A section with
// no way down yet, or a record that would lose a script, stops the save with the reason. The
// newest entry is the 3.0 Nightly Save As and New as they were.
//
// New in a build: the 3.0 tileset wizard (Cambria from the builds that have it, Classic), the
// tileset made a quest of that build through its Save As, then the wizard's rules.

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "zq_class.h"
#include "jwin.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqn_255.h"
#include "zq_tape.h"
#include "zq_builds.h"

extern FONT *lfont;
extern int q255_header_version[3];          // the open quest's program version (q255_basic.cpp)
extern std::string q255_header_version_text;
extern long zasm30_stream_len;
extern ffscript *ffscripts[512];
extern ffscript *itemscripts[256];
extern ffscript *guyscripts[256];
extern ffscript *wpnscripts[256];
extern ffscript *globalscripts[NUMSCRIPTGLOBAL];
extern ffscript *linkscripts[3];
extern ffscript *screenscripts[256];

// zqw_combos.cpp
bool zq30_combos_down(const std::vector<byte> &body, word from_v, word to_v, int weap_v, std::vector<byte> &out,
                      std::string &why);
// q255_scripts.cpp
bool q255_ffscript_meta_spans(const byte *data, size_t size, word v, std::vector<size_t> &meta_v_at,
                              std::vector<std::pair<size_t, size_t> > &spans);
// zq_builds_30_new.cpp
bool zq30_new_quest(const zq_build &b, int fmt);
bool zq30_new_from(int fmt, const char *version_name, int which, int ruleset, bool bugfix, bool zs_bugfix, std::string &why);

int zq30_format_count();
int zq30_save_as(int fmt, const char *path, std::string &why);

namespace
{

#include "zq_builds_30_table.h"

const int NFMT = (int)(sizeof(zq30_formats) / sizeof(zq30_formats[0]));

enum
{
    S_HEADER, S_RULES, S_STRINGS, S_MISC, S_TILES, S_COMBOS, S_CSETS, S_MAPS, S_DMAPS, S_DOORS, S_ITEMS, S_WEAPONS,
    S_COLORS, S_ICONS, S_INITDATA, S_GUYS, S_MIDIS, S_CHEATS, S_COMBOALIASES, S_LINKSPRITES, S_SUBSCREEN,
    S_ITEMDROPSETS, S_FFSCRIPT, S_SFX, S_FAVORITES, S_ZINFO, S_ADVMUSIC, S_SECTIONS,
    S_COMPATRULE = S_SECTIONS, S_METADATA, S_COMPILESETTING, S_WEAP_DATA, S_SCRIPT_CONFIG
};

dword tag(char a, char b, char c, char d)
{
    return ((dword)(byte)a << 24) | ((dword)(byte)b << 16) | ((dword)(byte)c << 8) | (dword)(byte)d;
}

dword section_id(int k)
{
    static const dword ids[S_SECTIONS] = {
        ID_HEADER, ID_RULES, ID_STRINGS, ID_MISC, ID_TILES, ID_COMBOS, ID_CSETS, ID_MAPS, ID_DMAPS, ID_DOORS, ID_ITEMS,
        ID_WEAPONS, ID_COLORS, ID_ICONS, ID_INITDATA, ID_GUYS, ID_MIDIS, ID_CHEATS, ID_COMBOALIASES, ID_LINKSPRITES,
        ID_SUBSCREEN, ID_ITEMDROPSETS, ID_FFSCRIPT, ID_SFX, ID_FAVORITES, 0, 0 };

    if(k == S_ZINFO)
        return tag('Z', 'I', 'N', 'F');

    if(k == S_ADVMUSIC)
        return tag('A', 'M', 'U', 'S');

    return ids[k];
}

int section_index(dword id)
{
    for(int k = 0; k < S_SECTIONS; ++k)
        if(section_id(k) == id)
            return k;

    return -1;
}

std::string sec_name(dword id)
{
    if(id == ID_HEADER)
        return "the header";

    char t[5] = { (char)(id >> 24), (char)(id >> 16), (char)(id >> 8), (char)id, 0 };
    std::string s = t;

    while(!s.empty() && s[s.size() - 1] == ' ')
        s.erase(s.size() - 1);

    return s;
}

int newest_format()
{
    return NFMT - 1;
}

long date_number(const char *date)
{
    // "2026-11-06" -> 20261106
    return atol(date) * 10000 + atol(date + 5) * 100 + atol(date + 8);
}

std::string format_name(int f)
{
    return std::string("3.0 Nightly ") + zq30_formats[f].date;
}

// ---- a 3.0 quest file as sections ----------------------------------------------------------

struct Sec
{
    dword id;
    word v, cv;
    std::vector<byte> extra;    // RULE: the compat rule version (4 bytes); FFSC: the ZASM metadata version (2)
    std::vector<byte> data;
};

struct File30
{
    byte idstr[31];
    std::vector<Sec> secs;
};

word le16(const std::vector<byte> &b, size_t at)
{
    return (word)(b[at] | (b[at + 1] << 8));
}

dword le32(const std::vector<byte> &b, size_t at)
{
    return (dword)b[at] | ((dword)b[at + 1] << 8) | ((dword)b[at + 2] << 16) | ((dword)b[at + 3] << 24);
}

void put16(std::vector<byte> &b, word v)
{
    b.push_back((byte)v);
    b.push_back((byte)(v >> 8));
}

void put32(std::vector<byte> &b, dword v)
{
    put16(b, (word)v);
    put16(b, (word)(v >> 16));
}

bool read_file(const char *path, File30 &out)
{
    PACKFILE *f = pack_fopen_password(path, F_READ_PACKED, "");

    if(!f)
        return false;

    bool ok = pack_fread(out.idstr, 31, f) == 31;

    for(bool first = true; ok; first = false)
    {
        byte h[8];
        long got = pack_fread(h, 8, f);

        if(got == 0 && !first)
            break;

        if(got != 8)
        {
            ok = false;
            break;
        }

        Sec s;
        s.id = ((dword)h[0] << 24) | ((dword)h[1] << 16) | ((dword)h[2] << 8) | (dword)h[3];
        s.v = (word)(h[4] | (h[5] << 8));
        s.cv = (word)(h[6] | (h[7] << 8));
        const size_t extra = s.id == ID_RULES ? 4 : s.id == ID_FFSCRIPT ? 2 : 0;
        s.extra.resize(extra);
        byte z[4];

        if((extra && pack_fread(&s.extra[0], (long)extra, f) != (long)extra) || pack_fread(z, 4, f) != 4)
        {
            ok = false;
            break;
        }

        const dword size = (dword)z[0] | ((dword)z[1] << 8) | ((dword)z[2] << 16) | ((dword)z[3] << 24);
        s.data.resize(size);

        if(size && pack_fread(&s.data[0], (long)size, f) != (long)size)
        {
            ok = false;
            break;
        }

        out.secs.push_back(s);
    }

    pack_fclose(f);
    return ok && !out.secs.empty() && out.secs[0].id == ID_HEADER;
}

bool write_file(const char *path, const File30 &in)
{
    std::vector<byte> b(in.idstr, in.idstr + 31);

    for(size_t i = 0; i < in.secs.size(); ++i)
    {
        const Sec &s = in.secs[i];
        b.push_back((byte)(s.id >> 24));
        b.push_back((byte)(s.id >> 16));
        b.push_back((byte)(s.id >> 8));
        b.push_back((byte)s.id);
        put16(b, s.v);
        put16(b, s.cv);
        b.insert(b.end(), s.extra.begin(), s.extra.end());
        put32(b, (dword)s.data.size());
        b.insert(b.end(), s.data.begin(), s.data.end());
    }

    PACKFILE *f = pack_fopen_password(path, F_WRITE_PACKED, "");

    if(!f)
        return false;

    const bool ok = pack_fwrite(&b[0], (long)b.size(), f) == (long)b.size();
    pack_fclose(f);
    return ok;
}

// ---- record schemas: a record laid out again in another version ---------------------------

typedef bool (*Schema)(TapeParser &, int);

// Whether the field is a script's (a script number, its run arguments or instance values): a
// record whose script data an older layout would drop is refused, never written without it.
bool script_key(const std::string &k)
{
    // a run argument count and an instance value count only shape what follows
    if((k.size() >= 5 && k.compare(k.size() - 5, 5, ".argc") == 0) || (k.size() >= 6 && k.compare(k.size() - 6, 6, ".instc") == 0))
        return false;

    return k.find("script") != std::string::npos || k.find("initd") != std::string::npos ||
           k.compare(0, 3, "sc.") == 0 || k.find(".sc.") != std::string::npos || k.find(".inst") != std::string::npos;
}

// Script fields by their names before prerelease 205's script configs (one script, its run
// arguments, its instance values in a group of their own: "sc.action.script"), as the older
// records name them ("script", "initd0"). Pairs of the newer name's end and the older one's.
static const char *const g_config_names[][2] = {
    { "sc.action.script", "script" }, { "sc.pickup.script", "collect_script" }, { "sc.sprite.script", "sprite_script" },
    { "sc.active.script", "script" }, { "sc.asub.script", "asub_script" }, { "sc.psub.script", "psub_script" },
    { "sc.onmap.script", "onmap_script" }, { "sc.script", "script" } };

bool ends_with(const std::string &s, const std::string &e)
{
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}

void alias(Tape &t, const TapeField &f, const std::string &key)
{
    if(t.find(key.c_str()))
        return;

    TapeField x = f;
    x.key = key;
    t.f.push_back(x);
}

// newer: the tape holds the newer layout's fields and gets the older names beside them (or the
// other way round)
void add_config_aliases(Tape &t, bool newer)
{
    const size_t n = t.f.size();

    for(size_t i = 0; i < n; ++i)
    {
        const TapeField f = t.f[i];
        const std::string &k = f.key;

        for(size_t q = 0; q < sizeof(g_config_names) / sizeof(g_config_names[0]); ++q)
        {
            const std::string a = newer ? g_config_names[q][0] : g_config_names[q][1];
            const std::string b = newer ? g_config_names[q][1] : g_config_names[q][0];

            if(k == a)
                alias(t, f, b);
        }

        // run arguments: "sc.action.initd3" and "initd3", "sc.asub.initd3" and "sub_initd3",
        // "sc.onmap.initd3" and "onmap_initd3"; inside a group (weapon data, FFCs):
        // "wd.sc.initd3" and "wd.initd3", "ffc2.sc.script" and "ffc2.script"
        static const char *const pre[][2] = { { "sc.action.", "" }, { "sc.active.", "" }, { "sc.asub.", "sub_" }, { "sc.psub.", "sub_" },
                                              { "sc.onmap.", "onmap_" }, { "sc.", "" } };
        const size_t d = k.rfind(".initd");
        const size_t at = d == std::string::npos ? k.find("initd") : d + 1;

        if(newer)
        {
            for(size_t q = 0; q < sizeof(pre) / sizeof(pre[0]); ++q)
                if(k.compare(0, strlen(pre[q][0]), pre[q][0]) == 0 && k.compare(strlen(pre[q][0]), 5, "initd") == 0)
                {
                    alias(t, f, std::string(pre[q][1]) + k.substr(strlen(pre[q][0])));
                    break;
                }

            const size_t sc = k.find(".sc.");

            if(sc != std::string::npos && sc > 0 && k.compare(0, 3, "sc.") != 0)
                alias(t, f, k.substr(0, sc) + "." + k.substr(sc + 4));
        }
        else
        {
            if(at == 0 || (at != std::string::npos && at > 0 && k[at - 1] == '_'))
                for(size_t q = 0; q < sizeof(pre) / sizeof(pre[0]); ++q)
                    if(k.compare(0, at, pre[q][1]) == 0 && strlen(pre[q][1]) == at)
                        alias(t, f, std::string(pre[q][0]) + k.substr(at));

            const size_t dot = k.find('.');

            if(dot != std::string::npos && dot > 0 && (ends_with(k, ".script") || k.find(".initd") != std::string::npos) &&
               k.compare(0, 3, "sc.") != 0 && k.find(".sc.") == std::string::npos)
                alias(t, f, k.substr(0, dot) + ".sc." + k.substr(dot + 1));
        }
    }
}

// A script settings container of the init data (version 50 on) that holds nothing: its run
// arguments all 0 and no instance values (global and hero scripts), or no instance values
// (generic scripts).
bool blank_config_blob(const std::string &key, const std::vector<byte> &b)
{
    size_t at = 0;
    bool bad = false;

    struct R
    {
        const std::vector<byte> &b;
        size_t &at;
        bool &bad;
        uint64_t get(int n)
        {
            if(bad || at + n > b.size()) { bad = true; return 0; }
            uint64_t v = 0;
            for(int k = 0; k < n; ++k) v |= (uint64_t)b[at + k] << (8 * k);
            at += n;
            return v;
        }
    } r = { b, at, bad };

    if(key == "global_scripts" || key == "hero_scripts")
    {
        const size_t n = (size_t)r.get(2);

        for(size_t q = 0; q < n && !bad; ++q)
        {
            const size_t argc = (size_t)r.get(2);

            for(size_t a = 0; a < argc && a < 8 && !bad; ++a)
                if(r.get(4))
                    return false;

            if(r.get(2))
                return false;
        }

        return !bad && at == b.size();
    }

    if(key == "gen_inst")
    {
        if(!r.get(2))
            return !bad;

        const bool pairs = r.get(1) != 0;
        const size_t count = (size_t)r.get(2);

        for(size_t q = 0; q < count && !bad; ++q)
        {
            if(pairs)
                r.get(2);

            if(r.get(4))
                return false;
        }

        return !bad && at == b.size();
    }

    return false;
}

// One record (or whole section) at data[at...] of version from, laid out in version to. *end:
// where it ended. weap_v: the build's weapon data version.
bool rebuild(Schema schema, const std::vector<byte> &data, size_t at, int from, int to, int weap_v, size_t *end,
             std::vector<byte> &out, std::string &lost)
{
    Tape t;
    TapeParser p(data.empty() ? NULL : &data[0], data.size(), at, &t);

    if(!schema(p, from) || p.bad)
        return false;

    *end = p.pos;

    for(size_t i = 0; i < t.f.size(); ++i)
    {
        const std::string &k = t.f[i].key;

        if(weap_v >= 0 && k.size() >= 8 && k.compare(k.size() - 8, 8, ".version") == 0 && t.f[i].kind == TK_HIDDEN &&
           t.f[i].value > weap_v)
            t.f[i].value = weap_v;
    }

    add_config_aliases(t, true);

    if(!zqn255_tape_rebuild(schema, t, to, out))
        return false;

    // what of the script data comes back when the older record is read and laid out as the newer
    Tape back, up;
    TapeParser q(out.empty() ? NULL : &out[0], out.size(), 0, &back);

    if(!schema(q, to) || q.bad)
        return false;

    add_config_aliases(back, false);

    std::vector<byte> again;

    if(!zqn255_tape_rebuild(schema, back, from, again))
        return false;

    TapeParser r(again.empty() ? NULL : &again[0], again.size(), 0, &up);

    if(!schema(r, from) || r.bad)
        return false;

    const TapeIndex ui(up);

    for(size_t i = 0; i < t.f.size() && lost.empty(); ++i)
    {
        const TapeField &f = t.f[i];

        if(!script_key(f.key))
            continue;

        const TapeField *g = ui.find(f.key);

        if(!g || g->value != f.value || g->bytes != f.bytes)
        {
            bool blank = f.value == 0 || (ends_with(f.key, ".argc") && !g);

            for(size_t j = 0; j < f.bytes.size() && blank; ++j)
                blank = f.bytes[j] == 0;

            if(!blank && blank_config_blob(f.key, f.bytes))
                blank = true;

            if(!blank)
                lost = f.key;
        }
    }

    return true;
}

bool refuse(std::string &why, const Sec &s, int to, const char *what)
{
    char b[256];
    snprintf(b, sizeof(b), "%s (format %d to %d): %s", sec_name(s.id).c_str(), (int)s.v, to, what);
    why = b;
    return false;
}

bool lost_script(std::string &why, const Sec &s, int to, int rec, const std::string &key)
{
    char b[256];
    snprintf(b, sizeof(b), "record %d holds script data (%s) that format %d can't hold yet", rec, key.c_str(), to);
    return refuse(why, s, to, b);
}

bool whole_section(Schema schema, Sec &s, int to, int weap_v, std::string &why)
{
    size_t end = 0;
    std::vector<byte> out;
    std::string lost;

    if(!rebuild(schema, s.data, 0, s.v, to, weap_v, &end, out, lost) || end != s.data.size())
        return refuse(why, s, to, "it doesn't read as its format says");

    if(!lost.empty())
        return lost_script(why, s, to, 0, lost);

    s.data.swap(out);
    return true;
}

// count (a word), then that many records
bool counted_records(Schema schema, Sec &s, int to, int weap_v, std::string &why)
{
    if(s.data.size() < 2)
        return refuse(why, s, to, "it is too short");

    const int count = le16(s.data, 0);
    std::vector<byte> out(s.data.begin(), s.data.begin() + 2);
    size_t at = 2;

    for(int i = 0; i < count; ++i)
    {
        size_t end = at;
        std::vector<byte> rec;
        std::string lost;

        if(!rebuild(schema, s.data, at, s.v, to, weap_v, &end, rec, lost))
            return refuse(why, s, to, "a record doesn't read as its format says");

        if(!lost.empty())
            return lost_script(why, s, to, i, lost);

        out.insert(out.end(), rec.begin(), rec.end());
        at = end;
    }

    if(at != s.data.size())
        return refuse(why, s, to, "it has bytes past its last record");

    s.data.swap(out);
    return true;
}

// the enemies: 512 names of 64 bytes, then 512 records
bool guy_records(Sec &s, int to, int weap_v, std::string &why)
{
    const size_t names = 64 * (size_t)MAXGUYS;

    if(s.data.size() < names)
        return refuse(why, s, to, "it is too short");

    std::vector<byte> out(s.data.begin(), s.data.begin() + names);
    size_t at = names;

    for(int i = 0; i < MAXGUYS; ++i)
    {
        size_t end = at;
        std::vector<byte> rec;
        std::string lost;

        if(!rebuild(zq_tape_guy, s.data, at, s.v, to, weap_v, &end, rec, lost))
            return refuse(why, s, to, "an enemy doesn't read as its format says");

        if(!lost.empty())
            return lost_script(why, s, to, i, lost);

        out.insert(out.end(), rec.begin(), rec.end());
        at = end;
    }

    if(at != s.data.size())
        return refuse(why, s, to, "it has bytes past its last enemy");

    s.data.swap(out);
    return true;
}

// the items: a count; before version 67 that many names of 64 bytes; the records (with their
// names from 67)
bool item_records(Sec &s, int to, int weap_v, std::string &why)
{
    if(s.data.size() < 2)
        return refuse(why, s, to, "it is too short");

    const int count = le16(s.data, 0);

    if((s.v < 67) != (to < 67))
        return refuse(why, s, to, "the item names move out of the records there (not written yet)");

    size_t at = 2 + (s.v < 67 ? 64 * (size_t)count : 0);

    if(at > s.data.size())
        return refuse(why, s, to, "it is too short");

    std::vector<byte> out(s.data.begin(), s.data.begin() + at);

    for(int i = 0; i < count; ++i)
    {
        size_t end = at;
        std::vector<byte> rec;
        std::string lost;

        if(!rebuild(zq_tape_item, s.data, at, s.v, to, weap_v, &end, rec, lost))
            return refuse(why, s, to, "an item doesn't read as its format says");

        if(!lost.empty())
            return lost_script(why, s, to, i, lost);

        out.insert(out.end(), rec.begin(), rec.end());
        at = end;
    }

    if(at != s.data.size())
        return refuse(why, s, to, "it has bytes past its last item");

    s.data.swap(out);
    return true;
}

// the maps: a count; each map's valid byte, then (when valid) its auto layers, auto palette (36
// on) and region ids (31 on), and its 136 screens
bool map_records(Sec &s, int to, int weap_v, std::string &why)
{
    // before 37 a screen's music is a MIDI number, not 3.0's music list's; before 34 the screen
    // states moved; before 31 the region ids are the DMaps'
    if(to < 37)
        return refuse(why, s, to, "screen music before format 37 is a MIDI number (not written yet)");

    if(s.data.size() < 2)
        return refuse(why, s, to, "it is too short");

    const int maps = le16(s.data, 0);
    std::vector<byte> out(s.data.begin(), s.data.begin() + 2);
    size_t at = 2;

    for(int m = 0; m < maps; ++m)
    {
        if(at >= s.data.size())
            return refuse(why, s, to, "it is too short");

        const byte valid = s.data[at++];
        out.push_back(valid);

        if(!valid)
            continue;

        const size_t head = 12 + 2 + 64;

        if(at + head > s.data.size())
            return refuse(why, s, to, "it is too short");

        out.insert(out.end(), s.data.begin() + at, s.data.begin() + at + head);
        at += head;

        for(int j = 0; j < 136; ++j)
        {
            size_t end = at;
            std::vector<byte> rec;
            std::string lost;

            if(!rebuild(zq_tape_screen, s.data, at, s.v, to, weap_v, &end, rec, lost))
                return refuse(why, s, to, "a screen doesn't read as its format says");

            if(!lost.empty())
                return lost_script(why, s, to, m * 136 + j, lost);

            out.insert(out.end(), rec.begin(), rec.end());
            at = end;
        }
    }

    if(at != s.data.size())
        return refuse(why, s, to, "it has bytes past its last map");

    s.data.swap(out);
    return true;
}

// the editor names (readzinfo): item, combo type and map flag names with help (a word count, then
// a name and help text each), counter and weapon names (a word count, then names); from 4 the
// enemy type names, from 5 the level item names: 5 to 4 to 3 leave the last part out
bool zinfo_down(Sec &s, int to, std::string &why)
{
    const std::vector<byte> &d = s.data;
    size_t at = 0;
    bool bad = false;

    struct W
    {
        const std::vector<byte> &d;
        size_t &at;
        bool &bad;
        size_t get(int n)
        {
            if(bad || at + n > d.size()) { bad = true; return 0; }
            size_t v = 0;
            for(int k = 0; k < n; ++k) v |= (size_t)d[at + k] << (8 * k);
            at += n;
            return v;
        }
        void skip(size_t n) { if(bad || at + n > d.size()) bad = true; else at += n; }
    } w = { d, at, bad };

    for(int b = 0; b < 5; ++b)
    {
        const size_t n = w.get(2);

        for(size_t q = 0; q < n && !bad; ++q)
        {
            w.skip(w.get(1));

            if(b < 3)
                w.skip(w.get(2));
        }
    }

    const size_t end3 = at;
    const size_t ne = w.get(2);

    for(size_t q = 0; q < ne && !bad; ++q)
        w.skip(w.get(1));

    const size_t end4 = at;

    if(bad || s.v != 5 || (to != 4 && to != 3))
        return refuse(why, s, to, "its editor names aren't laid out as expected");

    s.data.resize(to == 4 ? end4 : end3);
    return true;
}

bool quest_has_scripts()
{
    if(zasm30_stream_len > 1)
        return true;

    ffscript **tables[] = { ffscripts, itemscripts, guyscripts, wpnscripts, screenscripts };
    const int sizes[] = { 512, 256, 256, 256, 256 };

    for(int t = 0; t < 5; ++t)
        for(int i = 0; i < sizes[t]; ++i)
            if(tables[t][i] && tables[t][i][0].command != 0xFFFF)
                return true;

    for(int i = 0; i < NUMSCRIPTGLOBAL; ++i)
        if(globalscripts[i] && globalscripts[i][0].command != 0xFFFF)
            return true;

    for(int i = 0; i < 3; ++i)
        if(linkscripts[i] && linkscripts[i][0].command != 0xFFFF)
            return true;

    return false;
}

// One section, from the newest format to the build's.
bool down_section(Sec &s, const Zq30Format &F, std::string &why)
{
    const int k = section_index(s.id);

    if(k < 0)
        return refuse(why, s, 0, "a section this build doesn't know");

    const int to = F.v[k];
    const int weap_v = F.v[S_WEAP_DATA];

    if(s.id == ID_RULES)
    {
        s.extra.clear();
        put32(s.extra, (dword)F.v[S_COMPATRULE]);
    }

    // the scripts' metadata is laid out as its version (the extra word) says: 7 to 6 leaves out
    // each script's info and setup text (q255_ffscript_meta_spans finds them)
    if(s.id == ID_FFSCRIPT && s.extra.size() == 2 && le16(s.extra, 0) != F.v[S_METADATA])
    {
        std::vector<size_t> meta_v_at;
        std::vector<std::pair<size_t, size_t> > spans;

        if(le16(s.extra, 0) != 7 || F.v[S_METADATA] != 6 ||
           !q255_ffscript_meta_spans(s.data.empty() ? NULL : &s.data[0], s.data.size(), s.v, meta_v_at, spans))
        {
            char b[96];
            snprintf(b, sizeof(b), "its scripts' metadata layout %d to %d isn't written yet", (int)le16(s.extra, 0),
                     F.v[S_METADATA]);
            return refuse(why, s, F.v[k], b);
        }

        std::vector<byte> out;
        size_t from = 0, sp = 0, mv = 0;

        for(; sp <= spans.size(); ++sp)
        {
            const size_t to_at = sp < spans.size() ? spans[sp].first : s.data.size();

            for(; mv < meta_v_at.size() && meta_v_at[mv] + 2 <= to_at; ++mv)
                if(meta_v_at[mv] >= from)
                {
                    // the bytes before it, then the meta's own layout version: 6
                    out.insert(out.end(), s.data.begin() + from, s.data.begin() + meta_v_at[mv]);
                    put16(out, 6);
                    from = meta_v_at[mv] + 2;
                }

            out.insert(out.end(), s.data.begin() + from, s.data.begin() + to_at);
            from = sp < spans.size() ? spans[sp].second : s.data.size();
        }

        s.data.swap(out);
        s.extra.clear();
        put16(s.extra, 6);
    }

    // a section the newest save wrote in an older format than the build's (3.0 writes scripts last
    // compiled by an old compiler in the old layout): the build reads it as it is
    if(to < s.v)
    {
        bool ok = false;

        switch(k)
        {
        case S_COMBOS:
        {
            std::vector<byte> out;
            std::string w;

            if(to < 45 || !zq30_combos_down(s.data, s.v, (word)to, weap_v, out, w))
                return refuse(why, s, to, w.empty() ? "not written yet" : w.c_str());

            s.data.swap(out);
            ok = true;
            break;
        }

        case S_RULES:
            // 18 added the compile settings after the rules
            if(s.v == 18 && to == 17)
            {
                if(s.data.size() < 106 || s.data.size() != 106 + 6 * (size_t)le16(s.data, 104))
                    return refuse(why, s, to, "its compile settings aren't laid out as expected");

                if(le16(s.data, 104))
                    return refuse(why, s, to, "it has compile settings, which this build doesn't have");

                s.data.resize(100);
                ok = true;
            }

            break;

        case S_INITDATA: ok = whole_section(zq_tape_init, s, to, weap_v, why); if(!ok) return false; break;
        case S_MISC: ok = whole_section(zq_tape_misc, s, to, weap_v, why); if(!ok) return false; break;
        case S_STRINGS: ok = whole_section(zq_tape_strings, s, to, weap_v, why); if(!ok) return false; break;
        case S_SUBSCREEN: ok = whole_section(zq_tape_subscreens, s, to, weap_v, why); if(!ok) return false; break;
        case S_LINKSPRITES: ok = whole_section(zq_tape_link, s, to, weap_v, why); if(!ok) return false; break;
        case S_DMAPS:
            if(to < 25)
                return refuse(why, s, to, "DMap music before format 25 is a MIDI number (not written yet)");

            ok = counted_records(zq_tape_dmap, s, to, weap_v, why);
            if(!ok) return false;
            break;
        case S_GUYS: ok = guy_records(s, to, weap_v, why); if(!ok) return false; break;
        case S_ITEMS: ok = item_records(s, to, weap_v, why); if(!ok) return false; break;
        case S_MAPS: ok = map_records(s, to, weap_v, why); if(!ok) return false; break;

        case S_ZINFO: ok = zinfo_down(s, to, why); if(!ok) return false; break;

        case S_WEAPONS:
        {
            // 9 (pr.166) moved each sprite's name into its record as a counted string; 8 has a
            // table of 64-byte names first, then the records (misc, CSets, frames, speed, type,
            // script, tile: 11 bytes each); qst_weapons.cpp
            if(s.v != 9 || to != 8 || s.data.size() < 2)
                break;

            const size_t count = le16(s.data, 0);
            std::vector<byte> names, recs;
            size_t at = 2;

            for(size_t q = 0; q < count && at < s.data.size(); ++q)
            {
                const size_t n = s.data[at];

                if(at + 1 + n + 11 > s.data.size())
                    return refuse(why, s, to, "a weapon sprite doesn't read as its format says");

                std::vector<byte> nm(64, 0);
                memcpy(&nm[0], &s.data[at + 1], n < 63 ? n : 63);
                names.insert(names.end(), nm.begin(), nm.end());
                recs.insert(recs.end(), s.data.begin() + at + 1 + n, s.data.begin() + at + 1 + n + 11);
                at += 1 + n + 11;
            }

            if(at != s.data.size() || recs.size() != count * 11)
                return refuse(why, s, to, "it has bytes past its last weapon sprite");

            std::vector<byte> out(s.data.begin(), s.data.begin() + 2);
            out.insert(out.end(), names.begin(), names.end());
            out.insert(out.end(), recs.begin(), recs.end());
            s.data.swap(out);
            ok = true;
            break;
        }

        case S_TILES:
            // 4 (pr.188) marks the format the .ztileset exports share; a quest's tiles read alike
            // in 3 and 4 (qst_tiles.cpp: only "section_version > 2" decides)
            ok = s.v == 4 && to == 3;
            break;

        case S_FFSCRIPT:
            // a quest with no scripts, 31 to 30 with the metadata layout kept (7): its slots are
            // laid out alike (31 adds fields to exported variables only)
            if(to == 30 && F.v[S_METADATA] >= 6 && !quest_has_scripts())
                ok = true;
            else
                return refuse(why, s, to, "its scripts can't be written in that format yet");

            break;

        default:
            break;
        }

        if(!ok)
            return refuse(why, s, to, "not written yet");

        s.v = (word)to;
    }

    // cversions: the build's own until 3.0 deprecated them (pr.98), the version again after
    s.cv = F.first_n < 98 ? (word)F.cv[k] : s.v;

    return true;
}

// our own program, as the version string's build metadata (zqn_basic.cpp's version_string_30)
std::string our_meta()
{
    std::string meta;
#ifdef ZPLAYER_WINDOW_TITLE
    for(const char *p = ZPLAYER_WINDOW_TITLE; *p; ++p)
    {
        char ch = *p;
        bool ok = (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '.' || ch == '-';
        meta += ok ? ch : '-';
    }
#endif
    return meta;
}

// The header as the build writes it: its version string names the build's first prerelease (and
// this program as build metadata), "a nightly" (isStableRelease), before pr.92 the module name,
// before pr.98 the header's own cversion.
bool down_header(Sec &h, const Zq30Format &F, std::string &why)
{
    std::vector<byte> &d = h.data;
    // the version string is last (a length byte, then its characters): the 3.0 Nightly save's own,
    // "3.0.0" and this program as build metadata (zqn_basic.cpp's version_string_30)
    std::string meta = our_meta();
    std::string had = "3.0.0" + (meta.empty() ? std::string("") : "+" + meta);

    if(had.size() > 44)
        had.resize(44);

    const size_t fixed = 1 + 1 + 6 + 256 + 256 + 1024;     // nightly, zinfo, time zone, time, date, module

    if(d.size() < fixed + 1 + had.size() + 1)
    {
        why = "the header isn't laid out as expected";
        return false;
    }

    const size_t at = d.size() - 1 - had.size();

    if(d[at] != had.size() || memcmp(&d[at + 1], had.data(), had.size()) != 0)
    {
        why = "the header isn't laid out as expected";
        return false;
    }

    char num[32];
    snprintf(num, sizeof(num), "%d", F.first_n);
    std::string vs = std::string("3.0.0-prerelease.") + num + "+" + (meta.empty() ? std::string(F.date) : meta);

    if(vs.size() > 44)
        vs.resize(44);

    d.resize(at);
    d.push_back((byte)vs.size());
    d.insert(d.end(), vs.begin(), vs.end());
    d[at - 1] = 1;      // a nightly (isStableRelease: false)

    if(F.first_n < 92)
    {
        // the module the data came from, as 2.55 and 3.0 wrote it until modules were removed
        const size_t mod = at - fixed;
        memset(&d[mod], 0, 1024);
        static const char m[] = "modules/classic.zmod";
        memcpy(&d[mod], m, sizeof(m) - 1);
    }

    h.cv = F.first_n < 98 ? (word)F.cv[S_HEADER] : h.v;
    return true;
}

int save_as_format(int fmt, const char *path, std::string &why)
{
    if(fmt < 0 || fmt >= NFMT)
    {
        why = "no such build";
        return 3;
    }

    const Zq30Format &F = zq30_formats[fmt];

    // a quest made in 2.55 or later: never written in a format older than its own sections
    if(zq_quest_file.zelda_version >= 0x255)
    {
        std::string too_new;

        for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        {
            const ZqQuestPart &p = zq_quest_file.parts[i];
            const int k = section_index(p.id);
            word v = 0, cv = 0;

            if(k <= 0 || !zq_part_versions(p, &v, &cv))
                continue;

            if(!F.v[k] || v > F.v[k])
                too_new += (too_new.empty() ? "" : ", ") + sec_name(p.id);
        }

        if(!too_new.empty())
        {
            why = "It holds parts in versions newer than " + format_name(fmt) + " holds: " + too_new;
            return 8;
        }
    }

    if(fmt == newest_format())
        return zq_save_newer(path, ZQ_NEWER_30, why);

    std::string tmp = std::string(path) + ".z30.tmp";
    remove(tmp.c_str());
    int ret = zq_save_newer(tmp.c_str(), ZQ_NEWER_30, why);

    if(ret)
    {
        remove(tmp.c_str());
        return ret;
    }

    // the editor names beside the quest: not laid out for older builds yet
    FILE *side = fopen((tmp + ".zinfo").c_str(), "rb");

    if(side)
    {
        fclose(side);
        remove((tmp + ".zinfo").c_str());
        remove(tmp.c_str());
        why = "its editor names are kept in a .zinfo file beside it, which isn't written for older 3.0 builds yet";
        return 2;
    }

    File30 q;

    if(!read_file(tmp.c_str(), q))
    {
        remove(tmp.c_str());
        why = "the 3.0 Nightly file couldn't be read back";
        return 3;
    }

    remove(tmp.c_str());
    std::vector<Sec> kept;
    std::string fails;

    for(size_t i = 0; i < q.secs.size(); ++i)
    {
        Sec &s = q.secs[i];
        const int k = section_index(s.id);
        std::string w;

        if(k == S_HEADER)
        {
            if(!down_header(s, F, w))
                fails += (fails.empty() ? "" : "; ") + w;

            kept.push_back(s);
            continue;
        }

        // a section the build doesn't have: 3.0's music objects before pr.157
        if(k >= 0 && !F.v[k])
        {
            if(s.data.size() > 2 && (s.data[0] || s.data[1]))
                fails += (fails.empty() ? "" : "; ") + sec_name(s.id) + ": the build has no place for it";

            continue;
        }

        if(!down_section(s, F, w))
            fails += (fails.empty() ? "" : "; ") + w;

        kept.push_back(s);
    }

    if(!fails.empty())
    {
        why = "These parts can't be written as " + format_name(fmt) + " yet: " + fails;
        return 2;
    }

    q.secs.swap(kept);
    std::string out_tmp = std::string(path) + ".zqsave.tmp";
    remove(out_tmp.c_str());

    if(!write_file(out_tmp.c_str(), q))
    {
        remove(out_tmp.c_str());
        why = "The file couldn't be written.";
        return 3;
    }

    remove(path);

    if(rename(out_tmp.c_str(), path) != 0)
    {
        remove(out_tmp.c_str());
        why = "The saved file couldn't replace the old one.";
        return 7;
    }

    return 0;
}

bool save_as_build(const zq_build &b, const char *path)
{
    std::string why;
    const int ret = save_as_format(b.tag, path, why);

    if(ret)
    {
        std::string a = why.size() > 300 ? why.substr(0, 300) + "..." : why;
        jwin_alert("Save Quest As", ("The quest couldn't be saved as " + b.name + ".").c_str(), a.c_str(), NULL, "O&K",
                   NULL, 'k', 0, lfont);
    }

    return ret == 0;
}

bool make_new_build(const zq_build &b)
{
    return zq30_new_quest(b, b.tag);
}

// the run of builds the quest's header names ("3.0.0-prerelease.N+..."), or -1
int format_of_text(const std::string &text)
{
    const char *p = strstr(text.c_str(), "prerelease.");

    if(!p)
        return -1;

    const int n = atoi(p + 11);

    for(int f = 0; f < NFMT; ++f)
        if(n >= zq30_formats[f].first_n && n <= zq30_formats[f].last_n && zq30_formats[f].first_n != 10000)
            return f;

    return -1;
}

// the run whose section versions are the quest's own (the newest such run), or the oldest run
// that holds them all
int format_of_parts()
{
    int exact = -1, holds = -1;

    for(int f = 0; f < NFMT; ++f)
    {
        bool same = true, fits = true;

        for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        {
            const ZqQuestPart &p = zq_quest_file.parts[i];
            const int k = section_index(p.id);
            word v = 0, cv = 0;

            if(k <= 0 || !zq_part_versions(p, &v, &cv))
                continue;

            if(v != zq30_formats[f].v[k])
                same = false;

            if(v > zq30_formats[f].v[k])
                fits = false;
        }

        if(same)
            exact = f;

        if(fits && holds < 0)
            holds = f;
    }

    return exact >= 0 ? exact : holds;
}

} // namespace

int zq30_format_count()
{
    return NFMT;
}

int zq30_save_as(int fmt, const char *path, std::string &why)
{
    return save_as_format(fmt, path, why);
}

const char *zq30_format_first(int fmt)
{
    return fmt >= 0 && fmt < NFMT ? zq30_formats[fmt].first : "";
}

const char *zq30_format_last(int fmt)
{
    return fmt >= 0 && fmt < NFMT ? zq30_formats[fmt].last : "";
}

bool zq30_format_has_cambria(int fmt)
{
    // upstream added Cambria to File > New on 2024-12-31 (bc449928e): prerelease 84 on
    // (its files are Git LFS objects upstream's history doesn't hold: the 3.0 Nightly one stands in)
    return fmt >= 0 && fmt < NFMT && (*zq30_formats[fmt].cambria || zq30_formats[fmt].last_n >= 84);
}

// File > New's tilesets in the run's last build: 0 Classic, 1 Cambria (a git blob, "" none)
const char *zq30_format_tileset(int fmt, int which)
{
    return fmt >= 0 && fmt < NFMT ? (which == 1 ? zq30_formats[fmt].cambria : zq30_formats[fmt].classic) : "";
}

int zq30_format_compat(int fmt)
{
    return fmt >= 0 && fmt < NFMT ? zq30_formats[fmt].v[S_COMPATRULE] : 0;
}

int zq30_format_init_version(int fmt)
{
    return fmt >= 0 && fmt < NFMT ? zq30_formats[fmt].v[S_INITDATA] : 0;
}

const char *zq30_format_date(int fmt)
{
    return fmt >= 0 && fmt < NFMT ? zq30_formats[fmt].date : "";
}

// The oldest run Save As and New are done for: every run from it to the newest has a way down for
// each of its sections (checked: Save As and New, then opened again). Older runs are offered once
// their sections' steps are written; -qst-save-as-build still tries them.
const char *const OLDEST_OFFERED = "2026-02-16";

void zq_builds_30(std::vector<zq_build> &out)
{
    for(int f = 0; f < NFMT; ++f)
    {
        if(strcmp(zq30_formats[f].date, OLDEST_OFFERED) < 0)
            continue;

        zq_build b;
        b.name = format_name(f);
        b.order = date_number(zq30_formats[f].date);
        b.gen = 0x300;
        b.tag = f;
        b.save_as = save_as_build;
        b.make_new = make_new_build;
        out.push_back(b);
    }
}

long zq_made_order_30()
{
    if(zq_quest_file.zelda_version < 0x255 || q255_header_version[0] != 3)
        return -1;

    int f = format_of_text(q255_header_version_text);

    if(f < 0)
        f = format_of_parts();

    if(f < 0)
        f = newest_format();

    return date_number(zq30_formats[f].date);
}

// -qst-save-as-build "<name>" in.qst out.qst report.txt (tests): loads the quest and saves it as
// that 3.0 build, without the dialogs. -qst-save-as-build list report.txt: the builds.
int zq_builds_30_cli(int argc, char **argv, int at)
{
    extern bool skip_quest_password;
    extern bool zq_batch_load;
    skip_quest_password = true;
    zq_batch_load = true;

    if(argc > at + 2 && !strcmp(argv[at + 1], "list"))
    {
        FILE *out = fopen(argv[at + 2], "w");

        for(int f = 0; out && f < NFMT; ++f)
            fprintf(out, "%s\t%s\t%s\t%d\n", format_name(f).c_str(), zq30_formats[f].first, zq30_formats[f].last,
                    zq30_formats[f].count);

        if(out)
            fclose(out);

        return 0;
    }

    // -qst-save-as-build new "<name>" 0|1 out.qst report.txt: File > New in that build (0 Classic,
    // 1 Cambria; bug fixes on, the tileset's own rules), saved as that build
    if(argc > at + 5 && !strcmp(argv[at + 1], "new"))
    {
        int fmt = -1;

        for(int f = 0; f < NFMT; ++f)
            if(format_name(f) == argv[at + 2] || !strcmp(zq30_formats[f].first, argv[at + 2]))
                fmt = f;

        std::string why = "no such 3.0 build";
        int ret = 3;

        if(fmt >= 0)
            why.clear();

        if(fmt >= 0 && zq30_new_from(fmt, format_name(fmt).c_str(), atoi(argv[at + 3]), 0, true, true, why))
            ret = save_as_format(fmt, argv[at + 4], why);

        FILE *out = fopen(argv[at + 5], "w");

        if(out)
        {
            fprintf(out, "new %s (%s): %s\nmade order %ld\n%s\n", argv[at + 2], atoi(argv[at + 3]) ? "Cambria" : "Classic",
                    ret == 0 ? "SAVED" : "NOT SAVED", zq_made_order_30(), why.c_str());
            fclose(out);
        }

        return ret == 0 ? 0 : 3;
    }

    if(argc <= at + 4)
        return 3;

    int fmt = -1;

    for(int f = 0; f < NFMT; ++f)
        if(format_name(f) == argv[at + 1] || !strcmp(zq30_formats[f].first, argv[at + 1]))
            fmt = f;

    int ret = 3;
    std::string why = "no such 3.0 build";
    long made = -1;

    if(fmt >= 0)
    {
        ret = load_quest(argv[at + 2], true, true);

        if(ret == qe_OK)
        {
            made = zq_made_order_30();
            why.clear();
            ret = save_as_format(fmt, argv[at + 3], why);
        }
        else
            why = "the quest didn't load";
    }

    FILE *out = fopen(argv[at + 4], "w");

    if(out)
    {
        fprintf(out, "%s as %s: %s\nmade order %ld\n%s\n", argv[at + 2], argv[at + 1], ret == 0 ? "SAVED" : "NOT SAVED",
                made, why.c_str());
        fclose(out);
    }

    return ret == 0 ? 0 : 3;
}

// Compiling a 3.0 Nightly quest's ZScript in ZQuest: the values placed scripts hold for a
// script's exported variables, which a recompile has to move when the variables move.
//
// A script's instance variables are stored by index, in declaration order, and the values set
// per placed script (on FFCs, screens, combos and their weapons, items, enemies, DMaps,
// subscreens and their widgets, save menu options, init data) are keyed by that index. This is
// upstream ZQuest's for_each_inst_init and apply_slot_remaps (src/zq/script_slot_remap.cpp) and
// its apply_inst_var_remap and apply_initd_moves (components/zasm/inst_var_remap.cpp): every
// value follows its variable to the new index (converted when the variable changed between int
// and long), values of variables that are gone are dropped, and a script converted from run()
// parameters gets its InitD values copied to the variables chosen for them.
//
// ZQuest holds these quests' sections as records in the quest's own section versions
// (zq_tape.h), so the values are moved in the records' bytes: each record is walked by its
// schema, which reports every script config (zq_tape_inst_hook), the configs' value lists are
// written again, and the record goes back the way its editor puts an edited record in (and is
// read back by the section's reader). Nothing is touched unless every record could be worked
// out first.
//
// Written for the port (ported logic from upstream ZQuest Classic 3.0, GPL; no jwin or Allegro
// code).

#include "precompiled.h" //always first

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "q255.h"
#include "q30_triggers.h"
#include "zq_compile.h"
#include "zq_q255_store.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zq_tape.h"

extern bool saved;
extern itemdata *itemsbuf;
extern std::vector<mapscr> TheMaps;
bool zq255_item_records_all(std::vector<std::vector<byte> > &recs, int *version);      // zqw_items.cpp
bool zq255_item_commit(int i, const std::vector<byte> &rec, std::string &why);
bool zq255_guy_records_all(std::vector<std::vector<byte> > &recs, int *version);       // zqw_guys.cpp
bool zq255_guy_commit(int i, const std::vector<byte> &rec, std::string &why);
bool zq255_dmap_records_all(std::vector<std::vector<byte> > &recs, int *version);      // zqw_maps.cpp
bool zq255_dmap_commit(int i, const std::vector<byte> &rec, std::string &why);
bool zq255_screen_records_all(std::vector<std::vector<byte> > &recs, int *version, std::vector<int> *indices);
bool zq255_screen_fields_apply(int index, int version, std::vector<byte> &rec);
bool zq255_init_record(std::vector<byte> &rec, int *version);                          // zqw_init.cpp
bool zq255_init_commit(const std::vector<byte> &rec, std::string &why);
bool zq255_subs_record(std::vector<byte> &rec, int *version);
bool zq255_subs_section(std::vector<byte> &body, int *version, std::string &why);
bool zq255_subs_put_section(const std::vector<byte> &body, std::string &why);
bool zq255_misc_commit(const std::map<std::string, TapeField> &edits, std::string &why);   // zqw_basic.cpp

// What zq_remap_prepare worked out: every record the plan changes, not yet put in.
struct ZqRemapPending
{
    std::map<int, std::vector<byte> > items, guys, dmaps;
    std::map<int, std::map<std::string, TapeField> > screens;   // per screen, the fields that change
    bool init_changed, subs_changed;
    std::vector<byte> init, subs;
    std::map<std::string, TapeField> misc;                      // the MISC fields that change
    struct ComboRaw
    {
        int combo, which;       // which: 0 the combo's script, 1 its lift weapon's, 2 its misc weapon's
        std::vector<byte> raw;
    };
    std::vector<ComboRaw> combos;
    int updated, dropped, copied;

    ZqRemapPending() : init_changed(false), subs_changed(false), updated(0), dropped(0), copied(0) {}
};

namespace
{

typedef std::map<word, int32_t> InstMap;
typedef bool (*Schema)(TapeParser &, int);

enum InstSection { IS_ITEM, IS_GUY, IS_DMAP, IS_SCREEN, IS_INIT, IS_MISC, IS_SUBS, IS_COMBO };

bool key_is(const char *key, const char *want)
{
    return strcmp(key, want) == 0;
}

// The slot type a script config is for, by the section and its field key (upstream's
// for_each_inst_init lists the same configs). -1: not one of them.
int inst_type(int section, const char *key)
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
        return key_is(key, "init.global") ? ZQS_GLOBAL : key_is(key, "init.hero") ? ZQS_HERO : -1;
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

// One script config of a record: its run arguments and where its values are (pairs of a word
// variable id and an int, their word count just before them).
struct Site
{
    int type, slot;
    long args[8];
    size_t at, pairs;
    std::string key;
};

struct Walk
{
    const byte *base;
    int section;
    std::vector<Site> sites;
    // init data keeps the generic scripts' run arguments and values apart, each by slot
    std::map<int, std::vector<long> > gen_args;
    bool gen_seen;
    size_t gen_at, gen_len;

    Walk() : base(NULL), section(0), gen_seen(false), gen_at(0), gen_len(0) {}
};

Walk *g_walk = NULL;

void site_hook(const char *key, int script, const long *initd, const byte *inst, size_t pairs)
{
    Walk &w = *g_walk;

    if(key_is(key, "init.gen_initd"))
    {
        if(initd)
            w.gen_args[script].assign(initd, initd + 8);
        return;
    }

    if(key_is(key, "init.gen_inst.map"))    // the whole container: its place and size
    {
        w.gen_seen = true;
        w.gen_at = (size_t)(inst - w.base);
        w.gen_len = pairs;
        return;
    }

    if(key_is(key, "init.gen_inst") || !inst)
        return;

    const int type = inst_type(w.section, key);

    // a config without a script isn't one (upstream's visit); global and hero slots are their index
    if(type < 0 || (script <= 0 && type != ZQS_GLOBAL && type != ZQS_HERO))
        return;

    Site s;
    s.type = type;
    s.slot = script;
    s.at = (size_t)(inst - w.base);
    s.pairs = pairs;
    s.key = key;

    for(int q = 0; q < 8; ++q)
        s.args[q] = initd ? initd[q] : 0;

    w.sites.push_back(s);
}

int32_t get32(const byte *b)
{
    return (int32_t)((dword)b[0] | ((dword)b[1] << 8) | ((dword)b[2] << 16) | ((dword)b[3] << 24));
}

void put16(std::vector<byte> &o, unsigned v)
{
    o.push_back((byte)v);
    o.push_back((byte)(v >> 8));
}

void put32(std::vector<byte> &o, dword v)
{
    put16(o, v & 0xFFFF);
    put16(o, v >> 16);
}

InstMap read_pairs(const byte *b, size_t n)
{
    InstMap m;

    for(size_t q = 0; q < n; ++q)
        m[(word)(b[q * 6] | (b[q * 6 + 1] << 8))] = get32(b + q * 6 + 2);

    return m;
}

void put_pairs(std::vector<byte> &o, const InstMap &m)
{
    for(InstMap::const_iterator it = m.begin(); it != m.end(); ++it)
    {
        put16(o, it->first);
        put32(o, (dword)it->second);
    }
}

// What is done with every placed script's values. where: for a dump.
struct Visitor
{
    virtual ~Visitor() {}
    virtual void visit(int type, int slot, InstMap &inst, const long *args, const std::string &where) = 0;
};

// A piece of a record to replace.
struct Splice
{
    size_t at, len;
    std::vector<byte> bytes;
    bool operator<(const Splice &o) const { return at > o.at; }     // last one first
};

// Run arguments set in 2.53's own dialogs since the load (an FFC's, an item's), which the
// record doesn't show yet: they are what a save writes.
void newest_args(int section, int owner, Site &s)
{
    int ffc = -1;

    if(section == IS_SCREEN && sscanf(s.key.c_str(), "ffc%d.sc", &ffc) == 1 && ffc >= 0 && ffc < 32 &&
       owner >= 0 && (size_t)owner < TheMaps.size() && TheMaps[owner].ffscript[ffc] == s.slot)
    {
        for(int q = 0; q < 8; ++q)
            s.args[q] = TheMaps[owner].initd[ffc][q];
    }
    else if(section == IS_ITEM && s.key == "sc.action" && itemsbuf && owner >= 0 && owner < MAXITEMS &&
            itemsbuf[owner].script == s.slot)
    {
        for(int q = 0; q < 8; ++q)
            s.args[q] = itemsbuf[owner].initiald[q];
    }
}

// Init data's generic script values (upstream's bounded_map<word, std::map<word, int>>, written
// by p_putbmap): the slots' run arguments are in another container, and a slot with only run
// arguments can gain values, so the whole container is read and, when it changed, written again
// the way upstream writes it.
void generic_values(Walk &w, const std::vector<byte> &rec, Visitor &vis, std::vector<Splice> &sp)
{
    const byte *b = &rec[w.gen_at];
    const unsigned size = b[0] | (b[1] << 8);

    if(!size || w.gen_len < 5)
        return;     // a container that holds nothing: no place for values

    const bool pairs = b[2] != 0;
    const size_t count = b[3] | (b[4] << 8);
    std::map<int, InstMap> all;
    size_t pos = 5;

    for(size_t q = 0; q < count && pos + (pairs ? 6 : 4) <= w.gen_len; ++q)
    {
        const int slot = pairs ? (b[pos] | (b[pos + 1] << 8)) : (int)q;
        pos += pairs ? 2 : 0;
        const size_t n = (size_t)(dword)get32(b + pos);
        pos += 4;

        if(n > (w.gen_len - pos) / 6)
            return;

        if(n)
            all[slot] = read_pairs(b + pos, n);

        pos += n * 6;
    }

    std::set<int> slots;

    for(std::map<int, InstMap>::const_iterator it = all.begin(); it != all.end(); ++it)
        slots.insert(it->first);

    for(std::map<int, std::vector<long> >::const_iterator it = w.gen_args.begin(); it != w.gen_args.end(); ++it)
        slots.insert(it->first);

    bool changed = false;
    char where[48];

    for(std::set<int>::const_iterator it = slots.begin(); it != slots.end(); ++it)
    {
        static const long none[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        InstMap m = all.count(*it) ? all[*it] : InstMap();
        const InstMap was = m;
        snprintf(where, sizeof(where), "init generic");
        vis.visit(ZQS_GENERIC, *it, m, w.gen_args.count(*it) ? &w.gen_args[*it][0] : none, where);

        if(m != was && *it >= 0 && (unsigned)*it < size)
        {
            changed = true;

            if(m.empty())
                all.erase(*it);
            else
                all[*it] = m;
        }
    }

    if(!changed)
        return;

    // p_putbmap: pairs of a key and a value when that is no bigger than every value up to the
    // last key (16: what upstream's value type takes in its 64-bit Windows build)
    const size_t dense = all.empty() ? 0 : (size_t)all.rbegin()->first + 1;
    const bool as_pairs = all.size() * (16 + 2) <= dense * 16;
    Splice x;
    x.at = w.gen_at;
    x.len = w.gen_len;
    put16(x.bytes, size);
    x.bytes.push_back(as_pairs ? 1 : 0);
    put16(x.bytes, (unsigned)(as_pairs ? all.size() : dense));

    if(as_pairs)
        for(std::map<int, InstMap>::const_iterator it = all.begin(); it != all.end(); ++it)
        {
            put16(x.bytes, (unsigned)it->first);
            put32(x.bytes, (dword)it->second.size());
            put_pairs(x.bytes, it->second);
        }
    else
        for(size_t q = 0; q < dense; ++q)
        {
            std::map<int, InstMap>::const_iterator it = all.find((int)q);
            put32(x.bytes, it == all.end() ? 0 : (dword)it->second.size());

            if(it != all.end())
                put_pairs(x.bytes, it->second);
        }

    sp.push_back(x);
}

// Walks one record, shows every placed script's values to the visitor and lays the record out
// again with what it changed. False: the record can't be walked, before or after.
bool process(int section, Schema schema, int v, std::vector<byte> &rec, Visitor &vis, bool &changed, int owner,
             const char *what)
{
    changed = false;

    if(rec.empty())
        return true;

    Walk w;
    w.base = &rec[0];
    w.section = section;
    size_t end = 0;
    {
        Tape t;
        TapeParser p(&rec[0], rec.size(), 0, &t);
        g_walk = &w;
        zq_tape_inst_hook = site_hook;
        const bool ok = schema(p, v) && !p.bad;
        zq_tape_inst_hook = NULL;
        g_walk = NULL;

        if(!ok)
            return false;

        end = p.pos;
    }

    std::vector<Splice> sp;
    char where[160];

    for(size_t i = 0; i < w.sites.size(); ++i)
    {
        Site &s = w.sites[i];

        if(s.at < 2 || s.at + s.pairs * 6 > rec.size())
            return false;

        newest_args(section, owner, s);
        InstMap m = read_pairs(&rec[s.at], s.pairs);
        const InstMap was = m;
        snprintf(where, sizeof(where), "%s %d %s", what, owner, s.key.c_str());
        vis.visit(s.type, s.slot, m, s.args, where);

        if(m == was)
            continue;

        if(m.size() > 0xFFFF)
            return false;

        Splice x;
        x.at = s.at - 2;
        x.len = 2 + s.pairs * 6;
        put16(x.bytes, (unsigned)m.size());
        put_pairs(x.bytes, m);
        sp.push_back(x);
    }

    if(w.gen_seen && w.gen_at + w.gen_len <= rec.size() && w.gen_len >= 2)
        generic_values(w, rec, vis, sp);

    if(sp.empty())
        return true;

    std::sort(sp.begin(), sp.end());
    long grown = 0;

    for(size_t i = 0; i < sp.size(); ++i)
    {
        rec.erase(rec.begin() + sp[i].at, rec.begin() + sp[i].at + sp[i].len);
        rec.insert(rec.begin() + sp[i].at, sp[i].bytes.begin(), sp[i].bytes.end());
        grown += (long)sp[i].bytes.size() - (long)sp[i].len;
    }

    changed = true;

    // the record as it is now walks the same way, to where it should end
    Tape t;
    TapeParser p(&rec[0], rec.size(), 0, &t);
    return schema(p, v) && !p.bad && (long)p.pos == (long)end + grown;
}

bool tape_of(Schema schema, int v, const std::vector<byte> &rec, Tape &t)
{
    TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);
    return schema(p, v) && !p.bad;
}

// The fields of 'now' that 'was' doesn't hold the same.
bool changed_fields(Schema schema, int v, const std::vector<byte> &was, const std::vector<byte> &now,
                    std::map<std::string, TapeField> &out)
{
    Tape a, b;

    if(!tape_of(schema, v, was, a) || !tape_of(schema, v, now, b))
        return false;

    const TapeIndex ia(a);

    for(size_t i = 0; i < b.f.size(); ++i)
    {
        const TapeField *x = ia.find(b.f[i].key);

        if(!x || x->kind != b.f[i].kind || x->value != b.f[i].value || x->bytes != b.f[i].bytes)
            out[b.f[i].key] = b.f[i];
    }

    return true;
}

// upstream's is_misc_lweapon (zq/dialog/comboeditor.cpp): whether a combo's misc weapon is
// the player's (an LWeapon script) or an enemy's.
bool misc_is_lweapon(const Q30Combo &c)
{
    const long id = c.attributes[9] / 10000;

    if(c.type == Q30C_STEPSFX)
    {
        if(!(c.usrflags & 0x01))
            return false;   // no weapon

        if(id >= 128)       // wEnemyWeapons
            return false;

        if(id >= 31 && id <= 40)    // wScript1 to wScript10
            return (c.usrflags & 0x02) != 0;

        return true;
    }

    if(c.type == Q30C_SHOOTER)
    {
        if(id >= 128)
            return false;

        if(id >= 31 && id <= 40)
            return (c.usrflags & 0x08) != 0;

        return true;
    }

    return false;
}

// A combo config's values, kept as bytes with their word count in front (empty: none).
void combo_config(int combo, int which, int type, int script, const long *initd, const std::vector<byte> &raw, Visitor &vis,
                  ZqRemapPending *out)
{
    if(script <= 0)
        return;

    const size_t pairs = raw.size() >= 2 ? (size_t)(raw[0] | (raw[1] << 8)) : 0;

    if(raw.size() < 2 + pairs * 6)
        return;

    InstMap m = pairs ? read_pairs(&raw[2], pairs) : InstMap();
    const InstMap was = m;
    char where[64];
    snprintf(where, sizeof(where), "combo %d %s", combo, which == 0 ? "sc" : which == 1 ? "lift_weap" : "misc_weap");
    vis.visit(type, script, m, initd, where);

    if(m == was || !out || m.size() > 0xFFFF)
        return;

    ZqRemapPending::ComboRaw c;
    c.combo = combo;
    c.which = which;
    put16(c.raw, (unsigned)m.size());
    put_pairs(c.raw, m);
    out->combos.push_back(c);
}

std::string fail(const char *what, int i = -1)
{
    return std::string("the values placed scripts hold in the quest's ") + what +
           (i >= 0 ? " (number " + std::to_string(i) + ")" : std::string()) + " can't be read or written again";
}

// Every placed script of the quest as ZQuest holds it now. out: where the records the visitor
// changed go (NULL: it changes nothing). False (why) when something couldn't be walked.
bool run_all(Visitor &vis, ZqRemapPending *out, std::string &why)
{
    std::vector<std::vector<byte> > recs;
    std::vector<byte> rec;
    int v = 0;
    bool changed = false;

    if(zq_find_part(ID_ITEMS))
    {
        if(!zq255_item_records_all(recs, &v))
            return why = fail("items"), false;

        for(size_t i = 0; i < recs.size(); ++i)
        {
            if(!process(IS_ITEM, zq_tape_item, v, recs[i], vis, changed, (int)i, "item"))
                return why = fail("items", (int)i), false;

            if(changed && out)
                out->items[(int)i] = recs[i];
        }
    }

    if(zq_find_part(ID_GUYS))
    {
        if(!zq255_guy_records_all(recs, &v))
            return why = fail("enemies"), false;

        for(size_t i = 0; i < recs.size(); ++i)
        {
            if(!process(IS_GUY, zq_tape_guy, v, recs[i], vis, changed, (int)i, "enemy"))
                return why = fail("enemies", (int)i), false;

            if(changed && out)
                out->guys[(int)i] = recs[i];
        }
    }

    if(zq_find_part(ID_DMAPS))
    {
        if(!zq255_dmap_records_all(recs, &v))
            return why = fail("DMaps"), false;

        for(size_t i = 0; i < recs.size(); ++i)
        {
            if(!process(IS_DMAP, zq_tape_dmap, v, recs[i], vis, changed, (int)i, "dmap"))
                return why = fail("DMaps", (int)i), false;

            if(changed && out)
                out->dmaps[(int)i] = recs[i];
        }
    }

    if(zq_find_part(ID_MAPS))
    {
        std::vector<int> at;

        if(!zq255_screen_records_all(recs, &v, &at))
            return why = fail("screens"), false;

        for(size_t i = 0; i < recs.size(); ++i)
        {
            // with the fields edited since the load
            if(zq255.screen_fields.count(at[i]) && !zq255_screen_fields_apply(at[i], v, recs[i]))
                return why = fail("screens", at[i]), false;

            const std::vector<byte> was = out ? recs[i] : std::vector<byte>();

            if(!process(IS_SCREEN, zq_tape_screen, v, recs[i], vis, changed, at[i], "screen"))
                return why = fail("screens", at[i]), false;

            if(changed && out && !changed_fields(zq_tape_screen, v, was, recs[i], out->screens[at[i]]))
                return why = fail("screens", at[i]), false;
        }
    }

    if(zq255_init_record(rec, &v))
    {
        if(!process(IS_INIT, zq_tape_init, v, rec, vis, changed, 0, "init"))
            return why = fail("init data"), false;

        if(changed && out)
        {
            out->init_changed = true;
            out->init = rec;
        }
    }

    // misc data and the subscreens as a save writes them now (their editors' edits in)
    const ZqQuestPart *misc = zq_find_part(ID_MISC);
    std::vector<byte> sec;

    if(misc && zq_own_part_bytes(*misc, sec) && sec.size() >= 8 && (sec[0] | (sec[1] << 8)) >= 22)
    {
        v = sec[0] | (sec[1] << 8);
        rec.assign(sec.begin() + 8, sec.end());
        const std::vector<byte> was = rec;

        if(!process(IS_MISC, zq_tape_misc, v, rec, vis, changed, 0, "misc"))
            return why = fail("misc data"), false;

        if(changed && out && !changed_fields(zq_tape_misc, v, was, rec, out->misc))
            return why = fail("misc data"), false;
    }

    if(zq255_subs_record(rec, &v))
    {
        std::string swhy;

        if(!zq255_subs_section(rec, &v, swhy) || !process(IS_SUBS, zq_tape_subscreens, v, rec, vis, changed, 0, "subscreens"))
            return why = fail("subscreens"), false;

        if(changed && out)
        {
            out->subs_changed = true;
            out->subs = rec;
        }
    }

    for(int i = 0; i < MAXCOMBOS; ++i)
    {
        const Q30Combo *c = zq255.combos[i];

        if(!c)
            continue;

        combo_config(i, 0, ZQS_COMBO, c->script, c->initd, c->script_inst_raw, vis, out);
        combo_config(i, 1, ZQS_LWEAPON, c->lift_weap.script, c->lift_weap.initd, c->lift_weap.inst_raw, vis, out);
        combo_config(i, 2, misc_is_lweapon(*c) ? ZQS_LWEAPON : ZQS_EWEAPON, c->misc_weap.script, c->misc_weap.initd,
                     c->misc_weap.inst_raw, vis, out);
    }

    return true;
}

// Puts the worked-out records in. False (why: the first one that didn't go in).
bool put_in(ZqRemapPending &p, std::string &why)
{
    bool ok = true;
    std::string w;
#define ZQ_PUT(call) if(!(call) && ok) { ok = false; why = w; }

    for(std::map<int, std::vector<byte> >::const_iterator it = p.items.begin(); it != p.items.end(); ++it)
        ZQ_PUT(zq255_item_commit(it->first, it->second, w))

    for(std::map<int, std::vector<byte> >::const_iterator it = p.guys.begin(); it != p.guys.end(); ++it)
        ZQ_PUT(zq255_guy_commit(it->first, it->second, w))

    for(std::map<int, std::vector<byte> >::const_iterator it = p.dmaps.begin(); it != p.dmaps.end(); ++it)
        ZQ_PUT(zq255_dmap_commit(it->first, it->second, w))

    for(std::map<int, std::map<std::string, TapeField> >::const_iterator it = p.screens.begin(); it != p.screens.end(); ++it)
    {
        std::map<std::string, TapeField> &f = zq255.screen_fields[it->first];

        for(std::map<std::string, TapeField>::const_iterator e = it->second.begin(); e != it->second.end(); ++e)
            f[e->first] = e->second;
    }

    if(p.init_changed)
        ZQ_PUT(zq255_init_commit(p.init, w))

    if(!p.misc.empty())
    {
        std::map<std::string, TapeField> edits = zq255.misc_fields;

        for(std::map<std::string, TapeField>::const_iterator e = p.misc.begin(); e != p.misc.end(); ++e)
            edits[e->first] = e->second;

        ZQ_PUT(zq255_misc_commit(edits, w))
    }

    if(p.subs_changed)
        ZQ_PUT(zq255_subs_put_section(p.subs, w))

#undef ZQ_PUT

    for(size_t i = 0; i < p.combos.size(); ++i)
    {
        Q30Combo *c = zq255.combos[p.combos[i].combo];

        if(!c)
            continue;

        (p.combos[i].which == 0 ? c->script_inst_raw : p.combos[i].which == 1 ? c->lift_weap.inst_raw : c->misc_weap.inst_raw) =
            p.combos[i].raw;
    }

    saved = false;
    return ok;
}

const ZqSlotRemap *remap_of(const std::vector<ZqSlotRemap> &plan, int type, int slot)
{
    for(size_t i = 0; i < plan.size(); ++i)
        if(plan[i].type == type && plan[i].slot == slot)
            return &plan[i];

    return NULL;
}

// upstream's plan_slot_remaps, its second half: how many placed scripts hold a value for each
// variable that has no new place, and a run argument for each parameter of a converted script.
struct CountVisitor : Visitor
{
    std::vector<ZqSlotRemap> &plan;
    explicit CountVisitor(std::vector<ZqSlotRemap> &p) : plan(p) {}

    void visit(int type, int slot, InstMap &inst, const long *args, const std::string &)
    {
        for(size_t i = 0; i < plan.size(); ++i)
        {
            ZqSlotRemap &r = plan[i];

            if(r.type != type || r.slot != slot)
                continue;

            const std::vector<ZqInstVar> uo = r.unmatched_old();

            for(size_t k = 0; k < uo.size(); ++k)
                if(inst.count(uo[k].id))
                    r.unmatched_value_counts[uo[k].id]++;

            for(size_t k = 0; k < r.remap.old_params.size(); ++k)
            {
                const int id = r.remap.old_params[k].id;

                if(id < 8 && args[id])
                    r.initd_value_counts[id]++;
            }
        }
    }
};

// upstream's convert_inst_var_value: a value keeps its number when its variable changed
// between int (stored * 10000) and long.
int32_t convert_value(int32_t value, int from, int to)
{
    if(from == 1 && to == 2)
        return value / 10000;

    if(from == 2 && to == 1)
    {
        const int64_t big = (int64_t)value * 10000;
        return (int32_t)(big < INT32_MIN ? INT32_MIN : big > INT32_MAX ? INT32_MAX : big);
    }

    return value;
}

int kind_of(const std::map<word, ZqInstVar> &vars, word id)
{
    std::map<word, ZqInstVar>::const_iterator it = vars.find(id);
    return it == vars.end() ? 0 : it->second.kind;
}

// upstream's apply_inst_var_remap and apply_initd_moves on every placed script of a remapped slot.
struct ApplyVisitor : Visitor
{
    const std::vector<ZqSlotRemap> &plan;
    int updated, dropped, copied;
    explicit ApplyVisitor(const std::vector<ZqSlotRemap> &p) : plan(p), updated(0), dropped(0), copied(0) {}

    void visit(int type, int slot, InstMap &inst, const long *args, const std::string &)
    {
        const ZqSlotRemap *r = remap_of(plan, type, slot);

        if(!r)
            return;

        const ZqVarRemap &m = r->remap;
        InstMap moved;

        for(InstMap::const_iterator it = inst.begin(); it != inst.end(); ++it)
        {
            std::map<word, word>::const_iterator to = m.old_to_new.find(it->first);

            if(to == m.old_to_new.end())
            {
                ++dropped;
                continue;
            }

            const int32_t val = convert_value(it->second, kind_of(m.old_vars, to->first), kind_of(m.new_vars, to->second));

            if(to->second != to->first || val != it->second)
                ++updated;

            moved[to->second] = val;
        }

        // InitD values go to their variables, except where a value is set already; InitD
        // itself is left as it is
        for(size_t i = 0; i < m.initd_moves.size(); ++i)
        {
            const ZqInitDMove &mv = m.initd_moves[i];

            if(moved.count(mv.new_id) || mv.initd < 0 || mv.initd >= 8)
                continue;

            moved[mv.new_id] = convert_value((int32_t)args[mv.initd], mv.kind, kind_of(m.new_vars, mv.new_id));
            ++copied;
        }

        inst.swap(moved);
    }
};

// Testing: ZQ_COMPILE_SET_INST="type,slot,variable,value;..." sets a value on every placed
// script of a slot (there is no editor for them here yet).
struct SetVisitor : Visitor
{
    struct One { int type, slot, var; long value; };
    std::vector<One> list;
    int set;
    SetVisitor() : set(0) {}

    void visit(int type, int slot, InstMap &inst, const long *, const std::string &)
    {
        for(size_t i = 0; i < list.size(); ++i)
            if(list[i].type == type && list[i].slot == slot)
            {
                inst[(word)list[i].var] = (int32_t)list[i].value;
                ++set;
            }
    }
};

struct DumpVisitor : Visitor
{
    FILE *f;
    explicit DumpVisitor(FILE *file) : f(file) {}

    void visit(int type, int slot, InstMap &inst, const long *args, const std::string &where)
    {
        bool any = !inst.empty();

        for(int q = 0; q < 8; ++q)
            any = any || args[q];

        if(!any)
            return;

        fprintf(f, "%s slot %d at %s: args", zq_slot_type(type).name, slot, where.c_str());

        for(int q = 0; q < 8; ++q)
            fprintf(f, " %ld", args[q]);

        fprintf(f, " values");

        for(InstMap::const_iterator it = inst.begin(); it != inst.end(); ++it)
            fprintf(f, " %d=%ld", (int)it->first, (long)it->second);

        fprintf(f, "\n");
    }
};

} // namespace

bool zq_remap_count(std::vector<ZqSlotRemap> &plan, std::string &why)
{
    CountVisitor v(plan);
    return run_all(v, NULL, why);
}

ZqRemapPending *zq_remap_prepare(const std::vector<ZqSlotRemap> &plan, std::string &why)
{
    ZqRemapPending *p = new ZqRemapPending;
    ApplyVisitor v(plan);

    if(!run_all(v, p, why))
    {
        delete p;
        return NULL;
    }

    p->updated = v.updated;
    p->dropped = v.dropped;
    p->copied = v.copied;
    return p;
}

void zq_remap_discard(ZqRemapPending *pending)
{
    delete pending;
}

bool zq_remap_commit(ZqRemapPending *pending, const std::vector<ZqSlotRemap> &plan, std::string &log, std::string &why)
{
    const bool ok = put_in(*pending, why);
    char line[400];

    // upstream's messages (apply_slot_remaps)
    for(size_t i = 0; i < plan.size(); ++i)
    {
        const ZqSlotRemap &r = plan[i];
        const std::vector<ZqInstVar> uo = r.unmatched_old();

        for(size_t k = 0; k < uo.size(); ++k)
            if(r.unmatched_value_counts.count(uo[k].id))
            {
                snprintf(line, sizeof(line), "%s script '%s': discarded the values of variable '%s'\n", zq_slot_type(r.type).name,
                         r.script_name.c_str(), uo[k].name.c_str());
                log += line;
            }

        for(size_t k = 0; k < r.remap.old_params.size(); ++k)
        {
            const ZqInstVar &param = r.remap.old_params[k];
            bool moved = false;

            for(size_t q = 0; q < r.remap.initd_moves.size(); ++q)
                moved = moved || r.remap.initd_moves[q].initd == param.id;

            if(!moved && r.initd_value_counts.count(param.id))
            {
                snprintf(line, sizeof(line), "%s script '%s': the InitD values of parameter '%s' weren't copied to a variable\n",
                         zq_slot_type(r.type).name, r.script_name.c_str(), param.name.c_str());
                log += line;
            }
        }
    }

    if(pending->updated || pending->dropped)
    {
        snprintf(line, sizeof(line), "Updated %d and discarded %d script variable values to match the recompiled scripts\n",
                 pending->updated, pending->dropped);
        log += line;
    }

    if(pending->copied)
    {
        snprintf(line, sizeof(line), "Copied %d InitD values to the script variables of the same name\n", pending->copied);
        log += line;
    }

    delete pending;
    return ok;
}

bool zq_remap_dump(const char *path)
{
    FILE *f = fopen(path, "w");

    if(!f)
        return false;

    DumpVisitor v(f);
    std::string why;
    const bool ok = run_all(v, NULL, why);

    if(!ok)
        fprintf(f, "not walked: %s\n", why.c_str());

    fclose(f);
    return ok;
}

// Testing (see SetVisitor). How many values were set, -1 when the quest couldn't be walked or
// the records didn't go back in.
int zq_remap_test_set(const char *spec, std::string &why)
{
    SetVisitor v;
    int len = 0;
    SetVisitor::One one;

    while(sscanf(spec, "%d,%d,%d,%ld%n", &one.type, &one.slot, &one.var, &one.value, &len) == 4)
    {
        v.list.push_back(one);
        spec += len;

        if(*spec != ';')
            break;

        ++spec;
    }

    ZqRemapPending p;

    if(!run_all(v, &p, why) || !put_in(p, why))
        return -1;

    return v.set;
}

// The GUY section (enemy data and enemy names), written back in the section version the
// quest was made in (see zq_writers.h). Two readers to mirror:
//   - src/qst.cpp's readguys: sectioned quests up to 2.53 (1.93 to 2.50.x headers), and
//     2.53.x quests at a section version older than 2.53's own (2.53's own version is left
//     to ZQuest's 2.53 writer). Versions 1 and 3 hold no enemies at all, only the section's
//     version: the enemies are the built-in ones, so nothing about them can be changed;
//   - src/port/game/q255_guys.cpp: 2.55 and 3.0 Nightly quests (versions 24 to 60).
//
// Done the same way as the items (zqw_items.cpp): each record is rebuilt from its original
// bytes, with only the changed fields encoded (every field, when forced), then the section
// is read back by the reader itself and compared with ZQuest's data; anything that doesn't
// come back stops the save. Before version 23 the file holds 256 enemies, so the rest can't
// be changed; before version 24 the reader rewrites a good many fields of some enemy kinds
// (frame rates, defenses, movement of keese, peahats and the like), which then can't be
// changed for those enemies either.
//
// One thing ZQuest itself changes after the load: its 2.53 writer, which the load runs to
// tell later whether a part changed, gives an enemy with no hit sound the old hard-coded one
// (WAV_EHIT) in quests older than 2.53.0 Gamma 2. What was loaded is compared after that,
// and the file keeps its "none" (see stored_hitsfx).

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "sfx.h"
#include "qst.h"
#include "q255.h"
#include "zq_q255_store.h"
#include "zq_writers.h"
#include "zqw_itemguy.h"

extern guydata *guysbuf;
extern char *guy_string[eMAXGUYS];
extern word quest_header_zelda_version, quest_header_zelda_build;

namespace
{

using namespace zqw_ig;

const char *diff_guy(const guydata &a, const guydata &b)
{
#define CMP(x) if(a.x != b.x) return #x;
    CMP(flags) CMP(flags2) CMP(tile) CMP(width) CMP(height) CMP(s_tile) CMP(s_width) CMP(s_height)
    CMP(e_tile) CMP(e_width) CMP(e_height) CMP(hp) CMP(family) CMP(cset) CMP(anim) CMP(e_anim)
    CMP(frate) CMP(e_frate) CMP(dp) CMP(wdp) CMP(weapon) CMP(rate) CMP(hrate) CMP(step)
    CMP(homing) CMP(grumble) CMP(item_set) CMP(misc1) CMP(misc2) CMP(misc3) CMP(misc4) CMP(misc5)
    CMP(misc6) CMP(misc7) CMP(misc8) CMP(misc9) CMP(misc10) CMP(misc11) CMP(misc12) CMP(misc13)
    CMP(misc14) CMP(misc15) CMP(bgsfx) CMP(bosspal) CMP(extend) CMP(hitsfx) CMP(deadsfx)
#undef CMP

    for(int j = 0; j < edefLAST; ++j)
        if(a.defense[j] != b.defense[j])
            return "defense";

    return NULL;
}

// What writeguys (zq_class.cpp) does to the enemies of quests older than 2.53.0 Gamma 2
// every time it runs, which the load does to fingerprint the part.
bool hit_sound_filled_in()
{
    return quest_header_zelda_version < 0x250 || (quest_header_zelda_version == 0x250 && quest_header_zelda_build < 32);
}

void as_253_writer_leaves_them(guydata *g, int n)
{
    if(hit_sound_filled_in())
        for(int i = 0; i < n; ++i)
            if(g[i].hitsfx == 0)
                g[i].hitsfx = WAV_EHIT;
}

// The hit sound to store. Where the file has none and ZQuest filled in WAV_EHIT, none stays:
// the 2.50 player plays WAV_EHIT and then the enemy's own sound, so storing WAV_EHIT would
// play it twice.
long stored_hitsfx(byte now, byte raw)
{
    return (hit_sound_filled_in() && now == WAV_EHIT && raw == 0) ? 0 : now;
}

// ---- what was loaded ------------------------------------------------------------------

guydata g_guys[MAXGUYS];            // guysbuf as loaded (or as last saved)
char g_names[MAXGUYS][64];

void snapshot()
{
    for(int i = 0; i < MAXGUYS; ++i)
    {
        g_guys[i] = guysbuf[i];
        memcpy(g_names[i], guy_string[i], 64);
    }

    as_253_writer_leaves_them(g_guys, MAXGUYS);
}

ZqLoadHookRegistration g_hook(snapshot);

bool guy_changed(int i)
{
    return diff_guy(guysbuf[i], g_guys[i]) != NULL || strcmp(guy_string[i], g_names[i]) != 0;
}

// ZQuest's buffers while a reader runs on what was written: put back afterwards.
struct SavedGuys
{
    std::vector<guydata> guys;
    std::vector<std::string> names;     // all 64 bytes of each
    std::map<int, std::vector<byte> > defenses;
    std::map<int, Q30ScriptRef> script, weapon_script;
    std::map<int, dword> moveflags;
    std::map<int, int> wpnsprite;

    void take()
    {
        guys.assign(guysbuf, guysbuf + MAXGUYS);
        names.resize(MAXGUYS);

        for(int i = 0; i < MAXGUYS; ++i)
            names[i].assign(guy_string[i], 64);

        defenses = zq255.guy_defenses;
        script = zq255.guy_script;
        weapon_script = zq255.guy_weapon_script;
        moveflags = zq255.guy_moveflags;
        wpnsprite = zq255.guy_wpnsprite;
    }

    void put() const
    {
        for(int i = 0; i < MAXGUYS; ++i)
        {
            guysbuf[i] = guys[i];
            memcpy(guy_string[i], names[i].data(), 64);
        }

        zq255.guy_defenses = defenses;
        zq255.guy_script = script;
        zq255.guy_weapon_script = weapon_script;
        zq255.guy_moveflags = moveflags;
        zq255.guy_wpnsprite = wpnsprite;
    }
};

// ---- qst.cpp's readguys (sectioned quests up to 2.53) ---------------------------------

// One enemy as readguys reads it for section version v (above 3).
void guy_old(Rec &r, int v, const guydata &c, const guydata &p)
{
    r.field(4, (long)c.flags, (long)p.flags);
    r.field(4, (long)c.flags2, (long)p.flags2);
    r.field(2, c.tile, p.tile);
    r.field(1, c.width, p.width);
    r.field(1, c.height, p.height);
    r.field(2, c.s_tile, p.s_tile);
    r.field(1, c.s_width, p.s_width);
    r.field(1, c.s_height, p.s_height);
    r.field(2, c.e_tile, p.e_tile);
    r.field(1, c.e_width, p.e_width);
    r.field(1, c.e_height, p.e_height);

    const short cs[16] = { c.hp, c.family, c.cset, c.anim, c.e_anim, c.frate, c.e_frate, c.dp, c.wdp, c.weapon,
                           c.rate, c.hrate, c.step, c.homing, c.grumble, c.item_set };
    const short ps[16] = { p.hp, p.family, p.cset, p.anim, p.e_anim, p.frate, p.e_frate, p.dp, p.wdp, p.weapon,
                           p.rate, p.hrate, p.step, p.homing, p.grumble, p.item_set };

    for(int j = 0; j < 16; ++j)
        r.field(2, cs[j], ps[j]);

    // Version 22 made the misc attributes 32-bit; before, they were signed words.
    const long cm[10] = { c.misc1, c.misc2, c.misc3, c.misc4, c.misc5, c.misc6, c.misc7, c.misc8, c.misc9, c.misc10 };
    const long pm[10] = { p.misc1, p.misc2, p.misc3, p.misc4, p.misc5, p.misc6, p.misc7, p.misc8, p.misc9, p.misc10 };

    for(int j = 0; j < 10; ++j)
        r.field(v >= 22 ? 4 : 2, cm[j], pm[j]);

    r.field(2, c.bgsfx, p.bgsfx);
    r.field(2, c.bosspal, p.bosspal);
    r.field(2, c.extend, p.extend);

    if(v >= 16)
        for(int j = 0; j < edefLAST; ++j)
            r.field(1, c.defense[j], p.defense[j]);

    if(v >= 18)
    {
        byte raw = (byte)r.peek(1);
        r.field(1, stored_hitsfx(c.hitsfx, raw), stored_hitsfx(p.hitsfx, raw));
        r.field(1, c.deadsfx, p.deadsfx);
    }

    if(v >= 19)
    {
        r.field(v >= 22 ? 4 : 2, c.misc11, p.misc11);
        r.field(v >= 22 ? 4 : 2, c.misc12, p.misc12);
    }
}

// Runs readguys on the section; d and names get what it read. False if it refused it.
bool read_back_old(const std::vector<byte> &bytes, const ZqPartContext &ctx, std::vector<guydata> &d, std::vector<std::string> &names)
{
    SavedGuys saved;
    saved.take();

    zquestheader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.zelda_version = ctx.zelda_version;
    hdr.build = ctx.build;

    MemFile m;
    PACKFILE *f = open_mem(m, bytes);
    int ret = f ? readguys(f, &hdr, true) : qe_nomem;
    bool whole = mem_used_up(m);

    if(f)
        pack_fclose(f);

    d.assign(guysbuf, guysbuf + MAXGUYS);
    names.resize(MAXGUYS);

    for(int i = 0; i < MAXGUYS; ++i)
        names[i] = guy_string[i];

    saved.put();
    as_253_writer_leaves_them(&d[0], MAXGUYS);
    return ret == 0 && whole;
}

// Versions 1 and 3: the section's version and size, and nothing else.
int encode_guys_bare(const ZqPartContext &ctx, const std::vector<byte> &orig, std::vector<byte> &out)
{
    if(orig.size() != 8)
        return ERR_PARSE;

    for(int i = 0; i < MAXGUYS; ++i)
        if(guy_changed(i))
        {
            zqw_ig_refuse("Enemies, section version %d: enemy %d (%s) was changed; this version only has the built-in enemies.\n", ctx.version, i, guy_string[i]);
            note("guys v%d: enemy %d changed (%s)\n", ctx.version, i, diff_guy(guysbuf[i], g_guys[i]) ? diff_guy(guysbuf[i], g_guys[i]) : "name");
            return ERR_CANT_HOLD;
        }

    out.clear();
    put_le(out, ctx.version, 2);
    put_le(out, ctx.cversion, 2);
    put_le(out, get_le(orig, 4, 4), 4);    // readguys doesn't look at the size: as it was
    return ZQW_OK;
}

int encode_guys_old(const ZqPartContext &ctx, std::vector<byte> &out)
{
    std::vector<byte> orig;

    if(!read_part(*ctx.part, orig) || orig.size() < 8)
        return ERR_PARSE;

    int v = ctx.version;

    if(v == 1 || v == 3)
        return encode_guys_bare(ctx, orig, out);

    if(v < 4)
        return ZQW_UNSUPPORTED;     // version 2 holds Link's sprites here; no quest has it

    // Before version 23 the file has the first 256 enemies; the rest are left empty.
    int n = v < 23 ? OLDBETAMAXGUYS : MAXGUYS;
    std::vector<byte> tmp;
    Rec walk(NULL, 0, 0, true, &tmp);
    guy_old(walk, v, guysbuf[0], guysbuf[0]);
    size_t rec_size = tmp.size();
    size_t names_at = 8, recs_at = 8 + 64 * (size_t)n;

    if(orig.size() != recs_at + rec_size * n)
    {
        note("guys v%d: the part (%u bytes) isn't laid out as readguys reads it\n", v, (unsigned)orig.size());
        return ERR_PARSE;
    }

    std::vector<int> mode(n);

    for(int i = 0; i < n; ++i)
        mode[i] = forced() ? M_DIRECT : (guy_changed(i) ? M_MINIMAL : M_COPY);

    std::vector<guydata> d;
    std::vector<std::string> names;
    Tries tries(mode);

    for(;;)
    {
        out.assign(orig.begin(), orig.begin() + 8);

        for(int i = 0; i < n; ++i)
        {
            Rec r(&orig[0], orig.size(), names_at + 64 * (size_t)i, mode[i] == M_DIRECT, &out);

            if(mode[i] == M_COPY)
                r.keep(64);
            else
                r.name64(guy_string[i], g_names[i]);
        }

        for(int i = 0; i < n; ++i)
        {
            size_t at = out.size();
            Rec r(&orig[0], orig.size(), recs_at + rec_size * (size_t)i, mode[i] == M_DIRECT, &out);

            if(mode[i] == M_COPY)
                r.keep(rec_size);
            else
                guy_old(r, v, guysbuf[i], g_guys[i]);

            note_record_change("guys", v, i, &orig[recs_at + rec_size * (size_t)i], rec_size, &out[at], out.size() - at);
        }

        if(!read_back_old(out, ctx, d, names))
        {
            note("guys v%d: readguys refused the section written\n", v);
            return ERR_READER;
        }

        bool retry = false;
        int bad = 0;

        for(int i = 0; i < MAXGUYS; ++i)
        {
            const char *why = diff_guy(d[i], guysbuf[i]);

            if(!why && names[i] != guy_string[i])
                why = "name";

            if(!why)
                continue;

            if(i < n && tries.next(i, true, true))
            {
                retry = true;
                note("guys v%d: enemy %d (%s) comes back different (%s), trying it %s\n", v, i, guy_string[i], why,
                     mode[i] == M_DIRECT ? "encoded whole" : "with its unchanged fields kept");
                continue;
            }

            ++bad;
            zqw_ig_refuse("Enemies, section version %d: enemy %d (%s) can't be kept as it is in this version (%s).\n", v, i, guy_string[i], why);
            note("guys v%d: enemy %d (%s) can't be held: %s\n", v, i, guy_string[i], why);
        }

        if(bad)
            return ERR_CANT_HOLD;

        if(!retry)
            break;
    }

    return ZQW_OK;
}

// ---- q255_guys.cpp (2.55 and 3.0 Nightly) ---------------------------------------------

const int FILE_GUYS = 512;
const int FILE_DEFENSES = 41;       // from v25; v24 has 2.53's 19
const int FILE_QUAKE = 19;          // where 2.53's Quake defense is from v25

// q255_guys.cpp's map_defense: upstream's added defense outcomes as the nearest 2.53 one.
byte map_defense(byte d)
{
    if(d < edLAST)
        return d;

    switch(d)
    {
    case 15:                // edCHINKL10
        return edCHINKL8;

    case 19:                // edHEAL
    case 51:                // edSWITCH
        return edIGNORE;

    case 21:                // edFREEZE
        return edSTUNONLY;

    case 26:                // edSPLIT
    case 27:                // edREPLACE
    case 33:                // edEXPLODESMALL
    case 34:                // edEXPLODELARGE
    case 41:                // edEXPLODEHARMLESS
        return ed1HKO;

    default:
        return edNORMAL;
    }
}

// 2.53's defense slot for the file's defense j, -1 for one 2.53 has no slot for.
int defense_slot(int j, int v)
{
    if(j < edefQUAKE)
        return j;

    if(v <= 24)
        return j < edefLAST ? j : -1;

    return j == FILE_QUAKE ? edefQUAKE : -1;
}

// The defense as the file stores it. The reader maps upstream's outcomes to 2.53's and
// keeps the stored ones aside; while ZQuest's outcome is still what the stored one maps
// to, the stored one stands, otherwise ZQuest's outcome was changed.
long stored_defense(int i, int j, int v, const guydata &d)
{
    std::map<int, std::vector<byte> >::const_iterator it = zq255.guy_defenses.find(i);
    int k = defense_slot(j, v);
    byte st = (it != zq255.guy_defenses.end() && (int)it->second.size() > j) ? it->second[j] : (k >= 0 ? d.defense[k] : 0);

    if(k < 0 || map_defense(st) == d.defense[k])
        return st;

    return d.defense[k];
}

// What a bubble-type enemy jinxes (v49+): bit flags in the file (1 sword, 2 item, 4 shield),
// 2.53's e8t values in ZQuest.
bool jinx_walker(int v, const guydata &d)
{
    return v >= 49 && d.family == eeWALK && (d.misc7 == e7tTEMPJINX || d.misc7 == e7tPERMJINX || d.misc7 == e7tUNJINX);
}

long jinx_target(long bits)
{
    switch(bits & 3)
    {
    case 1:
        return e8tSWORD;

    case 2:
        return e8tITEM;

    case 3:
        return e8tALL;

    default:
        return e8tLAST;
    }
}

long stored_misc8(int v, const guydata &d, long raw)
{
    if(!jinx_walker(v, d) || jinx_target(raw) == d.misc8)
        return jinx_walker(v, d) ? raw : d.misc8;

    switch(d.misc8)
    {
    case e8tSWORD:
        return (raw & ~3L) | 1;

    case e8tITEM:
        return (raw & ~3L) | 2;

    case e8tALL:
        return (raw & ~3L) | 3;

    case e8tLAST:
        return raw & ~3L;
    }

    return d.misc8;
}

// One enemy as q255_guys.cpp's read_guy reads it.
void guy_255(Rec &r, int v, int i, const guydata &c, const guydata &p)
{
    const int tw = v >= 36 ? 4 : 2;
    const int sw = v >= 55 ? 2 : 1;

    r.field(4, (long)c.flags, (long)p.flags);
    r.field(4, (long)c.flags2, (long)p.flags2);
    r.field(tw, c.tile, p.tile);
    r.field(1, c.width, p.width);
    r.field(1, c.height, p.height);
    r.field(tw, c.s_tile, p.s_tile);
    r.field(1, c.s_width, p.s_width);
    r.field(1, c.s_height, p.s_height);
    r.field(tw, c.e_tile, p.e_tile);
    r.field(1, c.e_width, p.e_width);
    r.field(1, c.e_height, p.e_height);

    const short cs[16] = { c.hp, c.family, c.cset, c.anim, c.e_anim, c.frate, c.e_frate, c.dp, c.wdp, c.weapon,
                           c.rate, c.hrate, c.step, c.homing, c.grumble, c.item_set };
    const short ps[16] = { p.hp, p.family, p.cset, p.anim, p.e_anim, p.frate, p.e_frate, p.dp, p.wdp, p.weapon,
                           p.rate, p.hrate, p.step, p.homing, p.grumble, p.item_set };

    for(int j = 0; j < 16; ++j)
        r.field(2, cs[j], ps[j]);

    const long cm[7] = { c.misc1, c.misc2, c.misc3, c.misc4, c.misc5, c.misc6, c.misc7 };
    const long pm[7] = { p.misc1, p.misc2, p.misc3, p.misc4, p.misc5, p.misc6, p.misc7 };

    for(int j = 0; j < 7; ++j)
        r.field(4, cm[j], pm[j]);

    long raw8 = (long)(int)r.peek(4);
    r.field(4, stored_misc8(v, c, raw8), stored_misc8(v, p, raw8));
    r.field(4, c.misc9, p.misc9);
    r.field(4, c.misc10, p.misc10);

    r.field(2, c.bgsfx, p.bgsfx);
    r.field(2, c.bosspal, p.bosspal);
    r.field(2, c.extend, p.extend);

    for(int j = 0; j < edefLAST; ++j)
        r.field(1, stored_defense(i, j, v, c), stored_defense(i, j, v, p));

    r.field(sw, c.hitsfx, p.hitsfx);
    r.field(sw, c.deadsfx, p.deadsfx);
    r.field(4, c.misc11, p.misc11);
    r.field(4, c.misc12, p.misc12);

    if(v > 24)
        for(int j = edefLAST; j < FILE_DEFENSES; ++j)
            r.field(1, stored_defense(i, j, v, c), stored_defense(i, j, v, p));

    if(v > 25)
        r.keep(5 * 4);          // tile and hit sizes

    if(v > 26)
        r.keep(5 * 4);          // hit and draw offsets

    if(v > 27)
    {
        long spr = get_or(zq255.guy_wpnsprite, i, 0);
        r.field(4, spr, spr);
    }

    if(v > 28)
        r.keep(4);              // SIZEflags

    if(v >= 30)
        r.keep(3 * 4 + 10 * 2); // frozen tile, cset, clock, misc

    const Q30ScriptRef script = get_or(zq255.guy_script, i, Q30ScriptRef());
    const Q30ScriptRef wscript = get_or(zq255.guy_weapon_script, i, Q30ScriptRef());

    if(v >= 34)
    {
        r.keep(2);              // firesfx
        r.keep(17 * 4);         // attributes 15-31
        r.keep(32 * 4);         // movement
        r.keep(32 * 4);         // new_weapon

        if(v < 59)
        {
            r.field(2, script.script, script.script);

            for(int j = 0; j < 8; ++j)
                r.field(4, script.initd[j], script.initd[j]);

            r.keep(2 * 4);
        }
        else
            script_config(r, script.script, script.script, script.initd, script.initd, true);
    }

    if(v >= 37)
        r.keep(4);              // editorflags

    if(v >= 38)
    {
        r.field(4, c.misc13, p.misc13);
        r.field(4, c.misc14, p.misc14);
        r.field(4, c.misc15, p.misc15);
    }

    if(v >= 39)
    {
        if(v < 59)
            r.keep(8 * 65);     // InitD labels

        if(v < 54)
            r.keep(8 * 65);     // and the weapon script's
    }

    if(v >= 40 && v < 54)
        r.field(2, wscript.script, wscript.script);

    if(v >= 41 && v < 54)
        for(int j = 0; j < 8; ++j)
            r.field(4, wscript.initd[j], wscript.initd[j]);

    long mf = (long)get_or(zq255.guy_moveflags, i, (dword)0);

    if(v >= 47)
        r.field(4, mf, mf);
    else if(v >= 42)
        r.field(1, mf, mf);

    if(v > 55)
        r.keep(3 * 2);          // shadow, death and spawn sprites
    else if(v > 44)
        r.keep(3);

    if(v >= 52 && v < 54)
    {
        r.keep(1);
        r.keep(12 * 4);
        r.keep(5 * (2 + 2));
    }

    if(v >= 55)
        r.keep(2);              // specialsfx
    else if(v >= 53)
        r.keep(1);

    if(v >= 54)
        weapon_block(r, wscript, true);

    if(v >= 58)
        r.keep(2 * 4);          // viewport ranges

    if(v >= 60)
        r.keep(2);              // firesfx_secondary
}

bool read_back_255(const std::vector<byte> &bytes, const ZqPartContext &ctx, std::vector<guydata> &d, std::vector<std::string> &names, SavedGuys &store)
{
    SavedGuys saved;
    saved.take();

    zquestheader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.zelda_version = ctx.zelda_version;
    hdr.build = ctx.build;

    Q255Section s;
    s.data = bytes.size() > 8 ? &bytes[8] : NULL;
    s.size = bytes.size() - 8;
    s.version = (word)get_le(bytes, 0, 2);
    s.cversion = (word)get_le(bytes, 2, 2);
    s.keep = true;
    s.header = &hdr;
    int ret = q255_read_guys(s);

    store.take();
    d = store.guys;
    names.resize(MAXGUYS);

    for(int i = 0; i < MAXGUYS; ++i)
        names[i] = guy_string[i];

    saved.put();
    return ret == 0 && !s.failed && s.pos == s.size && get_le(bytes, 4, 4) == s.size;
}

// Why enemy i doesn't come back as ZQuest holds it, NULL when it does.
const char *diff_255(int v, int i, const std::vector<guydata> &d, const std::vector<std::string> &names, const SavedGuys &got)
{
    const char *why = diff_guy(d[i], guysbuf[i]);

    if(why)
        return why;

    if(names[i] != guy_string[i])
        return "name";

    std::map<int, std::vector<byte> >::const_iterator it = got.defenses.find(i);

    if(it == got.defenses.end() || (int)it->second.size() != FILE_DEFENSES)
        return "defenses as stored";

    for(int j = 0; j < FILE_DEFENSES; ++j)
    {
        long want = (v > 24 || j < edefLAST) ? stored_defense(i, j, v, guysbuf[i]) : 0;

        if(it->second[j] != want)
            return "defenses as stored";
    }

    if(!same_ref(get_or(got.script, i, Q30ScriptRef()), get_or(zq255.guy_script, i, Q30ScriptRef())))
        return "script";

    if(!same_ref(get_or(got.weapon_script, i, Q30ScriptRef()), get_or(zq255.guy_weapon_script, i, Q30ScriptRef())))
        return "weapon script";

    if(get_or(got.moveflags, i, (dword)0) != get_or(zq255.guy_moveflags, i, (dword)0))
        return "move flags";

    if(get_or(got.wpnsprite, i, 0) != get_or(zq255.guy_wpnsprite, i, 0))
        return "weapon sprite";

    return NULL;
}

int encode_guys_255(const ZqPartContext &ctx, std::vector<byte> &out)
{
    std::vector<byte> orig;

    if(!read_part(*ctx.part, orig) || orig.size() < 8)
        return ERR_PARSE;

    int v = ctx.version;
    std::vector<byte> body(orig.begin() + 8, orig.end());
    size_t recs_at = 64 * (size_t)FILE_GUYS;
    std::vector<size_t> rec_at;

    if(get_le(orig, 4, 4) != body.size() || body.size() < recs_at)
        return ERR_PARSE;

    // Each record's place, walking the original as the reader does.
    size_t pos = recs_at;

    for(int i = 0; i < FILE_GUYS; ++i)
    {
        rec_at.push_back(pos);
        std::vector<byte> scratch;
        Rec r(&body[0], body.size(), pos, false, &scratch);
        guy_255(r, v, i, guysbuf[i], guysbuf[i]);

        if(r.short_read)
            break;

        pos = r.pos;
    }

    rec_at.push_back(pos);

    if((int)rec_at.size() != FILE_GUYS + 1 || pos != body.size())
    {
        note("guys v%d: the part isn't laid out as q255_read_guys reads it\n", v);
        return ERR_PARSE;
    }

    std::vector<int> mode(FILE_GUYS);

    for(int i = 0; i < FILE_GUYS; ++i)
        mode[i] = forced() ? M_DIRECT : (guy_changed(i) ? M_MINIMAL : M_COPY);

    std::vector<guydata> d;
    std::vector<std::string> names;
    SavedGuys got;
    Tries tries(mode);

    for(;;)
    {
        out.assign(orig.begin(), orig.begin() + 8);

        for(int i = 0; i < FILE_GUYS; ++i)
        {
            Rec r(&body[0], body.size(), 64 * (size_t)i, mode[i] == M_DIRECT, &out);

            if(mode[i] == M_COPY)
                r.keep(64);
            else
                r.name64(guy_string[i], g_names[i]);
        }

        for(int i = 0; i < FILE_GUYS; ++i)
        {
            size_t at = out.size();
            Rec r(&body[0], body.size(), rec_at[i], mode[i] == M_DIRECT, &out);

            if(mode[i] == M_COPY)
                r.keep(rec_at[i + 1] - rec_at[i]);
            else
                guy_255(r, v, i, guysbuf[i], g_guys[i]);

            note_record_change("guys", v, i, &body[rec_at[i]], rec_at[i + 1] - rec_at[i], &out[at], out.size() - at);
        }

        set_le(out, 4, (dword)(out.size() - 8), 4);

        if(!read_back_255(out, ctx, d, names, got))
        {
            note("guys v%d: q255_read_guys refused the section written\n", v);
            return ERR_READER;
        }

        bool retry = false;
        int bad = 0;

        for(int i = 0; i < MAXGUYS; ++i)
        {
            const char *why = diff_255(v, i, d, names, got);

            if(!why)
                continue;

            if(tries.next(i, true, false))
            {
                retry = true;
                note("guys v%d: enemy %d (%s) comes back different (%s), trying it with its unchanged fields kept\n", v, i, guy_string[i], why);
                continue;
            }

            ++bad;
            zqw_ig_refuse("Enemies, section version %d: enemy %d (%s) can't be kept as it is in this version (%s).\n", v, i, guy_string[i], why);
            note("guys v%d: enemy %d (%s) can't be held: %s\n", v, i, guy_string[i], why);
        }

        if(bad)
            return ERR_CANT_HOLD;

        if(!retry)
            break;
    }

    return ZQW_OK;
}

// ---- which writer takes the part ------------------------------------------------------

typedef int (*Encoder)(const ZqPartContext &ctx, std::vector<byte> &out);

// ZQW_IG_SELFTEST=1 (with ZQW_IG_LOG): once per run, edits a field in every fifth enemy the
// way a user would, encodes the section, notes whether it could be kept, and puts the
// enemies back. A kept edit has been read back as it is, so this exercises the edit path.
void self_test(const ZqPartContext &ctx, Encoder encode)
{
    static bool done = false;

    if(done || !getenv("ZQW_IG_SELFTEST"))
        return;

    done = true;
    const char *what[] = { "hp", "name", "misc1", "defense", "tile" };

    // only the enemies the file has: 256 before version 23, none in versions 1 and 3
    int held = ctx.zelda_version == 0x255 ? FILE_GUYS : (ctx.version <= 3 ? 0 : (ctx.version < 23 ? OLDBETAMAXGUYS : MAXGUYS));

    for(int k = 0; k < 5; ++k)
    {
        SavedGuys saved;
        saved.take();
        int n = 0;

        for(int i = 1; i < held; i += 5, ++n)
        {
            guydata &g = guysbuf[i];

            if(k == 0)
                g.hp = (short)(g.hp + 1);
            else if(k == 1)
                sprintf(guy_string[i], "Edited enemy %d", i);
            else if(k == 2)
                g.misc1 = g.misc1 > 1000 ? g.misc1 - 1 : g.misc1 + 1;
            else if(k == 3)
                g.defense[edefSWORD] = g.defense[edefSWORD] == edCHINK ? edNORMAL : edCHINK;
            else
                g.tile = g.tile ? g.tile - 1 : 1;
        }

        std::vector<byte> out;
        int rc = encode(ctx, out);
        saved.put();
        note("selftest guys v%d (quest 0x%x): %s on %d enemies: %s\n", ctx.version, ctx.zelda_version, what[k], n, rc == ZQW_OK ? "kept" : "REFUSED");
    }
}

int zqw_guys(PACKFILE *f, const ZqPartContext &ctx)
{
    Encoder encode = NULL;

    if(ctx.zelda_version == 0x255)
    {
        if(ctx.version < 24 || ctx.version > 60)
            return ZQW_UNSUPPORTED;

        encode = encode_guys_255;
    }
    else
    {
        if(ctx.zelda_version < 0x193 || ctx.zelda_version > 0x250 || ctx.version == 2)
            return ZQW_UNSUPPORTED;     // version 2 holds Link's sprites here; no quest has it

        // 2.53.0 and 2.53.1 at 2.53's own version: ZQuest's own 2.53 writer, as ever
        bool is253 = ctx.zelda_version == 0x250 && ctx.build >= 31 && ctx.build <= 33;

        if(is253 && ctx.version == V_GUYS && ctx.cversion == CV_GUYS)
            return ZQW_UNSUPPORTED;

        encode = encode_guys_old;
    }

    self_test(ctx, encode);
    std::vector<byte> out;
    int rc = encode(ctx, out);
    return rc != ZQW_OK ? rc : write_part(f, ctx.part->id, out);
}

ZqWriterRegistration g_register(ID_GUYS, zqw_guys, "enemies, in the quest's own version");
// Old quests derive some of this from the rules at load: written on every save (zq_writers.h).
ZqEverySave g_every_save_guys(ID_GUYS);

} // namespace

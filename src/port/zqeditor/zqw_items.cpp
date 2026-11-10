// The ITEM section (item data and item names), written back in the section version the
// quest was made in (see zq_writers.h). Two readers to mirror:
//   - src/qst.cpp's readitems: sectioned quests up to 2.53 (1.93 to 2.50.x headers), and
//     2.53.x quests at a section version older than 2.53's own (2.53's own version is left
//     to ZQuest's 2.53 writer);
//   - src/port/game/q255_items.cpp: 2.55 and 3.0 Nightly quests (versions 25 to 71).
//
// Every record is rebuilt from its original bytes, walking them the way the reader does:
// bytes ZQuest has no field for always stay as they were, and so does every field that
// wasn't changed since the load, so conversions the reader makes on the way in (values
// bounded, families mapped, old rules folded into the data) can't be undone by mistake.
// Only the fields that changed take ZQuest's value. With zqw_force_serialize every field
// takes ZQuest's value, which is how -qst-roundtrip-check tests the encoding.
//
// The section is then read back with the reader itself, into ZQuest's own buffers, which
// are put back right after. Whatever doesn't come back as ZQuest holds it (a field the
// version doesn't store, a value the reader would convert, an item past the ones a 2.55
// file lists) stops the save with an error rather than being lost. A record that doesn't
// come back is first tried the other way (see Tries in zqw_itemguy.h).
//
// Older formats list fewer items than 2.53 has; an item past those can be added, since
// the section holds its own count, as long as it reads back the same.
//
// One load-time change comes from another part: init data older than version 15 (2.10 and
// older) puts its own values into the fairies, potions, swords and hookshots after the
// items are read, so those fields come from the init data on every load and can't be
// changed here.

#include "precompiled.h" //always first

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "q255.h"
#include "zq_q255_store.h"
#include "zq_writers.h"
#include "zqw_itemguy.h"

extern itemdata *itemsbuf;
extern char *item_string[ITEMCNT];

namespace
{

using namespace zqw_ig;

const char *diff_item(const itemdata &a, const itemdata &b)
{
#define CMP(x) if(a.x != b.x) return #x;
    CMP(tile) CMP(misc) CMP(csets) CMP(frames) CMP(speed) CMP(delay) CMP(ltm)
    CMP(family) CMP(fam_type) CMP(power) CMP(flags) CMP(script) CMP(count) CMP(amount)
    CMP(setmax) CMP(max) CMP(playsound) CMP(collect_script)
    CMP(initiald[0]) CMP(initiald[1]) CMP(initiald[2]) CMP(initiald[3])
    CMP(initiald[4]) CMP(initiald[5]) CMP(initiald[6]) CMP(initiald[7])
    CMP(initiala[0]) CMP(initiala[1])
    CMP(wpn) CMP(wpn2) CMP(wpn3) CMP(wpn4) CMP(wpn5) CMP(wpn6) CMP(wpn7) CMP(wpn8) CMP(wpn9) CMP(wpn10)
    CMP(pickup_hearts) CMP(misc1) CMP(misc2) CMP(misc3) CMP(misc4) CMP(misc5) CMP(misc6)
    CMP(misc7) CMP(misc8) CMP(misc9) CMP(misc10) CMP(magic) CMP(usesound)
#undef CMP
    return NULL;
}

// ---- what was loaded ------------------------------------------------------------------

itemdata g_items[MAXITEMS];         // itemsbuf as loaded (or as last saved)
char g_names[ITEMCNT][64];

void snapshot()
{
    memcpy(g_items, itemsbuf, sizeof(g_items));

    for(int i = 0; i < ITEMCNT; ++i)
        memcpy(g_names[i], item_string[i], 64);
}

ZqLoadHookRegistration g_hook(snapshot);

bool item_changed(int i)
{
    return diff_item(itemsbuf[i], g_items[i]) != NULL || strcmp(item_string[i], g_names[i]) != 0;
}

// ZQuest's buffers while a reader runs on what was written: put back afterwards.
struct SavedItems
{
    std::vector<itemdata> items;
    std::vector<std::string> names;     // all 64 bytes of each
    std::map<int, long> family;
    std::map<int, int> usesound2, override_flags;
    std::map<int, std::string> display_name;
    std::map<int, bool> passive;
    std::map<int, Q30ScriptRef> sprite, weapon;

    void take()
    {
        items.assign(itemsbuf, itemsbuf + MAXITEMS);
        names.resize(ITEMCNT);

        for(int i = 0; i < ITEMCNT; ++i)
            names[i].assign(item_string[i], 64);

        family = zq255.item_family;
        usesound2 = zq255.item_usesound2;
        override_flags = zq255.item_override_flags;
        display_name = zq255.item_display_name;
        passive = zq255.item_passive_script;
        sprite = zq255.item_sprite_script;
        weapon = zq255.item_weapon_script;
    }

    void put() const
    {
        memcpy(itemsbuf, &items[0], sizeof(itemdata) * MAXITEMS);

        for(int i = 0; i < ITEMCNT; ++i)
            memcpy(item_string[i], names[i].data(), 64);

        zq255.item_family = family;
        zq255.item_usesound2 = usesound2;
        zq255.item_override_flags = override_flags;
        zq255.item_display_name = display_name;
        zq255.item_passive_script = passive;
        zq255.item_sprite_script = sprite;
        zq255.item_weapon_script = weapon;
    }
};

// ---- qst.cpp's readitems (sectioned quests up to 2.53) --------------------------------

// The fields of one item as readitems reads them for section version s (s > 0x192 quests).
void item_old(Rec &r, int s, const itemdata &c, const itemdata &p)
{
    r.field(2, c.tile, p.tile);
    r.field(1, c.misc, p.misc);
    r.field(1, c.csets, p.csets);
    r.field(1, c.frames, p.frames);
    r.field(1, c.speed, p.speed);
    r.field(1, c.delay, p.delay);
    r.field(4, c.ltm, p.ltm);

    if(s <= 1)
        return;

    r.field(1, c.family, p.family);
    r.field(1, c.fam_type, p.fam_type);

    if(s > 5)
    {
        r.field(1, c.power, p.power);
        r.field(2, c.flags, p.flags);
    }
    else
        r.field(1, (c.flags & ITEM_GAMEDATA) ? 1 : 0, (p.flags & ITEM_GAMEDATA) ? 1 : 0);

    r.field(2, c.script, p.script);
    r.field(1, c.count, p.count);
    r.field(2, c.amount, p.amount);
    r.field(2, c.collect_script, p.collect_script);
    r.field(2, c.setmax, p.setmax);
    r.field(2, c.max, p.max);
    r.field(1, c.playsound, p.playsound);

    for(int j = 0; j < 8; ++j)
        r.field(4, c.initiald[j], p.initiald[j]);

    for(int j = 0; j < 2; ++j)
        r.field(1, c.initiala[j], p.initiala[j]);

    if(s <= 4)
        return;

    if(s > 5)
    {
        const byte cw[10] = { c.wpn, c.wpn2, c.wpn3, c.wpn4, c.wpn5, c.wpn6, c.wpn7, c.wpn8, c.wpn9, c.wpn10 };
        const byte pw[10] = { p.wpn, p.wpn2, p.wpn3, p.wpn4, p.wpn5, p.wpn6, p.wpn7, p.wpn8, p.wpn9, p.wpn10 };

        for(int j = 0; j < (s >= 15 ? 10 : 4); ++j)
            r.field(1, cw[j], pw[j]);

        r.field(1, c.pickup_hearts, p.pickup_hearts);
        r.field(s < 15 ? 2 : 4, c.misc1, p.misc1);
        r.field(s < 15 ? 2 : 4, c.misc2, p.misc2);
        r.field(1, c.magic, p.magic);
    }
    else
        r.field(1, (c.flags & ITEM_EDIBLE) ? 1 : 0, (p.flags & ITEM_EDIBLE) ? 1 : 0);

    if(s < 12)
        return;

    if(s < 15)
    {
        r.field(2, c.misc3, p.misc3);
        r.field(2, c.misc4, p.misc4);
    }
    else
    {
        const long cm[8] = { c.misc3, c.misc4, c.misc5, c.misc6, c.misc7, c.misc8, c.misc9, c.misc10 };
        const long pm[8] = { p.misc3, p.misc4, p.misc5, p.misc6, p.misc7, p.misc8, p.misc9, p.misc10 };

        for(int j = 0; j < 8; ++j)
            r.field(4, cm[j], pm[j]);
    }

    r.field(1, c.usesound, p.usesound);
}

// Old init data (version 14 and older) puts some of its own values into these items when
// it is read, after the items: those fields come from the init data on every load.
void init_overrides(itemdata *d, const itemdata *loaded)
{
    const int fairies_potions[] = { iFairyStill, iFairyMoving, iRPotion, iBPotion };
    const int swords[] = { iSword, iWSword, iMSword, iXSword };

    for(int k = 0; k < 4; ++k)
    {
        int i = fairies_potions[k];
        d[i].misc1 = loaded[i].misc1;
        d[i].misc2 = loaded[i].misc2;

        if(i == iFairyStill || i == iFairyMoving)
            d[i].misc3 = loaded[i].misc3;

        d[i].flags |= loaded[i].flags & (ITEM_FLAG1 | ITEM_FLAG2);
    }

    for(int k = 0; k < 4; ++k)
    {
        int i = swords[k];
        d[i].pickup_hearts = loaded[i].pickup_hearts;
        d[i].misc1 = loaded[i].misc1;
        d[i].misc2 = loaded[i].misc2;

        if(!(loaded[i].flags & ITEM_FLAG1))
            d[i].flags &= ~ITEM_FLAG1;
    }

    d[iHookshot].misc1 = loaded[iHookshot].misc1;
    d[iHookshot].misc2 = loaded[iHookshot].misc2;
    d[iLongshot].misc1 = loaded[iLongshot].misc1;
    d[iLongshot].misc2 = loaded[iLongshot].misc2;
}

// Whether old init data follows this part in the file.
bool old_init_follows(const ZqQuestPart *items)
{
    bool after = false;

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
    {
        const ZqQuestPart &p = zq_quest_file.parts[i];

        if(&p == items)
            after = true;
        else if(after && p.id == ID_INITDATA)
        {
            word v, cv;
            return zq_part_versions(p, &v, &cv) && v < 15;
        }
    }

    return false;
}

struct OldLayout
{
    word version, cversion, count;
    dword size;
    size_t rec_size, names_at, recs_at;
};

size_t old_record_size(int s)
{
    std::vector<byte> tmp;
    Rec r(NULL, 0, 0, true, &tmp);
    item_old(r, s, itemsbuf[0], itemsbuf[0]);
    return tmp.size();
}

void build_old(const std::vector<byte> &orig, const OldLayout &L, int count, const std::vector<int> &mode, std::vector<byte> &out)
{
    out.clear();
    put_le(out, L.version, 2);
    put_le(out, L.cversion, 2);
    put_le(out, 0, 4);
    put_le(out, (dword)count, 2);

    if(L.version > 1)
    {
        for(int i = 0; i < count; ++i)
        {
            bool old = i < L.count;
            Rec r(old ? &orig[0] : NULL, orig.size(), old ? L.names_at + 64 * (size_t)i : 0, mode[i] == M_DIRECT, &out);

            if(mode[i] == M_COPY)
                r.keep(64);
            else
                r.name64(item_string[i], g_names[i]);
        }
    }

    for(int i = 0; i < count; ++i)
    {
        bool old = i < L.count;
        size_t at = out.size();
        Rec r(old ? &orig[0] : NULL, orig.size(), old ? L.recs_at + L.rec_size * (size_t)i : 0, mode[i] == M_DIRECT, &out);

        if(mode[i] == M_COPY)
            r.keep(L.rec_size);
        else
            item_old(r, L.version, itemsbuf[i], g_items[i]);

        if(old)
            note_record_change("items", L.version, i, &orig[L.recs_at + L.rec_size * (size_t)i], L.rec_size, &out[at], out.size() - at);
    }

    // readitems doesn't check the size; it moves with the length, as a section size would
    set_le(out, 4, L.size + (dword)(out.size() - orig.size()), 4);
}

// Runs readitems on the section; d and names get what it read. False if it refused it.
bool read_back_old(const std::vector<byte> &bytes, const ZqPartContext &ctx, std::vector<itemdata> &d, std::vector<std::string> &names)
{
    SavedItems saved;
    saved.take();

    MemFile m;
    PACKFILE *f = open_mem(m, bytes);
    int ret = f ? readitems(f, ctx.zelda_version, ctx.build, true) : qe_nomem;
    bool whole = mem_used_up(m);

    if(f)
        pack_fclose(f);

    d.assign(itemsbuf, itemsbuf + MAXITEMS);
    names.resize(ITEMCNT);

    for(int i = 0; i < ITEMCNT; ++i)
        names[i] = item_string[i];

    saved.put();
    return ret == 0 && whole;
}

int encode_items_old(const ZqPartContext &ctx, std::vector<byte> &out)
{
    std::vector<byte> orig;

    if(!read_part(*ctx.part, orig) || orig.size() < 10)
        return ERR_PARSE;

    OldLayout L;
    L.version = (word)get_le(orig, 0, 2);
    L.cversion = (word)get_le(orig, 2, 2);
    L.size = get_le(orig, 4, 4);
    L.count = (word)get_le(orig, 8, 2);
    L.rec_size = old_record_size(L.version);
    L.names_at = 10;
    L.recs_at = 10 + (L.version > 1 ? 64 * (size_t)L.count : 0);

    if(L.count > MAXITEMS || orig.size() != L.recs_at + L.rec_size * L.count)
    {
        note("items v%d: the part (%u bytes, %d items) isn't laid out as readitems reads it\n", L.version, (unsigned)orig.size(), L.count);
        return ERR_PARSE;
    }

    // An item past the ones the file lists can be added: the section holds its own count.
    int count = L.count;

    for(int i = L.count; i < MAXITEMS; ++i)
        if(item_changed(i))
            count = i + 1;

    std::vector<int> mode(count);

    for(int i = 0; i < count; ++i)
        mode[i] = (forced() || i >= L.count) ? M_DIRECT : (item_changed(i) ? M_MINIMAL : M_COPY);

    bool old_init = old_init_follows(ctx.part);
    std::vector<itemdata> d;
    std::vector<std::string> names;
    Tries tries(mode);

    for(;;)
    {
        build_old(orig, L, count, mode, out);

        if(!read_back_old(out, ctx, d, names))
        {
            note("items v%d: readitems refused the section written\n", L.version);
            return ERR_READER;
        }

        if(old_init)
            init_overrides(&d[0], g_items);

        bool retry = false;
        int bad = 0;

        for(int i = 0; i < MAXITEMS; ++i)
        {
            const char *why = diff_item(d[i], itemsbuf[i]);

            if(!why && names[i] != item_string[i])
                why = "name";

            if(!why)
                continue;

            if(i < count && tries.next(i, i < L.count, true))
            {
                retry = true;
                note("items v%d: item %d (%s) comes back different (%s), trying it %s\n", L.version, i, item_string[i], why,
                     mode[i] == M_DIRECT ? "encoded whole" : "with its unchanged fields kept");
                continue;
            }

            ++bad;
            zqw_ig_refuse("Items, section version %d: item %d (%s) can't be kept as it is in this version (%s).\n", L.version, i, item_string[i], why);
            note("items v%d: item %d (%s) can't be held: %s\n", L.version, i, item_string[i], why);
        }

        if(bad)
            return ERR_CANT_HOLD;

        if(!retry)
            break;
    }

    return ZQW_OK;
}

// ---- q255_items.cpp (2.55 and 3.0 Nightly) --------------------------------------------

// The family as the file stores it. The reader maps anything 2.53 has no family for to
// itype_misc and keeps the stored one aside; while ZQuest's family is still what the stored
// one maps to, the stored one stands, otherwise ZQuest's family was changed.
long family_255(int i, const itemdata &d)
{
    std::map<int, long>::const_iterator it = zq255.item_family.find(i);

    if(it == zq255.item_family.end())
        return d.family;

    long st = it->second;
    long mapped = (st < 0 || st >= itype_max) ? itype_misc : st;
    return mapped == (long)d.family ? st : (long)d.family;
}

// The cost counter (v34+). 2.53 charges rupees with ITEM_RUPEE_MAGIC and magic without it;
// the reader keeps magic only for those two counters and makes any other one free.
long cost_counter(const itemdata &d, signed char raw)
{
    if(d.flags & ITEM_RUPEE_MAGIC)
        return 1;

    if(raw == 1)
        return 4;

    if(d.magic != 0 && raw != 4)
        return 4;

    return raw;
}

// One record as q255_items.cpp's read_item reads it, with the name in front from v67.
void item_255(Rec &r, int v, int i, const itemdata &c, const itemdata &p, const char *name_c, const char *name_p)
{
    if(v >= 67)
        r.cstr(name_c, name_p);

    r.field(v > 35 ? 4 : 2, c.tile, p.tile);
    r.field(1, c.misc, p.misc);
    r.field(1, c.csets, p.csets);
    r.field(1, c.frames, p.frames);
    r.field(1, c.speed, p.speed);
    r.field(1, c.delay, p.delay);
    r.field(4, c.ltm, p.ltm);
    r.field(v >= 31 ? 4 : 1, family_255(i, c), family_255(i, p));
    r.field(1, c.fam_type, p.fam_type);
    r.field(v >= 31 ? 4 : 1, c.power, p.power);

    if(v >= 41)
    {
        // the upper flags have no 2.53 flag, apart from bit 23, the passive script
        dword kept = r.peek(4) & 0xFF7F0000;

        if(get_or(zq255.item_passive_script, i, false))
            kept |= 0x00800000;

        r.field(4, (long)(kept | c.flags), (long)(kept | p.flags));
    }
    else
        r.field(2, c.flags, p.flags);

    if(v < 71)
        r.field(2, c.script, p.script);

    r.field(1, c.count, p.count);
    r.field(2, c.amount, p.amount);

    if(v < 71)
        r.field(2, c.collect_script, p.collect_script);

    r.field(2, c.setmax, p.setmax);
    r.field(2, c.max, p.max);
    r.field(v >= 66 ? 2 : 1, c.playsound, p.playsound);

    if(v < 71)
        for(int j = 0; j < 8; ++j)
            r.field(4, c.initiald[j], p.initiald[j]);

    if(v < 67)
        for(int j = 0; j < 2; ++j)
            r.field(1, c.initiala[j], p.initiala[j]);

    const byte cw[10] = { c.wpn, c.wpn2, c.wpn3, c.wpn4, c.wpn5, c.wpn6, c.wpn7, c.wpn8, c.wpn9, c.wpn10 };
    const byte pw[10] = { p.wpn, p.wpn2, p.wpn3, p.wpn4, p.wpn5, p.wpn6, p.wpn7, p.wpn8, p.wpn9, p.wpn10 };

    for(int j = 0; j < 10; ++j)
        r.field(v >= 68 ? 2 : 1, cw[j], pw[j]);

    r.field(1, c.pickup_hearts, p.pickup_hearts);
    r.field(4, c.misc1, p.misc1);
    r.field(4, c.misc2, p.misc2);

    if(v >= 53)
    {
        r.field(2, c.magic, p.magic);
        r.keep(2);              // cost_amount[1]
    }
    else
        r.field(1, c.magic, p.magic);

    const long cm[8] = { c.misc3, c.misc4, c.misc5, c.misc6, c.misc7, c.misc8, c.misc9, c.misc10 };
    const long pm[8] = { p.misc3, p.misc4, p.misc5, p.misc6, p.misc7, p.misc8, p.misc9, p.misc10 };

    for(int j = 0; j < 8; ++j)
        r.field(4, cm[j], pm[j]);

    long sound2 = get_or(zq255.item_usesound2, i, 0);

    if(v >= 66)
    {
        r.field(2, c.usesound, p.usesound);
        r.field(2, sound2, sound2);
    }
    else
    {
        r.field(1, c.usesound, p.usesound);

        if(v >= 49)
            r.field(1, sound2, sound2);
    }

    const Q30ScriptRef weapon = get_or(zq255.item_weapon_script, i, Q30ScriptRef());
    const Q30ScriptRef sprite = get_or(zq255.item_sprite_script, i, Q30ScriptRef());

    if(v >= 26)
    {
        if(v < 63)
            r.keep(2);          // imitate_weapon, default_defense

        r.keep(4 + 4);          // weaprange, weapduration
        r.keep(10 * 4);         // weap_pattern
    }

    if(v >= 27)
    {
        r.keep(4);              // duplicates

        if(v < 63)
            for(int j = 0; j < 8; ++j)
                r.field(4, weapon.initd[j], weapon.initd[j]);

        if(v < 67)
            r.keep(2);          // weapon InitA

        r.keep(1);              // drawlayer
        r.keep(7 * 4);          // item hit box and offsets

        if(v < 63)
        {
            r.keep(7 * 4);      // weapon hit box and offsets
            r.field(2, weapon.script, weapon.script);
        }

        r.keep(4);              // wpnsprite
        r.keep(v >= 53 ? 2 * 4 : 4);    // magiccosttimer
    }

    if(v >= 28)
    {
        long ov = get_or(zq255.item_override_flags, i, 0);
        r.field(4, ov, ov);
        r.keep(2 * 4);          // tile width, tile height
    }

    if(v >= 29 && v < 63)
        r.keep(3 * 4);

    if(v >= 30)
        r.keep(4);              // pickup

    if(v >= 32)
        r.keep(2);              // pstring

    if(v >= 33)
        r.keep(2);              // pickup_string_flags

    if(v >= 34)
    {
        signed char raw = (signed char)r.peek(1);
        r.field(1, cost_counter(c, raw), cost_counter(p, raw));

        if(v >= 53)
            r.keep(1);          // cost_counter[1]
    }

    if(v >= 71)
    {
        // The reader gives 2.53's one InitD to the action script, or to the collect script
        // when only that one is set: that one's arguments are ZQuest's, the other's stay.
        bool collect_c = c.script == 0 && c.collect_script != 0;
        bool collect_p = p.script == 0 && p.collect_script != 0;
        script_config(r, c.script, p.script, collect_c ? NULL : c.initiald, collect_p ? NULL : p.initiald, false);
        script_config(r, c.collect_script, p.collect_script, collect_c ? c.initiald : NULL, collect_p ? p.initiald : NULL, false);
        script_config(r, sprite.script, sprite.script, sprite.initd, sprite.initd, false);
    }
    else if(v >= 44)
    {
        r.keep(8 * (65 + (v < 63 ? 65 : 0) + 65 + 4));     // script labels, one long each

        if(v < 67)
            r.keep(2);          // sprite InitA

        r.field(2, sprite.script, sprite.script);
    }

    if(v >= 48)
        r.keep(1);              // pickupflag

    if(v >= 57)
    {
        std::string dn = get_or(zq255.item_display_name, i, std::string());
        r.cstr(dn, dn);
    }

    if(v >= 58 && v < 63)
        r.keep(5 * (v >= 59 ? 2 : 1));

    if(v >= 60)
    {
        r.keep(v >= 65 ? 2 : 1);    // pickup_litems
        r.keep(2);                  // pickup_litem_level
    }

    if(v >= 62)
    {
        r.keep(4);              // moveflags

        if(v < 63)
            r.keep(4);
    }

    if(v >= 63)
        weapon_block(r, weapon, false);

    if(v >= 64)
        r.keep(4);              // cooldown

    if(v >= 70)
        r.keep(4 + 4);          // viewport ranges
}

struct Layout255
{
    word version, cversion, count;
    size_t names_at;                // in the body
    std::vector<size_t> rec_at;     // each record's start in the body, and the body's end
};

// Walks the original body the way the reader does. False if it doesn't come out at the end.
bool walk_255(const std::vector<byte> &body, Layout255 &L)
{
    if(body.size() < 2)
        return false;

    L.count = (word)get_le(body, 0, 2);
    L.names_at = 2;
    size_t pos = 2 + (L.version < 67 ? 64 * (size_t)L.count : 0);
    L.rec_at.clear();

    for(int i = 0; i < L.count; ++i)
    {
        L.rec_at.push_back(pos);
        std::vector<byte> scratch;
        int k = i < MAXITEMS ? i : 0;
        Rec r(&body[0], body.size(), pos, false, &scratch);
        item_255(r, L.version, k, itemsbuf[k], itemsbuf[k], item_string[k], item_string[k]);

        if(r.short_read)
            return false;

        pos = r.pos;
    }

    L.rec_at.push_back(pos);
    return pos == body.size();
}

void build_255(const std::vector<byte> &body, const Layout255 &L, const std::vector<int> &mode, std::vector<byte> &out)
{
    out.clear();
    put_le(out, L.version, 2);
    put_le(out, L.cversion, 2);
    put_le(out, 0, 4);
    size_t body_at = out.size();
    put_le(out, L.count, 2);

    if(L.version < 67)
    {
        for(int i = 0; i < L.count; ++i)
        {
            Rec r(&body[0], body.size(), L.names_at + 64 * (size_t)i, mode[i] == M_DIRECT, &out);

            if(mode[i] == M_COPY || i >= MAXITEMS)
                r.keep(64);
            else
                r.name64(item_string[i], g_names[i]);
        }
    }

    for(int i = 0; i < L.count; ++i)
    {
        size_t at = out.size();
        Rec r(&body[0], body.size(), L.rec_at[i], mode[i] == M_DIRECT, &out);

        if(mode[i] == M_COPY || i >= MAXITEMS)
            r.keep(L.rec_at[i + 1] - L.rec_at[i]);
        else
            item_255(r, L.version, i, itemsbuf[i], g_items[i], item_string[i], g_names[i]);

        note_record_change("items", L.version, i, &body[L.rec_at[i]], L.rec_at[i + 1] - L.rec_at[i], &out[at], out.size() - at);
    }

    set_le(out, 4, (dword)(out.size() - body_at), 4);
}

// What q255_read_items left in ZQuest's buffers and store.
struct Read255
{
    std::vector<itemdata> d;
    std::vector<std::string> names;
    std::map<int, long> family;
    std::map<int, int> usesound2, override_flags;
    std::map<int, std::string> display_name;
    std::map<int, bool> passive;
    std::map<int, Q30ScriptRef> sprite, weapon;
};

bool read_back_255(const std::vector<byte> &bytes, const ZqPartContext &ctx, Read255 &got)
{
    SavedItems saved;
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
    int ret = q255_read_items(s);

    got.d.assign(itemsbuf, itemsbuf + MAXITEMS);
    got.names.resize(ITEMCNT);

    for(int i = 0; i < ITEMCNT; ++i)
        got.names[i] = item_string[i];

    got.family = zq255.item_family;
    got.usesound2 = zq255.item_usesound2;
    got.override_flags = zq255.item_override_flags;
    got.display_name = zq255.item_display_name;
    got.passive = zq255.item_passive_script;
    got.sprite = zq255.item_sprite_script;
    got.weapon = zq255.item_weapon_script;

    saved.put();
    return ret == 0 && !s.failed && s.pos == s.size && get_le(bytes, 4, 4) == s.size;
}

// Why item i doesn't come back as ZQuest holds it, NULL when it does.
const char *diff_255(const Read255 &got, int i, int count)
{
    const char *why = diff_item(got.d[i], itemsbuf[i]);

    if(why)
        return why;

    if(got.names[i] != item_string[i])
        return "name";

    if(i >= count)
        return NULL;

    if(get_or(got.family, i, -1L) != family_255(i, itemsbuf[i]))
        return "family as stored";

    if(get_or(got.usesound2, i, 0) != get_or(zq255.item_usesound2, i, 0))
        return "second use sound";

    if(get_or(got.override_flags, i, 0) != get_or(zq255.item_override_flags, i, 0))
        return "override flags";

    if(get_or(got.display_name, i, std::string()) != get_or(zq255.item_display_name, i, std::string()))
        return "display name";

    if(get_or(got.passive, i, false) != get_or(zq255.item_passive_script, i, false))
        return "passive script flag";

    if(!same_ref(get_or(got.sprite, i, Q30ScriptRef()), get_or(zq255.item_sprite_script, i, Q30ScriptRef())))
        return "sprite script";

    if(!same_ref(get_or(got.weapon, i, Q30ScriptRef()), get_or(zq255.item_weapon_script, i, Q30ScriptRef())))
        return "weapon script";

    return NULL;
}

int encode_items_255(const ZqPartContext &ctx, std::vector<byte> &out)
{
    std::vector<byte> orig;

    if(!read_part(*ctx.part, orig) || orig.size() < 8)
        return ERR_PARSE;

    Layout255 L;
    L.version = (word)get_le(orig, 0, 2);
    L.cversion = (word)get_le(orig, 2, 2);
    std::vector<byte> body(orig.begin() + 8, orig.end());

    if(get_le(orig, 4, 4) != body.size() || !walk_255(body, L))
    {
        note("items v%d: the part isn't laid out as q255_read_items reads it\n", L.version);
        return ERR_PARSE;
    }

    // The section can't list more items than it does: 2.55 always writes all of them.
    for(int i = L.count; i < MAXITEMS; ++i)
        if(item_changed(i))
        {
            zqw_ig_refuse("Items, section version %d: item %d is past the %d this quest's file holds.\n", L.version, i, L.count);
            return ERR_CANT_HOLD;
        }

    std::vector<int> mode(L.count);

    for(int i = 0; i < L.count; ++i)
        mode[i] = forced() ? M_DIRECT : ((i < MAXITEMS && item_changed(i)) ? M_MINIMAL : M_COPY);

    Read255 got;
    Tries tries(mode);

    for(;;)
    {
        build_255(body, L, mode, out);

        if(!read_back_255(out, ctx, got))
        {
            note("items v%d: q255_read_items refused the section written\n", L.version);
            return ERR_READER;
        }

        bool retry = false;
        int bad = 0;

        for(int i = 0; i < MAXITEMS; ++i)
        {
            const char *why = diff_255(got, i, L.count);

            if(!why)
                continue;

            if(i < L.count && tries.next(i, true, false))
            {
                retry = true;
                note("items v%d: item %d (%s) comes back different (%s), trying it with its unchanged fields kept\n", L.version, i, item_string[i], why);
                continue;
            }

            ++bad;
            zqw_ig_refuse("Items, section version %d: item %d (%s) can't be kept as it is in this version (%s).\n", L.version, i, item_string[i], why);
            note("items v%d: item %d (%s) can't be held: %s\n", L.version, i, item_string[i], why);
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

// ZQW_IG_SELFTEST=1 (with ZQW_IG_LOG): once per run, edits a field in every fifth item the
// way a user would, encodes the section, notes whether it could be kept, and puts the
// items back. A kept edit has been read back as it is, so this exercises the edit path.
void self_test(const ZqPartContext &ctx, Encoder encode)
{
    static bool done = false;

    if(done || !getenv("ZQW_IG_SELFTEST"))
        return;

    done = true;
    const char *what[] = { "tile", "name", "misc1", "power" };

    // only the items the file lists (after the version, cversion and size, in both readers)
    byte head[10];
    int held = zq_part_read(*ctx.part, 0, head, sizeof(head)) ? head[8] | (head[9] << 8) : 0;

    if(held > MAXITEMS)
        held = MAXITEMS;

    for(int k = 0; k < 4; ++k)
    {
        SavedItems saved;
        saved.take();
        int n = 0;

        for(int i = 1; i < held; i += 5, ++n)
        {
            itemdata &it = itemsbuf[i];

            if(k == 0)
                it.tile = it.tile ? it.tile - 1 : 1;
            else if(k == 1)
                sprintf(item_string[i], "Edited item %d", i);
            else if(k == 2)
                it.misc1 += 3;
            else
                it.power = (byte)(it.power + 1);
        }

        std::vector<byte> out;
        int rc = encode(ctx, out);
        saved.put();
        note("selftest items v%d (quest 0x%x): %s on %d items: %s\n", ctx.version, ctx.zelda_version, what[k], n, rc == ZQW_OK ? "kept" : "REFUSED");
    }
}

int zqw_items(PACKFILE *f, const ZqPartContext &ctx)
{
    Encoder encode = NULL;

    if(ctx.zelda_version == 0x255)
    {
        if(ctx.version < 25 || ctx.version > 71)
            return ZQW_UNSUPPORTED;

        encode = encode_items_255;
    }
    else
    {
        if(ctx.zelda_version < 0x193 || ctx.zelda_version > 0x250)
            return ZQW_UNSUPPORTED;

        // 2.53.0 and 2.53.1 at 2.53's own version: ZQuest's own 2.53 writer, as ever
        bool is253 = ctx.zelda_version == 0x250 && ctx.build >= 31 && ctx.build <= 33;

        if(is253 && ctx.version == V_ITEMS && ctx.cversion == CV_ITEMS)
            return ZQW_UNSUPPORTED;

        encode = encode_items_old;
    }

    self_test(ctx, encode);
    std::vector<byte> out;
    int rc = encode(ctx, out);
    return rc != ZQW_OK ? rc : write_part(f, ctx.part->id, out);
}

ZqWriterRegistration g_register(ID_ITEMS, zqw_items, "items, in the quest's own version");
// Old quests derive some of this from the rules at load: written on every save (zq_writers.h).
ZqEverySave g_every_save_items(ID_ITEMS);

} // namespace

// Section writers for init data (INIT), Link sprites (LINK), subscreens (SUBS), cheats (CHT),
// item drop sets (DROP) and editor favorites (FAVS), for quests made before 2.53 (1.92's
// sectioned builds to 2.50.x) and for 2.55 and 3.0 Nightly quests. See zqw_init.h for how
// they keep what nobody changed, and zq_writers.h for what a writer writes.
//
// A 2.53.0/2.53.1 quest is left to ZQuest's own 2.53 writers, as the Allegro ZQuest saves it.
//
// What these versions can't hold, and the writers refuse to save rather than drop:
//   INIT v1 (2.10, early 2.11): items that aren't the first of their family and level, level 1
//     rings and bracelets, other families' items, values past a byte, magic that isn't whole
//     blocks, anything 2.11 added (arrows, level keys, the subscreen grid, ...); the fairy,
//     potion and hookshot values it sets in the item data on load.
//   INIT 2.55: subscreen type and style, transition type, the misc bits other than continue
//     percent, double magic and can slash; the "more" icon from v49, where it moved to each
//     string; generic script and screen data start values once ZQuest's store has them
//     changed (nothing in ZQuest edits them).
//   LINK v1: swim, jump and charge sprites and the swim speed.
//   SUBS from v8: objects added, removed or turned into another type, and any object that
//     isn't one of a widget's own (see the subscreen part below).
//   CHT: codes in a quest whose header says it has none.
//   FAVS 2.55 v3/v4: any change; their layout of favorites differs from 2.53's.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "subscr.h"
#include "zc_custom.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zq_q255_store.h"
#include "zqw_init.h"

extern zinitdata zinit;
extern zquestheader header;
extern ZCHEATS zcheats;
extern itemdata *itemsbuf;
extern item_drop_object item_drop_sets[MAXITEMDROPSETS];
int count_item_drop_sets();

// ---- shared helpers (zqw_init.h) ------------------------------------------------------

namespace zqwe
{

namespace
{
std::string g_why;
}

void set_why(const std::string &s)
{
    g_why = s;
    zqw_last_error = s;         // the reason the save shows the user (zq_writers.h)
    al_trace("section writer: %s\n", s.c_str());
}

const std::string &why()
{
    return g_why;
}

bool header_is_253(const ZqPartContext &ctx)
{
    return ctx.zelda_version == 0x250 && ctx.build >= 31 && ctx.build <= 33;
}

bool testing_unforced()
{
    static int on = -1;

    if(on < 0)
        on = getenv("ZQW_E_NOFORCE") ? 1 : 0;

    return on == 1;
}

bool mutate_now(bool &done)
{
    if(done || !getenv("ZQW_E_MUTATE"))
        return false;

    done = true;
    return true;
}

bool load_part(const ZqQuestPart &p, Src &s)
{
    s.d.assign((size_t)p.length, 0);
    s.pos = 0;
    s.bad = false;
    return p.length == 0 || zq_part_read(p, 0, &s.d[0], (size_t)p.length);
}

bool read_head(Src &s, size_t extra, Head &h)
{
    s.pos = 0;
    h.version = (word)s.w();
    h.cversion = (word)s.w();
    h.extra = extra;
    s.skip(extra);
    h.size = (dword)s.l();
    h.body = s.pos;
    return !s.bad;
}

int emit(PACKFILE *f, dword id, const Src &orig, const Head &h, const Buf &body)
{
    Buf out;
    out.put(&orig.d[0], h.body - 4);       // version, cversion and any extra field
    size_t old_len = orig.d.size() - h.body;
    out.l(body.size() == old_len ? (long)h.size : (long)body.size());
    out.put(body);

    // ZQW_E_LOG=file: how much of each written part differs from the original, to see the
    // encoders at work (the round-trip check forces every field through them).
    const char *log = getenv("ZQW_E_LOG");

    if(log)
    {
        FILE *lf = fopen(log, "a");

        if(lf)
        {
            long diff = 0;

            for(size_t i = 0; i < out.size() && i < orig.d.size(); ++i)
                if(out.b[i] != orig.d[i])
                    ++diff;

            fprintf(lf, "%c%c%c%c v%d%s: %lu bytes, was %lu, %ld differ\n", (char)(id >> 24), (char)(id >> 16), (char)(id >> 8), (char)id,
                    h.version, zqw_force_serialize ? " forced" : "", (unsigned long)out.size(), (unsigned long)orig.d.size(), diff);
            fclose(lf);
        }
    }

    if(pack_mputl((long)id, f) == EOF)
        return 1;

    if(!out.b.empty() && pack_fwrite(&out.b[0], (long)out.b.size(), f) != (long)out.b.size())
        return 1;

    return ZQW_OK;
}

bool Bounded::read(Src &s, int sz_bytes, int k_bytes, int v_bytes)
{
    size_bytes = sz_bytes;
    key_bytes = k_bytes;
    value_bytes = v_bytes;
    start = s.pos;
    keys.clear();
    values.clear();
    pairs = false;

    declared = sz_bytes == 1 ? s.c() : (sz_bytes == 2 ? s.w() : s.l());

    if(declared != 0)
    {
        pairs = s.c() != 0;
        long count = sz_bytes == 1 ? s.c() : (sz_bytes == 2 ? s.w() : s.l());

        if(count < 0 || (size_t)count > s.left())
        {
            s.bad = true;
            return false;
        }

        for(long q = 0; q < count && !s.bad; ++q)
        {
            long k = q;

            if(pairs)
                k = k_bytes == 1 ? s.c() : (k_bytes == 2 ? s.w() : s.l());

            long v = v_bytes == 1 ? s.c() : (v_bytes == 2 ? s.w() : s.l());
            keys.push_back(k);
            values.push_back(v);
        }
    }

    end = s.pos;
    return !s.bad;
}

void Bounded::write(Buf &o) const
{
    long count = (long)keys.size();
    long decl = declared;

    if(count && !decl)
        decl = 1;

    // Without stored keys the entries are keys 0, 1, 2...: keep that only while it's true.
    bool use_pairs = pairs;

    for(long q = 0; q < count && !use_pairs; ++q)
        if(keys[q] != q)
            use_pairs = true;

    if(size_bytes == 1) o.c(decl); else if(size_bytes == 2) o.w(decl); else o.l(decl);

    if(!decl)
        return;

    o.c(use_pairs ? 1 : 0);

    if(size_bytes == 1) o.c(count); else if(size_bytes == 2) o.w(count); else o.l(count);

    for(long q = 0; q < count; ++q)
    {
        if(use_pairs)
        {
            if(key_bytes == 1) o.c(keys[q]); else if(key_bytes == 2) o.w(keys[q]); else o.l(keys[q]);
        }

        if(value_bytes == 1) o.c(values[q]); else if(value_bytes == 2) o.w(values[q]); else o.l(values[q]);
    }
}

long Bounded::get(long key, long dflt) const
{
    long v = dflt;

    for(size_t q = 0; q < keys.size(); ++q)     // the reader lets the last one win
        if(keys[q] == key)
            v = values[q];

    return v;
}

void Bounded::set(long key, long value)
{
    bool found = false;

    for(size_t q = 0; q < keys.size(); ++q)
        if(keys[q] == key)
        {
            values[q] = value;
            found = true;
        }

    if(!found)
    {
        keys.push_back(key);
        values.push_back(value);
    }
}

} // namespace zqwe

using namespace zqwe;

namespace
{

// ---- what ZQuest held right after the load ------------------------------------------

const int g_legacy_items[] = { iSword, iWSword, iMSword, iXSword, iHookshot, iLongshot, iFairyStill, iFairyMoving, iRPotion, iBPotion };
const int LEGACY_COUNT = sizeof(g_legacy_items) / sizeof(g_legacy_items[0]);

struct Snapshot
{
    bool taken;

    // INIT, and what its old versions set in the item data
    zinitdata z;
    itemdata legacy[LEGACY_COUNT];
    int switch_style;
    bool light_set, box_set;
    int light[6], box[4];
    std::vector<byte> bottles;
    std::map<int, Zq255Store::GenericInit> generic;
    std::map<int, Zq255Store::ScreenData> screen_data;

    // LINK
    int sprites[11][4][3];      // walk, stab, slash, float, swim, dive, pound, jump, charge
    int casting[3];
    int hold[2][2][3];
    byte swim_speed;

    // CHT
    ZCHEATS cheats;
    byte use_cheats;

    // DROP
    item_drop_object drops[MAXITEMDROPSETS];

    // FAVS
    int fav_combos[MAXFAVORITECOMBOS], fav_aliases[MAXFAVORITECOMBOALIASES];

    Snapshot() : taken(false) {}
};

Snapshot g_snap;

int (*const g_sprite_sets[9])[3] = { walkspr, stabspr, slashspr, floatspr, swimspr, divespr, poundspr, jumpspr, chargespr };

int dc_fclose(void *) { return 0; }
int dc_getc(void *) { return EOF; }
int dc_ungetc(int, void *) { return EOF; }
#pragma push_macro("long")  // packfile callbacks use the system's long (zcport/lp64.h)
#undef long
long dc_fread(void *, long, void *) { return 0; }
#pragma pop_macro("long")
int dc_putc(int c, void *) { return c & 0xFF; }
#pragma push_macro("long")  // packfile callbacks use the system's long (zcport/lp64.h)
#undef long
long dc_fwrite(const void *, long n, void *) { return n; }
#pragma pop_macro("long")
int dc_fseek(void *, int) { return -1; }
int dc_feof(void *) { return 1; }
int dc_ferror(void *) { return 0; }
const PACKFILE_VTABLE g_discard = { dc_fclose, dc_getc, dc_ungetc, dc_fread, dc_putc, dc_fwrite, dc_fseek, dc_feof, dc_ferror };

const ZqQuestPart *find_part(dword id)
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == id)
            return &zq_quest_file.parts[i];

    return NULL;
}

void load_255_favorites();
void snapshot_subscreens();

void take_snapshot()
{
    load_255_favorites();

    // ZQuest's 2.53 init data writer sets the custom sound flag every time it runs, so the
    // fingerprints after this see it; the snapshot has to see the same, or every quest would
    // look changed there. (zq_write_253_section keeps the quest's saved view as it was.)
    PACKFILE *f = zq_open_sink(&g_discard, NULL);

    if(f)
    {
        zq_write_253_section(f, ID_INITDATA);
        pack_fclose(f);
    }

    Snapshot &s = g_snap;
    s.taken = true;
    s.z = zinit;

    for(int i = 0; i < LEGACY_COUNT; ++i)
        s.legacy[i] = itemsbuf[g_legacy_items[i]];

    s.switch_style = zq255.init_switch_style;
    s.light_set = zq255.light_init_set;
    s.box_set = zq255.item_box_set;
    memcpy(s.light, zq255.light_init, sizeof(s.light));
    memcpy(s.box, zq255.item_box, sizeof(s.box));
    s.bottles = zq255.init_bottle_slots;
    s.generic = zq255.generic_init;
    s.screen_data = zq255.screen_data_init;

    for(int k = 0; k < 9; ++k)
        memcpy(s.sprites[k], g_sprite_sets[k], sizeof(s.sprites[k]));

    memcpy(s.casting, castingspr, sizeof(s.casting));
    memcpy(s.hold, holdspr, sizeof(s.hold));
    s.swim_speed = zinit.link_swim_speed;

    s.cheats = zcheats;
    s.use_cheats = header.data_flags[ZQ_CHEATS2];
    memcpy(s.drops, item_drop_sets, sizeof(s.drops));
    memcpy(s.fav_combos, favorite_combos, sizeof(s.fav_combos));
    memcpy(s.fav_aliases, favorite_comboaliases, sizeof(s.fav_aliases));

    snapshot_subscreens();

    // ZQW_E_MUTATE (zqw_init.h): values every version these writers handle can hold
    static bool mutated = false;

    if(mutate_now(mutated))
    {
        zinit.bombs ^= 1;
        zinit.keys ^= 1;
        zinit.rupies ^= 1;
        walkspr[0][spr_tile] ^= 1;
        item_drop_sets[0].chance[1] ^= 1;
        zcheats.codes[0][0] ^= 1;

        const ZqQuestPart *fav = find_part(ID_FAVORITES);
        word fv = 0, fcv = 0;

        if(fav && zq_part_versions(*fav, &fv, &fcv) && fv == 1)
            favorite_combos[0] = favorite_combos[0] >= 0 ? (favorite_combos[0] ^ 1) : 1;   // -1 is an empty one

        for(int i = 0; i < MAXCUSTOMSUBSCREENS; ++i)
            if(custom_subscreen[i].objects[0].type > ssoNONE)
            {
                custom_subscreen[i].objects[0].x ^= 1;
                break;
            }
    }
}

ZqLoadHookRegistration g_hook(take_snapshot);

// The part to write, read whole, and its head. FAILED: it isn't there or is cut short.
bool start(const ZqPartContext &ctx, Src &s, Head &h, size_t extra = 0)
{
    if(!load_part(*ctx.part, s) || !read_head(s, extra, h))
    {
        set_why(zq_part_name(*ctx.part) + ": the kept copy of the part couldn't be read");
        return false;
    }

    return true;
}

int finish(PACKFILE *f, const ZqPartContext &ctx, Src &s, const Head &h, Patch &p, Buf &body)
{
    if(s.bad)
    {
        set_why(zq_part_name(*ctx.part) + ": the part doesn't have the layout its version says");
        return BAD_PART;
    }

    if(p.cant)
        return cant_hold(zq_part_name(*ctx.part), p.what);

    p.copy(s.left());       // nothing the reader reads should be left, but keep it if there is
    return emit(f, ctx.part->id, s, h, body);
}

// ---- CHT --------------------------------------------------------------------------------

int write_cheats(PACKFILE *f, const ZqPartContext &ctx)
{
    // 2.53.0/2.53.1 quests too: the 2.53 fallback writes the codes by ZQuest's current cheats
    // switch, while the header writer (zqw_basic.cpp) keeps the switch as the file had it, so a
    // toggled switch would leave the two disagreeing on reload.
    if(ctx.version != 1 || ctx.zelda_version <= 0x192)
        return ZQW_UNSUPPORTED;

    Src s;
    Head h;

    if(!start(ctx, s, h))
        return BAD_PART;

    Buf body;
    Patch p(s, body);
    p.u8(header.data_flags[ZQ_CHEATS2], g_snap.use_cheats, "the cheats switch");

    // The codes are there when the header said so at the load; the header keeps saying it.
    bool codes = s.left() >= 4 + sizeof(zcheats.codes);

    if(codes)
    {
        p.x32((long)zcheats.flags, (long)g_snap.cheats.flags);
        p.bytes(zcheats.codes, g_snap.cheats.codes, sizeof(zcheats.codes));
    }
    else if(memcmp(&zcheats, &g_snap.cheats, sizeof(zcheats)) != 0)
        p.fail("cheat codes (the quest's header says it has none)");

    return finish(f, ctx, s, h, p, body);
}

ZqWriterRegistration g_reg_cheats(ID_CHEATS, write_cheats, "cheats v1, 2.10 to 3.0");

// ---- DROP -------------------------------------------------------------------------------

int write_dropsets(PACKFILE *f, const ZqPartContext &ctx)
{
    if(ctx.version != 2 || ctx.zelda_version <= 0x192)
        return ZQW_UNSUPPORTED;

    if(header_is_253(ctx) && ctx.cversion == CV_ITEMDROPSETS)
        return ZQW_UNSUPPORTED;

    Src s;
    Head h;

    if(!start(ctx, s, h))
        return BAD_PART;

    Buf body;
    Patch p(s, body);
    long count = s.w();
    s.pos -= 2;

    // Sets past the file's count are empty after the load; a set given chances since goes in.
    long out = count;

    if(count_item_drop_sets() > out)
        out = count_item_drop_sets();

    if(out > MAXITEMDROPSETS)
        out = count;

    p.u16(out, count);

    for(long i = 0; i < out && !s.bad; ++i)
    {
        if(i >= MAXITEMDROPSETS)
        {
            p.copy(sizeof(item_drop_object));   // past what the reader keeps
            continue;
        }

        Buf cur, old;
        const item_drop_object &c = item_drop_sets[i];
        const item_drop_object &o = g_snap.drops[i];

        cur.put(c.name, sizeof(c.name));
        old.put(o.name, sizeof(o.name));

        for(int j = 0; j < 10; ++j)
        {
            cur.w(c.item[j]);
            old.w(o.item[j]);
        }

        for(int j = 0; j < 11; ++j)
        {
            cur.w(c.chance[j]);
            old.w(o.chance[j]);
        }

        if(i < count)
            p.field(cur.b == old.b, cur.size(), &cur.b[0]);
        else
            body.put(cur);
    }

    return finish(f, ctx, s, h, p, body);
}

ZqWriterRegistration g_reg_dropsets(ID_ITEMDROPSETS, write_dropsets, "item drop sets v2, 2.11 to 3.0");

// ---- FAVS -------------------------------------------------------------------------------

// The player's 2.55 loader skips this editor-only part, so ZQuest fills its favorites from
// a v1 part itself, as the 2.53 reader would. Later versions lay them out differently.
void load_255_favorites()
{
    const ZqQuestPart *part = find_part(ID_FAVORITES);

    if(!part || zq_quest_file.zelda_version < 0x255)
        return;

    Src s;
    Head h;

    if(!load_part(*part, s) || !read_head(s, 0, h) || h.version != 1)
        return;

    long n = s.w();

    for(long i = 0; i < n && !s.bad; ++i)
    {
        long v = s.l();
        if(i < MAXFAVORITECOMBOS && !s.bad)
            favorite_combos[i] = (int)v;
    }

    n = s.w();

    for(long i = 0; i < n && !s.bad; ++i)
    {
        long v = s.l();
        if(i < MAXFAVORITECOMBOALIASES && !s.bad)
            favorite_comboaliases[i] = (int)v;
    }
}

void fav_list(Patch &p, Src &s, Buf &body, const int *cur, const int *snap, int cap)
{
    long n = s.w();
    s.pos -= 2;
    long out = n;

    for(int i = (int)n; i < cap; ++i)
        if(cur[i] != snap[i])
            out = cap;

    p.u16(out, n);

    for(long i = 0; i < out && !s.bad; ++i)
    {
        if(i >= cap)
            p.copy(4);      // the 2.53 reader writes these past its array; nothing here has them
        else if(i < n)
            p.x32(cur[i], snap[i]);
        else
            body.l(cur[i]);
    }
}

int write_favorites(PACKFILE *f, const ZqPartContext &ctx)
{
    if(header_is_253(ctx) && ctx.version == V_FAVORITES && ctx.cversion == CV_FAVORITES)
        return ZQW_UNSUPPORTED;

    if(ctx.zelda_version <= 0x192 || (ctx.version != 1 && (ctx.zelda_version < 0x255 || ctx.version > 4)))
        return ZQW_UNSUPPORTED;

    Src s;
    Head h;

    if(!start(ctx, s, h))
        return BAD_PART;

    Buf body;
    Patch p(s, body);

    if(h.version == 1)
    {
        fav_list(p, s, body, favorite_combos, g_snap.fav_combos, MAXFAVORITECOMBOS);
        fav_list(p, s, body, favorite_comboaliases, g_snap.fav_aliases, MAXFAVORITECOMBOALIASES);
    }
    else
    {
        if(memcmp(favorite_combos, g_snap.fav_combos, sizeof(g_snap.fav_combos)) ||
           memcmp(favorite_comboaliases, g_snap.fav_aliases, sizeof(g_snap.fav_aliases)))
            p.fail("changed favorites (it lays them out differently from 2.53)");

        p.copy(s.left());
    }

    return finish(f, ctx, s, h, p, body);
}

ZqWriterRegistration g_reg_favorites(ID_FAVORITES, write_favorites, "favorites v1, v3, v4, 2.50 to 3.0");

// ---- LINK -------------------------------------------------------------------------------

// tile, flip and extend of one sprite. wide: tiles are longs (2.55 v6 and up), and the 2.55
// reader turns values it doesn't know into 0, so those can't be saved.
void sprite(Patch &p, const int *cur, const int *snap, bool wide, bool checked)
{
    if(wide)
    {
        if(!p.keep(cur[spr_tile] == snap[spr_tile]) && (cur[spr_tile] < 0 || cur[spr_tile] >= NEWMAXTILES))
            p.fail("a Link sprite's tile");
        p.x32(cur[spr_tile], snap[spr_tile]);
    }
    else
        p.u16(cur[spr_tile], snap[spr_tile], "a Link sprite's tile");

    p.u8(cur[spr_flip], snap[spr_flip], "a Link sprite's flip");
    p.u8(cur[spr_extend], snap[spr_extend], "a Link sprite's size");

    if(checked && !p.keep(cur[spr_flip] == snap[spr_flip]) && cur[spr_flip] > 3)
        p.fail("a Link sprite's flip");
    if(checked && !p.keep(cur[spr_extend] == snap[spr_extend]) && cur[spr_extend] > 2)
        p.fail("a Link sprite's size");
}

void sprite_set(Patch &p, int k, bool wide, bool checked)
{
    for(int i = 0; i < 4; ++i)
        sprite(p, g_sprite_sets[k][i], g_snap.sprites[k][i], wide, checked);
}

bool set_same(int k)
{
    return memcmp(g_sprite_sets[k], g_snap.sprites[k], sizeof(g_snap.sprites[k])) == 0;
}

enum { S_WALK, S_STAB, S_SLASH, S_FLOAT, S_SWIM, S_DIVE, S_POUND, S_JUMP, S_CHARGE };

int write_linksprites(PACKFILE *f, const ZqPartContext &ctx)
{
    const int v = ctx.version;
    bool q255 = ctx.zelda_version >= 0x255;

    if(header_is_253(ctx) && v == V_LINKSPRITES && ctx.cversion == CV_LINKSPRITES)
        return ZQW_UNSUPPORTED;

    if(q255 ? (v < 5 || v > 16) : (v < 1 || v > 5))
        return ZQW_UNSUPPORTED;

    Src s;
    Head h;

    if(!start(ctx, s, h))
        return BAD_PART;

    Buf body;
    Patch p(s, body);
    bool wide = q255 && v >= 6;

    if(q255)
    {
        // q255_read_linksprites
        for(int k = S_WALK; k <= S_POUND; ++k)
            sprite_set(p, k, wide, true);

        sprite(p, castingspr, g_snap.casting, wide, true);

        for(int i = 0; i < 2; ++i)
        {
            sprite(p, holdspr[i][0], g_snap.hold[i][0], wide, true);
            sprite(p, holdspr[i][1], g_snap.hold[i][1], wide, true);
            if(v > 6)
                p.copy(6);      // sword hold
        }

        sprite_set(p, S_JUMP, wide, true);
        sprite_set(p, S_CHARGE, wide, true);
        p.u8(zinit.link_swim_speed, g_snap.swim_speed, "Link's swim speed");
        p.copy(s.left());       // everything after it is upstream's own
    }
    else
    {
        // readlinksprites2
        sprite_set(p, S_WALK, false, false);
        sprite_set(p, S_STAB, false, false);
        sprite_set(p, S_SLASH, false, false);
        sprite_set(p, S_FLOAT, false, false);

        if(v > 1)
            sprite_set(p, S_SWIM, false, false);
        else if(!set_same(S_SWIM))
            p.fail("swimming sprites");

        sprite_set(p, S_DIVE, false, false);
        sprite_set(p, S_POUND, false, false);
        sprite(p, castingspr, g_snap.casting, false, false);

        for(int i = 0; i < 2; ++i)
            for(int j = 0; j < 2; ++j)
                sprite(p, holdspr[i][j], g_snap.hold[i][j], false, false);

        if(v > 2)
            sprite_set(p, S_JUMP, false, false);
        else if(!set_same(S_JUMP))
            p.fail("jumping sprites");

        if(v > 3)
            sprite_set(p, S_CHARGE, false, false);
        else if(!set_same(S_CHARGE))
            p.fail("charging sprites");

        if(v > 4)
            p.u8(zinit.link_swim_speed, g_snap.swim_speed, "Link's swim speed");
        else if(zinit.link_swim_speed != g_snap.swim_speed)
            p.fail("Link's swim speed");
    }

    return finish(f, ctx, s, h, p, body);
}

ZqWriterRegistration g_reg_linksprites(ID_LINKSPRITES, write_linksprites, "Link sprites v1-v5, 2.55 v5-v16");

// ---- INIT -------------------------------------------------------------------------------

const zinitdata &S() { return g_snap.z; }
const zinitdata &Z() { return zinit; }

const itemdata &snap_item(int id)
{
    for(int i = 0; i < LEGACY_COUNT; ++i)
        if(g_legacy_items[i] == id)
            return g_snap.legacy[i];

    return itemsbuf[id];
}

// INIT v1's old style items: the levels of a family the quest starts with, as bits.
long family_levels(const bool *items, int family)
{
    long levels = 0;

    for(int i = 0; i < 8; ++i)
    {
        int id = getItemID(itemsbuf, family, i + 1);

        if(id >= 0 && items[id])
            levels |= 1 << i;
    }

    return levels;
}

// Which families v1 stores, in the order of its bytes, and those whose first level isn't
// stored (the reader shifts them up one).
struct OldFamily
{
    int family;
    bool shifted;
};

const OldFamily g_old_families_a[] =
{
    { itype_ring, true }, { itype_sword, false }, { itype_shield, false }, { itype_wallet, false },
    { itype_bracelet, true }, { itype_amulet, false }, { itype_bow, false }
};
const OldFamily g_old_families_b[] =
{
    { itype_candle, false }, { itype_brang, false }, { itype_arrow, false }, { itype_potion, false }, { itype_whistle, false }
};
const OldFamily g_old_families_c[] =
{
    { itype_wand, false }, { itype_letter, false }, { itype_lens, false }, { itype_hookshot, false }, { itype_bait, false },
    { itype_hammer, false }, { itype_dinsfire, false }, { itype_faroreswind, false }, { itype_nayruslove, false }
};

const int g_old_equipment[] = { iRaft, iLadder, iBook, iMKey, iFlippers, iBoots };

void old_family_byte(Patch &p, const OldFamily &fam)
{
    long cur = family_levels(Z().items, fam.family);
    long old = family_levels(S().items, fam.family);

    if(fam.shifted)
    {
        cur >>= 1;
        old >>= 1;
    }

    p.u8(cur, old);
}

// What v1's item bytes give back on a load: the items they name, and the small shield the
// reader adds to every quest this old.
void old_items_from(const byte *eq, const byte *a, const byte *b, const byte *c, bool *items)
{
    zinitdata t;
    memset(&t, 0, sizeof(t));

    for(int i = 0; i < 6; ++i)
        if(eq[i])
            t.items[g_old_equipment[i]] = true;

    for(int i = 0; i < 7; ++i)
    {
        char levels = (char)a[i];
        if(g_old_families_a[i].shifted)
            levels <<= 1;
        addOldStyleFamily(&t, itemsbuf, g_old_families_a[i].family, levels);
    }

    for(int i = 0; i < 5; ++i)
        addOldStyleFamily(&t, itemsbuf, g_old_families_b[i].family, (char)b[i]);

    for(int i = 0; i < 9; ++i)
        addOldStyleFamily(&t, itemsbuf, g_old_families_c[i].family, (char)c[i]);

    int shield = getItemID(itemsbuf, itype_shield, i_smallshield);

    if(shield != -1)
        t.items[shield] = true;

    memcpy(items, t.items, sizeof(t.items));
}

void bytes_u8(Patch &p, const byte *cur, const byte *snap, int n)
{
    for(int i = 0; i < n; ++i)
        p.u8(cur[i], snap[i]);
}

// Fields a version doesn't have: they must still be as the load left them.
void not_held(Patch &p, bool same, const char *what)
{
    if(!same)
        p.fail(what);
}

#define ZSAME(f) (Z().f == S().f)
#define ZARR_SAME(f) (memcmp(Z().f, S().f, sizeof(Z().f)) == 0)

// readinitdata, section version 1, for quests whose header is above 1.92 (2.10, 2.11 builds
// before 15).
void init_v1(Patch &p, Src &s)
{
    const zinitdata &z = Z();
    const zinitdata &o = S();
    static const int swords[4] = { iSword, iWSword, iMSword, iXSword };

    // init_v1_check_items checks that these give back ZQuest's items
    for(int i = 0; i < 6; ++i)
        p.bool8(z.items[g_old_equipment[i]], o.items[g_old_equipment[i]]);

    for(int i = 0; i < 7; ++i)
        old_family_byte(p, g_old_families_a[i]);

    for(int i = 0; i < 5; ++i)
        old_family_byte(p, g_old_families_b[i]);

    p.u8(z.bombs, o.bombs);
    p.u8(z.super_bombs, o.super_bombs);

    for(int i = 0; i < 9; ++i)
        old_family_byte(p, g_old_families_c[i]);

    p.copy(1);      // a byte the reader reads and drops

    p.u8(z.hc, o.hc);
    p.u8(z.start_heart, o.start_heart, "starting hearts past 255");
    p.u8(z.cont_heart, o.cont_heart, "continue hearts past 255");
    p.u8(z.hcp, o.hcp);
    p.u8(z.max_bombs, o.max_bombs);
    p.u8(z.keys, o.keys);
    p.u16(z.rupies, o.rupies);
    p.u8(z.triforce, o.triforce);
    bytes_u8(p, z.map, o.map, 32);
    bytes_u8(p, z.compass, o.compass, 32);
    bytes_u8(p, z.boss_key, o.boss_key, 32);
    bytes_u8(p, z.misc, o.misc, 16);

    // what v1 keeps for the swords is stored in their item data since 2.11
    for(int i = 0; i < 4; ++i)
        p.u8(itemsbuf[swords[i]].pickup_hearts, snap_item(swords[i]).pickup_hearts, "a sword's hearts needed");

    p.u8(z.last_map, o.last_map);
    p.u8(z.last_screen, o.last_screen);

    // stored in blocks
    if(!p.keep(z.max_magic == o.max_magic) && z.max_magic % MAGICPERBLOCK)
        p.fail("magic that isn't whole blocks");
    p.u8(z.max_magic / MAGICPERBLOCK, o.max_magic / MAGICPERBLOCK, "magic past 255 blocks");
    if(!p.keep(z.magic == o.magic) && z.magic % MAGICPERBLOCK)
        p.fail("magic that isn't whole blocks");
    p.u8(z.magic / MAGICPERBLOCK, o.magic / MAGICPERBLOCK, "magic past 255 blocks");

    for(int i = 0; i < 4; ++i)
        p.u8(itemsbuf[swords[i]].misc1, snap_item(swords[i]).misc1, "a sword beam's hearts needed");

    long percent = 0, old_percent = 0;

    for(int i = 0; i < 4; ++i)
    {
        if(itemsbuf[swords[i]].flags & ITEM_FLAG1)
            percent |= 1 << i;
        if(snap_item(swords[i]).flags & ITEM_FLAG1)
            old_percent |= 1 << i;
    }

    // the byte's other bits mean nothing to the reader
    {
        long orig = s.has(1) ? s.d[s.pos] : 0;
        p.u8((orig & ~15) | percent, (orig & ~15) | old_percent);
    }

    for(int i = 0; i < 4; ++i)
        p.u8(itemsbuf[swords[i]].misc2, snap_item(swords[i]).misc2, "a sword beam's power past 255");

    p.u8(itemsbuf[iHookshot].misc2, snap_item(iHookshot).misc2, "a hookshot chain past 255 links");
    p.u8(z.msg_more_x, o.msg_more_x);
    p.u8(z.msg_more_y, o.msg_more_y);
    p.u8(z.subscreen, o.subscreen);
    p.u8(z.start_dmap, o.start_dmap, "a starting DMap past 255");
    p.u8(z.linkanimationstyle, o.linkanimationstyle);

    // What v1 doesn't have, the reader sets to its defaults (some by quest rules), and the
    // load left those in ZQuest's data.
    not_held(p, ZSAME(arrows) && ZSAME(max_arrows), "arrows");
    not_held(p, ZARR_SAME(level_keys), "level keys");
    not_held(p, ZSAME(ss_grid_x) && ZSAME(ss_grid_y) && ZSAME(ss_grid_xofs) && ZSAME(ss_grid_yofs) && ZSAME(ss_grid_color) &&
             ZSAME(ss_bbox_1_color) && ZSAME(ss_bbox_2_color) && ZSAME(ss_flags), "the subscreen editor's grid");
    not_held(p, ZSAME(subscreen_style), "the subscreen style");
    not_held(p, ZSAME(max_rupees) && ZSAME(max_keys), "the rupee and key limits");
    not_held(p, ZSAME(hcp_per_hc) && ZSAME(bomb_ratio), "heart pieces per container or the bomb ratio");
    not_held(p, ZSAME(gravity) && ZSAME(terminalv) && ZSAME(jump_link_layer_threshold), "gravity");
    not_held(p, ZSAME(msg_speed) && ZSAME(transition_type) && ZSAME(msg_more_is_offset), "the message speed");
    not_held(p, memcmp(z.map + 32, o.map + 32, 32) == 0 && memcmp(z.compass + 32, o.compass + 32, 32) == 0 &&
             memcmp(z.boss_key + 32, o.boss_key + 32, 32) == 0, "levels past 255");

    // What the reader sets in the item data for things v1 doesn't store.
    struct Fixed
    {
        int item;
        long misc1, misc2, misc3;
        bool misc3_set;
        word flags;
    };

    static const Fixed fixed[] =
    {
        { iFairyStill, 3, 0, 0, true, 0 }, { iFairyMoving, 3, 0, 50, true, 0 },
        { iRPotion, 100, 100, 0, false, ITEM_FLAG1 | ITEM_FLAG2 }, { iBPotion, 100, 100, 0, false, ITEM_FLAG1 | ITEM_FLAG2 },
        { iLongshot, 99, 100, 0, false, 0 }
    };

    for(size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i)
    {
        const itemdata &it = itemsbuf[fixed[i].item];
        const itemdata &was = snap_item(fixed[i].item);
        bool same = it.misc1 == was.misc1 && it.misc2 == was.misc2 && it.misc3 == was.misc3 && it.flags == was.flags;
        bool fits = it.misc1 == fixed[i].misc1 && it.misc2 == fixed[i].misc2 && (!fixed[i].misc3_set || it.misc3 == fixed[i].misc3) &&
                    (it.flags & fixed[i].flags) == fixed[i].flags;

        if(!same && !fits)
            p.fail("fairy, potion and longshot values the item editor changed (this version's init data resets them)");
    }

    if(!p.keep(itemsbuf[iHookshot].misc1 == snap_item(iHookshot).misc1) && itemsbuf[iHookshot].misc1 != 99)
        p.fail("the hookshot's length (this version's init data resets it)");
}

void init_v1_check_items(Patch &p, size_t body_at)
{
    // The bytes as written: 6 equipment, 7 + 5 families, 2 bombs, 9 families.
    if(p.cant || p.o.size() < body_at + 6 + 7 + 5 + 2 + 9)
        return;

    const byte *b = &p.o.b[body_at];
    bool items[256];
    old_items_from(b, b + 6, b + 13, b + 20, items);

    for(int i = 0; i < 256; ++i)
        if(items[i] != Z().items[i])
        {
            p.fail("starting items other than the first item of each old family and level");
            return;
        }
}

// readinitdata, section version 18 (2.11 build 18, 2.50.x): what 2.53 writes, field by field.
void init_v18(Patch &p)
{
    const zinitdata &z = Z();
    const zinitdata &o = S();

    for(int i = 0; i < MAXITEMS; ++i)
        p.bool8(z.items[i], o.items[i]);

    p.u8(z.bombs, o.bombs);
    p.u8(z.super_bombs, o.super_bombs);
    p.u8(z.hc, o.hc);
    p.u16(z.start_heart, o.start_heart);
    p.u16(z.cont_heart, o.cont_heart);
    p.u8(z.hcp, o.hcp);
    p.u8(z.hcp_per_hc, o.hcp_per_hc);
    p.u8(z.max_bombs, o.max_bombs);
    p.u8(z.keys, o.keys);
    p.u16(z.rupies, o.rupies);
    p.u8(z.triforce, o.triforce);
    bytes_u8(p, z.map, o.map, 64);
    bytes_u8(p, z.compass, o.compass, 64);
    bytes_u8(p, z.boss_key, o.boss_key, 64);
    bytes_u8(p, z.misc, o.misc, 16);
    p.u8(z.last_map, o.last_map);
    p.u8(z.last_screen, o.last_screen);
    p.u16(z.max_magic, o.max_magic);
    p.u16(z.magic, o.magic);
    p.u8(z.bomb_ratio, o.bomb_ratio);
    p.u8(z.msg_more_x, o.msg_more_x);
    p.u8(z.msg_more_y, o.msg_more_y);
    p.u8(z.subscreen, o.subscreen);
    p.u16(z.start_dmap, o.start_dmap);
    p.u8(z.linkanimationstyle, o.linkanimationstyle);
    p.u8(z.arrows, o.arrows);
    p.u8(z.max_arrows, o.max_arrows);
    bytes_u8(p, z.level_keys, o.level_keys, MAXLEVELS);

    // the reader raises a grid size of 0 to 1
    if(!p.keep(z.ss_grid_x == o.ss_grid_x) && z.ss_grid_x < 1)
        p.fail("a subscreen grid width of 0");
    if(!p.keep(z.ss_grid_y == o.ss_grid_y) && z.ss_grid_y < 1)
        p.fail("a subscreen grid height of 0");

    p.u16(z.ss_grid_x, o.ss_grid_x);
    p.u16(z.ss_grid_y, o.ss_grid_y);
    p.u16(z.ss_grid_xofs, o.ss_grid_xofs);
    p.u16(z.ss_grid_yofs, o.ss_grid_yofs);
    p.u16(z.ss_grid_color, o.ss_grid_color);
    p.u16(z.ss_bbox_1_color, o.ss_bbox_1_color);
    p.u16(z.ss_bbox_2_color, o.ss_bbox_2_color);
    p.u16(z.ss_flags, o.ss_flags);
    p.u8(z.subscreen_style, o.subscreen_style);
    p.u8(z.usecustomsfx, o.usecustomsfx);
    p.u16(z.max_rupees, o.max_rupees);
    p.u16(z.max_keys, o.max_keys);
    p.u8(z.gravity, o.gravity);
    p.u16(z.terminalv, o.terminalv);
    p.u8(z.msg_speed, o.msg_speed);
    p.u8(z.transition_type, o.transition_type);
    p.u8(z.jump_link_layer_threshold, o.jump_link_layer_threshold);
    p.u8(z.msg_more_is_offset, o.msg_more_is_offset);
}

// ---- INIT, 2.55 and 3.0 (q255_read_initdata) ---------------------------------------------

bool store_generic_same()
{
    const std::map<int, Zq255Store::GenericInit> &a = zq255.generic_init, &b = g_snap.generic;

    if(a.size() != b.size())
        return false;

    std::map<int, Zq255Store::GenericInit>::const_iterator i = a.begin(), j = b.begin();

    for(; i != a.end(); ++i, ++j)
    {
        const Zq255Store::GenericInit &x = i->second, &y = j->second;

        if(i->first != j->first || x.run != y.run || x.exit_state != y.exit_state || x.reload_state != y.reload_state ||
           x.event_state != y.event_state || memcmp(x.initd, y.initd, sizeof(x.initd)) || x.data != y.data)
            return false;
    }

    return true;
}

bool store_screen_data_same()
{
    const std::map<int, Zq255Store::ScreenData> &a = zq255.screen_data_init, &b = g_snap.screen_data;

    if(a.size() != b.size())
        return false;

    std::map<int, Zq255Store::ScreenData>::const_iterator i = a.begin(), j = b.begin();

    for(; i != a.end(); ++i, ++j)
        if(i->first != j->first || i->second.size != j->second.size || i->second.values != j->second.values)
            return false;

    return true;
}

void light_byte(Patch &p, int i)
{
    p.u8(zq255.light_init[i], g_snap.light[i], "a lighting default");
}

void double_magic_byte(Patch &p)
{
    bool cur = get_bit((byte *)Z().misc, idM_DOUBLEMAGIC) != 0;
    bool old = get_bit((byte *)S().misc, idM_DOUBLEMAGIC) != 0;
    long orig = p.s.has(1) ? p.s.d[p.s.pos] : 0;
    // the reader: a drain rate of 1 or less is double magic's half cost
    long enc = cur ? (orig <= 1 ? orig : 1) : (orig > 1 ? orig : 2);
    byte e[1] = { (byte)enc };
    p.field(cur == old, 1, e);
}

// Walks one of the readers' skipped containers, copying it.
void copy_bounded(Patch &p, int sz_bytes, int value_bytes)
{
    Src &s = p.s;
    size_t at = s.pos;
    Bounded b;
    b.read(s, sz_bytes, sz_bytes, value_bytes);
    size_t end = s.pos;
    s.pos = at;
    p.copy(end - at);
}

void copy_bounded_nested(Patch &p, int sz_bytes, void (*value)(Src &))
{
    Src &s = p.s;
    size_t at = s.pos;
    long sz = sz_bytes == 2 ? s.w() : s.l();

    if(sz)
    {
        bool pairs = s.c() != 0;
        long count = sz_bytes == 2 ? s.w() : s.l();

        for(long q = 0; q < count && !s.bad; ++q)
        {
            if(pairs)
                s.skip(sz_bytes);
            value(s);
        }
    }

    size_t end = s.pos;
    s.pos = at;
    p.copy(end - at);
}

void walk_initd(Src &s)
{
    Bounded b;
    b.read(s, 1, 1, 4);
}

void walk_long_map(Src &s)
{
    Bounded b;
    b.read(s, 4, 4, 4);
}

void walk_inst_init(Src &s)
{
    long n = s.l();
    if(n < 0 || (size_t)n > s.left() / 6)
        s.bad = true;
    else
        s.skip((size_t)n * 6);
}

void walk_script_args(Src &s)
{
    long n = s.w();
    s.skip((size_t)(n < 8 ? n : 8) * 4);
    long m = s.w();
    if((size_t)m > s.left() / 6)
        s.bad = true;
    else
        s.skip((size_t)m * 6);
}

void copy_script_config_array(Patch &p, int capacity)
{
    Src &s = p.s;
    size_t at = s.pos;
    long n = s.w();

    for(long q = 0; q < n && q < capacity && !s.bad; ++q)
        walk_script_args(s);

    size_t end = s.pos;
    s.pos = at;
    p.copy(end - at);
}

// zinit fields the v37+ layout has no place for: the reader leaves them 0 (or sets them).
void init_new_not_held(Patch &p, int v)
{
    const zinitdata &z = Z();
    const zinitdata &o = S();
    not_held(p, ZSAME(subscreen) && ZSAME(subscreen_style) && ZSAME(transition_type), "a subscreen type, style or transition type");

    byte misc_z[16], misc_o[16];
    memcpy(misc_z, z.misc, 16);
    memcpy(misc_o, o.misc, 16);
    misc_z[0] &= ~7;
    misc_o[0] &= ~7;
    not_held(p, memcmp(misc_z, misc_o, 16) == 0, "init data flags other than continue percent, double magic and can slash");

    if(v >= 49)
        not_held(p, ZSAME(msg_more_x) && ZSAME(msg_more_y) && ZSAME(msg_more_is_offset), "the \"more\" icon's place (it moved to each string)");
}

// Level item bits of the v42+ layout: 1 McGuffin, 2 map, 4 compass, 8 boss killed, 16 boss key.
long level_bits(const zinitdata &z, int level)
{
    long b = 0;

    if(get_bit((byte *)z.map, level)) b |= 2;
    if(get_bit((byte *)z.compass, level)) b |= 4;
    if(get_bit((byte *)z.boss_key, level)) b |= 16;
    if(level >= 1 && level <= 8 && (z.triforce & (1 << (level - 1)))) b |= 1;

    return b;
}

void init_new(Patch &p, int v)
{
    Src &s = p.s;
    const zinitdata &z = Z();
    const zinitdata &o = S();

    // items, as bits
    {
        byte cur[32], old[32];
        memset(cur, 0, 32);
        memset(old, 0, 32);

        for(int i = 0; i < 256; ++i)
        {
            if(z.items[i]) cur[i / 8] |= 1 << (i % 8);
            if(o.items[i]) old[i / 8] |= 1 << (i % 8);
        }

        if(v >= 47)
        {
            size_t at = s.pos;
            Bounded b;
            b.read(s, 2, 2, 1);

            if(p.keep(memcmp(cur, old, 32) == 0))
            {
                s.pos = at;
                p.copy(b.end - at);
            }
            else
            {
                if(b.declared == 0)
                    b.declared = 32;
                for(int i = 0; i < 32; ++i)
                    if(b.get(i, 0) != cur[i])
                        b.set(i, cur[i]);
                b.write(p.o);
            }
        }
        else
            p.bytes(cur, old, 32);
    }

    // level items
    if(v >= 42)
    {
        for(int q = 0; q < MAXLEVELS; ++q)
        {
            long mask = 2 | 4 | 16 | ((q >= 1 && q <= 8) ? 1 : 0);
            long cur = level_bits(z, q), old = level_bits(o, q);
            long orig = v >= 44 ? (s.has(2) ? (s.d[s.pos] | (s.d[s.pos + 1] << 8)) : 0) : (s.has(1) ? s.d[s.pos] : 0);
            long enc = (orig & ~mask) | cur;

            if(v >= 44)
                p.u16(enc, (orig & ~mask) | old);
            else
                p.u8(enc, (orig & ~mask) | old);
        }
    }
    else
    {
        for(int q = 0; q < MAXLEVELS / 8; ++q)
        {
            p.u8(z.map[q], o.map[q]);
            p.u8(z.compass[q], o.compass[q]);
            p.u8(z.boss_key[q], o.boss_key[q]);

            long mask = 0, cur = 0, old = 0;

            for(int b = 0; b < 8; ++b)
            {
                int level = q * 8 + b;

                if(level >= 1 && level <= 8)
                {
                    mask |= 1 << b;
                    if(z.triforce & (1 << (level - 1))) cur |= 1 << b;
                    if(o.triforce & (1 << (level - 1))) old |= 1 << b;
                }
            }

            long orig = s.has(1) ? s.d[s.pos] : 0;
            p.u8((orig & ~mask) | cur, (orig & ~mask) | old);
        }
    }

    // level keys
    {
        size_t at = s.pos;
        Bounded b;
        b.read(s, 2, 2, 1);
        bool same = memcmp(z.level_keys, o.level_keys, MAXLEVELS) == 0;

        if(p.keep(same))
        {
            s.pos = at;
            p.copy(b.end - at);
        }
        else
        {
            if(b.declared == 0)
                b.declared = MAXLEVELS;
            for(int i = 0; i < MAXLEVELS; ++i)
                if(b.get(i, 0) != z.level_keys[i])
                    b.set(i, z.level_keys[i]);
            b.write(p.o);
        }
    }

    // counters: life, money, bombs, arrows, magic, keys, super bombs, then upstream's own
    long n = s.has(1) ? s.d[s.pos] : 0;
    long hp;
    {
        Src look = s;
        look.skip(1 + 4 * (size_t)n + 1 + 1 + 1 + 2);
        hp = look.c();
        if(!hp)
            hp = 16;
    }

    bool percent = get_bit((byte *)z.misc, idM_CONTPERCENT) != 0;
    bool old_percent = get_bit((byte *)o.misc, idM_CONTPERCENT) != 0;
    p.copy(1);

    const long start_cur[7] = { z.start_heart * hp, z.rupies, z.bombs, z.arrows, z.magic, z.keys, z.super_bombs };
    const long start_old[7] = { o.start_heart * hp, o.rupies, o.bombs, o.arrows, o.magic, o.keys, o.super_bombs };
    const long max_cur[7] = { z.hc * hp, z.max_rupees, z.max_bombs, z.max_arrows, z.max_magic, z.max_keys, 0 };
    const long max_old[7] = { o.hc * hp, o.max_rupees, o.max_bombs, o.max_arrows, o.max_magic, o.max_keys, 0 };

    for(long q = 0; q < n; ++q)
    {
        if(q < 7)
            p.u16(start_cur[q], start_old[q], "a counter past 65535");
        else
            p.copy(2);
    }

    for(long q = 0; q < n; ++q)
    {
        if(q < 6)
            p.u16(max_cur[q], max_old[q], "a counter's limit past 65535");
        else
            p.copy(2);
    }

    for(long q = n; q < 7; ++q)
        if(start_cur[q] != start_old[q] || (q < 6 && max_cur[q] != max_old[q]))
            p.fail("a counter the quest doesn't store");

    p.u8(z.bomb_ratio, o.bomb_ratio);
    p.u8(z.hcp, o.hcp);
    p.u8(z.hcp_per_hc, o.hcp_per_hc);
    // stored in HP unless it's a percentage, so the flag below decides what this means
    p.put16(z.cont_heart == o.cont_heart && percent == old_percent, percent ? z.cont_heart : z.cont_heart * hp, "continue hearts past 65535");
    p.copy(1);      // HP per heart
    p.copy(1);      // magic per block
    p.copy(2);      // damage multipliers

    for(int i = 0; i < 6; ++i)
        light_byte(p, i);

    // the subscreen editor's grid, signed longs here
    if(!p.keep(z.ss_grid_x == o.ss_grid_x) && z.ss_grid_x < 1)
        p.fail("a subscreen grid width of 0");
    if(!p.keep(z.ss_grid_y == o.ss_grid_y) && z.ss_grid_y < 1)
        p.fail("a subscreen grid height of 0");

    p.x32(z.ss_grid_x, o.ss_grid_x);
    p.x32(z.ss_grid_y, o.ss_grid_y);
    p.x32(z.ss_grid_xofs, o.ss_grid_xofs);
    p.x32(z.ss_grid_yofs, o.ss_grid_yofs);
    p.x32(z.ss_grid_color, o.ss_grid_color);
    p.x32(z.ss_bbox_1_color, o.ss_bbox_1_color);
    p.x32(z.ss_bbox_2_color, o.ss_bbox_2_color);
    p.x32(z.ss_flags, o.ss_flags);

    // flags: continue percent, can slash, and upstream's others
    {
        long cur = (percent ? 1 : 0) | (get_bit((byte *)z.misc, idM_CANSLASH) ? 2 : 0);
        long old = (old_percent ? 1 : 0) | (get_bit((byte *)o.misc, idM_CANSLASH) ? 2 : 0);
        size_t at = s.pos;
        Bounded b;
        b.read(s, 2, 2, 1);

        if(p.keep(cur == old))
        {
            s.pos = at;
            p.copy(b.end - at);
        }
        else
        {
            if(b.declared == 0)
                b.declared = 1;
            b.set(0, (b.get(0, 0) & ~3) | cur);
            b.write(p.o);
        }
    }

    p.u8(z.last_map, o.last_map);
    p.u8(z.last_screen, o.last_screen);

    if(v < 49)
    {
        p.u8(z.msg_more_x, o.msg_more_x);
        if(!p.keep(z.msg_more_y == o.msg_more_y) && z.msg_more_y > 160)
            p.fail("a \"more\" icon below 160");
        p.u8(z.msg_more_y, o.msg_more_y);
        p.bool8(z.msg_more_is_offset != 0, o.msg_more_is_offset != 0);
    }

    p.u8(z.msg_speed, o.msg_speed);

    if(v >= 49)
        p.copy(1);

    p.put32(z.gravity == o.gravity, z.gravity * 100L);     // in hundredths of 2.53's unit
    p.copy(4);      // swim gravity
    p.u16(z.terminalv, o.terminalv);
    p.u8(z.link_swim_speed, o.link_swim_speed);
    p.copy(2 + 6 + 4 + 2);  // swim multiplier, sideswim steps, exit water jump, walk step
    p.u8(z.linkanimationstyle, o.linkanimationstyle);
    p.u8(z.jump_link_layer_threshold, o.jump_link_layer_threshold);
    p.copy(4);      // bunny LTM
    p.u16(z.start_dmap, o.start_dmap);
    p.copy(2);      // subscreen speed
    p.u8(zq255.init_switch_style, g_snap.switch_style, "a switchhook style");
    double_magic_byte(p);
    p.copy(4);      // shove offset

    // generic scripts' start data: kept as it was; ZQuest has nothing that edits it
    if(!store_generic_same())
        p.fail("changed generic script start data");

    copy_bounded(p, 2, 1);      // which run at the start
    copy_bounded(p, 2, 2);      // exit states
    copy_bounded(p, 2, 2);      // reload states
    copy_bounded_nested(p, 2, walk_initd);
    copy_bounded(p, 2, 4);      // event states
    copy_bounded_nested(p, 2, walk_long_map);

    if(!store_screen_data_same())
        p.fail("changed screen script start data");

    copy_bounded_nested(p, 4, walk_long_map);

    if(v >= 38)
        p.copy(3);
    if(v >= 39)
        p.copy(4);

    if(v == 40 && s.left() == 1)
        p.copy(1);
    else
    {
        if(v >= 40)
            p.copy(4);
        if(v >= 41)
            p.copy(1);
    }

    if(v >= 43)
    {
        const std::vector<byte> &cur = zq255.init_bottle_slots, &old = g_snap.bottles;

        for(int i = 0; i < 256; ++i)
            p.u8(i < (int)cur.size() ? cur[i] : 0, i < (int)old.size() ? old[i] : 0, "a bottle's contents");
    }

    if(v >= 44)
        copy_bounded(p, 2, 4);      // level switch states
    if(v >= 45)
        p.copy(7);
    if(v >= 46)
        p.copy(10);

    if(v >= 48)
    {
        p.s8(zq255.item_box[0], g_snap.box[0], "the item box's place");
        p.s8(zq255.item_box[1], g_snap.box[1], "the item box's place");
        p.u8(zq255.item_box[2], g_snap.box[2], "the item box's size");
        p.u8(zq255.item_box[3], g_snap.box[3], "the item box's size");
    }

    if(v >= 50)
    {
        copy_bounded_nested(p, 2, walk_inst_init);
        copy_script_config_array(p, 8);
        copy_script_config_array(p, 5);
    }

    if(v >= 51)
        p.copy(2);
    if(v >= 52)
        p.copy(12);

    init_new_not_held(p, v);
}

// v18 to v36 of the 2.55 reader
void init_255_old(Patch &p, int v)
{
    Src &s = p.s;
    const zinitdata &z = Z();
    const zinitdata &o = S();

    for(int i = 0; i < 256; ++i)
        p.bool8(z.items[i], o.items[i]);

    if(v < 29)
    {
        p.u8(z.bombs, o.bombs);
        p.u8(z.super_bombs, o.super_bombs);
    }

    p.u8(z.hc, o.hc);
    p.u16(z.start_heart, o.start_heart);
    p.u16(z.cont_heart, o.cont_heart);
    p.u8(z.hcp, o.hcp);
    p.u8(z.hcp_per_hc, o.hcp_per_hc);

    if(v < 29)
        p.u8(z.max_bombs, o.max_bombs);

    p.u8(z.keys, o.keys);
    p.u16(z.rupies, o.rupies);
    p.u8(z.triforce, o.triforce);
    bytes_u8(p, z.map, o.map, 64);
    bytes_u8(p, z.compass, o.compass, 64);
    bytes_u8(p, z.boss_key, o.boss_key, 64);
    bytes_u8(p, z.misc, o.misc, 16);
    p.u8(z.last_map, o.last_map);
    p.u8(z.last_screen, o.last_screen);
    p.u16(z.max_magic, o.max_magic);
    p.u16(z.magic, o.magic);
    p.u8(z.bomb_ratio, o.bomb_ratio);
    p.u8(z.msg_more_x, o.msg_more_x);
    p.u8(z.msg_more_y, o.msg_more_y);
    p.u8(z.subscreen, o.subscreen);
    p.u16(z.start_dmap, o.start_dmap);
    p.u8(z.linkanimationstyle, o.linkanimationstyle);

    if(v < 29)
    {
        p.u8(z.arrows, o.arrows);
        p.u8(z.max_arrows, o.max_arrows);
    }

    bytes_u8(p, z.level_keys, o.level_keys, MAXLEVELS);

    if(!p.keep(z.ss_grid_x == o.ss_grid_x) && z.ss_grid_x < 1)
        p.fail("a subscreen grid width of 0");
    if(!p.keep(z.ss_grid_y == o.ss_grid_y) && z.ss_grid_y < 1)
        p.fail("a subscreen grid height of 0");

    p.u16(z.ss_grid_x, o.ss_grid_x);
    p.u16(z.ss_grid_y, o.ss_grid_y);
    p.u16(z.ss_grid_xofs, o.ss_grid_xofs);
    p.u16(z.ss_grid_yofs, o.ss_grid_yofs);
    p.u16(z.ss_grid_color, o.ss_grid_color);
    p.u16(z.ss_bbox_1_color, o.ss_bbox_1_color);
    p.u16(z.ss_bbox_2_color, o.ss_bbox_2_color);
    p.u16(z.ss_flags, o.ss_flags);
    p.u8(z.subscreen_style, o.subscreen_style);
    p.u8(z.usecustomsfx, o.usecustomsfx);
    p.u16(z.max_rupees, o.max_rupees);
    p.u16(z.max_keys, o.max_keys);

    if(v > 25)
        p.u8(z.gravity, z.gravity);     // replaced by the long below
    else
        p.u8(z.gravity, o.gravity);

    p.u16(z.terminalv, o.terminalv);
    p.u8(z.msg_speed, o.msg_speed);
    p.u8(z.transition_type, o.transition_type);
    p.u8(z.jump_link_layer_threshold, o.jump_link_layer_threshold);
    p.u8(z.msg_more_is_offset, o.msg_more_is_offset);

    if(v >= 19)
    {
        p.u16(z.bombs, o.bombs);
        p.u16(z.super_bombs, o.super_bombs);
        p.u16(z.max_bombs, o.max_bombs);
        p.copy(2);      // max super bombs
        p.u16(z.arrows, o.arrows);
        p.u16(z.max_arrows, o.max_arrows);
    }

    if(v >= 20)
        p.copy(2);
    if(v >= 21)
        p.copy(2);
    if(v > 21)
        p.copy(4);
    if(v > 22)
        p.copy(100);

    if(v > 23)
        for(int i = 0; i < 5; ++i)
            light_byte(p, i);
    else if(memcmp(zq255.light_init, g_snap.light, 5 * sizeof(int)))
        p.fail("lighting defaults");

    if(v > 24)
        light_byte(p, 5);
    else if(zq255.light_init[5] != g_snap.light[5])
        p.fail("the darkness colour");

    if(v > 25)
    {
        p.put32(z.gravity == o.gravity, z.gravity * 100L);     // in hundredths
        p.copy(4);      // swim gravity
    }

    if(v > 26)
        p.copy(6);
    if(v > 27)
        p.copy(4);
    if(v > 29)
        p.copy(4);

    if(v > 30)
        p.u8(zq255.init_switch_style, g_snap.switch_style, "a switchhook style");
    else if(zq255.init_switch_style != g_snap.switch_style)
        p.fail("a switchhook style");

    if(v > 31)
        double_magic_byte(p);

    if(v > 32)
    {
        size_t at = s.pos;
        long num = s.w();

        for(long q = 1; q < num && !s.bad; ++q)
        {
            long flags = s.c();
            if(!(flags & 2))
                continue;
            s.skip(2 + 2 + 8 * 4 + 4);
            long cnt = s.l();
            if(cnt < 0 || (size_t)cnt > s.left() / 4)
                s.bad = true;
            else
                s.skip((size_t)cnt * 4);
            s.skip(4);
        }

        size_t end = s.pos;
        s.pos = at;
        p.copy(end - at);
    }

    if(v > 33)
        p.copy(2);

    if(v > 34)
    {
        if(!store_screen_data_same())
            p.fail("changed screen script start data");

        size_t at = s.pos;
        long used = s.l();

        for(long q = 0; q < used && !s.bad; ++q)
        {
            long sz = s.l();
            if(sz)
            {
                long cnt = s.l();
                if(cnt < 0 || (size_t)cnt > s.left() / 4)
                    s.bad = true;
                else
                    s.skip((size_t)cnt * 4);
            }
        }

        size_t end = s.pos;
        s.pos = at;
        p.copy(end - at);
    }
    else if(!store_screen_data_same())
        p.fail("screen script start data");

    if(v > 35)
        p.copy(4);

    // this reader keeps no generic script start data, so nothing in ZQuest should have any
    if(!store_generic_same())
        p.fail("generic script start data");
}

int write_initdata(PACKFILE *f, const ZqPartContext &ctx)
{
    const int v = ctx.version;
    bool q255 = ctx.zelda_version >= 0x255;

    if(header_is_253(ctx) && v == V_INITDATA && ctx.cversion == CV_INITDATA)
        return ZQW_UNSUPPORTED;

    if(ctx.zelda_version <= 0x192)
        return ZQW_UNSUPPORTED;

    if(q255 ? (v < 18 || v > 52) : (v != 1 && v != 18))
        return ZQW_UNSUPPORTED;

    // Version 1 is only read this way with a header from 2.10 up to 2.11 build 14.
    if(!q255 && v == 1 && (ctx.zelda_version > 0x211 || (ctx.zelda_version == 0x211 && ctx.build >= 15)))
        return ZQW_UNSUPPORTED;

    // When the quest file has the old "continue with full hearts" rule, every loader (2.53's,
    // this ZQuest's, upstream's) turns it into init data as it opens the quest: continue at
    // 100%, start with all heart containers. The rules part keeps the rule, so changed hearts
    // or continue settings would be undone the next time the quest is opened. Refuse, as the
    // pre-1.93 writer does, so the user can save in a newer version instead of losing the edit.
    if(!q255)
    {
        const ZqQuestPart *rules = zq_find_part(ID_RULES);
        byte b = 0;

        if(rules && zq_part_read(*rules, 8 + (qr_CONTFULL_DEP >> 3), &b, 1) && ((b >> (qr_CONTFULL_DEP & 7)) & 1) &&
           !(get_bit(zinit.misc, idM_CONTPERCENT) && zinit.cont_heart == 100 && zinit.start_heart == zinit.hc))
        {
            set_why("the init data: continue hearts are set by a rule in this version");
            return CANT_HOLD;
        }
    }

    Src s;
    Head h;

    if(!start(ctx, s, h))
        return BAD_PART;

    Buf body;
    Patch p(s, body);

    if(q255)
    {
        if(v >= 37)
            init_new(p, v);
        else
            init_255_old(p, v);
    }
    else if(v == 18)
        init_v18(p);
    else
    {
        size_t at = body.size();
        init_v1(p, s);
        init_v1_check_items(p, at);
    }

    return finish(f, ctx, s, h, p, body);
}

ZqWriterRegistration g_reg_initdata(ID_INITDATA, write_initdata, "init data v1, v18, 2.55 v18-v52");
// Init data before v15 sets some items' values on load (swords, hookshot, fairies, potions), so
// an item edit alone would come back undone: this writer runs on every own-format save.
ZqEverySave g_every_save_initdata(ID_INITDATA);

} // namespace

// ---- SUBS -------------------------------------------------------------------------------
//
// Each subscreen is a record. Up to v7 the format is 2.53's own list of objects; from v8 it
// is 3.0's pages of widgets, which the reader turns into 2.53 objects (the first page only,
// and only the widgets 2.53 has an object for). There a changed object is written back into
// the widget it came from, field by field, and the widget is then converted again the way
// the reader does it to check it gives the same object: whatever doesn't survive that (a
// value the widget can't store, or one that would change the widget's layout) stops the save.

namespace
{

bool sub_has_text(int type)
{
    return type == ssoTEXT || type == ssoTEXTBOX || type == ssoCURRENTITEMTEXT || type == ssoCURRENTITEMCLASSTEXT;
}

struct SubObj
{
    subscreen_object o;     // dp1 cleared
    std::string text;
};

struct SubGroup
{
    byte ss_type;
    char name[64];
    std::vector<SubObj> objs;   // up to the first empty one, as the 2.53 writer writes them
};

SubGroup g_subs[MAXCUSTOMSUBSCREENS];

int sub_count(const subscreen_group &g)
{
    int n = 0;

    while(n < MAXSUBSCREENITEMS && g.objects[n].type != ssoNULL)
        ++n;

    return n;
}

std::string sub_text(const subscreen_object &o)
{
    return (sub_has_text(o.type) && o.dp1) ? std::string((const char *)o.dp1) : std::string();
}

// Everything the file holds of an object (not dp1, which only text objects use, nor
// frames, which the file doesn't have).
bool sub_obj_eq(const subscreen_object &a, const subscreen_object &b)
{
    return a.type == b.type && a.pos == b.pos && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h &&
           a.colortype1 == b.colortype1 && a.color1 == b.color1 && a.colortype2 == b.colortype2 && a.color2 == b.color2 &&
           a.colortype3 == b.colortype3 && a.color3 == b.color3 && a.d1 == b.d1 && a.d2 == b.d2 && a.d3 == b.d3 &&
           a.d4 == b.d4 && a.d5 == b.d5 && a.d6 == b.d6 && a.d7 == b.d7 && a.d8 == b.d8 && a.d9 == b.d9 && a.d10 == b.d10 &&
           a.speed == b.speed && a.delay == b.delay && a.frame == b.frame;
}

bool sub_obj_same(int i, int j)
{
    const SubGroup &s = g_subs[i];

    if(j >= (int)s.objs.size())
        return false;

    const subscreen_object &o = custom_subscreen[i].objects[j];
    return sub_obj_eq(o, s.objs[j].o) && sub_text(o) == s.objs[j].text;
}

bool sub_group_same(int i)
{
    const subscreen_group &g = custom_subscreen[i];
    const SubGroup &s = g_subs[i];

    if(g.ss_type != s.ss_type || memcmp(g.name, s.name, 64) || sub_count(g) != (int)s.objs.size())
        return false;

    for(int j = 0; j < (int)s.objs.size(); ++j)
        if(!sub_obj_same(i, j))
            return false;

    return true;
}

// ---- up to v7: 2.53's own layout (readsubscreens, q255's read_sub_group) ----

// How long an object is in the file, from its first byte.
bool walk_sub_object(Src &s, int v)
{
    int type = (int)s.c();
    s.skip(1 + 8 + 9 + 40);
    s.skip(v < 2 ? 12 : 4);

    if(sub_has_text(type))
    {
        long n = s.w();
        if(n)
            s.skip((size_t)n + 1);
    }
    else
        s.skip(1);

    return !s.bad;
}

void encode_sub_object(Buf &o, const subscreen_object &t, int orig_dp1)
{
    o.c(t.type);
    o.c(t.pos);
    o.w(t.x);
    o.w(t.y);
    o.w(t.w);
    o.w(t.h);
    o.c(t.colortype1);
    o.w(t.color1);
    o.c(t.colortype2);
    o.w(t.color2);
    o.c(t.colortype3);
    o.w(t.color3);
    o.l(t.d1);
    o.l(t.d2);
    o.l(t.d3);
    o.l(t.d4);
    o.l(t.d5);
    o.l(t.d6);
    o.l(t.d7);
    o.l(t.d8);
    o.l(t.d9);
    o.l(t.d10);
    o.c(t.speed);
    o.c(t.delay);
    o.w(t.frame);

    if(sub_has_text(t.type))
    {
        std::string text = sub_text(t);
        o.w((long)text.size());
        if(!text.empty())
            o.put(text.c_str(), text.size() + 1);
    }
    else
        o.c(orig_dp1 >= 0 ? orig_dp1 : 0);     // a placeholder nothing reads
}

void subs_old(Patch &p, int v, bool q255)
{
    Src &s = p.s;

    for(int i = 0; i < MAXCUSTOMSUBSCREENS && !s.bad; ++i)
    {
        const subscreen_group &g = custom_subscreen[i];
        const SubGroup &old = g_subs[i];
        size_t at = s.pos;

        // where each object starts, and the record's end
        s.skip(64);
        if(v > 1)
            s.skip(1);
        long n = v < 4 ? s.c() : s.w();
        size_t objs_at = s.pos;
        std::vector<size_t> starts;

        for(long j = 0; j < n && j < MAXSUBSCREENITEMS && !s.bad; ++j)
        {
            starts.push_back(s.pos);
            walk_sub_object(s, v);
        }

        size_t end = s.pos;
        s.pos = at;

        if(s.bad)
            return;

        if(p.keep(sub_group_same(i)))
        {
            p.copy(end - at);
            continue;
        }

        int count = sub_count(g);
        p.bytes(g.name, old.name, 64);

        if(v > 1)
            p.u8(g.ss_type, old.ss_type);

        if(v < 4)
            p.u8(count, n, "more than 255 subscreen objects");
        else
            p.u16(count, n);

        s.pos = objs_at;

        for(int j = 0; j < count; ++j)
        {
            const subscreen_object &t = g.objects[j];

            if(q255 && v >= 7 && t.type == ssoMAGICGAUGE && t.d9 == -1)
                p.fail("a magic gauge showing at -1 (this version reads that as 0)");

            if(j < (int)starts.size())
            {
                size_t obj_end = j + 1 < (int)starts.size() ? starts[j + 1] : end;

                if(p.keep(sub_obj_same(i, j)))
                {
                    s.pos = starts[j];
                    p.copy(obj_end - starts[j]);
                    continue;
                }

                int orig_type = s.d[starts[j]];
                int dp1 = sub_has_text(orig_type) ? -1 : s.d[obj_end - 1];
                encode_sub_object(p.o, t, dp1);
                s.pos = obj_end;
            }
            else
                encode_sub_object(p.o, t, -1);
        }

        s.pos = end;
    }
}

// ---- v8 and up: 3.0's widgets (q255's read_new_subscreens) ----

enum
{
    WIDG_NULL, WIDG_FRAME, WIDG_TEXT, WIDG_LINE, WIDG_RECT,
    WIDG_TIME, WIDG_MMETER, WIDG_LMETER, WIDG_BTNITM, WIDG_COUNTER,
    WIDG_OLDCTR, WIDG_MMAPTITLE, WIDG_MMAP, WIDG_LMAP, WIDG_BGCOLOR,
    WIDG_ITEMSLOT, WIDG_MCGUFF_FRAME, WIDG_MCGUFF, WIDG_TILEBLOCK, WIDG_MINITILE,
    WIDG_SELECTOR, WIDG_LGAUGE, WIDG_MGAUGE, WIDG_TEXTBOX, WIDG_SELECTEDTEXT,
    WIDG_MISCGAUGE, WIDG_BTNCOUNTER, WIDG_COUNTERPERCBAR, WIDG_ITMCOOLDOWNGAUGE, WIDG_ITMCOOLDOWNTEXT,
    WIDG_MAX
};

// The fields of a widget a 2.53 object's values come from.
enum
{
    WF_POSFLAGS, WF_X, WF_Y, WF_W, WF_H, WF_FLAGS, WF_SEL0, WF_SEL1, WF_SEL2, WF_SEL3, WF_SEL4,
    WF_TILE, WF_TILE2, WF_CSET1, WF_CSET2, WF_COL0, WF_COL1, WF_COL2, WF_FONT, WF_ALIGN, WF_SHADOW,
    WF_TEXT, WF_BYTE, WF_BYTE2, WF_CTR0, WF_CTR1, WF_CTR2, WF_ITEM, WF_CLASS, WF_NUMBER, WF_SPECIAL,
    WF_CRN, WF_FLIP, WF_GTILE0, WF_GTILE1, WF_GTILE2, WF_GTILE3, WF_GCSET0, WF_GCSET1, WF_GCSET2, WF_GCSET3,
    WF_FRAMES, WF_SPEED, WF_DELAY, WF_CONTAINER, WF_SHOW, WF_COUNT
};

struct WField
{
    int id;
    size_t off, len;
};

struct Widget
{
    byte ty;
    subscreen_object t;
    std::string text;
    bool converted;
    std::vector<WField> fields;
    // what the conversion read, for the inverse
    long genflags, flags;
};

struct WalkW
{
    Src &s;
    Widget &w;
    int v;

    WalkW(Src &src, Widget &wd, int version) : s(src), w(wd), v(version) {}

    void mark(int id, size_t len)
    {
        WField f;
        f.id = id;
        f.off = s.pos;
        f.len = len;
        w.fields.push_back(f);
    }

    long c(int id) { mark(id, 1); return s.c(); }
    long ws(int id) { mark(id, 2); return s.ws(); }
    long wu(int id) { mark(id, 2); return s.w(); }
    long l(int id) { mark(id, 4); return s.l(); }
    void col(int id, byte &type, short &color) { mark(id, 3); type = (byte)s.c(); color = (short)s.ws(); }
};

void walk_n(Src &s, long count, size_t each)
{
    if(count < 0 || (each && (size_t)count > s.left() / each))
        s.bad = true;
    else
        s.skip((size_t)count * each);
}

void skip_transition(Src &s, int v)
{
    s.skip(1);
    s.skip(v < 17 ? 1 : 2);
    s.skip(2);
    long args = s.c();
    walk_n(s, args, 4);
}

void skip_selector(Src &s)
{
    s.skip(8);
    long n = s.c();
    walk_n(s, n, 12);
}

void skip_config(Src &s)
{
    s.skip(2);
    walk_script_args(s);
}

int font_of(long font)
{
    switch(font)
    {
    case 0: return ssfZELDA;
    case 1: return ssfZ3;
    case 2: return ssfZ3SMALL;
    case 8: return ssfZTIME;
    case 9: return ssfSMALL;
    case 11: return ssfSMALLPROP;
    case 12: return ssfSS1;
    case 13: return ssfSS2;
    case 14: return ssfSS3;
    case 15: return ssfSS4;
    case 16: return ssfGBLA;
    case 17: return ssfGORON;
    case 18: return ssfZORAN;
    case 19: return ssfHYLIAN1;
    case 20: return ssfHYLIAN2;
    case 21: return ssfHYLIAN3;
    case 22: return ssfHYLIAN4;
    case 23: return ssfGBORACLE;
    case 24: return ssfGBORACLEP;
    case 25: return ssfDSPHANTOM;
    case 26: return ssfDSPHANTOMP;
    case 71: return ssfPROP;
    }

    return ssfZELDA;
}

long font_for(int ssf)
{
    static const long ids[] = { 0, 1, 2, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 71 };

    for(size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        if(font_of(ids[i]) == ssf)
            return ids[i];

    return -1000;
}

int counter_of(long ctr)
{
    switch(ctr)
    {
    case 1: return sscRUPEES;
    case 2: return sscBOMBS;
    case 3: return sscARROWS;
    case 5: return sscGENKEYNOMAGIC;
    case 6: return sscSBOMBS;
    case -9: return sscGENKEYMAGIC;
    case -8: return sscGENKEYNOMAGIC;
    case -7: return sscLEVKEYMAGIC;
    case -6: return sscLEVKEYNOMAGIC;
    case -5: return sscANYKEYMAGIC;
    case -4: return sscANYKEYNOMAGIC;
    }

    if(ctr >= 7 && ctr <= 31)
        return sscSCRIPT1 + (int)(ctr - 7);

    return -1;
}

long counter_for(int ssc)
{
    if(ssc < 0)
        return -1;

    static const long ids[] = { 1, 2, 3, 5, 6, -9, -7, -6, -5, -4 };

    for(size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        if(counter_of(ids[i]) == ssc)
            return ids[i];

    if(ssc >= sscSCRIPT1 && ssc < sscSCRIPT1 + 25)
        return 7 + (ssc - sscSCRIPT1);

    return -1000;   // no counter of the new format shows it
}

int tile_of(long tile)
{
    return (tile < 0 || tile >= NEWMAXTILES) ? 0 : (int)tile;
}

int minitile_of(long tilecrn)
{
    if(tilecrn < 0)
        return 0;

    return (tile_of(tilecrn >> 2) << 2) | (int)(tilecrn & 3);
}

int item_of(long item)
{
    return (item < 0 || item >= MAXITEMS) ? 0 : (int)item;
}

void color_of(byte &type, short &color, byte ct, short c)
{
    type = ct;
    color = c;

    if(ct != ssctMISC)
        return;

    if(c == ssctMSGTEXT)
        color = ssctTEXT;
    else if(c < 0 || c > ssctMSGTEXT)
    {
        type = ssctSYSTEM;
        color = -1;
    }
}

void text_style(WalkW &k, bool align = true)
{
    k.w.t.d1 = font_of(k.l(WF_FONT));
    k.w.t.d2 = align ? (int)k.c(WF_ALIGN) : 0;
    k.w.t.d3 = (int)k.c(WF_SHADOW);
}

void colors3(WalkW &k)
{
    byte ct[3];
    short c[3];
    k.col(WF_COL0, ct[0], c[0]);
    k.col(WF_COL1, ct[1], c[1]);
    k.col(WF_COL2, ct[2], c[2]);
    color_of(k.w.t.colortype1, k.w.t.color1, ct[0], c[0]);
    color_of(k.w.t.colortype2, k.w.t.color2, ct[1], c[1]);
    color_of(k.w.t.colortype3, k.w.t.color3, ct[2], c[2]);
}

std::string wstr(WalkW &k)
{
    long n = k.s.w();
    std::string out;

    if(!k.s.has((size_t)n))
    {
        k.s.bad = true;
        return out;
    }

    out.assign((const char *)k.s.at(), (size_t)n);
    k.s.skip((size_t)n);
    return out;
}

// The text as store_sub_object keeps it: up to its first NUL.
std::string text_field(WalkW &k)
{
    WField f;
    f.id = WF_TEXT;
    f.off = k.s.pos;
    std::string t = wstr(k);
    f.len = k.s.pos - f.off;
    k.w.fields.push_back(f);
    return std::string(t.c_str());
}

void gauge(WalkW &k, long flags)
{
    Src &s = k.s;
    subscreen_object &t = k.w.t;
    long tiles[4], csets[4];

    for(int i = 0; i < 4; ++i)
    {
        tiles[i] = k.l(WF_GTILE0 + i);
        csets[i] = k.c(WF_GCSET0 + i);
    }

    long frames = k.wu(WF_FRAMES);
    long speed = 0, delay = 0;
    bool still = frames <= 1;
    bool no_anim = still || !(flags & 0xC0);
    bool no_speed = still || (!no_anim && frames <= 2 && (flags & 0x200));
    bool no_inf = !(flags & 0xC00);

    if(!no_speed)
    {
        speed = k.wu(WF_SPEED);
        delay = k.wu(WF_DELAY);
    }

    long container = k.wu(WF_CONTAINER);
    s.skip(1);
    long wid = s.c();
    long hei = s.c();

    if(wid || hei)
        s.skip(6);
    if(!no_anim)
        s.skip(2);
    if(!no_inf)
        s.skip(k.v < 18 ? 2 : 4);

    t.d1 = (int)container;
    t.d2 = minitile_of(tiles[0]);
    t.d3 = minitile_of(tiles[1]);
    t.d4 = minitile_of(tiles[2]);
    t.d5 = minitile_of(tiles[3]);
    t.colortype1 = (byte)csets[0];
    t.color1 = (short)csets[1];
    t.colortype2 = (byte)csets[2];
    t.color2 = (short)csets[3];
    t.d6 = (int)frames;
    t.d7 = (int)speed;
    t.d8 = (int)delay;
    t.d10 = (int)(flags & 0x1F);
}

// read_widget_base and read_widget, from after the type byte: the object the reader makes,
// and where each field it takes is.
void walk_widget(Src &s, int v, byte ty, Widget &w)
{
    WalkW k(s, w, v);
    w.ty = ty;
    w.fields.clear();
    w.text.clear();
    memset(&w.t, 0, sizeof(w.t));
    w.converted = false;

    if(ty >= WIDG_MAX)
    {
        s.bad = true;
        return;
    }

    s.skip(1);
    long posflags = k.c(WF_POSFLAGS);
    long x = k.ws(WF_X), y = k.ws(WF_Y);
    long ww = k.wu(WF_W), hh = k.wu(WF_H);
    long genflags = s.l();
    long flags = k.l(WF_FLAGS);
    long pos[5] = { 0, 0, 0, 0, 0 };
    bool disabled = false;
    w.genflags = genflags;
    w.flags = flags;

    if(v >= 9)
        wstr(k);    // label
    if(v >= 11)
        s.skip(1);

    if(genflags & 1)
    {
        for(int i = 0; i < 5; ++i)
            pos[i] = k.l(WF_SEL0 + i);

        wstr(k);

        if(genflags & 4)
            skip_selector(s);

        if(v >= 21)
        {
            s.skip(1);
            skip_config(s);
        }
        else
        {
            long script = s.w();
            if(script)
                s.skip(1 + 32);
        }

        long pg_mode = s.c();
        if(pg_mode)
        {
            s.skip(2);
            skip_transition(s, v);
        }
    }

    if(v >= 14)
    {
        long count = s.w();
        walk_n(s, count, v < 18 ? 1 : 2);
        count = s.w();
        walk_n(s, count, v < 18 ? 1 : 2);
        s.skip(2 + 2 + 1);
        s.skip(v >= 15 ? 2 : 1);
        s.skip(2);
        disabled = s.c() != 0;

        if(v >= 16)
        {
            long n = s.c();
            walk_n(s, n, 1);
            n = s.w();
            walk_n(s, n, 2);
            n = s.w();
            walk_n(s, n, 2);
            n = s.w();
            walk_n(s, n, 2);
            n = s.c();
            walk_n(s, n, 1);
            s.skip(2 + 2 + 4 + 4 + 4 + 2);
        }
    }

    subscreen_object &t = w.t;
    t.pos = (byte)(posflags & (sspUP | sspDOWN | sspSCROLLING));
    t.x = (word)x;
    t.y = (word)y;
    t.w = (word)ww;
    t.h = (word)hh;

    const int f1 = (flags & 1) ? 1 : 0;
    const int f2 = (flags & 2) ? 1 : 0;
    const int f4 = (flags & 4) ? 1 : 0;
    byte ct[3];
    short c[3];
    bool convert = true;

    switch(ty)
    {
    case WIDG_NULL:
        convert = false;
        break;

    case WIDG_FRAME:
        t.type = sso2X2FRAME;
        t.d1 = tile_of(k.l(WF_TILE));
        k.col(WF_COL0, t.colortype1, t.color1);
        t.d3 = f2;
        t.d4 = f1;
        break;

    case WIDG_TEXT:
        t.type = ssoTEXT;
        text_style(k);
        w.text = text_field(k);
        colors3(k);
        break;

    case WIDG_LINE:
        t.type = ssoLINE;
        k.col(WF_COL0, ct[0], c[0]);
        color_of(t.colortype1, t.color1, ct[0], c[0]);
        t.d4 = f1;
        break;

    case WIDG_RECT:
        t.type = ssoRECT;
        k.col(WF_COL0, ct[0], c[0]);
        k.col(WF_COL1, ct[1], c[1]);
        color_of(t.colortype1, t.color1, ct[1], c[1]);
        color_of(t.colortype2, t.color2, ct[0], c[0]);
        t.d1 = f2;
        t.d2 = f1;
        break;

    case WIDG_TIME:
        t.type = f1 ? ssoBSTIME : ssoSSTIME;
        text_style(k);
        colors3(k);
        break;

    case WIDG_MMETER:
        t.type = ssoMAGICMETER;
        break;

    case WIDG_LMETER:
    {
        long rows = k.c(WF_BYTE);
        t.type = ssoLIFEMETER;
        t.d2 = f1;
        t.d3 = rows >= 3 ? 1 : 0;
        break;
    }

    case WIDG_BTNITM:
    {
        long btn = k.c(WF_BYTE);
        t.type = ssoBUTTONITEM;
        t.d1 = (int)btn;
        t.d2 = f1;
        convert = btn <= 3;
        break;
    }

    case WIDG_COUNTER:
    {
        t.type = ssoCOUNTER;
        text_style(k);
        colors3(k);
        long ctrs[3];
        ctrs[0] = k.l(WF_CTR0);
        ctrs[1] = k.l(WF_CTR1);
        ctrs[2] = k.l(WF_CTR2);
        t.d4 = (int)k.c(WF_BYTE);
        s.skip(1);
        t.d10 = item_of(k.l(WF_ITEM));
        t.d5 = (int)k.c(WF_BYTE2);
        t.d6 = f1 | (f2 << 1);

        if(v >= 12)
            s.skip(9);

        int shown[3] = { -1, -1, -1 };
        int n = 0;

        for(int i = 0; i < 3; ++i)
        {
            int ssc = counter_of(ctrs[i]);
            if(ssc >= 0)
                shown[n++] = ssc;
        }

        convert = n > 0;
        t.d7 = shown[0];
        t.d8 = shown[1];
        t.d9 = shown[2];
        break;
    }

    case WIDG_OLDCTR:
        t.type = ssoCOUNTERS;
        text_style(k, false);
        colors3(k);
        t.d4 = (int)k.c(WF_BYTE);
        t.d10 = item_of(k.l(WF_ITEM));
        t.d5 = (int)k.c(WF_BYTE2);
        t.d2 = f1;
        break;

    case WIDG_BTNCOUNTER:
        text_style(k);
        colors3(k);
        s.skip(5);
        if(v >= 12)
            s.skip(9);
        convert = false;
        break;

    case WIDG_MMAPTITLE:
        t.type = ssoMINIMAPTITLE;
        text_style(k);
        colors3(k);
        t.d4 = f1;
        if(v >= 10)
            s.skip(1);
        break;

    case WIDG_MMAP:
        t.type = ssoMINIMAP;
        if(v >= 13)
            s.skip(v >= 15 ? 2 : 1);
        colors3(k);
        if(v >= 19)
            s.skip(3);
        if(v >= 20)
            s.skip(6);
        t.d1 = f1;
        t.d2 = f2;
        t.d3 = f4;
        break;

    case WIDG_LMAP:
        t.type = ssoLARGEMAP;
        k.col(WF_COL0, ct[0], c[0]);
        k.col(WF_COL1, ct[1], c[1]);
        color_of(t.colortype1, t.color1, ct[0], c[0]);
        color_of(t.colortype2, t.color2, ct[1], c[1]);
        t.d1 = f1;
        t.d2 = f2;
        t.d3 = f4;
        t.d10 = (flags & 8) ? 1 : 0;
        break;

    case WIDG_BGCOLOR:
        t.type = ssoCLEAR;
        k.col(WF_COL0, ct[0], c[0]);
        color_of(t.colortype1, t.color1, ct[0], c[0]);
        break;

    case WIDG_ITEMSLOT:
    {
        long iclass = k.l(WF_CLASS);
        long iid = k.l(WF_ITEM);
        bool selectable = (genflags & 1) != 0;
        t.type = ssoCURRENTITEM;
        t.d1 = (iclass < 0 || iclass >= itype_max) ? (int)itype_misc : (int)iclass;
        t.d2 = f1 ? 0 : 1;
        t.d3 = selectable ? (int)pos[0] : -1;
        t.d4 = selectable ? (int)pos[1] : -1;
        t.d5 = selectable ? (int)pos[2] : -1;
        t.d6 = selectable ? (int)pos[3] : -1;
        t.d7 = selectable ? (int)pos[4] : -1;
        t.d8 = (iid >= 0 && iid < MAXITEMS) ? (int)iid + 1 : 0;
        t.d10 = (iclass >= itype_max) ? (int)iclass + 1 : 0;
        break;
    }

    case WIDG_MCGUFF_FRAME:
    {
        long frame_tile = k.l(WF_TILE);
        long piece_tile = k.l(WF_TILE2);
        long frame_cset = k.c(WF_CSET1);
        long piece_cset = k.c(WF_CSET2);
        k.col(WF_COL0, ct[0], c[0]);
        k.col(WF_COL1, ct[1], c[1]);
        t.type = ssoTRIFRAME;
        t.d1 = tile_of(frame_tile);
        t.d2 = (int)frame_cset;
        t.d3 = tile_of(piece_tile);
        t.d4 = (int)piece_cset;
        t.d5 = f1;
        t.d6 = f2;
        t.d7 = f4;
        color_of(t.colortype1, t.color1, ct[0], c[0]);
        color_of(t.colortype2, t.color2, ct[1], c[1]);
        break;
    }

    case WIDG_MCGUFF:
    {
        long tile = k.l(WF_TILE);
        long number = k.l(WF_NUMBER);
        long flip = k.c(WF_FLIP);
        k.col(WF_COL0, t.colortype1, t.color1);
        t.type = ssoTRIFORCE;
        t.d1 = tile_of(tile);
        t.d2 = (int)flip;
        t.d3 = f1;
        t.d4 = f2;
        t.d5 = (int)number;
        break;
    }

    case WIDG_TILEBLOCK:
    {
        long tile = k.l(WF_TILE);
        long flip = k.c(WF_FLIP);
        k.col(WF_COL0, t.colortype1, t.color1);
        t.type = ssoTILEBLOCK;
        t.d1 = tile_of(tile);
        t.d2 = (int)flip;
        t.d3 = f1;
        t.d4 = f2;
        break;
    }

    case WIDG_MINITILE:
    {
        long tile = k.l(WF_TILE);
        long special = k.l(WF_SPECIAL);
        long crn = k.c(WF_CRN);
        long flip = k.c(WF_FLIP);
        k.col(WF_COL0, t.colortype1, t.color1);
        t.type = ssoMINITILE;

        if(tile < 0)
        {
            t.d1 = -1;
            t.d2 = (int)special;
            t.d3 = (int)crn;
        }
        else
            t.d1 = (tile_of(tile) << 2) | (int)(crn & 3);

        t.d4 = (int)flip;
        t.d5 = f1;
        t.d6 = f2;
        break;
    }

    case WIDG_SELECTOR:
        t.type = f4 ? ssoSELECTOR2 : ssoSELECTOR1;
        t.d4 = f1;
        t.d5 = f2;
        break;

    case WIDG_LGAUGE:
        t.type = ssoLIFEGAUGE;
        gauge(k, flags);
        break;

    case WIDG_MGAUGE:
    {
        t.type = ssoMAGICGAUGE;
        gauge(k, flags);
        long show = k.ws(WF_SHOW);
        t.d9 = show == -1 ? 0 : (int)show;
        break;
    }

    case WIDG_MISCGAUGE:
        gauge(k, flags);
        s.skip(4);
        convert = false;
        break;

    case WIDG_ITMCOOLDOWNGAUGE:
        gauge(k, flags);
        s.skip(17);
        convert = false;
        break;

    case WIDG_ITMCOOLDOWNTEXT:
        text_style(k);
        colors3(k);
        s.skip(9);
        convert = false;
        break;

    case WIDG_TEXTBOX:
        t.type = ssoTEXTBOX;
        text_style(k);
        t.d5 = (int)k.c(WF_BYTE);
        w.text = text_field(k);
        colors3(k);
        t.d4 = f1;
        break;

    case WIDG_SELECTEDTEXT:
        t.type = ssoSELECTEDITEMNAME;
        text_style(k);
        t.d5 = (int)k.c(WF_BYTE);
        colors3(k);
        t.d4 = f1;
        break;

    case WIDG_COUNTERPERCBAR:
        s.skip(2);
        k.col(WF_COL0, ct[0], c[0]);
        k.col(WF_COL1, ct[1], c[1]);
        convert = false;
        break;
    }

    w.converted = convert && !disabled && !s.bad;
}

long get_le(const byte *p, size_t n)
{
    long v = 0;

    for(size_t i = 0; i < n; ++i)
        v |= (long)p[i] << (8 * i);

    if(n == 2)
        v = (short)v;
    else if(n == 4)
        v = (long)(int)v;

    return v;
}

void put_le(Buf &o, long v, size_t n)
{
    for(size_t i = 0; i < n; ++i)
        o.c(v >> (8 * i));
}

// A colour field back from an object's colour, keeping the original where it gives the same.
void put_color(Buf &o, const byte *orig, byte ct, short c, bool converted)
{
    byte t0 = orig[0];
    short c0 = (short)get_le(orig + 1, 2);
    byte t1 = t0;
    short c1 = c0;

    if(converted)
        color_of(t1, c1, t0, c0);

    if(t1 == ct && c1 == c)
        o.put(orig, 3);
    else
    {
        o.c(ct);
        o.w(c);
    }
}

// Which object colour each colour field of a widget type gives, and whether it goes through
// the reader's colour conversion.
bool color_target(byte ty, int which, const subscreen_object &t, byte &ct, short &c, bool &converted)
{
    converted = true;

    switch(ty)
    {
    case WIDG_FRAME: case WIDG_MCGUFF: case WIDG_TILEBLOCK: case WIDG_MINITILE:
        if(which != 0)
            return false;
        converted = false;
        ct = t.colortype1;
        c = t.color1;
        return true;

    case WIDG_RECT:
        if(which == 0) { ct = t.colortype2; c = t.color2; return true; }
        if(which == 1) { ct = t.colortype1; c = t.color1; return true; }
        return false;

    case WIDG_LINE: case WIDG_BGCOLOR: case WIDG_LMAP: case WIDG_MCGUFF_FRAME:
    case WIDG_TEXT: case WIDG_TIME: case WIDG_COUNTER: case WIDG_OLDCTR: case WIDG_MMAPTITLE:
    case WIDG_MMAP: case WIDG_TEXTBOX: case WIDG_SELECTEDTEXT:
        if(which == 0) { ct = t.colortype1; c = t.color1; return true; }
        if(which == 1) { ct = t.colortype2; c = t.color2; return true; }
        if(which == 2) { ct = t.colortype3; c = t.color3; return true; }
        return false;
    }

    return false;
}

long flags_for(byte ty, const subscreen_object &t, long orig)
{
    long f = orig;
    long bits = 0, mask = 0;

#define FB(bit, on) do { mask |= (bit); if(on) bits |= (bit); } while(0)

    switch(ty)
    {
    case WIDG_FRAME: FB(1, t.d4); FB(2, t.d3); break;
    case WIDG_LINE: FB(1, t.d4); break;
    case WIDG_RECT: FB(1, t.d2); FB(2, t.d1); break;
    case WIDG_TIME: FB(1, t.type == ssoBSTIME); break;
    case WIDG_LMETER: case WIDG_BTNITM: FB(1, t.d2); break;
    case WIDG_COUNTER: FB(1, t.d6 & 1); FB(2, t.d6 & 2); break;
    case WIDG_OLDCTR: FB(1, t.d2); break;
    case WIDG_MMAPTITLE: case WIDG_TEXTBOX: case WIDG_SELECTEDTEXT: FB(1, t.d4); break;
    case WIDG_MMAP: case WIDG_LMAP: FB(1, t.d1); FB(2, t.d2); FB(4, t.d3); if(ty == WIDG_LMAP) FB(8, t.d10); break;
    case WIDG_ITEMSLOT: FB(1, t.d2 == 0); break;
    case WIDG_MCGUFF_FRAME: FB(1, t.d5); FB(2, t.d6); FB(4, t.d7); break;
    case WIDG_MCGUFF: case WIDG_TILEBLOCK: FB(1, t.d3); FB(2, t.d4); break;
    case WIDG_MINITILE: FB(1, t.d5); FB(2, t.d6); break;
    case WIDG_SELECTOR: FB(1, t.d4); FB(2, t.d5); FB(4, t.type == ssoSELECTOR2); break;
    case WIDG_LGAUGE: case WIDG_MGAUGE: mask = 0x1F; bits = t.d10 & 0x1F; break;
    }

#undef FB

    return (f & ~mask) | bits;
}

// One field of a widget back from the object t (and text), into o.
void put_widget_field(Buf &o, const Widget &w, const WField &f, const byte *orig, const subscreen_object &t, const std::string &text, int v)
{
    long ov = f.len <= 4 ? get_le(orig, f.len) : 0;
    long nv = ov;
    byte ty = w.ty;

    switch(f.id)
    {
    case WF_POSFLAGS: nv = (ov & ~7) | (t.pos & 7); break;
    case WF_X: nv = (short)t.x; break;
    case WF_Y: nv = (short)t.y; break;
    case WF_W: nv = t.w; break;
    case WF_H: nv = t.h; break;
    case WF_FLAGS: nv = flags_for(ty, t, ov); break;
    case WF_SEL0: case WF_SEL1: case WF_SEL2: case WF_SEL3: case WF_SEL4:
        if(ty == WIDG_ITEMSLOT && (w.genflags & 1))
        {
            const int d[5] = { t.d3, t.d4, t.d5, t.d6, t.d7 };
            nv = d[f.id - WF_SEL0];
        }
        break;

    case WF_TILE:
        if(ty == WIDG_MINITILE)
        {
            if(t.d1 == -1)
                nv = ov < 0 ? ov : -1;
            else
                nv = (ov >= 0 && tile_of(ov) == (t.d1 >> 2)) ? ov : (t.d1 >> 2);
        }
        else
            nv = tile_of(ov) == t.d1 ? ov : t.d1;
        break;

    case WF_TILE2: nv = tile_of(ov) == t.d3 ? ov : t.d3; break;
    case WF_CSET1: nv = t.d2; break;
    case WF_CSET2: nv = t.d4; break;

    case WF_COL0: case WF_COL1: case WF_COL2:
    {
        byte ct;
        short c;
        bool converted;

        if(color_target(ty, f.id - WF_COL0, t, ct, c, converted))
            put_color(o, orig, ct, c, converted);
        else
            o.put(orig, f.len);
        return;
    }

    case WF_FONT: nv = font_of(ov) == t.d1 ? ov : font_for(t.d1); break;
    case WF_ALIGN: nv = t.d2; break;
    case WF_SHADOW: nv = t.d3; break;

    case WF_TEXT:
    {
        // the original's text, as the reader keeps it
        long n = get_le(orig, 2) & 0xFFFF;
        std::string was((const char *)orig + 2, (size_t)n);

        if(std::string(was.c_str()) == text)
            o.put(orig, f.len);
        else
        {
            o.w((long)text.size());
            o.put(text.data(), text.size());
        }
        return;
    }

    case WF_BYTE:
        switch(ty)
        {
        case WIDG_LMETER: nv = ((ov >= 3) == (t.d3 != 0)) ? ov : (t.d3 ? 3 : 2); break;
        case WIDG_BTNITM: nv = t.d1; break;
        case WIDG_COUNTER: case WIDG_OLDCTR: nv = t.d4; break;
        case WIDG_TEXTBOX: case WIDG_SELECTEDTEXT: nv = t.d5; break;
        }
        break;

    case WF_BYTE2: nv = t.d5; break;

    case WF_CTR0: case WF_CTR1: case WF_CTR2:
    {
        // all three together: the reader shows the convertible ones, in order
        nv = ov;
        long ctrs[3];

        for(int i = 0; i < 3; ++i)
        {
            const WField *g = NULL;

            for(size_t q = 0; q < w.fields.size(); ++q)
                if(w.fields[q].id == WF_CTR0 + i)
                    g = &w.fields[q];

            ctrs[i] = g ? get_le(orig - f.off + g->off, 4) : -1;
        }

        int shown[3] = { -1, -1, -1 }, n = 0;

        for(int i = 0; i < 3; ++i)
        {
            int ssc = counter_of(ctrs[i]);
            if(ssc >= 0)
                shown[n++] = ssc;
        }

        if(shown[0] != t.d7 || shown[1] != t.d8 || shown[2] != t.d9)
        {
            const int d[3] = { t.d7, t.d8, t.d9 };
            nv = counter_for(d[f.id - WF_CTR0]);
        }
        break;
    }

    case WF_ITEM:
        if(ty == WIDG_ITEMSLOT)
        {
            long had = (ov >= 0 && ov < MAXITEMS) ? ov + 1 : 0;
            nv = had == t.d8 ? ov : (t.d8 ? t.d8 - 1 : -1);
        }
        else
            nv = item_of(ov) == t.d10 ? ov : t.d10;
        break;

    case WF_CLASS:
    {
        long d1 = (ov < 0 || ov >= itype_max) ? itype_misc : ov;
        long d10 = ov >= itype_max ? ov + 1 : 0;
        nv = (d1 == t.d1 && d10 == t.d10) ? ov : (t.d10 ? t.d10 - 1 : t.d1);
        break;
    }

    case WF_NUMBER: nv = t.d5; break;

    case WF_SPECIAL:
        if(t.d1 == -1)
            nv = t.d2;
        break;

    case WF_CRN:
        nv = t.d1 == -1 ? t.d3 : ((ov & ~3) | (t.d1 & 3));
        break;

    case WF_FLIP: nv = ty == WIDG_MINITILE ? t.d4 : t.d2; break;

    case WF_GTILE0: case WF_GTILE1: case WF_GTILE2: case WF_GTILE3:
    {
        const int d[4] = { t.d2, t.d3, t.d4, t.d5 };
        int want = d[f.id - WF_GTILE0];
        nv = minitile_of(ov) == want ? ov : want;
        break;
    }

    case WF_GCSET0: nv = t.colortype1; break;
    case WF_GCSET1: nv = t.color1; break;
    case WF_GCSET2: nv = t.colortype2; break;
    case WF_GCSET3: nv = t.color2; break;
    case WF_FRAMES: nv = t.d6; break;
    case WF_SPEED: nv = t.d7; break;
    case WF_DELAY: nv = t.d8; break;
    case WF_CONTAINER: nv = t.d1; break;
    case WF_SHOW: nv = ((ov == -1 ? 0 : ov) == t.d9) ? ov : t.d9; break;
    }

    (void)v;

    if(nv == ov)
        o.put(orig, f.len);
    else
        put_le(o, nv, f.len);
}

// A widget written back from object t. False when the result doesn't convert back to it.
bool rewrite_widget(const Src &s, int v, const Widget &w, size_t start, size_t end, const subscreen_object &t,
                    const std::string &text, Buf &out)
{
    Buf b;
    size_t at = start;
    std::vector<WField> fields = w.fields;

    // fields in file order
    for(size_t i = 1; i < fields.size(); ++i)
        for(size_t j = i; j > 0 && fields[j].off < fields[j - 1].off; --j)
        {
            WField x = fields[j];
            fields[j] = fields[j - 1];
            fields[j - 1] = x;
        }

    for(size_t i = 0; i < fields.size(); ++i)
    {
        const WField &f = fields[i];
        b.put(&s.d[at], f.off - at);
        put_widget_field(b, w, f, &s.d[f.off], t, text, v);
        at = f.off + f.len;
    }

    b.put(&s.d[at], end - at);

    // what the reader makes of it
    Src chk;
    chk.d = b.b;
    Widget again;
    walk_widget(chk, v, w.ty, again);

    if(chk.bad || chk.pos != chk.d.size() || !again.converted || !sub_obj_eq(again.t, t) || (sub_has_text(t.type) && again.text != text))
        return false;

    out.put(b);
    return true;
}

void copy_to(Patch &p, size_t end)
{
    if(end >= p.s.pos)
        p.copy(end - p.s.pos);
    else
        p.s.bad = true;
}

std::string cstr_at(const Src &s, size_t at)
{
    size_t n = s.d[at];
    return std::string((const char *)&s.d[at + 1], n);
}

void subs_new(Patch &p, int v)
{
    Src &s = p.s;
    int lists = v >= 16 ? 4 : 3;
    int next_slot = 0;
    int kept[2] = { 0, 0 };

    for(int list = 0; list < lists && !s.bad; ++list)
    {
        long count = s.c();
        s.pos -= 1;
        p.copy(1);

        for(long q = 0; q < count && !s.bad; ++q)
        {
            int slot = -1;

            if(list < 2 ? next_slot < MAXCUSTOMSUBSCREENS : next_slot < MAXCUSTOMSUBSCREENS - 2)
            {
                slot = next_slot++;
                if(list < 2)
                    ++kept[list];
            }

            // the subscreen's own fields, up to its pages
            size_t sub_at = s.pos;
            long name_len = s.c();
            s.skip((size_t)name_len);
            long file_type = s.c();
            long flags = s.l();

            if(file_type == 0 || file_type == 3)
            {
                if(flags & 2)
                    skip_selector(s);
                s.skip(8 + 2);
                skip_transition(s, v);
                skip_transition(s, v);
                if(v >= 21)
                    skip_config(s);
                else
                {
                    long script = s.w();
                    if(script)
                        s.skip(32);
                }
            }

            size_t pages_at = s.pos;
            long pages = s.c();

            // where each page and widget is
            std::vector<size_t> widget_at, widget_end;
            std::vector<Widget> widgets;
            size_t first_page_end = 0;

            for(long pg = 0; pg < pages && !s.bad; ++pg)
            {
                s.skip(4);
                long n = s.w();

                for(long i = 0; i < n && !s.bad; ++i)
                {
                    byte ty = (byte)s.c();
                    Widget w;
                    size_t at = s.pos;
                    walk_widget(s, v, ty, w);

                    if(pg == 0)
                    {
                        widget_at.push_back(at);
                        widget_end.push_back(s.pos);
                        widgets.push_back(w);
                    }
                }

                if(v >= 16)
                {
                    long tr = s.c();
                    for(long i = 0; i < tr && !s.bad; ++i)
                    {
                        s.skip(4);
                        skip_transition(s, v);
                    }
                }

                if(pg == 0)
                    first_page_end = s.pos;
            }

            size_t sub_end = s.pos;
            s.pos = sub_at;

            if(s.bad)
                return;

            if(slot < 0 || p.keep(sub_group_same(slot)))
            {
                p.copy(sub_end - sub_at);
                continue;
            }

            const subscreen_group &g = custom_subscreen[slot];
            const SubGroup &old = g_subs[slot];

            if(g.ss_type != old.ss_type)
                p.fail("a subscreen moved to another kind (from v8 each kind is its own list)");

            // the name: the reader keeps 63 characters
            {
                std::string was = cstr_at(s, sub_at);
                std::string kept_name = std::string(was.c_str()).substr(0, 63);
                std::string now(g.name, strnlen(g.name, 63));

                if(kept_name == now)
                    p.copy(1 + was.size());
                else
                {
                    p.o.c((long)now.size());
                    p.o.put(now.data(), now.size());
                    s.pos += 1 + was.size();
                }
            }

            copy_to(p, pages_at + 1);

            // the first page's widgets that became objects, in order
            int obj = 0;
            int count_now = sub_count(g);
            int converted_widgets = 0;

            for(size_t i = 0; i < widgets.size(); ++i)
                if(widgets[i].converted && converted_widgets < MAXSUBSCREENITEMS)
                    ++converted_widgets;

            if(pages > 0)
            {
                for(size_t i = 0; i < widgets.size() && !s.bad; ++i)
                {
                    const Widget &w = widgets[i];

                    if(!w.converted || obj >= MAXSUBSCREENITEMS)
                        continue;

                    copy_to(p, widget_at[i]);

                    if(obj >= count_now)
                    {
                        p.fail("subscreen objects removed (from v8 they're widgets, and stay)");
                        break;
                    }

                    if(p.keep(sub_obj_same(slot, obj)))
                        copy_to(p, widget_end[i]);
                    else
                    {
                        const subscreen_object &t = g.objects[obj];

                        if(!rewrite_widget(s, v, w, widget_at[i], widget_end[i], t, sub_text(t), p.o))
                            p.fail("a subscreen object value its widget can't store");

                        s.pos = widget_end[i];
                    }

                    ++obj;
                }

                copy_to(p, first_page_end);
            }

            // no widget became an object: the reader puts a placeholder in its place
            bool placeholder = converted_widgets == 0;

            if(placeholder ? !(count_now == 1 && g.objects[0].type == ssoNONE && sub_obj_same(slot, 0)) : count_now != obj)
                p.fail("subscreen objects added (from v8 only a widget can hold one)");

            copy_to(p, sub_end);
        }
    }

    // the placeholders the reader adds, and any slot past the quest's subscreens
    for(int i = next_slot; i < MAXCUSTOMSUBSCREENS; ++i)
        if(!sub_group_same(i))
        {
            p.fail("subscreens added (from v8 they need 3.0's layout)");
            break;
        }

    (void)kept;
}

int write_subscreens(PACKFILE *f, const ZqPartContext &ctx)
{
    const int v = ctx.version;
    bool q255 = ctx.zelda_version >= 0x255;

    if(header_is_253(ctx) && v == V_SUBSCREEN && ctx.cversion == CV_SUBSCREEN)
        return ZQW_UNSUPPORTED;

    // the 2.53 reader's older versions convert more; none of them is in a quest here yet
    if(q255 ? (v < 6 || v > 21) : v != 6)
        return ZQW_UNSUPPORTED;

    Src s;
    Head h;

    if(!start(ctx, s, h))
        return BAD_PART;

    Buf body;
    Patch p(s, body);

    if(v >= 8)
        subs_new(p, v);
    else
        subs_old(p, v, q255);

    return finish(f, ctx, s, h, p, body);
}

ZqWriterRegistration g_reg_subscreens(ID_SUBSCREEN, write_subscreens, "subscreens v6, 2.55 v6-v21");

void snapshot_subscreens()
{
    for(int i = 0; i < MAXCUSTOMSUBSCREENS; ++i)
    {
        const subscreen_group &g = custom_subscreen[i];
        SubGroup &s = g_subs[i];
        s.ss_type = g.ss_type;
        memcpy(s.name, g.name, 64);
        s.objs.resize(sub_count(g));

        for(int j = 0; j < (int)s.objs.size(); ++j)
        {
            s.objs[j].o = g.objects[j];
            s.objs[j].o.dp1 = NULL;
            s.objs[j].text = sub_text(g.objects[j]);
        }
    }
}

} // namespace

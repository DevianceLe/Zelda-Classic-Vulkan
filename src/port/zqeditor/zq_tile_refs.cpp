// See zq_tile_refs.h.

#include "precompiled.h" //always first
#include <stdint.h>

#include <ctype.h>
#include <stdio.h>
#include <map>
#include <string>

#include "zdefs.h"
#include "zc_custom.h"
#include "subscr.h"
#include "zq_tile_refs.h"

extern newcombo *combobuf;
extern itemdata *itemsbuf;
extern wpndata *wpnsbuf;
extern guydata *guysbuf;
extern MsgStr *MsgStrings;
extern word msg_count;
extern dmap *DMaps;
extern miscQdata misc;
extern tiledata *newtilebuf;
int count_tiles(tiledata *buf);

namespace
{

// The kinds of reference, in the order they're listed below.
enum
{
    F_COMBO, F_ITEM, F_WEAPON, F_GUY, F_GUY_S, F_GUY_E, F_STRING,
    F_DMAP_MINI1, F_DMAP_MINI2, F_DMAP_LARGE1, F_DMAP_LARGE2,
    F_MAPSTYLE, F_ICON, F_LINK, F_SUBSCREEN
};

// The tiles a 2.53 file holds: tile numbers 0 to 65,519.
const long LIMIT_253 = TILES_PER_PAGE * 252;

struct Walk
{
    dword section;
    ZqTileRefFn fn;
    void *user;

    bool wants(dword s) const { return section == 0 || section == s; }

    void ref(dword s, int field, int index, long tile, bool narrow) const
    {
        ZqTileRef r;
        r.section = s;
        r.field = field;
        r.index = index;
        r.tile = tile;
        r.narrow = narrow;
        fn(r, user);
    }
};

// Link's sprites, in writelinksprites' order: nine sets of four directions, then casting and
// the four holding sprites.
int *link_sprite(int i)
{
    static int (*const four[])[3] = { walkspr, stabspr, slashspr, floatspr, swimspr, divespr, poundspr, jumpspr, chargespr };

    if(i < 36)
        return four[i / 4][i % 4];

    if(i == 36)
        return castingspr;

    i -= 37;
    return holdspr[i / 2][i % 2];
}

const int LINK_SPRITES = 36 + 1 + 4;

const char *link_sprite_name(int i)
{
    static const char *const names[] = { "walking", "stabbing", "slashing", "floating", "swimming", "diving", "pounding", "jumping", "charging" };

    if(i < 36)
        return names[i / 4];

    return i == 36 ? "casting" : "holding";
}

// The subscreen objects that show tiles, and where (2.53's subscreen editor, zq_subscr.cpp):
// slot 0-3 of an object is d1, d3 or d2-d5; a mini tile is stored as tile*4 + its corner.
void subscreen_refs(const Walk &w)
{
    for(int i = 0; i < MAXCUSTOMSUBSCREENS; ++i)
    {
        for(int j = 0; j < MAXSUBSCREENITEMS; ++j)
        {
            const subscreen_object &o = custom_subscreen[i].objects[j];

            if(o.type == ssoNULL)       // write_one_subscreen stops at the first empty object
                break;

            int at = (i * MAXSUBSCREENITEMS + j) * 4;

            switch(o.type)
            {
            case sso2X2FRAME:
            case ssoTRIFORCE:
            case ssoTILEBLOCK:
            case ssoSELECTOR1:
            case ssoSELECTOR2:
                w.ref(ID_SUBSCREEN, F_SUBSCREEN, at, o.d1, false);
                break;

            case ssoTRIFRAME:
                w.ref(ID_SUBSCREEN, F_SUBSCREEN, at, o.d1, false);
                w.ref(ID_SUBSCREEN, F_SUBSCREEN, at + 1, o.d3, false);
                break;

            case ssoMINITILE:
                if(o.d1 >= 0)           // -1: one of the special tiles (d2), not a tile number
                    w.ref(ID_SUBSCREEN, F_SUBSCREEN, at, o.d1 >> 2, false);
                break;

            case ssoMAGICGAUGE:
            case ssoLIFEGAUGE:
            {
                const int d[4] = { o.d2, o.d3, o.d4, o.d5 };

                for(int k = 0; k < 4; ++k)
                    w.ref(ID_SUBSCREEN, F_SUBSCREEN, at + k, d[k] >> 2, false);

                break;
            }
            }
        }
    }
}

// What the file held at load: tile numbers past 65,519, by field and index.
std::map<uint64_t, long> g_loaded;

uint64_t key_of(const ZqTileRef &r)
{
    return ((uint64_t)(unsigned)r.field << 32) | (uint32_t)r.index;
}

bool past_253(long tile)
{
    return tile < 0 || tile >= LIMIT_253;
}

void note_one(const ZqTileRef &r, void *)
{
    if(past_253(r.tile))
        g_loaded[key_of(r)] = r.tile;
}

struct Misfit
{
    int count;
    ZqTileRef first;
};

void check_one(const ZqTileRef &r, void *user)
{
    if(!past_253(r.tile))
        return;

    std::map<uint64_t, long>::const_iterator it = g_loaded.find(key_of(r));

    // Written back exactly as the file had it. A 16-bit field never loads a number past 65,535.
    if(it != g_loaded.end() && it->second == r.tile && !(r.narrow && (r.tile < 0 || r.tile > 0xFFFF)))
        return;

    Misfit &m = *(Misfit *)user;

    if(m.count++ == 0)
        m.first = r;
}

void hash_one(const ZqTileRef &r, void *user)
{
    if(!r.narrow || ((unsigned long)r.tile & ~0xFFFFUL) == 0)
        return;

    // what the fingerprints' sink does with each byte (FNV-1a)
    uint64_t &h = *(uint64_t *)user;
    const uint32_t v[3] = { (uint32_t)r.field, (uint32_t)r.index, (uint32_t)r.tile };

    for(int i = 0; i < 3; ++i)
        for(int b = 0; b < 4; ++b)
        {
            h ^= (v[i] >> (8 * b)) & 0xFF;
            h *= 1099511628211ULL;
        }
}

std::string number(long n)
{
    char buf[32];

    if(n >= 1000 || n <= -1000)
        snprintf(buf, sizeof(buf), "%ld,%03ld", n / 1000, (n < 0 ? -n : n) % 1000);
    else
        snprintf(buf, sizeof(buf), "%ld", n);

    return buf;
}

} // namespace

void zq_each_tile_ref(dword section, ZqTileRefFn fn, void *user)
{
    Walk w;
    w.section = section;
    w.fn = fn;
    w.user = user;

    if(w.wants(ID_COMBOS) && combobuf)
        for(int i = 0; i < MAXCOMBOS; ++i)
            w.ref(ID_COMBOS, F_COMBO, i, combobuf[i].tile, true);

    if(w.wants(ID_ITEMS) && itemsbuf)
        for(int i = 0; i < MAXITEMS; ++i)
            w.ref(ID_ITEMS, F_ITEM, i, itemsbuf[i].tile, true);

    if(w.wants(ID_WEAPONS) && wpnsbuf)
        for(int i = 0; i < MAXWPNS; ++i)
            w.ref(ID_WEAPONS, F_WEAPON, i, wpnsbuf[i].tile, true);

    if(w.wants(ID_GUYS) && guysbuf)
        for(int i = 0; i < MAXGUYS; ++i)
        {
            w.ref(ID_GUYS, F_GUY, i, guysbuf[i].tile, true);
            w.ref(ID_GUYS, F_GUY_S, i, guysbuf[i].s_tile, true);
            w.ref(ID_GUYS, F_GUY_E, i, guysbuf[i].e_tile, true);
        }

    if(w.wants(ID_STRINGS) && MsgStrings)
        for(int i = 0; i < msg_count; ++i)
            w.ref(ID_STRINGS, F_STRING, i, MsgStrings[i].tile, true);

    if(w.wants(ID_DMAPS) && DMaps)
        for(int i = 0; i < MAXDMAPS; ++i)
        {
            w.ref(ID_DMAPS, F_DMAP_MINI1, i, DMaps[i].minimap_1_tile, true);
            w.ref(ID_DMAPS, F_DMAP_MINI2, i, DMaps[i].minimap_2_tile, true);
            w.ref(ID_DMAPS, F_DMAP_LARGE1, i, DMaps[i].largemap_1_tile, true);
            w.ref(ID_DMAPS, F_DMAP_LARGE2, i, DMaps[i].largemap_2_tile, true);
        }

    if(w.wants(ID_COLORS))
    {
        const long styles[6] =
        {
            misc.colors.triforce_tile, misc.colors.triframe_tile, misc.colors.overworld_map_tile,
            misc.colors.dungeon_map_tile, misc.colors.blueframe_tile, misc.colors.HCpieces_tile
        };

        for(int i = 0; i < 6; ++i)
            w.ref(ID_COLORS, F_MAPSTYLE, i, styles[i], true);
    }

    if(w.wants(ID_ICONS))
        for(int i = 0; i < 4; ++i)
            w.ref(ID_ICONS, F_ICON, i, misc.icons[i], true);

    if(w.wants(ID_LINKSPRITES))
        for(int i = 0; i < LINK_SPRITES; ++i)
            w.ref(ID_LINKSPRITES, F_LINK, i, link_sprite(i)[spr_tile], true);

    if(w.wants(ID_SUBSCREEN))
        subscreen_refs(w);
}

std::string zq_tile_ref_name(const ZqTileRef &r)
{
    char buf[96];

    switch(r.field)
    {
    case F_COMBO: snprintf(buf, sizeof(buf), "combo %d", r.index); break;
    case F_ITEM: snprintf(buf, sizeof(buf), "item %d", r.index); break;
    case F_WEAPON: snprintf(buf, sizeof(buf), "weapon sprite %d", r.index); break;
    case F_GUY: snprintf(buf, sizeof(buf), "enemy %d (old tile)", r.index); break;
    case F_GUY_S: snprintf(buf, sizeof(buf), "enemy %d (special tile)", r.index); break;
    case F_GUY_E: snprintf(buf, sizeof(buf), "enemy %d (new tile)", r.index); break;
    case F_STRING: snprintf(buf, sizeof(buf), "string %d", r.index); break;
    case F_DMAP_MINI1:
    case F_DMAP_MINI2:
    case F_DMAP_LARGE1:
    case F_DMAP_LARGE2: snprintf(buf, sizeof(buf), "DMap %d's map", r.index); break;
    case F_MAPSTYLE: snprintf(buf, sizeof(buf), "the map styles"); break;
    case F_ICON: snprintf(buf, sizeof(buf), "game icon %d", r.index); break;
    case F_LINK: snprintf(buf, sizeof(buf), "Link's %s sprite", link_sprite_name(r.index)); break;
    case F_SUBSCREEN: snprintf(buf, sizeof(buf), "subscreen %d, object %d", r.index / 4 / MAXSUBSCREENITEMS, r.index / 4 % MAXSUBSCREENITEMS); break;
    default: snprintf(buf, sizeof(buf), "something"); break;
    }

    return buf;
}

void zq_tile_refs_note_loaded()
{
    g_loaded.clear();
    zq_each_tile_ref(0, note_one, NULL);
}

int zq_tile_refs_loaded_past(std::string *names)
{
    if(names)
    {
        names->clear();
        int shown = 0;

        for(std::map<uint64_t, long>::const_iterator it = g_loaded.begin(); it != g_loaded.end() && shown < 4; ++it, ++shown)
        {
            ZqTileRef r;
            r.section = 0;
            r.field = (int)(it->first >> 32);
            r.index = (int)(uint32_t)it->first;
            r.tile = it->second;
            r.narrow = false;
            *names += (shown ? ", " : "") + zq_tile_ref_name(r) + " (tile " + number(r.tile) + ")";
        }
    }

    return (int)g_loaded.size();
}

bool zq_tiles_fit_253(std::string &why)
{
    int used = count_tiles(newtilebuf);

    if(used > LIMIT_253)
    {
        why = "Tile " + number(used - 1) + " is drawn on";
        return false;
    }

    Misfit m;
    m.count = 0;
    zq_each_tile_ref(0, check_one, &m);

    if(m.count == 0)
        return true;

    why = zq_tile_ref_name(m.first) + " uses tile " + number(m.first.tile);

    if(m.count > 1)
        why += " (" + number(m.count) + " tile numbers past 65,519 in all)";

    why[0] = (char)toupper((unsigned char)why[0]);
    return false;
}

void zq_hash_wide_tile_refs(dword section, uint64_t *h)
{
    zq_each_tile_ref(section, hash_one, h);
}

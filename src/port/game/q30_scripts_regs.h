// Registers 3.0 and 2.55 scripts use that 2.53's get_register has no case for.
// Upstream's numbers (q30_scripts_defs.h), ported from engine/src/zc/scripting/types/.
// Included once, from src/ffscript.cpp.

#ifndef ZC_Q30_SCRIPTS_REGS_H
#define ZC_Q30_SCRIPTS_REGS_H

#include <limits.h>
#include <math.h>

#include "port/game/q30_audio.h"

#define Z30_F        zasm30_frame()
#define Z30_REF(x)   (Z30_F.refs[Z30REF_##x])

// Index registers the way upstream names them (rINDEX, rINDEX2, rEXP1, rSFTEMP, rWHAT_NO_7).
#define Z30_INDEX    (ri->d[0] / 10000)
#define Z30_D(n)     (ri->d[n])

extern byte quest_rules[QUESTRULES_SIZE];

// Fields 3.0 has and 2.53 hasn't, keyed by register + object + index, so scripts can store and
// read them back. A write here is also how an unported field shows up in the log.
static std::map<std::pair<std::pair<long,long>,long>, long> zasm30_store;

static long zasm30_store_get(long reg, long ref, long idx)
{
    std::map<std::pair<std::pair<long,long>,long>, long>::iterator it = zasm30_store.find(std::make_pair(std::make_pair(reg, ref), idx));
    return it == zasm30_store.end() ? 0 : it->second;
}

static void zasm30_store_set(long reg, long ref, long idx, long value)
{
    zasm30_store[std::make_pair(std::make_pair(reg, ref), idx)] = value;
}

void zasm30_log_once(const char *kind, long id)
{
    static std::set<long> seen;

    if(seen.insert(((long)(kind[0]) << 24) ^ id).second)
        al_trace("ZASM: unhandled %s %ld\n", kind, id);
}

// ---- resolving a sprite reference --------------------------------------------------------------

enum { Z30S_NONE, Z30S_HERO, Z30S_NPC, Z30S_LWPN, Z30S_EWPN, Z30S_ITEM, Z30S_FFC };

#define Z30_FFC_BASE 0x7FF00000L

struct Z30Sprite
{
    sprite *s;
    int kind;
    int ffc;
};

static Z30Sprite z30_sprite(long uid)
{
    Z30Sprite r;
    r.s = NULL;
    r.kind = Z30S_NONE;
    r.ffc = -1;

    if(uid >= Z30_FFC_BASE)
    {
        r.ffc = uid - Z30_FFC_BASE;
        if(r.ffc >= 0 && r.ffc < MAXFFCS)
            r.kind = Z30S_FFC;
        return r;
    }

    if(uid == 1 || uid == Link.getUID())
    {
        r.s = &Link;
        r.kind = Z30S_HERO;
        return r;
    }

    if((r.s = guys.getByUID(uid)) != NULL) { r.kind = Z30S_NPC; return r; }
    if((r.s = Lwpns.getByUID(uid)) != NULL) { r.kind = Z30S_LWPN; return r; }
    if((r.s = Ewpns.getByUID(uid)) != NULL) { r.kind = Z30S_EWPN; return r; }
    if((r.s = items.getByUID(uid)) != NULL) { r.kind = Z30S_ITEM; return r; }
    return r;
}

// ffc->Width and friends are packed: low 6 bits hit size - 1, top 2 bits tile size - 1.
static int z30_ffc_hit_w(int i) { return (tmpscr->ffwidth[i] & 0x3F) + 1; }
static int z30_ffc_hit_h(int i) { return (tmpscr->ffheight[i] & 0x3F) + 1; }
static int z30_ffc_tile_w(int i) { return (tmpscr->ffwidth[i] >> 6) + 1; }
static int z30_ffc_tile_h(int i) { return (tmpscr->ffheight[i] >> 6) + 1; }

// The fractional part of a sprite position is kept only for 3.0 quests with qr_SPRITEXY_IS_FLOAT.
static long z30_fixreg(fix f, bool as_float)
{
    return as_float ? (long)(double(f) * 10000.0) : long(int(f)) * 10000;
}

static long z30_fl(fix f) { return (long)(double(f) * 10000.0); }

// FakeJump and FakeZ are kept as the script's own numbers; the stored jump is in the scale Jump uses inside.
static long z30_fake_jump_read(long raw) { return raw / -100; }
static long z30_fake_jump_write(long value) { return (long)(int64_t(value) * -100); }

// ---- SPRITE_* ----------------------------------------------------------------------------------

static bool zasm30_sprite_read(const long arg, long &ret, const Z30Sprite &r)
{
    sprite *s = r.s;

    const bool isffc = r.kind == Z30S_FFC;
    const bool isfloat = isffc;   // 2.53 sprites have no sub-pixel position to report
    const int f = r.ffc;

    Zasm30SpriteState *st = s ? &zasm30_sprite_state(s->getUID()) : NULL;

    switch(arg)
    {
    case Z30R_SPRITE_SPAWN_SCREEN:
    case Z30R_SPRITE_CURRENT_SCREEN: ret = currscr * 10000; return true;
    case Z30R_SPRITE_X:
        if(isffc) ret = tmpscr->ffx[f];
        else ret = z30_fixreg(s->x, false);
        return true;
    case Z30R_SPRITE_Y:
        if(isffc) ret = tmpscr->ffy[f];
        else ret = z30_fixreg(s->y, false);
        return true;
    case Z30R_SPRITE_Z:
        if(isffc) ret = 0;
        else ret = z30_fixreg(s->z, false);
        return true;
    case Z30R_SPRITE_FAKE_Z: ret = st ? st->fakez : 0; return true;
    case Z30R_SPRITE_X_OFFSET: ret = s ? long(int(s->xofs)) * 10000 : 0; return true;
    case Z30R_SPRITE_Y_OFFSET: ret = s ? (int(s->yofs) - playing_field_offset) * 10000 : 0; return true;
    case Z30R_SPRITE_Z_OFFSET: ret = s ? long(int(s->zofs)) * 10000 : 0; return true;
    case Z30R_SPRITE_ROTATION: ret = st ? st->rotation * 10000 : 0; return true;
    case Z30R_SPRITE_SCALE: ret = st ? st->scale * 100 : 10000; return true;
    case Z30R_SPRITE_DIR: ret = s ? s->dir * 10000 : 0; return true;
    case Z30R_SPRITE_INVIS_CLK: ret = st ? st->invis_clk * 10000 : 0; return true;
    case Z30R_SPRITE_TILE:
        if(isffc) ret = combobuf[tmpscr->ffdata[f]].tile * 10000;
        else ret = s->tile * 10000;
        return true;
    case Z30R_SPRITE_SCRIPT_TILE: ret = st ? st->script_tile * 10000 : -10000; return true;
    case Z30R_SPRITE_TILE_W:
        if(isffc) ret = z30_ffc_tile_w(f) * 10000;
        else ret = s->txsz * 10000;
        return true;
    case Z30R_SPRITE_TILE_H:
        if(isffc) ret = z30_ffc_tile_h(f) * 10000;
        else ret = s->tysz * 10000;
        return true;
    case Z30R_SPRITE_CSET:
        if(isffc) ret = tmpscr->ffcset[f] * 10000;
        else if(r.kind == Z30S_ITEM) ret = (((item *)s)->o_cset & 0xF) * 10000;
        else ret = (s->cs & 0xF) * 10000;
        return true;
    case Z30R_SPRITE_DRAW_STYLE: ret = s ? s->drawstyle * 10000 : 0; return true;
    case Z30R_SPRITE_JUMP:
        if(r.kind == Z30S_HERO) { ret = z30_fl(Link.getFall()) / -100; return true; }
        ret = s ? z30_fl(s->fall) / -100 : 0;
        return true;
    case Z30R_SPRITE_FAKE_JUMP: ret = st ? z30_fake_jump_read(st->fakefall) : 0; return true;
    case Z30R_SPRITE_GRAVITY: ret = (st && st->gravity_strength >= 0) ? 10000 : 10000; return true;
    case Z30R_SPRITE_GRAVITY_STRENGTH: ret = st ? st->gravity_strength : 0; return true;
    case Z30R_SPRITE_TERMINAL_VELOCITY: ret = st ? st->terminal_v : 0; return true;
    case Z30R_SPRITE_CUSTOM_GRAVITY_STRENGTH: ret = st ? st->gravity_strength : 0; return true;
    case Z30R_SPRITE_CUSTOM_TERMINAL_VELOCITY: ret = st ? st->terminal_v : 0; return true;
    case Z30R_SPRITE_FLIP: ret = s ? s->flip * 10000 : 0; return true;
    case Z30R_SPRITE_SCRIPT_FLIP: ret = st ? st->script_flip * 10000 : -10000; return true;
    case Z30R_SPRITE_ENGINE_ANIMATE: ret = st ? (st->engine_animate ? 10000 : 0) : 10000; return true;
    case Z30R_SPRITE_EXTEND: ret = s ? s->extend * 10000 : 0; return true;
    case Z30R_SPRITE_HIT_WIDTH:
        if(isffc) ret = z30_ffc_hit_w(f) * 10000;
        else ret = s->hxsz * 10000;
        return true;
    case Z30R_SPRITE_HIT_HEIGHT:
        if(isffc) ret = z30_ffc_hit_h(f) * 10000;
        else ret = s->hysz * 10000;
        return true;
    case Z30R_SPRITE_HIT_ZHEIGHT: ret = s ? s->hzsz * 10000 : 0; return true;
    case Z30R_SPRITE_HIT_OFFSET_X: ret = s ? s->hxofs * 10000 : 0; return true;
    case Z30R_SPRITE_HIT_OFFSET_Y: ret = s ? s->hyofs * 10000 : 0; return true;
    case Z30R_SPRITE_FALL_CLK: ret = st ? st->fall_clk * 10000 : 0; return true;
    case Z30R_SPRITE_DROWN_CLK: ret = st ? st->drown_clk * 10000 : 0; return true;
    case Z30R_SPRITE_FALL_CMB:
    case Z30R_SPRITE_DROWN_CMB:
        ret = zasm30_store_get(arg, Z30_REF(SPRITE), 0); return true;
    case Z30R_SPRITE_LIGHT_RADIUS: ret = st ? st->glow_rad * 10000 : 0; return true;
    case Z30R_SPRITE_LIGHT_SHAPE: ret = st ? st->glow_shape * 10000 : 0; return true;
    case Z30R_SPRITE_LIGHT_OFFSET: ret = st ? st->glow_ofs * 10000 : 0; return true;
    case Z30R_SPRITE_NOCOLL_CLK: ret = st ? st->no_colldet * 10000 : 0; return true;
    case Z30R_SPRITE_SWHOOKED: ret = 0; return true;
    case Z30R_SPRITE_SHADOW_SPR: ret = st ? st->shadow_spr * 10000 : 0; return true;
    case Z30R_SPRITE_SHADOW_XOFS: ret = st ? st->shadow_xofs * 10000 : 0; return true;
    case Z30R_SPRITE_SHADOW_YOFS: ret = st ? st->shadow_yofs * 10000 : 0; return true;
    case Z30R_SPRITE_VIEWPORT_SUSPEND_RANGE:
    case Z30R_SPRITE_VIEWPORT_DESPAWN_RANGE: ret = 0; return true;
    case Z30R_SPRITE_MISCD:
    {
        long n = Z30_INDEX;

        if(isffc)
        {
            ret = (n >= 0 && n < 16) ? ffmisc[f][n] : 0;
            return true;
        }

        if(n >= 0 && n < 16) ret = s->miscellaneous[n];
        else if(n >= 16 && n < 32 && st) ret = st->misc2[n - 16];
        else ret = 0;
        return true;
    }
    case Z30R_SPRITE_MOVE_FLAGS:
        ret = (st && Z30_INDEX >= 0 && Z30_INDEX < 32 && (st->move_flags & (1L << Z30_INDEX))) ? 10000 : 0;
        return true;
    }

    return false;
}

// Is this register a sprite field at all? Asked of Link, whose fields can always be read.
static bool z30_is_sprite_register(const long arg)
{
    Z30Sprite hero;
    hero.s = &Link;
    hero.kind = Z30S_HERO;
    hero.ffc = -1;

    long unused;
    return zasm30_sprite_read(arg, unused, hero);
}

static bool zasm30_sprite_get(const long arg, long &ret)
{
    Z30Sprite r = z30_sprite(Z30_REF(SPRITE));

    if(r.kind != Z30S_NONE)
        return zasm30_sprite_read(arg, ret, r);

    // With no sprite selected a sprite field reads 0, and any other register isn't ours to answer.
    if(!z30_is_sprite_register(arg))
        return false;

    ret = 0;
    return true;
}

static bool zasm30_sprite_set(const long arg, const long value)
{
    Z30Sprite r = z30_sprite(Z30_REF(SPRITE));
    sprite *s = r.s;

    if(r.kind == Z30S_NONE)
        return z30_is_sprite_register(arg);

    const bool isffc = r.kind == Z30S_FFC;
    const bool ishero = r.kind == Z30S_HERO;
    const int f = r.ffc;
    Zasm30SpriteState *st = s ? &zasm30_sprite_state(s->getUID()) : NULL;

    switch(arg)
    {
    case Z30R_SPRITE_SPAWN_SCREEN:
    case Z30R_SPRITE_CURRENT_SCREEN: return true;
    case Z30R_SPRITE_X:
        if(isffc) tmpscr->ffx[f] = value;
        else if(ishero) Link.setX(value / 10000);
        else s->x = fix(value / 10000);
        return true;
    case Z30R_SPRITE_Y:
        if(isffc) tmpscr->ffy[f] = value;
        else if(ishero) Link.setY(value / 10000);
        else
        {
            if(r.kind == Z30S_NPC)
                ((enemy *)s)->floor_y += fix(value / 10000) - s->y;
            s->y = fix(value / 10000);
        }
        return true;
    case Z30R_SPRITE_Z:
        if(isffc) return true;
        if(ishero) { Link.setZ(value / 10000); return true; }
        if(r.kind == Z30S_NPC && never_in_air(s->id)) return true;
        s->z = fix(value / 10000);
        if(s->z < 0) s->z = 0;
        return true;
    case Z30R_SPRITE_FAKE_Z: if(st) st->fakez = value; return true;
    case Z30R_SPRITE_X_OFFSET: if(s) s->xofs = value / 10000; return true;
    case Z30R_SPRITE_Y_OFFSET: if(s) s->yofs = value / 10000 + playing_field_offset; return true;
    case Z30R_SPRITE_Z_OFFSET: if(s) s->zofs = value / 10000; return true;
    case Z30R_SPRITE_ROTATION: if(st) st->rotation = value / 10000; return true;
    case Z30R_SPRITE_SCALE: if(st) st->scale = value / 100; return true;
    case Z30R_SPRITE_DIR:
        if(ishero) Link.setDir(value / 10000);
        else if(s) s->dir = value / 10000;
        return true;
    case Z30R_SPRITE_INVIS_CLK: if(st) st->invis_clk = value / 10000; return true;
    case Z30R_SPRITE_TILE: if(s) s->tile = vbound(value / 10000, 0, NEWMAXTILES - 1); return true;
    case Z30R_SPRITE_SCRIPT_TILE: if(st) st->script_tile = value / 10000; return true;
    case Z30R_SPRITE_TILE_W: if(s) s->txsz = vbound(value / 10000, 1, 20); return true;
    case Z30R_SPRITE_TILE_H: if(s) s->tysz = vbound(value / 10000, 1, 20); return true;
    case Z30R_SPRITE_CSET:
        if(isffc) tmpscr->ffcset[f] = (value / 10000) & 0xF;
        else if(r.kind == Z30S_ITEM)
        {
            item *i = (item *)s;
            i->o_cset = (i->o_cset & ~15) | ((value / 10000) & 15);
            i->cs = i->o_cset & 15;
        }
        else if(s) s->cs = (value / 10000) & 0xF;
        return true;
    case Z30R_SPRITE_DRAW_STYLE: if(s) s->drawstyle = value / 10000; return true;
    case Z30R_SPRITE_JUMP:
        if(ishero) Link.setFall(fix(-(value / 10000.0) * 100));
        else if(s) s->fall = fix(-(value / 10000.0) * 100);
        return true;
    case Z30R_SPRITE_FAKE_JUMP: if(st) st->fakefall = z30_fake_jump_write(value); return true;
    case Z30R_SPRITE_GRAVITY: return true;
    case Z30R_SPRITE_CUSTOM_GRAVITY_STRENGTH: if(st) st->gravity_strength = value; return true;
    case Z30R_SPRITE_CUSTOM_TERMINAL_VELOCITY: if(st) st->terminal_v = value; return true;
    case Z30R_SPRITE_FLIP: if(s) s->flip = value / 10000; return true;
    case Z30R_SPRITE_SCRIPT_FLIP: if(st) st->script_flip = vbound(value / 10000, -1, 127); return true;
    case Z30R_SPRITE_ENGINE_ANIMATE: if(st) st->engine_animate = value != 0; return true;
    case Z30R_SPRITE_EXTEND: if(s) s->extend = value / 10000; return true;
    case Z30R_SPRITE_HIT_WIDTH: if(s) s->hxsz = value / 10000; return true;
    case Z30R_SPRITE_HIT_HEIGHT: if(s) s->hysz = value / 10000; return true;
    case Z30R_SPRITE_HIT_ZHEIGHT: if(s) s->hzsz = value / 10000; return true;
    case Z30R_SPRITE_HIT_OFFSET_X: if(s) s->hxofs = value / 10000; return true;
    case Z30R_SPRITE_HIT_OFFSET_Y: if(s) s->hyofs = value / 10000; return true;
    case Z30R_SPRITE_FALL_CLK: if(st) st->fall_clk = vbound(value / 10000, 0, 70); return true;
    case Z30R_SPRITE_DROWN_CLK: if(st) st->drown_clk = vbound(value / 10000, 0, 70); return true;
    case Z30R_SPRITE_FALL_CMB:
    case Z30R_SPRITE_DROWN_CMB:
        zasm30_store_set(arg, Z30_REF(SPRITE), 0, value); return true;
    case Z30R_SPRITE_LIGHT_RADIUS: if(st) st->glow_rad = vbound(value / 10000, 0, 255); return true;
    case Z30R_SPRITE_LIGHT_SHAPE: if(st) st->glow_shape = vbound(value / 10000, 0, 255); return true;
    case Z30R_SPRITE_LIGHT_OFFSET: if(st) st->glow_ofs = value / 10000; return true;
    case Z30R_SPRITE_NOCOLL_CLK: if(st) st->no_colldet = value / 10000; return true;
    case Z30R_SPRITE_SWHOOKED: return true;
    case Z30R_SPRITE_SHADOW_SPR: if(st) st->shadow_spr = vbound(value / 10000, 0, 255); return true;
    case Z30R_SPRITE_SHADOW_XOFS: if(st) st->shadow_xofs = value / 10000; return true;
    case Z30R_SPRITE_SHADOW_YOFS: if(st) st->shadow_yofs = value / 10000; return true;
    case Z30R_SPRITE_VIEWPORT_SUSPEND_RANGE:
    case Z30R_SPRITE_VIEWPORT_DESPAWN_RANGE: return true;
    case Z30R_SPRITE_MISCD:
    {
        long n = Z30_INDEX;

        if(isffc)
        {
            if(n >= 0 && n < 16) ffmisc[f][n] = value;
            return true;
        }

        if(n >= 0 && n < 16) s->miscellaneous[n] = value;
        else if(n >= 16 && n < 32 && st) st->misc2[n - 16] = value;
        return true;
    }
    case Z30R_SPRITE_MOVE_FLAGS:
        if(st && Z30_INDEX >= 0 && Z30_INDEX < 32)
        {
            if(value) st->move_flags |= (1L << Z30_INDEX);
            else st->move_flags &= ~(1L << Z30_INDEX);
        }
        return true;
    }

    return false;
}

// ---- the reference registers -------------------------------------------------------------------

static int z30_ref_slot(long arg)
{
    switch(arg)
    {
    case Z30R_REFBITMAP: return Z30REF_BITMAP;
    case Z30R_REFBOTTLESHOP: return Z30REF_BOTTLESHOP;
    case Z30R_REFBOTTLETYPE: return Z30REF_BOTTLETYPE;
    case Z30R_REFCOMBODATA: return Z30REF_COMBODATA;
    case Z30R_REFCOMBOTRIGGER: return Z30REF_COMBOTRIGGER;
    case Z30R_REFDIRECTORY: return Z30REF_DIRECTORY;
    case Z30R_REFDMAPDATA: return Z30REF_DMAPDATA;
    case Z30R_REFDROPSETDATA: return Z30REF_DROPSETDATA;
    case Z30R_REFFILE: return Z30REF_FILE;
    case Z30R_REFGENERICDATA: return Z30REF_GENERICDATA;
    case Z30R_REFMAPDATA: return Z30REF_MAPDATA;
    case Z30R_REFMSGDATA: return Z30REF_MSGDATA;
    case Z30R_REFMUSIC: return Z30REF_MUSIC;
    case Z30R_REFNPCDATA: return Z30REF_NPCDATA;
    case Z30R_REFPALDATA: return Z30REF_PALDATA;
    case Z30R_REFPORTAL: return Z30REF_PORTAL;
    case Z30R_REFRNG: return Z30REF_RNG;
    case Z30R_REFSAVEMENU: return Z30REF_SAVEMENU;
    case Z30R_REFSAVPORTAL: return Z30REF_SAVPORTAL;
    case Z30R_REFSCREEN: return Z30REF_SCREEN;
    case Z30R_REFSHOPDATA: return Z30REF_SHOPDATA;
    case Z30R_REFSPRITE: return Z30REF_SPRITE;
    case Z30R_REFSPRITEDATA: return Z30REF_SPRITEDATA;
    case Z30R_REFSTACK: return Z30REF_STACK;
    case Z30R_REFSUBSCREENDATA: return Z30REF_SUBSCREENDATA;
    case Z30R_REFSUBSCREENPAGE: return Z30REF_SUBSCREENPAGE;
    case Z30R_REFSUBSCREENWIDG: return Z30REF_SUBSCREENWIDG;
    case Z30R_REFWEAPDATA: return Z30REF_WEAPDATA;
    case Z30R_REFWEBSOCKET: return Z30REF_WEBSOCKET;
    }
    return -1;
}

// ---- helpers for the mapdata family -----------------------------------------------------------

// A mapdata reference: 0 is none, otherwise the map screen (map*MAPSCRS+scr) + 1, or a
// temporary (tmpscr / layer) reference with the high bit set: 0x40000000 | layer.
#define Z30_MAPDATA_TMP 0x40000000L

static mapscr *z30_mapdata(long ref)
{
    if(ref <= 0)
        return NULL;

    if(ref & Z30_MAPDATA_TMP)
    {
        const long layer = ref & 0xFFFF;
        if(layer == 0) return tmpscr;
        if(layer >= 1 && layer <= 6) return &tmpscr2[layer - 1];
        return NULL;
    }

    const long index = ref - 1;
    return index < (long)TheMaps.size() ? &TheMaps[index] : NULL;
}

// 3.0 combo data kept by the combo reader; NULL when the quest gave this combo none.
#include "q30_triggers.h"
#pragma comment(linker, "/alternatename:?q30_combo@@YAPBUQ30Combo@@H@Z=?zasm30_no_combo@@YAPBUQ30Combo@@H@Z")
const Q30Combo *zasm30_no_combo(int) { return NULL; }

static long z30_combo_attr_get(int cid, long idx)
{
    if(const Q30Combo *c = q30_combo(cid))
        return (idx >= 0 && idx < 24) ? c->attributes[idx] : 0;
    return zasm30_store_get(Z30R_COMBODATTRIBUTES, cid, idx);
}

static void z30_combo_attr_set(int cid, long idx, long value)
{
    if(const Q30Combo *c = q30_combo(cid))
    {
        if(idx >= 0 && idx < 24)
            const_cast<Q30Combo *>(c)->attributes[idx] = value;
        return;
    }
    zasm30_store_set(Z30R_COMBODATTRIBUTES, cid, idx, value);
}

// Functions other areas provide (q30_triggers.cpp); this build falls back to no-ops if they
// aren't linked yet.
#pragma comment(linker, "/alternatename:?q30_get_exstate@@YA_NHH@Z=?zasm30_no_get_exstate@@YA_NHH@Z")
#pragma comment(linker, "/alternatename:?q30_set_exstate@@YAXHH_N@Z=?zasm30_no_set_exstate@@YAXHH_N@Z")
#pragma comment(linker, "/alternatename:?q30_get_xdoor@@YA_NHHH@Z=?zasm30_no_get_xdoor@@YA_NHHH@Z")
#pragma comment(linker, "/alternatename:?q30_set_xdoor@@YAXHHH_N@Z=?zasm30_no_set_xdoor@@YAXHHH_N@Z")
bool zasm30_no_get_exstate(int, int) { return false; }
void zasm30_no_set_exstate(int, int, bool) {}
bool zasm30_no_get_xdoor(int, int, int) { return false; }
void zasm30_no_set_xdoor(int, int, int, bool) {}

// The combo a script has selected. A reference to no combo reads as a blank one, and writes to it
// go nowhere.
static newcombo &z30_cdata()
{
    static newcombo blank;
    const long id = Z30_REF(COMBODATA);

    if(id >= 0 && id < MAXCOMBOS)
        return combobuf[id];

    memset(&blank, 0, sizeof(blank));
    return blank;
}

// ---- what the registers below read and write --------------------------------------------------

// Hero->HitBy[]: what hit the Hero this frame (upstream's HIT_BY_*). Filled from checkhit().
enum { Z30_HITBY_COUNT = 17 };
static long zasm30_hitby[Z30_HITBY_COUNT];

static bool z30_hitby_is_uid(long n)
{
    return (n >= 4 && n <= 7) || (n >= 13 && n <= 15);
}

// Hero->ScriptTile and friends. Hero->ScriptTile is 0 when unset; the others are -1.
static long zasm30_hero_script_tile = 0, zasm30_hero_script_flip = -1, zasm30_hero_script_cset = -1;
static long zasm30_hero_move_flags = 0;
static long zasm30_hero_immortal = 0, zasm30_hero_hurt_sfx = 0;     // 0: not set
static bool zasm30_no_active_subscreen = false;
static bool zasm30_key_input[127], zasm30_key_press[127], zasm30_key_disabled[127], zasm30_key_was_down[127];
static bool zasm30_button_disabled[18];

// Input->Key[] and KeyPress[] for this frame, from the keys as they stand right after polling.
static void zasm30_update_input()
{
    for(int q = 0; q < 127; ++q)
    {
        const bool down = key[q] != 0;
        zasm30_key_press[q] = down && !zasm30_key_was_down[q];
        zasm30_key_input[q] = down;
        zasm30_key_was_down[q] = down;
    }

    for(int q = 0; q < 18; ++q)
    {
        if(zasm30_button_disabled[q])
        {
            control_state[q] = false;
            button_press[q] = false;
        }
    }
}

int q30_enemy_clk2(enemy *e);
void q30_enemy_set_clk2(enemy *e, int value);
int q30_enemy_hitdir(enemy *e);
void q30_enemy_set_hitdir(enemy *e, int dir);
int q30_enemy_lined_up(enemy *e, int range, bool dir8);

static enemy *z30_npc_ref() { return (enemy *)guys.getByUID(ri->guyref); }
static weapon *z30_lwpn_ref() { return (weapon *)Lwpns.getByUID(ri->lwpn); }
static weapon *z30_ewpn_ref() { return (weapon *)Ewpns.getByUID(ri->ewpn); }
static item *z30_item_ref() { return (item *)items.getByUID(ri->itemref); }

// The script state of the sprite a register is about, or NULL if it no longer exists. A sprite that
// has no entry yet gets one, which is why this asks for the sprite first.
static Zasm30SpriteState *z30_state_of(sprite *s)
{
    return s ? &zasm30_sprite_state(s->getUID()) : NULL;
}

// A sprite's one-bit flag array (MoveFlags[]): the bit asked for, as 0 or 1.
static bool z30_flag_get(long bits, long n, long count)
{
    return n >= 0 && n < count && (bits & (1L << n)) != 0;
}

static void z30_flag_set(long &bits, long n, long count, bool on)
{
    if(n < 0 || n >= count)
        return;

    if(on)
        bits |= 1L << n;
    else
        bits &= ~(1L << n);
}

// What the sprite draws with this frame: the CSet that flashing and dying give it, as enemy::draw works it out.
static int z30_flashing_cset(enemy *e)
{
    if(e->dying)
        return (((q30_enemy_clk2(e) + 5) >> 1) & 3) + 6;

    if(e->hclk <= 0)
    {
        if(e->flags & guy_flashing)
            return (frame & 3) + 6;
        return e->cs;
    }

    if(e->family == eeGANON)
        return (((e->hclk - 1) >> 1) & 3) + 6;

    if(e->hclk < 33 && !get_bit(quest_rules, qr_ENEMIESFLICKER))
        return (((e->hclk - 1) >> 1) & 3) + 6;

    return e->cs;
}

static mapscr *z30_mapdata_now() { return z30_mapdata(Z30_REF(MAPDATA)); }

static const int Z30_FFC_COUNT = 32;

// Which map screen a mapdata reference is, for state kept per screen; -1 if it has none.
static int z30_mapdata_mi(long ref)
{
    if(ref <= 0)
        return -1;

    if(ref & Z30_MAPDATA_TMP)
        return (ref & 0xFFFF) == 0 ? currmap * MAPSCRSNORMAL + currscr : -1;

    const long index = ref - 1, screen = index % MAPSCRS;
    return (index < (long)TheMaps.size() && screen < MAPSCRSNORMAL) ? (index / MAPSCRS) * MAPSCRSNORMAL + screen : -1;
}

// DMap MIDIs in this engine: 0 none, 1 overworld, 2 dungeon, 3 level 9, 4 up custom. Scripts number
// them as 2.55 did, where -3 is none, -2 overworld, -1 dungeon, 0 level 9 and 1 up custom.
static int z30_old_midi_id(int midi)
{
    switch(midi)
    {
    case 0: return -3;
    case 1: return -2;
    case 2: return -1;
    case 3: return 0;
    default: return midi - 3;
    }
}

static int z30_new_midi_id(int id)
{
    switch(id)
    {
    case -3: return 0;
    case -2: return 1;
    case -1: return 2;
    case 0: return 3;
    default: return id > 0 && id <= 252 ? id + 3 : -1;
    }
}

static bool zasm30_paldata_get(const long arg, long &ret);
static bool zasm30_paldata_set(const long arg, const long value);
static bool zasm30_sub_get(const long arg, long &ret);       // q30_scripts_subscr.h
static bool zasm30_scrdata_get(const long arg, long &ret);   // q30_scripts_save.h
static bool zasm30_scrdata_set(const long arg, const long value);
static bool zasm30_sub_set(const long arg, const long value);

// ---- smaller 3.0 fields ------------------------------------------------------------------------

enum { Z30_QR_WRITE_ENTRYPOINTS_AFFECTS_HEROCLASS = 685 };

// Per-item and per-enemy fields 2.53's itemdata and guydata have no room for, from the quest's
// item and enemy sections (q255_items.cpp, q255_guys.cpp). Scripts can change them at runtime.
static std::map<int, std::string> zasm30_item_display_names;
static std::map<int, int> zasm30_item_override_flags, zasm30_guy_wpnsprite;

void q30_clear_item_extras() { zasm30_item_display_names.clear(); zasm30_item_override_flags.clear(); }
void q30_clear_guy_extras() { zasm30_guy_wpnsprite.clear(); }

void q30_set_item_display_name(int id, const char *name)
{
    if(name && name[0]) zasm30_item_display_names[id] = name;
    else zasm30_item_display_names.erase(id);
}

void q30_set_item_override_flags(int id, int flags)
{
    if(flags) zasm30_item_override_flags[id] = flags;
    else zasm30_item_override_flags.erase(id);
}

void q30_set_guy_wpnsprite(int id, int spr)
{
    if(spr) zasm30_guy_wpnsprite[id] = spr;
    else zasm30_guy_wpnsprite.erase(id);
}

static int z30_map_get(const std::map<int, int> &m, int key)
{
    std::map<int, int>::const_iterator it = m.find(key);
    return it == m.end() ? 0 : it->second;
}

bool q30_item_has_display_name(int id) { return zasm30_item_display_names.count(id) != 0; }

static std::string z30_item_display_name(int id)
{
    std::map<int, std::string>::const_iterator it = zasm30_item_display_names.find(id);
    return it == zasm30_item_display_names.end() ? std::string() : it->second;
}

// The name a subscreen shows for an item: the display name when it has one (upstream puts a bottle's
// contents where "%s" is; this engine has no bottles, so "%s" is dropped), otherwise its name.
std::string q30_item_shown_name(int id)
{
    std::string s = z30_item_display_name(id);

    if(s.empty())
        return (id >= 0 && id < ITEMCNT) ? std::string(item_string[id]) : std::string();

    // "%s" is a bottle's contents ("Empty" when it holds nothing; q30_triggers.cpp), else nothing
    const std::string contents = q30_bottle_contents_name(id);

    for(size_t p = 0; (p = s.find("%s", p)) != std::string::npos; p += contents.size())
        s.replace(p, 2, contents);

    return s;
}

// npc->Core: false for the extra segments of the multi-part bosses, which 2.53 spawns with
// id + 0x1000 or more (upstream sets isCore false in exactly these classes' constructors).
static bool z30_npc_is_core(enemy *e)
{
    if((e->id & 0xF000) == 0)
        return true;

    switch(e->family)
    {
    case eeMOLD: case eeLANM: case eeMANHAN: case eeGLEEOK: case eePATRA:
        return false;
    default:
        return true;
    }
}

// combodata->Frame and ->OriginalTile. 2.53 animates by stepping combobuf[].tile from the
// original tile in its animation table, so the frame is found by stepping the same way.
static tile_id z30_combo_otile(int id)
{
    return (combobuf[id].animflags & AF_FRESH) ? animated_combo_table2[id][1] : animated_combo_table[id][1];
}

static tile_id z30_combo_frame_tile(int id, tile_id otile, int frame)
{
    const newcombo &c = combobuf[id];
    tile_id t = otile;

    for(int f = 0; f < frame; ++f)
    {
        tile_id before = t;
        t += 1 + c.skipanim;

        if(before / TILES_PER_ROW != t / TILES_PER_ROW)
            t += TILES_PER_ROW * c.skipanimy;
    }

    return t;
}

static int z30_combo_frame(int id)
{
    const newcombo &c = combobuf[id];
    const tile_id otile = z30_combo_otile(id);

    for(int f = 0; f < zc_max(1, (int)c.frames); ++f)
        if(z30_combo_frame_tile(id, otile, f) == c.tile)
            return f;

    return 0;
}

// Game->GameOverScreen[] (upstream's SaveScreenSettings), read by game_over() in title.cpp for newer
// quests. Each game starts from upstream's defaults, which are what 2.53's game over screen draws.
enum
{
    Z30_GOS_BACKGROUND, Z30_GOS_TEXT, Z30_GOS_USETILE, Z30_GOS_CURSOR_CSET, Z30_GOS_CUR_SOUND,
    Z30_GOS_CONTINUE_COLOUR, Z30_GOS_SAVE_COLOUR, Z30_GOS_RETRY_COLOUR, Z30_GOS_CONTINUE_FLASH,
    Z30_GOS_SAVE_FLASH, Z30_GOS_RETRY_FLASH, Z30_GOS_MIDI, Z30_GOS_CUR_FLIP, Z30_GOS_DONTSAVE_COLOUR,
    Z30_GOS_SAVEQUIT_COLOUR, Z30_GOS_SAVE2_COLOUR, Z30_GOS_QUIT_COLOUR, Z30_GOS_DONTSAVE_FLASH,
    Z30_GOS_SAVEQUIT_FLASH, Z30_GOS_SAVE2_FLASH, Z30_GOS_QUIT_FLASH, Z30_GOS_EXTRA1, Z30_GOS_EXTRA2,
    Z30_GOS_EXTRA3, Z30_GOS_LAST
};

static int zasm30_gameover[Z30_GOS_LAST];
static bool zasm30_gameover_set = false;
static std::string zasm30_gameover_text[3];     // CONTINUE, SAVE, RETRY; empty for the default

static void z30_gameover_reset()
{
    for(int i = 0; i < Z30_GOS_LAST; ++i)
        zasm30_gameover[i] = 0;

    zasm30_gameover[Z30_GOS_TEXT] = QMisc.colors.msgtext;
    zasm30_gameover[Z30_GOS_USETILE] = 2;
    zasm30_gameover[Z30_GOS_CURSOR_CSET] = 1;
    zasm30_gameover[Z30_GOS_CUR_SOUND] = WAV_CHINK;
    zasm30_gameover[Z30_GOS_MIDI] = -4;     // + ZC_MIDI_COUNT - 1: the built-in game over tune
    zasm30_gameover_set = false;

    for(int i = 0; i < 3; ++i)
        zasm30_gameover_text[i].clear();
}

// As upstream's SetSaveScreenSetting: each value bounded, the three extras not writable.
static void z30_gameover_write(int index, int v)
{
    if(index < 0 || index >= Z30_GOS_EXTRA1)
        return;

    int hi = 255;

    if(index == Z30_GOS_USETILE) hi = NEWMAXTILES;
    else if(index == Z30_GOS_CURSOR_CSET) hi = 14;
    else if(index == Z30_GOS_CUR_FLIP) hi = 3;

    zasm30_gameover[index] = vbound(v, 0, hi);
    zasm30_gameover_set = true;
}

// Game->GameOverStrings[]: CONTINUE, SAVE and RETRY are the only lines this game over screen has.
static void z30_gameover_text_write(int index, long ptr)
{
    std::string s;
    ArrayH::getString(ptr / 10000, s, 73);

    if(s.size() > 31)
        s.resize(31);

    index = vbound(index, 0, 6);

    if(index < 3)
    {
        zasm30_gameover_text[index] = s;
        zasm30_gameover_set = true;
    }
}

// title.cpp's game_over: a colour or other setting, or the text for line 0..2 (NULL for the default).
bool q30_gameover_custom() { return zasm30_gameover_set; }
int q30_gameover_setting(int index) { return (index >= 0 && index < Z30_GOS_LAST) ? zasm30_gameover[index] : 0; }
const char *q30_gameover_text(int line)
{
    return (line >= 0 && line < 3 && !zasm30_gameover_text[line].empty()) ? zasm30_gameover_text[line].c_str() : NULL;
}

// Hero->ShieldJinx (upstream's shieldjinxclk): while not 0 the shield blocks nothing; it counts
// down each frame, and -1 lasts until cured. link.cpp asks q30_shield_jinxed() before blocking.
static int zasm30_shield_jinx = 0;

bool q30_shield_jinxed() { return zasm30_ffscript_version && zasm30_shield_jinx != 0; }

// LinkClass::resetflags: jinxes that count down are cured, and all of them with 'all'.
void q30_shield_jinx_reset(bool all)
{
    if(zasm30_shield_jinx > 0 || all)
        zasm30_shield_jinx = 0;
}

// Viewport->Mode: 0 keeps the viewport on the screen, which is all this engine draws; 1 (centred on
// the Hero) and 2 (moved by the script) can move it, which nothing here draws yet.
static int zasm30_viewport_mode = 0;

// ---- reading registers -------------------------------------------------------------------------

// ZC_Q30_REGLOG=n,n,...: logs the first 40 reads and writes of each register number listed, with
// the values, to see what a quest's scripts actually get (diagnostics only).
static bool z30_reglog_wanted(long arg)
{
    static std::set<long> wanted;
    static bool parsed = false;

    if(!parsed)
    {
        parsed = true;
        if(const char *s = getenv("ZC_Q30_REGLOG"))
            for(const char *p = s; *p; )
            {
                wanted.insert(strtol(p, NULL, 0));
                while(*p && *p != ',') ++p;
                if(*p) ++p;
            }
    }

    return !wanted.empty() && wanted.count(arg) != 0;
}

static void z30_reglog(const char *what, long arg, long value)
{
    static std::map<long, int> counts;

    if(z30_reglog_wanted(arg) && counts[arg]++ < 40)
        al_trace("q30 reglog: %s %ld = %ld (index %ld, refs npc %ld npcdata %ld combodata %ld idata %ld widg %ld)\n", what, arg, value,
                 Z30_INDEX, (long)ri->guyref, Z30_REF(NPCDATA), Z30_REF(COMBODATA), (long)ri->idata, Z30_REF(SUBSCREENWIDG));
}

static bool zasm30_get_register_inner(const long arg, long &ret);
static bool zasm30_set_register_inner(const long arg, const long value);

static bool zasm30_get_register(const long arg, long &ret)
{
    const bool r = zasm30_get_register_inner(arg, ret);
    z30_reglog("read", arg, ret);
    return r;
}

static bool zasm30_set_register(const long arg, const long value)
{
    z30_reglog("write", arg, value);
    return zasm30_set_register_inner(arg, value);
}

// Rotation, scale and shadow registers of the Hero, items and weapons that the sprite draw code
// (q30_triggers.cpp, through q30_sprite_values) reads: kept in the sprite's script state, in the
// same units as the npc and Sprite-> forms (degrees, percent, sprite id, pixels).
enum { Z30DRAW_ROT, Z30DRAW_SCALE, Z30DRAW_SHADOW, Z30DRAW_SHX, Z30DRAW_SHY };

static bool z30_draw_reg(long arg, sprite *&s, int &field)
{
    struct Entry { long reg; int kind; int field; };
    static const Entry table[] =
    {
        { Z30R_LINKROTATION, Z30S_HERO, Z30DRAW_ROT }, { Z30R_ITEMROTATION, Z30S_ITEM, Z30DRAW_ROT },
        { Z30R_LINKSCALE, Z30S_HERO, Z30DRAW_SCALE }, { Z30R_ITEMSCALE, Z30S_ITEM, Z30DRAW_SCALE },
        { Z30R_LWPNSCALE, Z30S_LWPN, Z30DRAW_SCALE }, { Z30R_EWPNSCALE, Z30S_EWPN, Z30DRAW_SCALE },
        { Z30R_LWPNSHADOWSPR, Z30S_LWPN, Z30DRAW_SHADOW },
        { Z30R_HEROSHADOWXOFS, Z30S_HERO, Z30DRAW_SHX }, { Z30R_HEROSHADOWYOFS, Z30S_HERO, Z30DRAW_SHY },
        { Z30R_NPCSHADOWXOFS, Z30S_NPC, Z30DRAW_SHX },
        { Z30R_ITEMSHADOWXOFS, Z30S_ITEM, Z30DRAW_SHX }, { Z30R_ITEMSHADOWYOFS, Z30S_ITEM, Z30DRAW_SHY },
        { Z30R_LWPNSHADOWXOFS, Z30S_LWPN, Z30DRAW_SHX }, { Z30R_LWPNSHADOWYOFS, Z30S_LWPN, Z30DRAW_SHY },
        { Z30R_EWPNSHADOWXOFS, Z30S_EWPN, Z30DRAW_SHX }, { Z30R_EWPNSHADOWYOFS, Z30S_EWPN, Z30DRAW_SHY },
    };

    for(size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
    {
        if(table[i].reg != arg)
            continue;

        switch(table[i].kind)
        {
        case Z30S_HERO: s = &Link; break;
        case Z30S_ITEM: s = z30_item_ref(); break;
        case Z30S_LWPN: s = z30_lwpn_ref(); break;
        case Z30S_EWPN: s = z30_ewpn_ref(); break;
        default: s = z30_npc_ref(); break;
        }

        field = table[i].field;
        return true;
    }

    return false;
}

static bool zasm30_draw_get(const long arg, long &ret)
{
    sprite *s;
    int field;

    if(!z30_draw_reg(arg, s, field))
        return false;

    Zasm30SpriteState *st = z30_state_of(s);

    if(!st)
        ret = field == Z30DRAW_SHADOW ? 0 : -10000;
    else switch(field)
    {
    case Z30DRAW_ROT: ret = st->rotation * 10000; break;
    case Z30DRAW_SCALE: ret = st->scale * 100; break;
    case Z30DRAW_SHADOW: ret = st->shadow_spr * 10000; break;
    case Z30DRAW_SHX: ret = st->shadow_xofs * 10000; break;
    default: ret = st->shadow_yofs * 10000; break;
    }

    return true;
}

static bool zasm30_draw_set(const long arg, const long value)
{
    sprite *s;
    int field;

    if(!z30_draw_reg(arg, s, field))
        return false;

    if(Zasm30SpriteState *st = z30_state_of(s))
    {
        switch(field)
        {
        case Z30DRAW_ROT: st->rotation = value / 10000; break;
        case Z30DRAW_SCALE: st->scale = value / 100; break;
        case Z30DRAW_SHADOW: st->shadow_spr = vbound(value / 10000, 0, 255); break;
        case Z30DRAW_SHX: st->shadow_xofs = value / 10000; break;
        default: st->shadow_yofs = value / 10000; break;
        }
    }

    return true;
}

static bool zasm30_get_register_inner(const long arg, long &ret)
{
    if(zasm30_sub_get(arg, ret) || zasm30_scrdata_get(arg, ret) || zasm30_draw_get(arg, ret))
        return true;

    switch(arg)
    {
    case Z30R_ENGINE_MUSIC_ACTIVE: ret = q30_engine_music_active() ? 10000 : 0; return true;
    case Z30R_MUSICUPDATECOND:     ret = q30_music_update_cond() * 10000; return true;
    case Z30R_MUSICUPDATEFLAGS:    ret = q30_music_update_flag(Z30_INDEX) ? 10000 : 0; return true;
    case Z30R_NUM_MUSICS:          ret = q30_music_count() * 10000; return true;
    case Z30R_SCREENDATA_MUSIC:    ret = q30_screen_music(currmap * MAPSCRS + currscr) * 10000; return true;
    case Z30R_MUSICDATA_ID:        ret = q30_musicdata_get(Z30_REF(MUSIC), q30md_ID); return true;
    case Z30R_MUSICDATA_IS_ACTIVE: ret = q30_musicdata_get(Z30_REF(MUSIC), q30md_IS_ACTIVE); return true;
    case Z30R_MUSICDATA_MIDI:      ret = q30_musicdata_get(Z30_REF(MUSIC), q30md_MIDI); return true;
    case Z30R_MUSICDATA_IS_ENHANCED: ret = q30_musicdata_get(Z30_REF(MUSIC), q30md_IS_ENHANCED); return true;
    case Z30R_MUSICDATA_TRACK:     ret = q30_musicdata_get(Z30_REF(MUSIC), q30md_TRACK); return true;
    case Z30R_MUSICDATA_LOOPSTART: ret = q30_musicdata_get(Z30_REF(MUSIC), q30md_LOOPSTART); return true;
    case Z30R_MUSICDATA_LOOPEND:   ret = q30_musicdata_get(Z30_REF(MUSIC), q30md_LOOPEND); return true;
    case Z30R_MUSICDATA_XFADEIN:   ret = q30_musicdata_get(Z30_REF(MUSIC), q30md_XFADEIN); return true;
    case Z30R_MUSICDATA_XFADEOUT:  ret = q30_musicdata_get(Z30_REF(MUSIC), q30md_XFADEOUT); return true;
    default: break;
    }

    if(zasm30_sprite_get(arg, ret))
        return true;

    const int slot = z30_ref_slot(arg);

    if(slot >= 0)
    {
        ret = Z30_F.refs[slot];
        return true;
    }

    switch(arg)
    {
    // Hero
    case Z30R_LINKITEMX: ret = Xwpn * 10000; return true;
    case Z30R_LINKITEMY: ret = Ywpn * 10000; return true;
    case Z30R_LINKITEMA: ret = Awpn * 10000; return true;
    case Z30R_LINKITEMB: ret = Bwpn * 10000; return true;
    case Z30R_LINKSTUN: ret = zasm30_store_get(arg, 0, 0); return true;
    case Z30R_HEROBUNNY: ret = q30_bunny_clock() * 10000; return true;
    case Z30R_LINKTILEMOD:
    case Z30R_LINKCANFLICKER: ret = zasm30_store_get(arg, 0, 0) ? 10000 : (arg == Z30R_LINKCANFLICKER ? 10000 : 0); return true;
    case Z30R_LINKDIAG: ret = get_bit(quest_rules, qr_LTTPWALK) ? 10000 : 0; return true;
    case Z30R_LINKBIGHITBOX: ret = get_bit(quest_rules, qr_LTTPCOLLISION) ? 10000 : 0; return true;
    case Z30R_LINKINVFRAME: ret = Link.getHClk() * 10000; return true;
    case Z30R_HEROSTEPRATE: ret = zasm30_store_get(arg, 0, 0) * 10000; return true;
    case Z30R_HEROLIFTEDWPN: ret = 0; return true;
    case Z30R_HEROSTANDING: ret = Link.getZ() == 0 ? 10000 : 0; return true;
    case Z30R_HERO_SCREEN: ret = currscr * 10000; return true;

    // NPC
    case Z30R_NPCFLAGS:
    {
        enemy *e = (enemy *)guys.getByUID(ri->guyref);
        ret = 0;
        if(e && Z30_INDEX >= 0 && Z30_INDEX < 32)
            ret = (e->flags & (1 << Z30_INDEX)) ? 10000 : 0;
        return true;
    }
    case Z30R_NPCINITD:
    {
        Zasm30SpriteState *st = zasm30_sprite_state_if(ri->guyref);
        ret = (st && Z30_INDEX >= 0 && Z30_INDEX < 8) ? st->initd[Z30_INDEX] : -10000;
        return true;
    }
    case Z30R_NPCIMMORTAL: ret = zasm30_store_get(arg, ri->guyref, 0) ? 10000 : 0; return true;
    case Z30R_NPCSCRIPTUID: ret = guys.getByUID(ri->guyref) ? (long)ri->guyref : 0; return true;
    case Z30R_NPCFRAME: { enemy *e = (enemy *)guys.getByUID(ri->guyref); ret = e ? e->clk * 10000 : -10000; return true; }
    case Z30R_NPCSPAWNSPR: { Zasm30SpriteState *st = zasm30_sprite_state_if(ri->guyref); ret = st ? st->spawn_spr * 10000 : 0; return true; }
    case Z30R_NPCDEATHSPR: { Zasm30SpriteState *st = zasm30_sprite_state_if(ri->guyref); ret = st ? st->death_spr * 10000 : 0; return true; }
    case Z30R_NPCSCRIPT: { Zasm30SpriteState *st = zasm30_sprite_state_if(ri->guyref); ret = st ? st->script * 10000 : -10000; return true; }
    case Z30R_NPCHITBY:
    case Z30R_NPCBEHAVIOUR:
        ret = zasm30_store_get(arg, ri->guyref, Z30_INDEX);
        return true;
    case Z30R_NPCWEAPSPRITE: ret = zasm30_store_get(arg, ri->guyref, 0); return true;
    case Z30R_NPCSCRIPTTILE: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); ret = st ? st->script_tile * 10000 : -10000; return true; }
    case Z30R_NPCSCRIPTFLIP: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); ret = st ? st->script_flip * 10000 : -10000; return true; }

    // LWeapon / EWeapon
    case Z30R_LWPNSCRIPT: { Zasm30SpriteState *st = zasm30_sprite_state_if(ri->lwpn); ret = st ? st->script * 10000 : 0; return true; }
    case Z30R_EWPNSCRIPT: { Zasm30SpriteState *st = zasm30_sprite_state_if(ri->ewpn); ret = st ? st->script * 10000 : 0; return true; }
    case Z30R_LWPNINITD:
    {
        Zasm30SpriteState *st = zasm30_sprite_state_if(ri->lwpn);
        ret = (st && Z30_INDEX >= 0 && Z30_INDEX < 8) ? st->initd[Z30_INDEX] : 0;
        return true;
    }
    case Z30R_EWPNINITD:
    {
        Zasm30SpriteState *st = zasm30_sprite_state_if(ri->ewpn);
        ret = (st && Z30_INDEX >= 0 && Z30_INDEX < 8) ? st->initd[Z30_INDEX] : 0;
        return true;
    }
    case Z30R_LWPNFLAGS:
    case Z30R_LWPNMOVEFLAGS:
    case Z30R_LWPNTIMEOUT:
    case Z30R_LWPNLIFTLEVEL:
    case Z30R_LWPNLIFTTIME:
    case Z30R_LWPNLIFTHEIGHT:
    case Z30R_LWPNUSEWEAPON:
        ret = zasm30_store_get(arg, ri->lwpn, (arg == Z30R_LWPNFLAGS || arg == Z30R_LWPNMOVEFLAGS) ? Z30_INDEX : 0);
        return true;
    case Z30R_EWPNUNBL:
        ret = zasm30_store_get(arg, ri->ewpn, 0) * 10000;
        return true;
    case Z30R_LWPNDEGANGLE:
    {
        weapon *w = (weapon *)Lwpns.getByUID(ri->lwpn);
        ret = w ? (long)(w->angle * (180.0 / PI) * 10000) : 0;
        return true;
    }
    case Z30R_EWPNDEGANGLE:
    {
        weapon *w = (weapon *)Ewpns.getByUID(ri->ewpn);
        ret = w ? (long)(w->angle * (180.0 / PI) * 10000) : 0;
        return true;
    }

    // Combo script position
    case Z30R_COMBOPOSR:
        ret = (Z30_REF(COMBOPOS) & 0xFFFF) * 10000;
        return true;
    case Z30R_COMBOLAYERR:
        ret = (Z30_REF(COMBOPOS) >> 16) * 10000;
        return true;
    case Z30R_COMBOXR:
        ret = ((Z30_REF(COMBOPOS) & 0xFFFF) % 16) * 16 * 10000;
        return true;
    case Z30R_COMBOYR:
        ret = ((Z30_REF(COMBOPOS) & 0xFFFF) & 0xF0) * 10000;
        return true;

    // combodata->
    case Z30R_COMBODATAID: ret = Z30_REF(COMBODATA) * 10000; return true;
    case Z30R_COMBODTILE: ret = z30_cdata().tile * 10000; return true;
    case Z30R_COMBODFLIP: ret = z30_cdata().flip * 10000; return true;
    case Z30R_COMBODWALK: ret = (z30_cdata().walk & 0x0F) * 10000; return true;
    case Z30R_COMBODTYPE: ret = z30_cdata().type * 10000; return true;
    case Z30R_COMBODCSET: ret = (z30_cdata().csets & 0x0F) * 10000; return true;
    case Z30R_COMBODFLAG: ret = z30_cdata().flag * 10000; return true;
    case Z30R_COMBODFRAMES: ret = z30_cdata().frames * 10000; return true;
    case Z30R_COMBODASPEED: ret = z30_cdata().speed * 10000; return true;
    case Z30R_COMBODNEXTD: ret = z30_cdata().nextcombo * 10000; return true;
    case Z30R_COMBODNEXTC: ret = z30_cdata().nextcset * 10000; return true;
    case Z30R_COMBODSKIPANIM: ret = z30_cdata().skipanim * 10000; return true;
    case Z30R_COMBODNEXTTIMER: ret = z30_cdata().nexttimer * 10000; return true;
    case Z30R_COMBODAKIMANIMY: ret = z30_cdata().skipanimy * 10000; return true;
    case Z30R_COMBODANIMFLAGS: ret = z30_cdata().animflags * 10000; return true;
    case Z30R_COMBODATTRIBUTES:
        ret = z30_combo_attr_get(Z30_REF(COMBODATA), Z30_INDEX);
        return true;
    case Z30R_COMBODATTRIBYTES:
        ret = long(z30_combo_attr_get(Z30_REF(COMBODATA), 8 + Z30_INDEX) / 10000) * 10000;
        return true;
    case Z30R_COMBODUSRFLAGARR:
    {
        const Q30Combo *c = q30_combo(Z30_REF(COMBODATA));
        ret = (c && Z30_INDEX >= 0 && Z30_INDEX < 16) ? ((c->usrflags & (1 << Z30_INDEX)) ? 10000 : 0) : 0;
        return true;
    }
    case Z30R_COMBODATAINITD:
    {
        const Q30ScriptRef *r = q30_combo_script(Z30_REF(COMBODATA));
        ret = (r && Z30_INDEX >= 0 && Z30_INDEX < 8) ? r->initd[Z30_INDEX] : 0;
        return true;
    }
    case Z30R_COMBODATASCRIPT:
    {
        const Q30ScriptRef *r = q30_combo_script(Z30_REF(COMBODATA));
        ret = r ? r->script * 10000 : 0;
        return true;
    }
    case Z30R_COMBODTRIGGERGSTATE:
    case Z30R_COMBODTRIGGERCHANGECMB:
    {
        const Q30Combo *c = q30_combo(Z30_REF(COMBODATA));
        ret = -10000;
        if(c && !c->triggers.empty())
            ret = (arg == Z30R_COMBODTRIGGERGSTATE ? (long)c->triggers[0].trig_gstate : c->triggers[0].trigchange) * 10000;
        return true;
    }

    // Screen->
    case Z30R_SCREENDATAITEM: ret = tmpscr->hasitem ? tmpscr->item * 10000 : -10000; return true;
    case Z30R_SCREENDATAHASITEM: ret = tmpscr->hasitem * 10000; return true;
    case Z30R_SCREENDATASTRING: ret = tmpscr->str * 10000; return true;
    case Z30R_SCREENDATAENEMY: ret = (Z30_INDEX >= 0 && Z30_INDEX < 10) ? tmpscr->enemy[Z30_INDEX] * 10000 : 0; return true;
    case Z30R_SCREENDATASECRETCOMBO: ret = (Z30_INDEX >= 0 && Z30_INDEX < 128) ? tmpscr->secretcombo[Z30_INDEX] * 10000 : 0; return true;
    case Z30R_SCREENDATASECRETCSET: ret = (Z30_INDEX >= 0 && Z30_INDEX < 128) ? tmpscr->secretcset[Z30_INDEX] * 10000 : 0; return true;
    case Z30R_SCREENDATASECRETFLAG: ret = (Z30_INDEX >= 0 && Z30_INDEX < 128) ? tmpscr->secretflag[Z30_INDEX] * 10000 : 0; return true;
    case Z30R_SCREENDATAWARPRETX: ret = (Z30_INDEX >= 0 && Z30_INDEX < 4) ? tmpscr->warpreturnx[Z30_INDEX] * 10000 : 0; return true;
    case Z30R_SCREENDATAWARPRETY: ret = (Z30_INDEX >= 0 && Z30_INDEX < 4) ? tmpscr->warpreturny[Z30_INDEX] * 10000 : 0; return true;
    case Z30R_SCREENDATATWARPRETSQR: ret = ((tmpscr->warpreturnc >> (Z30_INDEX * 2)) & 3) * 10000; return true;
    case Z30R_SCREENDATASWARPRETSQR: ret = ((tmpscr->warpreturnc >> (8 + Z30_INDEX * 2)) & 3) * 10000; return true;
    case Z30R_SCREENDATALAYERINVIS: ret = (zasm30_store_get(arg, currmap * MAPSCRS + currscr, 0) & (1 << Z30_INDEX)) ? 10000 : 0; return true;
    case Z30R_SCREEN_FLAG:
    {
        long n = Z30_INDEX;
        ret = (n >= 0 && n < 88) ? (((&tmpscr->flags)[n / 8] & (1 << (n % 8))) ? 10000 : 0) : 0;
        return true;
    }
    case Z30R_SCREENEXSTATED:
    {
        extern bool q30_get_exstate(int, int);
        ret = q30_get_exstate(currmap * MAPSCRS + currscr, Z30_INDEX) ? 10000 : 0;
        return true;
    }
    case Z30R_SCREENDATAEXDOOR:
    {
        extern bool q30_get_xdoor(int, int, int);
        int dir = SH::read_stack(SPR + 1) / 10000;
        int ind = SH::read_stack(SPR) / 10000;
        ret = (unsigned(dir) <= 3 && unsigned(ind) <= 7 && q30_get_xdoor(currmap * MAPSCRS + currscr, dir, ind)) ? 10000 : 0;
        return true;
    }
    case Z30R_SCREENINITD:
    {
        ret = 0;
        const int index = currmap * MAPSCRS + currscr;
        if(Z30_INDEX >= 0 && Z30_INDEX < 8)
        {
            std::map<int, Zasm30ScreenScript>::const_iterator it = zasm30_screen_scripts.find(index);
            if(it != zasm30_screen_scripts.end())
                ret = it->second.initd[Z30_INDEX];
        }
        return true;
    }
    case Z30R_SCREEN_DRAW_ORIGIN:
        ret = zasm30_store_get(arg, 0, 0);
        return true;
    case Z30R_SCREEN_DRAW_ORIGIN_TARGET:
        ret = zasm30_store_get(arg, 0, 0);
        return true;
    case Z30R_SHOWNMSG:
        ret = (msgstr >= 0 ? msgstr : 0) * 10000;
        return true;
    case Z30R_PUSHBLOCKLAYER:
        ret = zasm30_store_get(arg, 0, 0) * 10000;
        return true;

    // mapdata->
    // Game->LoadMapData(map, screen), as upstream's loadMapData: the screen's index + 1.
    case Z30R_LOADMAPDATA:
    {
        const long map = ri->d[0] / 10000, scr = ri->d[1] / 10000;
        ret = 0;
        if(map < 1 || map > (long)(TheMaps.size() / MAPSCRS))
            Z_scripterrlog("Invalid Map ID passed to Game->LoadMapData: %ld\n", map);
        else if(scr < 0 || scr > 129)
            Z_scripterrlog("Invalid Screen Index passed to Game->LoadMapData: %ld\n", scr);
        else
            ret = (map - 1) * MAPSCRS + scr + 1;
        Z30_REF(MAPDATA) = ret;
        return true;
    }
    case Z30R_MAPDATACOMBODD:
    case Z30R_MAPDATACOMBOCD:
    case Z30R_MAPDATACOMBOFD:
    case Z30R_MAPDATACOMBOTD:
    case Z30R_MAPDATACOMBOID:
    {
        mapscr *m = z30_mapdata(Z30_REF(MAPDATA));
        long n = Z30_INDEX;
        ret = -10000;

        if(m && n >= 0 && n < 176)
        {
            switch(arg)
            {
            case Z30R_MAPDATACOMBODD: ret = m->data[n] * 10000; break;
            case Z30R_MAPDATACOMBOCD: ret = m->cset[n] * 10000; break;
            case Z30R_MAPDATACOMBOFD: ret = m->sflag[n] * 10000; break;
            case Z30R_MAPDATACOMBOTD: ret = combobuf[m->data[n]].type * 10000; break;
            case Z30R_MAPDATACOMBOID: ret = combobuf[m->data[n]].flag * 10000; break;
            }
        }
        return true;
    }
    case Z30R_MAPDATAWARPRETX:
    case Z30R_MAPDATAWARPRETY:
    {
        mapscr *m = z30_mapdata(Z30_REF(MAPDATA));
        long n = Z30_INDEX;
        ret = 0;
        if(m && n >= 0 && n < 4)
            ret = (arg == Z30R_MAPDATAWARPRETX ? m->warpreturnx[n] : m->warpreturny[n]) * 10000;
        return true;
    }

    // dmapdata->
    case Z30R_DMAPDATAID: ret = Z30_REF(DMAPDATA) * 10000; return true;
    case Z30R_DMAPDATACONTINUE: ret = (unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS ? DMaps[Z30_REF(DMAPDATA)].cont : 0) * 10000; return true;
    case Z30R_DMAPDATAOFFSET: ret = (unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS ? DMaps[Z30_REF(DMAPDATA)].xoff : 0) * 10000; return true;
    case Z30R_DMAPDATASIDEVIEW: ret = zasm30_store_get(arg, Z30_REF(DMAPDATA), 0) ? 10000 : 0; return true;
    case Z30R_DMAPDATAMAP: ret = (unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS ? DMaps[Z30_REF(DMAPDATA)].map + 1 : 0) * 10000; return true;
    case Z30R_DMAPDATALEVEL: ret = (unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS ? DMaps[Z30_REF(DMAPDATA)].level : 0) * 10000; return true;
    case Z30R_DMAPDATACOMPASS: ret = (unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS ? DMaps[Z30_REF(DMAPDATA)].compass : 0) * 10000; return true;
    case Z30R_DMAPDATATYPE: ret = (unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS ? (DMaps[Z30_REF(DMAPDATA)].type & dmfTYPE) : 0) * 10000; return true;
    case Z30R_DMAPDATAPALETTE: ret = (unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS ? DMaps[Z30_REF(DMAPDATA)].color : 0) * 10000; return true;
    case Z30R_DMAPDATAFLAGARR:
    {
        const long id = Z30_REF(DMAPDATA);
        ret = (unsigned(id) < MAXDMAPS && (DMaps[id].flags & (1 << Z30_INDEX))) ? 10000 : 0;
        return true;
    }
    case Z30R_DMAPINITD:
    {
        const Q30ScriptRef *r = q30_dmap_script(Z30_REF(DMAPDATA), q30_dmap_active);
        ret = (r && Z30_INDEX >= 0 && Z30_INDEX < 8) ? r->initd[Z30_INDEX] : -10000;
        return true;
    }

    // itemdata-> (ri->idata)
    case Z30R_IDATAID: ret = ri->idata * 10000; return true;
    case Z30R_IDATATILE: ret = itemsbuf[ri->idata].tile * 10000; return true;
    case Z30R_IDATACSET: ret = (itemsbuf[ri->idata].csets & 15) * 10000; return true;
    case Z30R_IDATABUNNYABLE: ret = q30_item_bunny(ri->idata) ? 10000 : 0; return true;
    case Z30R_IDATASCRIPT: ret = itemsbuf[ri->idata].script * 10000; return true;
    case Z30R_IDATASPRITE: ret = zasm30_store_get(arg, ri->idata, Z30_INDEX) * 10000; return true;

    // spritedata->
    case Z30R_SPRITEDATATILE: ret = (unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS ? wpnsbuf[Z30_REF(SPRITEDATA)].tile : 0) * 10000; return true;
    case Z30R_SPRITEDATACSETS: ret = (unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS ? (wpnsbuf[Z30_REF(SPRITEDATA)].csets & 0xF) : 0) * 10000; return true;
    case Z30R_SPRITEDATAFLCSET: ret = (unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS ? ((wpnsbuf[Z30_REF(SPRITEDATA)].csets & 0xF0) >> 4) : 0) * 10000; return true;
    case Z30R_SPRITEDATAFRAMES: ret = (unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS ? wpnsbuf[Z30_REF(SPRITEDATA)].frames : 0) * 10000; return true;
    case Z30R_SPRITEDATASPEED: ret = (unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS ? wpnsbuf[Z30_REF(SPRITEDATA)].speed : 0) * 10000; return true;
    case Z30R_SPRITEDATATYPE: ret = (unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS ? wpnsbuf[Z30_REF(SPRITEDATA)].misc & 3 : 0) * 10000; return true;
    case Z30R_SPRITEDATAMISC: ret = (unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS ? wpnsbuf[Z30_REF(SPRITEDATA)].misc : 0) * 10000; return true;
    case Z30R_SPRITEDATAID: ret = Z30_REF(SPRITEDATA) * 10000; return true;

    // npcdata->
    case Z30R_NPCDATATILE: ret = (unsigned(Z30_REF(NPCDATA)) < MAXGUYS ? guysbuf[Z30_REF(NPCDATA)].tile : 0) * 10000; return true;
    case Z30R_NPCDATADEATHSFX: ret = (unsigned(Z30_REF(NPCDATA)) < MAXGUYS ? guysbuf[Z30_REF(NPCDATA)].deadsfx : 0) * 10000; return true;
    case Z30R_NPCDATAHITSFX: ret = (unsigned(Z30_REF(NPCDATA)) < MAXGUYS ? guysbuf[Z30_REF(NPCDATA)].hitsfx : 0) * 10000; return true;
    case Z30R_NPCDATAANIM: ret = (unsigned(Z30_REF(NPCDATA)) < MAXGUYS ? guysbuf[Z30_REF(NPCDATA)].anim : 0) * 10000; return true;
    case Z30R_NPCDATAEANIM: ret = (unsigned(Z30_REF(NPCDATA)) < MAXGUYS ? guysbuf[Z30_REF(NPCDATA)].e_anim : 0) * 10000; return true;

    // Generic scripts
    case Z30R_GENDATARUNNING:
    {
        long slot2 = Z30_REF(GENERICDATA);
        ret = (slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS && zasm30_generic[slot2].run) ? 10000 : 0;
        return true;
    }
    case Z30R_GENDATASIZE:
    {
        long slot2 = Z30_REF(GENERICDATA);
        ret = (slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS) ? (long)zasm30_generic[slot2].data.size() * 10000 : 0;
        return true;
    }
    case Z30R_GENDATADATA:
    {
        long slot2 = Z30_REF(GENERICDATA), n = Z30_INDEX;
        ret = 0;
        if(slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS && n >= 0 && n < (long)zasm30_generic[slot2].data.size())
            ret = zasm30_generic[slot2].data[n];
        return true;
    }
    case Z30R_GENDATAINITD:
    {
        long slot2 = Z30_REF(GENERICDATA), n = Z30_INDEX;
        ret = (slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS && n >= 0 && n < 8) ? zasm30_generic[slot2].initd[n] : 0;
        return true;
    }
    case Z30R_GENDATAEVENTSTATE:
    {
        long slot2 = Z30_REF(GENERICDATA), n = Z30_INDEX;
        ret = (slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS && n >= 0 && n < 32 && (zasm30_generic[slot2].event_state & (1 << n))) ? 10000 : 0;
        return true;
    }
    case Z30R_GENDATAEXITSTATE:
    case Z30R_GENDATARELOADSTATE:
    {
        long slot2 = Z30_REF(GENERICDATA), n = Z30_INDEX;
        ret = 0;
        if(slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS && n >= 0 && n < 16)
        {
            word v = arg == Z30R_GENDATAEXITSTATE ? zasm30_generic[slot2].exit_state : zasm30_generic[slot2].reload_state;
            ret = (v & (1 << n)) ? 10000 : 0;
        }
        return true;
    }

    // Game, input, misc
    case Z30R_GAMEMISC: ret = game->get_maxcounter(Z30_INDEX & 0x1F); return true;
    case Z30R_READKEY: ret = (Z30_INDEX >= 0 && Z30_INDEX < 127 && key[Z30_INDEX]) ? 10000 : 0; return true;
    case Z30R_KEYMODIFIERS: ret = key_shifts * 10000; return true;
    case Z30R_BUTTONPRESS:
    case Z30R_BUTTONINPUT:
    {
        int n = vbound(Z30_INDEX, 0, 18 - 1);
        ret = (arg == Z30R_BUTTONPRESS ? button_press[n] : control_state[n]) ? 10000 : 0;
        return true;
    }
    case Z30R_GAMESCROLLING: ret = zasm30_store_get(arg, 0, Z30_INDEX) * 10000; return true;
    case Z30R_GAMESUSPEND: ret = zasm30_store_get(arg, 0, Z30_INDEX) * 10000; return true;
    case Z30R_GAMEEVENTDATA: ret = zasm30_store_get(arg, 0, Z30_INDEX); return true;
    case Z30R_GAMEBOTTLEST: ret = q30_bottle_slot(Z30_INDEX) * 10000; return true;   // the save's bottle slots (q30_triggers.cpp)
    case Z30R_MUSICUPDATECOND: ret = zasm30_store_get(arg, 0, 0) * 10000; return true;
    case Z30R_GAMEASUBYOFF: ret = zasm30_store_get(arg, 0, 0) * 10000; return true;
    case Z30R_FFRULE: ret = (Z30_INDEX >= 0 && Z30_INDEX < QUESTRULES_SIZE * 8) ? (get_bit(quest_rules, Z30_INDEX) ? 10000 : 0) : 0; return true;
    case Z30R_FFCID: ret = (ri->ffcref + 1) * 10000; return true;
    case Z30R_FFCLAYER: ret = zasm30_store_get(arg, ri->ffcref, 0) * 10000; return true;
    case Z30R_MAX_FFC_ID: ret = MAXFFCS * 10000; return true;
    case Z30R_DISTANCE:
    {
        double x = Z30_D(6) / 10000.0 - Z30_D(1) / 10000.0;
        double y = Z30_D(0) / 10000.0 - Z30_D(2) / 10000.0;
        ret = (long)(sqrt((x * x + y * y) * 1000000.0) * 10);
        return true;
    }
    case Z30R_DISTANCESCALE:
    {
        double x = Z30_D(6) / 10000.0 - Z30_D(1) / 10000.0;
        double y = Z30_D(0) / 10000.0 - Z30_D(2) / 10000.0;
        double scale = Z30_D(7) / 10000.0;

        if(!scale)
            scale = 10000;

        ret = (long)(sqrt(x * x + y * y) * scale * 10000 / 10000);
        return true;
    }

    // Region and viewport: a 2.53 engine has neither, so the whole screen is one region.
    case Z30R_REGION_WIDTH: ret = 256 * 10000; return true;
    case Z30R_REGION_HEIGHT: ret = 176 * 10000; return true;
    case Z30R_REGION_ID: ret = 0; return true;
    case Z30R_REGION_SCREEN_WIDTH: ret = 10000; return true;
    case Z30R_REGION_SCREEN_HEIGHT: ret = 10000; return true;
    case Z30R_REGION_NUM_COMBOS: ret = 176 * 10000; return true;
    case Z30R_REGION_ORIGIN_SCREEN: ret = currscr * 10000; return true;
    // The viewport is the screen (upstream's 256x176 outside extended height mode). Only a
    // script-moved viewport (mode 2) keeps the position a script gives it.
    case Z30R_VIEWPORT_X:
    case Z30R_VIEWPORT_Y: ret = zasm30_viewport_mode == 2 ? zasm30_store_get(arg, 0, 0) : 0; return true;
    case Z30R_VIEWPORT_WIDTH: ret = 256 * 10000; return true;
    case Z30R_VIEWPORT_HEIGHT: ret = zasm30_store_get(arg, 0, 0) ? zasm30_store_get(arg, 0, 0) : 176 * 10000; return true;
    case Z30R_VIEWPORT_MODE: ret = zasm30_viewport_mode; return true;
    case Z30R_HEROSHIELDJINX: ret = zasm30_shield_jinx * 10000; return true;

    // Game->MaxLWeapons(): 2.53's sprite lists hold 255 each.
    case Z30R_SPRITEMAXLWPN: ret = 255 * 10000; return true;

    // ->LightRadius and ->LightShape of an npc, weapon or item (also Sprite->LightRadius): what
    // a script gave it, drawn in new dark rooms by q30_script_lights.
    case Z30R_NPCGLOWRAD: case Z30R_LWPNGLOWRAD: case Z30R_EWPNGLOWRAD: case Z30R_ITEMGLOWRAD:
    case Z30R_NPCGLOWSHP: case Z30R_LWPNGLOWSHP: case Z30R_EWPNGLOWSHP: case Z30R_ITEMGLOWSHP:
    {
        sprite *s = (arg == Z30R_NPCGLOWRAD || arg == Z30R_NPCGLOWSHP) ? (sprite *)z30_npc_ref()
                  : (arg == Z30R_LWPNGLOWRAD || arg == Z30R_LWPNGLOWSHP) ? (sprite *)z30_lwpn_ref()
                  : (arg == Z30R_EWPNGLOWRAD || arg == Z30R_EWPNGLOWSHP) ? (sprite *)z30_ewpn_ref() : (sprite *)z30_item_ref();
        Zasm30SpriteState *st = z30_state_of(s);
        const bool shape = arg >= Z30R_ITEMGLOWSHP;
        ret = st ? (shape ? st->glow_shape : st->glow_rad) * 10000 : -10000;
        return true;
    }

    // combodata->Frame, ->OriginalTile
    case Z30R_COMBODFRAME:
    {
        const long id = Z30_REF(COMBODATA);
        ret = (id >= 0 && id < MAXCOMBOS) ? z30_combo_frame(id) * 10000 : 0;
        return true;
    }
    case Z30R_COMBODOTILE:
    {
        const long id = Z30_REF(COMBODATA);
        ret = (id >= 0 && id < MAXCOMBOS) ? (long)z30_combo_otile(id) * 10000 : 0;
        return true;
    }

    // itemdata->SizeFlags, npcdata->WeaponSprite, npc->Core
    case Z30R_IDATAOVERRIDEFL: ret = z30_map_get(zasm30_item_override_flags, ri->idata) * 10000; return true;
    case Z30R_NPCDATAWPNSPRITE:
        ret = unsigned(Z30_REF(NPCDATA)) < MAXGUYS ? z30_map_get(zasm30_guy_wpnsprite, Z30_REF(NPCDATA)) * 10000 : -10000;
        return true;
    case Z30R_NPCISCORE:
    {
        enemy *e = z30_npc_ref();
        if(!e)
            ret = -10000;
        else
        {
            const long set = zasm30_store_get(arg, ri->guyref, 0);     // 0 unset, 1 false, 2 true
            ret = (set ? set == 2 : z30_npc_is_core(e)) ? 10000 : 0;
        }
        return true;
    }

    // Screen->EntryX/Y: kept per screen, cleared when the Hero arrives on it, as upstream's
    // copy of the screen is reloaded then.
    case Z30R_SCREENDATAENTRYX:
    case Z30R_SCREENDATAENTRYY:
        ret = zasm30_store_get(arg, currmap * MAPSCRS + currscr, 0) * 10000;
        return true;
    case Z30R_SCREENDATAOCEANSFX: ret = tmpscr->oceansfx * 10000; return true;

    // mapdata->LayerMap[], ->LayerScreen[] (1-based from script format 11), ->D[]
    case Z30R_MAPDATALAYERMAP:
    case Z30R_MAPDATALAYERSCREEN:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX - (zasm30_ffscript_version < 11 ? 0 : 1);
        ret = -10000;
        if(m && n >= 0 && n < 6)
            ret = (arg == Z30R_MAPDATALAYERMAP ? m->layermap[n] : m->layerscreen[n]) * 10000;
        return true;
    }
    case Z30R_MAPDATAMISCD:
    {
        const int mi = z30_mapdata_mi(Z30_REF(MAPDATA));
        const long n = Z30_INDEX;
        ret = -10000;
        if(mi >= 0 && mi < MAXDMAPS * MAPSCRSNORMAL && n >= 0 && n < 8)
            ret = game->screen_d[mi][n] * 10000;
        return true;
    }

    // Hero
    case Z30R_HEROFALLCLK:
    case Z30R_NPCFALLCLK:
        ret = 0;                // pits act at once in this engine: nothing is ever mid-fall
        return true;
    case Z30R_LINKCLIMBING:
        ret = 0;                // this engine has no ladder climbing state on the Hero
        return true;
    case Z30R_LINKHITBY:
    {
        const long n = Z30_INDEX;
        ret = 0;
        if(n >= 0 && n < Z30_HITBY_COUNT)
            ret = z30_hitby_is_uid(n) ? zasm30_hitby[n] : zasm30_hitby[n] * 10000;
        return true;
    }
    case Z30R_LINKPUSH: ret = Link.getPushing() * 10000; return true;
    case Z30R_LINKCSET: ret = Link.cs * 10000; return true;
    case Z30R_LINKEATEN: ret = Link.getEaten() * 10000; return true;
    case Z30R_LINKGRABBED: ret = Link.getInWallm() ? 10000 : 0; return true;
    case Z30R_HEROISWARPING: ret = Link.ffwarp ? 10000 : 0; return true;
    case Z30R_HEROIMMORTAL: ret = zasm30_hero_immortal * 10000; return true;
    case Z30R_LINKHURTSFX: ret = (zasm30_hero_hurt_sfx > 0 ? zasm30_hero_hurt_sfx : WAV_OUCH) * 10000; return true;
    case Z30R_LINKSCRIPTTILE: ret = zasm30_hero_script_tile * 10000; return true;
    case Z30R_LINKSCRIPFLIP: ret = zasm30_hero_script_flip * 10000; return true;
    case Z30R_HEROSCRIPTCSET: ret = zasm30_hero_script_cset * 10000; return true;
    case Z30R_HEROMOVEFLAGS: ret = z30_flag_get(zasm30_hero_move_flags, Z30_INDEX, 11) ? 10000 : 0; return true;
    case Z30R_HEROFAKEZ:
    {
        Zasm30SpriteState *st = zasm30_sprite_state_if(Link.getUID());
        ret = st ? st->fakez : 0;
        return true;
    }
    case Z30R_HEROFAKEJUMP:
    {
        Zasm30SpriteState *st = zasm30_sprite_state_if(Link.getUID());
        ret = st ? z30_fake_jump_read(st->fakefall) : 0;
        return true;
    }

    // Screen and game state
    case Z30R_SCREENSECRETSTRIGGERED: ret = q30_secrets_triggered() ? 10000 : 0; return true;
    case Z30R_SKIPF6: ret = get_bit(quest_rules, qr_NOCONTINUE) ? 10000 : 0; return true;
    case Z30R_NOACTIVESUBSC: ret = zasm30_no_active_subscreen ? 10000 : 0; return true;
    case Z30R_DEBUGTESTING: ret = 0; return true;   // only true when testing from the editor
    case Z30R_GAMELSWITCH: ret = (Z30_INDEX >= 0 && Z30_INDEX < MAXLEVELS) ? (long)q30_level_state(Z30_INDEX) : 0; return true;
    case Z30R_GAMEGSWITCH: ret = (Z30_INDEX >= 0 && Z30_INDEX < 256) ? q30_global_state(Z30_INDEX) * 10000 : 0; return true;
    case Z30R_GAMETRIGGROUPS: ret = (Z30_INDEX >= 0 && Z30_INDEX < 256) ? q30_trigger_group_count(Z30_INDEX) * 10000 : 0; return true;

    // Input
    case Z30R_KEYPRESS:
    case Z30R_KEYINPUT:
    {
        const long n = Z30_INDEX;
        ret = 0;
        if(n >= 0 && n < 127 && !zasm30_key_disabled[n])
            ret = (arg == Z30R_KEYPRESS ? zasm30_key_press[n] : zasm30_key_input[n]) ? 10000 : 0;
        return true;
    }
    case Z30R_DISABLEKEY: ret = (Z30_INDEX >= 0 && Z30_INDEX < 127 && zasm30_key_disabled[Z30_INDEX]) ? 10000 : 0; return true;
    case Z30R_DISABLEBUTTON: ret = (Z30_INDEX >= 0 && Z30_INDEX < 18 && zasm30_button_disabled[Z30_INDEX]) ? 10000 : 0; return true;
    case Z30R_MOUSEARR:
        switch(Z30_INDEX)
        {
        case 0: ret = get_register(INPUTMOUSEX); break;
        case 1: ret = get_register(INPUTMOUSEY); break;
        case 2: ret = get_register(INPUTMOUSEZ); break;
        case 3: ret = (gui_mouse_b() & 1) ? 10000 : 0; break;
        case 4: ret = (gui_mouse_b() & 2) ? 10000 : 0; break;
        case 5: ret = (gui_mouse_b() & 4) ? 10000 : 0; break;
        default: ret = 0; break;
        }
        return true;

    // messagedata->
    case Z30R_MESSAGEDATATEXTLEN:
        ret = BC::checkMessage(Z30_REF(MSGDATA), "messagedata->Length") == SH::_NoError ? (long)strlen(MsgStrings[Z30_REF(MSGDATA)].s) * 10000 : -10000;
        return true;

    // combodata->
    case Z30R_COMBODEFFECT:
    {
        const Q30Combo *c = q30_combo(Z30_REF(COMBODATA));
        ret = (Z30_REF(COMBODATA) >= 0 && Z30_REF(COMBODATA) < MAXCOMBOS) ? ((c ? c->walk : z30_cdata().walk) >> 4) * 10000 : -10000;
        return true;
    }
    case Z30R_COMBODATTRISHORTS:
        ret = long(z30_combo_attr_get(Z30_REF(COMBODATA), 16 + Z30_INDEX) / 10000) * 10000;
        return true;
    case Z30R_COMBODTRIGSPAWNITEM:
    {
        const Q30Combo *c = q30_combo(Z30_REF(COMBODATA));
        ret = (c && !c->triggers.empty()) ? c->triggers[0].spawnitem * 10000 : -10000;
        return true;
    }
    case Z30R_COMBODTRIGGERFLAGS2:
    {
        const Q30Combo *c = q30_combo(Z30_REF(COMBODATA));
        const long n = Z30_INDEX;
        ret = 0;
        if(c && n >= 0 && n < 32 * 6)
        {
            if(n == Q30TF_ONLYGENTRIG) ret = c->only_gentrig ? 10000 : 0;
            else if(!c->triggers.empty()) ret = c->triggers[0].flags.get(n) ? 10000 : 0;
        }
        return true;
    }

    // mapdata->
    case Z30R_MAPDATACOMBOSD:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        ret = -10000;
        if(m && n >= 0 && n < (long)m->data.size() && m->data[n] < MAXCOMBOS)
            ret = (combobuf[m->data[n]].walk & 0xF) * 10000;
        return true;
    }
    case Z30R_MAPDATAFFSCRIPT:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        ret = (m && n >= 0 && n < Z30_FFC_COUNT) ? m->ffscript[n] * 10000 : -10000;
        return true;
    }
    case Z30R_MAPDATAFFX:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        ret = (m && n >= 0 && n < Z30_FFC_COUNT) ? m->ffx[n] : -10000;
        return true;
    }
    case Z30R_MAPDATAFFY:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        ret = (m && n >= 0 && n < Z30_FFC_COUNT) ? m->ffy[n] : -10000;
        return true;
    }
    case Z30R_MAPDATAENEMY:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        ret = (m && n >= 0 && n < 10) ? m->enemy[n] * 10000 : -10000;
        return true;
    }
    case Z30R_MAPDATASIDEWARPDMAP:
    case Z30R_MAPDATASIDEWARPSC:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        ret = -10000;
        if(m && n >= 0 && n < 4)
            ret = (arg == Z30R_MAPDATASIDEWARPDMAP ? m->sidewarpdmap[n] : m->sidewarpscr[n]) * 10000;
        return true;
    }
    case Z30R_MAPDATASIDEWARPID:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        ret = -10000;
        if(m && n >= 0 && n < 4)
            ret = ((m->flags2 >> n) & 1) ? ((m->sidewarpindex >> (2 * n)) & 3) * 10000 : -10000;
        return true;
    }
    case Z30R_MAPDATABOSSSFX:
    case Z30R_MAPDATAOCEANSFX:
    {
        mapscr *m = z30_mapdata_now();
        ret = m ? (arg == Z30R_MAPDATABOSSSFX ? m->bosssfx : m->oceansfx) * 10000 : -10000;
        return true;
    }
    case Z30R_MAPDATACATCHALL: { mapscr *m = z30_mapdata_now(); ret = m ? m->catchall * 10000 : -10000; return true; }
    case Z30R_MAPDATACOLOUR: { mapscr *m = z30_mapdata_now(); ret = m ? m->color * 10000 : -10000; return true; }
    case Z30R_MAPDATASTRING: { mapscr *m = z30_mapdata_now(); ret = m ? m->str * 10000 : -10000; return true; }
    case Z30R_MAPDATAROOM: { mapscr *m = z30_mapdata_now(); ret = m ? m->room * 10000 : -10000; return true; }
    case Z30R_MAPDATAITEM: { mapscr *m = z30_mapdata_now(); ret = (m && m->hasitem) ? m->item * 10000 : -10000; return true; }
    case Z30R_MAPDATAFLAGS:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        ret = (m && n >= 0 && n < 10) ? (&m->flags)[n] * 10000 : -10000;
        return true;
    }
    case Z30R_MAPDATALAYEROPACITY:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX - 1;
        ret = (m && n >= 0 && n < 6) ? m->layeropacity[n] * 10000 : -10000;
        return true;
    }
    case Z30R_MAPDATAGUY:
    {
        const int mi = z30_mapdata_mi(Z30_REF(MAPDATA));
        ret = mi >= 0 ? game->guys[mi] * 10000 : -10000;
        return true;
    }
    case Z30R_MAPDATASCREENEFLAGSD:
    {
        mapscr *m = z30_mapdata_now();
        const long n = vbound(Z30_INDEX, 0, 2);
        ret = m ? get_screeneflags(m, n) : -10000;
        return true;
    }
    // mapdata->GetFFCInitD(ffc, d): the ffc's run arguments as the screen holds them
    case Z30R_MAPDATAINTID:
    {
        mapscr *m = z30_mapdata_now();
        const long f = ri->d[0] / 10000, d = ri->d[1] / 10000;
        ret = (m && f >= 0 && f < Z30_FFC_COUNT && d >= 0 && d < 8) ? m->initd[f][d] : -10000;
        return true;
    }
    case Z30R_MAPDATAINITDARRAY:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        ret = 0;
        if(m && n >= 0 && n < 8)
        {
            const long ref = Z30_REF(MAPDATA);
            const int index = (ref & Z30_MAPDATA_TMP) ? currmap * MAPSCRS + currscr : ref - 1;
            std::map<int, Zasm30ScreenScript>::const_iterator it = zasm30_screen_scripts.find(index);
            if(it != zasm30_screen_scripts.end())
                ret = it->second.initd[n];
        }
        return true;
    }

    // Screen-> extras
    case Z30R_SCREENDATALAYERMAP:
    {
        const long n = Z30_INDEX - 1;
        ret = (n >= 0 && n < 6) ? tmpscr->layermap[n] * 10000 : -10000;
        return true;
    }
    case Z30R_SCREENDATALAYERSCREEN:
    {
        const long n = Z30_INDEX - 1;
        ret = (n >= 0 && n < 6) ? tmpscr->layerscreen[n] * 10000 : -10000;
        return true;
    }
    case Z30R_SCREENDATASECRETSFX: ret = tmpscr->secretsfx * 10000; return true;
    case Z30R_CLOCKACTIVE: ret = watch ? 10000 : 0; return true;
    case Z30R_LWPNFALLCLK: ret = 0; return true;     // weapons never fall here either
    case Z30R_SCREENSIDEWARPID:
    {
        const long n = Z30_INDEX;
        ret = -10000;
        if(n >= 0 && n < 4)
            ret = ((tmpscr->flags2 >> n) & 1) ? ((tmpscr->sidewarpindex >> (2 * n)) & 3) * 10000 : -10000;
        return true;
    }

    // dmapdata->
    case Z30R_DMAPLEVELPAL: ret = (Z30_INDEX >= 0 && Z30_INDEX < MAXDMAPS) ? DMaps[Z30_INDEX].color * 10000 : -10000; return true;
    case Z30R_DMAPDATAMIDI:
        ret = -10000;
        if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS)
            ret = z30_old_midi_id(DMaps[Z30_REF(DMAPDATA)].midi) * 10000;
        return true;
    case Z30R_DMAPDATASUBSCRA: ret = (unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS ? DMaps[Z30_REF(DMAPDATA)].active_subscreen : 0) * 10000; return true;
    case Z30R_DMAPDATAFLAGS: ret = unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS ? DMaps[Z30_REF(DMAPDATA)].flags * 10000 : -10000; return true;
    case Z30R_DMAPDATAGRID:
    {
        const long id = Z30_REF(DMAPDATA), n = Z30_INDEX;
        ret = (unsigned(id) < MAXDMAPS && n >= 0 && n < 8) ? DMaps[id].grid[n] * 10000 : -10000;
        return true;
    }
    case Z30R_DMAPDATADISABLEDITEMS:
    {
        const long id = Z30_REF(DMAPDATA), n = Z30_INDEX;
        ret = (unsigned(id) < MAXDMAPS && n >= 0 && n < iMax) ? (DMaps[id].disableditems[n] ? 10000 : 0) : -10000;
        return true;
    }
    case Z30R_DMAPDATAMAPINITD:
    {
        const Q30ScriptRef *r = q30_dmap_script(Z30_REF(DMAPDATA), q30_dmap_onmap);
        ret = (r && Z30_INDEX >= 0 && Z30_INDEX < 8) ? r->initd[Z30_INDEX] : -10000;
        return true;
    }
    case Z30R_DMAPSCRIPT:
    case Z30R_DMAPDATAASUBSCRIPT:
    {
        const Q30ScriptRef *r = q30_dmap_script(Z30_REF(DMAPDATA), arg == Z30R_DMAPSCRIPT ? q30_dmap_active : q30_dmap_active_subscreen);
        ret = r ? r->script * 10000 : 0;
        return true;
    }

    // npc->
    case Z30R_NPCHITDIR: { enemy *e = z30_npc_ref(); ret = e ? q30_enemy_hitdir(e) * 10000 : -10000; return true; }
    case Z30R_NPCINVINC: { enemy *e = z30_npc_ref(); ret = e ? e->hclk * 10000 : -10000; return true; }
    case Z30R_NPCHALTCLK: { enemy *e = z30_npc_ref(); ret = e ? q30_enemy_clk2(e) * 10000 : -10000; return true; }
    case Z30R_NPCLINEDUP:
    {
        enemy *e = z30_npc_ref();
        ret = e ? q30_enemy_lined_up(e, Z30_INDEX, ri->d[1] != 0) * 10000 : 0;
        return true;
    }
    case Z30R_NPCFLASHINGCSET: { enemy *e = z30_npc_ref(); ret = e ? z30_flashing_cset(e) * 10000 : -10000; return true; }
    case Z30R_NPCRANDOM: { enemy *e = z30_npc_ref(); ret = e ? e->rate * 10000 : -10000; return true; }
    case Z30R_NPCMOVEFLAGS: { enemy *e = z30_npc_ref(); Zasm30SpriteState *st = e ? zasm30_npc_state(e) : NULL; ret = (st && z30_flag_get(st->move_flags, Z30_INDEX, 16)) ? 10000 : 0; return true; }
    case Z30R_NPCFAKEZ: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); ret = st ? st->fakez : -10000; return true; }
    case Z30R_NPCFAKEJUMP: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); ret = st ? z30_fake_jump_read(st->fakefall) : -10000; return true; }
    case Z30R_NPCROTATION: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); ret = st ? st->rotation * 10000 : -10000; return true; }
    case Z30R_NPCSCALE: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); ret = st ? st->scale * 100 : -10000; return true; }
    case Z30R_NPCSHADOWSPR: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); ret = st ? st->shadow_spr * 10000 : 0; return true; }
    case Z30R_NPCSHADOWYOFS: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); ret = st ? st->shadow_yofs * 10000 : -10000; return true; }
    case Z30R_NPCKNOCKBACKSPEED: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); ret = st ? st->knockback_speed * 10000 : -10000; return true; }
    case Z30R_NPCNOSLIDE: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); ret = st ? (st->no_slide ? 10000 : 0) : -10000; return true; }
    case Z30R_NPCNOSCRIPTKB: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); ret = st ? (st->no_script_kb ? 10000 : 0) : -10000; return true; }
    case Z30R_NPCSHIELD:
    {
        static const dword bits[5] = { inv_front, inv_left, inv_right, inv_back, guy_bkshield };
        enemy *e = z30_npc_ref();
        const long n = Z30_INDEX;
        ret = (e && n >= 0 && n < 5) ? ((e->flags & bits[n]) ? 10000 : 0) : -10000;
        return true;
    }

    // npcdata->
    case Z30R_NPCDATAHP: ret = unsigned(Z30_REF(NPCDATA)) < MAXGUYS ? guysbuf[Z30_REF(NPCDATA)].hp * 10000 : -10000; return true;
    case Z30R_NPCDATASTEP: ret = unsigned(Z30_REF(NPCDATA)) < MAXGUYS ? guysbuf[Z30_REF(NPCDATA)].step * 10000 : -10000; return true;
    case Z30R_NPCDATASTILE: ret = unsigned(Z30_REF(NPCDATA)) < MAXGUYS ? guysbuf[Z30_REF(NPCDATA)].s_tile * 10000 : -10000; return true;
    case Z30R_NPCDATAWEAPON: ret = unsigned(Z30_REF(NPCDATA)) < MAXGUYS ? guysbuf[Z30_REF(NPCDATA)].weapon * 10000 : -10000; return true;
    case Z30R_NPCDATAWEAPONDAMAGE: ret = unsigned(Z30_REF(NPCDATA)) < MAXGUYS ? guysbuf[Z30_REF(NPCDATA)].wdp * 10000 : -10000; return true;

    // lweapon-> and eweapon->
    case Z30R_LWPNSCRIPTTILE: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = st ? st->script_tile * 10000 : -10000; return true; }
    case Z30R_LWPNSCRIPTFLIP: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = st ? st->script_flip * 10000 : -10000; return true; }
    case Z30R_EWPNSCRIPTTILE: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? st->script_tile * 10000 : -10000; return true; }
    case Z30R_EWPNSCRIPTFLIP: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? st->script_flip * 10000 : -10000; return true; }
    case Z30R_EWPNFAKEZ: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? st->fakez : -10000; return true; }
    case Z30R_EWPNFAKEJUMP: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? z30_fake_jump_read(st->fakefall) : -10000; return true; }
    case Z30R_LWPNFAKEZ: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = st ? st->fakez : -10000; return true; }
    case Z30R_LWPNFAKEJUMP: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = st ? z30_fake_jump_read(st->fakefall) : -10000; return true; }
    case Z30R_EWPNMOVEFLAGS: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = (st && z30_flag_get(st->move_flags, Z30_INDEX, 32)) ? 10000 : 0; return true; }
    case Z30R_EWPNFLAGS: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = (st && z30_flag_get(st->weapon_flags, Z30_INDEX, 32)) ? 10000 : 0; return true; }
    case Z30R_EWPNAUTOROTATE: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? (st->auto_rotate ? 10000 : 0) : -10000; return true; }
    case Z30R_EWPNGRAVITY: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? ((st->move_flags & 1) ? 10000 : 0) : -10000; return true; }
    case Z30R_EWPNLEVEL: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? st->level * 10000 : -10000; return true; }
    case Z30R_LWPNLEVEL: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = st ? st->level * 10000 : -10000; return true; }
    case Z30R_LWPNUNBL: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = st ? st->unblockable * 10000 : -10000; return true; }
    case Z30R_LWPNROTATION: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = st ? st->rotation * 10000 : -10000; return true; }
    case Z30R_EWPNROTATION: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? st->rotation * 10000 : -10000; return true; }
    case Z30R_EWPNTIMEOUT: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? st->weap_timeout * 10000 : -10000; return true; }
    case Z30R_EWPNSHADOWSPR: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? st->shadow_spr * 10000 : 0; return true; }
    case Z30R_EWEAPONSCRIPTUID: ret = z30_ewpn_ref() ? (long)ri->ewpn : 0; return true;
    case Z30R_EWPNPARENT: { weapon *w = z30_ewpn_ref(); ret = w ? (q30_quest_rule(512) ? w->parentid * 10000 : (long)w->parentid) : 0; return true; }

    // item-> and itemdata->
    case Z30R_ITEMSCRIPTTILE: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); ret = st ? st->script_tile * 10000 : -10000; return true; }
    case Z30R_ITEMSCRIPTFLIP: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); ret = st ? st->script_flip * 10000 : -10000; return true; }
    case Z30R_ITEMMOVEFLAGS: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); ret = (st && z30_flag_get(st->move_flags, Z30_INDEX, 11)) ? 10000 : 0; return true; }
    case Z30R_ITEMSHADOWSPR: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); ret = st ? st->shadow_spr * 10000 : 0; return true; }
    case Z30R_ITEMPSTRING: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); ret = st ? st->pstring * 10000 : -10000; return true; }
    case Z30R_ITEMSPRITESCRIPT: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); ret = st ? st->script * 10000 : -10000; return true; }
    case Z30R_ITEMSPRITEINITD:
    {
        Zasm30SpriteState *st = z30_state_of(z30_item_ref());
        ret = (st && Z30_INDEX >= 0 && Z30_INDEX < 8) ? st->initd[Z30_INDEX] : -10000;
        return true;
    }
    case Z30R_IDATAATTRIB:
    {
        ret = -10000;
        if(unsigned(ri->idata) < MAXITEMS && Z30_INDEX >= 0 && Z30_INDEX < 10)
        {
            const itemdata &it = itemsbuf[ri->idata];
            const long values[10] = { it.misc1, it.misc2, it.misc3, it.misc4, it.misc5, it.misc6, it.misc7, it.misc8, it.misc9, it.misc10 };
            ret = values[Z30_INDEX] * 10000;
        }
        return true;
    }
    case Z30R_IDATAMINHEARTS:
        ret = unsigned(ri->idata) < MAXITEMS ? itemsbuf[ri->idata].pickup_hearts * 10000 : -10000;
        return true;

    // paldata->
    case Z30R_PALDATACOLOR:
    case Z30R_PALDATAR:
    case Z30R_PALDATAG:
    case Z30R_PALDATAB:
        return zasm30_paldata_get(arg, ret);

    case Z30R_ALLOCATEBITMAPR:
    case Z30R_BITMAPWIDTH:
    case Z30R_BITMAPHEIGHT:
    case Z30R_CREATEBITMAP:
    case Z30R_DROPSETCHOOSE:
    case Z30R_FILEPOS:
    case Z30R_FILEEOF:
    case Z30R_FILEERR:
    case Z30R_DIRECTORYSIZE:
    case Z30R_STACKSIZE:
    case Z30R_STACKFULL:
        return zasm30_family_get(arg, ret);

    default:
        break;
    }

    // Anything else: the value last stored, and one log line naming the register.
    zasm30_log_once("register read", arg);
    ret = zasm30_store_get(arg, 0, Z30_INDEX);
    return false;
}

static bool zasm30_set_register_inner(const long arg, const long value)
{
    if(zasm30_sub_set(arg, value) || zasm30_scrdata_set(arg, value) || zasm30_draw_set(arg, value))
        return true;

    switch(arg)
    {
    case Z30R_MUSICUPDATECOND:     q30_set_music_update_cond(value / 10000); return true;
    case Z30R_MUSICUPDATEFLAGS:    q30_set_music_update_flag(Z30_INDEX, value != 0); return true;
    case Z30R_SCREENDATA_MUSIC:    q30_set_screen_music_script(currmap * MAPSCRS + currscr, value / 10000); return true;
    case Z30R_MUSICDATA_MIDI:      q30_musicdata_set(Z30_REF(MUSIC), q30md_MIDI, value); return true;
    case Z30R_MUSICDATA_TRACK:     q30_musicdata_set(Z30_REF(MUSIC), q30md_TRACK, value); return true;
    case Z30R_MUSICDATA_LOOPSTART: q30_musicdata_set(Z30_REF(MUSIC), q30md_LOOPSTART, value); return true;
    case Z30R_MUSICDATA_LOOPEND:   q30_musicdata_set(Z30_REF(MUSIC), q30md_LOOPEND, value); return true;
    case Z30R_MUSICDATA_XFADEIN:   q30_musicdata_set(Z30_REF(MUSIC), q30md_XFADEIN, value); return true;
    case Z30R_MUSICDATA_XFADEOUT:  q30_musicdata_set(Z30_REF(MUSIC), q30md_XFADEOUT, value); return true;
    default: break;
    }

    if(zasm30_sprite_set(arg, value))
        return true;

    const int slot = z30_ref_slot(arg);

    if(slot >= 0)
    {
        Z30_F.refs[slot] = value;
        return true;
    }

    switch(arg)
    {
    case Z30R_LINKITEMX: q30_set_button_item(true, value / 10000); return true;
    case Z30R_LINKITEMY: q30_set_button_item(false, value / 10000); return true;
    case Z30R_HEROBUNNY: q30_set_bunny_clock(value / 10000); return true;
    case Z30R_LINKITEMA:
    case Z30R_LINKITEMB:
    case Z30R_LINKSTUN:
    case Z30R_LINKTILEMOD:
    case Z30R_LINKCANFLICKER:
    case Z30R_HEROSTEPRATE:
        zasm30_store_set(arg, 0, 0, arg == Z30R_LINKCANFLICKER ? (value ? 10000 : 0) : value / 10000);
        return true;
    case Z30R_HEROLIFTEDWPN:
    case Z30R_HEROSTANDING:
        return true;
    case Z30R_LINKDIAG: set_bit(quest_rules, qr_LTTPWALK, value ? 1 : 0); return true;
    case Z30R_LINKBIGHITBOX: set_bit(quest_rules, qr_LTTPCOLLISION, value ? 1 : 0); return true;
    case Z30R_LINKINVFRAME: Link.setHClk(vbound(value / 10000, 0, 214747)); return true;

    case Z30R_NPCFLAGS:
    {
        enemy *e = (enemy *)guys.getByUID(ri->guyref);
        if(e && Z30_INDEX >= 0 && Z30_INDEX < 32)
        {
            if(value) e->flags |= (1 << Z30_INDEX);
            else e->flags &= ~(1 << Z30_INDEX);
        }
        return true;
    }
    case Z30R_NPCINITD:
        if(Z30_INDEX >= 0 && Z30_INDEX < 8)
            zasm30_sprite_state(ri->guyref).initd[Z30_INDEX] = value;
        return true;
    case Z30R_NPCIMMORTAL: zasm30_store_set(arg, ri->guyref, 0, value != 0); return true;
    case Z30R_NPCFRAME:
    {
        enemy *e = (enemy *)guys.getByUID(ri->guyref);
        if(e) e->clk = vbound(value / 10000, 0, 214748);
        return true;
    }
    case Z30R_NPCSPAWNSPR: zasm30_sprite_state(ri->guyref).spawn_spr = vbound(value / 10000, 0, 255); return true;
    case Z30R_NPCDEATHSPR: zasm30_sprite_state(ri->guyref).death_spr = vbound(value / 10000, 0, 255); return true;
    case Z30R_NPCSCRIPT:
    {
        Zasm30SpriteState &st = zasm30_sprite_state(ri->guyref);
        st.script = vbound(value / 10000, 0, 255);
        st.script_started = true;
        zasm30_drop_instance(SCRIPT_NPC, ri->guyref);
        return true;
    }
    case Z30R_NPCHITBY:
    case Z30R_NPCBEHAVIOUR:
        zasm30_store_set(arg, ri->guyref, Z30_INDEX, value);
        return true;
    case Z30R_NPCWEAPSPRITE: zasm30_store_set(arg, ri->guyref, 0, value); return true;

    case Z30R_LWPNSCRIPT:
    {
        Zasm30SpriteState &st = zasm30_sprite_state(ri->lwpn);
        st.script = vbound(value / 10000, 0, 255);
        st.script_started = true;
        zasm30_drop_instance(SCRIPT_LWPN, ri->lwpn);
        return true;
    }
    case Z30R_EWPNSCRIPT:
    {
        Zasm30SpriteState &st = zasm30_sprite_state(ri->ewpn);
        st.script = vbound(value / 10000, 0, 255);
        st.script_started = true;
        zasm30_drop_instance(SCRIPT_EWPN, ri->ewpn);
        return true;
    }
    case Z30R_LWPNINITD:
        if(Z30_INDEX >= 0 && Z30_INDEX < 8) zasm30_sprite_state(ri->lwpn).initd[Z30_INDEX] = value;
        return true;
    case Z30R_EWPNINITD:
        if(Z30_INDEX >= 0 && Z30_INDEX < 8) zasm30_sprite_state(ri->ewpn).initd[Z30_INDEX] = value;
        return true;
    case Z30R_LWPNFLAGS:
    case Z30R_LWPNMOVEFLAGS:
    case Z30R_LWPNTIMEOUT:
    case Z30R_LWPNLIFTLEVEL:
    case Z30R_LWPNLIFTTIME:
    case Z30R_LWPNLIFTHEIGHT:
    case Z30R_LWPNUSEWEAPON:
        zasm30_store_set(arg, ri->lwpn, (arg == Z30R_LWPNFLAGS || arg == Z30R_LWPNMOVEFLAGS) ? Z30_INDEX : 0, value);
        return true;
    case Z30R_EWPNUNBL: zasm30_store_set(arg, ri->ewpn, 0, value / 10000); return true;
    case Z30R_LWPNDEGANGLE:
    {
        weapon *w = (weapon *)Lwpns.getByUID(ri->lwpn);
        if(w) { w->angular = true; w->angle = (value / 10000.0) * (PI / 180.0); }
        return true;
    }
    case Z30R_EWPNDEGANGLE:
    {
        weapon *w = (weapon *)Ewpns.getByUID(ri->ewpn);
        if(w) { w->angular = true; w->angle = (value / 10000.0) * (PI / 180.0); }
        return true;
    }

    // combodata->
    case Z30R_COMBODTILE: z30_cdata().tile = vbound(value / 10000, 0, NEWMAXTILES - 1); return true;
    case Z30R_COMBODFLIP: z30_cdata().flip = (byte)(value / 10000); return true;
    case Z30R_COMBODWALK: z30_cdata().walk = (z30_cdata().walk & ~0x0F) | ((value / 10000) & 0x0F); return true;
    case Z30R_COMBODTYPE: z30_cdata().type = (byte)(value / 10000); return true;
    case Z30R_COMBODCSET: z30_cdata().csets = (z30_cdata().csets & 0xF0) | ((value / 10000) & 0x0F); return true;
    case Z30R_COMBODFLAG: z30_cdata().flag = (byte)(value / 10000); return true;
    case Z30R_COMBODFRAMES: z30_cdata().frames = (byte)(value / 10000); return true;
    case Z30R_COMBODASPEED: z30_cdata().speed = (byte)(value / 10000); return true;
    case Z30R_COMBODNEXTD: z30_cdata().nextcombo = (word)(value / 10000); return true;
    case Z30R_COMBODNEXTC: z30_cdata().nextcset = (byte)(value / 10000); return true;
    case Z30R_COMBODSKIPANIM: z30_cdata().skipanim = (byte)(value / 10000); return true;
    case Z30R_COMBODNEXTTIMER: z30_cdata().nexttimer = (word)(value / 10000); return true;
    case Z30R_COMBODAKIMANIMY: z30_cdata().skipanimy = (byte)(value / 10000); return true;
    case Z30R_COMBODANIMFLAGS: z30_cdata().animflags = (byte)(value / 10000); return true;
    case Z30R_COMBODATTRIBUTES: z30_combo_attr_set(Z30_REF(COMBODATA), Z30_INDEX, value); return true;
    case Z30R_COMBODATTRIBYTES: z30_combo_attr_set(Z30_REF(COMBODATA), 8 + Z30_INDEX, (value / 10000) * 10000); return true;
    case Z30R_COMBODUSRFLAGARR:
    {
        if(const Q30Combo *c = q30_combo(Z30_REF(COMBODATA)))
        {
            Q30Combo *cc = const_cast<Q30Combo *>(c);
            if(value) cc->usrflags |= (1 << Z30_INDEX);
            else cc->usrflags &= ~(1 << Z30_INDEX);
        }
        return true;
    }
    case Z30R_COMBODATASCRIPT:
    case Z30R_COMBODATAINITD:
    case Z30R_COMBODTRIGGERGSTATE:
    case Z30R_COMBODTRIGGERCHANGECMB:
        return true;

    // Screen->
    case Z30R_SCREENDATAITEM:
    {
        int v = vbound(value / 10000, -1, MAXITEMS - 1);
        if(v > -1) tmpscr->item = v;
        tmpscr->hasitem = v > -1;
        return true;
    }
    case Z30R_SCREENDATAHASITEM: tmpscr->hasitem = (byte)(value / 10000); return true;
    case Z30R_SCREENDATASTRING: tmpscr->str = (word)(value / 10000); return true;
    case Z30R_SCREENDATAENEMY: if(Z30_INDEX >= 0 && Z30_INDEX < 10) tmpscr->enemy[Z30_INDEX] = vbound(value / 10000, 0, MAXGUYS - 1); return true;
    case Z30R_SCREENDATASECRETCOMBO: if(Z30_INDEX >= 0 && Z30_INDEX < 128) tmpscr->secretcombo[Z30_INDEX] = (word)(value / 10000); return true;
    case Z30R_SCREENDATASECRETCSET: if(Z30_INDEX >= 0 && Z30_INDEX < 128) tmpscr->secretcset[Z30_INDEX] = (byte)(value / 10000); return true;
    case Z30R_SCREENDATASECRETFLAG: if(Z30_INDEX >= 0 && Z30_INDEX < 128) tmpscr->secretflag[Z30_INDEX] = (byte)(value / 10000); return true;
    case Z30R_SCREENDATAWARPRETX: if(Z30_INDEX >= 0 && Z30_INDEX < 4) tmpscr->warpreturnx[Z30_INDEX] = (byte)vbound(value / 10000, 0, 255); return true;
    case Z30R_SCREENDATAWARPRETY: if(Z30_INDEX >= 0 && Z30_INDEX < 4) tmpscr->warpreturny[Z30_INDEX] = (byte)vbound(value / 10000, 0, 255); return true;
    case Z30R_SCREENDATATWARPRETSQR:
        if(Z30_INDEX >= 0 && Z30_INDEX < 4)
            tmpscr->warpreturnc = (tmpscr->warpreturnc & ~(3 << (Z30_INDEX * 2))) | (((value / 10000) & 3) << (Z30_INDEX * 2));
        return true;
    case Z30R_SCREENDATASWARPRETSQR:
        if(Z30_INDEX >= 0 && Z30_INDEX < 4)
            tmpscr->warpreturnc = (tmpscr->warpreturnc & ~(3 << (8 + Z30_INDEX * 2))) | (((value / 10000) & 3) << (8 + Z30_INDEX * 2));
        return true;
    case Z30R_SCREENDATALAYERINVIS:
    {
        const int id = currmap * MAPSCRS + currscr;
        long v = zasm30_store_get(arg, id, 0);
        if(value) v |= 1 << Z30_INDEX; else v &= ~(1 << Z30_INDEX);
        zasm30_store_set(arg, id, 0, v);
        return true;
    }
    case Z30R_SCREEN_FLAG:
    {
        long n = Z30_INDEX;
        if(n >= 0 && n < 88)
        {
            byte &flag = (&tmpscr->flags)[n / 8];
            if(value) flag |= 1 << (n % 8);
            else flag &= ~(1 << (n % 8));
        }
        return true;
    }
    case Z30R_SCREENEXSTATED:
    {
        extern void q30_set_exstate(int, int, bool);
        q30_set_exstate(currmap * MAPSCRS + currscr, Z30_INDEX, value != 0);
        return true;
    }
    case Z30R_SCREENDATAEXDOOR:
    {
        extern void q30_set_xdoor(int, int, int, bool);
        int dir = SH::read_stack(SPR + 1) / 10000;
        int ind = SH::read_stack(SPR) / 10000;
        if(unsigned(dir) <= 3 && unsigned(ind) <= 7)
            q30_set_xdoor(currmap * MAPSCRS + currscr, dir, ind, true);
        return true;
    }
    case Z30R_SCREENINITD:
    {
        const int index = currmap * MAPSCRS + currscr;
        if(Z30_INDEX >= 0 && Z30_INDEX < 8)
        {
            std::map<int, Zasm30ScreenScript>::iterator it = zasm30_screen_scripts.find(index);
            if(it != zasm30_screen_scripts.end())
                it->second.initd[Z30_INDEX] = value;
        }
        return true;
    }
    case Z30R_SCREEN_DRAW_ORIGIN: zasm30_store_set(arg, 0, 0, value); return true;
    case Z30R_SCREEN_DRAW_ORIGIN_TARGET: zasm30_store_set(arg, 0, 0, value); return true;
    case Z30R_PUSHBLOCKLAYER: zasm30_store_set(arg, 0, 0, vbound(value / 10000, 0, 6)); return true;
    case Z30R_SHOWNMSG: return true;

    // mapdata->
    case Z30R_LOADMAPDATA: return true;
    case Z30R_MAPDATAINTID:
    {
        mapscr *m = z30_mapdata_now();
        const long f = ri->d[0] / 10000, d = ri->d[1] / 10000;
        if(m && f >= 0 && f < Z30_FFC_COUNT && d >= 0 && d < 8)
            m->initd[f][d] = value;
        return true;
    }
    case Z30R_MAPDATACOMBODD:
    case Z30R_MAPDATACOMBOCD:
    case Z30R_MAPDATACOMBOFD:
    case Z30R_MAPDATACOMBOTD:
    case Z30R_MAPDATACOMBOID:
    {
        mapscr *m = z30_mapdata(Z30_REF(MAPDATA));
        long n = Z30_INDEX;

        if(m && n >= 0 && n < 176)
        {
            switch(arg)
            {
            case Z30R_MAPDATACOMBODD: m->data[n] = vbound(value / 10000, 0, MAXCOMBOS - 1); break;
            case Z30R_MAPDATACOMBOCD: m->cset[n] = vbound(value / 10000, 0, 15); break;
            case Z30R_MAPDATACOMBOFD: m->sflag[n] = vbound(value / 10000, 0, 255); break;
            case Z30R_MAPDATACOMBOTD: combobuf[m->data[n]].type = vbound(value / 10000, 0, 255); break;
            case Z30R_MAPDATACOMBOID: combobuf[m->data[n]].flag = vbound(value / 10000, 0, 255); break;
            }
        }
        return true;
    }
    case Z30R_MAPDATAWARPRETX:
    case Z30R_MAPDATAWARPRETY:
    {
        mapscr *m = z30_mapdata(Z30_REF(MAPDATA));
        long n = Z30_INDEX;
        if(m && n >= 0 && n < 4)
        {
            if(arg == Z30R_MAPDATAWARPRETX) m->warpreturnx[n] = (byte)vbound(value / 10000, 0, 255);
            else m->warpreturny[n] = (byte)vbound(value / 10000, 0, 255);
        }
        return true;
    }

    // dmapdata->
    case Z30R_DMAPDATACONTINUE: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].cont = (byte)(value / 10000); return true;
    case Z30R_DMAPDATAOFFSET: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].xoff = (char)(value / 10000); return true;
    case Z30R_DMAPDATASIDEVIEW: zasm30_store_set(arg, Z30_REF(DMAPDATA), 0, value ? 1 : 0); return true;
    case Z30R_DMAPDATAMAP: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].map = vbound(value / 10000 - 1, 0, map_count - 1); return true;
    case Z30R_DMAPDATALEVEL: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].level = (word)(value / 10000); return true;
    case Z30R_DMAPDATACOMPASS: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].compass = (byte)(value / 10000); return true;
    case Z30R_DMAPDATATYPE: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].type = (DMaps[Z30_REF(DMAPDATA)].type & ~dmfTYPE) | ((value / 10000) & dmfTYPE); return true;
    case Z30R_DMAPDATAPALETTE: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].color = (word)(value / 10000); return true;
    case Z30R_DMAPDATAFLAGARR:
    {
        const long id = Z30_REF(DMAPDATA);
        if(unsigned(id) < MAXDMAPS)
        {
            if(value) DMaps[id].flags |= (1 << Z30_INDEX);
            else DMaps[id].flags &= ~(1 << Z30_INDEX);
        }
        return true;
    }
    case Z30R_DMAPINITD: return true;

    // itemdata->
    case Z30R_IDATATILE: itemsbuf[ri->idata].tile = vbound(value / 10000, 0, NEWMAXTILES - 1); return true;
    case Z30R_IDATACSET: itemsbuf[ri->idata].csets = (itemsbuf[ri->idata].csets & 0xF0) | vbound(value / 10000, 0, 15); return true;
    case Z30R_IDATABUNNYABLE: q30_set_item_bunny(ri->idata, value != 0); return true;
    case Z30R_IDATASCRIPT: itemsbuf[ri->idata].script = vbound(value / 10000, 0, 255); return true;
    case Z30R_IDATASPRITE: zasm30_store_set(arg, ri->idata, Z30_INDEX, value / 10000); return true;

    // spritedata->
    case Z30R_SPRITEDATATILE: if(unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS) wpnsbuf[Z30_REF(SPRITEDATA)].tile = vbound(value / 10000, 0, NEWMAXTILES - 1); return true;
    case Z30R_SPRITEDATACSETS: if(unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS) wpnsbuf[Z30_REF(SPRITEDATA)].csets = (wpnsbuf[Z30_REF(SPRITEDATA)].csets & 0xF0) | vbound(value / 10000, 0, 15); return true;
    case Z30R_SPRITEDATAFLCSET: if(unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS) wpnsbuf[Z30_REF(SPRITEDATA)].csets = (wpnsbuf[Z30_REF(SPRITEDATA)].csets & 0x0F) | (vbound(value / 10000, 0, 15) << 4); return true;
    case Z30R_SPRITEDATAFRAMES: if(unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS) wpnsbuf[Z30_REF(SPRITEDATA)].frames = (byte)(value / 10000); return true;
    case Z30R_SPRITEDATASPEED: if(unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS) wpnsbuf[Z30_REF(SPRITEDATA)].speed = (byte)(value / 10000); return true;
    case Z30R_SPRITEDATATYPE: return true;
    case Z30R_SPRITEDATAMISC: if(unsigned(Z30_REF(SPRITEDATA)) < MAXWPNS) wpnsbuf[Z30_REF(SPRITEDATA)].misc = (byte)(value / 10000); return true;

    // npcdata->
    case Z30R_NPCDATATILE: if(unsigned(Z30_REF(NPCDATA)) < MAXGUYS) guysbuf[Z30_REF(NPCDATA)].tile = vbound(value / 10000, 0, NEWMAXTILES - 1); return true;
    case Z30R_NPCDATADEATHSFX: if(unsigned(Z30_REF(NPCDATA)) < MAXGUYS) guysbuf[Z30_REF(NPCDATA)].deadsfx = (byte)(value / 10000); return true;
    case Z30R_NPCDATAHITSFX: if(unsigned(Z30_REF(NPCDATA)) < MAXGUYS) guysbuf[Z30_REF(NPCDATA)].hitsfx = (byte)(value / 10000); return true;
    case Z30R_NPCDATAANIM: if(unsigned(Z30_REF(NPCDATA)) < MAXGUYS) guysbuf[Z30_REF(NPCDATA)].anim = (short)(value / 10000); return true;
    case Z30R_NPCDATAEANIM: if(unsigned(Z30_REF(NPCDATA)) < MAXGUYS) guysbuf[Z30_REF(NPCDATA)].e_anim = (short)(value / 10000); return true;

    // Generic scripts
    case Z30R_GENDATARUNNING:
    {
        long slot2 = Z30_REF(GENERICDATA);
        if(slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS)
        {
            if(value) zasm30_generic_launch(slot2);
            else zasm30_generic_quit(slot2);
        }
        return true;
    }
    case Z30R_GENDATASIZE:
    {
        long slot2 = Z30_REF(GENERICDATA);
        if(slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS)
            zasm30_generic[slot2].data.resize(vbound(value / 10000, 0, 65536), 0);
        return true;
    }
    case Z30R_GENDATADATA:
    {
        long slot2 = Z30_REF(GENERICDATA), n = Z30_INDEX;
        if(slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS && n >= 0 && n < (long)zasm30_generic[slot2].data.size())
            zasm30_generic[slot2].data[n] = value;
        return true;
    }
    case Z30R_GENDATAINITD:
    {
        long slot2 = Z30_REF(GENERICDATA), n = Z30_INDEX;
        if(slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS && n >= 0 && n < 8)
            zasm30_generic[slot2].initd[n] = value;
        return true;
    }
    case Z30R_GENDATAEVENTSTATE:
    {
        long slot2 = Z30_REF(GENERICDATA), n = Z30_INDEX;
        if(slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS && n >= 0 && n < 32)
        {
            if(value) zasm30_generic[slot2].event_state |= (1 << n);
            else zasm30_generic[slot2].event_state &= ~(1 << n);
        }
        return true;
    }
    case Z30R_GENDATAEXITSTATE:
    case Z30R_GENDATARELOADSTATE:
    {
        long slot2 = Z30_REF(GENERICDATA), n = Z30_INDEX;
        if(slot2 >= 1 && slot2 < Q30_GENERIC_SLOTS && n >= 0 && n < 16)
        {
            word &v = arg == Z30R_GENDATAEXITSTATE ? zasm30_generic[slot2].exit_state : zasm30_generic[slot2].reload_state;
            if(value) v |= (1 << n);
            else v &= ~(1 << n);
        }
        return true;
    }

    // Game, input, misc
    case Z30R_GAMEMISC: game->set_maxcounter((word)value, Z30_INDEX & 0x1F); return true;
    case Z30R_READKEY: return true;
    case Z30R_KEYMODIFIERS: key_shifts = value / 10000; return true;
    case Z30R_BUTTONPRESS:
    case Z30R_BUTTONINPUT:
    {
        int n = vbound(Z30_INDEX, 0, 18 - 1);
        if(arg == Z30R_BUTTONPRESS) button_press[n] = value != 0;
        else control_state[n] = value != 0;
        return true;
    }
    case Z30R_GAMESCROLLING: return true;
    case Z30R_GAMEBOTTLEST: q30_set_bottle_slot(Z30_INDEX, vbound(value / 10000, 0, 64)); return true;
    case Z30R_GAMESUSPEND:
    case Z30R_MUSICUPDATECOND:
    case Z30R_GAMEASUBYOFF:
        zasm30_store_set(arg, 0, Z30_INDEX, value / 10000);
        return true;
    case Z30R_GAMEEVENTDATA: zasm30_store_set(arg, 0, Z30_INDEX, value); return true;
    case Z30R_FFRULE: if(Z30_INDEX >= 0 && Z30_INDEX < QUESTRULES_SIZE * 8) set_bit(quest_rules, Z30_INDEX, value ? 1 : 0); return true;
    case Z30R_FFCLAYER: zasm30_store_set(arg, ri->ffcref, 0, value / 10000); return true;
    case Z30R_FFCID:
    case Z30R_MAX_FFC_ID:
    case Z30R_DISTANCE:
    case Z30R_DISTANCESCALE:
    case Z30R_REGION_WIDTH:
    case Z30R_REGION_HEIGHT:
    case Z30R_REGION_ID:
    case Z30R_REGION_SCREEN_WIDTH:
    case Z30R_REGION_SCREEN_HEIGHT:
    case Z30R_REGION_NUM_COMBOS:
    case Z30R_REGION_ORIGIN_SCREEN:
        return true;
    // Nothing here draws the screen anywhere but at the viewport's origin, so a script that
    // really moves it (or centres it on the Hero, mode 1) is logged.
    case Z30R_VIEWPORT_X:
    case Z30R_VIEWPORT_Y:
        zasm30_store_set(arg, 0, 0, value);
        if(zasm30_viewport_mode == 2 && value != 0)
            zasm30_log_once("register write", arg);
        return true;
    case Z30R_VIEWPORT_HEIGHT:
        zasm30_store_set(arg, 0, 0, value);
        return true;
    case Z30R_HEROSHIELDJINX: zasm30_shield_jinx = value / 10000; return true;
    case Z30R_VIEWPORT_MODE:
        if(value < 0 || value > 2)
            return true;
        zasm30_viewport_mode = value;
        if(value == 1)
            zasm30_log_once("register write", arg);
        return true;

    // Game->MaxLWeapons(n): the list can't grow past its 255, and nothing lowers it here: a
    // script asking for another limit is logged.
    case Z30R_SPRITEMAXLWPN:
        if(value / 10000 != 255)
            zasm30_log_once("register write", arg);
        return true;

    case Z30R_NPCGLOWRAD: case Z30R_LWPNGLOWRAD: case Z30R_EWPNGLOWRAD: case Z30R_ITEMGLOWRAD:
    case Z30R_NPCGLOWSHP: case Z30R_LWPNGLOWSHP: case Z30R_EWPNGLOWSHP: case Z30R_ITEMGLOWSHP:
    {
        sprite *s = (arg == Z30R_NPCGLOWRAD || arg == Z30R_NPCGLOWSHP) ? (sprite *)z30_npc_ref()
                  : (arg == Z30R_LWPNGLOWRAD || arg == Z30R_LWPNGLOWSHP) ? (sprite *)z30_lwpn_ref()
                  : (arg == Z30R_EWPNGLOWRAD || arg == Z30R_EWPNGLOWSHP) ? (sprite *)z30_ewpn_ref() : (sprite *)z30_item_ref();
        Zasm30SpriteState *st = z30_state_of(s);
        if(st)
        {
            if(arg >= Z30R_ITEMGLOWSHP) st->glow_shape = vbound(value / 10000, 0, 2);
            else st->glow_rad = vbound(value / 10000, 0, 255);
        }
        return true;
    }

    case Z30R_COMBODFRAME:
    {
        const long id = Z30_REF(COMBODATA);
        if(id >= 0 && id < MAXCOMBOS)
        {
            const int f = vbound(value / 10000, 0, zc_max(1, (int)combobuf[id].frames) - 1);
            combobuf[id].tile = z30_combo_frame_tile(id, z30_combo_otile(id), f);
        }
        return true;
    }
    case Z30R_COMBODOTILE:
    {
        const long id = Z30_REF(COMBODATA);
        if(id >= 0 && id < MAXCOMBOS)
        {
            const int f = z30_combo_frame(id);
            const tile_id t = vbound(value / 10000, 0, NEWMAXTILES - 1);
            int (*table)[2] = (combobuf[id].animflags & AF_FRESH) ? animated_combo_table2 : animated_combo_table;
            table[id][1] = t;
            combobuf[id].tile = z30_combo_frame_tile(id, t, f);
        }
        return true;
    }

    case Z30R_IDATAOVERRIDEFL: q30_set_item_override_flags(ri->idata, value / 10000); return true;
    case Z30R_NPCDATAWPNSPRITE:
        if(unsigned(Z30_REF(NPCDATA)) < MAXGUYS)
            q30_set_guy_wpnsprite(Z30_REF(NPCDATA), vbound(value / 10000, 0, 255));
        return true;
    case Z30R_NPCISCORE:
        if(z30_npc_ref())
            zasm30_store_set(arg, ri->guyref, 0, value ? 2 : 1);
        return true;

    // Screen->EntryX/Y; with qr_WRITE_ENTRYPOINTS_AFFECTS_HEROCLASS also where the Hero comes back
    // after drowning or falling, which is Link's entry point in this engine.
    case Z30R_SCREENDATAENTRYX:
    case Z30R_SCREENDATAENTRYY:
    {
        const bool is_x = arg == Z30R_SCREENDATAENTRYX;
        const int v = vbound(value / 10000, 0, is_x ? 255 : 175);
        zasm30_store_set(arg, currmap * MAPSCRS + currscr, 0, v);
        if(q30_quest_rule(Z30_QR_WRITE_ENTRYPOINTS_AFFECTS_HEROCLASS))
            Link.setEntryPoints(is_x ? v : Link.getEntryX(), is_x ? Link.getEntryY() : v);
        return true;
    }
    case Z30R_SCREENDATAOCEANSFX:
    {
        const int v = vbound(value / 10000, 0, 255);
        if(tmpscr->oceansfx != v)
        {
            stop_sfx(tmpscr->oceansfx);
            tmpscr->oceansfx = (byte)v;
            cont_sfx(tmpscr->oceansfx);
        }
        return true;
    }

    case Z30R_MAPDATALAYERMAP:
    case Z30R_MAPDATALAYERSCREEN:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX - (zasm30_ffscript_version < 11 ? 0 : 1);
        if(m && n >= 0 && n < 6)
        {
            const byte v = (byte)vbound(value / 10000, 0, 255);
            if(arg == Z30R_MAPDATALAYERMAP) m->layermap[n] = v;
            else m->layerscreen[n] = v;
        }
        return true;
    }
    case Z30R_MAPDATAMISCD:
    {
        const int mi = z30_mapdata_mi(Z30_REF(MAPDATA));
        const long n = Z30_INDEX;
        if(mi >= 0 && mi < MAXDMAPS * MAPSCRSNORMAL && n >= 0 && n < 8)
            game->screen_d[mi][n] = value / 10000;
        return true;
    }

    case Z30R_SETGAMEOVERELEMENT: z30_gameover_write(Z30_INDEX, value / 10000); return true;
    case Z30R_SETGAMEOVERSTRING: z30_gameover_text_write(Z30_INDEX, value); return true;

    // Hero
    case Z30R_LINKHITBY: return true;       // read only upstream too
    case Z30R_LINKPUSH: Link.setPushing(zc_max(value / 10000, 0L)); return true;
    case Z30R_LINKCSET: Link.cs = value / 10000; return true;
    case Z30R_LINKEATEN: Link.setEaten(value / 10000); return true;
    case Z30R_LINKGRABBED: Link.setInWallm(value != 0); return true;
    case Z30R_HEROIMMORTAL: zasm30_hero_immortal = value / 10000; return true;
    case Z30R_LINKHURTSFX: zasm30_hero_hurt_sfx = vbound(value / 10000, 0, 255); return true;
    case Z30R_LINKSCRIPTTILE: zasm30_hero_script_tile = vbound(value / 10000, -1, NEWMAXTILES - 1); return true;
    case Z30R_LINKSCRIPFLIP: zasm30_hero_script_flip = vbound(value / 10000, -1, 256); return true;
    case Z30R_HEROSCRIPTCSET: zasm30_hero_script_cset = vbound(value / 10000, -1, 15); return true;
    case Z30R_HEROMOVEFLAGS: z30_flag_set(zasm30_hero_move_flags, Z30_INDEX, 11, value != 0); return true;
    case Z30R_HEROFAKEZ: zasm30_sprite_state(Link.getUID()).fakez = zc_max(value, 0L); return true;
    case Z30R_HEROFAKEJUMP: zasm30_sprite_state(Link.getUID()).fakefall = z30_fake_jump_write(value); return true;

    // Screen and game state
    case Z30R_SKIPF6: set_bit(quest_rules, qr_NOCONTINUE, value ? 1 : 0); return true;
    case Z30R_CLOCKACTIVE: Link.setClock(watch = (value != 0)); return true;
    case Z30R_SCREENDATASECRETSFX: tmpscr->secretsfx = (byte)vbound(value / 10000, 0, 255); return true;
    case Z30R_NOACTIVESUBSC: zasm30_no_active_subscreen = value != 0; return true;
    case Z30R_GAMELSWITCH:
        if(Z30_INDEX >= 0 && Z30_INDEX < MAXLEVELS)
            q30_set_level_state(Z30_INDEX, (dword)value);
        return true;
    case Z30R_GAMEGSWITCH:
        if(Z30_INDEX >= 0 && Z30_INDEX < 256)
            q30_set_global_state(Z30_INDEX, value / 10000);
        return true;

    // Input
    case Z30R_KEYPRESS:
        if(Z30_INDEX >= 0 && Z30_INDEX < 127)
            zasm30_key_press[Z30_INDEX] = value != 0;
        return true;
    case Z30R_KEYINPUT:
        if(Z30_INDEX >= 0 && Z30_INDEX < 127)
            zasm30_key_input[Z30_INDEX] = value != 0;
        return true;
    case Z30R_DISABLEKEY:
        if(Z30_INDEX >= 0 && Z30_INDEX < 127)
            zasm30_key_disabled[Z30_INDEX] = value != 0;
        return true;
    case Z30R_DISABLEBUTTON:
        if(Z30_INDEX >= 0 && Z30_INDEX < 18)
            zasm30_button_disabled[Z30_INDEX] = value != 0;
        return true;
    case Z30R_MOUSEARR:
        switch(Z30_INDEX)
        {
        case 0: set_register(INPUTMOUSEX, value); break;
        case 1: set_register(INPUTMOUSEY, value); break;
        case 2: set_register(INPUTMOUSEZ, value); break;
        }
        return true;

    // combodata->
    case Z30R_COMBODEFFECT:
    {
        const int cid = Z30_REF(COMBODATA);
        if(const Q30Combo *c = q30_combo(cid))
            const_cast<Q30Combo *>(c)->walk = (c->walk & 0x0F) | (((value / 10000) & 0x0F) << 4);
        if(cid >= 0 && cid < MAXCOMBOS)
            combobuf[cid].walk = (combobuf[cid].walk & 0x0F) | (((value / 10000) & 0x0F) << 4);
        return true;
    }
    case Z30R_COMBODATTRISHORTS: z30_combo_attr_set(Z30_REF(COMBODATA), 16 + Z30_INDEX, (value / 10000) * 10000); return true;
    case Z30R_COMBODTRIGSPAWNITEM:
    {
        const Q30Combo *c = q30_combo(Z30_REF(COMBODATA));
        if(c && !c->triggers.empty())
            const_cast<Q30Combo *>(c)->triggers[0].spawnitem = vbound(value / 10000, -(MAXITEMDROPSETS - 1), MAXITEMS - 1);
        return true;
    }
    case Z30R_COMBODTRIGGERFLAGS2:
    {
        const Q30Combo *c = q30_combo(Z30_REF(COMBODATA));
        const long n = Z30_INDEX;
        if(c && n >= 0 && n < 32 * 6)
        {
            Q30Combo *cc = const_cast<Q30Combo *>(c);
            if(n == Q30TF_ONLYGENTRIG)
                cc->only_gentrig = value != 0;
            else if(!cc->triggers.empty())
            {
                Q30Bits &bits = cc->triggers[0].flags;
                if(value)
                    bits.set(n);
                else if(n / 8 < (long)bits.v.size())
                    bits.v[n / 8] &= (byte)~(1 << (n % 8));
            }
        }
        return true;
    }

    // mapdata->
    case Z30R_MAPDATACOMBOSD:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        if(m && n >= 0 && n < (long)m->data.size() && m->data[n] < MAXCOMBOS)
        {
            newcombo &c = combobuf[m->data[n]];
            c.walk = (c.walk & 0xF0) | ((value / 10000) & 0x0F);
        }
        return true;
    }
    case Z30R_MAPDATAFFSCRIPT:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX, v = value / 10000;
        if(m && n >= 0 && n < Z30_FFC_COUNT && v >= 0 && v < NUMSCRIPTFFC)
            m->ffscript[n] = (word)v;
        return true;
    }
    case Z30R_MAPDATAFFX:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        if(m && n >= 0 && n < Z30_FFC_COUNT)
            m->ffx[n] = value;
        return true;
    }
    case Z30R_MAPDATAFFY:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        if(m && n >= 0 && n < Z30_FFC_COUNT)
            m->ffy[n] = value;
        return true;
    }
    case Z30R_MAPDATAENEMY:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX, v = value / 10000;
        if(m && n >= 0 && n < 10 && v >= 0 && v < MAXGUYS)
            m->enemy[n] = (word)v;
        return true;
    }
    case Z30R_MAPDATASIDEWARPDMAP:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        if(m && n >= 0 && n < 4)
            m->sidewarpdmap[n] = (word)vbound(value / 10000, 0, MAXDMAPS - 1);
        return true;
    }
    case Z30R_MAPDATASIDEWARPSC:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        if(m && n >= 0 && n < 4)
            m->sidewarpscr[n] = (byte)vbound(value / 10000, 0, 255);
        return true;
    }
    case Z30R_MAPDATASIDEWARPID:
    case Z30R_SCREENSIDEWARPID:
    {
        mapscr *m = arg == Z30R_SCREENSIDEWARPID ? tmpscr : z30_mapdata_now();
        const long n = Z30_INDEX, v = value / 10000;
        if(m && n >= 0 && n < 4 && v >= -1 && v <= 3)
        {
            if(v < 0)
                m->flags2 &= ~(1 << n);
            else
            {
                m->flags2 |= (1 << n);
                m->sidewarpindex = (m->sidewarpindex & ~(3 << (2 * n))) | (v << (2 * n));
            }
        }
        return true;
    }
    case Z30R_MAPDATABOSSSFX:
    {
        mapscr *m = z30_mapdata_now();
        if(m) m->bosssfx = (byte)vbound(value / 10000, 0, 255);
        return true;
    }
    case Z30R_MAPDATAOCEANSFX:
    {
        mapscr *m = z30_mapdata_now();
        if(m) m->oceansfx = (byte)vbound(value / 10000, 0, 255);
        return true;
    }
    case Z30R_MAPDATACATCHALL: { mapscr *m = z30_mapdata_now(); if(m) m->catchall = (word)(value / 10000); return true; }
    case Z30R_MAPDATACOLOUR: { mapscr *m = z30_mapdata_now(); if(m) m->color = (word)(value / 10000); return true; }
    case Z30R_MAPDATASTRING: { mapscr *m = z30_mapdata_now(); if(m) m->str = (word)(value / 10000); return true; }
    case Z30R_MAPDATAROOM: { mapscr *m = z30_mapdata_now(); if(m) m->room = (byte)(value / 10000); return true; }
    case Z30R_MAPDATAITEM:
    {
        mapscr *m = z30_mapdata_now();
        if(m)
        {
            const int v = vbound(value / 10000, -1, MAXITEMS - 1);
            if(v > -1) m->item = v;
            m->hasitem = v > -1;
        }
        return true;
    }
    case Z30R_MAPDATAFLAGS:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        if(m && n >= 0 && n < 10)
            (&m->flags)[n] = (byte)(value / 10000);
        return true;
    }
    case Z30R_MAPDATAGUY:
    {
        const int mi = z30_mapdata_mi(Z30_REF(MAPDATA));
        if(mi >= 0)
            game->guys[mi] = (byte)vbound(value / 10000, 0, 10);
        return true;
    }

    // dmapdata->
    case Z30R_DMAPLEVELPAL:
    {
        const long n = Z30_INDEX, v = value / 10000;
        if(n >= 0 && n < MAXDMAPS && v >= 0 && v < 256)
        {
            DMaps[n].color = (word)v;
            if(n == currdmap)
            {
                loadlvlpal(v);
                currcset = v;
            }
        }
        return true;
    }
    case Z30R_DMAPDATAMIDI:
        if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS)
        {
            const int midi = z30_new_midi_id(value / 10000);
            if(midi >= 0)
                DMaps[Z30_REF(DMAPDATA)].midi = (byte)midi;
        }
        return true;
    case Z30R_DMAPDATASUBSCRA: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].active_subscreen = (byte)(value / 10000); return true;
    case Z30R_DMAPDATAFLAGS: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].flags = value / 10000; return true;
    case Z30R_DMAPDATAGRID:
    {
        const long id = Z30_REF(DMAPDATA), n = Z30_INDEX;
        if(unsigned(id) < MAXDMAPS && n >= 0 && n < 8)
            DMaps[id].grid[n] = (byte)(value / 10000);
        return true;
    }
    case Z30R_DMAPDATADISABLEDITEMS:
    {
        const long id = Z30_REF(DMAPDATA), n = Z30_INDEX;
        if(unsigned(id) < MAXDMAPS && n >= 0 && n < iMax)
            DMaps[id].disableditems[n] = value ? 1 : 0;
        return true;
    }

    // npc->
    case Z30R_NPCHITDIR: { enemy *e = z30_npc_ref(); if(e) q30_enemy_set_hitdir(e, vbound(value / 10000, 0, 3)); return true; }
    case Z30R_NPCINVINC: { enemy *e = z30_npc_ref(); if(e) e->hclk = value / 10000; return true; }
    case Z30R_NPCHALTCLK: { enemy *e = z30_npc_ref(); if(e) q30_enemy_set_clk2(e, vbound(value / 10000, 0, 214748)); return true; }
    case Z30R_NPCLINEDUP: return true;
    case Z30R_NPCFLASHINGCSET: return true;
    case Z30R_NPCRANDOM: { enemy *e = z30_npc_ref(); if(e) e->rate = (short)(value / 10000); return true; }
    case Z30R_NPCMOVEFLAGS: { enemy *e = z30_npc_ref(); if(e) z30_flag_set(zasm30_npc_state(e)->move_flags, Z30_INDEX, 16, value != 0); return true; }
    case Z30R_NPCSCRIPTTILE: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); if(st) st->script_tile = vbound(value / 10000, -1, NEWMAXTILES - 1); return true; }
    case Z30R_NPCSCRIPTFLIP: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); if(st) st->script_flip = vbound(value / 10000, -1, 127); return true; }
    case Z30R_NPCFAKEZ: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); if(st) st->fakez = zc_max(value, 0L); return true; }
    case Z30R_NPCFAKEJUMP:
    {
        enemy *e = z30_npc_ref();
        Zasm30SpriteState *st = z30_state_of(e);
        if(st && canfall(e->id))
        {
            st->fakefall = z30_fake_jump_write(value);
            if(GuyH::loadNPC(ri->guyref, "npc->FakeJump") == SH::_NoError && GuyH::hasLink())
                zasm30_sprite_state(Link.getUID()).fakefall = st->fakefall;
        }
        return true;
    }
    case Z30R_NPCROTATION: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); if(st) st->rotation = value / 10000; return true; }
    case Z30R_NPCSCALE: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); if(st) st->scale = value / 100; return true; }
    case Z30R_NPCSHADOWSPR: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); if(st) st->shadow_spr = vbound(value / 10000, 0, 255); return true; }
    case Z30R_NPCSHADOWYOFS: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); if(st) st->shadow_yofs = value / 10000; return true; }
    case Z30R_NPCKNOCKBACKSPEED: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); if(st) st->knockback_speed = vbound(value / 10000, 0, 255); return true; }
    case Z30R_NPCNOSLIDE: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); if(st) st->no_slide = value != 0; return true; }
    case Z30R_NPCNOSCRIPTKB: { Zasm30SpriteState *st = z30_state_of(z30_npc_ref()); if(st) st->no_script_kb = value != 0; return true; }
    case Z30R_NPCSHIELD:
    {
        static const dword bits[5] = { inv_front, inv_left, inv_right, inv_back, guy_bkshield };
        enemy *e = z30_npc_ref();
        const long n = Z30_INDEX;
        if(e && n >= 0 && n < 5)
        {
            if(value) e->flags |= bits[n];
            else e->flags &= ~bits[n];
        }
        return true;
    }

    // npcdata->
    case Z30R_NPCDATAHP: if(unsigned(Z30_REF(NPCDATA)) < MAXGUYS) guysbuf[Z30_REF(NPCDATA)].hp = (short)(value / 10000); return true;
    case Z30R_NPCDATASTEP: if(unsigned(Z30_REF(NPCDATA)) < MAXGUYS) guysbuf[Z30_REF(NPCDATA)].step = (short)(value / 10000); return true;
    case Z30R_NPCDATASTILE: if(unsigned(Z30_REF(NPCDATA)) < MAXGUYS) guysbuf[Z30_REF(NPCDATA)].s_tile = vbound(value / 10000, 0, NEWMAXTILES - 1); return true;
    case Z30R_NPCDATAWEAPON: if(unsigned(Z30_REF(NPCDATA)) < MAXGUYS) guysbuf[Z30_REF(NPCDATA)].weapon = (short)(value / 10000); return true;
    case Z30R_NPCDATAWEAPONDAMAGE: if(unsigned(Z30_REF(NPCDATA)) < MAXGUYS) guysbuf[Z30_REF(NPCDATA)].wdp = (short)(value / 10000); return true;

    // lweapon-> and eweapon->
    case Z30R_LWPNSCRIPTTILE: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); if(st) st->script_tile = vbound(value / 10000, -1, NEWMAXTILES - 1); return true; }
    case Z30R_LWPNSCRIPTFLIP: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); if(st) st->script_flip = vbound(value / 10000, -1, 127); return true; }
    case Z30R_EWPNSCRIPTTILE: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) st->script_tile = vbound(value / 10000, -1, NEWMAXTILES - 1); return true; }
    case Z30R_EWPNSCRIPTFLIP: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) st->script_flip = vbound(value / 10000, -1, 127); return true; }
    case Z30R_EWPNFAKEZ: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) st->fakez = zc_max(value, 0L); return true; }
    case Z30R_EWPNFAKEJUMP: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) st->fakefall = z30_fake_jump_write(value); return true; }
    case Z30R_LWPNFAKEZ: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); if(st) st->fakez = zc_max(value, 0L); return true; }
    case Z30R_LWPNFAKEJUMP: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); if(st) st->fakefall = z30_fake_jump_write(value); return true; }
    case Z30R_EWPNMOVEFLAGS: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) z30_flag_set(st->move_flags, Z30_INDEX, 32, value != 0); return true; }
    case Z30R_EWPNFLAGS: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) z30_flag_set(st->weapon_flags, Z30_INDEX, 32, value != 0); return true; }
    case Z30R_EWPNAUTOROTATE: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) st->auto_rotate = value != 0; return true; }
    case Z30R_EWPNGRAVITY: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) z30_flag_set(st->move_flags, 0, 32, value != 0); return true; }
    case Z30R_EWPNLEVEL: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) st->level = value / 10000; return true; }
    case Z30R_LWPNLEVEL: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); if(st) st->level = value / 10000; return true; }
    case Z30R_LWPNUNBL: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); if(st) st->unblockable = (value / 10000) & 0xF; return true; }
    case Z30R_LWPNROTATION: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); if(st) st->rotation = value / 10000; return true; }
    case Z30R_EWPNROTATION: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) st->rotation = value / 10000; return true; }
    case Z30R_EWPNTIMEOUT: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) st->weap_timeout = vbound(value / 10000, 0, 214748); return true; }
    case Z30R_EWPNSHADOWSPR: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) st->shadow_spr = vbound(value / 10000, 0, 255); return true; }
    case Z30R_EWPNPARENT: { weapon *w = z30_ewpn_ref(); if(w) w->parentid = q30_quest_rule(512) ? value / 10000 : value; return true; }
    case Z30R_EWEAPONSCRIPTUID: return true;

    // item-> and itemdata->
    case Z30R_ITEMSCRIPTTILE: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); if(st) st->script_tile = vbound(value / 10000, -1, NEWMAXTILES - 1); return true; }
    case Z30R_ITEMSCRIPTFLIP: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); if(st) st->script_flip = vbound(value / 10000, -1, 127); return true; }
    case Z30R_ITEMMOVEFLAGS: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); if(st) z30_flag_set(st->move_flags, Z30_INDEX, 11, value != 0); return true; }
    case Z30R_ITEMSHADOWSPR: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); if(st) st->shadow_spr = vbound(value / 10000, 0, 255); return true; }
    case Z30R_ITEMPSTRING: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); if(st) st->pstring = vbound(value / 10000, 0, msg_count - 1); return true; }
    case Z30R_ITEMSPRITESCRIPT:
    {
        item *it = z30_item_ref();
        if(it)
        {
            Zasm30SpriteState *st = zasm30_item_state(it);
            st->script = (word)vbound(value / 10000, 0, Q30_ITEMSPRITE_SLOTS - 1);
            st->script_started = true;
            zasm30_drop_instance(SCRIPT_ITEMSPRITE, ri->itemref);
        }
        return true;
    }
    case Z30R_ITEMSPRITEINITD:
    {
        Zasm30SpriteState *st = z30_state_of(z30_item_ref());
        if(st && Z30_INDEX >= 0 && Z30_INDEX < 8)
            st->initd[Z30_INDEX] = value;
        return true;
    }
    case Z30R_IDATAATTRIB:
    {
        if(unsigned(ri->idata) < MAXITEMS && Z30_INDEX >= 0 && Z30_INDEX < 10)
        {
            itemdata &it = itemsbuf[ri->idata];
            long *fields[10] = { &it.misc1, &it.misc2, &it.misc3, &it.misc4, &it.misc5, &it.misc6, &it.misc7, &it.misc8, &it.misc9, &it.misc10 };
            *fields[Z30_INDEX] = value / 10000;
        }
        return true;
    }
    case Z30R_IDATAMINHEARTS:
        if(unsigned(ri->idata) < MAXITEMS)
            itemsbuf[ri->idata].pickup_hearts = (byte)vbound(value / 10000, 0, 255);
        return true;

    // paldata->
    case Z30R_PALDATACOLOR:
    case Z30R_PALDATAR:
    case Z30R_PALDATAG:
    case Z30R_PALDATAB:
        return zasm30_paldata_set(arg, value);

    case Z30R_BITMAPWIDTH:
    case Z30R_BITMAPHEIGHT:
    case Z30R_CREATEBITMAP:
    case Z30R_DROPSETCHOOSE:
        return zasm30_family_set(arg, value);

    default:
        break;
    }

    zasm30_log_once("register write", arg);
    zasm30_store_set(arg, 0, Z30_INDEX, value);
    return false;
}

// ---- what the engine asks of these registers --------------------------------------------------

// A new frame's hit check for Link is about to run.
void q30_hero_hit_clear()
{
    if(zasm30_ffscript_version)
        memset(zasm30_hitby, 0, sizeof(zasm30_hitby));
}

// Something hit Link this frame. kind is 0 for an enemy, 1 for an enemy weapon, 2 for one of his own.
void q30_hero_hit(int kind, int index, sprite *s)
{
    if(!zasm30_ffscript_version || !s)
        return;

    switch(kind)
    {
    case 0:
        zasm30_hitby[0] = index + 1;                // HIT_BY_NPC
        zasm30_hitby[4] = s->getUID();              // HIT_BY_NPC_UID
        zasm30_hitby[13] = s->getUID();             // HIT_BY_NPC_ENGINE_UID
        zasm30_hitby[16] = s->id;                   // HIT_BY_NPC_ID
        zasm30_hitby[10] = ((enemy *)s)->family;    // HIT_BY_NPC_TYPE
        break;

    case 1:
        zasm30_hitby[1] = index + 1;                // HIT_BY_EWEAPON
        zasm30_hitby[5] = s->getUID();              // HIT_BY_EWEAPON_UID
        zasm30_hitby[14] = s->getUID();             // HIT_BY_EWEAPON_ENGINE_UID
        zasm30_hitby[11] = s->id;                   // HIT_BY_EWEAPON_TYPE
        break;

    case 2:
    {
        const int parent = ((weapon *)s)->parentitem;
        zasm30_hitby[2] = index + 1;                // HIT_BY_LWEAPON
        zasm30_hitby[6] = s->getUID();              // HIT_BY_LWEAPON_UID
        zasm30_hitby[15] = s->getUID();             // HIT_BY_LWEAPON_ENGINE_UID
        zasm30_hitby[12] = s->id;                   // HIT_BY_LWEAPON_TYPE
        zasm30_hitby[8] = parent >= 0 ? parent : -1;
        zasm30_hitby[9] = parent >= 0 ? q30_item_family(parent) : -1;
        break;
    }
    }
}

bool q30_no_active_subscreen()
{
    return zasm30_ffscript_version && zasm30_no_active_subscreen;
}

// The CSet Hero->ScriptCSet puts in place of his usual one, or normal if it isn't set.
bool q30_hero_immortal()
{
    return zasm30_ffscript_version && zasm30_hero_immortal != 0;
}

int q30_hero_hurt_sfx()
{
    return (zasm30_ffscript_version && zasm30_hero_hurt_sfx > 0) ? zasm30_hero_hurt_sfx : WAV_OUCH;
}

int q30_hero_cset_base(int normal)
{
    return zasm30_ffscript_version && zasm30_hero_script_cset > -1 ? zasm30_hero_script_cset : normal;
}

// sprite::draw for 2.55+ quests: what a sprite draws as when a script has set its ScriptTile,
// ScriptFlip or FakeZ. Flips past 3 (upstream's rotations) become the plain flip the drawing here has.
void q30_sprite_appearance(sprite *s, int *tile, int *flip, fix *yofs)
{
    if(!zasm30_ffscript_version)
        return;

    static const bool trace = getenv("ZC_Q30_TRACE_APPEAR") != NULL;   // diagnostics: what scripts changed at draw time
    static int trace_left = 60;
    const int old_tile = *tile, old_flip = *flip;
    const long old_yofs = long(*yofs);

    if(s == (sprite *)&Link)
    {
        if(zasm30_hero_script_tile > 0)
            *tile = zasm30_hero_script_tile;

        if(zasm30_hero_script_flip > -1)
            *flip = zasm30_hero_script_flip & 3;

        if(Zasm30SpriteState *hero = zasm30_sprite_state_if(s->getUID()))
            if(hero->fakez)
                *yofs -= hero->fakez / 10000;
    }
    else if(Zasm30SpriteState *st = zasm30_sprite_state_if(s->getUID()))
    {
        if(st->script_tile > -1)
            *tile = st->script_tile;

        if(st->script_flip > -1)
            *flip = st->script_flip & 3;

        if(st->fakez)
            *yofs -= st->fakez / 10000;
    }

    if(trace && trace_left > 0 && (*tile != old_tile || *flip != old_flip || long(*yofs) != old_yofs))
    {
        --trace_left;
        al_trace("ZASM: unhandled appearance %s uid %ld tile %d->%d flip %d->%d yofs %ld->%ld\n", s == (sprite *)&Link ? "hero" : "sprite",
                 s->getUID(), old_tile, *tile, old_flip, *flip, old_yofs, long(*yofs));
    }
}

// sprite drawing extras for q30_triggers.cpp (engine area): { rotation, scale, whole ScriptFlip,
// shadow sprite, shadow x offset, shadow y offset } a script set on this sprite.
void q30_sprite_values(sprite *s, int *v)
{
    if(!zasm30_ffscript_version)
        return;

    if(s == (sprite *)&Link)
        v[2] = (int)zasm30_hero_script_flip;

    if(Zasm30SpriteState *st = zasm30_sprite_state_if(s->getUID()))
    {
        v[0] = (int)st->rotation;
        v[1] = (int)st->scale;

        if(s != (sprite *)&Link)
            v[2] = (int)st->script_flip;

        v[3] = (int)st->shadow_spr;
        v[4] = (int)st->shadow_xofs;
        v[5] = (int)st->shadow_yofs;
    }
}

// Game start: nothing a script set on the player or the input carries over from the last game.
static void zasm30_reset_register_state()
{
    memset(zasm30_hitby, 0, sizeof(zasm30_hitby));
    zasm30_hero_script_tile = 0;
    zasm30_hero_script_flip = -1;
    zasm30_hero_script_cset = -1;
    zasm30_hero_move_flags = 1 | 2 | 4;     // upstream's Hero starts obeying gravity and able to fall and drown
    zasm30_hero_immortal = 0;
    zasm30_hero_hurt_sfx = 0;
    zasm30_no_active_subscreen = false;
    memset(zasm30_key_input, 0, sizeof(zasm30_key_input));
    memset(zasm30_key_press, 0, sizeof(zasm30_key_press));
    memset(zasm30_key_disabled, 0, sizeof(zasm30_key_disabled));
    memset(zasm30_key_was_down, 0, sizeof(zasm30_key_was_down));
    memset(zasm30_button_disabled, 0, sizeof(zasm30_button_disabled));
    z30_gameover_reset();
    zasm30_viewport_mode = 0;
    zasm30_shield_jinx = 0;
}

// The start of each frame of a newer quest.
static int zasm30_frame_screen = -1;

static void zasm30_frame_start()
{
    if(zasm30_shield_jinx > 0)
        --zasm30_shield_jinx;

    // Screen->EntryX/Y belong to upstream's copy of the screen, made afresh on arrival.
    const int index = currmap * MAPSCRS + currscr;

    if(index != zasm30_frame_screen)
    {
        zasm30_frame_screen = index;
        zasm30_store.erase(std::make_pair(std::make_pair((long)Z30R_SCREENDATAENTRYX, (long)index), 0L));
        zasm30_store.erase(std::make_pair(std::make_pair((long)Z30R_SCREENDATAENTRYY, (long)index), 0L));
    }
}

#endif

// Registers 3.0 and 2.55 scripts use that 2.53's get_register has no case for.
// Upstream's numbers (q30_scripts_defs.h), ported from engine/src/zc/scripting/types/.
// Included once, from src/ffscript.cpp.

#ifndef ZC_Q30_SCRIPTS_REGS_H
#define ZC_Q30_SCRIPTS_REGS_H

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

// ---- SPRITE_* ----------------------------------------------------------------------------------

static bool zasm30_sprite_get(const long arg, long &ret)
{
    Z30Sprite r = z30_sprite(Z30_REF(SPRITE));
    sprite *s = r.s;

    if(r.kind == Z30S_NONE)
    {
        ret = 0;
        return true;
    }

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
    case Z30R_SPRITE_FAKE_JUMP: ret = st ? st->fakefall : 0; return true;
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
        ret = st ? ((st->drawstyle2 & (1 << Z30_INDEX)) ? 10000 : 0) : 0;
        return true;
    }

    return false;
}

static bool zasm30_sprite_set(const long arg, const long value)
{
    Z30Sprite r = z30_sprite(Z30_REF(SPRITE));
    sprite *s = r.s;

    if(r.kind == Z30S_NONE)
        return true;

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
    case Z30R_SPRITE_FAKE_JUMP: if(st) st->fakefall = value; return true;
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
        if(st)
        {
            if(value) st->drawstyle2 |= (1 << Z30_INDEX);
            else st->drawstyle2 &= ~(1 << Z30_INDEX);
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

// ---- reading registers -------------------------------------------------------------------------

static bool zasm30_get_register(const long arg, long &ret)
{
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
    case Z30R_NPCSCRIPTTILE:
    case Z30R_NPCSCRIPTFLIP:
        ret = -10000;
        return true;

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
    case Z30R_COMBODTILE: ret = combobuf[Z30_REF(COMBODATA)].tile * 10000; return true;
    case Z30R_COMBODFLIP: ret = combobuf[Z30_REF(COMBODATA)].flip * 10000; return true;
    case Z30R_COMBODWALK: ret = (combobuf[Z30_REF(COMBODATA)].walk & 0x0F) * 10000; return true;
    case Z30R_COMBODTYPE: ret = combobuf[Z30_REF(COMBODATA)].type * 10000; return true;
    case Z30R_COMBODCSET: ret = (combobuf[Z30_REF(COMBODATA)].csets & 0x0F) * 10000; return true;
    case Z30R_COMBODFLAG: ret = combobuf[Z30_REF(COMBODATA)].flag * 10000; return true;
    case Z30R_COMBODFRAMES: ret = combobuf[Z30_REF(COMBODATA)].frames * 10000; return true;
    case Z30R_COMBODASPEED: ret = combobuf[Z30_REF(COMBODATA)].speed * 10000; return true;
    case Z30R_COMBODNEXTD: ret = combobuf[Z30_REF(COMBODATA)].nextcombo * 10000; return true;
    case Z30R_COMBODNEXTC: ret = combobuf[Z30_REF(COMBODATA)].nextcset * 10000; return true;
    case Z30R_COMBODSKIPANIM: ret = combobuf[Z30_REF(COMBODATA)].skipanim * 10000; return true;
    case Z30R_COMBODNEXTTIMER: ret = combobuf[Z30_REF(COMBODATA)].nexttimer * 10000; return true;
    case Z30R_COMBODAKIMANIMY: ret = combobuf[Z30_REF(COMBODATA)].skipanimy * 10000; return true;
    case Z30R_COMBODANIMFLAGS: ret = combobuf[Z30_REF(COMBODATA)].animflags * 10000; return true;
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
    case Z30R_LOADMAPDATA:
        ret = 0;
        return true;
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
    case Z30R_GAMEBOTTLEST: ret = zasm30_store_get(arg, 0, Z30_INDEX) * 10000; return true;
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
    case Z30R_VIEWPORT_X:
    case Z30R_VIEWPORT_Y: ret = zasm30_store_get(arg, 0, 0); return true;
    case Z30R_VIEWPORT_WIDTH: ret = 256 * 10000; return true;
    case Z30R_VIEWPORT_HEIGHT: ret = zasm30_store_get(arg, 0, 0) ? zasm30_store_get(arg, 0, 0) : 232 * 10000; return true;

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
    case Z30R_SUBDATACURPG:
    case Z30R_SUBDATAPAGES:
    case Z30R_SUBDATATRANSCLK:
    case Z30R_SUBDATATRANSTOPG:
    case Z30R_SUBWIDGPOSFLAG:
        return zasm30_family_get(arg, ret);

    default:
        break;
    }

    // Anything else: the value last stored, and one log line naming the register.
    zasm30_log_once("register read", arg);
    ret = zasm30_store_get(arg, 0, Z30_INDEX);
    return false;
}

static bool zasm30_set_register(const long arg, const long value)
{
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
    case Z30R_COMBODTILE: combobuf[Z30_REF(COMBODATA)].tile = vbound(value / 10000, 0, NEWMAXTILES - 1); return true;
    case Z30R_COMBODFLIP: combobuf[Z30_REF(COMBODATA)].flip = (byte)(value / 10000); return true;
    case Z30R_COMBODWALK: combobuf[Z30_REF(COMBODATA)].walk = (combobuf[Z30_REF(COMBODATA)].walk & ~0x0F) | ((value / 10000) & 0x0F); return true;
    case Z30R_COMBODTYPE: combobuf[Z30_REF(COMBODATA)].type = (byte)(value / 10000); return true;
    case Z30R_COMBODCSET: combobuf[Z30_REF(COMBODATA)].csets = (combobuf[Z30_REF(COMBODATA)].csets & 0xF0) | ((value / 10000) & 0x0F); return true;
    case Z30R_COMBODFLAG: combobuf[Z30_REF(COMBODATA)].flag = (byte)(value / 10000); return true;
    case Z30R_COMBODFRAMES: combobuf[Z30_REF(COMBODATA)].frames = (byte)(value / 10000); return true;
    case Z30R_COMBODASPEED: combobuf[Z30_REF(COMBODATA)].speed = (byte)(value / 10000); return true;
    case Z30R_COMBODNEXTD: combobuf[Z30_REF(COMBODATA)].nextcombo = (word)(value / 10000); return true;
    case Z30R_COMBODNEXTC: combobuf[Z30_REF(COMBODATA)].nextcset = (byte)(value / 10000); return true;
    case Z30R_COMBODSKIPANIM: combobuf[Z30_REF(COMBODATA)].skipanim = (byte)(value / 10000); return true;
    case Z30R_COMBODNEXTTIMER: combobuf[Z30_REF(COMBODATA)].nexttimer = (word)(value / 10000); return true;
    case Z30R_COMBODAKIMANIMY: combobuf[Z30_REF(COMBODATA)].skipanimy = (byte)(value / 10000); return true;
    case Z30R_COMBODANIMFLAGS: combobuf[Z30_REF(COMBODATA)].animflags = (byte)(value / 10000); return true;
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
    case Z30R_GAMESUSPEND:
    case Z30R_GAMEBOTTLEST:
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
    case Z30R_VIEWPORT_X:
    case Z30R_VIEWPORT_Y:
    case Z30R_VIEWPORT_HEIGHT:
        zasm30_store_set(arg, 0, 0, value);
        return true;

    case Z30R_BITMAPWIDTH:
    case Z30R_BITMAPHEIGHT:
    case Z30R_CREATEBITMAP:
    case Z30R_DROPSETCHOOSE:
    case Z30R_SUBDATACURPG:
    case Z30R_SUBDATAPAGES:
    case Z30R_SUBDATATRANSCLK:
    case Z30R_SUBDATATRANSTOPG:
    case Z30R_SUBWIDGPOSFLAG:
        return zasm30_family_set(arg, value);

    default:
        break;
    }

    zasm30_log_once("register write", arg);
    zasm30_store_set(arg, 0, Z30_INDEX, value);
    return false;
}

#endif

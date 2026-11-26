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
extern int q30_active_sub_yoff;     // Game->ActiveSubscreenY (zc_subscr.cpp)

// Fields 3.0 has and 2.53 hasn't, keyed by register + object + index, so scripts can store and
// read them back. A write here is also how an unported field shows up in the log.
static std::map<std::pair<std::pair<long,long>,long>, long> zasm30_store;

// A button's item as scripts see it: a combined bow and arrow holds the arrow's id + 0xF000 (2.53's
// Bweapon), where upstream's button holds the arrow itself.
static long z30_btn_id(int wpn)
{
    return (wpn & 0xF000) == 0xF000 ? wpn - 0xF000 : wpn;
}

static long zasm30_store_get(long reg, long ref, long idx)
{
    std::map<std::pair<std::pair<long,long>,long>, long>::iterator it = zasm30_store.find(std::make_pair(std::make_pair(reg, ref), idx));
    return it == zasm30_store.end() ? 0 : it->second;
}

static void zasm30_store_set(long reg, long ref, long idx, long value)
{
    zasm30_store[std::make_pair(std::make_pair(reg, ref), idx)] = value;
}

// Game->Suspend[] (upstream's FFCore.system_suspend[susptLAST], cleared by FFScript::init as a
// game starts or continues). game_loop holds Link back under susptHERO (5); scripts read the rest.
static bool z30_system_suspend[68];

bool q30_game_suspended(long type)
{
    return type >= 0 && type < 68 && z30_system_suspend[type];
}

void q30_set_game_suspend(long type, bool on)
{
    if(type >= 0 && type < 68)
        z30_system_suspend[type] = on;
}

void q30_clear_game_suspend()
{
    memset(z30_system_suspend, 0, sizeof(z30_system_suspend));
}

void zasm30_log_once(const char *kind, long id)
{
    static std::set<long> seen;

    if(seen.insert(((long)(kind[0]) << 24) ^ id).second)
        al_trace("ZASM: unhandled %s %ld\n", kind, id);
}

// ---- resolving a sprite reference --------------------------------------------------------------

// Screen->DrawOrigin values (upstream DrawOrigin, raw longs in screendata.zh)
enum
{
    Z30_DRAW_ORIGIN_DEFAULT, Z30_DRAW_ORIGIN_PLAYING_FIELD, Z30_DRAW_ORIGIN_SCREEN, Z30_DRAW_ORIGIN_REGION,
    Z30_DRAW_ORIGIN_REGION_SCROLLING_NEW, Z30_DRAW_ORIGIN_REGION_SCROLLING_OLD, Z30_DRAW_ORIGIN_SPRITE
};

enum { Z30S_NONE, Z30S_HERO, Z30S_NPC, Z30S_LWPN, Z30S_EWPN, Z30S_ITEM, Z30S_FFC };

// Hero->ScriptTile and friends. Hero->ScriptTile is 0 when unset; the others are -1. A sprite
// reference to the Hero reads and sets the same ScriptTile and ScriptFlip (upstream's
// script_hero_sprite and script_hero_flip).
static long zasm30_hero_script_tile = 0, zasm30_hero_script_flip = -1, zasm30_hero_script_cset = -1;

#define Z30_FFC_BASE 0x7FF00000L

struct Z30Sprite
{
    sprite *s;
    int kind;
    int ffc;
    int ffc_screen;     // the FFC's screen: -1 the current one, else another of a scrolling region
};

// 3.0 scrolling regions: FFCs named across the region (q30_scripts_region.h)
static bool zasm30_ffc_from_ref(long ref, int *screen, int *index);
static long zasm30_selected_ffc_id();
static bool zasm30_region_ffc_sprite_get(const long arg, long &ret, const Z30Sprite &r);
static bool zasm30_region_ffc_sprite_set(const long arg, long value, const Z30Sprite &r);

static Z30Sprite z30_sprite(long uid)
{
    Z30Sprite r;
    r.s = NULL;
    r.kind = Z30S_NONE;
    r.ffc = -1;
    r.ffc_screen = -1;

    if(uid == 1 || uid == Link.getUID())
    {
        r.s = &Link;
        r.kind = Z30S_HERO;
        return r;
    }

    if((r.s = guys.getByUID(uid)) != NULL) { r.kind = Z30S_NPC; return r; }
    if((r.s = Lwpns.getByUID(uid)) != NULL) { r.kind = Z30S_LWPN; return r; }
    if((r.s = q30_lifted_weapon(uid)) != NULL) { r.kind = Z30S_LWPN; return r; }  // (held over Link's head)
    if((r.s = Ewpns.getByUID(uid)) != NULL) { r.kind = Z30S_EWPN; return r; }
    if((r.s = items.getByUID(uid)) != NULL) { r.kind = Z30S_ITEM; return r; }

    // an FFC (upstream's are sprites, numbered with the rest): its slot, and in a scrolling region
    // its screen (IDs go across the region there)
    if(zasm30_ffc_from_ref(uid, &r.ffc_screen, &r.ffc))
        r.kind = Z30S_FFC;
    else
        r.ffc = -1, r.ffc_screen = -1;
    return r;
}

// ffc->Width and friends are packed: low 6 bits hit size - 1, top 2 bits tile size - 1.
static int z30_ffc_hit_w(int i) { return (tmpscr->ffwidth[i] & 0x3F) + 1; }
static int z30_ffc_hit_h(int i) { return (tmpscr->ffheight[i] & 0x3F) + 1; }
static int z30_ffc_tile_w(int i) { return (tmpscr->ffwidth[i] >> 6) + 1; }
static int z30_ffc_tile_h(int i) { return (tmpscr->ffheight[i] >> 6) + 1; }

// The fractional part of a sprite position is kept only under qr_SPRITEXY_IS_FLOAT (644), and so
// far only for the Hero (as ffscript.cpp's Link->X/Y/Z); other sprites report whole pixels.
static long z30_fixreg(fix f, bool as_float)
{
    return as_float ? (long)floor(double(f) * 10000.0 + 0.5) : long(int(f)) * 10000;
}

static long z30_fl(fix f) { return (long)(double(f) * 10000.0); }

// FakeJump and FakeZ are kept as the script's own numbers; the stored jump is in the scale Jump uses inside.
static long z30_fake_jump_read(long raw) { return raw / -100; }
static long z30_fake_jump_write(long value) { return (long)(int64_t(value) * -100); }

// a weapon's FakeZ as upstream's lweapon/eweapon registers: whole pixels (zfix's rounding) unless
// qr_SPRITEXY_IS_FLOAT
static long z30_wpn_fakez_read(long raw)
{
    if(q30_quest_rule(644))
        return raw;

    return (raw / 10000 + vbound((raw % 10000) / 5000, raw < 0 ? -1 : 0, raw < 0 ? 0 : 1)) * 10000;
}

static long z30_wpn_fakez_write(long value) { return zc_max(q30_quest_rule(644) ? value : value / 10000 * 10000, 0L); }

// ---- SPRITE_* ----------------------------------------------------------------------------------

static long &z30_hero_moveflags();

// The script state of the sprite r names. An enemy's is filled in from its data first (its move
// flags), so a sprite field written before anything else asked about it isn't lost.
static Zasm30SpriteState *z30_sprite_st(const Z30Sprite &r)
{
    if(!r.s)
        return NULL;

    if(r.kind == Z30S_NPC)
        return zasm30_npc_state(r.s);

    if(r.kind == Z30S_ITEM)     // (its data's move flags first, as upstream's item gets them)
        return zasm30_item_state((item *)r.s);

    return &zasm30_sprite_state(r.s->getUID());
}

// lweapon/eweapon/item/npc/Hero->Misc[] (LWPNMISCD, EWPNMISCD, ITEMMISCD, NPCMISCD, LINKMISCD): 32
// slots, as upstream's sprites have (miscellaneous[32]), where 2.53's handlers keep 16. Slots 16-31
// live in the script state, as Sprite->Misc[] keeps them. The index as upstream's internal arrays
// take it (scripting/arrays.h): under qr_OLD_SCRIPTS_INTERNAL_ARRAYS_BOUND_INDEX the weapon and Hero
// ones clamp it and the others give their default (an npc's -10000 under
// qr_OLD_SCRIPTS_ARRAYS_NON_ZERO_DEFAULT_VALUE); without it a negative index counts from the end.
// NULL for a bad sprite or index; *dflt is what a read gives then.
static long *z30_misc32_slot(const long arg, long n, long *dflt)
{
    long uid = 0;
    int kind = Z30S_NONE;

    switch(arg)
    {
    case Z30R_LWPNMISCD: uid = ri->lwpn; kind = Z30S_LWPN; break;
    case Z30R_EWPNMISCD: uid = ri->ewpn; kind = Z30S_EWPN; break;
    case Z30R_ITEMMISCD: uid = ri->itemref; kind = Z30S_ITEM; break;
    case Z30R_NPCMISCD: uid = ri->guyref; kind = Z30S_NPC; break;
    case Z30R_LINKMISCD: uid = Link.getUID(); kind = Z30S_HERO; break;
    default: return NULL;
    }

    *dflt = 0;
    const Z30Sprite r = z30_sprite(uid);

    if(!r.s || r.kind != kind)
        return NULL;

    if(q30_compat_rule(487))        // qr_OLD_SCRIPTS_INTERNAL_ARRAYS_BOUND_INDEX
    {
        if(kind == Z30S_LWPN || kind == Z30S_EWPN || kind == Z30S_HERO)
            n = vbound(n, 0, 31);
        else if(kind == Z30S_NPC && q30_compat_rule(488))   // qr_OLD_SCRIPTS_ARRAYS_NON_ZERO_DEFAULT_VALUE
            *dflt = -10000;
    }
    else if(n < 0)
        n += 32;

    if(n < 0 || n >= 32)
        return NULL;

    if(n < 16)
        return &r.s->miscellaneous[n];

    Zasm30SpriteState *st = z30_sprite_st(r);
    return st ? &st->misc2[n - 16] : NULL;
}

static bool z30_misc32_get(const long arg, long &ret)
{
    long dflt;

    if(arg != Z30R_LWPNMISCD && arg != Z30R_EWPNMISCD && arg != Z30R_ITEMMISCD && arg != Z30R_NPCMISCD && arg != Z30R_LINKMISCD)
        return false;

    const long *p = z30_misc32_slot(arg, Z30_INDEX, &dflt);
    ret = p ? *p : dflt;
    return true;
}

static bool z30_misc32_set(const long arg, const long value)
{
    long dflt;

    if(arg != Z30R_LWPNMISCD && arg != Z30R_EWPNMISCD && arg != Z30R_ITEMMISCD && arg != Z30R_NPCMISCD && arg != Z30R_LINKMISCD)
        return false;

    if(long *p = z30_misc32_slot(arg, Z30_INDEX, &dflt))
        *p = value;

    return true;
}

static bool zasm30_sprite_read(const long arg, long &ret, const Z30Sprite &r)
{
    sprite *s = r.s;

    const bool isffc = r.kind == Z30S_FFC;
    const bool isfloat = isffc;   // 2.53 sprites have no sub-pixel position to report
    const int f = r.ffc;

    Zasm30SpriteState *st = z30_sprite_st(r);

    switch(arg)
    {
    case Z30R_SPRITE_SPAWN_SCREEN:
    {
        // the screen a sprite came from (upstream's screen_spawned): in a scrolling region the
        // screen it was tagged with, and the region's top-left screen for the Hero; else currscr
        int scr = currscr;

        if(r.kind == Z30S_HERO)
            scr = q30_region_origin();
        else if(s && q30_region_sprite_screen(s->getUID()) >= 0)
            scr = q30_region_sprite_screen(s->getUID());

        ret = scr * 10000;
        return true;
    }
    case Z30R_SPRITE_CURRENT_SCREEN:
        // the region screen under the sprite's centre (upstream's current_screen; the Hero's is
        // x + 8, y + 8); currscr outside a region
        if(!s)
            ret = currscr * 10000;
        else if(r.kind == Z30S_HERO)
            ret = q30_region_screen_at(int(s->x) + 8, int(s->y) + 8) * 10000;
        else
            ret = q30_region_screen_at(int(s->x) + s->hxofs + s->hxsz / 2, int(s->y) + s->hyofs + s->hysz / 2) * 10000;
        return true;
    // Positions in a scrolling region are region pixels (q30_region_origin_x/y; 0 elsewhere).
    // FFCs keep theirs as they are here (see the FFC position code).
    case Z30R_SPRITE_X:
        if(isffc) ret = tmpscr->ffx[f];
        else ret = z30_fixreg(s->x, r.kind == Z30S_HERO && q30_quest_rule(644)) + q30_region_origin_x() * 10000L;
        return true;
    case Z30R_SPRITE_Y:
        if(isffc) ret = tmpscr->ffy[f];
        else ret = z30_fixreg(s->y, r.kind == Z30S_HERO && q30_quest_rule(644)) + q30_region_origin_y() * 10000L;
        return true;
    case Z30R_SPRITE_Z:
        if(isffc) ret = 0;
        else ret = z30_fixreg(s->z, r.kind == Z30S_HERO && q30_quest_rule(644));
        return true;
    case Z30R_SPRITE_FAKE_Z:
        ret = st ? ((r.kind == Z30S_LWPN || r.kind == Z30S_EWPN) ? z30_wpn_fakez_read(st->fakez) : st->fakez) : 0;
        return true;
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
    case Z30R_SPRITE_SCRIPT_TILE: ret = (r.kind == Z30S_HERO ? zasm30_hero_script_tile : st ? st->script_tile : -1) * 10000; return true;
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
    case Z30R_SPRITE_GRAVITY:   // (upstream: the move_obeys_grav move flag)
        if(r.kind == Z30S_HERO) ret = (z30_hero_moveflags() & 1) ? 10000 : 0;
        else ret = (st && (st->move_flags & 1)) ? 10000 : 0;
        return true;
    case Z30R_SPRITE_GRAVITY_STRENGTH: ret = st ? st->gravity_strength : 0; return true;
    case Z30R_SPRITE_TERMINAL_VELOCITY: ret = st ? st->terminal_v : 0; return true;
    case Z30R_SPRITE_CUSTOM_GRAVITY_STRENGTH: ret = st ? st->gravity_strength : 0; return true;
    case Z30R_SPRITE_CUSTOM_TERMINAL_VELOCITY: ret = st ? st->terminal_v : 0; return true;
    case Z30R_SPRITE_FLIP: ret = s ? s->flip * 10000 : 0; return true;
    case Z30R_SPRITE_SCRIPT_FLIP: ret = (r.kind == Z30S_HERO ? zasm30_hero_script_flip : st ? st->script_flip : -1) * 10000; return true;
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
    case Z30R_SPRITE_FALL_CLK:
        if(r.kind == Z30S_NPC)  // (an enemy falling into a pit: guys.cpp's fall)
        {
            ret = q30_enemy_fall_clk(s->getUID()) * 10000;
        }
        else ret = st ? st->fall_clk * 10000 : 0;
        return true;
    case Z30R_SPRITE_DROWN_CLK: ret = st ? st->drown_clk * 10000 : 0; return true;
    case Z30R_SPRITE_FALL_CMB:
    case Z30R_SPRITE_DROWN_CMB:
        ret = zasm30_store_get(arg, Z30_REF(SPRITE), 0); return true;
    case Z30R_SPRITE_LIGHT_RADIUS: ret = st ? st->glow_rad * 10000 : 0; return true;
    case Z30R_SPRITE_LIGHT_SHAPE: ret = st ? st->glow_shape * 10000 : 0; return true;
    case Z30R_SPRITE_LIGHT_OFFSET: ret = st ? st->glow_ofs * 10000 : 0; return true;
    case Z30R_SPRITE_NOCOLL_CLK: ret = st ? st->no_colldet * 10000 : 0; return true;
    case Z30R_SPRITE_SWHOOKED: ret = q30_switch_hooked(s, isffc ? f : -1) ? 10000 : 0; return true;
    case Z30R_SPRITE_SHADOW_SPR: ret = st ? st->shadow_spr * 10000 : 0; return true;
    case Z30R_SPRITE_SHADOW_XOFS: ret = st ? st->shadow_xofs * 10000 : 0; return true;
    case Z30R_SPRITE_SHADOW_YOFS: ret = st ? st->shadow_yofs * 10000 : 0; return true;
    case Z30R_SPRITE_VIEWPORT_SUSPEND_RANGE:
    case Z30R_SPRITE_VIEWPORT_DESPAWN_RANGE: ret = 0; return true;
    case Z30R_SPRITE_MISCD:
    {
        long n = Z30_INDEX;

        if(zasm30_size_probe)       // (its size as an internal array: q30_scripts_arrays.h)
            zasm30_probed_size = isffc ? 16 : 32;

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

    // an FFC in a 3.0 scrolling region: its screen's table, its X and Y in region pixels
    if(r.kind == Z30S_FFC && q30_region_on())
        return zasm30_region_ffc_sprite_get(arg, ret, r);

    if(r.kind != Z30S_NONE)
        return zasm30_sprite_read(arg, ret, r);

    // With no sprite selected a sprite field reads 0, and any other register isn't ours to answer.
    if(!z30_is_sprite_register(arg))
        return false;

    ret = 0;
    return true;
}

static bool zasm30_sprite_write(const long arg, const long value, const Z30Sprite &r);

static bool zasm30_sprite_set(const long arg, const long value)
{
    Z30Sprite r = z30_sprite(Z30_REF(SPRITE));

    if(r.kind == Z30S_NONE)
        return z30_is_sprite_register(arg);

    // an FFC in a 3.0 scrolling region: its screen's table, its X and Y in region pixels
    if(r.kind == Z30S_FFC && q30_region_on())
        return zasm30_region_ffc_sprite_set(arg, value, r);

    return zasm30_sprite_write(arg, value, r);
}

// A sprite register written on the sprite r names.
static bool zasm30_sprite_write(const long arg, const long value, const Z30Sprite &r)
{
    sprite *s = r.s;
    const bool isffc = r.kind == Z30S_FFC;
    const bool ishero = r.kind == Z30S_HERO;
    const int f = r.ffc;
    Zasm30SpriteState *st = z30_sprite_st(r);

    switch(arg)
    {
    case Z30R_SPRITE_SPAWN_SCREEN:
    case Z30R_SPRITE_CURRENT_SCREEN: return true;
    // (region pixels in a scrolling region: the current screen's offset comes off)
    case Z30R_SPRITE_X:
        if(isffc) tmpscr->ffx[f] = value;
        else if(ishero && q30_quest_rule(644)) Link.setXfix(fix(value / 10000.0 - q30_region_origin_x()));
        else if(ishero) Link.setX(value / 10000 - q30_region_origin_x());
        else s->x = fix(value / 10000 - q30_region_origin_x());
        return true;
    case Z30R_SPRITE_Y:
        if(isffc) tmpscr->ffy[f] = value;
        else if(ishero && q30_quest_rule(644)) Link.setYfix(fix(value / 10000.0 - q30_region_origin_y()));
        else if(ishero) Link.setY(value / 10000 - q30_region_origin_y());
        else
        {
            const fix ny = fix(value / 10000 - q30_region_origin_y());

            if(r.kind == Z30S_NPC)
            {
                if(FILE *tf = q31_enemy_trace_file())
                    fprintf(tf, "%d npc Y %ld by type %d script %d pc %ld\n", frame, value / 10000, int(curScriptType), int(curScriptNum), zasm30_dbg_pc);

                ((enemy *)s)->floor_y += ny - s->y;
            }
            s->y = ny;
        }
        return true;
    case Z30R_SPRITE_Z:
        if(isffc) return true;
        if(ishero && q30_quest_rule(644)) { Link.setZfix(fix(value / 10000.0)); return true; }
        if(ishero) { Link.setZ(value / 10000); return true; }
        if(r.kind == Z30S_NPC && never_in_air(s->id)) return true;
        s->z = fix(value / 10000);
        if(s->z < 0) s->z = 0;
        return true;
    case Z30R_SPRITE_FAKE_Z:
        if(st) st->fakez = (r.kind == Z30S_LWPN || r.kind == Z30S_EWPN) ? z30_wpn_fakez_write(value) : value;
        return true;
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
    case Z30R_SPRITE_SCRIPT_TILE:
        if(r.kind == Z30S_HERO) zasm30_hero_script_tile = vbound(value / 10000, -1, NEWMAXTILES - 1);
        else if(st) st->script_tile = value / 10000;
        return true;
    // (an FFC's tile size is 1-4, upstream's vbound; it's kept in the top 2 bits of ffwidth/ffheight)
    case Z30R_SPRITE_TILE_W:
        if(isffc) tmpscr->ffwidth[f] = (byte)((tmpscr->ffwidth[f] & 0x3F) | ((vbound(value / 10000, 1, 4) - 1) << 6));
        else if(s) s->txsz = vbound(value / 10000, 1, 20);
        return true;
    case Z30R_SPRITE_TILE_H:
        if(isffc) tmpscr->ffheight[f] = (byte)((tmpscr->ffheight[f] & 0x3F) | ((vbound(value / 10000, 1, 4) - 1) << 6));
        else if(s) s->tysz = vbound(value / 10000, 1, 20);
        return true;
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
    case Z30R_SPRITE_GRAVITY:
        if(ishero) z30_hero_moveflags() = value ? (z30_hero_moveflags() | 1) : (z30_hero_moveflags() & ~1L);
        else if(st) st->move_flags = value ? (st->move_flags | 1) : (st->move_flags & ~1L);
        return true;
    case Z30R_SPRITE_CUSTOM_GRAVITY_STRENGTH: if(st) st->gravity_strength = value; return true;
    case Z30R_SPRITE_CUSTOM_TERMINAL_VELOCITY: if(st) st->terminal_v = value; return true;
    case Z30R_SPRITE_FLIP: if(s) s->flip = value / 10000; return true;
    case Z30R_SPRITE_SCRIPT_FLIP:
        if(r.kind == Z30S_HERO) zasm30_hero_script_flip = vbound(value / 10000, -1, 256);
        else if(st) st->script_flip = vbound(value / 10000, -1, 127);
        return true;
    case Z30R_SPRITE_ENGINE_ANIMATE: if(st) st->engine_animate = value != 0; return true;
    case Z30R_SPRITE_EXTEND: if(s) s->extend = value / 10000; return true;
    // (an FFC's hit size: the low 6 bits of ffwidth/ffheight hold 1-64 pixels)
    case Z30R_SPRITE_HIT_WIDTH:
        if(isffc) tmpscr->ffwidth[f] = (byte)((tmpscr->ffwidth[f] & ~0x3F) | ((vbound(value / 10000, 1, 64) - 1) & 0x3F));
        else if(s) s->hxsz = value / 10000;
        return true;
    case Z30R_SPRITE_HIT_HEIGHT:
        if(isffc) tmpscr->ffheight[f] = (byte)((tmpscr->ffheight[f] & ~0x3F) | ((vbound(value / 10000, 1, 64) - 1) & 0x3F));
        else if(s) s->hysz = value / 10000;
        return true;
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

// A mapdata reference: 0 is none, above 0 the map screen (map*MAPSCRS+scr) + 1, below 0 a
// temporary one as upstream's create_mapdata_temp_ref makes: -(flags | screen << 8 | layer << 16)
// - 1, flags 1 a scrolling screen (none here), 2 the whole current region (no screen given).
static long z30_md_tmp_ref(bool region, int screen, int layer)
{
    long r = (region ? 2L : 0L) | ((long)(layer & 0xFF) << 16);

    if(!region)
        r |= (long)(screen & 0xFF) << 8;

    return -r - 1;
}
static long z30_md_bits(long ref) { return ref < 0 ? -(ref + 1) : 0; }
static int z30_md_layer(long ref) { return (int)((z30_md_bits(ref) >> 16) & 0xFF); }
static int z30_md_screen(long ref) { return (int)((z30_md_bits(ref) >> 8) & 0xFF); }
static bool z30_md_region(long ref) { return ref < 0 && (z30_md_bits(ref) & 3) == 2; }   // (whole region)
static bool z30_md_one(long ref) { return ref < 0 && (z30_md_bits(ref) & 3) == 0; }      // (one screen)
// a temporary mapdata of the screen being played (the region's, or the one screen currscr)
static bool z30_md_here(long ref) { return z30_md_region(ref) || (z30_md_one(ref) && z30_md_screen(ref) == currscr); }

static mapscr *z30_mapdata(long ref)
{
    if(ref == 0)
        return NULL;

    if(ref < 0)
    {
        const int layer = z30_md_layer(ref);

        if(layer > 6)
            return NULL;

        if(z30_md_one(ref))     // one screen of the region (q30_scripts_sys.h)
        {
            const int index = q30_region_screen_index(z30_md_screen(ref));
            int pos;
            bool current;
            return index < 0 ? NULL : q30_region_rpos_screen(index * 176, layer, &pos, &current);
        }

        if(!z30_md_region(ref))
            return NULL;
        return layer ? &tmpscr2[layer - 1] : tmpscr;
    }

    const long index = ref - 1;
    return index < (long)TheMaps.size() ? &TheMaps[index] : NULL;
}

// The screen holding combo n of mapdata `ref` (->ComboD[] and the other combo arrays), with n
// made the position on it; NULL: no such combo. A Game->LoadTempScreen(layer) mapdata covers the
// whole scrolling region, as upstream's (TemporaryCurrentRegion), so in one its index is a combo
// position across the region (q30_region_rpos_screen); any other mapdata is 0..175.
static mapscr *z30_mapdata_combo(long ref, long &n)
{
    if(z30_md_region(ref) && q30_region_on())
    {
        int pos;
        bool current;
        mapscr *m = q30_region_rpos_screen((int)n, z30_md_layer(ref), &pos, &current);

        if(m)
            n = pos;

        return m && n < (long)m->data.size() ? m : NULL;
    }

    mapscr *m = z30_mapdata(ref);
    return (m && n >= 0 && n < 176) ? m : NULL;
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
// itemdata->Flags[n]: bits 4, 5, 10-22 and 23 (upstream's item_flags), the low 16 in the item
// itself and the rest beside it (q30_set_item_flags32)
static dword z30_idata_flag_bit(long n)
{
    return n == 0 ? 0x10u : n == 1 ? 0x20u : (dword)1 << (n + 8);
}

static dword z30_idata_flags(long id)
{
    return (q30_item_flags((int)id) & ~0xFFFFu) | Q30_IDATA(id).flags;
}

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

// (Hero->ScriptTile and friends: at the top, the sprite registers reach them too)
static long zasm30_hero_move_flags = 0;
static long &z30_hero_moveflags() { return zasm30_hero_move_flags; }
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
static weapon *z30_lwpn_ref()
{
    weapon *w = (weapon *)Lwpns.getByUID(ri->lwpn);
    return w ? w : q30_lifted_weapon(ri->lwpn);     // (the one Link holds up is an lweapon too)
}
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

// a mapdata's FFC slots, as upstream's (a stored screen without FFCs past 32 reads 0 there)
static const int Z30_FFC_COUNT = Q30_MAXFFCS;

// Before a mapdata write to FFC slot n: a screen that had only 32 gets the rest, as upstream's
// screens grow to the FFC a script names.
static mapscr *z30_ffc_room(mapscr *m, long n)
{
    if(m && n >= m->ffc_count() && n < Q30_MAXFFCS)
        m->q30_extend_ffcs();

    return m;
}

// Which map screen a mapdata reference is, for state kept per screen; -1 if it has none.
static int z30_mapdata_mi(long ref)
{
    if(ref == 0)
        return -1;

    // (the screen being played has no saved state of its own in a cave or passageway, screens
    // 0x80 and up: upstream reads and sets nothing there, where this indexed the next map's)
    if(ref < 0)
    {
        const int scr = z30_md_region(ref) ? currscr : z30_md_one(ref) ? z30_md_screen(ref) : -1;
        return (z30_md_layer(ref) == 0 && scr >= 0 && scr < MAPSCRSNORMAL && q30_region_screen_index(scr) >= 0)
               ? currmap * MAPSCRSNORMAL + scr : -1;
    }

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
static bool zasm30_live_get(const long arg, long &ret);      // q30_scripts_live.h
static bool zasm30_live_set(const long arg, const long value);
static void zasm30_live_reset();
void q30_auto_rotate(sprite *s, Zasm30SpriteState *st);
static bool zasm30_scrdata_set(const long arg, const long value);
static bool zasm30_data_reg(const long arg, long &ret, const long value, const bool write);  // q30_scripts_data.h
static bool zasm30_sub_set(const long arg, const long value);
static bool zasm30_sys_get(const long arg, long &ret);       // q30_scripts_sys.h
static bool zasm30_sys_set(const long arg, const long value);

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
        return (id >= 0 && id < q30_items_end()) ? std::string(q30_item_name(id)) : std::string();

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
// CONTINUE, SAVE, RETRY (the game over screen), DON'T SAVE, SAVE AND QUIT, SAVE, QUIT (the save
// screen); empty for the default
static std::string zasm30_gameover_text[7];

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

    for(int i = 0; i < 7; ++i)
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

// Game->GameOverStrings[]: the game over screen's three lines and the save screen's four.
static void z30_gameover_text_write(int index, long ptr)
{
    std::string s;
    ArrayH::getString(zasm30_aptr(ptr), s, 73);

    if(s.size() > 31)
        s.resize(31);

    index = vbound(index, 0, 6);
    zasm30_gameover_text[index] = s;
    zasm30_gameover_set = true;
}

// title.cpp's game_over and save_game: a colour or other setting, or the text of a line (0..6, as
// upstream's SaveScreenText; NULL for the default).
bool q30_gameover_custom() { return zasm30_gameover_set; }
int q30_gameover_setting(int index) { return (index >= 0 && index < Z30_GOS_LAST) ? zasm30_gameover[index] : 0; }
const char *q30_gameover_text(int line)
{
    return (line >= 0 && line < 7 && !zasm30_gameover_text[line].empty()) ? zasm30_gameover_text[line].c_str() : NULL;
}

// Hero->ShieldJinx (upstream's shieldjinxclk): while not 0 the shield blocks nothing; it counts
// down each frame, and -1 lasts until cured. link.cpp asks q30_shield_jinxed() before blocking.
static int zasm30_shield_jinx = 0;

bool q30_shield_jinxed() { return (zasm30_ffscript_version || q30_active()) && zasm30_shield_jinx != 0; }

const Q30GuyData *q30_guy_data(int guy);    // q30_scripts_data.h

// A bubble enemy's touch (upstream hero.cpp, eeWALK): with the shield among what it jinxes (its
// attribute 8's e8tSHIELD, 4) it jinxes the shield for 150 frames (by the Whisp Ring's divisor),
// for good (or 150 frames with the ring's flag 1), or cures it, as it does the sword.
void q30_bubble_shield_jinx(int guy, int dm7, int divisor, bool ringflag1)
{
    const Q30GuyData *x = q30_guy_data(guy);

    if(!x || !(x->attributes[7] & 4))
        return;

    switch(dm7)
    {
    case e7tTEMPJINX: if(zasm30_shield_jinx >= 0 && divisor) zasm30_shield_jinx = 150 / divisor; break;
    case e7tPERMJINX: if(divisor) zasm30_shield_jinx = ringflag1 ? 150 / divisor : -1; break;
    case e7tUNJINX: zasm30_shield_jinx = 0; break;
    }
}

// the jinx counts down with Link's other jinxes in a quest without 3.0 scripts (with them: at the
// start of the frame, zasm30_frame_start)
void q30_shield_jinx_tick()
{
    if(!zasm30_ffscript_version && zasm30_shield_jinx > 0)
        --zasm30_shield_jinx;
}

// LinkClass::resetflags: jinxes that count down are cured, and all of them with 'all'.
void q30_shield_jinx_reset(bool all)
{
    if(zasm30_shield_jinx > 0 || all)
        zasm30_shield_jinx = 0;
}

// Viewport->: the camera's state is kept with it (q30_regions.cpp); the follow registers by number.
static int z30_viewport_follow_reg(long arg)
{
    switch(arg)
    {
    case Z30R_VIEWPORT_DEADZONE_WIDTH: return Q30_VP_DEADZONE_W;
    case Z30R_VIEWPORT_DEADZONE_HEIGHT: return Q30_VP_DEADZONE_H;
    case Z30R_VIEWPORT_LOOKAHEAD_X: return Q30_VP_LOOKAHEAD_X;
    case Z30R_VIEWPORT_LOOKAHEAD_Y: return Q30_VP_LOOKAHEAD_Y;
    case Z30R_VIEWPORT_LOOKAHEAD_SPEED: return Q30_VP_LOOKAHEAD_SPEED;
    case Z30R_VIEWPORT_RECENTER_SPEED: return Q30_VP_RECENTER_SPEED;
    case Z30R_VIEWPORT_RECENTER_DELAY: return Q30_VP_RECENTER_DELAY;
    }

    return -1;
}

// upstream's BC::checkBounds for the Viewport-> setters
static bool z30_viewport_in(long v, long lo, long hi)
{
    if(v >= lo && v <= hi)
        return true;

    Z_scripterrlog("Invalid value: %ld - must be >= %ld and <= %ld\n", v, lo, hi);
    return false;
}

// the camera for Viewport->Target (Hero for 1, NULL when the sprite is gone)
sprite *q30_viewport_sprite(long uid)
{
    return z30_sprite(uid).s;
}

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

// ---- combo triggers -----------------------------------------------------------------------------
// combodata->Trig* and the combodata trigger arrays name the combo's first trigger (upstream's
// get_first_combo_trigger, which gives a combo without one a new trigger, on reads too);
// combotrigger-> names the trigger its reference holds, combo id | index << 24 (upstream's get_combo_trigger). The bounds
// are upstream's (combodata.cpp and combotrigger.cpp: a few differ).

// qr_OLD_SCRIPTS_INTERNAL_ARRAYS_BOUND_INDEX, qr_OLD_SCRIPTS_ARRAYS_NON_ZERO_DEFAULT_VALUE: upstream
// sets both for compat rule versions before 91 (q30_types.cpp's table).
enum { Z30_QR_OLD_ARRAY_INDEX = 487, Z30_QR_OLD_ARRAY_DEFAULT = 488 };

// Upstream's internal arrays (zc/scripting/arrays.h resolveIndex): an index past the end fails; a
// negative one counts from the end, unless Z30_QR_OLD_ARRAY_INDEX, which fails it too.
static bool z30_array_index(long index, long size, long &out)
{
    if(zasm30_size_probe)       // (an internal array's size: q30_scripts_arrays.h)
    {
        zasm30_probed_size = size;
        return false;
    }

    if(index >= 0 && index < size)
        out = index;
    else if(index < 0 && size + index >= 0 && !q30_compat_rule(Z30_QR_OLD_ARRAY_INDEX))
        out = size + index;
    else
    {
        Z_scripterrlog("Invalid array index %ld for internal array of size %ld\n", index, size);
        return false;
    }

    return true;
}

// What a failed read of an internal array gives: 0, or the array's old value under
// Z30_QR_OLD_ARRAY_DEFAULT (only some arrays have one other than 0).
static long z30_array_default(long old_value)
{
    return q30_compat_rule(Z30_QR_OLD_ARRAY_DEFAULT) ? old_value : 0;
}

static void z30_bits_put(Q30Bits &bits, size_t i, bool on)
{
    if(on)
        bits.set(i);
    else if(i / 8 < bits.v.size())
        bits.v[i / 8] &= (byte)~(1 << (i % 8));
}

// Scripts' colours are 0-255, or 0-63 under qr_SCRIPTS_6_BIT_COLOR (z30_color_max, q30_scripts_cmds.h);
// a trigger's tint is kept 8-bit (upstream's scripting_write_pal_color: a 6-bit value is scaled up as
// _rgb_scale_6 does, keeping its sign). Reads give the kept value as it is, as upstream's do.
static short z30_trig_tint_from_script(long v)
{
    const int max = q30_quest_rule(480) ? 63 : 255;     // qr_SCRIPTS_6_BIT_COLOR
    const int c = (int)vbound(v, (long)-max, (long)max);

    if(max == 255)
        return (short)c;

    const int a = c < 0 ? -c : c;
    const int scaled = (a << 2) | (a >> 4);
    return (short)(c < 0 ? -scaled : scaled);
}

enum { Z30_MAX_ITEMS_UP = 1024, Z30_MAX_SFX_UP = 1024, Z30_MAX_LEVELS_UP = 512, Z30_MAX_COUNTERS_UP = 107, Z30_SSC_MIN = -18,
       Z30_IP_ALLOWED = ipHOLDUP | ipTIMER | 4096 | 8192, Z30_LI_ALL_UP = 0xFFFF, Z30_MAX_COMBO_TRIGGERS = 255,
       Z30_MUSIC_UPDATE_REGION = 4 };

// X(combodata register, combotrigger register, field, combodata low, high, combotrigger low, high)
#define Z30_TRIG_BOTH(X) \
    X(Z30R_COMBODTRIGBOSSPAL, Z30R_CMBTRIGBOSSPAL, trigbosspalette, -1, 29, -1, 29) \
    X(Z30R_COMBODTRIGBUNNY, Z30R_CMBTRIGBUNNY, trig_bunnytime, -2, 32767, -2, 32767) \
    X(Z30R_COMBODTRIGCSETCHANGE, Z30R_CMBTRIGCSETCHANGE, trigcschange, -15, 15, -128, 127) \
    X(Z30R_COMBODTRIGDMAPLVL, Z30R_CMBTRIGDMAPLVL, trigdmlevel, -1, Z30_MAX_LEVELS_UP - 1, -1, Z30_MAX_LEVELS_UP - 1) \
    X(Z30R_COMBODTRIGEXDOORDIR, Z30R_CMBTRIGEXDOORDIR, exdoor_dir, -1, 3, -1, 3) \
    X(Z30R_COMBODTRIGEXDOORIND, Z30R_CMBTRIGEXDOORIND, exdoor_ind, 0, 7, 0, 7) \
    X(Z30R_COMBODTRIGEXSTATE, Z30R_CMBTRIGEXSTATE, exstate, -1, 31, -1, 31) \
    X(Z30R_COMBODTRIGGERCHANGECMB, Z30R_CMBTRIGCHANGECMB, trigchange, -65535, 65535, -214748, 214748) \
    X(Z30R_COMBODTRIGGERCOOLDOWN, Z30R_CMBTRIGCOOLDOWN, trigcooldown, 0, 255, 0, 255) \
    X(Z30R_COMBODTRIGGERCOPYCAT, Z30R_CMBTRIGCOPYCAT, trigcopycat, 0, 255, 0, 255) \
    X(Z30R_COMBODTRIGGERCTR, Z30R_CMBTRIGCTR, trigctr, Z30_SSC_MIN, Z30_MAX_COUNTERS_UP - 1, Z30_SSC_MIN, Z30_MAX_COUNTERS_UP - 1) \
    X(Z30R_COMBODTRIGGERCTRAMNT, Z30R_CMBTRIGCTRAMNT, trigctramnt, -65535, 65535, -65535, 65535) \
    X(Z30R_COMBODTRIGGERGENSCRIPT, Z30R_CMBTRIGGENSCRIPT, trig_genscr, 0, 65535, 0, 65535) \
    X(Z30R_COMBODTRIGGERGROUP, Z30R_CMBTRIGGROUP, trig_group, 0, 255, 0, 255) \
    X(Z30R_COMBODTRIGGERGROUPVAL, Z30R_CMBTRIGGROUPVAL, trig_group_val, 0, 65535, 0, 65535) \
    X(Z30R_COMBODTRIGGERGSTATE, Z30R_CMBTRIGGSTATE, trig_gstate, 0, 255, 0, 255) \
    X(Z30R_COMBODTRIGGERGTIMER, Z30R_CMBTRIGGTIMER, trig_statetime, 0, 214748, 0, 214748) \
    X(Z30R_COMBODTRIGGERITEM, Z30R_CMBTRIGREQITEM, triggeritem, 0, Z30_MAX_ITEMS_UP - 1, 0, Z30_MAX_ITEMS_UP - 1) \
    X(Z30R_COMBODTRIGGERLEVEL, Z30R_CMBTRIGWPNLEVEL, triggerlevel, 0, 214747, 0, 214748) \
    X(Z30R_COMBODTRIGGERLIGHTBEAM, Z30R_CMBTRIGLIGHTBEAM, triglbeam, 0, 32, 0, 32) \
    X(Z30R_COMBODTRIGGERLSTATE, Z30R_CMBTRIGLSTATE, trig_lstate, 0, 31, 0, 31) \
    X(Z30R_COMBODTRIGGERPROX, Z30R_CMBTRIGPROX, trigprox, 0, 65535, 0, 65535) \
    X(Z30R_COMBODTRIGGERSFX, Z30R_CMBTRIGSFX, trigsfx, 0, Z30_MAX_SFX_UP, 0, Z30_MAX_SFX_UP) \
    X(Z30R_COMBODTRIGGERTIMER, Z30R_CMBTRIGTIMER, trigtimer, 0, 65535, 0, 65535) \
    X(Z30R_COMBODTRIGITEMJINX, Z30R_CMBTRIGITEMJINX, trig_itmjinxtime, -2, 32767, -2, 32767) \
    X(Z30R_COMBODTRIGLVLPAL, Z30R_CMBTRIGLVLPAL, triglvlpalette, -1, 512, -1, 512) \
    X(Z30R_COMBODTRIGPUSHTIME, Z30R_CMBTRIGPUSHTIME, trig_pushtime, 0, 255, 0, 255) \
    X(Z30R_COMBODTRIGQUAKETIME, Z30R_CMBTRIGQUAKETIME, trigquaketime, -1, 32767, -1, 32767) \
    X(Z30R_COMBODTRIGSHIELDJINX, Z30R_CMBTRIGSHIELDJINX, trig_shieldjinxtime, -2, 32767, -2, 32767) \
    X(Z30R_COMBODTRIGSPAWNENEMY, Z30R_CMBTRIGSPAWNENEMY, spawnenemy, 0, 511, 0, 511) \
    X(Z30R_COMBODTRIGSPAWNITEM, Z30R_CMBTRIGSPAWNITEM, spawnitem, -(MAXITEMDROPSETS - 1), Z30_MAX_ITEMS_UP - 1, -(MAXITEMDROPSETS - 1), Z30_MAX_ITEMS_UP - 1) \
    X(Z30R_COMBODTRIGSTUN, Z30R_CMBTRIGSTUN, trig_stuntime, -2, 32767, -2, 32767) \
    X(Z30R_COMBODTRIGSWORDJINX, Z30R_CMBTRIGSWORDJINX, trig_swjinxtime, -2, 32767, -2, 32767) \
    X(Z30R_COMBODTRIGWAVYTIME, Z30R_CMBTRIGWAVYTIME, trigwavytime, -1, 32767, -1, 32767)

// M(combodata register, combotrigger register, field, mask)
#define Z30_TRIG_BOTH_MASK(M) \
    M(Z30R_COMBODTRIGITEMPICKUP, Z30R_CMBTRIGITEMPICKUP, spawnip, Z30_IP_ALLOWED) \
    M(Z30R_COMBODTRIGLITEMS, Z30R_CMBTRIGLITEMS, trig_levelitems, Z30_LI_ALL_UP)

// C(combotrigger register, field, low, high): combotrigger-> only
#define Z30_TRIG_CT(C) \
    C(Z30R_CMBTRIG_LARGECOMBO_COPYCAT, large_combo_copycat, 0, 255) \
    C(Z30R_CMBTRIGPOSSTATE, combopos_state, 0, 7) \
    C(Z30R_CMBTRIGGERFAILPROMPTCID, fail_prompt_cid, 0, MAXCOMBOS - 1) \
    C(Z30R_CMBTRIGGERFAILSTR, fail_msgstr, 0, msg_count - 1) \
    C(Z30R_CMBTRIGGERFORCEPLAYERDIR, dest_player_dir, -1, 3) \
    C(Z30R_CMBTRIGGERICECOMBO, force_ice_combo, -1, MAXCOMBOS - 1) \
    C(Z30R_CMBTRIGGERPROMPTCID, prompt_cid, 0, MAXCOMBOS - 1) \
    C(Z30R_CMBTRIGGERPROMPTX, prompt_x, -32768, 32767) \
    C(Z30R_CMBTRIGGERPROMPTY, prompt_y, -32768, 32767) \
    C(Z30R_CMBTRIGGERTRIGSTR, trig_msgstr, 0, msg_count - 1) \
    C(Z30R_CMBTRIGGER_VIEWPORT_RANGE, viewport_cond_range, -32768, 32767) \
    C(Z30R_CMBTRIGGER_CHANCE_NUMERATOR, chance_numerator, 0, 214748) \
    C(Z30R_CMBTRIGGER_CHANCE_DENOMINATOR, chance_denominator, 1, 214748) \
    C(Z30R_CMBTRIGGER_SFX_PAN, sfx_pan, -1, 255) \
    C(Z30R_CMBTRIGGER_SFX_VOLUME, sfx_volume, 0, 255) \
    C(Z30R_CMBTRIGGER_SFX_FREQUENCY, sfx_frequency, -1, 214748) \
    C(Z30R_CMBTRIGREQSTATEMAP, trigstatemap, 0, (map_count < 255 ? map_count : 255)) \
    C(Z30R_CMBTRIGREQSTATESCREEN, trigstatescreen, 0, 127)

// K(combotrigger register, field, mask)
#define Z30_TRIG_CT_MASK(K) \
    K(Z30R_CMBTRIGGERFAILPROMPTCS, fail_prompt_cs, 15) \
    K(Z30R_CMBTRIGGERPROMPTCS, prompt_cs, 15)

// F(combotrigger register, field): zfix values, kept as the script's own number
#define Z30_TRIG_CT_FIX(F) \
    F(Z30R_CMBTRIGGERDESTHEROX, dest_player_x) F(Z30R_CMBTRIGGERDESTHEROY, dest_player_y) \
    F(Z30R_CMBTRIGGERDESTHEROZ, dest_player_z) F(Z30R_CMBTRIGGERICEVX, force_ice_vx) \
    F(Z30R_CMBTRIGGERICEVY, force_ice_vy) F(Z30R_CMBTRIGGERPLAYERBOUNCE, player_bounce) \
    F(Z30R_CMBTRIGGERREQPLAYERJUMP, req_player_jump) F(Z30R_CMBTRIGGERREQPLAYERX, req_player_x) \
    F(Z30R_CMBTRIGGERREQPLAYERY, req_player_y) F(Z30R_CMBTRIGGERREQPLAYERZ, req_player_z) \
    F(Z30R_CMBTRIGGER_GRAVITY, trig_gravity) F(Z30R_CMBTRIGGER_TERMINAL_VELOCITY, trig_terminal_v)

enum { Z30TRIG_NONE, Z30TRIG_CD, Z30TRIG_CT };

static int z30_trig_reg_kind(long arg)
{
#define Z30_K_BOTH(cd, ct, f, a, b, c, d) case cd: return Z30TRIG_CD; case ct: return Z30TRIG_CT;
#define Z30_K_BOTHM(cd, ct, f, m) case cd: return Z30TRIG_CD; case ct: return Z30TRIG_CT;
#define Z30_K_CT(ct, f, a, b) case ct: return Z30TRIG_CT;
#define Z30_K_CTM(ct, f, m) case ct: return Z30TRIG_CT;
#define Z30_K_FIX(ct, f) case ct: return Z30TRIG_CT;
    switch(arg)
    {
    Z30_TRIG_BOTH(Z30_K_BOTH)
    Z30_TRIG_BOTH_MASK(Z30_K_BOTHM)
    Z30_TRIG_CT(Z30_K_CT)
    Z30_TRIG_CT_MASK(Z30_K_CTM)
    Z30_TRIG_CT_FIX(Z30_K_FIX)
    case Z30R_COMBODTRIGTINTR: case Z30R_COMBODTRIGTINTG: case Z30R_COMBODTRIGTINTB:
    case Z30R_COMBODTRIGGERBUTTON: case Z30R_COMBODTRIGGERFLAGS: case Z30R_COMBODTRIGGERFLAGS2: case Z30R_COMBODTRIGGERS:
    case Z30R_COMBODNUMTRIGGERS: case Z30R_COMBODONLYGEN:
        return Z30TRIG_CD;
    case Z30R_CMBTRIGTINTR: case Z30R_CMBTRIGTINTG: case Z30R_CMBTRIGTINTB: case Z30R_CMBTRIG_MUSIC_REFRESH:
    case Z30R_CMBTRIG_PLAY_MUSIC: case Z30R_CMBTRIG_REQ_SCREXDOOR: case Z30R_CMBTRIG_UNREQ_SCREXDOOR:
    case Z30R_CMBTRIGBUTTON: case Z30R_CMBTRIGREQLVLSTATE: case Z30R_CMBTRIGUNREQLVLSTATE: case Z30R_CMBTRIGREQGLOBALSTATE:
    case Z30R_CMBTRIGUNREQGLOBALSTATE: case Z30R_CMBTRIGREQSCRSTATE: case Z30R_CMBTRIGUNREQSCRSTATE:
    case Z30R_CMBTRIGREQSCREXSTATE: case Z30R_CMBTRIGUNREQSCREXSTATE: case Z30R_CMBTRIGGERREQPLAYERDIR: case Z30R_CMBTRIGFLAGS:
        return Z30TRIG_CT;
    default:
        return Z30TRIG_NONE;
    }
#undef Z30_K_BOTH
#undef Z30_K_BOTHM
#undef Z30_K_CT
#undef Z30_K_CTM
#undef Z30_K_FIX
}

// The trigger a combotrigger reference names (NULL: none such), and its combo.
static Q30Trigger *z30_trigger_by_ref(long ref, Q30Combo **combo)
{
    const long cid = ref & 0x00FFFFFF;
    const size_t idx = (size_t)((ref >> 24) & 0xFF);
    const Q30Combo *c = (cid >= 0 && cid < MAXCOMBOS) ? q30_combo((int)cid) : NULL;

    if(!c || idx >= c->triggers.size())
    {
        Z_scripterrlog("Invalid combotrigger ID: %d-%ld\n", (int)idx, cid);
        return NULL;
    }

    if(combo)
        *combo = const_cast<Q30Combo *>(c);
    return const_cast<Q30Trigger *>(&c->triggers[idx]);
}

// A combo's first trigger for combodata->, given one if it has none, for reads too (as upstream's
// get_first_combo_trigger): NULL for a combo id out of range.
static Q30Trigger *z30_first_trigger(long cid, Q30Combo **combo)
{
    Q30Combo *cc = (cid >= 0 && cid < MAXCOMBOS) ? q30_combo_for_write((int)cid) : NULL;

    if(!cc)
        return NULL;

    if(cc->triggers.empty())
        cc->triggers.push_back(Q30Trigger());

    if(combo)
        *combo = cc;
    return &cc->triggers[0];
}

// What upstream's combotrigger-> getters give for a zfix field: the zfix as a whole number (its
// int conversion, rounding halves away from 0), not the script's own fixed number.
static long z30_trig_fix_get(long raw)
{
    const long d = (raw % 10000) / 5000;
    return raw / 10000 + (raw < 0 ? (d < -1 ? -1 : d > 0 ? 0 : d) : (d < 0 ? 0 : d > 1 ? 1 : d));
}

static bool zasm30_trigger_reg(const long arg, long &ret, const long value, const bool write)
{
    const int kind = z30_trig_reg_kind(arg);

    if(kind == Z30TRIG_NONE)
        return false;

    const bool ct = kind == Z30TRIG_CT;
    const long v = value / 10000;
    const long cid = Z30_REF(COMBODATA);
    Q30Combo *combo = NULL;

    // -- combodata-> arrays, NumTriggers and OnlyGenTriggers -----------------------------------------
    switch(arg)
    {
    case Z30R_COMBODNUMTRIGGERS:    // writing resizes Triggers[]
    case Z30R_COMBODONLYGEN:
    {
        const bool num = arg == Z30R_COMBODNUMTRIGGERS;

        if(cid < 0 || cid >= MAXCOMBOS)
        {
            if(!write)
                ret = num ? -10000 : 0;
            return true;
        }

        if(!write)
        {
            const Q30Combo *c = q30_combo((int)cid);
            ret = num ? (c ? (long)c->triggers.size() * 10000 : 0) : (c && c->only_gentrig ? 10000 : 0);
        }
        else if(Q30Combo *cc = q30_combo_for_write((int)cid))
        {
            if(num)
                cc->triggers.resize((size_t)vbound(v, 0L, (long)Z30_MAX_COMBO_TRIGGERS));
            else
                cc->only_gentrig = value != 0 ? 1 : 0;
        }
        return true;
    }
    case Z30R_COMBODTRIGGERS:   // combotrigger[] Triggers: references, read only
    {
        const Q30Combo *c = (cid >= 0 && cid < MAXCOMBOS) ? q30_combo((int)cid) : NULL;
        const long size = c ? (long)(c->triggers.size() < (size_t)Z30_MAX_COMBO_TRIGGERS ? c->triggers.size() : Z30_MAX_COMBO_TRIGGERS) : 0;
        long n;

        if(write)
            return true;

        ret = z30_array_index(Z30_INDEX, size, n) ? (long)(dword(cid) | (dword(n) << 24)) : z30_array_default(-10000);
        return true;
    }
    case Z30R_COMBODTRIGGERFLAGS:   // long[6] TriggerFlags: the old flag sets, 32 flags each
    {
        // (upstream gives a combo without a trigger one here, for reads too)
        Q30Trigger *t = (cid >= 0 && cid < MAXCOMBOS) ? z30_first_trigger(cid, &combo) : NULL;
        long n;

        if(!t || !z30_array_index(Z30_INDEX, t ? 6 : 0, n))
        {
            if(!write)
                ret = z30_array_default(-10000);
            return true;
        }

        if(write)
        {
            const long bits = vbound(v, 0L, 214747L);

            for(int q = 0; q < 32; ++q)
                z30_bits_put(t->flags, n * 32 + q, (bits & (1L << q)) != 0);
            if(n == 0)
                combo->only_gentrig = t->flags.get(Q30TF_ONLYGENTRIG) ? 1 : 0;
        }
        else
        {
            dword bits = 0;

            for(int q = 0; q < 32; ++q)
                if(t->flags.get(n * 32 + q))
                    bits |= 1u << q;
            if(n == 0)
            {
                bits &= ~(1u << (Q30TF_ONLYGENTRIG % 32));
                if(combo->only_gentrig)
                    bits |= 1u << (Q30TF_ONLYGENTRIG % 32);
            }
            ret = (long)(int32_t)(bits * 10000u);     // (wraps, as upstream's int multiply does)
        }
        return true;
    }
    case Z30R_COMBODTRIGGERBUTTON:  // bool[8] TrigButton
    case Z30R_COMBODTRIGGERFLAGS2:  // bool[] TrigFlags
    {
        Q30Trigger *t = z30_first_trigger(cid, &combo);
        const long size = arg == Z30R_COMBODTRIGGERBUTTON ? 8 : 32 * 6;
        long n;

        if(!t)
        {
            if(!write)
                ret = z30_array_default(0);
            return true;
        }

        if(!z30_array_index(Z30_INDEX, size, n))
        {
            if(!write)
                ret = z30_array_default(0);
            return true;
        }

        if(arg == Z30R_COMBODTRIGGERBUTTON)
        {
            if(write)
                t->triggerbtn = (byte)(value ? (t->triggerbtn | (1 << n)) : (t->triggerbtn & ~(1 << n)));
            else
                ret = (t->triggerbtn & (1 << n)) ? 10000 : 0;
            return true;
        }

        const Q30Combo *c = q30_combo((int)cid);

        if(write)
        {
            z30_bits_put(t->flags, n, value != 0);
            if(n == Q30TF_ONLYGENTRIG)
                combo->only_gentrig = value != 0;
        }
        else if(n == Q30TF_ONLYGENTRIG)
            ret = (c && c->only_gentrig) ? 10000 : 0;
        else
            ret = t->flags.get(n) ? 10000 : 0;
        return true;
    }
    default:
        break;
    }

    // -- the trigger ---------------------------------------------------------------------------------
    Q30Trigger *t = ct ? z30_trigger_by_ref(Z30_REF(COMBOTRIGGER), &combo) : z30_first_trigger(cid, &combo);

    if(!t)
    {
        if(!write)
        {
            switch(arg)
            {
            // (upstream leaves these at 0 for no trigger)
            case Z30R_COMBODTRIGTINTR: case Z30R_COMBODTRIGTINTG: case Z30R_COMBODTRIGTINTB:
            case Z30R_CMBTRIG_MUSIC_REFRESH: case Z30R_CMBTRIG_REQ_SCREXDOOR: case Z30R_CMBTRIG_UNREQ_SCREXDOOR:
                ret = 0;
                break;
            case Z30R_CMBTRIG_PLAY_MUSIC: ret = -2; break;
            case Z30R_CMBTRIGBUTTON: case Z30R_CMBTRIGREQLVLSTATE: case Z30R_CMBTRIGUNREQLVLSTATE: case Z30R_CMBTRIGREQGLOBALSTATE:
            case Z30R_CMBTRIGUNREQGLOBALSTATE: case Z30R_CMBTRIGREQSCRSTATE: case Z30R_CMBTRIGUNREQSCRSTATE:
            case Z30R_CMBTRIGREQSCREXSTATE: case Z30R_CMBTRIGUNREQSCREXSTATE: case Z30R_CMBTRIGGERREQPLAYERDIR:
            case Z30R_CMBTRIGFLAGS:
                ret = 0;
                break;
            default: ret = -10000; break;
            }
        }
        return true;
    }

#define Z30_V_BOTH(cd, ctr, f, a, b, c, d) \
    case cd: case ctr: \
        if(write) t->f = (decltype(t->f))(ct ? vbound(v, (long)(c), (long)(d)) : vbound(v, (long)(a), (long)(b))); \
        else ret = long(t->f) * 10000; \
        return true;
#define Z30_V_BOTHM(cd, ctr, f, m) \
    case cd: case ctr: \
        if(write) t->f = (decltype(t->f))(v & (m)); \
        else ret = long(t->f) * 10000; \
        return true;
#define Z30_V_CT(ctr, f, a, b) \
    case ctr: \
        if(write) t->f = (decltype(t->f))vbound(v, (long)(a), (long)(b)); \
        else ret = long(t->f) * 10000; \
        return true;
#define Z30_V_CTM(ctr, f, m) \
    case ctr: \
        if(write) t->f = (decltype(t->f))(v & (m)); \
        else ret = long(t->f) * 10000; \
        return true;
#define Z30_V_FIX(ctr, f) \
    case ctr: \
        if(write) t->f = value; \
        else ret = z30_trig_fix_get(t->f); \
        return true;

    switch(arg)
    {
    Z30_TRIG_BOTH(Z30_V_BOTH)
    Z30_TRIG_BOTH_MASK(Z30_V_BOTHM)
    Z30_TRIG_CT(Z30_V_CT)
    Z30_TRIG_CT_MASK(Z30_V_CTM)
    Z30_TRIG_CT_FIX(Z30_V_FIX)

    case Z30R_COMBODTRIGTINTR: case Z30R_CMBTRIGTINTR:
    case Z30R_COMBODTRIGTINTG: case Z30R_CMBTRIGTINTG:
    case Z30R_COMBODTRIGTINTB: case Z30R_CMBTRIGTINTB:
    {
        const int i = (arg == Z30R_COMBODTRIGTINTR || arg == Z30R_CMBTRIGTINTR) ? 0 : (arg == Z30R_COMBODTRIGTINTG || arg == Z30R_CMBTRIGTINTG) ? 1 : 2;

        if(write)
            t->trigtint[i] = z30_trig_tint_from_script(v);
        else
            ret = long(t->trigtint[i]) * 10000;
        return true;
    }
    case Z30R_CMBTRIG_MUSIC_REFRESH:
        if(!write)
            ret = long(t->set_music_refresh) * 10000;
        else if(v >= -1 && v <= Z30_MUSIC_UPDATE_REGION)
            t->set_music_refresh = (signed char)v;
        return true;
    case Z30R_CMBTRIG_PLAY_MUSIC:    // a musicdata reference, or -2 (none), -1 (stop), 0 (the screen's)
        if(!write)
            ret = t->play_music;
        else if(value == -2 || value == -1 || value == 0 || q30_music_exists(value))
            t->play_music = value;
        return true;
    case Z30R_CMBTRIG_REQ_SCREXDOOR:    // (dir, index): the ex-door states it needs set, or set unset
    case Z30R_CMBTRIG_UNREQ_SCREXDOOR:
    {
        const long dir = SH::read_stack(SPR + 1) / 10000, ind = SH::read_stack(SPR) / 10000;
        Q30Bits *bits = arg == Z30R_CMBTRIG_REQ_SCREXDOOR ? t->req_screen_ex_door : t->unreq_screen_ex_door;

        if(unsigned(dir) > 3)
            Z_scripterrlog("Invalid dir '%ld'; must be 0-3\n", dir);
        else if(unsigned(ind) > 7)
            Z_scripterrlog("Invalid index '%ld'; must be 0-7\n", ind);
        else if(write)
            z30_bits_put(bits[dir], ind, value != 0);
        else
            ret = bits[dir].get(ind) ? 10000 : 0;
        return true;
    }

    // combotrigger-> arrays: bits of the trigger
    case Z30R_CMBTRIGBUTTON: case Z30R_CMBTRIGREQLVLSTATE: case Z30R_CMBTRIGUNREQLVLSTATE:
    case Z30R_CMBTRIGGERREQPLAYERDIR:
    {
        const long size = arg == Z30R_CMBTRIGBUTTON ? 8 : arg == Z30R_CMBTRIGGERREQPLAYERDIR ? 4 : 32;
        long n;

        if(!z30_array_index(Z30_INDEX, size, n))
        {
            if(!write)
                ret = z30_array_default(0);
            return true;
        }

        dword bits = arg == Z30R_CMBTRIGBUTTON ? t->triggerbtn : arg == Z30R_CMBTRIGGERREQPLAYERDIR ? t->req_player_dir
                     : arg == Z30R_CMBTRIGREQLVLSTATE ? t->req_level_state : t->unreq_level_state;

        if(!write)
        {
            ret = (bits >> n) & 1 ? 10000 : 0;
            return true;
        }

        bits = value ? (bits | (1u << n)) : (bits & ~(1u << n));

        switch(arg)
        {
        case Z30R_CMBTRIGBUTTON: t->triggerbtn = (byte)bits; break;
        case Z30R_CMBTRIGGERREQPLAYERDIR: t->req_player_dir = (byte)bits; break;
        case Z30R_CMBTRIGREQLVLSTATE: t->req_level_state = bits; break;
        default: t->unreq_level_state = bits; break;
        }
        return true;
    }
    case Z30R_CMBTRIGREQGLOBALSTATE: case Z30R_CMBTRIGUNREQGLOBALSTATE: case Z30R_CMBTRIGREQSCRSTATE:
    case Z30R_CMBTRIGUNREQSCRSTATE: case Z30R_CMBTRIGREQSCREXSTATE: case Z30R_CMBTRIGUNREQSCREXSTATE:
    case Z30R_CMBTRIGFLAGS:
    {
        Q30Bits &bits = arg == Z30R_CMBTRIGREQGLOBALSTATE ? t->req_global_state : arg == Z30R_CMBTRIGUNREQGLOBALSTATE ? t->unreq_global_state
                        : arg == Z30R_CMBTRIGREQSCRSTATE ? t->req_screen_state : arg == Z30R_CMBTRIGUNREQSCRSTATE ? t->unreq_screen_state
                        : arg == Z30R_CMBTRIGREQSCREXSTATE ? t->req_screen_ex_state : arg == Z30R_CMBTRIGUNREQSCREXSTATE ? t->unreq_screen_ex_state
                        : t->flags;
        long n;

        if(!z30_array_index(Z30_INDEX, arg == Z30R_CMBTRIGFLAGS ? (long)Q30TF_MAX : 256, n))
        {
            if(!write)
                ret = z30_array_default(0);
            return true;
        }

        if(write)
        {
            z30_bits_put(bits, n, value != 0);
            if(arg == Z30R_CMBTRIGFLAGS && n == Q30TF_ONLYGENTRIG && combo)
                combo->only_gentrig = value != 0;
        }
        else if(arg == Z30R_CMBTRIGFLAGS && n == Q30TF_ONLYGENTRIG)
            ret = (combo && combo->only_gentrig) ? 10000 : 0;
        else
            ret = bits.get(n) ? 10000 : 0;
        return true;
    }
    default:
        break;
    }
#undef Z30_V_BOTH
#undef Z30_V_BOTHM
#undef Z30_V_CT
#undef Z30_V_CTM
#undef Z30_V_FIX

    return true;
}

// ---- r12: plain fields scripts in the pack use ----------------------------------------------------
// Upstream's types/mapdata.cpp, screendata.cpp, shopdata.cpp, itemdata.cpp, lweapon.cpp and
// combodata.cpp: values 2.53's structures already hold.
static bool zasm30_plain_reg(const long arg, long &ret, const long value, const bool write)
{
    const long v = value / 10000;
    long n;

    switch(arg)
    {
    // mapdata-> bytes (no mapdata: -10000; writes 0-255)
    case Z30R_MAPDATASTAIRX: case Z30R_MAPDATASTAIRY: case Z30R_MAPDATAEXITDIR: case Z30R_MAPDATAWARPARRIVALX:
    case Z30R_MAPDATAWARPARRIVALY: case Z30R_MAPDATAITEMX: case Z30R_MAPDATAITEMY:
    case Z30R_SCREENDATAITEMX: case Z30R_SCREENDATAITEMY:
    {
        mapscr *m = (arg == Z30R_SCREENDATAITEMX || arg == Z30R_SCREENDATAITEMY) ? tmpscr : z30_mapdata_now();

        if(!m)
        {
            if(!write)
                ret = -10000;
            return true;
        }

        byte *f = arg == Z30R_MAPDATASTAIRX ? &m->stairx : arg == Z30R_MAPDATASTAIRY ? &m->stairy : arg == Z30R_MAPDATAEXITDIR ? &m->exitdir
                  : arg == Z30R_MAPDATAWARPARRIVALX ? &m->warparrivalx : arg == Z30R_MAPDATAWARPARRIVALY ? &m->warparrivaly
                  : (arg == Z30R_MAPDATAITEMX || arg == Z30R_SCREENDATAITEMX) ? &m->itemx : &m->itemy;

        if(write)
            *f = (byte)vbound(v, 0L, 255L);
        else
            ret = long(*f) * 10000;
        return true;
    }
    case Z30R_SCREENDATACATCHALL:
        if(write)
            tmpscr->catchall = (word)vbound(v, -214747L, 214747L);
        else
            ret = long(tmpscr->catchall) * 10000;
        return true;

    // mapdata->Path[4], SecretCombo[128], SecretCSet[128], SecretFlags[128]; Screen->TileWarpType[4],
    // TileWarpDMap[4], TileWarpScreen[4]. A write out of a byte's range is bound to it; a combo or
    // DMap number out of range is refused.
    case Z30R_MAPDATAPATH: case Z30R_MAPDATASECRETCOMBO: case Z30R_MAPDATASECRETCSET: case Z30R_MAPDATASECRETFLAG:
    case Z30R_SCREENDATATILEWARPTYPE: case Z30R_SCREENDATATILEWARPDMAP: case Z30R_SCREENDATATILEWARPSCREEN:
    {
        const bool scr = arg == Z30R_SCREENDATATILEWARPTYPE || arg == Z30R_SCREENDATATILEWARPDMAP || arg == Z30R_SCREENDATATILEWARPSCREEN;
        mapscr *m = scr ? tmpscr : z30_mapdata_now();
        const long size = (arg == Z30R_MAPDATAPATH || scr) ? 4 : 128;

        if(!m || !z30_array_index(Z30_INDEX, size, n))
        {
            if(!write)
                ret = 0;
            return true;
        }

        if(arg == Z30R_MAPDATASECRETCOMBO || arg == Z30R_SCREENDATATILEWARPDMAP)
        {
            word &f = arg == Z30R_MAPDATASECRETCOMBO ? m->secretcombo[n] : m->tilewarpdmap[n];
            const long limit = arg == Z30R_MAPDATASECRETCOMBO ? MAXCOMBOS : MAXDMAPS;

            if(!write)
                ret = long(f) * 10000;
            else if(v < 0 || v >= limit)
                Z_scripterrlog("Invalid value %ld (must be 0 to %ld)\n", v, limit - 1);
            else
                f = (word)v;
            return true;
        }

        byte &f = arg == Z30R_MAPDATAPATH ? m->path[n] : arg == Z30R_MAPDATASECRETCSET ? m->secretcset[n]
                  : arg == Z30R_MAPDATASECRETFLAG ? m->secretflag[n] : arg == Z30R_SCREENDATATILEWARPTYPE ? m->tilewarptype[n]
                  : m->tilewarpscr[n];

        if(write)
            f = (byte)vbound(v, 0L, 255L);
        else
            ret = long(f) * 10000;
        return true;
    }

    // shopdata->Item[3], HasItem[3], Price[3] (an info shop has prices only), Type
    case Z30R_SHOPDATAITEM: case Z30R_SHOPDATAHASITEM: case Z30R_SHOPDATAPRICE:
    {
        const long ref = Z30_REF(SHOPDATA);
        const bool info = ref >= 256 && ref <= 511;

        if(ref < 0 || ref > 511)
            Z_scripterrlog("Invalid shopdata pointer: %ld\n", ref);

        if(ref < 0 || ref > 511 || !z30_array_index(Z30_INDEX, 3, n))
        {
            if(!write)
                ret = z30_array_default(-10000);
            return true;
        }

        if(arg == Z30R_SHOPDATAPRICE)
        {
            word &f = info ? QMisc.info[ref - 256].price[n] : QMisc.shop[ref].price[n];

            if(write)
                f = (word)vbound(v, 0L, 65535L);
            else
                ret = long(f) * 10000;
            return true;
        }

        if(info)
        {
            Z_scripterrlog("Attempted to load '%s' from an infoshop, using shop ID: %ld\n", arg == Z30R_SHOPDATAITEM ? "item" : "hasitem", ref);
            if(!write)
                ret = -10000;
            return true;
        }

        byte &f = arg == Z30R_SHOPDATAITEM ? QMisc.shop[ref].item[n] : QMisc.shop[ref].hasitem[n];

        if(!write)
            ret = arg == Z30R_SHOPDATAITEM ? long(f) * 10000 : (f ? 10000 : 0);
        else if(arg == Z30R_SHOPDATAHASITEM)
            f = value ? 1 : 0;
        else
            f = (byte)vbound(v, 0L, 255L);
        return true;
    }
    case Z30R_SHOPDATATYPE:     // 1 a shop, 2 an info shop (read only)
    {
        const long ref = Z30_REF(SHOPDATA);

        if(!write)
            ret = (ref > 511 || ref < 0) ? 0 : (ref <= 256 ? 10000 : 20000);
        return true;
    }

    // itemdata->TileMod, Frames, ASpeed, Delay
    case Z30R_IDATALTM: case Z30R_IDATAFRAMES: case Z30R_IDATAASPEED: case Z30R_IDATADELAY:
    {
        const long id = ri->idata;

        if(id < 0 || id >= q30_items_end())
        {
            Z_scripterrlog("Invalid itemdata access: %ld\n", id);
            if(!write)
                ret = arg == Z30R_IDATALTM ? 0 : -10000;
            return true;
        }

        itemdata &it = Q30_IDATA(id);

        if(arg == Z30R_IDATALTM)
        {
            if(write) it.ltm = v;
            else ret = it.ltm * 10000;
            return true;
        }

        byte &f = arg == Z30R_IDATAFRAMES ? it.frames : arg == Z30R_IDATAASPEED ? it.speed : it.delay;

        if(write)
            f = (byte)vbound(v, 0L, 214748L);
        else
            ret = long(f) * 10000;
        return true;
    }

    // lweapon->Parent (the item that made it), ->ScriptUID (read only)
    case Z30R_LWPNPARENT:
    case Z30R_LWEAPONSCRIPTUID:
    {
        weapon *w = z30_lwpn_ref();

        if(!w)
        {
            if(!write)
                ret = -10000;
            return true;
        }

        if(arg == Z30R_LWEAPONSCRIPTUID)
        {
            if(!write)
                ret = w->getUID();
        }
        else if(write)
            w->parentitem = (int)vbound(v, -1L, (long)Z30_MAX_ITEMS_UP - 1);
        else
            ret = long(w->parentitem) * 10000;
        return true;
    }

    // combodata->UserFlags: all of them as one number
    case Z30R_COMBODUSRFLAGS:
    {
        const long cid = Z30_REF(COMBODATA);
        Q30Combo *cc = (cid >= 0 && cid < MAXCOMBOS) ? q30_combo_for_write((int)cid) : NULL;

        if(!cc)
        {
            if(!write)
                ret = -10000;
            return true;
        }

        if(write)
            cc->usrflags = (dword)v;
        else
            ret = (long)(int32_t)(cc->usrflags * 10000u);     // (wraps, as upstream's int multiply does)
        return true;
    }
    default:
        return false;
    }
}

// FFScript::Distance, in 1/10000ths: each square rounded on its own, as on x64 (ARM64 would fuse
// one into the sum and come out 0.0001 low now and then, as upstream found).
static long z30_distance(double x1, double y1, double x2, double y2)
{
    const double x = x1 - x2, y = y1 - y2;
    volatile double xx = x * x;
    volatile double yy = y * y;
    return (long)(sqrt((xx + yy) * 1000000.0) * 10);
}

static bool zasm30_get_register_inner(const long arg, long &ret)
{
    if(zasm30_sys_get(arg, ret))
        return true;

    if(zasm30_data_reg(arg, ret, 0, false) || zasm30_sub_get(arg, ret) || zasm30_scrdata_get(arg, ret) || zasm30_draw_get(arg, ret) || zasm30_trigger_reg(arg, ret, 0, false)
       || zasm30_plain_reg(arg, ret, 0, false) || zasm30_live_get(arg, ret))
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
    // (an empty button holds 0 here and -1 in upstream 2.55/3.0, unless a script put item 0 there)
    // (a combined bow and arrow is the arrow's id + 0xF000 here; upstream's button holds the arrow's id)
    case Z30R_LINKITEMX: ret = (Xwpn || q30_btn_item0[2] ? z30_btn_id(Xwpn) : -1) * 10000; return true;
    case Z30R_LINKITEMY: ret = (Ywpn || q30_btn_item0[3] ? z30_btn_id(Ywpn) : -1) * 10000; return true;
    case Z30R_LINKITEMA: ret = (Awpn || q30_btn_item0[0] ? z30_btn_id(Awpn) : -1) * 10000; return true;
    case Z30R_LINKITEMB: ret = (Bwpn || q30_btn_item0[1] ? z30_btn_id(Bwpn) : -1) * 10000; return true;
    case Z30R_LINKSTUN: ret = zasm30_store_get(arg, 0, 0); return true;
    case Z30R_HEROBUNNY: ret = q30_bunny_clock() * 10000; return true;
    // Hero->TileMod: what the items (and bunny form) add to the Hero's tile now (upstream's
    // getTileModifier; read only)
    case Z30R_LINKTILEMOD: ret = (item_tile_mod(false) + q30_bunny_tile_mod()) * 10000L; return true;
    case Z30R_LINKCANFLICKER: ret = zasm30_store_get(arg, 0, 0) ? 10000 : (arg == Z30R_LINKCANFLICKER ? 10000 : 0); return true;
    case Z30R_LINKDIAG: ret = Link.getDiagMove() ? 10000 : 0; return true;
    case Z30R_LINKBIGHITBOX: ret = Link.getBigHitbox() ? 10000 : 0; return true;
    case Z30R_LINKINVFRAME: ret = Link.getHClk() * 10000; return true;
    case Z30R_HEROSTEPRATE: ret = q30_hero_step_rate() * 10000; return true;
    case Z30R_HERO_TMP_STEPBOOST: ret = q30_hero_step_boost() * 10000; return true;
    case Z30R_HEROLIFTEDWPN:    // (upstream: the held weapon's UID, 0 for none)
    {
        weapon *w = q30_lifted_weapon();
        ret = w ? w->getUID() : 0;
        return true;
    }
    case Z30R_HEROSTANDING: ret = Link.q30_is_standing() ? 10000 : 0; return true;
    case Z30R_HERO_SCREEN: ret = currscr * 10000; return true;
    // Game->CurScreen / CurDMapScreen: upstream's cur_screen, which in a scrolling region is the
    // region's top-left screen (Hero->Screen above is Link's own); currscr outside regions
    case Z30R_CURSCR: ret = q30_region_origin() * 10000; return true;
    case Z30R_CURDSCR:
        ret = (DMaps[currdmap].type == dmOVERW ? q30_region_origin() : q30_region_origin() - DMaps[currdmap].xoff) * 10000;
        return true;

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
    case Z30R_LWPNTIMEOUT: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = st ? st->weap_timeout * 10000 : -10000; return true; }
    case Z30R_LWPNFLAGS: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = (st && z30_flag_get(st->weapon_flags, Z30_INDEX, 32)) ? 10000 : 0; return true; }
    case Z30R_LWPNMOVEFLAGS:
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

    // Combo script position (a combo of the current screen; in a scrolling region, as the
    // region's combo position and pixels, q30_regions.h)
    case Z30R_COMBOPOSR:
        ret = q30_region_rpos_of_pos(Z30_REF(COMBOPOS) & 0xFFFF) * 10000;
        return true;
    case Z30R_COMBOLAYERR:
        ret = (Z30_REF(COMBOPOS) >> 16) * 10000;
        return true;
    case Z30R_COMBOXR:
        ret = (((Z30_REF(COMBOPOS) & 0xFFFF) % 16) * 16 + q30_region_origin_x()) * 10000;
        return true;
    case Z30R_COMBOYR:
        ret = (((Z30_REF(COMBOPOS) & 0xFFFF) & 0xF0) + q30_region_origin_y()) * 10000;
        return true;

    // combodata->
    case Z30R_COMBODATAID: ret = Z30_REF(COMBODATA) * 10000; return true;
    case Z30R_COMBODTILE: ret = z30_cdata().tile * 10000; return true;
    case Z30R_COMBODFLIP: ret = z30_cdata().flip * 10000; return true;
    case Z30R_COMBODWALK: ret = q30_script_combo_get(Z30_REF(COMBODATA), 2) * 10000; return true;
    case Z30R_COMBODTYPE: ret = q30_script_combo_get(Z30_REF(COMBODATA), 0) * 10000; return true;
    case Z30R_COMBODCSET: ret = (z30_cdata().csets & 0x0F) * 10000; return true;
    case Z30R_COMBODFLAG: ret = q30_script_combo_get(Z30_REF(COMBODATA), 1) * 10000; return true;
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
    // GenFlags[] (4), and what a combo that can be walked on top of or dived under needs
    // (upstream combodata.cpp; the heights are zfix numbers, as a script's own)
    case Z30R_COMBODGENFLAGARR:
    {
        const Q30Combo *c = q30_combo(Z30_REF(COMBODATA));
        ret = (c && Z30_INDEX >= 0 && Z30_INDEX < 4) ? ((c->genflags & (1 << Z30_INDEX)) ? 10000 : 0) : 0;
        return true;
    }
    case Z30R_COMBOD_Z_HEIGHT:
    case Z30R_COMBOD_Z_STEP_HEIGHT:
    case Z30R_COMBOD_DIVE_UNDER_LEVEL:
    {
        const Q30Combo *c = q30_combo(Z30_REF(COMBODATA));
        ret = !c ? 0 : (arg == Z30R_COMBOD_Z_HEIGHT ? c->z_height : (arg == Z30R_COMBOD_Z_STEP_HEIGHT ? c->z_step_height : c->dive_under_level * 10000));
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

    // Screen->
    case Z30R_SCREENDATAITEM: ret = tmpscr->hasitem ? mapscr_item(tmpscr) * 10000 : -10000; return true;
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
        // (a global or generic script in a scrolling region reads the region's top-left screen's)
        const int index = currmap * MAPSCRS + zasm30_script_screen();
        if(Z30_INDEX >= 0 && Z30_INDEX < 8)
        {
            std::map<int, Zasm30ScreenScript>::const_iterator it = zasm30_screen_scripts.find(index);
            if(it != zasm30_screen_scripts.end())
                ret = it->second.initd[Z30_INDEX];
        }
        return true;
    }
    // raw DRAW_ORIGIN_* (not x10000) and a sprite uid, kept by each running script (upstream)
    case Z30R_SCREEN_DRAW_ORIGIN:
        ret = zasm30_frame().draw_origin;
        return true;
    case Z30R_SCREEN_DRAW_ORIGIN_TARGET:
        ret = zasm30_frame().draw_origin_target;
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
        long n = Z30_INDEX;
        mapscr *m = z30_mapdata_combo(Z30_REF(MAPDATA), n);    // (across a scrolling region: z30_mapdata_combo)
        ret = -10000;

        if(m)
        {
            switch(arg)
            {
            case Z30R_MAPDATACOMBODD: ret = m->data[n] * 10000; break;
            case Z30R_MAPDATACOMBOCD: ret = m->cset[n] * 10000; break;
            case Z30R_MAPDATACOMBOFD: ret = m->sflag[n] * 10000; break;
            case Z30R_MAPDATACOMBOTD: ret = q30_script_combo_get(m->data[n], 0) * 10000; break;
            case Z30R_MAPDATACOMBOID: ret = q30_script_combo_get(m->data[n], 1) * 10000; break;
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
    case Z30R_DMAPDATASIDEVIEW: ret = q30_dmap_sideview(Z30_REF(DMAPDATA)) ? 10000 : 0; return true;
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
    case Z30R_DMAPINITD:            // (upstream: run_args, whether or not a script is set)
    {
        const long *v = q30_dmap_initd(Z30_REF(DMAPDATA), q30_dmap_active);
        ret = (unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS && Z30_INDEX >= 0 && Z30_INDEX < 8) ? (v ? v[Z30_INDEX] : 0) : -10000;
        return true;
    }

    // itemdata-> (ri->idata)
    case Z30R_IDATAID: ret = ri->idata * 10000; return true;
    case Z30R_IDATATILE: ret = Q30_IDATA(ri->idata).tile * 10000; return true;
    case Z30R_IDATACSET: ret = (Q30_IDATA(ri->idata).csets & 15) * 10000; return true;
    case Z30R_IDATABUNNYABLE: ret = q30_item_bunny(ri->idata) ? 10000 : 0; return true;
    case Z30R_IDATASCRIPT: ret = Q30_IDATA(ri->idata).script * 10000; return true;
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
    // Game->Scrolling[]: the running scroll (q30_regions.h); DIR is -1 while none runs
    case Z30R_GAMESCROLLING: ret = q30_scroll_data(Z30_INDEX) * 10000; return true;
    case Z30R_GAMESUSPEND: ret = q30_game_suspended(Z30_INDEX) ? 10000 : 0; return true;
    case Z30R_GAMEEVENTDATA:    // the event being thrown (q30_throw_generic_event)
        ret = (Z30_INDEX >= 0 && Z30_INDEX < (long)zasm30_event_data.size()) ? zasm30_event_data[Z30_INDEX] : 0;
        return true;
    case Z30R_GAMEBOTTLEST: ret = q30_bottle_slot(Z30_INDEX) * 10000; return true;   // the save's bottle slots (q30_triggers.cpp)
    case Z30R_MUSICUPDATECOND: ret = zasm30_store_get(arg, 0, 0) * 10000; return true;
    case Z30R_GAMEASUBYOFF: ret = q30_active_sub_yoff * 10000; return true;   // (zc_subscr.cpp; read only)
    // (2.53's rule table holds the first 160 rules; the 2.55 and 3.0 ones are q30_triggers.cpp's)
    case Z30R_FFRULE: ret = (Z30_INDEX >= 0 && Z30_INDEX < QUESTRULES_SIZE * 8) ? (get_bit(quest_rules, Z30_INDEX) ? 10000 : 0) : (q30_script_rule_get(Z30_INDEX) ? 10000 : 0); return true;
    // (in a 3.0 scrolling region an FFC's ID goes across the region, 128 to a screen as upstream's:
    // q30_scripts_region.h; outside one, its slot + 1; 0 for no FFC, as upstream's)
    case Z30R_FFCID:
    {
        const long id = zasm30_selected_ffc_id();
        ret = id >= 0 ? (id + 1) * 10000 : 0;
        return true;
    }
    // the FFC's draw layer (mapscr fflayer, upstream's ffcdata::layer)
    case Z30R_FFCLAYER: ret = (ri->ffcref >= 0 && ri->ffcref < tmpscr->ffc_count()) ? (tmpscr->fflayer[ri->ffcref] + 1) * 10000 : 0; return true;
    // upstream's MAX_FFCID + 1: 128 FFCs a screen, so every screen of a scrolling region's
    case Z30R_MAX_FFC_ID: ret = (q30_region_on() ? q30_region_screens_wide() * q30_region_screens_high() : 1) * Q30_MAXFFCS * 10000; return true;
    // Screen->Index: the screen the script's Screen-> means (upstream's ri->screenref())
    case Z30R_SCREEN_INDEX: ret = zasm30_script_screen() * 10000; return true;
    case Z30R_DISTANCE:
    case Z30R_DISTANCESCALE:
    {
        // The x64 build's steps (/fp:fast multiplies by 0.0001 where upstream divides by 10000),
        // each rounded on its own: the same on every compiler and CPU.
        volatile double x1 = Z30_D(6) * 0.0001, x2 = Z30_D(1) * 0.0001;
        volatile double y1 = Z30_D(0) * 0.0001, y2 = Z30_D(2) * 0.0001;

        if(arg == Z30R_DISTANCE)
            ret = z30_distance(x1, y1, x2, y2);
        else
        {
            // upstream's Distance(..., scale): the distance to the point 1/scale of the way
            // there, times scale (a whole number, 10000 when 0). x64 divides once, by 1/scale.
            int32_t scale = (int32_t)(Z30_D(7) / 10000);
            if(!scale)
                scale = 10000;
            volatile double inv = 1.0 / scale;
            volatile double dx = (x2 - x1) * inv, dy = (y2 - y1) * inv;
            const double x3 = x1 + dx, y3 = y1 + dy;
            ret = (int32_t)((int64_t)z30_distance(x1, y1, x3, y3) * scale);
        }
        return true;
    }

    // Region and viewport (q30_regions.h): a screen outside a scrolling region is a region of
    // its own, one screen big.
    case Z30R_REGION_WIDTH: ret = q30_region_width() * 10000; return true;
    case Z30R_REGION_HEIGHT: ret = q30_region_height() * 10000; return true;
    case Z30R_REGION_ID: ret = q30_region_id() * 10000; return true;
    case Z30R_REGION_SCREEN_WIDTH: ret = q30_region_screens_wide() * 10000; return true;
    case Z30R_REGION_SCREEN_HEIGHT: ret = q30_region_screens_high() * 10000; return true;
    case Z30R_REGION_NUM_COMBOS: ret = 176 * q30_region_screens_wide() * q30_region_screens_high() * 10000; return true;
    case Z30R_REGION_ORIGIN_SCREEN: ret = q30_region_origin() * 10000; return true;
    // The viewport: the camera's top-left in region pixels (upstream's 256x176, 256x232 in
    // extended height mode); a script-moved one (mode 2) where the script puts it.
    case Z30R_VIEWPORT_X:
    case Z30R_VIEWPORT_Y:
    {
        int vx, vy;
        q30_region_viewport(&vx, &vy);
        ret = (arg == Z30R_VIEWPORT_X ? vx : vy) * 10000;
        return true;
    }
    case Z30R_VIEWPORT_WIDTH: ret = q30_viewport_w() * 10000; return true;
    case Z30R_VIEWPORT_HEIGHT: ret = q30_viewport_h() * 10000; return true;
    case Z30R_VIEWPORT_MODE: ret = q30_viewport_mode(); return true;
    case Z30R_VIEWPORT_TARGET: ret = q30_viewport_target(); return true;
    case Z30R_VIEWPORT_DEADZONE_WIDTH:
    case Z30R_VIEWPORT_DEADZONE_HEIGHT:
    case Z30R_VIEWPORT_LOOKAHEAD_X:
    case Z30R_VIEWPORT_LOOKAHEAD_Y:
    case Z30R_VIEWPORT_LOOKAHEAD_SPEED:
    case Z30R_VIEWPORT_RECENTER_SPEED:
    case Z30R_VIEWPORT_RECENTER_DELAY:
        ret = q30_viewport_follow(z30_viewport_follow_reg(arg));
        return true;
    case Z30R_HEROSHIELDJINX: ret = zasm30_shield_jinx * 10000; return true;

    // Game->MaxNPCs/MaxLWeapons/MaxEWeapons/MaxItemsprites(): each list's limit (255 at most,
    // which 2.53's lists hold).
    case Z30R_SPRITEMAXNPC: ret = guys.getMax() * 10000; return true;
    case Z30R_SPRITEMAXLWPN: ret = Lwpns.getMax() * 10000; return true;
    case Z30R_SPRITEMAXEWPN: ret = Ewpns.getMax() * 10000; return true;
    case Z30R_SPRITEMAXITEM: ret = items.getMax() * 10000; return true;

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
        ret = 0;                // pits act at once on Link in this engine: he is never mid-fall
        return true;
    case Z30R_NPCFALLCLK:       // (an enemy falling into a pit: guys.cpp's fall, 70 down to 1)
    {
        enemy *e = z30_npc_ref();
        ret = e ? q30_enemy_fall_clk(e->getUID()) * 10000 : 0;
        return true;
    }
    case Z30R_LINKCLIMBING:     // (upstream: on a sideview ladder)
        ret = Link.q30_sv_ladder() ? 10000 : 0;
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
    case Z30R_ACTIVESSSPEED: ret = q30_subscr_speed * 10000; return true;   // the subscreen's slide speed
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

        if(!q30_quest_rule(644))    // whole pixels without qr_SPRITEXY_IS_FLOAT, as upstream's
            ret = ret / 10000 * 10000;
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
    case Z30R_DEBUGTESTING:     // (upstream: use_testingst_start, its test mode; ours: a ZC_TEST_START run)
    {
        static const bool testing = getenv("ZC_TEST_START") && *getenv("ZC_TEST_START");
        ret = testing ? 10000 : 0;
        return true;
    }
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
        ret = (Z30_REF(COMBODATA) >= 0 && Z30_REF(COMBODATA) < MAXCOMBOS) ? q30_script_combo_get(Z30_REF(COMBODATA), 3) * 10000 : -10000;
        return true;
    }
    case Z30R_COMBODATTRISHORTS:
        ret = long(z30_combo_attr_get(Z30_REF(COMBODATA), 16 + Z30_INDEX) / 10000) * 10000;
        return true;

    // mapdata->
    case Z30R_MAPDATACOMBOSD:
    case Z30R_MAPDATACOMBOED:
    {
        long n = Z30_INDEX;
        mapscr *m = z30_mapdata_combo(Z30_REF(MAPDATA), n);
        ret = -10000;
        if(m && n >= 0 && n < (long)m->data.size() && m->data[n] < MAXCOMBOS)
            ret = q30_script_combo_get(m->data[n], arg == Z30R_MAPDATACOMBOED ? 3 : 2) * 10000;
        return true;
    }
    case Z30R_COMBOED:      // Screen->ComboE[]: the effect squares of the combo there
    {
        int pos = Z30_INDEX;
        bool cur;
        mapscr *cs = zasm30_combo_screen(&pos, &cur);
        ret = (pos >= 0 && pos < 176) ? q30_script_combo_get(cs->data[pos], 3) * 10000 : -10000;
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
    // mapdata->Pattern: the screen's enemy pattern
    case Z30R_MAPDATAPATTERN:
    {
        mapscr *m = z30_mapdata_now();
        ret = m ? m->pattern * 10000 : -10000;
        return true;
    }
    // mapdata->TileWarpDMap[], TileWarpScreen[], TileWarpType[], SideWarpType[] and the two
    // overlay flag arrays (upstream's mapdata array registers)
    case Z30R_MAPDATATILEWARPDMAP:
    case Z30R_MAPDATATILEWARPSCREEN:
    case Z30R_MAPDATATILEWARPTYPE:
    case Z30R_MAPDATASIDEWARPTYPE:
    case Z30R_MAPDATATILEWARPOVFLAGS:
    case Z30R_MAPDATASIDEWARPOVFLAGS:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        ret = -10000;
        if(!m || n < 0 || n >= 4)
            return true;
        switch(arg)
        {
        case Z30R_MAPDATATILEWARPDMAP: ret = m->tilewarpdmap[n] * 10000; break;
        case Z30R_MAPDATATILEWARPSCREEN: ret = m->tilewarpscr[n] * 10000; break;
        case Z30R_MAPDATATILEWARPTYPE: ret = m->tilewarptype[n] * 10000; break;
        case Z30R_MAPDATASIDEWARPTYPE: ret = m->sidewarptype[n] * 10000; break;
        case Z30R_MAPDATATILEWARPOVFLAGS: ret = ((m->tilewarpoverlayflags >> n) & 1) ? 10000 : 0; break;
        case Z30R_MAPDATASIDEWARPOVFLAGS: ret = ((m->sidewarpoverlayflags >> n) & 1) ? 10000 : 0; break;
        }
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
    case Z30R_MAPDATAITEM: { mapscr *m = z30_mapdata_now(); ret = (m && m->hasitem) ? mapscr_item(m) * 10000 : -10000; return true; }
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
        // (scripts from before ZScript version 11 count this array from 0, as LayerMap[] above)
        const long n = Z30_INDEX - (zasm30_ffscript_version < 11 ? 0 : 1);
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
            const int index = ref < 0 ? currmap * MAPSCRS + (z30_md_one(ref) ? z30_md_screen(ref) : currscr) : ref - 1;
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
    case Z30R_DMAPDATAMAPINITD:     // (the same for the map script's)
    {
        const long *v = q30_dmap_initd(Z30_REF(DMAPDATA), q30_dmap_onmap);
        ret = (unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS && Z30_INDEX >= 0 && Z30_INDEX < 8) ? (v ? v[Z30_INDEX] : 0) : -10000;
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
    case Z30R_EWPNFAKEZ: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? z30_wpn_fakez_read(st->fakez) : -10000; return true; }
    case Z30R_EWPNFAKEJUMP: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? z30_fake_jump_read(st->fakefall) : -10000; return true; }
    case Z30R_LWPNFAKEZ: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = st ? z30_wpn_fakez_read(st->fakez) : -10000; return true; }
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
    case Z30R_ITEMMOVEFLAGS: { item *it = z30_item_ref(); Zasm30SpriteState *st = it ? zasm30_item_state(it) : NULL; ret = (st && z30_flag_get(st->move_flags, Z30_INDEX, 11)) ? 10000 : 0; return true; }
    case Z30R_ITEMSHADOWSPR: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); ret = st ? st->shadow_spr * 10000 : 0; return true; }
    case Z30R_ITEMPSTRING: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); ret = st ? st->pstring * 10000 : -10000; return true; }
    case Z30R_ITEMSPRITESCRIPT: { Zasm30SpriteState *st = z30_state_of(z30_item_ref()); ret = st ? st->script * 10000 : -10000; return true; }
    case Z30R_ITEMSPRITEINITD:
    {
        Zasm30SpriteState *st = z30_state_of(z30_item_ref());
        ret = (st && Z30_INDEX >= 0 && Z30_INDEX < 8) ? st->initd[Z30_INDEX] : -10000;
        return true;
    }
    case Z30R_IDATAFLAGS:   // itemdata->Flags[]: upstream's item_flag1-15 and item_passive_script
    {
        ret = 0;
        if(unsigned(ri->idata) < unsigned(q30_items_end()) && Z30_INDEX >= 0 && Z30_INDEX < 16)
            ret = (z30_idata_flags(ri->idata) & z30_idata_flag_bit(Z30_INDEX)) ? 10000 : 0;
        return true;
    }
    case Z30R_IDATAATTRIB:
    {
        ret = -10000;
        if(unsigned(ri->idata) < unsigned(q30_items_end()) && Z30_INDEX >= 0 && Z30_INDEX < 10)
        {
            const itemdata &it = Q30_IDATA(ri->idata);
            const long values[10] = { it.misc1, it.misc2, it.misc3, it.misc4, it.misc5, it.misc6, it.misc7, it.misc8, it.misc9, it.misc10 };
            ret = values[Z30_INDEX] * 10000;
        }
        return true;
    }
    case Z30R_IDATAMINHEARTS:
        ret = unsigned(ri->idata) < unsigned(q30_items_end()) ? Q30_IDATA(ri->idata).pickup_hearts * 10000 : -10000;
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
    long unused;

    if(zasm30_sys_set(arg, value))
        return true;

    if(zasm30_data_reg(arg, unused, value, true) || zasm30_sub_set(arg, value) || zasm30_scrdata_set(arg, value) || zasm30_draw_set(arg, value) || zasm30_trigger_reg(arg, unused, value, true)
       || zasm30_plain_reg(arg, unused, value, true) || zasm30_live_set(arg, value))
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
    case Z30R_ACTIVESSSPEED: q30_subscr_speed = vbound(value / 10000, 1, 85); return true;
    case Z30R_HEROBUNNY: q30_set_bunny_clock(value / 10000); return true;
    case Z30R_LINKITEMA: q30_set_ab_item(true, value / 10000); return true;
    case Z30R_LINKITEMB: q30_set_ab_item(false, value / 10000); return true;
    case Z30R_LINKTILEMOD: return true;    // (read only, as upstream)
    case Z30R_LINKSTUN:
    case Z30R_LINKCANFLICKER:
        zasm30_store_set(arg, 0, 0, arg == Z30R_LINKCANFLICKER ? (value ? 10000 : 0) : value / 10000);
        return true;
    case Z30R_HEROSTEPRATE: q30_set_hero_step_rate(value / 10000); return true;     // Hero->Step
    case Z30R_HERO_TMP_STEPBOOST: q30_set_hero_step_boost(value / 10000); return true;
    case Z30R_HEROLIFTEDWPN:
        q30_set_lifted_weapon(value ? (weapon *)Lwpns.getByUID(value) : NULL);
        return true;
    case Z30R_LINKCLIMBING: Link.q30_set_sv_ladder(value != 0); return true;
    case Z30R_HEROSTANDING:
        return true;
    case Z30R_LINKDIAG: set_bit(quest_rules, qr_LTTPWALK, value ? 1 : 0); Link.q30_set_diag_move(value != 0); return true;
    case Z30R_LINKBIGHITBOX: set_bit(quest_rules, qr_LTTPCOLLISION, value / 10000 ? 1 : 0); Link.q30_set_big_hitbox(value / 10000 != 0); return true;
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
    case Z30R_LWPNTIMEOUT: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); if(st) st->weap_timeout = vbound(value / 10000, 0, 214748); return true; }
    case Z30R_LWPNFLAGS: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); if(st) z30_flag_set(st->weapon_flags, Z30_INDEX, 32, value != 0); return true; }
    case Z30R_LWPNMOVEFLAGS:
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
    case Z30R_COMBODWALK: q30_script_combo_set(Z30_REF(COMBODATA), 2, value / 10000); return true;
    case Z30R_COMBODTYPE:
        // (as upstream: a negative type is no type; a 3.0 quest's type past the last is refused
        // and a 2.55 quest's is kept within the byte. They wrapped round to some other type.)
        if(value < 0)
            q30_script_combo_set(Z30_REF(COMBODATA), 0, 0);
        else if(!zasm30_bad_combo_type(value))
            q30_script_combo_set(Z30_REF(COMBODATA), 0, zc_min(value / 10000, 255));
        return true;
    case Z30R_COMBODCSET: z30_cdata().csets = (z30_cdata().csets & 0xF0) | ((value / 10000) & 0x0F); return true;
    case Z30R_COMBODFLAG: q30_script_combo_set(Z30_REF(COMBODATA), 1, value / 10000); return true;
    case Z30R_COMBODFRAMES: z30_cdata().frames = (byte)(value / 10000); return true;
    case Z30R_COMBODASPEED: z30_cdata().speed = (byte)(value / 10000); return true;
    case Z30R_COMBODNEXTD: if(!zasm30_bad_combo(value)) z30_cdata().nextcombo = (word)(value / 10000); return true;
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
    case Z30R_COMBODGENFLAGARR:
    {
        Q30Combo *cc = q30_combo_for_write(Z30_REF(COMBODATA));

        if(cc && Z30_INDEX >= 0 && Z30_INDEX < 4)
        {
            if(value) cc->genflags |= (1 << Z30_INDEX);
            else cc->genflags &= ~(1 << Z30_INDEX);
        }
        return true;
    }
    case Z30R_COMBOD_Z_HEIGHT:
    case Z30R_COMBOD_Z_STEP_HEIGHT:
    case Z30R_COMBOD_DIVE_UNDER_LEVEL:
    {
        if(Q30Combo *cc = q30_combo_for_write(Z30_REF(COMBODATA)))
        {
            if(arg == Z30R_COMBOD_Z_HEIGHT)
                cc->z_height = value;
            else if(arg == Z30R_COMBOD_Z_STEP_HEIGHT)
                cc->z_step_height = zc_max(0L, value);
            else
                cc->dive_under_level = (byte)vbound(value / 10000, 0, 255);
        }
        return true;
    }
    case Z30R_COMBODATASCRIPT:      // (upstream: the combo's script and its InitD, written)
    case Z30R_COMBODATAINITD:
    {
        const long cid = Z30_REF(COMBODATA);

        if(cid < 0 || cid >= MAXCOMBOS)
            return true;

        const Q30ScriptRef *r = q30_combo_script((int)cid);
        Q30ScriptRef ref = r ? *r : Q30ScriptRef();

        if(arg == Z30R_COMBODATASCRIPT)
            ref.script = (word)vbound(value / 10000, 0, 65535);
        else if(Z30_INDEX >= 0 && Z30_INDEX < 8)
            ref.initd[Z30_INDEX] = value;   // (kept while the combo has a script)

        q30_set_combo_script((int)cid, ref.script, ref.initd);
        return true;
    }

    // Screen->
    case Z30R_SCREENDATAITEM:
    {
        int v = vbound(value / 10000, -1, q30_items_end() - 1);
        if(v > -1) set_mapscr_item(tmpscr, v);     // (256 and up too)
        tmpscr->hasitem = v > -1;
        return true;
    }
    case Z30R_SCREENDATAHASITEM: tmpscr->hasitem = (byte)(value / 10000); return true;
    case Z30R_SCREENDATASTRING: tmpscr->str = (word)(value / 10000); return true;
    case Z30R_SCREENDATAENEMY: if(Z30_INDEX >= 0 && Z30_INDEX < 10) tmpscr->enemy[Z30_INDEX] = vbound(value / 10000, 0, MAXGUYS - 1); return true;
    // (a combo that doesn't exist is refused, as upstream: triggering the secret then drew from
    // outside the combos)
    case Z30R_SCREENDATASECRETCOMBO: if(Z30_INDEX >= 0 && Z30_INDEX < 128 && !zasm30_bad_combo(value)) tmpscr->secretcombo[Z30_INDEX] = (word)(value / 10000); return true;
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
        const int index = currmap * MAPSCRS + zasm30_script_screen();
        if(Z30_INDEX >= 0 && Z30_INDEX < 8)
        {
            std::map<int, Zasm30ScreenScript>::iterator it = zasm30_screen_scripts.find(index);
            if(it != zasm30_screen_scripts.end())
                it->second.initd[Z30_INDEX] = value;
        }
        return true;
    }
    case Z30R_SCREEN_DRAW_ORIGIN:
        // DRAW_ORIGIN_DEFAULT (0) to DRAW_ORIGIN_SPRITE (6), raw; anything else is refused
        if(value < Z30_DRAW_ORIGIN_DEFAULT || value > Z30_DRAW_ORIGIN_SPRITE)
            Z_scripterrlog("Invalid value %ld for Screen->DrawOrigin (0 to 6)\n", value);
        else
            zasm30_frame().draw_origin = value;
        return true;
    case Z30R_SCREEN_DRAW_ORIGIN_TARGET:
        if(z30_sprite(value).s)     // the hero, an npc, weapon or item sprite
            zasm30_frame().draw_origin_target = value;
        else
            Z_scripterrlog("Screen->DrawOriginTarget: no sprite with uid %ld\n", value);
        return true;
    case Z30R_PUSHBLOCKLAYER: zasm30_store_set(arg, 0, 0, vbound(value / 10000, 0, 6)); return true;
    case Z30R_SHOWNMSG: return true;

    // mapdata->
    case Z30R_LOADMAPDATA: return true;
    case Z30R_MAPDATAINTID:
    {
        const long f = ri->d[0] / 10000, d = ri->d[1] / 10000;
        mapscr *m = z30_ffc_room(z30_mapdata_now(), f);
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
        long n = Z30_INDEX;
        mapscr *m = z30_mapdata_combo(Z30_REF(MAPDATA), n);    // (across a scrolling region: z30_mapdata_combo)

        if(m)
        {
            switch(arg)
            {
            case Z30R_MAPDATACOMBODD: m->data[n] = vbound(value / 10000, 0, MAXCOMBOS - 1); break;
            case Z30R_MAPDATACOMBOCD: m->cset[n] = vbound(value / 10000, 0, 15); break;
            case Z30R_MAPDATACOMBOFD: m->sflag[n] = vbound(value / 10000, 0, 255); break;
            case Z30R_MAPDATACOMBOTD: q30_script_combo_set(m->data[n], 0, value / 10000); break;
            case Z30R_MAPDATACOMBOID: q30_script_combo_set(m->data[n], 1, value / 10000); break;
            }

            // upstream's screen_combo_modify_postroutine (ComboD, ComboC): a combo put on a layer the
            // current screen has no map for makes that layer valid, drawn and solid (maps.cpp)
            if((arg == Z30R_MAPDATACOMBODD || arg == Z30R_MAPDATACOMBOCD) && m >= tmpscr2 && m < tmpscr2 + 6)
                m->valid |= mVALID;
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
    case Z30R_DMAPDATASIDEVIEW: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) q30_set_dmap_sideview(Z30_REF(DMAPDATA), value != 0); return true;
    case Z30R_DMAPDATAMAP: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].map = vbound(value / 10000 - 1, 0, map_count - 1); return true;
    case Z30R_DMAPDATALEVEL: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].level = (word)(value / 10000); return true;
    case Z30R_DMAPDATACOMPASS: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].compass = (byte)(value / 10000); return true;
    case Z30R_DMAPDATATYPE: if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS) DMaps[Z30_REF(DMAPDATA)].type = (DMaps[Z30_REF(DMAPDATA)].type & ~dmfTYPE) | ((value / 10000) & dmfTYPE); return true;
    case Z30R_DMAPDATAPALETTE:
        // (a level palette that doesn't exist is refused, as upstream: loading it read past the palettes)
        if(value / 10000 < 0 || value / 10000 >= MAXLEVELS)
            Z_scripterrlog("Invalid palette: %ld - must be >= 0 and <= %d\n", (long)(value / 10000), MAXLEVELS - 1);
        else if(unsigned(Z30_REF(DMAPDATA)) < MAXDMAPS)
            DMaps[Z30_REF(DMAPDATA)].color = (word)(value / 10000);
        return true;
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
    case Z30R_DMAPINITD:            // (upstream: the dmap's active script's InitD, with or without the script)
    {
        const long id = Z30_REF(DMAPDATA);

        if(unsigned(id) < MAXDMAPS && Z30_INDEX >= 0 && Z30_INDEX < 8)
        {
            const Q30ScriptRef *r = q30_dmap_script(id, q30_dmap_active);
            const long *v = q30_dmap_initd(id, q30_dmap_active);
            long initd[8] = { 0 };

            if(v)
                memcpy(initd, v, sizeof(initd));

            initd[Z30_INDEX] = value;

            if(r)
                q30_set_dmap_script(id, q30_dmap_active, r->script, initd);
            else
                q30_set_dmap_initd(id, q30_dmap_active, initd);
        }
        return true;
    }

    // itemdata->
    case Z30R_IDATATILE: Q30_IDATA(ri->idata).tile = vbound(value / 10000, 0, NEWMAXTILES - 1); return true;
    case Z30R_IDATACSET: Q30_IDATA(ri->idata).csets = (Q30_IDATA(ri->idata).csets & 0xF0) | vbound(value / 10000, 0, 15); return true;
    case Z30R_IDATABUNNYABLE: q30_set_item_bunny(ri->idata, value != 0); return true;
    case Z30R_IDATASCRIPT: Q30_IDATA(ri->idata).script = vbound(value / 10000, 0, 255); return true;
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
    case Z30R_GAMESUSPEND: q30_set_game_suspend(Z30_INDEX, value != 0); return true;     // (a bool array: not /10000)
    case Z30R_MUSICUPDATECOND:
        zasm30_store_set(arg, 0, Z30_INDEX, value / 10000);
        return true;
    case Z30R_GAMEASUBYOFF: return true;     // (read only, as upstream's)
    case Z30R_GAMEEVENTDATA:
        if(Z30_INDEX >= 0 && Z30_INDEX < (long)zasm30_event_data.size())
            zasm30_event_data[Z30_INDEX] = value;
        return true;
    case Z30R_FFRULE: if(Z30_INDEX >= 0 && Z30_INDEX < QUESTRULES_SIZE * 8) set_bit(quest_rules, Z30_INDEX, value ? 1 : 0); q30_script_rule_set(Z30_INDEX, value != 0);
        if(Z30_INDEX == qr_LTTPWALK) Link.q30_set_diag_move(value != 0);     // (upstream's apply_qr_rule)
        else if(Z30_INDEX == qr_LTTPCOLLISION) Link.q30_set_big_hitbox(value != 0);
        return true;
    case Z30R_FFCLAYER: if(ri->ffcref >= 0 && ri->ffcref < tmpscr->ffc_count()) tmpscr->fflayer[ri->ffcref] = (signed char)(vbound(value / 10000, 0, 7) - 1); return true;
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
    // Viewport-> (upstream's viewport_set_register). The camera draws a script's or a centred
    // viewport inside a scrolling region (as far as a screen around Link's).
    case Z30R_VIEWPORT_X: q30_viewport_set_xy(false, value / 10000); return true;
    case Z30R_VIEWPORT_Y: q30_viewport_set_xy(true, value / 10000); return true;
    case Z30R_VIEWPORT_WIDTH:
        if(z30_viewport_in(value / 10000, 0, 256))
            q30_viewport_set_size(false, value / 10000);
        return true;
    case Z30R_VIEWPORT_HEIGHT:
        if(z30_viewport_in(value / 10000, 0, 232))
            q30_viewport_set_size(true, value / 10000);
        return true;
    case Z30R_HEROSHIELDJINX: zasm30_shield_jinx = value / 10000; return true;
    case Z30R_VIEWPORT_MODE:
        if(z30_viewport_in(value, 0, 2))
            q30_viewport_set_mode(value);
        return true;
    case Z30R_VIEWPORT_TARGET:
        if(z30_sprite(value).s)
            q30_viewport_set_target(value);
        return true;
    case Z30R_VIEWPORT_DEADZONE_WIDTH:
    case Z30R_VIEWPORT_DEADZONE_HEIGHT:
    case Z30R_VIEWPORT_LOOKAHEAD_X:
    case Z30R_VIEWPORT_LOOKAHEAD_Y:
    case Z30R_VIEWPORT_RECENTER_DELAY:
    {
        const int which = z30_viewport_follow_reg(arg), v = value / 10000;
        const long hi = which == Q30_VP_DEADZONE_W ? 240 : which == Q30_VP_DEADZONE_H ? 160
                        : which == Q30_VP_RECENTER_DELAY ? 65535 : 127;
        const long lo = which == Q30_VP_LOOKAHEAD_X || which == Q30_VP_LOOKAHEAD_Y ? -128 : 0;

        if(z30_viewport_in(v, lo, hi))
            q30_viewport_set_follow(which, v);
        return true;
    }
    case Z30R_VIEWPORT_LOOKAHEAD_SPEED:
    case Z30R_VIEWPORT_RECENTER_SPEED:
        // fractional pixels a frame, 0 to 240 (upstream's validate_viewport_follow_speed)
        if(value < 0 || value > 240 * 10000)
            Z_scripterrlog("Invalid value: %ld.%04ld - must be >= 0 and <= 240\n", value / 10000, labs(value % 10000));
        else
            q30_viewport_set_follow(z30_viewport_follow_reg(arg), value);
        return true;

    // Game->MaxNPCs(n) and the others: 1 to 1024, as upstream (MAX_LWPN_SPRITES and the
    // others); adding past the limit fails, as upstream. Every list starts at 255.
    case Z30R_SPRITEMAXNPC:
    case Z30R_SPRITEMAXLWPN:
    case Z30R_SPRITEMAXEWPN:
    case Z30R_SPRITEMAXITEM:
    {
        sprite_list &l = arg == Z30R_SPRITEMAXNPC ? guys : arg == Z30R_SPRITEMAXLWPN ? Lwpns
                       : arg == Z30R_SPRITEMAXEWPN ? Ewpns : items;
        l.setMax((int)vbound(value / 10000, 1, SLMAX_ALLOC));

        if(l.getMax() > SLMAX)     // once per list: a quest that holds more than 2.53 could
        {
            static bool said[4];
            const int k = arg == Z30R_SPRITEMAXNPC ? 0 : arg == Z30R_SPRITEMAXLWPN ? 1 : arg == Z30R_SPRITEMAXEWPN ? 2 : 3;

            if(!said[k])
                al_trace("ZASM: a script set a sprite list's limit to %d (list %d)\n", l.getMax(), k);

            said[k] = true;
        }

        return true;
    }

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
    case Z30R_HEROFAKEZ:    // (upstream's setFakeZ takes whole pixels without qr_SPRITEXY_IS_FLOAT)
        zasm30_sprite_state(Link.getUID()).fakez = zc_max(q30_quest_rule(644) ? value : value / 10000 * 10000, 0L);
        return true;
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
        q30_script_combo_set(Z30_REF(COMBODATA), 3, value / 10000);
        return true;
    }
    case Z30R_COMBODATTRISHORTS: z30_combo_attr_set(Z30_REF(COMBODATA), 16 + Z30_INDEX, (value / 10000) * 10000); return true;
    // mapdata->
    case Z30R_MAPDATACOMBOSD:
    case Z30R_MAPDATACOMBOED:
    {
        long n = Z30_INDEX;
        mapscr *m = z30_mapdata_combo(Z30_REF(MAPDATA), n);
        if(m && n >= 0 && n < (long)m->data.size() && m->data[n] < MAXCOMBOS)
            q30_script_combo_set(m->data[n], arg == Z30R_MAPDATACOMBOED ? 3 : 2, value / 10000);
        return true;
    }
    case Z30R_COMBOED:      // Screen->ComboE[]: the effect squares of the combo there
    {
        int pos = Z30_INDEX;
        bool cur;
        mapscr *cs = zasm30_combo_screen(&pos, &cur);
        if(pos >= 0 && pos < 176)
            q30_script_combo_set(cs->data[pos], 3, value / 10000);
        return true;
    }
    case Z30R_MAPDATAFFSCRIPT:
    {
        const long n = Z30_INDEX, v = value / 10000;
        mapscr *m = z30_ffc_room(z30_mapdata_now(), n);
        if(m && n >= 0 && n < Z30_FFC_COUNT && v >= 0 && v < NUMSCRIPTFFC)
            m->ffscript[n] = (word)v;
        return true;
    }
    case Z30R_MAPDATAFFX:
    {
        const long n = Z30_INDEX;
        mapscr *m = z30_ffc_room(z30_mapdata_now(), n);
        if(m && n >= 0 && n < Z30_FFC_COUNT)
            m->ffx[n] = value;
        return true;
    }
    case Z30R_MAPDATAFFY:
    {
        const long n = Z30_INDEX;
        mapscr *m = z30_ffc_room(z30_mapdata_now(), n);
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
    case Z30R_MAPDATAPATTERN:
    {
        mapscr *m = z30_mapdata_now();
        if(m)
            m->pattern = (byte)vbound(value / 10000, 0, 255);
        return true;
    }
    case Z30R_MAPDATATILEWARPDMAP:
    case Z30R_MAPDATATILEWARPSCREEN:
    case Z30R_MAPDATATILEWARPTYPE:
    case Z30R_MAPDATASIDEWARPTYPE:
    case Z30R_MAPDATATILEWARPOVFLAGS:
    case Z30R_MAPDATASIDEWARPOVFLAGS:
    {
        mapscr *m = z30_mapdata_now();
        const long n = Z30_INDEX;
        if(!m || n < 0 || n >= 4)
            return true;
        const byte b = (byte)vbound(value / 10000, 0, 255);
        switch(arg)
        {
        case Z30R_MAPDATATILEWARPDMAP: m->tilewarpdmap[n] = (word)vbound(value / 10000, 0, MAXDMAPS - 1); break;
        case Z30R_MAPDATATILEWARPSCREEN: m->tilewarpscr[n] = b; break;
        case Z30R_MAPDATATILEWARPTYPE: m->tilewarptype[n] = b; break;
        case Z30R_MAPDATASIDEWARPTYPE: m->sidewarptype[n] = b; break;
        case Z30R_MAPDATATILEWARPOVFLAGS:
            if(value) m->tilewarpoverlayflags |= (1 << n); else m->tilewarpoverlayflags &= ~(1 << n);
            break;
        case Z30R_MAPDATASIDEWARPOVFLAGS:
            if(value) m->sidewarpoverlayflags |= (1 << n); else m->sidewarpoverlayflags &= ~(1 << n);
            break;
        }
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
            const int v = vbound(value / 10000, -1, q30_items_end() - 1);
            if(v > -1) set_mapscr_item(m, v);
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
    case Z30R_EWPNFAKEZ: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) st->fakez = z30_wpn_fakez_write(value); return true; }
    case Z30R_EWPNFAKEJUMP: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); if(st) st->fakefall = z30_fake_jump_write(value); return true; }
    case Z30R_LWPNFAKEZ: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); if(st) st->fakez = z30_wpn_fakez_write(value); return true; }
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
    case Z30R_ITEMMOVEFLAGS: { item *it = z30_item_ref(); Zasm30SpriteState *st = it ? zasm30_item_state(it) : NULL; if(st) z30_flag_set(st->move_flags, Z30_INDEX, 11, value != 0); return true; }
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
    case Z30R_IDATAFLAGS:
    {
        if(unsigned(ri->idata) < unsigned(q30_items_end()) && Z30_INDEX >= 0 && Z30_INDEX < 16)
        {
            const dword bit = z30_idata_flag_bit(Z30_INDEX);
            const dword f = value ? (z30_idata_flags(ri->idata) | bit) : (z30_idata_flags(ri->idata) & ~bit);
            Q30_IDATA(ri->idata).flags = (word)(f & 0xFFFF);
            q30_set_item_flags32(ri->idata, f);
        }
        return true;
    }
    case Z30R_IDATAATTRIB:
    {
        if(unsigned(ri->idata) < unsigned(q30_items_end()) && Z30_INDEX >= 0 && Z30_INDEX < 10)
        {
            itemdata &it = Q30_IDATA(ri->idata);
            long *fields[10] = { &it.misc1, &it.misc2, &it.misc3, &it.misc4, &it.misc5, &it.misc6, &it.misc7, &it.misc8, &it.misc9, &it.misc10 };
            *fields[Z30_INDEX] = value / 10000;
        }
        return true;
    }
    case Z30R_IDATAMINHEARTS:
        if(unsigned(ri->idata) < unsigned(q30_items_end()))
            Q30_IDATA(ri->idata).pickup_hearts = (byte)vbound(value / 10000, 0, 255);
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

// Link's tile as upstream's herotile sets it: Hero->ScriptTile, when a script set one, is the tile
// itself (Hero->Tile reads it), with no tile modifier or animation frame added.
void q30_hero_script_tile(int *tile)
{
    if(zasm30_ffscript_version && zasm30_hero_script_tile > 0)
        *tile = (int)zasm30_hero_script_tile;
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
        q30_auto_rotate(s, st);     // (q30_scripts_live.h)
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
    zasm30_shield_jinx = 0;
    zasm30_live_reset();         // (q30_scripts_live.h)
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

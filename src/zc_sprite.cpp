//--------------------------------------------------------
//  Zelda Classic
//  by Jeremy Craner, 1999-2000
//
//  sprite.cc
//
//  Sprite classes:
//   - sprite:      base class for the guys and enemies in zelda.cc
//   - movingblock: the moving block class
//   - sprite_list: main container class for different groups of sprites
//   - item:        items class
//
//--------------------------------------------------------

#include "precompiled.h" //always first

#include "sprite.h"
#include "zelda.h"
#include "maps.h"
#include "tiles.h"
#include "guys.h"
#include "zc_sys.h"
#include "port/game/q30_triggers.h"

class item;
dword q30_item_moveflags(item *i, long *fakez);     // q30_scripts_engine.h: a live item's move flags and FakeZ

/*
void sprite::check_conveyor()
{
  if (conveyclk<=0)
  {
    int ctype=(combobuf[MAPCOMBO(x+8,y+8)].type);
    if((ctype>=cOLD_CVUP) && (ctype<=cOLD_CVRIGHT))
    {
      switch (ctype-cOLD_CVUP)
      {
        case up:
        if(!_walkflag(x,y+8-2,2))
        {
          y=y-2;
        }
        break;
        case down:
        if(!_walkflag(x,y+15+2,2))
        {
          y=y+2;
        }
        break;
        case left:
        if(!_walkflag(x-2,y+8,1))
        {
          x=x-2;
        }
        break;
        case right:
        if(!_walkflag(x+15+2,y+8,1))
        {
          x=x+2;
        }
        break;
      }
    }
  }
}
*/

// ---- 2.55/3.0 conveyors (upstream zc_sprite.cpp: get_conveyor, sprite::check_conveyor) ---------------
//
// A conveyor's "Custom speed" flag (flag 2) takes its X / Y speed from attributes 0 / 1 and moves every
// attribute-8th frame of newconveyorclk (the Hero's check counts it); conveyors on layers 1-2 / all
// layers and FFCs (rules), under their effect squares and not under a bridge.

byte q30_newconveyorclk = 0;
int q30_conveyor_stun = 0;      // upstream's is_conveyor_stunned (q30_link_stunned)

static bool q30_is_conveyor(int cid)
{
    const int t = combobuf[cid].type;
    return combo_class_buf[t].conveyor_x_speed || combo_class_buf[t].conveyor_y_speed;
}

// upstream effectflag(x, y, layer - 1): the combo's effect square there, unless a bridge above covers it
static bool q30_conveyor_effect(int x, int y, int layer)
{
    if(q30_has_bridge_above(x, y, layer))
        return false;

    const int cid = layer ? MAPCOMBO2(layer - 1, x, y) : MAPCOMBO(x, y);
    const Q30Combo *c = q30_combo(cid);
    const int b = ((x & 8) ? 4 : 1) << ((y & 8) ? 1 : 0);
    return c ? ((c->walk >> 4) & b) != 0 : true;
}

int q30_get_conveyor(int x, int y, bool check_rates)
{
    x = vbound(x, 0, 255);
    y = vbound(y, 0, 175);
    const int maxlayer = q30_quest_rule(Q30_QR_CONVEYORS_ALL_LAYERS) ? 6 : (q30_quest_rule(Q30_QR_CONVEYORS_L1_L2) ? 2 : 0);
    int found_layer = -1, cid = -1;
    const int ffc_cid = maxlayer == 6 ? MAPFFCOMBO(x, y) : 0;

    if(ffc_cid && q30_is_conveyor(ffc_cid))
    {
        cid = ffc_cid;
        found_layer = 999;
    }
    else for(int q = maxlayer; q >= 0; --q)
    {
        if(q > 0 && (tmpscr2[q - 1].valid == 0 || tmpscr2[q - 1].data.empty()))
            continue;

        const int c = q ? MAPCOMBO2(q - 1, x, y) : MAPCOMBO(x, y);

        if(q30_is_conveyor(c) && q30_conveyor_effect(x, y, q))
        {
            found_layer = q;
            cid = c;
            break;
        }
    }

    if(cid < 0)
        return -1;

    const Q30Combo *qc = q30_combo(cid);
    const bool custom_spd = qc && (qc->usrflags & 0x2);

    if(!check_rates || custom_spd || conveyclk <= 0)
    {
        if(found_layer < 6 && q30_has_bridge_above(x, y, found_layer))
            return -1;

        if(check_rates)
        {
            const int rate = custom_spd ? zc_max((int)(qc->attributes[8] / 10000), 1) : 3;

            if(custom_spd && (q30_newconveyorclk % rate))
                return -1;
        }

        return cid;
    }

    return -1;
}

// (q30_zfix_int: zfix's getInt, rounding half away from zero: a sprite's conveyor speeds are whole pixels)

// upstream sprite::on_sideview_solid: an item also stands on sideview platforms and ladder tops
// (uses_sideview_platforms: items, unless qr_ITEMS_IGNORE_SIDEVIEW_PLATFORMS; on a tile's edge only)
static bool q30_conveyor_sv_solid(sprite *s, bool hardcoded, bool is_item)
{
    if(!(tmpscr->flags7 & fSIDEVIEW))
        return false;

    const int lx = hardcoded ? int(s->x) : int(s->x) + s->hxofs;
    const int by = hardcoded ? int(s->y) + 16 : int(s->y) + s->hyofs + s->hysz;
    const bool platforms = is_item && !q30_quest_rule(179) && (by % 16) == 0;

    for(int xo = zc_min(4, s->hxsz / 2); xo < s->hxsz; xo += 8)
        if(_walkflag(lx + xo, by, 1) || (platforms && q30_sideview_platform_at(lx + xo, by)))
            return true;

    if(_walkflag(lx + s->hxsz - 1, by, 1) || (platforms && q30_sideview_platform_at(lx + s->hxsz - 1, by)))
        return true;

    return by >= 176 && currscr >= 0x70 && !(tmpscr->flags2 & wfDOWN);
}

// upstream sprite::check_conveyor for an item (its hit box unless qr_ITEM_CONVEYORS_IGNORE_HITBOX) or a
// weapon that obeys conveyors; the combo it is on, or -1
int q30_sprite_conveyor(sprite *s, bool is_item, dword moveflags, long fakez)
{
    bool use_hitbox = !(is_item && q30_quest_rule(577));
    const int hw = s->hxsz, hh = s->hysz;
    const bool hardcoded = q30_quest_rule(is_item ? 578 : 579);    // qr_*_HARDCODED_SIDEVIEW_HITBOX
    int lx, ty, cx, cy, by;

    if(use_hitbox)
    {
        lx = int(s->x) + s->hxofs;
        ty = int(s->y) + s->hyofs;
        cx = lx + hw / 2;
        cy = ty + hh / 2;
        by = ty + hh;
    }
    else
    {
        lx = int(s->x);
        ty = int(s->y);
        cx = lx + 8;
        cy = ty + 8;
        by = ty + 16;
    }

    int cid = q30_get_conveyor(cx, cy, false);
    const bool is_sv = cid < 0 && !q30_quest_rule(576) && q30_conveyor_sv_solid(s, hardcoded, is_item);   // qr_BROKEN_SV_SOLID_CONVEYORS

    if(is_sv)
    {
        if(hardcoded)
        {
            use_hitbox = false;
            lx = int(s->x); ty = int(s->y); cx = lx + 8; cy = ty + 8; by = ty + 16;
        }

        for(int xo = 0; xo < (use_hitbox ? hw : 16) && cid < 0; xo += 4)
            cid = q30_get_conveyor(lx + xo, by, false);

        if(cid < 0)
            cid = q30_get_conveyor(lx + (use_hitbox ? hw : 16) - 1, by, false);
    }

    if(cid < 0)
        return -1;

    const Q30Combo *qc = q30_combo(cid);
    const dword usr = qc ? qc->usrflags : 0;

    if(s->z > 0 || fakez > 0 || !(moveflags & 1))      // in the air (or not obeying gravity)
        if(!(tmpscr->flags2 & fAIRCOMBOS) && !(usr & 0x40))     // flag 7: moves airborne sprites
            return -1;

    const bool custom_spd = (usr & 0x2) != 0;
    int deltax, deltay;

    if(custom_spd)
    {
        deltax = q30_zfix_int(qc->attributes[0]);
        deltay = q30_zfix_int(qc->attributes[1]);
    }
    else
    {
        deltax = combo_class_buf[combobuf[cid].type].conveyor_x_speed;
        deltay = combo_class_buf[combobuf[cid].type].conveyor_y_speed;
    }

    if(is_sv)
        deltay = 0;

    if(!deltax && !deltay)
        return -1;

    const int rate = custom_spd ? zc_max((int)(qc->attributes[8] / 10000), 1) : 3;

    if(q30_newconveyorclk % rate)
        return cid;

    bool failed = false;

    if(deltay < 0)
    {
        if(!use_hitbox)
            failed = _walkflag(int(s->x), int(s->y) + 8 - 2, 2);
        else
        {
            for(int xo = zc_min(4, hw / 2); xo < hw && !failed; xo += 4)
                failed = _walkflag(lx + xo, ty + deltay, 1);

            if(!failed)
                failed = _walkflag(lx + hw - 1, ty + deltay, 1);
        }

        if(!failed)
            s->y = s->y - abs(deltay);
    }
    else if(deltay > 0)
    {
        if(!use_hitbox)
            failed = _walkflag(int(s->x), int(s->y) + 15 + 2, 2);
        else
        {
            for(int xo = zc_min(4, hw / 2); xo < hw && !failed; xo += 4)
                failed = _walkflag(lx + xo, ty + hh - 1 + deltay, 1);

            if(!failed)
                failed = _walkflag(lx + hw - 1, ty + hh - 1 + deltay, 1);
        }

        if(!failed)
            s->y = s->y + abs(deltay);
    }

    failed = false;

    if(deltax < 0)
    {
        if(!use_hitbox)
            failed = _walkflag(int(s->x) - 2, int(s->y) + 8, 1);
        else
        {
            for(int yo = zc_min(4, hh / 2); yo < hh && !failed; yo += 4)
                failed = _walkflag(lx + deltax, ty + yo, 1);

            if(!failed)
                failed = _walkflag(lx + deltax, ty + hh - 1, 1);
        }

        if(!failed)
            s->x = s->x - abs(deltax);
    }
    else if(deltax > 0)
    {
        if(!use_hitbox)
            failed = _walkflag(int(s->x) + 15 + 2, int(s->y) + 8, 1);
        else
        {
            for(int yo = zc_min(4, hh / 2); yo < hh && !failed; yo += 4)
                failed = _walkflag(lx + hw - 1 + deltax, ty + yo, 1);

            if(!failed)
                failed = _walkflag(lx + hw - 1 + deltax, ty + hh - 1, 1);
        }

        if(!failed)
            s->x = s->x + abs(deltax);
    }

    return cid;
}

void sprite::check_conveyor()
{
    int deltax=0;
    int deltay=0;

    if(q30_active())
    {
        // (only the item list calls this; weapons: q30_weapon_conveyor)
        long fakez = 0;
        const dword mf = q30_item_moveflags((item *)this, &fakez);
        q30_sprite_conveyor(this, true, mf, fakez);
        return;
    }

    if(conveyclk<=0 && (z==0 || (tmpscr->flags2&fAIRCOMBOS)))
    {
        int ctype=(combobuf[MAPCOMBO(x+8,y+8)].type);
        deltax=combo_class_buf[ctype].conveyor_x_speed;
        deltay=combo_class_buf[ctype].conveyor_y_speed;
        
        if(deltax!=0||deltay!=0)
        {
            if(deltay<0&&!_walkflag(x,y+8-2,2))
            {
                y=y-abs(deltay);
            }
            else if(deltay>0&&!_walkflag(x,y+15+2,2))
            {
                y=y+abs(deltay);
            }
            
            if(deltax<0&&!_walkflag(x-2,y+8,1))
            {
                x=x-abs(deltax);
            }
            else if(deltax>0&&!_walkflag(x+15+2,y+8,1))
            {
                x=x+abs(deltax);
            }
        }
    }
}

// ---- 2.55+ push blocks -------------------------------------------------------------
//
// One block moves at a time, so its layer lives here rather than in the class. A push
// from layer 0 leaves it at 0, which is the only value a 2.53 quest ever sees.

extern movingblock mblock2;

static int g_block_layer = 0;
static int g_next_block_layer = 0;

int q30_mblock_layer()
{
    return g_block_layer;
}

// A layer's screen, or NULL if the current screen has no such layer.
static mapscr *block_screen(int layer)
{
    if(layer <= 0)
        return tmpscr;

    mapscr *m = &tmpscr2[layer - 1];
    return (m->valid && !m->data.empty()) ? m : NULL;
}

// upstream's is_push_flag; d < 0 means any direction
static bool push_flag(int f, int d)
{
    switch(f)
    {
    case mfPUSHUD: case mfPUSHUDNS: case mfPUSHUDINS: return d < 0 || d <= down;
    case mfPUSHLR: case mfPUSHLRNS: case mfPUSHLRINS: return d < 0 || d >= left;
    case mfPUSHU: case mfPUSHUNS: case mfPUSHUINS: return d < 0 || d == up;
    case mfPUSHD: case mfPUSHDNS: case mfPUSHDINS: return d < 0 || d == down;
    case mfPUSHL: case mfPUSHLNS: case mfPUSHLINS: return d < 0 || d == left;
    case mfPUSHR: case mfPUSHRNS: case mfPUSHRINS: return d < 0 || d == right;
    case mfPUSH4: case mfPUSH4NS: case mfPUSH4INS: return true;
    }

    return false;
}

// upstream's is_push: does this position hold a push block?
static bool is_push_at(mapscr *m, int pos)
{
    if(push_flag(m->sflag[pos], -1) || push_flag(combobuf[m->data[pos]].flag, -1))
        return true;

    const Q30Combo *c = q30_combo(m->data[pos]);
    return c && (c->type == Q30C_PUSHBLOCK || (c->type == Q30C_SWITCHHOOK && (c->usrflags & 0x40)));
}

static bool flag_here(int layer, int x, int y, int flag)
{
    return MAPFLAG2(layer - 1, x, y) == flag || MAPCOMBOFLAG2(layer - 1, x, y) == flag;
}

// Is the block's way clear from this layer? The port's own checks, plus the top half.
static bool block_can_go(int layer, int bx, int by, int d)
{
    int nx = bx, ny = by;       // the half tile checked first
    int tx = bx, ty = by;       // the top half

    switch(d)
    {
    case up: ny = by - 8; ty = by - 16; break;
    case down: ny = by + 24; ty = by + 16; break;
    case left: nx = bx - 16; ny = by + 8; tx = bx - 16; break;
    default: nx = bx + 16; ny = by + 8; tx = bx + 16; break;
    }

    if(get_bit(quest_rules, qr_SOLIDBLK) && !flag_here(layer, nx, ny, mfBLOCKHOLE))
    {
        if(_walkflag(nx, ny, 2))
            return false;

        if(!q30_compat_rule(Q30_QR_BROKEN_PUSHBLOCK_TOP_HALF_SOLIDS) && _walkflag(tx, ty, 2))
            return false;
    }

    return !flag_here(layer, nx, ny, mfNOBLOCKS);
}

// checkpushblock for layers 2 and 1, topmost first so stacked blocks move the top one.
bool q30_push_layer_block(int bx, int by, int d, int pushing, bool glove_spent)
{
    if(!q30_quest_rule(Q30_QR_PUSHBLOCK_LAYER_1_2) || blockmoving || mblock2.clk > 0)
        return false;

    if(get_bit(quest_rules, qr_HESITANTPUSHBLOCKS) && pushing < 4)
        return false;

    int pos = (by & 0xF0) + (bx >> 4);

    if(pos < 0 || pos > 175)
        return false;

    for(int lyr = 2; lyr >= 1; --lyr)
    {
        mapscr *m = block_screen(lyr);

        if(!m)
            continue;

        int cid = m->data[pos];
        int f = m->sflag[pos], f2 = combobuf[cid].flag, t = combobuf[cid].type;
        const Q30Combo *qc = q30_combo(cid);
        bool generic = qc && qc->type == Q30C_PUSHBLOCK;
        bool wait = t == cPUSH_WAIT || t == cPUSH_HW || t == cPUSH_HW2 || (generic && (qc->usrflags & 0x20));
        int heavy = (t == cPUSH_HW || t == cPUSH_HEAVY) ? 1 : (t == cPUSH_HEAVY2 || t == cPUSH_HW2) ? 2 : 0;

        if(generic)
            heavy = qc->attributes[8] / 10000;

        if(wait && (pushing < 16 || hasMainGuy()))
            continue;

        if(heavy)
        {
            int glove = current_item_id(itype_bracelet);

            if(glove < 0 || itemsbuf[glove].power < heavy || glove_spent)
                continue;
        }

        bool doit;

        if(generic && f != mfPUSHED)
            doit = (qc->usrflags & (d == up ? 0x1 : d == down ? 0x2 : d == left ? 0x4 : 0x8)) != 0;
        else
            doit = push_flag(f, d) || (push_flag(f2, d) && f != mfPUSHED);

        if(!doit || !block_can_go(lyr, bx, by, d))
            continue;

        if(push_flag(f, d))
            m->sflag[pos] = 0;

        g_next_block_layer = lyr;
        mblock2.push((fix)bx, (fix)by, d, f);

        if(generic)
        {
            if(qc->attributes[9] / 10000)
                sfx(qc->attributes[9] / 10000, pan(bx));
        }
        else if(get_bit(quest_rules, qr_MORESOUNDS))
            sfx(WAV_ZN1PUSHBLOCK, bx);

        return true;
    }

    return false;
}

// A block coming to rest: the moving block, or one the switch hook puts down.
struct BlockLanding
{
    int x, y, bcombo, oldcset, cs, oldflag, dir, layer;
    bool force_many;            // a switch-hook combo that counts as a block: never locks
    bool trigger, hole;         // out
};

static void land_block(BlockLanding &b);

// Upstream's end of movingblock::animate for 2.55+ quests, with block triggers and holes
// looked for on the block's own layer and, under qr_PUSHBLOCK_LAYER_1_2, the others.
static void block_landed(movingblock &mb)
{
    BlockLanding b;
    b.x = int(mb.x);
    b.y = int(mb.y);
    b.bcombo = mb.bcombo;
    b.oldcset = mb.oldcset;
    b.cs = mb.cs;
    b.oldflag = mb.oldflag;
    b.dir = mb.dir;
    b.layer = g_block_layer;
    b.force_many = false;

    blockmoving = false;
    g_block_layer = 0;
    land_block(b);
    mb.trigger = b.trigger;
}

bool q30_switch_block_land(int x, int y, int combo, int cset, int layer, int placed_flag, int dir, bool force_many, bool *hole)
{
    BlockLanding b;
    b.x = x;
    b.y = y;
    b.bcombo = combo;
    b.oldcset = b.cs = cset;
    b.oldflag = placed_flag;
    b.dir = dir;
    b.layer = layer;
    b.force_many = force_many;
    b.trigger = b.hole = false;

    // a block still sliding keeps going
    bool moving = blockmoving;
    land_block(b);
    blockmoving = moving;

    if(hole)
        *hole = b.hole;

    return b.trigger;
}

// A block hole where the block stops, on its layer or (unless qr_BLOCKHOLE_SAME_ONLY)
// another push layer.
static bool hole_at(int layer, int pos)
{
    int maxlayer = q30_quest_rule(Q30_QR_PUSHBLOCK_LAYER_1_2) ? 2 : 0;

    for(int lyr = 0; lyr <= maxlayer; ++lyr)
    {
        if(lyr != layer && q30_quest_rule(Q30_QR_BLOCKHOLE_SAME_ONLY))
            continue;

        mapscr *o = block_screen(lyr);

        if(o && (o->sflag[pos] == mfBLOCKHOLE || combobuf[o->data[pos]].flag == mfBLOCKHOLE))
            return true;
    }

    return false;
}

// 2.55's push blocks drop into a pitfall they stop on (a block hole there is filled
// instead): the pit's sound, upstream's falling sprite for 70 frames, and the block is gone.
static bool block_fell(movingblock &mb)
{
    int bx = int(mb.x), by = int(mb.y);
    int pit = q30_pitfall_at(bx + 8, by + 8);
    int pos = (by & 0xF0) + (bx >> 4);

    if(!pit || pos < 0 || pos > 175 || hole_at(g_block_layer, pos))
        return false;

    const Q30Combo *pc = q30_combo(pit);
    const Q30Combo *bc = q30_combo(mb.bcombo);
    int s = pc ? int(pc->attributes[8] / 10000) : 0;

    if(bc && bc->sfx_falling)
        s = bc->sfx_falling;

    if(s > 0)
        sfx(s, pan(bx));

    int spr = q30_misc_sprite(0);       // sprFALL

    if(bc && bc->spr_falling)
        spr = bc->spr_falling;

    if(spr >= 0 && spr <= 255)
        q30_add_sprite_deco(bx, by, spr, 70, true);     // PITFALL_FALL_FRAMES

    blockmoving = false;
    g_block_layer = 0;
    mb.trigger = false;

    if(getenv("ZC_SW_DEBUG"))
        al_trace("swdbg block fell at %d,%d pit %d sfx %d sprite %d\n", bx, by, pit, s, spr);

    return true;
}

static void land_block(BlockLanding &b)
{
    int pos = (b.y & 0xF0) + (b.x >> 4);
    int layer = b.layer;
    mapscr *m = block_screen(layer);

    b.trigger = b.hole = false;

    if(!m || pos < 0 || pos > 175)
        return;

    int maxlayer = q30_quest_rule(Q30_QR_PUSHBLOCK_LAYER_1_2) ? 2 : 0;
    bool no_trig_replace = q30_quest_rule(Q30_QR_BLOCKS_DONT_LOCK_OTHER_LAYERS);
    bool same_only = q30_quest_rule(Q30_QR_BLOCKHOLE_SAME_ONLY);
    bool trig_is_layer = false;
    int f1 = m->sflag[pos];
    int f2 = combobuf[m->data[pos]].flag;
    bool hole = f1 == mfBLOCKHOLE || f2 == mfBLOCKHOLE;

    b.trigger = false;
    m->data[pos] = b.bcombo;
    m->cset[pos] = b.oldcset;

    if(f1 == mfBLOCKTRIGGER || f2 == mfBLOCKTRIGGER)
        b.trigger = true;
    else if(!same_only)
    {
        for(int lyr = 0; lyr <= maxlayer; ++lyr)
        {
            mapscr *o = lyr == layer ? NULL : block_screen(lyr);

            if(o && (o->sflag[pos] == mfBLOCKTRIGGER || combobuf[o->data[pos]].flag == mfBLOCKTRIGGER))
            {
                b.trigger = true;
                trig_is_layer = true;

                if(!no_trig_replace)
                {
                    o->data[pos] = o->undercombo;
                    o->cset[pos] = o->undercset;
                    o->sflag[pos] = 0;
                }
            }
        }
    }

    bool locks_here = b.trigger && !(no_trig_replace && trig_is_layer);

    if(locks_here)
        m->sflag[pos] = mfPUSHED;

    if(hole)
        m->data[pos] += 1;
    else if(!same_only)
    {
        for(int lyr = 0; lyr <= maxlayer; ++lyr)
        {
            mapscr *o = lyr == layer ? NULL : block_screen(lyr);

            if(o && (o->sflag[pos] == mfBLOCKHOLE || combobuf[o->data[pos]].flag == mfBLOCKHOLE))
            {
                m->data[pos] = m->undercombo;
                m->cset[pos] = m->undercset;
                o->data[pos] = b.bcombo + 1;
                o->cset[pos] = b.oldcset;
                o->sflag[pos] = mfNONE;
                hole = true;
                break;
            }
        }
    }

    int d = b.dir;
    b.hole = hole;

    if(hole)
        m->sflag[pos] = mfNONE;
    else
    {
        f2 = combobuf[m->data[pos]].flag;

        if(!(b.force_many || (f2 == mfPUSHUDINS && d <= down) || (f2 == mfPUSHLRINS && d >= left) || (f2 == mfPUSHUINS && d == up) ||
             (f2 == mfPUSHDINS && d == down) || (f2 == mfPUSHLINS && d == left) || (f2 == mfPUSHRINS && d == right) ||
             f2 == mfPUSH4INS))
            m->sflag[pos] = mfPUSHED;
    }

    // every block trigger on the push layers has to be covered
    bool didtrigger = b.trigger;

    for(int lyr = 0; didtrigger && lyr <= maxlayer; ++lyr)
    {
        mapscr *s = block_screen(lyr);

        for(int i = 0; s && didtrigger && i < 176; ++i)
        {
            if((!same_only || lyr == layer) && i == pos)
                continue;

            if(s->sflag[i] != mfBLOCKTRIGGER && combobuf[s->data[i]].flag != mfBLOCKTRIGGER)
                continue;

            bool found = false;

            for(int l2 = 0; no_trig_replace && !found && l2 <= maxlayer; ++l2)
            {
                mapscr *o = block_screen(l2);
                found = o && is_push_at(o, i);
            }

            if(!found)
                didtrigger = false;
        }
    }

    int oldflag = b.oldflag;

    if(oldflag >= mfPUSHUDINS && (oldflag <= mfPUSHRINS || q30_compat_rule(Q30_QR_BROKEN_PUSHBLOCK_FLAG_CLONING)) && !locks_here && !hole)
        m->sflag[pos] = oldflag;

    f2 = combobuf[m->data[pos]].flag;

    if(oldflag == mfPUSH4 || (oldflag == mfPUSHUD && d <= down) || (oldflag == mfPUSHLR && d >= left) ||
       (oldflag == mfPUSHU && d == up) || (oldflag == mfPUSHD && d == down) || (oldflag == mfPUSHL && d == left) ||
       (oldflag == mfPUSHR && d == right) || f2 == mfPUSH4 || (f2 == mfPUSHUD && d <= down) ||
       (f2 == mfPUSHLR && d >= left) || (f2 == mfPUSHU && d == up) || (f2 == mfPUSHD && d == down) ||
       (f2 == mfPUSHL && d == left) || (f2 == mfPUSHR && d == right) || didtrigger)
    {
        if(didtrigger && no_trig_replace)
        {
            // qr_BLOCKS_DONT_LOCK_OTHER_LAYERS leaves this until every trigger is covered:
            // lock the blocks now and clear the triggers.
            for(int lyr = 0; lyr <= maxlayer; ++lyr)
            {
                mapscr *s = block_screen(lyr);

                for(int i = 0; s && i < 176; ++i)
                {
                    if(s->sflag[i] != mfBLOCKTRIGGER && combobuf[s->data[i]].flag != mfBLOCKTRIGGER)
                        continue;

                    for(int l2 = 0; l2 <= maxlayer; ++l2)
                    {
                        mapscr *o = l2 == lyr ? NULL : block_screen(l2);

                        if(o && is_push_at(o, i))
                            o->sflag[i] = mfPUSHED;
                    }

                    s->data[i] = s->undercombo;
                    s->cset[i] = s->undercset;
                    s->sflag[i] = 0;
                }
            }
        }

        if(hiddenstair(0, true))
            sfx(tmpscr->secretsfx);
        else
        {
            hidden_entrance(0, true, true);

            int t = combobuf[b.bcombo].type;

            if(t == cPUSH_WAIT || t == cPUSH_HW || t == cPUSH_HW2 || didtrigger)
                sfx(tmpscr->secretsfx);
        }

        if(isdungeon() && (tmpscr->flags & fSHUTTERS))
            opendoors = 8;

        if(!isdungeon() || q30_quest_rule(Q30_QR_DUNGEON_DMAPS_PERM_SECRETS))
        {
            int t = combobuf[b.bcombo].type;

            if((q30_quest_rule(Q30_QR_NONHEAVY_BLOCKTRIGGER_PERM) || t == cPUSH_HEAVY || t == cPUSH_HW ||
                t == cPUSH_HEAVY2 || t == cPUSH_HW2) && !(tmpscr->flags5 & fTEMPSECRETS))
                setmapflag(mSECRET);
        }
    }

    if(layer == 0)
        putcombo(scrollbuf, b.x, b.y, b.bcombo, b.cs);
}

void movingblock::push(fix bx,fix by,int d2,int f)
{
    g_block_layer = g_next_block_layer;
    g_next_block_layer = 0;
    mapscr *m = block_screen(g_block_layer);
    trigger=false;
    endx=x=bx;
    endy=y=by;
    dir=d2;
    oldflag=f;

    if(q30_active())
    {
        // Upstream (movingblock::push): a block going right or down is drawn half a pixel back,
        // so it is seen to move on the same frame as one going left or up.
        xofs = (dir == right) ? (fix)(-0.5) : (fix)0;
        yofs = (fix)playing_field_offset - ((dir == down) ? (fix)0.5 : (fix)0);
    }

    word *di = &(m->data[(int(y)&0xF0)+(int(x)>>4)]);
    byte *ci = &(m->cset[(int(y)&0xF0)+(int(x)>>4)]);
    //   bcombo = ((*di)&0xFF)+(tmpscr->cpage<<8);
    bcombo =  m->data[(int(y)&0xF0)+(int(x)>>4)];
    oldcset = m->cset[(int(y)&0xF0)+(int(x)>>4)];
    cs     = (isdungeon() && !get_bit(quest_rules, qr_PUSHBLOCKCSETFIX)) ? 9 : oldcset;
    tile = combobuf[bcombo].tile;
    flip = combobuf[bcombo].flip;
    //   cs = ((*di)&0x700)>>8;
    *di = m->undercombo;
    *ci = m->undercset;
    if(!g_block_layer)
        putcombo(scrollbuf,x,y,*di,*ci);
    clk=32;
    blockmoving=true;
}

bool movingblock::animate(int index)
{
    //these are here to bypass compiler warnings about unused arguments
    index=index;


    if(clk<=0)
        return false;

    move((fix)0.5);

    if(--clk==0)
    {
        if(q30_active())
        {
            if(!block_fell(*this))
                block_landed(*this);

            return false;
        }

        bool bhole=false;
        blockmoving=false;
        int f1 = tmpscr->sflag[(int(y)&0xF0)+(int(x)>>4)];
        int f2 = MAPCOMBOFLAG(x,y);
        
        tmpscr->data[(int(y)&0xF0)+(int(x)>>4)]=bcombo;
        tmpscr->cset[(int(y)&0xF0)+(int(x)>>4)]=oldcset;
        
        if((f1==mfBLOCKTRIGGER)||f2==mfBLOCKTRIGGER)
        {
            trigger=true;
            tmpscr->sflag[(int(y)&0xF0)+(int(x)>>4)]=mfPUSHED;
            //the above line used to be in the following if statement.
            //However, it caused inherent-flag pushblocks to not lock into
            //block trigger combos unless the block trigger is also an
            //inherent flag
            /*
            if(f2==mfBLOCKTRIGGER)
            {
              tmpscr->sflag[(int(y)&0xF0)+(int(x)>>4)]=mfPUSHED;
            }
            */
        }
        
        if((f1==mfBLOCKHOLE)||f2==mfBLOCKHOLE)
        {
            tmpscr->data[(int(y)&0xF0)+(int(x)>>4)]+=1;
            bhole=true;
            //tmpscr->cset[(int(y)&0xF0)+(int(x)>>4)]=;
        }
        
        if(bhole)
        {
            tmpscr->sflag[(int(y)&0xF0)+(int(x)>>4)]=mfNONE;
        }
        else
        {
            f2 = MAPCOMBOFLAG(x,y);
            
            if(!((f2==mfPUSHUDINS && dir<=down) ||
                    (f2==mfPUSHLRINS && dir>=left) ||
                    (f2==mfPUSHUINS && dir==up) ||
                    (f2==mfPUSHDINS && dir==down) ||
                    (f2==mfPUSHLINS && dir==left) ||
                    (f2==mfPUSHRINS && dir==right) ||
                    (f2==mfPUSH4INS)))
            {
                tmpscr->sflag[(int(y)&0xF0)+(int(x)>>4)]=mfPUSHED;
            }
        }
        
        if(oldflag>=mfPUSHUDINS&&oldflag&&!trigger&&!bhole)
        {
            tmpscr->sflag[(int(y)&0xF0)+(int(x)>>4)]=oldflag;
        }
        
        for(int i=0; i<176; i++)
        {
            if(tmpscr->sflag[i]==mfBLOCKTRIGGER||combobuf[tmpscr->data[i]].flag==mfBLOCKTRIGGER)
            {
                trigger=false;
            }
        }
        
        //triggers a secret
        f2 = MAPCOMBOFLAG(x,y);
        
        if((oldflag==mfPUSH4 ||
            (oldflag==mfPUSHUD && dir<=down) ||
            (oldflag==mfPUSHLR && dir>=left) ||
            (oldflag==mfPUSHU && dir==up) ||
            (oldflag==mfPUSHD && dir==down) ||
            (oldflag==mfPUSHL && dir==left) ||
            (oldflag==mfPUSHR && dir==right) ||
            f2==mfPUSH4 ||
            (f2==mfPUSHUD && dir<=down) ||
            (f2==mfPUSHLR && dir>=left) ||
            (f2==mfPUSHU && dir==up) ||
            (f2==mfPUSHD && dir==down) ||
            (f2==mfPUSHL && dir==left) ||
            (f2==mfPUSHR && dir==right)) ||
           trigger)
            //if(oldflag<mfPUSHUDNS||trigger)
        {
            if(hiddenstair(0,true))
            {
                sfx(tmpscr->secretsfx);
            }
            else
            {
                hidden_entrance(0,true,true);
                
                if((combobuf[bcombo].type == cPUSH_WAIT) ||
                        (combobuf[bcombo].type == cPUSH_HW) ||
                        (combobuf[bcombo].type == cPUSH_HW2) || trigger)
                {
                    sfx(tmpscr->secretsfx);
                }
            }
            
            if(isdungeon() && tmpscr->flags&fSHUTTERS)
            {
                opendoors=8;
            }
            
            if(!isdungeon())
            {
                if(combobuf[bcombo].type==cPUSH_HEAVY || combobuf[bcombo].type==cPUSH_HW
                        || combobuf[bcombo].type==cPUSH_HEAVY2 || combobuf[bcombo].type==cPUSH_HW2)
                {
                    if(!(tmpscr->flags5&fTEMPSECRETS)) setmapflag(mSECRET);
                }
            }
        }
        
        putcombo(scrollbuf,x,y,bcombo,cs);
    }
    
    return false;
}

/*** end of sprite.cc ***/


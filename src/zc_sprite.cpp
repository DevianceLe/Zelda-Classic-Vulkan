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

void sprite::check_conveyor()
{
    int deltax=0;
    int deltay=0;
    
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
    return c && c->type == Q30C_PUSHBLOCK;
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

// Upstream's end of movingblock::animate for 2.55+ quests, with block triggers and holes
// looked for on the block's own layer and, under qr_PUSHBLOCK_LAYER_1_2, the others.
static void block_landed(movingblock &b)
{
    int pos = (int(b.y) & 0xF0) + (int(b.x) >> 4);
    int layer = g_block_layer;
    mapscr *m = block_screen(layer);

    blockmoving = false;
    g_block_layer = 0;

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

    if(hole)
        m->sflag[pos] = mfNONE;
    else
    {
        f2 = combobuf[m->data[pos]].flag;

        if(!((f2 == mfPUSHUDINS && d <= down) || (f2 == mfPUSHLRINS && d >= left) || (f2 == mfPUSHUINS && d == up) ||
             (f2 == mfPUSHDINS && d == down) || (f2 == mfPUSHLINS && d == left) || (f2 == mfPUSHRINS && d == right) ||
             f2 == mfPUSH4INS))
            m->sflag[pos] = mfPUSHED;
    }

    // every block trigger on the screen has to be covered
    bool didtrigger = b.trigger;

    for(int lyr = 0; didtrigger && lyr <= 6; ++lyr)
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


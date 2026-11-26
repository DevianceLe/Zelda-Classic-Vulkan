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

#ifndef _WEAPONS_H_
#define _WEAPONS_H_

#include "zdefs.h"
#include "sprite.h"

/**************************************/
/***********  Weapon Class  ***********/
/**************************************/

extern byte boomframe[16];
extern byte bszboomflip[4];

class weapon : public sprite
{
private:
    void seekLink();
    void seekEnemy(int j);
    int seekEnemy2(int j);
    
public:
    int power,type,dead,clk2,misc2,ignorecombo;
    bool isLit; //if true, this weapon is providing light to the current screen
    int parentid, //Enemy who created it
        parentitem; //Item which created it
    int dragging;
    fix step;
    bool bounce, ignoreLink;
    word flash,wid,aframe,csclk;
    int o_tile, o_cset, o_speed, o_type, frames, o_flip;
    int temp1;
    bool behind;
    int linkedItem;
    
    weapon(weapon const & other);
    weapon(fix X,fix Y,fix Z,int Id,int Type,int pow,int Dir, int Parentid, int prntid, bool isDummy=false);
    virtual ~weapon();
    void LOADGFX(int wpn);
    bool Dead();
    bool clip();
    bool blocked();
    virtual bool blocked(int xOffset, int yOffset);
    virtual bool animate(int index);
    bool animate_body(int index);   // (animate, without 2.55/3.0's extra death frame)
    bool q30_dying_frame;           // upstream's weapon_dying_frame (qr_WEAPONS_EXTRA_DEATH_FRAME)
    int q30_ref_o_tile;             // upstream's ref_o_tile: LOADGFX's tile (a sideways weapon's o_tile moves; beam shards draw from this)
    virtual void onhit(bool clipped);
    virtual void onhit(bool clipped, int special, int linkdir);
    void q30_kill_special();            // 2.55/3.0 upstream kill_weapon_special
    void q30_hit_pierce(bool old_hit);  // 2.55/3.0 upstream hit_pierce (2.53: onhit)
    void q30_pierced_hit();
    // override hit detection to check for invicibility, etc
    virtual bool hit(sprite *s);
    virtual bool hit(int tx,int ty,int tz,int txsz,int tysz,int tzsz);
    virtual void draw(BITMAP *dest);
    virtual void update_weapon_frame(int change, int orig);
};

void putweapon(BITMAP *dest,int x,int y,int weapon_id, int type, int dir, int &aclk, int &aframe,
               int parentid);
#endif
/*** end of sprite.cc ***/


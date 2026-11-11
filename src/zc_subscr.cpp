//--------------------------------------------------------
//  Zelda Classic
//  by Jeremy Craner, 1999-2000
//
//  subscr.cc
//
//  Subscreen code for zelda.cc
//
//--------------------------------------------------------

#ifndef __GTHREAD_HIDE_WIN32API
#define __GTHREAD_HIDE_WIN32API 1
#endif                            //prevent indirectly including windows.h

#include "precompiled.h" //always first

#include <string.h>

#include "zelda.h"
#include "zc_sys.h"
#include "subscr.h"
#include "zc_subscr.h"
#include "link.h"
#include "gamedata.h"
#include "guys.h"
#include "port/game/q30_triggers.h"

extern LinkClass   Link;
extern int directItem;
extern int directItemA;
extern int directItemB;

//DIALOG *sso_properties_dlg;

void put_active_subscr(miscQdata *misc, int y, int pos)
{
    //Don't call Sitems.animate() - that gets called somewhere else, somehow. -L
    animate_selectors();
    bool showtime = game->get_timevalid() && !game->get_cheat() && get_bit(quest_rules,qr_TIME);
    show_custom_subscreen(framebuf, misc, current_subscreen_active, 0, 6-y, showtime, pos);
}

extern bool q30_passive_on_top();

// Newer quests draw the passive panel last, so an active subscreen that runs 8 rows too tall
// can't paint over it.
static void put_both_subscrs(miscQdata *misc, int y, bool showtime, int pos)
{
    if(q30_passive_on_top())
    {
        put_active_subscr(misc,y,pos);
        put_passive_subscr(framebuf,misc,0,176-2-y,showtime,pos);
        return;
    }

    //throw the passive subscreen onto the screen
    put_passive_subscr(framebuf,misc,0,176-2-y,showtime,pos);
    //put the active subscreen above the passive subscreen
    put_active_subscr(misc,y,pos);
}

// A button giving up an item to the one being set takes over whatever that one held.
static void q30_trade_extra_buttons(int item_here, int old_wpn, int old_pos, int old_direct)
{
    if(q30_use_x_button() && Xwpn == item_here)
        q30_set_button_slot(true, old_wpn, old_pos, old_direct);
    else if(q30_use_y_button() && Ywpn == item_here)
        q30_set_button_slot(false, old_wpn, old_pos, old_direct);
}

// Puts the item under the cursor on X or Y. Pressing it again takes it off, if the quest allows.
static void q30_equip_extra_button(bool x)
{
    enum { QR_NO_BUTTON_VERIFY = 424 };
    const int item_here = Bweapon(Bpos);
    const int direct_here = directItem;

    if(item_here <= 0)
        return;

    const int old_wpn = x ? Xwpn : Ywpn;
    const int old_pos = x ? q30_xpos : q30_ypos;
    const int old_direct = x ? directItemX : directItemY;

    if(old_wpn == item_here)
    {
        if(q30_quest_rule(QR_NO_BUTTON_VERIFY))
        {
            q30_set_button_slot(x, 0, 255, -1);
            sfx(WAV_PLACE);
        }

        return;
    }

    if(Bwpn == item_here)
    {
        Bwpn = old_wpn;
        game->bwpn = old_pos;
        directItemB = old_direct;
    }
    else if(get_bit(quest_rules,qr_SELECTAWPN) && Awpn == item_here)
    {
        Awpn = old_wpn;
        game->awpn = old_pos;
        directItemA = old_direct;
    }
    else if((x ? Ywpn : Xwpn) == item_here)
    {
        q30_set_button_slot(!x, old_wpn, old_pos, old_direct);
    }

    q30_set_button_slot(x, item_here, Bpos, direct_here);
    sfx(WAV_PLACE);
}

void dosubscr(miscQdata *misc)
{
    PALETTE temppal;
    
    if(tmpscr->flags3&fNOSUBSCR)
    {
        return;
    }
    
    if(usebombpal)
    {
        memcpy(temppal, RAMpal, PAL_SIZE*sizeof(RGB));
        memcpy(RAMpal, tempbombpal, PAL_SIZE*sizeof(RGB));
        refreshpal=true;
    }
    
    int miny;
    bool showtime = game->get_timevalid() && !game->get_cheat() && get_bit(quest_rules,qr_TIME);
    load_Sitems(misc);
    
    pause_sfx(WAV_BRANG);
    
    if(current_item_id(itype_brang)>=0)
        pause_sfx(itemsbuf[current_item_id(itype_brang)].usesound);
        
    if(current_item_id(itype_hookshot)>=0)
        pause_sfx(itemsbuf[current_item_id(itype_hookshot)].usesound);
        
    adjust_sfx(WAV_ER,128,false);
    adjust_sfx(WAV_MSG,128,false);
    
    set_clip_rect(scrollbuf, 0, 0, scrollbuf->w, scrollbuf->h);
    set_clip_rect(framebuf, 0, 0, framebuf->w, framebuf->h);
    
    //make a copy of the blank playing field on the right side of scrollbuf
    blit(scrollbuf,scrollbuf,0,playing_field_offset,256,0,256,176);
    //make a copy of the complete playing field on the bottom of scrollbuf
    blit(framebuf,scrollbuf,0,playing_field_offset,0,176,256,176);
    miny = 6;
    
    //Set the selector to the correct position before bringing up the subscreen -DD
    if(get_bit(quest_rules,qr_SELECTAWPN))
    {
        if(Bwpn==0 && Awpn!=0)
            Bpos = zc_max(game->awpn,0);
        else
            Bpos = zc_max(game->bwpn,0);
    }
    else
        Bpos = zc_max(game->bwpn,0);

    q30_subscreen_cursor(true, Bpos);   // 2.55/3.0: a page other than the first keeps its cursor

    for(int y=176-2; y>=6; y-=3)
    {
        do_dcounters();
        Link.refill();
        //fill in the screen with black to prevent the hall of mirrors effect
        rectfill(framebuf, 0, 0, 255, 223, 0);
        
        if(COOLSCROLL)
        {
            //copy the playing field back onto the screen
            blit(scrollbuf,framebuf,0,176,0,passive_subscreen_height,256,176);
        }
        else
        {
            //scroll the playing field (copy the copy we made)
            blit(scrollbuf,framebuf,256,0,0,176-2-y+passive_subscreen_height,256,y);
        }
        
        put_both_subscrs(misc,y,showtime,sspSCROLLING);
        advanceframe(false);
        
        if(Quit)
            return;
    }
    
    bool done=false;
    int q30_held = 0xFF;    // buttons held as the subscreen opens don't count as pressed

    do
    {
        load_control_state();
        int pos = Bpos;

        // 2.55 and 3.0 quests: the buttons pressed this frame (not held from before), for page
        // changes, seen before anything below takes them (upstream's getIntBtnInput); and with
        // the newer subscreens L and R are page buttons, not selector moves (upstream moves with
        // them only with qr_OLD_SUBSCR).
        const bool q30_lr = q30_subscreen_lr_are_buttons();
        const int q30_now = !q30_active() ? 0 :
            (cAbtn() ? 0x01 : 0) | (cBbtn() ? 0x02 : 0) | (cLbtn() ? 0x04 : 0) | (cRbtn() ? 0x08 : 0) |
            (cEx1btn() ? 0x10 : 0) | (cEx2btn() ? 0x20 : 0) | (cEx3btn() ? 0x40 : 0) | (cEx4btn() ? 0x80 : 0);
        const int q30_btns = q30_now & ~q30_held;
        q30_held = q30_now;

        // (2.55/3.0 with qr_FREEFORM_SUBSCREEN_CURSOR: the cursor stops on any selectable widget)
        const bool q30_free = q30_active() && q30_quest_rule(416);
        if(rUp())         Bpos = selectWpn_new(SEL_UP, pos, -1, q30_free);
        else if(rDown())  Bpos = selectWpn_new(SEL_DOWN, pos, -1, q30_free);
        else if(rLeft())  Bpos = selectWpn_new(SEL_LEFT, pos, -1, q30_free);
        else if(rRight()) Bpos = selectWpn_new(SEL_RIGHT, pos, -1, q30_free);
        else if(!q30_lr && rLbtn())  Bpos = selectWpn_new(SEL_LEFT, pos);
        else if(!q30_lr && rRbtn())  Bpos = selectWpn_new(SEL_RIGHT, pos);

        const bool q30_x = q30_use_x_button(), q30_y = q30_use_y_button();

        // 2.55/3.0, as upstream: a press where the cursor has no item (a page arrow) equips nothing
        const bool q30_no_item = q30_active() && !Bweapon(Bpos);

        if(get_bit(quest_rules,qr_SELECTAWPN) || q30_x || q30_y)
        {
            if(rBbtn() && !q30_no_item)
            {
                q30_trade_extra_buttons(Bweapon(Bpos), Bwpn, game->bwpn, directItemB);

                if(get_bit(quest_rules,qr_SELECTAWPN) && Awpn == Bweapon(Bpos))
                {
                    Awpn = Bwpn;
                    game->awpn = game->bwpn;
                    directItemA = directItemB;
                }

                Bwpn = Bweapon(Bpos);
                sfx(WAV_PLACE);
                
                game->bwpn = Bpos;
                directItemB = directItem;
            }
            else if(q30_x && rEx1btn())
            {
                q30_equip_extra_button(true);
            }
            else if(q30_y && rEx2btn())
            {
                q30_equip_extra_button(false);
            }
            else if(get_bit(quest_rules,qr_SELECTAWPN) && rAbtn() && !q30_no_item)
            {
                q30_trade_extra_buttons(Bweapon(Bpos), Awpn, game->awpn, directItemA);

                if(Bwpn == Bweapon(Bpos))
                {
                    Bwpn = Awpn;
                    game->bwpn = game->awpn;
                    directItemB = directItemA;
                }
                
                Awpn = Bweapon(Bpos);
                sfx(WAV_PLACE);
                game->awpn = Bpos;
                directItemA = directItem;
            }
        }
        else if(!q30_no_item)
        {
            // (2.55/3.0, as upstream: only an item under the cursor is equipped, so a page
            // without one, like a map page, leaves B as it was)
            Bwpn = Bweapon(Bpos);
            game->bwpn = Bpos;
            directItemB = directItem;
        }
        
        if(pos!=Bpos)
            sfx(WAV_CHIME);

        if(q30_btns)
            q30_subscreen_pages(q30_btns, Bpos);

        do_dcounters();
        Link.refill();

        //put_passive_subscr(framebuf,misc,0,174-miny,showtime,true);
        //blit(scrollbuf,framebuf,0,6,0,6-miny,256,168);
        //put_active_subscr(misc,miny,true);
        
        //fill in the screen with black to prevent the hall of mirrors effect
        rectfill(framebuf, 0, 0, 255, 223, 0);
        
        if(COOLSCROLL)
        {
            //copy the playing field back onto the screen
            blit(scrollbuf,framebuf,0,176,0,passive_subscreen_height,256,176);
        }
        else
        {
            //nothing to do; the playing field has scrolled off the screen
        }
        
        put_both_subscrs(misc,miny,showtime,sspDOWN);
        
        advanceframe(false);
        
        if(NESquit && Up() && cAbtn() && cBbtn())
        {
            Udown=true;
            Quit=qQUIT;
        }
        
        if(Quit)
            return;
            
        if(rSbtn())
            done=true;
    }
    while(!done);

    q30_subscreen_cursor(false, Bpos);

    for(int y=6; y<=174; y+=3)
    {
        do_dcounters();
        Link.refill();
        //fill in the screen with black to prevent the hall of mirrors effect
        rectfill(framebuf, 0, 0, 255, 223, 0);
        
        if(COOLSCROLL)
        {
            //copy the playing field back onto the screen
            blit(scrollbuf,framebuf,0,176,0,passive_subscreen_height,256,176);
        }
        else
        {
            //scroll the playing field (copy the copy we made)
            blit(scrollbuf,framebuf,256,0,0,176-2-y+passive_subscreen_height,256,y);
        }
        
        put_both_subscrs(misc,y,showtime,sspSCROLLING);
        advanceframe(false);
        
        if(Quit)
            return;
    }
    
    //  Sitems.clear();
    if(usebombpal)
    {
        memcpy(RAMpal, temppal, PAL_SIZE*sizeof(RGB));
    }
    
    resume_sfx(WAV_BRANG);
}

void markBmap(int dir, int sc)
{
    /*
      if((DMaps[get_currdmap()].type&dmfTYPE)==dmOVERW)
        return;
    */
    if(sc>=128)
    {
        return;
    }
    
    byte drow = DMaps[get_currdmap()].grid[sc>>4];
    byte mask = 1 << (7-((sc&15)-DMaps[get_currdmap()].xoff));
    int di = ((get_currdmap()-1)<<6) + ((sc>>4)<<3) + ((sc&15)-DMaps[get_currdmap()].xoff);
    int code = 0;
    
    
    switch((DMaps[get_currdmap()].type&dmfTYPE))
    {
    case dmDNGN:
    
        // check dmap
        if((drow&mask)==0)
            return;
            
        // calculate code
        for(int i=3; i>=0; i--)
        {
            code <<= 1;
            code += tmpscr->door[i]&1;
        }
        
        // mark the map
        game->bmaps[di] = code|128;
        break;
        
    case dmOVERW:
        break;
        
    default:
        game->bmaps[di] |= 128;
        
        if(dir>=0)
            game->bmaps[di] |= (1<<dir);
            
        break;
    }
}

void markBmap(int dir)
{
    markBmap(dir, get_currscr());
}

/*** end of subscr.cc ***/


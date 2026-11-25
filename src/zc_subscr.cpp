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
#include "port/game/q30_regions.h"
#include "script_drawing.h"
#include "maps.h"

extern LinkClass   Link;
extern word quest_header_zelda_version;
extern int q255_header_build;       // q255_basic.cpp
bool q30_quest_rule(int id);        // q30_triggers.cpp
bool q30_compat_rule(int id);       // q30_triggers.cpp
byte q30_chart_get(int di);         // q30_triggers.cpp: 3.0's map chart, dmap << 7 | screen
void q30_chart_set(int di, byte v);
extern int directItem;
extern int directItemA;
extern int directItemB;

//DIALOG *sso_properties_dlg;

bool q30_subscreen_anim_draw(BITMAP *dest, int dx, int dy);    // q30_scripts_subscr.h
bool q30_sub_none_active();                                       // q30_scripts_subscr.h
bool q30_run_subscreen_script();                                   // q30_scripts_subscr.h
// Game->ActiveSubscreenY (upstream's active_sub_yoff): the active subscreen's top, relative to the
// playing field, while it slides and while it is open; -224 while it is closed.
int q30_active_sub_yoff = -224;
void q30_subscreen_script_reset();
bool q30_sub_move_cursor(int dir, int &pos);                      // q30_scripts_subscr.h
bool q30_sub_move_legacy(int dir, int &pos);                      // q30_scripts_subscr.h
bool q30_subscreen_widget_script(int btns, int pos);              // q30_scripts_subscr.h
bool q30_subscreen_animating();                                    // q30_scripts_subscr.h
bool q30_map_subscreen_open();                                     // q30_scripts_subscr.h
extern word q30_init_subscr_speed;      // q255_init.cpp
extern dword q255_compat_rule_version;  // q255_core.cpp

// 2.55/3.0 (upstream's can_btn): while a page change animates, the open subscreen reads none of
// the cursor, equip, page, Start and Map buttons. Upstream's down states still follow the buttons
// at each frame's end, so a press made meanwhile is lost; with qr_BROKEN_INPUT_DOWN_STATE (414, or
// compat rules before 35) they don't, and a button still held counts once the change is over.
static bool q30_broken_down_state()
{
    return q30_quest_rule(414) || q255_compat_rule_version < 35;
}

static void q30_eat_subscreen_buttons()
{
    rUp(); rDown(); rLeft(); rRight();
    eat_buttons();
}

// The subscreen cursor's move from pos in direction dir: a 2.55/3.0 newer subscreen's as upstream
// moves it (legacy: L and R with qr_OLD_SUBSCR), else 2.53's (any_stop: 2.55/3.0's freeform cursor).
static int q30_move_cursor(int dir, int pos, bool any_stop, bool legacy)
{
    int p = pos;

    if(q30_active() && (legacy ? q30_sub_move_legacy(dir, p) : q30_sub_move_cursor(dir, p)))
        return p;

    return legacy ? selectWpn_new(dir, pos) : selectWpn_new(dir, pos, -1, any_stop);
}

int q30_subscr_speed = 1;   // see q30_triggers.h; init_game sets it from the quest's init data

// The play time is shown when it's valid and the quest asks for it; 2.53 also hides it in a
// game that cheated, 2.55/3.0 don't (upstream's should_show_time).
bool q30_should_show_time()
{
    return game->get_timevalid() && (quest_header_zelda_version >= 0x255 || !game->get_cheat()) && get_bit(quest_rules,qr_TIME);
}

// The game's start: the active subscreen's slide speed is the init data's (upstream's Hero init).
void q30_reset_subscr_speed()
{
    q30_subscr_speed = quest_header_zelda_version >= 0x255 ? vbound((int)q30_init_subscr_speed, 1, 85) : 1;
}

// How far the active subscreen slides: 168 rows, or 176 with the 232-row picture (upstream's
// draw_subscrs distance). It moves 3 * q30_subscr_speed rows a frame (3 for 2.53 quests).
static int subscr_distance()
{
    return zc_view_h == 232 ? 176 : 168;
}

// qr_ACTIVE_SUB_IGNORE_8PX (2.55/3.0): the quest's bit, or upstream's readrules turning it on for
// quests saved before it: 3.0 ones before compat version 96, and 2.55 ones (whose branch couldn't
// raise the compat version) before 2.55.12
static bool q30_active_sub_ignore_8px()
{
    enum { QR_ACTIVE_SUB_IGNORE_8PX = 476 };
    extern int q255_header_version[3];
    extern dword q255_compat_rule_version;
    const int *v = q255_header_version;

    if(q30_quest_rule(QR_ACTIVE_SUB_IGNORE_8PX))
        return true;

    if(v[0] >= 3)
        return q255_compat_rule_version < 96;

    return v[0] < 2 || (v[0] == 2 && (v[1] < 55 || (v[1] == 55 && v[2] < 12)));
}

// How many rows a 2.55/3.0 subscreen's "clear" widget fills (upstream's SW_Clear::getH): the
// active (and map) subscreen's height, 56 for the passive one, the whole picture for an overlay.
int q30_sub_clear_height(int ss_type)
{
    switch(ss_type)
    {
    case 0: // sstACTIVE
    case 3: // sstMAP
        return 168 + ((q30_active_sub_ignore_8px() || !q30_show_bottom_8px()) ? 0 : 8);

    case 2: // sstOVERLAY
        return q30_show_bottom_8px() ? 232 : 224;

    default:
        return 56;
    }
}

// Draws the active subscreen with its top at ay.
static void put_active_at(miscQdata *misc, int ay, int pos)
{
    //Don't call Sitems.animate() - that gets called somewhere else, somehow. -L
    animate_selectors();

    // 2.55/3.0 with the 232-row picture and qr_ACTIVE_SUB_IGNORE_8PX: the active subscreen is
    // 176 rows apart from the passive one but only 168 tall, so upstream draws 8 black rows over
    // the playing field and the subscreen 8 rows lower
    if(zc_view_h == 232 && q30_active_sub_ignore_8px())
    {
        rectfill(framebuf, 0, ay, framebuf->w, ay+8-1, BLACK);
        ay += 8;
    }

    // 2.55/3.0: a page change's animation, drawn in the subscreen's place while it lasts
    if(q30_subscreen_anim_draw(framebuf, 0, ay))
        return;

    show_custom_subscreen(framebuf, misc, current_subscreen_active, 0, ay, q30_should_show_time(), pos);
}

void put_active_subscr(miscQdata *misc, int y, int pos)
{
    put_active_at(misc, 6-y, pos);
}

extern bool q30_passive_on_top();

// The active subscreen with its top at ay (upstream's y: -distance closed, 0 open), the passive
// one subscr_distance() rows below it. Newer quests draw the passive panel last, so an active
// subscreen that runs 8 rows too tall can't paint over it.
static void put_both_subscrs(miscQdata *misc, int ay, bool showtime, int pos)
{
    const int py = ay+subscr_distance();

    if(q30_passive_on_top())
    {
        put_active_at(misc,ay,pos);
        put_passive_subscr(framebuf,misc,0,py,showtime,pos);
        return;
    }

    //throw the passive subscreen onto the screen
    put_passive_subscr(framebuf,misc,0,py,showtime,pos);
    //put the active subscreen above the passive subscreen
    put_active_at(misc,ay,pos);
}

// A button giving up an item to the one being set (button from) takes over whatever that one
// held (with the newer subscreens, its slot by page too).
static void q30_trade_extra_buttons(int item_here, int old_wpn, int old_pos, int old_direct, int from)
{
    if(q30_use_x_button() && Xwpn == item_here)
        q30_set_button_slot(true, old_wpn, old_pos, old_direct, q30_btn[from]);
    else if(q30_use_y_button() && Ywpn == item_here)
        q30_set_button_slot(false, old_wpn, old_pos, old_direct, q30_btn[from]);
}

// With the newer subscreens, a slot that can't be equipped (upstream's SUBSCR_CURITM_NONEQP)
// equips nothing; under qr_NO_BUTTON_VERIFY, pressing A or B on the item it already holds takes
// it off, unless the slot has SUBSCR_CURITM_NO_UNEQUIP (upstream's can_unequip; q30_scripts_subscr.h).
dword q30_btn_slot_flags_at(int pos);

static bool q30_cant_equip_here()
{
    return q30_paged_buttons() && (q30_btn_slot_flags_at(Bpos) & 0x2) != 0;
}

static bool q30_can_unequip_here()
{
    enum { QR_NO_BUTTON_VERIFY = 424 };
    return q30_paged_buttons() && q30_quest_rule(QR_NO_BUTTON_VERIFY) && !(q30_btn_slot_flags_at(Bpos) & 0x40);
}

// Puts the item under the cursor on X or Y. Pressing it again takes it off, if the quest allows.
static void q30_equip_extra_button(bool x)
{
    enum { QR_NO_BUTTON_VERIFY = 424 };
    const int item_here = Bweapon(Bpos);
    const int direct_here = directItem;

    if(item_here <= 0 || q30_cant_equip_here())
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

    const int self = x ? Q30_BTN_X : Q30_BTN_Y;    // (its slot by page, newer subscreens)

    if(Bwpn == item_here)
    {
        Bwpn = old_wpn;
        game->bwpn = old_pos;
        directItemB = old_direct;
        q30_btn_copy(Q30_BTN_B, self);
    }
    else if(get_bit(quest_rules,qr_SELECTAWPN) && Awpn == item_here)
    {
        Awpn = old_wpn;
        game->awpn = old_pos;
        directItemA = old_direct;
        q30_btn_copy(Q30_BTN_A, self);
    }
    else if((x ? Ywpn : Xwpn) == item_here)
    {
        q30_set_button_slot(!x, old_wpn, old_pos, old_direct, q30_btn[self]);
    }

    q30_set_button_slot(x, item_here, Bpos, direct_here);
    sfx(WAV_PLACE);
}

void q30_bunny_check(bool on);

// 2.55/3.0: the subscreen chooses among everything Link has, bunny or not (upstream's subscreen
// looks items up without the bunny check; using them is still checked)
struct Q30DosubscrBunnyOff
{
    Q30DosubscrBunnyOff() { q30_bunny_check(false); }
    ~Q30DosubscrBunnyOff() { q30_bunny_check(true); }
};

// How many rows of the playing field still show under the sliding subscreen whose top is at ay:
// upstream's for newer quests (8 more with the 232-row picture), 2.53's (6 too many, cut off by
// the screen's bottom) otherwise.
static int subscr_field_rows(int ay)
{
    if(quest_header_zelda_version >= 0x255)
        return -ay+(zc_view_h==232 ? 8 : 0);

    return 6-ay;
}

// 2.55/3.0 (upstream's dosubscr(bool)): the open subscreen clears the script drawings every frame,
// so the drawings the interrupted frame had queued are put aside and given back as it closes; what
// the subscreen's own script left queued is dropped. Nothing for 2.53 quests.
struct Q30SubscrStashDraws
{
    bool on;
    Q30SubscrStashDraws() : on(q30_active())
    {
        if(on)
            stash().q30_swap_commands(script_drawing_commands);
    }
    ~Q30SubscrStashDraws()
    {
        if(on)
        {
            script_drawing_commands.Clear();
            stash().q30_swap_commands(script_drawing_commands);
            stash().Clear();
        }
    }
    static CScriptDrawingCommands &stash()
    {
        static CScriptDrawingCommands s;
        return s;
    }
};

void dosubscr(miscQdata *misc)
{
    Q30DosubscrBunnyOff q30_bunny_off;
    Q30SubscrStashDraws q30_stash_draws;
    PALETTE temppal;
    
    if(tmpscr->flags3&fNOSUBSCR)
    {
        return;
    }

    // 2.55/3.0: a quest with no active subscreen opens none (q30_scripts_subscr.h)
    if(q30_active() && q30_sub_none_active())
        return;
    
    if(usebombpal)
    {
        memcpy(temppal, RAMpal, PAL_SIZE*sizeof(RGB));
        memcpy(RAMpal, tempbombpal, PAL_SIZE*sizeof(RGB));
        refreshpal=true;
    }
    
    bool showtime = q30_should_show_time();
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

    // 2.55/3.0 extended height mode (upstream's dosubscr): the playing field fills the picture, so
    // all of it (240 rows from the field's top) slides down from the top, and stays where the
    // slide left it while the subscreen is open
    const bool ext_view = q30_extended_height_mode();
    static BITMAP *ext_field = NULL;

    if(ext_view)
    {
        if(!ext_field)
            ext_field = create_bitmap_ex(8,256,240);

        clear_bitmap(ext_field);
        blit(framebuf,ext_field,0,playing_field_offset,0,0,256,240);
    }

    // The slide, as upstream counts it: the active subscreen's top goes from -distance (closed)
    // to 0 (open), 3 * q30_subscr_speed rows a frame. For 2.53 quests that's 168 rows 3 at a
    // time, frame for frame what this loop always did (it counted y = 6 - ay down from 174).
    const int distance = subscr_distance();
    const int step = 3*q30_subscr_speed;
    int ay;

    //Set the selector to the correct position before bringing up the subscreen -DD
    if(q30_old_subscreens())
    {
        // 2.55/3.0 with the older subscreens (upstream's dosubscr under qr_OLD_SUBSCR): on B's
        // slot if B has one, else on A's, X's or Y's, else on slot 0 (2.53 goes to A's whenever
        // B holds no item, and to 255 when neither has a slot)
        if(game->bwpn != 255)
            Bpos = game->bwpn;
        else if(game->awpn != 255)
            Bpos = game->awpn;
        else if(q30_use_x_button() && q30_xpos != 255)
            Bpos = q30_xpos;
        else if(q30_use_y_button() && q30_ypos != 255)
            Bpos = q30_ypos;
        else
            Bpos = 0;
    }
    else if(get_bit(quest_rules,qr_SELECTAWPN))
    {
        if(Bwpn==0 && Awpn!=0)
            Bpos = zc_max(game->awpn,0);
        else
            Bpos = zc_max(game->bwpn,0);
    }
    else
        Bpos = zc_max(game->bwpn,0);

    q30_subscreen_cursor(true, Bpos);   // 2.55/3.0: a page keeps its cursor (with the newer subscreens the first too)

    // 2.55/3.0: the subscreen's own script runs once a frame while it is open (upstream's
    // run_active_subscript; Waitdraw is invalid there, so its second call never runs it); it
    // can close the subscreen
    bool q30_exit = false;
    q30_subscreen_script_reset();

    // 2.55/3.0 (upstream's dosubscr): the input is read as each slide frame starts, and while the
    // subscreen is open right after each frame's advance, where the Start check looks at it, so the
    // subscreen starts closing on the frame Start is pressed. 2.53 reads it at the top of the next
    // pass and stays open a frame longer (the game, its combo animation too, resumes a frame later).
    const bool q30_input = q30_active();

    for(ay=-distance; ay<=0 && !q30_exit; ay+=step)
    {
        if(q30_input)
            load_control_state();
        if(q30_active())
            script_drawing_commands.Clear();    // (2.55/3.0: a frame's script drawings, as upstream)
        q30_active_sub_yoff = ay-playing_field_offset;
        q30_exit = q30_run_subscreen_script();
        do_dcounters();
        Link.refill();
        //fill in the screen with black to prevent the hall of mirrors effect
        rectfill(framebuf, 0, 0, 255, zc_view_h-1, 0);

        if(ext_view)
            blit(ext_field,framebuf,0,0,0,COOLSCROLL ? 0 : ay+distance,256,240);
        else if(COOLSCROLL)
        {
            //copy the playing field back onto the screen
            blit(scrollbuf,framebuf,0,176,0,passive_subscreen_height,256,176);
        }
        else
        {
            //scroll the playing field (copy the copy we made)
            blit(scrollbuf,framebuf,256,0,0,ay+distance+passive_subscreen_height,256,subscr_field_rows(ay));
        }

        put_both_subscrs(misc,ay,showtime,sspSCROLLING);
        q30_draw_subscreen_scripts(framebuf);
        advanceframe(false);
        
        if(Quit)
            return;
    }
    
    q30_active_sub_yoff = -playing_field_offset;
    bool done=false;
    int q30_held = 0xFF;    // buttons held as the subscreen opens don't count as pressed

    if(q30_input)
        load_control_state();   // (upstream: what came in during the opening slide is consumed)

    do
    {
        if(!q30_input)
            load_control_state();
        if(q30_active())
            script_drawing_commands.Clear();
        if(!q30_exit && (q30_exit = q30_run_subscreen_script()))
            break;
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

        // (2.55/3.0: none of the buttons below while a page change animates, see q30_broken_down_state)
        const bool q30_can_btn = !(q30_active() && q30_subscreen_animating());

        if(q30_can_btn || !q30_broken_down_state())
            q30_held = q30_now;

        if(!q30_can_btn && !q30_broken_down_state())
            q30_eat_subscreen_buttons();

        if(q30_can_btn)     // (the block below is not indented again, to keep the change small)
        {
        // (2.55/3.0 with qr_FREEFORM_SUBSCREEN_CURSOR: the cursor stops on any selectable widget;
        // the newer subscreens move it as upstream's SubscrPage does, q30_sub_move_cursor)
        const bool q30_free = q30_active() && q30_quest_rule(416);
        if(rUp())         Bpos = q30_move_cursor(SEL_UP, pos, q30_free, false);
        else if(rDown())  Bpos = q30_move_cursor(SEL_DOWN, pos, q30_free, false);
        else if(rLeft())  Bpos = q30_move_cursor(SEL_LEFT, pos, q30_free, false);
        else if(rRight()) Bpos = q30_move_cursor(SEL_RIGHT, pos, q30_free, false);
        else if(!q30_lr && rLbtn())  Bpos = q30_move_cursor(SEL_LEFT, pos, false, true);
        else if(!q30_lr && rRbtn())  Bpos = q30_move_cursor(SEL_RIGHT, pos, false, true);

        const bool q30_x = q30_use_x_button(), q30_y = q30_use_y_button();

        // 2.55/3.0 newer subscreens: the selected widget's generic script, before anything is
        // equipped; when it changed the subscreen shown, the rest of the press is dropped (upstream)
        const bool q30_press_ok = q30_subscreen_widget_script(q30_btns, Bpos);

        // 2.55/3.0, as upstream: a press where the cursor has no item (a page arrow) equips nothing,
        // nor (newer subscreens) one on a slot that can't be equipped
        const bool q30_no_item = q30_active() && (!Bweapon(Bpos) || q30_cant_equip_here());

        if(!q30_press_ok)
        {
        }
        else if(get_bit(quest_rules,qr_SELECTAWPN) || q30_x || q30_y)
        {
            if(rBbtn() && !q30_no_item)
            {
                if(q30_can_unequip_here() && Bwpn == Bweapon(Bpos))
                {
                    // (newer subscreens, qr_NO_BUTTON_VERIFY: B gives its item up and stays empty)
                    Bwpn = 0;
                    directItemB = -1;
                    q30_btn_here(Q30_BTN_B, 255);
                    sfx(WAV_PLACE);
                }
                else
                {
                    q30_trade_extra_buttons(Bweapon(Bpos), Bwpn, game->bwpn, directItemB, Q30_BTN_B);

                    if(get_bit(quest_rules,qr_SELECTAWPN) && Awpn == Bweapon(Bpos))
                    {
                        Awpn = Bwpn;
                        game->awpn = game->bwpn;
                        directItemA = directItemB;
                        q30_btn_copy(Q30_BTN_A, Q30_BTN_B);     // (newer subscreens: the slots by page)
                    }

                    Bwpn = Bweapon(Bpos);
                    sfx(WAV_PLACE);

                    game->bwpn = Bpos;
                    directItemB = directItem;
                    q30_btn_here(Q30_BTN_B, Bpos);
                }
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
                if(q30_can_unequip_here() && Awpn == Bweapon(Bpos))
                {
                    // (newer subscreens, qr_NO_BUTTON_VERIFY: A gives its item up and stays empty)
                    Awpn = 0;
                    directItemA = -1;
                    q30_btn_here(Q30_BTN_A, 255);
                    sfx(WAV_PLACE);
                }
                else
                {
                    q30_trade_extra_buttons(Bweapon(Bpos), Awpn, game->awpn, directItemA, Q30_BTN_A);

                    if(Bwpn == Bweapon(Bpos))
                    {
                        Bwpn = Awpn;
                        game->bwpn = game->awpn;
                        directItemB = directItemA;
                        q30_btn_copy(Q30_BTN_B, Q30_BTN_A);     // (newer subscreens: the slots by page)
                    }

                    Awpn = Bweapon(Bpos);
                    sfx(WAV_PLACE);
                    game->awpn = Bpos;
                    directItemA = directItem;
                    q30_btn_here(Q30_BTN_A, Bpos);
                }
            }
        }
        else if(!q30_no_item)
        {
            // (2.55/3.0, as upstream: only an item under the cursor is equipped, so a page
            // without one, like a map page, leaves B as it was)
            Bwpn = Bweapon(Bpos);
            game->bwpn = Bpos;
            directItemB = directItem;
            q30_btn_here(Q30_BTN_B, Bpos);
        }
        
        if(pos!=Bpos)
            sfx(WAV_CHIME);

        if(q30_btns && q30_press_ok)
            q30_subscreen_pages(q30_btns, Bpos);
        }

        do_dcounters();
        Link.refill();

        //put_passive_subscr(framebuf,misc,0,174-miny,showtime,true);
        //blit(scrollbuf,framebuf,0,6,0,6-miny,256,168);
        //put_active_subscr(misc,miny,true);
        
        //fill in the screen with black to prevent the hall of mirrors effect
        rectfill(framebuf, 0, 0, 255, zc_view_h-1, 0);

        if(ext_view)
            blit(ext_field,framebuf,0,0,0,COOLSCROLL ? 0 : distance,256,240);
        else if(COOLSCROLL)
        {
            //copy the playing field back onto the screen
            blit(scrollbuf,framebuf,0,176,0,passive_subscreen_height,256,176);
        }
        else
        {
            //nothing to do; the playing field has scrolled off the screen
        }

        put_both_subscrs(misc,0,showtime,sspDOWN);
        q30_draw_subscreen_scripts(framebuf);
        
        advanceframe(false);
        
        // (2.55/3.0: the next frame's input, for the checks below and the next pass)
        if(q30_input)
            load_control_state();
        
        if(q30_can_btn && NESquit && Up() && cAbtn() && cBbtn())
        {
            Udown=true;
            Quit=qQUIT;
        }
        
        if(Quit)
            return;
            
        // (2.55/3.0, upstream: the Map button closes the map subscreen, and with
        // qr_MAP_BUTTON_CLOSES_SUBSCREEN (474) the active one too)
        if(q30_can_btn && (rSbtn() || (q30_active() && (q30_map_subscreen_open() || q30_quest_rule(474)) && rPbtn())))
            done=true;
    }
    while(!done && !q30_exit);

    q30_subscreen_cursor(false, Bpos);

    // (upstream starts closing from where the opening stopped, at most 0)
    for(ay=zc_min(ay,0); ay>=-distance; ay-=step)
    {
        if(q30_input)
            load_control_state();
        if(q30_active())
            script_drawing_commands.Clear();
        q30_active_sub_yoff = ay-playing_field_offset;
        if(!q30_exit)
            q30_exit = q30_run_subscreen_script();
        do_dcounters();
        Link.refill();
        //fill in the screen with black to prevent the hall of mirrors effect
        rectfill(framebuf, 0, 0, 255, zc_view_h-1, 0);

        if(ext_view)
            blit(ext_field,framebuf,0,0,0,COOLSCROLL ? 0 : ay+distance,256,240);
        else if(COOLSCROLL)
        {
            //copy the playing field back onto the screen
            blit(scrollbuf,framebuf,0,176,0,passive_subscreen_height,256,176);
        }
        else
        {
            //scroll the playing field (copy the copy we made)
            blit(scrollbuf,framebuf,256,0,0,ay+distance+passive_subscreen_height,256,subscr_field_rows(ay));
        }

        put_both_subscrs(misc,ay,showtime,sspSCROLLING);
        q30_draw_subscreen_scripts(framebuf);
        advanceframe(false);
        
        if(Quit)
            return;
    }
    
    q30_active_sub_yoff = -224;
    //  Sitems.clear();
    if(usebombpal)
    {
        memcpy(RAMpal, temppal, PAL_SIZE*sizeof(RGB));
    }
    
    resume_sfx(WAV_BRANG);
}

// 2.55/3.0: qr_DUNGEONS_USE_CLASSIC_CHARTING (213) and qr_NO_OVERWORLD_MAP_CHARTING (212), which
// upstream's readrules also turns on for quests saved before 2.55 build 57 and 56
bool q30_classic_dungeon_charting()
{
    return q30_quest_rule(213) || quest_header_zelda_version < 0x255
           || (quest_header_zelda_version == 0x255 && q255_header_build < 57);
}

bool q30_no_overworld_charting()
{
    return q30_quest_rule(212) || quest_header_zelda_version < 0x255
           || (quest_header_zelda_version == 0x255 && q255_header_build < 56);
}

// 2.55/3.0: upstream 3.0's markBmap. The chart is kept per DMap as dmap << 7 | screen
// (q30_chart_*), overworld screens included, where 2.53 keeps 8 x 8 a DMap from DMap 1.
static void q30_markBmap(int dir, int sc, int dmap)
{
    if(dmap < 0)
        dmap = get_currdmap();

    if(sc >= 128 || sc < 0)
        return;

    const int col = (sc&15) - DMaps[dmap].xoff;
    const byte drow = DMaps[dmap].grid[sc>>4];
    const byte mask = unsigned(col) > 7 ? 0 : (byte)(1 << (7-col));   // (upstream's shift: 0 outside)
    const int di = (dmap << 7) + (sc & 0x7F);
    int code = 0;

    switch(DMaps[dmap].type&dmfTYPE)
    {
    case dmDNGN:
        if(q30_classic_dungeon_charting())
        {
            if((drow&mask)==0)      // only squares marked in the DMap editor can be charted
                return;

            for(int i=3; i>=0; i--)
            {
                code <<= 1;
                code += tmpscr->door[i]&1;
            }

            q30_chart_set(di, (byte)(code|128));
            return;
        }

        break;

    case dmOVERW:
        if(q30_no_overworld_charting())
            return;

        break;
    }

    byte v = q30_chart_get(di) | 128;

    if(dir >= 0)
        v |= (byte)(1<<dir);

    q30_chart_set(di, v);
}

void markBmap(int dir, int sc)
{
    /*
      if((DMaps[get_currdmap()].type&dmfTYPE)==dmOVERW)
        return;
    */
    if(q30_active())
    {
        q30_markBmap(dir, sc, -1);
        return;
    }

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

// 2.55/3.0 only: upstream's markBmap(dir, screen, dmap), as loadscr charts the screen it loaded
// with the DMap it loads into (a scrolling warp's, before currdmap changes)
void q30_markBmap_dmap(int dir, int sc, int dmap)
{
    if(q30_active())
        q30_markBmap(dir, sc, dmap);
}

// 2.55/3.0 only: upstream's mark_visited (a scrolling region's screens: all of them when the region
// loads, or each one Link steps on, by the init data's region mapping)
void q30_mark_visited(int sc)
{
    if(!q30_active() || sc < 0 || sc >= 0x80)
        return;

    if(!q30_compat_rule(475) || (DMaps[currdmap].flags&dmfVIEWMAP))   // qr_ONLY_MARK_SCREENS_VISITED_IF_MAP_VIEWABLE
        game->maps[(currmap*MAPSCRSNORMAL)+sc] |= mVISITED;

    q30_markBmap(-1, sc, -1);
}

/*** end of subscr.cc ***/


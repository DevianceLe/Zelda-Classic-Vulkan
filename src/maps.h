//--------------------------------------------------------
//  Zelda Classic
//  by Jeremy Craner, 1999-2000
//
//  maps.cc
//
//  Map and screen scrolling stuff for zelda.cc
//
//--------------------------------------------------------

#ifndef _MAPS_H_
#define _MAPS_H_
#include "zdefs.h"

#define DRIEDLAKE ((tmpscr->flags7 & fWHISTLEWATER) && (whistleclk>=88))
#define COMBOPOS(x,y) (((y)&0xF0)+((x)>>4))
void debugging_box(int x1, int y1, int x2, int y2);
void clear_dmap(word i);
void clear_dmaps();
int count_dmaps();
int isdungeon(int dmap = -1, int scr = -1);
int MAPCOMBO(int x,int y);
int MAPFFCOMBO(int x,int y);
int MAPCSET(int x,int y);
int MAPFLAG(int x,int y);
int MAPCOMBOFLAG(int x,int y);
int MAPFFCOMBOFLAG(int x,int y);
int COMBOTYPE(int x,int y);
int FFCOMBOTYPE(int x, int y);
int MAPCOMBO2(int layer,int x,int y);
int MAPCSET2(int layer,int x,int y);
int MAPFLAG2(int layer,int x,int y);
int MAPCOMBOFLAG2(int layer,int x,int y);
int COMBOTYPE2(int layer,int x,int y);
int getFFCAt(int x, int y);
void eventlog_mapflags();
void setmapflag(int mi2, int flag);
void setmapflag(int flag = 32); // 32 = mBELOW
void unsetmapflag(int mi2, int flag, bool anyflag=false);
void unsetmapflag(int flag = 32);
bool getmapflag(int flag = 32); // 32 = mBELOW
int WARPCODE(int dmap,int scr,int dw);
void update_combo_cycling();
bool iswater(int combo);
bool iswater_type(int type);
bool isGrassType(int type);
bool isFlowersType(int type);
bool isBushType(int type);
bool isSlashType(int type);
bool isCuttableNextType(int type);
bool isTouchyType(int type);
bool isCuttableType(int type);
bool isCuttableItemType(int type);
bool isstepable(int combo);                                 //can use ladder on it
bool ishookshottable(int bx, int by);
bool hiddenstair(int tmp, bool redraw);                      // tmp = index of tmpscr[]
bool remove_lockblocks(int tmp);                // tmp = index of tmpscr[]
bool remove_bosslockblocks(int tmp);            // tmp = index of tmpscr[]
bool remove_chests(int tmp);                    // tmp = index of tmpscr[]
bool remove_lockedchests(int tmp);              // tmp = index of tmpscr[]
bool remove_bosschests(int tmp);                // tmp = index of tmpscr[]
bool overheadcombos(mapscr *s);
void delete_fireball_shooter(mapscr *s, int i);
void hidden_entrance(int tmp,bool refresh, bool high16only=false,int single=-1);
void update_freeform_combos();
bool findentrance(int x, int y, int flag, bool setflag);
bool hitcombo(int x, int y, int combotype);
bool hitflag(int x, int y, int flagtype);
int nextscr(int dir);
void bombdoor(int x,int y);
void do_scrolling_layer(BITMAP *bmp, int type, mapscr* layer, int x, int y, bool scrolling, int tempscreen);
void do_layer(BITMAP *bmp, int type, mapscr* layer, int x, int y, int tempscreen, bool scrolling = false, bool drawprimitives=false);
void put_walkflags(BITMAP *dest,int x,int y,int xofs,int yofs, word cmbdat,int lyr);
void do_walkflags(BITMAP *dest,mapscr* layer,int x, int y, int tempscreen);
void do_primitives(BITMAP *bmp, int type, mapscr *layer, int x, int y);

// Script drawing layers 2.55/3.0 quests have and 2.53 hasn't: the special sprite layers
// (upstream SPLAYER_*, the values ZScript passes) and the negative layers -7..-1.
enum
{
    Q30_SPLAYER_PLAYER_DRAW = -200000, Q30_SPLAYER_EWEAP_BEHIND_DRAW, Q30_SPLAYER_EWEAP_FRONT_DRAW,
    Q30_SPLAYER_LWEAP_BEHIND_DRAW, Q30_SPLAYER_LWEAP_FRONT_DRAW, Q30_SPLAYER_LWEAP_ABOVE_DRAW,
    Q30_SPLAYER_CHAINLINK_DRAW, Q30_SPLAYER_NPC_DRAW, Q30_SPLAYER_NPC_ABOVEPLAYER_DRAW,
    Q30_SPLAYER_NPC_AIRBORNE_DRAW, Q30_SPLAYER_ITEMSPRITE_DRAW, Q30_SPLAYER_FAIRYITEM_DRAW,
    Q30_SPLAYER_PUSHBLOCK, Q30_SPLAYER_MOVINGBLOCK, Q30_SPLAYER_OVERHEAD_CMB, Q30_SPLAYER_OVERHEAD_FFC,
    Q30_SPLAYER_DARKROOM_UNDER, Q30_SPLAYER_DARKROOM_OVER, Q30_SPLAYER_FFC_DRAW,
    Q30_SPLAYER_LENS_UNDER_1, Q30_SPLAYER_LENS_UNDER_2, Q30_SPLAYER_LENS_OVER,
    Q30_SPLAYER_UNDER_PLAYER_DRAW, Q30_SPLAYER_OVER_LIFT_WEAPON, Q30_SPLAYER_OVER_PROMPT_COMBO,
    Q30_SPLAYER_SPRITE_TARGET_UNDER, Q30_SPLAYER_SPRITE_TARGET_OVER
};

// Draws what scripts queued on one of those layers, at the point upstream draws it, through a
// scrolling region's camera while one applies. Does nothing for 2.53 quests.
void q30_draw_script_layer(BITMAP *bmp, int layer);
// Newer quests without upstream's qr_OLD_LAYER_DRAW_ORDER: layers -7..-1 are drawn, and
// background layer 2 goes over background layer 3. False for 2.53 quests.
bool q30_new_layer_draw_order();

void draw_screen(mapscr* this_screen, bool showlink=true);
/*
  INLINE void do_scrolling_layer(int type, mapscr* layer, int x, int y, bool scrolling, int tempscreen)
  {
  do_scrolling_layer(framebuf, type, layer, x, y, scrolling, tempscreen);
  }

  INLINE void do_layer(int type, mapscr* layer, int x, int y, int tempscreen)
  {
  do_layer(framebuf, type, layer, x, y, tempscreen);
  }
  */
void put_door(BITMAP *dest,int t,int pos,int side,int type,bool redraw,bool even_walls=false);
void over_door(BITMAP *dest,int t, int pos,int side);
void putdoor(BITMAP *dest,int t,int side,int door,bool redraw=true,bool even_walls=false);
void showbombeddoor(BITMAP *dest, int side);
void openshutters();
void loadscr2(int tmp,int scr,int);
void loadscr(int tmp,int destdmap,int scr,int ldir,bool overlay);
void putscr(BITMAP* dest,int x,int y,mapscr* screen);
void putscrdoors(BITMAP *dest,int x,int y,mapscr* screen);
bool _walkflag(int x,int y,int cnt);

// 2.55+ push blocks on layers 1 and 2 (zc_sprite.cpp). True if a block on one of those
// layers started moving.
bool q30_push_layer_block(int bx, int by, int d, int pushing, bool glove_spent);
int q30_mblock_layer();                 // the layer the moving block belongs to
bool water_walkflag(int x,int y,int cnt);
bool hit_walkflag(int x,int y,int cnt);
void map_bkgsfx(bool on);

//extern FONT *lfont;
/****  View Map  ****/
extern int mapres;
bool displayOnMap(int x, int y);
void ViewMap();
int onViewMap();

//extern bool FuckIAlreadyDrewThatAlready[ 7 ];
#endif

/*** end of maps.cc ***/


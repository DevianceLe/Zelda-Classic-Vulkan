// Things only 2.55 and 3.0 quests have, shared between the player's quest readers
// (q255_*.cpp) and the game code that acts on them. Everything here is a table kept
// beside the 2.53 structures, because those structures' layouts are shared with the
// quest editor and must not change. 2.53 quests leave all of it empty.

#ifndef ZC_Q30_H
#define ZC_Q30_H

#include <map>
#include <string>
#include <vector>

#include "q255.h"

struct Q30ScriptRef
{
    word script;
    long initd[8];

    Q30ScriptRef() : script(0) { memset(initd, 0, sizeof(initd)); }
};

// DMap script slots, as upstream numbers them.
enum { q30_dmap_active, q30_dmap_passive_subscreen, q30_dmap_active_subscreen, q30_dmap_onmap, q30_dmap_kinds };

// q30_core.cpp. Written by the readers; every call is safe with any index.
void q30_clear_tables();
// How much wider a string's text may run than w: 3.0 strings choose their own side margins.
void q30_set_msg_wrap_extra(int str, int extra);
int q30_msg_wrap_extra(int str);
// A 3.0 string's own layout (STR v7+): its box size as upstream draws it, its margins (up,
// down, left, right, from the box's edges) and, from v13, where its "more" icon goes
// (upstream's message_icon: sprite, anchor and offset). q30_clear_msg_layouts drops them all
// before a quest's strings are read.
void q30_clear_msg_layouts();
void q30_set_msg_layout(int str, int box_w, int box_h, const int margins[4], bool has_icon, int icon_sprite, int icon_anchor, int icon_x, int icon_y);
int q30_msg_text_x(int str);            // where text starts in the box: the left margin, 8 otherwise
int q30_msg_text_y(int str);            // the top margin, 8 otherwise
int q30_msg_height_extra(int str);      // how much taller than h-16 the text may run, 0 otherwise
// The "more" icon's place on the playing field (before the subscreen offset) and its sprite;
// false keeps 2.53's place (zinit.msg_more_x, msg_more_y).
bool q30_msg_more_icon(int str, int *x, int *y, int *sprite);
// A 3.0 string whose text runs past 2.53's MSGSIZE: its whole text in the binary form, up to
// Q30_MSG_TEXT_MAX, kept here while MsgStrings[str].s holds the first MSGSIZE as before.
// NULL drops it (a script set the text); q30_msg_long_text is NULL for every other string.
enum { Q30_MSG_TEXT_MAX = 8192 };
void q30_set_msg_long_text(int str, const char *text);
char *q30_msg_long_text(int str);
void q30_set_combo_script(int combo, word script, const long *initd);
const Q30ScriptRef *q30_combo_script(int combo);
void q30_set_dmap_script(int dmap, int kind, word script, const long *initd);
const Q30ScriptRef *q30_dmap_script(int dmap, int kind);
// Each dmap's overlay subscreen (an index into the quest's overlay subscreens; upstream's byte,
// 0 by default) and map subscreen (-1 for none, which opens the old map).
void q30_set_dmap_subscreens(int dmap, int overlay, int map);
int q30_dmap_overlay_subscreen(int dmap);
int q30_dmap_map_subscreen(int dmap);
// Each dmap's whole title (2.53 keeps 20 characters) and intro string (upstream's intro_string_id,
// 0 for none), for quests with the newer DMap titles and intros (no qr_OLD_DMAP_INTRO_STRINGS).
void q30_set_dmap_texts(int dmap, const char *title, long intro_string);
void q30_set_dmap_floor(int dmap, int floor);     // upstream's dmap floor (0 when not given)
int q30_dmap_floor(int dmap);
void q30_set_dmap_mirror(int dmap, int mirror);   // upstream's mirrorDMap (-1 when not given)
int q30_dmap_mirror(int dmap);
const char *q30_dmap_title(int dmap);   // NULL when the quest gave none
long q30_dmap_intro_string(int dmap);

// A screen's guy look and room flags (MAP v27+; index map*MAPSCRS+screen): upstream's guytile
// (-1: the enemy's own tile, 0: none), guycs and roomflags (RFL_ALWAYS_GUY 1, RFL_GUYFIRES 2).
// q30_screen_guy is false for older quests and screens and under qr_OLD_GUY_HANDLING, which
// keep 2.53's handling.
void q30_set_screen_guy(int index, long tile, int cs, int roomflags);
bool q30_screen_guy(int index, long *tile, int *cs, int *roomflags);

// Items numbered 256 and up (3.0 has 512 or more; 2.53's itemsbuf and save keep 256). The
// readers keep their data and which of them the init data starts with; for now the game
// only gives the starting ones' counter effects (q30_give_init_high_items in link.cpp).
void q30_clear_high_items();
void q30_set_high_item(int id, const itemdata &d, int family, const char *name);
const itemdata *q30_high_item(int id, int *family = NULL, const char **name = NULL);
void q30_set_init_high_items(const byte *bits, int first_id, int count);
const std::vector<int> &q30_init_high_items();
// Scripts' itemdata (Game->LoadItemData): itemsbuf[id] below 256, the high item's data above
// (a blank item for an id the quest doesn't have). 2.53 quests only ever ask below 256.
itemdata &q30_itemsbuf(int id);
#define Q30_IDATA(id) q30_itemsbuf(id)

// q30_audio.cpp
int q30_read_advmusic(Q255Section &s);

// q30_msg.cpp: the message engine of 2.55/3.0 quests (a port of upstream's message_string.cpp).
// The reader keeps each string as upstream does (core/msgstr.h MsgStr) beside MsgStrings.
enum
{
    Q30_STRINGFLAG_FULLTILE = 0x10, Q30_STRINGFLAG_TRANS_BG = 0x20, Q30_STRINGFLAG_TRANS_FG = 0x40,
    Q30_STRINGFLAG_NOFREEZE = 0x80
};

struct Q30MsgIcon
{
    short sprite;       // -1: none
    byte anchor;        // upstream's message_anchor (up, down, left, right, l_up, r_up, l_down,
                        // r_down, center, screen, screen_y_offset)
    short x, y;

    Q30MsgIcon() : sprite(-1), anchor(7), x(0), y(0) {}
};

struct Q30MsgStr
{
    std::string text;       // as the file holds it
    bool ascii;             // STR v11+: the ascii encoding; older ones the binary one
    word nextstring;
    long tile;
    byte cset;
    bool trans;
    byte font;
    short x, y;
    word w, h;              // as upstream sizes the box (MsgStrings keeps them 16 smaller)
    word sfx, adv_sfx, menu_move_sfx, menu_close_sfx;
    byte vspace, hspace, stringflags;
    short margins[4];       // up, down, left, right
    long portrait_tile;
    byte portrait_cset, portrait_x, portrait_y, portrait_tw, portrait_th;
    byte shadow_type, shadow_color, drawlayer;
    Q30MsgIcon icons[3];    // "more", scroll up, scroll down (STR v13+)
    byte active_scroll_speed, passive_scroll_speed;

    // upstream's init_msgstr
    Q30MsgStr() : ascii(false), nextstring(0), tile(0), cset(0), trans(false), font(0), x(24), y(32), w(26 * 8), h(5 * 8),
        sfx(18), adv_sfx(0), menu_move_sfx(18), menu_close_sfx(0), vspace(0), hspace(0), stringflags(0), portrait_tile(0),
        portrait_cset(0), portrait_x(0), portrait_y(0), portrait_tw(1), portrait_th(1), shadow_type(0), shadow_color(0),
        drawlayer(6), active_scroll_speed(1), passive_scroll_speed(1)
    {
        margins[0] = 8; margins[1] = 0; margins[2] = 8; margins[3] = 8;
    }
};

void q30_msg_clear_strings();
// qr_STRING_FRAME_OLD_WIDTH_HEIGHT and qr_OLD_STRING_EDITOR_MARGINS as readrules and the string
// reader set them for older quests
void q30_msg_set_rules(bool old_frame, bool old_margins);
void q30_msg_set_string(int str, const Q30MsgStr &s);
void q30_msg_set_text(int str, const std::string &text, bool ascii);    // a script set the text
// The game's side (zelda.cpp): true for newer quests, whose strings run here.
bool q30_msg_engine();
void q30_msg_do_new(int str);           // donewmsg
void q30_msg_dismiss();                 // dismissmsg
void q30_msg_run();                     // putmsg, then the string onto msgdisplaybuf
void q30_msg_draw_icons();              // the "more" and scroll icons
void q30_msg_clear_display();
void q30_msg_init();                    // as a game starts
bool q30_msg_nofreeze();                // the string doesn't freeze the game (its NOFREEZE flag)
int q30_msg_layer();
// The string onto dest at row dy (h rows), its frame and text translucent when the string says;
// false for 2.53 quests, which draw msgdisplaybuf as before.
bool q30_msg_blit(struct BITMAP *dest, int dy, int h);
// draw_screen's draws of it: before the passive subscreen, or after it and layer 7's drawing,
// as the string's draw layer says; false for 2.53 quests
bool q30_msg_blit_screen(struct BITMAP *dest, int dy, int h, bool after_subscreen);

#endif

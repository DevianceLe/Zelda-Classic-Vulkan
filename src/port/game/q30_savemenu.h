// 2.55/3.0 quests' save menus (upstream save_menu.h): the misc section's 32 menus, the one the
// game over and F6 open, and what scripts do with them (savemenu, Game->LoadSaveMenu). The
// reader (q255_basic.cpp) fills them; the menus run in q30_scripts_sys.h.

#ifndef ZC_Q30_SAVEMENU_H
#define ZC_Q30_SAVEMENU_H

#include <string>
#include <vector>

enum
{
    Q30_NUM_SAVE_MENUS = 32, Q30_MAX_SAVEMENU_OPTIONS = 20, Q30_MAX_SAVEMENU_MISC_TEXTS = 20,
    Q30_SAVEMENU_NAME_LENGTH = 32, Q30_SAVEMENU_OPTION_TEXT_LENGTH = 48, Q30_SAVEMENU_MISC_TEXT_LENGTH = 48
};

// menu flags: SMENU_CONFIRM_A, CANCEL_B, DONT_KILL_MUSIC, REPEAT_BG, DONT_KILL_SFX
enum { Q30_SMENU_CONFIRM_A = 0x01, Q30_SMENU_CANCEL_B = 0x02, Q30_SMENU_DONT_KILL_MUSIC = 0x04, Q30_SMENU_REPEAT_BG = 0x08,
       Q30_SMENU_DONT_KILL_SFX = 0x10 };
// option flags: SMENU_OPT_EXIT, CONTINUE, RELOAD, SAVE, CANCEL
enum { Q30_SMENU_OPT_EXIT = 0x01, Q30_SMENU_OPT_CONTINUE = 0x02, Q30_SMENU_OPT_RELOAD = 0x04, Q30_SMENU_OPT_SAVE = 0x08,
       Q30_SMENU_OPT_CANCEL = 0x10 };

struct Q30SaveMenuOption
{
    std::string text;
    int flags;
    int color, picked_color;
    int shadow_type, shadow_color, picked_shadow_color;
    long font;
    int script;             // a generic script run frozen when it is picked (gen_scrconfig)
    long initd[8];

    Q30SaveMenuOption() : flags(0), color(254), picked_color(254), shadow_type(0), shadow_color(253), picked_shadow_color(253),
        font(0), script(0)
    {
        for(int q = 0; q < 8; ++q)
            initd[q] = 0;
    }
};

struct Q30SaveMenuMiscText
{
    std::string text;
    int color, shadow_type, shadow_color;
    long font;
    int x, y, text_align;

    Q30SaveMenuMiscText() : color(254), shadow_type(0), shadow_color(253), font(0), x(128), y(32), text_align(1) {}
};

struct Q30SaveMenu
{
    std::string name;
    int flags;
    long cursor_tile, bg_tile;
    int cursor_cset, bg_cset, bg_tw, bg_th, bg_color;
    int cursor_sfx, choose_sfx;
    int hspace, vspace, opt_x, opt_y;
    int opt_sel_x_offset, opt_sel_y_offset;
    int text_align, textbox_align;
    int close_frames, close_flash_rate;
    int music;              // a 3.0 music (1-based; 0 none)
    std::vector<Q30SaveMenuOption> options;
    std::vector<Q30SaveMenuMiscText> misc_texts;

    // upstream's defaults: WHITE 254, BLACK 253, WAV_CHINK 6, ALIGN_LEFT 0, ALIGN_CENTER 1
    Q30SaveMenu() : flags(0), cursor_tile(0), bg_tile(0), cursor_cset(0), bg_cset(0), bg_tw(1), bg_th(1), bg_color(253),
        cursor_sfx(6), choose_sfx(6), hspace(6), vspace(16), opt_x(128), opt_y(48), opt_sel_x_offset(0), opt_sel_y_offset(0),
        text_align(0), textbox_align(1), close_frames(30), close_flash_rate(5), music(0) {}
};

#ifdef ZC_PLAYER_BUILD
void q30_savemenus_clear();             // as a quest is read
Q30SaveMenu *q30_savemenu_slot(int index);  // 0-based; NULL past the 32
void q30_savemenu_set_special(int game_over, int f6);   // savemenu_game_over, savemenu_f6 (1-based, 0 none)
// The hero just died (Quit is qGAMEOVER): the quest's game over save menu, if it has one, as
// upstream's game loop runs it. Nothing for quests without one.
void q30_savemenu_game_over();
#endif

#endif

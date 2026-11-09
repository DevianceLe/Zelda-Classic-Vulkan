// The ZScript engine for 2.55 and 3.0 quests: every script type 2.53 has no slot for.
//
// All of this is keyed beside 2.53's own structures (sprite uid, script slot, combo
// position), and is empty for a 2.53 quest, so nothing here reaches a 2.53 quest's behaviour.

#ifndef ZC_Q30_SCRIPTS_H
#define ZC_Q30_SCRIPTS_H

#include <map>
#include <string>
#include <vector>

#include "q30.h"

// Script types past 2.53's (see SCRIPT_* in ffscript.h: global, ffc, screen, link, item, lwpn,
// npc, subscreen, ewpn are 0..8).
enum
{
    SCRIPT_DMAP = 9, SCRIPT_ITEMSPRITE, SCRIPT_ACTIVESUB, SCRIPT_PASSIVESUB, SCRIPT_COMBO,
    SCRIPT_ONMAP, SCRIPT_GENERIC, SCRIPT_GENERICFROZEN, SCRIPT_ENGINESUB
};

// How many slots each table has; upstream's limits.
enum
{
    Q30_GLOBAL_SLOTS = 8, Q30_HERO_SLOTS = 5, Q30_LWPN_SLOTS = 256, Q30_EWPN_SLOTS = 256, Q30_DMAP_SLOTS = 256,
    Q30_ITEMSPRITE_SLOTS = 256, Q30_COMBO_SLOTS = 512, Q30_GENERIC_SLOTS = 512, Q30_SUBSCREEN_SLOTS = 256
};

// Written by the script reader (q255_scripts.cpp), read by the engine (ffscript.cpp).
extern ffscript *q30_global_scripts[Q30_GLOBAL_SLOTS]; // slots 4..7; 0..3 are 2.53's globalscripts[]
extern ffscript *q30_hero_scripts[Q30_HERO_SLOTS];
extern ffscript *q30_lwpn_scripts[Q30_LWPN_SLOTS];
extern ffscript *q30_ewpn_scripts[Q30_EWPN_SLOTS];
extern ffscript *q30_dmap_scripts[Q30_DMAP_SLOTS];
extern ffscript *q30_itemsprite_scripts[Q30_ITEMSPRITE_SLOTS];
extern ffscript *q30_combo_scripts[Q30_COMBO_SLOTS];
extern ffscript *q30_generic_scripts[Q30_GENERIC_SLOTS];
extern ffscript *q30_subscreen_scripts[Q30_SUBSCREEN_SLOTS];

// Name binding: slot number by the name the quest gave it, as Game->GetNPCScript("name") etc. need.
enum { q30_names_ffc, q30_names_global, q30_names_item, q30_names_npc, q30_names_lweapon, q30_names_eweapon,
       q30_names_hero, q30_names_dmap, q30_names_screen, q30_names_itemsprite, q30_names_combo, q30_names_generic,
       q30_names_subscreen, q30_names_count };
extern std::map<std::string, int> q30_script_names[q30_names_count];

void q30_script_tables_reset(); // quest load: frees every slot above

// Per-enemy, per-item script and weapon-script settings, filled by the readers.
void q30_clear_guy_scripts();
void q30_set_guy_script(int guy, word script, const long *initd);
const Q30ScriptRef *q30_guy_script(int guy);
void q30_set_guy_weapon_script(int guy, word script, const long *initd);
const Q30ScriptRef *q30_guy_weapon_script(int guy);

void q30_clear_item_scripts();
void q30_set_item_sprite_script(int item, word script, const long *initd);
const Q30ScriptRef *q30_item_sprite_script(int item);
void q30_set_item_weapon_script(int item, word script, const long *initd);
const Q30ScriptRef *q30_item_weapon_script(int item);

// The stages of a frame a generic script can wait for (upstream's scr_timing).
enum
{
    Z30T_START_FRAME, Z30T_POST_COMBO_ANIM, Z30T_POST_POLL_INPUT, Z30T_POST_FFCS, Z30T_POST_GLOBAL_ACTIVE,
    Z30T_POST_PLAYER_ACTIVE, Z30T_POST_DMAPDATA_ACTIVE, Z30T_POST_DMAPDATA_PASSIVESUBSCREEN, Z30T_POST_COMBOSCRIPT,
    Z30T_POST_PUSHBLOCK, Z30T_POST_ITEMSPRITE_SCRIPT, Z30T_POST_ITEMSPRITE_ANIMATE, Z30T_POST_NPC_ANIMATE,
    Z30T_POST_EWPN_ANIMATE, Z30T_POST_EWPN_SCRIPT, Z30T_POST_OLD_ITEMDATA_SCRIPT, Z30T_POST_PLAYER_ANIMATE,
    Z30T_POST_NEW_ITEMDATA_SCRIPT, Z30T_POST_CASTING, Z30T_POST_LWPN_ANIMATE, Z30T_POST_DECOPARTICLE_ANIMATE,
    Z30T_POST_COLLISIONS_PALETTECYCLE, Z30T_WAITDRAW, Z30T_POST_GLOBAL_WAITDRAW, Z30T_POST_PLAYER_WAITDRAW,
    Z30T_POST_DMAPDATA_ACTIVE_WAITDRAW, Z30T_POST_DMAPDATA_PASSIVESUBSCREEN_WAITDRAW, Z30T_POST_SCREEN_WAITDRAW,
    Z30T_POST_FFC_WAITDRAW, Z30T_POST_COMBO_WAITDRAW, Z30T_POST_ITEM_WAITDRAW, Z30T_POST_NPC_WAITDRAW,
    Z30T_POST_EWPN_WAITDRAW, Z30T_POST_LWPN_WAITDRAW, Z30T_POST_ITEMSPRITE_WAITDRAW, Z30T_POST_DRAW,
    Z30T_POST_STRINGS, Z30T_END_FRAME, Z30T_INIT
};

struct Zasm30Generic
{
    bool run, atleast, waitevent, loaded;
    int waituntil;
    word exit_state, reload_state;
    dword event_state;
    long initd[8];
    std::vector<long> data;

    Zasm30Generic() : run(false), atleast(true), waitevent(false), loaded(false), waituntil(Z30T_START_FRAME), exit_state(0), reload_state(0), event_state(0)
    {
        memset(initd, 0, sizeof(initd));
    }

    void clear()
    {
        atleast = true; waitevent = false; waituntil = Z30T_START_FRAME;
        exit_state = reload_state = 0; event_state = 0;
        memset(initd, 0, sizeof(initd));
        data.clear();
    }
};

extern Zasm30Generic zasm30_generic[Q30_GENERIC_SLOTS];
extern int zasm30_genscript_timing;

// Generic script setup from the init data (q255_init.cpp): which run at game start, their state.
void q30_clear_generic_init();
void q30_set_generic_init(int slot, bool run, word exit_state, word reload_state, dword event_state,
                          const long *initd, const std::map<dword, long> *data);

// Every sprite tells the script engine it is gone, so its script state and owned objects go too.
void q30_sprite_destroyed(long uid);

// Game loop stages for 2.55+ quests (zelda.cpp). All do nothing for older quests.
bool q30_scripts_active();
void q30_gen_timing(int timing);    // Runs generic scripts waiting for this frame stage.
void q30_frame_stage(int stage);    // Named stages of game_loop, below.
void q30_run_hero_active();
void q30_run_dmap_active();
void q30_run_passive_subscreen();
void q30_run_combo_scripts(bool waitdraw);
void q30_run_itemsprite_scripts(bool waitdraw);
void q30_run_npc_scripts(bool waitdraw);
void q30_run_lweapon_scripts(bool waitdraw);
void q30_run_eweapon_scripts(bool waitdraw);
void q30_run_ffc_waitdraw();
void q30_run_screen_waitdraw();
void q30_run_item_scripts(bool waitdraw);
void q30_run_waitdraw_scripts();
void q30_game_init(bool first);     // before the first frame of a game: run Init-type scripts
void q30_clear_engine();            // game end: frees every instance and script object
void q30_run_hero_script(int slot);  // Hero's init(1), death(3), win(4)
void q30_run_ondeath();
void q30_run_onwin();
void q30_run_onsave();
void q30_run_onlaunch();
void q30_run_continue();
void q30_generic_event(int event);
void q30_generic_exit_state(int state);
void q30_generic_reload_state(int state);

// enemy::animate: this enemy's script runs here when it has one. Returns true if the enemy was
// deleted by its own script, so the caller must not touch it any more.
bool q30_run_npc_script(sprite *spr);

// How a 2.55+ quest's enemy of a script family is built (guys.cpp).
bool q30_item_collect_hook(int item_id); // an item was collected this frame

enum
{
    Q30_STAGE_FFC_DONE, Q30_STAGE_GLOBAL_DONE, Q30_STAGE_COMBO_ANIM_DONE, Q30_STAGE_INPUT_DONE
};

#endif

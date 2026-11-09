// 3.0 combo data kept beside combobuf: everything newcombo has no field for, and the
// game side of the combo trigger system. 2.53 quests leave all of it empty; every
// lookup returns NULL then, so 2.53 code paths behave as before.
//
// Numbers are upstream's: trigger flags, combo types and combo flags in here use the
// values in ZQuestClassic 3.0's combo.h and zdefs.h. combobuf[].type and .flag hold this
// engine's numbers; the true ones are in Q30Combo.
//
// Values upstream keeps as zfix are stored as they are in the quest (a long, 10000 = 1).

#ifndef ZC_Q30_TRIGGERS_H
#define ZC_Q30_TRIGGERS_H

#include <string>
#include <vector>

#include "zc_alleg.h"
#include "zdefs.h"

class weapon;

// A bitstring from the quest.
struct Q30Bits
{
    std::vector<byte> v;

    bool get(size_t i) const
    {
        size_t b = i / 8;
        return b < v.size() && (v[b] & (1 << (i % 8))) != 0;
    }
    void set(size_t i)
    {
        size_t b = i / 8;
        if(b >= v.size())
            v.resize(b + 1, 0);
        v[b] |= (byte)(1 << (i % 8));
    }
    bool empty() const
    {
        for(size_t i = 0; i < v.size(); ++i)
            if(v[i])
                return false;
        return true;
    }
};

// upstream's enum ComboTriggerFlag, same order
enum Q30TrigFlag
{
    Q30TF_SWORD, Q30TF_SWORDBEAM, Q30TF_BRANG, Q30TF_BOMB, Q30TF_SBOMB, Q30TF_LITBOMB, Q30TF_LITSBOMB,
    Q30TF_ARROW, Q30TF_FIRE, Q30TF_WHISTLE, Q30TF_BAIT, Q30TF_WAND, Q30TF_MAGIC, Q30TF_WIND,
    Q30TF_REFMAGIC, Q30TF_REFFIREBALL, Q30TF_REFROCK, Q30TF_HAMMER, Q30TF_RESETANIM, Q30TF_INVERTPROX,
    Q30TF_BTN_TOP, Q30TF_BTN_BOTTOM, Q30TF_BTN_LEFT, Q30TF_BTN_RIGHT, Q30TF_INVERTMINMAX, Q30TF_STEP,
    Q30TF_STEPSENS, Q30TF_SHUTTER, Q30TF_CMBTYPEFX, Q30TF_ONLYGENTRIG, Q30TF_KILLWPN, Q30TF_EWFIREBALL,
    Q30TF_HOOKSHOT, Q30TF_SPARKLE, Q30TF_BYRNA, Q30TF_REFBEAM, Q30TF_STOMP,
    Q30TF_SCRIPT01, Q30TF_SCRIPT02, Q30TF_SCRIPT03, Q30TF_SCRIPT04, Q30TF_SCRIPT05,
    Q30TF_SCRIPT06, Q30TF_SCRIPT07, Q30TF_SCRIPT08, Q30TF_SCRIPT09, Q30TF_SCRIPT10,
    Q30TF_AUTOMATIC, Q30TF_SECRETS, Q30TF_INVERTITEM, Q30TF_CONSUMEITEM, Q30TF_COUNTERGE, Q30TF_COUNTERLT,
    Q30TF_COUNTEREAT, Q30TF_CTRNONLYTRIG, Q30TF_LIGHTON, Q30TF_LIGHTOFF, Q30TF_PUSH, Q30TF_LENSON, Q30TF_LENSOFF,
    Q30TF_EWARROW, Q30TF_EWBRANG, Q30TF_EWSWORD, Q30TF_EWROCK,
    Q30TF_EWSCRIPT01, Q30TF_EWSCRIPT02, Q30TF_EWSCRIPT03, Q30TF_EWSCRIPT04, Q30TF_EWSCRIPT05,
    Q30TF_EWSCRIPT06, Q30TF_EWSCRIPT07, Q30TF_EWSCRIPT08, Q30TF_EWSCRIPT09, Q30TF_EWSCRIPT10,
    Q30TF_EWMAGIC, Q30TF_EWBBLAST, Q30TF_EWSBBLAST, Q30TF_EWLITBOMB, Q30TF_EWLITSBOMB, Q30TF_EWFIRETRAIL,
    Q30TF_EWFLAME, Q30TF_EWWIND, Q30TF_EWFLAME2, Q30TF_SPCITEM, Q30TF_EXSTITEM, Q30TF_EXSTENEMY,
    Q30TF_AUTOGRABITEM, Q30TF_ENEMIESKILLED, Q30TF_SECRETSTR, Q30TF_THROWN, Q30TF_QUAKESTUN, Q30TF_SQUAKESTUN,
    Q30TF_ANYFIRE, Q30TF_STRONGFIRE, Q30TF_MAGICFIRE, Q30TF_DIVINEFIRE, Q30TF_TRIGLEVELSTATE, Q30TF_LEVELSTATE,
    Q30TF_TRIGGLOBALSTATE, Q30TF_GLOBALSTATE, Q30TF_KILLENEMIES, Q30TF_CLEARENEMIES, Q30TF_CLEARLWEAPONS,
    Q30TF_CLEAREWEAPONS, Q30TF_IGNITE_ANYFIRE, Q30TF_IGNITE_STRONGFIRE, Q30TF_IGNITE_MAGICFIRE,
    Q30TF_IGNITE_DIVINEFIRE, Q30TF_SEPARATEWEAPON, Q30TF_TGROUP_CONTRIB, Q30TF_TGROUP_LESS, Q30TF_TGROUP_GREATER,
    Q30TF_PUSHEDTRIG, Q30TF_DIVETRIG, Q30TF_DIVESENSTRIG, Q30TF_LWREFARROW, Q30TF_LWREFFIRE, Q30TF_LWREFFIRE2,
    Q30TF_TOGGLEDARK, Q30TF_COND_DARK, Q30TF_COND_NODARK, Q30TF_LITEM_COND, Q30TF_LITEM_REVCOND, Q30TF_LITEM_SET,
    Q30TF_LITEM_UNSET, Q30TF_TINT_CLEAR, Q30TF_ONLY_GROUND_WPN, Q30TF_COUNTERDISCOUNT, Q30TF_SCREENLOAD,
    Q30TF_UNSETEXSTATE, Q30TF_UNSETEXDOOR, Q30TF_PLAYER_STANDING, Q30TF_PLAYER_NOTSTANDING, Q30TF_INVERT_PLAYER_Z,
    Q30TF_COUNTER_GRADUAL, Q30TF_COUNTER_PERCENT, Q30TF_NO_COPYCAT_CAUSE, Q30TF_PLAYERLANDHERE,
    Q30TF_PLAYERLANDANYWHERE, Q30TF_SETPLAYER_X_ABS, Q30TF_SETPLAYER_X_REL_CMB, Q30TF_SETPLAYER_Y_ABS,
    Q30TF_SETPLAYER_Y_REL_CMB, Q30TF_SETPLAYER_Z_ABS, Q30TF_CMBTYPECAUSES, Q30TF_REQ_JUMP_GE, Q30TF_REQ_JUMP_LE,
    Q30TF_REQ_X_GE, Q30TF_REQ_X_LE, Q30TF_REQ_X_REL, Q30TF_REQ_Y_GE, Q30TF_REQ_Y_LE, Q30TF_REQ_Y_REL,
    Q30TF_FORCE_ICE_VX, Q30TF_FORCE_ICE_VY, Q30TF_UNIGNITE_WEAPONS, Q30TF_CANCEL_TRIGGER, Q30TF_SET_GRAVITY,
    Q30TF_REVERT_GRAVITY, Q30TF_BLOCK_TRIGGER_SAME_LAYER, Q30TF_BLOCK_TRIGGER_ANY_LAYER,
    Q30TF_VIEWPORT_REQ_ONSCREEN, Q30TF_VIEWPORT_REQ_OFFSCREEN, Q30TF_SFX_LOOP, Q30TF_SFX_NO_RESTART, Q30TF_SFX_KILL,
    Q30TF_CMB_CHANGE_ABSOLUTE, Q30TF_CSET_CHANGE_ABSOLUTE, Q30TF_SWIMTRIG, Q30TF_SWIMSENSTRIG, Q30TF_RESPAWN_HERE,
    Q30TF_RESET_RESPAWN, Q30TF_SCREENUNLOAD, Q30TF_PROX_USE_SOL_HITBOX, Q30TF_PROX_USE_SQUARE,
    Q30TF_COUNTER_PARTIAL_CONSUME, Q30TF_MAPVIEW_ONLY, Q30TF_MAPVIEW_NEVER, Q30TF_COMBOPOSSTATE_CAUSE,
    Q30TF_COMBOPOSSTATE_REQ, Q30TF_COMBOPOSSTATE_UNREQ, Q30TF_COMBOPOSSTATE_SET, Q30TF_COMBOPOSSTATE_UNSET,
    Q30TF_TRIGGER_LARGE_COMBO_COPYCAT, Q30TF_TRIGGERED_BY_LARGE_COMBO_COPYCAT,
    Q30TF_MAX
};

// upstream's combo types past the numbering this engine shares (cSCRIPT5 = last shared)
// the thrown-object weapon upstream calls wThrown; free in this engine's id range
enum { Q30W_THROWN = 120 };

enum Q30ComboType
{
    Q30C_SCRIPT6 = 147, Q30C_SCRIPT7, Q30C_SCRIPT8, Q30C_SCRIPT9, Q30C_SCRIPT10, Q30C_SCRIPT11, Q30C_SCRIPT12,
    Q30C_SCRIPT13, Q30C_SCRIPT14, Q30C_SCRIPT15, Q30C_SCRIPT16, Q30C_SCRIPT17, Q30C_SCRIPT18, Q30C_SCRIPT19,
    Q30C_SCRIPT20, Q30C_TRIGGERGENERIC, Q30C_PITFALL, Q30C_STEPSFX, Q30C_BRIDGE, Q30C_SIGNPOST, Q30C_CSWITCH,
    Q30C_CSWITCHBLOCK, Q30C_TORCH, Q30C_SPOTLIGHT, Q30C_GLASS, Q30C_LIGHTTARGET, Q30C_SWITCHHOOK, Q30C_BUTTONPROMPT,
    Q30C_CUSTOMBLOCK, Q30C_SHOOTER, Q30C_SLOPE, Q30C_CUTSCENETRIG, Q30C_PUSHBLOCK, Q30C_ICY, Q30C_MIRRORNEW,
    Q30C_CRUMBLE, Q30C_CUTSCENEEFFECT, Q30C_MAX
};

// one combo_trigger; defaults as upstream's
struct Q30Trigger
{
    std::string label;
    Q30Bits flags;
    long triggerlevel;
    byte triggerbtn;
    word triggeritem;
    word trigtimer;
    word trigsfx;
    long trigchange;
    word trigprox;
    short trigctr;
    long trigctramnt;
    byte triglbeam;
    signed char trigcschange;
    long spawnitem;
    short spawnenemy;
    signed char exstate, exdoor_dir;
    byte exdoor_ind;
    byte combopos_state;
    long spawnip;
    byte trigcopycat;
    byte trigcooldown;
    byte trig_lstate, trig_gstate;
    long trig_statetime;
    word trig_genscr;
    byte trig_group;
    word trig_group_val;
    word trig_levelitems;
    dword req_level_state, unreq_level_state;
    Q30Bits req_global_state, unreq_global_state, req_screen_state, unreq_screen_state,
        req_screen_ex_state, unreq_screen_ex_state;
    Q30Bits req_screen_ex_door[4], unreq_screen_ex_door[4];
    short trigdmlevel;
    byte trigstatemap, trigstatescreen;
    short trigtint[3];
    short triglvlpalette, trigbosspalette;
    short trigquaketime, trigwavytime;
    short trig_swjinxtime, trig_itmjinxtime, trig_shieldjinxtime, trig_stuntime, trig_bunnytime;
    byte trig_pushtime;
    word prompt_cid, fail_prompt_cid;
    byte prompt_cs, fail_prompt_cs;
    short prompt_x, prompt_y;
    long trig_msgstr, fail_msgstr;
    long req_player_x, req_player_y, req_player_z, req_player_jump;
    long dest_player_x, dest_player_y, dest_player_z, player_bounce;
    byte req_player_dir;
    signed char dest_player_dir;
    long force_ice_combo;
    long force_ice_vx, force_ice_vy;
    long trig_gravity, trig_terminal_v;
    long play_music;
    signed char set_music_refresh;
    short viewport_cond_range;
    long chance_numerator, chance_denominator;
    short sfx_pan;
    byte sfx_volume;
    long sfx_frequency;
    byte large_combo_copycat;

    Q30Trigger();
};

// the parts of a weapon_data game code uses
struct Q30WeaponData
{
    word flags;
    dword moveflags;
    word wflags;
    word burnsprs[5];
    byte light_rads[5];
    long light_offsets[5];
    byte glow_shape;
    long override_flags;
    long tilew, tileh, hxsz, hysz, hzsz, hxofs, hyofs, xofs, yofs;
    long step;
    byte unblockable;
    long timeout;
    byte imitate_weapon, default_defense;
    byte lift_level, lift_time;
    long lift_height;
    word script;
    long initd[8];
    short pierce_count;
    long bounce_mult, bounce_add;
    long viewport_suspend_range, viewport_despawn_range;

    Q30WeaponData();
};

// a combo's data newcombo has no room for
struct Q30Combo
{
    byte type;              // upstream's number
    byte flag;              // upstream's number
    byte walk;              // whole byte: effect squares in the high nibble
    long attributes[24];
    dword usrflags;
    word genflags;
    std::string label;
    word script;
    long initd[8];

    std::vector<Q30Trigger> triggers;
    byte only_gentrig;

    // lifting
    word liftcmb, liftundercmb;
    byte liftcs, liftundercs;
    byte liftdmg, liftlvl, liftitm, liftflags, liftgfx;
    word liftsprite, liftsfx;
    short liftbreaksprite;
    byte liftbreaksfx;
    byte lifthei, lifttime;
    word lift_parent_item;
    Q30WeaponData lift_weap;
    Q30WeaponData misc_weap;

    // general data
    byte speed_mult, speed_div;
    long speed_add;
    word sfx_appear, sfx_disappear, sfx_loop, sfx_walking, sfx_standing, sfx_tap, sfx_landing;
    word sfx_falling, sfx_drowning, sfx_lava_drowning;
    word spr_appear, spr_disappear, spr_walking, spr_standing, spr_falling, spr_drowning, spr_lava_drowning;
    long z_height, z_step_height;
    byte dive_under_level;
    byte large_combo_dirs;

    // animation
    word delay;
    byte cs2_flash_duration;

    Q30Combo();
};

// ---- readers -----------------------------------------------------------------------

void q30_combos_clear();
void q30_trigger_stats();   // logs what the quest uses when ZC_Q30_TRIGSTATS is set
bool q30_trigger_combo_at(int layer, int pos, int idx);     // Screen->TriggerCombo

// Item scripts that keep running across frames (quest rule 162). See q30_scripts_engine.h.
bool q30_items_keep_running();
bool q30_item_script_busy(int id);          // true: it's still running, so using the item does nothing
void q30_use_item_script(int id);           // the player used the item (2.53's behaviour without the rule)
void q30_passive_item_script(int id);       // the Hero got an item with a passive script
void q30_item_scripts_tick(bool waitdraw);  // a frame of the scrolling loop
void q30_set_item_passive_script(int id, bool on);  // for the quest reader
// Hands the table the combo; takes ownership.
void q30_combo_store(int id, Q30Combo *data);
// The quest's whole rule bitfield (q255_read_rules should call this).
void q30_set_quest_rules(const byte *rules, size_t bytes);
bool q30_quest_rule(int id);                   // upstream's rule number; false for older quests

// upstream's rule numbers (qrs.h) the game code reads
enum Q30QuestRuleId
{
    Q30_QR_SIDEVIEWLADDER_FACEUP = 178,
    Q30_QR_BUSHESONLAYERS1AND2 = 198,
    Q30_QR_NEW_HERO_MOVEMENT = 199,
    Q30_QR_DISABLE_4WAY_GRIDLOCK = 200,
    Q30_QR_NO_SCROLL_WHILE_IN_AIR = 206,
    Q30_QR_NO_HOPPING = 218,
    Q30_QR_NO_SOLID_SWIM = 219,
    Q30_QR_SHALLOW_SENSITIVE = 222,
    Q30_QR_SMARTER_SMART_SCROLL = 223,
    Q30_QR_OVERHEAD_COMBOS_L1_L2 = 229,
    Q30_QR_NONHEAVY_BLOCKTRIGGER_PERM = 225,
    Q30_QR_DUNGEON_DMAPS_PERM_SECRETS = 204,
    Q30_QR_ENEMIES_SECRET_ONLY_16_31 = 239,
    Q30_QR_PUSHBLOCK_LAYER_1_2 = 246,
    Q30_QR_BLOCKHOLE_SAME_ONLY = 257,
    Q30_QR_BLOCKS_DONT_LOCK_OTHER_LAYERS = 264,
    Q30_QR_STEPTEMP_SECRET_ONLY_16_31 = 248,
    Q30_QR_ALLTRIG_PERMSEC_NO_TEMP = 249,
    Q30_QR_HOOKSHOTALLLAYER = 255,
    Q30_QR_LESS_AWFUL_SIDESPIKES = 282,
    Q30_QR_PERMANENT_WHISTLE_SECRETS = 296,
    Q30_QR_GRASS_SENSITIVE = 329,
    Q30_QR_BETTER_RAFT = 330,
    Q30_QR_DMAP_0_CONTINUE_BUG = 334,
    Q30_QR_CONVEYORS_L1_L2 = 339,
    Q30_QR_SENSITIVE_SOLID_DAMAGE = 359,
    Q30_QR_LENIENT_SOLID_DAMAGE = 360,
    Q30_QR_POUNDLAYERS1AND2 = 404,
    Q30_QR_NEW_HERO_MOVEMENT2 = 406,
    Q30_QR_PUSHBLOCK_SPRITE_LAYER = 421,
    Q30_QR_BROKEN_PUSHBLOCK_TOP_HALF_SOLIDS = 442,
    Q30_QR_BROKEN_PUSHBLOCK_FLAG_CLONING = 443,
    Q30_QR_CONVEYORS_ALL_LAYERS = 434,
    Q30_QR_WEAPONS_EXTRA_DEATH_FRAME = 664
};

// For a rule whose "on" means the old 2.53 behaviour: true for every 2.53 quest and for
// newer quests with the rule set.
bool q30_compat_rule(int id);

// ---- lookups, for any code that wants a combo's extra data -------------------------

// NULL for a combo with no extra data, or in a quest older than 2.55.
const Q30Combo *q30_combo(int id);
// The combo's true type and flag, or the engine's own if it has no entry.
int q30_combo_type(int id);
int q30_combo_flag(int id);

// ---- hooks the game calls; each does nothing for a 2.53 quest ---------------------

// Once per frame while the game runs (not frozen).
void q30_trig_update();
// A new screen just became tmpscr / is being left (trigger timers, ScreenLoad triggers).
void q30_trig_screen_load();
void q30_trig_screen_unload();
// Quest-side hooks; these keep their state here.
void q30_note_item_exstate(long uid, int exstate);
void q30_note_enemy_exstate(long uid, int exstate);
void q30_item_collected(long uid);          // an item with an exstate was picked up
void q30_enemy_killed(long uid);            // an enemy with an exstate died (call from guys.cpp)
void q30_play_quest_music(long id);
void q30_run_generic_script(word id);       // a trigger's generic script (runs the registered runner)
void q30_set_generic_script_runner(void (*fn)(word));   // the script engine registers its runner
void q30_set_music_player(void (*fn)(long));            // the audio code registers quest music playback
void q30_force_ice(long combo);
void q30_force_ice_velocity(bool setx, long vx, bool sety, long vy);
void q30_draw_prompt(BITMAP *dest);         // button prompt combo near Link, if any
void q30_trig_push();                       // Link is pushing: fires push triggers
bool q30_active();                          // true for 2.55+ quests
bool q30_has_bridge_at(int x, int y, int layer);
bool q30_has_bridge_above(int x, int y, int target_layer);
int q30_link_mapcombo(int x, int y);        // MAPCOMBO, but 0 where a bridge covers the screen combo
bool q30_walkflag(int x, int y, int cnt);   // _walkflag for 2.55+ quests
void q30_walk_speed(int x, int y, int z, int &dx, int &dy);  // combo walk-speed modifiers
double q30_walk_speed_scale(int x, int y, int z, double step);   // same, for a fractional step
bool q30_respawn_forced();                  // a trigger fixed the drown respawn point
bool q30_link_stunned();
long q30_item_switch_family(int itemid);    // the family startwpn should treat this item as
bool q30_switchhook_frame(weapon *w);       // true: the hook head is done
void q30_switchhook_update();
long q30_item_family(int item);             // upstream's family number for any item
void q30_clear_item_families();
void q30_set_item_family(int item, long family);   // families 2.53 can't hold; reader only
int q30_current_item(long family);          // owned item of such a family, or -1
bool q30_lifting();
bool q30_lift_busy();
void q30_lift_reset();
void q30_drop_lift();
void q30_lift_on_hit();
void q30_do_liftglove(int liftid, bool passive);
int q30_startwpn(int itemid);               // -1: not handled, else startwpn's return
void q30_thrown_frame(weapon *w);
void q30_draw_lift(BITMAP *dest);
void q30_draw_darkness(BITMAP *dest);
void q30_cutscene_flags(const Q30Combo *c);
void q30_cutscene_clear();
void q30_cutscene_input();                  // call after the control state is read
bool q30_sideview_platform_at(int x, int y);
bool q30_unsafe_ground(int x, int y);
void q30_draw_beams(BITMAP *dest);          // light beams, over the playing field
// Upstream's placed flag numbers, which the map reader can note for flags 2.53 can't hold.
void q30_clear_placed_flags();
void q30_note_placed_flag(int screen_index, int pos, byte flag);
int q30_true_placed_flag(int screen_index, int pos);
int q30_ice_move(bool u, bool d, bool l, bool r);    // 0: not on ice; else Link's move was handled here
int q30_link_gravity();
int q30_link_terminalv();
int q30_mirror_new(int x, int y, int dir);
bool q30_pushblock_ok(int bx, int by, int d, int pushing, bool enemies);
void q30_pushblock_sfx(int bx, int by, int linkx);
void q30_tint(int r, int g, int b);
void q30_tint_clear();
void q30_set_gravity(long gravity, long terminal);
void q30_revert_gravity();

// Link's frame: step, swim and dive triggers, proximity-free automatic ones, buttons,
// pushing, and types that act on him (pitfalls, slopes, ice, shooters).
void q30_trig_link_frame();
// A weapon's trigger check for this frame (call from the weapon's animate).
void q30_trig_weapon(weapon *w);
// A fire weapon at x,y burning. Which levels are lit come as bits, 1=any 2=strong 4=magic 8=divine.
void q30_trig_fire(weapon *w, int x, int y, int levels);
// Enemies cleared on this screen.
void q30_trig_enemies_killed();
// Secrets happened by any cause.
void q30_trig_secrets();
// Dungeon shutters opened.
void q30_trig_shutters();
// A level (dmap level) or global state changed: fires level/global-state triggers.
void q30_trig_level_state(dword changed);
void q30_trig_global_state(int state);
// Lens of Truth on or off.
void q30_trig_lens(bool on);
// Quake hammer: stun triggers within rad of x,y at the given level.
void q30_trig_quake(int x, int y, int rad, int level, bool super);
// Link landed on the ground.
void q30_trig_land();
// Sign posts, prompts and button triggers for the combo Link faces. Returns true if a
// button press was used.
bool q30_trig_buttons(int dir);

// ---- state other systems can use ---------------------------------------------------

bool q30_get_exstate(int mi, int bit);
void q30_set_exstate(int mi, int bit, bool on);
bool q30_get_xdoor(int mi, int dir, int ind);
void q30_set_xdoor(int mi, int dir, int ind, bool on);
byte &q30_pos_state(int mi, int key);

#endif

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
class sprite;

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
// Link's attack while he swings the bug net: upstream's wBugNet (free in this engine's range)
enum { Q30W_BUGNET = 52 };

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

    // For ZQuest's writers, which encode a changed record again: the data's own format
    // version as the file had it (0: none read) and its script's instance values, as bytes.
    int data_version;           // -1: none read
    std::vector<byte> inst_raw;

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

    // For ZQuest's writers: the combo script's instance values (v72+), as bytes.
    std::vector<byte> script_inst_raw;

    Q30Combo();
};

// ---- readers -----------------------------------------------------------------------

void q30_combos_clear();
void q30_trigger_stats();   // logs what the quest uses when ZC_Q30_TRIGSTATS is set
void q30_slope_audit();     // logs where slopes are placed (ZC_Q255_DUMP_USE)
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
void q30_draw_mirror_portal(BITMAP *dest);  // the Magic Mirror's portal on this screen, if any
// The bug net (2.55/3.0): q30_startwpn answers 2 for it and link.cpp starts the swing as
// attack Q30W_BUGNET; each frame of it positions the net and catches items; draw_screen draws it.
void q30_bugnet_start(int itemid);
void q30_bugnet_frame(int attackclk);
void q30_draw_bugnet(BITMAP *dest);
// The bunny form (2.55/3.0): its clock (upstream lbunnyclock), whether an item can be used now
// (upstream checkbunny), the tile modifier on Link while a bunny, and the readers' data.
int q30_bunny_clock();
void q30_set_bunny_clock(int v);
bool q30_bunny_allows(int itemid);
int q30_bunny_tile_mod();
void q30_bunny_check(bool on);              // off while the subscreen draws or chooses (nests)
void q30_bunny_reset();                     // a game starts or continues
void q30_set_bunny_ltm(long ltm);           // readers
void q30_set_item_bunny(int item, bool usable);     // readers; item -1 clears all
bool q30_item_bunny(int item);
// Item pickups (2.55/3.0): an item's own pickup flags (applied when an item sprite is made,
// upstream's pickupflag), its pickup string and flags, which strings have been shown (saved),
// and the screen flag that makes screen items trigger secrets. 2.53 quests get none of it.
enum { Q30_IP_SECRETS = 4096 };             // upstream ipSECRETS
void q30_set_item_pickup(int item, long pickup, int mode, int pstring, int psflags);  // readers; -1 clears
// All 32 bits of an item's flags (upstream's item_flag9 and up are bits 16..); 2.53's 16 for
// 2.53 quests. The low 16 always come from itemsbuf, which scripts may change.
enum { Q30_ITEM_FLAG6 = 0x2000, Q30_ITEM_FLAG7 = 0x4000, Q30_ITEM_FLAG8 = 0x8000, Q30_ITEM_FLAG9 = 0x10000,
       Q30_ITEM_FLAG10 = 0x20000, Q30_ITEM_FLAG11 = 0x40000 };     // past 2.53's ITEM_FLAG5
void q30_set_item_flags32(int item, dword flags);   // readers; -1 clears
dword q30_item_flags(int item);
bool q30_item_flag(int item, dword bit);    // q30_item_flags(item) & bit, false for no item
// upstream's two item costs (counter and amount each): readers store them, and the subscreen's
// button counters read the counter of a cost that has an amount (-1 none)
void q30_set_item_cost(int item, int ctr0, int amt0, int ctr1, int amt1);  // -1 clears
int q30_item_cost_counter(int item, int which);
// rules that became item flags in 2.55: the item's flag for 2.55/3.0 quests, the rule for 2.53
bool q30_slash_flip(int itemid);            // qr_SLASHFLIPFIX
bool q30_quick_sword(int itemid);           // qr_QUICKSWORD (the sword or wand in use)
bool q30_no_diving();                       // qr_NODIVING (the flippers Link has)
bool q30_slow_charging(int itemid);         // qr_SLOWCHARGINGWALK (the sword in use)
bool q30_lens_does(int what);               // 0 qr_LENSHINTS, 1 qr_RAFTLENS, 2 qr_LENSSEESENEMIES
void q30_item_pickup_info(int item, long *pickup, int *mode, int *pstring, int *psflags);
// checkitems / the bug net: the pickup string of sprite_item (-1 none) for item, after a shop's
// string if any; true if a string was shown
bool q30_item_pickup_string(int sprite_item, int item, bool held, int shop_pstr = 0);
void q30_set_shop_string(int shop, int slot, int str);     // readers; shop -1 clears
int q30_shop_string(int shop, int slot);    // 0 for 2.53 quests
void q30_item_pickup_secrets(int pickup);   // checkitems: ipSECRETS triggers the screen's secrets
int q30_item_secret_flag();                 // Q30_IP_SECRETS for a screen item when the screen says so
bool q30_item_returns();                    // the screen's item comes back though taken (fITEMRETURN)
bool q30_below_returns(int tmp = 0);        // its special item does too (fBELOWRETURN), tmpscr[tmp]
void q30_trig_push();                       // Link is pushing: fires push triggers
bool q30_active();                          // true for 2.55+ quests
// dosubscr, while the active subscreen is open (q30_scripts_subscr.h): L and R don't move the
// selector in a 2.55/3.0 quest with the newer subscreens (upstream: only with qr_OLD_SUBSCR);
// q30_subscreen_pages runs the page buttons (btns: A 1, B 2, L 4, R 8, Ex1-4 16-128, pressed
// this frame) and moves pos to the new page's cursor when the page changes; q30_subscreen_cursor
// keeps the cursor of the page shown as the subscreen closes (open false) and, as it opens again
// on a page other than the first, puts pos back there (open true).
bool q30_subscreen_lr_are_buttons();
void q30_subscreen_pages(int btns, int &pos);
void q30_subscreen_cursor(bool open, int &pos);
bool q30_has_bridge_at(int x, int y, int layer);
bool q30_has_bridge_above(int x, int y, int target_layer);
int q30_link_mapcombo(int x, int y);        // MAPCOMBO, but 0 where a bridge covers the screen combo
bool q30_walkflag(int x, int y, int cnt);   // _walkflag for 2.55+ quests
void q30_walk_speed(int x, int y, int z, int &dx, int &dy);  // combo walk-speed modifiers
double q30_walk_speed_scale(int x, int y, int z, double step);   // same, for a fractional step
bool q30_respawn_forced();                  // a trigger fixed the drown respawn point
bool q30_link_stunned();
long q30_item_switch_family(int itemid);    // the family startwpn should treat this item as
int q30_switchhook_frame(weapon *w);        // 0 carry on, 1 the hook head is done, 2 held for a switch
bool q30_switchhook_held(const weapon *w);  // the hook, held still while a switch runs (no hits)
void q30_switchhook_update();
// The switch hook's switch (upstream's doSwitchHook): Link is held while it runs.
bool q30_switchhook_defer_cost(int itemid); // startwpn: true when the item pays at the swap instead
void q30_switchhook_tick();                 // Link.animate, each frame the hookshot holds him
bool q30_switchhook_hide_link();            // the Flicker style's off frames
int q30_switchhook_rise();                  // the Rise style's lift, in pixels
void q30_set_item_usesound2(int item, int sfx);     // readers
void q30_set_init_switch_style(int style);
void q30_set_guy_defenses(int guy, const byte *defs);  // the 41 stored slots; NULL clears (guy < 0: all)
int q30_misc_sprite(int i);                 // upstream's QMisc.sprites
void q30_set_misc_sprite(int i, int spr);
void q30_add_sprite_deco(int x, int y, int spr, int duration, bool hold_last);
int q30_pitfall_at(int x, int y);           // the pitfall combo at a point, or 0
// zc_sprite.cpp: a block put down at x,y as if pushed there; true if it landed on a block
// trigger. hole: it filled a block hole.
bool q30_switch_block_land(int x, int y, int combo, int cset, int layer, int placed_flag, int dir, bool force_many, bool *hole);
// Slashing a ->Next combo: 0 as 2.53, 1 skip the 16-31 secret flag, 2 skip every flag.
int q30_slash_next_skip(int type);
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
void q30_passive_buttons();                 // LinkClass::animate, before the item buttons
int q30_startwpn(int itemid);               // -1: not handled, 2: the bug net, else startwpn's return
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

// Screen->SecretsTriggered: whether this screen's secrets went off since it was loaded.
void q30_note_secrets(int single);          // hidden_entrance, for the screen being played
void q30_clear_secrets_note();              // a new screen is loading
bool q30_secrets_triggered();
// Game->LevelStates[] and GlobalStates[], and how many combos a trigger group holds.
dword q30_level_state(int level);
void q30_set_level_state(int level, dword value);
long q30_global_state(int state);
void q30_set_global_state(int state, long value);
int q30_trigger_group_count(int group);
// Hero->HitBy[] and the other per-frame facts about the Hero (q30_scripts_regs.h).
void q30_hero_hit_clear();
void q30_hero_hit(int kind, int index, sprite *s);
bool q30_no_active_subscreen();
int q30_hero_cset_base(int normal);
bool q30_hero_immortal();
int q30_hero_hurt_sfx();

// ---- slopes (upstream's slopes.cpp) -----------------------------------------------
// Positions and sizes are zfix raw values (1/10000 pixel). A box is a sprite's solid box;
// otx,oty is where that box was last frame. A slot names one slope: a combo position on
// a layer, or an FFC. Nothing here is used by a 2.53 quest: q30_slopes_here() is false.
bool q30_slopes_here();
void q30_slopes_update();       // once a frame, and when a screen loads: where each slope is now
// upstream's check_slope / check_new_slope: -1 the box is on top of a slope, 1 it hits one, 0 none
int q30_check_slope(int tx, int ty, int tw, int th, bool fallthrough, bool platformonly);
int q30_check_new_slope(int tx, int ty, int tw, int th, int otx, int oty, bool fallthrough, bool platformonly, int id);
int q30_get_new_slope(int tx, int ty, int tw, int th, int otx, int oty);   // nearest such slot, or -1
int q30_slope_value(int slot);  // the slot's gradient (dy/dx, zfix raw), 0 for none
// Sideview: standing on a slope that moved, or a slippery one. Sets id to the slope's gradient.
bool q30_slide_slope(int tx, int ty, int tw, int th, int &dx, int &dy, int &id);
// How far the box must move to get off the slot's line; dx or dy of -10000 holds that axis.
void q30_slope_push(int slot, int bx, int by, int bw, int bh, bool sideview, int &dx, int &dy, bool onplatform, bool fallthrough);

// ---- bottles (upstream's bottle types, bottle shops and bottle slots) -------------
// A bottle item (upstream's family 270) holds the contents of slot misc1; a bottle-fill
// item (271) fills an empty bottle with type misc1. Slot contents live in the save's
// generic bytes 128-253 (see q30_triggers.cpp), so 2.53 saves never see them.
struct Q30BottleType
{
    char name[33];
    signed char counter[3];     // -1 none
    word amount[3];             // percent of the counter's max when flags bit 0/1/2
    byte flags;                 // upstream's BTFLAG_*
    byte next_type;             // what the slot holds after drinking (0 empty)
};
struct Q30BottleShop
{
    char name[33];
    byte fill[3];
    word comb[3];
    byte cset[3];
    word price[3];
    word str[3];
};
// readers
void q30_bottles_clear();
void q30_set_bottle_type(int i, const Q30BottleType &t);
void q30_set_bottle_shop(int i, const Q30BottleShop &s);
void q30_set_init_bottle_slots(const byte *slots, int count);
void q30_set_misc_sfx(int i, int sfx);      // upstream's QMisc.miscsfx
void q30_note_room(int screen_index, int room);     // room types 2.53 has no number for
// game
bool q30_bottle_shop_here(int *shop);       // the screen Link is on is a bottle shop
const Q30BottleShop *q30_bottle_shop(int i);
int q30_bottle_slot(int slot);
void q30_set_bottle_slot(int slot, int type);
bool q30_has_bottle(int type);              // an owned bottle holds this (0: an empty one)
int q30_fill_bottle(int type);              // fills an owned empty bottle; its slot or -1
int q30_bottle_item_for_slot(int slot);     // the owned bottle item using the slot, or -1
bool q30_is_bottle(int item);
int q30_bottle_tile_offset(int item, int frames);   // tiles past the item's own for its contents
bool q30_bottle_revive();                   // Link at 0 life: drink a revive bottle; true if it saved him
std::string q30_bottle_contents_name(int item);     // a bottle item's contents, "Empty", or "" for other items
// Link's item pickup box for newer quests (upstream's itembox, off with qr_OLD_ITEM_HITBOXES):
// false when the old pickup point applies, as for every 2.53 quest.
void q30_set_item_box(int xofs, int yofs, int w, int h);   // init data reader
bool q30_item_box(bool big_hitbox, int *xofs, int *yofs, int *w, int *h);

bool q30_get_exstate(int mi, int bit);
void q30_set_exstate(int mi, int bit, bool on);
bool q30_get_xdoor(int mi, int dir, int ind);
void q30_set_xdoor(int mi, int dir, int ind, bool on);
byte &q30_pos_state(int mi, int key);

#endif

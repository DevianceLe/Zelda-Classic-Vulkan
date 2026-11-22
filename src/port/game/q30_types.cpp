// Default values of the data kept beside 2.53's structures for 2.55 and 3.0 Nightly
// quests (q30_triggers.h). Both the player and ZQuest build this file: the player for its
// quest readers and game code, ZQuest for the same readers and its own store.

#include "precompiled.h" //always first

#include <string.h>

#include "q30_triggers.h"

Q30Trigger::Q30Trigger()
{
    triggerlevel = 0;
    triggerbtn = 0;
    triggeritem = 0;
    trigtimer = 0;
    trigsfx = 0;
    trigchange = 0;
    trigprox = 0;
    trigctr = 0;
    trigctramnt = 0;
    triglbeam = 0;
    trigcschange = 0;
    spawnitem = 0;
    spawnenemy = 0;
    exstate = -1;
    exdoor_dir = -1;
    exdoor_ind = 0;
    combopos_state = 0;
    spawnip = 0;
    trigcopycat = 0;
    trigcooldown = 0;
    trig_lstate = trig_gstate = 0;
    trig_statetime = 0;
    trig_genscr = 0;
    trig_group = 0;
    trig_group_val = 0;
    trig_levelitems = 0;
    req_level_state = unreq_level_state = 0;
    trigdmlevel = -1;
    trigstatemap = trigstatescreen = 0;
    trigtint[0] = trigtint[1] = trigtint[2] = 0;
    triglvlpalette = trigbosspalette = -1;
    trigquaketime = trigwavytime = -1;
    trig_swjinxtime = trig_itmjinxtime = trig_shieldjinxtime = trig_stuntime = trig_bunnytime = -2;
    trig_pushtime = 8;
    prompt_cid = fail_prompt_cid = 0;
    prompt_cs = fail_prompt_cs = 0;
    prompt_x = 12;
    prompt_y = -8;
    trig_msgstr = fail_msgstr = 0;
    req_player_x = req_player_y = req_player_z = req_player_jump = 0;
    dest_player_x = dest_player_y = dest_player_z = player_bounce = 0;
    req_player_dir = 0;
    dest_player_dir = -1;
    force_ice_combo = -1;
    force_ice_vx = force_ice_vy = 0;
    trig_gravity = trig_terminal_v = 0;
    play_music = -2;
    set_music_refresh = -1;
    viewport_cond_range = 0;
    chance_numerator = chance_denominator = 1;
    sfx_pan = -1;
    sfx_volume = 100;
    sfx_frequency = -1;
    large_combo_copycat = 0;
}

Q30WeaponData::Q30WeaponData()
{
    flags = 0;
    moveflags = 0;
    wflags = 0;
    memset(burnsprs, 0, sizeof(burnsprs));
    memset(light_rads, 0, sizeof(light_rads));
    memset(light_offsets, 0, sizeof(light_offsets));
    glow_shape = 0;
    override_flags = 0;
    tilew = tileh = hxsz = hysz = hzsz = hxofs = hyofs = xofs = yofs = 0;
    step = 0;
    unblockable = 0;
    timeout = 0;
    imitate_weapon = default_defense = 0;
    lift_level = 0;
    lift_time = 16;
    lift_height = 80000;
    script = 0;
    memset(initd, 0, sizeof(initd));
    pierce_count = -1;
    bounce_mult = bounce_add = 0;
    viewport_suspend_range = viewport_despawn_range = 0;
    data_version = -1;
}

Q30Combo::Q30Combo()
{
    type = 0;
    flag = 0;
    walk = 0xF0;
    memset(attributes, 0, sizeof(attributes));
    usrflags = 0;
    genflags = 0;
    script = 0;
    memset(initd, 0, sizeof(initd));
    only_gentrig = 0;
    liftcmb = liftundercmb = 0;
    liftcs = liftundercs = 0;
    liftdmg = liftlvl = liftitm = liftflags = liftgfx = 0;
    liftsprite = liftsfx = 0;
    liftbreaksprite = -1;
    liftbreaksfx = 0;
    lifthei = 8;
    lifttime = 16;
    lift_parent_item = 0;
    speed_mult = speed_div = 1;
    speed_add = 0;
    sfx_appear = sfx_disappear = sfx_loop = sfx_walking = sfx_standing = sfx_tap = sfx_landing = 0;
    sfx_falling = sfx_drowning = sfx_lava_drowning = 0;
    spr_appear = spr_disappear = spr_walking = spr_standing = spr_falling = spr_drowning = spr_lava_drowning = 0;
    z_height = z_step_height = 0;
    dive_under_level = 0;
    large_combo_dirs = 0;
    delay = 0;
    cs2_flash_duration = 0;
}

// Upstream's readrules turns these on for quests saved before the rule existed (core/qst_rules.cpp:
// "if(compatrule_version < N) set_qr(...)"), so a clear bit in such a file doesn't mean the new
// behaviour was asked for: the compat rule version from which a clear bit means off, 0 for rules
// it never turns on. The player's q30_compat_rule (q30_triggers.cpp) and ZQuest's subscreen layout
// editor (zq_subscr_layout.cpp) both ask here.
dword q30_compat_rule_since(int id)
{
    switch(id)
    {
    case Q30_QR_ENEMIES_SECRET_ONLY_16_31: return 1;
    case 242: return 3;         // qr_HARDCODED_ENEMY_ANIMS
    case Q30_QR_STEPTEMP_SECRET_ONLY_16_31: return 6;
    case Q30_QR_ALLTRIG_PERMSEC_NO_TEMP: return 7;
    case Q30_QR_DMAP_0_CONTINUE_BUG: return 24;
    case 344: return 30;        // qr_DECO_2_YOFFSET
    case 348: return 31;        // qr_FFCPRELOAD_BUGGED_LOAD
    case Q30_QR_PUSHBLOCK_SPRITE_LAYER: return 51;
    case Q30_QR_BROKEN_PUSHBLOCK_TOP_HALF_SOLIDS: return 71;
    case Q30_QR_BROKEN_PUSHBLOCK_FLAG_CLONING: return 72;
    case 433: return 61;        // qr_BROKEN_BOMB_AMMO_COSTS
    case 450: return 79;        // qr_BROKEN_SYSTEM_COLORS
    case 487: return 91;        // qr_OLD_SCRIPTS_INTERNAL_ARRAYS_BOUND_INDEX
    case 488: return 91;        // qr_OLD_SCRIPTS_ARRAYS_NON_ZERO_DEFAULT_VALUE
    case 561: return 58;        // qr_OLD_DMAP_INTRO_STRINGS
    case 570: return 103;       // qr_OLD_ITEM_HITBOXES
    default: return 0;
    }
}

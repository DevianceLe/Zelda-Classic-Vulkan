// 2.55/3.0 weapon behaviour from the weapon data that 2.53's weapons have no room for (upstream
// weapons.cpp): the timeout, piercing, the burning sprites, lifting a weapon with the glove.
// Kept beside the sprite (Zasm30SpriteState, the live weapon fields); hooks from weapons.cpp,
// guys.cpp, link.cpp and q30_triggers.cpp. Included once, after q30_scripts_live.h.

#ifndef ZC_Q30_WEAPON_FX_H
#define ZC_Q30_WEAPON_FX_H

int q30_wpn_trigger_burn(long uid);     // q30_triggers.cpp: the fire levels combo triggers lit the weapon with

enum { Z30WF_UPDATE_IGNITE_SPRITE = 1 << 9 };
static const long Z30WF_LAST_BURN = 1 + 40;     // live field: the burn state the sprite is for (upstream's last_burnstate)

// upstream weapon::animate: if(weap_timeout > 0 && !--weap_timeout) kill_weapon_special()
bool q30_weapon_timeout_tick(weapon *w)
{
    Zasm30SpriteState *st = zasm30_sprite_state_if(w->getUID());
    return st && st->weap_timeout > 0 && !--st->weap_timeout;
}

void q30_weapon_timeout_clear(weapon *w)
{
    Zasm30SpriteState *st = zasm30_sprite_state_if(w->getUID());

    if(st)
        st->weap_timeout = 0;
}

// The weapon's PierceCount (-1: the old way, it hits; 0: pierces forever); set >= -1 writes it.
int q30_weapon_pierce(weapon *w, int set)
{
    if(set >= -1)
        z30_lf_set(w->getUID(), 1 + Z30W_PIERCE, 0, set);

    return (int)z30_lf_get(w->getUID(), 1 + Z30W_PIERCE, 0, -1);
}

// An lweapon's LiftLevel, LiftTime and LiftHeight (upstream's lift_level, lift_time, lift_height)
int q30_weapon_lift_data(weapon *w, int *time, long *height)
{
    const long uid = w->getUID();
    *time = (int)(zasm30_store_get(Z30R_LWPNLIFTTIME, uid, 0) / 10000);
    *height = zasm30_store_get(Z30R_LWPNLIFTHEIGHT, uid, 0);
    return (int)(zasm30_store_get(Z30R_LWPNLIFTLEVEL, uid, 0) / 10000);
}

// the fire levels a weapon's flags (WFLAG_BURN_*: its weapon data, or a script) burn with
int q30_weapon_data_burn(long uid)
{
    const Zasm30SpriteState *st = zasm30_sprite_state_if(uid);
    return st ? (int)((st->weapon_flags >> 3) & 0xF) : 0;
}

// upstream's blast under qr_BOMB_BOOMS_CLEAR_SCRIPTS: scrconfig.script = 0, FFCore.destroySprite
void q30_weapon_clear_script(weapon *w)
{
    Zasm30SpriteState *st = zasm30_sprite_state_if(w->getUID());

    if(!st || !st->script)
        return;

    st->script = 0;
    zasm30_drop_instance(SCRIPT_LWPN, w->getUID());
    zasm30_sprite_released(w->getUID());
}

void q30_weapon_clear_lift(weapon *w)
{
    zasm30_store_set(Z30R_LWPNLIFTLEVEL, w->getUID(), 0, 0);
}

// upstream weapon::get_burnstate: the strongest fire the weapon burns with (its Flags[] from the
// weapon data or a script, and what combo triggers lit it with)
static int z30_wpn_burnstate(weapon *w, const Zasm30SpriteState &st)
{
    const int lv = (int)((st.weapon_flags >> 3) & 0xF) | q30_wpn_trigger_burn(w->getUID());
    return (lv & 8) ? 4 : (lv & 4) ? 3 : (lv & 2) ? 2 : (lv & 1) ? 1 : 0;
}

// upstream's _handle_loadsprite(misc_wsprites[burnstate]) and the glow that goes with it
static void z30_wpn_burn_load(weapon *w, Zasm30SpriteState &st, int state)
{
    const long uid = w->getUID();
    z30_lf_set(uid, Z30WF_LAST_BURN, 0, state);
    w->LOADGFX((int)z30_lf_get(uid, 1 + Z30W_SPRITES, state, 0));

    if((w->id == wArrow || w->id == ewArrow) && (w->dir == left || w->dir == right))
    {
        w->update_weapon_frame(((w->frames > 1) ? w->frames : 1), w->o_tile);

        if(!q30_quest_rule(236))    // qr_BROKEN_HORIZONTAL_WEAPON_ANIM
            w->o_tile = w->tile;
    }

    st.glow_rad = z30_lf_get(uid, 1 + Z30W_LRAD, state, 0);
    st.glow_ofs = z30_lf_get(uid, 1 + Z30W_LOFS, state, 0);
}

// The rest of upstream's weapon::load_weap_data, for an lweapon from its item, an eweapon from
// its enemy, or a script's weapon data: pierce, bounce, lifting, and the burning sprites
// (WFLAG_UPDATE_IGNITE_SPRITE: each fire state's sprite and light, the one it burns with now).
void q30_weapon_seed_data(weapon *w, const Q30WeaponData &d, bool lw)
{
    const long uid = w->getUID();
    z30_lf_set(uid, 1 + Z30W_PIERCE, 0, d.pierce_count);
    z30_lf_set(uid, 1 + Z30W_BMULT, 0, d.bounce_mult);
    z30_lf_set(uid, 1 + Z30W_BADD, 0, d.bounce_add);

    if(lw)
    {
        zasm30_store_set(Z30R_LWPNLIFTLEVEL, uid, 0, d.lift_level * 10000);
        zasm30_store_set(Z30R_LWPNLIFTTIME, uid, 0, d.lift_time * 10000);
        zasm30_store_set(Z30R_LWPNLIFTHEIGHT, uid, 0, d.lift_height);
    }
    else
    {
        z30_lf_set(uid, 1 + Z30W_LIFTL, 0, d.lift_level);
        z30_lf_set(uid, 1 + Z30W_LIFTT, 0, d.lift_time);
        z30_lf_set(uid, 1 + Z30W_LIFTH, 0, d.lift_height);
    }

    Zasm30SpriteState &st = zasm30_sprite_state(uid);
    st.weapon_flags = d.wflags;

    switch(w->id)
    {
    case wSword: case wWand: case wHSHandle: case wHSChain: case wSSparkle: case wFSparkle: case wWhistle:
        return;     // (unsupported there)
    }

    if(!(d.wflags & Z30WF_UPDATE_IGNITE_SPRITE))
    {
        if(d.flags & Z30D_WD_GLOW_RAD)
        {
            z30_lf_set(uid, 1 + Z30W_LRAD, 0, d.light_rads[0]);
            z30_lf_set(uid, 1 + Z30W_LOFS, 0, d.light_offsets[0]);
        }

        return;
    }

    for(int q = 0; q < 5; ++q)
    {
        z30_lf_set(uid, 1 + Z30W_SPRITES, q, d.burnsprs[q]);
        z30_lf_set(uid, 1 + Z30W_LRAD, q, d.light_rads[q]);
        z30_lf_set(uid, 1 + Z30W_LOFS, q, d.light_offsets[q]);
    }

    z30_wpn_burn_load(w, st, z30_wpn_burnstate(w, st));
}

// upstream weapon::animate's WFLAG_UPDATE_IGNITE_SPRITE: the sprite follows the fire it burns with
void q30_weapon_burn_frame(weapon *w)
{
    Zasm30SpriteState *st = zasm30_sprite_state_if(w->getUID());

    if(!st || !(st->weapon_flags & Z30WF_UPDATE_IGNITE_SPRITE))
        return;

    const int state = z30_wpn_burnstate(w, *st);

    if(z30_lf_get(w->getUID(), Z30WF_LAST_BURN, 0, 0) != state)
        z30_wpn_burn_load(w, *st, state);
}

#endif

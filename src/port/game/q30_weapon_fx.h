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

// upstream's weapon::useweapon: the weapon type an lweapon acts as for collisions and triggers
// (its weapon data's "imitate weapon", or a script's lweapon->Weapon); 0 for none
int q30_weapon_imitates(weapon *w)
{
    if(!w || !q30_active())
        return 0;

    const long v = zasm30_store_get(Z30R_LWPNUSEWEAPON, w->getUID(), 0) / 10000;
    return (v > 0 && v < 256) ? (int)v : 0;
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

// ---- height, gravity and landing (upstream weapon::animate's "fall down", sprite::handle_termv) ----

enum { Z30WF_BREAK_WHEN_LANDING = 1 << 1, Z30WF_BREAK_ON_SOLID = 1 << 2, Z30WF_STOP_WHEN_LANDING = 1 << 7,
       Z30WF_STOP_WHEN_HIT_SOLID = 1 << 8, Z30WF_NO_COLL_WHEN_STILL = 1 << 10, Z30WF_TEMP_IGNORE_SOLID = 1 << 11,
       Z30WF_OBEY_CONVEYORS = 1 << 12 };
enum { Z30MV_OBEYS_GRAV = 1, Z30MV_CAN_PITFALL = 2, Z30MV_NO_FAKE_Z = 1 << 8, Z30MV_NO_REAL_Z = 1 << 9 };

// upstream's weapon constructor: the weapons that fall unless their weapon data says otherwise
// (an enemy's flame takes its enemy's data, which q30_enemy_weapon_made puts over this)
void q30_weapon_default_moveflags(weapon *w)
{
    switch(w->id)
    {
    case ewFlame: case ewFireTrail: case wFire: case wLitBomb: case wLitSBomb: case wBait: case Q30W_THROWN:
        zasm30_sprite_state(w->getUID()).move_flags |= Z30MV_OBEYS_GRAV | Z30MV_CAN_PITFALL;
        break;
    }
}

// the fix fields as upstream's zfix values (1/10000ths; a 16.16 fix holds each such value exactly)
static long z30_wz(fix f)
{
    const double d = double(f) * 10000.0;
    return (long)(d < 0 ? d - 0.5 : d + 0.5);
}

static fix z30_wfix(long v) { return fix(v / 10000.0); }

// zfix's -(fall * bounce_mult + bounce_add * 100), the speed it bounces back up with
static long z30_wbounce(long fall, long uid)
{
    const long mult = z30_lf_get(uid, 1 + Z30W_BMULT, 0, 0), add = z30_lf_get(uid, 1 + Z30W_BADD, 0, 0);
    return -(long)(int64_t(fall) * mult / 10000 + int64_t(add) * 100);
}

// upstream's get_grav_fall / get_terminalv_fall: the sprite's own (a script's), else the screen's
static long z30_wgrav(const Zasm30SpriteState &st) { return st.gravity_strength ? st.gravity_strength / 100 * 10000 : (long)q30_link_gravity() * 10000; }
static long z30_wtermv(const Zasm30SpriteState &st) { return st.terminal_v ? st.terminal_v / 100 * 10000 : (long)q30_link_terminalv() * 10000; }

static void z30_wtermv_clamp(weapon *w, Zasm30SpriteState &st)
{
    if(q30_quest_rule(463))     // qr_OLD_TERMINAL_VELOCITY
        return;

    const long termv = z30_wtermv(st);

    if(z30_wz(w->fall) > termv)
        w->fall = z30_wfix(termv);

    if(st.fakefall > termv)
        st.fakefall = termv;
}

// WFLAG_BREAK_WHEN_LANDING / WFLAG_STOP_WHEN_LANDING: it hits what it lands on first (upstream's
// collision_check; the combo triggers it touches run at the end of its frame as ever)
void q30_weapon_collision_check(weapon *w);     // guys.cpp

static void z30_wland(weapon *w, const Zasm30SpriteState &st)
{
    if(st.weapon_flags & Z30WF_BREAK_WHEN_LANDING)
    {
        q30_weapon_collision_check(w);
        w->q30_kill_special();
    }

    if(st.weapon_flags & Z30WF_STOP_WHEN_LANDING)
    {
        q30_weapon_collision_check(w);
        w->step = 0;
    }
}

// one 3.0 height (Z or FakeZ): falls by its speed, lands at 0, bounces while the bounce goes up
static void z30_wfall_axis(weapon *w, Zasm30SpriteState &st, long &z, long &fall, bool real = false)
{
    const bool didfall = z > 0;
    z -= fall / 100;

    if(z <= 0)
    {
        z = 0;
        const long bf = z30_wbounce(fall, w->getUID());

        if(didfall && bf < 0)
            fall = bf;
        else
        {
            fall = 0;

            if(didfall)
            {
                if(real)    // (it is on the ground as it hits what it lands on)
                {
                    w->z = z30_wfix(z);
                    w->fall = z30_wfix(fall);
                }

                z30_wland(w, st);
            }
        }
    }
    else if(fall <= z30_wtermv(st))
        fall += z30_wgrav(st);
}

// upstream sprite::on_sideview_solid for a weapon (no sideview platforms)
static bool z30_wsv_solid(weapon *w)
{
    const int hw = w->hxsz;
    const int lx = int(w->x) + w->hxofs;      // (zfix's rounding, as fix's)
    const int by = int(w->y) + w->hyofs + w->hysz;

    for(int xo = zc_min(4, hw / 2); xo < hw; xo += 8)
        if(_walkflag(lx + xo, by, 1))
            return true;

    if(_walkflag(lx + hw - 1, by, 1))
        return true;

    return by >= 176 && currscr >= 0x70 && !(tmpscr->flags2 & wfDOWN);
}

// upstream weapon::animate "fall down" for 2.55/3.0 quests: what the move flags say (gravity on Z
// and FakeZ, or sideview's), the bounce, and the landing flags
void q30_weapon_fall(weapon *w)
{
    if(w->id == Q30W_THROWN)    // (a thrown object falls its own way: q30_triggers.cpp)
        return;

    Zasm30SpriteState *pst = zasm30_sprite_state_if(w->getUID());

    if(!pst)
        return;

    Zasm30SpriteState &st = *pst;
    z30_wtermv_clamp(w, st);

    if(st.weapon_flags & Z30WF_OBEY_CONVEYORS)  // (upstream weapon::check_conveyor: flag 1, "stunned while moving", stops it)
    {
        const int cid = q30_sprite_conveyor(w, false, (dword)st.move_flags, st.fakez);
        const Q30Combo *c = cid >= 0 ? q30_combo(cid) : NULL;

        if(c && (c->usrflags & 0x1))
            w->step = 0;
    }

    if(st.move_flags & Z30MV_OBEYS_GRAV)
    {
        if(tmpscr->flags7 & fSIDEVIEW)
        {
            const bool hardcoded = q30_quest_rule(579);     // qr_WEAPONS_HARDCODED_SIDEVIEW_HITBOX
            const bool on_solid = hardcoded ? _walkflag(int(w->x), int(w->y) + 16, 0) : z30_wsv_solid(w);
            long fall = z30_wz(w->fall);

            if(q30_quest_rule(563) ? !on_solid : ((fall >= 0 && !on_solid) || fall < 0))    // qr_BROKEN_SIDEVIEW_SPRITE_JUMP
            {
                w->y = z30_wfix(z30_wz(w->y) + fall / 100);

                if(fall <= z30_wtermv(st))
                    fall += z30_wgrav(st);
            }
            else
            {
                if(fall != 0 && !(w->step > 0 && w->dir == up))    // (not while it still moves up through it)
                {
                    const int fy = int(floor(double(w->y)));
                    w->y = fix(fy - (hardcoded ? fy : fy + w->hyofs + w->hysz) % 8);
                }

                const long bf = z30_wbounce(fall, w->getUID());

                if(bf < 0)
                    fall = bf;
                else
                {
                    fall = 0;
                    z30_wland(w, st);
                }
            }

            w->fall = z30_wfix(fall);

            if(w->y > 192)
                w->dead = 0;    // (out of bounds)
        }
        else
        {
            if(!(st.move_flags & Z30MV_NO_FAKE_Z))
                z30_wfall_axis(w, st, st.fakez, st.fakefall);

            if(!(st.move_flags & Z30MV_NO_REAL_Z))
            {
                long z = z30_wz(w->z), fall = z30_wz(w->fall);
                z30_wfall_axis(w, st, z, fall, true);
w->z = z30_wfix(z);
                w->fall = z30_wfix(fall);
            }
        }
    }

    z30_wtermv_clamp(w, st);
}

// ---- falling into pits and drowning (upstream weapon::animate, sprite::check_pits / check_water) ----

enum { Z30MV_CAN_WATERDROWN = 1 << 3 };
static const long Z30WF_FALL_DYING = 1 + 41;    // live field: the fall's extra death frame has begun

int q30_water_at(int x, int y);     // link.cpp: upstream's iswaterex for a sprite (not Link), 0 for none
extern int q255_header_version[3];

static int z30_wfall_at(int x, int y, bool water) { return water ? q30_water_at(x, y) : q30_pitfall_at(x, y); }

// qr_OLD_SPRITE_FALL_DROWN (462; quests before 2.55.11 as upstream's loader sets it)
static bool z30_wfall_old_rule()
{
    return q30_quest_rule(462) || (q255_header_version[0] == 2 && q255_header_version[1] < 55)
           || (q255_header_version[0] == 2 && q255_header_version[1] == 55 && q255_header_version[2] < 11);
}

// the old check: a weapon partly over a pit (water) is drawn in a pixel at a time towards it,
// falling once all its corners are over it (or after 15 pulls); returns the combo, 0 for none
static int z30_wfall_old(weapon *w, bool water)
{
    bool fallen = false;
    int ul = 0, bl = 0, ur = 0, br = 0, ul5 = 0, bl5 = 0, ur5 = 0, br5 = 0;

    for(int safe = 1; safe < 16; ++safe)
    {
        const int x = int(w->x), y = int(w->y);
        ul = z30_wfall_at(x, y, water); bl = z30_wfall_at(x, y+15, water);
        ur = z30_wfall_at(x+15, y, water); br = z30_wfall_at(x+15, y+15, water);

        if(water)   // (upstream's check_water takes its middle points crosswise)
        {
            ul5 = z30_wfall_at(x+8, y+8, true); bl5 = z30_wfall_at(x+8, y+7, true);
            ur5 = z30_wfall_at(x+7, y+8, true); br5 = z30_wfall_at(x+7, y+7, true);
        }
        else
        {
            ul5 = z30_wfall_at(x+7, y+7, false); bl5 = z30_wfall_at(x+7, y+8, false);
            ur5 = z30_wfall_at(x+8, y+7, false); br5 = z30_wfall_at(x+8, y+8, false);
        }

        int d = -1;

        switch((ul?1:0) + (ur?1:0) + (bl?1:0) + (br?1:0))
        {
        case 4:
            return ul5 ? ul5 : ul;

        case 3:
            if(ul && ur && bl) { if(ul5) d = l_up; }
            else if(ul && ur && br) { if(ur5) d = r_up; }
            else if(ul && bl && br) { if(bl5) d = l_down; }
            else if(bl && ur && br) { if(br5) d = r_down; }
            break;

        case 2:
            if(ul && ur && (ul5 || ur5)) d = (ul5 && ur5) ? up : ul5 ? l_up : r_up;
            else if(bl && br && (bl5 || br5)) d = (bl5 && br5) ? down : bl5 ? l_down : r_down;
            else if(ul && bl && (ul5 || bl5)) d = (ul5 && bl5) ? left : ul5 ? l_up : l_down;
            else if(ur && br && (ur5 || br5)) d = (ur5 && br5) ? right : ur5 ? r_up : r_down;
            break;

        case 1:
            if(ul && ul5) d = l_up;
            else if(ur && ur5) d = r_up;
            else if(bl && bl5) d = l_down;
            else if(br && br5) d = r_down;
            break;
        }

        if(d == -1)
            return 0;

        fallen = true;

        if(d == l_up || d == l_down || d == left) w->x -= 1;
        else if(d == r_up || d == r_down || d == right) w->x += 1;

        if(d == l_up || d == r_up || d == up) w->y -= 1;
        else if(d == l_down || d == r_down || d == down) w->y += 1;
    }

    if(!fallen)
        return 0;

    return ul5 ? ul5 : ur5 ? ur5 : bl5 ? bl5 : br5 ? br5 : ul ? ul : ur ? ur : bl ? bl : br;
}

// the newer check: every point of the hit box, 2 pixels in from its edges (its lower half in
// sideview), over a pit (water)
static int z30_wfall_new(weapon *w, bool water)
{
    const int endx = zc_max(w->hxsz - 2, 1), endy = zc_max(w->hysz - 2, 1);
    const int startx = zc_min(2, endx - 1);
    const int starty = (tmpscr->flags7 & fSIDEVIEW) ? zc_min(endy / 2 + 2, endy - 1) : zc_min(2, endy - 1);
    int ret = 0;

    for(int dx = startx; dx < endx; ++dx)
        for(int dy = starty; dy < endy; ++dy)
            if(!(ret = z30_wfall_at(int(w->x) + dx + w->hxofs, int(w->y) + dy + w->hyofs, water)))
                return 0;

    return ret;
}

// after its fall: a weapon on the ground whose move flags let it falls into the pit (drowns in
// the water) under it; the melee weapons and sparkles never do
void q30_weapon_pits(weapon *w)
{
    Zasm30SpriteState *st = zasm30_sprite_state_if(w->getUID());

    if(!st || !(st->move_flags & (Z30MV_CAN_PITFALL | Z30MV_CAN_WATERDROWN)) || st->fall_clk || st->drown_clk)
        return;

    switch(w->id)
    {
    case wSword: case wWand: case wCByrna: case wHammer: case wHookshot: case wWhistle:
    case wFSparkle: case wHSChain: case wHSHandle: case wSSparkle: case wStomp: case wSmack:
        return;
    }

    if(w->z > 0 || st->fakez > 0)
        return;

    const bool old = z30_wfall_old_rule();

    if(st->move_flags & Z30MV_CAN_PITFALL)
        if(const int pit = old ? z30_wfall_old(w, false) : z30_wfall_new(w, false))
        {
            st->fall_clk = 70;      // PITFALL_FALL_FRAMES
            w->o_cset = w->cs;
            z30_lf_set(w->getUID(), 1 + Z30W_FALLCMB, 0, pit);
        }

    if(st->move_flags & Z30MV_CAN_WATERDROWN)
        if(const int wat = old ? z30_wfall_old(w, true) : z30_wfall_new(w, true))
        {
            st->drown_clk = 64;     // WATER_DROWN_FRAMES
            w->o_cset = w->cs;
            z30_lf_set(w->getUID(), 1 + Z30W_DROWNCMB, 0, wat);
        }
}

// upstream weapon::animate while it falls or drowns: 0 not falling; 1 it falls on, drawn with the
// quest's fall (drown, lava drown) sprite, touching nothing; 2 it is gone, with no death effects
// (qr_WEAPONS_EXTRA_DEATH_FRAME: one more frame first, not drawn)
int q30_weapon_falling(weapon *w)
{
    Zasm30SpriteState *st = zasm30_sprite_state_if(w->getUID());

    if(!st || (st->fall_clk <= 0 && st->drown_clk <= 0))
        return 0;

    const long uid = w->getUID();
    const bool pit = st->fall_clk > 0;
    long &clk = pit ? st->fall_clk : st->drown_clk;
    const int frames = pit ? 70 : 64;
    const int cmb = (int)z30_lf_get(uid, 1 + (pit ? Z30W_FALLCMB : Z30W_DROWNCMB), 0, 0);

    if(pit && clk == frames && cmb)
    {
        const Q30Combo *c = q30_combo(cmb);
        const int s = c ? int(c->attributes[8] / 10000) : 0;

        if(s > 0)
            sfx(s, pan(int(w->x)));
    }

    if(!--clk)
    {
        if(!z30_lf_get(uid, Z30WF_FALL_DYING, 0, 0) && q30_quest_rule(Q30_QR_WEAPONS_EXTRA_DEATH_FRAME)
           && w->id != wSword && w->id != wBrang)
        {
            w->dead = 0;
            z30_lf_set(uid, Z30WF_FALL_DYING, 0, 1);
            ++clk;
            return 1;
        }

        return 2;
    }

    const Q30Combo *c = (!pit && cmb) ? q30_combo(cmb) : NULL;
    const int spr = q30_misc_sprite(pit ? 0 : (c && (c->usrflags & 0x1)) ? 2 : 1);     // sprFALL, sprDROWN, sprLAVADROWN

    if(spr >= 0 && spr < MAXWPNS)
    {
        const int fr = wpnsbuf[spr].frames ? wpnsbuf[spr].frames : 1, spd = wpnsbuf[spr].speed ? wpnsbuf[spr].speed : 1;
        w->cs = wpnsbuf[spr].csets & 0xF;
        w->tile = wpnsbuf[spr].tile + zc_min((frames - (int)clk) / spd, fr - 1);
    }

    return 1;
}

// 1 while it falls (drawn as a plain sprite), 2 in the fall's extra death frame (not drawn)
int q30_weapon_fall_draw(weapon *w)
{
    const Zasm30SpriteState *st = zasm30_sprite_state_if(w->getUID());

    if(!st || (st->fall_clk <= 0 && st->drown_clk <= 0))
        return 0;

    return z30_lf_get(w->getUID(), Z30WF_FALL_DYING, 0, 0) ? 2 : 1;
}

// upstream weapon::collide_solid: a solid combo anywhere under the weapon's hitbox
static bool z30_wcollide_solid(weapon *w)
{
    if(q30_quest_rule(457))     // qr_IMPRECISE_WEAPON_SOLIDITY_CHECKS
        return _walkflag(int(w->x), int(w->y), 2) || _walkflag(int(w->x), int(w->y) + 8, 2);

    const int x1 = int(w->x) + w->hxofs, x2 = x1 + w->hxsz;      // (zfix's rounding, as fix's)
    const int y1 = int(w->y) + w->hyofs, y2 = y1 + w->hysz;
    const int incr = 8;

    for(int tx = x1; tx < x2;)
    {
        for(int ty = y1; ty < y2;)
        {
            if(_walkflag(tx, ty, 1))
                return true;

            if(ty % incr) ty += incr - (ty % incr);     // (the next half-grid, and the far edge)
            else if(ty < y2 - 1 && ty + incr >= y2) ty = y2 - 1;
            else ty += incr;
        }

        if(tx % incr) tx += incr - (tx % incr);
        else if(tx < x2 - 1 && tx + incr >= x2) tx = x2 - 1;
        else tx += incr;
    }

    return false;
}

// upstream weapon::animate: WFLAG_BREAK_ON_SOLID / WFLAG_STOP_WHEN_HIT_SOLID as the weapon touches
// a solid (WFLAG_TEMP_IGNORE_SOLID: not until it has left the solids it is in)
void q30_weapon_solid(weapon *w)
{
    Zasm30SpriteState *st = w->id == Q30W_THROWN ? NULL : zasm30_sprite_state_if(w->getUID());   // (a thrown object breaks its own way)

    if(!st || w->dead != -1 || !(st->weapon_flags & (Z30WF_BREAK_ON_SOLID | Z30WF_STOP_WHEN_HIT_SOLID | Z30WF_TEMP_IGNORE_SOLID)))
        return;

    if(z30_wcollide_solid(w))
    {
        if(!(st->weapon_flags & Z30WF_TEMP_IGNORE_SOLID))
        {
            if(st->weapon_flags & Z30WF_BREAK_ON_SOLID)
                w->q30_kill_special();

            if(st->weapon_flags & Z30WF_STOP_WHEN_HIT_SOLID)
                w->step = 0;
        }
    }
    else
        st->weapon_flags &= ~(long)Z30WF_TEMP_IGNORE_SOLID;
}

// a still weapon with WFLAG_NO_COLL_WHEN_STILL touches nothing (upstream weapon::no_collision)
// (the glove lifts it all the same: q30_weapon_still_hits while it looks)
bool q30_weapon_still_hits = false;

bool q30_weapon_no_collision(weapon *w)
{
    const Zasm30SpriteState *st = zasm30_sprite_state_if(w->getUID());

    if(st && (st->fall_clk > 0 || st->drown_clk > 0))     // (falling into a pit or drowning)
        return true;

    return st && !q30_weapon_still_hits && w->step == 0 && (st->weapon_flags & Z30WF_NO_COLL_WHEN_STILL);
}

// A lit bomb set off (how 0, kill_weapon_special) no longer breaks on landing or solids; one going
// off (1) also touches things while still; as it goes off and its blast ends (1, 2) it stops
// falling unless qr_BROKEN_MOVING_BOMBS (upstream weapon::animate's bomb case)
void q30_weapon_blast_flags(weapon *w, int how)
{
    Zasm30SpriteState *st = zasm30_sprite_state_if(w->getUID());

    if(!st)
        return;

    if(how < 2)
        st->weapon_flags &= ~(long)(Z30WF_BREAK_ON_SOLID | Z30WF_BREAK_WHEN_LANDING | (how == 1 ? Z30WF_NO_COLL_WHEN_STILL : 0));

    if(how > 0 && !q30_quest_rule(410))
        st->move_flags &= ~(long)Z30MV_OBEYS_GRAV;
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

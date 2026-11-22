// Script registers and commands of things alive in play, for 2.55/3.0 quests: live enemies
// (npc->), weapons (lweapon->, eweapon->), the Hero, Game-> globals and the ownership of script
// objects (sprite->OwnBitmap() and friends). Ported from upstream 3.0 (ffscript.cpp and
// scripting/types/{npc,lweapon,eweapon,hero,game}.cpp), on the sprite and LinkClass members this
// engine has. Included by ffscript.cpp after q30_scripts_cmds.h; nothing here runs for a 2.53
// quest (its scripts never use these numbers).

#ifndef ZC_Q30_SCRIPTS_LIVE_H
#define ZC_Q30_SCRIPTS_LIVE_H

#include "particles.h"
#include "tiles.h"

int q30_pit_combo();                    // q30_triggers.cpp: the pit Link fell in (upstream's fallCombo)
void q30_set_pit_combo(int cid);
void q30_set_item_slot(int itm, int slot, int force);  // link.cpp: Hero->SetItemSlot()

// ---- state upstream's sprites and game keep that 2.53's have no room for --------------------------

static bool zasm30_skip_credits = false;            // Game->SkipCredits (upstream's skip_ending_credits)
static long zasm30_air_drag = 100;                  // Game->Gravity[3] (zinit.air_drag, 0.01 as upstream's default)
// Game->Gravity[0] as written: upstream's zinit.gravity is a long in 1/10000ths, 2.53's a byte in
// 1/100ths (what the physics here use); the script reads back what it wrote while that still holds
static long zasm30_gravity_written = 0;
static bool zasm30_gravity_set = false;
static std::map<long, long> zasm30_ewpn_parent;     // eweapon uid -> parent sprite uid (upstream's sprite::parent)
struct Zasm30ScriptKb { int clk, speed; };          // a script's npc->Knockback() (script_knockback_clk/_speed)
static std::map<long, Zasm30ScriptKb> zasm30_script_kb;
// Objects a sprite owns (sprite->OwnBitmap(), OwnArray() ...): the value a script holds -> the sprite's uid
static std::map<long, long> zasm30_owned;
static BITMAP *zasm30_custom_cursor = NULL;         // Game->SetCustomCursor(): kept (no pointer is shown in play here)
static int zasm30_custom_cursor_fx = 0, zasm30_custom_cursor_fy = 0;

bool q30_skip_credits() { return zasm30_ffscript_version && zasm30_skip_credits; }
void q30_clear_skip_credits() { zasm30_skip_credits = false; }

static void zasm30_live_reset()
{
    zasm30_skip_credits = false;
    zasm30_air_drag = 100;
    zasm30_gravity_set = false;
    zasm30_ewpn_parent.clear();
    zasm30_script_kb.clear();
    zasm30_owned.clear();
    q30_set_pit_combo(0);       // (upstream's HeroClass::init)

    if(zasm30_custom_cursor)
        destroy_bitmap(zasm30_custom_cursor);

    zasm30_custom_cursor = NULL;
}

// ---- sprite->Explode() (upstream's sprite::explode and HeroClass::explode) ------------------------

static void z30_explode(sprite *s, int type, bool hero)
{
    // Scripts can set any tile. Like drawing it, exploding one that doesn't exist does nothing.
    if((unsigned)s->tile >= (unsigned)NEWMAXTILES)
        return;

    byte buf[256];
    unpack_tile(newtilebuf, s->tile, s->flip, true);
    memcpy(buf, unpackbuf, 256);

    const Zasm30SpriteState *st = hero ? NULL : zasm30_sprite_state_if(s->getUID());
    const int fakez = st ? (int)(st->fakez / 10000) : 0;
    const int bx = int(s->x), by = int(s->y) - int(s->z) - fakez;
    // (upstream's layer 6, over everything: this engine's particles go up to layer 5)
    const int layer = 5;

    for(int i = 0; i < 16; ++i)
    {
        for(int j = 0; j < 16; ++j)
        {
            if(!buf[i * 16 + j])
                continue;

            particle *p;

            if(type == 0)       // Twilight
            {
                particles.add(new pTwilight(fix(bx + j), fix(by + i), layer, 0, 0, (rand() % 8) + i * 4));
                p = (particle *)particles.spr(particles.Count() - 1);
                p->step = 3;

                if(!hero)
                {
                    p->cset = s->cs;
                    p->color = rand() % 4 + 1;
                }
            }
            else if(type == 1)  // Sands of Hours
            {
                particles.add(new pTwilight(fix(bx + j), fix(by + i), layer, 1, 2, (rand() % 16) + i * 2));
                p = (particle *)particles.spr(particles.Count() - 1);
                p->step = 4;

                if(rand() % 10 < 2)
                {
                    p->color = hero ? 1 : rand() % 4 + 1;
                    p->cset = hero ? 0 : s->cs;
                }
            }
            else                // explode
            {
                particles.add(new pFaroresWindDust(fix(bx + j), fix(by + i), layer, 6, buf[i * 16 + j], rand() % 96));
                p = (particle *)particles.spr(particles.Count() - 1);
                p->angular = true;
                p->angle = rand();
                p->step = (((double)j) / 8);
                p->yofs = s->yofs;

                if(!hero)
                {
                    p->color = rand() % 4 + 1;
                    p->cset = s->cs;
                }
            }
        }
    }
}

static void z30_explode_command(sprite *s, const char *what, bool hero)
{
    const long mode = get_register(sarg1) / 10000;

    if((unsigned long)mode > 2)
        Z_scripterrlog("Invalid mode (%ld) passed to %s->Explode(int32_t mode)\n", mode, what);
    else if(s)
        z30_explode(s, (int)mode, hero);
}

// ---- npc-> movement (upstream's do_npc_* in scripting/types/npc.cpp) ------------------------------

// The enemy members these need are protected.
class q30_live_enemy_access : public enemy
{
public:
    void var_walk(int r, int h, int s) { variable_walk(r, h, s); }
    void var_walk8(int r, int h, int c, int s) { variable_walk_8(r, h, c, s); }
    void var_walk8(int r, int h, int c, int s, int a, int b, int d, int e) { variable_walk_8(r, h, c, s, a, b, d, e); }
    void halt_walk(int r, int h, int s, int hr, int hc) { halting_walk(r, h, s, hr, hc); }
    void halt_walk8(int r, int h, int c, int s, int hr, int hc) { halting_walk_8(r, h, c, s, hr, hc); }
    void float_walk(int r, int c, fix s) { floater_walk(r, c, s); }
    void float_walk(int r, int c, fix ms, fix ss, int s, int p, int g) { floater_walk(r, c, ms, ss, s, p, g); }
    void fire_breath(bool seek) { FireBreath(seek); }
    bool can_move(int d) { return canmove(d); }
    bool can_move(int d, int special) { return canmove(d, special); }
    bool can_move(int d, fix s, int special) { return canmove(d, s, special); }
    bool can_move(int d, fix s, int special, int a, int b, int c, int e) { return canmove(d, s, special, a, b, c, e); }
    bool walkflag(int dx, int dy, int special) { return m_walkflag(dx, dy, special, -1000, -1000); }
    int get_sclk() { return sclk; }
    void set_sclk(int v) { sclk = v; }
};

static q30_live_enemy_access *z30_live_npc(const char *what)
{
    if(GuyH::loadNPC(ri->guyref, what) != SH::_NoError)
        return NULL;

    return (q30_live_enemy_access *)GuyH::getNPC();
}

// The array a movement command takes, or NULL (upstream's ArrayManager am(arrayptr); am.invalid()).
static ZScriptArray *z30_live_array(long ptr)
{
    ZScriptArray &a = ArrayH::getArray(ptr / 10000);
    return &a == &INVALIDARRAY ? NULL : &a;
}

// upstream's enemy::scr_canplace on this engine's walkability: every 8 pixels of the hit box
// (with the size given, -1 for its own) and its far edges
static bool z30_npc_canplace(q30_live_enemy_access *e, fix dx, fix dy, int special, int nw, int nh)
{
    const int w = nw > -1 ? nw : e->hxsz, h = nh > -1 ? nh : e->hysz;
    const int bx = int(dx) + e->hxofs, by = int(dy) + e->hyofs;
    const int rx = bx + w - 1, ry = by + h - 1;

    for(int ty = 0; by + ty < ry; ty += 8)
    {
        for(int tx = 0; bx + tx < rx; tx += 8)
            if(e->walkflag(bx + tx, by + ty, special))
                return false;

        if(e->walkflag(rx, by + ty, special))
            return false;
    }

    for(int tx = 0; bx + tx < rx; tx += 8)
        if(e->walkflag(bx + tx, ry, special))
            return false;

    return !e->walkflag(rx, ry, special);
}

// Each frame of an enemy's animation (enemy::animate): a script's knockback moves it (upstream's
// enemy::runKnockback, called at the same point there).
void q30_npc_run_knockback(enemy *en)
{
    if(zasm30_script_kb.empty())
        return;

    std::map<long, Zasm30ScriptKb>::iterator it = zasm30_script_kb.find(en->getUID());

    if(it == zasm30_script_kb.end())
        return;

    Zasm30ScriptKb &kb = it->second;

    if((kb.clk & 0xFF) == 0)
    {
        zasm30_script_kb.erase(it);
        return;
    }

    if(zasm30_npc_state(en)->no_script_kb)
        return;

    q30_live_enemy_access *e = (q30_live_enemy_access *)en;
    const bool old = q30_quest_rule(336);   // qr_OLD_SCRIPTED_KNOCKBACK
    int move = kb.speed;
    const int kb_dir = kb.clk >> 8;
    --kb.clk;

    while(move > 0)
    {
        const int thismove = zc_min(old ? 8 : 4, move);
        move -= thismove;

        if(kb_dir == r_up || kb_dir == l_up || kb_dir == up) e->y -= thismove;
        else if(kb_dir == r_down || kb_dir == l_down || kb_dir == down) e->y += thismove;

        if(kb_dir == l_up || kb_dir == l_down || kb_dir == left) e->x -= thismove;
        else if(kb_dir == r_up || kb_dir == r_down || kb_dir == right) e->x += thismove;

        if(old ? !e->can_move(kb_dir, fix(0), 0) : !z30_npc_canplace(e, e->x, e->y, 0, -1, -1))
        {
            kb.clk = 0;
            e->clk3 = 0;

            // fix to the grid
            if(kb_dir != up && kb_dir != down)
            {
                if(e->x < 0) e->x = 0;
                else if((int(e->x) & 15) > 7) e->x = (int(e->x) & ~15) + 16;
                else e->x = int(e->x) & ~15;
            }

            if(kb_dir != left && kb_dir != right)
            {
                if(e->y < 0) e->y = 0;
                else if((int(e->y) & 15) > 7) e->y = (int(e->y) & ~15) + 16;
                else e->y = int(e->y) & ~15;
            }

            break;
        }
    }
}

// ---- script object ownership (OBJ_OWN_*: sprite->OwnBitmap() and friends) -------------------------
//
// Upstream gives the object to the sprite: from ZScript version 25 (gc) it holds a reference
// until the sprite goes, before that the object is deleted with the sprite. Here an owned object
// is one more root of the collector (zasm30_collect) while its sprite lives; a quest from before
// version 25 (whose bitmaps, files and so on the collector never frees) loses it with the
// sprite, and a local array of a quest from before version 28 (freed at the end of its scope)
// is kept until then. Paldata and RNG objects are never collected here, so owning one changes
// nothing but its end, which they don't have.

static void z30_own_value(long v, long type)
{
    const long uid = zasm30_own_sprite_uid(type);

    if(uid)
        zasm30_owned[v] = uid;
}

static void zasm30_own_array(long ptr, long type)
{
    const long p = ptr / 10000;

    if(p >= MAX_ZCARRAY_SIZE && p < MAX_ZCARRAY_SIZE * 2)
        return;     // a global array: ignored, as upstream's

    if(!ArrayH::isValid(p))
    {
        Z_scripterrlog("Tried to 'OwnArray()' an invalid array '%ld'\n", p);
        return;
    }

    const long uid = zasm30_own_sprite_uid(type);

    if(!uid)
        return;

    zasm30_owned[ptr] = uid;

    if(zasm30_ffscript_version < 28 && p > 0 && p < MAX_ZCARRAY_SIZE)
    {
        zasm30_array_owned[p] = true;      // the end of its scope leaves it (upstream's specOwned)
        zasm30_array_destroyed[p] = false;
    }
}

// deallocateArray: the end of a scope leaves an array a living sprite owns
bool zasm30_live_array_owned(long ptr)
{
    if(zasm30_owned.empty() || zasm30_ffscript_version >= 28)
        return false;

    std::map<long, long>::const_iterator it = zasm30_owned.find(ptr * 10000);
    return it != zasm30_owned.end() && z30_sprite(it->second).s != NULL;
}

// zasm30_collect: what living sprites own
void zasm30_mark_owned(std::vector<long> &work)
{
    for(std::map<long, long>::iterator it = zasm30_owned.begin(); it != zasm30_owned.end(); ++it)
        if(z30_sprite(it->second).s)
            zasm30_mark(it->first, work);
}

// A sprite is gone (q30_sprite_destroyed): what it owned is no longer held; before version 25 it
// goes with it, as do the local arrays of a quest from before version 28.
void zasm30_live_sprite_released(long uid)
{
    zasm30_script_kb.erase(uid);
    zasm30_ewpn_parent.erase(uid);

    for(std::map<long, long>::iterator it = zasm30_owned.begin(); it != zasm30_owned.end(); )
    {
        if(it->second != uid)
        {
            ++it;
            continue;
        }

        const long v = it->first;
        zasm30_owned.erase(it++);

        if(v > 0 && v % 10000 == 0 && (v / 10000 < MAX_ZCARRAY_SIZE || zasm30_heap_slot(v / 10000)))
        {
            const long p = v / 10000;

            if(zasm30_ffscript_version < 28)
            {
                if(p < MAX_ZCARRAY_SIZE)
                {
                    zasm30_array_owned[p] = false;

                    if(localRAM[p].Size() != 0)
                        deallocateArray(p);
                }
                else if(zasm30_array_live(p))
                    zasm30_free_array(p);
            }
        }
        else if(!z30_counted())
        {
            if(z30_bitmap(v) && v >= 1000)
                z30_free_bitmap(v);
            else if(v > Z30_FILE_BASE && v <= Z30_FILE_BASE + Z30_MAX_FILES && z30_files[v - Z30_FILE_BASE - 1].used)
            {
                z30_file_close(&z30_files[v - Z30_FILE_BASE - 1]);
                z30_files[v - Z30_FILE_BASE - 1].used = false;
            }
            else if(v > Z30_DIR_BASE && v <= Z30_DIR_BASE + Z30_MAX_DIRS)
            {
                z30_dirs[v - Z30_DIR_BASE - 1].used = false;
                z30_dirs[v - Z30_DIR_BASE - 1].names.clear();
            }
            else
                zasm30_stacks.erase(v);
        }
    }
}

// Auto-rotating weapons (lweapon->AutoRotate, eweapon->AutoRotate): drawn turned to their angle,
// or their direction (upstream's weapon::doAutoRotate, which runs as they turn).
static double z30_dir_degrees(int d)
{
    static const short deg[8] = { 270, 90, 180, 0, 225, 315, 135, 45 };
    return (d >= 0 && d < 8) ? deg[d] : 0;
}

void q30_auto_rotate(sprite *s, Zasm30SpriteState *st)
{
    if(st->auto_rotate && (Lwpns.getByUID(s->getUID()) || Ewpns.getByUID(s->getUID())))
        st->rotation = s->angular ? (long)(int)(s->angle * (180.0 / PI)) : (long)z30_dir_degrees(s->dir);
}

// lweapon->Vx / Vy (upstream's LWPNVX / LWPNVY)
static long z30_wpn_v(weapon *w, bool vx)
{
    if(w->angular)
        return (long)((vx ? cos(w->angle) : sin(w->angle)) * 10000.0 * double(w->step));

    static const signed char nd[16] = { up, down, left, right, l_up, r_up, l_down, r_down,
                                        up, r_up, right, r_down, down, l_down, left, l_up };
    const int d = (w->dir >= 0 && w->dir < 16) ? nd[w->dir] : -1;

    if(vx)
    {
        if(d == l_up || d == l_down || d == left) return (long)(-10000.0 * double(w->step));
        if(d == r_down || d == r_up || d == right) return (long)(10000.0 * double(w->step));
        return 0;
    }

    if(d == l_up || d == r_up || d == up) return (long)(-10000.0 * double(w->step));
    if(d == l_down || d == r_down || d == down) return (long)(10000.0 * double(w->step));
    return 0;
}

static void z30_wpn_set_v(weapon *w, bool vx, long value)
{
    const double v = value / 10000.0;
    const double other = z30_wpn_v(w, !vx) / 10000.0;
    const double nvx = vx ? v : other, nvy = vx ? other : v;
    w->angular = true;
    w->angle = atan2(nvy, nvx);
    w->step = fix(floor(sqrt(nvx * nvx + nvy * nvy) * 10000.0) / 10000.0);    // (upstream's zfix: 1/10000ths)

    if(Zasm30SpriteState *st = zasm30_sprite_state_if(w->getUID()))
        q30_auto_rotate(w, st);
}

// a weapon's FallClk / Drowning write (upstream's: the tile and CSet it had come back as it ends)
static void z30_wpn_clk_write(weapon *w, long &clk, long value)
{
    if(clk != 0 && value == 0)
    {
        w->cs = w->o_cset;
        w->tile = w->o_tile;
    }
    else if(clk == 0 && value != 0)
        w->o_cset = w->cs;

    clk = vbound(value / 10000, 0, 70);
}

// ---- Hero->GetOriginalTile() (upstream's FFScript::getHeroOTile) ----------------------------------

static long z30_hero_otile(long set, long dir)
{
    if(dir < 0 || dir > 3)
    {
        Z_scripterrlog("Invalid direction %ld passed to Hero->GetOriginalTile()\n", dir);
        return 0;
    }

    const int *q = NULL;
    long t = 0;

    switch(set)
    {
    case 0:  t = walkspr[dir][0]; break;
    case 1:  t = stabspr[dir][0]; break;
    case 2:  t = slashspr[dir][0]; break;
    case 3:  t = floatspr[dir][0]; break;
    case 4:  t = swimspr[dir][0]; break;
    case 5:  t = divespr[dir][0]; break;
    case 6:  t = poundspr[dir][0]; break;
    case 7:  t = jumpspr[dir][0]; break;
    case 8:  t = chargespr[dir][0]; break;
    case 9:  t = castingspr[0]; break;
    case 10: t = holdspr[0][0][0]; break;
    case 11: t = holdspr[0][1][0]; break;
    case 12: t = holdspr[1][0][0]; break;
    case 13: t = holdspr[1][1][0]; break;
    case 14: q = q255_hero_sprite(Q255HS_DROWN, dir); break;
    case 15: q = q255_hero_sprite(Q255HS_LAVADROWN, dir); break;
    case 16: q = q255_hero_sprite(Q255HS_SIDESWIM, dir); break;
    case 17: q = q255_hero_sprite(Q255HS_SIDESWIMSLASH, dir); break;
    case 18: q = q255_hero_sprite(Q255HS_SIDESWIMSTAB, dir); break;
    case 19: q = q255_hero_sprite(Q255HS_SIDESWIMPOUND, dir); break;
    case 20: q = q255_hero_sprite(Q255HS_SIDESWIMCHARGE, dir); break;
    case 21: q = q255_hero_sprite(Q255HS_SIDESWIMHOLD, 0); break;      // one hand
    case 22: q = q255_hero_sprite(Q255HS_SIDESWIMHOLD, 1); break;      // two hands
    case 23: q = q255_hero_sprite(Q255HS_SIDESWIMCAST, 0); break;
    case 24: q = q255_hero_sprite(Q255HS_SIDEDROWN, dir); break;
    case 25: q = q255_hero_sprite(Q255HS_REVSLASH, dir); break;
    default: t = 0; break;
    }

    if(q)
        t = q[0];

    return t * 10000;
}

// ---- registers ---------------------------------------------------------------------------------

static bool zasm30_live_get(const long arg, long &ret)
{
    switch(arg)
    {
    // Hero
    case Z30R_HEROFALLCMB: ret = q30_pit_combo() * 10000; return true;
    case Z30R_HEROFALLCLK: ret = q30_pit_falling() ? Link.q30_drown_clk() * 10000 : 0; return true;
    case Z30R_HERODROWNCLK:
        ret = (Link.getAction() == drowning && !q30_pit_falling()) ? Link.q30_drown_clk() * 10000 : 0;
        return true;
    case Z30R_LINKOTILE: ret = z30_hero_otile(Z30_INDEX, ri->d[1] / 10000); return true;
    case Z30R_LINKOFLIP: ret = 0; return true;     // Hero->GetOriginalFlip(): never implemented upstream, always 0
    case Z30R_SETITEMSLOT:
        Z_scripterrlog("Hero->SetItemSlot(): Not supported.\n");
        ret = 0;
        return true;

    // Game->
    case Z30R_INCQST:
    {
        const int newqst = game->get_quest() < 255 ? game->get_quest() + 1 : 1;    // 255 is a custom quest

        if(newqst < 11)
        {
            ret = newqst * 10000;
            Quit = qINCQST;
        }
        else
            ret = -10000;

        return true;
    }
    case Z30R_SKIPCREDITS: ret = zasm30_skip_credits ? 10000 : 0; return true;
    case Z30R_GAMEGRAVITY:
    {
        long n;

        if(!z30_array_index(Z30_INDEX, 4, n))
        {
            ret = -10000;
            return true;
        }

        switch(n)
        {
        case 0:
            ret = (zasm30_gravity_set && zinit.gravity == (byte)vbound(zasm30_gravity_written / 100, 0, 255))
                  ? zasm30_gravity_written : (long)zinit.gravity * 100;
            break;
        case 1:  ret = (long)zinit.terminalv * 100; break;
        case 2:  ret = (long)zinit.jump_link_layer_threshold * 10000; break;
        default: ret = zasm30_air_drag; break;
        }

        return true;
    }
    case Z30R_GAMEMISCSFX:
    {
        long n;     // (256 of them, upstream's sfxMAX)
        ret = z30_array_index(Z30_INDEX, 256, n) ? q30_misc_sfx(n) * 10000 : -10000;
        return true;
    }

    // npc->
    case Z30R_NPCCOLLISION:
    {
        const long type = Z30_INDEX;
        ret = 0;

        if(GuyH::loadNPC(ri->guyref, "npc->Collision()") == SH::_NoError && (type < 0 || type > 6))
            Z_scripterrlog("Invalid object type (%ld) passed to npc->Collision(int32_t type, int32_t ptr)\n", type);
        return true;
    }
    case Z30R_NPCSUBMERGED:
    {
        enemy *e = z30_npc_ref();

        // a wizzrobe that has gone (upstream's submerge(), whose submerged stays false under
        // qr_OLD_WIZZROBE_SUBMERGING, 354)
        if(!e)
            ret = -10000;
        else
            ret = (e->family == eeWIZZ && e->hxofs >= 1000 && !q30_quest_rule(354)) ? 10000 : 0;
        return true;
    }
    case Z30R_NPCENGINEANIMATE:
    case Z30R_LWPNENGINEANIMATE:
    case Z30R_EWPNENGINEANIMATE:
    {
        sprite *s = arg == Z30R_NPCENGINEANIMATE ? (sprite *)z30_npc_ref() : arg == Z30R_LWPNENGINEANIMATE ? (sprite *)z30_lwpn_ref()
                    : (sprite *)z30_ewpn_ref();
        Zasm30SpriteState *st = z30_sprite_st(z30_sprite(s ? s->getUID() : 0));
        ret = st ? (st->engine_animate ? 10000 : 0) : (arg == Z30R_NPCENGINEANIMATE ? -10000 : 0);
        return true;
    }
    case Z30R_NPCDROWNCLK:
    {
        enemy *e = z30_npc_ref();
        ret = e ? zasm30_npc_state(e)->drown_clk * 10000 : 0;
        return true;
    }
    case Z30R_NPCRINGLEAD:
    {
        enemy *e = z30_npc_ref();
        ret = (e && e->leader) ? 10000 : 0;
        return true;
    }

    // lweapon-> and eweapon->
    case Z30R_LWPNUSEDEFENCE: ret = zasm30_store_get(arg, ri->lwpn, 0) * 10000; return true;
    case Z30R_LWPNAUTOROTATE: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = (st && st->auto_rotate) ? 10000 : 0; return true; }
    case Z30R_LWPNGRAVITY: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = (st && (st->move_flags & 1)) ? 10000 : 0; return true; }
    case Z30R_LWPNDROWNCLK: { Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()); ret = st ? st->drown_clk * 10000 : 0; return true; }
    case Z30R_EWPNFALLCLK: { Zasm30SpriteState *st = z30_state_of(z30_ewpn_ref()); ret = st ? st->fall_clk * 10000 : 0; return true; }
    case Z30R_LWPNVX:
    case Z30R_LWPNVY:
    {
        weapon *w = z30_lwpn_ref();
        ret = w ? z30_wpn_v(w, arg == Z30R_LWPNVX) : 0;
        return true;
    }
    case Z30R_EWPNPARENTUID:
    {
        ret = 0;

        if(weapon *w = z30_ewpn_ref())
        {
            std::map<long, long>::const_iterator it = zasm30_ewpn_parent.find(w->getUID());

            if(it != zasm30_ewpn_parent.end() && z30_sprite(it->second).s)
                ret = it->second;
        }
        return true;
    }
    }

    return false;
}

static bool zasm30_live_set(const long arg, const long value)
{
    switch(arg)
    {
    // Hero
    case Z30R_HEROFALLCMB: q30_set_pit_combo(vbound(value / 10000, 0, MAXCOMBOS - 1)); return true;
    case Z30R_HERODROWNCLK:
    {
        // (upstream also writes the value to fallclk here, a slip this engine's falls can't follow)
        const long val = vbound(value / 10000, 0, 70);

        if(val)
        {
            if(Link.getAction() != drowning)
                Link.Drown();
        }
        else if(Link.getAction() == drowning)
            Link.setAction(none);
        return true;
    }
    case Z30R_LINKOTILE:
    case Z30R_LINKOFLIP: return true;
    case Z30R_SETITEMSLOT:
        // item, slot, force (upstream: "value = third arg")
        q30_set_item_slot(vbound(Z30_INDEX, -1, Q30_MAX_ITEMS - 1), ri->d[1] / 10000, ri->d[2] / 10000);
        return true;

    // Game->
    case Z30R_INCQST: return true;
    case Z30R_SKIPCREDITS: zasm30_skip_credits = (value / 10000) != 0; return true;
    case Z30R_GAMEGRAVITY:
    {
        long n;

        if(!z30_array_index(Z30_INDEX, 4, n))
            return true;

        switch(n)
        {
        case 0:
            zinit.gravity = (byte)vbound(value / 100, 0, 255);
            zasm30_gravity_written = value;
            zasm30_gravity_set = true;
            break;
        case 1:  zinit.terminalv = (word)(value / 100); break;
        case 2:  zinit.jump_link_layer_threshold = (byte)(value / 10000); break;
        default: zasm30_air_drag = value; break;
        }

        return true;
    }
    case Z30R_GAMEMISCSFX:
    {
        long n;

        if(z30_array_index(Z30_INDEX, 256, n))
            q30_set_misc_sfx(n, vbound(value / 10000, 0, 65535));
        return true;
    }

    // npc->
    case Z30R_NPCCOLLISION:
    case Z30R_NPCSUBMERGED: return true;
    case Z30R_NPCENGINEANIMATE:
    case Z30R_LWPNENGINEANIMATE:
    case Z30R_EWPNENGINEANIMATE:
    {
        sprite *s = arg == Z30R_NPCENGINEANIMATE ? (sprite *)z30_npc_ref() : arg == Z30R_LWPNENGINEANIMATE ? (sprite *)z30_lwpn_ref()
                    : (sprite *)z30_ewpn_ref();

        if(Zasm30SpriteState *st = z30_sprite_st(z30_sprite(s ? s->getUID() : 0)))
            st->engine_animate = arg == Z30R_NPCENGINEANIMATE ? (value / 10000) != 0 : value != 0;
        return true;
    }
    case Z30R_NPCDROWNCLK:
    {
        // (enemies don't drown in this engine: the clock is kept for the script)
        if(enemy *e = z30_npc_ref())
        {
            Zasm30SpriteState *st = zasm30_npc_state(e);

            if(st->drown_clk != 0 && value == 0)
                e->tile = e->o_tile;

            st->drown_clk = vbound(value / 10000, 0, 70);
        }
        return true;
    }
    case Z30R_NPCRINGLEAD:
        if(enemy *e = z30_npc_ref())
            e->leader = (value / 10000) != 0;
        return true;

    // lweapon-> and eweapon->
    case Z30R_LWPNUSEDEFENCE:
        if(z30_lwpn_ref())
            zasm30_store_set(arg, ri->lwpn, 0, vbound(value / 10000, 0, 255));
        return true;
    case Z30R_LWPNAUTOROTATE:
        if(weapon *w = z30_lwpn_ref())
        {
            Zasm30SpriteState *st = z30_state_of(w);
            st->auto_rotate = value != 0;
            q30_auto_rotate(w, st);
        }
        return true;
    case Z30R_LWPNGRAVITY:
        if(Zasm30SpriteState *st = z30_state_of(z30_lwpn_ref()))
            z30_flag_set(st->move_flags, 0, 32, value != 0);
        return true;
    case Z30R_LWPNDROWNCLK:
        if(weapon *w = z30_lwpn_ref())
            z30_wpn_clk_write(w, z30_state_of(w)->drown_clk, value);
        return true;
    case Z30R_EWPNFALLCLK:
        if(weapon *w = z30_ewpn_ref())
            z30_wpn_clk_write(w, z30_state_of(w)->fall_clk, value);
        return true;
    case Z30R_LWPNVX:
    case Z30R_LWPNVY:
        if(weapon *w = z30_lwpn_ref())
            z30_wpn_set_v(w, arg == Z30R_LWPNVX, value);
        return true;
    case Z30R_EWPNPARENTUID:
        if(weapon *w = z30_ewpn_ref())
        {
            if(value && z30_sprite(value).s)
                zasm30_ewpn_parent[w->getUID()] = value;
            else
                zasm30_ewpn_parent.erase(w->getUID());
        }
        return true;
    }

    return false;
}

// ---- commands ----------------------------------------------------------------------------------

static bool zasm30_live_command(word cmd)
{
    switch(cmd)
    {
    // sprite->Explode()
    case Z30C_LINKEXPLODER: z30_explode_command(&Link, "Hero", true); return true;
    case Z30C_NPCEXPLODER:
    {
        const long mode = get_register(sarg1) / 10000;

        if((unsigned long)mode > 2)
            Z_scripterrlog("Invalid mode (%ld) passed to npc->Explode(int32_t mode)\n", mode);
        else if(GuyH::loadNPC(ri->guyref, "npc->Explode()") == SH::_NoError)
            z30_explode(GuyH::getNPC(), (int)mode, false);
        return true;
    }
    case Z30C_ITEMEXPLODER: z30_explode_command(z30_item_ref(), "item", false); return true;
    case Z30C_LWEAPONEXPLODER: z30_explode_command(z30_lwpn_ref(), "lweapon", false); return true;
    case Z30C_EWEAPONEXPLODER: z30_explode_command(z30_ewpn_ref(), "eweapon", false); return true;

    // Game->GetNPC("name"): the enemy id of that name, -1 if none
    case Z30C_GETNPCBYNAME:
    {
        std::string name;
        long num = -1;
        ArrayH::getString(get_register(sarg1) / 10000, name, 256);

        for(int q = 0; q < MAXGUYS; ++q)
        {
            if(guy_string[q] && !strcmp(name.c_str(), guy_string[q]))
            {
                num = q;
                break;
            }
        }

        set_register(sarg1, num * 10000);
        return true;
    }

    // Game->SetCustomCursor(bitmap, fx, fy, recolor, scale)
    case Z30C_GAMESETCUSTOMCURSOR:
    {
        const long ptr = SH::read_stack(SPR + 4);
        BITMAP *b = z30_bitmap(ptr);

        if(!b)
            Z_scripterrlog("Game->SetCustomCursor(): invalid bitmap %ld\n", ptr);
        else
        {
            if(zasm30_custom_cursor)
                destroy_bitmap(zasm30_custom_cursor);

            zasm30_custom_cursor = create_bitmap_ex(8, b->w, b->h);
            blit(b, zasm30_custom_cursor, 0, 0, 0, 0, b->w, b->h);
            zasm30_custom_cursor_fx = SH::read_stack(SPR + 3) / 10000;
            zasm30_custom_cursor_fy = SH::read_stack(SPR + 2) / 10000;
        }
        return true;
    }

    // npc-> movement
    case Z30C_NPCVARWALK:
    case Z30C_NPCVARWALK8:
    case Z30C_NPCHALTWALK:
    case Z30C_NPCHALTWALK8:
    case Z30C_NPCFLOATWALK:
    {
        ZScriptArray *a = z30_live_array(get_register(sarg1));

        if(!a)
            return true;

        q30_live_enemy_access *e = z30_live_npc("npc->Walk()");

        if(!e)
            return true;

        const long sz = (long)a->Size();
        long v[8];

        for(int n = 0; n < 8; ++n)
            v[n] = n < sz ? (*a)[n] / 10000 : 0;

        if(cmd == Z30C_NPCVARWALK && sz == 3) e->var_walk(v[0], v[1], v[2]);
        else if(cmd == Z30C_NPCVARWALK8 && sz == 4) e->var_walk8(v[0], v[1], v[2], v[3]);
        else if(cmd == Z30C_NPCVARWALK8 && sz == 8) e->var_walk8(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
        else if(cmd == Z30C_NPCHALTWALK && sz == 5) e->halt_walk(v[0], v[1], v[2], v[3], v[4]);
        else if(cmd == Z30C_NPCHALTWALK8 && sz == 6) e->halt_walk8(v[0], v[1], v[2], v[3], v[4], v[5]);
        else if(cmd == Z30C_NPCFLOATWALK && sz == 3) e->float_walk(v[0], v[1], fix(v[2]));
        else if(cmd == Z30C_NPCFLOATWALK && sz == 7) e->float_walk(v[0], v[1], fix(v[2]), fix(v[3]), v[4], v[5], v[6]);
        else Z_scripterrlog("Invalid array size: %ld\n", sz);
        return true;
    }
    case Z30C_NPCFIREBREATH:
    {
        const bool seek = get_register(sarg1) != 0;

        if(q30_live_enemy_access *e = z30_live_npc("npc->BreathAttack()"))
            e->fire_breath(seek);
        return true;
    }
    case Z30C_NPCCANMOVE:
    {
        const long ptr = get_register(sarg1);
        q30_live_enemy_access *e = z30_live_npc("npc->CanMove()");
        ZScriptArray *a = e ? z30_live_array(ptr) : NULL;

        if(!a)
            return true;

        const long sz = (long)a->Size();
        long v[7];

        for(int n = 0; n < 7; ++n)
            v[n] = n < sz ? (*a)[n] / 10000 : 0;

        if(sz == 1) set_register(sarg1, e->can_move(v[0]) ? 10000 : 0);
        else if(sz == 2) set_register(sarg1, e->can_move(v[0], (int)v[1]) ? 10000 : 0);
        else if(sz == 3) set_register(sarg1, e->can_move(v[0], fix(v[1]), v[2]) ? 10000 : 0);
        else if(sz == 7) set_register(sarg1, e->can_move(v[0], fix(v[1]), v[2], v[3], v[4], v[5], v[6]) ? 10000 : 0);
        else
        {
            Z_scripterrlog("Invalid array size (%ld) passed to npc->CanMove(). The array size must be [1], [2], [3], or [7].\n", sz);
            set_register(sarg1, 0);
        }
        return true;
    }
    case Z30C_NPCCANPLACE:
    {
        ri->guyref = SH::read_stack(SPR + 6);
        ri->d[2] = 0;

        if(q30_live_enemy_access *e = z30_live_npc("npc->CanPlace()"))
        {
            const fix nx = fix(SH::read_stack(SPR + 5) / 10000.0), ny = fix(SH::read_stack(SPR + 4) / 10000.0);
            const int special = SH::read_stack(SPR + 3) / 10000;
            const int nw = SH::read_stack(SPR + 1) / 10000, nh = SH::read_stack(SPR + 0) / 10000;
            ri->d[2] = z30_npc_canplace(e, nx, ny, special, nw, nh) ? 10000 : 0;
        }
        return true;
    }
    case Z30C_NPCCANSLIDE:
    {
        q30_live_enemy_access *e = z30_live_npc("npc->CanSlide()");

        if(!e)
        {
            set_register(sarg1, -10000);
            return true;
        }

        // upstream's enemy::can_slide; this engine's knockback is the old kind (12 pixels a check)
        const int sclk = e->get_sclk();
        bool ok = !(sclk == 0 || e->hp <= 0);

        if(ok && (sclk & 255) == 16 && !e->can_move(sclk >> 8, fix(e->dmisc2 == e2tSPLITHIT ? 1 : 12), 0))
            ok = false;

        set_register(sarg1, ok ? 10000 : 0);
        return true;
    }
    case Z30C_NPCHITWITH:
    {
        ZScriptArray *a = z30_live_array(get_register(sarg1));

        if(!a)
            return true;

        bool ishit = false;

        if(q30_live_enemy_access *e = z30_live_npc("npc->SimulateHit()"))
        {
            const long sz = (long)a->Size();

            if(sz == 6)
                ishit = e->hit((*a)[0] / 10000, (*a)[1] / 10000, (*a)[2] / 10000, (*a)[3] / 10000, (*a)[4] / 10000, (*a)[5] / 10000);
            else
                Z_scripterrlog("Invalid array size (%ld) passed to npc->SimulateHit(). The array size must be [1] or [3].\n", sz);
        }

        set_register(sarg1, ishit ? 10000 : 0);
        return true;
    }
    case Z30C_NPCKNOCKBACK:
    {
        const int time = get_register(sarg1) / 10000, dir = get_register(sarg2) / 10000;
        const int spd = vbound(ri->d[0] / 10000, 0, 255);
        bool ok = false;

        // upstream's enemy::knockback, then sprite::knockback
        if(q30_live_enemy_access *e = z30_live_npc("npc->Knockback()"))
        {
            if(e->hp > 0 && e->can_move(dir, fix(spd), 0, 0, 0, 15, 15) && !zasm30_npc_state(e)->no_script_kb)
            {
                Zasm30ScriptKb kb = { (time & 0xFF) | ((dir & 0xFF) << 8), spd };
                zasm30_script_kb[e->getUID()] = kb;
                e->set_sclk(0);     // the engine's own knockback stops
                ok = true;
            }
        }

        set_register(sarg1, ok ? 10000 : 0);
        return true;
    }

    // sprite->OwnBitmap() and friends (types: upstream's ScriptType, see zasm30_own_sprite_uid)
    case Z30C_OBJ_OWN_BITMAP:
    {
        const long v = get_register(sarg1);

        if(v >= 1000 && z30_bitmap(v))      // (never the system bitmaps)
            z30_own_value(v, sarg2);
        return true;
    }
    case Z30C_OBJ_OWN_FILE:
    {
        const long v = get_register(sarg1);

        if(z30_file(v, false))
            z30_own_value(v, sarg2);
        return true;
    }
    case Z30C_OBJ_OWN_DIR:
    {
        const long v = get_register(sarg1);

        if(z30_dir(v))
            z30_own_value(v, sarg2);
        return true;
    }
    case Z30C_OBJ_OWN_STACK:
    {
        const long v = get_register(sarg1);

        if(zasm30_stacks.count(v))
            z30_own_value(v, sarg2);
        return true;
    }
    case Z30C_OBJ_OWN_PALDATA:
    {
        const long v = get_register(sarg1);

        if(z30_paldata(v))
            z30_own_value(v, sarg2);
        return true;
    }
    case Z30C_OBJ_OWN_RNG:
    {
        const long v = get_register(sarg1);

        if(zasm30_rngs.count(v))
            z30_own_value(v, sarg2);
        return true;
    }
    case Z30C_OBJ_OWN_ARRAY:
        zasm30_own_array(get_register(sarg1), sarg2);
        return true;
    }

    return false;
}

#endif

// Enemy data (GUY section), format versions 24 to 60.
//
// Kept beside guysbuf as stored (q30_set_guy_defenses), for the switch hook's own defense
// and "Switch w/ Player"; 2.53's enemy::defend doesn't see them:
//   defense[18]                                  v25+  upstream's unused edefLAST250 slot
//   defense[20-29]                               v25+  script weapons 1-10
//   defense[30-40]                               v25+  ice, bait, wind, sparkle, sonic, whistle,
//                                                      switchhook, thrown, refl. arrow, fire, fire 2
//
// Read and dropped, 2.53 has no field for these:
//   txsz, tysz, hxsz, hysz, hzsz                 v26+  tile and hit sizes
//   hxofs, hyofs, xofs, yofs, zofs               v27+  hit and draw offsets
//   wpnsprite                                    v28+
//   SIZEflags                                    v29+
//   frozentile, frozencset, frozenclock,
//   frozenmisc[10]                               v30+
//   firesfx                                      v34+
//   attributes[15-31]                            v34+
//   movement[32], new_weapon[32]                 v34+
//   npc script, its InitD[8], 2 unused longs     v34-58
//   npc script config (script, run args,
//   instance init values)                        v59+
//   editorflags                                  v37+
//   8 x 65 bytes of InitD label text             v39-58  upstream discards these too
//   8 x 65 bytes more InitD label text           v39-53  upstream discards these too
//   weapon script                                v40-53
//   weapon script InitD[8]                       v41-53
//   moveflags (one byte, then four from v47)     v42+
//   spr_shadow, spr_death, spr_spawn             v45+  (words from v56)
//   weapon data, inline form: unblockable, moveflags, override flags, sizes,
//   offsets, step, burn sprites, light radii     v52-53
//   specialsfx                                   v53+  (word from v55)
//   weapon data block (see skip_weapon_data)     v54+
//   viewport suspend and despawn ranges          v58+
//   firesfx_secondary                            v60+
//
// Stored, but 2.53 has no behaviour for them:
//   flags bit 6 (burning sprites); flags2 bits 16-18 (ignore tmpnr, ignore
//   kill-all, kill summoned enemies)
//   family 43+ (script and friendly enemies): addenemy() refuses to spawn them
//
// Mapped to the 2.53 equivalent:
//   flags bit 7 (no contact damage): contact damage is set to 0; touching the
//   enemy still knocks Link back
//   defense outcomes after ed1HKO: see map_defense
//   anim, e_anim 51-55 (upstream's big-enemy animations): see map_anim
//   weapon ids 2.53 doesn't have become no weapon
//   misc8 of bubble enemies, bit flags from v49: 2.53 expects the old values,
//   and has nothing for the "shield" bit
//   hitsfx, deadsfx: words from v55; values past 2.53's sounds become silence
//
// Narrowed:
//   tile, s_tile, e_tile are 32-bit from v36 and reach 214499 upstream; values
//   past the 2.53 sheet are clamped to its last tile.
//
// Upstream fixes deliberately not applied:
//   v<49 rewrites misc8 of bubble enemies to bit flags; 2.53 expects the old values.
//   fixpolsvoice only applies to initdata below v14, which no 2.55 quest has; the
//   global can be left set by an earlier 2.53 quest, so it isn't consulted here.

#include "precompiled.h" //always first

#include "q30_scripts.h"

#include "q255.h"
#include "q30_triggers.h"
#include "q30_regions.h"
#include "sfx.h"
#ifdef ZC_PLAYER_BUILD
void q30_set_guy_death_sprite(int guy, int death, int spawn);    // guys.cpp
void q30_set_guy_data(int guy, const Q30GuyData &d);            // q30_scripts_data.h
bool q30_quest_rule(int id);
#endif

extern guydata *guysbuf;
extern char *guy_string[eMAXGUYS];
extern const char *old_guy_string[OLDMAXGUYS];

static const int FILE_GUYS = 512;           // entries in the section
static const int FILE_DEFENSES_FIRST = 19;  // stored before hitsfx
static const int FILE_DEFENSES_ALL = 41;    // total from v25 on; the rest follow misc12
static const int FILE_QUAKE = 19;           // edefQUAKE's index from v25 on
static const int FILE_WPNSPR_MAX = 5;       // upstream's WPNSPR_MAX
static const int FILE_MAX_VERSION = 60;     // upstream's V_GUYS

// Upstream's numbers for its added enemy flags and defense outcomes.
static const dword FILE_FLAG_NO_CONTACT = 0x00000080;
static const dword FILE_FLAG2_IGNORE_KILL_ALL = 0x00020000;

enum
{
    FILE_edCHINKL10 = 15, FILE_ed2x, FILE_ed3x, FILE_ed4x, FILE_edHEAL, FILE_edTRIGGERSECRETS,
    FILE_edFREEZE, FILE_edMSG_NOT_ENABLED, FILE_edMSG_LINE, FILE_edLEVELDAMAGE, FILE_edLEVELREDUCTION,
    FILE_edSPLIT, FILE_edREPLACE, FILE_edLEVELCHINK2, FILE_edLEVELCHINK3, FILE_edLEVELCHINK4,
    FILE_edLEVELCHINK5, FILE_edSHOCK, FILE_edEXPLODESMALL, FILE_edEXPLODELARGE, FILE_edSTONE,
    FILE_edBREAKSHIELD, FILE_edRESTORESHIELD, FILE_edSPECIALDROP, FILE_edINCREASECOUNTER,
    FILE_edREDUCECOUNTER, FILE_edEXPLODEHARMLESS, FILE_edKILLNOSPLIT, FILE_edTRIBBLE, FILE_edFIREBALL,
    FILE_edFIREBALLLARGE, FILE_edSUMMON, FILE_edWINGAME, FILE_edJUMP, FILE_edEATHERO, FILE_edSHOWMESSAGE,
    FILE_edSWITCH
};

// Upstream's added defense outcomes, as the nearest thing 2.53's enemy::defend can do.
// 2.53 treats any outcome it doesn't know as normal damage.
static byte map_defense(byte d)
{
    if(d < edLAST)
        return d;

    switch(d)
    {
    case FILE_edCHINKL10:
        return edCHINKL8;

    case FILE_edHEAL:
    case FILE_edSWITCH:
        return edIGNORE;

    case FILE_edFREEZE:
        return edSTUNONLY;

    // These remove the enemy in upstream (its replacements and blasts are lost).
    case FILE_edSPLIT:
    case FILE_edREPLACE:
    case FILE_edEXPLODESMALL:
    case FILE_edEXPLODELARGE:
    case FILE_edEXPLODEHARMLESS:
        return ed1HKO;

    // ed2x, ed3x, ed4x, edTRIGGERSECRETS and edSUMMON also do normal damage in
    // upstream; the rest aren't implemented there, and fall through to normal.
    default:
        return edNORMAL;
    }
}

// Upstream's animations 51-55 are the big-enemy forms of 2.53's a4FRM8EYE,
// a4FRM4EYE, a4FRM8DIRF, a4FRM4DIR and a4FRM4DIRF. a4FRM8DIRB has the same
// tile layout (2x2 per frame) in the four cardinal directions.
#ifdef ZC_PLAYER_BUILD
byte q255_guy_defense_253(byte d) { return map_defense(d); }     // npcdata->Defense[] writes
#endif

static short map_anim(short a)
{
#ifndef ZC_PLAYER_BUILD     // (the player draws them as upstream: guys.cpp update_enemy_frame)
    if(a >= aMAX && a <= aMAX + 4)
        return a4FRM8DIRB;
#endif

    return a;
}

// Upstream's extra player weapons (wIce, wFlame, wSound...) have no 2.53 id.
static short map_weapon(short w)
{
    if(w < 0 || w >= wMax || (w > wScript10 && w < wEnemyWeapons))
        return wNone;

    return w;
}

// v49+ stores what a bubble enemy jinxes as bit flags (1 sword, 2 item, 4 shield);
// 2.53's Link::hitlink compares against e8tSWORD, e8tITEM and e8tALL.
static long map_jinx_target(long bits)
{
    switch(bits & 3)
    {
    case 1:
        return e8tSWORD;

    case 2:
        return e8tITEM;

    case 3:
        return e8tALL;

    default:
        return e8tLAST;     // no target at all, or only the shield
    }
}

static byte read_sfx(Q255Section &s)
{
    if(s.version < 55)
        return s.getc();

    word v = s.igetw();
    return (v >= WAV_COUNT) ? 0 : (byte)v;
}

static tile_id read_tile(Q255Section &s)
{
    dword t = (s.version >= 36) ? s.igetl() : (dword)s.igetw();
    q255_note_tile("enemy", t);
    return (t >= (dword)NEWMAXTILES) ? (tile_id)(NEWMAXTILES - 1) : (tile_id)t;
}

// p_getvar(script_config*): the script number, its run args (a word count; upstream
// stops storing after 8, and reads only those), then a word-counted word -> long map.
static void read_script_config(Q255Section &s, word *script, long *initd)
{
    word id = s.igetw();
    if(script)
        *script = id;

    word args = s.igetw();

    for(word i = 0; i < 8 && i < args; ++i)
    {
        long v = s.igetl_s();
        if(initd)
            initd[i] = v;
    }

    if(args > 8)
        s.skip((size_t)(args - 8) * 4);

    word inits = s.igetw();
    s.skip((size_t)inits * (2 + 4));
}

static void skip_script_config(Q255Section &s)
{
    read_script_config(s, NULL, NULL);
}

// The npc and weapon script of the enemy being read: set while its fields go by.
static word g_npc_script, g_npc_weapon_script;
static dword g_npc_moveflags;
static long g_npc_wpnsprite;     // kept for scripts (npcdata->WeaponSprite)
static int g_npc_death_spr;     // the death sprite (upstream's spr_death; iwDeath before v45)
static int g_npc_spawn_spr;     // the spawn sprite (upstream's spr_spawn; iwSpawn before v45)
static long g_npc_vp_suspend, g_npc_vp_despawn;     // viewport ranges (q30_regions.h)
static long g_npc_wvp_suspend, g_npc_wvp_despawn;   // its weapons' (the weapon data block)
static long g_npc_initd[8], g_npc_weapon_initd[8];
static byte g_npc_raw_defense[FILE_DEFENSES_ALL];    // as stored: the switch hook's and edSWITCH need them
static Q30GuyData g_npc_data;   // the rest 2.53's guydata has no room for (player side)

// read_weap_data: every field of the block, which carries its own version.
static void skip_weapon_data(Q255Section &s)
{
    word *script_out = &g_npc_weapon_script;
    long *initd_out = g_npc_weapon_initd;
    Q30WeaponData &d = g_npc_data.weap;

    word v = s.igetw();
    d.data_version = v;

    d.flags = s.igetw();
    d.moveflags = s.igetl();
    d.wflags = s.igetw();

    for(int q = 0; q < FILE_WPNSPR_MAX && !s.failed; ++q)
    {
        d.burnsprs[q] = (v < 1) ? (word)s.getc() : s.igetw();     // burn sprite
        d.light_rads[q] = s.getc();                                // light radius

        if(v >= 3)
            d.light_offsets[q] = s.igetl_s();                      // light offset
    }

    d.glow_shape = s.getc();
    d.override_flags = s.igetl_s();
    d.tilew = s.igetl_s();      // tile and hit sizes, hit and draw offsets
    d.tileh = s.igetl_s();
    d.hxsz = s.igetl_s();
    d.hysz = s.igetl_s();
    d.hzsz = s.igetl_s();
    d.hxofs = s.igetl_s();
    d.hyofs = s.igetl_s();
    d.xofs = s.igetl_s();
    d.yofs = s.igetl_s();
    d.step = s.igetl_s();
    d.unblockable = s.getc();
    d.timeout = s.igetl_s();
    d.imitate_weapon = s.getc();
    d.default_defense = s.getc();
    d.lift_level = s.getc();
    d.lift_time = s.getc();
    d.lift_height = s.igetl_s();

    if(v < 5)
    {
        *script_out = s.igetw();
        for(int i = 0; i < 8; ++i)
            initd_out[i] = s.igetl_s();
    }
    else
        read_script_config(s, script_out, initd_out);

    d.pierce_count = s.igetw_s();

    if(v >= 2)
    {
        d.bounce_mult = s.igetl_s();
        d.bounce_add = s.igetl_s();
    }

    if(v >= 4)
    {
        g_npc_wvp_suspend = s.igetl_s();     // viewport suspend, despawn ranges
        g_npc_wvp_despawn = s.igetl_s();
        d.viewport_suspend_range = g_npc_wvp_suspend;
        d.viewport_despawn_range = g_npc_wvp_despawn;
    }
}

static void read_names(Q255Section &s)
{
    for(int i = 0; i < FILE_GUYS && !s.failed; ++i)
    {
        char tempname[64];
        s.read(tempname, 64);

        if(s.failed || !s.keep || i >= MAXGUYS)
            continue;

        tempname[63] = '\0';

        // A trailing space marks an uneditable entry's name: use the built-in one.
        if(i >= OLDMAXGUYS || strlen(tempname) < 1 || tempname[strlen(tempname) - 1] != ' ')
            strcpy(guy_string[i], tempname);
        else
            strcpy(guy_string[i], old_guy_string[i]);
    }
}

#ifdef ZC_PLAYER_BUILD
// ---- upstream's reader fixes of the data kept player side (qst_guys.cpp) ----------------------

static short legacy_weapon_firesfx(int weapon)
{
    switch(weapon)
    {
    case ewFireTrail: case ewFlame: case ewFlame2Trail: case ewFlame2: return WAV_FIRE;
    case ewWind: case ewMagic: return WAV_WAND;
    case ewIce: return WAV_ZN1ICE;
    case ewRock: return WAV_ZN1ROCK;
    case ewFireball2: case ewFireball: return WAV_ZN1FIREBALL;
    case ewBrang: return WAV_BRANG;
    case ewBomb: case ewSBomb: case ewLitBomb: case ewLitSBomb: return WAV_BOMB;
    }

    return 0;
}

// the weapon the 'Fire Octo' death effect fires (secondary_fire_weapon)
static int secondary_fire_weapon(const guydata &g, const Q30GuyData &x)
{
    if(g.family != eeWALK || x.attributes[1] != e2tFIREOCTO)
        return 0;
    if(!g.weapon || g.weapon == ewBrang)
        return 0;

    int weapon = g.weapon + x.attributes[2];
    if(weapon <= wEnemyWeapons || weapon >= wMax)
        weapon = g.weapon;
    return weapon;
}

static bool firesfx_played_for_weapon(int weapon)
{
    switch(weapon)
    {
    case wScript1: case wScript2: case wScript3: case wScript4: case wScript5: case wScript6: case wScript7:
    case wScript8: case wScript9: case wScript10: case ewFireTrail: case ewFlame: case ewFlame2Trail: case ewFlame2:
    case ewWind: case ewMagic: case ewIce:
        return true;
    case ewRock: case ewFireball2: case ewFireball:
        return q30_quest_rule(106);     // qr_MORESOUNDS
    }

    return false;
}

static void guy_data_fixes(int v, const guydata &g, Q30GuyData &x)
{
    const bool summon = (g.family == eeWALK || g.family == eePROJECTILE)
                        && (x.attributes[0] == e1tSUMMON || x.attributes[0] == e1tSUMMONLAYER);

    if(v < 51)      // guy_update_firesfx
    {
        x.firesfx = 0;

        if(g.family == eeWIZZ)
        {
            switch(x.attributes[1])
            {
            case 0: x.firesfx = WAV_WAND; break;
            case 1:
                if(!q30_quest_rule(277))    // qr_8WAY_SHOT_SFX_DEP
                {
                    x.firesfx = legacy_weapon_firesfx(g.weapon);
                    break;
                }
                // fall through
            case 2: x.firesfx = WAV_FIRE; break;
            case 3: x.firesfx = q30_quest_rule(106) ? WAV_ZN1SUMMON : WAV_FIRE; break;
            }
        }
        else if(summon)
            x.firesfx = q30_quest_rule(106) ? WAV_ZN1SUMMON : WAV_FIRE;
        else
            x.firesfx = legacy_weapon_firesfx(g.weapon);

        x.firesfx_secondary = legacy_weapon_firesfx(secondary_fire_weapon(g, x));
    }
    else if(v < 60)
        x.firesfx_secondary = x.firesfx;

    if(v < 60)      // guy_silence_unplayed_firesfx
    {
        if(!(g.family == eeWIZZ || summon) && !firesfx_played_for_weapon(g.weapon))
            x.firesfx = 0;
        if(!firesfx_played_for_weapon(secondary_fire_weapon(g, x)))
            x.firesfx_secondary = 0;
    }

    // upstream's readguys before v49: what a bubble jinxes (attribute 8) becomes bit flags, 1 the
    // sword, 2 items (2.53's guydata keeps 0 sword, 1 item, 2 both, which play uses)
    if(v < 49 && g.family == eeWALK
            && (x.attributes[6] == e7tPERMJINX || x.attributes[6] == e7tTEMPJINX || x.attributes[6] == e7tUNJINX))
    {
        switch(x.attributes[7])
        {
        case 0: x.attributes[7] = 1; break;
        case 1: x.attributes[7] = 2; break;
        case 2: x.attributes[7] = 1 | 2; break;
        default: x.attributes[7] = 0; break;     // (any other number jinxes nothing)
        }
    }

    Q30WeaponData &d = x.weap;

    if(v < 52)      // guy_update_weaponflags
    {
        d.unblockable = 0;
        d.moveflags = (g.weapon == ewFlame || g.weapon == ewFireTrail) ? (0x01 | 0x02) : 0;   // obeys gravity, can pitfall
        d.override_flags = 0;
        d.hxofs = d.hyofs = d.hxsz = d.hysz = d.hzsz = d.xofs = d.yofs = d.tilew = d.tileh = 0;

        switch(g.weapon)
        {
        case ewFireball: case ewFireball2: d.step = 17500; break;
        case ewSword: case ewLitBomb: case ewLitSBomb: case ewRock: case ewMagic: case ewWind: d.step = 30000; break;
        case ewArrow: d.step = 20000; break;
        case ewFlame: case ewFlame2: d.step = 10000; break;
        default: d.step = 0; break;
        }

        memset(d.burnsprs, 0, sizeof(d.burnsprs));
        memset(d.light_rads, 0, sizeof(d.light_rads));
    }

    if(v < 54)
    {
        if(g.flags & 0x40)          // guy_burning_sprites -> WFLAG_UPDATE_IGNITE_SPRITE
            d.wflags |= 0x0200;
        d.flags |= 0x02;            // wdata_set_step
        if(g.weapon == ewRock)
            d.wflags |= 0x0004;     // WFLAG_BREAK_ON_SOLID
    }

    d.script = g_npc_weapon_script;     // (weap_data.scrconfig)
    memcpy(d.initd, g_npc_weapon_initd, sizeof(d.initd));
}
#endif

static void read_guy(Q255Section &s, guydata &g)
{
    const int v = s.version;

    memset(&g, 0, sizeof(guydata));

    g.flags = s.igetl();
    g.flags2 = s.igetl();

    g.tile = read_tile(s);
    g.width = s.getc();
    g.height = s.getc();
    g.s_tile = read_tile(s);
    g.s_width = s.getc();
    g.s_height = s.getc();
    g.e_tile = read_tile(s);
    g.e_width = s.getc();
    g.e_height = s.getc();

    g.hp = s.igetw_s();
    g.family = s.igetw_s();
    g.cset = s.igetw_s();
    g.anim = map_anim(s.igetw_s());
    g.e_anim = map_anim(s.igetw_s());
    g.frate = s.igetw_s();
    g.e_frate = s.igetw_s();
    g.dp = s.igetw_s();
    g.wdp = s.igetw_s();
    g.weapon = map_weapon(s.igetw_s());
    g.rate = s.igetw_s();
    g.hrate = s.igetw_s();
    g.step = s.igetw_s();
    g.homing = s.igetw_s();
    g.grumble = s.igetw_s();
    g.item_set = s.igetw_s();

    g.misc1 = s.igetl_s();
    g.misc2 = s.igetl_s();
    g.misc3 = s.igetl_s();
    g.misc4 = s.igetl_s();
    g.misc5 = s.igetl_s();
    g.misc6 = s.igetl_s();
    g.misc7 = s.igetl_s();
    g.misc8 = s.igetl_s();
    g.misc9 = s.igetl_s();
    g.misc10 = s.igetl_s();

    g.bgsfx = s.igetw_s();
    g.bosspal = s.igetw_s();
    g.extend = s.igetw_s();

    // Indices 0-17 match 2.53. Index 18 is 2.53's Quake slot only in v24; later
    // versions moved Quake to index 19.
    memset(g_npc_raw_defense, 0, sizeof(g_npc_raw_defense));

    for(int j = 0; j < FILE_DEFENSES_FIRST; ++j)
    {
        byte d = s.getc();
        g_npc_raw_defense[j] = d;

        if(j < edefQUAKE || v <= 24)
            g.defense[j] = map_defense(d);
    }

    g.hitsfx = read_sfx(s);
    g.deadsfx = read_sfx(s);

    g.misc11 = s.igetl_s();
    g.misc12 = s.igetl_s();

    if(v > 24)
    {
        for(int j = FILE_DEFENSES_FIRST; j < FILE_DEFENSES_ALL; ++j)
        {
            byte d = s.getc();
            g_npc_raw_defense[j] = d;

            if(j == FILE_QUAKE)
                g.defense[edefQUAKE] = map_defense(d);
        }
    }

    Q30GuyData &x = g_npc_data;

    if(v > 25)
    {
        x.txsz = s.igetl_s();     // tile and hit sizes
        x.tysz = s.igetl_s();
        x.hxsz = s.igetl_s();
        x.hysz = s.igetl_s();
        x.hzsz = s.igetl_s();
    }

    if(v > 26)
    {
        x.hxofs = s.igetl_s();    // hit and draw offsets
        x.hyofs = s.igetl_s();
        x.xofs = s.igetl_s();
        x.yofs = s.igetl_s();
        x.zofs = s.igetl_s();
    }

    if(v > 27)
        g_npc_wpnsprite = s.igetl_s();     // wpnsprite

    if(v > 28)
        x.sizeflags = s.igetl_s();         // SIZEflags

    if(v >= 30)
    {
        x.frozentile = s.igetl_s();        // frozen tile, cset, clock, misc
        x.frozencset = s.igetl_s();
        x.frozenclock = s.igetl_s();

        for(int q = 0; q < 10; ++q)
            x.frozenmisc[q] = s.igetw_s();
    }

    if(v >= 34)
    {
        x.firesfx = s.igetw_s();           // firesfx

        for(int q = 15; q < 32; ++q)
            x.attributes[q] = s.igetl_s(); // attributes 15-31

        s.skip(32 * 4);    // movement
        s.skip(32 * 4);    // new_weapon

        if(v < 59)
        {
            g_npc_script = s.igetw();    // npc script
            for(int i = 0; i < 8; ++i)
                g_npc_initd[i] = s.igetl_s();
            s.skip(2 * 4);     // unused
        }
        else
        {
            read_script_config(s, &g_npc_script, g_npc_initd);
        }
    }

    if(v >= 37)
        x.editorflags = s.igetl();         // editorflags

    if(v >= 38)
    {
        g.misc13 = s.igetl_s();
        g.misc14 = s.igetl_s();
        g.misc15 = s.igetl_s();
    }

    if(v >= 39)
    {
        if(v < 59)
            s.skip(8 * 65);    // InitD labels

        if(v < 54)
            s.skip(8 * 65);    // and the weapon script's
    }

    if(v >= 40 && v < 54)
        g_npc_weapon_script = s.igetw();     // weapon script

    if(v >= 41 && v < 54)
    {
        for(int i = 0; i < 8; ++i)
            g_npc_weapon_initd[i] = s.igetl_s();  // weapon script InitD
    }

    if(v >= 47)
        g_npc_moveflags = s.igetl();         // moveflags
    else if(v >= 42)
        g_npc_moveflags = s.getc();

    if(v > 55)
    {
        x.spr_shadow = s.igetw();          // shadow, death and spawn sprites
        g_npc_death_spr = s.igetw();
        g_npc_spawn_spr = s.igetw();
    }
    else if(v > 44)
    {
        x.spr_shadow = s.getc();
        g_npc_death_spr = s.getc();
        g_npc_spawn_spr = s.getc();
    }
    else
        x.spr_shadow = (g.family == eeROCK && g.misc10 == 1) ? iwLargeShadow : iwShadow;

    if(v >= 52 && v < 54)
    {
        Q30WeaponData &d = x.weap;         // the inline form (upstream's reader)
        d.unblockable = s.getc();
        d.moveflags = s.igetl();
        d.override_flags = s.igetl_s();
        d.tilew = s.igetl_s();
        d.tileh = s.igetl_s();
        d.hxsz = s.igetl_s();
        d.hysz = s.igetl_s();
        d.hzsz = s.igetl_s();
        d.hxofs = s.igetl_s();
        d.hyofs = s.igetl_s();
        d.xofs = s.igetl_s();
        d.yofs = s.igetl_s();
        d.step = s.igetl_s() * 100;

        for(int q = 0; q < FILE_WPNSPR_MAX; ++q)
        {
            d.burnsprs[q] = s.igetw();
            d.light_rads[q] = (byte)s.igetw();
        }
    }

    if(v >= 55)
        x.specialsfx = s.igetw_s();        // specialsfx
    else if(v >= 53)
        x.specialsfx = s.getc();

    if(v >= 54)
        skip_weapon_data(s);

    if(v >= 58)
    {
        g_npc_vp_suspend = s.igetl_s();     // viewport suspend, despawn ranges
        g_npc_vp_despawn = s.igetl_s();
    }

    if(v >= 60)
        x.firesfx_secondary = s.igetw_s(); // firesfx_secondary

    x.attributes[0] = g.misc1; x.attributes[1] = g.misc2; x.attributes[2] = g.misc3; x.attributes[3] = g.misc4;
    x.attributes[4] = g.misc5; x.attributes[5] = g.misc6; x.attributes[6] = g.misc7; x.attributes[7] = g.misc8;
    x.attributes[8] = g.misc9; x.attributes[9] = g.misc10; x.attributes[10] = g.misc11; x.attributes[11] = g.misc12;
    x.attributes[12] = g.misc13; x.attributes[13] = g.misc14; x.attributes[14] = g.misc15;
#ifdef ZC_PLAYER_BUILD
    guy_data_fixes(v, g, x);
#endif

    // Older quests relied on the hardcoded hit sound (WAV_EHIT).
    if((s.header->zelda_version <= 0x250 || v < 35) && g.hitsfx == 0)
        g.hitsfx = 11;

    if(v < 44 && g.family == eeGHOMA)
        g.flags |= guy_fadeinstant;

    if(v < 47 && g.family == eeDIG && g.misc10 != 1)
        g.flags2 |= FILE_FLAG2_IGNORE_KILL_ALL;

    // Upstream's move flags (readguys) for enemies saved before they had any: fliers and bosses
    // go over pits (0x04), walkers obey gravity (0x01) and fall in pits (0x02).
    if(v < 42)
    {
        switch(g.family)
        {
        case eeTEK: case eePEAHAT: case eeROCK: case eeTRAP: case eePROJECTILE: case eeSPINTILE:
        case eeKEESE: case eeFIRE: case eeFAIRY: case eeGUY: case eeNONE: case eeZORA: case eeAQUA:
        case eeDIG: case eeGHOMA: case eeGANON: case eePATRA: case eeGLEEOK: case eeMOLD: case eeMANHAN:
            g_npc_moveflags = 0x04;
            break;

        case eeLEV:
            g_npc_moveflags = 0x02;
            break;

        case eeDONGO:
            g_npc_moveflags = 0x01;
            break;

        case eeWIZZ: case eeWALLM: case eeGHINI:
            g_npc_moveflags = 0x01 | 0x04;
            break;

        case eeWALK:
            if(g.misc9 != e9tPOLSVOICE && g.misc9 != e9tVIRE)
                g_npc_moveflags = 0x01 | 0x02;

            break;

        default:
            if(g.family == eeOTHER || (g.family > eeMAX && g.family <= eeMAX + 30))   // scripted and friendly types
                g_npc_moveflags = 0x01 | 0x02;
        }
    }

    if(v < 43)
    {
        switch(g.family)
        {
        case eeTEK: case eePEAHAT: case eeROCK: case eeTRAP: case eePROJECTILE: case eeSPINTILE:
        case eeKEESE: case eeFIRE: case eeFAIRY: case eeGUY: case eeNONE: case eeZORA: case eeAQUA:
        case eeDIG: case eeGHOMA: case eeGANON: case eePATRA: case eeGLEEOK: case eeMOLD: case eeMANHAN:
        case eeWIZZ: case eeWALLM: case eeGHINI:
            g_npc_moveflags |= 0x10 | 0x04;     // walks on water and over pits
            break;
        }
    }

    if(v < 46 && g.family == eeWALK && g.misc9 == e9tPOLSVOICE)
        g_npc_moveflags |= 0x10;

    if(v >= 49 && g.family == eeWALK
            && (g.misc7 == e7tTEMPJINX || g.misc7 == e7tPERMJINX || g.misc7 == e7tUNJINX))
        g.misc8 = map_jinx_target(g.misc8);

    if(g.flags & FILE_FLAG_NO_CONTACT)
        g.dp = 0;
}

int q255_read_guys(Q255Section &s)
{
    if(s.version < 24 || s.version > FILE_MAX_VERSION)
        return Q255_NOT_HANDLED;

    read_names(s);

    if(s.failed)
        return qe_invalid;

    if(s.keep)
    {
        init_guys(s.version);
        q30_clear_guy_scripts();
        q30_clear_guy_extras();
        q30_set_guy_defenses(-1, NULL);
    }

    for(int i = 0; i < FILE_GUYS; ++i)
    {
        guydata g;
        g_npc_script = g_npc_weapon_script = 0; g_npc_moveflags = 0; g_npc_wpnsprite = 0;
        g_npc_death_spr = iwDeath;
        g_npc_spawn_spr = iwSpawn;
        g_npc_vp_suspend = 48;      // (upstream's guydata default)
        g_npc_vp_despawn = 0;
        g_npc_wvp_suspend = 48;     // (upstream's guydata::clear)
        g_npc_wvp_despawn = 0;
        memset(g_npc_initd, 0, sizeof(g_npc_initd));
        memset(g_npc_weapon_initd, 0, sizeof(g_npc_weapon_initd));
        g_npc_data = Q30GuyData();
        read_guy(s, g);

        if(s.failed)
            return qe_invalid;

        if(s.keep && i < MAXGUYS)
        {
            guysbuf[i] = g;
            q30_set_guy_script(i, g_npc_script, g_npc_initd);
            q30_set_guy_moveflags(i, g_npc_moveflags);
            q30_set_guy_weapon_script(i, g_npc_weapon_script, g_npc_weapon_initd);
            q30_set_guy_wpnsprite(i, (int)g_npc_wpnsprite);
            q30_set_guy_defenses(i, g_npc_raw_defense);
#ifdef ZC_PLAYER_BUILD

            q30_set_guy_death_sprite(i, g_npc_death_spr, g_npc_spawn_spr);
            q30_set_guy_data(i, g_npc_data);

            if(g_npc_vp_suspend != 48 || g_npc_vp_despawn != 0)
                q30_vp_guy_ranges(i, g_npc_vp_suspend, g_npc_vp_despawn);

            if(g_npc_wvp_suspend != 48 || g_npc_wvp_despawn != 0)
                q30_vp_guy_weapon_ranges(i, g_npc_wvp_suspend, g_npc_wvp_despawn);

#endif
        }
    }

    return 0;
}

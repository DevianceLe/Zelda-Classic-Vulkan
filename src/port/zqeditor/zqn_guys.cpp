// The GUY section (enemy data and enemy names) saved as 2.55.17 (section version 48) or as
// 3.0 Nightly (version 60), for quests made in 2.53.1 or older (see zq_writers.h).
//
// Upstream's loader converts an old quest's enemies far more than 2.53's does: fields 2.53
// has no room for get values made up from the enemy's kind and weapon (weapon sprites, fire
// sounds, movement flags, spawn and death sprites, halt rates, whistle defences, and in 3.0
// a whole block of weapon data), and a few 2.53 fields come out differently (the Quake
// defence's slot, a darknut's breakable shield, a 2.53 Wizzrobe's ignored defences). Some of
// that depends on what the file held before 2.53's own conversions, which ZQuest's data no
// longer shows. So, as upstream does, each enemy nobody changed since the load is read again
// from the quest's own bytes with a port of upstream's reader for that section version
// (2.55.17's readguys in qst.cpp, 3.0's readguy_single in core/qst_guys.cpp), and quests
// that store no enemies (1.9x, 2.10: GUY version 1, or none) start from a port of upstream's
// init_guys. An enemy the user changed is taken from ZQuest's data as an enemy of a 2.53.1
// quest (section version 24), which is how upstream would read it from a quest saved in
// 2.53 with that change.
//
// Upstream's deprecated rules decide a few old conversions. They are what the quest's file
// held, which 2.53's loader keeps aside where ZQuest can't see them; ZQuest's own reader
// still applies them, so they are found by running it on a few made-up enemies (probe_rules).

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "sfx.h"
#include "qst.h"
#include "defdata.h"
#include "zquest.h"
#include "zq_writers.h"
#include "zqw_itemguy.h"
#include "zq_tape.h"
#include "zqn_255.h"

extern guydata *guysbuf;
extern char *guy_string[eMAXGUYS];
extern const char *old_guy_string[OLDMAXGUYS];
extern word quest_header_zelda_version, quest_header_zelda_build;
extern bool fixpolsvoice;

namespace
{

using zqw_ig::put_le;
using zqw_ig::get_le;
using zqw_ig::MemFile;
using zqw_ig::open_mem;

// ---- upstream's values where 2.53's differ or are missing --------------------------------

const int U_DEFENSES = 41;          // edefLAST255
const int U_edefSCRIPT01 = 20;      // edefSCRIPT01..edefSCRIPT10: 20..29
const int U_edefSCRIPT10 = 29;
const int U_edefWhistle = 35;
const int U_edefQUAKE = 19;         // 2.53's Quake defence (slot 18) stays in slot 18 upstream
const int U_wIce = 41;
const dword U_guy_ignorekill = 0x00020000;     // flags2 (3.0: bit 49 of its 64-bit flags)

// movement flags (2.55's FLAG_*, 3.0's move_*)
const dword MF_OBEYS_GRAV = 1, MF_CAN_PITFALL = 2, MF_CAN_PITWALK = 4, MF_CAN_WATERWALK = 16;

// 3.0's weapon data flags
const word WD_SET_STEP = 2;                             // wdata_set_step
const word WFLAG_BREAK_ON_SOLID = 4, WFLAG_UPDATE_IGNITE_SPRITE = 512;
const dword guy_burning_sprites = 0x40;                 // LLF(6)

// 3.0's bubble jinx targets are bit flags (e8tSWORD 1, e8tITEM 2), 2.53's and 2.55's a list
const long U30_e8tSWORD = 1, U30_e8tITEM = 2;

const int V_GUYS_255 = 48, CV_GUYS_255 = 4;
const int V_GUYS_30 = 60;
const int V_WEAP_DATA_30 = 5;

// The enemy kinds upstream's enemy-editor families number past 2.53's
const int eeSCRIPT01 = 43, eeSCRIPT20 = 62, eeFFRIENDLY01 = 63, eeFFRIENDLY10 = 72;

// ---- an enemy as upstream holds it --------------------------------------------------------

struct UWeapon      // 3.0's weapon_data
{
    word flags;
    dword moveflags;
    word wflags;
    word burnsprs[5];
    byte light_rads[5];
    long light_offsets[5];
    byte glow_shape;
    long override_flags, tilew, tileh, hxsz, hysz, hzsz, hxofs, hyofs, xofs, yofs;
    long step;                      // fixed point, x10000
    byte unblockable;
    long timeout;
    byte imitate_weapon, default_defense, lift_level, lift_time;
    long lift_height;               // fixed point, x10000
    word script;
    long run_args[8];
    short pierce_count;
    long bounce_mult, bounce_add;
    long viewport_suspend_range, viewport_despawn_range;
};

struct UGuy
{
    dword flags, flags2;            // 3.0 keeps them as one 64-bit value, flags2 on top
    long tile, s_tile, e_tile;
    byte width, height, s_width, s_height, e_width, e_height;
    short hp, family, cset, anim, e_anim, frate, e_frate, dp, wdp, weapon;
    short rate, hrate, step, homing, grumble, item_set;
    long misc[32];                  // misc1..misc32 (3.0's attributes[0..31])
    short bgsfx, bosspal, extend;
    byte defense[U_DEFENSES];
    word hitsfx, deadsfx;
    long xofs, yofs, zofs, hxofs, hyofs, hxsz, hysz, hzsz, txsz, tysz;
    long wpnsprite, SIZEflags, frozentile, frozencset, frozenclock;
    short frozenmisc[10];
    short firesfx, firesfx_secondary;
    long movement[32], new_weapon[32];
    word script;
    long initD[8], initA[2];
    long editorflags;
    char initD_label[8][65], weapon_initD_label[8][65];     // 2.55 only
    word weaponscript;
    long weap_initiald[8];
    dword moveflags;
    word spr_shadow, spr_death, spr_spawn, specialsfx;
    UWeapon wd;                     // 3.0 only
    long viewport_suspend_range, viewport_despawn_range;
};

// What upstream's reader looks at besides the enemy itself.
struct UCtx
{
    int target;
    word zver;                      // the quest's header
    byte build;
    int v, cv;                      // the GUY section's version and cversion (0 for none)
    bool newenemytiles, moresounds, eightway_dep, polsvoice;
    bool dep_slowanim, dep_rope2flash, dep_bubbleflash, dep_ghini2blink, dep_phantomghini2;
    bool dep_brkblshlds, dep_oldtribbles, dep_46;
};

bool is30(const UCtx &c)
{
    return c.target == ZQ_NEWER_30;
}

// An enemy cleared as upstream clears one: 2.55 zeroes it, 3.0 sets it to guydata().
void clear_guy(UGuy &g, int target)
{
    memset(&g, 0, sizeof(g));

    if(target == ZQ_NEWER_30)
    {
        g.viewport_suspend_range = 48;
        g.wd.lift_time = 16;
        g.wd.lift_height = 8 * 10000;
        g.wd.pierce_count = -1;
        g.wd.viewport_suspend_range = 48;       // guydata::clear()
    }
}

// The fields 2.53 has, from a 2.53 enemy.
void take_253(UGuy &g, const guydata &z)
{
    g.flags = z.flags;
    g.flags2 = z.flags2;
    g.tile = z.tile;
    g.width = z.width;
    g.height = z.height;
    g.s_tile = z.s_tile;
    g.s_width = z.s_width;
    g.s_height = z.s_height;
    g.e_tile = z.e_tile;
    g.e_width = z.e_width;
    g.e_height = z.e_height;
    g.hp = z.hp;
    g.family = z.family;
    g.cset = z.cset;
    g.anim = z.anim;
    g.e_anim = z.e_anim;
    g.frate = z.frate;
    g.e_frate = z.e_frate;
    g.dp = z.dp;
    g.wdp = z.wdp;
    g.weapon = z.weapon;
    g.rate = z.rate;
    g.hrate = z.hrate;
    g.step = z.step;
    g.homing = z.homing;
    g.grumble = z.grumble;
    g.item_set = z.item_set;
    const long m[15] = { z.misc1, z.misc2, z.misc3, z.misc4, z.misc5, z.misc6, z.misc7, z.misc8, z.misc9, z.misc10,
                         z.misc11, z.misc12, z.misc13, z.misc14, z.misc15 };

    for(int j = 0; j < 15; ++j)
        g.misc[j] = m[j];

    g.bgsfx = z.bgsfx;
    g.bosspal = z.bosspal;
    g.extend = z.extend;

    for(int j = 0; j < edefLAST; ++j)
        g.defense[j] = z.defense[j];

    g.hitsfx = z.hitsfx;
    g.deadsfx = z.deadsfx;
}

// ---- upstream's default enemies ------------------------------------------------------------

// Upstream's default_guys (defdata.cpp) is 2.53's but for a few entries: the Patra (BS)
// eye animation and the inner eyes of two Patras in both, and in 3.0 the heights of the
// Gleeok neck tiles and the bubbles' jinx targets (3.0 counts them as bit flags).
void upstream_default(UGuy &g, int target, int i)
{
    clear_guy(g, target);
    take_253(g, default_guys[i]);

    switch(i)
    {
    case 103:
        g.e_anim = 51;              // a4FRM8EYEB
        break;

    case 104:
        g.s_tile = 5300;
        g.s_width = 16;
        g.s_height = 2;
        break;

    case 105:
        g.s_tile = 5420;
        g.s_width = 16;
        g.s_height = 6;
        break;
    }

    if(target != ZQ_NEWER_30)
        return;

    switch(i)
    {
    case 62: case 63: case 64: case 65:
    case 109: case 110: case 111: case 112:
        g.e_height = 2;
        break;

    case 51: case 81: case 82:
        g.misc[7] = U30_e8tSWORD;
        break;

    case 117: case 118: case 119:
        g.misc[7] = U30_e8tITEM;
        break;
    }
}

// ---- 3.0's made-up weapon sounds and data (core/qst_guys.cpp) ------------------------------

short legacy_weapon_firesfx(int weapon)
{
    switch(weapon)
    {
    case ewFireTrail: case ewFlame: case ewFlame2Trail: case ewFlame2:
        return WAV_FIRE;

    case ewWind: case ewMagic:
        return WAV_WAND;

    case ewIce:
        return WAV_ZN1ICE;

    case ewRock:
        return WAV_ZN1ROCK;

    case ewFireball2: case ewFireball:
        return WAV_ZN1FIREBALL;

    case ewBrang:
        return WAV_BRANG;

    case ewBomb: case ewSBomb: case ewLitBomb: case ewLitSBomb:
        return WAV_BOMB;
    }

    return 0;
}

// The weapon a 'Fire Octo' fires when it dies, 0 for none.
int secondary_fire_weapon(const UGuy &g)
{
    if(g.family != eeWALK || g.misc[1] != e2tFIREOCTO)
        return 0;

    if(!g.weapon || g.weapon == ewBrang)
        return 0;

    int weapon = g.weapon + g.misc[2];

    if(weapon <= wEnemyWeapons || weapon >= wMax)
        weapon = g.weapon;

    return weapon;
}

void guy_update_firesfx(UGuy &g, const UCtx &c)
{
    g.firesfx = 0;

    if(g.family == eeWIZZ)
    {
        switch(g.misc[1])
        {
        case 0:
            g.firesfx = WAV_WAND;
            break;

        case 1:
            if(c.eightway_dep)
                g.firesfx = WAV_FIRE;
            else
            {
                g.firesfx = legacy_weapon_firesfx(g.weapon);
                break;
            }

            // falls through, as upstream's does
        case 2:
            g.firesfx = WAV_FIRE;
            break;

        case 3:
            g.firesfx = c.moresounds ? WAV_ZN1SUMMON : WAV_FIRE;
            break;
        }
    }
    else if((g.family == eeWALK || g.family == eePROJECTILE) && (g.misc[0] == e1tSUMMON || g.misc[0] == e1tSUMMONLAYER))
        g.firesfx = c.moresounds ? WAV_ZN1SUMMON : WAV_FIRE;
    else
        g.firesfx = legacy_weapon_firesfx(g.weapon);

    g.firesfx_secondary = legacy_weapon_firesfx(secondary_fire_weapon(g));
}

bool firesfx_played_for_weapon(int weapon, const UCtx &c)
{
    switch(weapon)
    {
    case wScript1: case wScript2: case wScript3: case wScript4: case wScript5:
    case wScript6: case wScript7: case wScript8: case wScript9: case wScript10:
    case ewFireTrail: case ewFlame: case ewFlame2Trail: case ewFlame2:
    case ewWind: case ewMagic: case ewIce:
        return true;

    case ewRock: case ewFireball2: case ewFireball:
        return c.moresounds;
    }

    return false;
}

void guy_silence_unplayed_firesfx(UGuy &g, const UCtx &c)
{
    bool always = g.family == eeWIZZ || ((g.family == eeWALK || g.family == eePROJECTILE) && (g.misc[0] == e1tSUMMON || g.misc[0] == e1tSUMMONLAYER));

    if(!always && !firesfx_played_for_weapon(g.weapon, c))
        g.firesfx = 0;

    if(!firesfx_played_for_weapon(secondary_fire_weapon(g), c))
        g.firesfx_secondary = 0;
}

void guy_update_weaponflags(UGuy &g)
{
    g.wd.unblockable = 0;
    g.wd.moveflags = 0;

    if(g.weapon == ewFlame || g.weapon == ewFireTrail)
        g.wd.moveflags |= MF_OBEYS_GRAV | MF_CAN_PITFALL;

    g.wd.override_flags = 0;
    g.wd.hxofs = g.wd.hyofs = g.wd.hxsz = g.wd.hysz = g.wd.hzsz = 0;
    g.wd.xofs = g.wd.yofs = g.wd.tilew = g.wd.tileh = 0;

    switch(g.weapon)
    {
    case ewFireball: case ewFireball2:
        g.wd.step = 17500;
        break;

    case ewSword: case ewLitBomb: case ewLitSBomb: case ewRock: case ewMagic: case ewWind:
        g.wd.step = 30000;
        break;

    case ewArrow:
        g.wd.step = 20000;
        break;

    case ewFlame: case ewFlame2:
        g.wd.step = 10000;
        break;

    default:
        g.wd.step = 0;
        break;
    }

    for(int q = 0; q < 5; ++q)
    {
        g.wd.burnsprs[q] = 0;
        g.wd.light_rads[q] = 0;
    }
}

void guy_update_weaponspecialsfx(UGuy &g)
{
    switch(g.weapon)
    {
    case ewBrang:
        g.specialsfx = WAV_BRANG;
        break;

    case ewBomb: case ewSBomb: case ewLitBomb: case ewLitSBomb:
        g.specialsfx = WAV_BOMB;
        break;

    default:
        g.specialsfx = 0;
        break;
    }
}

// ---- upstream's update_guy_1 (November 2009) ----------------------------------------------

void update_guy_1(UGuy &g, const UCtx &c)
{
    bool doesntcount = false;
    long *m = g.misc;
    g.flags &= ~weak_arrow;

    switch(g.family)
    {
    case 1: //eeWALK
        switch(m[9])
        {
        case 0:
            if(m[0] == 1)
                m[0] = 4;

            break;

        case 1:
            goto darknuts;
        }

        m[9] = 0;
        break;

    case 2: //eeSHOOT
        g.family = eeWALK;

        switch(m[9])
        {
        case 0: //Octorok
            if(m[0] == 1 || m[0] == 2)
            {
                m[0] = e1tFIREOCTO;
                m[1] = e2tFIREOCTO;
            }
            else
                m[0] = 0;

            m[5] = m[3];
            m[3] = m[2];
            m[2] = 0;
            break;

        case 1: //Moblin
            m[0] = 0;
            break;

        case 2: //Lynel
            m[5] = m[0] + 1;
            m[0] = 0;
            break;

        case 3: //Stalfos 2
            if(m[0] == 1)
                m[0] = e1t4SHOTS;
            else
                m[0] = 0;

            break;

        case 4: //Darknut 5
darknuts:
            g.defense[edefFIRE] = edIGNORE;
            g.defense[edefBRANG] = edSTUNORCHINK;
            g.defense[edefHOOKSHOT] = 0;
            g.defense[edefARROW] = g.defense[edefBYRNA] = g.defense[edefREFROCK] =
                                       g.defense[edefMAGIC] = g.defense[edefSTOMP] = edCHINK;

            if(m[0] == 1)
                m[0] = 2;
            else if(m[0] == 2)
            {
                m[3] = m[2];
                m[2] = m[1];
                m[1] = e2tSPLIT;
                m[0] = 0;
            }
            else
                m[0] = 0;

            g.flags |= inv_front;

            // upstream also takes a breakable shield away when the rule is off; 2.53 only adds it
            if(!c.dep_brkblshlds)
                g.flags &= ~guy_bkshield;
            else
                g.flags |= guy_bkshield;

            break;
        }

        m[9] = 0;
        break;

    case 11: //eeGEL
    case 33: //eeGELTRIB
        if(g.family == 33)
        {
            m[3] = 1;

            if(c.dep_oldtribbles)
                m[2] = m[1];

            m[1] = e2tTRIBBLE;
        }
        else
        {
            m[3] = 0;
            m[2] = 0;
            m[1] = 0;
        }

        g.family = eeWALK;

        if(m[0])
        {
            m[0] = 1;
            g.weapon = ewFireTrail;
        }

        break;

    case 34: //eeZOLTRIB
    case 12: //eeZOL
        m[3] = m[2];
        m[2] = m[1];
        g.family = eeWALK;
        m[1] = e2tSPLITHIT;

        if(m[0])
        {
            m[0] = 1;
            g.weapon = ewFireTrail;
        }

        break;

    case 13: //eeROPE
        g.family = eeWALK;
        m[8] = e9tROPE;

        if(m[0])
        {
            m[3] = m[2];
            m[2] = m[1];
            m[1] = e2tBOMBCHU;
        }

        m[0] = 0;
        break;

    case 14: //eeGORIYA
        g.family = eeWALK;

        if(m[0] != 2)
            m[0] = 0;

        break;

    case 17: //eeBUBBLE
        g.family = eeWALK;
        m[7] = m[1];
        m[6] = m[0] + 1;
        m[0] = m[1] = 0;
        doesntcount = true;
        break;

    case eeTRAP:
    case eeROCK:
        doesntcount = true;
        break;

    case 35: //eeVIRETRIB
    case 18: //eeVIRE
        g.family = eeWALK;
        m[3] = m[2];
        m[2] = m[1];
        m[1] = e2tSPLITHIT;
        m[8] = e9tVIRE;
        break;

    case 19: //eeLIKE
        g.family = eeWALK;
        m[6] = e7tEATITEMS;
        m[7] = 95;
        break;

    case 20: //eePOLSV
        g.defense[edefBRANG] = edSTUNORCHINK;
        g.defense[edefBOMB] = g.defense[edefSBOMB] = g.defense[edefFIRE] = edIGNORE;
        g.defense[edefMAGIC] = g.defense[edefBYRNA] = edCHINK;
        g.defense[edefARROW] = ed1HKO;
        g.defense[edefHOOKSHOT] = edSTUNONLY;
        g.family = eeWALK;
        m[8] = e9tPOLSVOICE;
        g.rate = 4;
        g.homing = 32;
        g.hrate = 10;
        g.grumble = 0;
        break;

    case eeWIZZ:
        if(m[3])
        {
            // upstream's Quake defence isn't among the first 19, so slot 18 is ignored too
            for(int j = 0; j < edefLAST; ++j)
                g.defense[j] = (j != edefREFBEAM && j != edefREFMAGIC && j != U_edefQUAKE) ? edIGNORE : 0;
        }
        else
        {
            g.defense[edefBRANG] = edSTUNORCHINK;
            g.defense[edefMAGIC] = edCHINK;
            g.defense[edefHOOKSHOT] = edSTUNONLY;
            g.defense[edefARROW] = g.defense[edefFIRE] = g.defense[edefWAND] = g.defense[edefBYRNA] = edIGNORE;
        }

        break;

    case eePEAHAT:
        g.flags &= ~(guy_superman | guy_sbombonly);

        if(!(g.flags & guy_bhit))
            g.defense[edefBRANG] = edSTUNONLY;

        break;

    case eeLEV:
        g.defense[edefSTOMP] = edCHINK;
        break;
    }

    if(g.flags & guy_superman)
    {
        for(int j = 0; j < edefLAST; ++j)
            if(!(j == edefSBOMB && (g.flags & guy_sbombonly)))
                g.defense[j] = (j == edefBRANG && g.defense[j] != edIGNORE && g.family != eeROCK && g.family != eeTRAP
                                && g.family != eePROJECTILE) ? edSTUNORIGNORE : edIGNORE;
    }

    g.flags &= ~(guy_superman | guy_sbombonly);

    if(doesntcount)
        g.flags |= guy_doesntcount;
}

// ---- upstream's init_guys and readguys' header fixes ---------------------------------------

void init_guys(std::vector<UGuy> &gs, const UCtx &c, int guyversion)
{
    for(int i = 0; i < MAXGUYS; ++i)
        upstream_default(gs[i], c.target, 0);

    for(int i = 0; i < OLDMAXGUYS; ++i)
    {
        UGuy &g = gs[i];
        upstream_default(g, c.target, i);
        g.spr_shadow = (g.family == eeROCK && g.misc[9] == 1) ? iwLargeShadow : iwShadow;
        g.spr_death = iwDeath;
        g.spr_spawn = iwSpawn;

        if(guyversion <= 3 && i == ePATRABS)
        {
            g.bosspal = spDIG;
            g.cset = 14;
            g.misc[8] = 14;
        }

        if(guyversion <= 3)
        {
            if(c.dep_rope2flash && i == eROPE2)
                g.flags2 &= ~guy_flashing;

            if(c.dep_bubbleflash && (i == eBUBBLEST || i == eBUBBLESP || i == eBUBBLESR || i == eBUBBLEIT || i == eBUBBLEIP || i == eBUBBLEIR))
                g.flags2 &= ~guy_flashing;

            if(i == eGHINI2)
            {
                if(c.dep_ghini2blink)
                    g.flags2 |= guy_blinking;

                if(c.dep_phantomghini2)
                    g.flags2 |= guy_transparent;
            }

            if(i == eDIG1 || i == eDIG3)
                g.flags2 |= U_guy_ignorekill;
        }

        if(i == eDKNUT1 || i == eDKNUT2 || i == eDKNUT3 || i == eDKNUT4 || i == eDKNUT5)
        {
            if(c.newenemytiles)
            {
                g.s_tile = g.e_tile + 120;
                g.s_width = g.e_width;
                g.s_height = g.e_height;
            }
            else
                g.s_tile = 860;

            if(!c.dep_brkblshlds)
                g.flags &= ~guy_bkshield;
            else
                g.flags |= guy_bkshield;
        }

        if((i == eGELTRIB || i == eFGELTRIB) && c.dep_oldtribbles)
            g.misc[2] = (i == eFGELTRIB ? eFZOL : eZOL);

        if(is30(c))
        {
            guy_update_firesfx(g, c);
            guy_silence_unplayed_firesfx(g, c);
            guy_update_weaponflags(g);
            guy_update_weaponspecialsfx(g);
        }
    }
}

void header_fixes(std::vector<UGuy> &gs, const UCtx &c, int guyversion)
{
    const word zv = c.zver;
    const byte b = c.build;

    if((zv < 0x211 || (zv == 0x211 && b < 7)) && c.newenemytiles)
    {
        gs[gGORIYA].tile = 130;
        gs[gGORIYA].e_tile = 130;
    }

    if(zv < 0x193 && c.dep_46)
    {
        gs[eDODONGO].cset = 14;
        gs[eDODONGO].bosspal = spDIG;
    }

    if(zv < 0x211 || (zv == 0x211 && b < 18))
    {
        gs[eWWIZ].editorflags |= 0x10;          // ENEMY_FLAG5
        gs[eMOLDORM].editorflags |= 0x20;       // ENEMY_FLAG6
        gs[eMANHAN].editorflags |= 0x20;
        gs[eCENT1].misc[2] = 1;
        gs[eCENT2].misc[2] = 1;
    }

    if(guyversion < 33)
        for(int i = 0; i < MAXGUYS; ++i)
            if(gs[i].family != eeDIG || gs[i].misc[9] == 1)
                gs[i].defense[U_edefWhistle] = edIGNORE;

    gs[eWPOLSV].defense[U_edefWhistle] = ed1HKO;    // every quest older than 2.55 build 47

    if(zv <= 0x210)
    {
        gs[eGLEEOK1F].misc[5] = 16;
        gs[eGLEEOK2F].misc[5] = 16;
        gs[eGLEEOK3F].misc[5] = 16;
        gs[eGLEEOK4F].misc[5] = 16;
        gs[eWIZ1].misc[3] = 1;
        gs[eBATROBE].misc[3] = 1;
        gs[eWWIZ].misc[3] = 1;
        gs[eDODONGO].deadsfx = 15;
        gs[eDODONGOBS].deadsfx = 15;
    }

    if(zv == 0x190)
    {
        gs[eKEESETRIB].misc[2] = eVIRE;
        gs[eGELTRIB].misc[2] = eZOL;
    }

    if(zv <= 0x190)
    {
        gs[eCENT1].misc[2] = 0;
        gs[eCENT2].misc[2] = 0;
        gs[eMOLDORM].misc[1] = 0;
    }
    else if(zv <= 0x210)
    {
        gs[eCENT1].misc[2] = 1;
        gs[eCENT2].misc[2] = 1;
        gs[eMOLDORM].misc[1] = 0;
    }

    if(zv < 0x211)
    {
        const int keese[4] = { eKEESE1, eKEESE2, eKEESE3, eKEESETRIB };

        for(int k = 0; k < 4; ++k)
        {
            gs[keese[k]].misc[15] = 120;
            gs[keese[k]].misc[16] = 16;
        }

        gs[ePEAHAT].misc[15] = 80;
        gs[ePEAHAT].misc[16] = 16;
        gs[eGHINI2].misc[15] = 120;
        gs[eGHINI2].misc[16] = 10;

        // replay_version_check(20) is true in the editor
        const int fly[] = { eGHINI2, eMOLDORM, eKEESETRIB, eKEESE3, eKEESE2, eKEESE1, eTEK1, eTEK2, ePEAHAT, eROCK,
                            eTRAP, eWALLM, ePOLSV, eMANHAN, eGLEEOK1, eGLEEOK2, eGLEEOK3, eGLEEOK4, eDIG1, eDIG3,
                            eDIGPUP1, eDIGPUP2, eDIGPUP3, eDIGPUP4, eRAQUAM, eITEMFAIRY, eFIRE, eMANHAN2, eTRAP_H,
                            eTRAP_V, eTRAP_LR, eTRAP_UD, ePATRA1, ePATRA2, ePATRABS, eBAT, eGLEEOK1F, eGLEEOK2F,
                            eGLEEOK3F, eGLEEOK4F, eTRIGGER, ePATRAL2, ePATRAL3, eGOHMA1, eGOHMA2, eGOHMA3, eGOHMA4,
                            eMPOLSV, eWPOLSV };

        for(size_t k = 0; k < sizeof(fly) / sizeof(fly[0]); ++k)
            gs[fly[k]].moveflags |= MF_CAN_WATERWALK | MF_CAN_PITWALK;
    }
}

// ---- one enemy's record in an old GUY section (versions 4 to 24) -------------------------

struct Bytes
{
    const std::vector<byte> &b;
    size_t pos;
    bool bad;

    Bytes(const std::vector<byte> &data, size_t at) : b(data), pos(at), bad(false) {}

    dword get(int n)
    {
        if(pos + n > b.size())
        {
            bad = true;
            pos += n;
            return 0;
        }

        dword v = (dword)get_le(b, pos, n);
        pos += n;
        return v;
    }

    short s16()
    {
        return (short)(word)get(2);
    }
};

// The first part of upstream's record reading: the fields an old record holds, with the
// conversions made while reading them.
void read_old_fields(Bytes &r, UGuy &g, const UCtx &c, int i)
{
    const int v = c.v;
    g.flags = r.get(4);
    g.flags2 = r.get(4);
    g.tile = (word)r.get(2);
    g.width = (byte)r.get(1);
    g.height = (byte)r.get(1);
    g.s_tile = (word)r.get(2);
    g.s_width = (byte)r.get(1);
    g.s_height = (byte)r.get(1);
    g.e_tile = (word)r.get(2);
    g.e_width = (byte)r.get(1);
    g.e_height = (byte)r.get(1);
    g.hp = r.s16();
    g.family = r.s16();

    if(v < 9 && (i == eDKNUT1 || i == eDKNUT2 || i == eDKNUT3 || i == eDKNUT4 || i == eDKNUT5))
    {
        if(c.newenemytiles)
        {
            g.s_tile = g.e_tile + 120;
            g.s_width = g.e_width;
            g.s_height = g.e_height;
        }
        else
            g.s_tile = 860;
    }

    g.cset = r.s16();
    g.anim = r.s16();
    g.e_anim = r.s16();
    g.frate = r.s16();
    g.e_frate = r.s16();

    if(v < 13 && c.dep_slowanim)
    {
        g.frate *= 2;
        g.e_frate *= 2;
    }

    if(v < 14)
    {
        if(g.anim == a2FRMSLOW)
        {
            g.anim = a2FRM;
            g.frate *= 2;
        }

        if(g.e_anim == a2FRMSLOW)
        {
            g.e_anim = a2FRM;
            g.e_frate *= 2;
        }

        if(g.anim == aFLIPSLOW)
        {
            g.anim = aFLIP;
            g.frate *= 2;
        }

        if(g.e_anim == aFLIPSLOW)
        {
            g.e_anim = aFLIP;
            g.e_frate *= 2;
        }

        if(g.anim == aNEWDWALK)
            g.anim = a4FRM4DIR;

        if(g.e_anim == aNEWDWALK)
            g.e_anim = a4FRM4DIR;

        if(g.anim == aNEWPOLV || g.anim == a4FRM3TRAP)
        {
            g.anim = a4FRM4DIR;
            g.s_tile = (c.newenemytiles ? g.e_tile : g.tile) + 20;
        }

        if(g.e_anim == aNEWPOLV || g.e_anim == a4FRM3TRAP)
        {
            g.e_anim = a4FRM4DIR;
            g.s_tile = (c.newenemytiles ? g.e_tile : g.tile) + 20;
        }
    }

    g.dp = r.s16();

    if(v < 6 && i == gFIRE)
        g.dp = 2;

    g.wdp = r.s16();
    g.weapon = r.s16();

    if(v < 5 && (i == eLAQUAM || i == eRAQUAM || i == eGOHMA1 || i == eGOHMA2 || i == eGOHMA3 || i == eGOHMA4)
       && g.weapon == ewFireball)
        g.weapon = ewFireball2;

    g.rate = r.s16();
    g.hrate = r.s16();
    g.step = r.s16();

    if(c.polsvoice && g.family == eePOLSV)
        g.step /= 2;

    g.homing = r.s16();
    g.grumble = r.s16();
    g.item_set = r.s16();

    if(v >= 22)
    {
        for(int j = 0; j < 10; ++j)
            g.misc[j] = (long)r.get(4);
    }
    else
    {
        for(int j = 0; j < 10; ++j)
            g.misc[j] = r.s16();

        if(v < 13 && g.family == eeWIZZ && !g.misc[0])
            g.misc[4] = 74;
    }

    g.bgsfx = r.s16();
    g.bosspal = r.s16();
    g.extend = r.s16();

    if(v >= 16)
        for(int j = 0; j < edefLAST; ++j)
            g.defense[j] = (byte)r.get(1);

    if(v >= 18)
    {
        g.hitsfx = (byte)r.get(1);
        g.deadsfx = (byte)r.get(1);
    }

    if(v >= 22)
    {
        g.misc[10] = (long)r.get(4);
        g.misc[11] = (long)r.get(4);
    }
    else if(v >= 19)
    {
        g.misc[10] = r.s16();
        g.misc[11] = r.s16();
    }
}

// The rest of upstream's record reading, for a record of section version 24 or older: the
// fields that version doesn't hold, and the conversions made after reading.
void convert_old_record(UGuy &g, const UCtx &c, int i)
{
    const int v = c.v;
    long *m = g.misc;

    // v <= 24: the ten script defences take the one 2.53 has
    for(int j = U_edefSCRIPT01; j <= U_edefSCRIPT10; ++j)
        g.defense[j] = g.defense[edefSCRIPT];

    g.wpnsprite = 0;
    g.SIZEflags = 0;
    g.frozentile = g.frozencset = g.frozenclock = 0;

    for(int q = 0; q < 10; ++q)
        g.frozenmisc[q] = 0;

    g.editorflags = 0;

    if(!is30(c))
    {
        for(int q = 0; q < 8; ++q)
        {
            sprintf(g.initD_label[q], "InitD[%d]", q);
            sprintf(g.weapon_initD_label[q], "InitD[%d]", q);
        }
    }

    g.weaponscript = 0;

    // weapon sprites before 2.54
    if(g.wpnsprite <= 0)
    {
        switch(g.weapon)
        {
        case wNone:
            g.wpnsprite = 0;
            break;

        case wSword: case wBeam: case wBrang: case wBomb: case wSBomb: case wLitBomb: case wLitSBomb:
        case wArrow: case wFire: case wWhistle: case wBait: case wWand: case wMagic: case wCatching:
        case wWind: case wRefMagic: case wRefFireball: case wRefRock: case wHammer: case wHookshot:
        case wHSHandle: case wHSChain: case wSSparkle: case wFSparkle: case wSmack: case wPhantom:
        case wCByrna: case wRefBeam: case wStomp: case lwMax: case wScript1: case wScript2: case wScript3:
        case wScript4: case wScript5: case wScript6: case wScript7: case wScript8: case wScript9:
        case wScript10: case U_wIce:
            g.wpnsprite = -1;
            break;

        case wEnemyWeapons:
        case ewFireball: g.wpnsprite = 17; break;
        case ewArrow: g.wpnsprite = 19; break;
        case ewBrang: g.wpnsprite = 4; break;
        case ewSword: g.wpnsprite = 20; break;
        case ewRock: g.wpnsprite = 18; break;
        case ewMagic: g.wpnsprite = 21; break;
        case ewBomb: g.wpnsprite = 78; break;
        case ewSBomb: g.wpnsprite = 79; break;
        case ewLitBomb: g.wpnsprite = 76; break;
        case ewLitSBomb: g.wpnsprite = 77; break;
        case ewFireTrail: g.wpnsprite = 80; break;
        case ewFlame: g.wpnsprite = 35; break;
        case ewWind: g.wpnsprite = 36; break;
        case ewFlame2: g.wpnsprite = 81; break;
        case ewFlame2Trail: g.wpnsprite = 82; break;
        case ewIce: g.wpnsprite = 83; break;
        case ewFireball2: g.wpnsprite = 17; break;
        default: break;
        }
    }

    // fire sounds before 2.54, and nothing in the fields 2.54 added
    for(int q = 0; q < 32; ++q)
        g.movement[q] = g.new_weapon[q] = 0;

    g.script = 0;

    for(int q = 0; q < 8; ++q)
        g.initD[q] = 0;

    g.initA[0] = g.initA[1] = 0;

    for(int q = 15; q < 32; ++q)
        m[q] = 0;

    if(g.firesfx <= 0)
    {
        switch(g.weapon)
        {
        case wNone:
            g.firesfx = 0;
            break;

        case wSword: case wBeam: case wBrang: case wBomb: case wSBomb: case wLitBomb: case wLitSBomb:
        case wArrow: case wFire: case wWhistle: case wBait: case wWand: case wMagic: case wCatching:
        case wWind: case wRefMagic: case wRefFireball: case wRefRock: case wHammer: case wHookshot:
        case wHSHandle: case wHSChain: case wSSparkle: case wFSparkle: case wSmack: case wPhantom:
        case wCByrna: case wRefBeam: case wStomp: case lwMax: case wScript1: case wScript2: case wScript3:
        case wScript4: case wScript5: case wScript6: case wScript7: case wScript8: case wScript9:
        case wScript10: case U_wIce:
            g.firesfx = -1;
            break;

        case wEnemyWeapons:
        case ewFireball: g.firesfx = 40; break;
        case ewArrow: g.firesfx = 1; break;
        case ewBrang: g.firesfx = 4; break;
        case ewSword: g.firesfx = 20; break;
        case ewRock: g.firesfx = 51; break;
        case ewMagic: g.firesfx = 32; break;
        case ewBomb: g.firesfx = 3; break;
        case ewSBomb: g.firesfx = 3; break;
        case ewLitBomb: g.firesfx = 21; break;
        case ewLitSBomb: g.firesfx = 21; break;
        case ewFireTrail: g.firesfx = 13; break;
        case ewFlame: g.firesfx = 13; break;
        case ewWind: g.firesfx = 32; break;
        case ewFlame2: g.firesfx = 13; break;
        case ewFlame2Trail: g.firesfx = 13; break;
        case ewIce: g.firesfx = 44; break;
        case ewFireball2: g.firesfx = 40; break;
        default: break;
        }
    }

    // the hit sound 2.50 and older played for every enemy
    if(c.zver <= 0x250 && g.hitsfx == 0)
        g.hitsfx = 11;

    // keese, peahat and ghini halt rates
    if(c.cv < 4)
    {
        if(g.family == eeKEESE && !m[0])
        {
            m[15] = 120;
            m[16] = 16;
        }

        if(g.family == eePEAHAT)
        {
            m[15] = 80;
            m[16] = 16;
        }

        if(g.family == eeGHINI)
        {
            m[15] = 120;
            m[16] = 10;
        }
    }

    if(v < 7 && i == eMWIZ)
    {
        m[1] = 0;
        m[3] = 1;
    }

    if(v < 8 && (i == eGLEEOK1 || i == eGLEEOK2 || i == eGLEEOK3 || i == eGLEEOK4 || i == eGLEEOK1F || i == eGLEEOK2F
                 || i == eGLEEOK3F || i == eGLEEOK4F))
    {
        m[4] = 4;
        m[5] = 8;
        m[6] = 40;
        m[7] = 168;
        m[8] = 228;

        if(i == eGLEEOK1F || i == eGLEEOK2F || i == eGLEEOK3F || i == eGLEEOK4F)
        {
            m[5] += 10;
            m[7] -= 12;
        }
    }

    if(v < 10 && c.dep_46 && g.family == eeDONGO && m[0] == 0)
        g.bosspal = spDIG;

    if(v < 11 && g.family == eeSPINTILE)
    {
        g.flags |= guy_superman;
        g.item_set = 0;
        g.step = 300;
    }

    if(v < 12)
    {
        if(c.dep_rope2flash && g.family == eeROPE)
            g.flags2 &= ~guy_flashing;

        if(c.dep_bubbleflash && g.family == eeBUBBLE)
            g.flags2 &= ~guy_flashing;

        if(g.family == eeGHINI && m[0])
        {
            if(c.dep_ghini2blink)
                g.flags2 |= guy_blinking;

            if(c.dep_phantomghini2)
                g.flags2 |= guy_transparent;
        }
    }

    if(v < 15)
    {
        if(i == gFIRE)
        {
            g.e_anim = aFLIP;
            g.e_frate = 24;
        }

        if(i == gFAIRY)
        {
            g.e_anim = a2FRM;
            g.e_frate = 16;
        }
    }

    if(v < 16)
    {
        update_guy_1(g, c);

        if(i == eMPOLSV)
        {
            g.defense[edefARROW] = edCHINK;
            g.defense[edefMAGIC] = ed1HKO;
            g.defense[edefREFMAGIC] = ed1HKO;
        }
    }

    if(v < 17 && g.family == eePROJECTILE)
        m[0] = 0;

    if(v < 18)
    {
        bool boss = (g.family == eeAQUA || g.family == eeDONGO || g.family == eeMANHAN || g.family == eeGHOMA || g.family == eeDIG
                     || g.family == eeGLEEOK || g.family == eePATRA || g.family == eeGANON || g.family == eeMOLD);

        g.hitsfx = (boss && g.family != eeMOLD && g.family != eeDONGO && g.family != eeDIG) ? WAV_GASP : 0;
        g.deadsfx = (boss && (g.family != eeDIG || m[9] == 0)) ? WAV_GASP : WAV_EDEAD;

        if(g.family == eeAQUA)
            for(int j = 0; j < edefLAST; ++j) g.defense[j] = default_guys[eRAQUAM].defense[j];
        else if(g.family == eeMANHAN)
            for(int j = 0; j < edefLAST; ++j) g.defense[j] = default_guys[eMANHAN].defense[j];
        else if(g.family == eePATRA)
            for(int j = 0; j < edefLAST; ++j) g.defense[j] = default_guys[eGLEEOK1].defense[j];
        else if(g.family == eeGHOMA)
        {
            for(int j = 0; j < edefLAST; ++j)
                g.defense[j] = default_guys[eGOHMA1].defense[j];

            g.defense[edefARROW] = ((m[0] == 3) ? edCHINKL8 : (m[0] == 2) ? edCHINKL4 : 0);

            if(m[0] == 3 && !g.weapon)
                g.weapon = ewFlame;

            m[0]--;
        }
        else if(g.family == eeGLEEOK)
        {
            for(int j = 0; j < edefLAST; ++j)
                g.defense[j] = default_guys[eGLEEOK1].defense[j];

            if(m[2] == 1 && !g.weapon)
                g.weapon = ewFlame;
        }
        else if(g.family == eeARMOS)
        {
            g.family = eeWALK;
            g.hrate = 0;
            m[9] = m[0];
            m[0] = m[1] = m[2] = m[3] = m[4] = m[5] = m[6] = m[7] = 0;
            m[8] = e9tARMOS;
        }
        else if(g.family == eeGHINI && !m[0])
        {
            g.family = eeWALK;
            g.hrate = 0;

            for(int q = 0; q < 10; ++q)
                m[q] = 0;
        }

        if(g.family == eeWALK && (g.flags2 & cmbflag_armos || g.flags2 & cmbflag_ghini))
            g.flags |= guy_fadeflicker;
        else
        {
            g.flags &= 0x0F00000F;

            // 3.0's flags are 64 bits, so its mask takes 2.55's second flags word away too
            if(is30(c))
                g.flags2 = 0;
        }
    }

    if(v < 20)
    {
        if(g.family == eeTRAP)
        {
            m[1] = m[9];

            if(m[9] >= 1)
                m[0]++;

            m[9] = 0;
        }

        if(g.weapon == ewBomb && g.family != eeROPE && (g.family != eeWALK || m[1] != e2tBOMBCHU))
            g.weapon = ewLitBomb;
        else if(g.weapon == ewSBomb && g.family != eeROPE && (g.family != eeWALK || m[1] != e2tBOMBCHU))
            g.weapon = ewLitSBomb;
    }

    if(v < 21)
    {
        if(g.family == eeKEESE || g.family == eeKEESETRIB)
        {
            if(g.family == eeKEESETRIB)
            {
                g.family = eeKEESE;
                m[1] = e2tKEESETRIB;
                m[0] = 0;
            }

            g.rate = 2;
            g.hrate = 8;
            g.homing = 0;
            g.step = (g.family == eeKEESE && m[0] ? 100 : 62);
        }
        else if(g.family == eePEAHAT || g.family == eePATRA)
        {
            if(g.family == eePEAHAT)
            {
                g.rate = 4;
                g.step = 62;
            }
            else
                g.step = 25;

            g.hrate = 8;
            g.homing = 0;
        }
        else if(g.family == eeDIG || g.family == eeMANHAN)
        {
            if(g.family == eeMANHAN)
                g.step = 50;

            g.hrate = 16;
            g.homing = 0;
        }
        else if(g.family == eeGLEEOK)
        {
            g.rate = 2;
            g.homing = 0;
            g.hrate = 9;
            g.step = 89;
        }
        else if(g.family == eeGHINI)
        {
            g.rate = 4;
            g.hrate = 12;
            g.step = 62;
            g.homing = 0;
        }

        if(g.family == eeDIG && m[9] == 1)
            g.rate = 2;
    }

    if(v < 24)
    {
        if(g.family == eeLANM)
            m[2] = 1;
        else if(g.family == eeMOLD)
            m[1] = 0;
    }

    // there was no whistle defence: only the big Digdogger reacts to it
    if(g.family != eeDIG || m[9] == 1)
        g.defense[U_edefWhistle] = edIGNORE;

    if(c.zver <= 0x210 && g.family == eeDONGO)
        g.deadsfx = 15;

    // movement flags by kind
    switch(g.family)
    {
    case eeTEK: case eePEAHAT: case eeROCK: case eeTRAP:
    case eePROJECTILE: case eeSPINTILE: case eeKEESE: case eeFIRE:
    case eeFAIRY: case eeGUY: case eeNONE: case eeZORA:
    case eeAQUA: case eeDIG: case eeGHOMA: case eeGANON:
    case eePATRA: case eeGLEEOK: case eeMOLD: case eeMANHAN:
        g.moveflags = MF_CAN_PITWALK;
        break;

    case eeLEV:
        g.moveflags = MF_CAN_PITFALL;
        break;

    case eeDONGO:
        g.moveflags = MF_OBEYS_GRAV;
        break;

    case eeLANM:
        g.moveflags = 0;
        break;

    case eeWIZZ: case eeWALLM: case eeGHINI:
        g.moveflags = MF_OBEYS_GRAV | MF_CAN_PITWALK;
        break;

    case eeWALK:
        if(m[8] == e9tPOLSVOICE || m[8] == e9tVIRE)
            break;

        g.moveflags = MF_OBEYS_GRAV | MF_CAN_PITFALL;
        break;

    default:
        if(g.family == eeOTHER || (g.family >= eeSCRIPT01 && g.family <= eeSCRIPT20)
           || (g.family >= eeFFRIENDLY01 && g.family <= eeFFRIENDLY10))
            g.moveflags = MF_OBEYS_GRAV | MF_CAN_PITFALL;

        break;
    }

    switch(g.family)
    {
    case eeTEK: case eePEAHAT: case eeROCK: case eeTRAP:
    case eePROJECTILE: case eeSPINTILE: case eeKEESE: case eeFIRE:
    case eeFAIRY: case eeGUY: case eeNONE: case eeZORA:
    case eeAQUA: case eeDIG: case eeGHOMA: case eeGANON:
    case eePATRA: case eeGLEEOK: case eeMOLD: case eeMANHAN:
    case eeWIZZ: case eeWALLM: case eeGHINI:
        g.moveflags |= MF_CAN_WATERWALK | MF_CAN_PITWALK;
        break;
    }

    if(g.family == eeGHOMA)
        g.flags |= guy_fadeinstant;

    g.spr_shadow = (g.family == eeROCK && m[9] == 1) ? iwLargeShadow : iwShadow;
    g.spr_death = iwDeath;
    g.spr_spawn = iwSpawn;

    if(g.family == eeWALK && m[8] == e9tPOLSVOICE)
        g.moveflags |= MF_CAN_WATERWALK;

    if(g.family == eeDIG && m[9] != 1)
        g.flags2 |= U_guy_ignorekill;

    if(!is30(c))
        return;

    // 3.0: bubbles' jinx targets as bit flags
    if(g.family == eeWALK && (m[6] == e7tPERMJINX || m[6] == e7tTEMPJINX || m[6] == e7tUNJINX))
    {
        switch(m[7])
        {
        case 0: m[7] = U30_e8tSWORD; break;
        case 1: m[7] = U30_e8tITEM; break;
        case 2: m[7] = U30_e8tSWORD | U30_e8tITEM; break;
        default: m[7] = 0; break;
        }
    }

    guy_update_firesfx(g, c);
    guy_silence_unplayed_firesfx(g, c);
    guy_update_weaponflags(g);
    guy_update_weaponspecialsfx(g);

    if(g.flags & guy_burning_sprites)
        g.wd.wflags |= WFLAG_UPDATE_IGNITE_SPRITE;

    g.wd.flags |= WD_SET_STEP;

    if(g.weapon == ewRock)
        g.wd.wflags |= WFLAG_BREAK_ON_SOLID;
}

// The size of one record of an old GUY section.
size_t old_record_size(int v)
{
    size_t n = 8 + 3 * (2 + 1 + 1) + 16 * 2;
    n += v >= 22 ? 10 * 4 : 10 * 2;
    n += 3 * 2;

    if(v >= 16)
        n += edefLAST;

    if(v >= 18)
        n += 2;

    if(v >= 22)
        n += 2 * 4;
    else if(v >= 19)
        n += 2 * 2;

    return n;
}

// ---- deprecated rules -----------------------------------------------------------------------

// ZQuest's buffers while its reader runs on made-up sections: put back afterwards.
struct SavedBuffers
{
    std::vector<guydata> guys;
    std::vector<std::string> names;

    SavedBuffers() : guys(guysbuf, guysbuf + MAXGUYS), names(MAXGUYS)
    {
        for(int i = 0; i < MAXGUYS; ++i)
            names[i].assign(guy_string[i], 64);
    }

    ~SavedBuffers()
    {
        for(int i = 0; i < MAXGUYS; ++i)
        {
            guysbuf[i] = guys[i];
            memcpy(guy_string[i], names[i].data(), 64);
        }
    }
};

bool run_253_reader(const std::vector<byte> &section)
{
    zquestheader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.zelda_version = 0x250;
    hdr.build = 33;
    MemFile m;
    PACKFILE *f = open_mem(m, section);

    if(!f)
        return false;

    int ret = readguys(f, &hdr, true);
    pack_fclose(f);
    return ret == 0;
}

// The deprecated rules as 2.53's loader kept them for the open quest, found through its
// enemy reader: a section that holds no enemies shows what it does to the defaults (a
// darknut's breakable shield, with its default shield taken off first), and one with a few
// old version 4 enemies shows the rest.
bool probe_rules(UCtx &c)
{
    SavedBuffers saved;
    bool ok = true;

    // version 3: the default enemies only
    {
        std::vector<byte> s;
        put_le(s, 3, 2);
        put_le(s, 3, 2);
        put_le(s, 0, 4);
        dword was = default_guys[eDKNUT1].flags;
        default_guys[eDKNUT1].flags &= ~guy_bkshield;
        ok = run_253_reader(s) && ok;
        default_guys[eDKNUT1].flags = was;
        c.dep_brkblshlds = (guysbuf[eDKNUT1].flags & guy_bkshield) != 0;
    }

    // version 4: 256 names and records, a few of them set up to show a rule
    {
        const int v = 4;
        const size_t rs = old_record_size(v);
        std::vector<byte> s;
        put_le(s, v, 2);
        put_le(s, 3, 2);
        put_le(s, 0, 4);
        s.insert(s.end(), 64 * OLDBETAMAXGUYS, (byte)0);
        size_t recs = s.size();
        s.insert(s.end(), rs * OLDBETAMAXGUYS, (byte)0);

        // fields at their offsets in a version 4 record
        const size_t F_FLAGS2 = 4, F_FAMILY = 22, F_FRATE = 30, F_E_FRATE = 32, F_MISC1 = 52, F_BOSSPAL = 74;

        struct Probe { int i, family; dword flags2; int frate; long misc1, misc2; int bosspal; };
        const Probe p[] =
        {
            { 200, eeDONGO, 0, 1, 0, 0, -1 },       // frame rate doubled: slow enemy animation; bosspal: rule 46
            { 201, eeROPE, guy_flashing, 0, 0, 0, 0 },
            { 202, eeBUBBLE, guy_flashing, 0, 0, 0, 0 },
            { 203, eeGHINI, 0, 0, 1, 0, 0 },
            { 204, 33, 0, 0, 0, 7, 0 }              // eeGELTRIB: misc3 takes misc2 with old tribbles
        };

        for(size_t k = 0; k < sizeof(p) / sizeof(p[0]); ++k)
        {
            size_t at = recs + rs * p[k].i;
            zqw_ig::set_le(s, at + F_FLAGS2, p[k].flags2, 4);
            zqw_ig::set_le(s, at + F_FAMILY, (word)p[k].family, 2);
            zqw_ig::set_le(s, at + F_FRATE, (word)p[k].frate, 2);
            zqw_ig::set_le(s, at + F_E_FRATE, (word)p[k].frate, 2);
            zqw_ig::set_le(s, at + F_MISC1, (word)p[k].misc1, 2);
            zqw_ig::set_le(s, at + F_MISC1 + 2, (word)p[k].misc2, 2);
            zqw_ig::set_le(s, at + F_BOSSPAL, (word)p[k].bosspal, 2);
        }

        ok = run_253_reader(s) && ok;
        c.dep_slowanim = guysbuf[200].frate == 2;
        c.dep_46 = guysbuf[200].bosspal == spDIG;
        c.dep_rope2flash = !(guysbuf[201].flags2 & guy_flashing);
        c.dep_bubbleflash = !(guysbuf[202].flags2 & guy_flashing);
        c.dep_ghini2blink = (guysbuf[203].flags2 & guy_blinking) != 0;
        c.dep_phantomghini2 = (guysbuf[203].flags2 & guy_transparent) != 0;
        c.dep_oldtribbles = guysbuf[204].misc3 == 7;
    }

    // Upstream's header reader takes the breakable-shield rule out of the rules of quests
    // older than 1.92 build 149 before the rules are kept aside, so for those it is off.
    if(c.zver < 0x192 || (c.zver == 0x192 && c.build < 149))
        c.dep_brkblshlds = false;

    return ok;
}

// ---- what was loaded --------------------------------------------------------------------------

guydata g_guys[MAXGUYS];
char g_names[MAXGUYS][64];

// What ZQuest's 2.53 writer does to the enemies of quests older than 2.53.0 Gamma 2 when the
// load runs it to fingerprint the part (see zqw_guys.cpp).
void snapshot()
{
    bool fill = quest_header_zelda_version < 0x250 || (quest_header_zelda_version == 0x250 && quest_header_zelda_build < 32);

    for(int i = 0; i < MAXGUYS; ++i)
    {
        g_guys[i] = guysbuf[i];
        memcpy(g_names[i], guy_string[i], 64);

        if(fill && g_guys[i].hitsfx == 0)
            g_guys[i].hitsfx = WAV_EHIT;
    }
}

ZqLoadHookRegistration g_hook(snapshot);

bool same_guy(const guydata &a, const guydata &b)
{
#define CMP(x) if(a.x != b.x) return false;
    CMP(flags) CMP(flags2) CMP(tile) CMP(width) CMP(height) CMP(s_tile) CMP(s_width) CMP(s_height)
    CMP(e_tile) CMP(e_width) CMP(e_height) CMP(hp) CMP(family) CMP(cset) CMP(anim) CMP(e_anim)
    CMP(frate) CMP(e_frate) CMP(dp) CMP(wdp) CMP(weapon) CMP(rate) CMP(hrate) CMP(step)
    CMP(homing) CMP(grumble) CMP(item_set) CMP(misc1) CMP(misc2) CMP(misc3) CMP(misc4) CMP(misc5)
    CMP(misc6) CMP(misc7) CMP(misc8) CMP(misc9) CMP(misc10) CMP(misc11) CMP(misc12) CMP(misc13)
    CMP(misc14) CMP(misc15) CMP(bgsfx) CMP(bosspal) CMP(extend) CMP(hitsfx) CMP(deadsfx)
#undef CMP

    for(int j = 0; j < edefLAST; ++j)
        if(a.defense[j] != b.defense[j])
            return false;

    return true;
}

// ---- building upstream's enemies ---------------------------------------------------------

// An enemy the user changed, as upstream reads it from a 2.53.1 quest (section version 24).
void from_changed(UGuy &g, const guydata &z, const UCtx &base, int i)
{
    UCtx c = base;
    c.zver = 0x250;
    c.build = 33;
    c.v = 24;
    c.cv = 3;
    c.polsvoice = false;
    clear_guy(g, c.target);
    take_253(g, z);
    convert_old_record(g, c, i);
}

// Upstream's name for an enemy: a name ending in a space is one of the built-in ones, and
// upstream's list of those dropped the trailing space from a few.
void upstream_name(char out[64], const char *name, int i)
{
    memset(out, 0, 64);
    size_t n = strlen(name);

    if(n > 63)
        n = 63;

    memcpy(out, name, n);

    if(i < OLDMAXGUYS && n > 0 && name[n - 1] == ' ')
    {
        memset(out, 0, 64);
        strncpy(out, old_guy_string[i], 63);
        size_t k = strlen(out);

        const int trimmed[] = { 165, 168, 169, 171, 172, 173, 174 };

        for(size_t t = 0; t < sizeof(trimmed) / sizeof(trimmed[0]); ++t)
            if(trimmed[t] == i && k > 0 && out[k - 1] == ' ')
                out[k - 1] = 0;
    }
}

// Every enemy as upstream holds it after opening the quest, with the user's changes.
int build_guys(const ZqNewerContext &ctx, std::vector<UGuy> &gs)
{
    UCtx c;
    memset(&c, 0, sizeof(c));
    c.target = ctx.target;
    c.zver = ctx.src_zelda_version;
    c.build = ctx.src_build;
    c.v = ctx.src_part ? ctx.src_version : 0;
    c.cv = ctx.src_part ? ctx.src_cversion : 0;
    c.newenemytiles = get_bit(quest_rules, qr_NEWENEMYTILES) != 0;
    c.moresounds = get_bit(quest_rules, qr_MORESOUNDS) != 0;
    c.eightway_dep = c.zver < 0x250;    // 3.0's qr_8WAY_SHOT_SFX_DEP, set for quests older than 2.50
    c.polsvoice = fixpolsvoice;

    if(!probe_rules(c))
    {
        zqw_ig_refuse("Enemies: ZQuest's own enemy reader failed while checking the quest's old rules.\n");
        return 1;
    }

    gs.assign(MAXGUYS, UGuy());
    init_guys(gs, c, c.v);
    header_fixes(gs, c, c.v);

    std::vector<byte> orig;

    if(ctx.src_part && !zqw_ig::read_part(*ctx.src_part, orig))
    {
        zqw_ig_refuse("Enemies: the quest's own enemy section couldn't be read back.\n");
        return 1;
    }

    // Records the file holds: version 4 and up, 256 of them before version 23.
    int held = c.v > 3 ? (c.v < 23 ? OLDBETAMAXGUYS : MAXGUYS) : 0;

    if(held)
    {
        size_t rs = old_record_size(c.v);
        size_t recs = 8 + 64 * (size_t)held;

        if(orig.size() != recs + rs * held)
        {
            zqw_ig_refuse("Enemies: the quest's enemy section (version %d) isn't laid out the way its reader reads it.\n", c.v);
            return 1;
        }

        for(int i = held; i < MAXGUYS; ++i)
            clear_guy(gs[i], c.target);

        for(int i = 0; i < held; ++i)
        {
            Bytes r(orig, recs + rs * i);
            clear_guy(gs[i], c.target);
            read_old_fields(r, gs[i], c, i);

            if(r.bad || r.pos != recs + rs * (i + 1))
            {
                zqw_ig_refuse("Enemies: enemy %d of the quest's enemy section couldn't be read.\n", i);
                return 1;
            }

            convert_old_record(gs[i], c, i);
        }
    }

    // ZQN_GUYS_ALL_CHANGED=1, for the checks: every enemy as if the user had changed it. A
    // quest of 2.53.0 or 2.53.1 must then still come out as upstream's conversion does.
    const char *all = getenv("ZQN_GUYS_ALL_CHANGED");
    bool force = all && *all == '1';

    for(int i = 0; i < MAXGUYS; ++i)
        if(force || !same_guy(guysbuf[i], g_guys[i]))
            from_changed(gs[i], guysbuf[i], c, i);

    return 0;
}

// ---- writing ----------------------------------------------------------------------------

void put_s(std::vector<byte> &o, long v, int n)
{
    put_le(o, (dword)v, n);
}

void write_guy_255(std::vector<byte> &o, const UGuy &g)
{
    put_s(o, g.flags, 4);
    put_s(o, g.flags2, 4);
    put_s(o, g.tile, 4);
    put_s(o, g.width, 1);
    put_s(o, g.height, 1);
    put_s(o, g.s_tile, 4);
    put_s(o, g.s_width, 1);
    put_s(o, g.s_height, 1);
    put_s(o, g.e_tile, 4);
    put_s(o, g.e_width, 1);
    put_s(o, g.e_height, 1);
    const short s[16] = { g.hp, g.family, g.cset, g.anim, g.e_anim, g.frate, g.e_frate, g.dp, g.wdp, g.weapon,
                          g.rate, g.hrate, g.step, g.homing, g.grumble, g.item_set };

    for(int j = 0; j < 16; ++j)
        put_s(o, s[j], 2);

    for(int j = 0; j < 10; ++j)
        put_s(o, g.misc[j], 4);

    put_s(o, g.bgsfx, 2);
    put_s(o, g.bosspal, 2);
    put_s(o, g.extend, 2);

    for(int j = 0; j < edefLAST; ++j)
        put_s(o, g.defense[j], 1);

    put_s(o, g.hitsfx, 1);
    put_s(o, g.deadsfx, 1);
    put_s(o, g.misc[10], 4);
    put_s(o, g.misc[11], 4);

    for(int j = edefLAST; j < U_DEFENSES; ++j)
        put_s(o, g.defense[j], 1);

    const long sz[10] = { g.txsz, g.tysz, g.hxsz, g.hysz, g.hzsz, g.hxofs, g.hyofs, g.xofs, g.yofs, g.zofs };

    for(int j = 0; j < 10; ++j)
        put_s(o, sz[j], 4);

    put_s(o, g.wpnsprite, 4);
    put_s(o, g.SIZEflags, 4);
    put_s(o, g.frozentile, 4);
    put_s(o, g.frozencset, 4);
    put_s(o, g.frozenclock, 4);

    for(int q = 0; q < 10; ++q)
        put_s(o, g.frozenmisc[q], 2);

    put_s(o, g.firesfx, 2);

    for(int q = 15; q < 32; ++q)
        put_s(o, g.misc[q], 4);

    for(int q = 0; q < 32; ++q)
        put_s(o, g.movement[q], 4);

    for(int q = 0; q < 32; ++q)
        put_s(o, g.new_weapon[q], 4);

    put_s(o, g.script, 2);

    for(int q = 0; q < 8; ++q)
        put_s(o, g.initD[q], 4);

    for(int q = 0; q < 2; ++q)
        put_s(o, g.initA[q], 4);

    put_s(o, g.editorflags, 4);
    put_s(o, g.misc[12], 4);
    put_s(o, g.misc[13], 4);
    put_s(o, g.misc[14], 4);

    for(int q = 0; q < 8; ++q)
    {
        o.insert(o.end(), g.initD_label[q], g.initD_label[q] + 65);
        o.insert(o.end(), g.weapon_initD_label[q], g.weapon_initD_label[q] + 65);
    }

    put_s(o, g.weaponscript, 2);

    for(int q = 0; q < 8; ++q)
        put_s(o, g.weap_initiald[q], 4);

    put_s(o, g.moveflags, 4);
    put_s(o, g.spr_shadow, 1);
    put_s(o, g.spr_death, 1);
    put_s(o, g.spr_spawn, 1);
}

// p_putvar(script_config): the script, all 8 run arguments, an empty map of init values
void write_script_config(std::vector<byte> &o, word script, const long *args)
{
    put_s(o, script, 2);
    put_s(o, 8, 2);

    for(int q = 0; q < 8; ++q)
        put_s(o, args[q], 4);

    put_s(o, 0, 2);
}

void write_weapon_30(std::vector<byte> &o, const UWeapon &w)
{
    put_s(o, V_WEAP_DATA_30, 2);
    put_s(o, w.flags, 2);
    put_s(o, w.moveflags, 4);
    put_s(o, w.wflags, 2);

    for(int q = 0; q < 5; ++q)
    {
        put_s(o, w.burnsprs[q], 2);
        put_s(o, w.light_rads[q], 1);
        put_s(o, w.light_offsets[q], 4);
    }

    put_s(o, w.glow_shape, 1);
    put_s(o, w.override_flags, 4);
    const long v[9] = { w.tilew, w.tileh, w.hxsz, w.hysz, w.hzsz, w.hxofs, w.hyofs, w.xofs, w.yofs };

    for(int j = 0; j < 9; ++j)
        put_s(o, v[j], 4);

    put_s(o, w.step, 4);
    put_s(o, w.unblockable, 1);
    put_s(o, w.timeout, 4);
    put_s(o, w.imitate_weapon, 1);
    put_s(o, w.default_defense, 1);
    put_s(o, w.lift_level, 1);
    put_s(o, w.lift_time, 1);
    put_s(o, w.lift_height, 4);
    write_script_config(o, w.script, w.run_args);
    put_s(o, w.pierce_count, 2);
    put_s(o, w.bounce_mult, 4);
    put_s(o, w.bounce_add, 4);
    put_s(o, w.viewport_suspend_range, 4);
    put_s(o, w.viewport_despawn_range, 4);
}

void write_guy_30(std::vector<byte> &o, const UGuy &g)
{
    put_s(o, g.flags, 4);
    put_s(o, g.flags2, 4);
    put_s(o, g.tile, 4);
    put_s(o, g.width, 1);
    put_s(o, g.height, 1);
    put_s(o, g.s_tile, 4);
    put_s(o, g.s_width, 1);
    put_s(o, g.s_height, 1);
    put_s(o, g.e_tile, 4);
    put_s(o, g.e_width, 1);
    put_s(o, g.e_height, 1);
    const short s[16] = { g.hp, g.family, g.cset, g.anim, g.e_anim, g.frate, g.e_frate, g.dp, g.wdp, g.weapon,
                          g.rate, g.hrate, g.step, g.homing, g.grumble, g.item_set };

    for(int j = 0; j < 16; ++j)
        put_s(o, s[j], 2);

    for(int j = 0; j < 10; ++j)
        put_s(o, g.misc[j], 4);

    put_s(o, g.bgsfx, 2);
    put_s(o, g.bosspal, 2);
    put_s(o, g.extend, 2);

    for(int j = 0; j < edefLAST; ++j)
        put_s(o, g.defense[j], 1);

    put_s(o, g.hitsfx, 2);
    put_s(o, g.deadsfx, 2);
    put_s(o, g.misc[10], 4);
    put_s(o, g.misc[11], 4);

    for(int j = edefLAST; j < U_DEFENSES; ++j)
        put_s(o, g.defense[j], 1);

    const long sz[10] = { g.txsz, g.tysz, g.hxsz, g.hysz, g.hzsz, g.hxofs, g.hyofs, g.xofs, g.yofs, g.zofs };

    for(int j = 0; j < 10; ++j)
        put_s(o, sz[j], 4);

    put_s(o, g.wpnsprite, 4);
    put_s(o, g.SIZEflags, 4);
    put_s(o, g.frozentile, 4);
    put_s(o, g.frozencset, 4);
    put_s(o, g.frozenclock, 4);

    for(int q = 0; q < 10; ++q)
        put_s(o, g.frozenmisc[q], 2);

    put_s(o, g.firesfx, 2);

    for(int q = 15; q < 32; ++q)
        put_s(o, g.misc[q], 4);

    for(int q = 0; q < 32; ++q)
        put_s(o, g.movement[q], 4);

    for(int q = 0; q < 32; ++q)
        put_s(o, g.new_weapon[q], 4);

    write_script_config(o, g.script, g.initD);
    put_s(o, g.editorflags, 4);
    put_s(o, g.misc[12], 4);
    put_s(o, g.misc[13], 4);
    put_s(o, g.misc[14], 4);
    put_s(o, g.moveflags, 4);
    put_s(o, g.spr_shadow, 2);
    put_s(o, g.spr_death, 2);
    put_s(o, g.spr_spawn, 2);
    put_s(o, g.specialsfx, 2);
    write_weapon_30(o, g.wd);
    put_s(o, g.viewport_suspend_range, 4);
    put_s(o, g.viewport_despawn_range, 4);
    put_s(o, g.firesfx_secondary, 2);
}

int zqn_guys(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;     // 2.55 quests saved as 3.0 come later

    // GUY version 2 holds Link's sprites; no quest has it
    if(ctx.src_part && (ctx.src_version == 2 || ctx.src_version > 24))
        return ZQW_UNSUPPORTED;

    std::vector<UGuy> gs;

    if(build_guys(ctx, gs))
        return 1;

    // Upstream's writer gives an enemy with no hit sound the old hard-coded one in quests
    // made before 2.53.0 Gamma 2.
    bool fill = ctx.src_zelda_version < 0x250 || (ctx.src_zelda_version == 0x250 && ctx.src_build < 32);
    bool t30 = ctx.target == ZQ_NEWER_30;
    std::vector<byte> out;
    put_le(out, t30 ? V_GUYS_30 : V_GUYS_255, 2);
    put_le(out, t30 ? V_GUYS_30 : CV_GUYS_255, 2);     // 3.0 writes the version again here
    put_le(out, 0, 4);

    for(int i = 0; i < MAXGUYS; ++i)
    {
        char name[64];
        upstream_name(name, guy_string[i], i);
        out.insert(out.end(), name, name + 64);
    }

    for(int i = 0; i < MAXGUYS; ++i)
    {
        if(fill && gs[i].hitsfx == 0)
            gs[i].hitsfx = WAV_EHIT;

        if(t30)
            write_guy_30(out, gs[i]);
        else
            write_guy_255(out, gs[i]);
    }

    zqw_ig::set_le(out, 4, (dword)(out.size() - 8), 4);
    return zqw_ig::write_part(f, ID_GUYS, out);
}

ZqNewerRegistration g_register(ID_GUYS, zqn_guys, "enemies for 2.55.17 and 3.0");

// ---- a quest made in 2.55 (zqn_255.h) ------------------------------------------------------
//
// Each enemy's record read with the tape (zq_tape_guy) the way readguy_single reads versions 41
// to 59, then the conversions that reader makes for records older than 3.0's version 60, in its
// order. The header fixes readguys makes before reading the records don't reach these: every
// record is read over them. Names as readguys keeps them (upstream_name).

void from_tape(UGuy &g, const TapeIndex &t, int v)
{
    char k[32];
#define T(key) t.get(key)
    g.flags = (dword)T("flags");
    g.flags2 = (dword)T("flags2");
    g.tile = T("tile");
    g.width = (byte)T("width");
    g.height = (byte)T("height");
    g.s_tile = T("s_tile");
    g.s_width = (byte)T("s_width");
    g.s_height = (byte)T("s_height");
    g.e_tile = T("e_tile");
    g.e_width = (byte)T("e_width");
    g.e_height = (byte)T("e_height");
    g.hp = (short)T("hp");
    g.family = (short)T("family");
    g.cset = (short)T("cset");
    g.anim = (short)T("anim");
    g.e_anim = (short)T("e_anim");
    g.frate = (short)T("frate");
    g.e_frate = (short)T("e_frate");
    g.dp = (short)T("dp");
    g.wdp = (short)T("wdp");
    g.weapon = (short)T("weapon");
    g.rate = (short)T("rate");
    g.hrate = (short)T("hrate");
    g.step = (short)T("step");
    g.homing = (short)T("homing");
    g.grumble = (short)T("grumble");
    g.item_set = (short)T("item_set");

    for(int j = 0; j < 32; ++j)
    {
        snprintf(k, sizeof(k), "attr%d", j);
        g.misc[j] = T(k);
    }

    g.bgsfx = (short)T("bgsfx");
    g.bosspal = (short)T("bosspal");
    g.extend = (short)T("extend");

    for(int j = 0; j < U_DEFENSES; ++j)
    {
        snprintf(k, sizeof(k), "def%d", j);
        g.defense[j] = (byte)T(k);
    }

    g.hitsfx = (word)T("hitsfx");
    g.deadsfx = (word)T("deadsfx");
    g.txsz = T("txsz"); g.tysz = T("tysz"); g.hxsz = T("hxsz"); g.hysz = T("hysz"); g.hzsz = T("hzsz");
    g.hxofs = T("hxofs"); g.hyofs = T("hyofs"); g.xofs = T("xofs"); g.yofs = T("yofs"); g.zofs = T("zofs");
    g.wpnsprite = T("wpnsprite");
    g.SIZEflags = T("sizeflags");
    g.frozentile = T("frozentile");
    g.frozencset = T("frozencset");
    g.frozenclock = T("frozenclock");

    for(int q = 0; q < 10; ++q)
    {
        snprintf(k, sizeof(k), "frozenmisc%d", q);
        g.frozenmisc[q] = (short)T(k);
    }

    g.firesfx = (short)T("firesfx");

    for(int q = 0; q < 32; ++q)
    {
        snprintf(k, sizeof(k), "movement%d", q);
        g.movement[q] = T(k);
        snprintf(k, sizeof(k), "new_weapon%d", q);
        g.new_weapon[q] = T(k);
    }

    const char *const sc = v < 59 ? "" : "sc.";
    snprintf(k, sizeof(k), "%sscript", sc);
    g.script = (word)T(k);

    for(int q = 0; q < 8; ++q)
    {
        snprintf(k, sizeof(k), "%sinitd%d", sc, q);
        g.initD[q] = T(k);
    }

    g.editorflags = T("editorflags");

    if(v >= 40 && v < 54)
    {
        g.wd.script = (word)T("w.script");

        for(int q = 0; q < 8; ++q)
        {
            snprintf(k, sizeof(k), "w.initd%d", q);
            g.wd.run_args[q] = T(k);
        }
    }

    g.moveflags = (dword)T("moveflags");
    g.spr_shadow = (word)T("spr_shadow");
    g.spr_death = (word)T("spr_death");
    g.spr_spawn = (word)T("spr_spawn");
    g.specialsfx = (word)T("specialsfx");

    if(t.find("vpsuspend"))
    {
        g.viewport_suspend_range = T("vpsuspend");
        g.viewport_despawn_range = T("vpdespawn");
    }

    g.firesfx_secondary = (short)T("firesfx2");
#undef T
}

// read_weap_data (versions 1 to 5 of a weapon's data); what a version lacks stays as cleared
void weapon_from_tape(UWeapon &w, const TapeIndex &t)
{
    char k[40], n[24];
#define W(name) (snprintf(k, sizeof(k), "wd.%s", name), t.get(k))
#define WD(name, dflt) (snprintf(k, sizeof(k), "wd.%s", name), t.find(k) ? t.get(k) : (dflt))
    w.flags = (word)W("flags");
    w.moveflags = (dword)W("moveflags");
    w.wflags = (word)W("wflags");

    for(int q = 0; q < 5; ++q)
    {
        snprintf(n, sizeof(n), "burnspr%d", q);
        w.burnsprs[q] = (word)W(n);
        snprintf(n, sizeof(n), "lightrad%d", q);
        w.light_rads[q] = (byte)W(n);
        snprintf(n, sizeof(n), "lightofs%d", q);
        w.light_offsets[q] = W(n);
    }

    w.glow_shape = (byte)W("glow");
    w.override_flags = W("override");
    w.tilew = W("tilew"); w.tileh = W("tileh"); w.hxsz = W("hxsz"); w.hysz = W("hysz"); w.hzsz = W("hzsz");
    w.hxofs = W("hxofs"); w.hyofs = W("hyofs"); w.xofs = W("xofs"); w.yofs = W("yofs");
    w.step = W("step");
    w.unblockable = (byte)W("unblockable");
    w.timeout = W("timeout");
    w.imitate_weapon = (byte)W("imitate");
    w.default_defense = (byte)W("defdef");
    w.lift_level = (byte)W("liftlvl");
    w.lift_time = (byte)W("lifttime");
    w.lift_height = W("lifthei");
    const bool old = t.find("wd.script") != NULL;

    w.script = (word)(old ? W("script") : W("sc.script"));

    for(int q = 0; q < 8; ++q)
    {
        snprintf(n, sizeof(n), old ? "initd%d" : "sc.initd%d", q);
        w.run_args[q] = W(n);
    }

    w.pierce_count = (short)W("pierce");
    w.bounce_mult = WD("bouncemult", w.bounce_mult);
    w.bounce_add = WD("bounceadd", w.bounce_add);
    w.viewport_suspend_range = WD("vpsuspend", w.viewport_suspend_range);
    w.viewport_despawn_range = WD("vpdespawn", w.viewport_despawn_range);
#undef W
#undef WD
}

void convert_255_record(UGuy &g, const UCtx &c, const TapeIndex &t)
{
    const int v = c.v;
    long *m = g.misc;

    if(c.cv < 4)
    {
        if(g.family == eeKEESE && !m[0])
            m[15] = 120, m[16] = 16;

        if(g.family == eePEAHAT)
            m[15] = 80, m[16] = 16;

        if(g.family == eeGHINI)
            m[15] = 120, m[16] = 10;
    }

    if(v < 42)
    {
        switch(g.family)
        {
        case eeTEK: case eePEAHAT: case eeROCK: case eeTRAP:
        case eePROJECTILE: case eeSPINTILE: case eeKEESE: case eeFIRE:
        case eeFAIRY: case eeGUY: case eeNONE: case eeZORA:
        case eeAQUA: case eeDIG: case eeGHOMA: case eeGANON:
        case eePATRA: case eeGLEEOK: case eeMOLD: case eeMANHAN:
            g.moveflags = MF_CAN_PITWALK;
            break;

        case eeLEV:
            g.moveflags = MF_CAN_PITFALL;
            break;

        case eeDONGO:
            g.moveflags = MF_OBEYS_GRAV;
            break;

        case eeLANM:
            g.moveflags = 0;
            break;

        case eeWIZZ: case eeWALLM: case eeGHINI:
            g.moveflags = MF_OBEYS_GRAV | MF_CAN_PITWALK;
            break;

        case eeWALK:
            if(m[8] == e9tPOLSVOICE || m[8] == e9tVIRE)
                break;

            g.moveflags = MF_OBEYS_GRAV | MF_CAN_PITFALL;
            break;

        default:
            if(g.family == eeOTHER || (g.family >= eeSCRIPT01 && g.family <= eeSCRIPT20)
               || (g.family >= eeFFRIENDLY01 && g.family <= eeFFRIENDLY10))
                g.moveflags = MF_OBEYS_GRAV | MF_CAN_PITFALL;

            break;
        }
    }

    if(v < 43)
    {
        switch(g.family)
        {
        case eeTEK: case eePEAHAT: case eeROCK: case eeTRAP:
        case eePROJECTILE: case eeSPINTILE: case eeKEESE: case eeFIRE:
        case eeFAIRY: case eeGUY: case eeNONE: case eeZORA:
        case eeAQUA: case eeDIG: case eeGHOMA: case eeGANON:
        case eePATRA: case eeGLEEOK: case eeMOLD: case eeMANHAN:
        case eeWIZZ: case eeWALLM: case eeGHINI:
            g.moveflags |= MF_CAN_WATERWALK | MF_CAN_PITWALK;
            break;
        }
    }

    if(v < 44 && g.family == eeGHOMA)
        g.flags |= guy_fadeinstant;

    if(v <= 44)
    {
        g.spr_shadow = (g.family == eeROCK && m[9] == 1) ? iwLargeShadow : iwShadow;
        g.spr_death = iwDeath;
        g.spr_spawn = iwSpawn;
    }

    if(v < 46 && g.family == eeWALK && m[8] == e9tPOLSVOICE)
        g.moveflags |= MF_CAN_WATERWALK;

    if(v < 47 && g.family == eeDIG && m[9] != 1)
        g.flags2 |= U_guy_ignorekill;

    if(v < 49 && g.family == eeWALK && (m[6] == e7tPERMJINX || m[6] == e7tTEMPJINX || m[6] == e7tUNJINX))
    {
        switch(m[7])
        {
        case 0: m[7] = U30_e8tSWORD; break;
        case 1: m[7] = U30_e8tITEM; break;
        case 2: m[7] = U30_e8tSWORD | U30_e8tITEM; break;
        default: m[7] = 0; break;
        }
    }

    if(v < 51)
        guy_update_firesfx(g, c);
    else if(v < 60)
        g.firesfx_secondary = g.firesfx;

    if(v < 60)
        guy_silence_unplayed_firesfx(g, c);

    if(v < 52)
        guy_update_weaponflags(g);
    else if(v < 54)
    {
        char k[24];
        g.wd.unblockable = (byte)t.get("w.unblockable");
        g.wd.moveflags = (dword)t.get("w.moveflags");
        g.wd.override_flags = t.get("w.override");
        g.wd.tilew = t.get("w.tilew"); g.wd.tileh = t.get("w.tileh");
        g.wd.hxsz = t.get("w.hxsz"); g.wd.hysz = t.get("w.hysz"); g.wd.hzsz = t.get("w.hzsz");
        g.wd.hxofs = t.get("w.hxofs"); g.wd.hyofs = t.get("w.hyofs");
        g.wd.xofs = t.get("w.xofs"); g.wd.yofs = t.get("w.yofs");
        g.wd.step = t.get("w.step") * 100;     // zslongToFix(step * 100)

        for(int q = 0; q < 5; ++q)
        {
            snprintf(k, sizeof(k), "w.burnspr%d", q);
            g.wd.burnsprs[q] = (word)t.get(k);
            snprintf(k, sizeof(k), "w.lightrad%d", q);
            g.wd.light_rads[q] = (byte)t.get(k);
        }
    }

    if(v < 53)
        guy_update_weaponspecialsfx(g);

    if(v >= 54)
        weapon_from_tape(g.wd, t);
    else
    {
        if(g.flags & guy_burning_sprites)
            g.wd.wflags |= WFLAG_UPDATE_IGNITE_SPRITE;

        g.wd.flags |= WD_SET_STEP;

        if(g.weapon == ewRock)
            g.wd.wflags |= WFLAG_BREAK_ON_SOLID;
    }
}

int zqn255_guys(PACKFILE *f, const ZqNewerContext &ctx)
{
    // (version 60, 3.0's own, from 3.0 prereleases: read and written again the same way)
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 41 || ctx.src_version > 60 || ctx.target != ZQ_NEWER_30)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    std::vector<byte> rules;

    if(!zqn255_rules_loaded(ctx.target, rules))
        return 2;

    UCtx c;
    memset(&c, 0, sizeof(c));
    c.target = ctx.target;
    c.zver = ctx.src_zelda_version;
    c.build = ctx.src_build;
    c.v = s.version;
    c.cv = s.cversion;
    c.moresounds = ((rules[106 >> 3] >> (106 & 7)) & 1) != 0;   // qr_MORESOUNDS
    c.eightway_dep = ((rules[277 >> 3] >> (277 & 7)) & 1) != 0; // qr_8WAY_SHOT_SFX_DEP
    const size_t names = 64 * (size_t)MAXGUYS;

    if(s.data.size() < names)
    {
        zqw_last_error = "its enemies aren't laid out as expected";
        return 2;
    }

    std::vector<byte> out;
    put_le(out, V_GUYS_30, 2);
    put_le(out, V_GUYS_30, 2);
    put_le(out, 0, 4);

    for(int i = 0; i < MAXGUYS; ++i)
    {
        char raw[64], name[64];
        memcpy(raw, &s.data[(size_t)i * 64], 64);
        raw[63] = 0;
        upstream_name(name, raw, i);
        out.insert(out.end(), name, name + 64);
    }

    size_t at = names;

    for(int i = 0; i < MAXGUYS; ++i)
    {
        Tape tape;
        TapeParser p(&s.data[0], s.data.size(), at, &tape);

        if(!zq_tape_guy(p, s.version) || p.bad)
        {
            zqw_last_error = "an enemy of its enemy section couldn't be read";
            return 2;
        }

        at = p.pos;
        const TapeIndex t(tape);
        UGuy g;
        clear_guy(g, ctx.target);
        from_tape(g, t, s.version);
        convert_255_record(g, c, t);
        write_guy_30(out, g);
    }

    if(at != s.data.size())
    {
        zqw_last_error = "its enemy section has bytes past its last enemy";
        return 2;
    }

    zqw_ig::set_le(out, 4, (dword)(out.size() - 8), 4);
    return zqw_ig::write_part(f, ID_GUYS, out);
}

ZqNewerRegistration g_register_255(ID_GUYS, zqn255_guys, "2.55 enemies as 3.0");

// A quest made in an older 2.55 build, saved as 2.55.17: each enemy's record (versions 41-47)
// gets 2.55.17's readguys conversions for its version (3.0's for the same versions, which it
// inherited), then is laid out again as version 48 (the tape's rebuild: every field kept by
// name, the newer ones at the reader's values just set).
void convert_255_17_record(Tape &t, int v, int cv)
{
    const long family = (long)t.get("family");
    const long a0 = (long)t.get("attr0"), a6 = (long)t.get("attr6"), a8 = (long)t.get("attr8"), a9 = (long)t.get("attr9");
    (void)a6;

    if(cv < 4)
    {
        if(family == eeKEESE && !a0)
            zqn255_tape_set(t, "attr15", 120), zqn255_tape_set(t, "attr16", 16);

        if(family == eePEAHAT)
            zqn255_tape_set(t, "attr15", 80), zqn255_tape_set(t, "attr16", 16);

        if(family == eeGHINI)
            zqn255_tape_set(t, "attr15", 120), zqn255_tape_set(t, "attr16", 10);
    }

    dword move = (dword)t.get("moveflags");

    if(v < 42)
    {
        switch(family)
        {
        case eeTEK: case eePEAHAT: case eeROCK: case eeTRAP:
        case eePROJECTILE: case eeSPINTILE: case eeKEESE: case eeFIRE:
        case eeFAIRY: case eeGUY: case eeNONE: case eeZORA:
        case eeAQUA: case eeDIG: case eeGHOMA: case eeGANON:
        case eePATRA: case eeGLEEOK: case eeMOLD: case eeMANHAN:
            move = MF_CAN_PITWALK;
            break;

        case eeLEV:
            move = MF_CAN_PITFALL;
            break;

        case eeDONGO:
            move = MF_OBEYS_GRAV;
            break;

        case eeLANM:
            move = 0;
            break;

        case eeWIZZ: case eeWALLM: case eeGHINI:
            move = MF_OBEYS_GRAV | MF_CAN_PITWALK;
            break;

        case eeWALK:
            if(a8 == e9tPOLSVOICE || a8 == e9tVIRE)
                break;

            move = MF_OBEYS_GRAV | MF_CAN_PITFALL;
            break;

        default:
            if(family == eeOTHER || (family >= eeSCRIPT01 && family <= eeSCRIPT20)
               || (family >= eeFFRIENDLY01 && family <= eeFFRIENDLY10))
                move = MF_OBEYS_GRAV | MF_CAN_PITFALL;

            break;
        }
    }

    if(v < 43)
    {
        switch(family)
        {
        case eeTEK: case eePEAHAT: case eeROCK: case eeTRAP:
        case eePROJECTILE: case eeSPINTILE: case eeKEESE: case eeFIRE:
        case eeFAIRY: case eeGUY: case eeNONE: case eeZORA:
        case eeAQUA: case eeDIG: case eeGHOMA: case eeGANON:
        case eePATRA: case eeGLEEOK: case eeMOLD: case eeMANHAN:
        case eeWIZZ: case eeWALLM: case eeGHINI:
            move |= MF_CAN_WATERWALK | MF_CAN_PITWALK;
            break;
        }
    }

    if(v < 46 && family == eeWALK && a8 == e9tPOLSVOICE)
        move |= MF_CAN_WATERWALK;

    zqn255_tape_set(t, "moveflags", move);

    if(v < 44 && family == eeGHOMA)
        zqn255_tape_set(t, "flags", (dword)t.get("flags") | guy_fadeinstant);

    if(v <= 44)
    {
        zqn255_tape_set(t, "spr_shadow", (family == eeROCK && a9 == 1) ? iwLargeShadow : iwShadow);
        zqn255_tape_set(t, "spr_death", iwDeath);
        zqn255_tape_set(t, "spr_spawn", iwSpawn);
    }

    if(v < 47 && family == eeDIG && a9 != 1)
        zqn255_tape_set(t, "flags2", (dword)t.get("flags2") | U_guy_ignorekill);
}

int zqn255_17_guys(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.src_version < 41 || ctx.src_version > 47 || ctx.target != ZQ_NEWER_255)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const size_t names = 64 * (size_t)MAXGUYS;

    if(s.data.size() < names)
    {
        zqw_last_error = "its enemies aren't laid out as expected";
        return 2;
    }

    std::vector<byte> out;
    put_le(out, V_GUYS_255, 2);
    put_le(out, CV_GUYS_255, 2);
    put_le(out, 0, 4);

    for(int i = 0; i < MAXGUYS; ++i)
    {
        char raw[64], name[64];
        memcpy(raw, &s.data[(size_t)i * 64], 64);
        raw[63] = 0;
        upstream_name(name, raw, i);
        out.insert(out.end(), name, name + 64);
    }

    size_t at = names;

    for(int i = 0; i < MAXGUYS; ++i)
    {
        Tape tape;
        TapeParser p(&s.data[0], s.data.size(), at, &tape);

        if(!zq_tape_guy(p, s.version) || p.bad)
        {
            zqw_last_error = "an enemy of its enemy section couldn't be read";
            return 2;
        }

        at = p.pos;
        convert_255_17_record(tape, s.version, s.cversion);
        std::vector<byte> rec;

        if(!zqn255_tape_rebuild(zq_tape_guy, tape, V_GUYS_255, rec))
        {
            zqw_last_error = "an enemy couldn't be laid out as 2.55.17's version 48";
            return 2;
        }

        out.insert(out.end(), rec.begin(), rec.end());
    }

    if(at != s.data.size())
    {
        zqw_last_error = "its enemy section has bytes past its last enemy";
        return 2;
    }

    zqw_ig::set_le(out, 4, (dword)(out.size() - 8), 4);
    return zqw_ig::write_part(f, ID_GUYS, out);
}

ZqNewerRegistration g_register_255_17(ID_GUYS, zqn255_17_guys, "2.55 enemies as 2.55.17");


} // namespace

// Enemy data (GUY section), format versions 24 to 60.
//
// Read and dropped, 2.53 has no field for these:
//   defense[18]                                  v25+  upstream's unused edefLAST250 slot
//   defense[20-29]                               v25+  script weapons 1-10
//   defense[30-40]                               v25+  ice, bait, wind, sparkle, sonic, whistle,
//                                                      switchhook, thrown, refl. arrow, fire, fire 2
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
static short map_anim(short a)
{
    if(a >= aMAX && a <= aMAX + 4)
        return a4FRM8DIRB;

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
static long g_npc_initd[8], g_npc_weapon_initd[8];

// read_weap_data: every field of the block, which carries its own version.
static void skip_weapon_data(Q255Section &s)
{
    word *script_out = &g_npc_weapon_script;
    long *initd_out = g_npc_weapon_initd;

    word v = s.igetw();

    s.skip(2);      // flags
    s.skip(4);      // moveflags
    s.skip(2);      // wflags

    for(int q = 0; q < FILE_WPNSPR_MAX && !s.failed; ++q)
    {
        s.skip(v < 1 ? 1 : 2);     // burn sprite
        s.skip(1);                 // light radius

        if(v >= 3)
            s.skip(4);             // light offset
    }

    s.skip(1);      // glow shape
    s.skip(4);      // override flags
    s.skip(9 * 4);  // tile and hit sizes, hit and draw offsets
    s.skip(4);      // step
    s.skip(1);      // unblockable
    s.skip(4);      // timeout
    s.skip(2);      // imitate weapon, default defense
    s.skip(2);      // lift level, lift time
    s.skip(4);      // lift height

    if(v < 5)
    {
        *script_out = s.igetw();
        for(int i = 0; i < 8; ++i)
            initd_out[i] = s.igetl_s();
    }
    else
        read_script_config(s, script_out, initd_out);

    s.skip(2);      // pierce count

    if(v >= 2)
        s.skip(2 * 4);         // bounce mult, bounce add

    if(v >= 4)
        s.skip(2 * 4);         // viewport suspend, despawn ranges
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
    for(int j = 0; j < FILE_DEFENSES_FIRST; ++j)
    {
        byte d = s.getc();

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

            if(j == FILE_QUAKE)
                g.defense[edefQUAKE] = map_defense(d);
        }
    }

    if(v > 25)
        s.skip(5 * 4);     // tile and hit sizes

    if(v > 26)
        s.skip(5 * 4);     // hit and draw offsets

    if(v > 27)
        s.skip(4);         // wpnsprite

    if(v > 28)
        s.skip(4);         // SIZEflags

    if(v >= 30)
        s.skip(3 * 4 + 10 * 2);    // frozen tile, cset, clock, misc

    if(v >= 34)
    {
        s.skip(2);         // firesfx
        s.skip(17 * 4);    // attributes 15-31
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
        s.skip(4);         // editorflags

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
        s.skip(4);         // moveflags
    else if(v >= 42)
        s.skip(1);

    if(v > 55)
        s.skip(3 * 2);     // shadow, death and spawn sprites
    else if(v > 44)
        s.skip(3);

    if(v >= 52 && v < 54)
    {
        s.skip(1);                         // unblockable
        s.skip(12 * 4);                    // moveflags, override flags, sizes, offsets, step
        s.skip(FILE_WPNSPR_MAX * (2 + 2)); // burn sprites, light radii
    }

    if(v >= 55)
        s.skip(2);         // specialsfx
    else if(v >= 53)
        s.skip(1);

    if(v >= 54)
        skip_weapon_data(s);

    if(v >= 58)
        s.skip(2 * 4);     // viewport suspend, despawn ranges

    if(v >= 60)
        s.skip(2);         // firesfx_secondary

    // Older quests relied on the hardcoded hit sound (WAV_EHIT).
    if((s.header->zelda_version <= 0x250 || v < 35) && g.hitsfx == 0)
        g.hitsfx = 11;

    if(v < 44 && g.family == eeGHOMA)
        g.flags |= guy_fadeinstant;

    if(v < 47 && g.family == eeDIG && g.misc10 != 1)
        g.flags2 |= FILE_FLAG2_IGNORE_KILL_ALL;

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
    }

    for(int i = 0; i < FILE_GUYS; ++i)
    {
        guydata g;
        g_npc_script = g_npc_weapon_script = 0;
        memset(g_npc_initd, 0, sizeof(g_npc_initd));
        memset(g_npc_weapon_initd, 0, sizeof(g_npc_weapon_initd));
        read_guy(s, g);

        if(s.failed)
            return qe_invalid;

        if(s.keep && i < MAXGUYS)
        {
            guysbuf[i] = g;
            q30_set_guy_script(i, g_npc_script, g_npc_initd);
            q30_set_guy_weapon_script(i, g_npc_weapon_script, g_npc_weapon_initd);
        }
    }

    return 0;
}

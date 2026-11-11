// Saving an older quest as 2.55.17 or 3.0 Nightly: the combo (CMBO) and combo alias (CMBA)
// sections. See zq_writers.h.
//
// Quests made in 2.53.1 or older hold combos in the old flat format (CMBO v7 and before, or in a
// pre-1.93 quest's body). Upstream's loader reads them with readcombos_old and fills its newer
// fields from the combo's type: attributes for liquid, tall grass, lock blocks and chests, the
// chest's "uses an item" flag, a trigger for script combos, a landing sound for liquid in 3.0.
// ZQuest's 2.53 load holds the same combos before those conversions, so they are made here.
//
// Only quests made in 2.53.1 or older are handled; a 2.55 quest saved as 3.0 is not yet.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "tiles.h"
#include "zq_writers.h"

extern newcombo *combobuf;
extern combo_alias combo_aliases[MAXCOMBOALIASES];
extern byte quest_rules[QUESTRULES_SIZE];

namespace
{

enum { ERR_WRITE = 3 };

struct Out
{
    std::vector<byte> b;

    void p8(byte v) { b.push_back(v); }
    void p16(word v) { p8((byte)v); p8((byte)(v >> 8)); }
    void p32(dword v) { p16((word)v); p16((word)(v >> 16)); }
};

int put_section(PACKFILE *f, dword id, const ZqNewerContext &ctx, word v255, word v30, const Out &data)
{
    const word v = ctx.target == ZQ_NEWER_30 ? v30 : v255;
    Out h;
    h.p16(v);
    h.p16(ctx.target == ZQ_NEWER_30 ? v : 1);     // 3.0 writes the version again in its place
    h.p32((dword)data.b.size());

    if(pack_mputl((long)id, f) == EOF || pack_fwrite(&h.b[0], 8, f) != 8)
        return ERR_WRITE;

    if(!data.b.empty() && pack_fwrite((void *)&data.b[0], (long)data.b.size(), f) != (long)data.b.size())
        return ERR_WRITE;

    return ZQW_OK;
}

// ---- upstream's numbers -----------------------------------------------------------------------

// Combo types past 2.53's (2.53 ends at cSCRIPT5); a file can hold them as plain numbers.
enum
{
    UP_cSCRIPT1 = 142, UP_cSCRIPT10 = 151, UP_cTRIGGERGENERIC = 162, UP_cCSWITCH = 167
};

const int UP_WAV_DOOR = 9;
const int UP_WAV_ZN1SPLASH = 55;
const int UP_iwTallGrass = 55;
const int UP_iwRipples = 56;
const dword UP_cflag7 = 0x40;
const dword ZF = 10000;            // a zfix's whole unit

// ---- what upstream holds of an old combo --------------------------------------------------------

// The fields upstream's 2.55 and 3.0 combos have that an old quest's loader sets. Attributes are
// kept in 3.0's single table of 24 zfixes (stored as raw values): 0-3 the old attributes, 8-15
// the old attribytes, 16-23 the old attrishorts; 2.55 writes the same values in its own fields.
struct UpCombo
{
    int tile;
    byte flip, walk, type, csets, flag;
    byte frames, speed, nextcset, skipanim, skipanimy, animflags;
    word nextcombo;
    int attr[24];
    dword usrflags;
    bool typefx_trigger;        // a trigger with only "combo type effects" on (script combos)
    word sfx_landing;           // 3.0 only
};

tile_id swap_goriya(tile_id t)
{
    switch(t)
    {
    case 130: return 132;
    case 131: return 133;
    case 132: return 130;
    case 133: return 131;
    }

    return t;
}

// Combo tiles as loaded (or as last saved in the quest's own version): 2.53's loader leaves
// the Goriya tiles of a quest before 1.92 as they are, upstream's swaps them as it does for
// 1.92 to 2.11 build 6, so a tile still as the file holds it is swapped here.
std::vector<tile_id> g_tiles_loaded;

void note_combo_tiles()
{
    reset_combo_animations();
    reset_combo_animations2();
    g_tiles_loaded.resize(MAXCOMBOS);

    for(int i = 0; i < MAXCOMBOS; ++i)
        g_tiles_loaded[i] = combobuf[i].tile;
}

ZqLoadHookRegistration g_hook_combo_tiles(note_combo_tiles);

// sv: the CMBO section version the quest was loaded from (0 before 1.93).
void up_combo(const newcombo &c, int index, const ZqNewerContext &ctx, word sv, UpCombo &u)
{
    memset(&u, 0, sizeof(u));
    u.tile = c.tile;

    if(ctx.src_zelda_version < 0x192 && !get_bit(quest_rules, qr_NEWENEMYTILES) &&
       index < (int)g_tiles_loaded.size() && g_tiles_loaded[index] == c.tile)
        u.tile = swap_goriya(c.tile);

    u.flip = c.flip;
    u.walk = (byte)(c.walk | 0xF0);     // the effect squares, which old combos didn't have
    u.type = c.type;
    u.csets = c.csets;
    u.flag = c.flag;
    u.frames = c.frames;
    u.speed = c.speed;
    u.nextcombo = c.nextcombo;
    u.nextcset = c.nextcset;
    u.skipanim = c.skipanim;
    u.skipanimy = c.skipanimy;
    u.animflags = c.animflags;

    // In the order readcombos_old makes them; every one of them applies to a combo from
    // before CMBO v8, as every 2.53-or-older combo is.
    if(sv < 23 && ((u.type >= UP_cSCRIPT1 && u.type <= UP_cSCRIPT10) || u.type == UP_cTRIGGERGENERIC ||
                   u.type == UP_cCSWITCH))
        u.typefx_trigger = true;

    if(sv < 25 && (u.type == cLOCKBLOCK || u.type == cBOSSLOCKBLOCK))
        u.attr[8 + 3] = UP_WAV_DOOR * ZF;

    if(sv < 28 && (u.type == cCHEST || u.type == cLOCKEDCHEST || u.type == cBOSSCHEST))
    {
        u.attr[16 + 2] = -1 * (int)ZF;
        u.usrflags |= UP_cflag7;
    }

    if(sv < 40)
    {
        if(u.type == cWATER || u.type == cSHALLOWWATER)
            u.attr[8 + 6] = UP_iwRipples * ZF;
        else if(u.type == cTALLGRASS || u.type == cTALLGRASSNEXT || u.type == cTALLGRASSTOUCHY)
            u.attr[8 + 6] = UP_iwTallGrass * ZF;
    }

    if(sv < 49 && (u.type == cWATER || u.type == cSHALLOWWATER))
        u.sfx_landing = UP_WAV_ZN1SPLASH;

    if(sv < 16 && u.type == cWATER)
        u.attr[0] = 4 * ZF;
}

bool any_attr(const UpCombo &u)
{
    for(int q = 0; q < 24; ++q)
        if(u.attr[q])
            return true;

    return false;
}

bool any_anim(const UpCombo &u)
{
    return u.frames || u.speed || u.nextcombo || u.nextcset || u.skipanim || u.skipanimy || u.animflags;
}

bool any_basic(const UpCombo &u)
{
    return u.tile || u.flip || u.walk != 0xF0 || u.type || u.csets;
}

void put_basic(Out &o, const UpCombo &u)
{
    o.p32((dword)u.tile);
    o.p8(u.flip);
    o.p8(u.walk);
    o.p8(u.type);
    o.p8(u.flag);
    o.p8(u.csets);
}

void put_anim(Out &o, const UpCombo &u)
{
    o.p8(u.frames);
    o.p8(u.speed);
    o.p16(u.nextcombo);
    o.p8(u.nextcset);
    o.p8(u.skipanim);
    o.p8(u.skipanimy);
    o.p8(u.animflags);
}

// ---- CMBO for 2.55.17 (writecombo_loop, section v45) -----------------------------------------

enum { CHAS_ATTRIB = 0x01, CHAS_FLAG = 0x02, CHAS_TRIG = 0x04, CHAS_ANIM = 0x08, CHAS_BASIC = 0x20,
       CHAS_GENERAL = 0x80 };

void put_combo_255(Out &o, const UpCombo &u)
{
    byte has = 0;

    if(any_attr(u)) has |= CHAS_ATTRIB;
    if(u.typefx_trigger) has |= CHAS_TRIG;
    if(u.usrflags) has |= CHAS_FLAG;
    if(any_anim(u)) has |= CHAS_ANIM;
    if(any_basic(u)) has |= CHAS_BASIC;

    o.p8(has);

    if(has & CHAS_BASIC)
        put_basic(o, u);

    if(has & CHAS_ANIM)
        put_anim(o, u);

    if(has & CHAS_ATTRIB)
    {
        for(int q = 0; q < 4; ++q) o.p32((dword)u.attr[q]);                 // attributes, raw
        for(int q = 0; q < 8; ++q) o.p8((byte)(u.attr[8 + q] / (int)ZF));   // attribytes
        for(int q = 0; q < 8; ++q) o.p16((word)(u.attr[16 + q] / (int)ZF)); // attrishorts
    }

    if(has & CHAS_FLAG)
    {
        o.p32(u.usrflags);
        o.p16(0);           // genflags
    }

    if(has & CHAS_TRIG)
    {
        o.p32(0x10000000);  // triggerflags[0]: combotriggerCMBTYPEFX
        for(int q = 1; q < 6; ++q) o.p32(0);
        o.p32(0);           // triggerlevel
        o.p8(0);            // triggerbtn
        o.p8(0);            // triggeritem
        o.p8(0);            // trigtimer
        o.p8(0);            // trigsfx
        o.p32(0);           // trigchange
        o.p16(0);           // trigprox
        o.p8(0);            // trigctr
        o.p32(0);           // trigctramnt
        o.p8(0);            // triglbeam
        o.p8(0);            // trigcschange
        o.p16(0);           // spawnitem
        o.p16(0);           // spawnenemy
        o.p8(0xFF);         // exstate -1
        o.p32(0);           // spawnip
        o.p8(0);            // trigcopycat
        o.p8(0);            // trigcooldown
        o.p16(0);           // prompt_cid
        o.p8(0);            // prompt_cs
        o.p16(12);          // prompt_x
        o.p16((word)-8);    // prompt_y
        o.p8(0);            // trig_lstate
        o.p8(0);            // trig_gstate
        o.p32(0);           // trig_statetime
        o.p16(0);           // trig_genscr
        o.p8(0);            // trig_group
        o.p16(0);           // trig_group_val
        o.p8(0xFF);         // exdoor_dir -1
        o.p8(0);            // exdoor_ind
    }
}

// ---- CMBO for 3.0 (writecombo_loop, section v73) ---------------------------------------------

enum { CHAS30_GENERAL = 0x0080 };

// A bitstring with nothing set: its size (65535), "pairs", no pairs.
void put_empty_bitstr(Out &o)
{
    o.p16(0xFFFF);
    o.p8(1);
    o.p16(0);
}

// The one trigger an old script combo gets: every field at its default, the trigger flags with
// only TRIGFLAG_CMBTYPEFX (bit 28) on (writecombo_triggers_loop).
void put_typefx_trigger(Out &o)
{
    o.p8(0);                    // label
    o.p16(0xFFFF);              // trigger_flags: size, pairs, one pair: byte 3 = 0x10
    o.p8(1);
    o.p16(1);
    o.p16(3);
    o.p8(0x10);
    o.p32(0);                   // triggerlevel
    o.p8(0);                    // triggerbtn
    o.p16(0);                   // triggeritem
    o.p16(0);                   // trigtimer
    o.p16(0);                   // trigsfx
    o.p32(0);                   // trigchange
    o.p16(0);                   // trigprox
    o.p16(0);                   // trigctr
    o.p32(0);                   // trigctramnt
    o.p8(0);                    // triglbeam
    o.p8(0);                    // trigcschange
    o.p32(0);                   // spawnitem
    o.p16(0);                   // spawnenemy
    o.p8(0xFF);                 // exstate
    o.p32(0);                   // spawnip
    o.p8(0);                    // trigcopycat
    o.p8(0);                    // trigcooldown
    o.p16(0);                   // prompt_cid
    o.p8(0);                    // prompt_cs
    o.p16(12);                  // prompt_x
    o.p16((word)-8);            // prompt_y
    o.p8(0);                    // trig_lstate
    o.p8(0);                    // trig_gstate
    o.p32(0);                   // trig_statetime
    o.p16(0);                   // trig_genscr
    o.p8(0);                    // trig_group
    o.p16(0);                   // trig_group_val
    o.p8(0xFF);                 // exdoor_dir
    o.p8(0);                    // exdoor_ind
    o.p16(0);                   // trig_levelitems
    o.p16((word)-1);            // trigdmlevel
    for(int q = 0; q < 3; ++q) o.p16(0);   // trigtint
    o.p16((word)-1);            // triglvlpalette
    o.p16((word)-1);            // trigbosspalette
    o.p16((word)-1);            // trigquaketime
    o.p16((word)-1);            // trigwavytime
    o.p16((word)-2);            // trig_swjinxtime
    o.p16((word)-2);            // trig_itmjinxtime
    o.p16((word)-2);            // trig_stuntime
    o.p16((word)-2);            // trig_bunnytime
    o.p8(8);                    // trig_pushtime
    o.p16((word)-2);            // trig_shieldjinxtime
    o.p32(0);                   // req_level_state
    o.p32(0);                   // unreq_level_state
    put_empty_bitstr(o);        // req_global_state
    put_empty_bitstr(o);        // unreq_global_state
    o.p16(0);                   // fail_prompt_cid
    o.p8(0);                    // fail_prompt_cs
    o.p32(0);                   // trig_msgstr
    o.p32(0);                   // fail_msgstr
    o.p32(0);                   // player_bounce
    o.p32(0);                   // req_player_z
    o.p8(0);                    // req_player_dir
    o.p32(0);                   // dest_player_x
    o.p32(0);                   // dest_player_y
    o.p32(0);                   // dest_player_z
    o.p32(0);                   // req_player_jump
    o.p32(0);                   // req_player_x
    o.p32(0);                   // req_player_y
    o.p8(0xFF);                 // dest_player_dir
    o.p32((dword)-1);           // force_ice_combo
    o.p32(0);                   // force_ice_vx
    o.p32(0);                   // force_ice_vy
    o.p32(0);                   // trig_gravity
    o.p32(0);                   // trig_terminal_v
    for(int q = 0; q < 4; ++q) put_empty_bitstr(o);    // req/unreq screen state, ex state
    o.p8(0);                    // trigstatemap
    o.p8(0);                    // trigstatescreen
    o.p32((dword)-2);           // play_music
    o.p8(0xFF);                 // set_music_refresh
    o.p16(0);                   // viewport_cond_range
    o.p32(1);                   // chance_numerator
    o.p32(1);                   // chance_denominator
    o.p16((word)-1);            // sfx_pan
    o.p8(100);                  // sfx_volume
    o.p32((dword)-1);           // sfx_frequency
    for(int q = 0; q < 8; ++q) put_empty_bitstr(o);    // req/unreq screen ex door, each direction
    o.p8(0);                    // combopos_state
    o.p8(0);                    // large_combo_copycat
}

void put_combo_30(Out &o, const UpCombo &u)
{
    word has = 0;

    if(any_attr(u)) has |= CHAS_ATTRIB;
    if(u.typefx_trigger) has |= CHAS_TRIG;
    if(u.usrflags) has |= CHAS_FLAG;
    if(any_anim(u)) has |= CHAS_ANIM;
    if(any_basic(u)) has |= CHAS_BASIC;
    if(u.sfx_landing) has |= CHAS30_GENERAL;

    o.p16(has);

    if(has & CHAS_BASIC)
        put_basic(o, u);

    if(has & CHAS_ANIM)
    {
        put_anim(o, u);
        o.p16(0);           // delay
        o.p8(0);            // cs2_flash_duration
    }

    if(has & CHAS_ATTRIB)
    {
        o.p16(24);
        for(int q = 0; q < 24; ++q) o.p32((dword)u.attr[q]);
    }

    if(has & CHAS_FLAG)
    {
        o.p32(u.usrflags);
        o.p16(0);           // genflags
    }

    if(has & CHAS_TRIG)
    {
        o.p8(1);
        put_typefx_trigger(o);
        o.p8(0);            // only_gentrig
    }

    if(has & CHAS30_GENERAL)
    {
        o.p8(1);            // speed_mult
        o.p8(1);            // speed_div
        o.p32(0);           // speed_add
        for(int q = 0; q < 10; ++q) o.p16(0);  // sfx appear .. tap, sprites appear .. standing
        o.p16(u.sfx_landing);
        for(int q = 0; q < 6; ++q) o.p16(0);   // falling/drowning/lava sprites and sounds
        o.p32(0);           // z_height
        o.p32(0);           // z_step_height
        o.p8(0);            // dive_under_level
        o.p8(0);            // large_combo_dirs
    }
}

int zqn_combos(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    const word sv = ctx.src_zelda_version > 0x192 ? ctx.src_version : 0;
    reset_combo_animations();
    reset_combo_animations2();
    const int count = count_combos();
    Out o;
    o.p16((word)count);
    UpCombo u;

    for(int i = 0; i < count; ++i)
    {
        up_combo(combobuf[i], i, ctx, sv, u);

        if(ctx.target == ZQ_NEWER_30)
            put_combo_30(o, u);
        else
            put_combo_255(o, u);
    }

    return put_section(f, ID_COMBOS, ctx, 45, 73, o);
}

ZqNewerRegistration g_reg_combos(ID_COMBOS, zqn_combos, "combos for 2.55.17 and 3.0");

// ---- CMBA: combo aliases ----------------------------------------------------------------------
//
// Upstream holds 8192 aliases; an old quest fills the first 2048 (or 256) and the rest stay
// empty. No combo pools or autocombos.

const int UP_MAXCOMBOALIASES = 8192;

int zqn_aliases(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    Out o;

    for(int j = 0; j < UP_MAXCOMBOALIASES; ++j)
    {
        if(j >= MAXCOMBOALIASES)
        {
            for(int k = 0; k < 9; ++k)
                o.p8(0);

            continue;
        }

        const combo_alias &a = combo_aliases[j];

        if(a.cset > 255)
        {
            zqw_last_error = "a combo alias has a cset past 255";
            return 2;
        }

        o.p16(a.combo);
        o.p8((byte)a.cset);
        o.p8(a.width);
        o.p8(a.height);
        o.p8(a.layermask);
        const int n = (a.width + 1) * (a.height + 1) * (comboa_lmasktotal(a.layermask) + 1);

        for(int k = 0; k < n; ++k) o.p16(a.combos ? a.combos[k] : 0);
        for(int k = 0; k < n; ++k) o.p8(a.csets ? a.csets[k] : 0);
    }

    o.p16(0);   // combo pools
    o.p16(0);   // autocombos
    return put_section(f, ID_COMBOALIASES, ctx, 5, 5, o);
}

ZqNewerRegistration g_reg_aliases(ID_COMBOALIASES, zqn_aliases, "combo aliases for 2.55.17 and 3.0");

} // namespace

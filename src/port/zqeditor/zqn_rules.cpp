// See zqn_rules.h. Ported from ZC 2.55.17 (qst.cpp: readheader, readrules and the readers
// after them; zq/zquest.cpp: set_rules) and 3.0 Nightly (core/qst_header.cpp,
// core/qst_rules.cpp, core/qst_strings.cpp, core/qst_combos.cpp, core/qst_initdata.cpp,
// core/qst_ffscript.cpp, core/qst_sfx.cpp, core/qst.cpp's end of loading, zq/zquest.cpp:
// set_rules), in the order upstream runs them.

#include "precompiled.h" //always first

#include <string.h>
#include <string>

#include "zdefs.h"
#include "zsys.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqn_qrs.h"
#include "zqn_rules.h"

extern byte quest_rules[QUESTRULES_SIZE];
extern zquestheader header;
extern ffscript *ffscripts[512];
extern ffscript *itemscripts[256];
extern ffscript *guyscripts[256];
extern ffscript *wpnscripts[256];
extern ffscript *globalscripts[NUMSCRIPTGLOBAL];
extern ffscript *linkscripts[3];
extern ffscript *screenscripts[256];

// Inside zqnqr, so the rule names are upstream's, not 2.53's of the same names.
namespace zqnqr
{

namespace
{

// ZQuest's rules as loaded, to tell which ones the user changed since.
byte g_loaded_rules[QUESTRULES_SIZE];

void note_loaded()
{
    memcpy(g_loaded_rules, quest_rules, QUESTRULES_SIZE);
}

ZqLoadHookRegistration g_load_hook(note_loaded);

// quest_rules as upstream's set_qr/get_qr see them
struct Bits
{
    byte *b;
    int max;        // qr_MAX of the target: set_qr's range

    bool get(int n) const { return ((b[n >> 3] >> (n & 7)) & 1) != 0; }

    void set(int n, bool on)
    {
        if(on)
            b[n >> 3] |= (byte)(1 << (n & 7));
        else
            b[n >> 3] &= (byte)~(1 << (n & 7));
    }
};

word rd16(const byte *p)
{
    return (word)(p[0] | (p[1] << 8));
}

dword rd32(const byte *p)
{
    return (dword)p[0] | ((dword)p[1] << 8) | ((dword)p[2] << 16) | ((dword)p[3] << 24);
}

// Section version of a part of the open quest, -1 when the quest has none.
int part_version(dword id)
{
    const ZqQuestPart *p = zq_find_part(id);
    word v, cv;

    if(!p || !zq_part_versions(*p, &v, &cv))
        return -1;

    return v;
}

// The version upstream's readheader gives a header without version fields (header versions
// before 4, and every quest older than 1.93): what compareVer compares.
struct HeaderVersion
{
    int major, minor, patch;

    // compareVer(major, minor, patch) < 0
    bool older_than(int ma, int mi, int pa) const
    {
        if(major != ma)
            return major < ma;

        if(minor != mi)
            return minor < mi;

        return patch < pa;
    }
};

HeaderVersion header_version(word zv, byte build)
{
    HeaderVersion h = { 0, 0, 0 };

    if(zv < 0x193)
        return h;       // a pre-1.93 header has none: all zero

    switch(zv)
    {
    case 0x254:
        h.major = 2;
        h.minor = 54;
        break;

    case 0x250:
        h.major = 2;

        if(build >= 31 && build <= 33)
        {
            h.minor = 53;
            h.patch = build == 33 ? 1 : 0;
        }
        else if(build >= 19 && build <= 30)
        {
            h.minor = 50;
            h.patch = build <= 24 ? 0 : build <= 28 ? 1 : build == 29 ? 2 : 3;
        }
        else
            h.major = 0;    // a build the switch doesn't name keeps zeros

        break;

    case 0x211:
        h.major = 2;
        h.minor = 11;
        break;

    case 0x210:
        h.major = 2;
        h.minor = 10;
        break;
    }

    return h;
}

// The rule bits as stored: the rules section's, or a pre-1.93 header's. ok is false when the
// kept bytes can't be read; has_section false when a sectioned quest has no rules section
// (then upstream never runs readrules).
bool read_stored(byte *stored, word *s_version, dword *compat, bool *has_section)
{
    memset(stored, 0, ZQN_RULES_SIZE);
    *s_version = 0;
    *compat = 0;
    *has_section = true;

    if(zq_quest_file.zelda_version < 0x193)
    {
        // readheader's pre-1.93 branch: rule words at 37, 61, and from 1.87 sixteen more at 256
        const ZqQuestPart *h = zq_find_part(ID_HEADER);
        byte b[272];

        if(!h)
            return false;

        size_t n = zq_quest_file.zelda_version >= 0x187 ? 272 : 63;

        if(!zq_part_read(*h, 0, b, n))
            return false;

        memcpy(stored, b + 37, 2);
        memcpy(stored + 2, b + 61, 2);

        if(zq_quest_file.zelda_version >= 0x187)
            memcpy(stored + 4, b + 256, 16);

        return true;
    }

    const ZqQuestPart *p = zq_find_part(ID_RULES);

    if(!p)
    {
        *has_section = false;
        return true;
    }

    byte head[12];

    if(!zq_part_read(*p, 0, head, 8))
        return false;

    *s_version = rd16(head);
    size_t at = 8;

    if(*s_version > 16)
    {
        if(!zq_part_read(*p, 0, head, 12))
            return false;

        *compat = rd32(head + 4);
        at = 12;
    }

    size_t n = *s_version < 15 ? QUESTRULES_SIZE : ZQN_RULES_SIZE;
    return zq_part_read(*p, at, stored, n);
}

// readheader, pre-1.93 branch, after the rule words: two rules it changes there.
void readheader_rules(Bits &q, byte *deprecated, word zv, byte build)
{
    if(zv >= 0x187 && zv <= 0x190)
        q.set(qr_MEANPLACEDTRAPS, 0);

    if(zv < 0x192 || (zv == 0x192 && build < 149))
    {
        q.set(qr_BRKNSHLDTILES, q.get(qr_BRKBLSHLDS_DEP));
        Bits d = { deprecated, q.max };
        d.set(qr_BRKBLSHLDS_DEP, q.get(qr_BRKBLSHLDS_DEP));
        q.set(qr_BRKBLSHLDS_DEP, 0);
    }
}

// readrules after reading the bits: the compatibility rules upstream switches by version.
// 2.55.17 and 3.0 share most of it; where they differ the target decides.
void readrules_compat(Bits &q, byte *deprecated, bool t30, word zv, byte build, word s_version, dword compat,
                      const HeaderVersion &hv, const char *title, const char *author)
{
    Bits d = { deprecated, q.max };
    memcpy(deprecated, q.b, ZQN_RULES_SIZE);

    if(s_version < 2)
    {
        const int old[] = { 14, 27, 28, 29, 30, 32, 36, 49, 50, 51, 68, 75, 76, 98, 110, 113, 116, 102, 132 };

        for(size_t i = 0; i < sizeof(old) / sizeof(old[0]); ++i)
            q.set(old[i], 0);
    }

    if(zv < 0x211 || (zv == 0x211 && build < 18))
    {
        q.set(qr_SMOOTHVERTICALSCROLLING, 1);
        q.set(qr_REPLACEOPENDOORS, 1);
        q.set(qr_OLDLENSORDER, 1);
        q.set(qr_NOFAIRYGUYFIRES, 1);
        q.set(qr_TRIGGERSREPEAT, 1);
    }

    if(zv < 0x193 || (zv == 0x193 && build < 3))
        q.set(qr_WALLFLIERS, 1);

    if(zv < 0x193 || (zv == 0x193 && build < 4))
        q.set(qr_NOBOMBPALFLASH, 1);

    if(zv < 0x193 || (zv == 0x193 && build < 3))
        q.set(qr_NOSCROLLCONTINUE, 1);

    if(zv <= 0x210)
        q.set(qr_ARROWCLIP, 1);

    if(zv == 0x210)
    {
        q.set(qr_NOSCROLLCONTINUE, q.get(qr_CMBCYCLELAYERS));
        q.set(qr_CMBCYCLELAYERS, 0);
        q.set(qr_CONT_SWORD_TRIGGERS, 1);
    }

    if(zv <= 0x210)
    {
        q.set(qr_OLDSTYLEWARP, 1);
        q.set(qr_210_WARPRETURN, 1);
    }

    if(zv < 0x210)
    {
        d.set(qr_OLDTRIBBLES_DEP, 1);
        q.set(qr_OLDTRIBBLES_DEP, 1);
        q.set(qr_OLDHOOKSHOTGRAB, 1);
    }

    if(zv < 0x211)
        q.set(qr_WRONG_BRANG_TRAIL_DIR, 1);

    if((zv == 0x192 && build <= 163) || zv < 0x192)
        q.set(qr_192b163_WARP, 1);

    if(zv == 0x210)
    {
        d.set(qr_OLDTRIBBLES_DEP, q.get(qr_DMGCOMBOPRI));
        q.set(qr_OLDTRIBBLES_DEP, q.get(qr_DMGCOMBOPRI));
        q.set(qr_DMGCOMBOPRI, 0);
    }

    if(zv < 0x211 || (zv == 0x211 && build < 15))
        q.set(qr_OLDPICKUP, 1);

    if(zv < 0x211 || (zv == 0x211 && build < 18))
    {
        q.set(qr_NOSOLIDDAMAGECOMBOS, 1);
        q.set(qr_ITEMPICKUPSETSBELOW, 1);
    }

    if(zv < 0x250)
        q.set(qr_HOOKSHOTDOWNBUG, 1);

    if(zv == 0x250 && build == 24)
        q.set(qr_PEAHATCLOCKVULN, 1);

    if(zv < 0x250 || (zv == 0x250 && build < 22))
        q.set(qr_OLD_DOORREPAIR, 1);

    if(zv < 0x250 || (zv == 0x250 && build < 20))
        q.set(qr_OLD_SECRETMONEY, 1);

    if(zv < 0x250 || (zv == 0x250 && build < 28))
        q.set(qr_OLD_POTION_OR_HC, 1);

    if(zv < 0x250 || (zv == 0x250 && build < 28))
        q.set(qr_OFFSCREENWEAPONS, 1);

    if(zv == 0x250)
    {
        if(build == 24 || build == 28)
            q.set(qr_BOMBCHUSUPERBOMB, 1);

        if(build == 29 || build == 30)
            q.set(qr_BOMBCHUSUPERBOMB, 0);
    }

    if(zv < 0x250 || (zv == 0x250 && build < 29))
    {
        q.set(qr_OFFSETEWPNCOLLISIONFIX, 1);

        if(zv >= 0x211 && build >= 18)
            q.set(qr_BROKENSTATUES, 1);
    }

    // std::string(title).starts_with("Zelda 3000\0"): the literal ends at its first NUL
    if(zv <= 0x190 || (zv == 0x192 && !strncmp(title, "Zelda 3000", 10)))
        q.set(qr_COPIED_SWIM_SPRITES, 1);

    if(zv <= 0x254 || (zv == 0x255 && build < 50))
        q.set(qr_OLD_SLASHNEXT_SECRETS, 1);

    if(zv < 0x211)
        q.set(qr_OLD_210_WATER, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 51))
        q.set(qr_STEP_IS_FLOAT, 0);

    if(zv < 0x250)
        q.set(qr_8WAY_SHOT_SFX_DEP, 1);

    if(s_version < 3)
    {
        q.set(qr_HOLDNOSTOPMUSIC, 1);
        q.set(qr_CAVEEXITNOSTOPMUSIC, 1);
    }

    if(s_version < 4)
        q.set(10, 0);

    if(s_version < 5)
        q.set(27, 0);

    if(s_version < 6)
        q.set(46, 0);

    if(s_version < 7)
    {
        q.set(qr_HEARTSREQUIREDFIX, 0);
        q.set(qr_PUSHBLOCKCSETFIX, 1);
    }

    if(s_version < 8)
        q.set(12, 0);
    else
        d.set(12, 0);

    if(s_version < 9)
    {
        q.set(qr_NOROPE2FLASH_DEP, 0);
        q.set(qr_NOBUBBLEFLASH_DEP, 0);
        q.set(qr_GHINI2BLINK_DEP, 0);
        q.set(qr_PHANTOMGHINI2_DEP, 0);
    }

    if(s_version < 10)
    {
        q.set(qr_NOCLOCKS_DEP, 0);
        q.set(qr_ALLOW10RUPEEDROPS_DEP, 0);
    }

    if(s_version < 11)
        q.set(qr_SLOWENEMYANIM_DEP, 0);

    if(s_version < 13)
        q.set(qr_SHOPCHEAT, 1);

    // the 2.50.2 bitmap offset fix (extra_rules aren't saved)
    if(zv < 0x250 || (zv == 0x250 && build < 29))
        q.set(qr_BITMAPOFFSETFIX, 1);

    if(zv == 0x250 && (build == 29 || build == 30 || build == 31))
        q.set(qr_BITMAPOFFSETFIX, 0);

    if(zv == 0x254)
        q.set(qr_BITMAPOFFSETFIX, 0);

    if(zv == 0x255 && build < 42)
        q.set(qr_BITMAPOFFSETFIX, 0);

    if(zv < 0x255 || (zv == 0x255 && build < 42))
        q.set(qr_OLDSPRITEDRAWS, 1);

    if(zv == 0x254 || (zv == 0x255 && build < 42))
        q.set(qr_OLDEWPNPARENT, 1);

    if(zv == 0x254 || (zv == 0x255 && build < 44))
        q.set(qr_OLDCREATEBITMAP_ARGS, 1);

    if(zv == 0x254 || (zv == 0x255 && build < 45))
        q.set(qr_OLDQUESTMISC, 1);

    if(zv < 0x254)
    {
        q.set(qr_OLDCREATEBITMAP_ARGS, 0);
        q.set(qr_OLDEWPNPARENT, 0);
        q.set(qr_OLDQUESTMISC, 0);
    }

    if(zv < 0x255 || (zv == 0x255 && build < 44))
    {
        q.set(qr_ITEMSCRIPTSKEEPRUNNING, 0);
        q.set(qr_SCRIPTSRUNINHEROSTEPFORWARD, 0);
        q.set(qr_FIXSCRIPTSDURINGSCROLLING, 0);
        q.set(qr_SCRIPTDRAWSINWARPS, 0);
        q.set(qr_DYINGENEMYESDONTHURTHERO, 0);
        q.set(qr_OUTOFBOUNDSENEMIES, 0);
        q.set(qr_SPRITEXY_IS_FLOAT, 0);
    }

    if(zv < 0x255 || (zv == 0x255 && build < 46))
        q.set(qr_CLEARINITDONSCRIPTCHANGE, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 46))
    {
        q.set(qr_TRACESCRIPTIDS, 0);
        q.set(qr_SCRIPT_FRIENDLY_ENEMY_TYPES, 1);
        q.set(qr_PARSER_BOOL_TRUE_DECIMAL, 1);
        q.set(qr_PARSER_250DIVISION, 1);
        q.set(qr_PARSER_TRUE_INT_SIZE, 0);
        q.set(qr_PARSER_FORCE_INLINE, 0);
        q.set(qr_PARSER_BINARY_32BIT, 0);

        if(q.get(qr_SELECTAWPN))
            q.set(qr_NO_L_R_BUTTON_INVENTORY_SWAP, 1);
    }

    if(zv < 0x255 || (zv == 0x255 && build < 47))
    {
        q.set(qr_DISALLOW_SETTING_RAFTING, 1);
        q.set(qr_BROKEN_ASKIP_Y_FRAMES, 1);
        q.set(qr_ENEMY_BROKEN_TOP_HALF_SOLIDITY, 1);
        q.set(qr_OLD_SIDEVIEW_CEILING_COLLISON, 1);
        q.set(qr_0AFRAME_ITEMS_IGNORE_AFRAME_CHANGES, 1);
        q.set(qr_OLD_ENEMY_KNOCKBACK_COLLISION, 1);
    }

    if(zv < 0x255)
    {
        q.set(qr_NOFFCWAITDRAW, 1);
        q.set(qr_NOITEMWAITDRAW, 1);
        q.set(qr_SETENEMYWEAPONSPRITESONWPNCHANGE, 1);
        q.set(qr_OLD_INIT_SCRIPT_TIMING, 1);
    }

    if(zv < 0x255 || (zv == 0x255 && build < 48))
        q.set(qr_SETENEMYWEAPONSPRITESONWPNCHANGE, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 52))
        q.set(qr_OLD_PRINTF_ARGS, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 54))
        q.set(qr_BROKEN_RING_POWER, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 56))
        q.set(qr_NO_OVERWORLD_MAP_CHARTING, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 57))
        q.set(qr_DUNGEONS_USE_CLASSIC_CHARTING, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 58))
    {
        if(q.get(qr_SET_XBUTTON_ITEMS))
            q.set(qr_SET_YBUTTON_ITEMS, 1);
    }

    if(zv < 0x255 || (zv == 0x255 && build < 59))
        q.set(qr_ALLOW_EDITING_COMBO_0, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 60))
        q.set(qr_OLD_CHEST_COLLISION, 1);

    if(zv < 0x254)
        q.set(qr_250WRITEEDEFSCRIPT, 1);

    if(zv < 0x250 || (zv == 0x250 && build < 27))
        q.set(qr_OLDSIDEVIEWSPIKES, 1);

    if(zv < 0x250 || (zv == 0x250 && build < 31))
    {
        q.set(qr_MELEEMAGICCOST, 0);
        q.set(qr_GANONINTRO, 0);
        q.set(qr_OLDMIRRORCOMBOS, 1);
        q.set(qr_BROKENBOOKCOST, 1);
        q.set(qr_BROKENCHARINTDRAWING, 1);
    }

    if(zv == 0x254 && build < 41)
        q.set(qr_MELEEMAGICCOST, 1);

    if(zv < 0x193)
        q.set(qr_SHORTDGNWALK, 1);

    if(zv < 0x255)
        q.set(qr_OLDINFMAGIC, 1);

    if(zv < 0x250)
        q.set(qr_SIDEVIEWTRIFORCECELLAR, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 47))
        q.set(qr_OLD_F6, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 49))
        q.set(qr_NO_OVERWRITING_HOPPING, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 50))
        q.set(qr_STRING_FRAME_OLD_WIDTH_HEIGHT, 1);

    if(zv < 0x255 || (zv == 0x255 && build < 53))
        q.set(qr_BROKEN_OVERWORLD_MINIMAP, 1);

    // ---- by the compat rule version the rules section carries (0 for every quest here)

    if(compat < 1)
        q.set(qr_ENEMIES_SECRET_ONLY_16_31, 1);

    if(compat < 2)
        q.set(qr_OLDCS2, 1);

    if(compat < 3)
        q.set(qr_HARDCODED_ENEMY_ANIMS, 1);

    if(compat < 4)
        q.set(qr_OLD_ITEMDATA_SCRIPT_TIMING, 1);

    if(compat < 5 && zv >= 0x250)
        q.set(qr_NO_LANMOLA_RINGLEADER, 1);

    if(compat < 6)
        q.set(qr_STEPTEMP_SECRET_ONLY_16_31, 1);

    if(compat < 7)
        q.set(qr_ALLTRIG_PERMSEC_NO_TEMP, 1);

    if(compat < 8)
        q.set(qr_HARDCODED_LITEM_LTMS, 1);

    if(compat < 9)
    {
        q.set(qr_HARDCODED_BS_PATRA, 1);
        q.set(qr_PATRAS_USE_HARDCODED_OFFSETS, 1);
        q.set(qr_BROKEN_BIG_ENEMY_ANIMATION, 1);
        q.set(qr_BROKEN_ATTRIBUTE_31_32, 1);
    }

    if(compat < 10)
        q.set(qr_CANDLES_SHARED_LIMIT, 1);

    if(compat < 11)
        q.set(qr_OLD_RESPAWN_POINTS, 1);

    if(compat < 12)
    {
        q.set(qr_OLD_FLAMETRAIL_DURATION, 1);
        q.set(qr_GANONINTRO, !q.get(qr_GANONINTRO));
    }

    if(compat < 13 && zv >= 0x255)
        q.set(qr_ANONE_NOANIM, 1);

    if(compat < 14)
        q.set(qr_OLD_BRIDGE_COMBOS, 1);

    if(compat < 15)
        q.set(qr_BROKEN_Z3_ANIMATION, 1);

    if(compat < 16)
        q.set(qr_OLD_TILE_INITIALIZATION, 1);

    if(compat < 18)
    {
        q.set(qr_BROKEN_DRAWSCREEN_FUNCTIONS, 1);
        q.set(qr_SCROLLING_KILLS_CHARGE, 1);
    }

    if(compat < 19)
        q.set(qr_BROKEN_ITEM_CARRYING, 1);

    if(compat < 20)
        q.set(qr_CUSTOMWEAPON_IGNORE_COST, 1);

    if(compat < 21)
    {
        q.set(qr_LEEVERS_DONT_OBEY_STUN, 1);
        q.set(qr_GANON_CANT_SPAWN_ON_CONTINUE, 1);
        q.set(qr_WIZZROBES_DONT_OBEY_STUN, 1);
        q.set(qr_OLD_BUG_NET, 1);
        q.set(qr_MANHANDLA_BLOCK_SFX, 1);
    }

    if(compat < 22)
        q.set(qr_BROKEN_KEEPOLD_FLAG, 1);

    if(compat < 23)
        q.set(qr_OLD_HALF_MAGIC, 1);

    if(compat < 24)
    {
        q.set(qr_WARPS_RESTART_DMAPSCRIPT, 1);
        q.set(qr_DMAP_0_CONTINUE_BUG, 1);
    }

    if(compat < 25)
    {
        q.set(qr_OLD_FAIRY_LIMIT, !q.get(qr_OLD_FAIRY_LIMIT));
        q.set(qr_OLD_SCRIPTED_KNOCKBACK, 1);
    }

    if(compat < 26)
    {
        q.set(qr_OLD_KEESE_Z_AXIS, 1);
        q.set(qr_POLVIRE_NO_SHADOW, 1);
        q.set(qr_SUBSCR_OLD_SELECTOR, 1);
    }

    if(compat < 27)     // junk once found in the rules: cleared up to the parser rules and past them
    {
        for(int n = qr_POLVIRE_NO_SHADOW + 1; n < qr_PARSER_250DIVISION; ++n)
            q.set(n, 0);

        for(int n = qr_COMBODATA_INITD_MULT_TENK + 1; n < q.max; ++n)
            q.set(n, 0);
    }

    if(zv < 0x211)
        q.set(qr_OLD_210_HAMMER_POUND_REACH, 1);

    if(zv < 0x250 || (zv == 0x250 && build < 29))
        q.set(qr_TURN_WHILE_CHARGING_HAMMER, 1);

    if(compat < 28)
        q.set(qr_SUBSCR_BACKWARDS_ID_ORDER, 1);

    if(compat < 29)
        q.set(qr_OLD_LOCKBLOCK_COLLISION, 1);

    if(compat < 30)
    {
        q.set(qr_DECO_2_YOFFSET, 1);
        q.set(qr_SCREENSTATE_80s_BUG, 1);
    }

    if(compat < 31)
    {
        q.set(qr_GOHMA_UNDAMAGED_BUG, 1);
        q.set(qr_FFCPRELOAD_BUGGED_LOAD, 1);
    }

    if(compat < 32)
        q.set(qr_BROKEN_GETPIXEL_VALUE, 1);

    if(compat < 33)
        q.set(qr_NO_LIFT_SPRITE, 1);

    if(compat < 34)
    {
        q.set(qr_OLD_SIDEVIEW_LANDING_CODE, 1);
        q.set(qr_OLD_FFC_SPEED_CAP, 1);
        q.set(qr_OLD_FFC_FUNCTIONALITY, 1);
        q.set(qr_OLD_WIZZROBE_SUBMERGING, 1);
    }

    if(compat < 35)
    {
        q.set(qr_ZS_NO_NEG_ARRAY, 1);
        q.set(qr_BROKEN_INPUT_DOWN_STATE, 1);
    }

    if(compat < 36)
        q.set(qr_OLD_SHALLOW_SFX, 1);

    if(compat < 37)
        q.set(qr_SPARKLES_INHERIT_PROPERTIES, 1);

    if(compat < 38)
        q.set(qr_BUGGED_LAYERED_FLAGS, 1);

    if(compat < 39)
        q.set(qr_HARDCODED_FFC_BUSH_DROPS, 1);

    if(compat < 40)
        q.set(qr_MOVINGBLOCK_FAKE_SOLID, 1);

    if(compat < 41)
        q.set(qr_BROKENHITBY, 1);

    if(compat < 42)
        q.set(qr_BROKEN_MOVING_BOMBS, 1);

    if(compat < 43)
        q.set(qr_OLD_BOMB_HITBOXES, 1);

    if(compat < 44)
        q.set(qr_SCROLLWARP_NO_RESET_FRAME, 1);

    if(compat < 45)
        q.set(qr_ENEMIES_DONT_SCRIPT_FIRST_FRAME, 1);

    if(compat < 46)
        q.set(qr_BROKEN_RAFT_SCROLL, 1);

    if(compat < 47)
    {
        q.set(qr_SENSITIVE_SOLID_DAMAGE, 1);
        q.set(qr_OLD_CONVEYOR_COLLISION, 1);
    }

    if(compat < 48)
        q.set(qr_OLD_GUY_HANDLING, 1);

    if(compat < 49)
        q.set(qr_FAIRY_FLAG_COMPAT, 1);

    if(compat < 50)
        q.set(qr_OLD_LENS_LAYEREFFECT, 1);

    if(compat < 51)
        q.set(qr_PUSHBLOCK_SPRITE_LAYER, 1);

    if(compat < 52)
        q.set(qr_OLD_SCRIPT_VOLUME, 1);

    if(compat < 53)
    {
        q.set(qr_OLD_SUBSCR, 1);
        q.set(qr_ITM_0_INVIS_ON_BTNS, 1);
        q.set(qr_OLD_GAUGE_TILE_LAYOUT, 1);
    }

    if(compat < 54)
        q.set(qr_WALKTHROUGHWALL_NO_DOORSTATE, 1);

    if(compat < 55)
        q.set(qr_SPOTLIGHT_IGNR_SOLIDOBJ, 1);

    if(compat < 56)
        q.set(qr_BROKEN_LIGHTBEAM_HITBOX, 1);

    if(compat < 57)
        q.set(qr_BROKEN_SWORD_SPIN_TRIGGERS, 1);

    if(compat < 58)
        q.set(qr_OLD_DMAP_INTRO_STRINGS, 1);

    if(compat < 59)
        q.set(qr_SCRIPT_CONTHP_IS_HEARTS, 1);

    if(compat < 60)
        q.set(qr_SEPARATE_BOMBABLE_TAPPING_SFX, 1);

    if(compat < 61)
        q.set(qr_BROKEN_BOMB_AMMO_COSTS, 1);

    if(compat < 62)
        q.set(qr_OLD_BROKEN_WARPEX_MUSIC, 1);

    if(compat < 63)
    {
        q.set(qr_BROKEN_LIFTSWIM, 1);
        q.set(qr_BROKEN_GENERIC_PUSHBLOCK_LOCKING, 1);
    }

    if(compat < 64)
        q.set(qr_BROKEN_FLAME_ARROW_REFLECTING, 1);

    if(compat < 65)
        q.set(qr_BROKEN_SIDEVIEW_SPRITE_JUMP, 1);

    if(t30)
    {
        // compat rules 3.0 added after 2.55's last (65)
        if(compat < 67)
            q.set(qr_OLD_HERO_WARP_RETSQUARE, 1);

        if(compat < 68)
            q.set(qr_SCRIPTS_6_BIT_COLOR, 1);

        if(compat < 69)
            q.set(qr_SETENEMYWEAPONSOUNDSONWPNCHANGE, 1);

        if(compat < 70)
            q.set(qr_BROKEN_CONVEYORS, 1);

        if(compat < 71)
            q.set(qr_BROKEN_PUSHBLOCK_TOP_HALF_SOLIDS, 1);

        if(compat < 72)
            q.set(qr_BROKEN_PUSHBLOCK_FLAG_CLONING, 1);

        if(compat < 73)
        {
            q.set(qr_OLD_LANDING_SFX, 1);
            q.set(qr_FIRE_LEVEL_TRIGGERS_ARENT_WEAPONS, 1);
        }

        if(compat < 76)
            q.set(qr_INVERTED_DARK_COMBO_TRIGGERS, 1);

        if(compat < 77)
            q.set(qr_BROKEN_ICY_FLOOR_SIDEVIEW, 1);

        if(compat < 78)
            q.set(qr_SCRIPTS_SCREEN_DRAW_LIGHT_NO_OFFSET, 1);

        if(compat < 79)
            q.set(qr_BROKEN_SYSTEM_COLORS, 1);

        if(compat < 80)
            q.set(qr_ZS_OLD_SUSPEND_FFC, 1);

        if(compat < 82)
        {
            q.set(qr_EW_ROCKS_HARDCODED_BREAK_ON_SOLID, 1);
            q.set(qr_IMPRECISE_WEAPON_SOLIDITY_CHECKS, 1);
        }

        if(compat < 85)
            q.set(qr_OLD_WEAPON_REFLECTION, 1);

        if(compat < 87)
            q.set(qr_OLD_TERMINAL_VELOCITY, 1);

        if(compat < 88)
            q.set(qr_OLD_SCRIPT_LEVEL_GLOBAL_STATES, 1);

        if(compat < 89)
            q.set(qr_BROKEN_ARMOS_GRAVE_BIGHITBOX_COLLISION, 1);

        if(compat < 90)
        {
            q.set(qr_CLASSIC_DRAWING_ORDER, 1);
            q.set(qr_OLD_WEAPON_DRAW_ANIMATE_TIMING, 1);
        }

        if(compat < 91)
        {
            q.set(qr_OLD_SCRIPTS_INTERNAL_ARRAYS_BOUND_INDEX, 1);
            q.set(qr_OLD_SCRIPTS_ARRAYS_NON_ZERO_DEFAULT_VALUE, 1);
        }

        if(compat < 94)
            q.set(qr_OLD_SCRIPTS_MESSAGE_DATA_BINARY_ENCODING, 1);

        if(compat < 95)
            q.set(qr_ONLY_MARK_SCREENS_VISITED_IF_MAP_VIEWABLE, 1);

        if(compat < 101)
            q.set(qr_BROKEN_WATER_PASSIVE_DAMAGE, 1);

        if(compat < 103)
            q.set(qr_OLD_ITEM_HITBOXES, 1);

        if(compat < 104)
            q.set(qr_SUBSCR_SELECTOR_USES_ITEM_HITBOX, 1);

        if(compat < 108)
        {
            q.set(qr_BROKEN_SV_SOLID_CONVEYORS, 1);
            q.set(qr_ITEM_CONVEYORS_IGNORE_HITBOX, 1);
            q.set(qr_ITEMS_HARDCODED_SIDEVIEW_HITBOX, 1);
            q.set(qr_WEAPONS_HARDCODED_SIDEVIEW_HITBOX, 1);
        }

        if(compat < 110)
        {
            q.set(qr_OLD_LAYER_DRAW_ORDER, q.get(qr_CLASSIC_DRAWING_ORDER));
            q.set(qr_MAP_SPOILS_DARKROOMS, 1);
        }

        if(compat < 113)
        {
            q.set(qr_OLD_BRIDGE_COMBO_COVER, 1);
            q.set(qr_MINIMAP_VISITED_CHARTED, 1);
            q.set(qr_QUAKE_HAMMER_IN_AIR, 1);
        }

        if(compat < 114)
        {
            q.set(qr_NO_FLASHING_MSG_MORE, 1);
            q.set(qr_BROKEN_LENS_HINTS_SECRETS, 1);
        }

        if(compat < 115)
            q.set(qr_CHESTS_SIGNS_LOCKBLOCKS_ABOVE_2, 1);

        if(compat < 116)
            q.set(qr_OLD_SUMMONER_COUNTS, 1);

        if(compat < 117)
            q.set(qr_BROKEN_LARGE_ARMOS_ANIM, 1);

        if(compat < 119)
            q.set(qr_OLD_LONG_SCC_ARGS, 1);

        if(compat < 120)
            q.set(qr_STRINGS_DONT_SCROLL, 1);

        if(compat < 121)
            q.set(qr_SCRIPTS_SHARE_INITD, 1);
    }

    q.set(qr_ANIMATECUSTOMWEAPONS, 0);

    if(s_version < 16)
        q.set(qr_BROKEN_HORIZONTAL_WEAPON_ANIM, 1);

    // Changes cherry-picked to 2.55, by the header's version: every quest here is older than
    // all of them. 2.55.17 spares quests whose author is "zcdev" the first three; 3.0 leaves
    // the bottom 8 pixels to the check after loading and sets the 2.55.15 ones by compat rule.
    bool dev = !t30 && !strcmp(author, "zcdev");

    if(hv.older_than(2, 55, 6) && !dev)
        q.set(qr_ROPE_ENEMIES_SPEED_NOT_CONFIGURABLE, 1);

    if(hv.older_than(2, 55, 9) && !dev)
    {
        q.set(qr_NEWDARK_TRANS_STACKING, 1);
        q.set(qr_BROKEN_SCRIPTS_SCROLLING_HERO_POSITION, 1);

        if(!t30)
            q.set(qr_HIDE_BOTTOM_8_PIXELS, 1);
    }

    if(hv.older_than(2, 55, 10) && !dev)
    {
        q.set(qr_BROKEN_SCRIPTS_BITMAP_DRAW_ORIGIN, 1);
        q.set(qr_LAYER6_STRINGS_OVER_SUBSCREEN, 1);
        q.set(qr_BROKEN_SCROLL_INSTEAD_OF_DROWN_FALL, 1);
    }

    if(hv.older_than(2, 55, 11))
    {
        q.set(qr_BROKEN_BLOCKHOLE_PITFALLS, 1);
        q.set(qr_CUSTOM_WEAPON_BROKEN_SIZE, 1);
        q.set(qr_OLD_SPRITE_FALL_DROWN, 1);
        q.set(qr_OLD_PIT_SENSITIVITY, 1);
    }

    if(hv.older_than(2, 55, 12))
    {
        q.set(qr_ACTIVE_SHIELD_PASSIVE_ROC_NO_SCRIPT, 1);
        q.set(qr_ACTIVE_SUB_IGNORE_8PX, 1);
    }

    if(hv.older_than(2, 55, 13))
    {
        q.set(qr_DRAWCOMBO_IGNORES_FRAME, 1);
        q.set(qr_DRAWTILE_TALL_DRAWS_WRAP_POORLY, 1);
        q.set(qr_BUGGY_MULTIPLE_WIND, 1);
        q.set(qr_NO_DEATH_EVENTS_FOR_SEGMENTED_ENEMY_CORES, 1);
        q.set(qr_BROKEN_COPYCAT_SELF_TRIGGER, 1);
        q.set(qr_BROKEN_SELF_TRIGGERING_TRIGGERS, 1);
    }

    if(hv.older_than(2, 55, 14))
    {
        q.set(qr_BROKEN_0_DAMAGE_BRANG_HSHOT, 1);
        q.set(qr_BROKEN_SPINTILE_COMBO_CHANGE, 1);
        q.set(qr_LIGHT_RADIUS_IGNORE_HIT_OFFSETS, 1);
    }

    if(hv.older_than(2, 55, 15))
    {
        q.set(qr_BROKEN_PUSHBLOCK_TRIGGER_GROUPS, 1);
        q.set(qr_BS_OW_IGNORES_MAP_ITEM, 1);
        q.set(qr_BROKEN_ENEMY_SIDEVIEW_PLATFORM_HITBOX, 1);
        q.set(qr_BROKEN_PLAYER_SOLIDPUSH, 1);
        q.set(qr_KNOCKBACK_THROUGH_PLATFORMS, 1);
        q.set(qr_BROKEN_SIDEVIEW_SOLID_FFC_COLLISION, 1);
        q.set(qr_BROKEN_SCC_MAP_ARGS, 1);

        if(!t30)
        {
            q.set(qr_CHESTS_SIGNS_LOCKBLOCKS_ABOVE_2, 1);
            q.set(qr_BROKEN_LARGE_ARMOS_ANIM, 1);
        }
    }

    if(hv.older_than(2, 55, 17))
        q.set(qr_BROKEN_FFC_WRAP_CHANGERS, 1);
}

// 3.0's compat_qr_hide_bottom_pixels for a quest older than 2.55.9: true when any script sets
// or pushes one of the values that draw at the old bottom of the screen (no 2.53 DMap has a
// scripted subscreen, and the replay case doesn't apply to an editor).
bool scripts_draw_at_bottom()
{
    if(!strcmp(header.title, "Yuurand: Tales of the Labyrinth"))
        return true;

    ffscript **const tables[7] = { ffscripts, itemscripts, guyscripts, wpnscripts, screenscripts, globalscripts, linkscripts };
    const int sizes[7] = { 512, 256, 256, 256, 256, NUMSCRIPTGLOBAL, 3 };
    const word setv = 0x0000, pushv = 0x0043;   // SETV and PUSHV, the same in 2.53 and 3.0

    for(int t = 0; t < 7; ++t)
    {
        for(int i = 0; i < sizes[t]; ++i)
        {
            const ffscript *s = tables[t][i];

            if(!s)
                continue;

            for(int k = 0; s[k].command != 0xFFFF; ++k)
            {
                if(s[k].command != setv && s[k].command != pushv)
                    continue;

                long v = s[k].arg2;

                if(v == 167000000 || v == 168000000 || v == 167870000 || v == 167910000 || v == 168130000)
                    return true;
            }
        }
    }

    return false;
}

// What a save in the quest's own version stores for the rules the user changed since the load
// (zqw_basic.cpp's write_rules_old, zqw_flat.cpp's rules_inverse): each changed rule's own
// stored bit flipped, except where 2.53's loader makes the rule from another stored bit; then
// that bit. Rules 2.53's loader always sets are refused by those writers, so what is stored for
// them doesn't matter. The first 160 rule numbers are 2.53's and upstream's alike.
void own_save_stored(const byte *stored, const byte *diff, byte *out)
{
    memcpy(out, stored, ZQN_RULES_SIZE);
    word zv = zq_quest_file.zelda_version;
    byte build = zq_quest_file.build;
    Bits o = { out, (int)qr_MAX };

    for(int n = 0; n < QUESTRULES_SIZE * 8; ++n)
    {
        if(!zqn_rule(diff, n))
            continue;

        int from = n;

        // a 2.10 quest's "no scrolling continue" is read from the old cycling-layers bit
        if(zv == 0x210 && n == qr_NOSCROLLCONTINUE)
            from = qr_CMBCYCLELAYERS;

        // before 1.92 beta 149 the broken shield tiles rule is read from the old bit for it
        if(zv < 0x193 && (zv < 0x192 || build < 149) && n == qr_BRKNSHLDTILES)
            from = qr_BRKBLSHLDS_DEP;

        o.set(from, !o.get(from));
    }
}

// Upstream's load from the rule bits stored in the file to quest_rules once loading is done:
// readheader, readrules, the readers after the rules section that change rules, the end of
// loading and ZQuest's set_rules. deprecated and after are what the readers on the way see.
void run_load(bool t30, const byte *stored, word s_version, dword compat, bool has_rules, byte *deprecated, byte *after,
              byte *loaded)
{
    word zv = zq_quest_file.zelda_version;
    byte build = zq_quest_file.build;
    byte *q = loaded;
    memcpy(q, stored, ZQN_RULES_SIZE);
    memcpy(deprecated, stored, ZQN_RULES_SIZE);
    Bits b = { q, t30 ? (int)qr_MAX : (int)qr_MAX_255 };

    // header.title and author as upstream's readheader keeps them: the last byte cleared
    char title[65], author[65];
    memcpy(title, header.title, 65);
    memcpy(author, header.author, 65);
    title[64] = author[64] = 0;

    if(zv < 0x193)
        readheader_rules(b, deprecated, zv, build);

    // a quest older than 1.93 runs readrules with no section to read
    if(has_rules)
        readrules_compat(b, deprecated, t30, zv, build, s_version, compat, header_version(zv, build), title, author);

    memcpy(after, q, ZQN_RULES_SIZE);

    // ---- the readers after the rules, in the order of the file -----------------------

    // readstrings: old string margins for strings before section version 7
    int str = part_version(ID_STRINGS);

    if(zv < 0x193 || (str >= 0 && str < 7))
        b.set(qr_OLD_STRING_EDITOR_MARGINS, 1);

    // readcombos: combos of section versions 13-20 had a buggy ->next flag
    int cmb = part_version(ID_COMBOS);

    if(cmb >= 13 && cmb < 21)
        b.set(qr_BUGGY_BUGGY_SLASH_TRIGGERS, 1);

    // readinitdata
    int init = part_version(ID_INITDATA);

    if(zv < 0x193 || init >= 0)
    {
        if(init >= 14 && init < 16)
            b.set(qr_BRANGPICKUP + 1, 0);

        if(zv < 0x192 || (zv == 0x192 && build < 129))
        {
            for(int i = 0; i < 4; ++i)  // idBP_MAX: the beam percentages these bits were
                b.set(qr_LENSHINTS + i, 0);

            b.set(qr_HIDECARRIEDITEMS, 0);
        }

        if(zv < 0x192 || (zv == 0x192 && build < 168))
            b.set(qr_FREEFORM, 0);
    }

    // readffscript: sprite jump values of early 2.55 alphas
    int ffs = part_version(ID_FFSCRIPT);

    if(ffs >= 0)
    {
        if(zv < 0x253 || (zv == 0x253 && build < 33) || (zv > 0x253 && ffs < 12))
            b.set(qr_SPRITE_JUMP_IS_TRUNCATED, 1);

        if(ffs < 19)
            b.set(qr_FLUCTUATING_ENEMY_JUMP, 1);
    }

    // readsfx: GOTOLESS changed meaning with sound section version 2
    int sfx = part_version(ID_SFX);

    if(sfx >= 0 && sfx < 2)
        b.set(qr_GOTOLESSNOTEQUAL, 1);

    // ---- after loading -----------------------------------------------------------------

    if(t30 && scripts_draw_at_bottom())
        b.set(qr_HIDE_BOTTOM_8_PIXELS, 1);

    // the old "continue with full health" rule becomes init data
    b.set(qr_CONTFULL_DEP, 0);

    // ZQuest's set_rules, run by load_quest: 2.50.0/2.50.1's very fast scrolling
    if(b.get(qr_VERYFASTSCROLLING))
        b.set(qr_FASTDNGN, 1);
}

} // namespace

bool upstream_rules(int target, ZqnRules &r)
{
    memset(&r, 0, sizeof(r));
    r.overridden_rule = -1;
    word zv = zq_quest_file.zelda_version;
    byte build = zq_quest_file.build;

    if(zv >= 0x255)
        return false;

    bool t30 = target == ZQ_NEWER_30;
    bool has_rules;
    byte file[ZQN_RULES_SIZE], dep[ZQN_RULES_SIZE], after[ZQN_RULES_SIZE];

    if(!read_stored(file, &r.s_version, &r.compat_version, &has_rules))
        return false;

    // the unchanged quest, as upstream opens the file
    run_load(t30, file, r.s_version, r.compat_version, has_rules, dep, after, r.loaded);

    // The quest as it is now, as upstream opens the file a save in its own version would write.
    // That is the same file when the user changed no rule.
    byte diff[ZQN_RULES_SIZE];
    memset(diff, 0, sizeof(diff));

    for(int i = 0; i < QUESTRULES_SIZE; ++i)
        diff[i] = (byte)(quest_rules[i] ^ g_loaded_rules[i]);

    own_save_stored(file, diff, r.stored);
    run_load(t30, r.stored, r.s_version, r.compat_version, has_rules, r.deprecated, r.after_readrules, r.now);

    // A rule the user changed that upstream's loader sets whatever the file says, for a quest of
    // this version: 2.55.17 and 3.0 would undo the change.
    for(int n = 0; n < QUESTRULES_SIZE * 8 && r.overridden_rule < 0; ++n)
        if(zqn_rule(diff, n) && zqn_rule(r.now, n) == zqn_rule(r.loaded, n))
            r.overridden_rule = n;

    // readinitdata's "unorthodox updating thing" for quests before init data version 14: the
    // enemy reader then gives Pols Voices their old speed
    int init = part_version(ID_INITDATA);
    r.fix_pols_voice = zv < 0x193 || (init >= 0 && init < 14);
    return true;
}

} // namespace zqnqr

bool zqn_upstream_rules(int target, ZqnRules &r)
{
    return zqnqr::upstream_rules(target, r);
}

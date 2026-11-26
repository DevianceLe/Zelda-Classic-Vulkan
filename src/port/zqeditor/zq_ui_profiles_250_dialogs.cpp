// zq_ui_profiles_250_dialogs.cpp: the windows of 2.50.x's ZQuest interface that today's (2.53.1) editor
// has no window for.
//
// - Quest > Rules in 2.50.2 and 2.50.3 (@rules250): one "Quest rules" window with seven tabs,
//   Animation, Combos, Items, Enemy, NES fixes, Misc. and Backward compatibility, each a column
//   of the rules that build listed, by its names, with OK and Cancel. 2.50.0, 2.50.1 and 2.53
//   have one window per tab, in a submenu; the rules and their bits are the same, so this
//   window sets the same quest_rules bits as those, and does what their OK does after. Laid out
//   here (one column per tab, ten pixels a rule, as 2.50.2's own window set its widgets out).
//
// It is drawn by the port's dialog host like every other window.
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <vector>

#include "gui.h"
#include "zdefs.h"
#include "zquest.h"
#include "zc_sys.h"
#include "zsys.h"
#include "jwin.h"

bool zq_edit_rules_newer(int category, bool *shown);    // zq_rules_editor.cpp

namespace
{

struct Rule
{
    int id;
    const char *name;
};

// 2.50.2's lists (dialog/zquest/questRules.cpp; 2.50.3's are the same), in its order
const Rule animation[] =
{
    { qr_BSZELDA, "BS-Zelda Animation" },
    { qr_COOLSCROLL, "Circle Opening/Closing Wipes" },
    { qr_OVALWIPE, "Oval Opening/Closing Wipes" },
    { qr_TRIANGLEWIPE, "Triangle Opening/Closing Wipes" },
    { qr_SMASWIPE, "Super Mario All-Stars Opening/Closing Wipes" },
    { qr_EXPANDEDLTM, "Expanded Link Tile Modifiers" },
    { qr_NOSCROLL, "No Screen Scrolling" },
    { qr_FASTDNGN, "Fast Dungeon Scrolling" },
    { qr_VERYFASTSCROLLING, "Very Fast Scrolling" },
    { qr_SMOOTHVERTICALSCROLLING, "Smooth Vertical Scrolling" },
    { qr_FFCSCROLL, "FFCs Are Visible While The Screen Is Scrolling" },
    { qr_FADE, "Interpolated Fading" },
    { qr_FADECS5, "Fade CSet 5" },
    { qr_LINKFLICKER, "Invincible Link Flickers" },
    { qr_HOLDITEMANIMATION, "Items Held Above Link's Head Continue To Animate" },
    { qr_NOBOMBPALFLASH, "Bomb Explosions Don't Flash Palette" },
    { qr_LAYER12UNDERCAVE, "Layers 1 and 2 Are Drawn Under Caves" },
    { -1, NULL }
};

const Rule combos[] =
{
    { qr_DROWN, "Link Drowns in Walkable Water" },
    { qr_SMARTSCREENSCROLL, "Smart Screen Scrolling" },
    { qr_SOLIDBLK, "Can't Push Blocks Onto Unwalkable Combos" },
    { qr_HESITANTPUSHBLOCKS, "Push Blocks Don't Move When Bumped" },
    { qr_INSTABURNFLAGS, "Burn Flags Are Triggered Instantly" },
    { qr_SWORDMIRROR, "Magic Mirror/Prism Combos Reflect Sword Beams" },
    { qr_WHIRLWINDMIRROR, "Magic Mirrors Reflect Whistle Whirlwinds" },
    { qr_CMBCYCLELAYERS, "Combo Cycling On Layers" },
    { qr_DMGCOMBOPRI, "Full Priority Damage Combos" },
    { qr_WARPSIGNOREARRIVALPOINT, "Warps Ignore Arrival X/Y Position When Setting Continue Screen" },
    { qr_NOARRIVALPOINT, "Use Warp Return Points Only" },
    { qr_NOSCROLLCONTINUE, "Scrolling Warps Don't Set The Continue Point" },
    { qr_OLDSTYLEWARP, "Use Old-Style Warp Detection (NES Movement Only)" },
    { qr_DMGCOMBOLAYERFIX, "Damage Combos Work On Layers 1 And 2" },
    { qr_HOOKSHOTLAYERFIX, "Hookshot Grab Combos Work On Layers 1 And 2" },
    { -1, NULL }
};

const Rule items[] =
{
    { qr_ENABLEMAGIC, "Enable Magic" },
    { qr_Z3BRANG_HSHOT, "New Boomerang/Hookshot" },
    { qr_TRUEARROWS, "True Arrows" },
    { qr_NODIVING, "No Diving" },
    { qr_TEMPCANDLELIGHT, "Dark Rooms Stay Lit Only While Fire Is On Screen" },
    { qr_FIREPROOFLINK, "Link Isn't Hurt By Own Fire Weapons" },
    { qr_OUCHBOMBS, "Link's Bombs Hurt Link" },
    { qr_RINGAFFECTDAMAGE, "Rings Affect Damage Combos" },
    { qr_QUICKSWORD, "Quick Sword" },
    { qr_SLASHFLIPFIX, "Flip Right-Facing Slash" },
    { qr_NOWANDMELEE, "Wand Can't Be Used As Melee Weapon" },
    { qr_NOITEMMELEE, "Melee Weapons Can't Pick Up Items" },
    { qr_BRANGPICKUP, "Boomerang/Hookshot Grabs All Items" },
    { qr_HEARTSREQUIREDFIX, "'Hearts Required' Affects Non-Special Items" },
    { qr_4TRI, "Big Triforce Pieces" },
    { qr_3TRI, "3 or 6 Triforce Total" },
    { qr_SLOWCHARGINGWALK, "Slow Walk While Charging" },
    { qr_LENSHINTS, "Show Secret Combo Hints When Using The Lens" },
    { qr_RAFTLENS, "Lens Shows Raft Paths" },
    { qr_LENSSEESENEMIES, "Lens Reveals Invisible Enemies" },
    { qr_NONBUBBLEMEDICINE, "Medicine Doesn't Remove Sword Jinxes" },
    { qr_NONBUBBLEFAIRIES, "Fairies Don't Remove Sword Jinxes" },
    { qr_NONBUBBLETRIFORCE, "Triforce Doesn't Remove Sword Jinxes" },
    { qr_ITEMBUBBLE, "Things That Remove Sword Jinxes Also Remove Item Jinxes" },
    { -1, NULL }
};

const Rule enemies[] =
{
    { qr_NEWENEMYTILES, "Use New Enemy Tiles" },
    { qr_ENEMIESZAXIS, "Enemies Jump/Fly Through Z-Axis" },
    { qr_HIDECARRIEDITEMS, "Hide Enemy-Carried Items" },
    { qr_ALWAYSRET, "Enemies Always Return" },
    { qr_NOTMPNORET, "Temporary No Return Disabled" },
    { qr_KILLALL, "Must Kill All Bosses To Set 'No Return' Screen State" },
    { qr_MEANTRAPS, "Multi-Directional Traps" },
    { qr_MEANPLACEDTRAPS, "Line-of-Sight Traps Can Move Across Entire Screen" },
    { qr_PHANTOMPLACEDTRAPS, "Constant Traps Can Pass Through Enemies" },
    { qr_WALLFLIERS, "Flying Enemies Can Appear on Unwalkable Combos" },
    { qr_BRKNSHLDTILES, "Broken Enemy Shield Tiles" },
    { qr_NOFLASHDEATH, "Enemies Don't Flash When Dying" },
    { qr_SHADOWS, "Show Shadows" },
    { qr_TRANSSHADOWS, "Translucent Shadows" },
    { qr_SHADOWSFLICKER, "Shadows Flicker" },
    { qr_ENEMIESFLICKER, "Enemies Flicker When Hit" },
    { -1, NULL }
};

const Rule nes_fixes[] =
{
    { qr_FREEFORM, "Freeform Dungeons" },
    { qr_SAFEENEMYFADE, "Can Safely Trigger Armos/Grave From The South" },
    { qr_ITEMSONEDGES, "Can Use Items/Weapons on Edge of Screen" },
    { qr_LINKDUNGEONPOSFIX, "Fix Link's Position in Dungeons" },
    { qr_RLFIX, "Raft/Ladder Sprite Direction Fix" },
    { qr_NOLEVEL3FIX, "No Palette 3 CSet 6 Fix" },
    { qr_BOMBHOLDFIX, "Link Holds Special Bombs Over His Head" },
    { qr_HOLDNOSTOPMUSIC, "Holding Up Items Doesn't Restart Music" },
    { qr_CAVEEXITNOSTOPMUSIC, "Leaving Item Cellar/Passageway Doesn't Restart Music" },
    { qr_OVERWORLDTUNIC, "Tunic Color Can Change On Overworld" },
    { qr_SWORDWANDFLIPFIX, "Sword/Wand Flip Fix" },
    { qr_PUSHBLOCKCSETFIX, "Push Block CSet Fix" },
    { qr_TRAPPOSFIX, "Trap Position Fix" },
    { qr_NOBORDER, "No Invisible Border on Non-Dungeon Dmaps" },
    { qr_OLDPICKUP, "Items Disappear During Hold-Up" },
    { qr_SUBSCREENOVERSPRITES, "Subscreen Appears Above Sprites" },
    { qr_BOMBDARKNUTFIX, "Correct Bomb/Darknut Interaction" },
    { qr_OFFSETEWPNCOLLISIONFIX, "Correct Offset Enemy Weapon Collision Detection" },
    { qr_ITEMSINPASSAGEWAYS, "Special Items Don't Appear In Passageways" },
    { qr_NOFLICKER, "No NES Sprite Flicker" },
    { qr_FIREPROOFLINK2, "Invincible Link Isn't Hurt By Own Fire Weapons" },
    { qr_NOITEMOFFSET, "No Position Offset Of Screen Items" },
    { qr_LADDERANYWHERE, "Allow Ladder Anywhere" },
    { -1, NULL }
};

const Rule misc[] =
{
    { qr_SELECTAWPN, "Can Select A-Button Weapon On Subscreen" },
    { qr_MORESOUNDS, "More Sound Effects" },
    { qr_FASTFILL, "Fast Heart Refill" },
    { qr_QUARTERHEART, "Enable 1/4 Heart Life Bar" },
    { qr_ALLOWFASTMSG, "Messages Can Be Sped Up With The A Button" },
    { qr_ALLOWMSGBYPASS, "Messages Can Be Bypassed With The B Button" },
    { qr_MSGFREEZE, "Messages Freeze All Action" },
    { qr_MSGDISAPPEAR, "Messages Disappear" },
    { qr_TIME, "Show Time On Subscreen" },
    { qr_HEARTRINGFIX, "Healing Fairy Heart Circle Is Centered Around Link" },
    { qr_NOHEARTRING, "No Healing Fairy Heart Circle" },
    { qr_NOSAVE, "No Saving When Continuing" },
    { qr_NOCONTINUE, "Skip Continue Screen" },
    { qr_NOGUYFIRES, "No Fires In Special Rooms" },
    { qr_NOGUYPOOF, "Special Room Guys Don't Create A Puff When Appearing" },
    { qr_LOG, "Log Game Events To Allegro.log" },
    { qr_SCRIPTERRLOG, "Log Script Errors To Allegro.log" },
    { qr_SHOPCHEAT, "Draining Rupees Can Still Be Spent" },
    { -1, NULL }
};

const Rule compat[] =
{
    { qr_GOTOLESSNOTEQUAL, "Old GOTOLESS Behavior" },
    { qr_OLDLENSORDER, "Old Lens Drawing Order" },
    { qr_NOFAIRYGUYFIRES, "No Fairy Guy Fires" },
    { qr_TRIGGERSREPEAT, "Continuous Step Triggers" },
    { qr_HOOKSHOTDOWNBUG, "Downward Hookshot Bug" },
    { qr_REPLACEOPENDOORS, "Fix Open Door Solidity" },
    { qr_NOSOLIDDAMAGECOMBOS, "No Solid Damage Combos" },
    { qr_OLDHOOKSHOTGRAB, "Old Hookshot Grab Checking" },
    { qr_PEAHATCLOCKVULN, "Peahats Are Vulnerable When Frozen By Clocks" },
    { qr_OFFSCREENWEAPONS, "Weapons With No Collision Detection Move Offscreen" },
    { -1, NULL }
};

struct Tab
{
    const char *name;
    const Rule *rules;
};

const Tab tabs[] =
{
    { "&Animation", animation },
    { "C&ombos", combos },
    { "&Items", items },
    { "&Enemy", enemies },
    { "&NES fixes", nes_fixes },
    { "&Misc.", misc },
    { "&Backward compatibility", compat },
};
enum { TABS = sizeof(tabs) / sizeof(tabs[0]) };

int count(const Rule *r)
{
    int n = 0;
    while(r[n].id >= 0)
        ++n;
    return n;
}

// The window, built on every opening: 0 window, 1 timer, 2 tabs, 3 OK, 4 Cancel, 5 F1, then every tab's rules.
enum { D_WIN, D_TIMER, D_TABS, D_OK, D_CANCEL, D_HELP, D_FIRST };
enum { ROW = 10, TOP = 33, WIDTH = 310 };

// a fixed array: the editor keeps the address of every window large_dialog() enlarged
// (zq_remember_dialog), to put it back when the layout changes
enum { MOST = D_FIRST + 160 };
DIALOG g_dlg[MOST];
int g_objects;
std::vector<const Rule *> g_rule;               // the rule each check box (from D_FIRST) sets
std::vector<int> g_lists[TABS];                 // the dialog objects each tab shows, -1 ended
TABPANEL g_panel[TABS + 1];
char g_tab_text[TABS][32];

DIALOG object(int (*proc)(int, DIALOG *, int), int x, int y, int w, int h, int key, int flags, const void *dp)
{
    DIALOG d;
    memset(&d, 0, sizeof(d));
    d.proc = proc;
    d.x = x;
    d.y = y;
    d.w = w;
    d.h = h;
    d.fg = vc(14);
    d.bg = vc(1);
    d.key = key;
    d.flags = flags;
    d.dp = (void *)dp;
    return d;
}

void build()
{
    int most = 0;

    for(int t = 0; t < TABS; ++t)
        if(count(tabs[t].rules) > most)
            most = count(tabs[t].rules);

    const int tab_h = 10 + most * ROW + 8;
    const int height = TOP + tab_h + 6 + 21 + 8;
    g_objects = 0;
    g_rule.clear();
    const int by = 23 + tab_h + 6;
    g_dlg[g_objects++] = object(jwin_win_proc, 0, 0, WIDTH, height, 0, D_EXIT, "Quest rules");
    g_dlg[g_objects++] = object(d_timer_proc, 0, 0, 0, 0, 0, 0, NULL);
    g_dlg[g_objects++] = object(jwin_tab_proc, 5, 23, WIDTH - 10, tab_h, 0, 0, g_panel);
    g_dlg[g_objects++] = object(jwin_button_proc, WIDTH / 2 - 61 - 10, by, 61, 21, 13, D_EXIT, "O&K");
    g_dlg[g_objects++] = object(jwin_button_proc, WIDTH / 2 + 10, by, 61, 21, 27, D_EXIT, "&Cancel");
    g_dlg[g_objects++] = object(d_keyboard_proc, 0, 0, 0, 0, 0, 0, (const void *)onHelp);
    g_dlg[D_TABS].d1 = 1;
    g_dlg[D_HELP].d1 = KEY_F1;

    for(int t = 0; t < TABS; ++t)
    {
        g_lists[t].clear();
        const Rule *r = tabs[t].rules;

        for(int i = 0; r[i].id >= 0 && g_objects < MOST - 1; ++i)
        {
            g_lists[t].push_back(g_objects);
            g_rule.push_back(&r[i]);
            DIALOG c = object(jwin_check_proc, 10, TOP + 10 + i * ROW, WIDTH - 20, 9, 0, 0, r[i].name);
            c.d1 = 1;
            g_dlg[g_objects++] = c;
        }

        g_lists[t].push_back(-1);
        snprintf(g_tab_text[t], sizeof(g_tab_text[t]), "%s", tabs[t].name);
        g_panel[t].text = g_tab_text[t];
        g_panel[t].flags = t == 0 ? D_SELECTED : 0;
        g_panel[t].dialog = &g_lists[t][0];
        g_panel[t].objects = 0;
        g_panel[t].xy = NULL;
    }

    memset(&g_panel[TABS], 0, sizeof(g_panel[TABS]));
    memset(&g_dlg[g_objects], 0, sizeof(DIALOG) * (MOST - g_objects));
    g_dlg[D_TABS].dp3 = g_dlg;
}

} // namespace

// Quest > Rules in 2.50.2 and 2.50.3.
int zq_ui_rules_250()
{
    // a 2.55 or 3.0 quest's rules have their own editor (this interface isn't offered for them)
    bool shown = false;
    zq_edit_rules_newer(0, &shown);

    if(shown)
        return D_O_K;

    build();

    if(is_large)
        large_dialog(g_dlg);

    g_dlg[0].dp2 = lfont;

    for(size_t i = 0; i < g_rule.size(); ++i)
        g_dlg[D_FIRST + i].flags = get_bit(quest_rules, g_rule[i]->id) ? D_SELECTED : 0;

    if(zc_popup_dialog(g_dlg, D_OK) != D_OK)
        return D_O_K;

    saved = false;

    for(size_t i = 0; i < g_rule.size(); ++i)
        set_bit(quest_rules, g_rule[i]->id, (g_dlg[D_FIRST + i].flags & D_SELECTED) ? 1 : 0);

    // as 2.50.2's OK did: for 2.50.0 and 2.50.1, and the subscreen style that follows the wipes
    if(get_bit(quest_rules, qr_VERYFASTSCROLLING))
        set_bit(quest_rules, qr_FASTDNGN, 1);

    zinit.subscreen_style = get_bit(quest_rules, qr_COOLSCROLL) ? 1 : 0;
    return D_O_K;
}

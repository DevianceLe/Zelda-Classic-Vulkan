// The MISC section of 2.55 and 3.0 Nightly quests (versions 7-23) as a tape (zq_tape.h), field
// by field as q255_read_misc reads it.
//
// Shops, infos, warp rings, the triforce and the end string are 2.53's: its dialogs edit them
// and write_misc_255 writes them. The rest is 2.55 / 3.0 only ("extra"): the shops' item info
// strings, misc sprites, bottle types and bottle shops, misc sounds, and the save menus (kept
// as their bytes).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>

#include "zq_tape.h"

namespace
{

int g_misc_v;

void walk_cstr(TapeParser &p)
{
    p.rskip((size_t)p.rd(1));
}

// q255_basic.cpp's skip_save_menus
void walk_save_menus(TapeParser &p)
{
    const int v = g_misc_v;
    const int count = (int)p.rd(1);

    for(int i = 0; i < count && !p.bad; ++i)
    {
        if(p.rd(1) != 0)        // an empty menu
            continue;

        walk_cstr(p);           // name
        p.rskip(2 + 4 + 1);     // flags, cursor tile and cset
        p.rskip(v < 19 ? 2 : 4);        // cursor and choose sounds
        p.rskip(7 + 3 + 2 + 4 + 3);     // colors and layout, closing, music, background

        if(v >= 23)
            p.rskip(4);

        const int options = (int)p.rd(1);

        for(int j = 0; j < options && !p.bad; ++j)
        {
            walk_cstr(p);
            p.rskip(2 + 1 + 1 + 4);

            if(v >= 22)
            {
                p.rskip(2);
                const size_t args = (size_t)p.rd(2);
                p.rskip((args < 8 ? args : 8) * 4);
                p.rskip((size_t)p.rd(2) * (2 + 4));
            }
            else
                p.rskip(2);

            if(v >= 23)
                p.rskip(3);
        }

        if(v >= 23)
        {
            const int texts = (int)p.rd(1);

            for(int j = 0; j < texts && !p.bad; ++j)
            {
                walk_cstr(p);
                p.rskip(3 + 4 + 3);
            }
        }
    }

    p.rskip(2);     // the game over and F6 menus
}

// upstream's names for the misc sound and sprite slots (zq/dialog/misc_sfx.cpp, misc_sprs.cpp)
const char *const g_misc_sfx_names[][2] =
{
    { "Grass cut", "Played when bush/grass/etc is cut, and does not have a custom SFX set." },
    { "'Switch with Hero'", "Played when an enemy's 'Switch w/ Hero' defense is triggered, or a switchhook with 0 for 'Switch Sound' switches." },
    { "Low health beep", "Played when the Hero's health is low." },
    { "Error", "Played when something errors (Ex. Using an item without enough magic)" },
    { "Hero hurt", "Plays when the Hero is hurt." },
    { "Hammer peg pound", "Plays when a hammer peg is pounded." },
    { "Subscreen item select", "Plays when an item is assigned to a button in the subscreen." },
    { "Subscreen cursor move", "Plays when the subscreen cursor moves." },
    { "Refill", "Plays when a counter is being refilled." },
    { "Rupee drain", "Plays when rupees are being drained." },
    { "Sword tap", "Plays when the sword is tapped against a combo." },
    { "Sword tap (hollow)", "Plays when the sword is tapped against a bombable door/flag. Needs the rule 'Separate Bombable Tapping SFX'." },
    { "Hero lands", "Plays when the Hero lands from the air, if the combo they land on doesn't set its own sound. Not with the rule 'Old Landing SFX'." },
};

const char *const g_misc_spr_names[][2] =
{
    { "Falling", "Shown when an enemy/item/etc falls in a pit" },
    { "Drowning", "Shown when an enemy/item/etc falls in non-lava liquid" },
    { "Lava drowning", "Shown when an enemy/item/etc falls in lava" },
    { "Switch poof", "Shown when 'switching' objects with the poof switch style." },
};

// A slot of 256: the named ones are always shown, the others only when set.
void misc_slot(TapeParser &p, const char *key_fmt, int i, int width, int list, const char *const (*names)[2], int named)
{
    char k[32], l[64];
    snprintf(k, sizeof(k), key_fmt, i);

    if(i < named)
        snprintf(l, sizeof(l), "%s", names[i][0]);
    else
        snprintf(l, sizeof(l), "Slot %d", i);

    const int64_t val = p.num(k, l, width, false, TK_ENUM, list, i, i < named ? names[i][1] : NULL);

    if(i >= named && !val && p.t && !p.t->f.empty() && p.t->f.back().key == k)
        p.t->f.back().kind = TK_HIDDEN;
}

} // namespace

bool zq_tape_misc(TapeParser &p, int v)
{
    const int item_w = v >= 20 ? 2 : 1;
    char k[48], l[64];

    p.group("Shops");
    const int shops = (int)p.hidden("shops", 2);

    for(int i = 0; i < shops && !p.bad; ++i)
    {
        snprintf(k, sizeof(k), "shop%d.name", i);
        p.text(k, "Name", 32);

        for(int j = 0; j < 3; ++j)
        {
            snprintf(k, sizeof(k), "shop%d.item%d", i, j);
            p.u(k, "Item", item_w, TL_ITEM);
        }

        for(int j = 0; j < 3; ++j)
        {
            snprintf(k, sizeof(k), "shop%d.price%d", i, j);
            p.u(k, "Price", 2);
        }

        for(int j = 0; j < 3; ++j)
        {
            snprintf(k, sizeof(k), "shop%d.has%d", i, j);
            p.u(k, "Has item", 1);
        }
    }

    p.group("Infos");
    const int infos = (int)p.hidden("infos", 2);

    for(int i = 0; i < infos && !p.bad; ++i)
    {
        snprintf(k, sizeof(k), "info%d.name", i);
        p.text(k, "Name", 32);

        for(int j = 0; j < 3; ++j)
        {
            snprintf(k, sizeof(k), "info%d.str%d", i, j);
            p.u(k, "String", 2);
        }

        for(int j = 0; j < 3; ++j)
        {
            snprintf(k, sizeof(k), "info%d.price%d", i, j);
            p.u(k, "Price", 2);
        }
    }

    p.group("Warp rings");
    const int rings = (int)p.hidden("rings", 2);

    for(int i = 0; i < rings && !p.bad; ++i)
    {
        for(int j = 0; j < 9; ++j)
        {
            snprintf(k, sizeof(k), "ring%d.dmap%d", i, j);
            p.u(k, "DMap", 2);
        }

        for(int j = 0; j < 9; ++j)
        {
            snprintf(k, sizeof(k), "ring%d.scr%d", i, j);
            p.u(k, "Screen", 1);
        }

        snprintf(k, sizeof(k), "ring%d.size", i);
        p.u(k, "Size", 1);
    }

    p.group("Triforce");

    for(int i = 0; i < 8; ++i)
    {
        snprintf(k, sizeof(k), "triforce%d", i);
        p.u(k, "Piece", 1);
    }

    p.u("endstring", "End string", 2);
    p.extra(true);

    if(v >= 8)
    {
        p.group("Shop strings");

        for(int i = 0; i < shops && !p.bad; ++i)
            for(int j = 0; j < 3; ++j)
            {
                snprintf(k, sizeof(k), "shop%d.str%d", i, j);
                snprintf(l, sizeof(l), "Item %d: info string", j + 1);
                p.u(k, l, 2);
            }
    }

    if(v >= 9)
        p.blob("quest_misc", "quest misc values and retired string labels", 32 * 4 + 32 * 128);

    if(v >= 11)
        p.blob("zscript_version", "last compiled ZScript version", 4);

    if(v >= 12)
    {
        p.group("Misc sprites");

        for(int i = 0; i < 256; ++i)
            misc_slot(p, "spr%d", i, v >= 21 ? 2 : 1, TL_SPRITE, g_misc_spr_names,
                      (int)(sizeof(g_misc_spr_names) / sizeof(g_misc_spr_names[0])));
    }

    if(v >= 13)
    {
        p.group("Bottle types");

        for(int i = 0; i < 64 && !p.bad; ++i)
        {
            snprintf(k, sizeof(k), "bt%d.name", i);
            p.text(k, "Name", 32);

            for(int j = 0; j < 3; ++j)
            {
                snprintf(k, sizeof(k), "bt%d.counter%d", i, j);
                snprintf(l, sizeof(l), "Refills counter %d", j + 1);
                p.s(k, l, 1, TL_COUNTER);
                snprintf(k, sizeof(k), "bt%d.amount%d", i, j);
                snprintf(l, sizeof(l), "Amount %d", j + 1);
                p.u(k, l, 2);
            }

            snprintf(k, sizeof(k), "bt%d.flags", i);
            p.flags(k, "Flags", 1, TL_BOTTLEFLAGS);
            snprintf(k, sizeof(k), "bt%d.next", i);
            p.num(k, "When used, becomes type", 1, false, TK_ENUM, TL_BOTTLETYPE, 0,
                  "The bottle type this one turns into once used (0: an empty bottle).");
        }

        p.group("Bottle shops");

        for(int i = 0; i < 256 && !p.bad; ++i)
        {
            snprintf(k, sizeof(k), "bs%d.name", i);
            p.text(k, "Name", 32);

            for(int j = 0; j < 3; ++j)
            {
                snprintf(k, sizeof(k), "bs%d.fill%d", i, j);
                snprintf(l, sizeof(l), "Item %d: fills with", j + 1);
                p.u(k, l, 1, TL_BOTTLETYPE);
                snprintf(k, sizeof(k), "bs%d.combo%d", i, j);
                snprintf(l, sizeof(l), "Item %d: display combo", j + 1);
                p.u(k, l, 2);
                snprintf(k, sizeof(k), "bs%d.cset%d", i, j);
                snprintf(l, sizeof(l), "Item %d: display CSet", j + 1);
                p.u(k, l, 1);
                snprintf(k, sizeof(k), "bs%d.price%d", i, j);
                snprintf(l, sizeof(l), "Item %d: price", j + 1);
                p.u(k, l, 2);
                snprintf(k, sizeof(k), "bs%d.str%d", i, j);
                snprintf(l, sizeof(l), "Item %d: info string", j + 1);
                p.u(k, l, 2);
            }
        }
    }

    if(v >= 14)
    {
        p.group("Misc sounds");

        for(int i = 0; i < 256; ++i)
            misc_slot(p, "sfx%d", i, v >= 19 ? 2 : 1, TL_SFX, g_misc_sfx_names,
                      (int)(sizeof(g_misc_sfx_names) / sizeof(g_misc_sfx_names[0])));
    }

    if(v >= 17)
    {
        g_misc_v = v;
        p.raw("save_menus", "save menus", walk_save_menus, 3);
    }

    return !p.bad;
}

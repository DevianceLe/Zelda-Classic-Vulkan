// The tape schema of the LINK section of 2.55 and 3.0 Nightly quests (versions 5 to 16): the
// hero's sprites, swim speed, hammer offsets and defenses, the whole section's data in file
// order, as upstream's readherosprites2 (version 5) and readherosprites3 (6 and up) read them
// (core/qst_herosprites.cpp in 3.0; the same layout in 2.55.17).
//
// Each sprite is a tile (a word in version 5, a long from 6), a flip and an extend (size)
// byte; the lifting sprites have a frame count after them from version 16. Keys are
// "<set>.<which>.tile" / ".flip" / ".ext" / ".frames", with the sets and their entries named
// as in zq_hero_link_sets below, which the hero editor (zq_hero_editor.cpp) also uses.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <string>

#include "zdefs.h"
#include "zq_tape.h"
#include "zq_item_names.h"

namespace
{

const char *const g_dirs[4] = { "up", "down", "left", "right" };
const char *const g_dir_labels[4] = { "Up", "Down", "Left", "Right" };

void sprite(TapeParser &p, const std::string &key, const std::string &label, bool wide, bool frames)
{
    const std::string kt = key + ".tile", kf = key + ".flip", ke = key + ".ext", kn = key + ".frames";
    const std::string lt = label + " tile", lf = label + " flip", le = label + " size", ln = label + " frames";

    if(wide)
        p.s(kt.c_str(), lt.c_str(), 4);
    else
        p.u(kt.c_str(), lt.c_str(), 2);

    p.u(kf.c_str(), lf.c_str(), 1);
    p.u(ke.c_str(), le.c_str(), 1);

    if(frames)
        p.u(kn.c_str(), ln.c_str(), 1);
}

// a set of four, one per direction
void dirs(TapeParser &p, const char *set, const char *label, bool wide, bool frames = false)
{
    for(int d = 0; d < 4; ++d)
        sprite(p, std::string(set) + "." + g_dirs[d], std::string(label) + " " + g_dir_labels[d], wide, frames);
}

// a set of n, numbered from 1
void numbered(TapeParser &p, const char *set, const char *label, int n, bool wide)
{
    for(int q = 0; q < n; ++q)
    {
        char k[64], l[96];
        snprintf(k, sizeof(k), "%s.%d", set, q + 1);
        snprintf(l, sizeof(l), "%s %d", label, q + 1);
        sprite(p, k, l, wide, false);
    }
}

// upstream's weapon types (core/zdefs.h): the 146 the defenses are stored for
const int WMAX = 146;

} // namespace

bool zq_tape_link(TapeParser &p, int v)
{
    if(v < 5 || v > 16)
        return false;

    const bool wide = v >= 6;
    p.group("Land");
    dirs(p, "walk", "Walk", wide);
    dirs(p, "stab", "Stab", wide);
    dirs(p, "slash", "Slash", wide);
    p.group("Liquid");
    dirs(p, "float", "Float", wide);
    dirs(p, "swim", "Swim", wide);
    dirs(p, "dive", "Dive", wide);
    p.group("Land");
    dirs(p, "pound", "Pound", wide);
    sprite(p, "cast", "Casting", wide, false);

    // holding up an item: one hand and two hands, and from version 7 the sword
    const int holds = v > 6 ? 3 : 2;

    for(int i = 0; i < 2; ++i)
    {
        p.group(i ? "Liquid" : "Land");
        static const char *const which[3] = { "1", "2", "sword" };
        static const char *const wl[3] = { "1 hand", "2 hands", "Sword" };

        for(int j = 0; j < holds; ++j)
            sprite(p, std::string(i ? "hold_water." : "hold_land.") + which[j], std::string(i ? "Water hold " : "Land hold ") + wl[j],
                   wide, false);
    }

    p.group("Land");
    dirs(p, "jump", "Jump", wide);
    dirs(p, "charge", "Charge", wide);
    p.group("Settings");
    p.u("swim_speed", "Swim speed (50 slow, 67 fast)", 1);

    if(v > 6)
    {
        p.group("Other sprites");
        dirs(p, "frozen", "Frozen", wide);
        dirs(p, "frozen_water", "Frozen (water)", wide);
        dirs(p, "onfire", "On fire", wide);
        dirs(p, "onfire_water", "On fire (water)", wide);
        dirs(p, "digging", "Digging", wide);
        dirs(p, "usingrod", "Using a rod", wide);
        dirs(p, "usingcane", "Using a cane", wide);
        dirs(p, "pushing", "Pushing", wide);
        p.group("Land");
        dirs(p, "lifting", "Lifting", wide, v > 15);
        dirs(p, "liftwalk", "Lift + walk", wide);
        p.group("Other sprites");
        dirs(p, "stunned", "Stunned", wide);
        dirs(p, "stunned_water", "Stunned (water)", wide);
        p.group("Liquid");
        dirs(p, "drown", "Drown", wide);
        dirs(p, "lavadrown", "Lava drown", wide);
        p.group("Land");
        dirs(p, "falling", "Falling", wide);
        p.group("Other sprites");
        dirs(p, "shocked", "Shocked", wide);
        dirs(p, "shocked_water", "Shocked (water)", wide);
        dirs(p, "pullsword", "Pulling a sword", wide);
        dirs(p, "reading", "Reading", wide);
        dirs(p, "slash180", "Slash 180", wide);
        dirs(p, "slashz4", "Slash (Z4)", wide);
        dirs(p, "dash", "Dash", wide);
        dirs(p, "bonk", "Bonk", wide);
        numbered(p, "medallion", "Medallion", 3, wide);
    }

    p.group("Side liquid");

    if(v > 8)
        dirs(p, "sideswim", "Side swim", wide);

    if(v > 9)
    {
        dirs(p, "sideswimslash", "Side swim slash", wide);
        dirs(p, "sideswimstab", "Side swim stab", wide);
        dirs(p, "sideswimpound", "Side swim pound", wide);
        dirs(p, "sideswimcharge", "Side swim charge", wide);
    }

    if(v > 10)
    {
        p.group("Settings");

        for(int d = 0; d < 4; ++d)
        {
            char k[32], l[64];
            snprintf(k, sizeof(k), "hammer.%s", g_dirs[d]);
            snprintf(l, sizeof(l), "Hammer offset %s", g_dir_labels[d]);
            p.s(k, l, 4);
        }
    }

    p.group("Side liquid");

    if(v > 11)
    {
        static const char *const which[3] = { "1", "2", "sword" };
        static const char *const wl[3] = { "Side hold 1 hand", "Side hold 2 hands", "Side hold sword" };

        for(int j = 0; j < 3; ++j)
            sprite(p, std::string("sideswimhold.") + which[j], wl[j], wide, false);
    }

    if(v > 12)
        sprite(p, "sideswimcast", "Side swim casting", wide, false);

    if(v > 13)
        dirs(p, "sidedrown", "Side drown", wide);

    if(v > 14)
    {
        p.group("Land");
        dirs(p, "revslash", "Slash 2", wide);
    }

    if(v > 7)
    {
        p.group("Defenses");

        for(int w = 0; w < WMAX; ++w)
        {
            char k[16], l[96];
            snprintf(k, sizeof(k), "def%d", w);
            const char *n = q30_weapon_type_name(w);
            snprintf(l, sizeof(l), "%s defense", n && n[0] ? n : "Weapon");
            p.u(k, l, 1, TL_DEFENSE);
        }
    }

    return !p.bad;
}

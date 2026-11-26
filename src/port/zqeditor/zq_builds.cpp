// The build catalog (zq_builds.h): every ZC build ZQuest can start a new quest in or save a quest as,
// gathered from the three generations' lists and sorted oldest first.
//
// Builds compare by generation first, then by order within it: every 3.0 build is newer than every 2.55
// one (the 2.55 line kept releasing alongside 3.0), every 2.55 one newer than every 2.53.1-and-older
// one. Save As offers the build the open quest was made in (the first generation whose made_order isn't
// -1 says which) and every build after it: never an older one (a writer that can't hold the quest
// refuses with its reason). File > New offers every build that has a make_new.
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <algorithm>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_builds.h"

extern zquestheader header;
extern int q255_header_version[3];

namespace
{

bool by_order(const zq_build &a, const zq_build &b)
{
    return a.gen != b.gen ? a.gen < b.gen : a.order < b.order;
}

std::vector<zq_build> gather()
{
    std::vector<zq_build> all;
    zq_builds_192(all);
    zq_builds_old(all);
    zq_builds_255(all);
    zq_builds_30(all);
    std::stable_sort(all.begin(), all.end(), by_order);
    return all;
}

// The generation the open quest's header says, for a quest no generation claims.
int header_gen()
{
    const int v = header.zelda_version;

    if(v >= 0x255)
        return q255_header_version[0] >= 3 ? 0x300 : 0x255;

    if(v >= 0x250)
        return 0x250;

    if(v >= 0x211)
        return 0x211;

    if(v >= 0x210)
        return 0x210;

    return v >= 0x192 ? 0x192 : 0x190;
}

} // namespace

// Every build of every generation, sorted (the test switches in zq_builds_old.cpp use it).
std::vector<zq_build> zq_builds_all()
{
    return gather();
}


// The open quest's build: its order (-1 when no generation claims it) and generation.
long zq_builds_made(int *gen)
{
    long order = zq_made_order_old();
    int g = 0;

    if(order != -1)
        g = 0;      // the old generations: the build's own, below
    else if((order = zq_made_order_255()) != -1)
        g = 0x255;
    else if((order = zq_made_order_30()) != -1)
        g = 0x300;

    if(order == -1 || g == 0)
    {
        // a claimed build's own generation, when the catalog lists it
        std::vector<zq_build> all = gather();
        g = header_gen();

        for(size_t i = 0; order != -1 && i < all.size(); ++i)
            if(all[i].order == order)
                g = all[i].gen;
    }

    if(gen)
        *gen = g;

    return order;
}

// Save As: the builds after the open quest's own, oldest first (own_name: the quest's own build as
// the catalog names it, empty when the catalog doesn't list it).
void zq_builds_for_save_as(std::vector<zq_build> &newer, std::string &own_name)
{
    newer.clear();
    own_name.clear();
    int gen = 0;
    const long made = zq_builds_made(&gen);
    std::vector<zq_build> all = gather();

    for(size_t i = 0; i < all.size(); ++i)
    {
        const zq_build &b = all[i];

        if(made != -1 && b.order == made && b.gen == gen)
        {
            own_name = b.name;
            continue;
        }

        // never older: an older generation, or one of its own generation before the quest's build (with
        // no build known for the quest, only newer generations than its header's)
        if(!b.save_as || b.gen < gen || (b.gen == gen && (made == -1 || b.order < made)))
            continue;

        // one row a name
        bool listed = b.name == own_name;

        for(size_t k = 0; k < newer.size() && !listed; ++k)
            listed = newer[k].name == b.name;

        if(!listed)
            newer.push_back(b);
    }
}

// File > New: every build with a make_new, oldest first.
void zq_builds_for_new(std::vector<zq_build> &out)
{
    out.clear();
    std::vector<zq_build> all = gather();

    for(size_t i = 0; i < all.size(); ++i)
    {
        if(!all[i].make_new)
            continue;

        // one row a name
        bool listed = false;

        for(size_t k = 0; k < out.size() && !listed; ++k)
            listed = out[k].name == all[i].name;

        if(!listed)
            out.push_back(all[i]);
    }
}

// ==== STAND-INS ======================================================================================
// Until zqbold's zq_builds_old.cpp and zqb30's zq_builds_30.cpp are merged: the builds ZQuest offered
// before the catalog (File > New in 2.10, 2.50.1, 2.53.0, 2.53.1 and 3.0 Nightly; Save As 2.53.1 and
// 3.0 Nightly), through the code that made them (zq_files.cpp). src/port/zqeditor.cmake defines
// ZQ_HAVE_BUILDS_OLD / ZQ_HAVE_BUILDS_30 / ZQ_HAVE_BUILDS_192 when those files are there (zq192's
// zq_builds_192.cpp: 1.90 and 1.92); this whole block can then go.
#if !defined(ZQ_HAVE_BUILDS_OLD) || !defined(ZQ_HAVE_BUILDS_30)
#include "zq_writers.h"

bool zq_files_new_fixed(const char *version);          // zq_files.cpp
bool zq_files_new_fixed_offered(const char *version);
bool zq_files_save_253(const char *path);
bool zq_files_save_newer(const char *path, int target);
bool zq_newer_upgrades(int target);                    // zq_writers.h

namespace
{

bool standin_new(const zq_build &b)
{
    return zq_files_new_fixed(b.name.c_str());
}

void standin_add(std::vector<zq_build> &out, const char *name, long order, int gen, int tag,
                 bool (*save_as)(const zq_build &, const char *), bool offer_new)
{
    zq_build b;
    b.name = name;
    b.order = order;
    b.gen = gen;
    b.tag = tag;
    b.save_as = save_as;
    b.make_new = offer_new && zq_files_new_fixed_offered(name) ? standin_new : NULL;
    out.push_back(b);
}

} // namespace
#endif

#ifndef ZQ_HAVE_BUILDS_OLD
namespace
{

// release dates, roughly (zqbold's file has the real ones)
const long SI_190 = 20021215, SI_192 = 20070101, SI_210 = 20080416, SI_211 = 20090101, SI_2500 = 20121212,
           SI_2501 = 20131201, SI_2502 = 20151107, SI_2503 = 20160827, SI_2530 = 20190912, SI_2531 = 20201215;

bool standin_save_253(const zq_build &, const char *path)
{
    return zq_files_save_253(path);
}

} // namespace

void zq_builds_old(std::vector<zq_build> &out)
{
    standin_add(out, "2.10", SI_210, 0x210, 0, NULL, true);
    standin_add(out, "2.50.1", SI_2501, 0x250, 1, NULL, true);
    standin_add(out, "2.53.0", SI_2530, 0x250, 2, NULL, true);
    standin_add(out, "2.53.1", SI_2531, 0x250, 3, standin_save_253, true);
}

long zq_made_order_old()
{
    const int v = header.zelda_version, b = header.build;

    if(v >= 0x254)
        return -1;

    if(v == 0x250)
        return b <= 23 ? SI_2500 - 1 : b == 24 ? SI_2500 : b <= 28 ? SI_2501 : b == 29 ? SI_2502 : b == 30 ? SI_2503
             : b <= 32 ? SI_2530 : SI_2531;

    if(v >= 0x211)
        return SI_211;

    if(v >= 0x210)
        return SI_210;

    return v >= 0x192 ? SI_192 : SI_190;
}
#endif

#ifndef ZQ_HAVE_BUILDS_30
namespace
{

const long SI_30 = 20261108;     // upstream main at ef321f8, what "3.0 Nightly" writes

bool standin_save_30(const zq_build &, const char *path)
{
    return zq_files_save_newer(path, ZQ_NEWER_30);
}

} // namespace

void zq_builds_30(std::vector<zq_build> &out)
{
    standin_add(out, "3.0 Nightly", SI_30, 0x300, 0, standin_save_30, true);
}

long zq_made_order_30()
{
    if(header.zelda_version < 0x255)
        return -1;

    // a 3.0 prerelease older than 3.0 Nightly's sections: just before it
    return zq_newer_upgrades(ZQ_NEWER_30) ? SI_30 - 1 : SI_30;
}
#endif
#ifndef ZQ_HAVE_BUILDS_192
// zq192's 1.90 and 1.92 (File > New only): none yet
void zq_builds_192(std::vector<zq_build> &)
{
}
#endif
// ==== END OF STAND-INS ===============================================================================

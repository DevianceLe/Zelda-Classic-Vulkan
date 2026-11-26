// The 2.55 releases in the build catalog (zq_builds.h): 2.55.0 to 2.55.19, each to save a quest as and
// to start a new quest in.
//
// The releases' data is generated from upstream's source at each release (zq_builds_255_data.h, by
// tools/zq_gen_255_builds.py): its date, its three section versions that changed during 2.55, the
// tilesets its File > New starts from and its rule lists. Every release writes the same formats but for
// those three sections, so a save as one of them is the newer-version save (zq_writers.h, ZQ_NEWER_255)
// with zq_newer_255_release() set to it: the header says that release saved the quest, and the writers
// write its section versions and make only the load fixes its loader makes.
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_builds.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zq_rule_names.h"
#include "zqn_255.h"
#include "zq_builds_255_data.h"

extern zquestheader header;
extern int q255_header_version_ids[8];
bool zq255_quest_is_newer();     // q255_store_zq.cpp

// zq_files.cpp: a save as a newer version (zq_save_newer, telling the user why when it can't), and
// File > New in a 2.55 release (its tileset dialog, then zq_new_quest_255 below).
bool zq_files_save_newer(const char *path, int target);
bool zq_files_new_255(int patch, int new_flow, const char *classic, const char *cambria);

namespace
{

int g_release = 19;

struct ReleaseGuard
{
    int was;
    explicit ReleaseGuard(int patch) : was(g_release) { zq_newer_set_255_release(patch); }
    ~ReleaseGuard() { g_release = was; }
};

const Zq255Release *release_of(const zq_build &b)
{
    return b.tag >= 0 && b.tag < zq255_release_count ? &zq255_releases[b.tag] : NULL;
}

bool save_as_255(const zq_build &b, const char *path)
{
    const Zq255Release *r = release_of(b);

    if(!r)
        return false;

    ReleaseGuard g(r->patch);
    return zq_files_save_newer(path, ZQ_NEWER_255);
}

bool make_new_255(const zq_build &b)
{
    const Zq255Release *r = release_of(b);

    if(!r)
        return false;

    return zq_files_new_255(r->patch, r->new_flow, r->classic, r->cambria);
}

// A part newer than 2.55.19 holds (an early 3.0 Nightly wrote 2.55 headers with 3.0's sections), or
// 3.0's music objects: not a 2.55 release's quest.
bool parts_newer_than_255()
{
    const dword amus = ZC_ID('A', 'M', 'U', 'S');

    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
    {
        const ZqQuestPart &p = zq_quest_file.parts[i];
        word v = 0, cv = 0, newest = 0, newest_cv = 0;

        if(p.id == amus)
            return true;

        zqn255_newest(p.id, ZQ_NEWER_255, &newest, &newest_cv);

        if(newest && zq_part_versions(p, &v, &cv) && v > newest)
            return true;
    }

    return false;
}

} // namespace

int zq_newer_255_release()
{
    return g_release;
}

void zq_newer_set_255_release(int patch)
{
    g_release = patch < 0 ? 0 : patch > 19 ? 19 : patch;
}

// readrules' "older than 2.55.<k>" fixes, by the release that added each: 2.55.6's was cherry-picked
// in 2.55.10 (before, 2.55.6 to 2.55.9 opened older quests without it), the others came with their
// own release (upstream's readrules at each release tag).
bool zq_rules_255_fix(int k)
{
    return g_release >= (k == 6 ? 10 : k);
}

bool zq_newer_255_has_fix(int target, int k)
{
    return target == ZQ_NEWER_30 || zq_rules_255_fix(k);
}

void zq_builds_255(std::vector<zq_build> &out)
{
    for(int i = 0; i < zq255_release_count; ++i)
    {
        const Zq255Release &r = zq255_releases[i];
        zq_build b;
        b.name = r.name;
        b.order = r.date;
        b.gen = 0x255;
        b.tag = i;
        b.save_as = save_as_255;
        b.make_new = make_new_255;
        out.push_back(b);
    }
}

// A 2.55 quest's header records the release that saved it (major, minor, patch; alpha, beta, gamma
// and release for the prereleases). A quest saved by a 2.55 alpha, beta or gamma, or with no version
// fields at all (2.55 headers before version 4), was made before 2.55.0: just before its date.
long zq_made_order_255()
{
    if(header.zelda_version < 0x254 || !zq255_quest_is_newer())
        return -1;

    const int *id = q255_header_version_ids;    // major, minor, patch, fourth, alpha, beta, gamma, release

    if(id[0] >= 3 || (id[0] == 2 && id[1] != 55) || (id[0] != 0 && id[0] != 2))
        return -1;

    if(parts_newer_than_255())
        return -1;

    const bool prerelease = id[0] == 0 || (!id[7] && (id[4] || id[5] || id[6]));

    if(prerelease)
        return zq255_releases[0].date - 1;

    for(int i = 0; i < zq255_release_count; ++i)
        if(zq255_releases[i].patch == id[2])
            return zq255_releases[i].date;

    return -1;      // a later 2.55 release than ZQuest knows
}

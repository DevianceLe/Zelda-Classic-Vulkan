// zq_builds.h: every ZC build ZQuest can start a new quest in or save a quest as.
#pragma once
#include <string>
#include <vector>
struct zq_build
{
    std::string name;   // as listed: "2.50.2", "2.53.1", "2.55.10", "3.0 Nightly 2026-11-20"
    long order;         // sorts every build oldest first across all generations (the release date as
                        // YYYYMMDD, plus a small number to break ties)
    int gen;            // 0x190, 0x192, 0x210, 0x211, 0x250 (2.50.x and 2.53.x), 0x255, 0x300
    int tag;            // the generation's own index for this build
    bool (*save_as)(const zq_build& b, const char* path);   // write the open quest as this build
    bool (*make_new)(const zq_build& b);                   // File > New in this build; nullptr if not offered
};
// Each generation adds its builds (zqbold: zq_builds_old.cpp; zqb55: zq_builds_255.cpp; zqb30: zq_builds_30.cpp).
void zq_builds_old(std::vector<zq_build>& out);
void zq_builds_255(std::vector<zq_build>& out);
void zq_builds_30(std::vector<zq_build>& out);
// Each generation says which of its builds made the quest open now: its order, or -1 if not its generation.
long zq_made_order_old();
long zq_made_order_255();
long zq_made_order_30();

// The ZC version a quest was built with, as text for window titles. The player and ZQuest
// both show it after the quest's name.

#ifndef ZC_QUEST_VERSION_H
#define ZC_QUEST_VERSION_H

#include <string>

// From the quest header's version and build ("" when the header doesn't say). 2.55 and 3.0
// Nightly quests use what their header reader found (q255_basic.cpp).
std::string zc_quest_built_with(int zelda_version, int build);

#endif

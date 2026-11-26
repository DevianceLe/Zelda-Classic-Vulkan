// zq_ui_profiles_250.cpp: ZQuest's interface in 2.50.0, 2.50.1, 2.50.2, 2.50.3 and 2.53.0 (zq_ui_profile.h).
//
// These builds' editors are today's (2.53.1) with small differences, so each profile says only
// what differs:
// - Menus (zq_ui_profiles_250_menus.h, from the builds' sources): 2.50.0 calls Import/Export
//   Enemies "Guys" and has no Backward compatibility rules; 2.50.0 to 2.50.3 have no "Save ZQuest
//   Configuraton" and "Clear Quest Filepath" in Etc, and call Video Settings "Video Mode"; 2.50.2
//   and 2.50.3 open the rules in one tabbed "Quest rules" window (Quest > Rules,
//   zq_ui_profiles_250_dialogs.cpp) instead of a submenu of seven. None has today's entries for
//   2.55/3.0 quests. 2.53.0's menus are 2.53.1's: its profile keeps today's.
// - Keys: the shortcuts in the menu texts are today's, the same F-keys for the same entries.
// - Layout: every one has 2.53.1's Large and Small layouts with the same panel places, and the
//   same Options switch between them: the user's choice, as in 2.53.1.
// - Look: the same colour sets in Options (2.53 added two hidden ones): the user's colours.
// - Dialogs: zquicore draws each build's own from zquiinv's data.
//
// Quests saved by the builds between releases get the interface of the release they led to (by
// order, before the catalog's names): 2.50.1's release candidates ("2.50.1 Gamma", builds 25 to
// 27) had 2.50.1's menus, 2.53's betas (build 31: 5th.qst) 2.53.0's, today's. Without these, the
// newest release before them would show: 2.50.0's, and 2.50.3's single Rules window for 2.53
// betas. (2.50 betas, builds up to 23, show 2.50.0's, the oldest of the generation.)
//
// Written for the port (no jwin or Allegro code): data, plus the one extra action.

#include "precompiled.h" //always first

#include <stddef.h>

#include "zq_ui_profile.h"

int zq_ui_rules_250();      // zq_ui_profiles_250_dialogs.cpp: 2.50.2's Quest rules window

namespace
{

#include "zq_ui_profiles_250_menus.h"

#define ROWS(t) t, (int)(sizeof(t) / sizeof(t[0]))

const zq_ui_profile p2500 = { "2.50.0", 0x250, ROWS(ZQ_UI_MENU_2_50_0), NULL, NULL, NULL, 0, 0 };
const zq_ui_profile p2501 = { "2.50.1", 0x250, ROWS(ZQ_UI_MENU_2_50_1), NULL, NULL, NULL, 0, 0 };
const zq_ui_profile p2502 = { "2.50.2", 0x250, ROWS(ZQ_UI_MENU_2_50_2), NULL, NULL, NULL, 0, 0 };
const zq_ui_profile p2503 = { "2.50.3", 0x250, ROWS(ZQ_UI_MENU_2_50_3), NULL, NULL, NULL, 0, 0 };
// 2.53.0: today's menus, layout and colours
const zq_ui_profile p2530 = { "2.53.0", 0x250, NULL, 0, NULL, NULL, NULL, 0, 0 };
// the builds between releases, by their order on the catalog's scale (zq_builds_old.cpp's
// dynamic_order: 20140901 + build for 2.50.1 Gamma, 20171015 for 2.53 betas)
const zq_ui_profile p2501rc = { "2.50.1 Gamma", 0x250, ROWS(ZQ_UI_MENU_2_50_1), NULL, NULL, NULL, 0, 20140901 + 25 };
const zq_ui_profile p253b = { "2.53 beta", 0x250, NULL, 0, NULL, NULL, NULL, 0, 20171015 };

#undef ROWS

void add()
{
    zq_ui_add_action("@rules250", zq_ui_rules_250, 0);
    zq_ui_add_profile(&p2500);
    zq_ui_add_profile(&p2501);
    zq_ui_add_profile(&p2502);
    zq_ui_add_profile(&p2503);
    zq_ui_add_profile(&p2530);
    zq_ui_add_profile(&p2501rc);
    zq_ui_add_profile(&p253b);
}

zq_ui_registrar reg(add);

} // namespace

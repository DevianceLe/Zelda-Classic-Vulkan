// 3.0 Nightly's door combo sets for a quest made before 1.92 beta 154.
//
// Such a quest has no door sets of its own: upstream's map reader makes one from each screen's
// map template (MakeDoors), reusing a set already made when it is the same, and sets each
// screen's door set number. 2.53 does the same, and ZQuest holds its result; 3.0's differs for
// quests before 1.92 beta 137, whose screen 132 (a copy of the template screen) 3.0 reads with
// combo page 0 instead of the template's: on maps whose template uses another page that makes
// one more set, and moves the numbers of every set made after it. The DOOR writer and the map
// writer both need these numbers.

#ifndef ZQN_DOORS_H
#define ZQN_DOORS_H

#include <vector>

#include "zdefs.h"

// The sets 3.0 makes, named as it names them ("Door Combo Set n"), and each screen's set number
// (map*MAPSCRS + screen). False when the open quest isn't one whose sets the map reader makes
// (1.92 beta 154 or newer), or the user changed ZQuest's door sets since the load (then ZQuest's
// own sets are what a 2.53.1 quest would carry).
bool zqn_made_door_sets(std::vector<DoorComboSet> &sets, std::vector<word> &screen_set);

#endif

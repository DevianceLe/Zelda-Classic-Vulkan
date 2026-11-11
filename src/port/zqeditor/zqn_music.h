// 3.0 Nightly's "advanced music" (the AMUS section) for a quest made in 2.53.1 or older.
//
// 3.0 keeps a quest's music as one list of music entries; DMaps and screens refer to an entry
// by its number (1-based, 0 for none). Opening an older quest, its loader makes the list as it
// reads: one entry per DMap that has a MIDI or an enhanced music file ("DMap n", in DMap
// order), then, for each screen with a MIDI of its own, the entry "MIDI m" (made once per
// MIDI, in map and screen order). The DMap and map writers need the same numbers this list
// gives, so all three get them from here.

#ifndef ZQN_MUSIC_H
#define ZQN_MUSIC_H

#include <string>
#include <vector>

#include "zdefs.h"

struct ZqnMusic
{
    std::string name;
    std::string path;       // enhanced music file, empty for none
    byte track;
    short midi;             // 3.0's numbering: >0 a quest MIDI, <0 a built-in one, 0 none
};

struct ZqnQuestMusic
{
    std::vector<ZqnMusic> music;        // AMUS's entries in order; entry n is number n+1
    std::vector<word> dmap_music;       // MAXDMAPS: each DMap's music number (dmap::music)
    std::vector<short> screen_music;    // map_count*MAPSCRS: each screen's mapscr::music
};

// The list as 3.0's loader makes it from the open quest (ZQuest's DMaps and screens, which
// hold the old fields as 2.53 read them). False for a quest it doesn't apply to (2.55 or newer).
bool zqn_quest_music(ZqnQuestMusic &out);

#endif

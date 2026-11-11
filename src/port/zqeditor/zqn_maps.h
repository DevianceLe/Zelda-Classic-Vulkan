// The maps' and DMaps' part in 3.0's advanced music (zqn_maps.cpp).
//
// 3.0 has no MIDI numbers in DMaps and screens: reading a quest older than that makes an
// advanced music entry for each DMap that has music ("DMap n", from its MIDI and enhanced music,
// in DMap order), then one for each MIDI the screens play ("MIDI n", in the order the screens are
// read), and the DMaps and screens keep the entries' numbers (from 1). The AMUS section writes
// that list; the DMAP and MAP writers use the same numbers.

#ifndef ZQN_MAPS_H
#define ZQN_MAPS_H

#include <string>
#include <vector>

#include "zq_writers.h"

struct ZqnMusicEntry
{
    std::string name;       // "DMap 3", "MIDI -5"
    int midi;               // 3.0's MIDI number: a tune from 1, or a built-in one below 0
    std::string path;       // the enhanced music file, or empty
    byte track;
};

// The entries 3.0 makes for the open quest, in order (entry k has number k + 1). False when the
// quest as opened can't be read again.
bool zqn_music_entries(const ZqNewerContext &ctx, std::vector<ZqnMusicEntry> &out);

// A screen's music number: -1 (the DMap's) or 0 (none) as they are, otherwise its "MIDI n"
// entry, made the first time a screen uses that MIDI (midis: the MIDIs given entries so far).
int zqn_screen_music_index(short screen_midi, int dmap_entries, std::vector<int> &midis);

// The door combo sets upstream makes for a quest before 1.92 build 154, which stores none:
// readmaps' MakeDoors gives each screen the set its map's template screen makes on the screen's
// combo page ("Door Combo Set n", in the order first made). The two targets differ: 3.0 doesn't
// copy a 1.90 map's combo page to its second template screen. Empty for other quests (their
// sets are the ones the file stores). False when the quest as opened can't be read again.
bool zqn_made_door_sets(const ZqNewerContext &ctx, std::vector<DoorComboSet> &out);

#endif

// Where ZQuest's data names a tile, for the checks that keep a save from losing tile numbers.
//
// ZQuest holds 825 pages of tiles (214,500), as many as a 2.55 or 3.0 Nightly quest can have.
// A 2.53 file, and every older one, holds 252 pages (65,520 tiles) and stores most tile numbers
// in 16 bits: ZQuest's 2.53 writers put them in with p_iputw, which would cut a bigger number
// short without a word. The tile editor keeps 2.53 and older quests within 252 pages
// (zq_tile_limit), and these checks make sure nothing past them reaches such a file anyway.

#ifndef ZQ_TILE_REFS_H
#define ZQ_TILE_REFS_H

#include <stdint.h>
#include <string>

#include "zdefs.h"

struct ZqTileRef
{
    dword section;      // the section that stores it (ID_COMBOS, ID_ITEMS, ...)
    int field;          // what kind of reference it is (zq_tile_refs.cpp)
    int index;          // which record, and for subscreens which object
    long tile;
    bool narrow;        // 2.53's writer stores it in 16 bits
};

typedef void (*ZqTileRefFn)(const ZqTileRef &r, void *user);

// Every tile number in ZQuest's 2.53 data that a 2.53 section stores, or only those of one
// section (0: all of them).
void zq_each_tile_ref(dword section, ZqTileRefFn fn, void *user);

// "combo 1234", "enemy 40's second tile": what uses it, for messages.
std::string zq_tile_ref_name(const ZqTileRef &r);

// After every load and every save in a quest's own format: remembers the tile numbers past
// 65,519 the file already holds (an old file can have junk there), so a save that only writes
// them back as they were isn't refused.
void zq_tile_refs_note_loaded();
// How many it remembered; names: the first few, "item 3 (tile 65,535)", for reports.
int zq_tile_refs_loaded_past(std::string *names = NULL);

// Before any save in a 2.53-or-older format. False when the quest has a tile such a file can't
// hold: one drawn on past tile 65,519, or a tile number past 65,519 that the file didn't already
// have. why names the first one, as a sentence without its full stop: "Combo 12 uses tile 70,000".
bool zq_tiles_fit_253(std::string &why);

// For the fingerprints (zq_sections.cpp), which run the 2.53 writers: mixes into h every tile
// number past 65,535 that those writers would cut to 16 bits, for one section (0: all of them),
// so a change in the high bits still shows as a change.
void zq_hash_wide_tile_refs(dword section, uint64_t *h);

#endif

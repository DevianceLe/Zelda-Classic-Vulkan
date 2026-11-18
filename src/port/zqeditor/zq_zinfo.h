// The open quest's editor names (ZINF, zqn_255_zinfo.cpp): what its author called item classes,
// combo types, map flags, counters, weapons, enemy classes and level items, and their help.
// A 2.55 or 3.0 quest keeps them in the quest file, or, when its header says so (external_zinfo),
// in a '<quest>.zinfo' file beside it; upstream's loaders read that file if it is there and give
// the quest no names if it isn't. ZQuest shows the names in its lists in place of upstream's
// own ones, and its saves keep them where the quest had them.

#ifndef ZQ_ZINFO_H
#define ZQ_ZINFO_H

#include <string>
#include <vector>

#include "zdefs.h"

enum ZqNameKind
{
    ZQN_ITEMCLASS, ZQN_ITEMCLASS_HELP, ZQN_COMBOTYPE, ZQN_COMBOTYPE_HELP, ZQN_MAPFLAG, ZQN_MAPFLAG_HELP,
    ZQN_COUNTER, ZQN_WEAPON, ZQN_ENEMYCLASS, ZQN_LEVELITEM, ZQN_LEVELITEM_ABBR, ZQN_LEVELITEM_HELP
};

// The open quest's own name (or help) of that kind, or NULL where it has none; upstream's zinfo
// getters show their own name then.
const char *zq_quest_name(int kind, int index);

// load_quest calls this after each load: reads the quest's names from its ZINF section, or from
// the side file when its header points to one. NULL: no quest was loaded.
void zq_zinfo_loaded(const char *filename);

// The open quest (made in 2.55 or later) keeps its names in a side file: its header's
// external_zinfo byte.
bool zq_zinfo_external();

// The side file as it was when the quest was opened: false when there was none (or the quest
// keeps its names in the quest file). version: its ZINF section version.
bool zq_zinfo_side(std::vector<byte> *bytes, int *version);

// The side file's name for a quest file: the quest's name with its extension replaced by
// "zinfo", as upstream's replace_extension makes it ("a\b.qst" -> "a\b.zinfo").
std::string zq_zinfo_path(const char *quest_file);

// The ZInfo editor's store (zqw_zinfo.cpp), when it holds the open quest's names (read from its
// ZINF section, and changed in the editor): zq_quest_name answers from it (true; *out may be
// NULL), and false leaves the answer to the names read at load (e.g. a side .zinfo file).
bool zq_zinfo_store_answers(int kind, int index, const char **out);
// The store's ZINF data (after the section header) and version, for Save As 3.0: false when it
// holds no names.
bool zq_zinfo_store_bytes(std::vector<byte> &data, int &version);

#endif

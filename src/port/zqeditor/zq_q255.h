// What ZQuest's own files need to know about 2.55 and 3.0 Nightly quests. The data
// itself is in zq_q255_store.h.

#ifndef ZQ_Q255_H
#define ZQ_Q255_H

// Every quest load and File > New: forgets the last 2.55 or 3.0 Nightly quest.
void zq255_store_clear();

// True while the open quest is a 2.55 or 3.0 Nightly quest (its header keeps the version
// it was read with). Saving and compiling scripts are off for it until its writers exist.
bool zq255_quest_is_newer();

// zq_files.cpp: the message for Save, Save As and the whole-quest exports. Returns D_O_K.
int zq255_saving_not_ready();
// zq_files.cpp: the message for the partial exports that carry tile numbers. Returns D_O_K.
int zq255_export_not_ready();

// How many tiles the open quest can use: 2.53 and older quests keep tile numbers in 16 bits,
// so 252 pages (65520 tiles); newer quests get all of NEWMAXTILES. The tile editor and
// pickers stop here, though ZQuest's buffers always hold NEWMAXTILES.
int zq_tile_limit();

// ZQuest's combo operations on combobuf (zq_tiles.cpp): a 2.55/3.0 combo's newer part (the
// Q30Combo table) goes along, so a copied or moved combo stays whole. Nothing for older quests.
void zq255_combo_copy(int dest, int src);
void zq255_combo_swap(int a, int b);
void zq255_combo_clear(int i);
void zq255_combos_undo_note();      // with go_combos()
void zq255_combos_undo_restore();   // with comeback_combos()

// The editor names (ZInfo, the ZINF section) of the open 2.55/3.0 quest: its own name, help or
// abbreviation for an entry of one of upstream's lists, or NULL where it keeps upstream's
// default (and for every older quest). zqw_zinfo.cpp.
enum ZqZinfoKind
{
    ZQZ_ITEMCLASS, ZQZ_COMBOTYPE, ZQZ_MAPFLAG, ZQZ_COUNTER, ZQZ_WEAPON, ZQZ_ENEMYCLASS, ZQZ_LEVELITEM, ZQZ_KINDS
};
const char *zq_zinfo_name(int kind, int index);
const char *zq_zinfo_help(int kind, int index);
const char *zq_zinfo_abbr(int index);      // level items only
bool zq_zinfo_old_mapflag(int f);           // a 2.55 name for map flag f in a ZINF v0 quest (zq_zinfo_name gives it)
void zq_zinfo_defaults_only(bool on);       // on: the above give NULL (upstream's names show)

#endif

// Exports, imports and timed saves of 2.55 and 3.0 Nightly quests (zq_exports.cpp).
//
// For these quests ZQuest's File > Export and Import menus write and read the files upstream's
// editor writes for that version: 2.55.17's for a quest made in 2.55, 3.0's for one made in 3.0.
// What an export holds comes from what the quest's own-version save would write now, so it is in
// the quest's own section versions (upstream's matching import reads every one of those), and an
// import goes into ZQuest's data and its store for these quests, so the next save writes it in
// the quest's own version. The dialogs are 2.53's, in zq_files.cpp and zquest.cpp.

#ifndef ZQ_EXPORTS_H
#define ZQ_EXPORTS_H

#include <string>

// True for a quest made in 3.0 (its header's major version), false for one made in 2.55.
bool zqx_is_30();

// ---- exports: true when the file was written; otherwise why says what stopped it ----------

// DMaps first..last (.zdmap: upstream's writesomedmaps).
bool zqx_export_dmaps(const char *path, int first, int last, std::string &why);
// The string table (.zqs: the strings section, upstream's save_msgstrs).
bool zqx_export_strings(const char *path, std::string &why);
// Every combo (.zcombo: upstream's writecombofile, as File > Export > Combo Set writes it).
bool zqx_export_combos(const char *path, std::string &why);
// Tiles (.ztileset: upstream's writetilefile). A 3.0 quest: pages first_page.. (pages of them);
// a 2.55 quest: every tile, as 2.55.17 writes them (the pages are ignored).
bool zqx_export_tiles(const char *path, int first_page, int pages, std::string &why);
// The enemies (.guy: the enemy section, upstream's save_guys).
bool zqx_export_guys(const char *path, std::string &why);
// The graphics pack (.zgp: upstream's save_zgp: tiles, combos, palettes, items, weapon
// sprites, game icons, misc colours, door sets).
bool zqx_export_zgp(const char *path, std::string &why);
// The whole quest in its own version, unencoded: packed (.zqt) or not (.qsu), as upstream
// stores a .qst and a .qsu.
bool zqx_export_quest(const char *path, bool packed, std::string &why);
// Map `map`'s 136 screens (.map: upstream's zmap::save: the map section's version, a byte once
// the build, then each screen's record as the map section holds it).
bool zqx_export_map(const char *path, int map, std::string &why);

// ---- imports: true when the file went in; otherwise why says what stopped it (and nothing in
// the quest changed). note: anything the user should know after it went in ("" for nothing).
// Each takes a file in the quest's own section version, as upstream's import reads it into a
// quest of that version, and what goes in is then the file's data exactly, so the quest's
// own-version save writes it as the file has it (a whole string table or palette set, or the
// items of a graphics pack, become the quest's part; DMaps and enemies are kept as edited
// records). Files in an older format are converted as upstream's loader converts them (its
// imports read with the loader's readers):
// - combos of any older format (and newer ones the quest's format can hold), tiles of any;
// - strings, enemies and a graphics pack's items when the quest's section is in its generation's
//   newest format (2.55.17's or 3.0's), with what saving a quest as that version converts them;
// - DMaps record by record, except across the formats that changed how a DMap holds its music,
//   disabled items or scripts (25, 26, 27).
// A file in a newer format than the quest's (combos aside), or one of the conversions above that
// isn't done, is refused with the reason, and nothing in the quest changes.

// DMaps (.zdmap), each to its own number. max_map: the highest map they use.
bool zqx_import_dmaps(const char *path, int *max_map, std::string &why, std::string &note);
// After a DMap import whose DMaps use maps the quest doesn't have, if the user doesn't add maps:
// every DMap on a map past maps goes to map 0 (its record too), as upstream's import does.
bool zqx_dmaps_fit_maps(int maps, std::string &why);
// The string table (.zqs): replaces every string, as upstream's import does.
bool zqx_import_strings(const char *path, std::string &why, std::string &note);
// Combos (.zcombo) from combo dest on.
bool zqx_import_combos(const char *path, int dest, std::string &why, std::string &note);
// Tiles (.ztileset) from tile dest on.
bool zqx_import_tiles(const char *path, int dest, std::string &why, std::string &note);
// The enemies (.guy): replaces every enemy, as upstream's import does (which reads the file as
// a 2.50 quest's, so an enemy with no hit sound gets sound 11: here too).
bool zqx_import_guys(const char *path, std::string &why, std::string &note);
// A graphics pack (.zgp): tiles, combos, palettes, items, weapon sprites, game icons, misc
// colours and door sets, as upstream's import does.
bool zqx_import_zgp(const char *path, std::string &why, std::string &note);
// A map (.map) over map `map`, as upstream's zmap::load: bias 0 takes every screen ("Total
// Copy"), 1 only where the map's screen is unused ("Prefer Original"), 2 only the file's used
// screens ("Prefer Import"). A file of an older map format (2.53's .map files too) is converted
// up to the quest's format; one of a newer format is refused.
bool zqx_import_map(const char *path, int map, int bias, std::string &why, std::string &note);

// ---- one item or enemy (.zitem, .znpc: upstream's writeoneitem / readoneitem and writeonenpc /
// readonenpc, the item and enemy listers' Save and Load) -------------------------------------
// The file is upstream's: the program's version and build, the section's version and cversion,
// the name (64 bytes: an enemy's always, an item's while its section keeps names apart, before
// format 67) and the record as the section holds it, here in the quest's own section format.
// 2.55.17 reads a 2.55 quest's files (it stops before the record's last fields, which its own
// files leave out), 3.0 an item's of any format and an enemy's from format 57 on.
bool zqx_export_one(bool item, int index, const char *path, std::string &why);
// Replaces item or enemy 'index' with the file's. The file must be in the quest's own section
// format (a record in another format isn't converted). A 2.55.17 file's missing last fields
// read as 0, as 2.55.17 leaves them.
bool zqx_import_one(bool item, int index, const char *path, std::string &why, std::string &note);

// A quest template (.zqt, packed) or unencoded quest (.qsu) can replace a 2.55 / 3.0 quest
// (2.53's Import Quest Template and Import Unencoded Quest) only when it was made in the quest's
// version or a newer one, as the quest is then saved in its version: false (why says) when it
// is older or isn't a quest. made: the version it was made in.
bool zqx_quest_not_older(const char *path, bool packed, std::string &made, std::string &why);

// ---- timed saves ----------------------------------------------------------------------------

// Whether a timed save has something to keep: the quest has unsaved edits (ZQuest's "saved"
// flag is clear) and its data changed since it was last saved, in its own version or by a timed
// save (upstream's "autosaved"); an import that put a whole section in counts. A timed save,
// like a save, is the starting point the next one is compared with.
bool zqx_timed_save_due();


// The quest in its own version under the name upstream's timed save uses, beside the quest's
// file: <folder>/backups.<file name>/<YYYY_MM_DD HHMMSS>--autosave--v<version>--<file name>
// (<file>.backups/... off Windows), as .qsu (not packed) when uncompressed, then the oldest timed
// save past retention (a count; 0: no limit) removed, as upstream does. 0: saved (path says
// where); otherwise why says.
int zqx_timed_save(const char *quest_path, int retention, bool uncompressed, std::string &path, std::string &why);

#endif

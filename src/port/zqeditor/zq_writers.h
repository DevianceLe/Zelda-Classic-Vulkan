// Saving a quest in the version it was made in.
//
// A quest is saved from the parts kept when it was opened (zq_sections.h): a part nobody
// changed is written back exactly as it came, and a changed part goes through the writer for
// its section and section version. A writer writes the whole part as it stands in the file:
// the four-byte section id (none for the header, a pre-1.93 body or the tail), then exactly
// what that version's reader reads (version, cversion, any extra fields, size, data).
//
// Writers register here, one or more per section id, and say ZQW_UNSUPPORTED for versions
// they don't handle yet; the next one registered for that id is tried then. ZQuest's own 2.53
// writers are registered for the parts that are at 2.53's own section version.

#ifndef ZQ_WRITERS_H
#define ZQ_WRITERS_H

#include <string>
#include <vector>

#include "zq_sections.h"

struct ZqPartContext
{
    const ZqQuestPart *part;    // the part as it was loaded
    word zelda_version;         // the quest file's header
    byte build;
    word version, cversion;     // the part's section version (0 for raw parts)
};

enum { ZQW_OK = 0, ZQW_UNSUPPORTED = -1000 };

// A writer that refuses a part (returns anything but ZQW_OK or ZQW_UNSUPPORTED) says why here,
// in a short sentence the user can read: "a combo uses a type 1.90 doesn't have". The save
// clears it before each writer runs and shows it when the part can't be written.
extern std::string zqw_last_error;

typedef int (*ZqPartWriter)(PACKFILE *f, const ZqPartContext &ctx);

// For writer files: struct ZqWriterRegistration reg_combos(ID_COMBOS, zqw_combos, "combos v1-45");
// priority: higher is asked first. Version-specific writers use the default; ZQuest's own
// 2.53 writers are a low-priority fallback (zq_writers.cpp).
struct ZqWriterRegistration
{
    ZqWriterRegistration(dword id, ZqPartWriter writer, const char *name, int priority = 100);
};

// A section whose data an old quest's loader derives partly from other sections (old items and
// enemies take values from the rules) asks to be written on every own-format save, not only
// when its own fingerprint changed: its writer then sees an edit elsewhere that would change
// it on reload, and refuses rather than let it change silently. ZqEverySave items(ID_ITEMS);
struct ZqEverySave
{
    ZqEverySave(dword id);
};

// A writer area that keeps its own copy of records as loaded (to copy the records nobody
// changed straight from the original bytes) registers a function run after every load and
// after every save in the quest's own format: ZqLoadHookRegistration hook_items(snapshot_items);
struct ZqLoadHookRegistration
{
    ZqLoadHookRegistration(void (*fn)());
};
void zq_writers_note_loaded();      // zq_sections.cpp calls this; it runs those functions

// Newer data that ZQuest edits but 2.53's writers don't hold (the 2.55/3.0 store): each
// section's area adds it to that section's fingerprint, so an edit to it counts as a change
// and the section goes through its writer. ZqStoreHashRegistration combos(ID_COMBOS, hash_fn);
struct ZqStoreHashRegistration
{
    ZqStoreHashRegistration(dword id, void (*fn)(uint64_t *h));
};
void zq_hash_store(dword id, uint64_t *h);     // zq_sections.cpp calls this for each fingerprint
void zq_hash_add(uint64_t *h, const void *data, size_t n);   // FNV-1a, as the fingerprints use

// When true, writers encode every record from ZQuest's data, as if all of it had changed,
// instead of copying unchanged records: -qst-roundtrip-check uses it to test that code.
extern bool zqw_force_serialize;

// Saving as another build's file the way that build's own ZQuest did when it opened an older quest
// (zq_builds_old.cpp): when true, what the target's format can't hold as ZQuest has it is written as
// that build had it, from the file the save starts from (the build's new-quest file), and its loader
// makes the rest of it on load, instead of the save stopping. Only that save sets it.
extern bool zqw_lenient;

// The part's section version and cversion, from its kept bytes. False for raw parts.
bool zq_part_versions(const ZqQuestPart &p, word *version, word *cversion);

// Writes the part with the first registered writer that takes it. ZQW_UNSUPPORTED if none does.
int zq_write_part(PACKFILE *f, const ZqQuestPart &p);

// The 2.53-and-older family's storage of the open quest's file (zq_quest_file).
// Writes the quest, in the version it was made in, to filename: unencoded if 'encode' is
// false (an export), otherwise stored as the original file was. why says what stopped it.
// Returns 0 when it saved.
int zq_save_own_format(const char *filename, bool encode, std::string &why);

// File > New for a version that writes every section as the open quest's file has it (2.53.0 for
// ZQuest's 2.53.1 new quest): the header and the kept copy say that version made the quest, so it
// saves as that version from now on. False (nothing changed) when there's no header to mark.
bool zq_new_quest_stamp(word zelda_version, byte build);

// Which changed parts have no writer yet (empty: the quest can be saved in its own format).
void zq_own_format_blockers(std::vector<std::string> &names);

// A short name for a part, for messages: "combos (CMBO v7)".
std::string zq_part_name(const ZqQuestPart &p);

// ---- saving as a newer version than the quest was made in --------------------------------
//
// A quest can be saved in its own version or a newer one, never an older one. Newer than 2.53.1
// means a 2.55 or 3.0 Nightly file: every section in that version's newest format, with the
// conversions upstream's loader makes when it opens an older quest already applied, so the
// quest plays there as it did in its own version. Upstream's own ZQuest, opening the quest and
// saving it, is the reference these writers are checked against.

enum ZqNewerTarget
{
    ZQ_NEWER_255 = 1,       // ZC 2.55.19, the last 2.55 release (its section formats are 2.55.18's)
    ZQ_NEWER_30 = 2         // ZC 3.0 Nightly (upstream main at ef321f8)
};

const char *zq_newer_target_name(int target);      // "2.55.19", "3.0 Nightly"

// The 2.55 release ZQ_NEWER_255 writes: 0 to 19 (2.55.0 to 2.55.19), 19 unless a Save As or New in
// an older release set it for its save (zq_builds_255.cpp). A release writes its own patch number in
// the header and its section versions (maps 29 before 2.55.2, enemies 47 before 2.55.4, subscreens 11
// before 2.55.8; every other section is the same in all of them), and its loader makes only the load
// fixes it has (the rules "older than 2.55.<k>" each release added, the cuttables' trigger fix from
// 2.55.14, the Dig enemies' "ignore Kill All Enemies" from 2.55.4).
int zq_newer_255_release();
void zq_newer_set_255_release(int patch);

// Whether the target's loader has readrules' "older than 2.55.<k>" fix (always for 3.0; for 2.55,
// from the release that added it: 2.55.6's came in 2.55.10, the others in their own release).
bool zq_newer_255_has_fix(int target, int k);

// What a newer-version writer knows about the quest it converts. Upstream's loader converts an
// old quest's data as it reads it, by the quest's header version and by each section's own
// version; a writer that does the same conversion needs both.
struct ZqNewerContext
{
    int target;                         // ZQ_NEWER_255 or ZQ_NEWER_30
    word src_zelda_version;             // the opened quest's header (zq_quest_file)
    byte src_build;
    const ZqQuestPart *src_part;        // this section's part in the opened file, NULL if none
    word src_version, src_cversion;     // that part's section version and cversion (0 if none)
};

// Writes one whole section (its id first; the header writes what upstream's writeheader writes)
// in the target's newest format from ZQuest's data. ZQW_UNSUPPORTED when that section isn't
// done yet for the target; another non-zero value is a refusal, with the reason in
// zqw_last_error.
typedef int (*ZqNewerWriter)(PACKFILE *f, const ZqNewerContext &ctx);

// The opened quest's first part with this id, or NULL.
const ZqQuestPart *zq_find_part(dword id);

struct ZqNewerRegistration
{
    ZqNewerRegistration(dword id, ZqNewerWriter writer, const char *name);
};

// Saves the open quest as the target version. 0: saved. 2: some sections can't be written in
// that version yet (why lists them). 8: a quest made in 2.55 or later holds parts newer than the
// target holds (why lists them; it is never written older). Anything else: failed (why says what).
int zq_save_newer(const char *filename, int target, std::string &why);

// One section of the open quest as saving it as the target would write it (its id first), from
// what the quest's own-version save writes now. False (why says) when no writer takes it.
bool zq_newer_section_bytes(dword id, int target, std::vector<byte> &out, std::string &why);

// A section from a file (as it follows its id: version, cversion, size, data), converted the way
// saving a quest made in 2.55 as the target converts that section of the quest: the file's section
// stands in for the quest's own while its converter runs, so it is read as the target's loader
// reads a section of that version (with the open quest's rules, items and so on where the loader
// looks at them) and comes out in the target's newest format, its id first. For imports of files
// older than the quest's section. False (why says) when no converter takes that version.
bool zq_newer_convert_section(dword id, int target, const std::vector<byte> &section, std::vector<byte> &out, std::string &why);

// True while zq_newer_convert_section runs: a converter then leaves out what upstream's loader
// does to a whole quest after its sections are read, which its imports don't do.
bool zq_newer_converting_file();

// The same, stopping at an older format than the target's newest: for an import into a quest
// whose own section is in that format (to_version, with its cversion). The section comes out as
// upstream's loader would read the file's, less what that loader still does to a section of
// to_version when it reads it, so loading the result gives what loading the file's section
// gives. False (why says) when the section's converter can't stop there.
bool zq_newer_convert_section_to(dword id, int target, const std::vector<byte> &section, word to_version, word to_cversion,
                                 std::vector<byte> &out, std::string &why);

// For the converters: the format zq_newer_convert_section_to asked for (0: the target's newest,
// which is every other time), and its cversion.
int zq_newer_convert_to(word *cversion);

// An enemy or item section out of a file (version, cversion, size, data) in one of 2.53's formats
// or an older one, for an import into a quest made in 2.55 or 3.0: converted as saving a quest of
// 2.53.1 as the target converts that section (upstream's reader of that version), into the
// target's newest format, its id first. Enemies: formats 4 to 24; items: 2 to 25 (zqn_guys.cpp,
// zqn_items.cpp). False (why says) otherwise.
bool zqn_guys_from_old_file(const std::vector<byte> &section, int target, std::vector<byte> &out, std::string &why);
bool zqn_items_from_old_file(const std::vector<byte> &section, int target, std::vector<byte> &out, std::string &why);

// Whether saving the open quest (made in 2.55 or later) as the target would bring some of its
// parts up to newer versions: some part is older than the target's newest and none is newer.
// A quest made in a 3.0 prerelease is then still offered 3.0 Nightly (it is a newer version).
bool zq_newer_upgrades(int target);

// A write-only file that appends what is written to it to *out.
PACKFILE *zq_open_vector_sink(std::vector<byte> *out);

// The part as a save in the quest's own version would write it now, after its id: its writer's
// bytes when it changed (or is written on every save), its kept bytes otherwise. False when that
// save couldn't write it (zqw_last_error may say why).
bool zq_own_part_bytes(const ZqQuestPart &p, std::vector<byte> &out);

// all_parts: every part that has a writer goes through it, changed or not (for the checks).
int zq_save_own_format_ex(const char *filename, bool encode, bool all_parts, std::string &why);

// zq_save_own_format, stored as upstream stores a .qst (pack 1: packed) and a .qsu (pack 0: not
// packed), with no encoding and no password, whatever the quest's file was: the exports of
// 2.55 / 3.0 quests and their timed saves.
int zq_save_own_format_packed(const char *filename, bool pack, std::string &why);

// True when ZQuest's data differs from what the last load or own-format save left (some part
// changed, or data a section the file lacks would hold): what a timed save is for.
bool zq_changed_since_save();

// -qst-rewrite-check in.qst report.txt: every part goes through its writer, as if changed,
// and is compared with the original bytes. Returns 0 when every written part is identical.
int zq_rewrite_check(const char *filename, const char *report);

// -qst-roundtrip-check in.qst report.txt: saves the quest in its own format with every record
// encoded by the writers (zqw_force_serialize), loads that file, and compares each part's
// data (its fingerprint through the 2.53 writers) with the original load. Returns 0 when all
// parts come back the same.
int zq_roundtrip_check(const char *filename, const char *report);

#endif

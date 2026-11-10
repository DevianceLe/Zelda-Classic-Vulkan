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

// When true, writers encode every record from ZQuest's data, as if all of it had changed,
// instead of copying unchanged records: -qst-roundtrip-check uses it to test that code.
extern bool zqw_force_serialize;

// The part's section version and cversion, from its kept bytes. False for raw parts.
bool zq_part_versions(const ZqQuestPart &p, word *version, word *cversion);

// Writes the part with the first registered writer that takes it. ZQW_UNSUPPORTED if none does.
int zq_write_part(PACKFILE *f, const ZqQuestPart &p);

// The 2.53-and-older family's storage of the open quest's file (zq_quest_file).
// Writes the quest, in the version it was made in, to filename: unencoded if 'encode' is
// false (an export), otherwise stored as the original file was. why says what stopped it.
// Returns 0 when it saved.
int zq_save_own_format(const char *filename, bool encode, std::string &why);

// Which changed parts have no writer yet (empty: the quest can be saved in its own format).
void zq_own_format_blockers(std::vector<std::string> &names);

// A short name for a part, for messages: "combos (CMBO v7)".
std::string zq_part_name(const ZqQuestPart &p);

// all_parts: every part that has a writer goes through it, changed or not (for the checks).
int zq_save_own_format_ex(const char *filename, bool encode, bool all_parts, std::string &why);

// -qst-rewrite-check in.qst report.txt: every part goes through its writer, as if changed,
// and is compared with the original bytes. Returns 0 when every written part is identical.
int zq_rewrite_check(const char *filename, const char *report);

// -qst-roundtrip-check in.qst report.txt: saves the quest in its own format with every record
// encoded by the writers (zqw_force_serialize), loads that file, and compares each part's
// data (its fingerprint through the 2.53 writers) with the original load. Returns 0 when all
// parts come back the same.
int zq_roundtrip_check(const char *filename, const char *report);

#endif

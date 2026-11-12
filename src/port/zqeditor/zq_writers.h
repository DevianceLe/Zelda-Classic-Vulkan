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

// ---- saving as a newer version than the quest was made in --------------------------------
//
// A quest can be saved in its own version or a newer one, never an older one. Newer than 2.53.1
// means a 2.55 or 3.0 Nightly file: every section in that version's newest format, with the
// conversions upstream's loader makes when it opens an older quest already applied, so the
// quest plays there as it did in its own version. Upstream's own ZQuest, opening the quest and
// saving it, is the reference these writers are checked against.

enum ZqNewerTarget
{
    ZQ_NEWER_255 = 1,       // ZC 2.55.17, the last 2.55 release
    ZQ_NEWER_30 = 2         // ZC 3.0 Nightly (upstream main at ef321f8)
};

const char *zq_newer_target_name(int target);      // "2.55.17", "3.0 Nightly"

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
// that version yet (why lists them). Anything else: failed (why says what).
int zq_save_newer(const char *filename, int target, std::string &why);

// A write-only file that appends what is written to it to *out.
PACKFILE *zq_open_vector_sink(std::vector<byte> *out);

// The part as a save in the quest's own version would write it now, after its id: its writer's
// bytes when it changed (or is written on every save), its kept bytes otherwise. False when that
// save couldn't write it (zqw_last_error may say why).
bool zq_own_part_bytes(const ZqQuestPart &p, std::vector<byte> &out);

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

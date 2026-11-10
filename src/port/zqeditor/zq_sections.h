// ZQuest's copy of the open quest's file, part by part: the header, then each section as
// it was read (or, for a quest older than 1.93, the whole body). Saving a quest back in the
// version it was made in starts from these bytes: a part nobody changed is written back
// exactly as it came.

#ifndef ZQ_SECTIONS_H
#define ZQ_SECTIONS_H

#include <string>
#include <vector>

#include "zdefs.h"

struct ZqQuestPart
{
    dword id;                   // ID_HEADER, a section id, 0 for a pre-1.93 body, or the tail
    long long offset, length;   // its bytes in the store: everything read after the id
    bool has_fingerprint;       // false: ZQuest has no 2.53 writer for it, so it can't change it
    unsigned long long fingerprint;     // ZQuest's data for it through its 2.53 writers, at load
    bool fingerprint_failed;    // a writer failed at load: the part always counts as changed

    ZqQuestPart() : id(0), offset(0), length(0), has_fingerprint(false), fingerprint(0), fingerprint_failed(false) {}
};

struct ZqQuestFileCopy
{
    std::vector<ZqQuestPart> parts;     // in file order
    bool inexact;                       // the loader searched past damaged data, so the copy has gaps
    int enc_method;                     // the encoding that opened the file, -1 for none
    bool compressed;
    word zelda_version;                 // the header the file was read with
    byte build;

    ZqQuestFileCopy() : inexact(false), enc_method(-1), compressed(true), zelda_version(0), build(0) {}
};

extern ZqQuestFileCopy zq_quest_file;

// The sections 2.53 has that the open quest's file doesn't (a 2.10 file has no subscreens,
// scripts, Link sprites or drop sets): their names, when ZQuest's data for them changed since
// the load, so a save in the quest's own version stops instead of dropping the change. Editor
// favorites are left out; they were never part of the old formats.
void zq_absent_changed(std::vector<std::string> &names);

// Around ZQuest's own load_quest only, so importing from another quest leaves the copy alone.
void zq_sections_begin_load();
void zq_sections_end_load(bool loaded);

// n bytes of a part's original data from 'at' (0 = its start). The bytes are kept in a
// temporary file, not in memory. False when that range isn't there.
bool zq_part_read(const ZqQuestPart &p, long long at, void *buf, size_t n);

// After a successful load: notes what each part's data looks like through ZQuest's 2.53
// writers, so a save in the quest's own format can tell which parts were changed since.
void zq_sections_note_loaded();

// True when ZQuest's data for this part is no longer what was loaded.
bool zq_part_changed(const ZqQuestPart &p);

// Writes one kind of section (its id first) with ZQuest's 2.53 writer, as a 2.53 save
// would. False when there is no 2.53 writer for that id.
bool zq_write_253_section(PACKFILE *f, dword id);

// Replaces a part's kept bytes with what writer writes for it now (the writer writes the
// id too; only what follows it is kept). rc is the writer's result; false keeps the old bytes.
bool zq_part_replace(ZqQuestPart &p, int (*writer)(PACKFILE *f, const ZqQuestPart &p), int *rc);

// After saving the quest in a newer format, the kept copy no longer describes the file.
void zq_sections_forget();

// A write-only file made of the given functions, marked as open for writing: ZQuest's write
// helpers (p_putc and the rest in zdefs.h) refuse a file without that mark and write nothing.
PACKFILE *zq_open_sink(const PACKFILE_VTABLE *vtable, void *user);

// Parts with no section id in front of their bytes.
bool zq_part_is_raw(dword id);
const dword ZQ_PART_DAMAGED = 0xFFFFFFFF;
const dword ZQ_PART_TAIL = 0xFFFFFFFE;

// -qst-capture-check in.qst report.txt: after loading the quest, compares the joined parts
// with the whole decoded file. Returns 0 when they're identical.
int zq_sections_check(const char *filename, const char *report);

#endif

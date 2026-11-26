// zq_builds_old.cpp: the builds from 1.90 to 2.53.1 for Save As and File > New (zq_builds.h).
//
// Save As writes the open quest as one of these builds, never one older than the build that made
// it (the caller lists only those; save_as refuses an older one too):
//   - its own build: the quest's own-version save (zq_writers.h), every part nobody changed as it
//     came. 2.53.1 is ZQuest's own 2.53 save, as it always was.
//   - 2.50.0 to 2.53.0 (header 0x250, builds 24 to 32) from any older quest: ZQuest's 2.53 save,
//     whose sections are at the versions every one of those builds writes (zq_builds_old_table.h:
//     2.50.0 to 2.53.1 share every section version), with the header's build set to the target's.
//     The quest's data goes in as 2.53.1 has it after the load's conversions, so it plays as it did;
//     the target's own loader conversions apply on top when the file is opened.
//   - 2.10.1 to 2.10.3 from 2.10.0 (the same section versions): the own-version save with the
//     header's build set to 1.
//   Anything else (a 1.9x quest as 2.10 or 2.11, a 2.10 quest as 2.11) isn't written yet: the
//   writers re-encode a part only in the version it was loaded in, and nothing gives them a part
//   at the target's versions to start from. save_as says so and saves nothing.
//   After a save the quest open is the saved file (reopened when its build changed): its path,
//   title and saved state are set. The caller shows the "Saved" message; save_as shows why when it
//   fails (only the message: nothing is changed).
//
// File > New makes the quest that build's own ZQuest made (the caller has asked about unsaved
// changes first):
//   2.53.1   ZQuest's new quest and the ruleset dialog, as always
//   2.53.0   the same, its header saying 2.53.0 (build 32) made it: 2.53.0 wrote every section in
//            the formats 2.53.1 writes
//   template the new-quest file the build's ZQuest loaded (its qst.dat or zgp.dat quest, shipped
//            as templates\<file>), with the header build that ZQuest wrote when it saved it:
//            2.10.0     zgp.dat's DAT_NESZGP (2.10.0's init_quest), saved as build 0
//            2.10.1-3   qst.dat's DAT_NESQST (build 1), as it is (templates\2.10.qst)
//            2.11 Beta 6  qst.dat's DAT_NESQST, the same file in every 2.11 beta's tree; it is a
//                       beta 6 file and is used as it is
//            2.50.0     its qst.dat quest (a build 23 file), saved as build 24
//            2.50.1     its qst.dat quest (build 27), saved as build 28
//            2.50.2     the build 29 qst.dat (zc-history's 2.50.3-RC1 tree holds it; its 2.50.2
//                       branch holds 2.50.1's), as it is
//            2.50.3     the same file, saved as build 30
//   A build whose template file is missing isn't offered. 1.90, 1.92 and 2.11 Beta 18 have no New
//   yet: those ZQuests built new quests in code, or their saved sections are at versions nothing
//   here writes.
//
// The open quest's build: its header (zelda_version, build). 2.10.1, 2.10.2 and 2.10.3 all write
// build 1, so a build 1 quest counts as 2.10.1's (the three write the same file). A header no
// build here names (a 1.92 beta, a 2.11 beta, a 2.50 or 2.53 beta) gets an entry of its own,
// listed only while that quest is open, that saves it in its own version.

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "gui.h"
#include "jwin.h"
#include "zquest.h"
#include "zq_page.h"
#include "zq_class.h"
#include "zq_files.h"
#include "zq_misc.h"
#include "tiles.h"
#include "zq_builds.h"
#include "zq_writers.h"
#include "zq_sections.h"
#include "zq_tile_refs.h"
#include "zq_zinfo.h"
#include "zq_builds_old_table.h"

extern zquestheader header;
extern zinitdata zinit;
extern bool zq_batch_load;
extern bool skip_quest_password;
extern std::vector<mapscr> TheMaps;

namespace
{

enum { N_NONE, N_253_1, N_253_0, N_TEMPLATE };

struct OldBuild
{
    const char *name;
    long order;         // the release date (zq_builds.h); approximate where noted
    int gen;
    word zv;            // the header this build writes
    byte build;
    int new_kind;
    const char *tpl;    // N_TEMPLATE: the new-quest file, beside the program
    const char *table;  // its row in zq_builds_old_table.h, NULL for none
};

// Dates: 1.90 and 1.92 beta 113 from changelog_historical.txt (1.92's betas get one day each from
// May 2001 in the dynamic entries, b114 among them); 2.10.x, the 2.11 betas and 2.50.2 are
// approximate (their releases left no date in the sources), placed after the builds whose formats
// they read and before the ones that read theirs. 2.10.3 came out much later (2019) but writes
// 2.10's file, so it sorts with 2.10.
const OldBuild g_builds[] =
{
    { "1.90",            20010403,       0x190, 0x190, 0,   N_NONE,     NULL,                   NULL },
    { "1.92 (Beta 114)", 20010501 + 114, 0x192, 0x192, 114, N_NONE,     NULL,                   NULL },
    { "2.10.0",          20061201,       0x210, 0x210, 0,   N_TEMPLATE, "templates/2.10.0.qst", "2.10.0" },
    { "2.10.1",          20070101,       0x210, 0x210, 1,   N_TEMPLATE, "templates/2.10.qst",   "2.10.1" },
    { "2.10.2",          20070102,       0x210, 0x210, 1,   N_TEMPLATE, "templates/2.10.qst",   "2.10.2" },
    { "2.10.3",          20070103,       0x210, 0x210, 1,   N_TEMPLATE, "templates/2.10.qst",   "2.10.3" },
    { "2.11 Beta 6",     20080101 + 6,   0x211, 0x211, 6,   N_TEMPLATE, "templates/2.11.qst",   NULL },
    { "2.11 Beta 18",    20080101 + 18,  0x211, 0x211, 18,  N_NONE,     NULL,                   "2.11 Beta 18" },
    { "2.50.0",          20121221,       0x250, 0x250, 24,  N_TEMPLATE, "templates/2.50.0.qst", "2.50.0" },
    { "2.50.1",          20141004,       0x250, 0x250, 28,  N_TEMPLATE, "templates/2.50.1.qst", "2.50.1" },
    { "2.50.2",          20150801,       0x250, 0x250, 29,  N_TEMPLATE, "templates/2.50.2.qst", "2.50.2" },
    { "2.50.3",          20151022,       0x250, 0x250, 30,  N_TEMPLATE, "templates/2.50.2.qst", "2.50.3" },
    { "2.53.0",          20190403,       0x250, 0x250, 32,  N_253_0,    NULL,                   "2.53.0" },
    { "2.53.1",          20190417,       0x250, 0x250, 33,  N_253_1,    NULL,                   "2.53.1" },
};
const int g_build_count = (int)(sizeof(g_builds) / sizeof(g_builds[0]));
const int TAG_OWN = 1000;      // the entry of a header no build above names

int gen_of(word zv)
{
    return zv <= 0x190 ? 0x190 : zv <= 0x193 ? 0x192 : zv <= 0x210 ? 0x210 : zv == 0x211 ? 0x211 : 0x250;
}

bool is_old(word zv)
{
    return zv >= 0x100 && zv <= 0x250;
}

// The build above that made a quest with this header, or -1.
int static_for(word zv, byte b)
{
    for(int i = 0; i < g_build_count; ++i)
    {
        const OldBuild &o = g_builds[i];

        if(o.zv != zv)
            continue;

        if(zv == 0x190 || o.build == b)
            return i;
    }

    return -1;
}

// For a header no build above names: its name and its place among the others.
std::string dynamic_name(word zv, byte b)
{
    char buf[64];

    if(zv == 0x250)
        snprintf(buf, sizeof(buf), b <= 23 ? "2.50 beta (build %d)" : b <= 27 ? "2.50.1 Gamma (build %d)" : "2.53 beta (build %d)", b);
    else if(zv == 0x211)
        snprintf(buf, sizeof(buf), "2.11 Beta %d", b);
    else if(zv == 0x192)
        snprintf(buf, sizeof(buf), "1.92 Beta %d", b);
    else if(b)
        snprintf(buf, sizeof(buf), "%x.%02x (build %d)", zv >> 8, zv & 0xFF, b);
    else
        snprintf(buf, sizeof(buf), "%x.%02x", zv >> 8, zv & 0xFF);

    return buf;
}

long dynamic_order(word zv, byte b)
{
    if(zv < 0x190)
        return 20000101 + (zv & 0xFF);
    if(zv == 0x192)
        return 20010501 + b;
    if(zv == 0x193)
        return 20030801 + b;
    if(zv < 0x210)
        return 20050101 + (zv & 0xFF);
    if(zv == 0x210)
        return 20070110 + b;
    if(zv == 0x211)
        return 20080101 + b;

    // 0x250
    return b <= 23 ? 20120101 + b : b <= 27 ? 20140901 + b : 20171015;
}

const ZqOldBuildTable *table_row(const char *name)
{
    if(!name)
        return NULL;

    for(int i = 0; i < zq_old_build_table_count; ++i)
        if(!strcmp(zq_old_build_table[i].name, name))
            return &zq_old_build_table[i];

    return NULL;
}

dword section_id(int s)
{
    static const dword ids[ZQOS_COUNT] =
    {
        ID_HEADER, ID_RULES, ID_STRINGS, ID_MISC, ID_TILES, ID_COMBOS, ID_CSETS, ID_MAPS, ID_DMAPS, ID_DOORS, ID_ITEMS,
        ID_WEAPONS, ID_COLORS, ID_ICONS, ID_INITDATA, ID_GUYS, ID_MIDIS, ID_CHEATS, ID_COMBOALIASES, ID_LINKSPRITES,
        ID_SUBSCREEN, ID_ITEMDROPSETS, ID_FFSCRIPT, ID_SFX, ID_FAVORITES
    };
    return ids[s];
}

// Whether every section of the open quest's file is at the version this build writes it in.
bool parts_at(const ZqOldBuildTable &t)
{
    if(zq_quest_file.parts.empty() || zq_quest_file.inexact)
        return false;

    for(int s = 1; s < ZQOS_COUNT; ++s)
    {
        const ZqQuestPart *p = zq_find_part(section_id(s));
        word v = 0, cv = 0;

        if(p)
            zq_part_versions(*p, &v, &cv);

        // (a section the build writes inside another one, as 2.10's misc colours and icons, has
        // a version but no section of its own)
        if(p && (v != t.version[s] || cv != t.cversion[s]))
            return false;
    }

    return true;
}

// ---- messages and the state after a save ------------------------------------------------

std::string g_last_error;

bool fail(const std::string &line1, const std::string &why)
{
    g_last_error = line1 + (why.empty() ? std::string("") : " " + why);
    al_trace("zq_builds_old: %s\n", g_last_error.c_str());

    if(!zq_batch_load)
        jwin_alert("ZQuest", line1.c_str(), why.empty() ? NULL : why.c_str(), "Nothing was saved.", "O&K", NULL, 'k', 0, lfont);

    return false;
}

void note_saved(const char *path)
{
    if(path != filepath)
        strcpy(filepath, path);

    zq_set_quest_title(filepath);
    saved = true;
    first_save = true;
    header.dirty_password = false;
    last_timed_save[0] = 0;
}

// What File > Open does with the file just saved: from now on the quest is that file.
bool reopen(const char *path, bool untitled)
{
    char p[2048];
    strncpy(p, path, sizeof(p) - 1);
    p[sizeof(p) - 1] = 0;
    const int map = Map.getCurrMap(), scr = Map.getCurrScr();
    int ret = load_quest(p, true, true);

    if(ret != qe_OK)
    {
        g_last_error = std::string("The quest was saved, but couldn't be opened again: ") + qst_error[ret];

        if(!zq_batch_load)
            jwin_alert("Error", "The quest was saved, but couldn't be opened again:", qst_error[ret], NULL, "O&K", NULL, 'k', 0, lfont);

        filepath[0] = 0;
        return false;
    }

    setup_combo_animations();
    setup_combo_animations2();
    Map.setCurrMap(untitled ? 0 : map);
    Map.setCurrScr(untitled ? 0 : scr);
    Map.setCanPaste(false);
    Map.setCanUndo(false);
    rebuild_trans_table();
    rebuild_string_list();
    onDrawingModeNormal();

    if(!untitled)
        note_saved(p);

    refresh(rALL);
    return true;
}

std::string temp_qst()
{
    char tmp[64];
    temp_name(tmp);
    return std::string(tmp) + ".qst";
}

void remove_temp(const std::string &path)
{
    remove(path.c_str());
    remove(zq_zinfo_path(path.c_str()).c_str());
}

bool read_file(const char *path, std::vector<byte> &out)
{
    FILE *f = fopen(path, "rb");

    if(!f)
        return false;

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    bool ok = n >= 0 && (out.empty() || fread(&out[0], 1, out.size(), f) == out.size());
    fclose(f);
    return ok;
}

// ---- the saves -----------------------------------------------------------------------------

bool save_own(const OldBuild *t, const std::string &name, const char *path);
bool save_253_1(const char *path);

// 2.50.0 to 2.53.0 from an older quest: ZQuest's 2.53 save, then the header's build set to the
// target's, packed and encoded as a .qst is.
bool save_250_patched(const OldBuild &t, const char *path)
{
    std::string why;

    if(!zq_tiles_fit_253(why))
        return fail(std::string("This quest can't be saved as ") + t.name + ":", why + ". Files of 2.53 and older hold tiles up to 65,519.");

    const std::string raw = temp_qst(), packed = temp_qst();
    const zquestheader keep = header;   // the 2.53 save marks the header as 2.53.1's
    int ret = save_unencoded_quest(raw.c_str(), false);
    box_end(false);
    header = keep;
    std::vector<byte> b;

    if(ret || !read_file(raw.c_str(), b))
    {
        remove(raw.c_str());
        return fail(std::string("This quest can't be saved as ") + t.name + ":", "the 2.53 save it starts from failed.");
    }

    remove(raw.c_str());
    const size_t at_version = 43, at_build = 45;    // id string, section id, version, cversion, size

    if(b.size() <= at_build || memcmp(&b[0], QH_NEWIDSTR, strlen(QH_NEWIDSTR)) || b[at_version] != 0x50 ||
       b[at_version + 1] != 0x02 || b[at_build] != VERSION_BUILD)
        return fail(std::string("This quest can't be saved as ") + t.name + ":", "the 2.53 save's header isn't as expected.");

    b[at_build] = t.build;
    PACKFILE *f = pack_fopen_password(packed.c_str(), F_WRITE_PACKED, datapwd);

    if(!f)
        return fail(std::string("This quest can't be saved as ") + t.name + ":", "a temporary file couldn't be written.");

    bool ok = pack_fwrite(&b[0], (long)b.size(), f) == (long)b.size();
    pack_fclose(f);
    const std::string out_tmp = std::string(path) + ".zqsave.tmp";
    remove(out_tmp.c_str());

    if(!ok || encode_file_007(packed.c_str(), out_tmp.c_str(), ((INTERNAL_VERSION + rand()) & 0xffff) + 0x413F0000, ENC_STR, ENC_METHOD_MAX - 1))
    {
        remove(packed.c_str());
        remove(out_tmp.c_str());
        return fail(std::string("This quest can't be saved as ") + t.name + ":", "the file couldn't be written.");
    }

    remove(packed.c_str());
    remove(path);

    if(rename(out_tmp.c_str(), path))
    {
        remove(out_tmp.c_str());
        return fail(std::string("This quest can't be saved as ") + t.name + ":", "the saved file couldn't replace the old one.");
    }

    return reopen(path, false);
}

// The same section versions, a newer build number: the quest's own-version save (so every edit
// is in ZQuest's copy of the file), then its header marked as the target's and every part copied.
bool save_restamped(const OldBuild &t, const char *path)
{
    std::string why;
    const std::string tmp = temp_qst();
    int r = zq_save_own_format(tmp.c_str(), false, why);
    remove_temp(tmp);

    if(r)
        return fail(std::string("This quest can't be saved as ") + t.name + ":", why);

    const word ozv = header.zelda_version;
    const byte ob = header.build;

    if(!zq_new_quest_stamp(t.zv, t.build))
        return fail(std::string("This quest can't be saved as ") + t.name + ":", "its header couldn't be marked as that build's.");

    r = zq_save_own_format(path, true, why);

    if(r)
    {
        zq_new_quest_stamp(ozv, ob);
        return fail(std::string("This quest can't be saved as ") + t.name + ":", why);
    }

    return reopen(path, false);
}

bool save_own(const OldBuild *t, const std::string &name, const char *path)
{
    if(t && t->zv == 0x250 && t->build == VERSION_BUILD)
        return save_253_1(path);

    std::string why;
    int r = zq_save_own_format(path, true, why);

    if(r)
        return fail("This quest can't be saved as " + name + ":", why);

    note_saved(path);
    return true;
}

// ZQuest's own 2.53 save, as Save As 2.53.1 always did.
bool save_253_1(const char *path)
{
    std::string why;

    if(!zq_tiles_fit_253(why))
        return fail("This quest can't be saved as 2.53.1:", why + ". Files of 2.53 and older hold tiles up to 65,519.");

    if(save_quest(path, false))
        return fail("Error saving the quest as 2.53.1.", "");

    strcpy(filepath, path);
    zq_sections_forget();           // the file is a 2.53.1 quest now
    note_saved(filepath);
    refresh(rMENU);
    return true;
}

bool save_as_old(const zq_build &b, const char *path)
{
    g_last_error.clear();
    const word zv = header.zelda_version;
    const byte qb = header.build;

    if(!is_old(zv))
        return fail("This quest can't be saved as " + b.name + ":", "it was made in a newer version.");

    if(b.tag == TAG_OWN)
        return save_own(NULL, b.name, path);

    if(b.tag < 0 || b.tag >= g_build_count || g_builds[b.tag].gen != b.gen)
        return fail("This quest can't be saved as " + b.name + ".", "");

    const OldBuild &t = g_builds[b.tag];
    const int made = static_for(zv, qb);
    const long made_order = made >= 0 ? g_builds[made].order : dynamic_order(zv, qb);

    if(t.order < made_order)
        return fail(std::string("This quest can't be saved as ") + t.name + ":", "that is older than the version it was made in.");

    // its own build (2.10.1 to 2.10.3 all write build 1: the same file)
    if(zv == t.zv && (qb == t.build || zv == 0x190))
        return save_own(&t, t.name, path);

    if(t.zv == 0x250 && zv <= 0x250)
        return t.build == VERSION_BUILD ? save_253_1(path) : save_250_patched(t, path);

    const ZqOldBuildTable *row = table_row(t.table);

    if(zv == t.zv && qb < t.build && row && parts_at(*row))
        return save_restamped(t, path);

    return fail(std::string("A quest made in ") + dynamic_name(zv, qb) + " can't be saved as " + t.name + " yet.",
                "ZQuest can write it in its own version, or as 2.50.0 or newer.");
}

// ---- File > New ------------------------------------------------------------------------------

void new_untitled(const char *version)
{
    memset(filepath, 0, 255);
    first_save = false;
    saved = true;
    char buf[256];
    snprintf(buf, sizeof(buf), ZQ_WINDOW_NAME " - Untitled Quest (ZC %s)", version);
    set_window_title(buf);
    refresh(rALL);
    last_timed_save[0] = 0;
}

bool new_failed(const char *name, const std::string &why)
{
    g_last_error = std::string("The new quest couldn't be made for ") + name + ": " + why;
    al_trace("zq_builds_old: %s\n", g_last_error.c_str());

    if(!zq_batch_load)
        jwin_alert("ZQuest", (std::string("The new quest couldn't be made for ") + name + ":").c_str(), why.c_str(),
                   "It is a 2.53.1 quest.", "O&K", NULL, 'k', 0, lfont);

    NewQuestFile(0);
    return false;
}

bool make_new_old(const zq_build &b)
{
    g_last_error.clear();

    if(b.tag < 0 || b.tag >= g_build_count || g_builds[b.tag].gen != b.gen)
        return false;

    const OldBuild &t = g_builds[b.tag];

    switch(t.new_kind)
    {
    case N_253_1:
    case N_253_0:
        NewQuestFile(0);

        if(RulesetDialog > 0 && !zq_batch_load)
            PickRuleset();

        if(t.new_kind == N_253_1)
            return true;

        if(!zq_new_quest_stamp(0x250, t.build))
        {
            if(!zq_batch_load)
                jwin_alert("ZQuest", "The new quest couldn't be made for 2.53.0.", "It is a 2.53.1 quest.", NULL, "O&K", NULL, 'k', 0, lfont);

            g_last_error = "the 2.53.1 new quest couldn't be marked as 2.53.0's";
            return false;
        }

        new_untitled(t.name);
        return true;

    case N_TEMPLATE:
    {
        char p[2048];
        strncpy(p, t.tpl, sizeof(p) - 1);
        p[sizeof(p) - 1] = 0;

        if(!exists(p))
            return new_failed(t.name, std::string("its new-quest file, ") + t.tpl + ", is missing.");

        int ret = load_quest(p, true, true);

        if(ret != qe_OK)
            return new_failed(t.name, std::string("its new-quest file couldn't be opened: ") + qst_error[ret]);

        if(header.zelda_version != t.zv || header.build != t.build)
        {
            // The file is one an earlier build saved; this build's ZQuest wrote its own build
            // number into the header when it saved a quest made from it. Saved so and opened
            // again, so the quest is what opening that file gives.
            std::string why;
            const std::string tmp = temp_qst();

            if(!zq_new_quest_stamp(t.zv, t.build) || zq_save_own_format(tmp.c_str(), true, why))
            {
                remove_temp(tmp);
                return new_failed(t.name, why.empty() ? std::string("its header couldn't be marked as that build's.") : why);
            }

            bool ok = reopen(tmp.c_str(), true);
            remove_temp(tmp);

            if(!ok)
                return new_failed(t.name, g_last_error);
        }
        else
        {
            setup_combo_animations();
            setup_combo_animations2();
            Map.setCanPaste(false);
            Map.setCanUndo(false);
            rebuild_trans_table();
            rebuild_string_list();
            onDrawingModeNormal();
        }

        zinit.last_map = 0;
        zinit.last_screen = 0;
        Map.setCurrMap(0);
        Map.setCurrScr(0);
        new_untitled(t.name);
        return true;
    }
    }

    return false;
}

} // namespace

void zq_builds_old(std::vector<zq_build> &out)
{
    for(int i = 0; i < g_build_count; ++i)
    {
        const OldBuild &o = g_builds[i];
        zq_build b;
        b.name = o.name;
        b.order = o.order;
        b.gen = o.gen;
        b.tag = i;
        b.save_as = save_as_old;
        b.make_new = NULL;

        if(o.new_kind == N_253_1 || o.new_kind == N_253_0 || (o.new_kind == N_TEMPLATE && exists(o.tpl)))
            b.make_new = make_new_old;

        out.push_back(b);
    }

    // a quest whose header no build above names: its own version, while it is open
    const word zv = header.zelda_version;
    const byte qb = header.build;

    if(is_old(zv) && static_for(zv, qb) < 0)
    {
        zq_build b;
        b.name = dynamic_name(zv, qb);
        b.order = dynamic_order(zv, qb);
        b.gen = gen_of(zv);
        b.tag = TAG_OWN;
        b.save_as = save_as_old;
        b.make_new = NULL;
        out.push_back(b);
    }
}

long zq_made_order_old()
{
    const word zv = header.zelda_version;
    const byte qb = header.build;

    if(!is_old(zv))
        return -1;

    int i = static_for(zv, qb);
    return i >= 0 ? g_builds[i].order : dynamic_order(zv, qb);
}

// ---- test switches (until the shared Save As and New lists are in) ----------------------------
//
// -qst-build-save in.qst "<build>" out.qst report.txt   opens the quest, Save As that build
// -qst-build-new "<build>" out.qst report.txt           File > New in that build, then Save As the
//                                                       same build (a new quest's first save)
// -qst-build-list report.txt                            the builds, the order of the quest made, New
// ZQ_BUILD_TEST_EDIT=1: before the save, combo 1 on screen 0's first cell of map 0 (a small edit).
// Each writes one report line: what was done, SAVED or NOT SAVED, the saved file's header.

namespace
{

const zq_build *find_build(std::vector<zq_build> &all, const char *name)
{
    for(size_t i = 0; i < all.size(); ++i)
        if(all[i].name == name)
            return &all[i];

    return NULL;
}

void test_edit()
{
    if(!getenv("ZQ_BUILD_TEST_EDIT") || TheMaps.empty())
        return;

    TheMaps[0].data[0] = 1;
    TheMaps[0].valid |= mVALID;
    saved = false;
}

void report(const char *file, const std::string &line)
{
    FILE *f = fopen(file, "a");

    if(f)
    {
        fprintf(f, "%s\n", line.c_str());
        fclose(f);
    }
}

std::string header_text()
{
    char buf[128];
    snprintf(buf, sizeof(buf), "header 0x%X build %d, made_order %ld, parts %d, map 0 screen 0 cell 0 combo %d",
             header.zelda_version, header.build, zq_made_order_old(), (int)zq_quest_file.parts.size(),
             TheMaps.empty() ? -1 : (int)TheMaps[0].data[0]);
    return buf;
}

} // namespace

int zq_builds_old_switches(int argc, char **argv)
{
    for(int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];

        if(a == "-qst-build-save" && i + 4 < argc)
        {
            skip_quest_password = true;
            zq_batch_load = true;
            int ret = load_quest(argv[i + 1], true, true);
            std::string line = std::string(argv[i + 1]) + " as " + argv[i + 2] + ": ";

            if(ret != qe_OK)
            {
                report(argv[i + 4], line + "NOT SAVED, didn't load");
                return 3;
            }

            line += "made " + header_text() + "; ";
            std::vector<zq_build> all;
            zq_builds_old(all);
            const zq_build *b = find_build(all, argv[i + 2]);

            if(!b)
            {
                report(argv[i + 4], line + "NOT SAVED, no such build");
                return 3;
            }

            test_edit();
            bool ok = b->save_as(*b, argv[i + 3]);
            report(argv[i + 4], line + (ok ? "SAVED; now " + header_text() : "NOT SAVED, " + g_last_error));
            return ok ? 0 : 3;
        }

        if(a == "-qst-build-new" && i + 3 < argc)
        {
            skip_quest_password = true;
            zq_batch_load = true;
            std::vector<zq_build> all;
            zq_builds_old(all);
            const zq_build *b = find_build(all, argv[i + 1]);
            std::string line = std::string("new ") + argv[i + 1] + ": ";

            if(!b || !b->make_new)
            {
                report(argv[i + 3], line + "NOT MADE, no New for it");
                return 3;
            }

            if(!b->make_new(*b))
            {
                report(argv[i + 3], line + "NOT MADE, " + g_last_error);
                return 3;
            }

            line += "made " + header_text() + "; ";
            test_edit();
            bool ok = b->save_as(*b, argv[i + 2]);
            report(argv[i + 3], line + (ok ? "SAVED; now " + header_text() : "NOT SAVED, " + g_last_error));
            return ok ? 0 : 3;
        }

        if(a == "-qst-build-list" && i + 1 < argc)
        {
            std::vector<zq_build> all;
            zq_builds_old(all);

            for(size_t k = 0; k < all.size(); ++k)
            {
                char buf[160];
                snprintf(buf, sizeof(buf), "%-24s order %ld gen 0x%X tag %d new %s", all[k].name.c_str(), all[k].order,
                         all[k].gen, all[k].tag, all[k].make_new ? "yes" : "no");
                report(argv[i + 1], buf);
            }

            report(argv[i + 1], "open quest: " + header_text());
            return 0;
        }
    }

    return -1;
}

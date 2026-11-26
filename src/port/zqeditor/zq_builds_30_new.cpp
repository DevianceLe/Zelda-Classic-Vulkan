// File > New in a 3.0 build (zq_builds_30.cpp): upstream's tileset wizard, as the 3.0 Nightly New
// has it (zq_files.cpp, zq_new_quest.cpp), with the tileset that build's own File > New used:
// Classic (resources/modules/classic/default.qst in that build) and, from prerelease 84 on,
// Cambria (resources/tilesets/cambria.qst), shipped as templates\30\<git blob>.qst. Without a
// build's own file, the 3.0 Nightly tilesets (templates\classic_3.0.qst, cambria.qst) stand in.
// The tileset is made a quest of that build by its Save As and opened from there; then the
// wizard's rules, stored with that build's compat rule version.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zq_page.h"
#include "gui.h"
#include "zdefs.h"
#include "zquest.h"
#include "zq_files.h"
#include "zc_sys.h"
#include "zq_misc.h"
#include "qst.h"
#include "zsys.h"
#include "zq_class.h"
#include "jwin.h"
#include "port/zqeditor/zq_q255.h"
#include "port/zqeditor/zq_q255_store.h"
#include "port/zqeditor/zq_writers.h"
#include "port/zqeditor/zq_zinfo.h"
#include "port/zqeditor/zq_builds.h"

extern FONT *lfont;

bool zq_new_quest_from_tileset(int ruleset, bool bugfix, bool zs_bugfix, std::string &why);    // zq_new_quest.cpp
int zq30_save_as(int fmt, const char *path, std::string &why);                                  // zq_builds_30.cpp
bool zq30_format_has_cambria(int fmt);
const char *zq30_format_tileset(int fmt, int which);
int zq30_format_compat(int fmt);
int zq30_format_init_version(int fmt);
int zq30_format_count();

namespace
{

const char *g_names[2] = { "Cambria (recommended)", "Classic" };
int g_offered = 2;

const char *tileset_list(int index, int *list_size)
{
    if(index < 0)
    {
        *list_size = g_offered;
        return NULL;
    }

    return g_offered == 1 ? "Classic" : index < 2 ? g_names[index] : "";
}

const char *g_rulesets[6] = { "The tileset's own", "Modern", "SNES (Enhanced)", "SNES (BS)", "Fixed NES", "Authentic NES" };
const int g_ruleset_ids[6] = { 0, 5, 4, 3, 2, 1 };

const char *ruleset_list(int index, int *list_size)
{
    if(index < 0)
    {
        *list_size = 6;
        return NULL;
    }

    return index < 6 ? g_rulesets[index] : "";
}

ListData g_tileset_data(tileset_list, &font);
ListData g_ruleset_data(ruleset_list, &font);

DIALOG g_dlg[] =
{
    { jwin_win_proc,          0,      0,    240,    182,    vc(14),                vc(1),                  0,    D_EXIT,     0,           0, (void *) "New Quest: Choose a tileset", NULL, NULL },
    { jwin_text_proc,        12,     26,    216,      8,    vc(14),                vc(1),                  0,    0,          0,           0, (void *) "The new quest starts from this tileset:", NULL, NULL },
    { jwin_list_proc,        12,     38,    216,     36,    jwin_pal[jcBOXFG],     jwin_pal[jcLIGHT],      0,    0,          0,           0, (void *) &g_tileset_data, NULL, NULL },
    { jwin_check_proc,       12,     82,    150,      9,    vc(14),                vc(1),                  0,    D_SELECTED, 1,           0, (void *) "Apply bug fixes", NULL, NULL },
    { jwin_check_proc,       12,     96,    150,      9,    vc(14),                vc(1),                  0,    D_SELECTED, 1,           0, (void *) "Apply script bug fixes", NULL, NULL },
    { jwin_text_proc,        12,    116,     50,      8,    vc(14),                vc(1),                  0,    0,          0,           0, (void *) "Ruleset:", NULL, NULL },
    { jwin_droplist_proc,    62,    112,    166,     16,    jwin_pal[jcTEXTFG],    jwin_pal[jcTEXTBG],     0,    0,          0,           0, (void *) &g_ruleset_data, NULL, NULL },
    { jwin_button_proc,      44,    148,     61,     21,    vc(14),                vc(1),                 13,    D_EXIT,     0,           0, (void *) "OK",                  NULL,   NULL  },
    { jwin_button_proc,     134,    148,     61,     21,    vc(14),                vc(1),                 27,    D_EXIT,     0,           0, (void *) "Cancel",              NULL,   NULL  },
    { d_timer_proc,           0,      0,      0,      0,    0,                     0,                      0,    0,          0,           0,  NULL,                             NULL,   NULL  },
    { NULL,                   0,      0,      0,      0,    0,                     0,                      0,    0,          0,           0,  NULL,                             NULL,   NULL  }
};

// the build's own tileset file, or the 3.0 Nightly one
std::string tileset_file(int fmt, int which)
{
    const char *blob = zq30_format_tileset(fmt, which);

    if(blob && *blob)
    {
        std::string p = std::string("templates/30/") + blob + ".qst";

        if(exists(p.c_str()))
            return p;
    }

    return which == 1 ? "templates/cambria.qst" : "templates/classic_3.0.qst";
}

void untitled(const char *version)
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

} // namespace

// The new quest made without the dialog: tileset 0 Classic, 1 Cambria (tests and the dialog).
bool zq30_new_from(int fmt, const char *version_name, int which, int ruleset, bool bugfix, bool zs_bugfix, std::string &why)
{
    const std::string file = tileset_file(fmt, which);
    char p[2048];
    strncpy(p, file.c_str(), sizeof(p) - 1);
    p[sizeof(p) - 1] = 0;
    int ret = load_quest(p, true, true);

    if(ret != qe_OK)
    {
        why = std::string("the tileset ") + file + " couldn't be opened";
        return false;
    }

    char tmp[64];
    temp_name(tmp);
    std::string path = std::string(tmp) + ".qst";

    if(zq30_save_as(fmt, path.c_str(), why) != 0)
    {
        remove(path.c_str());
        return false;
    }

    strncpy(p, path.c_str(), sizeof(p) - 1);
    ret = load_quest(p, true, true);
    remove(path.c_str());
    remove(zq_zinfo_path(path.c_str()).c_str());

    if(ret != qe_OK)
    {
        why = "the tileset was saved as the build, but couldn't be opened again";
        return false;
    }

    // TILESET_CLEARHEADER: no title, author, version, minimum version or password
    memset(header.title, 0, sizeof(header.title));
    memset(header.author, 0, sizeof(header.author));
    memset(header.version, 0, sizeof(header.version));
    memset(header.minver, 0, sizeof(header.minver));
    set_questpwd("", false);
    header.dirty_password = false;

    std::string w;
    bool rules_ok = zq_new_quest_from_tileset(ruleset, bugfix, zs_bugfix, w);

    // the build's own compat rule version (the wizard stores 3.0 Nightly's); a build whose init
    // data has no viewport deadzone (before format 51) has none to set
    ZqRuleSection s;

    if(zq255_rules_get(s) && s.version >= 17)
    {
        s.compat = (dword)zq30_format_compat(fmt);
        std::string w2;

        if(!zq255_rules_put(s, w2) && rules_ok)
        {
            rules_ok = false;
            w = w2;
        }
    }

    if(!rules_ok && zq30_format_init_version(fmt) >= 51)
    {
        why = "the new quest's rules couldn't be set: " + w;
        return false;
    }

    Map.setCurrMap(vbound(zinit.last_map, 0, map_count - 1));
    Map.setCurrScr(zinit.last_screen);
    untitled(version_name);
    saved = false;          // as upstream: the new quest is unsaved
    return true;
}

bool zq30_new_quest(const zq_build &b, int fmt)
{
    if(fmt < 0 || fmt >= zq30_format_count())
        return false;

    g_offered = zq30_format_has_cambria(fmt) ? 2 : 1;
    g_dlg[0].dp2 = lfont;

    if(is_large)
        large_dialog(g_dlg);

    g_dlg[2].d1 = 0;
    g_dlg[2].d2 = 0;
    g_dlg[6].d1 = 0;
    g_dlg[3].flags |= D_SELECTED;
    g_dlg[4].flags |= D_SELECTED;

    if(zc_popup_dialog(g_dlg, 7) != 7)
        return false;

    // the list: Cambria, Classic; or Classic alone
    const int which = g_offered == 1 ? 0 : (g_dlg[2].d1 == 1 ? 0 : 1);
    const bool bugfix = (g_dlg[3].flags & D_SELECTED) != 0;
    const bool zs_bugfix = (g_dlg[4].flags & D_SELECTED) != 0;
    const int rs = g_dlg[6].d1;
    const int ruleset = rs >= 0 && rs < 6 ? g_ruleset_ids[rs] : 0;
    std::string why;

    if(!zq30_new_from(fmt, b.name.c_str(), which, ruleset, bugfix, zs_bugfix, why))
    {
        jwin_alert("ZQuest", ("The new " + b.name + " quest couldn't be made:").c_str(), why.c_str(), NULL, "O&K", NULL, 'k',
                   0, lfont);
        NewQuestFile(0);
        return false;
    }

    return true;
}

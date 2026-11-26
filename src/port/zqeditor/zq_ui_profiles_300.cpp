// zq_ui_profiles_300.cpp: ZQuest's interface in the 3.0 builds (3.0.0-prerelease.1 to .239, and our
// 3.0 Nightly, which shows the newest), registered with the interface profiles (zq_ui_profile.h).
//
// - Menus: every build's own menus, as its source has them, in zq_ui_profiles_300_data.h (made by
//   tools/zq_gen_ui_profiles_300.py from the build inventory). Builds with the same menus share a
//   table; a profile starts where the menus change and covers the builds up to the next one.
//   Each entry runs the editor this ZQuest has for it: one of today's menu entries, or one of the
//   actions below where 3.0 put several of today's dialogs behind one entry (Quest > Options holds
//   the rules, the header and the cheats; Screen > Room Data the room type, guy, string and catch
//   all) or opened a part of an editor today's menus reach another way (Misc Data > Bottle Types).
//   An entry with no editor here shows disabled, with the build's text.
// - Look: 3.0's own default theme (themes/dark.ztheme, the default in every 3.0 build; its
//   colours were stored as 6-bit values before prerelease 48 and as 8-bit ones after, the same
//   colours), its windows classic.
// - Layout: 3.0's ZQuest runs at 912x684 in its Large or Compact mode; this ZQuest's Large mode
//   (800x600) has 3.0's Large arrangement (screen top left, combo lists right, favourites below
//   them, minimap and the screen's squares under the screen, commands between), so the profile
//   asks for Large and leaves the panels where Large has them.
//
// Data only, plus the few actions calling the editor's own functions.

#include "precompiled.h" //always first

#include <string.h>
#include <string>
#include <vector>

#include "jwin.h"
#include "port/zqeditor/zq_ui_profile.h"

extern FONT *lfont;

// the editor (zquest.cpp, zq_record_editor.cpp, zq_rules_editor.cpp, zq_misc.cpp)
int (*zq_audit_menu(const char *path, bool *disabled, std::vector<std::string> *all))();
int select_data(const char *prompt, int index, const char *(proc)(int, int *), FONT *title_font, void (*copyFunc)(int, int));
bool zq255_quest_is_newer();
bool zq_edit_rules_newer(int category, bool *shown);
bool zq_edit_misc_newer(const char *tab, bool *shown);
int onShowDarkness();
long zq30_order_of_prerelease(int n);       // zq_builds_30.cpp: a 3.0 build's place in the catalog

#include "port/zqeditor/zq_ui_profiles_300_data.h"

namespace
{

// ---- the look: 3.0's themes/dark.ztheme -----------------------------------------------------------
// colours 1-8; jcbox 6, jclight 5, jcmedlt 6, jcmeddark 3, jcdark 2, jcboxfg 1, jctitlel 3,
// jctitler 5, jctitlefg 7, jctextbg 5, jctextfg 1, jcselbg 8, jcselfg 6. The title bar is a
// gradient from colour 3 to colour 5 (jwin_draw_titlebar's dither from jcTITLEL to jcTITLER),
// put here on dvc(9..15) as the editor's own title gradient.
const zq_ui_look look_30 =
{
    "3.0 (dark)", false,
    { 0, 0x1098B8, 0x100800, 0x445050, 0x107090, 0x000000, 0x343838, 0xA8F0C0, 0x18C48C },
    0x445050, 0x000000, 7,
    { 6, 5, 6, 3, 2, 1, 9, 15, 7, 5, 1, 8, 6 },
    1, 0
};

const zq_ui_layout layout_30 = { "3.0 Large", 1, 0, 0, NULL, 0 };

// ---- the actions ----------------------------------------------------------------------------------

// one of today's menu entries, by its path
int run_path(const char *path)
{
    bool disabled = false;
    int (*proc)() = zq_audit_menu(path, &disabled, NULL);
    return proc && !disabled ? proc() : D_O_K;
}

// a short list to pick one of several of today's dialogs from (3.0 has them in one)
struct pick_entry
{
    const char *text;
    const char *path;
};

const pick_entry *g_pick = NULL;
int g_pick_count = 0;

const char *pick_list(int index, int *list_size)
{
    if(index < 0)
    {
        *list_size = g_pick_count;
        return NULL;
    }

    return index < g_pick_count ? g_pick[index].text : "";
}

int pick(const char *title, const pick_entry *entries, int count)
{
    g_pick = entries;
    g_pick_count = count;
    const int ret = select_data(title, 0, pick_list, lfont, NULL);
    g_pick = NULL;
    g_pick_count = 0;
    return ret >= 0 && ret < count ? run_path(entries[ret].path) : D_O_K;
}

// Quest > Options: 3.0's quest rules dialog, with its Header and Cheats buttons. A 2.55 or 3.0
// quest's rules are one editor (every category); older quests' are today's seven dialogs.
const pick_entry options_newer[] =
{
    { "Rules", NULL },
    { "Header", "Quest/Header" },
    { "Cheats", "Quest/Cheats" },
    { "Map Count", "Quest/Map Count" },
};

const pick_entry options_older[] =
{
    { "Rules: Animation", "Quest/Rules/Animation" },
    { "Rules: Combos", "Quest/Rules/Combos" },
    { "Rules: Items", "Quest/Rules/Items" },
    { "Rules: Enemies", "Quest/Rules/Enemies" },
    { "Rules: NES Fixes", "Quest/Rules/NES Fixes" },
    { "Rules: Other", "Quest/Rules/Other" },
    { "Rules: Backward compatibility", "Quest/Rules/Backward compatibility" },
    { "Header", "Quest/Header" },
    { "Cheats", "Quest/Cheats" },
    { "Map Count", "Quest/Map Count" },
};

int q30_options()
{
    if(zq255_quest_is_newer())
    {
        g_pick = options_newer;
        g_pick_count = (int)(sizeof(options_newer) / sizeof(options_newer[0]));
        const int ret = select_data("Options", 0, pick_list, lfont, NULL);
        g_pick = NULL;
        g_pick_count = 0;

        if(ret == 0)
        {
            bool shown = false;

            if(!zq_edit_rules_newer(-1, &shown) && !shown)
                return run_path("Quest/Rules/Other");

            return D_O_K;
        }

        return ret > 0 && ret < 4 ? run_path(options_newer[ret].path) : D_O_K;
    }

    return pick("Options", options_older, (int)(sizeof(options_older) / sizeof(options_older[0])));
}

// Screen > Room Data: 3.0's room dialog holds the room type, its guy and string, and the catch all.
const pick_entry room_entries[] =
{
    { "Room Type", "Screen/Room Type" },
    { "Guy", "Screen/Guy" },
    { "Message String", "Screen/Message String" },
    { "Catch All", "Screen/Catch All" },
};

int q30_room()
{
    return pick("Room Data", room_entries, (int)(sizeof(room_entries) / sizeof(room_entries[0])));
}

// Screen > Screen Data: 3.0's screen data dialog also holds what only 2.55 and 3.0 screens have
// (scripts, music, lens layers...), which today is a dialog of its own.
const pick_entry screen_entries[] =
{
    { "Screen Data", "Screen/Screen Data" },
    { "Scripts, Music and the rest (2.55/3.0)", "Screen/2.55/3.0 Screen Data..." },
};

int q30_screen_data()
{
    if(!zq255_quest_is_newer())
        return run_path("Screen/Screen Data");

    return pick("Screen Data", screen_entries, 2);
}

// Quest > Strings: 3.0's string editor also holds the strings' margins, portraits, shadows,
// layers and sounds.
const pick_entry string_entries[] =
{
    { "Strings", "Quest/Strings" },
    { "Margins, Portraits and the rest (2.55/3.0)", "Quest/2.55/3.0 String Data..." },
};

int q30_strings()
{
    if(!zq255_quest_is_newer())
        return run_path("Quest/Strings");

    return pick("Strings", string_entries, 2);
}

// Misc Data > Bottle Types and so on: those tabs of the 2.55/3.0 misc data editor
int misc_tab(const char *tab)
{
    bool shown = false;

    if(zq255_quest_is_newer())
        zq_edit_misc_newer(tab, &shown);

    return shown ? D_O_K : run_path("Quest/Misc Data/2.55/3.0 Misc Data...");
}

int q30_bottle_types() { return misc_tab("Bottle types"); }
int q30_bottle_shops() { return misc_tab("Bottle shops"); }
int q30_misc_sprites() { return misc_tab("Misc sprites"); }
int q30_misc_sfx() { return misc_tab("Misc sounds"); }

// ZScript > Quest Script Settings / Compiler Settings: those categories of the rules editor
int rules_tab(int category)
{
    bool shown = false;

    if(zq255_quest_is_newer())
        zq_edit_rules_newer(category, &shown);

    return D_O_K;
}

int q30_script_rules() { return rules_tab(10); }        // "ZS: Script"
int q30_compiler_settings() { return rules_tab(15); }   // "Compiler Settings"

int q30_darkness()
{
    return onShowDarkness();
}

// ---- the profiles ---------------------------------------------------------------------------------

const int g_count = (int)(sizeof(zq30ui::g_builds) / sizeof(zq30ui::g_builds[0]));
zq_ui_profile g_profiles[sizeof(zq30ui::g_builds) / sizeof(zq30ui::g_builds[0])];
zq_ui_profile g_newest, g_nightly;

void add()
{
    for(int i = 0; i < g_count; ++i)
    {
        zq_ui_profile &p = g_profiles[i];
        // named after the first build with these menus; placed by that build's order, as the
        // catalog lists 3.0 builds by format (a profile covers the builds up to the next one)
        p.build = zq30ui::g_builds[i].build;
        p.gen = 0x300;
        p.order = zq30_order_of_prerelease(zq30ui::g_builds[i].first_n);
        p.menu = zq30ui::g_builds[i].menu;
        p.menu_rows = zq30ui::g_builds[i].rows;
        p.layout = &layout_30;
        p.look = &look_30;
        p.dialogs = NULL;
        p.dialog_count = 0;
        zq_ui_add_profile(&p);
    }

    // the newest build by its own name too (it replaces the framework's hand-written profile of
    // it, and the Interface choice lists the newest build)
    const zq30ui::build_menus &newest = zq30ui::g_builds[g_count - 1];

    if(newest.last_n != newest.first_n)
    {
        g_newest = g_profiles[g_count - 1];
        g_newest.build = newest.last;
        g_newest.order = zq30_order_of_prerelease(newest.last_n);
        zq_ui_add_profile(&g_newest);
    }

    // and our 3.0 Nightly (made after the newest prerelease: order 240), under the name the
    // framework's hand-written 3.0 profile has, which it replaces
    g_nightly = g_profiles[g_count - 1];
    g_nightly.build = "3.0 Nightly 2026-11-06";
    g_nightly.order = zq30_order_of_prerelease(newest.last_n + 1);
    zq_ui_add_profile(&g_nightly);

    zq_ui_add_action("@q30_options", q30_options, 0);
    zq_ui_add_action("@q30_room", q30_room, 0);
    zq_ui_add_action("@q30_screen_data", q30_screen_data, 0);
    zq_ui_add_action("@q30_strings", q30_strings, 0);
    zq_ui_add_action("@q30_bottle_types", q30_bottle_types, 0x255);
    zq_ui_add_action("@q30_bottle_shops", q30_bottle_shops, 0x255);
    zq_ui_add_action("@q30_misc_sprites", q30_misc_sprites, 0x255);
    zq_ui_add_action("@q30_misc_sfx", q30_misc_sfx, 0x255);
    zq_ui_add_action("@q30_script_rules", q30_script_rules, 0x255);
    zq_ui_add_action("@q30_compiler_settings", q30_compiler_settings, 0x255);
    zq_ui_add_action("@q30_darkness", q30_darkness, 0);
}

zq_ui_registrar reg(add);

} // namespace

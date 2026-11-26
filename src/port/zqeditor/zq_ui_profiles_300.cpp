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
// - Layout: 3.0's ZQuest runs at 912x684, starting in its Compact mode: the screen at 3x, a narrow
//   combo pane of two columns, the layer buttons beside the page bar (rects_30_compact). Its Large
//   mode is kept too (rects_30_large).
// - Keys: each build's default hotkeys (zq_hotkey.cpp), as a key table: S the string editor, R the
//   room data, K combos, T tiles, L the darkness preview, Ctrl+S save, Ctrl+Z undo and so on; G and A
//   (today's guy and catch all keys) do nothing, as in 3.0.
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

// ---- the layout: 3.0's own, at 912x684 ------------------------------------------------------------
// From 3.0's load_size_poses. Every 3.0 build starts in Compact mode (ZQ_GUI compact_mode, default
// 1): the screen at 3x without its edges (768x528) under the menu bar, a narrow combo pane right of
// it (x 768, 144 wide) with two columns of 16-px combos, their scroll arrows above and the alias
// previews at y 476, the favourites under it (y 548, 136 high), the page bar under the screen with
// the layer panel (one row of buttons) beside it, and under them the minimap, the screen's squares
// and the favourite commands.
// This editor draws all nine page bars (3.0 Compact shows six of 48 px; here nine of 32 px fill the
// same 288 px). Its seven layer buttons need 616 px where 3.0's beside the page bar have 480, so
// the layer panel is a row of its own under the page bar and the panels under it start 20 px lower.
// It keeps at most 100 favourite combos and 8 favourite commands, and its commands are 88 px wide,
// so the commands window is two columns.
const zq_ui_rect rects_30_compact[] =
{
    { "mapscreen", -1, 0, 16, 3, 0 },
    { "combocolumns", -1, 0, 0, 2, 16 },
    { "combolist_window", -1, 768, 0, 144, 548 },
    { "combo_preview", -1, 872, 6, 32, 32 },
    { "combolist", 0, 773, 54, 4, 30 },
    { "combolist", 1, 842, 54, 4, 30 },
    { "comboaliaslist", 0, 773, 54, 4, 26 },
    { "comboaliaslist", 1, 842, 54, 4, 26 },
    { "comboalias_preview", 0, 773, 476, 64, 64 },
    { "comboalias_preview", 1, 842, 476, 64, 64 },
    { "combolistscrollers", 0, 794, 40, 11, 11 },
    { "combolistscrollers", 1, 863, 40, 11, 11 },
    { "favorites_window", -1, 768, 548, 144, 136 },
    { "favorites_list", -1, 776, 564, 8, 7 },
    { "map_page_bar", 0, 0, 544, 32, 20 },
    { "map_page_bar", 1, 32, 544, 32, 20 },
    { "map_page_bar", 2, 64, 544, 32, 20 },
    { "map_page_bar", 3, 96, 544, 32, 20 },
    { "map_page_bar", 4, 128, 544, 32, 20 },
    { "map_page_bar", 5, 160, 544, 32, 20 },
    { "map_page_bar", 6, 192, 544, 32, 20 },
    { "map_page_bar", 7, 224, 544, 32, 20 },
    { "map_page_bar", 8, 256, 544, 32, 20 },
    { "layer_panel", -1, 0, 564, 768, 20 },
    { "minimap", -1, 3, 587, 151, 97 },
    { "panel", -1, 154, 584, 422, 100 },
    { "commands_window", -1, 576, 584, 192, 100 },
    { "commands_list", -1, 584, 602, 2, 4 },
};

// 3.0's Large mode (Compact off): the screen at 2x with its edges (576x416), the combo pane right of
// it (x 576, 336 wide) with its columns spaced evenly, the favourites under it (y 548), the page
// bar under the screen, the layer panel under that, then the minimap, squares and commands. Kept
// for a profile that asks for it; 3.0's own start is Compact.
const zq_ui_rect rects_30_large[] =
{
    { "mapscreen", -1, 0, 16, 2, 1 },
    { "combocolumns", -1, 0, 0, 3, 16 },
    { "combolist_window", -1, 576, 0, 336, 548 },
    { "combo_preview", -1, 704, 6, 32, 32 },
    { "combolist", 0, 612, 60, 4, 30 },
    { "combolist", 1, 712, 60, 4, 30 },
    { "combolist", 2, 812, 60, 4, 30 },
    { "comboaliaslist", 0, 612, 60, 4, 25 },
    { "comboaliaslist", 1, 712, 60, 4, 25 },
    { "comboaliaslist", 2, 812, 60, 4, 25 },
    { "comboalias_preview", 0, 612, 476, 64, 64 },
    { "comboalias_preview", 1, 712, 476, 64, 64 },
    { "comboalias_preview", 2, 812, 476, 64, 64 },
    { "combolistscrollers", 0, 633, 47, 11, 11 },
    { "combolistscrollers", 1, 733, 47, 11, 11 },
    { "combolistscrollers", 2, 833, 47, 11, 11 },
    { "favorites_window", -1, 576, 548, 336, 136 },
    { "favorites_list", -1, 584, 564, 20, 5 },
    { "layer_panel", -1, 0, 452, 576, 16 },
    { "minimap", -1, 3, 472, 151, 97 },
    { "panel", -1, 154, 468, 230, 216 },
    { "commands_window", -1, 384, 468, 192, 216 },
    { "commands_list", -1, 392, 488, 2, 4 },
};

const zq_ui_layout layout_30 = { "3.0 Compact", 1, 912, 684, rects_30_compact,
                                 (int)(sizeof(rects_30_compact) / sizeof(rects_30_compact[0])) };
const zq_ui_layout layout_30_large = { "3.0 Large", 1, 912, 684, rects_30_large,
                                       (int)(sizeof(rects_30_large) / sizeof(rects_30_large[0])) };

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
        p.dialogs = NULL;       // the framework draws the builds' dialogs from the inventory itself
        p.dialog_count = 0;
        p.keys = zq30ui::g_builds[i].keys;
        p.key_count = zq30ui::g_builds[i].key_count;
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

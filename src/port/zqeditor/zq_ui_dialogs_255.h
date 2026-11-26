// zq_ui_dialogs_255.h: the extra actions 2.55's ZQuest menus need (zq_ui_profile.h), for the
// 2.55.0 ... 2.55.19 interface profiles (zq_ui_profiles_255.cpp, which includes this once).
//
// 2.55's menus name dialogs that this ZQuest splits in two: the 2.53 dialog, plus a window for
// what only 2.55 and 3.0 quests hold (Screen > 2.55/3.0 Screen Data, Quest > 2.55/3.0 String
// Data, the misc data editor's tabs, the rules editor's categories). These actions open the right
// part from 2.55's own entry: a 2.55 entry whose dialog has both parts asks which (a small window
// laid out like the 2.55 dialog's buttons), one that names a single tab opens there. Quest >
// Options is 2.55's Quest Options dialog: its Header, Cheats and Map Count buttons and its rules.
//
// Written for the port (no jwin or Allegro code): the windows are Dear ImGui, as the record
// editors' are; each choice runs the editor function after the window is closed.

#pragma once

#include <stdio.h>
#include <string>
#include <vector>

#include "zcport/ui.h"
#include <imgui.h>

// The editor's functions these actions open (zquest.cpp, zq_misc.cpp, zq_rules.cpp, the record
// and rules editors). Declared at file scope (never inside the unnamed namespace).
int onHeader();
int onCheats();
int onMapCount();
int onAnimationRules();
int onComboRules();
int onItemRules();
int onEnemyRules();
int onFixesRules();
int onMiscRules();
int onCompatRules();
int onScrData();
int onScrDataNewer();
int onStrings();
int onStringsNewer();
int onRType();
int onGuy();
int onString();
int onCatchall();
int onShowDarkness();
int onMiscDataNewer();
int onEditSubscreens();
int onSubsNewer();
bool zq255_quest_is_newer();                                // q255_store_zq.cpp
bool zq_edit_misc_newer(const char *tab, bool *shown);      // zq_record_editor.cpp
bool zq_edit_rules_newer(int category, bool *shown);        // zq_rules_editor.cpp

namespace zq_ui_255
{

// ---- a small window of buttons, like a 2.55 dialog's button row ---------------------------------

struct Choice
{
    const char *label;
    const char *help;           // shown beside the button; NULL: none
    int (*proc)();              // NULL: shown disabled (2.55 had it, this editor has no such function)
};

struct Chooser
{
    bool open, close, focus, esc_was_down;
    int picked;
    std::string title, note;
    std::vector<Choice> items;

    Chooser() : open(false), close(false), focus(false), esc_was_down(true), picked(-1) {}
};

inline Chooser &chooser()
{
    static Chooser c;
    return c;
}

inline void chooser_layer(void *)
{
    Chooser &c = chooser();

    if(!c.open)
        return;

    const float sc = zcui::scale();
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if(c.focus)
    {
        ImGui::SetNextWindowFocus();
        c.focus = false;
    }

    char wtitle[160];
    snprintf(wtitle, sizeof(wtitle), "%s###zqui255choice", c.title.c_str());
    bool open = true;

    if(ImGui::Begin(wtitle, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_AlwaysAutoResize))
    {
        if(!c.note.empty())
        {
            ImGui::PushTextWrapPos(420.0f * sc);
            ImGui::TextDisabled("%s", c.note.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
        }

        for(size_t i = 0; i < c.items.size(); ++i)
        {
            const Choice &ch = c.items[i];
            ImGui::PushID((int)i);

            if(!ch.proc)
                ImGui::BeginDisabled();

            if(ImGui::Button(ch.label, ImVec2(200.0f * sc, 0)))
            {
                c.picked = (int)i;
                c.close = true;
            }

            if(!ch.proc)
                ImGui::EndDisabled();

            if(ch.help && *ch.help)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", ch.help);
            }

            ImGui::PopID();
        }

        ImGui::Separator();

        if(ImGui::Button("Close", ImVec2(90.0f * sc, 0)))
            c.close = true;
    }

    ImGui::End();

    // Escape closes it: read from the key's own state, as ImGui's navigation may take the key
    // (an Escape held when the window opened counts only once it is let go)
    const bool esc = ImGui::GetIO().KeysData[ImGuiKey_Escape - ImGuiKey_NamedKey_BEGIN].Down;

    if(esc && !c.esc_was_down)
        c.close = true;

    c.esc_was_down = esc;

    if(!open)
        c.close = true;
}

// Shows the buttons and runs the one picked (after the window is gone). Without the UI layer
// (a headless run) the first entry runs, as the 2.53 menus would.
inline int choose(const char *title, const char *note, const std::vector<Choice> &items)
{
    int picked = -1;

    if(!zcui::available())
        picked = items.empty() ? -1 : 0;
    else
    {
        {
            zcui::Lock lock;
            Chooser &c = chooser();

            if(c.open)
                return 0;

            c.open = true;
            c.close = false;
            c.focus = true;
            c.esc_was_down = true;
            c.picked = -1;
            c.title = title;
            c.note = note ? note : "";
            c.items = items;
        }

        const int layer = zcui::add_layer(chooser_layer, NULL, 600);
        zcui::begin_session();

        for(;;)
        {
            zcui::wait_frame(100);
            bool done;
            {
                zcui::Lock lock;
                done = chooser().close;
            }

            if(done || !zcui::available())
                break;
        }

        zcui::end_session();
        zcui::remove_layer(layer);
        zcui::Lock lock;
        chooser().open = false;
        picked = chooser().picked;
    }

    if(picked >= 0 && picked < (int)items.size() && items[picked].proc)
        return items[picked].proc();

    return 0;
}

// ---- the rules: 2.55's Quest Options and its script rule dialogs --------------------------------

inline int rules_255_at(int category)
{
    bool shown = false;
    zq_edit_rules_newer(category, &shown);
    return 0;
}

inline int rules_255_first() { return rules_255_at(-1); }

// Quest > Options (2.55's QRDialog): its top row of buttons (Header, Pick Ruleset, Cheats, Pick
// Rule Templates, Search QRs, Copy / Load QR String, Map Count) and the rules themselves. A 2.53
// quest's rules are 2.53's seven rule dialogs.
inline int quest_options()
{
    std::vector<Choice> c;
    c.push_back(Choice { "Header", "title, author, password", onHeader });
    c.push_back(Choice { "Pick Ruleset", NULL, NULL });
    c.push_back(Choice { "Cheats", NULL, onCheats });
    c.push_back(Choice { "Pick Rule Templates", NULL, NULL });

    if(zq255_quest_is_newer())
    {
        c.push_back(Choice { "Search QRs", "every rule, by category", rules_255_first });
        c.push_back(Choice { "Copy QR String", NULL, NULL });
        c.push_back(Choice { "Load QR String", NULL, NULL });
    }
    else
    {
        c.push_back(Choice { "Animation", "rules", onAnimationRules });
        c.push_back(Choice { "Combos", "rules", onComboRules });
        c.push_back(Choice { "Items", "rules", onItemRules });
        c.push_back(Choice { "Enemies", "rules", onEnemyRules });
        c.push_back(Choice { "NES Fixes", "rules", onFixesRules });
        c.push_back(Choice { "Other", "rules", onMiscRules });
        c.push_back(Choice { "Backward compatibility", "rules", onCompatRules });
    }

    c.push_back(Choice { "Map Count...", NULL, onMapCount });
    return choose("Quest Options", zq255_quest_is_newer() ? "The quest's rules are in Search QRs, by 2.55's categories." : NULL, c);
}

// ZScript > Quest Script Settings (2.55's ScriptRulesDialog): the ZScript rule categories
inline int script_rules() { return rules_255_at(10); }

// ZScript > Compiler Settings
inline int compiler_settings() { return rules_255_at(15); }

// ---- the misc data editor's tabs ---------------------------------------------------------------

inline int misc_tab(const char *tab)
{
    bool shown = false;

    if(zq255_quest_is_newer())
        zq_edit_misc_newer(tab, &shown);

    return shown ? 0 : onMiscDataNewer();
}

inline int misc_sfx() { return misc_tab("Misc sounds"); }
inline int misc_sprites() { return misc_tab("Misc sprites"); }
inline int bottle_types() { return misc_tab("Bottle types"); }
inline int bottle_shops() { return misc_tab("Bottle shops"); }

// ---- dialogs this editor splits: the 2.53 part and the 2.55 part --------------------------------

// Screen > Screen Data (F9): 2.55's dialog holds the screen's flags, enemies' settings and its
// script, InitD, music, lens and the rest of what only 2.55 holds.
inline int screen_data()
{
    if(!zq255_quest_is_newer())
        return onScrData();

    std::vector<Choice> c;
    c.push_back(Choice { "Screen Data", "flags, enemy settings, timed warps, ...", onScrData });
    c.push_back(Choice { "Script and 2.55 settings", "script, InitD, music, lens, notes, ...", onScrDataNewer });
    return choose("Screen Data", NULL, c);
}

// Quest > Strings: 2.55's string editor holds the text and its margins, portrait, shadow, layer
inline int strings()
{
    if(!zq255_quest_is_newer())
        return onStrings();

    std::vector<Choice> c;
    c.push_back(Choice { "Strings", "text, next string, frame, sound, ...", onStrings });
    c.push_back(Choice { "String Data", "margins, portrait, shadow, layer, ...", onStringsNewer });
    return choose("Strings", NULL, c);
}

// Screen > Room Data (2.55's room dialog: room type, its argument, guy and string)
inline int room_data()
{
    std::vector<Choice> c;
    c.push_back(Choice { "Room Type", NULL, onRType });
    c.push_back(Choice { "Catch All", "the room type's argument", onCatchall });
    c.push_back(Choice { "Guy", NULL, onGuy });
    c.push_back(Choice { "String", NULL, onString });
    return choose("Room Data", NULL, c);
}

// Quest > Misc Data > Subscreens: 2.55's subscreen editor holds the pages and widgets and the
// subscreen's own settings (flags, page buttons and transitions)
inline int subscreens()
{
    if(!zq255_quest_is_newer())
        return onEditSubscreens();

    std::vector<Choice> c;
    c.push_back(Choice { "Subscreens", "pages and widgets", onEditSubscreens });
    c.push_back(Choice { "Subscreen Data", "every setting, page and widget field", onSubsNewer });
    return choose("Subscreens", NULL, c);
}

// The actions, registered with the profiles (zq_ui_add_action)
inline void add_actions()
{
    zq_ui_add_action("@qr255_options", quest_options, 0);
    zq_ui_add_action("@qr255_script", script_rules, 0x255);
    zq_ui_add_action("@qr255_compiler", compiler_settings, 0x255);
    zq_ui_add_action("@misc255_sfx", misc_sfx, 0x255);
    zq_ui_add_action("@misc255_sprites", misc_sprites, 0x255);
    zq_ui_add_action("@misc255_bottles", bottle_types, 0x255);
    zq_ui_add_action("@misc255_bottle_shops", bottle_shops, 0x255);
    zq_ui_add_action("@screen255_data", screen_data, 0);
    zq_ui_add_action("@strings255", strings, 0);
    zq_ui_add_action("@room255_data", room_data, 0);
    zq_ui_add_action("@subscreens255", subscreens, 0);
    zq_ui_add_action("@zq255_darkness", onShowDarkness, 0);
}

} // namespace zq_ui_255

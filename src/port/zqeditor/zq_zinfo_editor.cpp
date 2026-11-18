// ZQuest's ZInfo editor for 2.55 and 3.0 Nightly quests (Dear ImGui): the quest's own names
// and help for upstream's lists (the ZINF section): item classes, combo types, map flags,
// weapons, counters, and in 3.0 enemy types and level items. A port of upstream's ZInfoDialog
// (zq/dialog/zinfodlg.cpp, 3.0; 2.55.17's has the first five tabs): every entry either keeps
// upstream's default ("Default" ticked) or has the quest's own text, and each kind can be reset
// to the defaults as a whole.
//
// The edits go into ZQuest's store (zq255.zinfo, zqw_zinfo.cpp) on OK; the ZINF writer saves
// them in the quest's own ZINF version, and the Save As converters (zqn_255_zinfo.cpp) start
// from that. A kind the quest's ZINF version has no place for is shown but can't be edited, and
// a quest without a ZINF section (its names in a .zinfo file beside it) isn't offered.
// ZQuest's own lists (the record editor's, the combo editor's) show the names through
// zq_zinfo_name.
//
// Like the other newer-quest editors, the window is a UI layer (the platform thread, UI lock
// held) shown during a modal session the game thread holds open.
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zcport/ui.h"
#include "zq_imgui_fit.h"
#include "zq_q255.h"
#include "zq_q255_store.h"
#include "zq_item_names.h"
#include "zq_combo_names.h"

#include <imgui.h>
#include <imgui_internal.h>

extern bool saved;
extern int q255_header_version[3];

namespace
{

// One tab: a kind of upstream's lists.
struct Kind
{
    int kind;               // ZqZinfoKind
    const char *tab;        // upstream's tab name
    const char *what;       // "Itemclass", for the labels
    int from_version;       // the ZINF version that first holds it
    bool help, abbr;        // has help text / an abbreviation (level items)
    int first;              // upstream's list starts here (1: entry 0 is "(None)")
};

const Kind g_kinds[] =
{
    { ZQZ_ITEMCLASS, "Itemclass", "Itemclass", 0, true, false, 0 },
    { ZQZ_COMBOTYPE, "Combo Type", "Combo Type", 1, true, false, 1 },
    { ZQZ_MAPFLAG, "Mapflags", "Mapflag", 1, true, false, 1 },
    { ZQZ_WEAPON, "Weapons", "Weapon", 3, false, false, 1 },
    { ZQZ_COUNTER, "Counters", "Counter", 2, false, false, 0 },
    { ZQZ_ENEMYCLASS, "Enemy Types", "Enemy Type", 4, false, false, 1 },
    { ZQZ_LEVELITEM, "Level Items", "Level Item", 5, true, true, 0 }
};
const int KINDS = (int)(sizeof(g_kinds) / sizeof(g_kinds[0]));

// upstream's level items (zinfo.cpp litem_default_*)
const char *const g_litem_names[16] =
{
    "McGuffin", "Map", "Compass", "Boss Killed", "Boss Key", "Custom 01", "Custom 02", "Custom 03",
    "Custom 04", "Custom 05", "Custom 06", "Custom 07", "Custom 08", "Custom 09", "Custom 10", "Custom 11"
};
const char *const g_litem_abbrs[16] =
{
    "McG", "Map", "Comp", "Boss", "BKey", "C1", "C2", "C3", "C4", "C5", "C6", "C7", "C8", "C9", "C10", "C11"
};
const char *const g_litem_helps[5] =
{
    "The main collectable of the game.", "The map, to help find your way.", "The compass, to guide you to your destination.",
    "The boss has been defeated.", "The big key to open the way."
};

struct ZinfoEd
{
    bool open, close, want_ok, focus;
    bool is30;
    Zq255Store::Zinfo z;        // the edit (the quest's copy until OK)
    int sel[ZQZ_KINDS];         // the entry picked on each tab
    char filter[64];

    ZinfoEd() : open(false), close(false), want_ok(false), focus(false), is30(false)
    {
        memset(sel, 0, sizeof(sel));
        filter[0] = 0;
    }
};

ZinfoEd g_ze;
std::string g_unticked;     // the field whose Default was just unticked (empty until typed in)

float sc() { return zcui::scale(); }


// How many entries upstream's list of this kind has in the quest's generation (what its ZQuest
// writes, zqn_255_zinfo.cpp): the most this editor lets the quest's section hold.
int kind_count(int kind)
{
    switch(kind)
    {
    case ZQZ_ITEMCLASS: return 512;
    case ZQZ_COMBOTYPE: return q30_ctype_count(g_ze.is30);
    case ZQZ_MAPFLAG: return 256;
    case ZQZ_WEAPON: return 146;
    case ZQZ_COUNTER: return q30_counter_count();
    case ZQZ_ENEMYCLASS: return g_ze.is30 ? 73 : 0;
    case ZQZ_LEVELITEM: return g_ze.is30 ? 16 : 0;
    }

    return 0;
}

// upstream's default name ("" for none; names starting '-' are unused slots)
std::string default_name(int kind, int i)
{
    zq_zinfo_defaults_only(true);
    const char *n = "";

    switch(kind)
    {
    case ZQZ_ITEMCLASS: n = q30_itemclass_name(i); break;
    case ZQZ_COMBOTYPE: n = q30_ctype_name(i); break;
    case ZQZ_MAPFLAG: n = q30_mflag_name(i); break;
    case ZQZ_WEAPON: n = q30_weapon_type_name(i); break;
    case ZQZ_COUNTER: n = q30_counter_name(i); break;
    case ZQZ_ENEMYCLASS: n = q30_enemyclass_name(i); break;
    case ZQZ_LEVELITEM: n = i >= 0 && i < 16 ? g_litem_names[i] : ""; break;
    }

    std::string s = n ? n : "";
    zq_zinfo_defaults_only(false);
    return s;
}

std::string default_help(int kind, int i)
{
    zq_zinfo_defaults_only(true);
    const char *h = "";

    switch(kind)
    {
    case ZQZ_ITEMCLASS: h = q30_itemclass_help(i); break;
    case ZQZ_COMBOTYPE: h = q30_ctype_help(i); break;
    case ZQZ_MAPFLAG: h = q30_mflag_help(i); break;
    case ZQZ_LEVELITEM: h = i >= 0 && i < 5 ? g_litem_helps[i] : ""; break;
    }

    std::string s = h ? h : "";
    zq_zinfo_defaults_only(false);
    return s;
}

// upstream's isUsable...: entries whose default name starts '-' are hidden (counters and level
// items: all shown)
bool usable(int kind, int i)
{
    if(kind == ZQZ_COUNTER || kind == ZQZ_LEVELITEM)
        return true;

    const std::string d = default_name(kind, i);
    return !d.empty() && d[0] != '-';
}

// what the quest's own text is ("" for the default): the reader keeps characters up to a NUL
std::string own(const std::string &s)
{
    const size_t z = s.find('\0');
    return z == std::string::npos ? s : s.substr(0, z);
}

const Zq255Store::ZinfoEntry *entry(int kind, int i)
{
    const std::vector<Zq255Store::ZinfoEntry> &k = g_ze.z.kinds[kind];
    return i >= 0 && (size_t)i < k.size() ? &k[i] : NULL;
}

// the entry to change, the section's count grown to take it where needed
Zq255Store::ZinfoEntry *entry_for_edit(int kind, int i)
{
    std::vector<Zq255Store::ZinfoEntry> &k = g_ze.z.kinds[kind];

    if(i < 0 || i >= kind_count(kind))
        return NULL;

    if((size_t)i >= k.size())
        k.resize((size_t)i + 1);

    return &k[i];
}

// upstream's list text: "<name> (<number>)", the quest's name where it has one (marked *)
std::string list_label(int kind, int i)
{
    const Zq255Store::ZinfoEntry *e = entry(kind, i);
    std::string n = e ? own(e->name) : std::string();
    const bool custom = !n.empty() || (e && (!own(e->help).empty() || !own(e->abbr).empty()));

    if(n.empty())
        n = default_name(kind, i);

    char b[400];

    if(n.empty())
        snprintf(b, sizeof(b), "%s%03d (%03d)%s", kind == ZQZ_LEVELITEM ? "li" : "zz", i, i, custom ? "  *" : "");
    else
        snprintf(b, sizeof(b), "%s (%03d)%s", n.c_str(), i, custom ? "  *" : "");

    return b;
}

// a text the quest may set: "Default" ticked keeps upstream's; the field is the quest's own
void own_text(const char *label, const char *id, std::string &value, size_t max, bool multiline, const std::string &dflt)
{
    std::string cur = own(value);
    bool is_default = cur.empty();
    const bool unticked = g_unticked == std::string(label) + id;
    ImGui::PushID(id);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    bool box = is_default && !unticked;

    if(ImGui::Checkbox("Default", &box))
    {
        if(box)
        {
            value.clear();
            g_unticked.clear();
        }
        else
            g_unticked = std::string(label) + id;
    }

    ImGui::BeginDisabled(box);
    std::vector<char> buf(max + 1, 0);
    memcpy(&buf[0], cur.data(), cur.size() < max ? cur.size() : max);
    bool changed;

    if(multiline)
        changed = ImGui::InputTextMultiline("##t", &buf[0], buf.size(), ImVec2(-1, 90 * sc()));
    else
    {
        ImGui::SetNextItemWidth(-1);
        changed = ImGui::InputText("##t", &buf[0], buf.size());
    }

    if(changed)
        value = &buf[0];

    ImGui::EndDisabled();

    if(box && !dflt.empty())
        ImGui::TextDisabled("Default: %s", dflt.c_str());

    ImGui::PopID();
}

// "Reset all <kind> ..." with upstream's confirmation (field 0 names, 1 help, 2 abbreviations)
void reset_button(const char *button, const char *popup, const char *question, int kind, int field)
{
    if(ImGui::Button(button))
        ImGui::OpenPopup(popup);

    if(ImGui::BeginPopupModal(popup, NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted(question);

        if(ImGui::Button("Yes", ImVec2(90 * sc(), 0)))
        {
            std::vector<Zq255Store::ZinfoEntry> &k = g_ze.z.kinds[kind];

            for(size_t q = 0; q < k.size(); ++q)
                (field == 0 ? k[q].name : field == 1 ? k[q].help : k[q].abbr).clear();

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if(ImGui::Button("No", ImVec2(90 * sc(), 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void kind_tab(const Kind &k)
{
    ZinfoEd &e = g_ze;

    if(e.z.version < k.from_version || kind_count(k.kind) == 0)
    {
        ImGui::TextWrapped("This quest's ZInfo (format %d) has no %s names: they came with format %d. They can't be set "
                           "in a quest saved in this version.", e.z.version, k.what, k.from_version);

        // upstream's ZQuest names the scripted map flags of these quests as 2.55 did
        if(k.kind == ZQZ_MAPFLAG)
        {
            bool any = false;

            for(int f = 0; f < 256; ++f)
                if(zq_zinfo_old_mapflag(f))
                {
                    if(!any)
                        ImGui::TextWrapped("ZQuest's lists name these map flags as 2.55 did, as upstream's editor does for "
                                           "this quest:");

                    any = true;
                    ImGui::BulletText("%d: %s", f, zq_zinfo_name(ZQZ_MAPFLAG, f));
                }
        }

        return;
    }

    int &sel = e.sel[k.kind];
    const int n = kind_count(k.kind);

    if(sel < k.first || sel >= n || !usable(k.kind, sel))
    {
        sel = k.first;

        while(sel < n - 1 && !usable(k.kind, sel))
            ++sel;
    }

    // the entries on the left, the picked one on the right
    std::string f = e.filter;

    for(size_t c = 0; c < f.size(); ++c)
        f[c] = (char)tolower((unsigned char)f[c]);

    std::vector<int> shown;

    for(int i = k.first; i < n; ++i)
    {
        if(!usable(k.kind, i))
            continue;

        if(!f.empty())
        {
            std::string l = list_label(k.kind, i);

            for(size_t c = 0; c < l.size(); ++c)
                l[c] = (char)tolower((unsigned char)l[c]);

            if(l.find(f) == std::string::npos)
                continue;
        }

        shown.push_back(i);
    }

    // the list takes up to 300 points, less in a narrow window
    float list_w = ImGui::GetContentRegionAvail().x * 0.45f;
    list_w = list_w > 300 * sc() ? 300 * sc() : list_w < 120 ? 120 : list_w;

    if(ImGui::BeginChild("list", ImVec2(list_w, 0), ImGuiChildFlags_Borders))
    {
        ImGuiListClipper clip;
        clip.Begin((int)shown.size());

        while(clip.Step())
            for(int r = clip.DisplayStart; r < clip.DisplayEnd; ++r)
            {
                const int i = shown[r];
                ImGui::PushID(i);

                if(ImGui::Selectable(list_label(k.kind, i).c_str(), i == sel))
                {
                    sel = i;
                    g_unticked.clear();
                }

                ImGui::PopID();
            }
    }

    ImGui::EndChild();
    ImGui::SameLine();

    if(ImGui::BeginChild("entry", ImVec2(0, 0)))
    {
        ImGui::Text("%s %d", k.what, sel);
        ImGui::Separator();
        const Zq255Store::ZinfoEntry *have = entry(k.kind, sel);
        Zq255Store::ZinfoEntry cur = have ? *have : Zq255Store::ZinfoEntry();
        const Zq255Store::ZinfoEntry before = cur;
        char label[96];
        snprintf(label, sizeof(label), "%s Name:", k.what);
        own_text(label, "name", cur.name, 255, false, default_name(k.kind, sel));

        if(k.abbr)
        {
            snprintf(label, sizeof(label), "%s Abbreviation:", k.what);
            own_text(label, "abbr", cur.abbr, 4, false, sel >= 0 && sel < 16 ? g_litem_abbrs[sel] : "");
        }

        if(k.help)
        {
            snprintf(label, sizeof(label), "%s Help Text:", k.what);
            own_text(label, "help", cur.help, 65535, true, std::string());

            // the help as ZQuest shows it: the quest's own, or upstream's
            const std::string shownhelp = own(cur.help).empty() ? default_help(k.kind, sel) : own(cur.help);

            if(ImGui::BeginChild("helpview", ImVec2(0, 110 * sc()), ImGuiChildFlags_Borders))
                ImGui::TextWrapped("%s", shownhelp.c_str());

            ImGui::EndChild();
        }

        if(cur.name != before.name || cur.help != before.help || cur.abbr != before.abbr)
        {
            if(Zq255Store::ZinfoEntry *w = entry_for_edit(k.kind, sel))
                *w = cur;
        }

        ImGui::Separator();
        ImGui::Text("Reset all %s...", k.what);
        char pop[96], q[160];
        snprintf(pop, sizeof(pop), "Reset %s names?", k.what);
        snprintf(q, sizeof(q), "This will clear ALL %s names to default!", k.what);
        reset_button("Names", pop, q, k.kind, 0);

        if(k.abbr)
        {
            ImGui::SameLine();
            snprintf(pop, sizeof(pop), "Reset %s abbreviations?", k.what);
            snprintf(q, sizeof(q), "This will clear ALL %s abbreviations to default!", k.what);
            reset_button("Abbreviations", pop, q, k.kind, 2);
        }

        if(k.help)
        {
            ImGui::SameLine();
            snprintf(pop, sizeof(pop), "Reset %s help text?", k.what);
            snprintf(q, sizeof(q), "This will clear ALL %s helptext to default!", k.what);
            reset_button("Help Text", pop, q, k.kind, 1);
        }
    }

    ImGui::EndChild();
}

void editor_layer(void *)
{
    ZinfoEd &e = g_ze;

    if(!e.open)
        return;

    zq_fit_next_window(820 * sc(), 560 * sc());

    if(e.focus)
    {
        ImGui::SetNextWindowFocus();
        e.focus = false;
    }

    char title[160];
    snprintf(title, sizeof(title), "ZInfo Editor (%s, ZInfo format %d)###zqzinfo", e.is30 ? "3.0 Nightly" : "2.55", e.z.version);
    bool open = true;

    if(ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        zq_keep_window_inside(820 * sc(), 560 * sc());
        ImGui::TextDisabled("Untick 'Default' to give an entry the quest's own text; ZQuest's lists show it (* in the list).");
        ImGui::SetNextItemWidth(260 * sc());
        ImGui::InputTextWithHint("##filter", "Search the list", e.filter, sizeof(e.filter));
        const float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

        if(ImGui::BeginChild("body", ImVec2(0, -bottom)))
        {
            if(ImGui::BeginTabBar("tabs"))
            {
                for(int t = 0; t < KINDS; ++t)
                {
                    // 2.55.17's dialog has the first five tabs only
                    if(!e.is30 && g_kinds[t].from_version >= 4)
                        continue;

                    if(ImGui::BeginTabItem(g_kinds[t].tab))
                    {
                        ImGui::PushID(t);
                        kind_tab(g_kinds[t]);
                        ImGui::PopID();
                        ImGui::EndTabItem();
                    }
                }

                ImGui::EndTabBar();
            }
        }

        ImGui::EndChild();

        if(ImGui::Button("OK", ImVec2(90 * sc(), 0)))
            e.want_ok = true;

        ImGui::SameLine();

        if(ImGui::Button("Cancel", ImVec2(90 * sc(), 0)))
            e.close = true;

        if(!ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) &&
           ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            e.close = true;
    }

    ImGui::End();

    if(!open)
        e.close = true;
}

bool same_zinfo(const Zq255Store::Zinfo &a, const Zq255Store::Zinfo &b)
{
    if(a.present != b.present || a.version != b.version)
        return false;

    for(int k = 0; k < ZQZ_KINDS; ++k)
    {
        if(a.kinds[k].size() != b.kinds[k].size())
            return false;

        for(size_t q = 0; q < a.kinds[k].size(); ++q)
            if(a.kinds[k][q].name != b.kinds[k][q].name || a.kinds[k][q].help != b.kinds[k][q].help ||
               a.kinds[k][q].abbr != b.kinds[k][q].abbr)
                return false;
    }

    return true;
}

// The counts as the edit left them, trimmed back to the quest's own where the entries added past
// them stayed empty (a name set and cleared again changes nothing).
void trim_to_original(Zq255Store::Zinfo &z, const Zq255Store::Zinfo &orig)
{
    for(int k = 0; k < ZQZ_KINDS; ++k)
    {
        std::vector<Zq255Store::ZinfoEntry> &v = z.kinds[k];
        const size_t keep = orig.kinds[k].size();

        while(v.size() > keep && v.back().name.empty() && v.back().help.empty() && v.back().abbr.empty())
            v.pop_back();
    }
}

} // namespace

// Quest > ZInfo (2.55 and 3.0 Nightly quests). *shown: false when the open quest has no ZINF
// section of its own (the caller says why). True when OK was pressed.
bool zq_edit_zinfo_newer(bool *shown)
{
    *shown = zq255_quest_is_newer() && zq255.zinfo.present;

    if(!*shown || !zcui::available())
        return false;

    {
        zcui::Lock lock;

        if(g_ze.open)
            return false;

        g_ze.open = true;
        g_ze.close = g_ze.want_ok = false;
        g_ze.focus = true;
        g_ze.is30 = q255_header_version[0] >= 3;
        g_ze.z = zq255.zinfo;
    }

    const int layer = zcui::add_layer(editor_layer, NULL, 600);
    zcui::begin_session();
    bool accepted = false;

    for(;;)
    {
        zcui::wait_frame(100);
        bool done, ok;
        {
            zcui::Lock lock;
            done = g_ze.close;
            ok = g_ze.want_ok;
            g_ze.want_ok = false;
        }

        if(ok)
        {
            zcui::Lock lock;
            Zq255Store::Zinfo z = g_ze.z;
            trim_to_original(z, zq255.zinfo);

            if(!same_zinfo(z, zq255.zinfo))
            {
                zq255.zinfo = z;
                saved = false;
            }

            accepted = true;
            break;
        }

        if(done || !zcui::available())
            break;
    }

    zcui::end_session();
    zcui::remove_layer(layer);
    zcui::Lock lock;
    g_ze.open = false;
    return accepted;
}

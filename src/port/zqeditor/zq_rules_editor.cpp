// ZQuest's quest rules editor for 2.55 and 3.0 Nightly quests (Dear ImGui): every rule the
// quest's version has (800 bits against 2.53's 160), by upstream's categories, with upstream's
// names and help (zq_rule_names.cpp, generated from its rule dialogs), a search across all of
// them, and the compiler settings a version 18 rule section holds. 2.53 and older quests keep
// the 2.53 rule dialogs.
//
// The rules show as upstream's loader has them: its readrules turns compatibility rules on for
// quests saved before the rule existed (by the quest's build and the section's compat rule
// version), whatever the stored bit says. Those rules can't be changed in the file as it is,
// so they are greyed out; "Update compatibility rules" does what upstream's own editor does on
// every save (stores the rules as loaded, with its latest compat rule version), after which
// they can be. Everything else goes into the stored bits; the section's other bytes stay as
// they are (zq255_rules_put, zqw_basic.cpp).
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <allegro.h>

#include <algorithm>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zcport/ui.h"
#include "zq_q255_store.h"
#include "zq_rule_names.h"

#include <imgui.h>

extern bool saved;
extern int q255_header_version[3];
extern zquestheader header;

namespace
{

enum { RULE_BYTES = 100, TAB_UNLISTED = 100, TAB_COMPILER = 101 };

struct RulesEditor
{
    bool open, close, want_ok, focus;
    bool is30;
    int start_tab;              // the category to show first; -1: the first
    ZqRuleSection orig, cur;
    ZqRuleQuest quest;
    char title[66], author[66];
    byte eff[RULE_BYTES];       // the rules as upstream's loader has them
    byte locked[RULE_BYTES];    // rules the loader decides, whatever the stored bit says
    byte listed[RULE_BYTES];    // rules some list names
    char search[128];
    bool only_on;
    std::string error, note;

    RulesEditor() : open(false), close(false), want_ok(false), focus(false), is30(false), start_tab(-1), only_on(false)
    {
        search[0] = 0;
    }
};

RulesEditor g_ru;

bool bit(const byte *b, int n)
{
    return (b[n >> 3] >> (n & 7)) & 1;
}

void flip(byte *b, int n)
{
    b[n >> 3] ^= (byte)(1 << (n & 7));
}

// What upstream's loader makes of these stored bits.
void effective(const byte *stored, size_t total, dword compat, byte *out)
{
    memset(out, 0, RULE_BYTES);
    memcpy(out, stored, total);
    ZqRuleQuest q = g_ru.quest;
    q.compat = compat;
    zq_rules_upstream(g_ru.is30, out, q);
}

// The loader's view and which rules it decides (game or UI thread, under the lock).
void recompute(RulesEditor &e)
{
    effective(e.cur.rules, e.cur.total, e.cur.compat, e.eff);
    memset(e.locked, 0, sizeof(e.locked));

    for(int n = 0; n < RULE_BYTES * 8; ++n)
    {
        if(!bit(e.listed, n))
            continue;

        if(n >= (int)e.cur.total * 8)
        {
            flip(e.locked, n);
            continue;
        }

        byte t[RULE_BYTES], got[RULE_BYTES];
        memcpy(t, e.cur.rules, RULE_BYTES);
        flip(t, n);
        effective(t, e.cur.total, e.cur.compat, got);

        if(bit(got, n) == bit(e.eff, n))
            flip(e.locked, n);
    }
}

bool contains_nocase(const char *hay, const char *needle)
{
    if(!*needle)
        return true;

    for(; *hay; ++hay)
    {
        const char *h = hay, *n = needle;

        while(*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n))
        {
            ++h;
            ++n;
        }

        if(!*n)
            return true;
    }

    return false;
}

const char *rule_name(int n)
{
    for(int k = 0; k < zq_rule_count(g_ru.is30); ++k)
        if(zq_rule_at(g_ru.is30, k).rule == n)
            return zq_rule_at(g_ru.is30, k).name;

    return NULL;
}

void rule_tooltip(RulesEditor &e, const ZqRuleInfo &r)
{
    if(!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        return;

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
    ImGui::TextUnformatted(r.name);
    ImGui::TextDisabled("Rule %d, %s", r.rule, zq_rule_category(e.is30, r.category));

    if(r.help && *r.help)
    {
        ImGui::Separator();
        ImGui::TextUnformatted(r.help);
    }

    for(int k = 0; k < 4 && r.related[k] >= 0; ++k)
    {
        const char *nm = rule_name(r.related[k]);
        ImGui::TextDisabled("See also: %s (%s)", nm ? nm : "rule", bit(e.eff, r.related[k]) ? "on" : "off");
    }

    if(r.rule >= (int)e.cur.total * 8)
    {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "This quest's rule format (%d) stores only the first %d rules.",
                           e.cur.version, (int)e.cur.total * 8);
    }
    else if(bit(e.locked, r.rule))
    {
        // would the rule be free with the compat rules brought up to date?
        bool by_compat = false;

        if(e.cur.version > 16 && e.cur.compat < zq_rules_latest_compat(e.is30))
        {
            byte up[RULE_BYTES], a[RULE_BYTES], b[RULE_BYTES];
            const dword latest = zq_rules_latest_compat(e.is30);
            memset(up, 0, sizeof(up));
            memcpy(up, e.eff, e.cur.total);
            effective(up, e.cur.total, latest, a);
            flip(up, r.rule);
            effective(up, e.cur.total, latest, b);
            by_compat = bit(a, r.rule) != bit(b, r.rule);
        }

        ImGui::Separator();

        if(by_compat)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f),
                               "Upstream's loader sets this rule for quests saved before compat rule version %u "
                               "(this one has %u), whatever the file says. Update compatibility rules to change it.",
                               (unsigned)zq_rules_latest_compat(e.is30), (unsigned)e.cur.compat);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f),
                               "Upstream's loader decides this rule for quests made in %d.%d.%d (build %d), whatever "
                               "the file says.",
                               e.quest.major, e.quest.minor, e.quest.patch, e.quest.build);
    }

    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void rule_row(RulesEditor &e, const ZqRuleInfo &r, bool show_category)
{
    const int n = r.rule;
    bool on = bit(e.eff, n);

    if(e.only_on && !on)
        return;

    ImGui::PushID(n * 32 + r.category);
    const bool lock = bit(e.locked, n);

    if(lock)
        ImGui::BeginDisabled();

    if(ImGui::Checkbox(r.name, &on))
    {
        flip(e.cur.rules, n);
        recompute(e);
    }

    if(lock)
        ImGui::EndDisabled();

    rule_tooltip(e, r);

    if(show_category)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", zq_rule_category(e.is30, r.category));
    }

    if(n < (int)e.cur.total * 8 && bit(e.cur.rules, n) != bit(e.orig.rules, n))
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.0f, 1.0f), "(changed)");
    }

    ImGui::PopID();
}

// Rules no list names that the quest has on: kept as they are.
void unlisted_tab(RulesEditor &e)
{
    ImGui::TextWrapped("Rule bits set in this quest that no list of upstream's rule dialogs names (old or "
                       "retired rules). They are kept as they are.");
    ImGui::Separator();

    for(int n = 0; n < (int)e.cur.total * 8; ++n)
        if(!bit(e.listed, n) && bit(e.cur.rules, n))
            ImGui::BulletText("Rule %d", n);
}

void compiler_tab(RulesEditor &e)
{
    ImGui::TextWrapped("The ZScript compiler settings this quest stores with its rules. The compiler options "
                       "stored as rules are under the Compiler Settings tab.");
    ImGui::Separator();
    std::vector<std::pair<word, long> > &s = e.cur.settings;
    int at = -1;

    for(size_t q = 0; q < s.size(); ++q)
        if(s[q].first == 0)
            at = (int)q;

    static const char *const modes[3] = { "Auto", "Off", "On" };
    int mode = at < 0 ? 0 : s[at].second == 0 ? 1 : s[at].second == 1 ? 2 : 0;
    ImGui::SetNextItemWidth(140.0f * zcui::scale());

    if(ImGui::Combo("Default Static Script Members", &mode, modes, 3))
    {
        if(mode == 0)
        {
            if(at >= 0)
                s.erase(s.begin() + at);
        }
        else
        {
            if(at < 0)
            {
                s.push_back(std::make_pair((word)0, 0L));
                at = (int)s.size() - 1;
            }

            s[at].second = mode == 1 ? 0 : 1;

            if(e.cur.setting_version == 0)
                e.cur.setting_version = 1;
        }
    }

    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Auto: picks On for now.\nOff: script members aren't static unless marked 'static'.\n"
                          "On: script members are static unless marked 'nonstatic'.");

    for(size_t q = 0; q < s.size(); ++q)
    {
        if(s[q].first == 0)
            continue;

        int v = (int)s[q].second;
        char label[64];
        snprintf(label, sizeof(label), "Setting %u", (unsigned)s[q].first);
        ImGui::SetNextItemWidth(140.0f * zcui::scale());

        if(ImGui::InputInt(label, &v))
            s[q].second = v;
    }
}

void editor_layer(void *)
{
    RulesEditor &e = g_ru;

    if(!e.open)
        return;

    const float sc = zcui::scale();
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(820 * sc, 640 * sc), ImGuiCond_Appearing);

    if(e.focus)
    {
        ImGui::SetNextWindowFocus();
        e.focus = false;
    }

    char wtitle[160];
    snprintf(wtitle, sizeof(wtitle), "Quest Rules (%s, rule format %d%s)###zqrules", e.is30 ? "3.0 Nightly" : "2.55",
             e.cur.version, e.cur.version > 16 ? "" : ", no compat rule version");
    bool open = true;

    if(ImGui::Begin(wtitle, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::SetNextItemWidth(260.0f * sc);
        ImGui::InputTextWithHint("##search", "Search names and help", e.search, sizeof(e.search));
        ImGui::SameLine();
        ImGui::Checkbox("Only rules that are on", &e.only_on);

        if(e.cur.version > 16)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Compat rule version %u", (unsigned)e.cur.compat);

            if(e.cur.compat < zq_rules_latest_compat(e.is30))
            {
                ImGui::SameLine();

                if(ImGui::SmallButton("Update compatibility rules"))
                {
                    byte now[RULE_BYTES];
                    effective(e.cur.rules, e.cur.total, e.cur.compat, now);
                    memcpy(e.cur.rules, now, e.cur.total);
                    e.cur.compat = zq_rules_latest_compat(e.is30);
                    recompute(e);
                    e.note = memcmp(now, e.eff, e.cur.total) == 0
                           ? "The rules are stored as they load now, with compat rule version " +
                                 std::to_string((unsigned long)e.cur.compat) + "."
                           : "Updated, but some rules load differently now; check them.";
                }

                if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("What upstream's own editor does on every save: the rules are stored as they "
                                      "load, with its latest compat rule version (%u), so the greyed out rules can be "
                                      "changed. The quest stays in its version.",
                                      (unsigned)zq_rules_latest_compat(e.is30));
            }
        }

        const float bottom = ImGui::GetFrameHeightWithSpacing() * (e.error.empty() && e.note.empty() ? 1 : 2) +
                             ImGui::GetStyle().ItemSpacing.y;

        if(ImGui::BeginChild("body", ImVec2(0, -bottom)))
        {
            const int count = zq_rule_count(e.is30);

            if(e.search[0])
            {
                // one list across the categories, sorted by name
                std::vector<int> hits;

                for(int k = 0; k < count; ++k)
                {
                    const ZqRuleInfo &r = zq_rule_at(e.is30, k);

                    if(contains_nocase(r.name, e.search) || (r.help && contains_nocase(r.help, e.search)))
                        hits.push_back(k);
                }

                std::stable_sort(hits.begin(), hits.end(), [&](int a, int b) {
                    return strcmp(zq_rule_at(e.is30, a).name, zq_rule_at(e.is30, b).name) < 0;
                });

                if(ImGui::BeginChild("hits"))
                {
                    ImGui::TextDisabled("%d rules", (int)hits.size());

                    for(size_t h = 0; h < hits.size(); ++h)
                        rule_row(e, zq_rule_at(e.is30, hits[h]), true);
                }

                ImGui::EndChild();
            }
            else if(ImGui::BeginTabBar("cats", ImGuiTabBarFlags_FittingPolicyScroll))
            {
                bool unlisted = false;

                for(int n = 0; n < (int)e.cur.total * 8 && !unlisted; ++n)
                    unlisted = !bit(e.listed, n) && bit(e.cur.rules, n);

                const int start = e.start_tab;
                e.start_tab = -2;

                for(int cat = 0; cat < ZQ_RULE_CATEGORIES + 2; ++cat)
                {
                    const int tab = cat < ZQ_RULE_CATEGORIES ? cat : cat == ZQ_RULE_CATEGORIES ? TAB_UNLISTED : TAB_COMPILER;

                    if((tab == TAB_UNLISTED && !unlisted) || (tab == TAB_COMPILER && !e.cur.has_settings))
                        continue;

                    int on = 0, total = 0;

                    for(int k = 0; k < count && tab < ZQ_RULE_CATEGORIES; ++k)
                        if(zq_rule_at(e.is30, k).category == tab)
                        {
                            ++total;
                            on += bit(e.eff, zq_rule_at(e.is30, k).rule) ? 1 : 0;
                        }

                    if(tab < ZQ_RULE_CATEGORIES && !total)
                        continue;

                    char label[80];

                    if(tab == TAB_UNLISTED)
                        snprintf(label, sizeof(label), "Not listed###tabun");
                    else if(tab == TAB_COMPILER)
                        snprintf(label, sizeof(label), "Script compiler###tabcs");
                    else
                        snprintf(label, sizeof(label), "%s (%d)###tab%d", zq_rule_category(e.is30, tab), on, tab);

                    if(ImGui::BeginTabItem(label, NULL, start == tab ? ImGuiTabItemFlags_SetSelected : 0))
                    {
                        if(ImGui::BeginChild("tab"))
                        {
                            if(tab == TAB_UNLISTED)
                                unlisted_tab(e);
                            else if(tab == TAB_COMPILER)
                                compiler_tab(e);
                            else
                            {
                                // the dialogs list each category by name
                                std::vector<int> rows;

                                for(int k = 0; k < count; ++k)
                                    if(zq_rule_at(e.is30, k).category == tab)
                                        rows.push_back(k);

                                std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
                                    return strcmp(zq_rule_at(e.is30, a).name, zq_rule_at(e.is30, b).name) < 0;
                                });

                                for(size_t q = 0; q < rows.size(); ++q)
                                    rule_row(e, zq_rule_at(e.is30, rows[q]), false);
                            }
                        }

                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }
                }

                ImGui::EndTabBar();
            }
        }

        ImGui::EndChild();

        if(!e.error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", e.error.c_str());
        else if(!e.note.empty())
            ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.0f, 1.0f), "%s", e.note.c_str());

        if(ImGui::Button("OK", ImVec2(90 * sc, 0)))
            e.want_ok = true;

        ImGui::SameLine();

        if(ImGui::Button("Cancel", ImVec2(90 * sc, 0)))
            e.close = true;

        if(!ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) &&
           ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            e.close = true;
    }

    ImGui::End();

    if(!open)
        e.close = true;
}

bool same(const ZqRuleSection &a, const ZqRuleSection &b)
{
    return a.compat == b.compat && memcmp(a.rules, b.rules, a.total) == 0 && a.setting_version == b.setting_version &&
           a.settings == b.settings;
}

// The game thread: the rules in; true when they took.
bool commit(RulesEditor &e)
{
    ZqRuleSection s;
    {
        zcui::Lock lock;
        s = e.cur;

        if(same(e.cur, e.orig))
            return true;
    }

    std::string why;

    if(!zq255_rules_put(s, why))
    {
        zcui::Lock lock;
        e.error = "Can't keep this: " + why + ".";
        return false;
    }

    saved = false;
    return true;
}

bool run(int category)
{
    ZqRuleSection s;

    if(!zq255_rules_get(s) || !zcui::available())
        return false;

    {
        zcui::Lock lock;

        if(g_ru.open)
            return false;

        RulesEditor &e = g_ru;
        e.open = true;
        e.close = e.want_ok = false;
        e.focus = true;
        e.is30 = q255_header_version[0] >= 3;
        e.start_tab = category;
        e.orig = e.cur = s;
        e.error.clear();
        e.note.clear();
        e.search[0] = 0;
        e.only_on = false;
        memcpy(e.title, header.title, sizeof(header.title) < sizeof(e.title) ? sizeof(header.title) : sizeof(e.title));
        memcpy(e.author, header.author, sizeof(header.author) < sizeof(e.author) ? sizeof(header.author) : sizeof(e.author));
        e.title[sizeof(e.title) - 1] = e.author[sizeof(e.author) - 1] = 0;
        e.quest.zelda_version = header.zelda_version;
        e.quest.build = header.build;
        e.quest.major = q255_header_version[0];
        e.quest.minor = q255_header_version[1];
        e.quest.patch = q255_header_version[2];
        e.quest.title = e.title;
        e.quest.author = e.author;
        e.quest.s_version = s.version;
        e.quest.compat = s.compat;
        memset(e.listed, 0, sizeof(e.listed));

        for(int k = 0; k < zq_rule_count(e.is30); ++k)
        {
            const int n = zq_rule_at(e.is30, k).rule;

            if(n >= 0 && n < RULE_BYTES * 8 && !bit(e.listed, n))
                flip(e.listed, n);
        }

        recompute(e);
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
            done = g_ru.close;
            ok = g_ru.want_ok;
            g_ru.want_ok = false;
        }

        if(ok && commit(g_ru))
        {
            accepted = true;
            break;
        }

        if(done || !zcui::available())
            break;
    }

    zcui::end_session();
    zcui::remove_layer(layer);
    zcui::Lock lock;
    g_ru.open = false;
    return accepted;
}

} // namespace

// The rules editor (Quest > Rules) for 2.55 and 3.0 quests, opened at a category (upstream's:
// 0 Animation, 1 Combo, 2 Compatibility, 3 Enemy, 4 Item, 5 Misc, 6 NES Fixes...). *shown is
// false when the quest's rules aren't edited this way (the caller uses 2.53's dialogs).
bool zq_edit_rules_newer(int category, bool *shown)
{
    ZqRuleSection s;
    *shown = zq255_rules_get(s);
    return *shown && run(category);
}

// -qst-dump-record q rules 0 out: the rules as the editor shows them, for tests.
void zq_dump_rules(FILE *f)
{
    ZqRuleSection s;

    if(!zq255_rules_get(s))
    {
        fprintf(f, "no 2.55 / 3.0 rules\n");
        return;
    }

    g_ru.is30 = q255_header_version[0] >= 3;
    g_ru.cur = s;
    memcpy(g_ru.title, header.title, 65);
    memcpy(g_ru.author, header.author, 65);
    g_ru.title[65] = g_ru.author[65] = 0;
    g_ru.quest.zelda_version = header.zelda_version;
    g_ru.quest.build = header.build;
    g_ru.quest.major = q255_header_version[0];
    g_ru.quest.minor = q255_header_version[1];
    g_ru.quest.patch = q255_header_version[2];
    g_ru.quest.title = g_ru.title;
    g_ru.quest.author = g_ru.author;
    g_ru.quest.s_version = s.version;
    g_ru.quest.compat = s.compat;
    memset(g_ru.listed, 0, sizeof(g_ru.listed));

    for(int k = 0; k < zq_rule_count(g_ru.is30); ++k)
    {
        const int n = zq_rule_at(g_ru.is30, k).rule;

        if(!bit(g_ru.listed, n))
            flip(g_ru.listed, n);
    }

    recompute(g_ru);
    int locked = 0, on = 0;

    for(int n = 0; n < RULE_BYTES * 8; ++n)
    {
        locked += bit(g_ru.locked, n) && bit(g_ru.listed, n) && n < (int)s.total * 8;
        on += bit(g_ru.eff, n) && bit(g_ru.listed, n);
    }

    fprintf(f, "rules %s: format %d, compat %u (latest %u), %d stored bytes, %d listed rules on, %d decided by the loader, "
               "%d compiler settings\n",
            g_ru.is30 ? "3.0" : "2.55", s.version, (unsigned)s.compat, (unsigned)zq_rules_latest_compat(g_ru.is30),
            (int)s.total, on, locked, (int)s.settings.size());

    for(int k = 0; k < zq_rule_count(g_ru.is30); ++k)
    {
        const ZqRuleInfo &r = zq_rule_at(g_ru.is30, k);

        if(bit(g_ru.eff, r.rule) || bit(g_ru.locked, r.rule))
            fprintf(f, "  %3d %-3s%s %s [%s]\n", r.rule, bit(g_ru.eff, r.rule) ? "on" : "off",
                    bit(g_ru.locked, r.rule) ? " (loader)" : "", r.name, zq_rule_category(g_ru.is30, r.category));
    }
}

// ZQuest's compile dialog for 2.55 and 3.0 Nightly quests (Dear ImGui), as upstream's
// (src/zq/dialog/compilezscript.cpp, compilesetting.cpp, view_script_slots.cpp and the slot
// assignment in src/zq/zquest.cpp, of the quest's generation): the quest's script buffer
// (load it from a file, export it, edit it), the compiler settings, the slots as they are, and
// Compile, which shows the compiler's messages as they come and then assigns the scripts to
// slots (Quick Assign, Smart Assign, or by hand). A compile that fails changes nothing. The work
// itself is in zq_compile.cpp. 2.53 and older quests keep 2.53's compiler dialog.
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <allegro.h>

#include <algorithm>
#include <chrono>
#include <ctype.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "zdefs.h"
#include "zcport/ui.h"
#include "zq_compile.h"
#include "zq_compile_proc.h"
#include "zq_q255_store.h"
#include "zq_rule_names.h"

#include <imgui.h>

extern std::string zScript;
extern bool saved;
extern char *filepath;
extern int q255_header_version[3];
extern zquestheader header;

namespace
{

enum Page { PAGE_MAIN, PAGE_EDIT, PAGE_LOAD, PAGE_EXPORT, PAGE_SETTINGS, PAGE_VIEW, PAGE_COMPILING, PAGE_RESULT, PAGE_ASSIGN, PAGE_DONE,
            PAGE_REMAP };

// One row of the remap question (upstream's InstVarRemapDialog::Choice): an old variable with
// values set, or a converted script's run() parameter with InitD values, and the new variable
// that takes them (-1: none, the values are discarded / stay InitD only).
struct RemapChoice
{
    int remap;          // which of the plan's slots
    int old_id;
    int new_id;
    bool initd;
};

struct CompileDialog
{
    bool open, close, focus;
    bool have_rect;                 // wx..wh hold the window's place of the last frame
    float wx, wy, ww, wh;
    int page;
    bool is30;

    std::vector<char> edit;         // the buffer being edited
    char path[1024];
    std::string note, error;
    bool confirm;                   // the Load/Export overwrite question is up

    // settings
    int cfg[4], timeout;
    char includes[1024];
    ZqRuleSection rules;            // the stored rules, for the compiler rules
    bool have_rules;
    int static_members;             // 3.0 compiler setting: -1 Auto, 0 Off, 1 On

    // view / assign
    int view_type, view_slot;
    bool alphabetize, hide_empty;
    std::map<int, std::string> names[ZQS_COUNT];
    int assign_type, assign_slot, assign_script;

    // compile
    volatile bool cancel;
    bool running, finished;
    ZqCompileResult result;
    int want;                       // what the game thread is to do next (see Want)
    int assign_mode;
    int assign_ms;

    // 3.0: the slots whose placed scripts' values move with this assignment, the question about
    // them, and the names the assignment is made with once it is answered
    std::vector<ZqSlotRemap> plan;
    std::vector<RemapChoice> choices;
    std::map<int, std::string> plan_names[ZQS_COUNT];
    std::string moved;              // what the assignment moved (upstream's log lines)

    CompileDialog() : open(false), close(false), focus(false), have_rect(false), wx(0), wy(0), ww(0), wh(0), page(PAGE_MAIN), is30(false), confirm(false), timeout(30),
                      have_rules(false), static_members(-1), view_type(0), view_slot(0), alphabetize(false), hide_empty(false),
                      assign_type(0), assign_slot(0), assign_script(0), cancel(false), running(false), finished(false), want(0),
                      assign_mode(0), assign_ms(0)
    {
        path[0] = includes[0] = 0;
        memset(cfg, 0, sizeof(cfg));
    }
};

enum Want { WANT_NONE, WANT_COMPILE, WANT_ASSIGN, WANT_LOAD, WANT_EXPORT, WANT_SETTINGS, WANT_ASSIGN_REMAPPED = 50 };

CompileDialog g_cd;
std::thread g_worker;

const ImVec4 COL_ERROR(1.0f, 0.45f, 0.4f, 1.0f);
const ImVec4 COL_WARN(1.0f, 0.85f, 0.35f, 1.0f);
const ImVec4 COL_DEBUG(0.85f, 0.5f, 1.0f, 1.0f);
const ImVec4 COL_NOTE(0.45f, 0.8f, 1.0f, 1.0f);

// The compiler's own settings file ([Compiler] in zscript.cfg, in the folder it runs in).
std::string compiler_cfg_path(bool is30)
{
    return zq_program_dir() + (is30 ? "zscript30/zscript.cfg" : "zscript255_files/zscript.cfg");
}

struct CfgOption
{
    const char *key, *label;
    int def;
    int count;
    const char *names[4];
    int values[4];
};

// upstream's compilesetting.cpp, per generation
const CfgOption g_cfg30[4] =
{
    { "NO_ERROR_HALT", "On error", 0, 2, { "Halt", "Do Not Halt" }, { 0, 1 } },
    { "WARN_DEPRECATED", "Deprecated features", 0, 3, { "Off", "On", "Warn" }, { 0, 1, 3 } },
    { "ON_MISSING_RETURN", "Missing return", 2, 3, { "Nothing", "Warn", "Error" }, { 0, 3, 2 } },
    { "LEGACY_ARRAYS", "Legacy arrays", 3, 3, { "Nothing", "Warn", "Error" }, { 0, 3, 2 } },
};
const CfgOption g_cfg255[3] =
{
    { "NO_ERROR_HALT", "On error", 0, 2, { "Halt", "Do Not Halt" }, { 0, 1 } },
    { "HEADER_GUARD", "Header guard", 1, 4, { "Disable", "Enable", "Error, Enable", "Warn, Enable" }, { 0, 1, 2, 3 } },
    { "WARN_DEPRECATED", "Deprecated", 0, 3, { "Ignore", "Warn", "Error" }, { 0, 1, 2 } },
};

int cfg_count(bool is30)
{
    return is30 ? 4 : 3;
}

const CfgOption &cfg_at(bool is30, int i)
{
    return is30 ? g_cfg30[i] : g_cfg255[i];
}

void read_cfg_file(const std::string &path, std::vector<std::string> &lines)
{
    lines.clear();
    FILE *f = fopen(path.c_str(), "rb");

    if(!f)
        return;

    std::string cur;
    int ch;

    while((ch = fgetc(f)) != EOF)
    {
        if(ch == '\n')
        {
            if(!cur.empty() && cur[cur.size() - 1] == '\r')
                cur.resize(cur.size() - 1);
            lines.push_back(cur);
            cur.clear();
        }
        else
            cur += (char)ch;
    }

    if(!cur.empty())
        lines.push_back(cur);
    fclose(f);
}

std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

int get_cfg(const std::vector<std::string> &lines, const char *key, int def)
{
    bool in = false;

    for(size_t i = 0; i < lines.size(); ++i)
    {
        const std::string &l = lines[i];

        if(!l.empty() && l[0] == '[')
        {
            in = trim(l) == "[Compiler]";
            continue;
        }

        size_t eq = l.find('=');

        if(in && eq != std::string::npos && trim(l.substr(0, eq)) == key)
            return atoi(trim(l.substr(eq + 1)).c_str());
    }

    return def;
}

void set_cfg(std::vector<std::string> &lines, const char *key, int value)
{
    char line[128];
    snprintf(line, sizeof(line), "%s = %d", key, value);
    bool in = false;
    int section = -1;

    for(size_t i = 0; i < lines.size(); ++i)
    {
        const std::string &l = lines[i];

        if(!l.empty() && l[0] == '[')
        {
            in = trim(l) == "[Compiler]";
            if(in)
                section = (int)i;
            continue;
        }

        size_t eq = l.find('=');

        if(in && eq != std::string::npos && trim(l.substr(0, eq)) == key)
        {
            lines[i] = line;
            return;
        }
    }

    if(section < 0)
    {
        lines.push_back("[Compiler]");
        section = (int)lines.size() - 1;
    }

    lines.insert(lines.begin() + section + 1, line);
}

bool write_cfg_file(const std::string &path, const std::vector<std::string> &lines)
{
    FILE *f = fopen(path.c_str(), "wb");

    if(!f)
        return false;

    for(size_t i = 0; i < lines.size(); ++i)
        fprintf(f, "%s\n", lines[i].c_str());

    return fclose(f) == 0;
}

std::string default_buffer_file()
{
    std::string q = filepath ? filepath : "";
    size_t dot = q.find_last_of('.'), slash = q.find_last_of("/\\");

    if(q.empty())
        return "buffer.zs";

    if(dot != std::string::npos && (slash == std::string::npos || dot > slash))
        q = q.substr(0, dot);

    return q + ".zs";
}

int resize_cb(ImGuiInputTextCallbackData *d)
{
    if(d->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        std::vector<char> *v = (std::vector<char> *)d->UserData;
        v->resize((size_t)d->BufTextLen + 1);
        d->Buf = &(*v)[0];
    }

    return 0;
}

const char *page_title(int page)
{
    switch(page)
    {
    case PAGE_EDIT: return "Edit the script buffer";
    case PAGE_LOAD: return "Load ZScript";
    case PAGE_EXPORT: return "Export ZScript";
    case PAGE_SETTINGS: return "ZScript Compiler Settings";
    case PAGE_VIEW: return "View Script Slots";
    case PAGE_COMPILING: return "Compiling";
    case PAGE_RESULT: return "ZScript Parser";
    case PAGE_ASSIGN: return "Assign Compiled Scripts";
    case PAGE_DONE: return "Slots Assigned";
    case PAGE_REMAP:
        for(size_t i = 0; i < g_cd.choices.size(); ++i)
            if(g_cd.choices[i].initd)
                return "Move InitD Values?";
        return "Renamed Script Variables?";
    }
    return "Compile ZScript";
}

void messages(CompileDialog &e, float height)
{
    if(ImGui::BeginChild("console", ImVec2(0, height), true, ImGuiWindowFlags_HorizontalScrollbar))
    {
        zq_compile_lock();

        for(size_t i = 0; i < e.result.messages.size(); ++i)
        {
            const ZqCompileMessage &m = e.result.messages[i];
            const char *tag = m.code == -9997 ? "[Error] " : m.code == -9996 ? "[Warn] " : m.code == -9995 ? "[Debug] " : "[Info] ";
            const ImVec4 col = m.code == -9997 ? COL_ERROR : m.code == -9996 ? COL_WARN : m.code == -9995 ? COL_DEBUG : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted((std::string(tag) + m.text).c_str());
            ImGui::PopStyleColor();
        }

        zq_compile_unlock();

        if(e.running && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4)
            ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

std::string slot_label(int t, int id, const std::string &name, const ZqCompileResult *r)
{
    char buf[64];

    if(t == ZQS_GLOBAL)
    {
        static const char *const g[8] = { "Init", "Active", "onExit", "onSaveLoad", "onLaunch", "onContGame", "onF6Menu", "onSave" };
        snprintf(buf, sizeof(buf), "%s:", g[id & 7]);
    }
    else if(t == ZQS_HERO)
    {
        static const char *const h[4] = { "Init", "Active", "onDeath", "onWin" };
        snprintf(buf, sizeof(buf), "%s:", h[id & 3]);
    }
    else
        snprintf(buf, sizeof(buf), "Slot %d:", id + 1);

    std::string s = buf;

    if(!name.empty())
    {
        s += " " + name;
        if(r && !zq_slot_found(*r, t, name))
            s += "  (missing)";
    }

    return s;
}

void page_main(CompileDialog &e, float sc)
{
    ImGui::Text("Quest made in %s: compiled by %s.", e.is30 ? "a 3.0 Nightly" : "2.55",
                e.is30 ? "zscript30 (ZC 3.0 Nightly)" : "zscript255 (ZC 2.55.17)");

    if(zq_compile_last_version() >= 0)
        ImGui::Text("Last Compiled Using ZScript: v.%d", zq_compile_last_version());
    else
        ImGui::TextDisabled("Last Compiled Using ZScript: unknown");

    ImGui::Text("%d Bytes in Buffer", (int)zScript.size());

    if(zq_compile_pending())
        ImGui::TextColored(COL_NOTE, "Compiled scripts are assigned; they go into the quest when it is saved.");

    ImGui::Spacing();
    const ImVec2 b(110 * sc, 0);

    if(ImGui::Button("Load", b))
    {
        snprintf(e.path, sizeof(e.path), "%s", get_config_string("Compiler", "buffer_file", default_buffer_file().c_str()));
        e.page = PAGE_LOAD;
        e.confirm = false;
        e.error.clear();
    }

    ImGui::SameLine();

    if(ImGui::Button("Export", b))
    {
        snprintf(e.path, sizeof(e.path), "%s", get_config_string("Compiler", "buffer_file", default_buffer_file().c_str()));
        e.page = PAGE_EXPORT;
        e.confirm = false;
        e.error.clear();
    }

    ImGui::SameLine();

    if(ImGui::Button("Edit", b))
    {
        e.edit.assign(zScript.begin(), zScript.end());
        e.edit.push_back(0);
        e.page = PAGE_EDIT;
        e.error.clear();
    }

    if(ImGui::Button("Settings", b))
    {
        e.want = WANT_SETTINGS;     // the game thread reads the files
        e.error.clear();
    }

    ImGui::SameLine();

    if(ImGui::Button("Slots", b))
    {
        for(int t = 0; t < ZQS_COUNT; ++t)
            e.names[t] = zq_slot_names(t);
        e.page = PAGE_VIEW;
    }

    if(ImGui::Button("Compile", b))
        e.want = WANT_COMPILE;

    ImGui::SameLine();

    if(ImGui::Button("Close", b))
        e.close = true;
}

void page_path(CompileDialog &e, float sc, bool load)
{
    ImGui::TextUnformatted(load ? "Load ZScript (.z, .zh, .zs, .zlib, etc.) into the buffer from:" : "Save the buffer as ZScript (.zs) to:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##path", e.path, sizeof(e.path));

    if(e.confirm)
    {
        ImGui::TextColored(COL_WARN, "%s", load ? "Loading will erase the current buffer. Proceed anyway?" : "File already exists. Overwrite?");

        if(ImGui::Button("Yes", ImVec2(90 * sc, 0)))
            e.want = load ? WANT_LOAD : WANT_EXPORT;

        ImGui::SameLine();

        if(ImGui::Button("No", ImVec2(90 * sc, 0)))
            e.confirm = false;

        return;
    }

    if(ImGui::Button(load ? "Load" : "Export", ImVec2(90 * sc, 0)))
    {
        if(load && !zScript.empty())
            e.confirm = true;
        else if(!load && exists(e.path))
            e.confirm = true;
        else
            e.want = load ? WANT_LOAD : WANT_EXPORT;
    }

    ImGui::SameLine();

    if(ImGui::Button("Back", ImVec2(90 * sc, 0)))
        e.page = PAGE_MAIN;
}

void page_edit(CompileDialog &e, float sc)
{
    // The text box ends the buffer at its first NUL byte, so Apply would cut off the rest:
    // a buffer that holds one is only loaded and exported, not edited here.
    if(zScript.find('\0') != std::string::npos)
    {
        ImGui::TextWrapped("The script buffer contains a NUL byte, so it can't be edited here without losing "
                           "everything after it. Export it, edit the file, and load it again.");

        if(ImGui::Button("Back", ImVec2(90 * sc, 0)))
            e.page = PAGE_MAIN;

        return;
    }

    ImGui::TextDisabled("%d bytes", (int)strlen(&e.edit[0]));
    const float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::InputTextMultiline("##buffer", &e.edit[0], e.edit.size(), ImVec2(-1, -bottom),
                              ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize, resize_cb, &e.edit);

    if(ImGui::Button("Apply", ImVec2(90 * sc, 0)))
    {
        std::string now(&e.edit[0]);

        if(now != zScript)
        {
            zScript = now;
            saved = false;
        }

        e.page = PAGE_MAIN;
    }

    ImGui::SameLine();

    if(ImGui::Button("Cancel", ImVec2(90 * sc, 0)))
        e.page = PAGE_MAIN;
}

bool rule_bit(const byte *b, int n)
{
    return (b[n >> 3] >> (n & 7)) & 1;
}

void page_settings(CompileDialog &e, float sc)
{
    ImGui::TextWrapped("Settings used by the compiler when compiling scripts. For quest settings: 'COMPAT' options should "
                       "generally be off unless you have a reason, and 'COMPAT REVERSE' options should generally be on unless "
                       "you have a reason. 'OPTION' options are entirely user discretion.");
    // the options scroll; OK and Cancel stay below them
    const float bottom = ImGui::GetFrameHeightWithSpacing() * (e.error.empty() ? 1 : 2) + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("options", ImVec2(0, -bottom), false);
    ImGui::SeparatorText("Compiler (its zscript.cfg)");

    for(int i = 0; i < cfg_count(e.is30); ++i)
    {
        const CfgOption &o = cfg_at(e.is30, i);
        int sel = 0;
        for(int k = 0; k < o.count; ++k)
            if(o.values[k] == e.cfg[i])
                sel = k;

        ImGui::SetNextItemWidth(180 * sc);

        if(ImGui::Combo(o.label, &sel, o.names, o.count))
            e.cfg[i] = o.values[sel];
    }

    ImGui::SetNextItemWidth(180 * sc);
    ImGui::InputInt("Timeout (seconds, 0: none)", &e.timeout);
    if(e.timeout < 0)
        e.timeout = 0;
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("Include paths", e.includes, sizeof(e.includes));
    ImGui::TextDisabled("Separated by ';', relative to the compiler's folder; the quest's own scripts folder is always added.");

    if(e.have_rules)
    {
        ImGui::SeparatorText("Quest settings (stored in the quest's rules)");

        for(int k = 0; k < zq_rule_count(e.is30); ++k)
        {
            const ZqRuleInfo &r = zq_rule_at(e.is30, k);

            if(r.category != ZQ_RULE_CATEGORIES - 1 || r.rule < 0 || r.rule >= (int)e.rules.total * 8)
                continue;

            bool on = rule_bit(e.rules.rules, r.rule);

            if(ImGui::Checkbox(r.name, &on))
                e.rules.rules[r.rule >> 3] ^= (byte)(1 << (r.rule & 7));

            if(r.help && *r.help && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", r.help);
        }

        if(e.is30 && e.rules.has_settings)
        {
            const char *opts[3] = { "Auto", "Off", "On" };
            int sel = e.static_members + 1;
            ImGui::SetNextItemWidth(180 * sc);

            if(ImGui::Combo("Default Static Script Members", &sel, opts, 3))
                e.static_members = sel - 1;
        }
    }

    ImGui::EndChild();

    if(!e.error.empty())
        ImGui::TextColored(COL_ERROR, "%s", e.error.c_str());

    if(ImGui::Button("OK", ImVec2(90 * sc, 0)))
        e.want = WANT_SETTINGS + 100;   // save them (game thread)

    ImGui::SameLine();

    if(ImGui::Button("Cancel", ImVec2(90 * sc, 0)))
        e.page = PAGE_MAIN;
}

void page_view(CompileDialog &e, float sc)
{
    ImGui::TextUnformatted("This is a preview of all currently assigned script slots. To assign new slots, use 'Compile'.");
    const float bottom = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y;

    if(ImGui::BeginChild("types", ImVec2(130 * sc, -bottom), true))
    {
        for(int t = 0; t < ZQS_COUNT; ++t)
            if(ImGui::Selectable(zq_slot_type(t).name, e.view_type == t))
                e.view_type = t;
    }

    ImGui::EndChild();
    ImGui::SameLine();

    if(ImGui::BeginChild("slots", ImVec2(0, -bottom), true))
    {
        const int t = e.view_type;
        std::vector<std::pair<std::string, int> > rows;

        for(int i = 0; i < zq_slot_type(t).slots; ++i)
        {
            std::map<int, std::string>::const_iterator it = e.names[t].find(i);
            const std::string name = it == e.names[t].end() ? std::string() : it->second;

            if(e.hide_empty && name.empty())
                continue;

            rows.push_back(std::make_pair(slot_label(t, i, name, NULL), i));
        }

        if(e.alphabetize)
            std::sort(rows.begin(), rows.end());

        ImGuiListClipper clip;
        clip.Begin((int)rows.size());

        while(clip.Step())
            for(int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                if(ImGui::Selectable(rows[i].first.c_str(), e.view_slot == rows[i].second))
                    e.view_slot = rows[i].second;
    }

    ImGui::EndChild();
    ImGui::Checkbox("Alphabetize", &e.alphabetize);
    ImGui::SameLine();
    ImGui::Checkbox("Hide Empty", &e.hide_empty);

    if(ImGui::Button("Back", ImVec2(90 * sc, 0)))
        e.page = PAGE_MAIN;
}

void page_compiling(CompileDialog &e, float sc)
{
    ImGui::Text("External ZScript Parser: %s", zq_compile_compiler_path().c_str());
    const float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    messages(e, -bottom);

    if(ImGui::Button("Cancel", ImVec2(90 * sc, 0)))
        e.cancel = true;
}

void page_result(CompileDialog &e, float sc)
{
    const bool ok = e.result.code == 0;
    ImGui::TextColored(ok ? COL_NOTE : COL_ERROR, "ZScript compilation: Returned code '%d' (%s)", e.result.code, ok ? "success" : "failure");
    ImGui::Text("Compile took %d ms", e.result.ms);

    if(!e.result.error.empty())
        ImGui::TextColored(COL_ERROR, "%s", e.result.error.c_str());

    if(!ok)
        ImGui::TextColored(COL_ERROR, "Compilation failed. Nothing in the quest was changed.");

    const float lines = ok ? 6.0f : 2.0f;
    const float bottom = ImGui::GetFrameHeightWithSpacing() * lines + ImGui::GetStyle().ItemSpacing.y;
    messages(e, -bottom);

    if(!ok)
    {
        if(ImGui::Button("OK", ImVec2(90 * sc, 0)))
            e.page = PAGE_MAIN;
        return;
    }

    ImGui::TextUnformatted("Quick Assign: updates all already-assigned scripts.");
    ImGui::TextUnformatted("Smart Assign: updates all already-assigned scripts, assigns global/hero scripts to empty");
    ImGui::TextUnformatted("  slots of the same name, and assigns all other unassigned scripts to slots.");
    ImGui::TextUnformatted("OK: assign the slots by hand.");

    if(!e.error.empty())
        ImGui::TextColored(COL_ERROR, "%s", e.error.c_str());

    const ImVec2 b(120 * sc, 0);

    if(ImGui::Button("Quick Assign", b))
    {
        e.assign_mode = 1;
        e.want = WANT_ASSIGN;
    }

    ImGui::SameLine();

    if(ImGui::Button("Smart Assign", b))
    {
        e.assign_mode = 2;
        e.want = WANT_ASSIGN;
    }

    ImGui::SameLine();

    if(ImGui::Button("OK", b))
    {
        for(int t = 0; t < ZQS_COUNT; ++t)
            e.names[t] = zq_slot_names(t);
        zq_slots_auto(e.result, 0, e.names);
        e.assign_type = 0;
        e.assign_slot = 0;
        e.assign_script = 0;
        e.page = PAGE_ASSIGN;
    }

    ImGui::SameLine();

    if(ImGui::Button("Cancel", b))
        e.page = PAGE_MAIN;
}

void page_assign(CompileDialog &e, float sc)
{
    if(ImGui::BeginTabBar("types"))
    {
        for(int t = 0; t < ZQS_COUNT; ++t)
            if(ImGui::BeginTabItem(zq_slot_type(t).name))
            {
                if(e.assign_type != t)
                {
                    e.assign_type = t;
                    e.assign_slot = 0;
                    e.assign_script = 0;
                }
                ImGui::EndTabItem();
            }

        ImGui::EndTabBar();
    }

    const int t = e.assign_type;
    const float bottom = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y;
    const float w = ImGui::GetContentRegionAvail().x;

    if(ImGui::BeginChild("slots", ImVec2(w * 0.5f - 40 * sc, -bottom), true))
    {
        ImGuiListClipper clip;
        clip.Begin(zq_slot_type(t).slots);

        while(clip.Step())
            for(int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
            {
                ImGui::PushID(i);
                if(ImGui::Selectable(slot_label(t, i, e.names[t][i], &e.result).c_str(), e.assign_slot == i))
                    e.assign_slot = i;
                ImGui::PopID();
            }
    }

    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();

    if(ImGui::Button("<<", ImVec2(60 * sc, 0)))
    {
        if(t == ZQS_GLOBAL && e.assign_slot == 0)
            e.error = "ZScript reserves this slot.";
        else
        {
            e.names[t][e.assign_slot] = e.assign_script <= 0 ? std::string() : e.result.names[t][e.assign_script - 1];
            e.error.clear();
        }
    }

    ImGui::EndGroup();
    ImGui::SameLine();

    if(ImGui::BeginChild("scripts", ImVec2(0, -bottom), true))
    {
        if(ImGui::Selectable("<none>", e.assign_script == 0))
            e.assign_script = 0;

        for(size_t i = 0; i < e.result.names[t].size(); ++i)
            if(ImGui::Selectable(e.result.names[t][i].c_str(), e.assign_script == (int)i + 1))
                e.assign_script = (int)i + 1;
    }

    ImGui::EndChild();

    if(ImGui::Button("Clear Type", ImVec2(110 * sc, 0)))
        for(std::map<int, std::string>::iterator it = e.names[t].begin(); it != e.names[t].end(); ++it)
            if(!(t == ZQS_GLOBAL && it->first == 0))
                it->second.clear();

    ImGui::SameLine();

    if(ImGui::Button("Clear Missing", ImVec2(110 * sc, 0)))
        for(int k = 0; k < ZQS_COUNT; ++k)
            for(std::map<int, std::string>::iterator it = e.names[k].begin(); it != e.names[k].end(); ++it)
                if(!it->second.empty() && !zq_slot_found(e.result, k, it->second))
                    it->second.clear();

    if(!e.error.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(COL_ERROR, "%s", e.error.c_str());
    }

    if(ImGui::Button("OK", ImVec2(90 * sc, 0)))
    {
        e.assign_mode = 0;
        e.want = WANT_ASSIGN;
    }

    ImGui::SameLine();

    if(ImGui::Button("Cancel", ImVec2(90 * sc, 0)))
        e.page = PAGE_MAIN;
}

// ---- the remap question (upstream's zq/dialog/inst_var_remap.cpp) ---------------------------

std::string describe_var(const ZqInstVar &var, const std::map<word, std::string> &labels)
{
    std::map<word, std::string>::const_iterator it = labels.find(var.id);
    return var.type_name + " " + var.name + (it == labels.end() ? std::string() : " (\"" + it->second + "\")");
}

const ZqInstVar *param_of(const ZqVarRemap &m, int id)
{
    for(size_t i = 0; i < m.old_params.size(); ++i)
        if(m.old_params[i].id == id)
            return &m.old_params[i];

    return NULL;
}

std::string choice_old_name(const CompileDialog &e, const RemapChoice &c)
{
    const ZqVarRemap &m = e.plan[c.remap].remap;

    if(c.initd)
    {
        const ZqInstVar *p = param_of(m, c.old_id);
        return p ? p->name : std::string();
    }

    std::map<word, ZqInstVar>::const_iterator it = m.old_vars.find((word)c.old_id);
    return it == m.old_vars.end() ? std::string() : it->second.name;
}

// The rows to ask about: for every slot that needs it, its converted parameters that have InitD
// values (each first going to the variable of its name) and its variables that are gone but
// have values (a lone rename of the same type is chosen first; otherwise the user chooses).
void build_choices(CompileDialog &e)
{
    e.choices.clear();

    for(size_t ri = 0; ri < e.plan.size(); ++ri)
    {
        const ZqSlotRemap &r = e.plan[ri];

        if(!r.needs_user_mapping())
            continue;

        const std::vector<ZqInstVar> uo = r.unmatched_old(), un = r.unmatched_new();

        for(size_t i = 0; i < r.remap.old_params.size(); ++i)
        {
            const ZqInstVar &param = r.remap.old_params[i];

            if(!r.initd_value_counts.count(param.id))
                continue;

            RemapChoice c = { (int)ri, param.id, -1, true };

            for(size_t k = 0; k < r.remap.initd_moves.size(); ++k)
                if(r.remap.initd_moves[k].initd == param.id)
                    c.new_id = r.remap.initd_moves[k].new_id;

            e.choices.push_back(c);
        }

        int guess = -1;

        if(uo.size() == 1 && un.size() == 1 && uo[0].type_name == un[0].type_name)
            guess = un[0].id;

        for(size_t i = 0; i < uo.size(); ++i)
            if(r.unmatched_value_counts.count(uo[i].id))
            {
                RemapChoice c = { (int)ri, uo[i].id, guess, false };
                e.choices.push_back(c);
            }
    }
}

// Two rows of one script going to the same new variable: only one can give it values.
std::string choice_conflict(const CompileDialog &e, size_t i)
{
    const RemapChoice &c = e.choices[i];
    std::string others;

    if(c.new_id < 0)
        return others;

    for(size_t k = 0; k < e.choices.size(); ++k)
        if(k != i && e.choices[k].remap == c.remap && e.choices[k].new_id == c.new_id)
            others += (others.empty() ? "" : ", ") + choice_old_name(e, e.choices[k]);

    return others;
}

// The answers into the plan's remaps (upstream's handleMessage OK).
void take_choices(CompileDialog &e)
{
    for(size_t i = 0; i < e.choices.size(); ++i)
    {
        const RemapChoice &c = e.choices[i];
        ZqVarRemap &m = e.plan[c.remap].remap;

        if(c.initd)
        {
            std::vector<ZqInitDMove> keep;

            for(size_t k = 0; k < m.initd_moves.size(); ++k)
                if(m.initd_moves[k].initd != c.old_id)
                    keep.push_back(m.initd_moves[k]);

            if(c.new_id >= 0)
            {
                const ZqInstVar *p = param_of(m, c.old_id);
                ZqInitDMove mv = { c.old_id, (byte)(p ? p->kind : 0), (word)c.new_id };
                keep.push_back(mv);
            }

            m.initd_moves.swap(keep);
        }
        else if(c.new_id >= 0)
            m.old_to_new[(word)c.old_id] = (word)c.new_id;
    }

    e.choices.clear();
}

void page_remap(CompileDialog &e, float sc)
{
    bool any_initd = false, any_vars = false, conflicts = false;

    for(size_t i = 0; i < e.choices.size(); ++i)
        (e.choices[i].initd ? any_initd : any_vars) = true;

    // the explanation scrolls with the rows, so the buttons stay in reach in a small window
    const float bottom = ImGui::GetFrameHeightWithSpacing() * 3 + ImGui::GetStyle().ItemSpacing.y;

    if(ImGui::BeginChild("remaps", ImVec2(0, -bottom), true))
    {
    if(any_initd)
        ImGui::TextWrapped("These scripts' run() parameters (InitD) were replaced with variables. Each parameter's values (on "
                           "FFCs, combos, items, etc.) are copied to the variable chosen for it - initially the one of the same "
                           "name. InitD itself is left as is.");

    if(any_vars)
        ImGui::TextWrapped("These script variables have values set (on FFCs, combos, items, etc.), but no longer exist after "
                           "compiling. If a variable was renamed, choose its new name to keep its values. Otherwise, its values "
                           "will be discarded.");

    ImGui::TextWrapped("Cancel to leave all scripts as they were.");

        if(ImGui::BeginTable("rows", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Script");
            ImGui::TableSetupColumn("Old Variable");
            ImGui::TableSetupColumn("New Variable");
            ImGui::TableHeadersRow();

            for(size_t i = 0; i < e.choices.size(); ++i)
            {
                RemapChoice &c = e.choices[i];
                const ZqSlotRemap &r = e.plan[c.remap];
                ImGui::PushID((int)i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s '%s'", zq_slot_type(r.type).name, r.script_name.c_str());
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                std::string old_text;
                int count = 0;

                if(c.initd)
                {
                    const ZqInstVar *p = param_of(r.remap, c.old_id);
                    old_text = "InitD[" + std::to_string(c.old_id) + "] " + (p ? p->type_name + " " + p->name : std::string());
                    std::map<int, int>::const_iterator n = r.initd_value_counts.find(c.old_id);
                    count = n == r.initd_value_counts.end() ? 0 : n->second;
                }
                else
                {
                    std::map<word, ZqInstVar>::const_iterator v = r.remap.old_vars.find((word)c.old_id);
                    old_text = v == r.remap.old_vars.end() ? std::string() : describe_var(v->second, r.old_labels);
                    std::map<word, int>::const_iterator n = r.unmatched_value_counts.find((word)c.old_id);
                    count = n == r.unmatched_value_counts.end() ? 0 : n->second;
                }

                ImGui::TextUnformatted(old_text.c_str());

                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("Has values set in %d place%s.", count, count == 1 ? "" : "s");

                ImGui::TableNextColumn();

                // any exported variable not taking an old variable of the same name
                std::vector<std::pair<int, std::string> > list;
                list.push_back(std::make_pair(-1, std::string("(Discard values)")));

                for(std::map<word, ZqInstVar>::const_iterator v = r.remap.new_vars.begin(); v != r.remap.new_vars.end(); ++v)
                {
                    bool matched = false;

                    for(std::map<word, word>::const_iterator o = r.remap.old_to_new.begin(); o != r.remap.old_to_new.end(); ++o)
                        matched = matched || o->second == v->first;

                    if(!matched && r.new_labels.count(v->first))
                        list.push_back(std::make_pair((int)v->first, describe_var(v->second, r.new_labels)));
                }

                std::string now = list[0].second;

                for(size_t k = 0; k < list.size(); ++k)
                    if(list[k].first == c.new_id)
                        now = list[k].second;

                const std::string clash = choice_conflict(e, i);
                conflicts = conflicts || !clash.empty();

                if(!clash.empty())
                    ImGui::PushStyleColor(ImGuiCol_Text, COL_ERROR);

                ImGui::SetNextItemWidth(-FLT_MIN);

                if(ImGui::BeginCombo("##new", now.c_str()))
                {
                    for(size_t k = 0; k < list.size(); ++k)
                        if(ImGui::Selectable(list[k].second.c_str(), list[k].first == c.new_id))
                            c.new_id = list[k].first;

                    ImGui::EndCombo();
                }

                if(!clash.empty())
                {
                    ImGui::PopStyleColor();

                    if(ImGui::IsItemHovered())
                        ImGui::SetTooltip("It is also chosen for %s.\nOnly one of them can give it values.", clash.c_str());
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    ImGui::EndChild();

    if(conflicts)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_ERROR);
        ImGui::TextWrapped("Each new variable can only take one old variable's values. Choose a different variable, or "
                           "discard, for all but one of those in red.");
        ImGui::PopStyleColor();
    }

    ImGui::BeginDisabled(conflicts);

    if(ImGui::Button("OK", ImVec2(90 * sc, 0)))
        e.want = WANT_ASSIGN_REMAPPED;

    ImGui::EndDisabled();
    ImGui::SameLine();

    if(ImGui::Button("Cancel", ImVec2(90 * sc, 0)))
    {
        e.choices.clear();
        e.plan.clear();
        e.page = e.assign_mode ? PAGE_RESULT : PAGE_ASSIGN;
    }
}

void page_done(CompileDialog &e, float sc)
{
    ImGui::TextUnformatted("ZScripts successfully loaded into script slots");
    ImGui::Text("Assign Slots took %d ms", e.assign_ms);

    if(!e.moved.empty())
        ImGui::TextWrapped("%s", e.moved.c_str());

    ImGui::TextDisabled("The quest's scripts change when it is saved.");

    if(ImGui::Button("OK", ImVec2(90 * sc, 0)))
        e.page = PAGE_MAIN;
}

void dialog_layer(void *)
{
    CompileDialog &e = g_cd;

    if(!e.open)
        return;

    const float sc = zcui::scale();
    ImGuiViewport *vp = ImGui::GetMainViewport();
    // fits a small (tiled) ZQuest window too
    const float ww = (std::min)(760 * sc, vp->WorkSize.x - 8), wh = (std::min)(560 * sc, vp->WorkSize.y - 8);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ww, wh), ImGuiCond_Appearing);

    // ZQuest's own window got smaller: move and shrink this one so its buttons stay in reach
    if(e.have_rect)
    {
        const float x0 = vp->WorkPos.x + 4, y0 = vp->WorkPos.y + 4;
        const float x1 = vp->WorkPos.x + vp->WorkSize.x - 4, y1 = vp->WorkPos.y + vp->WorkSize.y - 4;
        const float w = (std::max)(120.0f, (std::min)(e.ww, x1 - x0)), h = (std::max)(120.0f, (std::min)(e.wh, y1 - y0));
        const float x = (std::max)(x0, (std::min)(e.wx, x1 - w)), y = (std::max)(y0, (std::min)(e.wy, y1 - h));

        if(x != e.wx || y != e.wy || w != e.ww || h != e.wh)
        {
            ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        }
    }

    if(e.focus)
    {
        ImGui::SetNextWindowFocus();
        e.focus = false;
    }

    char wtitle[160];
    snprintf(wtitle, sizeof(wtitle), "%s (%s)###zqcompile", page_title(e.page), e.is30 ? "3.0 Nightly" : "2.55");
    bool open = true;

    if(ImGui::Begin(wtitle, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        e.wx = ImGui::GetWindowPos().x;
        e.wy = ImGui::GetWindowPos().y;
        e.ww = ImGui::GetWindowSize().x;
        e.wh = ImGui::GetWindowSize().y;
        e.have_rect = true;

        switch(e.page)
        {
        case PAGE_EDIT: page_edit(e, sc); break;
        case PAGE_LOAD: page_path(e, sc, true); break;
        case PAGE_EXPORT: page_path(e, sc, false); break;
        case PAGE_SETTINGS: page_settings(e, sc); break;
        case PAGE_VIEW: page_view(e, sc); break;
        case PAGE_COMPILING: page_compiling(e, sc); break;
        case PAGE_RESULT: page_result(e, sc); break;
        case PAGE_ASSIGN: page_assign(e, sc); break;
        case PAGE_DONE: page_done(e, sc); break;
        case PAGE_REMAP: page_remap(e, sc); break;
        default: page_main(e, sc); break;
        }

        if(e.page == PAGE_MAIN && !e.note.empty())
            ImGui::TextColored(COL_NOTE, "%s", e.note.c_str());
        if(e.page == PAGE_MAIN && !e.error.empty())
            ImGui::TextColored(COL_ERROR, "%s", e.error.c_str());

        if(e.page == PAGE_MAIN && !ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
           ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            e.close = true;
    }

    ImGui::End();

    if(!open && !e.running)
        e.close = true;
}

// ---- the game thread's side --------------------------------------------------------------------

void load_settings(CompileDialog &e)
{
    std::vector<std::string> lines;
    read_cfg_file(compiler_cfg_path(e.is30), lines);
    std::vector<std::string> base;
    read_cfg_file(zq_program_dir() + (e.is30 ? "zscript30/base_config/zscript.cfg" : "zscript255_files/base_config/zscript.cfg"), base);

    for(int i = 0; i < cfg_count(e.is30); ++i)
    {
        const CfgOption &o = cfg_at(e.is30, i);
        e.cfg[i] = get_cfg(lines, o.key, get_cfg(base, o.key, o.def));
    }

    e.timeout = get_config_int("Compiler", "compiler_timeout", 30);
    snprintf(e.includes, sizeof(e.includes), "%s", get_config_string("Compiler", e.is30 ? "include_paths_30" : "include_paths_255", "include/;headers/;scripts/"));
    e.have_rules = zq255_rules_get(e.rules);
    e.static_members = -1;

    if(e.have_rules)
        for(size_t i = 0; i < e.rules.settings.size(); ++i)
            if(e.rules.settings[i].first == 0)
                e.static_members = (int)e.rules.settings[i].second;
}

bool save_settings(CompileDialog &e, std::string &why)
{
    std::vector<std::string> lines;
    read_cfg_file(compiler_cfg_path(e.is30), lines);

    for(int i = 0; i < cfg_count(e.is30); ++i)
        set_cfg(lines, cfg_at(e.is30, i).key, e.cfg[i]);

    if(!write_cfg_file(compiler_cfg_path(e.is30), lines))
    {
        why = "the compiler's settings file (" + compiler_cfg_path(e.is30) + ") couldn't be written";
        return false;
    }

    set_config_int("Compiler", "compiler_timeout", e.timeout);
    set_config_string("Compiler", e.is30 ? "include_paths_30" : "include_paths_255", e.includes);

    if(!e.have_rules)
        return true;

    ZqRuleSection s;

    if(!zq255_rules_get(s))
        return true;

    bool changed = memcmp(s.rules, e.rules.rules, s.total) != 0;
    memcpy(s.rules, e.rules.rules, s.total);

    if(e.is30 && s.has_settings)
    {
        std::vector<std::pair<word, long> > now;

        for(size_t i = 0; i < s.settings.size(); ++i)
            if(s.settings[i].first != 0)
                now.push_back(s.settings[i]);

        if(e.static_members != -1)     // Auto: no entry, as upstream saves it
            now.push_back(std::make_pair((word)0, (long)e.static_members));

        std::sort(now.begin(), now.end());
        changed = changed || now != s.settings;
        s.settings = now;
    }

    if(changed)
    {
        if(!zq255_rules_put(s, why))
            return false;
        saved = false;
    }

    return true;
}

void do_load(CompileDialog &e)
{
    FILE *f = fopen(e.path, "rb");

    if(!f)
    {
        zcui::Lock lock;
        e.error = "Cannot open specified file!";
        e.confirm = false;
        return;
    }

    // as upstream: everything up to the end of the file, with the file's own line ends
    std::string text;
    int ch;

    while((ch = fgetc(f)) != EOF)
        text += (char)ch;

    fclose(f);
    set_config_string("Compiler", "buffer_file", e.path);
    zcui::Lock lock;
    zScript = text;
    saved = false;
    e.note = "Loaded " + std::to_string((long)text.size()) + " bytes from " + e.path + ".";
    e.error.clear();
    e.page = PAGE_MAIN;
}

void do_export(CompileDialog &e)
{
    FILE *f = fopen(e.path, "wb");
    bool ok = f && fwrite(zScript.data(), 1, zScript.size(), f) == zScript.size();

    if(f)
        ok = fclose(f) == 0 && ok;

    zcui::Lock lock;

    if(!ok)
    {
        e.error = f ? "IO error while writing script to file!" : "Unable to open file for writing!";
        e.confirm = false;
        return;
    }

    set_config_string("Compiler", "buffer_file", e.path);
    e.note = "The buffer was written to " + std::string(e.path) + ".";
    e.error.clear();
    e.page = PAGE_MAIN;
}

void run(void)
{
    if(!zq_compile_available() || !zcui::available())
        return;

    {
        zcui::Lock lock;

        if(g_cd.open)
            return;

        CompileDialog &e = g_cd;
        e.open = true;
        e.close = false;
        e.focus = true;
        e.have_rect = false;
        e.page = PAGE_MAIN;
        e.is30 = zq_compile_is30();
        e.note.clear();
        e.error.clear();
        e.want = WANT_NONE;
        e.running = e.finished = false;
    }

    const int layer = zcui::add_layer(dialog_layer, NULL, 600);
    zcui::begin_session();

    for(;;)
    {
        zcui::wait_frame(100);
        int want;
        bool done;
        {
            zcui::Lock lock;
            done = g_cd.close;
            want = g_cd.want;
            g_cd.want = WANT_NONE;

            if(g_cd.running && g_cd.finished)
            {
                g_cd.running = false;
                if(g_worker.joinable())
                    g_worker.join();
                g_cd.page = PAGE_RESULT;
                g_cd.error.clear();
            }
        }

        if(done || !zcui::available())
        {
            zcui::Lock lock;
            if(!g_cd.running)
                break;
            g_cd.cancel = true;
            g_cd.close = false;
            continue;
        }

        switch(want)
        {
        case WANT_COMPILE:
        {
            zcui::Lock lock;
            g_cd.result = ZqCompileResult();
            g_cd.cancel = false;
            g_cd.running = true;
            g_cd.finished = false;
            g_cd.page = PAGE_COMPILING;
            const std::string source = zScript;
            g_worker = std::thread([source]()
            {
                zq_compile_run(source, g_cd.result, &g_cd.cancel);
                zcui::Lock l2;
                g_cd.finished = true;
                zcui::request_redraw();
            });
            break;
        }

        case WANT_ASSIGN:
        {
            std::map<int, std::string> names[ZQS_COUNT];
            int mode;
            {
                zcui::Lock lock;
                mode = g_cd.assign_mode;

                for(int t = 0; t < ZQS_COUNT; ++t)
                    names[t] = mode ? zq_slot_names(t) : g_cd.names[t];
            }

            if(mode)
                zq_slots_auto(g_cd.result, mode, names);

            std::string why;
            // under the UI lock: the dialog shows the buffer and the slots this replaces
            zcui::Lock lock;
            std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

            // 3.0: the values placed scripts hold follow their variables; when some have no
            // variable of their name any more, the user says where they go first
            bool ok = zq_compile_plan_remaps(g_cd.result, names, g_cd.plan, why);
            g_cd.moved.clear();

            if(ok)
            {
                build_choices(g_cd);

                if(!g_cd.choices.empty())
                {
                    for(int t = 0; t < ZQS_COUNT; ++t)
                        g_cd.plan_names[t] = names[t];

                    g_cd.page = PAGE_REMAP;
                    g_cd.error.clear();
                    break;
                }

                ok = zq_compile_assign(g_cd.result, names, why, &g_cd.plan, &g_cd.moved);
            }

            g_cd.assign_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

            if(ok)
            {
                g_cd.page = PAGE_DONE;
                g_cd.error.clear();
            }
            else
                g_cd.error = "The scripts couldn't be assigned: " + why + ". Nothing was changed.";
            break;
        }

        case WANT_ASSIGN_REMAPPED:
        {
            // the question is answered: the assignment it was asked for
            std::string why;
            zcui::Lock lock;
            std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
            take_choices(g_cd);
            const bool ok = zq_compile_assign(g_cd.result, g_cd.plan_names, why, &g_cd.plan, &g_cd.moved);
            g_cd.assign_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            g_cd.page = ok ? PAGE_DONE : (g_cd.assign_mode ? PAGE_RESULT : PAGE_ASSIGN);
            g_cd.error = ok ? std::string() : "The scripts couldn't be assigned: " + why + ". Nothing was changed.";
            break;
        }

        case WANT_LOAD:
            do_load(g_cd);
            break;

        case WANT_EXPORT:
            do_export(g_cd);
            break;

        case WANT_SETTINGS:
        {
            zcui::Lock lock;
            load_settings(g_cd);
            g_cd.page = PAGE_SETTINGS;
            break;
        }

        case WANT_SETTINGS + 100:
        {
            std::string why;
            zcui::Lock lock;

            if(save_settings(g_cd, why))
            {
                g_cd.page = PAGE_MAIN;
                g_cd.note = "Compiler settings saved.";
            }
            else
                g_cd.error = "Can't save: " + why + ".";
            break;
        }
        }
    }

    zcui::end_session();
    zcui::remove_layer(layer);
    zcui::Lock lock;
    g_cd.open = false;
}

} // namespace

void zq_compile_dialog()
{
    run();
}

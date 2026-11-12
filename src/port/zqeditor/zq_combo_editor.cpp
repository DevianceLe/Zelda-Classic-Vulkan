// ZQuest's combo editor for 2.55 and 3.0 Nightly quests (Dear ImGui): everything such a combo
// holds, as the quest's own version holds it. 2.53 and older quests keep the 2.53 editor.
//
// The window is a UI layer (the platform thread, UI lock held) shown during a modal session that
// zq_edit_combo_newer() holds open on the game thread. It edits a working copy of the combo:
// newcombo (what 2.53's structures hold) and the Q30Combo table entry (the rest). OK puts both
// back; the section writer (zqw_combos.cpp) encodes a combo whose newer part changed again in the
// quest's own combo format, and refuses the save if that format can't hold what was set.
//
// What the quest's combo section version can't hold is shown greyed out: the type list stops at
// the generation's last type (2.55.17 or 3.0 Nightly), and fields newer than the section version
// are off. Labels and help for each type's attributes and flags are upstream's
// (zq_combo_names.cpp).
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <allegro.h>

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>

#include "zdefs.h"
#include "tiles.h"
#include "zcport/ui.h"
#include "zq_q255_store.h"
#include "zq_combo_names.h"

#include <imgui.h>
#include <imgui_internal.h>

extern newcombo *combobuf;
extern bool saved;
extern int q255_combo_section_version;     // q255_combos.cpp: the open quest's CMBO version
extern int q255_header_version[3];         // q255_basic.cpp
int select_tile(int &tile, int &flip, int type, int &cs, bool edit_cs, int exnow, bool always_use_flip);
void reset_combo_animations();
void reset_combo_animations2();
void setup_combo_animations();
void setup_combo_animations2();
bool zq255_q30_same(const Q30Combo &a, const Q30Combo &b);   // zqw_combos.cpp

namespace
{

const int PREVIEW = 64;     // the preview's size in pixels (a 16x16 combo times 4)

struct Editor
{
    bool open, close, accept, focus, busy;
    int id, cs;
    bool is30;
    int v;                  // the combo section's version
    newcombo c;
    Q30Combo x;
    int trig;               // the trigger shown
    char filter[64];        // the trigger flag filter
    bool want_tile;         // the game thread runs the tile picker
    std::vector<unsigned int> px;   // the preview, RGBA
    bool px_dirty;
    ImTextureData *tex;

    Editor() : open(false), close(false), accept(false), focus(false), busy(false), id(0), cs(0), is30(false), v(0),
               trig(0), want_tile(false), px_dirty(false), tex(NULL)
    {
        filter[0] = 0;
    }
};

Editor g_ed;

// ---- the quest version's fields -------------------------------------------------------

bool flat() { return g_ed.v <= 32; }
bool at(int v) { return g_ed.v >= v; }

// Greys out what the section version can't hold.
struct Gate
{
    bool off;
    explicit Gate(bool on) : off(!on)
    {
        if(off)
            ImGui::BeginDisabled();
    }
    ~Gate()
    {
        if(off)
            ImGui::EndDisabled();
    }
};

void help(const std::string &text)
{
    if(text.empty())
        return;

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");

    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// ---- inputs of each width -------------------------------------------------------------

float field_w() { return 110.0f * zcui::scale(); }

bool in_int(const char *label, int &v, int lo, int hi)
{
    ImGui::SetNextItemWidth(field_w());
    int t = v;
    bool ch = ImGui::InputInt(label, &t, 0, 0);
    if(t < lo) t = lo;
    if(t > hi) t = hi;
    if(ch) v = t;
    return ch;
}

template<typename T>
bool in_num(const char *label, T &v, int64_t lo, int64_t hi)
{
    int t = (int)v;
    if(!in_int(label, t, (int)(lo < -2147483647LL ? -2147483647LL : lo), (int)(hi > 2147483647LL ? 2147483647LL : hi)))
        return false;
    v = (T)t;
    return true;
}

bool in_u8(const char *l, byte &v) { return in_num(l, v, 0, 255); }
bool in_s8(const char *l, signed char &v) { return in_num(l, v, -128, 127); }
bool in_u16(const char *l, word &v) { return in_num(l, v, 0, 65535); }
bool in_s16(const char *l, short &v) { return in_num(l, v, -32768, 32767); }
bool in_s32(const char *l, long &v) { return in_num(l, v, -2147483647LL - 1, 2147483647LL); }
bool in_u32(const char *l, dword &v)
{
    ImGui::SetNextItemWidth(field_w());
    unsigned int t = v;
    bool ch = ImGui::InputScalar(l, ImGuiDataType_U32, &t);
    if(ch) v = t;
    return ch;
}

// A fixed-point value (ZScript's zfix, 1.0 = 10000), shown as a decimal.
bool in_zfix(const char *label, long &v)
{
    ImGui::SetNextItemWidth(field_w());
    double d = v / 10000.0;

    if(!ImGui::InputDouble(label, &d, 0, 0, "%.4f"))
        return false;

    double r = floor(d * 10000.0 + 0.5);
    if(r > 2147483647.0) r = 2147483647.0;
    if(r < -2147483648.0) r = -2147483648.0;
    v = (long)r;
    return true;
}

bool check(const char *label, dword &flags, dword bit)
{
    bool b = (flags & bit) != 0;

    if(!ImGui::Checkbox(label, &b))
        return false;

    flags = b ? (flags | bit) : (flags & ~bit);
    return true;
}

template<typename T>
bool check_t(const char *label, T &flags, unsigned bit)
{
    dword f = flags;
    if(!check(label, f, bit))
        return false;
    flags = (T)f;
    return true;
}

// A bitstring as the indices of its set bits ("1, 5, 12").
bool in_bits(const char *label, Q30Bits &b)
{
    std::string s;

    for(size_t i = 0; i < b.v.size() * 8; ++i)
        if(b.get(i))
        {
            if(!s.empty())
                s += ", ";
            s += std::to_string(i);
        }

    char buf[512];
    snprintf(buf, sizeof(buf), "%s", s.c_str());
    ImGui::SetNextItemWidth(field_w() * 2);

    if(!ImGui::InputText(label, buf, sizeof(buf)))
        return false;

    b.v.clear();
    const char *p = buf;

    while(*p)
    {
        char *end = NULL;
        long n = strtol(p, &end, 10);

        if(end == p)
        {
            ++p;
            continue;
        }

        if(n >= 0 && n < 65535 * 8)
            b.set((size_t)n);

        p = end;
    }

    return true;
}

// ---- tabs ---------------------------------------------------------------------------------

byte narrow_type(byte t) { return t < cMAX ? t : (byte)cNONE; }
byte narrow_flag(byte f) { return f == 104 ? (byte)mfPUSHED : (f >= mfMAX ? (byte)mfNONE : f); }

// newcombo's copies of the type, flag and walk follow the table's
void sync_narrowed()
{
    g_ed.c.type = narrow_type(g_ed.x.type);
    g_ed.c.flag = narrow_flag(g_ed.x.flag);
    g_ed.c.walk = (byte)(g_ed.x.walk & 0x0F);
}

void type_combo()
{
    const int n = q30_ctype_count(g_ed.is30);
    char cur[300];
    snprintf(cur, sizeof(cur), "%d: %s", g_ed.x.type, q30_ctype_name(g_ed.x.type));
    ImGui::SetNextItemWidth(field_w() * 2.6f);

    if(ImGui::BeginCombo("Type", cur, ImGuiComboFlags_HeightLarge))
    {
        for(int t = 0; t < n; ++t)
        {
            const char *name = q30_ctype_name(t);

            if(name[0] == '-' && t != g_ed.x.type)
                continue;

            char item[300];
            snprintf(item, sizeof(item), "%d: %s", t, name);

            if(ImGui::Selectable(item, t == g_ed.x.type))
            {
                g_ed.x.type = (byte)t;
                sync_narrowed();
            }

            if(t == g_ed.x.type)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
    }
}

void flag_combo()
{
    const int n = q30_mflag_count(g_ed.is30);
    char cur[300];
    snprintf(cur, sizeof(cur), "%d: %s", g_ed.x.flag, q30_mflag_name(g_ed.x.flag));
    ImGui::SetNextItemWidth(field_w() * 2.6f);

    if(ImGui::BeginCombo("Inherent Flag", cur, ImGuiComboFlags_HeightLarge))
    {
        for(int f = 0; f < n && f < 256; ++f)
        {
            const char *name = q30_mflag_name(f);

            if(name[0] == '-' && f != g_ed.x.flag)
                continue;

            char item[300];
            snprintf(item, sizeof(item), "%d: %s", f, name);

            if(ImGui::Selectable(item, f == g_ed.x.flag))
            {
                g_ed.x.flag = (byte)f;
                sync_narrowed();
            }
        }

        ImGui::EndCombo();
    }
}

void tab_basic()
{
    newcombo &c = g_ed.c;
    Q30Combo &x = g_ed.x;

    type_combo();
    help(q30_ctype_help(x.type));
    flag_combo();

    {
        Gate g(flat() ? at(12) : true);
        char label[256];
        snprintf(label, sizeof(label), "%s", x.label.c_str());
        ImGui::SetNextItemWidth(field_w() * 2.6f);

        if(ImGui::InputText("Label", label, (at(41) && !flat()) ? 256 : 12))
            x.label = label;
    }

    ImGui::SeparatorText("Solidity");
    static const char *corner[4] = { "Top-left", "Bottom-left", "Top-right", "Bottom-right" };

    if(ImGui::BeginTable("walk", 3, ImGuiTableFlags_SizingFixedFit))
    {
        for(int q = 0; q < 4; ++q)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(corner[q]);
            ImGui::TableNextColumn();
            ImGui::PushID(q);

            if(check_t("Solid", x.walk, 1u << q))
                sync_narrowed();

            ImGui::TableNextColumn();
            {
                Gate g(!flat() || at(18));
                check_t("Effect", x.walk, 0x10u << q);
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    help("Effect: where the combo's type takes effect (upstream's 'effect squares').");

    ImGui::SeparatorText("CSet 2");
    int off = c.csets & 0x0F;
    if(off & 8) off -= 16;

    if(in_int("CSet offset", off, -8, 7))
        c.csets = (byte)((c.csets & 0xF0) | (off & 0x0F));

    for(int q = 0; q < 4; ++q)
    {
        ImGui::PushID(100 + q);
        if(q) ImGui::SameLine();
        check_t(corner[q], c.csets, 0x10u << q);
        ImGui::PopID();
    }
}

void tab_animation()
{
    newcombo &c = g_ed.c;
    Q30Combo &x = g_ed.x;

    in_u8("Frames", c.frames);
    in_u8("Speed", c.speed);
    in_u8("Skip X", c.skipanim);
    in_u8("Skip Y", c.skipanimy);
    in_u16("Next combo", c.nextcombo);
    in_u8("Next CSet", c.nextcset);

    if(flat())
        in_u16("Next timer", c.nexttimer);

    ImGui::SeparatorText("Animation flags");
    static const char *af[7] = { "Refresh animation on room entry", "Cycle to next combo", "Cycle keeps CSet",
                                 "Transparent", "Cycle the combo under", "Editor only", "Overhead" };

    for(int q = 0; q < 7; ++q)
        check_t(af[q], c.animflags, 1u << q);

    ImGui::SeparatorText("Timing");
    Gate g(!flat() && at(73));
    in_u16("Delay", x.delay);
    help("Frames to wait before the animation starts.");
    in_u8("CSet 2 flash duration", x.cs2_flash_duration);
}

std::string g_la[24], g_ha[24], g_lf[16], g_hf[16];

void tab_attributes()
{
    Q30Combo &x = g_ed.x;
    q30_combo_labels(x, g_la, g_ha, g_lf, g_hf);

    if(!flat() && at(63))
    {
        for(int q = 0; q < 24; ++q)
        {
            ImGui::PushID(q);
            in_zfix(g_la[q].c_str(), x.attributes[q]);
            help(g_ha[q]);
            ImGui::PopID();
        }

        return;
    }

    // before v63: four fixed-point attributes, eight byte-sized and eight short ones, which
    // 3.0 numbers 0-3, 8-15 and 16-23; unnamed ones are called what 2.55 calls them
    for(int q = 0; q < 16; ++q)
    {
        const int i = 8 + q;

        if(g_la[i] == "Attributes[" + std::to_string(i) + "]:")
            g_la[i] = (q < 8 ? "Attribytes[" : "Attrishorts[") + std::to_string(q % 8) + "]:";
    }

    ImGui::SeparatorText("Attributes");
    {
        Gate g(at(8));

        for(int q = 0; q < 4; ++q)
        {
            ImGui::PushID(q);
            in_zfix(g_la[q].c_str(), x.attributes[q]);
            help(g_ha[q]);
            ImGui::PopID();
        }
    }

    ImGui::SeparatorText("Attribytes");

    for(int q = 0; q < 8; ++q)
    {
        Gate g(!flat() || at(q < 4 ? 13 : 17));
        ImGui::PushID(8 + q);
        int b = (int)(x.attributes[8 + q] / 10000);

        if(in_int(g_la[8 + q].c_str(), b, 0, 255))
            x.attributes[8 + q] = (long)b * 10000;

        help(g_ha[8 + q]);
        ImGui::PopID();
    }

    ImGui::SeparatorText("Attrishorts");

    for(int q = 0; q < 8; ++q)
    {
        Gate g(!flat() || at(17));
        ImGui::PushID(16 + q);
        int s = (int)(x.attributes[16 + q] / 10000);

        if(in_int(g_la[16 + q].c_str(), s, -32768, 32767))
            x.attributes[16 + q] = (long)s * 10000;

        help(g_ha[16 + q]);
        ImGui::PopID();
    }
}

void tab_flags()
{
    Q30Combo &x = g_ed.x;
    q30_combo_labels(x, g_la, g_ha, g_lf, g_hf);
    ImGui::SeparatorText("Flags");
    {
        Gate g(at(8));

        for(int q = 0; q < 16; ++q)
        {
            ImGui::PushID(q);
            check(g_lf[q].c_str(), x.usrflags, 1u << q);
            help(g_hf[q]);
            ImGui::PopID();
        }
    }

    ImGui::SeparatorText("General flags");
    static const char *gen[5] = { "Hook-Grabbable", "Switch-Hookable", "Allow walk-on-top", "Walk-on-top -8px DrawYOffset",
                                  "Step 'in front' in sideview" };
    static const char *gen_help[5] =
    {
        "Solid parts of this combo can be grabbed by the Hookshot",
        "Solid parts of this combo can be grabbed by the SwitchHook",
        "The Player can walk along the top of this combo's solid parts if they are on it, or above the combo's 'Z Height'.",
        "While the Player is walking on top of this combo, they will be visually offset up by 8 pixels.",
        "The effects of 'landing'/'standing'/'walking' on this combo will occur when landing 'in front' of the combo in sideview, rather than 'on top' of the combo."
    };
    Gate g(flat() ? at(20) : true);

    for(int q = 0; q < 5; ++q)
    {
        ImGui::PushID(200 + q);
        check_t(gen[q], x.genflags, 1u << q);
        help(gen_help[q]);
        ImGui::PopID();
    }
}

// ---- triggers -------------------------------------------------------------------------

void trigger_flags(Q30Trigger &t)
{
    ImGui::SetNextItemWidth(field_w() * 2);
    ImGui::InputTextWithHint("##filter", "Filter flags", g_ed.filter, sizeof(g_ed.filter));
    const int words = flat() ? 3 : (g_ed.v < 36 ? 3 : (g_ed.v < 57 ? 6 : 1000));   // bits the version holds: 32 per word
    std::string f = g_ed.filter;

    for(size_t k = 0; k < f.size(); ++k)
        f[k] = (char)tolower((unsigned char)f[k]);

    if(ImGui::BeginChild("tflags", ImVec2(0, 220.0f * zcui::scale()), ImGuiChildFlags_Borders))
    {
        if(ImGui::BeginTable("tf", 3))
        {
            for(int q = 0; q < q30_trigflag_count(); ++q)
            {
                std::string name = q30_trigflag_name(q);
                std::string low = name;

                for(size_t k = 0; k < low.size(); ++k)
                    low[k] = (char)tolower((unsigned char)low[k]);

                if(!f.empty() && low.find(f) == std::string::npos)
                    continue;

                ImGui::TableNextColumn();
                Gate g(q < words * 32);
                bool b = t.flags.get(q);
                ImGui::PushID(q);

                if(ImGui::Checkbox(name.c_str(), &b))
                {
                    if(b)
                        t.flags.set(q);
                    else if((size_t)q / 8 < t.flags.v.size())
                        t.flags.v[q / 8] &= (byte)~(1 << (q % 8));
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    ImGui::EndChild();
}

// Field rows, two to a line.
struct Rows
{
    Rows() { ImGui::BeginTable("rows", 2, ImGuiTableFlags_SizingStretchSame); }
    ~Rows() { ImGui::EndTable(); }
    void next() { ImGui::TableNextColumn(); }
};

// The trigger fields, each with the combo version it appeared in (old flat records hold fewer).
void trigger_fields(Q30Trigger &t)
{
    const bool fl = flat();
    // A field is there from 'nv' in the grouped format and from 'fv' in the flat one (0: never).
#define AV(nv, fv) Gate gate_((fl) ? ((fv) && at(fv)) : at(nv))

    if(ImGui::CollapsingHeader("Conditions and causes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Rows r;
        { r.next(); AV(33, 9); in_s32("Min weapon level", t.triggerlevel); }
        { r.next(); AV(33, 22); in_u8("Button (bits)", t.triggerbtn); }
        { r.next(); AV(33, 24); in_u16("Item", t.triggeritem); }
        { r.next(); AV(33, 24); in_u16("Timer", t.trigtimer); }
        { r.next(); AV(33, 29); in_u16("Proximity", t.trigprox); }
        { r.next(); AV(33, 29); in_s16("Counter", t.trigctr); }
        { r.next(); AV(33, 29); in_s32("Counter amount", t.trigctramnt); }
        { r.next(); AV(33, 30); in_u8("Light beam", t.triglbeam); }
        { r.next(); AV(33, 32); in_u8("Cooldown", t.trigcooldown); }
        { r.next(); AV(33, 31); in_u8("Copycat ID", t.trigcopycat); }
        { r.next(); AV(36, 0); in_u8("Level state", t.trig_lstate); }
        { r.next(); AV(36, 0); in_u8("Global state", t.trig_gstate); }
        { r.next(); AV(36, 0); in_s32("State timer", t.trig_statetime); }
        { r.next(); AV(38, 0); in_u8("Trigger group", t.trig_group); }
        { r.next(); AV(38, 0); in_u16("Group value", t.trig_group_val); }
        { r.next(); AV(46, 0); in_u16("Level items", t.trig_levelitems); }
        { r.next(); AV(46, 0); in_s16("DMap level", t.trigdmlevel); }
        { r.next(); AV(53, 0); in_u32("Req. level states", t.req_level_state); }
        { r.next(); AV(53, 0); in_u32("Unreq. level states", t.unreq_level_state); }
        { r.next(); AV(53, 0); in_s32("Req. player Z", t.req_player_z); }
        { r.next(); AV(54, 0); in_u8("Req. player dir", t.req_player_dir); }
        { r.next(); AV(54, 0); in_s32("Req. jump", t.req_player_jump); }
        { r.next(); AV(54, 0); in_s32("Req. player X", t.req_player_x); }
        { r.next(); AV(54, 0); in_s32("Req. player Y", t.req_player_y); }
        { r.next(); AV(68, 0); in_s16("Viewport range", t.viewport_cond_range); }
        { r.next(); AV(68, 0); in_s32("Chance (numerator)", t.chance_numerator); }
        { r.next(); AV(68, 0); in_s32("Chance (denominator)", t.chance_denominator); }
        { r.next(); AV(70, 0); in_u8("Combo position state", t.combopos_state); }
        { r.next(); AV(71, 0); in_u8("Large combo copycat", t.large_combo_copycat); }
    }

    if(ImGui::CollapsingHeader("States"))
    {
        { AV(53, 0); in_bits("Req. global states", t.req_global_state); }
        { AV(53, 0); in_bits("Unreq. global states", t.unreq_global_state); }
        { AV(61, 0); in_bits("Req. screen states", t.req_screen_state); }
        { AV(61, 0); in_bits("Unreq. screen states", t.unreq_screen_state); }
        { AV(61, 0); in_bits("Req. screen ExStates", t.req_screen_ex_state); }
        { AV(61, 0); in_bits("Unreq. screen ExStates", t.unreq_screen_ex_state); }
        {
            AV(61, 0);
            in_u8("State map", t.trigstatemap);
            in_u8("State screen", t.trigstatescreen);
        }

        static const char *dirs[4] = { "up", "down", "left", "right" };

        for(int d = 0; d < 4; ++d)
        {
            AV(69, 0);
            char l[64];
            ImGui::PushID(300 + d);
            snprintf(l, sizeof(l), "Req. ExDoors (%s)", dirs[d]);
            in_bits(l, t.req_screen_ex_door[d]);
            snprintf(l, sizeof(l), "Unreq. ExDoors (%s)", dirs[d]);
            in_bits(l, t.unreq_screen_ex_door[d]);
            ImGui::PopID();
        }
    }

    if(ImGui::CollapsingHeader("Effects", ImGuiTreeNodeFlags_DefaultOpen))
    {
        Rows r;
        { r.next(); AV(33, 25); in_u16("Sound", t.trigsfx); }
        { r.next(); AV(33, 27); in_s32("Combo change", t.trigchange); }
        { r.next(); AV(33, 31); in_s8("CSet change", t.trigcschange); }
        { r.next(); AV(33, 31); in_s32("Spawn item", t.spawnitem); }
        { r.next(); AV(33, 31); in_s16("Spawn enemy", t.spawnenemy); }
        { r.next(); AV(33, 31); in_s8("ExState", t.exstate); }
        { r.next(); AV(33, 31); in_s32("Item pickup flags", t.spawnip); }
        { r.next(); AV(37, 0); in_u16("Generic script", t.trig_genscr); }
        { r.next(); AV(45, 0); in_s8("ExDoor dir", t.exdoor_dir); }
        { r.next(); AV(45, 0); in_u8("ExDoor index", t.exdoor_ind); }
        { r.next(); AV(46, 0); in_s16("Tint red", t.trigtint[0]); }
        { r.next(); AV(46, 0); in_s16("Tint green", t.trigtint[1]); }
        { r.next(); AV(46, 0); in_s16("Tint blue", t.trigtint[2]); }
        { r.next(); AV(46, 0); in_s16("Level palette", t.triglvlpalette); }
        { r.next(); AV(46, 0); in_s16("Boss palette", t.trigbosspalette); }
        { r.next(); AV(46, 0); in_s16("Quake time", t.trigquaketime); }
        { r.next(); AV(46, 0); in_s16("Wavy time", t.trigwavytime); }
        { r.next(); AV(46, 0); in_s16("Sword jinx time", t.trig_swjinxtime); }
        { r.next(); AV(46, 0); in_s16("Item jinx time", t.trig_itmjinxtime); }
        { r.next(); AV(47, 0); in_s16("Shield jinx time", t.trig_shieldjinxtime); }
        { r.next(); AV(46, 0); in_s16("Stun time", t.trig_stuntime); }
        { r.next(); AV(46, 0); in_s16("Bunny time", t.trig_bunnytime); }
        { r.next(); AV(46, 0); in_u8("Push time", t.trig_pushtime); }
        { r.next(); AV(53, 0); in_s32("Message", t.trig_msgstr); }
        { r.next(); AV(53, 0); in_s32("Fail message", t.fail_msgstr); }
        { r.next(); AV(53, 0); in_s32("Player bounce", t.player_bounce); }
        { r.next(); AV(54, 0); in_s32("Set player X", t.dest_player_x); }
        { r.next(); AV(54, 0); in_s32("Set player Y", t.dest_player_y); }
        { r.next(); AV(54, 0); in_s32("Set player Z", t.dest_player_z); }
        { r.next(); AV(56, 0); in_s8("Set player dir", t.dest_player_dir); }
        { r.next(); AV(56, 0); in_s32("Force ice combo", t.force_ice_combo); }
        { r.next(); AV(56, 0); in_s32("Force ice X speed", t.force_ice_vx); }
        { r.next(); AV(56, 0); in_s32("Force ice Y speed", t.force_ice_vy); }
        { r.next(); AV(58, 0); in_s32("Gravity", t.trig_gravity); }
        { r.next(); AV(58, 0); in_s32("Terminal velocity", t.trig_terminal_v); }
        { r.next(); AV(62, 0); in_s32("Play music", t.play_music); }
        { r.next(); AV(62, 0); in_s8("Music refresh", t.set_music_refresh); }
        { r.next(); AV(68, 0); in_s16("Sound pan", t.sfx_pan); }
        { r.next(); AV(68, 0); in_u8("Sound volume", t.sfx_volume); }
        { r.next(); AV(68, 0); in_s32("Sound frequency", t.sfx_frequency); }
    }

    if(ImGui::CollapsingHeader("Button prompt"))
    {
        Rows r;
        { r.next(); AV(35, 0); in_u16("Prompt combo", t.prompt_cid); }
        { r.next(); AV(35, 0); in_u8("Prompt CSet", t.prompt_cs); }
        { r.next(); AV(35, 0); in_s16("Prompt X", t.prompt_x); }
        { r.next(); AV(35, 0); in_s16("Prompt Y", t.prompt_y); }
        { r.next(); AV(53, 0); in_u16("Fail prompt combo", t.fail_prompt_cid); }
        { r.next(); AV(53, 0); in_u8("Fail prompt CSet", t.fail_prompt_cs); }
    }
#undef AV
}

void tab_triggers()
{
    Q30Combo &x = g_ed.x;
    const bool many = !flat() && at(52);
    const bool any = flat() ? at(9) : true;

    if(!any)
    {
        ImGui::TextWrapped("This quest's combo format has no triggers.");
        return;
    }

    if(g_ed.trig >= (int)x.triggers.size())
        g_ed.trig = (int)x.triggers.size() - 1;

    if(g_ed.trig < 0)
        g_ed.trig = 0;

    {
        Gate g(many || x.triggers.empty());

        if(ImGui::Button("Add"))
        {
            x.triggers.push_back(Q30Trigger());
            g_ed.trig = (int)x.triggers.size() - 1;
        }
    }

    ImGui::SameLine();
    {
        Gate g(!x.triggers.empty() && (many || x.triggers.size() > 1));

        if(ImGui::Button("Duplicate") && !x.triggers.empty())
        {
            x.triggers.insert(x.triggers.begin() + g_ed.trig + 1, x.triggers[g_ed.trig]);
            ++g_ed.trig;
        }
    }

    ImGui::SameLine();
    {
        Gate g(!x.triggers.empty());

        if(ImGui::Button("Remove") && !x.triggers.empty())
        {
            x.triggers.erase(x.triggers.begin() + g_ed.trig);
            if(g_ed.trig > 0) --g_ed.trig;
        }
    }

    ImGui::SameLine();
    {
        Gate g(g_ed.trig > 0);

        if(ImGui::ArrowButton("up", ImGuiDir_Up) && g_ed.trig > 0)
        {
            std::swap(x.triggers[g_ed.trig], x.triggers[g_ed.trig - 1]);
            --g_ed.trig;
        }
    }

    ImGui::SameLine();
    {
        Gate g(g_ed.trig + 1 < (int)x.triggers.size());

        if(ImGui::ArrowButton("down", ImGuiDir_Down) && g_ed.trig + 1 < (int)x.triggers.size())
        {
            std::swap(x.triggers[g_ed.trig], x.triggers[g_ed.trig + 1]);
            ++g_ed.trig;
        }
    }

    if(!many)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(this quest's combo format holds one trigger)");
    }

    {
        Gate g(!flat() && at(52));
        bool og = x.only_gentrig != 0;

        if(ImGui::Checkbox("Only generic triggers", &og))
            x.only_gentrig = og ? 1 : 0;
    }

    if(x.triggers.empty())
        return;

    if(ImGui::BeginListBox("##triggers", ImVec2(-FLT_MIN, 4.5f * ImGui::GetTextLineHeightWithSpacing())))
    {
        for(int q = 0; q < (int)x.triggers.size(); ++q)
        {
            char l[300];
            snprintf(l, sizeof(l), "%d: %s", q, x.triggers[q].label.empty() ? "(no label)" : x.triggers[q].label.c_str());

            if(ImGui::Selectable(l, q == g_ed.trig))
                g_ed.trig = q;
        }

        ImGui::EndListBox();
    }

    Q30Trigger &t = x.triggers[g_ed.trig];
    ImGui::PushID(g_ed.trig);
    {
        Gate g(!flat() && at(52));
        char label[256];
        snprintf(label, sizeof(label), "%s", t.label.c_str());
        ImGui::SetNextItemWidth(field_w() * 2.6f);

        if(ImGui::InputText("Trigger label", label, sizeof(label)))
            t.label = label;
    }

    if(ImGui::CollapsingHeader("Flags", ImGuiTreeNodeFlags_DefaultOpen))
        trigger_flags(t);

    trigger_fields(t);
    ImGui::PopID();
}

void tab_lifting()
{
    Q30Combo &x = g_ed.x;

    if(flat())
    {
        ImGui::TextWrapped("This quest's combo format has no lifting data.");
        return;
    }

    Rows r;
    { r.next(); in_u16("Lift combo", x.liftcmb); }
    { r.next(); in_u8("Lift CSet", x.liftcs); }
    { r.next(); in_u16("Under combo", x.liftundercmb); }
    { r.next(); in_u8("Under CSet", x.liftundercs); }
    { r.next(); in_u8("Damage", x.liftdmg); }
    { r.next(); in_u8("Glove level", x.liftlvl); }
    { r.next(); in_u8("Item", x.liftitm); }
    { r.next(); in_u8("Lift flags (bits)", x.liftflags); }
    { r.next(); in_u8("Graphics (0 this, 1 other, 2 sprite)", x.liftgfx); }
    { r.next(); Gate g(at(66) || x.liftsprite < 256); in_u16("Sprite", x.liftsprite); }
    { r.next(); in_u16("Sound", x.liftsfx); }
    { r.next(); in_s16("Break sprite", x.liftbreaksprite); }
    { r.next(); in_u8("Break sound", x.liftbreaksfx); }
    { r.next(); Gate g(at(34)); in_u8("Height", x.lifthei); }
    { r.next(); Gate g(at(34)); in_u8("Time", x.lifttime); }
    { r.next(); Gate g(at(39)); in_u16("Parent item", x.lift_parent_item); }
}

void tab_general()
{
    Q30Combo &x = g_ed.x;

    if(flat())
    {
        ImGui::TextWrapped("This quest's combo format has no general data (speed, sounds, sprites).");
        return;
    }

    ImGui::SeparatorText("Movement");
    {
        Rows r;
        { r.next(); in_u8("Speed multiplier", x.speed_mult); }
        { r.next(); in_u8("Speed divisor", x.speed_div); }
        { r.next(); in_zfix("Speed added", x.speed_add); }
        { r.next(); Gate g(at(56)); in_zfix("Z height", x.z_height); }
        { r.next(); Gate g(at(56)); in_zfix("Z step height", x.z_step_height); }
        { r.next(); Gate g(at(60)); in_u8("Dive under level", x.dive_under_level); }
        { r.next(); Gate g(at(71)); in_u8("Large combo dirs (bits)", x.large_combo_dirs); }
    }

    ImGui::SeparatorText("Sounds");
    {
        Rows r;
        { r.next(); Gate g(at(42)); in_u16("Appear", x.sfx_appear); }
        { r.next(); Gate g(at(42)); in_u16("Disappear", x.sfx_disappear); }
        { r.next(); Gate g(at(42)); in_u16("Loop", x.sfx_loop); }
        { r.next(); Gate g(at(42)); in_u16("Walking", x.sfx_walking); }
        { r.next(); Gate g(at(42)); in_u16("Standing", x.sfx_standing); }
        { r.next(); Gate g(at(44)); in_u16("Tap", x.sfx_tap); }
        { r.next(); Gate g(at(49)); in_u16("Landing", x.sfx_landing); }
        { r.next(); Gate g(at(50)); in_u16("Falling", x.sfx_falling); }
        { r.next(); Gate g(at(50)); in_u16("Drowning", x.sfx_drowning); }
        { r.next(); Gate g(at(50)); in_u16("Lava drowning", x.sfx_lava_drowning); }
    }

    ImGui::SeparatorText("Sprites");
    {
        Rows r;
        { r.next(); Gate g(at(42)); in_u16("Appear##s", x.spr_appear); }
        { r.next(); Gate g(at(42)); in_u16("Disappear##s", x.spr_disappear); }
        { r.next(); Gate g(at(42)); in_u16("Walking##s", x.spr_walking); }
        { r.next(); Gate g(at(42)); in_u16("Standing##s", x.spr_standing); }
        { r.next(); Gate g(at(50)); in_u16("Falling##s", x.spr_falling); }
        { r.next(); Gate g(at(50)); in_u16("Drowning##s", x.spr_drowning); }
        { r.next(); Gate g(at(50)); in_u16("Lava drowning##s", x.spr_lava_drowning); }
    }
}

void tab_script()
{
    Q30Combo &x = g_ed.x;
    Gate g(flat() ? at(14) : true);
    in_u16("Combo script", x.script);
    help("The combo script's slot number.");
    const int n = flat() ? 2 : (at(43) ? 8 : 2);

    for(int q = 0; q < 8; ++q)
    {
        Gate h(q < n);
        char l[32];
        snprintf(l, sizeof(l), "InitD[%d]", q);
        in_zfix(l, x.initd[q]);
    }
}

// ---- the window -----------------------------------------------------------------------

void preview_image()
{
    Editor &e = g_ed;

    if(!e.tex)
    {
        e.tex = IM_NEW(ImTextureData)();
        e.tex->Create(ImTextureFormat_RGBA32, PREVIEW, PREVIEW);
        ImGui::RegisterUserTexture(e.tex);
        e.px_dirty = true;
    }

    if(e.px_dirty && e.px.size() == (size_t)PREVIEW * PREVIEW)
    {
        memcpy(e.tex->GetPixels(), &e.px[0], e.px.size() * 4);

        if(e.tex->Status == ImTextureStatus_OK || e.tex->Status == ImTextureStatus_WantUpdates)
            ImTextureDataQueueUpload(e.tex, 0, 0, PREVIEW, PREVIEW);

        e.px_dirty = false;
    }

    const float s = PREVIEW * zcui::scale();
    ImGui::Image(e.tex->GetTexRef(), ImVec2(s, s));
}

void editor_layer(void *)
{
    Editor &e = g_ed;

    if(!e.open || e.busy)
        return;

    const float sc = zcui::scale();
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760 * sc, 600 * sc), ImGuiCond_Appearing);

    if(e.focus)
    {
        ImGui::SetNextWindowFocus();
        e.focus = false;
    }

    char title[96];
    snprintf(title, sizeof(title), "Combo %d (%s, combo format %d)###zqcombo30", e.id, e.is30 ? "3.0 Nightly" : "2.55",
             e.v);
    bool open = true;

    if(ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        preview_image();
        ImGui::SameLine();
        ImGui::BeginGroup();
        {
            int tile = (int)e.c.tile;

            if(in_int("Tile", tile, 0, NEWMAXTILES - 1))
                e.c.tile = (tile_id)tile;

            ImGui::SameLine();

            if(ImGui::Button("Pick..."))
                e.want_tile = true;

            int flip = e.c.flip;

            if(in_int("Flip (0-3)", flip, 0, 3))
                e.c.flip = (byte)flip;

            in_int("Preview CSet", e.cs, 0, 15);
        }
        ImGui::EndGroup();

        const float bottom = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

        if(ImGui::BeginChild("body", ImVec2(0, -bottom)))
        {
            if(ImGui::BeginTabBar("tabs"))
            {
                if(ImGui::BeginTabItem("Basic")) { tab_basic(); ImGui::EndTabItem(); }
                if(ImGui::BeginTabItem("Animation")) { tab_animation(); ImGui::EndTabItem(); }
                if(ImGui::BeginTabItem("Attributes")) { tab_attributes(); ImGui::EndTabItem(); }
                if(ImGui::BeginTabItem("Flags")) { tab_flags(); ImGui::EndTabItem(); }
                if(ImGui::BeginTabItem("Triggers")) { tab_triggers(); ImGui::EndTabItem(); }
                if(ImGui::BeginTabItem("Lifting")) { tab_lifting(); ImGui::EndTabItem(); }
                if(ImGui::BeginTabItem("General")) { tab_general(); ImGui::EndTabItem(); }
                if(ImGui::BeginTabItem("Script")) { tab_script(); ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
        }

        ImGui::EndChild();

        if(ImGui::Button("OK", ImVec2(90 * sc, 0)))
        {
            e.accept = true;
            e.close = true;
        }

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

// The game thread: the combo drawn as it is in the window, for the preview.
void draw_preview(int id, const newcombo &c, int cs)
{
    BITMAP *b = create_bitmap_ex(8, 16, 16);

    if(!b)
        return;

    clear_to_color(b, 0);
    const newcombo keep = combobuf[id];
    combobuf[id] = c;
    putcombo(b, 0, 0, id, cs);
    combobuf[id] = keep;

    PALETTE pal;
    get_palette(pal);
    std::vector<unsigned int> px((size_t)PREVIEW * PREVIEW);

    for(int y = 0; y < PREVIEW; ++y)
        for(int x = 0; x < PREVIEW; ++x)
        {
            const int i = b->line[y / 4][x / 4];
#ifdef ZC_ENGINE
            const int r = pal[i].r, gg = pal[i].g, bl = pal[i].b;
#else
            const int r = (pal[i].r << 2) | (pal[i].r >> 4), gg = (pal[i].g << 2) | (pal[i].g >> 4),
                      bl = (pal[i].b << 2) | (pal[i].b >> 4);
#endif
            px[(size_t)y * PREVIEW + x] = 0xFF000000u | ((unsigned)bl << 16) | ((unsigned)gg << 8) | (unsigned)r;
        }

    destroy_bitmap(b);
    zcui::Lock lock;
    g_ed.px.swap(px);
    g_ed.px_dirty = true;
}

} // namespace

// The editor for a combo of the open 2.55 or 3.0 quest. True when OK was pressed.
bool zq_edit_combo_newer(int id, int cs)
{
    if(id < 0 || id >= MAXCOMBOS)
        return false;

    if(!zcui::available())
    {
        al_trace("Combo editor: the UI is not available\n");
        return false;
    }

    {
        zcui::Lock lock;

        if(g_ed.open)
            return false;

        g_ed.open = true;
        g_ed.close = g_ed.accept = g_ed.busy = g_ed.want_tile = false;
        g_ed.focus = true;
        g_ed.id = id;
        g_ed.cs = cs & 15;
        g_ed.is30 = q255_header_version[0] >= 3;
        g_ed.v = q255_combo_section_version;
        g_ed.c = combobuf[id];
        g_ed.x = zq255.combos[id] ? *zq255.combos[id] : Q30Combo();
        g_ed.trig = 0;
        g_ed.filter[0] = 0;

        if(!zq255.combos[id])
        {
            // a combo with no entry holds 2.53's numbers and upstream's defaults otherwise
            g_ed.x.type = g_ed.c.type;
            g_ed.x.flag = g_ed.c.flag == mfPUSHED ? (byte)104 : g_ed.c.flag;
            g_ed.x.walk = (byte)(0xF0 | (g_ed.c.walk & 0x0F));
        }
    }

    const int layer = zcui::add_layer(editor_layer, NULL, 600);
    zcui::begin_session();
    newcombo shown;
    memset(&shown, 0xFF, sizeof(shown));
    int shown_cs = -1;

    for(;;)
    {
        zcui::wait_frame(100);
        bool done, want_tile;
        newcombo c;
        int pcs;
        {
            zcui::Lock lock;
            done = g_ed.close;
            want_tile = g_ed.want_tile;
            g_ed.want_tile = false;
            c = g_ed.c;
            pcs = g_ed.cs;
        }

        if(done || !zcui::available())
            break;

        if(want_tile)
        {
            {
                zcui::Lock lock;
                g_ed.busy = true;
            }

            int t = (int)c.tile, f = c.flip, tcs = pcs;

            if(select_tile(t, f, 1, tcs, true, 0, false))
            {
                zcui::Lock lock;
                g_ed.c.tile = (tile_id)t;
                g_ed.c.flip = (byte)f;
                g_ed.cs = tcs;
            }

            zcui::Lock lock;
            g_ed.busy = false;
            g_ed.focus = true;
            continue;
        }

        if(memcmp(&c, &shown, sizeof(c)) != 0 || pcs != shown_cs)
        {
            draw_preview(id, c, pcs);
            shown = c;
            shown_cs = pcs;
        }
    }

    zcui::end_session();
    zcui::remove_layer(layer);

    bool accepted;
    {
        zcui::Lock lock;
        accepted = g_ed.accept;
        g_ed.open = false;

        if(accepted)
        {
            sync_narrowed();
            const Q30Combo before = zq255.combos[id] ? *zq255.combos[id] : Q30Combo();
            // what a combo with no entry reads as: newcombo's numbers and upstream's defaults
            Q30Combo plain = Q30Combo();
            plain.type = g_ed.c.type;
            plain.flag = g_ed.c.flag == mfPUSHED ? (byte)104 : g_ed.c.flag;
            plain.walk = (byte)(0xF0 | (g_ed.c.walk & 0x0F));

            if(memcmp(&combobuf[id], &g_ed.c, sizeof(newcombo)) != 0)
            {
                combobuf[id] = g_ed.c;
                saved = false;
            }

            // A combo with no entry gets one only when something past newcombo was set.
            const bool had = zq255.combos[id] != NULL;

            if(had ? !zq255_q30_same(before, g_ed.x) : !zq255_q30_same(plain, g_ed.x))
            {
                delete zq255.combos[id];
                zq255.combos[id] = new Q30Combo(g_ed.x);
                saved = false;
            }
        }
    }

    return accepted;
}

static void dump_one_combo(FILE *f, int id);

// -qst-dump-combo (zquest.cpp): one combo of the open quest (or, for id -1, every combo with a
// tile or newer data), its 2.53 fields and the newer ones, as text. 0 when written.
int zq_dump_combo(int id, const char *report)
{
    FILE *f = fopen(report, "w");

    if(!f)
        return 1;

    if(id == -1)
    {
        for(int i = 0; i < MAXCOMBOS; ++i)
            if(combobuf[i].tile || zq255.combos[i])
                dump_one_combo(f, i);

        fclose(f);
        return 0;
    }

    if(id < 0 || id >= MAXCOMBOS)
    {
        fprintf(f, "no combo %d\n", id);
        fclose(f);
        return 2;
    }

    dump_one_combo(f, id);
    fclose(f);
    return 0;
}

static void dump_one_combo(FILE *f, int id)
{
    const newcombo &c = combobuf[id];
    fprintf(f, "combo %d: section version %d, generation %s\n", id, q255_combo_section_version,
            !zq255_quest_is_newer() ? "2.53 or older" : q255_header_version[0] >= 3 ? "3.0" : "2.55");
    fprintf(f, "newcombo: tile %d flip %d walk 0x%02x type %d csets 0x%02x frames %d speed %d next %d/%d flag %d skip %d/%d animflags 0x%02x\n",
            (int)c.tile, c.flip, c.walk, c.type, c.csets, c.frames, c.speed, c.nextcombo, c.nextcset, c.flag, c.skipanim,
            c.skipanimy, c.animflags);
    const Q30Combo *x = zq255.combos[id];

    if(!x)
    {
        fprintf(f, "table: none\n");
        return;
    }

    fprintf(f, "table: type %d (%s) flag %d walk 0x%02x label \"%s\" script %d\n", x->type, q30_ctype_name(x->type), x->flag,
            x->walk, x->label.c_str(), x->script);
    fprintf(f, "attributes:");
    for(int q = 0; q < 24; ++q) fprintf(f, " %ld", x->attributes[q]);
    fprintf(f, "\nusrflags 0x%08lx genflags 0x%04x initd:", (unsigned long)x->usrflags, x->genflags);
    for(int q = 0; q < 8; ++q) fprintf(f, " %ld", x->initd[q]);
    fprintf(f, "\nlift: combo %d cs %d under %d/%d dmg %d lvl %d item %d flags 0x%02x gfx %d sprite %d sfx %d break %d/%d hei %d time %d parent %d\n",
            x->liftcmb, x->liftcs, x->liftundercmb, x->liftundercs, x->liftdmg, x->liftlvl, x->liftitm, x->liftflags, x->liftgfx,
            x->liftsprite, x->liftsfx, x->liftbreaksprite, x->liftbreaksfx, x->lifthei, x->lifttime, x->lift_parent_item);
    fprintf(f, "general: speed %d/%d+%ld sfx %d %d %d %d %d tap %d land %d fall %d %d %d spr %d %d %d %d fall %d %d %d z %ld %ld dive %d large %d delay %d cs2 %d\n",
            x->speed_mult, x->speed_div, x->speed_add, x->sfx_appear, x->sfx_disappear, x->sfx_loop, x->sfx_walking,
            x->sfx_standing, x->sfx_tap, x->sfx_landing, x->sfx_falling, x->sfx_drowning, x->sfx_lava_drowning, x->spr_appear,
            x->spr_disappear, x->spr_walking, x->spr_standing, x->spr_falling, x->spr_drowning, x->spr_lava_drowning,
            x->z_height, x->z_step_height, x->dive_under_level, x->large_combo_dirs, x->delay, x->cs2_flash_duration);
    fprintf(f, "triggers: %d, only generic %d\n", (int)x->triggers.size(), x->only_gentrig);

    for(size_t t = 0; t < x->triggers.size(); ++t)
    {
        const Q30Trigger &g = x->triggers[t];
        fprintf(f, "  trigger %d \"%s\": flags", (int)t, g.label.c_str());

        for(size_t b = 0; b < g.flags.v.size() * 8; ++b)
            if(g.flags.get(b))
                fprintf(f, " %d(%s)", (int)b, q30_trigflag_name((int)b));

        fprintf(f, "\n    level %ld btn %d item %d timer %d sfx %d change %ld cschange %d prox %d ctr %d/%ld spawn item %ld enemy %d exstate %d cooldown %d copycat %d msg %ld\n",
                g.triggerlevel, g.triggerbtn, g.triggeritem, g.trigtimer, g.trigsfx, g.trigchange, g.trigcschange, g.trigprox,
                g.trigctr, g.trigctramnt, g.spawnitem, g.spawnenemy, g.exstate, g.trigcooldown, g.trigcopycat, g.trig_msgstr);
    }
}

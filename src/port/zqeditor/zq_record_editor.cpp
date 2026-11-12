// ZQuest's item, enemy, DMap and screen editors for 2.55 and 3.0 Nightly quests (Dear ImGui):
// every field the record holds in the quest's own section version, grouped in tabs, with
// upstream's names for item classes, counters, enemy classes, defense outcomes and animation
// styles, and its item and enemy editors' labels for each class. 2.53 and older quests keep
// the 2.53 dialogs. Screens show only what 2.53's screen dialogs don't (the screen script,
// hidden layers, lens layers, music, notes, gravity...): those are merged into the screen as
// the rest of ZQuest leaves it when it is saved (zqw_maps.cpp).
//
// The record is a tape (zq_tape.h): every byte of it is a field, so what the version doesn't
// hold isn't offered, and an unchanged record writes back exactly as it was. OK puts the
// record in through the section's writer (zq255_item_commit / zq255_guy_commit), which reads
// it the way the player does to update ZQuest's own buffers.
//
// Written for the port (no jwin or Allegro code).

#include "precompiled.h" //always first

#include <allegro.h>

#include <algorithm>
#include <ctype.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

#include "zdefs.h"
#include "zcport/ui.h"
#include "zq_q255_store.h"
#include "zq_tape.h"
#include "zq_item_names.h"

#include <imgui.h>

extern bool saved;
extern int q255_header_version[3];
extern char *item_string[ITEMCNT];
extern char *guy_string[eMAXGUYS];
extern char *weapon_string[WPNCNT];
extern char *sfx_string[WAV_COUNT];

bool zq255_item_record(int i, std::vector<byte> &rec, int *version);         // zqw_items.cpp
bool zq255_item_commit(int i, const std::vector<byte> &rec, std::string &why);
bool zq255_guy_record(int i, std::vector<byte> &rec, int *version);          // zqw_guys.cpp
bool zq255_guy_commit(int i, const std::vector<byte> &rec, std::string &why);
bool zq255_dmap_record(int i, std::vector<byte> &rec, int *version);         // zqw_maps.cpp
bool zq255_dmap_commit(int i, const std::vector<byte> &rec, std::string &why);
bool zq255_screen_record(int index, std::vector<byte> &rec, int *version);
bool zq255_init_record(std::vector<byte> &rec, int *version);                // zqw_init.cpp
bool zq255_init_commit(const std::vector<byte> &rec, std::string &why);
bool zq255_misc_record(std::vector<byte> &rec, int *version);                // zqw_basic.cpp
bool zq255_misc_commit(const std::map<std::string, TapeField> &edits, std::string &why);
bool zq255_strings_record(std::vector<byte> &rec, int *version);
bool zq255_strings_commit(const std::map<std::string, TapeField> &edits, std::string &why);
bool zq255_subs_record(std::vector<byte> &rec, int *version);                // zqw_init.cpp
bool zq255_subs_commit(const std::map<std::string, TapeField> &edits, std::string &why);
void zq255_screen_fields_sync(int index, bool music, bool lens);
extern dmap *DMaps;

namespace
{

enum { KIND_ITEM, KIND_GUY, KIND_DMAP, KIND_SCREEN, KIND_INIT, KIND_MISC, KIND_STRINGS, KIND_SUBS };

// Screens, misc data and strings show only what 2.53's dialogs don't.
bool extras_only(int kind)
{
    return kind == KIND_SCREEN || kind == KIND_MISC || kind == KIND_STRINGS || kind == KIND_SUBS;
}

// Misc data and strings: whole sections whose edits are kept by key.
bool keyed(int kind)
{
    return kind == KIND_MISC || kind == KIND_STRINGS || kind == KIND_SUBS;
}

// The store of a keyed section's edits.
std::map<std::string, TapeField> &keyed_store(int kind)
{
    return kind == KIND_STRINGS ? zq255.string_fields : kind == KIND_SUBS ? zq255.sub_fields : zq255.misc_fields;
}

typedef bool (*Schema)(TapeParser &, int);

Schema schema_of(int kind)
{
    switch(kind)
    {
    case KIND_ITEM: return zq_tape_item;
    case KIND_GUY: return zq_tape_guy;
    case KIND_DMAP: return zq_tape_dmap;
    case KIND_INIT: return zq_tape_init;
    case KIND_MISC: return zq_tape_misc;
    case KIND_STRINGS: return zq_tape_strings;
    case KIND_SUBS: return zq_tape_subscreens;
    }

    return zq_tape_screen;
}

const char *kind_word(int kind)
{
    static const char *w[8] = { "item", "enemy", "DMap", "screen", "init data", "misc data", "strings", "subscreens" };
    return w[kind];
}

// the screen groups that hold 2.55/3.0-only fields: shown even where the screen has none yet
const int64_t SCREEN_EXTRA_GROUPS = 0x0001 | 0x0020 | 0x0040 | 0x0400 | 0x0800 | 0x8000;

// A tape laid out again by its schema (fields a changed flag adds or drops).
Tape rebuilt(const Tape &t, int kind, int v)
{
    Tape out;
    TapeParser p(&t, &out);
    schema_of(kind)(p, v);
    return out;
}

struct RecEditor
{
    bool open, close, want_ok, focus, busy;
    int kind, id, v;
    bool is30;
    Tape tape;
    Tape baseline;          // screens: the fields as the screen has them without this editor's edits
    std::vector<byte> original;
    char name[64];          // the name, where the record doesn't hold it (the names block)
    bool name_in_tape;
    std::string error;
    std::string start_group;    // the tab to show first
    std::map<std::string, int> picked;  // list tabs: the entry shown
    std::map<std::string, size_t> keys; // where each key is in the tape (rebuilt when its size changes)

    RecEditor() : open(false), close(false), want_ok(false), focus(false), busy(false), kind(0), id(0), v(0), is30(false),
                  name_in_tape(false)
    {
        name[0] = 0;
    }
};

RecEditor g_re;
std::string g_start_group;      // run(): the tab to open at

// A field of the editor's tape by key, through an index (the tapes of whole sections are large).
const TapeField *find_field(const std::string &key)
{
    if(g_re.keys.size() != g_re.tape.f.size())
    {
        g_re.keys.clear();

        for(size_t i = 0; i < g_re.tape.f.size(); ++i)
            g_re.keys[g_re.tape.f[i].key] = i;
    }

    std::map<std::string, size_t>::const_iterator it = g_re.keys.find(key);
    return it == g_re.keys.end() || it->second >= g_re.tape.f.size() ? NULL : &g_re.tape.f[it->second];
}
Q30ItemLabels g_labels;
std::string g_gl_attr[32], g_gh_attr[32], g_gl_bflag[16], g_gh_bflag[16];   // the enemy's labels
const Q30NameVal *g_glist[32];      // the enemy's attributes upstream shows as dropdowns

float field_w() { return 120.0f * zcui::scale(); }

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

// the range a field's width and sign allow
void range(const TapeField &f, int64_t &lo, int64_t &hi)
{
    const int bits = f.width * 8;

    if(f.sign)
    {
        lo = -(1LL << (bits - 1));
        hi = (1LL << (bits - 1)) - 1;
    }
    else
    {
        lo = 0;
        hi = (bits >= 64) ? 0x7FFFFFFFFFFFFFFFLL : (int64_t)((1ULL << bits) - 1);
    }
}

bool in_number(const char *label, TapeField &f)
{
    int64_t lo, hi;
    range(f, lo, hi);
    ImGui::SetNextItemWidth(field_w());
    int64_t t = f.value;

    if(!ImGui::InputScalar(label, ImGuiDataType_S64, &t))
        return false;

    if(t < lo) t = lo;
    if(t > hi) t = hi;
    f.value = t;
    return true;
}

// ---- labels -------------------------------------------------------------------------------

// The item editor's label for a field, for the item's class (empty: the field's own).
std::string item_label(const TapeField &f, std::string *h)
{
    if(g_re.kind == KIND_INIT)
    {
        if(f.key.compare(0, 3, "ctr") == 0 || f.key.compare(0, 4, "mctr") == 0)
            return std::string(f.key[0] == 'm' ? "Max " : "Start ") + q30_counter_name(f.index);

        return "";
    }

    if(g_re.kind == KIND_GUY)
    {
        if(f.key.compare(0, 4, "attr") == 0 && f.index >= 0 && f.index < 32)
        {
            *h = g_gh_attr[f.index];
            return g_gl_attr[f.index];
        }

        return "";
    }

    if(g_re.kind != KIND_ITEM)
        return "";

    if(f.list == TL_ITEMMISC && f.index >= 0 && f.index < 10)
    {
        *h = g_labels.h_misc[f.index];
        return g_labels.misc[f.index];
    }

    if(f.list == TL_SPRITE && f.key.compare(0, 3, "wpn") == 0 && f.index >= 0 && f.index < 10)
    {
        *h = g_labels.h_wpn[f.index];
        return g_labels.wpn[f.index];
    }

    if(f.key == "usesound" || f.key == "usesound2")
    {
        const int k = f.key == "usesound" ? 0 : 1;
        *h = g_labels.h_actionsnd[k];
        return g_labels.actionsnd[k];
    }

    if(f.key == "power")
    {
        *h = g_labels.h_power;
        return g_labels.power;
    }

    return "";
}

// The item's flags: the named ones, and the class's labels for the generic ones.
std::string item_flag_label(int bit, std::string *h)
{
    static const char *named[32] =
    {
        "Equipment Item", "Can Be Eaten By Enemies", "Upgrade When Collected Twice", "Remove Item When Used", NULL, NULL,
        "Keep Lower Level Items", "Cost Is Rupees (old)", NULL, "Gain All Lower Level Items", NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "Passive Action Script", "Only Validate Cost",
        "Disabled In Sideview Water", "Usable as a Bunny", "Only Validate Cost 2", "Immune to jinxes", "Uses Other Jinx",
        "Burning Sprites", NULL
    };

    // item_flag1..15: bits 4, 5, then 10-22
    int generic = -1;

    if(bit == 4 || bit == 5)
        generic = bit - 4;
    else if(bit >= 10 && bit <= 22)
        generic = bit - 8;

    if(generic >= 0)
    {
        *h = g_labels.h_flag[generic];

        if(!g_labels.flag[generic].empty())
            return g_labels.flag[generic];

        char b[32];
        snprintf(b, sizeof(b), "Flags[%d]", generic);
        return b;
    }

    if(named[bit])
        return named[bit];

    char b[32];
    snprintf(b, sizeof(b), "Bit %d (unused)", bit);
    return b;
}

// Upstream's names for the bits of the other flag fields (core/flags.h, zdefs.h); NULL: unused.
const char *bit_name(int list, int bit)
{
    static const char *guy[32] =
    {
        "Boomerangs stun", "Invisible", "Never returns after death", "Doesn't count as beatable", "Fade: flicker",
        "Fade: instant", "Weapons use burning sprites", "No contact damage", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "Shield: front", "Shield: left", "Shield: right", "Shield: back",
        "Hammer breaks shield", "Weak to arrows (old)", NULL, "Only visible with the Lens"
    };
    static const char *guy2[32] =
    {
        "Flashing", "Zora (old)", "Rock (old)", "Trap (old)", "Trap: horizontal", "Trap: vertical", "Trap: 4-way",
        "Trap: left-right", "Trap: up-down", "Trap: 2", "Fire (old)", "Armos (old)", "Ghini (old)", "Ganon (old)", "Blinking",
        "Transparent", "Ignores 'temporary no return'", "Ignores 'kill all enemies'", "Kills summoned enemies on death"
    };
    static const char *move[32] =
    {
        "Obeys gravity", "Can fall into pits", "Can walk over pits", "Can drown in water", "Can walk on water",
        "Only walks on water", "Only walks on shallow water", "Only walks over pits", "No fake Z", "No real Z", "Uses fake Z",
        "Ignores solidity", "Ignores block flags", "Ignores the screen edge", "New movement", "Can't be pushed"
    };
    static const char *pickup[32] =
    {
        "Large collision box", "Held up when collected", "Sets the screen's item state", "Dummy (can't be collected)",
        "Checks restrictions (shops)", "Costs money", "Blinks while fading", "Carried by an enemy", "Disappears after a while",
        "Large triforce collision box", "Not drawn", "Sets the screen's special item state", "Triggers secrets",
        "Always grabbable (hookshot, arrows, boomerang)"
    };

    static const char *dmap[32] =
    {
        "Use caves instead of item cellars", "Allow 3-stair warps", "Allow whistle whirlwinds", "Special rooms and guys are in caves only",
        "Don't display compass marker", "Wavy screen", "Whistle whirlwind returns to start", "Always display intro string",
        "View overworld map by pressing Map", "...but only show screens marked in the DMap map", "Use minimap foreground colour 2",
        "Script 1", "Script 2", "Script 3", "Script 4", "Script 5", "Sideview (old)", "Layer 3 is background",
        "Layer 2 is background", "New cellar enemies", "Bunny if no Moon Pearl", "Mirror continues", "Extended viewport",
        "Custom gravity", "Custom viewport follow"
    };
    static const char *layer[32] = { "Layer 0", "Layer 1", "Layer 2", "Layer 3", "Layer 4", "Layer 5", "Layer 6" };
    static const char *ffc[32] =
    {
        "Draw over", "Translucent", "Solid", "Carry over", "Stationary", "Is a changer", "Run script at screen init",
        "Only visible to the Lens", "Script restarts when carried over", "Ethereal", "Updates while the Hero holds up an item",
        "Ignores changers", "Imprecise changer collision", "Invisible to the Lens", "Platform", "Solid if its combo is",
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "Changer: change speed", "Changer: change to this",
        "Changer: previous combo", "Changer: next combo", "Changer: swap speed with previous", "Changer: swap speed with next"
    };
    static const char *wflags[32] =
    {
        "Picks up items", "Breaks when landing", "Breaks on solid", "Burns: any fire", "Burns: strong fire", "Burns: magic fire",
        "Burns: divine fire", "Stops when landing", "Stops when hitting solid", "Updates its ignite sprite",
        "No collision while still", "Ignores solidity for now", "Obeys conveyors", "Carried across screens"
    };
    static const char *sizeflags[32] =
    {
        "Tile width", "Tile height", "Hitbox width", "Hitbox height", "Hitbox Z height", "Hitbox X offset", "Hitbox Y offset",
        "Draw X offset", "Draw Y offset", "Draw Z offset"
    };
    static const char *scrstate[32] =
    {
        "Door up", "Door down", "Door left", "Door right", "Item", "Special item", "Enemies never return",
        "Enemies don't return (temporary)", "Lock block", "Boss lock block", "Chest", "Locked chest", "Boss chest", "Secrets",
        "Visited", "Light beams", "No enemies return"
    };
    static const char *litem[32] = { "McGuffin", "Map", "Compass", "Boss killed", "Boss key" };
    static const char *btns[32] = { "A", "B", "L", "R", "Ex1", "Ex2", "Ex3", "Ex4" };
    static const char *widg[32] =
    {
        "Selectable", "Page change doesn't wrap", "Override the selector", "Counter: needs its maximum",
        "Counter: value is a percent", "Level items: inverted", "Page change swaps to the map subscreen",
        "Screen states: inverted", "Items: any of them", "Level items: any of them", "Screen states: any of them",
        "Level states: inverted", "Level states: any of them"
    };
    static const char *bottle[32] =
    {
        "Amount 1 is a percent", "Amount 2 is a percent", "Amount 3 is a percent", "Cures shield jinx", "Cures sword jinx",
        "Cures item jinx", "Usable when the counters are full", "Used automatically on death"
    };

    if(bit < 0 || bit >= 32)
        return NULL;

    switch(list)
    {
    case TL_DMAPFLAGS: return dmap[bit];
    case TL_INITFLAGS: return bit == 0 ? "Continue HP is a percent" : bit == 1 ? "Can slash" : NULL;
    case TL_INITMISC: return bit == 0 ? "Continue hearts are a percent" : bit == 1 ? "Double magic" : bit == 2 ? "Can slash" : NULL;
    case TL_LAYERBITS: return layer[bit];
    case TL_GUYFLAGS: return guy[bit];
    case TL_GUYFLAGS2: return guy2[bit];
    case TL_MOVEFLAGS: return move[bit];
    case TL_PICKUPFLAGS: return pickup[bit];
    case TL_BOTTLEFLAGS: return bottle[bit];
    case TL_SUBFLAGS: return bit == 0 ? "No page wrap" : bit == 1 ? "Override the selector" : NULL;
    case TL_WIDGFLAGS: return widg[bit];
    case TL_SUBPOSFLAGS: return bit == 0 ? "On the active subscreen when it's up" : bit == 1 ? "When it's down" : bit == 2 ? "While it scrolls" : NULL;
    case TL_SUBTRANSFLAGS: return bit == 0 ? "Don't hide the selector" : NULL;
    case TL_INTBTNS: return btns[bit];
    case TL_SCRSTATES: return scrstate[bit];
    case TL_WEAPFLAGS: return bit == 0 ? "Glow radius" : bit == 1 ? "Set step speed" : NULL;
    case TL_WFLAGS: return wflags[bit];
    case TL_FFCFLAGS: return ffc[bit];
    case TL_ROOMFLAGS: return bit == 0 ? "Guy always shows" : bit == 1 ? "Guy's fires" : NULL;
    case TL_SIZEFLAGS: return sizeflags[bit];
    case TL_PSTRFLAGS: return bit == 0 ? "Always shown" : bit == 1 ? "Only when held up" : bit == 2 ? "Doesn't mark the item seen" : NULL;
    case TL_LITEMBITS: return litem[bit];
    }

    return "";      // no names: every bit shown by number
}

// A bottle type's name (1-64; 0 is none): as this editor has it, or as the quest does.
std::string bottle_type_name(int type)
{
    if(type <= 0)
        return "(None)";

    if(type > 64)
        return "";

    char k[32];
    snprintf(k, sizeof(k), "bt%d.name", type - 1);

    if(const TapeField *f = g_re.tape.find(k))
        return f->bytes.empty() ? std::string() : std::string((const char *)&f->bytes[0], strnlen((const char *)&f->bytes[0], f->bytes.size()));

    std::map<int, Q30BottleType>::const_iterator it = zq255.bottle_types.find(type - 1);
    return it == zq255.bottle_types.end() ? std::string() : std::string(it->second.name);
}

// ---- fields -----------------------------------------------------------------------------

void enum_combo(const char *label, TapeField &f)
{
    char cur[300];
    const int64_t val = f.value;
    std::string name;

    switch(f.list)
    {
    case TL_BOTTLETYPE: name = bottle_type_name((int)val); break;
    case TL_ITEMCLASS: name = q30_itemclass_name((int)val); break;
    case TL_ENEMYCLASS: name = q30_enemyclass_name((int)val); break;
    case TL_COUNTER: name = q30_counter_name((int)val); break;

    case TL_DEFENSE:
    case TL_ANIM:
        for(int k = 0;; ++k)
        {
            int v = 0;
            const char *n = f.list == TL_DEFENSE ? q30_defense_at(k, &v) : q30_anim_at(k, &v);

            if(!n)
                break;

            if(v == val)
                name = n;
        }

        break;
    }

    snprintf(cur, sizeof(cur), "%lld: %s", val, name.c_str());
    ImGui::SetNextItemWidth(field_w() * 2.4f);

    if(!ImGui::BeginCombo(label, cur, ImGuiComboFlags_HeightLarge))
        return;

    int64_t lo, hi;
    range(f, lo, hi);

    if(f.list == TL_BOTTLETYPE)
    {
        for(int k = 0; k <= 64; ++k)
        {
            char item[300];
            snprintf(item, sizeof(item), "%d: %s", k, bottle_type_name(k).c_str());

            if(ImGui::Selectable(item, k == val))
                f.value = k;
        }
    }
    else if(f.list == TL_DEFENSE || f.list == TL_ANIM)
    {
        for(int k = 0;; ++k)
        {
            int v = 0;
            const char *n = f.list == TL_DEFENSE ? q30_defense_at(k, &v) : q30_anim_at(k, &v);

            if(!n)
                break;

            if(v < lo || v > hi)
                continue;

            char item[300];
            snprintf(item, sizeof(item), "%d: %s", v, n);

            if(ImGui::Selectable(item, v == val))
                f.value = v;
        }
    }
    else
    {
        const int first = f.list == TL_COUNTER ? -1 : 0;
        const int n = f.list == TL_ITEMCLASS ? q30_itemclass_count(g_re.is30)
                    : f.list == TL_ENEMYCLASS ? q30_enemyclass_count(g_re.is30) : q30_counter_count();

        for(int k = first; k < n; ++k)
        {
            const char *nm = f.list == TL_ITEMCLASS ? q30_itemclass_name(k)
                           : f.list == TL_ENEMYCLASS ? q30_enemyclass_name(k) : q30_counter_name(k);

            if((!nm[0] || nm[0] == '-') && k != val)
                continue;

            if(k < lo || k > hi)
                continue;

            char item[300];
            snprintf(item, sizeof(item), "%d: %s", k, nm);

            if(ImGui::Selectable(item, k == val))
                f.value = k;
        }
    }

    ImGui::EndCombo();
}

// An enemy attribute upstream's editor shows as a dropdown: its choices (or the quest's
// enemies), and the number itself where it's none of them.
bool attr_dropdown(const char *label, TapeField &f)
{
    if(g_re.kind != KIND_GUY || f.key.compare(0, 4, "attr") != 0 || f.index < 0 || f.index >= 32 || !g_glist[f.index])
        return false;

    const Q30NameVal *list = g_glist[f.index];
    const bool enemies = q30_attr_list_is_enemies(list);
    const int64_t val = f.value;
    std::string cur;

    if(enemies)
        cur = val >= 0 && val < eMAXGUYS && guy_string[val] ? guy_string[val] : "";
    else
        for(const Q30NameVal *c = list; c->name; ++c)
            if(c->value == val)
                cur = c->name;

    char shown[300];
    snprintf(shown, sizeof(shown), "%lld: %s", val, cur.empty() ? "?" : cur.c_str());
    ImGui::SetNextItemWidth(field_w() * 2.4f);

    if(ImGui::BeginCombo(label, shown, ImGuiComboFlags_HeightLarge))
    {
        if(enemies)
        {
            for(int k = 0; k < eMAXGUYS; ++k)
            {
                char item[300];
                snprintf(item, sizeof(item), "%d: %s", k, guy_string[k] ? guy_string[k] : "");

                if(ImGui::Selectable(item, k == val))
                    f.value = k;
            }
        }
        else
        {
            for(const Q30NameVal *c = list; c->name; ++c)
            {
                char item[300];
                snprintf(item, sizeof(item), "%d: %s", c->value, c->name);

                if(ImGui::Selectable(item, c->value == val))
                    f.value = c->value;
            }
        }

        ImGui::EndCombo();
    }

    return true;
}

// A counted list of numbers (TK_NLIST): each number, a way to add one, a way to remove each.
void nlist_field(TapeField &f)
{
    const int w = f.width > 0 ? f.width : 1;
    const size_t count = f.bytes.size() / (size_t)w;
    const uint64_t maxv = w >= 8 ? ~0ULL : (1ULL << (8 * w)) - 1;
    const uint64_t maxn = f.index >= 8 ? ~0ULL : (1ULL << (8 * f.index)) - 1;
    ImGui::Text("%s", f.label.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%d)", (int)count);
    int remove = -1;

    for(size_t i = 0; i < count; ++i)
    {
        uint64_t v = 0;

        for(int k = 0; k < w; ++k)
            v |= (uint64_t)f.bytes[i * w + k] << (8 * k);

        ImGui::PushID((int)i);
        int64_t t = (int64_t)v;
        ImGui::SetNextItemWidth(field_w());

        if(ImGui::InputScalar("##v", ImGuiDataType_S64, &t))
        {
            if(t < 0)
                t = 0;
            if((uint64_t)t > maxv)
                t = (int64_t)maxv;

            for(int k = 0; k < w; ++k)
                f.bytes[i * w + k] = (byte)((uint64_t)t >> (8 * k));
        }

        if(f.list == TL_ITEM && t >= 0 && t < ITEMCNT && item_string[t])
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", item_string[t]);
        }

        ImGui::SameLine();

        if(ImGui::SmallButton("Remove"))
            remove = (int)i;

        ImGui::PopID();
    }

    if(remove >= 0)
        f.bytes.erase(f.bytes.begin() + remove * w, f.bytes.begin() + (remove + 1) * w);

    if(count < maxn && ImGui::SmallButton("Add"))
        f.bytes.insert(f.bytes.end(), (size_t)w, (byte)0);
}

// A number with the name it refers to (a sound, a sprite).
void named_number(const char *label, TapeField &f)
{
    in_number(label, f);
    const char *name = NULL;

    if(f.list == TL_SFX && f.value > 0 && f.value < WAV_COUNT)
        name = sfx_string[f.value];
    else if(f.list == TL_SPRITE && f.value >= 0 && f.value < WPNCNT)
        name = weapon_string[f.value];

    if(name && name[0])
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", name);
    }
}

// upstream's names for a widget type's own flags (new_subscr.h SUBSCR_*_)
const char *widget_flag_name(int ty, int bit)
{
    static const char *const gauge[16] =
    {
        "Modifier 1", "Modifier 2", "Modifier 3", "Modifier 4", "Unique last", "Full tile", "Animate under", "Animate over",
        "Animate by percent", "Skip animation", "Infinite item required", "Infinite item banned", "Grid right to left",
        "Grid top to bottom", "Grid column first", "Grid snake"
    };
    static const char *const curitm[7] =
    {
        "Invisible", "Not equippable", "Ignore special selected text", "Ignore special display", "No interaction without the item",
        "No interaction unless equipped", "Can't unequip"
    };
    static const char *const mmap[6] =
    {
        "Show the map", "Show the player", "Show the compass", "Visited rooms need the map", "Draw over the map tile", "Only rooms"
    };
    static const char *const counter[4] = { "Show 0", "Only when selected", "Show the maximum", "Fraction" };
    static const char *const btncounter[4] = { "Show 0", "Don't collapse", "Show the maximum", "Fraction" };

    if(bit < 0)
        return NULL;

    switch(ty)
    {
    case 1: return bit == 0 ? "Transparent" : bit == 1 ? "Overlay" : NULL;                    // frame
    case 3: return bit == 0 ? "Transparent" : NULL;                                            // line
    case 4: return bit == 0 ? "Transparent" : bit == 1 ? "Filled" : NULL;                     // rectangle
    case 5: return bit == 0 ? "Alternate style" : NULL;                                        // time
    case 7: return bit == 0 ? "Bottom up" : NULL;                                              // life meter
    case 8: return bit == 0 ? "Transparent" : NULL;                                            // button item
    case 9: return bit < 4 ? counter[bit] : NULL;                                              // counter
    case 10: return bit == 0 ? "Show X" : NULL;                                                // counter block
    case 11: return bit == 0 ? "Needs the map" : bit == 1 ? "One line" : bit == 2 ? "Word wrap" : NULL;
    case 12: return bit < 6 ? mmap[bit] : NULL;                                                // minimap
    case 13: return bit == 0 ? "Show the map" : bit == 1 ? "Show rooms" : bit == 2 ? "Show the player" : bit == 3 ? "Large" : NULL;
    case 15: return bit < 7 ? curitm[bit] : NULL;                                              // item slot
    case 16: return bit == 0 ? "Show the frame" : bit == 1 ? "Show the pieces" : bit == 2 ? "Large pieces" : NULL;
    case 17: case 18: case 19: return bit == 0 ? "Overlay" : bit == 1 ? "Transparent" : NULL;   // McGuffin, tile block, mini tile
    case 20: return bit == 0 ? "Transparent" : bit == 1 ? "Large" : bit == 2 ? "Use selector 2" : NULL;
    case 21: case 22: case 25: case 28: return bit < 16 ? gauge[bit] : NULL;                  // gauges
    case 23: case 24: return bit == 0 ? "Word wrap" : NULL;                                    // text box, selected text
    case 26: return bit < 4 ? btncounter[bit] : NULL;                                          // button counter
    case 27: return bit == 0 ? "Transparent" : bit == 1 ? "Vertical" : bit == 2 ? "Inverted" : NULL;
    case 29: return bit == 0 ? "Alternate style" : NULL;                                       // cooldown text
    }

    return NULL;
}

void flags_field(TapeField &f)
{
    const int bits = f.width * 8;
    ImGui::Text("%s", f.label.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(0x%0*llX)", f.width * 2, (uint64_t)f.value & ((bits >= 64) ? ~0ULL : ((1ULL << bits) - 1)));

    // four to a row where there is room for them, fewer in a narrow panel
    const int fit = (int)(ImGui::GetContentRegionAvail().x / (190.0f * zcui::scale()));
    int cols = f.list == TL_ITEMFLAGS ? 2 : 4;
    cols = fit < 1 ? 1 : fit < cols ? fit : cols;

    if(!ImGui::BeginTable(f.key.c_str(), cols, ImGuiTableFlags_SizingStretchSame))
        return;

    for(int b = 0; b < bits; ++b)
    {
        std::string h, label;
        const bool set = ((uint64_t)f.value >> b) & 1;

        if(f.list == TL_GUYBFLAGS)
        {
            label = b < 16 ? g_gl_bflag[b] : "";
            h = b < 16 ? g_gh_bflag[b] : "";

            if(label.empty() || label.compare(0, 7, "BFlags[") == 0)
            {
                if(!set)
                    continue;

                char t[32];
                snprintf(t, sizeof(t), "Bit %d", b);
                label = t;
            }
        }
        else if(f.list == TL_WIDGSPEC)
        {
            const char *n = widget_flag_name(f.index, b);

            if(!n && !set)
                continue;

            char t[48];
            snprintf(t, sizeof(t), "Bit %d", b);
            label = n ? n : t;
        }
        else if(f.list == TL_ITEMFLAGS)
        {
            label = item_flag_label(b, &h);

            // a flag this class doesn't use is shown only while it's set
            if(!set && (label.compare(0, 6, "Flags[") == 0 || label.find("(unused)") != std::string::npos))
                continue;
        }
        else
        {
            const char *n = bit_name(f.list, b);

            if(!n && !set)
                continue;

            char t[48];
            snprintf(t, sizeof(t), "Bit %d", b);
            label = (n && n[0]) ? n : t;
        }

        ImGui::TableNextColumn();
        ImGui::PushID(b);
        bool on = ((uint64_t)f.value >> b) & 1;

        if(ImGui::Checkbox(label.c_str(), &on))
        {
            uint64_t v = (uint64_t)f.value;
            v = on ? (v | (1ULL << b)) : (v & ~(1ULL << b));

            if(f.sign && bits < 64 && ((v >> (bits - 1)) & 1))
                v |= ~0ULL << bits;
            else if(bits < 64)
                v &= (1ULL << bits) - 1;

            f.value = (int64_t)v;
        }

        help(h);
        ImGui::PopID();
    }

    ImGui::EndTable();
}

void text_field(const char *label, TapeField &f, size_t max)
{
    char buf[256];
    size_t n = f.bytes.size() < sizeof(buf) - 1 ? f.bytes.size() : sizeof(buf) - 1;
    memcpy(buf, f.bytes.empty() ? "" : (const char *)&f.bytes[0], n);
    buf[n] = 0;

    if(f.kind == TK_TEXT)
        buf[strnlen(buf, n)] = 0;

    ImGui::SetNextItemWidth(field_w() * 2.6f);

    if(!ImGui::InputText(label, buf, max + 1 < sizeof(buf) ? max + 1 : sizeof(buf)))
        return;

    if(f.kind == TK_TEXT)
    {
        std::vector<byte> b(f.bytes.size(), 0);
        memcpy(b.empty() ? NULL : &b[0], buf, strnlen(buf, b.size()));
        f.bytes.swap(b);
    }
    else
        f.bytes.assign(buf, buf + strlen(buf));
}

// A one-bit-per-item field (a DMap's disabled items): the stored bytes change only when a box does.
void item_bits_field(TapeField &f)
{
    const bool per_byte = f.kind == TK_BYTES;   // one byte (0 or 1) per item
    std::vector<byte> bits = f.kind == TK_BITSTR ? zq_bitstr_bits(f.bytes) : f.bytes;
    ImGui::TextUnformatted(f.label.c_str());

    if(!ImGui::BeginChild(f.key.c_str(), ImVec2(0, 300.0f * zcui::scale()), ImGuiChildFlags_Borders))
    {
        ImGui::EndChild();
        return;
    }

    const int count = per_byte ? (int)f.bytes.size() : f.kind == TK_BITFIELD ? (int)f.bytes.size() * 8 : MAXITEMS;
    bool changed = false;

    if(ImGui::BeginTable("bits", 3, ImGuiTableFlags_SizingStretchSame))
    {
        for(int i = 0; i < count; ++i)
        {
            ImGui::TableNextColumn();
            bool on = per_byte ? bits[i] != 0 : (size_t)(i / 8) < bits.size() && (bits[i / 8] >> (i % 8)) & 1;
            char l[96];
            snprintf(l, sizeof(l), "%d: %s##b%d", i, i < ITEMCNT && item_string[i] ? item_string[i] : "", i);

            if(ImGui::Checkbox(l, &on) && per_byte)
            {
                bits[i] = on ? 1 : 0;
                changed = true;
            }
            else if(ImGui::IsItemEdited())
            {
                if((size_t)(i / 8) >= bits.size())
                    bits.resize(i / 8 + 1, 0);

                bits[i / 8] = (byte)(on ? (bits[i / 8] | (1 << (i % 8))) : (bits[i / 8] & ~(1 << (i % 8))));
                changed = true;
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();

    if(changed)
    {
        if(f.kind == TK_BITSTR)
            f.bytes = zq_bitstr_store(bits, zq_bitstr_size(f.bytes));
        else
            f.bytes = bits;
    }
}

// One number per entry: bottle slots, keys per level (fixed bytes, or a bitstring of values).
void values_field(TapeField &f)
{
    std::vector<byte> vals = f.kind == TK_BITSTR ? zq_bitstr_bits(f.bytes) : f.bytes;
    const int count = f.kind == TK_BITSTR ? MAXLEVELS : (int)vals.size();

    if(f.kind == TK_BITSTR && vals.size() < (size_t)count)
        vals.resize(count, 0);

    ImGui::TextUnformatted(f.label.c_str());
    bool changed = false;

    if(ImGui::BeginChild(f.key.c_str(), ImVec2(0, 300.0f * zcui::scale()), ImGuiChildFlags_Borders) &&
       ImGui::BeginTable("vals", 4, ImGuiTableFlags_SizingStretchSame))
    {
        ImGuiListClipper clip;
        clip.Begin((count + 3) / 4);

        while(clip.Step())
            for(int row = clip.DisplayStart; row < clip.DisplayEnd; ++row)
                for(int c = 0; c < 4; ++c)
                {
                    const int i = row * 4 + c;
                    ImGui::TableNextColumn();

                    if(i >= count)
                        continue;

                    char l[48];
                    snprintf(l, sizeof(l), "%s %d##v%d", f.list == TL_BOTTLES ? "Slot" : "Level", i, i);
                    int t = vals[i];
                    ImGui::SetNextItemWidth(60.0f * zcui::scale());

                    if(ImGui::InputInt(l, &t, 0, 0))
                    {
                        vals[i] = (byte)(t < 0 ? 0 : (t > 255 ? 255 : t));
                        changed = true;
                    }
                }

        ImGui::EndTable();
    }

    ImGui::EndChild();

    if(changed)
    {
        if(f.kind == TK_BITSTR)
            f.bytes = zq_bitstr_store(vals, zq_bitstr_size(f.bytes));
        else
            f.bytes = vals;
    }
}

// The init data's Levels tab: each level's McGuffin, map, compass, boss key, boss killed and keys,
// however the version keeps them.
bool level_bit(Tape &t, int v, int level, int what, bool set, bool on)
{
    // what: 0 McGuffin, 1 map, 2 compass, 3 boss killed, 4 boss key
    static const int litem_bit[5] = { 1, 2, 4, 8, 16 };
    char k[32];

    if(v >= 42)
    {
        snprintf(k, sizeof(k), "litem%d", level);
        TapeField *x = t.find(k);

        if(!x)
            return false;

        if(set)
            x->value = on ? (x->value | litem_bit[what]) : (x->value & ~litem_bit[what]);

        return (x->value & litem_bit[what]) != 0;
    }

    if(v >= 37)
    {
        static const char *keys[5] = { "lmcg", "lmap", "lcompass", NULL, "lboss" };

        if(!keys[what])
            return false;

        snprintf(k, sizeof(k), "%s%d", keys[what], level / 8);
        TapeField *x = t.find(k);

        if(!x)
            return false;

        if(set)
            x->value = on ? (x->value | (1 << (level % 8))) : (x->value & ~(1 << (level % 8)));

        return (x->value >> (level % 8)) & 1;
    }

    if(what == 0)
    {
        TapeField *x = t.find("triforce");

        if(!x || level < 1 || level > 8)
            return false;

        if(set)
            x->value = on ? (x->value | (1 << (level - 1))) : (x->value & ~(1 << (level - 1)));

        return (x->value >> (level - 1)) & 1;
    }

    static const char *fields[5] = { NULL, "map_bits", "compass_bits", NULL, "bosskey_bits" };

    if(!fields[what])
        return false;

    TapeField *x = t.find(fields[what]);

    if(!x || (size_t)(level / 8) >= x->bytes.size())
        return false;

    if(set)
        x->bytes[level / 8] = (byte)(on ? (x->bytes[level / 8] | (1 << (level % 8))) : (x->bytes[level / 8] & ~(1 << (level % 8))));

    return (x->bytes[level / 8] >> (level % 8)) & 1;
}

void levels_tab()
{
    Tape &t = g_re.tape;
    const int v = g_re.v;
    const bool killed = v >= 42;
    static const char *names[5] = { "McGuffin", "Map", "Compass", "Boss killed", "Boss key" };

    if(!ImGui::BeginTable("levels", 6, ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH,
                          ImVec2(0, 340.0f * zcui::scale())))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Level");

    for(int w = 0; w < 5; ++w)
        if(w != 3 || killed)
            ImGui::TableSetupColumn(names[w]);

    if(!killed)
        ImGui::TableSetupColumn("");

    ImGui::TableHeadersRow();
    ImGuiListClipper clip;
    clip.Begin(MAXLEVELS);

    while(clip.Step())
        for(int level = clip.DisplayStart; level < clip.DisplayEnd; ++level)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", level);

            for(int w = 0; w < 5; ++w)
            {
                if(w == 3 && !killed)
                    continue;

                ImGui::TableNextColumn();
                bool on = level_bit(t, v, level, w, false, false);
                char id[24];
                snprintf(id, sizeof(id), "##l%d_%d", level, w);

                if((w != 0 || v >= 37 || (level >= 1 && level <= 8)) && ImGui::Checkbox(id, &on))
                    level_bit(t, v, level, w, true, on);
            }

            if(!killed)
                ImGui::TableNextColumn();
        }

    ImGui::EndTable();

    // the keys and the level switch states that follow
    for(size_t i = 0; i < t.f.size(); ++i)
        if(t.f[i].group == "Levels" && t.f[i].list == TL_LEVELVALUES)
            values_field(t.f[i]);
}

// Bits held as bytes (a bitstring or a fixed field) shown as named checkboxes.
void flags_bytes_field(TapeField &f)
{
    std::vector<byte> bits = f.kind == TK_BITSTR ? zq_bitstr_bits(f.bytes) : f.bytes;
    const int count = f.kind == TK_BITSTR ? 16 : (int)f.bytes.size() * 8;
    ImGui::TextUnformatted(f.label.c_str());
    bool changed = false;

    for(int b = 0; b < count; ++b)
    {
        const char *n = bit_name(f.list, b);
        bool on = (size_t)(b / 8) < bits.size() && (bits[b / 8] >> (b % 8)) & 1;

        if(!n && !on)
            continue;

        char l[64];
        snprintf(l, sizeof(l), "%s##fb%d", n && n[0] ? n : "(unnamed bit)", b);

        if(ImGui::Checkbox(l, &on))
        {
            if((size_t)(b / 8) >= bits.size())
                bits.resize(b / 8 + 1, 0);

            bits[b / 8] = (byte)(on ? (bits[b / 8] | (1 << (b % 8))) : (bits[b / 8] & ~(1 << (b % 8))));
            changed = true;
        }
    }

    if(changed)
    {
        if(f.kind == TK_BITSTR)
            f.bytes = zq_bitstr_store(bits, zq_bitstr_size(f.bytes));
        else
            f.bytes = bits;
    }
}

void notes_field(const char *label, TapeField &f)
{
    static char buf[16384];

    if(f.bytes.size() >= sizeof(buf))
    {
        ImGui::TextDisabled("%s: %d characters (too long to edit here)", f.label.c_str(), (int)f.bytes.size());
        return;
    }

    memcpy(buf, f.bytes.empty() ? "" : (const char *)&f.bytes[0], f.bytes.size());
    buf[f.bytes.size()] = 0;
    ImGui::TextUnformatted(f.label.c_str());

    if(ImGui::InputTextMultiline(label, buf, sizeof(buf), ImVec2(-FLT_MIN, 200.0f * zcui::scale())))
        f.bytes.assign(buf, buf + strlen(buf));
}

void field(TapeField &f)
{
    if(f.kind == TK_HIDDEN || f.kind == TK_BLOB)
        return;

    std::string h = f.help;
    std::string special = item_label(f, &h);

    if(f.list == TL_ITEMCLASS)
        h = q30_itemclass_help((int)f.value);
    const std::string label = (special.empty() ? f.label : special) + "##" + f.key;

    switch(f.kind)
    {
    case TK_FLAGS:
        ImGui::PushID(f.key.c_str());
        flags_field(f);
        ImGui::PopID();
        return;

    case TK_TEXT:
        text_field(label.c_str(), f, f.bytes.empty() ? 0 : f.bytes.size() - 1);
        break;

    case TK_CSTR:
        text_field(label.c_str(), f, 255);
        break;

    case TK_WSTR:
        text_field(label.c_str(), f, 254);
        break;

    case TK_LSTR:
        notes_field(("##" + f.key).c_str(), f);
        return;

    case TK_BITSTR:
    case TK_BITFIELD:
    case TK_BYTES:
        ImGui::PushID(f.key.c_str());

        if(f.list == TL_ITEMBITS)
            item_bits_field(f);
        else if(f.list == TL_LEVELVALUES || f.list == TL_BOTTLES || f.kind == TK_BYTES)
            values_field(f);
        else
            flags_bytes_field(f);

        ImGui::PopID();
        return;

    case TK_NLIST:
        ImGui::PushID(f.key.c_str());
        nlist_field(f);
        ImGui::PopID();
        return;

    case TK_ENUM:
        if(f.list == TL_SFX || f.list == TL_SPRITE)
            named_number(label.c_str(), f);
        else
            enum_combo(label.c_str(), f);
        break;

    default:
        if(!attr_dropdown(label.c_str(), f))
            in_number(label.c_str(), f);
    }

    help(h);
}

// "bt12.name" -> "bt", 12; false for a key that isn't one of a numbered set.
bool indexed_key(const std::string &key, std::string &prefix, int &index)
{
    size_t a = 0;

    while(a < key.size() && isalpha((unsigned char)key[a]))
        ++a;

    size_t b = a;

    while(b < key.size() && isdigit((unsigned char)key[b]))
        ++b;

    if(a == 0 || b == a || b >= key.size() || key[b] != '.')
        return false;

    prefix = key.substr(0, a);
    index = atoi(key.c_str() + a);
    return true;
}

// The name shown for an entry of a numbered set: its own name field, or a shop's.
std::string entry_name(const std::string &prefix, int index)
{
    char k[48];
    snprintf(k, sizeof(k), "%s%d.name", prefix.c_str(), index);
    const TapeField *f = find_field(k);

    if(!f || f->bytes.empty())
        return std::string();

    return std::string((const char *)&f->bytes[0], strnlen((const char *)&f->bytes[0], f->bytes.size()));
}

// A string's text as a list shows it: printable characters only, cut short.
std::string text_preview(const TapeField &f)
{
    std::string out;

    for(size_t i = 0; i < f.bytes.size() && out.size() < 48; ++i)
    {
        const byte c = f.bytes[i];

        if(!c && f.kind == TK_TEXT)
            break;

        if(c >= 32 && c < 127)
            out += (char)c;
        else if(!out.empty() && out[out.size() - 1] != ' ')
            out += ' ';
    }

    return out;
}

void tab(const std::string &group);

bool shown_in_tab(const TapeField &f, const std::string &group)
{
    return f.group == group && f.kind != TK_HIDDEN && f.kind != TK_BLOB && !(extras_only(g_re.kind) && !f.extra);
}

// upstream's widget types (q255_init.cpp's WIDG_*), for the subscreen editor's lists
const char *widget_type_name(int ty)
{
    static const char *const names[] =
    {
        "(none)", "Frame", "Text", "Line", "Rectangle", "Time", "Magic meter", "Life meter", "Button item", "Counter",
        "Counter block", "Minimap title", "Minimap", "Large map", "Background color", "Item slot", "McGuffin frame",
        "McGuffin piece", "Tile block", "Mini tile", "Selector", "Life gauge", "Magic gauge", "Text box", "Selected text",
        "Misc gauge", "Button counter", "Counter bar", "Item cooldown gauge", "Item cooldown text"
    };

    return ty >= 0 && ty < (int)(sizeof(names) / sizeof(names[0])) ? names[ty] : "?";
}

// What a list shows for an entry of a numbered set under base: its name, a string's text, a
// widget's label or type, a page's number.
std::string entry_label(const std::string &base, const std::string &prefix, int n, const std::vector<size_t> &texts)
{
    if(g_re.kind == KIND_STRINGS && base.empty() && n < (int)texts.size())
        return text_preview(g_re.tape.f[texts[n]]);

    if(base.empty())
        return entry_name(prefix, n);

    char k[96];
    snprintf(k, sizeof(k), "%s%s%d.", base.c_str(), prefix.c_str(), n);
    const std::string pre = k;

    if(prefix == "w")
    {
        std::string label;
        const TapeField *lf = find_field(pre + "label");

        if(lf && !lf->bytes.empty())
            label = std::string((const char *)&lf->bytes[0], lf->bytes.size());

        char out[160];
        const TapeField *ty = find_field(pre + "type"), *x = find_field(pre + "x"), *y = find_field(pre + "y");
        snprintf(out, sizeof(out), "%s%s%s (%lld, %lld)", widget_type_name(ty ? (int)ty->value : -1), label.empty() ? "" : ": ",
                 label.c_str(), x ? x->value : 0, y ? y->value : 0);
        return out;
    }

    if(prefix == "p")
        return "Page " + std::to_string(n + 1);

    return entry_name(prefix, n);
}

// A tab of numbered entries (bottle types, strings, subscreens...): the fields of the group under
// base. Where each entry is itself a numbered set (a subscreen's pages, a page's widgets) a
// combo picks the entry and the next level shows below it; at the last level a list on the
// left picks the entry whose fields show on the right. False when the fields aren't numbered.
bool list_level(const std::string &group, const std::string &base, int depth)
{
    std::string prefix;
    std::vector<int> entries;
    std::vector<size_t> texts;      // strings: each entry's text field

    for(size_t i = 0; i < g_re.tape.f.size(); ++i)
    {
        const TapeField &f = g_re.tape.f[i];

        if(depth == 0 && g_re.kind == KIND_STRINGS && f.group == "String" && f.key.size() > 5 &&
           f.key.compare(f.key.size() - 5, 5, ".text") == 0)
            texts.push_back(i);

        if(!shown_in_tab(f, group) || f.key.compare(0, base.size(), base) != 0)
            continue;

        std::string p;
        int n = 0;

        if(!indexed_key(f.key.substr(base.size()), p, n) || (!prefix.empty() && p != prefix))
            return false;

        prefix = p;

        if(entries.empty() || entries.back() != n)
            entries.push_back(n);
    }

    if(entries.size() < (depth == 0 ? 2u : 1u))
        return false;

    // one pick for the whole set, whichever tab shows it
    int &picked = g_re.picked[base + prefix];

    if(std::find(entries.begin(), entries.end(), picked) == entries.end())
        picked = entries[0];

    char nb[96];
    snprintf(nb, sizeof(nb), "%s%s%d.", base.c_str(), prefix.c_str(), picked);
    const std::string next = nb;

    // is the picked entry a numbered set itself?
    bool nested = false, flat = false;

    for(size_t i = 0; i < g_re.tape.f.size() && !flat; ++i)
    {
        const TapeField &f = g_re.tape.f[i];

        if(!shown_in_tab(f, group) || f.key.compare(0, next.size(), next) != 0)
            continue;

        std::string p;
        int n = 0;

        if(indexed_key(f.key.substr(next.size()), p, n))
            nested = true;
        else
            flat = true;
    }

    const float sc = zcui::scale();
    ImGui::PushID(depth);

    if(nested && !flat)
    {
        const std::string cur = entry_label(base, prefix, picked, texts);
        ImGui::SetNextItemWidth(320 * sc);

        if(ImGui::BeginCombo(prefix == "s" ? "Subscreen" : prefix == "p" ? "Page" : "##level", cur.c_str(), ImGuiComboFlags_HeightLarge))
        {
            for(size_t e = 0; e < entries.size(); ++e)
            {
                char item[200];
                snprintf(item, sizeof(item), "%d: %s##e%d", entries[e], entry_label(base, prefix, entries[e], texts).c_str(), entries[e]);

                if(ImGui::Selectable(item, entries[e] == picked))
                    picked = entries[e];
            }

            ImGui::EndCombo();
        }

        list_level(group, next, depth + 1);
        ImGui::PopID();
        return true;
    }

    const float list_w = (g_re.kind == KIND_STRINGS ? 300 : prefix == "w" ? 260 : 220) * sc;

    if(ImGui::BeginChild("list", ImVec2(list_w, 0), ImGuiChildFlags_Borders))
    {
        ImGuiListClipper clip;
        clip.Begin((int)entries.size());

        while(clip.Step())
        for(int e = clip.DisplayStart; e < clip.DisplayEnd; ++e)
        {
            const int n = entries[e];
            const std::string nm = entry_label(base, prefix, n, texts);
            char item[200];
            // bottle types are numbered from 1 where they are used
            snprintf(item, sizeof(item), "%d: %s##e%d", prefix == "bt" ? n + 1 : n, nm.c_str(), n);

            if(ImGui::Selectable(item, n == picked))
                picked = n;
        }
    }

    ImGui::EndChild();
    ImGui::SameLine();

    if(ImGui::BeginChild("entry"))
    {
        bool table = false;

        for(size_t i = 0; i < g_re.tape.f.size(); ++i)
        {
            TapeField &f = g_re.tape.f[i];

            if(!shown_in_tab(f, group) || f.key.compare(0, next.size(), next) != 0)
                continue;

            const bool wide = f.kind != TK_INT;

            if(wide && table)
            {
                ImGui::EndTable();
                table = false;
            }

            if(!wide && !table)
            {
                // two to a row where the labels fit, one in a narrow panel
                char id[64];
                snprintf(id, sizeof(id), "t%d", (int)i);
                const int cols = ImGui::GetContentRegionAvail().x >= 620.0f * sc ? 2 : 1;
                table = ImGui::BeginTable(id, cols, ImGuiTableFlags_SizingStretchSame);
            }

            if(table)
                ImGui::TableNextColumn();

            field(f);
        }

        if(table)
            ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::PopID();
    return true;
}

bool list_tab(const std::string &group)
{
    return list_level(group, std::string(), 0);
}

void tab(const std::string &group)
{
    if(g_re.kind == KIND_INIT && group == "Levels")
    {
        levels_tab();
        return;
    }

    if(keyed(g_re.kind) && list_tab(group))
        return;

    // flags and long texts on their own; numbers two to a row. An enemy's attributes come in
    // their own order (the file adds the later ones in several places).
    std::vector<size_t> order;

    for(size_t i = 0; i < g_re.tape.f.size(); ++i)
        if(g_re.tape.f[i].group == group)
            order.push_back(i);

    if(g_re.kind == KIND_GUY && group == "Attributes")
    {
        for(size_t a = 1; a < order.size(); ++a)
            for(size_t b = a; b > 0 && g_re.tape.f[order[b - 1]].index > g_re.tape.f[order[b]].index; --b)
                std::swap(order[b - 1], order[b]);
    }

    bool table = false;

    for(size_t oi = 0; oi < order.size(); ++oi)
    {
        const size_t i = order[oi];
        TapeField &f = g_re.tape.f[i];

        if(f.group != group || f.kind == TK_HIDDEN || f.kind == TK_BLOB || (extras_only(g_re.kind) && !f.extra))
            continue;

        const bool wide = f.kind != TK_INT;

        if(wide && table)
        {
            ImGui::EndTable();
            table = false;
        }

        if(!wide && !table)
        {
            char id[64];
            snprintf(id, sizeof(id), "t%d", (int)i);
            table = ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame);
        }

        if(table)
            ImGui::TableNextColumn();

        field(f);
    }

    if(table)
        ImGui::EndTable();
}

void editor_layer(void *)
{
    RecEditor &e = g_re;

    if(!e.open || e.busy)
        return;

    if(e.kind == KIND_DMAP)
        e.tape = rebuilt(e.tape, KIND_DMAP, e.v);   // a flag that adds or drops fields

    if(e.kind == KIND_ITEM)
        q30_item_labels((int)e.tape.get("family"), (unsigned)e.tape.get("flags"), g_labels);
    else if(e.kind == KIND_GUY)
    {
        long attributes[32];

        for(int q = 0; q < 32; ++q)
        {
            char k[16];
            snprintf(k, sizeof(k), "attr%d", q);
            attributes[q] = (long)e.tape.get(k);
        }

        q30_enemy_labels((int)e.tape.get("family"), attributes, g_gl_attr, g_gh_attr, g_gl_bflag, g_gh_bflag);
        q30_enemy_attr_lists((int)e.tape.get("family"), attributes, g_glist);
    }

    const float sc = zcui::scale();
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(780 * sc, 620 * sc), ImGuiCond_Appearing);

    if(e.focus)
    {
        ImGui::SetNextWindowFocus();
        e.focus = false;
    }

    char title[200];
    const char *gen = e.is30 ? "3.0 Nightly" : "2.55";

    if(e.kind == KIND_SCREEN)
        snprintf(title, sizeof(title), "Screen %02X of map %d: what 2.53 doesn't have (%s, screen format %d)###zqrecord",
                 e.id % MAPSCRS, e.id / MAPSCRS + 1, gen, e.v);
    else if(e.kind == KIND_DMAP)
        snprintf(title, sizeof(title), "DMap %d: %s (%s, DMap format %d)###zqrecord", e.id, e.name, gen, e.v);
    else if(e.kind == KIND_INIT)
        snprintf(title, sizeof(title), "Init Data (%s, init data format %d)###zqrecord", gen, e.v);
    else if(e.kind == KIND_MISC)
        snprintf(title, sizeof(title), "Misc Data: what 2.53 doesn't have (%s, misc data format %d)###zqrecord", gen, e.v);
    else if(e.kind == KIND_STRINGS)
        snprintf(title, sizeof(title), "Strings: what 2.53 doesn't have (%s, string format %d)###zqrecord", gen, e.v);
    else if(e.kind == KIND_SUBS)
        snprintf(title, sizeof(title), "Subscreens: what 2.53 doesn't have (%s, subscreen format %d)###zqrecord", gen, e.v);
    else
        snprintf(title, sizeof(title), "%s %d: %s (%s, %s format %d)###zqrecord", e.kind == KIND_ITEM ? "Item" : "Enemy",
                 e.id, e.name, gen, kind_word(e.kind), e.v);
    bool open = true;

    if(ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        if(!e.name_in_tape)
        {
            ImGui::SetNextItemWidth(field_w() * 2.6f);
            ImGui::InputText("Name", e.name, 64);
        }

        // the tabs, in the order their first field comes
        std::vector<std::string> groups;

        for(size_t i = 0; i < e.tape.f.size(); ++i)
        {
            const TapeField &f = e.tape.f[i];

            if(f.kind == TK_HIDDEN || f.kind == TK_BLOB || (extras_only(e.kind) && !f.extra))
                continue;

            bool have = false;

            for(size_t k = 0; k < groups.size() && !have; ++k)
                have = groups[k] == f.group;

            if(!have)
                groups.push_back(f.group);
        }

        // the main tab first, then the others as upstream's editors have them
        static const char *order[] = { "Data", "Script", "Room", "Graphics", "Attributes", "Flags", "Action", "Pickup", "Sprites",
                                       "Weapon", "Defenses", "Sounds", "Size", "Movement", "Scripts", "Layers", "Music and lens",
                                       "Carry-over", "Maze", "Gravity", "Notes", "Life and magic", "Counters", "Items",
                                       "Levels", "Bottles", "Hero", "Physics", "Messages", "Lighting", "Viewport",
                                       "Misc sounds", "Misc sprites", "Bottle types", "Bottle shops", "Shop strings",
                                       "Layout", "Portrait", "Icons", "Scrolling and sounds",
                                       "Subscreens", "Pages", "Widgets", "Conditions" };
        std::vector<std::string> sorted;

        for(size_t o = 0; o < sizeof(order) / sizeof(order[0]); ++o)
            for(size_t k = 0; k < groups.size(); ++k)
                if(groups[k] == order[o])
                    sorted.push_back(groups[k]);

        for(size_t k = 0; k < groups.size(); ++k)
        {
            bool have = false;

            for(size_t o = 0; o < sorted.size() && !have; ++o)
                have = sorted[o] == groups[k];

            if(!have)
                sorted.push_back(groups[k]);
        }

        groups.swap(sorted);

        const float bottom = ImGui::GetFrameHeightWithSpacing() * (e.error.empty() ? 1 : 2) + ImGui::GetStyle().ItemSpacing.y;

        if(ImGui::BeginChild("body", ImVec2(0, -bottom)))
        {
            if(ImGui::BeginTabBar("tabs", ImGuiTabBarFlags_FittingPolicyScroll))
            {
                const std::string start = e.start_group;
                e.start_group.clear();

                for(size_t k = 0; k < groups.size(); ++k)
                    if(ImGui::BeginTabItem(groups[k].c_str(), NULL, groups[k] == start ? ImGuiTabItemFlags_SetSelected : 0))
                    {
                        if(ImGui::BeginChild("tab"))
                            tab(groups[k]);

                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                ImGui::EndTabBar();
            }
        }

        ImGui::EndChild();

        if(!e.error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", e.error.c_str());

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

// A screen's edits: the 2.55/3.0-only fields that differ from the screen as it is.
bool commit_screen(RecEditor &e)
{
    std::map<std::string, TapeField> edits;
    {
        zcui::Lock lock;

        for(size_t i = 0; i < e.tape.f.size(); ++i)
        {
            const TapeField &f = e.tape.f[i];

            if(!f.extra)
                continue;

            const TapeField *b = e.baseline.find(f.key.c_str());

            if(!b || b->value != f.value || b->bytes != f.bytes)
                edits[f.key] = f;
        }
    }

    std::map<int, std::map<std::string, TapeField> >::iterator it = zq255.screen_fields.find(e.id);
    const bool had = it != zq255.screen_fields.end() && !it->second.empty();

    // the keys this edit puts in or takes out: the music and lens ones also set fields 2.53 holds
    bool touch_music = false, touch_lens = false;
    {
        std::map<std::string, TapeField> both = edits;

        if(had)
            both.insert(it->second.begin(), it->second.end());

        for(std::map<std::string, TapeField>::const_iterator f = both.begin(); f != both.end(); ++f)
        {
            touch_music = touch_music || f->first == "music";
            touch_lens = touch_lens || f->first == "lens_show" || f->first == "lens_hide";
        }
    }

    if(edits.empty())
    {
        if(had)
        {
            zq255.screen_fields.erase(it);
            zq255_screen_fields_sync(e.id, touch_music, touch_lens);
            saved = false;
        }

        return true;
    }

    bool same = had && it->second.size() == edits.size();

    for(std::map<std::string, TapeField>::const_iterator f = edits.begin(); same && f != edits.end(); ++f)
    {
        std::map<std::string, TapeField>::const_iterator o = it->second.find(f->first);
        same = o != it->second.end() && o->second.value == f->second.value && o->second.bytes == f->second.bytes;
    }

    if(!same)
    {
        zq255.screen_fields[e.id] = edits;
        zq255_screen_fields_sync(e.id, touch_music, touch_lens);
        saved = false;
    }

    return true;
}

// Misc data's and strings' edits: the 2.55/3.0-only fields that differ from the section as
// loaded.
bool commit_misc(RecEditor &e)
{
    std::map<std::string, TapeField> &now = keyed_store(e.kind);

    std::map<std::string, TapeField> edits;
    {
        zcui::Lock lock;

        for(size_t i = 0; i < e.tape.f.size(); ++i)
        {
            const TapeField &f = e.tape.f[i];

            if(!f.extra || f.kind == TK_BLOB)
                continue;

            const TapeField *b = e.baseline.find(f.key.c_str());

            if(!b || b->value != f.value || b->bytes != f.bytes)
                edits[f.key] = f;
        }
    }

    if(edits.size() == now.size())
    {
        bool same = true;

        for(std::map<std::string, TapeField>::const_iterator it = edits.begin(); same && it != edits.end(); ++it)
        {
            std::map<std::string, TapeField>::const_iterator o = now.find(it->first);
            same = o != now.end() && o->second.value == it->second.value && o->second.bytes == it->second.bytes;
        }

        if(same)
            return true;
    }

    std::string why;

    if(!(e.kind == KIND_STRINGS ? zq255_strings_commit(edits, why) : e.kind == KIND_SUBS ? zq255_subs_commit(edits, why)
         : zq255_misc_commit(edits, why)))
    {
        zcui::Lock lock;
        e.error = "Can't keep this: " + why + ".";
        return false;
    }

    saved = false;
    return true;
}

// The game thread: the record in; true when it took.
bool commit(RecEditor &e)
{
    if(e.kind == KIND_SCREEN)
        return commit_screen(e);

    if(keyed(e.kind))
        return commit_misc(e);

    std::vector<byte> rec;
    std::string name_now;
    {
        zcui::Lock lock;
        rebuilt(e.tape, e.kind, e.v).encode(rec);
        name_now = e.name;
    }

    if(e.kind == KIND_DMAP || e.kind == KIND_INIT)
    {
        if(rec == e.original)
            return true;

        std::string why;

        if(!(e.kind == KIND_DMAP ? zq255_dmap_commit(e.id, rec, why) : zq255_init_commit(rec, why)))
        {
            zcui::Lock lock;
            e.error = "Can't keep this: " + why + ".";
            return false;
        }

        saved = false;
        return true;
    }

    char *const old_name = e.kind == KIND_ITEM ? item_string[e.id] : guy_string[e.id];
    const bool name_changed = !e.name_in_tape && strcmp(old_name, name_now.c_str()) != 0;

    if(rec == e.original && !name_changed)
        return true;     // nothing changed

    std::string why;
    bool ok = rec == e.original ? true
            : (e.kind == KIND_ITEM ? zq255_item_commit(e.id, rec, why) : zq255_guy_commit(e.id, rec, why));

    if(!ok)
    {
        zcui::Lock lock;
        e.error = "Can't keep this: " + why + ".";
        return false;
    }

    if(!e.name_in_tape)
    {
        strncpy(old_name, name_now.c_str(), 63);
        old_name[63] = 0;
    }

    saved = false;
    return true;
}

bool record_of(int kind, int id, std::vector<byte> &rec, int *v)
{
    switch(kind)
    {
    case KIND_ITEM: return zq255_item_record(id, rec, v);
    case KIND_GUY: return zq255_guy_record(id, rec, v);
    case KIND_DMAP: return zq255_dmap_record(id, rec, v);
    case KIND_INIT: return zq255_init_record(rec, v);
    case KIND_MISC: return zq255_misc_record(rec, v);
    case KIND_STRINGS: return zq255_strings_record(rec, v);
    case KIND_SUBS: return zq255_subs_record(rec, v);
    }

    return zq255_screen_record(id, rec, v);
}

bool run(int kind, int id)
{
    std::vector<byte> rec;
    int v = 0;

    if(!record_of(kind, id, rec, &v))
        return false;

    Tape t;
    TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);
    const bool parsed = schema_of(kind)(p, v);

    if(!parsed || p.pos != rec.size())
    {
        al_trace("Record editor: %s %d (format %d) doesn't read as its schema says\n", kind_word(kind), id, v);
        return false;
    }

    Tape baseline;

    if(kind == KIND_SCREEN)
    {
        if(TapeField *has = t.find("has"))
            has->value |= SCREEN_EXTRA_GROUPS;     // the groups with 2.55/3.0-only fields, even where empty
        else if(v >= 23)
            return false;   // an unused screen

        baseline = rebuilt(t, kind, v);
        t = baseline;
        std::map<int, std::map<std::string, TapeField> >::const_iterator it = zq255.screen_fields.find(id);

        if(it != zq255.screen_fields.end())
            for(std::map<std::string, TapeField>::const_iterator f = it->second.begin(); f != it->second.end(); ++f)
                if(TapeField *x = t.find(f->first.c_str()))
                {
                    x->value = f->second.value;
                    x->bytes = f->second.bytes;
                }
    }

    if(keyed(kind))
    {
        // the edits so far go on the section as loaded
        baseline = t;
        const std::map<std::string, TapeField> &edits = keyed_store(kind);
        std::map<std::string, size_t> at;

        for(size_t i = 0; i < t.f.size(); ++i)
            if(t.f[i].extra)
                at[t.f[i].key] = i;

        for(std::map<std::string, TapeField>::const_iterator f = edits.begin(); f != edits.end(); ++f)
        {
            std::map<std::string, size_t>::const_iterator x = at.find(f->first);

            if(x != at.end())
            {
                t.f[x->second].value = f->second.value;
                t.f[x->second].bytes = f->second.bytes;
            }
        }
    }

    if(!zcui::available())
        return false;

    {
        zcui::Lock lock;

        if(g_re.open)
            return false;

        g_re.open = true;
        g_re.close = g_re.want_ok = g_re.busy = false;
        g_re.focus = true;
        g_re.kind = kind;
        g_re.id = id;
        g_re.v = v;
        g_re.is30 = q255_header_version[0] >= 3;
        g_re.tape = t;
        g_re.baseline = baseline;
        g_re.original = rec;
        g_re.error.clear();
        g_re.start_group = g_start_group;
        g_re.picked.clear();
        g_re.keys.clear();

        if(kind == KIND_STRINGS)
            g_re.picked["msg"] = id;
        g_re.name_in_tape = kind >= KIND_DMAP || t.find("name") != NULL;
        const char *nm = kind == KIND_ITEM ? item_string[id] : kind == KIND_GUY ? guy_string[id]
                       : kind == KIND_DMAP ? DMaps[id].name : "";
        strncpy(g_re.name, nm, 63);
        g_re.name[63] = 0;
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
            done = g_re.close;
            ok = g_re.want_ok;
            g_re.want_ok = false;
        }

        if(ok && commit(g_re))
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
    g_re.open = false;
    return accepted;
}

} // namespace

// The editors for an item / enemy of the open 2.55 or 3.0 quest. False when it can't be edited
// this way (the caller then uses 2.53's dialog) or was cancelled; *shown says which.
bool zq_edit_item_newer(int index, bool *shown)
{
    std::vector<byte> rec;
    *shown = zq255_item_record(index, rec, NULL);
    return *shown && run(KIND_ITEM, index);
}

// The init data editor (Quest > Init Data) for newer quests.
bool zq_edit_init_newer(bool *shown)
{
    std::vector<byte> rec;
    *shown = zq255_init_record(rec, NULL);
    return *shown && run(KIND_INIT, 0);
}

// The misc data editor (Quest > Misc Data > 2.55/3.0 Misc Data) for newer quests, at a tab
// ("Bottle types"...; NULL: the first).
bool zq_edit_misc_newer(const char *tab, bool *shown)
{
    std::vector<byte> rec;
    *shown = zq255_misc_record(rec, NULL);
    g_start_group = tab ? tab : "";
    const bool ok = *shown && run(KIND_MISC, 0);
    g_start_group.clear();
    return ok;
}

// The string data editor (Quest > 2.55/3.0 String Data) for newer quests, at a string.
bool zq_edit_strings_newer(int string, bool *shown)
{
    std::vector<byte> rec;
    *shown = zq255_strings_record(rec, NULL);

    if(!*shown)
        return false;

    {
        zcui::Lock lock;
        g_re.picked.clear();
    }

    g_start_group.clear();
    return run(KIND_STRINGS, string);
}

// The subscreen data editor (Quest > Misc Data > 2.55/3.0 Subscreen Data) for newer quests in
// upstream's own subscreen format.
bool zq_edit_subs_newer(bool *shown)
{
    std::vector<byte> rec;
    *shown = zq255_subs_record(rec, NULL);
    g_start_group.clear();
    return *shown && run(KIND_SUBS, 0);
}

bool zq_edit_dmap_newer(int index, bool *shown)
{
    std::vector<byte> rec;
    *shown = zq255_dmap_record(index, rec, NULL);
    return *shown && run(KIND_DMAP, index);
}

// The screen editor (Screen > 2.55/3.0 Data): false if the screen has no record of its own in
// the quest's file (a map added in ZQuest) or isn't in use.
bool zq_edit_screen_newer(int index, bool *shown)
{
    std::vector<byte> rec;
    *shown = zq255_screen_record(index, rec, NULL);
    return *shown && run(KIND_SCREEN, index);
}

// DMap copy and paste in a newer quest: the destination takes the source's whole record.
bool zq255_copy_dmap(int dest, int src)
{
    std::vector<byte> rec, dummy;
    std::string why;

    if(!zq255_dmap_record(src, rec, NULL) || !zq255_dmap_record(dest, dummy, NULL))
        return false;

    return zq255_dmap_commit(dest, rec, why);
}

bool zq_edit_enemy_newer(int index, bool *shown)
{
    std::vector<byte> rec;
    *shown = zq255_guy_record(index, rec, NULL);
    return *shown && run(KIND_GUY, index);
}

// Item / enemy copy and paste in a newer quest: the destination takes the source's whole record
// (its name stays its own). False if the records can't be had (the caller copies 2.53's data).
bool zq255_copy_record(bool item, int dest, int src)
{
    std::vector<byte> rec, dummy;
    std::string why;

    if(item ? !(zq255_item_record(src, rec, NULL) && zq255_item_record(dest, dummy, NULL))
            : !(zq255_guy_record(src, rec, NULL) && zq255_guy_record(dest, dummy, NULL)))
        return false;

    if(item)
    {
        // from item format 67 the name is the record's first field: keep the destination's
        int v = 0;
        zq255_item_record(dest, dummy, &v);

        if(v >= 67 && !rec.empty() && !dummy.empty())
        {
            const size_t src_name = 1 + rec[0], dest_name = 1 + dummy[0];

            if(src_name <= rec.size() && dest_name <= dummy.size())
            {
                std::vector<byte> r(dummy.begin(), dummy.begin() + dest_name);
                r.insert(r.end(), rec.begin() + src_name, rec.end());
                rec.swap(r);
            }
        }

        return zq255_item_commit(dest, rec, why);
    }

    return zq255_guy_commit(dest, rec, why);
}

// -qst-dump-record (zquest.cpp): an item's, enemy's, DMap's or screen's record of the open
// quest, field by field. kind: 0 item, 1 enemy, 2 DMap, 3 screen. 0 when written.
void zq_dump_rules(FILE *f);     // zq_rules_editor.cpp

int zq_dump_record(int kind, int id, const char *report)
{
    FILE *f = fopen(report, "w");

    if(!f)
        return 1;

    if(kind == 9)
    {
        zq_dump_rules(f);
        fclose(f);
        return 0;
    }

    std::vector<byte> rec;
    int v = 0;

    if(kind < 0 || kind > KIND_SUBS || !record_of(kind, id, rec, &v))
    {
        fprintf(f, "no %s %d record in this quest\n", kind >= 0 && kind <= KIND_SUBS ? kind_word(kind) : "?", id);
        fclose(f);
        return 2;
    }

    Tape t;
    TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);
    const bool ok = schema_of(kind)(p, v);
    fprintf(f, "%s %d: format %d, %d bytes, %s\n", kind_word(kind), id, v, (int)rec.size(),
            ok && p.pos == rec.size() ? "reads whole" : "DOESN'T READ");

    for(size_t i = 0; i < t.f.size(); ++i)
    {
        const TapeField &x = t.f[i];

        if(x.kind == TK_BLOB || x.kind == TK_BITSTR || x.kind == TK_BITFIELD || x.kind == TK_BYTES)
        {
            unsigned h = 2166136261u;

            for(size_t k = 0; k < x.bytes.size(); ++k)
                h = (h ^ x.bytes[k]) * 16777619u;

            fprintf(f, "%s: %d bytes, hash %08x\n", x.key.c_str(), (int)x.bytes.size(), h);
        }
        else if(x.kind == TK_NLIST)
        {
            fprintf(f, "%s: [", x.key.c_str());

            for(size_t k = 0; x.width > 0 && k + x.width <= x.bytes.size(); k += x.width)
            {
                uint64_t v = 0;

                for(int b = 0; b < x.width; ++b)
                    v |= (uint64_t)x.bytes[k + b] << (8 * b);

                fprintf(f, "%s%llu", k ? " " : "", v);
            }

            fprintf(f, "]\n");
        }
        else if(x.kind == TK_CSTR || x.kind == TK_TEXT || x.kind == TK_WSTR || x.kind == TK_LSTR)
            fprintf(f, "%s: \"%s\"\n", x.key.c_str(), std::string(x.bytes.begin(), x.bytes.end()).c_str());
        else
            fprintf(f, "%s: %lld\n", x.key.c_str(), x.value);
    }

    fclose(f);
    return 0;
}

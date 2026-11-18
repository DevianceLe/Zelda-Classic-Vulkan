// ZQuest's hero editor for 2.55 and 3.0 Nightly quests (Dear ImGui): the hero's sprites for every
// animation the quest's LINK section holds (walking, slashing, lifting, drowning, falling,
// sideview swimming and the rest), the hero's defenses, and the hero settings upstream keeps
// beside them (animation style, swim speed, big hitbox, diagonal movement, the item hitbox,
// the hammer offsets). A port of upstream's HeroEditorDialog (zq/dialog/heroeditor.cpp, 3.0) and
// 2.55.17's hero tile dialog (zq_custom.cpp onCustomHero, which also shows the hammer offsets):
// the same tabs and swatches; clicking a swatch picks its tile (and size) in ZQuest's tile
// picker, as upstream's SelHeroTileSwatch does. The swatches show each sprite's first frame
// (upstream animates them).
//
// What it changes and where it goes:
// - the LINK section: as a whole record in the quest's own LINK version (zq_tape_link.cpp
//   schema), put in with zq255_link_commit (zqw_init.cpp), which also sets ZQuest's 2.53 view
//   (walkspr...) and keeps the record for the LINK writer;
// - animation style, the item hitbox (3.0, init data 48+) and the init data's copy of the swim
//   speed (init data 37+; the LINK one wins when upstream loads): the INIT record, through
//   zq255_init_commit like the init data editor;
// - "Big Hero Hitbox" and "Diagonal Movement" (qr_LTTPCOLLISION, qr_LTTPWALK): the rules, through
//   zq255_rules_put like the rules editor.
// A field the quest's own versions don't hold isn't shown. 2.53 quests keep 2.53's Link sprite
// dialog.
//
// The window is a UI layer (the platform thread, UI lock held) shown during a modal session the
// game thread holds open; the game thread draws the swatches and runs the tile picker.
//
// Written for the port (no jwin or Allegro-derived code).

#include "precompiled.h" //always first

#include <allegro.h>

#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "tiles.h"
#include "zcport/ui.h"
#include "zq_imgui_fit.h"
#include "zq_q255_store.h"
#include "zq_item_names.h"
#include "zq_tape.h"

#include <imgui.h>
#include <imgui_internal.h>

extern bool saved;
extern int q255_header_version[3];
extern int ex;      // zq_tiles.cpp: the size the tile picker ended on
int select_tile(int &tile, int &flip, int type, int &cs, bool edit_cs, int exnow, bool always_use_flip);
bool zq255_link_record(std::vector<byte> &rec, int *version);       // zqw_init.cpp
bool zq255_link_commit(const std::vector<byte> &rec, std::string &why);
bool zq255_init_record(std::vector<byte> &rec, int *version);
bool zq255_init_commit(const std::vector<byte> &rec, std::string &why);

namespace
{

const int CELL = 32;        // a swatch's cell in the atlas: a 32x32 sprite at most
const int ATLAS_COLS = 16;
const int ATLAS_ROWS = 16;  // 256 sprites; LINK version 16 has 170

// ---- what the window shows ------------------------------------------------------------

// A sprite set: four directions ("walk.up" ...), or a single sprite named in full.
struct SpriteSet
{
    const char *key;        // the tape's key prefix
    const char *tab;        // the sub-tab
    int from;               // the LINK version that holds it
    bool dirs;              // four directions; otherwise a single sprite
    const char *label;      // the single sprite's label
};

const char *const g_dirs[4] = { "up", "down", "left", "right" };
const char *const g_dir_labels[4] = { "Up", "Down", "Left", "Right" };

// upstream 3.0's tabs, in its order
const SpriteSet g_land[] =
{
    { "walk", "Walk", 5, true, NULL },
    { "slash", "Slash", 5, true, NULL },
    { "revslash", "Slash 2", 15, true, NULL },
    { "stab", "Stab", 5, true, NULL },
    { "pound", "Pound", 5, true, NULL },
    { "jump", "Jump", 5, true, NULL },
    { "charge", "Charge", 5, true, NULL },
    { "hold_land.1", "Misc", 5, false, "1 Hand" },
    { "cast", "Misc", 5, false, "Casting" },
    { "hold_land.2", "Misc", 5, false, "2 Hands" },
    { "falling", "Falling", 7, true, NULL },
    { "lifting", "Lifting", 7, true, NULL },
    { "liftwalk", "Lift + Walk", 7, true, NULL }
};

const SpriteSet g_liquid[] =
{
    { "float", "Float", 5, true, NULL },
    { "swim", "Swim", 5, true, NULL },
    { "dive", "Dive", 5, true, NULL },
    { "drown", "Drown", 7, true, NULL },
    { "hold_water.1", "Hold", 5, false, "1 Hand" },
    { "hold_water.2", "Hold", 5, false, "2 Hands" },
    { "lavadrown", "Lava Drown", 7, true, NULL }
};

// upstream's Misc tab shows the side drowning sprite facing up only; the others are on their own tab
const SpriteSet g_side[] =
{
    { "sideswim", "Swim", 9, true, NULL },
    { "sideswimslash", "S. Slash", 10, true, NULL },
    { "sideswimstab", "S. Stab", 10, true, NULL },
    { "sideswimpound", "S. Pound", 10, true, NULL },
    { "sideswimcharge", "S. Charge", 10, true, NULL },
    { "sideswimhold.1", "Misc", 12, false, "1 Hand" },
    { "sideswimhold.2", "Misc", 12, false, "2 Hands" },
    { "sideswimcast", "Misc", 13, false, "Casting" },
    { "sidedrown.up", "Misc", 14, false, "Drown" },
    { "sidedrown", "S. Drown", 14, true, NULL }
};

// in the file from version 7, not on upstream's dialogs (its engine doesn't draw them)
const SpriteSet g_kept[] =
{
    { "hold_land.sword", "Holding", 7, false, "Land: sword" },
    { "hold_water.sword", "Holding", 7, false, "Water: sword" },
    { "sideswimhold.sword", "Holding", 12, false, "Side swim: sword" },
    { "frozen", "Frozen", 7, true, NULL },
    { "frozen_water", "Frozen (water)", 7, true, NULL },
    { "onfire", "On fire", 7, true, NULL },
    { "onfire_water", "On fire (water)", 7, true, NULL },
    { "digging", "Digging", 7, true, NULL },
    { "usingrod", "Rod", 7, true, NULL },
    { "usingcane", "Cane", 7, true, NULL },
    { "pushing", "Pushing", 7, true, NULL },
    { "stunned", "Stunned", 7, true, NULL },
    { "stunned_water", "Stunned (water)", 7, true, NULL },
    { "shocked", "Shocked", 7, true, NULL },
    { "shocked_water", "Shocked (water)", 7, true, NULL },
    { "pullsword", "Pull sword", 7, true, NULL },
    { "reading", "Reading", 7, true, NULL },
    { "slash180", "Slash 180", 7, true, NULL },
    { "slashz4", "Slash (Z4)", 7, true, NULL },
    { "dash", "Dash", 7, true, NULL },
    { "bonk", "Bonk", 7, true, NULL },
    { "medallion.1", "Medallion", 7, false, "Medallion 1" },
    { "medallion.2", "Medallion", 7, false, "Medallion 2" },
    { "medallion.3", "Medallion", 7, false, "Medallion 3" }
};

// upstream's Defenses tabs (heroeditor.cpp): label and weapon type (core/zdefs.h)
struct Def
{
    const char *tab, *label;
    int weapon;
};

const Def g_defs[] =
{
    { "Enemy 1", "Fireball", 129 }, { "Enemy 1", "Boomerang", 131 }, { "Enemy 1", "Arrow", 130 },
    { "Enemy 1", "Sword Beam", 132 }, { "Enemy 1", "Rock", 133 }, { "Enemy 1", "Magic", 134 },
    { "Enemy 1", "Bomb (object)", 135 }, { "Enemy 1", "S. Bomb (object)", 136 }, { "Enemy 1", "Bomb (explode)", 137 },
    { "Enemy 1", "S. Bomb (explode)", 138 },
    { "Enemy 2", "Fire Trail", 139 }, { "Enemy 2", "Flame", 140 }, { "Enemy 2", "Wind", 141 }, { "Enemy 2", "Flame 2", 142 },
    { "Enemy 2", "Flame 2 Trail", 143 }, { "Enemy 2", "Ice", 144 }, { "Enemy 2", "Fireball 2", 145 },
    { "Other", "Candle Fire", 9 }, { "Other", "Player Bomb", 4 }, { "Other", "Refl. Magic", 16 },
    { "Other", "Refl. Fireball", 17 }, { "Other", "Refl. Rock", 18 }, { "Other", "Refl. Sword Beam", 28 },
    { "Other", "Refl. Arrow", 53 }, { "Other", "Refl. Fire", 54 }, { "Other", "Refl. Fire 2", 55 },
    { "Custom", "Custom Weapon 1", 31 }, { "Custom", "Custom Weapon 2", 32 }, { "Custom", "Custom Weapon 3", 33 },
    { "Custom", "Custom Weapon 4", 34 }, { "Custom", "Custom Weapon 5", 35 }, { "Custom", "Custom Weapon 6", 36 },
    { "Custom", "Custom Weapon 7", 37 }, { "Custom", "Custom Weapon 8", 38 }, { "Custom", "Custom Weapon 9", 39 },
    { "Custom", "Custom Weapon 10", 40 }
};

const char *const g_anim_styles[4] = { "Original", "BS-Zelda", "Zelda 3", "Zelda 3 (Slow-Walk)" };

struct Sprite
{
    int tile, flip, ext;

    bool operator==(const Sprite &o) const { return tile == o.tile && flip == o.flip && ext == o.ext; }
};

struct HeroEd
{
    bool open, close, want_ok, focus, busy;
    bool is30;
    int link_v, init_v;         // the quest's LINK and INIT versions (init: 0 when it can't be edited)
    Tape link, init;            // the records as edited
    std::vector<byte> link_orig, init_orig;
    bool have_rules;
    bool big_hitbox, diagonal;  // the two rules as edited
    bool big_hitbox_orig, diagonal_orig;
    std::string error;
    std::string want_tile;      // a swatch was clicked: its key ("walk.up")

    // the swatches: every sprite of the tape, in tape order (keys), drawn by the game thread
    std::vector<std::string> keys;
    std::vector<unsigned int> px;   // RGBA, the whole atlas
    bool px_ready, px_dirty;
    ImTextureData *tex;             // made once, kept for later sessions

    HeroEd() : open(false), close(false), want_ok(false), focus(false), busy(false), is30(false), link_v(0), init_v(0),
               have_rules(false), big_hitbox(false), diagonal(false), big_hitbox_orig(false), diagonal_orig(false),
               px_ready(false), px_dirty(false), tex(NULL) {}
};

HeroEd g_he;

float sc() { return zcui::scale(); }


TapeField *lf(const std::string &key) { return g_he.link.find(key.c_str()); }

Sprite sprite_of(const Tape &t, const std::string &key)
{
    Sprite s;
    s.tile = (int)t.get((key + ".tile").c_str());
    s.flip = (int)t.get((key + ".flip").c_str());
    s.ext = (int)t.get((key + ".ext").c_str());
    return s;
}

int slot_of(const std::string &key)
{
    for(size_t i = 0; i < g_he.keys.size(); ++i)
        if(g_he.keys[i] == key)
            return (int)i;

    return -1;
}

// The atlas texture, uploaded from the game thread's pixels when they changed.
bool atlas_ready()
{
    HeroEd &e = g_he;

    if(!e.px_ready || e.px.size() != (size_t)ATLAS_COLS * CELL * ATLAS_ROWS * CELL)
        return false;

    if(!e.tex)
    {
        e.tex = IM_NEW(ImTextureData)();
        e.tex->Create(ImTextureFormat_RGBA32, ATLAS_COLS * CELL, ATLAS_ROWS * CELL);
        ImGui::RegisterUserTexture(e.tex);
        e.px_dirty = true;
    }

    if(e.px_dirty)
    {
        memcpy(e.tex->GetPixels(), &e.px[0], e.px.size() * 4);

        if(e.tex->Status == ImTextureStatus_OK || e.tex->Status == ImTextureStatus_WantUpdates)
            ImTextureDataQueueUpload(e.tex, 0, 0, ATLAS_COLS * CELL, ATLAS_ROWS * CELL);

        e.px_dirty = false;
    }

    return true;
}

// One sprite: its picture (click: the tile picker), tile number, flip and size.
void swatch(const std::string &key, const char *label)
{
    HeroEd &e = g_he;
    TapeField *t = lf(key + ".tile"), *fl = lf(key + ".flip"), *xt = lf(key + ".ext");

    if(!t || !fl || !xt)
        return;

    ImGui::PushID(key.c_str());
    ImGui::BeginGroup();
    ImGui::TextUnformatted(label);
    const int slot = slot_of(key);
    const float side = 64 * sc();

    if(slot >= 0 && slot < ATLAS_COLS * ATLAS_ROWS && atlas_ready())
    {
        const float w = (float)(ATLAS_COLS * CELL), h = (float)(ATLAS_ROWS * CELL);
        const int cx = slot % ATLAS_COLS, cy = slot / ATLAS_COLS;
        const ImVec2 uv0(cx * CELL / w, cy * CELL / h), uv1((cx + 1) * CELL / w, (cy + 1) * CELL / h);

        if(ImGui::ImageButton("pick", e.tex->GetTexRef(), ImVec2(side, side), uv0, uv1, ImVec4(0, 0, 0, 1)))
            e.want_tile = key;
    }
    else if(ImGui::Button("pick", ImVec2(side, side)))
        e.want_tile = key;

    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Click to pick this sprite's tile and size");

    int tile = (int)t->value;
    ImGui::SetNextItemWidth(110 * sc());

    if(ImGui::InputInt("Tile", &tile, 1, 20))
    {
        const int most = t->width == 2 ? 65535 : NEWMAXTILES - 1;
        t->value = tile < 0 ? 0 : tile > most ? most : tile;
    }

    static const char *const flips[4] = { "No flip", "Horizontal", "Vertical", "Both" };
    static const char *const sizes[3] = { "16x16", "16x32", "32x32" };
    int f = (int)fl->value & 3, x = (int)xt->value;
    ImGui::SetNextItemWidth(110 * sc());

    if(ImGui::Combo("Flip", &f, flips, 4))
        fl->value = f;

    ImGui::SetNextItemWidth(110 * sc());

    if(x >= 0 && x <= 2 && ImGui::Combo("Size", &x, sizes, 3))
        xt->value = x;

    if(TapeField *fr = lf(key + ".frames"))
    {
        // upstream: 1 to 255
        int n = (int)fr->value;
        ImGui::SetNextItemWidth(110 * sc());

        if(ImGui::InputInt("Num Frames", &n))
            fr->value = n < 1 ? 1 : n > 255 ? 255 : n;
    }

    ImGui::EndGroup();
    ImGui::PopID();
}

// a sub-tab's sprites: two to a row (upstream's Rows<4>: label and swatch, twice)
void sprite_tab(const SpriteSet *sets, int n, const char *tab)
{
    int col = 0;

    for(int i = 0; i < n; ++i)
    {
        const SpriteSet &s = sets[i];

        if(strcmp(s.tab, tab) != 0 || g_he.link_v < s.from)
            continue;

        for(int d = 0; d < (s.dirs ? 4 : 1); ++d)
        {
            if(col % 2)
                ImGui::SameLine(300 * sc());
            else if(col)
                ImGui::Spacing();

            if(s.dirs)
                swatch(std::string(s.key) + "." + g_dirs[d], g_dir_labels[d]);
            else
                swatch(s.key, s.label);

            ++col;
        }
    }
}

void sprite_tabs(const char *bar, const SpriteSet *sets, int n)
{
    if(!ImGui::BeginTabBar(bar))
        return;

    std::vector<std::string> tabs;

    for(int i = 0; i < n; ++i)
    {
        bool have = false;

        for(size_t k = 0; k < tabs.size() && !have; ++k)
            have = tabs[k] == sets[i].tab;

        if(!have && g_he.link_v >= sets[i].from)
            tabs.push_back(sets[i].tab);
    }

    for(size_t k = 0; k < tabs.size(); ++k)
        if(ImGui::BeginTabItem(tabs[k].c_str()))
        {
            if(ImGui::BeginChild("sprites"))
                sprite_tab(sets, n, tabs[k].c_str());

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

    ImGui::EndTabBar();
}

void defense_combo(const char *label, int weapon)
{
    char key[16];
    snprintf(key, sizeof(key), "def%d", weapon);
    TapeField *f = lf(key);

    if(!f)
        return;

    std::string cur;

    for(int k = 0; k < q30_defense_count(); ++k)
    {
        int v = 0;
        const char *n = q30_defense_at(k, &v);

        if(n && v == f->value)
            cur = n;
    }

    if(cur.empty())
    {
        char b[32];
        snprintf(b, sizeof(b), "%d", (int)f->value);
        cur = b;
    }

    ImGui::TextUnformatted(label);
    ImGui::SameLine(200 * sc());
    ImGui::SetNextItemWidth(220 * sc());

    if(ImGui::BeginCombo((std::string("##") + key).c_str(), cur.c_str(), ImGuiComboFlags_HeightLarge))
    {
        for(int k = 0; k < q30_defense_count(); ++k)
        {
            int v = 0;
            const char *n = q30_defense_at(k, &v);

            if(n && ImGui::Selectable(n, v == f->value))
                f->value = v;
        }

        ImGui::EndCombo();
    }
}

void defenses_tab()
{
    if(g_he.link_v < 8)
    {
        ImGui::TextWrapped("This quest's hero data (format %d) has no defenses: they came with format 8.", g_he.link_v);
        return;
    }

    if(!ImGui::BeginTabBar("defs"))
        return;

    static const char *const tabs[4] = { "Enemy 1", "Enemy 2", "Other", "Custom" };

    for(int t = 0; t < 4; ++t)
        if(ImGui::BeginTabItem(tabs[t]))
        {
            if(ImGui::BeginChild("d"))
            {
                for(size_t i = 0; i < sizeof(g_defs) / sizeof(g_defs[0]); ++i)
                    if(!strcmp(g_defs[i].tab, tabs[t]))
                    {
                        char l[64];
                        snprintf(l, sizeof(l), "%s Defense:", g_defs[i].label);
                        defense_combo(l, g_defs[i].weapon);

                        // upstream's "Set All", beside the first: the defenses the dialog shows take
                        // the fireball's, and so does slot 128, which upstream's OK also saves;
                        // the others (not shown) keep theirs, as upstream saves only those
                        if(t == 0 && i == 0)
                        {
                            ImGui::SameLine();

                            if(ImGui::Button("Set All"))
                            {
                                const TapeField *src = lf("def129");
                                const int64_t v = src ? src->value : 0;

                                for(size_t q = 0; src && q <= sizeof(g_defs) / sizeof(g_defs[0]); ++q)
                                {
                                    char key[16];
                                    snprintf(key, sizeof(key), "def%d", q < sizeof(g_defs) / sizeof(g_defs[0]) ? g_defs[q].weapon : 128);

                                    if(TapeField *d = g_he.link.find(key))
                                        d->value = v;
                                }
                            }
                        }
                    }
            }

            ImGui::EndChild();
            ImGui::EndTabItem();
        }

    ImGui::EndTabBar();
}

void help_marker(const char *text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");

    if(ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(400 * sc());
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void bounded(const char *label, TapeField *f, int lo, int hi)
{
    if(!f)
        return;

    int v = (int)f->value;
    ImGui::SetNextItemWidth(120 * sc());

    if(ImGui::InputInt(label, &v))
        f->value = v < lo ? lo : v > hi ? hi : v;
}

void other_tab()
{
    HeroEd &e = g_he;

    if(TapeField *a = e.init.find("animation_style"))
    {
        int s = (int)a->value;
        ImGui::SetNextItemWidth(220 * sc());

        if(s >= 0 && s < 4 && ImGui::Combo("Animation Style", &s, g_anim_styles, 4))
            a->value = s;
    }

    if(TapeField *sw = lf("swim_speed"))
    {
        static const char *const speeds[2] = { "Slow", "Fast" };
        int fast = sw->value > 60 ? 1 : 0;      // upstream's list: 50 and 67
        ImGui::SetNextItemWidth(220 * sc());

        if(ImGui::Combo("Swim Speed", &fast, speeds, 2))
        {
            sw->value = fast ? 67 : 50;

            // the init data keeps the same value from version 37 (upstream writes both)
            if(TapeField *is = e.init.find("swim_speed"))
                is->value = sw->value;
        }

        help_marker("Determines the player's speed in water, when 'New Hero Movement' / 'Newer Hero Movement' are disabled.");
    }

    if(e.have_rules)
    {
        ImGui::Checkbox("Big Hero Hitbox", &e.big_hitbox);
        help_marker("If checked, the player uses a 16x16 hitbox for many interactions (ex. solidity). Otherwise, the player uses "
                    "a 16x8 hitbox (offset to the bottom of the player sprite).");
        ImGui::Checkbox("Diagonal Movement", &e.diagonal);
        help_marker("If checked, the player can move diagonally, instead of only orthogonally.");
    }

    if(e.init.find("itembox_xofs"))
    {
        ImGui::SeparatorText("Item Hitbox");
        ImGui::TextWrapped("These values determine the area of the player that detects collision with items. Has no effect if "
                           "'Old Item Hitboxes' is enabled.");
        bounded("X Offset", e.init.find("itembox_xofs"), -128, 127);
        bounded("Y Offset", e.init.find("itembox_yofs"), -128, 127);
        bounded("Width", e.init.find("itembox_w"), 0, 255);
        bounded("Height", e.init.find("itembox_h"), 0, 255);
    }

    if(lf("hammer.up"))
    {
        // 2.55.17's dialog has them (3.0's keeps them in the file but doesn't show them)
        ImGui::SeparatorText("Hammer Offsets");

        for(int d = 0; d < 4; ++d)
        {
            TapeField *h = lf(std::string("hammer.") + g_dirs[d]);
            int v = h ? (int)h->value : 0;
            ImGui::SetNextItemWidth(120 * sc());

            if(h && ImGui::InputInt(g_dir_labels[d], &v))
                h->value = v;
        }
    }
}

void editor_layer(void *)
{
    HeroEd &e = g_he;

    if(!e.open || e.busy)
        return;

    zq_fit_next_window(760 * sc(), 640 * sc());

    if(e.focus)
    {
        ImGui::SetNextWindowFocus();
        e.focus = false;
    }

    char title[160];
    snprintf(title, sizeof(title), "Hero Data (%s, hero data format %d)###zqhero", e.is30 ? "3.0 Nightly" : "2.55", e.link_v);
    bool open = true;

    if(ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        zq_keep_window_inside(760 * sc(), 640 * sc());
        ImGui::TextDisabled("Edit the Hero's sprites, defenses, and other settings");
        const float bottom = ImGui::GetFrameHeightWithSpacing() * (e.error.empty() ? 1 : 2) + ImGui::GetStyle().ItemSpacing.y;

        if(ImGui::BeginChild("body", ImVec2(0, -bottom)))
        {
            if(ImGui::BeginTabBar("main"))
            {
                if(ImGui::BeginTabItem("Sprites"))
                {
                    if(ImGui::BeginTabBar("sprites"))
                    {
                        if(ImGui::BeginTabItem("Land"))
                        {
                            sprite_tabs("land", g_land, (int)(sizeof(g_land) / sizeof(g_land[0])));
                            ImGui::EndTabItem();
                        }

                        if(ImGui::BeginTabItem("Liquid"))
                        {
                            sprite_tabs("liquid", g_liquid, (int)(sizeof(g_liquid) / sizeof(g_liquid[0])));
                            ImGui::EndTabItem();
                        }

                        if(e.link_v >= 9 && ImGui::BeginTabItem("Side Liquid"))
                        {
                            sprite_tabs("side", g_side, (int)(sizeof(g_side) / sizeof(g_side[0])));
                            ImGui::EndTabItem();
                        }

                        if(e.link_v >= 7 && ImGui::BeginTabItem("Kept"))
                        {
                            ImGui::TextWrapped("These sprites are in the quest's hero data, but upstream's hero editor doesn't show "
                                               "them and its engine doesn't draw them; they are kept as they are.");
                            sprite_tabs("kept", g_kept, (int)(sizeof(g_kept) / sizeof(g_kept[0])));
                            ImGui::EndTabItem();
                        }

                        ImGui::EndTabBar();
                    }

                    ImGui::EndTabItem();
                }

                if(ImGui::BeginTabItem("Defenses"))
                {
                    defenses_tab();
                    ImGui::EndTabItem();
                }

                if(ImGui::BeginTabItem("Other"))
                {
                    other_tab();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }

        ImGui::EndChild();

        if(!e.error.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", e.error.c_str());

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

// ---- the game thread ------------------------------------------------------------------

// Draws every sprite into the atlas, as 2.53's Link tile dialog draws its swatches (CSet 6).
void draw_atlas(const std::vector<Sprite> &spr)
{
    BITMAP *b = create_bitmap_ex(8, CELL, CELL);

    if(!b)
        return;

    PALETTE pal;
    get_palette(pal);
    const int W = ATLAS_COLS * CELL;
    std::vector<unsigned int> px((size_t)W * ATLAS_ROWS * CELL, 0xFF000000u);

    for(size_t i = 0; i < spr.size() && i < (size_t)(ATLAS_COLS * ATLAS_ROWS); ++i)
    {
        const Sprite &s = spr[i];
        clear_to_color(b, 0);
        const int side = s.flip & 1 ? -1 : 1;

        switch(s.ext)
        {
        case 0:
            overtile16(b, s.tile, 8, 16, 6, s.flip);
            break;

        case 1:
            overtile16(b, s.tile - TILES_PER_ROW, 8, 0, 6, s.flip);
            overtile16(b, s.tile, 8, 16, 6, s.flip);
            break;

        default:
            overtile16(b, s.tile - TILES_PER_ROW, 8, 0, 6, s.flip);          // top middle
            overtile16(b, s.tile - TILES_PER_ROW - side, -8, 0, 6, s.flip);  // top left
            overtile16(b, s.tile - TILES_PER_ROW + side, 24, 0, 6, s.flip);  // top right
            overtile16(b, s.tile - side, -8, 16, 6, s.flip);                 // bottom left
            overtile16(b, s.tile + side, 24, 16, 6, s.flip);                 // bottom right
            overtile16(b, s.tile, 8, 16, 6, s.flip);                         // bottom middle
            break;
        }

        const int ox = (int)(i % ATLAS_COLS) * CELL, oy = (int)(i / ATLAS_COLS) * CELL;

        for(int y = 0; y < CELL; ++y)
            for(int x = 0; x < CELL; ++x)
            {
                const int c = b->line[y][x];
#ifdef ZC_ENGINE
                const int r = pal[c].r, gg = pal[c].g, bl = pal[c].b;
#else
                const int r = (pal[c].r << 2) | (pal[c].r >> 4), gg = (pal[c].g << 2) | (pal[c].g >> 4),
                          bl = (pal[c].b << 2) | (pal[c].b >> 4);
#endif
                px[(size_t)(oy + y) * W + ox + x] = 0xFF000000u | ((unsigned)bl << 16) | ((unsigned)gg << 8) | (unsigned)r;
            }
    }

    destroy_bitmap(b);
    zcui::Lock lock;
    g_he.px.swap(px);
    g_he.px_ready = true;
    g_he.px_dirty = true;
}

bool rule_bit(const ZqRuleSection &s, int bit)
{
    return bit / 8 < (int)s.total && ((s.rules[bit / 8] >> (bit % 8)) & 1);
}

void set_rule_bit(ZqRuleSection &s, int bit, bool on)
{
    if(bit / 8 >= (int)s.total)
        return;

    if(on)
        s.rules[bit / 8] |= (byte)(1 << (bit % 8));
    else
        s.rules[bit / 8] &= (byte)~(1 << (bit % 8));
}

// OK: the changes go in (init data first: the LINK section's swim speed wins, as when upstream
// loads), or the window stays open with the reason one can't.
bool commit(std::string &why)
{
    Tape link, init;
    std::vector<byte> link_orig, init_orig;
    int init_v;
    bool have_rules, big, diag, big0, diag0;
    {
        zcui::Lock lock;
        link = g_he.link;
        init = g_he.init;
        link_orig = g_he.link_orig;
        init_orig = g_he.init_orig;
        init_v = g_he.init_v;
        have_rules = g_he.have_rules;
        big = g_he.big_hitbox;
        diag = g_he.diagonal;
        big0 = g_he.big_hitbox_orig;
        diag0 = g_he.diagonal_orig;
    }

    std::vector<byte> lrec, irec;
    link.encode(lrec);

    if(init_v)
        init.encode(irec);

    const bool link_changed = lrec != link_orig;
    const bool init_changed = init_v && irec != init_orig;
    const bool rules_changed = have_rules && (big != big0 || diag != diag0);

    if(init_changed && !zq255_init_commit(irec, why))
    {
        why = "The init data part couldn't be put in: " + why + ".";
        return false;
    }

    if(link_changed && !zq255_link_commit(lrec, why))
    {
        why = "The hero data couldn't be put in: " + why + ".";
        return false;
    }

    if(rules_changed)
    {
        ZqRuleSection s;

        if(!zq255_rules_get(s))
        {
            why = "The quest's rules couldn't be read.";
            return false;
        }

        set_rule_bit(s, qr_LTTPCOLLISION, big);
        set_rule_bit(s, qr_LTTPWALK, diag);

        if(!zq255_rules_put(s, why))
        {
            why = "The rules couldn't be put in: " + why + ".";
            return false;
        }
    }

    if(link_changed || init_changed || rules_changed)
        saved = false;

    return true;
}

// the keys of every sprite in the tape, in tape order (the atlas slots)
std::vector<std::string> sprite_keys(const Tape &t)
{
    std::vector<std::string> keys;

    for(size_t i = 0; i < t.f.size(); ++i)
    {
        const std::string &k = t.f[i].key;

        if(k.size() > 5 && k.compare(k.size() - 5, 5, ".tile") == 0)
            keys.push_back(k.substr(0, k.size() - 5));
    }

    return keys;
}

} // namespace

// Graphics > Sprites > Link (2.55 and 3.0 Nightly quests). *shown: false when the open quest's
// hero data can't be edited this way (the caller then uses 2.53's dialog). True when OK was
// pressed.
bool zq_edit_hero_newer(bool *shown)
{
    std::vector<byte> lrec, irec;
    int lv = 0, iv = 0;
    *shown = zq255_quest_is_newer() && zq255_link_record(lrec, &lv);

    if(!*shown)
        return false;

    Tape link, init;
    TapeParser lp(lrec.empty() ? NULL : &lrec[0], lrec.size(), 0, &link);

    if(!zq_tape_link(lp, lv) || lp.pos != lrec.size())
    {
        al_trace("Hero editor: the LINK section (format %d) doesn't read as its schema says\n", lv);
        *shown = false;
        return false;
    }

    if(zq255_init_record(irec, &iv))
    {
        TapeParser ip(irec.empty() ? NULL : &irec[0], irec.size(), 0, &init);

        if(!zq_tape_init(ip, iv) || ip.pos != irec.size())
            iv = 0;     // the init data's settings aren't offered then
    }
    else
        iv = 0;

    ZqRuleSection rs;
    const bool have_rules = zq255_rules_get(rs);

    if(!zcui::available())
        return false;

    {
        zcui::Lock lock;

        if(g_he.open)
            return false;

        g_he.open = true;
        g_he.close = g_he.want_ok = g_he.busy = false;
        g_he.focus = true;
        g_he.is30 = q255_header_version[0] >= 3;
        g_he.link_v = lv;
        g_he.init_v = iv;
        g_he.link = link;
        g_he.init = iv ? init : Tape();
        g_he.link_orig = lrec;
        g_he.init_orig = iv ? irec : std::vector<byte>();
        g_he.have_rules = have_rules;
        g_he.big_hitbox = g_he.big_hitbox_orig = have_rules && rule_bit(rs, qr_LTTPCOLLISION);
        g_he.diagonal = g_he.diagonal_orig = have_rules && rule_bit(rs, qr_LTTPWALK);
        g_he.error.clear();
        g_he.want_tile.clear();
        g_he.keys = sprite_keys(link);
        g_he.px_ready = false;
    }

    const int layer = zcui::add_layer(editor_layer, NULL, 600);
    zcui::begin_session();
    bool accepted = false;
    std::vector<Sprite> drawn;

    for(;;)
    {
        zcui::wait_frame(100);
        bool done, ok;
        std::string pick;
        std::vector<Sprite> now;
        {
            zcui::Lock lock;
            done = g_he.close;
            ok = g_he.want_ok;
            g_he.want_ok = false;
            pick.swap(g_he.want_tile);

            for(size_t i = 0; i < g_he.keys.size(); ++i)
                now.push_back(sprite_of(g_he.link, g_he.keys[i]));
        }

        if(done || !zcui::available())
            break;

        if(ok)
        {
            std::string why;

            if(commit(why))
            {
                accepted = true;
                break;
            }

            zcui::Lock lock;
            g_he.error = why;
            continue;
        }

        if(!pick.empty())
        {
            // ZQuest's tile picker, as upstream's swatch opens it (CSet 6, the sprite's size)
            Sprite s;
            {
                zcui::Lock lock;
                g_he.busy = true;
                s = sprite_of(g_he.link, pick);
            }

            int t = s.tile, f = s.flip, cs = 6;

            // The picker is one of ZQuest's own dialogs, which see the keyboard and mouse only
            // outside a UI session: the session is closed while it runs (this window is hidden).
            zcui::end_session();
            const bool picked = select_tile(t, f, 2, cs, false, s.ext, true);
            zcui::begin_session();

            // the keys and clicks that closed the picker go by while this window is still hidden
            for(int k = 0; k < 3; ++k)
                zcui::wait_frame(100);

            if(picked)
            {
                zcui::Lock lock;
                TapeField *tf = g_he.link.find((pick + ".tile").c_str());
                TapeField *ff = g_he.link.find((pick + ".flip").c_str());
                TapeField *xf = g_he.link.find((pick + ".ext").c_str());

                if(tf && ff && xf)
                {
                    const int most = tf->width == 2 ? 65535 : NEWMAXTILES - 1;
                    tf->value = t < 0 ? 0 : t > most ? most : t;
                    ff->value = f & 3;
                    xf->value = ex < 0 ? 0 : ex > 2 ? 2 : ex;
                }
            }

            zcui::Lock lock;
            g_he.busy = false;
            g_he.focus = true;
            continue;
        }

        if(!(now.size() == drawn.size() && std::equal(now.begin(), now.end(), drawn.begin())))
        {
            draw_atlas(now);
            drawn = now;
        }
    }

    zcui::end_session();
    zcui::remove_layer(layer);
    zcui::Lock lock;
    g_he.open = false;
    return accepted;
}

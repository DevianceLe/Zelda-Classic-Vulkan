// ZQuest's editors (Dear ImGui) for 3.0 Nightly's scrolling regions and for the combo pools and
// autocombos of 2.55.17 and 3.0 Nightly quests, ported from upstream ZQuest Classic's (GPL):
//  - Edit > Maps > Scrolling Regions: zq/dialog/edit_region.cpp with zq/gui/regiongrid.cpp
//    (paint region ids on a map's 8 x 16 screens; every region a rectangle, not touching another
//    of the same id, as core/mapscr.cpp get_all_region_descriptions checks);
//  - Quest > Graphics > Combo Pools: zq/dialog/combopool.cpp (each pool a weighted list of
//    combos, each with its own cset or the one drawn with);
//  - Quest > Graphics > Autocombos: zq/gui/edit_autocombo.cpp (a type, its combo slots with
//    upstream's engravings beside them, Auto Generate, and each type's settings).
// Upstream edits one pool or autocombo per dialog; here the list of them is on the left, and
// the one picked there when OK is pressed is the one the map editor's Pool or Autocombo mode
// draws with. The edits go into ZQuest's store (zq_cpool.cpp): the combo alias and map writers
// write them in the quest's own format, and refuse what it can't hold.
//
// Written for the port (no jwin or Allegro code). 2.53's combo picker (select_combo_2) chooses
// combos and putcombo draws them, both on the game thread, as the combo editor's tile picker.

#include "precompiled.h" //always first

#include <allegro.h>

#include <algorithm>
#include <map>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_class.h"
#include "zcport/ui.h"
#include "zq_autocombo.h"
#include "zq_engravings.h"

#include <imgui.h>
#include <imgui_internal.h>

extern bool saved;
extern int CSet, Combo;
extern newcombo *combobuf;
extern word map_count;
int select_combo_2(int &tile, int &cs);
void putcombo(BITMAP *dest, int x, int y, int cmbdat, int cset);
const char *zq_autocombo_type_name(int type);

namespace
{

int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// ---- combo pictures -------------------------------------------------------------------
//
// The windows draw combos from one texture of 16x16 cells. The UI thread asks for a combo in a
// cset; the game thread (each editor's session loop) draws what was asked with putcombo and the
// current palette, and the next frame shows it.

struct Atlas
{
    enum { COLS = 32, ROWS = 32, PX = 16, W = COLS * PX, H = ROWS * PX, CELLS = COLS * ROWS };
    ImTextureData *tex;
    std::vector<unsigned int> px;   // W x H, RGBA
    std::map<int, int> cell;        // combo * 16 + cset -> its cell (-1: asked, not drawn yet)
    std::vector<int> asked;         // keys to draw
    int used;
    bool dirty;

    Atlas() : tex(NULL), used(0), dirty(false) {}
};

Atlas g_atlas;
ImTextureData *g_engrave_tex = NULL;

// UI thread: the texture, made the first time and sent again when the game thread drew more.
void atlas_upload()
{
    Atlas &a = g_atlas;

    if(!a.tex)
    {
        a.tex = IM_NEW(ImTextureData)();
        a.tex->Create(ImTextureFormat_RGBA32, Atlas::W, Atlas::H);
        ImGui::RegisterUserTexture(a.tex);

        if(a.px.size() != (size_t)Atlas::W * Atlas::H)
            a.px.assign((size_t)Atlas::W * Atlas::H, 0xFF000000u);

        a.dirty = true;
    }

    if(a.dirty)
    {
        memcpy(a.tex->GetPixels(), &a.px[0], a.px.size() * 4);

        if(a.tex->Status == ImTextureStatus_OK || a.tex->Status == ImTextureStatus_WantUpdates)
            ImTextureDataQueueUpload(a.tex, 0, 0, Atlas::W, Atlas::H);

        a.dirty = false;
    }
}

// UI thread: a combo as a button s pixels wide (black while it's being drawn, or for none).
// True when clicked.
bool combo_swatch(const char *id, int cid, int cs, float s)
{
    Atlas &a = g_atlas;
    int c = -1;

    if(cid >= 0 && cid < MAXCOMBOS)
    {
        const int key = cid * 16 + (cs & 15);
        std::map<int, int>::iterator it = a.cell.find(key);

        if(it == a.cell.end())
        {
            a.cell[key] = -1;
            a.asked.push_back(key);
        }
        else
            c = it->second;
    }

    if(c >= 0 && a.tex)
    {
        const ImVec2 uv0((c % Atlas::COLS) * (float)Atlas::PX / Atlas::W, (c / Atlas::COLS) * (float)Atlas::PX / Atlas::H);
        const ImVec2 uv1(uv0.x + (float)Atlas::PX / Atlas::W, uv0.y + (float)Atlas::PX / Atlas::H);
        return ImGui::ImageButton(id, a.tex->GetTexRef(), ImVec2(s, s), uv0, uv1);
    }

    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 1));
    const bool r = ImGui::Button(id, ImVec2(s + pad.x * 2, s + pad.y * 2));
    ImGui::PopStyleColor();
    return r;
}

// Game thread, at each editor's opening: what was drawn before may be out of date.
void atlas_reset()
{
    zcui::Lock lock;
    g_atlas.cell.clear();
    g_atlas.asked.clear();
    g_atlas.used = 0;
}

// Game thread: draws the combos the windows asked for.
void atlas_service()
{
    std::vector<int> todo;
    {
        zcui::Lock lock;
        todo.swap(g_atlas.asked);

        // full: start again (what's on show is asked for again next frame)
        if(!todo.empty() && g_atlas.used + (int)todo.size() > Atlas::CELLS)
        {
            g_atlas.cell.clear();
            g_atlas.used = 0;
            todo.clear();
        }
    }

    if(todo.empty())
        return;

    PALETTE pal;
    get_palette(pal);
    BITMAP *b = create_bitmap_ex(8, 16, 16);

    if(!b)
        return;

    std::vector<std::vector<unsigned int> > pics(todo.size(), std::vector<unsigned int>(256));

    for(size_t k = 0; k < todo.size(); ++k)
    {
        clear_to_color(b, 0);
        putcombo(b, 0, 0, todo[k] / 16, todo[k] % 16);

        for(int y = 0; y < 16; ++y)
            for(int x = 0; x < 16; ++x)
            {
                const int i = b->line[y][x];
#ifdef ZC_ENGINE
                const int r = pal[i].r, gg = pal[i].g, bl = pal[i].b;
#else
                const int r = (pal[i].r << 2) | (pal[i].r >> 4), gg = (pal[i].g << 2) | (pal[i].g >> 4),
                          bl = (pal[i].b << 2) | (pal[i].b >> 4);
#endif
                pics[k][y * 16 + x] = 0xFF000000u | ((unsigned)bl << 16) | ((unsigned)gg << 8) | (unsigned)r;
            }
    }

    destroy_bitmap(b);
    zcui::Lock lock;
    Atlas &a = g_atlas;

    if(a.px.size() != (size_t)Atlas::W * Atlas::H)
        a.px.assign((size_t)Atlas::W * Atlas::H, 0xFF000000u);

    for(size_t k = 0; k < todo.size() && a.used < Atlas::CELLS; ++k)
    {
        const int c = a.used++;
        const int cx = (c % Atlas::COLS) * Atlas::PX, cy = (c / Atlas::COLS) * Atlas::PX;

        for(int y = 0; y < 16; ++y)
            memcpy(&a.px[(size_t)(cy + y) * Atlas::W + cx], &pics[k][y * 16], 16 * 4);

        a.cell[todo[k]] = c;
    }

    a.dirty = true;
}

// UI thread: one of upstream's engravings (the picture beside an autocombo slot), s pixels.
void engraving_image(int idx, float s)
{
    enum { COLS = 32, ROWS = 16, W = COLS * 16, H = ROWS * 16 };

    if(!g_engrave_tex)
    {
        g_engrave_tex = IM_NEW(ImTextureData)();
        g_engrave_tex->Create(ImTextureFormat_RGBA32, W, H);
        ImGui::RegisterUserTexture(g_engrave_tex);
        unsigned int *px = (unsigned int *)g_engrave_tex->GetPixels();
        memset(px, 0, (size_t)W * H * 4);

        for(int i = 0; i < 512; ++i)
        {
            const char *e = zq_engravings[i];

            if(!e)
                continue;

            const int ox = (i % COLS) * 16, oy = (i / COLS) * 16;

            for(int p = 0; p < 256 && e[p]; ++p)
            {
                const char ch = e[p];
                const int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : 0;
                px[(size_t)(oy + p / 16) * W + ox + p % 16] = zq_engraving_rgba[d];
            }
        }

        if(g_engrave_tex->Status == ImTextureStatus_OK || g_engrave_tex->Status == ImTextureStatus_WantUpdates)
            ImTextureDataQueueUpload(g_engrave_tex, 0, 0, W, H);
    }

    if(idx < 0 || idx >= 512 || !zq_engravings[idx])
    {
        ImGui::Dummy(ImVec2(s, s));
        return;
    }

    const ImVec2 uv0((idx % COLS) * 16.0f / W, (idx / COLS) * 16.0f / H);
    const ImVec2 uv1(uv0.x + 16.0f / W, uv0.y + 16.0f / H);
    ImGui::Image(g_engrave_tex->GetTexRef(), ImVec2(s, s), uv0, uv1);
}

void help_marker(const char *text)
{
    ImGui::TextDisabled("(?)");

    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

// ---- what every editor's session shares ------------------------------------------------

enum { EK_NONE, EK_POOLS, EK_AUTOS, EK_REGIONS };

// What 2.53's combo picker is choosing for
enum
{
    PK_NONE,
    PK_POOL_ADD,        // a new pool entry
    PK_POOL_ENTRY,      // pool entry pick_arg
    PK_AUTO_SLOT,       // autocombo slot pick_arg
    PK_AUTO_ERASE,
    PK_AUTO_DISPLAY,
    PK_AUTO_GENERATE    // Auto Generate from the picked combo (pick_shift: fill the empty slots)
};

struct Session
{
    bool open, close, want_ok, busy, focus;
    int kind;
    std::string error, note;
    // a combo asked of 2.53's picker: the game thread runs it and leaves the answer here
    bool pick_want, pick_done, pick_ok, pick_shift;
    int pick_target, pick_arg, pick_cid, pick_cs;

    Session() : open(false), close(false), want_ok(false), busy(false), focus(false), kind(EK_NONE), pick_want(false),
        pick_done(false), pick_ok(false), pick_shift(false), pick_target(PK_NONE), pick_arg(0), pick_cid(0), pick_cs(0) {}
};

Session g_s;

// UI thread: asks for a combo (starting at cid, cs) for the target.
void ask_pick(int target, int arg, int cid, int cs, bool shift = false)
{
    g_s.pick_want = true;
    g_s.pick_target = target;
    g_s.pick_arg = arg;
    g_s.pick_cid = cid;
    g_s.pick_cs = cs;
    g_s.pick_shift = shift;
}

void ok_cancel_row()
{
    const float sc = zcui::scale();

    if(!g_s.error.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", g_s.error.c_str());
    else if(!g_s.note.empty())
        ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.0f, 1.0f), "%s", g_s.note.c_str());

    if(ImGui::Button("OK", ImVec2(90 * sc, 0)))
        g_s.want_ok = true;

    ImGui::SameLine();

    if(ImGui::Button("Cancel", ImVec2(90 * sc, 0)))
        g_s.close = true;

    if(!ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup) &&
       ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        g_s.close = true;
}

// The window, for one frame of an editor's layer; false (and nothing to end) while the game
// thread is busy with the combo picker.
bool begin_window(const char *title, float w, float h)
{
    if(!g_s.open || g_s.busy)
        return false;

    const float sc = zcui::scale();
    ImGuiViewport *vp = ImGui::GetMainViewport();
    // the dialog's size, or ZQuest's window's when that is smaller (it can change any time)
    const ImVec2 room(vp->WorkSize.x, vp->WorkSize.y);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImMin(w * sc, room.x), ImMin(h * sc, room.y)), ImGuiCond_Always);

    if(g_s.focus)
    {
        ImGui::SetNextWindowFocus();
        g_s.focus = false;
    }

    bool open = true;
    const bool shown = ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

    if(!open)
        g_s.close = true;

    if(!shown)
    {
        ImGui::End();
        return false;
    }

    // kept inside ZQuest's window when that shrinks
    const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    const float nx = ImMax(vp->WorkPos.x, ImMin(pos.x, vp->WorkPos.x + room.x - size.x));
    const float ny = ImMax(vp->WorkPos.y, ImMin(pos.y, vp->WorkPos.y + room.y - size.y));

    if(nx != pos.x || ny != pos.y)
        ImGui::SetWindowPos(ImVec2(nx, ny));

    atlas_upload();
    return true;
}

// The game thread: runs the editor's window until OK (and the commit took) or Cancel.
bool run_session(int kind, zcui::LayerFn layer, bool (*commit)())
{
    {
        zcui::Lock lock;
        g_s.open = true;
        g_s.close = g_s.want_ok = g_s.busy = false;
        g_s.focus = true;
        g_s.kind = kind;
        g_s.error.clear();
        g_s.note.clear();
        g_s.pick_want = g_s.pick_done = false;
    }

    atlas_reset();
    const int id = zcui::add_layer(layer, NULL, 600);
    zcui::begin_session();
    bool accepted = false;

    for(;;)
    {
        zcui::wait_frame(100);
        atlas_service();
        bool done, ok, want_pick;
        int cid, cs;
        {
            zcui::Lock lock;
            done = g_s.close;
            ok = g_s.want_ok;
            g_s.want_ok = false;
            want_pick = g_s.pick_want;
            g_s.pick_want = false;
            cid = g_s.pick_cid;
            cs = g_s.pick_cs;

            if(want_pick)
                g_s.busy = true;
        }

        if(want_pick)
        {
            // 2.53's picker takes the keyboard and mouse itself: no session while it runs
            // (a session sends all input to the UI); the window is hidden meanwhile (busy)
            int c = cid < 0 || cid >= MAXCOMBOS ? Combo : cid, s = cs < 0 ? CSet : cs;
            zcui::end_session();
            const int r = select_combo_2(c, s);
            zcui::begin_session();
            zcui::Lock lock;
            g_s.pick_done = true;
            g_s.pick_ok = r != 0;
            g_s.pick_cid = c;
            g_s.pick_cs = s;
            g_s.busy = false;
            g_s.focus = true;
            continue;
        }

        if(ok && commit())
        {
            accepted = true;
            break;
        }

        if(done || !zcui::available())
            break;
    }

    zcui::end_session();
    zcui::remove_layer(id);
    zcui::Lock lock;
    g_s.open = false;
    g_s.kind = EK_NONE;
    return accepted;
}

// ---- combo pools ----------------------------------------------------------------------

struct PoolEditor
{
    std::vector<combo_pool> pools, orig;
    int sel;
    bool only_used;
    int cursor_cset;        // the cset 'Use CSet' starts with (upstream: CSet when opened)
    int picked;             // the pool OK leaves for the Pool mode

    PoolEditor() : sel(0), only_used(false), cursor_cset(0), picked(0) {}
};

PoolEditor g_pe;
combo_pool g_pool_clip;     // upstream's Copy / Paste
bool g_pool_copied = false;
bool g_pool_new_usecs = false;

const char *const POOL_HELP =
    "When placing a pool, a random combo in the pool is selected to place.\n"
    "'Copy' copies the current data to a clipboard that can be\n"
    "'Paste'd later to any combo pool.\n"
    "'Tidy' merges duplicate entries, and removes combos with weight 0.\n"
    "'Tidy' will automatically be done when clicking 'OK'.\n"
    "When using '+' or '-', holding SHIFT increments by 10, and CTRL by 100.\n"
    "Click a combo to choose another; 'Add Brush Combo' adds the map editor's current one.";

bool pool_same(const combo_pool &a, const combo_pool &b)
{
    if(a.combos.size() != b.combos.size())
        return false;

    for(size_t i = 0; i < a.combos.size(); ++i)
        if(a.combos[i].cid != b.combos[i].cid || a.combos[i].cset != b.combos[i].cset || a.combos[i].quant != b.combos[i].quant)
            return false;

    return true;
}

void pool_apply_pick()
{
    PoolEditor &e = g_pe;

    if(!g_s.pick_done)
        return;

    g_s.pick_done = false;

    if(!g_s.pick_ok || e.sel < 0 || e.sel >= (int)e.pools.size())
        return;

    combo_pool &p = e.pools[e.sel];

    if(g_s.pick_target == PK_POOL_ADD)
        p.add(g_s.pick_cid, g_pool_new_usecs ? (int8_t)g_s.pick_cs : (int8_t)-1, 1);
    else if(g_s.pick_target == PK_POOL_ENTRY && g_s.pick_arg >= 0 && g_s.pick_arg < (int)p.combos.size())
    {
        cpool_entry &en = p.combos[g_s.pick_arg];
        en.cid = g_s.pick_cid;

        if(en.cset > -1)
            en.cset = (int8_t)g_s.pick_cs;
    }

    p.recalc();
}

void pool_layer(void *)
{
    PoolEditor &e = g_pe;

    if(!begin_window("Combo Pools###zqcpools", 900, 640))
        return;

    pool_apply_pick();
    const float sc = zcui::scale();
    const ImGuiIO &io = ImGui::GetIO();

    // the pools, on the left
    ImGui::BeginChild("##list", ImVec2(ImMin(250 * sc, ImGui::GetContentRegionAvail().x * 0.4f), -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);
    ImGui::Checkbox("Only pools in use", &e.only_used);
    std::vector<int> rows;

    for(int i = 0; i < (int)e.pools.size(); ++i)
        if(!e.only_used || !e.pools[i].combos.empty() || i == e.sel)
            rows.push_back(i);

    ImGuiListClipper clip;
    clip.Begin((int)rows.size());

    while(clip.Step())
        for(int r = clip.DisplayStart; r < clip.DisplayEnd; ++r)
        {
            const int i = rows[r];
            const combo_pool &p = e.pools[i];
            ImGui::PushID(i);
            char label[64];

            if(p.combos.empty())
                snprintf(label, sizeof(label), "%4d%s", i, pool_same(p, e.orig[i]) ? "" : " *");
            else
                snprintf(label, sizeof(label), "%4d  %d combo%s, weight %d%s", i, (int)p.combos.size(), p.combos.size() == 1 ? "" : "s",
                         (int)p.getTotalWeight(), pool_same(p, e.orig[i]) ? "" : " *");

            if(ImGui::Selectable(label, i == e.sel))
                e.sel = i;

            if(i == e.sel && ImGui::IsWindowAppearing())
                ImGui::SetScrollHereY();

            ImGui::PopID();
        }

    ImGui::EndChild();
    ImGui::SameLine();

    // the picked pool
    ImGui::BeginChild("##pool", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);

    if(e.sel >= 0 && e.sel < (int)e.pools.size())
    {
        combo_pool &p = e.pools[e.sel];
        ImGui::Text("Combo Pool Editor (%d)", e.sel);
        ImGui::SameLine();
        help_marker(POOL_HELP);
        ImGui::SameLine();
        ImGui::TextDisabled("  total weight %d", (int)p.getTotalWeight());
        ImGui::Separator();

        const float cw = 176 * sc, ch = 150 * sc;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const int per_row = clampi((int)((ImGui::GetContentRegionAvail().x + spacing) / (cw + spacing)), 1, 64);
        int act = 0, act_ind = -1;     // 1 left, 2 erase, 3 right (done after the loop)

        for(size_t ind = 0; ind <= p.combos.size(); ++ind)
        {
            if(ind % per_row)
                ImGui::SameLine();

            ImGui::PushID((int)ind);
            ImGui::BeginChild("##entry", ImVec2(cw, ch), ImGuiChildFlags_Borders);

            if(ind == p.combos.size())
            {
                // upstream's "Add Combo" square, and the map editor's brush combo
                if(ImGui::Button("Add Combo...", ImVec2(-1, 0)))
                    ask_pick(PK_POOL_ADD, 0, Combo, CSet);

                if(ImGui::Button("Add Brush Combo", ImVec2(-1, 0)))
                {
                    p.add(Combo, g_pool_new_usecs ? (int8_t)CSet : (int8_t)-1, 1);
                    p.recalc();
                }

                ImGui::TextDisabled("brush: %d, cset %d", Combo, CSet);
                ImGui::EndChild();
                ImGui::PopID();
                break;
            }

            cpool_entry &en = p.combos[ind];
            ImGui::BeginDisabled(ind == 0);
            if(ImGui::SmallButton("<")) { act = 1; act_ind = (int)ind; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if(ImGui::SmallButton("X")) { act = 2; act_ind = (int)ind; }
            ImGui::SameLine();
            ImGui::BeginDisabled(ind + 1 >= p.combos.size());
            if(ImGui::SmallButton(">")) { act = 3; act_ind = (int)ind; }
            ImGui::EndDisabled();

            const int show_cs = en.cset < 0 ? e.cursor_cset : en.cset;

            if(combo_swatch("##cmb", en.cid, show_cs, 32 * sc))
                ask_pick(PK_POOL_ENTRY, (int)ind, en.cid, show_cs);

            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("Combo %d", (int)en.cid);
            bool usecs = en.cset > -1;

            if(ImGui::Checkbox("Use CSet", &usecs))
                en.cset = usecs ? (int8_t)e.cursor_cset : (int8_t)-1;

            // the csets upstream's combo picker offers, 0-13 (it is the only way upstream sets one)
            if(en.cset > -1)
            {
                int c = en.cset;
                ImGui::SetNextItemWidth(70 * sc);

                if(ImGui::InputInt("##cs", &c))
                    en.cset = (int8_t)clampi(c, 0, 13);
            }

            ImGui::EndGroup();
            const int inc = io.KeyCtrl ? 100 : io.KeyShift ? 10 : 1;

            if(ImGui::SmallButton("-"))
                en.quant = (word)(en.quant > inc ? en.quant - inc : 0);

            ImGui::SameLine();
            int w = en.quant;
            ImGui::SetNextItemWidth(64 * sc);

            if(ImGui::InputInt("##w", &w, 0, 0))
                en.quant = (word)clampi(w, 0, 65535);

            ImGui::SameLine();

            if(ImGui::SmallButton("+"))
                en.quant = (word)(en.quant < 65535 - inc ? en.quant + inc : 65535);

            ImGui::SameLine();
            ImGui::TextDisabled("weight");
            ImGui::EndChild();
            ImGui::PopID();
        }

        if(act == 1) p.swap(act_ind, act_ind - 1);
        else if(act == 2) p.erase(act_ind);
        else if(act == 3) p.swap(act_ind, act_ind + 1);

        p.recalc();
        ImGui::Separator();
        ImGui::Checkbox("New Combos default 'Use CSet'", &g_pool_new_usecs);

        if(ImGui::Button("Tidy", ImVec2(90 * sc, 0)))
            p.trim();

        ImGui::SameLine();

        if(ImGui::Button("Copy", ImVec2(90 * sc, 0)))
        {
            g_pool_clip = p;
            g_pool_copied = true;
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!g_pool_copied);

        if(ImGui::Button("Paste", ImVec2(90 * sc, 0)))
            p = g_pool_clip;

        ImGui::EndDisabled();
        ImGui::SameLine();

        if(ImGui::Button("Clear", ImVec2(90 * sc, 0)))
            p.clear();
    }

    ImGui::EndChild();
    g_s.note = "OK keeps the edits; the Pool drawing mode then draws with the pool picked here.";
    ok_cancel_row();
    ImGui::End();
}

// The game thread: the pools into the store (each changed one tidied, as upstream's OK does).
bool pool_commit()
{
    PoolEditor &e = g_pe;
    std::vector<combo_pool> now;
    {
        zcui::Lock lock;
        now = e.pools;
        e.picked = e.sel;
    }

    bool changed = false;

    for(int i = 0; i < MAXCOMBOPOOLS && i < (int)now.size(); ++i)
    {
        if(pool_same(now[i], e.orig[i]))
            continue;

        now[i].trim();
        combo_pools[i] = now[i];
        changed = true;
    }

    if(changed)
        saved = false;

    return true;
}

// ---- autocombos -----------------------------------------------------------------------

struct AutoEditor
{
    std::vector<combo_auto> autos, orig;
    int sel;
    bool only_used;
    int preview_cs;         // the cset the slots are shown in (upstream: CSet)
    int picked;
    bool ask_type;          // the "Clear Autocombo?" question is up
    int new_type;

    AutoEditor() : sel(0), only_used(false), preview_cs(0), picked(0), ask_type(false), new_type(0) {}
};

AutoEditor g_ae;
combo_auto g_auto_clip;
bool g_auto_copied = false;

bool auto_same(const combo_auto &a, const combo_auto &b)
{
    if(a.getType() != b.getType() || a.getIconDisplay() != b.getIconDisplay() || a.getEraseCombo() != b.getEraseCombo() ||
       a.getFlags() != b.getFlags() || a.getArg() != b.getArg() || a.combos.size() != b.combos.size())
        return false;

    for(size_t i = 0; i < a.combos.size(); ++i)
        if(a.combos[i].cid != b.combos[i].cid || a.combos[i].ctype != b.combos[i].ctype)
            return false;

    return true;
}

int combos_set(const combo_auto &a)
{
    int n = 0;

    for(size_t i = 0; i < a.combos.size(); ++i)
        n += a.combos[i].cid > 0;

    return n;
}

// upstream addCombos / removeCombos
void add_combos(combo_auto &a, int count, bool from0 = false, int engraveoff = -1)
{
    const int start = from0 ? 0 : (int)a.combos.size();

    for(int q = 0; q < count; ++q)
        a.addEntry(0, ACT_NORMAL, start + q, engraveoff == -1 ? -1 : engraveoff + q);
}

void remove_combos(combo_auto &a, int count)
{
    for(int q = 0; q < count; ++q)
        a.removeEntry();
}

// upstream refreshPanels: the type's empty slots
void reset_slots(combo_auto &a, int type, byte arg)
{
    a.clear();
    a.setType(type);
    a.updateValid();

    switch(type)
    {
    case AUTOCOMBO_BASIC: add_combos(a, 16); break;
    case AUTOCOMBO_Z1: add_combos(a, 6); break;
    case AUTOCOMBO_Z4: add_combos(a, 30); break;
    case AUTOCOMBO_FENCE: add_combos(a, 21); break;
    case AUTOCOMBO_RELATIONAL: add_combos(a, 47); break;
    case AUTOCOMBO_DGNCARVE: add_combos(a, 94); break;
    case AUTOCOMBO_DOR: add_combos(a, 76); break;
    case AUTOCOMBO_TILING: add_combos(a, ((arg & 0xF) + 1) * (((arg >> 4) & 0xF) + 1)); break;
    case AUTOCOMBO_DENSEFOREST: add_combos(a, 7); break;
    }
}

// upstream refreshTilingGrid: the pattern laid out at its new size, keeping each slot's combo
// at its column and row
void resize_tiling(combo_auto &a, int w, int h)
{
    const int oldw = (a.getArg() & 0xF) + 1, oldh = ((a.getArg() >> 4) & 0xF) + 1;
    byte tt[8][8];
    int tg[8][8];
    memset(tt, 0, sizeof(tt));
    memset(tg, 0, sizeof(tg));

    for(int xi = 0; xi < oldw; ++xi)
        for(int yi = 0; yi < oldh; ++yi)
        {
            const size_t i = (size_t)(xi + yi * oldw);

            if(i < a.combos.size())
            {
                tt[xi][yi] = a.combos[i].ctype;
                tg[xi][yi] = a.combos[i].cid;
            }
        }

    const byte arg = (byte)(((h - 1) << 4) | (w - 1));
    const byte flags = a.getFlags();
    reset_slots(a, AUTOCOMBO_TILING, arg);

    for(int xi = 0; xi < w; ++xi)
        for(int yi = 0; yi < h; ++yi)
        {
            const size_t i = (size_t)(xi + yi * w);
            a.combos[i].ctype = tt[xi][yi];
            a.combos[i].cid = tg[xi][yi];
        }

    a.setArg(arg);
    a.setFlags(flags);
    a.updateValid();
}

// upstream refreshTypes' help, by type
const char *type_help(int type)
{
    switch(type)
    {
    case AUTOCOMBO_BASIC:
        return "A generic autocombo setup. Will tile along cardinal directions.\n\n"
               "CONTROLS:\nLeft Click: Place combo\nRight Click: Remove combo (uses the Erase Combo)\n"
               "Ctrl + Left Click: Fill combos\nCtrl + Right Click: Fill remove combos";
    case AUTOCOMBO_Z1:
        return "A very simple autocombo setup for making classic mountains.\n"
               "Uses only 6 combos made up of tops, bottoms, and corners.\n\n"
               "CONTROLS:\nLeft Click: Place combo\nRight Click: Remove combo (uses the Erase Combo)\n"
               "Ctrl + Left Click: Fill combos\nCtrl + Right Click: Fill remove combos";
    case AUTOCOMBO_Z4:
        return "An autocombo setup for making mountains that stack vertically.\n"
               "Works identically to 'Fence', but with combos for the bottoms\n"
               "of the mountain filled in as it goes.\n\n"
               "CONTROLS:\nLeft Click: Place combo\nRight Click: Remove combo (uses the Erase Combo)\n"
               "Ctrl + Click: Flip orientation between inward / outward\n"
               "Shift + [: Increase height, changes combos if hovering over them\n"
               "Shift + ]: Decrease height, changes combos if hovering over them";
    case AUTOCOMBO_FENCE:
        return "An autocombo setup for making one tile high walls.\n\n"
               "CONTROLS:\nLeft Click: Place combo\nRight Click: Remove combo (uses the Erase Combo)\n"
               "Ctrl + Click: Flip orientation between inward / outward";
    case AUTOCOMBO_RELATIONAL:
        return "A more complex version of the generic setup. Will tile along\n"
               "cardinal directions, accounting for corners.\n\n"
               "CONTROLS:\nLeft Click: Place combo\nRight Click: Remove combo (uses the Erase Combo)\n"
               "Ctrl + Left Click: Fill combos\nCtrl + Right Click: Fill remove combos";
    case AUTOCOMBO_DGNCARVE:
        return "An autocombo for making two tile high dungeon walls.\n\n"
               "CONTROLS:\nLeft Click: Place combo\nRight Click: Remove combo (uses the Erase Combo)\n"
               "Ctrl + Left Click: Fill combos\nCtrl + Right Click: Fill remove combos";
    case AUTOCOMBO_DOR:
        return "An autocombo for mountains in a 3/4 perspective.\n"
               "WARNING: The tiles in this set are slightly incomplete. Not all formations\n"
               "are possible and some manual correction will be needed for more complex setups.\n\n"
               "CONTROLS:\nLeft Click: Place combo\nRight Click: Remove combo (uses the Erase Combo)\n"
               "Shift + Click: Place / Remove combos without writing sides\n"
               "Ctrl + Left Click: Fill combos\nCtrl + Right Click: Fill remove combos";
    case AUTOCOMBO_TILING:
        return "An autocombo for tiling patterns based on X/Y position.\n\n"
               "CONTROLS:\nLeft Click: Place combo\nRight Click: Remove combo (uses the Erase Combo)\n"
               "Shift + Click: Update the X/Y offset for the top-left corner of the tiling pattern";
    case AUTOCOMBO_REPLACE:
        return "An autocombo for replacing a set of arbitrary tiles with others.\n\n"
               "CONTROLS:\nLeft Click: Replace combo\nRight Click: Replace combo (reverse)";
    case AUTOCOMBO_DENSEFOREST:
        return "An autocombo for placing 2x2 trees that alternate position every other row.\n\n"
               "CONTROLS:\nLeft Click: Place combo\nRight Click: Remove combo\n"
               "Shift + Click: Update the X/Y offset for the top-left corner of the pattern";
    case AUTOCOMBO_EXTEND:
        return "An autocombo for extending things along one axis.\n\n"
               "CONTROLS:\nLeft Click: Place combo\nRight Click: Remove combo";
    }

    return NULL;
}

// upstream's slot layouts (refreshWidgets): 4 to a row, 1 = a gap
const byte *slot_grid(int type, int &n)
{
    static const byte z1[] = { 0, 0, 0, 1, 0, 0, 0 };
    static const byte z4[] =
    {
        0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    static const byte fence[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
    static byte dgn[128];
    static byte dor[84];
    static bool made = false;

    if(!made)
    {
        memset(dgn, 0, sizeof(dgn));
        dgn[23 * 4 + 1] = dgn[23 * 4 + 3] = 1;
        memset(dor, 0, sizeof(dor));

        for(int r = 6; r <= 13; ++r)
            dor[r * 4 + 3] = 1;

        made = true;
    }

    switch(type)
    {
    case AUTOCOMBO_Z1: n = (int)sizeof(z1); return z1;
    case AUTOCOMBO_Z4: n = (int)sizeof(z4); return z4;
    case AUTOCOMBO_FENCE: n = (int)sizeof(fence); return fence;
    case AUTOCOMBO_DGNCARVE: n = (int)sizeof(dgn); return dgn;
    case AUTOCOMBO_DOR: n = (int)sizeof(dor); return dor;
    }

    n = 0;
    return NULL;
}

// upstream's Auto Generate: each slot from the picked combo, as the engravings lay them out
// (altmode, shift held: every empty slot gets the next combo not in the set)
void auto_generate(combo_auto &a, int cmb, bool altmode)
{
    int tiling_offs = 0;
    int filler = altmode ? cmb : 0;

    for(size_t slot = 0; slot < a.combos.size(); ++slot)
    {
        autocombo_entry &en = a.combos[slot];

        if(altmode)
        {
            while(a.containsCombo(filler, false) && filler < MAXCOMBOS - 1)
                ++filler;

            if(en.cid == 0)
                en.cid = filler;

            continue;
        }

        int cid;

        switch(a.getType())
        {
        case AUTOCOMBO_TILING:
        {
            const int wid = (a.getArg() & 0xF) + 1;
            cid = clampi(cmb + (int)slot + tiling_offs, 0, MAXCOMBOS - 1);

            if((int)slot % wid == wid - 1)
                tiling_offs += 4 - wid;

            break;
        }

        case AUTOCOMBO_EXTEND:
            cid = clampi(cmb + (int)slot, 0, MAXCOMBOS - 1);

            if(a.getFlags() & ACF_VERTICAL)
            {
                cid = clampi(cmb + tiling_offs + ((int)slot % 4) * 4, 0, MAXCOMBOS - 1);

                if(slot % 4 == 3)
                {
                    ++tiling_offs;

                    if((cmb + (int)(slot / 4)) % 4 == 3)
                        tiling_offs += 12;
                }
            }

            break;

        case AUTOCOMBO_RELATIONAL:
        case AUTOCOMBO_DGNCARVE:
            if(a.getFlags() & ACF_LEGACY)
            {
                cid = clampi(cmb + combo_auto::legacy_offsets((byte)a.getType(), (int16_t)slot), 0, MAXCOMBOS - 1);
                break;
            }
            // fall through

        default:
            cid = clampi(cmb + combo_auto::convert_offsets((byte)a.getType(), (int16_t)slot), 0, MAXCOMBOS - 1);
            break;
        }

        en.cid = cid;
    }

    a.updateValid();
}

void auto_apply_pick()
{
    AutoEditor &e = g_ae;

    if(!g_s.pick_done)
        return;

    g_s.pick_done = false;

    if(!g_s.pick_ok || e.sel < 0 || e.sel >= (int)e.autos.size())
        return;

    combo_auto &a = e.autos[e.sel];
    e.preview_cs = g_s.pick_cs;

    switch(g_s.pick_target)
    {
    case PK_AUTO_SLOT:
        if(g_s.pick_arg >= 0 && g_s.pick_arg < (int)a.combos.size())
            a.combos[g_s.pick_arg].cid = g_s.pick_cid;
        break;

    case PK_AUTO_ERASE:
        a.setEraseCombo(g_s.pick_cid);
        break;

    case PK_AUTO_DISPLAY:
        a.setDisplay(g_s.pick_cid);
        break;

    case PK_AUTO_GENERATE:
        auto_generate(a, g_s.pick_cid, g_s.pick_shift);
        break;
    }

    a.updateValid();
}

void flag_box(combo_auto &a, byte flag, const char *text, const char *info)
{
    bool on = (a.flags & flag) != 0;

    if(ImGui::Checkbox(text, &on))
        a.flags = on ? (byte)(a.flags | flag) : (byte)(a.flags & ~flag);

    ImGui::SameLine();
    help_marker(info);
}

// One slot: its engraving and its combo (a click picks another).
void slot_widget(combo_auto &a, size_t ind, int engraving, float s, int cs)
{
    ImGui::PushID((int)ind);
    ImGui::BeginGroup();

    if(engraving >= 0)
    {
        engraving_image(engraving, s);
        ImGui::SameLine(0, 2);
    }

    if(combo_swatch("##slot", a.combos[ind].cid, cs, s))
        ask_pick(PK_AUTO_SLOT, (int)ind, a.combos[ind].cid, cs);

    if(ImGui::IsItemHovered())
        ImGui::SetTooltip("Slot %d: combo %d", (int)ind, (int)a.combos[ind].cid);

    ImGui::EndGroup();
    ImGui::PopID();
}

void auto_combos_tab(combo_auto &a, int cs)
{
    const float sc = zcui::scale();
    const float s = 32 * sc;
    const int type = a.getType();
    int per_row = 4;

    if(type == AUTOCOMBO_TILING)
        per_row = (a.getArg() & 0xF) + 1;
    else if(type == AUTOCOMBO_EXTEND)
        per_row = 1;
    else if(type == AUTOCOMBO_REPLACE)
        per_row = 2;

    int gn = 0;
    const byte *grid = slot_grid(type, gn);
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    ImGui::BeginChild("##slots", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.3f));
    size_t ind = 0;
    int cell = 0;

    while(ind < a.combos.size())
    {
        if(cell % per_row)
            ImGui::SameLine(0, 10 * sc);

        if(grid && cell < gn && grid[cell])
        {
            ImGui::Dummy(ImVec2(s * 2 + 2 + pad.x * 2, s + pad.y * 2));
            ++cell;
            continue;
        }

        switch(type)
        {
        case AUTOCOMBO_TILING:
            slot_widget(a, ind, -1, s, cs);
            ++ind;
            break;

        case AUTOCOMBO_REPLACE:
            // a pair: the combo replaced, then what replaces it
            ImGui::BeginGroup();
            slot_widget(a, ind, -1, s, cs);
            ImGui::SameLine(0, 2);
            engraving_image(63, s);

            if(ind + 1 < a.combos.size())
            {
                ImGui::SameLine(0, 2);
                slot_widget(a, ind + 1, -1, s, cs);
            }

            ImGui::EndGroup();
            ind += 2;
            break;

        case AUTOCOMBO_EXTEND:
        {
            const int eng = (a.flags & ACF_VERTICAL) ? 336 : 340;

            for(int q = 0; q < 4 && ind < a.combos.size(); ++q, ++ind)
            {
                if(q)
                    ImGui::SameLine(0, 6 * sc);

                slot_widget(a, ind, eng + q, s, cs);
            }

            break;
        }

        case AUTOCOMBO_DGNCARVE:
            slot_widget(a, ind, autocombo_entry::base_engrave_offset(AUTOCOMBO_DGNCARVE) + (ind < 94 ? (int)ind : (int)ind + 114), s, cs);
            ++ind;
            break;

        default:
            slot_widget(a, ind, a.combos[ind].engrave_offset, s, cs);
            ++ind;
            break;
        }

        ++cell;
    }

    ImGui::EndChild();

    if(type == AUTOCOMBO_REPLACE || type == AUTOCOMBO_EXTEND)
    {
        const int step = type == AUTOCOMBO_REPLACE ? 2 : 4;
        ImGui::BeginDisabled(a.combos.empty());

        if(ImGui::Button("-", ImVec2(32 * sc, 0)))
            remove_combos(a, step);

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(a.combos.size() >= 512);

        if(ImGui::Button("+", ImVec2(32 * sc, 0)))
            add_combos(a, step);

        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%d slots", (int)a.combos.size());
        a.updateValid();
    }
}

void height_field(combo_auto &a)
{
    int h = a.getArg() + 1;
    ImGui::SetNextItemWidth(90 * zcui::scale());

    if(ImGui::InputInt("Height", &h))
        a.setArg((byte)(clampi(h, 1, 9) - 1));

    ImGui::SameLine();
    help_marker("The default height the mountain extends downwards.");
}

void auto_settings_tab(combo_auto &a, int cs)
{
    const float sc = zcui::scale();
    const int type = a.getType();
    ImGui::Text("Display Combo:");
    ImGui::SameLine();
    ImGui::BeginDisabled(type == AUTOCOMBO_NONE);

    if(combo_swatch("##display", a.getIconDisplay(), cs, 32 * sc))
        ask_pick(PK_AUTO_DISPLAY, 0, a.getIconDisplay(), cs);

    ImGui::EndDisabled();
    ImGui::Separator();
    const char *cross = "If checked, this autocombo can affect combos on adjacent screens.";
    const char *edge = "If checked, this autocombo will connect to the edge of the screen.";
    const char *solid = "If checked, this autocombo will connect to fully solid combos as if they're in the set.\n"
                        "This will react to solids on lower layers but ignore higher ones.";
    const char *flip = "Flips the default orientation when placing combos between up/left and down/right.";
    const char *legacy = "Makes 'Auto Generate' use combo ordering from older versions.\n"
                         "For tilesets that have combos set up for Relational and Dungeon Carving modes";

    switch(type)
    {
    case AUTOCOMBO_BASIC:
    case AUTOCOMBO_Z1:
    case AUTOCOMBO_DENSEFOREST:
        flag_box(a, ACF_CROSSSCREENS, "Cross Screens", cross);
        flag_box(a, ACF_CONNECTEDGE, "Connect to Edge", edge);
        flag_box(a, ACF_CONNECTSOLID, "Connect to Solids", solid);
        break;

    case AUTOCOMBO_FENCE:
        flag_box(a, ACF_CROSSSCREENS, "Cross Screens", cross);
        flag_box(a, ACF_FLIP, "Flip", flip);
        break;

    case AUTOCOMBO_Z4:
        flag_box(a, ACF_CROSSSCREENS, "Cross Screens", cross);
        flag_box(a, ACF_FLIP, "Flip", flip);
        height_field(a);
        break;

    case AUTOCOMBO_RELATIONAL:
        flag_box(a, ACF_CROSSSCREENS, "Cross Screens", cross);
        flag_box(a, ACF_CONNECTEDGE, "Connect to Edge", edge);
        flag_box(a, ACF_CONNECTSOLID, "Connect to Solids", solid);
        flag_box(a, ACF_LEGACY, "Legacy Ordering", legacy);
        break;

    case AUTOCOMBO_DGNCARVE:
    {
        flag_box(a, ACF_CROSSSCREENS, "Cross Screens", cross);
        flag_box(a, ACF_CONNECTEDGE, "Connect to Edge", edge);
        flag_box(a, ACF_LEGACY, "Legacy Ordering", legacy);
        bool on = (a.flags & ACF_UNIQUECORNER) != 0;

        if(ImGui::Checkbox("Large Corners", &on))
        {
            // upstream: 32 more slots (engravings from 352) for eight 2x2 corner blocks
            a.flags = on ? (byte)(a.flags | ACF_UNIQUECORNER) : (byte)(a.flags & ~ACF_UNIQUECORNER);

            if(on && a.combos.size() <= 94)
                add_combos(a, 32, false, 352);
            else if(!on)
                remove_combos(a, 32);

            a.updateValid();
        }

        ImGui::SameLine();
        help_marker("Adds extra slots for eight 2x2 blocks that replace the corners");
        break;
    }

    case AUTOCOMBO_DOR:
        flag_box(a, ACF_CROSSSCREENS, "Cross Screens", cross);
        flag_box(a, ACF_CONNECTEDGE, "Connect to Edge", edge);
        height_field(a);
        break;

    case AUTOCOMBO_TILING:
    {
        int w = (a.getArg() & 0xF) + 1, h = ((a.getArg() >> 4) & 0xF) + 1;
        bool resized = false;
        ImGui::SetNextItemWidth(90 * sc);
        resized |= ImGui::InputInt("Width", &w);
        ImGui::SameLine();
        help_marker("The width of the tiling pattern.");
        ImGui::SetNextItemWidth(90 * sc);
        resized |= ImGui::InputInt("Height", &h);
        ImGui::SameLine();
        help_marker("The height of the tiling pattern.");

        if(resized)
        {
            w = clampi(w, 1, 8);
            h = clampi(h, 1, 8);

            if(w != (a.getArg() & 0xF) + 1 || h != ((a.getArg() >> 4) & 0xF) + 1)
                resize_tiling(a, w, h);
        }

        break;
    }

    case AUTOCOMBO_EXTEND:
        flag_box(a, ACF_CROSSSCREENS, "Cross Screens", cross);
        flag_box(a, ACF_CONNECTEDGE, "Connect to Edge", edge);
        flag_box(a, ACF_VERTICAL, "Vertical", "If checked, extends tiles vertically instead of horizontally.");
        flag_box(a, ACF_NOCREATE, "Only Extend", "If checked, prevents creating or destroying the last combo in a set.");
        flag_box(a, ACF_DOGROUP, "Extend Group", "If checked, will also extend any adjacent combos going at a perpendicular.");
        break;

    default:
        ImGui::TextDisabled("This type has no settings.");
        break;
    }
}

// upstream's type list, in its order
const int TYPE_ORDER[] =
{
    AUTOCOMBO_NONE, AUTOCOMBO_BASIC, AUTOCOMBO_RELATIONAL, AUTOCOMBO_Z1, AUTOCOMBO_Z4, AUTOCOMBO_FENCE, AUTOCOMBO_DGNCARVE,
    AUTOCOMBO_DOR, AUTOCOMBO_TILING, AUTOCOMBO_REPLACE, AUTOCOMBO_DENSEFOREST, AUTOCOMBO_EXTEND
};

// upstream's type change: the new type's empty slots, flags and argument cleared
void change_type(combo_auto &a, int t)
{
    reset_slots(a, t, 0);
    a.setFlags(0);
    a.setArg(0);
    a.updateValid();
}

void auto_layer(void *)
{
    AutoEditor &e = g_ae;

    if(!begin_window("Autocombos###zqautocombos", 920, 680))
        return;

    auto_apply_pick();
    const float sc = zcui::scale();

    ImGui::BeginChild("##list", ImVec2(ImMin(250 * sc, ImGui::GetContentRegionAvail().x * 0.4f), -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);
    ImGui::Checkbox("Only autocombos in use", &e.only_used);
    std::vector<int> rows;

    for(int i = 0; i < (int)e.autos.size(); ++i)
        if(!e.only_used || e.autos[i].getType() != AUTOCOMBO_NONE || !e.autos[i].combos.empty() || i == e.sel)
            rows.push_back(i);

    ImGuiListClipper clip;
    clip.Begin((int)rows.size());

    while(clip.Step())
        for(int r = clip.DisplayStart; r < clip.DisplayEnd; ++r)
        {
            const int i = rows[r];
            const combo_auto &a = e.autos[i];
            ImGui::PushID(i);
            char label[80];

            if(a.getType() == AUTOCOMBO_NONE && a.combos.empty())
                snprintf(label, sizeof(label), "%4d%s", i, auto_same(a, e.orig[i]) ? "" : " *");
            else
                snprintf(label, sizeof(label), "%4d  %s%s%s", i, zq_autocombo_type_name(a.getType()), a.valid() ? "" : " (not valid)",
                         auto_same(a, e.orig[i]) ? "" : " *");

            if(ImGui::Selectable(label, i == e.sel) && i != e.sel)
            {
                e.sel = i;
                e.ask_type = false;
            }

            if(i == e.sel && ImGui::IsWindowAppearing())
                ImGui::SetScrollHereY();

            ImGui::PopID();
        }

    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##auto", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);

    if(e.sel >= 0 && e.sel < (int)e.autos.size())
    {
        combo_auto &a = e.autos[e.sel];
        const int type = a.getType();
        ImGui::Text("Auto Combo Editor (%d)", e.sel);
        ImGui::SameLine();
        help_marker("Fill in all the boxes with combos as indicated by the engravings to their left.\n"
                    "All boxes need to be filled with combos with no duplicates in order\n"
                    "for the autocombo to function.\n\n"
                    "Click 'Auto Generate' to auto generate based on the combo layout of the engravings.\n"
                    "Shift click 'Auto Generate' to fill all remaining empty slots with unique combos.\n"
                    "Click a slot to choose its combo.");

        // the type, with upstream's question when combos would be cleared
        ImGui::SetNextItemWidth(190 * sc);

        if(ImGui::BeginCombo("Type", zq_autocombo_type_name(type)))
        {
            for(size_t k = 0; k < sizeof(TYPE_ORDER) / sizeof(TYPE_ORDER[0]); ++k)
                if(ImGui::Selectable(zq_autocombo_type_name(TYPE_ORDER[k]), TYPE_ORDER[k] == type) && TYPE_ORDER[k] != type)
                {
                    if(combos_set(a))
                    {
                        e.ask_type = true;
                        e.new_type = TYPE_ORDER[k];
                    }
                    else
                        change_type(a, TYPE_ORDER[k]);
                }

            ImGui::EndCombo();
        }

        if(type_help(type))
        {
            ImGui::SameLine();
            help_marker(type_help(type));
        }

        ImGui::SameLine(0, 24 * sc);
        ImGui::Text("Erase Combo:");
        ImGui::SameLine();
        ImGui::BeginDisabled(!a.canErase());

        if(combo_swatch("##erase", a.getEraseCombo(), e.preview_cs, 24 * sc))
            ask_pick(PK_AUTO_ERASE, 0, a.getEraseCombo(), e.preview_cs);

        ImGui::EndDisabled();
        ImGui::SameLine(0, 24 * sc);
        ImGui::BeginDisabled(!a.hasTemplate() || a.combos.empty());

        if(ImGui::Button("Auto Generate..."))
            ask_pick(PK_AUTO_GENERATE, 0, Combo, e.preview_cs, ImGui::GetIO().KeyShift);

        ImGui::EndDisabled();

        if(e.ask_type)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Changing this autocombo's type will clear all its combos. Are you sure?");
            ImGui::SameLine();

            if(ImGui::SmallButton("Yes"))
            {
                change_type(a, e.new_type);
                e.ask_type = false;
            }

            ImGui::SameLine();

            if(ImGui::SmallButton("No"))
                e.ask_type = false;
        }

        // whether it's valid as it stands, checked on a copy: just looking at an autocombo whose
        // stored valid flag is stale mustn't change it (only edits and OK store the flag)
        combo_auto chk = a;
        chk.updateValid();

        if(type == AUTOCOMBO_NONE)
            ImGui::TextDisabled("Pick a type.");
        else if(chk.valid())
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Valid");
        else
        {
            std::string why = chk.getInvalidReason();

            for(size_t k = 0; k < why.size(); ++k)
                if(why[k] == '\n')
                    why[k] = ' ';

            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "Not valid:%s", why.c_str());
        }

        if(ImGui::BeginTabBar("##tabs"))
        {
            if(ImGui::BeginTabItem("Combos"))
            {
                auto_combos_tab(a, e.preview_cs);
                ImGui::EndTabItem();
            }

            if(ImGui::BeginTabItem("Settings"))
            {
                auto_settings_tab(a, e.preview_cs);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        if(ImGui::Button("Copy", ImVec2(90 * sc, 0)))
        {
            g_auto_clip = a;
            g_auto_copied = true;
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!g_auto_copied);

        if(ImGui::Button("Paste", ImVec2(90 * sc, 0)))
        {
            a = g_auto_clip;
            a.updateValid();
        }

        ImGui::EndDisabled();
        ImGui::SameLine();

        if(ImGui::Button("Clear", ImVec2(90 * sc, 0)))
            a.clear(true);
    }

    ImGui::EndChild();
    g_s.note = "OK keeps the edits; the Autocombo drawing mode then draws with the one picked here.";
    ok_cancel_row();
    ImGui::End();
}

bool auto_commit()
{
    AutoEditor &e = g_ae;
    std::vector<combo_auto> now;
    {
        zcui::Lock lock;
        now = e.autos;
        e.picked = e.sel;
    }

    bool changed = false;

    for(int i = 0; i < MAXAUTOCOMBOS && i < (int)now.size(); ++i)
    {
        if(auto_same(now[i], e.orig[i]))
            continue;

        now[i].updateValid();       // as upstream's OK
        combo_autos[i] = now[i];
        combo_autos[i].updateValid();
        changed = true;
    }

    if(changed)
        saved = false;

    return true;
}

// ---- scrolling regions ----------------------------------------------------------------

struct RegionEditor
{
    int map;                                        // 0-based, the one shown
    int maps;                                       // map_count
    int cur_map, cur_screen;                        // the map editor's
    std::vector<std::vector<byte> > ids, orig;      // per map, one id per screen (128); orig: as stored
    std::vector<byte> valid;                        // per map, per screen: in use
    std::vector<byte> any_valid;                    // per map: has a screen in use
    std::vector<byte> shown;                        // per map: shown in the window (only those are written)
    int id;                                         // the id painted

    RegionEditor() : map(0), maps(0), cur_map(0), cur_screen(0), id(1) {}
};

RegionEditor g_re;

// 2.53's jwin colours 0-9 (vc), as upstream's region squares
ImU32 region_colour(int id)
{
    static const ImU32 c[10] =
    {
        IM_COL32(0, 0, 0, 255), IM_COL32(0, 0, 170, 255), IM_COL32(0, 170, 0, 255), IM_COL32(0, 170, 170, 255),
        IM_COL32(170, 0, 0, 255), IM_COL32(170, 0, 170, 255), IM_COL32(170, 85, 0, 255), IM_COL32(170, 170, 170, 255),
        IM_COL32(85, 85, 85, 255), IM_COL32(85, 85, 255, 255)
    };
    return c[id < 0 || id > 9 ? 0 : id];
}

ImU32 region_text_colour(int id)
{
    switch(id)
    {
    case 2: case 3: case 5: case 7: case 9:
        return IM_COL32(0, 0, 0, 255);
    }

    return IM_COL32(255, 255, 255, 255);
}

void pack_ids(const std::vector<byte> &ids, byte packed[64])
{
    memset(packed, 0, 64);

    for(int s = 0; s < 128; ++s)
    {
        byte &d = packed[(s / 16) * 8 + (s % 16) / 2];
        d = (s % 2 == 0) ? (byte)((d & 0x0F) | ((ids[s] & 15) << 4)) : (byte)((d & 0xF0) | (ids[s] & 0x0F));
    }
}

void region_layer(void *)
{
    RegionEditor &e = g_re;

    if(!begin_window("Scrolling Regions###zqregions", 640, 620))
        return;

    const float sc = zcui::scale();
    ImGui::Text("Edit Map Settings (%d): Regions", e.map + 1);
    ImGui::SameLine();
    help_marker("Screens with the same Region ID define a single Region\n"
                "  of screens connected via free-scrolling\n\n"
                "Regions must be rectangular\n\n"
                "For more on Regions, see the Z3 User Guide\n"
                "(docs.zquestclassic.com/tutorials/z3_user_guide)");

    int m = e.map + 1;
    ImGui::SetNextItemWidth(110 * sc);

    if(ImGui::InputInt("Map", &m))
        e.map = clampi(m, 1, e.maps) - 1;

    std::vector<byte> &ids = e.ids[e.map];

    // A map shown for the first time starts as upstream's grid does on the map it edits (MSG_START):
    // its screens not in use belong to no region. Maps never shown keep their ids untouched.
    if(!e.shown[e.map])
    {
        e.shown[e.map] = 1;

        for(int s = 0; s < 128; ++s)
            if(!e.valid[(size_t)e.map * 128 + s])
                ids[s] = 0;
    }

    // the rest scrolls, above the OK and Cancel buttons
    ImGui::BeginChild("##body", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f));

    if(!e.any_valid[e.map])
    {
        ImGui::TextWrapped("There must be at least one valid screen in a map to configure regions.");
    }
    else
    {
        // the 8 x 16 grid: click or drag to paint the picked id on screens in use
        // squares about upstream's size (27 x 25), smaller when the window is narrow
        const float hx = 18 * sc, hy = 16 * sc;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float cw = ImMax(10.0f, ImMin(30 * sc, ImMin((avail.x - hx) / 16, (avail.y - hy - 4) / 8 / 0.9f))), chh = cw * 0.9f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImGui::InvisibleButton("##grid", ImVec2(hx + 16 * cw, hy + 8 * chh));
        const bool painting = ImGui::IsItemActive();

        for(int x = 0; x < 16; ++x)
        {
            char t[4];
            snprintf(t, sizeof(t), "%X", x);
            dl->AddText(ImVec2(origin.x + hx + x * cw + cw * 0.4f, origin.y), ImGui::GetColorU32(ImGuiCol_Text), t);
        }

        for(int y = 0; y < 8; ++y)
        {
            char t[4];
            snprintf(t, sizeof(t), "%d", y);
            dl->AddText(ImVec2(origin.x + 2, origin.y + hy + y * chh + chh * 0.3f), ImGui::GetColorU32(ImGuiCol_Text), t);
        }

        if(painting)
        {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            const float fx = (mp.x - origin.x - hx) / cw, fy = (mp.y - origin.y - hy) / chh;
            const int x = (int)fx, y = (int)fy;

            if(fx >= 0 && fy >= 0 && x < 16 && y < 8 && e.valid[(size_t)e.map * 128 + y * 16 + x] && ids[y * 16 + x] != e.id)
            {
                ids[y * 16 + x] = (byte)e.id;
                g_s.error.clear();
            }
        }

        for(int y = 0; y < 8; ++y)
            for(int x = 0; x < 16; ++x)
            {
                const int scr = y * 16 + x;
                const ImVec2 a(origin.x + hx + x * cw + 1, origin.y + hy + y * chh + 1), b(a.x + cw - 2, a.y + chh - 2);

                if(!e.valid[(size_t)e.map * 128 + scr])
                {
                    dl->AddRect(a, b, IM_COL32(60, 60, 60, 255));
                    continue;
                }

                dl->AddRectFilled(a, b, region_colour(ids[scr]));
                const bool here = e.map == e.cur_map && scr == e.cur_screen;
                dl->AddRect(a, b, here ? IM_COL32(0, 255, 0, 255) : IM_COL32(120, 120, 120, 255), 0, 0, here ? 2.5f : 1.0f);
                char t[4];
                snprintf(t, sizeof(t), "%d", ids[scr]);
                const ImVec2 ts = ImGui::CalcTextSize(t);
                dl->AddText(ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), region_text_colour(ids[scr]), t);
            }

        // the id painted (upstream: the checkboxes, or the number keys)
        ImGui::Text("Select Region ID:");

        for(int k = 0; k <= 9; ++k)
        {
            char t[24];
            snprintf(t, sizeof(t), k ? "%d" : "0 (no scrolling)", k);

            if(k > 1)
                ImGui::SameLine();

            ImGui::RadioButton(t, &e.id, k);
        }

        if(!ImGui::IsAnyItemActive() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            for(int k = 0; k <= 9; ++k)
                if(ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_0 + k), false) || ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_Keypad0 + k), false))
                    e.id = k;

        // the regions as they stand
        ImGui::Separator();
        std::vector<ZqRegionDesc> descs;
        const bool ok = zq_regions_valid(&ids[0], &descs);

        if(!ok)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Scrolling regions must be rectangular (and not touch another of the same id).");
        else if(descs.empty())
            ImGui::TextDisabled("No scrolling regions on this map.");

        for(size_t k = 0; ok && k < descs.size(); ++k)
        {
            const ZqRegionDesc &d = descs[k];
            const int last = d.screen + (d.h - 1) * 16 + d.w - 1;
            ImGui::Text("Region id %d: screens %02X to %02X (%d x %d screens)", d.id, d.screen, last, d.w, d.h);
        }
    }

    ImGui::EndChild();
    ok_cancel_row();
    ImGui::End();
}

bool region_commit()
{
    RegionEditor &e = g_re;
    std::vector<std::vector<byte> > now;
    std::vector<byte> shown;
    {
        zcui::Lock lock;
        now = e.ids;
        shown = e.shown;
    }

    // Only maps shown in the window are written (upstream's dialog edits one map, the one it
    // shows); one whose ids changed (painted, or its unused screens cleared) must check out, as
    // upstream's OK checks the map it edits.
    for(size_t m = 0; m < now.size(); ++m)
    {
        if(!shown[m] || now[m] == e.orig[m] || !e.any_valid[m])
            continue;

        if(!zq_regions_valid(&now[m][0], NULL))
        {
            zcui::Lock lock;
            char b[160];
            snprintf(b, sizeof(b), "Bad regions on map %d: scrolling regions must be rectangular.", (int)m + 1);
            g_s.error = b;
            e.map = (int)m;
            return false;
        }
    }

    bool changed = false;

    for(size_t m = 0; m < now.size(); ++m)
    {
        byte packed[64], stored[64];
        pack_ids(now[m], packed);
        zq_region_ids((int)m, stored);

        if(!shown[m] || !e.any_valid[m] || memcmp(packed, stored, 64) == 0)
            continue;

        zq_region_set_ids((int)m, packed);
        changed = true;
    }

    if(changed)
        saved = false;

    return true;
}

} // namespace

// Quest > Graphics > Combo Pools (zquest.cpp; the caller checks the quest's format holds them).
// Opens at pool 'index'; true when OK was pressed, with *picked the pool picked last.
bool zq_edit_cpools(int index, int *picked)
{
    if(!zcui::available())
        return false;

    {
        zcui::Lock lock;

        if(g_s.open)
            return false;

        PoolEditor &e = g_pe;
        e.pools.assign(combo_pools, combo_pools + MAXCOMBOPOOLS);
        e.orig = e.pools;
        e.sel = clampi(index, 0, MAXCOMBOPOOLS - 1);
        e.cursor_cset = CSet;
        e.picked = e.sel;
    }

    const bool ok = run_session(EK_POOLS, pool_layer, pool_commit);

    if(ok && picked)
        *picked = g_pe.picked;

    {
        zcui::Lock lock;
        g_pe.pools.clear();
        g_pe.orig.clear();
    }

    return ok;
}

// Quest > Graphics > Autocombos, likewise.
bool zq_edit_autocombos(int index, int *picked)
{
    if(!zcui::available())
        return false;

    {
        zcui::Lock lock;

        if(g_s.open)
            return false;

        AutoEditor &e = g_ae;
        e.autos.assign(combo_autos, combo_autos + MAXAUTOCOMBOS);
        e.orig = e.autos;
        e.sel = clampi(index, 0, MAXAUTOCOMBOS - 1);
        e.preview_cs = CSet;
        e.picked = e.sel;
        e.ask_type = false;
    }

    const bool ok = run_session(EK_AUTOS, auto_layer, auto_commit);

    if(ok && picked)
        *picked = g_ae.picked;

    {
        zcui::Lock lock;
        g_ae.autos.clear();
        g_ae.orig.clear();
    }

    return ok;
}

// Edit > Maps > Scrolling Regions (the caller checks the quest's map format holds them):
// starts at map 'map' (0-based). True when OK was pressed.
bool zq_edit_regions(int map)
{
    if(!zcui::available() || map_count < 1)
        return false;

    {
        zcui::Lock lock;

        if(g_s.open)
            return false;

        RegionEditor &e = g_re;
        e.maps = map_count;
        e.map = clampi(map, 0, map_count - 1);
        e.cur_map = Map.getCurrMap();
        e.cur_screen = Map.getCurrScr();
        e.id = clampi(e.id, 0, 9);
        e.ids.assign(map_count, std::vector<byte>(128, 0));
        e.valid.assign((size_t)map_count * 128, 0);
        e.any_valid.assign(map_count, 0);
        e.shown.assign(map_count, 0);

        for(int m = 0; m < map_count; ++m)
        {
            byte packed[64];
            zq_region_ids(m, packed);

            for(int s = 0; s < 128; ++s)
            {
                const byte d = packed[(s / 16) * 8 + (s % 16) / 2];
                mapscr *scr = Map.AbsoluteScr(m, s);
                const bool v = scr && (scr->valid & mVALID);
                e.valid[(size_t)m * 128 + s] = v;
                e.any_valid[m] |= (byte)v;
                e.ids[m][s] = (s % 2 == 0) ? (byte)(d >> 4) : (byte)(d & 15);
            }
        }

        // as stored; region_layer clears a map's unused screens when it first shows it
        e.orig = e.ids;
    }

    return run_session(EK_REGIONS, region_layer, region_commit);
}

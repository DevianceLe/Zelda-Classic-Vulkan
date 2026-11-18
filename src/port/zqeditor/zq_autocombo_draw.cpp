// The map editor's Pool and Autocombo drawing modes for 2.55.17 and 3.0 Nightly quests
// (zquest.cpp draws through these): placing a random weighted pick of a combo pool, and running
// an autocombo's pattern where the mouse is, as upstream's map editor does (src/zq/zquest.cpp
// draw_autocombo, draw_autocombo_command, fill, flood). 2.53's map editor shows one screen,
// so the fills cover the screen being edited; a pattern with "Cross Screens" set still reaches
// the screens around it.
//
// Writes go straight into the screens; draw() and the fills take 2.53's undo copy first.
// Written for the port (no jwin or Allegro code; key[] is the port's input state).

#include "precompiled.h" //always first

#include <allegro.h>

#include <algorithm>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_class.h"
#include "zq_autocombo.h"
#include "autocombo/pattern_basic.h"
#include "autocombo/pattern_flatmtn.h"
#include "autocombo/pattern_fence.h"
#include "autocombo/pattern_cakemtn.h"
#include "autocombo/pattern_relational.h"
#include "autocombo/pattern_dungeoncarve.h"
#include "autocombo/pattern_dormtn.h"
#include "autocombo/pattern_tiling.h"
#include "autocombo/pattern_replace.h"
#include "autocombo/pattern_denseforest.h"
#include "autocombo/pattern_extend.h"

extern int CSet, Combo, CurrentLayer;
extern int combo_auto_pos;      // the autocombo the Autocombo mode draws with (zquest.cpp)
extern newcombo *combobuf;
extern bool saved;

bool zq_autocombo_lshift()
{
    return key[KEY_LSHIFT] != 0;
}

namespace
{

bool shift_held() { return key[KEY_LSHIFT] || key[KEY_RSHIFT]; }
bool ctrl_held() { return key[KEY_ZC_LCONTROL] || key[KEY_ZC_RCONTROL]; }

// Runs a pattern of type P at the position: places, or with rclick erases.
template<class P>
void run(P &ap, int screen, int pos, bool rclick)
{
    if(rclick)
        ap.erase(screen, pos);
    else
        ap.execute(screen, pos);
}

} // namespace

// The height Pancake and Complex Mountains are drawn with (upstream's cauto_height): the
// autocombo's own default when it is picked.
int zq_autocombo_height = 1;

// Upstream draw_autocombo: one autocombo at a screen's combo position on the current map and
// layer. False when the autocombo isn't valid (why says so, as upstream's notice does).
bool zq_autocombo_draw(int index, int screen, int pos, bool rclick, bool pressframe, std::string &why)
{
    if(index < 0 || index >= MAXAUTOCOMBOS)
        return false;

    combo_auto &ca = combo_autos[index];

    if(!ca.valid())
    {
        ca.updateValid();

        if(!ca.valid())
            why = "The autocombo you're trying to use is invalid. Reason:" + ca.getInvalidReason();

        return false;
    }

    const int type = ca.getType();
    const int layer = CurrentLayer;
    saved = false;

    switch(type)
    {
    case AUTOCOMBO_BASIC: { AutoPattern::autopattern_basic ap(type, layer, screen, pos, &ca); run(ap, screen, pos, rclick); break; }
    case AUTOCOMBO_Z1: { AutoPattern::autopattern_flatmtn ap(type, layer, screen, pos, &ca); run(ap, screen, pos, rclick); break; }
    case AUTOCOMBO_FENCE: { AutoPattern::autopattern_fence ap(type, layer, screen, pos, &ca); run(ap, screen, pos, rclick); break; }
    case AUTOCOMBO_Z4: { AutoPattern::autopattern_cakemtn ap(type, layer, screen, pos, &ca, (byte)zq_autocombo_height); run(ap, screen, pos, rclick); break; }
    case AUTOCOMBO_RELATIONAL: { AutoPattern::autopattern_relational ap(type, layer, screen, pos, &ca); run(ap, screen, pos, rclick); break; }
    case AUTOCOMBO_DGNCARVE: { AutoPattern::autopattern_dungeoncarve ap(type, layer, screen, pos, &ca); run(ap, screen, pos, rclick); break; }
    case AUTOCOMBO_DOR: { AutoPattern::autopattern_dormtn ap(type, layer, screen, pos, &ca, (byte)zq_autocombo_height); run(ap, screen, pos, rclick); break; }

    case AUTOCOMBO_TILING:
    {
        // Shift on the first press: the pattern's top-left corner starts here
        if(pressframe && shift_held())
        {
            const int x = (screen % 16) * 16 + (pos % 16);
            const int y = (screen / 16) * 11 + (pos / 16);
            const int w = (ca.getArg() & 0xF) + 1;
            const int h = ((ca.getArg() >> 4) & 0xF) + 1;
            ca.setOffsets((byte)(x % w), (byte)(y % h));
        }

        AutoPattern::autopattern_tiling ap(type, layer, screen, pos, &ca);
        run(ap, screen, pos, rclick);
        break;
    }

    case AUTOCOMBO_REPLACE: { AutoPattern::autopattern_replace ap(type, layer, screen, pos, &ca); run(ap, screen, pos, rclick); break; }

    case AUTOCOMBO_DENSEFOREST:
    {
        if(pressframe && shift_held())
        {
            const int x = (screen % 16) * 16 + (pos % 16);
            const int y = (screen / 16) * 11 + (pos / 16);
            ca.setOffsets((byte)(x % 2), (byte)(y % 2));
        }

        AutoPattern::autopattern_denseforest ap(type, layer, screen, pos, &ca);
        run(ap, screen, pos, rclick);
        break;
    }

    case AUTOCOMBO_EXTEND:
    {
        if(ctrl_held())
            break;

        AutoPattern::autopattern_extend ap(type, layer, screen, pos, &ca);
        run(ap, screen, pos, rclick);
        break;
    }
    }

    return true;
}

// Upstream draw_autocombo_command: Fence and Pancake Mountain flip everything connected to the
// position between facing in and out (Ctrl+click), or (cmd 1) a Pancake Mountain changes height.
void zq_autocombo_command(int index, int screen, int pos, int cmd, int arg)
{
    if(index < 0 || index >= MAXAUTOCOMBOS)
        return;

    combo_auto ca = combo_autos[index];

    if(!ca.valid())
        return;

    saved = false;

    if(ca.getType() == AUTOCOMBO_FENCE)
    {
        AutoPattern::autopattern_fence ap(ca.getType(), CurrentLayer, screen, pos, &ca);
        ap.flip_all_connected(screen, pos, 2048);
    }
    else if(ca.getType() == AUTOCOMBO_Z4)
    {
        AutoPattern::autopattern_cakemtn ap(ca.getType(), CurrentLayer, screen, pos, &ca, (byte)zq_autocombo_height);

        if(cmd == 0)
            ap.flip_all_connected(screen, pos, 2048);
        else
            ap.resize_connected(screen, pos, 2048, arg < 1 ? 1 : arg > 9 ? 9 : arg);
    }
}

// The fills of the Pool and Autocombo modes (upstream fill, fill2 and flood) on the screen being
// edited, from (sx, sy). kind: 0 flood (every combo), 1 fill (the area of the clicked combo), 2
// fill2 (the area up to the brush's combo and cset). mode: 0 pool, 1 autocombo. rclick: the autocombo
// fills erase. False when nothing could be done (why may say why).
bool zq_cpool_fill(int mode, int index, int drawmap, int drawscr, int sx, int sy, int kind, bool diagonal,
                   bool only_cset, bool rclick, std::string &why)
{
    mapscr *scr = Map.AbsoluteScr(drawmap, drawscr);

    if(!scr || sx < 0 || sy < 0 || sx > 15 || sy > 10)
        return false;

    if(mode == 0 && (index < 0 || index >= MAXCOMBOPOOLS || !combo_pools[index].valid()))
        return false;

    if(mode == 1 && (index < 0 || index >= MAXAUTOCOMBOS))
        return false;

    const int screen = Map.getCurrScr();
    const int start = sy * 16 + sx;
    const int targetcombo = scr->data[start], targetcset = scr->cset[start];
    std::vector<int> positions;

    if(kind == 0)
    {
        for(int i = 0; i < 176; ++i)
            positions.push_back(i);
    }
    else
    {
        const combo_auto &cauto = combo_autos[mode == 1 ? index : 0];

        // Whether the fill goes over a position. fill: the clicked combo and cset (or just the
        // cset); fill2: anything but the brush's combo and cset (2.53's fill2 stops at either).
        // Autocombos: upstream's rules (combos of the set, the fill-ignored ones).
        struct Check
        {
            static bool ok(const mapscr *scr, int q, int mode, int kind, bool only_cset, bool rclick, int targetcombo,
                           int targetcset, const combo_auto &cauto)
            {
                const int cid = scr->data[q], cset = scr->cset[q];

                if(mode == 1)
                {
                    const bool ignored = cauto.isIgnoredCombo(cid);
                    bool ok;

                    if(rclick)
                        ok = cauto.containsCombo(targetcombo) && cauto.containsCombo(cid) &&
                             !(cauto.getType() == AUTOCOMBO_REPLACE && ignored);
                    else
                        ok = (cid == targetcombo || ignored) && !(cauto.getType() == AUTOCOMBO_REPLACE && !ignored);

                    return ok && (cset == targetcset || ignored);
                }

                if(kind == 1)
                    return (only_cset || cid == targetcombo) && cset == targetcset;

                return (only_cset || cid != Combo) && cset != CSet;
            }
        };

        if(mode == 0 && kind == 2 && !Check::ok(scr, start, mode, kind, only_cset, rclick, targetcombo, targetcset, cauto))
            return false;

        // upstream flood_filler: every position reachable from the start that passes the check
        bool seen[176] = { false };
        std::vector<int> queue(1, start);
        seen[start] = true;

        while(!queue.empty())
        {
            const int p = queue.back();
            queue.pop_back();
            positions.push_back(p);
            const int px = p % 16, py = p / 16;

            for(int d = 0; d < (diagonal ? 8 : 4); ++d)
            {
                static const int dx[8] = { 0, 0, 1, -1, 1, 1, -1, -1 };
                static const int dy[8] = { 1, -1, 0, 0, 1, -1, 1, -1 };
                const int nx = px + dx[d], ny = py + dy[d];

                if(nx < 0 || ny < 0 || nx > 15 || ny > 10)
                    continue;

                const int q = ny * 16 + nx;

                if(!seen[q] && Check::ok(scr, q, mode, kind, only_cset, rclick, targetcombo, targetcset, cauto))
                {
                    seen[q] = true;
                    queue.push_back(q);
                }
            }
        }
    }

    saved = false;

    for(size_t i = 0; i < positions.size(); ++i)
    {
        const int p = positions[i];

        if(mode == 0)
        {
            int cid = Combo, cs = CSet;

            if(!zq_cpool_pick(index, cid, cs))
                continue;

            if(!only_cset)
                scr->data[p] = (word)cid;

            scr->cset[p] = (byte)cs;
        }
        else
        {
            const combo_auto &cauto = combo_autos[index];

            if(kind != 0)
            {
                if(!cauto.valid())
                    continue;
                if(!rclick && cauto.containsCombo(targetcombo) && !cauto.isIgnoredCombo(targetcombo))
                    continue;
                if(rclick && cauto.getEraseCombo() == targetcombo)
                    continue;
            }

            if(!zq_autocombo_draw(index, screen, p, rclick, false, why))
                return false;
        }
    }

    return true;
}

// ---- what the map editor shows of pools and autocombos (zquest.cpp) ---------------------

// upstream's autocombo type names (zc_list_data.cpp autocombo_types), by type
const char *zq_autocombo_type_name(int type)
{
    static const char *const names[] =
    {
        "(None)", "Flat Mountain", "Basic Relational", "Fence", "Pancake Mountain", "Relational",
        "Dungeon Carving", "Complex Mountain", "Positional Tiling", "Replace", "Dense Forest", "Extension"
    };

    return type >= 0 && type < (int)(sizeof(names) / sizeof(names[0])) ? names[type] : "(Unknown)";
}

int zq_autocombo_type(int index)
{
    return index >= 0 && index < MAXAUTOCOMBOS ? combo_autos[index].getType() : AUTOCOMBO_NONE;
}

// The height Pancake and Complex Mountains start with when the autocombo is picked (upstream:
// cauto_height = arg + 1).
int zq_autocombo_default_height(int index)
{
    if(index < 0 || index >= MAXAUTOCOMBOS)
        return 1;

    const int h = combo_autos[index].getArg() + 1;
    return h < 1 ? 1 : h > 9 ? 9 : h;
}

// A cell of the alias list in the Pool (mode 0) and Autocombo (mode 1) modes: a pool's first
// combo by weight (with its cset, or the current one), an autocombo's display combo; cid -1
// when there's nothing to show (no combo, or one without a tile). mark: -1 an autocombo that
// isn't valid but has a display combo, 1-9 a Pancake or Complex Mountain's height (upstream's
// put_autocombo_engravings).
bool zq_cpool_list_cell(int mode, int index, int &cid, int &cs, int &mark)
{
    cid = -1;
    mark = 0;

    if(mode == 0)
    {
        if(index < 0 || index >= MAXCOMBOPOOLS)
            return false;

        int32_t c = -1;
        int8_t s = (int8_t)cs;

        if(!combo_pools[index].get_w(c, s, 0) || c < 0 || c >= MAXCOMBOS || !combobuf[c].tile)
            return false;

        cid = c;
        cs = s;
        return true;
    }

    if(index < 0 || index >= MAXAUTOCOMBOS)
        return false;

    const combo_auto &ca = combo_autos[index];
    const int d = ca.getDisplay();

    if(!ca.valid())
    {
        if(d > 0)
            mark = -1;
    }
    else if(ca.getType() == AUTOCOMBO_Z4 || ca.getType() == AUTOCOMBO_DOR)
        mark = index == combo_auto_pos ? zq_autocombo_height : zq_autocombo_default_height(index);

    if(d <= 0 || d >= MAXCOMBOS)
        return false;

    cid = d;
    return true;
}

// The pool's entry i for the brush and the alias preview (in the pool's order; cset -1 entries
// get the current one). 0 past the last.
int zq_cpool_brush_entry(int pool, int i, int &cid, int &cs)
{
    if(pool < 0 || pool >= MAXCOMBOPOOLS)
        return 0;

    int32_t c = -1;
    int8_t s = (int8_t)cs;

    if(!combo_pools[pool].get_ind(c, s, (size_t)i) || c < 0 || c >= MAXCOMBOS)
        return 0;

    cid = c;
    cs = s;
    return 1;
}

// Text about a pool or autocombo. mode 0 / 1: the three lines beside the combo preview
// ("Pool: 3", "CSet: 2", "Weight: 10" / "Auto: 2", "CSet: 2", type); mode 2 / 3: one line, the
// alias list's tooltip ("Combo pool 3: 4 combos" / "Autocombo 2: Fence"). n: each buffer's size.
void zq_cpool_describe(int mode, int index, char *l1, char *l2, char *l3, int n)
{
    l1[0] = l2[0] = l3[0] = 0;
    const bool autos = mode == 1 || mode == 3;

    if(index < 0 || index >= (autos ? MAXAUTOCOMBOS : MAXCOMBOPOOLS))
        return;

    if(!autos)
    {
        const combo_pool &p = combo_pools[index];

        if(mode == 2)
        {
            snprintf(l1, n, "Combo pool %d: %d combo%s, weight %d", index, (int)p.combos.size(), p.combos.size() == 1 ? "" : "s",
                     (int)p.getTotalWeight());
            return;
        }

        snprintf(l1, n, "Pool: %d", index);
        snprintf(l2, n, "CSet: %d", CSet);
        snprintf(l3, n, "Weight: %d", (int)p.getTotalWeight());
        return;
    }

    combo_auto &a = combo_autos[index];

    if(mode == 3)
    {
        if(a.getType() == AUTOCOMBO_NONE)
            snprintf(l1, n, "Autocombo %d", index);
        else
            snprintf(l1, n, "Autocombo %d: %s%s", index, zq_autocombo_type_name(a.getType()), a.valid() ? "" : " (not valid)");
        return;
    }

    snprintf(l1, n, "Auto: %d", index);
    snprintf(l2, n, "CSet: %d", CSet);
    snprintf(l3, n, "%.15s", zq_autocombo_type_name(a.getType()));
}

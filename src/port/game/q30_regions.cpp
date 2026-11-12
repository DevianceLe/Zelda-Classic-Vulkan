// 3.0 scrolling regions, player only. See q30_regions.h.

#include "precompiled.h" //always first

#include <map>
#include <vector>
#include <algorithm>

#include "zelda.h"
#include "zdefs.h"
#include "maps.h"
#include "link.h"
#include "q255.h"
#include "q30_triggers.h"
#include "q30_regions.h"

extern LinkClass Link;
extern mapscr tmpscr[2];
extern mapscr tmpscr2[6];
extern mapscr tmpscr3[6];
extern int playing_field_offset;

// (defined in sprite.cpp, which ZQuest builds too)

namespace
{

struct Region
{
    bool on;
    int map, origin, ox, oy, w, h;   // origin screen, its column/row, size in screens
    int dx, dy;                      // the current screen within it
};

Region g_reg = { false, -1, 0, 0, 0, 1, 1, 0, 0 };

// The other screens of the region, as drawn and walked on: copies of the quest's screens, or
// the live screen Link last left (its combos as he left them).
struct RegionScreen
{
    mapscr base;
    mapscr layers[6];
};

std::map<int, RegionScreen *> g_screens;

struct ScreenState
{
    bool guys, enemies, did_secret;
    int hasitem;
};

std::map<int, ScreenState> g_screen_state;  // screens of the region visited in this stay
std::map<long, int> g_sprite_screen;        // sprite UID -> the screen it came from
std::vector<signed char> g_map_valid;    // per map: -1 unknown, 0 invalid region layout, 1 valid
BITMAP *g_canvas = NULL;                 // 3 x 3 screens, the current one in the middle

// The camera in region pixels, worked out once a frame. After a scroll into a region it starts
// over the screen the scroll showed and eases to where it belongs (upstream instead shifts the
// camera during the scroll); otherwise it is exactly upstream's.
int g_cam_vx = 0, g_cam_vy = 0, g_cam_frame = -1;
bool g_cam_easing = false;

void camera_update(int lx, int ly)
{
    if(g_cam_frame == frame)
        return;

    g_cam_frame = frame;

    // upstream's calculate_viewport, centred on Link and kept inside the region
    const int wx = lx + g_reg.dx * 256, wy = ly + g_reg.dy * 176;
    const int tx = vbound(wx + 8 - 128, 0, g_reg.w * 256 - 256);
    const int ty = vbound(wy + 8 - 88, 0, g_reg.h * 176 - 176);

    if(!g_cam_easing)
    {
        g_cam_vx = tx;
        g_cam_vy = ty;
        return;
    }

    const int step = 6;
    g_cam_vx += vbound(tx - g_cam_vx, -step, step);
    g_cam_vy += vbound(ty - g_cam_vy, -step, step);
    g_cam_easing = g_cam_vx != tx || g_cam_vy != ty;
}


void forget_screens()
{
    for(std::map<int, RegionScreen *>::iterator it = g_screens.begin(); it != g_screens.end(); ++it)
        delete it->second;

    g_screens.clear();
    g_screen_state.clear();
    g_sprite_screen.clear();
}

int region_id(int map, int scr)
{
    if(map < 0 || map >= (int)q255_map_region_ids.size() || scr < 0 || scr >= 128)
        return 0;

    const std::vector<byte> &ids = q255_map_region_ids[map];

    if(ids.size() < 64)
        return 0;

    const int sx = scr % 16, sy = scr / 16;
    const byte b = ids[sy * 8 + sx / 2];
    return (sx & 1) ? (b & 0xF) : ((b >> 4) & 0xF);
}

// upstream's calculate_region walk (zc/maps.cpp): left along the entry row, up the new column,
// then right and down from the origin
void region_rect(int map, int scr, int *ox, int *oy, int *w, int *h)
{
    const int id = region_id(map, scr);
    int x = scr % 16, y = scr / 16;

    while(x > 0 && region_id(map, x - 1 + y * 16) == id) --x;
    while(y > 0 && region_id(map, x + (y - 1) * 16) == id) --y;

    int rx = x, by = y;

    while(rx < 15 && region_id(map, rx + 1 + y * 16) == id) ++rx;
    while(by < 7 && region_id(map, x + (by + 1) * 16) == id) ++by;

    *ox = x;
    *oy = y;
    *w = rx - x + 1;
    *h = by - y + 1;
}

// upstream's editor check (core/mapscr.cpp get_all_region_descriptions): every region a
// rectangle of one id, not touching another screen of the same id. A map that fails is played
// without regions.
bool map_valid(int map)
{
    if(map < 0)
        return false;

    if((int)g_map_valid.size() <= map)
        g_map_valid.resize(map + 1, -1);

    if(g_map_valid[map] >= 0)
        return g_map_valid[map] != 0;

    bool seen[128] = { false };
    bool ok = true;

    for(int i = 0; i < 128 && ok; ++i)
    {
        if(seen[i])
            continue;

        if(!region_id(map, i))
        {
            seen[i] = true;
            continue;
        }

        int ox, oy, w, h;
        region_rect(map, i, &ox, &oy, &w, &h);
        const int id = region_id(map, ox + oy * 16);

        for(int y = oy; y < oy + h && ok; ++y)
            for(int x = ox; x < ox + w && ok; ++x)
            {
                if(seen[x + y * 16] || region_id(map, x + y * 16) != id)
                    ok = false;

                seen[x + y * 16] = true;
            }

        for(int x = ox; x < ox + w && ok; ++x)
        {
            if(oy > 0 && region_id(map, x + (oy - 1) * 16) == id) ok = false;
            if(oy + h < 8 && region_id(map, x + (oy + h) * 16) == id) ok = false;
        }

        for(int y = oy; y < oy + h && ok; ++y)
        {
            if(ox > 0 && region_id(map, ox - 1 + y * 16) == id) ok = false;
            if(ox + w < 16 && region_id(map, ox + w + y * 16) == id) ok = false;
        }
    }

    if(!ok)
        al_trace("q30 regions: map %d's scrolling regions aren't rectangles; it plays without them\n", map + 1);

    g_map_valid[map] = ok ? 1 : 0;
    return ok;
}

// the screen at (sdx, sdy) screens from the current one, if it is in the region
bool in_region(int sdx, int sdy, int *scr)
{
    if(!g_reg.on)
        return false;

    const int x = g_reg.dx + sdx, y = g_reg.dy + sdy;

    if(x < 0 || y < 0 || x >= g_reg.w || y >= g_reg.h)
        return false;

    *scr = g_reg.ox + x + (g_reg.oy + y) * 16;
    return true;
}

RegionScreen *region_screen(int scr)
{
    std::map<int, RegionScreen *>::iterator it = g_screens.find(scr);

    if(it != g_screens.end())
        return it->second;

    const size_t index = (size_t)g_reg.map * MAPSCRS + scr;

    if(index >= TheMaps.size())
        return NULL;

    RegionScreen *rs = new RegionScreen;
    rs->base = TheMaps[index];

    for(int i = 0; i < 6; ++i)
    {
        const int lm = rs->base.layermap[i];
        const size_t li = (size_t)(lm - 1) * MAPSCRS + rs->base.layerscreen[i];

        if(lm > 0 && li < TheMaps.size())
            rs->layers[i] = TheMaps[li];
        else
            rs->layers[i] = rs->base, rs->layers[i].valid = 0;     // never drawn: layermap is 0
    }

    g_screens[scr] = rs;
    return rs;
}

// The neighbour's layers stand in for tmpscr3 (do_layer's "other screen" source, used only while
// scrolling) while it is drawn.
struct LayersIn
{
    RegionScreen *rs;
    LayersIn(RegionScreen *r) : rs(r) { for(int i = 0; i < 6; ++i) std::swap(tmpscr3[i], rs->layers[i]); }
    ~LayersIn() { for(int i = 0; i < 6; ++i) std::swap(tmpscr3[i], rs->layers[i]); }
};

bool overlaps(int a0, int a1, int b0, int b1)
{
    return a0 < b1 && b0 < a1;
}

} // namespace

bool q30_region_on()
{
    return g_reg.on;
}

static bool g_listed = false;

void q30_region_forget()
{
    g_reg.on = false;
    g_reg.map = -1;
    g_map_valid.clear();
    forget_screens();
    g_listed = false;
}

// ZC_REGION_LIST=1 (tests): every map's regions, and the DMaps on those maps (once per game)
static void list_regions()
{
    for(int map = 0; map < (int)q255_map_region_ids.size(); ++map)
    {
        bool shown = false, seen[128] = { false };

        for(int scr = 0; scr < 128; ++scr)
        {
            if(seen[scr] || !region_id(map, scr))
                continue;

            int ox, oy, w, h;
            region_rect(map, scr, &ox, &oy, &w, &h);

            for(int y = oy; y < oy + h; ++y)
                for(int x = ox; x < ox + w; ++x)
                    seen[x + y * 16] = true;

            if(w * h > 1)
            {
                al_trace("q30 regions: map %d: %dx%d screens from 0x%02X (id %d)%s\n", map + 1, w, h, ox + oy * 16, region_id(map, scr),
                         map_valid(map) ? "" : " (map invalid)");
                shown = true;
            }
        }

        if(shown)
            for(int d = 0; d < MAXDMAPS; ++d)
                if(DMaps[d].map == map && DMaps[d].name[0])
                    al_trace("q30 regions:   DMap %d \"%s\" xoff %d continue 0x%02X\n", d, DMaps[d].name, DMaps[d].xoff, DMaps[d].cont);
    }
}

void q30_region_screen_loaded()
{
    static const bool list = getenv("ZC_REGION_LIST") != NULL;

    if(list && !g_listed && q30_active())
    {
        g_listed = true;
        list_regions();
    }

    const Region old = g_reg;
    g_reg.on = false;

    if(!q30_active() || currscr >= 128 || !map_valid(currmap) || !region_id(currmap, currscr))
    {
        forget_screens();
        return;
    }

    int ox, oy, w, h;
    region_rect(currmap, currscr, &ox, &oy, &w, &h);

    const int dx = currscr % 16 - ox, dy = currscr / 16 - oy;

    if(w * h <= 1 || dx < 0 || dy < 0 || dx >= w || dy >= h)
    {
        forget_screens();
        return;
    }

    const bool same_region = old.on && old.map == currmap && old.ox == ox && old.oy == oy && old.w == w && old.h == h;

    if(!same_region)
    {
        forget_screens();

        // a scroll ends with this screen in view: the camera starts there; anything else
        // (warp, start, continue) puts it straight where it belongs
        g_cam_vx = dx * 256;
        g_cam_vy = dy * 176;
        g_cam_easing = screenscrolling;
        g_cam_frame = -1;
    }

    // the current screen is drawn live; a copy kept from an earlier visit is stale now
    std::map<int, RegionScreen *>::iterator it = g_screens.find(currscr);

    if(it != g_screens.end())
    {
        delete it->second;
        g_screens.erase(it);
    }

    g_reg.on = true;
    g_reg.map = currmap;
    g_reg.ox = ox;
    g_reg.oy = oy;
    g_reg.w = w;
    g_reg.h = h;
    g_reg.dx = dx;
    g_reg.dy = dy;
    g_reg.origin = ox + oy * 16;

    if(getenv("ZC_EVENTLOG") && !(old.on && old.map == currmap && old.ox == ox && old.oy == oy))
        al_trace("q30 region: map %d screen 0x%02X is in a %dx%d region from 0x%02X\n", currmap + 1, currscr, w, h, g_reg.origin);
}

int q30_region_origin() { return g_reg.on ? g_reg.origin : currscr; }
int q30_region_id() { return g_reg.on ? region_id(g_reg.map, g_reg.origin) : 0; }
int q30_region_width() { return g_reg.on ? g_reg.w * 256 : 256; }
int q30_region_height() { return g_reg.on ? g_reg.h * 176 : 176; }
int q30_region_screens_wide() { return g_reg.on ? g_reg.w : 1; }
int q30_region_screens_high() { return g_reg.on ? g_reg.h : 1; }
int q30_region_offset_x() { return g_reg.on ? g_reg.dx * 256 : 0; }
int q30_region_offset_y() { return g_reg.on ? g_reg.dy * 176 : 0; }


void q30_region_camera(int lx, int ly, int *cam_x, int *cam_y)
{
    *cam_x = *cam_y = 0;

    if(!g_reg.on)
        return;

    camera_update(lx, ly);
    *cam_x = g_cam_vx - g_reg.dx * 256;
    *cam_y = g_cam_vy - g_reg.dy * 176;
}

void q30_region_viewport(int *vx, int *vy)
{
    int cx, cy;
    q30_region_camera(int(Link.getX()), int(Link.getY()), &cx, &cy);
    *vx = cx + q30_region_offset_x();
    *vy = cy + q30_region_offset_y();
}

void q30_region_compose_lower(BITMAP *scrollbuf)
{
    if(!g_reg.on)
        return;

    int cx, cy;
    q30_region_camera(int(Link.getX()), int(Link.getY()), &cx, &cy);

    if(!cx && !cy)
        return;

    if(!g_canvas)
        g_canvas = create_bitmap_ex(8, 768, 528);

    if(!g_canvas)
        return;

    clear_bitmap(g_canvas);
    const int pfo = playing_field_offset;
    blit(scrollbuf, g_canvas, 0, pfo, 256, 176, 256, 176);

    for(int sdy = -1; sdy <= 1; ++sdy)
        for(int sdx = -1; sdx <= 1; ++sdx)
        {
            int scr;

            if((!sdx && !sdy) || !overlaps(sdx * 256, sdx * 256 + 256, cx, cx + 256) || !overlaps(sdy * 176, sdy * 176 + 176, cy, cy + 176)
               || !in_region(sdx, sdy, &scr))
                continue;

            RegionScreen *rs = region_screen(scr);

            if(!rs)
                continue;

            mapscr *s = &rs->base;
            const int ox = 256 + sdx * 256, oy = 176 + sdy * 176;
            const int x = -ox, y = pfo - oy;
            LayersIn in(rs);

            // as draw_screen orders them: background layers, layer 0, layer 1, FFCs, layer 2
            if(s->flags7 & fLAYER2BG) do_layer(g_canvas, 1, s, x, y, 3);
            if(s->flags7 & fLAYER3BG) do_layer(g_canvas, 2, s, x, y, 3);

            putscr(g_canvas, ox, oy, s);
            do_layer(g_canvas, 0, s, x, y, 3);
            do_layer(g_canvas, -3, s, x, y, 3);

            if(!(s->flags7 & fLAYER2BG))
                do_layer(g_canvas, 1, s, x, y, 3);
        }

    blit(g_canvas, scrollbuf, 256 + cx, 176 + cy, 0, pfo, 256, 176);
}

void q30_region_upper_layer(BITMAP *bmp, int type, mapscr *this_screen, bool drawprimitives)
{
    int cx, cy;
    q30_region_camera(int(Link.getX()), int(Link.getY()), &cx, &cy);
    do_layer(bmp, type, this_screen, cx, cy, 2, false, drawprimitives);

    if(!g_reg.on || (!cx && !cy))
        return;

    for(int sdy = -1; sdy <= 1; ++sdy)
        for(int sdx = -1; sdx <= 1; ++sdx)
        {
            int scr;

            if((!sdx && !sdy) || !overlaps(sdx * 256, sdx * 256 + 256, cx, cx + 256) || !overlaps(sdy * 176, sdy * 176 + 176, cy, cy + 176)
               || !in_region(sdx, sdy, &scr))
                continue;

            RegionScreen *rs = region_screen(scr);

            if(!rs)
                continue;

            LayersIn in(rs);
            do_layer(bmp, type, &rs->base, cx - sdx * 256, cy - sdy * 176, 3);
        }
}

void q30_region_begin_sprites()
{
    q30_region_camera(int(Link.getX()), int(Link.getY()), &q30_draw_cam_x, &q30_draw_cam_y);
}

void q30_region_end_sprites()
{
    q30_draw_cam_x = q30_draw_cam_y = 0;
}

namespace
{

int floordiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// one point outside the current screen: the neighbour's combos (the screen and its layers 1
// and 2, by their walkability bits, as 2.53's _walkflag reads them)
bool point_solid(int px, int py)
{
    const int sdx = floordiv(px, 256), sdy = floordiv(py, 176);
    int scr;

    if(!in_region(sdx, sdy, &scr))
        return false;           // past the region: as off-screen always was

    RegionScreen *rs = region_screen(scr);

    if(!rs)
        return false;

    const int lx = px - sdx * 256, ly = py - sdy * 176;
    const int pos = (lx >> 4) + (ly & 0xF0);

    if(pos < 0 || pos >= 176)
        return false;

    int b = 1;
    if(lx & 8) b <<= 2;
    if(ly & 8) b <<= 1;

    if(pos < (int)rs->base.data.size() && (combobuf[rs->base.data[pos]].walk & b))
        return true;

    for(int i = 0; i < 2; ++i)
        if(rs->base.layermap[i] > 0 && pos < (int)rs->layers[i].data.size() && (combobuf[rs->layers[i].data[pos]].walk & b))
            return true;

    return false;
}

} // namespace

bool _walkflag(int x, int y, int cnt);

int q30_region_walkflag(int x, int y, int cnt)
{
    if(!g_reg.on)
        return -1;

    const int x2 = cnt == 2 ? x + 8 : x;

    if(x >= 0 && x2 <= 255 && y >= 0 && y <= 175)
        return -1;

    for(int k = 0; k < (cnt == 2 ? 2 : 1); ++k)
    {
        const int px = k ? x + 8 : x;
        const bool inside = px >= 0 && px <= 255 && y >= 0 && y <= 175;

        if(inside ? _walkflag(px, y, 1) : point_solid(px, y))
            return 1;
    }

    return 0;
}

int q30_region_crossed(int x, int y, int *ndx, int *ndy)
{
    *ndx = *ndy = 0;

    if(!g_reg.on)
        return -1;

    const int cx = x + 8, cy = y + 8;
    int dx = cx >= 256 ? 1 : (cx < 0 ? -1 : 0);
    int dy = cy >= 176 ? 1 : (cy < 0 ? -1 : 0);
    int scr;

    // one axis at a time (a corner crossing goes through the side screen first)
    if(dx && !in_region(dx, 0, &scr)) dx = 0;
    if(dy && !in_region(0, dy, &scr)) dy = 0;
    if(dx && dy) dy = 0;

    if(!dx && !dy)
        return -1;

    *ndx = dx;
    *ndy = dy;
    return dy < 0 ? up : dy > 0 ? down : dx < 0 ? left : right;
}

bool q30_region_has_neighbour(int dir)
{
    int scr;

    switch(dir)
    {
    case up: return in_region(0, -1, &scr);
    case down: return in_region(0, 1, &scr);
    case left: return in_region(-1, 0, &scr);
    case right: return in_region(1, 0, &scr);
    }

    return false;
}

void q30_region_keep_current()
{
    if(!g_reg.on)
        return;

    std::map<int, RegionScreen *>::iterator it = g_screens.find(currscr);
    RegionScreen *rs = it != g_screens.end() ? it->second : (g_screens[currscr] = new RegionScreen);
    rs->base = tmpscr[0];

    for(int i = 0; i < 6; ++i)
        rs->layers[i] = tmpscr2[i];
}

void q30_region_bounds(int *l, int *t, int *r, int *b)
{
    if(!g_reg.on)
    {
        *l = *t = 0;
        *r = 256;
        *b = 176;
        return;
    }

    *l = -g_reg.dx * 256;
    *t = -g_reg.dy * 176;
    *r = (g_reg.w - g_reg.dx) * 256;
    *b = (g_reg.h - g_reg.dy) * 176;
}

void q30_region_view_bounds(int *l, int *t, int *r, int *b)
{
    int cx, cy;
    q30_region_camera(int(Link.getX()), int(Link.getY()), &cx, &cy);
    *l = cx;
    *t = cy;
    *r = cx + 256;
    *b = cy + 176;
}

extern sprite_list guys, items, Ewpns;

void q30_region_tag_sprites()
{
    if(!g_reg.on)
        return;

    sprite_list *lists[] = { &guys, &items, &Ewpns };

    for(int k = 0; k < 3; ++k)
        for(int i = 0; i < lists[k]->Count(); ++i)
        {
            const long uid = lists[k]->spr(i)->getUID();

            if(!g_sprite_screen.count(uid))
                g_sprite_screen[uid] = currscr;
        }
}

int q30_region_sprite_screen(long uid)
{
    if(!g_reg.on)
        return -1;

    std::map<long, int>::iterator it = g_sprite_screen.find(uid);
    return it == g_sprite_screen.end() ? -1 : it->second;
}

void q30_region_save_screen_state()
{
    if(!g_reg.on)
        return;

    ScreenState s;
    s.guys = loaded_guys;
    s.enemies = loaded_enemies;
    s.did_secret = did_secret;
    s.hasitem = hasitem;
    g_screen_state[currscr] = s;
}

bool q30_region_restore_screen_state()
{
    std::map<int, ScreenState>::iterator it = g_reg.on ? g_screen_state.find(currscr) : g_screen_state.end();

    if(it == g_screen_state.end())
        return false;

    loaded_guys = it->second.guys;
    loaded_enemies = it->second.enemies;
    did_secret = it->second.did_secret;
    hasitem = it->second.hasitem;
    return true;
}

void loadenemies();

// Upstream loads a region screen's enemies once the camera shows any of it. This engine's enemy
// loader works on the current screen, so the neighbour stands in as the current screen while it
// runs (its screen and layers swapped into tmpscr / tmpscr2, currscr and homescr pointing at
// it), and its new enemies are moved into the current screen's coordinates and tagged as its.
// Side-loading patterns (enemies walking in over several frames) still load when Link enters.
void q30_region_load_enemies_in_view()
{
    if(!g_reg.on)
        return;

    int cx, cy;
    q30_region_camera(int(Link.getX()), int(Link.getY()), &cx, &cy);

    for(int sdy = -1; sdy <= 1; ++sdy)
        for(int sdx = -1; sdx <= 1; ++sdx)
        {
            int scr;

            if((!sdx && !sdy) || !overlaps(sdx * 256, sdx * 256 + 256, cx, cx + 256) || !overlaps(sdy * 176, sdy * 176 + 176, cy, cy + 176)
               || !in_region(sdx, sdy, &scr))
                continue;

            std::map<int, ScreenState>::iterator st = g_screen_state.find(scr);

            if(st != g_screen_state.end() && st->second.enemies)
                continue;

            RegionScreen *rs = region_screen(scr);

            if(!rs || rs->base.pattern == pSIDES || rs->base.pattern == pSIDESR)
                continue;

            // stand-in
            std::swap(tmpscr[0], rs->base);

            for(int i = 0; i < 6; ++i)
                std::swap(tmpscr2[i], rs->layers[i]);

            const int keep_scr = currscr, keep_home = homescr;
            const bool keep_loaded = loaded_enemies;
            currscr = homescr = scr;
            loaded_enemies = false;
            const int before = guys.Count();

            loadenemies();

            currscr = keep_scr;
            homescr = keep_home;
            loaded_enemies = keep_loaded;

            for(int i = 0; i < 6; ++i)
                std::swap(tmpscr2[i], rs->layers[i]);

            std::swap(tmpscr[0], rs->base);

            for(int i = before; i < guys.Count(); ++i)
            {
                sprite *s = guys.spr(i);
                s->x += sdx * 256;
                s->y += sdy * 176;
                g_sprite_screen[s->getUID()] = scr;
            }

            // its enemies are out; its room guy and item come when Link enters it
            ScreenState s;
            s.guys = false;
            s.enemies = true;
            s.did_secret = false;
            s.hasitem = 0;
            g_screen_state[scr] = s;

            if(getenv("ZC_EVENTLOG"))
                al_trace("q30 region: screen 0x%02X came into view: %d enemies\n", scr, guys.Count() - before);
        }
}

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

// upstream's calculate_viewport, centred on Link at local (lx, ly) and kept inside the region
void camera_target(int lx, int ly, int *tx, int *ty)
{
    const int wx = lx + g_reg.dx * 256, wy = ly + g_reg.dy * 176;
    *tx = vbound(wx + 8 - 128, 0, g_reg.w * 256 - 256);
    *ty = vbound(wy + 8 - 88, 0, g_reg.h * 176 - 176);
}

void camera_update(int lx, int ly)
{
    if(g_cam_frame == frame)
        return;

    g_cam_frame = frame;

    int tx, ty;
    camera_target(lx, ly, &tx, &ty);

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
    q30_scroll_reset();
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

static bool g_scroll_view_on = false;      // a scroll is running: Viewport->X / Y follow its view
static int g_scroll_view_x = 0, g_scroll_view_y = 0;

void q30_region_viewport(int *vx, int *vy)
{
    if(g_scroll_view_on)
    {
        *vx = g_scroll_view_x;
        *vy = g_scroll_view_y;
        return;
    }

    int cx, cy;
    q30_region_camera(int(Link.getX()), int(Link.getY()), &cx, &cy);
    *vx = cx + q30_region_offset_x();
    *vy = cy + q30_region_offset_y();
}

// ---- draw_screen's lower layers through the camera ------------------------------------------
// With the camera off this screen's grid they go on g_canvas, 3 x 3 screens with this one in
// the middle (its top-left at 256, 176). 2.53's putcombo drops a tile at a negative position, so
// layer 0 and the background layers can't be drawn straight through the camera; on the canvas
// every screen is on the grid, and script drawings on these layers aren't cut at this screen's
// edges. The window the camera shows then goes to scrollbuf's playing field.

static bool g_lower_canvas = false;     // the lower layers are on the canvas this frame
static int g_lower_cx = 0, g_lower_cy = 0;  // the camera for them (relative to this screen)

BITMAP *q30_region_lower_begin(BITMAP *scrollbuf)
{
    g_lower_canvas = false;

    if(!g_reg.on || q30_scroll_on())
        return scrollbuf;

    q30_region_camera(int(Link.getX()), int(Link.getY()), &g_lower_cx, &g_lower_cy);

    if(!g_lower_cx && !g_lower_cy)
        return scrollbuf;

    if(!g_canvas)
        g_canvas = create_bitmap_ex(8, 768, 528);

    if(!g_canvas)
        return scrollbuf;

    clear_bitmap(g_canvas);
    g_lower_canvas = true;

    // this screen's (x, y) is drawn at (x - q30_draw_cam_x, y + pfo - q30_draw_cam_y): 256, 176 on
    // the canvas (sprites drawn here, Link under a climb cover, follow the same offsets)
    q30_draw_cam_x = -256;
    q30_draw_cam_y = playing_field_offset - 176;
    return g_canvas;
}

void q30_region_lower_layer(BITMAP *lower, int which)
{
    if(!g_lower_canvas || lower != g_canvas)
        return;

    const int cx = g_lower_cx, cy = g_lower_cy;
    const int pfo = playing_field_offset;

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

            // as draw_screen draws this screen's: each by the other screen's own flags
            switch(which)
            {
            case Q30_LOWER_BG2:
                if(s->flags7 & fLAYER2BG) do_layer(lower, 1, s, x, y, 3);
                break;

            case Q30_LOWER_BG3:
                if(s->flags7 & fLAYER3BG) do_layer(lower, 2, s, x, y, 3);
                break;

            case Q30_LOWER_LAYER0:
                putscr(lower, ox, oy, s);
                break;

            case Q30_LOWER_LAYER1:
                do_layer(lower, 0, s, x, y, 3);
                break;

            case Q30_LOWER_FFCS:
                do_layer(lower, -3, s, x, y, 3);
                break;

            case Q30_LOWER_LAYER2:
                if(!(s->flags7 & fLAYER2BG)) do_layer(lower, 1, s, x, y, 3);
                break;
            }
        }
}

BITMAP *q30_region_lower_local(BITMAP *lower)
{
    static BITMAP *sub = NULL;
    static int sub_pfo = -1;

    if(!g_lower_canvas || lower != g_canvas)
        return lower;

    // this screen's top-left is at 256, 176 on the canvas; the sub-bitmap starts pfo above it
    // (remade when the playing field offset moves, as an earthquake does)
    if(!sub || sub_pfo != playing_field_offset)
    {
        if(sub)
            destroy_bitmap(sub);

        sub_pfo = playing_field_offset;
        sub = create_sub_bitmap(g_canvas, 256, 176 - sub_pfo, 512, 352 + sub_pfo);
    }

    return sub ? sub : lower;
}

void q30_draw_view_origin(BITMAP *bmp, int *x, int *y)
{
    *x = *y = 0;

    if(g_lower_canvas && bmp == g_canvas)
    {
        *x = 256 + g_lower_cx;
        *y = 176 + g_lower_cy - playing_field_offset;
    }
}

void q30_region_lower_end(BITMAP *scrollbuf)
{
    if(!g_lower_canvas)
        return;

    blit(g_canvas, scrollbuf, 256 + g_lower_cx, 176 + g_lower_cy, 0, playing_field_offset, 256, 176);
    g_lower_canvas = false;
    q30_draw_cam_x = q30_draw_cam_y = 0;
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

extern sprite_list guys, items, Lwpns, Ewpns;

bool q30_draw_origin_offset(int origin, int target_uid, int xoff, int yoff, int *x, int *y)
{
    // the camera relative to the current screen (0 outside regions and while scrolling, whose
    // view scrollscr draws through itself)
    int cx = 0, cy = 0;

    if(g_reg.on && !q30_scroll_on())
        q30_region_camera(int(Link.getX()), int(Link.getY()), &cx, &cy);

    // the playing field's top-left as the caller has it: on the lower layers' canvas the camera
    // window is taken afterwards (q30_region_lower_end), so it is where this screen's top-left is
    // there; elsewhere without the camera do_layer took off
    const bool composed = g_lower_canvas;
    const int bx = xoff + (composed ? 0 : q30_draw_cam_x), by = yoff + (composed ? 0 : q30_draw_cam_y);
    int dx = 0, dy = 0;

    switch(origin)
    {
    case 0:     // DEFAULT: the region in a scrolling region, else the playing field (cx, cy 0)
    case 3:     // REGION
    case 4:     // REGION_SCROLLING_NEW
    case 5:     // REGION_SCROLLING_OLD
        dx = -cx;
        dy = -cy;
        break;

    case 1:     // PLAYING_FIELD
        break;

    case 2:     // SCREEN: from the top of the screen
        dy = -playing_field_offset;
        break;

    case 6:     // SPRITE: from the sprite's position in the region
    {
        sprite *s = Link.getUID() == target_uid ? (sprite *)&Link : guys.getByUID(target_uid);

        if(!s) s = items.getByUID(target_uid);
        if(!s) s = Lwpns.getByUID(target_uid);
        if(!s) s = Ewpns.getByUID(target_uid);

        if(!s)
            return false;

        dx = int(s->x) - cx;
        dy = int(s->y) - cy;
        break;
    }
    }

    *x = bx + dx + (composed ? cx : 0);
    *y = by + dy + (composed ? cy : 0);
    return true;
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

// ---- scrolling between screens (LinkClass::scrollscr) --------------------------------------
// After upstream's HeroClass::scrollscr (zc/hero.cpp): its ScrollingData, and its view, which
// keeps the old region's camera on the other axis and lines it up with the new region's.

namespace
{

int g_scroll_data[Q30_SCROLL_COUNT] = { -1 };      // DIR -1: no scroll

struct Scroll
{
    bool on;
    int dir;
    bool horiz;                 // left / right: the other axis is y
    Region old_reg;             // the region scrolled out of (on false: a screen of its own)
    int old_vx, old_vy;         // its camera when the scroll began, in its pixels
    int old_sx, old_sy;         // the old screen's top-left in the old region's pixels
    int lx, ly;                 // Link when the scroll began (local to the old screen)
    int obx, oby, nbx, nby;     // the old and the new screen's top-left in scrollscr's picture
    int new_off_x, new_off_y;   // the new region's top-left in the old region's pixels
    int s_from, s_to, s;        // the view's offset on the other axis, relative to both screens
    int align_mode;             // 0: line up before the scroll, 1: after it
    int align_left;             // pixels still to line up
    RegionScreen *old_nb[2];    // the old screen's neighbours on the other axis (before, after)
    int new_nb[2];              // the new screen's (screen numbers, -1: none)
};

Scroll g_scroll;

void scroll_free_old()
{
    for(int k = 0; k < 2; ++k)
    {
        delete g_scroll.old_nb[k];
        g_scroll.old_nb[k] = NULL;
    }
}

// a screen beside the old (new_screen false) or the new one on the other axis: k 0 before, 1 after
RegionScreen *scroll_neighbour(bool new_screen, int k)
{
    if(!new_screen)
        return g_scroll.old_nb[k];

    return g_scroll.new_nb[k] >= 0 ? region_screen(g_scroll.new_nb[k]) : NULL;
}

// the view shows the neighbour k: it leans that way on the other axis
bool scroll_shows(int k)
{
    return k ? g_scroll.s > 0 : g_scroll.s < 0;
}

} // namespace

int q30_scroll_data(int index)
{
    return index >= 0 && index < Q30_SCROLL_COUNT ? g_scroll_data[index] : 0;
}

void q30_scroll_reset()
{
    memset(g_scroll_data, 0, sizeof(g_scroll_data));
    g_scroll_data[Q30_SCROLL_DIR] = -1;
    g_scroll_view_on = false;
    scroll_free_old();
    g_scroll.on = false;
    g_scroll.s = 0;
    g_scroll.align_left = 0;
    g_scroll.new_nb[0] = g_scroll.new_nb[1] = -1;
}

bool q30_scroll_on()
{
    return g_scroll.on;
}

void q30_scroll_begin(int dir)
{
    q30_scroll_reset();

    if(!q30_active() || dir < up || dir > right)
        return;

    Scroll &s = g_scroll;
    s.on = true;
    s.dir = dir;
    s.horiz = dir == left || dir == right;
    s.old_reg = g_reg;
    s.lx = int(Link.getX());
    s.ly = int(Link.getY());
    s.old_sx = g_reg.on ? g_reg.dx * 256 : 0;
    s.old_sy = g_reg.on ? g_reg.dy * 176 : 0;
    s.old_vx = s.old_sx;
    s.old_vy = s.old_sy;

    if(g_reg.on)
    {
        camera_update(s.lx, s.ly);
        s.old_vx = g_cam_vx;
        s.old_vy = g_cam_vy;
    }

    // the old region's screens beside the old one on the other axis, kept for the scroll (the new
    // screen's load forgets the old region)
    for(int k = 0; k < 2; ++k)
    {
        int scr;
        const int step = k ? 1 : -1;

        if(in_region(s.horiz ? 0 : step, s.horiz ? step : 0, &scr))
        {
            RegionScreen *rs = region_screen(scr);

            if(rs)
                s.old_nb[k] = new RegionScreen(*rs);
        }
    }

    // 2.53's picture: the new screen beside or above the old one
    s.obx = dir == left ? 256 : 0;
    s.oby = dir == up ? 176 : 0;
    s.nbx = dir == right ? 256 : 0;
    s.nby = dir == down ? 176 : 0;
}

void q30_scroll_loaded(bool moves)
{
    if(!g_scroll.on)
        return;

    Scroll &s = g_scroll;

    // where Link ends up on the new screen, and the new region's camera then
    const int nlx = s.dir == right ? 0 : s.dir == left ? 240 : s.lx;
    const int nly = s.dir == down ? 0 : s.dir == up ? 160 : s.ly;
    const int ndx = g_reg.on ? g_reg.dx : 0, ndy = g_reg.on ? g_reg.dy : 0;
    int new_vx = 0, new_vy = 0;

    if(g_reg.on)
    {
        camera_target(nlx, nly, &new_vx, &new_vy);

        // the scroll lines the camera up itself: no easing afterwards
        g_cam_vx = new_vx;
        g_cam_vy = new_vy;
        g_cam_easing = false;
        g_cam_frame = -1;
    }

    const int nx = s.old_sx + s.nbx - s.obx, ny = s.old_sy + s.nby - s.oby;
    s.new_off_x = nx - ndx * 256;
    s.new_off_y = ny - ndy * 176;

    // the other axis: the old camera relative to the old screen, the new one relative to the new
    s.s_from = s.horiz ? s.old_vy - s.old_sy : s.old_vx - s.old_sx;
    s.s_to = s.horiz ? new_vy - ndy * 176 : new_vx - ndx * 256;

    // line up first when the old region can show the view lined up (upstream's align_mode)
    int lo = 0, hi = 0;

    if(s.old_reg.on)
    {
        const int d = s.horiz ? s.old_reg.dy : s.old_reg.dx, n = s.horiz ? s.old_reg.h : s.old_reg.w;
        const int size = s.horiz ? 176 : 256;
        lo = -d * size;
        hi = (n - 1 - d) * size;
    }

    s.align_mode = (s.s_to >= lo && s.s_to <= hi) ? 0 : 1;
    s.align_left = moves ? abs(s.s_from - s.s_to) : 0;
    s.s = moves ? s.s_from : s.s_to;

    for(int k = 0; k < 2; ++k)
    {
        int scr;
        const int step = k ? 1 : -1;
        s.new_nb[k] = in_region(s.horiz ? 0 : step, s.horiz ? step : 0, &scr) ? scr : -1;
    }

    // upstream's ScrollingData at the start (the region delta is the new region's top-left in
    // the old region's pixels, which for a scroll to the next screen is the origin screens'
    // difference upstream uses)
    int *d = g_scroll_data;
    d[Q30_SCROLL_DIR] = s.dir;
    d[Q30_SCROLL_NEW_REGION_DELTA_X] = s.new_off_x;
    d[Q30_SCROLL_NEW_REGION_DELTA_Y] = s.new_off_y;
    d[Q30_SCROLL_NEW_SCREEN_X] = ndx * 256;
    d[Q30_SCROLL_NEW_SCREEN_Y] = ndy * 176;
    d[Q30_SCROLL_OLD_SCREEN_X] = s.old_sx;
    d[Q30_SCROLL_OLD_SCREEN_Y] = s.old_sy;
    d[Q30_SCROLL_NEW_HERO_X] = ndx * 256 + nlx;
    d[Q30_SCROLL_NEW_HERO_Y] = ndy * 176 + nly;
    d[Q30_SCROLL_OLD_HERO_X] = s.old_sx + s.lx;
    d[Q30_SCROLL_OLD_HERO_Y] = s.old_sy + s.ly;
    d[Q30_SCROLL_NEW_REGION_SCREEN_WIDTH] = g_reg.on ? g_reg.w : 1;
    d[Q30_SCROLL_NEW_REGION_SCREEN_HEIGHT] = g_reg.on ? g_reg.h : 1;
    d[Q30_SCROLL_OLD_REGION_SCREEN_WIDTH] = s.old_reg.on ? s.old_reg.w : 1;
    d[Q30_SCROLL_OLD_REGION_SCREEN_HEIGHT] = s.old_reg.on ? s.old_reg.h : 1;
    d[Q30_SCROLL_NEW_VIEWPORT_WIDTH] = d[Q30_SCROLL_OLD_VIEWPORT_WIDTH] = 256;
    d[Q30_SCROLL_NEW_VIEWPORT_HEIGHT] = d[Q30_SCROLL_OLD_VIEWPORT_HEIGHT] = 176;
    d[Q30_SCROLL_NEW_VIEWPORT_X] = new_vx;
    d[Q30_SCROLL_NEW_VIEWPORT_Y] = new_vy;
    d[Q30_SCROLL_OLD_VIEWPORT_X] = s.old_vx;
    d[Q30_SCROLL_OLD_VIEWPORT_Y] = s.old_vy;

    // the view where the scroll starts (scrollscr's picture: the old screen in full)
    q30_scroll_view(s.obx, s.oby);

    if(getenv("ZC_EVENTLOG"))
        al_trace("q30 scroll: dir %d, other axis %d -> %d (%s), new region at %d,%d\n", s.dir, s.s_from, s.s_to,
                 s.align_mode ? "after" : "first", s.new_off_x, s.new_off_y);
}

bool q30_scroll_align_first()
{
    return g_scroll.on && g_scroll.align_mode == 0 && g_scroll.align_left > 0;
}

bool q30_scroll_align_pending()
{
    return g_scroll.on && g_scroll.align_left > 0;
}

int q30_scroll_align_step(bool scroll_left)
{
    Scroll &s = g_scroll;

    if(!s.on || !s.align_left)
        return 0;

    int ret;

    if(s.align_mode == 0)
    {
        s.align_left = s.align_left > 4 ? s.align_left - 4 : 0;
        ret = s.align_left ? 1 : 0;
    }
    else if(!scroll_left)
    {
        s.align_left = s.align_left > 4 ? s.align_left - 4 : 0;
        ret = 2;
    }
    else
        return 0;

    // upstream: the view moves by (align_counter - |amount|) * sign(amount) from where it began
    const int amount = s.s_from - s.s_to;
    s.s = s.s_from + (s.align_left - abs(amount)) * (amount > 0 ? 1 : -1);
    return ret;
}

void q30_scroll_offset(int *ox, int *oy)
{
    *ox = *oy = 0;

    if(!g_scroll.on)
        return;

    if(g_scroll.horiz)
        *oy = g_scroll.s;
    else
        *ox = g_scroll.s;
}

void q30_scroll_view(int sx, int sy)
{
    if(!g_scroll.on)
        return;

    const Scroll &s = g_scroll;
    int ox, oy;
    q30_scroll_offset(&ox, &oy);

    // the view's top-left in the old region's pixels
    const int vx = s.old_sx + sx + ox - s.obx, vy = s.old_sy + sy + oy - s.oby;
    const int nx = s.old_sx + s.nbx - s.obx, ny = s.old_sy + s.nby - s.oby;
    int *d = g_scroll_data;
    d[Q30_SCROLL_NX] = nx - vx;
    d[Q30_SCROLL_NY] = ny - vy;
    d[Q30_SCROLL_OX] = s.old_sx - vx;
    d[Q30_SCROLL_OY] = s.old_sy - vy;
    d[Q30_SCROLL_NRX] = s.new_off_x - vx;
    d[Q30_SCROLL_NRY] = s.new_off_y - vy;
    d[Q30_SCROLL_ORX] = -vx;
    d[Q30_SCROLL_ORY] = -vy;

    // scripts see the view in the new region's pixels while it scrolls (upstream's
    // run_scrolling_script moves the viewport by the new region's offset)
    g_scroll_view_on = true;
    g_scroll_view_x = vx - s.new_off_x;
    g_scroll_view_y = vy - s.new_off_y;
}

bool q30_scroll_blit_lower(BITMAP *scrollbuf, BITMAP *framebuf, int sx, int sy)
{
    if(!g_scroll.on || !g_scroll.s)
        return false;

    if(!g_canvas)
        g_canvas = create_bitmap_ex(8, 768, 528);

    if(!g_canvas)
        return false;

    // scrollbuf holds 2.53's picture (the two screens, no playing field offset); put it in the
    // middle of the canvas and the screens beside them around it
    clear_bitmap(g_canvas);
    blit(scrollbuf, g_canvas, 0, 0, 256, 176, 512, 352);
    const int pfo = playing_field_offset;

    for(int n = 0; n < 2; ++n)
        for(int k = 0; k < 2; ++k)
        {
            RegionScreen *rs = scroll_shows(k) ? scroll_neighbour(n != 0, k) : NULL;

            if(!rs)
                continue;

            const int step = k ? 1 : -1;
            const int px = (n ? g_scroll.nbx : g_scroll.obx) + (g_scroll.horiz ? 0 : step * 256);
            const int py = (n ? g_scroll.nby : g_scroll.oby) + (g_scroll.horiz ? step * 176 : 0);
            const int ox = 256 + px, oy = 176 + py;
            mapscr *s = &rs->base;
            LayersIn in(rs);

            if(s->flags7 & fLAYER2BG) do_layer(g_canvas, 1, s, -ox, pfo - oy, 3);
            if(s->flags7 & fLAYER3BG) do_layer(g_canvas, 2, s, -ox, pfo - oy, 3);

            putscr(g_canvas, ox, oy, s);
        }

    int ox, oy;
    q30_scroll_offset(&ox, &oy);
    blit(g_canvas, framebuf, 256 + sx + ox, 176 + sy + oy, 0, pfo, 256, zc_field_h());
    return true;
}

void q30_scroll_layer(BITMAP *bmp, int type, bool new_screen, int x, int y, bool scrolling)
{
    if(!g_scroll.on || !g_scroll.s)
        return;

    for(int k = 0; k < 2; ++k)
    {
        RegionScreen *rs = scroll_shows(k) ? scroll_neighbour(new_screen, k) : NULL;

        // (background layers 2 and 3 are with the lower layers)
        if(!rs || (type == 1 && (rs->base.flags7 & fLAYER2BG)) || (type == 2 && (rs->base.flags7 & fLAYER3BG)))
            continue;

        const int step = k ? 1 : -1;
        LayersIn in(rs);
        do_layer(bmp, type, &rs->base, x - (g_scroll.horiz ? 0 : step * 256), y - (g_scroll.horiz ? step * 176 : 0), 3, scrolling);
    }
}

void q30_scroll_end()
{
    if(!g_scroll.on)
        return;

    if(g_reg.on)
    {
        g_cam_easing = false;
        g_cam_frame = -1;
    }

    q30_scroll_reset();
}

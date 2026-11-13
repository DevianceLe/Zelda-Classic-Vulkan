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

// each live region screen's own script and trigger state ("every screen of a region live")
void *zasm30_screen_state_new(const mapscr *scr);     // ffscript.cpp (q30_scripts_region.h)
void zasm30_screen_state_delete(void *state);
void *q30_trig_state_new();                             // q30_triggers.cpp
void q30_trig_state_delete(void *state);

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
// the live screen Link last left (its combos as he left them). In a region whose screens are
// live ("every screen of a region live", below) each is a whole screen kept running: its
// combos and FFCs here, what runs on it in `scripts` and `trig`, its FFC changers' state.
struct RegionScreen
{
    mapscr base;
    mapscr layers[6];
    bool live;
    void *scripts;                  // the script engine's FFC, screen and combo script state
    void *trig;                     // the combo triggers' state
    short ffposx[32], ffposy[32];   // maps.cpp's FFC changer state
    long ffprvx[32], ffprvy[32];

    RegionScreen() : live(false), scripts(NULL), trig(NULL) { no_changers(); }

    // a copy (the screens a scroll keeps drawing) is only its combos and FFCs, nothing running
    RegionScreen(const RegionScreen &o) : base(o.base), live(false), scripts(NULL), trig(NULL)
    {
        for(int i = 0; i < 6; ++i)
            layers[i] = o.layers[i];

        no_changers();
    }

    ~RegionScreen()
    {
        if(scripts)
            zasm30_screen_state_delete(scripts);

        if(trig)
            q30_trig_state_delete(trig);
    }

    // as loadscr leaves them: no changer met yet
    void no_changers()
    {
        for(int i = 0; i < 32; ++i)
        {
            ffposx[i] = ffposy[i] = -1000;
            ffprvx[i] = ffprvy[i] = -10000000;
        }
    }

private:
    RegionScreen &operator=(const RegionScreen &);
};

std::map<int, RegionScreen *> g_screens;

// The other screens of the current region are live (below); a hand-off between live screens
// is going on (q30_region_go_live), so loading leaves them as they are.
bool g_live = false;
bool g_going_live = false;
void region_make_live(int destdmap);
bool region_draw_ffcs(BITMAP *bmp, int type, int x, int y);

struct ScreenState
{
    bool guys, enemies, did_secret;
    int hasitem;
};

std::map<int, ScreenState> g_screen_state;  // screens of the region visited in this stay
std::map<long, int> g_sprite_screen;        // sprite UID -> the screen it came from
std::vector<signed char> g_map_valid;    // per map: -1 unknown, 0 invalid region layout, 1 valid
BITMAP *g_canvas = NULL;                 // 3 x 3 screens, the current one in the middle

// ---- the camera: upstream's viewport and its follow logic (zc/maps.cpp) ----------------------
// The viewport (g_cam_vx/vy) is the camera's top-left in region pixels, kept inside the region.
// It is centred on a focus point that follows Link's centre: with a dead zone the focus stays
// put while Link moves inside a box around it, a lookahead leads it the way Link walks, and
// recentering eases it back onto him once he has stood still a while. Init data or the DMap
// set these; with all of them 0 (quests saved before them) the camera is locked on Link.
// The focus is kept in 1/10000 pixels, as upstream's zfix. The follow logic moves once a frame
// after Link has (q30_region_tick_camera); anything else only places the viewport by it.

const int64_t Z = 10000;

struct Follow
{
    int deadzone_w, deadzone_h;         // pixels
    int lookahead_x, lookahead_y;       // pixels ahead of Link's way (negative: behind)
    long lookahead_speed;               // 1/10000 pixels a frame
    long recenter_speed;                // 1/10000 pixels a frame; 0: never
    int recenter_delay;                 // frames
};

const Follow FOLLOW_DEFAULT = { 0, 0, 0, 0, 10000, 0, 30 };
Follow g_follow_init = FOLLOW_DEFAULT;
std::map<int, Follow> g_follow_dmap;    // DMaps with their own (dmfCUSTOM_VIEWPORT_FOLLOW)

struct FollowState
{
    int64_t x, y;           // the focus, in region pixels
    int64_t tx, ty;         // Link's centre when last seen
    int64_t lx, ly;         // the lookahead offset
    int idle;               // frames Link has stood still
};

FollowState g_fs = { 0, 0, 0, 0, 0, 0, 0 };
bool g_fs_reset = true;     // put the focus on Link when next looked at (a warp has placed him)
bool g_handoff = false;     // the screen loading is a hand-off inside the region
int g_cam_vx = 0, g_cam_vy = 0;
int g_vp_frame = -1;        // the frame the viewport was last placed in

// upstream's zfix::getInt (rounds half away from 0)
int z_int(int64_t v)
{
    return int(v / Z + (v % Z) / 5000);
}

// upstream's get_viewport_follow_settings (no script overrides)
Follow follow_settings()
{
    std::map<int, Follow>::const_iterator it = g_follow_dmap.find(currdmap);
    Follow s = it != g_follow_dmap.end() ? it->second : g_follow_init;
    s.deadzone_w = vbound(s.deadzone_w, 0, 240);       // VIEWPORT_FOLLOW_MAX_DEADZONE_W, _H
    s.deadzone_h = vbound(s.deadzone_h, 0, 160);
    s.lookahead_speed = vbound(s.lookahead_speed, 0L, 240L * 10000);   // VIEWPORT_FOLLOW_MAX_SPEED
    s.recenter_speed = vbound(s.recenter_speed, 0L, 240L * 10000);
    return s;
}

// upstream's vbound for these 1/10000 pixel values
int64_t clamp64(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

// the lookahead the camera may hold: what the dead zone leaves of 120 / 80 pixels, so the camera
// still reaches the region's edges (upstream's bound_viewport_lookahead_x / _y)
int64_t bound_lookahead(int64_t v, int deadzone, int max_offset)
{
    const int64_t cap = (max_offset - deadzone / 2 > 0 ? max_offset - deadzone / 2 : 0) * Z;
    return clamp64(v, -cap, cap);
}

// Link's centre in region pixels (his screen's place in the region, outside one 0)
void link_centre(int64_t *x, int64_t *y)
{
    const int ox = g_reg.on ? g_reg.dx * 256 : 0, oy = g_reg.on ? g_reg.dy * 176 : 0;
    *x = (int64_t)Link.getX().v * Z / 65536 + (ox + 8) * Z;
    *y = (int64_t)Link.getY().v * Z / 65536 + (oy + 8) * Z;
}

// upstream's apply_viewport_follow and calculate_viewport (CenterAndBound): the focus pushed
// just enough to keep Link's centre (plus the lookahead) inside the dead zone, the viewport
// centred on it inside the region
void place_viewport()
{
    if(!g_reg.on)
    {
        g_cam_vx = g_cam_vy = 0;
        return;
    }

    const Follow s = follow_settings();
    int64_t tx, ty;
    link_centre(&tx, &ty);
    const int64_t ax = tx + bound_lookahead(g_fs.lx, s.deadzone_w, 120), ay = ty + bound_lookahead(g_fs.ly, s.deadzone_h, 80);
    g_fs.x = clamp64(g_fs.x, ax - (s.deadzone_w / 2) * Z, ax + (s.deadzone_w / 2) * Z);
    g_fs.y = clamp64(g_fs.y, ay - (s.deadzone_h / 2) * Z, ay + (s.deadzone_h / 2) * Z);
    g_cam_vx = vbound(z_int(g_fs.x) - 128, 0, g_reg.w * 256 - 256);
    g_cam_vy = vbound(z_int(g_fs.y) - 88, 0, g_reg.h * 176 - 176);
}

// upstream's reset_viewport_follow: the focus onto Link after he was put somewhere (a warp, the
// game's start, the end of a scroll, which keeps the lookahead it aimed with)
void reset_follow(bool keep_lookahead)
{
    const Follow s = follow_settings();
    link_centre(&g_fs.tx, &g_fs.ty);
    g_fs.idle = 0;

    if(!keep_lookahead)
        g_fs.lx = g_fs.ly = 0;

    g_fs.x = g_fs.tx + bound_lookahead(g_fs.lx, s.deadzone_w, 120);
    g_fs.y = g_fs.ty + bound_lookahead(g_fs.ly, s.deadzone_h, 80);
}

// The viewport for this frame: once the follow logic moved it (after Link moved), as it left it;
// before that, or in frames the game loop doesn't run (a scroll's, a warp's, stepping through a
// door), placed by Link where he is now, as upstream's update_viewport calls there do.
void camera_look()
{
    if(g_fs_reset)
    {
        reset_follow(false);
        g_fs_reset = false;
        g_vp_frame = -1;
    }

    if(g_vp_frame != frame)
    {
        place_viewport();
        g_vp_frame = frame;
    }
}


void forget_screens()
{
    for(std::map<int, RegionScreen *>::iterator it = g_screens.begin(); it != g_screens.end(); ++it)
        delete it->second;

    g_screens.clear();
    g_screen_state.clear();
    g_sprite_screen.clear();
    g_live = false;     // (their scripts and FFCs went with them)
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

// 2.53's hidden_entrance: the secret a trigger flag (inherent or placed) changes its combo to, -1 none
int secret_for_flag(int flag)
{
    switch(flag)
    {
    case mfBCANDLE: return sBCANDLE;
    case mfRCANDLE: return sRCANDLE;
    case mfWANDFIRE: return sWANDFIRE;
    case mfDINSFIRE: return sDINSFIRE;
    case mfARROW: return sARROW;
    case mfSARROW: return sSARROW;
    case mfGARROW: return sGARROW;
    case mfSBOMB: return sSBOMB;
    case mfBOMB: return sBOMB;
    case mfBRANG: return sBRANG;
    case mfMBRANG: return sMBRANG;
    case mfFBRANG: return sFBRANG;
    case mfWANDMAGIC: return sWANDMAGIC;
    case mfREFMAGIC: return sREFMAGIC;
    case mfREFFIREBALL: return sREFFIREBALL;
    case mfSWORD: return sSWORD;
    case mfWSWORD: return sWSWORD;
    case mfMSWORD: return sMSWORD;
    case mfXSWORD: return sXSWORD;
    case mfSWORDBEAM: return sSWORDBEAM;
    case mfWSWORDBEAM: return sWSWORDBEAM;
    case mfMSWORDBEAM: return sMSWORDBEAM;
    case mfXSWORDBEAM: return sXSWORDBEAM;
    case mfHOOKSHOT: return sHOOKSHOT;
    case mfWAND: return sWAND;
    case mfHAMMER: return sHAMMER;
    case mfSTRIKE: return sSTRIKE;
    }

    return -1;
}

// One screen (s, its own secret combos) of a copy with its secrets done, as 2.53's hidden_entrance
// changes it for a screen whose secrets were done before (the "Secrets" screen state): every
// trigger flag's combo (a misc. secret flag picks the secret), then the 16-31 flags, unless the
// screen keeps those for its enemies. Nothing else hidden_entrance does (sounds, enemies, timed
// warps) happens to a copy.
void copy_secret_combos(mapscr *s, bool high16)
{
    if(s->data.size() < 176 || s->sflag.size() < 176 || s->cset.size() < 176)
        return;

    for(int i = 0; i < 176; ++i)
    {
        int msflag = 0;

        if(s->sflag[i] >= mfSECRETS01 && s->sflag[i] <= mfSECRETS16)
            msflag = sSECRET01 + (s->sflag[i] - mfSECRETS01);
        else if(combobuf[s->data[i]].flag >= mfSECRETS01 && combobuf[s->data[i]].flag <= mfSECRETS16)
            msflag = sSECRET01 + (combobuf[s->data[i]].flag - mfSECRETS01);

        int newflag = -1;

        for(int iter = 0; iter < 2; ++iter)
        {
            int ft = secret_for_flag(iter ? s->sflag[i] : combobuf[s->data[i]].flag);

            if(ft < 0)
                continue;

            if(msflag)
                ft = msflag;

            s->data[i] = s->secretcombo[ft];
            s->cset[i] = s->secretcset[ft];
            newflag = s->secretflag[ft];
        }

        if(newflag > -1)
            s->sflag[i] = newflag;
    }

    if(!high16)
        return;

    for(int i = 0; i < 176; ++i)
    {
        int newflag = -1;

        for(int iter = 0; iter < 2; ++iter)
        {
            const int checkflag = iter ? s->sflag[i] : combobuf[s->data[i]].flag;

            if(checkflag > 15 && checkflag < 32)
            {
                s->data[i] = s->secretcombo[checkflag - 16 + 4];
                s->cset[i] = s->secretcset[checkflag - 16 + 4];
                newflag = s->secretflag[checkflag - 16 + 4];
            }
        }

        if(newflag > -1)
            s->sflag[i] = newflag;
    }
}

// The same for the copy's FFCs (inherent flags only).
void copy_secret_ffcs(mapscr *s, bool high16)
{
    for(int i = 0; i < 32; ++i)
    {
        const int ft = secret_for_flag(combobuf[s->ffdata[i]].flag);

        if(ft >= 0)
        {
            s->ffdata[i] = s->secretcombo[ft];
            s->ffcset[i] = s->secretcset[ft];
        }
    }

    if(!high16)
        return;

    for(int i = 0; i < 32; ++i)
    {
        const int checkflag = combobuf[s->ffdata[i]].flag;

        if(checkflag > 15 && checkflag < 32)
        {
            s->ffdata[i] = s->secretcombo[checkflag - 16 + 4];
            s->ffcset[i] = s->secretcset[checkflag - 16 + 4];
        }
    }
}

// 2.53's remove_screenstatecombos on a copy: opened lock blocks and chests
void copy_remove_state_combos(RegionScreen *rs, int what1, int what2)
{
    for(int j = -1; j < 6; ++j)
    {
        mapscr *s = j < 0 ? &rs->base : &rs->layers[j];

        if(j >= 0 && (!rs->base.layermap[j] || s->data.empty()))
            continue;

        for(int i = 0; i < 176 && i < (int)s->data.size(); ++i)
            if(combobuf[s->data[i]].type == what1 || combobuf[s->data[i]].type == what2)
                s->data[i]++;
    }
}

// A screen of the region the camera shows (and Link can walk into) before he has been on it in
// this stay: the quest's screen with what loadscr would do to it before it is shown (upstream
// loads every screen of a region at once, load_a_screen_and_layers_post): secrets done before,
// opened lock blocks and chests, 3.0's exstates, ex doors and combo position states, combos that
// cycle at screen init. (Not done for a copy: dungeon doors, level and global switch states.)
void apply_screen_state(int scr, RegionScreen *rs)
{
    const dword flags = game->maps[(g_reg.map * MAPSCRSNORMAL) + scr];
    mapscr *s = &rs->base;

    if(!isdungeon(currdmap, scr) && (flags & mSECRET))
    {
        // hiddenstair, then hidden_entrance for the "Secrets" screen state
        if((s->stairx || s->stairy) && s->secretcombo[sSTAIRS] && s->data.size() >= 176)
        {
            const int di = COMBOPOS(s->stairx, s->stairy);
            s->data[di] = s->secretcombo[sSTAIRS];
            s->cset[di] = s->secretcset[sSTAIRS];
            s->sflag[di] = s->secretflag[sSTAIRS];
        }

        const bool high16 = !(q30_compat_rule(Q30_QR_ENEMIES_SECRET_ONLY_16_31) && (s->flags2 & fCLEARSECRET)) || (s->flags4 & fENEMYSCRTPERM);
        copy_secret_combos(s, high16);

        for(int j = 0; j < 6; ++j)
            if(s->layermap[j] && !rs->layers[j].data.empty() && !rs->layers[j].cset.empty())
                copy_secret_combos(&rs->layers[j], high16);

        copy_secret_ffcs(s, high16);
    }

    if(flags & mLOCKBLOCK) copy_remove_state_combos(rs, cLOCKBLOCK, cLOCKBLOCK2);
    if(flags & mBOSSLOCKBLOCK) copy_remove_state_combos(rs, cBOSSLOCKBLOCK, cBOSSLOCKBLOCK2);
    if(flags & mCHEST) copy_remove_state_combos(rs, cCHEST, cCHEST2);
    if(flags & mLOCKEDCHEST) copy_remove_state_combos(rs, cLOCKEDCHEST, cLOCKEDCHEST2);
    if(flags & mBOSSCHEST) copy_remove_state_combos(rs, cBOSSCHEST, cBOSSCHEST2);

    // combos that cycle at screen init first, then the trigger states, in loadscr's order
    if(s->flags3 & fCYCLEONINIT)
    {
        for(int j = -1; j < 6; ++j)
        {
            mapscr *l = j < 0 ? s : &rs->layers[j];

            if(j >= 0 && (!s->layermap[j] || l->data.empty() || !get_bit(quest_rules, qr_CMBCYCLELAYERS)))
                continue;

            for(int i = 0; i < 176 && i < (int)l->data.size(); ++i)
                for(int r = 0; combobuf[l->data[i]].nextcombo != 0 && r < 10; ++r)
                {
                    const int c = l->data[i];
                    l->data[i] = combobuf[c].nextcombo;
                    l->cset[i] = combobuf[c].nextcset;
                }
        }
    }

    q30_trig_display_states(g_reg.map * MAPSCRSNORMAL + scr, s, rs->layers);
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

    apply_screen_state(scr, rs);
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
    g_fs_reset = true;
    g_handoff = false;
    g_vp_frame = -1;
}

extern std::map<int, std::pair<std::string, std::string> > ffcmap;
bool zasm30_screen_script_info(int index, int *script, long *initd);   // (q30_scripts_region.h)

// ZC_REGION_LIST=2 also lists each region screen's screen script and InitD, and its scripted FFCs
// (slot, script, place)
static void list_region_ffcs(int map, int ox, int oy, int w, int h)
{
    const char *list = getenv("ZC_REGION_LIST");

    if(!list || atoi(list) < 2)
        return;

    for(int y = oy; y < oy + h; ++y)
        for(int x = ox; x < ox + w; ++x)
        {
            const size_t index = (size_t)map * MAPSCRS + x + y * 16;

            if(index >= TheMaps.size())
                continue;

            int script = 0;
            long d[8];

            if(zasm30_screen_script_info((int)index, &script, d))
                al_trace("q30 regions:     screen 0x%02X: screen script %d InitD %ld %ld %ld %ld %ld %ld %ld %ld\n", x + y * 16, script,
                         d[0] / 10000, d[1] / 10000, d[2] / 10000, d[3] / 10000, d[4] / 10000, d[5] / 10000, d[6] / 10000, d[7] / 10000);

            const mapscr &m = TheMaps[index];

            for(int i = 0; i < 32; ++i)
            {
                if(!m.ffscript[i] && !m.ffdata[i])
                    continue;

                const int s = m.ffscript[i];
                std::map<int, std::pair<std::string, std::string> >::const_iterator it = ffcmap.find(s - 1);
                al_trace("q30 regions:     screen 0x%02X ffc %d: script %d %s combo %d at %ld,%ld flags 0x%lx\n", x + y * 16, i + 1, s,
                         (s && it != ffcmap.end()) ? it->second.second.c_str() : "", (int)m.ffdata[i], m.ffx[i] / 10000, m.ffy[i] / 10000,
                         (unsigned long)m.ffflags[i]);
            }
        }
}

// ZC_REGION_LIST=1 (tests): every map's regions, and the DMaps on those maps (once per game)
static void list_regions()
{
    const Follow &f = g_follow_init;
    al_trace("q30 regions: camera follow: dead zone %dx%d, lookahead %d,%d at %ld/10000, recenter %ld/10000 after %d\n", f.deadzone_w,
             f.deadzone_h, f.lookahead_x, f.lookahead_y, f.lookahead_speed, f.recenter_speed, f.recenter_delay);

    for(std::map<int, Follow>::const_iterator it = g_follow_dmap.begin(); it != g_follow_dmap.end(); ++it)
        al_trace("q30 regions:   DMap %d's own: dead zone %dx%d, lookahead %d,%d at %ld/10000, recenter %ld/10000 after %d\n", it->first,
                 it->second.deadzone_w, it->second.deadzone_h, it->second.lookahead_x, it->second.lookahead_y,
                 it->second.lookahead_speed, it->second.recenter_speed, it->second.recenter_delay);

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
                // (and how many of its screens are dark)
                int dark = 0;

                for(int y = oy; y < oy + h; ++y)
                    for(int x = ox; x < ox + w; ++x)
                    {
                        const size_t i = (size_t)map * MAPSCRS + x + y * 16;
                        dark += i < TheMaps.size() && (TheMaps[i].flags & fDARK) != 0;
                    }

                al_trace("q30 regions: map %d: %dx%d screens from 0x%02X (id %d)%s%s\n", map + 1, w, h, ox + oy * 16, region_id(map, scr),
                         map_valid(map) ? "" : " (map invalid)", dark ? ", dark screens" : "");
                shown = true;
                list_region_ffcs(map, ox, oy, w, h);
            }
        }

        if(shown)
            for(int d = 0; d < MAXDMAPS; ++d)
                if(DMaps[d].map == map && DMaps[d].name[0])
                    al_trace("q30 regions:   DMap %d \"%s\" xoff %d continue 0x%02X\n", d, DMaps[d].name, DMaps[d].xoff, DMaps[d].cont);
    }
}

void q30_region_screen_loaded(int destdmap)
{
    static const bool list = getenv("ZC_REGION_LIST") != NULL;

    if(list && !g_listed && q30_active())
    {
        g_listed = true;
        list_regions();
    }

    const Region old = g_reg;
    g_reg.on = false;

    // Link is put on this screen by whatever loads it (a warp, the game's start or a continue):
    // the camera snaps onto him once he is there. A scroll lines it up itself (q30_scroll_*), and
    // a hand-off inside a region is no jump at all.
    const bool handoff = g_handoff;
    g_handoff = false;

    if(!handoff && !screenscrolling)
        g_fs_reset = true;

    g_vp_frame = -1;

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
        forget_screens();

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

    // Link entered the region (or it was loaded again: a warp, a continue): all its other screens
    // load and run from now on ("every screen of a region live"), for the DMap this one loaded
    // for. A hand-off between them keeps them.
    if(!g_going_live)
        region_make_live(destdmap);
}

int q30_region_origin() { return g_reg.on ? g_reg.origin : currscr; }
int q30_region_id() { return g_reg.on ? region_id(g_reg.map, g_reg.origin) : 0; }
int q30_region_width() { return g_reg.on ? g_reg.w * 256 : 256; }
int q30_region_height() { return g_reg.on ? g_reg.h * 176 : 176; }
int q30_region_screens_wide() { return g_reg.on ? g_reg.w : 1; }
int q30_region_screens_high() { return g_reg.on ? g_reg.h : 1; }
int q30_region_offset_x() { return g_reg.on ? g_reg.dx * 256 : 0; }
int q30_region_offset_y() { return g_reg.on ? g_reg.dy * 176 : 0; }

// ---- what scripts see in a scrolling region (q30_regions.h) ---------------------------------

int q30_region_origin_x() { return g_reg.on ? g_reg.dx * 256 : 0; }
int q30_region_origin_y() { return g_reg.on ? g_reg.dy * 176 : 0; }

int q30_region_num_rpos() { return g_reg.on ? g_reg.w * g_reg.h * 176 : 176; }

// upstream's COMBO_AT: the point is clamped into the region (into the screen outside one), then
// COMBOPOS_REGION
int q30_region_rpos_at(int x, int y)
{
    const int w = q30_region_width(), h = q30_region_height();
    x = vbound(x, 0, w - 1);
    y = vbound(y, 0, h - 1);
    const int index = x / 256 + (y / 176) * (w / 256);
    return index * 176 + COMBOPOS(x % 256, y % 176);
}

// upstream's COMBOXY_REGION
void q30_region_rpos_xy(int rpos, int *x, int *y)
{
    const int sw = q30_region_screens_wide();
    const int index = rpos / 176, pos = rpos % 176;
    *x = (index % sw) * 256 + (pos & 15) * 16;
    *y = (index / sw) * 176 + (pos & 0xF0);
}

int q30_region_rpos_of_pos(int pos)
{
    return g_reg.on ? (g_reg.dx + g_reg.dy * g_reg.w) * 176 + pos : pos;
}

mapscr *q30_region_rpos_screen(int rpos, int layer, int *pos, bool *current)
{
    *current = false;

    if(layer < 0 || layer > 6 || rpos < 0 || rpos >= q30_region_num_rpos())
        return NULL;

    *pos = rpos % 176;
    const int index = rpos / 176;
    const int ix = g_reg.on ? index % g_reg.w : 0, iy = g_reg.on ? index / g_reg.w : 0;

    if(!g_reg.on || (ix == g_reg.dx && iy == g_reg.dy))
    {
        *current = true;
        return layer ? &tmpscr2[layer - 1] : tmpscr;
    }

    RegionScreen *rs = region_screen(g_reg.ox + ix + (g_reg.oy + iy) * 16);

    if(!rs)
        return NULL;

    if(!layer)
        return &rs->base;

    return rs->base.layermap[layer - 1] > 0 ? &rs->layers[layer - 1] : NULL;
}

int q30_region_screen_at(int lx, int ly)
{
    if(!g_reg.on)
        return currscr;

    const int x = vbound(lx + g_reg.dx * 256, 0, g_reg.w * 256 - 1), y = vbound(ly + g_reg.dy * 176, 0, g_reg.h * 176 - 1);
    return g_reg.ox + x / 256 + (g_reg.oy + y / 176) * 16;
}


void q30_region_camera(int, int, int *cam_x, int *cam_y)
{
    *cam_x = *cam_y = 0;

    if(!g_reg.on)
        return;

    camera_look();
    *cam_x = g_cam_vx - g_reg.dx * 256;
    *cam_y = g_cam_vy - g_reg.dy * 176;
}

void q30_set_viewport_follow(int dmap, bool custom, int deadzone_w, int deadzone_h, int lookahead_x, int lookahead_y,
                             long lookahead_speed, long recenter_speed, int recenter_delay)
{
    const Follow f = { deadzone_w, deadzone_h, lookahead_x, lookahead_y, lookahead_speed, recenter_speed, recenter_delay };

    if(dmap < 0)
        g_follow_init = f;
    else if(custom)
        g_follow_dmap[dmap] = f;
    else
        g_follow_dmap.erase(dmap);
}

// upstream's tick_viewport_follow, run by the game loop once a frame after Link moved (not while
// a scroll runs): the lookahead eases toward its distance the way Link moves (moving against the
// way he faces doesn't count), the focus recentres on him after he has stood still long enough,
// and the viewport is placed. It runs on screens outside regions too (as upstream's), so the
// lookahead built walking there carries into a region a scroll enters.
void q30_region_tick_camera()
{
    if(!q30_active())
        return;

    if(g_fs_reset)
    {
        reset_follow(false);
        g_fs_reset = false;
    }

    const Follow s = follow_settings();
    int64_t tx, ty;
    link_centre(&tx, &ty);
    int64_t dx = tx - g_fs.tx, dy = ty - g_fs.ty;
    g_fs.tx = tx;
    g_fs.ty = ty;
    g_fs.idle = (!dx && !dy) ? (g_fs.idle < (1 << 20) ? g_fs.idle + 1 : g_fs.idle) : 0;

    // the horizontal and vertical part of each direction (upstream's xDir / yDir)
    static const int xd[8] = { -1, -1, left, right, left, right, left, right };
    static const int yd[8] = { up, down, -1, -1, up, up, down, down };
    const int d = Link.getDir();

    if(d >= 0 && d < 8)
    {
        if((xd[d] == left && dx > 0) || (xd[d] == right && dx < 0)) dx = 0;
        if((yd[d] == up && dy > 0) || (yd[d] == down && dy < 0)) dy = 0;
    }

    g_fs.lx = bound_lookahead(g_fs.lx, s.deadzone_w, 120);
    g_fs.ly = bound_lookahead(g_fs.ly, s.deadzone_h, 80);

    const bool moving = dx || dy;
    const int64_t speed = s.lookahead_speed;

    for(int axis = 0; axis < 2; ++axis)
    {
        int64_t &offset = axis ? g_fs.ly : g_fs.lx;
        const int64_t delta = axis ? dy : dx;
        const int lookahead = axis ? s.lookahead_y : s.lookahead_x;
        // standing still holds the offset; no lookahead eases it away
        const int64_t want = !lookahead ? 0 : !moving ? offset
                             : bound_lookahead((delta < 0 ? -lookahead : delta > 0 ? lookahead : 0) * Z,
                                               axis ? s.deadzone_h : s.deadzone_w, axis ? 80 : 120);
        offset += clamp64(want - offset, -speed, speed);
    }

    const int64_t rs = s.recenter_speed;

    if(rs > 0 && g_fs.idle > s.recenter_delay)
    {
        g_fs.x += clamp64(g_fs.tx - g_fs.x, -rs, rs);
        g_fs.y += clamp64(g_fs.ty - g_fs.y, -rs, rs);
    }

    place_viewport();
    g_vp_frame = frame;
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
    // live screens: the FFCs of every screen of the region, wherever they are ("every screen of a
    // region live", below), placed as this screen's are (q30_draw_cam_x/y)
    if(which == Q30_LOWER_FFCS && region_draw_ffcs(lower, -3, q30_draw_cam_x, q30_draw_cam_y))
        return;

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

    // live screens: the overhead FFCs of every screen of the region, wherever they are
    if(type == -4 && region_draw_ffcs(bmp, -4, cx, cy))
        return;

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

int q30_region_view_screens(Q30ViewScreen *out, int extra)
{
    out[0].ox = out[0].oy = 0;
    out[0].base = &tmpscr[0];
    out[0].layers = tmpscr2;
    int n = 1;

    if(!g_reg.on || q30_scroll_on())
        return n;

    int cx, cy;
    q30_region_camera(0, 0, &cx, &cy);

    for(int sdy = -2; sdy <= 2; ++sdy)
        for(int sdx = -2; sdx <= 2 && n < Q30_VIEW_SCREENS; ++sdx)
        {
            int scr;

            if((!sdx && !sdy) || !overlaps(sdx * 256, sdx * 256 + 256, cx - extra, cx + 256 + extra)
               || !overlaps(sdy * 176, sdy * 176 + 176, cy - extra, cy + 176 + extra) || !in_region(sdx, sdy, &scr))
                continue;

            RegionScreen *rs = region_screen(scr);

            if(!rs)
                continue;

            out[n].ox = sdx * 256;
            out[n].oy = sdy * 176;
            out[n].base = &rs->base;
            out[n].layers = rs->layers;
            ++n;
        }

    return n;
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
        // the script's coordinates are region pixels (q30_region_origin_x/y), so the viewport in
        // region pixels comes off, as upstream's xoff - viewport.x
        dx = -cx - q30_region_origin_x();
        dy = -cy - q30_region_origin_y();
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

    g_handoff = true;   // the next screen loading is the hand-off: the camera goes on as it is

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

// ---- every screen of a region live: FFCs and scripts (q30_regions.h) --------------------------
// Upstream's loadscr loads all the screens of a scrolling region and every frame runs all their
// FFCs and scripts. Here each screen of the region other than the current one has a slot in
// g_screens, loaded when Link enters the region (region_make_live), with what runs on it: its
// combos and FFCs (base, layers), the script engine's state for it (FFC, screen and combo
// scripts), its triggers' state and its FFC changers'. To run a part of a frame on it, it stands
// in for the current screen: its slot and the globals change places (swap_current), and currscr,
// its place in the region and the positions of Link and the sprites go with it, so scripts and
// the 2.53 code see it as the current screen. Then it all goes back.

#include "q30.h"

void zasm30_screen_state_swap(void *state);             // ffscript.cpp (q30_scripts_region.h)
bool zasm30_screen_state_busy(void *state, int what);
bool zasm30_screen_has_script(int index);
void q30_slopes_update();                               // q30_triggers.cpp

extern short ffposx[32], ffposy[32];                    // maps.cpp: the FFC changer state
extern long ffprvx[32], ffprvy[32];
extern sprite_list chainlinks, decorations, particles;

namespace
{

bool g_loading_other = false;   // a screen of the region loads beside the current one

// While another screen of the region stands in for the current one (q30_region_each_screen,
// region_make_live): the screen Link is on, whose slot holds it meanwhile (q30_region_hero_scr).
// -1 when none stands in.
int g_hero_slot = -1;

// Positions local to the current screen become local to the screen (dx, dy) pixels from it the
// other way: Link, the sprites and the moving push block.
void shift_world(int dx, int dy)
{
    if(!dx && !dy)
        return;

    Link.x += dx;
    Link.y += dy;
    sprite_list *lists[] = { &guys, &items, &Lwpns, &Ewpns, &chainlinks, &decorations, &particles };

    for(int k = 0; k < 7; ++k)
        for(int i = 0; i < lists[k]->Count(); ++i)
        {
            lists[k]->spr(i)->x += dx;
            lists[k]->spr(i)->y += dy;
        }

    mblock2.x += dx;
    mblock2.y += dy;
}

// Region screen scr (a live slot) and the current screen change places: scr's slot comes into
// the globals (tmpscr, tmpscr2, the FFC changers, the scripts' and triggers' state) and becomes
// the current screen, and the current screen goes into that slot, filed under its own number.
// shift: Link and the sprites move into scr's coordinates too (a hand-off moves them itself).
bool swap_current(int scr, bool shift)
{
    std::map<int, RegionScreen *>::iterator it = g_screens.find(scr);

    if(it == g_screens.end() || !it->second->live)
    {
        al_trace("q30 region: no live slot for screen 0x%02X (current 0x%02X)\n", scr, currscr);
        return false;
    }

    RegionScreen *rs = it->second;
    g_screens.erase(it);
    const int from = currscr;

    std::swap(tmpscr[0], rs->base);

    for(int i = 0; i < 6; ++i)
        std::swap(tmpscr2[i], rs->layers[i]);

    for(int i = 0; i < 32; ++i)
    {
        std::swap(ffposx[i], rs->ffposx[i]);
        std::swap(ffposy[i], rs->ffposy[i]);
        std::swap(ffprvx[i], rs->ffprvx[i]);
        std::swap(ffprvy[i], rs->ffprvy[i]);
    }

    zasm30_screen_state_swap(rs->scripts);
    q30_trig_state_swap(rs->trig);

    // (a copy the hand-off kept of the screen Link leaves, for drawing, gives way to its slot)
    std::map<int, RegionScreen *>::iterator old = g_screens.find(from);

    if(old != g_screens.end())
    {
        delete old->second;
        g_screens.erase(old);
    }

    g_screens[from] = rs;
    currscr = homescr = scr;

    const int sdx = (scr % 16 - g_reg.ox) - g_reg.dx, sdy = (scr / 16 - g_reg.oy) - g_reg.dy;
    g_reg.dx += sdx;
    g_reg.dy += sdy;

    if(shift)
        shift_world(-sdx * 256, -sdy * 176);

    return true;
}

// whether region screen scr (its slot) has anything to do for `what`
bool has_work(const RegionScreen *rs, int scr, int what)
{
    const mapscr &m = rs->base;

    switch(what)
    {
    case Q30_EACH_SCREEN_SCRIPT:
        return zasm30_screen_has_script(g_reg.map * MAPSCRS + scr);

    case Q30_EACH_FFC_SCRIPTS:
    case Q30_EACH_FFC_PRELOAD:
        for(int i = 0; i < 32; ++i)
            if(m.ffscript[i] && (what == Q30_EACH_FFC_SCRIPTS || (m.ffflags[i] & ffPRELOAD)))
                return true;

        return false;

    case Q30_EACH_FFC_MOVE:
        for(int i = 0; i < 32; ++i)
            if(m.ffdata[i] && !(m.ffflags[i] & (ffCHANGER | ffSTATIONARY)))
                return true;

        return false;

    case Q30_EACH_COMBO_CYCLE:
        if(q30_trig_screen_busy(&m, rs->layers))
            return true;

        for(int i = 0; i < 32; ++i)
            if(m.ffdata[i] && combobuf[m.ffdata[i]].nextcombo)
                return true;

        for(int layer = 0; layer < (get_bit(quest_rules, qr_CMBCYCLELAYERS) ? 7 : 1); ++layer)
        {
            const mapscr &l = layer ? rs->layers[layer - 1] : m;

            if(layer && l.data.empty())
                continue;

            for(int pos = 0; pos < 176 && pos < (int)l.data.size(); ++pos)
                if(combobuf[l.data[pos]].nextcombo)
                    return true;
        }

        return false;

    case Q30_EACH_COMBO_SCRIPTS:
        for(int layer = 0; layer < 7; ++layer)
        {
            const mapscr &l = layer ? rs->layers[layer - 1] : m;

            if(layer && !l.valid)
                continue;

            for(int pos = 0; pos < 176 && pos < (int)l.data.size(); ++pos)
            {
                const Q30ScriptRef *ref = q30_combo_script(l.data[pos]);

                if(ref && ref->script)
                    return true;
            }
        }

        return false;
    }

    return zasm30_screen_state_busy(rs->scripts, what);     // the Waitdraw halves
}

// Link entered the region, or it loaded again (a warp or a continue into it): every screen of it
// other than the current one gets a slot, loaded as loadscr loads the current screen (permanent
// secrets, lock blocks, chests, doors, combos cycled at init, its FFCs from the start, its
// ScreenLoad triggers), with it standing in for the current screen. Its FFC scripts first run
// with the region's (the preload after the load, then each frame). destdmap: the DMap the
// current screen loaded for (-1: currdmap); a scroll into another DMap sets currdmap only after
// the scroll, and the doors of the other screens are a dungeon's or not by that DMap.
void region_make_live(int destdmap)
{
    forget_screens();

    if(!g_reg.on || !q30_active())
        return;

    const int here = currscr, home = homescr;
    const int dmap = destdmap >= 0 ? destdmap : currdmap;

    for(int y = 0; y < g_reg.h; ++y)
        for(int x = 0; x < g_reg.w; ++x)
        {
            const int scr = g_reg.ox + x + (g_reg.oy + y) * 16;

            if(scr == here || (size_t)g_reg.map * MAPSCRS + scr >= TheMaps.size())
                continue;

            RegionScreen *rs = new RegionScreen;
            rs->live = true;
            rs->scripts = zasm30_screen_state_new(&rs->base);
            rs->trig = q30_trig_state_new();
            g_screens[scr] = rs;

            if(!swap_current(scr, true))
                continue;

            // (a shutter on it doesn't shut the current screen's doors; and while a scroll brings
            // Link in, putdoor opens the shutter on the side he comes in by: only the current
            // screen's, so the others load as if no scroll were going on)
            const int keep_opendoors = opendoors;
            const bool keep_scrolling = screenscrolling;
            const int keep_hero_slot = g_hero_slot;
            screenscrolling = false;
            g_hero_slot = here;
            g_loading_other = true;
            loadscr(0, dmap, scr, -1, false);
            g_loading_other = false;
            g_hero_slot = keep_hero_slot;
            screenscrolling = keep_scrolling;
            opendoors = keep_opendoors;

            swap_current(here, true);
            homescr = home;
        }

    g_live = true;

    if(getenv("ZC_EVENTLOG"))
        al_trace("q30 region: %d other screens of the region are live (DMap %d)\n", (int)g_screens.size(), dmap);
}

// the FFCs (type -3 or -4) of every live screen of the region, the current screen's own drawn
// with do_layer offsets (x, y)
bool region_draw_ffcs(BITMAP *bmp, int type, int x, int y)
{
    if(!g_live || !g_reg.on)
        return false;

    for(std::map<int, RegionScreen *>::iterator it = g_screens.begin(); it != g_screens.end(); ++it)
    {
        if(!it->second->live)
            continue;

        const int sdx = (it->first % 16 - g_reg.ox) - g_reg.dx, sdy = (it->first / 16 - g_reg.oy) - g_reg.dy;
        do_layer(bmp, type, &it->second->base, x - sdx * 256, y - sdy * 176, 3);
    }

    return true;
}

} // namespace

bool q30_region_loading_other()
{
    return g_loading_other;
}

mapscr *q30_region_hero_scr()
{
    if(g_hero_slot >= 0)
    {
        std::map<int, RegionScreen *>::iterator it = g_screens.find(g_hero_slot);

        if(it != g_screens.end())
            return &it->second->base;
    }

    return tmpscr;
}

// ZC_REGION_FFCLOG=file (tests): after the FFCs moved, every FFC of the region each frame, in
// region pixels: frame, screen, slot, x, y, combo, script
static void log_region_ffcs()
{
    static const char *path = getenv("ZC_REGION_FFCLOG");
    static FILE *f = path ? fopen(path, "w") : NULL;

    if(!f)
        return;

    for(int y = 0; y < g_reg.h; ++y)
        for(int x = 0; x < g_reg.w; ++x)
        {
            const int scr = g_reg.ox + x + (g_reg.oy + y) * 16;
            const mapscr *m = q30_region_ffc_table(scr);

            for(int i = 0; m && i < 32; ++i)
                if(m->ffdata[i] || m->ffscript[i])
                    fprintf(f, "%d %02X %d %ld %ld %d %d\n", frame, scr, i + 1, m->ffx[i] / 10000 + x * 256, m->ffy[i] / 10000 + y * 176,
                            (int)m->ffdata[i], (int)m->ffscript[i]);
        }

    fflush(f);
}

bool q30_region_each_screen(int what, void (*fn)(int), int arg)
{
    if(!g_live || !g_reg.on)
        return false;

    const int here = currscr;
    const Region reg = g_reg;

    // row by row from the region's top-left, as upstream's for_every_base_screen_in_region
    for(int y = 0; y < reg.h; ++y)
        for(int x = 0; x < reg.w; ++x)
        {
            const int scr = reg.ox + x + (reg.oy + y) * 16;

            // (something run left the region, or the current screen changed: stop)
            if(!g_live || currscr != here)
                return true;

            if(scr == here)
            {
                fn(arg);
                continue;
            }

            std::map<int, RegionScreen *>::iterator it = g_screens.find(scr);

            if(it == g_screens.end() || !it->second->live || !has_work(it->second, scr, what))
                continue;

            q30_region_tag_sprites();       // the sprites out now are the current screen's
            const int home = homescr;
            const int keep_hero_slot = g_hero_slot;
            swap_current(scr, true);
            g_hero_slot = here;             // (Link's screen waits in its slot: q30_region_hero_scr)
            fn(arg);

            // (another screen's combo triggers run where the current screen's q30_trig_update does)
            if(what == Q30_EACH_COMBO_CYCLE)
                q30_trig_update_other();

            q30_region_tag_sprites();       // the ones its scripts made are its own
            g_hero_slot = keep_hero_slot;
            swap_current(here, true);
            homescr = home;
        }

    if(what == Q30_EACH_FFC_MOVE)
        log_region_ffcs();

    return true;
}

bool q30_region_go_live(int scr)
{
    if(!g_live || !g_reg.on || !swap_current(scr, false))
        return false;

    // the region's place of the new current screen, and what follows a change of screen, with
    // the region's screens left as they are
    g_going_live = true;
    q30_region_screen_loaded();
    g_going_live = false;
    q30_slopes_update();

    if(getenv("ZC_EVENTLOG"))
        al_trace("q30 region: screen 0x%02X goes on live\n", currscr);

    return true;
}

int q30_region_screen_index(int scr)
{
    if(!g_reg.on)
        return scr == currscr ? 0 : -1;

    const int x = scr % 16 - g_reg.ox, y = scr / 16 - g_reg.oy;

    if(scr < 0 || scr >= 128 || x < 0 || y < 0 || x >= g_reg.w || y >= g_reg.h)
        return -1;

    return x + y * g_reg.w;
}

int q30_region_index_screen(int index)
{
    if(!g_reg.on)
        return index == 0 ? currscr : -1;

    if(index < 0 || index >= g_reg.w * g_reg.h)
        return -1;

    return g_reg.ox + index % g_reg.w + (g_reg.oy + index / g_reg.w) * 16;
}

void q30_region_screen_pixels(int scr, int *x, int *y)
{
    *x = *y = 0;

    if(!g_reg.on || q30_region_screen_index(scr) < 0)
        return;

    *x = (scr % 16 - g_reg.ox) * 256;
    *y = (scr / 16 - g_reg.oy) * 176;
}

mapscr *q30_region_ffc_table(int scr)
{
    if(scr == currscr)
        return tmpscr;

    if(!g_live || !g_reg.on)
        return NULL;

    std::map<int, RegionScreen *>::iterator it = g_screens.find(scr);
    return (it != g_screens.end() && it->second->live) ? &it->second->base : NULL;
}

void *q30_region_screen_scripts(int scr)
{
    if(scr == currscr || !g_live || !g_reg.on)
        return NULL;

    std::map<int, RegionScreen *>::iterator it = g_screens.find(scr);
    return (it != g_screens.end() && it->second->live) ? it->second->scripts : NULL;
}

int q30_region_ffcombo_at(int x, int y)
{
    if(!g_live || !g_reg.on)
        return 0;

    for(std::map<int, RegionScreen *>::iterator it = g_screens.begin(); it != g_screens.end(); ++it)
    {
        if(!it->second->live)
            continue;

        const mapscr &m = it->second->base;
        const int px = x - ((it->first % 16 - g_reg.ox) - g_reg.dx) * 256, py = y - ((it->first / 16 - g_reg.oy) - g_reg.dy) * 176;

        // (maps.cpp's ffcIsAt on that screen's FFCs, in its coordinates)
        for(int i = 0; i < 32; ++i)
        {
            const int fx = m.ffx[i] / 10000, fy = m.ffy[i] / 10000;

            if(px >= fx && px <= fx + (m.ffwidth[i] & 63) && py >= fy && py <= fy + (m.ffheight[i] & 63)
               && !(m.ffflags[i] & (ffCHANGER | ffETHEREAL)))
                return m.ffdata[i];
        }
    }

    return 0;
}

// upstream's solid FFCs block the Hero anywhere in the region (q30_ffc_solid_at in link.cpp does
// the current screen's)
bool q30_region_ffc_solid_at(int x, int y, int cnt)
{
    if(!g_live || !g_reg.on)
        return false;

    for(std::map<int, RegionScreen *>::iterator it = g_screens.begin(); it != g_screens.end(); ++it)
    {
        if(!it->second->live)
            continue;

        const mapscr &m = it->second->base;
        const int ox = ((it->first % 16 - g_reg.ox) - g_reg.dx) * 256, oy = ((it->first / 16 - g_reg.oy) - g_reg.dy) * 176;

        for(int i = 0; i < 32; ++i)
        {
            if(!(m.ffflags[i] & ffSOLID) || (m.ffflags[i] & ffCHANGER))
                continue;

            const int fx = m.ffx[i] / 10000 + ox, fy = m.ffy[i] / 10000 + oy;
            const int fw = (m.ffwidth[i] & 0x3F) + 1, fh = (m.ffheight[i] & 0x3F) + 1;

            for(int k = 0; k < cnt; ++k)
            {
                const int px = x + k * 8;

                if(px >= fx && px < fx + fw && y >= fy && y < fy + fh)
                    return true;
            }
        }
    }

    return false;
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
        camera_look();
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
        // aimed where the follow logic will take over after the scroll: Link's centre plus the
        // lookahead it carries through (upstream's calc_new_viewport_and_pos)
        const Follow f = follow_settings();
        const int64_t ax = (int64_t)(ndx * 256 + nlx + 8) * Z + bound_lookahead(g_fs.lx, f.deadzone_w, 120);
        const int64_t ay = (int64_t)(ndy * 176 + nly + 8) * Z + bound_lookahead(g_fs.ly, f.deadzone_h, 80);
        new_vx = vbound(z_int(ax) - 128, 0, g_reg.w * 256 - 256);
        new_vy = vbound(z_int(ay) - 88, 0, g_reg.h * 176 - 176);
        g_cam_vx = new_vx;
        g_cam_vy = new_vy;
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

int q30_scroll_view_screens(Q30ViewScreen *out, int sx, int sy, int *vx, int *vy, int *lx, int *ly)
{
    if(!g_scroll.on)
        return 0;

    const Scroll &s = g_scroll;
    int ox, oy;
    q30_scroll_offset(&ox, &oy);

    // the old and the new screen's top-left on the view (scrollscr's picture through the view)
    const int obx = s.obx - sx - ox, oby = s.oby - sy - oy;
    const int nbx = s.nbx - sx - ox, nby = s.nby - sy - oy;
    int n = 0;

    out[n].ox = obx;
    out[n].oy = oby;
    out[n].base = &tmpscr[1];
    out[n].layers = tmpscr3;
    ++n;
    out[n].ox = nbx;
    out[n].oy = nby;
    out[n].base = &tmpscr[0];
    out[n].layers = tmpscr2;
    ++n;

    // the screens beside them that the view leans over (q30_scroll_blit_lower draws them)
    for(int isnew = 0; isnew < 2; ++isnew)
        for(int k = 0; k < 2; ++k)
        {
            RegionScreen *rs = scroll_shows(k) ? scroll_neighbour(isnew != 0, k) : NULL;

            if(!rs)
                continue;

            const int step = k ? 1 : -1;
            out[n].ox = (isnew ? nbx : obx) + (s.horiz ? 0 : step * 256);
            out[n].oy = (isnew ? nby : oby) + (s.horiz ? step * 176 : 0);
            out[n].base = &rs->base;
            out[n].layers = rs->layers;
            ++n;
        }

    *vx = -nbx;
    *vy = -nby;
    *lx = int(Link.getX()) - ox;    // (scrollscr draws Link at his x, y through the view's offset)
    *ly = int(Link.getY()) - oy;
    return n;
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

    // the follow logic goes on from where the scroll left Link, with the lookahead the scroll
    // aimed with (upstream's reset_viewport_follow(true) at the end of scrollscr)
    reset_follow(true);
    g_fs_reset = false;
    g_vp_frame = -1;
    q30_scroll_reset();
}

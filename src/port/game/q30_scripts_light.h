// Dithering, script-drawn lights and sprite glow for 2.55 and 3.0 quests, after upstream's
// drawing.cpp (ditherblit, dithercircfill, lampcone, doDarkroomCircle/Square/Cone) and
// sprite::handle_sprlighting. Included once, from q30_scripts_cmds.h.
//
// The dark room itself is drawn by q30_draw_darkness (q30_triggers.cpp) from a 256x176 mask in
// playing-field coordinates: 1 dark, 2 half dark (upstream's translucent ring), 0 lit. The lights
// here are added to that mask by q30_script_lights.

#ifndef ZC_Q30_SCRIPTS_LIGHT_H
#define ZC_Q30_SCRIPTS_LIGHT_H

#include <vector>

// ---- dither patterns ---------------------------------------------------------------------------
// Upstream's dithType: every odd type is the inverse of the one before it.
enum
{
    Z30_DITH_CHECKER = 0, Z30_DITH_CRISSCROSS = 2, Z30_DITH_DIAG_ULDR = 4, Z30_DITH_DIAG_URDL = 6,
    Z30_DITH_ROW = 8, Z30_DITH_COL = 10, Z30_DITH_DOTS = 12, Z30_DITH_GRID = 14, Z30_DITH_STATIC = 16,
    Z30_DITH_STATIC2 = 18, Z30_DITH_STATIC3 = 20, Z30_DITH_DOTS2 = 22, Z30_DITH_DOTS3 = 24,
    Z30_DITH_DOTS4 = 26, Z30_DITH_MAX = 28
};

struct Z30Dither
{
    int type, arg, wid, hei;
    bool inv;
    double filt;

    Z30Dither(int dtype, int darg, int w, int h)
    {
        inv = (dtype % 2) != 0;
        type = dtype & ~1;
        arg = darg;
        if(type != Z30_DITH_STATIC && type != Z30_DITH_STATIC2 && type != Z30_DITH_STATIC3)
            arg = zc_max(1, arg);
        filt = arg / 255.0 * 2.0;
        wid = w;
        hei = h;
    }

    static bool staticcheck(int x, int y, double filt)
    {
        const double s = sin((double)((x * double(x)) + (y * double(y))));
        const double c = cos(double(x) * y);
        return fabs(s - c) < filt;
    }

    bool raw(int x, int y) const
    {
        switch(type)
        {
        case Z30_DITH_CHECKER: return (((x / arg) & 1) ^ ((y / arg) & 1)) != 0;
        case Z30_DITH_CRISSCROSS: return ((x % arg) == (y % arg)) || ((arg - (x % arg)) == (y % arg));
        case Z30_DITH_DIAG_ULDR: return (x % arg) == (y % arg);
        case Z30_DITH_DIAG_URDL: return ((arg - 1) - (x % arg)) == (y % arg);
        case Z30_DITH_ROW: return !(y % arg);
        case Z30_DITH_COL: return !(x % arg);
        case Z30_DITH_DOTS: return !(x % arg || y % arg);
        case Z30_DITH_GRID: return !(x % arg) || !(y % arg);
        case Z30_DITH_STATIC: return staticcheck(x, y, filt);
        case Z30_DITH_STATIC2: return staticcheck(x - (wid / 2), y - (hei / 2), filt);
        case Z30_DITH_STATIC3: return staticcheck(x + (wid / 2), y + (hei / 2), filt);
        case Z30_DITH_DOTS2:
        {
            const int a2 = (arg + 1) * 2;
            return !(x % a2 || y % a2) || ((x % a2 == arg + 1) && (y % a2 == arg + 1));
        }
        case Z30_DITH_DOTS3:
        {
            const int a2 = (arg + 1) * 2;
            return !(x % a2 || y % 2) || ((x % a2 == arg + 1) && (y % 2 == 1));
        }
        case Z30_DITH_DOTS4:
        {
            const int a2 = (arg + 1) * 2;
            return !(x % 2 || y % a2) || ((x % 2 == 1) && (y % a2 == arg + 1));
        }
        default: return true;
        }
    }

    bool check(int x, int y) const { return raw(x, y) != inv; }
};

// Upstream's dither_offx/offy: the pattern is anchored to the region's origin, which for a 2.53
// screen is the screen's place on its map. While a dark room's mask is built through a scrolling
// region's camera, the mask's top-left is the view's: z30_dark_view_x/y from the current
// screen's top-left (0 otherwise).
static int z30_dark_view_x = 0, z30_dark_view_y = 0;
static int z30_dither_offx() { return (currscr % 16) * 256 + z30_dark_view_x; }
static int z30_dither_offy() { return (currscr / 16) * 176 + z30_dark_view_y; }

// ditherblit: every pixel of dest where src (if any) is set and the pattern passes becomes color.
static void z30_ditherblit(BITMAP *dest, BITMAP *src, int color, int dtype, int darg, int xoffs, int yoffs)
{
    int wid = src ? zc_min(dest->w, src->w) : dest->w;
    int hei = src ? zc_min(dest->h, src->h) : dest->h;
    int sx = 0, sy = 0;

    xoffs += z30_dither_offx();
    yoffs += z30_dither_offy();

    if(dest->clip)
    {
        wid = zc_min(dest->cr, wid);
        hei = zc_min(dest->cb, hei);
        sx = zc_max(dest->cl, sx);
        sy = zc_max(dest->ct, sy);
    }

    const Z30Dither d(dtype, darg, wid, hei);

    for(int ty = sy; ty < hei; ++ty)
    {
        const byte *read = src ? (const byte *)src->line[ty] : NULL;
        byte *write = (byte *)dest->line[ty];

        for(int tx = sx; tx < wid; ++tx)
            if((!read || read[tx]) && d.check(tx + xoffs, ty + yoffs))
                write[tx] = (byte)color;
    }
}

// ---- light shapes ------------------------------------------------------------------------------

// Upstream's lampcone polygon for each direction, in 66ths of the cone's range. The orthogonal
// cones start from the apex and end at a point given from it again (the last pair); the
// diagonal ones are steps from the apex all the way round.
static void z30_lampcone_points(int sx, int sy, int range, int dir, std::vector<int> &pts)
{
    static const signed char up[10][2] = { { -17, -8 }, { -16, -31 }, { 0, -4 }, { 8, -15 }, { 15, -8 },
                                           { 19, 0 }, { 15, 8 }, { 8, 15 }, { 0, 4 }, { 17, -8 } };
    static const signed char lup[16][2] = { { 6, -16 }, { 0, -3 }, { -4, -12 }, { -6, -18 }, { -1, -1 },
                                            { -14, -4 }, { -6, 0 }, { -18, 4 }, { -12, 12 }, { -4, 18 },
                                            { 0, 6 }, { 4, 14 }, { 1, 1 }, { 18, 6 }, { 12, 4 }, { 3, 0 } };

    #define Z30_STEP(k) ((int)ceil(((k) < 0 ? -(k) : (k)) / 66.0 * range) * ((k) < 0 ? -1 : 1))

    pts.clear();
    pts.push_back(sx);
    pts.push_back(sy);

    if(dir < 4)
    {
        // up as given; down mirrors y; left and right swap the axes
        int x = sx, y = sy;

        for(int i = 0; i < 10; ++i)
        {
            int a = up[i][0], b = up[i][1];
            int dx = (dir == left || dir == right) ? b : a;
            int dy = (dir == left || dir == right) ? a : b;

            if(dir == down) dy = -dy;
            if(dir == right) dx = -dx;

            if(i == 0 || i == 9)
            {
                x = sx + Z30_STEP(dx);
                y = sy + Z30_STEP(dy);
            }
            else
            {
                x += Z30_STEP(dx);
                y += Z30_STEP(dy);
            }

            pts.push_back(x);
            pts.push_back(y);
        }
    }
    else
    {
        const int xs = (dir == r_up || dir == r_down) ? -1 : 1;
        const int ys = (dir == l_down || dir == r_down) ? -1 : 1;
        int x = sx, y = sy;

        for(int i = 0; i < 16; ++i)
        {
            x += Z30_STEP(lup[i][0] * xs);
            y += Z30_STEP(lup[i][1] * ys);
            pts.push_back(x);
            pts.push_back(y);
        }
    }

    #undef Z30_STEP
}

// One shape filled with color: 0 circle, 1 cone, 2 square.
static void z30_light_shape(BITMAP *b, int shape, int cx, int cy, int rad, int dir, int color)
{
    if(rad <= 0)
        return;

    switch(shape)
    {
    case 1:
    {
        std::vector<int> pts;
        z30_lampcone_points(cx, cy, rad, dir, pts);
        polygon(b, (int)pts.size() / 2, &pts[0], color);
        break;
    }
    case 2:
        rectfill(b, cx - rad, cy - rad, cx + rad, cy + rad, color);
        break;
    default:
        circlefill(b, cx, cy, rad, color);
        break;
    }
}

// Lighting defaults from the quest's init data (q255_init.cpp), as upstream's zinit has them.
struct Z30LightInit
{
    int dith_type, dith_arg, dith_perc, light_rad, trans_perc, dark_color;
};

static Z30LightInit zasm30_light_init = { 0, 0, 20, 24, 0, BLACK };

void q30_set_light_init(int dith_type, int dith_arg, int dith_perc, int light_rad, int trans_perc, int dark_color)
{
    zasm30_light_init.dith_type = dith_type;
    zasm30_light_init.dith_arg = dith_arg;
    zasm30_light_init.dith_perc = dith_perc;
    zasm30_light_init.light_rad = light_rad;
    zasm30_light_init.trans_perc = trans_perc;
    zasm30_light_init.dark_color = dark_color;
}

int q30_default_light_radius() { return zasm30_light_init.light_rad; }

// The light wave (init data): rate in frames, size in pixels; 0 is steady. Its clock is the
// engine's frame counter (upstream counts the frames played since the game began, so the wave's
// phase can differ; its size and speed are the same).
static int zasm30_light_wave_rate = 0, zasm30_light_wave_size = 0;

void q30_set_light_wave(int rate, int size)
{
    zasm30_light_wave_rate = rate;
    zasm30_light_wave_size = size;
}

// Upstream's calc_wave_rad: the radius swung by the sine of the clock over 2 * rate frames.
static int z30_wave_rad(int rad)
{
    const int rate = zasm30_light_wave_rate, size = zasm30_light_wave_size;
    const unsigned clk = (unsigned)frame;

    if(rate <= 0 || !size || !clk)
        return rad;

    const double turns = (clk % (unsigned)(rate * 2)) / double(rate * 2);
    const double scaled = size * sin(turns * 2 * PI);
    const int offset = (int)(scaled < 0 ? scaled - 0.5 : scaled + 0.5);
    return vbound(rad + offset, 0, 65535);
}

// Upstream's scale_by_percent.
static int z30_scale_perc(int val, int perc)
{
    return (int)(val * (perc / 100.0));
}

// One light, the way doDarkroomCircle/Square/Cone cut it into upstream's two dark bitmaps,
// added to the mask: what both bitmaps lose is lit (0); what only the opaque one loses is half
// dark (2).
static BITMAP *zasm30_light_tmp[2] = { NULL, NULL };

static void z30_add_light(BITMAP *mask, int shape, int cx, int cy, int glow, int dir,
                          int dith_perc, int trans_perc, int dith_type, int dith_arg, bool wave = true)
{
    if(glow <= 0 || !mask)
        return;

    if(wave)
        glow = z30_wave_rad(glow);     // (the quest's light wave; a lantern may opt out)

    if(dith_perc < 0) dith_perc = zasm30_light_init.dith_perc;
    if(trans_perc < 0) trans_perc = zasm30_light_init.trans_perc;
    if(dith_type < 0) dith_type = zasm30_light_init.dith_type;
    if(dith_arg < 0) dith_arg = zasm30_light_init.dith_arg;

    for(int i = 0; i < 2; ++i)
    {
        BITMAP *&b = zasm30_light_tmp[i];

        if(!b || b->w != mask->w || b->h != mask->h)
        {
            if(b)
                destroy_bitmap(b);
            b = create_bitmap_ex(8, mask->w, mask->h);
            if(!b)
                return;
        }

        clear_bitmap(b);
    }

    // cut: 2 where only the opaque layer is cut, 3 where both are; dith: 1 inside the dithered
    // radius, where both layers are cut wherever the pattern passes.
    BITMAP *cut = zasm30_light_tmp[0], *dith = zasm30_light_tmp[1];

    if(shape == 1)
    {
        const int dd = z30_scale_perc(glow, dith_perc), td0 = z30_scale_perc(glow, trans_perc);
        const int dith_rad = glow + 2 * dd, trans_rad = glow + 2 * td0;
        const int td = glow > trans_rad ? 0 : td0;
        const int max_rad = zc_max(glow, trans_rad);
        double xs = 0, ys = 0;

        switch(dir)
        {
        case up: case l_up: case r_up: ys = 1; break;
        case down: case l_down: case r_down: ys = -1; break;
        }
        switch(dir)
        {
        case left: case l_up: case l_down: xs = 1; break;
        case right: case r_up: case r_down: xs = -1; break;
        }
        if(dir & 4) { xs *= 0.75; ys *= 0.75; }

        z30_light_shape(dith, 1, cx + int(xs * dd), cy + int(ys * dd), dith_rad, dir, 1);
        z30_light_shape(cut, 1, cx + int(xs * td), cy + int(ys * td), max_rad, dir, 2);
        z30_light_shape(cut, 1, cx, cy, glow, dir, 3);
    }
    else
    {
        const int dith_rad = glow + z30_scale_perc(glow, dith_perc);
        const int trans_rad = glow + z30_scale_perc(glow, trans_perc);
        const int max_rad = zc_max(glow, trans_rad);

        z30_light_shape(dith, shape, cx, cy, dith_rad, dir, 1);
        z30_light_shape(cut, shape, cx, cy, max_rad, dir, 2);
        z30_light_shape(cut, shape, cx, cy, glow, dir, 3);
    }

    const Z30Dither d(dith_type, dith_arg, mask->w, mask->h);
    const int ox = z30_dither_offx(), oy = z30_dither_offy();

    for(int y = 0; y < mask->h; ++y)
    {
        const byte *c = (const byte *)cut->line[y];
        const byte *in = (const byte *)dith->line[y];
        byte *dst = (byte *)mask->line[y];

        for(int x = 0; x < mask->w; ++x)
        {
            if(c[x] == 3 || (in[x] && d.check(x + ox, y + oy)))
                dst[x] = 0;
            else if(c[x] == 2 && dst[x] == 1)
                dst[x] = 2;
        }
    }
}

// ---- what the dark room drawing (q30_draw_darkness) uses -------------------------------------------

// One light with the quest's default dither and transparency, as upstream's handle_lighting: torches,
// the lantern, lit weapons. shape 0 circle, 1 cone (dir), 2 square. wave: the quest's light wave
// swings its radius (a lantern can opt out).
void q30_add_light(BITMAP *mask, int shape, int cx, int cy, int rad, int dir, bool wave)
{
    z30_add_light(mask, shape, cx, cy, rad, dir & 7, -1, -1, -1, -1, wave);
}

enum { Z30_FDARK_DITHER = 0x08, Z30_FDARK_TRANS = 0x10, Z30_QR_NEWDARK_TRANS_STACKING = 440 };

// A screen flagged "dark dither" (upstream's fDARK_DITHER) is only dark where the quest's dither
// pattern passes: the part of the mask from (x0, y0) to (x1, y1) (exclusive) that screen covers.
void q30_darkness_dither(BITMAP *mask, int x0, int y0, int x1, int y1)
{
    const Z30Dither d(zasm30_light_init.dith_type, zasm30_light_init.dith_arg, mask->w, mask->h);
    const int ox = z30_dither_offx(), oy = z30_dither_offy();
    x0 = zc_max(0, x0);
    y0 = zc_max(0, y0);
    x1 = zc_min(mask->w, x1);
    y1 = zc_min(mask->h, y1);

    for(int y = y0; y < y1; ++y)
    {
        byte *line = (byte *)mask->line[y];

        for(int x = x0; x < x1; ++x)
            if(line[x] && d.check(x + ox, y + oy))
                line[x] = 0;
    }
}

// Draws the mask (0 lit, 1 dark, 2 the translucent ring) onto dest at yofs, the way upstream's
// draw_darkroom puts its two bitmaps over the screen: in the quest's darkness colour, solid, and
// translucent through trans_table2 for the ring, or everywhere on a "dark transparent" screen
// (fDARK_TRANS), where the ring only darkens again with qr_NEWDARK_TRANS_STACKING. Each part of
// the mask goes by the screen it shows (areas: the screens in view, the first one also for
// anything outside them; upstream's draw_darkroom works out the same per screen).
void q30_darkness_draw(BITMAP *dest, BITMAP *mask, int yofs, const Q30DarkArea *areas, int count)
{
    const int dark = zasm30_light_init.dark_color;

    if(dark == 0 || count <= 0)
        return;     // upstream's darkness bitmaps are masked: colour 0 never draws

    const bool stack = q30_quest_rule(Z30_QR_NEWDARK_TRANS_STACKING);
    const int x0 = zc_max(0, dest->cl), x1 = zc_min(mask->w, dest->cr);

    for(int y = 0; y < mask->h; ++y)
    {
        const int dy = y + yofs;

        if(dy < dest->ct || dy >= dest->cb)
            continue;

        const byte *line = (const byte *)mask->line[y];
        byte *out = (byte *)dest->line[dy];

        for(int x = x0; x < x1; ++x)
        {
            if(!line[x])
                continue;

            bool trans = areas[0].trans;

            for(int k = 1; k < count; ++k)
                if(x >= areas[k].x0 && x < areas[k].x1 && y >= areas[k].y0 && y < areas[k].y1)
                {
                    trans = areas[k].trans;
                    break;
                }

            int layers = 0;     // how many translucent passes; -1 solid

            if(line[x] == 1)
                layers = trans ? (stack ? 2 : 1) : -1;
            else if(line[x] == 2)
                layers = (!trans || stack) ? 1 : 0;

            if(layers < 0)
                out[x] = (byte)dark;
            else
                for(int k = 0; k < layers; ++k)
                    out[x] = trans_table2.data[dark][out[x]];
        }
    }
}

// The view's top-left (from the current screen's) while the mask is built: the dither patterns
// stay put on the map as the camera moves.
void q30_darkness_view(int vx, int vy)
{
    z30_dark_view_x = vx;
    z30_dark_view_y = vy;
}

int q30_darkness_color() { return zasm30_light_init.dark_color; }

// ---- lights scripts ask for --------------------------------------------------------------------

struct Z30Light
{
    int shape, x, y, rad, dir, trans_perc, dith_perc, dith_type, dith_arg;
    long origin, target;    // where x, y start (the script's Screen->DrawOrigin, as drawing commands)
};

static std::vector<Z30Light> zasm30_lights;

// Screen->DrawLightCircle/Square/Cone: queued with the frame's other script drawing, cleared with it.
// Arguments as upstream reads them: x, y, (dir for a cone,) radius, then the transparency and
// dither percentages and the dither type and argument, each -1 for the quest's default.
static void z30_drawlight_command(int cmd)
{
    const bool cone = cmd == Z30C_DRAWLIGHT_CONE;
    const int n = cone ? 8 : 7;
    long a[8];

    for(int k = 0; k < n; ++k)
        a[k] = SH::read_stack(SPR + (n - 1 - k));

    Z30Light l;
    l.shape = cmd == Z30C_DRAWLIGHT_SQUARE ? 2 : cone ? 1 : 0;
    l.x = a[0] / 10000;
    l.y = a[1] / 10000;

    l.dir = cone ? a[2] / 10000 : 0;

    long rad = a[cone ? 3 : 2];
    if(rad >= 0)
        rad /= 10000;
    else
        rad = zasm30_light_init.light_rad * (cone ? 2 : 1);

    if(!rad || (cone && l.dir < 0))
        return;

    if(cone)
        l.dir = l.dir & 7;

    const long *rest = &a[cone ? 4 : 3];
    l.rad = (int)rad;
    l.trans_perc = rest[0] >= 0 ? rest[0] / 10000 : -1;
    l.dith_perc = rest[1] >= 0 ? rest[1] / 10000 : -1;
    l.dith_type = rest[2] >= 0 ? rest[2] / 10000 : -1;
    l.dith_arg = rest[3] >= 0 ? rest[3] / 10000 : -1;

    // placed by the script's draw origin, as upstream draws lights with the other draw commands
    // (DEFAULT: the region inside a scrolling region, else the playing field)
    l.origin = zasm30_frame().draw_origin;
    l.target = l.origin == Z30_DRAW_ORIGIN_SPRITE ? zasm30_frame().draw_origin_target : 0;

    if(l.origin == Z30_DRAW_ORIGIN_DEFAULT)
        l.origin = q30_region_on() ? Z30_DRAW_ORIGIN_REGION : Z30_DRAW_ORIGIN_PLAYING_FIELD;

    zasm30_lights.push_back(l);
}

static void z30_clear_script_lights()
{
    zasm30_lights.clear();
}

// ---- sprite glow -------------------------------------------------------------------------------

// Where upstream's handle_sprlighting centres a sprite's light, in playing-field coordinates.
static void z30_glow_centre(sprite *s, int ofs, int &x, int &y)
{
    x = int(s->x) + s->hxsz / 2 + s->hxofs;
    y = int(s->y) + s->hysz / 2 + s->hyofs;

    switch(s->dir)
    {
    case up: y -= ofs; break;
    case down: y += ofs; break;
    case left: x -= ofs; break;
    case right: x += ofs; break;
    }
}

// Called by q30_draw_darkness before it draws the mask: every light a script drew this frame and
// every sprite a script gave a light radius. The mask's top-left is the view's, (vx, vy) from the
// current screen's top-left (a scrolling region's camera; 0, 0 otherwise).
void q30_script_lights(BITMAP *mask, int vx, int vy)
{
    if(!zasm30_ffscript_version || !mask)
        return;

    // ZC_Q30_LIGHTLOG: one line per dark screen visited, with what lit it (diagnostics only)
    static const bool log = getenv("ZC_Q30_LIGHTLOG") != NULL;
    static int logged_screen = -1;
    if(log && logged_screen != currmap * MAPSCRS + currscr)
    {
        logged_screen = currmap * MAPSCRS + currscr;
        int glows = 0;
        for(std::map<long, Zasm30SpriteState>::const_iterator it = zasm30_sprite_states.begin(); it != zasm30_sprite_states.end(); ++it)
            glows += it->second.glow_rad > 0;
        al_trace("q30 lightlog: flags9 %x dark colour %d dither %d/%d/%d%% trans %d%% rad %d stacking %d wave %d/%d level csets 1,5,7,8: %d%d%d%d; Link z %d threshold %d; dark map %d screen %d: %d script lights, %d glowing sprites%s\n", (int)tmpscr->flags9, zasm30_light_init.dark_color, zasm30_light_init.dith_type, zasm30_light_init.dith_arg, zasm30_light_init.dith_perc, zasm30_light_init.trans_perc, zasm30_light_init.light_rad, q30_quest_rule(440) ? 1 : 0, zasm30_light_wave_rate, zasm30_light_wave_size, q30_quest_rule(303) ? 1 : 0, q30_quest_rule(304) ? 1 : 0, q30_quest_rule(305) ? 1 : 0, q30_quest_rule(306) ? 1 : 0, int(Link.getZ()), (int)zinit.jump_link_layer_threshold, currmap, currscr,
                 (int)zasm30_lights.size(), glows, zasm30_lights.empty() ? "" : "");
        for(size_t i = 0; i < zasm30_lights.size() && i < 4; ++i)
            al_trace("q30 lightlog:   shape %d at %d,%d radius %d dir %d\n", zasm30_lights[i].shape, zasm30_lights[i].x,
                     zasm30_lights[i].y, zasm30_lights[i].rad, zasm30_lights[i].dir);
    }

    for(size_t i = 0; i < zasm30_lights.size(); ++i)
    {
        const Z30Light &l = zasm30_lights[i];
        int ox = 0, oy = 0;     // where the light's coordinates start on the mask

        switch(l.origin)
        {
        case Z30_DRAW_ORIGIN_PLAYING_FIELD:
            break;

        case Z30_DRAW_ORIGIN_SCREEN:
            oy = -playing_field_offset;
            break;

        case Z30_DRAW_ORIGIN_SPRITE:
        {
            Z30Sprite r = z30_sprite(l.target);

            if(!r.s)
                continue;   // its sprite has gone

            ox = int(r.s->x) - vx;
            oy = int(r.s->y) - vy;
            break;
        }

        default:    // the region (and its scrolling variants): moves with the camera; script
                    // light coordinates are region pixels, so the current screen's offset in its
                    // region comes off too (0 outside a scrolling region)
            ox = -vx - q30_region_origin_x();
            oy = -vy - q30_region_origin_y();
            break;
        }

        z30_add_light(mask, l.shape, l.x + ox, l.y + oy, l.rad, l.dir, l.dith_perc, l.trans_perc, l.dith_type, l.dith_arg);
    }

    for(std::map<long, Zasm30SpriteState>::const_iterator it = zasm30_sprite_states.begin(); it != zasm30_sprite_states.end(); ++it)
    {
        const Zasm30SpriteState &st = it->second;

        if(st.glow_rad <= 0)
            continue;

        Z30Sprite r = z30_sprite(it->first);

        if(!r.s)
            continue;

        int x, y;
        z30_glow_centre(r.s, st.glow_ofs, x, y);
        z30_add_light(mask, st.glow_shape, x - vx, y - vy, st.glow_rad, r.s->dir & 7, -1, -1, -1, -1);
    }

    // ZC_Q30_LIGHTDUMP=folder: the finished mask of each dark screen with script lights, as
    // mask_<map>_<screen>.bmp (white lit, grey half dark, black dark; diagnostics only)
    static const char *dump = getenv("ZC_Q30_LIGHTDUMP");
    static std::set<int> dumped;
    if(dump && !zasm30_lights.empty() && dumped.insert(currmap * MAPSCRS + currscr).second)
    {
        PALETTE pal;
        memset(pal, 0, sizeof(pal));
        pal[0].r = pal[0].g = pal[0].b = 63;
        pal[2].r = pal[2].g = pal[2].b = 32;
        char path[1024];
        snprintf(path, sizeof(path), "%s/mask_%d_%d.bmp", dump, currmap, currscr);
        save_bmp(path, mask, pal);
    }
}

#endif

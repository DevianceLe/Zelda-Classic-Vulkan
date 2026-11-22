// 2.55/3.0 drawing commands 2.53 has no command for, the command half (upstream ffscript.cpp's
// do_drawing_command for them): Screen-> commands are queued with the frame's other drawing,
// bitmap-> commands draw on a script's own bitmap at once (as this engine's other bitmap
// commands do) and are queued when their bitmap is the screen or a render target. The drawing
// itself is q30_draws.h (script_drawing.cpp). Also Graphics->ConvertFromRGB / ConvertToRGB,
// bitmap->CountColor and paldata->LoadBitmapPalette. Included once, from q30_scripts_cmds.h.

#ifndef ZC_Q30_SCRIPTS_DRAW_H
#define ZC_Q30_SCRIPTS_DRAW_H

bool zasm30_draw_cmd(BITMAP *bmp, int *sdci, std::vector<long> *v, int xoffset, int yoffset, bool offscreen);  // q30_draws.h
void zasm30_draw_mode7(BITMAP *dest, BITMAP *src, int *sdci, int yoffset);                                    // q30_draws.h
bool q30_load_image_palette(const char *path, RGB *pal, bool *indexed);                                       // q30_image.cpp

enum { Z30D_QR_BROKEN_SCRIPTS_BITMAP_DRAW_ORIGIN = 483 };
#ifndef Z30D_TARGET
#define Z30D_TARGET 19     // a queued bitmap command's own bitmap (as q30_draws.h)
#endif

// upstream FFScript::GetScriptBitmap: 9 the screen, 10..16 the render targets, else a script's
// bitmap (2.55.18 reads 9..16 as render targets 9..16: none, as z30_bitmap)
static BITMAP *z30d_script_bitmap_id(long id, BITMAP *screen_bmp)
{
    if(zasm30_ffscript_version < 23 && id >= 9 && id <= 16)
        return NULL;

    if(id == 9)
        return screen_bmp;

    if(id >= 10 && id <= 16)
        return zscriptDrawingRenderTarget->GetBitmapPtr(id - 10);

    std::map<long, BITMAP *>::iterator it = zasm30_bitmaps.find(id);
    return it == zasm30_bitmaps.end() ? NULL : it->second;
}

// upstream resolveScriptingBitmap: a negative number is an RT_ constant (-10000 RT_SCREEN)
BITMAP *zasm30_script_bitmap(long ref, BITMAP *screen_bmp)
{
    return z30d_script_bitmap_id(ref < 0 ? ref / 10000 + 10 : ref, screen_bmp);
}

// upstream resolveScriptingBitmapId: the RT_ number a reference names (-1 the screen, -2 the
// current target, 0..6 the render targets), or `user` set for a script's bitmap.
static long z30d_bitmap_id(long ref, bool &user)
{
    user = false;

    if(ref % 10000 == 0 && ref >= -20000 && ref <= 60000)
        return ref / 10000;

    if(ref - 10 >= -2 && ref - 10 <= 6)
        return ref - 10;

    user = true;
    return ref;
}

// upstream doesResolveToScreenBitmap
static bool z30d_is_screen_id(long id, bool user)
{
    if(user)
        return false;

    if(id == -1)
        return true;

    if(id == -2)
    {
        const int rt = zscriptDrawingRenderTarget->GetCurrentRenderTarget();
        return !(rt >= 0 && rt < 7);
    }

    return false;
}

// A new queued command j: [0] the command, [1..nargs] its arguments, [18] the render target.
static int z30d_queue(int cmd, int nargs)
{
    if(q30_skip_script_draws)
        return -1;

    const int j = script_drawing_commands.GetNext();

    if(j == -1)
    {
        Z_scripterrlog("Max draw primitive limit reached\n");
        return -1;
    }

    script_drawing_commands[j][0] = ZASM30_DRAW_BASE + cmd;

    for(int k = 1; k <= nargs; ++k)
        script_drawing_commands[j][k] = SH::read_stack(SPR + (nargs - k));

    script_drawing_commands[j][18] = zscriptDrawingRenderTarget->GetCurrentRenderTarget();
    return j;
}

// Where a queued command's coordinates start: the script's Screen->DrawOrigin, or the screen's
// top-left (upstream get_draw_origin_for_draw_command: a bitmap command that doesn't draw on the
// screen, without qr_BROKEN_SCRIPTS_BITMAP_DRAW_ORIGIN).
static void z30d_origin(int j, bool screen_origin)
{
    if(j < 0)
        return;

    if(screen_origin || q30_quest_rule(Z30D_QR_BROKEN_SCRIPTS_BITMAP_DRAW_ORIGIN))
        zasm30_capture_draw_origin(j);
    else
    {
        script_drawing_commands[j].q30_origin = Z30_DRAW_ORIGIN_SCREEN + 1;
        script_drawing_commands[j].q30_origin_target = 0;
    }
}

static void z30d_set_vector(int j, const std::vector<long> &v)
{
    std::vector<long> *p = script_drawing_commands.GetVector();
    p->assign(v.begin(), v.end());
    script_drawing_commands[j].SetVector(p);
}

// Screen-> command: queued, with the array or mapdata it reads in its vector.
static void z30d_screen(int cmd, int nargs, const std::vector<long> *v = NULL)
{
    const int j = z30d_queue(cmd, nargs);

    if(j < 0)
        return;

    if(v)
        z30d_set_vector(j, *v);

    zasm30_capture_draw_origin(j);
}

// The array of a PutPixels / DrawTiles / Lines / DrawCombos (its pointer at [2]) as whole numbers;
// false: none to draw (upstream drops the command, or logs an invalid pointer).
static bool z30d_array_arg(int nargs, const char *name, std::vector<long> &out)
{
    const long ptr = SH::read_stack(SPR + (nargs - 2)) / 10000;

    if(!ptr)
    {
        Z_scripterrlog("Invalid array pointer %ld passed to %s. Aborting.\n", ptr, name);
        return false;
    }

    std::vector<int> a;
    z30_array_ints(ptr, a);
    out.assign(a.begin(), a.end());
    return !out.empty();
}

// The combos of mapdata `ref` as DrawMapData draws them: {x, y, combo, cset} for every position
// (a Game->LoadTempScreen mapdata covers the whole scrolling region, as upstream's).
static void z30d_mapdata_combos(long ref, std::vector<long> &out)
{
    out.clear();

    if(ref > 0 && (ref & Z30_MAPDATA_TMP) && q30_region_on())
    {
        const int n = q30_region_num_rpos();

        for(int rpos = 0; rpos < n; ++rpos)
        {
            int pos, x, y;
            bool current;
            mapscr *m = q30_region_rpos_screen(rpos, (int)(ref & 0xFFFF), &pos, &current);

            if(!m || pos < 0 || pos >= (int)m->data.size())
                continue;

            q30_region_rpos_xy(rpos, &x, &y);
            out.push_back(x);
            out.push_back(y);
            out.push_back(m->data[pos]);
            out.push_back(m->cset[pos]);
        }

        return;
    }

    mapscr *m = z30_mapdata(ref);

    if(!m)
        return;

    for(int pos = 0; pos < 176 && pos < (int)m->data.size(); ++pos)
    {
        out.push_back((pos & 15) << 4);
        out.push_back(pos & 0xF0);
        out.push_back(m->data[pos]);
        out.push_back(m->cset[pos]);
    }
}

// bitmap-> command: [Z30D_TARGET] the bitmap the script called it on (at SPR + nargs).
// A script's own bitmap: drawn at once from its top-left. The screen (RT_SCREEN, or the pointer
// Game->LoadBitmapID(RT_SCREEN) gives): queued, drawn with the frame's other drawing from the
// script's draw origin. A render target: queued, drawn into it. Upstream draws nothing on an
// RT_CURRENT pointer, a render target never made, or a freed bitmap.
static void z30d_bitmap(int cmd, int nargs, const std::vector<long> *v = NULL)
{
    const long ref = SH::read_stack(SPR + nargs);

    if(ref <= 0 && ref != -10000)
        return;

    if(zasm30_ffscript_version < 23 && (ref <= 0 || (ref >= 9 && ref <= 16)))
        return;     // 2.55.18: no bitmap

    if(ref == 9 || ref == -10000)
    {
        const int j = z30d_queue(cmd, nargs);

        if(j < 0)
            return;

        script_drawing_commands[j][Z30D_TARGET] = (int)ref;

        if(v)
            z30d_set_vector(j, *v);

        z30d_origin(j, true);
        return;
    }

    if(ref >= 10 && ref <= 16)
    {
        if(!zscriptDrawingRenderTarget->GetBitmapPtr(ref - 10))
            return;

        const int j = z30d_queue(cmd, nargs);

        if(j < 0)
            return;

        script_drawing_commands[j][Z30D_TARGET] = (int)ref;
        script_drawing_commands[j][18] = (int)(ref - 10);   // drawn into that render target

        if(v)
            z30d_set_vector(j, *v);

        z30d_origin(j, false);
        return;
    }

    BITMAP *b = z30d_script_bitmap_id(ref, NULL);

    if(!b)
        return;

    int sdci[20];
    memset(sdci, 0, sizeof(sdci));
    sdci[0] = ZASM30_DRAW_BASE + cmd;

    for(int k = 1; k <= nargs; ++k)
        sdci[k] = SH::read_stack(SPR + (nargs - k));

    sdci[Z30D_TARGET] = (int)ref;
    std::vector<long> copy;

    if(v)
        copy = *v;

    zasm30_draw_cmd(b, sdci, &copy, 0, 0, false);
}

// bitmap->DrawLayerSolid and the other four: upstream draws them on the screen whatever the
// bitmap (do_bmpdrawlayer*): queued, from the draw origin only when the bitmap is the screen.
static void z30d_layer_info(int cmd)
{
    const long ref = SH::read_stack(SPR + 9);
    const int j = z30d_queue(cmd, 9);

    if(j < 0)
        return;

    bool user;
    const long id = z30d_bitmap_id(ref, user);
    script_drawing_commands[j][Z30D_TARGET] = (int)ref;
    z30d_origin(j, z30d_is_screen_id(id, user));
}

// bitmap->Quad3D / Triangle3D(layer, pos[], uv[], colours[], size[], flip, tile, polytype,
// texture bitmap): the arrays read now, as upstream reads them when it queues the command.
static void z30d_bmp_poly3d(bool quad)
{
    const int npos = quad ? 12 : 8, nuv = quad ? 8 : 6, ncol = quad ? 4 : 3;
    const int at_uv = quad ? 12 : 9, at_col = quad ? 20 : 15, at_size = quad ? 24 : 18;
    std::vector<long> v((size_t)(quad ? 26 : 20), 0);
    const int counts[4] = { npos, nuv, ncol, 2 }, offsets[4] = { 0, at_uv, at_col, at_size };

    for(int a = 0; a < 4; ++a)
    {
        std::vector<int> vals;
        z30_array_ints(SH::read_stack(SPR + (9 - (2 + a))) / 10000, vals);

        for(int q = 0; q < counts[a] && q < (int)vals.size(); ++q)
            v[offsets[a] + q] = vals[q];
    }

    z30d_bitmap(quad ? Z30C_BMPQUAD3DR : Z30C_BMPTRIANGLE3DR, 9, &v);
}

// bitmap->DrawPlane(layer, dest, src x, src y, -, dest y, dest w, dest h, space z, horizon,
// scale x, scale y, masked): the bitmap it's called on is the source (upstream bmp_do_mode7r).
static void z30d_bmp_mode7()
{
    const long ref = SH::read_stack(SPR + 13);
    long dest = SH::read_stack(SPR + 11);   // [2]
    bool user_dest = false;

    if(ref <= 0)
    {
        Z_scripterrlog("bitmap->DrawPlane() wanted to use to an invalid source bitmap id: %ld. Aborting.\n", ref);
        return;
    }

    if(dest >= 10000)
        dest /= 10000;
    else if(dest > 0)
        user_dest = true;

    // A script's bitmap as the destination: upstream 3.0's bitmap pointers are object ids from
    // 60000 up, which this reads as the raw number 6: render target 6 (the first 10000 objects a
    // quest makes, which is all of them for these quests). 2.55's small ids are the bitmap.
    if(user_dest && z30_counted() && zasm30_bitmaps.count(dest))
    {
        dest = 6;
        user_dest = false;
    }

    const int rt = zscriptDrawingRenderTarget->GetCurrentRenderTarget();

    // onto the screen: queued, with the source as it is now (the screen itself is read then)
    if(!user_dest && (dest == -1 || (dest == -2 && !(rt >= 0 && rt < 7))))
    {
        BITMAP *src = ref == 9 ? NULL : z30d_script_bitmap_id(ref, NULL);
        std::vector<long> v;

        if(ref != 9)
        {
            if(!src)
            {
                Z_message("Warning: %ld->DrawPlane() source bitmap contains invalid data or is not initialized.\n", ref);
                return;
            }

            v.reserve(2 + (size_t)src->w * src->h);
            v.push_back(src->w);
            v.push_back(src->h);

            for(int y = 0; y < src->h; ++y)
                for(int x = 0; x < src->w; ++x)
                    v.push_back(src->line[y][x]);
        }

        const int j = z30d_queue(Z30C_BMPMODE7, 13);

        if(j < 0)
            return;

        script_drawing_commands[j][Z30D_TARGET] = (int)ref;
        z30d_set_vector(j, v);
        z30d_origin(j, ref == 9);
        return;
    }

    BITMAP *src = z30d_script_bitmap_id(ref, framebuf);

    if(!src)
    {
        Z_message("Warning: %ld->DrawPlane() source bitmap contains invalid data or is not initialized.\n", ref);
        return;
    }

    BITMAP *destBMP = NULL;

    if(user_dest)
        destBMP = z30d_script_bitmap_id(dest, NULL);
    else if(dest >= 0 && dest <= 6)
        destBMP = zscriptDrawingRenderTarget->GetTargetBitmap((int)dest);

    if(!destBMP)
    {
        Z_message("Warning: DrawPlane(%ld) destination bitmap contains invalid data or is not initialized.\n", dest);
        return;
    }

    int sdci[20];
    memset(sdci, 0, sizeof(sdci));

    for(int k = 1; k <= 13; ++k)
        sdci[k] = SH::read_stack(SPR + (13 - k));

    zasm30_draw_mode7(destBMP, src, sdci, 0);
}

// ---- colours ----------------------------------------------------------------------------------
// upstream user_paldata::RGBTo / RGBFrom, all twelve colour spaces (RGB, CMYK, HSV, HSL, LAB,
// LCH and their hue directions).
enum { Z30D_CSPACE_RGB, Z30D_CSPACE_CMYK, Z30D_CSPACE_HSV, Z30D_CSPACE_HSV_CW, Z30D_CSPACE_HSV_CCW, Z30D_CSPACE_HSL,
       Z30D_CSPACE_HSL_CW, Z30D_CSPACE_HSL_CCW, Z30D_CSPACE_LAB, Z30D_CSPACE_LCH, Z30D_CSPACE_LCH_CW, Z30D_CSPACE_LCH_CCW };

static double z30d_vbound(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

static void z30d_rgb_xyz(double &r, double &g, double &b, double &x, double &y, double &z)
{
    r = r > 0.04045 ? pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
    g = g > 0.04045 ? pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
    b = b > 0.04045 ? pow((b + 0.055) / 1.055, 2.4) : b / 12.92;
    x = r * 0.4124 + g * 0.3576 + b * 0.1805;
    y = r * 0.2126 + g * 0.7152 + b * 0.0722;
    z = r * 0.0193 + g * 0.1192 + b * 0.9505;
    x = x > 0.008856 ? pow(x, 1.0 / 3.0) : (7.787 * x) + (16.0 / 116.0);
    y = y > 0.008856 ? pow(y, 1.0 / 3.0) : (7.787 * y) + (16.0 / 116.0);
    z = z > 0.008856 ? pow(z, 1.0 / 3.0) : (7.787 * z) + (16.0 / 116.0);
}

static void z30d_rgb_to(const RGB &c, double arr[], int color_space)
{
    const double upper = z30_color_max();
    double r = z30d_vbound(c.r / upper, 0.0, 1.0);
    double g = z30d_vbound(c.g / upper, 0.0, 1.0);
    double b = z30d_vbound(c.b / upper, 0.0, 1.0);

    switch(color_space)
    {
    case Z30D_CSPACE_CMYK:
    {
        double cc = 1.0 - r, m = 1.0 - g, y = 1.0 - b, k = 1.0;

        if(cc < k) k = cc;
        if(m < k) k = m;
        if(y < k) k = y;

        if(k == 1)
            cc = m = y = 0.0;
        else
        {
            cc = (cc - k) / (1.0 - k);
            m = (m - k) / (1.0 - k);
            y = (y - k) / (1.0 - k);
        }

        arr[0] = cc;
        arr[1] = m;
        arr[2] = y;
        arr[3] = k;
        break;
    }
    case Z30D_CSPACE_HSV_CW:
    case Z30D_CSPACE_HSV_CCW:
    case Z30D_CSPACE_HSV:
    case Z30D_CSPACE_HSL_CW:
    case Z30D_CSPACE_HSL_CCW:
    case Z30D_CSPACE_HSL:
    {
        const bool hsv = color_space <= Z30D_CSPACE_HSV_CCW;
        const double min_val = zc_min(zc_min(r, g), b), max_val = zc_max(zc_max(r, g), b);
        const double del_max = max_val - min_val;
        const double l = (max_val + min_val) / 2.0;
        double h = 0, s = 0;

        if(del_max != 0)
        {
            if(hsv)
                s = del_max / max_val;
            else if(l < 0.5)
                s = del_max / (max_val + min_val);
            else
                s = del_max / (2 - max_val - min_val);

            const double del_r = (((max_val - r) / 6.0) + (del_max / 2.0)) / del_max;
            const double del_g = (((max_val - g) / 6.0) + (del_max / 2.0)) / del_max;
            const double del_b = (((max_val - b) / 6.0) + (del_max / 2.0)) / del_max;

            if(r == max_val) h = del_b - del_g;
            else if(g == max_val) h = (1.0 / 3.0) + del_r - del_b;
            else if(b == max_val) h = (2.0 / 3.0) + del_g - del_r;

            if(h < 0) ++h;
            if(h > 1) --h;
        }

        arr[0] = h;
        arr[1] = s;
        arr[2] = hsv ? max_val : l;
        break;
    }
    case Z30D_CSPACE_LAB:
    case Z30D_CSPACE_LCH_CW:
    case Z30D_CSPACE_LCH_CCW:
    case Z30D_CSPACE_LCH:
    {
        double x, y, z;
        z30d_rgb_xyz(r, g, b, x, y, z);
        const double CIEL = (116 * y) - 16, CIEa = 500 * (x - y), CIEb = 200 * (y - z);

        if(color_space == Z30D_CSPACE_LAB)
        {
            arr[0] = CIEL;
            arr[1] = CIEa;
            arr[2] = CIEb;
            break;
        }

        double h = atan2(CIEb, CIEa);
        if(h > 0) h = (h / PI) * 180;
        else h = 360 - (fabs(h) / PI) * 180;

        arr[0] = CIEL;
        arr[1] = sqrt(pow(CIEa, 2) + pow(CIEb, 2));
        arr[2] = h;
        break;
    }
    }
}

static double z30d_hue_to_rgb(double v1, double v2, double vH)
{
    if(vH < 0) vH += 1;
    if(vH > 1) vH -= 1;
    if((6 * vH) < 1) return (v1 + (v2 - v1) * 6 * vH);
    if((2 * vH) < 1) return (v2);
    if((3 * vH) < 2) return (v1 + (v2 - v1) * ((2.0 / 3.0) - vH) * 6);
    return (v1);
}

static RGB z30d_make_rgb(double r, double g, double b)    // upstream _RGB(int, int, int): truncated
{
    RGB c;
    c.r = (unsigned char)(int)r;
    c.g = (unsigned char)(int)g;
    c.b = (unsigned char)(int)b;
    c.filler = 0;
    return c;
}

static RGB z30d_rgb_from(const double arr[], int color_space)
{
    const double upper = z30_color_max();
    double r = 0.0, g = 0.0, b = 0.0;

    switch(color_space)
    {
    case Z30D_CSPACE_CMYK:
    {
        const double cc = (arr[0] * (1 - arr[3]) + arr[3]);
        const double m = (arr[1] * (1 - arr[3]) + arr[3]);
        const double y = (arr[2] * (1 - arr[3]) + arr[3]);
        return z30d_make_rgb(z30d_vbound((1 - cc) * upper, 0.0, upper), z30d_vbound((1 - m) * upper, 0.0, upper),
                             z30d_vbound((1 - y) * upper, 0.0, upper));
    }
    case Z30D_CSPACE_HSV_CW:
    case Z30D_CSPACE_HSV_CCW:
    case Z30D_CSPACE_HSV:
    {
        const double h = arr[0], s = arr[1], v = arr[2];

        if(s == 0)
            r = g = b = v;
        else
        {
            double var_h = h * 6;
            if(var_h >= 6) var_h = 0;
            const int var_i = (int)floor(var_h);
            const double var_1 = v * (1 - s);
            const double var_2 = v * (1 - s * (var_h - var_i));
            const double var_3 = v * (1 - s * (1 - (var_h - var_i)));

            switch(var_i)
            {
            case 0: r = v; g = var_3; b = var_1; break;
            case 1: r = var_2; g = v; b = var_1; break;
            case 2: r = var_1; g = v; b = var_3; break;
            case 3: r = var_1; g = var_2; b = v; break;
            case 4: r = var_3; g = var_1; b = v; break;
            default: r = v; g = var_1; b = var_2; break;
            }
        }
        break;
    }
    case Z30D_CSPACE_HSL_CW:
    case Z30D_CSPACE_HSL_CCW:
    case Z30D_CSPACE_HSL:
    {
        const double h = arr[0], s = arr[1], l = arr[2];

        if(s == 0)
            r = g = b = l;
        else
        {
            const double var_2 = l < 0.5 ? l * (1 + s) : (l + s) - (s * l);
            const double var_1 = 2 * l - var_2;
            r = z30d_hue_to_rgb(var_1, var_2, h + (1.0 / 3.0));
            g = z30d_hue_to_rgb(var_1, var_2, h);
            b = z30d_hue_to_rgb(var_1, var_2, h - (1.0 / 3.0));
        }
        break;
    }
    case Z30D_CSPACE_LAB:
    case Z30D_CSPACE_LCH_CW:
    case Z30D_CSPACE_LCH_CCW:
    case Z30D_CSPACE_LCH:
    {
        const double CIEL = arr[0];
        const double CIEa = color_space == Z30D_CSPACE_LAB ? arr[1] : cos((arr[2] * PI) / 180.0) * arr[1];
        const double CIEb = color_space == Z30D_CSPACE_LAB ? arr[2] : sin((arr[2] * PI) / 180.0) * arr[1];
        double var_y = (CIEL + 16) / 116.0;
        double var_x = CIEa / 500.0 + var_y;
        double var_z = var_y - CIEb / 200.0;

        var_x = pow(var_x, 3) > 0.008856 ? pow(var_x, 3) : (var_x - 16.0 / 116.0) / 7.787;
        var_y = pow(var_y, 3) > 0.008856 ? pow(var_y, 3) : (var_y - 16.0 / 116.0) / 7.787;
        var_z = pow(var_z, 3) > 0.008856 ? pow(var_z, 3) : (var_z - 16.0 / 116.0) / 7.787;

        r = var_x * 3.2406 + var_y * -1.5372 + var_z * -0.4986;
        g = var_x * -0.9689 + var_y * 1.8758 + var_z * 0.0415;
        b = var_x * 0.0557 + var_y * -0.2040 + var_z * 1.0570;

        r = r > 0.0031308 ? 1.055 * pow(r, (1 / 2.4)) - 0.055 : 12.92 * r;
        g = g > 0.0031308 ? 1.055 * pow(g, (1 / 2.4)) - 0.055 : 12.92 * g;
        b = b > 0.0031308 ? 1.055 * pow(b, (1 / 2.4)) - 0.055 : 12.92 * b;
        break;
    }
    default:
        return z30d_make_rgb(0, 0, 0);
    }

    return z30d_make_rgb(z30d_vbound(r * upper, 0.0, upper), z30d_vbound(g * upper, 0.0, upper), z30d_vbound(b * upper, 0.0, upper));
}

// Graphics->ConvertFromRGB(buf[], rgb, colour space): the colour's components, x10000
static void z30d_convert_from_rgb()
{
    const long buf = SH::read_stack(SPR + 2) / 10000;
    const long clri = SH::read_stack(SPR + 1);
    const int color_space = SH::read_stack(SPR) / 10000;
    ZScriptArray &a = ArrayH::getArray(buf);

    if(&a == &INVALIDARRAY)
        return;

    const long target = color_space == Z30D_CSPACE_CMYK ? 4 : 3;

    if((long)a.Size() < target)
        zasm30_resize_array(a, target);

    RGB c = z30d_make_rgb((clri >> 16) & 0xFF, (clri >> 8) & 0xFF, clri & 0xFF);
    double convert[4] = { 0, 0, 0, 0 };
    z30d_rgb_to(c, convert, color_space);

    for(long q = 0; q < target; ++q)
        a[q] = long(convert[q] * 10000);
}

// Graphics->ConvertToRGB(buf[], colour space): the rgb value in rEXP1
static void z30d_convert_to_rgb()
{
    const long buf = SH::read_stack(SPR + 1) / 10000;
    const int color_space = SH::read_stack(SPR) / 10000;
    ZScriptArray &a = ArrayH::getArray(buf);

    if(&a == &INVALIDARRAY)
        return;

    const long target = color_space == Z30D_CSPACE_CMYK ? 4 : 3;

    if((long)a.Size() < target)
    {
        Z_scripterrlog("Array not large enough. Should be at least size %ld\n", target);
        return;
    }

    double convert[4] = { 0, 0, 0, 0 };

    for(long q = 0; q < target; ++q)
        convert[q] = a[q] / 10000.0;

    const RGB c = z30d_rgb_from(convert, color_space);
    ri->d[2] = (long(c.r) << 16) | (long(c.g) << 8) | c.b;
}

// bitmap->CountColor(mask, x, y, colour, mask colour): how many pixels under the mask's
// (maskCol, or any non-0) pixels have the colour (or any non-0); -1 for a bad bitmap.
// Upstream's FFCore.GetScriptBitmap: 9 is the last frame drawn.
static void z30d_count_color()
{
    const long bmpref = SH::read_stack(SPR + 5), maskref = SH::read_stack(SPR + 4);
    const int x = SH::read_stack(SPR + 3) / 10000, y = SH::read_stack(SPR + 2) / 10000;
    const int checkCol = SH::read_stack(SPR + 1) / 10000, maskCol = SH::read_stack(SPR) / 10000;
    BITMAP *checkbit = z30d_script_bitmap_id(bmpref, framebuf);
    BITMAP *maskbit = z30d_script_bitmap_id(maskref, framebuf);

    if(!(checkbit && maskbit))
    {
        set_register(sarg1, -10000);
        Z_scripterrlog("Invalid bitmap%s passed to 'bitmap->CountColor()': %ld, %ld\n", (checkbit || maskbit) ? "" : "s",
                       bmpref, maskref);
        return;
    }

    const int wid = zc_min(checkbit->w - x, maskbit->w);
    const int hei = zc_min(checkbit->h - y, maskbit->h);
    long count = 0;

    for(int ty = 0; ty < hei; ++ty)
    {
        if(ty + y < 0 || ty >= maskbit->h)
            continue;

        for(int tx = 0; tx < wid; ++tx)
        {
            if(tx + x < 0)
                continue;

            const int maskc = maskbit->line[ty][tx];

            if(maskCol < 0 ? maskc != 0 : maskc == maskCol)
            {
                const int color = checkbit->line[ty + y][tx + x];

                if(checkCol < 0 ? color != 0 : color == checkCol)
                    ++count;
            }
        }
    }

    set_register(sarg1, count * 10000);
}

// paldata->LoadBitmapPalette(filename): all 256 colours from an image file's palette, kept as
// the file has them (6-bit, as upstream keeps them here). An image with no palette (true colour)
// leaves the paldata as it was (upstream gives it Allegro's 3-3-2 palette).
static void z30d_paldata_load_bitmap()
{
    Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));

    if(!pd)
        return;

    std::string user, path;
    ArrayH::getString(get_register(sarg1) / 10000, user, 256);

    if(!(q30_quest_rule(678) ? z30_resolve_path(user, true, path) : z30_legacy_path(user, path)))
    {
        Z_scripterrlog("Error: the path '%s' is not allowed for LoadBitmapPalette().\n", user.c_str());
        return;
    }

    if(path.empty())
    {
        al_trace("String pointer is null! Internal error. \n");
        return;
    }

    if(!z30_path_is(path, false))
    {
        Z_scripterrlog("Failed to load image file: %s. File not found.\n", path.c_str());
        return;
    }

    PALETTE pal;
    get_palette(pal);
    bool indexed = false;

    if(!q30_load_image_palette(path.c_str(), pal, &indexed))
    {
        Z_scripterrlog("LoadBitmapPalette() failed to load image file %s.\n", path.c_str());
        return;
    }

    if(!indexed)
    {
        zasm30_log_once("paldata->LoadBitmapPalette() of a true-colour image (left as it was)", Z30C_PALDATALOADBITMAP);
        return;
    }

    for(int q = 0; q < Z30_PALDATA_COLORS; ++q)
    {
        pd->c[q] = pal[q];
        pd->used[q] = true;
    }
}

// The hook in zasm30_command.
static bool zasm30_draw_command(word cmd)
{
    std::vector<long> v;

    switch(cmd)
    {
    // -- Screen-> --------------------------------------------------------------------------------
    case Z30C_FRAMER: z30d_screen(cmd, 9); return true;
    case Z30C_DRAWMINITILE: z30d_screen(cmd, 7); return true;
    case Z30C_DRAWTILECLOAKEDR: z30d_screen(cmd, 7); return true;
    case Z30C_DRAWCOMBOCLOAKEDR: z30d_screen(cmd, 7); return true;
    case Z30C_BITMAPEXR: z30d_screen(cmd, 16); return true;
    case Z30C_TILEBLIT: z30d_screen(cmd, 17); return true;
    case Z30C_COMBOBLIT: z30d_screen(cmd, 17); return true;
    case Z30C_PIXELARRAYR:
        if(z30d_array_arg(5, "Screen->PutPixels()", v))
            z30d_screen(cmd, 5, &v);
        return true;
    case Z30C_TILEARRAYR:
        if(z30d_array_arg(2, "Screen->DrawTiles()", v))
            z30d_screen(cmd, 2, &v);
        return true;
    case Z30C_LINESARRAY:
        if(z30d_array_arg(2, "Screen->Lines()", v))
            z30d_screen(cmd, 2, &v);
        return true;
    case Z30C_COMBOARRAYR:
        if(z30d_array_arg(2, "Screen->DrawCombos()", v))
            z30d_screen(cmd, 2, &v);
        return true;
    case Z30C_DRAWMAPDATA:
        z30d_mapdata_combos(SH::read_stack(SPR + 3), v);
        z30d_screen(cmd, 5, &v);
        return true;

    // -- bitmap-> ----------------------------------------------------------------------------------
    case Z30C_BMPFRAMER: z30d_bitmap(cmd, 9); return true;
    case Z30C_BMPDRAWMINITILE: z30d_bitmap(cmd, 7); return true;
    case Z30C_BMPDRAWTILECLOAKEDR: z30d_bitmap(cmd, 7); return true;
    case Z30C_BMPDRAWCOMBOCLOAKEDR: z30d_bitmap(cmd, 7); return true;
    case Z30C_BMPTILEBLIT: z30d_bitmap(cmd, 17); return true;
    case Z30C_BMPCOMBOBLIT: z30d_bitmap(cmd, 17); return true;
    case Z30C_BMPDRAWSCREENSOLIDR:
    case Z30C_BMPDRAWSCREENSOLID2R:
    case Z30C_BMPDRAWSCREENCOMBOIR:
    case Z30C_BMPDRAWSCREENCOMBOTR:
        z30d_bitmap(cmd, 6);
        return true;
    case Z30C_BMPDRAWLAYERSOLIDR:
    case Z30C_BMPDRAWLAYERCFLAGR:
    case Z30C_BMPDRAWLAYERCTYPER:
    case Z30C_BMPDRAWLAYERCIFLAGR:
    case Z30C_BMPDRAWLAYERSOLIDITYR:
        z30d_layer_info(cmd);
        return true;
    case Z30C_BMPDRAWMAPDATA:
        z30d_mapdata_combos(SH::read_stack(SPR + 3), v);
        z30d_bitmap(cmd, 5, &v);
        return true;
    case Z30C_BMPQUAD3DR: z30d_bmp_poly3d(true); return true;
    case Z30C_BMPTRIANGLE3DR: z30d_bmp_poly3d(false); return true;
    case Z30C_BMPMODE7: z30d_bmp_mode7(); return true;

    // -- colours and bitmaps read --------------------------------------------------------------
    case Z30C_CONVERTFROMRGB: z30d_convert_from_rgb(); return true;
    case Z30C_CONVERTTORGB: z30d_convert_to_rgb(); return true;
    case Z30C_GRAPHICSCOUNTCOLOR: z30d_count_color(); return true;
    case Z30C_PALDATALOADBITMAP: z30d_paldata_load_bitmap(); return true;
    }

    return false;
}

#endif

// 2.55/3.0 script drawing commands 2.53's script_drawing.cpp has no code for: the drawing half
// (upstream script_drawing.cpp's do_* and bmp_do_* for them). The command half, which reads the
// arguments and queues the command or draws it on a script's bitmap at once, is
// q30_scripts_draw.h. Included once, at the end of script_drawing.cpp, for its TileHelper and
// degrees_to_fixed.
//
// A queued command keeps its 3.0 number plus ZASM30_DRAW_BASE in [0], upstream's arguments in
// [1..17], the render target in [18] as 2.53's do, and a bitmap command's own bitmap in [19]
// (upstream's DRAWCMD_BMP_TARGET); arrays come in the command's vector.

#ifndef ZC_Q30_DRAWS_H
#define ZC_Q30_DRAWS_H

#include "q30_scripts_defs.h"
#include "subscr.h"

#ifndef Z30D_TARGET
#define Z30D_TARGET 19
#endif

int q30_script_combo_get(int id, int what);    // q30 combo reader: upstream's type (0), flag (1), walk (2)
bool q30_quest_rule(int id);
// A 3.0 bitmap reference as upstream's resolveScriptingBitmap reads it at drawing time (the
// screen being drawn for RT_SCREEN); q30_scripts_draw.h.
BITMAP *zasm30_script_bitmap(long ref, BITMAP *screen_bmp);

enum { Z30D_QR_BROKEN_DRAWSCREEN_FUNCTIONS = 320, Z30D_QR_DRAWTILE_TALL_DRAWS_WRAP_POORLY = 564 };

#define Z30D_WRAP_CS(cs) ((cs+14)%14)
#ifndef TILEROW
#define TILEROW(tile) ((tile)/TILES_PER_ROW)
#endif

static int z30d_wrap(int x, int low, int high)
{
    while(x < low)
        x += high - low + 1;

    while(x > high)
        x -= high - low + 1;

    return x;
}

static bool z30d_pow2(int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

// A bitmap command drawing on the screen keeps the draw origin's y; on any other bitmap upstream
// starts at its top ("Don't crop"): only refs 8 and 9 (LoadBitmapID's RT_CURRENT / RT_SCREEN) keep it.
static int z30d_crop_y(const int *sdci, int yoffset)
{
    return (sdci[Z30D_TARGET] - 10 != -2 && sdci[Z30D_TARGET] - 10 != -1) ? 0 : yoffset;
}

// upstream TileHelper::OverTileCloaked
static void z30d_overtile_cloaked(BITMAP *dest, int tile, int x, int y, int w, int h, int flip, byte skiprows = 0)
{
    if(skiprows > 0 && tile % TILES_PER_ROW + w >= TILES_PER_ROW)
    {
        byte w2 = (tile + w) % TILES_PER_ROW;
        z30d_overtile_cloaked(dest, tile, x, y, w - w2, h, flip);
        z30d_overtile_cloaked(dest, tile + (w - w2) + (skiprows * TILES_PER_ROW), x + 16 * (w - w2), y, w2, h, flip);
        return;
    }

    switch(flip)
    {
    case 1:
        for(int j = 0; j < h; j++)
            for(int k = w - 1; k >= 0; k--)
                overtilecloaked16(dest, tile + (j * TILES_PER_ROW) + k, x + ((w - 1) - k) * 16, y + j * 16, flip);
        break;

    case 2:
        for(int j = h - 1; j >= 0; j--)
            for(int k = 0; k < w; k++)
                overtilecloaked16(dest, tile + (j * TILES_PER_ROW) + k, x + k * 16, y + ((h - 1) - j) * 16, flip);
        break;

    case 3:
        for(int j = h - 1; j >= 0; j--)
            for(int k = w - 1; k >= 0; k--)
                overtilecloaked16(dest, tile + (j * TILES_PER_ROW) + k, x + ((w - 1) - k) * 16, y + ((h - 1) - j) * 16, flip);
        break;

    default:
        for(int j = 0; j < h; j++)
            for(int k = 0; k < w; k++)
                overtilecloaked16(dest, tile + (j * TILES_PER_ROW) + k, x + k * 16, y + j * 16, flip);
        break;
    }
}

// DrawFrame(layer, x, y, tile, cset, width, height, overlay, opacity)
static void z30d_framer(BITMAP *bmp, int *sdci, int xoffset, int yoffset)
{
    const int x = sdci[2] / 10000, y = sdci[3] / 10000;
    const int tile = sdci[4] / 10000, cs = sdci[5] / 10000, w = sdci[6] / 10000, h = sdci[7] / 10000;
    const bool overlay = sdci[8] != 0;
    const bool trans = (sdci[9] / 10000 <= 127);

    frame2x2(bmp, &QMisc, x + xoffset, y + yoffset, tile, cs, w, h, 0, overlay, trans);
}

// Screen->PutPixels(layer, array {x, y, colour, opacity ...}, rx, ry, angle)
static void z30d_putpixels(BITMAP *bmp, int *sdci, std::vector<long> *vp, int xoffset, int yoffset)
{
    if(!vp || vp->empty())
        return;

    std::vector<long> &v = *vp;
    const int sz = (int)v.size();

    for(int q = 0; q + 3 < sz; q += 4)
    {
        int x1 = v[q], y1 = v[q + 1];

        if(sdci[5] != 0)     // rotation
        {
            const int rx = sdci[3] / 10000, ry = sdci[4] / 10000;
            fixed ra1 = itofix(sdci[5] % 10000) / 10000;
            fixed ra2 = itofix(sdci[5] / 10000);
            fixed ra = ra1 + ra2;
            const int nx = rx + fixtoi((fixcos(ra) * (x1 - rx) - fixsin(ra) * (y1 - ry)));
            const int ny = ry + fixtoi((fixsin(ra) * (x1 - rx) + fixcos(ra) * (y1 - ry)));
            x1 = nx;
            y1 = ny;
        }

        if(v[q + 3] < 128)
            drawing_mode(DRAW_MODE_TRANS, NULL, 0, 0);
        else
            drawing_mode(DRAW_MODE_SOLID, NULL, 0, 0);

        putpixel(bmp, x1 + xoffset, y1 + yoffset, (int)v[q + 2]);
    }

    drawing_mode(DRAW_MODE_SOLID, NULL, 0, 0);
}

// Screen->DrawTiles(layer, array {x, y, tile, cset, opacity ...})
static void z30d_fasttiles(BITMAP *bmp, std::vector<long> *vp, int xoffset, int yoffset)
{
    if(!vp || vp->empty())
        return;

    std::vector<long> &v = *vp;

    for(int q = 0; q + 4 < (int)v.size(); q += 5)
    {
        if(v[q + 4] < 128)
            overtiletranslucent16(bmp, (int)v[q + 2], xoffset + (int)v[q], yoffset + (int)v[q + 1], (int)v[q + 3], 0, (int)v[q + 4]);
        else
            overtile16(bmp, (int)v[q + 2], xoffset + (int)v[q], yoffset + (int)v[q + 1], (int)v[q + 3], 0);
    }
}

// Screen->Lines(layer, array {x, y, x2, y2, colour, scale, rx, ry, angle, opacity ...})
static void z30d_lines(BITMAP *bmp, std::vector<long> *vp, int xoffset, int yoffset)
{
    if(!vp || vp->empty())
        return;

    std::vector<long> &v = *vp;
    const int sz = (int)v.size();

    for(int q = 0; q + 9 < sz; q += 10)
    {
        int x1 = v[q], y1 = v[q + 1], x2 = v[q + 2], y2 = v[q + 3];
        const int color = v[q + 4];

        if(v[q + 5] == 0)
        {
            Z_scripterrlog("Lines() aborting due to scale\n");
            return;
        }

        // (upstream compares the scale, already a whole number here, with 10000)
        if(v[q + 5] != 10000)
        {
            const int w = x2 - x1 + 1, h = y2 - y1 + 1;
            const int w2 = int(w * ((double)v[q + 5])), h2 = int(h * ((double)v[q + 5]));
            x1 = x1 - ((w2 - w) / 2);
            x2 = x2 + ((w2 - w) / 2);
            y1 = y1 - ((h2 - h) / 2);
            y2 = y2 + ((h2 - h) / 2);
        }

        if(v[q + 9] <= 127)
            drawing_mode(DRAW_MODE_TRANS, NULL, 0, 0);
        else
            drawing_mode(DRAW_MODE_SOLID, NULL, 0, 0);

        if(v[q + 8] != 0)    // rotation
        {
            const int rx = v[q + 6], ry = v[q + 7];
            fixed ra1 = itofix(v[q + 8] % 1);
            fixed ra2 = itofix(v[q + 8]);
            fixed ra = ra1 + ra2;
            const int nx1 = rx + fixtoi((fixcos(ra) * (x1 - rx) - fixsin(ra) * (y1 - ry)));
            const int ny1 = ry + fixtoi((fixsin(ra) * (x1 - rx) + fixcos(ra) * (y1 - ry)));
            const int nx2 = rx + fixtoi((fixcos(ra) * (x2 - rx) - fixsin(ra) * (y2 - ry)));
            const int ny2 = ry + fixtoi((fixsin(ra) * (x2 - rx) + fixcos(ra) * (y2 - ry)));
            x1 = nx1;
            y1 = ny1;
            x2 = nx2;
            y2 = ny2;
        }

        line(bmp, x1 + xoffset, y1 + yoffset, x2 + xoffset, y2 + yoffset, color);
    }

    drawing_mode(DRAW_MODE_SOLID, NULL, 0, 0);
}

// Screen->DrawCombos(layer, array {x, y, combo, cset, opacity ...})
static void z30d_fastcombos(BITMAP *bmp, std::vector<long> *vp, int xoffset, int yoffset)
{
    if(!vp || vp->empty())
        return;

    std::vector<long> &v = *vp;

    for(int q = 0; q + 4 < (int)v.size(); q += 5)
    {
        const long cid = v[q + 2];

        if((unsigned long)cid >= MAXCOMBOS)
        {
            Z_scripterrlog("FastCombos() cannot draw combo '%ld', as it is out of bounds.\n", cid);
            continue;
        }

        if(combo_editor_only((int)cid))
            continue;

        if(v[q + 4] < 128)
            overcomboblocktranslucent(bmp, xoffset + (int)v[q], yoffset + (int)v[q + 1], (int)cid, (int)v[q + 3], 1, 1, 128);
        else
            overcomboblock(bmp, xoffset + (int)v[q], yoffset + (int)v[q + 1], (int)cid, (int)v[q + 3], 1, 1);
    }
}

// DrawMiniTile(layer, x, y, tile, corner, cset, opacity): one 8x8 quarter of a tile
static void z30d_minitile(BITMAP *bmp, int *sdci, int xoffset, int yoffset)
{
    const int opacity = sdci[7] / 10000;
    const int x = xoffset + (sdci[2] / 10000), y = yoffset + (sdci[3] / 10000);
    const int minitile = ((sdci[4] / 10000) << 2) + sdci[5] / 10000;
    const int cset = sdci[6] / 10000;

    if(opacity < 128)
        overtiletranslucent8(bmp, minitile, x, y, cset, 0, opacity);
    else
        overtile8(bmp, minitile, x, y, cset, 0);
}

// DrawTileCloaked(layer, x, y, tile, w, h, flip)
static void z30d_tilecloaked(BITMAP *bmp, int *sdci, int xoffset, int yoffset)
{
    const int w = sdci[5] / 10000, h = sdci[6] / 10000;

    if(w < 1 || h < 1 || h > 20 || w > 20)
        return;

    const int flip = (sdci[7] / 10000) & 3;
    const byte skiprows = q30_quest_rule(Z30D_QR_DRAWTILE_TALL_DRAWS_WRAP_POORLY) ? 0 : h - 1;
    z30d_overtile_cloaked(bmp, sdci[4] / 10000, xoffset + sdci[2] / 10000, yoffset + sdci[3] / 10000, w, h, flip, skiprows);
}

// DrawComboCloaked(layer, x, y, combo, w, h, flip)
static void z30d_combocloaked(BITMAP *bmp, int *sdci, int xoffset, int yoffset)
{
    const int w = sdci[5] / 10000, h = sdci[6] / 10000;

    if(w < 1 || h < 1 || h > 20 || w > 20)
        return;

    const int cmb = sdci[4] / 10000;

    if((unsigned)cmb >= MAXCOMBOS)
    {
        Z_scripterrlog("DrawComboCloaked() cannot draw combo '%d', as it is out of bounds.\n", cmb);
        return;
    }

    const int x1 = sdci[2] / 10000, y1 = sdci[3] / 10000;
    const newcombo &c = combobuf[cmb];

    if(combo_editor_only(cmb))
        return;

    const int tiletodraw = combo_tile(c, x1, y1);
    const int flip = ((sdci[7] / 10000) & 3) ^ c.flip;
    z30d_overtile_cloaked(bmp, tiletodraw, xoffset + x1, yoffset + y1, w, h, flip, c.skipanimy);
}

// Screen->DrawMapData / bitmap->DrawMapData: the combos the command took when it was given
// ({x, y, combo, cset} each, in the vector), [3], [4] where, [5] opacity (upstream draw_mapdata).
static void z30d_mapdata(BITMAP *bmp, int *sdci, std::vector<long> *vp, int xoffset, int yoffset)
{
    if(!vp)
        return;

    const int x = xoffset + sdci[3] / 10000, y = yoffset + sdci[4] / 10000;
    const bool transparent = (sdci[5] / 10000) < 128;
    std::vector<long> &v = *vp;

    for(size_t q = 0; q + 3 < v.size(); q += 4)
    {
        const int cid = (int)v[q + 2], cset = (int)v[q + 3];

        if(combo_editor_only(cid))
            continue;

        if(transparent != combo_transparent(cid))
            overcomboblocktranslucent(bmp, x + (int)v[q], y + (int)v[q + 1], cid, cset, 1, 1, 128);
        else
            overcomboblock(bmp, x + (int)v[q], y + (int)v[q + 1], cid, cset, 1, 1);
    }
}

// ---- screen information drawn as colours (DrawScreenSolid ... DrawLayerComboTypes) -------------
// Each 16x16 square gets the palette index of what the combo there has: 0 solidity, 1 the
// solid quarters as colour 1, 2 placed flag, 3 combo type, 4 inherent flag. Upstream's numbers.

static int z30d_info_value(const mapscr &m, int i, int what)
{
    const int cid = m.data[i];

    switch(what)
    {
    case 0: return q30_script_combo_get(cid, 2) & 15;
    case 2: return m.sflag[i];
    case 3: return q30_script_combo_get(cid, 0);
    case 4: return q30_script_combo_get(cid, 1);
    }

    return 0;
}

// upstream draw_map_solid's square: colour 1 in each solid quarter
static void z30d_solid_square(BITMAP *square, BITMAP *subsquare, int sol)
{
    if(sol & 1) blit(subsquare, square, 0, 0, 0, 0, 8, 8);
    if(sol & 2) blit(subsquare, square, 0, 0, 0, 8, 8, 8);
    if(sol & 4) blit(subsquare, square, 0, 0, 8, 0, 8, 8);
    if(sol & 8) blit(subsquare, square, 0, 0, 8, 8, 8, 8);
}

// upstream draw_map_solidity, draw_map_solid, draw_map_cflag, draw_map_combotype, draw_map_comboiflag
static void z30d_draw_map_info(BITMAP *b, const mapscr &m, int x, int y, int what)
{
    const bool broken = q30_quest_rule(Z30D_QR_BROKEN_DRAWSCREEN_FUNCTIONS);
    BITMAP *square = create_bitmap_ex(8, 16, 16);
    BITMAP *subsquare = what == 1 ? create_bitmap_ex(8, 16, 16) : NULL;

    if(!square)
        return;

    if(subsquare)
        clear_to_color(subsquare, 1);

    for(int i = 0; i < 176 && i < (int)m.data.size(); ++i)
    {
        const int x2 = ((i & 15) << 4) + x;
        const int y2 = (i & 0xF0) + y;

        if(what == 1)
        {
            clear_bitmap(square);
            z30d_solid_square(square, subsquare, q30_script_combo_get(m.data[i], 2));
        }
        else
            clear_to_color(square, z30d_info_value(m, i, what));

        if(broken)
            blit(square, b, 0, 0, x2, y2, square->w, square->h);
        else
            masked_blit(square, b, 0, 0, x2, y2, square->w, square->h);
    }

    destroy_bitmap(square);

    if(subsquare)
        destroy_bitmap(subsquare);
}

// bitmap->DrawScreenSolid / Solid2 / ComboIFlags / ComboTypes(layer, map, screen, x, y, rotation)
// on the bitmap itself (upstream do_bmpdrawscreen_*). With rotation the screen is drawn at x, y
// of a 256x176 bitmap that is then rotated onto x, y, as upstream does.
static void z30d_screen_info(BITMAP *refbmp, int *sdci, int xoffset, int yoffset, int what)
{
    yoffset = z30d_crop_y(sdci, yoffset);
    const int map = (sdci[2] / 10000) - 1;
    const int screen = sdci[3] / 10000;
    const int x1 = sdci[4] / 10000 + xoffset, y1 = sdci[5] / 10000 + yoffset;
    const int rotation = sdci[6] / 10000;
    const unsigned index = (unsigned)(map * MAPSCRS + screen);

    if(index >= TheMaps.size())
    {
        al_trace("DrawScreen: invalid map or screen index. \n");
        return;
    }

    const mapscr &m = TheMaps[index];
    BITMAP *b = refbmp;

    if(rotation != 0)
        b = script_drawing_commands.AquireSubBitmap(256, 176);

    z30d_draw_map_info(b, m, x1, y1, what);

    // the layers' too: always for Solid2, with qr_BROKEN_DRAWSCREEN_FUNCTIONS for the others
    if(what == 1 || q30_quest_rule(Z30D_QR_BROKEN_DRAWSCREEN_FUNCTIONS))
    {
        for(int i = 0; i < 6; ++i)
        {
            if(m.layermap[i] == 0)
                continue;

            const unsigned li = (unsigned)((m.layermap[i] - 1) * MAPSCRS + m.layerscreen[i]);

            if(li >= TheMaps.size())
                continue;

            z30d_draw_map_info(b, TheMaps[li], x1, y1, what);
        }
    }

    if(rotation != 0)
    {
        rotate_sprite(refbmp, b, x1, y1, degrees_to_fixed(rotation));
        script_drawing_commands.ReleaseSubBitmap(b);
    }
}

// bitmap->DrawLayerSolid / Solidity / ComboFlags / ComboTypes / ComboIFlags(layer, map, screen,
// layer, x, y, rotation, noclip, opacity). Upstream draws these on the screen being drawn, not
// on the bitmap, and only when noclip is set (do_bmpdrawlayer*); each square is copied whole.
// DrawLayerSolid's square keeps the quarters of the combos before it, as upstream's does (it
// is cleared once here: upstream's starts as whatever memory it got).
static void z30d_layer_info(BITMAP *bmp, int *sdci, int xoffset, int yoffset, bool offscreen, int what)
{
    const int map = (sdci[2] / 10000) - 1;
    const int screen = sdci[3] / 10000;
    const int source_layer = vbound(sdci[4] / 10000, 0, 6);
    const int x1 = sdci[5] / 10000 + xoffset, y1 = sdci[6] / 10000 + yoffset;
    const int rotation = sdci[7] / 10000;
    const bool noclip = sdci[8] != 0;
    const unsigned index = (unsigned)(map * MAPSCRS + screen);
    const mapscr *m = getmapscreen(map, screen, source_layer);

    if(!m)
        return;

    if(index >= TheMaps.size())
    {
        al_trace("DrawLayer: invalid map index \"%u\". Map count is %u.\n", index, (unsigned)TheMaps.size());
        return;
    }

    const mapscr &l = *m;
    const int maxX = offscreen ? 512 : 256;
    const int maxY = offscreen ? 512 : 176 + yoffset;

    if(rotation != 0)
    {
        BITMAP *b = script_drawing_commands.AquireSubBitmap(256, 176);
        z30d_draw_map_info(b, l, x1, y1, what);
        rotate_sprite(bmp, b, x1, y1, degrees_to_fixed(rotation));
        script_drawing_commands.ReleaseSubBitmap(b);
        return;
    }

    BITMAP *square = create_bitmap_ex(8, 16, 16);
    BITMAP *subsquare = what == 1 ? create_bitmap_ex(8, 16, 16) : NULL;

    if(!square)
        return;

    clear_bitmap(square);

    if(subsquare)
        clear_to_color(subsquare, 1);

    for(int i = 0; i < 176 && i < (int)l.data.size(); ++i)
    {
        const int x2 = ((i & 15) << 4) + x1;
        const int y2 = (i & 0xF0) + y1;

        if(noclip && (x2 > -16 && x2 < maxX && y2 > -16 && y2 < maxY))
        {
            if(what == 1)
                z30d_solid_square(square, subsquare, q30_script_combo_get(l.data[i], 2));
            else
                clear_to_color(square, z30d_info_value(l, i, what));

            blit(square, bmp, 0, 0, x2, y2, square->w, square->h);
        }
    }

    destroy_bitmap(square);

    if(subsquare)
        destroy_bitmap(subsquare);
}

// ---- bitmap->Quad3D and ->Triangle3D (upstream bmp_do_drawquad3dr / bmp_do_drawtriangle3dr) ------
// [2..5] the arrays (in the vector: pos, uv, colours, size), [6] flip, [7] tile (or -combo),
// [8] polytype, [9] a bitmap to use as the texture instead (0: the tile).
static void z30d_tile_texture(BITMAP *tex, int tile, int w, int h, int cset, int flip)
{
    if(tile > 0)
        TileHelper::OverTile(tex, tile, 0, 0, w, h, cset, flip);
    else
    {
        const int cid = vbound(abs(tile), 0, MAXCOMBOS - 1);
        const newcombo &c = combobuf[cid];
        const int tiletodraw = combo_tile(c, 0, 0);

        if(!combo_editor_only(cid))
            TileHelper::OldPutTile(tex, tiletodraw, 0, 0, w, h, cset, flip ^ c.flip);
    }
}

static void z30d_bmp_poly3d(BITMAP *refbmp, int *sdci, std::vector<long> *vp, int xoffset, int yoffset, bool quad, BITMAP *screen_bmp)
{
    if(!vp || vp->empty())
        return;

    std::vector<long> &v = *vp;
    long *pos = &v[0];
    long *uv = &v[quad ? 12 : 9];
    long *col = &v[quad ? 20 : 15];
    long *size = &v[quad ? 24 : 18];
    const int w = size[0], h = size[1];
    int flip = (sdci[6] / 10000) & 3;
    const int tile = sdci[7] / 10000;
    const int polytype = vbound(sdci[8] / 10000, 0, 14);
    const long source = sdci[9];
    const char *name = quad ? "bitmap->Quad3D()" : "bitmap->Triangle3D()";
    BITMAP *bmptexture = source != 0 ? zasm30_script_bitmap(source, screen_bmp) : NULL;
    bool tex_is_bitmap = source != 0;

    yoffset = z30d_crop_y(sdci, yoffset);

    if(!quad && tex_is_bitmap && !bmptexture)
    {
        Z_scripterrlog("Bitmap pointer used as a texture in %s is uninitialised.\n Defaulting to using a tile as a texture.\n", "bitmap->Triangle3()");
        tex_is_bitmap = false;
    }

    if(!tex_is_bitmap || !quad)
    {
        if(w < 1 || h < 1 || ((w - 1) & w) != 0 || ((h - 1) & h) != 0)
        {
            Z_message(quad ? "Quad3d() : Args h, w, must be in powers of two! Power of 2 error with %i, %i."
                           : "bitmap->Triangle3d() : Args h, w, must be in powers of two! Power of 2 error with %i, %i.", w, h);
            return;
        }
    }

    if(quad && tex_is_bitmap && !bmptexture)
    {
        Z_scripterrlog("Bitmap pointer used as a texture in %s is uninitialised.\n Defaulting to using a tile as a texture.\n", name);
        return;
    }

    BITMAP *tex = NULL;
    bool must_destroy = false;

    if(!tex_is_bitmap)
    {
        tex = script_drawing_commands.GetSmallTextureBitmap(w, h);

        if(!tex)
        {
            must_destroy = true;
            tex = create_bitmap_ex(8, w * 16, h * 16);
            clear_bitmap(tex);
        }

        z30d_tile_texture(tex, tile, w, h, (int)col[0], flip);
    }
    else
    {
        if(!z30d_pow2(bmptexture->h))
            Z_scripterrlog("HEIGHT of Bitmap ( pointer %ld ) provided as a render source for %s is not a POWER OF TWO.\nTextels may render improperly!\n", source, name);
        if(!z30d_pow2(bmptexture->w))
            Z_scripterrlog("WIDTH of Bitmap ( pointer %ld ) provided as a render source for %s is not a POWER OF TWO.\nTextels may render improperly!\n", source, name);
    }

    BITMAP *t = tex_is_bitmap ? bmptexture : tex;
    V3D_f V1 = { static_cast<float>(pos[0] + xoffset), static_cast<float>(pos[1] + yoffset), static_cast<float>(pos[2]), static_cast<float>(uv[0]), static_cast<float>(uv[1]), (int)col[0] };
    V3D_f V2 = { static_cast<float>(pos[3] + xoffset), static_cast<float>(pos[4] + yoffset), static_cast<float>(pos[5]), static_cast<float>(uv[2]), static_cast<float>(uv[3]), (int)col[1] };
    V3D_f V3 = { static_cast<float>(pos[6] + xoffset), static_cast<float>(pos[7] + yoffset), static_cast<float>(pos[8]), static_cast<float>(uv[4]), static_cast<float>(uv[5]), (int)col[2] };

    if(quad)
    {
        V3D_f V4 = { static_cast<float>(pos[9] + xoffset), static_cast<float>(pos[10] + yoffset), static_cast<float>(pos[11]), static_cast<float>(uv[6]), static_cast<float>(uv[7]), (int)col[3] };
        quad3d_f(refbmp, polytype, t, &V1, &V2, &V3, &V4);
    }
    else
        triangle3d_f(refbmp, polytype, t, &V1, &V2, &V3);

    if(must_destroy)
        destroy_bitmap(tex);
}

// ---- bitmap->DrawPlane (upstream bmp_do_mode7r) -------------------------------------------------
// The command's own bitmap (sourceBitmap) is drawn line by line onto destBMP as a plane seen
// in perspective. [3], [4] the source position, [6] dest y, [7], [8] dest size, [9] space z,
// [10] horizon, [11], [12] scale, [13] masked. (Upstream never adds the x offset.)
static void z30d_mode7(BITMAP *destBMP, BITMAP *sourceBitmap, int *sdci, int yoffset)
{
    const double srcX = sdci[3] / 10000.0, srcY = sdci[4] / 10000.0;
    const double destY = sdci[6] / 10000.0, destW = sdci[7] / 10000.0, destH = sdci[8] / 10000.0;
    const double space_z = sdci[9] / 10000.0, horizon = sdci[10] / 10000.0;
    const double scale_x = sdci[11] / 10000.0, scale_y = sdci[12] / 10000.0;
    const bool masked = sdci[13] != 0;
    const int screen_x = 0;

    for(int screen_y = 0; screen_y < destH; screen_y++)
    {
        const int screen_y_horizon = int(screen_y + horizon);
        const double distance = ((space_z * scale_y) / ((screen_y_horizon != 0) ? screen_y_horizon : 1));
        const double horizontal_scale = (distance / ((scale_x != 0) ? scale_x : 1));
        const double line_dx = horizontal_scale, line_dy = 0;
        const int space_x = int(srcX - destW / 2.0 * line_dx);
        const int space_y = int(srcY - distance + destH / 2.0 * line_dy);
        const int y1 = int(srcY + space_y);
        const int y2 = int(destY + screen_y);

        if(y1 >= 0 && y1 <= (sourceBitmap->h - 1) && y2 >= 0 && y2 <= (destBMP->h - 1))
        {
            if(masked)
                masked_stretch_blit(sourceBitmap, destBMP, (int)(srcX + space_x), (int)(srcY + space_y),
                                    (int)(line_dx * destW), 1, (int)(screen_x), (int)(screen_y) + yoffset, (int)(destW), 1);
            else
                stretch_blit(sourceBitmap, destBMP, (int)(srcX + space_x), (int)(srcY + space_y),
                             (int)(line_dx * destW), 1, (int)(screen_x), (int)(screen_y) + yoffset, (int)(destW), 1);
        }
    }
}

// A bitmap's pixels kept in a command's vector: width, height, then the pixels.
static BITMAP *z30d_unpack_bitmap(std::vector<long> *vp)
{
    if(!vp || vp->size() < 2)
        return NULL;

    const int w = (int)(*vp)[0], h = (int)(*vp)[1];

    if(w <= 0 || h <= 0 || (long)vp->size() != 2 + (long)w * h)
        return NULL;

    BITMAP *b = create_bitmap_ex(8, w, h);

    if(!b)
        return NULL;

    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
            b->line[y][x] = (unsigned char)(*vp)[2 + (size_t)y * w + x];

    return b;
}

#include "q30_draws_blit.h"

// Runs one of these commands on bmp: from do_primitives for a queued command (bmp is the layer
// or the render target being drawn, the offsets its draw origin's), or at once on a script's
// bitmap (offsets 0). False: not one of these commands.
bool zasm30_draw_cmd(BITMAP *bmp, int *sdci, std::vector<long> *v, int xoffset, int yoffset, bool offscreen)
{
    if(sdci[0] < ZASM30_DRAW_BASE || !bmp)
        return false;

    COLOR_MAP *saved_map = color_map;
    color_map = &trans_table2;

    switch(sdci[0] - ZASM30_DRAW_BASE)
    {
    case Z30C_FRAMER: z30d_framer(bmp, sdci, xoffset, yoffset); break;
    case Z30C_BMPFRAMER: z30d_framer(bmp, sdci, xoffset, z30d_crop_y(sdci, yoffset)); break;
    case Z30C_PIXELARRAYR: z30d_putpixels(bmp, sdci, v, xoffset, yoffset); break;
    case Z30C_TILEARRAYR: z30d_fasttiles(bmp, v, xoffset, yoffset); break;
    case Z30C_LINESARRAY: z30d_lines(bmp, v, xoffset, yoffset); break;
    case Z30C_COMBOARRAYR: z30d_fastcombos(bmp, v, xoffset, yoffset); break;
    case Z30C_DRAWMINITILE: z30d_minitile(bmp, sdci, xoffset, yoffset); break;
    case Z30C_BMPDRAWMINITILE: z30d_minitile(bmp, sdci, xoffset, z30d_crop_y(sdci, yoffset)); break;
    case Z30C_DRAWTILECLOAKEDR: z30d_tilecloaked(bmp, sdci, xoffset, yoffset); break;
    case Z30C_BMPDRAWTILECLOAKEDR: z30d_tilecloaked(bmp, sdci, xoffset, z30d_crop_y(sdci, yoffset)); break;
    case Z30C_DRAWCOMBOCLOAKEDR: z30d_combocloaked(bmp, sdci, xoffset, yoffset); break;
    case Z30C_BMPDRAWCOMBOCLOAKEDR: z30d_combocloaked(bmp, sdci, xoffset, z30d_crop_y(sdci, yoffset)); break;
    case Z30C_DRAWMAPDATA: z30d_mapdata(bmp, sdci, v, xoffset, yoffset); break;
    case Z30C_BMPDRAWMAPDATA: z30d_mapdata(bmp, sdci, v, xoffset, z30d_crop_y(sdci, yoffset)); break;
    case Z30C_BITMAPEXR: z30d_drawbitmapexr(bmp, sdci, xoffset, yoffset); break;
    case Z30C_TILEBLIT: z30d_tileblit(bmp, sdci, xoffset, yoffset, false, "TileBlit()"); break;
    case Z30C_COMBOBLIT: z30d_comboblit(bmp, sdci, xoffset, yoffset, false); break;
    case Z30C_BMPTILEBLIT: z30d_tileblit(bmp, sdci, xoffset, yoffset, true, "TileBlit()"); break;
    case Z30C_BMPCOMBOBLIT: z30d_comboblit(bmp, sdci, xoffset, yoffset, true); break;
    case Z30C_BMPDRAWSCREENSOLIDR: z30d_screen_info(bmp, sdci, xoffset, yoffset, 0); break;
    case Z30C_BMPDRAWSCREENSOLID2R: z30d_screen_info(bmp, sdci, xoffset, yoffset, 1); break;
    case Z30C_BMPDRAWSCREENCOMBOTR: z30d_screen_info(bmp, sdci, xoffset, yoffset, 3); break;
    case Z30C_BMPDRAWSCREENCOMBOIR: z30d_screen_info(bmp, sdci, xoffset, yoffset, 4); break;
    case Z30C_BMPDRAWLAYERSOLIDR: z30d_layer_info(bmp, sdci, xoffset, yoffset, offscreen, 1); break;
    case Z30C_BMPDRAWLAYERSOLIDITYR: z30d_layer_info(bmp, sdci, xoffset, yoffset, offscreen, 0); break;
    case Z30C_BMPDRAWLAYERCFLAGR: z30d_layer_info(bmp, sdci, xoffset, yoffset, offscreen, 2); break;
    case Z30C_BMPDRAWLAYERCTYPER: z30d_layer_info(bmp, sdci, xoffset, yoffset, offscreen, 3); break;
    case Z30C_BMPDRAWLAYERCIFLAGR: z30d_layer_info(bmp, sdci, xoffset, yoffset, offscreen, 4); break;
    case Z30C_BMPQUAD3DR: z30d_bmp_poly3d(bmp, sdci, v, xoffset, yoffset, true, bmp); break;
    case Z30C_BMPTRIANGLE3DR: z30d_bmp_poly3d(bmp, sdci, v, xoffset, yoffset, false, bmp); break;
    case Z30C_BMPMODE7:
    {
        // queued: drawn on the screen, the source bitmap as it was when the command was given
        // (the screen itself when that is the source)
        if(BITMAP *src = z30d_unpack_bitmap(v))
        {
            z30d_mode7(bmp, src, sdci, z30d_crop_y(sdci, yoffset));
            destroy_bitmap(src);
        }
        else if(sdci[Z30D_TARGET] == 9)
            z30d_mode7(bmp, bmp, sdci, yoffset);
        break;
    }
    }

    color_map = saved_map;
    return true;
}

// bitmap->DrawPlane at once: src is the command's bitmap, dest the one it draws on.
void zasm30_draw_mode7(BITMAP *dest, BITMAP *src, int *sdci, int yoffset)
{
    z30d_mode7(dest, src, sdci, yoffset);
}

#endif

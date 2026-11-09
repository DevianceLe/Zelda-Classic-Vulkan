/*
 * test_blit_polydbg.cpp - small 3D polygon cases, one check_bitmap each, so
 * a mismatch shows the differing pixels (diagnostics for test_blit_poly3d).
 */
#include "conf.h"

namespace
{

BITMAP *coord_texture(int w, int h)
{
    BITMAP *t = create_bitmap_ex(8, w, h);
    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
            t->line[y][x] = (unsigned char)(1 + (x & 15) + (y & 15) * 16);
    return t;
}

V3D_f vtx(float x, float y, float z, float u, float v, int c)
{
    V3D_f r;
    r.x = x; r.y = y; r.z = z; r.u = u; r.v = v; r.c = c;
    return r;
}

void run(conf::Ctx &ctx, const char *name, int type, BITMAP *tex, int n, const float *xy, const float *uv,
         float z, const int *c)
{
    BITMAP *dst = create_bitmap_ex(8, 40, 40);
    V3D_f v[4];
    for(int i = 0; i < n; ++i)
        v[i] = vtx(xy[i * 2], xy[i * 2 + 1], z, uv[i * 2], uv[i * 2 + 1], c[i]);
    clear_to_color(dst, 0);
    if(n == 4)
        quad3d_f(dst, type, tex, &v[0], &v[1], &v[2], &v[3]);
    else
        triangle3d_f(dst, type, tex, &v[0], &v[1], &v[2]);
    ctx.check_bitmap(name, dst);
    destroy_bitmap(dst);
}

} // namespace

CONF_TEST(polydbg)
{
    BITMAP *tex = coord_texture(16, 16);
    const int c4[4] = { 5, 5, 5, 5 };
    const int cg[4] = { 16, 64, 160, 250 };
    const float sq[8] = { 10, 10, 10, 26, 26, 26, 26, 10 };
    const float squv[8] = { 0, 0, 0, 16, 16, 16, 16, 0 };
    const float fr[8] = { 10.5f, 10.25f, 9.75f, 26.5f, 26.25f, 25.5f, 25.5f, 9.5f };
    const float tri[6] = { 5, 3, 12, 30, 33, 17 };
    const float triuv[6] = { 0, 0, 0, 16, 16, 16 };
    const float rot[8] = { 12, 2, 4, 20, 22, 30, 30, 12 };
    const float half[8] = { 10.5f, 10.5f, 10.5f, 26.5f, 26.5f, 26.5f, 26.5f, 10.5f };

    run(ctx, "flat_sq", POLYTYPE_FLAT, tex, 4, sq, squv, 0, c4);
    run(ctx, "flat_fr", POLYTYPE_FLAT, tex, 4, fr, squv, 0, c4);
    run(ctx, "flat_half", POLYTYPE_FLAT, tex, 4, half, squv, 0, c4);
    run(ctx, "flat_rot", POLYTYPE_FLAT, tex, 4, rot, squv, 0, c4);
    run(ctx, "flat_tri", POLYTYPE_FLAT, tex, 3, tri, triuv, 0, c4);
    run(ctx, "gcol_sq", POLYTYPE_GCOL, tex, 4, sq, squv, 0, cg);
    run(ctx, "gcol_rot", POLYTYPE_GCOL, tex, 4, rot, squv, 0, cg);
    run(ctx, "gcol_tri", POLYTYPE_GCOL, tex, 3, tri, triuv, 0, cg);
    run(ctx, "atex_sq", POLYTYPE_ATEX, tex, 4, sq, squv, 0, c4);
    run(ctx, "atex_fr", POLYTYPE_ATEX, tex, 4, fr, squv, 0, c4);
    run(ctx, "atex_rot", POLYTYPE_ATEX, tex, 4, rot, squv, 0, c4);
    run(ctx, "atex_tri", POLYTYPE_ATEX, tex, 3, tri, triuv, 0, c4);
    run(ctx, "ptex_sq_z0", POLYTYPE_PTEX, tex, 4, sq, squv, 0, c4);
    run(ctx, "ptex_sq_z1", POLYTYPE_PTEX, tex, 4, sq, squv, 1, c4);
    run(ctx, "ptex_rot_z1", POLYTYPE_PTEX, tex, 4, rot, squv, 1, c4);
    run(ctx, "ptex_tri_z0", POLYTYPE_PTEX, tex, 3, tri, triuv, 0, c4);
    run(ctx, "ptex_tri_z1", POLYTYPE_PTEX, tex, 3, tri, triuv, 1, c4);
    destroy_bitmap(tex);
}

CONF_TEST(polydbg3)
{
    /* perspective: z changes across the polygon */
    BITMAP *tex = coord_texture(16, 16);
    BITMAP *dst = create_bitmap_ex(8, 40, 40);
    const float zs[][4] = { {1, 2, 3, 4}, {1, 1, 4, 4}, {2, 5, 2, 5}, {1.5f, 7, 3.25f, 10} };
    for(int k = 0; k < 4; ++k)
    {
        V3D_f v1 = vtx(4, 4, zs[k][0], 0, 0, 1), v2 = vtx(4, 36, zs[k][1], 0, 16, 1),
              v3 = vtx(36, 36, zs[k][2], 16, 16, 1), v4 = vtx(36, 4, zs[k][3], 16, 0, 1);
        clear_to_color(dst, 0);
        quad3d_f(dst, POLYTYPE_PTEX, tex, &v1, &v2, &v3, &v4);
        ctx.check_bitmap(conf::nm("ptex_z%d", k), dst);
    }
    /* z of mixed sign and zero (scripts can pass anything) */
    const float zm[][4] = { {-6, 3, 0, 9}, {7, 9, 266, -43}, {-6, 9, 1329, -3}, {1, -1, 1, -1} };
    for(int k = 0; k < 4; ++k)
    {
        V3D_f v1 = vtx(4, 4, zm[k][0], 0, 0, 1), v2 = vtx(4, 36, zm[k][1], 0, 16, 1),
              v3 = vtx(36, 36, zm[k][2], 16, 16, 1), v4 = vtx(36, 4, zm[k][3], 16, 0, 1);
        clear_to_color(dst, 0);
        quad3d_f(dst, POLYTYPE_PTEX, tex, &v1, &v2, &v3, &v4);
        ctx.check_bitmap(conf::nm("ptex_mixed%d", k), dst);
    }
    destroy_bitmap(dst);
    destroy_bitmap(tex);
}

CONF_TEST(polydbg5)
{
    /* perspective with constant z: per polygon, per row only, triangles */
    BITMAP *tex = coord_texture(16, 16);
    BITMAP *dst = create_bitmap_ex(8, 48, 48);
    const float zc[] = { 1, 2, 0.5f, 3.7f, 1000 };
    const float shapes[][8] = {
        { 12, 2, 4, 20, 22, 30, 30, 12 },
        { 3, 9, 40, 30, 30, 4, 9, 38 },
        { 5, 5, 41, 7, 39, 44, 2, 40 },
    };
    for(int zi = 0; zi < 5; ++zi)
        for(int si = 0; si < 3; ++si)
        {
            const float *p = shapes[si];
            V3D_f v1 = vtx(p[0], p[1], zc[zi], 0, 0, 1), v2 = vtx(p[2], p[3], zc[zi], 0, 16, 1),
                  v3 = vtx(p[4], p[5], zc[zi], 16, 16, 1), v4 = vtx(p[6], p[7], zc[zi], 16, 0, 1);
            clear_to_color(dst, 0);
            quad3d_f(dst, POLYTYPE_PTEX, tex, &v1, &v2, &v3, &v4);
            ctx.check_bitmap(conf::nm("quad_z%d_s%d", zi, si), dst);
            clear_to_color(dst, 0);
            triangle3d_f(dst, POLYTYPE_PTEX, tex, &v1, &v2, &v3);
            ctx.check_bitmap(conf::nm("tri_z%d_s%d", zi, si), dst);
        }
    /* axis-aligned quad, z changes only from top to bottom: every row has
     * constant z, the polygon does not */
    for(int k = 0; k < 3; ++k)
    {
        const float zt = 1.0f + k, zb = 2.5f + 3 * k;
        V3D_f v1 = vtx(3, 3, zt, 0, 0, 1), v2 = vtx(3, 43, zb, 0, 16, 1), v3 = vtx(43, 43, zb, 16, 16, 1),
              v4 = vtx(43, 3, zt, 16, 0, 1);
        clear_to_color(dst, 0);
        quad3d_f(dst, POLYTYPE_PTEX, tex, &v1, &v2, &v3, &v4);
        ctx.check_bitmap(conf::nm("rows_k%d", k), dst);
    }
    destroy_bitmap(dst);
    destroy_bitmap(tex);
}

CONF_TEST(polydbg4)
{
    /* Screen->Triangle cases */
    BITMAP *tex = coord_texture(16, 16);
    BITMAP *dst = create_bitmap_ex(8, 48, 48);
    const float sh[][6] = {
        { 30, 30, 20, 50, 50, 60 },
        { 3, 2, 9, 38, 40, 30 },
        { 26, 10, 26, 26, 10, 26 },
        { 12, 1, 2, 30, 44, 40 },
    };
    for(int k = 0; k < 4; ++k)
    {
        V3D_f v1 = vtx(sh[k][0], sh[k][1], 0, 0, 0, 1), v2 = vtx(sh[k][2], sh[k][3], 0, 0, 16, 1),
              v3 = vtx(sh[k][4], sh[k][5], 0, 16, 16, 1);
        clear_to_color(dst, 0);
        triangle3d_f(dst, POLYTYPE_ATEX, tex, &v1, &v2, &v3);
        ctx.check_bitmap(conf::nm("tri%d", k), dst);
    }
    destroy_bitmap(dst);
    destroy_bitmap(tex);
}

CONF_TEST(polydbg2)
{
    BITMAP *tex = coord_texture(16, 32);
    BITMAP *dst = create_bitmap_ex(8, 64, 64);
    {
        V3D_f v1 = vtx(16, 7, 847, 0, 0, 9), v2 = vtx(23, 3, 1645, 0, 0, 9), v3 = vtx(-12, 46, 9, 0, 0, 9),
              v4 = vtx(60, 72, 0, 0, 0, 9);
        clear_to_color(dst, 0);
        quad3d_f(dst, POLYTYPE_FLAT, tex, &v1, &v2, &v3, &v4);
        ctx.check_bitmap("flat_cross", dst);
    }
    {
        V3D_f v1 = vtx(3, 2, 0, 0, 0, 1), v2 = vtx(9, 38, 0, 0, 32, 1), v3 = vtx(40, 30, 0, 16, 32, 1);
        clear_to_color(dst, 0);
        triangle3d_f(dst, POLYTYPE_ATEX, tex, &v1, &v2, &v3);
        ctx.check_bitmap("atex_tri3", dst);
        clear_to_color(dst, 0);
        quad3d_f(dst, POLYTYPE_ATEX, tex, &v1, &v2, &v3, &v3);
        ctx.check_bitmap("atex_tri3_as_quad", dst);
    }
    {
        V3D_f v1 = vtx(32, 42, 8, 0, 0, 9), v2 = vtx(2, -17, 7, 0, 0, 9), v3 = vtx(-8, 68, 1791, 0, 0, 9),
              v4 = vtx(40, 23, 0, 0, 0, 9);
        clear_to_color(dst, 0);
        quad3d_f(dst, POLYTYPE_FLAT, tex, &v1, &v2, &v3, &v4);
        ctx.check_bitmap("flat_q3", dst);
    }
    {
        V3D_f v1 = vtx(78, -20, 0, 0, 0, 9), v2 = vtx(70, 70, 0, 0, 0, 9), v3 = vtx(10, 2, 0, 0, 0, 9),
              v4 = vtx(-6, 59, 0, 0, 0, 9);
        clear_to_color(dst, 0);
        quad3d_f(dst, POLYTYPE_FLAT, tex, &v1, &v2, &v3, &v4);
        ctx.check_bitmap("flat_q5", dst);
    }
    {
        const float q[8][4][2] = {
            { {56, -10}, {58, 66}, {26, 17}, {43, -17} },
            { {37, 64}, {66, 27}, {51, 32}, {67, 3} },
            { {60, 43}, {71, 17}, {32, 21}, {26, 9} },
            { {79, 9}, {38, 13}, {11, 73}, {69, 8} },
            { {54, 14}, {57, -4}, {54, 29}, {41, -1} },
            { {-2, -4}, {4, 70}, {37, -4}, {13, 17} },
            { {40, -18}, {-3, 56}, {51, -4}, {68, 74} },
            { {-6, -2}, {-9, 21}, {17, -15}, {33, 62} },
        };
        for(int k = 0; k < 8; ++k)
        {
            V3D_f v1 = vtx(q[k][0][0], q[k][0][1], 1, 0, 0, 9), v2 = vtx(q[k][1][0], q[k][1][1], 1, 0, 0, 9),
                  v3 = vtx(q[k][2][0], q[k][2][1], 1, 0, 0, 9), v4 = vtx(q[k][3][0], q[k][3][1], 1, 0, 0, 9);
            clear_to_color(dst, 0);
            quad3d_f(dst, POLYTYPE_FLAT, tex, &v1, &v2, &v3, &v4);
            ctx.check_bitmap(conf::nm("flat_r%d", k), dst);
        }
    }
    destroy_bitmap(dst);
    destroy_bitmap(tex);
}

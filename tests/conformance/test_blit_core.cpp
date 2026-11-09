/*
 * test_blit_core.cpp - blits and sprites must be pixel-exact against the
 * reference build.
 *
 * blit (every depth, clipping, sub-bitmaps, overlapping self-blits),
 * masked_blit, stretch_blit, masked_stretch_blit, stretch_sprite,
 * draw_sprite and its flipped forms, draw_sprite_ex, draw_trans_sprite and
 * draw_lit_sprite through a fixed 8-bit COLOR_MAP, draw_character_ex.
 *
 * Bitmaps are filled with random raw pixel values (some of them the mask
 * color); inputs stay inside what Allegro defines (valid source rectangles
 * for the stretchers, in-bitmap coordinates when clipping is off).
 */
#include "conf.h"

#include <string.h>

namespace
{

const int kDepths[] = { 8, 15, 16, 24, 32 };
const int kNumDepths = 5;

/* Writes a raw pixel value (no clipping, no drawing mode). */
void raw_put(BITMAP *b, int x, int y, unsigned c)
{
    switch(bitmap_color_depth(b))
    {
    case 8:
        b->line[y][x] = (unsigned char)c;
        break;
    case 15:
    case 16:
        ((unsigned short *)b->line[y])[x] = (unsigned short)c;
        break;
    case 24:
        b->line[y][x * 3 + 0] = (unsigned char)c;
        b->line[y][x * 3 + 1] = (unsigned char)(c >> 8);
        b->line[y][x * 3 + 2] = (unsigned char)(c >> 16);
        break;
    default:
        ((unsigned int *)b->line[y])[x] = c;
        break;
    }
}

unsigned random_pixel(conf::Rng &rng, int depth)
{
    switch(depth)
    {
    case 8: return (unsigned)rng.range(1, 255);
    case 15:
    case 16: return (unsigned)rng.bits(16);
    case 24: return (unsigned)rng.bits(24);
    default: return rng.next();
    }
}

/* Random pixels, mask_pct percent of them the mask color. */
void fill_random(BITMAP *b, conf::Rng &rng, int mask_pct)
{
    int depth = bitmap_color_depth(b);
    unsigned mask = (unsigned)bitmap_mask_color(b);
    for(int y = 0; y < b->h; ++y)
        for(int x = 0; x < b->w; ++x)
            raw_put(b, x, y, (rng.range(0, 99) < mask_pct) ? mask : random_pixel(rng, depth));
}

/* Random clip rectangle inside the bitmap (or the full bitmap). */
void random_clip(BITMAP *b, conf::Rng &rng)
{
    if(rng.range(0, 2) == 0)
    {
        set_clip_rect(b, 0, 0, b->w - 1, b->h - 1);
        return;
    }
    int x1 = rng.range(0, b->w - 1), x2 = rng.range(0, b->w - 1);
    int y1 = rng.range(0, b->h - 1), y2 = rng.range(0, b->h - 1);
    if(x2 < x1) { int t = x1; x1 = x2; x2 = t; }
    if(y2 < y1) { int t = y1; y1 = y2; y2 = t; }
    set_clip_rect(b, x1, y1, x2, y2);
}

int coord(conf::Rng &rng, int size)
{
    switch(rng.range(0, 7))
    {
    case 0: return rng.range(-2 * size, 3 * size);
    case 1: return rng.range(-3, 3);
    case 2: return size + rng.range(-3, 3);
    default: return rng.range(-size / 2, size);
    }
}

/* A deterministic, asymmetric "translucency" table. */
COLOR_MAP g_map;
void init_map()
{
    for(int a = 0; a < PAL_SIZE; ++a)
        for(int b = 0; b < PAL_SIZE; ++b)
            g_map.data[a][b] = (unsigned char)((a * 7 + b * 3 + (a ^ b) + 1) & 0xFF);
}

/* Destination: either a plain bitmap or a sub-bitmap of a bigger one. Both
 * are filled; the root is what gets checked. */
struct Dest
{
    BITMAP *root;
    BITMAP *bmp;
};

Dest make_dest(conf::Rng &rng, int depth, int w, int h)
{
    Dest d;
    if(rng.range(0, 3) == 0)
    {
        d.root = create_bitmap_ex(depth, w + 9, h + 7);
        fill_random(d.root, rng, 10);
        d.bmp = create_sub_bitmap(d.root, rng.range(0, 9), rng.range(0, 7), w, h);
    }
    else
    {
        d.root = create_bitmap_ex(depth, w, h);
        fill_random(d.root, rng, 10);
        d.bmp = d.root;
    }
    random_clip(d.bmp, rng);
    return d;
}

void free_dest(Dest &d)
{
    if(d.bmp != d.root)
        destroy_bitmap(d.bmp);
    destroy_bitmap(d.root);
}

/* Source: plain bitmap or a sub-bitmap. */
struct Src
{
    BITMAP *root;
    BITMAP *bmp;
};

Src make_src(conf::Rng &rng, int depth, int w, int h, int mask_pct)
{
    Src s;
    if(rng.range(0, 3) == 0)
    {
        s.root = create_bitmap_ex(depth, w + 5, h + 4);
        fill_random(s.root, rng, mask_pct);
        s.bmp = create_sub_bitmap(s.root, rng.range(0, 5), rng.range(0, 4), w, h);
    }
    else
    {
        s.root = create_bitmap_ex(depth, w, h);
        fill_random(s.root, rng, mask_pct);
        s.bmp = s.root;
    }
    return s;
}

void free_src(Src &s)
{
    if(s.bmp != s.root)
        destroy_bitmap(s.bmp);
    destroy_bitmap(s.root);
}

} // namespace

/* ------------------------------------------------------------------------- */

CONF_TEST(blit_same_depth)
{
    for(int di = 0; di < kNumDepths; ++di)
    {
        int depth = kDepths[di];
        conf::Seq s(ctx, conf::nm("depth%d", depth));
        for(int i = 0; i < 300; ++i)
        {
            Src src = make_src(ctx.rng, depth, ctx.rng.range(1, 30), ctx.rng.range(1, 30), 20);
            Dest dst = make_dest(ctx.rng, depth, ctx.rng.range(1, 40), ctx.rng.range(1, 40));
            int sx = coord(ctx.rng, src.bmp->w), sy = coord(ctx.rng, src.bmp->h);
            int dx = coord(ctx.rng, dst.bmp->w), dy = coord(ctx.rng, dst.bmp->h);
            int w = ctx.rng.range(-2, 45), h = ctx.rng.range(-2, 45);
            blit(src.bmp, dst.bmp, sx, sy, dx, dy, w, h);
            s.add_bitmap(dst.root, "i=%d s=%d,%d d=%d,%d %dx%d", i, sx, sy, dx, dy, w, h);
            free_src(src);
            free_dest(dst);
        }
    }
}

CONF_TEST(blit_overlap)
{
    for(int di = 0; di < kNumDepths; ++di)
    {
        int depth = kDepths[di];
        conf::Seq s(ctx, conf::nm("depth%d", depth));
        for(int i = 0; i < 200; ++i)
        {
            BITMAP *b = create_bitmap_ex(depth, ctx.rng.range(4, 40), ctx.rng.range(4, 40));
            fill_random(b, ctx.rng, 10);
            BITMAP *from = b, *to = b, *sub = NULL;
            if(ctx.rng.range(0, 2) == 0)
            {
                /* sub-bitmap overlapping its parent */
                sub = create_sub_bitmap(b, ctx.rng.range(0, b->w / 2), ctx.rng.range(0, b->h / 2),
                                        ctx.rng.range(1, b->w / 2 + 1), ctx.rng.range(1, b->h / 2 + 1));
                if(ctx.rng.range(0, 1))
                    from = sub;
                else
                    to = sub;
            }
            int sx = ctx.rng.range(-5, from->w), sy = ctx.rng.range(-5, from->h);
            int dx = sx + ctx.rng.range(-6, 6), dy = sy + ctx.rng.range(-6, 6);
            int w = ctx.rng.range(1, b->w + 4), h = ctx.rng.range(1, b->h + 4);
            blit(from, to, sx, sy, dx, dy, w, h);
            s.add_bitmap(b, "i=%d s=%d,%d d=%d,%d %dx%d sub=%d", i, sx, sy, dx, dy, w, h, sub != NULL);
            if(sub)
                destroy_bitmap(sub);
            destroy_bitmap(b);
        }
    }
}

CONF_TEST(blit_clip_off)
{
    /* With clipping disabled, coordinates stay inside both bitmaps. */
    for(int di = 0; di < kNumDepths; ++di)
    {
        int depth = kDepths[di];
        conf::Seq s(ctx, conf::nm("depth%d", depth));
        for(int i = 0; i < 100; ++i)
        {
            BITMAP *src = create_bitmap_ex(depth, 24, 20);
            BITMAP *dst = create_bitmap_ex(depth, 30, 26);
            fill_random(src, ctx.rng, 25);
            fill_random(dst, ctx.rng, 10);
            set_clip_rect(dst, 5, 4, 20, 18);
            set_clip_state(dst, 0);
            int w = ctx.rng.range(1, 24), h = ctx.rng.range(1, 20);
            int sx = ctx.rng.range(0, 24 - w), sy = ctx.rng.range(0, 20 - h);
            int dx = ctx.rng.range(0, 30 - w), dy = ctx.rng.range(0, 26 - h);
            switch(i % 4)
            {
            case 0: blit(src, dst, sx, sy, dx, dy, w, h); break;
            case 1: masked_blit(src, dst, sx, sy, dx, dy, w, h); break;
            case 2:
            {
                BITMAP *spr = create_sub_bitmap(src, sx, sy, w, h);
                draw_sprite(dst, spr, dx, dy);
                destroy_bitmap(spr);
                break;
            }
            default:
                stretch_blit(src, dst, sx, sy, w, h, dx, dy, ctx.rng.range(1, 30 - dx), ctx.rng.range(1, 26 - dy));
                break;
            }
            s.add_bitmap(dst, "i=%d s=%d,%d d=%d,%d %dx%d", i, sx, sy, dx, dy, w, h);
            destroy_bitmap(src);
            destroy_bitmap(dst);
        }
    }
}

CONF_TEST(masked_blit_all)
{
    for(int di = 0; di < kNumDepths; ++di)
    {
        int depth = kDepths[di];
        conf::Seq s(ctx, conf::nm("depth%d", depth));
        for(int i = 0; i < 300; ++i)
        {
            Src src = make_src(ctx.rng, depth, ctx.rng.range(1, 30), ctx.rng.range(1, 30), 30);
            Dest dst = make_dest(ctx.rng, depth, ctx.rng.range(1, 40), ctx.rng.range(1, 40));
            int sx = coord(ctx.rng, src.bmp->w), sy = coord(ctx.rng, src.bmp->h);
            int dx = coord(ctx.rng, dst.bmp->w), dy = coord(ctx.rng, dst.bmp->h);
            int w = ctx.rng.range(-2, 45), h = ctx.rng.range(-2, 45);
            masked_blit(src.bmp, dst.bmp, sx, sy, dx, dy, w, h);
            s.add_bitmap(dst.root, "i=%d s=%d,%d d=%d,%d %dx%d", i, sx, sy, dx, dy, w, h);
            free_src(src);
            free_dest(dst);
        }
    }
}

namespace
{

/* stretch_blit, masked_stretch_blit or stretch_sprite with a valid source
 * rectangle and random destination rectangles. */
void stretch_cases(conf::Ctx &ctx, int which, int count)
{
    for(int di = 0; di < kNumDepths; ++di)
    {
        int depth = kDepths[di];
        conf::Seq s(ctx, conf::nm("depth%d", depth));
        for(int i = 0; i < count; ++i)
        {
            Src src = make_src(ctx.rng, depth, ctx.rng.range(1, 24), ctx.rng.range(1, 24), 30);
            Dest dst = make_dest(ctx.rng, depth, ctx.rng.range(1, 48), ctx.rng.range(1, 48));
            int sx = ctx.rng.range(0, src.bmp->w - 1), sy = ctx.rng.range(0, src.bmp->h - 1);
            int sw = ctx.rng.range(1, src.bmp->w - sx), sh = ctx.rng.range(1, src.bmp->h - sy);
            int dx = coord(ctx.rng, dst.bmp->w), dy = coord(ctx.rng, dst.bmp->h);
            int dw, dh;
            switch(ctx.rng.range(0, 5))
            {
            case 0: dw = ctx.rng.range(-3, 2); dh = ctx.rng.range(-3, 60); break;
            case 1: dw = sw; dh = sh; break;
            case 2: dw = sw * ctx.rng.range(1, 4); dh = sh * ctx.rng.range(1, 4); break;
            case 3: dw = ctx.rng.range(100, 400); dh = ctx.rng.range(1, 300); break;
            default: dw = ctx.rng.range(1, 60); dh = ctx.rng.range(1, 60); break;
            }
            if(which == 0)
                stretch_blit(src.bmp, dst.bmp, sx, sy, sw, sh, dx, dy, dw, dh);
            else if(which == 1)
                masked_stretch_blit(src.bmp, dst.bmp, sx, sy, sw, sh, dx, dy, dw, dh);
            else
                stretch_sprite(dst.bmp, src.bmp, dx, dy, dw, dh);
            s.add_bitmap(dst.root, "i=%d s=%d,%d %dx%d d=%d,%d %dx%d", i, sx, sy, sw, sh, dx, dy, dw, dh);
            free_src(src);
            free_dest(dst);
        }
    }
}

} // namespace

CONF_TEST(stretch_blit_all)
{
    stretch_cases(ctx, 0, 300);
}

CONF_TEST(masked_stretch_blit_all)
{
    stretch_cases(ctx, 1, 300);
}

CONF_TEST(stretch_sprite_all)
{
    stretch_cases(ctx, 2, 300);
}

CONF_TEST(stretch_blit_mapping)
{
    /* Which source column feeds each destination column: source pixels are
     * their own x index, so the destination row reads back the mapping. */
    BITMAP *src = create_bitmap_ex(8, 250, 1);
    for(int x = 0; x < 250; ++x)
        src->line[0][x] = (unsigned char)x;
    BITMAP *dst = create_bitmap_ex(8, 300, 1);
    conf::Seq s(ctx, "columns");
    for(int sw = 1; sw <= 40; ++sw)
        for(int dw = 1; dw <= 60; dw += (dw < 20 ? 1 : 7))
        {
            clear_to_color(dst, 255);
            stretch_blit(src, dst, 3, 0, sw, 1, 0, 0, dw, 1);
            s.add_mem(dst->line[0], 300, "sw=%d dw=%d", sw, dw);
            /* clipped on the left: the start state must match */
            clear_to_color(dst, 255);
            stretch_blit(src, dst, 3, 0, sw, 1, -(dw / 3), 0, dw, 1);
            s.add_mem(dst->line[0], 300, "clipped sw=%d dw=%d", sw, dw);
        }
    s.end();
    destroy_bitmap(src);
    destroy_bitmap(dst);
}

CONF_TEST(draw_sprite_all)
{
    for(int di = 0; di < kNumDepths; ++di)
    {
        int depth = kDepths[di];
        for(int f = 0; f < 4; ++f)
        {
            conf::Seq s(ctx, conf::nm("depth%d_flip%d", depth, f));
            for(int i = 0; i < 150; ++i)
            {
                Src src = make_src(ctx.rng, depth, ctx.rng.range(1, 30), ctx.rng.range(1, 30), 30);
                Dest dst = make_dest(ctx.rng, depth, ctx.rng.range(1, 40), ctx.rng.range(1, 40));
                int dx = coord(ctx.rng, dst.bmp->w), dy = coord(ctx.rng, dst.bmp->h);
                switch(f)
                {
                case 0: draw_sprite(dst.bmp, src.bmp, dx, dy); break;
                case 1: draw_sprite_h_flip(dst.bmp, src.bmp, dx, dy); break;
                case 2: draw_sprite_v_flip(dst.bmp, src.bmp, dx, dy); break;
                default: draw_sprite_vh_flip(dst.bmp, src.bmp, dx, dy); break;
                }
                s.add_bitmap(dst.root, "i=%d %dx%d at %d,%d", i, src.bmp->w, src.bmp->h, dx, dy);
                free_src(src);
                free_dest(dst);
            }
        }
    }
}

CONF_TEST(draw_sprite_ex_8)
{
    init_map();
    COLOR_MAP *old_map = color_map;
    color_map = &g_map;
    for(int mode = 0; mode < 3; ++mode)
        for(int f = 0; f < 4; ++f)
        {
            if(mode == DRAW_SPRITE_LIT)
                continue; /* lit mode of draw_sprite_ex: tested separately below */
            conf::Seq s(ctx, conf::nm("mode%d_flip%d", mode, f));
            for(int i = 0; i < 120; ++i)
            {
                Src src = make_src(ctx.rng, 8, ctx.rng.range(1, 30), ctx.rng.range(1, 30), 30);
                Dest dst = make_dest(ctx.rng, 8, ctx.rng.range(1, 40), ctx.rng.range(1, 40));
                int dx = coord(ctx.rng, dst.bmp->w), dy = coord(ctx.rng, dst.bmp->h);
                draw_sprite_ex(dst.bmp, src.bmp, dx, dy, mode, f);
                s.add_bitmap(dst.root, "i=%d %dx%d at %d,%d", i, src.bmp->w, src.bmp->h, dx, dy);
                free_src(src);
                free_dest(dst);
            }
        }
    color_map = old_map;
}

CONF_TEST(draw_trans_sprite_8)
{
    init_map();
    COLOR_MAP *old_map = color_map;
    color_map = &g_map;
    conf::Seq s(ctx, "trans");
    for(int i = 0; i < 400; ++i)
    {
        Src src = make_src(ctx.rng, 8, ctx.rng.range(1, 30), ctx.rng.range(1, 30), 30);
        Dest dst = make_dest(ctx.rng, 8, ctx.rng.range(1, 40), ctx.rng.range(1, 40));
        int dx = coord(ctx.rng, dst.bmp->w), dy = coord(ctx.rng, dst.bmp->h);
        draw_trans_sprite(dst.bmp, src.bmp, dx, dy);
        s.add_bitmap(dst.root, "i=%d %dx%d at %d,%d", i, src.bmp->w, src.bmp->h, dx, dy);
        free_src(src);
        free_dest(dst);
    }
    s.end();
    color_map = old_map;
}

CONF_TEST(draw_lit_sprite_8)
{
    init_map();
    COLOR_MAP *old_map = color_map;
    color_map = &g_map;
    conf::Seq s(ctx, "lit");
    for(int i = 0; i < 400; ++i)
    {
        Src src = make_src(ctx.rng, 8, ctx.rng.range(1, 30), ctx.rng.range(1, 30), 30);
        Dest dst = make_dest(ctx.rng, 8, ctx.rng.range(1, 40), ctx.rng.range(1, 40));
        int dx = coord(ctx.rng, dst.bmp->w), dy = coord(ctx.rng, dst.bmp->h);
        int color = ctx.rng.range(0, 255);
        draw_lit_sprite(dst.bmp, src.bmp, dx, dy, color);
        s.add_bitmap(dst.root, "i=%d %dx%d at %d,%d color=%d", i, src.bmp->w, src.bmp->h, dx, dy, color);
        free_src(src);
        free_dest(dst);
    }
    s.end();
    color_map = old_map;
}

CONF_TEST(draw_character_ex_all)
{
    for(int di = 0; di < kNumDepths; ++di)
    {
        int depth = kDepths[di];
        conf::Seq s(ctx, conf::nm("depth%d", depth));
        for(int i = 0; i < 200; ++i)
        {
            Src src = make_src(ctx.rng, 8, ctx.rng.range(1, 30), ctx.rng.range(1, 30), 40);
            Dest dst = make_dest(ctx.rng, depth, ctx.rng.range(1, 40), ctx.rng.range(1, 40));
            int dx = coord(ctx.rng, dst.bmp->w), dy = coord(ctx.rng, dst.bmp->h);
            int color = (int)(random_pixel(ctx.rng, depth) & 0x7FFFFFFF);
            int bg = ctx.rng.range(0, 1) ? -1 : (int)(random_pixel(ctx.rng, depth) & 0x7FFFFFFF);
            draw_character_ex(dst.bmp, src.bmp, dx, dy, color, bg);
            s.add_bitmap(dst.root, "i=%d %dx%d at %d,%d fg=%d bg=%d", i, src.bmp->w, src.bmp->h, dx, dy, color, bg);
            free_src(src);
            free_dest(dst);
        }
    }
}

CONF_TEST(draw_sprite_ex_lit_8)
{
    /* DRAW_SPRITE_LIT has no color argument: with the default blender state
     * (no set_trans_blender call) it must light like the reference. */
    init_map();
    COLOR_MAP *old_map = color_map;
    color_map = &g_map;
    for(int f = 0; f < 4; ++f)
    {
        conf::Seq s(ctx, conf::nm("flip%d", f));
        for(int i = 0; i < 60; ++i)
        {
            Src src = make_src(ctx.rng, 8, ctx.rng.range(1, 30), ctx.rng.range(1, 30), 30);
            Dest dst = make_dest(ctx.rng, 8, ctx.rng.range(1, 40), ctx.rng.range(1, 40));
            int dx = coord(ctx.rng, dst.bmp->w), dy = coord(ctx.rng, dst.bmp->h);
            draw_sprite_ex(dst.bmp, src.bmp, dx, dy, DRAW_SPRITE_LIT, f);
            s.add_bitmap(dst.root, "i=%d %dx%d at %d,%d", i, src.bmp->w, src.bmp->h, dx, dy);
            free_src(src);
            free_dest(dst);
        }
    }
    color_map = old_map;
}

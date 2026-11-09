/*
 * test_blit_rle.cpp - RLE sprites must match the reference build: the
 * encoding get_rle_sprite() produces (it is the format datafiles store, so
 * the files module's loader and these drawers share it) and the pixels
 * draw_rle_sprite / draw_trans_rle_sprite / draw_lit_rle_sprite write.
 */
#include "conf.h"

#include <stdio.h>
#include <string.h>
#include <string>

namespace
{

const int kDepths[] = { 8, 15, 16, 24, 32 };
const int kNumDepths = 5;

void raw_put(BITMAP *b, int x, int y, unsigned c)
{
    switch(bitmap_color_depth(b))
    {
    case 8: b->line[y][x] = (unsigned char)c; break;
    case 15:
    case 16: ((unsigned short *)b->line[y])[x] = (unsigned short)c; break;
    case 24:
        b->line[y][x * 3 + 0] = (unsigned char)c;
        b->line[y][x * 3 + 1] = (unsigned char)(c >> 8);
        b->line[y][x * 3 + 2] = (unsigned char)(c >> 16);
        break;
    default: ((unsigned int *)b->line[y])[x] = c; break;
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

/* Runs of mask and solid pixels of random lengths. */
void fill_runs(BITMAP *b, conf::Rng &rng, int max_run)
{
    int depth = bitmap_color_depth(b);
    unsigned mask = (unsigned)bitmap_mask_color(b);
    for(int y = 0; y < b->h; ++y)
    {
        int x = 0;
        bool solid = rng.range(0, 1) != 0;
        while(x < b->w)
        {
            int n = rng.range(1, max_run);
            for(int i = 0; i < n && x < b->w; ++i, ++x)
                raw_put(b, x, y, solid ? random_pixel(rng, depth) : mask);
            solid = !solid;
        }
    }
}

void fill_random(BITMAP *b, conf::Rng &rng, int mask_pct)
{
    int depth = bitmap_color_depth(b);
    unsigned mask = (unsigned)bitmap_mask_color(b);
    for(int y = 0; y < b->h; ++y)
        for(int x = 0; x < b->w; ++x)
            raw_put(b, x, y, (rng.range(0, 99) < mask_pct) ? mask : random_pixel(rng, depth));
}

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

COLOR_MAP g_map;
void init_map()
{
    for(int a = 0; a < PAL_SIZE; ++a)
        for(int b = 0; b < PAL_SIZE; ++b)
            g_map.data[a][b] = (unsigned char)((a * 5 + b * 9 + (a ^ (b >> 1)) + 3) & 0xFF);
}

/* Hex dump of the encoded data, for the small cases. */
std::string hex(const void *p, int n)
{
    std::string s;
    char buf[4];
    for(int i = 0; i < n; ++i)
    {
        sprintf(buf, "%02x", ((const unsigned char *)p)[i]);
        s += buf;
    }
    return s;
}

void check_rle(conf::Ctx &ctx, const char *c, const RLE_SPRITE *r)
{
    if(!r)
    {
        ctx.checkf(c, "(null)");
        return;
    }
    if(r->size <= 48)
        ctx.checkf(c, "%dx%d d=%d size=%d %s", r->w, r->h, r->color_depth, r->size, hex(r->dat, r->size).c_str());
    else
        ctx.checkf(c, "%dx%d d=%d size=%d %s", r->w, r->h, r->color_depth, r->size,
                   conf::hash_text(conf::hash_bytes(r->dat, (size_t)r->size)).c_str());
}

} // namespace

CONF_TEST(rle_encode_small)
{
    /* Hand-made rows: leading/trailing/whole-row transparency, single pixels. */
    for(int di = 0; di < kNumDepths; ++di)
    {
        int depth = kDepths[di];
        unsigned mask = (unsigned)(depth == 8 ? 0 : (depth == 15 ? 0x7C1F : (depth == 16 ? 0xF81F : 0xFF00FF)));
        BITMAP *b = create_bitmap_ex(depth, 5, 4);
        const char *rows[4] = { "a..b.", ".....", "aaaaa", "..a.." };
        for(int y = 0; y < 4; ++y)
            for(int x = 0; x < 5; ++x)
                raw_put(b, x, y, rows[y][x] == '.' ? mask : (unsigned)(0x11 * (x + 1) + y));
        RLE_SPRITE *r = get_rle_sprite(b);
        check_rle(ctx, conf::nm("rows_d%d", depth), r);
        destroy_rle_sprite(r);

        BITMAP *one = create_bitmap_ex(depth, 1, 1);
        raw_put(one, 0, 0, mask);
        r = get_rle_sprite(one);
        check_rle(ctx, conf::nm("one_mask_d%d", depth), r);
        destroy_rle_sprite(r);
        raw_put(one, 0, 0, 7);
        r = get_rle_sprite(one);
        check_rle(ctx, conf::nm("one_solid_d%d", depth), r);
        destroy_rle_sprite(r);
        destroy_bitmap(one);
        destroy_bitmap(b);
    }
}

CONF_TEST(rle_encode_long_runs)
{
    /* Runs longer than a signed byte / the run limits. */
    for(int di = 0; di < kNumDepths; ++di)
    {
        int depth = kDepths[di];
        unsigned mask = (unsigned)(depth == 8 ? 0 : (depth == 15 ? 0x7C1F : (depth == 16 ? 0xF81F : 0xFF00FF)));
        int widths[] = { 126, 127, 128, 129, 255, 256, 300, 600 };
        for(int wi = 0; wi < 8; ++wi)
        {
            int w = widths[wi];
            BITMAP *b = create_bitmap_ex(depth, w, 3);
            for(int x = 0; x < w; ++x)
            {
                raw_put(b, x, 0, mask);
                raw_put(b, x, 1, (unsigned)(x % 200 + 1));
                raw_put(b, x, 2, (x < w / 3 || x > 2 * w / 3) ? mask : 9u);
            }
            RLE_SPRITE *r = get_rle_sprite(b);
            check_rle(ctx, conf::nm("w%d_d%d", w, depth), r);
            destroy_rle_sprite(r);
            destroy_bitmap(b);
        }
    }
}

CONF_TEST(rle_encode_random)
{
    for(int di = 0; di < kNumDepths; ++di)
    {
        int depth = kDepths[di];
        conf::Seq s(ctx, conf::nm("depth%d", depth));
        for(int i = 0; i < 100; ++i)
        {
            BITMAP *b = create_bitmap_ex(depth, ctx.rng.range(1, 70), ctx.rng.range(1, 20));
            if(i % 2)
                fill_runs(b, ctx.rng, ctx.rng.range(1, 140));
            else
                fill_random(b, ctx.rng, ctx.rng.range(0, 100));
            BITMAP *src = b;
            BITMAP *sub = NULL;
            if(i % 5 == 0 && b->w > 2 && b->h > 2)
                src = sub = create_sub_bitmap(b, 1, 1, b->w - 2, b->h - 2);
            RLE_SPRITE *r = get_rle_sprite(src);
            s.add_int(r ? r->w : -1, "w i=%d", i);
            s.add_int(r ? r->h : -1, "h i=%d", i);
            s.add_int(r ? r->color_depth : -1, "depth i=%d", i);
            s.add_int(r ? r->size : -1, "size i=%d", i);
            if(r)
                s.add_mem(r->dat, (size_t)r->size, "dat i=%d", i);
            destroy_rle_sprite(r);
            if(sub)
                destroy_bitmap(sub);
            destroy_bitmap(b);
        }
    }
    destroy_rle_sprite(NULL);
}

CONF_TEST(rle_draw)
{
    for(int di = 0; di < kNumDepths; ++di)
    {
        int depth = kDepths[di];
        conf::Seq s(ctx, conf::nm("depth%d", depth));
        for(int i = 0; i < 250; ++i)
        {
            BITMAP *spr = create_bitmap_ex(depth, ctx.rng.range(1, 40), ctx.rng.range(1, 30));
            if(i % 2)
                fill_runs(spr, ctx.rng, ctx.rng.range(1, 10));
            else
                fill_random(spr, ctx.rng, 35);
            RLE_SPRITE *r = get_rle_sprite(spr);
            BITMAP *root = create_bitmap_ex(depth, ctx.rng.range(1, 50) + 6, ctx.rng.range(1, 40) + 5);
            fill_random(root, ctx.rng, 5);
            BITMAP *dst = root;
            if(i % 4 == 0)
                dst = create_sub_bitmap(root, ctx.rng.range(0, 6), ctx.rng.range(0, 5), root->w - 6, root->h - 5);
            random_clip(dst, ctx.rng);
            int x = coord(ctx.rng, dst->w), y = coord(ctx.rng, dst->h);
            draw_rle_sprite(dst, r, x, y);
            s.add_bitmap(root, "i=%d %dx%d at %d,%d", i, spr->w, spr->h, x, y);
            if(dst != root)
                destroy_bitmap(dst);
            destroy_bitmap(root);
            destroy_rle_sprite(r);
            destroy_bitmap(spr);
        }
    }
}

CONF_TEST(rle_draw_trans_lit_8)
{
    init_map();
    COLOR_MAP *old_map = color_map;
    color_map = &g_map;
    conf::Seq st(ctx, "trans");
    conf::Seq sl(ctx, "lit");
    for(int i = 0; i < 300; ++i)
    {
        BITMAP *spr = create_bitmap_ex(8, ctx.rng.range(1, 40), ctx.rng.range(1, 30));
        if(i % 2)
            fill_runs(spr, ctx.rng, ctx.rng.range(1, 10));
        else
            fill_random(spr, ctx.rng, 35);
        RLE_SPRITE *r = get_rle_sprite(spr);
        BITMAP *dst = create_bitmap_ex(8, ctx.rng.range(1, 50), ctx.rng.range(1, 40));
        fill_random(dst, ctx.rng, 5);
        random_clip(dst, ctx.rng);
        int x = coord(ctx.rng, dst->w), y = coord(ctx.rng, dst->h);
        BITMAP *copy = create_bitmap_ex(8, dst->w, dst->h);
        blit(dst, copy, 0, 0, 0, 0, dst->w, dst->h);
        set_clip_rect(copy, dst->cl, dst->ct, dst->cr - 1, dst->cb - 1);
        draw_trans_rle_sprite(dst, r, x, y);
        st.add_bitmap(dst, "i=%d %dx%d at %d,%d", i, spr->w, spr->h, x, y);
        int color = ctx.rng.range(0, 255);
        draw_lit_rle_sprite(copy, r, x, y, color);
        sl.add_bitmap(copy, "i=%d %dx%d at %d,%d color=%d", i, spr->w, spr->h, x, y, color);
        destroy_bitmap(copy);
        destroy_bitmap(dst);
        destroy_rle_sprite(r);
        destroy_bitmap(spr);
    }
    st.end();
    sl.end();
    color_map = old_map;
}

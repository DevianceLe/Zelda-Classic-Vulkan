/*
 * test_files_save.cpp - save_bmp / save_bitmap (and the PCX and TGA writers
 * save_bitmap reaches for the game's snapshot formats) must write the
 * reference's bytes; register_bitmap_file_type must route save_bitmap by
 * extension. PNG and JPG (loadpng / jpgalleg) are tested port-only in
 * test_files_save_portonly.cpp.
 */
#include "conf.h"

#include <stdio.h>
#include <string.h>

#include <string>

namespace
{

void random_palette(conf::Rng &rng, PALETTE pal)
{
    for(int i = 0; i < PAL_SIZE; ++i)
    {
        pal[i].r = (unsigned char)rng.range(0, 63);
        pal[i].g = (unsigned char)rng.range(0, 63);
        pal[i].b = (unsigned char)rng.range(0, 63);
        pal[i].filler = 0;
    }
}

BITMAP *random_bitmap(conf::Rng &rng, int depth, int w, int h)
{
    BITMAP *b = create_bitmap_ex(depth, w, h);
    int bpp = conf::bytes_per_pixel(depth);

    for(int y = 0; y < h; ++y)
    {
        for(int x = 0; x < w * bpp; ++x)
            b->line[y][x] = (unsigned char)rng.bits(8);

        /* keep the unused top bit of 15-bit pixels clear, as makecol15 does */
        if(depth == 15)
            for(int x = 0; x < w; ++x)
                b->line[y][x * 2 + 1] &= 0x7F;

        /* 32-bit: no alpha byte, as makecol32 does */
        if(depth == 32)
            for(int x = 0; x < w; ++x)
                b->line[y][x * 4 + 3] = 0;
    }

    return b;
}

int g_saver_calls;
std::string g_saver_name;
int g_saver_w;

int test_saver(const char *filename, BITMAP *bmp, const RGB *pal)
{
    ++g_saver_calls;
    g_saver_name = get_filename(filename);
    g_saver_w = bmp ? bmp->w : -1;
    (void)pal;
    return 1234;
}

} /* namespace */

CONF_TEST(save_bmp_8bit)
{
    std::string fn = ctx.tmp("save8.bmp");
    static const int sizes[][2] = { { 1, 1 }, { 2, 3 }, { 3, 2 }, { 4, 4 }, { 5, 7 }, { 6, 1 }, { 7, 9 }, { 8, 8 },
                                    { 9, 2 }, { 33, 17 }, { 256, 224 }, { 320, 240 }, { 640, 480 } };

    for(size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        PALETTE pal;
        random_palette(ctx.rng, pal);
        BITMAP *b = random_bitmap(ctx.rng, 8, sizes[i][0], sizes[i][1]);
        ctx.check_int(conf::nm("ret_%d", (int)i), save_bmp(fn.c_str(), b, pal));
        ctx.check_file(conf::nm("file_%d", (int)i), fn.c_str());
        destroy_bitmap(b);
    }

    /* (palette values above 63 index past the reference's 6-bit scale
     * table: undefined, so not tested) */
    PALETTE pal;
    BITMAP *b = random_bitmap(ctx.rng, 8, 16, 16);

    /* a sub-bitmap */
    BITMAP *big = random_bitmap(ctx.rng, 8, 40, 30);
    BITMAP *sub = create_sub_bitmap(big, 3, 5, 17, 11);
    random_palette(ctx.rng, pal);
    ctx.check_int("ret_sub", save_bmp(fn.c_str(), sub, pal));
    ctx.check_file("file_sub", fn.c_str());
    destroy_bitmap(sub);
    destroy_bitmap(big);
    destroy_bitmap(b);
    delete_file(fn.c_str());
}

CONF_TEST(save_bmp_truecolor)
{
    std::string fn = ctx.tmp("savetc.bmp");
    static const int depths[] = { 15, 16, 24, 32 };
    PALETTE pal;
    random_palette(ctx.rng, pal);

    for(int d = 0; d < 4; ++d)
        for(int s = 0; s < 4; ++s)
        {
            int w = 1 + s * 3, h = 2 + s;
            BITMAP *b = random_bitmap(ctx.rng, depths[d], w, h);
            ctx.check_int(conf::nm("ret_%d_%d", depths[d], s), save_bmp(fn.c_str(), b, pal));
            ctx.check_file(conf::nm("file_%d_%d", depths[d], s), fn.c_str());
            destroy_bitmap(b);
        }

    delete_file(fn.c_str());
}

/* save_bitmap: the built-in types the game's snapshot menu reaches (bmp,
 * pcx, tga), extension case, unknown types, registered savers. */
CONF_TEST(save_bitmap_types)
{
    static const char *names[] = { "snap.bmp", "SNAP.BMP", "snap.Bmp", "snap.pcx", "snap.PCX", "snap.tga", "snap.TGA" };
    static const int depths[] = { 8, 15, 16, 24, 32 };

    for(size_t n = 0; n < sizeof(names) / sizeof(names[0]); ++n)
        for(int d = 0; d < 5; ++d)
            for(int s = 0; s < 3; ++s)
            {
                PALETTE pal;
                random_palette(ctx.rng, pal);
                int w = s == 0 ? 1 : s == 1 ? 7 : 129, h = s == 0 ? 1 : s == 1 ? 3 : 40;
                BITMAP *b = random_bitmap(ctx.rng, depths[d], w, h);

                /* runs for the RLE coders of PCX and TGA */
                if(s == 2)
                    for(int y = 0; y < h; ++y)
                        for(int x = 0; x < w; ++x)
                            if((x / 5 + y) % 3 == 0)
                                memcpy(b->line[y] + x * conf::bytes_per_pixel(depths[d]), b->line[y],
                                       (size_t)conf::bytes_per_pixel(depths[d]));

                std::string fn = ctx.tmp(names[n]);
                ctx.check_int(conf::nm("ret_%d_%d_%d", (int)n, depths[d], s), save_bitmap(fn.c_str(), b, pal));
                ctx.check_file(conf::nm("file_%d_%d_%d", (int)n, depths[d], s), fn.c_str());
                delete_file(fn.c_str());
                destroy_bitmap(b);
            }

    /* unknown extension, no extension */
    PALETTE pal;
    random_palette(ctx.rng, pal);
    BITMAP *b = random_bitmap(ctx.rng, 8, 8, 8);
    std::string unk = ctx.tmp("snap.xyz");
    ctx.check_bool("unknown_nonzero", save_bitmap(unk.c_str(), b, pal) != 0);
    ctx.check_bool("unknown_exists", exists(unk.c_str()) != 0);
    std::string noext = ctx.tmp("snapnoext");
    ctx.check_bool("noext_nonzero", save_bitmap(noext.c_str(), b, pal) != 0);
    ctx.check_bool("noext_exists", exists(noext.c_str()) != 0);

    /* a registered saver (the game registers "GIF" this way) */
    register_bitmap_file_type("xyz", NULL, test_saver);
    g_saver_calls = 0;
    ctx.check_int("custom_ret", save_bitmap(unk.c_str(), b, pal));
    ctx.check_int("custom_calls", g_saver_calls);
    ctx.check_str("custom_name", g_saver_name.c_str());
    ctx.check_int("custom_w", g_saver_w);
    ctx.check_int("custom_upper_ret", save_bitmap(ctx.tmp("SNAP.XYZ").c_str(), b, pal));
    ctx.check_int("custom_calls2", g_saver_calls);

    /* re-registering replaces; a NULL saver makes the type unsavable */
    register_bitmap_file_type("XYZ", NULL, NULL);
    g_saver_calls = 0;
    ctx.check_bool("nullsaver_nonzero", save_bitmap(unk.c_str(), b, pal) != 0);
    ctx.check_int("nullsaver_calls", g_saver_calls);

    /* replacing a built-in type */
    register_bitmap_file_type("tga", NULL, test_saver);
    g_saver_calls = 0;
    ctx.check_int("builtin_replaced_ret", save_bitmap(ctx.tmp("x.tga").c_str(), b, pal));
    ctx.check_int("builtin_replaced_calls", g_saver_calls);
    register_bitmap_file_type("tga", NULL, save_tga);
    std::string t = ctx.tmp("x.tga");
    ctx.check_int("builtin_restored_ret", save_bitmap(t.c_str(), b, pal));
    ctx.check_file("builtin_restored", t.c_str());
    delete_file(t.c_str());
    destroy_bitmap(b);
}

/* With a packfile password the image is scrambled like any packfile. */
CONF_TEST(save_with_password)
{
    PALETTE pal;
    random_palette(ctx.rng, pal);
    BITMAP *b = random_bitmap(ctx.rng, 8, 10, 10);
    std::string fn = ctx.tmp("pw.bmp");
    packfile_password("abc");
    int r = save_bmp(fn.c_str(), b, pal);
    packfile_password(NULL);
    ctx.check_int("ret", r);
    ctx.check_file("file", fn.c_str());

    /* errors: a directory that does not exist */
    ctx.check_bool("nodir_nonzero", save_bmp(ctx.tmp("no_such_dir/x.bmp").c_str(), b, pal) != 0);
    destroy_bitmap(b);
    delete_file(fn.c_str());
}

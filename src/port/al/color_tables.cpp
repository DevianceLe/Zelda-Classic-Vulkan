/*
 * zcport color module: create_color_table, a COLOR_MAP filled by a caller's
 * blending function (ZQuest's tile grabber). Each result is found through
 * rgb_map when one is installed, otherwise by a search of the palette.
 *
 * Written from scratch for zcport.
 */

#include "color_internal.h"

extern "C" void create_color_table(COLOR_MAP *table, AL_CONST PALETTE pal,
                                   AL_METHOD(void, blend, (AL_CONST PALETTE pal, int x, int y, RGB *rgb)),
                                   AL_METHOD(void, callback, (int pos)))
{
    for(int x = 0; x < PAL_SIZE; ++x)
    {
        for(int y = 0; y < PAL_SIZE; ++y)
        {
            RGB c;
            blend(pal, x, y, &c);

            if(rgb_map)
                table->data[x][y] = rgb_map->data[c.r >> 1][c.g >> 1][c.b >> 1];
            else
                table->data[x][y] = (unsigned char)bestfit_color(pal, c.r, c.g, c.b);
        }

        if(callback)
            callback(x);
    }
}

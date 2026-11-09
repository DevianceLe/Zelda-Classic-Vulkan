/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/lzss.h - LZSS compression used by packed PACKFILEs.
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_LZSS_H
#define ALLEGRO_LZSS_H

#include "base.h"
#include "file.h"

#ifdef __cplusplus
   extern "C" {
#endif

typedef struct LZSS_PACK_DATA LZSS_PACK_DATA;
typedef struct LZSS_UNPACK_DATA LZSS_UNPACK_DATA;

AL_FUNC(LZSS_PACK_DATA *, create_lzss_pack_data, (void));
AL_FUNC(void, free_lzss_pack_data, (LZSS_PACK_DATA *dat));
AL_FUNC(int, lzss_write, (PACKFILE *file, LZSS_PACK_DATA *dat, int size, unsigned char *buf, int last));

AL_FUNC(LZSS_UNPACK_DATA *, create_lzss_unpack_data, (void));
AL_FUNC(void, free_lzss_unpack_data, (LZSS_UNPACK_DATA *dat));
AL_FUNC(int, lzss_read, (PACKFILE *file, LZSS_UNPACK_DATA *dat, int s, unsigned char *buf));

#ifdef __cplusplus
   }
#endif

#endif /* ALLEGRO_LZSS_H */

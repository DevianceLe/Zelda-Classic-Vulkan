/*
 * conf_zc.h - helpers for tests that read Zelda Classic's own data files
 * (fonts.dat, zelda.dat, sfx.dat, qst.dat, *.qst).
 *
 * The quest decoding below reimplements the game's own wrapper format
 * (src/zsys.cpp decode_file_007 and src/qst.cpp open_quest_file, Zelda
 * Classic code, not Allegro code) with the same Allegro calls the game makes,
 * so a test that uses it exercises exactly the packfile paths the player uses
 * when it loads a quest.
 *
 * Which password goes with which file (what the player does):
 *   zelda.dat, sfx.dat ........ no password  (packfile_password(NULL))
 *   fonts.dat, qst.dat ........ datapwd()    ("longtan")
 *   *.qst ...................... decode_quest() into a temp file, then
 *                               open_decoded_quest() (datapwd(), F_READ_PACKED)
 */
#ifndef ZC_CONFORMANCE_CONF_ZC_H
#define ZC_CONFORMANCE_CONF_ZC_H

#include "conf.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace conf
{
namespace zc
{

/* The game's datapwd after resolve_password(): each stored byte minus
 * (index + 1) * 11. */
inline const char *datapwd()
{
    static const unsigned char stored[8] = { 'l' + 11, 'o' + 22, 'n' + 33, 'g' + 44,
                                             't' + 55, 'a' + 66, 'n' + 77, 0 + 88 };
    static char pwd[8];

    for(int i = 0; i < 8; ++i)
        pwd[i] = (char)(stored[i] - (i + 1) * 11);

    return pwd;
}

/* zdefs.h ENC_STR: header in front of every encoded quest. */
static const char QST_HEADER[] = "Zelda Classic Quest File";

/* zdefs.h ENC_METHOD_* (oldest first). */
enum
{
    METHOD_192B104 = 0,
    METHOD_192B105,
    METHOD_192B185,
    METHOD_211B9,
    METHOD_211B18,
    METHOD_MAX
};

/* Return codes of decode_007 (same as decode_file_007). */
enum
{
    DECODE_OK = 0,
    DECODE_SRC_NOT_OPENED = 1,
    DECODE_DEST_NOT_OPENED = 2,
    DECODE_TOO_SMALL = 3,
    DECODE_READ_ERROR = 4,
    DECODE_BAD_CHECKSUM = 5,   /* wrong method: try an older one */
    DECODE_BAD_HEADER = 6
};

namespace detail
{

inline int wrap_int(unsigned v)
{
    return (int)v;
}

/* The game's 16-bit register-style generator, with its exact integer widths
 * (short and signed char truncation, sign extension). */
inline int rand_007(int &seed, int method)
{
    static const int pv[METHOD_MAX] = { 0x62E9, 0x7D14, 0x1A82, 0x02BB, 0xE09C };
    static const int qv[METHOD_MAX] = { 0x3619, 0xA26B, 0xF03C, 0x7B12, 0x4E8F };

    short hi = (short)(seed >> 8);
    short lo = (short)((seed & 0xFF) << 8);
    signed char top = (signed char)(seed >> 24);
    signed char carry_hi = (signed char)(top >> 7);
    signed char carry_lo = (signed char)(hi >> 15);

    hi = (short)((hi << 1) | carry_hi);
    lo = (short)((lo << 1) | carry_lo);
    lo = (short)(lo + (seed & 0xFFFF));
    hi = (short)(hi + (seed >> 16) + carry_hi);
    lo = (short)(lo + pv[method]);
    hi = (short)(hi + qv[method] + carry_lo);

    seed = wrap_int(((unsigned)(int)hi << 16) + (unsigned)(int)lo);
    return wrap_int(((unsigned)(int)lo << 16) + (unsigned)(int)hi);
}

struct Source
{
    bool packed;
    FILE *plain;
    PACKFILE *pf;

    int get()
    {
        return packed ? pack_getc(pf) : fgetc(plain);
    }

    void close()
    {
        if(packed)
        {
            if(pf)
                pack_fclose(pf);
        }
        else if(plain)
            fclose(plain);

        pf = 0;
        plain = 0;
    }
};

/* alleg_compat.cpp pack_fopen_password / file_size_ex_password */
inline PACKFILE *pack_fopen_password(const char *filename, const char *mode, const char *password)
{
    packfile_password(password);
    PACKFILE *f = pack_fopen(filename, mode);
    packfile_password("");
    return f;
}

inline long file_size_password(const char *filename, const char *password)
{
    packfile_password(password);
    long size = (long)file_size_ex(filename);
    packfile_password("");
    return size;
}

} /* namespace detail */

/*
 * Decodes one encode_file_007 wrapper: header, 4 key bytes, data, 4 checksum
 * bytes. packed: read srcfile through pack_fopen (the game does this for
 * "file.dat#OBJECT" names), else through stdio. Writes the plain data to
 * destfile.
 */
inline int decode_007(const char *srcfile, const char *destfile, const char *header, int method,
                      bool packed, const char *password)
{
    static const unsigned masks[METHOD_MAX] = { 0x4C358938u, 0x91B2A2D1u, 0x4A7C1B87u,
                                                0xF93941E6u, 0xFD095E94u };
    detail::Source src;
    src.packed = packed;
    src.plain = 0;
    src.pf = 0;

    long size = detail::file_size_password(srcfile, password);

    if(size < 1)
        return DECODE_SRC_NOT_OPENED;

    size -= 8;

    if(size < 1)
        return DECODE_TOO_SMALL;

    if(!packed)
    {
        src.plain = fopen(srcfile, "rb");

        if(!src.plain)
            return DECODE_SRC_NOT_OPENED;
    }
    else
    {
        src.pf = detail::pack_fopen_password(srcfile, F_READ_PACKED, password);

        if(errno == EDOM)
            src.pf = detail::pack_fopen_password(srcfile, F_READ, password);

        if(!src.pf)
            return DECODE_SRC_NOT_OPENED;
    }

    FILE *dest = fopen(destfile, "wb");

    if(!dest)
    {
        src.close();
        return DECODE_DEST_NOT_OPENED;
    }

    int err = DECODE_READ_ERROR;
    int c, r = 0, tog = 0, seed = 0;
    short sum1 = 0, sum2 = 0, check1, check2;
    unsigned key_bits = 0;

    if(header)
    {
        for(int i = 0; header[i]; ++i)
        {
            if((c = src.get()) == EOF)
                goto fail;

            if((c & 255) != header[i])
            {
                err = DECODE_BAD_HEADER;
                goto fail;
            }

            --size;
        }
    }

    /* 4 key bytes, big-endian */
    for(int i = 0; i < 4; ++i)
    {
        if((c = src.get()) == EOF)
            goto fail;

        key_bits = i == 0 ? ((unsigned)c << 24) : key_bits + ((unsigned)(c & 255) << (8 * (3 - i)));
    }

    seed = detail::wrap_int(key_bits ^ masks[method]);

    for(long i = 0; i < size; ++i)
    {
        if((c = src.get()) == EOF)
            goto fail;

        if(tog)
            c -= r;
        else
        {
            r = detail::rand_007(seed, method);
            c ^= r;
        }

        tog ^= 1;
        c &= 255;
        sum1 = (short)(sum1 + c);
        sum2 = (short)((sum2 << 4) + (sum2 >> 12) + c);
        fputc(c, dest);
    }

    {
        int b[4];

        for(int i = 0; i < 4; ++i)
            if((b[i] = src.get()) == EOF)
                goto fail;

        check1 = (short)(b[0] << 8);
        check1 = (short)(check1 + (b[1] & 255));
        check2 = (short)(b[2] << 8);
        check2 = (short)(check2 + (b[3] & 255));
        r = detail::rand_007(seed, method);
        check1 = (short)(check1 ^ r);
        check2 = (short)(check2 - r);

        if(check1 != sum1 || check2 != sum2)
        {
            err = DECODE_BAD_CHECKSUM;
            goto fail;
        }
    }

    src.close();
    fclose(dest);
    return DECODE_OK;

fail:
    src.close();
    fclose(dest);
    delete_file(destfile);
    return err;
}

/*
 * Decodes an encrypted quest like the player does: "x.qst" (stdio) or
 * "qst.dat#OBJECT" (packfile), newest method first, then the older ones while
 * the checksum fails. Returns DECODE_OK and the method in *method_used, or the
 * last error. (On failure the game treats the file as an old unencrypted
 * quest and opens it directly with no password.)
 */
inline int decode_quest(const char *srcfile, const char *destfile, int *method_used = 0)
{
    static const int order[METHOD_MAX] = { METHOD_211B18, METHOD_211B9, METHOD_192B185,
                                           METHOD_192B105, METHOD_192B104 };
    bool packed = strstr(srcfile, ".dat#") != NULL;
    int ret = DECODE_BAD_CHECKSUM;
    int method = order[0];

    for(int i = 0; i < METHOD_MAX && (i == 0 || ret == DECODE_BAD_CHECKSUM); ++i)
    {
        method = order[i];
        ret = decode_007(srcfile, destfile, QST_HEADER, method, packed, datapwd());
    }

    if(method_used)
        *method_used = method;

    return ret;
}

/* Opens a decoded quest the way open_quest_file() does for compressed quests:
 * datapwd(), F_READ_PACKED, and F_READ if that fails with errno EDOM. */
inline PACKFILE *open_decoded_quest(const char *decoded)
{
    PACKFILE *f = detail::pack_fopen_password(decoded, F_READ_PACKED, datapwd());

    if(!f && errno == EDOM)
        f = detail::pack_fopen_password(decoded, F_READ, datapwd());

    return f;
}

} /* namespace zc */
} /* namespace conf */

#endif

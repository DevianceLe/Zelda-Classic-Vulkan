/*
 * zcport: the global packfile password (packfile_password).
 *
 * Packfiles opened afterwards are scrambled/unscrambled with this password;
 * an empty or NULL password means none. The packfile code reads it through
 * zcport_packfile_password().
 *
 * Like the reference, the password is taken in the current text format and
 * kept as 8-bit characters: with U_UTF8 (Allegro's default, active while
 * the player loads its datafiles) a multi-byte sequence becomes one
 * character holding the low 8 bits of its code point ('^' for a broken
 * sequence; a zero byte ends the password). The game's passwords are plain
 * ASCII, where nothing changes.
 *
 * Written from scratch for zcport.
 */

#include <allegro.h>

#include <string>

/* The text format comes from the unicode module; until one is linked in,
 * Allegro's default U_UTF8 applies. */
extern "C" int zcfiles_fb_get_uformat(void)
{
    return U_UTF8;
}

#if defined(_MSC_VER)
#if defined(_M_IX86)
#pragma comment(linker, "/alternatename:_get_uformat=_zcfiles_fb_get_uformat")
#else
#pragma comment(linker, "/alternatename:get_uformat=zcfiles_fb_get_uformat")
#endif
#else
extern "C" __attribute__((weak)) int get_uformat(void)
{
    return zcfiles_fb_get_uformat();
}
#endif

namespace
{
std::string g_password;

/* Next character of a UTF-8 string; advances p. Broken sequences give '^'
 * and leave the offending byte for the next call. */
int utf8_next(const unsigned char *&p)
{
    int c = *p++;

    if(!(c & 0x80))
        return c;

    int ones = 1;

    while(ones < 8 && (c & (0x80 >> ones)))
        ++ones;

    int value = ones >= 8 ? 0 : (c & ((1 << (8 - ones)) - 1));

    for(int k = 1; k < ones; ++k)
    {
        int t = *p;

        if(!(t & 0x80) || (t & 0x40))
            return '^';

        ++p;
        value = (value << 6) | (t & 0x3F);
    }

    return value;
}

std::string to_8bit(const char *s)
{
    std::string out;
    int fmt = get_uformat();

    if(fmt != U_UTF8)
    {
        /* U_ASCII and the 8-bit code page format: byte for byte */
        if(fmt != U_UNICODE)
            return s;

        /* 16-bit characters */
        const unsigned short *w = (const unsigned short *)s;

        for(; *w; ++w)
            out += (char)(*w < 256 ? *w : '^');

        return out;
    }

    const unsigned char *p = (const unsigned char *)s;

    while(*p)
    {
        /* characters keep their low 8 bits; a zero byte ends the password */
        int c = utf8_next(p) & 0xFF;

        if(c == 0)
            break;

        out += (char)c;
    }

    return out;
}
}

/* Current password ("" when none). */
const char *zcport_packfile_password(void)
{
    return g_password.c_str();
}

extern "C" {

void packfile_password(AL_CONST char *password)
{
    g_password = password ? to_8bit(password) : "";
}

} // extern "C"

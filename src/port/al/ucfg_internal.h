// zcport ucfg module: private declarations shared by the ucfg_*.cpp files
// (Unicode strings, printf, config files). Not a contract for other modules.

#ifndef ZCPORT_UCFG_INTERNAL_H
#define ZCPORT_UCFG_INTERNAL_H

#include <allegro.h>

namespace zcport_ucfg
{
    // One text encoding: the handlers set_uformat() installs.
    struct TextFormat
    {
        int id;
        int (*getc_fn)(const char *s);
        int (*getx_fn)(char **s);
        int (*setc_fn)(char *s, int c);
        int (*width_fn)(const char *s);
        int (*cwidth_fn)(int c);
        int (*isok_fn)(int c);
        int width_max;
    };

    // The format with this id (U_CURRENT: the current one), or nullptr.
    const TextFormat *find_format(int id);

    // U_ASCII_CP reduction of a code point: the byte, or -1 when the
    // codepage has no representation.
    int codepage_encode(int c);
    // U_ASCII_CP: code point of a byte.
    int codepage_decode(unsigned char b);

    // Default U_ASCII_CP reduction pairs (Unicode value, byte), terminated by
    // a zero Unicode value.
    extern const unsigned short default_codepage_extras[];
}

#endif

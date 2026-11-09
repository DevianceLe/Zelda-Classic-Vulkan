/*
 * test_ucfg_unicode.cpp - the unicode.h string routines must match the
 * reference build byte for byte in every text format: the GUI (jwin, the
 * file selector, edit boxes) and every textprintf go through them, and the
 * player switches between U_UTF8 (the default) and U_ASCII at run time.
 *
 * Covers: set_uformat/get_uformat/uwidth_max, the per-format function
 * pointers (ugetc, ugetx, ugetxc, usetc, uwidth, ucwidth, uisok) on valid and
 * invalid input, index helpers (uoffset, ugetat, usetat, uinsert, uremove),
 * character classes and case mapping over the whole code point range, length
 * and comparison, searching, tokenizing, number parsing, copying, conversion
 * between formats, and empty_string. Printf formatting is in
 * test_ucfg_printf.cpp.
 */
#include "conf.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace
{

struct Fmt
{
    int type;
    const char *name;
};

const Fmt kFmts[] =
{
    { U_ASCII, "ascii" },
    { U_UTF8, "utf8" },
    { U_UNICODE, "unicode" },
    { U_ASCII_CP, "ascii_cp" }
};
const int kNumFmts = (int)(sizeof(kFmts) / sizeof(kFmts[0]));

/* The formats the player actually runs in. */
const Fmt kGameFmts[] =
{
    { U_ASCII, "ascii" },
    { U_UTF8, "utf8" }
};
const int kNumGameFmts = 2;

std::string hexs(const void *p, int n)
{
    static const char digits[] = "0123456789abcdef";
    const unsigned char *b = (const unsigned char *)p;
    std::string r;

    for(int i = 0; i < n; ++i)
    {
        r += digits[b[i] >> 4];
        r += digits[b[i] & 15];
    }

    return r;
}

/* Bytes of a string up to and including its terminator (two zero bytes at
 * an even offset for U_UNICODE, one zero byte otherwise), at most max. */
std::string hexz(const char *p, int type, int max)
{
    int n = 0;

    if(type == U_UNICODE)
    {
        while(n + 2 <= max && (p[n] || p[n + 1]))
            n += 2;

        n += 2;
    }
    else
    {
        while(n < max && p[n])
            ++n;

        ++n;
    }

    return hexs(p, n < max ? n : max);
}

/* Offset of p in the 64-byte buffer b: -1 for NULL, -2 outside. */
int off_in(const char *p, const char *b)
{
    if(!p)
        return -1;

    if(p < b || p > b + 64)
        return -2;

    return (int)(p - b);
}

class FormatGuard
{
public:
    explicit FormatGuard(int type) : old_(get_uformat()) { set_uformat(type); }
    ~FormatGuard() { set_uformat(old_); }
private:
    int old_;
};

/* Encodes code points (terminated) in the current format; returns bytes. */
int build(char *buf, const int *cps, int n)
{
    int pos = 0;

    for(int i = 0; i < n; ++i)
        pos += usetc(buf + pos, cps[i]);

    pos += usetc(buf + pos, 0);
    return pos;
}

/* Code point lists used to build test strings in every format. */
struct Cps
{
    int n;
    int c[12];
};

const Cps kStrings[] =
{
    { 0, { 0 } },
    { 1, { 'a' } },
    { 3, { 'a', 'b', 'c' } },
    { 5, { 'H', 'e', 'l', 'l', 'o' } },
    { 4, { 'a', 0xE9, 0x20AC, 'd' } },
    { 3, { 0x10000, 'x', 0xFF } },
    { 6, { 'A', 0xC0, 0x3A3, 0x416, 0x1E9E, 'z' } },
    { 7, { ' ', '\t', 'x', '\n', 0xA0, 0x3000, 'y' } },
    { 5, { '1', '2', 0x661, '3', '.' } },
    { 8, { 'f', 'o', 'o', '.', 'b', 'a', 'r', '/' } },
};
const int kNumStrings = (int)(sizeof(kStrings) / sizeof(kStrings[0]));

} // namespace

/* ------------------------------------------------------------------------- */

CONF_TEST(ucfg_uformat)
{
    ctx.check_hex("initial", (unsigned)get_uformat());

    for(int t = 0; t < kNumFmts; ++t)
        ctx.check_int(conf::nm("uwidth_max_%s", kFmts[t].name), uwidth_max(kFmts[t].type));

    int old = get_uformat();

    for(int t = 0; t < kNumFmts; ++t)
    {
        set_uformat(kFmts[t].type);
        ctx.check_hex(conf::nm("%s/get", kFmts[t].name), (unsigned)get_uformat());
        ctx.check_int(conf::nm("%s/uwidth_max_cur", kFmts[t].name), uwidth_max(U_CURRENT));
        set_uformat(U_CURRENT);
        ctx.check_hex(conf::nm("%s/after_cur", kFmts[t].name), (unsigned)get_uformat());
    }

    set_uformat(old);
    ctx.check_hex("restored", (unsigned)get_uformat());
    ctx.check_mem("empty_string", empty_string, 4);
}

CONF_TEST(ucfg_decode)
{
    static const char *const kBytes[] =
    {
        "", "A", "abc", "\x7f", "\x80", "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80",
        "\xf8\x88\x80\x80\x80", "\xfc\x84\x80\x80\x80\x80", "\xfe", "\xff", "\xc0\x80",
        "\xc3" "A", "\xe2\x82", "\xbf", "\xc3\xa9\xc3\xa9x", "\xe2\x82\xac\xf0\x9f\x98\x80z",
        "\xed\xa0\x80", "\xef\xbb\xbf" "a", "\xf4\x8f\xbf\xbf", "\xf7\xbf\xbf\xbf",
        "\xc1\xbf", "\xe0\x80\xaf", "\x80\x80\x80\x80", "\xdf\xbf", "\xff\xfe", "a\xff" "b",
        "\xfe\x80\x80\x80\x80\x80\x80", "\xfe\xbf\xbf\xbf\xbf\xbf\xbf", "\xff\x80\x80\x80\x80\x80\x80\x80",
        "\xff\xbf\xbf\xbf\xbf\xbf\xbf\xbf", "\xfd\xbf\xbf\xbf\xbf\xbf", "\xfc\x80\x80\x80\x80\x80",
        "\xfb\xbf\xbf\xbf\xbf", "\xf0\x80\x80\x80", "\xe0\x80\x80", "\xc2\x80\xc2", "\xf0\x9f\x98",
        "\xf0\x9f\x98" "A", "\xf0\x9f" "A", "\xe2" "\xc3\xa9"
    };
    const int nbytes = (int)(sizeof(kBytes) / sizeof(kBytes[0]));

    for(int t = 0; t < kNumFmts; ++t)
    {
        FormatGuard g(kFmts[t].type);
        conf::Seq s(ctx, kFmts[t].name);

        for(int i = 0; i < nbytes + 64; ++i)
        {
            char buf[32];
            memset(buf, 0, sizeof(buf));

            if(i < nbytes)
                strcpy(buf, kBytes[i]);
            else
            {
                int len = ctx.rng.range(0, 10);

                for(int k = 0; k < len; ++k)
                {
                    int b = ctx.rng.range(0, 3) == 0 ? ctx.rng.range(0, 127) : ctx.rng.range(0x80, 0xFF);
                    buf[k] = (char)(b ? b : 'q');
                }
            }

            std::string r = conf::nm("c=%x w=%d |", ugetc(buf), uwidth(buf));

            char *p = buf;

            for(int k = 0; k < 12; ++k)
            {
                int c = ugetx(&p);
                r += conf::nm(" %x@%d", c, (int)(p - buf));

                if(!c)
                    break;
            }

            r += " |";
            const char *q = buf;

            for(int k = 0; k < 12; ++k)
            {
                int c = ugetxc(&q);
                r += conf::nm(" %x@%d", c, (int)(q - buf));

                if(!c)
                    break;
            }

            s.add_str(r.c_str(), "bytes=%s", hexs(buf, 12).c_str());
        }
    }
}

#define SKIP_UTF8_ENCODE(c) ((c) >= 0x40000000)

CONF_TEST(ucfg_encode)
{
    static const int kValues[] =
    {
        0, 1, 'A', 0x7F, 0x80, 0xA9, 0xFF, 0x100, 0x17F, 0x7FF, 0x800, 0xFFF, 0xD800,
        0xFFFD, 0xFFFF, 0x10000, 0x10FFFF, 0x110000, 0x1FFFFF, 0x200000, 0x3FFFFFF,
        0x4000000, 0x7FFFFFFF, -1, -2, -128, -129, -256, -0x8000, -0x10000,
        (int)0x80000000, 0x20AC, 0x152, 0x2122, 0x1234, 0x10000000, 0x20000000, 0x3FFFFFFF
    };
    const int nvalues = (int)(sizeof(kValues) / sizeof(kValues[0]));

    for(int t = 0; t < kNumFmts; ++t)
    {
        FormatGuard g(kFmts[t].type);
        conf::Seq s(ctx, kFmts[t].name);

        for(int i = 0; i < nvalues + 200; ++i)
        {
            int c;

            if(i < nvalues)
                c = kValues[i];
            else if(i < nvalues + 64)
                c = 0x80 + (i - nvalues) * 2;          /* Latin-1 range */
            else
            {
                switch(ctx.rng.range(0, 3))
                {
                case 0: c = ctx.rng.range(0, 0x7FF); break;
                case 1: c = ctx.rng.range(0, 0xFFFF); break;
                case 2: c = ctx.rng.range(0, 0x10FFFF); break;
                default: c = (int)ctx.rng.next(); break;
                }
            }

            /* The reference's UTF-8 encoder never returns for some values
             * (no valid input reaches them). */
            if(kFmts[t].type == U_UTF8 && SKIP_UTF8_ENCODE(c))
                continue;

            unsigned char buf[16];
            memset(buf, 0xAA, sizeof(buf));
            int w = usetc((char *)buf, c);
            s.add_str(conf::nm("ret=%d cw=%d ok=%d %s", w, ucwidth(c), uisok(c),
                               hexs(buf, 8).c_str()), "c=%x", (unsigned)c);
        }
    }
}

/* The default U_ASCII_CP tables (used by allegro_message): every 8-bit
 * value read, and every code point the reduction maps to something other
 * than '^'. */
CONF_TEST(ucfg_ascii_cp_tables)
{
    FormatGuard g(U_ASCII_CP);
    conf::Seq rd(ctx, "read");

    for(int b = 1; b < 256; ++b)
    {
        char s[4] = { (char)b, 0, 0, 0 };
        rd.add_hex((unsigned)ugetc(s), "byte=%02x", b);
    }

    rd.end();
    conf::Seq wr(ctx, "write");
    int n = 0;

    for(int c = 0; c < 0x30000; ++c)
    {
        unsigned char buf[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
        int w = usetc((char *)buf, c);
        int ok = uisok(c);

        if(c == '^' || buf[0] != '^' || ok || w != 1 || buf[1] != 0xAA)
        {
            wr.add_str(conf::nm("%x>%02x w=%d ok=%d", c, buf[0], w, ok));
            ++n;
        }
    }

    wr.end();
    ctx.check_int("write_count", n);
}

CONF_TEST(ucfg_position)
{
    for(int t = 0; t < kNumFmts; ++t)
    {
        FormatGuard g(kFmts[t].type);
        conf::Seq s(ctx, kFmts[t].name);

        for(int si = 0; si < kNumStrings; ++si)
        {
            char base[64];
            memset(base, 0, sizeof(base));
            build(base, kStrings[si].c, kStrings[si].n);

            for(int idx = -8; idx <= 8; ++idx)
            {
                s.add_str(conf::nm("off=%d at=%x", uoffset(base, idx), ugetat(base, idx)),
                          "str=%d idx=%d", si, idx);

                static const int kSet[] = { 'Z', 0xE9, 0x20AC, 0x10400 };

                for(int k = 0; k < 4; ++k)
                {
                    char b[64];
                    memcpy(b, base, sizeof(b));
                    int r = usetat(b, idx, kSet[k]);
                    s.add_str(conf::nm("usetat=%d %s", r, hexs(b, 40).c_str()),
                              "str=%d idx=%d c=%x", si, idx, kSet[k]);

                    memcpy(b, base, sizeof(b));
                    r = uinsert(b, idx, kSet[k]);
                    s.add_str(conf::nm("uinsert=%d %s", r, hexs(b, 40).c_str()),
                              "str=%d idx=%d c=%x", si, idx, kSet[k]);
                }

                char b[64];
                memcpy(b, base, sizeof(b));
                int r = uremove(b, idx);
                s.add_str(conf::nm("uremove=%d %s", r, hexs(b, 40).c_str()),
                          "str=%d idx=%d", si, idx);
            }
        }
    }
}

/* Malformed UTF-8: where the string routines step by the lead byte and where
 * by the decoded sequence. */
CONF_TEST(ucfg_malformed)
{
    static const char *const kBad[] =
    {
        "a\xc3" "Ab", "\x80xyb", "x\xc0\x80yb", "\xe2\x82" "Azb", "\xfe" "ab", "a\xf0\x9f\x98" "b",
        "\xc3\xa9\xff" "b", "b\xbf\xbf" "b"
    };
    FormatGuard g(U_UTF8);
    conf::Seq s(ctx, "utf8");

    for(int i = 0; i < (int)(sizeof(kBad) / sizeof(kBad[0])); ++i)
    {
        char b[64];
        memset(b, 0, sizeof(b));
        strcpy(b, kBad[i]);
        s.add_str(conf::nm("len=%d size=%d sizez=%d", ustrlen(b), ustrsize(b), ustrsizez(b)), "len %d", i);

        std::string r;

        for(int idx = -3; idx <= 6; ++idx)
            r += conf::nm(" %d:%d/%x", idx, uoffset(b, idx), ugetat(b, idx));

        s.add_str(r.c_str(), "offsets %d", i);

        char *p = ustrchr(b, 'b');
        char *q = ustrrchr(b, 'b');
        char *t = ustrpbrk(b, "zb");
        char *u = ustrstr(b, "b");
        s.add_str(conf::nm("chr=%d rchr=%d pbrk=%d str=%d cmp=%d icmp=%d ncmp=%d",
                           off_in(p, b), off_in(q, b), off_in(t, b), off_in(u, b),
                           ustrcmp(b, "ab"), ustricmp(b, "AB"), ustrncmp(b, "a^", 2)), "search %d", i);

        char *dup = ustrdup(b);
        s.add_str(hexs(dup, (int)strlen(dup) + 1).c_str(), "dup %d", i);
        free(dup);

        char d[64];
        memset(d, 0x55, sizeof(d));
        ustrzcpy(d, 32, b);
        s.add_str(hexs(d, 16).c_str(), "zcpy %d", i);
        memset(d, 0x55, sizeof(d));
        ustrzncpy(d, 32, b, 3);
        s.add_str(hexs(d, 16).c_str(), "zncpy %d", i);

        for(int idx = 0; idx < 3; ++idx)
        {
            memcpy(d, b, sizeof(d));
            int r1 = usetat(d, idx, 'Z');
            s.add_str(conf::nm("%d %s", r1, hexs(d, 16).c_str()), "usetat %d idx=%d", i, idx);
            memcpy(d, b, sizeof(d));
            r1 = uinsert(d, idx, 0xE9);
            s.add_str(conf::nm("%d %s", r1, hexs(d, 16).c_str()), "uinsert %d idx=%d", i, idx);
            memcpy(d, b, sizeof(d));
            r1 = uremove(d, idx);
            s.add_str(conf::nm("%d %s", r1, hexs(d, 16).c_str()), "uremove %d idx=%d", i, idx);
        }
        memcpy(d, b, sizeof(d));
        ustrupr(d);
        s.add_str(hexs(d, 16).c_str(), "upr %d", i);

        memcpy(d, b, sizeof(d));
        char *last = 0;
        char *tok = ustrtok_r(d, "yz", &last);
        s.add_str(conf::nm("%d %d %s", off_in(tok, d), off_in(last, d), hexs(d, 16).c_str()), "tok %d", i);
        memcpy(d, b, sizeof(d));
        last = 0;
        tok = ustrtok_r(d, "b", &last);
        s.add_str(conf::nm("%d %d %s", off_in(tok, d), off_in(last, d), hexs(d, 16).c_str()), "tok b %d", i);
        memcpy(d, b, sizeof(d));
        last = 0;
        tok = ustrtok_r(d, "\xc3", &last);
        s.add_str(conf::nm("%d %d %s", off_in(tok, d), off_in(last, d), hexs(d, 16).c_str()), "tok c3 %d", i);

        unsigned char o[64];
        memset(o, 0x55, sizeof(o));
        do_uconvert(b, U_UTF8, (char *)o, U_ASCII, 32);
        s.add_str(hexs(o, 12).c_str(), "to ascii %d", i);
        s.add_int(uconvert_size(b, U_UTF8, U_UNICODE), "size unicode %d", i);

        char pf[64];
        memset(pf, 0x55, sizeof(pf));
        int n = uszprintf(pf, 40, "[%s|%3s|%.2s]", b, b, b);
        s.add_str(conf::nm("%d %s", n, hexs(pf, 24).c_str()), "printf %d", i);
        memset(pf, 0x55, sizeof(pf));
        n = uszprintf(pf, 40, b);
        s.add_str(conf::nm("%d %s", n, hexs(pf, 12).c_str()), "printf fmt %d", i);
    }

    /* uconvert's static buffer (buf == NULL) with long input. */
    char big[3000];

    for(int i = 0; i < 2999; ++i)
        big[i] = (char)(i % 3 == 0 ? 0xE9 : 'a' + i % 26);

    big[2999] = 0;
    char *r1 = uconvert(big, U_ASCII, NULL, U_UTF8, 0);
    ctx.check_int("static_utf8_len", (long long)strlen(r1));
    ctx.check_hash("static_utf8", conf::hash_bytes(r1, strlen(r1) + 1));
    char *r2 = uconvert(big, U_ASCII, NULL, U_UNICODE, 0);
    int n2 = 0;

    while(r2[n2] || r2[n2 + 1])
        n2 += 2;

    ctx.check_int("static_unicode_len", n2);
}

CONF_TEST(ucfg_ctype)
{
    /* Case mapping and classes over the BMP and the supplementary planes
     * that have letters; only the code points where something happens are
     * listed. */
    const int kMax = 0x20000;
    conf::Seq lower(ctx, "utolower");
    conf::Seq upper(ctx, "utoupper");
    conf::Seq space(ctx, "uisspace");
    conf::Seq digit(ctx, "uisdigit");
    int nl = 0, nu = 0, ns = 0, nd = 0;

    for(int c = 0; c < kMax; ++c)
    {
        int l = utolower(c), u = utoupper(c);

        if(l != c)
        {
            lower.add_str(conf::nm("%x>%x", c, l));
            ++nl;
        }

        if(u != c)
        {
            upper.add_str(conf::nm("%x>%x", c, u));
            ++nu;
        }

        if(uisspace(c))
        {
            space.add_str(conf::nm("%x=%d", c, uisspace(c)));
            ++ns;
        }

        if(uisdigit(c))
        {
            digit.add_str(conf::nm("%x=%d", c, uisdigit(c)));
            ++nd;
        }
    }

    lower.end();
    upper.end();
    space.end();
    digit.end();
    ctx.check_int("n_lower", nl);
    ctx.check_int("n_upper", nu);
    ctx.check_int("n_space", ns);
    ctx.check_int("n_digit", nd);

    static const int kOdd[] =
    {
        -1, -2, -32, -65, -0x7F, -0x80, -0xFF, -0x100, -0x10000, 0x20000, 0x2FFFF,
        0x10FFFF, 0x110000, 0x7FFFFFFF, (int)0x80000000, 0x7FFFFF41, 0x10041
    };
    conf::Seq odd(ctx, "odd");

    for(size_t i = 0; i < sizeof(kOdd) / sizeof(kOdd[0]); ++i)
    {
        int c = kOdd[i];
        odd.add_str(conf::nm("l=%x u=%x s=%d d=%d", utolower(c), utoupper(c),
                             uisspace(c), uisdigit(c)), "c=%x", (unsigned)c);
    }
}

CONF_TEST(ucfg_length_compare)
{
    for(int t = 0; t < kNumFmts; ++t)
    {
        FormatGuard g(kFmts[t].type);
        conf::Seq s(ctx, kFmts[t].name);
        char bufs[kNumStrings][64];

        for(int si = 0; si < kNumStrings; ++si)
        {
            memset(bufs[si], 0, 64);
            build(bufs[si], kStrings[si].c, kStrings[si].n);
            s.add_str(conf::nm("len=%d size=%d sizez=%d", ustrlen(bufs[si]),
                               ustrsize(bufs[si]), ustrsizez(bufs[si])), "str=%d", si);
        }

        /* Case variants for the comparisons. */
        static const Cps kCmp[] =
        {
            { 3, { 'A', 'B', 'C' } },
            { 3, { 'a', 'b', 'c' } },
            { 4, { 'a', 'b', 'c', 'd' } },
            { 2, { 'a', 'B' } },
            { 3, { 0xC9, 'x', 'Y' } },
            { 3, { 0xE9, 'X', 'y' } },
            { 2, { 0x3A3, 0x416 } },
            { 2, { 0x3C3, 0x436 } },
            { 2, { 0x3C2, 0x436 } },
            { 1, { 0x20AC } },
            { 1, { 0xFF } },
            { 1, { 0x178 } },
            { 0, { 0 } },
            { 3, { 'a', 0x10000, 'b' } },
            { 3, { 'a', 0xFFFF, 'b' } },
        };
        const int ncmp = (int)(sizeof(kCmp) / sizeof(kCmp[0]));
        char cb[ncmp][64];

        for(int i = 0; i < ncmp; ++i)
        {
            memset(cb[i], 0, 64);
            build(cb[i], kCmp[i].c, kCmp[i].n);
        }

        for(int i = 0; i < ncmp; ++i)
            for(int j = 0; j < ncmp; ++j)
            {
                std::string r = conf::nm("cmp=%d icmp=%d", ustrcmp(cb[i], cb[j]), ustricmp(cb[i], cb[j]));

                for(int n = 0; n <= 4; ++n)
                    r += conf::nm(" n%d=%d/%d", n, ustrncmp(cb[i], cb[j], n), ustrnicmp(cb[i], cb[j], n));

                r += conf::nm(" n-1=%d/%d", ustrncmp(cb[i], cb[j], -1), ustrnicmp(cb[i], cb[j], -1));
                s.add_str(r.c_str(), "a=%d b=%d", i, j);
            }
    }
}

CONF_TEST(ucfg_search)
{
    static const int kChars[] = { 'a', 'o', '.', '/', 'b', 0, 0xE9, 0x20AC, 0x10000, 'x', 'Z', 0xFF, ' ' };
    const int nchars = (int)(sizeof(kChars) / sizeof(kChars[0]));

    for(int t = 0; t < kNumFmts; ++t)
    {
        FormatGuard g(kFmts[t].type);
        conf::Seq s(ctx, kFmts[t].name);

        for(int si = 0; si < kNumStrings; ++si)
        {
            char b[64];
            memset(b, 0, sizeof(b));
            build(b, kStrings[si].c, kStrings[si].n);

            for(int k = 0; k < nchars; ++k)
            {
                char *p = ustrchr(b, kChars[k]);
                char *q = ustrrchr(b, kChars[k]);
                s.add_str(conf::nm("chr=%d rchr=%d", p ? (int)(p - b) : -1, q ? (int)(q - b) : -1),
                          "str=%d c=%x", si, kChars[k]);
            }

            for(int sj = 0; sj < kNumStrings; ++sj)
            {
                char n[64];
                memset(n, 0, sizeof(n));
                build(n, kStrings[sj].c, kStrings[sj].n < 3 ? kStrings[sj].n : 2);
                char *p = ustrstr(b, n);
                char *q = ustrpbrk(b, n);
                s.add_str(conf::nm("str=%d pbrk=%d", p ? (int)(p - b) : -1, q ? (int)(q - b) : -1),
                          "hay=%d needle=%d", si, sj);
            }
        }
    }
}

CONF_TEST(ucfg_tok)
{
    static const char *const kInputs[] =
    {
        "*.qst", "*.qst;*.zqt", "bmp;pcx;tga", "  a  b ,c;;d  ", "", ";;;", ",x,", "single",
        "a /+r-h", "/r", "png;gif /+d", " lead", "trail ", "x\xc3\xa9y\xc3\xa9z"
    };
    const int ninputs = (int)(sizeof(kInputs) / sizeof(kInputs[0]));
    static const char *const kSets[] = { " ,;", ";", " ", "\xc3\xa9", "", "xyz" };
    const int nsets = (int)(sizeof(kSets) / sizeof(kSets[0]));

    for(int t = 0; t < kNumGameFmts; ++t)
    {
        FormatGuard g(kGameFmts[t].type);
        conf::Seq s(ctx, conf::nm("%s_r", kGameFmts[t].name));
        conf::Seq s2(ctx, conf::nm("%s_plain", kGameFmts[t].name));

        for(int i = 0; i < ninputs; ++i)
            for(int k = 0; k < nsets; ++k)
            {
                char b[64];
                memset(b, 0, sizeof(b));
                strcpy(b, kInputs[i]);
                char *last = (char *)1;
                std::string r;
                char *p = ustrtok_r(b, kSets[k], &last);

                for(int n = 0; n < 12; ++n)
                {
                    r += conf::nm("[%d,%d]", off_in(p, b), off_in(last, b));

                    if(!p)
                        break;

                    p = ustrtok_r(NULL, kSets[k], &last);
                }

                r += " ";
                r += hexs(b, (int)strlen(kInputs[i]) + 2);
                s.add_str(r.c_str(), "in=%d set=%d", i, k);

                memset(b, 0, sizeof(b));
                strcpy(b, kInputs[i]);
                r = "";
                p = ustrtok(b, kSets[k]);

                for(int n = 0; n < 12; ++n)
                {
                    r += conf::nm("[%d]", off_in(p, b));

                    if(!p)
                        break;

                    p = ustrtok(NULL, kSets[k]);
                }

                r += " ";
                r += hexs(b, (int)strlen(kInputs[i]) + 2);
                s2.add_str(r.c_str(), "in=%d set=%d", i, k);
            }
    }
}

CONF_TEST(ucfg_strtol)
{
    static const char *const kInputs[] =
    {
        "0", "1", "-1", "+1", "42", "  42", "\t\n 42x", "12abc", "0x1F", "0X1f", "0x", "0xg",
        "010", "08", "-0x10", "+0x10", "2147483647", "2147483648", "-2147483648",
        "-2147483649", "99999999999999999999", "-99999999999999999999", "", " ", "-", "+",
        "abc", "zz", "ZZ", "1010", "777", "ff", "FF", "7fffffff", "80000000", "ffffffff",
        "100000000", "-ffffffff", "12\xc3\xa9", "\xc3\xa9" "12", "1 2", "0b101", "--1", "+-1",
        "00000000000000000000000000000001", "1e5", "0x7fffffff", "0x80000000", "0xffffffff",
        "4294967295", "4294967296", "-4294967295", "36", "z1", "\xef\xbc\x91\xef\xbc\x92",
        "-0x", " 0xz", "+0x", "  -0X", "0x 5", "\xe3\x80\x80" "5", "\xc2\xa0" "5", "\x0b" "5",
        "\x0c" "5", "\r5", " \t-12", "0x0", "00x1", "0x-1", "- 1"
    };
    const int ninputs = (int)(sizeof(kInputs) / sizeof(kInputs[0]));
    static const int kBases[] = { 0, 10, 16, 8, 2, 36, 7 };
    const int nbases = (int)(sizeof(kBases) / sizeof(kBases[0]));

    for(int t = 0; t < kNumGameFmts; ++t)
    {
        FormatGuard g(kGameFmts[t].type);
        conf::Seq s(ctx, kGameFmts[t].name);

        for(int i = 0; i < ninputs; ++i)
            for(int k = 0; k < nbases; ++k)
            {
                char b[64];
                memset(b, 0, sizeof(b));
                strcpy(b, kInputs[i]);
                char *end = (char *)1;
                errno = 0;
                long v = ustrtol(b, &end, kBases[k]);
                int e = errno;
                s.add_str(conf::nm("v=%ld end=%d errno=%d", v, off_in(end, b), e),
                          "in=%d base=%d", i, kBases[k]);
            }

        /* NULL endp */
        s.add_int(ustrtol("123", NULL, 10), "null end");
    }
}

CONF_TEST(ucfg_strtod)
{
    static const char *const kInputs[] =
    {
        "0", "1", "-1", "1.5", "  -2.25e3x", ".5", "5.", "-.5e-1", "1e", "1e+", "1e-", "e5",
        "inf", "-inf", "INF", "infinity", "nan", "NAN", "nan(123)", "0x1p3", "0x1.8p1", "0x",
        "1e308", "1e309", "-1e309", "1e-307", "1e-320", "1e-400", "", " ", "abc", "+", "-",
        "3.14159265358979323846264338327950288", "123456789012345678901234567890",
        "0.1", "0.2", "0.3", "2.5", "1,5", "12\xc3\xa9", "\xc3\xa9", "4.9406564584124654e-324",
        "2.2250738585072011e-308", "1.7976931348623157e308", "1.7976931348623158e308",
        "9007199254740993", "0.000001", "100", "  +7.0E2  ", "-nan", "+nan", "-inf", "+inf", "nan(ind)",
        "-0", "1e-310", "0x1.fffffffffffffp1023", "0X1P-1074", "\t\n\v\f\r 3", "\xe3\x80\x80" "3"
    };
    const int ninputs = (int)(sizeof(kInputs) / sizeof(kInputs[0]));

    for(int t = 0; t < kNumGameFmts; ++t)
    {
        FormatGuard g(kGameFmts[t].type);
        conf::Seq s(ctx, kGameFmts[t].name);

        for(int i = 0; i < ninputs; ++i)
        {
            char b[64];
            memset(b, 0, sizeof(b));
            strcpy(b, kInputs[i]);
            char *end = (char *)1;
            double v = ustrtod(b, &end);
            double a = uatof(b);
            unsigned long long bits, abits;
            memcpy(&bits, &v, 8);
            memcpy(&abits, &a, 8);
            s.add_str(conf::nm("v=%016llx end=%d atof=%016llx", bits, off_in(end, b), abits),
                      "in=%d", i);
        }
    }
}

CONF_TEST(ucfg_copy)
{
    for(int t = 0; t < kNumFmts; ++t)
    {
        FormatGuard g(kFmts[t].type);
        conf::Seq s(ctx, kFmts[t].name);

        for(int si = 0; si < kNumStrings; ++si)
        {
            char src[64];
            memset(src, 0, sizeof(src));
            build(src, kStrings[si].c, kStrings[si].n);

            char pre[64];
            memset(pre, 0, sizeof(pre));
            static const int kPre[] = { 'p', 0xE9, 'q' };
            build(pre, kPre, 3);

            for(int size = 1; size <= 24; ++size)
            {
                unsigned char d[48];

                memset(d, 0xAA, sizeof(d));
                ustrzcpy((char *)d, size, src);
                s.add_str(hexs(d, 28).c_str(), "zcpy str=%d size=%d", si, size);

                memset(d, 0xAA, sizeof(d));
                memcpy(d, pre, 8);
                ustrzcat((char *)d, size, src);
                s.add_str(hexs(d, 28).c_str(), "zcat str=%d size=%d", si, size);

                for(int n = -1; n <= 5; n += 2)
                {
                    memset(d, 0xAA, sizeof(d));
                    ustrzncpy((char *)d, size, src, n);
                    s.add_str(hexs(d, 28).c_str(), "zncpy str=%d size=%d n=%d", si, size, n);

                    memset(d, 0xAA, sizeof(d));
                    memcpy(d, pre, 8);
                    ustrzncat((char *)d, size, src, n);
                    s.add_str(hexs(d, 28).c_str(), "zncat str=%d size=%d n=%d", si, size, n);
                }
            }

            char *dup = ustrdup(src);
            s.add_str(dup ? hexs(dup, ustrsizez(src)).c_str() : "(null)", "dup str=%d", si);
            free(dup);

            char b[64];
            memcpy(b, src, sizeof(b));
            ustrlwr(b);
            s.add_str(hexs(b, 40).c_str(), "lwr str=%d", si);
            memcpy(b, src, sizeof(b));
            ustrupr(b);
            s.add_str(hexs(b, 40).c_str(), "upr str=%d", si);
        }

        /* Case changes that change the encoded width in UTF-8. */
        static const Cps kWidth[] =
        {
            { 3, { 0x131, 'a', 0x17F } },
            { 3, { 'i', 0x1E9E, 'k' } },
            { 3, { 0x212A, 'x', 0x2126 } },
            { 2, { 0x23A, 0x2C65 } },
        };

        for(int i = 0; i < 4; ++i)
        {
            char b[64];
            memset(b, 0, sizeof(b));
            build(b, kWidth[i].c, kWidth[i].n);
            char c2[64];
            memcpy(c2, b, sizeof(c2));
            ustrlwr(b);
            ustrupr(c2);
            s.add_str(conf::nm("%s %s", hexs(b, 16).c_str(), hexs(c2, 16).c_str()), "width %d", i);
        }

        /* A source whose terminator is followed by more text: the padding of
         * ustrzncpy must not read past the terminator. */
        {
            char src[32];
            memset(src, 0, sizeof(src));
            static const int kAb[] = { 'a', 'b' };
            static const int kXyz[] = { 'X', 'Y', 'Z' };
            int pos = build(src, kAb, 2);
            build(src + pos, kXyz, 3);

            for(int n = 1; n <= 7; ++n)
            {
                unsigned char d[48];
                memset(d, 0xAA, sizeof(d));
                ustrzncpy((char *)d, 40, src, n);
                s.add_str(hexs(d, 24).c_str(), "garbage zncpy n=%d", n);
                memset(d, 0xAA, sizeof(d));
                memset(d, 0, 4);
                ustrzncat((char *)d, 40, src, n);
                s.add_str(hexs(d, 24).c_str(), "garbage zncat n=%d", n);
            }
        }
    }
}

CONF_TEST(ucfg_convert)
{
    static const char *const kAscii[] =
    {
        "", "A", "hello world", "*.qst", "\x7f", "\x80\xe9\xff", "caf\xe9", "\xa4\xa3"
    };
    static const char *const kUtf8[] =
    {
        "", "A", "hello", "\xc3\xa9", "\xe2\x82\xac" "5", "\xf0\x9f\x98\x80", "a\xc3\xbf" "b",
        "\xc5\x92", "\xff", "\xc3"
    };

    /* need_uconvert / uconvert_size / do_uconvert between every pair of the
     * explicit formats, from strings built in the source format. */
    for(int a = 0; a < kNumFmts; ++a)
    {
        conf::Seq s(ctx, conf::nm("from_%s", kFmts[a].name));

        for(int i = 0; i < 18; ++i)
        {
            char src[64];
            memset(src, 0, sizeof(src));

            if(i < 8)
            {
                /* ASCII bytes interpreted through the source format's
                 * encoder, so every format gets the same characters. */
                FormatGuard g(kFmts[a].type);
                int pos = 0;

                for(const unsigned char *p = (const unsigned char *)kAscii[i]; *p; ++p)
                    pos += usetc(src + pos, *p);

                usetc(src + pos, 0);
            }
            else if(i < 18)
            {
                FormatGuard g(U_UTF8);
                const char *p = kUtf8[i - 8];
                int cps[16], n = 0;

                while(n < 15)
                {
                    int c = ugetxc(&p);

                    if(!c)
                        break;

                    cps[n++] = c;
                }

                set_uformat(kFmts[a].type);
                build(src, cps, n);
            }

            for(int b = 0; b < kNumFmts; ++b)
            {
                s.add_str(conf::nm("need=%d size=%d",
                                   need_uconvert(src, kFmts[a].type, kFmts[b].type),
                                   uconvert_size(src, kFmts[a].type, kFmts[b].type)),
                          "str=%d to=%s", i, kFmts[b].name);

                static const int kSizes[] = { 1, 2, 3, 4, 5, 7, 64 };

                for(int k = 0; k < 7; ++k)
                {
                    unsigned char out[80];
                    memset(out, 0xAA, sizeof(out));
                    do_uconvert(src, kFmts[a].type, (char *)out, kFmts[b].type, kSizes[k]);
                    s.add_str(hexs(out, kSizes[k] < 40 ? kSizes[k] + 2 : 40).c_str(),
                              "do_uconvert str=%d to=%s size=%d", i, kFmts[b].name, kSizes[k]);
                }

                unsigned char out[80];
                memset(out, 0xAA, sizeof(out));
                char *res = uconvert(src, kFmts[a].type, (char *)out, kFmts[b].type, 64);
                s.add_str(conf::nm("%s %s", res == src ? "src" : res == (char *)out ? "buf" : "other",
                                   hexs(res, 24).c_str()), "uconvert str=%d to=%s", i, kFmts[b].name);
            }
        }
    }

    /* U_CURRENT, the macros and the static buffer, in the game formats. */
    for(int t = 0; t < kNumGameFmts; ++t)
    {
        FormatGuard g(kGameFmts[t].type);
        conf::Seq s(ctx, conf::nm("cur_%s", kGameFmts[t].name));

        for(int i = 0; i < 8; ++i)
        {
            char buf[64];
            memset(buf, 0x55, sizeof(buf));
            const char *in = kAscii[i];
            char *r1 = uconvert_ascii(in, buf);
            s.add_str(conf::nm("%s %s", r1 == in ? "src" : r1 == buf ? "buf" : "other",
                               hexz(r1, U_CURRENT, 12).c_str()), "uconvert_ascii %d", i);

            char *r2 = uconvert_ascii(in, NULL);
            s.add_str(conf::nm("%s %s", r2 == in ? "src" : "other", hexz(r2, U_CURRENT, 12).c_str()),
                      "uconvert_ascii NULL %d", i);

            char tmp[8];
            memset(tmp, 0x55, sizeof(tmp));
            char *r3 = uconvert_ascii(in, tmp);
            s.add_str(conf::nm("%s %s", r3 == in ? "src" : r3 == tmp ? "buf" : "other",
                               hexz(r3, U_CURRENT, 8).c_str()), "uconvert_ascii small %d", i);

            s.add_int(need_uconvert(in, U_ASCII, U_CURRENT), "need ascii->cur %d", i);
            s.add_int(need_uconvert(in, U_CURRENT, U_ASCII), "need cur->ascii %d", i);
            s.add_int(uconvert_size(in, U_CURRENT, U_UNICODE), "size cur->unicode %d", i);
        }

        for(int i = 0; i < 10; ++i)
        {
            char buf[64];
            memset(buf, 0x55, sizeof(buf));
            const char *in = kUtf8[i];
            char *r1 = uconvert_toascii(in, buf);
            s.add_str(conf::nm("%s %s", r1 == in ? "src" : r1 == buf ? "buf" : "other",
                               hexz(r1, U_ASCII, 12).c_str()), "uconvert_toascii %d", i);
            char *r2 = uconvert(in, U_UTF8, NULL, U_CURRENT, 0);
            s.add_str(conf::nm("%s %s", r2 == in ? "src" : "other", hexz(r2, U_CURRENT, 12).c_str()),
                      "utf8->cur NULL %d", i);
            s.add_int(need_uconvert(in, U_UTF8, U_CURRENT), "need utf8->cur %d", i);
        }
    }
}

namespace
{

/* A custom format for register_uformat: one byte per character, stored
 * XOR 0x20. */
int x_getc(const char *s) { return (unsigned char)*s ^ 0x20; }
int x_getx(char **s) { return (unsigned char)*((*s)++) ^ 0x20; }
int x_setc(char *s, int c) { *s = (char)(c ^ 0x20); return 1; }
int x_width(const char *) { return 1; }
int x_cwidth(int) { return 1; }
int x_isok(int c) { return c >= 0 && c < 256; }

} // namespace

/* register_uformat and set_ucodepage (other modules may use them). */
CONF_TEST(ucfg_custom_formats)
{
    const int kX = AL_ID('X', 'O', 'R', ' ');
    register_uformat(kX, x_getc, x_getx, x_setc, x_width, x_cwidth, x_isok, 1);
    ctx.check_int("width_max", uwidth_max(kX));
    char out[32];
    memset(out, 0x55, sizeof(out));
    do_uconvert("Hello", U_ASCII, out, kX, 32);
    ctx.check_str("to_custom", hexs(out, 8).c_str());
    char back[32];
    memset(back, 0x55, sizeof(back));
    do_uconvert(out, kX, back, U_UTF8, 32);
    ctx.check_str("from_custom", hexs(back, 8).c_str());
    ctx.check_int("need", need_uconvert("Hello", U_ASCII, kX));
    ctx.check_int("size", uconvert_size("Hello", U_ASCII, kX));

    {
        FormatGuard g(kX);
        ctx.check_hex("current", (unsigned)get_uformat());
        /* In this format the terminator is the byte 0x20: fill with it, or
         * ustrlen() runs past the buffer (into whatever the stack holds, which
         * differs between builds). */
        char b[16];
        memset(b, 0x20, sizeof(b));
        int w = usetc(b, 'a');
        ctx.check_int("usetc", w);
        ctx.check_str("usetc_bytes", hexs(b, 2).c_str());
        ctx.check_int("ustrlen", ustrlen(b));
        /* "%d" plus terminator, encoded: '%' 0x25 ^ 0x20, 'd' 0x64 ^ 0x20, 0 ^ 0x20 */
        ctx.check_int("uszprintf", uszprintf(b, 16, "\x05\x44\x20", 42));
        ctx.check_str("uszprintf_bytes", hexs(b, 6).c_str());
    }

    /* A codepage: bytes 0x80.. map to Greek capitals, extras reduce two
     * accented letters. */
    static unsigned short table[256];
    static const unsigned short extras[] = { 0xE9, 'e', 0x3B1, 0x80, 0 };

    for(int i = 0; i < 256; ++i)
        table[i] = (unsigned short)(i < 0x80 ? i : (i < 0x98 ? 0x391 + (i - 0x80) : '?'));

    set_ucodepage(table, extras);
    {
        FormatGuard g(U_ASCII_CP);
        conf::Seq s(ctx, "codepage");
        static const int kChars[] = { 'A', 0x391, 0x392, 0x3A8, 0x3B1, 0xE9, 0xEA, '?', 0x20AC };

        for(size_t i = 0; i < sizeof(kChars) / sizeof(kChars[0]); ++i)
        {
            unsigned char b[4] = { 0xAA, 0xAA, 0xAA, 0xAA };
            usetc((char *)b, kChars[i]);
            s.add_str(conf::nm("%02x ok=%d", b[0], uisok(kChars[i])), "c=%x", kChars[i]);
        }

        for(int bt = 0x7E; bt < 0x9A; ++bt)
        {
            char b[2] = { (char)bt, 0 };
            s.add_hex((unsigned)ugetc(b), "byte=%02x", bt);
        }
    }

    /* The default tables cannot be put back (set_ucodepage(NULL, NULL) makes
     * the reference crash on the next U_ASCII_CP character), so this test
     * stays the last of the file. */
}

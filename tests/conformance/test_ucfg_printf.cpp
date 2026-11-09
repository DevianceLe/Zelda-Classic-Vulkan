/*
 * test_ucfg_printf.cpp - uszprintf/usprintf/uvszprintf must format exactly
 * like the reference build: every textprintf in the player (HUD counters,
 * menus, the subscreen, debug text) goes through them, in U_ASCII and U_UTF8.
 *
 * Covers: every conversion (d i u o x X c s f F e E g G a A p n %) with
 * flags, widths, precisions and '*', length modifiers, truncation and the
 * return value at every buffer size, multibyte characters and strings,
 * special floating-point values, and malformed format strings.
 */
#include "conf.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <string>

namespace
{

struct Fmt
{
    int type;
    const char *name;
};

const Fmt kGameFmts[] =
{
    { U_UTF8, "utf8" },
    { U_ASCII, "ascii" }
};
const int kNumGameFmts = 2;

class FormatGuard
{
public:
    explicit FormatGuard(int type) : old_(get_uformat()) { set_uformat(type); }
    ~FormatGuard() { set_uformat(old_); }
private:
    int old_;
};

std::string esc(const char *p, int n)
{
    std::string r;

    for(int i = 0; i < n; ++i)
    {
        unsigned char c = (unsigned char)p[i];

        if(c >= 0x20 && c < 0x7F && c != '\\')
            r += (char)c;
        else
            r += conf::nm("\\x%02x", c);
    }

    return r;
}

/* Buffer with guard bytes on both sides. */
struct Out
{
    char raw[600];
    char *buf() { return raw + 16; }
    void reset() { memset(raw, 0x55, sizeof(raw)); }

    std::string describe(int ret)
    {
        std::string r = conf::nm("r=%d \"", ret);
        int n = 0;

        while(n < 560 && buf()[n])
            ++n;

        r += esc(buf(), n);
        r += "\"";

        /* What lies after the terminator, and whether anything before the
         * buffer was touched. */
        r += conf::nm(" tail=%02x%02x", (unsigned char)buf()[n + 1], (unsigned char)buf()[n + 2]);

        for(int i = 0; i < 16; ++i)
            if((unsigned char)raw[i] != 0x55)
            {
                r += " UNDERRUN";
                break;
            }

        return r;
    }

    /* describe() plus every byte of the first size + 2 bytes. */
    std::string describe_raw(int ret, int size)
    {
        std::string r = describe(ret) + " raw=";

        for(int i = 0; i < size + 2; ++i)
            r += conf::nm("%02x", (unsigned char)buf()[i]);

        return r;
    }
};

int vwrap(char *buf, int size, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int r = uvszprintf(buf, size, format, ap);
    va_end(ap);
    return r;
}

} // namespace

/* One formatted case: uszprintf into a 512-byte buffer, labelled with the
 * call's source text. */
#define PF(...)                                                              \
    do {                                                                     \
        o.reset();                                                           \
        int r_ = uszprintf(o.buf(), 512, __VA_ARGS__);                       \
        s.add_str(o.describe(r_).c_str(), "%s", #__VA_ARGS__);               \
    } while(0)

CONF_TEST(ucfg_printf_int)
{
    for(int t = 0; t < kNumGameFmts; ++t)
    {
        FormatGuard g(kGameFmts[t].type);
        conf::Seq s(ctx, kGameFmts[t].name);
        Out o;

        static const int kInts[] = { 0, 1, -1, 7, -7, 42, 100, 999, -1000, 12345, 65535, 65536,
                                     70000, -70000, INT_MAX, INT_MIN, INT_MAX - 1, INT_MIN + 1 };

        for(size_t i = 0; i < sizeof(kInts) / sizeof(kInts[0]); ++i)
        {
            int v = kInts[i];
            PF("%d", v);
            PF("%i", v);
            PF("%3d", v);
            PF("%-3d|", v);
            PF("%03d", v);
            PF("%-2d|", v);
            PF("%2d", v);
            PF("%-4d|", v);
            PF("%-6d|", v);
            PF("%+d", v);
            PF("% d", v);
            PF("%+5d", v);
            PF("% 5d", v);
            PF("%-+5d|", v);
            PF("%.3d", v);
            PF("%8.3d", v);
            PF("%-8.3d|", v);
            PF("%08.3d", v);
            PF("%.0d", v);
            PF("%010d", v);
            PF("%-010d|", v);
            PF("%u", v);
            PF("%5u", v);
            PF("%x", v);
            PF("%X", v);
            PF("%#x", v);
            PF("%#X", v);
            PF("%08x", v);
            PF("%#08x", v);
            PF("%-#8x|", v);
            PF("%.4x", v);
            PF("%o", v);
            PF("%#o", v);
            PF("%5o", v);
            PF("%ld", (long)v);
            PF("%li", (long)v);
            PF("%lu", (unsigned long)v);
            PF("%lx", (unsigned long)v);
            PF("%hd", v);
            PF("%hu", v);
            PF("%hx", v);
            PF("%hhd", v);
            PF("%hhx", v);
            PF("%*d", 6, v);
            PF("%-*d|", 6, v);
            PF("%*d", -6, v);
            PF("%.*d", 4, v);
            PF("%.*d", -1, v);
            PF("%*.*d", 7, 3, v);
            PF("%d %d %d", v, v / 2, -v);
        }

        static const long long kLL[] = { 0LL, 1LL, -1LL, 4294967296LL, -4294967296LL,
                                         9223372036854775807LL, (-9223372036854775807LL - 1) };

        for(size_t i = 0; i < sizeof(kLL) / sizeof(kLL[0]); ++i)
        {
            long long v = kLL[i];
            PF("%lld|%d", v, 77);
            PF("%llu|%d", (unsigned long long)v, 77);
            PF("%llx|%d", (unsigned long long)v, 77);
            PF("%I64d|%d", v, 77);
            PF("%qd|%d", v, 77);
            PF("%Ld|%d", v, 77);
        }
    }
}

CONF_TEST(ucfg_printf_char_string)
{
    for(int t = 0; t < kNumGameFmts; ++t)
    {
        FormatGuard g(kGameFmts[t].type);
        conf::Seq s(ctx, kGameFmts[t].name);
        Out o;

        static const int kChars[] = { 'A', ' ', '%', 0x7F, 0x80, 0xE9, 0xFF, 0x100, 0x20AC, 0x10000,
                                      0x10FFFF, -1, 0x141, 256 + 'A' };

        for(size_t i = 0; i < sizeof(kChars) / sizeof(kChars[0]); ++i)
        {
            int c = kChars[i];
            PF("%c", c);
            PF("[%3c]", c);
            PF("[%-3c]", c);
            PF("[%03c]", c);
            PF("[%.0c]", c);
            PF("%c%c", c, 'z');
            PF("%lc", c);
        }

        PF("a%cb", 0);
        PF("%c|%s", 0, "after");

        static const char *const kStrs[] =
        {
            "", "a", "abc", "hello world", "Link", "%d", "\xc3\xa9t\xc3\xa9", "\xe2\x82\xac\xe2\x82\xac" "x",
            "\xf0\x9f\x98\x80!", "caf\xe9", "\xff\xfe"
        };

        for(size_t i = 0; i < sizeof(kStrs) / sizeof(kStrs[0]); ++i)
        {
            const char *v = kStrs[i];
            PF("%s", v);
            PF("[%8s]", v);
            PF("[%-8s]", v);
            PF("[%08s]", v);
            PF("[%.2s]", v);
            PF("[%.0s]", v);
            PF("[%5.1s]", v);
            PF("[%-5.3s]", v);
            PF("[%*s]", 4, v);
            PF("[%-*s]", 4, v);
            PF("[%.*s]", 3, v);
            PF("[%.*s]", -2, v);
            PF("[%-8s|%3d]", v, 5);
            PF("%s%s", v, v);
        }

        PF("100%%");
        PF("%%d");
        PF("%5%|");
        PF("%-5%|");
        PF("plain text");
        PF("");
        PF("%s: %d/%d", "Hearts", 3, 16);
        PF("%3d%c%3d", 12, '/', 345);
        PF("%-8s%ld", "Score", 1234567L);
        PF("%1.2f%%", 99.5);
        PF("x\xc3\xa9%dy", 5);
        PF("%p", (void *)0x1234);
        PF("%p", (void *)0);
        PF("%8p|", (void *)0xABCDEF);
        PF("%-12p|", (void *)0xABCDEF);
        PF("%#p", (void *)0x10);
    }
}

CONF_TEST(ucfg_printf_float)
{
    static const double kVals[] =
    {
        0.0, -0.0, 1.0, -1.0, 0.5, 1.5, 2.5, -2.5, 0.125, 0.375, 1e-5, 1.25e-5, 123.456, 99.5, 99.995,
        1e15, 1e16, 1e17, 1e20, 1e21, 1e100, 1.0 / 3.0, 2.0 / 3.0, DBL_MIN,
        4.9406564584124654e-324, 0.1, 0.05, 0.015, 0.0049999, 1234567.891, 9.9999999, 0.9999995,
        3.14159265358979, 65536.0, -1234.5678
    };
    const int nvals = (int)(sizeof(kVals) / sizeof(kVals[0]));

    /* Not tested, because they crash the reference: any conversion of a
     * value beyond ~1e120 (1e300, DBL_MAX: an internal buffer overruns), of
     * infinities and NaNs, and conversions longer than ~250 characters. */
    for(int t = 0; t < kNumGameFmts; ++t)
    {
        FormatGuard g(kGameFmts[t].type);
        conf::Seq s(ctx, kGameFmts[t].name);
        Out o;

        for(int i = 0; i < nvals; ++i)
        {
            double v = kVals[i];

            {
                PF("%f", v);
                PF("%F", v);
                PF("%.0f", v);
                PF("%.1f", v);
                PF("%.2f", v);
                PF("%1.2f", v);
                PF("%.3f", v);
                PF("%.10f", v);
                PF("%.17f", v);
                PF("%10.3f|", v);
                PF("%-10.3f|", v);
                PF("%010.3f", v);
                PF("%+f", v);
                PF("% f", v);
                PF("%#.0f", v);
                PF("%Lf", (long double)v);
                PF("%lf", v);
                PF("%*.*f", 9, 2, v);
                PF("%f|%d", v, 12);
            }

            PF("%e", v);
            PF("%E", v);
            PF("%.0e", v);
            PF("%.3e", v);
            PF("%#.0e", v);
            PF("%12.4e|", v);
            PF("%g", v);
            PF("%G", v);
            PF("%.0g", v);
            PF("%.1g", v);
            PF("%.3g", v);
            PF("%.10g", v);
            PF("%.17g", v);
            PF("%#g", v);
            PF("%#.3g", v);
            PF("%-12g|", v);
            PF("%012g", v);
            PF("%+g", v);
            PF("%a", v);
            PF("%A", v);
            PF("%.2a", v);
            PF("%.*e", 2, v);
        }

        static const float kF[] = { 1.5f, 0.1f, 3.4028235e38f, 1e-45f };

        for(int i = 0; i < 4; ++i)
            PF("%f %g %e", (double)kF[i], (double)kF[i], (double)kF[i]);
    }
}

CONF_TEST(ucfg_printf_truncate)
{
    for(int t = 0; t < kNumGameFmts; ++t)
    {
        FormatGuard g(kGameFmts[t].type);
        conf::Seq s(ctx, kGameFmts[t].name);
        Out o;

        for(int size = 1; size <= 26; ++size)
        {
            o.reset();
            int r = uszprintf(o.buf(), size, "%s=%d!", "abcdef", 12345);
            s.add_str(o.describe_raw(r, size).c_str(), "plain size=%d", size);

            o.reset();
            r = uszprintf(o.buf(), size, "x\xc3\xa9y%c\xe2\x82\xac%s", 0x20AC, "\xc3\xa9\xc3\xa9");
            s.add_str(o.describe_raw(r, size).c_str(), "multibyte size=%d", size);

            o.reset();
            r = uszprintf(o.buf(), size, "%8.3f|%-6s|", 3.14159, "ab");
            s.add_str(o.describe_raw(r, size).c_str(), "padded size=%d", size);

            o.reset();
            r = vwrap(o.buf(), size, "%d-%d-%d", 111, 222, 333);
            s.add_str(o.describe_raw(r, size).c_str(), "uvszprintf size=%d", size);
        }

        o.reset();
        int r = usprintf(o.buf(), "%s %d %c %5.2f", "usprintf", -42, 'q', 2.345);
        s.add_str(o.describe(r).c_str(), "usprintf");

        o.reset();
        r = vwrap(o.buf(), 512, "%s|%3d|%-3d|%c", "va", 7, 8, '!');
        s.add_str(o.describe(r).c_str(), "uvszprintf");

        /* Long outputs (the game formats into 512-byte buffers). */
        char big[400];
        memset(big, 'k', sizeof(big) - 1);
        big[sizeof(big) - 1] = 0;
        o.reset();
        r = uszprintf(o.buf(), 512, "%s|%s", big, big);
        s.add_str(o.describe(r).c_str(), "long");
    }
}

/* Random numbers of every magnitude the game can print, and exact rounding
 * ties at every precision. */
CONF_TEST(ucfg_printf_random)
{
    FormatGuard g(U_ASCII);
    conf::Seq s(ctx, "values");
    Out o;

    for(int i = 0; i < 1500; ++i)
    {
        double v;
        unsigned long long bits = ((unsigned long long)ctx.rng.next() << 32) | ctx.rng.next();

        switch(ctx.rng.range(0, 3))
        {
        case 0:
            v = (double)ctx.rng.range(-100000, 100000) / (double)(1 << ctx.rng.range(0, 20));
            break;
        case 1:
            v = (double)(int)ctx.rng.next() * 1e-6;
            break;
        case 2:
            /* any finite double between 1e-300 and 1e100 */
            memcpy(&v, &bits, 8);

            while(!(fabs(v) < 1e100) || fabs(v) < 1e-300)
            {
                bits = (bits & 0x800FFFFFFFFFFFFFull) | ((unsigned long long)ctx.rng.range(0x01, 0x54B) << 52);
                memcpy(&v, &bits, 8);
            }

            break;
        default:
            v = ctx.rng.range(-1000, 1000) + ctx.rng.range(0, 999) / 1000.0;
            break;
        }

        int p = ctx.rng.range(0, 20);
        PF("%f|%e|%g|%.*f|%.*e|%.*g", v, v, v, p, v, p, v, p, v);
        PF("%#.*g|%+.3f|% 12.4e|%-14.6g|%012.2f", p, v, v, v, v, v);
    }

    conf::Seq t(ctx, "ties");

    for(int k = 1; k <= 12; ++k)
        for(int m = -41; m <= 41; m += 2)
        {
            double v = (double)m / (double)(1 << k) + (m % 7);
            o.reset();
            int r = uszprintf(o.buf(), 512, "%.*f|%.*e|%.*g", k - 1, v, k, v, k, v);
            t.add_str(o.describe(r).c_str(), "k=%d m=%d", k, m);
        }

    conf::Seq n(ctx, "ints");

    for(int i = 0; i < 800; ++i)
    {
        int v = (int)ctx.rng.next() >> ctx.rng.range(0, 31);
        int w = ctx.rng.range(0, 14);
        int p = ctx.rng.range(0, 12);
        o.reset();
        int r = uszprintf(o.buf(), 512, "%*d|%-*.*d|%0*x|%#*.*o|%*u|%+.*d|%c", w, v, w, p, v, w, v, w, p, v, w, v,
                          p, v, 'a' + (v & 15));
        n.add_str(o.describe(r).c_str(), "v=%d w=%d p=%d", v, w, p);
    }
}

/* What happens at the end of the buffer when a multibyte character does not
 * fit: literal text, %c, %s and padding, followed by more text. */
CONF_TEST(ucfg_printf_edge)
{
    FormatGuard g(U_UTF8);
    conf::Seq s(ctx, "utf8");
    Out o;

    for(int size = 1; size <= 9; ++size)
    {
#define PE(...)                                                              \
        do {                                                                 \
            o.reset();                                                       \
            int r_ = uszprintf(o.buf(), size, __VA_ARGS__);                  \
            s.add_str(o.describe_raw(r_, size).c_str(), "size=%d %s", size,  \
                      #__VA_ARGS__);                                         \
        } while(0)
        PE("%s", "\xc3\xa9");
        PE("a%s", "\xc3\xa9");
        PE("%sZ", "\xe2\x82\xac");
        PE("%s", "a\xe2\x82\xac" "b");
        PE("%s|%s", "\xe2\x82\xac", "z");
        PE("%c", 0xE9);
        PE("%cZ", 0x20AC);
        PE("%c%c", 0x20AC, 'q');
        PE("\xe2\x82\xac" "Z");
        PE("a\xe2\x82\xac" "Zw");
        PE("%3s", "\xc3\xa9");
        PE("%-4s|", "\xc3\xa9");
        PE("%d\xe2\x82\xac" "Z", 12);
        PE("%s%d", "\xe2\x82\xac", 345);
        PE("%5d", 7);
        PE("%-5d|", 7);
        PE("%s", "abcdefghij");
        PE("\xe2\x82\xac%s", "\xc3\xa9");
        PE("\xe2\x82\xac%d", 5);
        PE("a\xc3\xa9%s", "\xe2\x82\xac");
        PE("%s%s", "\xc3\xa9", "\xe2\x82\xac");
        PE("%s%c", "ab", 0x20AC);
        PE("%s%s", "\xe2\x82\xac", "a");
        PE("%s%c%s", "\xe2\x82\xac", 'b', "cd");
        PE("%s%s|", "\xc3\xa9", "\xc3\xa9");
        PE("%2s%s", "\xe2\x82\xac", "\xc3\xa9");
#undef PE
    }

    /* A %c that produces a zero character. */
    for(int t = 0; t < kNumGameFmts; ++t)
    {
        FormatGuard g2(kGameFmts[t].type);
        conf::Seq z(ctx, conf::nm("nul_%s", kGameFmts[t].name));
        const int size = 12;
#define PZ(...)                                                              \
        do {                                                                 \
            o.reset();                                                       \
            int r_ = uszprintf(o.buf(), size, __VA_ARGS__);                  \
            z.add_str(o.describe_raw(r_, size).c_str(), "%s", #__VA_ARGS__); \
        } while(0)
        PZ("[%-3c]", 0);
        PZ("[%3c]", 0);
        PZ("a%cb", 0);
        PZ("%c|%s", 0, "after");
        PZ("%c%c", 0, 'z');
        PZ("[%-3c]", 0x100);
        PZ("x%cy%cz", 0x200, 'q');
        PZ("%5c|", 0);
#undef PZ
    }
}

/* Malformed or rarely-used formats, last in the file. */
CONF_TEST(ucfg_printf_odd)
{
    for(int t = 0; t < kNumGameFmts; ++t)
    {
        FormatGuard g(kGameFmts[t].type);
        conf::Seq s(ctx, kGameFmts[t].name);
        Out o;

        /* (%s with NULL crashes the reference: not tested.) */
        PF("abc%y|%d", 5, 6);
        PF("abc%");
        PF("abc%5");
        PF("abc%-");
        PF("abc%.");
        PF("%5.y|%d", 1, 2);
        PF("%k%d", 3, 4);
        PF("%-%d", 3);
        PF("%  d|% +d|%+ d", 1, 2, 3);
        PF("%--5d|", 9);
        PF("%00005d|", 9);
        PF("%#d|%#s|%#c", 1, "s", 'c');
        PF("%1$d", 5);
        PF("%.d|%.s|%.f", 0, "abc", 2.5);
        PF("%llld", 5LL);
        PF("%lh%d", 1, 2);
        PF("%zd|%d", (size_t)5, 6);
        PF("%jd|%d", (long long)5, 6);
        PF("%td|%d", (ptrdiff_t)5, 6);
        PF("%w%d", 1, 2);
        PF("%C|%d", 'x', 3);
        /* (Conversions longer than the reference's internal buffer, e.g.
         * "%.400f", crash it: not tested.) */
        PF("%.30f", 2.5);
        PF("%30f|", 2.5);
        PF("%.30e", 1.0 / 3.0);

        PF("%.*f|%.*e|%.*g", -1, 2.5, -3, 2.5, -1, 2.5);
        PF("%.*s|%.*s|%*.*s|", 0, "abc", -1, "abc", 5, -1, "abc");
        PF("%+u|% u|%+x|% x|%+o|%+X", 5, 5, 255, 255, 8, 255);
        PF("%+c|% c|%+s|% s|%+p|% p", 'a', 'b', "s", "t", (void *)0x1F, (void *)0x1F);
        PF("%#.3o|%#.3x|%#5o|%#-6X|%#05o", 8, 255, 8, 255, 8);
        PF("%08.3x|%08.3u|%-08.3u|%+08.3d|% 08.3d", 255, 77, 77, 77, 77);
        PF("%0*d|%-0*d|%0-*d", 5, 3, 5, 3, 5, 3);
        PF("%.3c|%5.3c|%-5.1c|", 'x', 'y', 'z');
        PF("%+.0f|% .0f|%+e|% g|%+#g", 0.5, 1.5, -0.0, 0.25, 3.0);
        PF("%010.2e|%-+10.1e|%+010.3g|%#010.0f", 12.5, -0.05, 1e-7, 9.5);
        PF("%.0f|%.0f|%.0f|%.0f|%.0f|%.0f", 0.49999999999999994, 1.4999999999999998, 3.5, 4.5, -0.5, 1e15 + 0.5);
        PF("%.1f|%.1f|%.1f|%.1f|%.2f|%.3f", 0.25, 0.35, 0.45, 0.05, 1.005, 2.0005);
        PF("%g|%g|%g|%g|%g|%g", 100000.0, 1000000.0, 0.0001, 0.00001, 123456.5, 999999.5);
        PF("%.3g|%.3g|%.3g|%.2g|%.2g", 999.5, 9995.0, 0.0009995, 99.5, 0.995);
        PF("%e|%e|%.2e|%.0e", 9.9999995, 999.9995, 9.995, 9.5);
        PF("%5.1s|%-5.0s|%.0s%c", "\xc3\xa9\xc3\xa9", "zz", "q", 'r');
        PF("%30d|", 5);
        PF("%.30d|", 5);
        PF("%30s|", "x");
        PF("%30c|", 'x');

        int n1 = -1, n2 = -1;
        o.reset();
        int r = uszprintf(o.buf(), 512, "abc%n\xc3\xa9%n", &n1, &n2);
        s.add_str(conf::nm("%s n1=%d n2=%d", o.describe(r).c_str(), n1, n2), "%%n");
    }
}

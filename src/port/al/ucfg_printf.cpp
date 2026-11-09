// zcport ucfg module: uszprintf, usprintf and uvszprintf.
//
// Formats like the reference Allegro 4.4.2 build, as measured by
// tests/conformance/test_ucfg_printf.cpp:
//   - the format and %s strings are read, and the output written, character
//     by character in the current text format; widths and precisions count
//     characters; the return value is the number of characters of the
//     complete result, even when the buffer truncates it;
//   - literal text stops at the first character that does not fit (leaving
//     room for the terminator); a conversion is copied as far as it fits,
//     and up to a zero character in it (%c of 0), but the position moves
//     past all of it, and the final terminator goes at that position or at
//     the end of the buffer, whichever comes first;
//   - "%%" right after '%' is a '%', a '%' anywhere else inside a
//     conversion starts a new one, an unknown conversion character is
//     printed as itself (no argument used), an incomplete conversion at the
//     end is dropped;
//   - 'h' is ignored, "ll" and 'L' read 64-bit integers, '+' and ' ' apply
//     to unsigned conversions and %p too, '#' always adds the "0"/"0x"
//     prefix (also to zero), the '0' flag pads %d with a precision and also
//     %s and %c, ".0" on %d still prints "0", '.' without digits means no
//     precision, a negative '*' precision means 0;
//   - floating point: exact decimal expansion of the double, rounded half
//     away from zero; %#g of zero keeps P decimals.
// Written for zcport; no Allegro source was used.

#include "ucfg_internal.h"

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include <cmath>
#include <string>
#include <vector>

namespace
{

// ---- output ---------------------------------------------------------------------

struct Output
{
    char *buf;
    int size;
    int pos;        // where the next character goes (may run past the end)
    int chars;      // characters of the complete result
    bool stopped;   // a literal character did not fit: nothing more is written

    int limit() const { return size - ucwidth(0); }

    void literal(int c)
    {
        ++chars;

        if(stopped)
            return;

        int w = ucwidth(c);

        if(pos + w > limit())
        {
            stopped = true;
            return;
        }

        pos += usetc(buf + pos, c);
    }

    // A conversion is copied as far as it fits and up to a zero character
    // in it, terminated there, and the position then moves past all of it.
    void conversion(const std::vector<int> &cs)
    {
        chars += (int)cs.size();

        if(stopped)
            return;

        int total = 0;

        for(int c : cs)
            total += ucwidth(c);

        if(pos <= limit())
        {
            int p = pos;

            for(int c : cs)
            {
                if(!c)
                    break;

                int w = ucwidth(c);

                if(p + w > limit())
                    break;

                p += usetc(buf + p, c);
            }

            usetc(buf + p, 0);
        }

        pos += total;
    }

    void finish()
    {
        int at = pos < limit() ? pos : limit();

        if(at >= 0)
            usetc(buf + at, 0);
    }
};

// ---- exact decimal expansion of a double ------------------------------------------

// Big unsigned integer, base 1e9, least significant limb first.
struct Big
{
    std::vector<uint32_t> limb;

    explicit Big(uint64_t v)
    {
        do
        {
            limb.push_back((uint32_t)(v % 1000000000u));
            v /= 1000000000u;
        }
        while(v);
    }

    void mul(uint32_t f)
    {
        uint64_t carry = 0;

        for(uint32_t &x : limb)
        {
            uint64_t t = (uint64_t)x * f + carry;
            x = (uint32_t)(t % 1000000000u);
            carry = t / 1000000000u;
        }

        while(carry)
        {
            limb.push_back((uint32_t)(carry % 1000000000u));
            carry /= 1000000000u;
        }
    }

    std::string str() const
    {
        std::string s = std::to_string(limb.back());

        for(size_t i = limb.size() - 1; i-- > 0;)
        {
            std::string part = std::to_string(limb[i]);
            s += std::string(9 - part.size(), '0') + part;
        }

        return s;
    }
};

// |v| = 0.D x 10^E with D the significant digits (no leading or trailing
// zeros); D is empty for zero.
struct Decimal
{
    std::string d;
    int e;
};

Decimal exact_decimal(double v)
{
    Decimal r;
    r.e = 0;
    v = std::fabs(v);

    if(v == 0)
        return r;

    int exp2;
    double frac = std::frexp(v, &exp2);              // v = frac * 2^exp2, 0.5 <= frac < 1
    uint64_t m = (uint64_t)std::ldexp(frac, 53);     // exact: 53-bit mantissa
    int e = exp2 - 53;                               // v = m * 2^e

    while(m && !(m & 1))
    {
        m >>= 1;
        ++e;
    }

    Big n(m);
    int point;                                       // digits after the decimal point

    if(e >= 0)
    {
        for(int i = 0; i < e; ++i)
            n.mul(2);

        point = 0;
    }
    else
    {
        for(int i = 0; i < -e; ++i)
            n.mul(5);

        point = -e;
    }

    std::string s = n.str();
    r.e = (int)s.size() - point;

    size_t last = s.find_last_not_of('0');
    r.d = s.substr(0, last + 1);
    return r;
}

// Keeps the first `count` digits of x, rounding half away from zero (the
// digits are exact, so the first dropped digit decides).
void round_digits(Decimal &x, int count)
{
    if(count >= (int)x.d.size())
        return;

    if(count < 0)
    {
        x.d.clear();
        return;
    }

    bool up = x.d[(size_t)count] >= '5';
    x.d.resize((size_t)count);

    if(up)
    {
        int i = count - 1;

        while(i >= 0 && x.d[(size_t)i] == '9')
        {
            x.d[(size_t)i] = '0';
            --i;
        }

        if(i < 0)
        {
            x.d.insert(x.d.begin(), '1');
            ++x.e;
        }
        else
            ++x.d[(size_t)i];
    }

    size_t last = x.d.find_last_not_of('0');
    x.d = last == std::string::npos ? std::string() : x.d.substr(0, last + 1);
}

// Digit of weight 10^(e - 1 - i) as a character.
char digit_at(const Decimal &x, int i)
{
    if(i < 0 || i >= (int)x.d.size())
        return '0';

    return x.d[(size_t)i];
}

std::string fixed_body(double v, int prec, bool alt)
{
    Decimal x = exact_decimal(v);
    round_digits(x, x.e + prec);
    std::string s;

    if(x.d.empty())
        x.e = 0;

    if(x.e > 0)
        for(int i = 0; i < x.e; ++i)
            s += digit_at(x, i);
    else
        s += '0';

    if(prec > 0 || alt)
        s += '.';

    for(int i = 0; i < prec; ++i)
        s += digit_at(x, x.e + i);

    return s;
}

std::string exponent_text(int ex, bool upper)
{
    std::string s(1, upper ? 'E' : 'e');
    s += ex < 0 ? '-' : '+';
    std::string digits = std::to_string(ex < 0 ? -ex : ex);

    if(digits.size() < 2)
        digits = "0" + digits;

    return s + digits;
}

std::string exp_body(double v, int prec, bool alt, bool upper)
{
    Decimal x = exact_decimal(v);
    int ex = 0;

    if(!x.d.empty())
    {
        round_digits(x, prec + 1);
        ex = x.e - 1;
    }

    std::string s(1, x.d.empty() ? '0' : x.d[0]);

    if(prec > 0 || alt)
        s += '.';

    for(int i = 1; i <= prec; ++i)
        s += digit_at(x, i);

    return s + exponent_text(ex, upper);
}

std::string general_body(double v, int prec, bool alt, bool upper)
{
    int p = prec < 0 ? 6 : (prec == 0 ? 1 : prec);
    std::string s;

    if(v == 0)
        s = fixed_body(v, p, alt);       // the reference keeps P decimals here
    else
    {
        Decimal x = exact_decimal(v);
        round_digits(x, p);
        int ex = x.e - 1;

        if(ex < p && ex >= -4)
            s = fixed_body(v, p - 1 - ex, alt);
        else
            s = exp_body(v, p - 1, alt, upper);
    }

    if(!alt)
    {
        size_t epos = s.find_first_of("eE");
        std::string mant = epos == std::string::npos ? s : s.substr(0, epos);
        std::string tail = epos == std::string::npos ? std::string() : s.substr(epos);

        if(mant.find('.') != std::string::npos)
        {
            size_t last = mant.find_last_not_of('0');
            mant.resize(last + 1);

            if(!mant.empty() && mant.back() == '.')
                mant.pop_back();
        }

        s = mant + tail;
    }

    return s;
}

// ---- integer digits ------------------------------------------------------------------

std::string unsigned_digits(unsigned long long v, int base, bool upper)
{
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string s;

    do
    {
        s.insert(s.begin(), set[v % (unsigned)base]);
        v /= (unsigned)base;
    }
    while(v);

    return s;
}

// ---- one conversion ------------------------------------------------------------------

struct Spec
{
    bool left = false;
    bool plus = false;
    bool space = false;
    bool alt = false;
    bool zero = false;
    int width = 0;
    int prec = -1;      // -1: none
    int longs = 0;      // number of 'l'
    bool big_l = false; // 'L'
};

// Builds sign + prefix + zero padding + body, padded to the width.
void emit_number(Output &out, const Spec &sp, const std::string &sign_prefix, const std::string &body)
{
    std::vector<int> cs;
    int len = (int)(sign_prefix.size() + body.size());
    int pad = sp.width > len ? sp.width - len : 0;

    if(!sp.left && !sp.zero)
        cs.insert(cs.end(), (size_t)pad, ' ');

    for(char ch : sign_prefix)
        cs.push_back((unsigned char)ch);

    if(!sp.left && sp.zero)
        cs.insert(cs.end(), (size_t)pad, '0');

    for(char ch : body)
        cs.push_back((unsigned char)ch);

    if(sp.left)
        cs.insert(cs.end(), (size_t)pad, ' ');

    out.conversion(cs);
}

// Pads a sequence of characters (code points) to the width.
void emit_chars(Output &out, const Spec &sp, const std::vector<int> &chars)
{
    std::vector<int> cs;
    int len = (int)chars.size();
    int pad = sp.width > len ? sp.width - len : 0;

    if(!sp.left)
        cs.insert(cs.end(), (size_t)pad, sp.zero ? '0' : ' ');

    cs.insert(cs.end(), chars.begin(), chars.end());

    if(sp.left)
        cs.insert(cs.end(), (size_t)pad, ' ');

    out.conversion(cs);
}

std::string sign_text(const Spec &sp, bool neg)
{
    if(neg)
        return "-";

    if(sp.plus)
        return "+";

    if(sp.space)
        return " ";

    return std::string();
}

int format_into(Output &out, const char *format, va_list args)
{
    const char *f = format;

    for(;;)
    {
        int c = ugetxc(&f);

        if(!c)
            break;

        if(c != '%')
        {
            out.literal(c);
            continue;
        }

        c = ugetxc(&f);

        if(c == '%')
        {
            out.literal('%');
            continue;
        }

    restart:
        Spec sp;

        for(;; c = ugetxc(&f))
        {
            if(c == '-')
                sp.left = true;
            else if(c == '+')
                sp.plus = true;
            else if(c == ' ')
                sp.space = true;
            else if(c == '#')
                sp.alt = true;
            else if(c == '0')
                sp.zero = true;
            else
                break;
        }

        if(c == '*')
        {
            sp.width = va_arg(args, int);

            if(sp.width < 0)
            {
                sp.left = true;
                sp.width = -sp.width;
            }

            c = ugetxc(&f);
        }
        else
            while(c >= '0' && c <= '9')
            {
                sp.width = sp.width * 10 + (c - '0');
                c = ugetxc(&f);
            }

        if(c == '.')
        {
            c = ugetxc(&f);

            if(c == '*')
            {
                sp.prec = va_arg(args, int);

                if(sp.prec < 0)
                    sp.prec = 0;

                c = ugetxc(&f);
            }
            else if(c >= '0' && c <= '9')
            {
                sp.prec = 0;

                while(c >= '0' && c <= '9')
                {
                    sp.prec = sp.prec * 10 + (c - '0');
                    c = ugetxc(&f);
                }
            }
        }

        for(;; c = ugetxc(&f))
        {
            if(c == 'l')
                ++sp.longs;
            else if(c == 'L')
                sp.big_l = true;
            else if(c != 'h')
                break;
        }

        if(!c)
            break;                          // incomplete conversion at the end

        if(c == '%')
        {
            c = ugetxc(&f);
            goto restart;
        }

        bool wide = sp.longs >= 2 || sp.big_l;

        switch(c)
        {
        case 'd':
        case 'i':
        {
            long long v;

            if(wide)
                v = va_arg(args, long long);
            else if(sp.longs == 1)
                v = va_arg(args, long);
            else
                v = va_arg(args, int);

            unsigned long long mag = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
            std::string digits = unsigned_digits(mag, 10, false);

            if(sp.prec > (int)digits.size())
                digits.insert(0, (size_t)(sp.prec - (int)digits.size()), '0');

            emit_number(out, sp, sign_text(sp, v < 0), digits);
            break;
        }

        case 'u':
        case 'o':
        case 'x':
        case 'X':
        case 'p':
        {
            unsigned long long v;

            if(c == 'p')
                v = (unsigned long long)(uintptr_t)va_arg(args, void *);
            else if(wide)
                v = va_arg(args, unsigned long long);
            else if(sp.longs == 1)
                v = va_arg(args, unsigned long);
            else
                v = va_arg(args, unsigned int);

            int base = c == 'u' ? 10 : (c == 'o' ? 8 : 16);
            std::string digits = unsigned_digits(v, base, c == 'X');

            if(sp.prec > (int)digits.size())
                digits.insert(0, (size_t)(sp.prec - (int)digits.size()), '0');

            std::string prefix = sign_text(sp, false);

            if(sp.alt)
            {
                if(c == 'o')
                    prefix += "0";
                else if(c != 'u')
                    prefix += "0x";
            }

            emit_number(out, sp, prefix, digits);
            break;
        }

        case 'c':
        {
            // The character goes through the current encoding once (so a
            // value it cannot hold comes out the way ugetx reads it back).
            char tmp[16];
            memset(tmp, 0, sizeof(tmp));
            usetc(tmp, va_arg(args, int));
            char *t = tmp;
            std::vector<int> chars(1, ugetx(&t));
            emit_chars(out, sp, chars);
            break;
        }

        case 's':
        {
            const char *s = va_arg(args, const char *);
            std::vector<int> chars;

            if(!s)
                s = "(null)";    // the reference crashes

            int d;

            while((sp.prec < 0 || (int)chars.size() < sp.prec) && (d = ugetxc(&s)) != 0)
                chars.push_back(d);

            emit_chars(out, sp, chars);
            break;
        }

        case 'e':
        case 'E':
        case 'f':
        case 'g':
        case 'G':
        {
            double v = va_arg(args, double);
            bool neg = std::signbit(v);
            std::string body;

            if(std::isnan(v))
                body = "nan";       // the reference crashes
            else if(std::isinf(v))
                body = "inf";       // the reference crashes
            else if(c == 'f')
                body = fixed_body(v, sp.prec < 0 ? 6 : sp.prec, sp.alt);
            else if(c == 'e' || c == 'E')
                body = exp_body(v, sp.prec < 0 ? 6 : sp.prec, sp.alt, c == 'E');
            else
                body = general_body(v, sp.prec, sp.alt, c == 'G');

            emit_number(out, sp, sign_text(sp, neg), body);
            break;
        }

        case 'n':
            *va_arg(args, int *) = out.chars;
            break;

        default:
            out.literal(c);
            break;
        }
    }

    out.finish();
    return out.chars;
}

} // namespace

extern "C" int uvszprintf(char *buf, int size, const char *format, va_list args)
{
    Output out = { buf, size, 0, 0, false };
    va_list ap;
    va_copy(ap, args);
    int r = format_into(out, format, ap);
    va_end(ap);
    return r;
}

extern "C" int uszprintf(char *buf, int size, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int r = uvszprintf(buf, size, format, ap);
    va_end(ap);
    return r;
}

extern "C" int usprintf(char *buf, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int r = uvszprintf(buf, INT_MAX, format, ap);
    va_end(ap);
    return r;
}

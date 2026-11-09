// zcport ucfg module: the unicode.h text formats and string routines.
//
// Behaviour follows the reference Allegro 4.4.2 build as measured by
// tests/conformance/test_ucfg_unicode.cpp, including what it does with
// malformed UTF-8 (where a routine steps by the lead byte, uwidth(), and
// where by the decoded sequence, ugetx()). Written for zcport; no Allegro
// source was used.

#include "ucfg_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <string>
#include <vector>

using zcport_ucfg::TextFormat;

namespace
{

// ---- U_ASCII ------------------------------------------------------------------

int ascii_getc(const char *s) { return (unsigned char)*s; }
int ascii_getx(char **s) { return (unsigned char)*((*s)++); }
int ascii_setc(char *s, int c) { *s = (char)c; return 1; }
int ascii_width(const char *) { return 1; }
int ascii_cwidth(int) { return 1; }
int ascii_isok(int c) { return (c >= 0 && c <= 255) ? 1 : 0; }

// ---- U_ASCII_CP ---------------------------------------------------------------

const unsigned short *g_cp_table = nullptr;     // nullptr: the default table
const unsigned short *g_cp_extras = zcport_ucfg::default_codepage_extras;

int cp_table_value(int b)
{
    if(g_cp_table)
        return g_cp_table[b];

    return b < 128 ? b : '^';
}

int cp_getc(const char *s) { return zcport_ucfg::codepage_decode((unsigned char)*s); }
int cp_getx(char **s) { return zcport_ucfg::codepage_decode((unsigned char)*((*s)++)); }

int cp_setc(char *s, int c)
{
    int b = zcport_ucfg::codepage_encode(c);
    *s = (char)(b < 0 ? '^' : b);
    return 1;
}

int cp_isok(int c) { return zcport_ucfg::codepage_encode(c) >= 0 ? TRUE : FALSE; }

// ---- U_UNICODE (16-bit, native byte order) --------------------------------------

int uni_getc(const char *s)
{
    unsigned short v;
    memcpy(&v, s, 2);
    return v;
}

int uni_getx(char **s)
{
    int c = uni_getc(*s);
    *s += 2;
    return c;
}

int uni_setc(char *s, int c)
{
    unsigned short v = (unsigned short)c;
    memcpy(s, &v, 2);
    return 2;
}

int uni_width(const char *) { return 2; }
int uni_cwidth(int) { return 2; }
int uni_isok(int c) { return (c >= 0 && c <= 0xFFFF) ? 1 : 0; }

// ---- U_UTF8 -------------------------------------------------------------------

int leading_ones(unsigned char b)
{
    int n = 0;

    while(n < 8 && (b & (0x80 >> n)))
        ++n;

    return n;
}

// Decodes one character; *len receives the bytes it occupies. A lead byte
// with n leading ones takes n - 1 continuation bytes (a bare continuation
// byte is a character of its own); a missing continuation byte gives '^'
// and stops before it.
int utf8_decode(const unsigned char *s, int *len)
{
    unsigned int b = s[0];

    if(b < 0x80)
    {
        *len = 1;
        return (int)b;
    }

    int n = leading_ones((unsigned char)b);
    unsigned int c = n >= 7 ? 0 : (b & ((1u << (7 - n)) - 1));

    for(int i = 1; i < n; ++i)
    {
        unsigned int t = s[i];

        if((t & 0xC0) != 0x80)
        {
            *len = i;
            return '^';
        }

        c = (c << 6) | (t & 0x3F);
    }

    *len = n < 1 ? 1 : n;
    return (int)c;
}

int utf8_getc(const char *s)
{
    int len;
    return utf8_decode((const unsigned char *)s, &len);
}

int utf8_getx(char **s)
{
    int len;
    int c = utf8_decode((const unsigned char *)*s, &len);
    *s += len;
    return c;
}

int utf8_cwidth(int c)
{
    if(c < 0x80)
        return 1;   // includes negative values (stored as one byte)

    if(c < 0x800)
        return 2;

    if(c < 0x10000)
        return 3;

    if(c < 0x200000)
        return 4;

    if(c < 0x4000000)
        return 5;

    // The reference never returns for c >= 0x40000000.
    return 6;
}

int utf8_setc(char *s, int c)
{
    int n = utf8_cwidth(c);

    if(n == 1)
    {
        *s = (char)c;
        return 1;
    }

    unsigned int v = (unsigned int)c;

    for(int i = n - 1; i > 0; --i)
    {
        s[i] = (char)(0x80 | (v & 0x3F));
        v >>= 6;
    }

    s[0] = (char)(((0xFF00u >> n) & 0xFF) | v);
    return n;
}

int utf8_width(const char *s)
{
    int n = leading_ones((unsigned char)*s);
    return n >= 2 ? n : 1;
}

int utf8_isok(int) { return TRUE; }

// ---- the format list ----------------------------------------------------------

std::vector<TextFormat> &formats()
{
    static std::vector<TextFormat> list =
    {
        { U_ASCII, ascii_getc, ascii_getx, ascii_setc, ascii_width, ascii_cwidth, ascii_isok, 1 },
        { U_ASCII_CP, cp_getc, cp_getx, cp_setc, ascii_width, ascii_cwidth, cp_isok, 1 },
        { U_UNICODE, uni_getc, uni_getx, uni_setc, uni_width, uni_cwidth, uni_isok, 2 },
        { U_UTF8, utf8_getc, utf8_getx, utf8_setc, utf8_width, utf8_cwidth, utf8_isok, 4 },
    };
    return list;
}

int g_current = U_UTF8;

int getxc_adapter(const char **s) { return ugetx((char **)s); }

} // namespace

// ---- internal API -------------------------------------------------------------

namespace zcport_ucfg
{

const TextFormat *find_format(int id)
{
    if(id == U_CURRENT)
        id = g_current;

    for(const TextFormat &f : formats())
        if(f.id == id)
            return &f;

    return nullptr;
}

int codepage_decode(unsigned char b)
{
    return cp_table_value(b);
}

int codepage_encode(int c)
{
    for(int i = 0; i < 256; ++i)
        if(cp_table_value(i) == c)
            return i;

    if(g_cp_extras)
        for(const unsigned short *p = g_cp_extras; p[0]; p += 2)
            if(p[0] == c)
                return p[1];

    return -1;
}

} // namespace zcport_ucfg

// ---- format selection -----------------------------------------------------------

extern "C"
{

int (*ugetc)(const char *s) = utf8_getc;
int (*ugetx)(char **s) = utf8_getx;
int (*ugetxc)(const char **s) = getxc_adapter;
int (*usetc)(char *s, int c) = utf8_setc;
int (*uwidth)(const char *s) = utf8_width;
int (*ucwidth)(int c) = utf8_cwidth;
int (*uisok)(int c) = utf8_isok;

char empty_string[] = { 0, 0, 0, 0 };

void set_uformat(int type)
{
    const TextFormat *f = zcport_ucfg::find_format(type);

    if(!f)
        return;

    g_current = f->id;
    ugetc = f->getc_fn;
    ugetx = f->getx_fn;
    ugetxc = getxc_adapter;
    usetc = f->setc_fn;
    uwidth = f->width_fn;
    ucwidth = f->cwidth_fn;
    uisok = f->isok_fn;
}

int get_uformat(void)
{
    return g_current;
}

void register_uformat(int type, int (*u_getc)(const char *), int (*u_getx)(char **),
                      int (*u_setc)(char *, int), int (*u_width)(const char *), int (*u_cwidth)(int),
                      int (*u_isok)(int), int u_width_max)
{
    TextFormat f = { type, u_getc, u_getx, u_setc, u_width, u_cwidth, u_isok, u_width_max };

    for(TextFormat &e : formats())
        if(e.id == type)
        {
            e = f;

            if(g_current == type)
                set_uformat(type);

            return;
        }

    formats().push_back(f);
}

void set_ucodepage(const unsigned short *table, const unsigned short *extras)
{
    g_cp_table = table;
    g_cp_extras = extras;
}

int uwidth_max(int type)
{
    const TextFormat *f = zcport_ucfg::find_format(type);
    return f ? f->width_max : 0;
}

// ---- conversion -----------------------------------------------------------------

int need_uconvert(const char *s, int type, int newtype)
{
    if(type == U_CURRENT)
        type = g_current;

    if(newtype == U_CURRENT)
        newtype = g_current;

    if(type == newtype)
        return FALSE;

    bool ascii_like = (type == U_ASCII || type == U_UTF8) && (newtype == U_ASCII || newtype == U_UTF8);

    if(!ascii_like)
        return TRUE;

    for(const unsigned char *p = (const unsigned char *)s; *p; ++p)
        if(*p & 0x80)
            return TRUE;

    return FALSE;
}

int uconvert_size(const char *s, int type, int newtype)
{
    const TextFormat *in = zcport_ucfg::find_format(type);
    const TextFormat *out = zcport_ucfg::find_format(newtype);

    if(!in || !out)
        return 0;

    char *p = (char *)s;
    int size = 0;
    int c;

    while((c = in->getx_fn(&p)) != 0)
        size += out->cwidth_fn(c);

    return size + out->cwidth_fn(0);
}

void do_uconvert(const char *s, int type, char *buf, int newtype, int size)
{
    const TextFormat *in = zcport_ucfg::find_format(type);
    const TextFormat *out = zcport_ucfg::find_format(newtype);

    if(!in || !out)
        return;

    char *p = (char *)s;
    int limit = size - out->cwidth_fn(0);
    int pos = 0;
    int c;

    while((c = in->getx_fn(&p)) != 0)
    {
        // A character the target cannot hold becomes '^'.
        if(!out->isok_fn(c))
            c = '^';

        int w = out->cwidth_fn(c);

        if(pos + w > limit)
            break;

        pos += out->setc_fn(buf + pos, c);
    }

    out->setc_fn(buf + pos, 0);
}

char *uconvert(const char *s, int type, char *buf, int newtype, int size)
{
    static char static_buf[1024];

    if(!need_uconvert(s, type, newtype))
        return (char *)s;

    if(!buf)
    {
        buf = static_buf;
        size = (int)sizeof(static_buf);
    }

    do_uconvert(s, type, buf, newtype, size);
    return buf;
}

// ---- index helpers ----------------------------------------------------------------

int ustrlen(const char *s)
{
    int n = 0;
    char *p = (char *)s;

    while(ugetx(&p))
        ++n;

    return n;
}

int uoffset(const char *s, int idx)
{
    const char *orig = s;

    if(idx < 0)
        idx += ustrlen(s);

    char *p = (char *)s;

    while(idx-- > 0)
    {
        char *before = p;

        if(!ugetx(&p))
        {
            p = before;
            break;
        }
    }

    return (int)(p - orig);
}

int ugetat(const char *s, int idx)
{
    return ugetc(s + uoffset(s, idx));
}

int ustrsize(const char *s)
{
    char *p = (char *)s;
    char *last = p;

    while(ugetx(&p))
        last = p;

    return (int)(last - s);
}

int ustrsizez(const char *s)
{
    char *p = (char *)s;

    while(ugetx(&p))
        ;

    return (int)(p - s);
}

int usetat(char *s, int idx, int c)
{
    s += uoffset(s, idx);
    int oldw = uwidth(s);
    int neww = ucwidth(c);

    if(oldw != neww)
        memmove(s + neww, s + oldw, ustrsizez(s + oldw));

    usetc(s, c);
    return neww - oldw;
}

int uinsert(char *s, int idx, int c)
{
    s += uoffset(s, idx);
    int w = ucwidth(c);
    memmove(s + w, s, ustrsizez(s));
    usetc(s, c);
    return w;
}

int uremove(char *s, int idx)
{
    s += uoffset(s, idx);
    int w = uwidth(s);
    memmove(s, s + w, ustrsizez(s + w));
    return -w;
}

// ---- comparison ---------------------------------------------------------------------

int ustrcmp(const char *s1, const char *s2)
{
    for(;;)
    {
        int c1 = ugetxc(&s1);
        int c2 = ugetxc(&s2);

        if(c1 != c2)
            return c1 - c2;

        if(!c1)
            return 0;
    }
}

int ustricmp(const char *s1, const char *s2)
{
    for(;;)
    {
        int c1 = utolower(ugetxc(&s1));
        int c2 = utolower(ugetxc(&s2));

        if(c1 != c2)
            return c1 - c2;

        if(!c1)
            return 0;
    }
}

int ustrncmp(const char *s1, const char *s2, int n)
{
    while(n-- > 0)
    {
        int c1 = ugetxc(&s1);
        int c2 = ugetxc(&s2);

        if(c1 != c2)
            return c1 - c2;

        if(!c1)
            return 0;
    }

    return 0;
}

int ustrnicmp(const char *s1, const char *s2, int n)
{
    while(n-- > 0)
    {
        int c1 = utolower(ugetxc(&s1));
        int c2 = utolower(ugetxc(&s2));

        if(c1 != c2)
            return c1 - c2;

        if(!c1)
            return 0;
    }

    return 0;
}

// ---- searching (these step by uwidth(), except ustrrchr) ---------------------------

char *ustrchr(const char *s, int c)
{
    for(;;)
    {
        int d = ugetc(s);

        if(d == c)
            return (char *)s;

        if(!d)
            return nullptr;

        s += uwidth(s);
    }
}

char *ustrrchr(const char *s, int c)
{
    char *p = (char *)s;
    char *found = nullptr;

    for(;;)
    {
        char *before = p;
        int d = ugetx(&p);

        if(!d)
            return found;

        if(d == c)
            found = before;
    }
}

char *ustrstr(const char *s1, const char *s2)
{
    int len = ustrlen(s2);

    while(ugetc(s1))
    {
        if(ustrncmp(s1, s2, len) == 0)
            return (char *)s1;

        s1 += uwidth(s1);
    }

    return nullptr;
}

char *ustrpbrk(const char *s, const char *set)
{
    int c;

    while((c = ugetc(s)) != 0)
    {
        const char *t = set;
        int d;

        while((d = ugetxc(&t)) != 0)
            if(d == c)
                return (char *)s;

        s += uwidth(s);
    }

    return nullptr;
}

} // extern "C"

namespace
{

bool in_set(int c, const char *set)
{
    int d;

    while((d = ugetxc(&set)) != 0)
        if(d == c)
            return true;

    return false;
}

} // namespace

extern "C"
{

char *ustrtok_r(char *s, const char *set, char **last)
{
    if(!s)
    {
        s = *last;

        if(!s)
            return nullptr;
    }

    // Skip leading delimiters.
    char *tok;

    for(;;)
    {
        char *before = s;
        int c = ugetx(&s);

        if(!c)
        {
            *last = nullptr;
            return nullptr;
        }

        if(!in_set(c, set))
        {
            tok = before;
            break;
        }
    }

    // Scan to the next delimiter and replace it with a terminator.
    for(;;)
    {
        char *before = s;
        int c = ugetx(&s);

        if(!c)
        {
            *last = nullptr;
            return tok;
        }

        if(in_set(c, set))
        {
            s += usetat(before, 0, 0);
            *last = s;
            return tok;
        }
    }
}

char *ustrtok(char *s, const char *set)
{
    static char *last = nullptr;
    return ustrtok_r(s, set, &last);
}

// ---- copying ---------------------------------------------------------------------------

char *ustrzcpy(char *dest, int size, const char *src)
{
    int limit = size - ucwidth(0);
    int pos = 0;
    int c;

    while((c = ugetxc(&src)) != 0)
    {
        int w = ucwidth(c);

        if(pos + w > limit)
            break;

        pos += usetc(dest + pos, c);
    }

    usetc(dest + pos, 0);
    return dest;
}

char *ustrzcat(char *dest, int size, const char *src)
{
    int pos = ustrsize(dest);
    int limit = size - ucwidth(0);
    int c;

    while((c = ugetxc(&src)) != 0)
    {
        int w = ucwidth(c);

        if(pos + w > limit)
            break;

        pos += usetc(dest + pos, c);
    }

    usetc(dest + pos, 0);
    return dest;
}

char *ustrzncpy(char *dest, int size, const char *src, int n)
{
    int limit = size - ucwidth(0);
    int pos = 0;

    // Copies n characters, padding with zero characters once the source
    // ends (never reading past its terminator).
    while(n-- > 0)
    {
        int c = ugetc(src);

        if(c)
            ugetxc(&src);

        int w = ucwidth(c);

        if(pos + w > limit)
            break;

        pos += usetc(dest + pos, c);
    }

    usetc(dest + pos, 0);
    return dest;
}

char *ustrzncat(char *dest, int size, const char *src, int n)
{
    int pos = ustrsize(dest);
    int limit = size - ucwidth(0);
    int c;

    while(n-- > 0 && (c = ugetxc(&src)) != 0)
    {
        int w = ucwidth(c);

        if(pos + w > limit)
            break;

        pos += usetc(dest + pos, c);
    }

    usetc(dest + pos, 0);
    return dest;
}

char *_ustrdup(const char *src, void *(*malloc_func)(size_t))
{
    int size = ustrsizez(src);
    char *p = (char *)malloc_func((size_t)size);

    if(p)
        ustrzcpy(p, size, src);

    return p;
}

char *ustrlwr(char *s)
{
    char *p = s;
    int c;

    while((c = ugetc(p)) != 0)
    {
        int l = utolower(c);

        if(l != c)
            usetat(p, 0, l);

        p += uwidth(p);
    }

    return s;
}

char *ustrupr(char *s)
{
    char *p = s;
    int c;

    while((c = ugetc(p)) != 0)
    {
        int u = utoupper(c);

        if(u != c)
            usetat(p, 0, u);

        p += uwidth(p);
    }

    return s;
}

// ---- numbers ---------------------------------------------------------------------------

long ustrtol(const char *s, char **endp, int base)
{
    // C-locale strtol on the decoded characters, matching the reference
    // C runtime: overflow clamps (errno untouched), "0x" without a hex
    // digit after it is no number at all.
    char *p = (char *)s;
    char *before = p;
    int c = ugetx(&p);

    while(c == ' ' || (c >= 9 && c <= 13))
    {
        before = p;
        c = ugetx(&p);
    }

    bool neg = false;

    if(c == '-' || c == '+')
    {
        neg = c == '-';
        before = p;
        c = ugetx(&p);
    }

    auto digit_value = [](int ch) -> int
    {
        if(ch >= '0' && ch <= '9')
            return ch - '0';

        if(ch >= 'a' && ch <= 'z')
            return ch - 'a' + 10;

        if(ch >= 'A' && ch <= 'Z')
            return ch - 'A' + 10;

        return 99;
    };

    if(base == 0 || base == 16)
    {
        char *q = p;
        int x = ugetc(q);

        if(c == '0' && (x == 'x' || x == 'X'))
        {
            ugetx(&q);
            char *r = q;
            int h = ugetx(&r);

            if(digit_value(h) >= 16)
            {
                if(endp)
                    *endp = (char *)s;

                return 0;
            }

            base = 16;
            before = q;
            p = r;
            c = h;
        }
        else if(base == 0)
            base = c == '0' ? 8 : 10;
    }

    if(base < 2 || base > 36)
    {
        if(endp)
            *endp = (char *)s;

        return 0;
    }

    unsigned long long value = 0;
    bool any = false;
    bool overflow = false;
    const unsigned long long limit = neg ? 2147483648ull : 2147483647ull;

    for(;;)
    {
        int d = digit_value(c);

        if(d >= base)
            break;

        any = true;

        if(!overflow)
        {
            value = value * (unsigned)base + (unsigned)d;

            if(value > limit)
                overflow = true;
        }

        before = p;
        c = ugetx(&p);
    }

    if(!any)
    {
        if(endp)
            *endp = (char *)s;

        return 0;
    }

    if(endp)
        *endp = before;

    // 32-bit limits on every platform (the game's numbers are ints).
    if(overflow)
        return neg ? (-2147483647L - 1) : 2147483647L;

    return neg ? (long)(0 - value) : (long)value;
}

double ustrtod(const char *s, char **endp)
{
    // Decode into ASCII (anything else becomes a byte no number contains),
    // remembering where every character starts.
    std::string text;
    std::vector<char *> starts;
    char *p = (char *)s;

    for(;;)
    {
        starts.push_back(p);
        int c = ugetx(&p);

        if(!c)
            break;

        text += (c > 0 && c < 128) ? (char)c : '\x01';
    }

    const char *t = text.c_str();
    size_t i = 0;

    while(t[i] == ' ' || (t[i] >= 9 && t[i] <= 13))
        ++i;

    size_t sign_at = i;
    bool neg = false;

    if(t[i] == '-' || t[i] == '+')
    {
        neg = t[i] == '-';
        ++i;
    }

    auto ieq = [&](size_t at, const char *word) -> bool
    {
        for(size_t k = 0; word[k]; ++k)
        {
            char ch = t[at + k];

            if(ch >= 'A' && ch <= 'Z')
                ch = (char)(ch - 'A' + 'a');

            if(ch != word[k])
                return false;
        }

        return true;
    };

    double value;
    size_t end;

    if(ieq(i, "inf"))
    {
        end = i + (ieq(i, "infinity") ? 8 : 3);
        // (built from bits: HUGE_VAL is folded wrongly under /fp:fast)
        unsigned long long bits = neg ? 0xFFF0000000000000ull : 0x7FF0000000000000ull;
        memcpy(&value, &bits, sizeof(value));
    }
    else if(ieq(i, "nan"))
    {
        // The reference C runtime's NaNs: all mantissa bits set (sign from
        // the input), "nan(ind)" the negative default NaN.
        end = i + 3;
        unsigned long long bits = 0x7FFFFFFFFFFFFFFFull;

        if(t[end] == '(')
        {
            size_t k = end + 1;

            while((t[k] >= '0' && t[k] <= '9') || (t[k] >= 'a' && t[k] <= 'z') ||
                  (t[k] >= 'A' && t[k] <= 'Z') || t[k] == '_')
                ++k;

            if(t[k] == ')')
            {
                if(k - end - 1 == 3 && ieq(end + 1, "ind"))
                    bits = 0xFFF8000000000000ull;

                end = k + 1;
            }
        }

        if(neg && bits == 0x7FFFFFFFFFFFFFFFull)
            bits |= 0x8000000000000000ull;

        memcpy(&value, &bits, sizeof(value));
    }
    else
    {
        int saved = errno;
        char *e = nullptr;
        value = strtod(t + sign_at, &e);
        errno = saved;
        end = (size_t)(e - t);

        if(end == sign_at)
            end = 0;
    }

    if(endp)
        *endp = end == 0 ? (char *)s : starts[end < starts.size() ? end : starts.size() - 1];

    return value;
}

double uatof(const char *s)
{
    return ustrtod(s, nullptr);
}

const char *ustrerror(int err)
{
    static char buf[1024];
    const char *msg = strerror(err);
    return uconvert(msg, U_ASCII, buf, U_CURRENT, (int)sizeof(buf));
}

} // extern "C"

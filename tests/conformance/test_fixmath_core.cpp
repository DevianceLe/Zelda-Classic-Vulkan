/*
 * test_fixmath_core.cpp - fixed-point math must match the reference build
 * bit for bit: sprite movement, scripts and collision all run on it.
 *
 * Covers: the lookup tables, ftofix/fixtof/itofix/fixtoi/fixfloor/fixceil,
 * fixadd/fixsub/fixmul/fixdiv (incl. overflow and errno), the trig and root
 * functions, and every operator of the C++ fix class with mixed operand types.
 */
#include "conf.h"

#include <errno.h>
#include <math.h>

namespace
{

/* Interesting fixed values: zero, unit fractions, integer boundaries,
 * rounding boundaries (0x8000), and the extremes. */
const fixed kEdge[] =
{
    0, 1, -1, 2, -2, 0x7FFF, -0x7FFF, 0x8000, -0x8000, 0x8001, -0x8001,
    0xFFFF, -0xFFFF, 0x10000, -0x10000, 0x10001, -0x10001, 0x18000, -0x18000,
    0x00FF0000, -0x00FF0000, 0x01000000, -0x01000000, 0x40000000, -0x40000000,
    0x7FFF0000, -0x7FFF0000, 0x7FFF8000, -0x7FFF8000, 0x7FFFFFFF, -0x7FFFFFFF,
    (fixed)0x80000000
};
const int kEdgeCount = (int)(sizeof(kEdge) / sizeof(kEdge[0]));

/* A random fixed value drawn from several magnitude classes. */
fixed rand_fixed(conf::Rng &rng)
{
    switch(rng.range(0, 5))
    {
    case 0: return (fixed)rng.next();                       /* anything */
    case 1: return (fixed)(rng.next() >> 8) - 0x800000;     /* +-128 */
    case 2: return (fixed)(rng.next() >> 12) - 0x80000;     /* +-8 */
    case 3: return (fixed)(rng.next() >> 16) - 0x8000;      /* +-0.5 */
    case 4: return itofix(rng.range(-320, 320));            /* whole numbers */
    default: return kEdge[rng.range(0, kEdgeCount - 1)];
    }
}

/* Run op under a clean errno and record both the result and errno. */
#define RECORD(seq, expr, ...) \
    do { errno = 0; long long r_ = (long long)(expr); int e_ = errno; \
         seq.add_int(r_, __VA_ARGS__); seq.add_int(e_, "errno"); } while(0)

} // namespace

CONF_TEST(fixmath_tables)
{
    conf::Seq cos_t(ctx, "_cos_tbl");
    for(int i = 0; i < 512; ++i)
        cos_t.add_hex((unsigned)_cos_tbl[i], "%d", i);
    cos_t.end();

    conf::Seq tan_t(ctx, "_tan_tbl");
    for(int i = 0; i < 256; ++i)
        tan_t.add_hex((unsigned)_tan_tbl[i], "%d", i);
    tan_t.end();

    conf::Seq acos_t(ctx, "_acos_tbl");
    for(int i = 0; i < 513; ++i)
        acos_t.add_hex((unsigned)_acos_tbl[i], "%d", i);
    acos_t.end();
}

CONF_TEST(fixmath_conversions)
{
    /* ftofix: dense around rounding boundaries, plus the clamp region. */
    {
        conf::Seq s(ctx, "ftofix_grid");
        for(int i = -40000; i <= 40000; i += 7)
        {
            double base = i * 1.0;
            RECORD(s, ftofix(base), "%d", i);
            RECORD(s, ftofix(base + 0.5 / 65536.0), "%d+half", i);
            RECORD(s, ftofix(base - 0.5 / 65536.0), "%d-half", i);
            RECORD(s, ftofix(base + 0.25), "%d+0.25", i);
            RECORD(s, ftofix(-base - 0.75), "-%d-0.75", i);
        }
        s.end();
    }
    {
        conf::Seq s(ctx, "ftofix_random");
        for(int i = 0; i < 20000; ++i)
        {
            double d = ((double)(int)ctx.rng.next()) / 65536.0 * (ctx.rng.range(0, 3) == 0 ? 1.7 : 1.0);
            RECORD(s, ftofix(d), "%d", i);
        }
        RECORD(s, ftofix(32767.0), "32767");
        RECORD(s, ftofix(32767.49999), "32767.49999");
        RECORD(s, ftofix(32767.99999), "32767.99999");
        RECORD(s, ftofix(32768.0), "32768");
        RECORD(s, ftofix(-32767.0), "-32767");
        RECORD(s, ftofix(-32768.0), "-32768");
        RECORD(s, ftofix(1e30), "1e30");
        RECORD(s, ftofix(-1e30), "-1e30");
        s.end();
    }

    /* fixtof, fixfloor, fixceil, fixtoi on edges and random values. */
    conf::Seq tof(ctx, "fixtof");
    conf::Seq flo(ctx, "fixfloor");
    conf::Seq cei(ctx, "fixceil");
    conf::Seq toi(ctx, "fixtoi");
    for(int i = 0; i < kEdgeCount + 30000; ++i)
    {
        fixed x = (i < kEdgeCount) ? kEdge[i] : rand_fixed(ctx.rng);
        tof.add_double(fixtof(x), "%d", (int)x);
        RECORD(flo, fixfloor(x), "%d", (int)x);
        RECORD(cei, fixceil(x), "%d", (int)x);
        RECORD(toi, fixtoi(x), "%d", (int)x);
    }
    tof.end();
    flo.end();
    cei.end();
    toi.end();

    conf::Seq ito(ctx, "itofix");
    for(int i = -70000; i <= 70000; i += 3)
        ito.add_int(itofix(i), "%d", i);
    ito.end();
}

CONF_TEST(fixmath_arith)
{
    conf::Seq add(ctx, "fixadd");
    conf::Seq sub(ctx, "fixsub");
    conf::Seq mul(ctx, "fixmul");
    conf::Seq dvd(ctx, "fixdiv");

    for(int a = 0; a < kEdgeCount; ++a)
        for(int b = 0; b < kEdgeCount; ++b)
        {
            fixed x = kEdge[a], y = kEdge[b];
            RECORD(add, fixadd(x, y), "%d,%d", (int)x, (int)y);
            RECORD(sub, fixsub(x, y), "%d,%d", (int)x, (int)y);
            RECORD(mul, fixmul(x, y), "%d,%d", (int)x, (int)y);
            RECORD(dvd, fixdiv(x, y), "%d,%d", (int)x, (int)y);
        }

    for(int i = 0; i < 60000; ++i)
    {
        fixed x = rand_fixed(ctx.rng), y = rand_fixed(ctx.rng);
        RECORD(add, fixadd(x, y), "%d,%d", (int)x, (int)y);
        RECORD(sub, fixsub(x, y), "%d,%d", (int)x, (int)y);
        RECORD(mul, fixmul(x, y), "%d,%d", (int)x, (int)y);
        RECORD(dvd, fixdiv(x, y), "%d,%d", (int)x, (int)y);
    }
    add.end();
    sub.end();
    mul.end();
    dvd.end();
}

CONF_TEST(fixmath_trig)
{
    conf::Seq cs(ctx, "fixcos");
    conf::Seq sn(ctx, "fixsin");
    conf::Seq tn(ctx, "fixtan");
    for(int x = -0x3000000; x <= 0x3000000; x += 0x0FF1)
    {
        RECORD(cs, fixcos(x), "%d", x);
        RECORD(sn, fixsin(x), "%d", x);
        RECORD(tn, fixtan(x), "%d", x);
    }
    for(int i = 0; i < 20000; ++i)
    {
        fixed x = (fixed)ctx.rng.next();
        RECORD(cs, fixcos(x), "%d", (int)x);
        RECORD(sn, fixsin(x), "%d", (int)x);
        RECORD(tn, fixtan(x), "%d", (int)x);
    }
    cs.end();
    sn.end();
    tn.end();

    conf::Seq ac(ctx, "fixacos");
    conf::Seq as(ctx, "fixasin");
    for(int x = -0x14000; x <= 0x14000; x += 3)
    {
        RECORD(ac, fixacos(x), "%d", x);
        RECORD(as, fixasin(x), "%d", x);
    }
    ac.end();
    as.end();

    conf::Seq at(ctx, "fixatan");
    for(int i = 0; i < kEdgeCount; ++i)
        RECORD(at, fixatan(kEdge[i]), "%d", (int)kEdge[i]);
    for(int x = -0x400000; x <= 0x400000; x += 0x101)
        RECORD(at, fixatan(x), "%d", x);
    for(int i = 0; i < 20000; ++i)
    {
        fixed x = rand_fixed(ctx.rng);
        RECORD(at, fixatan(x), "%d", (int)x);
    }
    at.end();

    conf::Seq at2(ctx, "fixatan2");
    for(int a = 0; a < kEdgeCount; ++a)
        for(int b = 0; b < kEdgeCount; ++b)
            RECORD(at2, fixatan2(kEdge[a], kEdge[b]), "%d,%d", (int)kEdge[a], (int)kEdge[b]);
    for(int i = 0; i < 40000; ++i)
    {
        fixed y = rand_fixed(ctx.rng), x = rand_fixed(ctx.rng);
        RECORD(at2, fixatan2(y, x), "%d,%d", (int)y, (int)x);
    }
    at2.end();
}

CONF_TEST(fixmath_roots)
{
    conf::Seq sq(ctx, "fixsqrt");
    for(int i = 0; i < kEdgeCount; ++i)
        RECORD(sq, fixsqrt(kEdge[i]), "%d", (int)kEdge[i]);
    for(int x = 0; x < 0x200000; x += 0x13)
        RECORD(sq, fixsqrt(x), "%d", x);
    for(int i = 0; i < 30000; ++i)
    {
        fixed x = rand_fixed(ctx.rng);
        RECORD(sq, fixsqrt(x), "%d", (int)x);
    }
    sq.end();

    conf::Seq hy(ctx, "fixhypot");
    for(int a = 0; a < kEdgeCount; ++a)
        for(int b = 0; b < kEdgeCount; ++b)
            RECORD(hy, fixhypot(kEdge[a], kEdge[b]), "%d,%d", (int)kEdge[a], (int)kEdge[b]);
    for(int i = 0; i < 30000; ++i)
    {
        fixed x = rand_fixed(ctx.rng), y = rand_fixed(ctx.rng);
        RECORD(hy, fixhypot(x, y), "%d,%d", (int)x, (int)y);
    }
    hy.end();
}

/* ---- the C++ fix class ---------------------------------------------------- */

namespace
{

fix make_fix(fixed v)
{
    fix f;
    f.v = v;
    return f;
}

/* One battery of every operator for operands a (fix) and b (fix), i (int),
 * l (long), fl (float), d (double); each result's raw value goes into s. */
void fix_battery(conf::Seq &s, fixed av, fixed bv, int i, long l, float fl, double d)
{
    fix a = make_fix(av), b = make_fix(bv);

#define R(label, expr) do { errno = 0; fix r_ = (expr); int e_ = errno; \
        s.add_int(r_.v, label); s.add_int(e_, "errno"); } while(0)
#define RB(label, expr) s.add_int((expr) ? 1 : 0, label)

    R("a+b", a + b);   R("a+i", a + i);   R("i+a", i + a);   R("a+l", a + l);   R("l+a", l + a);
    R("a+f", a + fl);  R("f+a", fl + a);  R("a+d", a + d);   R("d+a", d + a);
    R("a-b", a - b);   R("a-i", a - i);   R("i-a", i - a);   R("a-l", a - l);   R("l-a", l - a);
    R("a-f", a - fl);  R("f-a", fl - a);  R("a-d", a - d);   R("d-a", d - a);
    R("a*b", a * b);   R("a*i", a * i);   R("i*a", i * a);   R("a*l", a * l);   R("l*a", l * a);
    R("a*f", a * fl);  R("f*a", fl * a);  R("a*d", a * d);   R("d*a", d * a);
    if(bv != 0) { R("a/b", a / b); }
    if(i != 0)  { R("a/i", a / i); }
    if(av != 0) { R("i/a", i / a); R("l/a", l / a); R("f/a", fl / a); R("d/a", d / a); }
    if(l != 0)  { R("a/l", a / l); }
    if(fl != 0) { R("a/f", a / fl); }
    if(d != 0)  { R("a/d", a / d); }
    R("-a", -a);
    R("a<<3", a << 3); R("a>>3", a >> 3); R("a>>17", a >> 17);

    { fix t = a; t += b;  R("+=b", t); }
    { fix t = a; t += i;  R("+=i", t); }
    { fix t = a; t += l;  R("+=l", t); }
    { fix t = a; t += fl; R("+=f", t); }
    { fix t = a; t += d;  R("+=d", t); }
    { fix t = a; t -= b;  R("-=b", t); }
    { fix t = a; t -= i;  R("-=i", t); }
    { fix t = a; t -= l;  R("-=l", t); }
    { fix t = a; t -= fl; R("-=f", t); }
    { fix t = a; t -= d;  R("-=d", t); }
    { fix t = a; t *= b;  R("*=b", t); }
    { fix t = a; t *= i;  R("*=i", t); }
    { fix t = a; t *= l;  R("*=l", t); }
    { fix t = a; t *= fl; R("*=f", t); }
    { fix t = a; t *= d;  R("*=d", t); }
    if(bv != 0) { fix t = a; t /= b;  R("/=b", t); }
    if(i != 0)  { fix t = a; t /= i;  R("/=i", t); }
    if(l != 0)  { fix t = a; t /= l;  R("/=l", t); }
    if(fl != 0) { fix t = a; t /= fl; R("/=f", t); }
    if(d != 0)  { fix t = a; t /= d;  R("/=d", t); }
    { fix t = a; t <<= 2; R("<<=2", t); }
    { fix t = a; t >>= 2; R(">>=2", t); }
    { fix t = a; ++t; R("++pre", t); }
    { fix t = a; fix u = t++; R("post++old", u); R("post++new", t); }
    { fix t = a; --t; R("--pre", t); }
    { fix t = a; fix u = t--; R("post--old", u); R("post--new", t); }

    { fix t; t = i;  R("=i", t); }
    { fix t; t = l;  R("=l", t); }
    { fix t; t = fl; R("=f", t); }
    { fix t; t = d;  R("=d", t); }
    { fix t; t = (unsigned int)(i & 0x7FFF); R("=u", t); }
    { fix t(i);  R("ctor_i", t); }
    { fix t(l);  R("ctor_l", t); }
    { fix t(fl); R("ctor_f", t); }
    { fix t(d);  R("ctor_d", t); }

    s.add_int((int)a, "(int)a");
    s.add_int((long)a, "(long)a");
    s.add_double((double)(float)a, "(float)a");
    s.add_double((double)a, "(double)a");

    RB("a==b", a == b); RB("a!=b", a != b); RB("a<b", a < b); RB("a>b", a > b); RB("a<=b", a <= b); RB("a>=b", a >= b);
    RB("a==i", a == i); RB("i==a", i == a); RB("a<i", a < i); RB("i<a", i < a); RB("a>=i", a >= i); RB("i>=a", i >= a);
    RB("a==d", a == d); RB("a<d", a < d);   RB("d<a", d < a); RB("a>f", a > fl); RB("f>a", fl > a); RB("a!=l", a != l);

#undef R
#undef RB
}

} // namespace

CONF_TEST(fixmath_fix_class)
{
    conf::Seq s(ctx, "fix_ops");
    for(int k = 0; k < kEdgeCount; ++k)
        fix_battery(s, kEdge[k], kEdge[(k * 7 + 3) % kEdgeCount], k - 16, 3 - k, k * 0.37f - 5.0f, k * -1.25 + 2.5);
    for(int k = 0; k < 4000; ++k)
    {
        fixed a = rand_fixed(ctx.rng), b = rand_fixed(ctx.rng);
        int i = ctx.rng.range(-300, 300);
        long l = (long)ctx.rng.range(-70000, 70000);
        float fl = (float)((int)ctx.rng.next() % 200000) / 1000.0f;
        double d = (double)((int)ctx.rng.next()) / 65536.0 / 64.0;
        fix_battery(s, a, b, i, l, fl, d);
    }
    s.end();
}

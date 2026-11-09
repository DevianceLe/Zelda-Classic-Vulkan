/*
 * zcport: fixed-point trigonometry and roots (fixsin, fixcos, fixtan,
 * fixasin, fixacos, fixatan, fixatan2, fixsqrt, fixhypot) and the lookup
 * tables behind them.
 *
 * Angles are binary angles in 16.16 fixed point: 256.0 is a full circle.
 * Results must equal the reference build bit for bit (sprite movement,
 * scripts and collision depend on them); tests/conformance/test_fixmath_core
 * checks every function and table entry against the real library.
 *
 * The tables are generated here from closed-form formulas that reproduce
 * every entry of the reference tables exactly (verified by the conformance
 * test); no table data is copied.
 *
 * Written from scratch for zcport.
 */

#include <allegro.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>

extern "C" {
fixed _cos_tbl[512];
fixed _tan_tbl[256];
fixed _acos_tbl[513];
}

namespace
{

const double kPi = 3.14159265358979323846;

/* Round to nearest, halves away from zero. */
fixed round_to_fixed(double v)
{
    return (fixed)((v < 0) ? -floor(-v + 0.5) : floor(v + 0.5));
}

/* a * b as a 128-bit number (hi, lo), portable. */
void mul_64x64(uint64_t a, uint64_t b, uint64_t &hi, uint64_t &lo)
{
    const uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32;
    const uint64_t b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    lo = (mid << 32) | (p00 & 0xFFFFFFFFu);
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

/* True when sqrt(n) lies below the midpoint of the adjacent doubles lo < hi,
 * both in [2^26, 2^32) (so they are multiples of 2^-26 and the midpoint times
 * 2^27 is an integer below 2^59). Compares n * 2^54 with (mid * 2^27)^2; they
 * are never equal (an irrational root is no midpoint, an integer root is a
 * double). */
bool root_below_mid(uint64_t n, double lo, double hi)
{
    const uint64_t m = ((uint64_t)(lo * 134217728.0) + (uint64_t)(hi * 134217728.0)) / 2;
    uint64_t mhi, mlo;
    mul_64x64(m, m, mhi, mlo);
    const uint64_t nhi = n >> 10, nlo = n << 54;
    return (nhi != mhi) ? (nhi < mhi) : (nlo < mlo);
}

/* sqrt(n) rounded to the nearest double, exactly, for n <= 2^63. sqrt() is
 * correctly rounded everywhere (IEEE 754), so this only needs care when
 * (double)n itself is rounded (n > 2^53). */
double root_rounded(uint64_t n)
{
    double r = sqrt((double)n);
    if(n <= ((uint64_t)1 << 53))
        return r;
    for(;;)
    {
        const double below = nextafter(r, 0.0);
        if(root_below_mid(n, below, r))
        {
            r = below;
            continue;
        }
        const double above = nextafter(r, 4294967296.0);
        if(!root_below_mid(n, r, above))
        {
            r = above;
            continue;
        }
        return r;
    }
}

/* Fills the tables before main() runs (and before any game code). */
struct TableInit
{
    TableInit()
    {
        /* cos of 512 equal steps around the circle, 16.16 */
        for(int i = 0; i < 512; ++i)
            _cos_tbl[i] = round_to_fixed(cos(i * 2.0 * kPi / 512.0) * 65536.0);

        /* tan of 256 equal steps over half a circle; the 90 degree entry
         * (infinite) holds the most negative representable value */
        for(int i = 0; i < 256; ++i)
            _tan_tbl[i] = (i == 128) ? (fixed)-0x7FFFFFFF
                                     : round_to_fixed(tan(i * kPi / 256.0) * 65536.0);

        /* acos of 513 steps from -1 to 1, as a binary angle in 16.16
         * (pi == 128.0) */
        for(int i = 0; i <= 512; ++i)
            _acos_tbl[i] = round_to_fixed(acos((i - 256) / 256.0) / kPi * (double)0x800000);
    }
};

TableInit g_table_init;

/* Index of the 512-entry circle table for angle x, rounded to nearest. */
inline int circle_index(fixed x)
{
    return (int)(((x + 0x4000) >> 15) & 0x1FF);
}

/* Index of the 513-entry acos table for a value in [-1, 1]. */
inline int acos_index(fixed x)
{
    return (int)((x + 0x10000 + 127) >> 8);
}

} // namespace

extern "C" {

fixed fixcos(fixed x)
{
    return _cos_tbl[circle_index(x)];
}

fixed fixsin(fixed x)
{
    /* sin(a) == cos(a - quarter circle); 64.0 == 0x400000 */
    return _cos_tbl[circle_index(x - 0x400000)];
}

fixed fixtan(fixed x)
{
    return _tan_tbl[((x + 0x4000) >> 15) & 0xFF];
}

fixed fixacos(fixed x)
{
    if(x < -0x10000 || x > 0x10000)
    {
        *allegro_errno = EDOM;
        return 0;
    }

    return _acos_tbl[acos_index(x)];
}

fixed fixasin(fixed x)
{
    if(x < -0x10000 || x > 0x10000)
    {
        *allegro_errno = EDOM;
        return 0;
    }

    /* asin(v) == quarter circle - acos(v) */
    return 0x400000 - _acos_tbl[acos_index(x)];
}

fixed fixatan(fixed x)
{
    /* Search the tangent table for x within the half circle that matches
     * its sign (entries 0..127 hold tan of 0..90 degrees, 128..255 tan of
     * 90..180 degrees, i.e. the negative values). The search halves the
     * candidate range, stops on an exact hit or when the range is empty,
     * and answers with the angle of the entry it examined last. */
    int lo = (x >= 0) ? 0 : 128;
    int hi = (x >= 0) ? 127 : 255;
    int probe = lo;
    fixed diff;

    do
    {
        probe = (lo + hi) >> 1;
        diff = x - _tan_tbl[probe];

        if(diff > 0)
            lo = probe + 1;
        else if(diff < 0)
            hi = probe - 1;
    }
    while(lo <= hi && diff != 0);

    /* each table step is half a binary degree: probe << 15 */
    if(x >= 0)
        return (fixed)probe << 15;

    return (fixed)(-0x00800000 + ((fixed)probe << 15));
}

fixed fixatan2(fixed y, fixed x)
{
    if(x == 0)
    {
        if(y == 0)
        {
            *allegro_errno = EDOM;
            return 0;
        }

        return (y < 0) ? -0x00400000 : 0x00400000;
    }

    /* The quotient may overflow; that means the angle is (almost) vertical. */
    *allegro_errno = 0;
    fixed ratio = fixdiv(y, x);

    if(*allegro_errno)
    {
        *allegro_errno = 0;
        return (y < 0) ? -0x00400000 : 0x00400000;
    }

    fixed angle = fixatan(ratio);

    if(x >= 0)
        return angle;

    if(y >= 0)
        return 0x00800000 + angle;

    return angle - 0x00800000;
}

fixed fixsqrt(fixed x)
{
    if(x > 0)
        return ftofix(sqrt(fixtof(x)));

    if(x < 0)
        *allegro_errno = EDOM;

    return 0;
}

/* The reference is ftofix(hypot(fixtof(x), fixtof(y))) with a correctly
 * rounded hypot (the 32-bit CRT's). Other C libraries round hypot() their
 * own way: the x64 CRT returns 32767.000000000007 for (18, 2147418112), which
 * overflows. So the root is rounded here exactly, the same on every system:
 * with X = x*x + y*y (exact in 64 bits), hypot * 65536 = sqrt(X), and the
 * reference's double is sqrt(X) rounded to nearest. */
fixed fixhypot(fixed x, fixed y)
{
    const uint64_t ax = (x < 0) ? (uint64_t)0 - (uint64_t)(int64_t)x : (uint64_t)x;
    const uint64_t ay = (y < 0) ? (uint64_t)0 - (uint64_t)(int64_t)y : (uint64_t)y;
    const uint64_t sum = ax * ax + ay * ay;   /* <= 2^63 */
    return ftofix(root_rounded(sum) / 65536.0);
}

} // extern "C"

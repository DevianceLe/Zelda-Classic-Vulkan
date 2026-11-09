/*
 * zcport: Allegro 4.4 compatible API for the Zelda Classic player.
 *
 * allegro/fix.h - the C++ "fix" wrapper around 16.16 fixed point.
 *
 * The overload set is the same as Allegro's (it decides which conversion a
 * mixed expression takes, so it must not change). The arithmetic follows
 * the documented Allegro semantics and is checked against the reference
 * build by tests/conformance/test_foundation_fixed.cpp:
 *   fix (op) fix          raw add/sub of .v; fixmul/fixdiv for * and /
 *   fix (op) int/long     add/sub itofix(n); multiply/divide .v by n
 *   int/long / fix        fixdiv(itofix(n), f.v)
 *   fix (op) float/double ftofix() of the double result
 *   comparisons           compare .v with itofix(n) / ftofix(d)
 *
 * Written from scratch for zcport. Must stay C++98 compatible.
 */

#ifndef ALLEGRO_FIX_H
#define ALLEGRO_FIX_H

#include "fixed.h"
#include "fmaths.h"

#ifdef __cplusplus

class fix
{
public:
   fixed v;

   fix() : v(0) {}
   fix(const fix &x) : v(x.v) {}
   explicit fix(const int x) : v(itofix(x)) {}
   explicit fix(const long x) : v(itofix((int)x)) {}
   explicit fix(const unsigned int x) : v(itofix((int)x)) {}
   explicit fix(const unsigned long x) : v(itofix((int)x)) {}
   explicit fix(const float x) : v(ftofix(x)) {}
   explicit fix(const double x) : v(ftofix(x)) {}

   operator int() const            { return fixtoi(v); }
   operator long() const           { return fixtoi(v); }
   operator unsigned int() const   { return (unsigned int)fixtoi(v); }
   operator unsigned long() const  { return (unsigned long)fixtoi(v); }
   operator float() const          { return (float)fixtof(v); }
   operator double() const         { return fixtof(v); }

   fix &operator = (const fix &x)           { v = x.v; return *this; }
   fix &operator = (const int x)            { v = itofix(x); return *this; }
   fix &operator = (const long x)           { v = itofix((int)x); return *this; }
   fix &operator = (const unsigned int x)   { v = itofix((int)x); return *this; }
   fix &operator = (const unsigned long x)  { v = itofix((int)x); return *this; }
   fix &operator = (const float x)          { v = ftofix(x); return *this; }
   fix &operator = (const double x)         { v = ftofix(x); return *this; }

   fix &operator += (const fix x)     { v += x.v; return *this; }
   fix &operator += (const int x)     { v += itofix(x); return *this; }
   fix &operator += (const long x)    { v += itofix((int)x); return *this; }
   fix &operator += (const float x)   { v += ftofix(x); return *this; }
   fix &operator += (const double x)  { v += ftofix(x); return *this; }

   fix &operator -= (const fix x)     { v -= x.v; return *this; }
   fix &operator -= (const int x)     { v -= itofix(x); return *this; }
   fix &operator -= (const long x)    { v -= itofix((int)x); return *this; }
   fix &operator -= (const float x)   { v -= ftofix(x); return *this; }
   fix &operator -= (const double x)  { v -= ftofix(x); return *this; }

   fix &operator *= (const fix x)     { v = fixmul(v, x.v); return *this; }
   fix &operator *= (const int x)     { v *= x; return *this; }
   fix &operator *= (const long x)    { v *= x; return *this; }
   fix &operator *= (const float x)   { v = ftofix(fixtof(v) * x); return *this; }
   fix &operator *= (const double x)  { v = ftofix(fixtof(v) * x); return *this; }

   fix &operator /= (const fix x)     { v = fixdiv(v, x.v); return *this; }
   fix &operator /= (const int x)     { v /= x; return *this; }
   fix &operator /= (const long x)    { v /= x; return *this; }
   fix &operator /= (const float x)   { v = ftofix(fixtof(v) / x); return *this; }
   fix &operator /= (const double x)  { v = ftofix(fixtof(v) / x); return *this; }

   fix &operator <<= (const int x)    { v <<= x; return *this; }
   fix &operator >>= (const int x)    { v >>= x; return *this; }

   fix &operator ++ ()                { v += itofix(1); return *this; }
   fix &operator -- ()                { v -= itofix(1); return *this; }

   fix operator ++ (int)              { fix old(*this); v += itofix(1); return old; }
   fix operator -- (int)              { fix old(*this); v -= itofix(1); return old; }

   fix operator - () const            { fix r; r.v = -v; return r; }

   inline friend fix operator +  (const fix x, const fix y);
   inline friend fix operator +  (const fix x, const int y);
   inline friend fix operator +  (const int x, const fix y);
   inline friend fix operator +  (const fix x, const long y);
   inline friend fix operator +  (const long x, const fix y);
   inline friend fix operator +  (const fix x, const float y);
   inline friend fix operator +  (const float x, const fix y);
   inline friend fix operator +  (const fix x, const double y);
   inline friend fix operator +  (const double x, const fix y);

   inline friend fix operator -  (const fix x, const fix y);
   inline friend fix operator -  (const fix x, const int y);
   inline friend fix operator -  (const int x, const fix y);
   inline friend fix operator -  (const fix x, const long y);
   inline friend fix operator -  (const long x, const fix y);
   inline friend fix operator -  (const fix x, const float y);
   inline friend fix operator -  (const float x, const fix y);
   inline friend fix operator -  (const fix x, const double y);
   inline friend fix operator -  (const double x, const fix y);

   inline friend fix operator *  (const fix x, const fix y);
   inline friend fix operator *  (const fix x, const int y);
   inline friend fix operator *  (const int x, const fix y);
   inline friend fix operator *  (const fix x, const long y);
   inline friend fix operator *  (const long x, const fix y);
   inline friend fix operator *  (const fix x, const float y);
   inline friend fix operator *  (const float x, const fix y);
   inline friend fix operator *  (const fix x, const double y);
   inline friend fix operator *  (const double x, const fix y);

   inline friend fix operator /  (const fix x, const fix y);
   inline friend fix operator /  (const fix x, const int y);
   inline friend fix operator /  (const int x, const fix y);
   inline friend fix operator /  (const fix x, const long y);
   inline friend fix operator /  (const long x, const fix y);
   inline friend fix operator /  (const fix x, const float y);
   inline friend fix operator /  (const float x, const fix y);
   inline friend fix operator /  (const fix x, const double y);
   inline friend fix operator /  (const double x, const fix y);

   inline friend fix operator << (const fix x, const int y);
   inline friend fix operator >> (const fix x, const int y);

   inline friend int operator == (const fix x, const fix y);
   inline friend int operator == (const fix x, const int y);
   inline friend int operator == (const int x, const fix y);
   inline friend int operator == (const fix x, const long y);
   inline friend int operator == (const long x, const fix y);
   inline friend int operator == (const fix x, const float y);
   inline friend int operator == (const float x, const fix y);
   inline friend int operator == (const fix x, const double y);
   inline friend int operator == (const double x, const fix y);

   inline friend int operator != (const fix x, const fix y);
   inline friend int operator != (const fix x, const int y);
   inline friend int operator != (const int x, const fix y);
   inline friend int operator != (const fix x, const long y);
   inline friend int operator != (const long x, const fix y);
   inline friend int operator != (const fix x, const float y);
   inline friend int operator != (const float x, const fix y);
   inline friend int operator != (const fix x, const double y);
   inline friend int operator != (const double x, const fix y);

   inline friend int operator <  (const fix x, const fix y);
   inline friend int operator <  (const fix x, const int y);
   inline friend int operator <  (const int x, const fix y);
   inline friend int operator <  (const fix x, const long y);
   inline friend int operator <  (const long x, const fix y);
   inline friend int operator <  (const fix x, const float y);
   inline friend int operator <  (const float x, const fix y);
   inline friend int operator <  (const fix x, const double y);
   inline friend int operator <  (const double x, const fix y);

   inline friend int operator >  (const fix x, const fix y);
   inline friend int operator >  (const fix x, const int y);
   inline friend int operator >  (const int x, const fix y);
   inline friend int operator >  (const fix x, const long y);
   inline friend int operator >  (const long x, const fix y);
   inline friend int operator >  (const fix x, const float y);
   inline friend int operator >  (const float x, const fix y);
   inline friend int operator >  (const fix x, const double y);
   inline friend int operator >  (const double x, const fix y);

   inline friend int operator <= (const fix x, const fix y);
   inline friend int operator <= (const fix x, const int y);
   inline friend int operator <= (const int x, const fix y);
   inline friend int operator <= (const fix x, const long y);
   inline friend int operator <= (const long x, const fix y);
   inline friend int operator <= (const fix x, const float y);
   inline friend int operator <= (const float x, const fix y);
   inline friend int operator <= (const fix x, const double y);
   inline friend int operator <= (const double x, const fix y);

   inline friend int operator >= (const fix x, const fix y);
   inline friend int operator >= (const fix x, const int y);
   inline friend int operator >= (const int x, const fix y);
   inline friend int operator >= (const fix x, const long y);
   inline friend int operator >= (const long x, const fix y);
   inline friend int operator >= (const fix x, const float y);
   inline friend int operator >= (const float x, const fix y);
   inline friend int operator >= (const fix x, const double y);
   inline friend int operator >= (const double x, const fix y);

   inline friend fix sqrt(fix x);
   inline friend fix cos(fix x);
   inline friend fix sin(fix x);
   inline friend fix tan(fix x);
   inline friend fix acos(fix x);
   inline friend fix asin(fix x);
   inline friend fix atan(fix x);
   inline friend fix atan2(fix x, fix y);
};

/* Builds a fix from a raw 16.16 value. */
static inline fix zcport_fix_raw(fixed raw) { fix r; r.v = raw; return r; }

/* + */
inline fix operator + (const fix x, const fix y)    { return zcport_fix_raw(x.v + y.v); }
inline fix operator + (const fix x, const int y)    { return zcport_fix_raw(x.v + itofix(y)); }
inline fix operator + (const int x, const fix y)    { return zcport_fix_raw(itofix(x) + y.v); }
inline fix operator + (const fix x, const long y)   { return zcport_fix_raw(x.v + itofix((int)y)); }
inline fix operator + (const long x, const fix y)   { return zcport_fix_raw(itofix((int)x) + y.v); }
inline fix operator + (const fix x, const float y)  { return zcport_fix_raw(x.v + ftofix(y)); }
inline fix operator + (const float x, const fix y)  { return zcport_fix_raw(ftofix(x) + y.v); }
inline fix operator + (const fix x, const double y) { return zcport_fix_raw(x.v + ftofix(y)); }
inline fix operator + (const double x, const fix y) { return zcport_fix_raw(ftofix(x) + y.v); }

/* - */
inline fix operator - (const fix x, const fix y)    { return zcport_fix_raw(x.v - y.v); }
inline fix operator - (const fix x, const int y)    { return zcport_fix_raw(x.v - itofix(y)); }
inline fix operator - (const int x, const fix y)    { return zcport_fix_raw(itofix(x) - y.v); }
inline fix operator - (const fix x, const long y)   { return zcport_fix_raw(x.v - itofix((int)y)); }
inline fix operator - (const long x, const fix y)   { return zcport_fix_raw(itofix((int)x) - y.v); }
inline fix operator - (const fix x, const float y)  { return zcport_fix_raw(x.v - ftofix(y)); }
inline fix operator - (const float x, const fix y)  { return zcport_fix_raw(ftofix(x) - y.v); }
inline fix operator - (const fix x, const double y) { return zcport_fix_raw(x.v - ftofix(y)); }
inline fix operator - (const double x, const fix y) { return zcport_fix_raw(ftofix(x) - y.v); }

/* * */
inline fix operator * (const fix x, const fix y)    { return zcport_fix_raw(fixmul(x.v, y.v)); }
inline fix operator * (const fix x, const int y)    { return zcport_fix_raw(x.v * y); }
inline fix operator * (const int x, const fix y)    { return zcport_fix_raw(x * y.v); }
inline fix operator * (const fix x, const long y)   { return zcport_fix_raw((fixed)(x.v * y)); }
inline fix operator * (const long x, const fix y)   { return zcport_fix_raw((fixed)(x * y.v)); }
inline fix operator * (const fix x, const float y)  { return zcport_fix_raw(ftofix(fixtof(x.v) * y)); }
inline fix operator * (const float x, const fix y)  { return zcport_fix_raw(ftofix(x * fixtof(y.v))); }
inline fix operator * (const fix x, const double y) { return zcport_fix_raw(ftofix(fixtof(x.v) * y)); }
inline fix operator * (const double x, const fix y) { return zcport_fix_raw(ftofix(x * fixtof(y.v))); }

/* / */
inline fix operator / (const fix x, const fix y)    { return zcport_fix_raw(fixdiv(x.v, y.v)); }
inline fix operator / (const fix x, const int y)    { return zcport_fix_raw(x.v / y); }
inline fix operator / (const int x, const fix y)    { return zcport_fix_raw(fixdiv(itofix(x), y.v)); }
inline fix operator / (const fix x, const long y)   { return zcport_fix_raw((fixed)(x.v / y)); }
inline fix operator / (const long x, const fix y)   { return zcport_fix_raw(fixdiv(itofix((int)x), y.v)); }
inline fix operator / (const fix x, const float y)  { return zcport_fix_raw(ftofix(fixtof(x.v) / y)); }
inline fix operator / (const float x, const fix y)  { return zcport_fix_raw(ftofix(x / fixtof(y.v))); }
inline fix operator / (const fix x, const double y) { return zcport_fix_raw(ftofix(fixtof(x.v) / y)); }
inline fix operator / (const double x, const fix y) { return zcport_fix_raw(ftofix(x / fixtof(y.v))); }

/* shifts */
inline fix operator << (const fix x, const int y)   { return zcport_fix_raw(x.v << y); }
inline fix operator >> (const fix x, const int y)   { return zcport_fix_raw(x.v >> y); }

/* comparisons: fix against fix, int/long (via itofix), float/double (via ftofix) */
#define ZCPORT_FIX_COMPARE(OP)                                                            \
inline int operator OP (const fix x, const fix y)    { return x.v OP y.v; }               \
inline int operator OP (const fix x, const int y)    { return x.v OP itofix(y); }         \
inline int operator OP (const int x, const fix y)    { return itofix(x) OP y.v; }         \
inline int operator OP (const fix x, const long y)   { return x.v OP itofix((int)y); }    \
inline int operator OP (const long x, const fix y)   { return itofix((int)x) OP y.v; }    \
inline int operator OP (const fix x, const float y)  { return x.v OP ftofix(y); }         \
inline int operator OP (const float x, const fix y)  { return ftofix(x) OP y.v; }         \
inline int operator OP (const fix x, const double y) { return x.v OP ftofix(y); }         \
inline int operator OP (const double x, const fix y) { return ftofix(x) OP y.v; }

ZCPORT_FIX_COMPARE(==)
ZCPORT_FIX_COMPARE(!=)
ZCPORT_FIX_COMPARE(<)
ZCPORT_FIX_COMPARE(>)
ZCPORT_FIX_COMPARE(<=)
ZCPORT_FIX_COMPARE(>=)

#undef ZCPORT_FIX_COMPARE

/* math */
inline fix sqrt(fix x)          { return zcport_fix_raw(fixsqrt(x.v)); }
inline fix cos(fix x)           { return zcport_fix_raw(fixcos(x.v)); }
inline fix sin(fix x)           { return zcport_fix_raw(fixsin(x.v)); }
inline fix tan(fix x)           { return zcport_fix_raw(fixtan(x.v)); }
inline fix acos(fix x)          { return zcport_fix_raw(fixacos(x.v)); }
inline fix asin(fix x)          { return zcport_fix_raw(fixasin(x.v)); }
inline fix atan(fix x)          { return zcport_fix_raw(fixatan(x.v)); }
inline fix atan2(fix x, fix y)  { return zcport_fix_raw(fixatan2(x.v, y.v)); }

#endif /* __cplusplus */

#endif /* ALLEGRO_FIX_H */

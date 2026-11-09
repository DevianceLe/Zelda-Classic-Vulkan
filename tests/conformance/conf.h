/*
 * conf.h - reference-vs-port conformance framework.
 *
 * The same test source is compiled twice: once against the real Allegro 4.4
 * headers (conf_<name>_ref, linked with alleg44.dll) and once against the
 * replacement headers in src/port/include (conf_<name>_port, linked with
 * zcport). Both programs print one line per check,
 *
 *     test_name/case: value
 *
 * and run_conformance.py diffs the two outputs. A test passes only when every
 * line matches. Lines starting with '#' are notes and are not compared.
 *
 * Rules for test code:
 *   - use only the public Allegro API that the player uses (see
 *     src/port/notes/allegro_api_inventory.md), so it compiles on both sides;
 *   - never print pointers, addresses, timings or paths (they differ between
 *     runs); print values or hashes of values;
 *   - get randomness only from ctx.rng (seeded from the test name, so it is the
 *     same on both sides and independent of --filter);
 *   - case names must not contain ": ".
 *
 * This header is C++98-compatible on purpose.
 */
#ifndef ZC_CONFORMANCE_CONF_H
#define ZC_CONFORMANCE_CONF_H

/* Same configuration as the game (src/zc_alleg.h). */
#ifndef ALLEGRO_NO_COMPATIBILITY
#define ALLEGRO_NO_COMPATIBILITY
#endif
#ifndef DEBUGMODE
#define DEBUGMODE
#endif
/* Console program: no WinMain "magic main" on the reference side. */
#ifndef ALLEGRO_USE_CONSOLE
#define ALLEGRO_USE_CONSOLE
#endif

#include <allegro.h>

#include <stdarg.h>
#include <stddef.h>
#include <string>

#if defined(CONF_SIDE_REF)
#define CONF_SIDE "ref"
#elif defined(CONF_SIDE_PORT)
#define CONF_SIDE "port"
#else
#define CONF_SIDE "unknown"
#endif

namespace conf
{

typedef unsigned long long u64;
typedef unsigned int u32;

/* ------------------------------------------------------------------------- */
/* Hashing: 64-bit FNV-1a.                                                    */
/* ------------------------------------------------------------------------- */

class Hash
{
public:
    Hash();
    void add_bytes(const void *data, size_t size);
    void add_u8(unsigned v);
    void add_u16(unsigned v);          /* little-endian */
    void add_u32(u32 v);               /* little-endian */
    void add_u64(u64 v);               /* little-endian */
    void add_i32(int v);
    void add_str(const char *s);       /* NULL hashes differently from "" */
    u64 value() const { return h_; }
private:
    u64 h_;
};

u64 hash_bytes(const void *data, size_t size);
u64 hash_string(const char *s);
/* Hashes w, h, color depth and the w*bytes_per_pixel bytes of every row read
 * through bmp->line[y] (padding, struct fields and pointers are not hashed).
 * NULL gives a fixed distinct value. */
u64 hash_bitmap(BITMAP *bmp);
/* Hashes r, g, b of count entries (the filler byte is not hashed). */
u64 hash_palette(const RGB *pal, int count = PAL_SIZE);
/* 1 for 8-bit, 2 for 15/16-bit, 3 for 24-bit, 4 for 32-bit. */
int bytes_per_pixel(int color_depth);

/* "h:0123456789abcdef" */
std::string hash_text(u64 h);

/* printf into one of 16 rotating static buffers (handy for case names). */
const char *nm(const char *fmt, ...);

/* ------------------------------------------------------------------------- */
/* Deterministic RNG: xorshift64* with fixed seeds.                          */
/* ------------------------------------------------------------------------- */

class Rng
{
public:
    explicit Rng(u64 seed_value = 1);
    void seed(u64 seed_value);
    u32 next();                        /* 32 random bits */
    u64 next64();
    int range(int lo, int hi);         /* uniform-ish in [lo, hi] (inclusive) */
    int bits(int n);                   /* n random bits (0..32) as an int */
    void fill(void *data, size_t size);
private:
    u64 s_;
};

/* ------------------------------------------------------------------------- */
/* Test context.                                                             */
/* ------------------------------------------------------------------------- */

class Ctx
{
public:
    explicit Ctx(const char *test_name);

    const char *name() const { return name_; }

    /* Seeded from the test name before the test runs. */
    Rng rng;

    /* Each check prints "name()/c: value". */
    void check_int(const char *c, long long v);                 /* decimal */
    void check_hex(const char *c, unsigned long long v);        /* 0x%08llx */
    void check_bool(const char *c, bool v);                     /* true/false */
    void check_double(const char *c, double v);                 /* %.17g + bits */
    void check_str(const char *c, const char *s);               /* quoted, escaped */
    void check_hash(const char *c, u64 h);
    void check_mem(const char *c, const void *data, size_t size);
    void check_bitmap(const char *c, BITMAP *bmp);              /* "bmp WxHxD h:..." */
    /* "clip=<flag> cl=.. ct=.. cr=.. cb=.." straight from the struct fields
     * (a template, so it is only compiled where a test uses it). */
    template <class B> void check_clip(const char *c, B *bmp)
    {
        if(!bmp)
            checkf(c, "(null)");
        else
            checkf(c, "clip=%d cl=%d ct=%d cr=%d cb=%d", (int)bmp->clip,
                   (int)bmp->cl, (int)bmp->ct, (int)bmp->cr, (int)bmp->cb);
    }
    void check_palette(const char *c, const RGB *pal, int count = PAL_SIZE);
    void check_file(const char *c, const char *path);           /* hash of file bytes (stdio) */
    void checkf(const char *c, const char *fmt, ...);           /* free-form value */

    /* Prints "ok" or "FAIL <msg>"; the runner fails any line whose value
     * starts with FAIL, even if both sides print it. */
    void expect(const char *c, bool ok, const char *fmt = 0, ...);
    void fail(const char *c, const char *fmt, ...);

    /* Prints "name()/skipped: SKIP <reason>". */
    void skip(const char *reason_fmt, ...);
    /* "# name(): ..." (not compared). */
    void note(const char *fmt, ...);

    /* --data, --quests and --tmp directories joined with a file name. */
    std::string data(const char *file) const;
    std::string quest(const char *file) const;
    std::string tmp(const char *file) const;
    bool have_data(const char *file) const;     /* fopen check, no Allegro */
    bool have_quest(const char *file) const;

    /* True when the program runs with --expand (sequences print every item). */
    bool expanding() const;

    /* Used by the framework. */
    void emit(const char *c, const std::string &value);
    void dump(const char *c, const char *kind, const void *head, size_t head_size,
              const void *data, size_t size);
    int checks() const { return checks_; }
private:
    const char *name_;
    int checks_;
};

/*
 * A sequence of values checked as one line. Normally prints
 *     test/c: seq n=<count> h:<hash>
 * when finished; with --expand it also prints every item as
 *     test/c[i]: <value>  ; <label>
 * so the runner can show the first item that differs. The label (printf
 * format) is only formatted when expanding; describe the inputs in it.
 */
class Seq
{
public:
    Seq(Ctx &ctx, const char *c);
    ~Seq();                            /* calls end() if not done yet */
    void add_int(long long v, const char *label = 0, ...);
    void add_hex(unsigned long long v, const char *label = 0, ...);
    void add_double(double v, const char *label = 0, ...);
    void add_str(const char *s, const char *label = 0, ...);
    void add_mem(const void *data, size_t size, const char *label = 0, ...);
    void add_bitmap(BITMAP *bmp, const char *label = 0, ...);
    void end();
    int count() const { return count_; }
private:
    Seq(const Seq &);
    Seq &operator=(const Seq &);
    void push(const std::string &value, const char *label, va_list *ap);
    Ctx &ctx_;
    std::string case_;
    Hash hash_;
    int count_;
    bool done_;
};

/* ------------------------------------------------------------------------- */
/* Registration.                                                             */
/* ------------------------------------------------------------------------- */

typedef void (*TestFn)(Ctx &ctx);
typedef void (*HookFn)();

enum HookPhase
{
    HOOK_PRE_INIT,                     /* before install_allegro */
    HOOK_POST_INIT                     /* after install_allegro + set_color_depth */
};

struct Registrar
{
    Registrar(const char *name, TestFn fn, const char *file);
};

struct HookRegistrar
{
    HookRegistrar(HookPhase phase, HookFn fn);
};

/* Default system for install_allegro: SYSTEM_NONE normally; SYSTEM_AUTODETECT
 * for *_sdl tests (CMake defines CONF_SDL_TEST). --system overrides it. */
struct DefaultSystem
{
    explicit DefaultSystem(int system_id);
};

/* Name of the side this program was built for ("ref" or "port"). Never print
 * it in a check line. */
inline const char *side() { return CONF_SIDE; }

} /* namespace conf */

#define CONF_TEST(test_name)                                                   \
    static void conf_test_fn_##test_name(conf::Ctx &ctx);                     \
    static conf::Registrar conf_test_reg_##test_name(#test_name,              \
        conf_test_fn_##test_name, __FILE__);                                  \
    static void conf_test_fn_##test_name(conf::Ctx &ctx)

#define CONF_PRE_INIT(hook_name)                                               \
    static void conf_hook_fn_##hook_name();                                   \
    static conf::HookRegistrar conf_hook_reg_##hook_name(conf::HOOK_PRE_INIT, \
        conf_hook_fn_##hook_name);                                            \
    static void conf_hook_fn_##hook_name()

#define CONF_POST_INIT(hook_name)                                              \
    static void conf_hook_fn_##hook_name();                                   \
    static conf::HookRegistrar conf_hook_reg_##hook_name(conf::HOOK_POST_INIT,\
        conf_hook_fn_##hook_name);                                            \
    static void conf_hook_fn_##hook_name()

#ifdef CONF_SDL_TEST
static conf::DefaultSystem conf_default_system_sdl_(SYSTEM_AUTODETECT);
#endif

#endif /* ZC_CONFORMANCE_CONF_H */

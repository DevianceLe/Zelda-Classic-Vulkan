/*
 * conf.cpp - conformance framework implementation (hashing, RNG, checks).
 *
 * Compiled once per side. It touches Allegro only through BITMAP::w, h,
 * line[] and bitmap_color_depth(), so it builds against any port that has
 * those.
 */
#include "conf.h"
#include "conf_internal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define CONF_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define CONF_MKDIR(p) mkdir((p), 0777)
#endif

namespace conf
{

/* ------------------------------------------------------------------------- */
/* Framework state.                                                          */
/* ------------------------------------------------------------------------- */

namespace detail
{

Options &options()
{
    static Options o;
    return o;
}

std::vector<TestEntry> &tests()
{
    static std::vector<TestEntry> t;
    return t;
}

std::vector<HookEntry> &hooks()
{
    static std::vector<HookEntry> h;
    return h;
}

int &default_system()
{
    static int s = 0;
    return s;
}

bool &default_system_set()
{
    static bool b = false;
    return b;
}

void write_line(const std::string &line)
{
    Options &o = options();

    if(o.out)
    {
        fputs(line.c_str(), o.out);
        fputc('\n', o.out);
        fflush(o.out);
    }

    if(o.echo)
    {
        fputs(line.c_str(), stdout);
        fputc('\n', stdout);
        fflush(stdout);
    }
}

bool make_dirs(const std::string &path)
{
    if(path.empty())
        return false;

    std::string p = path;

    for(size_t i = 0; i < p.size(); ++i)
        if(p[i] == '\\')
            p[i] = '/';

    for(size_t i = 1; i <= p.size(); ++i)
    {
        if(i == p.size() || p[i] == '/')
        {
            std::string part = p.substr(0, i);

            /* skip "C:" */
            if(part.size() == 2 && part[1] == ':')
                continue;

            CONF_MKDIR(part.c_str());
        }
    }

    std::string probe = p + "/.conf_probe";
    FILE *f = fopen(probe.c_str(), "wb");

    if(!f)
        return false;

    fclose(f);
    remove(probe.c_str());
    return true;
}

} /* namespace detail */

/* ------------------------------------------------------------------------- */
/* Hashing.                                                                  */
/* ------------------------------------------------------------------------- */

static const u64 FNV_OFFSET = 14695981039346656037ULL;
static const u64 FNV_PRIME = 1099511628211ULL;

Hash::Hash() : h_(FNV_OFFSET)
{
}

void Hash::add_bytes(const void *data, size_t size)
{
    const unsigned char *p = (const unsigned char *)data;
    u64 h = h_;

    for(size_t i = 0; i < size; ++i)
    {
        h ^= p[i];
        h *= FNV_PRIME;
    }

    h_ = h;
}

void Hash::add_u8(unsigned v)
{
    unsigned char b = (unsigned char)v;
    add_bytes(&b, 1);
}

void Hash::add_u16(unsigned v)
{
    unsigned char b[2];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    add_bytes(b, 2);
}

void Hash::add_u32(u32 v)
{
    unsigned char b[4];

    for(int i = 0; i < 4; ++i)
        b[i] = (unsigned char)((v >> (8 * i)) & 0xFF);

    add_bytes(b, 4);
}

void Hash::add_u64(u64 v)
{
    unsigned char b[8];

    for(int i = 0; i < 8; ++i)
        b[i] = (unsigned char)((v >> (8 * i)) & 0xFF);

    add_bytes(b, 8);
}

void Hash::add_i32(int v)
{
    add_u32((u32)v);
}

void Hash::add_str(const char *s)
{
    if(!s)
    {
        add_u8(0xFF);
        add_u8(0x00);
        return;
    }

    size_t n = strlen(s);
    add_u32((u32)n);
    add_bytes(s, n);
}

u64 hash_bytes(const void *data, size_t size)
{
    Hash h;
    h.add_bytes(data, size);
    return h.value();
}

u64 hash_string(const char *s)
{
    Hash h;
    h.add_str(s);
    return h.value();
}

int bytes_per_pixel(int color_depth)
{
    switch(color_depth)
    {
    case 8:
        return 1;
    case 15:
    case 16:
        return 2;
    case 24:
        return 3;
    case 32:
        return 4;
    }

    return 0;
}

u64 hash_bitmap(BITMAP *bmp)
{
    Hash h;

    if(!bmp)
    {
        h.add_str(0);
        return h.value();
    }

    int depth = bitmap_color_depth(bmp);
    int bpp = bytes_per_pixel(depth);
    h.add_i32(bmp->w);
    h.add_i32(bmp->h);
    h.add_i32(depth);

    if(bpp > 0 && bmp->w > 0)
    {
        for(int y = 0; y < bmp->h; ++y)
            h.add_bytes(bmp->line[y], (size_t)bmp->w * bpp);
    }

    return h.value();
}

u64 hash_palette(const RGB *pal, int count)
{
    Hash h;

    if(!pal)
    {
        h.add_str(0);
        return h.value();
    }

    h.add_i32(count);

    for(int i = 0; i < count; ++i)
    {
        h.add_u8(pal[i].r);
        h.add_u8(pal[i].g);
        h.add_u8(pal[i].b);
    }

    return h.value();
}

std::string hash_text(u64 h)
{
    char buf[32];
    sprintf(buf, "h:%016llx", h);
    return buf;
}

static std::string vformat(const char *fmt, va_list ap)
{
    char stackbuf[512];
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, copy);
    va_end(copy);

    if(n < 0)
        return std::string(fmt);

    if((size_t)n < sizeof(stackbuf))
        return std::string(stackbuf, (size_t)n);

    std::string big((size_t)n + 1, '\0');
    vsnprintf(&big[0], big.size(), fmt, ap);
    big.resize((size_t)n);
    return big;
}

static std::string format(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string s = vformat(fmt, ap);
    va_end(ap);
    return s;
}

const char *nm(const char *fmt, ...)
{
    static char bufs[16][256];
    static int next = 0;
    char *b = bufs[next];
    next = (next + 1) & 15;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, 256, fmt, ap);
    va_end(ap);
    b[255] = 0;
    return b;
}

/* ------------------------------------------------------------------------- */
/* RNG: xorshift64* (Marsaglia xorshift, Vigna's multiplier).                */
/* ------------------------------------------------------------------------- */

Rng::Rng(u64 seed_value)
{
    seed(seed_value);
}

void Rng::seed(u64 seed_value)
{
    /* never let the state be 0 */
    s_ = seed_value ? seed_value : 0x9E3779B97F4A7C15ULL;
}

u64 Rng::next64()
{
    s_ ^= s_ >> 12;
    s_ ^= s_ << 25;
    s_ ^= s_ >> 27;
    return s_ * 2685821657736338717ULL;
}

u32 Rng::next()
{
    return (u32)(next64() >> 32);
}

int Rng::range(int lo, int hi)
{
    if(hi <= lo)
        return lo;

    u64 span = (u64)((long long)hi - (long long)lo) + 1;
    return (int)((long long)lo + (long long)(next64() % span));
}

int Rng::bits(int n)
{
    if(n <= 0)
        return 0;

    u32 v = next();

    if(n >= 32)
        return (int)v;

    return (int)(v >> (32 - n));
}

void Rng::fill(void *data, size_t size)
{
    unsigned char *p = (unsigned char *)data;

    while(size)
    {
        u64 v = next64();

        for(int i = 0; i < 8 && size; ++i, --size)
        {
            *p++ = (unsigned char)(v & 0xFF);
            v >>= 8;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Ctx.                                                                      */
/* ------------------------------------------------------------------------- */

static u64 seed_for(const char *test_name)
{
    Hash h;
    h.add_str("conformance-seed");
    h.add_str(test_name);
    return h.value();
}

Ctx::Ctx(const char *test_name) : rng(seed_for(test_name)), name_(test_name), checks_(0)
{
}

void Ctx::emit(const char *c, const std::string &value)
{
    std::string line(name_);
    line += '/';
    line += c ? c : "(null)";
    line += ": ";
    line += value;
    detail::write_line(line);
    ++checks_;
    ++detail::options().total_checks;
}

static std::string sanitize(const std::string &s)
{
    std::string r;

    for(size_t i = 0; i < s.size(); ++i)
    {
        char ch = s[i];

        if((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-' || ch == '.')
            r += ch;
        else if(ch == '/')
            r += "__";
        else
            r += '_';
    }

    if(r.size() > 150)
    {
        /* keep names short but unique */
        std::string tail = hash_text(hash_bytes(s.data(), s.size())).substr(2);
        r = r.substr(0, 130) + "_" + tail;
    }

    return r;
}

void Ctx::dump(const char *c, const char *kind, const void *head, size_t head_size,
               const void *data, size_t size)
{
    detail::Options &o = detail::options();

    if(o.dump_dir.empty())
        return;

    std::string full_name = std::string(name_) + "/" + (c ? c : "(null)");
    std::string path = o.dump_dir + "/" + sanitize(full_name) + "." + kind;
    FILE *f = fopen(path.c_str(), "wb");

    if(!f)
        return;

    if(head_size)
        fwrite(head, 1, head_size, f);

    if(size)
        fwrite(data, 1, size, f);

    fclose(f);
}

void Ctx::check_int(const char *c, long long v)
{
    emit(c, format("%lld", v));
}

void Ctx::check_hex(const char *c, unsigned long long v)
{
    emit(c, format("0x%08llx", v));
}

void Ctx::check_bool(const char *c, bool v)
{
    emit(c, v ? "true" : "false");
}

static std::string double_text(double v)
{
    u64 bits;
    memcpy(&bits, &v, sizeof(bits));
    return format("%.17g (0x%016llx)", v, bits);
}

void Ctx::check_double(const char *c, double v)
{
    emit(c, double_text(v));
}

static std::string quote(const char *s)
{
    if(!s)
        return "(null)";

    const size_t limit = 160;
    size_t n = strlen(s);
    std::string r = "\"";

    for(size_t i = 0; i < n && i < limit; ++i)
    {
        unsigned char ch = (unsigned char)s[i];

        if(ch == '\\' || ch == '"')
        {
            r += '\\';
            r += (char)ch;
        }
        else if(ch >= 0x20 && ch < 0x7F)
            r += (char)ch;
        else
            r += format("\\x%02x", ch);
    }

    r += '"';

    if(n > limit)
        r += format("... len=%u %s", (unsigned)n, hash_text(hash_bytes(s, n)).c_str());

    return r;
}

void Ctx::check_str(const char *c, const char *s)
{
    emit(c, quote(s));
}

void Ctx::check_hash(const char *c, u64 h)
{
    emit(c, hash_text(h));
}

void Ctx::check_mem(const char *c, const void *data, size_t size)
{
    if(!data && size)
    {
        emit(c, "(null)");
        return;
    }

    emit(c, format("mem %u %s", (unsigned)size, hash_text(hash_bytes(data, size)).c_str()));
    dump(c, "mem", 0, 0, data, size);
}

static std::string bitmap_text(BITMAP *bmp)
{
    if(!bmp)
        return "(null)";

    return format("bmp %dx%dx%d %s", bmp->w, bmp->h, bitmap_color_depth(bmp),
                  hash_text(hash_bitmap(bmp)).c_str());
}

void Ctx::check_bitmap(const char *c, BITMAP *bmp)
{
    emit(c, bitmap_text(bmp));

    if(!bmp || detail::options().dump_dir.empty())
        return;

    /* Dump: text header, then the hashed row bytes. */
    int depth = bitmap_color_depth(bmp);
    int bpp = bytes_per_pixel(depth);
    std::string head = format("CONFBMP %d %d %d\n", bmp->w, bmp->h, depth);
    std::string rows;

    if(bpp > 0 && bmp->w > 0)
    {
        size_t row = (size_t)bmp->w * bpp;
        rows.reserve(row * (bmp->h > 0 ? bmp->h : 0));

        for(int y = 0; y < bmp->h; ++y)
            rows.append((const char *)bmp->line[y], row);
    }

    dump(c, "bmp", head.data(), head.size(), rows.data(), rows.size());
}

void Ctx::check_palette(const char *c, const RGB *pal, int count)
{
    if(!pal)
    {
        emit(c, "(null)");
        return;
    }

    emit(c, format("pal %d %s", count, hash_text(hash_palette(pal, count)).c_str()));

    if(!detail::options().dump_dir.empty() && count > 0)
    {
        std::string bytes;

        for(int i = 0; i < count; ++i)
        {
            bytes += (char)pal[i].r;
            bytes += (char)pal[i].g;
            bytes += (char)pal[i].b;
        }

        dump(c, "pal", 0, 0, bytes.data(), bytes.size());
    }
}

void Ctx::check_file(const char *c, const char *path)
{
    FILE *f = path ? fopen(path, "rb") : 0;

    if(!f)
    {
        emit(c, "file (missing)");
        return;
    }

    std::string bytes;
    char buf[65536];
    size_t n;

    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
        bytes.append(buf, n);

    fclose(f);
    emit(c, format("file %u %s", (unsigned)bytes.size(),
                   hash_text(hash_bytes(bytes.data(), bytes.size())).c_str()));
    dump(c, "mem", 0, 0, bytes.data(), bytes.size());
}

void Ctx::checkf(const char *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string v = vformat(fmt, ap);
    va_end(ap);
    emit(c, v);
}

void Ctx::expect(const char *c, bool ok, const char *fmt, ...)
{
    if(ok)
    {
        emit(c, "ok");
        return;
    }

    std::string msg = "FAIL";

    if(fmt)
    {
        va_list ap;
        va_start(ap, fmt);
        msg += " ";
        msg += vformat(fmt, ap);
        va_end(ap);
    }

    emit(c, msg);
}

void Ctx::fail(const char *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string msg = "FAIL " + vformat(fmt, ap);
    va_end(ap);
    emit(c, msg);
}

void Ctx::skip(const char *reason_fmt, ...)
{
    va_list ap;
    va_start(ap, reason_fmt);
    std::string msg = "SKIP " + vformat(reason_fmt, ap);
    va_end(ap);
    emit("skipped", msg);
}

void Ctx::note(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string msg = "# " + std::string(name_) + ": " + vformat(fmt, ap);
    va_end(ap);
    detail::write_line(msg);
}

static std::string join(const std::string &dir, const char *file)
{
    if(!file || !*file)
        return dir;

    if(dir.empty())
        return file;

    char last = dir[dir.size() - 1];

    if(last == '/' || last == '\\')
        return dir + file;

    return dir + "/" + file;
}

std::string Ctx::data(const char *file) const
{
    return join(detail::options().data_dir, file);
}

std::string Ctx::quest(const char *file) const
{
    return join(detail::options().quests_dir, file);
}

std::string Ctx::tmp(const char *file) const
{
    return join(detail::options().tmp_dir, file);
}

static bool readable(const std::string &p)
{
    FILE *f = fopen(p.c_str(), "rb");

    if(!f)
        return false;

    fclose(f);
    return true;
}

bool Ctx::have_quest(const char *file) const
{
    return readable(quest(file));
}

bool Ctx::have_data(const char *file) const
{
    std::string p = data(file);
    FILE *f = fopen(p.c_str(), "rb");

    if(!f)
        return false;

    fclose(f);
    return true;
}

bool Ctx::expanding() const
{
    return detail::options().expand;
}

/* ------------------------------------------------------------------------- */
/* Seq.                                                                      */
/* ------------------------------------------------------------------------- */

Seq::Seq(Ctx &ctx, const char *c) : ctx_(ctx), case_(c ? c : "(null)"), count_(0), done_(false)
{
}

Seq::~Seq()
{
    end();
}

void Seq::push(const std::string &value, const char *label, va_list *ap)
{
    hash_.add_str(value.c_str());

    if(ctx_.expanding())
    {
        std::string v = value;

        if(label && ap)
        {
            v += "  ; ";
            v += vformat(label, *ap);
        }

        std::string c = case_ + format("[%d]", count_);
        detail::write_line(std::string(ctx_.name()) + "/" + c + ": " + v);
    }

    ++count_;
}

#define CONF_SEQ_PUSH(value_expr)                                              \
    do                                                                         \
    {                                                                          \
        if(label && ctx_.expanding())                                          \
        {                                                                      \
            va_list ap;                                                        \
            va_start(ap, label);                                               \
            push((value_expr), label, &ap);                                    \
            va_end(ap);                                                        \
        }                                                                      \
        else                                                                   \
            push((value_expr), 0, 0);                                          \
    } while(0)

void Seq::add_int(long long v, const char *label, ...)
{
    CONF_SEQ_PUSH(format("%lld", v));
}

void Seq::add_hex(unsigned long long v, const char *label, ...)
{
    CONF_SEQ_PUSH(format("0x%08llx", v));
}

void Seq::add_double(double v, const char *label, ...)
{
    CONF_SEQ_PUSH(double_text(v));
}

void Seq::add_str(const char *s, const char *label, ...)
{
    CONF_SEQ_PUSH(quote(s));
}

void Seq::add_mem(const void *data, size_t size, const char *label, ...)
{
    CONF_SEQ_PUSH(format("mem %u %s", (unsigned)size, hash_text(hash_bytes(data, size)).c_str()));
}

void Seq::add_bitmap(BITMAP *bmp, const char *label, ...)
{
    CONF_SEQ_PUSH(bitmap_text(bmp));
}

void Seq::end()
{
    if(done_)
        return;

    done_ = true;
    ctx_.emit(case_.c_str(), format("seq n=%d %s", count_, hash_text(hash_.value()).c_str()));
}

/* ------------------------------------------------------------------------- */
/* Registration.                                                             */
/* ------------------------------------------------------------------------- */

Registrar::Registrar(const char *name, TestFn fn, const char *file)
{
    detail::TestEntry e;
    e.name = name;
    e.fn = fn;
    e.file = file;
    detail::tests().push_back(e);
}

HookRegistrar::HookRegistrar(HookPhase phase, HookFn fn)
{
    detail::HookEntry e;
    e.phase = phase;
    e.fn = fn;
    detail::hooks().push_back(e);
}

DefaultSystem::DefaultSystem(int system_id)
{
    detail::default_system() = system_id;
    detail::default_system_set() = true;
}

} /* namespace conf */

extern "C" void conf_crash_note(const char *what, unsigned long code)
{
    conf::detail::Options &o = conf::detail::options();
    char buf[256];
    sprintf(buf, "# CRASH in test '%s': %s (code 0x%08lx)", o.current_test.c_str(),
            what ? what : "?", code);

    if(o.out)
    {
        fputs(buf, o.out);
        fputc('\n', o.out);
        fflush(o.out);
    }

    fputs(buf, stderr);
    fputc('\n', stderr);
    fflush(stderr);
    fflush(stdout);
}

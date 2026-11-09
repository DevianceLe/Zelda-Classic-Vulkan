/*
 * test_files_pack.cpp - PACKFILE I/O must match the reference byte for byte:
 * the files it writes (raw, LZSS-packed, "slh." no-pack, password-scrambled),
 * how it reads them back (values, EOF/error indicators, errno), pack_fseek,
 * the 16/32-bit helpers, chunks, pack_fgets/pack_fputs/pack_ungetc, the
 * "file.dat#OBJECT" syntax and the LZSS functions.
 *
 * Files written by both sides hash identically, so whatever the port writes
 * (save games) the reference reads, and vice versa.
 */
#include "conf.h"
#include "conf_zc.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

namespace
{

typedef std::vector<unsigned char> Bytes;

/* Deterministic test content of a few kinds. */
Bytes make_bytes(conf::Rng &rng, int n, int style)
{
    static const char text[] = "Zelda Classic quest data: tiles, combos, maps, dmaps, strings. ";
    Bytes b((size_t)n);

    for(int i = 0; i < n; ++i)
    {
        switch(style)
        {
        case 0: b[i] = (unsigned char)rng.bits(8); break;                     /* noise */
        case 1: b[i] = (unsigned char)text[(i * 7 / 5 + i / 300) % (sizeof(text) - 1)]; break;
        case 2: b[i] = (unsigned char)((i / (1 + (i % 23))) & 3); break;      /* runs */
        case 3: b[i] = 0; break;
        case 4: b[i] = (unsigned char)(rng.range(0, 9) < 7 ? 0 : rng.bits(8)); break; /* sparse */
        default: b[i] = (unsigned char)(i & 0xFF); break;
        }
    }

    return b;
}

/* Writes b with a random mix of pack_putc / pack_fwrite / word writes. */
void write_mixed(PACKFILE *f, const Bytes &b, conf::Rng &rng)
{
    size_t i = 0;

    while(i < b.size())
    {
        size_t left = b.size() - i;
        int op = rng.range(0, 5);

        if(op == 0 || left < 4)
        {
            pack_putc(b[i], f);
            ++i;
        }
        else if(op == 1)
        {
            pack_iputw(b[i] | (b[i + 1] << 8), f);
            i += 2;
        }
        else if(op == 2)
        {
            pack_mputl((long)(((unsigned long)b[i] << 24) | ((unsigned long)b[i + 1] << 16) | ((unsigned long)b[i + 2] << 8) | b[i + 3]), f);
            i += 4;
        }
        else
        {
            size_t n = (size_t)rng.range(1, 6000);

            if(n > left)
                n = left;

            pack_fwrite(&b[i], (long)n, f);
            i += n;
        }
    }
}

bool write_file(const std::string &fn, const char *mode, const char *pw, const Bytes &b, conf::Rng &rng)
{
    packfile_password(pw);
    PACKFILE *f = pack_fopen(fn.c_str(), mode);
    packfile_password(NULL);

    if(!f)
        return false;

    write_mixed(f, b, rng);
    pack_fclose(f);
    return true;
}

/* Reads everything with pack_getc; hashes value, count and the final feof. */
std::string read_all(const std::string &fn, const char *mode, const char *pw)
{
    packfile_password(pw);
    errno = 0;
    PACKFILE *f = pack_fopen(fn.c_str(), mode);
    int err = errno;
    packfile_password(NULL);

    if(!f)
        return std::string(conf::nm("NULL errno=%d", err));

    conf::Hash h;
    long n = 0;
    int c;

    while((c = pack_getc(f)) != EOF)
    {
        h.add_u8((unsigned)c);
        ++n;
    }

    int eof = pack_feof(f) ? 1 : 0;
    int ferr = pack_ferror(f) ? 1 : 0;
    pack_fclose(f);
    return std::string(conf::nm("n=%ld %s eof=%d err=%d", n, conf::hash_text(h.value()).c_str(), eof, ferr));
}

const char *g_passwords[] = { "", "a", "ab", "abc", "abcd", "abcde", "longtan", "Z", "\xff\x80q", "pass word 1234567" };
const int g_npasswords = sizeof(g_passwords) / sizeof(g_passwords[0]);

const char *g_wmodes[] = { F_WRITE, F_WRITE_PACKED, F_WRITE_NOPACK };
const char *g_wnames[] = { "w", "wp", "wn" };

} /* namespace */

/* Every write mode with every password: the written files must be identical
 * to the reference's, and read back the same in every read mode. */
CONF_TEST(pack_write_modes)
{
    std::string fn = ctx.tmp("pack_modes.bin");

    for(int p = 0; p < g_npasswords; ++p)
        for(int m = 0; m < 3; ++m)
        {
            Bytes b = make_bytes(ctx.rng, 1000 + 300 * m + p, (p + m) % 6);

            if(!write_file(fn, g_wmodes[m], g_passwords[p], b, ctx.rng))
            {
                ctx.fail(conf::nm("open_%s_%d", g_wnames[m], p), "pack_fopen failed");
                continue;
            }

            ctx.check_file(conf::nm("file_%s_%d", g_wnames[m], p), fn.c_str());
            ctx.checkf(conf::nm("data_%s_%d", g_wnames[m], p), "%s", conf::hash_text(conf::hash_bytes(&b[0], b.size())).c_str());
            ctx.checkf(conf::nm("r_%s_%d", g_wnames[m], p), "%s", read_all(fn, F_READ, g_passwords[p]).c_str());
            ctx.checkf(conf::nm("rp_%s_%d", g_wnames[m], p), "%s", read_all(fn, F_READ_PACKED, g_passwords[p]).c_str());
            /* other password */
            ctx.checkf(conf::nm("rp_other_%s_%d", g_wnames[m], p), "%s", read_all(fn, F_READ_PACKED, g_passwords[(p + 3) % g_npasswords]).c_str());
            ctx.checkf(conf::nm("r_none_%s_%d", g_wnames[m], p), "%s", read_all(fn, F_READ, NULL).c_str());
        }

    delete_file(fn.c_str());
}

/* Random passwords of every length 1..40 (masks and key cycling). */
CONF_TEST(pack_password_lengths)
{
    std::string fn = ctx.tmp("pack_pw.bin");

    for(int len = 1; len <= 40; ++len)
    {
        char pw[64];

        for(int i = 0; i < len; ++i)
            pw[i] = (char)ctx.rng.range(1, 255);

        pw[len] = 0;

        for(int m = 0; m < 3; ++m)
        {
            Bytes b = make_bytes(ctx.rng, 64 + len, 1);
            write_file(fn, g_wmodes[m], pw, b, ctx.rng);
            ctx.check_file(conf::nm("file_%s_%d", g_wnames[m], len), fn.c_str());
            ctx.checkf(conf::nm("rp_%s_%d", g_wnames[m], len), "%s", read_all(fn, m == 0 ? F_READ : F_READ_PACKED, pw).c_str());
        }
    }

    delete_file(fn.c_str());
}

/* The LZSS encoder must produce the reference's bytes for any input. */
CONF_TEST(pack_lzss_identity)
{
    static const int sizes[] = { 0, 1, 2, 3, 4, 17, 18, 19, 20, 35, 36, 37, 100, 4077, 4078, 4079,
                                 4095, 4096, 4097, 4113, 8191, 8192, 8193, 12288, 20000, 70001 };
    std::string fn = ctx.tmp("pack_lzss.bin");
    int k = 0;

    for(size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        for(int style = 0; style < 6; ++style, ++k)
        {
            Bytes b = make_bytes(ctx.rng, sizes[s], style);
            write_file(fn, F_WRITE_PACKED, NULL, b, ctx.rng);
            ctx.check_file(conf::nm("file_%d_%d", sizes[s], style), fn.c_str());
            ctx.checkf(conf::nm("read_%d_%d", sizes[s], style), "%s", read_all(fn, F_READ_PACKED, NULL).c_str());
        }

    /* Same data, written one byte at a time and in one call. */
    Bytes big = make_bytes(ctx.rng, 50000, 1);

    for(size_t i = 0; i < big.size(); i += 97)
        big[i] = (unsigned char)ctx.rng.bits(8);

    packfile_password(NULL);
    PACKFILE *f = pack_fopen(fn.c_str(), F_WRITE_PACKED);

    for(size_t i = 0; i < big.size(); ++i)
        pack_putc(big[i], f);

    pack_fclose(f);
    ctx.check_file("bytewise", fn.c_str());
    f = pack_fopen(fn.c_str(), F_WRITE_PACKED);
    pack_fwrite(&big[0], (long)big.size(), f);
    pack_fclose(f);
    ctx.check_file("oneshot", fn.c_str());

    /* Long periodic patterns (matches of every distance). */
    for(int period = 1; period <= 4100; period = period * 3 + 1)
    {
        Bytes p = make_bytes(ctx.rng, period, 0);
        Bytes b;

        for(int i = 0; i < 30000; ++i)
            b.push_back(p[i % period]);

        write_file(fn, F_WRITE_PACKED, "longtan", b, ctx.rng);
        ctx.check_file(conf::nm("period_%d", period), fn.c_str());
    }

    delete_file(fn.c_str());
}

/* pack_getc / pack_feof / pack_ferror around the end of files of many sizes. */
CONF_TEST(pack_read_eof)
{
    static const int sizes[] = { 0, 1, 2, 3, 4, 5, 8, 4095, 4096, 4097, 8191, 8192, 8193 };
    static const char *rmodes[] = { F_READ, F_READ_PACKED };
    std::string fn = ctx.tmp("pack_eof.bin");

    for(size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        for(int m = 0; m < 3; ++m)
            for(int r = 0; r < 2; ++r)
            {
                Bytes b = make_bytes(ctx.rng, sizes[s], 5);

                if(!write_file(fn, g_wmodes[m], "abc", b, ctx.rng))
                    continue;

                packfile_password("abc");
                errno = 0;
                PACKFILE *f = pack_fopen(fn.c_str(), rmodes[r]);
                int err = errno;
                packfile_password(NULL);
                const char *cn = conf::nm("getc_%d_%s_%s", sizes[s], g_wnames[m], r ? "rp" : "r");

                if(!f)
                {
                    ctx.checkf(cn, "NULL errno=%d", err);
                    continue;
                }

                conf::Seq q(ctx, cn);
                q.add_int(pack_feof(f), "feof before");

                for(int i = 0; i < sizes[s] + 12; ++i)
                {
                    int c = pack_getc(f);
                    q.add_int(c, "getc %d", i);
                    q.add_int(pack_feof(f) ? 1 : 0, "feof %d", i);
                    q.add_int(pack_ferror(f) ? 1 : 0, "ferror %d", i);
                }

                q.end();
                pack_fclose(f);
            }

    /* pack_fread in pieces */
    static const int pieces[] = { 1, 7, 100, 4095, 4096, 5000, 9000 };

    for(int m = 0; m < 3; ++m)
        for(size_t pc = 0; pc < sizeof(pieces) / sizeof(pieces[0]); ++pc)
        {
            Bytes b = make_bytes(ctx.rng, 10001, 1);
            write_file(fn, g_wmodes[m], NULL, b, ctx.rng);
            PACKFILE *f = pack_fopen(fn.c_str(), m == 0 ? F_READ : F_READ_PACKED);
            conf::Seq q(ctx, conf::nm("fread_%s_%d", g_wnames[m], pieces[pc]));
            Bytes buf((size_t)pieces[pc]);

            for(int i = 0; i < 12000 / pieces[pc] + 3; ++i)
            {
                long got = pack_fread(&buf[0], pieces[pc], f);
                q.add_int(got, "fread %d", i);
                q.add_mem(&buf[0], (size_t)(got > 0 ? got : 0), "data %d", i);
                q.add_int(pack_feof(f) ? 1 : 0, "feof %d", i);
                q.add_int(pack_ferror(f) ? 1 : 0, "ferror %d", i);
            }

            q.end();
            pack_fclose(f);
        }

    delete_file(fn.c_str());
}

/* 16/32-bit helpers: return values when writing, values when reading, and
 * what they return when the file ends in the middle of a value. */
CONF_TEST(pack_words)
{
    static const long vals[] = { 0, 1, -1, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000,
                                 0x12345, -0x8000, -0x8001, 0x7FFFFFFFL, (long)0x80000000UL,
                                 (long)0xFFFFFFFFUL, 0x01020304L, (long)0xFEDCBA98UL };
    std::string fn = ctx.tmp("pack_words.bin");
    const int n = (int)(sizeof(vals) / sizeof(vals[0]));

    for(int m = 0; m < 3; ++m)
    {
        packfile_password(m == 1 ? "abc" : NULL);
        PACKFILE *f = pack_fopen(fn.c_str(), g_wmodes[m]);
        packfile_password(NULL);
        conf::Seq w(ctx, conf::nm("put_%s", g_wnames[m]));

        for(int i = 0; i < n; ++i)
        {
            w.add_int(pack_iputw((int)vals[i], f), "iputw %ld", vals[i]);
            w.add_int(pack_mputw((int)vals[i], f), "mputw %ld", vals[i]);
            w.add_int(pack_iputl(vals[i], f), "iputl %ld", vals[i]);
            w.add_int(pack_mputl(vals[i], f), "mputl %ld", vals[i]);
            w.add_int(pack_putc((int)vals[i], f), "putc %ld", vals[i]);
        }

        w.end();
        pack_fclose(f);
        ctx.check_file(conf::nm("file_%s", g_wnames[m]), fn.c_str());

        packfile_password(m == 1 ? "abc" : NULL);
        f = pack_fopen(fn.c_str(), m == 0 ? F_READ : F_READ_PACKED);
        packfile_password(NULL);
        conf::Seq r(ctx, conf::nm("get_%s", g_wnames[m]));

        for(int i = 0; i < n; ++i)
        {
            r.add_int(pack_igetw(f), "igetw %ld", vals[i]);
            r.add_int(pack_mgetw(f), "mgetw %ld", vals[i]);
            r.add_int(pack_igetl(f), "igetl %ld", vals[i]);
            r.add_int(pack_mgetl(f), "mgetl %ld", vals[i]);
            r.add_int(pack_getc(f), "getc %ld", vals[i]);
        }

        r.add_int(pack_igetw(f), "igetw at eof");
        r.add_int(pack_mgetl(f), "mgetl at eof");
        r.end();
        pack_fclose(f);
    }

    /* files of 1..7 bytes read with each helper */
    for(int len = 0; len <= 7; ++len)
    {
        Bytes b = make_bytes(ctx.rng, len, 0);
        conf::Seq q(ctx, conf::nm("partial_%d", len));

        for(int fn_i = 0; fn_i < 4; ++fn_i)
        {
            write_file(fn, F_WRITE, NULL, b, ctx.rng);
            PACKFILE *f = pack_fopen(fn.c_str(), F_READ);

            for(int k = 0; k < 3; ++k)
            {
                long v = 0;

                switch(fn_i)
                {
                case 0: v = pack_igetw(f); break;
                case 1: v = pack_mgetw(f); break;
                case 2: v = pack_igetl(f); break;
                default: v = pack_mgetl(f); break;
                }

                q.add_int(v, "fn %d call %d", fn_i, k);
                q.add_int(pack_feof(f) ? 1 : 0, "feof fn %d call %d", fn_i, k);
                q.add_int(pack_ferror(f) ? 1 : 0, "ferror fn %d call %d", fn_i, k);
            }

            pack_fclose(f);
        }

        q.end();
    }

    delete_file(fn.c_str());
}

/* pack_fseek: forward skips in raw and packed files, past the end, after
 * reads, and on files opened for writing. */
CONF_TEST(pack_fseek)
{
    static const int offs[] = { 0, 1, 2, 100, 4095, 4096, 4097, 5000, 9999, 10000, 10001, 20000 };
    std::string fn = ctx.tmp("pack_seek.bin");

    for(int m = 0; m < 3; ++m)
    {
        Bytes b = make_bytes(ctx.rng, 10000, 0);
        write_file(fn, g_wmodes[m], m == 2 ? "longtan" : NULL, b, ctx.rng);
        conf::Seq q(ctx, conf::nm("seek_%s", g_wnames[m]));

        for(size_t pre = 0; pre < 3; ++pre)
            for(size_t o = 0; o < sizeof(offs) / sizeof(offs[0]); ++o)
            {
                packfile_password(m == 2 ? "longtan" : NULL);
                PACKFILE *f = pack_fopen(fn.c_str(), m == 0 ? F_READ : F_READ_PACKED);
                packfile_password(NULL);
                int prefix = pre == 0 ? 0 : pre == 1 ? 3 : 4100;

                for(int i = 0; i < prefix; ++i)
                    pack_getc(f);

                errno = 0;
                int ret = pack_fseek(f, offs[o]);
                q.add_int(ret, "ret pre=%d off=%d", prefix, offs[o]);
                q.add_int(errno, "errno pre=%d off=%d", prefix, offs[o]);
                q.add_int(pack_feof(f) ? 1 : 0, "feof pre=%d off=%d", prefix, offs[o]);
                q.add_int(pack_ferror(f) ? 1 : 0, "ferror pre=%d off=%d", prefix, offs[o]);
                q.add_int(pack_getc(f), "next pre=%d off=%d", prefix, offs[o]);
                q.add_int(pack_getc(f), "next2 pre=%d off=%d", prefix, offs[o]);
                /* consecutive seeks */
                q.add_int(pack_fseek(f, 7), "seek7 pre=%d off=%d", prefix, offs[o]);
                q.add_int(pack_getc(f), "next3 pre=%d off=%d", prefix, offs[o]);
                pack_fclose(f);
            }

        q.end();
    }

    /* seeking a file opened for writing */
    PACKFILE *f = pack_fopen(fn.c_str(), F_WRITE);
    pack_fwrite("abcdef", 6, f);
    errno = 0;
    ctx.check_int("seek_write_ret", pack_fseek(f, 2));
    ctx.check_int("seek_write_errno", errno);
    pack_fwrite("gh", 2, f);
    pack_fclose(f);
    ctx.check_file("seek_write_file", fn.c_str());
    delete_file(fn.c_str());
}

/* errno, flags and return values of pack_fopen / pack_fclose. */
CONF_TEST(pack_open_errors)
{
    std::string fn = ctx.tmp("pack_err.bin");
    std::string missing = ctx.tmp("pack_missing_file.bin");
    std::string nodir = ctx.tmp("no_such_dir/x.bin");
    delete_file(missing.c_str());

    static const char *modes[] = { "r", "rp", "w", "wp", "w!", "R", "W", "rb", "wb", "RP", "WP", "pr", "pw", "!w" };
    const int nm_ = (int)(sizeof(modes) / sizeof(modes[0]));

    for(int i = 0; i < nm_; ++i)
    {
        Bytes b = make_bytes(ctx.rng, 50, 1);
        write_file(fn, F_WRITE_PACKED, NULL, b, ctx.rng);
        errno = 12345;
        PACKFILE *f = pack_fopen(fn.c_str(), modes[i]);
        int err = errno;
        const char *cn = conf::nm("mode_%d", i);

        if(!f)
        {
            ctx.checkf(cn, "NULL errno=%d", err);
            continue;
        }

        int wflag = (f->normal.flags & PACKFILE_FLAG_WRITE) ? 1 : 0;
        int c = wflag ? pack_putc('x', f) : pack_getc(f);
        errno = 12345;
        int cl = pack_fclose(f);
        ctx.checkf(cn, "ok errno=%d write=%d c=%d close=%d errno2=%d", err, wflag, c, cl, errno);
        ctx.check_file(conf::nm("mode_%d_file", i), fn.c_str());
    }

    errno = 0;
    ctx.checkf("missing_r", "%s errno=%d", pack_fopen(missing.c_str(), F_READ) ? "ok" : "NULL", errno);
    errno = 0;
    ctx.checkf("missing_rp", "%s errno=%d", pack_fopen(missing.c_str(), F_READ_PACKED) ? "ok" : "NULL", errno);
    errno = 0;
    ctx.checkf("nodir_w", "%s errno=%d", pack_fopen(nodir.c_str(), F_WRITE) ? "ok" : "NULL", errno);
    errno = 0;
    ctx.checkf("nodir_wp", "%s errno=%d", pack_fopen(nodir.c_str(), F_WRITE_PACKED) ? "ok" : "NULL", errno);

    /* "rp" on non-packed content of every length 0..9: EDOM or not */
    for(int len = 0; len < 10; ++len)
    {
        Bytes b = make_bytes(ctx.rng, len, 5);
        write_file(fn, F_WRITE, NULL, b, ctx.rng);
        errno = 0;
        PACKFILE *f = pack_fopen(fn.c_str(), F_READ_PACKED);
        int err = errno;
        ctx.checkf(conf::nm("rp_raw_%d", len), "%s errno=%d", f ? "ok" : "NULL", err);

        if(f)
            pack_fclose(f);

        /* exactly a magic plus len bytes */
        packfile_password(NULL);
        f = pack_fopen(fn.c_str(), F_WRITE);
        pack_mputl(len & 1 ? F_PACK_MAGIC : F_NOPACK_MAGIC, f);
        pack_fwrite(&b[0], len, f);
        pack_fclose(f);
        ctx.checkf(conf::nm("rp_magic_%d", len), "%s", read_all(fn, F_READ_PACKED, NULL).c_str());
        /* old exe-appended magic is not a packfile magic */
        f = pack_fopen(fn.c_str(), F_WRITE);
        pack_mputl(F_EXE_MAGIC, f);
        pack_fwrite(&b[0], len, f);
        pack_fclose(f);
        ctx.checkf(conf::nm("rp_exemagic_%d", len), "%s", read_all(fn, F_READ_PACKED, NULL).c_str());
    }

    /* errno after a successful open is left alone? */
    Bytes b = make_bytes(ctx.rng, 10, 1);
    write_file(fn, F_WRITE, NULL, b, ctx.rng);
    errno = 4321;
    PACKFILE *f = pack_fopen(fn.c_str(), F_READ);
    ctx.check_int("errno_after_ok_r", errno);
    pack_fclose(f);
    write_file(fn, F_WRITE_PACKED, NULL, b, ctx.rng);
    errno = 4321;
    f = pack_fopen(fn.c_str(), F_READ_PACKED);
    ctx.check_int("errno_after_ok_rp", errno);
    errno = 4321;
    pack_getc(f);
    ctx.check_int("errno_after_getc", errno);
    errno = 4321;
    ctx.check_int("close_ret", pack_fclose(f));
    ctx.check_int("errno_after_close", errno);
    errno = 4321;
    f = pack_fopen(fn.c_str(), F_WRITE);
    ctx.check_int("errno_after_ok_w", errno);
    pack_fclose(f);

    delete_file(fn.c_str());
}

/* pack_ungetc, pack_fgets, pack_fputs. */
CONF_TEST(pack_text)
{
    std::string fn = ctx.tmp("pack_text.bin");
    static const char content[] = "line one\r\nline two\nthree\rfour\r\r\n\n\nlast line without end"
                                  "\r\n0123456789012345678901234567890123456789012345678901234567890123456789\n";
    PACKFILE *f = pack_fopen(fn.c_str(), F_WRITE);
    pack_fwrite(content, (long)strlen(content), f);
    pack_fclose(f);

    static const int maxes[] = { 1, 2, 3, 5, 10, 64, 200 };

    for(size_t m = 0; m < sizeof(maxes) / sizeof(maxes[0]); ++m)
    {
        f = pack_fopen(fn.c_str(), F_READ);
        conf::Seq q(ctx, conf::nm("fgets_%d", maxes[m]));
        char buf[256];

        for(int i = 0; i < 40; ++i)
        {
            memset(buf, '#', sizeof(buf));
            buf[255] = 0;
            char *r = pack_fgets(buf, maxes[m], f);
            q.add_int(r == buf ? 1 : r == NULL ? 0 : 2, "ret %d", i);

            if(r)
                q.add_str(buf, "str %d", i);

            q.add_int(pack_feof(f) ? 1 : 0, "feof %d", i);
        }

        q.end();
        pack_fclose(f);
    }

    /* ungetc */
    f = pack_fopen(fn.c_str(), F_READ);
    conf::Seq u(ctx, "ungetc");
    u.add_int(pack_getc(f));
    u.add_int(pack_ungetc('Q', f));
    u.add_int(pack_getc(f));
    u.add_int(pack_getc(f));
    u.add_int(pack_ungetc(0x1FF, f));
    u.add_int(pack_getc(f));
    u.add_int(pack_ungetc('a', f));
    char tmp[8];
    u.add_int(pack_fread(tmp, 3, f));
    u.add_mem(tmp, 3);
    u.end();
    pack_fclose(f);

    /* fputs */
    f = pack_fopen(fn.c_str(), F_WRITE);
    ctx.check_int("fputs1", pack_fputs("hello\nworld\r\n", f));
    ctx.check_int("fputs2", pack_fputs("", f));
    ctx.check_int("fputs3", pack_fputs("a\n\nb\rc", f));
    pack_fclose(f);
    ctx.check_file("fputs_file", fn.c_str());
    delete_file(fn.c_str());
}

/* Chunks read from hand-made files: plain and LZSS-compressed, nested,
 * partially read, inside raw / packed / password files. */
CONF_TEST(pack_chunks_read)
{
    std::string fn = ctx.tmp("pack_chunk.bin");
    std::string zf = ctx.tmp("pack_chunk_z.bin");
    Bytes inner = make_bytes(ctx.rng, 3000, 1);

    /* compressed bytes of 'inner': a "wp" file without its 4 magic bytes */
    write_file(zf, F_WRITE_PACKED, NULL, inner, ctx.rng);
    Bytes z;
    {
        PACKFILE *f = pack_fopen(zf.c_str(), F_READ);
        int c;

        while((c = pack_getc(f)) != EOF)
            z.push_back((unsigned char)c);

        pack_fclose(f);
        z.erase(z.begin(), z.begin() + 4);
    }

    for(int outer = 0; outer < 3; ++outer)
        for(int packed = 0; packed < 2; ++packed)
            for(int readsz = 0; readsz < 4; ++readsz)
            {
                const char *pw = outer == 2 ? "longtan" : NULL;
                packfile_password(pw);
                PACKFILE *f = pack_fopen(fn.c_str(), g_wmodes[outer]);
                packfile_password(NULL);
                pack_fwrite("HEAD", 4, f);

                if(packed)
                {
                    pack_mputl((long)z.size(), f);
                    pack_mputl(-(long)inner.size(), f);
                    pack_fwrite(&z[0], (long)z.size(), f);
                }
                else
                {
                    pack_mputl((long)inner.size(), f);
                    pack_mputl((long)inner.size(), f);
                    pack_fwrite(&inner[0], (long)inner.size(), f);
                }

                /* a nested plain chunk */
                pack_mputl(10 + 8, f);
                pack_mputl(10 + 8, f);
                pack_mputl(10, f);
                pack_mputl(10, f);
                pack_fwrite("0123456789", 10, f);
                pack_fwrite("TAIL", 4, f);
                pack_fclose(f);

                packfile_password(pw);
                f = pack_fopen(fn.c_str(), outer == 0 ? F_READ : F_READ_PACKED);
                packfile_password(NULL);
                conf::Seq q(ctx, conf::nm("chunk_o%d_p%d_r%d", outer, packed, readsz));
                char head[5] = { 0 };
                q.add_int(pack_fread(head, 4, f), "head");
                q.add_str(head);
                PACKFILE *c = pack_fopen_chunk(f, readsz & 1);
                q.add_int(c != NULL, "chunk open");

                if(!c)
                {
                    q.end();
                    pack_fclose(f);
                    continue;
                }

                q.add_int((c->normal.flags & PACKFILE_FLAG_WRITE) ? 1 : 0, "chunk write flag");
                int want = readsz < 2 ? (int)inner.size() + 5 : 100;
                conf::Hash h;
                int n = 0, ch;

                while(n < want && (ch = pack_getc(c)) != EOF)
                {
                    h.add_u8((unsigned)ch);
                    ++n;
                }

                q.add_int(n, "read");
                q.add_hex(h.value(), "hash");
                q.add_int(pack_feof(c) ? 1 : 0, "feof");
                q.add_int(pack_ferror(c) ? 1 : 0, "ferror");
                PACKFILE *p = pack_fclose_chunk(c);
                q.add_int(p == f, "parent back");
                /* nested chunk */
                PACKFILE *c1 = pack_fopen_chunk(f, 0);
                PACKFILE *c2 = c1 ? pack_fopen_chunk(c1, 0) : NULL;

                if(c2)
                {
                    char d[16] = { 0 };
                    q.add_int(pack_fread(d, 15, c2), "nested read");
                    q.add_str(d);
                    q.add_int(pack_feof(c2) ? 1 : 0, "nested feof");
                    q.add_int(pack_fclose_chunk(c2) == c1, "nested back");
                }

                if(c1)
                    q.add_int(pack_fclose_chunk(c1) == f, "outer back");

                char tail[5] = { 0 };
                q.add_int(pack_fread(tail, 4, f), "tail");
                q.add_str(tail);
                q.add_int(pack_getc(f), "after tail");
                q.end();
                pack_fclose(f);
            }

    delete_file(fn.c_str());
    delete_file(zf.c_str());
}

/* "file.dat#OBJECT" names: objects of the real datafiles, nested names,
 * case, missing objects, file_size_ex. */
CONF_TEST(pack_dat_objects)
{
    if(!ctx.have_data("zelda.dat") || !ctx.have_data("fonts.dat") || !ctx.have_data("qst.dat"))
    {
        ctx.skip("game data not found");
        return;
    }

    struct Obj
    {
        const char *file;
        const char *obj;
        bool pwd;
    };
    static const Obj objs[] = {
        { "zelda.dat", "_SIGNATURE", false },
        { "zelda.dat", "BMP_MOUSE", false },
        { "zelda.dat", "bmp_mouse", false },
        { "zelda.dat", "PAL_GUI", false },
        { "zelda.dat", "TITLE_25", false },
        { "zelda.dat", "TITLE_25/BMP_1", false },
        { "zelda.dat", "TITLE_25/PAL_1", false },
        { "zelda.dat", "ZC_MIDI/DUNGEON", false },
        { "zelda.dat", "ZC_MIDI/NOPE", false },
        { "zelda.dat", "NOPE", false },
        { "zelda.dat", "GrabberInfo", false },
        { "zelda.dat", "", false },
        { "zelda.dat", "BMP_MOUSE/X", false },
        { "fonts.dat", "FONT_NES", true },
        { "fonts.dat", "_SIGNATURE", true },
        { "fonts.dat", "FONT_NES", false },
        { "qst.dat", "_SIGNATURE", true },
        { "qst.dat", "NESQST_NEW_QST", true },
        { "sfx.dat", "WAV_ARROW", false },
        { "sfx.dat", "Z35", false },
    };

    for(size_t i = 0; i < sizeof(objs) / sizeof(objs[0]); ++i)
    {
        if(!ctx.have_data(objs[i].file))
            continue;

        std::string name = ctx.data(objs[i].file) + "#" + objs[i].obj;
        const char *pw = objs[i].pwd ? conf::zc::datapwd() : NULL;

        packfile_password(pw);
        ctx.check_int(conf::nm("size_%d", (int)i), (long long)file_size_ex(name.c_str()));
        ctx.check_bool(conf::nm("exists_%d", (int)i), exists(name.c_str()) != 0);
        packfile_password(NULL);
        ctx.checkf(conf::nm("r_%d", (int)i), "%s", read_all(name, F_READ, pw).c_str());
        ctx.checkf(conf::nm("rp_%d", (int)i), "%s", read_all(name, F_READ_PACKED, pw).c_str());

        packfile_password(pw);
        PACKFILE *f = pack_fopen(name.c_str(), F_READ);
        packfile_password(NULL);

        if(f)
        {
            ctx.check_int(conf::nm("flags_%d", (int)i), (f->normal.flags & PACKFILE_FLAG_WRITE) ? 1 : 0);
            conf::Seq q(ctx, conf::nm("ops_%d", (int)i));
            q.add_int(pack_feof(f) ? 1 : 0);
            q.add_int(pack_mgetl(f));
            q.add_int(pack_fseek(f, 3));
            q.add_int(pack_igetw(f));
            q.add_int(pack_fseek(f, 100000000));
            q.add_int(pack_getc(f));
            q.add_int(pack_feof(f) ? 1 : 0);
            q.end();
            pack_fclose(f);
        }
    }

    /* writing to an object name: the reference opens the plain name (and
     * reports EROFS). In the scratch directory only: never write next to
     * the game data. */
    std::string wdat = ctx.tmp("pack_w.dat");
    write_file(wdat, F_WRITE_PACKED, NULL, make_bytes(ctx.rng, 20, 1), ctx.rng);
    std::string name = wdat + "#BMP_MOUSE";
    errno = 0;
    PACKFILE *f = pack_fopen(name.c_str(), F_WRITE);
    ctx.checkf("write_object", "%s errno=%d", f ? "ok" : "NULL", errno);

    if(f)
    {
        pack_putc('x', f);
        pack_fclose(f);
    }

    FILE *lit = fopen(name.c_str(), "rb");
    ctx.check_bool("write_object_plain_file", lit != NULL);

    if(lit)
        fclose(lit);

    remove(name.c_str());
    ctx.check_file("write_object_dat_unchanged", wdat.c_str());
    delete_file(wdat.c_str());

    std::string missing = ctx.tmp("missing.dat") + "#X";
    errno = 0;
    f = pack_fopen(missing.c_str(), F_READ);
    ctx.checkf("missing_dat", "%s errno=%d", f ? "ok" : "NULL", errno);
    ctx.check_int("missing_dat_size", (long long)file_size_ex(missing.c_str()));

    /* alleg_compat.cpp's helpers, as the game uses them for qst.dat */
    std::string q = ctx.data("qst.dat") + "#NESQST_NEW_QST";
    ctx.check_int("compat_size", (long long)conf::zc::detail::file_size_password(q.c_str(), conf::zc::datapwd()));
    f = conf::zc::detail::pack_fopen_password(q.c_str(), F_READ_PACKED, conf::zc::datapwd());
    ctx.checkf("compat_open", "%s errno=%d", f ? "ok" : "NULL", f ? 0 : errno);

    if(f)
        pack_fclose(f);
}

/* Custom vtables (pack_fopen_vtable) over a memory buffer. */
namespace
{
struct MemFile
{
    Bytes data;
    size_t pos;
    int ungot;
};

int mem_fclose(void *) { return 0; }
int mem_getc(void *u)
{
    MemFile *m = (MemFile *)u;

    if(m->ungot >= 0)
    {
        int c = m->ungot;
        m->ungot = -1;
        return c;
    }

    return m->pos < m->data.size() ? m->data[m->pos++] : EOF;
}
int mem_ungetc(int c, void *u)
{
    ((MemFile *)u)->ungot = c & 0xFF;
    return c;
}
long mem_fread(void *p, long n, void *u)
{
    long i = 0;
    int c;

    while(i < n && (c = mem_getc(u)) != EOF)
        ((unsigned char *)p)[i++] = (unsigned char)c;

    return i;
}
int mem_putc(int c, void *u)
{
    ((MemFile *)u)->data.push_back((unsigned char)c);
    return c;
}
long mem_fwrite(const void *p, long n, void *u)
{
    for(long i = 0; i < n; ++i)
        mem_putc(((const unsigned char *)p)[i], u);

    return n;
}
int mem_fseek(void *u, int off)
{
    MemFile *m = (MemFile *)u;
    m->pos += (size_t)off;
    return 0;
}
int mem_feof(void *u)
{
    MemFile *m = (MemFile *)u;
    return m->pos >= m->data.size();
}
int mem_ferror(void *) { return 0; }

PACKFILE_VTABLE g_mem_vtable = { mem_fclose, mem_getc, mem_ungetc, mem_fread, mem_putc, mem_fwrite,
                                 mem_fseek, mem_feof, mem_ferror };
} /* namespace */

CONF_TEST(pack_vtable)
{
    MemFile m;
    m.pos = 0;
    m.ungot = -1;
    packfile_password("abc");   /* no effect on vtable packfiles */
    PACKFILE *f = pack_fopen_vtable(&g_mem_vtable, &m);
    packfile_password(NULL);
    ctx.check_bool("open", f != NULL);
    ctx.check_bool("userdata", pack_get_userdata(f) == &m);
    pack_mputl(0x01020304, f);
    pack_iputw(0xABCD, f);
    pack_fwrite("hello", 5, f);
    pack_putc('!', f);
    ctx.check_mem("written", &m.data[0], m.data.size());
    conf::Seq q(ctx, "reads");
    q.add_int(pack_mgetl(f));
    q.add_int(pack_igetw(f));
    q.add_int(pack_fseek(f, 2));
    q.add_int(pack_getc(f));
    q.add_int(pack_ungetc('Z', f));
    q.add_int(pack_getc(f));
    char b[8] = { 0 };
    q.add_int(pack_fread(b, 8, f));
    q.add_str(b);
    q.add_int(pack_feof(f) ? 1 : 0);
    q.add_int(pack_getc(f));
    q.end();
    ctx.check_int("close", pack_fclose(f));
}

/* The public LZSS functions (create_lzss_pack_data, lzss_write, lzss_read). */
CONF_TEST(pack_lzss_api)
{
    std::string fn = ctx.tmp("pack_lzss_api.bin");
    Bytes b = make_bytes(ctx.rng, 9000, 1);
    PACKFILE *f = pack_fopen(fn.c_str(), F_WRITE);
    LZSS_PACK_DATA *pd = create_lzss_pack_data();
    conf::Seq w(ctx, "write");
    size_t i = 0;
    int pieces[] = { 1, 17, 4096, 1000, 3886 };

    for(int k = 0; k < 5; ++k)
    {
        w.add_int(lzss_write(f, pd, pieces[k], &b[i], k == 4), "piece %d", k);
        i += (size_t)pieces[k];
    }

    w.end();
    free_lzss_pack_data(pd);
    pack_fclose(f);
    ctx.check_file("file", fn.c_str());

    f = pack_fopen(fn.c_str(), F_READ);
    LZSS_UNPACK_DATA *ud = create_lzss_unpack_data();
    conf::Seq r(ctx, "read");
    Bytes out(10000);
    int got = 0;
    int asks[] = { 5, 1, 4096, 2000, 10000, 10 };

    for(int k = 0; k < 6; ++k)
    {
        int n = lzss_read(f, ud, asks[k], &out[got]);
        r.add_int(n, "ask %d", asks[k]);

        if(n > 0)
            got += n;
    }

    r.add_mem(&out[0], (size_t)got);
    r.end();
    free_lzss_unpack_data(ud);
    pack_fclose(f);
    delete_file(fn.c_str());
}

/* Old-format password files (written by older Allegro versions, e.g. old
 * quests): magic mask without the final 42, the LZSS flags bytes XORed
 * with the password and the other bytes left alone. The files are built
 * here from what the library itself writes. */
CONF_TEST(pack_old_format)
{
    std::string fn = ctx.tmp("pack_old.bin");
    static const char *pws[] = { "abc", "longtan", "x", "a much longer password" };

    for(int p = 0; p < 4; ++p)
        for(int style = 0; style < 3; ++style)
        {
            const char *pw = pws[p];
            size_t pl = strlen(pw);
            Bytes data = make_bytes(ctx.rng, 3000 + p * 1000, style == 2 ? 4 : style);

            /* the stream mask of this password: from an empty "wp" file */
            write_file(fn, F_WRITE_PACKED, pw, Bytes(), ctx.rng);
            Bytes head;
            {
                PACKFILE *f = pack_fopen(fn.c_str(), F_READ);
                int c;

                while((c = pack_getc(f)) != EOF)
                    head.push_back((unsigned char)c);

                pack_fclose(f);
            }

            unsigned long first = ((unsigned long)head[0] << 24) | ((unsigned long)head[1] << 16) |
                                  ((unsigned long)head[2] << 8) | head[3];
            unsigned long stream4 = ((unsigned long)(unsigned char)pw[0 % pl] << 24) |
                                    ((unsigned long)(unsigned char)pw[1 % pl] << 16) |
                                    ((unsigned long)(unsigned char)pw[2 % pl] << 8) | (unsigned char)pw[3 % pl];
            unsigned long mask_new = first ^ stream4 ^ (unsigned long)F_PACK_MAGIC;
            unsigned long mask_old = mask_new ^ 42;

            /* LZSS data without a password */
            write_file(fn, F_WRITE_PACKED, NULL, data, ctx.rng);
            Bytes z;
            {
                PACKFILE *f = pack_fopen(fn.c_str(), F_READ);
                int c;

                while((c = pack_getc(f)) != EOF)
                    z.push_back((unsigned char)c);

                pack_fclose(f);
                z.erase(z.begin(), z.begin() + 4);
            }

            /* scramble the flags bytes */
            size_t i = 0, k = 0;

            while(i < z.size())
            {
                unsigned char flags = z[i];
                z[i] ^= (unsigned char)pw[k % pl];
                ++k;
                ++i;

                for(int bit = 0; bit < 8 && i < z.size(); ++bit)
                    i += (flags >> bit) & 1 ? 1 : 2;
            }

            for(int nopack = 0; nopack < 2; ++nopack)
            {
                unsigned long magic = (nopack ? (unsigned long)F_NOPACK_MAGIC : (unsigned long)F_PACK_MAGIC) ^ mask_old ^ stream4;
                PACKFILE *f = pack_fopen(fn.c_str(), F_WRITE);
                pack_mputl((long)magic, f);

                if(nopack)
                    pack_fwrite(&data[0], (long)data.size(), f);
                else
                    pack_fwrite(&z[0], (long)z.size(), f);

                pack_fclose(f);
                ctx.checkf(conf::nm("old_%d_%d_%d", p, style, nopack), "%s", read_all(fn, F_READ_PACKED, pw).c_str());
                ctx.checkf(conf::nm("old_wrongpw_%d_%d_%d", p, style, nopack), "%s",
                           read_all(fn, F_READ_PACKED, "wrong").c_str());

                packfile_password(pw);
                f = pack_fopen(fn.c_str(), F_READ_PACKED);
                packfile_password(NULL);

                if(f)
                {
                    conf::Seq q(ctx, conf::nm("old_ops_%d_%d_%d", p, style, nopack));
                    q.add_int(pack_mgetl(f));
                    q.add_int(pack_fseek(f, 1000));
                    q.add_int(pack_igetw(f));
                    q.add_int(pack_feof(f) ? 1 : 0);
                    q.end();
                    pack_fclose(f);
                }
            }

            ctx.checkf(conf::nm("data_%d_%d", p, style), "%s", conf::hash_text(conf::hash_bytes(&data[0], data.size())).c_str());
        }

    delete_file(fn.c_str());
}

/* When pack_feof turns true on LZSS-packed files: right after the last
 * byte or only after a read hits the end, depending on how the stream's
 * last tokens fall on the 4096-byte buffer refills. */
CONF_TEST(pack_packed_eof_sizes)
{
    static const int sizes[] = { 1, 2, 17, 18, 19, 300, 4000, 4095, 4096, 4097, 4098, 4100, 4110, 4200, 5000, 8000,
                                 8191, 8192, 8193, 8200, 12287, 12288, 12289, 12300, 16000, 20000, 24576, 24577 };
    std::string fn = ctx.tmp("pack_peof.bin");

    for(int style = 0; style < 4; ++style)
        for(size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        {
            int n = sizes[s];
            PACKFILE *f = pack_fopen(fn.c_str(), F_WRITE_PACKED);

            for(int i = 0; i < n; ++i)
            {
                int c;

                switch(style)
                {
                case 0: c = i & 0xFF; break;
                case 1: c = "abcdefghijklmnopqrstuvwxyz"[(i * 7) % 26]; break;
                case 2: c = (int)(((unsigned)i * 2654435761u) >> 24) & 0xFF; break;
                default: c = (i % 50 < 40) ? 0 : (i * 13) & 0xFF; break;
                }

                pack_putc(c, f);
            }

            pack_fclose(f);
            f = pack_fopen(fn.c_str(), F_READ_PACKED);
            conf::Seq q(ctx, conf::nm("s%d_%d", style, n));

            for(int i = 0; i < n; ++i)
            {
                pack_getc(f);

                if(i >= n - 1 || (i % 4096) == 4095)
                    q.add_int(pack_feof(f) ? 1 : 0, "feof after byte %d", i);
            }

            q.add_int(pack_getc(f), "getc at end");
            q.add_int(pack_feof(f) ? 1 : 0, "feof at end");
            q.end();
            pack_fclose(f);
        }

    delete_file(fn.c_str());
}

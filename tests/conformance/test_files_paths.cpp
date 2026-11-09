/*
 * test_files_paths.cpp - file name helpers and file system queries must give
 * the reference's results on the path forms the game uses (Windows drive
 * letters, both slashes, relative paths, extensions, truncation to a buffer
 * size), plus exists/file_exists/file_size_ex/delete_file/for_each_file_ex
 * and the drive helpers of aintern.h.
 *
 * Results that contain the current directory are printed with the directory
 * replaced by "<CWD>" (it differs between the two programs' run folders).
 */
#include "conf.h"

#include <allegro/internal/aintern.h>

#include <ctype.h>
#include <direct.h>
#include <errno.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <string>
#include <vector>

extern "C" __declspec(dllimport) int __stdcall SetFileAttributesA(const char *name, unsigned long attrib);

namespace
{

const char *g_paths[] = {
    "", "a", "a.b", "a.b.c", ".bashrc", "a.", "..", ".", "dir/a.txt", "dir\\a.txt", "dir.x/a",
    "dir.x\\a", "c:a.b", "c:", "C:\\", "C:/", "c:\\dir\\file.qst", "C:/dir/file.qst", "/", "\\",
    "a/", "a\\", "dir/.b", "dir/sub/", "x/y.z/", "quests/My Quest 2.53.qst", "a b/c d.e f",
    "\\\\server\\share\\f.txt", "//server/share/f.txt", "dir/a.tar.gz", "dir\\..\\x.y", "abc.",
    "abc..", "a/b\\c/d.e", "1st.qst", "C:\\Program Files\\Zelda Classic\\zc.cfg", ":", "a:b:c",
    "dir/file.", "file.ext/", "e\\f.", "no_ext_name", "UPPER.CASE", "mIxEd.QsT",
};
const int g_npaths = (int)(sizeof(g_paths) / sizeof(g_paths[0]));

std::string cwd_text()
{
    char buf[1024];

    if(!_getcwd(buf, sizeof(buf)))
        return "";

    return buf;
}

/* Replaces the current directory and its parents (without the drive letter,
 * both slash styles) with <CWD>, <CWD..>, <CWD../..>. */
std::string redact(const char *s)
{
    if(!s)
        return "(null)";

    std::string r = s;
    std::string cwd = cwd_text();

    if(cwd.size() > 2 && cwd[1] == ':')
        cwd = cwd.substr(2);

    std::vector<std::string> dirs;
    std::vector<std::string> labels;
    std::string d = cwd;
    const char *names[] = { "<CWD>", "<CWD..>", "<CWD../..>", "<CWD../../..>" };

    for(int k = 0; k < 4 && d.size() > 1; ++k)
    {
        dirs.push_back(d);
        labels.push_back(names[k]);
        size_t at = d.find_last_of("\\/");

        if(at == std::string::npos || at == 0)
            break;

        d = d.substr(0, at);
    }

    for(size_t k = 0; k < dirs.size(); ++k)
        for(int pass = 0; pass < 2; ++pass)
        {
            std::string c = dirs[k];

            for(size_t i = 0; i < c.size(); ++i)
                if(c[i] == '\\' || c[i] == '/')
                    c[i] = pass ? '/' : '\\';

            size_t at;

            while((at = r.find(c)) != std::string::npos)
                r.replace(at, c.size(), labels[k]);
        }

    return r;
}

std::string tmp_rel(conf::Ctx &ctx, const char *s)
{
    std::string t = ctx.tmp("");
    std::string r = s;

    for(int pass = 0; pass < 2; ++pass)
    {
        std::string c = t;

        for(size_t i = 0; i < c.size(); ++i)
            if(c[i] == '\\' || c[i] == '/')
                c[i] = pass ? '/' : '\\';

        size_t at;

        while((at = r.find(c)) != std::string::npos)
            r.replace(at, c.size(), "<TMP>/");

        /* without the trailing separator */
        std::string c2 = c.substr(0, c.size() - 1);

        while(!c2.empty() && (at = r.find(c2)) != std::string::npos)
            r.replace(at, c2.size(), "<TMP>");
    }

    return redact(r.c_str());
}

void make_file(const std::string &name, int size)
{
    FILE *f = fopen(name.c_str(), "wb");

    for(int i = 0; i < size; ++i)
        fputc(i & 0xFF, f);

    fclose(f);
}

} /* namespace */

CONF_TEST(paths_get)
{
    conf::Seq a(ctx, "get_filename");
    conf::Seq b(ctx, "get_extension");
    conf::Seq c(ctx, "is_relative");

    for(int i = 0; i < g_npaths; ++i)
    {
        char buf[256];
        strcpy(buf, g_paths[i]);
        char *f = get_filename(buf);
        a.add_str(f, "%s", g_paths[i]);
        a.add_int(f ? (int)(f - buf) : -1, "offset %s", g_paths[i]);
        char *e = get_extension(buf);
        b.add_str(e, "%s", g_paths[i]);
        b.add_int(e ? (int)(e - buf) : -1, "offset %s", g_paths[i]);
        c.add_int(is_relative_filename(buf) ? 1 : 0, "%s", g_paths[i]);
    }
}

CONF_TEST(paths_replace)
{
    static const char *names[] = { "", "x", "new.qst", "sub/n.e", "..\\up.txt", "c:\\abs.f" };
    static const char *exts[] = { "", "qst", ".qst", "k", "zpwd", "a.b" };
    static const int sizes[] = { 1, 2, 4, 8, 13, 256 };
    conf::Seq rf(ctx, "replace_filename");
    conf::Seq re(ctx, "replace_extension");
    conf::Seq af(ctx, "append_filename");

    for(int i = 0; i < g_npaths; ++i)
        for(int k = 0; k < 6; ++k)
            for(int s = 0; s < 6; ++s)
            {
                char dest[300];
                memset(dest, '#', sizeof(dest));
                dest[299] = 0;
                char *r = replace_filename(dest, g_paths[i], names[k], sizes[s]);
                rf.add_str(dest, "path=%s name=%s size=%d", g_paths[i], names[k], sizes[s]);
                rf.add_int(r == dest, "ret path=%s name=%s size=%d", g_paths[i], names[k], sizes[s]);
                memset(dest, '#', sizeof(dest));
                dest[299] = 0;
                r = replace_extension(dest, g_paths[i], exts[k], sizes[s]);
                re.add_str(dest, "path=%s ext=%s size=%d", g_paths[i], exts[k], sizes[s]);
                re.add_int(r == dest, "ret path=%s ext=%s size=%d", g_paths[i], exts[k], sizes[s]);
                memset(dest, '#', sizeof(dest));
                dest[299] = 0;
                r = append_filename(dest, g_paths[i], names[k], sizes[s]);
                af.add_str(dest, "path=%s name=%s size=%d", g_paths[i], names[k], sizes[s]);
                af.add_int(r == dest, "ret path=%s name=%s size=%d", g_paths[i], names[k], sizes[s]);
            }

    /* in place, as the game does (replace_extension(qstpath, path, ...)) */
    char p[64];
    strcpy(p, "dir/quest.qst");
    replace_extension(p, p, "sav", sizeof(p));
    ctx.check_str("inplace_ext", p);
    strcpy(p, "dir/quest.qst");
    replace_filename(p, p, "other.qst", sizeof(p));
    ctx.check_str("inplace_name", p);
    strcpy(p, "dir");
    append_filename(p, p, "file", sizeof(p));
    ctx.check_str("inplace_append", p);
}

CONF_TEST(paths_fix)
{
    conf::Seq pb(ctx, "put_backslash");
    conf::Seq fc(ctx, "fix_filename_case");
    conf::Seq fs(ctx, "fix_filename_slashes");

    for(int i = 0; i < g_npaths; ++i)
    {
        char buf[300];
        strcpy(buf, g_paths[i]);
        put_backslash(buf);
        pb.add_str(buf, "%s", g_paths[i]);
        strcpy(buf, g_paths[i]);
        char *r = fix_filename_case(buf);
        fc.add_str(buf, "%s", g_paths[i]);
        fc.add_int(r == buf, "ret %s", g_paths[i]);
        strcpy(buf, g_paths[i]);
        r = fix_filename_slashes(buf);
        fs.add_str(buf, "%s", g_paths[i]);
        fs.add_int(r == buf, "ret %s", g_paths[i]);
    }
}

CONF_TEST(paths_relative)
{
    static const char *bases[] = { "c:/dir/sub/", "c:/dir/sub/file.txt", "C:\\dir\\sub\\", "c:/", "d:/other/",
                                   "c:/dir/", "c:/dir", "c:/Dir/Sub/", "/abs/path/", "rel/path/", "" };
    static const char *files[] = { "c:/dir/sub/a.qst", "c:/dir/a.qst", "c:/a.qst", "c:/dir/sub/deeper/b.txt",
                                   "C:\\dir\\sub\\a.qst", "d:/other/x", "c:/dirx/a", "c:/DIR/SUB/a.qst",
                                   "/abs/path/q", "/abs/x", "rel/path/f", "c:/dir/sub/" };
    conf::Seq q(ctx, "make_relative");

    for(size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); ++b)
        for(size_t f = 0; f < sizeof(files) / sizeof(files[0]); ++f)
            for(int sz = 0; sz < 2; ++sz)
            {
                char dest[300];
                memset(dest, '#', sizeof(dest));
                dest[299] = 0;
                char *r = make_relative_filename(dest, bases[b], files[f], sz ? 8 : 300);
                q.add_str(r ? dest : NULL, "base=%s file=%s size=%d", bases[b], files[f], sz ? 8 : 300);
            }

    q.end();

    static const char *pairs[][2] = {
        { "c:\\dir\\sub\\", "c:/dir/sub/a.qst" }, { "c:/dir\\sub/", "c:/dir/sub\\a.qst" }, { "x/", "y/f" },
        { "rel/a/", "rel/b/f" }, { "/x/", "/y/f" }, { "c:/x/", "c:/y/f" }, { "c:", "c:a" }, { "c:/dir/sub//", "c:/dir/sub/a" },
        { "c:/dir/./", "c:/dir/a" }, { "c:/a/b/c/", "c:/a/b/c/d/e/f.g" }, { "c:/a/b/c/", "c:/a/x/y/z" },
        { "c:/a/", "c:/a" }, { "c:/a/", "c:/ab" }, { "c:/ab/", "c:/a/b" }, { "a", "a/b" }, { "a/", "a/b" },
        { "c:/dir/sub/", "c:/dir/sub/a/" }, { "c:\\a\\", "c:\\a\\b\\c" }, { "\\\\srv\\sh\\", "\\\\srv\\sh\\x" },
        { "c:/a b/c/", "c:/a b/d e" },
    };

    conf::Seq q2(ctx, "make_relative2");

    for(size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i)
    {
        char dest[300];
        memset(dest, '#', sizeof(dest));
        dest[299] = 0;
        char *r = make_relative_filename(dest, pairs[i][0], pairs[i][1], 300);
        q2.add_str(r ? dest : NULL, "base=%s file=%s", pairs[i][0], pairs[i][1]);
    }
}

CONF_TEST(paths_canonicalize)
{
    static const char *in[] = { "a.txt", "./a.txt", "../a.txt", "dir/../a", "dir/./b/../c", "c:/x/../y",
                                "C:\\a\\.\\b", "/abs", "\\abs\\x", "", ".", "..", "x/", "x\\",
                                "c:/", "C:\\Windows\\..\\Users", "a//b", "a\\\\b", "dir/sub/../../up",
                                "../../x", "~/x", "./", "a/.", "a/..", "c:/a/b/../../../..", "x.qst",
                                "a/b/../../../x", "c:\\..\\x", "c:/../x", "a/./././b", "a/.//b", "a/..//b",
                                "a\\.\\..\\b", "x/./", "x/../", "C:x.qst", "c:x/../y", "C:", "c:.", "D:/x/y",
                                "z:/q", "\\\\server\\share\\x", "//server/share/x", "/", "\\", "a/b/c/../../d",
                                "...", ".../x", "a/.../b", "a/..b", "a/b..", "./../.", "dir/./../x/./y/." };
    conf::Seq q(ctx, "canonicalize");

    for(size_t i = 0; i < sizeof(in) / sizeof(in[0]); ++i)
    {
        char dest[1024];
        memset(dest, '#', sizeof(dest));
        dest[1023] = 0;
        canonicalize_filename(dest, in[i], sizeof(dest));
        q.add_str(redact(dest).c_str(), "%s", in[i]);
    }

    q.end();

    /* truncation */
    conf::Seq t(ctx, "canon_small");

    for(int sz = 1; sz < 24; ++sz)
    {
        char small_[64];
        memset(small_, '#', sizeof(small_));
        small_[63] = 0;
        canonicalize_filename(small_, "c:/abc/def/../ghi.txt", sz);
        t.add_str(small_, "size %d", sz);
        memset(small_, '#', sizeof(small_));
        small_[63] = 0;
        canonicalize_filename(small_, "C:\\x\\.\\y.z", sz);
        t.add_str(small_, "size %d b", sz);
    }
}

CONF_TEST(paths_drive)
{
    int drive = _al_getdrive();
    ctx.check_int("getdrive", drive);
    ctx.check_bool("getdrive_matches_cwd", toupper((unsigned char)cwd_text()[0]) - 'A' == drive);

    conf::Seq q(ctx, "drive_exists");

    for(int i = 0; i < 26; ++i)
        q.add_int(_al_drive_exists(i) ? 1 : 0, "drive %d", i);

    q.end();

    char buf[1024];
    memset(buf, '#', sizeof(buf));
    buf[1023] = 0;
    _al_getdcwd(drive, buf, sizeof(buf));
    ctx.check_str("getdcwd", redact(buf).c_str());

    /* as jwinfsel.cpp uses it: room for a separator, then fixes */
    char path[1024];
    _al_getdcwd(drive, path, (int)sizeof(path) - 1);
    fix_filename_case(path);
    fix_filename_slashes(path);
    put_backslash(path);
    ctx.check_str("getdcwd_fixed", redact(path).c_str());
    memset(buf, '#', sizeof(buf));
    buf[1023] = 0;
    _al_getdcwd(drive, buf, 6);
    ctx.check_str("getdcwd_small", buf);
}

CONF_TEST(paths_files)
{
    std::string a = ctx.tmp("pf_a.bin");
    std::string b = ctx.tmp("pf_b.dat");
    std::string ro = ctx.tmp("pf_ro.txt");
    std::string hid = ctx.tmp("pf_hidden.txt");
    std::string dir = ctx.tmp("pf_dir");
    std::string missing = ctx.tmp("pf_missing.xyz");

    make_file(a, 1234);
    make_file(b, 0);
    _chmod(ro.c_str(), _S_IREAD | _S_IWRITE);
    make_file(ro, 77);
    _chmod(ro.c_str(), _S_IREAD);
    SetFileAttributesA(hid.c_str(), 0x80);
    make_file(hid, 5);
    SetFileAttributesA(hid.c_str(), 0x02);
    _mkdir(dir.c_str());
    remove(missing.c_str());

    const std::string *names[] = { &a, &b, &ro, &hid, &dir, &missing };
    static const int attribs[] = { 0, FA_RDONLY, FA_HIDDEN, FA_DIREC, FA_ARCH, FA_RDONLY | FA_HIDDEN | FA_DIREC,
                                   FA_ALL, FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_DIREC | FA_ARCH };

    for(int i = 0; i < 6; ++i)
    {
        const char *n = names[i]->c_str();
        ctx.check_int(conf::nm("exists_%d", i), exists(n));
        ctx.check_int(conf::nm("size_%d", i), (long long)file_size_ex(n));

        for(int k = 0; k < 8; ++k)
        {
            int aret = -12345;
            int r = file_exists(n, attribs[k], &aret);
            ctx.checkf(conf::nm("file_exists_%d_%d", i, k), "%d aret=%d", r, r ? aret : -12345);
        }

        ctx.check_int(conf::nm("file_exists_null_%d", i), file_exists(n, FA_ALL, NULL));
    }

    /* slashes and trailing separators */
    std::string dir_slash = dir + "/";
    std::string a_bs = a;
    std::replace(a_bs.begin(), a_bs.end(), '/', '\\');
    ctx.check_int("exists_dir_slash", exists(dir_slash.c_str()));
    ctx.check_int("exists_backslashes", exists(a_bs.c_str()));
    ctx.check_int("size_backslashes", (long long)file_size_ex(a_bs.c_str()));
    ctx.check_int("exists_empty", exists(""));
    ctx.check_int("exists_wild", exists(ctx.tmp("pf_*.bin").c_str()));
    int aret = 0;
    ctx.check_int("file_exists_wild", file_exists(ctx.tmp("pf_*").c_str(), FA_ALL, &aret));

    /* delete_file */
    errno = 0;
    ctx.checkf("delete_a", "%d errno=%d", delete_file(a.c_str()), errno);
    ctx.check_int("exists_a_after", exists(a.c_str()));
    errno = 0;
    ctx.checkf("delete_missing", "%d errno=%d", delete_file(missing.c_str()), errno);
    errno = 0;
    ctx.checkf("delete_ro", "%d errno=%d", delete_file(ro.c_str()), errno);
    ctx.check_int("exists_ro_after", exists(ro.c_str()));
    errno = 0;
    ctx.checkf("delete_hidden", "%d errno=%d", delete_file(hid.c_str()), errno);
    errno = 0;
    ctx.checkf("delete_dir", "%d errno=%d", delete_file(dir.c_str()), errno);
    ctx.check_int("exists_dir_after", exists(dir.c_str()));

    /* clean up */
    _chmod(ro.c_str(), _S_IREAD | _S_IWRITE);
    remove(ro.c_str());
    SetFileAttributesA(hid.c_str(), 0x80);
    remove(hid.c_str());
    remove(b.c_str());
    _rmdir(dir.c_str());
}

namespace
{
struct Found
{
    std::vector<std::string> names;
    std::vector<int> attribs;
    int stop_after;
};

conf::Ctx *g_ctx;

int collect(const char *filename, int attrib, void *param)
{
    Found *f = (Found *)param;
    f->names.push_back(tmp_rel(*g_ctx, filename));
    f->attribs.push_back(attrib);

    if(f->stop_after > 0 && (int)f->names.size() >= f->stop_after)
        return 1;

    return 0;
}
}

CONF_TEST(paths_for_each)
{
    g_ctx = &ctx;
    std::string root = ctx.tmp("fe");
    _mkdir(root.c_str());
    make_file(root + "/b.txt", 3);
    make_file(root + "/a.qst", 10);
    make_file(root + "/C.QST", 1);
    make_file(root + "/noext", 2);
    make_file(root + "/.dotfile", 2);
    std::string ro = root + "/ro.qst";
    make_file(ro, 4);
    _chmod(ro.c_str(), _S_IREAD);
    std::string hid = root + "/hid.qst";
    make_file(hid, 4);
    SetFileAttributesA(hid.c_str(), 0x02);
    _mkdir((root + "/sub").c_str());
    _mkdir((root + "/sub.qst").c_str());
    make_file(root + "/sub/inner.qst", 1);

    static const char *pats[] = { "fe/*", "fe/*.*", "fe/*.qst", "fe\\*.QST", "fe/?.*", "fe/sub", "fe/sub/*",
                                  "fe/nothing*", "fe/", "fe/a.qst", "nodir/*" };
    static const int in_attr[] = { 0, FA_DIREC, FA_RDONLY, FA_HIDDEN, FA_DIREC | FA_HIDDEN, FA_ARCH };
    static const int out_attr[] = { 0, FA_DIREC, FA_HIDDEN, FA_LABEL, FA_DIREC | FA_HIDDEN | FA_SYSTEM, FA_RDONLY };

    for(size_t p = 0; p < sizeof(pats) / sizeof(pats[0]); ++p)
        for(int i = 0; i < 6; ++i)
        {
            Found f;
            f.stop_after = 0;
            std::string pat = ctx.tmp(pats[p]);
            errno = 0;
            int n = for_each_file_ex(pat.c_str(), in_attr[i], out_attr[i], collect, &f);
            int err = errno;
            conf::Seq q(ctx, conf::nm("fe_%d_%d", (int)p, i));
            q.add_int(n, "count");
            q.add_int(err, "errno");

            for(size_t k = 0; k < f.names.size(); ++k)
            {
                q.add_str(f.names[k].c_str(), "name %d", (int)k);
                q.add_int(f.attribs[k], "attrib %d", (int)k);
            }
        }

    /* stop early */
    Found f;
    f.stop_after = 2;
    errno = 0;
    int n = for_each_file_ex(ctx.tmp("fe/*").c_str(), 0, 0, collect, &f);
    ctx.checkf("fe_stop", "%d errno=%d found=%d", n, errno, (int)f.names.size());

    /* the game's own call shapes (jwinfsel.cpp) */
    Found g;
    g.stop_after = 0;
    n = for_each_file_ex(ctx.tmp("fe/*.*").c_str(), 0, FA_LABEL, collect, &g);
    ctx.check_int("fe_game1", n);
    Found h;
    h.stop_after = 0;
    n = for_each_file_ex(ctx.tmp("fe/*.*").c_str(), FA_DIREC, FA_LABEL | FA_HIDDEN, collect, &h);
    ctx.check_int("fe_game2", n);

    _chmod(ro.c_str(), _S_IREAD | _S_IWRITE);
    remove(ro.c_str());
    SetFileAttributesA(hid.c_str(), 0x80);
    remove(hid.c_str());
    remove((root + "/b.txt").c_str());
    remove((root + "/a.qst").c_str());
    remove((root + "/C.QST").c_str());
    remove((root + "/noext").c_str());
    remove((root + "/.dotfile").c_str());
    remove((root + "/sub/inner.qst").c_str());
    _rmdir((root + "/sub").c_str());
    _rmdir((root + "/sub.qst").c_str());
    _rmdir(root.c_str());
}

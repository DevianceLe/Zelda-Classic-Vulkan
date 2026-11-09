/*
 * test_ucfg_config.cpp - the config file routines must read and write
 * exactly like the reference build: the player keeps its settings in zc.cfg
 * (read at start, written back by save_game_configs() and at exit, unknown
 * keys preserved) and reads themes through set_config_file().
 *
 * Covers: parsing (sections, comments, white space, quotes, duplicates,
 * global variables, case, CR/LF, long lines, malformed lines), every getter
 * (string, int, hex, float, id, argv, text) with defaults, every setter and
 * the exact bytes flush_config_file() writes (new keys, new sections,
 * removal, empty values), when files are written (dirty tracking,
 * set_config_file switching), list_config_*, and the game's real zc.cfg,
 * zquest.cfg and theme files with the game's own read/save sequence.
 */
#include "conf.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

namespace
{

std::string esc(const std::string &s)
{
    std::string r;

    for(size_t i = 0; i < s.size(); ++i)
    {
        unsigned char c = (unsigned char)s[i];

        if(c >= 0x20 && c < 0x7F && c != '\\')
            r += (char)c;
        else
            r += conf::nm("\\x%02x", c);
    }

    return r;
}

bool write_file(const std::string &path, const std::string &data)
{
    FILE *f = fopen(path.c_str(), "wb");

    if(!f)
        return false;

    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}

bool read_file(const std::string &path, std::string &out)
{
    FILE *f = fopen(path.c_str(), "rb");

    if(!f)
        return false;

    out.clear();
    char buf[4096];
    size_t n;

    while((n = fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, n);

    fclose(f);
    return true;
}

bool file_exists(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");

    if(f)
        fclose(f);

    return f != 0;
}

/* The bytes of a file: size + hash, and every line in a sequence (so a
 * difference shows the line). */
void check_contents(conf::Ctx &ctx, const char *c, const std::string &path)
{
    std::string data;

    if(!read_file(path, data))
    {
        ctx.checkf(c, "(missing)");
        return;
    }

    ctx.checkf(c, "%u bytes %s", (unsigned)data.size(),
               conf::hash_text(conf::hash_bytes(data.data(), data.size())).c_str());
    conf::Seq s(ctx, conf::nm("%s_lines", c));
    size_t start = 0;
    int line = 0;

    while(start < data.size())
    {
        size_t nl = data.find('\n', start);
        size_t end = nl == std::string::npos ? data.size() : nl + 1;
        s.add_str(esc(data.substr(start, end - start)).c_str(), "line %d", line++);
        start = end;
    }
}

/* A config file nobody looks at, so the next test starts clean and nothing
 * of this test is flushed into a file it checks later. */
void park_config(conf::Ctx &ctx)
{
    set_config_file(ctx.tmp("parked.cfg").c_str());
}

std::string str_or_null(const char *s)
{
    return s ? std::string("\"") + esc(s) + "\"" : std::string("(null)");
}

/* Every getter on one key. */
void probe(conf::Seq &s, const char *sec, const char *name)
{
    const char *v = get_config_string(sec, name, "DEF");
    std::string r = "str=" + str_or_null(v);
    r += conf::nm(" int=%d hex=%d", get_config_int(sec, name, -77), get_config_hex(sec, name, -77));
    float f = get_config_float(sec, name, -7.5f);
    unsigned int fb;
    memcpy(&fb, &f, 4);
    r += conf::nm(" float=%08x id=%08x", fb, (unsigned)get_config_id(sec, name, 0x41424344));
    /* Quoted tokens make the reference read stale bytes of its token
     * buffer (the player never uses get_config_argv): not compared. */
    if(v && (strchr(v, '"') || strchr(v, '\'')))
    {
        s.add_str((r + " argv=quoted").c_str(), "sec=%s name=%s", sec ? sec : "(null)", name ? name : "(null)");
        return;
    }

    int argc = -1;
    char **argv = get_config_argv(sec, name, &argc);
    r += conf::nm(" argc=%d", argc);

    if(argv)
        for(int i = 0; i < argc; ++i)
            r += " [" + esc(argv[i]) + "]";
    else
        r += " argv=(null)";

    s.add_str(r.c_str(), "sec=%s name=%s", sec ? sec : "(null)", name ? name : "(null)");
}

const char *const kParseText =
    "# comment at top\n"
    "global1 = g1\n"
    " global2=g2  \n"
    "\tglobal3\t=\tg3\t\n"
    "[sec]\n"
    "a = 1\n"
    "b=2\n"
    " c = 3 \n"
    "d =\n"
    "e\n"
    "= noname\n"
    "f = value with spaces  \n"
    "g = \"quoted value\"\n"
    "h = 'single'\n"
    "i = x = y\n"
    "j = tab\there\n"
    "k = 0x1F\n"
    "l = -5\n"
    "m = 12abc\n"
    "n = 010\n"
    "o = 99999999999\n"
    "p = -0x10\n"
    "q = 1.5\n"
    "r = 1e3\n"
    "s = abc\n"
    "t =    \n"
    "u = # not a comment?\n"
    "v = val # trailing comment\n"
    "w = val ; semicolon\n"
    ";semi = comment?\n"
    "#hash = comment\n"
    "x = \"a b\" c  'd e'\n"
    "y = ffffffff\n"
    "z = -1\n"
    "MixedCase = mc\n"
    "dvc2_r = 16*63/255\n"
    "theme_filename =\n"
    "//normal = text\n"
    "id = DXAC\n"
    "id2 = AB\n"
    "  [  spaced  ]\n"
    "x = 1\n"
    "[Sec]\n"
    "a = upper\n"
    "[sec]\n"
    "a = dup section\n"
    "extra = in second sec\n"
    "[sec2]\n"
    "a = s2\n"
    "dupkey = first\n"
    "dupkey = second\n"
    "hx1 = 7fffffff\n"
    "hx2 = 80000000\n"
    "hx3 = 100000000\n"
    "hx4 = -80000000\n"
    "hx5 = 7FFFFFFF\n"
    "hx6 = 0x7fffffff\n"
    "hx7 = 0x80000000\n"
    "hx8 = -7fffffff\n"
    "Dupkey = third\n"
    "[]\n"
    "emptysec = 1\n"
    "[#hidden]\n"
    "y = 1\n"
    "[ unterminated\n"
    "z = 1\n"
    "[a b]\n"
    "c d = e f\n"
    "[sec3] trailing\n"
    "k3 = v3\n"
    "[sec4]]\n"
    "k4 = v4\n"
    "last = no newline";

struct Key
{
    const char *sec;
    const char *name;
};

const Key kParseKeys[] =
{
    { 0, "global1" }, { 0, "global2" }, { 0, "global3" }, { "sec", "global1" }, { "sec2", "global2" },
    { "nosuch", "global1" }, { "", "global1" },
    { "sec", "a" }, { "sec", "b" }, { "sec", "c" }, { "sec", "d" }, { "sec", "e" }, { "sec", "" },
    { "sec", "f" }, { "sec", "g" }, { "sec", "h" }, { "sec", "i" }, { "sec", "j" }, { "sec", "k" },
    { "sec", "l" }, { "sec", "m" }, { "sec", "n" }, { "sec", "o" }, { "sec", "p" }, { "sec", "q" },
    { "sec", "r" }, { "sec", "s" }, { "sec", "t" }, { "sec", "u" }, { "sec", "v" }, { "sec", "w" },
    { "sec", ";semi" }, { "sec", "#hash" }, { "sec", "x" }, { "sec", "y" }, { "sec", "z" },
    { "sec", "MixedCase" }, { "sec", "mixedcase" }, { "SEC", "a" }, { "Sec", "a" }, { "[sec]", "a" },
    { "sec", "dvc2_r" }, { "sec", "theme_filename" }, { "sec", "//normal" }, { "sec", "id" },
    { "sec", "id2" }, { "sec", "extra" }, { "spaced", "x" }, { "  spaced  ", "x" }, { " spaced ", "x" },
    { "sec2", "a" }, { "sec2", "dupkey" }, { "sec2", "hx1" }, { "sec2", "hx2" }, { "sec2", "hx3" },
    { "sec2", "hx4" }, { "sec2", "hx5" }, { "sec2", "hx6" }, { "sec2", "hx7" }, { "sec2", "hx8" }, { "", "emptysec" }, { 0, "emptysec" }, { "#hidden", "y" },
    { "unterminated", "z" }, { " unterminated", "z" }, { "[ unterminated", "z" }, { "a b", "c d" },
    { "a b", "c" }, { "sec3", "k3" }, { "sec3] trailing", "k3" }, { "sec4", "k4" }, { "sec4]", "k4" },
    { "sec4", "last" }, { "sec4]", "last" }, { "nosuch", "nokey" }, { "sec", "nokey" }, { 0, "nokey" },
    { "sec", "a " }, { "sec ", "a" }, { " sec", "a" }
};

void parse_and_probe(conf::Ctx &ctx, const char *tag, const std::string &text)
{
    std::string path = ctx.tmp(conf::nm("parse_%s.cfg", tag));
    write_file(path, text);
    set_config_file(path.c_str());
    conf::Seq s(ctx, conf::nm("%s_values", tag));

    for(size_t i = 0; i < sizeof(kParseKeys) / sizeof(kParseKeys[0]); ++i)
        probe(s, kParseKeys[i].sec, kParseKeys[i].name);

    s.end();

    /* The section and entry lists. */
    const char **names = 0;
    int n = list_config_sections(&names);
    conf::Seq ls(ctx, conf::nm("%s_sections", tag));
    ls.add_int(n, "count");

    for(int i = 0; i < n; ++i)
        ls.add_str(names[i], "section %d", i);

    ls.end();
    conf::Seq le(ctx, conf::nm("%s_entries", tag));
    const char *secs[] = { 0, "sec", "sec2", "Sec", "spaced", "", "#hidden", "nosuch" };

    for(size_t k = 0; k < sizeof(secs) / sizeof(secs[0]); ++k)
    {
        const char **entries = 0;
        int m = list_config_entries(secs[k], &entries);
        le.add_int(m, "count %s", secs[k] ? secs[k] : "(null)");

        for(int i = 0; i < m; ++i)
            le.add_str(entries[i], "%s entry %d", secs[k] ? secs[k] : "(null)", i);

        free_config_entries(&entries);
    }

    le.end();
    free_config_entries(&names);

    /* Nothing was changed: flushing must not rewrite the file. */
    remove(path.c_str());
    flush_config_file();
    ctx.check_bool(conf::nm("%s_unchanged_not_written", tag), file_exists(path));

    /* One change elsewhere: how every line is written back. */
    set_config_string("sec2", "zz", "1");
    flush_config_file();
    check_contents(ctx, conf::nm("%s_rewritten", tag), path);
    park_config(ctx);
}

/* Line length limits, invalid UTF-8 and other bytes, in both formats. */
void bytes_case(conf::Ctx &ctx, const char *tag, const std::string &text, const Key *keys, int nkeys)
{
    for(int fmt = 0; fmt < 2; ++fmt)
    {
        int old = get_uformat();
        set_uformat(fmt ? U_ASCII : U_UTF8);
        std::string path = ctx.tmp(conf::nm("bytes_%s.cfg", tag));
        write_file(path, text);
        set_config_file(path.c_str());
        conf::Seq s(ctx, conf::nm("%s_%s", tag, fmt ? "ascii" : "utf8"));

        for(int k = 0; k < nkeys; ++k)
        {
            const char *v = get_config_string(keys[k].sec, keys[k].name, 0);
            s.add_str(v ? conf::nm("len=%d %s", (int)strlen(v), esc(std::string(v).substr(0, 40)).c_str())
                        : "(null)", "%s/%s", keys[k].sec ? keys[k].sec : "(null)", keys[k].name);
        }

        s.end();
        set_config_string("zz", "k", "1");
        flush_config_file();
        check_contents(ctx, conf::nm("%s_%s_written", tag, fmt ? "ascii" : "utf8"), path);
        park_config(ctx);
        set_uformat(old);
    }
}

std::string with_crlf(const std::string &s)
{
    std::string r;

    for(size_t i = 0; i < s.size(); ++i)
    {
        if(s[i] == '\n')
            r += '\r';

        r += s[i];
    }

    return r;
}

/* Runs a list of edits on a file and checks what flush writes. */
struct Edit
{
    enum Kind { STR, INT, HEX, FLOAT, ID, NULLSTR } kind;
    const char *sec;
    const char *name;
    const char *s;
    int i;
    float f;
};

void apply(const Edit &e)
{
    switch(e.kind)
    {
    case Edit::STR: set_config_string(e.sec, e.name, e.s); break;
    case Edit::NULLSTR: set_config_string(e.sec, e.name, 0); break;
    case Edit::INT: set_config_int(e.sec, e.name, e.i); break;
    case Edit::HEX: set_config_hex(e.sec, e.name, e.i); break;
    case Edit::FLOAT: set_config_float(e.sec, e.name, e.f); break;
    case Edit::ID: set_config_id(e.sec, e.name, e.i); break;
    }
}

} // namespace

CONF_TEST(ucfg_cfg_parse)
{
    parse_and_probe(ctx, "lf", kParseText);
    parse_and_probe(ctx, "crlf", with_crlf(kParseText));

    /* Odd files: empty, only comments, NUL bytes, a BOM, very long lines,
     * high-bit bytes. */
    std::string longline = "[long]\nk = ";

    for(int i = 0; i < 3000; ++i)
        longline += (char)('a' + i % 26);

    longline += "\nafter = yes\nk2 = ";

    for(int i = 0; i < 300; ++i)
        longline += (char)('0' + i % 10);

    longline += "\n";
    std::string longname = "[long]\n";

    for(int i = 0; i < 400; ++i)
        longname += (char)('A' + i % 26);

    longname += " = longname\n";

    struct Odd
    {
        const char *tag;
        std::string text;
    };

    std::vector<Odd> odd;
    odd.push_back(Odd());
    odd.back().tag = "empty";
    odd.push_back(Odd());
    odd.back().tag = "comments";
    odd.back().text = "# a\n# b\n\n";
    odd.push_back(Odd());
    odd.back().tag = "nul";
    odd.back().text = std::string("[n]\nk = a\0b\nk2 = c\n", 18);
    odd.push_back(Odd());
    odd.back().tag = "bom";
    odd.back().text = "\xef\xbb\xbf[b]\nk = v\n";
    odd.push_back(Odd());
    odd.back().tag = "longline";
    odd.back().text = longline;
    odd.push_back(Odd());
    odd.back().tag = "longname";
    odd.back().text = longname;
    odd.push_back(Odd());
    odd.back().tag = "highbit";
    odd.back().text = "[h\xe9]\nk\xe9 = v\xe9\xff\nk2 = \xc3\xa9t\xc3\xa9\n[h]\nk = \xe9\n";
    odd.push_back(Odd());
    odd.back().tag = "cronly";
    odd.back().text = "[c]\rk = 1\rk2 = 2\r";
    odd.push_back(Odd());
    odd.back().tag = "spaces";
    odd.back().text = "   \n\t\n[s]\n   k   =   v   \n";

    const Key kOddKeys[] =
    {
        { "n", "k" }, { "n", "k2" }, { "b", "k" }, { "\xef\xbb\xbf[b]", "k" }, { "long", "k" },
        { "long", "after" }, { "long", "k2" }, { "h\xe9", "k\xe9" }, { "h\xe9", "k2" }, { "h", "k" },
        { "c", "k" }, { "c", "k2" }, { "s", "k" }, { 0, "k" }
    };

    for(size_t i = 0; i < odd.size(); ++i)
    {
        for(int fmt = 0; fmt < 2; ++fmt)
        {
            int old = get_uformat();
            set_uformat(fmt ? U_ASCII : U_UTF8);
            std::string path = ctx.tmp(conf::nm("odd_%s.cfg", odd[i].tag));
            write_file(path, odd[i].text);
            set_config_file(path.c_str());
            conf::Seq s(ctx, conf::nm("%s_%s", odd[i].tag, fmt ? "ascii" : "utf8"));

            for(size_t k = 0; k < sizeof(kOddKeys) / sizeof(kOddKeys[0]); ++k)
                probe(s, kOddKeys[k].sec, kOddKeys[k].name);

            /* The long name by its first 200 characters and in full. */
            std::string ln(longname.begin() + 7, longname.end() - 12);
            s.add_str(str_or_null(get_config_string("long", ln.c_str(), "DEF")).c_str(), "long name");
            s.add_str(str_or_null(get_config_string("long", ln.substr(0, 200).c_str(), "DEF")).c_str(),
                      "long name prefix");
            s.end();

            /* Rewrite after a change: what survives. */
            set_config_string("added", "key", "val");
            flush_config_file();
            check_contents(ctx, conf::nm("%s_%s_written", odd[i].tag, fmt ? "ascii" : "utf8"), path);
            park_config(ctx);
            set_uformat(old);
        }
    }
}

CONF_TEST(ucfg_cfg_bytes)
{
    /* Values and names around the length where lines stop being written. */
    std::string lens = "[l]\n";
    std::vector<std::string> names;

    for(int n = 240; n <= 270; ++n)
    {
        std::string name = conf::nm("v%d", n);
        names.push_back(name);
        lens += name + " = " + std::string((size_t)n, 'x') + "\n";
    }

    for(int n = 120; n <= 136; n += 2)
    {
        std::string name = std::string((size_t)n, 'n');
        names.push_back(name);
        lens += name + " = " + std::string((size_t)n, 'y') + "\n";
    }

    for(int n = 248; n <= 262; ++n)
    {
        std::string name = std::string((size_t)n, 'N');
        names.push_back(name);
        lens += name + " = v\n";
    }

    for(int n = 248; n <= 262; ++n)
        lens += "#" + std::string((size_t)n, 'c') + "\n";

    std::vector<Key> keys;

    for(size_t i = 0; i < names.size(); ++i)
    {
        Key k = { "l", names[i].c_str() };
        keys.push_back(k);
    }

    bytes_case(ctx, "lens", lens, &keys[0], (int)keys.size());

    /* Invalid and valid UTF-8 in values, names and section names. */
    static const Key kU[] =
    {
        { "s", "k1" }, { "s", "k2" }, { "s", "k3" }, { "s", "k4" }, { "s", "k5" }, { "s", "k6" }, { "s", "k7" },
        { "s", "k8" }, { "s", "k9" }, { "s", "after" }, { "t", "k" }, { "s\xc3\xa9", "k" }, { "u", "x" }
    };
    bytes_case(ctx, "utf8",
               "[s]\nk1 = a\xe9" "b\nk2 = a\xe9\nk3 = \xff\nk4 = \xc3\nk5 = \x80z\nk6 = \xc3\xa9t\xc3\xa9\n"
               "k7 = \xe2\x82\xac\nk8 = \xf0\x9f\x98\x80!\nk9 = \xe9\xe9\xe9\xe9\nafter = ok\n"
               "[t]\nk\xe9 = v\nk = w\n[s\xc3\xa9]\nk = sec\n[u]\nx = \xc3\n",
               kU, (int)(sizeof(kU) / sizeof(kU[0])));

    /* How odd lines are stored and written back. */
    static const Key kL[] =
    {
        { "s", "c" }, { "s", "c d" }, { "s", "e" }, { "s", "" }, { "s", "tabbed" }, { "s", "eq" },
        { "s", "k" }, { "s2", "k" }, { "s3", "k" }, { "s4", "k" }
    };
    bytes_case(ctx, "lines",
               "[s]\nc d = e f\ne\n= x\n\ttabbed\t=\tt v\t\neq==\nk=v=w\n  # indented comment\n"
               "  ;semi\n[s2]\nk = 2\n   \n[s3]\nk = 3\n\t\n[s4]\nk = 4\n# c\n   ",
               kL, (int)(sizeof(kL) / sizeof(kL[0])));
    bytes_case(ctx, "ws_end", "[s]\nk = 1\n   \n", kL, 1);
    bytes_case(ctx, "comment_end", "[s]\nk = 1\n# c", kL, 1);
    bytes_case(ctx, "crlf_mix", "[s]\r\nk = 1\r\n\r\n\r[s2]\n\rk = 2\r\r\n", kL, 8);
}

CONF_TEST(ucfg_cfg_insert)
{
    /* Where set_config_string puts a new variable, for every kind of line
     * that may end a section. */
    struct Case
    {
        const char *tag;
        const char *text;
        const char *sec;
        const char *name;
    };

    static const Case kCases[] =
    {
        { "plain", "[a]\nx = 1\n[b]\ny = 2\n[c]\nz = 3\n", "b", "new" },
        { "empty_header", "[a]\nx = 1\n[b]\ny = 2\n[]\nq = 1\n[c]\nz = 3\n", "b", "new" },
        { "hash_header", "[a]\nx = 1\n[b]\ny = 2\n[#h]\nq = 1\n[c]\nz = 3\n", "b", "new" },
        { "bad_header", "[a]\nx = 1\n[b]\ny = 2\n[ bad\nq = 1\n[c]\nz = 3\n", "b", "new" },
        { "dup_section", "[a]\nx = 1\n[b]\ny = 2\n[b]\nw = 1\n[c]\nz = 3\n", "b", "new" },
        { "case_section", "[a]\nx = 1\n[B]\ny = 2\n[c]\nz = 3\n", "b", "new" },
        { "tail_header", "[a]\nx = 1\n[b] tail\ny = 2\n[c]\nz = 3\n", "b", "new" },
        { "global_top", "x = 0\n[a]\nx = 1\n", 0, "new" },
        { "global_comment", "# c\n\n[a]\nx = 1\n", 0, "new" },
        { "global_none", "[a]\nx = 1\n", 0, "new" },
        { "after_comment", "[a]\nx = 1\n\n# comment\n\n[c]\nz = 3\n", "a", "new" },
        { "empty_section", "[a]\n\n\n[c]\nz = 3\n", "a", "new" },
        { "header_only_end", "[a]\nx = 1\n[c]", "c", "new" },
        { "last_blank_ws", "[a]\nx = 1\n  \n\t\n", "a", "new" },
        { "new_after_ws", "[a]\nx = 1\n  \n", "zz", "new" },
        { "new_after_comment", "[a]\nx = 1\n# end", "zz", "new" },
        { "new_after_blank", "[a]\nx = 1\n\n", "zz", "new" },
        { "new_in_empty", "", "zz", "new" },
        { "new_in_blank", "\n", "zz", "new" },
        { "global_into_empty", "", 0, "new" },
        { "dup_key", "[a]\nk = 1\nk = 2\n", "a", "k" },
        { "dup_section_key", "[a]\nk = 1\n[a]\nk = 2\n", "a", "k" },
        { "bracket_var", "[a]\nx = 1\n[ y = 2\nq = 3\n", "a", "new" },
        { "spaced_header", "[a]\nx = 1\n  [  b  ]\ny = 2\n", "a", "new" },
        { "named_empty_mid", "[a]\nx = 1\ne\ny = 2\n[b]\nq = 1\n", "a", "new" },
        { "named_empty_last", "[a]\nx = 1\ne\n", "zz", "new" },
        { "new_after_header", "[a]", "zz", "new" },
        { "new_after_header_text", "[a] tail", "zz", "new" },
        { "new_after_eq_line", "[a]\nx = 1\n= y", "zz", "new" },
    };

    for(size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
    {
        std::string path = ctx.tmp(conf::nm("ins_%s.cfg", kCases[i].tag));
        write_file(path, kCases[i].text);
        set_config_file(path.c_str());
        set_config_string(kCases[i].sec, kCases[i].name, "N");
        ctx.check_str(conf::nm("%s_get", kCases[i].tag), get_config_string(kCases[i].sec, kCases[i].name, "DEF"));
        ctx.check_str(conf::nm("%s_get_w", kCases[i].tag), get_config_string("b", "w", "DEF"));
        flush_config_file();
        check_contents(ctx, kCases[i].tag, path);
        park_config(ctx);
    }

    /* Line lengths: the writer leaves out lines from some length on. */
    std::string lens = "[l]\n";

    for(int n = 1; n <= 250; ++n)
        lens += conf::nm("v%03d = ", n) + std::string((size_t)n, 'x') + "\n";

    std::string path = ctx.tmp("ins_lens.cfg");
    write_file(path, lens);
    set_config_file(path.c_str());
    conf::Seq rd(ctx, "lens_read");

    for(int n = 1; n <= 250; n += 7)
    {
        const char *v = get_config_string("l", conf::nm("v%03d", n), 0);
        rd.add_int(v ? (long long)strlen(v) : -1, "v%03d", n);
    }

    rd.end();
    set_config_string("zz", "k", "1");
    set_config_string("l", "added", std::string(200, 'a').c_str());
    set_config_string("l", "added2", std::string(250, 'b').c_str());
    flush_config_file();
    check_contents(ctx, "lens_written", path);
    park_config(ctx);
}

CONF_TEST(ucfg_cfg_argv)
{
    /* Token lists (only unquoted ones are used by anything). */
    std::string path = ctx.tmp("argv.cfg");
    write_file(path, "[a]\nk1 = one\nk2 = one two  three\nk3 =  \t lead\ttab \nk4 = a#b c\nk5 = # x\n"
                     "k6 = a b # c d\nk7 = x;y z\nk8 = 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20\n");
    set_config_file(path.c_str());
    conf::Seq s(ctx, "argv");
    static const char *const kNames[] = { "k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8", "none" };

    for(size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i)
    {
        int argc = -1;
        char **argv = get_config_argv("a", kNames[i], &argc);
        std::string r = conf::nm("argc=%d%s", argc, argv ? "" : " (null)");

        if(argv)
            for(int k = 0; k < argc; ++k)
                r += " [" + esc(argv[k]) + "]";

        s.add_str(r.c_str(), "%s", kNames[i]);
    }

    s.end();
    park_config(ctx);
}

CONF_TEST(ucfg_cfg_write)
{
    const char *const kBase =
        "# header comment\n"
        "glob = 1\n"
        "\n"
        "[zeldadx]\n"
        "window_display = 3\n"
        "fullscreen = 0\n"
        "\n"
        "[CONSOLE]\n"
        "ZScript_Debugger = 0\n"
        "# trailing comment\n"
        "\n"
        "\n"
        "[Theme]\n"
        "theme_filename =\n"
        "dvc1_r = 4\n"
        "    \n"
        "dvc2_r = 16*63/255\n"
        "\n"
        "[last]\n"
        "k = v";

    struct Scenario
    {
        const char *tag;
        Edit edits[8];
        int n;
    };

    static const Scenario kScenarios[] =
    {
        { "set_existing", { { Edit::INT, "zeldadx", "fullscreen", 0, 1, 0 } }, 1 },
        { "set_same_value", { { Edit::INT, "zeldadx", "fullscreen", 0, 0, 0 } }, 1 },
        { "set_same_string", { { Edit::STR, "zeldadx", "window_display", "3", 0, 0 } }, 1 },
        { "new_key", { { Edit::INT, "zeldadx", "resx", 0, 640, 0 } }, 1 },
        { "new_key_commented_sec", { { Edit::INT, "CONSOLE", "other", 0, 5, 0 } }, 1 },
        { "new_key_last_sec", { { Edit::STR, "last", "k2", "v2", 0, 0 } }, 1 },
        { "new_section", { { Edit::INT, "graphics", "disable_direct_updating", 0, 1, 0 } }, 1 },
        { "two_new_sections", { { Edit::STR, "s1", "a", "1", 0, 0 }, { Edit::STR, "s2", "b", "2", 0, 0 },
                                { Edit::STR, "s1", "c", "3", 0, 0 } }, 3 },
        { "global_new", { { Edit::STR, 0, "newglob", "x", 0, 0 } }, 1 },
        { "global_existing", { { Edit::STR, 0, "glob", "2", 0, 0 } }, 1 },
        { "empty_section_name", { { Edit::STR, "", "emp", "x", 0, 0 } }, 1 },
        { "remove_key", { { Edit::NULLSTR, "zeldadx", "fullscreen", 0, 0, 0 } }, 1 },
        { "remove_missing", { { Edit::NULLSTR, "zeldadx", "nosuch", 0, 0, 0 } }, 1 },
        { "remove_last_in_sec", { { Edit::NULLSTR, "last", "k", 0, 0, 0 } }, 1 },
        { "empty_value", { { Edit::STR, "zeldadx", "fullscreen", "", 0, 0 } }, 1 },
        { "empty_value_new", { { Edit::STR, "zeldadx", "newempty", "", 0, 0 } }, 1 },
        { "spaces_value", { { Edit::STR, "zeldadx", "sp", "  a b  ", 0, 0 } }, 1 },
        { "case_key", { { Edit::INT, "ZELDADX", "FULLSCREEN", 0, 7, 0 } }, 1 },
        { "bracket_section", { { Edit::INT, "[zeldadx]", "fullscreen", 0, 8, 0 } }, 1 },
        { "hash_name", { { Edit::STR, "zeldadx", "#hidden", "h", 0, 0 } }, 1 },
        { "hash_section", { { Edit::STR, "#sec", "k", "h", 0, 0 } }, 1 },
        { "ints", { { Edit::INT, "n", "a", 0, -5, 0 }, { Edit::INT, "n", "b", 0, (int)0x80000000, 0 },
                    { Edit::INT, "n", "c", 0, 2147483647, 0 }, { Edit::INT, "n", "d", 0, 0, 0 } }, 4 },
        { "hexes", { { Edit::HEX, "n", "a", 0, 255, 0 }, { Edit::HEX, "n", "b", 0, -1, 0 },
                     { Edit::HEX, "n", "c", 0, (int)0x80000000, 0 }, { Edit::HEX, "n", "d", 0, 0, 0 },
                     { Edit::HEX, "n", "e", 0, -2, 0 }, { Edit::HEX, "n", "f", 0, 0xABCDEF, 0 } }, 6 },
        { "floats", { { Edit::FLOAT, "n", "a", 0, 0, 1.5f }, { Edit::FLOAT, "n", "b", 0, 0, 0.1f },
                      { Edit::FLOAT, "n", "c", 0, 0, -0.0f }, { Edit::FLOAT, "n", "d", 0, 0, 1e20f },
                      { Edit::FLOAT, "n", "e", 0, 0, 3.0f }, { Edit::FLOAT, "n", "f", 0, 0, 1e-7f },
                      { Edit::FLOAT, "n", "g", 0, 0, 123456.789f } }, 7 },
        { "ids", { { Edit::ID, "n", "a", 0, AL_ID('D','X','A','C'), 0 }, { Edit::ID, "n", "b", 0, 0, 0 },
                   { Edit::ID, "n", "c", 0, AL_ID(' ','A','B',' '), 0 }, { Edit::ID, "n", "d", 0, -1, 0 },
                   { Edit::ID, "n", "e", 0, AL_ID(0,0,'A','B'), 0 } }, 5 },
        { "set_then_remove", { { Edit::STR, "zeldadx", "tmp", "1", 0, 0 }, { Edit::NULLSTR, "zeldadx", "tmp", 0, 0, 0 } }, 2 },
        { "remove_then_set", { { Edit::NULLSTR, "zeldadx", "fullscreen", 0, 0, 0 }, { Edit::INT, "zeldadx", "fullscreen", 0, 2, 0 } }, 2 },
        { "value_newline", { { Edit::STR, "zeldadx", "nl", "a\nb", 0, 0 } }, 1 },
        { "name_spaces", { { Edit::STR, "zeldadx", "my key", "v", 0, 0 } }, 1 },
        { "theme_dvc", { { Edit::INT, "Theme", "dvc2_r", 0, 3, 0 }, { Edit::INT, "Theme", "dvc9_r", 0, 1, 0 } }, 2 },
    };

    for(size_t i = 0; i < sizeof(kScenarios) / sizeof(kScenarios[0]); ++i)
    {
        const Scenario &sc = kScenarios[i];

        for(int crlf = 0; crlf < 2; ++crlf)
        {
            std::string path = ctx.tmp(conf::nm("w_%s_%d.cfg", sc.tag, crlf));
            write_file(path, crlf ? with_crlf(kBase) : std::string(kBase));
            set_config_file(path.c_str());

            for(int k = 0; k < sc.n; ++k)
                apply(sc.edits[k]);

            /* Values as read back before the flush. */
            conf::Seq s(ctx, conf::nm("%s_%d_values", sc.tag, crlf));

            for(int k = 0; k < sc.n; ++k)
                probe(s, sc.edits[k].sec, sc.edits[k].name);

            s.end();
            remove(path.c_str());
            flush_config_file();
            check_contents(ctx, conf::nm("%s_%d", sc.tag, crlf), path);

            /* A second flush without changes writes nothing. */
            remove(path.c_str());
            flush_config_file();
            ctx.check_bool(conf::nm("%s_%d_reflushed", sc.tag, crlf), file_exists(path));
            park_config(ctx);
        }
    }

    /* Writing to a file that does not exist yet. */
    std::string fresh = ctx.tmp("fresh.cfg");
    remove(fresh.c_str());
    set_config_file(fresh.c_str());
    ctx.check_str("fresh_get", get_config_string("a", "b", "DEF"));
    flush_config_file();
    ctx.check_bool("fresh_untouched_not_created", file_exists(fresh));
    set_config_int("zeldadx", "fullscreen", 1);
    set_config_string(0, "glob", "g");
    set_config_string("SAVEFILE", "save_filename", "zc.sav");
    set_config_int("zeldadx", "resx", 640);
    flush_config_file();
    check_contents(ctx, "fresh", fresh);
    park_config(ctx);
}

CONF_TEST(ucfg_cfg_switch)
{
    /* set_config_file() with a changed config: is the old one written? */
    std::string a = ctx.tmp("switch_a.cfg");
    std::string b = ctx.tmp("switch_b.cfg");
    write_file(a, "[s]\nk = 1\n");
    write_file(b, "[s]\nk = 2\n");
    set_config_file(a.c_str());
    set_config_int("s", "k", 5);
    set_config_file(b.c_str());
    check_contents(ctx, "a_after_switch", a);
    ctx.check_int("b_value", get_config_int("s", "k", -1));
    set_config_file(a.c_str());
    ctx.check_int("a_value_again", get_config_int("s", "k", -1));

    /* Same file again after an external change: is it re-read? */
    write_file(a, "[s]\nk = 9\n");
    set_config_file(a.c_str());
    ctx.check_int("a_reread", get_config_int("s", "k", -1));

    /* Pointers from get_config_string stay valid until the next switch. */
    const char *p1 = get_config_string("s", "k", "DEF");
    const char *p2 = get_config_string("s", "k", "DEF");
    ctx.check_bool("same_pointer", p1 == p2);
    const char *def = "DEFAULT";
    ctx.check_bool("default_pointer", get_config_string("s", "missing", def) == def);
    ctx.check_bool("default_null", get_config_string("s", "missing", 0) == 0);
    (void)p1;

    /* A dirty config is written when the program switches to a theme file
     * and back (what the game does with user themes). */
    std::string zc = ctx.tmp("switch_zc.cfg");
    std::string theme = ctx.tmp("switch_theme.ztheme");
    write_file(zc, "[zeldadx]\nfullscreen = 0\n[Theme]\ntheme_filename = x\n");
    write_file(theme, "[Theme]\ndvc1_r = 10\n");
    set_config_file(zc.c_str());
    set_config_int("zeldadx", "fullscreen", 1);
    set_config_file(theme.c_str());
    ctx.check_int("theme_dvc1_r", get_config_int("Theme", "dvc1_r", 4));
    ctx.check_int("theme_missing", get_config_int("Theme", "dvc2_r", 7));
    set_config_file(zc.c_str());
    ctx.check_int("zc_fullscreen", get_config_int("zeldadx", "fullscreen", -1));
    check_contents(ctx, "zc_after_theme", zc);
    check_contents(ctx, "theme_after", theme);

    /* packfile_password("") as save_game_configs() sets it, and a real
     * password, around a flush. */
    set_config_int("zeldadx", "fullscreen", 2);
    packfile_password("");
    flush_config_file();
    packfile_password(NULL);
    check_contents(ctx, "zc_flushed_empty_password", zc);
    park_config(ctx);
}

CONF_TEST(ucfg_cfg_text)
{
    static const char *const kMsgs[] = { "OK", "Cancel", "Yes", "No", "unknown message", "" };
    std::string path = ctx.tmp("text.cfg");
    write_file(path, "[language]\nOK = Okay\n[system]\nlanguage = de\n");

    for(int fmt = 0; fmt < 2; ++fmt)
    {
        int old = get_uformat();
        set_uformat(fmt ? U_ASCII : U_UTF8);
        conf::Seq s(ctx, fmt ? "ascii" : "utf8");

        for(int pass = 0; pass < 2; ++pass)
        {
            if(pass)
                set_config_file(path.c_str());

            for(size_t i = 0; i < sizeof(kMsgs) / sizeof(kMsgs[0]); ++i)
            {
                const char *t = get_config_text(kMsgs[i]);
                const char *t2 = get_config_text(kMsgs[i]);
                s.add_str(conf::nm("%s same=%d is_arg=%d", str_or_null(t).c_str(), t == t2 ? 1 : 0,
                                   t == kMsgs[i] ? 1 : 0), "pass %d msg %d", pass, (int)i);
            }
        }

        park_config(ctx);
        set_uformat(old);
    }
}

namespace
{

/* What load_game_configs() and the theme code read, with the game's
 * defaults. */
struct GameKey
{
    const char *sec;
    const char *name;
    int def;
};

const GameKey kGameReads[] =
{
    { "zeldadx", "window_display", 0 }, { "zeldadx", "joystick_index", 0 }, { "zeldadx", "js_stick_1_x_stick", 0 },
    { "zeldadx", "js_stick_1_x_axis", 0 }, { "zeldadx", "js_stick_1_y_axis", 1 }, { "zeldadx", "analog_movement", 1 },
    { "zeldadx", "key_a", 47 }, { "zeldadx", "key_b", 46 }, { "zeldadx", "key_s", 67 }, { "zeldadx", "key_up", 84 },
    { "zeldadx", "btn_a", 2 }, { "zeldadx", "btn_b", 1 }, { "zeldadx", "digi", 248 }, { "zeldadx", "midi", 255 },
    { "zeldadx", "sfx", 248 }, { "zeldadx", "emusic", 248 }, { "zeldadx", "pan", 1 }, { "zeldadx", "zcmusic_bufsz", 64 },
    { "zeldadx", "volkeys", 0 }, { "zeldadx", "vsync", 0 }, { "zeldadx", "throttlefps", 1 }, { "zeldadx", "translayers", 1 },
    { "zeldadx", "snapshot_format", 3 }, { "zeldadx", "name_entry_mode", 0 }, { "zeldadx", "showfps", 0 },
    { "zeldadx", "fastquit", 0 }, { "zeldadx", "clicktofreeze", 1 }, { "zeldadx", "title", 1 }, { "zeldadx", "resx", 640 },
    { "zeldadx", "resy", 480 }, { "zeldadx", "scanlines", 0 }, { "zeldadx", "load_last", 0 },
    { "zeldadx", "ss_enable", 1 }, { "zeldadx", "ss_after", 14 }, { "zeldadx", "ss_speed", 2 }, { "zeldadx", "ss_density", 3 },
    { "zeldadx", "heart_beep", 1 }, { "zeldadx", "gui_colorset", 0 }, { "zeldadx", "use_sfx_dat", 1 },
    { "zeldadx", "fullscreen", 1 }, { "zeldadx", "doublebuffer", 0 }, { "zeldadx", "triplebuffer", 0 },
    { "zeldadx", "color_depth", 8 }, { "zeldadx", "frame_rest_suggest", 0 }, { "zeldadx", "force_exit", 0 },
    { "zeldadx", "debug_console", 0 }, { "zeldadx", "zc_win_proc_fix", 0 }, { "graphics", "disable_direct_updating", 1 },
    { "zeldadx", "use_dwm_flush", 0 }, { "zeldadx", "midi_patch_fix", 0 }, { "zeldadx", "save_indicator", 0 },
    { "zeldadx", "zc_192b163_compatibility", 0 }, { "CONSOLE", "ZScript_Debugger", 0 }, { "zeldadx", "epilepsy_flash_reduction", 0 },
    { "Theme", "dvc1_r", 4 }, { "Theme", "dvc2_r", 16 * 63 / 255 }, { "Theme", "dvc8_b", 35 }, { "Theme", "jcbox", 4 },
    { "Theme", "jcselfg", 6 }, { "zquest", "open_last_quest", 0 }, { "zquest", "small", 0 }, { "zquest", "layer_mask", -1 },
    { "Compiler", "compile_audio_volume", 100 }, { "zquest", "keyboard_repeat_delay", 300 }
};

/* save_game_configs(): the keys it writes, in its order. */
const char *const kGameWrites[] =
{
    "window_display", "joystick_index", "js_stick_1_x_stick", "js_stick_1_x_axis", "js_stick_1_x_offset",
    "js_stick_1_y_stick", "js_stick_1_y_axis", "js_stick_1_y_offset", "js_stick_2_x_stick", "js_stick_2_x_axis",
    "js_stick_2_x_offset", "js_stick_2_y_stick", "js_stick_2_y_axis", "js_stick_2_y_offset", "analog_movement",
    "key_cheatmod_a1", "key_cheatmod_a2", "key_cheatmod_b1", "key_cheatmod_b2", "key_a", "key_b", "key_s", "key_l",
    "key_r", "key_p", "key_ex1", "key_ex2", "key_ex3", "key_ex4", "key_up", "key_down", "key_left", "key_right",
    "btn_a", "btn_b", "btn_s", "btn_m", "btn_l", "btn_r", "btn_p", "btn_ex1", "btn_ex2", "btn_ex3", "btn_ex4",
    "btn_up", "btn_down", "btn_left", "btn_right", "epilepsy_flash_reduction", "digi", "midi", "sfx", "emusic",
    "pan", "zcmusic_bufsz", "volkeys", "vsync", "throttlefps", "translayers", "snapshot_format", "name_entry_mode",
    "showfps", "fastquit", "clicktofreeze", "title", "resx", "resy", "scanlines", "load_last",
    "linear_quest_progression"
};

const char *const kGameWrites2[] =
{
    "ss_enable", "ss_after", "ss_speed", "ss_density", "heart_beep", "gui_colorset", "use_sfx_dat", "fullscreen",
    "doublebuffer", "triplebuffer", "color_depth", "frame_rest_suggest", "force_exit"
};

void game_save(conf::Ctx &ctx, int seed)
{
    conf::Rng r((conf::u64)(seed + 1));

    for(size_t i = 0; i < sizeof(kGameWrites) / sizeof(kGameWrites[0]); ++i)
        set_config_int("zeldadx", kGameWrites[i], i == 0 ? get_config_int("zeldadx", "window_display", 0)
                                                         : r.range(-2, 300));

    set_config_string("zeldadx", "win_qst_dir", seed ? "C:/quests/" : "");
    set_config_string("SAVEFILE", "save_filename", "zc.sav");

    for(size_t i = 0; i < sizeof(kGameWrites2) / sizeof(kGameWrites2[0]); ++i)
        set_config_int("zeldadx", kGameWrites2[i], r.range(0, 20));

    set_config_int("CONSOLE", "ZScript_Debugger", r.range(0, 1));
    set_config_int("zeldadx", "debug_console", 0);
    set_config_int("zeldadx", "zc_win_proc_fix", 0);
    set_config_int("graphics", "disable_direct_updating", 1);
    set_config_int("zeldadx", "use_dwm_flush", 0);
    set_config_int("zeldadx", "midi_patch_fix", 0);
    set_config_int("zeldadx", "debug_console", 1);
    set_config_int("zeldadx", "save_indicator", 0);
    set_config_int("zeldadx", "zc_192b163_compatibility", 0);
    packfile_password("");
    flush_config_file();
    packfile_password(NULL);
    (void)ctx;
}

} // namespace

CONF_TEST(ucfg_cfg_game_files)
{
    static const char *const kFiles[] =
    {
        "zc.cfg", "zquest.cfg", "../../output/common/zc.cfg", "../../output/common/zquest.cfg",
        "themes/190.ztheme", "themes/legacy192.ztheme", "themes/darkblue.ztheme", "themes/mooshmood.ztheme"
    };

    for(size_t f = 0; f < sizeof(kFiles) / sizeof(kFiles[0]); ++f)
    {
        std::string src;

        if(!ctx.have_data(kFiles[f]) || !read_file(ctx.data(kFiles[f]), src))
        {
            ctx.note("no %s", kFiles[f]);
            continue;
        }

        std::string path = ctx.tmp(conf::nm("game_%d.cfg", (int)f));
        write_file(path, src);
        ctx.checkf(conf::nm("file%d_source", (int)f), "%u bytes %s", (unsigned)src.size(),
                   conf::hash_text(conf::hash_bytes(src.data(), src.size())).c_str());
        set_config_file(path.c_str());

        /* Everything in it, through every getter. */
        conf::Seq all(ctx, conf::nm("file%d_all", (int)f));
        const char **secs = 0;
        int ns = list_config_sections(&secs);
        all.add_int(ns, "sections");

        for(int i = -1; i < ns; ++i)
        {
            const char *sec = i < 0 ? 0 : secs[i];
            const char **names = 0;
            int n = list_config_entries(sec, &names);
            all.add_int(n, "entries of %s", sec ? sec : "(null)");

            for(int k = 0; k < n; ++k)
                probe(all, sec, names[k]);

            free_config_entries(&names);
        }

        free_config_entries(&secs);
        all.end();

        /* What the game reads. */
        conf::Seq rd(ctx, conf::nm("file%d_game_reads", (int)f));

        for(size_t i = 0; i < sizeof(kGameReads) / sizeof(kGameReads[0]); ++i)
            rd.add_int(get_config_int(kGameReads[i].sec, kGameReads[i].name, kGameReads[i].def),
                       "%s/%s", kGameReads[i].sec, kGameReads[i].name);

        rd.add_str(str_or_null(get_config_string("zeldadx", "win_qst_dir", "")).c_str(), "win_qst_dir");
        rd.add_str(str_or_null(get_config_string("SAVEFILE", "save_filename", "zc.sav")).c_str(), "save_filename");
        rd.add_str(str_or_null(get_config_string("Theme", "theme_filename", "")).c_str(), "theme_filename");
        rd.add_str(str_or_null(get_config_string("zeldadx", "debug", "")).c_str(), "debug");
        rd.end();

        /* What save_game_configs() writes, twice (the second save changes
         * values that the first one added). */
        game_save(ctx, 0);
        check_contents(ctx, conf::nm("file%d_saved", (int)f), path);
        game_save(ctx, 1);
        check_contents(ctx, conf::nm("file%d_saved2", (int)f), path);

        /* Reading the saved file back. */
        set_config_file(path.c_str());
        conf::Seq back(ctx, conf::nm("file%d_reread", (int)f));

        for(size_t i = 0; i < sizeof(kGameReads) / sizeof(kGameReads[0]); ++i)
            back.add_int(get_config_int(kGameReads[i].sec, kGameReads[i].name, kGameReads[i].def),
                         "%s/%s", kGameReads[i].sec, kGameReads[i].name);

        back.end();
        park_config(ctx);
    }
}

/* The player reads zc.cfg in U_UTF8 (set_config_file before set_uformat),
 * then switches to U_ASCII and saves there; paths may hold non-ASCII bytes
 * of the system code page. */
CONF_TEST(ucfg_cfg_game_formats)
{
    int old = get_uformat();
    std::string path = ctx.tmp("fmt_zc.cfg");
    std::string theme = ctx.tmp("fmt_theme.ztheme");
    write_file(path, "[zeldadx]\nwin_qst_dir = C:/Users/Jos\xc3\xa9/quests/\nfullscreen = 0\n"
                     "latin = caf\xe9\n[SAVEFILE]\nsave_filename = zc.sav\n");
    write_file(theme, "[Theme]\ndvc1_r = 12\nname = th\xc3\xa9me\n");

    set_uformat(U_UTF8);
    set_config_file(path.c_str());
    ctx.check_str("utf8_dir", get_config_string("zeldadx", "win_qst_dir", ""));
    ctx.check_str("utf8_latin", get_config_string("zeldadx", "latin", ""));
    set_uformat(U_ASCII);
    ctx.check_str("ascii_dir_loaded_in_utf8", get_config_string("zeldadx", "win_qst_dir", ""));
    set_config_string("zeldadx", "win_qst_dir", "C:/Users/Jos\xe9/quests/");
    set_config_int("zeldadx", "fullscreen", 1);
    packfile_password("");
    flush_config_file();
    packfile_password(NULL);
    check_contents(ctx, "saved_ascii", path);

    /* a theme read in U_ASCII, then back to zc.cfg */
    set_config_int("zeldadx", "fullscreen", 2);
    set_config_file(theme.c_str());
    ctx.check_int("theme_dvc1_r", get_config_int("Theme", "dvc1_r", 4));
    ctx.check_str("theme_name", get_config_string("Theme", "name", ""));
    set_config_file(path.c_str());
    ctx.check_str("ascii_dir_reread", get_config_string("zeldadx", "win_qst_dir", ""));
    ctx.check_int("ascii_fullscreen", get_config_int("zeldadx", "fullscreen", -1));
    check_contents(ctx, "after_theme", path);

    /* the next start of the game */
    set_uformat(U_UTF8);
    set_config_file(path.c_str());
    ctx.check_str("next_utf8_dir", get_config_string("zeldadx", "win_qst_dir", ""));
    ctx.check_str("next_utf8_latin", get_config_string("zeldadx", "latin", ""));
    park_config(ctx);
    set_uformat(old);
}

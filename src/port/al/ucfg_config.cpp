// zcport ucfg module: the config file routines (config.h).
//
// Reads, looks up and writes back exactly like the reference Allegro 4.4.2
// build, as measured by tests/conformance/test_ucfg_config*.cpp:
//
// Reading. The whole file is read; when it holds bytes that need converting
// from UTF-8 to the current text format, it is converted as one string
// (which ends at the first character that decodes to zero). It is split
// into lines at '\n' or '\r' (the pair "\r\n" or "\n\r" counts once),
// stepping through it character by character in the current format. A line
// that is blank, starts with '#' (after white space) or with '=' is kept
// verbatim as a comment; otherwise the name runs up to white space or '=',
// then white space and '=' characters are skipped and the rest, with
// trailing white space removed, is the value. A name of the form "[...]"
// starts a section (it may be followed by text that is kept).
//
// Lookups compare section and variable names case-insensitively; a section
// name may be given with or without brackets; variables before the first
// section header form the NULL/"" section; every header with the name opens
// the section again; the first match wins; an empty value means "not set".
// Names and sections starting with '#' live in a store that is never read
// from or written to a file.
//
// Writing. Setting a variable marks the config changed (even to the same
// value or removing a missing one). An existing line is updated in place
// (keeping the name's spelling) or removed for a NULL/empty value. A new
// global variable goes to the top of the file; a new variable of an
// existing section goes after the run of non-blank lines that follows its
// first header (so after the following sections when no blank line comes
// first, where lookups no longer see it - as in the reference); a new
// section is appended, after a blank line unless the file is empty or its
// last line has an empty value. Lines are written as "name = value"
// (headers as "[name]text", comments verbatim), converted from the current
// format to UTF-8, with "\r\n" line ends. The config is written by
// flush_config_file(), when another config replaces it, by
// pop_config_state() and at allegro_exit(); one from set_config_data() is
// never written. Before set_config_file() the config is allegro.cfg next to
// the executable (written there if it is changed).
//
// Written for zcport; no Allegro source was used.

#include "ucfg_internal.h"

#include <allegro/internal/aintern.h>
#include "zcport/core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

namespace
{

// ---- data model -------------------------------------------------------------------

// One line of a config file. Comment lines (blank lines included) have no
// name and keep their text in data.
struct Entry
{
    bool named;
    std::string name;
    std::string data;
};

struct Config
{
    std::vector<std::unique_ptr<Entry>> entries;
    std::string filename;
    bool has_file = false;
    bool dirty = false;
};

struct Hook
{
    std::string section;        // "[name]"
    int (*intgetter)(const char *, int);
    const char *(*stringgetter)(const char *, const char *);
    void (*stringsetter)(const char *, const char *);
};

std::unique_ptr<Config> g_main;
std::unique_ptr<Config> g_override;
std::unique_ptr<Config> g_hidden;           // '#' names: never on disk
std::vector<std::unique_ptr<Config>> g_stack;
std::vector<Hook> g_hooks;
std::vector<char *> g_texts;                // get_config_text() results
std::vector<char> g_argv_buf;
std::vector<char *> g_argv;
bool g_exit_registered = false;

void config_exit(void);

void register_exit()
{
    if(!g_exit_registered)
    {
        _add_exit_func(config_exit, "config_exit");
        g_exit_registered = true;
    }
}

// ---- strings in the current format -------------------------------------------------

std::string encode(const std::vector<int> &cps, size_t from, size_t to)
{
    std::string s;
    char tmp[16];

    for(size_t i = from; i < to; ++i)
    {
        int w = usetc(tmp, cps[i]);
        s.append(tmp, (size_t)w);
    }

    s.append(4, '\0');               // a terminator in every format
    s.resize(s.size() - 4);
    return s;
}

std::vector<int> decode(const char *s)
{
    std::vector<int> cps;
    const char *p = s;
    int c;

    while((c = ugetxc(&p)) != 0)
        cps.push_back(c);

    return cps;
}

int first_char(const std::string &s)
{
    return s.empty() ? 0 : ugetc(s.c_str());
}

int last_char(const std::string &s)
{
    return s.empty() ? 0 : ugetat(s.c_str(), -1);
}

bool is_header(const Entry &e)
{
    return e.named && first_char(e.name) == '[' && last_char(e.name) == ']';
}

bool is_blank(const Entry &e)
{
    return !e.named && e.data.empty();
}

bool starts_with_hash(const char *s)
{
    return s && ugetc(s) == '#';
}

// "[name]" for a section argument; "" for the global section.
std::string section_key(const char *section)
{
    if(!section || !ugetc(section))
        return std::string();

    std::string key;
    char tmp[16];

    if(ugetc(section) != '[')
        key.append(tmp, (size_t)usetc(tmp, '['));

    key += section;

    if(ugetat(section, -1) != ']')
        key.append(tmp, (size_t)usetc(tmp, ']'));

    return key;
}

// ---- parsing -----------------------------------------------------------------------

} // namespace (the flag below is set from outside the library)
int zcport_config_value_notes = 0;
namespace {

std::unique_ptr<Entry> parse_line(const std::vector<int> &cps)
{
    std::unique_ptr<Entry> e(new Entry());
    size_t n = cps.size();
    size_t i = 0;

    while(i < n && uisspace(cps[i]))
        ++i;

    size_t j = i;

    if(i < n && cps[i] != '#')
        while(j < n && !uisspace(cps[j]) && cps[j] != '=')
            ++j;

    if(j == i)
    {
        e->named = false;
        e->data = encode(cps, 0, n);
        return e;
    }

    size_t k = j;

    while(k < n && (uisspace(cps[k]) || cps[k] == '='))
        ++k;

    size_t end = n;

    while(end > k && uisspace(cps[end - 1]))
        --end;

    e->named = true;
    e->name = encode(cps, i, j);
    e->data = encode(cps, k, end);

    // Unified engine: its config files can carry notes after a value ("x = 1 #? note").
    if(zcport_config_value_notes)
    {
        size_t note = e->data.find(" #?");
        if(note != std::string::npos)
        {
            e->data.erase(note);
            while(!e->data.empty() && (e->data.back() == ' ' || e->data.back() == '\t'))
                e->data.pop_back();
        }
    }
    return e;
}

std::unique_ptr<Config> parse_text(const char *data, size_t length)
{
    std::unique_ptr<Config> cfg(new Config());
    std::string raw(data, length);

    // (padding so that a lead byte at the very end decodes safely)
    std::string text;
    size_t end;

    if(need_uconvert(raw.c_str(), U_UTF8, U_CURRENT))
    {
        int size = uconvert_size(raw.c_str(), U_UTF8, U_CURRENT);
        std::vector<char> buf((size_t)size + 8, 0);
        do_uconvert(raw.c_str(), U_UTF8, &buf[0], U_CURRENT, size);
        end = (size_t)(size - ucwidth(0));
        text.assign(&buf[0], end);
    }
    else
    {
        text = raw;
        end = length;
    }

    text.append(16, '\0');
    const char *t = text.c_str();
    size_t pos = 0;

    while(pos < end)
    {
        std::vector<int> cps;
        bool ended = false;     // a zero character ends what the line holds

        while(pos < end)
        {
            int c = ugetc(t + pos);

            if(c == '\n' || c == '\r')
                break;

            if(!c)
                ended = true;
            else if(!ended)
                cps.push_back(c);

            pos += (size_t)uwidth(t + pos);
        }

        if(pos < end)
        {
            int c = ugetc(t + pos);
            pos += (size_t)uwidth(t + pos);

            if(pos < end)
            {
                int d = ugetc(t + pos);

                if((c == '\n' && d == '\r') || (c == '\r' && d == '\n'))
                    pos += (size_t)uwidth(t + pos);
            }
        }

        cfg->entries.push_back(parse_line(cps));
    }

    return cfg;
}

std::unique_ptr<Config> load_file(const char *filename)
{
    std::unique_ptr<Config> cfg;
    std::string data;
    FILE *f = fopen(filename, "rb");

    if(f)
    {
        char buf[4096];
        size_t n;

        while((n = fread(buf, 1, sizeof(buf), f)) > 0)
            data.append(buf, n);

        fclose(f);
    }

    cfg = parse_text(data.data(), data.size());
    cfg->filename = filename;
    cfg->has_file = true;
    return cfg;
}

// The config the program starts with: allegro.cfg next to the executable.
std::unique_ptr<Config> load_default()
{
    char exe[1024];
    exe[0] = 0;
    get_executable_name(exe, (int)sizeof(exe));
    std::string path = exe;
    size_t slash = path.find_last_of("/\\");
    path = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    path += "allegro.cfg";
    return load_file(path.c_str());
}

Config &main_config()
{
    register_exit();

    if(!g_main)
        g_main = load_default();

    return *g_main;
}

Config &hidden_config()
{
    if(!g_hidden)
        g_hidden.reset(new Config());

    return *g_hidden;
}

// ---- writing -----------------------------------------------------------------------

void write_config(Config &cfg)
{
    if(!cfg.dirty || !cfg.has_file)
        return;

    std::string out;
    bool utf8 = get_uformat() == U_UTF8;

    for(const std::unique_ptr<Entry> &e : cfg.entries)
    {
        std::string line;

        if(e->named)
        {
            line = e->name;

            if(first_char(e->name) != '[')
                line += " = ";

            line += e->data;
        }
        else
            line = e->data;

        line += '\n';

        if(!utf8)
        {
            int size = uconvert_size(line.c_str(), U_CURRENT, U_UTF8);
            std::vector<char> buf((size_t)size + 8, 0);
            do_uconvert(line.c_str(), U_CURRENT, &buf[0], U_UTF8, size);
            line = &buf[0];
        }

        for(char ch : line)
        {
            if(ch == '\n')
                out += '\r';

            out += ch;
        }
    }

    FILE *f = fopen(cfg.filename.c_str(), "wb");

    if(f)
    {
        fwrite(out.data(), 1, out.size(), f);
        fclose(f);
    }

    cfg.dirty = false;
}

// ---- lookups -----------------------------------------------------------------------

Entry *find_entry(Config &cfg, const std::string &key, const char *name)
{
    bool in = key.empty();

    for(const std::unique_ptr<Entry> &e : cfg.entries)
    {
        if(is_header(*e))
        {
            in = !key.empty() && ustricmp(e->name.c_str(), key.c_str()) == 0;
            continue;
        }

        if(in && e->named && ustricmp(e->name.c_str(), name) == 0)
            return e.get();
    }

    return nullptr;
}

const Hook *find_hook(const char *section)
{
    std::string key = section_key(section);

    for(const Hook &h : g_hooks)
        if(ustricmp(h.section.c_str(), key.c_str()) == 0)
            return &h;

    return nullptr;
}

bool is_hidden(const char *section, const char *name)
{
    return starts_with_hash(section) || starts_with_hash(name);
}

// The value of a variable, or nullptr when it is missing or empty.
const char *lookup(const char *section, const char *name)
{
    if(!name)
        return nullptr;

    std::string key = section_key(section);
    Entry *e = nullptr;

    if(is_hidden(section, name))
        e = find_entry(hidden_config(), key, name);
    else
    {
        if(g_override)
            e = find_entry(*g_override, key, name);

        if(!e)
            e = find_entry(main_config(), key, name);
    }

    if(!e || e->data.empty())
        return nullptr;

    return e->data.c_str();
}

void store(Config &cfg, const char *section, const char *name, const char *val)
{
    std::string key = section_key(section);
    Entry *e = find_entry(cfg, key, name);
    bool has_value = val && ugetc(val);

    if(e)
    {
        if(has_value)
            e->data = val;
        else
        {
            for(size_t i = 0; i < cfg.entries.size(); ++i)
                if(cfg.entries[i].get() == e)
                {
                    cfg.entries.erase(cfg.entries.begin() + (long)i);
                    break;
                }
        }
    }
    else if(has_value)
    {
        std::unique_ptr<Entry> v(new Entry());
        v->named = true;
        v->name = name;
        v->data = val;

        if(key.empty())
            cfg.entries.insert(cfg.entries.begin(), std::move(v));
        else
        {
            size_t n = cfg.entries.size();
            size_t h = n;

            for(size_t i = 0; i < n; ++i)
                if(is_header(*cfg.entries[i]) && ustricmp(cfg.entries[i]->name.c_str(), key.c_str()) == 0)
                {
                    h = i;
                    break;
                }

            if(h < n)
            {
                size_t p = h;

                while(p + 1 < n && !is_blank(*cfg.entries[p + 1]))
                    ++p;

                cfg.entries.insert(cfg.entries.begin() + (long)(p + 1), std::move(v));
            }
            else
            {
                // (a last line with an empty value counts as blank)
                if(n > 0 && !cfg.entries[n - 1]->data.empty())
                {
                    std::unique_ptr<Entry> blank(new Entry());
                    blank->named = false;
                    cfg.entries.push_back(std::move(blank));
                }

                std::unique_ptr<Entry> header(new Entry());
                header->named = true;
                header->name = key;
                cfg.entries.push_back(std::move(header));
                cfg.entries.push_back(std::move(v));
            }
        }
    }

    cfg.dirty = true;
}

void config_exit(void)
{
    if(g_main)
        write_config(*g_main);

    if(g_override)
        write_config(*g_override);

    g_main.reset();
    g_override.reset();
    g_hidden.reset();
    g_stack.clear();
    g_hooks.clear();

    for(char *t : g_texts)
        free(t);

    g_texts.clear();
    g_exit_registered = false;
}

std::string format_int(const char *fmt, int v)
{
    char buf[64];
    uszprintf(buf, (int)sizeof(buf), uconvert_ascii(fmt, nullptr), v);
    return buf;
}

} // namespace

extern "C"
{

// ---- config sources ----------------------------------------------------------------

void set_config_file(const char *filename)
{
    register_exit();

    if(g_main)
        write_config(*g_main);

    g_main = load_file(filename ? filename : "");
}

void set_config_data(const char *data, int length)
{
    register_exit();

    if(g_main)
        write_config(*g_main);

    g_main = parse_text(data ? data : "", data && length > 0 ? (size_t)length : 0);
}

void override_config_file(const char *filename)
{
    register_exit();

    if(g_override)
        write_config(*g_override);

    g_override.reset();

    if(filename)
        g_override = load_file(filename);
}

void override_config_data(const char *data, int length)
{
    register_exit();

    if(g_override)
        write_config(*g_override);

    g_override.reset();

    if(data)
        g_override = parse_text(data, length > 0 ? (size_t)length : 0);
}

void flush_config_file(void)
{
    if(g_main)
        write_config(*g_main);
}

void push_config_state(void)
{
    main_config();
    g_stack.push_back(std::move(g_main));
    g_main = load_default();
}

void pop_config_state(void)
{
    if(g_stack.empty())
        return;

    if(g_main)
        write_config(*g_main);

    g_main = std::move(g_stack.back());
    g_stack.pop_back();
}

void reload_config_texts(const char *new_language)
{
    if(new_language)
        set_config_string("system", "language", new_language);
}

void hook_config_section(const char *section, int (*intgetter)(const char *, int),
                         const char *(*stringgetter)(const char *, const char *),
                         void (*stringsetter)(const char *, const char *))
{
    std::string key = section_key(section);

    for(size_t i = 0; i < g_hooks.size(); ++i)
        if(ustricmp(g_hooks[i].section.c_str(), key.c_str()) == 0)
        {
            if(!intgetter && !stringgetter && !stringsetter)
                g_hooks.erase(g_hooks.begin() + (long)i);
            else
            {
                g_hooks[i].intgetter = intgetter;
                g_hooks[i].stringgetter = stringgetter;
                g_hooks[i].stringsetter = stringsetter;
            }

            return;
        }

    if(intgetter || stringgetter || stringsetter)
        g_hooks.push_back(Hook{ key, intgetter, stringgetter, stringsetter });
}

int config_is_hooked(const char *section)
{
    return find_hook(section) ? TRUE : FALSE;
}

// ---- getters -----------------------------------------------------------------------

// True when the latest get_config_* call found no value and returned its default.
static bool g_config_defaulted = false;

const char *get_config_string(const char *section, const char *name, const char *def)
{
    const Hook *h = find_hook(section);

    if(h && h->stringgetter)
        return h->stringgetter(name, def);

    const char *v = lookup(section, name);
    g_config_defaulted = (v == nullptr);
    return v ? v : def;
}

int get_config_defaulted(void)
{
    return g_config_defaulted ? 1 : 0;
}

int get_config_int(const char *section, const char *name, int def)
{
    const Hook *h = find_hook(section);

    if(h && h->intgetter)
        return h->intgetter(name, def);

    const char *v = get_config_string(section, name, nullptr);
    return v && ugetc(v) ? (int)ustrtol(v, nullptr, 0) : def;
}

int get_config_hex(const char *section, const char *name, int def)
{
    const char *v = get_config_string(section, name, nullptr);

    if(!v || !ugetc(v))
        return def;

    // Hex values above 0x7FFFFFFF (which strtol clamps to it) read as -1.
    long r = ustrtol(v, nullptr, 16);

    if(r == 0x7FFFFFFFL && ustricmp(v, uconvert_ascii("7FFFFFFF", nullptr)) != 0)
        return -1;

    return (int)r;
}

float get_config_float(const char *section, const char *name, float def)
{
    const char *v = get_config_string(section, name, nullptr);
    return v && ugetc(v) ? (float)uatof(v) : def;
}

int get_config_id(const char *section, const char *name, int def)
{
    const char *v = get_config_string(section, name, nullptr);

    if(!v || !ugetc(v))
        return def;

    char *end = nullptr;
    long n = ustrtol(v, &end, 0);

    if(end && !ugetc(end))
        return (int)n;

    // Up to four characters, upper case, padded with spaces.
    int chars[4] = { ' ', ' ', ' ', ' ' };
    const char *p = v;

    for(int i = 0; i < 4; ++i)
    {
        int c = ugetxc(&p);

        if(!c)
            break;

        chars[i] = utoupper(c);
    }

    return (int)(((unsigned)chars[0] << 24) | ((unsigned)(chars[1] & 0xFF) << 16) |
                 ((unsigned)(chars[2] & 0xFF) << 8) | (unsigned)(chars[3] & 0xFF));
}

char **get_config_argv(const char *section, const char *name, int *argc)
{
    const char *v = get_config_string(section, name, nullptr);

    if(argc)
        *argc = 0;

    if(!v)
        return nullptr;

    // Tokens separated by white space; a token starting with '#' ends the
    // list; a quoted token may hold white space.
    std::vector<int> cps = decode(v);
    std::vector<std::string> tokens;
    size_t i = 0;

    while(i < cps.size())
    {
        while(i < cps.size() && uisspace(cps[i]))
            ++i;

        if(i >= cps.size() || cps[i] == '#')
            break;

        size_t start = i;

        if(cps[i] == '"' || cps[i] == '\'')
        {
            int q = cps[i++];
            start = i;

            while(i < cps.size() && cps[i] != q)
                ++i;

            tokens.push_back(encode(cps, start, i));

            if(i < cps.size())
                ++i;

            continue;
        }

        while(i < cps.size() && !uisspace(cps[i]))
            ++i;

        tokens.push_back(encode(cps, start, i));
    }

    if(tokens.empty())
        return nullptr;

    size_t total = 0;

    for(const std::string &t : tokens)
        total += t.size() + 4;

    g_argv_buf.assign(total, '\0');
    g_argv.assign(tokens.size() + 1, nullptr);
    size_t at = 0;

    for(size_t k = 0; k < tokens.size(); ++k)
    {
        memcpy(&g_argv_buf[at], tokens[k].data(), tokens[k].size());
        g_argv[k] = &g_argv_buf[at];
        at += tokens[k].size() + 4;
    }

    if(argc)
        *argc = (int)tokens.size();

    return &g_argv[0];
}

const char *get_config_text(const char *msg)
{
    if(!msg)
        return nullptr;

    // A translation from the [language] section, else a new copy of the
    // message in the current format (a new one on every call, as the
    // reference does; they are freed at exit).
    char key[256];
    const char *v = get_config_string(uconvert_ascii("language", nullptr), uconvert_ascii(msg, key), nullptr);

    if(v)
        return v;

    int size = uconvert_size(msg, U_ASCII, U_CURRENT);
    char *copy = (char *)malloc((size_t)size);

    if(!copy)
        return msg;

    do_uconvert(msg, U_ASCII, copy, U_CURRENT, size);
    register_exit();
    g_texts.push_back(copy);
    return copy;
}

// ---- setters -----------------------------------------------------------------------

void set_config_string(const char *section, const char *name, const char *val)
{
    if(!name)
        return;

    const Hook *h = find_hook(section);

    if(h && h->stringsetter)
    {
        h->stringsetter(name, val);
        return;
    }

    if(is_hidden(section, name))
    {
        Config &hidden = hidden_config();
        hidden.has_file = true;     // takes new variables; never written
        store(hidden, section, name, val);
        hidden.dirty = false;
        return;
    }

    main_config();
    store(g_override ? *g_override : *g_main, section, name, val);
}

void set_config_int(const char *section, const char *name, int val)
{
    set_config_string(section, name, format_int("%d", val).c_str());
}

void set_config_hex(const char *section, const char *name, int val)
{
    if(val >= 0)
        set_config_string(section, name, format_int("%X", val).c_str());
    else
        set_config_string(section, name, uconvert_ascii("-1", nullptr));
}

void set_config_float(const char *section, const char *name, float val)
{
    char buf[512];
    uszprintf(buf, (int)sizeof(buf), uconvert_ascii("%f", nullptr), (double)val);
    set_config_string(section, name, buf);
}

void set_config_id(const char *section, const char *name, int val)
{
    if(val < 256)
    {
        set_config_int(section, name, val);
        return;
    }

    // The four characters, up to the first space or zero.
    char buf[32];
    int pos = 0;

    for(int shift = 24; shift >= 0; shift -= 8)
    {
        int c = (val >> shift) & 0xFF;

        if(c == 0 || c == ' ')
            break;

        pos += usetc(buf + pos, c);
    }

    usetc(buf + pos, 0);
    set_config_string(section, name, buf);
}

// ---- listing -----------------------------------------------------------------------

int list_config_entries(const char *section, const char ***names)
{
    std::string key = section_key(section);
    std::vector<const char *> found;
    Config *sources[2] = { g_override.get(), &main_config() };

    // '#' sections are listed from the store they live in
    if(starts_with_hash(section))
    {
        sources[0] = nullptr;
        sources[1] = &hidden_config();
    }

    for(Config *cfg : sources)
    {
        if(!cfg)
            continue;

        bool in = key.empty();

        for(const std::unique_ptr<Entry> &e : cfg->entries)
        {
            if(is_header(*e))
            {
                in = !key.empty() && ustricmp(e->name.c_str(), key.c_str()) == 0;
                continue;
            }

            if(!in || !e->named)
                continue;

            bool dup = false;

            for(const char *f : found)
                if(ustrcmp(f, e->name.c_str()) == 0)
                    dup = true;

            if(!dup)
                found.push_back(e->name.c_str());
        }
    }

    *names = (const char **)realloc((void *)*names, sizeof(char *) * (found.size() + 1));

    for(size_t i = 0; i < found.size(); ++i)
        (*names)[i] = found[i];

    return (int)found.size();
}

int list_config_sections(const char ***names)
{
    std::vector<const char *> found;
    Config *sources[2] = { g_override.get(), &main_config() };

    for(Config *cfg : sources)
    {
        if(!cfg)
            continue;

        for(const std::unique_ptr<Entry> &e : cfg->entries)
        {
            // ('#' sections are not listed)
            if(!is_header(*e) || ugetat(e->name.c_str(), 1) == '#')
                continue;

            bool dup = false;

            for(const char *f : found)
                if(ustrcmp(f, e->name.c_str()) == 0)
                    dup = true;

            if(!dup)
                found.push_back(e->name.c_str());
        }
    }

    *names = (const char **)realloc((void *)*names, sizeof(char *) * (found.size() + 1));

    for(size_t i = 0; i < found.size(); ++i)
        (*names)[i] = found[i];

    return (int)found.size();
}

void free_config_entries(const char ***names)
{
    if(names)
    {
        free((void *)*names);
        *names = nullptr;
    }
}

} // extern "C"

// zcport files module: file name helpers and file system queries.
//
// Separators are '/' and '\\' (OTHER_PATH_SEPARATOR), the device separator
// is ':' (DEVICE_SEPARATOR). The rules (what counts as an extension, when a
// separator is added, how canonicalize_filename and make_relative_filename
// resolve paths, truncation to the buffer size, errno) follow the reference
// build; tests/conformance/test_files_paths.cpp pins them down.
// Strings are handled byte-wise (the player runs with U_ASCII; separators
// and dots never occur inside UTF-8 sequences either).
//
// Written from scratch for zcport.

#include "files_internal.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#endif

#include <string>
#include <vector>

namespace
{

struct ErrnoKeeper
{
    int saved;
    ErrnoKeeper() : saved(errno) {}
    ~ErrnoKeeper() { errno = saved; }
};

bool is_sep(char c)
{
    return c == '/' || c == OTHER_PATH_SEPARATOR;
}

bool ends_dir(char c)
{
    return c == '/' || c == OTHER_PATH_SEPARATOR || c == DEVICE_SEPARATOR;
}

/* Copies s into dest (size bytes, NUL included), like ustrzcpy. */
char *copy_out(char *dest, const std::string &s, int size)
{
    if(size <= 0)
        return dest;

    size_t n = s.size();

    if(n > (size_t)(size - 1))
        n = (size_t)(size - 1);

    memmove(dest, s.data(), n);
    dest[n] = 0;
    return dest;
}

/* Offset of the file name part: after the last '/', '\\' or ':'. */
size_t filename_offset(const char *path)
{
    size_t n = strlen(path);

    while(n > 0 && !ends_dir(path[n - 1]))
        --n;

    return n;
}

/* Offset of the extension: after the last '.' of the file name part, or the
 * string's end. */
size_t extension_offset(const char *path)
{
    size_t len = strlen(path);
    size_t n = len;

    while(n > 0)
    {
        char c = path[n - 1];

        if(c == '.')
            return n;

        if(ends_dir(c))
            break;

        --n;
    }

    return len;
}

std::string current_dir_of_drive(int drive)
{
#ifdef _WIN32
    char buf[2048];

    if(_getdcwd(drive + 1, buf, sizeof(buf)))
        return buf;
#else
    char buf[2048];

    if(getcwd(buf, sizeof(buf)))
        return buf;
#endif
    return "";
}

int current_drive()
{
#ifdef _WIN32
    return _getdrive() - 1;
#else
    return 0;
#endif
}

#ifdef _WIN32
int map_attrib(DWORD a)
{
    int r = 0;

    if(a & FILE_ATTRIBUTE_READONLY)
        r |= FA_RDONLY;

    if(a & FILE_ATTRIBUTE_HIDDEN)
        r |= FA_HIDDEN;

    if(a & FILE_ATTRIBUTE_SYSTEM)
        r |= FA_SYSTEM;

    if(a & FILE_ATTRIBUTE_DIRECTORY)
        r |= FA_DIREC;

    if(a & FILE_ATTRIBUTE_ARCHIVE)
        r |= FA_ARCH;

    return r;
}
#endif

} /* namespace */

namespace zcfiles
{

bool os_stat(const char *filename, uint64_t *size, int *attrib, time_t *mtime)
{
    ErrnoKeeper keep;
#ifdef _WIN32
    struct _stati64 st;

    if(_stati64(filename, &st) != 0)
        return false;
#else
    struct stat st;

    if(stat(filename, &st) != 0)
        return false;
#endif

    if(size)
        *size = (st.st_mode & S_IFDIR) ? 0 : (uint64_t)st.st_size;

    if(attrib)
        *attrib = (st.st_mode & S_IFDIR) ? FA_DIREC : FA_ARCH;

    if(mtime)
        *mtime = st.st_mtime;

    return true;
}

} /* namespace zcfiles */

using namespace zcfiles;

/* ------------------------------------------------------------------------ */
/* al_findfirst / al_findnext                                               */
/* ------------------------------------------------------------------------ */

namespace
{
struct FindData
{
#ifdef _WIN32
    HANDLE h;
    WIN32_FIND_DATAA fd;
#else
    DIR *dir;
    std::string dirpath;   /* with a trailing '/', or empty for "." */
    std::string pattern;   /* the file name part, may hold * and ? */
    std::string name;      /* the current entry */
    struct stat st;
#endif
    int attrib;   /* allowed attributes */
};

#ifndef _WIN32
/* Windows file name matching, as the game expects it: case-insensitive,
 * '*' any run of characters, '?' one character, "*.*" matches every name
 * (also names without a dot). */
bool win_match(const char *p, const char *s)
{
    if(strcmp(p, "*.*") == 0)
        return true;

    while(*p)
    {
        if(*p == '*')
        {
            while(*p == '*')
                ++p;
            if(!*p)
                return true;
            for(; *s; ++s)
                if(win_match(p, s))
                    return true;
            return false;
        }
        if(!*s)
            return false;
        if(*p != '?' && tolower((unsigned char)*p) != tolower((unsigned char)*s))
            return false;
        ++p;
        ++s;
    }
    return *s == 0;
}

/* Attributes like Windows reports them: directories FA_DIREC, files
 * FA_ARCH, plus FA_RDONLY (not writable) and FA_HIDDEN (dot files). */
int posix_attrib(const char *name, const struct stat *st)
{
    int a = S_ISDIR(st->st_mode) ? FA_DIREC : FA_ARCH;
    if(!(st->st_mode & S_IWUSR))
        a |= FA_RDONLY;
    if(name[0] == '.' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0)
        a |= FA_HIDDEN;
    return a;
}

bool posix_next(FindData *d)
{
    while(struct dirent *e = readdir(d->dir))
    {
        if(!win_match(d->pattern.c_str(), e->d_name))
            continue;
        std::string full = d->dirpath + e->d_name;
        if(stat(full.c_str(), &d->st) != 0)
            continue;
        int a = posix_attrib(e->d_name, &d->st);
        if((a & ~d->attrib) != 0)
            continue;
        d->name = e->d_name;
        return true;
    }
    return false;
}
#endif

void fill_ffblk(struct al_ffblk *info, FindData *d)
{
#ifdef _WIN32
    info->attrib = map_attrib(d->fd.dwFileAttributes);
    ULARGE_INTEGER t;
    t.LowPart = d->fd.ftLastWriteTime.dwLowDateTime;
    t.HighPart = d->fd.ftLastWriteTime.dwHighDateTime;
    info->time = (time_t)((t.QuadPart - 116444736000000000ULL) / 10000000ULL);
    uint64_t sz = ((uint64_t)d->fd.nFileSizeHigh << 32) | d->fd.nFileSizeLow;
    info->size = (long)sz;
    strncpy(info->name, d->fd.cFileName, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = 0;
#else
    info->attrib = posix_attrib(d->name.c_str(), &d->st);
    info->time = d->st.st_mtime;
    info->size = (long)d->st.st_size;
    strncpy(info->name, d->name.c_str(), sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = 0;
#endif
}

#ifdef _WIN32
/* Advances to the next entry whose attributes are all in 'allowed'. */
bool find_matching(FindData *d, bool first_done)
{
    bool have = first_done;

    for(;;)
    {
        if(!have)
        {
            if(!FindNextFileA(d->h, &d->fd))
                return false;
        }

        have = false;
        int a = map_attrib(d->fd.dwFileAttributes);

        if((a & ~d->attrib) == 0)
            return true;
    }
}
#endif
} /* namespace */

extern "C" {

uint64_t al_ffblk_get_size(struct al_ffblk *info)
{
#ifdef _WIN32
    FindData *d = (FindData *)info->ff_data;

    if(d)
        return ((uint64_t)d->fd.nFileSizeHigh << 32) | d->fd.nFileSizeLow;
#else
    FindData *d = (FindData *)info->ff_data;

    if(d)
        return (uint64_t)d->st.st_size;
#endif
    return (uint64_t)info->size;
}

int al_findfirst(AL_CONST char *pattern, struct al_ffblk *info, int attrib)
{
    ErrnoKeeper keep;
    info->ff_data = NULL;
#ifdef _WIN32
    FindData *d = new FindData;
    d->attrib = attrib;
    d->h = FindFirstFileA(pattern, &d->fd);

    if(d->h == INVALID_HANDLE_VALUE)
    {
        delete d;
        return -1;
    }

    if(!find_matching(d, true))
    {
        FindClose(d->h);
        delete d;
        return -1;
    }

    info->ff_data = d;
    fill_ffblk(info, d);
    return 0;
#else
    std::string pat = pattern ? pattern : "";
    for(size_t i = 0; i < pat.size(); ++i)
        if(pat[i] == '\\')
            pat[i] = '/';
    size_t slash = pat.rfind('/');
    FindData *d = new FindData;
    d->attrib = attrib;
    d->dirpath = (slash == std::string::npos) ? std::string() : pat.substr(0, slash + 1);
    d->pattern = (slash == std::string::npos) ? pat : pat.substr(slash + 1);
    d->dir = opendir(d->dirpath.empty() ? "." : d->dirpath.c_str());

    if(!d->dir || !posix_next(d))
    {
        if(d->dir)
            closedir(d->dir);
        delete d;
        return -1;
    }

    info->ff_data = d;
    fill_ffblk(info, d);
    return 0;
#endif
}

int al_findnext(struct al_ffblk *info)
{
    ErrnoKeeper keep;
#ifdef _WIN32
    FindData *d = (FindData *)info->ff_data;

    if(!d || !find_matching(d, false))
        return -1;

    fill_ffblk(info, d);
    return 0;
#else
    FindData *d = (FindData *)info->ff_data;

    if(!d || !posix_next(d))
        return -1;

    fill_ffblk(info, d);
    return 0;
#endif
}

void al_findclose(struct al_ffblk *info)
{
#ifdef _WIN32
    FindData *d = (FindData *)info->ff_data;

    if(d)
    {
        FindClose(d->h);
        delete d;
    }
#else
    FindData *d = (FindData *)info->ff_data;

    if(d)
    {
        if(d->dir)
            closedir(d->dir);
        delete d;
    }
#endif
    info->ff_data = NULL;
}

/* ------------------------------------------------------------------------ */
/* file name helpers                                                        */
/* ------------------------------------------------------------------------ */

char *get_filename(AL_CONST char *path)
{
    return (char *)path + filename_offset(path);
}

char *get_extension(AL_CONST char *filename)
{
    return (char *)filename + extension_offset(filename);
}

void put_backslash(char *filename)
{
    size_t n = strlen(filename);

    if(n == 0 || !ends_dir(filename[n - 1]))
    {
        filename[n] = OTHER_PATH_SEPARATOR;
        filename[n + 1] = 0;
    }
}

char *fix_filename_case(char *path)
{
    /* Windows file names are case-insensitive: nothing to do */
    return path;
}

char *fix_filename_slashes(char *path)
{
    for(char *p = path; *p; ++p)
        if(*p == '/' || *p == OTHER_PATH_SEPARATOR)
            *p = OTHER_PATH_SEPARATOR;

    return path;
}

char *replace_filename(char *dest, AL_CONST char *path, AL_CONST char *filename, int size)
{
    std::string s(path, filename_offset(path));
    s += filename;
    return copy_out(dest, s, size);
}

char *replace_extension(char *dest, AL_CONST char *filename, AL_CONST char *ext, int size)
{
    size_t e = extension_offset(filename);
    std::string s(filename, e);

    if(e > 0 && filename[e - 1] == '.')
        s.erase(s.size() - 1);

    s += '.';
    s += ext;
    return copy_out(dest, s, size);
}

char *append_filename(char *dest, AL_CONST char *path, AL_CONST char *filename, int size)
{
    std::string s(path);

    if(!s.empty() && !ends_dir(s[s.size() - 1]))
        s += OTHER_PATH_SEPARATOR;

    s += filename;
    return copy_out(dest, s, size);
}

int is_relative_filename(AL_CONST char *filename)
{
    if(is_sep(filename[0]))
        return FALSE;

    if(strchr(filename, DEVICE_SEPARATOR))
        return FALSE;

    return TRUE;
}

char *canonicalize_filename(char *dest, AL_CONST char *filename, int size)
{
    ErrnoKeeper keep;
#ifndef _WIN32
    /* Unix paths: no drives (DEVICE_SEPARATOR is '\0' here). Relative names
     * get the current directory in front; repeated separators, "./" and
     * "../" are resolved; case is kept. */
    {
        std::string in = filename ? filename : "";
#ifdef __SWITCH__
        /* libnx: "sdmc:/..." is absolute, and the current directory has the
         * device in front as well. */
        if(in.find(':') == std::string::npos && (in.empty() || in[0] != '/'))
#else
        if(in.empty() || in[0] != '/')
#endif
        {
            std::string cwd = current_dir_of_drive(0);
            in = cwd + "/" + in;
        }
        std::vector<std::string> parts;
        size_t pos = 0;
        while(pos <= in.size())
        {
            size_t end = in.find('/', pos);
            if(end == std::string::npos)
                end = in.size();
            std::string part = in.substr(pos, end - pos);
            pos = end + 1;
            if(part.empty() || part == ".")
                continue;
            if(part == "..")
            {
                if(!parts.empty())
                    parts.pop_back();
                continue;
            }
            parts.push_back(part);
        }
        std::string out;
        for(size_t i = 0; i < parts.size(); ++i)
            out += "/" + parts[i];
        if(out.empty() || (!in.empty() && in[in.size() - 1] == '/'))
            out += "/";
#ifdef __SWITCH__
        /* "sdmc:" stays in front, without a '/' before it. */
        if(!parts.empty() && parts[0].find(':') != std::string::npos)
            out.erase(0, 1);
#endif
        return copy_out(dest, out, size);
    }
#else
    int drive;
    std::string rest;

    if(filename[0] && filename[1] == DEVICE_SEPARATOR)
    {
        drive = tolower((unsigned char)filename[0]) - 'a';
        rest = filename + 2;
    }
    else
    {
        drive = current_drive();
        rest = filename;
    }

    if(rest.empty() || !is_sep(rest[0]))
    {
        std::string cwd = current_dir_of_drive(drive);

        if(cwd.size() >= 2 && cwd[1] == ':')
            cwd.erase(0, 2);

        rest = cwd + OTHER_PATH_SEPARATOR + rest;
    }

    std::string s;
    s += (char)('a' + drive);
    s += DEVICE_SEPARATOR;

    for(size_t i = 0; i < rest.size(); ++i)
    {
        char c = rest[i] == '/' ? OTHER_PATH_SEPARATOR : rest[i];

        /* collapse repeated separators */
        if(c == OTHER_PATH_SEPARATOR && !s.empty() && s[s.size() - 1] == OTHER_PATH_SEPARATOR)
            continue;

        s += c;
    }

    const std::string dot = std::string(1, OTHER_PATH_SEPARATOR) + "." + OTHER_PATH_SEPARATOR;
    const std::string dotdot = std::string(1, OTHER_PATH_SEPARATOR) + ".." + OTHER_PATH_SEPARATOR;
    size_t at;

    while((at = s.find(dot)) != std::string::npos)
        s.erase(at, 2);

    while((at = s.find(dotdot)) != std::string::npos)
    {
        size_t prev = at == 0 ? std::string::npos : s.rfind(OTHER_PATH_SEPARATOR, at - 1);

        if(prev == std::string::npos)
            s.erase(at, 3);
        else
            s.erase(prev, at + 3 - prev);
    }

    return copy_out(dest, s, size);
#endif
}

char *make_absolute_filename(char *dest, AL_CONST char *path, AL_CONST char *filename, int size)
{
    char tmp[2048];
    std::string s;

    replace_filename(tmp, path, "", sizeof(tmp));
    s = tmp;
    s += filename;
    canonicalize_filename(tmp, s.c_str(), sizeof(tmp));
    return copy_out(dest, tmp, size);
}

char *make_relative_filename(char *dest, AL_CONST char *path, AL_CONST char *filename, int size)
{
    const char *p = path;
    const char *f = filename;
#ifdef _WIN32
    bool pdev = p[0] && p[1] == DEVICE_SEPARATOR;
    bool fdev = f[0] && f[1] == DEVICE_SEPARATOR;
#else
    bool pdev = false, fdev = false;   /* no drive letters on Unix */
#endif

    if(pdev != fdev)
        return NULL;

    if(pdev)
    {
        if(p[0] != f[0])
            return NULL;

        p += 2;
        f += 2;
    }

    /* the directory of 'path' */
    std::string base(p, filename_offset(p));
    std::string file(f);

    if(base.empty())
        return NULL;

    size_t i = 0;

    while(i < base.size() && i < file.size() && base[i] == file[i])
        ++i;

    /* common part: up to the last separator both share */
    size_t common = std::string::npos;

    for(size_t k = i; k > 0; --k)
    {
        if(is_sep(base[k - 1]))
        {
            common = k;
            break;
        }
    }

    if(common == std::string::npos)
        return NULL;

    std::string out;
    int ups = 0;

    for(size_t k = common; k < base.size(); ++k)
        if(is_sep(base[k]))
            ++ups;

    for(int k = 0; k < ups; ++k)
    {
        out += "..";
        out += OTHER_PATH_SEPARATOR;
    }

    std::string tail = file.substr(common);
    bool tail_has_sep = false;

    for(size_t k = 0; k < tail.size(); ++k)
    {
        if(is_sep(tail[k]))
        {
            tail[k] = OTHER_PATH_SEPARATOR;
            tail_has_sep = true;
        }
    }

    if(ups == 0 && !tail_has_sep)
    {
        out += '.';
        out += OTHER_PATH_SEPARATOR;
    }

    out += tail;
    return copy_out(dest, out, size);
}

/* ------------------------------------------------------------------------ */
/* file system queries                                                      */
/* ------------------------------------------------------------------------ */

int file_exists(AL_CONST char *filename, int attrib, int *aret)
{
    if(strchr(filename, '#') && !os_stat(filename, NULL, NULL, NULL))
    {
        ErrnoKeeper keep;
        PACKFILE *f = pack_fopen(filename, F_READ);

        if(!f)
            return FALSE;

        pack_fclose(f);

        if(aret)
            *aret = FA_ARCH | FA_RDONLY;

        return ((FA_ARCH | FA_RDONLY) & ~attrib) == 0 ? TRUE : FALSE;
    }

    struct al_ffblk info;

    if(al_findfirst(filename, &info, attrib) != 0)
        return FALSE;

    if(aret)
        *aret = info.attrib;

    al_findclose(&info);
    return TRUE;
}

int exists(AL_CONST char *filename)
{
    return file_exists(filename, FA_ARCH | FA_RDONLY, NULL);
}

uint64_t file_size_ex(AL_CONST char *filename)
{
    if(strchr(filename, '#') && !os_stat(filename, NULL, NULL, NULL))
    {
        uint64_t sz = special_size(filename);

        if(sz)
            return sz;
    }

    uint64_t size = 0;

    if(!os_stat(filename, &size, NULL, NULL))
        return 0;

    return size;
}

time_t file_time(AL_CONST char *filename)
{
    time_t t = 0;

    if(!os_stat(filename, NULL, NULL, &t))
        return 0;

    return t;
}

int delete_file(AL_CONST char *filename)
{
    ErrnoKeeper keep;

    if(remove(filename) != 0)
        return -1;

    return 0;
}

int for_each_file_ex(AL_CONST char *name, int in_attrib, int out_attrib,
                     AL_METHOD(int, callback, (AL_CONST char *filename, int attrib, void *param)), void *param)
{
    int count = 0;
#ifdef _WIN32
    int saved = errno;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(name, &fd);

    if(h == INVALID_HANDLE_VALUE)
    {
        errno = saved;
        return 0;
    }

    char buf[2048];

    do
    {
        int a = map_attrib(fd.dwFileAttributes);

        if((a & in_attrib) != in_attrib || (a & out_attrib) != 0)
            continue;

        replace_filename(buf, name, fd.cFileName, sizeof(buf));
        errno = saved;

        if(callback(buf, a, param) != 0)
            break;

        saved = errno;
        ++count;
    }
    while(FindNextFileA(h, &fd));

    FindClose(h);
    errno = saved;
#else
    (void)name;
    (void)in_attrib;
    (void)out_attrib;
    (void)callback;
    (void)param;
#endif
    return count;
}

/* ---- aintern.h helpers ------------------------------------------------------ */

int _al_file_isok(AL_CONST char *filename)
{
    (void)filename;
    return TRUE;
}

uint64_t _al_file_size_ex(AL_CONST char *filename)
{
    uint64_t size = 0;
    os_stat(filename, &size, NULL, NULL);
    return size;
}

time_t _al_file_time(AL_CONST char *filename)
{
    return file_time(filename);
}

int _al_drive_exists(int drive)
{
#ifdef _WIN32
    if(drive < 0 || drive > 31)
        return FALSE;

    return (GetLogicalDrives() >> drive) & 1 ? TRUE : FALSE;
#else
    return drive == 0;
#endif
}

int _al_getdrive(void)
{
    return current_drive();
}

void _al_getdcwd(int drive, char *buf, int size)
{
    ErrnoKeeper keep;
    copy_out(buf, current_dir_of_drive(drive), size);
}

/* ---- filename encoding / resources -------------------------------------- */

static int g_filename_encoding = U_ASCII;

void set_filename_encoding(int encoding)
{
    g_filename_encoding = encoding;
}

int get_filename_encoding(void)
{
    return g_filename_encoding;
}

int set_allegro_resource_path(int priority, AL_CONST char *path)
{
    (void)priority;
    (void)path;
    return 0;
}

int find_allegro_resource(char *dest, AL_CONST char *resource, AL_CONST char *ext, AL_CONST char *datafile,
                          AL_CONST char *objectname, AL_CONST char *envvar, AL_CONST char *subdir, int size)
{
    (void)ext;
    (void)datafile;
    (void)objectname;
    (void)envvar;
    (void)subdir;

    if(resource && exists(resource))
    {
        copy_out(dest, resource, size);
        return 0;
    }

    return -1;
}

} /* extern "C" */

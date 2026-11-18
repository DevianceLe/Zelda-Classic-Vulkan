// zplayer (ZC_PORT): the operating system side of the file selector and the
// message boxes: directory listing, drives, common folders, file times and
// the conversion between the game's file name encoding and ImGui's UTF-8.
// See ui_game.h. No Allegro here (windows.h and the Allegro names clash).
//
// The game opens files through zcport, which uses the ANSI functions on
// Windows (FindFirstFileA, fopen), so names are listed with the same
// functions: a name the game cannot open is never offered.
//
// Written from scratch for zplayer.

#include <stdint.h>
#include "ui_game.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/param.h>
#include <sys/mount.h>      // getfsstat
#elif defined(__linux__) && !defined(__ANDROID__)
#include <mntent.h>         // getmntent_r
#include <pwd.h>
#endif
#endif

namespace zcui_fs
{

#ifdef _WIN32

namespace
{

std::string convert(const char *s, UINT from, UINT to)
{
    if(!s || !*s)
        return std::string();
    int wn = MultiByteToWideChar(from, 0, s, -1, NULL, 0);
    if(wn <= 0)
        return std::string(s);
    std::wstring w((size_t)wn, L'\0');
    MultiByteToWideChar(from, 0, s, -1, &w[0], wn);
    int n = WideCharToMultiByte(to, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    if(n <= 0)
        return std::string(s);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(to, 0, w.c_str(), -1, &out[0], n, NULL, NULL);
    out.resize((size_t)n - 1);   // drop the terminator
    return out;
}

int64_t filetime_to_unix(const FILETIME &ft)
{
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    if(u.QuadPart == 0)
        return 0;
    // 100 ns intervals since 1601-01-01
    return (int64_t)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

bool is_dir(const std::string &path)
{
    DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

std::string native_to_utf8(const char *s)
{
    return convert(s, CP_ACP, CP_UTF8);
}

std::string utf8_to_native(const char *s)
{
    return convert(s, CP_UTF8, CP_ACP);
}

bool list_dir(const std::string &dir, std::vector<Entry> &out)
{
    out.clear();
    std::string pattern = dir + "*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if(h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND;   // an empty drive
    do
    {
        const char *n = fd.cFileName;
        if(n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0)))
            continue;
        Entry e;
        e.name = n;
        e.dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.hidden = (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
        e.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        e.mtime = filetime_to_unix(fd.ftLastWriteTime);
        out.push_back(e);
    }
    while(FindNextFileA(h, &fd));
    FindClose(h);
    return true;
}

std::vector<Place> drives()
{
    std::vector<Place> out;
    DWORD mask = GetLogicalDrives();
    for(int i = 0; i < 26; ++i)
    {
        if(!(mask & (1u << i)))
            continue;
        char root[4] = { (char)('A' + i), ':', '\\', 0 };
        Place p;
        p.drive = true;
        p.path = root;
        p.label = std::string(root, 2);
        UINT type = GetDriveTypeA(root);
        switch(type)
        {
        case DRIVE_REMOVABLE: p.detail = "Removable"; break;
        case DRIVE_FIXED:     p.detail = "Local Disk"; break;
        case DRIVE_REMOTE:    p.detail = "Network"; break;
        case DRIVE_CDROM:     p.detail = "CD/DVD"; break;
        case DRIVE_RAMDISK:   p.detail = "RAM Disk"; break;
        default:              p.detail = "Drive"; break;
        }
        // Volume names only for fixed disks: asking a floppy, card reader or
        // network share can stall.
        if(type == DRIVE_FIXED)
        {
            char name[MAX_PATH + 1] = { 0 };
            UINT old = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
            if(GetVolumeInformationA(root, name, sizeof(name), NULL, NULL, NULL, NULL, 0) && name[0])
                p.detail = native_to_utf8(name);
            SetErrorMode(old);
        }
        out.push_back(p);
    }
    return out;
}

std::vector<Place> user_folders()
{
    std::vector<Place> out;
    char home[MAX_PATH + 1];
    DWORD n = GetEnvironmentVariableA("USERPROFILE", home, sizeof(home));
    if(n == 0 || n >= sizeof(home))
        return out;
    static const char *const names[] = { "Desktop", "Documents", "Downloads" };
    for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        std::string path = std::string(home) + "\\" + names[i];
        if(!is_dir(path))
            continue;
        Place p;
        p.drive = false;
        p.label = names[i];
        p.path = path + "\\";
        out.push_back(p);
    }
    return out;
}

std::string format_time(int64_t t)
{
    if(t <= 0)
        return std::string();
    time_t tt = (time_t)t;
    struct tm lt;
    if(localtime_s(&lt, &tt) != 0)
        return std::string();
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &lt);
    return buf;
}

#else // POSIX

std::string native_to_utf8(const char *s)
{
    return s ? std::string(s) : std::string();
}

std::string utf8_to_native(const char *s)
{
    return s ? std::string(s) : std::string();
}

#if !defined(__SWITCH__) && !defined(__ANDROID__)

namespace
{

// Where file systems are mounted, from the system's table of mounts, without asking any
// of them: stat() of a share whose server doesn't answer blocks until it gives up, and
// the dialog with it (the Windows code avoids asking network drives for the same reason).
std::vector<std::string> mount_points()
{
    std::vector<std::string> out;
#if defined(__APPLE__)
    // the kernel's list as it stands (MNT_NOWAIT), into a buffer of our own
    int n = getfsstat(NULL, 0, MNT_NOWAIT);
    if(n > 0)
    {
        std::vector<struct statfs> buf((size_t)n + 8);     // room for a few mounted meanwhile
        n = getfsstat(&buf[0], (int)(buf.size() * sizeof(struct statfs)), MNT_NOWAIT);
        for(int i = 0; i < n; ++i)
            out.push_back(buf[i].f_mntonname);
    }
#else
    FILE *table = setmntent("/proc/self/mounts", "r");
    if(!table)
        table = setmntent("/etc/mtab", "r");
    if(table)
    {
        struct mntent m;
        char buf[8192];
        while(getmntent_r(table, &m, buf, sizeof(buf)))
            out.push_back(m.mnt_dir);
        endmntent(table);
    }
#endif
    return out;
}

// The name of mount point 'm' when it is right inside folder 'dir' (which ends with
// '/'), else "".
std::string name_inside(const std::string &dir, const std::string &m)
{
    if(m.size() <= dir.size() || m.compare(0, dir.size(), dir) != 0)
        return std::string();
    std::string name = m.substr(dir.size());
    return name.find('/') == std::string::npos ? name : std::string();
}

#if !defined(__APPLE__)
// The user's login name, for /media/<user> and /run/media/<user>, or "".
std::string user_name()
{
    const char *u = getenv("USER");
    if(!u || !*u)
    {
        struct passwd *pw = getpwuid(getuid());
        u = pw ? pw->pw_name : NULL;
    }
    if(!u || !*u || strchr(u, '/'))
        return std::string();
    return u;
}
#endif

} // namespace

#endif

bool list_dir(const std::string &dir, std::vector<Entry> &out)
{
    out.clear();
    DIR *d = opendir(dir.c_str());
    if(!d)
        return false;
#if !defined(__SWITCH__) && !defined(__ANDROID__)
    // Mount points in this folder are listed as folders without being looked into (as in
    // drives()): opening /mnt must not wait on a share there whose server is gone.
    std::vector<std::string> mounted;
    std::vector<std::string> all = mount_points();
    for(size_t i = 0; i < all.size(); ++i)
    {
        std::string name = name_inside(dir, all[i]);
        if(!name.empty())
            mounted.push_back(name);
    }
#endif
    while(struct dirent *de = readdir(d))
    {
        const char *n = de->d_name;
        if(n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0)))
            continue;
        Entry e;
        e.name = n;
        e.hidden = n[0] == '.';
#if !defined(__SWITCH__) && !defined(__ANDROID__)
        bool is_mount = false;
        for(size_t i = 0; i < mounted.size() && !is_mount; ++i)
            is_mount = mounted[i] == n;
        if(is_mount)
        {
            e.dir = true;
            e.size = 0;
            e.mtime = 0;        // shown as unknown
            out.push_back(e);
            continue;
        }
#endif
        std::string full = dir + n;
        struct stat st;
        if(stat(full.c_str(), &st) != 0)
            continue;
        e.dir = S_ISDIR(st.st_mode);
        e.size = (uint64_t)st.st_size;
        e.mtime = (int64_t)st.st_mtime;
        out.push_back(e);
    }
    closedir(d);
    return true;
}

std::vector<Place> drives()
{
    std::vector<Place> out;
    Place p;
    p.drive = true;
#if defined(__SWITCH__)
    p.label = "SD card";
    p.detail = "sdmc:";
    p.path = "sdmc:/";
#else
    p.label = "/";
    p.detail = "File System";
    p.path = "/";
#endif
    out.push_back(p);
#if !defined(__SWITCH__) && !defined(__ANDROID__)
    // Mounted volumes (USB sticks, other disks, shares), as Windows lists its drive
    // letters: on Linux what desktops mount in /media/<user>, /run/media/<user> and
    // /media and what is mounted in /mnt, on macOS what is mounted in /Volumes (the
    // start-up disk is mounted at "/", listed above). Each one right inside the first
    // of these folders that holds it, once, sorted by name within its folder.
    std::vector<std::string> roots;
#if defined(__APPLE__)
    roots.push_back("/Volumes/");
#else
    std::string user = user_name();
    if(!user.empty())
    {
        roots.push_back("/media/" + user + "/");
        roots.push_back("/run/media/" + user + "/");
    }
    roots.push_back("/media/");
    roots.push_back("/mnt/");
#endif
    std::vector<std::vector<Place> > found(roots.size());
    std::vector<std::string> all = mount_points();
    for(size_t i = 0; i < all.size(); ++i)
    {
        for(size_t r = 0; r < roots.size(); ++r)
        {
            std::string name = name_inside(roots[r], all[i]);
            if(name.empty())
                continue;       // not in this folder, or deeper down (/media/<user>/x)
            std::vector<Place> &list = found[r];
            bool twice = false;     // mounted twice at one place (autofs and what it mounts)
            for(size_t k = 0; k < list.size() && !twice; ++k)
                twice = list[k].detail == all[i];
            if(!twice)
            {
                size_t at = list.size();
                while(at > 0 && list[at - 1].label > name)
                    --at;
                Place v;
                v.drive = true;
                v.label = name;
                v.detail = all[i];
                v.path = all[i] + "/";
                list.insert(list.begin() + at, v);
            }
            break;
        }
    }
    for(size_t r = 0; r < found.size(); ++r)
        out.insert(out.end(), found[r].begin(), found[r].end());
#endif
    return out;
}

namespace
{

bool is_dir(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

#if defined(__linux__) && !defined(__ANDROID__)
// A folder from the XDG user-dirs file (~/.config/user-dirs.dirs, which desktops write
// in the user's language: XDG_DESKTOP_DIR="$HOME/Schreibtisch"), or "" when it names none.
std::string xdg_user_dir(const std::string &home, const char *key)
{
    const char *config = getenv("XDG_CONFIG_HOME");
    std::string file = (config && *config) ? std::string(config) : home + "/.config";
    file += "/user-dirs.dirs";
    FILE *f = fopen(file.c_str(), "r");
    if(!f)
        return std::string();
    std::string found;
    char line[1024];
    size_t klen = strlen(key);
    while(fgets(line, sizeof(line), f))
    {
        if(strncmp(line, key, klen) != 0 || line[klen] != '=')
            continue;
        std::string v = line + klen + 1;
        while(!v.empty() && (v[v.size() - 1] == '\n' || v[v.size() - 1] == '\r'))
            v.erase(v.size() - 1);
        if(v.size() >= 2 && v[0] == '"' && v[v.size() - 1] == '"')
            v = v.substr(1, v.size() - 2);
        if(v.compare(0, 5, "$HOME") == 0)
            v = home + v.substr(5);
        if(!v.empty() && v[0] == '/')
            found = v;
    }
    fclose(f);
    return found;
}
#endif

} // namespace

std::vector<Place> user_folders()
{
    std::vector<Place> out;
    const char *home = getenv("HOME");
    if(!home || !*home)
        return out;
    std::string h(home);
    while(h.size() > 1 && h[h.size() - 1] == '/')
        h.erase(h.size() - 1);
    Place p;
    p.drive = false;
    p.label = "Home";
    p.path = h;
    if(p.path[p.path.size() - 1] != '/')
        p.path += '/';
    out.push_back(p);
#if !defined(__SWITCH__) && !defined(__ANDROID__)
    // The usual folders, as on Windows, where they exist: on Linux where the desktop's
    // user-dirs file puts them (named in the user's language), else ~/Desktop and so on.
    static const char *const names[] = { "Desktop", "Documents", "Downloads" };
#if defined(__linux__)
    static const char *const keys[] = { "XDG_DESKTOP_DIR", "XDG_DOCUMENTS_DIR", "XDG_DOWNLOAD_DIR" };
#endif
    for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        std::string path;
#if defined(__linux__)
        path = xdg_user_dir(h, keys[i]);
#endif
        if(path.empty())
            path = h + "/" + names[i];
        while(path.size() > 1 && path[path.size() - 1] == '/')
            path.erase(path.size() - 1);
        // (user-dirs names the home folder itself for a folder the user doesn't want)
        if(path == h || !is_dir(path))
            continue;
        Place f;
        f.drive = false;
        f.label = names[i];
        f.path = path + "/";
        out.push_back(f);
    }
#endif
    return out;
}

std::string format_time(int64_t t)
{
    if(t <= 0)
        return std::string();
    time_t tt = (time_t)t;
    struct tm lt;
    if(!localtime_r(&tt, &lt))
        return std::string();
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &lt);
    return buf;
}

#endif

} // namespace zcui_fs

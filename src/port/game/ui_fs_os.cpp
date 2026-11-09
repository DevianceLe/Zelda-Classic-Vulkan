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
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
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

bool list_dir(const std::string &dir, std::vector<Entry> &out)
{
    out.clear();
    DIR *d = opendir(dir.c_str());
    if(!d)
        return false;
    while(struct dirent *de = readdir(d))
    {
        const char *n = de->d_name;
        if(n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0)))
            continue;
        std::string full = dir + n;
        struct stat st;
        if(stat(full.c_str(), &st) != 0)
            continue;
        Entry e;
        e.name = n;
        e.dir = S_ISDIR(st.st_mode);
        e.hidden = n[0] == '.';
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
    return out;
}

std::vector<Place> user_folders()
{
    std::vector<Place> out;
    const char *home = getenv("HOME");
    if(!home || !*home)
        return out;
    Place p;
    p.drive = false;
    p.label = "Home";
    p.path = std::string(home);
    if(p.path[p.path.size() - 1] != '/')
        p.path += '/';
    out.push_back(p);
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

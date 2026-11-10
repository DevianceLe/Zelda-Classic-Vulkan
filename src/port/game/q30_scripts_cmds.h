// ZASM commands from 2.55 and 3.0 that run_script doesn't handle inline. Upstream's numbers
// (q30_scripts_defs.h); behaviour ported from engine/src/zc/ffscript.cpp and scripting/types.

#ifndef ZC_Q30_SCRIPTS_CMDS_H
#define ZC_Q30_SCRIPTS_CMDS_H

#include <algorithm>
#include <ctype.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#define strnicmp strncasecmp
#endif

extern ffscript *zasm30_stream;

#include "q30_scripts_light.h"
#include "subscr.h"
#include "q30_scripts_subscr.h"
#include "q30_scripts_save.h"

// ---- bitmaps ------------------------------------------------------------------------------------
// A script bitmap reference: 0 none; 10..17 the render targets; 1000+ a bitmap the script made.

static std::map<long, BITMAP *> zasm30_bitmaps;
static long zasm30_next_bitmap = 1000;

static BITMAP *z30_bitmap(long ref)
{
    if(ref >= 10 && ref < 17)
    {
        const int rt = ref - 10;
        return rt == 0 ? scrollbuf : zscriptDrawingRenderTarget->GetBitmapPtr(rt - 1);
    }

    std::map<long, BITMAP *>::iterator it = zasm30_bitmaps.find(ref);
    return it == zasm30_bitmaps.end() ? NULL : it->second;
}

static long z30_new_bitmap(int w, int h)
{
    w = vbound(w, 1, 4096);
    h = vbound(h, 1, 4096);

    BITMAP *bmp = create_bitmap_ex(8, w, h);

    if(!bmp)
        return 0;

    clear_bitmap(bmp);
    const long id = zasm30_next_bitmap++;
    zasm30_bitmaps[id] = bmp;
    return id;
}

static void z30_free_bitmap(long ref)
{
    std::map<long, BITMAP *>::iterator it = zasm30_bitmaps.find(ref);

    if(it != zasm30_bitmaps.end())
    {
        destroy_bitmap(it->second);
        zasm30_bitmaps.erase(it);
    }
}

// ---- RNG and stack objects (a list of user objects, ids from 1) ---------------------------------

static std::map<long, unsigned int> zasm30_rngs;
static long zasm30_next_rng = 1;
static std::map<long, std::vector<long> > zasm30_stacks;
static long zasm30_next_stack = 1;

static unsigned int z30_rng_next(unsigned int &seed)
{
    seed = seed * 1103515245u + 12345u;
    return (seed >> 8) & 0x7FFFFFFF;
}

// ---- script files and directories ------------------------------------------------------------
// As upstream: a script can only reach Files/<quest name>/ beside the player.

enum { Z30_MAX_FILES = 256, Z30_MAX_DIRS = 256, Z30_MAX_ARRAY = 214748 };

struct Z30File
{
    bool used;
    FILE *f;
    std::string path;
};

struct Z30Dir
{
    bool used;
    std::string path;
    std::vector<std::string> names;
};

static Z30File z30_files[Z30_MAX_FILES];
static Z30Dir z30_dirs[Z30_MAX_DIRS];

static std::string z30_files_base()
{
    extern char *qstpath;
    std::string name = qstpath ? qstpath : "";
    const size_t slash = name.find_last_of("/\\");

    if(slash != std::string::npos)
        name = name.substr(slash + 1);

    const size_t dot = name.rfind('.');

    if(dot != std::string::npos)
        name = name.substr(0, dot);

    return "Files/" + (name.empty() ? std::string("quest") : name);
}

static std::string z30_lower(std::string s)
{
    for(size_t n = 0; n < s.size(); ++n)
        s[n] = (char)tolower((unsigned char)s[n]);

    return s;
}

// Windows ignores a trailing dot or space and reserves device names whatever the extension.
static bool z30_name_ok(const std::string &name)
{
    static const char reserved[] = " aux com1 com2 com3 com4 com5 com6 com7 com8 com9 con lpt1 lpt2 lpt3 lpt4 lpt5 lpt6 lpt7 lpt8 lpt9 nul prn ";

    if(name[name.size() - 1] == '.' || name[name.size() - 1] == ' ')
        return false;

    return std::string(reserved).find(" " + z30_lower(name.substr(0, name.find('.'))) + " ") == std::string::npos;
}

// Scripts can't create programs. The list is upstream's, from fileinfo.com's executable types.
static bool z30_extension_ok(const std::string &name)
{
    static const char banned[] = " .xlm .caction .8ck .actc .a6p .m3g .run .workflow .otm .apk .fxp .73k .0xe .exe .cmd .jsx .scar .wcm .jar .ebs2 .ipa .xap .ba_ .ac .bin .vlx .icd .elf .xbap .89k .widget .a7r .ex_ .zl9 .cgi .scr .coffee .ahk .plsc .air .ear .app .scptd .xys .hms .cyw .ebm .pwc .xqt .msl .seed .vexe .ebs .mcr .gpu .celx .wsh .frs .vxp .action .com .out .gadget .command .script .rfu .tcp .ex4 .bat .cof .phar .rxe .scb .ms .isu .fas .mlx .gpe .mrp .u3p .js .acr .epk .exe1 .jsf .rbf .rgs .vpm .ecf .hta .dld .applescript .prg .pyc .spr .nexe .server .appimage .pyo .dek .mrc .fpi .rpj .iim .vbs .pif .mel .scpt .csh .paf .ws .mm .acc .ex5 .mac .plx .snap .ps1 .vdo .mxe .gs .osx .sct .wiz .x86 .e_e .fky .azw2 .actm .cel .tiapp .thm .kix .wsf .vbe .lo .ls .tms .ezs .ds .n .esh .vbscript .arscript .qit .pex .dxl .wpm .s2a .sca .prc .shb .rbx .jse .beam .udf .mem .kx .ksh .rox .upx .mam .btm .es .asb .ipf .mio .sbs .hpf .ita .eham .ezt .dmc .qpx .ore .ncl .exopc .smm .pvd .ham .wpk ";
    const size_t dot = name.rfind('.');

    return dot == std::string::npos
           || std::string(banned).find(" " + z30_lower(name.substr(dot)) + " ") == std::string::npos;
}

// Turns a script's path into one inside the quest's folder; false if it would leave it.
static bool z30_resolve_path(const std::string &user, bool is_file, std::string &out)
{
    std::vector<std::string> parts;
    std::string cur;

    for(size_t n = 0; n <= user.size(); ++n)
    {
        const char c = n < user.size() ? user[n] : '/';

        if((unsigned char)c < 32 || strchr("<>|?*&^$#\":", c))
            return false;

        if(c != '/' && c != '\\')
        {
            cur += c;
            continue;
        }

        if(cur == "..")
        {
            if(parts.empty())
                return false;

            parts.pop_back();
        }
        else if(!cur.empty() && cur != ".")
        {
            if(!z30_name_ok(cur))
                return false;

            parts.push_back(cur);
        }

        cur.clear();
    }

    if(is_file)
    {
        const char last = user.empty() ? '/' : user[user.size() - 1];

        if(parts.empty() || last == '/' || last == '\\' || !z30_extension_ok(parts.back()))
            return false;
    }

    out = z30_files_base();

    for(size_t n = 0; n < parts.size(); ++n)
        out += "/" + parts[n];

    return true;
}

static void z30_make_dirs_for(const std::string &file)
{
    for(size_t n = file.find('/'); n != std::string::npos; n = file.find('/', n + 1))
    {
#ifdef _WIN32
        _mkdir(file.substr(0, n).c_str());
#else
        mkdir(file.substr(0, n).c_str(), 0755);
#endif
    }
}

static bool z30_path_is(const std::string &path, bool dir)
{
    struct stat info;

    if(stat(path.c_str(), &info) != 0)
        return false;

    return ((info.st_mode & S_IFMT) == S_IFDIR) == dir;
}

static Z30File *z30_file(long ref, bool need_open)
{
    if(ref < 1 || ref > Z30_MAX_FILES || !z30_files[ref - 1].used)
        return NULL;

    Z30File *f = &z30_files[ref - 1];
    return need_open && !f->f ? NULL : f;
}

static void z30_file_close(Z30File *f)
{
    if(f->f)
        fclose(f->f);

    f->f = NULL;
    f->path.clear();
}

static long z30_new_file()
{
    for(int n = 0; n < Z30_MAX_FILES; ++n)
    {
        if(!z30_files[n].used)
        {
            z30_files[n].used = true;
            z30_files[n].f = NULL;
            z30_files[n].path.clear();
            return n + 1;
        }
    }

    Z_scripterrlog("Scripts have used all %d files.\n", (int)Z30_MAX_FILES);
    return 0;
}

// fopen ends the program on some bad mode strings, so only plain ones get through.
static bool z30_mode_ok(const char *mode)
{
    if(!mode[0] || !strchr("rwa", mode[0]) || strlen(mode) > 4)
        return false;

    return strspn(mode + 1, "+bt") == strlen(mode + 1);
}

static void z30_file_open(long path_ptr, const char *mode)
{
    string user;
    std::string path;
    ArrayH::getString(path_ptr / 10000, user, 512);
    ri->d[2] = 0;
    ri->d[3] = 0;

    if(!z30_mode_ok(mode) || !z30_resolve_path(user, true, path))
    {
        Z_scripterrlog("Script tried to open a bad file path or mode: '%s', '%s'.\n", user.c_str(), mode);
        return;
    }

    Z30File *f = z30_file(Z30_REF(FILE), false);

    if(!f)
    {
        Z30_REF(FILE) = z30_new_file();
        f = z30_file(Z30_REF(FILE), false);
    }

    ri->d[3] = Z30_REF(FILE);

    if(!f)
        return;

    z30_file_close(f);

    if(strchr(mode, 'w') || strchr(mode, 'a'))
        z30_make_dirs_for(path);

    f->f = fopen(path.c_str(), mode);

    if(f->f)
    {
        f->path = path;
        ri->d[2] = 10000;
    }
    else
        Z_scripterrlog("Script failed to open path '%s'.\n", path.c_str());
}

// 3.0's arrays grow to fit what is read into them.
static void z30_fit_array(ZScriptArray &a, long size)
{
    if(zasm30_ffscript_version >= 27 && size > (long)a.Size() && size <= Z30_MAX_ARRAY)
        zasm30_resize_array(a, size);
}

// The start of every array read and write: the open file and the part of the array to use.
static Z30File *z30_array_args(ZScriptArray *&a, long &pos, long &count, bool reading)
{
    a = &ArrayH::getArray(get_register(sarg1) / 10000);
    count = get_register(sarg2) / 10000;
    pos = zc_max(ri->d[0] / 10000, 0L);
    ri->d[2] = 0;

    Z30File *f = z30_file(Z30_REF(FILE), true);

    if(!f || count == 0 || a == &INVALIDARRAY)
        return NULL;

    if(reading && count > 0)
        z30_fit_array(*a, pos + count);

    const long size = (long)a->Size();

    if(size <= 0 || pos >= size)
        return NULL;

    if(count < 0 || count > size - pos)
        count = size - pos;

    return f;
}

static void z30_file_read_chars()
{
    ZScriptArray *a;
    long pos, count;
    Z30File *f = z30_array_args(a, pos, count, true);

    if(!f)
        return;

    const long limit = pos + count;
    long q, got = 0;

    for(q = pos; q < limit; ++q)
    {
        const int c = fgetc(f->f);

        if(c <= 0 || feof(f->f) || ferror(f->f))
            break;

        (*a)[q] = c * 10000;
        ++got;
    }

    if(q >= limit)
    {
        --q;
        --got;
        ungetc((int)((*a)[q] / 10000), f->f);       // the last slot has to hold the terminator
    }

    (*a)[q] = 0;
    ri->d[2] = got * 10000;
}

static void z30_file_read_bytes()
{
    ZScriptArray *a;
    long pos, count;
    Z30File *f = z30_array_args(a, pos, count, true);

    if(!f)
        return;

    std::vector<unsigned char> data(count);
    ri->d[2] = (long)fread(&data[0], 1, count, f->f) * 10000;

    for(long q = 0; q < count; ++q)
        (*a)[pos + q] = data[q] * 10000;
}

static void z30_file_read_ints()
{
    ZScriptArray *a;
    long pos, count;
    Z30File *f = z30_array_args(a, pos, count, true);

    if(!f)
        return;

    std::vector<int> data(count);
    ri->d[2] = (long)fread(&data[0], 4, count, f->f) * 10000;

    for(long q = 0; q < count; ++q)
        (*a)[pos + q] = data[q];
}

static void z30_file_read_line()
{
    ZScriptArray &a = ArrayH::getArray(get_register(sarg1) / 10000);
    ri->d[2] = 0;
    Z30File *f = z30_file(Z30_REF(FILE), true);

    if(!f || &a == &INVALIDARRAY)
        return;

    const bool growable = zasm30_ffscript_version >= 27;
    const size_t limit = growable ? Z30_MAX_ARRAY - 1 : (a.Size() ? a.Size() - 1 : 0);
    std::string line;

    while(line.size() < limit)
    {
        const int c = fgetc(f->f);

        if(c <= 0 || feof(f->f) || ferror(f->f))
            break;

        line += (char)c;

        if(c == '\n')
            break;
    }

    z30_fit_array(a, (long)line.size() + 1);

    if(a.Size() == 0)
        return;

    for(size_t q = 0; q < line.size() && q + 1 < a.Size(); ++q)
        a[q] = (unsigned char)line[q] * 10000;

    a[zc_min((dword)line.size(), a.Size() - 1)] = 0;
    ri->d[2] = (long)line.size() * 10000;
}

static void z30_file_write_chars()
{
    ZScriptArray *a;
    long pos, count;
    Z30File *f = z30_array_args(a, pos, count, false);

    if(!f)
        return;

    long q = 0;

    for(; q < count; ++q)
    {
        const long c = (*a)[pos + q] / 10000;

        if(!c || fputc((int)c, f->f) < 0)
            break;
    }

    ri->d[2] = q * 10000;
}

static void z30_file_write_bytes()
{
    ZScriptArray *a;
    long pos, count;
    Z30File *f = z30_array_args(a, pos, count, false);

    if(!f)
        return;

    std::vector<unsigned char> data(count);

    for(long q = 0; q < count; ++q)
        data[q] = (unsigned char)((*a)[pos + q] / 10000);

    ri->d[2] = (long)fwrite(&data[0], 1, count, f->f) * 10000;
}

static void z30_file_write_ints()
{
    ZScriptArray *a;
    long pos, count;
    Z30File *f = z30_array_args(a, pos, count, false);

    if(!f)
        return;

    std::vector<int> data(count);

    for(long q = 0; q < count; ++q)
        data[q] = (int)(*a)[pos + q];

    ri->d[2] = (long)fwrite(&data[0], 4, count, f->f) * 10000;
}

static Z30Dir *z30_dir(long ref)
{
    return ref >= 1 && ref <= Z30_MAX_DIRS && z30_dirs[ref - 1].used ? &z30_dirs[ref - 1] : NULL;
}

static void z30_dir_add(Z30Dir &d, const char *name)
{
    if(strcmp(name, ".") && strcmp(name, ".."))
        d.names.push_back(name);
}

static void z30_dir_load(Z30Dir &d)
{
    d.names.clear();
#ifdef _WIN32
    _finddata_t info;
    const intptr_t find = _findfirst((d.path + "/*").c_str(), &info);

    if(find != -1)
    {
        do
            z30_dir_add(d, info.name);
        while(_findnext(find, &info) == 0);

        _findclose(find);
    }
#else
    DIR *dir = opendir(d.path.c_str());

    if(dir)
    {
        while(struct dirent *entry = readdir(dir))
            z30_dir_add(d, entry->d_name);

        closedir(dir);
    }
#endif
    std::sort(d.names.begin(), d.names.end());
}

static long z30_load_dir(const std::string &user)
{
    std::string path;

    if(!z30_resolve_path(user, false, path) || !z30_path_is(path, true))
    {
        Z_scripterrlog("Script tried to load a bad directory: '%s'.\n", user.c_str());
        return 0;
    }

    for(int n = 0; n < Z30_MAX_DIRS; ++n)
    {
        if(!z30_dirs[n].used)
        {
            z30_dirs[n].used = true;
            z30_dirs[n].path = path;
            z30_dir_load(z30_dirs[n]);
            return n + 1;
        }
    }

    Z_scripterrlog("Scripts have used all %d directories.\n", (int)Z30_MAX_DIRS);
    return 0;
}

static void z30_reset_paldatas();

static void z30_reset_script_objects()
{
    for(int n = 0; n < Z30_MAX_FILES; ++n)
    {
        z30_file_close(&z30_files[n]);
        z30_files[n].used = false;
    }

    for(int n = 0; n < Z30_MAX_DIRS; ++n)
    {
        z30_dirs[n].used = false;
        z30_dirs[n].names.clear();
    }

    zasm30_stacks.clear();
    zasm30_next_stack = 1;
    z30_reset_paldatas();
}

// A stack can hold the only reference to an array.
static void zasm30_mark_stacks(std::vector<long> &work)
{
    for(std::map<long, std::vector<long> >::iterator it = zasm30_stacks.begin(); it != zasm30_stacks.end(); ++it)
        for(size_t n = 0; n < it->second.size(); ++n)
            zasm30_mark(it->second[n], work);
}

static std::vector<long> *z30_stack()
{
    std::map<long, std::vector<long> >::iterator it = zasm30_stacks.find(Z30_REF(STACK));
    return it == zasm30_stacks.end() ? NULL : &it->second;
}

// ---- registers that belong to a family -----------------------------------------------------------

static bool zasm30_family_get(const long arg, long &ret)
{
    switch(arg)
    {
    case Z30R_BITMAPWIDTH:
    {
        BITMAP *b = z30_bitmap(Z30_REF(BITMAP));
        ret = b ? b->w * 10000 : -10000;
        return true;
    }
    case Z30R_BITMAPHEIGHT:
    {
        BITMAP *b = z30_bitmap(Z30_REF(BITMAP));
        ret = b ? b->h * 10000 : -10000;
        return true;
    }
    case Z30R_CREATEBITMAP:
    {
        // Game->CreateBitmap(height, width): upstream reads rSFTEMP/rINDEX2... d1/d0.
        ret = z30_new_bitmap(ri->d[1] / 10000, ri->d[0] / 10000);
        return true;
    }
    case Z30R_ALLOCATEBITMAPR:
        ret = z30_new_bitmap(1, 1);     // bitmap->Create sizes it
        return true;
    case Z30R_DROPSETCHOOSE:
    {
        extern int select_dropitem(int item_set, int x, int y);
        ret = select_dropitem(Z30_REF(DROPSETDATA), 0, 0) * 10000;
        return true;
    }
    case Z30R_FILEPOS:
    {
        Z30File *f = z30_file(Z30_REF(FILE), true);
        ret = f ? ftell(f->f) : -10000;
        return true;
    }
    case Z30R_FILEEOF:
    {
        Z30File *f = z30_file(Z30_REF(FILE), true);
        ret = f ? (feof(f->f) ? 10000 : 0) : -10000;
        return true;
    }
    case Z30R_FILEERR:
    {
        Z30File *f = z30_file(Z30_REF(FILE), true);
        ret = f ? ferror(f->f) * 10000 : -10000;
        return true;
    }
    case Z30R_DIRECTORYSIZE:
    {
        Z30Dir *d = z30_dir(Z30_REF(DIRECTORY));
        ret = d ? (long)d->names.size() * 10000 : 0;
        return true;
    }
    case Z30R_STACKSIZE:
    {
        std::vector<long> *st = z30_stack();
        ret = st ? (long)st->size() : -10000;
        return true;
    }
    case Z30R_STACKFULL:
        ret = z30_stack() ? 0 : -10000;     // only memory limits a stack here
        return true;
    default:
        ret = 0;
        return true;
    }
}

static bool zasm30_family_set(const long, const long)
{
    return true;
}

static void z30_list_del(sprite_list &list, long uid)
{
    for(int i = 0; i < list.Count(); ++i)
    {
        if(list.spr(i)->getUID() == uid)
        {
            list.del(i);
            return;
        }
    }
}

int q30_enemy_slide(enemy *e);
void q30_enemy_fire(enemy *e);
void q30_enemy_constant_walk(enemy *e, int rate, int homing, int special);
void q30_enemy_constant_walk_8(enemy *e, int rate, int homing, int special);

// ---- paldata objects ----------------------------------------------------------------------------
// Upstream's user_paldata. Colours are in the script's own range: 0-255, or 0-63 with
// qr_SCRIPTS_6_BIT_COLOR. This engine's palette and quest colour data are 6-bit.

enum { Z30_QR_SCRIPTS_6_BIT_COLOR = 480, Z30_PALDATA_COLORS = 256, Z30_MAX_PALDATAS = 4096 };

struct Z30PalData
{
    RGB c[Z30_PALDATA_COLORS];
    bool used[Z30_PALDATA_COLORS];

    Z30PalData() { memset(this, 0, sizeof(*this)); }
};

static std::map<long, Z30PalData> zasm30_paldatas;
static long zasm30_next_paldata = 1;

static int z30_color_max() { return q30_quest_rule(Z30_QR_SCRIPTS_6_BIT_COLOR) ? 63 : 255; }

// The engine's stored 6-bit component <-> the number scripts see.
static int z30_color_to_script(int c) { return z30_color_max() == 255 ? c * 255 / 63 : c; }
static int z30_color_from_script(int c) { return z30_color_max() == 255 ? vbound(c, 0, 255) * 63 / 255 : vbound(c, 0, 63); }

static Z30PalData *z30_paldata(long ref)
{
    std::map<long, Z30PalData>::iterator it = zasm30_paldatas.find(ref);
    return it == zasm30_paldatas.end() ? NULL : &it->second;
}

// 0 when the script already holds as many as this will keep.
static long z30_new_paldata()
{
    if(zasm30_paldatas.size() >= (size_t)Z30_MAX_PALDATAS)
    {
        Z_scripterrlog("Scripts have created the most paldata objects this engine keeps (%d).\n", (int)Z30_MAX_PALDATAS);
        return 0;
    }

    const long id = zasm30_next_paldata++;
    zasm30_paldatas[id];
    return id;
}

static int z30_round_color(double v)
{
    const double max = z30_color_max();
    return int((v < 0 ? 0 : v > max ? max : v) + 0.5);
}

// From easyrgb.com, as upstream's are.
enum { Z30_CSPACE_RGB, Z30_CSPACE_CMYK, Z30_CSPACE_HSV, Z30_CSPACE_HSV_CW, Z30_CSPACE_HSV_CCW,
       Z30_CSPACE_HSL, Z30_CSPACE_HSL_CW, Z30_CSPACE_HSL_CCW };

static void z30_rgb_to_space(const RGB &c, double *out, int space)
{
    const double upper = z30_color_max();
    const double r = c.r / upper, g = c.g / upper, b = c.b / upper;
    const double lo = zc_min(zc_min(r, g), b), hi = zc_max(zc_max(r, g), b), del = hi - lo;

    if(space == Z30_CSPACE_CMYK)
    {
        double C = 1 - r, M = 1 - g, Y = 1 - b, K = zc_min(zc_min(C, M), Y);

        if(K >= 1)
            C = M = Y = 0;
        else
        {
            C = (C - K) / (1 - K);
            M = (M - K) / (1 - K);
            Y = (Y - K) / (1 - K);
        }

        out[0] = C;
        out[1] = M;
        out[2] = Y;
        out[3] = K;
        return;
    }

    double h = 0;

    if(del != 0)
    {
        const double dr = ((hi - r) / 6 + del / 2) / del;
        const double dg = ((hi - g) / 6 + del / 2) / del;
        const double db = ((hi - b) / 6 + del / 2) / del;

        if(r == hi) h = db - dg;
        else if(g == hi) h = 1.0 / 3 + dr - db;
        else h = 2.0 / 3 + dg - dr;

        if(h < 0) ++h;
        if(h > 1) --h;
    }

    if(space == Z30_CSPACE_HSV || space == Z30_CSPACE_HSV_CW || space == Z30_CSPACE_HSV_CCW)
    {
        out[0] = h;
        out[1] = del == 0 ? 0 : del / hi;
        out[2] = hi;
        return;
    }

    const double l = (hi + lo) / 2;
    out[0] = h;
    out[1] = del == 0 ? 0 : (l < 0.5 ? del / (hi + lo) : del / (2 - hi - lo));
    out[2] = l;
}

static double z30_hue_to_rgb(double v1, double v2, double h)
{
    if(h < 0) h += 1;
    if(h > 1) h -= 1;
    if(6 * h < 1) return v1 + (v2 - v1) * 6 * h;
    if(2 * h < 1) return v2;
    if(3 * h < 2) return v1 + (v2 - v1) * (2.0 / 3 - h) * 6;
    return v1;
}

static RGB z30_rgb_from_space(const double *a, int space)
{
    const double upper = z30_color_max();
    double r = 0, g = 0, b = 0;

    if(space == Z30_CSPACE_CMYK)
    {
        r = 1 - (a[0] * (1 - a[3]) + a[3]);
        g = 1 - (a[1] * (1 - a[3]) + a[3]);
        b = 1 - (a[2] * (1 - a[3]) + a[3]);
    }
    else if(space == Z30_CSPACE_HSV || space == Z30_CSPACE_HSV_CW || space == Z30_CSPACE_HSV_CCW)
    {
        const double h = a[0], s = a[1], v = a[2];

        if(s == 0)
            r = g = b = v;
        else
        {
            double hv = h * 6;
            if(hv >= 6) hv = 0;
            const int i = int(floor(hv));
            const double p = v * (1 - s), q = v * (1 - s * (hv - i)), t = v * (1 - s * (1 - (hv - i)));

            switch(i)
            {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
            }
        }
    }
    else
    {
        const double h = a[0], s = a[1], l = a[2];

        if(s == 0)
            r = g = b = l;
        else
        {
            const double v2 = l < 0.5 ? l * (1 + s) : (l + s) - (s * l);
            const double v1 = 2 * l - v2;
            r = z30_hue_to_rgb(v1, v2, h + 1.0 / 3);
            g = z30_hue_to_rgb(v1, v2, h);
            b = z30_hue_to_rgb(v1, v2, h - 1.0 / 3);
        }
    }

    RGB out;
    out.r = z30_round_color(r * upper);
    out.g = z30_round_color(g * upper);
    out.b = z30_round_color(b * upper);
    out.filler = 0;
    return out;
}

static double z30_lerp(double a, double b, double t) { return a + (b - a) * t; }

// The hue interpolation that goes the way the colour space says around the wheel.
static double z30_wrap_lerp(double a, double b, double t, int direction)
{
    double dif = fabs(a - b);

    if(direction == 0)
    {
        if(dif > 0.5)
            dif = 1 - dif;
        direction = (a + dif == b) ? 1 : -1;
    }
    else if((direction > 0 && b < a) || (direction < 0 && b > a))
        dif = 1 - dif;

    double ret = z30_lerp(a, a + dif * direction, t);

    if(ret <= 0) ret += 1;
    else if(ret >= 1) ret -= 1;

    return ret;
}

// RGB, CMYK, HSV and HSL mix as upstream's do. Upstream's LAB and LCH spaces are mixed as RGB here.
static RGB z30_mix_color(const RGB &start, const RGB &end, double percent, int space)
{
    switch(space)
    {
    case Z30_CSPACE_CMYK:
    case Z30_CSPACE_HSV:
    case Z30_CSPACE_HSV_CW:
    case Z30_CSPACE_HSV_CCW:
    case Z30_CSPACE_HSL:
    case Z30_CSPACE_HSL_CW:
    case Z30_CSPACE_HSL_CCW:
    {
        double s[4] = { 0, 0, 0, 0 }, e[4] = { 0, 0, 0, 0 }, m[4];
        z30_rgb_to_space(start, s, space);
        z30_rgb_to_space(end, e, space);

        if(space == Z30_CSPACE_CMYK)
        {
            for(int q = 0; q < 4; ++q)
                m[q] = z30_lerp(s[q], e[q], percent);
        }
        else
        {
            const int direction = (space == Z30_CSPACE_HSV_CW || space == Z30_CSPACE_HSL_CW) ? 1
                                  : (space == Z30_CSPACE_HSV_CCW || space == Z30_CSPACE_HSL_CCW) ? -1 : 0;
            m[0] = z30_wrap_lerp(s[0], e[0], percent, direction);
            m[1] = z30_lerp(s[1], e[1], percent);
            m[2] = z30_lerp(s[2], e[2], percent);
            m[3] = 0;
        }

        return z30_rgb_from_space(m, space);
    }

    default:
    {
        RGB out;
        out.r = z30_round_color(z30_lerp(start.r, end.r, percent));
        out.g = z30_round_color(z30_lerp(start.g, end.g, percent));
        out.b = z30_round_color(z30_lerp(start.b, end.b, percent));
        out.filler = 0;
        return out;
    }
    }
}

static void z30_pal_load_cset(Z30PalData &p, int cset, int dataset)
{
    const byte *si = colordata + CSET(dataset) * 3;

    for(int q = 0; q < 16; ++q, si += 3)
    {
        const int ind = CSET(cset) + q;
        p.c[ind].r = z30_color_to_script(si[0]);
        p.c[ind].g = z30_color_to_script(si[1]);
        p.c[ind].b = z30_color_to_script(si[2]);
        p.used[ind] = true;
    }
}

static void z30_pal_load_cset_main(Z30PalData &p, int cset)
{
    for(int q = 0; q < 16; ++q)
    {
        const int ind = CSET(cset) + q;
        p.c[ind].r = z30_color_to_script(RAMpal[ind].r);
        p.c[ind].g = z30_color_to_script(RAMpal[ind].g);
        p.c[ind].b = z30_color_to_script(RAMpal[ind].b);
        p.used[ind] = true;
    }
}

// False if the cset holds what the colour data already does.
static bool z30_pal_write_cset(const Z30PalData &p, int cset, int dataset)
{
    byte *si = colordata + CSET(dataset) * 3;
    bool changed = false;

    for(int q = 0; q < 16; ++q, si += 3)
    {
        const int ind = CSET(cset) + q;

        if(!p.used[ind])
            continue;

        const byte nr = (byte)z30_color_from_script(p.c[ind].r), ng = (byte)z30_color_from_script(p.c[ind].g), nb = (byte)z30_color_from_script(p.c[ind].b);

        if(si[0] != nr || si[1] != ng || si[2] != nb)
            changed = true;

        si[0] = nr;
        si[1] = ng;
        si[2] = nb;
    }

    return changed;
}

static bool z30_pal_write_cset_main(const Z30PalData &p, int cset)
{
    bool changed = false;

    for(int q = 0; q < 16; ++q)
    {
        const int ind = CSET(cset) + q;

        if(!p.used[ind])
            continue;

        RGB col;
        col.r = (byte)z30_color_from_script(p.c[ind].r);
        col.g = (byte)z30_color_from_script(p.c[ind].g);
        col.b = (byte)z30_color_from_script(p.c[ind].b);
        col.filler = 0;

        if(RAMpal[ind].r != col.r || RAMpal[ind].g != col.g || RAMpal[ind].b != col.b)
            changed = true;

        RAMpal[ind] = col;
    }

    return changed;
}

static bool z30_valid_level(long level)
{
    return level >= 0 && level < MAXLEVELS;
}

// Reload the current DMap's level palette, if it's the one that changed.
static void z30_pal_level_refresh(long level)
{
    if(DMaps[currdmap].color == level)
    {
        loadlvlpal(level);
        currcset = level;

        if(darkroom && !get_bit(quest_rules, qr_FADE))
            loadfadepal(level * pdLEVEL + poFADE3);
    }
}

// Palette and bitmap scripts' colour commands that need the stack, in upstream's argument order.
static void z30_paldata_load_level(long level)
{
    Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));

    if(!pd || !z30_valid_level(level))
        return;

    z30_pal_load_cset(*pd, 2, level * pdLEVEL + poLEVEL);
    z30_pal_load_cset(*pd, 3, level * pdLEVEL + poLEVEL + 1);
    z30_pal_load_cset(*pd, 4, level * pdLEVEL + poLEVEL + 2);
    z30_pal_load_cset(*pd, 9, level * pdLEVEL + poLEVEL + 3);

    // Upstream's level data also holds CSets 1, 5, 7 and 8; this engine keeps those only in the main palette.
    z30_pal_load_cset(*pd, 1, 1);
    z30_pal_load_cset(*pd, 5, 5);
    z30_pal_load_cset(*pd, 7, 7);
    z30_pal_load_cset(*pd, 8, 8);
}

static void z30_paldata_write_level(long level, bool one_cset, int cset)
{
    Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));

    if(!pd || !z30_valid_level(level))
        return;

    static const int csets[4] = { 2, 3, 4, 9 };
    static const int sets[4] = { 0, 1, 2, 3 };
    bool changed = false;

    for(int q = 0; q < 4; ++q)
    {
        if(one_cset && csets[q] != cset)
            continue;

        if(z30_pal_write_cset(*pd, csets[q], level * pdLEVEL + poLEVEL + sets[q]))
            changed = true;
    }

    if(changed)
        z30_pal_level_refresh(level);
}

static void z30_paldata_write_cycle(long level, int cset)
{
    Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));

    if(!pd || !z30_valid_level(level))
        return;

    bool changed = false;

    for(int q = 4; q <= 12; ++q)
    {
        if(cset >= 0 && q != cset)
            continue;

        if(z30_pal_write_cset(*pd, q, level * pdLEVEL + poLEVEL + q))
            changed = true;
    }

    if(changed && cset >= 0)
        z30_pal_level_refresh(level);
}

static bool zasm30_paldata_get(const long arg, long &ret)
{
    Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));
    const long n = Z30_INDEX;
    ret = -10000;

    if(!pd || n < 0 || n >= Z30_PALDATA_COLORS || !pd->used[n])
        return true;

    const RGB &c = pd->c[n];

    if(arg == Z30R_PALDATACOLOR)
        ret = (long(c.r) << 16) | (long(c.g) << 8) | c.b;
    else
        ret = (arg == Z30R_PALDATAR ? c.r : arg == Z30R_PALDATAG ? c.g : c.b) * 10000;

    return true;
}

static bool zasm30_paldata_set(const long arg, const long value)
{
    Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));
    const long n = Z30_INDEX;

    if(!pd || n < 0 || n >= Z30_PALDATA_COLORS)
        return true;

    if(arg == Z30R_PALDATACOLOR)
    {
        pd->c[n].r = (byte)((value >> 16) & 0xFF);
        pd->c[n].g = (byte)((value >> 8) & 0xFF);
        pd->c[n].b = (byte)(value & 0xFF);
        pd->used[n] = true;
        return true;
    }

    if(pd->used[n])
    {
        const byte v = (byte)vbound(value / 10000, 0, z30_color_max());

        if(arg == Z30R_PALDATAR) pd->c[n].r = v;
        else if(arg == Z30R_PALDATAG) pd->c[n].g = v;
        else pd->c[n].b = v;
    }

    return true;
}

static void z30_reset_paldatas()
{
    zasm30_paldatas.clear();
    zasm30_next_paldata = 1;
}

// bitmap->ReplaceColors, ShiftColors and MaskDraw, on the script's own bitmaps.
static void z30_replace_colors(BITMAP *bmp, int newcol, int start, int end, bool shift)
{
    for(int y = 0; y < bmp->h; ++y)
    {
        for(int x = 0; x < bmp->w; ++x)
        {
            const int c = bmp->line[y][x];

            if(c >= start && c <= end)
                bmp->line[y][x] = shift ? (byte)(c + newcol) : (byte)newcol;
        }
    }
}

static void z30_mask_draw(BITMAP *dest, BITMAP *mask, int color, int start, int end)
{
    const int w = zc_min(dest->w, mask->w), h = zc_min(dest->h, mask->h);

    for(int y = 0; y < h; ++y)
    {
        for(int x = 0; x < w; ++x)
        {
            const int c = mask->line[y][x];

            if(c >= start && c <= end)
                dest->line[y][x] = (byte)color;
        }
    }
}

// ---- helpers for commands --------------------------------------------------------------------

static long z30_name_lookup(int table, const std::string &name)
{
    std::map<std::string, int>::iterator it = q30_script_names[table].find(name);
    return it == q30_script_names[table].end() ? 0 : it->second * 10000;
}

// The drawing primitive runs right away on the bitmap, with its arguments as a 2.53 command.
void zasm30_draw_now(BITMAP *bmp, int *sdci, std::string *str);

static void z30_user_bitmap_draw(int command, int base_command, int numargs)
{
    long sdci[20];
    memset(sdci, 0, sizeof(sdci));
    sdci[0] = base_command;

    for(int k = 1; k <= numargs; ++k)
        sdci[k] = SH::read_stack(SPR + (numargs - k));

    BITMAP *bmp = z30_bitmap(SH::read_stack(SPR + numargs));

    if(!bmp)
        return;

    string text;

    if(base_command == DRAWSTRINGR)
        ArrayH::getString(sdci[8] / 10000, text);

    zasm30_draw_now(bmp, (int *)sdci, &text);
}

static void z30_arrayptr_to_string(long ptr, string &out)
{
    ArrayH::getString(ptr / 10000, out, 4096);
}

// Writes a string into a script's array as upstream's ArrayH::setArray does: an array a script
// made grows to fit it first. Returns what setArray returns.
static int z30_set_string(long ptr, const std::string &s)
{
    ZScriptArray &a = ArrayH::getArray(ptr);

    if(&a != &INVALIDARRAY && a.Size() < s.size() + 1)
        zasm30_resize_array(a, (long)s.size() + 1);

    return ArrayH::setArray(ptr, s);
}

// sprintf for the variadic forms (args from PUSHVARG): the same conversions as zasm255_format,
// which reads its arguments off the stack instead.
static std::string z30_format(const std::string &format, const std::vector<long> &args)
{
    std::string out;
    size_t next = 0;

    for(size_t i = 0; i < format.size(); ++i)
    {
        if(format[i] != '%')
        {
            out += format[i];
            continue;
        }

        if(++i >= format.size())
            break;

        bool zero_fill = false;
        int min_digits = 0;

        if(format[i] == '0')
        {
            zero_fill = true;
            ++i;
        }

        while(i < format.size() && format[i] >= '0' && format[i] <= '9')
            min_digits = min_digits * 10 + (format[i++] - '0');

        if(i >= format.size())
            break;

        const char f = format[i];

        if(f == '%')
        {
            out += '%';
            continue;
        }

        const long arg = next < args.size() ? args[next] : 0;
        ++next;
        char spec[16], buf[64];
        min_digits = zc_min(min_digits, 10);
        sprintf(spec, "%%%s%dl%c", zero_fill ? "0" : "", min_digits, (f == 'x' || f == 'X') ? f : 'd');

        switch(f)
        {
        case 'd': case 'f': case 'i': case 'p':
        {
            sprintf(buf, min_digits ? spec : "%ld", arg / 10000);

            if(arg < 0 && buf[0] != '-')
                out += '-';

            out += buf;

            if(f == 'f' || (f == 'd' && (arg % 10000)))
            {
                unsigned long fraction = (arg < 0 ? (0ul - (unsigned long)arg) : (unsigned long)arg) % 10000;
                sprintf(buf, ".%04lu", fraction);
                size_t end = strlen(buf);

                while(end > 2 && buf[end - 1] == '0')
                    buf[--end] = 0;

                out += buf;
            }
            break;
        }
        case 'l':
            sprintf(buf, min_digits ? spec : "%ld", arg);
            out += buf;
            break;
        case 's':
            if(arg)
            {
                std::string text;
                ArrayH::getString(arg / 10000, text, 4096);
                out += text;
            }
            else
                out += "<NULL>";
            break;
        case 'c':
            out += (char)(arg / 10000);
            break;
        case 'x': case 'X':
            sprintf(buf, min_digits ? spec : (f == 'x' ? "%lx" : "%lX"), arg / 10000);
            out += "0x";
            out += buf;
            break;
        case 'b': case 'B':
        {
            const unsigned long bits = f == 'b' ? (unsigned long)(arg / 10000) : (unsigned long)arg;
            int digits = min_digits;

            for(int q = digits; q < 32; ++q)
                if(bits & (1ul << q))
                    digits = q + 1;

            for(int q = digits - 1; q >= 0; --q)
                out += (bits & (1ul << q)) ? '1' : '0';
            break;
        }
        }
    }

    return out;
}

FONT *get_zc_font(int index);
void textout_styled_aligned_ex(BITMAP *bmp, const FONT *f, const char *s, int x, int y, int textstyle, int alignment, int color, int shadow, int bg);

// bitmap->Dither(layer, mask, color, type, arg)
static void z30_bmp_dither()
{
    BITMAP *dest = z30_bitmap(SH::read_stack(SPR + 5));
    BITMAP *mask = z30_bitmap(SH::read_stack(SPR + 3));
    const long color = SH::read_stack(SPR + 2) / 10000;
    const long type = SH::read_stack(SPR + 1) / 10000;
    const long arg = SH::read_stack(SPR) / 10000;

    if(!dest || !mask)
        return;

    if(type < 0 || type >= Z30_DITH_MAX)
    {
        Z_scripterrlog("bitmap->Dither() used an invalid dither type: %ld. Aborting.\n", type);
        return;
    }

    z30_ditherblit(dest, mask, (byte)color, (int)type, (byte)arg, 0, 0);
}

// bitmap->DrawScreenComboFlags(layer, map, screen, x, y, rotation): each square of the screen
// filled with the palette index of its placed flag, as upstream's draw_map_cflag.
static void z30_bmp_draw_screen_flags()
{
    enum { Z30_QR_BROKEN_DRAWSCREEN_FUNCTIONS = 320 };

    BITMAP *dest = z30_bitmap(SH::read_stack(SPR + 6));
    const long map = SH::read_stack(SPR + 4) / 10000 - 1;
    const long scr = SH::read_stack(SPR + 3) / 10000;
    const int x = SH::read_stack(SPR + 2) / 10000;
    const int y = SH::read_stack(SPR + 1) / 10000;
    const int rotation = SH::read_stack(SPR) / 10000;

    if(!dest)
        return;

    const long index = map * MAPSCRS + scr;

    if(map < 0 || scr < 0 || scr >= MAPSCRS || index >= (long)TheMaps.size())
    {
        al_trace("DrawScreen: invalid map or screen index. \n");
        return;
    }

    const bool broken = q30_quest_rule(Z30_QR_BROKEN_DRAWSCREEN_FUNCTIONS);
    BITMAP *b = dest;
    int ox = x, oy = y;

    if(rotation != 0)
    {
        b = create_bitmap_ex(8, 256, 176);
        if(!b)
            return;
        clear_bitmap(b);
        ox = oy = 0;
    }

    // with the old rule, the layers' flags are drawn over layer 0's
    for(int lyr = 0; lyr < (broken ? 7 : 1); ++lyr)
    {
        const mapscr *m = &TheMaps[index];

        if(lyr)
        {
            if(!m->layermap[lyr - 1])
                continue;
            const long li = (m->layermap[lyr - 1] - 1) * MAPSCRS + m->layerscreen[lyr - 1];
            if(li < 0 || li >= (long)TheMaps.size())
                continue;
            m = &TheMaps[li];
        }

        for(int i = 0; i < 176 && i < (int)m->sflag.size(); ++i)
        {
            const int c = m->sflag[i];

            if(c || broken)
                rectfill(b, ox + ((i & 15) << 4), oy + (i & 0xF0), ox + ((i & 15) << 4) + 15, oy + (i & 0xF0) + 15, c);
        }
    }

    if(rotation != 0)
    {
        rotate_sprite(dest, b, x, y, ftofix(rotation / 360.0 * 256.0));    // degrees to allegro's 256ths
        destroy_bitmap(b);
    }
}

// bitmap->DrawString(layer, x, y, font, color, bg, format, string, opacity, shadow type, shadow colour)
static void z30_bmp_draw_string2()
{
    long a[12];

    for(int k = 1; k <= 11; ++k)
        a[k] = SH::read_stack(SPR + (11 - k));

    BITMAP *dest = z30_bitmap(SH::read_stack(SPR + 11));

    if(!dest)
        return;

    std::string text;
    ArrayH::getString(a[8] / 10000, text, 256);

    int x = a[2] / 10000;
    const int y = a[3] / 10000;
    FONT *f = get_zc_font(a[4] / 10000);
    const int color = a[5] / 10000;
    int bg = a[6] / 10000;
    const int format = a[7] / 10000;
    const int opacity = a[9] / 10000;
    const int style = a[10] / 10000;
    const int shadow = a[11] / 10000;

    if(bg < -1)
        bg = -1;

    if(opacity < 128)
    {
        const int w = zc_min(text_length(f, text.c_str()), 512);
        const int h = text_height(f);
        BITMAP *t = w > 0 && h > 0 ? create_bitmap_ex(8, w, h) : NULL;

        if(!t)
            return;

        clear_bitmap(t);
        textout_styled_aligned_ex(t, f, text.c_str(), 0, 0, style, 0, color, shadow, bg);     // left-aligned

        if(format == 2)
            x -= w;
        else if(format == 1)
            x -= w / 2;

        color_map = &trans_table2;
        draw_trans_sprite(dest, t, x, y);
        color_map = &trans_table;
        destroy_bitmap(t);
    }
    else
        textout_styled_aligned_ex(dest, f, text.c_str(), x, y, style, format, color, shadow, bg);
}

// One-shot nested run of a generic script, frame after frame, frozen.
static bool z30_run_generic_frozen(word script)
{
    if(script < 1 || script >= Q30_GENERIC_SLOTS || !q30_generic_scripts[script] || zasm30_generic_frozen_depth >= 400)
        return false;

    extern void doScriptMenuDraws();

    // Saved for this nesting: the frame running us stays as it was.
    refInfo *saved_ri = ri;
    ffscript *saved_curscript = curscript;
    long (*saved_stack)[ZSCRIPT_STACK_ALLOC] = stack;
    Zasm30Frame *saved_frame = zasm30_cur_frame;
    Zasm30Instance *saved_inst = zasm30_cur_inst;
    long *saved_sp = zasm30_wide_sp;
    long saved_limit = zasm30_stack_limit;
    long saved_arg1 = sarg1, saved_arg2 = sarg2;
    byte saved_type = curScriptType;
    word saved_num = curScriptNum;
    int depth = ++zasm30_generic_frozen_depth;

    zasm30_drop_instance(SCRIPT_GENERICFROZEN, depth);
    Zasm30Instance *in = zasm30_instance(SCRIPT_GENERICFROZEN, depth);

    while(in->doscript && !Quit)
    {
        script_drawing_commands.Clear();
        load_control_state();
        zasm30_run(SCRIPT_GENERICFROZEN, script, depth);
        clear_bitmap(framebuf);
        animate_combos();
        doScriptMenuDraws();
        advanceframe(true);
    }

    script_drawing_commands.Clear();
    zasm30_drop_instance(SCRIPT_GENERICFROZEN, depth);
    --zasm30_generic_frozen_depth;

    ri = saved_ri;
    curscript = saved_curscript;
    stack = saved_stack;
    zasm30_cur_frame = saved_frame;
    zasm30_cur_inst = saved_inst;
    zasm30_wide_sp = saved_sp;
    zasm30_stack_limit = saved_limit;
    sarg1 = saved_arg1;
    sarg2 = saved_arg2;
    curScriptType = saved_type;
    curScriptNum = saved_num;
    return true;
}

// ---- message strings -----------------------------------------------------------------------------
// Text a script gives a message, in the binary form MsgStr::s holds (see store_message_text in
// q255_basic.cpp, which this follows for what can be parsed without its command table).

extern dword q255_compat_rule_version;

enum { Z30_QR_MSG_BINARY_ENCODING = 489, Z30_SCC_LARGEST_ARG = 65023, Z30_SCC_MAX_ARGS = 10 };

static void z30_msg_literal(std::string &out, char c)
{
    if(out.size() < (size_t)MSGSIZE)
        out += c;
}

// A command goes in whole or not at all, and not where parsemsgcode won't read it.
static void z30_msg_command(std::string &out, int code, const int *args, int count)
{
    std::string bytes(1, (char)((code % 254) + 1));

    for(int q = 0; q < count; ++q)
    {
        const int arg = (args[q] < 0 || args[q] > Z30_SCC_LARGEST_ARG) ? Z30_SCC_LARGEST_ARG : args[q];
        bytes += (char)((arg % 254) + 1);

        if(arg >= 254)
        {
            bytes += (char)0xff;
            bytes += (char)(arg / 254);
        }
    }

    if(out.size() < (size_t)(MSGSIZE - 2) && out.size() + bytes.size() <= (size_t)MSGSIZE)
        out += bytes;
}

static bool z30_msg_number(const std::string &s, size_t &pos, int &value)
{
    const size_t start = pos;
    value = 0;

    for(; pos < s.size() && isdigit((unsigned char)s[pos]); ++pos)
    {
        if(value <= Z30_SCC_LARGEST_ARG)
            value = value * 10 + (s[pos] - '0');
    }

    return pos != start;
}

// Reads \code\arg\arg\ and the optional space after it; pos stays put if it isn't one.
static bool z30_msg_scc(const std::string &s, size_t &pos, int &code, int *args, int &count)
{
    size_t p = pos + 1;
    count = 0;

    if(!z30_msg_number(s, p, code))
        return false;

    while(p + 1 < s.size() && s[p] == '\\' && isdigit((unsigned char)s[p + 1]))
    {
        int value;
        ++p;
        z30_msg_number(s, p, value);

        if(count < Z30_SCC_MAX_ARGS)
            args[count++] = value;
    }

    if(p >= s.size() || s[p] != '\\')
        return false;

    ++p;

    if(p < s.size() && s[p] == ' ')
        ++p;

    pos = p;
    return true;
}

// Commands written by name stay in the text: the name table belongs to q255_basic.cpp.
static std::string z30_msg_from_ascii(const std::string &s)
{
    std::string out;

    for(size_t cur = 0; cur < s.size();)
    {
        const char c = s[cur];

        if(c == '\n')
        {
            z30_msg_command(out, MSGC_NEWLINE, NULL, 0);
            ++cur;
        }
        else if(c != '\\')
        {
            if((unsigned char)c >= 32)
                z30_msg_literal(out, c);

            ++cur;
        }
        else if(cur + 1 < s.size() && (s[cur + 1] == '\\' || s[cur + 1] == '{' || s[cur + 1] == '}'))
        {
            z30_msg_literal(out, s[cur + 1]);
            cur += 2;
        }
        else
        {
            int code = 0, count = 0, args[Z30_SCC_MAX_ARGS];

            if(z30_msg_scc(s, cur, code, args, count))
            {
                if((code >= 1 && code <= 17) || code == 20 || code == 21 || code == 25)
                    z30_msg_command(out, code, args, count);
            }
            else
            {
                z30_msg_literal(out, c);
                ++cur;
            }
        }
    }

    return out;
}

static void z30_msg_set(long id, const std::string &text)
{
    const bool binary = q30_quest_rule(Z30_QR_MSG_BINARY_ENCODING) || q255_compat_rule_version < 94;
    const std::string out = binary ? text.substr(0, MSGSIZE) : z30_msg_from_ascii(text);

    memcpy(MsgStrings[id].s, out.data(), out.size());
    MsgStrings[id].s[out.size()] = 0;
}

// ---- solidity on a screen other than the current one ----------------------------------------------
// _walkflag in maps.cpp, for one point of a screen chosen by the script.

static const mapscr *z30_layer_screen(const mapscr *m, int layer)
{
    const int lmap = m->layermap[layer];

    if(lmap <= 0)
        return m;

    const size_t index = (size_t)(lmap - 1) * MAPSCRS + m->layerscreen[layer];
    return index < TheMaps.size() && (TheMaps[index].valid & mVALID) ? &TheMaps[index] : m;
}

static bool z30_mapdata_solid(const mapscr *m, int x, int y)
{
    const bool lttp = get_bit(quest_rules, qr_LTTPWALK) != 0;

    if(x < 0 || y < 0 || x > (lttp ? 255 : 248) || y > (lttp ? 175 : 168))
        return false;

    const int pos = (x >> 4) + (y & 0xF0);
    const mapscr *s1 = z30_layer_screen(m, 0), *s2 = z30_layer_screen(m, 1);
    const newcombo &c = combobuf[m->data[pos]], &c1 = combobuf[s1->data[pos]], &c2 = combobuf[s2->data[pos]];
    const bool dried = (iswater_type(c.type) || iswater_type(c1.type) || iswater_type(c2.type)) && DRIEDLAKE;
    const int bit = ((x & 8) ? 4 : 1) << ((y & 8) ? 1 : 0);

    return ((c.walk & bit) || (c1.walk & bit) || (c2.walk & bit)) && !dried;
}

// ---- npc movement ----------------------------------------------------------------------------------
// enemy::scr_walkflag, scr_canmove and movexy from upstream's guys.cpp, on 2.53's enemy, which has
// no per-enemy move flags: fliers ignore solidity and everything else obeys it.

// (px,py) is the point to test, (nx,ny) where the hitbox's corner would end up.
static bool z30_npc_blocked(int px, int py, int special, int nx, int ny)
{
    const int yg = special == spw_floater ? 8 : 0;
    const int nb = get_bit(quest_rules, qr_NOBORDER) ? 16 : 0;

    if(nx < 16 - nb || ny < zc_max(16 - yg - nb, 0) || nx >= 240 + nb || ny >= 160 + nb)
        return true;

    if(px < 0 || px >= 256 || py < 0 || py >= 176)
        return true;

    bool flying = false;

    switch(special)
    {
    case spw_clipbottomright:
        if(py >= 128 || px >= 208)
            return true;
        break;

    case spw_wizzrobe:
    case spw_floater:
        if(isdungeon() && (py < 32 - yg || py >= 144 || px < 32 || px >= 224))
            return true;
        if(flyerblocked(px, py, special))
            return true;
        flying = true;
        break;
    }

    if(flying)
        return false;

    px &= ~7;
    py &= ~7;

    if(groundblocked(px, py))
        return true;

    return special == spw_water ? water_walkflag(px, py, 1) : _walkflag(px, py, 1);
}

static bool z30_npc_can_step(enemy *e, double dx, double dy, int special)
{
    if(dx == 0 && dy == 0)
        return true;

    if(dy < 0 && canfall(e->id) && (tmpscr->flags7 & fSIDEVIEW))
        return false;

    if(dx != 0 && dy != 0)
        return z30_npc_can_step(e, dx, 0, special) && z30_npc_can_step(e, 0, dy, special);

    const double bx = double(e->x) + e->hxofs, by = double(e->y) + e->hyofs;
    const int w = zc_max(int(e->hxsz), 1), h = zc_max(int(e->hysz), 1);
    const double rx = bx + w - 1, ry = by + h - 1;

    if(dx != 0)
    {
        if(dx < 0 && (special == spw_clipbottomright || special == spw_clipright))
            special = spw_none;

        const int mx = int(dx < 0 ? floor(bx + dx) : ceil(rx + dx));
        const int nx = dx < 0 ? mx : mx - w + 1;

        for(double t = 0; by + t < ry; t += 8)
        {
            if(z30_npc_blocked(mx, int(by + t), special, nx, int(by)))
                return false;
        }

        return !z30_npc_blocked(mx, int(ry), special, nx, int(by));
    }

    if(dy < 0 && special == spw_clipbottomright)
        special = spw_none;

    const int my = int(dy < 0 ? floor(by + dy) : ceil(ry + dy));
    const int ny = dy < 0 ? my : my - h + 1;

    for(double t = 0; bx + t < rx; t += 8)
    {
        if(z30_npc_blocked(int(bx + t), my, special, int(bx), ny))
            return false;
    }

    return !z30_npc_blocked(int(rx), my, special, int(bx), ny);
}

// Moves as far as it can along one axis; false if something was in the way.
static bool z30_npc_move_axis(enemy *e, double d, bool horizontal, int special, bool earlyret)
{
    if(d == 0)
        return true;

    const double step = d < 0 ? -1 : 1;
#define Z30_CAN(v) z30_npc_can_step(e, horizontal ? (v) : 0, horizontal ? 0 : (v), special)
#define Z30_ADD(v) (horizontal ? (e->x = fix(double(e->x) + (v))) : (e->y = fix(double(e->y) + (v))))

    if(Z30_CAN(d))
    {
        Z30_ADD(d);
        return true;
    }

    if(earlyret)
        return false;

    while(fabs(d) >= 1 && Z30_CAN(step))
    {
        Z30_ADD(step);
        d -= step;
    }

    // The rest of the distance is under a pixel: the most of it that still fits.
    if(fabs(d) >= 1)
        d = fmod(d, 1.0);

    double low = 0, high = d;

    for(int q = 0; q < 14; ++q)
    {
        const double mid = (low + high) / 2;

        if(Z30_CAN(mid))
            low = mid;
        else
            high = mid;
    }

    Z30_ADD(low);
#undef Z30_CAN
#undef Z30_ADD
    return false;
}

static bool z30_npc_movexy(enemy *e, double dx, double dy, int special, bool earlyret)
{
    if(dy < 0 && canfall(e->id) && (tmpscr->flags7 & fSIDEVIEW))
        dy = 0;

    bool ret = true;
    const double scale = 2;

    while(fabs(dx) > scale || fabs(dy) > scale)
    {
        const bool horizontal = fabs(dx) > fabs(dy);
        double &d = horizontal ? dx : dy;
        const double part = d < 0 ? -scale : scale;

        if(z30_npc_movexy(e, horizontal ? part : 0, horizontal ? 0 : part, special, earlyret))
            d -= part;
        else
        {
            if(earlyret)
                return false;

            d = part;       // the other axis keeps going
            ret = false;
        }
    }

    const bool x_ok = z30_npc_move_axis(e, dx, true, special, earlyret);

    if(!x_ok && earlyret)
        return false;

    const bool y_ok = z30_npc_move_axis(e, dy, false, special, earlyret);
    return ret && x_ok && y_ok;
}

static bool z30_npc_dir_delta(long dir, double px, double &dx, double &dy)
{
    static const signed char table[16] = { up, down, left, right, l_up, r_up, l_down, r_down,
                                           up, r_up, right, r_down, down, l_down, left, l_up };

    if(dir < 0 || dir >= 16)
        return false;

    const double diag = px * 0.7071;

    switch(table[dir])
    {
    case up:      dx = 0; dy = -px; break;
    case down:    dx = 0; dy = px; break;
    case left:    dx = -px; dy = 0; break;
    case right:   dx = px; dy = 0; break;
    case r_up:    dx = diag; dy = -diag; break;
    case r_down:  dx = diag; dy = diag; break;
    case l_up:    dx = -diag; dy = -diag; break;
    default:      dx = -diag; dy = diag; break;     // l_down
    }

    return true;
}

// npc->Move(), MoveAtAngle(), MoveXY() and their CanMove forms. rINDEX is the direction, angle or
// x distance, rEXP2 the distance or y distance, rEXP1 the special; the answer goes in rEXP1.
static void z30_npc_move_command(word cmd)
{
    const double a = ri->d[0] / 10000.0, b = ri->d[3] / 10000.0;
    const int special = ri->d[2] / 10000;
    ri->d[2] = 0;

    if(GuyH::loadNPC(ri->guyref, "npc->Move()") != SH::_NoError)
        return;

    enemy *e = GuyH::getNPC();
    double dx = 0, dy = 0;

    switch(cmd)
    {
    case Z30C_NPCMOVE:
    case Z30C_NPCCANMOVEDIR:
        if(!z30_npc_dir_delta(ri->d[0] / 10000, b, dx, dy))
            return;
        break;

    case Z30C_NPCMOVEANGLE:
    case Z30C_NPCCANMOVEANGLE:
    {
        const double radians = a * 3.14159265358979 / 180.0;
        dx = cos(radians) * b;
        dy = sin(radians) * b;
        break;
    }

    default:
        dx = a;
        dy = b;
        break;
    }

    if(cmd == Z30C_NPCMOVE || cmd == Z30C_NPCMOVEANGLE || cmd == Z30C_NPCMOVEXY)
    {
        ri->d[2] = z30_npc_movexy(e, dx, dy, special, false) ? 10000 : 0;
        return;
    }

    const fix old_x = e->x, old_y = e->y;
    ri->d[2] = z30_npc_movexy(e, dx, dy, special, true) ? 10000 : 0;
    e->x = old_x;
    e->y = old_y;
}

// ---- the commands ------------------------------------------------------------------------------

bool zasm30_combo_layer_on(int) { return true; }

long zasm30_hero_initd[Q30_HERO_SLOTS][8];
long zasm30_global_initd[Q30_GLOBAL_SLOTS][8];

// Hero->WarpEx. args: type, dmap, screen, x, y, effect, sound, flags, dir. A negative x means
// y is a warp return square. 2.53 warps through side warp A, so that is what this borrows; the
// exact position, if any, is applied once the warp has happened (zasm30_apply_pending_warp).
// Screen tints (Graphics->Tint, MonochromeHue). Palette reloads call q30_tint_palette_loaded.
extern PALETTE RAMpal;
extern bool refreshpal;
static RGB z30_tint_backup[PAL_SIZE];
static bool z30_tinted = false;
static int z30_tint[4] = { 0, 0, 0, -1 };   // r, g, b, base: -1 none, 0 uniform grey, 1 weighted grey

extern void (*q30_tint_palette_loaded_hook)();

static void z30_add_colour()
{
    for(int i = 0; i <= 0xEF; ++i)
    {
        if(z30_tint[3] >= 0)
        {
            int grey = z30_tint[3] == 0 ? (RAMpal[i].r + RAMpal[i].g + RAMpal[i].b) / 3
                       : (int)(0.299 * RAMpal[i].r + 0.587 * RAMpal[i].g + 0.114 * RAMpal[i].b);
            RAMpal[i].r = RAMpal[i].g = RAMpal[i].b = grey;
        }

        RAMpal[i].r = vbound(RAMpal[i].r + z30_tint[0], 0, 63);
        RAMpal[i].g = vbound(RAMpal[i].g + z30_tint[1], 0, 63);
        RAMpal[i].b = vbound(RAMpal[i].b + z30_tint[2], 0, 63);
    }
}

static void z30_apply_tint(int r, int g, int b, int base)
{
    if(z30_tinted)
    {
        memcpy(RAMpal, z30_tint_backup, sizeof(z30_tint_backup));
        z30_tint[0] = vbound(z30_tint[0] + r, -32768, 32767);
        z30_tint[1] = vbound(z30_tint[1] + g, -32768, 32767);
        z30_tint[2] = vbound(z30_tint[2] + b, -32768, 32767);
    }
    else
    {
        memcpy(z30_tint_backup, RAMpal, sizeof(z30_tint_backup));
        z30_tint[0] = r;
        z30_tint[1] = g;
        z30_tint[2] = b;
    }

    z30_tint[3] = base;
    z30_add_colour();
    z30_tinted = true;
    refreshpal = true;
}

static void z30_clear_tint()
{
    if(z30_tinted)
    {
        memcpy(RAMpal, z30_tint_backup, sizeof(z30_tint_backup));
        z30_tinted = false;
        refreshpal = true;
    }
}

// The game just loaded a palette over the tinted one: keep the tint on the new colours.
static void z30_tint_palette_loaded()
{
    if(z30_tinted)
    {
        memcpy(z30_tint_backup, RAMpal, sizeof(z30_tint_backup));
        z30_add_colour();
        refreshpal = true;
    }
}

FONT *get_zc_font(int index);

static bool zasm30_warp_pending = false;
static int zasm30_warp_x = 0, zasm30_warp_y = 0, zasm30_warp_dir = -1;

static void zasm30_warp_ex(const int *a)
{
    const int type = a[0], dmap = a[1], screen = a[2], x = a[3], y = a[4], effect = a[5], sound = a[6], dir = a[8];

    if(dmap < 0 || dmap >= MAXDMAPS || screen < 0 || screen >= MAPSCRS)
    {
        Z_scripterrlog("Invalid destination passed to WarpEx(): DMap %d, screen %d\n", dmap, screen);
        return;
    }

    if(type == wtNOWARP)
        return;

    int warp_type = type;

    if(type == wtIWARP) // the effect argument picks the look of an instant warp
    {
        static const int by_effect[] = { wtIWARP, wtIWARPZAP, wtIWARPWAVE, wtIWARP, wtIWARPOPEN };

        if(effect >= 0 && effect < 5)
            warp_type = by_effect[effect];
    }

    tmpscr->sidewarpdmap[0] = dmap;
    tmpscr->sidewarpscr[0] = screen;
    tmpscr->sidewarptype[0] = warp_type;

    if(x < 0)
    {
        tmpscr->warpreturnc = (tmpscr->warpreturnc & ~(3 << 8)) | ((y & 3) << 8);
        zasm30_warp_pending = false;
    }
    else
    {
        zasm30_warp_pending = true;
        zasm30_warp_x = x;
        zasm30_warp_y = y;
    }

    zasm30_warp_dir = dir;
    Link.ffwarp = true;

    if(sound > 0)
        sfx(sound);
}

static void zasm30_apply_pending_warp()
{
    if(!zasm30_warp_pending || Link.ffwarp)
        return;

    zasm30_warp_pending = false;
    Link.setX(zasm30_warp_x);
    Link.setY(zasm30_warp_y);

    if(zasm30_warp_dir >= 0 && zasm30_warp_dir < 4)
        Link.setDir(zasm30_warp_dir);
}

static bool zasm30_command(word cmd)
{
    if(zasm30_sub_command(cmd))
        return true;

    switch(cmd)
    {
    case Z30C_WARPEX:
    {
        int n = sarg1 / 10000, args[10];
        memset(args, 0, sizeof(args));
        args[8] = -1;

        if(n >= 8 && n <= 10)
        {
            for(int q = 0; q < n; ++q)
                args[q] = SH::read_stack(ri->sp + (n - q - 1)) / 10000;

            zasm30_warp_ex(args);
        }
        return true;
    }

    case Z30C_LINKWARPEXR:
    {
        long ptr = get_register(sarg1) / 10000;
        ZScriptArray &a = ArrayH::getArray(ptr);

        if(&a != &INVALIDARRAY && (a.Size() == 8 || a.Size() == 9))
        {
            int args[10];
            memset(args, 0, sizeof(args));
            args[8] = -1;

            for(dword q = 0; q < a.Size(); ++q)
                args[q] = a[q] / 10000;

            zasm30_warp_ex(args);
        }
        return true;
    }

    case Z30C_TINT:
        q30_tint_palette_loaded_hook = z30_tint_palette_loaded;
        z30_apply_tint(SH::read_stack(SPR + 2) / 10000, SH::read_stack(SPR + 1) / 10000, SH::read_stack(SPR) / 10000, -1);
        return true;

    case Z30C_MONOHUE:
        q30_tint_palette_loaded_hook = z30_tint_palette_loaded;
        z30_apply_tint(SH::read_stack(SPR + 3) / 10000, SH::read_stack(SPR + 2) / 10000, SH::read_stack(SPR + 1) / 10000, (SH::read_stack(SPR) / 10000) ? 1 : 0);
        return true;

    case Z30C_CLEARTINT:
        z30_clear_tint();
        return true;

    // -- string.h style commands, and text measuring -------------------------------------------
    case Z30C_STRINGLENGTH:
    {
        string str;
        ArrayH::getString(get_register(sarg2) / 10000, str);
        set_register(sarg1, (long)str.length() * 10000);
        return true;
    }

    case Z30C_STRINGCOPY:
    {
        string str;
        ArrayH::getString(get_register(sarg2) / 10000, str);
        ArrayH::setArray(get_register(sarg1) / 10000, str);
        return true;
    }

    case Z30C_ARRAYCOPY:
    {
        ZScriptArray &dest = ArrayH::getArray(get_register(sarg1) / 10000);
        ZScriptArray &src = ArrayH::getArray(get_register(sarg2) / 10000);

        if(&dest != &INVALIDARRAY && &src != &INVALIDARRAY && &dest != &src)
            for(dword n = 0; n < dest.Size() && n < src.Size(); ++n)
                dest[n] = src[n];
        return true;
    }

    case Z30C_STRINGNCOMPARE:
    case Z30C_STRINGNICOMPARE:
    {
        string a, b;
        ArrayH::getString(ri->d[0] / 10000, a);
        ArrayH::getString(ri->d[3] / 10000, b);
        long len = ri->d[2] / 10000;
        int r = cmd == Z30C_STRINGNCOMPARE ? strncmp(a.c_str(), b.c_str(), len) : strnicmp(a.c_str(), b.c_str(), len);
        set_register(sarg1, vbound(r, -1, 1) * 10000);
        return true;
    }

    case Z30C_STRINGICOMPARE:
    {
        string a, b;
        ArrayH::getString(ri->d[0] / 10000, a);
        ArrayH::getString(ri->d[1] / 10000, b);
        set_register(sarg1, vbound(stricmp(a.c_str(), b.c_str()), -1, 1) * 10000);
        return true;
    }

    case Z30C_STRCAT:
    {
        string a, b;
        ArrayH::getString(ri->d[0] / 10000, a);
        ArrayH::getString(ri->d[1] / 10000, b);
        bool over = ArrayH::setArray(ri->d[0] / 10000, a + b) == SH::_Overflow;
        set_register(sarg1, over ? 0 : ri->d[0]);
        return true;
    }

    case Z30C_STRSTR:
    {
        string a, b;
        ArrayH::getString(ri->d[0] / 10000, a);
        ArrayH::getString(ri->d[1] / 10000, b);
        set_register(sarg1, a.empty() ? -10000 : (long)a.find(b) * 10000);
        return true;
    }

    case Z30C_STRSPN:
    case Z30C_STRCSPN:
    {
        string a, b;
        ArrayH::getString(ri->d[0] / 10000, a);
        ArrayH::getString(ri->d[1] / 10000, b);
        size_t n = cmd == Z30C_STRSPN ? strspn(a.c_str(), b.c_str()) : strcspn(a.c_str(), b.c_str());
        set_register(sarg1, (long)n * 10000);
        return true;
    }

    case Z30C_STRCHR:
    case Z30C_STRRCHR:
    {
        string a;
        ArrayH::getString(ri->d[0] / 10000, a);
        char c = (char)(ri->d[1] / 10000);
        size_t at = cmd == Z30C_STRCHR ? a.find_first_of(c) : a.find_last_of(c);
        set_register(sarg1, a.empty() ? -10000 : (long)at * 10000);
        return true;
    }

    case Z30C_UPPERTOLOWER:
    case Z30C_LOWERTOUPPER:
    case Z30C_CONVERTCASE:
    {
        long ptr = get_register(sarg1) / 10000;
        string a;
        ArrayH::getString(ptr, a);

        for(size_t n = 0; n < a.size(); ++n)
        {
            byte c = (byte)a[n];

            if(cmd == Z30C_UPPERTOLOWER)
                c = (byte)tolower(c);
            else if(cmd == Z30C_LOWERTOUPPER)
                c = (byte)toupper(c);
            else if(c >= 'A' && c <= 'Z')
                c += 32;
            else if(c >= 'a' && c <= 'z')
                c -= 32;

            a[n] = (char)c;
        }

        ArrayH::setArray(ptr, a);
        set_register(sarg1, 10000);
        return true;
    }

    case Z30C_XTOI:
    {
        string a;
        ArrayH::getString(get_register(sarg2) / 10000, a);
        const char *p = a.c_str();
        bool negative = *p == '-';
        if(negative)
            ++p;
        if(p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            p += 2;
        long value = strtol(p, NULL, 16);
        set_register(sarg1, (negative ? -value : value) * 10000);
        return true;
    }

    case Z30C_XTOA:
    {
        long ptr = get_register(sarg1) / 10000, number = get_register(sarg2) / 10000;
        char text[32];
        sprintf(text, "%s0x%lX", number < 0 ? "-" : "", number < 0 ? -number : number);
        bool over = ArrayH::setArray(ptr, string(text)) == SH::_Overflow;
        set_register(sarg1, over ? 0 : (long)strlen(text) * 10000);
        return true;
    }

    case Z30C_ILEN:
    {
        string a;
        ArrayH::getString(get_register(sarg2) / 10000, a);
        size_t n = (!a.empty() && a[0] == '-') ? 1 : 0;
        while(n < a.size() && a[n] >= '0' && a[n] <= '9')
            ++n;
        set_register(sarg1, (long)n * 10000);
        return true;
    }

    case Z30C_ITOACAT:
    {
        long ptr = get_register(sarg1) / 10000;
        string a;
        ArrayH::getString(ptr, a);
        char digits[32];
        sprintf(digits, "%ld", get_register(sarg2) / 10000);
        bool over = ArrayH::setArray(ptr, a + digits) == SH::_Overflow;
        set_register(sarg1, over ? 0 : get_register(sarg1));
        return true;
    }

    case Z30C_FONTHEIGHTR:
        ri->d[2] = text_height(get_zc_font(get_register(sarg1) / 10000)) * 10000;
        return true;

    case Z30C_STRINGWIDTHR:
    {
        string a;
        ArrayH::getString(get_register(sarg1) / 10000, a, 512);
        ri->d[2] = text_length(get_zc_font(get_register(sarg2) / 10000), a.c_str()) * 10000;
        return true;
    }

    case Z30C_CHARWIDTHR:
    {
        char text[2] = { (char)(get_register(sarg1) / 10000), 0 };
        ri->d[2] = text_length(get_zc_font(get_register(sarg2) / 10000), text) * 10000;
        return true;
    }

    // -- stack frame access from before 3.0: the offset is in fixed point --------------------
    case Z30C_LOADD:
        set_register(sarg1, SH::read_stack((sarg2 + ri->d[4]) / 10000));
        return true;

    case Z30C_STORED:
        SH::write_stack((sarg2 + ri->d[4]) / 10000, get_register(sarg1));
        return true;

    case Z30C_STOREDV:
        SH::write_stack((sarg2 + ri->d[4]) / 10000, sarg1);
        return true;

    // -- music and sound (src/port/game/q30_audio.cpp) -----------------------------------------
    case Z30C_PLAY_MUSIC:
        q30_play_music(get_register(sarg1));
        return true;

    case Z30C_ADJUSTVOLUMER:
    case Z30C_ADJUSTVOLUMEV:
        q30_adjust_music_volume(SH::get_arg(sarg1, cmd == Z30C_ADJUSTVOLUMEV));
        return true;

    case Z30C_ADJUSTSFXVOLUMER:
    case Z30C_ADJUSTSFXVOLUMEV:
        q30_adjust_sfx_volume(SH::get_arg(sarg1, cmd == Z30C_ADJUSTSFXVOLUMEV));
        return true;

    case Z30C_PLAYENHMUSICEX:
    {
        long ptr = get_register(sarg1) / 10000;

        if(!ptr)
        {
            q30_play_enhanced(NULL, 0);
            return true;
        }

        string name;
        ArrayH::getString(ptr, name, 256);
        set_register(sarg2, q30_play_enhanced(name.c_str(), get_register(sarg2) / 10000 - 1) ? 10000 : 0);
        return true;
    }

    case Z30C_GETENHMUSICPOS:
        set_register(sarg1, q30_enh_get_pos());
        return true;

    case Z30C_SETENHMUSICPOS:
        q30_enh_set_pos(get_register(sarg1));
        return true;

    case Z30C_SETENHMUSICSPEED:
        q30_enh_set_speed(get_register(sarg1));
        return true;

    case Z30C_GETENHMUSICLEN:
        set_register(sarg1, q30_enh_get_length());
        return true;

    case Z30C_SETENHMUSICLOOP:
        q30_enh_set_loop(get_register(sarg1) / 10000.0, get_register(sarg2) / 10000.0);
        return true;

    case Z30C_ENHCROSSFADE:
    {
        long ptr = SH::read_stack(ri->sp + 5) / 10000;
        string name;

        if(ptr)
            ArrayH::getString(ptr, name, 256);

        bool done = q30_enh_crossfade(ptr ? name.c_str() : NULL, SH::read_stack(ri->sp + 4) / 10000,
                                      zc_max(SH::read_stack(ri->sp + 3) / 10000, 0), zc_max(SH::read_stack(ri->sp + 2) / 10000, 0),
                                      SH::read_stack(ri->sp + 1) / 10000, SH::read_stack(ri->sp));
        ri->d[2] = done ? 10000 : 0;
        return true;
    }

    case Z30C_PLAYSOUNDEX:
        q30_sfx_ex(SH::read_stack(ri->sp + 4) / 10000, SH::read_stack(ri->sp + 3), SH::read_stack(ri->sp + 2),
                   SH::read_stack(ri->sp + 1), SH::read_stack(ri->sp), true);
        return true;

    case Z30C_GETSFXCOMPLETION:
        set_register(sarg1, q30_sfx_completion(get_register(sarg1) / 10000));
        return true;

    case Z30C_LOADMUSICDATA:
    {
        long ref = get_register(sarg1) / 10000;
        Z30_REF(MUSIC) = q30_music_exists(ref) ? ref : 0;
        return true;
    }

    case Z30C_GETMUSICDATABYNAME:
    {
        string name;
        ArrayH::getString(get_register(sarg1) / 10000, name, 256);
        set_register(sarg1, q30_music_by_name(name.c_str()) * 10000);
        return true;
    }

    case Z30C_MUSICDATA_GETPATH:
    case Z30C_MUSICDATA_GETNAME:
        ArrayH::setArray(get_register(sarg1) / 10000, string(cmd == Z30C_MUSICDATA_GETPATH ? q30_musicdata_path(Z30_REF(MUSIC)) : q30_musicdata_name(Z30_REF(MUSIC))));
        return true;

    case Z30C_MUSICDATA_SETPATH:
    case Z30C_MUSICDATA_SETNAME:
    {
        string text;
        ArrayH::getString(get_register(sarg1) / 10000, text, 2048);

        if(cmd == Z30C_MUSICDATA_SETPATH)
            q30_musicdata_set_path(Z30_REF(MUSIC), text.c_str());
        else
            q30_musicdata_set_name(Z30_REF(MUSIC), text.c_str());
        return true;
    }

    // -- object loads ---------------------------------------------------------------------------
    case Z30C_LOADNPCDATAR:
    case Z30C_LOADNPCDATAV:
    {
        long id = SH::get_arg(sarg1, cmd == Z30C_LOADNPCDATAV) / 10000;
        Z30_REF(NPCDATA) = (id >= 1 && id < MAXGUYS) ? id : -1;
        return true;
    }
    case Z30C_LOADSPRITEDATAR:
    case Z30C_LOADSPRITEDATAV:
    {
        long id = SH::get_arg(sarg1, cmd == Z30C_LOADSPRITEDATAV) / 10000;
        Z30_REF(SPRITEDATA) = (unsigned(id) < MAXWPNS) ? id : 0;
        return true;
    }
    case Z30C_LOADCOMBODATAR:
    case Z30C_LOADCOMBODATAV:
    {
        long id = SH::get_arg(sarg1, cmd == Z30C_LOADCOMBODATAV) / 10000;
        Z30_REF(COMBODATA) = (unsigned(id) < MAXCOMBOS) ? id : 0;
        return true;
    }
    case Z30C_LOADDMAPDATAR:
    case Z30C_LOADDMAPDATAV:
    {
        long id = SH::get_arg(sarg1, cmd == Z30C_LOADDMAPDATAV) / 10000;
        Z30_REF(DMAPDATA) = (unsigned(id) < MAXDMAPS) ? id : -1;
        return true;
    }
    case Z30C_LOADDROPSETR:
    {
        long id = get_register(sarg1) / 10000;
        Z30_REF(DROPSETDATA) = id;
        return true;
    }
    case Z30C_LOADSHOPR:
    case Z30C_LOADSHOPV:
    {
        long id = SH::get_arg(sarg1, cmd == Z30C_LOADSHOPV) / 10000;
        Z30_REF(SHOPDATA) = id;
        return true;
    }
    case Z30C_LOADINFOSHOPR:
    case Z30C_LOADINFOSHOPV:
    {
        long id = SH::get_arg(sarg1, cmd == Z30C_LOADINFOSHOPV) / 10000;
        Z30_REF(SHOPDATA) = id + 256;
        return true;
    }
    case Z30C_LOADMESSAGEDATAR:
    case Z30C_LOADMESSAGEDATAV:
    {
        long id = SH::get_arg(sarg1, cmd == Z30C_LOADMESSAGEDATAV) / 10000;
        Z30_REF(MSGDATA) = id;
        return true;
    }
    case Z30C_LOADGENERICDATA:
    {
        long id = get_register(sarg1) / 10000;
        Z30_REF(GENERICDATA) = (id >= 1 && id < Q30_GENERIC_SLOTS) ? id : 0;
        return true;
    }
    case Z30C_LOADBITMAPDATAR:
    case Z30C_LOADBITMAPDATAV:
    {
        long id = SH::get_arg(sarg1, cmd == Z30C_LOADBITMAPDATAV) / 10000;
        Z30_REF(BITMAP) = id + 10;
        return true;
    }
    case Z30C_LOADTMPSCR:
    case Z30C_LOADTMPSCR2:
    {
        // Screen->... layer 0..6 of the current screen.
        long layer = (cmd == Z30C_LOADTMPSCR2 ? get_register(sarg1) : get_register(sarg1)) / 10000;
        Z30_REF(MAPDATA) = (layer >= 0 && layer <= 6) ? (Z30_MAPDATA_TMP | layer) : 0;
        set_register(sarg1, Z30_REF(MAPDATA));
        return true;
    }
    case Z30C_LOADSCROLLSCR:
    case Z30C_LOADSCROLLSCR2:
    {
        set_register(sarg1, 0);
        return true;
    }
    case Z30C_LOADNPCBYSUID:
    {
        long uid = get_register(sarg1);
        ri->guyref = guys.getByUID(uid) ? uid : 0;
        return true;
    }
    case Z30C_LOADLWEAPONBYSUID:
    {
        long uid = get_register(sarg1);
        ri->lwpn = Lwpns.getByUID(uid) ? uid : 0;
        return true;
    }
    case Z30C_LOADWEAPONCBYSUID:
    {
        long uid = get_register(sarg1);
        ri->ewpn = Ewpns.getByUID(uid) ? uid : 0;
        return true;
    }

    // -- script slots by name ------------------------------------------------------------------
    case Z30C_GETFFCSCRIPT:
    case Z30C_GETITEMSCRIPT:
    case Z30C_GETNPCSCRIPT:
    case Z30C_GETLWEAPONSCRIPT:
    case Z30C_GETEWEAPONSCRIPT:
    case Z30C_GETHEROSCRIPT:
    case Z30C_GETGLOBALSCRIPT:
    case Z30C_GETDMAPSCRIPT:
    case Z30C_GETSCREENSCRIPT:
    case Z30C_GETSPRITESCRIPT:
    case Z30C_GETCOMBOSCRIPT:
    case Z30C_GETGENERICSCRIPT:
    case Z30C_GETSUBSCREENSCRIPT:
    case Z30C_GETUNTYPEDSCRIPT:
    {
        // Upstream: the name is in the array at sarg1's register, the slot returns in rEXP1 (d2).
        string name;
        ArrayH::getString(get_register(sarg1) / 10000, name, 256);
        int table = -1;
        switch(cmd)
        {
        case Z30C_GETFFCSCRIPT: table = q30_names_ffc; break;
        case Z30C_GETITEMSCRIPT: table = q30_names_item; break;
        case Z30C_GETNPCSCRIPT: table = q30_names_npc; break;
        case Z30C_GETLWEAPONSCRIPT: table = q30_names_lweapon; break;
        case Z30C_GETEWEAPONSCRIPT: table = q30_names_eweapon; break;
        case Z30C_GETHEROSCRIPT: table = q30_names_hero; break;
        case Z30C_GETGLOBALSCRIPT: table = q30_names_global; break;
        case Z30C_GETDMAPSCRIPT: table = q30_names_dmap; break;
        case Z30C_GETSCREENSCRIPT: table = q30_names_screen; break;
        case Z30C_GETSPRITESCRIPT: table = q30_names_itemsprite; break;
        case Z30C_GETCOMBOSCRIPT: table = q30_names_combo; break;
        case Z30C_GETGENERICSCRIPT: table = q30_names_generic; break;
        case Z30C_GETSUBSCREENSCRIPT: table = q30_names_subscreen; break;
        }
        ri->d[2] = table >= 0 ? z30_name_lookup(table, name) : 0;
        return true;
    }

    // -- arrays and strings -------------------------------------------------------------------
    case Z30C_ISVALIDARRAY:
    {
        ZScriptArray &a = ArrayH::getArray(get_register(sarg1) / 10000);
        set_register(sarg1, (&a != &INVALIDARRAY) ? 10000 : 0);
        return true;
    }
    case Z30C_OWNARRAYR:
    case Z30C_DESTROYARRAYR:
        return true;
    case Z30C_BITMAPOWN:
    case Z30C_FILEOWN:
    case Z30C_DIRECTORYOWN:
    case Z30C_RNGOWN:
    case Z30C_PALDATAOWN:
        return true;

    // -- bitmaps --------------------------------------------------------------------------------
    case Z30C_READBITMAP:
    case Z30C_WRITEBITMAP:
        return true;
    case Z30C_REGENERATEBITMAP:
    {
        long ref = Z30_REF(BITMAP);
        int w = ri->d[1] / 10000, h = ri->d[0] / 10000;
        BITMAP *&b = zasm30_bitmaps[ref];

        if(b)
            destroy_bitmap(b);

        b = create_bitmap_ex(8, vbound(w, 1, 4096), vbound(h, 1, 4096));
        if(b)
            clear_bitmap(b);
        return true;
    }
    case Z30C_BITMAPFREE:
    {
        z30_free_bitmap(Z30_REF(BITMAP));
        return true;
    }
    case Z30C_CLEARBITMAP:
    case Z30C_BITMAPCLEARTOCOLOR:
    {
        const int numargs = cmd == Z30C_CLEARBITMAP ? 1 : 2;
        BITMAP *bmp = z30_bitmap(SH::read_stack(SPR + numargs));

        if(bmp)
        {
            if(cmd == Z30C_CLEARBITMAP)
                clear_bitmap(bmp);
            else
                clear_to_color(bmp, SH::read_stack(SPR) / 10000);
        }
        return true;
    }
    case Z30C_BMPRECTR: z30_user_bitmap_draw(cmd, RECTR, 12); return true;
    case Z30C_BMPCIRCLER: z30_user_bitmap_draw(cmd, CIRCLER, 11); return true;
    case Z30C_BMPARCR: z30_user_bitmap_draw(cmd, ARCR, 14); return true;
    case Z30C_BMPELLIPSER: z30_user_bitmap_draw(cmd, ELLIPSER, 12); return true;
    case Z30C_BMPLINER: z30_user_bitmap_draw(cmd, LINER, 11); return true;
    case Z30C_BMPSPLINER: z30_user_bitmap_draw(cmd, SPLINER, 11); return true;
    case Z30C_BMPPUTPIXELR: z30_user_bitmap_draw(cmd, PUTPIXELR, 8); return true;
    case Z30C_BMPDRAWTILER: z30_user_bitmap_draw(cmd, DRAWTILER, 15); return true;
    case Z30C_BMPDRAWCOMBOR: z30_user_bitmap_draw(cmd, DRAWCOMBOR, 16); return true;
    case Z30C_BMPFASTTILER: z30_user_bitmap_draw(cmd, FASTTILER, 6); return true;
    case Z30C_BMPFASTCOMBOR: z30_user_bitmap_draw(cmd, FASTCOMBOR, 6); return true;
    case Z30C_BMPDRAWCHARR: z30_user_bitmap_draw(cmd, DRAWCHARR, 10); return true;
    case Z30C_BMPDRAWINTR: z30_user_bitmap_draw(cmd, DRAWINTR, 11); return true;
    case Z30C_BMPDRAWSTRINGR: z30_user_bitmap_draw(cmd, DRAWSTRINGR, 9); return true;
    case Z30C_BMPQUADR: z30_user_bitmap_draw(cmd, QUADR, 16); return true;
    case Z30C_BMPTRIANGLER: z30_user_bitmap_draw(cmd, TRIANGLER, 14); return true;
    case Z30C_BMPDRAWLAYERR: z30_user_bitmap_draw(cmd, DRAWLAYERR, 8); return true;
    case Z30C_BMPDRAWSCREENR: z30_user_bitmap_draw(cmd, DRAWSCREENR, 6); return true;
    case Z30C_BMPBLIT:
    {
        // bmp->Blit(layer, dest, srcx, srcy, srcw, srch, dstx, dsty, dstw, dsth, ...)
        long src_ref = SH::read_stack(SPR + 16);
        long dst_ref = SH::read_stack(SPR + 14);
        int sx = SH::read_stack(SPR + 13) / 10000, sy = SH::read_stack(SPR + 12) / 10000;
        int sw = SH::read_stack(SPR + 11) / 10000, sh = SH::read_stack(SPR + 10) / 10000;
        int dx = SH::read_stack(SPR + 9) / 10000, dy = SH::read_stack(SPR + 8) / 10000;
        int dw = SH::read_stack(SPR + 7) / 10000, dh = SH::read_stack(SPR + 6) / 10000;
        bool masked = SH::read_stack(SPR) != 0;
        BITMAP *src = z30_bitmap(src_ref), *dst = z30_bitmap(dst_ref);

        if(src && dst)
        {
            if(masked)
                masked_stretch_blit(src, dst, sx, sy, sw, sh, dx, dy, dw, dh);
            else
                stretch_blit(src, dst, sx, sy, sw, sh, dx, dy, dw, dh);
        }
        return true;
    }
    case Z30C_BMPBLITTO:
    {
        long dst_ref = SH::read_stack(SPR + 16);
        long src_ref = SH::read_stack(SPR + 14);
        int sx = SH::read_stack(SPR + 13) / 10000, sy = SH::read_stack(SPR + 12) / 10000;
        int sw = SH::read_stack(SPR + 11) / 10000, sh = SH::read_stack(SPR + 10) / 10000;
        int dx = SH::read_stack(SPR + 9) / 10000, dy = SH::read_stack(SPR + 8) / 10000;
        int dw = SH::read_stack(SPR + 7) / 10000, dh = SH::read_stack(SPR + 6) / 10000;
        bool masked = SH::read_stack(SPR) != 0;
        BITMAP *src = z30_bitmap(src_ref), *dst = z30_bitmap(dst_ref);

        if(src && dst)
        {
            if(masked)
                masked_stretch_blit(src, dst, sx, sy, sw, sh, dx, dy, dw, dh);
            else
                stretch_blit(src, dst, sx, sy, sw, sh, dx, dy, dw, dh);
        }
        return true;
    }
    case Z30C_GRAPHICSGETPIXEL:
    {
        int xpos = ri->d[1] / 10000, ypos = ri->d[0] / 10000;
        BITMAP *b = z30_bitmap(ri->d[2]);

        if(!b)
            b = scrollbuf;

        set_register(sarg1, getpixel(b, xpos, ypos) * 10000);
        return true;
    }
    case Z30C_FFCTRIGGER:
        return true;

    // -- rng objects ----------------------------------------------------------------------------
    case Z30C_LOADRNG:
    {
        const long id = zasm30_next_rng++;
        zasm30_rngs[id] = (unsigned int)rand();
        Z30_REF(RNG) = id;
        ri->d[2] = id;
        return true;
    }
    case Z30C_RNGRAND1:
    {
        unsigned int &seed = zasm30_rngs[Z30_REF(RNG)];
        set_register(sarg1, (long)(z30_rng_next(seed)) * 10000L);
        return true;
    }
    case Z30C_RNGRAND2:
    {
        long bound = get_register(sarg1) / 10000;
        unsigned int &seed = zasm30_rngs[Z30_REF(RNG)];
        set_register(sarg1, bound ? (long)(z30_rng_next(seed) % bound) * 10000L : 0);
        return true;
    }
    case Z30C_RNGRAND3:
    {
        long low = get_register(sarg1) / 10000, high = get_register(sarg2) / 10000;
        if(high < low) zc_swap(low, high);
        unsigned int &seed = zasm30_rngs[Z30_REF(RNG)];
        set_register(sarg1, (low + (long)(z30_rng_next(seed) % (high - low + 1))) * 10000L);
        return true;
    }
    case Z30C_RNGLRAND1:
    {
        unsigned int &seed = zasm30_rngs[Z30_REF(RNG)];
        ri->d[2] = (long)z30_rng_next(seed);
        return true;
    }
    case Z30C_RNGLRAND2:
    {
        long high = get_register(sarg1);
        unsigned int &seed = zasm30_rngs[Z30_REF(RNG)];
        ri->d[2] = high > 0 ? (long)(z30_rng_next(seed) % high) : 0;
        return true;
    }
    case Z30C_RNGLRAND3:
    {
        long low = get_register(sarg1), high = get_register(sarg2);
        if(high < low) zc_swap(low, high);
        unsigned int &seed = zasm30_rngs[Z30_REF(RNG)];
        ri->d[2] = low + (long)(z30_rng_next(seed) % (unsigned long)(high - low + 1));
        return true;
    }
    case Z30C_RNGSEED:
    case Z30C_RNGRSEED:
    {
        zasm30_rngs[Z30_REF(RNG)] = (unsigned int)get_register(sarg1);
        return true;
    }
    case Z30C_RNGFREE:
        zasm30_rngs.erase(Z30_REF(RNG));
        return true;

    // -- stack objects ------------------------------------------------------------------------
    case Z30C_LOADSTACK:
    {
        const long id = zasm30_next_stack++;
        zasm30_stacks[id];
        Z30_REF(STACK) = id;
        ri->d[2] = id;
        return true;
    }
    case Z30C_STACKOWN:
        return true;
    case Z30C_STACKFREE:
        zasm30_stacks.erase(Z30_REF(STACK));
        return true;
    case Z30C_STACKCLEAR:
    {
        if(std::vector<long> *st = z30_stack())
            st->clear();
        return true;
    }
    case Z30C_STACKGET:
    {
        std::vector<long> *st = z30_stack();
        const long idx = get_register(sarg1);
        set_register(sarg1, st && idx >= 0 && idx < (long)st->size() ? (*st)[idx] : 0);
        return true;
    }
    case Z30C_STACKSET:
    {
        std::vector<long> *st = z30_stack();
        const long idx = get_register(sarg1);
        if(st && idx >= 0 && idx < (long)st->size())
            (*st)[idx] = get_register(sarg2);
        return true;
    }
    case Z30C_STACKPOPBACK:
    case Z30C_STACKPEEKBACK:
    {
        std::vector<long> *st = z30_stack();
        const bool has = st && !st->empty();
        set_register(sarg1, has ? st->back() : 0);
        if(has && cmd == Z30C_STACKPOPBACK)
            st->pop_back();
        return true;
    }
    case Z30C_STACKPOPFRONT:
    case Z30C_STACKPEEKFRONT:
    {
        std::vector<long> *st = z30_stack();
        const bool has = st && !st->empty();
        set_register(sarg1, has ? st->front() : 0);
        if(has && cmd == Z30C_STACKPOPFRONT)
            st->erase(st->begin());
        return true;
    }
    case Z30C_STACKPUSHBACK:
    {
        if(std::vector<long> *st = z30_stack())
            st->push_back(get_register(sarg1));
        return true;
    }
    case Z30C_STACKPUSHFRONT:
    {
        if(std::vector<long> *st = z30_stack())
            st->insert(st->begin(), get_register(sarg1));
        return true;
    }

    // -- hero movement and others ---------------------------------------------------------------
    case Z30C_HEROMOVEXY:
    case Z30C_HEROCANMOVEXY:
    case Z30C_HEROMOVEATANGLE:
    case Z30C_HEROCANMOVEATANGLE:
    case Z30C_HEROMOVE:
    case Z30C_HEROCANMOVE:
    {
        const double a = SH::read_stack(SPR + 4) / 10000.0, b = SH::read_stack(SPR + 3) / 10000.0;
        const bool shove = SH::read_stack(SPR) != 0;
        double dx = 0, dy = 0;
        bool valid = true;

        switch(cmd)
        {
        case Z30C_HEROMOVEXY:
        case Z30C_HEROCANMOVEXY:
            dx = a;
            dy = b;
            break;

        case Z30C_HEROMOVEATANGLE:
        case Z30C_HEROCANMOVEATANGLE:
            dx = cos(a * 3.14159265358979 / 180.0) * b;
            dy = sin(a * 3.14159265358979 / 180.0) * b;
            break;

        default:
            valid = z30_npc_dir_delta(SH::read_stack(SPR + 4) / 10000, b, dx, dy);
            break;
        }

        if(!valid)
        {
            ri->d[2] = 0;
            return true;
        }

        const bool moves = cmd == Z30C_HEROMOVEXY || cmd == Z30C_HEROMOVEATANGLE || cmd == Z30C_HEROMOVE;
        const bool ok = moves ? Link.q30_script_move_xy(dx, dy, shove) : Link.q30_script_can_move(dx, dy);
        ri->d[2] = ok ? 10000 : 0;
        return true;
    }
    case Z30C_HEROLIFTGRAB:
    case Z30C_HEROLIFTRELEASE:
        ri->d[2] = 0;
        return true;
    case Z30C_KILLPLAYER:
        game->set_life(0);
        return true;

    // -- npc --------------------------------------------------------------------------------------
    case Z30C_NPCKICKBUCKET:
    {
        if(GuyH::loadNPC(ri->guyref, "npc->Remove()") == SH::_NoError)
        {
            int ind = GuyH::getNPCIndex(ri->guyref);
            GuyH::getNPC()->stop_bgsfx(ind);
            guys.del(ind);
        }
        return true;
    }
    case Z30C_NPCSLIDE:
    {
        if(GuyH::loadNPC(ri->guyref, "npc->Slide()") == SH::_NoError)
            set_register(sarg1, q30_enemy_slide(GuyH::getNPC()) * 10000);
        else
            set_register(sarg1, -10000);
        return true;
    }
    case Z30C_NPCMOVEPAUSED:
        ri->d[2] = 0;
        return true;
    case Z30C_NPCMOVE:
    case Z30C_NPCMOVEANGLE:
    case Z30C_NPCMOVEXY:
    case Z30C_NPCCANMOVEDIR:
    case Z30C_NPCCANMOVEANGLE:
    case Z30C_NPCCANMOVEXY:
        z30_npc_move_command(cmd);
        return true;

    case Z30C_NPCADD:
    {
        ri->d[2] = ri->d[3] = 0;
        ZScriptArray &a = ArrayH::getArray(get_register(sarg1) / 10000);

        if(&a == &INVALIDARRAY || a.Size() < 1)
            return true;

        const long id = a[0] / 10000;
        const int nx = a.Size() == 3 ? a[1] / 10000 : 0, ny = a.Size() == 3 ? a[2] / 10000 : 0;

        if(BC::checkGuyID(id, "npc->Create()") != SH::_NoError)
            return true;

        const word created = addenemy(nx, ny, id, -10);

        if(created == 0)
        {
            ri->guyref = LONG_MAX;
            Z_scripterrlog("Couldn't create NPC \"%s\", screen NPC limit reached\n", guy_string[id]);
            return true;
        }

        word index = guys.Count() - created;    // the main enemy, not a segment
        ri->guyref = guys.spr(index)->getUID();

        for(; index < guys.Count(); ++index)
            ((enemy *)guys.spr(index))->script_spawned = true;

        ri->d[2] = ri->d[3] = ri->guyref;
        return true;
    }

    case Z30C_NPCDEAD:
        if(GuyH::loadNPC(ri->guyref, "npc->isDead()") == SH::_NoError)
            set_register(sarg1, GuyH::getNPC()->dying ? 10000 : 0);
        else
            set_register(sarg1, 0);
        return true;

    case Z30C_NPCATTACK:
        if(GuyH::loadNPC(ri->guyref, "npc->Attack()") == SH::_NoError)
            q30_enemy_fire(GuyH::getNPC());
        return true;
    case Z30C_NPCLINKINRANGE:
    {
        if(GuyH::loadNPC(ri->guyref, "npc->LinkInRange()") == SH::_NoError)
            set_register(sarg1, 0);
        return true;
    }

    // -- weapons / items -------------------------------------------------------------------------
    case Z30C_LWPNDEL:
    {
        z30_list_del(Lwpns, ri->lwpn);
        return true;
    }
    case Z30C_EWPNDEL:
    {
        z30_list_del(Ewpns, ri->ewpn);
        return true;
    }
    case Z30C_ITEMDEL:
    {
        z30_list_del(items, ri->itemref);
        return true;
    }
    case Z30C_RUNITEMSCRIPT:
    {
        const int itemid = ri->idata;
        const long mode = get_register(sarg1) / 10000;

        if(q30_items_keep_running())
        {
            if(unsigned(itemid) < MAXITEMS)
                zasm30_item_run_command(itemid, mode);

            return true;
        }

        if(unsigned(itemid) < MAXITEMS && mode != 0 && itemsbuf[itemid].script)
        {
            // Run it now; a script called from a script is one frame deep anyway here.
            refInfo *r = ri;
            ffscript *c = curscript;
            long(*st)[ZSCRIPT_STACK_ALLOC] = stack;
            Zasm30Frame *f = zasm30_cur_frame;
            Zasm30Instance *in = zasm30_cur_inst;
            long *sp = zasm30_wide_sp;
            long lim = zasm30_stack_limit;
            long a1 = sarg1, a2 = sarg2;
            ZScriptVersion::RunScript(SCRIPT_ITEM, itemsbuf[itemid].script, itemid & 0xFF);
            ri = r;
            curscript = c;
            stack = st;
            zasm30_cur_frame = f;
            zasm30_cur_inst = in;
            zasm30_wide_sp = sp;
            zasm30_stack_limit = lim;
            sarg1 = a1;
            sarg2 = a2;
        }
        return true;
    }

    // -- the X and Y item buttons -------------------------------------------------------------------
    case Z30C_SELECTXWPNR:
    case Z30C_SELECTYWPNR:
    {
        static const byte dirs[4] = { SEL_UP, SEL_DOWN, SEL_LEFT, SEL_RIGHT };
        const long dir = get_register(sarg1) / 10000;

        if(dir >= 0 && dir < 4)
            q30_select_next_button_item(cmd == Z30C_SELECTXWPNR, dirs[dir]);

        return true;
    }

    // -- generic scripts --------------------------------------------------------------------------
    case Z30C_RUNGENFRZSCR:
    {
        bool ok = z30_run_generic_frozen(Z30_REF(GENERICDATA));
        set_register(sarg1, ok ? 10000 : 0);
        return true;
    }

    // -- message strings -----------------------------------------------------------------------
    case Z30C_MESSAGEDATASETSTRINGR:
    case Z30C_MESSAGEDATASETSTRINGV:
    {
        const long id = Z30_REF(MSGDATA);

        if(BC::checkMessage(id, "messagedata->Set()") == SH::_NoError)
        {
            string text;
            ArrayH::getString(get_register(sarg1) / 10000, text, 8192);
            z30_msg_set(id, text);
        }
        return true;
    }

    case Z30C_MESSAGEDATAGETSTRINGR:
    case Z30C_MESSAGEDATAGETSTRINGV:
    {
        const long id = Z30_REF(MSGDATA);

        if(BC::checkMessage(id, "messagedata->Get()") == SH::_NoError)
            ArrayH::setArray(get_register(sarg1) / 10000, string(MsgStrings[id].s));
        return true;
    }

    // -- answers a script branches on ---------------------------------------------------------
    case Z30C_MAPDATAISSOLID:
    {
        const int x = ri->d[0] / 10000, y = ri->d[1] / 10000;
        const long ref = Z30_REF(MAPDATA);
        const mapscr *m = z30_mapdata(ref);

        if(!m)
            set_register(sarg1, 10000);
        else if(m == tmpscr)
            set_register(sarg1, _walkflag(x, y, 1) ? 10000 : 0);
        else
            set_register(sarg1, z30_mapdata_solid(m, x, y) ? 10000 : 0);
        return true;
    }

    case Z30C_SCRTRIGGERCOMBO:
    {
        const int layer = get_register(sarg1) / 10000, pos = get_register(sarg2) / 10000;
        set_register(sarg1, q30_trigger_combo_at(layer, pos, 0) ? 10000 : 0);
        return true;
    }

    case Z30C_ISVALIDBITMAP:
        set_register(sarg1, z30_bitmap(get_register(sarg1)) ? 10000 : 0);
        return true;

    case Z30C_ISALLOCATEDBITMAP:
        set_register(sarg1, zasm30_bitmaps.count(get_register(sarg1)) ? 10000 : 0);
        return true;

    case Z30C_GETCOMBOBYNAME:
    {
        string name;
        ArrayH::getString(get_register(sarg1) / 10000, name, 256);
        long found = -1;

        for(int q = 0; q < MAXCOMBOS && !name.empty(); ++q)
        {
            const Q30Combo *c = q30_combo(q);

            if(c && name == c->label)
            {
                found = q;
                break;
            }
        }

        set_register(sarg1, found * 10000);
        return true;
    }

    // -- bitmap colours (bitmap->ReplaceColors, ShiftColors, MaskDraw) -------------------------
    case Z30C_BMPREPLCOLOR:
    case Z30C_BMPSHIFTCOLOR:
    {
        BITMAP *bmp = z30_bitmap(SH::read_stack(SPR + 4));

        if(bmp)
            z30_replace_colors(bmp, SH::read_stack(SPR + 2) / 10000, SH::read_stack(SPR + 1) / 10000,
                               SH::read_stack(SPR) / 10000, cmd == Z30C_BMPSHIFTCOLOR);
        return true;
    }
    case Z30C_BMPMASKDRAW:
    case Z30C_BMPMASKDRAW2:
    case Z30C_BMPMASKDRAW3:
    {
        const int numargs = cmd == Z30C_BMPMASKDRAW ? 3 : cmd == Z30C_BMPMASKDRAW2 ? 4 : 5;
        BITMAP *dest = z30_bitmap(SH::read_stack(SPR + numargs));
        BITMAP *mask = z30_bitmap(SH::read_stack(SPR + numargs - 2));
        const long color = SH::read_stack(SPR + numargs - 3) / 10000;
        long start = 0x01, end = 0xFF;

        if(numargs >= 4)
            start = SH::read_stack(SPR + numargs - 4) / 10000;

        if(numargs == 4)
            end = start;
        else if(numargs == 5)
            end = SH::read_stack(SPR) / 10000;

        if(dest && mask && unsigned(color) <= 0xFF)
            z30_mask_draw(dest, mask, color, vbound(start, 0, 255), vbound(end, 0, 255));

        return true;
    }

    // -- sounds ---------------------------------------------------------------------------------
    case Z30C_ENDSOUNDR:
    case Z30C_ENDSOUNDV:
    {
        const long id = SH::get_arg(sarg1, cmd == Z30C_ENDSOUNDV) / 10000;

        if(BC::checkSFXID(id, "Audio->EndSound()") == SH::_NoError)
            stop_sfx(id);

        return true;
    }
    case Z30C_ADJUSTSFX:
        q30_sfx_ex(SH::read_stack(SPR + 4) / 10000, SH::read_stack(SPR + 3), SH::read_stack(SPR + 2),
                   SH::read_stack(SPR + 1), SH::read_stack(SPR), false);
        return true;

    // -- npc movement and sound -------------------------------------------------------------------
    case Z30C_NPCSTOPBGSFX:
        if(GuyH::loadNPC(ri->guyref, "npc->StopBGSFX()") == SH::_NoError)
            GuyH::getNPC()->stop_bgsfx(GuyH::getNPCIndex(ri->guyref));
        return true;
    case Z30C_NPCCONSTWALK:
    case Z30C_NPCCONSTWALK8:
    {
        ZScriptArray &a = ArrayH::getArray(get_register(sarg1) / 10000);

        if(&a != &INVALIDARRAY && a.Size() == 3 && GuyH::loadNPC(ri->guyref, "npc->ConstantWalk()") == SH::_NoError)
        {
            const int rate = a[0] / 10000, homing = a[1] / 10000, special = a[2] / 10000;

            if(cmd == Z30C_NPCCONSTWALK)
                q30_enemy_constant_walk(GuyH::getNPC(), rate, homing, special);
            else
                q30_enemy_constant_walk_8(GuyH::getNPC(), rate, homing, special);
        }
        return true;
    }

    // -- palette data ----------------------------------------------------------------------------
    case Z30C_CREATEPALDATA:
        Z30_REF(PALDATA) = z30_new_paldata();
        ri->d[2] = Z30_REF(PALDATA);
        return true;
    case Z30C_CREATEPALDATACLR:
    {
        const long clr = get_register(sarg1);
        Z30_REF(PALDATA) = z30_new_paldata();

        if(Z30PalData *pd = z30_paldata(Z30_REF(PALDATA)))
        {
            RGB c;
            c.r = (byte)vbound((clr >> 16) & 0xFF, 0, z30_color_max());
            c.g = (byte)vbound((clr >> 8) & 0xFF, 0, z30_color_max());
            c.b = (byte)vbound(clr & 0xFF, 0, z30_color_max());
            c.filler = 0;

            for(int q = 0; q < 240; ++q)
            {
                pd->c[q] = c;
                pd->used[q] = true;
            }
        }

        ri->d[2] = Z30_REF(PALDATA);
        return true;
    }
    case Z30C_CREATERGB:
    {
        const int max_value = z30_color_max();
        const long r = vbound(SH::read_stack(SPR + 2) / 10000, 0, max_value);
        const long g = vbound(SH::read_stack(SPR + 1) / 10000, 0, max_value);
        const long b = vbound(SH::read_stack(SPR) / 10000, 0, max_value);
        ri->d[2] = (r << 16) | (g << 8) | b;
        return true;
    }
    case Z30C_CREATERGBHEX:
    {
        const long hex = get_register(sarg1);
        const int shift = z30_color_max() == 255 ? 0 : 2;
        ri->d[2] = ((((hex >> 16) & 0xFF) >> shift) << 16) | ((((hex >> 8) & 0xFF) >> shift) << 8) | ((hex & 0xFF) >> shift);
        return true;
    }
    case Z30C_MIXCLR:
    {
        const long a = SH::read_stack(SPR + 3), b = SH::read_stack(SPR + 2);
        RGB start, end;
        start.r = (byte)((a >> 16) & 0xFF); start.g = (byte)((a >> 8) & 0xFF); start.b = (byte)(a & 0xFF); start.filler = 0;
        end.r = (byte)((b >> 16) & 0xFF); end.g = (byte)((b >> 8) & 0xFF); end.b = (byte)(b & 0xFF); end.filler = 0;
        const RGB out = z30_mix_color(start, end, SH::read_stack(SPR + 1) / 10000.0, SH::read_stack(SPR) / 10000);
        ri->d[2] = (long(out.r) << 16) | (long(out.g) << 8) | out.b;
        return true;
    }
    case Z30C_PALDATALOADLEVEL: z30_paldata_load_level(get_register(sarg1) / 10000); return true;
    case Z30C_PALDATALOADSPRITE:
    {
        const long page = get_register(sarg1) / 10000;
        Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));

        if(pd && (page == 0 || page == 1))
            for(int q = 0; q < 15; ++q)
                z30_pal_load_cset(*pd, q, pSprite(page * 15 + q));

        return true;
    }
    case Z30C_PALDATALOADMAIN:
        if(Z30PalData *pd = z30_paldata(Z30_REF(PALDATA)))
            for(int q = 0; q < 16; ++q)
                z30_pal_load_cset_main(*pd, q);
        return true;
    case Z30C_PALDATALOADCYCLE:
    {
        const long level = get_register(sarg1) / 10000;
        Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));

        if(pd && z30_valid_level(level))
            for(int q = 4; q <= 12; ++q)
                z30_pal_load_cset(*pd, q, level * pdLEVEL + poLEVEL + q);

        return true;
    }
    case Z30C_PALDATAWRITELEVEL: z30_paldata_write_level(get_register(sarg1) / 10000, false, 0); return true;
    case Z30C_PALDATAWRITELEVELCS: z30_paldata_write_level(get_register(sarg1) / 10000, true, get_register(sarg2) / 10000); return true;
    case Z30C_PALDATAWRITEMAIN:
    case Z30C_PALDATAWRITEMAINCS:
    {
        Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));
        const long only = cmd == Z30C_PALDATAWRITEMAINCS ? get_register(sarg1) / 10000 : -1;
        bool changed = false;

        if(pd)
        {
            for(int q = 0; q < 16; ++q)
                if((only < 0 || only == q) && z30_pal_write_cset_main(*pd, q))
                    changed = true;
        }

        if(changed)
            refreshpal = true;

        return true;
    }
    case Z30C_PALDATAWRITECYCLE: z30_paldata_write_cycle(get_register(sarg1) / 10000, -1); return true;
    case Z30C_PALDATAWRITECYCLECS: z30_paldata_write_cycle(get_register(sarg1) / 10000, get_register(sarg2) / 10000); return true;
    case Z30C_PALDATAVALIDCLR:
    {
        Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));
        const long ind = get_register(sarg1) / 10000;
        set_register(sarg1, (pd && ind >= 0 && ind < Z30_PALDATA_COLORS && pd->used[ind]) ? 10000 : 0);
        return true;
    }
    case Z30C_PALDATACLEARCLR:
    {
        Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));
        const long ind = get_register(sarg1) / 10000;

        if(pd && ind >= 0 && ind < Z30_PALDATA_COLORS)
            pd->used[ind] = false;

        return true;
    }
    case Z30C_PALDATACLEARCSET:
    {
        Z30PalData *pd = z30_paldata(Z30_REF(PALDATA));
        const long cs = get_register(sarg1) / 10000;

        if(pd && cs >= 0 && cs < 16)
            for(int q = 0; q < 16; ++q)
                pd->used[CSET(cs) + q] = false;

        return true;
    }
    case Z30C_PALDATAMIX:
    case Z30C_PALDATAMIXCS:
    {
        const int off = cmd == Z30C_PALDATAMIXCS ? 1 : 0;
        Z30PalData *pd = z30_paldata(SH::read_stack(SPR + 4 + off));
        Z30PalData *a = z30_paldata(SH::read_stack(SPR + 3 + off)), *b = z30_paldata(SH::read_stack(SPR + 2 + off));
        const long cset = off ? SH::read_stack(SPR + 2) / 10000 : -1;
        const double percent = SH::read_stack(SPR + 1) / 10000.0;
        const long space = SH::read_stack(SPR) / 10000;

        if(pd && a && b && (off == 0 || (cset >= 0 && cset < 16)))
        {
            const int first = off ? CSET(cset) : 0, last = off ? CSET(cset) + 16 : Z30_PALDATA_COLORS;

            for(int q = first; q < last; ++q)
                if(a->used[q] && b->used[q])
                {
                    pd->c[q] = z30_mix_color(a->c[q], b->c[q], percent, space);
                    pd->used[q] = true;
                }
        }

        return true;
    }
    case Z30C_PALDATACOPY:
    {
        Z30PalData *pd = z30_paldata(Z30_REF(PALDATA)), *dest = z30_paldata(get_register(sarg1));

        if(pd && dest)
            *dest = *pd;

        return true;
    }
    case Z30C_PALDATACOPYCSET:
    {
        Z30PalData *pd = z30_paldata(SH::read_stack(SPR + 3)), *dest = z30_paldata(SH::read_stack(SPR + 2));
        const long cs = SH::read_stack(SPR + 1) / 10000, cs2 = SH::read_stack(SPR) / 10000;

        if(pd && dest && cs >= 0 && cs < 16 && cs2 >= 0 && cs2 < 16)
            for(int q = 0; q < 16; ++q)
            {
                dest->c[CSET(cs2) + q] = pd->c[CSET(cs) + q];
                dest->used[CSET(cs2) + q] = pd->used[CSET(cs) + q];
            }

        return true;
    }
    case Z30C_PALDATAFREE:
        zasm30_paldatas.erase(Z30_REF(PALDATA));
        return true;

    // -- bitmap drawing that the 2.53 draw commands have no form of ----------------------------
    case Z30C_BMPDITHER: z30_bmp_dither(); return true;
    case Z30C_BMPDRAWSCREENCOMBOFR: z30_bmp_draw_screen_flags(); return true;
    case Z30C_BMPDRAWSTRINGR2: z30_bmp_draw_string2(); return true;

    // -- lights for the dark rooms: drawn with the darkness (q30_scripts_light.h) --------------
    case Z30C_DRAWLIGHT_CIRCLE:
    case Z30C_DRAWLIGHT_SQUARE:
    case Z30C_DRAWLIGHT_CONE:
        z30_drawlight_command(cmd);
        return true;

    // -- itemdata->GetDisplayName/SetDisplayName/GetShownName ----------------------------------
    case Z30C_ITEMGETDISPLAYNAME:
    case Z30C_ITEMSETDISPLAYNAME:
    case Z30C_ITEMGETSHOWNNAME:
    {
        const long id = ri->idata;
        const long ptr = get_register(sarg1) / 10000;

        if(id < 0 || id >= MAXITEMS)
            return true;

        if(cmd == Z30C_ITEMSETDISPLAYNAME)
        {
            std::string s;
            ArrayH::getString(ptr, s, 255);
            q30_set_item_display_name(id, s.c_str());
        }
        else
        {
            const std::string s = cmd == Z30C_ITEMGETSHOWNNAME ? q30_item_shown_name(id) : z30_item_display_name(id);
            if(z30_set_string(ptr, s) == SH::_Overflow)
                Z_scripterrlog("Array supplied to 'itemdata->GetDisplayName' not large enough.\n");
        }
        return true;
    }

    // sprintf(dest, format, ...): the arguments are the ones PUSHVARG collected.
    case Z30C_SPRINTFVARG:
    {
        Zasm30Frame &f = zasm30_frame();
        const long dest = SH::read_stack(SPR + 1) / 10000;
        std::string format;
        ArrayH::getString(SH::read_stack(SPR) / 10000, format, 65535);
        const std::string out = z30_format(format, f.vargs);

        if(z30_set_string(dest, out) == SH::_Overflow)
        {
            std::string kept;
            ArrayH::getString(dest, kept, 65535);
            ri->d[2] = (long)kept.size() * 10000;
        }
        else
            ri->d[2] = (long)out.size() * 10000;

        f.vargs.clear();
        return true;
    }

    // run_script reaches here with the end marker only when it stopped the script itself (the
    // game is quitting during a long script): nothing to do.
    case 0xFFFF:
        return true;

    // -- misc no-effect commands: these only tell the engine about objects it doesn't keep ------
    case Z30C_ZCLASS_FREE:
        zasm30_log_once("command", cmd);
        ri->d[2] = 0;
        return true;

    // -- files and directories -----------------------------------------------------------------
    case Z30C_FILEOPEN:
        z30_file_open(get_register(sarg1), "rb+");
        return true;
    case Z30C_FILECREATE:
        z30_file_open(get_register(sarg1), "wb+");
        return true;
    case Z30C_FILEOPENMODE:
    {
        string mode;
        ArrayH::getString(get_register(sarg2) / 10000, mode, 16);
        z30_file_open(get_register(sarg1), mode.c_str());
        return true;
    }
    case Z30C_FILECLOSE:
    {
        if(Z30File *f = z30_file(Z30_REF(FILE), false))
            z30_file_close(f);
        return true;
    }
    case Z30C_FILEFREE:
    {
        if(Z30File *f = z30_file(Z30_REF(FILE), false))
        {
            z30_file_close(f);
            f->used = false;
        }
        return true;
    }
    case Z30C_FILEISALLOCATED:
        ri->d[2] = z30_file(Z30_REF(FILE), false) ? 10000 : 0;
        return true;
    case Z30C_FILEISVALID:
        ri->d[2] = z30_file(Z30_REF(FILE), true) ? 10000 : 0;
        return true;
    case Z30C_FILEALLOCATE:
        Z30_REF(FILE) = z30_new_file();
        ri->d[3] = Z30_REF(FILE);
        ri->d[2] = Z30_REF(FILE) ? 10000 : 0;
        return true;
    case Z30C_FILEFLUSH:
    {
        Z30File *f = z30_file(Z30_REF(FILE), true);
        ri->d[2] = f && !fflush(f->f) ? 10000 : 0;
        return true;
    }
    case Z30C_FILEGETCHAR:
    {
        Z30File *f = z30_file(Z30_REF(FILE), true);
        ri->d[2] = f ? fgetc(f->f) * 10000 : -10000;
        return true;
    }
    case Z30C_FILEPUTCHAR:
    {
        Z30File *f = z30_file(Z30_REF(FILE), true);
        ri->d[2] = f ? fputc((char)(get_register(sarg1) / 10000), f->f) * 10000 : -10000;
        return true;
    }
    case Z30C_FILEUNGETCHAR:
    {
        Z30File *f = z30_file(Z30_REF(FILE), true);
        ri->d[2] = f ? ungetc((char)(get_register(sarg1) / 10000), f->f) * 10000 : -10000;
        return true;
    }
    case Z30C_FILEREWIND:
    {
        if(Z30File *f = z30_file(Z30_REF(FILE), true))
            rewind(f->f);
        return true;
    }
    case Z30C_FILECLEARERR:
    {
        if(Z30File *f = z30_file(Z30_REF(FILE), true))
            clearerr(f->f);
        return true;
    }
    case Z30C_FILESEEK:
    {
        Z30File *f = z30_file(Z30_REF(FILE), true);
        ri->d[2] = f && !fseek(f->f, get_register(sarg1), get_register(sarg2) ? SEEK_CUR : SEEK_SET) ? 10000 : 0;
        return true;
    }
    case Z30C_FILEGETERROR:
    {
        if(Z30File *f = z30_file(Z30_REF(FILE), true))
        {
            const int err = ferror(f->f);
            ArrayH::setArray(get_register(sarg1) / 10000, string(err ? strerror(err) : ""));
        }
        return true;
    }
    case Z30C_FILEREMOVE:
    {
        Z30File *f = z30_file(Z30_REF(FILE), true);
        ri->d[2] = 0;
        if(f)
        {
            const std::string path = f->path;
            z30_file_close(f);
            ri->d[2] = remove(path.c_str()) ? 0 : 10000;
        }
        return true;
    }
    case Z30C_FILEREADCHARS:
        z30_file_read_chars();
        return true;
    case Z30C_FILEREADBYTES:
        z30_file_read_bytes();
        return true;
    case Z30C_FILEREADINTS:
        z30_file_read_ints();
        return true;
    case Z30C_FILEREADSTR:
        z30_file_read_line();
        return true;
    case Z30C_FILEWRITECHARS:
        z30_file_write_chars();
        return true;
    case Z30C_FILEWRITEBYTES:
        z30_file_write_bytes();
        return true;
    case Z30C_FILEWRITEINTS:
        z30_file_write_ints();
        return true;
    case Z30C_FILEWRITESTR:
    {
        Z30File *f = z30_file(Z30_REF(FILE), true);
        ri->d[2] = 0;
        if(f)
        {
            string out;
            ArrayH::getString(get_register(sarg1) / 10000, out, 65535);
            ri->d[2] = (long)fwrite(out.data(), 1, out.size(), f->f) * 10000;
        }
        return true;
    }
    case Z30C_FILESYSREMOVE:
    case Z30C_DIREXISTS:
    case Z30C_FILEEXISTS:
    {
        string user;
        std::string path;
        ArrayH::getString(get_register(sarg1) / 10000, user, 2048);
        const bool ok = z30_resolve_path(user, cmd != Z30C_DIREXISTS, path);
        if(cmd == Z30C_FILESYSREMOVE)
            set_register(sarg1, ok && !remove(path.c_str()) ? 10000 : 0);
        else
            set_register(sarg1, ok && z30_path_is(path, cmd == Z30C_DIREXISTS) ? 10000 : 0);
        return true;
    }
    case Z30C_LOADDIRECTORYR:
    {
        string user;
        ArrayH::getString(get_register(sarg1) / 10000, user, 2048);
        Z30_REF(DIRECTORY) = z30_load_dir(user);
        set_register(sarg1, Z30_REF(DIRECTORY));
        return true;
    }
    case Z30C_DIRECTORYGET:
    {
        Z30Dir *d = z30_dir(Z30_REF(DIRECTORY));
        const long idx = get_register(sarg1) / 10000, ptr = get_register(sarg2) / 10000;
        const bool has = d && idx >= 0 && idx < (long)d->names.size();
        if(d)
            ArrayH::setArray(ptr, has ? d->names[idx] : string());
        set_register(sarg1, has ? 10000 : 0);
        return true;
    }
    case Z30C_DIRECTORYRELOAD:
    {
        if(Z30Dir *d = z30_dir(Z30_REF(DIRECTORY)))
            z30_dir_load(*d);
        return true;
    }
    case Z30C_DIRECTORYFREE:
    {
        if(Z30Dir *d = z30_dir(Z30_REF(DIRECTORY)))
        {
            d->used = false;
            d->names.clear();
        }
        return true;
    }

    default:
        return false;
    }
}

#endif

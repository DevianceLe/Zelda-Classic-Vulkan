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
        // Subscreen objects aren't modeled in this engine: pages and transitions read as 0.
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
    case Z30C_LOADSUBDATARV:
    {
        set_register(sarg1, 0);
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
    {
        Link.setX(int(Link.getX()) + SH::read_stack(SPR + 4) / 10000);
        Link.setY(int(Link.getY()) + SH::read_stack(SPR + 3) / 10000);
        ri->d[2] = 10000;
        return true;
    }
    case Z30C_HEROCANMOVEXY:
    {
        ri->d[2] = 10000;
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
    case Z30C_NPCMOVEANGLE:
    {
        ri->d[2] = 0;
        if(GuyH::loadNPC(ri->guyref, "npc->MoveAtAngle()") == SH::_NoError)
        {
            sprite *s = GuyH::getNPC();
            double a = (ri->d[0] / 10000.0) * (3.14159265358979 / 180.0);
            double px = ri->d[3] / 10000.0;
            s->x = fix(double(s->x) + cos(a) * px);
            s->y = fix(double(s->y) + sin(a) * px);
            ri->d[2] = 10000;
        }
        return true;
    }
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

    // -- generic scripts --------------------------------------------------------------------------
    case Z30C_RUNGENFRZSCR:
    {
        bool ok = z30_run_generic_frozen(Z30_REF(GENERICDATA));
        set_register(sarg1, ok ? 10000 : 0);
        return true;
    }

    // -- misc no-effect commands: these only tell the engine about objects it doesn't keep ------
    case Z30C_SUBPAGE_FIND_WIDGET_BY_LABEL:
    case Z30C_SUBWIDG_TY_SETTEXT:
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

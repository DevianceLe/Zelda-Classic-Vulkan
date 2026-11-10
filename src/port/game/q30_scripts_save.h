// Screen->Data[] and mapdata->Data[] (upstream's game->screen_data) for 2.55 and 3.0 quests, kept
// across saves. Included once, from q30_scripts_cmds.h.
//
// Like the trigger states (q30_triggers.cpp), the data lives in one extra array at the end of
// game->globalRAM, so a save carries it with no change to the save format, and a quest that
// never uses screen data never gets the array. The array is kept current as scripts write, so
// every place that copies the game into a save slot copies it too. Layout: magic, version, then
// for each screen with data: map screen index, size, and that many values.

#ifndef ZC_Q30_SCRIPTS_SAVE_H
#define ZC_Q30_SCRIPTS_SAVE_H

#include <map>
#include <vector>

static const long Z30_SCRDATA_MAGIC = 0x51333044;     // "Q30D"
static const long Z30_SCRDATA_VERSION = 1;
static const long Z30_SCRDATA_MAX = 214748;           // upstream's bound on one screen's size

static std::map<int, std::vector<long> > zasm30_scrdata;        // this game's
static std::map<int, std::vector<long> > zasm30_scrdata_init;   // the quest's init data
static std::map<int, size_t> zasm30_scrdata_pos;                // where each screen's values start in the array

// ---- from the init data reader (q255_init.cpp) ----------------------------------------------------

void q30_clear_screen_data_init()
{
    zasm30_scrdata_init.clear();
}

void q30_set_screen_data_init(int index, long size, int key, long value)
{
    if(index < 0 || size <= 0 || size > Z30_SCRDATA_MAX)
        return;

    std::vector<long> &v = zasm30_scrdata_init[index];

    if((long)v.size() < size)
        v.resize(size, 0);

    if(key >= 0 && key < size)
        v[key] = value;
}

// ---- the array in globalRAM -----------------------------------------------------------------------

static ZScriptArray *z30_scrdata_array()
{
    if(!game)
        return NULL;

    for(size_t i = game->globalRAM.size(); i > 0; --i)
    {
        ZScriptArray &a = game->globalRAM[i - 1];

        if(a.Size() >= 2 && a[0] == Z30_SCRDATA_MAGIC)
            return &a;
    }

    return NULL;
}

static void z30_scrdata_flush()
{
    if(!game)
        return;

    ZScriptArray *a = z30_scrdata_array();
    zasm30_scrdata_pos.clear();

    if(!a)
    {
        if(zasm30_scrdata.empty())
            return;

        game->globalRAM.resize(game->globalRAM.size() + 1);
        a = &game->globalRAM.back();
    }

    size_t total = 2;

    for(std::map<int, std::vector<long> >::const_iterator it = zasm30_scrdata.begin(); it != zasm30_scrdata.end(); ++it)
        total += 2 + it->second.size();

    a->Resize((dword)total);
    (*a)[0] = Z30_SCRDATA_MAGIC;
    (*a)[1] = Z30_SCRDATA_VERSION;
    size_t at = 2;

    for(std::map<int, std::vector<long> >::const_iterator it = zasm30_scrdata.begin(); it != zasm30_scrdata.end(); ++it)
    {
        (*a)[(dword)at++] = it->first;
        (*a)[(dword)at++] = (long)it->second.size();
        zasm30_scrdata_pos[it->first] = at;

        for(size_t k = 0; k < it->second.size(); ++k)
            (*a)[(dword)at++] = it->second[k];
    }
}

// ZC_Q30_SCRDATALOG: what screen data a game starts with or saves (diagnostics only).
static void z30_scrdata_log(const char *when)
{
    static const bool log = getenv("ZC_Q30_SCRDATALOG") != NULL;

    if(!log)
        return;

    al_trace("q30 scrdata: %s: %d screens\n", when, (int)zasm30_scrdata.size());

    int shown = 0;
    for(std::map<int, std::vector<long> >::const_iterator it = zasm30_scrdata.begin(); it != zasm30_scrdata.end() && shown < 12; ++it, ++shown)
        al_trace("q30 scrdata:   map %d screen %d: size %d, [0] = %ld\n", it->first / MAPSCRS, it->first % MAPSCRS,
                 (int)it->second.size(), it->second.empty() ? 0L : it->second[0]);
}

// ZC_Q30_SAVETEST=frame: saves the game once at that frame, as a save point would (diagnostics only).
static void z30_scrdata_savetest()
{
    static const char *at = getenv("ZC_Q30_SAVETEST");
    static bool done = false;

    if(at && !done && frame >= atoi(at))
    {
        done = true;
        z30_scrdata_log("saving");
        save_game(false);
    }
}

// A game starts: a new one from the quest's init data, a loaded one from its save.
static void z30_scrdata_game_init(bool first)
{
    zasm30_scrdata.clear();
    zasm30_scrdata_pos.clear();

    if(first)
    {
        zasm30_scrdata = zasm30_scrdata_init;
        z30_scrdata_flush();
        z30_scrdata_log("new game");
        return;
    }

    ZScriptArray *a = z30_scrdata_array();

    if(!a || (*a)[1] != Z30_SCRDATA_VERSION)
        return;

    for(dword i = 2; i + 1 < a->Size(); )
    {
        const long index = (*a)[i], size = (*a)[i + 1];
        i += 2;

        if(index < 0 || size < 0 || size > Z30_SCRDATA_MAX || i + (dword)size > a->Size())
            break;

        std::vector<long> &v = zasm30_scrdata[(int)index];
        v.resize(size);

        for(long k = 0; k < size; ++k)
            v[k] = (*a)[i + (dword)k];

        i += (dword)size;
    }

    z30_scrdata_flush();
    z30_scrdata_log("restored from the save");
}

// ---- what scripts use -----------------------------------------------------------------------------

static long z30_scrdata_size(int index)
{
    std::map<int, std::vector<long> >::const_iterator it = zasm30_scrdata.find(index);
    return it == zasm30_scrdata.end() ? 0 : (long)it->second.size();
}

static void z30_scrdata_resize(int index, long size)
{
    size = vbound(size, 0, Z30_SCRDATA_MAX);

    if(z30_scrdata_size(index) == size)
        return;

    if(size == 0)
        zasm30_scrdata.erase(index);
    else
        zasm30_scrdata[index].resize(size, 0);

    z30_scrdata_flush();
}

static bool z30_scrdata_get(int index, long n, long &ret)
{
    std::map<int, std::vector<long> >::const_iterator it = zasm30_scrdata.find(index);

    if(it == zasm30_scrdata.end() || n < 0 || n >= (long)it->second.size())
    {
        Z_scripterrlog("Invalid index %ld into screen data of size %ld\n", n, z30_scrdata_size(index));
        ret = 0;
        return false;
    }

    ret = it->second[n];
    return true;
}

static void z30_scrdata_set(int index, long n, long value)
{
    std::map<int, std::vector<long> >::iterator it = zasm30_scrdata.find(index);

    if(it == zasm30_scrdata.end() || n < 0 || n >= (long)it->second.size())
    {
        Z_scripterrlog("Invalid index %ld into screen data of size %ld\n", n, z30_scrdata_size(index));
        return;
    }

    it->second[n] = value;

    // the saved copy follows at once: in place when it has room for this screen, else rebuilt
    ZScriptArray *a = z30_scrdata_array();
    std::map<int, size_t>::const_iterator pos = zasm30_scrdata_pos.find(index);

    if(a && pos != zasm30_scrdata_pos.end() && pos->second + n < a->Size())
        (*a)[(dword)(pos->second + n)] = value;
    else
        z30_scrdata_flush();
}

// The 3.0 array collector (zasm30_collect) already scans every globalRAM array, so arrays this
// data holds stay alive through the saved copy.

// The map screen a Screen-> or mapdata-> register is about; -1 if none.
static int z30_scrdata_index_screen()
{
    return currmap * MAPSCRS + currscr;
}

static int z30_scrdata_index_mapdata(long ref)
{
    if(ref <= 0)
        return -1;

    if(ref & Z30_MAPDATA_TMP)
        return (ref & 0xFFFF) == 0 ? currmap * MAPSCRS + currscr : -1;

    return (ref - 1) < (long)TheMaps.size() ? (int)(ref - 1) : -1;
}

static bool zasm30_scrdata_get(const long arg, long &ret)
{
    switch(arg)
    {
    case Z30R_SCREENSCRDATASIZE:
        ret = z30_scrdata_size(z30_scrdata_index_screen()) * 10000;
        return true;
    case Z30R_SCREENSCRDATA:
        z30_scrdata_get(z30_scrdata_index_screen(), Z30_INDEX, ret);
        return true;
    case Z30R_MAPDATASCRDATASIZE:
    {
        const int index = z30_scrdata_index_mapdata(Z30_REF(MAPDATA));
        ret = index < 0 ? -10000 : z30_scrdata_size(index) * 10000;
        return true;
    }
    case Z30R_MAPDATASCRDATA:
    {
        const int index = z30_scrdata_index_mapdata(Z30_REF(MAPDATA));
        ret = 0;
        if(index >= 0)
            z30_scrdata_get(index, Z30_INDEX, ret);
        return true;
    }
    }

    return false;
}

static bool zasm30_scrdata_set(const long arg, const long value)
{
    switch(arg)
    {
    case Z30R_SCREENSCRDATASIZE:
        z30_scrdata_resize(z30_scrdata_index_screen(), value / 10000);
        return true;
    case Z30R_SCREENSCRDATA:
        z30_scrdata_set(z30_scrdata_index_screen(), Z30_INDEX, value);
        return true;
    case Z30R_MAPDATASCRDATASIZE:
    {
        const int index = z30_scrdata_index_mapdata(Z30_REF(MAPDATA));
        if(index >= 0)
            z30_scrdata_resize(index, value / 10000);
        return true;
    }
    case Z30R_MAPDATASCRDATA:
    {
        const int index = z30_scrdata_index_mapdata(Z30_REF(MAPDATA));
        if(index >= 0)
            z30_scrdata_set(index, Z30_INDEX, value);
        return true;
    }
    }

    return false;
}

#endif

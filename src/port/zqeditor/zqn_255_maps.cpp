// Saving a quest made in 2.55 as 3.0 Nightly (zqn_255.h): the maps (MAP), every map's screens.
//
// Read the way 3.0's readmaps reads a 2.55 quest's section (core/qst_maps.cpp): the map count;
// for each map a valid byte (versions after 22), its auto layer combos (after 25; the auto
// palette from 36) and its region ids (from 31); then its 136 screens. Screens before version 23
// hold every field in a fixed order (readmapscreen_old, with the 2.55 additions it reads when the
// quest's header is newer than 2.53: 100 unused bytes, the screen script, its preload flag, the
// hidden layers); from 23 they come in groups a "has" bitfield says are there (readmapscreen).
// Written as 3.0's version 41 (writemaps, writemapscreen in zq/zq_class.cpp).
//
// What 3.0 makes of them as it reads:
// - every screen starts as a fresh mapscr: a group a screen doesn't have keeps those defaults
//   (guy tile -1, side warps "scroll", doors "none", layer opacity 255, combo sensitivity 1,
//   secret sound 27, hold-up sound 20, no music of its own (-1), the default "no reset" and "no
//   carry over" states);
// - before version 27 the guy tile is -1 and the room flags come from the guy (a fairy is always
//   there; the guy fires unless it's a fairy and qr_NOFAIRYGUYFIRES is on), as the old reader
//   does for every version;
// - before version 29 a door "none" is a wall (with no door group: all four);
// - before version 34 the 16-bit "no reset" and "no carry over" get the light beam, temporary
//   no-return and visited bits (and no carry over the four doors, never-return and no enemies
//   return too), present or not;
// - before version 37 a screen's MIDI becomes a number in 3.0's music list, the one the DMaps and
//   the advanced music use (zqn255_music_list);
// - an enemy number past 512 is none;
// - FFCs: after the last one in use none are kept; written as 32 at least when
//   qr_OLD_FFC_FUNCTIONALITY is on (mapscr::numFFC), with layer 1 before version 33 and no
//   viewport ranges before 40;
// - region ids: maps before version 31 have none of their own; a version 21 DMap section gives
//   its DMaps' maps theirs (zqn255_dmap_regions), which 3.0 keeps for the maps.
//
// 2.55.17 (versions 18-30, written as its version 30) reads them the same way, but keeps what 3.0
// changes or drops: the 16-bit "no reset" and "no carry over" as stored (a fresh screen's are 0),
// each screen's own MIDI, the FFCs' InitA, and the arrays 2.55 never used (the "unused" group);
// it has no auto palette, region ids, maze wipe, exstates or viewport ranges. See write_maps_255.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_writers.h"
#include "zqn_255.h"

namespace
{

const int SCREENS = 136, SQUARES = 176, FFCS = 128, MAPS = 255, GUYS = 512;
const byte VALID = 0x01;
const byte GUY_FAIRY = 6;
const byte DOOR_NONE = 16, DOOR_WALL = 0, WARP_SCROLL = 3;
const word ROOM_ALWAYS_GUY = 0x0001, ROOM_GUY_FIRES = 0x0002;

// mapscr's state bits (core/mapscr.h)
const dword S_DOOR_UP = 0x1, S_DOOR_DOWN = 0x2, S_DOOR_LEFT = 0x4, S_DOOR_RIGHT = 0x8, S_NEVERRET = 0x40,
            S_TMPNORET = 0x80, S_VISITED = 0x4000, S_LIGHTBEAM = 0x8000, S_NO_ENEMIES_RETURN = 0x10000;
const dword DEF_NORESET = S_TMPNORET | S_VISITED;
const dword DEF_NOCARRY = S_TMPNORET | S_VISITED | S_DOOR_UP | S_DOOR_DOWN | S_DOOR_LEFT | S_DOOR_RIGHT | S_NEVERRET |
                          S_NO_ENEMIES_RETURN;
// what readmapscreen gives "no reset" and "no carry over" before version 34
const dword OLD_NORESET = S_LIGHTBEAM | S_TMPNORET | S_VISITED;
const dword OLD_NOCARRY = OLD_NORESET | S_DOOR_UP | S_DOOR_DOWN | S_DOOR_LEFT | S_DOOR_RIGHT | S_NEVERRET |
                          S_NO_ENEMIES_RETURN;

const dword HAS_ROOMDATA = 0x1, HAS_ITEM = 0x2, HAS_TWARP = 0x4, HAS_SWARP = 0x8, HAS_WARPRET = 0x10,
            HAS_LAYERS = 0x20, HAS_MAZE = 0x40, HAS_D_S_U = 0x80, HAS_FLAGS = 0x100, HAS_ENEMY = 0x200,
            HAS_CARRY = 0x400, HAS_SCRIPT = 0x800, HAS_SECRETS = 0x2000, HAS_COMBOFLAG = 0x4000,
            HAS_MISC = 0x8000;

const byte SCREEN_GRAVITY = 0x03;   // flags10: the screen has its own gravity
const int QR_NOFAIRYGUYFIRES = 138, QR_OLD_FFC_FUNCTIONALITY = 400;

// script_config: a script, its eight run arguments, its instance settings
struct Config
{
    word script;
    long args[8];
    std::vector<std::pair<word, long> > inst;

    Config() : script(0) { memset(args, 0, sizeof(args)); }

    bool empty() const
    {
        if(script || !inst.empty())
            return false;

        for(int q = 0; q < 8; ++q)
            if(args[q])
                return false;

        return true;
    }
};

// ffcdata as the map section holds it
struct Ffc
{
    word data;
    byte cset;
    word delay;
    long x, y, vx, vy, ax, ay;     // zfix: the value times 10000
    byte link;
    long hit_width, hit_height;
    byte txsz, tysz;
    dword flags;
    Config sc;
    byte layer;
    long suspend, despawn;          // viewport ranges
    byte inita[2];                  // 2.55.17 only: InitA (3.0 dropped them)

    Ffc() : data(0), cset(0), delay(0), x(0), y(0), vx(0), vy(0), ax(0), ay(0), link(0), hit_width(0), hit_height(0),
        txsz(0), tysz(0), flags(0), layer(1), suspend(0), despawn(0)
    {
        inita[0] = inita[1] = 0;
    }
};

// 3.0's mapscr, the fields its map section holds, with a fresh mapscr's values
struct Scr
{
    byte valid, guy;
    long guytile;
    byte guycs;
    word roomflags, str;
    byte room;
    word item;
    byte hasitem;
    byte tilewarptype[4];
    byte tilewarpoverlayflags;
    word door_combo_set;
    byte warpreturnx[4], warpreturny[4];
    word warpreturnc;
    byte stairx, stairy, itemx, itemy;
    word color;
    byte door[4];
    word tilewarpdmap[4];
    byte tilewarpscr[4];
    byte exitdir;
    word enemy[10];
    byte pattern;
    byte sidewarptype[4];
    byte sidewarpoverlayflags, warparrivalx, warparrivaly;
    byte path[4];
    byte maze_wipe;
    byte sidewarpscr[4];
    word sidewarpdmap[4];
    byte sidewarpindex;
    word undercombo;
    byte undercset;
    word catchall;
    byte flags[11];                 // flags .. flags10, flags11 (the old enemy flags)
    byte csensitive;
    dword noreset, nocarry, exstate_reset, exstate_carry;
    byte layermap[6], layerscreen[6], layeropacity[6];
    word timedwarptics;
    byte nextmap, nextscr;
    word secretcombo[128];
    byte secretcset[128], secretflag[128];
    word data[SQUARES];
    byte sflag[SQUARES], cset[SQUARES];
    Ffc ffc[FFCS];
    word oceansfx, bosssfx, secretsfx, holdupsfx;
    long music;
    byte lens_layer, lens_show, lens_hide;
    Config sc;
    byte preloadscript;
    byte hidelayers, hidescriptlayers;
    std::string notes;
    long gravity, terminal_v;
    // 2.55.17 only: the screen's own MIDI, the unused arrays
    short screen_midi;
    long npcstrings[10];
    word new_items[10], new_item_x[10], new_item_y[10];

    void clear()
    {
        valid = guy = 0;
        guytile = -1;
        guycs = 0;
        roomflags = str = 0;
        room = 0;
        item = 0;
        hasitem = 0;
        memset(tilewarptype, 0, sizeof(tilewarptype));
        tilewarpoverlayflags = 0;
        door_combo_set = 0;
        memset(warpreturnx, 0, sizeof(warpreturnx));
        memset(warpreturny, 0, sizeof(warpreturny));
        warpreturnc = 0;
        stairx = stairy = itemx = itemy = 0;
        color = 0;
        memset(door, DOOR_NONE, sizeof(door));
        memset(tilewarpdmap, 0, sizeof(tilewarpdmap));
        memset(tilewarpscr, 0, sizeof(tilewarpscr));
        exitdir = 0;
        memset(enemy, 0, sizeof(enemy));
        pattern = 0;
        memset(sidewarptype, WARP_SCROLL, sizeof(sidewarptype));
        sidewarpoverlayflags = warparrivalx = warparrivaly = 0;
        memset(path, 0, sizeof(path));
        maze_wipe = 0;
        memset(sidewarpscr, 0, sizeof(sidewarpscr));
        memset(sidewarpdmap, 0, sizeof(sidewarpdmap));
        sidewarpindex = 0;
        undercombo = 0;
        undercset = 0;
        catchall = 0;
        memset(flags, 0, sizeof(flags));
        csensitive = 1;
        noreset = DEF_NORESET;
        nocarry = DEF_NOCARRY;
        exstate_reset = exstate_carry = 0;
        memset(layermap, 0, sizeof(layermap));
        memset(layerscreen, 0, sizeof(layerscreen));
        memset(layeropacity, 255, sizeof(layeropacity));
        timedwarptics = 0;
        nextmap = nextscr = 0;
        memset(secretcombo, 0, sizeof(secretcombo));
        memset(secretcset, 0, sizeof(secretcset));
        memset(secretflag, 0, sizeof(secretflag));
        memset(data, 0, sizeof(data));
        memset(sflag, 0, sizeof(sflag));
        memset(cset, 0, sizeof(cset));

        for(int m = 0; m < FFCS; ++m)
            ffc[m] = Ffc();

        oceansfx = bosssfx = 0;
        secretsfx = 27;
        holdupsfx = 20;
        music = -1;
        lens_layer = lens_show = lens_hide = 0;
        sc = Config();
        preloadscript = 0;
        hidelayers = hidescriptlayers = 0;
        notes.clear();
        gravity = terminal_v = 0;
        screen_midi = -1;
        memset(npcstrings, 0, sizeof(npcstrings));
        memset(new_items, 0, sizeof(new_items));
        memset(new_item_x, 0, sizeof(new_item_x));
        memset(new_item_y, 0, sizeof(new_item_y));
    }
};

void get_bytes(Zqn255In &in, byte *dst, int n)
{
    for(int k = 0; k < n; ++k)
        dst[k] = in.u8();
}

void get_words(Zqn255In &in, word *dst, int n)
{
    for(int k = 0; k < n; ++k)
        dst[k] = in.u16();
}

// The guy's room flags, as the old reader and readmapscreen before version 27 set them.
void guy_room_flags(Scr &s, bool nofairyfires)
{
    s.guytile = -1;

    if(s.guy == GUY_FAIRY)
        s.roomflags |= ROOM_ALWAYS_GUY;
    else
        s.roomflags &= ~ROOM_ALWAYS_GUY;

    if(s.guy != GUY_FAIRY || !nofairyfires)
        s.roomflags |= ROOM_GUY_FIRES;
    else
        s.roomflags &= ~ROOM_GUY_FIRES;
}

word clear_bad_enemy(word e)
{
    return e > GUYS ? 0 : e;
}

// readmapscreen_old for a 2.55 quest (map versions 18-22): every field in a fixed order. t255:
// as 2.55.17 reads it (the states as stored, the MIDI, InitA and the unused arrays kept).
void read_old(Zqn255In &in, int v, bool nofairyfires, Scr &s, bool t255 = false)
{
    s.valid = in.u8();
    s.guy = in.u8();
    guy_room_flags(s, nofairyfires);
    s.str = in.u16();
    s.room = in.u8();
    s.item = in.u8();
    s.hasitem = in.u8();
    get_bytes(in, s.tilewarptype, 4);
    s.door_combo_set = in.u16();
    get_bytes(in, s.warpreturnx, 4);
    get_bytes(in, s.warpreturny, 4);
    s.warpreturnc = in.u16();
    s.stairx = in.u8();
    s.stairy = in.u8();
    s.itemx = in.u8();
    s.itemy = in.u8();
    s.color = in.u16();
    s.flags[10] = in.u8();
    get_bytes(in, s.door, 4);
    get_words(in, s.tilewarpdmap, 4);
    get_bytes(in, s.tilewarpscr, 4);
    s.tilewarpoverlayflags = in.u8();
    s.exitdir = in.u8();

    for(int k = 0; k < 10; ++k)
        s.enemy[k] = clear_bad_enemy(in.u16());

    s.pattern = in.u8();
    get_bytes(in, s.sidewarptype, 4);
    s.sidewarpoverlayflags = in.u8();
    s.warparrivalx = in.u8();
    s.warparrivaly = in.u8();
    get_bytes(in, s.path, 4);
    get_bytes(in, s.sidewarpscr, 4);
    get_words(in, s.sidewarpdmap, 4);
    s.sidewarpindex = in.u8();
    s.undercombo = in.u16();
    s.undercset = in.u8();
    s.catchall = in.u16();

    for(int k = 0; k < 5; ++k)
        s.flags[k] = in.u8();

    s.noreset = in.u16() | (t255 ? 0 : OLD_NORESET);
    s.nocarry = in.u16() | (t255 ? 0 : OLD_NOCARRY);

    if(s.flags[4] & 32)
    {
        s.flags[4] &= ~32;
        s.noreset |= 48;
    }

    for(int k = 5; k < 10; ++k)
        s.flags[k] = in.u8();

    s.csensitive = in.u8();
    s.oceansfx = in.u8();
    s.bosssfx = in.u8();
    s.secretsfx = in.u8();
    s.holdupsfx = in.u8();
    get_bytes(in, s.layermap, 6);
    get_bytes(in, s.layerscreen, 6);
    get_bytes(in, s.layeropacity, 6);
    s.timedwarptics = in.u16();
    s.nextmap = in.u8();
    s.nextscr = in.u8();
    get_words(in, s.secretcombo, 128);
    get_bytes(in, s.secretcset, 128);
    get_bytes(in, s.secretflag, 128);
    get_words(in, s.data, SQUARES);
    get_bytes(in, s.sflag, SQUARES);
    get_bytes(in, s.cset, SQUARES);
    s.screen_midi = (short)in.u16();    // 3.0: the music list has its number
    s.lens_layer = in.u8();
    const dword bits = in.u32();

    for(int m = 0; m < 32; ++m)
    {
        if(!((bits >> m) & 1))
            continue;

        Ffc &c = s.ffc[m];
        c.data = in.u16();
        c.cset = in.u8();
        c.delay = in.u16();
        c.x = in.s32();
        c.y = in.s32();
        c.vx = in.s32();
        c.vy = in.s32();
        c.ax = in.s32();
        c.ay = in.s32();
        c.link = in.u8();
        byte b = in.u8();
        c.hit_width = (b & 0x3F) + 1;
        c.txsz = (byte)((b >> 6) + 1);
        b = in.u8();
        c.hit_height = (b & 0x3F) + 1;
        c.tysz = (byte)((b >> 6) + 1);
        c.flags = in.u32();
        c.sc.script = in.u16();

        for(int q = 0; q < 8; ++q)
            c.sc.args[q] = in.s32();

        c.inita[0] = in.u8();       // InitA, which 3.0 doesn't have
        c.inita[1] = in.u8();
    }

    if(v >= 19)
    {
        // arrays 2.55 never used (3.0 skips them)
        for(int q = 0; q < 10; ++q) s.npcstrings[q] = in.s32();
        get_words(in, s.new_items, 10);
        get_words(in, s.new_item_x, 10);
        get_words(in, s.new_item_y, 10);
    }

    if(v >= 20)
    {
        s.sc.script = in.u16();

        for(int q = 0; q < 8; ++q)
            s.sc.args[q] = in.s32();
    }

    if(v >= 21)
        s.preloadscript = in.u8();

    if(v >= 22)
    {
        s.hidelayers = in.u8();
        s.hidescriptlayers = in.u8();
    }

    for(int k = 0; k < 4; ++k)
        if(s.door[k] == DOOR_NONE)
            s.door[k] = DOOR_WALL;
}

// p_getvar(script_config): the script, the run arguments (a count, then each), the instance
// settings (a count, then word / long pairs). False for more than eight run arguments, which
// 3.0's reader would leave unread.
bool read_config(Zqn255In &in, Config &c)
{
    c.script = in.u16();
    const int n = in.u16();

    if(n > 8)
        return false;

    for(int q = 0; q < n; ++q)
        c.args[q] = in.s32();

    const int ni = in.u16();

    for(int q = 0; q < ni && !in.bad; ++q)
    {
        const word k = in.u16();
        const long val = in.s32();
        bool found = false;

        for(size_t e = 0; e < c.inst.size(); ++e)
            if(c.inst[e].first == k)
            {
                c.inst[e].second = val;
                found = true;
            }

        if(!found)
            c.inst.push_back(std::make_pair(k, val));
    }

    // a std::map: in key order
    for(size_t a = 1; a < c.inst.size(); ++a)
        for(size_t b = a; b > 0 && c.inst[b - 1].first > c.inst[b].first; --b)
            std::swap(c.inst[b - 1], c.inst[b]);

    return true;
}

// readmapscreen for versions 23-41: groups the "has" bits say are there. False when the screen
// isn't one 3.0's reader takes. t255: as 2.55.17 reads it (versions 23-30: the states as stored,
// the MIDI and InitA kept, the unused group read).
bool read_grouped(Zqn255In &in, int v, bool nofairyfires, Scr &s, bool t255 = false)
{
    s.valid = in.u8();

    if(!(s.valid & VALID))
        return true;                // the auto layers it gets aren't written

    const dword has = in.u32();

    if(has & HAS_ROOMDATA)
    {
        s.guy = in.u8();

        if(v > 26)
        {
            s.guytile = in.s32();
            s.guycs = in.u8();
            s.roomflags = in.u16();
        }
        else
            guy_room_flags(s, nofairyfires);

        s.str = in.u16();
        s.room = in.u8();
        s.catchall = in.u16();
    }

    if(has & HAS_ITEM)
    {
        s.item = v < 39 ? in.u8() : in.u16();
        s.hasitem = in.u8();
        s.itemx = in.u8();
        s.itemy = in.u8();
    }

    if(has & (HAS_SWARP | HAS_TWARP))
        s.warpreturnc = in.u16();

    if(has & HAS_TWARP)
    {
        get_bytes(in, s.tilewarptype, 4);
        get_words(in, s.tilewarpdmap, 4);
        get_bytes(in, s.tilewarpscr, 4);
        s.tilewarpoverlayflags = in.u8();
    }

    if(has & HAS_SWARP)
    {
        get_bytes(in, s.sidewarptype, 4);
        get_words(in, s.sidewarpdmap, 4);
        get_bytes(in, s.sidewarpscr, 4);
        s.sidewarpoverlayflags = in.u8();
        s.sidewarpindex = in.u8();
    }

    if(has & HAS_WARPRET)
    {
        get_bytes(in, s.warpreturnx, 4);
        get_bytes(in, s.warpreturny, 4);
        s.warparrivalx = in.u8();
        s.warparrivaly = in.u8();
    }

    if(has & HAS_LAYERS)
    {
        get_bytes(in, s.layermap, 6);
        get_bytes(in, s.layerscreen, 6);
        get_bytes(in, s.layeropacity, 6);
        s.hidelayers = in.u8();
        s.hidescriptlayers = in.u8();
    }

    if(has & HAS_MAZE)
    {
        get_bytes(in, s.path, 4);
        s.exitdir = in.u8();

        if(v >= 32)
            s.maze_wipe = in.u8();
    }

    if(has & HAS_D_S_U)
    {
        s.door_combo_set = in.u16();

        for(int k = 0; k < 4; ++k)
        {
            s.door[k] = in.u8();

            if(v < 29 && s.door[k] == DOOR_NONE)
                s.door[k] = DOOR_WALL;
        }

        s.stairx = in.u8();
        s.stairy = in.u8();
        s.undercombo = in.u16();
        s.undercset = in.u8();
    }
    else if(v < 29)
        memset(s.door, DOOR_WALL, sizeof(s.door));

    if(has & HAS_FLAGS)
        get_bytes(in, s.flags, 11);

    if(has & HAS_ENEMY)
    {
        for(int k = 0; k < 10; ++k)
            s.enemy[k] = clear_bad_enemy(in.u16());

        s.pattern = in.u8();
    }

    if(has & HAS_CARRY)
    {
        if(v < 34)
        {
            s.noreset = in.u16() | (t255 ? 0 : OLD_NORESET);
            s.nocarry = in.u16() | (t255 ? 0 : OLD_NOCARRY);
        }
        else
        {
            s.noreset = in.u32();
            s.nocarry = in.u32();
            s.exstate_reset = in.u32();
            s.exstate_carry = in.u32();
        }

        s.nextmap = in.u8();
        s.nextscr = in.u8();
    }
    else if(t255)
        s.noreset = s.nocarry = 0;  // a fresh 2.55.17 mapscr's
    else if(v < 34)
    {
        s.noreset = OLD_NORESET;
        s.nocarry = OLD_NOCARRY;
    }

    if(has & HAS_SCRIPT)
    {
        if(v < 41)
        {
            s.sc.script = in.u16();
            s.preloadscript = in.u8();

            for(int q = 0; q < 8; ++q)
                s.sc.args[q] = in.s32();
        }
        else
        {
            if(!read_config(in, s.sc))
                return false;

            s.preloadscript = in.u8();
        }
    }

    if(t255 && (has & 0x1000))      // SCRHAS_UNUSED: 2.55.17 reads it (3.0 has no reader for it)
    {
        for(int q = 0; q < 10; ++q) s.npcstrings[q] = in.s32();
        get_words(in, s.new_items, 10);
        get_words(in, s.new_item_x, 10);
        get_words(in, s.new_item_y, 10);
    }

    if(has & HAS_SECRETS)
    {
        get_words(in, s.secretcombo, 128);
        get_bytes(in, s.secretcset, 128);
        get_bytes(in, s.secretflag, 128);
    }

    if(has & HAS_COMBOFLAG)
    {
        get_words(in, s.data, SQUARES);
        get_bytes(in, s.sflag, SQUARES);
        get_bytes(in, s.cset, SQUARES);
    }

    if(has & HAS_MISC)
    {
        s.color = in.u16();
        s.csensitive = in.u8();

        if(v < 38)
        {
            s.oceansfx = in.u8();
            s.bosssfx = in.u8();
            s.secretsfx = in.u8();
            s.holdupsfx = in.u8();
        }
        else
        {
            s.oceansfx = in.u16();
            s.bosssfx = in.u16();
            s.secretsfx = in.u16();
            s.holdupsfx = in.u16();
        }

        s.timedwarptics = in.u16();

        if(v < 37)
            s.screen_midi = (short)in.u16();    // 3.0: the music list has its number
        else
            s.music = in.s32();

        s.lens_layer = in.u8();

        if(v > 27)
        {
            s.lens_show = in.u8();
            s.lens_hide = in.u8();
        }
    }

    const bool masked = v < 25;
    dword bits = 0;
    int count = 32;

    if(masked)
        bits = in.u32();
    else
    {
        count = in.u16();

        if(count > FFCS)
            return false;
    }

    for(int m = 0; m < count && !in.bad; ++m)
    {
        if(masked && !((bits >> m) & 1))
            continue;

        const word data = in.u16();

        if(!masked && !data)
            continue;               // an empty FFC: nothing more of it

        Ffc &c = s.ffc[m];
        c.data = data;
        c.cset = in.u8();
        c.delay = in.u16();
        c.x = in.s32();
        c.y = in.s32();
        c.vx = in.s32();
        c.vy = in.s32();
        c.ax = in.s32();
        c.ay = in.s32();
        c.link = in.u8();

        if(v < 24)
        {
            byte b = in.u8();
            c.hit_width = (b & 0x3F) + 1;
            c.txsz = (byte)((b >> 6) + 1);
            b = in.u8();
            c.hit_height = (b & 0x3F) + 1;
            c.tysz = (byte)((b >> 6) + 1);
        }
        else
        {
            c.hit_width = in.s32();
            c.hit_height = in.s32();
            c.txsz = in.u8();
            c.tysz = in.u8();
        }

        c.flags = in.u32();

        if(v < 41)
        {
            c.sc.script = in.u16();

            for(int q = 0; q < 8; ++q)
                c.sc.args[q] = in.s32();
        }
        else if(!read_config(in, c.sc))
            return false;

        if(v < 33)
        {
            c.inita[0] = in.u8();   // InitA (3.0 drops them)
            c.inita[1] = in.u8();
        }
        else
            c.layer = in.u8();

        if(v >= 40)
        {
            c.suspend = in.s32();
            c.despawn = in.s32();
        }
    }

    if(v > 29)
    {
        const dword n = in.u32();

        if(n > in.d.size())
            return false;

        std::vector<byte> t = in.bytes(n);
        s.notes.assign(t.begin(), t.end());
    }

    if(v >= 35 && (s.flags[9] & SCREEN_GRAVITY))
    {
        s.gravity = in.s32();
        s.terminal_v = in.s32();
    }

    return true;
}

void put_config(Zqn255Out &o, const Config &c)
{
    o.w(c.script);
    o.w(8);

    for(int q = 0; q < 8; ++q)
        o.l(c.args[q]);

    o.w((int64_t)c.inst.size());

    for(size_t e = 0; e < c.inst.size(); ++e)
    {
        o.w(c.inst[e].first);
        o.l(c.inst[e].second);
    }
}

bool any_bytes(const byte *b, int n)
{
    for(int k = 0; k < n; ++k)
        if(b[k])
            return true;

    return false;
}

// writemapscreen (3.0 zq/zq_class.cpp), screen j of its map. old_ffcs: qr_OLD_FFC_FUNCTIONALITY.
void write_screen(Zqn255Out &o, const Scr &s, int j, bool old_ffcs)
{
    o.c(s.valid);

    if(!(s.valid & VALID))
        return;

    dword has = 0;

    if(s.guytile || s.guy || s.roomflags || s.str || s.room || s.catchall)
        has |= HAS_ROOMDATA;

    if(s.hasitem || (j >= 0x80 && (s.itemx || s.itemy)))
        has |= HAS_ITEM;

    bool any = (s.warpreturnc & 0x00FF) || s.tilewarpoverlayflags;

    for(int q = 0; q < 4; ++q)
        any = any || s.tilewarptype[q] || s.tilewarpdmap[q] || s.tilewarpscr[q];

    if(any)
        has |= HAS_TWARP;

    any = (s.warpreturnc & 0xFF00) || s.sidewarpindex || s.sidewarpoverlayflags;

    for(int q = 0; q < 4; ++q)
        any = any || s.sidewarptype[q] != WARP_SCROLL || s.sidewarpdmap[q] || s.sidewarpscr[q];

    if(any)
        has |= HAS_SWARP;

    if(s.warparrivalx || s.warparrivaly || any_bytes(s.warpreturnx, 4) || any_bytes(s.warpreturny, 4))
        has |= HAS_WARPRET;

    any = s.hidelayers || s.hidescriptlayers;

    for(int q = 0; q < 6; ++q)
        any = any || s.layermap[q] || s.layerscreen[q] || s.layeropacity[q] != 255;

    if(any)
        has |= HAS_LAYERS;

    if(s.exitdir || s.maze_wipe || any_bytes(s.path, 4))
        has |= HAS_MAZE;

    any = s.door_combo_set || s.stairx || s.stairy || s.undercombo || s.undercset;

    for(int q = 0; q < 4; ++q)
        any = any || s.door[q] != DOOR_NONE;

    if(any)
        has |= HAS_D_S_U;

    if(any_bytes(s.flags, 11))
        has |= HAS_FLAGS;

    any = s.pattern != 0;

    for(int q = 0; q < 10; ++q)
        any = any || s.enemy[q];

    if(any)
        has |= HAS_ENEMY;

    if(s.noreset != DEF_NORESET || s.nocarry != DEF_NOCARRY || s.nextmap || s.nextscr || s.exstate_reset ||
       s.exstate_carry)
        has |= HAS_CARRY;

    if(!s.sc.empty() || s.preloadscript)
        has |= HAS_SCRIPT;

    any = false;

    for(int q = 0; q < 128 && !any; ++q)
        any = s.secretcombo[q] || s.secretcset[q] || s.secretflag[q];

    if(any)
        has |= HAS_SECRETS;

    any = false;

    for(int q = 0; q < SQUARES && !any; ++q)
        any = s.data[q] || s.cset[q] || s.sflag[q];

    if(any)
        has |= HAS_COMBOFLAG;

    if(s.color || s.csensitive != 1 || s.oceansfx || s.bosssfx || s.secretsfx || s.holdupsfx || s.timedwarptics ||
       s.music != -1 || s.lens_layer || s.lens_show || s.lens_hide)
        has |= HAS_MISC;

    o.l(has);

    if(has & HAS_ROOMDATA)
    {
        o.c(s.guy);
        o.l(s.guytile);
        o.c(s.guycs);
        o.w(s.roomflags);
        o.w(s.str);
        o.c(s.room);
        o.w(s.catchall);
    }

    if(has & HAS_ITEM)
    {
        o.w(s.item);
        o.c(s.hasitem);
        o.c(s.itemx);
        o.c(s.itemy);
    }

    if(has & (HAS_SWARP | HAS_TWARP))
        o.w(s.warpreturnc);

    if(has & HAS_TWARP)
    {
        o.put(s.tilewarptype, 4);
        for(int k = 0; k < 4; ++k) o.w(s.tilewarpdmap[k]);
        o.put(s.tilewarpscr, 4);
        o.c(s.tilewarpoverlayflags);
    }

    if(has & HAS_SWARP)
    {
        o.put(s.sidewarptype, 4);
        for(int k = 0; k < 4; ++k) o.w(s.sidewarpdmap[k]);
        o.put(s.sidewarpscr, 4);
        o.c(s.sidewarpoverlayflags);
        o.c(s.sidewarpindex);
    }

    if(has & HAS_WARPRET)
    {
        o.put(s.warpreturnx, 4);
        o.put(s.warpreturny, 4);
        o.c(s.warparrivalx);
        o.c(s.warparrivaly);
    }

    if(has & HAS_LAYERS)
    {
        o.put(s.layermap, 6);
        o.put(s.layerscreen, 6);
        o.put(s.layeropacity, 6);
        o.c(s.hidelayers);
        o.c(s.hidescriptlayers);
    }

    if(has & HAS_MAZE)
    {
        o.put(s.path, 4);
        o.c(s.exitdir);
        o.c(s.maze_wipe);
    }

    if(has & HAS_D_S_U)
    {
        o.w(s.door_combo_set);
        o.put(s.door, 4);
        o.c(s.stairx);
        o.c(s.stairy);
        o.w(s.undercombo);
        o.c(s.undercset);
    }

    if(has & HAS_FLAGS)
        o.put(s.flags, 11);

    if(has & HAS_ENEMY)
    {
        for(int k = 0; k < 10; ++k) o.w(s.enemy[k]);
        o.c(s.pattern);
    }

    if(has & HAS_CARRY)
    {
        o.l(s.noreset);
        o.l(s.nocarry);
        o.l(s.exstate_reset);
        o.l(s.exstate_carry);
        o.c(s.nextmap);
        o.c(s.nextscr);
    }

    if(has & HAS_SCRIPT)
    {
        put_config(o, s.sc);
        o.c(s.preloadscript);
    }

    if(has & HAS_SECRETS)
    {
        for(int k = 0; k < 128; ++k) o.w(s.secretcombo[k]);
        o.put(s.secretcset, 128);
        o.put(s.secretflag, 128);
    }

    if(has & HAS_COMBOFLAG)
    {
        for(int k = 0; k < SQUARES; ++k) o.w(s.data[k]);
        o.put(s.sflag, SQUARES);
        o.put(s.cset, SQUARES);
    }

    if(has & HAS_MISC)
    {
        o.w(s.color);
        o.c(s.csensitive);
        o.w(s.oceansfx);
        o.w(s.bosssfx);
        o.w(s.secretsfx);
        o.w(s.holdupsfx);
        o.w(s.timedwarptics);
        o.l(s.music);
        o.c(s.lens_layer);
        o.c(s.lens_show);
        o.c(s.lens_hide);
    }

    // numFFC: up to the last one in use, at least 32 with the old FFC rule
    int count = 0;

    for(int m = FFCS - 1; m >= 0; --m)
        if(s.ffc[m].data)
        {
            count = m + 1;
            break;
        }

    if(count < 32 && old_ffcs)
        count = 32;

    o.w(count);

    for(int m = 0; m < count; ++m)
    {
        const Ffc &c = s.ffc[m];
        o.w(c.data);

        if(!c.data)
            continue;

        o.c(c.cset);
        o.w(c.delay);
        o.l(c.x);
        o.l(c.y);
        o.l(c.vx);
        o.l(c.vy);
        o.l(c.ax);
        o.l(c.ay);
        o.c(c.link);
        o.l(c.hit_width);
        o.l(c.hit_height);
        o.c(c.txsz);
        o.c(c.tysz);
        o.l(c.flags);
        put_config(o, c.sc);
        o.c(c.layer);
        o.l(c.suspend);
        o.l(c.despawn);
    }

    o.l((int64_t)s.notes.size());
    o.put(s.notes.data(), s.notes.size());

    if(s.flags[9] & SCREEN_GRAVITY)
    {
        o.l(s.gravity);
        o.l(s.terminal_v);
    }
}

int refuse(const char *why)
{
    zqw_last_error = why;
    return 2;
}

int write_maps(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || ctx.target != ZQ_NEWER_30 || !ctx.src_part || ctx.src_version < 18 ||
       ctx.src_version > 41)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const int v = s.version;
    std::vector<byte> rules;

    if(!zqn255_rules_loaded(ctx.target, rules) || rules.size() * 8 <= (size_t)QR_OLD_FFC_FUNCTIONALITY)
        return refuse("the quest rules as 3.0 holds them couldn't be worked out");

    const bool nofairyfires = (rules[QR_NOFAIRYGUYFIRES >> 3] >> (QR_NOFAIRYGUYFIRES & 7)) & 1;
    const bool old_ffcs = (rules[QR_OLD_FFC_FUNCTIONALITY >> 3] >> (QR_OLD_FFC_FUNCTIONALITY & 7)) & 1;
    Zqn255In in(s.data);
    const int maps = in.u16();

    if(in.bad || maps > MAPS)
        return refuse("its maps aren't laid out as expected");

    Zqn255MusicList ml;

    if(v < 37 && (!zqn255_music_list(ctx.target, ml) || ml.screen_music.size() != (size_t)maps * SCREENS))
        return refuse("the screens' music couldn't be worked out");

    // region ids: the map section's own from version 31, the DMaps' before
    std::vector<std::vector<byte> > dmap_regions;

    if(v < 31 && !zqn255_dmap_regions(ctx.target, dmap_regions))
        return refuse("the DMaps' region ids couldn't be read");

    Zqn255Out o;
    o.w(maps);
    std::vector<Scr> scr(SCREENS);

    for(int i = 0; i < maps; ++i)
    {
        bool valid = true;
        word autolayers[6] = { 0, 0, 0, 0, 0, 0 };
        word autopalette = 0;
        std::vector<byte> regions(64, 0);

        if(v > 22)
            valid = in.u8() != 0;

        if(valid)
        {
            if(v > 25)
            {
                get_words(in, autolayers, 6);

                if(v >= 36)
                    autopalette = in.u16();
            }

            if(v >= 31)
                regions = in.bytes(64);
        }

        if(v < 31 && (size_t)i < dmap_regions.size() && dmap_regions[i].size() == 64)
            regions = dmap_regions[i];

        bool any_valid = false;

        for(int j = 0; j < SCREENS; ++j)
        {
            Scr &c = scr[j];
            c.clear();

            if(valid)
            {
                if(!(v < 23 ? (read_old(in, v, nofairyfires, c), true) : read_grouped(in, v, nofairyfires, c)) || in.bad)
                    return refuse("a screen isn't laid out as expected");

                if(v < 37)
                    c.music = ml.screen_music[(size_t)i * SCREENS + j];
            }
            else
                c.valid = 0;

            any_valid = any_valid || (c.valid & VALID);
        }

        o.c(any_valid ? 1 : 0);

        if(!any_valid)
            continue;

        for(int q = 0; q < 6; ++q)
            o.w(autolayers[q]);

        o.w(autopalette);

        if(regions.size() != 64)
            return refuse("its maps aren't laid out as expected");

        o.put(regions);

        for(int j = 0; j < SCREENS; ++j)
            write_screen(o, scr[j], j, old_ffcs);
    }

    if(!in.at_end())
        return refuse("its maps aren't laid out as expected");

    std::vector<byte> none;
    return zqn255_emit(f, ID_MAPS, 41, 41, none, o.b);
}

ZqNewerRegistration g_reg_maps(ID_MAPS, write_maps, "2.55 maps as 3.0");

// ---- 2.55.17 --------------------------------------------------------------------------------

// writemapscreen (2.55.17 zq/zq_class.cpp, version 30), screen j of its map: the groups that hold
// anything, by 2.55.17's tests (a fresh screen's guy tile -1 always makes room data).
void write_screen_255(Zqn255Out &o, const Scr &s, int j, bool old_ffcs)
{
    o.c(s.valid);

    if(!(s.valid & VALID))
        return;

    dword has = 0;

    if(s.guytile || s.guy || s.roomflags || s.str || s.room || s.catchall)
        has |= HAS_ROOMDATA;

    if(s.hasitem || (j >= 0x80 && (s.itemx || s.itemy)))
        has |= HAS_ITEM;

    bool any = (s.warpreturnc & 0x00FF) || s.tilewarpoverlayflags;

    for(int q = 0; q < 4; ++q)
        any = any || s.tilewarptype[q] || s.tilewarpdmap[q] || s.tilewarpscr[q];

    if(any)
        has |= HAS_TWARP;

    any = (s.warpreturnc & 0xFF00) || s.sidewarpindex || s.sidewarpoverlayflags;

    for(int q = 0; q < 4; ++q)
        any = any || s.sidewarptype[q] != WARP_SCROLL || s.sidewarpdmap[q] || s.sidewarpscr[q];

    if(any)
        has |= HAS_SWARP;

    if(s.warparrivalx || s.warparrivaly || any_bytes(s.warpreturnx, 4) || any_bytes(s.warpreturny, 4))
        has |= HAS_WARPRET;

    any = s.hidelayers || s.hidescriptlayers;

    for(int q = 0; q < 6; ++q)
        any = any || s.layermap[q] || s.layerscreen[q] || s.layeropacity[q] != 255;

    if(any)
        has |= HAS_LAYERS;

    if(s.exitdir || any_bytes(s.path, 4))
        has |= HAS_MAZE;

    any = s.door_combo_set || s.stairx || s.stairy || s.undercombo || s.undercset;

    for(int q = 0; q < 4; ++q)
        any = any || s.door[q] != DOOR_NONE;

    if(any)
        has |= HAS_D_S_U;

    if(any_bytes(s.flags, 11))
        has |= HAS_FLAGS;

    any = s.pattern != 0;

    for(int q = 0; q < 10; ++q)
        any = any || s.enemy[q];

    if(any)
        has |= HAS_ENEMY;

    if(s.noreset || s.nocarry || s.nextmap || s.nextscr)
        has |= HAS_CARRY;

    if(s.sc.script || s.preloadscript)
        has |= HAS_SCRIPT;

    for(int q = 0; q < 8; ++q)
        if(s.sc.args[q])
            has |= HAS_SCRIPT;

    const dword HAS_UNUSED = 0x1000;

    for(int q = 0; q < 10; ++q)
        if(s.npcstrings[q] || s.new_items[q] || s.new_item_x[q] || s.new_item_y[q])
            has |= HAS_UNUSED;

    any = false;

    for(int q = 0; q < 128 && !any; ++q)
        any = s.secretcombo[q] || s.secretcset[q] || s.secretflag[q];

    if(any)
        has |= HAS_SECRETS;

    any = false;

    for(int q = 0; q < SQUARES && !any; ++q)
        any = s.data[q] || s.cset[q] || s.sflag[q];

    if(any)
        has |= HAS_COMBOFLAG;

    if(s.color || s.csensitive != 1 || s.oceansfx || s.bosssfx || s.secretsfx || s.holdupsfx || s.timedwarptics ||
       s.screen_midi != -1 || s.lens_layer || s.lens_show || s.lens_hide)
        has |= HAS_MISC;

    o.l(has);

    if(has & HAS_ROOMDATA)
    {
        o.c(s.guy);
        o.l(s.guytile);
        o.c(s.guycs);
        o.w(s.roomflags);
        o.w(s.str);
        o.c(s.room);
        o.w(s.catchall);
    }

    if(has & HAS_ITEM)
    {
        o.c(s.item);
        o.c(s.hasitem);
        o.c(s.itemx);
        o.c(s.itemy);
    }

    if(has & (HAS_SWARP | HAS_TWARP))
        o.w(s.warpreturnc);

    if(has & HAS_TWARP)
    {
        o.put(s.tilewarptype, 4);
        for(int k = 0; k < 4; ++k) o.w(s.tilewarpdmap[k]);
        o.put(s.tilewarpscr, 4);
        o.c(s.tilewarpoverlayflags);
    }

    if(has & HAS_SWARP)
    {
        o.put(s.sidewarptype, 4);
        for(int k = 0; k < 4; ++k) o.w(s.sidewarpdmap[k]);
        o.put(s.sidewarpscr, 4);
        o.c(s.sidewarpoverlayflags);
        o.c(s.sidewarpindex);
    }

    if(has & HAS_WARPRET)
    {
        o.put(s.warpreturnx, 4);
        o.put(s.warpreturny, 4);
        o.c(s.warparrivalx);
        o.c(s.warparrivaly);
    }

    if(has & HAS_LAYERS)
    {
        o.put(s.layermap, 6);
        o.put(s.layerscreen, 6);
        o.put(s.layeropacity, 6);
        o.c(s.hidelayers);
        o.c(s.hidescriptlayers);
    }

    if(has & HAS_MAZE)
    {
        o.put(s.path, 4);
        o.c(s.exitdir);
    }

    if(has & HAS_D_S_U)
    {
        o.w(s.door_combo_set);
        o.put(s.door, 4);
        o.c(s.stairx);
        o.c(s.stairy);
        o.w(s.undercombo);
        o.c(s.undercset);
    }

    if(has & HAS_FLAGS)
        o.put(s.flags, 11);

    if(has & HAS_ENEMY)
    {
        for(int k = 0; k < 10; ++k) o.w(s.enemy[k]);
        o.c(s.pattern);
    }

    if(has & HAS_CARRY)
    {
        o.w(s.noreset);
        o.w(s.nocarry);
        o.c(s.nextmap);
        o.c(s.nextscr);
    }

    if(has & HAS_SCRIPT)
    {
        o.w(s.sc.script);
        o.c(s.preloadscript);

        for(int q = 0; q < 8; ++q)
            o.l(s.sc.args[q]);
    }

    if(has & HAS_UNUSED)
    {
        for(int q = 0; q < 10; ++q) o.l(s.npcstrings[q]);
        for(int q = 0; q < 10; ++q) o.w(s.new_items[q]);
        for(int q = 0; q < 10; ++q) o.w(s.new_item_x[q]);
        for(int q = 0; q < 10; ++q) o.w(s.new_item_y[q]);
    }

    if(has & HAS_SECRETS)
    {
        for(int k = 0; k < 128; ++k) o.w(s.secretcombo[k]);
        o.put(s.secretcset, 128);
        o.put(s.secretflag, 128);
    }

    if(has & HAS_COMBOFLAG)
    {
        for(int k = 0; k < SQUARES; ++k) o.w(s.data[k]);
        o.put(s.sflag, SQUARES);
        o.put(s.cset, SQUARES);
    }

    if(has & HAS_MISC)
    {
        o.w(s.color);
        o.c(s.csensitive);
        o.c(s.oceansfx);
        o.c(s.bosssfx);
        o.c(s.secretsfx);
        o.c(s.holdupsfx);
        o.w(s.timedwarptics);
        o.w(s.screen_midi);
        o.c(s.lens_layer);
        o.c(s.lens_show);
        o.c(s.lens_hide);
    }

    // numFFC: up to the last one in use, at least 32 with the old FFC rule
    int count = 0;

    for(int m = FFCS - 1; m >= 0; --m)
        if(s.ffc[m].data)
        {
            count = m + 1;
            break;
        }

    if(count < 32 && old_ffcs)
        count = 32;

    o.w(count);

    for(int m = 0; m < count; ++m)
    {
        const Ffc &c = s.ffc[m];
        o.w(c.data);

        if(!c.data)
            continue;

        o.c(c.cset);
        o.w(c.delay);
        o.l(c.x);
        o.l(c.y);
        o.l(c.vx);
        o.l(c.vy);
        o.l(c.ax);
        o.l(c.ay);
        o.c(c.link);
        o.l(c.hit_width);
        o.l(c.hit_height);
        o.c(c.txsz);
        o.c(c.tysz);
        o.l(c.flags);
        o.w(c.sc.script);

        for(int q = 0; q < 8; ++q)
            o.l(c.sc.args[q]);

        o.c(c.inita[0]);
        o.c(c.inita[1]);
    }

    o.l((int64_t)s.notes.size());
    o.put(s.notes.data(), s.notes.size());
}

// readmaps / writemaps of 2.55.17, for map versions 18-30: per map a valid byte (whether any of
// its screens is), its auto layer combos (as stored from version 26, else none), its 136 screens.
int write_maps_255(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || ctx.target != ZQ_NEWER_255 || !ctx.src_part || ctx.src_version < 18 ||
       ctx.src_version > 30)
        return ZQW_UNSUPPORTED;

    Zqn255Section s;

    if(!zqn255_section(ctx, s))
        return 2;

    const int v = s.version;
    std::vector<byte> rules;

    if(!zqn255_rules_loaded(ctx.target, rules) || rules.size() * 8 <= (size_t)QR_OLD_FFC_FUNCTIONALITY)
        return refuse("the quest rules as 2.55.17 holds them couldn't be worked out");

    const bool nofairyfires = (rules[QR_NOFAIRYGUYFIRES >> 3] >> (QR_NOFAIRYGUYFIRES & 7)) & 1;
    const bool old_ffcs = (rules[QR_OLD_FFC_FUNCTIONALITY >> 3] >> (QR_OLD_FFC_FUNCTIONALITY & 7)) & 1;
    Zqn255In in(s.data);
    const int maps = in.u16();

    if(in.bad || maps > MAPS)
        return refuse("its maps aren't laid out as expected");

    Zqn255Out o;
    o.w(maps);
    std::vector<Scr> scr(SCREENS);

    for(int i = 0; i < maps; ++i)
    {
        bool valid = true;
        word autolayers[6] = { 0, 0, 0, 0, 0, 0 };

        if(v > 22)
            valid = in.u8() != 0;

        if(valid && v > 25)
            get_words(in, autolayers, 6);

        bool any_valid = false;

        for(int j = 0; j < SCREENS; ++j)
        {
            Scr &c = scr[j];
            c.clear();
            c.noreset = c.nocarry = 0;  // a fresh 2.55.17 mapscr's

            if(valid)
            {
                if(!(v < 23 ? (read_old(in, v, nofairyfires, c, true), true) : read_grouped(in, v, nofairyfires, c, true)) || in.bad)
                    return refuse("a screen isn't laid out as expected");
            }
            else
                c.valid = 0;

            any_valid = any_valid || (c.valid & VALID);
        }

        o.c(any_valid ? 1 : 0);

        if(!any_valid)
            continue;

        for(int q = 0; q < 6; ++q)
            o.w(autolayers[q]);

        for(int j = 0; j < SCREENS; ++j)
            write_screen_255(o, scr[j], j, old_ffcs);
    }

    if(!in.at_end())
        return refuse("its maps aren't laid out as expected");

    std::vector<byte> none;
    return zqn255_emit(f, ID_MAPS, 30, 9, none, o.b);
}

ZqNewerRegistration g_reg_maps_255(ID_MAPS, write_maps_255, "2.55 maps as 2.55.17");

} // namespace

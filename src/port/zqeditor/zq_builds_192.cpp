// zq_builds_192.cpp: File > New for 1.92 (Beta 114) and 1.90 (zq_builds.h). New only: Save As
// for these builds is zq_builds_old.cpp's.
//
// 1.92's ZQuest made a new quest in code (zq_class.cc init_quest, H:\zc-history origin/1.92b114)
// and saved it as one packed, encoded body of its structures in a fixed order
// (save_unencoded_quest): header, strings, DMaps, misc data, items, weapons, the maps, the combos,
// the palettes and their names, the tiles (only when the quest has its own), the MIDIs, the
// cheats and the init data. This writes that body for a new quest in 1.92 Beta 114's own layouts
// (its zdefs.h, as the DJGPP build laid them out), which are also what our 1.92 reader (src/qst.cpp,
// the branches for zelda_version 0x192 and build 114) reads:
//   header      init_quest: 1.92's id string, version 0x192, internal 0xA721, build 114, one map,
//               one string, every rule off, no password; the cheats flag set as the save sets it.
//   strings     init_msgstrs: "(none)" and nothing else (255 records of 76 bytes).
//   DMaps       reset_dmap: all zero, the title and intro blank (256 records of 131 bytes).
//   misc        zero, but for the palette cycles (init_colordata) and the triforce positions, the
//               subscreen icons and the misc colours (reset_mapstyles).
//   items, weapons   reset_items and reset_wpns.
//   map         one map of cleared screens (zmap::clearscr), the four templates from the defaults.
//   combos      init_combos.
//   palettes    init_colordata: the 3373 csets 1.92 has, then the 256 level palette names.
//   tiles       none: a new quest uses the default tiles until its own are edited (usetiles).
//   MIDIs       none. cheats: none. init data: init_quest's (3 hearts, the shield, 8 bombs, the
//               sword hearts 0/5/12/21).
//
// Where the defaults come from: 1.92 took its combos, tiles, palettes, items, weapons, map styles
// and templates from object DAT_NESZGP of its zgp.dat (zquest.cc init_tiles, init_combos,
// init_colordata; zq_class.cc reset_items, reset_wpns, reset_mapstyles, reset_templates), a file
// that isn't in zc-history's 1.92 tree (its zquest.dat holds only the editor's own bitmaps and
// fonts). The oldest copy of that data kept is 2.10's (zgp.dat beside 2.10.1, where DAT_NESZGP
// had become a whole quest file: the NES set 1.92's grew into), which zq_builds_old.cpp already
// ships as templates/2.10.0.qst. So that file is opened, and its combos, palettes and their
// names, items, weapons, misc colours, cycles, triforce positions, icons and template screens
// are written in 1.92's layouts, undoing what our loader does to 2.10 data on the way in (the
// swapped Goriya tiles, the "touchy" slash combos, the moved sprite palettes), so the 1.92 file
// holds what the 2.10 file holds.
//
// The file is packed with ZC's data password and encoded with the 1.92 b105 method, as every
// 1.92 quest in the user's pack is (builds 152 to 183), then opened as the new quest, untitled.
//
// 1.90: no 1.90 source is kept (zc-history has 1.00, 1.50 and 1.92 b114), but the user's pack
// holds 41 1.90 quests (header 0x190, build 0). Their bodies are exactly what our loader's
// branches for 0x190 read, which is 1.92 Beta 114's order with 1.90's older layouts: a 272-byte
// header (no foo2), 128 strings, 32 DMaps of 16 bytes (no names, titles or intros), 680 bytes of
// misc data (16 levels' cycles, 3 icons, no ponds), 440-byte screens, always 2048 combos of 8
// bytes, 240 csets and no palette names, then the cheats and no init data. A new 1.90 quest is
// the 1.92 one in those layouts. Like those quests, the file is encoded with the 1.92 b104 method
// and packed in the older password scheme (write_old_packed).
//
// -qst-new-build <1.92|1.90> <out.qst>: makes the new quest for that build, writes it to out.qst
// and opens it again (exit 0 when that worked); for testing without the File > New menu.
//
// Written for the port (no Allegro or jwin code).

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "qst.h"
#include "gui.h"
#include "jwin.h"
#include "zquest.h"
#include "zq_class.h"
#include "zq_files.h"
#include "zq_misc.h"
#include "zq_page.h"
#include "colors.h"
#include "tiles.h"
#include "zq_builds.h"

extern zquestheader header;
extern zinitdata zinit;
extern bool zq_batch_load;
extern std::vector<mapscr> TheMaps;
extern miscQdata misc;
extern char palnames[MAXLEVELS][17];
extern byte *colordata;
extern itemdata *itemsbuf;
extern wpndata *wpnsbuf;

namespace
{

const char *const TEMPLATE_192 = "templates/2.10.0.qst";

enum
{
    B_MSGS = 255, B_DMAPS = 256, B_ITEMS = 256, B_WPNS = 256, B_MAPSCRS = 132, B_LEVELS = 256,
    B_PALNAMESIZE = 17, B_CSETS = 3373,    // newpdTOTAL: 1.92's 15 + 256 levels * 13 + 30 sprite csets
    B_HEADER = 322, B_MSG = 76, B_DMAP = 131, B_MISC = 3440, B_SCREEN = 832, B_COMBO = 24,
    B_CHEATS = 168, B_INIT = 256
};

std::string g_error;

struct Out
{
    std::vector<byte> b;
    void u8(int v) { b.push_back((byte)v); }
    void u16(int v) { u8(v & 0xFF); u8((v >> 8) & 0xFF); }
    void zeros(size_t n) { b.insert(b.end(), n, 0); }
    void str(const char *s, size_t n)       // a fixed-size char field
    {
        size_t l = strlen(s);

        for(size_t i = 0; i < n; ++i)
            u8(i < l ? s[i] : 0);
    }
};

// What 1.92's init_quest took from the NES graphics pack, as the 2.10.0 file holds it.
struct Defaults
{
    std::vector<newcombo> combos;
    word combo_count;
    std::vector<byte> csets;            // B_CSETS * 48
    std::vector<std::string> palnames;  // B_LEVELS
    palcycle cycles[256][3];
    byte triforce[8];
    zcolors colors;
    int icons[4];
    std::vector<itemdata> items;
    std::vector<wpndata> wpns;
    std::vector<mapscr> templates;      // screens 128 to 131 of map 0
    bool new_enemy_tiles;
};

// Our loader swaps the Goriya tiles of combos in quests older than 2.11 Beta 7 unless the quest
// uses the new enemy tiles; this gives the tile the file has.
int raw_combo_tile(int t, bool new_enemy_tiles)
{
    if(new_enemy_tiles)
        return t;

    switch(t)
    {
    case 130: return 132;
    case 131: return 133;
    case 132: return 130;
    case 133: return 131;
    }

    return t;
}

// The "touchy" slash combos are what our loader makes of the old types (combo section version < 7).
int raw_combo_type(int t)
{
    switch(t)
    {
    case cSLASHTOUCHY: return cSLASH;
    case cSLASHITEMTOUCHY: return cSLASHITEM;
    case cBUSHTOUCHY: return cBUSH;
    case cFLOWERSTOUCHY: return cFLOWERS;
    case cTALLGRASSTOUCHY: return cTALLGRASS;
    case cSLASHNEXTTOUCHY: return cSLASHNEXT;
    case cSLASHNEXTITEMTOUCHY: return cSLASHNEXTITEM;
    case cBUSHNEXTTOUCHY: return cBUSHNEXT;
    }

    return t;
}

bool combo_used(const newcombo &c)
{
    return c.tile || c.flip || c.walk || c.type || c.csets || c.frames || c.speed;
}

bool take_defaults(Defaults &d)
{
    FILE *t = fopen(TEMPLATE_192, "rb");

    if(!t)
    {
        g_error = std::string("the file with the default data, ") + TEMPLATE_192 + ", is missing";
        return false;
    }

    fclose(t);
    char p[2048];
    strncpy(p, TEMPLATE_192, sizeof(p) - 1);
    p[sizeof(p) - 1] = 0;
    int ret = load_quest(p, true, true);

    if(ret != qe_OK)
    {
        g_error = std::string("the file with the default data couldn't be opened: ") + qst_error[ret];
        return false;
    }

    d.new_enemy_tiles = get_bit(quest_rules, qr_NEWENEMYTILES) != 0;
    int last = -1;

    for(int i = 0; i < MAXCOMBOS && i < 16384; ++i)    // 1.92 has 16384 combos
        if(combo_used(combobuf[i]))
            last = i;

    d.combo_count = (word)(last + 1);
    d.combos.assign(combobuf, combobuf + d.combo_count);

    d.csets.assign(colordata, colordata + newpoSPRITE * 48);   // full, level palettes
    d.csets.insert(d.csets.end(), colordata + newerpoSPRITE * 48, colordata + (newerpoSPRITE + 30) * 48);

    for(int i = 0; i < B_LEVELS; ++i)
        d.palnames.push_back(std::string(palnames[i], strnlen(palnames[i], B_PALNAMESIZE)));

    memcpy(d.cycles, misc.cycles, sizeof(d.cycles));
    memcpy(d.triforce, misc.triforce, sizeof(d.triforce));
    d.colors = misc.colors;

    for(int i = 0; i < 4; ++i)
        d.icons[i] = misc.icons[i];

    d.items.assign(itemsbuf, itemsbuf + B_ITEMS);
    d.wpns.assign(wpnsbuf, wpnsbuf + B_WPNS);

    for(int i = 128; i < 132; ++i)
        d.templates.push_back(TheMaps[i]);

    return true;
}

// 1.90 (old) or 1.92 Beta 114: the header's id string and internal version are the same.
void put_header(Out &o, bool old)
{
    o.str("AG Zelda Classic Quest File\n ", 31);    // QH_IDSTR
    o.u8(0);                                        // padding
    o.u16(old ? 0x190 : 0x192);                     // zelda_version
    o.u16(0xA721);                                  // internal (INTERNAL_VERSION)
    o.u8(0);                                        // quest_number
    o.zeros(2);                                     // rules
    o.u8(1);                                        // map_count
    o.u8(1);                                        // str_count
    byte flags[20] = { 0 };
    flags[5] = 1;                                   // ZQ_CHEATS: the save always sets it
    for(int i = 0; i < 20; ++i)
        o.u8(flags[i]);                             // data_flags: no tiles (ZQ_TILES), no MIDIs
    o.zeros(2);                                     // rules2
    o.u8(0);                                        // options
    o.zeros(9);                                     // version
    o.zeros(65);                                    // title
    o.zeros(65);                                    // author
    o.u8(0);                                        // padding
    o.u16(0);                                       // pwdkey: no password
    o.zeros(30);                                    // password
    o.zeros(9);                                     // minver
    o.u8(old ? 0 : 114);                            // build
    o.zeros(10);                                    // foo
    o.zeros(16);                                    // rules3 to rules10

    if(!old)
        o.zeros(50);                                // foo2 (1.92)
}

void put_strings(Out &o, bool old)
{
    for(int i = 0; i < (old ? 128 : B_MSGS); ++i)
    {
        o.str(i == 0 ? "(none)" : "", 73);
        o.zeros(3);                                 // d1 d2 d3
    }
}

void put_dmaps(Out &o, bool old)
{
    for(int i = 0; i < (old ? 32 : B_DMAPS); ++i)
    {
        o.zeros(16);                                // map level xoff compass color midi cont type grid[8]

        if(old)
            continue;                               // 1.90's DMaps end there

        o.zeros(21);                                // name
        o.str("                    ", 21);          // title
        o.str("                                                                        ", 73);   // intro
    }
}

void put_misc(Out &o, const Defaults &d, bool old)
{
    o.zeros(16 * 10);                               // shops
    o.zeros(16 * 10);                               // infos
    o.zeros(8 * 18);                                // warp rings

    for(int i = 0; i < (old ? 16 : 256); ++i)      // 1.90: 16 levels' cycles
        for(int j = 0; j < 3; ++j)
        {
            o.u8(d.cycles[i][j].first);
            o.u8(d.cycles[i][j].count);
            o.u8(d.cycles[i][j].speed);
        }

    o.zeros(9 * 2);                                 // whirlwinds

    for(int i = 0; i < 8; ++i)
        o.u8(d.triforce[i]);

    const zcolors &c = d.colors;
    o.u8(c.text); o.u8(c.caption); o.u8(c.overw_bg); o.u8(c.dngn_bg); o.u8(c.dngn_fg); o.u8(c.cave_fg);
    o.u8(c.bs_dk); o.u8(c.bs_goal); o.u8(c.compass_lt); o.u8(c.compass_dk);
    o.u8(c.subscr_bg); o.u8(c.triframe_color); o.u8(c.link_dot); o.u8(c.bmap_bg); o.u8(c.bmap_fg);
    o.u8(c.triforce_cset); o.u8(c.triframe_cset); o.u8(c.overworld_map_cset); o.u8(c.dungeon_map_cset);
    o.u8(c.blueframe_cset);
    o.u16(c.triforce_tile); o.u16(c.triframe_tile); o.u16(c.overworld_map_tile); o.u16(c.dungeon_map_tile);
    o.u16(c.blueframe_tile); o.u16(c.HCpieces_tile);
    o.u8(c.HCpieces_cset);
    o.zeros(7);                                     // foo

    for(int i = 0; i < (old ? 3 : 4); ++i)
        o.u16(d.icons[i]);

    if(old)
        return;                                     // 1.90's misc data ends there

    o.zeros(16 * 25);                               // ponds
    o.u8(0);                                        // endstring
    o.u8(0);                                        // dummy
    o.zeros(98 * 2);                                // expansion
}

void put_items(Out &o, const Defaults &d)
{
    for(int i = 0; i < B_ITEMS; ++i)
    {
        const itemdata &it = d.items[i];
        o.u16(it.tile);
        o.u8(it.misc);
        o.u8(it.csets);
        o.u8(it.frames);
        o.u8(it.speed);
        o.u8(it.delay);
        o.u8(0);                                    // exp
    }

    for(int i = 0; i < B_WPNS; ++i)
    {
        const wpndata &w = d.wpns[i];
        o.u16(w.tile);
        o.u8(w.misc);
        o.u8(w.csets);
        o.u8(w.frames);
        o.u8(w.speed);
        o.u8(w.type);
        o.u8(0);                                    // exp
    }
}

// zmap::clearscr; a template screen keeps its combos, csets, flags and colour. 1.92 Beta 114's
// screen is 832 bytes; 1.90's is the 440-byte one (one-byte enemies, 15 extra bytes, the
// combo's flag in the top bits of its word, no layers).
void put_screen(Out &o, const mapscr *t, bool old)
{
    int cpage = 0;

    if(t && !t->data.empty())
        cpage = (t->data[0] >> 8) & 0xFF;

    o.u8(t ? (t->valid | mVERSION) : mVERSION);    // valid
    o.zeros(13);                                    // guy str room item _FOO1_ warptype _FOO2_ warpx warpy stairx stairy itemx itemy
    o.u8(t ? (t->color & 0xFF) : 0);                // color
    o.zeros(1 + 4 + 4);                             // enemyflags door[4] warpdmap warpscr exitdir _FOO3_
    o.zeros(old ? 10 : 20);                         // enemy[10]
    o.zeros(8 + 2);                                 // pattern warptype2 warpx2 warpy2 path[4] warpscr2 warpdmap2
    o.u16(t ? ((t->undercombo & 0xFF) | ((t->undercset & 7) << 8)) : 0);   // under
    o.u8(cpage);                                    // cpage
    o.u8(0);                                        // _FOO4_
    o.u16(0);                                       // catchall
    o.zeros(3);                                     // flags flags2 flags3

    if(!old)
        o.zeros(6 + 6);                             // layermap layerscreen

    o.zeros(old ? 15 : 32);                         // extra

    for(int i = 0; i < 4; ++i)
        o.u8(i);                                    // secret

    for(int i = 0; i < 16; ++i)
        o.u8(((i >> 2) << 4) + (i & 3) + 12);

    if(!old)
        o.u8(0);                                    // padding

    for(int k = 0; k < 176; ++k)
    {
        int c = 0, cs = 0, fl = 0;

        if(t && k < (int)t->data.size())
        {
            c = t->data[k];
            cs = k < (int)t->cset.size() ? t->cset[k] : 0;
            fl = k < (int)t->sflag.size() ? t->sflag[k] : 0;
        }

        o.u16((c & 0xFF) | ((cs & 7) << 8) | (old ? (fl & 31) << 11 : 0));
    }

    if(old)
        return;

    for(int k = 0; k < 176; ++k)
        o.u8(t && k < (int)t->sflag.size() ? t->sflag[k] : 0);

    o.zeros(176);                                   // sflag2
}

void put_map(Out &o, const Defaults &d, bool old)
{
    for(int s = 0; s < B_MAPSCRS; ++s)
        put_screen(o, s >= 128 && s - 128 < (int)d.templates.size() ? &d.templates[s - 128] : NULL, old);
}

// 1.92: a count, then 24-byte combos. 1.90: always 2048 combos of 8 bytes.
void put_combos(Out &o, const Defaults &d, bool old)
{
    const int n = old ? 2048 : d.combo_count;

    if(!old)
        o.u16(n);

    for(int i = 0; i < n; ++i)
    {
        newcombo c;
        memset(&c, 0, sizeof(c));

        if(i < d.combo_count)
            c = d.combos[i];

        o.u16(raw_combo_tile(c.tile, d.new_enemy_tiles));
        o.u8(c.flip);
        o.u8(c.walk);
        o.u8(raw_combo_type(c.type));
        o.u8(c.csets);
        o.u16(0);                                   // foo

        if(old)
            continue;

        o.u8(c.frames);
        o.u8(c.speed);
        o.u16(0);                                   // drawtile (set as the combos animate)
        o.zeros(12);                                // foo2
    }
}

// 1.92: 3373 csets (15 + 256 levels * 13 + 30 sprite csets) and 256 level names. 1.90: 240 csets
// (15 + 15 levels * 13 + 30 sprite csets), no names. 1.92 Beta 73 put a new sprite cset at 8
// (our loader moves 1.90's 8 to 10 up one, dropping 11), so that goes back.
void put_palettes(Out &o, const Defaults &d, bool old)
{
    if(!old)
    {
        o.b.insert(o.b.end(), d.csets.begin(), d.csets.end());

        for(int i = 0; i < B_LEVELS; ++i)
            o.str(d.palnames[i].c_str(), B_PALNAMESIZE);

        return;
    }

    o.b.insert(o.b.end(), d.csets.begin(), d.csets.begin() + 210 * 48);
    const byte *spr = &d.csets[newpoSPRITE * 48];

    for(int k = 0; k < 30; ++k)
    {
        const int from = (k >= 8 && k < 11) ? k + 1 : k;
        o.b.insert(o.b.end(), spr + from * 48, spr + from * 48 + 48);
    }
}

void put_init(Out &o, bool old)
{
    o.zeros(B_CHEATS);                              // ZCHEATS: none

    if(old)
        return;                                     // 1.90 has no init data

    byte z[B_INIT] = { 0 };
    z[2] = 1;                                       // shield
    z[16] = 3;                                      // hc
    z[17] = 3;                                      // start_heart
    z[18] = 3;                                      // cont_heart
    z[20] = 8;                                      // max_bombs
    z[105] = 0;                                     // swordhearts
    z[106] = 5;
    z[107] = 12;
    z[108] = 21;
    o.b.insert(o.b.end(), z, z + B_INIT);
}

bool build_body(std::vector<byte> &body, bool old)
{
    Defaults d;

    if(!take_defaults(d))
        return false;

    Out o;
    put_header(o, old);
    put_strings(o, old);
    put_dmaps(o, old);
    put_misc(o, d, old);
    put_items(o, d);
    put_map(o, d, old);
    put_combos(o, d, old);
    put_palettes(o, d, old);
    put_init(o, old);

    // 1.90's sizes are those of the pack's 1.90 quests (and our loader's branches for 0x190)
    const size_t want = old ? 272 + 128 * 76 + 32 * 16 + 680 + (B_ITEMS + B_WPNS) * 8 + B_MAPSCRS * 440 +
                              2048 * 8 + 240 * 48 + B_CHEATS :
                        B_HEADER + B_MSGS * B_MSG + B_DMAPS * B_DMAP + B_MISC + (B_ITEMS + B_WPNS) * 8 +
                        B_MAPSCRS * B_SCREEN + 2 + d.combo_count * B_COMBO + B_CSETS * 48 +
                        B_LEVELS * B_PALNAMESIZE + B_CHEATS + B_INIT;

    if(o.b.size() != want)
    {
        g_error = "the quest's layout came out the wrong size";
        al_trace("zq_builds_192: body %d bytes, expected %d\n", (int)o.b.size(), (int)want);
        return false;
    }

    body.swap(o.b);
    return true;
}

std::string temp_path(const char *ext)
{
    char tmp[64];
    temp_name(tmp);
    return std::string(tmp) + ext;
}

// 1.90's packfiles use the older password scheme (every 1.90 quest in the pack does): the magic is
// F_PACK_MAGIC with the password mask without its final 42 (the file's first four bytes XORed with
// the password, as a reader sees them), and only the LZSS flag bytes carry the password. The data
// goes in as literals (eight to a flag byte), which every LZSS reader takes.
bool write_old_packed(const std::vector<byte> &body, const char *path)
{
    const char *pw = datapwd;
    const size_t pl = strlen(pw);
    unsigned m = 0;

    for(size_t i = 0; i < pl; ++i)
        m ^= (unsigned)(int)(signed char)pw[i] << ((i & 3) * 8);

    for(size_t k = 0, pos = 0; k < 4; ++k)
    {
        m ^= (unsigned)(int)(signed char)pw[pos] << (24 - k * 8);

        if(++pos >= pl)
            pos = 0;
    }

    const unsigned magic = 0x736C6821u ^ m;        // F_PACK_MAGIC
    std::vector<byte> out;
    out.reserve(body.size() + body.size() / 8 + 8);

    for(int k = 0; k < 4; ++k)
        out.push_back((byte)(((magic >> (24 - k * 8)) & 0xFF) ^ (byte)pw[k % pl]));

    for(size_t i = 0, g = 0; i < body.size(); i += 8, ++g)
    {
        out.push_back((byte)(0xFF ^ (byte)pw[g % pl]));

        for(size_t j = i; j < i + 8 && j < body.size(); ++j)
            out.push_back(body[j]);
    }

    FILE *f = fopen(path, "wb");

    if(!f)
        return false;

    bool ok = fwrite(&out[0], 1, out.size(), f) == out.size();
    return (fclose(f) == 0) && ok;
}

// Packed with the data password, then encoded as the build encoded its quests (1.90: the 1.92 b104
// method and the old packfile scheme; 1.92 Beta 114: the b105 method, as the pack's 1.92 quests).
bool write_file(const std::vector<byte> &body, const char *path, bool old)
{
    const std::string packed = temp_path(".tmp");
    bool ok;

    if(old)
        ok = write_old_packed(body, packed.c_str());
    else
    {
        PACKFILE *f = pack_fopen_password(packed.c_str(), F_WRITE_PACKED, datapwd);
        ok = f != NULL;

        if(f)
        {
            ok = pack_fwrite((void *)&body[0], (long)body.size(), f) == (long)body.size();
            pack_fclose(f);
        }
    }

    if(!ok)
    {
        remove(packed.c_str());
        g_error = "a temporary file couldn't be written";
        return false;
    }

    ok = encode_file_007(packed.c_str(), path, ((0xA721 + rand()) & 0xFFFF) + 0x413F0000, ENC_STR,
                         old ? ENC_METHOD_192B104 : ENC_METHOD_192B105) == 0;
    remove(packed.c_str());

    if(!ok)
        g_error = std::string("the quest file couldn't be written to ") + path;

    return ok;
}

void after_open()
{
    setup_combo_animations();
    setup_combo_animations2();
    Map.setCanPaste(false);
    Map.setCanUndo(false);
    rebuild_trans_table();
    rebuild_string_list();
    onDrawingModeNormal();
    zinit.last_map = 0;
    zinit.last_screen = 0;
    Map.setCurrMap(0);
    Map.setCurrScr(0);
}

// Writes the new 1.92 quest to path and opens it.
bool new_19x_to(const char *path, bool old)
{
    g_error.clear();
    std::vector<byte> body;

    if(!build_body(body, old) || !write_file(body, path, old))
        return false;

    char p[2048];
    strncpy(p, path, sizeof(p) - 1);
    p[sizeof(p) - 1] = 0;
    int ret = load_quest(p, true, true);

    if(ret != qe_OK)
    {
        g_error = std::string("the new quest couldn't be opened: ") + qst_error[ret];
        return false;
    }

    after_open();
    return true;
}

bool make_new_19x(const zq_build &b)
{
    const std::string tmp = temp_path(".qst");
    bool ok = new_19x_to(tmp.c_str(), b.gen == 0x190);
    remove(tmp.c_str());

    if(!ok)
    {
        al_trace("zq_builds_192: %s\n", g_error.c_str());

        if(!zq_batch_load)
            jwin_alert("ZQuest", ("The new quest couldn't be made for " + b.name + ":").c_str(), g_error.c_str(),
                       "It is a 2.53.1 quest.", "O&K", NULL, 'k', 0, lfont);

        NewQuestFile(0);
        return false;
    }

    memset(filepath, 0, 255);
    first_save = false;
    saved = true;
    char title[256];
    snprintf(title, sizeof(title), ZQ_WINDOW_NAME " - Untitled Quest (ZC %s)", b.name.c_str());
    set_window_title(title);
    refresh(rALL);
    return true;
}

} // namespace

void zq_builds_192(std::vector<zq_build> &out)
{
    zq_build b;
    b.name = "1.92 (Beta 114)";
    b.order = 20010501 + 114;       // as zq_builds_old.cpp's "1.92 (Beta 114)"
    b.gen = 0x192;
    b.tag = 114;
    b.save_as = NULL;
    b.make_new = make_new_19x;
    out.push_back(b);

    b.name = "1.90";
    b.order = 20010403;             // as zq_builds_old.cpp's "1.90"
    b.gen = 0x190;
    b.tag = 0;
    out.push_back(b);
}

// -qst-new-build <name> <out.qst>: -1 when the switch isn't there, else the exit code.
int zq_builds_192_switches(int argc, char **argv)
{
    int a = used_switch(argc, argv, "-qst-new-build");

    if(!a || argc <= a + 2)
        return -1;

    zq_batch_load = true;
    std::vector<zq_build> builds;
    zq_builds_192(builds);

    for(size_t i = 0; i < builds.size(); ++i)
    {
        if(builds[i].name != argv[a + 1])
            continue;

        bool ok = new_19x_to(argv[a + 2], builds[i].gen == 0x190);
        al_trace("-qst-new-build %s: %s%s; header 0x%x build %d, %d maps\n", argv[a + 1], ok ? "ok" : "failed: ",
                 ok ? "" : g_error.c_str(), header.zelda_version, header.build, (int)map_count);
        return ok ? 0 : 3;
    }

    al_trace("-qst-new-build %s: no such build here\n", argv[a + 1]);
    return 2;
}

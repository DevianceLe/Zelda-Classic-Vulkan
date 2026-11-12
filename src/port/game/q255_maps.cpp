// The 2.55 and 3.0 MAP (versions 18-41) and DMAP (versions 9-28) sections. See q255.h.
//
// Read and dropped, 2.53 has no home for them:
// MAP v19+:  npcstrings[10], new_items[10], new_item_x[10], new_item_y[10] (100 bytes, unused by 2.55 too)
// MAP v20+:  screen script number and its 8 InitD values
// MAP v21+:  preloadscript
// MAP v22+:  hidelayers, hidescriptlayers
// MAP v25+:  FFCs past the 32 this engine has
// MAP v27+:  guytile, guycs, roomflags
// MAP v30+:  usr_notes
// MAP v31+:  per-map scrolling region ids (8 x 8 bytes)
// MAP v32+:  maze_transition_wipe
// MAP v33+:  FFC layer
// MAP v34+:  exstate_reset, exstate_carry, the high 16 bits of noreset and nocarry
// MAP v35+:  screen_gravity, screen_terminal_v
// MAP v40+:  FFC viewport_suspend_range, viewport_despawn_range
// MAP v41+:  inst_init maps of the screen script and of every FFC script
// DMAP v10+: sideview
// DMAP v13+: dmap script InitD labels (8 x 65 chars)
// DMAP v14+: labels of the subscreen scripts' InitDs (8 x 65 chars)
// DMAP v15+: onmap script labels (8 x 65 chars)
// DMAP v16+: mirrorDMap
// DMAP v19+: overlay_subscreen
// DMAP v20+: intro_string_id
// DMAP v21:  region ids (8 x 8 bytes)
// DMAP v23+: dmap_gravity, dmap_terminal_v
// DMAP v24+: map_subscreen, floor
// DMAP v26+: disabled item bits past item 255
// DMAP v27+: inst_init maps of the four DMap scripts
// DMAP v28+: viewport_follow
//
// Narrowed to what 2.53 can hold:
// minimap/largemap tiles: 32 bits in the file from v11, and only NEWMAXTILES exist; out of range becomes 0 (no map graphic)
// combo numbers (screen, secret, under, FFC): a word can reach 65535 but MAXCOMBOS is 65280; the rest become combo 0
// enemy ids at or above MAXGUYS become 0; door type 16 ("no door") becomes dWALL
// combo flags: 104 (the new number of mfPUSHED) becomes 2.53's mfPUSHED, everything else from 104 up becomes mfNONE
// room types past 2.53's (the bottle shop) become rNONE; pNOSPAWN (6) becomes pRANDOM with the enemy list emptied
// item (word from v39), and the item a special item or grumble room names in catchall: ids of MAXITEMS and up give no item
// screen sfx (word from v38): ids above 255 go to an unused sound slot (see q255_resolve_high_screen_sfx)
// FFC hitbox and tile sizes (separate from v24): clamped to the 1-64 and 1-4 that ffwidth and ffheight can hold
// FFC flags: only the bits 2.53 knows are kept (ffc_ignorechanger, imprecisionchanger, lensinvis, platform, cmb_solid are not)
// lens_show, lens_hide (v28+): one layer goes into lens_layer, 2.53 cannot hide more than one or the base layer
// DMap title (a word-sized string from v20): the first 20 characters, space padded as 2.53 expects

#include "precompiled.h" //always first

#include "q255.h"
#include "q30.h"
#include "q30_audio.h"
#include "q30_triggers.h"

extern std::vector<mapscr> TheMaps;
extern zcmap               *ZCMaps;
extern dmap                *DMaps;
extern word                map_count;
extern refInfo             ffcScriptData[32];

void clear_screen(mapscr *temp_scr); // src/qst.cpp

static const byte DOOR_NONE_255 = 16;
static const int FFCS_IN_FILE = 32;     // the old format's mask is a dword; matches MAXFFCS and mapscr's arrays
static const int FFCS_MAX_IN_FILE = 128;// upstream's MAXFFCS: a larger count in the newer format is an invalid file
static const int ARG_LABELS_SIZE = 8 * 65;
static const int INITD_SIZE = 8 * 4;
static const int REGION_IDS_SIZE = 8 * 8;

// bits of the screen's "has" dword, from v23
static const dword HAS_ROOMDATA  = 0x00000001;
static const dword HAS_ITEM      = 0x00000002;
static const dword HAS_TWARP     = 0x00000004;
static const dword HAS_SWARP     = 0x00000008;
static const dword HAS_WARPRET   = 0x00000010;
static const dword HAS_LAYERS    = 0x00000020;
static const dword HAS_MAZE      = 0x00000040;
static const dword HAS_D_S_U     = 0x00000080;
static const dword HAS_FLAGS     = 0x00000100;
static const dword HAS_ENEMY     = 0x00000200;
static const dword HAS_CARRY     = 0x00000400;
static const dword HAS_SCRIPT    = 0x00000800;
static const dword HAS_SECRETS   = 0x00002000;
static const dword HAS_COMBOFLAG = 0x00004000;
static const dword HAS_MISC      = 0x00008000;

static const byte FLAG_PUSHED_NEW = 104;    // mfPUSHED in the newer enum
static const byte PATTERN_NOSPAWN = 6;
static const byte SCREEN_GRAVITY_BITS = 0x03;   // upstream's fSCREEN_GRAVITY, in flags10
static const long DMF_CUSTOM_GRAVITY = 0x800000;
static const long DMF_CUSTOM_VIEWPORT_FOLLOW = 0x1000000;
static const dword FFC_FLAGS_253 = 0xFC0007FF;  // bits 0-10 and the changer bits 26-31

static word clamp_combo(word combo)
{
    return combo < MAXCOMBOS ? combo : (word)0;
}

static byte map_combo_flag(byte flag)
{
    if(flag == FLAG_PUSHED_NEW)
        return (byte)mfPUSHED;

    return flag < mfMAX ? flag : (byte)mfNONE;
}

static byte map_room(byte room)
{
    return room < rMAX ? room : (byte)rNONE;
}

// A room type past 2.53's (the bottle shop) is noted beside the screen, for screens kept in TheMaps.
static byte read_room(byte room, mapscr *t)
{
    if(room >= rMAX && !TheMaps.empty() && t >= &TheMaps[0] && t < &TheMaps[0] + TheMaps.size())
        q30_note_room((int)(t - &TheMaps[0]), room);

    return map_room(room);
}

static byte map_door(byte door)
{
    return door == DOOR_NONE_255 ? (byte)dWALL : door;
}

// Screen sound fields hold a byte. A sound past 255 is recorded here, and when both the
// map and sound sections have been read it gets a free slot of its own (the sound
// section comes later in the file).
struct HighScreenSfx
{
    int screen;
    byte mapscr::*field;
    int sfx;
};

static std::vector<HighScreenSfx> g_high_screen_sfx;

void q255_resolve_high_screen_sfx()
{
    for(size_t i = 0; i < g_high_screen_sfx.size(); ++i)
    {
        const HighScreenSfx &h = g_high_screen_sfx[i];

        if((size_t)h.screen < TheMaps.size())
            TheMaps[h.screen].*h.field = (byte)q30_sfx_alias(h.sfx);
    }

    if(!g_high_screen_sfx.empty())
        q30_sfx_finish_aliases();

    g_high_screen_sfx.clear();
}

static void read_words(Q255Section &s, word *dst, int n)
{
    for(int i = 0; i < n; ++i)
        dst[i] = s.igetw();
}

static void read_combos(Q255Section &s, word *dst, int n)
{
    for(int i = 0; i < n; ++i)
        dst[i] = clamp_combo(s.igetw());
}

static void read_flags(Q255Section &s, byte *dst, int n)
{
    s.read(dst, n);

    for(int i = 0; i < n; ++i)
        dst[i] = map_combo_flag(dst[i]);
}

// The screen's placed flags. The ones 2.53 can't hold become mfNONE in sflag, and the
// true number is kept beside it (only for screens that are stored in TheMaps).
static void read_placed_flags(Q255Section &s, mapscr *t)
{
    byte raw[176];
    s.read(raw, 176);

    if(s.failed)
        return;

    const bool stored = !TheMaps.empty() && t >= &TheMaps[0] && t < &TheMaps[0] + TheMaps.size();
    const int index = stored ? (int)(t - &TheMaps[0]) : -1;

    for(int i = 0; i < 176; ++i)
    {
        if(stored && raw[i] > mfRAFT_BOUNCE && raw[i] != FLAG_PUSHED_NEW)
            q30_note_placed_flag(index, i, raw[i]);

        t->sflag[i] = map_combo_flag(raw[i]);
    }
}

static void read_enemies(Q255Section &s, mapscr *t)
{
    for(int k = 0; k < 10; ++k)
    {
        word e = s.igetw();
        t->enemy[k] = e < MAXGUYS ? e : (word)0;
    }
}

static void store_pattern(mapscr *t, byte pattern)
{
    t->pattern = pattern;

    if(pattern == PATTERN_NOSPAWN)
    {
        // 2.53 always spawns the screen's list, so "no spawn" has to empty it.
        t->pattern = pRANDOM;

        for(int k = 0; k < 10; ++k)
            t->enemy[k] = 0;
    }
}

void zasm30_clear_screen_scripts();
void zasm30_set_screen_script(int index, word script, const long *initd);

// Where in TheMaps the screen being read goes, -1 when it isn't kept.
static int g_screen_index = -1;

static byte screen_sfx(Q255Section &s, byte mapscr::*field)
{
    const word sfx = s.igetw();

    if(sfx < 256)
        return (byte)sfx;

    if(s.keep && g_screen_index >= 0)
    {
        HighScreenSfx h;
        h.screen = g_screen_index;
        h.field = field;
        h.sfx = sfx;
        g_high_screen_sfx.push_back(h);
    }

    return 0;
}

static void keep_screen_script(Q255Section &s, word script, const long *initd)
{
    if(s.keep && g_screen_index >= 0 && script > 0)
        zasm30_set_screen_script(g_screen_index, script, initd);
}

// p_getvar(script_config): the script number, run_args as a counted array and
// inst_init as a counted map of word to int. Either pointer can be NULL.
static void read_script_config(Q255Section &s, word *script, long *initd)
{
    const word id = s.igetw();

    if(script)
        *script = id;

    const word args = s.igetw();

    for(int i = 0; i < args && i < 8; ++i)
    {
        const long v = s.igetl_s();

        if(initd)
            initd[i] = v;
    }

    const word inst_init = s.igetw();
    s.skip((size_t)inst_init * 6);
}

static void read_ffc(Q255Section &s, mapscr *t, int m)
{
    t->ffdata[m] = clamp_combo(s.igetw());
    t->ffcset[m] = s.getc();
    t->ffdelay[m] = s.igetw();
    t->ffx[m] = s.igetl_s();
    t->ffy[m] = s.igetl_s();
    t->ffxdelta[m] = s.igetl_s();
    t->ffydelta[m] = s.igetl_s();
    t->ffxdelta2[m] = s.igetl_s();
    t->ffydelta2[m] = s.igetl_s();
    t->fflink[m] = s.getc();
    t->ffwidth[m] = s.getc();
    t->ffheight[m] = s.getc();
    t->ffflags[m] = s.igetl() & FFC_FLAGS_253;
    t->ffscript[m] = s.igetw();

    for(int i = 0; i < 8; ++i)
        t->initd[m][i] = s.igetl_s();

    t->inita[m][0] = s.getc() * 10000;
    t->inita[m][1] = s.getc() * 10000;
    t->initialized[m] = false;
}

// t has been through clear_screen, or is scratch for data being dropped. Both
// cases run this same code, so -q255-check exercises what a real load does.
static void read_screen(Q255Section &s, mapscr *t)
{
    t->valid = s.getc();
    t->guy = s.getc();
    t->str = s.igetw();
    t->room = read_room(s.getc(), t);
    t->item = s.getc();
    t->hasitem = s.getc();
    s.read(t->tilewarptype, 4);
    t->door_combo_set = s.igetw();
    s.read(t->warpreturnx, 4);
    s.read(t->warpreturny, 4);
    t->warpreturnc = s.igetw();
    t->stairx = s.getc();
    t->stairy = s.getc();
    t->itemx = s.getc();
    t->itemy = s.getc();
    t->color = s.igetw();
    t->enemyflags = s.getc();
    s.read(t->door, 4);

    for(int k = 0; k < 4; ++k)
        t->door[k] = map_door(t->door[k]);

    read_words(s, t->tilewarpdmap, 4);
    s.read(t->tilewarpscr, 4);
    t->tilewarpoverlayflags = s.getc();
    t->exitdir = s.getc();
    read_enemies(s, t);
    store_pattern(t, s.getc());
    s.read(t->sidewarptype, 4);
    t->sidewarpoverlayflags = s.getc();
    t->warparrivalx = s.getc();
    t->warparrivaly = s.getc();
    s.read(t->path, 4);
    s.read(t->sidewarpscr, 4);
    read_words(s, t->sidewarpdmap, 4);
    t->sidewarpindex = s.getc();
    t->undercombo = clamp_combo(s.igetw());
    t->undercset = s.getc();
    t->catchall = s.igetw();
    t->flags = s.getc();
    t->flags2 = s.getc();
    t->flags3 = s.getc();
    t->flags4 = s.getc();
    t->flags5 = s.getc();
    t->noreset = s.igetw();
    t->nocarry = s.igetw();

    if(t->flags5 & 32)
    {
        t->flags5 &= ~32;
        t->noreset |= 48;
    }

    t->flags6 = s.getc();
    t->flags7 = s.getc();
    t->flags8 = s.getc();
    t->flags9 = s.getc();
    t->flags10 = s.getc();
    t->csensitive = s.getc();
    t->oceansfx = s.getc();
    t->bosssfx = s.getc();
    t->secretsfx = s.getc();
    t->holdupsfx = s.getc();
    s.read(t->layermap, 6);
    s.read(t->layerscreen, 6);
    s.read(t->layeropacity, 6);
    t->timedwarptics = s.igetw();
    t->nextmap = s.getc();
    t->nextscr = s.getc();
    read_combos(s, t->secretcombo, 128);
    s.read(t->secretcset, 128);
    read_flags(s, t->secretflag, 128);
    read_combos(s, &t->data[0], 176);
    read_placed_flags(s, t);
    s.read(&t->cset[0], 176);
    t->screen_midi = s.igetw_s();
    t->lens_layer = s.getc();
    t->numff = s.igetl();

    for(int m = 0; m < FFCS_IN_FILE; ++m)
    {
        if((t->numff >> m) & 1)
            read_ffc(s, t, m);
    }

    if(s.version >= 19)
        s.skip(100);    // the unused npcstrings and new_item arrays

    if(s.version >= 20)
    {
        word script = s.igetw();
        long initd[8];

        for(int i = 0; i < 8; ++i)
            initd[i] = s.igetl_s();

        keep_screen_script(s, script, initd);
    }

    if(s.version >= 21)
        s.skip(1);      // preloadscript

    if(s.version >= 22)
        s.skip(2);      // hidelayers, hidescriptlayers
}

static byte ffc_size(long hit, byte tiles)
{
    const int pixels = hit < 1 ? 1 : (hit > 64 ? 64 : (int)hit);
    const int blocks = tiles < 1 ? 1 : (tiles > 4 ? 4 : tiles);
    return (byte)(((blocks - 1) << 6) | (pixels - 1));
}

// One FFC of the v23+ format. m can be past this engine's 32; those are walked and dropped.
// masked: the version says which FFCs exist through a mask, so an empty combo is still a whole FFC.
static void read_ffc_new(Q255Section &s, mapscr *t, int m, bool masked)
{
    const word combo = s.igetw();

    if(!masked && combo == 0)
        return;     // an empty slot is only the combo word

    const byte cset = s.getc();
    const word delay = s.igetw();
    const long x = s.igetl_s();
    const long y = s.igetl_s();
    const long vx = s.igetl_s();
    const long vy = s.igetl_s();
    const long ax = s.igetl_s();
    const long ay = s.igetl_s();
    const byte link = s.getc();
    byte width, height;

    if(s.version < 24)
    {
        width = s.getc();
        height = s.getc();
    }
    else
    {
        const long hit_w = s.igetl_s();
        const long hit_h = s.igetl_s();
        const byte tiles_w = s.getc();
        const byte tiles_h = s.getc();
        width = ffc_size(hit_w, tiles_w);
        height = ffc_size(hit_h, tiles_h);
    }

    const dword flags = s.igetl();
    word script = 0;
    long initd[8];
    memset(initd, 0, sizeof(initd));

    if(s.version < 41)
    {
        script = s.igetw();

        for(int i = 0; i < 8; ++i)
            initd[i] = s.igetl_s();
    }
    else
        read_script_config(s, &script, initd);

    long a0 = 10000, a1 = 10000;

    if(s.version < 33)
    {
        a0 = s.getc() * 10000;
        a1 = s.getc() * 10000;
    }
    else
        s.skip(1);      // layer

    if(s.version >= 40)
        s.skip(8);      // viewport_suspend_range, viewport_despawn_range

    if(m >= FFCS_IN_FILE)
        return;

    if(!masked)
        t->numff |= (dword)1 << m;

    t->ffdata[m] = clamp_combo(combo);
    t->ffcset[m] = cset;
    t->ffdelay[m] = delay;
    t->ffx[m] = x;
    t->ffy[m] = y;
    t->ffxdelta[m] = vx;
    t->ffydelta[m] = vy;
    t->ffxdelta2[m] = ax;
    t->ffydelta2[m] = ay;
    t->fflink[m] = link;
    t->ffwidth[m] = width;
    t->ffheight[m] = height;
    t->ffflags[m] = flags & FFC_FLAGS_253;
    t->ffscript[m] = script;

    for(int i = 0; i < 8; ++i)
        t->initd[m][i] = initd[i];

    t->inita[m][0] = a0;
    t->inita[m][1] = a1;
    t->initialized[m] = false;
}

// A screen's music from v37: -2 silence, -1 the DMap's, 0 nothing, or a
// quest_music entry, which 2.53 cannot play.
static short map_screen_music(long music)
{
    return (music == -2 || music == 0) ? (short)-2 : (short)-1;
}

static byte map_lens(byte lens_layer, byte show, byte hide)
{
    for(int layer = 1; layer <= 6; ++layer)
    {
        if(show & (1 << layer))
            return (byte)((layer - 1) | llLENSSHOWS);

        if(hide & (1 << layer))
            return (byte)((layer - 1) | llLENSHIDES);
    }

    return lens_layer;
}

// The v23+ format: a "has" dword says which groups of fields the screen wrote,
// and the rest keep their defaults. t may be scratch left over from another
// screen, so nothing here decides what to read from a field of t.
static void read_screen_new(Q255Section &s, mapscr *t, int screen, const word *autolayers, word autopalette)
{
    t->valid = s.getc();

    if(!(t->valid & mVALID))
    {
        if(s.version > 25)
        {
            for(int q = 0; q < 6; ++q)
            {
                const byte layer_map = autolayers[q] < 256 ? (byte)autolayers[q] : (byte)0;
                t->layermap[q] = layer_map;

                if(layer_map)
                    t->layerscreen[q] = (byte)screen;
            }

            t->color = autopalette;
        }

        return;
    }

    const dword has = s.igetl();

    // what the newer engine's mapscr starts with when these groups are absent
    t->csensitive = 1;
    t->secretsfx = 27;
    t->holdupsfx = 20;
    t->screen_midi = -1;

    for(int i = 0; i < 4; ++i)
        t->sidewarptype[i] = wtSCROLL;

    if(has & HAS_ROOMDATA)
    {
        t->guy = s.getc();

        if(s.version > 26)
            s.skip(4 + 1 + 2);      // guytile, guycs, roomflags

        t->str = s.igetw();
        t->room = read_room(s.getc(), t);
        t->catchall = s.igetw();

        // these two rooms use it as an item id, which reaches 1023 now
        if((t->room == rSP_ITEM || t->room == rGRUMBLE) && t->catchall >= MAXITEMS)
            t->catchall = 0;
    }

    if(has & HAS_ITEM)
    {
        const word item = s.version < 39 ? (word)s.getc() : s.igetw();
        t->hasitem = s.getc();
        t->itemx = s.getc();
        t->itemy = s.getc();

        if(item < MAXITEMS)
            t->item = (byte)item;
        else
        {
            t->item = 0;
            t->hasitem = 0;
        }
    }

    if(has & (HAS_SWARP | HAS_TWARP))
        t->warpreturnc = s.igetw();

    if(has & HAS_TWARP)
    {
        s.read(t->tilewarptype, 4);
        read_words(s, t->tilewarpdmap, 4);
        s.read(t->tilewarpscr, 4);
        t->tilewarpoverlayflags = s.getc();
    }

    if(has & HAS_SWARP)
    {
        s.read(t->sidewarptype, 4);
        read_words(s, t->sidewarpdmap, 4);
        s.read(t->sidewarpscr, 4);
        t->sidewarpoverlayflags = s.getc();
        t->sidewarpindex = s.getc();
    }

    if(has & HAS_WARPRET)
    {
        s.read(t->warpreturnx, 4);
        s.read(t->warpreturny, 4);
        t->warparrivalx = s.getc();
        t->warparrivaly = s.getc();
    }

    if(has & HAS_LAYERS)
    {
        s.read(t->layermap, 6);
        s.read(t->layerscreen, 6);
        s.read(t->layeropacity, 6);
        s.skip(2);      // hidelayers, hidescriptlayers
    }

    if(has & HAS_MAZE)
    {
        s.read(t->path, 4);
        t->exitdir = s.getc();

        if(s.version >= 32)
            s.skip(1);  // maze_transition_wipe
    }

    if(has & HAS_D_S_U)
    {
        t->door_combo_set = s.igetw();
        s.read(t->door, 4);

        for(int k = 0; k < 4; ++k)
            t->door[k] = map_door(t->door[k]);

        t->stairx = s.getc();
        t->stairy = s.getc();
        t->undercombo = clamp_combo(s.igetw());
        t->undercset = s.getc();
    }

    byte flags10 = 0;

    if(has & HAS_FLAGS)
    {
        t->flags = s.getc();
        t->flags2 = s.getc();
        t->flags3 = s.getc();
        t->flags4 = s.getc();
        t->flags5 = s.getc();
        t->flags6 = s.getc();
        t->flags7 = s.getc();
        t->flags8 = s.getc();
        t->flags9 = s.getc();
        flags10 = s.getc();
        t->flags10 = flags10;
        t->enemyflags = s.getc();
    }

    if(has & HAS_ENEMY)
    {
        read_enemies(s, t);
        store_pattern(t, s.getc());
    }

    if(has & HAS_CARRY)
    {
        if(s.version < 34)
        {
            t->noreset = s.igetw();
            t->nocarry = s.igetw();
        }
        else
        {
            t->noreset = (word)s.igetl();
            t->nocarry = (word)s.igetl();
            s.skip(8);  // exstate_reset, exstate_carry
        }

        t->nextmap = s.getc();
        t->nextscr = s.getc();
    }

    if(has & HAS_SCRIPT)
    {
        word script = 0;
        long initd[8];
        memset(initd, 0, sizeof(initd));

        if(s.version < 41)
        {
            script = s.igetw();
            s.skip(1);  // preloadscript

            for(int i = 0; i < 8; ++i)
                initd[i] = s.igetl_s();
        }
        else
        {
            read_script_config(s, &script, initd);
            s.skip(1);  // preloadscript
        }

        keep_screen_script(s, script, initd);
    }

    if(has & HAS_SECRETS)
    {
        read_combos(s, t->secretcombo, 128);
        s.read(t->secretcset, 128);
        read_flags(s, t->secretflag, 128);
    }

    if(has & HAS_COMBOFLAG)
    {
        read_combos(s, &t->data[0], 176);
        read_placed_flags(s, t);
        s.read(&t->cset[0], 176);
    }

    if(has & HAS_MISC)
    {
        t->color = s.igetw();
        t->csensitive = s.getc();

        if(s.version < 38)
        {
            t->oceansfx = s.getc();
            t->bosssfx = s.getc();
            t->secretsfx = s.getc();
            t->holdupsfx = s.getc();
        }
        else
        {
            t->oceansfx = screen_sfx(s, &mapscr::oceansfx);
            t->bosssfx = screen_sfx(s, &mapscr::bosssfx);
            t->secretsfx = screen_sfx(s, &mapscr::secretsfx);
            t->holdupsfx = screen_sfx(s, &mapscr::holdupsfx);
        }

        t->timedwarptics = s.igetw();

        if(s.version < 37)
            t->screen_midi = s.igetw_s();
        else
        {
            const long music = s.igetl_s();
            t->screen_midi = map_screen_music(music);

            if(s.keep && g_screen_index >= 0 && music > 0)
                q30_set_screen_music(g_screen_index, music);
        }

        t->lens_layer = s.getc();

        if(s.version > 27)
        {
            const byte show = s.getc();
            const byte hide = s.getc();
            t->lens_layer = map_lens(t->lens_layer, show, hide);
        }
    }

    const bool masked = s.version < 25;
    dword bits = 0;
    int ffc_count = FFCS_IN_FILE;

    if(masked)
        bits = s.igetl();
    else
    {
        ffc_count = s.igetw();

        if(ffc_count > FFCS_MAX_IN_FILE)
        {
            s.failed = true;
            return;
        }
    }

    t->numff = bits;

    for(int m = 0; m < ffc_count && !s.failed; ++m)
    {
        if(masked && !((bits >> m) & 1))
            continue;

        read_ffc_new(s, t, m, masked);
    }

    if(s.version > 29)
    {
        const dword notes = s.igetl();

        if(notes > s.left())
            s.failed = true;
        else
            s.skip(notes);
    }

    if(s.version >= 35 && (flags10 & SCREEN_GRAVITY_BITS))
        s.skip(8);      // screen_gravity, screen_terminal_v
}

// For ZQuest's map writer (src/port/zqeditor/zqw_maps.cpp), which checks that what it writes
// reads back as ZQuest holds it: one screen from s.pos into t (cleared first), read as
// q255_read_maps reads it, keeping nothing beside it. Only ZQuest sets q255_keep_for_editor.
int q255_editor_read_screen(Q255Section &s, mapscr *t, int screen, const word *autolayers, word autopalette)
{
    if(!q255_keep_for_editor || s.version < 18 || s.version > 41)
        return Q255_NOT_HANDLED;

    const int index = g_screen_index;
    const bool keep = s.keep;
    g_screen_index = -1;
    s.keep = false;
    clear_screen(t);

    if(s.version >= 23)
        read_screen_new(s, t, screen, autolayers, autopalette);
    else
        read_screen(s, t);

    g_screen_index = index;
    s.keep = keep;
    return s.failed ? qe_invalid : 0;
}

// DMap scripts read so far. The DMAP section can come before the MAP section, whose
// reader empties the shared tables, so they go back in afterwards.
struct DMapScriptRef
{
    int dmap, kind;
    word script;
    long initd[8];
};

static std::vector<DMapScriptRef> g_dmap_scripts;

static void store_dmap_script(int dmap, int kind, word script, const long *initd)
{
    q30_set_dmap_script(dmap, kind, script, initd);

    if(!script)
        return;

    DMapScriptRef r;
    r.dmap = dmap;
    r.kind = kind;
    r.script = script;
    memcpy(r.initd, initd, sizeof(r.initd));
    g_dmap_scripts.push_back(r);
}

// The same for each DMap's overlay and map subscreens, title and intro string.
struct DMapExtraRef
{
    int dmap, overlay, map, floor, mirror;
    std::string title;
    long intro;
};

static std::vector<DMapExtraRef> g_dmap_extras;

static void store_dmap_extras(int dmap, int overlay, int map, int floor, int mirror, const std::string &title, long intro)
{
    DMapExtraRef r;
    r.dmap = dmap;
    r.overlay = overlay;
    r.map = map;
    r.floor = floor;
    r.mirror = mirror;
    r.title = title;
    r.intro = intro;
    g_dmap_extras.push_back(r);
    q30_set_dmap_subscreens(dmap, overlay, map);
    q30_set_dmap_texts(dmap, title.c_str(), intro);
    q30_set_dmap_floor(dmap, floor);
    q30_set_dmap_mirror(dmap, mirror);
}

static void restore_dmap_scripts()
{
    for(size_t i = 0; i < g_dmap_scripts.size(); ++i)
        q30_set_dmap_script(g_dmap_scripts[i].dmap, g_dmap_scripts[i].kind, g_dmap_scripts[i].script, g_dmap_scripts[i].initd);

    for(size_t i = 0; i < g_dmap_extras.size(); ++i)
    {
        const DMapExtraRef &r = g_dmap_extras[i];
        q30_set_dmap_subscreens(r.dmap, r.overlay, r.map);
        q30_set_dmap_texts(r.dmap, r.title.c_str(), r.intro);
        q30_set_dmap_floor(r.dmap, r.floor);
        q30_set_dmap_mirror(r.dmap, r.mirror);
    }
}

// The scrolling region ids each map stores (MAP v31+, 8 x 8 bytes; q30_regions.cpp reads them).
std::vector<std::vector<byte> > q255_map_region_ids;

int q255_read_maps(Q255Section &s)
{
    if(s.keep)
    {
        q255_map_region_ids.clear();
        q30_clear_placed_flags();
        zasm30_clear_screen_scripts();
        q30_clear_tables();
        q30_audio_clear_screens();
        restore_dmap_scripts();
        g_high_screen_sfx.clear();
    }

    if(s.version < 18 || s.version > 41)
        return Q255_NOT_HANDLED;

    const int count = s.igetw();

    if(s.failed)
        return qe_invalid;

    const int stored = !s.keep ? 0 : (count < MAXMAPS2 ? count : MAXMAPS2);

    if(s.keep)
    {
        const int screens = MAPSCRS * stored;
        TheMaps.resize(screens);

        for(int i = 0; i < screens; ++i)
            clear_screen(&TheMaps[i]);

        for(int i = 0; i < 32; ++i)
        {
            ffcScriptData[i].a[0] = 10000;
            ffcScriptData[i].a[1] = 10000;
        }

        memset(ZCMaps, 0, sizeof(zcmap) * MAXMAPS2);

        zcmap defaults;
        memset(&defaults, 0, sizeof(zcmap));
        defaults.scrResWidth = 256;
        defaults.scrResHeight = 224;
        defaults.tileWidth = 16;
        defaults.tileHeight = 11;
        defaults.viewWidth = 256;
        defaults.viewHeight = 176;
        defaults.viewX = 0;
        defaults.viewY = 64;
        defaults.subaWidth = 256;
        defaults.subaHeight = 168;
        defaults.subaTrans = false;
        defaults.subpWidth = 256;
        defaults.subpHeight = 56;
        defaults.subpTrans = false;

        for(int i = 0; i < stored; ++i)
            ZCMaps[i] = defaults;

        map_count = (word)stored;
    }

    mapscr scratch;

    for(int i = 0; i < count; ++i)
    {
        byte map_valid = 1;
        word autolayers[6];
        word autopalette = 0;
        memset(autolayers, 0, sizeof(autolayers));

        if(s.version > 22)
            map_valid = s.getc();

        if(map_valid)
        {
            if(s.version > 25)
            {
                read_words(s, autolayers, 6);

                if(s.version >= 36)
                    autopalette = s.igetw();
            }

            if(s.version >= 31)
            {
                std::vector<byte> ids(REGION_IDS_SIZE, 0);
                int used = 0;

                for(int k = 0; k < REGION_IDS_SIZE; ++k)
                {
                    ids[k] = s.getc();
                    used += ids[k] != 0;
                }

                if(s.keep && !s.failed && i < stored)
                {
                    if((int)q255_map_region_ids.size() <= i)
                        q255_map_region_ids.resize(i + 1);

                    q255_map_region_ids[i] = ids;

                    if(used)
                        al_trace("2.55 maps: map %d has scrolling regions (%d of its 64 id bytes set)\n", i + 1, used);
                }
            }
        }

        if(s.failed)
            return qe_invalid;

        if(!map_valid)
            continue;   // no screens in the file; the map stays cleared

        for(int j = 0; j < MAPSCRS; ++j)
        {
            mapscr *t = i < stored ? &TheMaps[i * MAPSCRS + j] : &scratch;
            g_screen_index = i < stored ? i * MAPSCRS + j : -1;

            if(s.version >= 23)
                read_screen_new(s, t, j, autolayers, autopalette);
            else
                read_screen(s, t);

            if(s.failed)
                return qe_invalid;
        }
    }

    if(s.keep)
        q255_resolve_high_screen_sfx();

    return 0;
}

static tile_id read_dmap_tile(Q255Section &s)
{
    const dword tile = s.version >= 11 ? s.igetl() : (dword)s.igetw();
    q255_note_tile("dmap", tile);
    return tile < (dword)NEWMAXTILES ? (tile_id)tile : (tile_id)0;
}

static void blank_dmap(dmap &d)
{
    memset(&d, 0, sizeof(dmap));
    memset(d.title, ' ', 20);
    memset(d.intro, ' ', 72);
}

// p_getbitstr: a word size, and if it isn't 0 a "pairs" byte, a word count and
// the bytes, with a word index in front of each one when pairs is set.
static void read_disabled_items(Q255Section &s, dmap &d)
{
    if(!s.igetw())
        return;

    const byte pairs = s.getc();
    const int count = s.igetw();

    for(int i = 0; i < count && !s.failed; ++i)
    {
        const int index = pairs ? (int)s.igetw() : i;
        const byte bits = s.getc();

        for(int b = 0; b < 8; ++b)
        {
            const int item = index * 8 + b;

            if(item < MAXITEMS && (bits & (1 << b)))
                d.disableditems[item] = 1;
        }
    }
}

// Eight InitD values, 8 x 4 bytes.
static void read_initd(Q255Section &s, long *initd)
{
    for(int i = 0; i < 8; ++i)
        initd[i] = s.igetl_s();
}

// A 2.53-style DMap midi number (0 none, 1 overworld, 2 dungeon, 3 level 9, 4+ a custom
// tune) as a music object's midi value.
static short old_midi_to_object(byte midi)
{
    switch(midi)
    {
    case 0:
        return 0;

    case 1:
        return -5;              // BUILTIN_MIDI_OVERWORLD

    case 2:
        return -1;              // BUILTIN_MIDI_DUNGEON

    case 3:
        return -4;              // BUILTIN_MIDI_LEVEL9
    }

    return (short)(midi - 3);
}

// full: the whole title, as upstream keeps it (its std::string; 2.53's title holds 20 characters)
static void read_dmap_title(Q255Section &s, dmap &d, std::string &full)
{
    if(s.version < 20)
    {
        s.read(d.title, 21);
        char t[22];
        memcpy(t, d.title, 21);
        t[21] = 0;
        full = t;
        return;
    }

    const std::string title = s.getwstr();
    full = title.c_str();

    for(size_t i = 0; i < title.size() && i < 20 && title[i]; ++i)
        d.title[i] = title[i];
}

// index: where the DMap goes, -1 when it isn't kept.
static void read_dmap(Q255Section &s, dmap &d, int index)
{
    const bool keep = s.keep && index >= 0;
    d.map = s.getc();
    d.level = s.igetw();
    d.xoff = (char)s.getc();
    d.compass = s.getc();
    d.color = s.igetw();

    if(s.version < 25)
        d.midi = s.getc();

    d.cont = s.getc();
    d.type = s.getc();

    if((d.type & dmfTYPE) == dmOVERW)
        d.xoff = 0;

    s.read(d.grid, 8);
    s.read(d.name, 21);
    std::string full_title;
    read_dmap_title(s, d, full_title);
    s.read(d.intro, 73);
    d.minimap_1_tile = read_dmap_tile(s);
    d.minimap_1_cset = s.getc();
    d.minimap_2_tile = read_dmap_tile(s);
    d.minimap_2_cset = s.getc();
    d.largemap_1_tile = read_dmap_tile(s);
    d.largemap_1_cset = s.getc();
    d.largemap_2_tile = read_dmap_tile(s);
    d.largemap_2_cset = s.getc();

    if(s.version < 25)
    {
        s.read(d.tmusic, 56);
        d.tmusictrack = s.getc();
    }

    d.active_subscreen = s.getc();
    d.passive_subscreen = s.getc();

    if(s.version >= 26)
        read_disabled_items(s, d);
    else
    {
        byte di[32];
        memset(di, 0, sizeof(di));
        s.read(di, 32);

        for(int j = 0; j < MAXITEMS; ++j)
            d.disableditems[j] = (di[j / 8] & (1 << (j % 8))) ? 1 : 0;
    }

    d.flags = s.igetl_s();

    if(s.version >= 10)
        s.skip(1);      // sideview

    if(s.version >= 27)
    {
        // as upstream writes them: active, active subscreen, passive subscreen, onmap
        static const int kinds[4] = { q30_dmap_active, q30_dmap_active_subscreen, q30_dmap_passive_subscreen, q30_dmap_onmap };

        for(int q = 0; q < 4; ++q)
        {
            word script = 0;
            long initd[8];
            memset(initd, 0, sizeof(initd));
            read_script_config(s, &script, initd);

            if(keep && !s.failed)
                store_dmap_script(index, kinds[q], script, initd);
        }
    }
    else
    {
        long initd[8];

        if(s.version >= 12)
        {
            word script = s.igetw();
            read_initd(s, initd);

            if(keep && !s.failed)
                store_dmap_script(index, q30_dmap_active, script, initd);
        }

        if(s.version >= 13)
            s.skip(ARG_LABELS_SIZE);

        if(s.version >= 14)
        {
            word active = s.igetw();
            word passive = s.igetw();
            read_initd(s, initd);
            s.skip(ARG_LABELS_SIZE);

            if(keep && !s.failed)
            {
                store_dmap_script(index, q30_dmap_active_subscreen, active, initd);
                store_dmap_script(index, q30_dmap_passive_subscreen, passive, initd);
            }
        }

        if(s.version >= 15)
        {
            word script = s.igetw();
            read_initd(s, initd);
            s.skip(ARG_LABELS_SIZE);

            if(keep && !s.failed)
                store_dmap_script(index, q30_dmap_onmap, script, initd);
        }
    }

    int mirror_dmap = -1;   // upstream's default: no mirror DMap

    if(s.version >= 16)
        mirror_dmap = (short)s.igetw();     // mirrorDMap

    long loop_start = 0, loop_end = 0, xfade_in = 0, xfade_out = 0;

    if(s.version >= 18 && s.version < 25)
    {
        loop_start = s.igetl_s();
        loop_end = s.igetl_s();
        xfade_in = s.igetl_s();
        xfade_out = s.igetl_s();
    }

    int overlay_sub = 0, map_sub = -1;     // upstream's defaults: the first overlay, no map subscreen

    if(s.version >= 19)
        overlay_sub = s.getc();     // overlay_subscreen

    long intro_string = 0;

    if(s.version >= 20)
        intro_string = s.igetl_s();     // intro_string_id

    if(s.version == 21)
        s.skip(REGION_IDS_SIZE);

    if(s.version > 22 && (d.flags & DMF_CUSTOM_GRAVITY))
        s.skip(4 + 4);  // dmap_gravity, dmap_terminal_v

    if(s.version >= 28 && (d.flags & DMF_CUSTOM_VIEWPORT_FOLLOW))
        s.skip(4 + 4 + 4 + 2);  // viewport_follow

    int floor = 0;

    if(s.version > 23)
    {
        map_sub = (short)s.igetw();     // map_subscreen
        floor = s.getc();
    }

    if(keep && !s.failed)
        store_dmap_extras(index, overlay_sub, map_sub, floor, mirror_dmap, full_title, intro_string);

    if(s.version >= 25)
    {
        const word music = s.igetw();

        if(keep && !s.failed)
            q30_set_dmap_music(index, music, true);
    }
    else if(keep && !s.failed && (loop_start || loop_end || xfade_in || xfade_out))
    {
        // Loop points or crossfades: the DMap becomes a music object so they can work.
        // Upstream turns every older DMap into one; the rest stay with 2.53's DMap music.
        char path[57];
        memcpy(path, d.tmusic, 56);
        path[56] = 0;
        int music = q30_music_add(NULL, path, loop_start, loop_end, xfade_in, xfade_out, d.tmusictrack, old_midi_to_object(d.midi));
        q30_set_dmap_music(index, music, false);
    }
}

// For ZQuest's DMap writer, as q255_editor_read_screen: one DMap from s.pos into d, keeping
// nothing beside it.
int q255_editor_read_dmap(Q255Section &s, dmap *d)
{
    if(!q255_keep_for_editor || s.version < 9 || s.version > 28)
        return Q255_NOT_HANDLED;

    const bool keep = s.keep;
    s.keep = false;
    blank_dmap(*d);
    read_dmap(s, *d, -1);
    s.keep = keep;
    return s.failed ? qe_invalid : 0;
}

int q255_read_dmaps(Q255Section &s)
{
    if(s.version < 9 || s.version > 28)
        return Q255_NOT_HANDLED;

    if(s.keep)
    {
        q30_audio_clear_dmaps();
        g_dmap_scripts.clear();
        g_dmap_extras.clear();

        for(int i = 0; i < MAXDMAPS; ++i)
        {
            blank_dmap(DMaps[i]);
            DMaps[i].type |= dmCAVE;
        }
    }

    const int count = s.igetw();

    if(s.failed)
        return qe_invalid;

    dmap temp;

    for(int i = 0; i < count; ++i)
    {
        blank_dmap(temp);
        read_dmap(s, temp, s.keep && i < MAXDMAPS ? i : -1);

        if(s.failed)
            return qe_invalid;

        if(s.keep && i < MAXDMAPS)
            memcpy(&DMaps[i], &temp, sizeof(dmap));
    }

    if(s.keep)
        q30_finish_music_tables();

    return 0;
}

// The 2.55 readers for misc colours, game icons, CSets, tiles, weapon sprites,
// sound effects and tunes. See q255.h.
//
// Read and dropped, because 2.53 has no field or room for them:
//   CSET v5-6: the 4 extra CSets at the end of each of the 512 level palettes
//            (2.55's per-level copies of main CSets 1, 5, 7 and 8; pdLEVEL is 17 there)
//   TILE:    tiles from NEWMAXTILES on; 2.55 allows 214500
//   WPN:     weapon sprites from MAXWPNS (256) on; 2.55 allows 8192
//   SFX v9:  channels past the second; the sample rate and length of OGG sounds
//            (the OGG holds them too; it is decoded in q30_audio.cpp when first played)
//
// Kept, but narrower than the file's value:
//   MCLR v3-4 tile ids, ICON v10 icons, WPN v7-9 tile: 32 bits in the file, 16 here.
//            Past NEWMAXTILES they become tile 0.
//   CSET v6: 8 bits per colour value in the file, 6 here (the older versions were 6 too)
//   WPN names: 64 bytes in the file (v9: up to 255), 63 characters here (the buffer has
//            room for the NUL)
//   SFX names: 36 bytes in the file (v9: up to 255), 35 characters here, likewise; sounds
//            past 255 have no name here
//   SFX v9 samples: 24-bit and float ones become 16-bit
//   MIDI v5 titles: up to 65535 bytes in the file, 35 characters here
//
// WPN v7's script is stored in wpndata.script, which nothing in 2.53 reads yet.

#include "precompiled.h" //always first

#include "q255.h"
#include "q30_audio.h"
#include "zc_malloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "colors.h"
#include "tiles.h"
#include "zsys.h"
#include "sfx.h"

extern byte                *colordata;
extern tiledata            *newtilebuf;
extern wpndata             *wpnsbuf;
extern char                palnames[MAXLEVELS][17];
extern char                *weapon_string[WPNCNT];
extern SAMPLE              customsfxdata[WAV_COUNT];
extern unsigned char       customsfxflag[WAV_COUNT>>3];
extern int                 sfxdat;
extern const char          *old_sfx_string[Z35];
extern char                *sfx_string[WAV_COUNT];
extern word                quest_header_zelda_build;
extern DATAFILE            *sfxdata;
extern byte                midi_flags[MIDIFLAGS_SIZE];

static const int CSET_BYTES = 48;

// CSET v5's layout: pdFULL, then 17 CSets per level, then the 30 sprite CSets.
static const int V5_LEVEL_CSETS = 17;
static const int V5_SPRITE_START = pdFULL + 17 * 512;
static const int V5_TOTAL_CSETS = pdFULL + 17 * 512 + 30;

// Limits of the format itself, above this engine's own.
static const int MAX_TILES_255 = 214500;
static const int MAX_SPRITES_255 = 8192;
static const int MAX_TUNES_255 = 512;

// The sample types of SFX v9 (upstream's SampleType).
static const byte SAMPLE_WAV = 1;
static const byte SAMPLE_OGG = 2;

static tile_id narrow_tile(dword id)
{
    q255_note_tile("misc or weapon", id);
    return id >= (dword)NEWMAXTILES ? (tile_id)0 : (tile_id)id;
}

// Where CSet i of a v5 block lives in colordata, or -1 if it has no home.
static int v5_cset_home(int i)
{
    if(i < pdFULL)
        return i;

    if(i >= V5_SPRITE_START)
        return newerpoSPRITE + (i - V5_SPRITE_START);

    int level = (i - pdFULL) / V5_LEVEL_CSETS;
    int cset = (i - pdFULL) % V5_LEVEL_CSETS;

    if(cset >= pdLEVEL)
        return -1;

    return pdFULL + level * pdLEVEL + cset;
}

// A tile's size in the file. Unknown formats take 256 bytes, as tilesize() says.
static size_t tile_file_size(byte format)
{
    switch(format)
    {
    case tf4Bit:
        return 128;

    case tf16Bit:
        return 512;

    case tf24Bit:
        return 768;

    case tf32Bit:
        return 1024;
    }

    return 256;
}

// q255_check_quest gets a blank header back from readheader, so fall back on the
// global readheader always sets.
static int quest_build(const Q255Section &s)
{
    if(s.header && s.header->zelda_version)
        return s.header->build;

    return quest_header_zelda_build;
}

// For a length taken from the file: has() could wrap around on a huge one.
static bool can_read(const Q255Section &s, dword n)
{
    return !s.failed && n <= s.left();
}

// Text from the file into one of this engine's fixed buffers, cut to fit.
static void store_name(char *dst, size_t room, const std::string &name)
{
    strncpy(dst, name.c_str(), room - 1);
    dst[room - 1] = 0;
}

// p_getbitstr: the size in bytes then, if it isn't 0, a byte saying which form
// follows, a word count, and that many (word index, byte value) pairs or byte values.
static bool read_bitstr(Q255Section &s, std::vector<byte> &bits)
{
    word size = s.igetw();
    bits.assign(size, 0);

    if(!size)
        return !s.failed;

    byte pairs = s.getc();
    word count = s.igetw();

    for(dword i = 0; i < count && !s.failed; ++i)
    {
        dword at = pairs ? (dword)s.igetw() : i;
        byte value = s.getc();

        if(at >= bits.size())
            bits.resize(at + 1, 0);

        bits[at] = value;
    }

    return !s.failed;
}

static bool bitstr_get(const std::vector<byte> &bits, int i)
{
    size_t at = (size_t)i / 8;
    return at < bits.size() && (bits[at] & (1 << (i % 8))) != 0;
}

static void default_sfx_name(int i)
{
    sprintf(sfx_string[i], "s%03d", i);

    if(i < Z35)
        strcpy(sfx_string[i], old_sfx_string[i-1]);
}

// Makes a sound slot empty. The mixer plays nothing for a NULL sample. Slots from
// WAV_COUNT up are in q30_audio.cpp and take the same treatment.
static void clear_sfx_slot(int i)
{
    SAMPLE *slot = q30_sfx_slot(i);

    if(slot->data != NULL)
        zc_free(slot->data);

    memset(slot, 0, sizeof(SAMPLE));
    slot->bits = 8;
    slot->freq = 22050;
    slot->priority = 128;
}

// A slot a v6-8 section has no sample for: as upstream does, the original
// sounds go back in the first slots and the rest stay empty.
static int default_sfx_slot(int i)
{
    clear_sfx_slot(i);

    if(i >= Z35)
        return 0;

    const SAMPLE *def = (const SAMPLE *)sfxdata[i].dat;
    size_t bytes = (def->bits == 8 ? 1 : 2) * (def->stereo == 0 ? 1 : 2) * def->len;
    void *data = calloc(bytes, 1);

    if(!data && bytes)
        return qe_nomem;

    if(bytes)
        memcpy(data, def->data, bytes);

    customsfxdata[i] = *def;
    customsfxdata[i].data = data;
    set_bit(customsfxflag, i-1, 1);
    return 0;
}

// Bytes of one value of an Allegro 5 sample, as al_get_audio_depth_size gives them
// (Allegro keeps a 24-bit value in 4 bytes). Bit 3 of the depth means unsigned. 0: not a depth.
static int a5_depth_bytes(byte depth)
{
    switch(depth & ~8)
    {
    case 0:
        return 1;

    case 1:
        return 2;

    case 2:
    case 3:
        return 4;
    }

    return 0;
}

// Stores a v9 sample, whose data is at the walker's position, in a sound slot.
// Allegro 5 samples are signed unless the depth says otherwise; a SAMPLE is always
// unsigned, 8 or 16 bits, in one or two channels. (Upstream converts the older
// sections' samples the other way, by subtracting 128 or 32768.)
static int store_a5_sample(Q255Section &s, int id, byte depth, int channels, dword freq, dword len)
{
    if(!len)
        return 0;

    int base = depth & ~8;
    bool is_unsigned = (depth & 8) != 0;
    int in_size = a5_depth_bytes(depth);
    int out_size = base == 0 ? 1 : 2;
    int out_channels = channels > 2 ? 2 : channels;
    size_t values = (size_t)len * out_channels;
    void *data = calloc(values, out_size);

    if(!data)
        return qe_nomem;

    const byte *in = s.data + s.pos;

    for(dword i = 0; i < len; ++i)
    {
        for(int c = 0; c < out_channels; ++c)
        {
            const byte *p = in + ((size_t)i * channels + c) * in_size;
            size_t at = (size_t)i * out_channels + c;

            if(base == 0)
            {
                ((byte *)data)[at] = is_unsigned ? p[0] : (byte)(p[0] ^ 0x80);
                continue;
            }

            word value;

            if(base == 1)
                value = (word)(p[0] | (p[1] << 8));
            else if(base == 2)
                value = (word)(p[1] | (p[2] << 8));
            else
            {
                float f;
                memcpy(&f, p, 4);

                if(f < -1.0f)
                    f = -1.0f;
                else if(f > 1.0f)
                    f = 1.0f;

                value = (word)(short)(f * 32767.0f);
            }

            if(!is_unsigned || base == 3)
                value ^= 0x8000;

            ((word *)data)[at] = value;
        }
    }

    clear_sfx_slot(id);
    SAMPLE *slot = q30_sfx_slot(id);
    slot->data = data;
    slot->bits = out_size * 8;
    slot->stereo = out_channels == 2;
    slot->freq = (int)freq;
    slot->priority = 128;
    slot->len = len;
    slot->loop_start = 0;
    slot->loop_end = len;
    slot->param = 0;

    if(id < WAV_COUNT)
        set_bit(customsfxflag, id-1, 1);

    return 0;
}

int q255_read_colors(Q255Section &s)
{
    if(s.version < 2 || s.version > 4)
        return Q255_NOT_HANDLED;

    zcolors c;
    memset(&c, 0, sizeof(c));

    if(s.keep)
        c = s.misc->colors;

    tile_id *tiles[6] =
    {
        &c.triforce_tile, &c.triframe_tile, &c.overworld_map_tile,
        &c.dungeon_map_tile, &c.blueframe_tile, &c.HCpieces_tile
    };

    c.text = s.getc();
    c.caption = s.getc();
    c.overw_bg = s.getc();
    c.dngn_bg = s.getc();
    c.dngn_fg = s.getc();
    c.cave_fg = s.getc();
    c.bs_dk = s.getc();
    c.bs_goal = s.getc();
    c.compass_lt = s.getc();
    c.compass_dk = s.getc();
    c.subscr_bg = s.getc();
    c.triframe_color = s.getc();
    c.link_dot = s.getc();
    c.bmap_bg = s.getc();
    c.bmap_fg = s.getc();
    c.triforce_cset = s.getc();
    c.triframe_cset = s.getc();
    c.overworld_map_cset = s.getc();
    c.dungeon_map_cset = s.getc();
    c.blueframe_cset = s.getc();

    if(s.version < 4)
    {
        for(int i = 0; i < 6; ++i)
            *tiles[i] = s.igetw();
    }

    c.HCpieces_cset = s.getc();
    c.subscr_shadow = s.getc();
    c.msgtext = s.getc();

    // v3 has both sizes; the 32-bit ones are the real ones.
    if(s.version >= 3)
    {
        for(int i = 0; i < 6; ++i)
            *tiles[i] = narrow_tile(s.igetl());
    }

    if(s.failed)
        return qe_invalid;

    if(s.keep)
        s.misc->colors = c;

    return 0;
}

int q255_read_icons(Q255Section &s)
{
    if(s.version < 1 || s.version > 10)
        return Q255_NOT_HANDLED;

    tile_id icons[4];

    for(int i = 0; i < 4; ++i)
    {
        if(s.version >= 10)
            icons[i] = narrow_tile(s.igetl());
        else
            icons[i] = s.igetw();
    }

    if(s.failed)
        return qe_invalid;

    if(s.keep)
    {
        for(int i = 0; i < 4; ++i)
            s.misc->icons[i] = icons[i];
    }

    return 0;
}

int q255_read_csets(Q255Section &s)
{
    if(s.version < 4 || s.version > 6)
        return Q255_NOT_HANDLED;

    bool v5 = s.version >= 5;
    int total = v5 ? V5_TOTAL_CSETS : newerpdTOTAL;

    if(!s.has((size_t)total * CSET_BYTES))
    {
        s.failed = true;
        return qe_invalid;
    }

    // v4 is this engine's own layout, v6 has v5's. v6 holds 8-bit values (upstream
    // scales the older versions up with _rgb_scale_6); 2.53 holds them 6-bit.
    for(int i = 0; i < total; ++i)
    {
        int home = v5 ? v5_cset_home(i) : i;

        if(s.keep && home >= 0)
        {
            byte *cset = colordata + home * CSET_BYTES;
            s.read(cset, CSET_BYTES);

            if(s.version >= 6)
            {
                for(int j = 0; j < CSET_BYTES; ++j)
                    cset[j] >>= 2;
            }
        }
        else
            s.skip(CSET_BYTES);
    }

    for(int i = 0; i < 512; ++i)
    {
        if(s.keep && i < MAXLEVELS)
            s.read(palnames[i], PALNAMESIZE);
        else
            s.skip(PALNAMESIZE);

        if(s.failed)
            return qe_invalid;
    }

    word count = s.igetw();

    if(s.failed || count > 256)
        return qe_invalid;

    palcycle cycles[256][3];
    memset(cycles, 0, sizeof(cycles));

    for(int i = 0; i < count; ++i)
    {
        for(int j = 0; j < 3; ++j)
            cycles[i][j].first = s.getc();

        for(int j = 0; j < 3; ++j)
            cycles[i][j].count = s.getc();

        for(int j = 0; j < 3; ++j)
            cycles[i][j].speed = s.getc();

        if(s.failed)
            return qe_invalid;
    }

    if(s.keep)
        memcpy(s.misc->cycles, cycles, sizeof(cycles));

    return 0;
}

int q255_read_tiles(Q255Section &s)
{
    if(s.version < 1 || s.version > 4)
        return Q255_NOT_HANDLED;

    // From v3 a format of 0 is a blank tile with no bytes after it.
    bool blank_tile_opt = s.version > 2;
    long tiles_used;

    // Only 2.55 quests get here, so only the build decides the count's width.
    if(quest_build(s) >= 41)
        tiles_used = s.igetl_s();
    else
        tiles_used = s.igetw();

    if(s.failed)
        return qe_invalid;

    if(tiles_used > MAX_TILES_255)
        tiles_used = MAX_TILES_255;

    al_trace("2.55 tiles: %ld in the file, %ld kept\n", tiles_used, tiles_used < NEWMAXTILES ? tiles_used : (long)NEWMAXTILES);

    for(long i = 0; i < tiles_used; ++i)
    {
        byte format = s.getc();

        if(s.failed)
            return qe_invalid;

        if(blank_tile_opt && format == tfInvalid)
        {
            if(s.keep && i < NEWMAXTILES)
                reset_tile(newtilebuf, (int)i, tf4Bit);

            continue;
        }

        // A 4-bit tile is packed two pixels per byte in the file (v1 on) and in
        // this engine's tile buffers alike, so unlike upstream it is copied as it is.
        size_t bytes = tile_file_size(format);

        if(!s.has(bytes))
        {
            s.failed = true;
            return qe_invalid;
        }

        if(!s.keep || i >= NEWMAXTILES)
        {
            s.skip(bytes);
            continue;
        }

        // Before v3, format 0 is a tile nothing was drawn on (its bytes still follow)
        if(format == tfInvalid)
        {
            s.skip(bytes);
            reset_tile(newtilebuf, (int)i, tf4Bit);
            continue;
        }

        tiledata &t = newtilebuf[i];

        if(t.data)
        {
            zc_free(t.data);
            t.data = NULL;
        }

        t.format = format;
        t.data = (byte *)zc_malloc(bytes);

        if(!t.data)
            return qe_nomem;

        s.read(t.data, bytes);
    }

    if(s.keep)
    {
        for(long i = tiles_used < 0 ? 0 : tiles_used; i < NEWMAXTILES; ++i)
            reset_tile(newtilebuf, (int)i, tf4Bit);

        register_blank_tiles();
    }

    return 0;
}

int q255_read_weapons(Q255Section &s)
{
    if(s.version < 6 || s.version > 9)
        return Q255_NOT_HANDLED;

    word count = s.igetw();

    if(s.failed || count > MAX_SPRITES_255)
        return qe_invalid;

    // v9 moved each sprite's name in front of its data.
    if(s.version < 9)
    {
        for(int i = 0; i < count; ++i)
        {
            if(s.keep && i < WPNCNT)
            {
                s.read(weapon_string[i], 64);

                if(!s.failed)
                    weapon_string[i][63] = 0;
            }
            else
                s.skip(64);

            if(s.failed)
                return qe_invalid;
        }
    }

    for(int i = 0; i < count; ++i)
    {
        wpndata w;
        memset(&w, 0, sizeof(w));

        if(s.version >= 9)
        {
            std::string name = s.getcstr();

            if(s.failed)
                return qe_invalid;

            if(s.keep && i < WPNCNT)
                store_name(weapon_string[i], 64, name);
        }

        // In v7 this word is a leftover; the real tile comes later. v8 dropped it.
        if(s.version < 8)
        {
            word old_tile = s.igetw();

            if(s.version < 7)
                w.tile = old_tile;
        }

        w.misc = s.getc();
        w.csets = s.getc();
        w.frames = s.getc();
        w.speed = s.getc();
        w.type = s.getc();

        if(s.version >= 7)
        {
            w.script = s.igetw();
            w.tile = narrow_tile(s.igetl());
        }

        if(s.failed)
            return qe_invalid;

        if(s.keep && i < MAXWPNS)
            wpnsbuf[i] = w;
    }

    // Upstream starts from an empty list, and a quest may hold fewer sprites than
    // this engine does (3.0 leaves out the blank ones at the end).
    if(s.keep)
    {
        for(int i = count; i < WPNCNT; ++i)
        {
            reset_weaponname(i);

            if(i < MAXWPNS)
                memset(&wpnsbuf[i], 0, sizeof(wpnsbuf[i]));
        }
    }

    return 0;
}

// v9: a count, then one entry per sound (sound number = place in the list + 1).
static int read_sfx_v9(Q255Section &s)
{
    word count = s.igetw();

    if(s.failed)
        return qe_invalid;

    // Upstream starts from an empty list: a sound the quest doesn't hold is silent.
    if(s.keep)
    {
        memset(customsfxflag, 0, sizeof(customsfxflag));
        q30_sfx_clear();

        for(int i = 1; i < WAV_COUNT; ++i)
        {
            default_sfx_name(i);
            clear_sfx_slot(i);
        }
    }

    int ogg_sounds = 0;
    size_t ogg_bytes = 0;

    for(int q = 0; q < count; ++q)
    {
        int id = q + 1;
        bool keep_this = s.keep && id < Q30_MAX_SFX;

        byte type = s.getc();
        std::string name = s.getcstr();

        if(s.failed)
            return qe_invalid;

        if(keep_this && id < WAV_COUNT && !name.empty())
            store_name(sfx_string[id], 36, name);

        if(type == SAMPLE_WAV)
        {
            byte depth = s.getc();
            byte conf = s.getc();
            dword freq = s.igetl();
            dword len = s.igetl();

            if(s.failed)
                return qe_invalid;

            // as al_get_channel_count does
            int channels = (conf >> 4) + (conf & 0xF);
            size_t frame = (size_t)channels * a5_depth_bytes(depth);

            if(!frame || len > s.left() / frame)
            {
                s.failed = true;
                return qe_invalid;
            }

            if(keep_this)
            {
                int ret = store_a5_sample(s, id, depth, channels, freq, len);

                if(ret)
                    return ret;
            }

            s.skip(frame * len);
        }
        else if(type == SAMPLE_OGG)
        {
            dword buffer_size = s.igetl();
            s.igetl(); // frequency
            s.igetl(); // length

            if(!can_read(s, buffer_size))
            {
                s.failed = true;
                return qe_invalid;
            }

            if(keep_this)
            {
                q30_sfx_store_ogg(id, s.data + s.pos, buffer_size);
                ++ogg_sounds;
                ogg_bytes += buffer_size;
            }

            s.skip(buffer_size);
        }

        if(s.failed)
            return qe_invalid;
    }

    if(s.keep)
    {
        sfxdat = 0;
        q255_resolve_high_screen_sfx();
        al_trace("SFX v9: %d sounds, %d of them OGG (%lu KB compressed)\n", (int)count, ogg_sounds, (unsigned long)(ogg_bytes / 1024));
    }

    return 0;
}

int q255_read_sfx(Q255Section &s)
{
    if(s.version < 6 || s.version > 9)
        return Q255_NOT_HANDLED;

    if(s.version >= 9)
        return read_sfx_v9(s);

    if(s.keep)
        q30_sfx_clear();

    byte flags[WAV_COUNT>>3];
    s.read(flags, sizeof(flags));

    if(s.failed)
        return qe_invalid;

    if(s.keep)
        memcpy(customsfxflag, flags, sizeof(flags));

    for(int i = 1; i < WAV_COUNT; ++i)
    {
        if(s.keep)
            default_sfx_name(i);

        if(get_bit(flags, i-1) == 0)
            continue;

        if(s.keep)
        {
            s.read(sfx_string[i], 36);

            if(!s.failed)
                sfx_string[i][35] = 0;
        }
        else
            s.skip(36);

        if(s.failed)
            return qe_invalid;
    }

    for(int i = 1; i < WAV_COUNT; ++i)
    {
        if(get_bit(flags, i-1) == 0)
        {
            if(s.keep)
            {
                int ret = default_sfx_slot(i);

                if(ret)
                    return ret;
            }

            continue;
        }

        int bits = (int)s.igetl();
        int stereo = (int)s.igetl();
        int freq = (int)s.igetl();
        int priority = (int)s.igetl();
        dword len = s.igetl();
        dword loop_start = s.igetl();
        dword loop_end = s.igetl();
        dword param = s.igetl();

        if(s.failed)
            return qe_invalid;

        size_t sample_size = (bits == 8 ? 1 : 2) * (stereo == 0 ? 1 : 2);

        // Bounding len first also keeps the size below from overflowing. The
        // +1: an odd-sized sample's last byte is not in the file.
        if(len > (s.left() + 1) / sample_size)
        {
            s.failed = true;
            return qe_invalid;
        }

        size_t bytes = sample_size * len;
        size_t words = bytes / 2;

        if(!s.keep)
        {
            s.skip(words * 2);
            continue;
        }

        void *data = calloc(bytes, 1);

        if(!data && bytes)
            return qe_nomem;

        for(size_t j = 0; j < words; ++j)
            ((word *)data)[j] = s.igetw();

        if(customsfxdata[i].data != NULL)
            zc_free(customsfxdata[i].data);

        customsfxdata[i].data = data;
        customsfxdata[i].bits = bits;
        customsfxdata[i].stereo = stereo;
        customsfxdata[i].freq = freq;
        customsfxdata[i].priority = priority;
        customsfxdata[i].len = len;
        customsfxdata[i].loop_start = loop_start;
        customsfxdata[i].loop_end = loop_end;
        customsfxdata[i].param = param;
    }

    if(s.failed)
        return qe_invalid;

    if(s.keep)
        sfxdat = 0;

    return 0;
}

// Upstream's read_midi: the divisions, then each track's length and bytes, big-endian.
// With result NULL the bytes are only walked.
static int read_tune_midi(Q255Section &s, MIDI **result)
{
    MIDI *m = NULL;

    if(result)
    {
        m = (MIDI *)_AL_MALLOC(sizeof(MIDI));

        if(!m)
            return qe_nomem;

        for(int c = 0; c < MIDI_TRACKS; ++c)
        {
            m->track[c].len = 0;
            m->track[c].data = NULL;
        }
    }

    int hi = s.getc();
    int lo = s.getc();

    if(m)
        m->divisions = (short)((hi << 8) | lo);

    for(int c = 0; c < MIDI_TRACKS; ++c)
    {
        int len = (int)s.mgetl();

        if(!s.failed && len > 0 && !can_read(s, (dword)len))
            s.failed = true;

        if(s.failed)
        {
            if(m)
                destroy_midi(m);

            return qe_invalid;
        }

        if(m)
            m->track[c].len = len;

        if(len <= 0)
            continue;

        if(!m)
        {
            s.skip((size_t)len);
            continue;
        }

        m->track[c].data = (byte *)_AL_MALLOC(len);

        if(!m->track[c].data)
        {
            destroy_midi(m);
            return qe_nomem;
        }

        s.read(m->track[c].data, (size_t)len);
    }

    if(result)
        *result = m;

    return 0;
}

// Tunes section newer than this engine's own reader. See upstream's qst_midis.cpp.
int q255_read_midis(Q255Section &s)
{
    if(s.keep)
        q30_tunes_clear();

    if(s.version != 5)
        return Q255_NOT_HANDLED;

    std::vector<byte> bits;

    if(!read_bitstr(s, bits))
        return qe_invalid;

    word count = s.igetw();

    if(s.failed || count > MAX_TUNES_255)
        return qe_invalid;

    bool store = s.keep && s.tunes != NULL;

    if(store)
    {
        memset(midi_flags, 0, sizeof(midi_flags));

        for(int i = 0; i < MAXCUSTOMTUNES; ++i)
            s.tunes[i].reset();
    }

    for(int i = 0; i < count; ++i)
    {
        if(!bitstr_get(bits, i))
            continue;

        std::string title = s.getwstr();
        long start = s.igetl_s();
        long loop_start = s.igetl_s();
        long loop_end = s.igetl_s();
        short loop = s.igetw_s();
        short volume = s.igetw_s();
        byte flags = s.getc();
        byte format = s.getc();

        if(s.failed || format != MFORMAT_MIDI)
            return qe_invalid;

        bool keep_this = store && i < MAXCUSTOMTUNES;
        bool keep_high = s.keep && i >= MAXCUSTOMTUNES;
        MIDI *midi = NULL;
        int ret = read_tune_midi(s, (keep_this || keep_high) ? &midi : NULL);

        if(ret)
            return ret;

        if(keep_high)
            q30_store_tune(i, title.c_str(), start, loop_start, loop_end, loop, volume, flags, format, midi);

        if(keep_this)
        {
            zctune &t = s.tunes[i];
            store_name(t.title, sizeof(t.title), title);
            t.start = start;
            t.loop_start = loop_start;
            t.loop_end = loop_end;
            t.loop = loop;
            t.volume = volume;
            t.flags = flags;
            t.format = format;
            t.data = midi;
            set_bit(midi_flags, i, 1);
        }
    }

    return 0;
}

// Sound effects (SFX) and tunes (MIDI) of a quest made in 2.53.1 or older, saved as 2.55.17
// (SFX version 8, MIDI 4) or 3.0 Nightly (SFX 9, MIDI 5). See zq_writers.h.
//
// Sounds. Both versions read an old quest's sounds as 2.53 does, then fill each of the 60
// original sounds the quest doesn't replace with the original sample, flagged as the quest's
// own. 2.55.17 writes them in 2.53's layout, each name as at most 35 characters (all its reader
// keeps) padded with zeros. 3.0 keeps sounds as a list of Allegro 5 samples (signed data, depth
// and channel codes) that starts with the 60 originals whenever the quest has init data, with
// names of up to 36 characters. A quest with no sound section keeps 2.55.17's empty set, and
// gets 3.0's 60 originals.
//
// Tunes: 2.55.17 writes them as 2.53 does; 3.0 as a bit string of the tunes there, their count,
// and each one's title as a counted string.
//
// The sounds' samples and the tunes are ZQuest's (2.53 loads them as both versions do); a sound's
// name is the file's own 36 bytes unless the user renamed it since the load.

#include "precompiled.h" //always first

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "zc_alleg.h"
#include "zdefs.h"
#include "zsys.h"
#include "sfx.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqw_init.h"
#include "zqn_out.h"

extern SAMPLE               customsfxdata[WAV_COUNT];
extern unsigned char        customsfxflag[WAV_COUNT>>3];
extern char                 *sfx_string[WAV_COUNT];
extern DATAFILE             *sfxdata;
extern byte                 midi_flags[MIDIFLAGS_SIZE];
extern zctune               *customtunes;

using zqwe::Buf;
using zqwe::Src;
using zqn::Out;

namespace
{

// The original sounds' names in each version (2.53 says "Link" and the old item names).
const char *const g_names_255[Z35 - 1] =
{
    "Arrow", "Sword beam", "Bomb blast", "Boomerang",  "Subscreen cursor",
    "Shield is hit", "Item chime",  "Roar (Dodongo, Gohma)", "Shutter", "Enemy dies",
    "Enemy is hit", "Low hearts warning", "Fire", "Ganon's fanfare", "Boss is hit", "Hammer",
    "Hookshot", "Message", "Player is hit", "Item fanfare", "Bomb placed", "Item pickup",
    "Refill", "Roar (Aquamentus, Gleeok, Ganon)", "Item pickup 2", "Ocean ambience",
    "Secret chime", "Player dies", "Stairs", "Sword", "Roar (Manhandla, Digdogger, Patra)",
    "Wand magic", "Whistle", "Zelda's fanfare", "Charging weapon", "Charging weapon 2",
    "Divine Fire", "Enemy falls from ceiling", "Divine Escape", "Fireball", "Tall Grass slashed",
    "Pound pounded", "Hover Boots", "Ice magic", "Jump", "Lens of Truth off", "Lens of Truth on",
    "Divine Protection shield", "Divine Protection 2", "Push block", "Rock", "Spell rocket down",
    "Spell rocket up", "Sword spin attack", "Splash", "Summon magic", "Sword tapping",
    "Sword tapping (secret)", "Whistle whirlwind", "Cane of Byrna orbit"
};

const char *const g_names_30[Z35 - 1] =
{
    "Arrow", "Sword beam", "Bomb blast", "Boomerang",  "Subscreen cursor",
    "Shield is hit", "Item chime",  "Roar (Dodongo, Gohma)", "Shutter", "Enemy dies",
    "Enemy is hit", "Low hearts warning", "Fire", "Ganon's fanfare", "Boss is hit", "Hammer",
    "Hookshot", "Message", "Hero is hit", "Item fanfare", "Bomb placed", "Item pickup",
    "Refill", "Roar (Aquamentus, Gleeok, Ganon)", "Item pickup 2", "Ocean ambience",
    "Secret chime", "Hero dies", "Stairs", "Sword", "Roar (Manhandla, Digdogger, Patra)",
    "Wand magic", "Whistle", "Zelda's fanfare", "Charging weapon", "Charging weapon 2",
    "Divine Fire", "Enemy falls from ceiling", "Divine Escape", "Fireball", "Tall Grass slashed",
    "Pound pounded", "Hover Boots", "Ice magic", "Jump", "Lens of Truth off", "Lens of Truth on",
    "Divine Protection shield", "Divine Protection 2", "Push block", "Rock", "Spell rocket down",
    "Spell rocket up", "Sword spin attack", "Splash", "Summon magic", "Sword tapping",
    "Sword tapping (secret)", "Whistle whirlwind", "Cane of Byrna orbit"
};

bool flag_of(const byte *flags, int i)
{
    return (flags[(i - 1) >> 3] >> ((i - 1) & 7)) & 1;
}

void set_flag(byte *flags, int i)
{
    flags[(i - 1) >> 3] |= (byte)(1 << ((i - 1) & 7));
}

std::string c_string(const char *p, size_t n)
{
    return p ? std::string(p, strnlen(p, n)) : std::string();
}

std::string numbered_name(int i)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "s%03d", i);
    return buf;
}

// ZQuest's sound names as loaded, to tell a name the user changed. Comparing with the file's
// own name isn't enough: 2.53's loader strcpy's each name from a 36-byte buffer, so a name of
// all 36 characters runs on into whatever follows on the stack and past its own 36-byte
// buffer, which can overwrite another sound's name as loaded. Upstream keeps that name as the
// file has it.
std::vector<std::string> g_loaded_names;

void snapshot_names()
{
    g_loaded_names.assign(WAV_COUNT, std::string());

    for(int i = 1; i < WAV_COUNT; ++i)
        g_loaded_names[i] = c_string(sfx_string[i], 36);
}

ZqLoadHookRegistration g_names_hook(snapshot_names);

// ZQN_FORCE_ZQUEST=1 (for testing): every name from ZQuest, as if the user had changed them all.
bool name_changed(int i)
{
    const char *force = getenv("ZQN_FORCE_ZQUEST");

    return (force && *force == '1') || (int)g_loaded_names.size() != WAV_COUNT ||
           g_loaded_names[i] != c_string(sfx_string[i], 36);
}

int write_section(PACKFILE *f, dword id, word version, word cversion, const Buf &body)
{
    Buf head;
    head.w(version);
    head.w(cversion);
    head.l((long)body.size());

    if(pack_mputl((long)id, f) == EOF || pack_fwrite(&head.b[0], (long)head.size(), f) != (long)head.size() ||
       (!body.b.empty() && pack_fwrite(&body.b[0], (long)body.size(), f) != (long)body.size()))
    {
        zqw_last_error = "writing the section failed";
        return 1;
    }

    return ZQW_OK;
}

// ============================================================================================
// SFX
// ============================================================================================

// A sound as upstream holds it after the load.
struct Sound
{
    bool there;             // flagged
    const SAMPLE *smp;      // its sample: the quest's, or the original sound's
    bool own_sample;        // the quest's (read from words, so an odd last byte is 0)
    bool has_name;          // a name of its own (from the file, or given in ZQuest)
    char name[36];          // that name's bytes

    Sound() : there(false), smp(NULL), own_sample(false), has_name(false) { memset(name, 0, sizeof(name)); }
};

struct SoundSet
{
    Sound s[WAV_COUNT];
    int wavcount;           // the slots the reader went through
    bool read;              // the quest's sound section was read (or made from ZQuest's sounds)
    bool setup;             // the 60 originals were set up first (3.0's setupsfx with the init data)
    byte flags[WAV_COUNT>>3];   // the flags as read, which the readers keep whole: the last bit
                                // stands for a sound past the last one, and stays as the file has it
};

const SAMPLE *original_sample(int i)
{
    return sfxdata ? (const SAMPLE *)sfxdata[i].dat : NULL;
}

// The sound section as both versions read it (readsfx / readsfx_old), with ZQuest's samples
// and names where the user changed them.
bool sound_set(const ZqNewerContext &ctx, SoundSet &set)
{
    byte fflags[WAV_COUNT>>3];
    memset(fflags, 0, sizeof(fflags));
    std::vector<bool> has_fname(WAV_COUNT, false);
    std::vector<std::vector<byte> > raw(WAV_COUNT);
    int v = 6;
    bool zquest_flags = false;

    for(int b = 0; b < (WAV_COUNT>>3); ++b)
        if(customsfxflag[b])
            zquest_flags = true;

    set.read = ctx.src_part != NULL || zquest_flags;
    set.setup = ctx.src_zelda_version < 0x193 || zq_find_part(ID_INITDATA) != NULL;

    if(ctx.src_part)
    {
        Src s;
        zqwe::Head h;

        if(!zqwe::load_part(*ctx.src_part, s) || !zqwe::read_head(s, 0, h))
        {
            zqw_last_error = "the sounds as loaded couldn't be read again";
            return false;
        }

        v = h.version;
        int wavcount = v < 6 ? 128 : WAV_COUNT;

        if(v < 4)
            memset(fflags, 0xFF, sizeof(fflags));
        else
            for(int b = 0; b < (wavcount >> 3); ++b)
                fflags[b] = (byte)s.c();

        for(int i = 1; v > 4 && i < wavcount && !s.bad; ++i)
            if(flag_of(fflags, i))
            {
                if(!s.has(36))
                    s.bad = true;
                else
                {
                    raw[i].assign(s.at(), s.at() + 36);
                    has_fname[i] = true;
                    s.skip(36);
                }
            }

        if(s.bad)
        {
            zqw_last_error = "the sounds as loaded don't read the way upstream reads them";
            return false;
        }
    }
    else if(zquest_flags)
        memcpy(fflags, customsfxflag, sizeof(fflags));      // as read from a 2.53 save

    set.wavcount = (ctx.src_part && v < 6) ? 128 : WAV_COUNT;

    // The user may have added or removed sounds since the load: ZQuest's flags win.
    if(set.read)
        memcpy(fflags, customsfxflag, sizeof(fflags));

    memcpy(set.flags, fflags, sizeof(fflags));

    // A sound section older than version 5 has no names, and a save in the quest's own version
    // keeps it that way: upstream gives every sound its own default name then.
    bool names_kept = !ctx.src_part || v > 4;

    for(int i = 1; i < WAV_COUNT && set.read; ++i)
    {
        Sound &snd = set.s[i];

        if(i < set.wavcount && flag_of(fflags, i))
        {
            snd.there = true;
            snd.smp = &customsfxdata[i];
            snd.own_sample = true;

            if(has_fname[i] && !name_changed(i))
            {
                memcpy(snd.name, &raw[i][0], 36);
                snd.has_name = true;
            }
            else if(names_kept)
            {
                // named in ZQuest, or a sound added there: a 2.53 save writes ZQuest's name for
                // every sound it has, and the readers keep 36 bytes of it
                std::string zq = c_string(sfx_string[i], 36);
                strncpy(snd.name, zq.c_str(), 36);
                snd.has_name = true;
            }
        }
        else if(i < set.wavcount && i < Z35)
        {
            snd.there = true;
            snd.smp = original_sample(i);
        }
        else if(i >= set.wavcount && flag_of(fflags, i))
        {
            // only a section older than version 4 flags these; their sample is whatever setupsfx
            // left there: the last original's
            snd.there = true;
            snd.smp = original_sample(i < Z35 ? i : Z35);
        }
    }

    return true;
}

size_t sample_bytes(const SAMPLE &s)
{
    return (size_t)(s.bits == 8 ? 1 : 2) * (s.stereo == 0 ? 1 : 2) * s.len;
}

// 2.55.17: the flags, the names (36 bytes each), the samples in 2.53's layout.
void sfx_255(Out &o, const SoundSet &set)
{
    byte flags[WAV_COUNT>>3];
    memcpy(flags, set.flags, sizeof(flags));

    for(int i = 1; i < WAV_COUNT; ++i)
        if(set.s[i].there)
            set_flag(flags, i);

    o.put(flags, sizeof(flags));

    for(int i = 1; i < WAV_COUNT; ++i)
    {
        if(!set.s[i].there)
            continue;

        // the sound's own name (35 characters at most) or the original one, NULs after it
        char buf[37];
        memset(buf, 0, sizeof(buf));

        if(set.s[i].has_name)
            memcpy(buf, set.s[i].name, strnlen(set.s[i].name, 35));
        else if(i < Z35)
            strcpy(buf, g_names_255[i - 1]);
        else
            snprintf(buf, sizeof(buf), "s%03d", i);

        o.put(buf, 36);
    }

    for(int i = 1; i < WAV_COUNT; ++i)
    {
        if(!set.s[i].there)
            continue;

        static const SAMPLE none = { 8, 0, 22050, 128, 0, 0, 0, 0, NULL };
        const SAMPLE &smp = set.s[i].smp ? *set.s[i].smp : none;
        o.l(smp.bits);
        o.l(smp.stereo);
        o.l(smp.freq);
        o.l(smp.priority);
        o.l((long)smp.len);
        o.l((long)smp.loop_start);
        o.l((long)smp.loop_end);
        o.l((long)smp.param);
        size_t words = sample_bytes(smp) / 2;

        if(smp.data)
            o.put(smp.data, words * 2);
        else
            o.zeros(words * 2);
    }
}

// 3.0: ZCSFX::write of an Allegro 5 sample made from an old one (SampleWAV_NoSound).
void zcsfx_30(Out &o, const std::string &name, const SAMPLE *smp, bool own)
{
    if(!smp)
    {
        o.c(0);             // SMPL_INVALID
        o.c((long)(name.size() < 255 ? name.size() : 255));
        o.put(name.data(), name.size() < 255 ? name.size() : 255);
        return;
    }

    o.c(1);                 // SMPL_WAV
    size_t n = name.size() < 255 ? name.size() : 255;
    o.c((long)n);
    o.put(name.data(), n);
    bool b8 = smp->bits == 8;
    o.c(b8 ? 0x00 : 0x01);                          // ALLEGRO_AUDIO_DEPTH_INT8 / INT16
    o.c(smp->stereo ? 0x20 : 0x10);                 // ALLEGRO_CHANNEL_CONF_2 / _1
    o.l(smp->freq);
    o.l((long)smp->len);
    size_t bytes = sample_bytes(*smp);
    const byte *d = (const byte *)smp->data;
    byte chunk[4096];

    // unsigned to signed (16-bit values are little-endian: their high byte is the odd one)
    for(size_t at = 0; at < bytes; at += sizeof(chunk))
    {
        size_t k = bytes - at < sizeof(chunk) ? bytes - at : sizeof(chunk);

        if(d)
            memcpy(chunk, d + at, k);
        else
            memset(chunk, 0, k);

        if(own && at + k == bytes && (bytes & 1))
            chunk[k - 1] = 0;   // a quest's own sample holds whole words only

        for(size_t q = 0; q < k; ++q)
            if(b8 || ((at + q) & 1))
                chunk[q] ^= 0x80;

        o.put(chunk, k);
    }
}

void sfx_30(Out &o, const SoundSet &set)
{
    // quest_sounds: index q-1 holds sound q
    struct Entry
    {
        bool valid;
        std::string name;
        const SAMPLE *smp;
        bool own;
    };

    std::vector<Entry> list;

    if(set.setup)
        for(int q = 1; q < Z35; ++q)
        {
            Entry e;
            e.valid = original_sample(q) != NULL;
            e.name = g_names_30[q - 1];
            e.smp = original_sample(q);
            e.own = false;
            list.push_back(e);
        }

    for(int q = 1; set.read && q < set.wavcount; ++q)
    {
        const Sound &snd = set.s[q];

        if(snd.there)
        {
            Entry e;
            e.valid = snd.smp != NULL;
            e.smp = snd.smp;
            e.own = snd.own_sample;

            if(snd.has_name)
                e.name = c_string(snd.name, 36);
            else
                e.name = q < Z35 ? std::string(g_names_30[q - 1]) : numbered_name(q);

            while((int)list.size() < q)
            {
                Entry blank;
                blank.valid = false;
                blank.smp = NULL;
                blank.own = false;
                list.push_back(blank);
            }

            list[q - 1] = e;
        }
        else if(q - 1 < (int)list.size())
        {
            list[q - 1].valid = false;
            list[q - 1].name.clear();
            list[q - 1].smp = NULL;
        }
    }

    size_t count = list.size();

    while(count > 0 && list[count - 1].name.empty() && !list[count - 1].valid)
        --count;

    o.w((long)count);

    for(size_t k = 0; k < count; ++k)
        zcsfx_30(o, list[k].name, list[k].valid ? list[k].smp : NULL, list[k].own);
}

struct SfxBody
{
    const SoundSet &set;
    int target;

    SfxBody(const SoundSet &s, int t) : set(s), target(t) {}

    bool operator()(Out &o)
    {
        if(target == ZQ_NEWER_30)
            sfx_30(o, set);
        else
            sfx_255(o, set);

        return true;
    }
};

int zqn_sfx(PACKFILE *f, const ZqNewerContext &ctx)
{
    zqn::wg_edit_test_once();

    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    if(!sfxdata)
    {
        zqw_last_error = "the original sounds (sfx.dat) aren't loaded";
        return 1;
    }

    SoundSet *set = new SoundSet();

    if(!sound_set(ctx, *set))
    {
        delete set;
        return 1;
    }

    SfxBody body(*set, ctx.target);
    int rc = ctx.target == ZQ_NEWER_30 ? zqn::write_section(f, ID_SFX, 9, 9, -1, body) :
             zqn::write_section(f, ID_SFX, 8, 5, -1, body);
    delete set;
    return rc;
}

// ============================================================================================
// MIDI
// ============================================================================================

void put_midi(Buf &o, const MIDI *m)
{
    o.c((m->divisions >> 8) & 0xFF);
    o.c(m->divisions & 0xFF);

    for(int c = 0; c < MIDI_TRACKS; ++c)
    {
        dword len = (dword)m->track[c].len;
        o.c((len >> 24) & 0xFF);
        o.c((len >> 16) & 0xFF);
        o.c((len >> 8) & 0xFF);
        o.c(len & 0xFF);

        if(m->track[c].len > 0)
            o.put(m->track[c].data, (size_t)m->track[c].len);
    }
}

int zqn_midis(PACKFILE *f, const ZqNewerContext &ctx)
{
    zqn::wg_edit_test_once();

    if(ctx.src_zelda_version >= 0x255)
        return ZQW_UNSUPPORTED;

    Buf body;

    if(ctx.target == ZQ_NEWER_255)
    {
        // 2.53's layout: the flags, then each flagged tune with a 36-byte title
        for(int i = 0; i < MAXCUSTOMTUNES; ++i)
            if(get_bit(midi_flags, i) && !customtunes[i].data)
            {
                zqw_last_error = "a tune is listed but has no music";
                return 1;
            }

        body.put(midi_flags, MIDIFLAGS_SIZE);

        for(int i = 0; i < MAXCUSTOMTUNES; ++i)
        {
            if(!get_bit(midi_flags, i))
                continue;

            const zctune &t = customtunes[i];
            char title[36];
            memset(title, 0, sizeof(title));
            memcpy(title, t.title, strnlen(t.title, 36));
            body.put(title, 36);
            body.l(t.start);
            body.l(t.loop_start);
            body.l(t.loop_end);
            body.w(t.loop);
            body.w(t.volume);
            body.c(t.flags);
            body.c(t.format);
            put_midi(body, (const MIDI *)t.data);
        }

        return write_section(f, ID_MIDIS, 4, 3, body);
    }

    // 3.0: the tunes there (those with music), as a bit string
    std::vector<byte> bits;
    int count = 0;

    for(int q = 0; q < MAXCUSTOMTUNES; ++q)
        if(customtunes[q].data)
        {
            count = q + 1;

            if(bits.size() < (size_t)(q / 8 + 1))
                bits.resize(q / 8 + 1, 0);

            bits[q / 8] |= (byte)(1 << (q % 8));
        }

    // p_putbvec: the size (65535), then pairs of index and byte or every byte, whichever is shorter
    long nonzero = 0;

    for(size_t k = 0; k < bits.size(); ++k)
        if(bits[k])
            ++nonzero;

    bool pairs = nonzero * 3 <= (long)bits.size();
    body.w(0xFFFF);
    body.c(pairs ? 1 : 0);
    body.w(pairs ? nonzero : (long)bits.size());

    for(size_t k = 0; k < bits.size(); ++k)
    {
        if(pairs)
        {
            if(!bits[k])
                continue;

            body.w((long)k);
        }

        body.c(bits[k]);
    }

    body.w(count);

    for(int q = 0; q < count; ++q)
    {
        const zctune &t = customtunes[q];

        if(!t.data)
            continue;

        size_t n = strnlen(t.title, 36);
        body.w((long)n);
        body.put(t.title, n);
        body.l(t.start);
        body.l(t.loop_start);
        body.l(t.loop_end);
        body.w(t.loop);
        body.w(t.volume);
        body.c(t.flags);
        body.c(0);              // MFORMAT_MIDI: the only format 3.0 keeps
        put_midi(body, (const MIDI *)t.data);
    }

    return write_section(f, ID_MIDIS, 5, 5, body);
}

ZqNewerRegistration g_reg_sfx(ID_SFX, zqn_sfx, "sound effects for 2.55.17 and 3.0");
ZqNewerRegistration g_reg_midis(ID_MIDIS, zqn_midis, "tunes for 2.55.17 and 3.0");

} // namespace

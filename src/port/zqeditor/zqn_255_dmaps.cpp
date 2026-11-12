// Saving a quest made in 2.55 as 3.0 Nightly (zqn_255.h): the DMaps (DMAP), the advanced music
// (AMUS) and the music list both share with the screens.
//
// Music. 3.0 keeps a quest's music as one list; DMaps and screens refer to an entry by number
// (1-based, 0 none). Reading a 2.55 quest it makes the list as it reads: one entry "DMap n" per
// DMap whose MIDI or enhanced music file isn't empty (DMaps before version 25, in DMap order,
// with the file's loop points and crossfades from version 18), then, for each screen with a MIDI
// of its own (maps before version 37), the entry "MIDI m", made once per MIDI, in map and screen
// order (read_one_dmap, readmapscreen; find_or_make_midi_music in advanced_music.cpp).
//
// DMaps. read_one_dmap for versions 14 to 27, then write_one_dmap (version 28): the title as a
// string, the disabled items as a bitstring, the four scripts as script configs (the passive
// subscreen's arguments are the active subscreen's before version 27), no mirror DMap before 16,
// no overlay subscreen before 19, no intro string before 20, no map subscreen before 24, the music
// as a list number. An overworld DMap's map X offset is 0. Each DMap's subscreen numbers as 3.0
// holds them after the init data and subscreens load (zqn255_dmap_subscreens); DMaps up to the
// last one count_dmaps finds.
//
// DMaps version 21 also holds region ids per map, which 3.0 keeps for the map section
// (zqn255_dmap_regions).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_writers.h"
#include "zq_tape.h"
#include "zqn_255.h"

namespace
{

const dword ID_ADVMUSIC_ = ZC_ID('A', 'M', 'U', 'S');
const int NUM_DMAPS = 512, SCREENS_PER_MAP = 136;
const int MIDI_OVERWORLD = -5, MIDI_LEVEL9 = -4, MIDI_GAMEOVER = -3, MIDI_DUNGEON = -1;

// convert_from_old_midi_id (advanced_music.cpp)
int from_old_midi(int midi, bool for_screen)
{
    switch(midi)
    {
    case -3: return 0;
    case -4: return for_screen ? -6 : MIDI_GAMEOVER;    // MIDI_SCREEN_INHERIT: can't come from a positive MIDI
    case -2: return MIDI_OVERWORLD;
    case -1: return MIDI_DUNGEON;
    case 0: return MIDI_LEVEL9;
    default: return midi < 0 ? 0 : midi;
    }
}

int find_or_make_midi(std::vector<Zqn255MusicEntry> &music, int midi)
{
    if(midi == 0)
        return 0;

    char name[32];
    snprintf(name, sizeof(name), "MIDI %d", midi);

    for(size_t q = 0; q < music.size(); ++q)
        if(music[q].path.empty() && music[q].midi == midi && music[q].name == name)
            return (int)q + 1;

    if(music.size() >= 65535)
        return 0;

    Zqn255MusicEntry m;
    m.name = name;
    m.midi = (short)midi;
    music.push_back(m);
    return (int)music.size();
}

bool section_of(int target, dword id, Zqn255Section &s)
{
    ZqNewerContext c;
    c.target = target;
    c.src_zelda_version = 0x255;
    c.src_build = 0;
    c.src_part = zq_find_part(id);
    c.src_version = c.src_cversion = 0;

    if(!c.src_part)
        return false;

    zq_part_versions(*c.src_part, &c.src_version, &c.src_cversion);
    return zqn255_section(c, s);
}

std::string c_text(const TapeField *f)
{
    if(!f)
        return std::string();

    size_t n = 0;

    while(n < f->bytes.size() && f->bytes[n])
        ++n;

    return std::string((const char *)(f->bytes.empty() ? NULL : &f->bytes[0]), n);
}

// Every DMap record of the quest's own DMAP section, as tapes.
bool dmap_tapes(int target, int *version, std::vector<Tape> &tapes)
{
    Zqn255Section s;

    if(!section_of(target, ID_DMAPS, s))
        return false;

    *version = s.version;
    Zqn255In r(s.data);
    const int count = r.u16();
    size_t at = 2;
    tapes.assign(count < NUM_DMAPS ? count : NUM_DMAPS, Tape());

    for(int i = 0; i < count; ++i)
    {
        Tape t;
        TapeParser p(s.data.empty() ? NULL : &s.data[0], s.data.size(), at, &t);

        if(!zq_tape_dmap(p, s.version) || p.bad)
            return false;

        at = p.pos;

        if(i < NUM_DMAPS)
            tapes[i].f.swap(t.f);
    }

    return at == s.data.size();
}

} // namespace

bool zqn255_music_list(int target, Zqn255MusicList &out)
{
    out.music.clear();
    out.dmap_music.assign(NUM_DMAPS, 0);
    out.screen_music.clear();
    int dv = 0;
    std::vector<Tape> dmaps;

    if(!dmap_tapes(target, &dv, dmaps))
        return false;

    for(size_t i = 0; i < dmaps.size(); ++i)
    {
        const TapeIndex t(dmaps[i]);

        if(dv >= 25)
        {
            out.dmap_music[i] = (word)t.get("music");
            continue;
        }

        Zqn255MusicEntry m;
        m.midi = (short)from_old_midi((int)t.get("midi") - 3, false);
        m.path = c_text(t.find("tmusic"));
        m.track = (byte)t.get("tmusictrack");
        m.loop_start = t.get("loop_start");
        m.loop_end = t.get("loop_end");
        m.xfade_in = t.get("xfade_in");
        m.xfade_out = t.get("xfade_out");

        if(m.path.empty() && !m.midi)
            continue;

        char name[32];
        snprintf(name, sizeof(name), "DMap %d", (int)i);
        m.name = name;
        out.music.push_back(m);
        out.dmap_music[i] = (word)out.music.size();
    }

    // the screens (readmapscreen): a MIDI of their own before map version 37
    Zqn255Section ms;

    if(!section_of(target, ID_MAPS, ms))
        return true;

    const int v = ms.version;
    Zqn255In r(ms.data);
    const int maps = r.u16();
    size_t at = 2;

    for(int i = 0; i < maps; ++i)
    {
        Zqn255In h(ms.data, at);
        bool valid = true;

        if(v > 22)
            valid = h.u8() != 0;

        if(valid)
        {
            if(v > 25)
                h.skip(12 + (v >= 36 ? 2 : 0));

            if(v >= 31)
                h.skip(64);
        }

        if(h.bad)
            return false;

        at = h.pos;

        for(int j = 0; valid && j < SCREENS_PER_MAP; ++j)
        {
            Tape t;
            TapeParser p(&ms.data[0], ms.data.size(), at, &t);

            if(!zq_tape_screen(p, v) || p.bad)
                return false;

            at = p.pos;
            long m = -1;

            if(v < 37)
            {
                const TapeField *f = t.find("screen_midi");

                if(f)
                {
                    const long sm = (short)f->value;
                    m = sm <= 0 ? (sm < -1 ? -1 : sm) : find_or_make_midi(out.music, from_old_midi((int)sm - 3, true));
                }
            }
            else if(const TapeField *f = t.find("music"))
                m = (long)f->value;

            out.screen_music.push_back(m);
        }

        if(!valid)
            out.screen_music.insert(out.screen_music.end(), SCREENS_PER_MAP, -1);
    }

    return true;
}

bool zqn255_dmap_regions(int target, std::vector<std::vector<byte> > &regions)
{
    regions.clear();
    int dv = 0;
    std::vector<Tape> dmaps;

    if(!dmap_tapes(target, &dv, dmaps))
        return false;

    if(dv != 21)
        return true;

    for(size_t i = 0; i < dmaps.size(); ++i)
    {
        const TapeIndex t(dmaps[i]);
        const TapeField *rf = t.find("regions");
        const size_t map = (size_t)t.get("map");

        if(!rf || rf->bytes.size() != 64)
            continue;

        if(regions.size() <= map)
            regions.resize(map + 1);

        regions[map] = rf->bytes;
    }

    return true;
}

namespace
{

void put_str16(Zqn255Out &o, const std::string &s)
{
    size_t n = s.size() < 65535 ? s.size() : 65535;
    o.w((int64_t)n);
    o.put(s.data(), n);
}

void put_config(Zqn255Out &o, word script, const long *args)
{
    o.w(script);
    o.w(8);

    for(int q = 0; q < 8; ++q)
        o.l(args[q]);

    o.w(0);
}

int write_amus(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || ctx.target != ZQ_NEWER_30 || ctx.src_part)
        return ZQW_UNSUPPORTED;     // a 2.55 quest has no music objects of its own

    Zqn255MusicList ml;

    if(!zqn255_music_list(ctx.target, ml))
    {
        zqw_last_error = "its DMaps or screens (their music) aren't laid out as expected";
        return 2;
    }

    Zqn255Out o;
    o.w((int64_t)ml.music.size());

    for(size_t i = 0; i < ml.music.size(); ++i)
    {
        const Zqn255MusicEntry &m = ml.music[i];
        put_str16(o, m.name);
        const size_t n = m.path.size() < 255 ? m.path.size() : 255;
        o.c((int64_t)n);
        o.put(m.path.data(), n);

        if(!m.path.empty())
        {
            o.l(m.loop_start);
            o.l(m.loop_end);
            o.l(m.xfade_in);
            o.l(m.xfade_out);
            o.c(m.track);
        }

        o.w(m.midi);
    }

    std::vector<byte> none;
    return zqn255_emit(f, ID_ADVMUSIC_, 1, 1, none, o.b);
}

struct Dmap
{
    long map, level, xoff, compass, color, cont, type;
    byte grid[8];
    std::vector<byte> name, intro;
    std::string title;
    long tile[4], cset[4];
    long active, passive, overlay, map_subscreen, floor;
    std::vector<byte> disabled;     // the stored bitstring
    long flags, sideview;
    word script[4];
    long args[4][8];
    long mirror, intro_string, gravity, terminal_v, music;
    long vf[4], vf_speed, vf_recenter, vf_delay;
};

bool any_of(const Dmap &d)
{
    if(d.map || d.level || d.xoff || d.compass || d.color || d.music || d.cont || d.type)
        return true;

    for(int j = 0; j < 8; ++j)
        if(d.grid[j])
            return true;

    if((!d.name.empty() && d.name[0]) || !d.title.empty() || (!d.intro.empty() && d.intro[0]))
        return true;

    for(int t = 0; t < 4; ++t)
        if(d.tile[t] || d.cset[t])
            return true;

    return false;
}

int write_dmaps(PACKFILE *f, const ZqNewerContext &ctx)
{
    if(!zqn255_source(ctx) || !ctx.src_part || ctx.target != ZQ_NEWER_30 || ctx.src_version < 14 || ctx.src_version > 27)
        return ZQW_UNSUPPORTED;

    int v = 0;
    std::vector<Tape> tapes;

    if(!dmap_tapes(ctx.target, &v, tapes))
    {
        zqw_last_error = "its DMaps aren't laid out as expected";
        return 2;
    }

    Zqn255MusicList ml;

    if(v < 25 && !zqn255_music_list(ctx.target, ml))
    {
        zqw_last_error = "its DMaps or screens (their music) aren't laid out as expected";
        return 2;
    }

    std::vector<Dmap> dm(NUM_DMAPS);
    std::vector<Zqn255DmapSubs> subs(NUM_DMAPS);

    for(int i = 0; i < NUM_DMAPS; ++i)
    {
        Dmap &d = dm[i];
        memset(d.grid, 0, sizeof(d.grid));
        d.map = d.level = d.xoff = d.compass = d.color = d.cont = d.type = 0;
        d.active = d.passive = d.overlay = d.floor = 0;
        d.map_subscreen = -1;
        d.flags = d.sideview = 0;
        memset(d.script, 0, sizeof(d.script));
        memset(d.args, 0, sizeof(d.args));
        d.mirror = -1;
        d.intro_string = d.gravity = d.terminal_v = d.music = 0;
        d.tile[0] = d.tile[1] = d.tile[2] = d.tile[3] = 0;
        d.cset[0] = d.cset[1] = d.cset[2] = d.cset[3] = 0;
        d.vf[0] = d.vf[1] = d.vf[2] = d.vf[3] = 0;
        d.vf_speed = 10000;
        d.vf_recenter = 0;
        d.vf_delay = 30;
        d.name.assign(21, 0);
        d.intro.assign(73, 0);
        d.disabled = zq_bitstr_store(std::vector<byte>());

        if(i >= (int)tapes.size())
        {
            subs[i].type = 0;
            subs[i].active = subs[i].passive = subs[i].overlay = 0;
            subs[i].map = -1;
            continue;
        }

        const TapeIndex t(tapes[i]);
#define T(k) t.get(k)
        d.map = T("map");
        d.level = T("level");
        d.xoff = (signed char)T("xoff");
        d.compass = T("compass");
        d.color = T("color");
        d.cont = T("cont");
        d.type = T("type");

        if((d.type & 0x7F) == 1)    // dmOVERW
            d.xoff = 0;

        for(int q = 0; q < 8; ++q)
        {
            char k[16];
            snprintf(k, sizeof(k), "grid%d", q);
            d.grid[q] = (byte)T(k);
        }

        if(const TapeField *nf = t.find("name"))
            d.name = nf->bytes;

        d.title = c_text(t.find("title"));

        if(const TapeField *inf = t.find("intro"))
            d.intro = inf->bytes;

        static const char *const tk[4] = { "minimap1", "minimap2", "largemap1", "largemap2" };

        for(int q = 0; q < 4; ++q)
        {
            char k[32];
            snprintf(k, sizeof(k), "%s_tile", tk[q]);
            d.tile[q] = T(k);
            snprintf(k, sizeof(k), "%s_cset", tk[q]);
            d.cset[q] = T(k);
        }

        d.active = T("active_subscreen");
        d.passive = T("passive_subscreen");

        if(const TapeField *df = t.find("disabled"))
        {
            if(v >= 26)
                d.disabled = df->bytes;
            else
            {
                // 32 bytes of item bits, as a bitstring
                std::vector<byte> vals(df->bytes.begin(), df->bytes.end());
                d.disabled = zq_bitstr_store(vals);
            }
        }

        d.flags = T("flags");
        d.sideview = v >= 10 ? T("sideview") : 0;

        if(v >= 27)
        {
            zqw_last_error = "its DMaps hold script configurations (version 27, not converted yet)";
            return 2;
        }

        if(v >= 12)
        {
            d.script[0] = (word)T("script");

            for(int q = 0; q < 8; ++q)
            {
                char k[16];
                snprintf(k, sizeof(k), "initd%d", q);
                d.args[0][q] = T(k);
            }
        }

        if(v >= 14)
        {
            d.script[1] = (word)T("asub_script");
            d.script[2] = (word)T("psub_script");

            for(int s = 1; s <= 2; ++s)
                if(d.script[s] >= 256)  // NUMSCRIPTSDMAP
                    d.script[s] = 0;

            for(int q = 0; q < 8; ++q)
            {
                char k[16];
                snprintf(k, sizeof(k), "sub_initd%d", q);
                d.args[1][q] = d.args[2][q] = T(k);
            }
        }

        if(v >= 15)
        {
            d.script[3] = (word)T("onmap_script");

            for(int q = 0; q < 8; ++q)
            {
                char k[24];
                snprintf(k, sizeof(k), "onmap_initd%d", q);
                d.args[3][q] = T(k);
            }
        }

        d.mirror = v >= 16 ? (short)T("mirror") : -1;
        d.overlay = v >= 19 ? T("overlay_subscreen") : 0;
        d.intro_string = v >= 20 ? T("intro_string") : 0;

        if(v > 22 && (d.flags & 0x800000))     // dmfCUSTOM_GRAVITY
        {
            d.gravity = T("gravity");
            d.terminal_v = T("terminal_v");
        }

        if(v > 23)
        {
            d.map_subscreen = (short)T("map_subscreen");
            d.floor = T("floor");
        }

        d.music = v >= 25 ? T("music") : ml.dmap_music[i];
#undef T
        subs[i].type = (int)d.type;
        subs[i].active = (int)d.active;
        subs[i].passive = (int)d.passive;
        subs[i].overlay = (int)d.overlay;
        subs[i].map = (int)d.map_subscreen;
    }

    if(!zqn255_dmap_subscreens(ctx.target, subs))
    {
        zqw_last_error = "its DMaps' subscreens couldn't be worked out";
        return 2;
    }

    for(int i = 0; i < NUM_DMAPS; ++i)
    {
        dm[i].active = subs[i].active;
        dm[i].passive = subs[i].passive;
        dm[i].overlay = subs[i].overlay;
        dm[i].map_subscreen = subs[i].map;
    }

    int count = 0;

    for(int i = NUM_DMAPS - 1; i >= 0 && !count; --i)
        if(any_of(dm[i]))
            count = i + 1;

    Zqn255Out o;
    o.w(count);

    for(int i = 0; i < count; ++i)
    {
        const Dmap &d = dm[i];
        o.c(d.map);
        o.w(d.level);
        o.c(d.xoff);
        o.c(d.compass);
        o.w(d.color);
        o.c(d.cont);
        o.c(d.type);
        o.put(d.grid, 8);
        o.put(d.name);
        put_str16(o, d.title);
        o.put(d.intro);

        for(int q = 0; q < 4; ++q)
        {
            o.l(d.tile[q]);
            o.c(d.cset[q]);
        }

        o.c(d.active);
        o.c(d.passive);
        o.put(d.disabled);
        o.l(d.flags);
        o.c(d.sideview);

        for(int q = 0; q < 4; ++q)
            put_config(o, d.script[q], d.args[q]);

        o.w(d.mirror);
        o.c(d.overlay);
        o.l(d.intro_string);

        if(d.flags & 0x800000)
        {
            o.l(d.gravity);
            o.l(d.terminal_v);
        }

        if(d.flags & 0x1000000)    // dmfCUSTOM_VIEWPORT_FOLLOW
        {
            for(int q = 0; q < 4; ++q)
                o.c(d.vf[q]);

            o.l(d.vf_speed);
            o.l(d.vf_recenter);
            o.w(d.vf_delay);
        }

        o.w(d.map_subscreen);
        o.c(d.floor);
        o.w(d.music);
    }

    std::vector<byte> none;
    return zqn255_emit(f, ID_DMAPS, 28, 28, none, o.b);
}

ZqNewerRegistration g_reg_amus(ID_ADVMUSIC_, write_amus, "2.55 music as 3.0's advanced music");
ZqNewerRegistration g_reg_dmaps(ID_DMAPS, write_dmaps, "2.55 DMaps as 3.0");

} // namespace

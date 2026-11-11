// See zqn_music.h. Ported from 3.0 Nightly's core/qst_dmaps.cpp (read_one_dmap, readdmaps),
// core/qst_maps.cpp (readmapscreen_old, readmaps) and advanced_music.cpp
// (convert_from_old_midi_id, find_or_make_midi_music).
//
// A DMap or screen whose music the user changed in ZQuest since the load is read as 3.0 reads
// it from a 2.53.1 quest; the others as 3.0 reads the original file (which drops some old
// versions' DMap music and every screen MIDI before maps section version 5).

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zsys.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zqn_music.h"

extern dmap *DMaps;
extern std::vector<mapscr> TheMaps;
extern word map_count;

namespace zqnm
{

namespace
{

// ---- what was loaded, to tell what the user changed --------------------------------------

struct DmapMusic
{
    byte midi;
    char tmusic[56];
    byte track;
};

std::vector<DmapMusic> g_dmaps;
std::vector<short> g_screens;

DmapMusic dmap_music_of(const dmap &d)
{
    DmapMusic m;
    m.midi = d.midi;
    memcpy(m.tmusic, d.tmusic, sizeof(m.tmusic));
    m.track = d.tmusictrack;
    return m;
}

void note_loaded()
{
    g_dmaps.resize(MAXDMAPS);

    for(int i = 0; i < MAXDMAPS; ++i)
        g_dmaps[i] = dmap_music_of(DMaps[i]);

    g_screens.resize(TheMaps.size());

    for(size_t i = 0; i < TheMaps.size(); ++i)
        g_screens[i] = TheMaps[i].screen_midi;
}

ZqLoadHookRegistration g_load_hook(note_loaded);

bool dmap_changed(int i)
{
    if(i >= (int)g_dmaps.size())
        return true;

    DmapMusic now = dmap_music_of(DMaps[i]);
    const DmapMusic &was = g_dmaps[i];
    return now.midi != was.midi || now.track != was.track || memcmp(now.tmusic, was.tmusic, sizeof(now.tmusic));
}

bool screen_changed(size_t i)
{
    return i >= g_screens.size() || g_screens[i] != TheMaps[i].screen_midi;
}

// ---- 3.0's conversions ------------------------------------------------------------------

enum
{
    BUILTIN_MIDI_GAMEOVER = -3, BUILTIN_MIDI_LEVEL9 = -4, BUILTIN_MIDI_OVERWORLD = -5, BUILTIN_MIDI_DUNGEON = -1
};

// 2.53's DMap and screen MIDI numbers count three built-in tunes before the quest's own:
// (MIDIOFFSET_DMAP - MIDIOFFSET_ZSCRIPT) is -3 in 3.0
const int OLD_MIDI_OFFSET = -3;

int convert_from_old_midi_id(int midi)
{
    switch(midi)
    {
    case -3:
        return 0;
    case -4:
        return BUILTIN_MIDI_GAMEOVER;   // a screen's -4 would be "the DMap's"; it can't occur here
    case -2:
        return BUILTIN_MIDI_OVERWORLD;
    case -1:
        return BUILTIN_MIDI_DUNGEON;
    case 0:
        return BUILTIN_MIDI_LEVEL9;
    default:
        return midi < 0 ? 0 : midi;
    }
}

int find_or_make_midi_music(std::vector<ZqnMusic> &music, int midi)
{
    if(midi == 0)
        return 0;

    char name[32];
    sprintf(name, "MIDI %d", midi);

    for(size_t q = 0; q < music.size(); ++q)
        if(music[q].path.empty() && music[q].midi == midi && music[q].name == name)
            return (int)q + 1;

    if(music.size() >= 65535)
        return 0;

    ZqnMusic m;
    m.name = name;
    m.track = 0;
    m.midi = (short)midi;
    music.push_back(m);
    return (int)music.size();
}

word rd16(const byte *p)
{
    return (word)(p[0] | (p[1] << 8));
}

// How many DMaps upstream reads: the DMaps section's count, or pre-1.93's fixed numbers.
int dmaps_read(word zv, byte build)
{
    if(zv > 0x192)
    {
        const ZqQuestPart *p = zq_find_part(ID_DMAPS);
        byte b[10];

        if(!p || !zq_part_read(*p, 0, b, 10))
            return 0;

        int n = rd16(b + 8);
        return n < MAXDMAPS ? n : MAXDMAPS;
    }

    if(zv < 0x192 || (zv == 0x192 && build < 5))
        return 32;

    return OLDMAXDMAPS;
}

} // namespace

bool quest_music(ZqnQuestMusic &out)
{
    word zv = zq_quest_file.zelda_version;
    byte build = zq_quest_file.build;

    if(zv >= 0x255)
        return false;

    out.music.clear();
    out.dmap_music.assign(MAXDMAPS, 0);
    out.screen_music.assign((size_t)map_count * MAPSCRS, -1);

    int dmap_v = -1, map_v = -1;
    const ZqQuestPart *p;
    word v, cv;

    if((p = zq_find_part(ID_DMAPS)) != NULL && zq_part_versions(*p, &v, &cv))
        dmap_v = v;

    if((p = zq_find_part(ID_MAPS)) != NULL && zq_part_versions(*p, &v, &cv))
        map_v = v;

    // read_one_dmap: each DMap's MIDI byte, and from 1.92 beta 152 its enhanced music file and
    // (from DMaps section version 2) track; 1.92 betas 41 to 151 return before the music is made
    int ndmaps = dmaps_read(zv, build);
    bool old_title = zv < 0x192 || (zv == 0x192 && build < 41);
    bool returns_early = !old_title && zv == 0x192 && build < 152;
    int s_version = zv > 0x192 ? dmap_v : 0;

    for(int i = 0; i < ndmaps; ++i)
    {
        const dmap &d = DMaps[i];
        bool as_253 = dmap_changed(i);

        if(returns_early && !as_253)
            continue;

        ZqnMusic m;
        m.midi = (short)convert_from_old_midi_id(d.midi + OLD_MIDI_OFFSET);
        m.track = 0;

        if(as_253 || !old_title)
            m.path.assign(d.tmusic, strnlen(d.tmusic, sizeof(d.tmusic)));

        if(as_253 || s_version > 1)
            m.track = d.tmusictrack;

        if(m.path.empty() && !m.midi)
            continue;

        char name[32];
        sprintf(name, "DMap %d", i);
        m.name = name;
        out.music.push_back(m);
        out.dmap_music[i] = (word)out.music.size();
    }

    // readmapscreen_old: a screen's MIDI, from maps section version 5
    if(zv > 0x192)
    {
        size_t n = out.screen_music.size() < TheMaps.size() ? out.screen_music.size() : TheMaps.size();

        for(size_t i = 0; i < n; ++i)
        {
            if(map_v <= 4 && !screen_changed(i))
                continue;

            int m = TheMaps[i].screen_midi;

            if(m <= 0)
                out.screen_music[i] = (short)(m < 0 ? -1 : 0);
            else
                out.screen_music[i] = (short)find_or_make_midi_music(out.music, convert_from_old_midi_id(m + OLD_MIDI_OFFSET));
        }
    }

    return true;
}

} // namespace zqnm

bool zqn_quest_music(ZqnQuestMusic &out)
{
    return zqnm::quest_music(out);
}

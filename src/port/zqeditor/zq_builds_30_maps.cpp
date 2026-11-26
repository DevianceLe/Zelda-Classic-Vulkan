// The 3.0 down-steps for the maps' region ids and the palettes (zq_builds_30.cpp's down_section
// calls these). Layouts from upstream's src/base/qst.cpp at the tags named:
//
// - MAP 32 -> 30 (pr.89) and DMAP 22 -> 21 (pr.89): from map format 31 each valid map holds its
//   region ids (8x8 bytes, after its auto layers); before, each DMap holds them (DMAP 21, after
//   its intro string), and the loader gives a map the ids of its last DMap (Regions[dmap.map]).
//   Down from 31 the maps leave them out (map_records) and every DMap of a map takes that map's.
// - MAP 30 -> 29 (pr.25): the screens' notes go (zq_tape_screen's v > 29).
// - CSET 6 -> 5 (pr.59): 6 holds the colours as 8 bits; 5 and older as 6 bits, which the loader
//   scales up (s_version < 6). The rest of the section (names, cycles) is alike.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "zdefs.h"
#include "colors.h"
#include "zq_tape.h"

namespace
{

std::vector<std::vector<byte> > g_regions;  // each map's region ids, from the maps section
bool g_maps_seen = false;
const std::vector<byte> *g_map_data = NULL; // the save's maps section (the DMaps come first)
int g_map_v = 0;
void (*g_dmap_next)(Tape &, int) = NULL;

std::string name_of(dword id)
{
    char t[5] = { (char)(id >> 24), (char)(id >> 16), (char)(id >> 8), (char)id, 0 };
    std::string s = t;

    while(!s.empty() && s[s.size() - 1] == ' ')
        s.erase(s.size() - 1);

    return s;
}

bool refuse(std::string &why, dword id, int from, int to, const char *what)
{
    char b[256];
    snprintf(b, sizeof(b), "%s (format %d to %d): %s", name_of(id).c_str(), from, to, what);
    why = b;
    return false;
}

// The maps section at version v: a count, each map's valid byte, then for a valid map its auto
// layers (v > 25), auto palette (v >= 36), region ids (v >= 31) and 136 screens.
bool read_regions(const std::vector<byte> &d, int v, std::string &what)
{
    g_regions.clear();

    if(d.size() < 2)
    {
        what = "it is too short";
        return false;
    }

    const int maps = d[0] | (d[1] << 8);
    size_t at = 2;

    for(int m = 0; m < maps; ++m)
    {
        std::vector<byte> ids(64, 0);

        if(at >= d.size())
        {
            what = "it is too short";
            return false;
        }

        const byte valid = d[at++];

        if(valid)
        {
            at += (v > 25 ? 12 : 0) + (v >= 36 ? 2 : 0);

            if(at + 64 > d.size())
            {
                what = "it is too short";
                return false;
            }

            memcpy(&ids[0], &d[at], 64);
            at += 64;

            for(int j = 0; j < 136; ++j)
            {
                Tape t;
                TapeParser p(&d[0], d.size(), at, &t);

                if(!zq_tape_screen(p, v) || p.bad)
                {
                    what = "a screen doesn't read as its format says";
                    return false;
                }

                at = p.pos;
            }
        }

        g_regions.push_back(ids);
    }

    if(at != d.size())
    {
        what = "it has bytes past its last map";
        return false;
    }

    return true;
}

// a DMap laid out as 21: its map's region ids
void dmap_regions(Tape &t, int rec)
{
    if(g_dmap_next)
        g_dmap_next(t, rec);

    const int64_t map = t.get("map", 0);
    std::vector<byte> ids(64, 0);

    if(map >= 0 && (size_t)map < g_regions.size())
        ids = g_regions[(size_t)map];

    TapeField *f = t.find("regions");

    if(!f)
    {
        TapeField x;
        x.key = "regions";
        x.group = "Data";
        x.label = "region ids (unused)";
        x.kind = TK_BLOB;
        x.width = 64;
        t.f.push_back(x);
        f = &t.f.back();
    }

    f->bytes = ids;
}

} // namespace

// The save's sections as the newest format holds them, before any goes down: the maps' are kept
// (by address: the save's sections stay put until it is written) for the DMaps, which come first.
void zq30_maps_read(dword id, word v, const std::vector<byte> &data)
{
    if(id == ID_HEADER)
    {
        g_regions.clear();
        g_maps_seen = false;
        g_map_data = NULL;
    }

    if(id == ID_MAPS)
    {
        g_map_data = &data;
        g_map_v = v;
    }
}

// Before a section goes down from version v to the build's: the palettes are done here (v becomes
// to); the maps' region ids are kept for the DMaps. False (why set) stops the section.
bool zq30_maps_step(dword id, word &v, int to, std::vector<byte> &data, std::string &why)
{
    if(to >= v)
        return true;

    // the maps' region ids, read once a step needs them (before the maps lose them)
    if((id == ID_MAPS && v >= 31 && to < 31) || (id == ID_DMAPS && v >= 22 && to < 22))
    {
        std::string what = "the quest has no maps section";

        if(!g_maps_seen && g_map_data && g_map_v >= 31)
            g_maps_seen = read_regions(*g_map_data, g_map_v, what);

        if(!g_maps_seen)
            return refuse(why, id, v, to, ("its maps' region ids couldn't be read: " + what).c_str());

        if(id == ID_DMAPS && to != 21)
            return refuse(why, id, v, to, "DMap formats before 21 aren't written yet");
    }

    if(id == ID_CSETS && v == 6 && to == 5)
    {
        // the colours: every CSet of 3.0's palettes (pdTOTAL255), 16 colours of 3 bytes
        const size_t colours = (size_t)(pdFULL + 17 * 512 + 30) * 48;

        if(data.size() < colours)
            return refuse(why, id, v, to, "it is too short");

        for(size_t i = 0; i < colours; ++i)
            data[i] = (byte)((data[i] * 63 + 127) / 255);

        v = (word)to;
    }

    return true;
}

// The DMaps' last change before they are laid out as 21: their maps' region ids (then next's).
void (*zq30_dmap_prep(void (*next)(Tape &, int), int from, int to))(Tape &, int)
{
    if(from < 22 || to >= 22)
        return next;

    g_dmap_next = next;
    return dmap_regions;
}

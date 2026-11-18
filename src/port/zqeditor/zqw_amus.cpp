// The music objects of 3.0 Nightly quests (the AMUS section): written back in the quest's own
// AMUS version when the music editor (zq_music_editor.cpp) changed them, and the DMaps' and
// screens' references to them moved along when entries are deleted or reordered, as upstream's
// update_quest_music (advanced_music.cpp) does.
//
// write_adv_music (upstream's zq_class.cpp), version 1: a word count, then each entry's name (a
// word count and characters), its enhanced music file (a byte count and characters) and, when
// that isn't empty, the loop start and end, the crossfades in and out (four longs) and the
// track (a byte), then its MIDI (a signed word: 1 and up the quest's tunes, -1 to -7 the
// built-in ones, 0 none). The store keeps each entry as read, so unedited ones write back
// byte for byte.

#include "precompiled.h" //always first

#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

#include "zdefs.h"
#include "zq_sections.h"
#include "zq_writers.h"
#include "zq_q255_store.h"
#include "zq_tape.h"

extern zquestheader header;

bool zq255_dmap_record(int i, std::vector<byte> &rec, int *version);         // zqw_maps.cpp
bool zq255_dmap_commit(int i, const std::vector<byte> &rec, std::string &why);
bool zq255_screen_record(int index, std::vector<byte> &rec, int *version);
void zq255_screen_fields_sync(int index, bool music, bool lens);

namespace
{

const dword ID_ADVMUSIC_ = ZC_ID('A', 'M', 'U', 'S');

void put8(std::vector<byte> &o, unsigned v) { o.push_back((byte)v); }
void put16(std::vector<byte> &o, unsigned v) { o.push_back((byte)v); o.push_back((byte)(v >> 8)); }
void put32(std::vector<byte> &o, unsigned v) { put16(o, v & 0xFFFF); put16(o, v >> 16); }

// The section's data in version 1, from the store.
void encode(std::vector<byte> &o)
{
    const std::vector<Zq255Store::Music> &m = zq255.music;
    put16(o, (unsigned)m.size());

    for(size_t q = 0; q < m.size(); ++q)
    {
        const size_t nn = m[q].name.size() < 65535 ? m[q].name.size() : 65535;
        put16(o, (unsigned)nn);
        o.insert(o.end(), m[q].name.begin(), m[q].name.begin() + nn);
        const size_t pn = m[q].path.size() < 255 ? m[q].path.size() : 255;
        put8(o, (unsigned)pn);
        o.insert(o.end(), m[q].path.begin(), m[q].path.begin() + pn);

        if(pn)
        {
            put32(o, (unsigned)m[q].loop_start);
            put32(o, (unsigned)m[q].loop_end);
            put32(o, (unsigned)m[q].xfade_in);
            put32(o, (unsigned)m[q].xfade_out);
            put8(o, m[q].track);
        }

        put16(o, (unsigned)(word)m[q].midi);
    }
}

bool has_amus_part()
{
    for(size_t i = 0; i < zq_quest_file.parts.size(); ++i)
        if(zq_quest_file.parts[i].id == ID_ADVMUSIC_)
            return true;

    return false;
}

// Only a quest whose file has the section: elsewhere zq255.music is what the DMap reader made
// of each DMap's own music, which the DMaps themselves keep.
void hash_music(uint64_t *h)
{
    if(!has_amus_part())
        return;

    std::vector<byte> o;
    encode(o);
    zq_hash_add(h, &o[0], o.size());
}

ZqStoreHashRegistration g_hash(ID_ADVMUSIC_, hash_music);

int write_amus_own(PACKFILE *f, const ZqPartContext &ctx)
{
    if(ctx.zelda_version < 0x255 || ctx.version != 1)
        return ZQW_UNSUPPORTED;

    if(zq255.music.size() > 65535)
    {
        zqw_last_error = "the quest has more than 65,535 music objects";
        return 2;
    }

    std::vector<byte> data, o;
    encode(data);
    put16(o, ctx.version);
    put16(o, ctx.cversion);
    put32(o, (unsigned)data.size());
    o.insert(o.end(), data.begin(), data.end());

    if(pack_mputl((long)ID_ADVMUSIC_, f) == EOF || pack_fwrite(&o[0], (long)o.size(), f) != (long)o.size())
        return 1;

    return ZQW_OK;
}

ZqWriterRegistration g_reg(ID_ADVMUSIC_, write_amus_own, "music objects (AMUS) v1, 3.0");

// A screen's music as it stands: its screen data edit, or as loaded (0: none of its own).
long screen_music_now(int index)
{
    std::map<int, std::map<std::string, TapeField> >::const_iterator it = zq255.screen_fields.find(index);

    if(it != zq255.screen_fields.end())
    {
        std::map<std::string, TapeField>::const_iterator f = it->second.find("music");

        if(f != it->second.end())
            return (long)f->second.value;
    }

    std::map<int, long>::const_iterator m = zq255.screen_music.find(index);
    return m == zq255.screen_music.end() ? 0 : m->second;
}

} // namespace

// True when the open quest keeps music objects of its own (a 3.0 quest whose file has AMUS).
bool zq255_has_music_objects()
{
    return zq255_quest_is_newer() && has_amus_part();
}

// After music objects were deleted or reordered (changes: old number -> new number, 0 for a
// deleted one): every DMap and screen that plays a moved one is set to its new number, as
// upstream's update_quest_music does. False (with why) if one of them couldn't be changed.
bool zq255_music_remap(const std::map<int, int> &changes, std::string &why)
{
    std::map<int, int> ch;

    for(std::map<int, int>::const_iterator it = changes.begin(); it != changes.end(); ++it)
        if(it->first != it->second && it->first > 0)
            ch[it->first] = it->second;

    if(ch.empty())
        return true;

    // DMaps that name a music object (DMAP v25 and up)
    std::vector<int> dmaps;

    for(std::map<int, std::pair<int, bool> >::const_iterator it = zq255.dmap_music.begin(); it != zq255.dmap_music.end(); ++it)
        if(it->second.second && ch.count(it->second.first))
            dmaps.push_back(it->first);

    for(size_t k = 0; k < dmaps.size(); ++k)
    {
        const int i = dmaps[k];
        std::vector<byte> rec;
        int v = 0;

        if(!zq255_dmap_record(i, rec, &v))
        {
            why = "a DMap's record couldn't be read";
            return false;
        }

        Tape t;
        TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);
        TapeField *f = NULL;

        if(!zq_tape_dmap(p, v) || p.pos != rec.size() || !(f = t.find("music")))
        {
            why = "a DMap's record doesn't read as expected";
            return false;
        }

        f->value = ch[(int)f->value];
        std::vector<byte> out;
        t.encode(out);

        if(!zq255_dmap_commit(i, out, why))
            return false;
    }

    // screens that play a music object (MAP v37 and up)
    std::vector<int> screens;

    for(std::map<int, long>::const_iterator it = zq255.screen_music.begin(); it != zq255.screen_music.end(); ++it)
        if(ch.count((int)screen_music_now(it->first)))
            screens.push_back(it->first);

    for(std::map<int, std::map<std::string, TapeField> >::const_iterator it = zq255.screen_fields.begin(); it != zq255.screen_fields.end(); ++it)
        if(!zq255.screen_music.count(it->first) && it->second.count("music") && ch.count((int)screen_music_now(it->first)))
            screens.push_back(it->first);

    for(size_t k = 0; k < screens.size(); ++k)
    {
        const int index = screens[k];
        const long now = screen_music_now(index);
        std::vector<byte> rec;
        int v = 0;

        if(!zq255_screen_record(index, rec, &v))
            continue;

        Tape t;
        TapeParser p(rec.empty() ? NULL : &rec[0], rec.size(), 0, &t);

        if(!zq_tape_screen(p, v))
        {
            why = "a screen's record doesn't read as expected";
            return false;
        }

        TapeField e;
        std::map<std::string, TapeField> &edits = zq255.screen_fields[index];
        std::map<std::string, TapeField>::const_iterator had = edits.find("music");

        if(had != edits.end())
            e = had->second;
        else if(const TapeField *f = t.find("music"))
            e = *f;
        else
        {
            // the screen's music group is off in its record: the field as the schema lays it
            Tape r;
            TapeField *has = t.find("has");

            if(has)
                has->value |= 0xFFFF;

            TapeParser rp(&t, &r);
            zq_tape_screen(rp, v);
            const TapeField *g = r.find("music");

            if(!g)
                continue;

            e = *g;
        }

        e.value = ch[(int)now];
        edits["music"] = e;
        zq255_screen_fields_sync(index, true, false);
    }

    return true;
}

// -qst-dump-record q music 0 out (zq_tests.cpp): the music objects and who plays them.
void zq_music_dump(FILE *f)
{
    fprintf(f, "music objects: %d (%s)\n", (int)zq255.music.size(), has_amus_part() ? "AMUS in the file" : "no AMUS section");

    for(size_t q = 0; q < zq255.music.size(); ++q)
    {
        const Zq255Store::Music &m = zq255.music[q];
        fprintf(f, "  %d: name \"%s\" midi %d path \"%s\"", (int)q + 1, m.name.c_str(), (int)m.midi, m.path.c_str());

        if(!m.path.empty())
            fprintf(f, " track %d loop %ld-%ld xfade %ld/%ld", (int)m.track, (long)m.loop_start, (long)m.loop_end,
                    (long)m.xfade_in, (long)m.xfade_out);

        fprintf(f, "\n");
    }

    for(std::map<int, std::pair<int, bool> >::const_iterator it = zq255.dmap_music.begin(); it != zq255.dmap_music.end(); ++it)
        if(it->second.first)
            fprintf(f, "  DMap %d: music %d%s\n", it->first, it->second.first, it->second.second ? "" : " (its own)");

    std::map<int, long> scr = zq255.screen_music;

    for(std::map<int, std::map<std::string, TapeField> >::const_iterator it = zq255.screen_fields.begin(); it != zq255.screen_fields.end(); ++it)
        if(it->second.count("music"))
            scr[it->first] = screen_music_now(it->first);

    for(std::map<int, long>::const_iterator it = scr.begin(); it != scr.end(); ++it)
        if(it->second)
            fprintf(f, "  screen %d (map %d, 0x%02X): music %ld\n", it->first, it->first / MAPSCRS + 1, it->first % MAPSCRS,
                    (long)it->second);
}
